#pragma once

#include "route.h"
#include "RiderAvatar.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickFramebufferObject>
#include <QTimer>
#include "RouteSegments.h"

#include <QVector>
#include <array>
#include <cmath>
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
    // Riding together: a name sign over each other station's rider, when two or
    // more others are on the route - [{ name, jersey, x, y, scale }] in item
    // pixels (above the head; updated each frame).
    Q_PROPERTY(QVariantList liveLabels READ liveLabels NOTIFY liveLabelsChanged)
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)
    // a route is loaded and drawable (the 3D road could be built from it)
    Q_PROPERTY(bool hasScene READ hasScene NOTIFY sceneChanged)
    // Qt Quick runs on OpenGL (else the view stays empty)
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    // why the route / the view cannot be shown ("" = fine)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // the cyclist's look: the profile's avatar (Profiles.avatar3d: model,
    // jersey, shorts, helmet, shoes - RiderAvatar)
    Q_PROPERTY(QVariantMap avatar READ avatar WRITE setAvatar NOTIFY avatarChanged)
    // the runner's look (Treadmill Run): Profiles.runner3d (the same keys)
    Q_PROPERTY(QVariantMap runnerAvatar READ runnerAvatar WRITE setRunnerAvatar NOTIFY runnerAvatarChanged)

public:
    static constexpr int kFrameMs = 33;        // ~30 FPS while riding
    static constexpr int kIdleFrameMs = 100;   // clouds / trees keep moving when not riding
    static constexpr double kMaxExtrapolationSec = 0.6;
    // a live rider: extrapolated for at most this long without a report, and
    // eased onto a new report over about kLiveEaseSec
    static constexpr double kLiveExtrapolationSec = 3.0;
    static constexpr double kLiveEaseSec = 0.8;
    // The rider's legs follow the real cadence (RlvSession::cadence, ~1 Hz):
    // the shown rpm approaches it with this time constant, so the legs do not
    // jerk at each new value. 0 rpm (coasting, no cadence) = legs still.
    static constexpr double kCadenceSmoothingSec = 0.6;
    // The ghost's / pacer's legs (cyclist; no cadence in a trace): a rider
    // shifts gears, so the cadence is near kRivalRpm from low speed on -
    // rpm = kRivalRpm * (1 - exp(-v / kRivalRpmRiseMs)), v in m/s
    // (2 m/s: ~70 rpm, 4 m/s: ~85 rpm); still when it stands.
    static constexpr double kRivalRpm = 88.0;
    static constexpr double kRivalRpmRiseMs = 1.2;
    static double rivalRpm(double speedMs)
    {
        return speedMs > 0.1 ? kRivalRpm * (1.0 - std::exp(-speedMs / kRivalRpmRiseMs)) : 0.0;
    }

    explicit Route3DItem(QQuickItem *parent = nullptr);
    ~Route3DItem() override;

    Renderer *createRenderer() const override;

    QObject *session() const;
    void setSession(QObject *session);
    bool hasScene() const { return m_route != nullptr; }
    bool supported() const { return m_supported; }
    QString error() const { return m_routeError.isEmpty() ? m_rendererError : m_routeError; }
    QVariantMap avatar() const { return m_avatarValues; }
    void setAvatar(const QVariantMap &values);
    const RiderAvatar &frameAvatar() const { return m_avatar; }
    QVariantMap runnerAvatar() const { return m_runnerAvatarValues; }
    void setRunnerAvatar(const QVariantMap &values);
    const RiderAvatar &frameRunnerAvatar() const { return m_runnerAvatar; }

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
    // place signs: drawn metres, name
    struct PlaceDrawn { double at; QString name; bool left; };
    const std::vector<PlaceDrawn> &framePlaces() const { return m_placeDrawn; }
    // the kilometre posts: drawn metres, km number (Route3DScene::setKmPosts())
    const std::vector<std::array<double, 2>> &frameKmPosts() const { return m_kmDrawn; }
    // GPX Ride loop: laps finished (the lap board at the finish line); -1 = no loop
    int frameLaps() const { return m_laps; }
    bool frameRunner() const { return m_runner; }
    // (read live: the profile's setting may change during a run)
    double frameWalkRunKmh() const;
    float frameTime() const { return float(m_clock.elapsed() / 1000.0); }
    // the other riders: [0] the ghost of an earlier ride
    // (RouteProgressSource::ghostVisible() & co.), [1] the pacer (pacerVisible() & co.),
    // [2..] the other stations' riders (liveRiders())
    struct RivalFrame
    {
        bool shown = false;
        double base = 0, frame = 0, speedKmh = 0;   // drawn metres at the last tick / this frame
        double phase = 0;                           // pedal angle (rivalRpm()) / stride phase (distance moved)
        double shownRpm = 0;                        // cyclist: smoothed like the rider's
        QElapsedTimer sinceTick;
        // a live rider (another station's): its reports come about once a
        // second - moved on at its speed every frame and eased onto each new
        // report (kLiveEaseSec) instead of stopping and jumping
        bool smooth = false;
        QVariantMap look;                           // (a live rider) its figure and colours
        int kind = -1;                              // (a live rider) 0 rides, 1 runs; -1: as the own rider
        QString name;                               // (a live rider) its name (the sign)
        QString jersey;
    };
    QVariantList liveLabels() const { return m_liveLabels; }
    // (render thread, synchronize(): the GUI thread is blocked) the signs' spots
    // of the last frame: seen, x, y (0..1 of the picture), scale
    struct LabelSpot { bool seen = false; float x = 0, y = 0, scale = 1; };
    void setLabelSpots(const std::array<LabelSpot, 6> &spots);
    const RivalFrame &frameRival(int i) const { return m_rivalFrames[size_t(i)]; }
    static constexpr int kMaxLive = 6;   // other stations' riders drawn (= Route3DScene::kMaxLive = LiveRide::kMaxRiders)
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
    // The gravel sections (app metres) on the drawn road (drawn metres; a
    // looped there-and-back road also on the way back), sorted and merged.
    static std::vector<std::pair<double, double>> gravelDrawn(const Built &b, const QVector<RouteSegment> &gravel);
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
    void avatarChanged();
    void runnerAvatarChanged();
    void liveLabelsChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private slots:
    void rebuildRoute();
    void onPosition();

