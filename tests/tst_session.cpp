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

    // --- manual start only (pedaling never starts a workout) ---------------

    void pedalingNeverStartsWorkout()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        QSignalSpy states(session, &TrainingSession::stateChanged);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        states.clear();
        // clipping in / spinning up: hard pedaling for a while
        for (int i = 0; i < 20; ++i) {
            hub->updateCadence(95);
            hub->updatePower(250);
            session->advanceClock(1000);
        }
        QVERIFY(!session->active());
        QVERIFY(!session->running());
        QCOMPARE(session->state(), int(TrainingSession::Idle));
        QCOMPARE(session->elapsedSec(), 0);
        QCOMPARE(session->samples().size(), 0);
        QCOMPARE(targets.size(), 0);
        QCOMPARE(states.size(), 0);
        QCOMPARE(session->workoutName(), QStringLiteral("Short"));
        // the free ride (no workout) neither
        session->loadFreeRide();
        hub->updateCadence(100);
        hub->updatePower(300);
        session->advanceClock(5000);
        QVERIFY(!session->active());
        QCOMPARE(db->rides(profiles->profileId()).size(), 0);
    }

    void startStartsWorkout()
    {
        QSignalSpy targets(hub, &SensorHub::targetPowerRequested);
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->start();
        QVERIFY(session->running());
        QCOMPARE(targets.size(), 1);
        QCOMPARE(targets.at(0).at(0).toInt(), 125);
        ride(10, 125);
        QCOMPARE(session->elapsedSec(), 10);
        session->discard();

        // Space / the Start button: togglePause() from idle starts
        session->loadWorkout(shortWorkout());
        session->togglePause();
        QVERIFY(session->running());
    }

    // There is no auto-pause, and resuming is manual too: pedaling again
    // after a pause never resumes the clock.
    void pedalingNeverResumesPausedWorkout()
    {
        session->loadWorkout(shortWorkout());
        session->start();
        ride(5, 125);
        session->pause();
        QVERIFY(session->paused());
        const int before = session->elapsedSec();
        for (int i = 0; i < 10; ++i) {
            hub->updateCadence(90);
            hub->updatePower(200);
            session->advanceClock(1000);
        }
        QVERIFY(session->paused());
        QCOMPARE(session->elapsedSec(), before);
        // stopping to pedal while running does not pause either
        session->resume();
        QVERIFY(session->running());
        for (int i = 0; i < 5; ++i) {
            hub->updateCadence(0);
            hub->updatePower(0);
            session->advanceClock(1000);
        }
        QVERIFY(session->running());
        QCOMPARE(session->elapsedSec(), before + 5);
        session->discard();
    }

    void stopOrDiscardBeforeStartSavesNothing()
    {
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        session->setMinimumSaveSeconds(0);
        session->loadWorkout(shortWorkout());
        session->advanceClock(5000);
        QCOMPARE(session->stop(), qint64(-1));
        session->discard();
        QCOMPARE(session->state(), int(TrainingSession::Idle));
        QCOMPARE(saved.size(), 0);
        QCOMPARE(db->rides(profiles->profileId()).size(), 0);
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

        // skipping the last interval finishes and saves, but the test was
        // not done: no FTP result, not a completed workout
        QSignalSpy completed(session, &TrainingSession::workoutCompleted);
        QSignalSpy saved(session, &TrainingSession::rideSaved);
        session->loadWorkout(shortFtpTest());
        session->start();
        ride(300, 140);
        ride(60, 300);
        session->skipInterval();   // skip the last interval -> finish
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(ftp.size(), 1);
        QCOMPARE(completed.size(), 0);
        QVERIFY(!session->lastWorkoutCompleted());
    }

    // An earlier skip: the workout still completes at its end, but the FTP
    // result is not offered (a skipped warm-up / step distorts it).
    void ftpTestWithSkippedIntervalGivesNoResult()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        QSignalSpy completed(session, &TrainingSession::workoutCompleted);
        Workout w = shortFtpTest();
        w.id = db->saveWorkout(w);
        QVERIFY(session->loadWorkout(w.id));
        session->start();
        ride(30, 140);
        session->skipInterval();   // skip the warm-up
        ride(130, 320);
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(completed.size(), 1);
        QCOMPARE(ftp.size(), 0);
    }

    // Workout::ftpTestKind(): the category alone is not a ramp test.
    void ftpTestKindDetection()
    {
        Workout ramp = shortFtpTest();
        QCOMPARE(ramp.ftpTestKind(), Workout::FtpTestKind::Ramp);   // "Ramp" in the name
        ramp.name = QStringLiteral("Step test");
        QCOMPARE(ramp.ftpTestKind(), Workout::FtpTestKind::None);   // longest block is the 300 s steady one
        ramp.intervals[1].durationSec = 900;                         // now the ramp dominates
        QCOMPARE(ramp.ftpTestKind(), Workout::FtpTestKind::Ramp);
        ramp.category = QStringLiteral("Intervals");
        QCOMPARE(ramp.ftpTestKind(), Workout::FtpTestKind::None);   // not an FTP test at all

        Workout twenty;
        twenty.name = QStringLiteral("FTP Test");
        twenty.category = Workout::ftpTestCategory();
        Interval wu; wu.type = BlockType::WarmUp; wu.durationSec = 1200; wu.startPercent = 0.4; wu.endPercent = 0.7;
        Interval effort; effort.type = BlockType::Interval; effort.durationSec = 1200; effort.startPercent = effort.endPercent = 1.0;
        twenty.intervals = { wu };
        QCOMPARE(twenty.ftpTestKind(), Workout::FtpTestKind::None);   // a warm-up is no test effort
        twenty.intervals = { wu, effort };
        QCOMPARE(twenty.ftpTestKind(), Workout::FtpTestKind::TwentyMinute);
        QCOMPARE(builtInRampTest().ftpTestKind(), Workout::FtpTestKind::Ramp);
    }

    // A 20-minute test: FTP = 95 % of the best 20 minutes.
    void twentyMinuteTestUsesBest20Minutes()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        Workout w;
        w.name = QStringLiteral("FTP 20 min test");
        w.category = Workout::ftpTestCategory();
        Interval wu; wu.type = BlockType::WarmUp; wu.durationSec = 300; wu.startPercent = 0.5; wu.endPercent = 0.6;
        Interval effort; effort.type = BlockType::Interval; effort.durationSec = 1200; effort.startPercent = effort.endPercent = 1.0;
        Interval cd; cd.type = BlockType::CoolDown; cd.durationSec = 120; cd.startPercent = 0.5; cd.endPercent = 0.4;
        w.intervals = { wu, effort, cd };
        w.id = db->saveWorkout(w);
        QCOMPARE(w.ftpTestKind(), Workout::FtpTestKind::TwentyMinute);
        QVERIFY(session->loadWorkout(w.id));
        session->start();
        ride(300, 150);
        ride(60, 400);      // a hard minute: not what a 20-minute test measures
        ride(1140, 260);
        ride(120, 120);
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(ftp.size(), 1);
        // best 20 min = (60·400 + 1140·260) / 1200 = 267 W -> 0.95 · 267 = 254
        QCOMPARE(ftp.at(0).at(3).toInt(), 267);
        QCOMPARE(ftp.at(0).at(1).toInt(), 254);
        QCOMPARE(ftp.at(0).at(4).toString(), QStringLiteral("20min"));

        HistoryManager history(db, profiles);
        history.selectRide(session->lastRideId());
        const QVariantMap r = history.selectedRide();
        QVERIFY(r.value(QStringLiteral("isFtpTest")).toBool());
        QCOMPARE(r.value(QStringLiteral("ftpTestMethod")).toString(), QStringLiteral("20min"));
        QCOMPARE(r.value(QStringLiteral("estimatedFtp")).toInt(), 254);
        QCOMPARE(r.value(QStringLiteral("best20min")).toInt(), 267);
    }

    // An FTP test category without a recognisable protocol: no dialog.
    void unknownFtpTestProtocolGivesNoResult()
    {
        QSignalSpy ftp(session, &TrainingSession::ftpTestCompleted);
        Workout w;
        w.name = QStringLiteral("FTP check");
        w.category = Workout::ftpTestCategory();
        Interval a; a.durationSec = 600; a.startPercent = a.endPercent = 0.9;
        w.intervals = { a };
        QVERIFY(session->loadWorkout(w));
        session->start();
        ride(600, 300);
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(ftp.size(), 0);
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

    // The workout of an FTP test ride was deleted: the protocol is unknown,
    // so no FTP result (no guessing from "FTP" in the name).
    void historyNoFtpResultWithoutWorkout()
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
        QVERIFY(!r.value(QStringLiteral("isFtpTest")).toBool());
        QCOMPARE(r.value(QStringLiteral("estimatedFtp")).toInt(), 0);
    }

    // Only cycling rides give an FTP result in the history (the cycling FTP).
    void historyFtpOnlyForCycling()
    {
        HistoryManager history(db, profiles);
        session->loadWorkout(builtInRampTest().id);
        session->start();
        ride(500, 150);
        ride(60, 340);
        const qint64 id = session->stop();
        QVERIFY(id > 0);
        // the same ride stored as a row of that workout
        const auto rec = db->ride(id);
        QVERIFY(rec);
        RideRecord row = *rec;
        row.id = -1;
        row.sport = sport::rowing();
        const qint64 rowId = db->saveRide(row, db->rideSamples(id));
        QVERIFY(rowId > 0);
        history.selectRide(id);
        QVERIFY(history.selectedRide().value(QStringLiteral("isFtpTest")).toBool());
        history.selectRide(rowId);
        QVERIFY(!history.selectedRide().value(QStringLiteral("isFtpTest")).toBool());
    }

    // start() after a finished ride: a fresh workout (no intensity offset).
    void restartResetsIntensity()
    {
        session->loadWorkout(shortWorkout());
        session->setErgMode(true);
        session->start();
        session->adjustIntensity(10);
        QCOMPARE(session->intensityOffset(), 10);
        ride(40, 150);
        session->stop();
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        session->start();
        QCOMPARE(session->intensityOffset(), 0);
        QCOMPARE(session->elapsedSec(), 0);
        session->discard();
    }

    // A long timer gap (computer asleep) is a pause, not ridden time.
    void longClockGapIsNoRiddenTime()
    {
        session->loadWorkout(shortWorkout());
        session->start();
        ride(10, 150);
        const int before = session->elapsedSec();
        session->advanceWallClock(3600 * 1000);   // an hour of sleep
        QCOMPARE(session->elapsedSec(), before);
        session->advanceWallClock(1000);          // normal ticks still count
        QCOMPARE(session->elapsedSec(), before + 1);
        QVERIFY(!session->timerActive());          // manual clock
        session->discard();
    }

    // Plan entries are only ticked off for completed workouts.
    void workoutCompletedOnlyWhenFinished()
    {
        const qint64 id = db->saveWorkout(shortWorkout());
        QVERIFY(id >= 0);
        QSignalSpy completed(session, &TrainingSession::workoutCompleted);
        QSignalSpy saved(session, &TrainingSession::rideSaved);

        // stopped early: saved, but not completed
        QVERIFY(session->loadWorkout(id));
        session->start();
        ride(40, 150);
        QVERIFY(session->stop() >= 0);
        QCOMPARE(saved.size(), 1);
        QCOMPARE(completed.size(), 0);
        QVERIFY(!session->lastWorkoutCompleted());

        // ridden to the end: completed, emitted after rideSaved
        QVERIFY(session->loadWorkout(id));
        session->start();
        ride(121, 150);
        QCOMPARE(session->state(), int(TrainingSession::Finished));
        QCOMPARE(saved.size(), 2);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.at(0).at(0).toLongLong(), saved.at(1).at(0).toLongLong());
        QCOMPARE(completed.at(0).at(1).toLongLong(), id);
        QVERIFY(session->lastWorkoutCompleted());

        // discarded: nothing
        QVERIFY(session->loadWorkout(id));
        session->start();
        ride(40, 150);
        session->discard();
        QCOMPARE(completed.size(), 1);
        QVERIFY(!session->lastWorkoutCompleted());
    }

    void historySelectsNewRideOnlyWhenVisible()
    {
        HistoryManager history(db, profiles);
        history.setSportFilter(QStringLiteral("rowing"));
        session->loadWorkout(shortWorkout());
        session->start();
        ride(40, 150);
        const qint64 id = session->stop();
        QVERIFY(id >= 0);
        history.showNewRide(id);   // cycling ride, rowing filter: not shown, not selected
        QVERIFY(history.selectedRide().isEmpty());
        history.setSportFilter(QStringLiteral("all"));
        history.showNewRide(id);
        QCOMPARE(history.selectedRide().value(QStringLiteral("rideId")).toLongLong(), id);
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
