#include "RlvSession.h"

#include "Database.h"
#include "DiagLog.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "RlvLibrary.h"
#include "SensorHub.h"
#include "SessionClock.h"
#include "TrainingSession.h"
#include "VirtualGears.h"
#include "Workout.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
// setting names below "rlv/" (Video Ride) or "gpx/" (GPX Ride), see settingKey()
const char *const kSettingMode = "mode";
const char *const kSettingDifficulty = "difficulty";
const char *const kSettingErgWatts = "ergWatts";
const char *const kSettingLastRoute = "lastRoute";
const char *const kSettingLoop = "loop";
const char *const kSettingRoadSound = "roadSound";
} // namespace

QString RlvSession::settingKey(const char *name) const
{
    return (isGpx() ? QStringLiteral("gpx/") : QStringLiteral("rlv/")) + QLatin1String(name);
}

QString RlvSession::rideWhat() const
{
    return isGpx() ? QStringLiteral("GPX ride") : QStringLiteral("video ride");
}

QString RlvSession::routeWhat() const
{
    return isGpx() ? QStringLiteral("route") : QStringLiteral("video");
}

RlvSession::RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears, RlvLibrary *library,
                       QObject *parent)
    : RlvSession(db, profiles, hub, gears, library, Kind::Video, parent)
{
}

RlvSession::RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears, RlvLibrary *library,
                       Kind kind, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
    , m_gears(gears)
    , m_res(hub)
    , m_library(library)
{
    m_timer.setInterval(250);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &RlvSession::onTimer);

    if (isGpx()) {
        // the ghost: an earlier ride on this route, at the same riding time
        m_ghost = new RouteGhost(m_db, QStringLiteral("gpx"), this);
        m_ghost->setPositioner([this](double covered) {
            return RouteRival::gpxPosition(covered, m_startDistance, totalDistanceM(), loopRide(), roundTrip());
        });
        connect(m_ghost, &RouteGhost::ghostChanged, this, &RlvSession::ghostChanged);
        // the pacer: a rider at a set W/kg, simulated with this ride's physics
        m_pacer = new RoutePacer(m_db, QStringLiteral("gpx"), this);
        m_pacer->setPositioner([this](double covered) {
            return RouteRival::gpxPosition(covered, m_startDistance, totalDistanceM(), loopRide(), roundTrip());
        });
        m_pacer->setGenerator([this](double wkg, int fromSec, double fromCovered, double fromSpeed) {
            return pacerTrace(wkg, fromSec, fromCovered, fromSpeed);
        });
        connect(m_pacer, &RouteRival::ghostChanged, this, &RlvSession::pacerChanged);
        // one rival at a time: the pacer switched on turns the ghost off, a
        // ghost chosen turns the pacer off (the pacer wins at start-up)
        const auto ghostOff = [this] {
            if (m_pacer->enabled() && m_ghost->choice() != QLatin1String("off"))
                m_ghost->setChoice(QStringLiteral("off"));
        };
        connect(m_pacer, &RoutePacer::settingsChanged, this, ghostOff);
        connect(m_ghost, &RouteGhost::choicesChanged, this, [this] {
            if (m_ghost->choice() != QLatin1String("off") && m_pacer->enabled())
                m_pacer->setEnabled(false);
        });
        ghostOff();
    }

    // road feel (gravel) follows the position, the state, ERG and the workout
    connect(this, &RlvSession::positionChanged, this, &RlvSession::updateRoadSurface);
    connect(this, &RlvSession::stateChanged, this, &RlvSession::updateRoadSurface);
    connect(this, &RlvSession::modeChanged, this, &RlvSession::updateRoadSurface);
    connect(this, &RlvSession::workoutChanged, this, &RlvSession::updateRoadSurface);
    connect(m_hub, &SensorHub::powerChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::displayPowerChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::cadenceChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::heartRateChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, &RlvSession::devicesChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, [this] {
        if (running() && trainerConnected())
            applyTarget(true);
        else
            updateHold(true);   // ERG selected on the open page: hold it now
    });
    // ERG outside a ride follows the open page (Main.qml: Gears.activePage).
    connect(m_gears, &VirtualGears::activePageChanged, this, [this] { updateHold(); });
    connect(m_hub, &SensorHub::trainerSimulationSupportChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::trainerReportsSpeedChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::cadenceReportedChanged, this, &RlvSession::resistanceSetupChanged);
    // Zwift hub protocol became active / failed: switch path at once
    connect(m_hub, &SensorHub::zwiftHubChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_gears, &VirtualGears::enabledChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_gears, &VirtualGears::shiftMethodChanged, this, &RlvSession::resistanceSetupChanged);
    auto gearChanged = [this] {
        emit targetChanged();
        emit metricsChanged();
        if (virtualGears())
            applyTarget(true);   // new gear / bike -> new resistance right away (ride or page hold)
    };
    connect(m_gears, &VirtualGears::gearChanged, this, gearChanged);
    connect(m_gears, &VirtualGears::gearSetupChanged, this, gearChanged);
    connect(m_gears, &VirtualGears::detectionChanged, this, gearChanged);   // the real gear (path a)
    if (m_library) {
        connect(m_library, &RlvLibrary::scanned, this, [this] { emit routeChanged(); });   // routeIndex
    }
    // the attached workout's watts follow the rider's FTP (not during a ride:
    // the ride keeps the FTP of its start)
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (!active()) {   // the sprint / KOM bests of the new rider
            loadBests(m_profiles->profileId());
            for (TimedSegment &seg : m_segments)
                seg.previousBest = seg.best;
            emitSegments(true, true);
            updateGhostContext();   // (and the new rider's ghosts)
        }
        if (!m_hasWorkout || active())
            return;
        m_workout.setFtp(m_profiles->current().ftpFor(sport::cycling()));
        rebuildWorkoutIntervals();
        emit workoutChanged();
        emit workoutTargetChanged();
        if (workoutErgActive())
            applyTarget(true);
    });

    const QString storedMode = m_db->setting(settingKey(kSettingMode), QStringLiteral("sim")).toString();
    m_mode = parseMode(storedMode).value_or(Mode::Sim);
    if (storedMode != mode())
        m_db->setSetting(settingKey(kSettingMode), mode());   // "power" / "gears" (old) -> "sim"
    const int storedErg = m_db->setting(settingKey(kSettingErgWatts), 0).toInt();
    m_ergTarget = storedErg > 0 ? std::clamp(storedErg, int(kMinErgWatts), int(kMaxWatts)) : defaultErgTarget();
    m_difficulty = std::clamp(m_db->setting(settingKey(kSettingDifficulty), kDefaultDifficulty).toInt(), 0, 100);
    m_loop = isGpx() && m_db->setting(settingKey(kSettingLoop), false).toBool();
    m_roadSound = m_db->setting(settingKey(kSettingRoadSound), true).toBool();

    const QString last = m_db->setting(settingKey(kSettingLastRoute)).toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) {
        selectPath(last);
        setError(QString());
    }
}

RlvSession::~RlvSession()
{
    if (m_training) {
        m_training->setStartBlocker(blockerKey(), {});
        m_training->setAbandonCanceller(blockerKey(), {});
    }
    if (active() || m_ergHold)
        m_res.release();
    m_gears->clearRideProfile(this);
}

// --- wiring ------------------------------------------------------------------------

void RlvSession::setTrainingSession(TrainingSession *training)
{
    if (m_training) {
        m_training->setStartBlocker(blockerKey(), {});
        m_training->setAbandonCanceller(blockerKey(), {});
    }
    m_training = training;
    if (training) {
        // a ride in progress blocks the workout; an empty paused one is
        // given up by its start
        training->setStartBlocker(blockerKey(), [this] { return inProgress(); });
        training->setAbandonCanceller(blockerKey(), [this] { abandon(); });
        connect(training, &TrainingSession::stateChanged, this, [this] { updateHold(); });
    }
}

void RlvSession::setStartBlocker(const QString &key, std::function<bool()> blocker)
{
    if (blocker)
        m_startBlockers.insert(key, std::move(blocker));
    else
        m_startBlockers.remove(key);
}

void RlvSession::setAbandonCanceller(const QString &key, std::function<void()> cancel)
{
    if (cancel)
        m_abandonCancellers.insert(key, std::move(cancel));
    else
        m_abandonCancellers.remove(key);
}

void RlvSession::abandonOthers()
{
    // only paused before anything was recorded: nothing is lost
    if (m_training)
        m_training->abandon();
    if (m_freeRide)
        m_freeRide->abandon();
    for (auto it = m_abandonCancellers.cbegin(); it != m_abandonCancellers.cend(); ++it) {
        if (it.value())
            it.value()();
    }
}

void RlvSession::syncRideProfile()
{
    if (active())
        m_gears->setRideProfile(this, m_rideProfile);
    else
        m_gears->clearRideProfile(this);
}

