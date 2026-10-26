#include "FreeRideSession.h"

#include "Database.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "SensorHub.h"
#include "TrainingSession.h"
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

QVector<int> toIntList(const QVariantList &list, bool *ok)
{
    QVector<int> out;
    *ok = true;
    for (const QVariant &v : list) {
        bool good = false;
        const double d = v.toDouble(&good);
        if (!good || !std::isfinite(d) || d != std::floor(d)) {
            *ok = false;
            return {};
        }
        out.append(int(d));
    }
    return out;
}
} // namespace

FreeRideSession::FreeRideSession(Database *db, ProfileManager *profiles, SensorHub *hub, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
{
    m_timer.setInterval(250);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FreeRideSession::onTimer);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &FreeRideSession::saveSettingsNow);

    connect(m_hub, &SensorHub::powerChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::cadenceChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::heartRateChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::speedChanged, this, &FreeRideSession::metricsChanged);
    connect(m_hub, &SensorHub::powerChanged, this, &FreeRideSession::checkPedaling);
    connect(m_hub, &SensorHub::cadenceChanged, this, &FreeRideSession::checkPedaling);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, &FreeRideSession::devicesChanged);
    connect(m_hub, &SensorHub::trainerConnectedChanged, this, [this] {
        if (running() && trainerConnected())
            applyTarget(true);   // a (re)connected trainer gets the current target
    });

    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (m_profiles->profileId() == m_setupProfileId)
            return;   // same rider, e.g. FTP edited
        if (active() || waiting()) {
            m_reloadPending = true;   // keep the bike of the running ride
            return;
        }
        loadSetup();
    });

    const QString mode = m_db->setting(kSettingMode, QStringLiteral("gears")).toString();
    m_mode = mode == QLatin1String("erg") ? Mode::Erg : mode == QLatin1String("free") ? Mode::Free : Mode::Gears;
    m_targetWatts = std::clamp(m_db->setting(kSettingTarget, kDefaultErgWatts).toInt(), kMinErgWatts, kMaxWatts);
    loadSetup();
}

FreeRideSession::~FreeRideSession()
{
    if (m_training)
        m_training->setStartBlocker(QStringLiteral("freeRide"), {});
    if (active() || waiting())
        releaseTrainer();
    saveSettingsNow();
}

// --- wiring --------------------------------------------------------------------

void FreeRideSession::setTrainingSession(TrainingSession *training)
{
    if (m_training)
        m_training->setStartBlocker(QStringLiteral("freeRide"), {});
    m_training = training;
    if (training)
        training->setStartBlocker(QStringLiteral("freeRide"), [this] { return active() || waiting(); });
}

void FreeRideSession::setStartBlocker(const QString &key, std::function<bool()> blocker)
{
    if (blocker)
        m_startBlockers.insert(key, std::move(blocker));
    else
        m_startBlockers.remove(key);
}

bool FreeRideSession::startBlocked() const
{
    if (m_training && m_training->active())
        return true;
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.value() && it.value()())
            return true;
    }
    return false;
}

// --- settings ------------------------------------------------------------------

void FreeRideSession::loadSetup()
{
    saveSettingsNow();   // a pending change belongs to the previous profile
    m_reloadPending = false;
    m_setupProfileId = m_profiles->profileId();
    const QString json = m_setupProfileId >= 0 ? m_db->gearSetupJson(m_setupProfileId) : QString();
    m_gearing = Gearing(GearSetup::fromJsonText(json));
    emit gearSetupChanged();
    emit gearChanged();
    emit targetChanged();
}

void FreeRideSession::scheduleSave()
{
    m_savePending = true;
    if (m_manualClock)
        return;   // tests call flushSettings()
    m_saveTimer.start();
}

void FreeRideSession::flushSettings()
{
    saveSettingsNow();
}

void FreeRideSession::saveSettingsNow()
{
    m_saveTimer.stop();
    if (!m_savePending)
        return;
    m_savePending = false;
    if (m_setupProfileId >= 0)
        m_db->setGearSetupJson(m_setupProfileId, m_gearing.setup().toJsonText());
}

