// The rides kept with the server (RideSync, docs/server-plan.md step 5) -
// two stations against a small stand-in for api/rides.php (as
// server/lib/rides.php: kept once, deletions as tombstones).
#include "Database.h"
#include "ProfileManager.h"
#include "RideSync.h"
#include "RouteGhost.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

using namespace watt;

namespace {

class FakeServer : public QObject
{
public:
    QTcpServer tcp;
    QString id = QStringLiteral("server-1");
    qint64 rev = 0;
    QMap<QString, QJsonObject> rides;   // uuid -> ride (full; deleted ones flagged)
    int posts = 0, fullGets = 0;
    qint64 pullLimit = 5000;

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

private:
    QHash<QTcpSocket *, QByteArray> buf;

    static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
    void onData(QTcpSocket *s)
    {
        QByteArray &b = buf[s];
        b += s->readAll();
        const int end = b.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        qint64 length = 0;
        for (const QByteArray &line : b.left(end).split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:"))
                length = line.trimmed().mid(15).trimmed().toLongLong();
        if (b.size() < end + 4 + length)
            return;
        const QByteArray body = b.mid(end + 4, length);
        const QList<QByteArray> first = b.left(end).split('\n').first().trimmed().split(' ');
        buf.remove(s);
        const QUrl url(QString::fromLatin1(first.value(1)));
        const QUrlQuery q(url);
        int status = 200;
        QByteArray out;
        if (first.value(0) == "POST") {
            ++posts;
            QJsonArray results;
            for (const QJsonValue &v : QJsonDocument::fromJson(body).object().value(QStringLiteral("rides")).toArray()) {
                QJsonObject r = v.toObject();
                const QString uuid = r.value(QStringLiteral("uuid")).toString();
                QJsonObject res { { QStringLiteral("uuid"), uuid }, { QStringLiteral("taken"), true } };
                if (r.value(QStringLiteral("deleted")).toBool()) {
                    QJsonObject t = rides.value(uuid);
                    t.insert(QStringLiteral("uuid"), uuid);
                    t.insert(QStringLiteral("deleted"), true);
                    t.insert(QStringLiteral("rev"), ++rev);
                    rides.insert(uuid, t);
                    res.insert(QStringLiteral("deleted"), true);
                } else if (rides.contains(uuid)) {
                    if (rides.value(uuid).value(QStringLiteral("deleted")).toBool())
                        res.insert(QStringLiteral("deleted"), true);
                } else {
                    r.insert(QStringLiteral("stationName"), QJsonDocument::fromJson(body).object().value(QStringLiteral("name")));
                    r.insert(QStringLiteral("deleted"), false);
                    r.insert(QStringLiteral("rev"), ++rev);
                    rides.insert(uuid, r);
                }
                results.append(res);
            }
            out = json({ { QStringLiteral("serverId"), id }, { QStringLiteral("rev"), rev }, { QStringLiteral("results"), results } });
        } else if (q.hasQueryItem(QStringLiteral("uuid"))) {
            ++fullGets;
            const QJsonObject r = rides.value(q.queryItemValue(QStringLiteral("uuid")));
            if (r.isEmpty() || r.value(QStringLiteral("deleted")).toBool())
                status = 404;
            else
                out = json({ { QStringLiteral("ride"), r } });
        } else {
            const qint64 since = q.queryItemValue(QStringLiteral("since")).toLongLong();
            QList<QJsonObject> changed;
            for (const QJsonObject &r : rides)
                if (r.value(QStringLiteral("rev")).toInteger() > since)
                    changed.append(r);
            std::sort(changed.begin(), changed.end(), [](const QJsonObject &a, const QJsonObject &b) {
                return a.value(QStringLiteral("rev")).toInteger() < b.value(QStringLiteral("rev")).toInteger();
            });
            const bool more = changed.size() > pullLimit;
            changed = changed.mid(0, int(pullLimit));
            QJsonArray list;
            for (QJsonObject r : changed) {
                r.remove(QStringLiteral("samples"));
                r.remove(QStringLiteral("laps"));
                r.remove(QStringLiteral("trace"));
                list.append(r);
            }
            out = json({ { QStringLiteral("serverId"), id },
                         { QStringLiteral("rev"), more ? changed.last().value(QStringLiteral("rev")).toInteger() : rev },
                         { QStringLiteral("more"), more }, { QStringLiteral("rides"), list } });
        }
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(out.size()) + "\r\nConnection: close\r\n\r\n" + out);
        s->disconnectFromHost();
    }
};

// one station: its database, profiles and RideSync
struct Station
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<RideSync> sync;
    bool riding = false;

