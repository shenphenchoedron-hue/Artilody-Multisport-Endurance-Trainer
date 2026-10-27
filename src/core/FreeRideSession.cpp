#include "FreeRideSession.h"

#include "Database.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "SensorHub.h"
#include "SessionClock.h"
#include "TrainingSession.h"
#include "VirtualGears.h"
#include "Workout.h"

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
const QString kSettingMode = QStringLiteral("freeRide/mode");
const QString kSettingTarget = QStringLiteral("freeRide/targetWatts");

int roundTo5(double w)
{
    return int(std::lround(w / 5.0)) * 5;
}
} // namespace

FreeRideSession::FreeRideSession(Database *db, ProfileManager *profiles, SensorHub *hub, VirtualGears *gears,
                                 QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
    , m_gears(gears)
    , m_res(hub)
{
    m_timer.setInterval(250);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FreeRideSession::onTimer);

    connect(m_hub, &SensorHub::powerChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::cadenceChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::heartRateChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::speedChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, &FreeRideSession::devicesChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, [this] {
        if (running() && trainerConnected())
            applyTarget(true);   // a (re)connected trainer gets the current target
        else
            updateHold(true);    // ERG selected on the open page: hold it now
    });
    // ERG outside a ride follows the open page (Main.qml: Gears.activePage).
    connect(m_gears, &VirtualGears::activePageChanged, this, [this] { updateHold(); });
    connect(m_hub, &SensorHub::trainerSimulationSupportChanged, this, &FreeRideSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::trainerReportsSpeedChanged, this, &FreeRideSession::resistanceSetupChanged);
    connect(m_hub, &SensorHub::cadenceReportedChanged, this, &FreeRideSession::resistanceSetupChanged);
    connect(m_gears, &VirtualGears::enabledChanged, this, &FreeRideSession::resistanceSetupChanged);
    connect(m_gears, &VirtualGears::shiftMethodChanged, this, &FreeRideSession::resistanceSetupChanged);
    auto gearChanged = [this] {
        emit targetChanged();
        emit metricsChanged();
        if (virtualGears())
            applyTarget(true);   // new gear / bike -> new resistance right away (ride or page hold)
    };
    connect(m_gears, &VirtualGears::gearChanged, this, gearChanged);
    connect(m_gears, &VirtualGears::gearSetupChanged, this, gearChanged);
    connect(m_gears, &VirtualGears::detectionChanged, this, gearChanged);   // the real gear (path a)

    const QString stored = m_db->setting(kSettingMode, QStringLiteral("ride")).toString();
    m_mode = parseMode(stored).value_or(Mode::Ride);
    if (stored != mode())
        m_db->setSetting(kSettingMode, mode());   // "gears" / "free" (old) -> "ride"
    const int storedTarget = m_db->setting(kSettingTarget, 0).toInt();
    m_targetWatts = storedTarget > 0 ? std::clamp(storedTarget, kMinErgWatts, kMaxWatts) : defaultErgTarget();
}

int FreeRideSession::defaultErgTarget() const
{
    const int ftp = m_profiles->current().ftpFor(sport::cycling());
    if (ftp <= 0)
        return kDefaultErgWatts;
    return std::clamp(roundTo5(ftp * kDefaultErgFtpShare), kMinErgWatts, kMaxWatts);
}

FreeRideSession::~FreeRideSession()
{
    if (m_training) {
        m_training->setStartBlocker(QStringLiteral("freeRide"), {});
        m_training->setAbandonCanceller(QStringLiteral("freeRide"), {});
    }
    if (active() || m_holding)
        m_res.release();
    m_gears->clearRideProfile(this);
}

// --- wiring --------------------------------------------------------------------

void FreeRideSession::setTrainingSession(TrainingSession *training)
{
    if (m_training) {
        m_training->setStartBlocker(QStringLiteral("freeRide"), {});
        m_training->setAbandonCanceller(QStringLiteral("freeRide"), {});
    }
    m_training = training;
    if (training) {
        // a ride in progress blocks the workout; an empty paused one is
        // given up by its start
        training->setStartBlocker(QStringLiteral("freeRide"), [this] { return inProgress(); });
        training->setAbandonCanceller(QStringLiteral("freeRide"), [this] { abandon(); });
        // a workout started elsewhere owns the trainer; when it ends,
        // ERG on the open Free Ride page applies again
        connect(training, &TrainingSession::stateChanged, this, [this] { updateHold(); });
    }
}