void RlvSession::resetLastRideId()
{
    m_lastRideId = -1;
    emit lastRideIdChanged();
}

bool RlvSession::startBlocked() const
{
    // only a session genuinely in progress (riding / paused with a recording)
    if (m_training && m_training->inProgress())
        return true;
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.value() && it.value()())
            return true;
    }
    return false;
}

const Gearing &RlvSession::gearing() const
{
    return m_gears->gearing();
}

void RlvSession::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

// --- state -----------------------------------------------------------------------------

QString RlvSession::state() const
{
    switch (m_state) {
    case State::Idle: return QStringLiteral("idle");
    case State::Running: return QStringLiteral("running");
    case State::Paused: return QStringLiteral("paused");
    case State::Finished: return QStringLiteral("finished");
    }
    return QStringLiteral("idle");
}

void RlvSession::setState(State s)
{
    if (m_state == s)
        return;
    const bool loopBefore = loopRide();
    m_state = s;
    m_clock.start();
    updateTimer();
    syncRideProfile();
    emit stateChanged();
    if (loopRide() != loopBefore)
        emit loopChanged();   // (the 3D view draws the road for the mode in use)
    if (!active())
        updateHold();   // idle / finished: ERG on the open page holds the target
}

void RlvSession::updateTimer()
{
    // the clock runs while a ride is running and while the page
    // holds the virtual gear resistance (it follows the cadence) - not while
    // paused, idle or holding a fixed ERG target
    const bool want = !m_manualClock && (running() || gearHolding());
    if (want && !m_timer.isActive()) {
        m_clock.start();
        m_timer.start();
    } else if (!want && m_timer.isActive()) {
        m_timer.stop();
    }
}

QString RlvSession::modeName(Mode m)
{
    switch (m) {
    case Mode::Sim: return QStringLiteral("sim");
    case Mode::Erg: return QStringLiteral("erg");
    }
    return QStringLiteral("sim");
}

std::optional<RlvSession::Mode> RlvSession::parseMode(const QString &text)
{
    if (text == QLatin1String("sim") || text == QLatin1String("power") || text == QLatin1String("gears"))
        return Mode::Sim;
    if (text == QLatin1String("erg"))
        return Mode::Erg;
    return std::nullopt;
}

void RlvSession::setMode(const QString &modeText)
{
    const auto parsed = parseMode(modeText);
    if (!parsed || *parsed == m_mode)
        return;
    m_mode = *parsed;
    m_db->setSetting(settingKey(kSettingMode), mode());
    if (m_mode == Mode::Erg) {
        // start from what the rider is pushing right now
        const int now = m_hub->displayPower();
        if (now >= kPedalPowerWatts) {
            // stored here, sent once by applyTarget() below
            m_ergTarget = std::clamp(int(std::lround(now / 5.0)) * 5, int(kMinErgWatts), int(kMaxWatts));
            m_db->setSetting(settingKey(kSettingErgWatts), m_ergTarget);
        }
    }
    emit modeChanged();
    emit positionChanged();   // effectiveGradePercent
    emit targetChanged();
    applyTarget(true);
}

void RlvSession::resistanceSetupChanged()
{
    emit modeChanged();   // virtualGears / simulationFallback / resistancePath
    emit targetChanged();
    emit metricsChanged();
    applyTarget(true);   // running: the ride's resistance; else the page hold
}

int RlvSession::defaultErgTarget() const
{
    const int ftp = m_profiles->current().ftpFor(sport::cycling());
    const int w = int(std::lround(std::max(1, ftp) * kDefaultErgFtpShare / 5.0)) * 5;
    return std::clamp(w, int(kMinErgWatts), int(kMaxWatts));
}

void RlvSession::setTargetWatts(int watts)
{
    watts = std::clamp(watts, int(kMinErgWatts), int(kMaxWatts));
    if (watts == m_ergTarget)
        return;
    m_ergTarget = watts;
    m_db->setSetting(settingKey(kSettingErgWatts), watts);
    emit targetChanged();
    if (m_mode == Mode::Erg)
        applyTarget(true);
}

bool RlvSession::simulationFallback() const
{
    return !ergNow() && !m_gears->enabled() && m_hub->trainerSimulationUnsupported();
}

bool RlvSession::virtualGears() const
{
    return !ergNow() && (m_gears->enabled() || m_hub->trainerSimulationUnsupported());
}

RideResistance::Path RlvSession::path() const
{
    return RideResistance::choose(ergNow(), virtualGears(), m_hub, m_gears->method());
}

int RlvSession::ergTargetNow() const
{
    if (workoutErgActive())
        return std::clamp(workoutTargetWatts(), int(kMinErgWatts), int(kMaxWatts));
    return m_ergTarget;
}

double RlvSession::gearSimulationGrade() const
{
    return RideResistance::gearGrade(gearDrive(controlCadence()), kMinSimGrade, kMaxSimGrade);
}

double RlvSession::simulationGradeToSend() const
{
    const double g = std::clamp(rideGradePercent(), kMinSimGrade, kMaxSimGrade);
    return std::round(g * 100.0) / 100.0;
}

void RlvSession::setDifficulty(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_difficulty)
        return;
    m_difficulty = percent;
    m_db->setSetting(settingKey(kSettingDifficulty), percent);
    emit difficultyChanged();
    emit positionChanged();   // effectiveGradePercent
    emit targetChanged();
    applyTarget(true);
    if (m_pacer)
        m_pacer->refresh();   // (its climbs follow the difficulty too)
}

// --- route -------------------------------------------------------------------------------

bool RlvSession::canSelect() const
{
    return !active();
}

int RlvSession::routeIndex() const
{
    return m_library && hasRoute() ? m_library->indexOfPath(m_route.sourcePath()) : -1;
}

bool RlvSession::select(int row)
{
    if (!m_library || row < 0 || row >= m_library->count()) {
        setError(QStringLiteral("No such %1").arg(routeWhat()));
        return false;
    }
    return selectPath(m_library->pathAt(row));
}

bool RlvSession::selectPath(const QString &pathIn)
{
    if (!canSelect()) {
        setError(QStringLiteral("Stop the current ride before choosing another %1").arg(routeWhat()));
        return false;
    }
    const QString path = RlvLibrary::toLocalPath(pathIn);
    // a GPX ride rides a plain .gpx track (a video next to it is ignored)
    if (isGpx() && QFileInfo(path).suffix().compare(QLatin1String("gpx"), Qt::CaseInsensitive) != 0) {
        setError(QStringLiteral("Choose a .gpx file"));
        return false;
    }
    QString error;
    RlvRoute route;
    if (!route.load(path, &error)) {
        setError(error);
        return false;
    }
    m_route = std::move(route);
    setError(isGpx() || m_route.hasVideo() ? QString() : error);
    m_db->setSetting(settingKey(kSettingLastRoute), m_route.sourcePath());
    m_profile.clear();
    for (const QPointF &p : m_route.profile(kProfileSamples))
        m_profile.append(QVariantMap { { QStringLiteral("d"), std::round(p.x() * 10.0) / 10.0 },
                                       { QStringLiteral("ele"), std::round(p.y() * 10.0) / 10.0 } });
    clearRecording();
    m_completed = false;
    m_startDistance = m_distance = 0;
    m_speed = 0;
    if (m_state == State::Finished)
        setState(State::Idle);
    segmentsRouteChanged();
    emit routeChanged();
    emit positionChanged();
    updateInformation();
    m_shouldPlay = false;
    m_rate = 1.0;
    emit videoChanged();
    requestSeek();
    return true;
}

void RlvSession::clearRoute()
{
    if (!canSelect())
        return;
    m_route = RlvRoute();
    m_profile.clear();
    clearRecording();
    m_startDistance = m_distance = 0;
    m_completed = false;
    if (m_state == State::Finished)
        setState(State::Idle);
    segmentsRouteChanged();
    emit routeChanged();
    emit positionChanged();
    updateInformation();
}

QUrl RlvSession::videoUrl() const
{
    return videoAvailable() ? QUrl::fromLocalFile(m_route.videoPath()) : QUrl();
}

int RlvSession::routeDurationSec() const
{
    // (the film's length; a GPX track has no film)
    return hasRoute() && !isGpx() ? int(std::lround(m_route.videoDuration() - m_route.videoOffset())) : 0;
}

QVariantList RlvSession::informations() const
{
    QVariantList out;
    for (const RlvInformation &i : m_route.informations())
        out.append(QVariantMap { { QStringLiteral("distance"), i.distance }, { QStringLiteral("text"), i.text } });
    return out;
}

double RlvSession::progress() const
{
    const double total = totalDistanceM();
    return total > 0 ? std::clamp(m_distance / total, 0.0, 1.0) : 0.0;
}

// --- the grade: one source for the rider (at m_distance) and the pacer ------