    explicit Station(const QString &profileUuid = {})
    {
        db.open(dir.filePath(QStringLiteral("s.sqlite")));
        db.seedDefaults();
        if (!profileUuid.isEmpty()) {   // the same rider as on the other station (as UserSync leaves it)
            const Profile seed = db.profiles().first();
            Profile p = seed;
            p.id = -1;
            p.uuid = profileUuid;
            p.updatedMs = 1;
            const qint64 id = db.storeServerProfile(p);
            db.deleteProfile(seed.id, false);
            db.setSetting(QStringLiteral("currentProfile"), id);
        }
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        sync = std::make_unique<RideSync>(&db, profiles.get(), nullptr);
        sync->setBusyCheck([this] { return riding; });
    }
    qint64 ride(int seconds, int watts, bool withTrace = false)
    {
        RideRecord r;
        r.profileId = profiles->profileId();
        r.startedAt = QDateTime::currentDateTime().addSecs(-seconds);
        r.durationSec = seconds;
        r.avgPower = watts;
        r.workoutName = QStringLiteral("Ride %1 W").arg(watts);
        QVector<Sample> samples;
        for (int t = 0; t < seconds; ++t)
            samples.append({ t, watts, 0, 90, 140, 9.0 });
        const qint64 id = db.saveRide(r, samples);
        if (withTrace) {
            RouteTrace t;
            t.profileId = r.profileId;
            t.rideId = id;
            t.sport = QStringLiteral("gpx");
            t.routeKey = QStringLiteral("route-1");
            t.routeName = QStringLiteral("Flad");
            t.durationSec = seconds;
            t.distanceM = seconds * 9.0;
            t.trace = { { 0, 0 }, { double(seconds), seconds * 9.0 } };
            db.saveRouteTrace(t);
        }
        return id;
    }
    bool round(FakeServer &server)
    {
        QSignalSpy done(sync.get(), &RideSync::synced);
        QSignalSpy failed(sync.get(), &RideSync::changed);
        sync->serverAnswered(server.url(), server.id, server.rev);
        if (sync->state() != QLatin1String("syncing"))
            return sync->state() != QLatin1String("error");
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 10000 && sync->state() == QLatin1String("syncing"))
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return sync->state() == QLatin1String("idle");
    }
};

const QString kLone = QStringLiteral("c11fffc6-cc95-421a-a39d-b65743745edc");

} // namespace

