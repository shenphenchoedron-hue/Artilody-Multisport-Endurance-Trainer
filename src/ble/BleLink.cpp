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
    m_serviceFound = false;
}

void BleLink::connectToDevice()
{
    if (m_state != LinkState::Disconnected)
        return;
    m_userDisconnect = false;
    m_reconnectTimer.stop();
    cleanup();
    setState(LinkState::Connecting);
    emit logMessage(QStringLiteral("Connecting to %1…").arg(m_info.name()), true);

    m_controller = m_localAdapter.isNull() ? QLowEnergyController::createCentral(m_info, this)
                                           : QLowEnergyController::createCentral(m_info, m_localAdapter, this);
    QLowEnergyController *c = m_controller;

    connect(c, &QLowEnergyController::connected, this, [this] {
        emit logMessage(QStringLiteral("Connected to %1, discovering services").arg(m_info.name()), true);
        m_controller->discoverServices();
    });
    connect(c, &QLowEnergyController::serviceDiscovered, this, [this](const QBluetoothUuid &uuid) {
        if (uuid == m_serviceUuid)
            m_serviceFound = true;
    });
    connect(c, &QLowEnergyController::discoveryFinished, this, [this] {
        if (!m_serviceFound) {
            emit logMessage(QStringLiteral("%1 does not offer the required service").arg(m_info.name()), false);
            m_userDisconnect = true;   // do not loop on an incompatible device
            m_controller->disconnectFromDevice();
            return;
        }
        m_service = m_controller->createServiceObject(m_serviceUuid, this);
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
        connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError e) {
            if (e == QLowEnergyService::CharacteristicWriteError)
                emit writeFailed();
            emit logMessage(QStringLiteral("GATT error %1").arg(int(e)), false);
        });
        m_service->discoverDetails();
    });
    connect(c, &QLowEnergyController::disconnected, this, [this] {
        emit logMessage(QStringLiteral("%1 disconnected").arg(m_info.name()), false);
        setState(LinkState::Disconnected);
        scheduleReconnect();
    });
    connect(c, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error e) {
        emit logMessage(QStringLiteral("Bluetooth error (%1): %2").arg(int(e)).arg(m_controller ? m_controller->errorString() : QString()), false);
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

void BleLink::onServiceState(QLowEnergyService::ServiceState s)
{
    if (s == QLowEnergyService::RemoteServiceDiscovered) {
        setState(LinkState::Connected);
        emit serviceReady();
    }
}

void BleLink::disconnectFromDevice()
{
    m_userDisconnect = true;
    m_reconnectTimer.stop();
    cleanup();
    setState(LinkState::Disconnected);
}

void BleLink::scheduleReconnect()
{
    if (m_userDisconnect || !m_autoReconnect)
        return;
    ++m_reconnectAttempts;
    const int delay = qMin(30000, 2000 * m_reconnectAttempts);
    emit logMessage(QStringLiteral("Reconnecting to %1 in %2 s").arg(m_info.name()).arg(delay / 1000), true);
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
    m_service->writeDescriptor(cccd, indicate ? QLowEnergyCharacteristic::CCCDEnableIndication
                                              : QLowEnergyCharacteristic::CCCDEnableNotification);
    return true;
}

bool BleLink::write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse)
{
    if (!m_service || m_state != LinkState::Connected)
        return false;
    const QLowEnergyCharacteristic ch = m_service->characteristic(characteristic);
    if (!ch.isValid())
        return false;
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
