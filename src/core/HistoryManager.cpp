#include "HistoryManager.h"

#include "CsvExporter.h"
#include "Database.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "TimeFormat.h"

#include <QDir>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace watt {

HistoryManager::HistoryManager(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    connect(m_profiles, &ProfileManager::profileChanged, this, &HistoryManager::refresh);
}

void HistoryManager::setRangeDays(int days)
{
    if (m_rangeDays == days)
        return;
    m_rangeDays = days;
    refresh();
}

QVariantList HistoryManager::rangeOptions() const
{
    auto opt = [](const QString &label, int days) {
        return QVariantMap { { QStringLiteral("label"), label }, { QStringLiteral("days"), days } };
    };
    return { opt(QStringLiteral("Last 7 days"), 7), opt(QStringLiteral("Last 30 days"), 30),
             opt(QStringLiteral("Last 90 days"), 90), opt(QStringLiteral("Last 12 months"), 365),
             opt(QStringLiteral("All time"), 0) };
}

void HistoryManager::refresh()
{
    QDateTime from;
    if (m_rangeDays > 0)
        from = QDateTime(QDate::currentDate().addDays(-(m_rangeDays - 1)), QTime(0, 0));
    m_rides = m_db->rides(m_profiles->profileId(), from);
    emit dataChanged();

    bool stillThere = false;
    for (const RideRecord &r : m_rides)
        stillThere = stillThere || r.id == m_selected.id;
    if (!stillThere)
        selectRide(m_rides.isEmpty() ? -1 : m_rides.first().id);
}

int HistoryManager::totalSeconds() const
{
    int s = 0;
    for (const RideRecord &r : m_rides)
        s += r.durationSec;
    return s;
}

QString HistoryManager::totalTimeText() const
{
    return formatHoursMinutes(totalSeconds(), true);
}

int HistoryManager::avgPower() const
{
    const int secs = totalSeconds();
    if (secs <= 0)
        return 0;
    double weighted = 0;
    for (const RideRecord &r : m_rides)
        weighted += r.avgPower * r.durationSec;
    return int(std::lround(weighted / secs));
}

int HistoryManager::totalKj() const
{
    double kj = 0;
    for (const RideRecord &r : m_rides)
        kj += r.kilojoules;
    return int(std::lround(kj));
}

int HistoryManager::totalTss() const
{
    double t = 0;
    for (const RideRecord &r : m_rides)
        t += r.tss;
    return int(std::lround(t));
}

QVariantList HistoryManager::rides() const
{
    QVariantList list;
    const QLocale en(QLocale::English);
    for (const RideRecord &r : m_rides) {
        QVariantMap m;
        m.insert(QStringLiteral("rideId"), r.id);
        m.insert(QStringLiteral("date"), en.toString(r.startedAt.date(), QStringLiteral("MMM d")));
        m.insert(QStringLiteral("name"), r.workoutName);
        m.insert(QStringLiteral("durationMin"), int(std::lround(r.durationSec / 60.0)));
        m.insert(QStringLiteral("avgPower"), int(std::lround(r.avgPower)));
        list.append(m);
    }
    return list;
}

QVariantList HistoryManager::weeklyHours() const
{
    // Hours per weekday (Mon..Sun) within the selected range.
    double hours[7] = {};
    for (const RideRecord &r : m_rides)
        hours[r.startedAt.date().dayOfWeek() - 1] += r.durationSec / 3600.0;
    QVariantList list;
    for (double h : hours)
        list.append(h);
    return list;
}

QVariantList HistoryManager::zonesToVariant(const std::array<int, kZoneCount> &zones)
{
    int total = 0;
    for (int s : zones)
        total += s;
    QVariantList list;
    const QStringList names = powerZoneNames();
    for (int z = 0; z < kZoneCount; ++z) {
        QVariantMap m;
        m.insert(QStringLiteral("zone"), z);
        m.insert(QStringLiteral("name"), names.at(z));
        m.insert(QStringLiteral("seconds"), zones[size_t(z)]);
        m.insert(QStringLiteral("timeText"), formatHoursMinutes(zones[size_t(z)], true));
        m.insert(QStringLiteral("fraction"), total > 0 ? zones[size_t(z)] / double(total) : 0.0);
        list.append(m);
    }
    return list;
}

QVariantList HistoryManager::zoneDistribution() const
{
    std::array<int, kZoneCount> sum {};
    for (const RideRecord &r : m_rides) {
        for (int z = 0; z < kZoneCount; ++z)
            sum[size_t(z)] += r.zoneSeconds[size_t(z)];
    }
    return zonesToVariant(sum);
}

QVariantList HistoryManager::selectedZones() const
{
    return zonesToVariant(m_selected.zoneSeconds);
}