void FreeRideSession::setStartBlocker(const QString &key, std::function<bool()> blocker)
{
    if (blocker)
        m_startBlockers.insert(key, std::move(blocker));
    else
        m_startBlockers.remove(key);
}

void FreeRideSession::setAbandonCanceller(const QString &key, std::function<void()> cancel)
{
    if (cancel)
        m_abandonCancellers.insert(key, std::move(cancel));
    else
        m_abandonCancellers.remove(key);
}

void FreeRideSession::abandonOthers()
{
    if (m_training)
        m_training->abandon();   // only empty paused: nothing ridden yet
    for (auto it = m_abandonCancellers.cbegin(); it != m_abandonCancellers.cend(); ++it) {
        if (it.value())
            it.value()();
    }
}

void FreeRideSession::syncRideProfile()
{
    if (active())
        m_gears->setRideProfile(this, m_rideProfile);
    else
        m_gears->clearRideProfile(this);
}

void FreeRideSession::resetLastRideId()
{
    m_lastRideId = -1;
    emit lastRideIdChanged();
}

bool FreeRideSession::startBlocked() const
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

// --- state -----------------------------------------------------------------------

QString FreeRideSession::state() const
{
    switch (m_state) {
    case State::Idle: return QStringLiteral("idle");
    case State::Running: return QStringLiteral("running");
    case State::Paused: return QStringLiteral("paused");
    case State::Finished: return QStringLiteral("finished");
    }
    return QStringLiteral("idle");
}

void FreeRideSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    m_clock.start();
    updateTimer();
    syncRideProfile();
    emit stateChanged();
    if (!active())
        updateHold();   // idle / finished: ERG on the open page holds the target
}

QString FreeRideSession::modeName(Mode m)
{
    return m == Mode::Erg ? QStringLiteral("erg") : QStringLiteral("ride");
}

QString FreeRideSession::mode() const
{
    return modeName(m_mode);
}

std::optional<FreeRideSession::Mode> FreeRideSession::parseMode(const QString &text)
{
    if (text == QLatin1String("ride") || text == QLatin1String("gears") || text == QLatin1String("free"))
        return Mode::Ride;
    if (text == QLatin1String("erg"))
        return Mode::Erg;
    return std::nullopt;
}

void FreeRideSession::setMode(const QString &modeText)
{
    const auto parsed = parseMode(modeText);
    if (!parsed || *parsed == m_mode)
        return;
    if (*parsed == Mode::Erg) {
        // Continue at the power the rider is doing right now (the 3 s power
        // of the display), else the virtual gear's watts, else the saved
        // target (first time: 60 % FTP).
        int start = m_hub->displayPower() >= kPedalPowerWatts ? m_hub->displayPower()
                    : watts() >= kPedalPowerWatts             ? watts()
                                                              : 0;
        if (start <= 0 && running() && virtualGears())
            start = m_res.lastWatts() > 0 ? m_res.lastWatts() : gearTargetWatts();
        if (start > 0)
            m_targetWatts = std::clamp(roundTo5(start), kMinErgWatts, kMaxWatts);
        m_db->setSetting(kSettingTarget, m_targetWatts);
    }
    m_mode = *parsed;
    m_db->setSetting(kSettingMode, mode());
    emit modeChanged();
    emit targetChanged();
    applyTarget(true);
}

void FreeRideSession::resistanceSetupChanged()
{
    emit modeChanged();   // virtualGears / simulationFallback / resistancePath
    emit targetChanged();
    emit metricsChanged();
    applyTarget(true);   // running: the ride's resistance; else the page hold
}

void FreeRideSession::updateTimer()
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

bool FreeRideSession::virtualGears() const
{
    return m_mode == Mode::Ride && (m_gears->enabled() || m_hub->trainerSimulationUnsupported());
}

