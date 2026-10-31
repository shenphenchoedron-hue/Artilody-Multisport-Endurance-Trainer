#pragma once

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
};

} // namespace watt
