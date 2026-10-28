#include "BleTrainer.h"
#include "ZwiftHubLink.h"

#include "BleLink.h"
#include "DiagLog.h"
#include "Pm5Protocol.h"

#include <cmath>

namespace watt {

namespace {
QBluetoothUuid uuid16(quint16 v) { return QBluetoothUuid(v); }

QString deviceId(const QBluetoothDeviceInfo &info)
{
    // macOS/iOS do not expose MAC addresses, only a per-host UUID.
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}

quint8 opOf(const QByteArray &cmd)
{
    return cmd.isEmpty() ? 0xFF : quint8(cmd.at(0));
}

bool isResistanceOp(quint8 op)
{
    return op == quint8(ftms::OpCode::SetTargetPower) || op == quint8(ftms::OpCode::SetIndoorBikeSimulation)
           || op == quint8(ftms::OpCode::SetTargetResistance);
}

// grade (%) of an encoded 0x11 command
double gradeOf(const QByteArray &cmd)
{
    if (cmd.size() < 5 || opOf(cmd) != quint8(ftms::OpCode::SetIndoorBikeSimulation))
        return 0.0;
    const qint16 g = qint16(quint16(quint8(cmd.at(3))) | (quint16(quint8(cmd.at(4))) << 8));
    return g / 100.0;
}

int targetOf(const QByteArray &cmd)
{
    if (cmd.size() < 3 || opOf(cmd) != quint8(ftms::OpCode::SetTargetPower))
        return 0;
    return qint16(quint16(quint8(cmd.at(1))) | (quint16(quint8(cmd.at(2))) << 8));
}

const char *linkStateName(LinkState s)
{
    switch (s) {
    case LinkState::Disconnected: return "disconnected";
    case LinkState::Connecting: return "connecting";
    case LinkState::Connected: return "connected";
    }
    return "?";
}

QString uuidHex(const QBluetoothUuid &uuid)
{
    bool ok = false;
    const quint16 v = uuid.toUInt16(&ok);
    return ok ? QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(4, QLatin1Char('0'))
              : uuid.toString();
}

QString gradeText(double g)
{
    // typographic minus, like the rest of the UI
    return (g < 0 ? QStringLiteral("−") : QString()) + QString::number(std::abs(g), 'f', 2) + QStringLiteral(" %");
}
} // namespace

BleTrainer::BleTrainer(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : TrainerDevice(parent)
    , m_id(deviceId(info))
    , m_name(info.name().isEmpty() ? QStringLiteral("Smart Trainer") : info.name())
    , m_link(new BleLink(info, uuid16(ftms::kServiceUuid), localAdapter, this))
{
    setRssi(info.rssi());
    // diagnostics: list every service of the trainer (Zwift 0xFC82, vendor
    // services), not only FTMS
    static_cast<BleLink *>(m_link)->setInspectOtherServices(true);
    init();
}

BleTrainer::BleTrainer(GattLink *link, const QString &id, const QString &name, QObject *parent)
    : TrainerDevice(parent)
    , m_id(id)
    , m_name(name)
    , m_link(link)
{
    m_link->setParent(this);
    init();
}

void BleTrainer::setTimingForTesting(int writeTimeoutMs, int retryBaseMs, int controlRetryMs)
{
    m_writeTimeout.setInterval(writeTimeoutMs);
    m_retryBaseMs = retryBaseMs;
    m_controlRetry.setInterval(controlRetryMs);
}

void BleTrainer::init()
{
    m_zhub = new ZwiftHubLink(m_link, this);
    connect(m_zhub, &ZwiftHubLink::stateChanged, this, [this] {
        emit zwiftHubChanged();
        emit controlStatusChanged();
        if (m_zhub->active() && m_lastRequest == Request::Hub)
            m_zhub->update(m_lastHub, true);
    });
    connect(m_zhub, &ZwiftHubLink::failed, this, [this](const QString &reason) {
        emit logMessage(QStringLiteral("Zwift protocol failed (%1) – using FTMS gradient simulation").arg(reason), false);
    });
    connect(m_link, &GattLink::otherServicesDiscovered, this, [this] {
        m_zhub->detect();
        maybeStartHub();
    });

    m_writeTimeout.setSingleShot(true);
    m_writeTimeout.setInterval(2000);
    connect(&m_writeTimeout, &QTimer::timeout, this, [this] { onWriteFinished(true); });

    m_controlRetry.setSingleShot(true);
    m_controlRetry.setInterval(3000);
    connect(&m_controlRetry, &QTimer::timeout, this, [this] {
        if (!m_hasControl && (m_pending || m_mode == Mode::NoControl))
            requestControl();
    });

    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (state() != LinkState::Connected)
            return;
        if (!m_retryCommand.isEmpty()) {
            const QByteArray cmd = m_retryCommand;
            m_retryCommand.clear();
            DiagLog::write("FTMS", QStringLiteral("retry %1/%2: %3").arg(m_failStreak).arg(kMaxRetries).arg(ftms::describeControlPoint(cmd)));
            enqueue(cmd);
            return;
        }
        if (m_busy || !m_queue.isEmpty())
            return;   // a newer command is on its way anyway
        DiagLog::write("FTMS", QStringLiteral("retry %1/%2: re-sending the current command").arg(m_failStreak).arg(kMaxRetries));
        sendLastRequest();
    });

