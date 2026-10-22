#pragma once

#include "Interval.h"
#include "Zones.h"

#include <QJsonArray>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace watt {

// One coloured bar of a workout profile chart.
struct Segment
{
    int startSec = 0;
    int durationSec = 0;
    int watts = 0;
    int zone = 0;   // 0..5
};

struct Workout
{
    qint64 id = -1;
    QString name;
    QString category = QStringLiteral("Intervals");   // Endurance, Intervals, Recovery, FTP Tests, Custom
    QString focus;          // short focus text, e.g. "Sweet Spot"
    QString description;
    bool favorite = false;
    bool builtIn = false;
    int timesUsed = 0;
    QVector<Interval> intervals;

    int totalDurationSec() const;
    int intervalIndexAt(int second) const;   // -1 when outside the workout
    int intervalStartSec(int index) const;
    int targetWattsAt(double second, int ftp) const;

    double averagePercent() const;           // time weighted mean intensity
    double intensityFactor() const;          // 4th-power mean, like NP / FTP
    double estimatedTss() const;

    QString summary(int ftp) const;          // "3 × 12 min @ 225 W"
    QString intervalsLabel() const;          // "3 × 12 min"

    QVector<Segment> segments(int ftp, const ZoneSet &zones, int rampStepSec = 60) const;
    static QVariantList segmentsToVariant(const QVector<Segment> &segments);

    QJsonArray intervalsToJson() const;
    static QVector<Interval> intervalsFromJson(const QJsonArray &array);

    static QVector<Workout> builtInLibrary();

    // Workouts in this category are FTP tests (incl. personal copies).
    static QString ftpTestCategory() { return QStringLiteral("FTP Tests"); }
    bool isFtpTest() const { return category == ftpTestCategory(); }
    // The built-in FTP test is the basis for FTP testing and can never be deleted.
    bool isProtected() const { return builtIn && isFtpTest(); }
};

int zoneForInterval(const Interval &interval, double percent, const ZoneSet &zones);

} // namespace watt
