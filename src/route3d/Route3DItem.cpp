#include "Route3DItem.h"

#include "Route3DScene.h"
#include "RlvRoute.h"
#include "RouteProgress.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QQuickWindow>
#include <QtQuick/qquickopenglutils.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace watt {

namespace {

// Track points further apart than this get points in between for drawing
// (route planners export sparse tracks; the ride follows the straight line
// between the points, so the drawn road does too).
constexpr double kMaxDrawGapM = 400.0;
// Checked on the raw track BEFORE points are added (route3d::Route::fromGeo
// has the same limits, but only after the gaps are filled): a broken or
// hostile track must not make the drawing allocate without bound.
constexpr double kMaxTrackM = 200000.0;
constexpr double kMaxGapM = 2000.0;
// After the FINISH arch the drawn road runs on straight for this far (not
// ridden: a point-to-point route would otherwise end in the grass).
constexpr double kRunOutM = 50.0;

class Route3DRenderer : public QQuickFramebufferObject::Renderer
{
public:
    ~Route3DRenderer() override
    {
        if (QOpenGLContext::currentContext())
            m_scene.release();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4);   // resolved by Qt (blit) before the item shows it
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        auto *view = static_cast<Route3DItem *>(item);
        if (!m_initTried) {
            m_initTried = true;
            m_scene.initialize(QOpenGLContext::currentContext());
            view->setRendererError(m_scene.ready() ? QString() : m_scene.error());
            if (!m_scene.ready())
                qWarning("Route3D: %s", qPrintable(m_scene.error()));
        }
        if (view->routeVersion() != m_version) {
            m_version = view->routeVersion();
            m_scene.setRoute(view->drawnRoute());
        }
        m_scene.setStartDistance(view->frameStartDistance());
        m_scene.setRunner(view->frameRunner());
        m_scene.setWalkRunKmh(view->frameWalkRunKmh());
        m_distance = view->frameDistance();
        m_speedKmh = view->frameSpeedKmh();
        m_crank = view->frameCrank();
        m_time = view->frameTime();
    }

    void render() override
    {
        if (m_scene.ready())
            m_scene.render(framebufferObject()->size(), m_distance, m_speedKmh, m_crank, m_time);
        // hand the context back to the scene graph in a known state
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    Route3DScene m_scene;
    bool m_initTried = false;
    quint64 m_version = 0;
    double m_distance = 0;
    double m_speedKmh = 0;
    double m_crank = 0;
    float m_time = 0;
};

} // namespace

Route3DItem::Route3DItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    // the FBO texture is bottom-up (OpenGL)
    setMirrorVertically(true);
    m_clock.start();
    m_sinceTick.start();
    m_frameClock.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Route3DItem::onFrame);
    checkSupported();
}

Route3DItem::~Route3DItem() = default;

QQuickFramebufferObject::Renderer *Route3DItem::createRenderer() const
{
    return new Route3DRenderer;
}

void Route3DItem::checkSupported()
{
    const bool gl = QQuickWindow::graphicsApi() == QSGRendererInterface::OpenGL;
    // (QQuickFramebufferObject draws with OpenGL only: no contents otherwise)
    setFlag(ItemHasContents, gl);
    if (gl == m_supported)
        return;
    m_supported = gl;
    emit supportedChanged();
    setRendererError(gl ? QString()
                        : QStringLiteral("The 3D view needs OpenGL, which is not available on this computer."));
}

QObject *Route3DItem::session() const
{
    return m_sessionObject.data();
}

void Route3DItem::setSession(QObject *object)
{
    auto *source = dynamic_cast<RouteProgressSource *>(object);
    QObject *s = source ? object : nullptr;
    if (s == session())
        return;
    if (m_sessionObject)
        disconnect(m_sessionObject, nullptr, this, nullptr);
    m_sessionObject = s;
    m_session = source;
    m_runner = source && source->runnerFigure();
    m_walkRunKmh = source ? source->walkRunKmh() : 6.25;
    m_builtValid = false;
    if (s) {
        connect(s, &QObject::destroyed, this, [this] { m_session = nullptr; });
        // (both session classes have these signals; string-based so the view
        // does not depend on the session libraries)
        connect(s, SIGNAL(routeChanged()), this, SLOT(rebuildRoute()));
        connect(s, SIGNAL(positionChanged()), this, SLOT(onPosition()));
        connect(s, SIGNAL(metricsChanged()), this, SLOT(onPosition()));
        connect(s, SIGNAL(stateChanged()), this, SLOT(onPosition()));
    }
    emit sessionChanged();
    rebuildRoute();
}

void Route3DItem::setRendererError(const QString &error)
{
    if (m_rendererError == error)
        return;
    const QString before = this->error();
    m_rendererError = error;
    if (this->error() != before)
        QMetaObject::invokeMethod(this, &Route3DItem::errorChanged, Qt::QueuedConnection);   // (may run on the render thread)
}

