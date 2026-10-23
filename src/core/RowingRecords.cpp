#include "RowingRecords.h"

#include "Database.h"
#include "ProfileManager.h"

#include <algorithm>
#include <cmath>

namespace watt {

RowingRecords::RowingRecords(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        m_traceCache.clear();
        ++m_revision;
        emit recordsChanged();
    });
}

double RowingRecords::ghostDistanceAt(const QVector<TracePoint> &trace, double t)
{
    if (trace.isEmpty() || !std::isfinite(t) || t <= 0)
        return 0.0;
    auto it = std::upper_bound(trace.cbegin(), trace.cend(), t,
                               [](double value, const TracePoint &p) { return value < p.t; });
    if (it == trace.cend())
        return trace.last().d;
    const TracePoint b = *it;
    const TracePoint a = it == trace.cbegin() ? TracePoint { 0.0, 0.0 } : *(it - 1);
    const double span = b.t - a.t;
    if (span <= 0)
        return b.d;
    return a.d + (b.d - a.d) * (t - a.t) / span;
}

double RowingRecords::pace500(int distanceM, double timeSec)
{
    return distanceM > 0 ? timeSec * 500.0 / distanceM : 0.0;
}

QVector<TracePoint> RowingRecords::traceFromVariant(const QVariantList &list)
{
    QVector<TracePoint> out;
    out.reserve(list.size());
    for (const QVariant &v : list) {
        TracePoint p;
        if (v.typeId() == QMetaType::QVariantMap) {
            const QVariantMap m = v.toMap();
            p = { m.value(QStringLiteral("t")).toDouble(), m.value(QStringLiteral("d")).toDouble() };
        } else {
            const QVariantList pair = v.toList();
            if (pair.size() < 2)
                continue;
            p = { pair.at(0).toDouble(), pair.at(1).toDouble() };
        }
        if (std::isfinite(p.t) && std::isfinite(p.d))
            out.append(p);
    }
    std::stable_sort(out.begin(), out.end(), [](const TracePoint &a, const TracePoint &b) { return a.t < b.t; });
    return out;
}

QVariantList RowingRecords::traceToVariant(const QVector<TracePoint> &trace)
{
    QVariantList list;
    list.reserve(trace.size());
    for (const TracePoint &p : trace)
        list.append(QVariant(QVariantList { p.t, p.d }));
    return list;
}

std::optional<RowingRecord> RowingRecords::best(int distanceM, qint64 profileId) const
{
    if (distanceM <= 0)
        return std::nullopt;
    return m_db->bestRowingRecord(profileId >= 0 ? profileId : m_profiles->profileId(), distanceM);
}

QVariantMap RowingRecords::bestFor(int distanceM) const
{
    const auto r = best(distanceM);
    if (!r)
        return {};
    QVariantMap m;
    m.insert(QStringLiteral("recordId"), r->id);
    m.insert(QStringLiteral("distanceM"), r->distanceM);
    m.insert(QStringLiteral("timeSec"), r->timeSec);
    m.insert(QStringLiteral("pace500Sec"), pace500(r->distanceM, r->timeSec));
    m.insert(QStringLiteral("rideId"), r->rideId);
    m.insert(QStringLiteral("createdAt"), r->createdAt);
    m.insert(QStringLiteral("trace"), traceToVariant(r->trace));
    return m;
}

bool RowingRecords::saveIfBest(int distanceM, double timeSec, const QVariantList &trace, qint64 rideId)
{
    return saveIfBest(distanceM, timeSec, traceFromVariant(trace), rideId);
}

bool RowingRecords::saveIfBest(int distanceM, double timeSec, const QVector<TracePoint> &trace, qint64 rideId,
                               qint64 profileId)
{
    const qint64 profile = profileId >= 0 ? profileId : m_profiles->profileId();
    if (distanceM <= 0 || !std::isfinite(timeSec) || timeSec <= 0 || profile < 0)
        return false;
    if (!plausibleTime(distanceM, timeSec))
        return false;
    const auto previous = best(distanceM, profile);
    if (previous && previous->timeSec <= timeSec)
        return false;
    RowingRecord r;
    r.profileId = profile;
    r.distanceM = distanceM;
    r.timeSec = timeSec;
    // ride_id is a foreign key: a record must never be lost for an unknown/unsaved ride
    r.rideId = rideId > 0 && m_db->ride(rideId) ? rideId : -1;
    r.createdAt = QDateTime::currentDateTime();
    r.trace = trace;
    if (m_db->saveRowingRecord(r) < 0)
        return false;
    if (profile != m_profiles->profileId())
        return true;   // another rider's record: the active profile's view is unchanged
    m_traceCache.remove(distanceM);
    ++m_revision;
    emit newRecord(distanceM, timeSec, previous ? previous->timeSec : 0.0);
    emit recordsChanged();
    return true;
}

QVariantList RowingRecords::allBests() const
{
    const QVector<RowingRecord> records = m_db->bestRowingRecords(m_profiles->profileId());
    QList<int> distances = standardDistances();
    for (const RowingRecord &r : records) {
        if (!distances.contains(r.distanceM))
            distances.append(r.distanceM);
    }
    std::sort(distances.begin(), distances.end());
    QVariantList list;
    for (int d : distances) {
        QVariantMap m;
        m.insert(QStringLiteral("distanceM"), d);
        m.insert(QStringLiteral("standard"), standardDistances().contains(d));
        auto it = std::find_if(records.cbegin(), records.cend(), [d](const RowingRecord &r) { return r.distanceM == d; });
        const bool has = it != records.cend();
        m.insert(QStringLiteral("hasRecord"), has);
        m.insert(QStringLiteral("timeSec"), has ? it->timeSec : 0.0);
        m.insert(QStringLiteral("pace500Sec"), has ? pace500(d, it->timeSec) : 0.0);
        m.insert(QStringLiteral("rideId"), has ? it->rideId : qint64(-1));
        m.insert(QStringLiteral("createdAt"), has ? QVariant(it->createdAt) : QVariant());
        list.append(m);
    }
    return list;
}

double RowingRecords::ghostDistance(int distanceM, double t) const
{
    auto it = m_traceCache.find(distanceM);
    if (it == m_traceCache.end()) {
        const auto r = best(distanceM);
        it = m_traceCache.insert(distanceM, r ? r->trace : QVector<TracePoint>());
    }
    return ghostDistanceAt(it.value(), t);
}

} // namespace watt