void FreeRideSession::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

// --- state -----------------------------------------------------------------------

QString FreeRideSession::state() const
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

void FreeRideSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (m_state == State::Ready || active()) {
        m_clock.start();
        if (!m_manualClock)
            m_timer.start();
    } else {
        m_timer.stop();
    }
    emit stateChanged();
}

QString FreeRideSession::mode() const
{
    switch (m_mode) {
    case Mode::Gears: return QStringLiteral("gears");
    case Mode::Erg: return QStringLiteral("erg");
    case Mode::Free: return QStringLiteral("free");
    }
    return QStringLiteral("gears");
}

void FreeRideSession::setMode(const QString &modeText)
{
    Mode m;
    if (modeText == QLatin1String("gears"))
        m = Mode::Gears;
    else if (modeText == QLatin1String("erg"))
        m = Mode::Erg;
    else if (modeText == QLatin1String("free"))
        m = Mode::Free;
    else
        return;
    if (m == m_mode)
        return;
    if (m == Mode::Erg) {
        // Continue at the power the rider is doing right now.
        int start = watts();
        if (start <= 0 && m_mode == Mode::Gears)
            start = m_lastSent > 0 ? m_lastSent : gearTargetWatts();
        if (start > 0)
            m_targetWatts = std::clamp(roundTo5(start), kMinErgWatts, kMaxWatts);
        m_db->setSetting(kSettingTarget, m_targetWatts);
    }
    m_mode = m;
    m_db->setSetting(kSettingMode, mode());
    emit modeChanged();
    emit targetChanged();
    applyTarget(true);
}

void FreeRideSession::setErgEnabled(bool on)
{
    setMode(on ? QStringLiteral("erg") : QStringLiteral("gears"));
}

// --- live values -------------------------------------------------------------------

int FreeRideSession::watts() const { return m_hub->power(); }
int FreeRideSession::cadence() const { return m_hub->cadence(); }
int FreeRideSession::heartRate() const { return m_hub->heartRate(); }
bool FreeRideSession::trainerConnected() const { return m_hub->trainerConnected(); }

bool FreeRideSession::coasting() const
{
    return m_hub->cadence() < kCoastCadenceRpm && m_hub->power() < kStartPowerWatts;
}

road::Model FreeRideSession::roadModel(double gradePercent) const
{
    const Profile &p = active() ? m_rideProfile : m_profiles->current();
    const GearSetup &s = m_gearing.setup();
    road::Model m;
    m.massKg = std::max(30.0, p.weightKg) + s.bikeWeightKg;
    m.cda = s.cda;
    m.crr = s.crr;
    m.gradePercent = gradePercent;
    return m;
}

