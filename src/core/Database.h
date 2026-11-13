#pragma once

#include "PlanTypes.h"
#include "RunWorkout.h"
#include "Profile.h"
#include "Ride.h"
#include "Track.h"
#include "Workout.h"

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QVariant>
#include <optional>

namespace watt {

// Local SQLite storage. Everything the app needs is stored here, so the app
// works fully offline. Each Database instance uses its own connection name,
// which makes it easy to use in-memory databases in unit tests.
class Database : public QObject
{
    Q_OBJECT
public:
    // Schema history (PRAGMA user_version):
    //   1  initial schema
    //   2  workouts.sport, rides.sport, rides.distance_m, profiles.rowing_ftp,
    //      tables planned_workouts and rowing_records
    //   3  profiles.gear_setup (JSON, virtual shifting / free ride bike setup),
    //      ride_samples.speed (m/s, free ride virtual speed; NULL = none)
    //   4  profiles.height_cm (rider height, 0 = not set; air drag of virtual gears)
    //   5  planned_workouts: kind ('workout' | 'freeride' | 'rlv'), route_path,
    //      route_name, planned_duration_sec, options (JSON); workout_id nullable
    //      (table rebuilt once: SQLite cannot drop NOT NULL with ALTER TABLE)
    //   6  run_workouts (treadmill running workouts, speed / % AnT) and
    //      ant_tests (anaerobic threshold tests per profile)
    //   7  running workouts move into workouts (sport 'running', % of the AnT
    //      speed); run_workouts is copied over and dropped
    //   8  segment_records (GPX Ride sprint / KOM times per profile and route)
    //   9  development builds' sprint_records copied into segment_records
    //  10  ride_laps (lap times of looped GPX Rides)
    //  11  route_traces (position over time of GPX Rides / Treadmill Runs: the ghost)
    //  12  uuid on profiles, rides, workouts and route_traces (kUuidTables):
    //      existing rows get one, new rows get one from a trigger (the inserts
    //      never set it) - ids that do not collide when several stations sync
    //      with a server
    //  13  profiles.updated_ms / dirty / picture_hash and profile_tombstones:
    //      profiles kept with a server (UserSync); the existing ones marked to send
    //  14  server_media: the server's media library as last fetched (MediaSync)
    //  15  rides.synced / station and ride_tombstones: rides kept with a server
    //      (RideSync); the existing ones marked to send
    static constexpr int kSchemaVersion = 15;
    // The tables whose rows have a uuid (schema v12).
    static const QStringList &uuidTables();
    // SQL expression giving a random (version 4) UUID, lower case, e.g.
    // "1b4e28ba-2fa1-41d2-883f-0016d3cca427".
    static QString uuidSql();

    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    // Opens (and creates/migrates) the database. Use ":memory:" for tests.
    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }
    static QString defaultPath();

    // Seeds built-in workouts and a default profile when the DB is empty, and
    // restores the built-in FTP test if it is missing.
    void seedDefaults();
    // The starter profile a new station is seeded with (sent to no server).
    static Profile starterProfile();
    bool hasBuiltInFtpTest() const;

    // Settings (key/value)
    QVariant setting(const QString &key, const QVariant &defaultValue = {}) const;
    void setSetting(const QString &key, const QVariant &value);
    void removeSetting(const QString &key);
    // This station's own uuid: made once, kept in the settings ("station/id")
    // with the machine it was made on ("station/machine") - a database copied
    // to another machine gets a new one there. Tells the server which station
    // sent what.
    QString stationId();
    // The uuid of a row of one of uuidTables() ("" when none).
    QString uuidOf(const QString &table, qint64 id) const;

    // Profiles
    QVector<Profile> profiles() const;
    std::optional<Profile> profile(qint64 id) const;
    // Insert when id < 0, returns id or -1. changed: stamped now and marked to
    // send to the server (UserSync) - false only for the seeded starter profile.
    qint64 saveProfile(const Profile &profile, bool changed = true);
    // tellServer: the server is told (a tombstone) - false when the server
    // itself deleted it.
    bool deleteProfile(qint64 id, bool tellServer = true);

