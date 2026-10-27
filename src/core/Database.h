#pragma once

#include "PlanTypes.h"
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
    static constexpr int kSchemaVersion = 5;

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
    bool hasBuiltInFtpTest() const;

    // Settings (key/value)
    QVariant setting(const QString &key, const QVariant &defaultValue = {}) const;
    void setSetting(const QString &key, const QVariant &value);

    // Profiles
    QVector<Profile> profiles() const;
    std::optional<Profile> profile(qint64 id) const;
    qint64 saveProfile(const Profile &profile);    // insert when id < 0, returns id or -1
    bool deleteProfile(qint64 id);
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
    bool deleteRide(qint64 id);   // reopens plan entries completed by the ride

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

private:
    bool exec(const QString &sql);
    bool migrate();
    bool createSchemaV1();
    bool migrateToV2();
    bool migrateToV3();
    bool migrateToV4();
    bool migrateToV5();
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
