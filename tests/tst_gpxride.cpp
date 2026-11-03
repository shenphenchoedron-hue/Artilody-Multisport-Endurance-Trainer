// GPX Ride: RlvSession / RlvLibrary with Kind::Gpx (plain .gpx tracks without
// a video, own settings, "GPX: <route>" rides, the same physics as the video
// ride) and the 3D view's route building / distance mapping (Route3DItem).
#include "Climb.h"
#include "Database.h"
#include "GpxWorkshop.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvSession.h"
#include "Route3DItem.h"
#include "SensorHub.h"
#include "TrainingSession.h"
#include "VirtualGears.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <functional>
#include <memory>
#include <tuple>

using namespace watt;

namespace {

const double kMPerDegLat = RlvRoute::haversine(0, 0, 1, 0);

// A plain GPX track (no time, no video): `points` points `step` m apart
// heading north-east, grade in %.
bool writeGpx(const QString &path, int points, double step, double grade, bool withVideo = false)
{
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><name>t</name><trkseg>\n";
    const double mPerDegLon = kMPerDegLat * std::cos(55.0 * M_PI / 180.0);
    for (int i = 0; i < points; ++i) {
        const double d = i * step;
        const double bend = 40.0 * std::sin(d / 300.0);   // gentle bends
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"%2\"><ele>%3</ele></trkpt>\n")
                   .arg(55.0 + d / kMPerDegLat, 0, 'f', 8)
                   .arg(12.0 + bend / mPerDegLon, 0, 'f', 8)
                   .arg(20.0 + d * grade / 100.0, 0, 'f', 2)
                   .toUtf8();
    }
    gpx += "</trkseg></trk></gpx>\n";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(gpx);
    if (withVideo) {
        QFile v(QFileInfo(path).path() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".mp4"));
        if (!v.open(QIODevice::WriteOnly))
            return false;
        v.write("not a real video");
    }
    return true;
}

// A GPX track heading north: `points` points `step` m apart, elevation ele(d).
bool writeGpxProfile(const QString &path, int points, double step, const std::function<double(double)> &ele)
{
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><name>hill</name><trkseg>\n";
    for (int i = 0; i < points; ++i) {
        const double d = i * step;
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"12.0\"><ele>%2</ele></trkpt>\n")
                   .arg(55.0 + d / kMPerDegLat, 0, 'f', 8)
                   .arg(ele(d), 0, 'f', 2)
                   .toUtf8();
    }
    gpx += "</trkseg></trk></gpx>\n";
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(gpx) > 0;
}

// Writes sprints / KOMs into a GPX file as the GPX Workshop does.
bool addSegments(const QString &path, QVector<RouteSegment> list)
{
    RlvRoute r;
    if (!r.load(path) || !segments::validate(list, r.totalDistance()))
        return false;
    return segments::writeToGpx(path, list, {});
}
RouteSegment seg(RouteSegment::Kind kind, double start, double finish, const QString &name = QString())
{
    RouteSegment s;
    s.kind = kind;
    s.start = start;
    s.finish = finish;
    s.name = name;
    return s;
}

// 1 km flat, a 1 km climb at 5 %, 0.5 km flat, a 0.6 km climb at 6 %, 0.9 km down
double twoClimbs(double d)
{
    if (d < 1000)
        return 10;
    if (d < 2000)
        return 10 + (d - 1000) * 0.05;
    if (d < 2500)
        return 60;
    if (d < 3100)
        return 60 + (d - 2500) * 0.06;
    return std::max(0.0, 96 - (d - 3100) * 0.04);
}

constexpr double kSprintM = 200.0;

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<TrainingSession> training;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<RlvLibrary> videos;
    std::unique_ptr<RlvLibrary> routes;
    std::unique_ptr<RlvSession> rlv;
    std::unique_ptr<RlvSession> gpx;

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
        gears = std::make_unique<VirtualGears>(&db, profiles.get());
        gears->setManualSave(true);
        videos = std::make_unique<RlvLibrary>(&db);
        routes = std::make_unique<RlvLibrary>(&db, RlvLibrary::Kind::Gpx);
        routes->setLibraryFolder(dir.path());
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), videos.get());
        gpx = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), routes.get(),
                                           RlvSession::Kind::Gpx);
        for (RlvSession *s : { rlv.get(), gpx.get() }) {
            s->setManualClock(true);
            s->setMinimumSaveSeconds(5);
            s->setTrainingSession(training.get());
        }
        gears->setSessions(nullptr, rlv.get());
        gears->setGpxSession(gpx.get());
        return true;
    }
    void ride_for(RlvSession *s, int ms)
    {
        for (int t = 0; t < ms && s->running(); t += 250)
            s->advanceClock(250);
    }
};

} // namespace