double RlvSession::gradeAt(double distance, bool returning) const
{
    // looking slightly ahead (a descent is felt when it starts)
    if (!hasRoute())
        return 0.0;
    return returning ? m_route.gradeBackAtDistance(distance) : m_route.gradeAheadAtDistance(distance);
}

double RlvSession::effectiveGradeAt(double distance, bool returning) const
{
    // ERG: the trainer holds the watts, the difficulty only applies to the
    // resistance modes - the speed uses the real gradient
    const double g = gradeAt(distance, returning);
    return ergNow() ? g : g * m_difficulty / 100.0;
}

bool RlvSession::gravelAtDistance(double distance) const
{
    return isGpx() && hasRoute() && m_route.gravelAt(distance);
}

double RlvSession::gravelGradeAt(double distance) const
{
    return gravelAllowed() && gravelAtDistance(distance)
               ? segments::kGravelGradePercent * m_route.gravelBlend(distance, segments::kGravelRampM)
               : 0.0;
}

double RlvSession::gradePercent() const
{
    return gradeAt(m_distance, m_returning);
}

double RlvSession::effectiveGradePercent() const
{
    return effectiveGradeAt(m_distance, m_returning);
}

bool RlvSession::onGravel() const
{
    return gravelAtDistance(m_distance);
}

bool RlvSession::gravelAllowed() const
{
    // not with ERG (the trainer holds the watts) and not riding a workout
    return !ergNow() && !(m_hasWorkout && !m_workoutFinished);
}

bool RlvSession::gravelEffort() const
{
    return active() && onGravel() && gravelAllowed();
}

double RlvSession::gravelGradePercent() const
{
    return active() ? gravelGradeAt(m_distance) : 0.0;   // (= gravelEffort() ? ... : 0)
}

double RlvSession::rideGradeAt(double distance, bool returning, bool gravel) const
{
    return effectiveGradeAt(distance, returning) + (gravel ? gravelGradeAt(distance) : 0.0);
}

double RlvSession::rideGradePercent() const
{
    return effectiveGradePercent() + gravelGradePercent();
}

void RlvSession::updateRoadSurface()
{
    const int want = running() && gravelEffort() ? SensorHub::RoadGravel : SensorHub::RoadSmooth;
    if (want == m_roadSurface)
        return;
    m_roadSurface = want;
    m_hub->setRoadSurface(want);
}

double RlvSession::gravelBlend() const
{
    return m_route.gravelBlend(m_distance, segments::kGravelRampM);
}

int RlvSession::gravelWatts() const
{
    if (!gravelEffort())
        return 0;
    // ΔCrr·m·g·v/η
    const road::Model m = roadModel(0.0);
    const double eff = m.efficiency > 0 ? m.efficiency : 1.0;
    return int(std::lround(segments::kGravelExtraCrr * gravelBlend() * m.massKg * road::kGravity * m_speed / eff));
}

double RlvSession::elevationM() const
{
    return hasRoute() ? m_route.elevationAtDistance(m_distance) : 0.0;
}

double RlvSession::ascentDoneM() const
{
    if (!hasRoute() || !(active() || m_state == State::Finished))
        return 0.0;   // (before the start: nothing climbed)
    return m_ascentBase + lapAscent();
}

double RlvSession::lapAscent() const
{
    if (!m_returning)
        return std::max(0.0, m_route.ascentAtDistance(m_distance) - m_route.ascentAtDistance(m_lapFrom));
    // backwards from m_lapFrom: the route's descents on [m_distance, m_lapFrom] are the climbs
    const double up = m_route.ascentAtDistance(m_lapFrom) - m_route.ascentAtDistance(m_distance);
    const double rise = m_route.elevationAtDistance(m_lapFrom) - m_route.elevationAtDistance(m_distance);
    return std::max(0.0, up - rise);
}

void RlvSession::setLoop(bool on)
{
    on = on && isGpx();
    if (on == m_loop || active())
        return;
    m_loop = on;
    m_db->setSetting(settingKey(kSettingLoop), on);
    emit loopChanged();
    updateGhostContext();   // (a loop races loops)
}

QVector<TracePoint> RlvSession::pacerTrace(double wkg, int fromSec, double fromCovered, double fromSpeed) const
{
    QVector<TracePoint> out;
    const double total = totalDistanceM();
    if (!hasRoute() || !(total > 0) || !(wkg > 0) || fromSec < 0 || fromSec >= kPacerMaxSec)
        return out;
    // the rider of the ride (roadModel(): the same profile, bike and air drag;
    // its watts from the weight the model uses)
    road::Model model = roadModel(0.0);
    const double riderKg = model.massKg - m_gears->setup().bikeWeightKg;
    const double watts = wkg * (riderKg > 0 ? riderKg : road::kDefaultRiderKg);
    const bool loop = loopRide(), trip = roundTrip();
    const double start = m_startDistance;
    out.reserve(loop ? 3600 : 1024);
    double v = std::max(0.0, fromSpeed), covered = std::max(0.0, fromCovered);
    out.append({ double(fromSec), covered });
    for (int t = fromSec + 1; t <= kPacerMaxSec; ++t) {
        const RouteRival::RoutePos pos = RouteRival::gpxPosition(covered, start, total, loop, trip);
        // the grade the ride's physics would use there (gravel too: riding)
        model.gradePercent = rideGradeAt(pos.distance, pos.returning, true);
        v = road::integrateSpeed(model, v, watts, 1.0, kRotatingMassKg, kMinDriveSpeedMps, kPacerStepSec);
        const double before = covered;
        covered += v;
        if (!loop && start + covered >= total) {   // the finish: when it crossed the line
            const double need = total - start - before;
            out.append({ t - 1 + (v > 0 ? std::clamp(need / v, 0.0, 1.0) : 1.0), total - start });
            break;
        }
        out.append({ double(t), covered });
    }
    return out;
}

void RlvSession::updateGhostContext()
{
    if (!m_ghost)
        return;
    RouteGhost::Context c;
    c.profileId = active() || m_state == State::Finished ? m_rideProfile.id : m_profiles->profileId();
    if (c.profileId < 0)
        c.profileId = m_profiles->profileId();
    c.routeKey = hasRoute() ? m_routeKey : QString();
    c.routeName = m_route.name();
    c.startM = m_startDistance;
    c.loop = loopRide();
    m_ghost->setContext(c);
    emit ghostChanged();
    // the pacer on the same route, from the same start (not while riding:
    // simulated at the start)
    if (m_pacer) {
        const RoutePacer::Batch once(m_pacer);   // (route and refresh: simulated once)
        m_pacer->setRoute(hasRoute(), loopRide());
        m_pacer->refresh();
    }
}

void RlvSession::setRoadSound(bool on)
{
    if (on == m_roadSound)
        return;
    m_roadSound = on;
    m_db->setSetting(settingKey(kSettingRoadSound), on);
    emit roadSoundChanged();
}

void RlvSession::clearLaps()
{
    m_ridden = 0;
    m_returning = false;
    m_lapsDone = 0;
    m_lapStartMs = 0;
    m_lastLapSec = 0;
    m_lapTimes.clear();
    m_ascentBase = 0;
    m_lapFrom = m_startDistance;
    m_lapRecordIds.clear();
    for (TimedSegment &seg : m_segments)
        seg.rearm = false;
    emit lapChanged();
}

void RlvSession::lapDone(double atMs)
{
    ++m_lapsDone;
    m_lastLapSec = std::round((atMs - m_lapStartMs) / 10.0) / 100.0;   // 0.01 s
    m_lapStartMs = atMs;
    m_lapTimes.append(m_lastLapSec);
    m_completed = true;
    DiagLog::write("RIDE", QStringLiteral("lap %1: %2 s").arg(m_lapsDone).arg(m_lastLapSec, 0, 'f', 2));
    emit lapCompleted(m_lapsDone, m_lastLapSec);
    emit lapChanged();
}

