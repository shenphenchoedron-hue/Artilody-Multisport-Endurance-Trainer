#pragma once

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
};

// Summary of a finished ride as stored in the database.
struct RideRecord
{
    qint64 id = -1;
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
};

} // namespace watt
