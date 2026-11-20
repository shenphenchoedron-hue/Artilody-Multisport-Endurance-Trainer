#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>

namespace watt {

class BleLink;

// FTMS treadmill (Fitness Machine Service 0x1826, Treadmill Data 0x2ACD).
// Without FTMS, Running Speed and Cadence (0x1814, RSC Measurement 0x2A53) is
// used: treadmills that send their belt speed like a foot pod (KETTLER Track
// S2 ... S10) and foot pods; speed and distance only, no incline. The
// console's heart rate (0x2A37) of such a device is passed on.
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
    // FTMS Treadmill Data, or (no FTMS) Running Speed and Cadence like a foot
    // pod (KETTLER Track): known after connecting
    QString protocol() const override;
    QString dataSourceText() const override;
    bool usesRsc() const { return m_rsc; }
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;
    bool reconnectNow() override;

    // One Treadmill Data notification (public so tests can feed packets).
    void processNotification(const QBluetoothUuid &uuid, const QByteArray &value);

signals:
    // Connected, but the device has no FTMS Treadmill Data characteristic.
    void treadmillDataMissing();
    // The device refused to send Treadmill Data (CCCD write failed) - usually
    // it needs to be paired first (PIN).
    void treadmillDataRefused();

private:
    void onServiceReady();
    // diagnostics: a raw packet to the log (TRAW) when it changed
    void logRaw(const QString &what, const QByteArray &value, QByteArray &last);

    QString m_id;
    QString m_name;
    BleLink *m_link;
    bool m_logged = false;
    bool m_rsc = false;   // connected through RSC (0x1814), not FTMS
    int m_rawLines = 0;
    QByteArray m_lastVendor, m_lastRsc;
};

} // namespace watt