    // Profiles and the server (schema v13, UserSync):
    QVector<Profile> profilesToSend() const;                       // changed here, not sent yet
    QVector<QPair<QString, qint64>> profileTombstones() const;     // deleted here: uuid, when (ms)
    int profilesWaiting() const;                                   // both of the above
    // Sent: no longer to send - unless it changed again since (updatedMs is
    // the version sent). pictureHash: its picture as the server has it now.
    bool markProfileSent(const QString &uuid, qint64 updatedMs, const QString &pictureHash);
    bool removeProfileTombstone(const QString &uuid);
    qint64 profileIdForUuid(const QString &uuid) const;           // -1: none
    // The server's version of a profile (p.uuid, p.updatedMs, p.pictureHash
    // set): inserted or updated by its uuid, not marked to send. Its id or -1.
    qint64 storeServerProfile(const Profile &p);
    int profileRideCount(qint64 id) const;
    // The same rider twice (joined on the server): everything of `from` -
    // rides, records, plans, traces, AnT tests - moves to `into`, and `from`
    // is deleted (the server is not told). False on an error.
    bool mergeProfile(qint64 from, qint64 into);
    bool setProfileUuid(qint64 id, const QString &uuid);
    void markAllProfilesToSend();   // (another server: all of them again)

    // The server's media library as last fetched (schema v14, MediaSync).
    QVector<ServerMedia> serverMedia() const;                      // by kind, name
    bool storeServerMedia(const ServerMedia &m);                   // by uuid; keeps its localPath
    bool removeServerMedia(const QString &uuid);
    bool setServerMediaLocalPath(const QString &uuid, const QString &path);
    void clearServerMedia();                                       // (another server)

    // Rides and the server (schema v15, RideSync).
    QVector<RideRecord> ridesToSend(int limit) const;              // recorded here, not sent, oldest first
    int ridesWaiting() const;                                      // those and the deletions to send
    bool markRideSent(const QString &uuid);
    void markAllRidesToSend();                                     // (another server)
    QVector<QPair<QString, qint64>> rideTombstones() const;        // deleted here: uuid, when (ms)
    bool removeRideTombstone(const QString &uuid);
    bool rideTombstoned(const QString &uuid) const;
    qint64 rideIdForUuid(const QString &uuid) const;               // -1: none
    std::optional<RouteTrace> routeTraceForRide(qint64 rideId) const;   // with its trace
    // A ride from the server (r.uuid, r.profileId mapped here, r.station set):
    // stored with its samples, laps and ghost trace, marked sent. Its id, or -1
    // (already here / error).
    qint64 storeServerRide(const RideRecord &r, const QVector<Sample> &samples, const QVector<RideLap> &laps,
                           const std::optional<RouteTrace> &trace);
    // The ghost traces of the other profiles on a route (riderName set), newest first.
    QVector<RouteTrace> routeTracesOfOthers(qint64 profileId, const QString &sport, const QString &routeKey, double startM,
                                            bool loop) const;
    // Free ride bike/gear setup of a profile (GearSetup JSON, schema v3).
    // Empty when none was stored yet.
    QString gearSetupJson(qint64 profileId) const;
    bool setGearSetupJson(qint64 profileId, const QString &json);

    // Workouts
    QVector<Workout> workouts() const;
    std::optional<Workout> workout(qint64 id) const;
    qint64 saveWorkout(const Workout &workout);    // insert when id < 0
    bool deleteWorkout(qint64 id);                 // refuses protected (built-in FTP test) workouts
    bool setWorkoutFavorite(qint64 id, bool favorite);
    bool incrementWorkoutUsage(qint64 id);

    // Rides
    qint64 saveRide(const RideRecord &ride, const QVector<Sample> &samples);
    QVector<RideRecord> rides(qint64 profileId, const QDateTime &from = {}, int limit = -1) const;
    std::optional<RideRecord> ride(qint64 id) const;
    QVector<Sample> rideSamples(qint64 rideId) const;
    // Reopens plan entries completed by the ride. tellServer: the server is
    // told (a tombstone) - false when the server itself deleted it.
    bool deleteRide(qint64 id, bool tellServer = true);
    // A looped GPX Ride's lap times (deleted with the ride).
    bool saveRideLaps(qint64 rideId, const QVector<RideLap> &laps);
    QVector<RideLap> rideLaps(qint64 rideId) const;

