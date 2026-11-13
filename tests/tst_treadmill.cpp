// Treadmill Run: FTMS Treadmill Data parsing (read only), the treadmill
// device contract, the standard 10 km route, running workouts (% AnT / km/h),
// the AnT test evaluation, RunLibrary (AnT history, editor) and RunSession
// (distance from the belt, laps, GPX on/off, workouts, AnT test, saving),
// plus the 3D runner figure.
#include "Database.h"
#include "FtmsProtocol.h"
#include "ProfileManager.h"
#include "RunLibrary.h"
#include "RunRoutes.h"
#include "RunSession.h"
#include "RunWorkout.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"
#include "WorkoutBuilder.h"
#include "WorkoutIO.h"
#include "AvatarPreviewItem.h"
#include "Rowing3DItem.h"
#include "ghost_runner.h"
#include "glb_model.h"
#include "route.h"

#include <QFile>
#include <QPainter>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace watt;

namespace {

QByteArray bytes(std::initializer_list<int> v)
{
    QByteArray b;
    for (int x : v)
        b.append(char(quint8(x)));
    return b;
}

class FakeTreadmill : public TreadmillDevice
{
public:
    QString id() const override { return QStringLiteral("fake:tm"); }
    QString name() const override { return QStringLiteral("Fake Treadmill"); }
    QString protocol() const override { return QStringLiteral("test"); }
    void connectDevice() override { setState(LinkState::Connected); }
    void disconnectDevice() override { setState(LinkState::Disconnected); }
    void suggest(double v, double i) override { suggestedSpeed = v; suggestedIncline = i; ++suggestions; }
    void belt(double kmh, double incline = 1.0) { applyMeasurement(kmh, incline, true, -1, 0); }
    double suggestedSpeed = -1, suggestedIncline = -1;
    int suggestions = 0;
};

bool writeGpx(const QString &path, double lengthM)
{
    const double mPerDeg = RlvRoute::haversine(0, 0, 1, 0);
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><name>Straight</name><trkseg>\n";
    for (double d = 0; d <= lengthM + 1e-6; d += 10.0)
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"10.0\"><ele>%2</ele></trkpt>\n")
                   .arg(50.0 + d / mPerDeg, 0, 'f', 8).arg(20.0 + d * 0.01, 0, 'f', 2).toUtf8();
    gpx += "</trkseg></trk></gpx>\n";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(gpx);
    return true;
}

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<RunLibrary> lib;
    std::unique_ptr<RunSession> run;
    FakeTreadmill tm;

    bool init()
    {
        if (!dir.isValid() || !db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(6000000);
        lib = std::make_unique<RunLibrary>(&db, profiles.get());
        run = std::make_unique<RunSession>(&db, profiles.get(), hub.get(), lib.get());
        run->setManualClock(true);
        run->setMinimumSaveSeconds(5);
        tm.connectDevice();
        run->setTreadmill(&tm);
        return true;
    }
    // a running workout of the library by the start of its name
    qint64 workoutId(const QString &prefix) const
    {
        for (const QVariant &v : lib->workouts()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("name")).toString().startsWith(prefix))
                return m.value(QStringLiteral("workoutId")).toLongLong();
        }
        return -1;
    }
    void runFor(int seconds, double kmh, int hr = 0)
    {
        tm.belt(kmh);
        if (hr > 0)
            hub->updateHeartRate(hr);
        for (int i = 0; i < seconds * 4 && run->active(); ++i)
            run->advanceClock(250);
    }
};

} // namespace

// The figure an avatar map names, loaded (the preview does not wait for it)
static void waitForFigure(const QVariantMap &values, watt::RiderAvatar::Kind kind)
{
    const auto a = watt::RiderAvatar::fromVariant(values, kind);
    QVERIFY(a.figure() != nullptr);
    a.walkingFigure();
}

class TestTreadmill : public QObject
{
    Q_OBJECT
private slots:
    // --- FTMS Treadmill Data --------------------------------------------------
    void parsesTreadmillData()
    {
        // flags 0x000C: speed (bit 0 = 0) + total distance (bit 2) + incline/ramp (bit 3)
        // 10.50 km/h, 1234 m, incline 2.5 %, ramp 1.4 °
        const auto d = ftms::parseTreadmillData(bytes({ 0x0C, 0x00, 0x1A, 0x04, 0xD2, 0x04, 0x00, 0x19, 0x00, 0x0E, 0x00 }));
        QVERIFY(d.has_value());
        QCOMPARE(*d->speedKmh, 10.5);
        QCOMPARE(*d->totalDistanceM, 1234);
        QCOMPARE(*d->inclinePercent, 2.5);
        QVERIFY(!d->heartRateBpm);
        // negative incline
        const auto down = ftms::parseTreadmillData(bytes({ 0x08, 0x00, 0xE8, 0x03, 0xF6, 0xFF, 0x00, 0x00 }));
        QVERIFY(down.has_value());
        QCOMPARE(*down->speedKmh, 10.0);
        QCOMPARE(*down->inclinePercent, -1.0);
        // "not available" incline (0x7FFF)
        const auto na = ftms::parseTreadmillData(bytes({ 0x08, 0x00, 0xE8, 0x03, 0xFF, 0x7F, 0xFF, 0x7F }));
        QVERIFY(na.has_value());
        QVERIFY(!na->inclinePercent);
        // heart rate + elapsed time (bits 8, 10)
        const auto hr = ftms::parseTreadmillData(bytes({ 0x00, 0x05, 0xB0, 0x04, 0x8C, 0x3C, 0x00 }));
        QVERIFY(hr.has_value());
        QCOMPARE(*hr->speedKmh, 12.0);
        QCOMPARE(*hr->heartRateBpm, 140);
        QCOMPARE(*hr->elapsedTimeS, 60);
        // more data: no speed
        const auto more = ftms::parseTreadmillData(bytes({ 0x01, 0x00 }));
        QVERIFY(more.has_value());
        QVERIFY(!more->speedKmh);
        // truncated
        QVERIFY(!ftms::parseTreadmillData(bytes({ 0x0C, 0x00, 0x1A })).has_value());
        QVERIFY(!ftms::parseTreadmillData(bytes({ 0x0C })).has_value());
    }

    void paceFieldWidthIsDetected()
    {
        // flags bit 5 (instantaneous pace): spec UInt8 vs. a treadmill sending UInt16
        const auto u8 = ftms::parseTreadmillData(bytes({ 0x20, 0x00, 0xE8, 0x03, 0x02 }));
        QVERIFY(u8.has_value());
        QCOMPARE(*u8->paceKmPerMin, 0.2);
        const auto u16 = ftms::parseTreadmillData(bytes({ 0x28, 0x00, 0xE8, 0x03, 0x0A, 0x00, 0x00, 0x00, 0x02, 0x00 }));
        QVERIFY(u16.has_value());
        QCOMPARE(*u16->speedKmh, 10.0);
        QCOMPARE(*u16->paceKmPerMin, 0.2);
        QCOMPARE(*u16->inclinePercent, 1.0);
    }

    void serviceDataTreadmillBit()
    {
        const auto sd = ftms::parseServiceData(bytes({ 0x01, 0x01, 0x00 }));
        QVERIFY(sd.has_value());
        QVERIFY(sd->treadmill());
        QVERIFY(sd->treadmillOnly());
        QVERIFY(!sd->rowerOnly());
        const auto both = ftms::parseServiceData(bytes({ 0x01, 0x21, 0x00 }));
        QVERIFY(!both->treadmillOnly());
    }

    // --- device contract ----------------------------------------------------------
    void treadmillDeviceGoesStale()
    {
        FakeTreadmill tm;
        QSignalSpy spy(&tm, &TreadmillDevice::measurementChanged);
        tm.connectDevice();
        tm.belt(11.0, 2.0);
        QCOMPARE(tm.speedKmh(), 11.0);
        QCOMPARE(tm.inclinePercent(), 2.0);
        QVERIFY(tm.inclineKnown());
        tm.checkStale(1000);
        QCOMPARE(tm.speedKmh(), 11.0);
        tm.checkStale(TreadmillDevice::kStaleMs + 1);
        QCOMPARE(tm.speedKmh(), 0.0);
        QVERIFY(spy.count() >= 2);
        tm.belt(9.0);
        tm.disconnectDevice();
        QCOMPARE(tm.speedKmh(), 0.0);
    }

