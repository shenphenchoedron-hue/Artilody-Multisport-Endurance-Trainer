// RlvRoute: gpx/xml parsing, distances, video time mapping, grade, ascent,
// information overlays, plus checks against the real example route when present.
#include "RlvLibrary.h"
#include "RlvRoute.h"

#include "Database.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace watt;

namespace {

// metres per degree latitude (haversine with the same radius)
const double kMPerDegLat = RlvRoute::haversine(0, 0, 1, 0);

struct SynthPoint
{
    double northM;      // distance travelled (straight north)
    double ele;
    double kmh = -1;    // gte:gps speed, -1 = omitted
};

QByteArray gpxFor(const QVector<SynthPoint> &pts, bool withTime = true)
{
    QByteArray out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
        "xmlns:gte=\"http://www.gpstrackeditor.com/xmlschemas/General/1\" "
        "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n"
        "<trk><name>Synthetic</name><trkseg>\n";
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("1999-12-31T23:00:00Z"), Qt::ISODate);
    for (int i = 0; i < pts.size(); ++i) {
        const double lat = 45.0 + pts.at(i).northM / kMPerDegLat;
        out += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.000000\"><ele>%2</ele>")
                   .arg(lat, 0, 'f', 8)
                   .arg(pts.at(i).ele, 0, 'f', 2)
                   .toUtf8();
        if (withTime)
            out += "<time>" + t0.addSecs(i).toString(Qt::ISODate).toUtf8() + "</time>";
        out += "<extensions>";
        if (pts.at(i).kmh >= 0)
            out += QStringLiteral("<gte:gps speed=\"%1\"/>").arg(pts.at(i).kmh, 0, 'f', 1).toUtf8();
        out += "<gpxtpx:TrackPointExtension><gpxtpx:hr>120</gpxtpx:hr><gpxtpx:cad>80</gpxtpx:cad>"
               "</gpxtpx:TrackPointExtension></extensions></trkpt>\n";
    }
    out += "</trkseg></trk></gpx>\n";
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

QString exampleDir()
{
    // tests run from the build tree; the example lives in the source root (never committed)
    for (const QString &base : { QStringLiteral(WATT_SOURCE_DIR), QDir::currentPath() }) {
        const QString d = QDir(base).filePath(QStringLiteral("FR_Le_petit_Menglonnais"));
        if (QFileInfo(d + QStringLiteral("/FR_Le_petit_Menglonnais.gpx")).isFile())
            return d;
    }
    return {};
}

// 10 m/s north for n seconds, flat
QVector<SynthPoint> straight(int n, double speed = 10.0, double ele = 100.0)
{
    QVector<SynthPoint> pts;
    for (int i = 0; i < n; ++i)
        pts.append({ i * speed, ele, speed * 3.6 });
    return pts;
}

} // namespace

class TestRlvRoute : public QObject
{
    Q_OBJECT
private slots:
    void haversine()
    {
        // 1° latitude ≈ 111.2 km
        QVERIFY(std::abs(RlvRoute::haversine(0, 0, 1, 0) - 111195.0) < 50.0);
        QCOMPARE(RlvRoute::haversine(45, 6, 45, 6), 0.0);
        // 0.001° longitude at 45° ≈ 78.6 m
        QVERIFY(std::abs(RlvRoute::haversine(45, 6, 45, 6.001) - 78.6) < 0.5);
    }

    void decodeText()
    {
        QCOMPARE(RlvRoute::decodeText(QStringLiteral("L&auml;nge\\nAnstieg")), QStringLiteral("Länge\nAnstieg"));
        QCOMPARE(RlvRoute::decodeText(QStringLiteral("Walnu&szlig; &amp; more")), QStringLiteral("Walnuß & more"));
        QCOMPARE(RlvRoute::decodeText(QStringLiteral("&unknown; x")), QStringLiteral("&unknown; x"));
        const QByteArray xml = RlvRoute::replaceHtmlEntities("<a t=\"L&auml;nge &amp; &lt;b&gt; &foo;\"/>");
        QCOMPARE(xml, QByteArray("<a t=\"L&#228;nge &amp; &lt;b&gt; &amp;foo;\"/>"));
    }