bool FreeRideSession::simulationFallback() const
{
    return m_mode == Mode::Ride && !m_gears->enabled() && m_hub->trainerSimulationUnsupported();
}

RideResistance::Path FreeRideSession::path() const
{
    return RideResistance::choose(m_mode == Mode::Erg, virtualGears(), m_hub, m_gears->method());
}

const Gearing &FreeRideSession::gearing() const
{
    return m_gears->gearing();
}

// --- live values -------------------------------------------------------------------

int FreeRideSession::watts() const { return m_hub->power(); }
int FreeRideSession::cadence() const { return m_hub->cadence(); }
int FreeRideSession::heartRate() const { return m_hub->heartRate(); }
bool FreeRideSession::trainerConnected() const { return m_hub->trainerConnected(); }

bool FreeRideSession::coasting() const
{
    return m_hub->cadence() < kCoastCadenceRpm && m_hub->power() < kPedalPowerWatts;
}

road::Model FreeRideSession::roadModel(double gradePercent) const
{
    // the rider of the ride; weight / height edits of the same profile apply
    // at once (like the trainer's weight, SensorHub::riderMassKg)
    const Profile &cur = m_profiles->current();
    const Profile &p = active() && m_rideProfile.id != cur.id ? m_rideProfile : cur;
    return p.weightKg > 0 ? m_gears->roadModel(gradePercent, p) : m_gears->roadModel(gradePercent);
}

double FreeRideSession::speedMps() const
{
    if (active())
        return m_speed;
    // idle/finished: instantaneous estimate for the live display
    const int cad = m_hub->cadence();
    if (virtualGears() && cad >= kCoastCadenceRpm)
        return gearing().virtualSpeed(cad);
    const int p = m_hub->power();
    return p > 0 ? road::speedForPower(roadModel(m_mode == Mode::Ride ? m_grade : 0.0), p) : 0.0;
}

double FreeRideSession::speedKmh() const
{
    return speedMps() * 3.6;
}

double FreeRideSession::avgSpeedKmh() const
{
    return m_activeMs > 0 ? m_distance / (m_activeMs / 1000.0) * 3.6 : 0.0;
}

int FreeRideSession::avgWatts() const
{
    return m_recordedSec > 0 ? int(std::lround(m_powerSum / m_recordedSec)) : 0;
}

// --- targets ------------------------------------------------------------------------

void FreeRideSession::setPlannedDurationSec(int seconds)
{
    seconds = std::clamp(seconds, 0, 24 * 3600);
    if (seconds == m_plannedDurationSec)
        return;
    m_plannedDurationSec = seconds;
    emit plannedDurationChanged();
}

void FreeRideSession::setTargetWatts(int w)
{
    w = std::clamp(w, kMinErgWatts, kMaxWatts);
    if (w == m_targetWatts)
        return;
    m_targetWatts = w;
    m_db->setSetting(kSettingTarget, w);
    emit targetChanged();
    applyTarget();
}

void FreeRideSession::adjustTargetWatts(int delta)
{
    setTargetWatts(m_targetWatts + delta);
}

void FreeRideSession::setGrade(double percent)
{
    if (!std::isfinite(percent))
        return;
    percent = std::clamp(std::round(percent * 10.0) / 10.0, kMinGrade, kMaxGrade);
    if (qFuzzyCompare(1.0 + percent, 1.0 + m_grade))
        return;
    m_grade = percent;
    emit gradeChanged();
    emit targetChanged();
    applyTarget(true);   // a new road applies at once, like a shift
}

void FreeRideSession::adjustGrade(double delta)
{
    setGrade(m_grade + delta);
}

RideResistance::GearDrive FreeRideSession::gearDrive(double cadenceRpm) const
{
    RideResistance::GearDrive d;
    d.road = roadModel(m_grade);
    d.cadenceRpm = cadenceRpm;
    d.virtualRatio = gearing().ratio();
    d.referenceRatio = m_gears->effectiveReferenceRatio();   // the detected real gear when known
    d.circumferenceMm = gearing().setup().circumferenceMm;
    // without a cadence the trainer's speed stands in for the reference gear
    d.trainerSpeedMps = m_hub->cadenceReported() ? 0.0 : m_hub->speedMps();
    d.bikeSpeedMps = running() ? m_speed : -1.0;
    return d;
}

