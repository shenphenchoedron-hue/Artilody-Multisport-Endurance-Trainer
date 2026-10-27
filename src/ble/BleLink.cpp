#include "BleLink.h"

#include <QLowEnergyDescriptor>
#include <QtGlobal>

namespace watt {

BleLink::BleLink(const QBluetoothDeviceInfo &info, const QBluetoothUuid &serviceUuid,
                 const QBluetoothAddress &localAdapter, QObject *parent)
    : QObject(parent)
    , m_info(info)
    , m_serviceUuid(serviceUuid)
    , m_localAdapter(localAdapter)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &BleLink::connectToDevice);

    m_settleTimer.setSingleShot(true);
    connect(&m_settleTimer, &QTimer::timeout, this, [this] {
        if (!m_service || m_service->state() != QLowEnergyService::RemoteServiceDiscovered)
            return;
        setState(LinkState::Connected);
        emit serviceReady();
    });

    // Detail discovery that never finishes (seen with BlueZ when the
    // peripheral is busy): drop the link and let the reconnect logic retry.
    m_detailsTimeout.setSingleShot(true);
    m_detailsTimeout.setInterval(20000);
    connect(&m_detailsTimeout, &QTimer::timeout, this, [this] {
        if (m_state != LinkState::Connecting || !m_controller)
            return;
        emit logMessage(QStringLiteral("%1: service discovery timed out").arg(displayName()), false);
        m_controller->disconnectFromDevice();
    });

    m_rssiTimer.setInterval(5000);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&m_rssiTimer, &QTimer::timeout, this, [this] {
        if (m_controller && m_controller->state() != QLowEnergyController::UnconnectedState)
            m_controller->readRssi();
    });
#endif
}

BleLink::~BleLink()
{
    m_userDisconnect = true;
    cleanup();
}

QString BleLink::displayName() const
{
    const QString n = m_info.name().trimmed();
    return n.isEmpty() ? m_info.address().toString() : n;
}

void BleLink::setState(LinkState s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (s == LinkState::Connected) {
        m_reconnectAttempts = 0;
        m_rssiTimer.start();
    } else {
        m_rssiTimer.stop();
    }
    emit stateChanged(s);
}

void BleLink::cleanup()
{
    m_settleTimer.stop();
    m_detailsTimeout.stop();
    m_pendingCccd.clear();
    if (m_service) {
        m_service->disconnect(this);
        m_service->deleteLater();
    }
    m_service = nullptr;
    if (m_controller) {
        m_controller->disconnect(this);
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
    }
    m_controller = nullptr;
}

