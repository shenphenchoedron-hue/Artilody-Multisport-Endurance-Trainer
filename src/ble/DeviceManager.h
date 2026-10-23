#pragma once

#include "DeviceListModel.h"
#include "SensorDevices.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <memory>

class QBluetoothLocalDevice;

namespace watt {

class Database;
class SensorHub;

// Discovers and manages Bluetooth LE sensors (FTMS trainers, HRS heart-rate
// sensors, Concept2 PM5 rowers) plus optional simulated devices. Feeds all measurements into the
// SensorHub and forwards ERG requests to the trainer. Exposed to QML as "Devices".
class DeviceManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY adapterChanged)
    Q_PROPERTY(QString adapterStatus READ adapterStatus NOTIFY adapterChanged)
    Q_PROPERTY(QString adapterName READ adapterName NOTIFY adapterChanged)
    Q_PROPERTY(QVariantList adapters READ adapters NOTIFY adapterChanged)
    Q_PROPERTY(int selectedAdapter READ selectedAdapter WRITE setSelectedAdapter NOTIFY adapterChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString lastScanText READ lastScanText NOTIFY scanningChanged)
    Q_PROPERTY(QString scanStatus READ scanStatus NOTIFY scanningChanged)

    Q_PROPERTY(DeviceListModel *trainers READ trainers CONSTANT)
    Q_PROPERTY(DeviceListModel *heartRateSensors READ heartRateSensors CONSTANT)
    Q_PROPERTY(DeviceListModel *rowers READ rowers CONSTANT)

    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY devicesChanged)
    Q_PROPERTY(QString connectedSummary READ connectedSummary NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerId READ trainerId NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerName READ trainerName NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerProtocol READ trainerProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int trainerState READ trainerState NOTIFY devicesChanged)
    Q_PROPERTY(int trainerRssi READ trainerRssi NOTIFY devicesChanged)
    Q_PROPERTY(QStringList trainerCapabilities READ trainerCapabilities NOTIFY devicesChanged)
    Q_PROPERTY(bool trainerSupportsControl READ trainerSupportsControl NOTIFY devicesChanged)
    Q_PROPERTY(QString heartRateId READ heartRateId NOTIFY devicesChanged)
    Q_PROPERTY(QString heartRateName READ heartRateName NOTIFY devicesChanged)
    Q_PROPERTY(QString heartRateProtocol READ heartRateProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int heartRateState READ heartRateState NOTIFY devicesChanged)

    // Active rower. `rower` is the RowerDevice object itself (null when none is
    // selected) with live properties (watts, strokesPerMinute, ...). The flat
    // rower* live values mirror it for null-safe QML bindings.
    Q_PROPERTY(watt::RowerDevice *rower READ rower NOTIFY rowerChanged)
    Q_PROPERTY(QString rowerId READ rowerId NOTIFY devicesChanged)
    Q_PROPERTY(QString rowerName READ rowerName NOTIFY devicesChanged)
    Q_PROPERTY(QString rowerProtocol READ rowerProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int rowerState READ rowerState NOTIFY devicesChanged)
    Q_PROPERTY(int rowerRssi READ rowerRssi NOTIFY devicesChanged)
    Q_PROPERTY(bool rowerConnected READ rowerConnected NOTIFY devicesChanged)
    Q_PROPERTY(int rowerWatts READ rowerWatts NOTIFY rowerDataChanged)
    Q_PROPERTY(int rowerStrokesPerMinute READ rowerStrokesPerMinute NOTIFY rowerDataChanged)
    Q_PROPERTY(int rowerHeartRate READ rowerHeartRate NOTIFY rowerDataChanged)
    Q_PROPERTY(double rowerPace500 READ rowerPace500 NOTIFY rowerDataChanged)
    Q_PROPERTY(double rowerDistance READ rowerDistance NOTIFY rowerDataChanged)
    Q_PROPERTY(double rowerElapsed READ rowerElapsed NOTIFY rowerDataChanged)
    Q_PROPERTY(int rowerStrokeCount READ rowerStrokeCount NOTIFY rowerDataChanged)
    Q_PROPERTY(bool rowerActive READ rowerActive NOTIFY rowerDataChanged)

    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY preferencesChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY preferencesChanged)
    Q_PROPERTY(bool allowMultiple READ allowMultiple WRITE setAllowMultiple NOTIFY preferencesChanged)
    Q_PROPERTY(bool simulatorEnabled READ simulatorEnabled WRITE setSimulatorEnabled NOTIFY preferencesChanged)

    Q_PROPERTY(bool ergTestActive READ ergTestActive NOTIFY ergTestChanged)