double FreeRideSession::controlCadence() const
{
    const int raw = m_hub->cadence();
    if (raw < kCoastCadenceRpm)
        return raw;   // coasting (or no cadence)
    // smoothed during a ride, else the live cadence
    return active() ? std::max(m_cadSmooth, double(kCoastCadenceRpm)) : double(raw);
}

int FreeRideSession::gearTargetWatts() const
{
    if (!virtualGears())
        return 0;
    // Smoothed cadence during a ride, else the live cadence.
    const double cad = active() ? m_cadSmooth : double(m_hub->cadence());
    return RideResistance::gearWatts(gearDrive(cad));
}

int FreeRideSession::desiredGearWatts() const
{
    return RideResistance::gearWatts(gearDrive(controlCadence()));
}

double FreeRideSession::gearSimulationGrade() const
{
    return RideResistance::gearGrade(gearDrive(controlCadence()));
}

bool FreeRideSession::sendResistance(bool force)
{
    switch (path()) {
    case RideResistance::Path::Erg:
        return m_res.sendErg(m_targetWatts, force);
    case RideResistance::Path::Simulation:
        return m_res.sendGrade(m_grade, force, RideResistance::trainerModel(gearDrive(0)));
    case RideResistance::Path::GearSimulation: {
        const RideResistance::GearDrive d = gearDrive(controlCadence());
        return m_res.sendGrade(RideResistance::gearGrade(d), force, RideResistance::trainerModel(d));
    }
    case RideResistance::Path::GearErg:
        // with engage/release hysteresis on the live cadence
        return m_res.sendGear(gearDrive(controlCadence()), force, m_hub->cadence());
    case RideResistance::Path::GearHybrid:
        return m_res.sendHybrid(gearDrive(controlCadence()), force, m_hub->cadence(), m_hub->cadenceReported());
    }
    return false;
}

void FreeRideSession::applyTarget(bool force)
{
    if (!running()) {
        updateHold(force);   // not running: ERG / gears on the open page
        return;
    }
    const bool wasHolding = m_holding;
    m_holding = false;   // the ride's resistance from here on
    if (sendResistance(force) || wasHolding)
        emit targetChanged();
}

void FreeRideSession::releaseTrainer()
{
    m_holding = false;
    m_res.release();
    emit targetChanged();
}

bool FreeRideSession::holdBlocked() const
{
    // another session drives the trainer: a workout riding (a
    // paused one has released it: the open page may hold it meanwhile), a
    // video ride / row in progress
    if (m_training && m_training->running())
        return true;
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.value() && it.value()())
            return true;
    }
    return false;
}

bool FreeRideSession::holdWanted() const
{
    return (m_mode == Mode::Erg || virtualGears()) && m_state == State::Idle
           && m_gears->activePage() == QLatin1String("freeride") && trainerConnected() && !holdBlocked();
}

void FreeRideSession::updateHold(bool force)
{
    if (active())
        return;   // running: applyTarget(); paused: released until resume()
    if (holdWanted()) {
        const bool sent = sendResistance(force || !m_holding);
        const bool changed = !m_holding;
        m_holding = true;
        updateTimer();
        if (sent || changed)
            emit targetChanged();
        return;
    }
    if (!m_holding)
        return;
    m_holding = false;
    updateTimer();
    // Release only what is still ours: another session may have sent its own
    // command in the meantime (e.g. the Video Ride page holding its target).
    if (!holdBlocked() && m_res.lastCommandIsOurs())
        m_res.release();
    else
        m_res.reset();
    emit targetChanged();
}

void FreeRideSession::controllerShift(int direction)
{
    if (direction == 0)
        return;
    if (m_mode == Mode::Erg) {
        const int before = m_targetWatts;
        adjustTargetWatts(direction > 0 ? kControllerErgStep : -kControllerErgStep);
        if (m_targetWatts != before)
            emit shifted(direction);
        return;
    }
    // ride: always the gears (VirtualGears::shifted drives the page)
    if (direction > 0)
        m_gears->shiftUp();
    else
        m_gears->shiftDown();
}

