#include "BleTrainer.h"

#include "BleLink.h"

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
        if (!m_hasControl && (m_pendingTarget >= 0 || m_pendingFreeRide))
            requestControl();
    });

    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
        if (s != LinkState::Connected) {
            m_ready = false;
            m_hasControl = false;
            m_controlRequested = false;
            m_queue.clear();
            m_inFlight.clear();
            m_busy = false;
            m_writeTimeout.stop();
            m_controlRetry.stop();
            // re-apply the last command after a reconnect
            restorePendingCommand();
        }
        setState(s);
    });
    connect(m_link, &BleLink::serviceReady, this, &BleTrainer::onServiceReady);
    connect(m_link, &BleLink::characteristicChanged, this, &BleTrainer::onNotification);
    connect(m_link, &BleLink::characteristicRead, this, &BleTrainer::onRead);
    connect(m_link, &BleLink::writeFailed, this, [this] {
        if (m_busy)
            onWriteFinished(true);
    });
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &BleTrainer::logMessage);
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
    if (m_features.simulation || m_features.resistanceTarget)
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
    emit logMessage(QStringLiteral("FTMS service ready on %1").arg(m_name), true);
    m_link->read(uuid16(ftms::kFeatureUuid));
    if (!m_link->enableNotifications(uuid16(ftms::kIndoorBikeDataUuid)))
        emit logMessage(QStringLiteral("Indoor Bike Data not available"), false);
    if (m_link->enableNotifications(uuid16(ftms::kControlPointUuid), true)) {
        m_ready = true;
        requestControl();
    }
    emit capabilitiesChanged();
}

void BleTrainer::onRead(const QBluetoothUuid &uuid, const QByteArray &value)
{
    if (uuid == uuid16(ftms::kFeatureUuid)) {
        if (auto f = ftms::parseFeatures(value)) {
            m_features = *f;
            m_featuresKnown = true;
            emit logMessage(QStringLiteral("FTMS capabilities detected"), true);
            emit capabilitiesChanged();
        }
    }
}

void BleTrainer::requestControl()
{
    if (!m_ready || m_controlRequested || m_hasControl || state() != LinkState::Connected)
        return;
    m_controlRequested = true;
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
    if (m_lastWasFreeRide) {
        m_pendingFreeRide = true;
        m_pendingTarget = -1;
    } else if (m_lastTarget >= 0) {
        m_pendingTarget = m_lastTarget;
        m_pendingFreeRide = false;
    }
}

void BleTrainer::onWriteFinished(bool failed)
{
    m_writeTimeout.stop();
    const QByteArray cmd = m_inFlight;
    m_inFlight.clear();
    m_busy = false;
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
        if (!d)
            return;
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
    if (uuid != uuid16(ftms::kControlPointUuid))
        return;

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
            if (m_pendingTarget >= 0) {
                const int w = m_pendingTarget;
                m_pendingTarget = -1;
                setTargetPower(w);
            } else if (m_pendingFreeRide) {
                setFreeRide();
            }
        } else {
            scheduleControlRetry();
        }
    } else if (!r->success()) {
        if (r->result == quint8(ftms::ResultCode::ControlNotPermitted)) {
            // Control was lost (e.g. another app took it): request it again and
            // re-send the last command once it is granted.
            m_hasControl = false;
            m_controlRequested = false;
            m_queue.clear();
            restorePendingCommand();
            requestControl();
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
    m_lastTarget = watts;
    m_lastWasFreeRide = false;
    m_pendingFreeRide = false;
    if (!m_hasControl) {
        m_pendingTarget = watts;
        requestControl();   // no-op until the control point is ready
        return;
    }
    // Only the latest target matters: drop queued, unsent targets.
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (opOf(m_queue.at(i)) == quint8(ftms::OpCode::SetTargetPower))
            m_queue.removeAt(i);
    }
    enqueue(ftms::setTargetPower(watts));
}

void BleTrainer::setFreeRide()
{
    m_lastWasFreeRide = true;
    m_pendingTarget = -1;
    if (!m_hasControl) {
        m_pendingFreeRide = true;
        requestControl();
        return;
    }
    m_pendingFreeRide = false;
    sendFreeRideCommand();
}

void BleTrainer::sendFreeRideCommand()
{
    if (!m_featuresKnown || m_features.simulation) {
        // Flat road: no wind, 0% grade, typical rolling resistance and drag.
        enqueue(ftms::setIndoorBikeSimulation(0.0, 0.0, 0.004, 0.51));
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
    if (m_link->write(uuid16(ftms::kControlPointUuid), cmd, true)) {
        m_inFlight = cmd;
        m_busy = true;
        m_writeTimeout.start();
    } else if (opOf(cmd) == quint8(ftms::OpCode::RequestControl)) {
        scheduleControlRetry();
    }
}

} // namespace watt
