// The profiles kept with the server (UserSync, docs/server-plan.md step 3) -
// against a small in-memory stand-in for the server's api/users.php and
// api/picture.php (the newest change wins, as in server/lib/users.php).
#include "Database.h"
#include "ProfileManager.h"
#include "UserSync.h"

#include <QCryptographicHash>
#include <QFile>
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
    QMap<QString, QJsonObject> users;          // uuid -> user (with rev)
    QMap<QString, QByteArray> pictures;        // sha1 -> bytes
    int posts = 0, gets = 0, pictureGets = 0, picturePosts = 0;

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

    // a change made on the server (the web interface)
    void edit(const QString &uuid, const QJsonObject &values)
    {
        QJsonObject u = users.value(uuid);
        for (auto it = values.begin(); it != values.end(); ++it)
            u.insert(it.key(), it.value());
        u.insert(QStringLiteral("uuid"), uuid);
        u.insert(QStringLiteral("updatedMs"), QDateTime::currentMSecsSinceEpoch());
        u.insert(QStringLiteral("rev"), ++rev);
        users.insert(uuid, u);
    }
    QString addPicture(const QByteArray &data)
    {
        const QString h = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
        pictures.insert(h, data);
        return h;
    }

private:
    QHash<QTcpSocket *, QByteArray> buf;

    void onData(QTcpSocket *s)
    {
        QByteArray &b = buf[s];
        b += s->readAll();
        const int end = b.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = b.left(end);
        int length = 0;
        for (const QByteArray &line : head.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        if (b.size() < end + 4 + length)
            return;
        const QByteArray body = b.mid(end + 4, length);
        const QList<QByteArray> first = head.split('\n').first().trimmed().split(' ');
        buf.remove(s);
        const QUrl url(QString::fromLatin1(first.value(1)));
        QByteArray type = "application/json", out;
        int status = 200;
        handle(first.value(0), url, body, status, type, out);
        s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + type + "\r\nContent-Length: "
                 + QByteArray::number(out.size()) + "\r\nConnection: close\r\n\r\n" + out);
        s->disconnectFromHost();
    }

    void handle(const QByteArray &method, const QUrl &url, const QByteArray &body, int &status, QByteArray &type, QByteArray &out)
    {
        const QUrlQuery query(url);
        if (url.path() == QLatin1String("/api/users.php") && method == "GET") {
            ++gets;
            const qint64 since = query.queryItemValue(QStringLiteral("since")).toLongLong();
            QJsonArray list;
            for (const QJsonObject &u : users)
                if (u.value(QStringLiteral("rev")).toInteger() > since)
                    list.append(u);
            out = QJsonDocument(QJsonObject { { QStringLiteral("serverId"), id }, { QStringLiteral("rev"), rev },
                                              { QStringLiteral("users"), list } })
                      .toJson();
        } else if (url.path() == QLatin1String("/api/users.php") && method == "POST") {
            ++posts;
            QJsonArray results;
            for (const QJsonValue &v : QJsonDocument::fromJson(body).object().value(QStringLiteral("users")).toArray()) {
                QJsonObject u = v.toObject();
                const QString uuid = u.value(QStringLiteral("uuid")).toString();
                const bool known = users.contains(uuid);
                bool take = !known || u.value(QStringLiteral("updatedMs")).toInteger()
                                          > users.value(uuid).value(QStringLiteral("updatedMs")).toInteger();
                if (take && u.value(QStringLiteral("deleted")).toBool() && !known)
                    take = false;
                const QString hash = u.value(QStringLiteral("pictureHash")).toString();
                if (take && !hash.isEmpty() && !pictures.contains(hash))
                    take = false;
                if (take) {
                    if (u.value(QStringLiteral("deleted")).toBool()) {
                        QJsonObject t = users.value(uuid);
                        t.insert(QStringLiteral("deleted"), true);
                        t.insert(QStringLiteral("updatedMs"), u.value(QStringLiteral("updatedMs")));
                        u = t;
                    }
                    u.insert(QStringLiteral("rev"), ++rev);
                    users.insert(uuid, u);
                }
                QJsonObject r { { QStringLiteral("uuid"), uuid }, { QStringLiteral("taken"), take } };
                if (!take && known)
                    r.insert(QStringLiteral("user"), users.value(uuid));
                results.append(r);
            }
            out = QJsonDocument(QJsonObject { { QStringLiteral("serverId"), id }, { QStringLiteral("rev"), rev },
                                              { QStringLiteral("results"), results } })
                      .toJson();
        } else if (url.path() == QLatin1String("/api/picture.php") && method == "POST") {
            ++picturePosts;
            out = QJsonDocument(QJsonObject { { QStringLiteral("hash"), addPicture(body) } }).toJson();
        } else if (url.path() == QLatin1String("/api/picture.php") && method == "GET") {
            ++pictureGets;
            const QString h = query.queryItemValue(QStringLiteral("hash"));
            if (pictures.contains(h)) {
                type = "image/png";
                out = pictures.value(h);
            } else {
                status = 404;
            }
        } else {
            status = 404;
        }
    }
};

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<UserSync> sync;
    FakeServer server;

    Fixture()
    {
        db.open(dir.filePath(QStringLiteral("s.sqlite")));
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        sync = std::make_unique<UserSync>(&db, profiles.get(), nullptr, dir.filePath(QStringLiteral("pictures")));
    }
    // one round, as after a ping
    bool round()
    {
        QSignalSpy done(sync.get(), &UserSync::synced);
        QSignalSpy changed(sync.get(), &UserSync::changed);
        sync->serverAnswered(server.url(), server.id, server.rev);
        if (sync->state() != QLatin1String("syncing"))
            return sync->state() != QLatin1String("error");
        return done.wait(5000) || done.count() > 0;
    }
};

} // namespace