double FreeRideSession::speedMps() const
{
    if (m_state == State::Ready || active())
        return m_speed;
    // idle/finished: instantaneous estimate for the live display
    const int cad = m_hub->cadence();
    if (m_mode == Mode::Gears && cad >= kCoastCadenceRpm)
        return m_gearing.virtualSpeed(cad);
    const int p = m_hub->power();
    return p > 0 ? road::speedForPower(roadModel(m_mode == Mode::Gears ? m_grade : 0.0), p) : 0.0;
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

void FreeRideSession::setClickConnected(bool on)
{
    if (m_clickConnected == on)
        return;
    m_clickConnected = on;
    emit devicesChanged();
}

// --- targets ------------------------------------------------------------------------

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

int FreeRideSession::computeGearTarget(double cadenceRpm) const
{
    if (cadenceRpm < kCoastCadenceRpm)
        return 0;
    const double v = m_gearing.virtualSpeed(cadenceRpm);
    const double p = road::powerForSpeed(roadModel(m_grade), v);
    return std::clamp(int(std::lround(p)), kMinGearWatts, kMaxWatts);
}

int FreeRideSession::gearTargetWatts() const
{
    // Smoothed cadence while a ride is armed/running, else the live cadence.
    const double cad = (m_state == State::Ready || active()) ? m_cadSmooth : double(m_hub->cadence());
    return computeGearTarget(cad);
}

int FreeRideSession::desiredTarget() const
{
    switch (m_mode) {
    case Mode::Erg:
        return m_targetWatts;
    case Mode::Free:
        return 0;
    case Mode::Gears:
        if (m_hub->cadence() < kCoastCadenceRpm)
            return 0;   // coasting: release
        return computeGearTarget(std::max(m_cadSmooth, double(kCoastCadenceRpm)));
    }
    return 0;
}

void FreeRideSession::applyTarget(bool force)
{
    if (!running())
        return;
    const int desired = desiredTarget();
    if (desired <= 0) {
        if (force || m_lastSent != 0) {
            m_hub->requestFreeRide();
            m_lastSent = 0;
            m_sinceSendMs = 0;
            emit targetChanged();
        }
        return;
    }
    const bool changed = m_lastSent <= 0 || std::abs(desired - m_lastSent) >= kMinTargetChangeWatts;
    // ERG targets and explicit changes (force) go out at once; the
    // cadence-driven gears target at most every kTargetIntervalMs.
    const bool due = m_mode != Mode::Gears || m_lastSent <= 0 || m_sinceSendMs >= kTargetIntervalMs;
    if (force || (changed && due)) {
        m_hub->requestTargetPower(desired);
        m_lastSent = desired;
        m_sinceSendMs = 0;
        emit targetChanged();
    }
}

void FreeRideSession::releaseTrainer()
{
    m_hub->requestFreeRide();
    m_lastSent = -1;
    emit targetChanged();
}

// --- gears ------------------------------------------------------------------------------

QVariantMap FreeRideSession::gear() const
{
    return QVariantMap { { QStringLiteral("front"), m_gearing.frontIndex() },
                         { QStringLiteral("rear"), m_gearing.rearIndex() },
                         { QStringLiteral("frontTeeth"), m_gearing.frontTeeth() },
                         { QStringLiteral("rearTeeth"), m_gearing.rearTeeth() },
                         { QStringLiteral("label"), m_gearing.label() },
                         { QStringLiteral("index"), m_gearing.gearIndex() },
                         { QStringLiteral("count"), m_gearing.gearCount() },
                         { QStringLiteral("ratio"), m_gearing.ratio() } };
}

QVariantList FreeRideSession::gears() const
{
    QVariantList out;
    const QVector<Gear> list = m_gearing.setup().sequential ? m_gearing.sequence() : m_gearing.allGears();
    for (int i = 0; i < list.size(); ++i) {
        const Gear &g = list.at(i);
        out.append(QVariantMap { { QStringLiteral("index"), i },
                                 { QStringLiteral("label"), Gearing::labelFor(g.frontTeeth, g.rearTeeth) },
                                 { QStringLiteral("ratio"), g.ratio },
                                 { QStringLiteral("front"), g.front },
                                 { QStringLiteral("rear"), g.rear } });
    }
    return out;
}

QVariantMap FreeRideSession::setupToMap(const GearSetup &s)
{
    QVariantList rings, cogs;
    for (int t : s.chainrings)
        rings.append(t);
    for (int t : s.cogs)
        cogs.append(t);
    return QVariantMap { { QStringLiteral("chainrings"), rings },
                         { QStringLiteral("cogs"), cogs },
                         { QStringLiteral("circumferenceMm"), s.circumferenceMm },
                         { QStringLiteral("sequential"), s.sequential },
                         { QStringLiteral("bikeWeightKg"), s.bikeWeightKg },
                         { QStringLiteral("cda"), s.cda },
                         { QStringLiteral("crr"), s.crr } };
}

QVariantMap FreeRideSession::gearSetup() const
{
    return setupToMap(m_gearing.setup());
}

QVariantMap FreeRideSession::defaultGearSetup() const
{
    return setupToMap(GearSetup::defaults());
}

QVariantList FreeRideSession::previewGears(const QVariantList &chainrings, const QVariantList &cogs,
                                           int circumferenceMm, bool seq, int cadenceRpm) const
{
    if (!validateGearSetup(chainrings, cogs, circumferenceMm).isEmpty())
        return {};
    bool ok = false;
    GearSetup s = m_gearing.setup();
    s.chainrings = toIntList(chainrings, &ok);
    s.cogs = toIntList(cogs, &ok);
    s.circumferenceMm = circumferenceMm;
    s.sequential = seq;
    s.frontIndex = s.rearIndex = 0;
    const Gearing g(s);
    const QVector<Gear> list = seq ? g.sequence() : g.allGears();
    const road::Model flat = roadModel(0.0);
    const double cadence = std::max(0, cadenceRpm);
    QVariantList out;
    for (int i = 0; i < list.size(); ++i) {
        const Gear &gear = list.at(i);
        const double v = Gearing::speedFor(cadence, gear.ratio, g.setup().circumferenceMm);
        out.append(QVariantMap { { QStringLiteral("index"), i },
                                 { QStringLiteral("label"), Gearing::labelFor(gear.frontTeeth, gear.rearTeeth) },
                                 { QStringLiteral("ratio"), gear.ratio },
                                 { QStringLiteral("front"), gear.front },
                                 { QStringLiteral("rear"), gear.rear },
                                 { QStringLiteral("frontTeeth"), gear.frontTeeth },
                                 { QStringLiteral("rearTeeth"), gear.rearTeeth },
                                 { QStringLiteral("speedKmh"), std::round(v * 36.0) / 10.0 },
                                 { QStringLiteral("watts"), std::max(0, int(std::lround(road::powerForSpeed(flat, v)))) } });
    }
    return out;
}

void FreeRideSession::afterShift(bool didShift, int direction)
{
    if (!didShift)
        return;
    scheduleSave();
    emit gearChanged();
    emit targetChanged();
    emit metricsChanged();
    emit shifted(direction);
    applyTarget(true);   // new gear -> new resistance right away
}

bool FreeRideSession::shiftUp()
{
    const bool ok = m_gearing.shiftUp();
    afterShift(ok, 1);
    return ok;
}

bool FreeRideSession::shiftDown()
{
    const bool ok = m_gearing.shiftDown();
    afterShift(ok, -1);
    return ok;
}

bool FreeRideSession::shiftFrontUp()
{
    const bool ok = m_gearing.shiftFrontUp();
    afterShift(ok, 1);
    return ok;
}

bool FreeRideSession::shiftFrontDown()
{
    const bool ok = m_gearing.shiftFrontDown();
    afterShift(ok, -1);
    return ok;
}

bool FreeRideSession::setGear(int front, int rear)
{
    const double before = m_gearing.ratio();
    if (!m_gearing.setGear(front, rear))
        return false;
    afterShift(true, m_gearing.ratio() >= before ? 1 : -1);
    return true;
}

bool FreeRideSession::setGearIndex(int index)
{
    const double before = m_gearing.ratio();
    if (!m_gearing.setGearIndex(index))
        return false;
    afterShift(true, m_gearing.ratio() >= before ? 1 : -1);
    return true;
}

void FreeRideSession::setSequential(bool on)
{
    if (sequential() == on)
        return;
    m_gearing.setSequential(on);
    scheduleSave();
    emit gearSetupChanged();
    emit gearChanged();
    applyTarget(true);
}

QString FreeRideSession::validateGearSetup(const QVariantList &chainrings, const QVariantList &cogs,
                                           int circumferenceMm) const
{
    bool okRings = false, okCogs = false;
    const QVector<int> rings = toIntList(chainrings, &okRings);
    const QVector<int> sprockets = toIntList(cogs, &okCogs);
    if (!okRings)
        return QStringLiteral("Chainrings must be whole numbers");
    if (!okCogs)
        return QStringLiteral("Cogs must be whole numbers");
    QString error;
    if (!GearSetup::validate(rings, sprockets, circumferenceMm, &error))
        return error;
    return QString();
}

bool FreeRideSession::setGearSetup(const QVariantList &chainrings, const QVariantList &cogs, int circumferenceMm,
                                   bool seq)
{
    const QString error = validateGearSetup(chainrings, cogs, circumferenceMm);
    if (!error.isEmpty()) {
        setError(error);
        return false;
    }
    bool ok = false;
    GearSetup s = m_gearing.setup();
    s.chainrings = toIntList(chainrings, &ok);
    s.cogs = toIntList(cogs, &ok);
    s.circumferenceMm = circumferenceMm;
    s.sequential = seq;
    s.frontIndex = s.rearIndex = -1;   // -> nearest ratio to the current gear
    m_gearing.setSetup(s);
    setError(QString());
    scheduleSave();
    emit gearSetupChanged();
    emit gearChanged();
    emit targetChanged();
    applyTarget(true);
    return true;
}

bool FreeRideSession::setBikePhysics(double bikeWeightKg, double cda, double crr)
{
    if (!std::isfinite(bikeWeightKg) || bikeWeightKg < 0 || bikeWeightKg > 60) {
        setError(QStringLiteral("Bike weight must be 0–60 kg"));
        return false;
    }
    if (!std::isfinite(cda) || cda < 0.1 || cda > 1.0) {
        setError(QStringLiteral("CdA must be 0.10–1.00 m²"));
        return false;
    }
    if (!std::isfinite(crr) || crr < 0.001 || crr > 0.05) {
        setError(QStringLiteral("Crr must be 0.001–0.050"));
        return false;
    }
    GearSetup s = m_gearing.setup();
    s.bikeWeightKg = bikeWeightKg;
    s.cda = cda;
    s.crr = crr;
    m_gearing.setSetup(s);
    setError(QString());
    scheduleSave();
    emit gearSetupChanged();
    emit targetChanged();
    applyTarget(true);
    return true;
}

void FreeRideSession::resetGearSetup()
{
    m_gearing = Gearing(GearSetup::defaults());
    setError(QString());
    scheduleSave();
    emit gearSetupChanged();
    emit gearChanged();
    emit targetChanged();
    applyTarget(true);
}

QVariantList FreeRideSession::parseTeeth(const QString &text) const
{
    QVariantList out;
    for (int v : GearSetup::parseTeethList(text))
        out.append(v);
    return out;
}

void FreeRideSession::controllerShift(int direction)
{
    if (direction == 0)
        return;
    switch (m_mode) {
    case Mode::Gears:
        if (direction > 0)
            shiftUp();
        else
            shiftDown();
        break;
    case Mode::Erg: {
        const int before = m_targetWatts;
        adjustTargetWatts(direction > 0 ? kControllerErgStep : -kControllerErgStep);
        if (m_targetWatts != before)
            emit shifted(direction);
        break;
    }
    case Mode::Free:
        break;   // no resistance control: nothing to shift
    }
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
    m_sinceSendMs = 0;
    emit seriesChanged();
    emit tick();
}

bool FreeRideSession::start()
{
    if (active())
        return false;
    if (startBlocked()) {
        if (m_state == State::Ready)
            setState(State::Idle);   // armed: give up instead of retrying on every pedal stroke
        emit startRefused(QStringLiteral("Another session is running. Stop it before starting a free ride."));
        return false;
    }
    if (m_training && m_training->waiting())
        m_training->stop();   // only armed: nothing ridden yet
    if (m_reloadPending)
        loadSetup();
    m_rideProfile = m_profiles->current();
    clearRecording();
    m_lastRideId = -1;
    m_startedAt = QDateTime::currentDateTime();
    m_cadSmooth = m_hub->cadence();
    if (m_state != State::Ready)
        m_speed = 0;
    m_lastSent = -1;
    setState(State::Running);
    applyTarget(true);
    emit started();
    emit metricsChanged();
    return true;
}

void FreeRideSession::startWhenPedaling()
{
    if (active() || waiting())
        return;
    if (startBlocked()) {
        emit startRefused(QStringLiteral("Another session is running. Stop it before starting a free ride."));
        return;
    }
    if (m_training && m_training->waiting())
        m_training->stop();   // only one armed session
    if (m_reloadPending)
        loadSetup();
    clearRecording();
    m_speed = 0;
    m_cadSmooth = 0;
    m_hub->expireStale();   // ignore readings left over from an earlier ride
    releaseTrainer();       // spin up freely, the resistance comes with start()
    setState(State::Ready);
    checkPedaling();        // already pedaling -> start right away
}

void FreeRideSession::checkPedaling()
{
    if (m_state != State::Ready)
        return;
    if (m_hub->cadence() >= kStartCadenceRpm || m_hub->power() >= kStartPowerWatts)
        start();
}

void FreeRideSession::pause()
{
    if (!running())
        return;
    if (!m_manualClock)
        process(int(m_clock.restart()));
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
    m_lastSent = -1;
    applyTarget(true);
}

void FreeRideSession::togglePause()
{
    if (running())
        pause();
    else if (paused())
        resume();
    else
        start();   // idle/ready/finished: start now
}

qint64 FreeRideSession::finish(bool save)
{
    if (m_state == State::Ready) {
        releaseTrainer();
        setState(State::Idle);
        return -1;
    }
    if (!active())
        return -1;
    if (running() && !m_manualClock)
        process(int(m_clock.restart()));
    releaseTrainer();
    setState(State::Finished);
    const qint64 id = save ? saveRide() : -1;
    saveSettingsNow();
    emit finished();
    if (m_reloadPending)
        loadSetup();
    return id;
}

void FreeRideSession::reset()
{
    if (active())
        return;
    if (m_state == State::Ready)
        releaseTrainer();
    clearRecording();
    m_speed = 0;
    setState(State::Idle);
    emit metricsChanged();
}

// --- clock ------------------------------------------------------------------------------

void FreeRideSession::advanceClock(int ms)
{
    process(ms);
}

void FreeRideSession::onTimer()
{
    process(int(m_clock.restart()));
}

void FreeRideSession::updateModel(double dt)
{
    const int cad = m_hub->cadence();
    const int power = m_hub->power();
    // cadence smoothing for the gears target (first-order low pass)
    m_cadSmooth += (cad - m_cadSmooth) * std::min(1.0, dt / kCadenceSmoothingSec);

    const road::Model model = roadModel(m_mode == Mode::Gears ? m_grade : 0.0);
    if (m_mode == Mode::Gears && cad >= kCoastCadenceRpm) {
        // pedaling: the virtual wheel turns with the cranks
        m_speed = m_gearing.virtualSpeed(cad);
    } else if (power >= kStartPowerWatts && (m_mode != Mode::Gears || cad <= 0)) {
        // ERG/free mode, or a trainer without cadence: speed from power
        const double v = road::speedForPower(model, power);
        m_speed += (v - m_speed) * std::min(1.0, dt / 2.0);
    } else {
        // coasting: roll out against the road resistance
        m_speed = std::clamp(m_speed - road::coastDeceleration(model, m_speed) * dt, 0.0, road::kMaxSpeedMps);
        if (m_speed < 0.3 && model.gradePercent >= 0)
            m_speed = 0;
    }
}

void FreeRideSession::process(int ms)
{
    m_hub->expireStale();
    if (ms <= 0 || !(m_state == State::Ready || active()))
        return;
    if (paused())
        return;   // clock stopped, values frozen
    const double dt = ms / 1000.0;
    updateModel(dt);
    if (running()) {
        m_activeMs += ms;
        m_distance += m_speed * dt;
        m_sinceSendMs += ms;
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
    if (m_lastRideId >= 0)
        emit rideSaved(m_lastRideId);
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
