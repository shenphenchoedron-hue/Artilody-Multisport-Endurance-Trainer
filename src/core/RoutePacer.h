#pragma once

#include "RouteRival.h"

namespace watt {

class Database;

// The pacer of the GPX Ride: a rider holding a set number of watts per
// kilogram of your weight all the way - slower up the climbs, faster down.
// Its own switch, separate from the ghost (RouteGhost); one of them at a time
// (RlvSession). Exposed to QML as the session's "pacer". On/off and the W/kg
// are kept in the settings ("<sport>/pacer", "<sport>/pacerWkg").
//
// The session simulates it over the route with the ride's own physics
// (setGenerator()): whenever it is switched on, its W/kg change, the route /
// start / loop change (refresh()) and at the start (begin()) - so the rider's
// weight, the difficulty and the loop as they are then apply. Its W/kg may
// change during the ride too: its trace is kept up to now and the rest
// simulated again from its place and speed then (no jump: it speeds up /
// slows down from there). On / off is set before the start (and after the
// finish).
class RoutePacer : public RouteRival
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(double wkg READ wkg WRITE setWkg NOTIFY settingsChanged)
    Q_PROPERTY(double minWkg READ minWkg CONSTANT)
    Q_PROPERTY(double maxWkg READ maxWkg CONSTANT)
    Q_PROPERTY(double stepWkg READ stepWkg CONSTANT)

public:
    // its trace [t, metres covered] for this many W/kg, one point per whole
    // second from fromSec on (its first point: [fromSec, fromCovered], moving
    // at fromSpeed m/s); empty: no route
    using Generator = std::function<QVector<TracePoint>(double wkg, int fromSec, double fromCovered, double fromSpeed)>;

    static constexpr double kMinWkg = 0.5;
    static constexpr double kMaxWkg = 8.0;
    static constexpr double kStepWkg = 0.1;
    static constexpr double kDefaultWkg = 2.5;

    // Simulations asked for while one of these lives (the session's start /
    // reset: several changes at once) run once, when the last one ends.
    class Batch
    {
    public:
        explicit Batch(RoutePacer *p) : m_p(p) { if (m_p) ++m_p->m_batch; }
        ~Batch() { if (m_p) m_p->endBatch(); }
        Batch(const Batch &) = delete;
        Batch &operator=(const Batch &) = delete;
    private:
        RoutePacer *m_p;
    };

    RoutePacer(Database *db, const QString &sport, QObject *parent = nullptr);

    void setGenerator(Generator g) { m_generator = std::move(g); }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);   // not while riding
    double wkg() const { return m_wkg; }
    void setWkg(double wkg);   // clamped to kMinWkg..kMaxWkg, kStepWkg steps
    static double minWkg() { return kMinWkg; }
    static double maxWkg() { return kMaxWkg; }
    static double stepWkg() { return kStepWkg; }

    QString ghostLabel() const override;

    // The route it rides (none: hidden) and whether it loops; simulated again.
    void setRoute(bool hasRoute, bool loop);
    // Simulated again (start point / difficulty / weight changed) - not while riding.
    void refresh();
    // The start: simulated again and from 0; riding (on / off locked; a W/kg
    // change re-simulates from now on) until end() / reset().
    void begin();
    // The finish: no longer riding (its ride stays as it was: the result).
    void end() { m_riding = false; }
    void reset();
    bool riding() const { return m_riding; }

signals:
    void settingsChanged();

private:
    void simulate();
    void resimulateFromNow();   // riding: the trace up to now kept, the rest at m_wkg
    void endBatch();

    Database *m_db;
    QString m_sport;
    Generator m_generator;
    bool m_enabled = false;
    double m_wkg = kDefaultWkg;
    bool m_riding = false;
    int m_batch = 0;
    bool m_pending = false;     // a simulation asked for in a batch
};

} // namespace watt