class TestUserSync : public QObject
{
    Q_OBJECT
private slots:
    void starterProfileStaysUntilServerUsersArrive()
    {
        // a new station: its starter profile is not sent; the server's users replace it
        Fixture f;
        QCOMPARE(f.sync->waiting(), 0);
        QVERIFY(f.round());
        QVERIFY(f.server.users.isEmpty());
        QCOMPARE(f.db.profiles().size(), 1);

        const QString uuid = QStringLiteral("c11fffc6-cc95-421a-a39d-b65743745edc");
        f.server.edit(uuid, { { QStringLiteral("name"), QStringLiteral("Lone") }, { QStringLiteral("weightKg"), 68 },
                              { QStringLiteral("heightCm"), 170 }, { QStringLiteral("ftp"), 110 },
                              { QStringLiteral("rowingFtp"), 150 }, { QStringLiteral("maxHr"), 185 },
                              { QStringLiteral("restHr"), 60 }, { QStringLiteral("cadence"), 85 },
                              { QStringLiteral("defaultErg"), true },
                              { QStringLiteral("zones"), QJsonArray { 0.5, 0.7, 0.85, 0.95, 1.1 } },
                              { QStringLiteral("avatar3d"), QJsonObject { { QStringLiteral("model"), QStringLiteral("female") },
                                                                          { QStringLiteral("jersey"), QStringLiteral("#ff0000") } } },
                              { QStringLiteral("deleted"), false } });
        QVERIFY(f.round());
        const QVector<Profile> all = f.db.profiles();
        QCOMPARE(all.size(), 1);   // (the starter went)
        QCOMPARE(all.first().uuid, uuid);
        QCOMPARE(all.first().ftp, 110);
        QCOMPARE(all.first().heightCm, 170.0);
        QCOMPARE(all.first().zones.upper[0], 0.5);
        QVERIFY(!all.first().dirty);
        // the current profile is the server's, with its 3D figure
        QCOMPARE(f.profiles->name(), QStringLiteral("Lone"));
        QCOMPARE(f.profiles->avatar3d().value(QStringLiteral("model")).toString(), QStringLiteral("female"));
        QCOMPARE(f.profiles->avatar3d().value(QStringLiteral("jersey")).toString(), QStringLiteral("#ff0000"));
        QCOMPARE(f.sync->waiting(), 0);
        QCOMPARE(f.server.posts, 0);   // (nothing of the station's was sent)
    }

