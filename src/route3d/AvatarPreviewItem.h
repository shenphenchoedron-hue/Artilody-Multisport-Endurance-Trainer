#pragma once

#include "RiderAvatar.h"
#include "ghost_vertex.h"

#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>

namespace watt {

// The profile's avatar on its bike, as the GPX Ride's 3D view draws it
// (QML type "AvatarPreview", Artilody.Backend): the same figure, posed on the
// same bike (route3d::GhostRider), in the chosen colours - drawn in software
// (no OpenGL), so it can sit in a dialog.
//
//   AvatarPreview { avatar: { model: "male", jersey: "#d0202a" }; yaw: 30 }
// (kind: "runner" - the Treadmill Run's runner, in mid-stride)
class AvatarPreviewItem : public QQuickPaintedItem
{
    Q_OBJECT
    // RiderAvatar's keys: model, jersey, shorts, helmet, shoes, hair, bike, boat
    Q_PROPERTY(QVariantMap avatar READ avatar WRITE setAvatar NOTIFY avatarChanged)
    // the view round the rider (degrees; 0 = from his right side, 90 = from the front)
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY yawChanged)
    // "cyclist" (on the bike), "runner" (in mid-stride) or "rower" (in the drive)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    // the figures to choose from: { id, name } (RiderAvatar::models())
    Q_PROPERTY(QVariantList models READ models NOTIFY kindChanged)
    // the chosen figure's garments found in its texture: { jersey: "#rrggbb", ... }
    // (its own colours) - and a cyclist's bike ("bike": the standard frame
    // colour), a rower's boat ("boat": the standard hull colour)
    Q_PROPERTY(QVariantMap ownColors READ ownColors NOTIFY avatarChanged)
    // the figure has a helmet (a cyclist): its colour can be chosen
    Q_PROPERTY(bool helmet READ helmet NOTIFY kindChanged)

public:
    explicit AvatarPreviewItem(QQuickItem *parent = nullptr);

    QVariantMap avatar() const { return m_values; }
    void setAvatar(const QVariantMap &values);
    double yaw() const { return m_yaw; }
    void setYaw(double deg);
    QString kind() const
    {
        return m_avatar.kind == RiderAvatar::Runner ? QStringLiteral("runner")
               : m_avatar.kind == RiderAvatar::Rower ? QStringLiteral("rower") : QStringLiteral("cyclist");
    }
    void setKind(const QString &kind);
    QVariantList models() const { return RiderAvatar::models(m_avatar.kind); }
    bool helmet() const { return m_avatar.kind == RiderAvatar::Cyclist; }
    QVariantMap ownColors() const;

    void paint(QPainter *painter) override;

signals:
    void avatarChanged();
    void yawChanged();
    void kindChanged();

private:
    std::shared_ptr<const route3d::GlbModel> figureNow() const;
    void reload();       // the figure or its colours changed
    void buildPosed();   // m_posed: the figure posed in its colours
    QVariantMap m_values;
    std::vector<route3d::GhostVertex> m_posed;
    std::vector<uint32_t> m_posedIdx;
    bool m_posedValid = false;
    RiderAvatar m_avatar;
    double m_yaw = 35;
};

} // namespace watt
