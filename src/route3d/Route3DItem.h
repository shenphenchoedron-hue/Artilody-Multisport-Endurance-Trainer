#pragma once

#include "route.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickFramebufferObject>
#include <QTimer>
#include <QVector>

#include <memory>

namespace watt {

class RlvRoute;
class RlvSession;

// The 3D view of the GPX Ride page (QML type "Route3DView", Artilody.Backend):
// the Route3D prototype's world drawn along the session's route.
//
//   Route3DView { session: Gpx }
//
// The session (RlvSession, Kind::Gpx) is the only source of truth: its route,
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
    double frameDistance() const { return m_frameDistance; }
    double frameSpeedKmh() const { return m_speedKmh; }
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
    };
    static Built build(const RlvRoute &route);
    // App distance (m) -> distance on the drawn road (m), linear between points.
    static double mapDistance(const Built &b, double appDistanceM);

signals:
    void sessionChanged();
    void sceneChanged();
    void supportedChanged();
    void errorChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void rebuildRoute();
    void onPosition();
    void onFrame();
    void updateTimer();
    void checkSupported();

    QPointer<QObject> m_sessionObject;   // the RlvSession (QObject: forward declared)
    RlvSession *m_session = nullptr;     // valid while m_sessionObject is
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
    bool m_supported = true;

    // position: the session's last value + extrapolation
    double m_baseDistance = 0;     // drawn metres at the last session tick
    double m_speedKmh = 0;
    double m_frameDistance = 0;    // what the next frame shows
    bool m_running = false;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace watt
