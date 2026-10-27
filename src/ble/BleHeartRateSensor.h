#pragma once

#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>

namespace watt {

class BleLink;

// Heart-rate strap / watch using the Heart Rate Service (HRS).
class BleHeartRateSensor : public HeartRateDevice
{
    Q_OBJECT
public:
    BleHeartRateSensor(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
               QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override { return QStringLiteral("Bluetooth Heart Rate Service (HRS)"); }
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;
    bool reconnectNow() override;

private:
    QString m_id;
    QString m_name;
    BleLink *m_link;
};

} // namespace watt
