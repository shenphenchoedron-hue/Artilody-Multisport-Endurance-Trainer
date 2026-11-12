#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace watt {

// Kinds of calendar entries (planned_workouts.kind, schema v5).
namespace plankind {
inline QString workout() { return QStringLiteral("workout"); }   // a workout of the library
inline QString freeRide() { return QStringLiteral("freeride"); } // Free Ride (FreeRideSession)
inline QString video() { return QStringLiteral("rlv"); }         // Video Ride (RlvSession)
inline QString gpx() { return QStringLiteral("gpx"); }           // GPX Ride (RlvSession, Kind::Gpx)
inline QString freeRow() { return QStringLiteral("row"); }       // free row (RowingSession, open)
inline QString freeRun() { return QStringLiteral("run"); }       // free treadmill run (RunSession, no workout)
inline bool isRoute(const QString &k) { return k == video() || k == gpx(); }
inline bool isFree(const QString &k) { return k == freeRide() || k == freeRow() || k == freeRun(); }
inline bool isValid(const QString &k)
{
    return k == workout() || isFree(k) || isRoute(k);
}
} // namespace plankind

// One entry scheduled on a calendar day (table planned_workouts, schema v2;
// kinds since v5). The workout fields are filled from a (left) join with the
// workouts table - only for kind "workout".
struct PlannedWorkout
{
    qint64 id = -1;
    qint64 profileId = -1;
    QDate date;
    qint64 workoutId = -1;       // kind "workout" only (NULL in the table otherwise)
    int position = 0;            // order within the day
    bool completed = false;
    qint64 rideId = -1;          // ride that completed the entry, -1 = none
    QString notes;

    // schema v5
    QString kind = plankind::workout();
    QString routePath;           // kind "rlv" / "gpx": route file (xml/gpx); "" = a video still to choose
    QString routeName;           // kind "rlv" / "gpx": cached display name
    int plannedDurationSec = 0;  // kind "freeride" / "row" / "run": planned duration (0 = open)
    QString options;             // JSON object text (kind specific, see WorkoutPlanner)

    // joined from workouts (kind "workout")
    QString workoutName;
    QString sport;
    QString category;
    int durationSec = 0;
};

// One sample of a rowing trace: distance (m) after t seconds.
struct TracePoint
{
    double t = 0;
    double d = 0;
    bool operator==(const TracePoint &o) const { return t == o.t && d == o.d; }
};

// A finished rowing piece over a fixed distance (table rowing_records).
struct RowingRecord
{
    qint64 id = -1;
    qint64 profileId = -1;
    int distanceM = 0;
    double timeSec = 0;
    qint64 rideId = -1;
    QDateTime createdAt;
    QVector<TracePoint> trace;
};

// One timed segment of a GPX Ride (a RouteSegment placed in the GPX Workshop:
// kind "sprint" / "kom"): every attempt of a saved ride is kept.
struct SegmentRecord
{
    qint64 id = -1;
    qint64 profileId = -1;
    QString kind;          // "sprint" / "kom"
    QString routeKey;      // sprint::routeKey(): the GPX track's fingerprint
    QString routeName;
    double startM = 0;     // where the segment starts on the route (m)
    double lengthM = 0;
    double timeSec = 0;
    qint64 rideId = -1;    // the saved ride it was part of (-1 = none)
    QDateTime createdAt;
};

// An item of the server's media library (table server_media, schema v14;
// MediaSync): a GPX route (one .gpx) or a video route (a folder: video +
// .gpx [+ .xml]) - its files by name, sha1 and size.
struct ServerMediaFile
{
    QString name, hash;
    qint64 size = 0;
};
struct ServerMedia
{
    QString uuid;
    QString kind;            // "gpx" / "video"
    QString name;
    QString folder;          // video: its folder on the station
    QVector<ServerMediaFile> files;
    qint64 sizeBytes = 0;
    qint64 updatedMs = 0;
    QString uploadedBy;
    QString localPath;       // where it is on this station ("" = not here / not known): gpx file / video folder
};

// Where a saved GPX Ride / Treadmill Run was on its route second by second
// (table route_traces, schema v11): the ghost of a later ride on the same
// route races it. The trace holds [t (s), metres covered since the start].
struct RouteTrace
{
    qint64 id = -1;
    QString uuid;            // (schema v12; given by the database, read only)
    qint64 profileId = -1;
    QString riderName;       // (another profile's: Database::routeTracesOfOthers)
    qint64 rideId = -1;      // the saved ride (deleted with it)
    QString sport;           // "gpx" (GPX Ride) / "run" (Treadmill Run)
    QString routeKey;        // sprint::routeKey(): the track's fingerprint
    QString routeName;
    double startM = 0;       // where on the route the ride started (m)
    bool loop = false;       // ridden as a loop (lap after lap)
    bool completed = false;  // reached the route's end (a loop: at least one lap)
    double durationSec = 0;
    double distanceM = 0;    // metres covered
    QDateTime createdAt;
    QVector<TracePoint> trace;   // (empty unless asked for)
};

} // namespace watt
