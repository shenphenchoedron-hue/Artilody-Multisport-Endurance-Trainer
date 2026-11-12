// The server's media library on a station (MediaSync, docs/server-plan.md
// step 4) - against a small stand-in for api/media.php, api/upload.php and
// /blobs/<sha1> (with ranges), and real GPX / video library folders.
#include "Database.h"
#include "MediaSync.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QUuid>
#include <QtTest>

using namespace watt;

namespace {

QString sha1(const QByteArray &d)
{
    return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha1).toHex());
}

// a valid little route: points ~111 m apart
QByteArray gpxRoute(double lat0, int points = 4)
{
    QByteArray g = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" creator=\"test\"><trk><name>T</name><trkseg>\n";
    for (int i = 0; i < points; ++i)
        g += QStringLiteral("<trkpt lat=\"%1\" lon=\"10.0\"><ele>%2</ele></trkpt>\n").arg(lat0 + i * 0.001, 0, 'f', 6).arg(100 + i).toUtf8();
    return g + "</trkseg></trk></gpx>\n";
}

class FakeServer : public QObject
{
public:
    QTcpServer tcp;
    QString id = QStringLiteral("server-1");
    qint64 rev = 0;
    QMap<QString, QJsonObject> media;        // uuid -> item
    QMap<QString, QByteArray> blobs;         // sha1 -> bytes
    QMap<QString, QByteArray> uploads;       // id -> received
    QMap<QString, qint64> uploadSizes;
    int chunks = 0, heads = 0, rangeRequests = 0, blobGets = 0;
    qint64 cutAfter = -1;                    // the next blob GET stops after this many bytes

