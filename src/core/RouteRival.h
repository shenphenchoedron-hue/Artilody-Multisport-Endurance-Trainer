#pragma once

#include "PlanTypes.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

namespace watt {

// Another rider on the route of the GPX Ride / Treadmill Run, raced at the
// same riding time: the ghost (RouteGhost, an earlier ride of yours) or the
// pacer (RoutePacer, a rider holding a set W/kg). Holds its trace [t, metres
// covered since the start] and the live values for the page and the 3D view.
//
// The session calls advance(t, covered) on every tick - the riding time (s,
// pauses excluded) and your metres covered since the start (all laps). The
// rival is at the trace's distance at that time: 0 before the start, frozen
// when paused. The session turns its metres into a place on the route
// (setPositioner(), see gpxPosition() / runPosition()).
class RouteRival : public QObject
{
    Q_OBJECT
    // a rival is loaded (something to race)
    Q_PROPERTY(bool available READ available NOTIFY ghostChanged)
    Q_PROPERTY(QString ghostLabel READ ghostLabel NOTIFY ghostChanged)    // "Best · 24:13 · 12 Nov"
    // live (changed() only when one of them changed)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)                 // drawn on the route now
    Q_PROPERTY(double coveredM READ coveredM NOTIFY changed)         // its metres since the start
    Q_PROPERTY(double gapM READ gapM NOTIFY changed)                 // you - it: + = you are ahead
    // seconds you are ahead (+) / behind (-): when it was where you are now;
    // 0 with hasGapSec false when unknown
    Q_PROPERTY(double gapSec READ gapSec NOTIFY changed)
    Q_PROPERTY(bool hasGapSec READ hasGapSec NOTIFY changed)
    Q_PROPERTY(bool ghostDone READ ghostDone NOTIFY changed)         // its ride is over
    Q_PROPERTY(double routeDistanceM READ routeDistanceM NOTIFY changed)   // its place on the route

public:
    // a place on the route: metres along it, and on the way back (a looped
    // there-and-back GPX Ride)
    struct RoutePos
    {
        double distance = 0;
        bool returning = false;
    };
    using Positioner = std::function<RoutePos(double coveredM)>;

    // A ride whose trace ended stays visible this long (then the rival
    // vanishes - except at the end of a completed point-to-point route: it
    // waits there)
    static constexpr double kLingerSec = 3.0;

    explicit RouteRival(QObject *parent = nullptr);

    void setPositioner(Positioner p) { m_positioner = std::move(p); }

    bool available() const { return !m_trace.trace.isEmpty(); }
    virtual QString ghostLabel() const = 0;
    const RouteTrace &trace() const { return m_trace; }

    void advance(double tSec, double coveredM);

    bool shown() const;
    double coveredM() const { return m_ghostM; }
    double gapM() const { return available() ? m_youM - m_ghostM : 0.0; }
    double gapSec() const { return m_gapSec; }
    bool hasGapSec() const { return m_hasGapSec; }
    bool ghostDone() const { return available() && m_t > m_trace.durationSec; }
    double speedKmh() const;           // its speed now (0 when not moving)
    RoutePos routePos() const;
    double routeDistanceM() const { return routePos().distance; }

    // Trace helpers (sorted by t; [0, 0] before the first point; the last
    // distance after the end).
    static double distanceAt(const QVector<TracePoint> &trace, double t);
    // When the trace first reached d (s); < 0 when it never did.
    static double timeAt(const QVector<TracePoint> &trace, double d);
    // GPX Ride: covered metres -> place on the route (RlvSession::advance):
    // not looping, from start to the end; looping, the first lap from start
    // to the end, then a round trip from 0 round again, any other route back
    // to 0 (returning) and out again.
    static RoutePos gpxPosition(double coveredM, double startM, double totalM, bool loop, bool roundTrip);
    // Treadmill Run (RunSession::process): from 0, lap after lap on a loop; a
    // point-to-point route ends at its end (unless the run went on past it: a
    // workout made it start over).
    static RoutePos runPosition(double coveredM, double totalM, bool loop);

signals:
    void ghostChanged();   // loaded / chosen (the label, available)
    void changed();

protected:
    // the route it is on ("" = none: never shown) and whether it is a loop
    void setRoute(bool hasRoute, bool loop) { m_hasRoute = hasRoute; m_loop = loop; }
    bool hasRoute() const { return m_hasRoute; }
    bool loops() const { return m_loop; }
    // a new trace (empty = none): the values again, ghostChanged() + changed()
    void load(const RouteTrace &trace);
    void clearTimes() { m_t = 0; m_youM = 0; }
    // the riding time / your metres were set: recorded by RouteGhost
    virtual void advanced() {}
    // m_t, m_youM -> live values; changed() when one changed (or you moved: the gap)
    void update(bool youMoved = false);

    RouteTrace m_trace;
    double m_t = 0;          // riding time now
    double m_youM = 0;       // your metres now

private:
    Positioner m_positioner;
    bool m_hasRoute = false;
    bool m_loop = false;
    double m_ghostM = 0;
    double m_gapSec = 0;
    bool m_hasGapSec = false;
    bool m_shown = false;    // shown() / ghostDone() at the last update (changed())
    bool m_done = false;
};

} // namespace watt
