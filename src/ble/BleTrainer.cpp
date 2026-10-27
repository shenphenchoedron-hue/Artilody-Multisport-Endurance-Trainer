#include "BleTrainer.h"

#include "BleLink.h"
#include "DiagLog.h"
#include "Pm5Protocol.h"

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
} // namespace

BleTrainer::BleTrainer(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : TrainerDevice(parent)
    , m_id(deviceId(info))
    , m_name(info.name().isEmpty() ? QStringLiteral("Smart Trainer") : info.name())
    , m_link(new BleLink(info, uuid16(ftms::kServiceUuid), localAdapter, this))
{
    setRssi(info.rssi());
    m_writeTimeout.setSingleShot(true);
    m_writeTimeout.setInterval(2000);
    connect(&m_writeTimeout, &QTimer::timeout, this, [this] { onWriteFinished(true); });

    m_controlRetry.setSingleShot(true);
    m_controlRetry.setInterval(3000);
    connect(&m_controlRetry, &QTimer::timeout, this, [this] {
        if (!m_hasControl && m_pending)
            requestControl();
    });

    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
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
            m_refusals.reset();
            // re-apply the last command after a reconnect
            restorePendingCommand();
        }
        setState(s);
    });
    connect(m_link, &BleLink::serviceReady, this, &BleTrainer::onServiceReady);
    connect(m_link, &BleLink::characteristicChanged, this, &BleTrainer::onNotification);
    connect(m_link, &BleLink::characteristicRead, this, &BleTrainer::onRead);
    connect(m_link, &BleLink::notificationResult, this, [this](const QBluetoothUuid &c, bool enabled, bool ok) {
        DiagLog::write("FTMS", QStringLiteral("%1: %2 %3 %4").arg(m_name, uuidHex(c),
                                                                  enabled ? QStringLiteral("notifications/indications on")
                                                                          : QStringLiteral("notifications off"),
                                                                  ok ? QStringLiteral("ok") : QStringLiteral("FAILED")));
    });
    connect(m_link, &BleLink::writeFailed, this, [this] {
        DiagLog::write("FTMS", QStringLiteral("CP write REJECTED by the trainer/stack: %1").arg(DiagLog::hex(m_inFlight)));
        if (m_busy)
            onWriteFinished(true);
    });
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &BleTrainer::logMessage);
    connect(m_link, &BleLink::gattDiscovered, this, &BleTrainer::gattReport);
    connect(m_link, &BleLink::serviceMissing, this, [this] {
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
    // Diagnostics only (the diagnostic log): the trainer's ranges and its
    // status / training status notifications. They change no behaviour.
    for (quint16 c : { ftms::kSupportedInclinationRangeUuid, ftms::kSupportedResistanceRangeUuid,
                       ftms::kSupportedPowerRangeUuid })
        if (m_link->hasCharacteristic(uuid16(c)))
            m_link->read(uuid16(c));
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
}

void BleTrainer::onRead(const QBluetoothUuid &uuid, const QByteArray &value)
{
    bool ok16 = false;
    const quint16 short16 = uuid.toUInt16(&ok16);
    if (ok16 && (short16 == ftms::kSupportedInclinationRangeUuid || short16 == ftms::kSupportedResistanceRangeUuid
                 || short16 == ftms::kSupportedPowerRangeUuid)) {
        DiagLog::write("FTMS", QStringLiteral("%1: supported range %2 [%3] = %4")
                                   .arg(m_name, uuidHex(uuid), DiagLog::hex(value), ftms::describeRange(short16, value)));
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
        const quint8 op = opOf(m_queue.at(i));
        if (op == quint8(ftms::OpCode::SetTargetPower) || op == quint8(ftms::OpCode::SetIndoorBikeSimulation)
            || op == quint8(ftms::OpCode::SetTargetResistance))
            m_queue.removeAt(i);
    }
}

void BleTrainer::onWriteFinished(bool failed)
{
    m_writeTimeout.stop();
    const QByteArray cmd = m_inFlight;
    m_inFlight.clear();
    m_busy = false;
    if (failed)
        DiagLog::write("FTMS", QStringLiteral("CP write failed / no indication within %1 ms: %2 [%3]")
                                   .arg(m_writeTimeout.interval())
                                   .arg(ftms::describeControlPoint(cmd), DiagLog::hex(cmd)));
    if (failed && opOf(cmd) == quint8(ftms::OpCode::RequestControl) && !m_hasControl) {
        emit logMessage(QStringLiteral("No answer to trainer control request – retrying"), false);
        scheduleControlRetry();
    }
    sendNext();
}

void BleTrainer::onNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
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
        DiagLog::write("FTMS", QStringLiteral("Machine Status [%1] = %2").arg(DiagLog::hex(value), ftms::describeMachineStatus(value)));
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
            m_queue.prepend(ftms::startOrResume());
            if (m_pending)
                sendLastRequest();
        } else {
            scheduleControlRetry();
        }
    } else if (r->success()) {
        m_refusals.onSuccess(r->requestOpCode);
        if (op == ftms::OpCode::SetIndoorBikeSimulation && simulationSupport() != 1) {
            // features unknown (read failed) but the trainer accepted 0x11
            m_simConfirmed = true;
            emit simulationSupportChanged();
            emit capabilitiesChanged();
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
            markSimulationRejected();
            if (m_lastRequest == Request::FreeRide)
                sendFreeRideCommand();
        } else {
            emit logMessage(QStringLiteral("Trainer command 0x%1 failed: %2")
                                .arg(r->requestOpCode, 2, 16, QLatin1Char('0'))
                                .arg(ftms::resultCodeText(r->result)), false);
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
    enqueue(ftms::setIndoorBikeSimulation(windSpeedMps, gradePercent, crr, cwKgPerM));
}

void BleTrainer::sendFreeRideCommand()
{
    if (simulationSupport() != 0) {
        // Flat road: no wind, 0% grade, the rider's last rolling resistance
        // and drag (default Crr 0.004, Cw = ρ·CdA 0.39 kg/m).
        enqueue(ftms::setIndoorBikeSimulation(0.0, 0.0, m_lastSim.crr, m_lastSim.cw));
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