class TestGpxRide : public QObject
{
    Q_OBJECT
private slots:
    void libraryFindsPlainGpxFiles()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(QDir(f.dir.path()).mkpath(QStringLiteral("sub")));
        QVERIFY(writeGpx(f.dir.filePath(QStringLiteral("a.gpx")), 200, 10, 2));
        QVERIFY(writeGpx(f.dir.filePath(QStringLiteral("sub/b.gpx")), 200, 10, 0, true));   // video ignored
        QFile junk(f.dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("x");
        junk.close();
        // the video library only lists gpx with a video, the GPX library every gpx
        QCOMPARE(RlvLibrary::findRoutes(f.dir.path(), RlvLibrary::Kind::Gpx).size(), 2);
        QCOMPARE(RlvLibrary::findRoutes(f.dir.path(), RlvLibrary::Kind::Video).size(), 1);
        f.routes->rescanNow();
        QCOMPARE(f.routes->count(), 2);
        // own settings: the video library keeps its folder
        QCOMPARE(f.routes->libraryFolder(), QDir::cleanPath(f.dir.path()));
        QVERIFY(f.videos->libraryFolder() != f.routes->libraryFolder());
        QVERIFY(RlvLibrary::defaultLibraryFolder(RlvLibrary::Kind::Gpx).endsWith(QStringLiteral("Artilody GPX Routes")));
        RlvLibrary again(&f.db, RlvLibrary::Kind::Gpx);
        QCOMPARE(again.libraryFolder(), f.routes->libraryFolder());
    }