    FakeServer()
    {
        tcp.listen(QHostAddress::LocalHost);
        connect(&tcp, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = tcp.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] { onData(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/").arg(tcp.serverPort()); }

    QString add(const QString &kind, const QString &name, const QList<QPair<QString, QByteArray>> &files)
    {
        QJsonArray list;
        qint64 total = 0;
        for (const auto &[fname, data] : files) {
            blobs.insert(sha1(data), data);
            list.append(QJsonObject { { QStringLiteral("name"), fname }, { QStringLiteral("hash"), sha1(data) },
                                      { QStringLiteral("size"), qint64(data.size()) } });
            total += data.size();
        }
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        media.insert(uuid, QJsonObject { { QStringLiteral("uuid"), uuid }, { QStringLiteral("kind"), kind },
                                         { QStringLiteral("name"), name },
                                         { QStringLiteral("folder"), kind == QLatin1String("video") ? name : QString() },
                                         { QStringLiteral("files"), list }, { QStringLiteral("sizeBytes"), total },
                                         { QStringLiteral("deleted"), false }, { QStringLiteral("updatedMs"), 1 },
                                         { QStringLiteral("uploadedBy"), QStringLiteral("web") }, { QStringLiteral("rev"), ++rev } });
        return uuid;
    }
    void remove(const QString &uuid)
    {
        media[uuid][QStringLiteral("deleted")] = true;
        media[uuid][QStringLiteral("files")] = QJsonArray();
        media[uuid][QStringLiteral("rev")] = ++rev;
    }

private:
    QHash<QTcpSocket *, QByteArray> buf;

    static void reply(QTcpSocket *s, int status, const QByteArray &body, const QByteArray &type = "application/json",
                      const QByteArray &extra = {}, qint64 cut = -1)
    {
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + type + "\r\nContent-Length: "
                 + QByteArray::number(body.size()) + "\r\n" + extra + "Connection: close\r\n\r\n");
        s->write(cut >= 0 ? body.left(cut) : body);
        s->flush();
        s->disconnectFromHost();
    }
    static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

    void onData(QTcpSocket *s)
    {
        QByteArray &b = buf[s];
        b += s->readAll();
        const int end = b.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = b.left(end);
        qint64 length = 0;
        QByteArray range;
        for (const QByteArray &line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("content-length:"))
                length = l.mid(15).trimmed().toLongLong();
            if (l.toLower().startsWith("range:"))
                range = l.mid(6).trimmed();
        }
        if (b.size() < end + 4 + length)
            return;
        const QByteArray body = b.mid(end + 4, length);
        const QList<QByteArray> first = head.split('\n').first().trimmed().split(' ');
        buf.remove(s);
        const QByteArray method = first.value(0);
        const QUrl url(QString::fromLatin1(first.value(1)));
        const QUrlQuery q(url);
        const QString path = url.path();

        if (path.startsWith(QLatin1String("/blobs/"))) {
            const QString h = path.mid(7);
            if (!blobs.contains(h)) {
                reply(s, 404, "no");
                return;
            }
            const QByteArray data = blobs.value(h);
            if (method == "HEAD") {
                ++heads;
                s->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\nConnection: close\r\n\r\n");
                s->disconnectFromHost();
                return;
            }
            ++blobGets;
            const qint64 cut = cutAfter;
            cutAfter = -1;
            if (range.startsWith("bytes=")) {
                ++rangeRequests;
                const qint64 from = range.mid(6, range.indexOf('-') - 6).toLongLong();
                reply(s, 206, data.mid(from), "application/octet-stream",
                      "Content-Range: bytes " + QByteArray::number(from) + "-" + QByteArray::number(data.size() - 1) + "/"
                          + QByteArray::number(data.size()) + "\r\n", cut);
            } else {
                reply(s, 200, data, "application/octet-stream", {}, cut);
            }
        } else if (path == QLatin1String("/api/media.php") && method == "GET") {
            const qint64 since = q.queryItemValue(QStringLiteral("since")).toLongLong();
            QJsonArray list;
            for (const QJsonObject &m : media)
                if (m.value(QStringLiteral("rev")).toInteger() > since)
                    list.append(m);
            reply(s, 200, json({ { QStringLiteral("serverId"), id }, { QStringLiteral("rev"), rev }, { QStringLiteral("media"), list } }));
        } else if (path == QLatin1String("/api/media.php") && method == "POST") {
            const QJsonObject in = QJsonDocument::fromJson(body).object();
            QList<QPair<QString, QByteArray>> files;
            for (const QJsonValue &v : in.value(QStringLiteral("files")).toArray()) {
                const QString h = v.toObject().value(QStringLiteral("hash")).toString();
                if (!blobs.contains(h)) {
                    reply(s, 400, json({ { QStringLiteral("error"), QStringLiteral("not uploaded") } }));
                    return;
                }
                files.append({ v.toObject().value(QStringLiteral("name")).toString(), blobs.value(h) });
            }
            const QString uuid = add(in.value(QStringLiteral("kind")).toString(), in.value(QStringLiteral("name")).toString(), files);
            media[uuid][QStringLiteral("folder")] = in.value(QStringLiteral("folder"));
            reply(s, 200, json({ { QStringLiteral("serverId"), id }, { QStringLiteral("rev"), rev },
                                 { QStringLiteral("item"), media.value(uuid) }, { QStringLiteral("created"), true } }));
        } else if (path == QLatin1String("/api/upload.php")) {
            const QString action = q.queryItemValue(QStringLiteral("action"));
            const QString uid = q.queryItemValue(QStringLiteral("id"));
            if (action == QLatin1String("start")) {
                const QString nid = QString::number(uploads.size() + 1);
                uploads.insert(nid, {});
                uploadSizes.insert(nid, q.queryItemValue(QStringLiteral("size")).toLongLong());
                reply(s, 200, json({ { QStringLiteral("id"), nid } }));
            } else if (action == QLatin1String("chunk")) {
                ++chunks;
                QByteArray &got = uploads[uid];
                if (q.queryItemValue(QStringLiteral("offset")).toLongLong() != got.size()) {
                    reply(s, 409, json({ { QStringLiteral("error"), QStringLiteral("offset") }, { QStringLiteral("received"), qint64(got.size()) } }));
                    return;
                }
                got += body;
                reply(s, 200, json({ { QStringLiteral("received"), qint64(got.size()) } }));
            } else {
                const QByteArray data = uploads.take(uid);
                if (data.size() != uploadSizes.value(uid)) {
                    reply(s, 400, json({ { QStringLiteral("error"), QStringLiteral("not complete") } }));
                    return;
                }
                blobs.insert(sha1(data), data);
                reply(s, 200, json({ { QStringLiteral("hash"), sha1(data) }, { QStringLiteral("size"), qint64(data.size()) } }));
            }
        } else {
            reply(s, 404, "no");
        }
    }
};

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<RlvLibrary> gpx, videos;
    std::unique_ptr<MediaSync> sync;
    FakeServer server;
    bool riding = false;