private:
    void publishLabels();
    std::array<LabelSpot, 6> m_spots {};
    QVariantList m_liveLabels;
    void onFrame();
    void updateTimer();
    void checkSupported();

    QPointer<QObject> m_sessionObject;   // the session (RlvSession / RunSession)
    RouteProgressSource *m_session = nullptr;   // valid while m_sessionObject is
    bool m_runner = false;               // draw a runner (Treadmill Run) instead of the cyclist
    QVariantMap m_avatarValues;
    RiderAvatar m_avatar;
    QVariantMap m_runnerAvatarValues;
    RiderAvatar m_runnerAvatar { RiderAvatar::Runner };
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
    QVector<RouteSegment> m_builtGravel;
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
    std::vector<PlaceDrawn> m_placeDrawn;   // place signs: drawn metres, name, side
    std::vector<std::array<double, 2>> m_kmDrawn;
    std::vector<std::array<double, 2>> kmPosts() const;
    double m_crank = 0;            // pedal angle (radians), integrated from the cadence
    double m_shownRpm = 0;         // smoothed cadence driving m_crank
    QElapsedTimer m_frameClock;    // time between frames for the crank
    bool m_running = false;
    QElapsedTimer m_sinceTick;
    // the rivals (the ghost, the pacer, the other stations' riders): drawn metres at the last tick, extrapolated with their speed
    std::array<RivalFrame, 2 + kMaxLive> m_rivalFrames;
    void tickRival(RivalFrame &r, bool shown, double metres, bool returning, double speedKmh);
    void frameRivalStep(RivalFrame &r, double dt);
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace watt