    connect(m_link, &GattLink::stateChanged, this, [this](LinkState s) {
        DiagLog::write("FTMS", QStringLiteral("%1 [%2]: link %3").arg(m_name, m_id, QString::fromLatin1(linkStateName(s))));
        if (s != LinkState::Connected) {
            m_ready = false;
            m_hasControl = false;
            m_controlRequested = false;
            m_queue.clear();
            m_inFlight.clear();
            m_busy = false;
            m_writeTimeout.stop();
            m_controlRetry.stop();
            m_retry.stop();
            m_retryCommand.clear();
            m_failStreak = 0;
            m_refusals.reset();
            m_mode = Mode::Unknown;
            m_problem.clear();
            m_zhub->reset();   // detected again after the next discovery
            // re-apply the last command after a reconnect
            restorePendingCommand();
        }
        setState(s);
        emit controlStatusChanged();
    });
    connect(m_link, &GattLink::serviceReady, this, &BleTrainer::onServiceReady);
    connect(m_link, &GattLink::characteristicChanged, this, &BleTrainer::onNotification);
    connect(m_link, &GattLink::characteristicRead, this, &BleTrainer::onRead);
    connect(m_link, &GattLink::notificationResult, this, [this](const QBluetoothUuid &c, bool enabled, bool ok) {
        DiagLog::write("FTMS", QStringLiteral("%1: %2 %3 %4").arg(m_name, uuidHex(c),
                                                                  enabled ? QStringLiteral("notifications/indications on")
                                                                          : QStringLiteral("notifications off"),
                                                                  ok ? QStringLiteral("ok") : QStringLiteral("FAILED")));
    });
    connect(m_link, &GattLink::writeFailed, this, [this] {
        DiagLog::write("FTMS", QStringLiteral("CP write REJECTED by the trainer/stack: %1").arg(DiagLog::hex(m_inFlight)));
        if (m_busy)
            onWriteFinished(true);
    });
    connect(m_link, &GattLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &GattLink::logMessage, this, &BleTrainer::logMessage);
    connect(m_link, &GattLink::gattDiscovered, this, &BleTrainer::gattReport);
    connect(m_link, &GattLink::serviceMissing, this, [this] {
        if (looksLikeRower(m_link->discoveredServices(), false, false)) {
            emit logMessage(QStringLiteral("%1 is a rowing machine (Concept2 rowing service)").arg(m_name), true);
            emit rowerDetected();
        }
    });
}

bool BleTrainer::looksLikeRower(const QList<QBluetoothUuid> &services, bool hasRowerData, bool hasIndoorBikeData)
{
    if (services.contains(QBluetoothUuid(pm5::uuid(pm5::kRowingService))))
        return true;
    return hasRowerData && !hasIndoorBikeData;
}

void BleTrainer::connectDevice()
{
    m_link->setAutoReconnect(m_autoReconnect);
    m_link->connectToDevice();
}

void BleTrainer::disconnectDevice()
{
    m_link->disconnectFromDevice();
}

bool BleTrainer::reconnectNow()
{
    m_link->setAutoReconnect(m_autoReconnect);
    return m_link->reconnectNow();
}

void BleTrainer::setAutoReconnect(bool on)
{
    TrainerDevice::setAutoReconnect(on);
    m_link->setAutoReconnect(on);
}

QStringList BleTrainer::capabilities() const
{
    QStringList caps;
    if (!m_featuresKnown || m_features.powerMeasurement)
        caps << QStringLiteral("Power");
    if (!m_featuresKnown || m_features.cadence)
        caps << QStringLiteral("Cadence");
    if (m_features.powerTarget)
        caps << QStringLiteral("ERG Control");
    if (simulationSupport() == 1)
        caps << QStringLiteral("Gradient Simulation");
    if ((m_features.simulation && !m_simRejected) || m_features.resistanceTarget)
        caps << QStringLiteral("Free Ride");
    if (m_features.heartRate)
        caps << QStringLiteral("Heart Rate");
    return caps;
}

bool BleTrainer::supportsControl() const
{
    return m_link->hasCharacteristic(uuid16(ftms::kControlPointUuid))
           && (!m_featuresKnown || m_features.powerTarget);
}