    // Workout plan (calendar). Workout entries whose workout no longer exists
    // are never returned and are deleted together with the workout; Free Ride
    // and Video Ride entries (no workout) are always returned.
    QVector<PlannedWorkout> plannedWorkouts(qint64 profileId, const QDate &from, const QDate &to,
                                            bool onlyOpen = false, int limit = -1) const;
    std::optional<PlannedWorkout> plannedWorkout(qint64 id) const;
    qint64 addPlannedWorkout(qint64 profileId, const QDate &date, qint64 workoutId, const QString &notes = {});
    // Any kind (e.profileId, date, kind, workoutId for "workout", routePath for
    // "rlv", routeName, plannedDurationSec, options, notes). Appended at the
    // end of the day. Returns the id or -1 (lastError).
    qint64 addPlannedEntry(const PlannedWorkout &e);
    // planned_duration_sec (<= 0 -> NULL) and options of an entry.
    bool updatePlannedDetails(qint64 id, int plannedDurationSec, const QString &options);
    bool removePlannedWorkout(qint64 id);
    bool movePlannedWorkout(qint64 id, const QDate &date);   // appended at the end of the new day
    bool setPlannedCompleted(qint64 id, bool completed, qint64 rideId = -1);
    bool setPlannedNotes(qint64 id, const QString &notes);
    // Marks the first open workout entry of workoutId on date completed. Returns its id or -1.
    qint64 completePlannedWorkout(qint64 profileId, qint64 workoutId, const QDate &date, qint64 rideId);

    // Rowing records (ghost race). Traces are stored as compact JSON [[t,d],...].
    qint64 saveRowingRecord(const RowingRecord &record);
    std::optional<RowingRecord> bestRowingRecord(qint64 profileId, int distanceM) const;
    QVector<RowingRecord> bestRowingRecords(qint64 profileId, bool withTrace = false) const;   // one per distance
    static QString traceToText(const QVector<TracePoint> &trace);
    static QVector<TracePoint> traceFromText(const QString &text);

    // GPX Ride sprint / KOM times (every attempt of a saved ride; a discarded
    // ride's are deleted). The best is the fastest, then the oldest, of a
    // profile for a kind on a route (key) and length.
    qint64 saveSegmentRecord(const SegmentRecord &record);
    // (a segment: kind, route, where it starts and its length, each within 0.5 m)
    std::optional<SegmentRecord> bestSegment(qint64 profileId, const QString &kind, const QString &routeKey,
                                             double startM, double lengthM) const;
    int segmentAttempts(qint64 profileId, const QString &kind, const QString &routeKey, double startM,
                        double lengthM) const;
    bool setSegmentRideId(qint64 recordId, qint64 rideId);
    bool deleteSegmentRecord(qint64 recordId);

    // Ghost traces of GPX Rides / Treadmill Runs (deleted with their ride).
    qint64 saveRouteTrace(const RouteTrace &trace);
    // A profile's traces of one sport on one route, ridden from the same start
    // (within 5 m) in the same way (loop or not): newest first.
    QVector<RouteTrace> routeTraces(qint64 profileId, const QString &sport, const QString &routeKey, double startM,
                                    bool loop, bool withTrace = false) const;
    std::optional<RouteTrace> routeTrace(qint64 id) const;   // with its trace

    // Running workouts are plain workouts of sport "running" (workouts()).
    // Current AnT speed of a profile: newest ant_tests entry, else the default.
    double currentAntKmh(qint64 profileId) const;

    // Anaerobic threshold tests, newest first.
    QVector<AntTest> antTests(qint64 profileId, int limit = -1) const;
    std::optional<AntTest> latestAntTest(qint64 profileId) const;
    qint64 saveAntTest(const AntTest &test);             // insert when id < 0
    bool deleteAntTest(qint64 id);

    // Music
    QVector<Playlist> playlists() const;
    qint64 createPlaylist(const QString &name);
    bool renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    QVector<Track> playlistTracks(qint64 playlistId) const;
    qint64 addTrack(qint64 playlistId, const Track &track);
    bool removeTrack(qint64 trackId);
    bool reorderTracks(qint64 playlistId, const QVector<qint64> &trackIdsInOrder);
    bool updateTrackInfo(qint64 trackId, const QString &title, const QString &artist, qint64 durationMs);

    bool hasTable(const QString &table) const;

private:
    bool exec(const QString &sql);
    bool migrate();
    bool createSchemaV1();
    bool migrateToV2();
    bool migrateToV3();
    bool migrateToV4();
    bool migrateToV5();
    bool migrateToV6();
    bool migrateToV7();
    bool migrateToV8();
    bool migrateToV9();
    bool migrateToV10();
    bool migrateToV11();
    bool migrateToV12();
    bool migrateToV13();
    bool migrateToV14();
    bool migrateToV15();
    bool columnNotNull(const QString &table, const QString &column) const;
    bool hasColumn(const QString &table, const QString &column) const;
    bool addColumnIfMissing(const QString &table, const QString &column, const QString &definition);
    int userVersion() const;
    void ensureBuiltInFtpTest();
    QSqlDatabase db() const;
    void setError(const QString &context, const QString &error) const;

    QString m_connection;
    mutable QString m_lastError;
};

} // namespace watt
