// SessionGuard ("Sessions"): only a session genuinely in progress (riding /
// paused with a recording) blocks starting another one; empty paused
// sessions are given up by another start; prepared (loaded / set up, not
// started) and finished sessions never block and pedaling / rowing never
// starts anything (manual start only); the blocker
// maps the pages show (why Start is disabled) and endSession() (the fix).
// Also: the video library scan always ends (failure / time-out).
#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"
#include "RlvSession.h"
#include "RowingRecords.h"
#include "RowingSession.h"
#include "RunLibrary.h"
#include "RunSession.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "SessionGuard.h"
#include "TrainingSession.h"
#include "VirtualGears.h"
#include "WorkoutPlanner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace watt;

namespace {

class FakeRower : public RowerDevice
{
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString name() const override { return QStringLiteral("Fake PM5"); }
    QString protocol() const override { return QStringLiteral("test"); }
    void connectDevice() override { setState(LinkState::Connected); }
    void disconnectDevice() override { setState(LinkState::Disconnected); }
    void requestTargetPower(int) override {}
    void stroke(int watts)
    {
        applyWorkoutState(1, true);   // PM5: rowing active
        applyStroke(++strokes, watts);
        applyStrokeRate(24);
        commitMeasurement();
    }
    void progress(double t, double d) { applyElapsed(t); applyDistance(d); commitMeasurement(); }
    int strokes = 0;
};

class FakeTreadmill : public TreadmillDevice
{
public:
    QString id() const override { return QStringLiteral("tm"); }
    QString name() const override { return QStringLiteral("Fake Treadmill"); }
    QString protocol() const override { return QStringLiteral("test"); }
    void connectDevice() override { setState(LinkState::Connected); }
    void disconnectDevice() override { setState(LinkState::Disconnected); }
    void belt(double kmh) { applyMeasurement(kmh, 1.0, true, -1, 0); }
};

// flat straight route north, 1 point per second at 8 m/s, with a dummy video
bool writeRoute(const QString &dir, const QString &name, double lengthM)
{
    const double mPerDeg = RlvRoute::haversine(0, 0, 1, 0);
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><trkseg>\n";
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("1999-12-31T23:00:00Z"), Qt::ISODate);
    double d = 0;
    for (int i = 0; d <= lengthM + 1e-6; ++i, d += 8.0)
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.0\"><ele>300</ele><time>%2</time></trkpt>\n")
                   .arg(45.0 + d / mPerDeg, 0, 'f', 8)
                   .arg(t0.addSecs(i).toString(Qt::ISODate))
                   .toUtf8();
    gpx += "</trkseg></trk></gpx>\n";
    QFile g(dir + QLatin1Char('/') + name + QStringLiteral(".gpx"));
    QFile v(dir + QLatin1Char('/') + name + QStringLiteral(".mp4"));
    if (!g.open(QIODevice::WriteOnly) || !v.open(QIODevice::WriteOnly))
        return false;
    g.write(gpx);
    v.write("not a real video");
    return true;
}

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<TrainingSession> training;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<FreeRideSession> freeRide;
    std::unique_ptr<RlvLibrary> library;
    std::unique_ptr<RlvSession> rlv;
    std::unique_ptr<RowingRecords> records;
    std::unique_ptr<WorkoutPlanner> planner;
    std::unique_ptr<RowingSession> rowing;
    std::unique_ptr<SessionGuard> guard;
    FakeRower rower;

    bool init()
    {
        if (!dir.isValid() || !db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(600000);
        hub->setTrainerConnected(true);
        training = std::make_unique<TrainingSession>(&db, profiles.get(), hub.get());
        training->setManualClock(true);
        training->setMinimumSaveSeconds(5);
        gears = std::make_unique<VirtualGears>(&db, profiles.get());
        gears->setManualSave(true);
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        freeRide->setManualClock(true);
        freeRide->setMinimumSaveSeconds(5);
        freeRide->setTrainingSession(training.get());
        library = std::make_unique<RlvLibrary>(&db);
        library->setLibraryFolder(dir.path());
        if (!QDir(dir.path()).mkpath(QStringLiteral("flat")) || !writeRoute(dir.filePath(QStringLiteral("flat")),
                                                                            QStringLiteral("flat"), 3000))
            return false;
        library->rescanNow();
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), library.get());
        rlv->setManualClock(true);
        rlv->setMinimumSaveSeconds(5);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setTrainingSession(training.get());
        if (!rlv->selectPath(dir.filePath(QStringLiteral("flat"))))
            return false;
        records = std::make_unique<RowingRecords>(&db, profiles.get());
        planner = std::make_unique<WorkoutPlanner>(&db, profiles.get());
        rowing = std::make_unique<RowingSession>(&db, profiles.get(), hub.get(), records.get(), planner.get());
        rowing->setManualClock(true);
        rowing->setTrainingSession(training.get());
        rower.connectDevice();
        rowing->setRower(&rower);
        gears->setSessions(freeRide.get(), rlv.get());
        guard = std::make_unique<SessionGuard>(training.get(), freeRide.get(), rlv.get(), rowing.get(), gears.get());
        gears->setActivePage(QStringLiteral("dashboard"));
        return true;
    }
    void stopPedaling()
    {
        hub->updateCadence(0);
        hub->updatePower(0);
    }
    void pedal()
    {
        hub->updateCadence(90);
        hub->updatePower(180);
    }
    // the Watt Training page's Start + riding `ms`
    void rideWorkout(int ms)
    {
        pedal();
        training->start();
        for (int t = 0; t < ms; t += 250)
            training->advanceClock(250);
    }
    void rideFree(int ms)
    {
        pedal();
        freeRide->start();
        for (int t = 0; t < ms; t += 250)
            freeRide->advanceClock(250);
    }
    QString blockerKind(const QVariantMap &m) const { return m.value(QStringLiteral("kind")).toString(); }
};

} // namespace

