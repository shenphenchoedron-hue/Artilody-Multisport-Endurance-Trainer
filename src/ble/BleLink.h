#pragma once

#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QPointer>
#include <QTimer>

namespace watt {

// Owns a QLowEnergyController for one peripheral and exposes one GATT
// service. Handles service discovery, notifications and auto-reconnect.
class BleLink : public QObject
{
    Q_OBJECT
public:
    BleLink(const QBluetoothDeviceInfo &info, const QBluetoothUuid &serviceUuid,
            const QBluetoothAddress &localAdapter = QBluetoothAddress(), QObject *parent = nullptr);
    ~BleLink() override;

    void connectToDevice();
    void disconnectFromDevice();
    void setAutoReconnect(bool on) { m_autoReconnect = on; }

    LinkState state() const { return m_state; }
    QLowEnergyService *service() const { return m_service; }
    bool hasCharacteristic(const QBluetoothUuid &uuid) const;

    bool enableNotifications(const QBluetoothUuid &characteristic, bool indicate = false);
    bool write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse = true);
    bool read(const QBluetoothUuid &characteristic);

signals:
    void stateChanged(watt::LinkState state);
    void serviceReady();
    void characteristicChanged(const QBluetoothUuid &uuid, const QByteArray &value);
    void characteristicRead(const QBluetoothUuid &uuid, const QByteArray &value);
    void writeFailed();   // a characteristic write was rejected by the peripheral
    void serviceMissing(); // connected, but the peripheral lacks the required service
    void rssiChanged(int rssi);
    void logMessage(const QString &text, bool ok);

private:
    void setState(LinkState s);
    void cleanup();
    void onServiceState(QLowEnergyService::ServiceState s);
    void scheduleReconnect();
    QString displayName() const;

    QBluetoothDeviceInfo m_info;
    QBluetoothUuid m_serviceUuid;
    QBluetoothAddress m_localAdapter;
    QPointer<QLowEnergyController> m_controller;
    QPointer<QLowEnergyService> m_service;
    LinkState m_state = LinkState::Disconnected;
    bool m_serviceFound = false;
    bool m_userDisconnect = false;
    bool m_autoReconnect = true;
    int m_reconnectAttempts = 0;
    bool m_everConnected = false;
    // Only used by Qt's kernel ATT backend (BlueZ < 5.42); BlueZ D-Bus and
    // CoreBluetooth resolve the address type themselves.
    QLowEnergyController::RemoteAddressType m_addressType = QLowEnergyController::PublicAddress;
    QTimer m_reconnectTimer;
    QTimer m_rssiTimer;
};

} // namespace watt
