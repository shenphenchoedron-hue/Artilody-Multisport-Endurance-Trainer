#include "Database.h"

#include <QAtomicInt>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>

namespace watt {

namespace {
QAtomicInt g_connectionCounter;

QString jsonText(const QJsonArray &a)
{
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QJsonArray jsonArray(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).array();
}

Profile profileFromQuery(const QSqlQuery &q)
{
    Profile p;
    p.id = q.value(0).toLongLong();
    p.name = q.value(1).toString();
    p.weightKg = q.value(2).toDouble();
    p.ftp = q.value(3).toInt();
    p.maxHr = q.value(4).toInt();
    p.restHr = q.value(5).toInt();
    p.preferredCadence = q.value(6).toInt();
    p.defaultErg = q.value(7).toInt() != 0;
    p.avatarPath = q.value(8).toString();
    p.zones = ZoneSet::fromJson(jsonArray(q.value(9).toString()));
    return p;
}

const char *kProfileColumns = "id, name, weight, ftp, max_hr, rest_hr, cadence, default_erg, avatar, zones";
const char *kWorkoutColumns = "id, name, category, focus, description, favorite, built_in, times_used, intervals";
const char *kRideColumns = "id, profile_id, workout_id, workout_name, started_at, duration, blocks, erg, ftp, "
                           "avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones";

Workout workoutFromQuery(const QSqlQuery &q)
{
    Workout w;
    w.id = q.value(0).toLongLong();
    w.name = q.value(1).toString();
    w.category = q.value(2).toString();
    w.focus = q.value(3).toString();
    w.description = q.value(4).toString();
    w.favorite = q.value(5).toInt() != 0;
    w.builtIn = q.value(6).toInt() != 0;
    w.timesUsed = q.value(7).toInt();
    w.intervals = Workout::intervalsFromJson(jsonArray(q.value(8).toString()));
    return w;
}

RideRecord rideFromQuery(const QSqlQuery &q)
{
    RideRecord r;
    r.id = q.value(0).toLongLong();
    r.profileId = q.value(1).toLongLong();
    r.workoutId = q.value(2).toLongLong();
    r.workoutName = q.value(3).toString();
    r.startedAt = QDateTime::fromSecsSinceEpoch(q.value(4).toLongLong());
    r.durationSec = q.value(5).toInt();
    r.blocks = q.value(6).toInt();
    r.ergMode = q.value(7).toInt() != 0;
    r.ftp = q.value(8).toInt();
    r.avgPower = q.value(9).toDouble();
    r.maxPower = q.value(10).toInt();
    r.normalizedPower = q.value(11).toDouble();
    r.avgHeartRate = q.value(12).toDouble();
    r.maxHeartRate = q.value(13).toInt();
    r.avgCadence = q.value(14).toDouble();
    r.kilojoules = q.value(15).toDouble();
    r.intensityFactor = q.value(16).toDouble();
    r.tss = q.value(17).toDouble();
    const QJsonArray zones = jsonArray(q.value(18).toString());
    for (int i = 0; i < kZoneCount && i < zones.size(); ++i)
        r.zoneSeconds[size_t(i)] = zones.at(i).toInt();
    return r;
}
} // namespace

Database::Database(QObject *parent)
    : QObject(parent)
    , m_connection(QStringLiteral("watt_db_%1").arg(g_connectionCounter.fetchAndAddRelaxed(1)))
{
}

Database::~Database()
{
    close();
}

QString Database::defaultPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("watt-trainer.sqlite"));
}

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(m_connection, false);
}

void Database::setError(const QString &context, const QString &error) const
{
    m_lastError = context + QStringLiteral(": ") + error;
    qWarning().noquote() << "[Database]" << m_lastError;
}

bool Database::open(const QString &path)
{
    close();
    QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    d.setDatabaseName(path);
    if (!d.open()) {
        setError(QStringLiteral("open"), d.lastError().text());
        return false;
    }
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    return migrate();
}

