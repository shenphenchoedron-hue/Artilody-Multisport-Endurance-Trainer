#pragma once

#include "Ride.h"

#include <QString>
#include <QVector>

namespace watt::csv {

// Per-second data of one ride.
QString samplesToCsv(const RideRecord &ride, const QVector<Sample> &samples);
// One line per ride (summary).
QString ridesToCsv(const QVector<RideRecord> &rides);
// Writes text atomically (QSaveFile). Returns false and sets error on failure.
bool writeFile(const QString &path, const QString &content, QString *error = nullptr);

} // namespace watt::csv
