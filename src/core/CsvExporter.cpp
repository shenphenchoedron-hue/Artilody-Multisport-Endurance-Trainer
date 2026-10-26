#include "CsvExporter.h"

#include <QSaveFile>
#include <QTextStream>
#include <algorithm>

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
} // namespace

QString samplesToCsv(const RideRecord &ride, const QVector<Sample> &samples)
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
    s << "seconds,elapsed,power_w,target_w,cadence_rpm,heart_rate_bpm" << (withSpeed ? ",speed_kmh" : "") << "\n";
    for (const Sample &x : samples) {
        const QString elapsed = QStringLiteral("%1:%2:%3")
                                    .arg(x.t / 3600, 2, 10, QLatin1Char('0'))
                                    .arg((x.t % 3600) / 60, 2, 10, QLatin1Char('0'))
                                    .arg(x.t % 60, 2, 10, QLatin1Char('0'));
        s << x.t << ',' << elapsed << ',' << x.power << ',' << x.targetPower << ',' << x.cadence << ','
          << x.heartRate;
        if (withSpeed)
            s << ',' << num(x.speedMps * 3.6);
        s << "\n";
    }
    return out;
}

QString ridesToCsv(const QVector<RideRecord> &rides)
{
    QString out;
    QTextStream s(&out);
    s << "date,workout,duration_s,mode,avg_power_w,max_power_w,normalized_power_w,avg_hr_bpm,max_hr_bpm,"
         "avg_cadence_rpm,energy_kj,intensity_factor,tss,ftp_w\n";
    for (const RideRecord &r : rides) {
        s << r.startedAt.toString(Qt::ISODate) << ',' << escape(r.workoutName) << ',' << r.durationSec << ','
          << (r.ergMode ? "ERG" : "Free Ride") << ',' << num(r.avgPower) << ',' << r.maxPower << ','
          << num(r.normalizedPower) << ',' << num(r.avgHeartRate) << ',' << r.maxHeartRate << ','
          << num(r.avgCadence) << ',' << num(r.kilojoules) << ',' << num(r.intensityFactor, 3) << ','
          << num(r.tss) << ',' << r.ftp << "\n";
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