class TestRideSync : public QObject
{
    Q_OBJECT
private slots:
    void sendsHistoryAndFetchesOtherStations()
    {
        FakeServer server;
        Station a(kLone), b(kLone);
        // station A: its history (two rides, one with a ghost trace) goes at the first look
        a.ride(120, 150);
        const qint64 withTrace = a.ride(60, 200, true);
        a.sync->rideSaved();
        QCOMPARE(a.sync->waiting(), 2);
        QVERIFY(a.round(server));
        QCOMPARE(server.rides.size(), 2);
        QCOMPARE(a.sync->waiting(), 0);
        const QJsonObject sent = server.rides.value(a.db.ride(withTrace)->uuid);
        QCOMPARE(sent.value(QStringLiteral("userUuid")).toString(), kLone);
        QCOMPARE(sent.value(QStringLiteral("samples")).toArray().size(), 60);
        QCOMPARE(sent.value(QStringLiteral("trace")).toObject().value(QStringLiteral("routeKey")).toString(), QStringLiteral("route-1"));
        // nothing new: nothing sent again
        QVERIFY(a.round(server));
        QCOMPARE(server.posts, 1);

        // station B: A's rides arrive in History, whole (samples, trace - the ghost can race them)
        QVERIFY(b.round(server));
        const QVector<RideRecord> rides = b.db.rides(b.profiles->profileId());
        QCOMPARE(rides.size(), 2);
        const qint64 fetched = b.db.rideIdForUuid(a.db.ride(withTrace)->uuid);
        QVERIFY(fetched >= 0);
        QCOMPARE(b.db.rideSamples(fetched).size(), 60);
        QCOMPARE(b.db.rideSamples(fetched).first().speedMps, 9.0);
        QVERIFY(!b.db.ride(fetched)->station.isEmpty());
        QCOMPARE(b.db.routeTraces(b.profiles->profileId(), QStringLiteral("gpx"), QStringLiteral("route-1"), 0, false).size(), 1);
        QCOMPARE(b.sync->waiting(), 0);   // (not sent back)
        QCOMPARE(server.posts, 1);

        // B rides: A gets it at its next look
        b.ride(90, 180);
        b.sync->rideSaved();
        QVERIFY(b.round(server));
        QCOMPARE(server.rides.size(), 3);
        QVERIFY(a.round(server));
        QCOMPARE(a.db.rides(a.profiles->profileId()).size(), 3);

        // deleted on A: gone on the server and on B
        QVERIFY(a.db.deleteRide(withTrace));
        a.sync->rideSaved();   // (History::rideDeleted in the app)
        QCOMPARE(a.sync->waiting(), 1);
        QVERIFY(a.round(server));
        QCOMPARE(a.sync->waiting(), 0);
        QVERIFY(server.rides.value(sent.value(QStringLiteral("uuid")).toString()).value(QStringLiteral("deleted")).toBool());
        QSignalSpy changed(b.sync.get(), &RideSync::ridesChanged);
        QVERIFY(b.round(server));
        QCOMPARE(b.db.rideIdForUuid(sent.value(QStringLiteral("uuid")).toString()), qint64(-1));
        QCOMPARE(b.db.rides(b.profiles->profileId()).size(), 2);
        QCOMPARE(changed.count(), 1);
        QVERIFY(b.db.rideTombstones().isEmpty());   // (deleted by the server: not told back)
    }

    void deletedWhileSendingStaysDeleted()
    {
        // a ride deleted here while it is being sent: the deletion goes next,
        // it does not come back from the server
        FakeServer server;
        Station a(kLone);
        const qint64 id = a.ride(60, 150);
        const QString uuid = a.db.ride(id)->uuid;
        a.sync->serverAnswered(server.url(), server.id, server.rev);
        QCOMPARE(a.sync->state(), QStringLiteral("syncing"));   // (the POST under way)
        QVERIFY(a.db.deleteRide(id));
        QTRY_COMPARE(a.sync->state(), QStringLiteral("idle"));
        QVERIFY(server.rides.contains(uuid));
        QCOMPARE(a.db.rideTombstones().size(), 1);   // (kept: the deletion was not sent yet)
        QVERIFY(a.round(server));
        QVERIFY(server.rides.value(uuid).value(QStringLiteral("deleted")).toBool());
        QCOMPARE(a.db.rideIdForUuid(uuid), qint64(-1));
        QVERIFY(a.db.rideTombstones().isEmpty());
    }

    void newProfileDuringARoundIsFetchedToo()
    {
        // a profile arriving while a round runs (UserSync on the first
        // connection): its rides are fetched afterwards, not skipped for good
        FakeServer server;
        Station a(kLone);
        a.ride(60, 150);
        a.ride(60, 160);
        QVERIFY(a.round(server));
        Station c;
        c.sync->serverAnswered(server.url(), server.id, server.rev);
        QCOMPARE(c.sync->state(), QStringLiteral("syncing"));
        Profile p;
        p.name = QStringLiteral("Lone");
        p.uuid = kLone;
        p.updatedMs = 1;
        const qint64 id = c.db.storeServerProfile(p);
        c.profiles->load();   // (profilesChanged while the round runs)
        QTRY_COMPARE_WITH_TIMEOUT(c.db.rides(id).size(), 2, 10000);
    }

    void nothingSentDuringARide()
    {
        // riding: the waiting rides wait (building them would cost the picture frames)
        FakeServer server;
        Station a(kLone);
        a.ride(60, 150);
        a.sync->rideSaved();
        a.riding = true;
        a.sync->serverAnswered(server.url(), server.id, server.rev);
        QTest::qWait(300);
        QCOMPARE(server.posts, 0);
        QCOMPARE(a.sync->waiting(), 1);
        a.riding = false;
        QVERIFY(a.round(server));
        QCOMPARE(server.posts, 1);
        QCOMPARE(a.sync->waiting(), 0);
    }