class TestSessionGuard : public QObject
{
    Q_OBJECT
private slots:
    // The user's report: a workout tested (and paused) on Watt Training, then
    // the Video Ride page: Start disabled - with the reason and the fix.
    void pausedWorkoutBlocksVideoRideWithReason()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rideWorkout(8000);
        QVERIFY(f.training->running());
        f.training->pause();
        QVERIFY(f.training->paused());
        QVERIFY(f.training->inProgress());
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.training->paused());   // a paused ride is kept

        const QVariantMap b = f.guard->videoBlocker();
        QCOMPARE(f.blockerKind(b), QStringLiteral("workout"));
        QCOMPARE(b.value(QStringLiteral("page")).toString(), QStringLiteral("dashboard"));
        QCOMPARE(b.value(QStringLiteral("pageTitle")).toString(), QStringLiteral("Watt Training"));
        QVERIFY(b.value(QStringLiteral("paused")).toBool());
        QCOMPARE(b.value(QStringLiteral("text")).toString(), QStringLiteral("A workout is paused on Watt Training."));
        QCOMPARE(f.blockerKind(f.guard->freeRideBlocker()), QStringLiteral("workout"));
        QCOMPARE(f.blockerKind(f.guard->rowingBlocker()), QStringLiteral("workout"));
        QVERIFY(f.guard->workoutBlocker().isEmpty());   // its own page is never blocked by itself

        QSignalSpy refused(f.rlv.get(), &RlvSession::startRefused);
        QVERIFY(!f.rlv->start());
        QCOMPARE(refused.count(), 1);

        // the reason card's "End & Save"
        QSignalSpy changed(f.guard.get(), &SessionGuard::changed);
        QSignalSpy ended(f.guard.get(), &SessionGuard::sessionEnded);
        const qint64 id = f.guard->endSession(QStringLiteral("workout"), true);
        QVERIFY(id >= 0);   // 8 s >= the minimum of 5 s: saved
        QCOMPARE(ended.count(), 1);
        QVERIFY(changed.count() >= 1);
        QVERIFY(!f.training->active());
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.rlv->start());
    }

    // "nothing reacts": while a workout RUNS elsewhere it owns the trainer;
    // once paused (trainer released) the open Video Ride page holds it, and
    // the workout's resume takes it back
    void videoPageHoldsTrainerWhileWorkoutPaused()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rlv->setMode(QStringLiteral("erg"));
        f.rideWorkout(6000);
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.training->running());
        QVERIFY(!f.rlv->ergHolding());
        f.training->pause();
        QVERIFY(f.rlv->ergHolding());
        f.training->resume();
        QVERIFY(!f.rlv->ergHolding());
        f.training->pause();
        QVERIFY(f.rlv->ergHolding());
        // ended via the reason card: still held, Start enabled
        f.guard->endSession(QStringLiteral("workout"), false);
        QVERIFY(f.rlv->ergHolding());
        QVERIFY(f.guard->videoBlocker().isEmpty());
        // a free ride discarded from the video page: the hold comes back
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.rideFree(3000);
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(!f.rlv->ergHolding());
        f.guard->endSession(QStringLiteral("freeride"), false);
        QVERIFY(f.rlv->ergHolding());
    }

    void runningWorkoutBlocksAndDiscardFrees()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rideWorkout(3000);
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.training->running());   // riding on: never cancelled by a page change
        const QVariantMap b = f.guard->videoBlocker();
        QCOMPARE(b.value(QStringLiteral("text")).toString(), QStringLiteral("A workout is running on Watt Training."));
        QVERIFY(!b.value(QStringLiteral("paused")).toBool());
        QCOMPARE(f.guard->endSession(QStringLiteral("workout"), false), qint64(-1));   // discard
        QVERIFY(!f.training->active());
        QCOMPARE(f.training->lastRideId(), qint64(-1));
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.rlv->start());
        // nothing to end any more
        QCOMPARE(f.guard->endSession(QStringLiteral("workout"), true), qint64(-1));
    }

    // Manual start only: pedaling with a workout loaded on Watt Training
    // never starts it - on that page or any other - and a loaded workout
    // never blocks the other pages.
    void loadedWorkoutNeverStartsOrBlocks()
    {
        Fixture f;
        QVERIFY(f.init());
        f.stopPedaling();
        for (const char *page : { "workouts", "dashboard", "video", "freeride", "rowing" }) {
            f.gears->setActivePage(QString::fromLatin1(page));
            f.pedal();
            for (int t = 0; t < 3000; t += 250)
                f.training->advanceClock(250);
            QVERIFY(!f.training->active());
            QCOMPARE(f.training->elapsedSec(), 0);
        }
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.guard->freeRideBlocker().isEmpty());
        QVERIFY(f.guard->rowingBlocker().isEmpty());
        QVERIFY(f.rlv->start());
        QVERIFY(!f.training->active());
        // a running video ride blocks the workout
        QCOMPARE(f.blockerKind(f.guard->workoutBlocker()), QStringLiteral("video"));
        QSignalSpy refused(f.training.get(), &TrainingSession::startRefused);
        f.training->start();
        QCOMPARE(refused.count(), 1);
        QVERIFY(!f.training->active());
    }

    // Prepared sessions on every page (route chosen, free ride page open, row
    // set up) never start by pedaling / rowing and never block each other;
    // Start starts exactly the one pressed.
    void preparedSessionsNeverStartOrBlock()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.rowing->setupSolo(1000));
        QCOMPARE(f.rowing->state(), QStringLiteral("ready"));
        for (const char *page : { "dashboard", "freeride", "video", "rowing" }) {
            f.gears->setActivePage(QString::fromLatin1(page));
            f.pedal();
            f.rower.stroke(250);
            f.rower.progress(10, 50);
            for (int t = 0; t < 2000; t += 250) {
                f.training->advanceClock(250);
                f.freeRide->advanceClock(250);
                f.rlv->advanceClock(250);
                f.rowing->advanceClock(250);
            }
            QVERIFY(!f.training->active());
            QCOMPARE(f.freeRide->state(), QStringLiteral("idle"));
            QCOMPARE(f.rlv->state(), QStringLiteral("idle"));
            QCOMPARE(f.rowing->state(), QStringLiteral("ready"));
            QVERIFY(f.guard->workoutBlocker().isEmpty());
            QVERIFY(f.guard->freeRideBlocker().isEmpty());
            QVERIFY(f.guard->videoBlocker().isEmpty());
            QVERIFY(f.guard->rowingBlocker().isEmpty());
        }
        f.gears->setActivePage(QStringLiteral("rowing"));
        QVERIFY(f.rowing->start());
        QVERIFY(f.rowing->running());
        QVERIFY(!f.training->active());
        QCOMPARE(f.freeRide->state(), QStringLiteral("idle"));
        QCOMPARE(f.rlv->state(), QStringLiteral("idle"));
        f.rowing->finish(false);
    }

    // started and paused before anything was recorded: nothing to lose
    void emptyPausedWorkoutGivenUpByVideoStart()
    {
        Fixture f;
        QVERIFY(f.init());
        f.training->start();
        f.training->pause();   // manual clock: no time ridden
        QVERIFY(f.training->paused());
        QVERIFY(!f.training->hasRecording());
        QVERIFY(!f.training->inProgress());
        QVERIFY(f.training->abandonable());
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.rlv->start());
        QVERIFY(!f.training->active());
    }

    void finishedSessionsNeverBlock()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rideWorkout(6000);
        QVERIFY(f.training->stop() >= 0);
        QCOMPARE(f.training->state(), int(TrainingSession::Finished));
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.guard->freeRideBlocker().isEmpty());

        f.gears->setActivePage(QStringLiteral("freeride"));
        f.rideFree(6000);
        QVERIFY(f.freeRide->finish(false) < 0);
        QCOMPARE(f.freeRide->state(), QStringLiteral("finished"));
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.guard->workoutBlocker().isEmpty());
        QVERIFY(f.rlv->start());
        QVERIFY(f.rlv->finish(false) < 0);
        QVERIFY(f.guard->workoutBlocker().isEmpty());
        QVERIFY(f.guard->freeRideBlocker().isEmpty());
        f.training->start();
        QVERIFY(f.training->running());
    }

    void pausedFreeRideBlocksVideoRide()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.rideFree(4000);
        f.freeRide->pause();
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.freeRide->paused());
        const QVariantMap b = f.guard->videoBlocker();
        QCOMPARE(f.blockerKind(b), QStringLiteral("freeride"));
        QCOMPARE(b.value(QStringLiteral("page")).toString(), QStringLiteral("freeride"));
        QCOMPARE(b.value(QStringLiteral("text")).toString(),
                 QStringLiteral("A free ride is paused on the Free Ride page."));
        QCOMPARE(f.blockerKind(f.guard->workoutBlocker()), QStringLiteral("freeride"));
        QVERIFY(!f.rlv->start());
        f.guard->endSession(QStringLiteral("freeride"), false);   // the reason card's "Discard"
        QCOMPARE(f.freeRide->state(), QStringLiteral("finished"));
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.rlv->start());
    }

    void emptyPausedFreeRideGivenUpByVideoStart()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(f.freeRide->start());
        f.freeRide->pause();
        QVERIFY(!f.freeRide->inProgress());
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.guard->videoBlocker().isEmpty());
        QVERIFY(f.rlv->start());
        QVERIFY(!f.freeRide->active());
    }

    void rowInProgressBlocksVideoRide()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gears->setActivePage(QStringLiteral("rowing"));
        f.rowing->setupSolo(1000);
        QVERIFY(f.rowing->start());
        f.rowing->advanceClock(3000);
        QVERIFY(f.rowing->inProgress());
        f.gears->setActivePage(QStringLiteral("video"));
        QCOMPARE(f.blockerKind(f.guard->videoBlocker()), QStringLiteral("rowing"));
        QCOMPARE(f.guard->videoBlocker().value(QStringLiteral("text")).toString(),
                 QStringLiteral("A row is in progress on the Rowing page."));
        QVERIFY(!f.rlv->start());
        f.guard->endSession(QStringLiteral("rowing"), false);
        QVERIFY(!f.rowing->active());
        QVERIFY(f.rlv->start());
        // a video ride in progress blocks rowing and the free ride
        QCOMPARE(f.blockerKind(f.guard->rowingBlocker()), QStringLiteral("video"));
        QCOMPARE(f.blockerKind(f.guard->freeRideBlocker()), QStringLiteral("video"));
        QVERIFY(!f.rowing->start());
        QVERIFY(!f.freeRide->start());
    }

    // a Zwift Click press on the Video Ride page goes to the video ride, not
    // to a free ride paused on its own page
    // Treadmill Run: a run in progress blocks the other sessions and is
    // blocked by them; endSession("run") ends it from another page.
    void treadmillRunBlocksOtherSessions()
    {
        Fixture f;
        QVERIFY(f.init());
        RunLibrary lib(&f.db, f.profiles.get());
        RunSession run(&f.db, f.profiles.get(), f.hub.get(), &lib);
        run.setManualClock(true);
        run.setMinimumSaveSeconds(5);
        FakeTreadmill tm;
        tm.connectDevice();
        run.setTreadmill(&tm);
        f.guard->setRunSession(&run);
        QVERIFY(f.blockerKind(f.guard->runBlocker()).isEmpty());

        tm.belt(10.0);
        QVERIFY(run.start());
        run.advanceClock(10000);
        QCOMPARE(f.blockerKind(f.guard->videoBlocker()), QStringLiteral("run"));
        QCOMPARE(f.blockerKind(f.guard->rowingBlocker()), QStringLiteral("run"));
        QCOMPARE(f.guard->videoBlocker().value(QStringLiteral("page")).toString(), QStringLiteral("treadmill"));
        QVERIFY(!f.rlv->start());
        f.pedal();
        f.training->start();
        QVERIFY(!f.training->running());

        // ended (saved) from another page
        const qint64 id = f.guard->endSession(QStringLiteral("run"), true);
        QVERIFY(id > 0);
        QVERIFY(!run.active());
        QVERIFY(f.blockerKind(f.guard->videoBlocker()).isEmpty());

        // a free ride in progress blocks the run
        f.rideFree(6000);
        QCOMPARE(f.blockerKind(f.guard->runBlocker()), QStringLiteral("freeride"));
        QVERIFY(!run.start());
        f.freeRide->finish(false);
        QVERIFY(run.start());
        f.guard->setRunSession(nullptr);
    }

    void controllerFollowsOpenPageWhilePaused()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rlv->setMode(QStringLiteral("erg"));
        f.freeRide->setMode(QStringLiteral("erg"));
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.rideFree(3000);
        f.freeRide->pause();
        f.gears->setActivePage(QStringLiteral("video"));
        const int rlvBefore = f.rlv->targetWatts();
        const int frBefore = f.freeRide->targetWatts();
        f.gears->controllerShift(1);
        QCOMPARE(f.freeRide->targetWatts(), frBefore);
        QCOMPARE(f.rlv->targetWatts(), rlvBefore + RlvSession::kControllerErgStep);
    }

    void describeForDebugLog()
    {
        Fixture f;
        QVERIFY(f.init());
        f.rideWorkout(4000);
        f.training->pause();
        f.gears->setActivePage(QStringLiteral("video"));
        const QString d = f.guard->describe();
        QVERIFY2(d.contains(QLatin1String("workout=paused(4 s)")), qPrintable(d));
        QVERIFY2(d.contains(QLatin1String("page=video")), qPrintable(d));
    }

    // "Scanning…" never sticks: a scan that takes too long is given up
    void libraryScanAlwaysEnds()
    {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.library->count(), 1);
        QSignalSpy scanned(f.library.get(), &RlvLibrary::scanned);
        RlvLibrary::setScanDelayForTesting(1500);
        f.library->setScanTimeoutMs(150);
        f.library->rescan();
        QVERIFY(f.library->scanning());
        QVERIFY(f.library->waitForScan(1200));   // given up after 150 ms, not after 1.5 s
        RlvLibrary::setScanDelayForTesting(0);
        QVERIFY(!f.library->scanning());
        QCOMPARE(scanned.count(), 1);
        QVERIFY(f.library->lastError().contains(QLatin1String("took too long")));
        QCOMPARE(f.library->count(), 1);   // the list stays
        // the next scan works and clears the message
        f.library->setScanTimeoutMs(RlvLibrary::kDefaultScanTimeoutMs);
        f.library->rescan();
        QVERIFY(f.library->waitForScan(10000));
        QCOMPARE(scanned.count(), 2);
        QVERIFY(f.library->lastError().isEmpty());
        QCOMPARE(f.library->count(), 1);
        QTest::qWait(1600);   // let the given-up worker end before the library goes
    }
};

QTEST_GUILESS_MAIN(TestSessionGuard)
#include "tst_sessionguard.moc"
