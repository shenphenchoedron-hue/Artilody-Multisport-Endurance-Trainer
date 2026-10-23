#pragma once

#include "Ride.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// Minimal, independently written encoder for FIT activity files (the binary
// format read by Garmin Connect, Strava, TrainingPeaks, intervals.icu, ...).
// Only the subset needed for an indoor ride or row is produced:
//   file_id, device_info, sport, event (timer start / stop_all), record (1 Hz),
//   lap (one, covering the ride), session, activity.
// RideRecord::sport "rowing" is written as sport rowing / indoor_rowing,
// anything else as cycling / indoor_cycling. RideRecord::distanceM > 0 adds
// total_distance + avg_speed to lap and session; for rows a cumulative record
// distance is derived from the total in proportion to power^(1/3).
namespace watt::fit {

// Seconds between the Unix epoch and the FIT epoch (1989-12-31T00:00:00Z).
constexpr qint64 kFitEpochOffset = 631065600;

// CRC-16 as used by FIT (header CRC and file CRC). Exposed for tests.
quint16 crc16(const QByteArray &data, quint16 crc = 0);

// Encodes a complete FIT activity file (header, messages, trailing CRC).
QByteArray encodeActivity(const RideRecord &ride, const QVector<Sample> &samples);

// Encodes and writes atomically (QSaveFile). Returns false and sets error on failure.
bool writeFile(const QString &path, const RideRecord &ride, const QVector<Sample> &samples,
               QString *error = nullptr);

} // namespace watt::fit