    void simulatedTreadmillFollowsSuggestions()
    {
        SimulatedTreadmill sim;
        for (int i = 0; i < 80; ++i)
            sim.step(0.25);
        QVERIFY(std::abs(sim.belt() - 10.0) < 0.06);
        sim.suggest(14.0, 4.0);
        for (int i = 0; i < 80; ++i)
            sim.step(0.25);
        QVERIFY(std::abs(sim.speedKmh() - 14.0) < 0.1);
        QCOMPARE(sim.inclinePercent(), 4.0);
        sim.suggest(-1, -1);   // workout over: back to the runner's own pace
        for (int i = 0; i < 80; ++i)
            sim.step(0.25);
        QVERIFY(std::abs(sim.belt() - 10.0) < 0.06);
    }

    // --- standard route ---------------------------------------------------------------
    void standardRouteIsA10kLoopWithTurns()
    {
        const RlvRoute r = runroutes::standardRoute();
        QVERIFY(r.isValid());
        QVERIFY2(std::abs(r.totalDistance() - 10000.0) < 150.0, qPrintable(QString::number(r.totalDistance())));
        QVERIFY(runroutes::isLoop(r));
        // turns: heading changes of more than 35 degrees over 150 m
        const auto &p = r.points();
        auto heading = [&](int i, int j) {
            const double dy = p.at(j).lat - p.at(i).lat;
            const double dx = (p.at(j).lon - p.at(i).lon) * std::cos(p.at(i).lat * M_PI / 180.0);
            return std::atan2(dy, dx);
        };
        int turns = 0;
        bool inTurn = false;
        const int span = 18;   // ~150 m at 8 m spacing
        for (int i = span; i + span < p.size(); i += 3) {
            double d = heading(i, i + span) - heading(i - span, i);
            while (d > M_PI) d -= 2 * M_PI;
            while (d < -M_PI) d += 2 * M_PI;
            const bool turning = std::abs(d) > 35.0 * M_PI / 180.0;
            if (turning && !inTurn)
                ++turns;
            inTurn = turning;
        }
        QVERIFY2(turns >= 10, qPrintable(QStringLiteral("%1 turns").arg(turns)));
        // rolling, not flat, not steep
        QVERIFY(r.maxElevation() - r.minElevation() > 10.0);
        QVERIFY(r.maxElevation() - r.minElevation() < 60.0);
        for (double d = 0; d < r.totalDistance(); d += 100)
            QVERIFY(std::abs(r.gradeAtDistance(d)) < 6.0);
    }

    // --- workouts -----------------------------------------------------------------------
    void runWorkoutTargets()
    {
        const auto lib = RunWorkout::builtInLibrary();
        QVERIFY(lib.size() >= 6);
        const RunWorkout &ant = lib.at(0);
        QVERIFY(ant.isAntTest());
        QCOMPARE(ant.totalDurationSec(), 45 * 60);
        QCOMPARE(ant.antTestStepIndex(), 1);
        QCOMPARE(ant.stepStartSec(1), 600);
        // 100 % of a 13 km/h AnT in the test block
        QCOMPARE(ant.targetSpeedAt(700, 13.0), 13.0);
        // warm-up ramp 65 % -> 82 %
        QCOMPARE(ant.targetSpeedAt(0, 10.0), 6.5);
        QVERIFY(std::abs(ant.targetSpeedAt(300, 10.0) - 7.35) < 1e-6);
        QCOMPARE(ant.targetSpeedAt(ant.totalDurationSec() + 1, 10.0), 0.0);

        RunStep abs;
        abs.absolute = true;
        abs.start = abs.end = 15.0;
        abs.durationSec = 120;
        QCOMPARE(abs.speedAt(10, 12.0), 15.0);
        QCOMPARE(abs.fractionAt(10, 12.0), 1.25);
        QCOMPARE(RunStep::fromJson(abs.toJson()), abs);

        const RunWorkout &threshold = lib.at(2);
        QVERIFY(threshold.summary(12.0).contains(QStringLiteral("3 × 10 min")));
        QVERIFY(threshold.estimatedRtss(12.0) > 40);
        QCOMPARE(runzones::zoneForFraction(0.70), 0);
        QCOMPARE(runzones::zoneForFraction(0.80), 1);
        QCOMPARE(runzones::zoneForFraction(1.00), 4);
        QCOMPARE(runzones::zoneForFraction(1.10), 5);
        QCOMPARE(running::paceText(12.0), QStringLiteral("5:00"));
        QCOMPARE(running::gradeAdjustedKmh(10.0, 0.0), 10.0);
        QVERIFY(running::gradeAdjustedKmh(10.0, 5.0) > 12.0);
    }

    void antTestEvaluation()
    {
        const int warm = 600, test = 1800;
        QVector<double> speed, incline;
        QVector<int> hr;
        for (int i = 0; i < warm + test + 60; ++i) {
            const bool inTest = i >= warm && i < warm + test;
            // first 10 min of the test faster: only the last 20 min count
            speed.append(!inTest ? 9.0 : i < warm + 600 ? 16.0 : 14.0);
            incline.append(1.0);
            hr.append(inTest ? 170 : 130);
        }
        const auto r = running::evaluateAntTest(speed, incline, hr, warm, warm + test);
        QVERIFY(r.valid);
        QVERIFY(std::abs(r.speedKmh - 14.0) < 1e-9);
        QCOMPARE(r.heartRate, 170);
        QCOMPARE(r.inclinePercent, 1.0);
        // not completed
        QVERIFY(!running::evaluateAntTest(speed.mid(0, warm + 900), incline, hr, warm, warm + test).valid);
        // belt stood still
        QVector<double> stopped = speed;
        for (int i = warm + 1000; i < warm + 1500; ++i)
            stopped[i] = 0;
        QVERIFY(!running::evaluateAntTest(stopped, incline, hr, warm, warm + test).valid);
    }

    // --- library --------------------------------------------------------------------------
    void antHistoryAndReminder()
    {
        Fixture f;
        QVERIFY(f.init());
        QCOMPARE(f.lib->antSpeedKmh(), running::kDefaultAntKmh);
        QVERIFY(!f.lib->antTested());
        QVERIFY(f.lib->testDue());
        QVERIFY(!f.lib->setManualAnt(40.0, 0));
        QVERIFY(f.lib->setManualAnt(13.4, 172));
        QCOMPARE(f.lib->antSpeedKmh(), 13.4);
        QCOMPARE(f.lib->antHeartRate(), 172);
        QVERIFY(f.lib->testDue());   // set by hand: still no test
        QVERIFY(f.lib->saveTestResult(13.8, 175, 1.0, -1) > 0);
        QCOMPARE(f.lib->antSpeedKmh(), 13.8);
        QVERIFY(!f.lib->testDue());
        QCOMPARE(f.lib->daysSinceTest(), 0);
        QCOMPARE(f.lib->antHistory().size(), 2);
        f.lib->setTestIntervalWeeks(4);
        f.lib->setTodayForTesting(QDate::currentDate().addDays(27));
        QVERIFY(!f.lib->testDue());
        f.lib->setTodayForTesting(QDate::currentDate().addDays(28));
        QVERIFY(f.lib->testDue());
        // zones follow the AnT
        const QVariantList zones = f.lib->zones();
        QCOMPARE(zones.size(), 6);
        QVERIFY(std::abs(zones.at(4).toMap().value(QStringLiteral("from")).toDouble() - 13.8) < 1e-9);
        // a second profile has its own AnT
        const qint64 other = f.profiles->createProfile(QStringLiteral("Other"));
        QVERIFY(other > 0);
        QVERIFY(f.profiles->selectProfile(other));
        QCOMPARE(f.lib->antSpeedKmh(), running::kDefaultAntKmh);
    }

    // speed correction in km/h (compared with a watch / foot pod)
    void speedOffset()
    {
        QCOMPARE(TreadmillDevice::correctedSpeed(10.0, -0.1), 9.9);
        QCOMPARE(TreadmillDevice::correctedSpeed(10.0, 0.3), 10.3);
        QCOMPARE(TreadmillDevice::correctedSpeed(0.0, 0.5), 0.0);   // a standing belt stays 0
        SimulatedTreadmill t;
        t.setSpeedOffsetKmh(-0.14);
        QCOMPARE(t.speedOffsetKmh(), -0.1);
        t.setSpeedOffsetKmh(9);
        QCOMPARE(t.speedOffsetKmh(), TreadmillDevice::kMaxSpeedOffset);
    }

