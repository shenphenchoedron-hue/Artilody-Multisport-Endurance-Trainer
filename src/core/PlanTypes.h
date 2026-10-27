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
inline bool isValid(const QString &k) { return k == workout() || k == freeRide() || k == video(); }
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
    QString routePath;           // kind "rlv": route file (xml/gpx)
    QString routeName;           // kind "rlv": cached display name
    int plannedDurationSec = 0;  // kind "freeride": planned duration (0 = open)
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

} // namespace watt
