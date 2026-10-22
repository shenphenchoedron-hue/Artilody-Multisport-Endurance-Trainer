#include "WorkoutEngine.h"

#include <QtTest>

using namespace watt;

static Workout simpleWorkout()
{
    Workout w;
    Interval a; a.name = "A"; a.durationSec = 60; a.startPercent = a.endPercent = 0.5;
    Interval b; b.name = "B"; b.durationSec = 120; b.startPercent = b.endPercent = 1.0; b.cadence = 90;
    Interval c; c.name = "C"; c.durationSec = 60; c.startPercent = 0.6; c.endPercent = 0.4;
    w.intervals = { a, b, c };
    return w;
}

class TestEngine : public QObject
{
    Q_OBJECT
private slots:
    void startsIdle()
    {
        WorkoutEngine e;
        e.load(simpleWorkout(), 200);
        QCOMPARE(e.state(), WorkoutEngine::State::Idle);
        QCOMPARE(e.totalSec(), 240);
        QVERIFY(!e.advance(1000));   // not running
        QCOMPARE(e.elapsedSec(), 0);
    }

    void followsIntervals()
    {
        WorkoutEngine e;
        e.load(simpleWorkout(), 200);
        e.start();
        QCOMPARE(e.currentIndex(), 0);
        QCOMPARE(e.targetWatts(), 100);
        QCOMPARE(e.nextTargetWatts(), 200);
        e.advance(59000);
        QCOMPARE(e.currentIndex(), 0);
        QCOMPARE(e.intervalRemainingSec(), 1);
        QVERIFY(e.advance(1000));     // interval change reported
        QCOMPARE(e.currentIndex(), 1);
        QCOMPARE(e.targetWatts(), 200);
        QCOMPARE(e.targetCadence(), 90);
        e.advance(150000);           // into the ramp-down: 30 s of 60
        QCOMPARE(e.currentIndex(), 2);
        QCOMPARE(e.targetWatts(), 100);   // 0.5 * 200
    }

    void pauseStopsClock()
    {
        WorkoutEngine e;
        e.load(simpleWorkout(), 200);
        e.start();
        e.advance(10000);
        e.pause();
        e.advance(10000);
        QCOMPARE(e.elapsedSec(), 10);
        e.resume();
        e.advance(5000);
        QCOMPARE(e.elapsedSec(), 15);
    }

    void finishesAtEnd()
    {
        WorkoutEngine e;
        e.load(simpleWorkout(), 200);
        e.start();
        e.advance(500000);
        QCOMPARE(e.state(), WorkoutEngine::State::Finished);
        QCOMPARE(e.elapsedSec(), 240);
        QCOMPARE(e.progress(), 1.0);
    }

    void skipAndIntensity()
    {
        WorkoutEngine e;
        e.load(simpleWorkout(), 200);
        e.start();
        e.skipInterval();
        QCOMPARE(e.currentIndex(), 1);
        QCOMPARE(e.elapsedSec(), 60);
        e.setIntensityOffset(10);
        QCOMPARE(e.targetWatts(), 220);
        e.setIntensityOffset(-90);           // clamped to -50
        QCOMPARE(e.intensityOffset(), -50);
    }

    void freeRideHasNoTargets()
    {
        WorkoutEngine e;
        e.loadFreeRide();
        e.start();
        e.advance(3600 * 1000);
        QVERIFY(e.isFreeRide());
        QCOMPARE(e.state(), WorkoutEngine::State::Running);   // never auto-finishes
        QCOMPARE(e.targetWatts(), 0);
        QCOMPARE(e.currentIndex(), -1);
        QCOMPARE(e.elapsedSec(), 3600);
    }
};

QTEST_GUILESS_MAIN(TestEngine)
#include "tst_engine.moc"
