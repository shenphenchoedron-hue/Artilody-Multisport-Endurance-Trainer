#include "Route3DItem.h"

#include <QCoreApplication>
#include <QPointer>

#include "Route3DScene.h"
#include "DiagLog.h"
#include "RlvRoute.h"
#include "biome.h"
#include "RouteProgress.h"
#include "LiveRide.h"

static_assert(watt::Route3DItem::kMaxLive == watt::Route3DScene::kMaxLive
              && watt::Route3DItem::kMaxLive == watt::LiveRide::kMaxRiders, "the live riders: one count");
#include "Sprint.h"

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
// ridden: a point-to-point route would otherwise end in the grass), then
// tapers gently away.
constexpr double kRunOutM = 90.0;   // (the last 35 m narrow to nothing: Route3DScene::buildTile)

// The landscape of a route (route3d::Biomes::classify): from its middle
// (mean position) and its profile (mean elevation, metres climbed per km).
// WATT_BIOME=nordic|tropical|australian|desert|icy|mountain draws every
// route in that one (for trying them out).
route3d::Biome biomeOf(const RlvRoute &route)
{
    route3d::Biome forced;
    const QByteArray env = qgetenv("WATT_BIOME");
    if (!env.isEmpty() && route3d::Biomes::fromName(env.constData(), &forced))
        return forced;
    const QVector<RlvPoint> &pts = route.points();
    if (pts.isEmpty())
        return route3d::Biome::Nordic;
    const auto m = route3d::Biomes::summarize(pts.constData(), size_t(pts.size()));
    const auto biome = route3d::Biomes::classify(m.lat, m.lon, m.elevation, m.climbPerKm);
    // (in the diagnostic log too: which landscape a route got, on any computer)
    DiagLog::write("3D", QStringLiteral("landscape %1 (%2, %3; mean %4 m, %5 m/km)")
                             .arg(QLatin1String(route3d::Biomes::name(biome)))
                             .arg(m.lat, 0, 'f', 2)
                             .arg(m.lon, 0, 'f', 2)
                             .arg(m.elevation, 0, 'f', 0)
                             .arg(m.climbPerKm, 0, 'f', 1));
    return biome;
}

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
        // the route's marks (also when only they changed: GPX Workshop saved)
        if (view->marksVersion() != m_marksVersion) {
            m_marksVersion = view->marksVersion();
            std::vector<Route3DScene::SprintMarks> sprints;
            for (const auto &s : view->frameSprints())
                sprints.push_back({ s[0], s[1], s[2] });
            m_scene.setSprints(sprints);
            std::vector<Route3DScene::KomMarks> koms;
            for (const auto &k : view->frameKoms())
                koms.push_back({ k[0], k[1] });
            m_scene.setKoms(koms);
            std::vector<Route3DScene::PlaceMark> places;
            for (const auto &p : view->framePlaces())
                places.push_back({ p.at, p.name, p.left });
            m_scene.setPlaces(places);
            m_scene.setKmPosts(view->frameKmPosts());
        }
        m_scene.setLaps(view->frameLaps());
        m_scene.setStartDistance(view->frameStartDistance());
        m_scene.setAvatar(view->frameAvatar());
        m_scene.setRunnerAvatar(view->frameRunnerAvatar());
        m_scene.setRunner(view->frameRunner());
        m_scene.setWalkRunKmh(view->frameWalkRunKmh());
        const auto &g = view->frameRival(0);
        m_scene.setGhost(g.shown, g.frame, g.speedKmh, g.phase);
        const auto &p = view->frameRival(1);
        m_scene.setPacer(p.shown, p.frame, p.speedKmh, p.phase);
        for (int i = 0; i < Route3DItem::kMaxLive; ++i) {
            const auto &l = view->frameRival(2 + i);
            m_scene.setLiveLook(i, l.look, l.kind == 1);
            m_scene.setLive(i, l.shown, l.frame, l.speedKmh, l.phase);
        }
        {
            std::array<Route3DItem::LabelSpot, 6> spots {};
            for (int i = 0; i < Route3DItem::kMaxLive; ++i) {
                const auto &sp = m_scene.liveLabelSpots()[size_t(i)];
                spots[size_t(i)] = { sp.seen, sp.x, sp.y, sp.scale };
            }
            view->setLabelSpots(spots);
        }
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
    int m_marksVersion = -1;
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
    for (RivalFrame &r : m_rivalFrames)
        r.sinceTick.start();
    m_frameClock.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Route3DItem::onFrame);
    checkSupported();
}

Route3DItem::~Route3DItem() = default;

