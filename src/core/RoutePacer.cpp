#include "RoutePacer.h"

#include "Database.h"

#include <algorithm>
#include <cmath>

namespace watt {

RoutePacer::RoutePacer(Database *db, const QString &sport, QObject *parent)
    : RouteRival(parent)
    , m_db(db)
    , m_sport(sport)
{
    if (m_db) {
        m_enabled = m_db->setting(m_sport + QStringLiteral("/pacer"), false).toBool();
        m_wkg = std::clamp(m_db->setting(m_sport + QStringLiteral("/pacerWkg"), kDefaultWkg).toDouble(), kMinWkg, kMaxWkg);
    }
}

void RoutePacer::setEnabled(bool on)
{
    if (on == m_enabled || m_riding)
        return;
    m_enabled = on;
    if (m_db)
        m_db->setSetting(m_sport + QStringLiteral("/pacer"), on);
    emit settingsChanged();
    simulate();
}

void RoutePacer::setWkg(double wkg)
{
    if (!std::isfinite(wkg))
        return;
    wkg = std::round(std::clamp(wkg, kMinWkg, kMaxWkg) / kStepWkg) * kStepWkg;
    if (std::abs(wkg - m_wkg) < 1e-9)
        return;
    m_wkg = wkg;
    if (m_db)
        m_db->setSetting(m_sport + QStringLiteral("/pacerWkg"), wkg);
    emit settingsChanged();
    if (m_riding)
        resimulateFromNow();
    else
        simulate();
}

void RoutePacer::resimulateFromNow()
{
    const QVector<TracePoint> &old = m_trace.trace;
    if (!m_enabled || !hasRoute() || !m_generator || old.size() < 2 || ghostDone()) {
        emit ghostChanged();   // (the label: the new W/kg)
        return;
    }
    // the last whole second at or before now (the trace has one point per
    // second from 0; the finish point of a point-to-point route is not one)
    const int k = std::clamp(int(std::floor(m_t)), 0, int(old.size()) - 1);
    if (old.at(k).t != double(k)) {
        simulate();   // (not one of its own traces: from the start)
        return;
    }
    const double speed = k > 0 ? old.at(k).d - old.at(k - 1).d : 0.0;   // (m in that second)
    const QVector<TracePoint> rest = m_generator(m_wkg, k, old.at(k).d, speed);
    if (rest.isEmpty())
        return;
    RouteTrace t = m_trace;
    t.trace = old.mid(0, k);
    t.trace += rest;   // (rest starts with [k, d(k)])
    t.durationSec = t.trace.last().t;
    t.distanceM = t.trace.last().d;
    load(t);
}

QString RoutePacer::ghostLabel() const
{
    return available() ? QStringLiteral("Pacer · %1 W/kg").arg(m_wkg, 0, 'f', 1) : QString();
}

void RoutePacer::setRoute(bool route, bool loop)
{
    if (route == hasRoute() && loop == loops())
        return;
    RouteRival::setRoute(route, loop);
    if (!m_riding)
        simulate();
}

void RoutePacer::refresh()
{
    if (!m_riding)
        simulate();
}

void RoutePacer::begin()
{
    m_riding = false;
    clearTimes();
    simulate();
    m_riding = true;
}

void RoutePacer::reset()
{
    m_riding = false;
    clearTimes();
    simulate();
}

void RoutePacer::endBatch()
{
    if (--m_batch > 0 || !m_pending)
        return;
    m_pending = false;
    simulate();
}

void RoutePacer::simulate()
{
    if (m_batch > 0) {
        m_pending = true;
        return;
    }
    RouteTrace t;
    if (m_enabled && hasRoute() && m_generator) {
        t.id = -2;
        t.trace = m_generator(m_wkg, 0, 0.0, 0.0);
        if (t.trace.size() > 1) {
            t.durationSec = t.trace.last().t;
            t.distanceM = t.trace.last().d;
            t.completed = !loops();
        } else {
            t.trace.clear();
        }
    }
    load(t);
}

} // namespace watt
