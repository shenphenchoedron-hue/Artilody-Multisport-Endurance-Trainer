#pragma once

#include "route.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickFramebufferObject>
#include <QTimer>
#include "RouteSegments.h"

#include <QVector>
#include <array>
#include <vector>
#include <QVector>

#include <memory>

namespace watt {

class RlvRoute;
class RouteProgressSource;

// The 3D view of the GPX Ride page (QML type "Route3DView", Artilody.Backend):
// the Route3D prototype's world drawn along the session's route.
//
//   Route3DView { session: Gpx }
//
// The session (RlvSession, Kind::Gpx, or the Treadmill Run's RunSession: any
// QObject implementing RouteProgressSource) is the only source of truth: its route,
// distance and speed come from the app's physics; the view only follows
// them. The track is the session's RlvRoute (smoothed elevation), projected
// and smoothed for drawing by route3d::Route::fromGeo(), which also reports
// where each track point lies on the drawn road; the app's distance is mapped
// point by point with that table (mapDistance()), so the profile, the grade
// and the 3D position agree. Between the session's ticks (250 ms)
// the position is extrapolated with the current speed, so the picture moves
// smoothly at up to kFrameMs.
//
// Needs Qt Quick on OpenGL (main.cpp selects it; "supported" is false on other
// graphics APIs and "error" says why the view cannot be shown).
class Route3DItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)
    // a route is loaded and drawable (the 3D road could be built from it)
    Q_PROPERTY(bool hasScene READ hasScene NOTIFY sceneChanged)
    // Qt Quick runs on OpenGL (else the view stays empty)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    // why the route / the view cannot be shown ("" = fine)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kFrameMs = 33;        // ~30 FPS while riding
    static constexpr int kIdleFrameMs = 100;   // clouds / trees keep moving when not riding
    static constexpr double kMaxExtrapolationSec = 0.6;
    // The rider's legs follow the real cadence (RlvSession::cadence, ~1 Hz):
    // the shown rpm approaches it with this time constant, so the legs do not
    // jerk at each new value. 0 rpm (coasting, no cadence) = legs still.
    static constexpr double kCadenceSmoothingSec = 0.6;

    explicit Route3DItem(QQuickItem *parent = nullptr);
    ~Route3DItem() override;

    Renderer *createRenderer() const override;

    QObject *session() const;
    void setSession(QObject *session);
    bool hasScene() const { return m_route != nullptr; }
    bool supported() const { return m_supported; }
    QString error() const { return m_routeError.isEmpty() ? m_rendererError : m_routeError; }

    // (render thread, in Renderer::synchronize(); the GUI thread is blocked)
    std::shared_ptr<const route3d::Route> drawnRoute() const { return m_route; }
    quint64 routeVersion() const { return m_routeVersion; }
    int marksVersion() const { return m_marksVersion; }   // sprints / KOMs / km posts changed
    double frameDistance() const { return m_frameDistance; }
    double frameSpeedKmh() const { return m_speedKmh; }
    double frameCrank() const { return m_crank; }
    double frameStartDistance() const { return m_startDrawn; }
    // the route's sprints on the drawn road: announce (< 0: none) / start /
    // finish (Route3DScene::setSprints())
    const std::vector<std::array<double, 3>> &frameSprints() const { return m_sprintDrawn; }
    // the route's KOMs on the drawn road: foot / top (Route3DScene::setKoms())
    const std::vector<std::array<double, 2>> &frameKoms() const { return m_komDrawn; }
    // the kilometre posts: drawn metres, km number (Route3DScene::setKmPosts())
    const std::vector<std::array<double, 2>> &frameKmPosts() const { return m_kmDrawn; }
    // GPX Ride loop: laps finished (the lap board at the finish line); -1 = no loop
    int frameLaps() const { return m_laps; }
    bool frameRunner() const { return m_runner; }
    // (read live: the profile's setting may change during a run)
    double frameWalkRunKmh() const;
    float frameTime() const { return float(m_clock.elapsed() / 1000.0); }
    void setRendererError(const QString &error);

    // Builds the drawn road from an app route; the table maps the app's
    // distance (RlvPoint::distance) onto the drawn road. Throws
    // std::runtime_error (with the reason) when the track cannot be drawn.
    struct Built
    {
        std::shared_ptr<const route3d::Route> route;
        QVector<double> appDistance;    // ascending
        QVector<double> drawnDistance;  // matching distance on the drawn road
        QVector<double> backDistance;   // looped there and back: the same on the way back (else empty)
    };
    // loop (GPX Ride loop): the drawn road is one closed lap - a round trip
    // closed back to its start, any other route out and back again.
    static Built build(const RlvRoute &route, bool loop = false);
    // App distance (m) -> distance on the drawn road (m), linear between
    // points; returning: on the way back of a looped there-and-back road.
    static double mapDistance(const Built &b, double appDistanceM, bool returning = false);
    // The kilometre posts (drawn metres, km number) for a rider at app
    // distance d: each whole km of the route; looping, the km count on lap
    // after lap (laps finished, returning: on a there-and-back's way back).
    static std::vector<std::array<double, 2>> kmPosts(const Built &b, double totalM, bool loop, int laps, double d,
                                                      bool returning);

signals:
    void sessionChanged();
    void sceneChanged();
    void supportedChanged();
    void errorChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private slots:
    void rebuildRoute();
    void onPosition();

private:
    void onFrame();
    void updateTimer();
    void checkSupported();

    QPointer<QObject> m_sessionObject;   // the session (RlvSession / RunSession)
    RouteProgressSource *m_session = nullptr;   // valid while m_sessionObject is
    bool m_runner = false;               // draw a runner (Treadmill Run) instead of the cyclist
    double m_walkRunKmh = 6.25;          // runner: the athlete's walk -> run speed
    Built m_built;
    std::shared_ptr<const route3d::Route> m_route;
    quint64 m_routeVersion = 0;
    QString m_routeError;
    QString m_rendererError;
    // what m_built was made from (rebuildRoute() skips an unchanged route)
    bool m_builtValid = false;
    QString m_builtPath;
    int m_builtPoints = 0;
    double m_builtLength = 0;
    double m_builtGrade = 0;
    bool m_builtLoop = false;
    QVector<RouteSegment> m_builtSegments;
    int m_marksVersion = 0;
    void buildMarks();
    bool m_supported = true;

    // position: the session's last value + extrapolation
    double m_baseDistance = 0;     // drawn metres at the last session tick
    double m_speedKmh = 0;
    double m_frameDistance = 0;    // what the next frame shows
    int m_laps = -1;               // loop ride: laps finished (-1: not looping)
    double m_lapOffset = 0;        // closed lap: drawn metres of the laps before (frames count on)
    double m_startDrawn = 0;       // where the ride starts, drawn metres (START arch)
    std::vector<std::array<double, 3>> m_sprintDrawn;   // per sprint: announce / start / finish, drawn metres
    std::vector<std::array<double, 2>> m_komDrawn;      // per KOM: foot / top, drawn metres
    std::vector<std::array<double, 2>> m_kmDrawn;
    std::vector<std::array<double, 2>> kmPosts() const;
    double m_crank = 0;            // pedal angle (radians), integrated from the cadence
    double m_shownRpm = 0;         // smoothed cadence driving m_crank
    QElapsedTimer m_frameClock;    // time between frames for the crank
    bool m_running = false;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace watt
