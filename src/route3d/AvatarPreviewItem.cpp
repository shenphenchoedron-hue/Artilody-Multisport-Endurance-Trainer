#include "AvatarPreviewItem.h"

#include "ghost_motion.h"
#include "ghost_rider.h"
#include "ghost_runner.h"
#include "Rowing3DItem.h"

#include <QColor>
#include <QCoreApplication>
#include <QPointer>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {

// a short straight, level road for the rider to stand on
const route3d::Route &previewRoute()
{
    static const route3d::Route route = [] {
        std::vector<route3d::GeoPoint> geo { { 55.0, 12.0, 10.0 }, { 55.0, 12.002, 10.0 } };
        return route3d::Route::fromGeo(geo);
    }();
    return route;
}

QString hex(const QVector3D &c)
{
    return QColor::fromRgbF(std::clamp(c.x(), 0.f, 1.f), std::clamp(c.y(), 0.f, 1.f), std::clamp(c.z(), 0.f, 1.f))
        .name(QColor::HexRgb);
}

} // namespace

AvatarPreviewItem::AvatarPreviewItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void AvatarPreviewItem::setKind(const QString &kind)
{
    const RiderAvatar::Kind k = kind == QLatin1String("runner") ? RiderAvatar::Runner
                                : kind == QLatin1String("rower") ? RiderAvatar::Rower : RiderAvatar::Cyclist;
    if (k == m_avatar.kind)
        return;
    m_avatar = RiderAvatar::fromVariant(m_values, k);
    emit kindChanged();
    reload();
}

void AvatarPreviewItem::setAvatar(const QVariantMap &values)
{
    if (values == m_values)
        return;
    m_values = values;
    m_avatar = RiderAvatar::fromVariant(values, m_avatar.kind);
    reload();
}

void AvatarPreviewItem::setYaw(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg == m_yaw)
        return;
    m_yaw = deg;
    emit yawChanged();
    update();
}

QVariantMap AvatarPreviewItem::ownColors() const
{
    QVariantMap out;
    if (m_avatar.kind == RiderAvatar::Cyclist)   // (the bike: any cyclist)
        out.insert(QStringLiteral("bike"), hex(route3d::GhostRider::kFrameColor));
    if (m_avatar.kind == RiderAvatar::Rower)   // (the boat: any rower)
        out.insert(QStringLiteral("boat"), hex(standardHullColor()));
    bool pending = false;
    const auto figure = m_avatar.figureIfReady(&pending);   // (not waiting: told again when loaded)
    if (!figure || !figure->textured())
        return out;
    for (int g = route3d::GlbModel::Jersey; g < route3d::GlbModel::GarmentCount; ++g) {
        const QVector3D c = figure->garmentColor(route3d::GlbModel::Garment(g));
        if (c.x() >= 0)
            out.insert(QLatin1String(RiderAvatar::garmentKey(route3d::GlbModel::Garment(g))), hex(c));
    }
    return out;
}

std::shared_ptr<const route3d::GlbModel> AvatarPreviewItem::figureNow() const
{
    // (not waiting: nothing is shown while the avatar loads - drawn when it is there)
    bool pending = false;
    return m_avatar.figureIfReady(&pending);
}

void AvatarPreviewItem::reload()
{
    m_posedValid = false;
    emit avatarChanged();
    update();
    // drawn again (and its own colours told) when the figure is loaded
    m_avatar.preload([self = QPointer<AvatarPreviewItem>(this)] {
        QMetaObject::invokeMethod(qApp, [self] {
            if (self) {
                self->m_posedValid = false;
                emit self->avatarChanged();
                self->update();
            }
        }, Qt::QueuedConnection);
    });
}

void AvatarPreviewItem::buildPosed()
{
    m_posedValid = true;
    m_posed.clear();
    m_posedIdx.clear();
    const auto figure = figureNow();
    constexpr double kAt = 30.0, kCrank = 1.1, kStride = 2.0;
    if (m_avatar.kind == RiderAvatar::Rower) {
        if (!figure)
            return;
        constexpr double kRowPhase = .3;   // the drive: legs pushing, arms pulling
        constexpr double kBoatHalfLength = 1.3;   // m of the boat shown each way from the seat
        rowerPreviewPose(*figure, kRowPhase, m_posed);
        const auto colors = m_avatar.figureColors(*figure);
        if (colors.size() == m_posed.size())
            for (size_t i = 0; i < m_posed.size(); ++i)
                m_posed[i].color = colors[i];
        // a stretch of the boat under the rower (its colour)
        std::vector<route3d::GhostVertex> boat;
        rowerPreviewBoat(m_avatar.boatColor, kBoatHalfLength, boat);
        m_posedIdx = figure->indices();
        const uint32_t base = uint32_t(m_posed.size());
        m_posed.insert(m_posed.end(), boat.begin(), boat.end());
        for (uint32_t k = 0; k < boat.size(); ++k)
            m_posedIdx.push_back(base + k);
    } else if (!figure) {
        return;   // (loading)
    } else if (m_avatar.kind == RiderAvatar::Runner) {
        route3d::GhostRunner runner;
        runner.setModel(figure);
        if (figure)
            runner.setColors(m_avatar.figureColors(*figure));
        m_posed = runner.build(previewRoute(), kAt, 11.0, kStride);
        m_posedIdx = runner.indices();
    } else {
        route3d::GhostRider rider;
        if (rider.setRiderModel(figure) && figure)
            rider.setRiderColors(m_avatar.figureColors(*figure));
        rider.setFrameColor(m_avatar.bikeColor);
        m_posed = rider.build(previewRoute(), kAt, 0.0, kCrank);
        m_posedIdx = rider.indices();
    }
}

