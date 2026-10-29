#include "ZwiftClick.h"

#include "BleLink.h"

#include <QLowEnergyCharacteristic>

namespace watt {

ZwiftClick::ZwiftClick(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : ControllerDevice(parent)
    , m_id(info.address().isNull() ? info.deviceUuid().toString() : info.address().toString())
    , m_name(info.name().trimmed().isEmpty() ? QStringLiteral("Zwift Click") : info.name().trimmed())
    , m_link(new BleLink(info, QBluetoothUuid(zwift::serviceUuid()), localAdapter, this))
{
    const QByteArray mfr = info.manufacturerData(zwift::kManufacturerId);
    if (!mfr.isEmpty())
        m_type = quint8(mfr.at(0));
    m_clock.start();
    m_link->setFallbackServices({ QBluetoothUuid(quint16(zwift::kServiceUuid16)) });
    m_link->setSettleDelayMs(200);
    m_link->setAdvertiseHint(QStringLiteral("press a button on the Click to wake it"));

    m_repeatTimer.setInterval(50);
    connect(&m_repeatTimer, &QTimer::timeout, this, [this] {
        emitShifts(m_tracker.poll(m_clock.elapsed()));
        if (!m_tracker.held())
            m_repeatTimer.stop();
    });
    m_keepAlive.setInterval(kKeepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] {
        if (m_link->state() == LinkState::Connected)
            m_link->write(QBluetoothUuid(zwift::syncRxUuid()), zwift::rideOn(), false);
    });
    m_handshakeFallback.setSingleShot(true);
    m_handshakeFallback.setInterval(kHandshakeFallbackMs);
    connect(&m_handshakeFallback, &QTimer::timeout, this, &ZwiftClick::sendHandshake);

    // Ride / Click v2 types are known from the advertisement: keep-alive and
    // the unlock acknowledgement from the first handshake on.
    if (zwift::isV2ControllerType(m_type))
        markV2(QStringLiteral("device type 0x%1").arg(m_type, 2, 16, QLatin1Char('0')));

    setRssi(info.rssi());
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &ZwiftClick::logMessage);
    connect(m_link, &BleLink::gattDiscovered, this, &ZwiftClick::gattReport);
    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
        if (s != LinkState::Connected) {
            m_repeatTimer.stop();
            m_keepAlive.stop();
            m_handshakeFallback.stop();
            m_handshakeSent = m_handshakeDone = false;
            m_tracker.reset();
            reportButtons(false, false);   // whatever was pressed is released now
        }
        setState(s);
    });
    connect(m_link, &BleLink::serviceReady, this, &ZwiftClick::onServiceReady);
    connect(m_link, &BleLink::notificationResult, this, [this](const QBluetoothUuid &, bool, bool ok) {
        if (!ok)
            emit logMessage(QStringLiteral("%1: a notification could not be enabled").arg(m_name), false);
        if (m_pendingCccd > 0 && --m_pendingCccd == 0)
            sendHandshake();
    });
    connect(m_link, &BleLink::characteristicChanged, this, &ZwiftClick::handleNotification);
}

QString ZwiftClick::protocol() const
{
    return m_v2 ? QStringLiteral("Zwift Click v2 (Bluetooth LE)") : QStringLiteral("Zwift Click (Bluetooth LE)");
}

void ZwiftClick::connectDevice()
{
    m_link->setAutoReconnect(m_autoReconnect);
    m_link->connectToDevice();
}

void ZwiftClick::disconnectDevice()
{
    m_link->disconnectFromDevice();
}

bool ZwiftClick::reconnectNow()
{
    m_link->setAutoReconnect(m_autoReconnect);
    return m_link->reconnectNow();
}

void ZwiftClick::setAutoReconnect(bool on)
{
    ControllerDevice::setAutoReconnect(on);
    m_link->setAutoReconnect(on);
}

void ZwiftClick::onServiceReady()
{
    m_handshakeSent = m_handshakeDone = false;
    m_pendingCccd = 0;
    if (m_link->activeService() == QBluetoothUuid(quint16(zwift::kServiceUuid16)))
        markV2(QStringLiteral("service 0xFC82"));
    const QBluetoothUuid async(zwift::asyncUuid());
    const QBluetoothUuid syncTx(zwift::syncTxUuid());
    if (!m_link->hasCharacteristic(async) || !m_link->hasCharacteristic(QBluetoothUuid(zwift::syncRxUuid()))) {
        emit logMessage(QStringLiteral("%1 lacks the Zwift characteristics – update its firmware in Zwift Companion")
                            .arg(m_name), false);
        return;
    }
    if (m_link->enableNotifications(async))
        ++m_pendingCccd;
    // sync TX is "indicate" on the Click (notify on some firmware)
    bool indicate = true;
    if (QLowEnergyService *svc = m_link->service()) {
        const auto props = svc->characteristic(syncTx).properties();
        indicate = props.testFlag(QLowEnergyCharacteristic::Indicate) || !props.testFlag(QLowEnergyCharacteristic::Notify);
    }
    if (m_link->hasCharacteristic(syncTx) && m_link->enableNotifications(syncTx, indicate))
        ++m_pendingCccd;
    // Some stacks never confirm CCCD writes: handshake anyway after a moment.
    m_handshakeFallback.start();
    if (m_pendingCccd == 0)
        sendHandshake();
}

