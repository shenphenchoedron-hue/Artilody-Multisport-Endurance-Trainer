#include "RunRoutes.h"

#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace watt::runroutes {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLat0 = 55.8000;
constexpr double kLon0 = 12.3700;
constexpr double kMetresPerDegLat = 111195.0;
constexpr double kTargetLengthM = 10000.0;
constexpr double kSpacingM = 8.0;

// Corners of the loop (metres, x east / y north) and the bend radius at each.
struct Corner { double x, y, radius; };
const Corner kCorners[] = {
    { -600, 0, 50 },    { 1500, 0, 30 },    { 1500, 700, 30 },  { 2500, 700, 45 },
    { 2300, 1800, 60 }, { 1600, 2100, 40 }, { 1300, 1600, 25 }, { 700, 2300, 50 },
    { -300, 2100, 35 }, { -200, 1300, 45 }, { -900, 1000, 40 },
};
constexpr int kCornerCount = int(sizeof(kCorners) / sizeof(kCorners[0]));

// Planar path: straight lines with circular bends, starting and ending in the
// middle of the first edge (so the joint of the loop is a straight).
QVector<QPointF> buildPath(double scale)
{
    auto corner = [&](int i) {
        const Corner &c = kCorners[((i % kCornerCount) + kCornerCount) % kCornerCount];
        return QPointF(c.x * scale, c.y * scale);
    };
    auto radius = [&](int i) { return kCorners[((i % kCornerCount) + kCornerCount) % kCornerCount].radius; };

    QVector<QPointF> pts;
    const QPointF startPt = (corner(0) + corner(1)) / 2.0;
    pts.append(startPt);
    QPointF cursor = startPt;
    auto lineTo = [&](const QPointF &to) {
        const QPointF d = to - cursor;
        const double len = std::hypot(d.x(), d.y());
        const int n = std::max(1, int(std::ceil(len / kSpacingM)));
        for (int k = 1; k <= n; ++k)
            pts.append(cursor + d * (double(k) / n));
        cursor = to;
    };
    for (int i = 1; i <= kCornerCount; ++i) {
        const QPointF p = corner(i), prev = corner(i - 1), next = corner(i + 1);
        QPointF a = p - prev, b = next - p;
        const double la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
        a /= la;
        b /= lb;
        const double turn = std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y());
        double r = radius(i);
        double tangent = r * std::tan(std::abs(turn) / 2.0);
        const double maxTangent = 0.45 * std::min(la, lb);
        if (tangent > maxTangent) {
            tangent = maxTangent;
            r = tangent / std::tan(std::abs(turn) / 2.0);
        }
        const QPointF t1 = p - a * tangent;
        const QPointF t2 = p + b * tangent;
        lineTo(t1);
        // arc from t1 to t2 around the centre on the inner side
        const double side = turn > 0 ? 1.0 : -1.0;   // left turn: centre to the left
        const QPointF normal(-a.y() * side, a.x() * side);
        const QPointF centre = t1 + normal * r;
        const double a0 = std::atan2(t1.y() - centre.y(), t1.x() - centre.x());
        const double arcLen = std::abs(turn) * r;
        const int n = std::max(2, int(std::ceil(arcLen / kSpacingM)));
        for (int k = 1; k <= n; ++k) {
            const double ang = a0 + turn * (double(k) / n);
            pts.append(QPointF(centre.x() + r * std::cos(ang), centre.y() + r * std::sin(ang)));
        }
        cursor = pts.constLast();
        // (the next corner's straight starts at t2 - already on the arc's end)
        Q_UNUSED(t2)
    }
    lineTo(startPt);
    return pts;
}

double pathLength(const QVector<QPointF> &pts)
{
    double len = 0;
    for (int i = 1; i < pts.size(); ++i)
        len += std::hypot(pts.at(i).x() - pts.at(i - 1).x(), pts.at(i).y() - pts.at(i - 1).y());
    return len;
}
} // namespace

RlvRoute standardRoute()
{
    // scale the corner layout so the loop is 10 km long (the bends, whose
    // radii do not scale, shorten it a little: a few refining passes)
    double scale = kTargetLengthM / pathLength(buildPath(1.0));
    for (int iter = 0; iter < 4; ++iter)
        scale *= kTargetLengthM / pathLength(buildPath(scale));
    const QVector<QPointF> pts = buildPath(scale);
    const double total = pathLength(pts);
    const double lonScale = kMetresPerDegLat * std::cos(kLat0 * kPi / 180.0);
    QVector<RlvPoint> out;
    out.reserve(pts.size());
    double along = 0;
    for (int i = 0; i < pts.size(); ++i) {
        if (i > 0)
            along += std::hypot(pts.at(i).x() - pts.at(i - 1).x(), pts.at(i).y() - pts.at(i - 1).y());
        const double u = 2.0 * kPi * along / total;   // periodic: the loop closes in height too
        RlvPoint p;
        p.time = i;
        p.lat = kLat0 + pts.at(i).y() / kMetresPerDegLat;
        p.lon = kLon0 + pts.at(i).x() / lonScale;
        p.rawElevation = 32.0 + 9.0 * std::sin(u - 0.6) + 4.5 * std::sin(3.0 * u + 1.1) + 2.2 * std::sin(7.0 * u + 2.3);
        out.append(p);
    }
    RlvRoute route;
    route.setPoints(out);
    return route;
}

bool isLoop(const RlvRoute &route)
{
    const auto &p = route.points();
    if (p.size() < 2 || route.totalDistance() < 4 * kLoopToleranceM)
        return false;
    return RlvRoute::haversine(p.constFirst().lat, p.constFirst().lon, p.constLast().lat, p.constLast().lon) <= kLoopToleranceM;
}

} // namespace watt::runroutes