void AvatarPreviewItem::paint(QPainter *painter)
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const int W = std::max(1, int(width() * dpr * 1.5)), H = std::max(1, int(height() * dpr * 1.5));   // (supersampled)
    if (width() < 4 || height() < 4)
        return;

    // the posed figure: built again only when the figure or its colours
    // changed (turning the view only draws it again)
    if (!m_posedValid)
        buildPosed();
    const bool ownFrame = m_avatar.kind == RiderAvatar::Rower;   // (the rower: in his own frame, z forward)
    constexpr double kAt = 30.0;
    const auto &v = m_posed;
    const auto &idx = m_posedIdx;
    if (v.empty() || idx.empty())
        return;
    const route3d::GhostPose pose = route3d::ghostPose(previewRoute(), kAt, 0.0);

    // the view: round the vertical, from the rider's right side at yaw 0
    const QVector3D up(0, 1, 0);
    QVector3D fwd = ownFrame ? QVector3D(0, 0, 1) : QVector3D(float(pose.fx), 0, float(pose.fz));
    fwd = fwd.lengthSquared() > 1e-6f ? fwd.normalized() : QVector3D(0, 0, -1);
    const QVector3D right = QVector3D::crossProduct(fwd, up).normalized();
    const float a = float(m_yaw * M_PI / 180.0);
    const QVector3D toEye = (right * std::cos(a) + fwd * std::sin(a)).normalized();
    const QVector3D viewX = QVector3D::crossProduct(up, toEye).normalized();
    const QVector3D viewUp = QVector3D::crossProduct(toEye, viewX).normalized();
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const auto &p : v)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p.position[k]);
            hi[k] = std::max(hi[k], p.position[k]);
        }
    const QVector3D center = (lo + hi) * .5f;
    std::vector<QVector3D> s(v.size());   // screen x, y (pixels), depth (towards the eye)
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (size_t i = 0; i < v.size(); ++i) {
        const QVector3D d = v[i].position - center;
        s[i] = QVector3D(QVector3D::dotProduct(d, viewX), QVector3D::dotProduct(d, viewUp), QVector3D::dotProduct(d, toEye));
        x0 = std::min(x0, s[i].x()); x1 = std::max(x1, s[i].x());
        y0 = std::min(y0, s[i].y()); y1 = std::max(y1, s[i].y());
    }
    const float scale = .92f * std::min(W / std::max(.01f, x1 - x0), H / std::max(.01f, y1 - y0));
    const float cx = W * .5f - (x0 + x1) * .5f * scale, cy = H * .5f + (y0 + y1) * .5f * scale;
    for (auto &p : s)
        p = QVector3D(cx + p.x() * scale, cy - p.y() * scale, p.z());

    // lit like the 3D view: sun from above, a little from the front and the left
    const QVector3D sun = (up * .75f + toEye * .55f - viewX * .35f).normalized();
    std::vector<QVector3D> lit(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        QVector3D n = v[i].normal;
        if (QVector3D::dotProduct(n, toEye) < 0)
            n = -n;   // (thin parts: lit on the side in view)
        const float d = std::max(0.f, QVector3D::dotProduct(n, sun));
        lit[i] = v[i].color * (.42f + .62f * d) + v[i].color * QVector3D(.06f, .08f, .12f) * (n.y() * .5f + .5f);
    }

    // a depth-buffered triangle rasteriser (colours blended across the triangle)
    QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    std::vector<float> depth(size_t(W) * size_t(H), -1e30f);
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        if (i0 >= s.size() || i1 >= s.size() || i2 >= s.size())
            continue;
        const QVector3D &A = s[i0], &B = s[i1], &C = s[i2];
        const float area = (B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x());
        if (std::abs(area) < 1e-9f)
            continue;
        const int bx0 = std::max(0, int(std::floor(std::min({ A.x(), B.x(), C.x() }))));
        const int bx1 = std::min(W - 1, int(std::ceil(std::max({ A.x(), B.x(), C.x() }))));
        const int by0 = std::max(0, int(std::floor(std::min({ A.y(), B.y(), C.y() }))));
        const int by1 = std::min(H - 1, int(std::ceil(std::max({ A.y(), B.y(), C.y() }))));
        for (int y = by0; y <= by1; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            const float py = y + .5f;
            for (int x = bx0; x <= bx1; ++x) {
                const float px = x + .5f;
                const float w0 = ((B.x() - px) * (C.y() - py) - (B.y() - py) * (C.x() - px)) / area;
                const float w1 = ((C.x() - px) * (A.y() - py) - (C.y() - py) * (A.x() - px)) / area;
                const float w2 = 1.f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                const float z = A.z() * w0 + B.z() * w1 + C.z() * w2;
                float &zb = depth[size_t(y) * size_t(W) + size_t(x)];
                if (z <= zb)
                    continue;
                zb = z;
                const QVector3D c = lit[i0] * w0 + lit[i1] * w1 + lit[i2] * w2;
                line[x] = qRgb(std::clamp(int(c.x() * 255.f + .5f), 0, 255), std::clamp(int(c.y() * 255.f + .5f), 0, 255),
                               std::clamp(int(c.z() * 255.f + .5f), 0, 255));
            }
        }
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(QRectF(0, 0, width(), height()), img);
}

} // namespace watt