void Database::close()
{
    if (QSqlDatabase::contains(m_connection)) {
        {
            QSqlDatabase d = QSqlDatabase::database(m_connection, false);
            if (d.isOpen())
                d.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool Database::isOpen() const
{
    return QSqlDatabase::contains(m_connection) && db().isOpen();
}

bool Database::exec(const QString &sql)
{
    QSqlQuery q(db());
    if (!q.exec(sql)) {
        setError(sql.left(60), q.lastError().text());
        return false;
    }
    return true;
}

bool Database::migrate()
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("PRAGMA user_version"));
    const int version = q.next() ? q.value(0).toInt() : 0;
    if (version >= kSchemaVersion)
        return true;

    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS profiles ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, weight REAL, ftp INTEGER,"
                       " max_hr INTEGER, rest_hr INTEGER, cadence INTEGER, default_erg INTEGER, avatar TEXT, zones TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS workouts ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, category TEXT, focus TEXT,"
                       " description TEXT, favorite INTEGER DEFAULT 0, built_in INTEGER DEFAULT 0,"
                       " times_used INTEGER DEFAULT 0, intervals TEXT, created_at INTEGER)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS rides ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " profile_id INTEGER REFERENCES profiles(id) ON DELETE CASCADE,"
                       " workout_id INTEGER, workout_name TEXT, started_at INTEGER, duration INTEGER, blocks INTEGER,"
                       " erg INTEGER, ftp INTEGER, avg_power REAL, max_power INTEGER, np REAL, avg_hr REAL,"
                       " max_hr INTEGER, avg_cadence REAL, kj REAL, if_value REAL, tss REAL, zones TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rides_profile ON rides(profile_id, started_at)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS ride_samples ("
                       " ride_id INTEGER REFERENCES rides(id) ON DELETE CASCADE, t INTEGER, power INTEGER,"
                       " target INTEGER, cadence INTEGER, hr INTEGER, PRIMARY KEY (ride_id, t)) WITHOUT ROWID"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlists ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS playlist_tracks ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " playlist_id INTEGER REFERENCES playlists(id) ON DELETE CASCADE,"
                       " position INTEGER, path TEXT NOT NULL, title TEXT, artist TEXT, duration_ms INTEGER DEFAULT 0)"),
        QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion),
    };

    db().transaction();
    for (const QString &sql : schema) {
        if (!exec(sql)) {
            db().rollback();
            return false;
        }
    }
    return db().commit();
}

void Database::seedDefaults()
{
    if (profiles().isEmpty()) {
        Profile p;
        p.name = QStringLiteral("Lone");
        p.weightKg = 68;
        p.ftp = 250;
        p.maxHr = 175;
        p.restHr = 58;
        p.preferredCadence = 85;
        const qint64 id = saveProfile(p);
        setSetting(QStringLiteral("currentProfile"), id);
    }
    if (!setting(QStringLiteral("workoutsSeeded")).toBool()) {
        for (const Workout &w : Workout::builtInLibrary())
            saveWorkout(w);
        setSetting(QStringLiteral("workoutsSeeded"), true);
    }
    ensureBuiltInFtpTest();
}

bool Database::hasBuiltInFtpTest() const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM workouts WHERE built_in = 1 AND category = ?"));
    q.addBindValue(Workout::ftpTestCategory());
    return q.exec() && q.next() && q.value(0).toInt() > 0;
}

void Database::ensureBuiltInFtpTest()
{
    // The Ramp FTP Test is required for FTP testing. Older versions allowed it
    // to be deleted, so restore it even when the library was seeded already.
    if (hasBuiltInFtpTest())
        return;
    for (const Workout &w : Workout::builtInLibrary()) {
        if (w.isProtected())
            saveWorkout(w);
    }
}

// --- settings --------------------------------------------------------------

QVariant Database::setting(const QString &key, const QVariant &defaultValue) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0);
    return defaultValue;
}

void Database::setSetting(const QString &key, const QVariant &value)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value.toString());
    if (!q.exec())
        setError(QStringLiteral("setSetting"), q.lastError().text());
}

// --- profiles --------------------------------------------------------------

QVector<Profile> Database::profiles() const
{
    QVector<Profile> out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT %1 FROM profiles ORDER BY id").arg(QLatin1String(kProfileColumns))))
        return out;
    while (q.next())
        out.append(profileFromQuery(q));
    return out;
}

std::optional<Profile> Database::profile(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM profiles WHERE id = ?").arg(QLatin1String(kProfileColumns)));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return profileFromQuery(q);
    return std::nullopt;
}

