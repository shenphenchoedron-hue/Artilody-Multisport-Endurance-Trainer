#include "Database.h"

#include <QAtomicInt>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>
#include <cmath>

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
    p.rowingFtp = q.value(10).isNull() ? 150 : q.value(10).toInt();
    return p;
}

const char *kProfileColumns = "id, name, weight, ftp, max_hr, rest_hr, cadence, default_erg, avatar, zones, rowing_ftp";
const char *kWorkoutColumns = "id, name, category, focus, description, favorite, built_in, times_used, intervals, sport";
const char *kRideColumns = "id, profile_id, workout_id, workout_name, started_at, duration, blocks, erg, ftp, "
                           "avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones, sport, distance_m";

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
    w.sport = sport::normalize(q.value(9).toString());
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
    r.sport = sport::normalize(q.value(19).toString());
    r.distanceM = q.value(20).toDouble();
    return r;
}
const char *kPlanSelect =
    "SELECT p.id, p.profile_id, p.date, p.workout_id, p.position, p.completed, p.ride_id, p.notes,"
    " w.name, w.sport, w.category, w.intervals"
    " FROM planned_workouts p JOIN workouts w ON w.id = p.workout_id";

PlannedWorkout planFromQuery(const QSqlQuery &q)
{
    PlannedWorkout e;
    e.id = q.value(0).toLongLong();
    e.profileId = q.value(1).toLongLong();
    e.date = QDate::fromString(q.value(2).toString(), Qt::ISODate);
    e.workoutId = q.value(3).toLongLong();
    e.position = q.value(4).toInt();
    e.completed = q.value(5).toInt() != 0;
    e.rideId = q.value(6).isNull() ? -1 : q.value(6).toLongLong();
    e.notes = q.value(7).toString();
    e.workoutName = q.value(8).toString();
    e.sport = sport::normalize(q.value(9).toString());
    e.category = q.value(10).toString();
    Workout w;
    w.intervals = Workout::intervalsFromJson(jsonArray(q.value(11).toString()));
    e.durationSec = w.totalDurationSec();
    return e;
}

QVariant nullableId(qint64 id)
{
    return id > 0 ? QVariant(id) : QVariant(QMetaType::fromType<qint64>());
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

int Database::userVersion() const
{
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        return q.value(0).toInt();
    return 0;
}

bool Database::hasColumn(const QString &table, const QString &column) const
{
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool Database::addColumnIfMissing(const QString &table, const QString &column, const QString &definition)
{
    if (hasColumn(table, column))
        return true;
    return exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition));
}

// Each step upgrades the schema by exactly one version inside one transaction
// and only ever adds tables/columns, so existing user data is preserved.
// Fresh databases run the same steps (v0 -> v1 -> v2 ...).
bool Database::migrate()
{
    const int version = userVersion();
    if (version > kSchemaVersion) {
        qWarning().noquote() << "[Database] schema version" << version << "is newer than supported" << kSchemaVersion;
        return true;   // additive schema: older code can still read what it knows
    }
    struct Step { int version; bool (Database::*apply)(); };
    const Step steps[] = { { 1, &Database::createSchemaV1 }, { 2, &Database::migrateToV2 } };
    for (const Step &step : steps) {
        if (version >= step.version)
            continue;
        if (!db().transaction()) {
            setError(QStringLiteral("migrate"), db().lastError().text());
            return false;
        }
        if (!(this->*step.apply)() || !exec(QStringLiteral("PRAGMA user_version = %1").arg(step.version))) {
            db().rollback();
            return false;
        }
        if (!db().commit()) {
            setError(QStringLiteral("migrate commit"), db().lastError().text());
            return false;
        }
    }
    return true;
}

bool Database::createSchemaV1()
{
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
    };
    for (const QString &sql : schema) {
        if (!exec(sql))
            return false;
    }
    return true;
}

