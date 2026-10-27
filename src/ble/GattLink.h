#pragma once

#include "SensorDevices.h"

#include <QBluetoothUuid>
#include <QByteArray>
#include <QList>
#include <QObject>

namespace watt {

// What a device driver needs from one GATT connection: connect / reconnect,
// characteristics of the discovered service(s), reads, writes and
// notifications. BleLink implements it with QLowEnergyController; tests use a
// scripted fake (no Bluetooth hardware), see tests/tst_ftmscontrol.cpp.
class GattLink : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void connectToDevice() = 0;
    virtual void disconnectFromDevice() = 0;
    virtual bool reconnectNow() = 0;
    virtual void setAutoReconnect(bool on) = 0;
    virtual LinkState state() const = 0;
    virtual bool hasCharacteristic(const QBluetoothUuid &uuid) const = 0;
    virtual QList<QBluetoothUuid> discoveredServices() const = 0;
    virtual bool enableNotifications(const QBluetoothUuid &characteristic, bool indicate = false) = 0;
    virtual bool write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse = true) = 0;
    virtual bool read(const QBluetoothUuid &characteristic) = 0;
    // The other services of the peripheral were inspected (diagnostics,
    // BleLink::setInspectOtherServices()); otherServicesDiscovered() then.
    virtual bool otherServicesInspected() const { return false; }

signals:
    void stateChanged(watt::LinkState state);
    void serviceReady();
    void characteristicChanged(const QBluetoothUuid &uuid, const QByteArray &value);
    void characteristicRead(const QBluetoothUuid &uuid, const QByteArray &value);
    void writeFailed();   // a characteristic write was rejected by the peripheral
    void serviceMissing(); // connected, but the peripheral lacks the required service
    void gattDiscovered(const QString &summary);   // after every (re)discovery
    void otherServicesDiscovered();
    // Outcome of a CCCD write queued by enableNotifications(): ok == false
    // when the peripheral/stack rejected it.
    void notificationResult(const QBluetoothUuid &characteristic, bool enabled, bool ok);
    void rssiChanged(int rssi);
    void logMessage(const QString &text, bool ok);
};

} // namespace watt
