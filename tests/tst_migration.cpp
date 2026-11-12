// Schema migrations v1 -> v2 -> v3 -> v4 -> v5 (PRAGMA user_version) without data loss.
#include "Database.h"
#include "ProfileManager.h"
#include "WorkoutBuilder.h"
#include "WorkoutLibrary.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace watt;

namespace {

// Exactly the schema of version 1 (as shipped before v2).
const char *kV1Schema[] = {
    "CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE profiles (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, weight REAL, ftp INTEGER,"
    " max_hr INTEGER, rest_hr INTEGER, cadence INTEGER, default_erg INTEGER, avatar TEXT, zones TEXT)",
    "CREATE TABLE workouts (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, category TEXT, focus TEXT,"
    " description TEXT, favorite INTEGER DEFAULT 0, built_in INTEGER DEFAULT 0,"
    " times_used INTEGER DEFAULT 0, intervals TEXT, created_at INTEGER)",
    "CREATE TABLE rides (id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " profile_id INTEGER REFERENCES profiles(id) ON DELETE CASCADE,"
    " workout_id INTEGER, workout_name TEXT, started_at INTEGER, duration INTEGER, blocks INTEGER,"
    " erg INTEGER, ftp INTEGER, avg_power REAL, max_power INTEGER, np REAL, avg_hr REAL,"
    " max_hr INTEGER, avg_cadence REAL, kj REAL, if_value REAL, tss REAL, zones TEXT)",
    "CREATE INDEX idx_rides_profile ON rides(profile_id, started_at)",
    "CREATE TABLE ride_samples (ride_id INTEGER REFERENCES rides(id) ON DELETE CASCADE, t INTEGER, power INTEGER,"
    " target INTEGER, cadence INTEGER, hr INTEGER, PRIMARY KEY (ride_id, t)) WITHOUT ROWID",
    "CREATE TABLE playlists (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)",
    "CREATE TABLE playlist_tracks (id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " playlist_id INTEGER REFERENCES playlists(id) ON DELETE CASCADE,"
    " position INTEGER, path TEXT NOT NULL, title TEXT, artist TEXT, duration_ms INTEGER DEFAULT 0)",
    "INSERT INTO settings VALUES ('currentProfile', '1')",
    "INSERT INTO settings VALUES ('workoutsSeeded', 'true')",
    "INSERT INTO profiles VALUES (1, 'Lone', 68, 262, 175, 58, 85, 1, '', '[0.55,0.75,0.87,0.94,1.05]')",
    "INSERT INTO workouts VALUES (7, 'My Sweet Spot', 'Intervals', 'SS', 'desc', 1, 0, 4,"
    " '[{\"type\":\"steady\",\"name\":\"SS\",\"duration\":600,\"start\":0.9,\"end\":0.9}]', 1700000000)",
    "INSERT INTO rides VALUES (3, 1, 7, 'My Sweet Spot', 1700000000, 600, 1, 1, 262, 230.5, 400, 236, 140, 170,"
    " 90, 138, 0.9, 13.5, '[0,0,600,0,0,0]')",
    "INSERT INTO ride_samples VALUES (3, 0, 220, 236, 90, 120)",
    "INSERT INTO playlists VALUES (1, 'Mix')",
    "INSERT INTO playlist_tracks VALUES (1, 1, 0, '/music/a.mp3', 'A', 'B', 1000)",
    "PRAGMA user_version = 1",
};

QStringList columns(const QString &dbPath, const QString &table)
{
    QStringList out;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("inspect"));
        d.setDatabaseName(dbPath);
        d.open();
        QSqlQuery q(d);
        q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
        while (q.next())
            out.append(q.value(1).toString());
    }
    QSqlDatabase::removeDatabase(QStringLiteral("inspect"));
    return out;
}

int pragmaVersion(const QString &dbPath)
{
    int v = -1;
    {
        QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("inspect"));
        d.setDatabaseName(dbPath);
        d.open();
        QSqlQuery q(d);
        if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
            v = q.value(0).toInt();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("inspect"));
    return v;
}

} // namespace