    Fixture()
    {
        db.open(dir.filePath(QStringLiteral("s.sqlite")));
        db.setSetting(QStringLiteral("gpx/libraryFolder"), dir.filePath(QStringLiteral("GPX")));
        db.setSetting(QStringLiteral("rlv/libraryFolder"), dir.filePath(QStringLiteral("Videos")));
        QDir().mkpath(dir.filePath(QStringLiteral("GPX")));
        QDir().mkpath(dir.filePath(QStringLiteral("Videos")));
        gpx = std::make_unique<RlvLibrary>(&db, RlvLibrary::Kind::Gpx);
        videos = std::make_unique<RlvLibrary>(&db, RlvLibrary::Kind::Video);
        gpx->rescanNow();
        videos->rescanNow();
        sync = std::make_unique<MediaSync>(&db, gpx.get(), videos.get(), nullptr);
        sync->setBusyCheck([this] { return riding; });
    }
    QString gpxDir() const { return dir.filePath(QStringLiteral("GPX")); }
    QString videoDir() const { return dir.filePath(QStringLiteral("Videos")); }
    void answer() { sync->serverAnswered(server.url(), server.id, server.rev); }
    QString stateOf(const QString &uuid) const
    {
        for (const QVariant &v : sync->items())
            if (v.toMap().value(QStringLiteral("uuid")).toString() == uuid)
                return v.toMap().value(QStringLiteral("state")).toString();
        return QString();
    }
    bool idle(int ms = 10000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (!sync->transferring() && sync->queued() == 0 && sync->state() != QLatin1String("syncing"))
                return true;
        }
        return false;
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    static void write(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(data);
    }
};

} // namespace

