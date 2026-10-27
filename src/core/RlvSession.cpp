#include "RlvSession.h"

#include "Database.h"
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
const QString kSettingMode = QStringLiteral("rlv/mode");
const QString kSettingDifficulty = QStringLiteral("rlv/difficulty");
const QString kSettingErgWatts = QStringLiteral("rlv/ergWatts");
const QString kSettingLastRoute = QStringLiteral("rlv/lastRoute");
const QString kRefused = QStringLiteral("Another session is running. Stop it before starting a video ride.");
} // namespace

RlvSession::RlvSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears, RlvLibrary *library,
                       QObject *parent)
    : QObject(parent)
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

    connect(m_hub, &SensorHub::powerChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::displayPowerChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::cadenceChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::heartRateChanged, this, &RlvSession::metricsChanged);
    connect(m_hub, &SensorHub::powerChanged, this, &RlvSession::checkPedaling);
    connect(m_hub, &SensorHub::cadenceChanged, this, &RlvSession::checkPedaling);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, &RlvSession::devicesChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, [this] {
        if (running() && trainerConnected())
            applyTarget(true);
        else
            updateHold(true);   // ERG selected on the open page: hold it now
    });
    // ERG outside a ride follows the open page (Main.qml: Gears.activePage).
    connect(m_gears, &VirtualGears::activePageChanged, this, [this] {
        // armed ("start when pedaling") only while its page is open: it must
        // not start unseen or keep blocking other sessions
        if (waiting() && m_gears->activePage() != QLatin1String("video")) {
            finish(false);
            return;
        }
        updateHold();
    });
    connect(m_hub, &SensorHub::trainerSimulationSupportChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::trainerReportsSpeedChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::cadenceReportedChanged, this, &RlvSession::resistanceSetupChanged);
    connect(m_gears, &VirtualGears::enabledChanged, this, &RlvSession::resistanceSetupChanged);
    auto gearChanged = [this] {
        emit targetChanged();
        emit metricsChanged();
        if (virtualGears())
            applyTarget(true);   // new gear / bike -> new resistance right away (ride or page hold)
    };
    connect(m_gears, &VirtualGears::gearChanged, this, gearChanged);
    connect(m_gears, &VirtualGears::gearSetupChanged, this, gearChanged);
    if (m_library) {
        connect(m_library, &RlvLibrary::scanned, this, [this] { emit routeChanged(); });   // routeIndex
    }
    // the attached workout's watts follow the rider's FTP (not during a ride:
    // the ride keeps the FTP of its start)
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (!m_hasWorkout || active())
            return;
        m_workout.setFtp(m_profiles->current().ftpFor(sport::cycling()));
        rebuildWorkoutIntervals();
        emit workoutChanged();
        emit workoutTargetChanged();
        if (workoutErgActive())
            applyTarget(true);
    });

    const QString storedMode = m_db->setting(kSettingMode, QStringLiteral("sim")).toString();
    m_mode = parseMode(storedMode).value_or(Mode::Sim);
    if (storedMode != mode())
        m_db->setSetting(kSettingMode, mode());   // "power" / "gears" (old) -> "sim"
    const int storedErg = m_db->setting(kSettingErgWatts, 0).toInt();
    m_ergTarget = storedErg > 0 ? std::clamp(storedErg, int(kMinErgWatts), int(kMaxWatts)) : defaultErgTarget();
    m_difficulty = std::clamp(m_db->setting(kSettingDifficulty, kDefaultDifficulty).toInt(), 0, 100);

    const QString last = m_db->setting(kSettingLastRoute).toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) {
        selectPath(last);
        setError(QString());
    }
}

RlvSession::~RlvSession()
{
    if (m_training) {
        m_training->setStartBlocker(QStringLiteral("rlv"), {});
        m_training->setArmedCanceller(QStringLiteral("rlv"), {});
    }
    if (active() || waiting() || m_ergHold)
        m_res.release();
    m_gears->clearRideProfile(this);
}

// --- wiring ------------------------------------------------------------------------