void Route3DItem::setLabelSpots(const std::array<LabelSpot, 6> &spots)
{
    // (the render thread: handed to the GUI thread)
    QMetaObject::invokeMethod(this, [this, spots] {
        m_spots = spots;
        publishLabels();
    }, Qt::QueuedConnection);
}

// The name signs: whenever someone else is on the route (riding alone: none),
// each over its rider's head.
void Route3DItem::publishLabels()
{
    QVariantList labels;
    int others = 0;
    for (int i = 0; i < kMaxLive; ++i)
        others += m_rivalFrames[size_t(2 + i)].shown ? 1 : 0;
    if (others >= 1) {
        for (int i = 0; i < kMaxLive; ++i) {
            const RivalFrame &f = m_rivalFrames[size_t(2 + i)];
            const LabelSpot &sp = m_spots[size_t(i)];
            if (!f.shown || !sp.seen || f.name.isEmpty())
                continue;
            labels.append(QVariantMap { { QStringLiteral("name"), f.name }, { QStringLiteral("jersey"), f.jersey },
                                        { QStringLiteral("x"), double(sp.x) * width() },
                                        { QStringLiteral("y"), double(sp.y) * height() },
                                        { QStringLiteral("scale"), double(sp.scale) } });
        }
    }
    // two riders close together: their signs stacked (the nearer - larger - one
    // lowest, on its rider; the other lifted above it), never on top of each other
    std::sort(labels.begin(), labels.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("scale")).toDouble() > b.toMap().value(QStringLiteral("scale")).toDouble();
    });
    QVector<QRectF> placed;
    for (QVariant &v : labels) {
        QVariantMap m = v.toMap();
        const double sc = m.value(QStringLiteral("scale")).toDouble();
        const double w = m.value(QStringLiteral("name")).toString().size() * 9.5 * sc + 22, h = 24 * sc + 6;
        QRectF r(m.value(QStringLiteral("x")).toDouble() - w / 2, m.value(QStringLiteral("y")).toDouble() - h, w, h);
        for (bool moved = true; moved;) {
            moved = false;
            for (const QRectF &o : std::as_const(placed)) {
                if (r.intersects(o)) {
                    r.moveBottom(o.top() - 2);
                    moved = true;
                }
            }
        }
        placed.append(r);
        m.insert(QStringLiteral("y"), r.bottom());
        v = m;
    }
    if (labels != m_liveLabels) {
        m_liveLabels = labels;
        emit liveLabelsChanged();
    }
}

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
        // GPX Ride loop: a closed lap is drawn (RlvSession only)
        if (s->metaObject()->indexOfSignal("loopChanged()") >= 0)
            connect(s, SIGNAL(loopChanged()), this, SLOT(rebuildRoute()));
        if (s->metaObject()->indexOfSignal("lapChanged()") >= 0)
            connect(s, SIGNAL(lapChanged()), this, SLOT(onPosition()));
        // the ghost / the pacer chosen / switched
        if (s->metaObject()->indexOfSignal("ghostChanged()") >= 0)
            connect(s, SIGNAL(ghostChanged()), this, SLOT(onPosition()));
        if (s->metaObject()->indexOfSignal("pacerChanged()") >= 0)
            connect(s, SIGNAL(pacerChanged()), this, SLOT(onPosition()));
        // the other stations' riders moved (riding together)
        if (s->metaObject()->indexOfSignal("liveChanged()") >= 0)
            connect(s, SIGNAL(liveChanged()), this, SLOT(onPosition()));
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

