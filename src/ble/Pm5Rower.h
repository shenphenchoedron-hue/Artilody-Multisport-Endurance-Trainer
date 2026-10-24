#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QTimer>

namespace watt {

class BleLink;

// Concept2 PM5 rowing monitor over its proprietary rowing service
// (CE060030-43E5-11E4-916C-0800200C9A66), with the standard FTMS Rower Data
// (0x1826 / 0x2AD1) as fallback when the peripheral has no Concept2 rowing
// service (also makes other FTMS rowing machines work).
//
// Notifications are enabled ~1 s after the service details were discovered.
// After service discovery the sample rate (0x0034) is set to 250 ms and
// notifications are enabled on 0x0031, 0x0032, 0x0033 and 0x0036. If those
// characteristics are not notifiable, the multiplexed characteristic 0x0080 is
// used instead. 0x0080 is always enabled as well when it is available: the
// PM5 only multiplexes ids whose dedicated notification is off, so it never
// duplicates data but covers every id whose direct subscription failed.
// (Duplicates would be harmless anyway: values are absolute and strokes are
// de-duplicated by their count.)
class Pm5Rower : public RowerDevice
{
    Q_OBJECT
public:
    Pm5Rower(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
             QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override;
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;

    enum class Source { Unknown, Concept2, Ftms };
    Source source() const { return m_source; }

    // Handles one notification/read value (public so tests can feed packets).
    void processNotification(const QBluetoothUuid &uuid, const QByteArray &value);

signals:
    // Connected, but the device has neither the PM5 rowing service (0x0030)
    // nor FTMS Rower Data.
    void rowingServiceMissing();
    void protocolChanged();

private:
    void onServiceReady();
    void applyFtmsRowerData(const ftms::RowerData &d);
    bool canNotify(const QBluetoothUuid &uuid) const;
    bool subscribe(quint16 shortId);
    void enableMultiplexed();

    QString m_id;
    QString m_name;
    BleLink *m_link;
    bool m_multiplexed = false;
    bool m_gotData = false;
    bool m_machineWarned = false;
    QTimer m_dataWatchdog;
    Source m_source = Source::Unknown;
    ftms::RowerDataAssembler m_ftmsAssembler;
};

} // namespace watt
