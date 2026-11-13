#include "UserSync.h"

#include "Database.h"
#include "DiagLog.h"
#include "ProfileManager.h"
#include "ServerConnection.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

namespace watt {

namespace {
bool isSha1(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{40}$"));
    return re.match(s).hasMatch();
}

// sha1 of a picture file ("" when there is none or it is too large to send)
QString fileSha1(const QString &path)
{
    QFile f(path);
    if (path.isEmpty() || f.size() <= 0 || f.size() > UserSync::kPictureMaxBytes || !f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QString pictureSuffix(const QByteArray &type)
{
    if (type.startsWith("image/png"))
        return QStringLiteral("png");
    if (type.startsWith("image/webp"))
        return QStringLiteral("webp");
    if (type.startsWith("image/gif"))
        return QStringLiteral("gif");
    return QStringLiteral("jpg");
}
} // namespace

UserSync::UserSync(Database *db, ProfileManager *profiles, ServerConnection *connection, const QString &pictureDir,
                   QObject *parent)
    : SyncTask(db, QStringLiteral("profiles"), QStringLiteral("server/users"), kTimeoutMs, parent)
    , m_profiles(profiles)
    , m_pictureDir(pictureDir)
{
    if (m_profiles) {
        // a profile changed here: counted, and sent soon when the server is there
        const auto touched = [this] {
            refreshWaiting();
            if (waiting() > 0)
                soon(1500);
        };
        connect(m_profiles, &ProfileManager::profileChanged, this, touched);
        connect(m_profiles, &ProfileManager::profilesChanged, this, touched);
        setBusyCheck([this] { return m_profiles->busy(); });
    }
    follow(connection, [](const ServerConnection *c) { return c->usersRev(); });
    refreshWaiting();
}

int UserSync::countWaiting() const
{
    return m_db ? m_db->profilesWaiting() : 0;
}

void UserSync::resendAll()
{
    m_db->markAllProfilesToSend();
}

void UserSync::round()
{
    m_profilesTouched = false;
    m_toSend = m_db->profilesToSend();
    m_tombstones = m_db->profileTombstones();
    m_sendHash.clear();
    sendPictures(0);
}

void UserSync::roundEnded()
{
    if (m_profilesTouched && m_profiles) {
        m_profilesTouched = false;
        m_profiles->load();   // (the current profile and the list as stored now)
    }
}

// 1. the pictures of the profiles to send that the server does not have yet
void UserSync::sendPictures(int index)
{
    for (; index < m_toSend.size(); ++index) {
        const Profile &p = m_toSend.at(index);
        const QString hash = fileSha1(p.avatarPath);
        m_sendHash.insert(p.uuid, hash);
        if (hash.isEmpty() || hash == p.pictureHash)
            continue;   // (none, or the server has it)
        QFile f(p.avatarPath);
        if (!f.open(QIODevice::ReadOnly)) {
            m_sendHash.insert(p.uuid, QString());
            continue;
        }
        const QByteArray data = f.readAll();
        request(QStringLiteral("POST"), QStringLiteral("api/picture.php"), data, "application/octet-stream",
                [this, index, hash](QNetworkReply *reply) {
                    const QString got = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("hash")).toString();
                    if (got != hash) {
                        finish(QStringLiteral("the server did not take a profile picture"));
                        return;
                    }
                    sendPictures(index + 1);
                });
        return;
    }
    sendProfiles();
}

QJsonObject UserSync::profileJson(const Profile &p, const QString &pictureHash) const
{
    QJsonArray zones;
    for (double z : p.zones.upper)
        zones.append(z);
    QJsonObject o {
        { QStringLiteral("uuid"), p.uuid },
        { QStringLiteral("name"), p.name },
        { QStringLiteral("weightKg"), p.weightKg },
        { QStringLiteral("heightCm"), p.heightCm },
        { QStringLiteral("ftp"), p.ftp },
        { QStringLiteral("rowingFtp"), p.rowingFtp },
        { QStringLiteral("maxHr"), p.maxHr },
        { QStringLiteral("restHr"), p.restHr },
        { QStringLiteral("cadence"), p.preferredCadence },
        { QStringLiteral("defaultErg"), p.defaultErg },
        { QStringLiteral("zones"), zones },
        { QStringLiteral("pictureHash"), pictureHash.isEmpty() ? QJsonValue() : QJsonValue(pictureHash) },
        { QStringLiteral("deleted"), false },
        { QStringLiteral("updatedMs"), p.updatedMs },
    };
    for (const QString &kind : ProfileManager::avatarKinds()) {
        const QByteArray json = m_db->setting(ProfileManager::avatarSettingKey(kind, p.id)).toString().toUtf8();
        o.insert(kind, QJsonObject::fromVariantMap(
                           ProfileManager::cleanAvatar3d(QJsonDocument::fromJson(json).object().toVariantMap())));
    }
    return o;
}

// 2. the changed and deleted profiles
void UserSync::sendProfiles()
{
    if (m_toSend.isEmpty() && m_tombstones.isEmpty()) {
        fetch();
        return;
    }
    QJsonArray users;
    for (const Profile &p : m_toSend)
        users.append(profileJson(p, m_sendHash.value(p.uuid)));
    for (const auto &[uuid, when] : m_tombstones)
        users.append(QJsonObject { { QStringLiteral("uuid"), uuid }, { QStringLiteral("deleted"), true },
                                   { QStringLiteral("updatedMs"), when } });
    const QJsonObject body {
        { QStringLiteral("station"), m_db->stationId() },
        { QStringLiteral("name"), QSysInfo::machineHostName().left(90) },
        { QStringLiteral("nowMs"), QDateTime::currentMSecsSinceEpoch() },
        { QStringLiteral("users"), users },
    };
    request(QStringLiteral("POST"), QStringLiteral("api/users.php"), QJsonDocument(body).toJson(QJsonDocument::Compact),
            "application/json", [this](QNetworkReply *reply) {
                const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
                if (!o.contains(QStringLiteral("results"))) {
                    finish(QStringLiteral("the server did not take the profiles"));
                    return;
                }
                // (answered for all of them: each is sent - taken, or the server's newer one comes back)
                for (const Profile &p : m_toSend)
                    m_db->markProfileSent(p.uuid, p.updatedMs, m_sendHash.value(p.uuid));
                for (const auto &t : m_tombstones)
                    m_db->removeProfileTombstone(t.first);
                for (const QJsonValue &v : o.value(QStringLiteral("results")).toArray()) {
                    const QJsonObject r = v.toObject();
                    if (r.contains(QStringLiteral("error")))
                        DiagLog::write("SYNC", QStringLiteral("profiles: %1 refused: %2")
                                                   .arg(r.value(QStringLiteral("uuid")).toString(),
                                                        r.value(QStringLiteral("error")).toString()));
                    if (r.contains(QStringLiteral("mergedInto")))   // (joined to the server's rider of that name)
                        m_profilesTouched |= applyUser(QJsonObject { { QStringLiteral("uuid"), r.value(QStringLiteral("uuid")) },
                                                                     { QStringLiteral("deleted"), true },
                                                                     { QStringLiteral("mergedInto"), r.value(QStringLiteral("mergedInto")) } });
                    if (!r.value(QStringLiteral("taken")).toBool() && r.contains(QStringLiteral("user")))
                        m_profilesTouched |= applyUser(r.value(QStringLiteral("user")).toObject());
                }
                DiagLog::write("SYNC", QStringLiteral("profiles: sent %1, deleted %2")
                                           .arg(m_toSend.size()).arg(m_tombstones.size()));
                setServerRev(o.value(QStringLiteral("rev")).toInteger(serverRev()));   // (now: with these)
                refreshWaiting();
                fetch();
            });
}

// 3. what changed on the server since the last look
void UserSync::fetch()
{
    m_needPictures.clear();
    const qint64 since = storedRev();
    if (busy()) {
        finish();   // (not during a ride: the rider must stay as it is; next time)
        return;
    }
    if (serverRev() >= 0 && serverRev() == since && !m_profilesTouched) {
        finish();
        return;
    }
    request(QStringLiteral("GET"), QStringLiteral("api/users.php?since=%1").arg(since), {}, {},
            [this](QNetworkReply *reply) {
                const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
                if (o.value(QStringLiteral("serverId")).toString() != serverId() || !o.contains(QStringLiteral("users"))) {
                    finish(QStringLiteral("the server's user list is not readable"));
                    return;
                }
                const QJsonArray users = o.value(QStringLiteral("users")).toArray();
                bool anyUser = false;
                for (const QJsonValue &v : users) {
                    m_profilesTouched |= applyUser(v.toObject());
                    anyUser |= !v.toObject().value(QStringLiteral("deleted")).toBool();
                }
                if (anyUser)
                    removeStarterProfiles();
                m_fetchedRev = o.value(QStringLiteral("rev")).toInteger(-1);
                fetchPictures(0);
            });
}

bool UserSync::applyUser(const QJsonObject &u)
{
    const QString uuid = u.value(QStringLiteral("uuid")).toString().toLower();
    if (QUuid(uuid).isNull())
        return false;
    const qint64 id = m_db->profileIdForUuid(uuid);
    const std::optional<Profile> local = id >= 0 ? m_db->profile(id) : std::nullopt;

    // the same rider made on two stations: the server keeps one - this
    // station's copy becomes that one (its rides go with it)
    const QString into = u.value(QStringLiteral("mergedInto")).toString().toLower();
    if (u.value(QStringLiteral("deleted")).toBool() && !QUuid(into).isNull() && into != uuid) {
        if (!local)
            return false;
        const qint64 keep = m_db->profileIdForUuid(into);
        if (keep >= 0) {
            m_db->mergeProfile(id, keep);   // (its rides, records ... to the one here; the copy goes)
            for (const QString &kind : ProfileManager::avatarKinds())
                m_db->removeSetting(ProfileManager::avatarSettingKey(kind, id));
            if (m_db->setting(QStringLiteral("currentProfile")).toLongLong() == id)
                m_db->setSetting(QStringLiteral("currentProfile"), keep);
        } else {
            m_db->setProfileUuid(id, into);   // (the server's version follows: storeServerProfile by uuid)
        }
        DiagLog::write("SYNC", QStringLiteral("profiles: %1 is the same rider as on the server - joined").arg(local->name));
        return true;
    }

    if (local && local->dirty)
        return false;   // (changed here again: it is sent next, the newest wins there)

    if (u.value(QStringLiteral("deleted")).toBool()) {
        if (!local)
            return false;
        if (m_db->profileRideCount(id) > 0 || m_db->profiles().size() <= 1) {
            DiagLog::write("SYNC", QStringLiteral("profiles: %1 deleted on the server - kept here (%2)")
                                       .arg(local->name, m_db->profiles().size() <= 1 ? QStringLiteral("the only profile")
                                                                                      : QStringLiteral("it has rides")));
            return false;
        }
        m_db->deleteProfile(id, false);
        for (const QString &kind : ProfileManager::avatarKinds())
            m_db->removeSetting(ProfileManager::avatarSettingKey(kind, id));
        DiagLog::write("SYNC", QStringLiteral("profiles: %1 deleted (on the server)").arg(local->name));
        return true;
    }

    Profile p = local ? *local : Profile();
    p.uuid = uuid;
    // (no markup: a name is shown in many places)
    p.name = u.value(QStringLiteral("name")).toString().remove(QLatin1Char('<')).remove(QLatin1Char('>')).trimmed().left(100);
    p.weightKg = u.value(QStringLiteral("weightKg")).toDouble();
    p.heightCm = u.value(QStringLiteral("heightCm")).toDouble();
    p.ftp = u.value(QStringLiteral("ftp")).toInt();
    p.rowingFtp = u.value(QStringLiteral("rowingFtp")).toInt();
    p.maxHr = u.value(QStringLiteral("maxHr")).toInt();
    p.restHr = u.value(QStringLiteral("restHr")).toInt();
    p.preferredCadence = u.value(QStringLiteral("cadence")).toInt();
    p.defaultErg = u.value(QStringLiteral("defaultErg")).toBool();
    const QJsonArray zones = u.value(QStringLiteral("zones")).toArray();
    if (zones.size() == kZoneCount - 1)
        p.zones = ZoneSet::fromJson(zones);
    p.updatedMs = u.value(QStringLiteral("updatedMs")).toInteger();
    // the picture: none, the one here already, or fetched after this
    const QString hash = u.value(QStringLiteral("pictureHash")).toString();
    if (!isSha1(hash)) {
        if (!p.pictureHash.isEmpty() || !local)
            p.avatarPath.clear();
        p.pictureHash.clear();
    } else if (!(local && local->pictureHash == hash && QFileInfo::exists(local->avatarPath))) {
        m_needPictures.append({ uuid, hash });
    }
    QString error;
    if (!p.isValid(&error)) {
        DiagLog::write("SYNC", QStringLiteral("profiles: %1 from the server not taken: %2").arg(p.name, error));
        return false;
    }
    const qint64 stored = m_db->storeServerProfile(p);
    if (stored < 0)
        return false;
    for (const QString &kind : ProfileManager::avatarKinds()) {
        const QVariantMap clean = ProfileManager::cleanAvatar3d(u.value(kind).toObject().toVariantMap());
        m_db->setSetting(ProfileManager::avatarSettingKey(kind, stored),
                         QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(clean)).toJson(QJsonDocument::Compact)));
    }
    return true;
}