// Moves the rider step metres on (from d0 at t0, the tick's start). Without
// a loop to the route's end; looping, over each lap's finish line into the
// next lap: a round trip from the start again, any other route back
// towards the start (m_distance then falls) and from there out again.
void RlvSession::advance(double step, double d0, qint64 t0)
{
    // a new lap starts just before 0 m: a sprint / KOM starting at 0 m is crossed
    constexpr double kBeforeStart = -1e-6;
    const double total = totalDistanceM();
    if (!m_loopRide) {
        m_distance = std::min(total, m_distance + step);
        m_ridden += std::max(0.0, m_distance - d0);
        updateSegments(d0, t0);
        return;
    }
    m_ridden += step;
    // when in the tick the rider was `left` metres on
    auto crossing = [&](double left) {
        return step > 0 ? double(t0) + double(m_activeMs - t0) * std::clamp(left / step, 0.0, 1.0) : double(m_activeMs);
    };
    auto newLap = [&](double from) {
        m_lapFrom = from;
        if (from == 0)   // out again: the sprints and KOMs are timed again
            for (TimedSegment &seg : m_segments)
                seg.rearm = true;
    };
    if (!m_returning) {
        const double d = m_distance + step;
        if (d < total) {
            m_distance = d;
            updateSegments(d0, t0);
            return;
        }
        const double atMs = crossing(total - d0);
        m_distance = total;
        updateSegments(d0, double(t0), atMs);   // (a KOM to the very end: timed at the lap line)
        m_ascentBase += lapAscent();
        lapDone(atMs);
        const double over = std::min(d - total, total);
        if (roundTrip()) {
            newLap(0);
            m_distance = over;
            updateSegments(kBeforeStart, atMs, double(m_activeMs));
        } else {
            m_returning = true;
            newLap(total);
            m_distance = total - over;
            emit lapChanged();
        }
        return;
    }
    const double d = m_distance - step;
    if (d > 0) {
        m_distance = d;
        return;
    }
    const double atMs = crossing(d0);
    m_distance = 0;
    m_ascentBase += lapAscent();
    lapDone(atMs);
    m_returning = false;
    newLap(0);
    m_distance = std::min(-d, total);
    updateSegments(kBeforeStart, atMs, double(m_activeMs));
    emit lapChanged();
}

QString RlvSession::currentInformation() const
{
    return m_infoIndex >= 0 && m_infoIndex < m_route.informations().size() ? m_route.informations().at(m_infoIndex).text
                                                                         : QString();
}

void RlvSession::updateInformation()
{
    const int i = hasRoute() ? m_route.informationIndexAt(m_distance) : -1;
    if (i == m_infoIndex)
        return;
    m_infoIndex = i;
    emit informationChanged();
}

QString RlvSession::formatDistance(double meters) const
{
    if (!std::isfinite(meters))
        return QStringLiteral("–");
    if (std::abs(meters) < 1000.0)
        return QStringLiteral("%1 m").arg(int(std::lround(meters)));
    return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'f', 2);
}

// --- live values ---------------------------------------------------------------------------

int RlvSession::watts() const { return m_hub->power(); }
int RlvSession::displayWatts() const { return m_hub->displayPower(); }
int RlvSession::cadence() const { return m_hub->cadence(); }
int RlvSession::heartRate() const { return m_hub->heartRate(); }
bool RlvSession::trainerConnected() const { return m_hub->trainerConnected(); }

bool RlvSession::pedaling() const
{
    // the power OR the cadence: the trainer's cadence lags (0 rpm at 180 W at
    // the start), and light pedaling reads 0 W on the Victory (36–107 rpm at
    // 0–24 W, log of 1.3.8) - that is pedaling too, not coasting
    return m_hub->power() >= kPedalPowerWatts || m_hub->cadence() >= kCoastCadenceRpm;
}

bool RlvSession::coasting() const
{
    // Zwift protocol: coasting = not pedaling while the flywheel turns; with
    // the flywheel standing still the bike has stopped
    if (path() == RideResistance::Path::GearHub && m_hub->trainerReportsSpeed())
        return !pedaling() && !m_gears->hubFlywheelStopped();
    return m_hub->cadence() < kCoastCadenceRpm && m_hub->power() < kPedalPowerWatts;
}

road::Model RlvSession::roadModel(double grade) const
{
    // the rider of the ride; weight / height edits of the same profile apply
    // at once (like the trainer's weight, SensorHub::riderMassKg)
    const Profile &cur = m_profiles->current();
    const Profile &p = active() && m_rideProfile.id != cur.id ? m_rideProfile : cur;
    return p.weightKg > 0 ? m_gears->roadModel(grade, p) : m_gears->roadModel(grade);
}

double RlvSession::solveSpeedForPower(double w, double grade) const
{
    if (!std::isfinite(w) || w <= 0)
        return 0.0;
    return road::speedForPower(roadModel(grade), w);
}

double RlvSession::speedMps() const
{
    if (active())
        return m_speed;
    const int cad = m_hub->cadence();
    // Zwift protocol: the trainer does the physics - speed from the power
    if (virtualGears() && cad >= kCoastCadenceRpm && path() != RideResistance::Path::GearHub)
        return gearing().virtualSpeed(cad);
    const int p = m_hub->power();
    return p > 0 ? solveSpeedForPower(p, rideGradePercent()) : 0.0;
}

double RlvSession::avgSpeedKmh() const
{
    return m_activeMs > 0 ? riddenMeters() / (m_activeMs / 1000.0) * 3.6 : 0.0;
}

int RlvSession::avgWatts() const
{
    return m_recordedSec > 0 ? int(std::lround(m_powerSum / m_recordedSec)) : 0;
}

// --- resistance ------------------------------------------------------------------------------

RideResistance::GearDrive RlvSession::gearDrive(double cadenceRpm) const
{
    RideResistance::GearDrive d;
    d.road = roadModel(rideGradePercent());
    d.cadenceRpm = cadenceRpm;
    d.virtualRatio = gearing().ratio();
    d.referenceRatio = m_gears->effectiveReferenceRatio();   // the detected real gear when known
    d.circumferenceMm = gearing().setup().circumferenceMm;
    d.riderKg = m_gears->riderWeightKg();
    d.hubGearFactor = m_gears->hubGearFactor();   // Zwift hub path: virtual gear for the real gear on the trainer
    d.hubGearKnown = m_gears->hubGearKnown();     // unknown: the soft start keeps a light gear
    d.hubSpeedVerifiedFlywheel = m_gears->hubGear().speed() == HubGearCompensation::Speed::Flywheel;
    // without a cadence the trainer's speed stands in for the reference gear
    d.trainerSpeedMps = m_hub->cadenceReported() ? 0.0 : m_hub->speedMps();
    d.bikeSpeedMps = m_state == State::Running ? m_speed : -1.0;
    return d;
}

double RlvSession::controlCadence() const
{
    const int raw = m_hub->cadence();
    if (raw < kCoastCadenceRpm)
        return raw;   // coasting (or no cadence)
    // smoothed during a ride, else the live cadence
    return active() ? std::max(m_cadSmooth, double(kCoastCadenceRpm)) : double(raw);
}

int RlvSession::gearTargetWatts() const
{
    if (!virtualGears() || path() == RideResistance::Path::GearHub)
        return 0;   // Zwift protocol: the trainer computes the power itself
    const double cad = active() ? m_cadSmooth : double(m_hub->cadence());
    return RideResistance::gearWatts(gearDrive(cad));
}

int RlvSession::desiredGearWatts() const
{
    return RideResistance::gearWatts(gearDrive(controlCadence()));
}

bool RlvSession::sendResistance(bool force)
{
    switch (path()) {
    case RideResistance::Path::Erg:
        return m_res.sendErg(ergTargetNow(), force);
    case RideResistance::Path::Simulation:
        return m_res.sendGrade(simulationGradeToSend(), force, RideResistance::trainerModel(gearDrive(0)));
    case RideResistance::Path::GearSimulation: {
        const RideResistance::GearDrive d = gearDrive(controlCadence());
        return m_res.sendGrade(RideResistance::gearGrade(d, kMinSimGrade, kMaxSimGrade), force,
                               RideResistance::trainerModel(d));
    }
    case RideResistance::Path::GearErg:
        // with engage/release hysteresis on the live cadence
        return m_res.sendGear(gearDrive(controlCadence()), force, m_hub->cadence());
    case RideResistance::Path::GearHybrid:
        return m_res.sendHybrid(gearDrive(controlCadence()), force, m_hub->cadence(), m_hub->cadenceReported(),
                                kMinSimGrade, kMaxSimGrade);
    case RideResistance::Path::GearHub:
        // Zwift hub protocol: the trainer gets the road + the virtual gear
        return m_res.sendHub(gearDrive(controlCadence()));
    }
    return false;
}

void RlvSession::applyTarget(bool force)
{
    if (!running()) {
        updateHold(force);   // not running: ERG / gears on the open page
        return;
    }
    const bool wasHolding = m_ergHold;
    m_ergHold = false;   // the ride's resistance from here on
    if (sendResistance(force) || wasHolding)
        emit targetChanged();
}

void RlvSession::releaseTrainer()
{
    m_ergHold = false;
    m_res.release();
    emit targetChanged();
}

bool RlvSession::holdBlocked() const
{
    // another session drives the trainer right now (riding). A
    // paused workout / free ride has released it: meanwhile the open Video
    // Ride page holds it (gears, ERG, difficulty react); their resume()
    // takes it back (updateHold() then leaves the trainer alone).
    if (m_training && m_training->running())
        return true;
    if (m_freeRide && m_freeRide->running())
        return true;
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.key() != QLatin1String("freeRide") && it.value() && it.value()())
            return true;   // a row in progress
    }
    return false;
}

