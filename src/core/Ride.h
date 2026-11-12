#pragma once

#include <algorithm>

#include "Zones.h"

#include <QDateTime>
#include <QString>
#include <QVector>
#include <array>

namespace watt {

// One recorded second of a ride.
struct Sample
{
    int t = 0;              // seconds since start
    int power = 0;          // W
    int targetPower = 0;    // W (0 in free ride without workout)
    int cadence = 0;        // rpm
    int heartRate = 0;      // bpm
    double speedMps = 0;    // m/s (free ride virtual speed; 0 = not recorded)
};

// Summary of a finished ride as stored in the database.
struct RideRecord
{
    qint64 id = -1;
    QString uuid;   // (schema v12; given by the database, read only)
    // The server (schema v15, RideSync; read only): sent to it, and the station
    // it was recorded on ("" = this one)
    bool synced = false;
    QString station;
    qint64 profileId = -1;
    qint64 workoutId = -1;
    QString workoutName;
    QDateTime startedAt;
    int durationSec = 0;
    int blocks = 0;
    bool ergMode = true;
    int ftp = 250;
    double avgPower = 0;
    int maxPower = 0;
    double normalizedPower = 0;
    double avgHeartRate = 0;
    int maxHeartRate = 0;
    double avgCadence = 0;
    double kilojoules = 0;
    double intensityFactor = 0;
    double tss = 0;
    std::array<int, kZoneCount> zoneSeconds {};
    QString sport = QStringLiteral("cycling");   // "cycling" or "rowing" (schema v2)
    double distanceM = 0;                         // rowing / free ride distance in metres (0 = unknown)
};

// A finished lap of a looped GPX Ride (schema v10, table ride_laps).
struct RideLap
{
    int lap = 0;          // 1, 2, ...
    double timeSec = 0;
    double distanceM = 0;
    bool operator==(const RideLap &o) const { return lap == o.lap && timeSec == o.timeSec && distanceM == o.distanceM; }
};

// The fastest whole lap (a lap 1 begun inside the route is shorter: left
// out); 0 = none.
inline double bestLapSec(const QVector<RideLap> &laps)
{
    double full = 0, best = 0;
    for (const RideLap &l : laps)
        full = std::max(full, l.distanceM);
    for (const RideLap &l : laps)
        if (l.distanceM >= full - 1.0 && l.timeSec > 0)
            best = best > 0 ? std::min(best, l.timeSec) : l.timeSec;
    return best;
}

} // namespace watt