// The station's starter profile (never changed, no rides) goes when the
// server's users arrive - unless it is the only one.
void UserSync::removeStarterProfiles()
{
    for (const Profile &p : m_db->profiles()) {
        if (p.updatedMs == 0 && m_db->profiles().size() > 1 && m_db->profileRideCount(p.id) == 0) {
            m_db->deleteProfile(p.id, false);
            for (const QString &kind : ProfileManager::avatarKinds())
                m_db->removeSetting(ProfileManager::avatarSettingKey(kind, p.id));
            m_profilesTouched = true;
        }
    }
}

// 4. the pictures of the fetched users this station does not have
void UserSync::fetchPictures(int index)
{
    if (index >= m_needPictures.size()) {
        if (m_fetchedRev >= 0)   // (only now: a picture missing is fetched again next time)
            storeRev(m_fetchedRev);
        finish();
        return;
    }
    const QString uuid = m_needPictures.at(index).first;
    const QString hash = m_needPictures.at(index).second;
    request(QStringLiteral("GET"), QStringLiteral("api/picture.php?hash=%1").arg(hash), {}, {},
            [this, index, uuid, hash](QNetworkReply *reply) {
                const QByteArray data = reply->readAll();
                if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) != hash) {
                    finish(QStringLiteral("a profile picture arrived damaged"));
                    return;
                }
                QDir().mkpath(m_pictureDir);
                const QString path = QDir(m_pictureDir).filePath(
                    hash + QLatin1Char('.') + pictureSuffix(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray()));
                QSaveFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
                    finish(QStringLiteral("a profile picture could not be saved in %1").arg(m_pictureDir));
                    return;
                }
                const qint64 id = m_db->profileIdForUuid(uuid);
                if (std::optional<Profile> p = id >= 0 ? m_db->profile(id) : std::nullopt; p && !p->dirty) {
                    p->avatarPath = path;
                    p->pictureHash = hash;
                    m_db->storeServerProfile(*p);
                    m_profilesTouched = true;
                }
                fetchPictures(index + 1);
            });
}

} // namespace watt
