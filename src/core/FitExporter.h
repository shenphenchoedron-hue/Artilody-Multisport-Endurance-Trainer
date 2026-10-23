#pragma once

#include "Ride.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// Minimal, independently written encoder for FIT activity files (the binary
// format read by Garmin Connect, Strava, TrainingPeaks, intervals.icu, ...).
// Only the subset needed for an indoor ride is produced:
//   file_id, device_info, event (timer start / stop_all), record (1 Hz),
//   lap (one, covering the ride), session, activity.
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
