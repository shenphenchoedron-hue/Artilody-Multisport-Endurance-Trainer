// FreeRideSession: modes Ride / ERG (old "gears" / "free" migrated), ERG
// hold, virtual gear targets (path b) and gear-adjusted grades (path a), real
// gears with the trainer simulating the grade, Zwift Click routing (never
// the difficulty / grade), manual start only (pedaling never starts or
// resumes a ride), recording, mutual exclusion with
// workouts/rowing, gear setup persistence (VirtualGears).
#include "CsvExporter.h"
#include "Database.h"
#include "FitExporter.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RowingRecords.h"
#include "RowingSession.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "TrainingSession.h"
#include "VirtualGears.h"
#include "WorkoutPlanner.h"

#include <QSignalSpy>
#include <QtTest>
#include <memory>

using namespace watt;

struct Fixture
{
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<TrainingSession> training;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<FreeRideSession> ride;

    bool init()
    {
        if (!db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(600000);   // the test clock is virtual
        hub->setTrainerConnected(true);
        training = std::make_unique<TrainingSession>(&db, profiles.get(), hub.get());
        training->setManualClock(true);
        gears = makeGears();
        // most tests ride with virtual gears on a trainer without gradient
        // simulation: path b, ERG (tests of path a / real gears set support 1)
        hub->setTrainerSimulationSupport(0);
        gears->setEnabled(true);
        ride = makeRide();
        return true;
    }
    std::unique_ptr<VirtualGears> makeGears()
    {
        auto g = std::make_unique<VirtualGears>(&db, profiles.get());
        g->setManualSave(true);
        return g;
    }
    std::unique_ptr<FreeRideSession> makeRide()
    {
        auto r = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        r->setManualClock(true);
        r->setMinimumSaveSeconds(5);
        r->setTrainingSession(training.get());
        return r;
    }
    // real gears on a trainer that simulates gradients
    void realGears()
    {
        gears->setEnabled(false);
        hub->setTrainerSimulationSupport(1);
    }
    void pedal(int cadence, int watts)
    {
        hub->updateCadence(cadence);
        hub->updatePower(watts);
    }
    // advances in 250 ms steps like the real timer
    void ride_for(int ms)
    {
        for (int t = 0; t < ms; t += 250)
            ride->advanceClock(250);
    }
};


// A trainer that records what it is told, wired to the hub exactly like
// DeviceManager forwards the resistance requests (target power -> FTMS 0x05,
// free ride, simulation -> 0x11).
class FakeTrainer : public TrainerDevice
{
public:
    explicit FakeTrainer(SensorHub *hub)
    {
        QObject::connect(hub, &SensorHub::targetPowerRequested, this, [this](int w) { setTargetPower(w); });
        QObject::connect(hub, &SensorHub::freeRideRequested, this, [this] { setFreeRide(); });
        QObject::connect(hub, &SensorHub::simulationRequested, this,
                         [this](double g, double wd, double crr, double cw) { setSimulation(g, wd, crr, cw); });
    }
    QString id() const override { return QStringLiteral("fake"); }
    QString name() const override { return QStringLiteral("Fake"); }
    QString protocol() const override { return QStringLiteral("Fake FTMS"); }
    void connectDevice() override {}
    void disconnectDevice() override {}
    QStringList capabilities() const override { return {}; }
    bool supportsControl() const override { return true; }
    int simulationSupport() const override { return 1; }
    void setTargetPower(int watts) override
    {
        log << QStringLiteral("erg %1").arg(watts);
        erg = true;
        target = watts;
    }
    void setFreeRide() override
    {
        log << QStringLiteral("free");
        erg = false;
        target = 0;
    }
    void setSimulation(double g, double, double, double) override
    {
        log << QStringLiteral("sim %1").arg(g);
        erg = false;
        target = 0;
    }
    QString last() const { return log.isEmpty() ? QString() : log.last(); }
    bool inErg(int watts) const { return erg && target == watts; }

    QStringList log;
    bool erg = false;
    int target = 0;
};

// Switches the soft start on for one test (the suite runs with it off).
struct SoftStartOn
{
    SoftStartOn() { RideResistance::setSoftStartEnabled(true); }
    ~SoftStartOn() { RideResistance::setSoftStartEnabled(false); }
};

class TestFreeRide : public QObject
{
    Q_OBJECT
private slots:
    // These tests are about physics / rate limits / paths: the soft start
    // (0 % until the flywheel turns, then a ramp) has its own tests below
    // and in tst_virtualgears (softStart*).
    void initTestCase() { RideResistance::setSoftStartEnabled(false); }

    // Zwift protocol: when the rider stops pedaling on the flat, the speed
    // must not roll on after the trainer's flywheel stopped (it took 10–15 s,
    // the road model coasted a 77 kg bike). Capped at the trainer's own
    // simulated speed (flywheel × sent ratio / 34/14); downhill it rolls on.
    void hubCoastStopsWithTheFlywheel()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setHub(f.hub.get());
        f.hub->setTrainerZwiftHub(SensorHub::HubActive, QStringLiteral("active"));
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearZwiftHub"));
        QVERIFY(f.ride->start());
        // pedaling: 80 rpm, 110 W, the flywheel at 38 km/h (44×11)
        f.pedal(80, 110);
        f.hub->updateSpeed(38.0);
        f.ride_for(30000);
        const double riding = f.ride->speedKmh();
        QVERIFY2(riding > 20.0, qPrintable(QString::number(riding)));
        // coasting: 0 rpm, 0 W, the flywheel runs down 38 -> 0 km/h in 15 s
        f.pedal(0, 0);
        QVector<double> app, trainer;
        for (int t = 0; t <= 15000; t += 250) {
            const double fly = std::max(0.0, 38.0 * (1.0 - t / 15000.0));
            f.hub->updateSpeed(fly);
            f.ride->advanceClock(250);
            app << f.ride->speedKmh();
            trainer << f.gears->hubTrainerSpeedMps() * 3.6;
        }
        // no jump: at most kHubCoastCatchUpMps2 (+ the road's own drag) per step
        double prev = riding;
        for (double v : app) {
            QVERIFY2(prev - v <= (FreeRideSession::kHubCoastCatchUpMps2 + 1.0) * 0.25 * 3.6 + 1e-6,
                     qPrintable(QStringLiteral("%1 -> %2").arg(prev).arg(v)));
            prev = v;
        }
        // caught up within a few seconds, then never faster than the trainer
        for (int i = 16; i < app.size(); ++i)
            QVERIFY2(app.at(i) <= trainer.at(i) + 1e-6,
                     qPrintable(QStringLiteral("%1: app %2 > trainer %3").arg(i).arg(app.at(i)).arg(trainer.at(i))));
        QCOMPARE(app.constLast(), 0.0);   // the flywheel stopped: so does the bike
        QVERIFY(!f.ride->coasting());      // ... and it is no longer coasting

        // coasting = not pedaling while the flywheel turns; a cadence at 0 W
        // is light pedaling (the Victory reads 0 W at 36–107 rpm, 1.3.8)
        f.pedal(80, 110);
        f.hub->updateSpeed(38.0);
        f.ride_for(20000);
        QVERIFY(!f.ride->coasting());
        f.pedal(83, 0);
        f.ride->advanceClock(250);
        QVERIFY(!f.ride->coasting());
        QVERIFY(f.ride->pedaling());
        f.pedal(0, 0);
        f.ride->advanceClock(250);
        QVERIFY(f.ride->coasting());
        // the start's lagging cadence (0 rpm at 180 W) is pedaling, not coasting
        f.pedal(0, 180);
        f.ride->advanceClock(250);
        QVERIFY(!f.ride->coasting());
        f.pedal(0, 0);
        f.ride->advanceClock(250);

        // downhill: the bike rolls on while the flywheel turns ...
        f.ride->setGrade(-4.0);
        f.hub->updateSpeed(20.0);
        f.ride_for(3000);
        QVERIFY2(f.ride->speedKmh() > 15.0, qPrintable(QString::number(f.ride->speedKmh())));
        QVERIFY(f.ride->coasting());
        // ... and stops when the flywheel stops: no longer coasting, braked to
        // 0 at kHubStopDecelMps2 (no jump from ~20 km/h to 0)
        const double rolling = f.ride->speedKmh();
        f.hub->updateSpeed(0.0);
        f.ride->advanceClock(250);
        QVERIFY(!f.ride->coasting());
        QVERIFY2(f.ride->speedKmh() > 0.0, qPrintable(QString::number(f.ride->speedKmh())));
        QVERIFY(rolling - f.ride->speedKmh() <= FreeRideSession::kHubStopDecelMps2 * 0.25 * 3.6 + 1e-6);
        f.ride_for(3000);   // stopped within ~1.5 s and stays stopped downhill
        QCOMPARE(f.ride->speedKmh(), 0.0);
        f.ride_for(3000);
        QCOMPARE(f.ride->speedKmh(), 0.0);
    }

