#include "RideSync.h"

#include "Database.h"
#include "DiagLog.h"
#include "ProfileManager.h"
#include "ServerConnection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

#include <cmath>

namespace watt {

namespace {
double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

RouteTrace traceFromJson(const QJsonObject &o)
{
    RouteTrace t;
    t.uuid = o.value(QStringLiteral("uuid")).toString();
    t.sport = o.value(QStringLiteral("sport")).toString() == QLatin1String("run") ? QStringLiteral("run") : QStringLiteral("gpx");
    t.routeKey = o.value(QStringLiteral("routeKey")).toString();
    t.routeName = o.value(QStringLiteral("routeName")).toString();
    t.startM = o.value(QStringLiteral("startM")).toDouble();
    t.loop = o.value(QStringLiteral("loop")).toBool();
    t.completed = o.value(QStringLiteral("completed")).toBool();
    t.durationSec = o.value(QStringLiteral("durationSec")).toDouble();
    t.distanceM = o.value(QStringLiteral("distanceM")).toDouble();
    t.createdAt = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    for (const QJsonValue &v : o.value(QStringLiteral("points")).toArray()) {
        const QJsonArray p = v.toArray();
        if (p.size() >= 2)
            t.trace.append({ p.at(0).toDouble(), p.at(1).toDouble() });
    }
    return t;
}
} // namespace

RideSync::RideSync(Database *db, ProfileManager *profiles, ServerConnection *connection, QObject *parent)
    : SyncTask(db, QStringLiteral("rides"), QStringLiteral("server/rides"), kTimeoutMs, parent)
    , m_profiles(profiles)
{
    for (const Profile &p : m_db->profiles())
        m_profileUuids.insert(p.uuid);
    if (m_profiles)
        connect(m_profiles, &ProfileManager::profilesChanged, this, &RideSync::checkNewProfiles);
    follow(connection, [](const ServerConnection *c) { return c->ridesRev(); });
    refreshWaiting();
}

int RideSync::countWaiting() const
{
    return m_db->ridesWaiting();
}

void RideSync::resendAll()
{
    m_db->markAllRidesToSend();
}

// A profile that was not here before (from the server): its rides may have
// been skipped - the list again from the start (only missing ones are fetched).
void RideSync::checkNewProfiles()
{
    bool added = false;
    for (const Profile &p : m_db->profiles()) {
        if (!m_profileUuids.contains(p.uuid)) {
            m_profileUuids.insert(p.uuid);
            added = true;
        }
    }
    if (added)
        restartFetch();
    refreshWaiting();
}

void RideSync::rideSaved()
{
    refreshWaiting();
    soon(5000);   // (after rideSaved the session still writes the ghost trace etc.)
}

void RideSync::round()
{
    m_ridesTouched = false;
    sendTombstones();
}

void RideSync::roundEnded()
{
    if (m_ridesTouched) {
        m_ridesTouched = false;
        emit ridesChanged();
    }
}

QByteArray RideSync::rideJson(qint64 rideId) const
{
    const std::optional<RideRecord> r = m_db->ride(rideId);
    const QString user = r ? m_db->uuidOf(QStringLiteral("profiles"), r->profileId) : QString();
    if (!r || user.isEmpty())
        return {};
    QJsonArray zones;
    for (int z : r->zoneSeconds)
        zones.append(z);
    QJsonArray samples;
    for (const Sample &s : m_db->rideSamples(rideId))
        samples.append(QJsonArray { s.t, s.power, s.targetPower, s.cadence, s.heartRate,
                                    s.speedMps > 0 ? QJsonValue(round2(s.speedMps)) : QJsonValue() });
    QJsonArray laps;
    for (const RideLap &l : m_db->rideLaps(rideId))
        laps.append(QJsonArray { l.lap, round2(l.timeSec), std::round(l.distanceM * 10) / 10 });
    QJsonObject o {
        { QStringLiteral("uuid"), r->uuid },
        { QStringLiteral("userUuid"), user },
        { QStringLiteral("sport"), r->sport },
        { QStringLiteral("workoutName"), r->workoutName },
        { QStringLiteral("startedAt"), r->startedAt.toSecsSinceEpoch() },
        { QStringLiteral("durationSec"), r->durationSec },
        { QStringLiteral("blocks"), r->blocks },
        { QStringLiteral("erg"), r->ergMode },
        { QStringLiteral("ftp"), r->ftp },
        { QStringLiteral("avgPower"), round2(r->avgPower) },
        { QStringLiteral("maxPower"), r->maxPower },
        { QStringLiteral("np"), round2(r->normalizedPower) },
        { QStringLiteral("avgHr"), round2(r->avgHeartRate) },
        { QStringLiteral("maxHr"), r->maxHeartRate },
        { QStringLiteral("avgCadence"), round2(r->avgCadence) },
        { QStringLiteral("kj"), round2(r->kilojoules) },
        { QStringLiteral("ifValue"), round2(r->intensityFactor) },
        { QStringLiteral("tss"), round2(r->tss) },
        { QStringLiteral("zones"), zones },
        { QStringLiteral("distanceM"), round2(r->distanceM) },
        { QStringLiteral("samples"), samples },
        { QStringLiteral("laps"), laps },
    };
    if (const std::optional<RouteTrace> t = m_db->routeTraceForRide(rideId); t && t->trace.size() > 1) {
        QJsonArray points;
        for (const TracePoint &p : t->trace)
            points.append(QJsonArray { std::round(p.t * 10) / 10, std::round(p.d * 10) / 10 });
        o.insert(QStringLiteral("trace"), QJsonObject {
                                              { QStringLiteral("uuid"), t->uuid },
                                              { QStringLiteral("sport"), t->sport },
                                              { QStringLiteral("routeKey"), t->routeKey },
                                              { QStringLiteral("routeName"), t->routeName },
                                              { QStringLiteral("startM"), t->startM },
                                              { QStringLiteral("loop"), t->loop },
                                              { QStringLiteral("completed"), t->completed },
                                              { QStringLiteral("durationSec"), t->durationSec },
                                              { QStringLiteral("distanceM"), t->distanceM },
                                              { QStringLiteral("createdAt"), t->createdAt.toUTC().toString(Qt::ISODate) },
                                              { QStringLiteral("points"), points },
                                          });
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// The server's answer to a POST: sent ones marked; deleted elsewhere: deleted
// here too. Only a deletion that was sent removes its tombstone (a ride
// deleted here while it was being sent keeps it: the deletion goes next).
bool RideSync::applyResults(const QJsonObject &o, bool deletions)
{
    if (!o.contains(QStringLiteral("results")))
        return false;
    for (const QJsonValue &v : o.value(QStringLiteral("results")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString uuid = r.value(QStringLiteral("uuid")).toString();
        if (!m_sent.contains(uuid))
            continue;
        if (r.contains(QStringLiteral("error")))
            DiagLog::write("SYNC", QStringLiteral("rides: %1 refused: %2").arg(uuid, r.value(QStringLiteral("error")).toString()));
        if (deletions) {
            m_db->removeRideTombstone(uuid);
            continue;
        }
        if (r.value(QStringLiteral("deleted")).toBool()) {
            const qint64 id = m_db->rideIdForUuid(uuid);
            if (id >= 0 && m_db->deleteRide(id, false))
                m_ridesTouched = true;
        } else if (r.value(QStringLiteral("taken")).toBool()) {
            m_db->markRideSent(uuid);
        }
    }
    return true;
}

// 1. the deletions
void RideSync::sendTombstones()
{
    const QVector<QPair<QString, qint64>> tombstones = m_db->rideTombstones();
    if (tombstones.isEmpty()) {
        sendRides();
        return;
    }
    QJsonArray rides;
    m_sent.clear();
    for (const auto &t : tombstones) {
        rides.append(QJsonObject { { QStringLiteral("uuid"), t.first }, { QStringLiteral("deleted"), true } });
        m_sent << t.first;
    }
    const QJsonObject body { { QStringLiteral("station"), m_db->stationId() },
                             { QStringLiteral("name"), QSysInfo::machineHostName().left(90) },
                             { QStringLiteral("rides"), rides } };
    request(QStringLiteral("POST"), QStringLiteral("api/rides.php"), QJsonDocument(body).toJson(QJsonDocument::Compact),
            "application/json", [this](QNetworkReply *reply) {
                if (!applyResults(QJsonDocument::fromJson(reply->readAll()).object(), true)) {
                    finish(QStringLiteral("the server did not take the deletions"));
                    return;
                }
                DiagLog::write("SYNC", QStringLiteral("rides: %1 deletion(s) sent").arg(m_sent.size()));
                refreshWaiting();
                sendRides();
            });
}

// 2. the rides not sent yet, in batches (each ride made JSON once)
void RideSync::sendRides()
{
    if (busy()) {   // (a ride started meanwhile: the rest after it)
        finish();
        return;
    }
    const QVector<RideRecord> waiting = m_db->ridesToSend(kBatchRides);
    if (waiting.isEmpty()) {
        fetch();
        return;
    }
    QByteArray rides;
    m_sent.clear();
    for (const RideRecord &r : waiting) {
        const QByteArray j = rideJson(r.id);
        if (j.isEmpty()) {
            m_db->markRideSent(r.uuid);   // (no profile: never sendable)
            continue;
        }
        if (!m_sent.isEmpty() && rides.size() + j.size() > kBatchBytes)
            break;   // (the next batch)
        rides += (m_sent.isEmpty() ? "" : ",") + j;
        m_sent << r.uuid;
    }
    if (m_sent.isEmpty()) {
        sendRides();
        return;
    }
    const QByteArray head = QJsonDocument(QJsonObject { { QStringLiteral("station"), m_db->stationId() },
                                                        { QStringLiteral("name"), QSysInfo::machineHostName().left(90) } })
                                .toJson(QJsonDocument::Compact);
    const QByteArray body = head.left(head.size() - 1) + ",\"rides\":[" + rides + "]}";
    request(QStringLiteral("POST"), QStringLiteral("api/rides.php"), body, "application/json", [this](QNetworkReply *reply) {
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (!applyResults(o, false)) {
            finish(QStringLiteral("the server did not take the rides"));
            return;
        }
        DiagLog::write("SYNC", QStringLiteral("rides: %1 sent").arg(m_sent.size()));
        setServerRev(o.value(QStringLiteral("rev")).toInteger(serverRev()));
        refreshWaiting();
        sendRides();   // (the next batch)
    });
}

// 3. what changed on the server since the last look
void RideSync::fetch()
{
    m_toDownload.clear();
    const qint64 since = storedRev();
    if (busy() || (serverRev() >= 0 && serverRev() == since && !m_more)) {
        finish();
        return;
    }
    request(QStringLiteral("GET"), QStringLiteral("api/rides.php?since=%1").arg(since), {}, {}, [this](QNetworkReply *reply) {
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value(QStringLiteral("serverId")).toString() != serverId() || !o.contains(QStringLiteral("rides"))) {
            finish(QStringLiteral("the server's ride list is not readable"));
            return;
        }
        int skipped = 0, deleted = 0;
        for (const QJsonValue &v : o.value(QStringLiteral("rides")).toArray()) {
            const QJsonObject r = v.toObject();
            const QString uuid = r.value(QStringLiteral("uuid")).toString().toLower();
            if (QUuid(uuid).isNull())
                continue;
            const qint64 id = m_db->rideIdForUuid(uuid);
            if (r.value(QStringLiteral("deleted")).toBool()) {
                if (id >= 0 && m_db->deleteRide(id, false)) {
                    m_ridesTouched = true;
                    ++deleted;
                }
            } else if (id < 0 && !m_db->rideTombstoned(uuid)) {
                if (m_db->profileIdForUuid(r.value(QStringLiteral("userUuid")).toString()) >= 0)
                    m_toDownload << uuid;
                else
                    ++skipped;   // (its profile is not here (yet): looked through again when it arrives)
            }
        }
        if (deleted > 0)
            DiagLog::write("SYNC", QStringLiteral("rides: %1 deleted (on the server)").arg(deleted));
        if (skipped > 0)
            DiagLog::write("SYNC", QStringLiteral("rides: %1 of profiles not on this station skipped").arg(skipped));
        m_listRev = o.value(QStringLiteral("rev")).toInteger(-1);
        m_more = o.value(QStringLiteral("more")).toBool();
        download(0);
    });
}

// 4. each new ride, whole
void RideSync::download(int index)
{
    if (index >= m_toDownload.size()) {
        if (!m_toDownload.isEmpty())
            DiagLog::write("SYNC", QStringLiteral("rides: %1 fetched from other stations").arg(m_toDownload.size()));
        if (m_listRev >= 0)   // (only now: one missing is fetched again next time)
            storeRev(m_listRev);
        if (m_more) {
            fetch();   // (the next part of a long list)
            return;
        }
        finish();
        return;
    }
    if (busy()) {   // (a ride started meanwhile: the rest afterwards)
        finish();
        return;
    }
    request(QStringLiteral("GET"), QStringLiteral("api/rides.php?uuid=%1").arg(m_toDownload.at(index)), {}, {},
            [this, index](QNetworkReply *reply) {
                const QJsonObject j = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("ride")).toObject();
                RideRecord r;
                r.uuid = j.value(QStringLiteral("uuid")).toString();
                r.profileId = m_db->profileIdForUuid(j.value(QStringLiteral("userUuid")).toString());
                r.station = j.value(QStringLiteral("stationName")).toString().left(100);
                if (r.station.isEmpty())
                    r.station = QStringLiteral("another station");
                r.sport = j.value(QStringLiteral("sport")).toString();
                r.workoutName = j.value(QStringLiteral("workoutName")).toString();
                r.startedAt = QDateTime::fromSecsSinceEpoch(j.value(QStringLiteral("startedAt")).toInteger());
                r.durationSec = j.value(QStringLiteral("durationSec")).toInt();
                r.blocks = j.value(QStringLiteral("blocks")).toInt();
                r.ergMode = j.value(QStringLiteral("erg")).toBool();
                r.ftp = j.value(QStringLiteral("ftp")).toInt();
                r.avgPower = j.value(QStringLiteral("avgPower")).toDouble();
                r.maxPower = j.value(QStringLiteral("maxPower")).toInt();
                r.normalizedPower = j.value(QStringLiteral("np")).toDouble();
                r.avgHeartRate = j.value(QStringLiteral("avgHr")).toDouble();
                r.maxHeartRate = j.value(QStringLiteral("maxHr")).toInt();
                r.avgCadence = j.value(QStringLiteral("avgCadence")).toDouble();
                r.kilojoules = j.value(QStringLiteral("kj")).toDouble();
                r.intensityFactor = j.value(QStringLiteral("ifValue")).toDouble();
                r.tss = j.value(QStringLiteral("tss")).toDouble();
                r.distanceM = j.value(QStringLiteral("distanceM")).toDouble();
                const QJsonArray zones = j.value(QStringLiteral("zones")).toArray();
                for (int i = 0; i < kZoneCount && i < zones.size(); ++i)
                    r.zoneSeconds[size_t(i)] = zones.at(i).toInt();
                QVector<Sample> samples;
                for (const QJsonValue &v : j.value(QStringLiteral("samples")).toArray()) {
                    const QJsonArray s = v.toArray();
                    if (s.size() >= 5)
                        samples.append({ s.at(0).toInt(), s.at(1).toInt(), s.at(2).toInt(), s.at(3).toInt(), s.at(4).toInt(),
                                         s.size() > 5 ? s.at(5).toDouble() : 0.0 });
                }
                QVector<RideLap> laps;
                for (const QJsonValue &v : j.value(QStringLiteral("laps")).toArray()) {
                    const QJsonArray l = v.toArray();
                    if (l.size() >= 2)
                        laps.append({ l.at(0).toInt(), l.at(1).toDouble(), l.size() > 2 ? l.at(2).toDouble() : 0.0 });
                }
                std::optional<RouteTrace> trace;
                if (j.value(QStringLiteral("trace")).isObject())
                    trace = traceFromJson(j.value(QStringLiteral("trace")).toObject());
                if (r.profileId >= 0 && !QUuid(r.uuid).isNull() && m_db->storeServerRide(r, samples, laps, trace) >= 0)
                    m_ridesTouched = true;
                download(index + 1);
            });
}

} // namespace watt