void BleLink::connectToDevice()
{
    if (m_state != LinkState::Disconnected)
        return;
    m_userDisconnect = false;
    m_reconnectTimer.stop();
    m_lastAttempt.start();
    cleanup();
    setState(LinkState::Connecting);
    emit logMessage(QStringLiteral("Connecting to %1…").arg(displayName()), true);

    m_controller = m_localAdapter.isNull() ? QLowEnergyController::createCentral(m_info, this)
                                           : QLowEnergyController::createCentral(m_info, m_localAdapter, this);
    QLowEnergyController *c = m_controller;
    c->setRemoteAddressType(m_addressType);

    connect(c, &QLowEnergyController::connected, this, [this] {
        m_everConnected = true;
        emit logMessage(QStringLiteral("Connected to %1, discovering services").arg(displayName()), true);
        m_detailsTimeout.start();
        m_controller->discoverServices();
    });
    connect(c, &QLowEnergyController::discoveryFinished, this, &BleLink::onDiscoveryFinished);
    connect(c, &QLowEnergyController::disconnected, this, [this] {
        emit logMessage(QStringLiteral("%1 disconnected").arg(displayName()), false);
        setState(LinkState::Disconnected);
        scheduleReconnect();
    });
    connect(c, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error e) {
        emit logMessage(QStringLiteral("Bluetooth error (%1): %2").arg(int(e)).arg(m_controller ? m_controller->errorString() : QString()), false);
        if (e == QLowEnergyController::UnknownRemoteDeviceError)
            emit logMessage(QStringLiteral("%1 is not known to the Bluetooth stack any more – it must be advertising "
                                           "(PM5: Connect screen) while scanning").arg(displayName()), false);
        // Kernel ATT backend only: an unanswered connection may mean the
        // peripheral uses the other address type, so alternate it.
        if (e == QLowEnergyController::ConnectionError && !m_everConnected)
            m_addressType = m_addressType == QLowEnergyController::PublicAddress ? QLowEnergyController::RandomAddress
                                                                                 : QLowEnergyController::PublicAddress;
        if (m_state == LinkState::Connecting) {
            setState(LinkState::Disconnected);
            scheduleReconnect();
        }
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(c, &QLowEnergyController::rssiRead, this, [this](qint16 rssi) { emit rssiChanged(rssi); });
#endif

    c->connectToDevice();
}

void BleLink::onDiscoveryFinished()
{
    if (!m_controller)
        return;
    m_discovered = m_controller->services();
    m_activeUuid = QBluetoothUuid();
    QList<QBluetoothUuid> wanted { m_serviceUuid };
    wanted += m_fallbacks;
    for (const QBluetoothUuid &u : std::as_const(wanted)) {
        if (m_discovered.contains(u)) {
            m_activeUuid = u;
            break;
        }
    }
    if (m_activeUuid.isNull()) {
        m_detailsTimeout.stop();
        emit gattDiscovered(gattSummary());
        emit logMessage(QStringLiteral("%1 does not offer the required service (%2)")
                            .arg(displayName(), uuidText(m_serviceUuid)), false);
        m_userDisconnect = true;   // do not loop on an incompatible device
        emit serviceMissing();
        if (m_controller)
            m_controller->disconnectFromDevice();
        return;
    }
    if (m_activeUuid != m_serviceUuid)
        emit logMessage(QStringLiteral("%1: %2 not found, using %3").arg(displayName(), uuidText(m_serviceUuid), uuidText(m_activeUuid)), true);
    m_service = m_controller->createServiceObject(m_activeUuid, this);
    if (!m_service) {
        emit logMessage(QStringLiteral("Could not create service object"), false);
        m_controller->disconnectFromDevice();
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &BleLink::onServiceState);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &ch, const QByteArray &v) { emit characteristicChanged(ch.uuid(), v); });
    connect(m_service, &QLowEnergyService::characteristicRead, this,
            [this](const QLowEnergyCharacteristic &ch, const QByteArray &v) { emit characteristicRead(ch.uuid(), v); });
    connect(m_service, &QLowEnergyService::descriptorWritten, this,
            [this](const QLowEnergyDescriptor &d, const QByteArray &value) {
                if (!m_service || d.uuid() != QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration))
                    return;
                const auto chars = m_service->characteristics();
                for (const QLowEnergyCharacteristic &ch : chars) {
                    if (ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration) != d)
                        continue;
                    for (int i = 0; i < m_pendingCccd.size(); ++i) {
                        if (m_pendingCccd.at(i).uuid == ch.uuid()) {
                            m_pendingCccd.removeAt(i);
                            break;
                        }
                    }
                    const bool enabled = !value.isEmpty() && (quint8(value.at(0)) & 0x03);
                    emit notificationResult(ch.uuid(), enabled, true);
                    return;
                }
            });
    connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError e) {
        if (e == QLowEnergyService::CharacteristicWriteError)
            emit writeFailed();
        if (e == QLowEnergyService::DescriptorWriteError && !m_pendingCccd.isEmpty()) {
            // Requests are processed in order: the oldest open CCCD write failed.
            const PendingCccd p = m_pendingCccd.takeFirst();
            emit logMessage(QStringLiteral("%1: could not %2 notifications for %3").arg(displayName(),
                                p.enable ? QStringLiteral("enable") : QStringLiteral("disable"), uuidText(p.uuid)), false);
            emit notificationResult(p.uuid, false, false);
        }
        emit logMessage(QStringLiteral("GATT error %1").arg(int(e)), false);
    });
    m_service->discoverDetails();
}

void BleLink::onServiceState(QLowEnergyService::ServiceState s)
{
    if (s != QLowEnergyService::RemoteServiceDiscovered)
        return;
    m_detailsTimeout.stop();
    emit gattDiscovered(gattSummary());
    // Subscribe only once every characteristic/descriptor is known, after an
    // optional settle delay.
    if (m_settleMs > 0) {
        m_settleTimer.start(m_settleMs);
    } else {
        setState(LinkState::Connected);
        emit serviceReady();
    }
}

QString BleLink::uuidText(const QBluetoothUuid &u)
{
    bool ok = false;
    const quint16 s16 = u.toUInt16(&ok);
    if (ok)
        return QStringLiteral("0x%1").arg(s16, 4, 16, QLatin1Char('0'));
    return u.toString(QUuid::WithoutBraces).toUpper();
}

