#pragma once

#include "SensorDevices.h"
#include "ZwiftClickProtocol.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QElapsedTimer>
#include <QTimer>

namespace watt {

class BleLink;

// Zwift Click v1 / v2 virtual shifter over Bluetooth LE (unencrypted mode).
//
// Connection: Zwift service 00000001-19ca-4651-86e5-fa29dcdd09d1 (newer
// firmware: 0xFC82) -> notifications on the async characteristic (0x0002),
// indications on sync TX (0x0004) -> write "RideOn" to sync RX (0x0003); the
// Click answers "RideOn" on sync TX and then sends protobuf button messages
// (0x37 Click v1, 0x23 Click v2) and battery messages (0x19) on 0x0002.
// Click v2 pods fall asleep without traffic: they get "RideOn" as keep-alive
// every kKeepAliveMs. See ZwiftClickProtocol.h for sources and limitations.
class ZwiftClick : public ControllerDevice
{
    Q_OBJECT
public:
    static constexpr int kKeepAliveMs = 3000;
    static constexpr int kHandshakeFallbackMs = 1500;

    ZwiftClick(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
               QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override;
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;
    bool reconnectNow() override;

    int deviceType() const { return m_type; }       // manufacturer data type byte, -1 = unknown
    bool handshakeDone() const { return m_handshakeDone; }
    bool isV2() const { return m_v2; }
    bool repeatTimerActive() const { return m_repeatTimer.isActive(); }   // tests

    // Feeds a notification as if it came from the device (tests / diagnostics).
    void handleNotification(const QBluetoothUuid &uuid, const QByteArray &value);

private:
    void onServiceReady();
    void sendHandshake();
    void markV2(const QString &why);
    void emitShifts(const QVector<int> &events);
    void reportButtons(bool plus, bool minus);   // emits buttonsChanged on a change

    QString m_id;
    QString m_name;
    int m_type = -1;
    BleLink *m_link;
    zwift::ButtonTracker m_tracker;
    QElapsedTimer m_clock;
    QTimer m_repeatTimer;
    QTimer m_keepAlive;
    QTimer m_handshakeFallback;
    int m_pendingCccd = 0;
    bool m_handshakeSent = false;
    bool m_handshakeDone = false;
    bool m_v2 = false;
    bool m_warnedEncryption = false;
    bool m_reportedPlus = false;    // last buttonsChanged() state
    bool m_reportedMinus = false;
};

} // namespace watt