// --- control ---------------------------------------------------------------------------

void FreeRideSession::clearRecording()
{
    m_samples.clear();
    m_powerData.clear();
    m_speedData.clear();
    m_hrData.clear();
    m_recordedSec = 0;
    m_activeMs = 0;
    m_energyJ = 0;
    m_powerSum = 0;
    m_maxWatts = 0;
    m_ergSeconds = 0;
    m_distance = 0;
    emit seriesChanged();
    emit tick();
}

bool FreeRideSession::start()
{
    if (active())
        return false;
    if (startBlocked()) {
        emit startRefused(QStringLiteral("Another session is running. Stop it before starting a free ride."));
        return false;
    }
    abandonOthers();
    m_gears->reloadIfPending();
    m_rideProfile = m_profiles->current();
    clearRecording();
    resetLastRideId();
    m_startedAt = QDateTime::currentDateTime();
    m_cadSmooth = m_hub->cadence();
    m_speed = 0;
    m_res.reset();
    setState(State::Running);
    applyTarget(true);
    emit started();
    emit metricsChanged();
    return true;
}

void FreeRideSession::pause()
{
    if (!running())
        return;
    if (!m_manualClock)
        process(sessionClockStep(m_clock.restart()));
    setState(State::Paused);
    releaseTrainer();   // otherwise the trainer keeps the resistance
}

void FreeRideSession::resume()
{
    if (!paused())
        return;
    if (startBlocked()) {
        emit startRefused(QStringLiteral("Another session is running."));
        return;
    }
    setState(State::Running);
    m_clock.restart();
    m_res.reset();
    applyTarget(true);
}

void FreeRideSession::togglePause()
{
    if (running())
        pause();
    else if (paused())
        resume();
    else
        start();   // idle/finished: Space / the Start button
}

qint64 FreeRideSession::finish(bool save)
{
    if (!active())
        return -1;
    if (running() && !m_manualClock)
        process(sessionClockStep(m_clock.restart()));
    releaseTrainer();
    setState(State::Finished);
    const qint64 id = save ? saveRide() : -1;
    if (!save)
        emit lastRideIdChanged();   // discarded: stays -1
    m_gears->flushSettings();
    emit finished();
    m_gears->reloadIfPending();
    return id;
}

void FreeRideSession::reset()
{
    if (active())
        return;
    clearRecording();
    m_speed = 0;
    resetLastRideId();
    setState(State::Idle);
    emit metricsChanged();
}

// --- clock ------------------------------------------------------------------------------

void FreeRideSession::advanceClock(int ms)
{
    process(ms);
}

void FreeRideSession::advanceWallClock(qint64 elapsedMs)
{
    process(sessionClockStep(elapsedMs));
}

void FreeRideSession::onTimer()
{
    process(sessionClockStep(m_clock.restart()));
}

void FreeRideSession::updateModel(double dt)
{
    const int cad = m_hub->cadence();
    const int power = m_hub->power();
    // cadence smoothing for the gear target (first-order low pass)
    m_cadSmooth += (cad - m_cadSmooth) * std::min(1.0, dt / kCadenceSmoothingSec);

    if (!virtualGears()) {
        // real gears / ERG: speed from power with inertia (ERG on a flat road)
        const road::Model model = roadModel(m_mode == Mode::Ride ? m_grade : 0.0);
        m_speed = road::integrateSpeed(model, m_speed, power >= kPedalPowerWatts || m_speed > 0 ? power : 0, dt);
        if (m_speed < 0.3 && power < kPedalPowerWatts && model.gradePercent >= 0)
            m_speed = 0;
        return;
    }
    const road::Model model = roadModel(m_grade);
    const double roll = std::clamp(m_speed - road::coastDeceleration(model, m_speed) * dt, 0.0, road::kMaxSpeedMps);
    if (cad >= kCoastCadenceRpm) {
        // pedaling: the virtual wheel turns with the cranks; downhill the
        // bike may roll faster than the pedals turn it
        const double pedal = gearing().virtualSpeed(cad);
        m_speed = model.gradePercent < 0 ? std::max(pedal, roll) : pedal;
    } else if (power >= kPedalPowerWatts && cad <= 0) {
        m_speed = road::integrateSpeed(model, m_speed, power, dt);   // trainer without cadence
    } else {
        // coasting: roll out against the road resistance
        m_speed = roll;
        if (m_speed < 0.3 && model.gradePercent >= 0)
            m_speed = 0;
    }
}

