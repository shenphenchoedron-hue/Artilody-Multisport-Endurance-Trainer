// FreeRideSession: modes Ride / ERG (old "gears" / "free" migrated), ERG
// hold, virtual gear targets (path b) and gear-adjusted grades (path a), real
// gears with the trainer simulating the grade, Zwift Click routing (never
// the difficulty / grade), auto-start, recording, mutual exclusion with
// workouts/rowing, gear setup persistence (VirtualGears).
#include "CsvExporter.h"
#include "Database.h"
#include "FitExporter.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RowingRecords.h"
#include "RowingSession.h"
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
        // most tests ride with virtual gears (a trainer without speed: path b, ERG)
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

class TestFreeRide : public QObject
{
    Q_OBJECT
private slots:
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
        QVERIFY(!gears.enabled());   // off until a Zwift Click / the toggle
        QVERIFY(!s.virtualGears());
        QCOMPARE(s.resistancePath(), QStringLiteral("simulation"));
        QVERIFY(!s.trainerConnected());
        QCOMPARE(s.speedKmh(), 0.0);
        QCOMPARE(s.watts(), 0);
        QCOMPARE(s.gearTargetWatts(), 0);
        QCOMPARE(s.requestedWatts(), 0);
        QVERIFY(s.powerSeries().isEmpty());
        QCOMPARE(gears.gearLabel(), QStringLiteral("50×21"));
        QCOMPARE(gears.gear().value(QStringLiteral("label")).toString(), gears.gearLabel());
        QCOMPARE(gears.gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2105);
        QCOMPARE(gears.referenceLabel(), QStringLiteral("50×17"));
        QCOMPARE(gears.summary(), QStringLiteral("50/34 · 11–34 12-speed · 700×25c · %1 gears").arg(gears.gearCount()));
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
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));   // no trainer speed: path b
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
        f.gears->setEnabled(false);
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
        // the Click shifts (and turns virtual gears on) - the grade stays
        f.gears->setSessions(f.ride.get(), nullptr);
        QSignalSpy notices(f.gears.get(), &VirtualGears::notice);
        const int gear = f.gears->gearIndex();
        f.gears->controllerShift(1);
        QCOMPARE(notices.count(), 1);
        QVERIFY(f.gears->enabled());
        QVERIFY(f.ride->virtualGears());
        QCOMPARE(f.gears->gearIndex(), gear + 1);
        QCOMPARE(f.ride->gradePercent(), 5.0);
        // a trainer that cannot simulate: virtual gears via ERG anyway
        f.gears->setEnabled(false);
        f.hub->setTrainerSimulationSupport(0);
        QVERIFY(f.ride->simulationFallback());
        QVERIFY(f.ride->virtualGears());
        QCOMPARE(f.ride->resistancePath(), QStringLiteral("gearErg"));
        f.ride->advanceClock(250);
        QVERIFY(f.hub->requestedTarget() > 0);
        f.ride->finish(false);
    }

    void gearSimulationPath()
    {
        Fixture f;
        QVERIFY(f.init());
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
        // in the reference gear the trainer gets about the road grade
        QVERIFY(f.gears->setGear(1, f.gears->setup().cogs.indexOf(17)));
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
        f.gears->setEnabled(false);
        f.ride->controllerShiftUp();
        QCOMPARE(f.gears->gearIndex(), gear);
        QCOMPARE(f.ride->gradePercent(), 4.0);
        QCOMPARE(f.ride->targetWatts(), target);
    }

    void autoStartOnPedaling()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy started(f.ride.get(), &FreeRideSession::started);
        f.ride->startWhenPedaling();
        QCOMPARE(f.ride->state(), QStringLiteral("ready"));
        QVERIFY(f.ride->waiting());
        QCOMPARE(f.hub->requestedTarget(), 0);   // free to spin up
        f.pedal(10, 0);
        QCOMPARE(f.ride->state(), QStringLiteral("ready"));
        f.pedal(FreeRideSession::kStartCadenceRpm, 0);
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        QCOMPARE(started.count(), 1);
        QVERIFY(f.hub->requestedTarget() >= FreeRideSession::kMinGearWatts);
        f.ride->finish(false);

        // trainer without cadence: power starts it too
        f.ride->reset();
        f.pedal(0, 0);
        f.ride->startWhenPedaling();
        f.pedal(0, FreeRideSession::kStartPowerWatts);
        QCOMPARE(f.ride->state(), QStringLiteral("running"));
        f.ride_for(8000);
        QVERIFY(f.ride->speedKmh() > 5);   // speed from power
        f.ride->finish(false);

        // armed ride is cancelled by stop()
        f.pedal(0, 0);
        f.ride->startWhenPedaling();
        QCOMPARE(f.ride->stop(), qint64(-1));
        QCOMPARE(f.ride->state(), QStringLiteral("idle"));
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
        f.ride->startWhenPedaling();
        QCOMPARE(rideRefused.count(), 2);
        QCOMPARE(f.ride->state(), QStringLiteral("finished"));
        f.training->discard();

        // an armed workout is cancelled when the free ride starts
        f.training->startWhenPedaling();
        QVERIFY(f.training->waiting());
        QVERIFY(f.ride->start());
        QVERIFY(!f.training->waiting());
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
        QVERIFY(f.gears->setGearSetup({ 42 }, { 44, 10, 11, 13, 15, 17, 19, 21, 24, 28, 32, 38 }, 2136, false));
        QVERIFY(f.gears->lastError().isEmpty());
        QCOMPARE(setupChanged.count(), 1);
        QCOMPARE(f.gears->gearCount(), 12);
        QVERIFY(!f.gears->canShiftFrontUp());
        QVERIFY(f.gears->setBikePhysics(11.0, 0.40, 0.006));
        QVERIFY(!f.gears->setBikePhysics(-1, 0.40, 0.006));
        QVERIFY(f.gears->setGear(0, 3));
        QCOMPARE(f.gears->gearLabel(), QStringLiteral("42×15"));
        f.gears->flushSettings();

        // reloaded by a new instance (app restart)
        auto again = f.makeGears();
        auto againRide = std::make_unique<FreeRideSession>(&f.db, f.profiles.get(), f.hub.get(), again.get());
        againRide->setManualClock(true);
        again->setBusyCheck([&] { return againRide->active(); });
        QCOMPARE(again->gearLabel(), QStringLiteral("42×15"));
        QCOMPARE(again->gearSetup().value(QStringLiteral("circumferenceMm")).toInt(), 2136);
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
        QVERIFY(f.profiles->selectProfile(first));
        QCOMPARE(again->gearLabel(), QStringLiteral("42×15"));
        // a running ride keeps its bike until it is finished
        QVERIFY(againRide->start());
        QVERIFY(f.profiles->selectProfile(second));
        QCOMPARE(again->gearLabel(), QStringLiteral("42×15"));
        againRide->finish(false);
        QCOMPARE(again->gearSetup().value(QStringLiteral("chainrings")).toList().size(), 2);
        f.ride.reset();
        againRide.reset();
        again.reset();
        f.training.reset();
    }

    void physicsUseRiderWeight()
    {
        Fixture f;
        QVERIFY(f.init());
        const road::Model m = f.ride->roadModel(3.0);
        QCOMPARE(m.massKg, f.profiles->weight() + 9.0);
        QCOMPARE(m.gradePercent, 3.0);
        QCOMPARE(m.cda, 0.32);
    }
};

QTEST_GUILESS_MAIN(TestFreeRide)
#include "tst_freeride.moc"
