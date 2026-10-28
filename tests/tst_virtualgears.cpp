// Virtual gears end to end: SensorHub + the virtual trainer ("sim:trainer",
// bike in the reference gear) + a virtual Zwift Click + FreeRideSession and
// RlvSession, wired like DeviceManager / main.cpp.
//
// Regression tests for:
//  * Free Ride "Ride": the gear changed but the watts did not (the page sent
//    no resistance before Start and the virtual trainer then produced its own
//    wandering free-ride power).
//  * Video Ride "Simulation": watts lower with a harder gear / "like the
//    wind" (same idle problem; descents: negative road power made the
//    gear-adjusted grade drop with a harder gear).
// At a constant cadence a harder gear must give more power and speed, an
// easier one less, on both pages, for path a (gear-adjusted grade) and path
// b (ERG at the gear's road power); no Set Target Power in Ride/Simulation
// on a simulating trainer; ERG -> Ride releases ERG; the Click shifts like
// the on-screen buttons.
//  * Real trainer (JetBlack Victory, 1.3.1): 44×44 needed ~200 W at the start
//    of a descent - path a sent a negative grade for the 33 km/h flywheel of
//    the 50×17 reference gear (root cause still being investigated). The
//    default method is "Gradient" (path a, simulation); the optional,
//    experimental "Auto" sends a low ERG target for light loads (hybrid* tests).
#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RideResistance.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"
#include "RlvSession.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"
#include "VirtualGears.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

using namespace watt;

namespace {

const double kMPerDegLat = RlvRoute::haversine(0, 0, 1, 0);

// Straight route north, one point per 10 m; grade(d) in %.
bool writeRoute(const QString &dir, const QString &name, double lengthM, const std::function<double(double)> &grade)
{
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><trkseg>\n";
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("1999-12-31T23:00:00Z"), Qt::ISODate);
    const double step = 10.0;
    double ele = 300, d = 0;
    for (int i = 0; d <= lengthM + 1e-6; ++i, d += step) {
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.0\"><ele>%2</ele><time>%3</time></trkpt>\n")
                   .arg(45.0 + d / kMPerDegLat, 0, 'f', 8)
                   .arg(ele, 0, 'f', 3)
                   .arg(t0.addSecs(i).toString(Qt::ISODate))
                   .toUtf8();
        ele += grade(d + step / 2) / 100.0 * step;
    }
    gpx += "</trkseg></trk></gpx>\n";
    QFile g(dir + QLatin1Char('/') + name + QStringLiteral(".gpx"));
    QFile v(dir + QLatin1Char('/') + name + QStringLiteral(".mp4"));
    if (!g.open(QIODevice::WriteOnly) || !v.open(QIODevice::WriteOnly))
        return false;
    g.write(gpx);
    v.write("not a real video");
    return true;
}

// rolling start, a steady 3 % climb (300..2000 m), then a -6 % descent
double routeGrade(double d)
{
    if (d < 300)
        return d / 300.0 * 3.0;
    if (d < 2000)
        return 3.0;
    return -6.0;
}

struct Rig
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<FreeRideSession> freeRide;
    std::unique_ptr<RlvLibrary> library;
    std::unique_ptr<RlvSession> rlv;
    std::unique_ptr<SimulatedTrainer> trainer;
    std::unique_ptr<SimulatedController> click;
    QStringList commands;   // what the trainer was told

    bool init(bool trainerSimulates = true)
    {
        if (!dir.isValid() || !db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(600000);   // the test clock is virtual
        gears = std::make_unique<VirtualGears>(&db, profiles.get());
        gears->setManualSave(true);
        gears->setHub(hub.get());   // rider + bike -> SensorHub::riderMassKg (main.cpp)
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        freeRide->setManualClock(true);
        library = std::make_unique<RlvLibrary>(&db);
        library->setLibraryFolder(dir.path());
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), library.get());
        rlv->setManualClock(true);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setStartBlocker(QStringLiteral("freeRide"), [this] { return freeRide->active(); });
        freeRide->setStartBlocker(QStringLiteral("rlv"), [this] { return rlv->active(); });
        gears->setSessions(freeRide.get(), rlv.get());

        // the virtual trainer, wired like DeviceManager::attachTrainer()
        trainer = std::make_unique<SimulatedTrainer>();
        trainer->setNoise(false);
        trainer->setSimulationSupported(trainerSimulates);
        trainer->setMassKg(hub->riderMassKg());
        QObject::connect(hub.get(), &SensorHub::riderMassChanged, trainer.get(),
                         [this] { trainer->setMassKg(hub->riderMassKg()); });
        QObject::connect(trainer.get(), &TrainerDevice::powerMeasured, hub.get(), &SensorHub::updatePower);
        QObject::connect(trainer.get(), &TrainerDevice::cadenceMeasured, hub.get(), &SensorHub::updateCadence);
        QObject::connect(trainer.get(), &TrainerDevice::speedMeasured, hub.get(), &SensorHub::updateSpeed);
        QObject::connect(hub.get(), &SensorHub::targetPowerRequested, trainer.get(), [this](int w) {
            commands << QStringLiteral("erg %1").arg(w);
            trainer->setTargetPower(w);
        });
        QObject::connect(hub.get(), &SensorHub::freeRideRequested, trainer.get(), [this] {
            commands << QStringLiteral("free");
            trainer->setFreeRide();
        });
        QObject::connect(hub.get(), &SensorHub::simulationRequested, trainer.get(),
                         [this](double g, double w, double crr, double cw) {
                             commands << QStringLiteral("sim %1").arg(g);
                             trainer->setSimulation(g, w, crr, cw);
                         });
        hub->setTrainerConnected(true);
        hub->setTrainerSimulationSupport(trainer->simulationSupport());

        // a Zwift Click: virtual gears on, the bike stays in the reference
        // gear (main.cpp: syncSimGear)
        click = std::make_unique<SimulatedController>();
        click->connectDevice();
        QObject::connect(click.get(), &ControllerDevice::shiftRequested, gears.get(), &VirtualGears::controllerShift);
        gears->setEnabled(true);
        trainer->setFixedGearRatio(gears->referenceRatio());
        setGear(17);
        // the rider pedals before anything starts
        for (int i = 0; i < 8; ++i)
            trainer->step();
        return hub->cadence() > 80;
    }
    // a light virtual gear like the reported 44×44 (ratio 1.0): Zwift gear 3
    // (0.99), the real bike still in 50×17 on the trainer
    bool lightBike()
    {
        return gears->referenceLabel() == QStringLiteral("50×17") && gears->setGearIndex(2)
               && gears->gearText() == QStringLiteral("Gear 3/24 (0.99)");
    }
    // the Zwift gear nearest to 50×cog (Zwift's table has no chainrings /
    // cogs): 27 -> 9 (1.86), 21 -> 12 (2.40), 17 -> 15 (3.03), 14 -> 17
    // (3.49), 11 -> 21 (4.54)
    bool setGear(int cog)
    {
        const double want = 50.0 / cog;
        int best = 0;
        for (int i = 1; i < zwiftgears::kCount; ++i)
            if (std::abs(zwiftgears::kRatios[i] - want) < std::abs(zwiftgears::kRatios[best] - want))
                best = i;
        return gears->setGearIndex(best);
    }
    bool addRoute()
    {
        const QString name = QStringLiteral("hills");
        return QDir(dir.path()).mkpath(name) && writeRoute(dir.filePath(name), name, 3500, routeGrade)
               && rlv->selectPath(dir.filePath(name));
    }
    // one simulated second: the trainer, then both sessions in 250 ms steps
    void second()
    {
        trainer->step();
        for (int i = 0; i < 4; ++i) {
            freeRide->advanceClock(250);
            rlv->advanceClock(250);
        }
    }
    // rides n seconds, returns the mean power of the last `last` seconds
    double ride(int n, int last = 3)
    {
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            second();
            if (i >= n - last)
                sum += hub->power();
        }
        return sum / std::min(n, last);
    }
};

struct Point
{
    QString gear;
    double watts;
    double speedKmh;
};

QString describe(const QVector<Point> &points)
{
    QStringList out;
    for (const Point &p : points)
        out << QStringLiteral("%1: %2 W %3 km/h").arg(p.gear).arg(p.watts, 0, 'f', 0).arg(p.speedKmh, 0, 'f', 1);
    return out.join(QStringLiteral(" | "));
}

// Shifts `count` times in `direction` with the Click (one shift per 6 s) and
// records the steady power / session speed in each gear.
QVector<Point> shiftAndMeasure(Rig &r, int direction, int count, const std::function<double()> &speedKmh)
{
    QVector<Point> points;
    points.append({ r.gears->gearLabel(), r.ride(6), speedKmh() });
    for (int i = 0; i < count; ++i) {
        r.click->pressButton(direction);
        points.append({ r.gears->gearLabel(), r.ride(6), speedKmh() });
    }
    return points;
}