bool RlvSession::holdWanted() const
{
    return (ergNow() || virtualGears()) && m_state == State::Idle
           && m_gears->activePage() == pageName() && trainerConnected() && !holdBlocked();
}

void RlvSession::updateHold(bool force)
{
    if (active())
        return;   // running: applyTarget(); paused: released until resume()
    if (holdWanted()) {
        const bool sent = sendResistance(force || !m_ergHold);
        const bool changed = !m_ergHold;
        m_ergHold = true;
        updateTimer();
        if (sent || changed)
            emit targetChanged();
        return;
    }
    if (!m_ergHold)
        return;
    m_ergHold = false;
    updateTimer();
    // release only what is still ours (another page may hold its own target)
    if (!holdBlocked() && m_res.lastCommandIsOurs())
        m_res.release();
    else
        m_res.reset();
    emit targetChanged();
}

void RlvSession::controllerShift(int direction)
{
    if (direction == 0)
        return;
    if (workoutErgActive()) {
        // the workout holds the trainer: harder / easier like Training's +/-
        const int before = intensityPercent();
        adjustIntensity(direction > 0 ? kControllerIntensityStep : -kControllerIntensityStep);
        if (intensityPercent() != before)
            emit shifted(direction);
        return;
    }
    if (m_mode == Mode::Erg) {
        const int before = m_ergTarget;
        adjustTargetWatts(direction > 0 ? kControllerErgStep : -kControllerErgStep);
        if (m_ergTarget != before)
            emit shifted(direction);
        return;
    }
    // simulation: always the gears, never the difficulty
    if (direction > 0)
        m_gears->shiftUp();
    else
        m_gears->shiftDown();
}

// --- control ---------------------------------------------------------------------------------

void RlvSession::clearRecording()
{
    m_samples.clear();
    m_recordedSec = 0;
    m_activeMs = 0;
    m_energyJ = 0;
    m_powerSum = 0;
    m_maxWatts = 0;
    clearLaps();
    if (m_ghost)
        m_ghost->reset();   // (back at the start: the ghost and the pacer too)
    if (m_pacer)
        m_pacer->reset();
    emit tick();
}

void RlvSession::requestSeek()
{
    m_seekAtMs = m_nowMs;
    m_reportedMs = -1;
    m_drift = 0;
    m_holding = false;
    emit seekRequested(videoPosition());
}

bool RlvSession::start()
{
    if (active())
        return false;
    if (!hasRoute()) {
        emit startRefused(QStringLiteral("Choose a %1 first.").arg(routeWhat()));
        return false;
    }
    if (startBlocked()) {
        emit startRefused(QStringLiteral("Another session is running. Stop it before starting a %1.").arg(rideWhat()));
        return false;
    }
    abandonOthers();
    m_gears->reloadIfPending();
    m_gears->rideStarting(QStringLiteral("%1 started").arg(rideWhat()));
    if (m_state == State::Finished || m_distance >= totalDistanceM())
        m_distance = m_startDistance;
    m_rideProfile = m_profiles->current();
    m_loopRide = isGpx() && m_loop;   // (fixed for the ride)
    const RoutePacer::Batch once(m_pacer);   // (reset, context and begin: simulated once)
    clearRecording();
    if (m_ghost) {   // the ghost of this rider, start and loop; recording this ride
        updateGhostContext();
        m_ghost->begin();
        m_pacer->begin();   // (simulated now: weight, difficulty, loop as they are)
    }
    loadBests(m_rideProfile.id);
    resetSegments();
    m_completed = false;
    resetLastRideId();
    m_startedAt = QDateTime::currentDateTime();
    m_cadSmooth = m_hub->cadence();
    m_speed = 0;
    m_res.reset();
    if (m_hasWorkout) {
        // a new ride: the workout from 0:00 at the rider's FTP (intensity kept)
        const int intensity = m_workout.intensityOffset();
        m_workout.reset();
        m_workout.setIntensityOffset(intensity);
        m_workout.setFtp(m_rideProfile.ftpFor(sport::cycling()));
        rebuildWorkoutIntervals();
        m_workoutFinished = false;
        m_complianceSec = m_compliantSec = 0;
        m_workout.start();
        if (m_workout.workout().id >= 0)
            m_db->incrementWorkoutUsage(m_workout.workout().id);
    }
    setState(State::Running);
    if (m_hasWorkout)
        emitWorkoutAll();
    applyTarget(true);
    emit positionChanged();
    updateInformation();
    requestSeek();
    updateVideo(0);
    emit started();
    emit metricsChanged();
    return true;
}

void RlvSession::pause()
{
    if (!running())
        return;
    if (!m_manualClock)
        process(sessionClockStep(m_clock.restart()));
    if (!running())
        return;   // the route ended in that step
    m_workout.pause();
    voidSegments();   // a sprint / KOM must be ridden in one go
    setState(State::Paused);
    releaseTrainer();
    m_shouldPlay = false;
    emit videoChanged();
}

void RlvSession::resume()
{
    if (!paused())
        return;
    if (startBlocked()) {
        emit startRefused(QStringLiteral("Another session is running."));
        return;
    }
    m_workout.resume();
    setState(State::Running);
    m_clock.restart();
    m_res.reset();
    applyTarget(true);
    updateVideo(0);
}

void RlvSession::togglePause()
{
    if (running())
        pause();
    else if (paused())
        resume();
    else
        start();
}

qint64 RlvSession::finish(bool save)
{
    if (!active())
        return -1;
    // the last time slice only when the ride is kept: its step may reach the
    // route end, which finishes and saves by itself - not for a discard
    if (save && running() && !m_manualClock && (!m_completed || m_loopRide))
        process(sessionClockStep(m_clock.restart()));
    if (!active())
        return m_lastRideId;   // finished at the route end inside process()
    releaseTrainer();
    m_shouldPlay = false;
    emit videoChanged();
    voidSegments();
    setState(State::Finished);
    const qint64 id = save ? saveRide() : -1;
    if (m_ghost) {   // this ride's trace: a ghost for the next ride (with the ride only)
        if (id >= 0)
            m_ghost->saveRecording(id, m_completed);
        else
            m_ghost->discardRecording();
        // (both free again together: one rival at a time holds after the ride too)
        m_pacer->end();
    }
    if (!save) {
        discardSegments();          // a discarded ride's sprint / KOM times go with it
        emit lastRideIdChanged();   // discarded: stays -1
    }
    m_gears->flushSettings();
    emit finished();
    m_gears->reloadIfPending();
    return id;
}

void RlvSession::reset()
{
    if (active())
        return;
    const RoutePacer::Batch once(m_pacer);   // (reset and context: simulated once)
    clearRecording();
    m_speed = 0;
    m_completed = false;
    m_distance = m_startDistance;
    resetLastRideId();
    resetSegments();
    if (m_hasWorkout) {
        const int intensity = m_workout.intensityOffset();
        m_workout.reset();
        m_workout.setIntensityOffset(intensity);
        m_workoutFinished = false;
        m_complianceSec = m_compliantSec = 0;
        emitWorkoutAll();
        emit modeChanged();   // workout ERG again (resistancePath)
    }
    setState(State::Idle);
    updateGhostContext();   // (the current rider and loop setting again, no longer the finished ride's)
    emit positionChanged();
    updateInformation();
    requestSeek();
    emit metricsChanged();
}

bool RlvSession::seekToDistance(double meters)
{
    if (!hasRoute() || active() || !std::isfinite(meters))
        return false;
    const double total = totalDistanceM();
    meters = std::clamp(meters, 0.0, std::max(0.0, total - 1.0));
    m_startDistance = m_distance = meters;
    const RoutePacer::Batch once(m_pacer);   // (reset and context: simulated once)
    if (m_state == State::Finished) {
        clearRecording();
        m_completed = false;
        setState(State::Idle);
    }
    resetSegments();
    updateGhostContext();   // (the ghosts ridden from this start)
    emit positionChanged();
    updateInformation();
    requestSeek();
    return true;
}

void RlvSession::reportVideoPosition(qint64 ms)
{
    if (ms < 0)
        return;
    m_reportedMs = ms;
    m_reportAtMs = m_nowMs;
}

// --- clock ----------------------------------------------------------------------------------

void RlvSession::advanceClock(int ms)
{
    process(ms);
}

void RlvSession::advanceWallClock(qint64 elapsedMs)
{
    process(sessionClockStep(elapsedMs));
}

void RlvSession::onTimer()
{
    process(sessionClockStep(m_clock.restart()));
}

void RlvSession::integratePower(double watts, double dt)
{
    // m·dv/dt = P·η/v − F(v): the kinetic energy follows the rider's power,
    // so the speed changes smoothly and settles at road::speedForPower().
    const road::Model model = roadModel(rideGradePercent());
    m_speed = road::integrateSpeed(model, m_speed, watts, dt, kRotatingMassKg, kMinDriveSpeedMps, kIntegrationStepSec);
    if (m_speed < 0.3 && watts <= 0 && model.gradePercent >= 0)
        m_speed = 0;
}

