#include "Database.h"
#include "HistoryManager.h"
#include "ProfileManager.h"
#include "SensorHub.h"
#include "TrainingSession.h"
#include "WorkoutLibrary.h"

#include <QSignalSpy>
#include <QtTest>

using namespace watt;

class TestSession : public QObject
{
    Q_OBJECT

    Database *db = nullptr;
    ProfileManager *profiles = nullptr;
    SensorHub *hub = nullptr;
    TrainingSession *session = nullptr;

    static Workout shortWorkout()
    {
        Workout w;
        w.name = QStringLiteral("Short");
        Interval a; a.durationSec = 60; a.startPercent = a.endPercent = 0.5;
        Interval b; b.durationSec = 60; b.startPercent = b.endPercent = 1.0;
        w.intervals = { a, b };
        return w;
    }

    Workout builtInRampTest() const
    {
        for (const Workout &w : db->workouts()) {
            if (w.isProtected())
                return w;
        }
        return {};
    }

    // 7 min FTP test (personal copy style: not built-in, category "FTP Tests")
    static Workout shortFtpTest()
    {
        Workout w;
        w.name = QStringLiteral("My Ramp");
        w.category = Workout::ftpTestCategory();
        Interval a; a.durationSec = 300; a.startPercent = a.endPercent = 0.5;
        Interval b; b.durationSec = 120; b.startPercent = 1.0; b.endPercent = 1.4; b.type = BlockType::Ramp;
        w.intervals = { a, b };
        return w;
    }

    // Simulates 1 s of riding at the given power.
    void ride(int seconds, int watts)
    {
        for (int i = 0; i < seconds; ++i) {
            hub->updatePower(watts);
            hub->updateCadence(90);
            hub->updateHeartRate(140);
            session->advanceClock(1000);
        }
    }

private slots:
    void init()
    {
        db = new Database;
        QVERIFY(db->open(QStringLiteral(":memory:")));
        db->seedDefaults();
        profiles = new ProfileManager(db);
        profiles->load();
        hub = new SensorHub;
        hub->setStaleTimeoutMs(60000);
        session = new TrainingSession(db, profiles, hub);
        session->setManualClock(true);
    }

    void cleanup()
    {
        delete session;
        delete hub;
        delete profiles;
        delete db;
    }

    void rowingWorkoutsAreRefused()
    {
        QVERIFY(session->loadWorkout(shortWorkout()));
        const QString cyclingName = session->workoutName();
        Workout row = shortWorkout();
        row.name = QStringLiteral("AAA Row Intervals");
        row.sport = sport::rowing();
        const qint64 rowId = db->saveWorkout(row);
        QVERIFY(rowId > 0);
        QSignalSpy refused(session, &TrainingSession::rowingWorkoutRefused);
        QVERIFY(!session->loadWorkout(rowId));
        QVERIFY(!session->loadWorkout(row));
        QCOMPARE(refused.count(), 2);
        QCOMPARE(session->workoutName(), cyclingName);   // unchanged

        // Default workout for the cycling dashboard skips rowing workouts,
        // even when a rowing workout is selected / sorts first.
        WorkoutLibrary lib(db, profiles);
        lib.reload();
        lib.setSortMode(WorkoutLibrary::NameAZ);
        lib.setSelectedId(rowId);
        const qint64 cyclingId = lib.firstWorkoutId(sport::cycling());
        QVERIFY(cyclingId >= 0);
        QCOMPARE(lib.workout(cyclingId)->sport, sport::cycling());
        QCOMPARE(lib.firstWorkoutId(sport::rowing()), rowId);
        QVERIFY(session->loadWorkout(cyclingId));
    }