void BleTrainer::onServiceReady()
{
    if (looksLikeRower(m_link->discoveredServices(), m_link->hasCharacteristic(uuid16(ftms::kRowerDataUuid)),
                       m_link->hasCharacteristic(uuid16(ftms::kIndoorBikeDataUuid)))) {
        emit logMessage(QStringLiteral("%1 is a rowing machine, not a smart trainer – switching to the rower driver").arg(m_name), true);
        emit rowerDetected();
        return;
    }
    emit logMessage(QStringLiteral("FTMS service ready on %1").arg(m_name), true);
    {
        QStringList present;
        for (quint16 c : { ftms::kFeatureUuid, ftms::kIndoorBikeDataUuid, ftms::kTrainingStatusUuid,
                           ftms::kSupportedInclinationRangeUuid, ftms::kSupportedResistanceRangeUuid,
                           ftms::kSupportedPowerRangeUuid, ftms::kControlPointUuid, ftms::kMachineStatusUuid })
            present << QStringLiteral("%1%2").arg(uuidHex(uuid16(c)), m_link->hasCharacteristic(uuid16(c)) ? QString()
                                                                                                          : QStringLiteral(" (absent)"));
        DiagLog::write("FTMS", QStringLiteral("%1 [%2]: FTMS service ready, characteristics %3")
                                   .arg(m_name, m_id, present.join(QStringLiteral(", "))));
    }
    m_link->read(uuid16(ftms::kFeatureUuid));
    // The ranges: the inclination range limits the grades we send (0x11);
    // the others are for the diagnostic log only.
    for (quint16 c : { ftms::kSupportedInclinationRangeUuid, ftms::kSupportedResistanceRangeUuid,
                       ftms::kSupportedPowerRangeUuid })
        if (m_link->hasCharacteristic(uuid16(c)))
            m_link->read(uuid16(c));
    // Missing optional characteristics (Jetblack Victory: no 0x2AD5, no
    // 0x2AD3): noted once, nothing waits for them.
    m_inclinationAbsent = !m_link->hasCharacteristic(uuid16(ftms::kSupportedInclinationRangeUuid));
    if (m_inclinationAbsent)
        DiagLog::write("FTMS", QStringLiteral("%1: no Supported Inclination Range (0x2AD5) – the trainer doesn't report its gradient "
                                              "range; grades are sent unclamped (−10 … 20 %)").arg(m_name));
    if (!m_link->hasCharacteristic(uuid16(ftms::kTrainingStatusUuid)))
        DiagLog::write("FTMS", QStringLiteral("%1: no Training Status (0x2AD3) – not needed").arg(m_name));
    emit controlStatusChanged();
    if (!m_link->enableNotifications(uuid16(ftms::kIndoorBikeDataUuid)))
        emit logMessage(QStringLiteral("Indoor Bike Data not available"), false);
    if (m_link->hasCharacteristic(uuid16(ftms::kMachineStatusUuid)))
        m_link->enableNotifications(uuid16(ftms::kMachineStatusUuid));
    if (m_link->hasCharacteristic(uuid16(ftms::kTrainingStatusUuid)))
        m_link->enableNotifications(uuid16(ftms::kTrainingStatusUuid));
    if (m_link->enableNotifications(uuid16(ftms::kControlPointUuid), true)) {
        m_ready = true;
        requestControl();
    }
    emit capabilitiesChanged();
    if (m_link->otherServicesInspected()) {
        m_zhub->detect();
        maybeStartHub();
    }
}

// --- Zwift hub protocol ------------------------------------------------------------------

void BleTrainer::maybeStartHub()
{
    if (!m_zhubEnabled || !m_ready || state() != LinkState::Connected)
        return;
    if (m_zhub->state() == ZwiftHubLink::State::Available) {
        emit logMessage(QStringLiteral("Zwift Click connected and %1 has the Zwift trainer service – starting the Zwift protocol").arg(m_name), true);
        m_zhub->start();
    }
}

int BleTrainer::zwiftHubState() const
{
    return state() == LinkState::Connected ? m_zhub->hubState() : int(SensorHub::HubNone);
}

QString BleTrainer::zwiftHubStatus() const
{
    return m_zhub->failureReason();
}

void BleTrainer::setZwiftHubEnabled(bool on)
{
    if (m_zhubEnabled == on)
        return;
    m_zhubEnabled = on;
    DiagLog::write("ZHUB", QStringLiteral("%1: Zwift protocol %2 (virtual shifting %3)")
                               .arg(m_name, on ? QStringLiteral("wanted") : QStringLiteral("not wanted"),
                                    on ? QStringLiteral("active: Zwift Click connected")
                                       : QStringLiteral("off: no Zwift Click – FTMS only"))
                               + QStringLiteral(", hub state %1").arg(m_zhub->stateText()));
    if (on) {
        if (m_zhub->state() == ZwiftHubLink::State::Failed)
            m_zhub->stop();   // the Click came back: try again
        maybeStartHub();
    } else {
        m_zhub->stop();
    }
}

void BleTrainer::setHubSimulation(const HubSimulation &p)
{
    m_lastRequest = Request::Hub;
    m_lastHub = p;
    // no FTMS resistance command while the hub path is active
    dropQueuedControlCommands();
    if (!m_hasControl) {
        m_pending = true;
        requestControl();   // FTMS control is still held (start / data)
    } else {
        m_pending = false;
    }
    if (m_zhub->active()) {
        m_hubIgnoredLogged = false;
        m_zhub->update(p);
    } else if (!m_hubIgnoredLogged) {
        m_hubIgnoredLogged = true;
        DiagLog::write("ZHUB", QStringLiteral("hub simulation requested but the Zwift protocol is %1 – nothing sent "
                                              "(the session switches to FTMS gradient)").arg(m_zhub->stateText()));
    }
}