Route3DItem::Built Route3DItem::build(const RlvRoute &route)
{
    const QVector<RlvPoint> &pts = route.points();
    if (pts.size() < 2)
        throw std::runtime_error("The route needs at least two points.");
    // Limits first, on the raw track (no allocation depends on them yet).
    double track = 0;
    for (int i = 0; i < pts.size(); ++i) {
        const RlvPoint &p = pts.at(i);
        if (!std::isfinite(p.lat) || !std::isfinite(p.lon) || std::abs(p.lat) > 90 || std::abs(p.lon) > 180)
            throw std::runtime_error("The GPX file contains invalid coordinates.");
        if (i == 0)
            continue;
        const RlvPoint &a = pts.at(i - 1);
        const double gap = RlvRoute::haversine(a.lat, a.lon, p.lat, p.lon);
        if (!std::isfinite(gap) || gap > kMaxGapM)
            throw std::runtime_error("Two track points are more than 2 km apart – the 3D view needs a continuous track.");
        track += gap;
        if (track > kMaxTrackM)
            throw std::runtime_error("The 3D view supports routes up to 200 km.");
    }
    // The geo points the ride follows (smoothed elevation = the app's grade),
    // with points added on long straight gaps (at most 4 per gap: <= 2 km).
    std::vector<route3d::GeoPoint> geo;
    geo.reserve(size_t(pts.size()) + 16);
    QVector<int> geoIndexOf(pts.size());
    for (int i = 0; i < pts.size(); ++i) {
        const RlvPoint &p = pts.at(i);
        if (i > 0) {
            const RlvPoint &a = pts.at(i - 1);
            const double gap = RlvRoute::haversine(a.lat, a.lon, p.lat, p.lon);
            const int extra = std::max(0, int(std::ceil(gap / kMaxDrawGapM)) - 1);
            for (int k = 1; k <= extra; ++k) {
                const double t = double(k) / (extra + 1);
                geo.push_back({ a.lat + (p.lat - a.lat) * t, a.lon + (p.lon - a.lon) * t,
                                a.elevation + (p.elevation - a.elevation) * t });
            }
        }
        geoIndexOf[i] = int(geo.size());
        geo.push_back({ p.lat, p.lon, p.elevation });
    }
    // the run-out: the road goes on straight past the finish (same height),
    // unless the route is a loop (the finish is at the start, the road goes on)
    const size_t ridden = geo.size();
    {
        const route3d::GeoPoint end = geo.back();
        const route3d::GeoPoint &first = geo.front();
        const bool loop = RlvRoute::haversine(first.lat, first.lon, end.lat, end.lon) < 60.0;
        // the direction over the last ~20 m
        size_t k = geo.size() - 1;
        while (k > 0 && RlvRoute::haversine(geo[k].lat, geo[k].lon, end.lat, end.lon) < 20.0)
            --k;
        const double span = RlvRoute::haversine(geo[k].lat, geo[k].lon, end.lat, end.lon);
        if (!loop && span > 1.0) {
            const double dLat = (end.lat - geo[k].lat) / span, dLon = (end.lon - geo[k].lon) / span;   // per metre
            for (int m = 10; m <= int(kRunOutM); m += 10)
                geo.push_back({ end.lat + dLat * m, end.lon + dLon * m, end.elevation });
        }
    }
    Built b;
    // fromGeo reports where each input point lies on the drawn (smoothed)
    // road: app distance -> drawn distance, point by point.
    std::vector<double> drawnAt;
    auto drawn = std::make_shared<route3d::Route>(route3d::Route::fromGeo(geo, &drawnAt));
    if (geo.size() > ridden)
        drawn->finishDistance = drawnAt[ridden - 1];
    b.route = drawn;
    b.appDistance.reserve(pts.size());
    b.drawnDistance.reserve(pts.size());
    double last = 0;
    for (int i = 0; i < pts.size(); ++i) {
        // (never backwards: the app distance is monotonic, so is the drawn one)
        last = std::max(last, std::min(b.route->finish(), drawnAt[size_t(geoIndexOf[i])]));
        b.appDistance.append(pts.at(i).distance);
        b.drawnDistance.append(last);
    }
    return b;
}

double Route3DItem::frameWalkRunKmh() const
{
    return m_session && m_sessionObject ? m_session->walkRunKmh() : m_walkRunKmh;
}

