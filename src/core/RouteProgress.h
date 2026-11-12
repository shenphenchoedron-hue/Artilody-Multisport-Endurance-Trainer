#pragma once

#include "RouteSegments.h"

#include <QVector>

#include <QString>

namespace watt {

class RlvRoute;
class LiveRide;

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
    // The route's place signs (GPX Ride, from the GPX Workshop): none by default.
    virtual QVector<RouteSegment> routePlaces() const { return {}; }
    // GPX Ride loop (RlvSession::loop): the 3D view draws the road as one
    // closed lap - a round trip closed at the start, any other route there
    // and back - and returning() says the rider is on the way back (the
    // position then counts back from the route's end).
    virtual bool loopRide() const { return false; }
    virtual bool returning() const { return false; }
    // GPX Ride loop: laps finished so far (the lap board at the finish line)
    virtual int lapsDone() const { return 0; }
    // The ghost (RouteGhost: an earlier ride on this route, yours or another
    // rider's): drawn
    // beside you in its own lane, see-through. Its place on the route (m,
    // returning: on the way back of a looped there-and-back road) and speed.
    // The session also has a ghostChanged() signal when it has a ghost.
    virtual bool ghostVisible() const { return false; }
    virtual double ghostDistanceMeters() const { return 0.0; }
    virtual bool ghostReturning() const { return false; }
    virtual double ghostSpeedKmh() const { return 0.0; }
    // The pacer (RoutePacer: a rider at a set W/kg, GPX Ride): the same, in
    // its own lane, tinted orange; the session's pacerChanged() signal.
    virtual bool pacerVisible() const { return false; }
    virtual double pacerDistanceMeters() const { return 0.0; }
    virtual bool pacerReturning() const { return false; }
    virtual double pacerSpeedKmh() const { return 0.0; }
    // Riding together (LiveRide, with a server): the other stations' riders
    // on this route, each in its own lane and colour; its changed() signal.
    virtual const LiveRide *liveRiders() const { return nullptr; }
};

} // namespace watt