Route3DItem::Built Route3DItem::build(const RlvRoute &route, bool loop)
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
    const bool roundTrip = loop && route.isRoundTrip();
    const bool thereAndBack = loop && !roundTrip;
    size_t seam = ridden - 1;   // round trip: the last ridden geo point drawn (the lap's end on the road)
    size_t turnIn = 0, turnOut = 0, backStart = 0;   // there and back: around the roundabouts
    // A roundabout at geo point `at` (the track arrives there going away from
    // geo[at + step]): the road runs on into a ring of kRingR m round an
    // island, counter-clockwise, and comes back out onto itself at `at`
    // heading back. Appends the points after `at`, the last one = geo[at].
    auto roundabout = [&](size_t at, int step) {
        constexpr double kRingR = 12.0, kLead = 8.0, kMPerDeg = 111320.0;
        const route3d::GeoPoint e = geo[at];
        const double mLon = kMPerDeg * std::cos(e.lat * M_PI / 180.0);
        // the arriving direction (east, north; unit) over ~20 m
        size_t k = at;
        while (int(k) + step >= 0 && size_t(int(k) + step) < geo.size()
               && RlvRoute::haversine(geo[k].lat, geo[k].lon, e.lat, e.lon) < 20.0)
            k = size_t(int(k) + step);
        double fe = (e.lon - geo[k].lon) * mLon, fn = (e.lat - geo[k].lat) * kMPerDeg;
        const double fl = std::hypot(fe, fn);
        if (fl < 1.0)
            return;
        fe /= fl;
        fn /= fl;
        const double ce = fe * (kRingR + kLead), cn = fn * (kRingR + kLead);   // the island's centre
        auto put = [&](double pe, double pn) { geo.push_back({ e.lat + pn / kMPerDeg, e.lon + pe / mLon, e.elevation }); };
        // Hermite p0 -> p1 (tangents t0, t1, unit; scaled to the chord), the points after p0
        auto curve = [&](double p0e, double p0n, double t0e, double t0n, double p1e, double p1n, double t1e, double t1n,
                         bool last) {
            const double chord = std::hypot(p1e - p0e, p1n - p0n);
            const int steps = std::max(2, int(std::ceil(chord / 2.5)));
            for (int i = 1; i <= steps - (last ? 0 : 1); ++i) {
                const double t = double(i) / steps, t2 = t * t, t3 = t2 * t;
                const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                put(h00 * p0e + h10 * chord * t0e + h01 * p1e + h11 * chord * t1e,
                    h00 * p0n + h10 * chord * t0n + h01 * p1n + h11 * chord * t1n);
            }
        };
        const double a0 = std::atan2(-fn, -fe);                       // the ring's near point
        const double aIn = a0 + 40 * M_PI / 180, aOut = a0 + 320 * M_PI / 180;   // in on the right, out on the left
        auto ring = [&](double a, double &pe, double &pn, double &te, double &tn) {
            pe = ce + kRingR * std::cos(a);
            pn = cn + kRingR * std::sin(a);
            te = -std::sin(a);   // counter-clockwise
            tn = std::cos(a);
        };
        double ie, in, ite, itn, oe, on, ote, otn;
        ring(aIn, ie, in, ite, itn);
        ring(aOut, oe, on, ote, otn);
        curve(0, 0, fe, fn, ie, in, ite, itn, false);   // in
        const int arc = int(std::ceil(kRingR * (aOut - aIn) / 2.5));
        for (int i = 0; i <= arc; ++i) {
            double pe, pn, te, tn;
            ring(aIn + (aOut - aIn) * i / arc, pe, pn, te, tn);
            put(pe, pn);
        }
        curve(oe, on, ote, otn, 0, 0, -fe, -fn, true);   // out, back onto the road
        geo.back() = e;
    };
    if (roundTrip) {
        // round and round: the road closes back to the start (no run-out) on
        // a curve that leaves the end the way the track arrives and meets the
        // start the way the track leaves it (no corner at the seam)
        const route3d::GeoPoint first = geo.front();
        // the direction (degrees per metre) over ~20 m from point `from` going `step`
        auto heading = [&](size_t from, int step, double &dLat, double &dLon) {
            size_t k = from;
            while (k + step < geo.size() && int(k) + step >= 0
                   && RlvRoute::haversine(geo[k].lat, geo[k].lon, geo[from].lat, geo[from].lon) < 20.0)
                k = size_t(int(k) + step);
            const double span = RlvRoute::haversine(geo[k].lat, geo[k].lon, geo[from].lat, geo[from].lon);
            dLat = span > 1 ? (geo[from].lat - geo[k].lat) / span * (step < 0 ? 1 : -1) : 0;
            dLon = span > 1 ? (geo[from].lon - geo[k].lon) / span * (step < 0 ? 1 : -1) : 0;
        };
        double eLat, eLon, sLat, sLon;
        heading(ridden - 1, -1, eLat, eLon);   // arriving at the end
        heading(0, 1, sLat, sLon);             // leaving the start
        // metres along (dLat, dLon) from a to b (degrees -> metres, locally)
        const double mLon = std::cos(first.lat * M_PI / 180.0);
        auto along = [&](const route3d::GeoPoint &a, const route3d::GeoPoint &b, double dLat, double dLon) {
            const double n = std::hypot(dLat, dLon * mLon);
            return n > 0 ? ((b.lat - a.lat) * dLat + (b.lon - a.lon) * dLon * mLon * mLon) / n * 111320.0 : 0.0;
        };
        // a track ending past the start (on the same road): the tail beyond
        // it is left out - the closing curve would fold back on itself
        while (seam > ridden / 2 && RlvRoute::haversine(geo[seam].lat, geo[seam].lon, first.lat, first.lon) < 60.0
               && along(geo[seam], first, eLat, eLon) < 1.0)
            --seam;
        geo.resize(seam + 1);
        // the end's height drifts from the start's (GPS): spread the step
        // over the last ~200 m (no ramp at the finish line)
        {
            const double rise = first.elevation - geo[seam].elevation;
            double back = 0;
            for (size_t k = seam; k > 0 && back < 200.0; --k) {
                geo[k].elevation += rise * (1.0 - back / 200.0);
                back += RlvRoute::haversine(geo[k].lat, geo[k].lon, geo[k - 1].lat, geo[k - 1].lon);
            }
        }
        const route3d::GeoPoint end = geo[seam];
        const double gap = RlvRoute::haversine(first.lat, first.lon, end.lat, end.lon);
        // tangents no longer than the way the chord goes (no loop in the curve)
        const double te = gap * std::clamp(along(end, first, eLat, eLon) / std::max(gap, 1e-6), .25, 1.0);
        const double ts = gap * std::clamp(along(end, first, sLat, sLon) / std::max(gap, 1e-6), .25, 1.0);
        const int steps = int(std::ceil(gap / 5.0));
        for (int k = 1; k < steps; ++k) {
            const double t = double(k) / steps, t2 = t * t, t3 = t2 * t;
            const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            geo.push_back({ h00 * end.lat + h10 * te * eLat + h01 * first.lat + h11 * ts * sLat,
                            h00 * end.lon + h10 * te * eLon + h01 * first.lon + h11 * ts * sLon,
                            end.elevation + (first.elevation - end.elevation) * (3 * t2 - 2 * t3) });
        }
        if (gap > 0.5)
            geo.push_back(first);
    } else if (thereAndBack) {
        // out and back again: at each end a roundabout to turn in (round the
        // island, keeping it on the left), then the same points backwards;
        // after the roundabout at the start the road goes out again - one
        // closed lap: out, turn, back, turn
        turnIn = ridden - 1;
        roundabout(ridden - 1, -1);
        turnOut = geo.size() - 1;   // (the end point again)
        for (size_t k = ridden - 1; k-- > 0;)
            geo.push_back(geo[k]);
        backStart = geo.size() - 1;   // (the start point, coming back)
        roundabout(0, 1);             // (ends on the start point: the seam)
    } else {
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
    // (a looped there-and-back road is drawn out and back: up to twice the 200 km)
    auto drawn = std::make_shared<route3d::Route>(route3d::Route::fromGeo(geo, &drawnAt, thereAndBack ? 400000 : 200000));
    if (!loop && geo.size() > ridden)
        drawn->finishDistance = drawnAt[ridden - 1];
    if (roundTrip || thereAndBack) {
        // the seam: the last point is the first (at() goes on round)
        drawn->points.back().x = drawn->points.front().x;
        drawn->points.back().y = drawn->points.front().y;
        drawn->points.back().z = drawn->points.front().z;
        drawn->closed = true;
        if (roundTrip)
            drawn->lapLines = { drawn->length() };
        else   // in the middle of each roundabout
            drawn->lapLines = { .5 * (drawnAt[turnIn] + drawnAt[turnOut]), .5 * (drawnAt[backStart] + drawn->length()) };
    }
    drawn->biome = int(biomeOf(route));
    b.route = drawn;
    b.appDistance.reserve(pts.size());
    b.drawnDistance.reserve(pts.size());
    double last = 0;
    // a round trip's tail past the start (left out of the road): ridden on
    // at the same rate from the last point drawn, then fitted into the lap
    // below (no stop at the finish line)
    double seamApp = 0, seamDrawn = 0;
    for (int i = 0; i < pts.size(); ++i) {
        // (never backwards: the app distance is monotonic, so is the drawn one)
        const size_t g = size_t(geoIndexOf[i]);
        double at;
        if (roundTrip && g > seam) {
            at = seamDrawn + (pts.at(i).distance - seamApp);
        } else {
            at = drawnAt[g];
            seamApp = pts.at(i).distance;
            seamDrawn = at;
        }
        last = std::max(last, roundTrip ? at : std::min(b.route->finish(), at));
        b.appDistance.append(pts.at(i).distance);
        b.drawnDistance.append(last);
    }
    if (roundTrip && !b.drawnDistance.isEmpty() && b.drawnDistance.constLast() > 0) {
        // the lap's end is the drawn road's end (= its start): the difference
        // (the closing curve, the tail left out) is spread over the lap's
        // last 200 m - smoothly, the rider stays where the grade is felt
        const double L = b.route->length(), E = b.drawnDistance.constLast(), extra = L - E;
        const double from = std::max(0.0, E - 200.0);
        for (double &d : b.drawnDistance)
            if (d > from)
                d = std::clamp(d + extra * (d - from) / std::max(1e-6, E - from), 0.0, L);
        b.drawnDistance.last() = L;
    } else if (thereAndBack) {
        // forward geo point k comes back at turnOut + (ridden - 1 - k)
        b.backDistance.reserve(pts.size());
        for (int i = 0; i < pts.size(); ++i)
            b.backDistance.append(drawnAt[turnOut + (ridden - 1 - size_t(geoIndexOf[i]))]);
        // the roundabouts: the app turns at once (the route's end, its start),
        // the road goes round the ring - the metres around it are spread over
        // the last / first kSpreadM of the route, the lap line in the middle
        const double total = pts.constLast().distance, L = b.route->length();
        const double spread = std::min(150.0, total / 3);
        const double turnLine = b.route->lapLines[0], startLine = b.route->lapLines[1] - L;   // (before 0: wraps)
        const double outEnd = b.drawnDistance.constLast(), backEnd = b.backDistance.constFirst();
        const double backTurn = b.backDistance.constLast(), outStart = b.drawnDistance.constFirst();
        for (int i = 0; i < pts.size(); ++i) {
            const double a = pts.at(i).distance;
            const double near = spread > 0 ? std::clamp((a - (total - spread)) / spread, 0.0, 1.0) : 0.0;
            const double nearStart = spread > 0 ? std::clamp((spread - a) / spread, 0.0, 1.0) : 0.0;
            b.drawnDistance[i] += (turnLine - outEnd) * near + (startLine - outStart) * nearStart;
            b.backDistance[i] += (turnLine - backTurn) * near + (b.route->lapLines[1] - backEnd) * nearStart;
        }
    }
    return b;
}

