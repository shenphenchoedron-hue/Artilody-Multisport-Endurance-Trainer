#include "ProfileManager.h"

#include "Database.h"

#include <QFileInfo>
#include <cmath>

namespace watt {

ProfileManager::ProfileManager(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

void ProfileManager::load()
{
    const qint64 wanted = m_db->setting(QStringLiteral("currentProfile"), -1).toLongLong();
    auto p = m_db->profile(wanted);
    if (!p) {
        const auto all = m_db->profiles();
        if (!all.isEmpty())
            p = all.first();
    }
    if (p)
        m_profile = *p;
    emit profileChanged();
    emit profilesChanged();
}

void ProfileManager::refreshAnt()
{
    if (m_profile.id < 0)
        return;
    const double ant = m_db->currentAntKmh(m_profile.id);
    if (qFuzzyCompare(ant, m_profile.antSpeedKmh))
        return;
    m_profile.antSpeedKmh = ant;
    emit profileChanged();
}

QString ProfileManager::initials() const
{
    QString out;
    for (const QString &part : m_profile.name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out += part.at(0).toUpper();
        if (out.size() == 2)
            break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

QUrl ProfileManager::avatarUrl() const
{
    if (m_profile.avatarPath.isEmpty() || !QFileInfo::exists(m_profile.avatarPath))
        return QUrl();
    return QUrl::fromLocalFile(m_profile.avatarPath);
}

QVariantList ProfileManager::powerZones() const
{
    QVariantList list;
    const QStringList names = powerZoneNames();
    const QStringList rpe = rpeZoneLabels();
    for (int z = 0; z < kZoneCount; ++z) {
        const double lo = m_profile.zones.lowerBound(z);
        const double hi = m_profile.zones.upperBound(z);
        QVariantMap m;
        m.insert(QStringLiteral("zone"), z);
        m.insert(QStringLiteral("name"), names.at(z));
        m.insert(QStringLiteral("rpe"), rpe.at(z));
        m.insert(QStringLiteral("lowPercent"), int(std::lround(lo * 100)));
        m.insert(QStringLiteral("highPercent"), hi > 0 ? int(std::lround(hi * 100)) : 0);
        m.insert(QStringLiteral("lowWatts"), int(std::lround(lo * m_profile.ftp)));
        m.insert(QStringLiteral("highWatts"), hi > 0 ? int(std::lround(hi * m_profile.ftp)) - 1 : 0);
        list.append(m);
    }
    return list;
}

QVariantList ProfileManager::heartRateZonesFor(int maxHr, int restHr) const
{
    QVariantList list;
    const QStringList names = heartRateZoneNames();
    for (int z = 1; z <= 5; ++z) {
        QVariantMap m;
        m.insert(QStringLiteral("zone"), z);
        m.insert(QStringLiteral("name"), names.at(z - 1));
        m.insert(QStringLiteral("low"), heartRateZoneLow(z, maxHr, restHr));
        m.insert(QStringLiteral("high"), heartRateZoneHigh(z, maxHr, restHr));
        list.append(m);
    }
    return list;
}

QVariantList ProfileManager::zoneBounds() const
{
    QVariantList list;
    for (double u : m_profile.zones.upper)
        list.append(u);
    return list;
}

QVariantList ProfileManager::defaultZoneBounds() const
{
    QVariantList list;
    for (double u : ZoneSet().upper)
        list.append(int(std::lround(u * 100)));
    return list;
}

QVariantList ProfileManager::profiles() const
{
    QVariantList list;
    for (const Profile &p : m_db->profiles()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.name);
        m.insert(QStringLiteral("ftp"), p.ftp);
        m.insert(QStringLiteral("rowingFtp"), p.rowingFtp);
        list.append(m);
    }
    return list;
}

QVariantMap ProfileManager::ftpComparison(int estimatedFtp, int previousFtp) const
{
    const int diff = estimatedFtp - previousFtp;
    const double pct = previousFtp > 0 ? diff * 100.0 / previousFtp : 0.0;
    const QChar minus(0x2212);
    auto signedText = [&](const QString &magnitude, bool zero) {
        if (zero)
            return QStringLiteral("±") + magnitude;
        return (diff > 0 ? QStringLiteral("+") : QString(minus)) + magnitude;
    };
    const QString pctMagnitude = QString::number(std::abs(pct), 'f', 1) + QLatin1Char('%');
    const double wkg = m_profile.weightKg > 0 ? estimatedFtp / m_profile.weightKg : 0.0;

    QVariantMap m;
    m.insert(QStringLiteral("estimatedFtp"), estimatedFtp);
    m.insert(QStringLiteral("previousFtp"), previousFtp);
    m.insert(QStringLiteral("diff"), diff);
    m.insert(QStringLiteral("diffText"), signedText(QStringLiteral("%1 W").arg(std::abs(diff)), diff == 0));
    m.insert(QStringLiteral("percentText"), signedText(pctMagnitude, std::abs(pct) < 0.05));
    m.insert(QStringLiteral("wattsPerKg"), wkg);
    m.insert(QStringLiteral("wattsPerKgText"), QStringLiteral("%1 W/kg").arg(wkg, 0, 'f', 2));
    return m;
}

void ProfileManager::setError(const QString &e)
{
    if (m_lastError != e) {
        m_lastError = e;
        emit lastErrorChanged();
    }
}

void ProfileManager::applyValues(Profile &p, const QVariantMap &v)
{
    p.name = v.value(QStringLiteral("name"), p.name).toString().trimmed();
    p.weightKg = v.value(QStringLiteral("weight"), p.weightKg).toDouble();
    p.heightCm = v.value(QStringLiteral("heightCm"), p.heightCm).toDouble();
    p.ftp = v.value(QStringLiteral("ftp"), p.ftp).toInt();
    p.rowingFtp = v.value(QStringLiteral("rowingFtp"), p.rowingFtp).toInt();
    p.maxHr = v.value(QStringLiteral("maxHr"), p.maxHr).toInt();
    p.restHr = v.value(QStringLiteral("restHr"), p.restHr).toInt();
    p.preferredCadence = v.value(QStringLiteral("preferredCadence"), p.preferredCadence).toInt();
    p.defaultErg = v.value(QStringLiteral("defaultErg"), p.defaultErg).toBool();
    if (v.contains(QStringLiteral("avatarUrl"))) {
        const QUrl url = v.value(QStringLiteral("avatarUrl")).toUrl();
        p.avatarPath = url.isLocalFile() ? url.toLocalFile() : QString();
    }
    if (v.contains(QStringLiteral("zoneBounds"))) {
        const QVariantList bounds = v.value(QStringLiteral("zoneBounds")).toList();
        if (bounds.size() == kZoneCount - 1) {
            for (int i = 0; i < kZoneCount - 1; ++i)
                p.zones.upper[size_t(i)] = bounds.at(i).toDouble();
        }
    }
}

bool ProfileManager::saveProfile(const QVariantMap &v)
{
    Profile p = m_profile;
    applyValues(p, v);
    QString error;
    if (!p.isValid(&error)) {
        setError(error);
        return false;
    }
    const qint64 id = m_db->saveProfile(p);
    if (id < 0) {
        setError(m_db->lastError());
        return false;
    }
    p.id = id;
    m_profile = p;
    setError(QString());
    emit profileChanged();
    emit profilesChanged();
    return true;
}

bool ProfileManager::setRowingFtp(int watts)
{
    if (watts == m_profile.rowingFtp)
        return true;
    return saveProfile({ { QStringLiteral("rowingFtp"), watts } });
}

bool ProfileManager::selectProfile(qint64 id)
{
    auto p = m_db->profile(id);
    if (!p)
        return false;
    m_profile = *p;
    m_db->setSetting(QStringLiteral("currentProfile"), id);
    emit profileChanged();
    return true;
}

qint64 ProfileManager::createProfile(const QString &name)
{
    Profile p;
    p.name = name.trimmed().isEmpty() ? QStringLiteral("New Rider") : name.trimmed();
    const qint64 id = m_db->saveProfile(p);
    if (id < 0) {
        setError(m_db->lastError());
        return -1;
    }
    emit profilesChanged();
    selectProfile(id);
    return id;
}

qint64 ProfileManager::createProfileWith(const QVariantMap &values)
{
    Profile p;
    applyValues(p, values);
    QString error;
    if (!p.isValid(&error)) {
        setError(error);
        return -1;
    }
    const qint64 id = m_db->saveProfile(p);
    if (id < 0) {
        setError(m_db->lastError());
        return -1;
    }
    setError(QString());
    emit profilesChanged();
    selectProfile(id);
    return id;
}

bool ProfileManager::deleteProfile(qint64 id)
{
    if (m_busy && m_busy()) {
        setError(QStringLiteral("Finish the current session before deleting a profile"));
        return false;
    }
    if (m_db->profiles().size() <= 1) {
        setError(QStringLiteral("At least one profile is required"));
        return false;
    }
    if (!m_db->deleteProfile(id))
        return false;
    emit profilesChanged();
    if (id == m_profile.id)
        selectProfile(m_db->profiles().first().id);
    return true;
}

int ProfileManager::zoneForWatts(int watts) const
{
    return m_profile.zones.zoneForWatts(watts, m_profile.ftp);
}

int ProfileManager::heartRateZoneFor(int bpm) const
{
    return heartRateZone(bpm, m_profile.maxHr, m_profile.restHr);
}

QString ProfileManager::heartRateZoneRange(int zone) const
{
    if (zone < 1)
        return QStringLiteral("< %1").arg(heartRateZoneLow(1, m_profile.maxHr, m_profile.restHr));
    return QStringLiteral("%1 – %2")
        .arg(heartRateZoneLow(zone, m_profile.maxHr, m_profile.restHr))
        .arg(heartRateZoneHigh(zone, m_profile.maxHr, m_profile.restHr));
}

} // namespace watt
