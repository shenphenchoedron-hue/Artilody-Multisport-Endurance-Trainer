#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QQueue>
#include <QTimer>

namespace watt {

class BleLink;

// Smart trainer using the Fitness Machine Service (FTMS).
class BleTrainer : public TrainerDevice
{
    Q_OBJECT
public:
    BleTrainer(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
               QObject *parent = nullptr);

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString protocol() const override { return QStringLiteral("Bluetooth FTMS"); }
    void connectDevice() override;
    void disconnectDevice() override;
    void setAutoReconnect(bool on) override;

    void setTargetPower(int watts) override;
    void setFreeRide() override;
    QStringList capabilities() const override;
    bool supportsControl() const override;

    // Rowing machine check after service discovery (public for tests).
    static bool looksLikeRower(const QList<QBluetoothUuid> &services, bool hasRowerData, bool hasIndoorBikeData);

private:
    void onServiceReady();
    void onNotification(const QBluetoothUuid &uuid, const QByteArray &value);
    void onRead(const QBluetoothUuid &uuid, const QByteArray &value);
    void onWriteFinished(bool failed);
    void requestControl();
    void scheduleControlRetry();
    void restorePendingCommand();
    void sendFreeRideCommand();
    void enqueue(const QByteArray &command);
    void sendNext();

    QString m_id;
    QString m_name;
    BleLink *m_link;
    ftms::Features m_features;
    bool m_featuresKnown = false;
    bool m_ready = false;             // control point indications configured
    bool m_hasControl = false;
    bool m_controlRequested = false;
    int m_pendingTarget = -1;         // ERG target waiting for control
    bool m_pendingFreeRide = false;
    int m_lastTarget = -1;            // last command requested by the app …
    bool m_lastWasFreeRide = false;   // … so it can be re-sent after a rejection
    QQueue<QByteArray> m_queue;
    QByteArray m_inFlight;            // command written, waiting for its response
    bool m_busy = false;
    QTimer m_writeTimeout;
    QTimer m_controlRetry;
};

} // namespace watt
