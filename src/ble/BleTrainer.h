#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QElapsedTimer>
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
    bool reconnectNow() override;

    void setTargetPower(int watts) override;
    void setFreeRide() override;
    void setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM) override;
    int simulationSupport() const override;
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
    void sendLastRequest();
    void sendFreeRideCommand();
    void dropQueuedControlCommands();
    void markSimulationRejected();
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
    // Last command requested by the app, re-sent after control was (re)gained
    // or the link came back.
    enum class Request { None, Target, FreeRide, Simulation };
    Request m_lastRequest = Request::None;
    int m_lastTarget = -1;
    struct SimParams
    {
        double grade = 0, wind = 0, crr = 0.004, cw = 0.39;   // Cw = ρ·CdA (1.225 · 0.32)
    } m_lastSim;
    bool m_pending = false;           // m_lastRequest waits for control
    bool m_simRejected = false;       // 0x11 answered "Op Code not supported"
    bool m_simConfirmed = false;      // 0x11 answered "Success" (features unknown / unread)
    ftms::ControlRefusalGuard m_refusals;   // consecutive "Control Not Permitted"
    QQueue<QByteArray> m_queue;
    QByteArray m_inFlight;            // command written, waiting for its response
    bool m_busy = false;
    QTimer m_writeTimeout;
    QTimer m_controlRetry;
    QElapsedTimer m_ibdLogClock;      // Indoor Bike Data in the diagnostic log: <= 1 Hz
};

} // namespace watt