void Route3DItem::setAvatar(const QVariantMap &values)
{
    if (values == m_avatarValues)
        return;
    m_avatarValues = values;
    m_avatar = RiderAvatar::fromVariant(values);
    emit avatarChanged();
    update();
    // loaded in the background: drawn again when it is there
    m_avatar.preload([self = QPointer<Route3DItem>(this)] {
        QMetaObject::invokeMethod(qApp, [self] { if (self) self->update(); }, Qt::QueuedConnection);
    });
}

void Route3DItem::setRunnerAvatar(const QVariantMap &values)
{
    if (values == m_runnerAvatarValues)
        return;
    m_runnerAvatarValues = values;
    m_runnerAvatar = RiderAvatar::fromVariant(values, RiderAvatar::Runner);
    emit runnerAvatarChanged();
    update();
    m_runnerAvatar.preload([self = QPointer<Route3DItem>(this)] {
        QMetaObject::invokeMethod(qApp, [self] { if (self) self->update(); }, Qt::QueuedConnection);
    });
}

double Route3DItem::frameWalkRunKmh() const
{
    return m_session && m_sessionObject ? m_session->walkRunKmh() : m_walkRunKmh;
}

double Route3DItem::mapDistance(const Built &b, double d, bool returning)
{
    const int n = b.appDistance.size();
    if (!b.route || n == 0)
        return 0.0;
    const QVector<double> &to = returning && b.backDistance.size() == n ? b.backDistance : b.drawnDistance;
    if (d <= b.appDistance.constFirst())
        return to.constFirst();
    if (d >= b.appDistance.constLast())
        return to.constLast();
    // last point with app distance <= d
    const auto it = std::upper_bound(b.appDistance.cbegin(), b.appDistance.cend(), d);
    const int i = std::max(0, int(it - b.appDistance.cbegin()) - 1);
    const int j = std::min(n - 1, i + 1);
    const double a = b.appDistance.at(i), c = b.appDistance.at(j);
    const double t = c > a ? (d - a) / (c - a) : 0.0;
    return to.at(i) + (to.at(j) - to.at(i)) * t;
}