void RlvSession::updateModel(double dt)
{
    const int cad = m_hub->cadence();
    const int power = m_hub->power();
    m_cadSmooth += (cad - m_cadSmooth) * std::min(1.0, dt / kCadenceSmoothingSec);

    // sim / ERG: speed from power; Zwift protocol: the trainer brakes with its
    // own physics (incline + virtual gear), the video follows the power
    if (!virtualGears() || path() == RideResistance::Path::GearHub) {
        integratePower(power >= kPedalPowerWatts || m_speed > 0 ? power : 0, dt);
        if (path() == RideResistance::Path::GearHub) {
            // light pedaling / coasting / stopped: RideResistance::hubSpeedStep
            RideResistance::HubSpeedInput in;
            in.trainerMps = m_gears->hubTrainerSpeedMps();
            in.pedaling = pedaling();
            in.lowPower = power < kPedalPowerWatts;
            in.flywheelStopped = m_gears->hubFlywheelStopped();
            in.gradePercent = rideGradePercent();
            in.dt = dt;
            m_speed = RideResistance::hubSpeedStep(m_speed, in);
        }
        return;
    }
    const road::Model model = roadModel(rideGradePercent());
    const double roll = std::clamp(m_speed - road::coastDeceleration(model, m_speed) * dt, 0.0, road::kMaxSpeedMps);
    if (cad >= kCoastCadenceRpm) {
        const double pedal = gearing().virtualSpeed(cad);
        // downhill the bike may roll faster than the pedals turn it
        m_speed = model.gradePercent < 0 ? std::max(pedal, roll) : pedal;
    } else if (power >= kPedalPowerWatts && cad <= 0) {
        integratePower(power, dt);   // trainer without cadence
    } else {
        m_speed = roll;
        if (m_speed < 0.3 && model.gradePercent >= 0)
            m_speed = 0;
    }
}

void RlvSession::process(int ms)
{
    m_hub->expireStale();
    if (ms <= 0)
        return;
    m_nowMs += ms;
    if (!active()) {
        if (m_ergHold) {   // idle page holding the virtual gear: follow the cadence
            m_res.elapse(ms);
            updateHold();
            emit metricsChanged();
        }
        return;
    }
    if (paused())
        return;
    const double dt = ms / 1000.0;
    updateModel(dt);
    if (running()) {
        const double d0 = m_distance;
        const qint64 t0 = m_activeMs;
        m_activeMs += ms;
        const double total = totalDistanceM();
        advance(m_speed * dt, d0, t0);
        if (m_ghost) {
            m_ghost->advance(m_activeMs / 1000.0, m_ridden);
            m_pacer->advance(m_activeMs / 1000.0, m_ridden);
        }
        m_res.elapse(ms);
        recordUpTo(int(m_activeMs / 1000));
        advanceWorkout(ms);
        emit positionChanged();
        updateInformation();
        updateVideo(ms);
        applyTarget();
        emit tick();
        if (!m_loopRide && m_distance >= total) {
            m_completed = true;
            emit metricsChanged();
            finish(true);   // route end: save automatically
            return;
        }
    }
    emit metricsChanged();
}

void RlvSession::updateVideo(int)
{
    const double oldRate = m_rate;
    const bool oldPlay = m_shouldPlay;
    const double oldDrift = m_drift;
    if (!hasRoute() || !running()) {
        m_shouldPlay = false;
        m_holding = false;
        if (oldPlay)
            emit videoChanged();
        return;
    }
    const double target = m_route.videoTimeAtDistance(m_distance);
    const double filmed = m_route.recordedSpeedAtDistance(m_distance);
    const double base = filmed > 0.3 ? m_speed / filmed : (m_speed > 0.3 ? kMaxRate : 0.0);
    const bool moving = speedKmh() >= kMinPlaySpeedKmh;
    const bool slow = base <= kMinRate;

    // drift from the last report, extrapolated to now
    m_drift = 0;
    const bool fresh = m_reportedMs >= 0 && m_nowMs - m_reportAtMs <= kReportMaxAgeMs
                       && m_nowMs - m_seekAtMs >= kSeekCooldownMs;
    if (fresh) {
        const double age = (m_nowMs - m_reportAtMs) / 1000.0;
        const double actual = m_reportedMs / 1000.0 + (oldPlay ? age * m_rate : 0.0);
        m_drift = actual - target;
        const bool holdInstead = m_drift > 0 && moving && slow;
        if (std::abs(m_drift) > kSeekThresholdSec && !holdInstead) {
            requestSeek();   // resets drift / hold
        }
    }

    // video ahead while the rider is slower than the slowest rate: wait
    if (!moving)
        m_holding = false;
    else if (slow && m_drift > kHoldAheadSec)
        m_holding = true;
    else if (m_holding && (m_drift <= 0 || !slow))
        m_holding = false;
    m_shouldPlay = moving && !m_holding;

    if (moving) {
        const double corr = std::clamp(m_drift * kRateCorrectionPerSec, -kMaxRateCorrection, kMaxRateCorrection);
        double rate = std::clamp(base * (1.0 - corr), kMinRate, kMaxRate);
        rate = std::clamp(std::round(rate / kRateQuantum) * kRateQuantum, kMinRate, kMaxRate);
        const double delta = std::abs(rate - m_rate);
        const bool due = m_nowMs - m_rateAtMs >= kRateMinIntervalMs;
        if (!oldPlay || delta >= kRateBigChange - 1e-9 || (due && delta >= kRateMinChange - 1e-9)) {
            if (delta > 1e-9)
                m_rateAtMs = m_nowMs;
            m_rate = rate;
        }
    }
    if (!qFuzzyCompare(oldRate, m_rate) || oldPlay != m_shouldPlay || std::abs(oldDrift - m_drift) >= 0.05)
        emit videoChanged();
}

qint64 RlvSession::videoPosition() const
{
    return hasRoute() ? qint64(std::llround(m_route.videoTimeAtDistance(m_distance) * 1000.0)) : 0;
}

void RlvSession::recordUpTo(int second)
{
    while (m_recordedSec < second) {
        Sample s;
        s.t = m_recordedSec;
        s.power = m_hub->power();
        s.cadence = m_hub->cadence();
        s.heartRate = m_hub->heartRate();
        // the workout's target while it runs (ERG or guidance), else the RLV ERG target
        const bool workoutRunning = m_hasWorkout && !m_workoutFinished
                                    && m_workout.state() == WorkoutEngine::State::Running;
        s.targetPower = workoutRunning ? workoutTargetWatts() : (m_mode == Mode::Erg ? m_res.lastWatts() : 0);
        if (workoutRunning && s.targetPower > 0) {
            ++m_complianceSec;
            if (std::abs(s.power - s.targetPower) <= s.targetPower * kComplianceTolerance + 1e-9)
                ++m_compliantSec;
        }
        s.speedMps = m_speed;
        m_samples.append(s);
        m_energyJ += s.power;
        m_powerSum += s.power;
        m_maxWatts = std::max(m_maxWatts, s.power);
        ++m_recordedSec;
    }
}

qint64 RlvSession::saveRide()
{
    if (m_samples.size() < m_minSaveSec)
        return -1;
    const Profile &p = m_rideProfile;
    RideRecord r;
    r.profileId = p.id;
    r.workoutId = m_hasWorkout ? m_workout.workout().id : -1;
    const QString prefix = isGpx() ? QStringLiteral("GPX") : QStringLiteral("RLV");
    r.workoutName = m_hasWorkout ? QStringLiteral("%1: %2 + %3").arg(prefix, m_route.name(), m_workout.workout().name)
                                 : QStringLiteral("%1: %2").arg(prefix, m_route.name());
    r.startedAt = m_startedAt;
    r.blocks = m_hasWorkout ? int(m_workout.workout().intervals.size()) : 0;
    r.ergMode = m_mode == Mode::Erg || (m_hasWorkout && m_workoutErg);
    r.sport = sport::cycling();
    r.ftp = p.ftpFor(r.sport);
    r.distanceM = riddenMeters();
    analysis::applyStats(r, analysis::analyze(m_samples, r.ftp, p.zones));
    m_lastRideId = m_db->saveRide(r, m_samples);
    for (const TimedSegment &seg : m_segments)
        if (m_lastRideId >= 0 && seg.recordId >= 0)
            m_db->setSegmentRideId(seg.recordId, m_lastRideId);
    for (qint64 id : std::as_const(m_lapRecordIds))   // (earlier laps)
        if (m_lastRideId >= 0)
            m_db->setSegmentRideId(id, m_lastRideId);
    if (m_lastRideId >= 0 && m_loopRide && !m_lapTimes.isEmpty()) {   // the lap times (History)
        QVector<RideLap> laps;
        for (int i = 0; i < m_lapTimes.size(); ++i)
            laps.append({ i + 1, m_lapTimes.at(i).toDouble(), totalDistanceM() - (i == 0 ? m_startDistance : 0.0) });   // (lap 1 from the start point)
        m_db->saveRideLaps(m_lastRideId, laps);
    }
    if (m_lastRideId >= 0) {
        emit lastRideIdChanged();
        emit rideSaved(m_lastRideId);
    }
    return m_lastRideId;
}