void BleTrainer::onRead(const QBluetoothUuid &uuid, const QByteArray &value)
{
    bool ok16 = false;
    const quint16 short16 = uuid.toUInt16(&ok16);
    if (ok16 && (short16 == ftms::kSupportedInclinationRangeUuid || short16 == ftms::kSupportedResistanceRangeUuid
                 || short16 == ftms::kSupportedPowerRangeUuid)) {
        DiagLog::write("FTMS", QStringLiteral("%1: supported range %2 [%3] = %4")
                                   .arg(m_name, uuidHex(uuid), DiagLog::hex(value), ftms::describeRange(short16, value)));
        if (short16 == ftms::kSupportedInclinationRangeUuid) {
            m_inclination = ftms::parseInclinationRange(value);
            if (!m_inclination) {
                DiagLog::write("FTMS", QStringLiteral("%1: Supported Inclination Range unreadable – ignored").arg(m_name));
            } else if (m_inclination->noDescents()) {
                DiagLog::write("FTMS", QStringLiteral("%1: inclination range %2 … %3 %: the trainer does NOT simulate descents "
                                                      "(negative grades are sent as %2 %)")
                                           .arg(m_name)
                                           .arg(m_inclination->minimumPercent, 0, 'f', 1)
                                           .arg(m_inclination->maximumPercent, 0, 'f', 1));
                emit logMessage(QStringLiteral("%1 doesn't simulate descents below 0 %").arg(m_name), false);
            } else {
                DiagLog::write("FTMS", QStringLiteral("%1: grades sent with 0x11 are clamped to %2 … %3 %")
                                           .arg(m_name)
                                           .arg(m_inclination->minimumPercent, 0, 'f', 1)
                                           .arg(m_inclination->maximumPercent, 0, 'f', 1));
            }
            emit controlStatusChanged();
            // a grade outside the range may already have gone out: send it again clamped
            if (m_inclination && m_hasControl && m_lastRequest == Request::Simulation
                && m_inclination->clamp(m_lastSim.grade) != m_lastSim.grade)
                sendLastRequest();
        }
        return;
    }
    if (uuid == uuid16(ftms::kTrainingStatusUuid)) {
        DiagLog::write("FTMS", QStringLiteral("Training Status [%1] = %2").arg(DiagLog::hex(value), ftms::describeTrainingStatus(value)));
        return;
    }
    if (uuid == uuid16(ftms::kFeatureUuid)) {
        DiagLog::write("FTMS", QStringLiteral("%1: Fitness Machine Feature 0x2ACC [%2] = %3")
                                   .arg(m_name, DiagLog::hex(value), ftms::describeFeatures(value)));
        if (auto f = ftms::parseFeatures(value)) {
            m_features = *f;
            m_featuresKnown = true;
            emit logMessage(QStringLiteral("FTMS capabilities detected%1")
                                .arg(f->simulation ? QString() : QStringLiteral(" (no gradient simulation)")), true);
            emit capabilitiesChanged();
            emit simulationSupportChanged();
        } else {
            // Support stays "unknown": the first 0x11 is still sent and its
            // answer ("Op Code not supported" or success) decides.
            emit logMessage(QStringLiteral("%1: FTMS features unreadable (%2 bytes) – gradient simulation will be tried")
                                .arg(m_name).arg(value.size()), false);
        }
    }
}

// --- trainer mode / status --------------------------------------------------------------

QString BleTrainer::modeName(Mode m)
{
    switch (m) {
    case Mode::Unknown: return QStringLiteral("unknown");
    case Mode::Simulation: return QStringLiteral("simulation");
    case Mode::Erg: return QStringLiteral("erg");
    case Mode::Resistance: return QStringLiteral("resistance");
    case Mode::Stopped: return QStringLiteral("stopped");
    case Mode::NoControl: return QStringLiteral("no control");
    }
    return QStringLiteral("unknown");
}

void BleTrainer::setMode(Mode m)
{
    if (m == m_mode)
        return;
    DiagLog::write("FTMS", QStringLiteral("trainer mode: %1 -> %2").arg(modeName(m_mode), modeName(m)));
    m_mode = m;
    emit controlStatusChanged();
}

void BleTrainer::setProblem(const QString &text)
{
    if (text == m_problem)
        return;
    m_problem = text;
    if (!text.isEmpty()) {
        DiagLog::write("FTMS", QStringLiteral("PROBLEM: %1").arg(text));
        emit logMessage(text, false);
    } else {
        DiagLog::write("FTMS", QStringLiteral("problem cleared"));
    }
    emit controlStatusChanged();
}

BleTrainer::Mode BleTrainer::expectedMode() const
{
    switch (m_lastRequest) {
    case Request::None: return Mode::Unknown;
    case Request::Target: return Mode::Erg;
    case Request::Simulation: return Mode::Simulation;
    case Request::FreeRide: return simulationSupport() != 0 ? Mode::Simulation : Mode::Resistance;
    case Request::Hub: return Mode::Unknown;   // the hub protocol has no FTMS status
    }
    return Mode::Unknown;
}

QString BleTrainer::trainerMode() const
{
    if (state() != LinkState::Connected)
        return QString();
    if (m_lastRequest == Request::Hub && m_zhub->active())
        return QStringLiteral("zwift");
    return modeName(m_mode);
}

bool BleTrainer::controlProblem() const
{
    if (state() != LinkState::Connected)
        return false;
    if (!m_problem.isEmpty())
        return true;
    // the trainer reported another mode than the one asked for (and nothing is on its way)
    const Mode want = expectedMode();
    return want != Mode::Unknown && m_mode != Mode::Unknown && m_mode != want && !m_busy && m_queue.isEmpty()
           && m_hasControl;
}