bool monotonic(const QVector<Point> &points, int direction, double minStepWatts = 3.0)
{
    for (int i = 1; i < points.size(); ++i) {
        const double dw = (points.at(i).watts - points.at(i - 1).watts) * direction;
        const double dv = (points.at(i).speedKmh - points.at(i - 1).speedKmh) * direction;
        if (dw < minStepWatts || dv <= 0.05)
            return false;
    }
    return true;
}

} // namespace

struct SoftStartOn
{
    SoftStartOn() { RideResistance::setSoftStartEnabled(true); }
    ~SoftStartOn() { RideResistance::setSoftStartEnabled(false); }
};

class TestVirtualGears : public QObject
{
    Q_OBJECT
private slots:
    // These tests are about physics / rate limits / paths: the soft start
    // (0 % until the flywheel turns, then a ramp) has its own tests below
    // and in tst_virtualgears (softStart*).
    void initTestCase() { RideResistance::setSoftStartEnabled(false); }

    // Soft start on the virtual-gears FTMS path (gear-adjusted grade): 0 %
    // while standing / before the flywheel turns, then the ramp; rate limits
    // stay (one grade per kMinGradeIntervalMs).
    void softStartGearSimulation()
    {
        SoftStartOn on;
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        hub.setTrainerConnected(true);
        hub.setTrainerSimulationSupport(1);
        RideResistance res(&hub);
        RideResistance::GearDrive d;
        d.road.massKg = 80;
        d.road.gradePercent = 4;
        d.virtualRatio = 50.0 / 15;
        d.referenceRatio = 50.0 / 17;
        // standing: gearGrade() is 0 % now (not the road's 4 %)
        QCOMPARE(RideResistance::gearGrade(d), 0.0);
        QVERIFY(res.sendGrade(RideResistance::gearGrade(d), true, RideResistance::trainerModel(d)));
        QCOMPARE(hub.requestedGrade(), 0.0);
        // pedaling at 90 rpm: the full gear grade is > 4 %, but 0 % for 2 s
        hub.updateCadence(90);
        d.cadenceRpm = 90;
        const double full = RideResistance::gearGrade(d);
        QVERIFY(full > 4.0);
        QVector<double> sent;
        int ms = 0;
        for (; ms < 2000; ms += 250) {
            res.elapse(250);
            res.sendGrade(full, false, RideResistance::trainerModel(d));
            QCOMPARE(hub.requestedGrade(), 0.0);
        }
        QVERIFY(res.softStart() == RideResistance::SoftStart::Ramping);
        double prev = 0;
        qint64 lastChange = -1;
        for (int t = 0; t < RideResistance::kSoftStartRampMs + 2000; t += 250) {
            res.elapse(250);
            res.sendGrade(full, false, RideResistance::trainerModel(d));
            const double g = hub.requestedGrade();
            QVERIFY(g >= prev);
            if (g != prev) {
                QVERIFY2(lastChange < 0 || t - lastChange >= RideResistance::kMinGradeIntervalMs,
                         qPrintable(QStringLiteral("%1 ms").arg(t - lastChange)));
                lastChange = t;
                sent << g;
            }
            prev = g;
        }
        QCOMPARE(hub.requestedGrade(), std::round(full * 100) / 100);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Done);
        QVERIFY2(sent.size() >= 3, qPrintable(QString::number(sent.size())));
        // latest wins: a new target during the ramp is followed (scaled)
        res.reset();
        QCOMPARE(res.softStartGrade(8.0), 0.0);
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs / 2; t += 250)
            res.elapse(250);
        QVERIFY(std::abs(res.softStartGrade(8.0) - 4.0) < 0.3);
        QVERIFY(std::abs(res.softStartGrade(-4.0) + 2.0) < 0.2);
        // re-armed after > 5 s without cadence
        for (int t = 0; t < RideResistance::kSoftStartRampMs; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Done);
        hub.updateCadence(0);
        for (int t = 0; t <= RideResistance::kSoftStartRearmMs; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Waiting);
        QCOMPARE(res.softStartFactor(), 0.0);
        // a short coast (< 5 s) does not re-arm
        hub.updateCadence(90);
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Done);
        hub.updateCadence(0);
        for (int t = 0; t < 4000; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Done);
        // ERG commands are never touched
        QVERIFY(res.sendErg(200, true));
        QCOMPARE(hub.requestedTarget(), 200);
        res.reset();
        QVERIFY(res.sendGearWatts(150, true));
        QCOMPARE(hub.requestedTarget(), 150);
    }

    // The Zwift hub path (trainer-side physics): the incline gets the same
    // soft start, the gear ratio / weights go out unchanged.
    void softStartHubIncline()
    {
        SoftStartOn on;
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        hub.setTrainerConnected(true);
        RideResistance res(&hub);
        RideResistance::GearDrive d;
        d.road.massKg = 80;
        d.road.gradePercent = 6;
        d.virtualRatio = 50.0 / 15;
        d.referenceRatio = 50.0 / 17;
        d.riderKg = 72;
        res.sendHub(d);
        QCOMPARE(hub.hubSimulation().inclinePercent, 0.0);
        // the flat-road part of the soft start: a light gear (half the ratio)
        QCOMPARE(hub.hubSimulation().gearRatio, d.virtualRatio * RideResistance::kHubLightGearFactor);
        QCOMPARE(hub.hubSimulation().riderKg, 72.0);
        hub.updateCadence(85);
        d.cadenceRpm = 85;
        QVector<double> inclines, gears;
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs + 500; t += 250) {
            res.elapse(250);
            res.sendHub(d);
            inclines << hub.hubSimulation().inclinePercent;
            gears << hub.hubSimulation().gearRatio;
        }
        // the gear ramps with the incline, never down, in a few steps
        QCOMPARE(gears.constFirst(), d.virtualRatio * RideResistance::kHubLightGearFactor);
        QCOMPARE(gears.constLast(), d.virtualRatio);
        int gearSteps = 0;
        for (int i = 1; i < gears.size(); ++i) {
            QVERIFY(gears.at(i) >= gears.at(i - 1) - 1e-9);
            if (gears.at(i) > gears.at(i - 1) + 1e-9)
                ++gearSteps;
        }
        QVERIFY(gearSteps >= 5 && gearSteps <= 10);   // 10 ramp steps, ticks may merge some
        // 0 % for 2 s, then a linear ramp to 6 % over 4 s
        for (int i = 0; i < 7; ++i)
            QCOMPARE(inclines.at(i), 0.0);
        QCOMPARE(inclines.constLast(), 6.0);
        int between = 0;
        for (int i = 1; i < inclines.size(); ++i) {
            QVERIFY(inclines.at(i) >= inclines.at(i - 1));
            QVERIFY(inclines.at(i) - inclines.at(i - 1) <= 6.0 * 250 / RideResistance::kSoftStartRampMs + 0.011);
            if (inclines.at(i) > 0 && inclines.at(i) < 6.0)
                ++between;
        }
        QVERIFY(between >= 10);
        // the trainer gets the ramp too (requestedGrade = incline)
        QCOMPARE(hub.requestedGrade(), 6.0);
    }

    // Zwift hub path, real gear unknown: the light gear stays (no timeout to
    // the full ratio); every change of the basis ramps FROM THE RATIO THE
    // TRAINER HAS - also long after the soft start's own ramp ended.
    void hubGearTransitionsRamp()
    {
        SoftStartOn on;
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        hub.setTrainerConnected(true);
        RideResistance res(&hub);
        RideResistance::GearDrive d;
        d.road.massKg = 77;
        d.virtualRatio = 2.40;
        d.riderKg = 68;
        d.hubGearKnown = false;
        hub.updateCadence(80);
        d.cadenceRpm = 80;
        const double light = 2.40 * RideResistance::kHubLightGearFactor;
        auto tick = [&](int ms = 250) {
            res.elapse(ms);
            res.sendHub(d);
            return hub.hubSimulation().gearRatio;
        };
        // unknown, long after the soft start ramp: still light - no jump to 2.40
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs + 30000; t += 250)
            QCOMPARE(tick(), light);
        QCOMPARE(res.softStart(), RideResistance::SoftStart::Done);
        // first accepted measurement: 1.20 -> 1.49 ramped, monotone, no jump
        d.hubGearKnown = true;
        d.hubGearFactor = 0.62;
        const double target = 2.40 * 0.62;
        QVector<double> sent;
        for (int t = 0; t <= RideResistance::kHubGearRampMs + 500; t += 250)
            sent << tick();
        QVERIFY(std::abs(sent.constFirst() - light) < 1e-9);   // starts at the last requested ratio
        QVERIFY(std::abs(sent.constLast() - target) < 1e-9);
        for (int i = 1; i < sent.size(); ++i) {
            QVERIFY(sent.at(i) >= sent.at(i - 1) - 1e-9);
            QVERIFY(sent.at(i) - sent.at(i - 1) <= (target - light) * 0.1 + 1e-9);   // steps of 1/10
        }
        // the factor changes while ramping: re-anchored on the actual ratio
        d.hubGearFactor = 0.62;
        d.hubGearKnown = true;
        res.reset();   // (Waiting: at once)
        tick();
        // ... settle first while pedaling
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs + RideResistance::kHubGearRampMs + 1000; t += 250)
            tick();
        QVERIFY(std::abs(hub.hubSimulation().gearRatio - target) < 1e-9);
        d.hubGearFactor = 0.8;
        tick();
        tick();   // a step or two into the ramp towards 1.92
        const double mid = hub.hubSimulation().gearRatio;
        QVERIFY(mid > target && mid < 2.40 * 0.8);
        d.hubGearFactor = 0.5;   // changes again mid-ramp
        const double next = tick();
        QVERIFY(std::abs(next - mid) <= std::abs(2.40 * 0.5 - mid) * 0.1 + 1e-9);   // from mid, not from 1.92
        // the provisional value times out (known -> unknown): down to light, ramped
        for (int t = 0; t < RideResistance::kHubGearRampMs + 500; t += 250)
            tick();
        d.hubGearKnown = false;
        d.hubGearFactor = 1.0;   // compensation removed ...
        QVector<double> back;
        for (int t = 0; t <= RideResistance::kHubGearRampMs + 500; t += 250)
            back << tick();
        for (double r : back)
            QVERIFY2(r <= 2.40 * 0.5 + 1e-9, qPrintable(QString::number(r)));   // ... never the full 2.40
        QVERIFY(std::abs(back.constLast() - light) < 1e-9);
        // a shift during a ramp acts at once (keeps the ramp position)
        d.hubGearKnown = true;
        d.hubGearFactor = 0.62;
        tick();
        tick();
        const double before = hub.hubSimulation().gearRatio;
        d.virtualRatio = 2.61;
        const double after = tick();
        QVERIFY(after > before * 1.05);
    }

    // Regression: a long coast with the flywheel above 10 km/h, 0 rpm and 0 W
    // (the trainer still reports cadence). "Turning" (speed >= 10 km/h) and
    // "stopped" (0 rpm) used to be true together: Waiting after 5.25 s,
    // Ramping after 7.25 s, Waiting after 10.5 s … - and the hub path's gear
    // with it. Now: re-armed once, stays Waiting, one gear change; pedaling
    // again ramps up once.
    void softStartLongCoastDoesNotCycle()
    {
        SoftStartOn on;
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        hub.setTrainerConnected(true);
        RideResistance res(&hub);
        RideResistance::GearDrive d;
        d.road.massKg = 77;
        d.virtualRatio = 2.40;
        d.riderKg = 68;
        d.hubGearKnown = true;
        d.hubGearFactor = 0.62;
        const double compensated = 2.40 * 0.62;
        const double light = 2.40 * RideResistance::kHubLightGearFactor;
        auto tick = [&] {
            res.elapse(250);
            res.sendHub(d);
            return hub.hubSimulation().gearRatio;
        };
        // pedaling until settled
        hub.updateCadence(80);
        hub.updatePower(120);
        hub.updateSpeed(30);
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs + RideResistance::kHubGearRampMs + 1000; t += 250)
            tick();
        QCOMPARE(res.softStart(), RideResistance::SoftStart::Done);
        QVERIFY(std::abs(hub.hubSimulation().gearRatio - compensated) < 1e-9);
        // coast for 30 s: 0 rpm, 0 W, the flywheel slowly from 30 to 12 km/h
        hub.updateCadence(0);
        hub.updatePower(0);
        QVector<RideResistance::SoftStart> states;
        QVector<double> gears;
        for (int t = 0; t < 30000; t += 250) {
            hub.updateSpeed(30.0 - 18.0 * t / 30000.0);
            gears << tick();
            states << res.softStart();
        }
        int stateChanges = 0, gearChanges = 0;
        for (int i = 1; i < states.size(); ++i) {
            if (states.at(i) != states.at(i - 1))
                ++stateChanges;
            if (std::abs(gears.at(i) - gears.at(i - 1)) > 1e-9)
                ++gearChanges;
        }
        QCOMPARE(stateChanges, 1);   // Done -> Waiting (re-armed after > 5 s), no cycling
        QCOMPARE(states.constLast(), RideResistance::SoftStart::Waiting);
        QVERIFY(!states.contains(RideResistance::SoftStart::Ramping));
        QCOMPARE(gearChanges, 1);    // compensated -> light, once (not pedaling: at once)
        QVERIFY(std::abs(gears.constFirst() - compensated) < 1e-9);
        QVERIFY(std::abs(gears.constLast() - light) < 1e-9);
        // pedaling again: Waiting for kSoftStartTurnMs, then one ramp up
        hub.updateCadence(80);
        hub.updatePower(120);
        hub.updateSpeed(25);
        QVector<double> up;
        QVector<RideResistance::SoftStart> upStates;
        for (int t = 0; t < 2000 + RideResistance::kSoftStartRampMs + RideResistance::kHubGearRampMs + 1000; t += 250) {
            up << tick();
            upStates << res.softStart();
        }
        QCOMPARE(upStates.constLast(), RideResistance::SoftStart::Done);
        QVERIFY(std::abs(up.constFirst() - light) < 1e-9);
        QVERIFY(std::abs(up.constLast() - compensated) < 1e-9);
        for (int i = 1; i < up.size(); ++i)
            QVERIFY(up.at(i) >= up.at(i - 1) - 1e-9);   // monotone: one ramp, no back and forth
        int waitingAgain = 0;
        for (int i = 1; i < upStates.size(); ++i)
            if (upStates.at(i) == RideResistance::SoftStart::Waiting && upStates.at(i - 1) != RideResistance::SoftStart::Waiting)
                ++waitingAgain;
        QCOMPARE(waitingAgain, 0);
        // the start's lagging cadence (0 rpm, power > 0, speed rising) still
        // counts as turning
        res.reset();
        hub.updateCadence(0);
        hub.updatePower(180);
        hub.updateSpeed(15);
        for (int t = 0; t < 2500; t += 250)
            tick();
        QVERIFY(res.softStart() != RideResistance::SoftStart::Waiting);
    }

    // A trainer that reports neither cadence nor speed: power >= 50 W for 2 s.
    void softStartPowerOnlyTrainer()
    {
        SoftStartOn on;
        SensorHub hub;
        hub.setStaleTimeoutMs(600000);
        hub.setTrainerConnected(true);
        RideResistance res(&hub);
        hub.updatePower(30);
        for (int t = 0; t < 3000; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Waiting);
        hub.updatePower(150);
        for (int t = 0; t < RideResistance::kSoftStartTurnMs; t += 250)
            res.elapse(250);
        QVERIFY(res.softStart() == RideResistance::SoftStart::Ramping);
        // and the switch: off = always the full grade
        RideResistance::setSoftStartEnabled(false);
        res.reset();
        QCOMPARE(res.softStartGrade(5.0), 5.0);
        RideResistance::setSoftStartEnabled(true);
        QCOMPARE(res.softStartGrade(5.0), 0.0);
    }
    // -- the shared physics -------------------------------------------------

    // -- rider weight -----------------------------------------------------------

    // A lighter rider pushes fewer watts in the same gear at the same cadence:
    // much fewer on a climb (mass), noticeably fewer on the flat (auto CdA).
    void lighterRiderNeedsFewerWatts()
    {
        Rig r;
        QVERIFY(r.init(true));
        auto wattsAt = [&](double riderKg, double grade, int front, int rear) {
            RideResistance::GearDrive d;
            Profile p = r.profiles->current();
            p.weightKg = riderKg;
            p.heightCm = 0;
            d.road = r.gears->roadModel(grade, p);
            d.cadenceRpm = 90;
            d.virtualRatio = double(front) / rear;
            d.referenceRatio = r.gears->referenceRatio();
            d.circumferenceMm = r.gears->setup().circumferenceMm;
            return RideResistance::gearWatts(d);
        };
        for (int cog : { 21, 17, 14, 11 }) {
            const int light = wattsAt(60, 0, 50, cog), heavy = wattsAt(90, 0, 50, cog);
            QVERIFY2(light < heavy * 0.92, qPrintable(QStringLiteral("flat 50x%1: %2 vs %3 W").arg(cog).arg(light).arg(heavy)));
            const int lightUp = wattsAt(60, 5, 50, cog), heavyUp = wattsAt(90, 5, 50, cog);
            QVERIFY2(lightUp < heavyUp * 0.8,
                     qPrintable(QStringLiteral("5 % 50x%1: %2 vs %3 W").arg(cog).arg(lightUp).arg(heavyUp)));
        }
        // the reported gear on the flat: realistic road power for 77 kg + 9 kg
        QVERIFY2(std::abs(wattsAt(77, 0, 50, 14) - 320) < 20, qPrintable(QString::number(wattsAt(77, 0, 50, 14))));

        // end to end on the virtual trainer (path a): the profile weight
        // changes what the rider pushes at the same gear and cadence
        QVERIFY(r.setGear(17));
        r.freeRide->setGrade(4.0);
        QVERIFY(r.profiles->saveProfile({ { QStringLiteral("weight"), 60.0 } }));
        QVERIFY(r.freeRide->start());
        const double light = r.ride(12);
        const double lightCad = r.hub->cadence();
        r.freeRide->finish(false);
        QVERIFY(r.profiles->saveProfile({ { QStringLiteral("weight"), 90.0 } }));
        QCOMPARE(r.trainer->massKg(), 99.0);   // the trainer's weight follows the profile
        QVERIFY(r.freeRide->start());
        const double heavy = r.ride(12);
        r.freeRide->finish(false);
        QVERIFY(std::abs(r.hub->cadence() - lightCad) < 2);   // same cadence (no rider limit here)
        QVERIFY2(light < heavy * 0.8, qPrintable(QStringLiteral("%1 vs %2 W").arg(light).arg(heavy)));
    }

    // FTMS 0x11 gets Cw = ρ·CdA of the rider (not Zwift's 0.51) and the
    // virtual trainer simulates the hub's rider + bike.
    void simulationParametersFollowTheRider()
    {
        Rig r;
        QVERIFY(r.init(true));
        const double weight = r.profiles->weight();
        QCOMPARE(r.hub->riderMassKg(), weight + 9.0);
        QCOMPARE(r.trainer->massKg(), weight + 9.0);
        r.freeRide->setGrade(2.0);
        QVERIFY(r.freeRide->start());
        r.ride(4);
        QVERIFY(r.hub->simulationActive());
        const double cda = road::autoCda(weight);
        QCOMPARE(r.hub->simulationCw(), std::round(1.225 * cda * 100) / 100);
        QVERIFY(r.hub->simulationCw() < 0.45);
        QCOMPARE(r.hub->simulationCrr(), 0.004);
        // a weight edit goes out at once with new parameters (and trainer weight)
        QVERIFY(r.profiles->saveProfile({ { QStringLiteral("weight"), 95.0 } }));
        r.ride(4);
        QCOMPARE(r.hub->riderMassKg(), 104.0);
        QCOMPARE(r.trainer->massKg(), 104.0);
        QCOMPARE(r.hub->simulationCw(), std::round(1.225 * road::autoCda(95) * 100) / 100);
        // manual CdA
        QVERIFY(r.gears->setBikePhysics(9.0, 0.25, 0.005, false));
        r.ride(4);
        QCOMPARE(r.hub->simulationCw(), 0.31);
        QCOMPARE(r.hub->simulationCrr(), 0.005);
        // real gears (plain grade) send the rider's parameters too
        r.gears->setEnabled(false);
        r.trainer->setFixedGearRatio(0);
        r.ride(4);
        QCOMPARE(r.freeRide->resistancePath(), QStringLiteral("simulation"));
        QCOMPARE(r.hub->requestedGrade(), 2.0);
        QCOMPARE(r.hub->simulationCw(), 0.31);
        r.freeRide->finish(false);
    }

    // Path a round trip: the virtual trainer, braking with exactly the sent
    // grade / Crr / Cw and the hub's mass at the reference gear's speed,
    // delivers the app's road power for the virtual gear.
    void pathARoundTrip()
    {
        Rig r;
        QVERIFY(r.init(true));
        const double vRef = 90 / 60.0 * r.gears->referenceRatio() * r.gears->setup().circumferenceMm / 1000.0;
        QCOMPARE(SimulatedTrainer::kWheelCircumferenceM, r.gears->setup().circumferenceMm / 1000.0);
        for (double kg : { 55.0, 75.0, 95.0 }) {
            QVERIFY(r.profiles->saveProfile({ { QStringLiteral("weight"), kg } }));
            for (double grade : { 0.0, 3.0, 6.0 }) {
                for (int cog : { 24, 19, 15, 12 }) {
                    RideResistance::GearDrive d;
                    d.road = r.gears->roadModel(grade);
                    d.cadenceRpm = 90;
                    d.virtualRatio = 50.0 / cog;
                    d.referenceRatio = r.gears->referenceRatio();
                    d.circumferenceMm = r.gears->setup().circumferenceMm;
                    const double sent = std::round(RideResistance::gearGrade(d) * 100) / 100;   // as sent
                    if (sent <= RideResistance::kMinGrade || sent >= RideResistance::kMaxGrade)
                        continue;
                    const road::TrainerModel tm = RideResistance::trainerModel(d);
                    SimulatedTrainer t;
                    t.setMassKg(r.hub->riderMassKg());
                    t.setSimulation(sent, 0, tm.crr, tm.cwKgPerM);
                    // the brake takes the wheel power: crank power (gearWatts) · η
                    // (power convention, Gearing.h)
                    const double want = RideResistance::gearWatts(d) * d.road.efficiency;
                    QVERIFY2(std::abs(t.brakePower(vRef) - want) < std::max(2.0, want * 0.01),
                             qPrintable(QStringLiteral("%1 kg %2 %% 50x%3: %4 vs %5 W")
                                            .arg(kg).arg(grade).arg(cog).arg(t.brakePower(vRef)).arg(want)));
                    // ... and reports crank power: the virtual gear's watts
                    const double crank = RideResistance::gearWatts(d);
                    QVERIFY2(std::abs(t.crankPower(vRef) - crank) < std::max(2.0, crank * 0.01),
                             qPrintable(QStringLiteral("%1 kg %2 %% 50x%3: crank %4 vs %5 W")
                                            .arg(kg).arg(grade).arg(cog).arg(t.crankPower(vRef)).arg(crank)));
                }
            }
        }
    }

    // The simulated rider has an FTP: a far too hard gear makes the cadence
    // drop (power capped near his capacity), an easy one makes him spin.
    void simRiderCadenceDropsInBigGears()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.trainer->setRiderFtp(200);
        r.freeRide->setGrade(5.0);
        QVERIFY(r.setGear(11));
        QVERIFY(r.freeRide->start());
        r.ride(10);
        QVERIFY2(r.trainer->cadenceRpm() < 70, qPrintable(QString::number(r.trainer->cadenceRpm())));
        QVERIFY(r.trainer->cadenceRpm() >= SimulatedTrainer::kSimMinCadenceRpm - 0.5);
        QVERIFY2(r.hub->power() <= 200 * SimulatedTrainer::kBurstFactor + 15, qPrintable(QString::number(r.hub->power())));
        // the burst reserve drains: after a while only ~FTP is left
        r.ride(120);
        QVERIFY(r.trainer->riderReserve() < 0.2);
        QVERIFY2(r.hub->power() < 200 * 1.15, qPrintable(QString::number(r.hub->power())));
        // a hard but doable gear: close to the preferred cadence
        r.freeRide->setGrade(0.0);
        QVERIFY(r.setGear(21));
        r.ride(30);
        QVERIFY2(std::abs(r.trainer->cadenceRpm() - SimulatedTrainer::kSimCadenceRpm) < 6,
                 qPrintable(QString::number(r.trainer->cadenceRpm())));
        // the easiest gear on the flat: he spins up
        QVERIFY(r.gears->setGearIndex(0));
        r.ride(10);
        QVERIFY2(r.trainer->cadenceRpm() > 95, qPrintable(QString::number(r.trainer->cadenceRpm())));
        QVERIFY(r.trainer->cadenceRpm() <= SimulatedTrainer::kSimMaxCadenceRpm + 2.5);
        r.freeRide->finish(false);
        // no FTP: steady ~88 rpm whatever the gear (the other tests)
        SimulatedTrainer t;
        t.setNoise(false);
        t.setFixedGearRatio(50.0 / 17);
        t.setSimulation(8.0, 0, 0.004, 0.39);
        for (int i = 0; i < 10; ++i)
            t.step();
        QVERIFY(std::abs(t.cadenceRpm() - SimulatedTrainer::kSimCadenceRpm) < 3);
    }

    void gearResistanceIsMonotonic()
    {
        // path a / path b functions over gears, cadences and road grades
        // (including steep descents, where path a was inverted)
        GearSetup setup = GearSetup::defaults();
        const Gearing gearing(setup);
        const QVector<Gear> list = gearing.sequence();
        for (double grade : { -10.0, -6.0, -3.0, 0.0, 3.0, 8.0, 15.0 }) {
            for (double cadence : { 60.0, 88.0, 110.0 }) {
                double lastGrade = -1e9;
                int lastWatts = -1;
                double lastSpeed = -1;
                for (const Gear &g : list) {
                    RideResistance::GearDrive d;
                    d.road.massKg = 77;
                    d.road.gradePercent = grade;
                    d.cadenceRpm = cadence;
                    d.virtualRatio = g.ratio;
                    d.referenceRatio = setup.referenceRatio();
                    d.circumferenceMm = setup.circumferenceMm;
                    const double sent = RideResistance::gearGrade(d);
                    const int watts = RideResistance::gearWatts(d);
                    const double v = RideResistance::gearSpeed(d);
                    QVERIFY2(sent >= lastGrade - 1e-9, qPrintable(QStringLiteral("grade %1 cad %2 gear %3: %4 < %5")
                                                                     .arg(grade).arg(cadence).arg(g.ratio).arg(sent).arg(lastGrade)));
                    QVERIFY(watts >= lastWatts);
                    QVERIFY(v > lastSpeed);
                    QVERIFY(sent >= RideResistance::kMinGrade && sent <= RideResistance::kMaxGrade);
                    QVERIFY(watts >= RideResistance::kMinGearWatts);
                    lastGrade = sent;
                    lastWatts = watts;
                    lastSpeed = v;
                }
            }
        }
        // the grade comes from the cadence (v_ref = cadence · R_ref · C), not
        // from the trainer's speed: a slower flywheel does not change it
        RideResistance::GearDrive d;
        d.road.massKg = 77;
        d.road.gradePercent = 4;
        d.cadenceRpm = 88;
        d.virtualRatio = 50.0 / 14;
        d.referenceRatio = 50.0 / 17;
        d.trainerSpeedMps = 9.0;
        const double a = RideResistance::gearGrade(d);
        d.trainerSpeedMps = 6.0;
        QCOMPARE(RideResistance::gearGrade(d), a);
        // the reference gear on the flat: about the road (aero differs)
        d.virtualRatio = d.referenceRatio;
        d.road.gradePercent = 0;
        QVERIFY(std::abs(RideResistance::gearGrade(d)) < 1.5);
        // no cadence sensor: the trainer's speed stands in
        d.cadenceRpm = 0;
        d.trainerSpeedMps = 9.0;
        d.road.gradePercent = 3;
        d.virtualRatio = 50.0 / 14;
        const double bySpeed = RideResistance::gearGrade(d);
        QVERIFY(bySpeed > 3.0);
        QCOMPARE(RideResistance::gearWatts(d), 0);   // path b needs the cadence
        // coasting / standing (cadence reported, but < 15 rpm): 0 %, not the
        // road grade (soft start behaviour)
        d.cadenceRpm = 5;
        QCOMPARE(RideResistance::gearGrade(d), 0.0);
        QCOMPARE(RideResistance::gearGrade(d, 1.0, 10.0), 1.0);   // clamped like any grade
    }

    void pagesUseTheSamePhysics()
    {
        Rig r;
        QVERIFY(r.init());
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(800));
        QVERIFY(std::abs(r.rlv->effectiveGradePercent() - 3.0) < 0.01);
        r.freeRide->setGrade(3.0);
        for (int cog : { 27, 21, 17, 14, 11 }) {
            QVERIFY(r.setGear(cog));
            // (the route's 3 % is interpolated: equal within 0.001 %)
            QVERIFY(std::abs(r.rlv->gearSimulationGrade() - r.freeRide->gearSimulationGrade()) < 1e-3);
            QCOMPARE(r.rlv->gearTargetWatts(), r.freeRide->gearTargetWatts());
            QVERIFY(r.rlv->gearTargetWatts() > 0);
        }
    }

    // -- the virtual trainer -------------------------------------------------

    void simTrainerKeepsTheReferenceGear()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setFixedGearRatio(50.0 / 17);
        QSignalSpy power(&t, &TrainerDevice::powerMeasured);
        QSignalSpy speed(&t, &TrainerDevice::speedMeasured);
        // free ride (nothing commanded): the flywheel's basic resistance at
        // that speed - steady, no wandering free-ride power of its own
        for (int i = 0; i < 10; ++i)
            t.step();
        const int gear = t.gearIndex();
        QCOMPARE(t.riderGoal(), 0.0);   // no effort target
        int lo = 10000, hi = 0;
        for (int i = 0; i < 60; ++i) {
            t.step();
            lo = std::min(lo, power.last().at(0).toInt());
            hi = std::max(hi, power.last().at(0).toInt());
            QVERIFY(t.cadenceRpm() > 84 && t.cadenceRpm() < 92);
        }
        QVERIFY2(hi - lo <= 25, qPrintable(QStringLiteral("%1..%2 W").arg(lo).arg(hi)));
        QCOMPARE(t.gearIndex(), gear);
        QVERIFY(std::abs(speed.last().at(0).toDouble() / 3.6 - t.cadenceRpm() / 60.0 * 50.0 / 17 * SimulatedTrainer::kWheelCircumferenceM) < 0.3);
        // simulation: steeper (gear-adjusted) grade = more watts, same gear,
        // cadence only a little lower under a heavy load
        double last = 0;
        for (double grade : { 0.0, 2.0, 4.0, 8.0 }) {
            t.setSimulation(grade, 0, SensorHub::kDefaultCrr, SensorHub::kDefaultCw);
            for (int i = 0; i < 12; ++i)
                t.step();
            QVERIFY(power.last().at(0).toInt() > last + 30);
            last = power.last().at(0).toInt();
            QCOMPARE(t.gearIndex(), gear);
            QVERIFY(t.cadenceRpm() > 80);
        }
        // ERG: holds the target at that cadence
        t.setTargetPower(230);
        for (int i = 0; i < 12; ++i)
            t.step();
        QVERIFY(std::abs(power.last().at(0).toInt() - 230) <= 1);
        // virtual gears off: the rider shifts his own bike again
        t.setFixedGearRatio(0);
        QVERIFY(t.riderGoal() > 0);
    }

    // -- Free Ride -------------------------------------------------------------

    void freeRidePathA_powerFollowsGear()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: hybrid* tests)
        r.freeRide->setGrade(1.5);
        QSignalSpy targets(r.hub.get(), &SensorHub::targetPowerRequested);
        QVERIFY(r.freeRide->start());
        QCOMPARE(r.freeRide->resistancePath(), QStringLiteral("gearSimulation"));
        r.ride(8);
        auto speed = [&] { return r.freeRide->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 4, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 7, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
        QCOMPARE(targets.count(), 0);   // Ride on a simulating trainer: never ERG
        QVERIFY(!r.trainer->ergActive());
        // climbs: more watts in the same gear
        QVERIFY(r.setGear(17));
        const double flat = r.ride(8);
        r.freeRide->setGrade(5.0);
        QVERIFY2(r.ride(8) > flat + 60, qPrintable(QString::number(flat)));
        r.freeRide->finish(false);
    }

    void freeRidePathB_powerFollowsGear()
    {
        Rig r;
        QVERIFY(r.init(false));
        r.freeRide->setGrade(1.5);
        QSignalSpy sims(r.hub.get(), &SensorHub::simulationRequested);
        QVERIFY(r.freeRide->start());
        QCOMPARE(r.freeRide->resistancePath(), QStringLiteral("gearErg"));
        r.ride(8);
        auto speed = [&] { return r.freeRide->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 4, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 7, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
        QCOMPARE(sims.count(), 0);
        r.freeRide->finish(false);
    }

    // The reported bug: on the open Free Ride page, before Start, the gear
    // number changed but the watts did not.
    void freeRideIdlePageFollowsGear()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.freeRide->setGrade(1.5);
        r.gears->setActivePage(QStringLiteral("freeride"));
        QCOMPARE(r.freeRide->state(), QStringLiteral("idle"));
        QVERIFY(r.freeRide->gearHolding());
        QVERIFY(!r.freeRide->ergHolding());
        QVERIFY(r.trainer->simulating());
        auto speed = [&] { return r.freeRide->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 3, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 5, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
        QVERIFY(!r.commands.join(QLatin1Char(',')).contains(QStringLiteral("erg")));
        // leaving the page releases the trainer
        r.gears->setActivePage(QStringLiteral("dashboard"));
        QVERIFY(!r.freeRide->gearHolding());
        QCOMPARE(r.commands.last(), QStringLiteral("free"));
        // path b likewise (ERG at the gear's power)
        Rig b;
        QVERIFY(b.init(false));
        b.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(b.freeRide->gearHolding());
        auto speedB = [&] { return b.freeRide->speedKmh(); };
        const QVector<Point> upB = shiftAndMeasure(b, 1, 3, speedB);
        QVERIFY2(monotonic(upB, 1), qPrintable(describe(upB)));
    }

    void ergToRideReleasesErg()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: hybrid* tests)
        r.gears->setActivePage(QStringLiteral("freeride"));
        // idle: the ERG hold, then Ride -> the virtual gear (no ERG left)
        r.freeRide->setMode(QStringLiteral("erg"));
        r.freeRide->setTargetWatts(250);
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        r.freeRide->setMode(QStringLiteral("ride"));
        QVERIFY(!r.trainer->ergActive());
        QVERIFY(r.trainer->simulating());
        // running: ERG -> Ride
        QVERIFY(r.freeRide->start());
        r.freeRide->setMode(QStringLiteral("erg"));
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        QSignalSpy targets(r.hub.get(), &SensorHub::targetPowerRequested);
        r.freeRide->setMode(QStringLiteral("ride"));
        QVERIFY(!r.trainer->ergActive());
        QVERIFY(r.trainer->simulating());
        r.ride(10);
        QCOMPARE(targets.count(), 0);
        QVERIFY(std::abs(r.hub->power() - 250) > 10 || r.hub->requestedTarget() == 0);
        QCOMPARE(r.hub->requestedTarget(), 0);
        r.freeRide->finish(false);

        // Video Ride: ERG -> Simulation
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(800));
        r.gears->setActivePage(QStringLiteral("video"));
        r.rlv->setMode(QStringLiteral("erg"));
        QVERIFY(r.rlv->start());
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        targets.clear();
        r.rlv->setMode(QStringLiteral("sim"));
        QVERIFY(!r.trainer->ergActive());
        QVERIFY(r.trainer->simulating());
        r.ride(10);
        QCOMPARE(targets.count(), 0);
        r.rlv->finish(false);
    }

    // -- Video Ride ------------------------------------------------------------

    void rlvPathA_powerFollowsGearOnAFixedGrade()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: hybrid* tests)
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(400));
        r.rlv->setMode(QStringLiteral("sim"));
        QSignalSpy targets(r.hub.get(), &SensorHub::targetPowerRequested);
        QVERIFY(r.rlv->start());
        QCOMPARE(r.rlv->resistancePath(), QStringLiteral("gearSimulation"));
        r.ride(8);
        auto speed = [&] { return r.rlv->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 3, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 5, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
        QVERIFY2(std::abs(r.rlv->gradePercent() - 3.0) < 0.01, qPrintable(QString::number(r.rlv->distanceMeters())));
        QCOMPARE(targets.count(), 0);
        r.rlv->finish(false);
    }

    void rlvPathB_powerFollowsGearOnAFixedGrade()
    {
        Rig r;
        QVERIFY(r.init(false));
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(400));
        r.rlv->setMode(QStringLiteral("sim"));
        QVERIFY(r.rlv->start());
        QCOMPARE(r.rlv->resistancePath(), QStringLiteral("gearErg"));
        r.ride(8);
        auto speed = [&] { return r.rlv->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 3, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 5, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
        QVERIFY(std::abs(r.rlv->gradePercent() - 3.0) < 0.01);
        r.rlv->finish(false);
    }

    // On the open Video Ride page before Start the gear works too.
    void rlvIdlePageFollowsGear()
    {
        Rig r;
        QVERIFY(r.init(true));
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(800));
        r.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(r.rlv->gearHolding());
        QVERIFY(r.trainer->simulating());
        auto speed = [&] { return r.rlv->speedKmh(); };
        const QVector<Point> up = shiftAndMeasure(r, 1, 3, speed);
        QVERIFY2(monotonic(up, 1), qPrintable(describe(up)));
        const QVector<Point> down = shiftAndMeasure(r, -1, 5, speed);
        QVERIFY2(monotonic(down, -1), qPrintable(describe(down)));
    }

    // Descents: the road power of slow virtual speeds is negative; the
    // gear-adjusted grade must never drop with a harder gear.
    void rlvDescentIsNotInverted()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: hybrid* tests)
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(2200));
        QVERIFY(r.rlv->start());
        r.ride(4);
        QVERIFY(r.rlv->gradePercent() < -5.9);
        double last = -1e9;
        for (int i = 0; i < r.gears->gearCount(); ++i) {
            QVERIFY(r.gears->setGearIndex(i));
            const double g = r.rlv->gearSimulationGrade();
            QVERIFY2(g >= last - 1e-9, qPrintable(QStringLiteral("gear %1: %2 < %3").arg(i).arg(g).arg(last)));
            QCOMPARE(r.hub->requestedGrade(), std::round(g * 100.0) / 100.0);   // sent at once on the shift
            last = g;
        }
        r.rlv->finish(false);
    }


    // -- virtual shifting method / hybrid (real trainers) --------------------------

    void methodChoosesThePath()
    {
        SensorHub hub;
        hub.setTrainerConnected(true);
        hub.setTrainerSimulationSupport(1);
        hub.updateCadence(90);
        using M = RideResistance::GearMethod;
        using P = RideResistance::Path;
        QCOMPARE(RideResistance::choose(false, true, &hub, M::Auto), P::GearHybrid);
        QCOMPARE(RideResistance::choose(false, true, &hub, M::Gradient), P::GearSimulation);
        QCOMPARE(RideResistance::choose(false, true, &hub, M::Erg), P::GearErg);
        QCOMPARE(RideResistance::choose(false, true, &hub), P::GearSimulation);   // default: as before
        QCOMPARE(RideResistance::choose(true, true, &hub, M::Auto), P::Erg);
        QCOMPARE(RideResistance::choose(false, false, &hub, M::Auto), P::Simulation);
        hub.setTrainerSimulationSupport(0);
        QCOMPARE(RideResistance::choose(false, true, &hub, M::Auto), P::GearErg);
        QCOMPARE(RideResistance::choose(false, true, &hub, M::Gradient), P::GearErg);
        QCOMPARE(RideResistance::parseMethod(QStringLiteral("Auto")), std::optional<M>(M::Auto));
        QCOMPARE(RideResistance::parseMethod(QStringLiteral("gradient")), std::optional<M>(M::Gradient));
        QCOMPARE(RideResistance::parseMethod(QStringLiteral("erg")), std::optional<M>(M::Erg));
        QVERIFY(!RideResistance::parseMethod(QStringLiteral("x")));
        QCOMPARE(RideResistance::methodName(M::Gradient), QStringLiteral("gradient"));
    }

    // The FTMS method is a hidden, experimental option (Bluetooth
    // diagnostics): never saved, every start is Gradient (simulation); a
    // value stored by older versions ("gears/method") is ignored, and so is
    // the old "Use virtual gears" switch ("gears/enabled").
    void methodIsNeverPersisted()
    {
        using M = RideResistance::GearMethod;
        QCOMPARE(VirtualGears::methodFromSetting(QVariant()), M::Gradient);
        QCOMPARE(VirtualGears::methodFromSetting(QStringLiteral("bogus")), M::Gradient);
        QCOMPARE(VirtualGears::methodFromSetting(QStringLiteral("auto")), M::Auto);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        {
            VirtualGears g(&db, &profiles);
            QCOMPARE(g.shiftMethod(), QStringLiteral("gradient"));   // default: simulation
            QSignalSpy changed(&g, &VirtualGears::shiftMethodChanged);
            g.setShiftMethod(QStringLiteral("erg"));
            QCOMPARE(changed.count(), 1);
            g.setShiftMethod(QStringLiteral("nonsense"));   // ignored
            g.setShiftMethod(QStringLiteral("erg"));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(g.method(), M::Erg);
            QCOMPARE(g.shiftingMode(), QStringLiteral("bike"));   // no Click: no virtual gears at all
        }
        QVERIFY(!db.setting(QStringLiteral("gears/method")).isValid());
        QCOMPARE(VirtualGears(&db, &profiles).method(), M::Gradient);
        // older versions' stored values
        db.setSetting(QStringLiteral("gears/method"), QStringLiteral("auto"));
        db.setSetting(QStringLiteral("gears/enabled"), true);
        db.setSetting(QStringLiteral("gears/backend"), QStringLiteral("zwift"));
        VirtualGears old(&db, &profiles);
        QCOMPARE(old.method(), M::Gradient);
        QVERIFY(!old.enabled());   // only a Zwift Click turns virtual gears on
        QCOMPARE(old.hubOverride(), QStringLiteral("auto"));
        // a fresh rig (sessions on the virtual trainer) uses gear simulation
        Rig r;
        QVERIFY(r.init(true));
        QCOMPARE(r.gears->shiftMethod(), QStringLiteral("gradient"));
        r.freeRide->setGrade(2.0);
        QVERIFY(r.freeRide->start());
        QCOMPARE(r.freeRide->resistancePath(), QStringLiteral("gearSimulation"));
        r.freeRide->finish(false);
    }

    // The reported case in numbers: 44×44 at 90 rpm with the bike in 50×17 on
    // a simulating trainer. Path a needs a negative grade (the flywheel turns
    // at 33 km/h); a trainer that brakes "0 %" there takes ~150-200 W. Auto
    // sends a low ERG target instead, on −3 % and on 0 %.
    void hybridLightestGearIsLight()
    {
        SensorHub hub;
        hub.setTrainerConnected(true);
        hub.setTrainerSimulationSupport(1);
        hub.updateCadence(90);
        QVERIFY(hub.cadenceReported());
        for (double grade : { -3.0, 0.0 }) {
            RideResistance::GearDrive d;
            d.road.massKg = 85;
            d.road.gradePercent = grade;
            d.cadenceRpm = 90;
            d.virtualRatio = 44.0 / 44;
            d.referenceRatio = 50.0 / 17;
            const double vRef = RideResistance::referenceSpeed(d);
            QVERIFY(std::abs(vRef * 3.6 - 33.4) < 0.2);
            QVERIFY(std::abs(RideResistance::gearSpeed(d) * 3.6 - 11.4) < 0.1);
            // what path a would send, and what a trainer clamping it to 0 % brakes
            const double gradient = RideResistance::gearGrade(d);
            QVERIFY2(gradient < -1.5, qPrintable(QString::number(gradient)));
            const road::TrainerModel tm = RideResistance::trainerModel(d);
            const double atZero = (tm.crr * tm.massKg * 9.81 + 0.5 * tm.cwKgPerM * vRef * vRef) * vRef;
            QVERIFY2(atZero > 150 && atZero < 230, qPrintable(QString::number(atZero)));

            RideResistance res(&hub);
            QSignalSpy sims(&hub, &SensorHub::simulationRequested);
            QVERIFY(res.sendHybrid(d, true, 90, true));
            QCOMPARE(sims.count(), 0);   // never a gradient here
            QVERIFY(hub.ergActive());
            QVERIFY2(hub.requestedTarget() >= RideResistance::kLightMinWatts && hub.requestedTarget() <= 30,
                     qPrintable(QString::number(hub.requestedTarget())));
            QVERIFY(res.hybridLight());
            QCOMPARE(res.gearDebug().decision, QStringLiteral("erg-light"));
            QVERIFY(std::abs(res.gearDebug().wantedGrade - gradient) < 1e-9);
            // the Gradient method sends that negative grade (as before)
            RideResistance old(&hub);
            QVERIFY(old.sendGrade(RideResistance::gearGrade(d), true, tm));
            QCOMPARE(hub.requestedGrade(), std::round(gradient * 100) / 100);
        }
    }

    void hybridHysteresis()
    {
        QVERIFY(RideResistance::lightWanted(false, 0.0, 200));    // grade <= 0
        QVERIFY(RideResistance::lightWanted(false, 2.0, 59));     // little power
        QVERIFY(!RideResistance::lightWanted(false, 0.1, 60));
        QVERIFY(RideResistance::lightWanted(true, 0.4, 200));     // not above the exit grade yet
        QVERIFY(RideResistance::lightWanted(true, 2.0, 70));      // not enough power yet
        QVERIFY(!RideResistance::lightWanted(true, 0.6, 75));

        SensorHub hub;
        hub.setTrainerConnected(true);
        hub.setTrainerSimulationSupport(1);
        hub.updateCadence(90);
        RideResistance res(&hub);
        RideResistance::GearDrive climb;
        climb.road.massKg = 85;
        climb.road.gradePercent = 6;
        climb.cadenceRpm = 90;
        climb.virtualRatio = 50.0 / 17;
        climb.referenceRatio = 50.0 / 17;
        RideResistance::GearDrive descent = climb;
        descent.road.gradePercent = -3;
        descent.virtualRatio = 1.0;

        QVERIFY(res.sendHybrid(climb, false, 90, true));
        QVERIFY(hub.simulationActive());
        QVERIFY(hub.requestedGrade() > 3);
        // the descent starts: not within the same second ...
        res.sendHybrid(descent, false, 90, true);
        QVERIFY(hub.simulationActive());
        res.elapse(500);
        res.sendHybrid(descent, false, 90, true);
        QVERIFY(hub.simulationActive());
        // ... but after kHybridSwitchMs: the light ERG at once
        res.elapse(500);
        QVERIFY(res.sendHybrid(descent, false, 90, true));
        QVERIFY(hub.ergActive());
        QVERIFY(hub.requestedTarget() <= 30);
        // climbing again: at most one switch per second
        res.sendHybrid(climb, false, 90, true);
        QVERIFY(hub.ergActive());
        res.elapse(1000);
        QVERIFY(res.sendHybrid(climb, false, 90, true));
        QVERIFY(hub.simulationActive());
        // coasting: released to the trainer's minimum
        res.elapse(1000);
        QVERIFY(res.sendHybrid(climb, false, 5, true));
        QVERIFY(!hub.simulationActive());
        QVERIFY(!hub.ergActive());
        QCOMPARE(res.gearDebug().decision, QStringLiteral("free"));
        // no cadence from the trainer: the gear grade (the speed stands in)
        RideResistance noCad(&hub);
        RideResistance::GearDrive bySpeed = descent;
        bySpeed.cadenceRpm = 0;
        bySpeed.trainerSpeedMps = 9.0;
        QVERIFY(noCad.sendHybrid(bySpeed, true, 0, false));
        QVERIFY(hub.simulationActive());
    }

    // End to end on the virtual trainer, Video Ride on the −6 % descent in
    // 44×44: Auto only ever sends low ERG targets, the trainer brakes lightly.
    void hybridDescentEndToEnd()
    {
        Rig r;
        QVERIFY(r.init(true));
        QCOMPARE(r.gears->shiftMethod(), QStringLiteral("gradient"));   // default
        r.gears->setShiftMethod(QStringLiteral("auto"));   // optional, experimental
        QVERIFY(r.lightBike());
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(2200));
        r.commands.clear();
        QVERIFY(r.rlv->start());
        QCOMPARE(r.rlv->resistancePath(), QStringLiteral("gearHybrid"));
        const double power = r.ride(8);
        QVERIFY(r.rlv->gradePercent() < -5.9);
        QVERIFY2(!r.commands.join(QLatin1Char(',')).contains(QStringLiteral("sim")), qPrintable(r.commands.join(QLatin1Char(','))));
        QVERIFY(r.trainer->ergActive());
        QVERIFY2(r.hub->requestedTarget() <= 30, qPrintable(QString::number(r.hub->requestedTarget())));
        QVERIFY2(power <= 35, qPrintable(QString::number(power)));
        QVERIFY(r.rlv->gearSimulationGrade() < -1.0);   // what Gradient would have sent
        const QString diag = r.gears->diagnosticsText();
        QVERIFY2(diag.contains(QStringLiteral("[Virtual gears]")) && diag.contains(QStringLiteral("gearHybrid -> erg-light"))
                     && diag.contains(QStringLiteral("Gear 3/24 (0.99)")) && diag.contains(QStringLiteral("Trainer command: ERG")),
                 qPrintable(diag));
        QVERIFY(r.gears->debugLine().contains(QStringLiteral("method=auto")));
        r.rlv->finish(false);

        // Free Ride on the flat in 44×44: light too; a hard gear on a climb: the gradient
        r.commands.clear();
        r.freeRide->setGrade(0.0);
        QVERIFY(r.freeRide->start());
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        QVERIFY(r.hub->requestedTarget() <= 30);
        QVERIFY(r.setGear(14));
        r.freeRide->setGrade(6.0);
        r.ride(4);
        QVERIFY(r.trainer->simulating());
        QVERIFY(r.hub->requestedGrade() > 3.0);
        r.freeRide->finish(false);
    }

    void ergMethodIsAlwaysErg()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("erg"));
        r.freeRide->setGrade(6.0);
        QSignalSpy sims(r.hub.get(), &SensorHub::simulationRequested);
        QVERIFY(r.freeRide->start());
        QCOMPARE(r.freeRide->resistancePath(), QStringLiteral("gearErg"));
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        QVERIFY(r.hub->requestedTarget() > 200);
        r.freeRide->setGrade(-4.0);
        r.ride(4);
        QVERIFY(r.trainer->ergActive());
        QCOMPARE(r.hub->requestedTarget(), RideResistance::kMinGearWatts);
        QCOMPARE(sims.count(), 0);
        r.freeRide->finish(false);
    }

    // The real gear on the trainer from its speed and cadence.
    void realGearIsDetected()
    {
        const int c = 2105;
        auto speedFor = [&](double cad, double ratio) { return cad / 60.0 * ratio * c / 1000.0; };
        GearRatioDetector det;
        qint64 t = 0;
        for (; t < GearRatioDetector::kWindowMs - 500; t += 500)
            QVERIFY(!det.addSample(t, 90, speedFor(90, 50.0 / 17), c));
        QVERIFY(!det.valid());   // not long enough yet
        for (; t <= GearRatioDetector::kWindowMs + 500; t += 500)
            det.addSample(t, 90, speedFor(90, 50.0 / 17), c);
        QVERIFY(det.valid());
        QVERIFY(std::abs(det.ratio() - 50.0 / 17) < 0.01);
        // not steady (cadence below 50 rpm, accelerating flywheel): no new result
        for (int i = 0; i < 20; ++i, t += 500)
            QVERIFY(!det.addSample(t, 40, speedFor(40, 2.0), c));
        for (int i = 0; i < 20; ++i, t += 500)
            QVERIFY(!det.addSample(t, 80 + (i % 2) * 15, speedFor(85, 2.0), c));   // ±8 %
        QVERIFY(std::abs(det.ratio() - 50.0 / 17) < 0.01);

        // through VirtualGears: label, mismatch, never touches the saved gear
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        VirtualGears g(&db, &profiles);
        g.setEnabled(true);   // virtual gears (Click) on FTMS gradient: the reference gear matters
        QCOMPARE(g.shiftingMode(), QStringLiteral("ftms"));
        QSignalSpy detected(&g, &VirtualGears::detectionChanged);
        QCOMPARE(g.detectedRatio(), 0.0);
        QCOMPARE(g.effectiveReferenceRatio(), g.referenceRatio());
        const int circ = g.setup().circumferenceMm;
        for (qint64 ms = 0; ms <= 7000; ms += 250)
            g.addTrainerSample(ms, 88, 88 / 60.0 * 50.0 / 17 * circ / 1000.0);
        QCOMPARE(g.detectedLabel(), QStringLiteral("50×17"));
        QVERIFY(!g.detectedDiffers());
        QVERIFY(g.heavyRealGear());   // 2.94: a small real gear makes the easy gears lighter
        QCOMPARE(detected.count(), 1);
        for (qint64 ms = 8000; ms <= 16000; ms += 250)
            g.addTrainerSample(ms, 92, 92 / 60.0 * 34.0 / 17 * circ / 1000.0);
        QCOMPARE(g.detectedLabel(), QStringLiteral("34×17"));
        QVERIFY(std::abs(g.detectedRatio() - 2.0) < 0.01);
        QVERIFY(g.detectedDiffers());
        QCOMPARE(g.referenceLabel(), QStringLiteral("50×17"));   // the saved reference stays
        // never used by itself: path a keeps the saved reference gear ...
        QCOMPARE(g.effectiveReferenceRatio(), g.referenceRatio());
        QVERIFY(g.heavyRealGear());
        QVERIFY(g.diagnosticsText().contains(QStringLiteral("detected: 34×17 (2.000)")));
        // ... and the rider is only asked once the detection is stable
        QVERIFY(!g.detectedStable());
        QCOMPARE(g.referencePrompt(), QString());
        for (qint64 ms = 16250; ms <= 30000; ms += 250)
            g.addTrainerSample(ms, 92, 92 / 60.0 * 34.0 / 17 * circ / 1000.0);
        QVERIFY(g.detectedStable());
        QCOMPARE(g.referencePrompt(),
                 QStringLiteral("Your trainer reports 34×17 (2.00) but the setting is 50×17 (2.94) — use the detected gear?"));
        // "Keep 50×17": no prompt for this detection any more
        g.dismissReferencePrompt();
        QCOMPARE(g.referencePrompt(), QString());
        QCOMPARE(g.referenceLabel(), QStringLiteral("50×17"));
        g.clearDetection();
        QCOMPARE(g.detectedRatio(), 0.0);
        QCOMPARE(g.effectiveReferenceRatio(), g.referenceRatio());
    }

    // Path a with a detected real gear: v_ref from the detected ratio once the
    // rider accepted the prompt (useDetectedGear()).
    void detectedGearDrivesPathA()
    {
        Rig r;
        QVERIFY(r.init(true));
        r.gears->setShiftMethod(QStringLiteral("gradient"));
        r.freeRide->setGrade(2.0);
        const double before = r.freeRide->gearSimulationGrade();
        // the bike really sits in 34×17: slower flywheel, so the same virtual
        // gear needs a steeper grade there
        r.gears->clearDetection();
        r.gears->addTrainerSample(1000000, 88, 88 / 60.0 * 2.0 * r.gears->setup().circumferenceMm / 1000.0);
        for (qint64 ms = 1000250; ms <= 1020000; ms += 250)
            r.gears->addTrainerSample(ms, 88, 88 / 60.0 * 2.0 * r.gears->setup().circumferenceMm / 1000.0);
        // detected only: nothing changes until the rider accepts it
        QCOMPARE(r.gears->effectiveReferenceRatio(), r.gears->referenceRatio());
        QCOMPARE(r.freeRide->gearSimulationGrade(), before);
        QVERIFY(!r.gears->referencePrompt().isEmpty());
        QVERIFY(r.gears->useDetectedGear());
        QCOMPARE(r.gears->referenceLabel(), QStringLiteral("34×17"));
        QCOMPARE(r.gears->referencePrompt(), QString());
        QVERIFY(std::abs(r.gears->effectiveReferenceRatio() - 2.0) < 0.01);
        QVERIFY2(r.freeRide->gearSimulationGrade() > before + 1.0,
                 qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(r.freeRide->gearSimulationGrade())));
    }

    // A 14T Zwift Cog trainer: 44×14 detected (not a gear of the setup) is
    // offered and saved as a custom reference gear; presets exist.
    void zwiftCogReferenceGear()
    {
        Rig r;
        QVERIFY(r.init(true));
        const QVariantList presets = r.gears->cogPresets();
        QVERIFY(!presets.isEmpty());
        bool has5014 = false;
        for (const QVariant &v : presets) {
            const QVariantMap m = v.toMap();
            QCOMPARE(m.value(QStringLiteral("rear")).toInt(), 14);
            if (m.value(QStringLiteral("front")).toInt() == 50)
                has5014 = m.value(QStringLiteral("label")).toString() == QStringLiteral("50×14 (Zwift Cog)");
        }
        QVERIFY(has5014);
        const int circ = r.gears->setup().circumferenceMm;
        const double ratio = 44.0 / 14;
        for (qint64 ms = 0; ms <= 22000; ms += 250)
            r.gears->addTrainerSample(ms, 90, 90 / 60.0 * ratio * circ / 1000.0);
        QVERIFY(r.gears->detectedStable());
        QCOMPARE(r.gears->detectedLabel(), QStringLiteral("44×14"));
        QVERIFY(r.gears->referencePrompt().startsWith(QStringLiteral("Your trainer reports 44×14 (3.14) but the setting is ")));
        QVERIFY(r.gears->useDetectedGear());
        QCOMPARE(r.gears->referenceLabel(), QStringLiteral("44×14"));
        QVERIFY(r.gears->referenceCustom());
        // a preset directly, and back to a gear of the setup
        QVERIFY(r.gears->setReferenceGearAny(50, 14));
        QCOMPARE(r.gears->referenceLabel(), QStringLiteral("50×14"));
        QVERIFY(!r.gears->setReferenceGearAny(80, 14));
        QVERIFY(r.gears->setReferenceGear(r.gears->setup().chainrings.constLast(), r.gears->setup().cogs.constFirst()));
        QVERIFY(!r.gears->referenceCustom());
        // custom reference survives the JSON round trip
        GearSetup s = r.gears->setup();
        s.referenceFrontTeeth = 44;
        s.referenceRearTeeth = 14;
        s.referenceCustom = true;
        const GearSetup back = GearSetup::fromJson(s.toJson());
        QCOMPARE(back.referenceFrontTeeth, 44);
        QCOMPARE(back.referenceRearTeeth, 14);
        QVERIFY(back.referenceCustom);
    }

    // -- Click = on-screen buttons, same direction on both pages ----------------

    void clickShiftsLikeTheButtons()
    {
        Rig r;
        QVERIFY(r.init(true));
        QVERIFY(r.addRoute());
        QVERIFY(r.rlv->seekToDistance(800));
        r.freeRide->setGrade(3.0);
        for (const QString page : { QStringLiteral("freeride"), QStringLiteral("video") }) {
            r.gears->setActivePage(page);
            QVERIFY(r.setGear(17));
            const int index = r.gears->gearIndex();
            const double ratio = r.gears->gearing().ratio();
            const double before = r.hub->requestedGrade();
            r.click->pressButton(1);   // Click "+"
            QCOMPARE(r.gears->gearIndex(), index + 1);
            QVERIFY(r.gears->gearing().ratio() > ratio);   // harder = higher ratio
            const double byClick = r.hub->requestedGrade();
            QVERIFY2(byClick > before, qPrintable(page));
            r.click->pressButton(-1);   // Click "−"
            QCOMPARE(r.gears->gearIndex(), index);
            QCOMPARE(r.hub->requestedGrade(), before);
            QVERIFY(r.gears->shiftUp());   // on-screen "Harder" / key ↑
            QCOMPARE(r.gears->gearIndex(), index + 1);
            QCOMPARE(r.hub->requestedGrade(), byClick);
            QVERIFY(r.gears->shiftDown());   // "Easier" / ↓
            QCOMPARE(r.gears->gearIndex(), index);
            QCOMPARE(r.hub->requestedGrade(), before);
        }
    }
};

QTEST_GUILESS_MAIN(TestVirtualGears)
#include "tst_virtualgears.moc"