    // Zwift protocol, light pedaling: cadence but 0 W (the Victory, log of
    // 1.3.8: 36 rpm at 0 W counted as coasting, the speed sank to 7 km/h with
    // the flywheel at 18 km/h). The speed follows the trainer's flywheel for
    // the NOMINAL gear - not the soft start's light gear that is sent.
    void hubLightPedalingFollowsTheFlywheel()
    {
        SoftStartOn soft;
        Fixture f;
        QVERIFY(f.init());
        f.gears->setHub(f.hub.get());
        f.hub->setTrainerZwiftHub(SensorHub::HubActive, QStringLiteral("active"));
        QVERIFY(f.ride->start());
        f.pedal(36, 0);
        f.hub->updateSpeed(18.0);
        f.ride_for(10000);
        QVERIFY(f.ride->pedaling());
        QVERIFY(!f.ride->coasting());
        const double trainer = f.gears->hubTrainerSpeedMps() * 3.6;
        QVERIFY2(trainer > 0, qPrintable(QString::number(trainer)));
        QVERIFY2(std::abs(f.ride->speedKmh() - trainer) < 0.5,
                 qPrintable(QStringLiteral("app %1 trainer %2").arg(f.ride->speedKmh()).arg(trainer)));
        // the nominal gear: virtual × compensation (unknown real gear: × 1),
        // independent of the light gear the soft start sends
        const double nominal = f.gears->gearRatio() * (f.gears->hubGearKnown() ? f.gears->hubGearFactor() : 1.0);
        QVERIFY2(std::abs(trainer - 18.0 * nominal / HubGearCompensation::kTrainerRatio) < 0.05,
                 qPrintable(QStringLiteral("trainer %1 nominal %2 sent %3")
                                .arg(trainer)
                                .arg(nominal)
                                .arg(f.hub->hubSimulation().gearRatio)));
    }
    // The shared hub speed step: downhill a light pedal / lagging cadence
    // never brakes the bike to the flywheel (it rolls on), on the flat it
    // follows the flywheel both ways; coasting caps only on the flat.
    void hubSpeedStepRules()
    {
        RideResistance::HubSpeedInput in;
        in.trainerMps = 4.0;
        in.pedaling = true;
        in.lowPower = true;
        in.dt = 0.25;
        in.gradePercent = -4.0;
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0);                  // downhill: not braked
        QCOMPARE(RideResistance::hubSpeedStep(2.0, in), 2.0 + 3.0 * 0.25);     // but pulled up
        in.gradePercent = 0.0;
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0 - 3.0 * 0.25);     // flat: follows down
        QCOMPARE(RideResistance::hubSpeedStep(2.0, in), 2.0 + 3.0 * 0.25);     // ... and up
        in.lowPower = false;
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0);                  // real power: untouched
        in.pedaling = false;
        QCOMPARE(RideResistance::hubSpeedStep(2.0, in), 2.0);                  // coasting: never raised
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0 - 3.0 * 0.25);     // capped on the flat
        in.gradePercent = -4.0;
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0);                  // rolls on downhill
        in.flywheelStopped = true;
        QCOMPARE(RideResistance::hubSpeedStep(8.0, in), 8.0 - 4.0 * 0.25);     // braked to 0, any grade
        QCOMPARE(RideResistance::hubSpeedStep(0.5, in), 0.0);
    }

    // Free ride, downhill: the rider stops, the Victory's cadence still reads
    // 80 rpm at 0 W - the speed must not drop to the flywheel's.
    void hubLaggingCadenceDownhillDoesNotBrake()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setHub(f.hub.get());
        f.hub->setTrainerZwiftHub(SensorHub::HubActive, QStringLiteral("active"));
        QVERIFY(f.ride->start());
        f.ride->setGrade(-4.0);
        f.pedal(85, 150);
        f.hub->updateSpeed(30.0);
        f.ride_for(20000);
        const double before = f.ride->speedKmh();
        f.pedal(80, 0);            // lagging cadence at 0 W
        f.hub->updateSpeed(8.0);   // the flywheel slows down
        f.ride_for(2000);
        QVERIFY2(f.ride->speedKmh() >= before - 0.5,
                 qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(f.ride->speedKmh())));
    }

    void idleIsSafeWithoutTrainer()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        SensorHub hub;
        VirtualGears gears(&db, &profiles);
        FreeRideSession s(&db, &profiles, &hub, &gears);
        QCOMPARE(s.state(), QStringLiteral("idle"));
        QCOMPARE(s.mode(), QStringLiteral("ride"));
        QVERIFY(!gears.enabled());   // off until a Zwift Click connects
        QVERIFY(!s.virtualGears());
        QCOMPARE(s.resistancePath(), QStringLiteral("simulation"));
        QVERIFY(!s.trainerConnected());
        QCOMPARE(s.speedKmh(), 0.0);
        QCOMPARE(s.watts(), 0);
        QCOMPARE(s.gearTargetWatts(), 0);
        QCOMPARE(s.requestedWatts(), 0);
        QVERIFY(s.powerSeries().isEmpty());
        // Zwift's 24 gears, start gear 12 (2.40)
        QCOMPARE(gears.gearLabel(), QStringLiteral("12"));
        QCOMPARE(gears.gearText(), QStringLiteral("Gear 12/24 (2.40)"));
        QCOMPARE(gears.gearCount(), 24);
        QCOMPARE(gears.gear().value(QStringLiteral("label")).toString(), gears.gearLabel());
        QCOMPARE(gears.gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2105);
        QCOMPARE(gears.referenceLabel(), QStringLiteral("50×17"));
        QCOMPARE(gears.summary(), QStringLiteral("Zwift's 24 virtual gears · 700×25c"));
        QVERIFY(!gears.chainringPresets().isEmpty());
        QVERIFY(!gears.cassettePresets().isEmpty());
        QVERIFY(!gears.wheelPresets().isEmpty());
        QCOMPARE(s.finish(true), qint64(-1));
        s.pause();
        s.resume();
        QCOMPARE(s.state(), QStringLiteral("idle"));
    }

    void ergHoldsTarget()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.ride->setMode(QStringLiteral("erg"));
        QVERIFY(f.ride->ergEnabled());
        f.ride->setTargetWatts(200);
        QCOMPARE(targets.count(), 0);   // nothing is sent before the ride starts
        QVERIFY(f.ride->start());
        QCOMPARE(f.hub->requestedTarget(), 200);
        QCOMPARE(f.ride->requestedWatts(), 200);
        f.pedal(70, 200);
        f.ride_for(60000);
        QCOMPARE(f.hub->requestedTarget(), 200);   // held as long as you like
        f.pedal(110, 200);                          // cadence does not change ERG
        f.ride_for(5000);
        QCOMPARE(f.hub->requestedTarget(), 200);
        f.ride->adjustTargetWatts(25);
        QCOMPARE(f.hub->requestedTarget(), 225);
        f.ride->adjustTargetWatts(-10);
        QCOMPARE(f.ride->targetWatts(), 215);
        f.ride->setTargetWatts(5000);
        QCOMPARE(f.ride->targetWatts(), FreeRideSession::kMaxWatts);
        f.ride->setTargetWatts(1);
        QCOMPARE(f.ride->targetWatts(), FreeRideSession::kMinErgWatts);
        QCOMPARE(f.hub->requestedTarget(), FreeRideSession::kMinErgWatts);
        // the recorded target is the ERG target
        QCOMPARE(f.ride->samples().at(10).targetPower, 200);
        // pause releases the trainer, resume re-applies
        f.ride->pause();
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.ride->resume();
        QCOMPARE(f.hub->requestedTarget(), FreeRideSession::kMinErgWatts);
        f.ride->finish(false);
        QCOMPARE(f.hub->requestedTarget(), 0);
    }

    void gearTargetsAreMonotonic()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.gears->setGearIndex(0));
        f.pedal(90, 150);
        QVERIFY(f.ride->start());
        f.ride_for(5000);   // smoothed cadence settles at 90
        int previous = 0;
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));   // no gradient simulation: path b
        for (int i = 0; i < f.gears->gearCount(); ++i) {
            QVERIFY(f.gears->setGearIndex(i));
            f.ride_for(1000);
            const int target = f.hub->requestedTarget();
            // matches the road model at the virtual speed (lowest gears on
            // the flat sit at the kMinGearWatts floor)
            const double v = f.ride->gearing().virtualSpeed(90);
            const int expected = int(std::lround(road::powerForSpeed(f.ride->roadModel(0), v)));
            QVERIFY(qAbs(target - std::clamp(expected, FreeRideSession::kMinGearWatts, FreeRideSession::kMaxWatts)) <= 3);
            if (expected > FreeRideSession::kMinGearWatts + 3)
                QVERIFY2(target > previous, qPrintable(QStringLiteral("gear %1: %2 W after %3 W").arg(i).arg(target).arg(previous)));
            else
                QVERIFY(target >= previous);
            previous = target;
            QVERIFY(qAbs(f.ride->speedKmh() - v * 3.6) < 0.01);
        }
        QVERIFY(previous > 400);   // 50x11 at 90 rpm is fast (~55 km/h)
    }

    void shiftAppliesImmediately()
    {
        Fixture f;
        QVERIFY(f.init());
        f.pedal(90, 150);
        QVERIFY(f.ride->start());
        f.ride_for(4000);
        const int before = f.hub->requestedTarget();
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        QSignalSpy shifted(f.gears.get(), &VirtualGears::shifted);
        QVERIFY(f.gears->shiftUp());
        QCOMPARE(targets.count(), 1);   // no waiting for the next update
        QVERIFY(f.hub->requestedTarget() > before);
        QCOMPARE(shifted.count(), 1);
        QCOMPARE(shifted.first().first().toInt(), 1);
        QVERIFY(f.gears->shiftDown());
        QVERIFY(f.gears->shiftDown());
        QVERIFY(f.hub->requestedTarget() < before);
        // grade: uphill needs more power at the same speed
        const int flat = f.hub->requestedTarget();
        f.ride->setGrade(4.0);
        QCOMPARE(f.ride->gradePercent(), 4.0);
        QVERIFY(f.hub->requestedTarget() > flat + 50);
        f.ride->setGrade(99);
        QCOMPARE(f.ride->gradePercent(), FreeRideSession::kMaxGrade);
        f.ride->setGrade(0);
        f.ride->finish(false);
    }

    void cadenceChangesAreSmoothedAndRateLimited()
    {
        Fixture f;
        QVERIFY(f.init());
        f.pedal(90, 150);
        QVERIFY(f.ride->start());
        f.ride_for(5000);
        const int at90 = f.hub->requestedTarget();
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.pedal(100, 150);
        f.ride->advanceClock(250);
        QVERIFY(targets.count() <= 1);
        f.ride_for(1000);
        QVERIFY(targets.count() <= 2);            // at most one update per second
        f.ride_for(5000);
        QVERIFY(f.hub->requestedTarget() > at90); // pedaling faster in the same gear = faster = harder
        f.ride->finish(false);
    }

    void coastingReleasesAndRollsOut()
    {
        Fixture f;
        QVERIFY(f.init());
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        f.ride_for(3000);
        const double speed = f.ride->speedKmh();
        QVERIFY(speed > 20);
        QSignalSpy free(f.hub.get(), &SensorHub::freeRideRequested);
        f.pedal(0, 0);
        f.ride->advanceClock(250);
        QVERIFY(free.count() >= 1);
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(f.ride->coasting());
        f.ride_for(2000);
        QVERIFY(f.ride->speedKmh() > 10);       // rolls on
        QVERIFY(f.ride->speedKmh() < speed);
        const double d = f.ride->distanceMeters();
        f.ride_for(5000);
        QVERIFY(f.ride->distanceMeters() > d);
        f.ride_for(120000);
        QCOMPARE(f.ride->speedKmh(), 0.0);      // eventually stops
        // pedaling again re-applies resistance
        f.pedal(80, 120);
        f.ride->advanceClock(250);
        QVERIFY(f.hub->requestedTarget() >= FreeRideSession::kMinGearWatts);
        f.ride->finish(false);
    }

    void ergStartsAtCurrentWatts()
    {
        Fixture f;
        QVERIFY(f.init());
        f.pedal(90, 187);
        QVERIFY(f.ride->start());
        f.ride_for(3000);
        QSignalSpy modes(f.ride.get(), &FreeRideSession::modeChanged);
        f.ride->setMode(QStringLiteral("erg"));
        QCOMPARE(modes.count(), 1);
        QCOMPARE(f.ride->mode(), QStringLiteral("erg"));
        QCOMPARE(f.ride->targetWatts(), 185);
        QCOMPARE(f.hub->requestedTarget(), 185);
        f.ride->setMode(QStringLiteral("ride"));
        QCOMPARE(f.ride->mode(), QStringLiteral("ride"));
        QVERIFY(f.hub->requestedTarget() > 0);
        f.ride->setMode(QStringLiteral("nonsense"));
        QCOMPARE(f.ride->mode(), QStringLiteral("ride"));
        f.ride->setMode(QStringLiteral("erg"));
        f.ride->finish(false);
        // mode and ERG target are remembered
        auto again = f.makeRide();
        QCOMPARE(again->mode(), QStringLiteral("erg"));
        QCOMPARE(again->targetWatts(), 185);
    }

    // The Ride | ERG selector is the only ERG switch: mode "erg" sends the
    // target at once, "ride" releases it; ergEnabled cannot be set apart.
    void modeSelectorIsTheErgSwitch()
    {
        Fixture f;
        QVERIFY(f.init());
        const QMetaObject *mo = f.ride->metaObject();
        const QMetaProperty ergProp = mo->property(mo->indexOfProperty("ergEnabled"));
        QVERIFY(ergProp.isValid());
        QVERIFY(!ergProp.isWritable());
        QVERIFY(!ergProp.write(f.ride.get(), true));
        QCOMPARE(f.ride->mode(), QStringLiteral("ride"));
        QVERIFY(!f.ride->ergEnabled());

        f.pedal(80, 140);
        QVERIFY(f.ride->start());
        f.ride_for(2000);
        f.ride->setTargetWatts(230);   // remembered, but not sent in ride mode
        QVERIFY(f.ride->resistancePath() != QStringLiteral("erg"));
        QVERIFY(f.hub->requestedTarget() != 230);

        QSignalSpy modes(f.ride.get(), &FreeRideSession::modeChanged);
        f.ride->setMode(QStringLiteral("erg"));
        QCOMPARE(modes.count(), 1);
        QVERIFY(f.ride->ergEnabled());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("erg"));
        const int target = f.ride->targetWatts();
        QVERIFY(target >= FreeRideSession::kMinErgWatts);
        QCOMPARE(f.hub->requestedTarget(), target);   // sent at once, no extra switch
        QCOMPARE(f.ride->requestedWatts(), target);
        f.ride->adjustTargetWatts(10);
        QCOMPARE(f.hub->requestedTarget(), target + 10);

        f.ride->setMode(QStringLiteral("ride"));
        QVERIFY(!f.ride->ergEnabled());
        QVERIFY(f.ride->resistancePath() != QStringLiteral("erg"));
        f.ride->advanceClock(1500);
        QVERIFY(f.hub->requestedTarget() != target + 10);   // the ERG hold is gone
        // pause/resume keeps mode and ERG together
        f.ride->setMode(QStringLiteral("erg"));
        f.ride->pause();
        QCOMPARE(f.ride->mode(), QStringLiteral("erg"));
        QVERIFY(f.ride->ergEnabled());
        f.ride->resume();
        QCOMPARE(f.hub->requestedTarget(), f.ride->targetWatts());
        f.ride->finish(false);
    }

    void modeReshapeAndMigration()
    {
        Fixture f;
        QVERIFY(f.init());
        f.ride.reset();
        // the old "gears" and "free" modes are the road ride now
        for (const char *old : { "gears", "free" }) {
            f.db.setSetting(QStringLiteral("freeRide/mode"), QString::fromLatin1(old));
            auto r = f.makeRide();
            QCOMPARE(r->mode(), QStringLiteral("ride"));
            QCOMPARE(f.db.setting(QStringLiteral("freeRide/mode")).toString(), QStringLiteral("ride"));
        }
        QVERIFY(FreeRideSession::parseMode(QStringLiteral("gears")) == FreeRideSession::Mode::Ride);
        QVERIFY(!FreeRideSession::parseMode(QStringLiteral("power")));
        // ride + real gears: the trainer gets the grade (FTMS 0x11)
        f.realGears();
        f.ride = f.makeRide();
        QVERIFY(!f.ride->virtualGears());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("simulation"));
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.ride->setGrade(3.0);
        f.pedal(85, 220);
        QVERIFY(f.ride->start());
        QCOMPARE(sims.count(), 1);
        QCOMPARE(sims.last().at(0).toDouble(), 3.0);
        QVERIFY(f.ride->simulationActive());
        QCOMPARE(f.ride->gearTargetWatts(), 0);
        f.ride_for(60000);
        QCOMPARE(targets.count(), 0);
        // speed from power on the grade, with inertia
        const double v = road::speedForPower(f.ride->roadModel(3.0), 220);
        QVERIFY2(qAbs(f.ride->speedKmh() / 3.6 - v) < 0.4, qPrintable(QStringLiteral("%1 vs %2").arg(f.ride->speedKmh() / 3.6).arg(v)));
        f.ride->setGrade(5.0);
        QCOMPARE(sims.last().at(0).toDouble(), 5.0);
        // the Click connects (virtual gears on) and shifts - the grade stays
        f.gears->setSessions(f.ride.get(), nullptr);
        const int gear = f.gears->gearIndex();
        f.gears->setClickConnected(true);
        f.gears->controllerShift(1);
        QVERIFY(f.gears->enabled());
        QVERIFY(f.ride->virtualGears());
        QCOMPARE(f.gears->gearIndex(), gear + 1);
        QCOMPARE(f.ride->gradePercent(), 5.0);
        // a trainer that cannot simulate: virtual gears via ERG anyway
        f.gears->setClickConnected(false);
        f.hub->setTrainerSimulationSupport(0);
        QVERIFY(f.ride->simulationFallback());
        QVERIFY(f.ride->virtualGears());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));
        f.ride->advanceClock(250);
        QVERIFY(f.hub->requestedTarget() > 0);
        f.ride->finish(false);
    }

    // Soft start, Ride mode with real gears (FTMS 0x11): 0 % until the
    // flywheel turns (cadence >= 40 rpm for 2 s), a linear ramp to the road
    // grade over 4 s, re-armed after > 5 s without cadence.
    void softStartRealGears()
    {
        SoftStartOn on;
        Fixture f;
        QVERIFY(f.init());
        f.realGears();
        f.ride = f.makeRide();
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("simulation"));
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        f.ride->setGrade(5.0);
        f.pedal(0, 0);
        QVERIFY(f.ride->start());
        QCOMPARE(sims.count(), 1);
        QCOMPARE(sims.last().at(0).toDouble(), 0.0);   // not the road's 5 %
        QVERIFY(f.ride->resistance().softStart() == RideResistance::SoftStart::Waiting);
        f.ride_for(6000);   // standing
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        // slow pedaling (< 40 rpm) does not count as a turning flywheel
        f.pedal(30, 60);
        f.ride_for(3000);
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        // >= 40 rpm: still 0 % for 2 s
        f.pedal(85, 200);
        f.ride_for(1750);
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        QVERIFY(f.ride->resistance().softStart() == RideResistance::SoftStart::Waiting);
        f.ride_for(250);
        QVERIFY(f.ride->resistance().softStart() == RideResistance::SoftStart::Ramping);
        // the ramp: rising, rate limited (>= 1 s apart), ends at the grade
        sims.clear();
        f.ride_for(RideResistance::kSoftStartRampMs + 1500);
        QVERIFY(f.ride->resistance().softStart() == RideResistance::SoftStart::Done);
        QCOMPARE(f.hub->requestedGrade(), 5.0);
        QVERIFY2(sims.count() >= 3 && sims.count() <= 6, qPrintable(QString::number(sims.count())));
        double prev = 0;
        int between = 0;
        for (const auto &a : sims) {
            const double g = a.at(0).toDouble();
            QVERIFY2(g >= prev, qPrintable(QStringLiteral("%1 after %2").arg(g).arg(prev)));
            if (g > 0 && g < 5.0)
                ++between;
            prev = g;
        }
        QVERIFY(between >= 2);
        // a new grade during the ride goes out as before (no soft start)
        f.ride->setGrade(6.0);
        QCOMPARE(f.hub->requestedGrade(), 6.0);
        // coasting a little (< 5 s): the grade stays
        f.pedal(0, 0);
        f.ride_for(4500);
        QCOMPARE(f.hub->requestedGrade(), 6.0);
        // stopped > 5 s: re-armed, 0 % again
        f.ride_for(1000);
        QVERIFY(f.ride->resistance().softStart() == RideResistance::SoftStart::Waiting);
        f.ride_for(1000);
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        // pedaling again: 2 s, then the ramp to 6 %
        f.pedal(80, 180);
        f.ride_for(2000);
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        f.ride_for(1500);
        QVERIFY(f.hub->requestedGrade() > 0.0 && f.hub->requestedGrade() < 6.0);
        f.ride_for(5000);
        QCOMPARE(f.hub->requestedGrade(), 6.0);
        // pause + resume arms it again
        f.ride->pause();
        f.ride->resume();
        QCOMPARE(f.hub->requestedGrade(), 0.0);
        f.ride->finish(false);
    }

    // ERG (Free Ride ERG mode, virtual gears via ERG) never waits for the flywheel.
    void softStartNotForErg()
    {
        SoftStartOn on;
        Fixture f;
        QVERIFY(f.init());
        f.realGears();
        f.ride = f.makeRide();
        f.ride->setTargetWatts(210);
        f.ride->setMode(QStringLiteral("erg"));
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        f.pedal(0, 0);
        QVERIFY(f.ride->start());
        QCOMPARE(f.hub->requestedTarget(), 210);   // at once
        f.ride_for(3000);
        QCOMPARE(f.hub->requestedTarget(), 210);
        QCOMPARE(sims.count(), 0);
        f.ride->finish(false);
        // virtual gears via ERG (trainer without simulation): gear watts as soon as pedaling
        Fixture g;
        QVERIFY(g.init());
        QCOMPARE(g.ride->resistancePath(), QStringLiteral("gearErg"));
        g.ride->setGrade(5.0);
        g.pedal(85, 200);
        QVERIFY(g.ride->start());
        g.ride_for(250);
        QVERIFY(g.hub->requestedTarget() > 0);
        QVERIFY(!g.hub->simulationActive());
        g.ride->finish(false);
    }

    void gearSimulationPath()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: tst_virtualgears hybrid*)
        // simulating trainer that reports its speed: path a
        f.hub->setTrainerSimulationSupport(1);
        f.pedal(85, 200);
        f.hub->updateSpeed(30.0);
        QVERIFY(f.hub->trainerReportsSpeed());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearSimulation"));
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.ride->setGrade(2.0);
        QVERIFY(f.ride->start());
        f.ride_for(5000);
        QVERIFY(sims.count() >= 1);
        QCOMPARE(targets.count(), 0);
        const double before = f.ride->simulationGradePercent();
        QVERIFY(qAbs(before - f.ride->gearSimulationGrade()) < 0.11);
        // harder gear at the same trainer speed = steeper grade sent, at once
        const int count = sims.count();
        QVERIFY(f.gears->shiftUp());
        QCOMPARE(sims.count(), count + 1);
        QVERIFY(f.ride->simulationGradePercent() > before);
        // speed follows the virtual gear, not the trainer
        f.ride->advanceClock(250);
        QVERIFY(qAbs(f.ride->speedKmh() / 3.6 - f.gears->gearing().virtualSpeed(85)) < 0.01);
        // in the gear nearest the reference (50×17 = 2.94 ≈ Zwift gear 15, 3.03)
        // the trainer gets about the road grade
        QVERIFY(!f.gears->setGear(1, f.gears->setup().cogs.indexOf(17)));   // no chainring/cog combinations
        QVERIFY(f.gears->setGearIndex(14));
        f.hub->updateSpeed(f.gears->gearing().virtualSpeed(85) * 3.6);
        f.ride_for(2000);
        QVERIFY2(qAbs(f.ride->simulationGradePercent() - 2.0) < 1.0, qPrintable(QString::number(f.ride->simulationGradePercent())));
        f.ride->finish(false);
        // the trainer disconnects: no speed reported any more -> path b
        f.hub->setTrainerConnected(false);
        QVERIFY(!f.hub->trainerReportsSpeed());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));
    }

    void controllerButtons()
    {
        Fixture f;
        QVERIFY(f.init());
        const int gear = f.gears->gearIndex();
        f.ride->setGrade(4.0);
        f.ride->controllerShiftUp();
        QCOMPARE(f.gears->gearIndex(), gear + 1);
        f.ride->controllerShift(-1);
        f.ride->controllerShift(-1);
        QCOMPARE(f.gears->gearIndex(), gear - 1);
        QCOMPARE(f.ride->gradePercent(), 4.0);   // never the grade / difficulty
        f.ride->setMode(QStringLiteral("erg"));
        const int target = f.ride->targetWatts();
        f.ride->controllerShiftUp();
        QCOMPARE(f.ride->targetWatts(), target + FreeRideSession::kControllerErgStep);
        QCOMPARE(f.gears->gearIndex(), gear - 1);
        f.ride->controllerShiftDown();
        QCOMPARE(f.ride->targetWatts(), target);
        // real gears (virtual gears off): the Click still shifts, never the grade
        f.ride->setMode(QStringLiteral("ride"));
        f.realGears();
        f.ride->controllerShiftUp();
        QCOMPARE(f.gears->gearIndex(), gear);
        QCOMPARE(f.ride->gradePercent(), 4.0);
        QCOMPARE(f.ride->targetWatts(), target);
    }

    // Sessions only start with Start: pedaling (e.g. while clipping in) never
    // starts a free ride, on the open page or elsewhere, and never resumes it.
    void pedalingNeverStartsRide()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy started(f.ride.get(), &FreeRideSession::started);
        f.gears->setActivePage(QStringLiteral("freeride"));
        for (int i = 0; i < 20; ++i) {
            f.pedal(95, 250);
            f.ride_for(1000);
        }
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        QCOMPARE(started.count(), 0);
        QCOMPARE(f.ride->elapsedSeconds(), 0);
        QCOMPARE(f.ride->samples().size(), 0);
        QVERIFY(!f.ride->inProgress());
        // power only (trainer without cadence) neither
        f.pedal(0, 300);
        f.ride_for(3000);
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        // Start does
        QVERIFY(f.ride->start());
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        QCOMPARE(started.count(), 1);
        f.ride_for(4000);
        QCOMPARE(f.ride->elapsedSeconds(), 4);
        // pause: pedaling on does not resume
        f.ride->pause();
        f.pedal(90, 200);
        f.ride_for(5000);
        QCOMPARE(f.ride->state(), QStringLiteral("paused"));
        QCOMPARE(f.ride->elapsedSeconds(), 4);
        // Space / the button
        f.ride->togglePause();
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        // coasting does not pause it
        f.pedal(0, 0);
        f.ride_for(3000);
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        QCOMPARE(f.ride->elapsedSeconds(), 7);
        f.ride->finish(false);
        // finished: pedaling does not start the next ride either
        f.pedal(90, 200);
        f.ride_for(3000);
        QCOMPARE(f.ride->state(), QStringLiteral("finished"));
        f.ride->togglePause();   // "Ride Again" / Space
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        f.ride->finish(false);
    }

    void rideIsSavedAndExportable()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy saved(f.ride.get(), &FreeRideSession::rideSaved);
        f.pedal(90, 180);
        f.hub->updateHeartRate(130);
        QVERIFY(f.ride->start());
        f.ride_for(20000);
        f.ride->setMode(QStringLiteral("erg"));
        f.ride_for(5000);
        QCOMPARE(f.ride->elapsedSeconds(), 25);
        QVERIFY(f.ride->distanceMeters() > 150);
        QCOMPARE(f.ride->avgWatts(), 180);
        QCOMPARE(f.ride->powerSeries().size(), 25);
        QCOMPARE(f.ride->speedSeries().size(), 25);
        QVERIFY(f.ride->speedSeries().first().toDouble() > 20);
        const double distance = f.ride->distanceMeters();
        const qint64 id = f.ride->stop();
        QVERIFY(id > 0);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(f.ride->lastRideId(), id);
        QCOMPARE(f.ride->state(), QStringLiteral("finished"));
        QCOMPARE(f.hub->requestedTarget(), 0);

        const auto r = f.db.ride(id);
        QVERIFY(r);
        QCOMPARE(r->workoutName, QStringLiteral("Free Ride"));
        QCOMPARE(r->sport, QStringLiteral("cycling"));
        QCOMPARE(r->workoutId, qint64(-1));
        QCOMPARE(r->durationSec, 25);
        QVERIFY(!r->ergMode);   // mostly ridden in gears
        QVERIFY(qAbs(r->distanceM - distance) < 0.01);
        QVERIFY(qAbs(r->avgPower - 180) < 0.5);
        QVERIFY(qAbs(r->avgHeartRate - 130) < 0.5);
        const QVector<Sample> samples = f.db.rideSamples(id);
        QCOMPARE(samples.size(), 25);
        QVERIFY(samples.first().speedMps > 5);
        QCOMPARE(samples.first().targetPower, 0);
        QCOMPARE(samples.last().targetPower, 180);

        const QString csv = csv::samplesToCsv(*r, samples);
        QVERIFY(csv.contains(QStringLiteral("heart_rate_bpm,speed_kmh")));
        QVERIFY(csv.contains(QStringLiteral("# distance_m,")));
        const QByteArray fit = fit::encodeActivity(*r, samples);
        QVERIFY(fit.size() > 500);
        QCOMPARE(fit.mid(8, 4), QByteArray(".FIT"));

        // too short: not saved
        f.ride->reset();
        QVERIFY(f.ride->start());
        f.ride_for(3000);
        QCOMPARE(f.ride->stop(), qint64(-1));
    }

    void exclusiveWithTraining()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy trainingRefused(f.training.get(), &TrainingSession::startRefused);
        QSignalSpy rideRefused(f.ride.get(), &FreeRideSession::startRefused);
        QVERIFY(f.ride->start());
        f.training->start();
        QCOMPARE(trainingRefused.count(), 1);
        QVERIFY(!f.training->active());
        f.ride->finish(false);

        f.training->start();
        QVERIFY(f.training->active());
        QVERIFY(!f.ride->start());
        QCOMPARE(rideRefused.count(), 1);
        QCOMPARE(f.ride->state(), QStringLiteral("finished"));
        f.training->discard();

        // a workout paused before anything was recorded is given up when the
        // free ride starts; a merely loaded one is not touched
        f.training->loadWorkout(f.training->engine().workout());
        QVERIFY(f.ride->start());
        QVERIFY(!f.training->active());
        f.ride->finish(false);
        f.training->start();
        f.training->pause();   // manual clock: nothing recorded
        QVERIFY(f.training->abandonable());
        QVERIFY(f.ride->start());
        QVERIFY(!f.training->active());
        f.ride->finish(false);

        // the guard is removed with the free ride session
        f.ride.reset();
        f.training->start();
        QVERIFY(f.training->active());
        f.training->discard();
    }

    void exclusiveWithRowing()
    {
        Fixture f;
        QVERIFY(f.init());
        RowingRecords records(&f.db, f.profiles.get());
        WorkoutPlanner planner(&f.db, f.profiles.get());
        RowingSession rowing(&f.db, f.profiles.get(), f.hub.get(), &records, &planner);
        rowing.setManualClock(true);
        rowing.setTrainingSession(f.training.get());
        f.ride->setStartBlocker(QStringLiteral("rowing"), [&rowing] { return rowing.active(); });
        rowing.setStartBlocker(QStringLiteral("freeRide"), [&f] { return f.ride->active(); },
                               QStringLiteral("A free ride is running."));
        QSignalSpy rowRefused(&rowing, &RowingSession::startRefused);
        QVERIFY(f.ride->start());
        QVERIFY(!rowing.start());
        QCOMPARE(rowRefused.count(), 1);
        QCOMPARE(rowRefused.first().first().toString(), QStringLiteral("A free ride is running."));
        f.ride->finish(false);
        QVERIFY(rowing.start());
        QVERIFY(!f.ride->start());
        rowing.finish(false);
        QVERIFY(f.ride->start());
        // both other guards coexist on the workout session
        f.training->start();
        QVERIFY(!f.training->active());
        f.ride->finish(false);
        rowing.setStartBlocker(QStringLiteral("freeRide"), {});
    }

    void previewGearsForUnsavedSetup()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString before = f.gears->gearLabel();
        // invalid -> empty
        QVERIFY(f.gears->previewGears({}, { 11, 12 }, 2105, true).isEmpty());
        QVERIFY(f.gears->previewGears({ 50 }, { 11 }, 100, true).isEmpty());
        // 1x: every cog, easiest first, speed at 90 rpm = 90/60 * ratio * 2.105 m
        const QVariantList single = f.gears->previewGears({ 40 }, { 11, 21, 42 }, 2105, false);
        QCOMPARE(single.size(), 3);
        const QVariantMap easiest = single.first().toMap();
        QCOMPARE(easiest.value(QStringLiteral("label")).toString(), QStringLiteral("40×42"));
        QCOMPARE(easiest.value(QStringLiteral("rearTeeth")).toInt(), 42);
        QVERIFY(qAbs(easiest.value(QStringLiteral("speedKmh")).toDouble() - 1.5 * 40.0 / 42.0 * 2.105 * 3.6) < 0.06);
        const QVariantMap hardest = single.last().toMap();
        QCOMPARE(hardest.value(QStringLiteral("label")).toString(), QStringLiteral("40×11"));
        QVERIFY(hardest.value(QStringLiteral("watts")).toInt() > easiest.value(QStringLiteral("watts")).toInt());
        // 2x: derailleur lists every combination, sequential skips duplicates
        const QVariantList rings { 34, 50 };
        const QVariantList cogs { 11, 12, 13, 14, 15, 17, 19, 21, 24, 27, 30, 34 };
        QCOMPARE(f.gears->previewGears(rings, cogs, 2105, false).size(), 24);
        const int seq = f.gears->previewGears(rings, cogs, 2105, true).size();
        QVERIFY(seq > 12 && seq < 24);
        const double at60 = f.gears->previewGears(rings, cogs, 2105, false, 60).first().toMap().value(QStringLiteral("speedKmh")).toDouble();
        const double at90 = f.gears->previewGears(rings, cogs, 2105, false, 90).first().toMap().value(QStringLiteral("speedKmh")).toDouble();
        QVERIFY(qAbs(at60 * 1.5 - at90) < 0.11);
        // nothing stored
        QCOMPARE(f.gears->gearLabel(), before);
        // defaults map
        const QVariantMap def = f.gears->defaultGearSetup();
        QCOMPARE(def.value(QStringLiteral("circumferenceMm")).toInt(), GearSetup::kDefaultCircumferenceMm);
        QCOMPARE(def.value(QStringLiteral("chainrings")).toList().size(), 2);
        QCOMPARE(def.value(QStringLiteral("cogs")).toList().size(), 12);
    }

    void gearSetupPerProfile()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(!f.gears->setGearSetup({ 5 }, { 11, 28 }, 2105, true));
        QVERIFY(!f.gears->lastError().isEmpty());
        QVERIFY(!f.gears->setGearSetup({ 50.5 }, { 11, 28 }, 2105, true));
        QVERIFY(!f.gears->validateGearSetup({ 50 }, { 11 }, 100).isEmpty());
        QVERIFY(f.gears->validateGearSetup({ 50 }, { 11 }, 2105).isEmpty());
        QCOMPARE(f.gears->parseTeeth(QStringLiteral("10, 12,14")), QVariantList({ 10, 12, 14 }));

        QSignalSpy setupChanged(f.gears.get(), &VirtualGears::gearSetupChanged);
        // the old chainring / cog setup is still stored, but virtual shifting
        // always uses Zwift's 24 gears
        QVERIFY(f.gears->setGearSetup({ 42 }, { 44, 10, 11, 13, 15, 17, 19, 21, 24, 28, 32, 38 }, 2136, false));
        QVERIFY(f.gears->lastError().isEmpty());
        QCOMPARE(setupChanged.count(), 1);
        QCOMPARE(f.gears->gearCount(), 24);
        QVERIFY(f.gears->canShiftFrontUp());   // jumps 3 Zwift gears
        QVERIFY(f.gears->setBikePhysics(11.0, 0.40, 0.006));
        QVERIFY(!f.gears->setBikePhysics(-1, 0.40, 0.006));
        QVERIFY(!f.gears->setGear(0, 3));
        QVERIFY(f.gears->setGearIndex(6));
        QCOMPARE(f.gears->gearLabel(), QStringLiteral("7"));
        f.gears->flushSettings();

        // reloaded by a new instance (app restart)
        auto again = f.makeGears();
        auto againRide = std::make_unique<FreeRideSession>(&f.db, f.profiles.get(), f.hub.get(), again.get());
        againRide->setManualClock(true);
        again->setBusyCheck([&] { return againRide->active(); });
        QCOMPARE(again->gearLabel(), QStringLiteral("12"));   // every start: Zwift gear 12
        QCOMPARE(again->gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2136);
        QCOMPARE(again->setup().chainrings, QVector<int>({ 42 }));   // data kept
        QCOMPARE(again->gearSetup().value(QStringLiteral("bikeWeightKg")).toDouble(), 11.0);
        QVERIFY(!again->sequential());

        // another rider has their own bike
        const qint64 first = f.profiles->profileId();
        const qint64 second = f.profiles->createProfile(QStringLiteral("Guest"));
        QVERIFY(second > 0);
        QVERIFY(f.profiles->selectProfile(second));
        QCOMPARE(again->gearSetup().value(QStringLiteral("chainrings")).toList().size(), 2);   // defaults
        again->shiftUp();
        again->flushSettings();
        // another rider starts in their own start gear (from their FTP)
        QVERIFY(f.profiles->selectProfile(first));
        QCOMPARE(again->gearLabel(), QString::number(again->startGear()));
        QCOMPARE(again->gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2136);
        // a running ride keeps its bike until it is finished
        QVERIFY(againRide->start());
        QVERIFY(f.profiles->selectProfile(second));
        QCOMPARE(again->gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2136);
        againRide->finish(false);
        QCOMPARE(again->gearSetup().value(QStringLiteral("chainrings")).toList().size(), 2);
        f.ride.reset();
        againRide.reset();
        again.reset();
        f.training.reset();
    }

    // Bug: ERG pressed on the Free Ride page did nothing until a ride ran.
    // The selector (FreeRide.mode = "erg") now puts the trainer in ERG at
    // once while the page is open (before Start); running as before; paused
    // on resume. Ride / leaving the page releases it.
    void ergSelectorEngagesTrainerWhenIdle()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: tst_virtualgears hybrid*)
        f.realGears();
        FakeTrainer trainer(f.hub.get());
        f.ride->setTargetWatts(210);
        QVERIFY(trainer.log.isEmpty());
        f.gears->setActivePage(QStringLiteral("freeride"));   // Main.qml: Gears.activePage
        QVERIFY(trainer.log.isEmpty());                       // Ride: the trainer stays free
        QVERIFY(!f.ride->ergHolding());

        QSignalSpy targetChanged(f.ride.get(), &FreeRideSession::targetChanged);
        f.ride->setProperty("mode", QStringLiteral("erg"));   // = onActivated: FreeRide.mode = v
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        QVERIFY(f.ride->ergEnabled());
        QVERIFY(f.ride->ergHolding());
        QVERIFY(trainer.inErg(210));
        QCOMPARE(trainer.log, QStringList { QStringLiteral("erg 210") });
        QCOMPARE(f.hub->requestedTarget(), 210);
        QCOMPARE(f.ride->requestedWatts(), 210);
        QVERIFY(targetChanged.count() > 0);

        // steps and the Zwift Click change the held target right away
        f.ride->adjustTargetWatts(5);
        QVERIFY(trainer.inErg(215));
        f.gears->setSessions(f.ride.get(), nullptr);
        f.gears->setEnabled(true);   // Click users have virtual gears on: ERG still wins
        QVERIFY(f.ride->ergHolding());
        f.gears->controllerShift(1);
        QVERIFY(trainer.inErg(225));
        f.gears->controllerShift(-1);
        f.gears->controllerShift(-1);
        QVERIFY(trainer.inErg(205));
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("erg"));

        // Ride releases the ERG target, ERG takes it again. (Virtual gears on
        // a simulating trainer whose cadence / speed is not known yet: the
        // gear-simulation path, which holds the plain road grade while the
        // rider does not pedal - see SensorHub::trainerFeedbackUnknown().)
        f.ride->setProperty("mode", QStringLiteral("ride"));
        QVERIFY(!f.ride->ergHolding());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearSimulation"));
        QCOMPARE(trainer.last(), QStringLiteral("sim 0"));
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.ride->setProperty("mode", QStringLiteral("erg"));
        QVERIFY(trainer.inErg(205));

        // leaving the page releases, coming back holds again
        f.gears->setActivePage(QStringLiteral("dashboard"));
        QVERIFY(!f.ride->ergHolding());
        QCOMPARE(trainer.last(), QStringLiteral("free"));
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(trainer.inErg(205));

        // a trainer that connects later gets the target
        f.hub->setTrainerConnected(false);
        QVERIFY(!f.ride->ergHolding());
        trainer.log.clear();
        f.hub->setTrainerConnected(true);
        QVERIFY(f.ride->ergHolding());
        QVERIFY(trainer.inErg(205));
        QVERIFY(trainer.log.contains(QStringLiteral("erg 205")));
    }

    void ergSelectorBeforeStartRunningAndPaused()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: tst_virtualgears hybrid*)
        f.realGears();
        qint64 now = 1000000;   // the hub's clock (3 s display power)
        f.hub->setClockForTesting([&now] { return now; });
        FakeTrainer trainer(f.hub.get());
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.ride->setTargetWatts(180);

        // before Start: ERG holds right away
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        f.ride->setMode(QStringLiteral("erg"));
        QVERIFY(trainer.inErg(180));
        QVERIFY(f.ride->ergHolding());
        // pedal strokes do not start the ride; Start does, ERG stays
        f.pedal(85, 175);
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        QVERIFY(f.ride->ergHolding());
        QVERIFY(f.ride->start());
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        QVERIFY(!f.ride->ergHolding());
        QVERIFY(trainer.inErg(180));
        f.ride_for(5000);
        QVERIFY(trainer.inErg(180));

        // running: ERG -> Ride (simulation) -> ERG
        f.ride->setMode(QStringLiteral("ride"));
        QVERIFY(trainer.last().startsWith(QStringLiteral("sim ")));
        QVERIFY(!trainer.erg);
        f.ride_for(2000);
        now += 5000;
        f.pedal(85, 212);
        now += 3500;        // the 3 s display power settles on 212 W
        f.hub->refreshDisplayPower();
        QCOMPARE(f.hub->displayPower(), 212);
        f.ride_for(1000);
        f.ride->setMode(QStringLiteral("erg"));
        QCOMPARE(f.ride->targetWatts(), 210);   // 3 s power rounded to 5 W
        QVERIFY(trainer.inErg(210));
        // virtual gears on/off while ERG: still ERG
        f.gears->setEnabled(true);
        QVERIFY(trainer.inErg(210));
        f.ride->controllerShift(1);
        QVERIFY(trainer.inErg(220));
        f.realGears();
        QVERIFY(trainer.inErg(220));
        // Ride with virtual gears -> gear watts / grade, back to ERG
        f.gears->setEnabled(true);
        f.ride->setMode(QStringLiteral("ride"));
        QVERIFY(f.ride->virtualGears());
        // a simulating trainer with cadence: the gear-adjusted grade, no ERG left over
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearSimulation"));
        QVERIFY(trainer.last().startsWith(QStringLiteral("sim ")));
        QVERIFY(!trainer.erg);
        f.ride->setMode(QStringLiteral("erg"));
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("erg"));
        QVERIFY(trainer.erg);
        const int target = f.ride->targetWatts();
        QCOMPARE(trainer.target, target);

        // paused: released; ERG pressed while paused applies on resume
        f.ride->setMode(QStringLiteral("ride"));
        f.ride->pause();
        QCOMPARE(trainer.last(), QStringLiteral("free"));
        trainer.log.clear();
        f.ride->setMode(QStringLiteral("erg"));
        QVERIFY(!f.ride->ergHolding());
        QVERIFY(trainer.log.isEmpty());   // nothing while paused
        f.ride->resume();
        QCOMPARE(trainer.last(), QStringLiteral("erg %1").arg(f.ride->targetWatts()));

        // finished: released and not taken again by itself
        f.ride->finish(false);
        QCOMPARE(trainer.last(), QStringLiteral("free"));
        QVERIFY(!f.ride->ergHolding());
        // reset -> idle on the open page: ERG holds again
        f.ride->reset();
        QVERIFY(f.ride->ergHolding());
        QVERIFY(trainer.erg);
    }

    void ergHoldYieldsToOtherSessions()
    {
        Fixture f;
        QVERIFY(f.init());
        FakeTrainer trainer(f.hub.get());
        f.ride->setTargetWatts(200);
        // no page: selecting ERG elsewhere (stored mode) does not grab the trainer
        f.ride->setMode(QStringLiteral("erg"));
        QVERIFY(trainer.log.isEmpty());
        f.ride->setMode(QStringLiteral("ride"));
        // a workout riding on Watt Training owns the trainer
        Workout w;
        w.name = QStringLiteral("W");
        Interval i;
        i.durationSec = 600;
        i.startPercent = i.endPercent = 0.5;
        w.intervals << i;
        f.training->loadWorkout(w);
        f.training->start();
        QVERIFY(f.training->running());
        f.gears->setActivePage(QStringLiteral("freeride"));
        trainer.log.clear();
        f.ride->setMode(QStringLiteral("erg"));
        QVERIFY(!f.ride->ergHolding());
        QVERIFY(trainer.log.isEmpty());
        // the workout is discarded: the open page's ERG applies
        f.training->discard();
        QVERIFY(f.ride->ergHolding());
        QVERIFY(trainer.inErg(200));
    }

    void firstErgTargetIsSixtyPercentFtp()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.profiles->saveProfile({ { QStringLiteral("ftp"), 300 } }));
        auto fresh = f.makeRide();
        QCOMPARE(fresh->targetWatts(), 180);   // no saved target: 60 % FTP
        fresh->setTargetWatts(240);
        auto again = f.makeRide();
        QCOMPARE(again->targetWatts(), 240);   // saved target
    }

    void physicsUseRiderWeight()
    {
        Fixture f;
        QVERIFY(f.init());
        const road::Model m = f.ride->roadModel(3.0);
        QCOMPARE(m.massKg, f.profiles->weight() + 9.0);
        QCOMPARE(m.gradePercent, 3.0);
        // auto CdA (default): from the rider's size
        QVERIFY(f.gears->cdaAuto());
        QCOMPARE(m.cda, road::autoCda(f.profiles->weight(), f.profiles->heightCm()));
        // weight and height edits apply at once
        QVERIFY(f.profiles->saveProfile({ { QStringLiteral("weight"), 90.0 }, { QStringLiteral("heightCm"), 185.0 } }));
        QCOMPARE(f.ride->roadModel(0).massKg, 99.0);
        QCOMPARE(f.ride->roadModel(0).cda, road::autoCda(90, 185));
        QVERIFY(f.ride->roadModel(0).cda > 0.34);
        // manual CdA
        QVERIFY(f.gears->setBikePhysics(9.0, 0.30, 0.004, false));
        QCOMPARE(f.ride->roadModel(0).cda, 0.30);
        QVERIFY(f.gears->setBikePhysics(9.0, 0.30, 0.004, true));
        QCOMPARE(f.ride->roadModel(0).cda, road::autoCda(90, 185));
        QCOMPARE(f.gears->physicsSummary(), QStringLiteral("Rider + bike: 99 kg · CdA %1 (auto)")
                                                .arg(road::autoCda(90, 185), 0, 'f', 2));
    }

    // Pedaling never starts a ride, whatever page is open; a running ride is
    // not affected by the page.
    void pagesNeverStartRide()
    {
        Fixture f;
        QVERIFY(f.init());
        for (const char *page : { "freeride", "dashboard", "video", "rowing", "history" }) {
            f.gears->setActivePage(QString::fromLatin1(page));
            f.pedal(90, 200);
            f.ride_for(2000);
            QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        }
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(f.ride->start());
        f.gears->setActivePage(QStringLiteral("history"));
        QVERIFY(f.ride->running());
        f.ride->finish(false);
    }

    // An idle (or empty paused) free ride never blocks another session.
    void otherSessionsStartBesideIdleRide()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy refused(f.training.get(), &TrainingSession::startRefused);
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.training->start();
        QCOMPARE(refused.count(), 0);
        QVERIFY(f.training->active());
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        f.training->discard();

        // an empty paused free ride is given up by the workout's start
        QVERIFY(f.ride->start());
        f.ride->pause();
        QVERIFY(f.ride->abandonable());
        f.training->start();
        QCOMPARE(refused.count(), 0);
        QVERIFY(f.training->active());
        QCOMPARE(f.ride->state(), QStringLiteral("finished"));
        f.training->discard();

        // rowing (wired like SessionGuard)
        RowingRecords records(&f.db, f.profiles.get());
        WorkoutPlanner planner(&f.db, f.profiles.get());
        RowingSession rowing(&f.db, f.profiles.get(), f.hub.get(), &records, &planner);
        rowing.setManualClock(true);
        rowing.setStartBlocker(QStringLiteral("freeRide"), [&f] { return f.ride->inProgress(); });
        rowing.setAbandonCanceller(QStringLiteral("freeRide"), [&f] { f.ride->abandon(); });
        f.ride->reset();
        QVERIFY(rowing.start());
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
        rowing.finish(false);
        rowing.setStartBlocker(QStringLiteral("freeRide"), {});
        rowing.setAbandonCanceller(QStringLiteral("freeRide"), {});
    }

    // A long gap between timer ticks (sleep) adds no time or distance.
    void longClockGapIsAPause()
    {
        Fixture f;
        QVERIFY(f.init());
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        f.ride_for(3000);
        const double d = f.ride->distanceMeters();
        const int t = f.ride->elapsedSeconds();
        f.ride->advanceWallClock(10 * 60 * 1000);
        QCOMPARE(f.ride->distanceMeters(), d);
        QCOMPARE(f.ride->elapsedSeconds(), t);
        QCOMPARE(f.ride->samples().size(), 3);
        f.ride->advanceWallClock(250);   // a normal tick counts
        QVERIFY(f.ride->distanceMeters() > d);
        f.ride->finish(false);
    }

    // The 250 ms clock only runs while it is needed.
    void timerRunsOnlyWhenNeeded()
    {
        Fixture f;
        QVERIFY(f.init());
        f.ride.reset();
        FreeRideSession ride(&f.db, f.profiles.get(), f.hub.get(), f.gears.get());   // real timer
        QVERIFY(!ride.timerActive());   // idle
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(ride.gearHolding());    // virtual gear hold follows the cadence
        QVERIFY(ride.timerActive());
        ride.setMode(QStringLiteral("erg"));
        QVERIFY(ride.ergHolding());     // a fixed ERG target needs no clock
        QVERIFY(!ride.timerActive());
        ride.setMode(QStringLiteral("ride"));
        QVERIFY(ride.timerActive());
        f.gears->setActivePage(QStringLiteral("dashboard"));
        QVERIFY(!ride.timerActive());
        QVERIFY(ride.start());
        QVERIFY(ride.timerActive());
        ride.pause();
        QVERIFY(!ride.timerActive());
        ride.resume();
        QVERIFY(ride.timerActive());
        ride.finish(false);
        QVERIFY(!ride.timerActive());
    }

    // Virtual gears via ERG: engage from 18 rpm, release below 12 rpm, at
    // most one switch per second - no ERG / free ride flapping near 15 rpm.
    void gearErgHysteresis()
    {
        Fixture f;
        QVERIFY(f.init());
        FakeTrainer trainer(f.hub.get());
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        f.ride_for(2000);
        QVERIFY(trainer.erg);
        f.pedal(14, 60);           // below the coast cadence, above the release cadence
        f.ride_for(2000);
        QVERIFY(trainer.erg);
        QVERIFY(f.hub->requestedTarget() >= FreeRideSession::kMinGearWatts);
        f.pedal(10, 20);           // released
        f.ride->advanceClock(250);
        QVERIFY(!trainer.erg);
        QCOMPARE(trainer.last(), QStringLiteral("free"));
        f.pedal(16, 40);           // not yet engaged again
        f.ride_for(2000);
        QVERIFY(!trainer.erg);
        f.pedal(20, 80);
        f.ride->advanceClock(250);
        QVERIFY(trainer.erg);
        // right after engaging a drop does not release at once (rate limit)
        f.pedal(5, 0);
        f.ride->advanceClock(250);
        QVERIFY(trainer.erg);
        f.ride_for(1000);
        QVERIFY(!trainer.erg);
        f.ride->finish(false);
    }

    // The rider's weight of a running ride stays when the profile changes.
    void rideKeepsItsRiderPhysics()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setBusyCheck([&f] { return f.ride->active(); });
        f.gears->setHub(f.hub.get());
        QVERIFY(f.profiles->saveProfile({ { QStringLiteral("weight"), 60.0 } }));
        QCOMPARE(f.gears->systemMassKg(), 69.0);
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        QVERIFY(f.profiles->createProfile(QStringLiteral("Heavy")) > 0);
        QVERIFY(f.profiles->saveProfile({ { QStringLiteral("weight"), 95.0 } }));
        QCOMPARE(f.gears->systemMassKg(), 69.0);   // the ride's rider
        QCOMPARE(f.hub->riderMassKg(), 69.0);
        QCOMPARE(f.ride->roadModel(0).massKg, 69.0);
        f.ride->finish(false);
        QCOMPARE(f.gears->systemMassKg(), 104.0);  // now the current profile
        QCOMPARE(f.hub->riderMassKg(), 104.0);
    }

    // A Zwift Click recognised during a ride does not swap the bike mid-ride.
    void clickConnectDuringRideKeepsBike()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setBusyCheck([&f] { return f.ride->active(); });
        const QString before = f.gears->summary();
        GearSetup other = GearSetup::defaults();
        other.chainrings = { 36, 52 };
        QVERIFY(f.db.setGearSetupJson(f.profiles->profileId(), other.toJsonText()));
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        f.gears->shiftDown();
        const QString gear = f.gears->gearText();
        f.gears->setClickConnected(true);
        QCOMPARE(f.gears->summary(), before);   // nothing reloaded
        QCOMPARE(f.gears->gearText(), gear);     // the gear stays
        f.ride->finish(false);
        QCOMPARE(f.gears->gearText(), gear);
    }

    // Right after connecting, cadence / speed reporting is unknown: virtual
    // gears on a simulating trainer use the simulation path (no brief ERG);
    // only pedaling kFeedbackProbeMs without either switches to ERG.
    void unknownTrainerFeedbackMayReport()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setShiftMethod(QStringLiteral("gradient"));   // path a as such (Auto: tst_virtualgears hybrid*)
        qint64 now = 1000000;
        f.hub->setClockForTesting([&now] { return now; });
        f.hub->setTrainerConnected(false);
        f.hub->setTrainerConnected(true);
        f.hub->setTrainerSimulationSupport(1);
        QVERIFY(f.hub->trainerFeedbackUnknown());
        FakeTrainer trainer(f.hub.get());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearSimulation"));
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(!trainer.log.join(QLatin1Char(' ')).contains(QStringLiteral("erg")));
        // power without cadence or speed for a while: the trainer reports neither
        for (int i = 0; i <= 6; ++i) {
            f.hub->updatePower(150);
            now += 1000;
        }
        f.hub->updatePower(150);
        QVERIFY(!f.hub->trainerFeedbackUnknown());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));
        // a reconnect forgets it
        f.hub->setTrainerConnected(false);
        f.hub->setTrainerConnected(true);
        QVERIFY(f.hub->trainerFeedbackUnknown());
        f.hub->updateCadence(85);   // a trainer with cadence: known, simulation path
        QVERIFY(!f.hub->trainerFeedbackUnknown());
        QVERIFY(f.hub->cadenceReported());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearSimulation"));
        f.gears->setActivePage(QString());
    }

    void lastRideIdNotifiesAndSettersAreInvokable()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy changed(f.ride.get(), &FreeRideSession::lastRideIdChanged);
        f.pedal(90, 200);
        QVERIFY(f.ride->start());
        QCOMPARE(changed.count(), 1);
        f.ride_for(6000);
        const qint64 id = f.ride->stop();
        QVERIFY(id > 0);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(f.ride->lastRideId(), id);
        f.ride->reset();
        QCOMPARE(changed.count(), 3);
        QCOMPARE(f.ride->lastRideId(), qint64(-1));
        QVERIFY(f.ride->start());
        f.ride->discard();
        QCOMPARE(changed.count(), 5);   // start + discard
        QVERIFY(QMetaObject::invokeMethod(f.ride.get(), "setGrade", Q_ARG(double, 2.5)));
        QCOMPARE(f.ride->gradePercent(), 2.5);
        QVERIFY(QMetaObject::invokeMethod(f.ride.get(), "setTargetWatts", Q_ARG(int, 230)));
        QCOMPARE(f.ride->targetWatts(), 230);
        QVERIFY(f.ride->setProperty("gradePercent", 1.5));   // property assignment works too
        QCOMPARE(f.ride->gradePercent(), 1.5);
    }
};

QTEST_GUILESS_MAIN(TestFreeRide)
#include "tst_freeride.moc"