class TestMediaSync : public QObject
{
    Q_OBJECT
private slots:
    void gpxByThemselvesVideosWhenAsked()
    {
        Fixture f;
        const QByteArray route = gpxRoute(55.0);
        const QString g = f.server.add(QStringLiteral("gpx"), QStringLiteral("Flad"), { { QStringLiteral("Flad.gpx"), route } });
        const QByteArray video = QByteArray(300000, 'v');
        const QByteArray track = gpxRoute(56.0);
        const QString v = f.server.add(QStringLiteral("video"), QStringLiteral("Alpe"),
                                       { { QStringLiteral("Alpe.gpx"), track }, { QStringLiteral("Alpe.mp4"), video } });
        f.answer();
        QVERIFY(f.idle());
        // the GPX route arrived by itself; the video waits
        QCOMPARE(Fixture::read(QDir(f.gpxDir()).filePath(QStringLiteral("Flad.gpx"))), route);
        QCOMPARE(f.stateOf(g), QStringLiteral("local"));
        QCOMPARE(f.stateOf(v), QStringLiteral("available"));
        QVERIFY(!QDir(f.videoDir()).exists(QStringLiteral("Alpe")));
        QTRY_VERIFY(f.gpx->indexOfPath(QDir(f.gpxDir()).filePath(QStringLiteral("Flad.gpx"))) >= 0);   // (rescanned)

        // the video asked for: its folder with both files, a route of the library
        QSignalSpy finished(f.sync.get(), &MediaSync::transferFinished);
        QVERIFY(f.sync->download(v));
        QVERIFY(f.idle());
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
        const QString folder = QDir(f.videoDir()).filePath(QStringLiteral("Alpe"));
        QCOMPARE(Fixture::read(QDir(folder).filePath(QStringLiteral("Alpe.mp4"))), video);
        QCOMPARE(Fixture::read(QDir(folder).filePath(QStringLiteral("Alpe.gpx"))), track);
        QVERIFY(QDir(folder).entryList({ QStringLiteral("*.part") }, QDir::Files).isEmpty());
        QCOMPARE(f.stateOf(v), QStringLiteral("local"));
        QTRY_VERIFY(f.videos->indexOfPath(folder) >= 0);
        QVERIFY(f.sync->onServer(QDir(folder).filePath(QStringLiteral("Alpe.gpx"))));

        // known after a restart (kept in the database)
        MediaSync again(&f.db, f.gpx.get(), f.videos.get(), nullptr);
        QCOMPARE(again.items().size(), 2);

        // deleted on the server: gone from the list, the copies here stay
        f.server.remove(v);
        f.answer();
        QVERIFY(f.idle());
        QCOMPARE(f.sync->items().size(), 1);
        QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("Alpe.mp4"))));
    }

    void missingFileOnTheServerLeavesNothing()
    {
        // the server answers 404 (its file gone): the error page is not kept as a part of the file
        Fixture f;
        const QString g = f.server.add(QStringLiteral("gpx"), QStringLiteral("Gone"), { { QStringLiteral("Gone.gpx"), gpxRoute(60.0) } });
        f.server.blobs.clear();
        f.answer();
        QVERIFY(f.idle());
        QCOMPARE(f.stateOf(g), QStringLiteral("available"));
        QCOMPARE(QDir(f.gpxDir()).entryList(QDir::Files).size(), 0);   // (no .part with "no" in it)
        QCOMPARE(f.sync->state(), QStringLiteral("error"));
        QVERIFY(!f.sync->message().contains(QStringLiteral("resumes")));
    }

    void interruptedDownloadResumes()
    {
        Fixture f;
        QByteArray video(2000000, 0);
        for (int i = 0; i < video.size(); ++i)
            video[i] = char(i * 7);
        const QString v = f.server.add(QStringLiteral("video"), QStringLiteral("Long"),
                                       { { QStringLiteral("Long.mp4"), video }, { QStringLiteral("Long.gpx"), gpxRoute(57.0) } });
        f.answer();
        QVERIFY(f.idle());
        f.server.cutAfter = 700000;
        QSignalSpy finished(f.sync.get(), &MediaSync::transferFinished);
        QVERIFY(f.sync->download(v));
        QVERIFY(f.idle());
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());   // (cut off)
        QCOMPARE(f.sync->state(), QStringLiteral("error"));
        const QString part = QDir(f.videoDir()).filePath(QStringLiteral("Long/Long.mp4.part"));
        QVERIFY(QFileInfo(part).size() > 0);
        QVERIFY(!QFileInfo::exists(QDir(f.videoDir()).filePath(QStringLiteral("Long/Long.gpx"))));   // (not a route yet)
        // again: the rest only
        QVERIFY(f.sync->download(v));
        QVERIFY(f.idle());
        QCOMPARE(f.server.rangeRequests, 1);
        QCOMPARE(Fixture::read(QDir(f.videoDir()).filePath(QStringLiteral("Long/Long.mp4"))), video);
        QCOMPARE(f.stateOf(v), QStringLiteral("local"));
        QCOMPARE(f.sync->state(), QStringLiteral("idle"));
    }

    void editedRouteIsNeverOverwritten()
    {
        Fixture f;
        // "Flad.gpx" here, edited (other content)
        const QString mine = QDir(f.gpxDir()).filePath(QStringLiteral("Flad.gpx"));
        Fixture::write(mine, gpxRoute(10.0));
        const QString g = f.server.add(QStringLiteral("gpx"), QStringLiteral("Flad"), { { QStringLiteral("Flad.gpx"), gpxRoute(20.0) } });
        f.answer();
        QVERIFY(f.idle());
        QCOMPARE(f.stateOf(g), QStringLiteral("conflict"));
        QCOMPARE(Fixture::read(mine), gpxRoute(10.0));
        QCOMPARE(f.server.blobGets, 0);
        // fetched as a copy when asked
        QVERIFY(f.sync->download(g));
        QVERIFY(f.idle());
        QCOMPARE(Fixture::read(QDir(f.gpxDir()).filePath(QStringLiteral("Flad (server).gpx"))), gpxRoute(20.0));
        QCOMPARE(Fixture::read(mine), gpxRoute(10.0));
        QCOMPARE(f.stateOf(g), QStringLiteral("local"));

        // the same content under another name: recognised, not fetched
        Fixture f2;
        Fixture::write(QDir(f2.gpxDir()).filePath(QStringLiteral("Renamed.gpx")), gpxRoute(30.0));
        f2.gpx->rescanNow();
        const QString g2 = f2.server.add(QStringLiteral("gpx"), QStringLiteral("Same"), { { QStringLiteral("Same.gpx"), gpxRoute(30.0) } });
        f2.answer();
        QVERIFY(f2.idle());
        QCOMPARE(f2.stateOf(g2), QStringLiteral("local"));
        QCOMPARE(f2.server.blobGets, 0);
    }

    void videoSuffixesMatchTheServer()
    {
        // the server takes the videos the app plays (server/lib/media.php MEDIA_VIDEO_EXTENSIONS)
        QFile php(QStringLiteral(WATT_SOURCE_DIR "/server/lib/media.php"));
        QVERIFY(php.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(php.readAll());
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("MEDIA_VIDEO_EXTENSIONS = \\[([^\\]]*)\\]")).match(text);
        QVERIFY(m.hasMatch());
        QStringList server = m.captured(1).remove(QLatin1Char('\'')).remove(QLatin1Char(' ')).split(QLatin1Char(','));
        QStringList app = RlvRoute::videoSuffixes();
        server.sort();
        app.sort();
        QCOMPARE(server, app);
    }

    void cancelledUploadKeepsTheFile()
    {
        // Cancel while sending: the route's own files stay (only a download's .part goes)
        Fixture f;
        f.answer();
        QVERIFY(f.idle());
        const QString folder = QDir(f.videoDir()).filePath(QStringLiteral("Mine"));
        const QByteArray video(int(MediaSync::kChunkBytes * 3), 'm');
        Fixture::write(QDir(folder).filePath(QStringLiteral("Mine.mp4")), video);
        Fixture::write(QDir(folder).filePath(QStringLiteral("Mine.gpx")), gpxRoute(41.0));
        f.videos->rescanNow();
        const int row = f.videos->indexOfPath(folder);
        QVERIFY(row >= 0);
        QVERIFY(f.sync->upload(f.videos->pathAt(row), QStringLiteral("video")));
        QTRY_VERIFY(f.server.chunks >= 1);   // (sending pieces)
        f.sync->cancel();
        QVERIFY(f.idle());
        QCOMPARE(Fixture::read(QDir(folder).filePath(QStringLiteral("Mine.mp4"))), video);
        QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("Mine.gpx"))));
        // and a later cancel (nothing under way) does not touch it either
        f.sync->cancel();
        QCOMPARE(Fixture::read(QDir(folder).filePath(QStringLiteral("Mine.mp4"))), video);
    }

    void uploadsFromTheStation()
    {
        Fixture f;
        f.answer();
        QVERIFY(f.idle());
        // a GPX route of the library
        const QString route = QDir(f.gpxDir()).filePath(QStringLiteral("Mine.gpx"));
        Fixture::write(route, gpxRoute(40.0));
        f.gpx->rescanNow();
        QVERIFY(!f.sync->onServer(route));
        QVERIFY(f.sync->upload(route, QStringLiteral("gpx")));
        QVERIFY(f.idle());
        QCOMPARE(f.server.media.size(), 1);
        QVERIFY(f.server.blobs.contains(sha1(gpxRoute(40.0))));
        QVERIFY(f.sync->onServer(route));
        QCOMPARE(f.sync->items().size(), 1);
        QCOMPARE(f.stateOf(f.server.media.firstKey()), QStringLiteral("local"));

        // a video route (two pieces of 8 MB) - the server has its gpx already: not sent again
        const QString folder = QDir(f.videoDir()).filePath(QStringLiteral("Ride"));
        QByteArray video(int(MediaSync::kChunkBytes + 12345), 'x');
        Fixture::write(QDir(folder).filePath(QStringLiteral("Ride.mp4")), video);
        Fixture::write(QDir(folder).filePath(QStringLiteral("Ride.gpx")), gpxRoute(40.0));
        f.videos->rescanNow();
        const int row = f.videos->indexOfPath(folder);
        QVERIFY(row >= 0);
        const int chunksBefore = f.server.chunks;
        QSignalSpy finished(f.sync.get(), &MediaSync::transferFinished);
        QVERIFY(f.sync->upload(f.videos->pathAt(row), QStringLiteral("video")));
        QVERIFY(f.idle(20000));
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(f.server.chunks - chunksBefore, 2);   // (the video only)
        QVERIFY(f.server.blobs.contains(sha1(video)));
        QCOMPARE(f.server.media.size(), 2);
        bool found = false;
        for (const QJsonObject &m : f.server.media)
            found |= m.value(QStringLiteral("kind")).toString() == QLatin1String("video")
                     && m.value(QStringLiteral("folder")).toString() == QLatin1String("Ride");
        QVERIFY(found);
        QVERIFY(f.sync->onServer(f.videos->pathAt(row)));

        // not a route of the library: refused
        QVERIFY(!f.sync->upload(f.dir.filePath(QStringLiteral("nope.gpx")), QStringLiteral("gpx")));
    }

    void notDuringARideAndAnotherServer()
    {
        Fixture f;
        f.server.add(QStringLiteral("gpx"), QStringLiteral("A"), { { QStringLiteral("A.gpx"), gpxRoute(1.0) } });
        f.riding = true;
        f.answer();
        QVERIFY(f.idle());
        QCOMPARE(f.sync->items().size(), 1);   // (the list: yes)
        QCOMPARE(f.server.blobGets, 0);         // (fetching: after the ride)
        f.riding = false;
        f.answer();
        QVERIFY(f.idle());
        QCOMPARE(f.server.blobGets, 1);

        // another server: its own list
        f.server.id = QStringLiteral("server-2");
        f.server.media.clear();
        f.server.rev = 0;
        f.answer();
        QVERIFY(f.idle());
        QVERIFY(f.sync->items().isEmpty());
        QVERIFY(QFileInfo::exists(QDir(f.gpxDir()).filePath(QStringLiteral("A.gpx"))));   // (the copy stays)
    }
};

QTEST_GUILESS_MAIN(TestMediaSync)
#include "tst_mediasync.moc"