// The GPX Workshop's sprints and KOMs on the drawn road (the announce line:
// kAnnounceM before the start, if the road is there and it is clear of the
// segment before).
void Route3DItem::buildMarks()
{
    m_sprintDrawn.clear();
    m_komDrawn.clear();
    m_placeDrawn.clear();
    if (!m_route)
        return;
    double previousFinish = 0;
    for (const RouteSegment &s : m_builtSegments) {
        if (s.kind == RouteSegment::Place) {   // a town sign (on a looped there-and-back road also on the way back)
            m_placeDrawn.push_back({ mapDistance(m_built, s.start), s.name, s.left });
            if (!m_built.backDistance.isEmpty())   // (on the way back: the same side of the rider)
                m_placeDrawn.push_back({ mapDistance(m_built, s.start, true), s.name, s.left });
            continue;
        }
        if (s.kind == RouteSegment::Sprint) {
            const double at = s.start - sprint::kAnnounceM;
            const double announce = at >= previousFinish + 20 ? mapDistance(m_built, at) : -1;
            m_sprintDrawn.push_back({ announce, mapDistance(m_built, s.start), mapDistance(m_built, s.finish) });
        } else {
            m_komDrawn.push_back({ mapDistance(m_built, s.start), mapDistance(m_built, s.finish) });
        }
        previousFinish = s.finish;
    }
}

