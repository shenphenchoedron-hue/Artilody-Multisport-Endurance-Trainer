#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace watt {

// One workout scheduled on a calendar day (table planned_workouts, schema v2).
// The workout fields are filled from a join with the workouts table.
struct PlannedWorkout
{
    qint64 id = -1;
    qint64 profileId = -1;
    QDate date;
    qint64 workoutId = -1;
    int position = 0;            // order within the day
    bool completed = false;
    qint64 rideId = -1;          // ride that completed the entry, -1 = none
    QString notes;

    // joined from workouts
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
