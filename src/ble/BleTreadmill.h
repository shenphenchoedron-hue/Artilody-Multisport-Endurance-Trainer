#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>

namespace watt {

class BleLink;

// FTMS treadmill (Fitness Machine Service 0x1826, Treadmill Data 0x2ACD).
//
// Strictly read-only: only notifications of Treadmill Data are enabled (and
// the Fitness Machine Feature is read for the log). The Fitness Machine
// Control Point is never written - no Request Control, no start / stop, no
// target speed or incline - so the app can never move the belt.
class BleTreadmill : public TreadmillDevice
{
    Q_OBJECT
public:
    BleTreadmill(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
                 QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override { return QStringLiteral("Bluetooth FTMS treadmill"); }
    QString dataSourceText() const override { return QStringLiteral("FTMS Treadmill Data (0x2ACD)"); }
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;
    bool reconnectNow() override;

    // One Treadmill Data notification (public so tests can feed packets).
    void processNotification(const QBluetoothUuid &uuid, const QByteArray &value);

signals:
    // Connected, but the device has no FTMS Treadmill Data characteristic.
    void treadmillDataMissing();

private:
    void onServiceReady();

    QString m_id;
    QString m_name;
    BleLink *m_link;
    bool m_logged = false;
};

} // namespace watt
