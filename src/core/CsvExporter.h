#pragma once

#include "Ride.h"

#include <QString>
#include <QVector>
#include <functional>

namespace watt::csv {

// Per-second data of one ride. laps (a looped GPX Ride): a "# lap" header
// line per lap and a lap column (the lap each second belongs to).
QString samplesToCsv(const RideRecord &ride, const QVector<Sample> &samples, const QVector<RideLap> &laps = {});
// One line per ride (summary); lapsOf (optional): a ride's laps -> the laps
// and best_lap_s columns.
QString ridesToCsv(const QVector<RideRecord> &rides, const std::function<QVector<RideLap>(qint64)> &lapsOf = {});
// Writes text atomically (QSaveFile). Returns false and sets error on failure.
bool writeFile(const QString &path, const QString &content, QString *error = nullptr);

} // namespace watt::csv
