#include "Database.h"
#include "ProfileManager.h"
#include "RowingRecords.h"
#include "RowingSession.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "TrainingSession.h"
#include "WorkoutPlanner.h"

#include <QSignalSpy>
#include <QtTest>
#include <memory>

using namespace watt;

// Drives the protected feed API of RowerDevice.
class FakeRower : public RowerDevice
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString name() const override { return QStringLiteral("Fake PM5"); }
    QString protocol() const override { return QStringLiteral("test"); }
    void connectDevice() override { setState(LinkState::Connected); }
    void disconnectDevice() override { setState(LinkState::Disconnected); }
    void requestTargetPower(int watts) override { lastTarget = watts; ++targetRequests; }

    void stroke(int watts) { applyStroke(++strokes, watts); applyStrokeRate(24); commitMeasurement(); }
    void progress(double t, double d) { applyElapsed(t); applyDistance(d); commitMeasurement(); }

    int strokes = 0;
    int lastTarget = -1;
    int targetRequests = 0;
};

struct Fixture
{
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<RowingRecords> records;
    std::unique_ptr<WorkoutPlanner> planner;
    std::unique_ptr<RowingSession> rowing;
    FakeRower rower;

    bool init()
    {
        if (!db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        records = std::make_unique<RowingRecords>(&db, profiles.get());
        planner = std::make_unique<WorkoutPlanner>(&db, profiles.get());
        rowing = std::make_unique<RowingSession>(&db, profiles.get(), hub.get(), records.get(), planner.get());
        rowing->setManualClock(true);
        rowing->setMinimumSaveSeconds(10);
        rower.connectDevice();
        rowing->setRower(&rower);
        return true;
    }
};

class TestRowingSession : public QObject
{
    Q_OBJECT
private slots:
    void idleIsSafe()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        RowingSession s(&db, &profiles, nullptr, nullptr, nullptr);
        QCOMPARE(s.state(), QStringLiteral("idle"));
        QCOMPARE(s.mode(), QStringLiteral("none"));
        QCOMPARE(s.rowerConnected(), false);
        QCOMPARE(s.watts(), 0);
        QCOMPARE(s.pace500(), 0.0);
        QCOMPARE(s.ghostGapMeters(), 0.0);
        QCOMPARE(s.standardDistances(), QList<int>({ 500, 1000, 2000, 5000, 6000, 10000 }));
        QVERIFY(!s.setupGhost(0));
        QVERIFY(!s.setupPlan(123456));
        QCOMPARE(s.finish(true), qint64(-1));
    }

    void traceTime()
    {
        const QVector<TracePoint> tr { { 0, 0 }, { 10, 50 }, { 20, 120 } };
        QCOMPARE(RowingSession::traceTimeAt(tr, 25), 5.0);
        QCOMPARE(RowingSession::traceTimeAt(tr, 85), 15.0);
        QCOMPARE(RowingSession::traceTimeAt(tr, 500), 20.0);
        QCOMPARE(RowingSession::traceTimeAt({}, 5), 0.0);
    }

    void soloDistanceCompletes()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy saved(f.rowing.get(), &RowingSession::rideSaved);
        QSignalSpy record(f.rowing.get(), &RowingSession::newRecordSet);
        // The monitor already shows an earlier piece: 1234 m.
        f.rower.progress(300, 1234);
        QVERIFY(f.rowing->setupSolo(500));
        QCOMPARE(f.rowing->state(), QStringLiteral("ready"));
        QCOMPARE(f.rowing->mode(), QStringLiteral("solo"));
        QCOMPARE(f.rowing->workoutName(), QStringLiteral("Row 500 m"));
        QCOMPARE(f.rowing->distanceMeters(), 0.0);