qint64 Database::saveProfile(const Profile &p)
{
    QSqlQuery q(db());
    if (p.id < 0) {
        q.prepare(QStringLiteral("INSERT INTO profiles (name, weight, ftp, max_hr, rest_hr, cadence, default_erg, avatar, zones)"
                                 " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    } else {
        q.prepare(QStringLiteral("UPDATE profiles SET name=?, weight=?, ftp=?, max_hr=?, rest_hr=?, cadence=?,"
                                 " default_erg=?, avatar=?, zones=? WHERE id=?"));
    }
    q.addBindValue(p.name);
    q.addBindValue(p.weightKg);
    q.addBindValue(p.ftp);
    q.addBindValue(p.maxHr);
    q.addBindValue(p.restHr);
    q.addBindValue(p.preferredCadence);
    q.addBindValue(p.defaultErg ? 1 : 0);
    q.addBindValue(p.avatarPath);
    q.addBindValue(jsonText(p.zones.toJson()));
    if (p.id >= 0)
        q.addBindValue(p.id);
    if (!q.exec()) {
        setError(QStringLiteral("saveProfile"), q.lastError().text());
        return -1;
    }
    return p.id >= 0 ? p.id : q.lastInsertId().toLongLong();
}

bool Database::deleteProfile(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM profiles WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

// --- workouts --------------------------------------------------------------

QVector<Workout> Database::workouts() const
{
    QVector<Workout> out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT %1 FROM workouts ORDER BY id").arg(QLatin1String(kWorkoutColumns))))
        return out;
    while (q.next())
        out.append(workoutFromQuery(q));
    return out;
}

std::optional<Workout> Database::workout(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM workouts WHERE id = ?").arg(QLatin1String(kWorkoutColumns)));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return workoutFromQuery(q);
    return std::nullopt;
}

qint64 Database::saveWorkout(const Workout &w)
{
    QSqlQuery q(db());
    if (w.id < 0) {
        q.prepare(QStringLiteral("INSERT INTO workouts (name, category, focus, description, favorite, built_in,"
                                 " times_used, intervals, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    } else {
        q.prepare(QStringLiteral("UPDATE workouts SET name=?, category=?, focus=?, description=?, favorite=?,"
                                 " built_in=?, times_used=?, intervals=? WHERE id=?"));
    }
    q.addBindValue(w.name);
    q.addBindValue(w.category);
    q.addBindValue(w.focus);
    q.addBindValue(w.description);
    q.addBindValue(w.favorite ? 1 : 0);
    q.addBindValue(w.builtIn ? 1 : 0);
    q.addBindValue(w.timesUsed);
    q.addBindValue(jsonText(w.intervalsToJson()));
    if (w.id < 0)
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
    else
        q.addBindValue(w.id);
    if (!q.exec()) {
        setError(QStringLiteral("saveWorkout"), q.lastError().text());
        return -1;
    }
    return w.id >= 0 ? w.id : q.lastInsertId().toLongLong();
}

bool Database::deleteWorkout(qint64 id)
{
    if (auto w = workout(id); w && w->isProtected()) {
        setError(QStringLiteral("deleteWorkout"), QStringLiteral("\"%1\" is required for FTP tests").arg(w->name));
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM workouts WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::setWorkoutFavorite(qint64 id, bool favorite)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE workouts SET favorite = ? WHERE id = ?"));
    q.addBindValue(favorite ? 1 : 0);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::incrementWorkoutUsage(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE workouts SET times_used = times_used + 1 WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

// --- rides -----------------------------------------------------------------

qint64 Database::saveRide(const RideRecord &r, const QVector<Sample> &samples)
{
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    q.prepare(QStringLiteral("INSERT INTO rides (profile_id, workout_id, workout_name, started_at, duration, blocks,"
                             " erg, ftp, avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    QJsonArray zones;
    for (int s : r.zoneSeconds)
        zones.append(s);
    q.addBindValue(r.profileId);
    q.addBindValue(r.workoutId);
    q.addBindValue(r.workoutName);
    q.addBindValue(r.startedAt.toSecsSinceEpoch());
    q.addBindValue(r.durationSec);
    q.addBindValue(r.blocks);
    q.addBindValue(r.ergMode ? 1 : 0);
    q.addBindValue(r.ftp);
    q.addBindValue(r.avgPower);
    q.addBindValue(r.maxPower);
    q.addBindValue(r.normalizedPower);
    q.addBindValue(r.avgHeartRate);
    q.addBindValue(r.maxHeartRate);
    q.addBindValue(r.avgCadence);
    q.addBindValue(r.kilojoules);
    q.addBindValue(r.intensityFactor);
    q.addBindValue(r.tss);
    q.addBindValue(jsonText(zones));
    if (!q.exec()) {
        setError(QStringLiteral("saveRide"), q.lastError().text());
        d.rollback();
        return -1;
    }
    const qint64 rideId = q.lastInsertId().toLongLong();

    QSqlQuery s(d);
    s.prepare(QStringLiteral("INSERT OR REPLACE INTO ride_samples (ride_id, t, power, target, cadence, hr) VALUES (?, ?, ?, ?, ?, ?)"));
    for (const Sample &smp : samples) {
        s.addBindValue(rideId);
        s.addBindValue(smp.t);
        s.addBindValue(smp.power);
        s.addBindValue(smp.targetPower);
        s.addBindValue(smp.cadence);
        s.addBindValue(smp.heartRate);
        if (!s.exec()) {
            setError(QStringLiteral("saveRide samples"), s.lastError().text());
            d.rollback();
            return -1;
        }
    }
    if (!d.commit()) {
        setError(QStringLiteral("saveRide commit"), d.lastError().text());
        return -1;
    }
    return rideId;
}

QVector<RideRecord> Database::rides(qint64 profileId, const QDateTime &from, int limit) const
{
    QVector<RideRecord> out;
    QSqlQuery q(db());
    QString sql = QStringLiteral("SELECT %1 FROM rides WHERE profile_id = ? AND started_at >= ? ORDER BY started_at DESC")
                      .arg(QLatin1String(kRideColumns));
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    q.prepare(sql);
    q.addBindValue(profileId);
    q.addBindValue(from.isValid() ? from.toSecsSinceEpoch() : qint64(0));
    if (!q.exec()) {
        setError(QStringLiteral("rides"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.append(rideFromQuery(q));
    return out;
}

std::optional<RideRecord> Database::ride(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT %1 FROM rides WHERE id = ?").arg(QLatin1String(kRideColumns)));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return rideFromQuery(q);
    return std::nullopt;
}

QVector<Sample> Database::rideSamples(qint64 rideId) const
{
    QVector<Sample> out;
    QSqlQuery q(db());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT t, power, target, cadence, hr FROM ride_samples WHERE ride_id = ? ORDER BY t"));
    q.addBindValue(rideId);
    if (!q.exec())
        return out;
    while (q.next())
        out.append({ q.value(0).toInt(), q.value(1).toInt(), q.value(2).toInt(), q.value(3).toInt(), q.value(4).toInt() });
    return out;
}

bool Database::deleteRide(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM rides WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

// --- music -----------------------------------------------------------------

QVector<Playlist> Database::playlists() const
{
    QVector<Playlist> out;
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT p.id, p.name, (SELECT COUNT(*) FROM playlist_tracks t WHERE t.playlist_id = p.id)"
                               " FROM playlists p ORDER BY p.id")))
        return out;
    while (q.next())
        out.append({ q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt() });
    return out;
}

qint64 Database::createPlaylist(const QString &name)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO playlists (name) VALUES (?)"));
    q.addBindValue(name);
    if (!q.exec()) {
        setError(QStringLiteral("createPlaylist"), q.lastError().text());
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::renamePlaylist(qint64 id, const QString &name)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE playlists SET name = ? WHERE id = ?"));
    q.addBindValue(name);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::deletePlaylist(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

QVector<Track> Database::playlistTracks(qint64 playlistId) const
{
    QVector<Track> out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, path, title, artist, duration_ms FROM playlist_tracks WHERE playlist_id = ?"
                             " ORDER BY position, id"));
    q.addBindValue(playlistId);
    if (!q.exec())
        return out;
    while (q.next())
        out.append({ q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                     q.value(4).toLongLong() });
    return out;
}

qint64 Database::addTrack(qint64 playlistId, const Track &t)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO playlist_tracks (playlist_id, position, path, title, artist, duration_ms)"
                             " VALUES (?, (SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_tracks WHERE playlist_id = ?),"
                             " ?, ?, ?, ?)"));
    q.addBindValue(playlistId);
    q.addBindValue(playlistId);
    q.addBindValue(t.path);
    q.addBindValue(t.title);
    q.addBindValue(t.artist);
    q.addBindValue(t.durationMs);
    if (!q.exec()) {
        setError(QStringLiteral("addTrack"), q.lastError().text());
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::removeTrack(qint64 trackId)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM playlist_tracks WHERE id = ?"));
    q.addBindValue(trackId);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::reorderTracks(qint64 playlistId, const QVector<qint64> &ids)
{
    QSqlDatabase d = db();
    d.transaction();
    QSqlQuery q(d);
    q.prepare(QStringLiteral("UPDATE playlist_tracks SET position = ? WHERE id = ? AND playlist_id = ?"));
    for (int i = 0; i < ids.size(); ++i) {
        q.addBindValue(i);
        q.addBindValue(ids.at(i));
        q.addBindValue(playlistId);
        if (!q.exec()) {
            d.rollback();
            return false;
        }
    }
    return d.commit();
}

bool Database::updateTrackInfo(qint64 trackId, const QString &title, const QString &artist, qint64 durationMs)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE playlist_tracks SET title = ?, artist = ?, duration_ms = ? WHERE id = ?"));
    q.addBindValue(title);
    q.addBindValue(artist);
    q.addBindValue(durationMs);
    q.addBindValue(trackId);
    return q.exec();
}

} // namespace watt