void HistoryManager::selectRide(qint64 id)
{
    m_selected = RideRecord();
    m_selSamples.clear();
    m_selPower.clear();
    m_selTarget.clear();
    m_selHr.clear();
    m_selIsFtpTest = false;
    m_selBest1min = 0;
    m_selEstimatedFtp = 0;
    if (id >= 0) {
        if (auto r = m_db->ride(id)) {
            m_selected = *r;
            m_selSamples = m_db->rideSamples(id);
            for (const Sample &s : m_selSamples) {
                m_selPower.append(s.power);
                m_selTarget.append(s.targetPower);
                m_selHr.append(s.heartRate);
            }
            // FTP test ride: the workout's category decides; if the workout was
            // deleted meanwhile, fall back to its name.
            if (auto w = m_db->workout(r->workoutId))
                m_selIsFtpTest = w->isFtpTest();
            else
                m_selIsFtpTest = r->workoutName.contains(QStringLiteral("FTP"), Qt::CaseInsensitive);
            if (m_selIsFtpTest) {
                m_selBest1min = analysis::bestAveragePower(m_selSamples, analysis::kRampTestWindowSec);
                m_selEstimatedFtp = analysis::plausibleRampFtp(m_selSamples);
            }
        }
    }
    emit selectionChanged();
}

QVariantMap HistoryManager::selectedRide() const
{
    QVariantMap m;
    if (m_selected.id < 0)
        return m;
    const QLocale en(QLocale::English);
    const RideRecord &r = m_selected;
    m.insert(QStringLiteral("rideId"), r.id);
    m.insert(QStringLiteral("name"), r.workoutName);
    m.insert(QStringLiteral("date"), en.toString(r.startedAt.date(), QStringLiteral("MMM d, yyyy")));
    m.insert(QStringLiteral("time"), en.toString(r.startedAt.time(), QStringLiteral("HH:mm")));
    m.insert(QStringLiteral("durationSec"), r.durationSec);
    m.insert(QStringLiteral("durationMin"), int(std::lround(r.durationSec / 60.0)));
    m.insert(QStringLiteral("blocks"), r.blocks);
    m.insert(QStringLiteral("mode"), r.ergMode ? QStringLiteral("ERG") : QStringLiteral("Free Ride"));
    m.insert(QStringLiteral("avgPower"), int(std::lround(r.avgPower)));
    m.insert(QStringLiteral("maxPower"), r.maxPower);
    m.insert(QStringLiteral("np"), int(std::lround(r.normalizedPower)));
    m.insert(QStringLiteral("avgHr"), int(std::lround(r.avgHeartRate)));
    m.insert(QStringLiteral("maxHr"), r.maxHeartRate);
    m.insert(QStringLiteral("cadence"), int(std::lround(r.avgCadence)));
    m.insert(QStringLiteral("kj"), int(std::lround(r.kilojoules)));
    m.insert(QStringLiteral("if"), r.intensityFactor);
    m.insert(QStringLiteral("tss"), int(std::lround(r.tss)));
    m.insert(QStringLiteral("ftp"), r.ftp);
    m.insert(QStringLiteral("isFtpTest"), m_selIsFtpTest);
    m.insert(QStringLiteral("best1min"), m_selBest1min);
    m.insert(QStringLiteral("estimatedFtp"), m_selEstimatedFtp);
    return m;
}

bool HistoryManager::deleteRide(qint64 id)
{
    if (!m_db->deleteRide(id))
        return false;
    refresh();
    return true;
}

void HistoryManager::setMessage(const QString &m)
{
    m_lastMessage = m;
    emit lastMessageChanged();
}

QUrl HistoryManager::suggestedExportUrl(bool summary) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QString name;
    if (summary || m_selected.id < 0) {
        name = QStringLiteral("watt-history-%1.csv").arg(QDate::currentDate().toString(Qt::ISODate));
    } else {
        QString w = m_selected.workoutName;
        w.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("-"));
        name = QStringLiteral("ride-%1-%2.csv").arg(m_selected.startedAt.toString(QStringLiteral("yyyyMMdd-HHmm")), w);
    }
    return QUrl::fromLocalFile(QDir(dir).filePath(name));
}

bool HistoryManager::exportSelectedCsv(const QUrl &file)
{
    if (m_selected.id < 0) {
        setMessage(QStringLiteral("No ride selected"));
        return false;
    }
    QString err;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!csv::writeFile(path, csv::samplesToCsv(m_selected, m_selSamples), &err)) {
        setMessage(QStringLiteral("Export failed: %1").arg(err));
        return false;
    }
    setMessage(QStringLiteral("Exported to %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool HistoryManager::exportSummaryCsv(const QUrl &file)
{
    QString err;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!csv::writeFile(path, csv::ridesToCsv(m_rides), &err)) {
        setMessage(QStringLiteral("Export failed: %1").arg(err));
        return false;
    }
    setMessage(QStringLiteral("Exported %1 rides to %2").arg(m_rides.size()).arg(QDir::toNativeSeparators(path)));
    return true;
}

} // namespace watt