public:
    DeviceManager(Database *db, SensorHub *hub, QObject *parent = nullptr);
    ~DeviceManager() override;

    // Call once after construction (loads preferences, auto-connects).
    void initialize();
    void setRiderPhysiology(int restHr, int maxHr, int ftp);

    bool bluetoothAvailable() const;
    QString adapterStatus() const;
    QString adapterName() const;
    QVariantList adapters() const;
    int selectedAdapter() const { return m_selectedAdapter; }
    void setSelectedAdapter(int index);
    bool scanning() const;
    QString lastScanText() const;
    QString scanStatus() const { return m_scanStatus; }

    DeviceListModel *trainers() { return &m_trainers; }
    DeviceListModel *heartRateSensors() { return &m_hrSensors; }
    DeviceListModel *rowers() { return &m_rowers; }

    int connectedCount() const;
    QString connectedSummary() const;
    QString trainerId() const;
    QString trainerName() const;
    QString trainerProtocol() const;
    int trainerState() const;
    int trainerRssi() const;
    QStringList trainerCapabilities() const;
    bool trainerSupportsControl() const;
    QString heartRateId() const;
    QString heartRateName() const;
    QString heartRateProtocol() const;
    int heartRateState() const;

    // Active rower for C++ consumers (e.g. a RowingSession): connect to
    // rowerChanged() and (re)attach to the RowerDevice signals. May be null.
    RowerDevice *rower() const { return m_rower; }
    QString rowerId() const;
    QString rowerName() const;
    QString rowerProtocol() const;
    int rowerState() const;
    int rowerRssi() const;
    bool rowerConnected() const;
    int rowerWatts() const { return m_rower ? m_rower->watts() : 0; }
    int rowerStrokesPerMinute() const { return m_rower ? m_rower->strokesPerMinute() : 0; }
    int rowerHeartRate() const { return m_rower ? m_rower->heartRateBpm() : 0; }
    double rowerPace500() const { return m_rower ? m_rower->pace500Seconds() : 0.0; }
    double rowerDistance() const { return m_rower ? m_rower->distanceMeters() : 0.0; }
    double rowerElapsed() const { return m_rower ? m_rower->elapsedSeconds() : 0.0; }
    int rowerStrokeCount() const { return m_rower ? m_rower->strokeCount() : 0; }
    bool rowerActive() const { return m_rower && m_rower->rowingActive(); }

    QVariantList log() const { return m_log; }
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool on);
    bool autoReconnect() const { return m_autoReconnect; }
    void setAutoReconnect(bool on);
    bool allowMultiple() const { return m_allowMultiple; }
    void setAllowMultiple(bool on);
    bool simulatorEnabled() const { return m_simulatorEnabled; }
    void setSimulatorEnabled(bool on);
    bool ergTestActive() const { return m_ergTest; }

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectDevice(const QString &id);
    Q_INVOKABLE void disconnectDevice(const QString &id);
    Q_INVOKABLE void startErgTest(int watts);
    Q_INVOKABLE void stopErgTest();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void refreshAdapter();
    Q_INVOKABLE QString signalText(int rssi) const;   // "Strong Signal (-52 dBm)"

signals:
    void adapterChanged();
    void scanningChanged();
    void devicesChanged();
    void logChanged();
    void preferencesChanged();
    void ergTestChanged();
    void rowerChanged(watt::RowerDevice *rower);   // active rower object replaced (may be null)
    void rowerDataChanged();                        // live rower values updated

public:
    enum class Kind { Unknown, Trainer, HeartRate, Rower };
    // Device kind from a scan result (public for tests).
    static Kind classify(const QBluetoothDeviceInfo &info);

private:
    static QString idFor(const QBluetoothDeviceInfo &info);

    void onDiscovered(const QBluetoothDeviceInfo &info);
    void doStartScan();
    void addLog(const QString &text, bool ok);
    void addSimulatedEntries();
    void attachTrainer(TrainerDevice *t);
    void attachHeartRate(HeartRateDevice *h);
    void attachRower(RowerDevice *r);
    void releaseTrainer();
    void releaseHeartRate();
    void releaseRower();
    void savePreferences();

    Database *m_db;
    SensorHub *m_hub;
    QBluetoothLocalDevice *m_local = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    DeviceListModel m_trainers;
    DeviceListModel m_hrSensors;
    DeviceListModel m_rowers;
    QHash<QString, QBluetoothDeviceInfo> m_infos;
    QPointer<TrainerDevice> m_trainer;
    QPointer<HeartRateDevice> m_hr;
    QPointer<RowerDevice> m_rower;
    QVariantList m_log;
    QDateTime m_lastScan;
    QString m_scanStatus;
    QString m_knownTrainer, m_knownHr, m_knownRower;
    bool m_autoConnect = true;
    bool m_autoReconnect = true;
    bool m_allowMultiple = true;
    bool m_simulatorEnabled = true;
    bool m_ergTest = false;
    int m_selectedAdapter = 0;
    int m_restHr = 60, m_maxHr = 185, m_ftp = 250;
    QTimer m_uiTimer;
};

} // namespace watt