    void workoutConversion()
    {
        RunWorkout r;
        r.name = QStringLiteral("Mixed");
        r.category = QStringLiteral("Intervals");
        RunStep pct;
        pct.type = BlockType::Ramp;
        pct.start = 0.7;
        pct.end = 0.9;
        pct.inclinePercent = 2.5;
        RunStep kmh;
        kmh.absolute = true;
        kmh.start = kmh.end = 15.0;
        r.steps = { pct, kmh };
        const Workout w = r.toWorkout(12.0);
        QVERIFY(w.isRunning());
        QCOMPARE(w.intervals.size(), 2);
        QCOMPARE(w.intervals.at(0).endPercent, 0.9);
        QCOMPARE(w.intervals.at(0).inclinePercent, 2.5);
        QCOMPARE(w.intervals.at(1).startPercent, 1.25);   // 15 km/h of a 12 km/h AnT
        const RunWorkout back = RunWorkout::fromWorkout(w);
        QCOMPARE(back.steps.size(), 2);
        QVERIFY(!back.steps.at(1).absolute);
        QCOMPARE(back.steps.at(1).speedAt(0, 12.0), 15.0);
        QCOMPARE(back.steps.at(0).inclinePercent, 2.5);
        // the incline survives the interval JSON (.awt / database)
        const Interval i = Interval::fromJson(w.intervals.at(0).toJson());
        QCOMPARE(i, w.intervals.at(0));
        QVERIFY(!Interval::fromJson(Interval().toJson()).inclinePercent);
        // summaries in km/h; the "FTP" of running is the AnT in 0.1 km/h
        Profile p;
        p.antSpeedKmh = 12.0;
        QCOMPARE(p.ftpFor(sport::running()), 120);
        QCOMPARE(w.summary(120), QStringLiteral("5 min @ 15.0 km/h"));
        // Zwift: running workouts are "run" workouts, both ways
        const QByteArray zwo = workoutio::toZwo(w);
        QVERIFY(zwo.contains("<sportType>run</sportType>"));
        const auto parsed = workoutio::parseZwo(zwo);
        QCOMPARE(parsed.workouts.size(), 1);
        QVERIFY(parsed.workouts.at(0).isRunning());
    }