double Route3DItem::mapDistance(const Built &b, double d)
{
    const int n = b.appDistance.size();
    if (!b.route || n == 0)
        return 0.0;
    if (d <= b.appDistance.constFirst())
        return b.drawnDistance.constFirst();
    if (d >= b.appDistance.constLast())
        return b.drawnDistance.constLast();
    // last point with app distance <= d
    const auto it = std::upper_bound(b.appDistance.cbegin(), b.appDistance.cend(), d);
    const int i = std::max(0, int(it - b.appDistance.cbegin()) - 1);
    const int j = std::min(n - 1, i + 1);
    const double a = b.appDistance.at(i), c = b.appDistance.at(j);
    const double t = c > a ? (d - a) / (c - a) : 0.0;
    return b.drawnDistance.at(i) + (b.drawnDistance.at(j) - b.drawnDistance.at(i)) * t;
}

void Route3DItem::rebuildRoute()
{
    // RlvSession also emits routeChanged after every library scan (its
    // routeIndex): the same route is not built again (no GUI stall, the
    // renderer keeps its terrain).
    const QString path = m_session && m_session->hasRoute() ? m_session->routePath() : QString();
    const int pointCount = m_session && m_session->hasRoute() ? int(m_session->route().points().size()) : 0;
    const double length = m_session && m_session->hasRoute() ? m_session->route().totalDistance() : 0.0;
    if (m_builtValid && path == m_builtPath && pointCount == m_builtPoints && length == m_builtLength)
        return;
    m_builtValid = true;
    m_builtPath = path;
    m_builtPoints = pointCount;
    m_builtLength = length;
    const bool hadScene = hasScene();
    const QString before = error();
    m_built = Built();
    m_route.reset();
    m_routeError.clear();
    if (m_session && m_session->hasRoute()) {
        try {
            m_built = build(m_session->route());
            m_route = m_built.route;
        } catch (const std::exception &e) {
            m_routeError = QString::fromUtf8(e.what());
            qWarning("Route3D: %s: %s", qPrintable(m_session->routePath()), e.what());
        }
    }
    ++m_routeVersion;
    if (hadScene != hasScene() || hasScene())
        emit sceneChanged();
    if (error() != before)
        emit errorChanged();
    onPosition();
    m_frameDistance = m_baseDistance;   // a new route: no catching up
}

void Route3DItem::onPosition()
{
    const double old = m_baseDistance;
    m_baseDistance = m_session && m_route ? mapDistance(m_built, m_session->distanceMeters()) : 0.0;
    m_startDrawn = m_session && m_route ? mapDistance(m_built, m_session->startDistanceM()) : 0.0;
    m_speedKmh = m_session ? m_session->speedKmh() : 0.0;
    const bool running = m_session && m_session->running();
    if (m_baseDistance != old || running != m_running)
        m_sinceTick.restart();
    m_running = running;
    if (!m_running)
        m_frameDistance = m_baseDistance;
    updateTimer();
    if (isVisible() && m_supported && m_route)   // (shown again: itemChange updates)
        update();
}

void Route3DItem::onFrame()
{
    // legs: the real cadence while riding (smoothed), still when paused / coasting
    const double dt = std::min(m_frameClock.restart() / 1000.0, 0.25);
    // (runner: steps per minute -> stride cycles of two steps)
    const double rpm = m_running && m_session ? std::max(0, m_session->cadence()) * (m_runner ? 0.5 : 1.0) : 0.0;
    m_shownRpm += (rpm - m_shownRpm) * (1.0 - std::exp(-dt / kCadenceSmoothingSec));
    if (rpm == 0 && m_shownRpm < 3)
        m_shownRpm = 0;   // come to rest instead of creeping
    m_crank = std::fmod(m_crank + m_shownRpm / 60.0 * 2 * M_PI * dt, 2 * M_PI);
    if (m_running && m_route) {
        // extrapolate from the last tick; never backwards (the next tick
        // catches up), but a jump (seek) is taken at once
        const double dt = std::min(m_sinceTick.elapsed() / 1000.0, kMaxExtrapolationSec);
        const double target = std::min(m_route->finish(), m_baseDistance + m_speedKmh / 3.6 * dt);
        if (std::abs(target - m_frameDistance) > 25.0)
            m_frameDistance = target;
        else
            m_frameDistance = std::max(m_frameDistance, target);
    } else {
        m_frameDistance = m_baseDistance;
    }
    update();
}

void Route3DItem::updateTimer()
{
    const bool shown = isVisible() && m_supported && m_route;
    const int want = !shown ? 0 : m_running ? kFrameMs : kIdleFrameMs;
    if (want == 0) {
        m_timer.stop();
    } else if (!m_timer.isActive() || m_timer.interval() != want) {
        m_timer.start(want);
    }
}

void Route3DItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickFramebufferObject::itemChange(change, value);
    if (change == ItemVisibleHasChanged) {
        updateTimer();
        if (isVisible())
            update();
    } else if (change == ItemSceneChange) {
        checkSupported();
    }
}

} // namespace watt