void RlvSession::setTrainingSession(TrainingSession *training)
{
    if (m_training) {
        m_training->setStartBlocker(QStringLiteral("rlv"), {});
        m_training->setArmedCanceller(QStringLiteral("rlv"), {});
    }
    m_training = training;
    if (training) {
        // a ride blocks the workout; an armed one is cancelled by its start
        training->setStartBlocker(QStringLiteral("rlv"), [this] { return active(); });
        training->setArmedCanceller(QStringLiteral("rlv"), [this] {
            if (waiting())
                finish(false);
        });
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

void RlvSession::setArmedCanceller(const QString &key, std::function<void()> cancel)
{
    if (cancel)
        m_armedCancellers.insert(key, std::move(cancel));
    else
        m_armedCancellers.remove(key);
}

void RlvSession::cancelArmedOthers()
{
    if (m_training && m_training->waiting())
        m_training->stop();   // only armed: nothing ridden yet
    if (m_freeRide && m_freeRide->waiting())
        m_freeRide->finish(false);
    for (auto it = m_armedCancellers.cbegin(); it != m_armedCancellers.cend(); ++it) {
        if (it.value())
            it.value()();
    }
}

void RlvSession::syncRideProfile()
{
    if (active() || waiting())
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
    if (m_training && m_training->active())
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
    case State::Ready: return QStringLiteral("ready");
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
    m_state = s;
    m_clock.start();
    updateTimer();
    syncRideProfile();
    emit stateChanged();
    if (!active())
        updateHold();   // idle / armed: ERG on the open page holds the target
}

void RlvSession::updateTimer()
{
    // the clock runs while a ride is armed or running and while the page
    // holds the virtual gear resistance (it follows the cadence) - not while
    // paused, idle or holding a fixed ERG target
    const bool want = !m_manualClock && (m_state == State::Ready || running() || gearHolding());
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
    m_db->setSetting(kSettingMode, mode());
    if (m_mode == Mode::Erg) {
        // start from what the rider is pushing right now
        const int now = m_hub->displayPower();
        if (now >= kStartPowerWatts) {
            // stored here, sent once by applyTarget() below
            m_ergTarget = std::clamp(int(std::lround(now / 5.0)) * 5, int(kMinErgWatts), int(kMaxWatts));
            m_db->setSetting(kSettingErgWatts, m_ergTarget);
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
    m_db->setSetting(kSettingErgWatts, watts);
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
    return RideResistance::choose(ergNow(), virtualGears(), m_hub);
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
    const double g = std::clamp(effectiveGradePercent(), kMinSimGrade, kMaxSimGrade);
    return std::round(g * 100.0) / 100.0;
}

void RlvSession::setDifficulty(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_difficulty)
        return;
    m_difficulty = percent;
    m_db->setSetting(kSettingDifficulty, percent);
    emit difficultyChanged();
    emit positionChanged();   // effectiveGradePercent
    emit targetChanged();
    applyTarget(true);
}

// --- route -------------------------------------------------------------------------------

bool RlvSession::canSelect() const
{
    return !active() && !waiting();
}

int RlvSession::routeIndex() const
{
    return m_library && hasRoute() ? m_library->indexOfPath(m_route.sourcePath()) : -1;
}

bool RlvSession::select(int row)
{
    if (!m_library || row < 0 || row >= m_library->count()) {
        setError(QStringLiteral("No such video"));
        return false;
    }
    return selectPath(m_library->pathAt(row));
}

bool RlvSession::selectPath(const QString &pathIn)
{
    if (!canSelect()) {
        setError(QStringLiteral("Stop the current ride before choosing another video"));
        return false;
    }
    const QString path = RlvLibrary::toLocalPath(pathIn);
    QString error;
    RlvRoute route;
    if (!route.load(path, &error)) {
        setError(error);
        return false;
    }
    m_route = std::move(route);
    setError(m_route.hasVideo() ? QString() : error);
    m_db->setSetting(kSettingLastRoute, m_route.sourcePath());
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
    emit routeChanged();
    emit positionChanged();
    updateInformation();
}

QUrl RlvSession::videoUrl() const
{
    return m_route.hasVideo() ? QUrl::fromLocalFile(m_route.videoPath()) : QUrl();
}

int RlvSession::routeDurationSec() const
{
    return hasRoute() ? int(std::lround(m_route.videoDuration() - m_route.videoOffset())) : 0;
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

double RlvSession::gradePercent() const
{
    return hasRoute() ? m_route.gradeAtDistance(m_distance) : 0.0;
}

double RlvSession::effectiveGradePercent() const
{
    // ERG: the trainer holds the watts, the difficulty only applies to the
    // resistance modes - the speed uses the real gradient
    return ergNow() ? gradePercent() : gradePercent() * m_difficulty / 100.0;
}

double RlvSession::elevationM() const
{
    return hasRoute() ? m_route.elevationAtDistance(m_distance) : 0.0;
}

double RlvSession::ascentDoneM() const
{
    return hasRoute() ? std::max(0.0, m_route.ascentAtDistance(m_distance) - m_route.ascentAtDistance(m_startDistance))
                      : 0.0;
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

bool RlvSession::coasting() const
{
    return m_hub->cadence() < kCoastCadenceRpm && m_hub->power() < kStartPowerWatts;
}

road::Model RlvSession::roadModel(double grade) const
{
    // the rider of the ride; weight / height edits of the same profile apply
    // at once (like the trainer's weight, SensorHub::riderMassKg)
    const Profile &cur = m_profiles->current();
    const Profile &p = (active() || waiting()) && m_rideProfile.id != cur.id ? m_rideProfile : cur;
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
    if (m_state == State::Ready || active())
        return m_speed;
    const int cad = m_hub->cadence();
    if (virtualGears() && cad >= kCoastCadenceRpm)
        return gearing().virtualSpeed(cad);
    const int p = m_hub->power();
    return p > 0 ? solveSpeedForPower(p, effectiveGradePercent()) : 0.0;
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
    d.road = roadModel(effectiveGradePercent());
    d.cadenceRpm = cadenceRpm;
    d.virtualRatio = gearing().ratio();
    d.referenceRatio = m_gears->referenceRatio();
    d.circumferenceMm = gearing().setup().circumferenceMm;
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
    // smoothed while a ride is armed/running, else the live cadence
    return (m_state == State::Ready || active()) ? std::max(m_cadSmooth, double(kCoastCadenceRpm)) : double(raw);
}

int RlvSession::gearTargetWatts() const
{
    if (!virtualGears())
        return 0;
    const double cad = (m_state == State::Ready || active()) ? m_cadSmooth : double(m_hub->cadence());
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
    }
    return false;
}

void RlvSession::applyTarget(bool force)
{
    if (!running()) {
        updateHold(force);   // idle / armed: ERG on the open page
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
    return startBlocked() || (m_training && m_training->waiting()) || (m_freeRide && m_freeRide->waiting());
}

bool RlvSession::holdWanted() const
{
    return (ergNow() || virtualGears()) && (m_state == State::Idle || m_state == State::Ready)
           && m_gears->activePage() == QLatin1String("video") && trainerConnected() && !holdBlocked();
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
        if (m_state == State::Ready)
            setState(State::Idle);
        emit startRefused(QStringLiteral("Choose a video first."));
        return false;
    }
    if (startBlocked()) {
        if (m_state == State::Ready)
            setState(State::Idle);
        emit startRefused(kRefused);
        return false;
    }
    cancelArmedOthers();
    m_gears->reloadIfPending();
    const bool wasReady = m_state == State::Ready;
    if (m_state == State::Finished || m_distance >= totalDistanceM())
        m_distance = m_startDistance;
    m_rideProfile = m_profiles->current();
    clearRecording();
    m_completed = false;
    resetLastRideId();
    m_startedAt = QDateTime::currentDateTime();
    m_cadSmooth = m_hub->cadence();
    if (!wasReady)
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

void RlvSession::startWhenPedaling()
{
    if (active() || waiting())
        return;
    if (!hasRoute()) {
        emit startRefused(QStringLiteral("Choose a video first."));
        return;
    }
    if (startBlocked()) {
        emit startRefused(kRefused);
        return;
    }
    cancelArmedOthers();   // only one armed session
    if (m_state == State::Finished || m_distance >= totalDistanceM())
        m_distance = m_startDistance;
    clearRecording();
    m_rideProfile = m_profiles->current();
    m_speed = 0;
    m_cadSmooth = 0;
    if (m_hasWorkout && m_workout.state() != WorkoutEngine::State::Idle) {
        const int intensity = m_workout.intensityOffset();
        m_workout.reset();   // shown from 0:00 while armed
        m_workout.setIntensityOffset(intensity);
        m_workoutFinished = false;
        m_complianceSec = m_compliantSec = 0;
        emitWorkoutAll();
    }
    m_hub->expireStale();
    releaseTrainer();
    setState(State::Ready);
    emit positionChanged();
    checkPedaling();
}

void RlvSession::checkPedaling()
{
    if (m_state != State::Ready)
        return;
    if (m_hub->cadence() >= kStartCadenceRpm || m_hub->power() >= kStartPowerWatts)
        start();
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
    if (m_state == State::Ready) {
        releaseTrainer();
        setState(State::Idle);
        return -1;
    }
    if (!active())
        return -1;
    // the last time slice only when the ride is kept: its step may reach the
    // route end, which finishes and saves by itself - not for a discard
    if (save && running() && !m_manualClock && !m_completed)
        process(sessionClockStep(m_clock.restart()));
    if (!active())
        return m_lastRideId;   // finished at the route end inside process()
    releaseTrainer();
    m_shouldPlay = false;
    emit videoChanged();
    setState(State::Finished);
    const qint64 id = save ? saveRide() : -1;
    if (!save)
        emit lastRideIdChanged();   // discarded: stays -1
    m_gears->flushSettings();
    emit finished();
    m_gears->reloadIfPending();
    return id;
}

void RlvSession::reset()
{
    if (active())
        return;
    if (m_state == State::Ready)
        releaseTrainer();
    clearRecording();
    m_speed = 0;
    m_completed = false;
    m_distance = m_startDistance;
    resetLastRideId();
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
    emit positionChanged();
    updateInformation();
    requestSeek();
    emit metricsChanged();
}

bool RlvSession::seekToDistance(double meters)
{
    if (!hasRoute() || active() || waiting() || !std::isfinite(meters))
        return false;
    const double total = totalDistanceM();
    meters = std::clamp(meters, 0.0, std::max(0.0, total - 1.0));
    m_startDistance = m_distance = meters;
    if (m_state == State::Finished) {
        clearRecording();
        m_completed = false;
        setState(State::Idle);
    }
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
    const road::Model model = roadModel(effectiveGradePercent());
    m_speed = road::integrateSpeed(model, m_speed, watts, dt, kRotatingMassKg, kMinDriveSpeedMps, kIntegrationStepSec);
    if (m_speed < 0.3 && watts <= 0 && model.gradePercent >= 0)
        m_speed = 0;
}

void RlvSession::updateModel(double dt)
{
    const int cad = m_hub->cadence();
    const int power = m_hub->power();
    m_cadSmooth += (cad - m_cadSmooth) * std::min(1.0, dt / kCadenceSmoothingSec);

    if (!virtualGears()) {   // sim / ERG: speed from power
        integratePower(power >= kStartPowerWatts || m_speed > 0 ? power : 0, dt);
        return;
    }
    const road::Model model = roadModel(effectiveGradePercent());
    const double roll = std::clamp(m_speed - road::coastDeceleration(model, m_speed) * dt, 0.0, road::kMaxSpeedMps);
    if (cad >= kCoastCadenceRpm) {
        const double pedal = gearing().virtualSpeed(cad);
        // downhill the bike may roll faster than the pedals turn it
        m_speed = model.gradePercent < 0 ? std::max(pedal, roll) : pedal;
    } else if (power >= kStartPowerWatts && cad <= 0) {
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
    if (!(m_state == State::Ready || active())) {
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
        m_activeMs += ms;
        const double total = totalDistanceM();
        m_distance = std::min(total, m_distance + m_speed * dt);
        m_res.elapse(ms);
        recordUpTo(int(m_activeMs / 1000));
        advanceWorkout(ms);
        emit positionChanged();
        updateInformation();
        updateVideo(ms);
        applyTarget();
        emit tick();
        if (m_distance >= total) {
            m_completed = true;
            emit metricsChanged();
            finish(true);   // route end: save automatically
            return;
        }
    } else if (m_ergHold) {
        m_res.elapse(ms);
        updateHold();   // armed: the page hold follows the cadence
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
    r.workoutName = m_hasWorkout ? QStringLiteral("RLV: %1 + %2").arg(m_route.name(), m_workout.workout().name)
                                 : QStringLiteral("RLV: %1").arg(m_route.name());
    r.startedAt = m_startedAt;
    r.blocks = m_hasWorkout ? int(m_workout.workout().intervals.size()) : 0;
    r.ergMode = m_mode == Mode::Erg || (m_hasWorkout && m_workoutErg);
    r.sport = sport::cycling();
    r.ftp = p.ftpFor(r.sport);
    r.distanceM = riddenMeters();
    analysis::applyStats(r, analysis::analyze(m_samples, r.ftp, p.zones));
    m_lastRideId = m_db->saveRide(r, m_samples);
    if (m_lastRideId >= 0) {
        emit lastRideIdChanged();
        emit rideSaved(m_lastRideId);
    }
    return m_lastRideId;
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
    if (w.isRowing() || w.intervals.isEmpty() || w.totalDurationSec() <= 0) {
        setError(QStringLiteral("\"%1\" cannot be ridden in a video ride (cycling workouts with intervals only)")
                     .arg(w.name));
        return false;
    }
    const Profile &p = (waiting() && m_rideProfile.id >= 0) ? m_rideProfile : m_profiles->current();
    m_workout.load(w, p.ftpFor(sport::cycling()));   // intensity 0, from 0:00
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