// --- sprints and KOMs ------------------------------------------------------------

void RlvSession::segmentsRouteChanged()
{
    m_segments.clear();
    m_segmentKey.clear();
    // the track's fingerprint: the ghost's route (and the sprints' / KOMs')
    m_routeKey = isGpx() && hasRoute() ? sprint::routeKey(m_route) : QString();
    if (isGpx() && hasRoute() && !m_route.segments().isEmpty()) {
        m_segmentKey = m_routeKey;
        int numbers[2] = { 0, 0 };
        for (const RouteSegment &r : m_route.segments()) {
            TimedSegment t;
            t.kindId = r.kind;
            t.kind = r.kindName();
            t.name = r.displayName(++numbers[r.kind]);
            t.start = r.start;
            t.finish = std::min(r.finish, totalDistanceM());   // (validate allows +0.5 m: the rider stops at the end)
            t.key = m_segmentKey;
            m_segments.append(t);
        }
    }
    loadBests(m_profiles->profileId());
    resetSegments();
    updateGhostContext();
}

void RlvSession::loadBest(TimedSegment &seg, qint64 profileId)
{
    seg.best = 0;
    seg.attempts = 0;
    if (!seg.valid() || profileId < 0)
        return;
    if (const auto best = m_db->bestSegment(profileId, seg.kind, seg.key, seg.start, seg.length()))
        seg.best = best->timeSec;
    seg.attempts = m_db->segmentAttempts(profileId, seg.kind, seg.key, seg.start, seg.length());
}

void RlvSession::loadBests(qint64 profileId)
{
    for (TimedSegment &seg : m_segments)
        loadBest(seg, profileId);
}

QVariantList RlvSession::segmentMarkers() const
{
    QVariantList out;
    for (const TimedSegment &t : m_segments)
        out.append(QVariantMap { { QStringLiteral("kind"), t.kind }, { QStringLiteral("start"), t.start },
                                 { QStringLiteral("finish"), t.finish }, { QStringLiteral("name"), t.name } });
    return out;
}

QVariantList RlvSession::gravelMarkers() const
{
    QVariantList out;
    for (const RouteSegment &g : routeGravel())
        out.append(QVariantMap { { QStringLiteral("start"), g.start }, { QStringLiteral("finish"), g.finish },
                                 { QStringLiteral("name"), g.name } });
    return out;
}

int RlvSession::countOf(RouteSegment::Kind kind) const
{
    return int(std::count_if(m_segments.begin(), m_segments.end(), [kind](const TimedSegment &t) { return t.kindId == kind; }));
}

int RlvSession::focusIndex(RouteSegment::Kind kind) const
{
    int next = -1, last = -1;
    for (int i = 0; i < m_segments.size(); ++i) {
        const TimedSegment &t = m_segments.at(i);
        if (t.kindId != kind)
            continue;
        if (t.phase == SegmentPhase::Running)
            return i;
        if (m_distance < t.start) {
            if (next < 0)
                next = i;
        } else {
            last = i;
        }
    }
    const int k = kind;
    const int finished = m_lastFinished[k];
    if (finished >= 0 && m_activeMs - m_lastFinishedMs[k] < kResultShowMs) {
        const double approach = kind == RouteSegment::Kom ? 500.0 : 400.0;
        if (next < 0 || m_segments.at(next).start - m_distance > approach)
            return finished;
    }
    return next >= 0 ? next : last;
}

const RlvSession::TimedSegment &RlvSession::focusSeg(RouteSegment::Kind kind) const
{
    const int i = focusIndex(kind);
    return i >= 0 ? m_segments.at(i) : m_noSegment;
}

double RlvSession::komGainM() const
{
    const TimedSegment &t = focusSeg(RouteSegment::Kom);
    if (!t.valid())
        return 0.0;
    return m_route.elevationAtDistance(t.finish) - m_route.elevationAtDistance(t.start);
}

void RlvSession::emitSegments(bool sprints, bool koms)
{
    if (sprints) {
        m_focusSeen[RouteSegment::Sprint] = focusIndex(RouteSegment::Sprint);
        emit sprintChanged();
    }
    if (koms) {
        m_focusSeen[RouteSegment::Kom] = focusIndex(RouteSegment::Kom);
        emit komChanged();
    }
}

void RlvSession::resetTimed(TimedSegment &seg)
{
    seg.phase = SegmentPhase::Idle;
    seg.startMs = 0;
    seg.time = 0;
    seg.previousBest = seg.best;
    seg.newBest = false;
    seg.recordId = -1;
}

void RlvSession::resetSegments()
{
    for (TimedSegment &seg : m_segments)
        resetTimed(seg);
    m_lastFinished[0] = m_lastFinished[1] = -1;
    emitSegments(true, true);
}

void RlvSession::voidSegments()
{
    bool sprints = false, koms = false;
    for (TimedSegment &seg : m_segments)
        if (seg.phase == SegmentPhase::Running) {
            seg.phase = SegmentPhase::Void;
            (seg.kindId == RouteSegment::Kom ? koms : sprints) = true;
        }
    emitSegments(sprints, koms);
}

void RlvSession::discardSegments()
{
    bool sprints = false, koms = false;
    for (qint64 id : std::as_const(m_lapRecordIds))   // (earlier laps)
        m_db->deleteSegmentRecord(id);
    m_lapRecordIds.clear();
    for (TimedSegment &seg : m_segments) {
        if (seg.recordId < 0)
            continue;
        m_db->deleteSegmentRecord(seg.recordId);
        seg.recordId = -1;
        seg.phase = SegmentPhase::Void;
        seg.time = 0;
        seg.newBest = false;
        loadBest(seg, m_rideProfile.id);
        seg.previousBest = seg.best;
        (seg.kindId == RouteSegment::Kom ? koms : sprints) = true;
    }
    emitSegments(sprints, koms);
}

QString RlvSession::timedState(const TimedSegment &seg) const
{
    if (!seg.valid())
        return QStringLiteral("none");
    switch (seg.phase) {
    case SegmentPhase::Running: return QStringLiteral("running");
    case SegmentPhase::Done: return QStringLiteral("done");
    case SegmentPhase::Void: return QStringLiteral("void");
    case SegmentPhase::Idle: break;
    }
    // (the start line is crossed coming from before it: a ride starting on it is too late)
    return m_distance < seg.start && !m_returning ? QStringLiteral("ahead") : QStringLiteral("passed");
}

double RlvSession::timedElapsed(const TimedSegment &seg) const
{
    if (seg.phase == SegmentPhase::Done)
        return seg.time;
    if (seg.phase != SegmentPhase::Running)
        return 0.0;
    return std::max(0.0, (double(m_activeMs) - seg.startMs) / 1000.0);
}

bool RlvSession::stepTimed(TimedSegment &seg, double d0, double t0, double t1, bool &finished)
{
    finished = false;
    if (!seg.valid())
        return false;
    const double d1 = m_distance;
    if (seg.rearm && seg.phase != SegmentPhase::Running) {
        // a new lap: the last lap's result stays until its start line comes again
        if (!std::isfinite(sprint::crossingMs(d0, d1, double(t0), t1, seg.start)))
            return false;
        if (seg.recordId >= 0)
            m_lapRecordIds.append(seg.recordId);
        resetTimed(seg);
        seg.rearm = false;
    }
    if (seg.phase == SegmentPhase::Idle) {
        const double at = sprint::crossingMs(d0, d1, double(t0), t1, seg.start);
        if (!std::isfinite(at))
            return false;
        seg.phase = SegmentPhase::Running;
        seg.startMs = at;
    }
    if (seg.phase != SegmentPhase::Running)
        return false;
    const double end = sprint::crossingMs(d0, d1, double(t0), t1, seg.finish);
    if (!std::isfinite(end))
        return true;   // the running time
    const double time = std::round((end - seg.startMs) / 10.0) / 100.0;   // 0.01 s
    seg.phase = SegmentPhase::Done;
    seg.time = time;
    seg.previousBest = seg.best;
    seg.newBest = seg.best <= 0 || time < seg.best;
    SegmentRecord r;
    r.profileId = m_rideProfile.id;
    r.kind = seg.kind;
    r.routeKey = seg.key;
    r.routeName = m_route.name();
    r.startM = seg.start;
    r.lengthM = seg.length();
    r.timeSec = time;
    r.createdAt = QDateTime::currentDateTime();
    seg.recordId = r.profileId >= 0 ? m_db->saveSegmentRecord(r) : -1;
    if (seg.recordId >= 0)
        ++seg.attempts;
    if (seg.newBest)
        seg.best = time;
    finished = true;
    return true;
}

