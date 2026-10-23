#pragma once

#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QTimer>

namespace watt {

class BleLink;

// Concept2 PM5 rowing monitor over its proprietary rowing service
// (CE060030-43E5-11E4-916C-0800200C9A66).
//
// After service discovery the sample rate (0x0034) is set to 250 ms and
// notifications are enabled on 0x0031, 0x0032, 0x0033 and 0x0036. If those
// characteristics are not notifiable, the multiplexed characteristic 0x0080 is
// used instead. If no data arrives within a few seconds of subscribing, 0x0080
// is enabled additionally (the PM5 only multiplexes ids whose dedicated
// notification is off, so this never duplicates data).
class Pm5Rower : public RowerDevice
{
    Q_OBJECT
public:
    Pm5Rower(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
             QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override { return QStringLiteral("Concept2 PM5 Bluetooth"); }
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;

private:
    void onServiceReady();
    void onNotification(const QBluetoothUuid &uuid, const QByteArray &value);
    bool canNotify(const QBluetoothUuid &uuid) const;
    bool subscribe(quint16 shortId);
    void enableMultiplexed();

    QString m_id;
    QString m_name;
    BleLink *m_link;
    bool m_multiplexed = false;
    bool m_gotData = false;
    QTimer m_dataWatchdog;
};

} // namespace watt
