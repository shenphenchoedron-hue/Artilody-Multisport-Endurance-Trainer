#pragma once

#include "RouteSegments.h"

#include <QVector>

#include <QString>

namespace watt {

class RlvRoute;

// What the 3D route view (Route3DItem) needs from a session that moves along
// an RlvRoute: the GPX Ride (RlvSession, Kind::Gpx) and the Treadmill Run
// (RunSession). The session object must also be a QObject with the signals
// routeChanged(), positionChanged(), metricsChanged() and stateChanged().
class RouteProgressSource
{
public:
    virtual ~RouteProgressSource() = default;
    virtual bool hasRoute() const = 0;
    virtual const RlvRoute &route() const = 0;
    virtual QString routePath() const = 0;
    virtual double distanceMeters() const = 0;   // position on the route (m)
    virtual double startDistanceM() const = 0;   // START arch (m)
    virtual double speedKmh() const = 0;
    virtual bool running() const = 0;
    virtual int cadence() const = 0;             // rpm (rider) / steps per minute (runner)
    // Draw a runner instead of the cyclist.
    virtual bool runnerFigure() const { return false; }
    // Runner: the athlete's walk -> run speed (km/h), see RunLibrary::walkRunKmh.
    virtual double walkRunKmh() const { return 6.25; }
    // The route's sprints and KOMs (GPX Ride, from the GPX Workshop; the 3D
    // view draws their lines, signs and arches): none by default.
    virtual QVector<RouteSegment> routeSegments() const { return {}; }
    // The route's gravel sections (GPX Ride, from the GPX Workshop; the 3D
    // view draws the road there as gravel): none by default.
    virtual QVector<RouteSegment> routeGravel() const { return {}; }
    // GPX Ride loop (RlvSession::loop): the 3D view draws the road as one
    // closed lap - a round trip closed at the start, any other route there
    // and back - and returning() says the rider is on the way back (the
    // position then counts back from the route's end).
    virtual bool loopRide() const { return false; }
    virtual bool returning() const { return false; }
    // GPX Ride loop: laps finished so far (the lap board at the finish line)
    virtual int lapsDone() const { return 0; }
};

} // namespace watt
