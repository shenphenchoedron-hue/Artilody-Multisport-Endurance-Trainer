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
    // Services tried (in this order) when the primary service is missing.
    void setFallbackServices(const QList<QBluetoothUuid> &uuids) { m_fallbacks = uuids; }
    // Delay between "service details discovered" and serviceReady(): BlueZ
    // and some peripherals (Concept2 PM5) drop CCCD writes issued right after
    // service discovery.
    void setSettleDelayMs(int ms) { m_settleMs = ms; }

    LinkState state() const { return m_state; }
    QLowEnergyService *service() const { return m_service; }
    bool hasCharacteristic(const QBluetoothUuid &uuid) const;
    // Service actually used (primary or a fallback); null before discovery.
    QBluetoothUuid activeService() const { return m_activeUuid; }
    // Every primary service the peripheral reported in the last discovery.
    QList<QBluetoothUuid> discoveredServices() const { return m_discovered; }
    // Human-readable list of the discovered services and the characteristics
    // (with properties) of the active service.
    QString gattSummary() const;
    static QString uuidText(const QBluetoothUuid &uuid);

    // Queues the CCCD write; the outcome is reported by notificationResult().
    bool enableNotifications(const QBluetoothUuid &characteristic, bool indicate = false);
    bool disableNotifications(const QBluetoothUuid &characteristic);
    bool write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse = true);
    bool read(const QBluetoothUuid &characteristic);

signals:
    void stateChanged(watt::LinkState state);
    void serviceReady();
    void characteristicChanged(const QBluetoothUuid &uuid, const QByteArray &value);
    void characteristicRead(const QBluetoothUuid &uuid, const QByteArray &value);
    void writeFailed();   // a characteristic write was rejected by the peripheral
    void serviceMissing(); // connected, but the peripheral lacks the required service
    void gattDiscovered(const QString &summary);   // after every (re)discovery
    // Outcome of a CCCD write queued by enable/disableNotifications():
    // ok == false when the peripheral/stack rejected it.
    void notificationResult(const QBluetoothUuid &characteristic, bool enabled, bool ok);
    void rssiChanged(int rssi);
    void logMessage(const QString &text, bool ok);

private:
    void setState(LinkState s);
    void cleanup();
    void onServiceState(QLowEnergyService::ServiceState s);
    void scheduleReconnect();
    QString displayName() const;
    void onDiscoveryFinished();

    QBluetoothDeviceInfo m_info;
    QBluetoothUuid m_serviceUuid;
    QList<QBluetoothUuid> m_fallbacks;
    QBluetoothUuid m_activeUuid;
    QList<QBluetoothUuid> m_discovered;
    int m_settleMs = 0;
    QBluetoothAddress m_localAdapter;
    QPointer<QLowEnergyController> m_controller;
    QPointer<QLowEnergyService> m_service;
    LinkState m_state = LinkState::Disconnected;
    bool m_userDisconnect = false;
    bool m_autoReconnect = true;
    int m_reconnectAttempts = 0;
    bool m_everConnected = false;
    // Only used by Qt's kernel ATT backend (BlueZ < 5.42); BlueZ D-Bus and
    // CoreBluetooth resolve the address type themselves.
    QLowEnergyController::RemoteAddressType m_addressType = QLowEnergyController::PublicAddress;
    QTimer m_reconnectTimer;
    QTimer m_rssiTimer;
    QTimer m_settleTimer;
    QTimer m_detailsTimeout;
    struct PendingCccd
    {
        QBluetoothUuid uuid;
        bool enable;
    };
    QList<PendingCccd> m_pendingCccd;   // CCCD writes in the order they were queued
};

} // namespace watt
