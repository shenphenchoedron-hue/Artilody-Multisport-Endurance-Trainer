// GPX Ride: RlvSession / RlvLibrary with Kind::Gpx (plain .gpx tracks without
// a video, own settings, "GPX: <route>" rides, the same physics as the video
// ride) and the 3D view's route building / distance mapping (Route3DItem).
#include "Database.h"
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
#include <memory>

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