    // Entities are replaced on the raw bytes: a Latin-1 file keeps its é,
    // a UTF-16 file is left alone.
    void entitiesKeepTheFileEncoding()
    {
        const QByteArray latin1 = QByteArray("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
                                             "<gpx-import><name>Caf\xE9 &auml;</name>"
                                             "<gpx-file-path>t.gpx</gpx-file-path>"
                                             "<informations><information distance=\"10\" en=\"Mont&eacute;e \xE9\"/>"
                                             "</informations></gpx-import>");
        const QByteArray replaced = RlvRoute::replaceHtmlEntities(latin1);
        QVERIFY(replaced.contains("Caf\xE9 &#228;"));   // the raw Latin-1 byte is untouched
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("t.xml")), latin1));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("t.gpx")), gpxFor(straight(31))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("t.mp4")), "x"));
        RlvRoute r;
        QString error;
        QVERIFY2(r.load(dir.filePath(QStringLiteral("t.xml")), &error), qPrintable(error));
        QCOMPARE(r.name(), QStringLiteral("Caf\u00e9 \u00e4"));
        QCOMPARE(r.informations().size(), 1);
        QCOMPARE(r.informations().at(0).text, QStringLiteral("Mont\u00e9e \u00e9"));

        // UTF-16 (BOM): returned unchanged, still parsed
        const QString text = QStringLiteral("<gpx-import><name>K\u00f6ln &amp; mehr</name></gpx-import>");
        QByteArray utf16("\xFF\xFE");
        utf16.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        QCOMPARE(RlvRoute::replaceHtmlEntities(utf16), utf16);
        QVERIFY(writeFile(dir.filePath(QStringLiteral("u.xml")), utf16));
        QVERIFY(RlvRoute::isRouteXml(dir.filePath(QStringLiteral("u.xml"))));
    }

    // <ele> missing on some points: interpolated in distance, carried at the
    // ends; none at all: flat 0 m.
    void missingElevationIsInterpolated()
    {
        QByteArray gpx = "<gpx><trk><trkseg>\n";
        for (int i = 0; i <= 40; ++i) {
            const double lat = 45.0 + i * 10.0 / kMPerDegLat;
            gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.0\">").arg(lat, 0, 'f', 8).toUtf8();
            if (i == 10)
                gpx += "<ele>100</ele>";
            else if (i == 30)
                gpx += "<ele>120</ele>";
            gpx += "</trkpt>\n";
        }
        gpx += "</trkseg></trk></gpx>\n";
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("e.gpx")), gpx));
        RlvRoute r;
        QString error;
        QVERIFY2(r.load(dir.filePath(QStringLiteral("e.gpx")), &error), qPrintable(error));
        const auto &p = r.points();
        QCOMPARE(p.size(), 41);
        QCOMPARE(p.at(0).rawElevation, 100.0);                     // carried back
        QVERIFY(std::abs(p.at(20).rawElevation - 110.0) < 0.01);   // half way
        QVERIFY(std::abs(p.at(25).rawElevation - 115.0) < 0.01);
        QCOMPARE(p.at(40).rawElevation, 120.0);                    // carried forward
        QVERIFY(r.ascent() > 15 && r.ascent() < 25);               // no drop to 0 m in between
    }

    void gpxDistanceAndVideoMapping()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("ride.gpx")), gpxFor(straight(101))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("ride.mp4")), "x"));
        QString error;
        RlvRoute r;
        QVERIFY2(r.load(dir.filePath(QStringLiteral("ride.gpx")), &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(r.hasVideo());
        QCOMPARE(r.name(), QStringLiteral("ride"));
        QCOMPARE(r.title(), QStringLiteral("Synthetic"));
        QCOMPARE(r.points().size(), 101);
        QVERIFY(std::abs(r.totalDistance() - 1000.0) < 0.5);
        QCOMPARE(r.points().at(0).heartRate, 120);
        QCOMPARE(r.points().at(0).cadence, 80);
        QVERIFY(std::abs(r.points().at(5).gpsSpeed - 10.0) < 0.05);
        QVERIFY(std::abs(r.recordedSpeedAtDistance(500) - 10.0) < 0.05);
        // no xml: offset 0, video = track
        QCOMPARE(r.videoOffset(), 0.0);
        QVERIFY(std::abs(r.videoTimeAtDistance(500) - 50.0) < 0.05);
        QVERIFY(std::abs(r.distanceAtVideoTime(25.5) - 255.0) < 0.5);
        QCOMPARE(r.videoTimeAtDistance(-5), 0.0);
        QCOMPARE(r.videoTimeAtDistance(5000), 100.0);
        QCOMPARE(r.distanceAtVideoTime(1000), r.totalDistance());
        QCOMPARE(r.videoDuration(), 101.0);
        QCOMPARE(r.gradeAtDistance(500), 0.0);
        QCOMPARE(r.ascent(), 0.0);
        // folder load finds the same route
        RlvRoute f;
        QVERIFY(f.load(dir.path(), &error));
        QCOMPARE(f.points().size(), 101);
        QCOMPARE(f.sourcePath(), r.sourcePath());
    }

    void stopsAreSkipped()
    {
        // ride 30 s, stand 10 s (jitter, speed 0), ride 30 s
        QVector<SynthPoint> pts;
        double d = 0;
        for (int i = 0; i < 30; ++i, d += 8)
            pts.append({ d, 100, 28.8 });
        for (int i = 0; i < 10; ++i)
            pts.append({ d + (i % 2) * 0.4, 100, 0.0 });   // GPS jitter while standing
        for (int i = 0; i < 30; ++i, d += 8)
            pts.append({ d, 100, 28.8 });
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("stop.gpx")), gpxFor(pts)));
        RlvRoute r;
        QVERIFY(r.load(dir.filePath(QStringLiteral("stop.gpx"))));
        QVERIFY(!r.hasVideo());
        const double stopD = r.points().at(30).distance;
        QCOMPARE(r.points().at(38).distance, stopD);   // jitter ignored
        // monotonic and the stop is skipped (end of the stop at that distance)
        double last = -1;
        for (double x = 0; x <= r.totalDistance(); x += 2.5) {
            const double t = r.videoTimeAtDistance(x);
            QVERIFY(t >= last);
            last = t;
        }
        const double atStop = r.videoTimeAtDistance(stopD);
        QVERIFY2(atStop >= 39.0 && atStop <= 40.0, qPrintable(QString::number(atStop)));
        QVERIFY(r.videoTimeAtDistance(stopD - 4) < 30.0);
    }

    void gradeAndAscent()
    {
        // 500 m flat, 1000 m at +6 %, 500 m flat, 500 m at -4 %, 1 m resolution in time (5 m/s)
        QVector<SynthPoint> pts;
        double ele = 200;
        for (double d = 0; d <= 2500.0 + 1e-6; d += 5.0) {
            if (d > 500 && d <= 1500)
                ele += 0.06 * 5;
            else if (d > 2000)
                ele -= 0.04 * 5;
            pts.append({ d, ele, 18.0 });
        }
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("hill.gpx")), gpxFor(pts)));
        RlvRoute r;
        QVERIFY(r.load(dir.filePath(QStringLiteral("hill.gpx"))));
        QVERIFY2(std::abs(r.gradeAtDistance(1000) - 6.0) < 0.2, qPrintable(QString::number(r.gradeAtDistance(1000))
                 + QStringLiteral(" d=") + QString::number(r.totalDistance())));
        QVERIFY(std::abs(r.gradeAtDistance(250)) < 0.2);
        QVERIFY(std::abs(r.gradeAtDistance(2250) + 4.0) < 0.2);
        QVERIFY2(std::abs(r.ascent() - 60.0) < 3.0, qPrintable(QString::number(r.ascent())));
        QVERIFY2(std::abs(r.descent() - 20.0) < 3.0, qPrintable(QString::number(r.descent())));
        QVERIFY(std::abs(r.elevationAtDistance(1750) - 260.0) < 1.0);
        QVERIFY(r.ascentAtDistance(500) < 1.0);
        QVERIFY(std::abs(r.ascentAtDistance(1600) - r.ascent()) < 2.5);
        QVERIFY(std::abs(r.maxElevation() - 260.0) < 1.0);
        const QVector<QPointF> prof = r.profile(500);
        QCOMPARE(prof.size(), 500);
        QCOMPARE(prof.first().x(), 0.0);
        QVERIFY(std::abs(prof.last().x() - r.totalDistance()) < 1e-6);
    }

    // The grade the rider feels looks slightly ahead: a descent after a crest
    // is felt when it starts (within 20 m), not when the centred base passed.
    void feltGradeLooksAhead()
    {
        QVector<SynthPoint> pts;
        double ele = 200;
        const double crest = 1000;
        for (double d = 0; d <= 2000.0 + 1e-6; d += 5.0) {
            pts.append({ d, ele, 18.0 });
            ele += (d < crest ? 0.04 : -0.06) * 5;
        }
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("crest.gpx")), gpxFor(pts)));
        RlvRoute r;
        QVERIFY(r.load(dir.filePath(QStringLiteral("crest.gpx"))));
        QVERIFY(std::abs(r.gradeAheadAtDistance(500) - 4.0) < 0.2);    // steady: the same grade
        QVERIFY(std::abs(r.gradeAheadAtDistance(1500) + 6.0) < 0.2);
        QVERIFY(r.gradeAheadAtDistance(crest - 40) > 3.5);             // still climbing before
        QVERIFY2(r.gradeAheadAtDistance(crest) < -1.5, qPrintable(QString::number(r.gradeAheadAtDistance(crest))));
        const double at20 = r.gradeAheadAtDistance(crest + 20);
        QVERIFY2(at20 < -5.3, qPrintable(QString::number(at20)));      // (almost) all of the -6 % after 20 m
        QVERIFY(at20 < r.gradeAtDistance(crest + 20) - 0.5);           // the centred grade lags
        // route ends: shrinks, stays finite
        QVERIFY(std::abs(r.gradeAheadAtDistance(0) - 4.0) < 0.5);
        QVERIFY(std::isfinite(r.gradeAheadAtDistance(r.totalDistance())));
    }

    void gradeIsClampedAndNoiseFiltered()
    {
        // a wall: +30 % for 200 m -> clamped to kMaxGrade; noise ±1 m on flat adds no ascent
        QVector<SynthPoint> pts;
        double ele = 100;
        for (double d = 0; d <= 1200; d += 5.0) {
            if (d > 500 && d <= 700)
                ele += 0.30 * 5;
            const double noise = (int(d / 5) % 2) ? 0.9 : -0.9;
            pts.append({ d, ele + (d > 800 ? noise : 0.0) });
        }
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("wall.gpx")), gpxFor(pts)));
        RlvRoute r;
        QVERIFY(r.load(dir.filePath(QStringLiteral("wall.gpx"))));
        QCOMPARE(r.gradeAtDistance(600), RlvRoute::kMaxGrade);
        QVERIFY2(std::abs(r.ascent() - 60.0) < 3.0, qPrintable(QString::number(r.ascent())));
        for (double d = 850; d < 1150; d += 10)
            QVERIFY(std::abs(r.gradeAtDistance(d)) < 1.0);
        // speed derived from the track when gps speed is absent (5 m/s)
        QVERIFY(std::abs(r.recordedSpeedAtDistance(300) - 5.0) < 0.05);
    }

    void xmlImport()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("track.gpx")), gpxFor(straight(201, 5.0))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("film.mp4")), "x"));
        const QByteArray xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\n"
            "<gpx-import>\n"
            "  <name>XX_Test_Route</name>\n  <id>abc</id>\n  <country>DE</country>\n"
            "  <title>Test &Uuml;berland</title>\n"
            "  <video-file-path>C:\\videos\\film.mp4</video-file-path>\n"
            "  <gpx-file-path>track.gpx</gpx-file-path>\n"
            "  <framerate>25.0</framerate>\n  <start-frame>51</start-frame>\n  <end-frame>5300</end-frame>\n"
            "  <informations>\n"
            "    <information distance=\"500\" de=\"Br&uuml;cke\" en=\"Bridge\\nover the river\" />\n"
            "    <information distance=\"20\" de=\"L&auml;nge: 1 km\\nLeicht\" en=\"\" />\n"
            "  </informations>\n"
            "</gpx-import>\n";
        QVERIFY(writeFile(dir.filePath(QStringLiteral("route.xml")), xml));
        QVERIFY(RlvRoute::isRouteXml(dir.filePath(QStringLiteral("route.xml"))));
        QString error;
        RlvRoute r;
        QVERIFY2(r.load(dir.path(), &error), qPrintable(error));
        QCOMPARE(r.sourcePath(), QFileInfo(dir.filePath(QStringLiteral("route.xml"))).absoluteFilePath());
        QCOMPARE(r.name(), QStringLiteral("XX_Test_Route"));
        QCOMPARE(r.title(), QStringLiteral("Test Überland"));
        QCOMPARE(r.country(), QStringLiteral("DE"));
        QCOMPARE(r.id(), QStringLiteral("abc"));
        QCOMPARE(QFileInfo(r.videoPath()).fileName(), QStringLiteral("film.mp4"));   // Windows path resolved locally
        QCOMPARE(r.framerate(), 25.0);
        QCOMPARE(r.startFrame(), 51);
        QCOMPARE(r.endFrame(), 5300);
        QCOMPARE(r.videoOffset(), 2.0);     // (51 - 1) / 25
        QCOMPARE(r.videoDuration(), 212.0); // 5300 / 25
        QVERIFY(std::abs(r.videoTimeAtDistance(0) - 2.0) < 1e-6);
        QVERIFY(std::abs(r.videoTimeAtDistance(500) - 102.0) < 0.05);
        QVERIFY(std::abs(r.distanceAtVideoTime(102.0) - 500.0) < 0.5);
        // informations: sorted, en preferred, de fallback, \n + entities decoded
        QCOMPARE(r.informations().size(), 2);
        QCOMPARE(r.informations().at(0).distance, 20.0);
        QCOMPARE(r.informations().at(0).text, QStringLiteral("Länge: 1 km\nLeicht"));
        QCOMPARE(r.informations().at(1).text, QStringLiteral("Bridge\nover the river"));
        QCOMPARE(r.informations().at(1).textDe, QStringLiteral("Brücke"));
        QCOMPARE(r.informationIndexAt(10), -1);
        QCOMPARE(r.informationIndexAt(25), 0);
        QCOMPARE(r.informationIndexAt(169), 0);   // shown >= 150 m
        QCOMPARE(r.informationIndexAt(171), -1);
        QCOMPARE(r.informationAt(520), QStringLiteral("Bridge\nover the river"));
        QCOMPARE(r.informationAt(700), QString());
    }

    void errors()
    {
        QString error;
        RlvRoute r;
        QVERIFY(!r.load(QStringLiteral("/no/such/route"), &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("bad.gpx")), "<gpx><trk><trkseg><trkpt lat=\"1\""));
        QVERIFY(!r.load(dir.filePath(QStringLiteral("bad.gpx")), &error));
        QVERIFY(!r.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("one.gpx")), gpxFor(straight(1))));
        QVERIFY(!r.load(dir.filePath(QStringLiteral("one.gpx")), &error));
        // without time stamps the points are 1 s apart
        QVERIFY(writeFile(dir.filePath(QStringLiteral("notime.gpx")), gpxFor(straight(21), false)));
        QVERIFY(r.load(dir.filePath(QStringLiteral("notime.gpx")), &error));
        QCOMPARE(r.points().last().time, 20.0);
        // shorter than RlvRoute::kMinDistanceM: not a route
        QVERIFY(writeFile(dir.filePath(QStringLiteral("short.gpx")), gpxFor(straight(8))));
        QVERIFY(!r.load(dir.filePath(QStringLiteral("short.gpx")), &error));
        QVERIFY(!r.isValid());
        QVERIFY(error.contains(QStringLiteral("shorter")));
        // xml pointing at a missing gpx
        QVERIFY(writeFile(dir.filePath(QStringLiteral("x.xml")),
                          "<gpx-import><name>x</name><gpx-file-path>missing.gpx</gpx-file-path></gpx-import>"));
        QVERIFY(!r.load(dir.filePath(QStringLiteral("x.xml")), &error));
        QVERIFY(error.contains(QStringLiteral("missing.gpx")));
    }

    void library()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("a/sub")));
        QVERIFY(root.mkpath(QStringLiteral("b")));
        QVERIFY(root.mkpath(QStringLiteral("c")));
        // a/sub: xml + gpx + mp4
        QVERIFY(writeFile(root.filePath(QStringLiteral("a/sub/t.gpx")), gpxFor(straight(301, 10.0))));
        QVERIFY(writeFile(root.filePath(QStringLiteral("a/sub/t.mp4")), "x"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("a/sub/t.xml")),
                          "<gpx-import><name>Alpha</name><title>Alpha Title</title><country>FR</country>"
                          "<video-file-path>t.mp4</video-file-path><gpx-file-path>t.gpx</gpx-file-path>"
                          "</gpx-import>"));
        // b: gpx + mp4 pair
        QVERIFY(writeFile(root.filePath(QStringLiteral("b/beta.gpx")), gpxFor(straight(51, 5.0))));
        QVERIFY(writeFile(root.filePath(QStringLiteral("b/beta.mp4")), "x"));
        // c: lone gpx (not a route), unrelated xml
        QVERIFY(writeFile(root.filePath(QStringLiteral("c/lone.gpx")), gpxFor(straight(5))));
        QVERIFY(writeFile(root.filePath(QStringLiteral("c/other.xml")), "<config/>"));
        const QStringList found = RlvLibrary::findRoutes(dir.path());
        QCOMPARE(found.size(), 2);

        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        {
            RlvLibrary lib(&db);
            QCOMPARE(lib.libraryFolder(), RlvLibrary::defaultLibraryFolder());
            QSignalSpy scanned(&lib, &RlvLibrary::scanned);
            lib.setLibraryFolder(dir.filePath(QStringLiteral("a")));   // scans in a worker thread
            QVERIFY(lib.scanning());
            QCOMPARE(lib.libraryFolderUrl(), QUrl::fromLocalFile(QDir::cleanPath(dir.filePath(QStringLiteral("a")))));
            QVERIFY(lib.waitForScan());
            QVERIFY(!lib.scanning());
            QCOMPARE(scanned.count(), 1);
            QCOMPARE(lib.count(), 1);
            QCOMPARE(lib.get(0).value(QStringLiteral("folderUrl")).toUrl(),
                     QUrl::fromLocalFile(QDir::cleanPath(dir.filePath(QStringLiteral("a/sub")))));
            QCOMPARE(lib.get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Alpha"));
            QCOMPARE(lib.get(0).value(QStringLiteral("displayName")).toString(), QStringLiteral("Alpha Title"));
            QCOMPARE(lib.get(0).value(QStringLiteral("country")).toString(), QStringLiteral("FR"));
            QVERIFY(std::abs(lib.get(0).value(QStringLiteral("distanceM")).toDouble() - 3000.0) < 1.0);
            QCOMPARE(lib.get(0).value(QStringLiteral("durationSec")).toInt(), 301);
            QVERIFY(lib.get(0).value(QStringLiteral("valid")).toBool());
            QCOMPARE(lib.get(0).value(QStringLiteral("label")).toString(), QStringLiteral("Alpha Title · 3.0 km · 0 m"));
            QVERIFY(lib.addFolder(QUrl::fromLocalFile(dir.filePath(QStringLiteral("b"))).toString()));
            QVERIFY(lib.waitForScan());
            QCOMPARE(lib.count(), 2);
            QCOMPARE(lib.get(1).value(QStringLiteral("name")).toString(), QStringLiteral("beta"));
            QVERIFY(!lib.addFolder(QStringLiteral("/no/such/folder")));
            QVERIFY(!lib.lastError().isEmpty());
            QCOMPARE(lib.importRoute(dir.filePath(QStringLiteral("c"))), -1);   // no video: not a route
            QCOMPARE(lib.indexOfPath(dir.filePath(QStringLiteral("b"))), 1);
            QCOMPARE(lib.rowCount(), 2);
            QCOMPARE(lib.data(lib.index(0), RlvLibrary::AscentMRole).toInt(), 0);
        }
        // persisted folders + cache
        RlvLibrary again(&db);
        QCOMPARE(again.libraryFolder(), QDir::cleanPath(dir.filePath(QStringLiteral("a"))));
        QCOMPARE(again.extraFolders().size(), 1);
        again.rescanNow();
        QCOMPARE(again.count(), 2);
        QVERIFY(again.removeFolder(dir.filePath(QStringLiteral("b"))));
        QVERIFY(again.waitForScan());
        QCOMPARE(again.count(), 1);
        const int row = again.importRoute(dir.filePath(QStringLiteral("b/beta.gpx")));
        QCOMPARE(row, 1);
        QCOMPARE(again.count(), 2);
        // a deleted video makes the entry invalid (cache not trusted)
        QVERIFY(QFile::remove(root.filePath(QStringLiteral("a/sub/t.mp4"))));
        again.rescan();
        again.rescan();   // while scanning: runs once more afterwards
        QVERIFY(again.waitForScan());
        QCOMPARE(again.count(), 2);
        QVERIFY(!again.get(0).value(QStringLiteral("valid")).toBool());
        QVERIFY(!again.get(0).value(QStringLiteral("error")).toString().isEmpty());
    }

    // Temporary folders (WATT_RLV_DIR) are scanned but their routes are not
    // written into the "rlv/cache"; the cache stamps the gpx an xml resolved to.
    void temporaryFolderNotCached()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("tmp")));
        QVERIFY(root.mkpath(QStringLiteral("lib/tracks")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("tmp/t.gpx")), gpxFor(straight(31))));
        QVERIFY(writeFile(root.filePath(QStringLiteral("tmp/t.mp4")), "x"));
        // xml route whose gpx lives elsewhere (gpx-file-path)
        QVERIFY(writeFile(root.filePath(QStringLiteral("lib/tracks/other.gpx")), gpxFor(straight(31))));
        QVERIFY(writeFile(root.filePath(QStringLiteral("lib/r.mp4")), "x"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("lib/r.xml")),
                          "<gpx-import><name>R</name><video-file-path>r.mp4</video-file-path>"
                          "<gpx-file-path>tracks/other.gpx</gpx-file-path></gpx-import>"));
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        RlvLibrary lib(&db);
        lib.setLibraryFolder(root.filePath(QStringLiteral("lib")));
        lib.addTemporaryFolder(root.filePath(QStringLiteral("tmp")));
        lib.rescanNow();
        QCOMPARE(lib.count(), 2);
        const QString cache = db.setting(QStringLiteral("rlv/cache")).toString();
        QVERIFY(cache.contains(QStringLiteral("r.xml")));
        QVERIFY(cache.contains(QStringLiteral("other.gpx")));   // the resolved track is stamped
        QVERIFY(!cache.contains(QStringLiteral("tmp/t.gpx")));
        QVERIFY(!db.setting(QStringLiteral("rlv/extraFolders")).toString().contains(QStringLiteral("tmp")));
        const int libRow = lib.indexOfPath(root.filePath(QStringLiteral("lib/r.xml")));
        QVERIFY(libRow >= 0);
        QCOMPARE(lib.entries().at(libRow).gpxPath,
                 QFileInfo(root.filePath(QStringLiteral("lib/tracks/other.gpx"))).absoluteFilePath());

        // the resolved gpx changes -> the cached entry is parsed again
        QVERIFY(writeFile(root.filePath(QStringLiteral("lib/tracks/other.gpx")), gpxFor(straight(61))));
        QFile gpxFile(root.filePath(QStringLiteral("lib/tracks/other.gpx")));
        QVERIFY(gpxFile.open(QIODevice::ReadWrite));
        QVERIFY(gpxFile.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
        gpxFile.close();
        RlvLibrary again(&db);
        again.rescanNow();
        const int row = again.indexOfPath(root.filePath(QStringLiteral("lib/r.xml")));
        QVERIFY(row >= 0);
        QVERIFY(std::abs(again.get(row).value(QStringLiteral("distanceM")).toDouble() - 600.0) < 1.0);
    }

    void exampleRoute()
    {
        const QString dir = exampleDir();
        if (dir.isEmpty())
            QSKIP("FR_Le_petit_Menglonnais example not present (not part of the repository)");
        QString error;
        RlvRoute r;
        QVERIFY2(r.load(dir, &error), qPrintable(error));
        QVERIFY(r.hasVideo());
        QCOMPARE(r.name(), QStringLiteral("FR_Le_petit_Menglonnais"));
        QCOMPARE(r.title(), QStringLiteral("Menglonnais"));
        QCOMPARE(r.country(), QStringLiteral("FR"));
        QCOMPARE(r.framerate(), 30.0);
        QCOMPARE(r.startFrame(), 1);
        QCOMPARE(r.endFrame(), 53490);
        QCOMPARE(r.points().size(), 1783);
        QCOMPARE(r.videoOffset(), 0.0);
        QCOMPARE(r.videoDuration(), 1783.0);                  // 53490 / 30
        QCOMPARE(r.trackDuration(), 1783.0);                  // 1782 s of points + 1 s
        QVERIFY(std::abs(r.videoDuration() - r.trackDuration()) < 1.0);
        qInfo("example: %d points, %.1f m, ascent %.1f m, descent %.1f m, ele %.1f..%.1f m, video %.1f s, "
              "grade range %.1f..%.1f %%",
              int(r.points().size()), r.totalDistance(), r.ascent(), r.descent(), r.minElevation(),
              r.maxElevation(), r.videoDuration(),
              std::min_element(r.points().cbegin(), r.points().cend(),
                               [](const RlvPoint &a, const RlvPoint &b) { return a.grade < b.grade; })->grade,
              std::max_element(r.points().cbegin(), r.points().cend(),
                               [](const RlvPoint &a, const RlvPoint &b) { return a.grade < b.grade; })->grade);
        // distance: "12.0 km" in the overlay text
        QVERIFY2(r.totalDistance() > 11500 && r.totalDistance() < 12500, qPrintable(QString::number(r.totalDistance())));
        // the recorded GPS speed integrates to about the same distance
        double gpsDist = 0;
        for (const RlvPoint &p : r.points())
            gpsDist += std::max(0.0, p.gpsSpeed);
        QVERIFY2(std::abs(gpsDist - r.totalDistance()) / r.totalDistance() < 0.05, qPrintable(QString::number(gpsDist)));
        // a loop: start and end elevation 498 m; ascent ≈ descent; the de text says 154 hm
        QVERIFY2(r.ascent() > 120 && r.ascent() < 220, qPrintable(QString::number(r.ascent())));
        QVERIFY(std::abs(r.ascent() - r.descent()) < 20);
        // mapping: last point at video 1782 s, end of the route at the video end
        QCOMPARE(r.videoTimeAtDistance(r.totalDistance()), 1782.0);
        QVERIFY(std::abs(r.distanceAtVideoTime(r.videoTimeAtDistance(6000)) - 6000) < 1.0);
        // informations
        QCOMPARE(r.informations().size(), 7);
        QCOMPARE(r.informations().at(0).text,
                 QStringLiteral("Le petit Menglonnais\nLength: 12.0 km - Ascent: 365 hm\nDifficulty: Easy"));
        QVERIFY(r.informations().at(0).textDe.contains(QStringLiteral("Länge")));
        QCOMPARE(r.informations().at(4).textDe, QStringLiteral("Walnußplantagen"));
        QCOMPARE(r.informationAt(5650), QStringLiteral("Menglon"));
        for (const RlvPoint &p : r.points()) {
            QVERIFY(p.grade >= RlvRoute::kMinGrade && p.grade <= RlvRoute::kMaxGrade);
        }
    }
};

QTEST_GUILESS_MAIN(TestRlvRoute)
#include "tst_rlvroute.moc"