class TestMigration : public QObject
{
    Q_OBJECT
private slots:
    void freshDatabaseIsCurrent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("fresh.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            QCOMPARE(db.profiles().first().rowingFtp, 150);
            for (const Workout &w : db.workouts())
                QVERIFY(w.sport == QStringLiteral("cycling") || w.sport == QStringLiteral("running"));
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QCOMPARE(Database::kSchemaVersion, 15);
        QVERIFY(columns(path, QStringLiteral("rides")).contains(QStringLiteral("synced")));   // v15
        QVERIFY(columns(path, QStringLiteral("ride_tombstones")).contains(QStringLiteral("deleted_ms")));
        QVERIFY(columns(path, QStringLiteral("server_media")).contains(QStringLiteral("local_path")));   // v14
        for (const char *c : { "updated_ms", "dirty", "picture_hash" })   // v13
            QVERIFY(columns(path, QStringLiteral("profiles")).contains(QString::fromLatin1(c)));
        QVERIFY(columns(path, QStringLiteral("profile_tombstones")).contains(QStringLiteral("deleted_ms")));
        for (const QString &t : Database::uuidTables())   // v12
            QVERIFY2(columns(path, t).contains(QStringLiteral("uuid")), qPrintable(t));
        QVERIFY(columns(path, QStringLiteral("run_workouts")).isEmpty());   // v7: running workouts live in workouts
        QVERIFY(columns(path, QStringLiteral("ant_tests")).contains(QStringLiteral("speed_kmh")));
        for (const char *c : { "kind", "route_path", "route_name", "planned_duration_sec", "options" })
            QVERIFY(columns(path, QStringLiteral("planned_workouts")).contains(QString::fromLatin1(c)));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));
        QVERIFY(columns(path, QStringLiteral("ride_samples")).contains(QStringLiteral("speed")));
        QVERIFY(columns(path, QStringLiteral("workouts")).contains(QStringLiteral("sport")));
        QVERIFY(columns(path, QStringLiteral("planned_workouts")).contains(QStringLiteral("ride_id")));
        QVERIFY(columns(path, QStringLiteral("rowing_records")).contains(QStringLiteral("trace")));
        for (const char *c : { "kind", "route_key", "start_m", "length_m", "time_sec", "ride_id" })   // v8
            QVERIFY(columns(path, QStringLiteral("segment_records")).contains(QString::fromLatin1(c)));
        QVERIFY(columns(path, QStringLiteral("sprint_records")).isEmpty());
        for (const char *c : { "ride_id", "lap", "time_sec", "distance_m" })   // v10
            QVERIFY(columns(path, QStringLiteral("ride_laps")).contains(QString::fromLatin1(c)));
        for (const char *c : { "profile_id", "ride_id", "sport", "route_key", "start_m", "loop", "completed",
                               "duration_sec", "distance_m", "trace" })   // v11
            QVERIFY(columns(path, QStringLiteral("route_traces")).contains(QString::fromLatin1(c)));
    }
    void ridesToSendAndFromTheServer()
    {
        // v15: rides recorded here wait to be sent; one from the server keeps
        // its uuid and station, is not sent back; deleted here: a tombstone
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        const Profile lone = db.profiles().first();
        RideRecord r;
        r.profileId = lone.id;
        r.startedAt = QDateTime::currentDateTime();
        const qint64 mine = db.saveRide(r, { { 0, 100, 0, 80, 120, 0 }, { 1, 110, 0, 81, 121, 5.5 } });
        QCOMPARE(db.ridesWaiting(), 1);
        QCOMPARE(db.ridesToSend(10).size(), 1);
        QVERIFY(db.markRideSent(db.ride(mine)->uuid));
        QCOMPARE(db.ridesWaiting(), 0);
        QVERIFY(db.ride(mine)->synced);
        QVERIFY(db.ride(mine)->station.isEmpty());

        RideRecord other = r;
        other.uuid = QStringLiteral("a0000000-0000-4000-8000-000000000001");
        other.station = QStringLiteral("Pi 2");
        RouteTrace t;
        t.uuid = QStringLiteral("b0000000-0000-4000-8000-000000000001");
        t.sport = QStringLiteral("gpx");
        t.routeKey = QStringLiteral("abc");
        t.durationSec = 60;
        t.distanceM = 500;
        t.trace = { { 0, 0 }, { 60, 500 } };
        const qint64 got = db.storeServerRide(other, { { 0, 200, 0, 90, 130, 0 } }, { { 1, 60, 500 } }, t);
        QVERIFY(got > mine);
        QCOMPARE(db.ride(got)->uuid, other.uuid);
        QCOMPARE(db.ride(got)->station, QStringLiteral("Pi 2"));
        QCOMPARE(db.rideIdForUuid(other.uuid), got);
        QCOMPARE(db.rideLaps(got).size(), 1);
        QCOMPARE(db.routeTraceForRide(got)->uuid, t.uuid);
        QCOMPARE(db.routeTraceForRide(got)->trace.size(), 2);
        QCOMPARE(db.ridesWaiting(), 0);   // (not sent back)
        QCOMPARE(db.storeServerRide(other, {}, {}, std::nullopt), qint64(-1));   // (already here)

        // another profile's trace is a ghost for this one
        Profile anne;
        anne.name = QStringLiteral("Anne");
        const qint64 anneId = db.saveProfile(anne);
        QCOMPARE(db.routeTracesOfOthers(anneId, QStringLiteral("gpx"), QStringLiteral("abc"), 0, false).size(), 1);
        QCOMPARE(db.routeTracesOfOthers(anneId, QStringLiteral("gpx"), QStringLiteral("abc"), 0, false).first().riderName, lone.name);
        QVERIFY(db.routeTracesOfOthers(lone.id, QStringLiteral("gpx"), QStringLiteral("abc"), 0, false).isEmpty());

        // deleted here: a tombstone to send; deleted by the server: none
        QVERIFY(db.deleteRide(mine));
        QCOMPARE(db.rideTombstones().size(), 1);
        QVERIFY(db.rideTombstoned(r.uuid.isEmpty() ? db.rideTombstones().first().first : r.uuid));
        QCOMPARE(db.ridesWaiting(), 1);
        QVERIFY(db.deleteRide(got, false));
        QCOMPARE(db.rideTombstones().size(), 1);
        QVERIFY(!db.routeTraceForRide(got));   // (its trace with it)
        // another server: everything recorded here again
        const qint64 again = db.saveRide(r, {});
        db.markRideSent(db.ride(again)->uuid);
        db.markAllRidesToSend();
        QCOMPARE(db.ridesToSend(10).size(), 1);
    }
    void serverMediaKeepsWhereItIsHere()
    {
        // v14: the server's media list; storing a newer version keeps local_path
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ServerMedia m;
        m.uuid = QStringLiteral("11111111-2222-4333-8444-555555555555");
        m.kind = QStringLiteral("video");
        m.name = QStringLiteral("Alpe");
        m.folder = QStringLiteral("Alpe");
        m.files = { { QStringLiteral("Alpe.mp4"), QString(40, QLatin1Char('a')), 100 },
                    { QStringLiteral("Alpe.gpx"), QString(40, QLatin1Char('b')), 5 } };
        m.sizeBytes = 105;
        QVERIFY2(db.storeServerMedia(m), qPrintable(db.lastError()));
        QVERIFY(db.setServerMediaLocalPath(m.uuid, QStringLiteral("/videos/Alpe")));
        m.name = QStringLiteral("Alpe d'Huez");
        QVERIFY(db.storeServerMedia(m));
        QVector<ServerMedia> all = db.serverMedia();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().name, QStringLiteral("Alpe d'Huez"));
        QCOMPARE(all.first().localPath, QStringLiteral("/videos/Alpe"));
        QCOMPARE(all.first().files.size(), 2);
        QCOMPARE(all.first().files.at(0).size, qint64(100));
        QVERIFY(db.removeServerMedia(m.uuid));
        QVERIFY(db.serverMedia().isEmpty());
    }
    void upgradesV12ToV13ProfilesToSend()
    {
        // v12 -> v13: the existing profiles are marked to send to a server;
        // a new station's starter profile is not; edits and deletions are
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v12.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            QCOMPARE(db.profiles().size(), 1);
            QCOMPARE(db.profiles().first().updatedMs, qint64(0));   // the starter: never changed
            QVERIFY(!db.profiles().first().dirty);
            QCOMPARE(db.profilesWaiting(), 0);
            // a second, the rider's own (the starter stays untouched)
            Profile anne;
            anne.name = QStringLiteral("Anne");
            QVERIFY(db.saveProfile(anne) > 0);
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v12"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            QVERIFY(q.exec(QStringLiteral("DROP TABLE profile_tombstones")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE ride_tombstones")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE server_media")));
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE rides DROP COLUMN synced")));
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE rides DROP COLUMN station")));
            for (const char *c : { "updated_ms", "dirty", "picture_hash" })
                QVERIFY(q.exec(QStringLiteral("ALTER TABLE profiles DROP COLUMN %1").arg(QLatin1String(c))));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 12")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v12"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        // the untouched starter: not to send (as on a new station); the rider's own: to send
        const QVector<Profile> all = db.profiles();
        QCOMPARE(all.size(), 2);
        const auto byName = [&](const QString &n) {
            return *std::find_if(all.begin(), all.end(), [&](const Profile &x) { return x.name == n; });
        };
        QVERIFY(!byName(QStringLiteral("Lone")).dirty);
        QCOMPARE(byName(QStringLiteral("Lone")).updatedMs, qint64(0));
        Profile p = byName(QStringLiteral("Anne"));
        QVERIFY(p.dirty);
        QVERIFY(p.updatedMs > 0);
        QCOMPARE(db.profilesToSend().size(), 1);
        // sent: no longer waiting - unless it changed again since
        QVERIFY(!db.markProfileSent(p.uuid, p.updatedMs - 1, QString()));
        QVERIFY(db.markProfileSent(p.uuid, p.updatedMs, QStringLiteral("abc")));
        QCOMPARE(db.profilesWaiting(), 0);
        QCOMPARE(db.profile(p.id)->pictureHash, QStringLiteral("abc"));
        QTest::qWait(2);
        p = *db.profile(p.id);
        p.ftp = 260;
        QCOMPARE(db.saveProfile(p), p.id);
        QVERIFY(db.profile(p.id)->dirty);
        QVERIFY(db.profile(p.id)->updatedMs > p.updatedMs);
        // the server's version: stored by uuid, not to send
        Profile server = *db.profile(p.id);
        server.name = QStringLiteral("From server");
        server.updatedMs = 1234;
        server.pictureHash.clear();
        QCOMPARE(db.storeServerProfile(server), p.id);
        QVERIFY(!db.profile(p.id)->dirty);
        QCOMPARE(db.profile(p.id)->name, QStringLiteral("From server"));
        Profile fresh;
        fresh.uuid = QStringLiteral("11111111-2222-4333-8444-555555555555");
        fresh.name = QStringLiteral("New from server");
        fresh.updatedMs = 99;
        const qint64 newId = db.storeServerProfile(fresh);
        QVERIFY(newId > p.id);
        QCOMPARE(db.profileIdForUuid(fresh.uuid), newId);
        QCOMPARE(db.uuidOf(QStringLiteral("profiles"), newId), fresh.uuid);   // (its own, not the trigger's)
        // deleted here: a tombstone for the server; deleted by the server: none
        QVERIFY(db.deleteProfile(newId));
        QCOMPARE(db.profileTombstones().size(), 1);
        QCOMPARE(db.profileTombstones().first().first, fresh.uuid);
        QCOMPARE(db.profilesWaiting(), 1);
        QVERIFY(db.removeProfileTombstone(fresh.uuid));
        const qint64 other = db.storeServerProfile(fresh);
        QVERIFY(db.deleteProfile(other, false));
        QVERIFY(db.profileTombstones().isEmpty());
        // another server: everything ever changed here goes again
        db.markAllProfilesToSend();
        QCOMPARE(db.profilesToSend().size(), 1);
        QVERIFY(db.profilesToSend().first().pictureHash.isEmpty());
    }
    void upgradesV11ToV12Uuids()
    {
        // v11 -> v12: every existing profile, ride, workout and trace gets a
        // uuid of its own; new rows get one from the database
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v11.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            Profile second;
            second.name = QStringLiteral("Second");
            QVERIFY(db.saveProfile(second) > 0);
            RideRecord r;
            r.profileId = 1;
            r.startedAt = QDateTime::currentDateTime();
            QVERIFY(db.saveRide(r, {}) > 0);
            QVERIFY(db.saveRide(r, {}) > 0);
            RouteTrace t;
            t.profileId = 1;
            t.sport = QStringLiteral("gpx");
            t.routeKey = QStringLiteral("abc");
            t.trace = { { 0, 0 }, { 1, 5 } };
            QVERIFY(db.saveRouteTrace(t) > 0);
        }
        {
            // back to v11: no uuid columns, triggers or indexes
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v11"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const QString &t : Database::uuidTables()) {
                QVERIFY(q.exec(QStringLiteral("DROP TRIGGER %1_uuid").arg(t)));
                QVERIFY(q.exec(QStringLiteral("DROP INDEX idx_%1_uuid").arg(t)));
                QVERIFY2(q.exec(QStringLiteral("ALTER TABLE %1 DROP COLUMN uuid").arg(t)), qPrintable(q.lastError().text()));
            }
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 11")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v11"));
        QVERIFY(!columns(path, QStringLiteral("rides")).contains(QStringLiteral("uuid")));

        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        const QRegularExpression v4(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"));
        QSet<QString> seen;
        const auto check = [&](const QString &uuid) {
            QVERIFY2(v4.match(uuid).hasMatch(), qPrintable(uuid));
            QVERIFY2(!seen.contains(uuid), qPrintable(uuid));
            seen.insert(uuid);
        };
        // the existing rows: all kept, each its own uuid
        const QVector<Profile> profiles = db.profiles();
        QCOMPARE(profiles.size(), 2);
        for (const Profile &p : profiles)
            check(p.uuid);
        const QVector<RideRecord> rides = db.rides(1);
        QCOMPARE(rides.size(), 2);
        for (const RideRecord &r : rides)
            check(r.uuid);
        const QVector<Workout> workouts = db.workouts();
        QVERIFY(!workouts.isEmpty());
        for (const Workout &w : workouts)
            check(w.uuid);
        const QVector<RouteTrace> traces = db.routeTraces(1, QStringLiteral("gpx"), QStringLiteral("abc"), 0, false, true);
        QCOMPARE(traces.size(), 1);
        check(traces.first().uuid);
        QCOMPARE(traces.first().trace.size(), 2);   // (the trace column after the uuid)
        QCOMPARE(db.uuidOf(QStringLiteral("profiles"), profiles.first().id), profiles.first().uuid);

        // new rows: a uuid from the trigger; an update keeps it
        Profile third;
        third.name = QStringLiteral("Third");
        const qint64 id = db.saveProfile(third);
        QVERIFY(id > 0);
        const QString uuid = db.profile(id)->uuid;
        check(uuid);
        Profile edited = *db.profile(id);
        edited.name = QStringLiteral("Third, edited");
        QCOMPARE(db.saveProfile(edited), id);
        QCOMPARE(db.profile(id)->uuid, uuid);
        RideRecord r;
        r.profileId = id;
        r.startedAt = QDateTime::currentDateTime();
        const qint64 rideId = db.saveRide(r, {});
        check(db.ride(rideId)->uuid);
        Workout w;
        w.name = QStringLiteral("New");
        check(db.workout(db.saveWorkout(w))->uuid);

        // the station's own uuid: made once, then kept
        const QString station = db.stationId();
        check(station);
        QCOMPARE(db.stationId(), station);
        // the database copied to another machine: a new one there
        db.setSetting(QStringLiteral("station/machine"), QStringLiteral("0000000000000000"));
        const QString elsewhere = db.stationId();
        QVERIFY(elsewhere != station);
        QCOMPARE(db.stationId(), elsewhere);
        QVERIFY(db.uuidOf(QStringLiteral("settings"), 1).isEmpty());   // (only the uuid tables)
    }
    void upgradesV10ToV11RouteTraces()
    {
        // v10 -> v11: the ghost traces table is added, the rides stay
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v10.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v10"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            QVERIFY(q.exec(QStringLiteral("DROP TABLE route_traces")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 10")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v10"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QVERIFY(db.hasTable(QStringLiteral("route_traces")));
    }
    void upgradesV9ToV10RideLaps()
    {
        // v9 -> v10: the lap table is added, the rides stay
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v9.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v9"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            QVERIFY(q.exec(QStringLiteral("DROP TABLE ride_laps")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 9")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v9"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QVERIFY(db.hasTable(QStringLiteral("ride_laps")));
    }

    void upgradesV1WithoutDataLoss()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v1.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));
        QCOMPARE(pragmaVersion(path), 1);

        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();

            // old data is intact and new fields have their defaults
            const auto p = db.profile(1);
            QVERIFY(p);
            QCOMPARE(p->name, QStringLiteral("Lone"));
            QCOMPARE(p->ftp, 262);
            QCOMPARE(p->rowingFtp, 150);
            const auto w = db.workout(7);
            QVERIFY(w);
            QCOMPARE(w->name, QStringLiteral("My Sweet Spot"));
            QCOMPARE(w->timesUsed, 4);
            QVERIFY(w->favorite);
            QCOMPARE(w->intervals.size(), 1);
            QCOMPARE(w->sport, QStringLiteral("cycling"));
            const auto r = db.ride(3);
            QVERIFY(r);
            QCOMPARE(r->workoutName, QStringLiteral("My Sweet Spot"));
            QCOMPARE(r->tss, 13.5);
            QCOMPARE(r->zoneSeconds[2], 600);
            QCOMPARE(r->sport, QStringLiteral("cycling"));
            QCOMPARE(r->distanceM, 0.0);
            QCOMPARE(db.rideSamples(3).size(), 1);
            QCOMPARE(db.playlistTracks(1).size(), 1);
            QCOMPARE(db.setting(QStringLiteral("currentProfile")).toInt(), 1);
            // seeding must not duplicate the library of an existing user
            // + restored built-in FTP test + the running workouts (new in v7)
            QCOMPARE(db.workouts().size(), 1 + 1 + RunWorkout::builtInLibrary().size());

            // new columns/tables are writable
            Profile changed = *p;
            changed.rowingFtp = 190;
            QCOMPARE(db.saveProfile(changed), qint64(1));
            QCOMPARE(db.profile(1)->rowingFtp, 190);
            RideRecord row;
            row.profileId = 1;
            row.workoutName = QStringLiteral("2k");
            row.startedAt = QDateTime::currentDateTime();
            row.sport = QStringLiteral("rowing");
            row.distanceM = 2000;
            const qint64 rowId = db.saveRide(row, {});
            QVERIFY(rowId > 0);
            QCOMPARE(db.ride(rowId)->sport, QStringLiteral("rowing"));
            QCOMPARE(db.ride(rowId)->distanceM, 2000.0);
            QVERIFY(db.addPlannedWorkout(1, QDate(2026, 10, 23), 7) > 0);
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        const QStringList rideCols = columns(path, QStringLiteral("rides"));
        QVERIFY(rideCols.contains(QStringLiteral("sport")));
        QVERIFY(rideCols.contains(QStringLiteral("distance_m")));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("rowing_ftp")));

        // reopening an up-to-date database is a no-op
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.profile(1)->rowingFtp, 190);
        QCOMPARE(again.plannedWorkouts(1, {}, {}).size(), 1);
    }

    void upgradesV2WithoutDataLoss()
    {
        // A v2 database (as shipped with 1.1.x): v1 schema + the v2 step.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v2.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v2"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            const char *v2[] = {
                "ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN distance_m REAL",
                "ALTER TABLE profiles ADD COLUMN rowing_ftp INTEGER NOT NULL DEFAULT 150",
                "CREATE TABLE planned_workouts (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, date TEXT NOT NULL,"
                " workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
                " position INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,"
                " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, notes TEXT)",
                "CREATE TABLE rowing_records (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, distance_m INTEGER NOT NULL,"
                " time_sec REAL NOT NULL, ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, created_at TEXT, trace TEXT)",
                "UPDATE profiles SET rowing_ftp = 177 WHERE id = 1",
                "INSERT INTO rides (id, profile_id, workout_id, workout_name, started_at, duration, sport, distance_m)"
                " VALUES (4, 1, -1, '2k', 1700100000, 480, 'rowing', 2000)",
                "INSERT INTO planned_workouts (profile_id, date, workout_id) VALUES (1, '2026-10-20', 7)",
                "INSERT INTO rowing_records (profile_id, distance_m, time_sec, ride_id, trace) VALUES (1, 2000, 480, 4, '[[0,0],[480,2000]]')",
                "PRAGMA user_version = 2",
            };
            for (const char *sql : v2)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v2"));
        QCOMPARE(pragmaVersion(path), 2);
        QVERIFY(!columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));

        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
            QCOMPARE(db.profile(1)->ftp, 262);
            QCOMPARE(db.profile(1)->rowingFtp, 177);
            QCOMPARE(db.workout(7)->name, QStringLiteral("My Sweet Spot"));
            QCOMPARE(db.ride(3)->tss, 13.5);
            QCOMPARE(db.ride(4)->distanceM, 2000.0);
            QCOMPARE(db.ride(4)->sport, QStringLiteral("rowing"));
            const QVector<Sample> old = db.rideSamples(3);
            QCOMPARE(old.size(), 1);
            QCOMPARE(old.first().power, 220);
            QCOMPARE(old.first().speedMps, 0.0);   // no speed recorded before v3
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 1);
            QVERIFY(db.bestRowingRecord(1, 2000));
            QCOMPARE(db.playlistTracks(1).size(), 1);
            QCOMPARE(db.workouts().size(), 2 + RunWorkout::builtInLibrary().size());   // + running (v7)
            // the new columns work
            QVERIFY(db.gearSetupJson(1).isEmpty());
            QVERIFY(db.setGearSetupJson(1, QStringLiteral("{\"chainrings\":[42]}")));
            QCOMPARE(db.gearSetupJson(1), QStringLiteral("{\"chainrings\":[42]}"));
            RideRecord r;
            r.profileId = 1;
            r.workoutName = QStringLiteral("Free Ride");
            r.startedAt = QDateTime::currentDateTime();
            r.distanceM = 1234;
            Sample s;
            s.power = 200;
            s.speedMps = 9.5;
            const qint64 id = db.saveRide(r, { s });
            QVERIFY(id > 0);
            QCOMPARE(db.rideSamples(id).first().speedMps, 9.5);
        }
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("gear_setup")));
        QVERIFY(columns(path, QStringLiteral("ride_samples")).contains(QStringLiteral("speed")));
        QVERIFY(columns(path, QStringLiteral("profiles")).contains(QStringLiteral("height_cm")));
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.gearSetupJson(1), QStringLiteral("{\"chainrings\":[42]}"));
    }

    void upgradesV3ToV4ProfileHeight()
    {
        // a v3 database (1.1.4 free ride test builds): the profile keeps its
        // weight, the new height is 0 (not set) and can be saved
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v3.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v3"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            // back to v3: without height_cm (SQLite >= 3.35 can drop columns)
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE profiles DROP COLUMN height_cm")));
            QVERIFY(q.exec(QStringLiteral("UPDATE profiles SET weight = 61.5 WHERE id = 1")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 3")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v3"));
        QVERIFY(!columns(path, QStringLiteral("profiles")).contains(QStringLiteral("height_cm")));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        auto p = db.profile(1);
        QVERIFY(p);
        QCOMPARE(p->weightKg, 61.5);
        QCOMPARE(p->heightCm, 0.0);
        Profile changed = *p;
        changed.heightCm = 168;
        QCOMPARE(db.saveProfile(changed), qint64(1));
        QCOMPARE(db.profile(1)->heightCm, 168.0);
        // validation: 0 (not set) or 100..230 cm
        changed.heightCm = 50;
        QVERIFY(!changed.isValid());
        changed.heightCm = 0;
        QVERIFY(changed.isValid());
        // ProfileManager reads and writes it
        ProfileManager profiles(&db);
        profiles.load();
        QVERIFY(profiles.saveProfile({ { QStringLiteral("heightCm"), 181.0 }, { QStringLiteral("weight"), 72.0 } }));
        QCOMPARE(profiles.heightCm(), 181.0);
        QCOMPARE(db.profile(profiles.profileId())->heightCm, 181.0);
        QCOMPARE(db.profile(profiles.profileId())->weightKg, 72.0);
    }

    // The 3D avatars (cyclist / runner / rower): saved per profile, only the
    // known keys and "#rrggbb" colours kept, gone with the profile.
    void profileAvatars3d()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("a.sqlite"))));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("model")).toString(), QStringLiteral("male"));   // (the default)
        QVERIFY(profiles.saveProfile({ { QStringLiteral("avatar3d"),
                                         QVariantMap { { "model", "Female" }, { "jersey", "#D0202A" }, { "bike", "red" },
                                                       { "evil", "x" } } },
                                       { QStringLiteral("rower3d"), QVariantMap { { "model", "male" }, { "boat", "#f5c400" } } } }));
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("model")).toString(), QStringLiteral("female"));
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("jersey")).toString(), QStringLiteral("#d0202a"));
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("bike")).toString(), QString());   // (not #rrggbb)
        QVERIFY(!profiles.avatar3d().contains(QStringLiteral("evil")));
        QCOMPARE(profiles.rower3d().value(QStringLiteral("boat")).toString(), QStringLiteral("#f5c400"));
        QCOMPARE(profiles.runner3d().value(QStringLiteral("model")).toString(), QStringLiteral("male"));
        // a second profile has its own; deleting one removes its avatars
        const qint64 first = profiles.profileId();
        const qint64 second = profiles.createProfileWith({ { QStringLiteral("name"), QStringLiteral("B") },
                                                           { QStringLiteral("runner3d"), QVariantMap { { "model", "female" } } } });
        QVERIFY(second > 0);
        QCOMPARE(profiles.runner3d().value(QStringLiteral("model")).toString(), QStringLiteral("female"));
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("model")).toString(), QStringLiteral("male"));
        // an old "classic" (1.9.8) is read as the male figure
        QVERIFY(profiles.saveProfile({ { QStringLiteral("rower3d"), QVariantMap { { "model", "classic" } } } }));
        QCOMPARE(profiles.rower3d().value(QStringLiteral("model")).toString(), QStringLiteral("male"));
        QVERIFY(profiles.selectProfile(first) || profiles.profileId() == first);
        QCOMPARE(profiles.avatar3d().value(QStringLiteral("model")).toString(), QStringLiteral("female"));
        QVERIFY(profiles.deleteProfile(second));
        QVERIFY(db.setting(QStringLiteral("runnerAvatar/%1").arg(second), QStringLiteral("gone")).toString() == QStringLiteral("gone"));
    }

    void upgradesDevV8SprintRecords()
    {
        // a development build's v8: sprint_records (no kind) -> segment_records
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v8.sqlite"));
        {
            Database db;
            QVERIFY(db.open(path));
            db.seedDefaults();
        }
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v8"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            QVERIFY(q.exec(QStringLiteral("DROP TABLE segment_records")));
            QVERIFY(q.exec(QStringLiteral("CREATE TABLE sprint_records (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                          " profile_id INTEGER NOT NULL, route_key TEXT NOT NULL, route_name TEXT,"
                                          " start_m REAL NOT NULL, length_m REAL NOT NULL, time_sec REAL NOT NULL,"
                                          " ride_id INTEGER, created_at TEXT)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO sprint_records (profile_id, route_key, route_name, start_m,"
                                          " length_m, time_sec) VALUES (1, 'abc', 'r', 500, 200, 21.5)")));
            // orphans (no foreign keys in the old table): a deleted profile, a deleted ride
            QVERIFY(q.exec(QStringLiteral("INSERT INTO sprint_records (profile_id, route_key, route_name, start_m,"
                                          " length_m, time_sec) VALUES (999, 'abc', 'r', 500, 200, 19.0)")));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO sprint_records (profile_id, route_key, route_name, start_m,"
                                          " length_m, time_sec, ride_id) VALUES (1, 'abc', 'r', 500, 200, 25.0, 4242)")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 8")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v8"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
        QVERIFY(!db.hasTable(QStringLiteral("sprint_records")));
        const auto best = db.bestSegment(1, QStringLiteral("sprint"), QStringLiteral("abc"), 500, 200);
        QVERIFY(best.has_value());
        QCOMPARE(best->timeSec, 21.5);
        QCOMPARE(db.segmentAttempts(1, QStringLiteral("sprint"), QStringLiteral("abc"), 500, 200), 2);   // the orphan profile's dropped
        QVERIFY(!db.bestSegment(1, QStringLiteral("kom"), QStringLiteral("abc"), 500, 200).has_value());
    }

    void upgradesV4ToV5PlanKinds()
    {
        // a v4 database (1.2.0): planned_workouts with workout_id NOT NULL
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v4.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v4"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            const char *v2to4[] = {
                "ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'",
                "ALTER TABLE rides ADD COLUMN distance_m REAL",
                "ALTER TABLE profiles ADD COLUMN rowing_ftp INTEGER NOT NULL DEFAULT 150",
                "CREATE TABLE planned_workouts (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, date TEXT NOT NULL,"
                " workout_id INTEGER NOT NULL REFERENCES workouts(id) ON DELETE CASCADE,"
                " position INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,"
                " ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, notes TEXT)",
                "CREATE INDEX idx_plan_profile_date ON planned_workouts(profile_id, date, position)",
                "CREATE TABLE rowing_records (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE, distance_m INTEGER NOT NULL,"
                " time_sec REAL NOT NULL, ride_id INTEGER REFERENCES rides(id) ON DELETE SET NULL, created_at TEXT, trace TEXT)",
                "ALTER TABLE profiles ADD COLUMN gear_setup TEXT",
                "ALTER TABLE ride_samples ADD COLUMN speed REAL",
                "ALTER TABLE profiles ADD COLUMN height_cm REAL NOT NULL DEFAULT 0",
                "INSERT INTO workouts (id, name, category, intervals, sport) VALUES (8, 'Other', 'Endurance',"
                " '[{\"type\":\"steady\",\"name\":\"E\",\"duration\":1200,\"start\":0.6,\"end\":0.6}]', 'cycling')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position, completed, ride_id, notes)"
                " VALUES (5, 1, '2026-10-20', 7, 0, 1, 3, 'done')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position, completed, notes)"
                " VALUES (6, 1, '2026-10-20', 8, 1, 0, 'next')",
                "INSERT INTO planned_workouts (id, profile_id, date, workout_id, position) VALUES (9, 1, '2026-10-22', 7, 0)",
                "PRAGMA user_version = 4",
            };
            for (const char *sql : v2to4)
                QVERIFY2(q.exec(QString::fromUtf8(sql)), sql);
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v4"));
        QCOMPARE(pragmaVersion(path), 4);
        QVERIFY(!columns(path, QStringLiteral("planned_workouts")).contains(QStringLiteral("kind")));

        {
            Database db;
            QVERIFY(db.open(path));
            QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
            // existing entries: same ids, order, completion, ride link, notes; kind workout
            QVector<PlannedWorkout> all = db.plannedWorkouts(1, {}, {});
            QCOMPARE(all.size(), 3);
            QCOMPARE(all.at(0).id, qint64(5));
            QCOMPARE(all.at(0).kind, QStringLiteral("workout"));
            QCOMPARE(all.at(0).workoutId, qint64(7));
            QVERIFY(all.at(0).completed);
            QCOMPARE(all.at(0).rideId, qint64(3));
            QCOMPARE(all.at(0).notes, QStringLiteral("done"));
            QCOMPARE(all.at(0).workoutName, QStringLiteral("My Sweet Spot"));
            QCOMPARE(all.at(0).durationSec, 600);
            QCOMPARE(all.at(1).id, qint64(6));
            QCOMPARE(all.at(1).position, 1);
            QCOMPARE(all.at(1).workoutName, QStringLiteral("Other"));
            QCOMPARE(all.at(2).id, qint64(9));

            // non-workout kinds: NULL workout_id is accepted
            PlannedWorkout fr;
            fr.profileId = 1;
            fr.date = QDate(2026, 10, 20);
            fr.kind = QStringLiteral("freeride");
            fr.plannedDurationSec = 3600;
            fr.options = QStringLiteral("{\"mode\":\"erg\",\"ergWatts\":180}");
            const qint64 frId = db.addPlannedEntry(fr);
            QVERIFY(frId > 9);   // AUTOINCREMENT continues after the copied ids
            PlannedWorkout rlv;
            rlv.profileId = 1;
            rlv.date = QDate(2026, 10, 21);
            rlv.kind = QStringLiteral("rlv");
            rlv.routePath = QStringLiteral("/videos/route/route.xml");
            rlv.routeName = QStringLiteral("Route");
            const qint64 rlvId = db.addPlannedEntry(rlv);
            QVERIFY(rlvId > frId);
            const auto frBack = db.plannedWorkout(frId);
            QVERIFY(frBack);
            QCOMPARE(frBack->kind, QStringLiteral("freeride"));
            QCOMPARE(frBack->workoutId, qint64(-1));
            QCOMPARE(frBack->position, 2);   // end of the day
            QCOMPARE(frBack->plannedDurationSec, 3600);
            QCOMPARE(frBack->options, fr.options);
            QCOMPARE(db.plannedWorkout(rlvId)->routePath, rlv.routePath);
            QCOMPARE(db.plannedWorkout(rlvId)->routeName, rlv.routeName);
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 5);
            // invalid: workout kind without a workout, rlv without route, unknown kind
            PlannedWorkout bad = rlv;
            bad.routePath.clear();
            bad.routeName.clear();   // (with a name it waits for the rider's video: imported plans)
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            bad = fr;
            bad.kind = QStringLiteral("workout");
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            bad.kind = QStringLiteral("swim");
            QCOMPARE(db.addPlannedEntry(bad), qint64(-1));
            {
                // the CHECK constraint guards raw SQL too
                QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
                d.setDatabaseName(path);
                QVERIFY(d.open());
                QSqlQuery q(d);
                QVERIFY(!q.exec(QStringLiteral("INSERT INTO planned_workouts (profile_id, date, kind) VALUES (1, '2026-10-23', 'workout')")));
                d.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("raw"));

            // a workout entry's completion helper never ticks off other kinds
            QCOMPARE(db.completePlannedWorkout(1, 8, QDate(2026, 10, 20), -1), qint64(6));
            QVERIFY(!db.plannedWorkout(frId)->completed);

            // deleting a ride reopens entries of any kind
            QVERIFY(db.setPlannedCompleted(frId, true, 3));
            QVERIFY(db.deleteRide(3));
            QVERIFY(!db.plannedWorkout(5)->completed);
            QCOMPARE(db.plannedWorkout(5)->rideId, qint64(-1));
            QVERIFY(!db.plannedWorkout(frId)->completed);

            // FK cascade still deletes workout entries with their workout - only those
            QVERIFY(db.deleteWorkout(7));
            QVERIFY(!db.plannedWorkout(5));
            QVERIFY(!db.plannedWorkout(9));
            QVERIFY(db.plannedWorkout(6));
            QVERIFY(db.plannedWorkout(frId));
            QVERIFY(db.plannedWorkout(rlvId));
            {
                // the FK itself (not only Database::deleteWorkout's explicit delete)
                QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
                d.setDatabaseName(path);
                QVERIFY(d.open());
                QSqlQuery q(d);
                QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
                QVERIFY(q.exec(QStringLiteral("DELETE FROM workouts WHERE id = 8")));
                d.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("raw"));
            QVERIFY(!db.plannedWorkout(6));
            QCOMPARE(db.plannedWorkouts(1, {}, {}).size(), 2);
            QVERIFY(db.updatePlannedDetails(frId, 1800, QStringLiteral("{\"mode\":\"ride\"}")));
            QCOMPARE(db.plannedWorkout(frId)->plannedDurationSec, 1800);
            QVERIFY(db.updatePlannedDetails(frId, 0, QString()));
            QCOMPARE(db.plannedWorkout(frId)->plannedDurationSec, 0);
            QVERIFY(db.plannedWorkout(frId)->options.isEmpty());
        }
        // reopening a v5 database changes nothing
        Database again;
        QVERIFY(again.open(path));
        QCOMPARE(again.plannedWorkouts(1, {}, {}).size(), 2);
    }

    void partiallyMigratedColumnsAreTolerated()
    {
        // A v1 database that already has one of the v2 columns (e.g. from a
        // development build) still migrates.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("partial.sqlite"));
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("p"));
            d.setDatabaseName(path);
            QVERIFY(d.open());
            QSqlQuery q(d);
            for (const char *sql : kV1Schema)
                QVERIFY(q.exec(QString::fromUtf8(sql)));
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE workouts ADD COLUMN sport TEXT NOT NULL DEFAULT 'cycling'")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("p"));
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(db.workout(7)->sport, QStringLiteral("cycling"));
        QCOMPARE(pragmaVersion(path), Database::kSchemaVersion);
    }

    void profileManagerRowingFtp()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        QCOMPARE(profiles.rowingFtp(), 150);
        QSignalSpy spy(&profiles, &ProfileManager::profileChanged);
        QVERIFY(profiles.setRowingFtp(210));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(db.profile(profiles.profileId())->rowingFtp, 210);
        QVERIFY(!profiles.setRowingFtp(10));   // out of range
        QCOMPARE(profiles.rowingFtp(), 210);
        QCOMPARE(profiles.ftpForSport(QStringLiteral("rowing")), 210);
        QCOMPARE(profiles.ftpForSport(QStringLiteral("cycling")), profiles.ftp());
        QVERIFY(profiles.saveProfile({ { QStringLiteral("rowingFtp"), 180 } }));
        QCOMPARE(profiles.rowingFtp(), 180);

        // helper
        Interval i;
        i.startPercent = i.endPercent = 0.5;
        QCOMPARE(targetWatts(i, QStringLiteral("rowing"), 250, 180), 90);
        QCOMPARE(targetWatts(i, QStringLiteral("cycling"), 250, 180), 125);
        QCOMPARE(ftpForSport(QString(), 250, 180), 250);
    }

    void builderAndLibrarySport()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        QVERIFY(profiles.setRowingFtp(200));
        WorkoutBuilder builder(&db, &profiles);
        builder.newWorkout(QStringLiteral("rowing"));
        QCOMPARE(builder.sport(), QStringLiteral("rowing"));
        QCOMPARE(builder.ftp(), 200);
        builder.setName(QStringLiteral("Row pyramid"));
        builder.setPercent(1, 100);
        QCOMPARE(builder.intervals()->data(builder.intervals()->index(1), IntervalModel::WattsRole).toInt(), 200);
        const qint64 id = builder.save();
        QVERIFY(id > 0);
        QCOMPARE(db.workout(id)->sport, QStringLiteral("rowing"));
        QVERIFY(builder.loadWorkout(id));
        QCOMPARE(builder.sport(), QStringLiteral("rowing"));

        WorkoutLibrary lib(&db, &profiles);
        lib.reload();
        QVERIFY(lib.roleNames().values().contains("sport"));
        lib.setSportFilter(QStringLiteral("rowing"));
        QCOMPARE(lib.rowCount(), 1);
        QCOMPARE(lib.data(lib.index(0), WorkoutLibrary::SportRole).toString(), QStringLiteral("rowing"));
        QCOMPARE(lib.get(id).value(QStringLiteral("sport")).toString(), QStringLiteral("rowing"));
        lib.setSportFilter(QStringLiteral("cycling"));
        QCOMPARE(lib.rowCount(), Workout::builtInLibrary().size());
        lib.setSportFilter(QStringLiteral("running"));
        QCOMPARE(lib.rowCount(), RunWorkout::builtInLibrary().size());
        lib.setSportFilter(QStringLiteral("all"));
        QCOMPARE(lib.rowCount(), Workout::builtInLibrary().size() + RunWorkout::builtInLibrary().size() + 1);
    }
};

QTEST_GUILESS_MAIN(TestMigration)
#include "tst_migration.moc"
