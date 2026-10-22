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

// Discovers and manages Bluetooth LE sensors (FTMS trainers + HRS heart-rate
// sensors) plus optional simulated devices. Feeds all measurements into the
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

private:
    enum class Kind { Unknown, Trainer, HeartRate };
    static Kind classify(const QBluetoothDeviceInfo &info);
    static QString idFor(const QBluetoothDeviceInfo &info);

    void onDiscovered(const QBluetoothDeviceInfo &info);
    void doStartScan();
    void addLog(const QString &text, bool ok);
    void addSimulatedEntries();
    void attachTrainer(TrainerDevice *t);
    void attachHeartRate(HeartRateDevice *h);
    void releaseTrainer();
    void releaseHeartRate();
    void savePreferences();

    Database *m_db;
    SensorHub *m_hub;
    QBluetoothLocalDevice *m_local = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    DeviceListModel m_trainers;
    DeviceListModel m_hrSensors;
    QHash<QString, QBluetoothDeviceInfo> m_infos;
    QPointer<TrainerDevice> m_trainer;
    QPointer<HeartRateDevice> m_hr;
    QVariantList m_log;
    QDateTime m_lastScan;
    QString m_scanStatus;
    QString m_knownTrainer, m_knownHr;
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