QString BleTrainer::controlStatus() const
{
    if (state() != LinkState::Connected)
        return QString();
    if (!m_problem.isEmpty())
        return m_problem;
    if (m_lastRequest == Request::Hub && m_zhub->active())
        return QStringLiteral("Zwift protocol – trainer simulates %1 %, gear ratio %2")
            .arg(m_lastHub.inclinePercent, 0, 'f', 1)
            .arg(m_lastHub.gearRatio, 0, 'f', 2);
    if (!m_hasControl)
        return m_mode == Mode::NoControl ? QStringLiteral("Trainer control lost – requesting it again")
                                         : QStringLiteral("Waiting for trainer control…");
    const Mode want = expectedMode();
    if (controlProblem())
        return QStringLiteral("Trainer is in %1 mode, not %2 – sending the command again")
            .arg(modeName(m_mode), modeName(want));
    switch (m_mode) {
    case Mode::Simulation: {
        QString s = QStringLiteral("Simulation – trainer accepted %1").arg(gradeText(m_acceptedGrade));
        if (m_lastRequest == Request::Simulation && std::abs(m_lastSim.grade - m_acceptedGrade) > 0.005
            && m_inclination && m_inclination->clamp(m_lastSim.grade) == m_acceptedGrade)
            s += QStringLiteral(" (wanted %1, outside its range)").arg(gradeText(m_lastSim.grade));
        return s;
    }
    case Mode::Erg: return QStringLiteral("ERG – trainer accepted %1 W").arg(m_acceptedTarget);
    case Mode::Resistance: return QStringLiteral("Resistance mode");
    case Mode::Stopped: return QStringLiteral("Trainer stopped / paused");
    case Mode::NoControl: return QStringLiteral("Trainer control lost");
    case Mode::Unknown: break;
    }
    if (want == Mode::Simulation)
        return QStringLiteral("Waiting for the trainer to accept the gradient command");
    if (want == Mode::Erg)
        return QStringLiteral("Waiting for the trainer to accept the target power");
    return QStringLiteral("Connected – no command sent yet");
}

QString BleTrainer::inclinationNote() const
{
    if (state() != LinkState::Connected)
        return QString();
    if (!m_inclination && m_inclinationAbsent)
        return QStringLiteral("Trainer doesn't report its gradient range");
    if (!m_inclination || !m_inclination->noDescents())
        return QString();
    return QStringLiteral("This trainer doesn't simulate descents below %1 % (its supported inclination: %2 … %3 %)")
        .arg(m_inclination->minimumPercent, 0, 'f', 0)
        .arg(m_inclination->minimumPercent, 0, 'f', 1)
        .arg(m_inclination->maximumPercent, 0, 'f', 1);
}

double BleTrainer::clampGrade(double grade)
{
    if (!m_inclination || !std::isfinite(grade))
        return grade;
    const double c = m_inclination->clamp(grade);
    if (c != grade) {
        if (!m_clampLogged || std::abs(grade - m_lastClampedFrom) >= 1.0) {
            DiagLog::write("FTMS", QStringLiteral("grade %1 % clamped to %2 % (trainer's supported inclination %3 … %4 %)")
                                       .arg(grade, 0, 'f', 2)
                                       .arg(c, 0, 'f', 2)
                                       .arg(m_inclination->minimumPercent, 0, 'f', 1)
                                       .arg(m_inclination->maximumPercent, 0, 'f', 1));
            m_clampLogged = true;
            m_lastClampedFrom = grade;
        }
    } else if (m_clampLogged) {
        m_clampLogged = false;
        DiagLog::write("FTMS", QStringLiteral("grade %1 % inside the trainer's range again").arg(grade, 0, 'f', 2));
    }
    return c;
}

void BleTrainer::reacquireControl(const QString &why)
{
    m_hasControl = false;
    m_controlRequested = false;
    m_queue.clear();
    restorePendingCommand();
    setMode(Mode::NoControl);
    DiagLog::write("FTMS", QStringLiteral("%1 – requesting control again and re-sending the current command").arg(why));
    // a trainer that keeps taking control away must not make us spam it
    if (m_reacquireClock.isValid() && m_reacquireClock.elapsed() < 2000) {
        scheduleControlRetry();
        return;
    }
    m_reacquireClock.start();
    requestControl();
}

void BleTrainer::onMachineStatus(const QByteArray &value)
{
    DiagLog::write("FTMS", QStringLiteral("Machine Status [%1] = %2").arg(DiagLog::hex(value), ftms::describeMachineStatus(value)));
    if (value.isEmpty())
        return;
    const auto op = ftms::MachineStatus(quint8(value.at(0)));
    switch (op) {
    case ftms::MachineStatus::ControlPermissionLost:
        emit logMessage(QStringLiteral("%1 reported: control permission lost – requesting control again").arg(m_name), false);
        reacquireControl(QStringLiteral("status: control permission lost"));
        return;
    case ftms::MachineStatus::Reset:
        // after a reset the trainer forgets the controlling client
        reacquireControl(QStringLiteral("status: reset"));
        setMode(Mode::Unknown);
        return;
    case ftms::MachineStatus::StoppedOrPausedByUser:
    case ftms::MachineStatus::StoppedBySafetyKey:
        setMode(Mode::Stopped);
        if (m_hasControl && m_lastRequest != Request::None
            && (!m_modeRepairClock.isValid() || m_modeRepairClock.elapsed() >= kModeRepairMs)) {
            m_modeRepairClock.start();
            DiagLog::write("FTMS", QStringLiteral("trainer stopped – sending start and the current command again"));
            enqueue(ftms::startOrResume());
            sendLastRequest();
        }
        return;
    case ftms::MachineStatus::StartedOrResumedByUser:
        if (m_mode == Mode::Stopped)
            setMode(Mode::Unknown);
        return;
    case ftms::MachineStatus::TargetResistanceChanged: setMode(Mode::Resistance); break;
    case ftms::MachineStatus::TargetPowerChanged:
        if (value.size() >= 3)
            m_acceptedTarget = qint16(quint16(quint8(value.at(1))) | (quint16(quint8(value.at(2))) << 8));
        setMode(Mode::Erg);
        break;
    case ftms::MachineStatus::SimulationParametersChanged:
        if (value.size() >= 5)
            m_acceptedGrade = qint16(quint16(quint8(value.at(3))) | (quint16(quint8(value.at(4))) << 8)) / 100.0;
        if (m_mode == Mode::Simulation)
            emit controlStatusChanged();
        setMode(Mode::Simulation);
        break;
    default:
        return;
    }
    // the trainer reports another mode than we asked for (another app, its
    // own buttons): ask once more, at most every kModeRepairMs
    const Mode want = expectedMode();
    if (want != Mode::Unknown && m_mode != want && m_hasControl && !m_busy && m_queue.isEmpty()
        && (!m_modeRepairClock.isValid() || m_modeRepairClock.elapsed() >= kModeRepairMs)) {
        m_modeRepairClock.start();
        DiagLog::write("FTMS", QStringLiteral("status shows %1 mode but %2 was requested – re-sending the current command")
                                   .arg(modeName(m_mode), modeName(want)));
        sendLastRequest();
    }
}