void RlvSession::updateSegments(double d0, qint64 t0)
{
    updateSegments(d0, double(t0), double(m_activeMs));
}

void RlvSession::updateSegments(double d0, double t0, double t1)
{
    bool changed[2] = { false, false };
    int finishedIndex[2] = { -1, -1 };
    for (int i = 0; i < m_segments.size(); ++i) {
        TimedSegment &seg = m_segments[i];
        bool finished = false;
        if (stepTimed(seg, d0, t0, t1, finished)) {
            changed[seg.kindId] = true;
            if (finished) {
                finishedIndex[seg.kindId] = i;
                m_lastFinished[seg.kindId] = i;
                m_lastFinishedMs[seg.kindId] = m_activeMs;
            }
        }
    }
    // the focus moves on (a line passed, a result shown long enough)
    for (int k = 0; k < 2; ++k)
        if (focusIndex(RouteSegment::Kind(k)) != m_focusSeen[k])
            changed[k] = true;
    emitSegments(changed[RouteSegment::Sprint], changed[RouteSegment::Kom]);
    if (finishedIndex[RouteSegment::Sprint] >= 0) {
        const TimedSegment &s = m_segments.at(finishedIndex[RouteSegment::Sprint]);
        emit sprintFinished(s.time, s.previousBest, s.newBest);
    }
    if (finishedIndex[RouteSegment::Kom] >= 0) {
        const TimedSegment &s = m_segments.at(finishedIndex[RouteSegment::Kom]);
        emit komFinished(s.time, s.previousBest, s.newBest);
    }
}

// --- workout during the ride ------------------------------------------------------

bool RlvSession::attachWorkout(qint64 id)
{
    if (active()) {
        setError(QStringLiteral("Stop the ride before choosing a workout"));
        return false;
    }
    const auto w = m_db->workout(id);
    if (!w) {
        setError(QStringLiteral("Workout not found"));
        return false;
    }
    return attachWorkout(*w);
}

bool RlvSession::attachWorkout(const Workout &w)
{
    if (active()) {
        setError(QStringLiteral("Stop the ride before choosing a workout"));
        return false;
    }
    if (!w.isCycling() || w.intervals.isEmpty() || w.totalDurationSec() <= 0) {
        setError(QStringLiteral("\"%1\" cannot be ridden in a %2 (cycling workouts with intervals only)")
                     .arg(w.name, rideWhat()));
        return false;
    }
    m_workout.load(w, m_profiles->current().ftpFor(sport::cycling()));   // intensity 0, from 0:00
    m_hasWorkout = true;
    m_workoutErg = true;
    m_workoutFinished = false;
    m_complianceSec = m_compliantSec = 0;
    rebuildWorkoutIntervals();
    setError(QString());
    emitWorkoutAll();
    workoutResistanceChanged();
    return true;
}

void RlvSession::detachWorkout()
{
    if (!m_hasWorkout)
        return;
    m_hasWorkout = false;
    m_workoutFinished = false;
    m_workout = WorkoutEngine();
    m_workoutIntervals.clear();
    m_complianceSec = m_compliantSec = 0;
    emitWorkoutAll();
    workoutResistanceChanged();
}

void RlvSession::rebuildWorkoutIntervals()
{
    m_workoutIntervals.clear();
    if (!m_hasWorkout)
        return;
    const int ftp = m_workout.ftp();
    int start = 0;
    for (const Interval &i : m_workout.workout().intervals) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), i.name);
        m.insert(QStringLiteral("startSec"), start);
        m.insert(QStringLiteral("durationSec"), i.durationSec);
        m.insert(QStringLiteral("watts"), i.wattsAt(0, ftp));
        m.insert(QStringLiteral("endWatts"), i.wattsAt(i.durationSec, ftp));
        m.insert(QStringLiteral("percent"), int(std::lround(i.averagePercent() * 100.0)));
        m.insert(QStringLiteral("ramp"), i.isRamp());
        m_workoutIntervals.append(m);
        start += i.durationSec;
    }
}

void RlvSession::emitWorkoutAll()
{
    m_workoutLastIndex = currentIntervalIndex();
    m_workoutLastTarget = workoutTargetWatts();
    emit workoutChanged();
    emit workoutIntervalChanged();
    emit workoutTargetChanged();
    emit workoutErgChanged();
    emit workoutTick();
}

void RlvSession::workoutResistanceChanged()
{
    emit modeChanged();       // virtualGears / simulationFallback / resistancePath
    emit positionChanged();   // effectiveGradePercent
    emit targetChanged();     // ergHolding / gearHolding
    emit metricsChanged();
    applyTarget(true);        // running: the ride's resistance; else the page hold
}

void RlvSession::advanceWorkout(int ms)
{
    if (!m_hasWorkout || m_workoutFinished || m_workout.state() != WorkoutEngine::State::Running)
        return;
    m_workout.advance(ms);
    const int idx = currentIntervalIndex();
    if (idx != m_workoutLastIndex) {
        m_workoutLastIndex = idx;
        emit workoutIntervalChanged();
    }
    const int target = workoutTargetWatts();
    if (target != m_workoutLastTarget) {
        m_workoutLastTarget = target;
        emit workoutTargetChanged();
    }
    emit workoutTick();
    if (m_workout.state() == WorkoutEngine::State::Finished) {
        // done before the route: release the workout ERG, the selected mode
        // takes over (applyTarget() in workoutResistanceChanged())
        m_workoutFinished = true;
        m_workoutLastTarget = 0;
        emit workoutTargetChanged();
        emit workoutErgChanged();
        workoutResistanceChanged();
        emit workoutCompleted();
    }
}

void RlvSession::skipInterval()
{
    if (!active() || !m_hasWorkout || m_workoutFinished)
        return;
    m_workout.skipInterval();
    if (m_workout.state() == WorkoutEngine::State::Finished) {
        // skipped past the last interval: over, but not completed
        m_workoutFinished = true;
        emitWorkoutAll();
        workoutResistanceChanged();
        return;
    }
    emitWorkoutAll();
    if (workoutErgActive())
        applyTarget(true);
}

QString RlvSession::currentIntervalName() const
{
    const int idx = currentIntervalIndex();
    return idx >= 0 && idx < m_workout.workout().intervals.size() ? m_workout.workout().intervals.at(idx).name
                                                                 : QString();
}

int RlvSession::workoutTargetWatts() const
{
    return m_hasWorkout && !m_workoutFinished ? m_workout.targetWatts() : 0;
}

int RlvSession::nextTargetWatts() const
{
    return m_hasWorkout && !m_workoutFinished ? m_workout.nextTargetWatts() : 0;
}

int RlvSession::workoutCompliance() const
{
    return m_complianceSec > 0 ? int(std::lround(100.0 * m_compliantSec / m_complianceSec)) : 0;
}

void RlvSession::setWorkoutErg(bool on)
{
    if (on == m_workoutErg)
        return;
    m_workoutErg = on;
    emit workoutErgChanged();
    if (m_hasWorkout)
        workoutResistanceChanged();
}

void RlvSession::setIntensityPercent(int percent)
{
    percent = std::clamp(percent, int(kMinIntensity), int(kMaxIntensity));
    if (percent == m_workout.intensityOffset())
        return;
    m_workout.setIntensityOffset(percent);
    m_workoutLastTarget = workoutTargetWatts();
    emit workoutTargetChanged();
    if (workoutErgActive()) {
        emit targetChanged();
        applyTarget(true);
    }
}

} // namespace watt

namespace watt {

void RlvSession::setLive(LiveRide *live)
{
    m_live = live;
    if (!m_live)
        return;
    // what this station tells the others: its place on the route while riding
    m_live->setSource([this] {
        LiveRide::Me me;
        me.route = isGpx() && hasRoute();
        me.routeKey = m_routeKey;
        me.riding = active();
        me.distanceM = distanceMeters();
        me.returning = returning();
        me.speedKmh = running() ? speedKmh() : 0.0;
        me.watts = running() ? watts() : 0;
        if (m_profiles) {
            me.userUuid = m_profiles->current().uuid;
            me.name = m_profiles->current().name;
            me.look = m_profiles->avatar3d();   // (the others draw this rider in its own figure and colours)
        }
        me.routeLength = hasRoute() ? m_route.totalDistance() : 0.0;
        me.loop = loopRide();
        return me;
    });
    connect(m_live, &LiveRide::changed, this, &RlvSession::liveChanged);
}

} // namespace watt