QString BleLink::gattSummary() const
{
    QStringList lines;
    QStringList services;
    for (const QBluetoothUuid &u : m_discovered)
        services << uuidText(u);
    lines << QStringLiteral("services: %1").arg(services.isEmpty() ? QStringLiteral("-") : services.join(QStringLiteral(", ")));
    if (m_service) {
        lines << QStringLiteral("using %1 (%2)")
                     .arg(uuidText(m_service->serviceUuid()),
                          m_service->state() == QLowEnergyService::RemoteServiceDiscovered ? QStringLiteral("details discovered")
                                                                                            : QStringLiteral("details pending"));
        const auto chars = m_service->characteristics();
        for (const QLowEnergyCharacteristic &ch : chars) {
            QStringList props;
            const auto p = ch.properties();
            if (p & QLowEnergyCharacteristic::Read) props << QStringLiteral("read");
            if (p & QLowEnergyCharacteristic::Write) props << QStringLiteral("write");
            if (p & QLowEnergyCharacteristic::WriteNoResponse) props << QStringLiteral("write-no-resp");
            if (p & QLowEnergyCharacteristic::Notify) props << QStringLiteral("notify");
            if (p & QLowEnergyCharacteristic::Indicate) props << QStringLiteral("indicate");
            const bool cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration).isValid();
            lines << QStringLiteral("  char %1 [%2]%3").arg(uuidText(ch.uuid()), props.join(QLatin1Char(' ')),
                                                            cccd ? QStringLiteral(" cccd") : QString());
        }
    } else if (!m_discovered.isEmpty()) {
        lines << QStringLiteral("required service %1 not found").arg(uuidText(m_serviceUuid));
    }
    return lines.join(QLatin1Char('\n'));
}

void BleLink::disconnectFromDevice()
{
    m_userDisconnect = true;
    m_reconnectTimer.stop();
    cleanup();
    setState(LinkState::Disconnected);
}

bool BleLink::reconnectNow()
{
    if (m_state != LinkState::Disconnected || m_userDisconnect || !m_autoReconnect)
        return false;
    if (m_lastAttempt.isValid() && m_lastAttempt.elapsed() < kMinReconnectGapMs)
        return false;
    m_reconnectAttempts = 0;
    emit logMessage(QStringLiteral("%1 is advertising again – reconnecting").arg(displayName()), true);
    connectToDevice();
    return true;
}

void BleLink::scheduleReconnect()
{
    if (m_userDisconnect || !m_autoReconnect)
        return;
    ++m_reconnectAttempts;
    const int delay = qMin(30000, 2000 * m_reconnectAttempts);
    emit logMessage(QStringLiteral("Reconnecting to %1 in %2 s").arg(displayName()).arg(delay / 1000), true);
    // The controller must be recreated from the event loop, not inside its own signal.
    QTimer::singleShot(0, this, [this] { cleanup(); });
    m_reconnectTimer.start(delay);
}

bool BleLink::hasCharacteristic(const QBluetoothUuid &uuid) const
{
    return m_service && m_service->characteristic(uuid).isValid();
}

bool BleLink::enableNotifications(const QBluetoothUuid &characteristic, bool indicate)
{
    if (!m_service)
        return false;
    const QLowEnergyCharacteristic ch = m_service->characteristic(characteristic);
    if (!ch.isValid())
        return false;
    const QLowEnergyDescriptor cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid())
        return false;
    m_pendingCccd.append({ characteristic, true });
    m_service->writeDescriptor(cccd, indicate ? QLowEnergyCharacteristic::CCCDEnableIndication
                                              : QLowEnergyCharacteristic::CCCDEnableNotification);
    return true;
}

bool BleLink::disableNotifications(const QBluetoothUuid &characteristic)
{
    if (!m_service)
        return false;
    const QLowEnergyCharacteristic ch = m_service->characteristic(characteristic);
    if (!ch.isValid())
        return false;
    const QLowEnergyDescriptor cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid())
        return false;
    m_pendingCccd.append({ characteristic, false });
    m_service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDDisable);
    return true;
}

bool BleLink::write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse)
{
    if (!m_service || m_state != LinkState::Connected)
        return false;
    const QLowEnergyCharacteristic ch = m_service->characteristic(characteristic);
    if (!ch.isValid())
        return false;
    // Use a write type the characteristic actually supports.
    const auto props = ch.properties();
    if (withResponse && !(props & QLowEnergyCharacteristic::Write) && (props & QLowEnergyCharacteristic::WriteNoResponse))
        withResponse = false;
    else if (!withResponse && !(props & QLowEnergyCharacteristic::WriteNoResponse) && (props & QLowEnergyCharacteristic::Write))
        withResponse = true;
    m_service->writeCharacteristic(ch, data, withResponse ? QLowEnergyService::WriteWithResponse
                                                          : QLowEnergyService::WriteWithoutResponse);
    return true;
}

bool BleLink::read(const QBluetoothUuid &characteristic)
{
    if (!m_service)
        return false;
    const QLowEnergyCharacteristic ch = m_service->characteristic(characteristic);
    if (!ch.isValid())
        return false;
    m_service->readCharacteristic(ch);
    return true;
}

} // namespace watt