// --- control point ------------------------------------------------------------------------

void BleTrainer::requestControl()
{
    if (!m_ready || m_controlRequested || m_hasControl || state() != LinkState::Connected)
        return;
    m_controlRequested = true;
    DiagLog::write("FTMS", QStringLiteral("requesting control"));
    m_queue.prepend(ftms::requestControl());
    sendNext();
}

void BleTrainer::scheduleControlRetry()
{
    m_controlRequested = false;
    if (!m_controlRetry.isActive())
        m_controlRetry.start();
}

void BleTrainer::restorePendingCommand()
{
    if (m_lastRequest != Request::None)
        m_pending = true;
}

void BleTrainer::sendLastRequest()
{
    m_pending = false;
    switch (m_lastRequest) {
    case Request::None: break;
    case Request::Target: setTargetPower(m_lastTarget); break;
    case Request::FreeRide: setFreeRide(); break;
    case Request::Simulation:
        setSimulation(m_lastSim.grade, m_lastSim.wind, m_lastSim.crr, m_lastSim.cw);
        break;
    case Request::Hub:
        if (m_zhub->active())
            m_zhub->update(m_lastHub, true);
        break;
    }
}

int BleTrainer::simulationSupport() const
{
    if (m_simRejected)
        return 0;
    if (m_simConfirmed)
        return 1;
    if (m_featuresKnown)
        return m_features.simulation ? 1 : 0;
    return -1;
}

void BleTrainer::markSimulationRejected()
{
    if (m_simRejected)
        return;
    m_simRejected = true;
    emit logMessage(QStringLiteral("%1 does not support gradient simulation").arg(m_name), false);
    emit simulationSupportChanged();
    emit capabilitiesChanged();
}

void BleTrainer::dropQueuedControlCommands()
{
    // Only the latest resistance command matters: drop queued, unsent ones.
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (isResistanceOp(opOf(m_queue.at(i))))
            m_queue.removeAt(i);
    }
}

void BleTrainer::onCommandFailed(const QByteArray &cmd, const QString &problem)
{
    const quint8 op = opOf(cmd);
    setProblem(problem);
    if (op == quint8(ftms::OpCode::RequestControl))
        return;   // the control retry timer handles it
    ++m_failStreak;
    if (m_failStreak > kMaxRetries) {
        DiagLog::write("FTMS", QStringLiteral("%1 failed %2x in a row – no more automatic retries (the next command is sent as usual)")
                                   .arg(ftms::commandNoun(op))
                                   .arg(m_failStreak));
        return;
    }
    const int delay = m_retryBaseMs * (1 << (m_failStreak - 1));
    m_retryCommand = isResistanceOp(op) ? QByteArray() : cmd;
    DiagLog::write("FTMS", QStringLiteral("retry %1/%2 of the %3 in %4 ms").arg(m_failStreak).arg(kMaxRetries)
                               .arg(ftms::commandNoun(op)).arg(delay));
    m_retry.start(delay);
}

void BleTrainer::onWriteFinished(bool failed)
{
    m_writeTimeout.stop();
    const QByteArray cmd = m_inFlight;
    m_inFlight.clear();
    m_busy = false;
    if (failed) {
        DiagLog::write("FTMS", QStringLiteral("CP write failed / no indication within %1 ms: %2 [%3]")
                                   .arg(m_writeTimeout.interval())
                                   .arg(ftms::describeControlPoint(cmd), DiagLog::hex(cmd)));
        if (!cmd.isEmpty())
            onCommandFailed(cmd, ftms::timeoutText(opOf(cmd), m_writeTimeout.interval()));
    }
    if (failed && opOf(cmd) == quint8(ftms::OpCode::RequestControl) && !m_hasControl) {
        emit logMessage(QStringLiteral("No answer to trainer control request – retrying"), false);
        scheduleControlRetry();
    }
    sendNext();
}

