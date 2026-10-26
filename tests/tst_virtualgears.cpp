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
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        freeRide->setManualClock(true);
        library = std::make_unique<RlvLibrary>(&db);
        library->setLibraryFolder(dir.path());
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), library.get());
        rlv->setManualClock(true);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setStartBlocker(QStringLiteral("freeRide"), [this] { return freeRide->active(); });
        freeRide->setStartBlocker(QStringLiteral("rlv"), [this] { return rlv->active() || rlv->waiting(); });
        gears->setSessions(freeRide.get(), rlv.get());

        // the virtual trainer, wired like DeviceManager::attachTrainer()
        trainer = std::make_unique<SimulatedTrainer>();
        trainer->setNoise(false);
        trainer->setSimulationSupported(trainerSimulates);
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
    bool setGear(int cog)
    {
        return gears->setGear(gears->setup().chainrings.indexOf(50), gears->setup().cogs.indexOf(cog));
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

class TestVirtualGears : public QObject
{
    Q_OBJECT
private slots:
    // -- the shared physics -------------------------------------------------

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
        // coasting (cadence reported, but < 15 rpm): the plain road grade
        d.cadenceRpm = 5;
        QCOMPARE(RideResistance::gearGrade(d), 3.0);
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
        QVERIFY(std::abs(speed.last().at(0).toDouble() / 3.6 - t.cadenceRpm() / 60.0 * 50.0 / 17 * 2.096) < 0.3);
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
        QVERIFY(t.cadenceRpm() < 86);   // ~600 W: a few rpm less
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