std::vector<std::pair<double, double>> Route3DItem::gravelDrawn(const Built &b, const QVector<RouteSegment> &gravel)
{
    std::vector<std::pair<double, double>> out;
    const bool back = !b.backDistance.isEmpty();
    for (const RouteSegment &g : gravel) {
        out.emplace_back(mapDistance(b, g.start), mapDistance(b, g.finish));
        if (back)   // a looped there-and-back road: the same gravel on the way back
            out.emplace_back(mapDistance(b, g.finish, true), mapDistance(b, g.start, true));
    }
    for (auto &r : out)
        if (r.second < r.first)
            std::swap(r.first, r.second);
    std::sort(out.begin(), out.end());
    // merged: sorted and not overlapping (Route::gravelAt)
    std::vector<std::pair<double, double>> merged;
    for (const auto &r : out) {
        if (r.second - r.first < 1e-6)
            continue;
        if (!merged.empty() && r.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, r.second);
        else
            merged.push_back(r);
    }
    return merged;
}

void Route3DItem::rebuildRoute()
{
    // RlvSession also emits routeChanged after every library scan (its
    // routeIndex): the same route is not built again (no GUI stall, the
    // renderer keeps its terrain).
    const QString path = m_session && m_session->hasRoute() ? m_session->routePath() : QString();
    const int pointCount = m_session && m_session->hasRoute() ? int(m_session->route().points().size()) : 0;
    const double length = m_session && m_session->hasRoute() ? m_session->route().totalDistance() : 0.0;
    // the elevation cleaning's limit changes the profile (GPX Workshop)
    const double grade = m_session && m_session->hasRoute() ? m_session->route().maxCleanGrade() : 0.0;
    // sprints / KOMs and the place signs: marks only (no new terrain)
    QVector<RouteSegment> segments = m_session ? m_session->routeSegments() : QVector<RouteSegment>();
    if (m_session)
        segments += m_session->routePlaces();
    const bool loop = m_session && m_session->hasRoute() && m_session->loopRide();
    // the gravel sections change the road itself (its tiles): a new build -
    // only where they lie, not their names
    const QVector<RouteSegment> gravel = m_session ? m_session->routeGravel() : QVector<RouteSegment>();
    auto sameGravel = [&] {
        if (gravel.size() != m_builtGravel.size())
            return false;
        for (int i = 0; i < gravel.size(); ++i)
            if (gravel.at(i).start != m_builtGravel.at(i).start || gravel.at(i).finish != m_builtGravel.at(i).finish)
                return false;
        return true;
    };
    if (m_builtValid && path == m_builtPath && pointCount == m_builtPoints && length == m_builtLength
        && grade == m_builtGrade && loop == m_builtLoop && sameGravel()) {
        if (segments != m_builtSegments) {   // only the sprints / KOMs: the marks, not the terrain
            m_builtSegments = segments;
            buildMarks();
            ++m_marksVersion;
            update();
        }
        return;
    }
    m_builtValid = true;
    m_builtPath = path;
    m_builtPoints = pointCount;
    m_builtLength = length;
    m_builtGrade = grade;
    m_builtLoop = loop;
    m_builtSegments = segments;
    m_builtGravel = gravel;
    const bool hadScene = hasScene();
    const QString before = error();
    m_built = Built();
    m_route.reset();
    m_routeError.clear();
    m_sprintDrawn.clear();
    m_komDrawn.clear();
    m_placeDrawn.clear();
    m_kmDrawn.clear();
    if (m_session && m_session->hasRoute()) {
        try {
            m_built = build(m_session->route(), loop);
            if (!gravel.isEmpty()) {
                auto withGravel = std::make_shared<route3d::Route>(*m_built.route);
                withGravel->gravel = gravelDrawn(m_built, gravel);
                m_built.route = withGravel;
            }
            m_route = m_built.route;
            buildMarks();
            m_kmDrawn = kmPosts();
        } catch (const std::exception &e) {
            m_routeError = QString::fromUtf8(e.what());
            qWarning("Route3D: %s: %s", qPrintable(m_session->routePath()), e.what());
        }
    }
    ++m_routeVersion;
    ++m_marksVersion;
    if (hadScene != hasScene() || hasScene())
        emit sceneChanged();
    if (error() != before)
        emit errorChanged();
    m_lapOffset = 0;
    m_baseDistance = 0;
    onPosition();
    m_frameDistance = m_baseDistance;   // a new route: no catching up
}

