#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QMap>
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
//
// Stroke rate, heart rate and pace come only from 0x0032 (Additional Status
// 1). When data flows but no 0x0032 arrives for 5 s, its notification is
// enabled again; if that does not help either, it is disabled so the PM5
// sends it on 0x0080 instead. Meanwhile RowerDevice computes the stroke rate
// from the stroke timing. Every packet is counted per characteristic (and per
// multiplexed id) with its last raw payload for the diagnostics report.
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

    QString dataSourceText() const override;
    QString dataDiagnostics() const override;

    // Diagnostics per channel: "0x0031", "0x0032", "0x0033", "0x0036",
    // "0x0080", "0x0080/0x32" (multiplexed id), "0x2AD1".
    struct Channel
    {
        int packets = 0;
        int undecoded = 0;          // too short / unsupported layout
        QByteArray last;            // last raw payload
        qint64 lastMs = -1;         // RowerDevice::nowMs() of the last packet
        QString subscription;       // requested / enabled / FAILED / disabled / not offered
    };
    QMap<QString, Channel> channels() const { return m_channels; }
    int lastDecodedStrokeRate() const { return m_lastDecodedSpm; }   // -1 = none yet
    static QString channelKey(quint16 shortId);

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
    void onNotificationResult(const QBluetoothUuid &uuid, bool enabled, bool ok);
    void onStatus1Watchdog();
    Channel &notePacket(const QString &key, const QByteArray &value, bool decoded);
    void setSubscription(quint16 shortId, const QString &text);

    QString m_id;
    QString m_name;
    BleLink *m_link;
    bool m_multiplexed = false;
    bool m_gotData = false;
    bool m_machineWarned = false;
    QTimer m_dataWatchdog;
    QTimer m_status1Watchdog;
    int m_status1Stage = 0;
    bool m_status1Seen = false;
    QString m_status1Via;            // "0x0032" or "0x0080" (last Additional Status 1)
    int m_lastDecodedSpm = -1;
    qint64 m_lastDecodedSpmMs = -1;
    QMap<QString, Channel> m_channels;
    Source m_source = Source::Unknown;
    ftms::RowerDataAssembler m_ftmsAssembler;
};

} // namespace watt