    void sessionRidesGpxWithoutVideo()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("climb.gpx"));
        QVERIFY(writeGpx(path, 60, 10, 4));
        QCOMPARE(f.gpx->kindName(), QStringLiteral("gpx"));
        QVERIFY(f.gpx->selectPath(path));
        QVERIFY(f.gpx->hasRoute());
        QVERIFY(!f.gpx->videoAvailable());
        QVERIFY(f.gpx->videoUrl().isEmpty());
        QCOMPARE(f.gpx->routeDurationSec(), 0);
        QVERIFY(f.gpx->lastError().isEmpty());
        // a non-gpx file is refused
        QVERIFY(!f.gpx->selectPath(f.dir.filePath(QStringLiteral("x.xml"))));

        f.hub->updateCadence(90);
        f.hub->updatePower(220);
        QVERIFY(f.gpx->start());
        f.ride_for(f.gpx.get(), 20000);
        QVERIFY(f.gpx->distanceMeters() > 50);
        QVERIFY(f.gpx->gradePercent() > 2);
        // the video ride cannot start meanwhile, and the GPX ride blocks the workout
        QVERIFY(!f.rlv->start());
        f.training->start();
        QVERIFY(!f.training->active());
        const qint64 id = f.gpx->finish(true);
        QVERIFY(id >= 0);
        QCOMPARE(f.db.ride(id)->workoutName, QStringLiteral("GPX: climb"));
        // last route is remembered under gpx/ (not rlv/)
        QCOMPARE(f.db.setting(QStringLiteral("gpx/lastRoute")).toString(), QDir::cleanPath(path));
        QVERIFY(f.db.setting(QStringLiteral("rlv/lastRoute")).toString().isEmpty());
    }

    void sprintSegmentAndCrossing()
    {
        // the line crossed a quarter into a step from 490 m at 1000 ms to 530 m at 1400 ms
        QCOMPARE(sprint::crossingMs(490, 530, 1000, 1400, 500), 1100.0);
        QVERIFY(std::isnan(sprint::crossingMs(500, 530, 1000, 1400, 500)));   // already on it
        QVERIFY(std::isnan(sprint::crossingMs(480, 499, 1000, 1400, 500)));
    }

    void sprintTimedSavedAndCompared()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("flat.gpx"));
        QVERIFY(writeGpx(path, 161, 10, 0));   // 1.6 km
        QVERIFY(f.gpx->selectPath(path));
        QVERIFY(!f.gpx->sprintAvailable());   // none placed: none
        QVERIFY(addSegments(path, { seg(RouteSegment::Sprint, 400, 600) }));
        QVERIFY(f.gpx->selectPath(path));
        QVERIFY(f.gpx->sprintAvailable());
        QCOMPARE(f.gpx->sprintStartM(), 400.0);
        QCOMPARE(f.gpx->sprintFinishM() - f.gpx->sprintStartM(), kSprintM);
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("ahead"));
        QCOMPARE(f.gpx->sprintBestSec(), 0.0);
        QVERIFY(!f.gpx->sprintKey().isEmpty());

        auto rideSprint = [&](int watts) {
            QSignalSpy done(f.gpx.get(), &RlvSession::sprintFinished);
            f.hub->updateCadence(90);
            f.hub->updatePower(watts);
            if (!f.gpx->start())
                return -1.0;
            bool sawRunning = false;
            double speedInSprint = 0;
            int steps = 0;
            while (f.gpx->running() && f.gpx->sprintState() != QStringLiteral("done") && steps++ < 4000) {
                f.gpx->advanceClock(250);
                if (f.gpx->sprintState() == QStringLiteral("running")) {
                    sawRunning = true;
                    speedInSprint = f.gpx->speedMps();
                }
            }
            if (!sawRunning || done.count() != 1)
                return -1.0;
            // the time matches the ride's speed through the sprint
            const double t = f.gpx->sprintTimeSec();
            if (std::abs(t - kSprintM / speedInSprint) > 1.0)
                return -2.0;
            f.ride_for(f.gpx.get(), 6000);
            const qint64 ride = f.gpx->finish(true);
            return ride >= 0 ? t : -3.0;
        };

        const double first = rideSprint(320);
        QVERIFY(first > 0);
        QVERIFY(f.gpx->sprintNewBest());
        QCOMPARE(f.gpx->sprintPreviousBestSec(), 0.0);
        QCOMPARE(f.gpx->sprintBestSec(), first);
        QCOMPARE(f.gpx->sprintAttempts(), 1);
        const auto best = f.db.bestSegment(f.profiles->profileId(), QStringLiteral("sprint"), f.gpx->sprintKey(), 400, kSprintM);
        QVERIFY(best.has_value());
        QCOMPARE(best->timeSec, first);
        QCOMPARE(best->rideId, f.gpx->lastRideId());   // linked to the saved ride

        // a slower sprint: kept, compared, not the best
        QVERIFY(f.gpx->seekToDistance(0));
        const double second = rideSprint(150);
        QVERIFY(second > first);
        QVERIFY(!f.gpx->sprintNewBest());
        QCOMPARE(f.gpx->sprintPreviousBestSec(), first);
        QCOMPARE(f.gpx->sprintBestSec(), first);
        QCOMPARE(f.gpx->sprintAttempts(), 2);

        // a faster sprint in a discarded ride: deleted again, the best stays
        {
            QVERIFY(f.gpx->seekToDistance(0));
            f.hub->updateCadence(90);
            f.hub->updatePower(450);
            QVERIFY(f.gpx->start());
            int steps = 0;
            while (f.gpx->sprintState() != QStringLiteral("done") && steps++ < 4000)
                f.gpx->advanceClock(250);
            QCOMPARE(f.gpx->sprintState(), QStringLiteral("done"));
            QVERIFY(f.gpx->sprintTimeSec() < first);
            QCOMPARE(f.gpx->sprintAttempts(), 3);
            f.gpx->finish(false);
            QCOMPARE(f.gpx->sprintBestSec(), first);
            QCOMPARE(f.gpx->sprintAttempts(), 2);
            QCOMPARE(f.gpx->sprintState(), QStringLiteral("void"));
            QCOMPARE(f.db.segmentAttempts(f.profiles->profileId(), QStringLiteral("sprint"), f.gpx->sprintKey(),
                                          400, kSprintM), 2);
        }
        // the same track under another name and folder is the same route
        QVERIFY(QDir(f.dir.path()).mkpath(QStringLiteral("moved")));
        const QString moved = f.dir.filePath(QStringLiteral("moved/renamed.gpx"));
        QVERIFY(QFile::copy(path, moved));
        const QString key = f.gpx->sprintKey();
        QVERIFY(f.gpx->selectPath(moved));
        QCOMPARE(f.gpx->sprintKey(), key);
        QCOMPARE(f.gpx->sprintBestSec(), first);
        QCOMPARE(f.gpx->sprintAttempts(), 2);
        // an edited track is a new route
        QVERIFY(writeGpx(moved, 161, 10, 1));
        QVERIFY(addSegments(moved, { seg(RouteSegment::Sprint, 400, 600) }));
        QVERIFY(f.gpx->selectPath(moved));
        QVERIFY(f.gpx->sprintKey() != key);
        QCOMPARE(f.gpx->sprintBestSec(), 0.0);
    }

    void sprintVoidedPassedOrAbsent()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("flat.gpx"));
        QVERIFY(writeGpx(path, 161, 10, 0, true));
        QVERIFY(addSegments(path, { seg(RouteSegment::Sprint, 400, 600) }));
        QVERIFY(f.gpx->selectPath(path));
        f.hub->updateCadence(90);
        f.hub->updatePower(300);
        // a pause during the sprint voids it: nothing saved
        QVERIFY(f.gpx->start());
        int steps = 0;
        while (f.gpx->sprintState() != QStringLiteral("running") && steps++ < 4000)
            f.gpx->advanceClock(250);
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("running"));
        f.gpx->pause();
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("void"));
        f.gpx->resume();
        f.ride_for(f.gpx.get(), 40000);
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("void"));
        f.gpx->finish(false);
        QCOMPARE(f.db.segmentAttempts(f.profiles->profileId(), QStringLiteral("sprint"), f.gpx->sprintKey(), 400, kSprintM), 0);
        // starting beyond the start line: no sprint this ride
        QVERIFY(f.gpx->seekToDistance(f.gpx->sprintStartM() + 10));
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("passed"));
        QVERIFY(f.gpx->start());
        f.ride_for(f.gpx.get(), 30000);
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("passed"));
        f.gpx->finish(false);
        // the video ride has no sprint, nor has a short route
        QVERIFY(f.rlv->selectPath(path));
        QVERIFY(!f.rlv->sprintAvailable());
        QCOMPARE(f.rlv->sprintState(), QStringLiteral("none"));
        const QString shortPath = f.dir.filePath(QStringLiteral("short.gpx"));
        QVERIFY(writeGpx(shortPath, 90, 10, 0));   // no segments placed
        QVERIFY(f.gpx->selectPath(shortPath));
        QVERIFY(!f.gpx->sprintAvailable());
        QCOMPARE(f.gpx->sprintState(), QStringLiteral("none"));
    }

    void komIsTheLongestClimb()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("hill.gpx"));
        QVERIFY(writeGpxProfile(path, 401, 10, twoClimbs));
        RlvRoute route;
        QVERIFY(route.load(path));
        // the 0.5 km flat between the climbs belongs to one climb (shorter than kMaxFlatM)
        const climb::Climb c = climb::longest(route);
        QVERIFY(c.valid);
        QVERIFY2(std::abs(c.start - 1000) < 120, qPrintable(QString::number(c.start)));
        QVERIFY2(std::abs(c.finish - 3100) < 120, qPrintable(QString::number(c.finish)));
        QVERIFY(std::abs(c.gainM - 86) < 8);
        // flat: no KOM
        const QString flat = f.dir.filePath(QStringLiteral("flat.gpx"));
        QVERIFY(writeGpxProfile(flat, 301, 10, [](double) { return 5.0; }));
        RlvRoute flatRoute;
        QVERIFY(flatRoute.load(flat));
        QVERIFY(!climb::longest(flatRoute).valid);
        // a long flat ends a climb: two separate climbs, the longer wins
        const QString split = f.dir.filePath(QStringLiteral("split.gpx"));
        QVERIFY(writeGpxProfile(split, 501, 10, [](double d) {
            if (d < 500) return 0.0;
            if (d < 1100) return (d - 500) * 0.05;                 // 600 m
            if (d < 2500) return 30.0;                             // 1.4 km flat
            if (d < 3500) return 30 + (d - 2500) * 0.04;           // 1000 m: the KOM
            return 70.0;
        }));
        RlvRoute splitRoute;
        QVERIFY(splitRoute.load(split));
        const climb::Climb k = climb::longest(splitRoute);
        QVERIFY(k.valid);
        QVERIFY2(std::abs(k.start - 2500) < 120, qPrintable(QString::number(k.start)));
        QVERIFY2(std::abs(k.finish - 3500) < 120, qPrintable(QString::number(k.finish)));
        // the longest climb of at least 2.5 %: a long gentle drag does not count
        const QString gentle = f.dir.filePath(QStringLiteral("gentle.gpx"));
        QVERIFY(writeGpxProfile(gentle, 501, 10, [](double d) {
            if (d < 500) return 0.0;
            if (d < 800) return (d - 500) * 0.08;                  // 300 m at 8 %: 24 m
            if (d < 1500) return std::max(0.0, 24 - (d - 800) * 0.04);
            if (d < 3500) return (d - 1500) * 0.003;               // 2 km at 0.3 %: too gentle
            return 6.0;
        }));
        RlvRoute gentleRoute;
        QVERIFY(gentleRoute.load(gentle));
        const climb::Climb g = climb::longest(gentleRoute);
        QVERIFY(g.valid);
        QVERIFY2(std::abs(g.start - 500) < 60 && std::abs(g.finish - 800) < 60, qPrintable(QString::number(g.start)));
        QVERIFY(g.gradePercent() > 6);
    }

    void komTimedSavedAndCompared()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("hill.gpx"));
        QVERIFY(writeGpxProfile(path, 401, 10, twoClimbs));
        QVERIFY(addSegments(path, { seg(RouteSegment::Kom, 1000, 2000, QStringLiteral("The wall")) }));
        QVERIFY(f.gpx->selectPath(path));
        QVERIFY(f.gpx->komAvailable());
        QCOMPARE(f.gpx->komName(), QStringLiteral("The wall"));
        QCOMPARE(f.gpx->komState(), QStringLiteral("ahead"));
        QVERIFY(f.gpx->komGradePercent() > 3);
        auto rideKom = [&](int watts) {
            QSignalSpy done(f.gpx.get(), &RlvSession::komFinished);
            f.hub->updateCadence(90);
            f.hub->updatePower(watts);
            if (!f.gpx->start())
                return -1.0;
            int steps = 0;
            while (f.gpx->running() && f.gpx->komState() != QStringLiteral("done") && steps++ < 20000)
                f.gpx->advanceClock(250);
            if (done.count() != 1)
                return -1.0;
            const double t = f.gpx->komTimeSec();
            f.ride_for(f.gpx.get(), 6000);
            return f.gpx->finish(true) >= 0 ? t : -3.0;
        };
        const double first = rideKom(300);
        QVERIFY(first > 60);
        QVERIFY(f.gpx->komNewBest());
        QCOMPARE(f.gpx->komBestSec(), first);
        const auto best = f.db.bestSegment(f.profiles->profileId(), QStringLiteral("kom"), f.gpx->komKey(),
                                           f.gpx->komStartM(), f.gpx->komLengthM());
        QVERIFY(best.has_value());
        QCOMPARE(best->timeSec, first);
        QCOMPARE(best->rideId, f.gpx->lastRideId());
        // the sprint (on the same route) is kept apart
        QCOMPARE(f.db.segmentAttempts(f.profiles->profileId(), QStringLiteral("kom"), f.gpx->komKey(),
                                      f.gpx->komStartM(), f.gpx->komLengthM()), 1);
        QVERIFY(f.gpx->seekToDistance(0));
        const double second = rideKom(200);
        QVERIFY(second > first);
        QVERIFY(!f.gpx->komNewBest());
        QCOMPARE(f.gpx->komPreviousBestSec(), first);
        QCOMPARE(f.gpx->komAttempts(), 2);
        // no KOM on the video ride
        QVERIFY(f.rlv->selectPath(path) || !f.rlv->hasRoute());
        QVERIFY(!f.rlv->komAvailable());
    }

    void segmentsReadWrittenAndValidated()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("s.gpx"));
        QVERIFY(writeGpx(path, 401, 10, 0));   // 4 km
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray before = in.readAll();
        in.close();
        // overlap / too short / outside: rejected
        QVector<RouteSegment> bad { seg(RouteSegment::Sprint, 100, 300), seg(RouteSegment::Kom, 250, 900) };
        QString error;
        QVERIFY(!segments::validate(bad, 4000, &error));
        QVERIFY(error.contains(QStringLiteral("overlaps")));
        QVector<RouteSegment> tiny { seg(RouteSegment::Sprint, 100, 120) };
        QVERIFY(!segments::validate(tiny, 4000));
        QVector<RouteSegment> out { seg(RouteSegment::Kom, 3900, 4200) };
        QVERIFY(!segments::validate(out, 4000));
        // written, read back sorted, the track untouched
        QVERIFY(addSegments(path, { seg(RouteSegment::Kom, 2000, 3100, QStringLiteral("Hill & dale")),
                                    seg(RouteSegment::Sprint, 500, 700) }));
        RlvRoute r;
        QVERIFY(r.load(path));
        QCOMPARE(r.segments().size(), 2);
        QCOMPARE(r.segments().at(0).kind, RouteSegment::Sprint);
        QCOMPARE(r.segments().at(1).name, QStringLiteral("Hill & dale"));
        QCOMPARE(r.segments().at(1).finish, 3100.0);
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        const QByteArray now = after.readAll();
        QVERIFY(now.startsWith(before.left(before.lastIndexOf("</gpx>"))));   // the track as it was
        // written again: replaced, not added; an empty list removes them
        QVERIFY(addSegments(path, { seg(RouteSegment::Sprint, 1000, 1300) }));
        QVERIFY(r.load(path));
        QCOMPARE(r.segments().size(), 1);
        QVERIFY(segments::writeToGpx(path, {}, {}));
        QVERIFY(r.load(path));
        QVERIFY(r.segments().isEmpty());
        QFile clean(path);
        QVERIFY(clean.open(QIODevice::ReadOnly));
        QCOMPARE(clean.readAll(), before);
        // a file with its own root <extensions>: ours go inside it, and out again
        QByteArray own = before;
        own.replace("</gpx>", "<extensions>\n<x:y xmlns:x=\"urn:x\">1</x:y>\n</extensions>\n</gpx>");
        QFile w(path);
        QVERIFY(w.open(QIODevice::WriteOnly));
        w.write(own);
        w.close();
        QVERIFY(addSegments(path, { seg(RouteSegment::Sprint, 1000, 1300) }));
        QVERIFY(r.load(path));
        QCOMPARE(r.segments().size(), 1);
        QFile o2(path);
        QVERIFY(o2.open(QIODevice::ReadOnly));
        const QByteArray withOurs = o2.readAll();
        o2.close();
        QCOMPARE(withOurs.count("<extensions>"), own.count("<extensions>"));
        QVERIFY(segments::writeToGpx(path, {}, {}));
        QFile o3(path);
        QVERIFY(o3.open(QIODevice::ReadOnly));
        QCOMPARE(o3.readAll(), own);
    }

    void segmentsKeepTheFileEncoding()
    {
        // an ISO-8859-1 GPX with "ø" / "é" in its name: written byte for byte, the name escaped
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("latin.gpx"));
        QByteArray g = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                       "<trk><name>S\xf8 rundt caf\xe9</name><trkseg>\n";
        for (int i = 0; i <= 200; ++i)
            g += QStringLiteral("<trkpt lat=\"%1\" lon=\"12.0\"><ele>10</ele></trkpt>\n").arg(55.0 + i * 10.0 / kMPerDegLat, 0, 'f', 8).toLatin1();
        g += "</trkseg></trk></gpx>\n";
        {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(g);
        }
        QVERIFY(addSegments(path, { seg(RouteSegment::Kom, 200, 900, QStringLiteral("Bakken \u00f8st")) }));
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray now = in.readAll();
        in.close();
        QVERIFY(now.startsWith(g.left(g.lastIndexOf("</gpx>"))));   // the track's bytes as they were
        QVERIFY(now.contains("Bakken &#xf8;st"));
        RlvRoute r;
        QVERIFY(r.load(path));
        QCOMPARE(r.title(), QStringLiteral("S\u00f8 rundt caf\u00e9"));
        QCOMPARE(r.segments().first().name, QStringLiteral("Bakken \u00f8st"));
        QVERIFY(segments::writeToGpx(path, {}, {}));
        QVERIFY(in.open(QIODevice::ReadOnly));
        QCOMPARE(in.readAll(), g);
    }

    void severalSegmentsAndTheWorkshop()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("hill.gpx"));
        QVERIFY(writeGpxProfile(path, 401, 10, twoClimbs));   // climbs at 1..2 km and 2.5..3.1 km
        GpxWorkshop ws(nullptr, f.gpx.get());
        QVERIFY(ws.open(path));
        QVERIFY(ws.segments().isEmpty());
        QCOMPARE(ws.addSprint(200, 200), QString());
        QVERIFY(!ws.addSprint(300, 200).isEmpty());   // overlaps the first
        QCOMPARE(ws.addKom(1000, 2000, QStringLiteral("Wall")), QString());
        QCOMPARE(ws.addKom(2500, 3100), QString());
        QCOMPARE(ws.addSprint(3300, 150), QString());
        QCOMPARE(ws.sprintCount(), 2);
        QCOMPARE(ws.komCount(), 2);
        QVERIFY(ws.dirty());
        QVERIFY(!ws.addSprint(3950, 200).isEmpty());   // past the end
        QCOMPARE(ws.setSegment(3, 3300, 3500, QStringLiteral("Last")), QString());
        QCOMPARE(ws.save(), QString());
        QVERIFY(!ws.dirty());
        // GPX Ride: it reloaded the selected route? (not selected: select it)
        QVERIFY(f.gpx->selectPath(path));
        QCOMPARE(f.gpx->sprintCount(), 2);
        QCOMPARE(f.gpx->komCount(), 2);
        QCOMPARE(f.gpx->sprintStartM(), 200.0);   // the next ahead
        QCOMPARE(f.gpx->komName(), QStringLiteral("Wall"));
        // ride it all: both sprints and both KOMs timed, focus moving on
        QSignalSpy sprints(f.gpx.get(), &RlvSession::sprintFinished), koms(f.gpx.get(), &RlvSession::komFinished);
        f.hub->updateCadence(90);
        f.hub->updatePower(320);
        QVERIFY(f.gpx->start());
        int steps = 0;
        while (f.gpx->running() && steps++ < 40000)
            f.gpx->advanceClock(250);
        QCOMPARE(sprints.count(), 2);
        QCOMPARE(koms.count(), 2);
        // each saved on its own (kind, start, length)
        for (const auto &[kind, start, length] :
             { std::tuple { "sprint", 200.0, 200.0 }, { "sprint", 3300.0, 200.0 }, { "kom", 1000.0, 1000.0 }, { "kom", 2500.0, 600.0 } })
            QCOMPARE(f.db.segmentAttempts(f.profiles->profileId(), QString::fromLatin1(kind), f.gpx->segmentKey(), start, length), 1);
        // suggest: the longest climb, when free
        QVERIFY(ws.open(path));
        QVERIFY(!ws.suggestKom().isEmpty());   // overlaps the KOMs placed
        ws.removeSegment(1);
        ws.removeSegment(1);
        QCOMPARE(ws.komCount(), 0);
        QCOMPARE(ws.suggestKom(), QString());
        QCOMPARE(ws.komCount(), 1);
        ws.revert();
        QCOMPARE(ws.komCount(), 2);
    }

    void elevationSpikesAndSteepStepsCleaned()
    {
        Fixture f;
        QVERIFY(f.init());
        // a 6 % climb with a single 25 m spike and a 30 m jump over 40 m at 2 km (a hairpin)
        const QString path = f.dir.filePath(QStringLiteral("alp.gpx"));
        QVERIFY(writeGpxProfile(path, 401, 10, [](double d) {
            double e = 100 + d * 0.06;
            if (std::abs(d - 1000) < 1) e += 25;              // spike
            if (d >= 2000) e += 30 * std::min(1.0, (d - 2000) / 40.0);   // 75 % over 40 m
            return e;
        }));
        RlvRoute r;
        QVERIFY(r.load(path));
        QVERIFY(r.rawMaxGrade() > 60);
        QVERIFY(r.spikesRemoved() >= 1);
        QVERIFY(r.stepsLimited() > 0);
        QVERIFY2(r.steepestGrade() < RlvRoute::kMaxCleanGrade + 1.5, qPrintable(QString::number(r.steepestGrade())));
        QVERIFY(std::abs(r.elevationAtDistance(1000) - (100 + 60)) < 2);    // the spike is gone
        // the height is kept: the summit as recorded
        QVERIFY(std::abs(r.elevationAtDistance(3900) - (100 + 234 + 30)) < 2);
        // the route's own limit: saved in the file, read back
        GpxWorkshop ws(nullptr, f.gpx.get());
        QVERIFY(ws.open(path));
        QCOMPARE(ws.maxGrade(), RlvRoute::kMaxCleanGrade);
        ws.setMaxGrade(12);
        QVERIFY(ws.dirty());
        QVERIFY(ws.steepestGrade() < 13.5);
        QCOMPARE(ws.save(), QString());
        RlvRoute again;
        QVERIFY(again.load(path));
        QCOMPARE(again.maxCleanGrade(), 12.0);
        QVERIFY(again.steepestGrade() < 13.5);
        QCOMPARE(sprint::routeKey(again), sprint::routeKey(r));   // the track is the same route
        // a flat, clean route is not touched
        const QString flat = f.dir.filePath(QStringLiteral("flat.gpx"));
        QVERIFY(writeGpx(flat, 300, 10, 3));
        RlvRoute plain;
        QVERIFY(plain.load(flat));
        QCOMPARE(plain.spikesRemoved(), 0);
        QCOMPARE(plain.stepsLimited(), 0);
    }

    void sameSpeedAsTheVideoRide()
    {
        // the physics is shared: the same route, power and clock give the same distance
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("roll.gpx"));
        QVERIFY(writeGpx(path, 300, 10, 1.5, true));
        QVERIFY(f.gpx->selectPath(path));
        QVERIFY(f.rlv->selectPath(path));
        f.hub->updateCadence(90);
        f.hub->updatePower(200);
        QVERIFY(f.gpx->start());
        f.ride_for(f.gpx.get(), 30000);
        const double gpxDistance = f.gpx->distanceMeters();
        f.gpx->finish(false);
        QVERIFY(f.rlv->start());
        f.ride_for(f.rlv.get(), 30000);
        QVERIFY(std::abs(f.rlv->distanceMeters() - gpxDistance) < 0.5);
        f.rlv->finish(false);
    }

    void settingsAreSeparate()
    {
        Fixture f;
        QVERIFY(f.init());
        f.gpx->setDifficulty(40);
        QCOMPARE(f.db.setting(QStringLiteral("gpx/difficulty")).toInt(), 40);
        QVERIFY(f.rlv->difficultyPercent() != 40);
        f.gpx->setMode(QStringLiteral("erg"));
        QCOMPARE(f.gpx->mode(), QStringLiteral("erg"));
        QCOMPARE(f.rlv->mode(), QStringLiteral("sim"));
    }

    void clickRoutesToTheGpxPage()
    {
        Fixture f;
        QVERIFY(f.init());
        const QString path = f.dir.filePath(QStringLiteral("r.gpx"));
        QVERIFY(writeGpx(path, 100, 10, 0));
        QVERIFY(f.gpx->selectPath(path));
        f.gpx->setMode(QStringLiteral("erg"));
        f.gears->setActivePage(QStringLiteral("gpx"));
        const int before = f.gpx->targetWatts();
        f.gears->controllerShift(1);
        QCOMPARE(f.gpx->targetWatts(), before + 10);
    }

    void route3dMapsTheAppDistance()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("m.gpx"));
        QVERIFY(writeGpx(path, 400, 10, 3));
        RlvRoute route;
        QVERIFY(route.load(path));
        const Route3DItem::Built b = Route3DItem::build(route);
        QVERIFY(b.route);
        QCOMPARE(b.appDistance.size(), route.points().size());
        // the drawn road is about as long as the track (smoothing cuts bends a little)
        QVERIFY(std::abs(b.route->finish() - route.totalDistance()) < route.totalDistance() * 0.02);
        QCOMPARE(Route3DItem::mapDistance(b, 0), 0.0);
        QVERIFY(std::abs(Route3DItem::mapDistance(b, route.totalDistance()) - b.route->finish()) < 1e-6);
        // the drawn road runs on ~50 m past the finish (a point-to-point route)
        QVERIFY(b.route->length() - b.route->finish() > 40.0);
        QVERIFY(b.route->length() - b.route->finish() < 60.0);
        double last = -1;
        for (double d = 0; d <= route.totalDistance(); d += 37) {
            const double m = Route3DItem::mapDistance(b, d);
            QVERIFY(m >= last);
            last = m;
        }
        // the drawn height follows the app's elevation (same smoothed track)
        const double mid = route.totalDistance() / 2;
        QVERIFY(std::abs(b.route->at(Route3DItem::mapDistance(b, mid)).y - route.elevationAtDistance(mid)) < 1.0);
    }

    void route3dMappingIsLocalInBends()
    {
        // a sharp zig-zag first (the smoothing shortens it), then 2 km straight:
        // on the straight the drawn road must advance metre for metre
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("zig.gpx"));
        QByteArray gpx = "<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>\n";
        const double mPerDegLon = kMPerDegLat * std::cos(55.0 * M_PI / 180.0);
        double north = 0;
        for (int i = 0; i < 40; ++i, north += 10)   // 400 m zig-zag, 12 m to the side
            gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"%2\"><ele>10</ele></trkpt>\n")
                       .arg(55.0 + north / kMPerDegLat, 0, 'f', 8)
                       .arg(12.0 + (i % 2 ? 12.0 : 0.0) / mPerDegLon, 0, 'f', 8)
                       .toUtf8();
        for (int i = 0; i <= 200; ++i, north += 10)
            gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"12.0\"><ele>10</ele></trkpt>\n")
                       .arg(55.0 + north / kMPerDegLat, 0, 'f', 8)
                       .toUtf8();
        gpx += "</trkseg></trk></gpx>\n";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(gpx);
        f.close();
        RlvRoute route;
        QVERIFY(route.load(path));
        const Route3DItem::Built b = Route3DItem::build(route);
        const double total = route.totalDistance();
        const double a = Route3DItem::mapDistance(b, total - 1500), c = Route3DItem::mapDistance(b, total - 500);
        QVERIFY2(std::abs((c - a) - 1000.0) < 3.0, qPrintable(QString::number(c - a)));
    }

    void route3dOutAndBackIsOneRoad()
    {
        // out 2 km north, back 3.5 m east of it and 1.5 m higher (GPS): one road
        std::vector<route3d::GeoPoint> geo;
        const double mPerDegLon = kMPerDegLat * std::cos(55.0 * M_PI / 180.0);
        for (int i = 0; i <= 400; ++i)
            geo.push_back({ 55.0 + i * 5.0 / kMPerDegLat, 12.0, 20.0 + i * 0.01 });
        for (int i = 400; i >= 0; --i)
            geo.push_back({ 55.0 + i * 5.0 / kMPerDegLat, 12.0 + 3.5 / mPerDegLon, 21.5 + i * 0.01 });
        const route3d::Route r = route3d::Route::fromGeo(geo);
        QCOMPARE(r.shared.size(), r.points.size());
        const double half = r.length() / 2;
        int shared = 0, checked = 0;
        for (size_t i = 0; i < r.points.size(); ++i) {
            const auto &p = r.points[i];
            if (p.distance < half + 150 || p.distance > r.length() - 150)
                continue;   // the way back, away from the turn and the end
            ++checked;
            shared += r.isShared(i);
            QVERIFY2(std::abs(p.x) < 0.3, qPrintable(QString::number(p.x)));   // on the way out
            QVERIFY(std::abs(p.y - (20.0 + (-p.z) / 5.0 * 0.01)) < 0.3);       // at its height
        }
        QVERIFY(checked > 100);
        QCOMPARE(shared, checked);
        for (size_t i = 0; i < r.points.size() && r.points[i].distance < half - 10; ++i)
            QVERIFY(!r.isShared(i));   // the way out is drawn
        // no jump where the way back joins the way out (after the turnaround)
        for (size_t i = 1; i < r.points.size(); ++i) {
            const auto &a = r.points[i - 1], &b = r.points[i];
            if (a.distance < half)
                continue;
            QVERIFY2(std::abs(b.x - a.x) < 0.25 * (b.distance - a.distance) + 0.05,
                     qPrintable(QStringLiteral("lateral step %1 m at %2 m").arg(b.x - a.x).arg(b.distance)));
        }
        // a hairpin on a climb: the legs 5 m apart but 12 m apart in height stay two roads
        {
            std::vector<route3d::GeoPoint> climb;
            for (int i = 0; i <= 200; ++i)
                climb.push_back({ 55.0 + i * 5.0 / kMPerDegLat, 12.0, 20.0 + i * 0.06 });
            for (int i = 200; i >= 0; --i)
                climb.push_back({ 55.0 + i * 5.0 / kMPerDegLat, 12.0 + 5.0 / mPerDegLon, 80.0 + (200 - i) * 0.06 });
            const route3d::Route h = route3d::Route::fromGeo(climb);
            QVERIFY(std::none_of(h.shared.begin(), h.shared.end(), [](unsigned char c) { return c != 0; }));
        }
        // a road that never comes back shares nothing
        const route3d::Route demo = route3d::Route::demo();
        QVERIFY(std::none_of(demo.shared.begin(), demo.shared.end(), [](unsigned char c) { return c != 0; }));
    }

    void route3dRejectsHugeGaps()
    {
        // points thousands of km apart: refused before anything is filled in
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("far.gpx"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<?xml version=\"1.0\"?><gpx version=\"1.1\"><trk><trkseg>"
                "<trkpt lat=\"10\" lon=\"0\"><ele>1</ele></trkpt><trkpt lat=\"10\" lon=\"0.01\"><ele>1</ele></trkpt>"
                "<trkpt lat=\"10\" lon=\"170\"><ele>1</ele></trkpt><trkpt lat=\"10\" lon=\"0\"><ele>1</ele></trkpt>"
                "</trkseg></trk></gpx>");
        f.close();
        RlvRoute route;
        QVERIFY(route.load(path));
        bool threw = false;
        try {
            Route3DItem::build(route);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void route3dFillsSparseTracks()
    {
        // a route planner's track with points 1.5 km apart still draws
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sparse.gpx"));
        QVERIFY(writeGpx(path, 6, 1500, 1));
        RlvRoute route;
        QVERIFY(route.load(path));
        const Route3DItem::Built b = Route3DItem::build(route);
        QVERIFY(b.route->length() > 7000);
    }
};

QTEST_GUILESS_MAIN(TestGpxRide)
#include "tst_gpxride.moc"