void FreeRideSession::process(int ms)
{
    m_hub->expireStale();
    if (ms <= 0)
        return;
    if (!active()) {
        if (m_holding) {   // idle page holding the virtual gear: follow the cadence
            m_res.elapse(ms);
            updateHold();
            emit metricsChanged();
        }
        return;
    }
    if (paused())
        return;   // clock stopped, values frozen
    const double dt = ms / 1000.0;
    updateModel(dt);
    if (running()) {
        m_activeMs += ms;
        m_distance += m_speed * dt;
        m_res.elapse(ms);
        recordUpTo(int(m_activeMs / 1000));
        applyTarget();
        emit tick();
    }
    emit metricsChanged();
}

void FreeRideSession::recordUpTo(int second)
{
    bool added = false;
    while (m_recordedSec < second) {
        Sample s;
        s.t = m_recordedSec;
        s.power = m_hub->power();
        s.cadence = m_hub->cadence();
        s.heartRate = m_hub->heartRate();
        s.targetPower = m_mode == Mode::Erg ? m_targetWatts : 0;
        s.speedMps = m_speed;
        m_samples.append(s);
        m_powerData.append(s.power);
        m_speedData.append(s.speedMps * 3.6);
        m_hrData.append(s.heartRate);
        m_energyJ += s.power;
        m_powerSum += s.power;
        m_maxWatts = std::max(m_maxWatts, s.power);
        if (m_mode == Mode::Erg)
            ++m_ergSeconds;
        ++m_recordedSec;
        added = true;
    }
    if (added)
        emit seriesChanged();
}

qint64 FreeRideSession::saveRide()
{
    if (m_samples.size() < m_minSaveSec)
        return -1;
    const Profile &p = m_rideProfile;
    RideRecord r;
    r.profileId = p.id;
    r.workoutId = -1;
    r.workoutName = QStringLiteral("Free Ride");
    r.startedAt = m_startedAt;
    r.blocks = 0;
    r.ergMode = m_ergSeconds * 2 >= m_samples.size();
    r.sport = sport::cycling();
    r.ftp = p.ftpFor(r.sport);
    r.distanceM = m_distance;
    analysis::applyStats(r, analysis::analyze(m_samples, r.ftp, p.zones));
    m_lastRideId = m_db->saveRide(r, m_samples);
    if (m_lastRideId >= 0) {
        emit lastRideIdChanged();
        emit rideSaved(m_lastRideId);
    }
    return m_lastRideId;
}

// --- series ----------------------------------------------------------------------------

int FreeRideSession::seriesStep() const
{
    return std::max(1, int(std::ceil(m_powerData.size() / double(kMaxSeriesPoints))));
}

QVariantList FreeRideSession::downsample(const QVector<double> &data, bool ignoreZero, int decimals) const
{
    QVariantList out;
    const int step = seriesStep();
    const double scale = std::pow(10.0, decimals);
    out.reserve(data.size() / step + 1);
    for (int i = 0; i < data.size(); i += step) {
        double sum = 0;
        int cnt = 0;
        for (int j = i; j < std::min<int>(data.size(), i + step); ++j) {
            if (ignoreZero && data.at(j) <= 0)
                continue;
            sum += data.at(j);
            ++cnt;
        }
        const double mean = cnt > 0 ? sum / cnt : 0.0;
        if (decimals == 0)
            out.append(int(std::lround(mean)));
        else
            out.append(std::round(mean * scale) / scale);
    }
    return out;
}

QVariantList FreeRideSession::powerSeries() const { return downsample(m_powerData, false, 0); }
QVariantList FreeRideSession::speedSeries() const { return downsample(m_speedData, false, 1); }
QVariantList FreeRideSession::heartRateSeries() const { return downsample(m_hrData, true, 0); }

} // namespace watt
