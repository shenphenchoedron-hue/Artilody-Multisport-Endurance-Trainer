#include "CsvExporter.h"

#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace watt::csv {

namespace {
QString escape(const QString &field)
{
    if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\n'))) {
        QString f = field;
        f.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + f + QLatin1Char('"');
    }
    return field;
}

QString num(double v, int decimals = 1)
{
    return QString::number(v, 'f', decimals);
}

QString lapClock(double sec)
{
    const double r = std::round(sec * 100.0) / 100.0;
    const int m = int(r / 60);
    return QStringLiteral("%1:%2").arg(m).arg(r - m * 60, 5, 'f', 2, QLatin1Char('0'));
}
} // namespace

QString samplesToCsv(const RideRecord &ride, const QVector<Sample> &samples, const QVector<RideLap> &laps)
{
    QString out;
    QTextStream s(&out);
    s << "# workout," << escape(ride.workoutName) << "\n";
    s << "# started," << ride.startedAt.toString(Qt::ISODate) << "\n";
    s << "# ftp," << ride.ftp << "\n";
    // Free rides record a (virtual) speed: extra column, only then.
    const bool withSpeed = std::any_of(samples.cbegin(), samples.cend(), [](const Sample &x) { return x.speedMps > 0; });
    if (ride.distanceM > 0)
        s << "# distance_m," << num(ride.distanceM) << "\n";
    // looped GPX Ride: the laps (number, time, seconds, distance)
    std::vector<double> lapEnds;   // where each lap ends (s)
    double at = 0;
    for (const RideLap &l : laps) {
        at += std::max(0.0, l.timeSec);
        lapEnds.push_back(double(std::llround(at)));   // (whole seconds: as the FIT laps)
        s << "# lap," << l.lap << ',' << lapClock(l.timeSec) << ',' << num(l.timeSec, 2) << ',' << num(l.distanceM) << "\n";
    }
    s << "seconds,elapsed,power_w,target_w,cadence_rpm,heart_rate_bpm" << (withSpeed ? ",speed_kmh" : "")
      << (laps.isEmpty() ? "" : ",lap") << "\n";
    for (const Sample &x : samples) {
        const QString elapsed = QStringLiteral("%1:%2:%3")
                                    .arg(x.t / 3600, 2, 10, QLatin1Char('0'))
                                    .arg((x.t % 3600) / 60, 2, 10, QLatin1Char('0'))
                                    .arg(x.t % 60, 2, 10, QLatin1Char('0'));
        s << x.t << ',' << elapsed << ',' << x.power << ',' << x.targetPower << ',' << x.cadence << ','
          << x.heartRate;
        if (withSpeed)
            s << ',' << num(x.speedMps * 3.6);
        if (!laps.isEmpty())   // (after the last: the unfinished lap)
            s << ',' << (std::upper_bound(lapEnds.begin(), lapEnds.end(), double(x.t)) - lapEnds.begin()) + 1;
        s << "\n";
    }
    return out;
}

QString ridesToCsv(const QVector<RideRecord> &rides, const std::function<QVector<RideLap>(qint64)> &lapsOf)
{
    QString out;
    QTextStream s(&out);
    s << "date,workout,duration_s,mode,avg_power_w,max_power_w,normalized_power_w,avg_hr_bpm,max_hr_bpm,"
         "avg_cadence_rpm,energy_kj,intensity_factor,tss,ftp_w" << (lapsOf ? ",laps,best_lap_s" : "") << "\n";
    for (const RideRecord &r : rides) {
        s << r.startedAt.toString(Qt::ISODate) << ',' << escape(r.workoutName) << ',' << r.durationSec << ','
          << (r.ergMode ? "ERG" : "Free Ride") << ',' << num(r.avgPower) << ',' << r.maxPower << ','
          << num(r.normalizedPower) << ',' << num(r.avgHeartRate) << ',' << r.maxHeartRate << ','
          << num(r.avgCadence) << ',' << num(r.kilojoules) << ',' << num(r.intensityFactor, 3) << ','
          << num(r.tss) << ',' << r.ftp;
        if (lapsOf) {
            const QVector<RideLap> laps = lapsOf(r.id);
            const double best = bestLapSec(laps);   // (whole laps only)
            s << ',' << laps.size() << ',' << (best > 0 ? num(best, 2) : QString());
        }
        s << "\n";
    }
    return out;
}

bool writeFile(const QString &path, const QString &content, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(content.toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace watt::csv
