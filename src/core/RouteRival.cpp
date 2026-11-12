#include "RouteRival.h"

#include "RowingRecords.h"

#include <algorithm>
#include <cmath>

namespace watt {

RouteRival::RouteRival(QObject *parent)
    : QObject(parent)
{
}

// --- trace helpers -------------------------------------------------------------------

double RouteRival::distanceAt(const QVector<TracePoint> &trace, double t)
{
    return RowingRecords::ghostDistanceAt(trace, t);   // (the same traces as the rowing ghost)
}

double RouteRival::timeAt(const QVector<TracePoint> &trace, double d)
{
    if (!std::isfinite(d) || trace.isEmpty())
        return -1.0;
    if (d <= 0)
        return 0.0;
    // (the distance never falls: the first point at or beyond d)
    auto it = std::lower_bound(trace.cbegin(), trace.cend(), d, [](const TracePoint &p, double value) { return p.d < value; });
    if (it == trace.cend())
        return -1.0;
    const TracePoint b = *it;
    const TracePoint a = it == trace.cbegin() ? TracePoint { 0.0, 0.0 } : *(it - 1);
    const double span = b.d - a.d;
    return span > 0 ? a.t + (b.t - a.t) * (d - a.d) / span : b.t;
}

RouteRival::RoutePos RouteRival::gpxPosition(double covered, double start, double total, bool loop, bool roundTrip)
{
    RoutePos p;
    if (!(total > 0) || !std::isfinite(covered))
        return p;
    covered = std::max(0.0, covered);
    start = std::clamp(start, 0.0, total);
    if (!loop) {
        p.distance = std::min(total, start + covered);
        return p;
    }
    const double first = total - start;   // lap 1: from the start point to the end
    if (covered < first) {
        p.distance = start + covered;
        return p;
    }
    const double rest = covered - first;
    if (roundTrip) {
        p.distance = std::fmod(rest, total);
        return p;
    }
    // there and back: back to 0 (returning), then out to the end again
    const double u = std::fmod(rest, 2 * total);
    if (u < total) {
        p.distance = total - u;
        p.returning = true;
    } else {
        p.distance = u - total;
    }
    return p;
}

RouteRival::RoutePos RouteRival::runPosition(double covered, double total, bool loop)
{
    RoutePos p;
    if (!(total > 0) || !std::isfinite(covered))
        return p;
    covered = std::max(0.0, covered);
    if (covered < total)
        p.distance = covered;
    else if (loop || covered > total + 0.5)   // (past the end of a point-to-point route: it started over)
        p.distance = std::fmod(covered, total);
    else
        p.distance = total;
    return p;
}

// --- the ride ----------------------------------------------------------------------------

void RouteRival::load(const RouteTrace &trace)
{
    m_trace = trace;
    update();
    emit ghostChanged();
    emit changed();   // (the label / availability: always)
}

void RouteRival::advance(double tSec, double coveredM)
{
    if (!std::isfinite(tSec) || !std::isfinite(coveredM))
        return;
    const double youBefore = m_youM;
    m_t = std::max(0.0, tSec);
    m_youM = std::max(0.0, coveredM);
    advanced();
    update(available() && m_youM != youBefore);
}

void RouteRival::update(bool youMoved)
{
    const double ghostM = m_ghostM, gapSec = m_gapSec;
    const bool hasGapSec = m_hasGapSec, shownBefore = m_shown, doneBefore = m_done;
    m_ghostM = available() ? distanceAt(m_trace.trace, m_t) : 0.0;
    m_hasGapSec = false;
    m_gapSec = 0;
    if (available() && m_t > 0) {
        const double when = timeAt(m_trace.trace, m_youM);   // it was where you are now
        if (when >= 0) {
            m_gapSec = when - m_t;
            m_hasGapSec = true;
        }
    }
    m_shown = shown();
    m_done = ghostDone();
    // (on every tick of a ride: only when something the page shows changed -
    // the gap follows your metres; without a rival nothing does)
    if (m_ghostM != ghostM || m_gapSec != gapSec || m_hasGapSec != hasGapSec || m_shown != shownBefore
        || m_done != doneBefore || youMoved)
        emit changed();
}

bool RouteRival::shown() const
{
    if (!available() || !m_hasRoute)
        return false;
    if (m_t <= m_trace.durationSec + kLingerSec)
        return true;
    return m_trace.completed && !m_loop;   // waits at the end of the route
}

double RouteRival::speedKmh() const
{
    if (!available() || m_t >= m_trace.durationSec)
        return 0.0;
    const double a = std::max(0.0, m_t - 0.5), b = std::min(m_trace.durationSec, m_t + 0.5);
    return b > a ? (distanceAt(m_trace.trace, b) - distanceAt(m_trace.trace, a)) / (b - a) * 3.6 : 0.0;
}

RouteRival::RoutePos RouteRival::routePos() const
{
    return m_positioner ? m_positioner(m_ghostM) : RoutePos { m_ghostM, false };
}

} // namespace watt
