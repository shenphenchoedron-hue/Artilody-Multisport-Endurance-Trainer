#pragma once

#include <QElapsedTimer>
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
    // shown state (smoothed / extrapolated)
    Frame m_frame;
    double m_shownBoat = 0, m_shownGhost = 0, m_ghostSpeed = 0, m_lastGhostTarget = -1;
    bool m_first = true;
    bool m_ghostMoving = false;
    QElapsedTimer m_clock, m_frameClock, m_ghostClock;
    QTimer m_timer;
};

} // namespace watt