    void sendsChangesWithPicturesAndTakesTheServers()
    {
        Fixture f;
        // the station's own profile, edited (with a picture): sent at the first round
        QFile pic(f.dir.filePath(QStringLiteral("me.png")));
        QVERIFY(pic.open(QIODevice::WriteOnly));
        pic.write(QByteArray("\x89PNG\r\n\x1a\nfake-picture"));
        pic.close();
        QVERIFY(f.profiles->saveProfile({ { QStringLiteral("name"), QStringLiteral("Anne") }, { QStringLiteral("ftp"), 95 },
                                          { QStringLiteral("avatarUrl"), QUrl::fromLocalFile(pic.fileName()) },
                                          { QStringLiteral("runner3d"), QVariantMap { { QStringLiteral("model"), QStringLiteral("female") } } } }));
        QCOMPARE(f.sync->waiting(), 1);
        QVERIFY(f.round());
        QCOMPARE(f.server.posts, 1);
        QCOMPARE(f.server.picturePosts, 1);
        const QString uuid = f.profiles->current().uuid;
        QVERIFY(f.server.users.contains(uuid));
        const QJsonObject sent = f.server.users.value(uuid);
        QCOMPARE(sent.value(QStringLiteral("name")).toString(), QStringLiteral("Anne"));
        QCOMPARE(sent.value(QStringLiteral("ftp")).toInt(), 95);
        QCOMPARE(sent.value(QStringLiteral("runner3d")).toObject().value(QStringLiteral("model")).toString(), QStringLiteral("female"));
        const QString hash = sent.value(QStringLiteral("pictureHash")).toString();
        QVERIFY(f.server.pictures.contains(hash));
        QCOMPARE(f.sync->waiting(), 0);
        QCOMPARE(f.db.profile(f.profiles->profileId())->pictureHash, hash);

        // nothing changed: nothing sent again, the picture not again either
        QVERIFY(f.round());
        QCOMPARE(f.server.posts, 1);
        f.profiles->saveProfile({ { QStringLiteral("ftp"), 97 } });
        QVERIFY(f.round());
        QCOMPARE(f.server.posts, 2);
        QCOMPARE(f.server.picturePosts, 1);

        // changed on the server (a new picture): taken here, the picture fetched
        const QString newPic = f.server.addPicture(QByteArray("\x89PNG\r\n\x1a\nother"));
        QTest::qWait(5);
        f.server.edit(uuid, { { QStringLiteral("ftp"), 120 }, { QStringLiteral("pictureHash"), newPic } });
        QVERIFY(f.round());
        QCOMPARE(f.profiles->ftp(), 120);
        QCOMPARE(f.server.pictureGets, 1);
        const Profile here = *f.db.profile(f.profiles->profileId());
        QCOMPARE(here.pictureHash, newPic);
        QVERIFY(here.avatarPath.startsWith(f.dir.filePath(QStringLiteral("pictures"))));
        QFile got(here.avatarPath);
        QVERIFY(got.open(QIODevice::ReadOnly));
        QCOMPARE(got.readAll(), QByteArray("\x89PNG\r\n\x1a\nother"));
        QVERIFY(!here.dirty);
        QCOMPARE(f.server.posts, 2);   // (taking the server's version sends nothing back)

        // an older change from here loses: the server's version comes back
        Profile stale = here;
        stale.ftp = 77;
        f.db.saveProfile(stale);
        QTest::qWait(5);
        f.server.edit(uuid, { { QStringLiteral("ftp"), 130 } });
        f.server.users[uuid][QStringLiteral("updatedMs")] = QDateTime::currentMSecsSinceEpoch() + 60000;
        QVERIFY(f.round());
        f.profiles->load();
        QCOMPARE(f.profiles->ftp(), 130);
        QVERIFY(!f.db.profile(f.profiles->profileId())->dirty);
    }