bool Database::migrateToV2()
{
    if (!addColumnIfMissing(QStringLiteral("workouts"), QStringLiteral("sport"), QStringLiteral("TEXT NOT NULL DEFAULT 'cycling'"))
        || !addColumnIfMissing(QStringLiteral("rides"), QStringLiteral("sport"), QStringLiteral("TEXT NOT NULL DEFAULT 'cycling'"))
        || !addColumnIfMissing(QStringLiteral("rides"), QStringLiteral("distance_m"), QStringLiteral("REAL"))
        || !addColumnIfMissing(QStringLiteral("profiles"), QStringLiteral("rowing_ftp"), QStringLiteral("INTEGER NOT NULL DEFAULT 150")))
        return false;
    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS planned_workouts ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
                       " date TEXT NOT NULL,"
                       " workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
                       " position INTEGER NOT NULL DEFAULT 0,"
                       " completed INTEGER NOT NULL DEFAULT 0,"
                       " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL,"
                       " notes TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_plan_profile_date ON planned_workouts(profile_id, date, position)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS rowing_records ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
                       " distance_m INTEGER NOT NULL,"
                       " time_sec REAL NOT NULL,"
                       " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL,"
                       " created_at TEXT,"
                       " trace TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_rowing_records ON rowing_records(profile_id, distance_m, time_sec)"),
    };
    for (const QString &sql : schema) {
        if (!exec(sql))
            return false;
    }
    return true;
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
        q.prepare(QStringLiteral("INSERT INTO profiles (name, weight, ftp, max_hr, rest_hr, cadence, default_erg, avatar, zones,"
                                 " rowing_ftp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    } else {
        q.prepare(QStringLiteral("UPDATE profiles SET name=?, weight=?, ftp=?, max_hr=?, rest_hr=?, cadence=?,"
                                 " default_erg=?, avatar=?, zones=?, rowing_ftp=? WHERE id=?"));
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
    q.addBindValue(p.rowingFtp);
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
                                 " times_used, intervals, sport, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    } else {
        q.prepare(QStringLiteral("UPDATE workouts SET name=?, category=?, focus=?, description=?, favorite=?,"
                                 " built_in=?, times_used=?, intervals=?, sport=? WHERE id=?"));
    }
    q.addBindValue(w.name);
    q.addBindValue(w.category);
    q.addBindValue(w.focus);
    q.addBindValue(w.description);
    q.addBindValue(w.favorite ? 1 : 0);
    q.addBindValue(w.builtIn ? 1 : 0);
    q.addBindValue(w.timesUsed);
    q.addBindValue(jsonText(w.intervalsToJson()));
    q.addBindValue(sport::normalize(w.sport));
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
    // Plan entries of the workout would dangle (the FK cascade covers this too,
    // but only when foreign keys are enabled on the connection). Both deletes
    // succeed or neither does.
    QSqlDatabase d = db();
    if (!d.transaction()) {
        setError(QStringLiteral("deleteWorkout"), d.lastError().text());
        return false;
    }
    QSqlQuery plan(d);
    plan.prepare(QStringLiteral("DELETE FROM planned_workouts WHERE workout_id = ?"));
    plan.addBindValue(id);
    if (!plan.exec()) {
        setError(QStringLiteral("deleteWorkout plan"), plan.lastError().text());
        d.rollback();
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("DELETE FROM workouts WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(QStringLiteral("deleteWorkout"), q.lastError().text());
        d.rollback();
        return false;
    }
    if (q.numRowsAffected() <= 0) {
        d.rollback();   // unknown id: nothing changed
        return false;
    }
    if (!d.commit()) {
        setError(QStringLiteral("deleteWorkout commit"), d.lastError().text());
        d.rollback();
        return false;
    }
    return true;
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
                             " erg, ftp, avg_power, max_power, np, avg_hr, max_hr, avg_cadence, kj, if_value, tss, zones,"
                             " sport, distance_m) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
    q.addBindValue(sport::normalize(r.sport));
    q.addBindValue(r.distanceM > 0 ? QVariant(r.distanceM) : QVariant(QMetaType::fromType<double>()));
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
    // A plan entry ticked off by this ride is open again afterwards.
    QSqlDatabase d = db();
    if (!d.transaction()) {
        setError(QStringLiteral("deleteRide"), d.lastError().text());
        return false;
    }
    QSqlQuery plan(d);
    plan.prepare(QStringLiteral("UPDATE planned_workouts SET completed = 0, ride_id = NULL WHERE ride_id = ?"));
    plan.addBindValue(id);
    if (!plan.exec()) {
        setError(QStringLiteral("deleteRide plan"), plan.lastError().text());
        d.rollback();
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("DELETE FROM rides WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        setError(QStringLiteral("deleteRide"), q.lastError().text());
        d.rollback();
        return false;
    }
    if (q.numRowsAffected() <= 0) {
        d.rollback();
        return false;
    }
    if (!d.commit()) {
        setError(QStringLiteral("deleteRide commit"), d.lastError().text());
        d.rollback();
        return false;
    }
    return true;
}

// --- workout plan ----------------------------------------------------------

QVector<PlannedWorkout> Database::plannedWorkouts(qint64 profileId, const QDate &from, const QDate &to,
                                                  bool onlyOpen, int limit) const
{
    QVector<PlannedWorkout> out;
    QString sql = QLatin1String(kPlanSelect) + QStringLiteral(" WHERE p.profile_id = ?");
    if (from.isValid())
        sql += QStringLiteral(" AND p.date >= ?");
    if (to.isValid())
        sql += QStringLiteral(" AND p.date <= ?");
    if (onlyOpen)
        sql += QStringLiteral(" AND p.completed = 0");
    sql += QStringLiteral(" ORDER BY p.date, p.position, p.id");
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    QSqlQuery q(db());
    q.prepare(sql);
    q.addBindValue(profileId);
    if (from.isValid())
        q.addBindValue(from.toString(Qt::ISODate));
    if (to.isValid())
        q.addBindValue(to.toString(Qt::ISODate));
    if (!q.exec()) {
        setError(QStringLiteral("plannedWorkouts"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.append(planFromQuery(q));
    return out;
}

std::optional<PlannedWorkout> Database::plannedWorkout(qint64 id) const
{
    QSqlQuery q(db());
    q.prepare(QLatin1String(kPlanSelect) + QStringLiteral(" WHERE p.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return planFromQuery(q);
    return std::nullopt;
}

qint64 Database::addPlannedWorkout(qint64 profileId, const QDate &date, qint64 workoutId, const QString &notes)
{
    if (!date.isValid() || !workout(workoutId)) {
        setError(QStringLiteral("addPlannedWorkout"), QStringLiteral("invalid date or workout"));
        return -1;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO planned_workouts (profile_id, date, workout_id, position, completed, notes)"
                             " VALUES (?, ?, ?, (SELECT COALESCE(MAX(position), -1) + 1 FROM planned_workouts"
                             " WHERE profile_id = ? AND date = ?), 0, ?)"));
    const QString d = date.toString(Qt::ISODate);
    q.addBindValue(profileId);
    q.addBindValue(d);
    q.addBindValue(workoutId);
    q.addBindValue(profileId);
    q.addBindValue(d);
    q.addBindValue(notes);
    if (!q.exec()) {
        setError(QStringLiteral("addPlannedWorkout"), q.lastError().text());
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::removePlannedWorkout(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM planned_workouts WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::movePlannedWorkout(qint64 id, const QDate &date)
{
    if (!date.isValid())
        return false;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE planned_workouts SET date = ?, position = (SELECT COALESCE(MAX(p2.position), -1) + 1"
                             " FROM planned_workouts p2 WHERE p2.profile_id = planned_workouts.profile_id AND p2.date = ?"
                             " AND p2.id <> ?) WHERE id = ?"));
    const QString d = date.toString(Qt::ISODate);
    q.addBindValue(d);
    q.addBindValue(d);
    q.addBindValue(id);
    q.addBindValue(id);
    if (!q.exec()) {
        setError(QStringLiteral("movePlannedWorkout"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool Database::setPlannedCompleted(qint64 id, bool completed, qint64 rideId)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE planned_workouts SET completed = ?, ride_id = ? WHERE id = ?"));
    q.addBindValue(completed ? 1 : 0);
    q.addBindValue(completed ? nullableId(rideId) : nullableId(-1));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

bool Database::setPlannedNotes(qint64 id, const QString &notes)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE planned_workouts SET notes = ? WHERE id = ?"));
    q.addBindValue(notes);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

qint64 Database::completePlannedWorkout(qint64 profileId, qint64 workoutId, const QDate &date, qint64 rideId)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id FROM planned_workouts WHERE profile_id = ? AND workout_id = ? AND date = ?"
                             " AND completed = 0 ORDER BY position, id LIMIT 1"));
    q.addBindValue(profileId);
    q.addBindValue(workoutId);
    q.addBindValue(date.toString(Qt::ISODate));
    if (!q.exec() || !q.next())
        return -1;
    const qint64 id = q.value(0).toLongLong();
    return setPlannedCompleted(id, true, rideId) ? id : -1;
}

// --- rowing records ------------------------------------------------------------

QString Database::traceToText(const QVector<TracePoint> &trace)
{
    // Compact text, rounded to 0.1 s / 0.1 m: ~12 bytes per sample.
    QString out;
    out.reserve(trace.size() * 14 + 2);
    out += QLatin1Char('[');
    for (int i = 0; i < trace.size(); ++i) {
        if (i > 0)
            out += QLatin1Char(',');
        out += QLatin1Char('[') + QString::number(std::round(trace.at(i).t * 10.0) / 10.0, 'g', 10) + QLatin1Char(',')
             + QString::number(std::round(trace.at(i).d * 10.0) / 10.0, 'g', 10) + QLatin1Char(']');
    }
    out += QLatin1Char(']');
    return out;
}

QVector<TracePoint> Database::traceFromText(const QString &text)
{
    QVector<TracePoint> out;
    const QJsonArray a = jsonArray(text);
    out.reserve(a.size());
    for (const QJsonValue &v : a) {
        const QJsonArray p = v.toArray();
        if (p.size() >= 2)
            out.append({ p.at(0).toDouble(), p.at(1).toDouble() });
    }
    return out;
}

qint64 Database::saveRowingRecord(const RowingRecord &r)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO rowing_records (profile_id, distance_m, time_sec, ride_id, created_at, trace)"
                             " VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(r.profileId);
    q.addBindValue(r.distanceM);
    q.addBindValue(r.timeSec);
    q.addBindValue(nullableId(r.rideId));
    q.addBindValue((r.createdAt.isValid() ? r.createdAt : QDateTime::currentDateTime()).toUTC().toString(Qt::ISODate));
    q.addBindValue(traceToText(r.trace));
    if (!q.exec()) {
        setError(QStringLiteral("saveRowingRecord"), q.lastError().text());
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

namespace {
RowingRecord rowingFromQuery(const QSqlQuery &q, bool withTrace)
{
    RowingRecord r;
    r.id = q.value(0).toLongLong();
    r.profileId = q.value(1).toLongLong();
    r.distanceM = q.value(2).toInt();
    r.timeSec = q.value(3).toDouble();
    r.rideId = q.value(4).isNull() ? -1 : q.value(4).toLongLong();
    r.createdAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
    if (withTrace)
        r.trace = Database::traceFromText(q.value(6).toString());
    return r;
}
} // namespace

std::optional<RowingRecord> Database::bestRowingRecord(qint64 profileId, int distanceM) const
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, profile_id, distance_m, time_sec, ride_id, created_at, trace FROM rowing_records"
                             " WHERE profile_id = ? AND distance_m = ? ORDER BY time_sec, id LIMIT 1"));
    q.addBindValue(profileId);
    q.addBindValue(distanceM);
    if (q.exec() && q.next())
        return rowingFromQuery(q, true);
    return std::nullopt;
}

QVector<RowingRecord> Database::bestRowingRecords(qint64 profileId, bool withTrace) const
{
    QVector<RowingRecord> out;
    QSqlQuery q(db());
    // the best (fastest, then oldest) record per distance
    q.prepare(QStringLiteral("SELECT r.id, r.profile_id, r.distance_m, r.time_sec, r.ride_id, r.created_at, %1"
                             " FROM rowing_records r WHERE r.profile_id = ? AND r.id = ("
                             " SELECT r2.id FROM rowing_records r2 WHERE r2.profile_id = r.profile_id AND r2.distance_m = r.distance_m"
                             " ORDER BY r2.time_sec, r2.id LIMIT 1) ORDER BY r.distance_m")
                  .arg(withTrace ? QStringLiteral("r.trace") : QStringLiteral("NULL")));
    q.addBindValue(profileId);
    if (!q.exec()) {
        setError(QStringLiteral("bestRowingRecords"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.append(rowingFromQuery(q, withTrace));
    return out;
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