void Route3DItem::onPosition()
{
    const double old = m_baseDistance;
    const double mapped = m_session && m_route ? mapDistance(m_built, m_session->distanceMeters(), m_session->returning()) : 0.0;
    if (m_route && m_route->closed) {
        // a closed lap: counted on over the seam (the frames ride on smoothly,
        // the scene wraps), a new lap adds the lap's length
        const double L = m_route->length();
        const double now = mapped + m_lapOffset;
        if (now < old - L / 2)
            m_lapOffset += L;
        else if (now > old + L / 2 && m_lapOffset >= L)
            m_lapOffset -= L;
        m_baseDistance = mapped + m_lapOffset;
    } else {
        m_lapOffset = 0;
        m_baseDistance = mapped;
    }
    m_startDrawn = m_session && m_route ? mapDistance(m_built, m_session->startDistanceM()) : 0.0;
    m_laps = m_session && m_route && m_session->loopRide() ? m_session->lapsDone() : -1;
    if (m_builtLoop) {   // looping: the kilometres count on lap after lap
        auto posts = kmPosts();
        if (posts != m_kmDrawn) {
            m_kmDrawn = std::move(posts);
            ++m_marksVersion;
        }
    }
    m_speedKmh = m_session ? m_session->speedKmh() : 0.0;
    // the rivals: the ghost, the pacer (the other stations' riders below)
    const bool live = m_session && m_route;
    tickRival(m_rivalFrames[0], live && m_session->ghostVisible(), live ? m_session->ghostDistanceMeters() : 0.0,
              live && m_session->ghostReturning(), live ? m_session->ghostSpeedKmh() : 0.0);
    tickRival(m_rivalFrames[1], live && m_session->pacerVisible(), live ? m_session->pacerDistanceMeters() : 0.0,
              live && m_session->pacerReturning(), live ? m_session->pacerSpeedKmh() : 0.0);
    // the other stations' riders (riding together), nearest first
    const LiveRide *others = live ? m_session->liveRiders() : nullptr;
    for (int i = 0; i < kMaxLive; ++i) {
        const bool shown = others && i < others->list().size();
        const LiveRide::Rider r = shown ? others->list().at(i) : LiveRide::Rider();
        RivalFrame &f = m_rivalFrames[size_t(2 + i)];
        f.smooth = true;
        if (shown) {
            f.look = r.look;
            f.kind = r.runner ? 1 : 0;
            f.name = r.name;
            f.jersey = r.look.value(QStringLiteral("jersey")).toString();
        }
        tickRival(f, shown, r.distanceM, r.returning, r.speedKmh);
    }
    const bool running = m_session && m_session->running();
    if (m_baseDistance != old || running != m_running)
        m_sinceTick.restart();
    m_running = running;
    if (!m_running) {
        m_frameDistance = m_baseDistance;
        for (RivalFrame &r : m_rivalFrames)
            r.frame = r.base;
    }
    updateTimer();
    if (isVisible() && m_supported && m_route)   // (shown again: itemChange updates)
        update();
}

// The kilometre posts (drawn metres, km number). Without a loop: each whole
// kilometre of the route. Looping, the kilometres go on counting lap after
// lap: the posts of the cycle being ridden (a round trip's lap; out and back
// of a there-and-back loop), near its end also the first of the next.
std::vector<std::array<double, 2>> Route3DItem::kmPosts() const
{
    if (!m_session || !m_session->hasRoute() || !m_route)
        return {};
    return kmPosts(m_built, m_session->route().totalDistance(), m_builtLoop, m_session->lapsDone(),
                   m_session->distanceMeters(), m_session->returning());
}