        f.hub->setStaleTimeoutMs(600000);
        f.rower.stroke(200);   // first stroke starts the session
        QCOMPARE(f.rowing->state(), QStringLiteral("running"));
        QCOMPARE(f.rowing->strokeCount(), 1);
        QCOMPARE(f.rowing->avgSpm(), 0);
        QCOMPARE(f.rowing->avgHeartRate(), 0);
        int k = 0;
        while (f.rowing->running() && k < 400) {
            ++k;
            if (k > 60)   // chest strap from 1:00 (seconds without HR do not count)
                f.hub->updateHeartRate(k > 90 ? 160 : 140);
            f.rowing->advanceClock(1000);
            if (k % 2 == 0)
                f.rower.stroke(200);
            f.rower.progress(300 + k, 1234 + 4.0 * k);
            if (k == 50) {
                QCOMPARE(f.rowing->distanceMeters(), 200.0);
                QCOMPARE(f.rowing->remainingMeters(), 300.0);
                QCOMPARE(f.rowing->progress(), 0.4);
                QCOMPARE(f.rowing->avgPace500(), 125.0);
                QCOMPARE(f.rowing->watts(), 200);
                QCOMPARE(f.rowing->spm(), 24);
                // chart series: 1 s buckets, steady 4 m/s -> 2:05 /500 m
                const QVariantList pace = f.rowing->paceSeries();
                const QVariantList dist = f.rowing->distanceSeries();
                QCOMPARE(f.rowing->seriesStep(), 1);
                QCOMPARE(pace.size(), f.rowing->powerSeries().size());
                QCOMPARE(dist.size(), pace.size());
                QCOMPARE(f.rowing->heartRateSeries().size(), pace.size());
                QCOMPARE(pace.last().toDouble(), 125.0);
                QVERIFY(dist.last().toDouble() > 180.0 && dist.last().toDouble() <= 200.0);
                QCOMPARE(f.rowing->heartRateSeries().last().toInt(), 0);
                QCOMPARE(f.rowing->avgSpm(), 24);
                QCOMPARE(f.rowing->avgHeartRate(), 0);
                QCOMPARE(f.rowing->maxWatts(), 200);
            }
        }
        QCOMPARE(k, 125);
        QCOMPARE(f.rowing->state(), QStringLiteral("finished"));
        QVERIFY(f.rowing->completed());
        QCOMPARE(f.rowing->finishTimeSec(), 125.0);
        QCOMPARE(f.rowing->distanceMeters(), 500.0);
        QCOMPARE(f.rowing->progress(), 1.0);
        QCOMPARE(f.rowing->avgWatts(), 200);
        QCOMPARE(f.rowing->maxWatts(), 200);
        QCOMPARE(f.rowing->avgSpm(), 24);
        // HR from iteration 61: 30 s at 140 + 35 s at 160
        QVERIFY(f.rowing->avgHeartRate() > 140 && f.rowing->avgHeartRate() < 160);
        QCOMPARE(f.rowing->avgHeartRate(), int(std::lround((30 * 140 + 35 * 160) / 65.0)));
        QCOMPARE(saved.count(), 1);
        QVERIFY(f.rowing->lastRideId() >= 0);

        const auto ride = f.db.ride(f.rowing->lastRideId());
        QVERIFY(ride.has_value());
        QCOMPARE(ride->sport, QStringLiteral("rowing"));
        QCOMPARE(ride->distanceM, 500.0);
        QCOMPARE(ride->ftp, f.profiles->rowingFtp());
        QCOMPARE(ride->durationSec, 125);
        QCOMPARE(ride->workoutId, qint64(-1));
        QCOMPARE(ride->workoutName, QStringLiteral("Row 500 m"));
        QCOMPARE(int(ride->avgCadence), 24);

        // first piece over 500 m -> new record with a 1 Hz trace ending at the finish
        QVERIFY(f.rowing->newRecord());
        QCOMPARE(f.rowing->previousRecordSec(), 0.0);
        QCOMPARE(record.count(), 1);
        const auto best = f.records->best(500);
        QVERIFY(best.has_value());
        QCOMPARE(best->timeSec, 125.0);
        QCOMPARE(best->rideId, f.rowing->lastRideId());
        QVERIFY(best->trace.size() >= 120);
        QCOMPARE(best->trace.first().d, 0.0);
        QCOMPARE(best->trace.last().d, 500.0);
        QCOMPARE(RowingRecords::ghostDistanceAt(best->trace, 50), 200.0);

        // rowing on after the finish changes nothing
        f.rower.progress(500, 1900);
        QCOMPARE(f.rowing->distanceMeters(), 500.0);