void BleTrainer::onNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    if (m_zhub->handleNotification(uuid, value))
        return;   // Zwift service (0002 / 0004)
    if (uuid == uuid16(ftms::kIndoorBikeDataUuid)) {
        const auto d = ftms::parseIndoorBikeData(value);
        if (!d) {
            DiagLog::write("IBD", QStringLiteral("malformed Indoor Bike Data [%1]").arg(DiagLog::hex(value)));
            return;
        }
        // at most once per second in the log
        if (DiagLog::instance().isActive() && (!m_ibdLogClock.isValid() || m_ibdLogClock.elapsed() >= 1000)) {
            m_ibdLogClock.start();
            DiagLog::write("IBD", QStringLiteral("%1 [%2]").arg(ftms::describeIndoorBikeData(*d), DiagLog::hex(value)));
        }
        if (d->powerW)
            emit powerMeasured(*d->powerW);
        if (d->cadenceRpm)
            emit cadenceMeasured(*d->cadenceRpm);
        if (d->speedKmh)
            emit speedMeasured(*d->speedKmh);
        if (d->heartRateBpm && *d->heartRateBpm > 0)
            emit heartRateMeasured(*d->heartRateBpm);
        return;
    }
    if (uuid == uuid16(ftms::kMachineStatusUuid)) {
        onMachineStatus(value);
        return;
    }
    if (uuid == uuid16(ftms::kTrainingStatusUuid)) {
        DiagLog::write("FTMS", QStringLiteral("Training Status [%1] = %2").arg(DiagLog::hex(value), ftms::describeTrainingStatus(value)));
        return;
    }
    if (uuid != uuid16(ftms::kControlPointUuid))
        return;

    DiagLog::write("FTMS", QStringLiteral("CP indication [%1] = %2").arg(DiagLog::hex(value), ftms::describeControlPointResponse(value)));
    const auto r = ftms::parseControlPointResponse(value);
    if (!r)
        return;
    m_writeTimeout.stop();
    const QByteArray cmd = opOf(m_inFlight) == r->requestOpCode ? m_inFlight : QByteArray();
    m_inFlight.clear();
    m_busy = false;
    const auto op = ftms::OpCode(r->requestOpCode);

    if (op == ftms::OpCode::RequestControl) {
        m_controlRequested = false;
        m_hasControl = r->success();
        emit logMessage(m_hasControl ? QStringLiteral("Trainer control granted")
                                     : QStringLiteral("Trainer control refused: %1").arg(ftms::resultCodeText(r->result)),
                        m_hasControl);
        if (m_hasControl) {
            if (m_mode == Mode::NoControl)
                setMode(Mode::Unknown);
            // problems that were about control are over now
            if (m_problem.startsWith(QLatin1String("Trainer refused control"))
                || m_problem.endsWith(QStringLiteral("(%1)").arg(ftms::resultCodeText(quint8(ftms::ResultCode::ControlNotPermitted))))
                || m_problem.contains(ftms::commandNoun(r->requestOpCode)))
                setProblem(QString());
            m_queue.prepend(ftms::startOrResume());
            if (m_pending)
                sendLastRequest();
            else if (m_lastRequest == Request::None)
                sendNeutralState();   // no session command yet: a defined start state
        } else {
            setProblem(ftms::rejectionText(r->requestOpCode, r->result));
            scheduleControlRetry();
        }
        emit controlStatusChanged();
    } else if (r->success()) {
        m_refusals.onSuccess(r->requestOpCode);
        if (isResistanceOp(r->requestOpCode) || opOf(cmd) == quint8(ftms::OpCode::StartOrResume)) {
            m_failStreak = 0;
            m_retry.stop();
            m_retryCommand.clear();
            setProblem(QString());
        }
        if (op == ftms::OpCode::SetIndoorBikeSimulation) {
            if (!cmd.isEmpty())
                m_acceptedGrade = gradeOf(cmd);
            if (m_mode == Mode::Simulation)
                emit controlStatusChanged();
            setMode(Mode::Simulation);
            if (simulationSupport() != 1) {
                // features unknown (read failed) but the trainer accepted 0x11
                m_simConfirmed = true;
                emit simulationSupportChanged();
                emit capabilitiesChanged();
            }
        } else if (op == ftms::OpCode::SetTargetPower) {
            if (!cmd.isEmpty())
                m_acceptedTarget = targetOf(cmd);
            if (m_mode == Mode::Erg)
                emit controlStatusChanged();
            setMode(Mode::Erg);
        } else if (op == ftms::OpCode::SetTargetResistance) {
            setMode(Mode::Resistance);
        } else if (op == ftms::OpCode::StartOrResume && m_mode == Mode::Stopped) {
            setMode(Mode::Unknown);
        }
    } else {
        if (r->result == quint8(ftms::ResultCode::ControlNotPermitted)) {
            // Control was lost (e.g. another app took it): request it again and
            // re-send the last command once it is granted. A trainer that
            // grants control and keeps refusing the command would loop here
            // without pause: after ControlRefusalGuard::kMaxImmediate
            // refusals of the same command, retry only every 3 s.
            m_hasControl = false;
            m_controlRequested = false;
            m_queue.clear();
            restorePendingCommand();
            setMode(Mode::NoControl);
            setProblem(ftms::rejectionText(r->requestOpCode, r->result));
            const auto action = m_refusals.onNotPermitted(r->requestOpCode);
            DiagLog::write("FTMS", QStringLiteral("ControlRefusalGuard: command 0x%1 refused (control not permitted) %2x in a row -> %3")
                                       .arg(r->requestOpCode, 2, 16, QLatin1Char('0'))
                                       .arg(m_refusals.count())
                                       .arg(action == ftms::ControlRefusalGuard::Action::RequestNow
                                                ? QStringLiteral("request control now")
                                                : QStringLiteral("retry in %1 s").arg(m_controlRetry.interval() / 1000)));
            if (action == ftms::ControlRefusalGuard::Action::RequestNow) {
                requestControl();
            } else {
                if (m_refusals.count() == ftms::ControlRefusalGuard::kMaxImmediate + 1)
                    emit logMessage(QStringLiteral("%1 keeps refusing command 0x%2 (control not permitted) – "
                                                   "retrying every %3 s")
                                        .arg(m_name)
                                        .arg(r->requestOpCode, 2, 16, QLatin1Char('0'))
                                        .arg(m_controlRetry.interval() / 1000), false);
                scheduleControlRetry();
            }
        } else if (op == ftms::OpCode::SetIndoorBikeSimulation
                   && r->result == quint8(ftms::ResultCode::NotSupported)) {
            // No gradient simulation: the app falls back (virtual gears via
            // ERG); a free ride uses a resistance level instead.
            setProblem(ftms::rejectionText(r->requestOpCode, r->result));
            markSimulationRejected();
            // free ride / the neutral start state: a resistance level instead
            if (m_lastRequest == Request::FreeRide || m_lastRequest == Request::None)
                sendFreeRideCommand();
        } else {
            // Invalid parameter / operation failed (or an unknown code):
            // shown, retried with backoff
            emit logMessage(QStringLiteral("Trainer command 0x%1 failed: %2")
                                .arg(r->requestOpCode, 2, 16, QLatin1Char('0'))
                                .arg(ftms::resultCodeText(r->result)), false);
            onCommandFailed(cmd.isEmpty() ? QByteArray(1, char(r->requestOpCode)) : cmd,
                            ftms::rejectionText(r->requestOpCode, r->result));
        }
    }
    sendNext();
}

