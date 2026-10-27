#pragma once

#include "GattLink.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QPointer>
#include <QTimer>

namespace watt {

// Owns a QLowEnergyController for one peripheral and exposes one GATT
// service. Handles service discovery, notifications and auto-reconnect.
class BleLink : public GattLink
{
    Q_OBJECT
public:
    BleLink(const QBluetoothDeviceInfo &info, const QBluetoothUuid &serviceUuid,
            const QBluetoothAddress &localAdapter = QBluetoothAddress(), QObject *parent = nullptr);
    ~BleLink() override;

    void connectToDevice() override;
    void disconnectFromDevice() override;
    // The peripheral was seen advertising again (scan result): a dropped link
    // that waits for its next reconnect attempt connects at once and the
    // backoff starts over. Does nothing (false) while connecting/connected,
    // after an explicit disconnect or an incompatible device (service
    // missing), with auto-reconnect off, or within kMinReconnectGapMs of the
    // last attempt (advertisements arrive about every second).
    bool reconnectNow() override;
    static constexpr int kMinReconnectGapMs = 5000;
    void setAutoReconnect(bool on) override { m_autoReconnect = on; }
    // Services tried (in this order) when the primary service is missing.
    void setFallbackServices(const QList<QBluetoothUuid> &uuids) { m_fallbacks = uuids; }
    // Delay between "service details discovered" and serviceReady(): BlueZ
    // and some peripherals (Concept2 PM5) drop CCCD writes issued right after
    // service discovery.
    void setSettleDelayMs(int ms) { m_settleMs = ms; }
    // Diagnostics (trainer): after serviceReady() also discover the details
    // of every OTHER service of the peripheral (one after the other, values
    // not read) - Zwift 0xFC82 / 00000001-19CA-..., vendor services, ... -
    // and report them in gattSummary() (gattDiscovered() again). The FTMS
    // service is not touched. otherServicesInspected() afterwards.
    void setInspectOtherServices(bool on) { m_inspectOthers = on; }
    static constexpr int kInspectDelayMs = 1500;
    static constexpr int kInspectTimeoutMs = 8000;

    LinkState state() const override { return m_state; }
    QLowEnergyService *service() const { return m_service; }
    bool hasCharacteristic(const QBluetoothUuid &uuid) const override;
    // Service actually used (primary or a fallback); null before discovery.
    QBluetoothUuid activeService() const { return m_activeUuid; }
    // Every primary service the peripheral reported in the last discovery.
    QList<QBluetoothUuid> discoveredServices() const override { return m_discovered; }
    // Human-readable list of the discovered services and the characteristics
    // (with properties) of the active service.
    QString gattSummary() const;
    static QString uuidText(const QBluetoothUuid &uuid);
    // "Fitness Machine", "Zwift (0xFC82)", ... or "" for an unknown service
    static QString serviceName(const QBluetoothUuid &uuid);
    static QString propertiesText(QLowEnergyCharacteristic::PropertyTypes p);
    bool otherServicesInspected() const override { return m_othersDone; }

    // Queues the CCCD write; the outcome is reported by notificationResult().
    bool enableNotifications(const QBluetoothUuid &characteristic, bool indicate = false) override;
    bool disableNotifications(const QBluetoothUuid &characteristic);
    bool write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse = true) override;
    bool read(const QBluetoothUuid &characteristic) override;

private:
    void setState(LinkState s);
    void cleanup();
    void onServiceState(QLowEnergyService::ServiceState s);
    void scheduleReconnect();
    QString displayName() const;
    void onDiscoveryFinished();
    void inspectNextService();
    QLowEnergyService *serviceWith(const QBluetoothUuid &characteristic) const;
    void finishInspection(const QString &line);
    void wireService(QLowEnergyService *svc, bool primary);
    QString characteristicLines(const QLowEnergyService *svc, const QString &indent) const;

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
    QElapsedTimer m_lastAttempt;        // last connectToDevice()
    QTimer m_rssiTimer;
    QTimer m_settleTimer;
    QTimer m_detailsTimeout;
    struct PendingCccd
    {
        QBluetoothUuid uuid;
        bool enable;
    };
    QList<PendingCccd> m_pendingCccd;   // CCCD writes in the order they were queued

    // other services (setInspectOtherServices())
    bool m_inspectOthers = false;
    bool m_othersDone = false;
    QList<QBluetoothUuid> m_inspectQueue;
    QPointer<QLowEnergyService> m_inspecting;
    QList<QPointer<QLowEnergyService>> m_others;   // discovered, kept for the session
    QStringList m_otherLines;                      // report lines per inspected service
    QTimer m_inspectTimer;                         // start delay / per-service timeout
};

} // namespace watt