    void deletions()
    {
        Fixture f;
        // two profiles here, sent
        f.profiles->saveProfile({ { QStringLiteral("name"), QStringLiteral("Lone") } });
        const qint64 anne = f.profiles->createProfileWith({ { QStringLiteral("name"), QStringLiteral("Anne") } });
        const qint64 bo = f.profiles->createProfileWith({ { QStringLiteral("name"), QStringLiteral("Bo") } });
        QVERIFY(anne > 0 && bo > 0);
        QVERIFY(f.round());
        QCOMPARE(f.server.users.size(), 3);
        const QString anneUuid = f.db.profile(anne)->uuid, boUuid = f.db.profile(bo)->uuid;

        // deleted here: the server is told
        QVERIFY(f.profiles->deleteProfile(bo));
        QCOMPARE(f.sync->waiting(), 1);
        QVERIFY(f.round());
        QVERIFY(f.server.users.value(boUuid).value(QStringLiteral("deleted")).toBool());
        QCOMPARE(f.sync->waiting(), 0);

        // deleted on the server: gone here - unless it has rides here
        RideRecord ride;
        ride.profileId = anne;
        ride.startedAt = QDateTime::currentDateTime();
        QVERIFY(f.db.saveRide(ride, {}) > 0);
        QTest::qWait(5);
        f.server.edit(anneUuid, { { QStringLiteral("deleted"), true } });
        QVERIFY(f.round());
        QVERIFY(f.db.profile(anne));   // (kept: it has a ride here)
        const QStringList others = f.server.users.keys();
        for (const QString &uuid : others) {
            if (uuid != anneUuid && uuid != boUuid) {   // (Lone)
                QTest::qWait(5);
                f.server.edit(uuid, { { QStringLiteral("deleted"), true } });
            }
        }
        QVERIFY(f.round());
        // Lone (no rides) deleted - Anne still here
        QCOMPARE(f.db.profiles().size(), 1);
        QCOMPARE(f.db.profiles().first().id, anne);
        QCOMPARE(f.profiles->profileId(), anne);   // (the current one moved to one still here)
    }

    void anotherServerGetsEverything()
    {
        Fixture f;
        f.profiles->saveProfile({ { QStringLiteral("ftp"), 200 } });
        QVERIFY(f.round());
        QCOMPARE(f.server.posts, 1);
        // the server reinstalled: a new id, no users
        f.server.id = QStringLiteral("server-2");
        f.server.users.clear();
        f.server.rev = 0;
        QVERIFY(f.round());
        QCOMPARE(f.server.posts, 2);
        QCOMPARE(f.server.users.size(), 1);
        QCOMPARE(f.server.users.first().value(QStringLiteral("ftp")).toInt(), 200);
    }

    void unreachableKeepsChanges()
    {
        Fixture f;
        f.profiles->saveProfile({ { QStringLiteral("ftp"), 210 } });
        // a server address nobody answers at
        quint16 port;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }
        QSignalSpy changed(f.sync.get(), &UserSync::changed);
        f.sync->serverAnswered(QStringLiteral("http://127.0.0.1:%1/").arg(port), QStringLiteral("x"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(f.sync->state(), QStringLiteral("error"), UserSync::kTimeoutMs + 2000);
        QVERIFY(!f.sync->message().isEmpty());
        QCOMPARE(f.sync->waiting(), 1);   // (still to send)
        // the real server: sent
        QVERIFY(f.round());
        QCOMPARE(f.sync->waiting(), 0);
        QCOMPARE(f.server.users.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestUserSync)
#include "tst_usersync.moc"