        // start again: a fresh piece from the current monitor position
        QVERIFY(f.rowing->start());
        QCOMPARE(f.rowing->distanceMeters(), 0.0);
        QCOMPARE(f.rowing->avgSpm(), 0);
        QCOMPARE(f.rowing->avgHeartRate(), 0);
        QCOMPARE(f.rowing->maxWatts(), 0);
        QVERIFY(!f.rowing->newRecord());
        f.rowing->reset();
        QCOMPARE(f.rowing->state(), QStringLiteral("ready"));
        QCOMPARE(f.rowing->targetDistanceM(), 500);
    }

    void offsetAndWorkoutReset()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rowing->setAutoStart(false);
        f.rower.progress(100, 1000);
        QVERIFY(f.rowing->setupSolo(0));   // open-ended
        f.rower.stroke(180);
        QCOMPARE(f.rowing->state(), QStringLiteral("ready"));   // no auto start
        QVERIFY(f.rowing->start());
        f.rowing->advanceClock(20000);
        f.rower.progress(120, 1100);
        QCOMPARE(f.rowing->distanceMeters(), 100.0);

        // A new workout on the PM5: elapsed and distance restart.
        QSignalSpy reset(&f.rower, &RowerDevice::workoutReset);
        f.rower.progress(1, 5);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(f.rowing->distanceMeters(), 100.0);
        f.rowing->advanceClock(5000);
        f.rower.progress(6, 25);
        QCOMPARE(f.rowing->distanceMeters(), 120.0);

        // Metres rowed while paused do not count.
        f.rowing->pause();
        QCOMPARE(f.rowing->state(), QStringLiteral("paused"));
        f.rower.progress(10, 60);
        QCOMPARE(f.rowing->distanceMeters(), 120.0);
        f.rowing->resume();
        f.rowing->advanceClock(2000);
        f.rower.progress(12, 70);
        QCOMPARE(f.rowing->distanceMeters(), 130.0);
        QCOMPARE(f.rowing->elapsedSeconds(), 27.0);

        // A different rower mid-session continues the distance too.
        FakeRower other;
        other.connectDevice();
        other.progress(50, 3000);
        f.rowing->setRower(&other);
        other.progress(52, 3010);
        QCOMPARE(f.rowing->distanceMeters(), 140.0);
        f.rowing->setRower(&f.rower);

        QCOMPARE(f.rowing->finish(false), qint64(-1));
        QCOMPARE(f.rowing->state(), QStringLiteral("finished"));
        QVERIFY(!f.rowing->completed());
        QVERIFY(!f.rowing->newRecord());
        QVERIFY(f.records->allBests().size() == RowingRecords::standardDistances().size());
        QVERIFY(!f.records->best(140).has_value());
    }

    void planIntervals()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.profiles->setRowingFtp(200));

        Workout w;
        w.name = QStringLiteral("Row Intervals");
        w.sport = sport::rowing();
        Interval a; a.name = QStringLiteral("Warm"); a.durationSec = 60; a.startPercent = a.endPercent = 0.5;
        Interval b; b.name = QStringLiteral("Work"); b.durationSec = 120; b.startPercent = b.endPercent = 1.0;
        Interval c; c.name = QStringLiteral("Cool"); c.durationSec = 60; c.startPercent = c.endPercent = 0.6;
        w.intervals = { a, b, c };
        const qint64 id = f.db.saveWorkout(w);
        QVERIFY(id >= 0);
        Workout bike = w;
        bike.id = -1;
        bike.sport = sport::cycling();
        const qint64 bikeId = f.db.saveWorkout(bike);
        QVERIFY(!f.rowing->setupPlan(bikeId));   // only rowing workouts
        {
            const QVariantList list = f.rowing->rowingWorkouts();
            QCOMPARE(list.size(), 1);   // built-in cycling workouts and bikeId excluded
            const QVariantMap m = list.first().toMap();
            QCOMPARE(m.value(QStringLiteral("workoutId")).toLongLong(), id);
            QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Row Intervals"));
            QCOMPARE(m.value(QStringLiteral("totalSec")).toInt(), 240);
        }

        const qint64 entry = f.planner->addWorkout(QDate::currentDate(), id);
        QVERIFY(entry >= 0);
        QVERIFY(f.planner->isRowing(entry));
        QVERIFY(f.planner->isRowing(f.planner->entry(entry)));
        QCOMPARE(f.planner->sportFor(QVariantMap { { QStringLiteral("workoutId"), bikeId } }), QStringLiteral("cycling"));

        QVERIFY(f.rowing->setupPlan(id));
        QCOMPARE(f.rowing->mode(), QStringLiteral("plan"));
        QCOMPARE(f.rowing->workoutId(), id);
        QCOMPARE(f.rowing->workoutName(), QStringLiteral("Row Intervals"));
        QCOMPARE(f.rowing->intervalCount(), 3);
        QCOMPARE(f.rowing->totalDurationSec(), 240);
        const QVariantList iv = f.rowing->intervals();
        QCOMPARE(iv.size(), 3);
        QCOMPARE(iv.at(1).toMap().value(QStringLiteral("startSec")).toInt(), 60);
        QCOMPARE(iv.at(1).toMap().value(QStringLiteral("durationSec")).toInt(), 120);
        QCOMPARE(iv.at(1).toMap().value(QStringLiteral("watts")).toInt(), 200);
        QCOMPARE(iv.at(1).toMap().value(QStringLiteral("percent")).toInt(), 100);
        QCOMPARE(iv.at(0).toMap().value(QStringLiteral("watts")).toInt(), 100);

        // rowing FTP changes before the start update the targets
        QVERIFY(f.profiles->setRowingFtp(220));
        QCOMPARE(f.rowing->intervals().at(1).toMap().value(QStringLiteral("watts")).toInt(), 220);
        QVERIFY(f.profiles->setRowingFtp(200));

        f.rower.progress(10, 0);
        QVERIFY(f.rowing->start());
        QCOMPARE(f.rowing->targetWatts(), 100);
        QCOMPARE(f.rowing->nextTargetWatts(), 200);
        QCOMPARE(f.rower.lastTarget, 100);   // the simulated athlete follows the plan
        int dist = 0;
        auto row = [&](int seconds, int watts) {
            for (int i = 0; i < seconds && f.rowing->running(); ++i) {
                f.rower.stroke(watts);
                f.rowing->advanceClock(1000);
                dist += 4;
                f.rower.progress(10 + dist / 4, dist);
            }
        };
        row(30, 100);
        QCOMPARE(f.rowing->currentIntervalIndex(), 0);
        QCOMPARE(f.rowing->intervalRemainingSec(), 30);
        QCOMPARE(f.rowing->remainingSec(), 210);
        row(31, 100);
        QCOMPARE(f.rowing->currentIntervalIndex(), 1);
        QCOMPARE(f.rowing->currentIntervalName(), QStringLiteral("Work"));
        QCOMPARE(f.rowing->targetWatts(), 200);
        QCOMPARE(f.rowing->nextTargetWatts(), 120);
        QCOMPARE(f.rower.lastTarget, 200);
        row(119, 150);   // well below target: not compliant
        QCOMPARE(f.rowing->currentIntervalIndex(), 2);
        QCOMPARE(f.rowing->targetWatts(), 120);
        QVERIFY(f.rowing->compliance() > 30 && f.rowing->compliance() < 40);   // 61 of 180 s
        row(100, 120);
        QCOMPARE(f.rowing->state(), QStringLiteral("finished"));
        QVERIFY(f.rowing->completed());
        QCOMPARE(f.rowing->finishTimeSec(), 240.0);
        QCOMPARE(f.rowing->targetWatts(), 0);
        QCOMPARE(f.rower.lastTarget, 0);   // released after the workout
        QVERIFY(!f.rowing->newRecord());

        const auto ride = f.db.ride(f.rowing->lastRideId());
        QVERIFY(ride.has_value());
        QCOMPARE(ride->sport, QStringLiteral("rowing"));
        QCOMPARE(ride->workoutId, id);
        QCOMPARE(ride->ftp, 200);
        QCOMPARE(ride->blocks, 3);
        QCOMPARE(ride->durationSec, 240);
        QCOMPARE(ride->distanceM, 956.0);   // the update after the final second arrives too late
        const QVector<Sample> samples = f.db.rideSamples(ride->id);
        QCOMPARE(samples.size(), 240);
        QCOMPARE(samples.at(90).targetPower, 200);
        QCOMPARE(samples.at(90).power, 150);

        QVERIFY(f.planner->entry(entry).value(QStringLiteral("completed")).toBool());
        QCOMPARE(f.planner->entry(entry).value(QStringLiteral("rideId")).toLongLong(), ride->id);
    }

    void ghostRace()
    {
        Fixture f;
        QVERIFY(f.init());
        QVector<TracePoint> slow;
        for (int t = 0; t <= 200; ++t)
            slow.append({ double(t), 5.0 * t });
        QVERIFY(f.records->saveIfBest(1000, 200.0, slow));

        QVERIFY(f.rowing->setupGhost(1000));
        QCOMPARE(f.rowing->mode(), QStringLiteral("ghost"));
        QVERIFY(f.rowing->hasGhost());
        QCOMPARE(f.rowing->recordTimeSec(), 200.0);
        QCOMPARE(f.rowing->recordPace500(), 100.0);
        QCOMPARE(f.rowing->ghostGapMeters(), 0.0);

        f.rower.progress(0, 0);
        QVERIFY(f.rowing->start());
        int k = 0;
        while (f.rowing->running() && k < 400) {
            ++k;
            f.rowing->advanceClock(1000);
            f.rower.stroke(250);
            f.rower.progress(k, 6.0 * k);
            if (k == 50) {
                QCOMPARE(f.rowing->distanceMeters(), 300.0);
                QCOMPARE(f.rowing->ghostDistanceM(), 250.0);
                QCOMPARE(f.rowing->ghostGapMeters(), 50.0);
                QCOMPARE(f.rowing->ghostGapSeconds(), 10.0);
            }
        }
        QCOMPARE(k, 167);
        QVERIFY(qAbs(f.rowing->finishTimeSec() - (166 + 4.0 / 6.0)) < 1e-6);
        QVERIFY(f.rowing->completed());
        QVERIFY(f.rowing->newRecord());
        QCOMPARE(f.rowing->previousRecordSec(), 200.0);
        QVERIFY(qAbs(f.rowing->ghostGapSeconds() - (200.0 - f.rowing->finishTimeSec())) < 1e-6);
        QVERIFY(qAbs(f.records->best(1000)->timeSec - f.rowing->finishTimeSec()) < 0.06);

        const auto ride = f.db.ride(f.rowing->lastRideId());
        QVERIFY(ride.has_value());
        QCOMPARE(ride->sport, QStringLiteral("rowing"));
        QCOMPARE(ride->distanceM, 1000.0);
        QCOMPARE(ride->workoutName, QStringLiteral("Ghost Race 1000 m"));

        // A slower second race is no record.
        QVERIFY(f.rowing->setupGhost(1000));
        QVERIFY(qAbs(f.rowing->recordTimeSec() - 166.7) < 0.06);
        f.rowing->start();
        k = 0;
        while (f.rowing->running() && k < 400) {
            ++k;
            f.rowing->advanceClock(1000);
            f.rower.progress(1000 + k, 5.0 * k);
        }
        QVERIFY(f.rowing->completed());
        QVERIFY(!f.rowing->newRecord());
        QVERIFY(f.rowing->ghostGapSeconds() < 0);
    }

    void exclusiveWithCycling()
    {
        Fixture f;
        QVERIFY(f.init());
        TrainingSession training(&f.db, f.profiles.get(), f.hub.get());
        training.setManualClock(true);
        f.rowing->setTrainingSession(&training);
        training.loadFreeRide();

        // Rower data never starts the armed cycling session ...
        training.startWhenPedaling();
        QVERIFY(training.waiting());
        f.rower.progress(1, 10);
        f.rower.stroke(300);
        QVERIFY(training.waiting());
        QVERIFY(!training.active());
        // ... and starting to row disarms it.
        QVERIFY(f.rowing->setupSolo(2000));
        QVERIFY(f.rowing->start());
        QVERIFY(!training.waiting());

        QSignalSpy refused(&training, &TrainingSession::startRefused);
        training.start();
        QVERIFY(!training.active());
        QCOMPARE(refused.count(), 1);
        f.rowing->finish(false);

        training.start();
        QVERIFY(training.active());
        QSignalSpy rowRefused(f.rowing.get(), &RowingSession::startRefused);
        QVERIFY(f.rowing->setupSolo(500));
        QVERIFY(!f.rowing->start());
        f.rower.stroke(300);   // no auto start either
        QCOMPARE(f.rowing->state(), QStringLiteral("ready"));
        QVERIFY(rowRefused.count() >= 1);
        training.discard();
    }
};

QTEST_GUILESS_MAIN(TestRowingSession)
#include "tst_rowingsession.moc"