    void runningWorkoutsAreLibraryWorkouts()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.lib->setManualAnt(12.0, 0));
        // walk -> run speed: per profile, clamped, used by the run session
        QCOMPARE(f.lib->walkRunKmh(), RunLibrary::kDefaultWalkRunKmh);
        f.lib->setWalkRunKmh(7.44);
        QCOMPARE(f.lib->walkRunKmh(), 7.4);
        QCOMPARE(f.run->walkRunKmh(), 7.4);
        f.lib->setWalkRunKmh(50);
        QCOMPARE(f.lib->walkRunKmh(), RunLibrary::kMaxWalkRunKmh);
        f.lib->setWalkRunKmh(7.4);
        f.lib->reload();
        QCOMPARE(f.lib->walkRunKmh(), 7.4);   // stored
        // the built-in running workouts are seeded into the workouts table
        const int builtIns = int(RunWorkout::builtInLibrary().size());
        QCOMPARE(f.lib->workouts().size(), builtIns);
        int running = 0;
        for (const Workout &w : f.db.workouts())
            running += w.isRunning() ? 1 : 0;
        QCOMPARE(running, builtIns);
        const qint64 antId = f.lib->antTestWorkoutId();
        QVERIFY(antId >= 0);
        QVERIFY(f.db.workout(antId)->isProtected());
        QVERIFY(!f.db.deleteWorkout(antId));
        // seeding is not repeated
        f.db.seedDefaults();
        f.lib->reloadWorkouts();
        QCOMPARE(f.lib->workouts().size(), builtIns);

        // the Workout Builder makes running workouts (% AnT, km/h, incline)
        WorkoutBuilder b(&f.db, f.profiles.get());
        b.newWorkout(QStringLiteral("running"));
        QVERIFY(b.isRunning());
        QCOMPARE(b.ftp(), 120);
        QCOMPARE(b.antSpeedKmh(), 12.0);
        QVERIFY(b.categories().contains(QStringLiteral("AnT Test")));
        QCOMPARE(b.blockCount(), 3);
        QCOMPARE(b.block(0).value(QStringLiteral("incline")).toDouble(), 1.0);
        QCOMPARE(b.block(0).value(QStringLiteral("cadence")).toInt(), 0);
        QCOMPARE(b.block(1).value(QStringLiteral("percent")).toInt(), 85);
        QCOMPARE(b.block(1).value(QStringLiteral("speed")).toDouble(), 10.2);
        b.setSelectedIndex(1);
        b.addIntervalSet(4, 180, 105, 120, 70, false);
        QCOMPARE(b.blockCount(), 3 + 7);
        b.setSpeed(2, 13.8);   // km/h -> % of the AnT
        QCOMPARE(b.block(2).value(QStringLiteral("percent")).toInt(), 115);
        b.setIncline(2, 3.04);
        QCOMPARE(b.block(2).value(QStringLiteral("incline")).toDouble(), 3.0);
        QCOMPARE(b.block(2).value(QStringLiteral("zone")).toInt(), 5);   // running zones
        b.setName(QStringLiteral("My Intervals"));
        const qint64 id = b.save();
        QVERIFY(id > 0);
        f.lib->reloadWorkouts();
        QCOMPARE(f.lib->workouts().size(), builtIns + 1);
        const auto saved = f.lib->workout(id);
        QVERIFY(saved.has_value());
        QCOMPARE(saved->steps.size(), 10);
        QCOMPARE(saved->steps.at(2).inclinePercent, 3.0);
        QVERIFY(std::abs(saved->steps.at(2).speedAt(0, 12.0) - 13.8) < 1e-9);
        // the run session runs it
        QVERIFY(f.run->attachWorkout(id));
        QCOMPARE(f.run->workoutId(), id);
        // the cycling session refuses it
        QVERIFY(f.db.workout(id)->isRunning());

        // the speeds follow the AnT (Builder via the profile)
        QVERIFY(f.lib->setManualAnt(13.0, 0));
        QCOMPARE(b.ftp(), 130);
        QCOMPARE(f.profiles->current().ftpFor(QStringLiteral("running")), 130);

        // a built-in is edited as a copy; switching the sport adapts the blocks
        QVERIFY(b.loadWorkout(f.workoutId(QStringLiteral("Threshold"))));
        QVERIFY(b.save() > 0);
        f.lib->reloadWorkouts();
        QCOMPARE(f.lib->workouts().size(), builtIns + 2);
        b.setSport(QStringLiteral("cycling"));
        QCOMPARE(b.block(0).value(QStringLiteral("incline")).toDouble(), 0.0);
        QCOMPARE(b.categories().constLast(), QStringLiteral("FTP Tests"));
        QVERIFY(f.db.deleteWorkout(id));
        f.lib->reloadWorkouts();
        QCOMPARE(f.lib->workouts().size(), builtIns + 1);
    }

    void runWorkoutsMigrateIntoWorkouts()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v6.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        // turn it into a schema-6 database with an own running workout
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v6"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            QVERIFY(q.exec(QStringLiteral("DELETE FROM workouts WHERE sport = 'running'")));
            QVERIFY(q.exec(QStringLiteral("DELETE FROM settings WHERE key = 'runWorkoutsSeeded'")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE run_workouts (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
                                          " category TEXT, description TEXT, steps TEXT, created_at INTEGER, updated_at INTEGER)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO run_workouts (name, category, description, steps) VALUES ('Old Run',"
                                          " 'Intervals', 'kept', '[{\"type\":\"interval\",\"duration\":300,\"mode\":\"speed\","
                                          "\"start\":15,\"end\":15,\"incline\":2}]')")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 6")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v6"));
        Database db;
        QVERIFY(db.open(path));
        db.seedDefaults();
        QVERIFY(!db.hasTable(QStringLiteral("run_workouts")));
        int builtIn = 0;
        std::optional<Workout> old;
        for (const Workout &w : db.workouts()) {
            if (!w.isRunning())
                continue;
            if (w.builtIn)
                ++builtIn;
            else if (w.name == QLatin1String("Old Run"))
                old = w;
        }
        QCOMPARE(builtIn, int(RunWorkout::builtInLibrary().size()));
        QVERIFY(old.has_value());
        QCOMPARE(old->description, QStringLiteral("kept"));
        QCOMPARE(old->intervals.size(), 1);
        QCOMPARE(old->intervals.at(0).startPercent, 15.0 / running::kDefaultAntKmh);
        QCOMPARE(old->intervals.at(0).inclinePercent, 2.0);
    }

    // --- session --------------------------------------------------------------------------
    void startNeedsTreadmill()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy refused(f.run.get(), &RunSession::startRefused);
        f.tm.disconnectDevice();
        QVERIFY(!f.run->start());
        QCOMPARE(refused.count(), 1);
        f.tm.connectDevice();
        // the belt moving never starts a run
        f.tm.belt(10.0);
        QCOMPARE(f.run->state(), QStringLiteral("ready"));
        QVERIFY(f.run->start());
        QCOMPARE(f.run->state(), QStringLiteral("running"));
        QVERIFY(f.run->runnerFigure());
        QCOMPARE(f.run->routePath(), runroutes::standardPath());
    }

    void distanceFollowsTheBeltAndLaps()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.run->routeIsLoop());
        QVERIFY(f.run->start());
        f.runFor(60, 12.0, 150);
        QVERIFY(std::abs(f.run->runDistanceM() - 200.0) < 0.5);
        QVERIFY(std::abs(f.run->distanceMeters() - 200.0) < 0.5);
        QCOMPARE(f.run->speedKmh(), 12.0);
        QCOMPARE(f.run->pace(), QStringLiteral("5:00"));
        QCOMPARE(f.run->heartRate(), 150);
        QVERIFY(f.run->cadence() > 150);
        QVERIFY(f.run->calories() > 10);
        QVERIFY(std::abs(f.run->climbedM() - 2.0) < 0.05);   // 200 m at 1 %
        // pause: nothing moves
        f.run->pause();
        f.run->advanceClock(5000);
        QVERIFY(std::abs(f.run->runDistanceM() - 200.0) < 0.5);
        f.run->resume();
        // a full lap of the loop at 20 km/h (no workout): the run goes on
        const double len = f.run->routeLengthM();
        f.runFor(int(std::ceil(len / (20.0 / 3.6))) + 5, 20.0);
        QVERIFY(f.run->active());
        QCOMPARE(f.run->lap(), 2);
        QVERIFY(f.run->distanceMeters() < 300.0);
        const qint64 id = f.run->finish(true);
        QVERIFY(id > 0);
        const auto ride = f.db.ride(id);
        QVERIFY(ride.has_value());
        QCOMPARE(ride->sport, QStringLiteral("running"));
        QVERIFY(std::abs(ride->distanceM - f.run->runDistanceM()) < 1.0);
        QCOMPARE(f.db.rideSamples(id).size(), f.run->samples().size());
        QVERIFY(f.db.rideSamples(id).at(10).speedMps > 3.0);
    }

    void gpxOnAndOff()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(!f.run->gpxEnabled());
        f.run->setGpxEnabled(true);   // nothing loaded yet: refused
        QVERIFY(!f.run->gpxEnabled());
        const QString path = f.dir.filePath(QStringLiteral("straight.gpx"));
        QVERIFY(writeGpx(path, 1000));
        QVERIFY(!f.run->selectGpx(f.dir.filePath(QStringLiteral("nope.txt"))));
        QSignalSpy routeSpy(f.run.get(), &RunSession::routeChanged);
        QVERIFY(f.run->selectGpx(QUrl::fromLocalFile(path).toString()));
        QVERIFY(f.run->gpxEnabled());
        QVERIFY(routeSpy.count() >= 1);
        QVERIFY(std::abs(f.run->routeLengthM() - 1000.0) < 5.0);
        QVERIFY(!f.run->routeIsLoop());
        QCOMPARE(f.run->routeName(), QStringLiteral("Straight"));
        f.run->setGpxEnabled(false);
        QCOMPARE(f.run->routeName(), runroutes::standardName());
        f.run->setGpxEnabled(true);
        QCOMPARE(f.run->gpxPath(), path);
        // remembered
        RunSession again(&f.db, f.profiles.get(), f.hub.get(), f.lib.get());
        QVERIFY(again.gpxEnabled());
        QCOMPARE(again.gpxPath(), path);

        // point to point: the free run finishes (and is saved) at the end
        QVERIFY(f.run->start());
        f.run->setGpxEnabled(false);   // locked while running
        QVERIFY(f.run->gpxEnabled());
        f.runFor(400, 12.0);
        QCOMPARE(f.run->state(), QStringLiteral("finished"));
        QVERIFY(f.run->completed());
        QVERIFY(f.run->lastRideId() > 0);
        QVERIFY(std::abs(f.run->distanceMeters() - f.run->routeLengthM()) < 1e-6);
    }

    void loopRouteOutAndBack()
    {
        // Loop route on a point-to-point GPX (as GPX Ride): out, back, out ... until
        // Finish - a lap each way, the way back counted down; set before the start only
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("straight.gpx"));
        QVERIFY(writeGpx(path, 500));
        QVERIFY(f.run->selectGpx(path));
        QVERIFY(!f.run->loop());
        QVERIFY(!f.run->loopRide());
        f.run->setLoop(true);
        QVERIFY(f.run->loop() && f.run->loopRide());
        RunSession again(&f.db, f.profiles.get(), f.hub.get(), f.lib.get());
        QVERIFY(again.loop());   // (remembered)
        QVERIFY(f.run->start());
        f.run->setLoop(false);   // locked while running
        QVERIFY(f.run->loop());
        f.runFor(180, 12.0);     // 600 m: at the end, 100 m back
        QCOMPARE(f.run->lap(), 2);
        QVERIFY(f.run->returning());
        QVERIFY(std::abs(f.run->distanceMeters() - 400.0) < 15.0);
        QVERIFY(f.run->routeGradePercent() < 0);   // (the way back: downhill on a rising track)
        f.runFor(150, 12.0);     // +500 m: back at the start, 100 m out again
        QCOMPARE(f.run->lap(), 3);
        QVERIFY(!f.run->returning());
        QVERIFY(f.run->active());   // (not finished at the route's end)
        QVERIFY(std::abs(f.run->distanceMeters() - 100.0) < 15.0);
    }

    void workoutTargetsAndSuggestions()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.lib->setManualAnt(12.0, 0));
        const qint64 threshold = f.workoutId(QStringLiteral("Threshold"));   // Threshold 3 × 10
        QVERIFY(f.run->attachWorkout(threshold));
        QVERIFY(f.run->hasWorkout());
        QCOMPARE(f.run->currentStepIndex(), 0);
        QVERIFY(std::abs(f.run->targetSpeedKmh() - 7.8) < 1e-9);   // 65 % of 12
        QVERIFY(f.tm.suggestions > 0);                              // (only the virtual treadmill follows)
        QVERIFY(f.run->start());
        f.runFor(600, 9.0);   // warm-up
        QCOMPARE(f.run->currentStepIndex(), 1);
        QCOMPARE(f.run->targetSpeedKmh(), 12.0);
        QCOMPARE(f.run->targetPercent(), 100);
        QCOMPARE(f.tm.suggestedSpeed, 12.0);
        f.runFor(300, 12.1);
        QVERIFY(f.run->compliance() > 25);
        QVERIFY(f.run->targetSeries().size() > 10);
        QVERIFY(f.run->nextStepText().contains(QStringLiteral("Jog")));
        // the AnT changing mid-run does not change the running targets
        QVERIFY(f.lib->setManualAnt(15.0, 0));
        QCOMPARE(f.run->targetSpeedKmh(), 12.0);
        // the loop continues until the workout is over, then the run is saved
        f.runFor(4000, 12.0);
        QCOMPARE(f.run->state(), QStringLiteral("finished"));
        QVERIFY(f.run->completed());
        QVERIFY(f.run->lastRideId() > 0);
        QCOMPARE(f.db.ride(f.run->lastRideId())->workoutName, QStringLiteral("Threshold 3 × 10"));
        QCOMPARE(f.db.ride(f.run->lastRideId())->workoutId, threshold);
        QCOMPARE(f.tm.suggestedSpeed, -1.0);
    }

    void antTestFlow()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.lib->setManualAnt(12.0, 165));
        QVERIFY(f.run->setupAntTest());
        QVERIFY(f.run->antTest());
        QSignalSpy done(f.run.get(), &RunSession::antTestCompleted);
        QVERIFY(f.run->start());
        f.runFor(600, 9.0, 140);
        QVERIFY(f.run->antTestBlockActive());
        f.runFor(600, 13.0, 168);
        f.runFor(1199, 13.6, 176);
        QCOMPARE(done.count(), 0);
        f.runFor(2, 13.6, 176);
        QCOMPARE(done.count(), 1);
        const QVariantMap r = f.run->antResult();
        QVERIFY(r.value(QStringLiteral("valid")).toBool());
        QVERIFY(std::abs(r.value(QStringLiteral("speed")).toDouble() - 13.6) < 0.051);
        QCOMPARE(r.value(QStringLiteral("heartRate")).toInt(), 176);
        QCOMPARE(r.value(QStringLiteral("previousSpeed")).toDouble(), 12.0);
        QVERIFY(f.run->active());   // the cool-down goes on
        QVERIFY(f.run->acceptAntResult());
        QCOMPARE(f.lib->antSpeedKmh(), 13.6);
        QCOMPARE(f.lib->antHeartRate(), 176);
        QVERIFY(!f.lib->testDue());
        // the run ends with the cool-down; the test entry points to the saved run
        f.runFor(400, 8.0, 130);
        QCOMPARE(f.run->state(), QStringLiteral("finished"));
        const qint64 rideId = f.run->lastRideId();
        QVERIFY(rideId > 0);
        QCOMPARE(f.db.latestAntTest(f.profiles->profileId())->rideId, rideId);
    }

    void discardSavesNothing()
    {
        Fixture f;
        QVERIFY(f.init());
        const int before = int(f.db.rides(f.profiles->profileId()).size());
        QVERIFY(f.run->start());
        f.runFor(60, 10.0);
        f.run->discard();
        QCOMPARE(f.run->lastRideId(), qint64(-1));
        QCOMPARE(int(f.db.rides(f.profiles->profileId()).size()), before);
        f.run->reset();
        QCOMPARE(f.run->state(), QStringLiteral("ready"));
        QCOMPARE(f.run->runDistanceM(), 0.0);
    }

    // --- 3D runner ----------------------------------------------------------------------
    void runnerFigureMoves()
    {
        const RlvRoute r = runroutes::standardRoute();
        std::vector<route3d::GeoPoint> geo;
        for (const auto &p : r.points())
            geo.push_back({ p.lat, p.lon, p.elevation });
        const route3d::Route route = route3d::Route::fromGeo(geo);
        route3d::GhostRunner a;
        const auto &v0 = a.build(route, 100.0, 12.0, 0.0);
        QVERIFY(v0.size() > 1000);
        QCOMPARE(a.indices().size(), v0.size());
        const std::vector<route3d::GhostVertex> first(v0.begin(), v0.end());
        const auto &v1 = a.build(route, 100.0, 12.0, 1.5);
        QCOMPARE(v1.size(), first.size());
        // the stride changes the pose
        double moved = 0;
        for (size_t i = 0; i < first.size(); ++i)
            moved = std::max(moved, double((first[i].position - v1[i].position).length()));
        QVERIFY(moved > 0.05);
        for (const auto &v : v1)
            QVERIFY(std::isfinite(v.position.x()) && std::isfinite(v.position.y()) && std::isfinite(v.position.z()));
    }

    // The bundled runner.glb: a skinned, animated (running cycle) model,
    // normalised to a 1.78 m figure standing on y = 0, facing +z, in place.
    void glbRunnerModel()
    {
        const QString path = QStringLiteral(WATT_SOURCE_DIR "/assets/models/runner.glb");
        route3d::GlbModel m;
        QString error;
        QVERIFY2(m.loadFile(path, &error), qPrintable(error));
        QVERIFY(m.duration() > 0.3 && m.duration() < 2.0);
        QVERIFY(m.vertexCount() > 1000);
        QCOMPARE(m.indices().size() % 3, size_t(0));
        std::vector<route3d::GhostVertex> a, b;
        float minY = 1e9f, maxY = -1e9f, maxXZ = 0;
        double moved = 0;
        for (int s = 0; s < 12; ++s) {
            m.pose(s / 12.0, a);
            for (const auto &v : a) {
                minY = std::min(minY, v.position.y());
                maxY = std::max(maxY, v.position.y());
                maxXZ = std::max({ maxXZ, std::abs(v.position.x()), std::abs(v.position.z()) });
                QVERIFY(std::isfinite(v.position.x()) && std::isfinite(v.normal.y()));
            }
            if (s == 3)
                b = a;
        }
        m.pose(0.0, a);
        for (size_t i = 0; i < a.size(); ++i)
            moved = std::max(moved, double((a[i].position - b[i].position).length()));
        qInfo("runner.glb: %zu vertices, %zu triangles, cycle %.3f s, y %.2f..%.2f, |xz| <= %.2f, max move %.2f",
              m.vertexCount(), m.indices().size() / 3, m.duration(), minY, maxY, maxXZ, moved);
        QVERIFY(std::abs(minY) < 0.02f);                 // feet on the ground at their lowest
        QVERIFY(maxY > 1.4f && maxY < 1.95f);            // a running figure ~1.6-1.8 m
        QVERIFY(maxXZ < 1.2f);                           // in place
        QVERIFY(moved > 0.15);                           // it is animated
        // feet in front of / behind the body (running along z, not sideways)
        float minZ = 1e9f, maxZ = -1e9f, minX = 1e9f, maxX = -1e9f;
        for (const auto &v : a) {
            if (v.position.y() > 0.3f)
                continue;
            minZ = std::min(minZ, v.position.z()); maxZ = std::max(maxZ, v.position.z());
            minX = std::min(minX, v.position.x()); maxX = std::max(maxX, v.position.x());
        }
        qInfo("runner.glb feet: x %.2f..%.2f z %.2f..%.2f", minX, maxX, minZ, maxZ);
        // a broken file is refused
        QVERIFY(!m.load(QByteArray("not a glb"), &error));

        // walk / run cross-fade (a walker.glb must be the same mesh)
        QCOMPARE(route3d::GhostRunner::runWeight(4.0), 0.0);
        QCOMPARE(route3d::GhostRunner::runWeight(10.0), 1.0);
        QVERIFY(std::abs(route3d::GhostRunner::runWeight(6.25) - 0.5) < 1e-9);
        // the athlete's own walk -> run speed moves the cross-fade
        QVERIFY(std::abs(route3d::GhostRunner::runWeight(8.0, 8.0) - 0.5) < 1e-9);
        QCOMPARE(route3d::GhostRunner::runWeight(7.0, 8.0), 0.0);
        QCOMPARE(route3d::GhostRunner::runWeight(9.0, 8.0), 1.0);
        auto run = std::make_shared<route3d::GlbModel>();
        QVERIFY(run->loadFile(path));
        route3d::GhostRunner figure;
        QVERIFY(!figure.setWalkModel(run));   // no running model yet
        figure.setModel(run);
        QVERIFY(figure.setWalkModel(run));
        // the bundled walker.glb: the same figure walking
        auto walk = std::make_shared<route3d::GlbModel>();
        QVERIFY2(walk->loadFile(QStringLiteral(WATT_SOURCE_DIR "/assets/models/walker.glb"), &error), qPrintable(error));
        QVERIFY(walk->duration() > 0.5);
        QVERIFY(figure.setWalkModel(walk));
        std::vector<route3d::GeoPoint> geo;
        const RlvRoute standard = runroutes::standardRoute();   // (kept alive: points() refers to it)
        for (const auto &p : standard.points())
            geo.push_back({ p.lat, p.lon, p.elevation });
        const route3d::Route route = route3d::Route::fromGeo(geo);
        for (double kmh : { 4.0, 6.2, 12.0 }) {
            const auto &v = figure.build(route, 50.0, kmh, 1.0);
            QCOMPARE(v.size(), run->vertexCount());
            QCOMPARE(figure.indices().size(), run->indices().size());
            QVERIFY(std::isfinite(v.front().position.x()));
        }
    }

    // The bundled rider.glb: posed on the bike (no animation), feet on the pedals
    void glbRiderOnBike()
    {
        auto m = std::make_shared<route3d::GlbModel>();
        QString error;
        QVERIFY2(m->loadFile(QStringLiteral(WATT_SOURCE_DIR "/assets/models/rider.glb"), &error,
                             route3d::GlbModel::Outfit::Cyclist), qPrintable(error));
        QVERIFY(m->canRide());
        route3d::GhostRider rider;
        QVERIFY(rider.setRiderModel(m));
        std::vector<route3d::GeoPoint> geo;
        const RlvRoute standard = runroutes::standardRoute();   // (kept alive: points() refers to it)
        for (const auto &p : standard.points())
            geo.push_back({ p.lat, p.lon, p.elevation });
        const route3d::Route route = route3d::Route::fromGeo(geo);
        const auto &v = rider.build(route, 50.0, 30.0, 0.0);
        QVERIFY(v.size() > m->vertexCount());
        QCOMPARE(rider.indices().size() % 3, size_t(0));
        for (uint32_t i : rider.indices())
            QVERIFY(i < v.size());
        for (const auto &x : v)
            QVERIFY(std::isfinite(x.position.x()) && std::isfinite(x.position.y()));
        // the runner skeleton without fingers can ride too; a broken one is refused
        QVERIFY(!rider.setRiderModel(nullptr));
        // more rocking from side to side on climbs over 6 %
        QCOMPARE(route3d::GlbModel::swayForGrade(0.03), route3d::GlbModel::kSwayDeg);
        QCOMPARE(route3d::GlbModel::swayForGrade(0.12), route3d::GlbModel::kClimbSwayDeg);
        QVERIFY(route3d::GlbModel::swayForGrade(0.08) > route3d::GlbModel::kSwayDeg);
    }

    // A .glb whose texture claims to be huge (40000 px a side): not decoded
    // (nothing allocated for it), the figure still loads - untextured.
    void glbHugeTextureRejected()
    {
        QFile f(QStringLiteral(WATT_SOURCE_DIR "/assets/models/rider_male.glb"));
        if (!f.open(QIODevice::ReadOnly))
            QSKIP("assets/models/rider_male.glb is not there");
        QByteArray glb = f.readAll();
        const int ihdr = glb.indexOf("IHDR");
        QVERIFY(ihdr > 0);
        for (int k = 0; k < 2; ++k) {   // width, height: big-endian after "IHDR"
            const quint32 v = 40000;
            for (int b = 0; b < 4; ++b)
                glb[ihdr + 4 + k * 4 + b] = char((v >> (24 - 8 * b)) & 0xff);
        }
        route3d::GlbModel m;
        QString error;
        QVERIFY2(m.load(glb, &error, route3d::GlbModel::Outfit::Avatar), qPrintable(error));
        QVERIFY(!m.textured());
        QVERIFY(m.canRide());
    }

    // The textured rowers (rower_male.glb, rower_female.glb): posed in the
    // boat by the app, their own clothes from the texture - top, shorts and
    // hair found; barefoot and bare-headed (no shoes, no helmet).
    void glbRowerAvatar_data()
    {
        QTest::addColumn<QString>("id");
        QTest::newRow("male") << QStringLiteral("male");
        QTest::newRow("female") << QStringLiteral("female");
    }
    void glbRowerAvatar()
    {
        QFETCH(QString, id);
        const QString path = QStringLiteral(WATT_SOURCE_DIR "/assets/models/rower_%1.glb").arg(id);
        if (!QFile::exists(path))
            QSKIP("the model is not there");
        using route3d::GlbModel;
        auto m = std::make_shared<GlbModel>();
        if (id == QLatin1String("female"))
        {
            m->setNeckLength(.65f);   // (as the app: RiderAvatar)
            m->setNeckWidth(1.35f);
        }
        QString error;
        QVERIFY2(m->loadFile(path, &error, GlbModel::Outfit::RowerAvatar), qPrintable(error));
        QVERIFY(m->canRide());   // (the joints posing needs)
        QVERIFY(m->textured());
        size_t count[GlbModel::GarmentCount] = {};
        for (uint8_t g : m->garments())
            ++count[g];
        qInfo("rower_%s.glb: %zu vertices; top %zu, shorts %zu, helmet %zu, shoes %zu, hair %zu, other %zu", qPrintable(id),
              m->vertexCount(), count[GlbModel::Jersey], count[GlbModel::Shorts], count[GlbModel::Helmet],
              count[GlbModel::Shoes], count[GlbModel::Hair], count[GlbModel::NoGarment]);
        QCOMPARE(count[GlbModel::Helmet], size_t(0));
        QCOMPARE(count[GlbModel::Shoes], size_t(0));   // (barefoot)
        for (GlbModel::Garment g : { GlbModel::Jersey, GlbModel::Shorts, GlbModel::Hair }) {
            QVERIFY2(m->garmentColor(g).x() >= 0, "a garment was not found in the texture");
            QVERIFY(count[g] > 150);
        }
        // posed in the boat, through the stroke: the vertices move
        qputenv("WATT_ROWER_GLB", WATT_SOURCE_DIR "/assets/models/rower.glb");
        std::vector<route3d::GhostVertex> catchPose, finishPose;
        watt::rowerPreviewPose(*m, 0.0, catchPose);
        watt::rowerPreviewPose(*m, .5, finishPose);
        QCOMPARE(catchPose.size(), m->garments().size());
        float moved = 0;
        for (size_t i = 0; i < catchPose.size(); ++i) {
            QVERIFY(std::isfinite(catchPose[i].position.x()) && std::isfinite(finishPose[i].position.y()));
            moved = std::max(moved, (catchPose[i].position - finishPose[i].position).length());
        }
        QVERIFY(moved > .2f);
        // in the boat: the legs inside the hull (not out through its sides)
        {
            const float out = watt::rowerHullOverhang(*m);
            qInfo("  out through the hull's sides: %.1f cm at most", out * 100);
            QVERIFY(out < .01f);
        }
        // the profile's preview draws it
        qputenv(id == QLatin1String("male") ? "WATT_ROWER_MALE_GLB" : "WATT_ROWER_FEMALE_GLB", path.toUtf8());
        watt::AvatarPreviewItem preview;
        preview.setKind(QStringLiteral("rower"));
        QVERIFY(!preview.helmet());
        const int side = qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") > 0 ? qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") : 320;
        preview.setSize(QSizeF(side, side * 15 / 16));
        const QString dump = qEnvironmentVariable("WATT_AVATAR_DUMP");
        int k = 0;
        for (const QVariantMap &v : { QVariantMap { { "model", id } },
                                      QVariantMap { { "model", id }, { "jersey", "#d0202a" }, { "shorts", "#f5f5f5" },
                                                    { "hair", "#e6c27a" }, { "boat", "#f5c400" } } }) {
            waitForFigure(v, watt::RiderAvatar::Rower);
            preview.setAvatar(v);
            for (double yaw : { 35.0, 0.0, 70.0 }) {
                preview.setYaw(yaw);
                QImage img(side, side * 15 / 16, QImage::Format_ARGB32_Premultiplied);
                img.fill(Qt::white);
                QPainter painter(&img);
                preview.paint(&painter);
                painter.end();
                int drawn = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        drawn += img.pixel(x, y) != qRgb(255, 255, 255);
                if (!dump.isEmpty())
                    img.save(QStringLiteral("%1/rower_%2_%3.png").arg(dump, id).arg(k));
                QVERIFY(drawn > 3000 * side / 320 * side / 320);
                ++k;
            }
        }
        // no classic figure to choose: male and female only; an old "classic" is the male
        const QVariantList models = watt::RiderAvatar::models(watt::RiderAvatar::Rower);
        QVERIFY(!models.isEmpty() && models.size() <= 2);   // (those whose files are found)
        for (const QVariant &v : models)
            QVERIFY(v.toMap().value("id").toString() != QLatin1String("classic"));
        QCOMPARE(watt::RiderAvatar::fromVariant({ { "model", "classic" } }, watt::RiderAvatar::Rower).model, QStringLiteral("male"));
        const QVariantMap own = preview.ownColors();
        QVERIFY(own.contains(QStringLiteral("jersey")) && !own.contains(QStringLiteral("shoes"))
                && !own.contains(QStringLiteral("bike")) && own.contains(QStringLiteral("boat")));
        // the boat's colour: its hull in it (the decks stay)
        const auto yellow = watt::RiderAvatar::fromVariant({ { "model", id }, { "boat", "#f5c400" } },
                                                           watt::RiderAvatar::Rower);
        QVERIFY(yellow.boatColor.x() > .9f && yellow.boatColor.z() < .1f);
        std::vector<route3d::GhostVertex> plainHull, yellowHull;
        watt::rowerPreviewBoat(QVector3D(-1, 0, 0), 5.0, plainHull);
        watt::rowerPreviewBoat(yellow.boatColor, 5.0, yellowHull);
        QCOMPARE(plainHull.size(), yellowHull.size());
        int recoloured = 0;
        for (size_t i = 0; i < plainHull.size(); ++i)
            if (plainHull[i].color != yellowHull[i].color) {
                QCOMPARE(plainHull[i].color, watt::standardHullColor());
                ++recoloured;
            }
        QVERIFY(recoloured > 100 && recoloured < int(plainHull.size()));
    }

    // The textured runners (runner_male.glb, runner_female.glb): animated,
    // their own clothes from the texture, top / shorts / shoes / hair found.
    void glbRunnerAvatar_data()
    {
        QTest::addColumn<QString>("id");
        QTest::newRow("male") << QStringLiteral("male");
        QTest::newRow("female") << QStringLiteral("female");
    }
    void glbRunnerAvatar()
    {
        QFETCH(QString, id);
        const QString path = QStringLiteral(WATT_SOURCE_DIR "/assets/models/runner_%1.glb").arg(id);
        if (!QFile::exists(path))
            QSKIP("the model is not there");
        using route3d::GlbModel;
        // it moves as the classic runner (its motion moved onto this skeleton)
        GlbModel classic, classicWalk;
        QString error;
        QVERIFY2(classic.loadFile(QStringLiteral(WATT_SOURCE_DIR "/assets/models/runner.glb"), &error), qPrintable(error));
        QVERIFY2(classicWalk.loadFile(QStringLiteral(WATT_SOURCE_DIR "/assets/models/walker.glb"), &error), qPrintable(error));
        auto m = std::make_shared<GlbModel>();
        m->setLegsIn(id == QLatin1String("male") ? 3.5f : 4.f);   // (as the app: RiderAvatar)
        QVERIFY2(m->loadFile(path, &error, GlbModel::Outfit::RunnerAvatar, &classic), qPrintable(error));
        if (id == QLatin1String("male"))
        {
            m->setShoulderWidth(.88f);   // (as the app: RiderAvatar)
            m->slimUpperBody(.25f);
        }
        QVERIFY(m->retargeted());
        QVERIFY(m->textured());
        QCOMPARE(m->duration(), classic.duration());
        // feet on the ground: the lowest point over the cycle at the ground, both feet down in turn
        {
            std::vector<route3d::GhostVertex> v;
            float lowest = 1e9f;
            bool down[2] = { false, false };
            for (int s = 0; s < 48; ++s) {
                m->pose(s / 48.0, v);
                float low[2] = { 1e9f, 1e9f };
                for (const auto &x : v) {
                    float &l = low[x.position.x() < 0 ? 0 : 1];
                    l = std::min(l, x.position.y());
                }
                lowest = std::min({ lowest, low[0], low[1] });
                for (int f = 0; f < 2; ++f)
                    down[f] = down[f] || low[f] < .01f;
            }
            QVERIFY(std::abs(lowest) < .005f);
            QVERIFY(down[0] && down[1]);
        }
        // the feet land under the body (not wide apart): the planted foot near the middle
        {
            auto widest = [](const GlbModel &g) {
                std::vector<route3d::GhostVertex> v;
                float w = 0;
                for (int s = 0; s < 48; ++s) {
                    g.pose(s / 48.0, v);
                    for (const auto &x : v)
                        if (x.position.y() < .01f)
                            w = std::max(w, std::abs(x.position.x()));
                }
                return w;
            };
            const float mine = widest(*m), theirs = widest(classic);
            qInfo("  planted feet within %.1f cm of the middle (the classic runner's %.1f cm)", mine * 100, theirs * 100);
            QVERIFY(mine < theirs + .045f);   // (a bigger shoe)
        }
        // walking: the same figure with the walker's motion - a copy of it,
        // not a second load (cross-faded by speed)
        std::shared_ptr<const GlbModel> walk = m->withMotion(classicWalk);
        QVERIFY(walk);
        QVERIFY(walk->retargeted() && walk->textured());
        QCOMPARE(walk->vertexCount(), m->vertexCount());
        QCOMPARE(walk->duration(), classicWalk.duration());
        QCOMPARE(walk->garments(), m->garments());
        size_t count[GlbModel::GarmentCount] = {};
        for (uint8_t g : m->garments())
            ++count[g];
        qInfo("runner_%s.glb: %zu vertices, cycle %.2f s; top %zu, shorts %zu, helmet %zu, shoes %zu, hair %zu, other %zu",
              qPrintable(id), m->vertexCount(), m->duration(), count[GlbModel::Jersey], count[GlbModel::Shorts],
              count[GlbModel::Helmet], count[GlbModel::Shoes], count[GlbModel::Hair], count[GlbModel::NoGarment]);
        QCOMPARE(count[GlbModel::Helmet], size_t(0));   // (bare-headed)
        for (GlbModel::Garment g : { GlbModel::Jersey, GlbModel::Shorts, GlbModel::Shoes, GlbModel::Hair }) {
            QVERIFY2(m->garmentColor(g).x() >= 0, "a garment was not found in the texture");
            QVERIFY(count[g] > 150);
        }
        // runs: the colours chosen are drawn, the stride moves the legs
        route3d::GhostRunner runner;
        runner.setModel(m);
        QVERIFY(runner.setWalkModel(walk));
        GlbModel::GarmentColors red;
        red.color[GlbModel::Jersey] = QVector3D(.85f, .1f, .1f);
        runner.setColors(m->colorsFor(red));
        std::vector<route3d::GeoPoint> geo;
        const RlvRoute standard = runroutes::standardRoute();
        for (const auto &p : standard.points())
            geo.push_back({ p.lat, p.lon, p.elevation });
        const route3d::Route route = route3d::Route::fromGeo(geo);
        const auto a = runner.build(route, 50.0, 11.0, 0.0);
        const auto &b = runner.build(route, 50.0, 11.0, M_PI);
        QCOMPARE(a.size(), b.size());
        float moved = 0;
        for (size_t i = 0; i < a.size(); ++i)
            moved = std::max(moved, (a[i].position - b[i].position).length());
        QVERIFY(moved > .2f);
        // it bounces: up and down with each step, like the classic runner
        {
            std::vector<route3d::GhostVertex> v;
            float lo = 1e9f, hi = -1e9f;
            for (int s = 0; s < 48; ++s) {
                m->pose(s / 48.0, v);
                float top = -1e9f;
                for (const auto &x : v)
                    top = std::max(top, x.position.y());
                lo = std::min(lo, top);
                hi = std::max(hi, top);
            }
            qInfo("  bounce %.1f cm", (hi - lo) * 100);
            QVERIFY(hi - lo > .03f && hi - lo < .12f);
        }
        for (uint32_t i : runner.indices())
            QVERIFY(i < b.size());
        // the profile's preview draws it
        qputenv(id == QLatin1String("male") ? "WATT_RUNNER_MALE_GLB" : "WATT_RUNNER_FEMALE_GLB", path.toUtf8());
        qputenv("WATT_RUNNER_GLB", WATT_SOURCE_DIR "/assets/models/runner.glb");   // (the motion: no app resources here)
        qputenv("WATT_WALKER_GLB", WATT_SOURCE_DIR "/assets/models/walker.glb");
        watt::AvatarPreviewItem preview;
        preview.setKind(QStringLiteral("runner"));
        QVERIFY(!preview.helmet());
        const int side = qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") > 0 ? qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") : 320;
        preview.setSize(QSizeF(side, side * 15 / 16));
        const QString dump = qEnvironmentVariable("WATT_AVATAR_DUMP");
        int k = 0;
        for (const QVariantMap &v : { QVariantMap { { "model", id } },
                                      QVariantMap { { "model", id }, { "jersey", "#d0202a" }, { "shorts", "#1e3a8a" },
                                                    { "shoes", "#f5c400" }, { "hair", "#e6c27a" } } }) {
            waitForFigure(v, watt::RiderAvatar::Runner);
            preview.setAvatar(v);
            for (double yaw : { 35.0, 0.0, 200.0 }) {
                preview.setYaw(yaw);
                QImage img(side, side * 15 / 16, QImage::Format_ARGB32_Premultiplied);
                img.fill(Qt::white);
                QPainter painter(&img);
                preview.paint(&painter);
                painter.end();
                if (!dump.isEmpty())
                    img.save(QStringLiteral("%1/runner_%2_%3.png").arg(dump, id).arg(k));
                ++k;
            }
        }
        QVERIFY(!preview.ownColors().isEmpty());
        QVERIFY(watt::RiderAvatar::fromVariant({ { "model", id } }, watt::RiderAvatar::Runner).figure()->retargeted());
    }

    // The textured avatars (rider_male.glb, rider_female.glb): their own clothes
    // from the texture, the jersey / shorts / helmet / shoes found and recoloured.
    void glbAvatarModel_data()
    {
        QTest::addColumn<QString>("id");
        QTest::newRow("male") << QStringLiteral("male");
        QTest::newRow("female") << QStringLiteral("female");
    }
    void glbAvatarModel()
    {
        QFETCH(QString, id);
        const QString path = QStringLiteral(WATT_SOURCE_DIR "/assets/models/rider_%1.glb").arg(id);
        if (!QFile::exists(path))
            QSKIP("the model is not there");
        using route3d::GlbModel;
        auto m = std::make_shared<GlbModel>();
        QString error;
        QVERIFY2(m->loadFile(path, &error, GlbModel::Outfit::Avatar), qPrintable(error));
        QVERIFY(m->canRide());
        QVERIFY(m->textured());
        size_t count[GlbModel::GarmentCount] = {};
        for (uint8_t g : m->garments())
            ++count[g];
        qInfo("rider_%s.glb: %zu vertices; jersey %zu, shorts %zu, helmet %zu, shoes %zu, hair %zu, other %zu", qPrintable(id),
              m->vertexCount(), count[GlbModel::Jersey], count[GlbModel::Shorts], count[GlbModel::Helmet], count[GlbModel::Shoes],
              count[GlbModel::Hair],
              count[GlbModel::NoGarment]);
        for (int g = GlbModel::Jersey; g < GlbModel::GarmentCount; ++g) {
            const QVector3D c = m->garmentColor(GlbModel::Garment(g));
            qInfo("  garment %d main colour %.2f %.2f %.2f, %zu vertices", g, c.x(), c.y(), c.z(), count[g]);
            if (g == GlbModel::Hair)
                continue;   // (short hair can hide under the helmet)
            QVERIFY2(c.x() >= 0, "a garment was not found in the texture");
            QVERIFY(count[g] > 200);
        }
        // the blue jersey and helmet, the black shorts
        QVERIFY(m->garmentColor(GlbModel::Jersey).z() > m->garmentColor(GlbModel::Jersey).x() + .15f);
        QVERIFY(m->garmentColor(GlbModel::Helmet).z() > m->garmentColor(GlbModel::Helmet).x() + .15f);
        QVERIFY(m->garmentColor(GlbModel::Shorts).length() < .5f);
        // no colours chosen: the texture's; a red jersey: red where the jersey is, the rest unchanged
        GlbModel::GarmentColors none;
        QVERIFY(m->colorsFor(none).empty());   // (nothing to change: drawn in its own)
        const auto &own = m->ownColors();
        QVERIFY(own.size() >= m->garments().size());
        GlbModel::GarmentColors red;
        red.color[GlbModel::Jersey] = QVector3D(.85f, .1f, .1f);
        const auto recoloured = m->colorsFor(red);
        QCOMPARE(recoloured.size(), own.size());
        for (size_t i = 0; i < m->garments().size(); ++i) {
            if (m->garments()[i] == GlbModel::Jersey)
                QVERIFY(recoloured[i].x() > recoloured[i].z());
            else
                QCOMPARE(recoloured[i], own[i]);
        }
        // on the bike: the colours given are drawn
        route3d::GhostRider rider;
        QVERIFY(rider.setRiderModel(m));
        rider.setRiderColors(recoloured);
        std::vector<route3d::GeoPoint> geo;
        const RlvRoute standard = runroutes::standardRoute();
        for (const auto &p : standard.points())
            geo.push_back({ p.lat, p.lon, p.elevation });
        const route3d::Route route = route3d::Route::fromGeo(geo);
        const auto &v = rider.build(route, 50.0, 30.0, 0.0);
        for (uint32_t i : rider.indices())
            QVERIFY(i < v.size());
        for (const auto &x : v)
            QVERIFY(std::isfinite(x.position.x()) && std::isfinite(x.position.y()) && std::isfinite(x.color.x()));
        // seated still: the hips do not bob with the pedal stroke (the
        // body's top - the helmet - stays within a few millimetres)
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 16; ++k) {
            const auto &w = rider.build(route, 50.0, 30.0, k * M_PI / 8);
            float topY = -1e9f;
            for (const auto &x : w)
                topY = std::max(topY, x.position.y());
            lo = std::min(lo, topY);
            hi = std::max(hi, topY);
        }
        qInfo("  top of the rider over a pedal turn: %.1f mm range", (hi - lo) * 1000);
        {   // the saddle raised to the seat (and only an avatar's)
            route3d::GlbModel::BikeFit fit;
            fit.bottomBracket = QVector3D(0, .27f, -.08f);
            fit.saddleTop = QVector3D(0, .977f, -.27f);
            fit.hood[0] = QVector3D(-.205f, .985f, .44f);
            fit.hood[1] = QVector3D(.205f, .985f, .44f);
            const float raise = m->saddleRaise(fit);
            qInfo("  saddle raised %.1f mm", raise * 1000);
            QVERIFY(raise >= 0 && raise < .15f);
            QCOMPARE(m->saddleRaise(fit), raise);   // (measured once)
        }
        QVERIFY(hi - lo < .008f);
        // the profile's preview draws it (WATT_AVATAR_DUMP=<dir>: the pictures written there)
        qputenv(id == QLatin1String("male") ? "WATT_RIDER_MALE_GLB" : "WATT_RIDER_FEMALE_GLB", path.toUtf8());   // (no app resources in the test)
        watt::AvatarPreviewItem preview;
        const int side = qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") > 0 ? qEnvironmentVariableIntValue("WATT_AVATAR_SIZE") : 320;
        preview.setSize(QSizeF(side, side * 15 / 16));
        const QString dump = qEnvironmentVariable("WATT_AVATAR_DUMP");
        int k = 0;
        for (const QVariantMap &a : { QVariantMap { { "model", id } },
                                      QVariantMap { { "model", id }, { "jersey", "#d0202a" }, { "shorts", "#1e3a8a" },
                                                    { "helmet", "#ffffff" }, { "shoes", "#111111" }, { "bike", "#e8e8e8" } },
                                      QVariantMap { { "model", id }, { "jersey", "#f5c400" }, { "helmet", "#f5c400" }, { "hair", "#e6c27a" } } }) {
            waitForFigure(a, watt::RiderAvatar::Cyclist);
            preview.setAvatar(a);
            for (double yaw : { 35.0, 0.0, 200.0 }) {
                preview.setYaw(yaw);
                QImage img(side, side * 15 / 16, QImage::Format_ARGB32_Premultiplied);
                img.fill(Qt::white);
                QPainter painter(&img);
                preview.paint(&painter);
                painter.end();
                int drawn = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        drawn += img.pixel(x, y) != qRgb(255, 255, 255);
                QVERIFY(drawn > 5000 * side / 320 * side / 320);
                if (!dump.isEmpty())
                    img.save(QStringLiteral("%1/avatar_%2_%3.png").arg(dump, id).arg(k));
                ++k;
            }
        }
        QVERIFY(!preview.ownColors().isEmpty());
        // the bike's colour
        QVERIFY(preview.ownColors().contains(QStringLiteral("bike")));
        const auto redBike = watt::RiderAvatar::fromVariant({ { "model", id }, { "bike", "#cc0000" } });
        QVERIFY(redBike.bikeColor.x() > .7f && redBike.bikeColor.z() < .1f);
        route3d::GhostRider plain, painted;
        painted.setFrameColor(redBike.bikeColor);
        const auto a = plain.build(route, 50.0, 30.0, 0.0);
        const auto &b = painted.build(route, 50.0, 30.0, 0.0);
        QCOMPARE(a.size(), b.size());
        int changed = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].color != b[i].color) {
                QVERIFY(b[i].color.x() > .7f);
                ++changed;
            }
        QVERIFY(changed > 100);
    }
};

QTEST_MAIN(TestTreadmill)
#include "tst_treadmill.moc"