    void otherRidersAreGhosts()
    {
        // Anne's ride on the route (fetched from the server) is a ghost for Lone
        FakeServer server;
        Station a(kLone);
        Profile anne;
        anne.name = QStringLiteral("Anne");
        anne.uuid = QStringLiteral("f51db3d3-7c56-4aec-bc8d-720622dcdd2a");
        anne.updatedMs = 1;
        const qint64 anneId = a.db.storeServerProfile(anne);
        RideRecord r;
        r.profileId = anneId;
        r.startedAt = QDateTime::currentDateTime();
        r.uuid = QStringLiteral("a0000000-0000-4000-8000-0000000000aa");
        r.station = QStringLiteral("Pi 2");
        RouteTrace t;
        t.sport = QStringLiteral("gpx");
        t.routeKey = QStringLiteral("route-1");
        t.durationSec = 100;
        t.distanceM = 1000;
        t.createdAt = QDateTime::currentDateTime();
        t.trace = { { 0, 0 }, { 100, 1000 } };
        QVERIFY(a.db.storeServerRide(r, {}, {}, t) >= 0);

        RouteGhost ghost(&a.db, QStringLiteral("gpx"));
        ghost.setContext({ a.profiles->profileId(), QStringLiteral("route-1"), QStringLiteral("Flad"), 0, false });
        QCOMPARE(ghost.rideCount(), 1);
        const QVariantList choices = ghost.choices();
        QCOMPARE(choices.size(), 4);   // off, best, latest, Anne's
        QVERIFY(choices.last().toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("Anne · ")));
        // best / latest: one's own only (none)
        ghost.setChoice(QStringLiteral("best"));
        QVERIFY(!ghost.available());
        ghost.setChoice(choices.last().toMap().value(QStringLiteral("value")).toString());
        QVERIFY(ghost.available());
        QVERIFY(ghost.ghostLabel().startsWith(QStringLiteral("Anne · ")));
    }

    void unknownProfileWaitsAndLongListsInParts()
    {
        FakeServer server;
        server.pullLimit = 2;
        Station a(kLone);
        for (int i = 0; i < 5; ++i)
            a.ride(60, 100 + i);
        QVERIFY(a.round(server));
        QCOMPARE(server.rides.size(), 5);

        // a station without that profile: skipped; when the profile arrives, fetched (in parts of 2)
        Station c;
        QVERIFY(c.round(server));
        QCOMPARE(c.db.rides(c.profiles->profileId()).size(), 0);
        Profile p;
        p.name = QStringLiteral("Lone");
        p.uuid = kLone;
        p.updatedMs = 1;
        const qint64 id = c.db.storeServerProfile(p);
        c.profiles->load();   // (as UserSync does)
        QVERIFY(c.round(server));
        QCOMPARE(c.db.rides(id).size(), 5);
        QCOMPARE(server.fullGets, 5);
    }

    void notDuringARideAndKeptWhenUnreachable()
    {
        FakeServer server;
        Station a(kLone), b(kLone);
        a.ride(60, 150);
        QVERIFY(a.round(server));
        b.riding = true;
        QVERIFY(b.round(server));
        QCOMPARE(b.db.rides(b.profiles->profileId()).size(), 0);   // (after the ride)
        b.riding = false;
        QVERIFY(b.round(server));
        QCOMPARE(b.db.rides(b.profiles->profileId()).size(), 1);

        // no server: the ride waits
        a.ride(60, 160);
        a.sync->rideSaved();
        quint16 port;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }
        a.sync->serverAnswered(QStringLiteral("http://127.0.0.1:%1/").arg(port), server.id, 99);
        QTRY_COMPARE_WITH_TIMEOUT(a.sync->state(), QStringLiteral("error"), RideSync::kTimeoutMs + 2000);
        QCOMPARE(a.sync->waiting(), 1);
        QVERIFY(a.round(server));
        QCOMPARE(a.sync->waiting(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRideSync)
#include "tst_ridesync.moc"