void ZwiftClick::sendHandshake()
{
    m_handshakeFallback.stop();
    if (m_handshakeSent || m_link->state() != LinkState::Connected)
        return;
    m_handshakeSent = true;
    const QBluetoothUuid rx(zwift::syncRxUuid());
    if (!m_link->write(rx, zwift::rideOn(), false)) {
        emit logMessage(QStringLiteral("%1: handshake could not be sent").arg(m_name), false);
        return;
    }
    if (m_v2)
        m_link->write(rx, zwift::unlockAck(), false);
    emit logMessage(QStringLiteral("%1: handshake sent (RideOn)").arg(m_name), true);
}

void ZwiftClick::markV2(const QString &why)
{
    if (m_v2)
        return;
    m_v2 = true;
    emit logMessage(QStringLiteral("%1: Click v2 protocol (%2) – keep-alive on").arg(m_name, why), true);
    if (m_link->state() == LinkState::Connected) {
        m_keepAlive.start();
        // detected after the handshake went out (0x23 frames, challenge):
        // send the unlock acknowledgement sendHandshake() skipped
        if (m_handshakeSent)
            m_link->write(QBluetoothUuid(zwift::syncRxUuid()), zwift::unlockAck(), false);
    }
}

void ZwiftClick::reportButtons(bool plus, bool minus)
{
    if (plus == m_reportedPlus && minus == m_reportedMinus)
        return;
    m_reportedPlus = plus;
    m_reportedMinus = minus;
    emit buttonsChanged(plus, minus);
}

void ZwiftClick::emitShifts(const QVector<int> &events)
{
    for (int dir : events)
        emit shiftRequested(dir);
}

void ZwiftClick::handleNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    if (uuid == QBluetoothUuid(zwift::syncTxUuid())) {
        const zwift::SyncResponse r = zwift::parseSyncResponse(value);
        switch (r) {
        case zwift::SyncResponse::RideOn:
            if (!m_handshakeDone) {
                m_handshakeDone = true;
                emit logMessage(QStringLiteral("%1 ready – press + / − to shift").arg(m_name), true);
            }
            if (m_v2 && !m_keepAlive.isActive())
                m_keepAlive.start();
            break;
        case zwift::SyncResponse::PublicKey:
            if (!m_warnedEncryption) {
                m_warnedEncryption = true;
                emit logMessage(QStringLiteral("%1 asks for the encrypted mode, which is not supported – "
                                               "update the Click firmware in Zwift Companion").arg(m_name), false);
            }
            break;
        case zwift::SyncResponse::Challenge:
            markV2(QStringLiteral("unlock challenge"));
            if (!m_warnedEncryption) {
                m_warnedEncryption = true;
                emit logMessage(QStringLiteral("%1 is locked (Click v2): its buttons only work after Zwift unlocked it "
                                               "(once per 24 h). The right pod works without unlock.").arg(m_name), false);
            }
            break;
        default:
            break;
        }
        return;
    }
    if (uuid != QBluetoothUuid(zwift::asyncUuid()))
        return;
    const zwift::Message m = zwift::parseAsync(value);
    if (m.type == zwift::MsgRideKeys)
        markV2(QStringLiteral("0x23 messages"));
    if (!m_handshakeDone && m.kind != zwift::Message::Kind::Invalid) {
        m_handshakeDone = true;   // data flows: the handshake worked even without echo
        emit logMessage(QStringLiteral("%1 ready – press + / − to shift").arg(m_name), true);
    }
    switch (m.kind) {
    case zwift::Message::Kind::Buttons: {
        const QVector<int> events = m_tracker.update(m.buttons, m_clock.elapsed());
        reportButtons(m.buttons.plus, m.buttons.minus);
        emitShifts(events);
        // A held button is reported again with every frame (the Click v2
        // sends ~10-20 per second): restarting the timer on each one would
        // keep it from ever firing, so only start it when it is idle.
        if (!m_tracker.held())
            m_repeatTimer.stop();
        else if (!m_repeatTimer.isActive())
            m_repeatTimer.start();
        break;
    }
    case zwift::Message::Kind::Battery:
        setBatteryLevel(m.battery);
        break;
    default:
        break;
    }
}

} // namespace watt
