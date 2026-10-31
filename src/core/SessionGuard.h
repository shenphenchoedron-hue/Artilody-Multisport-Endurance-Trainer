#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace watt {

class FreeRideSession;
class RlvSession;
class RowingSession;
class RunSession;
class TrainingSession;
class VirtualGears;

// One session at a time (workout on Watt Training, Free Ride, Video Ride,
// row), without trapping the rider. Exposed to QML as "Sessions".
//
// Rules (wired by the constructor between the sessions):
//   * Only a session that is genuinely in progress - riding, or paused with
//     something recorded (inProgress()) - blocks starting another one.
//   * A session paused before anything was recorded is given up when
//     another session starts (abandon()).
//   * Sessions only start on an explicit Start (button / Space): pedaling or
//     rowing never starts or resumes one, so nothing can start unseen.
//   * Finished / idle / prepared ("ready" row, loaded workout or route)
//     sessions never block.
// The pages show WHY their Start is disabled from the *Blocker maps and offer
// the fix right there (go to the page, end and save, discard: endSession()).
class SessionGuard : public QObject
{
    Q_OBJECT
    // What blocks starting a session of this page; empty map = nothing.
    // Keys: kind ("workout" | "freeride" | "video" | "gpx" | "rowing"), page (Main.qml
    // page name), pageTitle, paused (bool), elapsedSec, text (e.g. "A workout
    // is paused on Watt Training."), what ("workout", "free ride", ...).
    Q_PROPERTY(QVariantMap workoutBlocker READ workoutBlocker NOTIFY changed)
    Q_PROPERTY(QVariantMap freeRideBlocker READ freeRideBlocker NOTIFY changed)
    Q_PROPERTY(QVariantMap videoBlocker READ videoBlocker NOTIFY changed)
    Q_PROPERTY(QVariantMap gpxBlocker READ gpxBlocker NOTIFY changed)
    Q_PROPERTY(QVariantMap rowingBlocker READ rowingBlocker NOTIFY changed)
    Q_PROPERTY(QVariantMap runBlocker READ runBlocker NOTIFY changed)
    // WATT_DEBUG_RLVUI=1: the sessions / blockers (and the Video Ride page's
    // enabling conditions) are logged to stderr on every change.
    Q_PROPERTY(bool debugUi READ debugUi CONSTANT)

public:
    // Any session may be null (tests).
    SessionGuard(TrainingSession *training, FreeRideSession *freeRide, RlvSession *rlv, RowingSession *rowing,
                 VirtualGears *gears, QObject *parent = nullptr);
    ~SessionGuard() override;
    // The GPX Ride (a second RlvSession, Kind::Gpx): wired like the video ride
    // against the free ride, the row and the video ride. Null removes it.
    void setGpxSession(RlvSession *gpx);
    // The Treadmill Run: blocks / is blocked by every other session (kind
    // "run", page "treadmill"). Null removes it.
    void setRunSession(RunSession *run);

    QVariantMap workoutBlocker() const { return blockerFor(QStringLiteral("workout")); }
    QVariantMap freeRideBlocker() const { return blockerFor(QStringLiteral("freeride")); }
    QVariantMap videoBlocker() const { return blockerFor(QStringLiteral("video")); }
    QVariantMap gpxBlocker() const { return blockerFor(QStringLiteral("gpx")); }
    QVariantMap rowingBlocker() const { return blockerFor(QStringLiteral("rowing")); }
    QVariantMap runBlocker() const { return blockerFor(QStringLiteral("run")); }
    // The first other session in progress (workout, free ride, video, row).
    Q_INVOKABLE QVariantMap blockerFor(const QString &kind) const;
    // Ends the session of `kind` (the blocker's "kind"): save = finish and
    // save (too short rides are not saved), else discard. Returns the saved
    // ride id or -1.
    Q_INVOKABLE qint64 endSession(const QString &kind, bool save);
    // "workout=paused(12s) freeRide=idle video=idle rowing=idle page=video"
    Q_INVOKABLE QString describe() const;

    bool debugUi() const { return m_debug; }
    void setDebugUi(bool on) { m_debug = on; }

signals:
    void changed();
    // endSession() ended it: rideId >= 0 when saved
    void sessionEnded(const QString &kind, qint64 rideId, bool save);

private:
    void wire();
    void unwire();
    void wireGpx();
    void unwireGpx();
    void wireRun();
    void unwireRun();
    void onPageChanged();
    void onSessionChanged();

    TrainingSession *m_training;
    FreeRideSession *m_freeRide;
    RlvSession *m_rlv;
    RlvSession *m_gpx = nullptr;
    RowingSession *m_rowing;
    RunSession *m_run = nullptr;
    VirtualGears *m_gears;
    bool m_debug = false;
    QString m_lastDescription;
};

} // namespace watt