void BleTrainer::setTargetPower(int watts)
{
    m_lastRequest = Request::Target;
    m_lastTarget = watts;
    if (!m_hasControl) {
        m_pending = true;
        requestControl();   // no-op until the control point is ready
        return;
    }
    m_pending = false;
    dropQueuedControlCommands();
    enqueue(ftms::setTargetPower(watts));
}

void BleTrainer::setFreeRide()
{
    m_lastRequest = Request::FreeRide;
    if (!m_hasControl) {
        m_pending = true;
        requestControl();
        return;
    }
    m_pending = false;
    dropQueuedControlCommands();
    sendFreeRideCommand();
}

void BleTrainer::setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM)
{
    m_lastRequest = Request::Simulation;
    m_lastSim = { gradePercent, windSpeedMps, crr, cwKgPerM };
    if (simulationSupport() == 0) {
        DiagLog::write("FTMS", QStringLiteral("0x11 not sent (gradient simulation not supported): grade %1 %")
                                   .arg(gradePercent, 0, 'f', 2));
        return;   // the session falls back to virtual gears (ERG)
    }
    if (!m_hasControl) {
        m_pending = true;
        requestControl();
        return;
    }
    m_pending = false;
    dropQueuedControlCommands();
    enqueue(ftms::setIndoorBikeSimulation(windSpeedMps, clampGrade(gradePercent), crr, cwKgPerM));
}

void BleTrainer::sendNeutralState()
{
    // Control was granted but no session has asked for anything yet: another
    // app's ERG target (or grade) must not linger. The same as a free ride
    // (flat 0 % with our Crr / Cw, or a low resistance level without
    // simulation); m_lastRequest stays None, so the first session command
    // replaces it (dropQueuedControlCommands(): latest wins).
    const bool sim = simulationSupport() != 0;
    DiagLog::write("FTMS", QStringLiteral("neutral state sent: %1 (control granted, no session command pending)")
                               .arg(sim ? QStringLiteral("simulation 0 % flat, Crr %1, Cw %2 kg/m")
                                              .arg(m_lastSim.crr, 0, 'f', 4)
                                              .arg(m_lastSim.cw, 0, 'f', 2)
                                        : m_features.resistanceTarget ? QStringLiteral("target resistance 10 (no gradient simulation)")
                                                                      : QStringLiteral("start only (no simulation, no resistance target)")));
    emit logMessage(QStringLiteral("%1: neutral state sent (%2)")
                        .arg(m_name, sim ? QStringLiteral("flat road 0 %") : QStringLiteral("free ride")), true);
    sendFreeRideCommand();
}

void BleTrainer::sendFreeRideCommand()
{
    if (simulationSupport() != 0) {
        // Flat road: no wind, 0% grade, the rider's last rolling resistance
        // and drag (default Crr 0.004, Cw = ρ·CdA 0.39 kg/m).
        enqueue(ftms::setIndoorBikeSimulation(0.0, clampGrade(0.0), m_lastSim.crr, m_lastSim.cw));
    } else if (m_features.resistanceTarget) {
        enqueue(ftms::setTargetResistance(10.0));
    }
}

void BleTrainer::enqueue(const QByteArray &command)
{
    m_queue.enqueue(command);
    sendNext();
}

void BleTrainer::sendNext()
{
    if (m_busy || m_queue.isEmpty() || !m_ready || state() != LinkState::Connected)
        return;
    const QByteArray cmd = m_queue.dequeue();
    DiagLog::write("FTMS", QStringLiteral("CP write [%1] = %2").arg(DiagLog::hex(cmd), ftms::describeControlPoint(cmd)));
    if (m_link->write(uuid16(ftms::kControlPointUuid), cmd, true)) {
        m_inFlight = cmd;
        m_busy = true;
        m_writeTimeout.start();
    } else {
        DiagLog::write("FTMS", QStringLiteral("CP write could not be queued (link / characteristic not ready)"));
        if (opOf(cmd) == quint8(ftms::OpCode::RequestControl))
            scheduleControlRetry();
    }
}

} // namespace watt