    void ergSendsTargets()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->start();
        QVERIFY(session->running());
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.at(0).at(0).toInt(), 125);   // 50% of 250
        ride(61, 125);
        QCOMPARE(session->currentIndex(), 1);
        QCOMPARE(targets.last().at(0).toInt(), 250);
        QCOMPARE(hub->requestedTarget(), 250);
    }

    void freeRideSendsNoTargets()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        QSignalSpy free(hub, &SensorHub::freeRideRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(false);
        session->start();
        ride(70, 150);
        QCOMPARE(targets.size(), 0);
        QVERIFY(free.size() >= 1);
        QCOMPARE(session->targetPower(), 250);   // targets are still shown

        // switching ERG on mid-ride pushes the current target immediately
        session->setErgMode(true);
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.last().at(0).toInt(), 250);
    }

    void recordsAndSavesRide()
    {
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        session->loadWorkout(shortWorkout());
        session->start();
        ride(130, 200);   // workout is 120 s → auto finish
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(saved.size(), 1);
        const qint64 id = saved.at(0).at(0).toLongLong();
        auto r = db->ride(id);
        QVERIFY(r);
        QCOMPARE(r->durationSec, 120);
        QCOMPARE(int(r->avgPower), 200);
        QCOMPARE(r->maxHeartRate, 140);
        QCOMPARE(db->rideSamples(id).size(), 120);
        QCOMPARE(db->rideSamples(id).at(10).targetPower, 125);
        QCOMPARE(session->powerSeries().size(), 120);
    }

    void pauseDoesNotRecord()
    {
        session->loadFreeRide();
        session->start();
        ride(10, 100);
        session->pause();
        ride(10, 100);
        QCOMPARE(session->elapsedSec(), 10);
        session->resume();
        ride(5, 100);
        QCOMPARE(session->samples().size(), 15);
    }

    void shortRideIsNotSaved()
    {
        session->setMinimumSaveSeconds(30);
        session->loadFreeRide();
        session->start();
        ride(5, 100);
        QCOMPARE(session->stop(), qint64(-1));
        QCOMPARE(db->rides(profiles->profileId()).size(), 0);
    }

    void skipLastIntervalSavesRide()
    {
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        QSignalSpy free(hub, &SensorHub::freeRideRequested);
        session->loadWorkout(shortWorkout());
        session->start();
        ride(70, 200);                 // inside the last interval
        session->skipInterval();       // jumps to the end
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(saved.size(), 1);
        QVERIFY(free.size() >= 1);     // ERG released
        QCOMPARE(session->stop(), qint64(-1));
        QCOMPARE(db->rides(profiles->profileId()).size(), 1);
    }

    void skipDoesNotInventSamples()
    {
        Workout w = shortWorkout();
        Interval c; c.durationSec = 600; c.startPercent = c.endPercent = 0.5;
        w.intervals.append(c);
        session->loadWorkout(w);
        session->start();
        ride(10, 150);
        session->skipInterval();       // skips 50 s of the first block
        QCOMPARE(session->elapsedSec(), 60);
        QCOMPARE(session->samples().size(), 10);
        ride(5, 150);
        QCOMPARE(session->samples().size(), 15);
    }

    void pauseReleasesErg()
    {
        QSignalSpy free(hub, &SensorHub::freeRideRequested);
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->start();
        ride(5, 125);
        session->pause();
        QCOMPARE(free.size(), 1);
        QCOMPARE(hub->requestedTarget(), 0);
        const int before = targets.size();
        session->resume();
        QCOMPARE(targets.size(), before + 1);
        QCOMPARE(targets.last().at(0).toInt(), 125);
    }

    void profileSnapshotAndIntensityTarget()
    {
        session->loadWorkout(shortWorkout());
        session->start();
        session->adjustIntensity(10);  // 110 %
        ride(40, 140);
        const qint64 original = profiles->profileId();
        profiles->createProfile(QStringLiteral("Other"));   // switches profile mid-ride
        ride(10, 140);
        const qint64 id = session->stop();
        QVERIFY(id > 0);
        auto r = db->ride(id);
        QCOMPARE(r->profileId, original);
        QCOMPARE(r->ftp, 250);
        QCOMPARE(db->rideSamples(id).at(20).targetPower, 138);   // 125 W * 1.1
    }

    void discardWhenInactiveIsNoop()
    {
        QSignalSpy state(session, &TrainingSession::stateChanged);
        session->discard();
        QCOMPARE(state.size(), 0);
    }

    void seriesAreDownsampledForLongRides()
    {
        session->loadFreeRide();
        session->start();
        ride(2000, 150);
        QVERIFY(session->powerSeries().size() <= TrainingSession::kMaxSeriesPoints);
        QCOMPARE(session->seriesStep(), 3);
        QCOMPARE(session->powerSeries().first().toInt(), 150);
        QCOMPARE(session->samples().size(), 2000);
    }

    void stopSavesFreeRide()
    {
        session->loadFreeRide();
        session->start();
        ride(45, 180);
        const qint64 id = session->stop();
        QVERIFY(id > 0);
        QCOMPARE(db->ride(id)->workoutName, QStringLiteral("Free Ride"));
        QCOMPARE(session->stop(), qint64(-1));   // second stop is a no-op
    }

    // --- start when pedaling ---------------------------------------------

    void armedSessionWaitsForPedaling()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->startWhenPedaling();
        QVERIFY(session->waiting());
        QVERIFY(session->armedOrActive());
        QVERIFY(!session->active());
        QVERIFY(!session->running());
        QCOMPARE(session->workoutName(), QStringLiteral("Short"));
        // rider not pedaling: clock frozen, nothing recorded, no ERG target
        for (int i = 0; i < 10; ++i) {
            hub->updateCadence(0);
            hub->updatePower(0);
            session->advanceClock(1000);
        }
        hub->updateCadence(TrainingSession::kStartCadenceRpm - 1);
        hub->updatePower(TrainingSession::kStartPowerWatts - 1);
        session->advanceClock(1000);
        QVERIFY(session->waiting());
        QCOMPARE(session->elapsedSec(), 0);
        QCOMPARE(session->samples().size(), 0);
        QCOMPARE(targets.size(), 0);
        QCOMPARE(hub->requestedTarget(), 0);
    }

    void cadenceStartsArmedSession()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->startWhenPedaling();
        QVERIFY(session->waiting());
        hub->updateCadence(TrainingSession::kStartCadenceRpm);
        QVERIFY(!session->waiting());
        QVERIFY(session->running());
        QVERIFY(session->active());
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.at(0).at(0).toInt(), 125);
        ride(10, 125);
        QCOMPARE(session->elapsedSec(), 10);
        QCOMPARE(session->samples().size(), 10);
    }

    void powerOnlyStartsArmedSession()
    {
        session->loadWorkout(shortWorkout());
        session->startWhenPedaling();
        QVERIFY(session->waiting());
        hub->updatePower(TrainingSession::kStartPowerWatts);   // no cadence sensor
        QVERIFY(session->running());
        QVERIFY(!session->waiting());
    }

    void alreadyPedalingStartsImmediately()
    {
        hub->updateCadence(85);
        hub->updatePower(150);
        session->loadFreeRide();
        session->startWhenPedaling();
        QVERIFY(!session->waiting());
        QVERIFY(session->running());
    }

    void stopOrDiscardWhileWaitingSavesNothing()
    {
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        session->setMinimumSaveSeconds(0);
        session->loadWorkout(shortWorkout());
        session->startWhenPedaling();
        session->advanceClock(5000);
        QCOMPARE(session->stop(), qint64(-1));
        QVERIFY(!session->waiting());
        QVERIFY(!session->armedOrActive());
        QCOMPARE(session->state(), int(TrainingSession::Idle));

        session->startWhenPedaling();
        QVERIFY(session->waiting());
        session->discard();
        QVERIFY(!session->waiting());
        QCOMPARE(session->state(), int(TrainingSession::Idle));

        // pedaling after cancelling must not start anything
        hub->updateCadence(90);
        QVERIFY(!session->running());
        QCOMPARE(saved.size(), 0);
        QCOMPARE(db->rides(profiles->profileId()).size(), 0);
    }

    void manualStartOverridesWaiting()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->startWhenPedaling();
        session->start();
        QVERIFY(!session->waiting());
        QVERIFY(session->running());
        QCOMPARE(targets.size(), 1);

        session->discard();
        session->loadWorkout(shortWorkout());
        session->startWhenPedaling();
        session->togglePause();   // Space / "Start now"
        QVERIFY(!session->waiting());
        QVERIFY(session->running());
    }

    // --- FTP test ------------------------------------------------------------

    void rampTestStopEmitsFtpEstimate()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        QSignalSpy finished(session, &TrainingSession::finished);
        const Workout ramp = builtInRampTest();
        QVERIFY(ramp.id > 0);
        QVERIFY(session->loadWorkout(ramp.id));
        session->start();
        ride(500, 150);
        ride(60, 340);     // best minute
        ride(40, 150);
        QCOMPARE(ftp.size(), 0);
        const qint64 id = session->stop();
        QVERIFY(id > 0);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(ftp.size(), 1);
        QCOMPARE(ftp.at(0).at(0).toLongLong(), id);
        QCOMPARE(ftp.at(0).at(1).toInt(), 255);   // round(0.75 * 340)
        QCOMPARE(ftp.at(0).at(2).toInt(), 250);   // current FTP
        QCOMPARE(ftp.at(0).at(3).toInt(), 340);
        QCOMPARE(profiles->ftp(), 250);           // never changed automatically
    }

    void ftpTestAutoFinishAndSkipEmit()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        session->loadWorkout(shortFtpTest());
        session->start();
        ride(300, 140);
        ride(130, 320);    // workout ends after 420 s -> auto finish
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(ftp.size(), 1);
        QCOMPARE(ftp.at(0).at(1).toInt(), 240);   // 0.75 * 320

        session->loadWorkout(shortFtpTest());
        session->start();
        ride(300, 140);
        ride(60, 300);
        session->skipInterval();   // skip the last interval -> finish
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(ftp.size(), 2);
        QCOMPARE(ftp.at(1).at(1).toInt(), 225);   // 0.75 * 300
    }

    void noFtpEstimateForNormalOrShortRides()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        // normal workout, even with a hard minute
        Workout normal = shortFtpTest();
        normal.category = QStringLiteral("Intervals");
        session->loadWorkout(normal);
        session->start();
        ride(300, 140);
        ride(130, 340);
        QCOMPARE(saved.size(), 1);
        QCOMPARE(ftp.size(), 0);

        // FTP test stopped after 4 minutes: saved, but no estimate offered
        session->loadWorkout(builtInRampTest().id);
        session->start();
        ride(180, 150);
        ride(60, 340);
        QVERIFY(session->stop() > 0);
        QCOMPARE(saved.size(), 2);
        QCOMPARE(ftp.size(), 0);

        // implausible result (< 50 W)
        session->loadWorkout(builtInRampTest().id);
        session->start();
        ride(400, 40);
        QVERIFY(session->stop() > 0);
        QCOMPARE(ftp.size(), 0);

        // free ride
        session->loadFreeRide();
        session->start();
        ride(400, 340);
        QVERIFY(session->stop() > 0);
        QCOMPARE(ftp.size(), 0);
    }

    void noFtpPromptWhenProfileSwitched()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        session->loadWorkout(builtInRampTest().id);
        session->start();
        ride(400, 300);
        profiles->createProfile(QStringLiteral("Other"));
        QVERIFY(session->stop() > 0);
        QCOMPARE(ftp.size(), 0);
    }

    void historyExposesEstimatedFtp()
    {
        HistoryManager history(db, profiles);
        session->loadWorkout(builtInRampTest().id);
        session->start();
        ride(500, 150);
        ride(60, 340);
        ride(40, 150);
        const qint64 rampRide = session->stop();
        QVERIFY(rampRide > 0);

        history.refresh();
        history.selectRide(rampRide);
        QVariantMap r = history.selectedRide();
        QVERIFY(r.value(QStringLiteral("isFtpTest")).toBool());
        QCOMPARE(r.value(QStringLiteral("best1min")).toInt(), 340);
        QCOMPARE(r.value(QStringLiteral("estimatedFtp")).toInt(), 255);

        // normal ride
        session->loadFreeRide();
        session->start();
        ride(400, 300);
        const qint64 freeRide = session->stop();
        history.selectRide(freeRide);
        r = history.selectedRide();
        QVERIFY(!r.value(QStringLiteral("isFtpTest")).toBool());
        QCOMPARE(r.value(QStringLiteral("estimatedFtp")).toInt(), 0);
    }

    void historyFallsBackToWorkoutName()
    {
        HistoryManager history(db, profiles);
        Workout w = shortFtpTest();
        w.name = QStringLiteral("Old FTP Test");
        w.builtIn = false;
        w.id = db->saveWorkout(w);
        QVERIFY(session->loadWorkout(w.id));
        session->start();
        ride(300, 140);
        ride(130, 320);
        const qint64 id = session->lastRideId();
        QVERIFY(id > 0);
        QVERIFY(db->deleteWorkout(w.id));   // personal copy: deletable
        history.selectRide(id);
        const QVariantMap r = history.selectedRide();
        QVERIFY(r.value(QStringLiteral("isFtpTest")).toBool());
        QCOMPARE(r.value(QStringLiteral("estimatedFtp")).toInt(), 240);
    }

    void reloadWhileWaitingKeepsArmed()
    {
        session->loadWorkout(shortWorkout());
        session->startWhenPedaling();
        session->loadFreeRide();
        QVERIFY(session->waiting());
        QVERIFY(session->freeRide());
        hub->updateCadence(60);
        QVERIFY(session->running());
        QVERIFY(session->freeRide());
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
