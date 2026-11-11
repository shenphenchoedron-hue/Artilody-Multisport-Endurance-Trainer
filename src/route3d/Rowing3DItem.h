#pragma once

#include "RiderAvatar.h"

#include <QElapsedTimer>
#include <QVariantMap>
#include <QQuickFramebufferObject>
#include <QTimer>

namespace watt {

// The Rowing page's 3D view (QML type "Rowing3DView", Artilody.Backend): a
// single scull on a regatta lake seen from the side - the rower (rower.glb,
// a Mixamo rig, posed by the app) sits facing the stern and rows at the
// rower's stroke rate, the boat moves with the rowed distance; an optional
// ghost boat (half transparent) in the next lane rows the record.
//
//   Rowing3DView { distance: Rowing.distanceMeters; strokeRate: Rowing.spm; ... }
//
// The view only follows these numbers (the Rowing session is the source of
// truth); between updates the boats move on with their speed, so the picture
// is smooth at up to kFrameMs. OpenGL only, like Route3DView ("supported").
class Rowing3DItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY inputChanged)       // m rowed
    Q_PROPERTY(double speedMps READ speedMps WRITE setSpeedMps NOTIFY inputChanged)       // current boat speed
    Q_PROPERTY(int strokeRate READ strokeRate WRITE setStrokeRate NOTIFY inputChanged)    // strokes / min (0: still)
    Q_PROPERTY(bool rowing READ rowing WRITE setRowing NOTIFY inputChanged)               // the row is running
    Q_PROPERTY(bool ghostVisible READ ghostVisible WRITE setGhostVisible NOTIFY inputChanged)
    Q_PROPERTY(double ghostDistance READ ghostDistance WRITE setGhostDistance NOTIFY inputChanged)
    Q_PROPERTY(double finishDistance READ finishDistance WRITE setFinishDistance NOTIFY inputChanged)   // 0 = open
    Q_PROPERTY(bool supported READ supported NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    // the rower's look: the profile's avatar (Profiles.rower3d: model, jersey
    // (the top), shorts, hair - RiderAvatar::Rower); the ghost boat's too
    Q_PROPERTY(QVariantMap avatar READ avatar WRITE setAvatar NOTIFY avatarChanged)

public:
    static constexpr int kFrameMs = 33;
    static constexpr int kIdleFrameMs = 80;   // the water keeps moving

    explicit Rowing3DItem(QQuickItem *parent = nullptr);
    Renderer *createRenderer() const override;

    double distance() const { return m_distance; }
    void setDistance(double v);
    double speedMps() const { return m_speed; }
    void setSpeedMps(double v);
    int strokeRate() const { return m_rate; }
    void setStrokeRate(int v);
    bool rowing() const { return m_rowing; }
    void setRowing(bool v);
    bool ghostVisible() const { return m_ghostVisible; }
    void setGhostVisible(bool v);
    double ghostDistance() const { return m_ghostDistance; }
    void setGhostDistance(double v);
    double finishDistance() const { return m_finish; }
    void setFinishDistance(double v);
    bool supported() const { return m_supported; }
    QString error() const { return m_error; }
    void setRendererError(const QString &error);   // (render thread: queued)
    QVariantMap avatar() const { return m_avatarValues; }
    void setAvatar(const QVariantMap &values);
    const RiderAvatar &avatar3d() const { return m_avatar; }   // (render thread, in synchronize())

    // (render thread, in synchronize(); the GUI thread is blocked)
    struct Frame {
        double boat = 0, ghost = 0, finish = 0;
        double phase = 0, ghostPhase = 0;
        bool ghostVisible = false;
        float time = 0;
    };
    Frame frame() const { return m_frame; }

signals:
    void inputChanged();
    void statusChanged();
    void avatarChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void onFrame();
    void schedule();

    double m_distance = 0, m_speed = 0, m_ghostDistance = 0, m_finish = 0;
    int m_rate = 0;
    bool m_rowing = false, m_ghostVisible = false;
    bool m_supported = false;
    QString m_error;
    QVariantMap m_avatarValues;
    RiderAvatar m_avatar { RiderAvatar::Rower };
    // shown state (smoothed / extrapolated)
    Frame m_frame;
    double m_shownBoat = 0, m_shownGhost = 0, m_ghostSpeed = 0, m_lastGhostTarget = -1;
    bool m_first = true;
    bool m_ghostMoving = false;
    QElapsedTimer m_clock, m_frameClock, m_ghostClock;
    QTimer m_timer;
};

// The classic rower (rower.glb; null when there is none) and a figure posed
// as the Rowing view poses it at stroke phase `phase` (0 = the catch) - for
// the profile's preview (what is under the water line flattened onto it).
std::shared_ptr<const route3d::GlbModel> classicRowerModel();
void rowerPreviewPose(const route3d::GlbModel &figure, double phase, std::vector<route3d::GhostVertex> &out);
// The scull's hull (in the boat's colour; x < 0: the standard red) round the
// seat, +- halfLength along it, in the rower's frame (as rowerPreviewPose()):
// triangles, three vertices each.
void rowerPreviewBoat(QVector3D hullColor, double halfLength, std::vector<route3d::GhostVertex> &out);
QVector3D standardHullColor();
// How far (m) the figure, rowing, comes out through the hull's sides above
// the water at most (0: it stays inside).
float rowerHullOverhang(const route3d::GlbModel &figure);

} // namespace watt