std::vector<std::array<double, 2>> Route3DItem::kmPosts(const Built &b, double total, bool loop, int laps, double d,
                                                        bool returning)
{
    std::vector<std::array<double, 2>> out;
    if (!b.route || total <= 0)
        return out;
    if (!loop) {
        for (int km = 1; km * 1000.0 < total - 30; ++km)
            out.push_back({ mapDistance(b, km * 1000.0), double(km) });
        return out;
    }
    const bool back = !b.backDistance.isEmpty();   // there and back
    const double cycle = back ? 2 * total : total;
    const int c = back ? laps / 2 : laps;
    const double u = returning ? 2 * total - d : d;   // where in the cycle
    auto drawnAt = [&](double v) { return v <= total ? mapDistance(b, v) : mapDistance(b, 2 * total - v, true); };
    auto add = [&](int cy, double from, double to) {
        for (int km = int(std::floor(cy * cycle / 1000.0)) + 1; km * 1000.0 < cy * cycle + to; ++km) {
            const double v = km * 1000.0 - cy * cycle;
            if (v >= from && v > 30 && v < cycle - 30)
                out.push_back({ drawnAt(v), double(km) });
        }
    };
    add(c, 0, cycle);
    if (u > cycle - 500)   // the next cycle's first posts, ahead over the finish
        add(c + 1, 0, 300);
    return out;
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
        const double ahead = m_baseDistance + m_speedKmh / 3.6 * dt;
        const double target = m_route->closed ? ahead : std::min(m_route->finish(), ahead);
        if (std::abs(target - m_frameDistance) > 25.0)
            m_frameDistance = target;
        else
            m_frameDistance = std::max(m_frameDistance, target);
    } else {
        m_frameDistance = m_baseDistance;
    }
    for (RivalFrame &r : m_rivalFrames)
        frameRivalStep(r, dt);
    update();
}

// A tick: a rival's place (session metres -> drawn metres) and speed - the
// ghost, the pacer or another station's rider.
void Route3DItem::tickRival(RivalFrame &r, bool shown, double metres, bool returning, double speedKmh)
{
    const double base = shown ? mapDistance(m_built, metres, returning) : 0.0;
    if (base != r.base || shown != r.shown)
        r.sinceTick.restart();
    if (shown && (!r.shown || std::abs(base - r.frame) > 25.0))
        r.frame = base;   // (appears / jumps: no catching up)
    r.shown = shown;
    r.base = base;
    r.speedKmh = shown ? speedKmh : 0.0;
}

// A frame: from its last tick on at its speed (a closed lap: the scene wraps);
// a runner's legs follow the distance it moves, a cyclist's pedals turn at
// rivalRpm() of its speed.
void Route3DItem::frameRivalStep(RivalFrame &r, double dt)
{
    const double before = r.frame;
    if (r.smooth && r.shown && m_running && m_route) {
        // where its last report says it is now, and a frame of its own speed
        // towards it - eased, never a jump (unless far off: appears / turned)
        const double v = r.speedKmh / 3.6;
        const double target = r.base + v * std::min(r.sinceTick.elapsed() / 1000.0, kLiveExtrapolationSec);
        double error = target - r.frame;
        if (m_route->closed && std::abs(error) > m_route->length() / 2)
            error += error > 0 ? -m_route->length() : m_route->length();
        if (std::abs(error) > 25.0) {
            r.frame = target;
        } else {
            double step = v * dt + error * (1.0 - std::exp(-dt / kLiveEaseSec));
            if (v > 0.5)
                step = std::max(step, 0.0);   // (riding on: never backwards - it only slows a little)
            r.frame += step;
        }
        if (!m_route->closed)
            r.frame = std::min(m_route->finish(), r.frame);
    } else if (r.shown && m_running && m_route) {
        const double gdt = std::min(r.sinceTick.elapsed() / 1000.0, kMaxExtrapolationSec);
        const double ahead = r.base + r.speedKmh / 3.6 * gdt;
        r.frame = m_route->closed ? ahead : std::min(m_route->finish(), ahead);
    } else {
        r.frame = r.base;
    }
    // (a jump / the seam: no leg movement)
    double moved = r.frame - before;
    if (m_route && m_route->closed && std::abs(moved) > m_route->length() / 2)
        moved += moved > 0 ? -m_route->length() : m_route->length();
    const bool moving = std::abs(moved) < 25.0 && moved > 0;
    if (r.kind < 0 ? m_runner : r.kind == 1) {   // (a runner - also another station's beside a cyclist)
        if (moving) {   // a stride (two steps), longer the faster
            const double cycle = std::clamp(0.9 + r.speedKmh / 3.6 * 0.42, 1.2, 3.0);
            r.phase = std::fmod(r.phase + moved / cycle * 2 * M_PI, 2 * M_PI);
        }
        return;
    }
    const double rpm = moving ? rivalRpm(r.speedKmh / 3.6) : 0.0;
    r.shownRpm += (rpm - r.shownRpm) * (1.0 - std::exp(-dt / kCadenceSmoothingSec));
    if (rpm == 0 && r.shownRpm < 3)
        r.shownRpm = 0;   // come to rest instead of creeping
    r.phase = std::fmod(r.phase + r.shownRpm / 60.0 * 2 * M_PI * dt, 2 * M_PI);
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
