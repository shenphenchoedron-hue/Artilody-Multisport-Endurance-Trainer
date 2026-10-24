#pragma once

#include "DeviceListModel.h"
#include "SensorDevices.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QDateTime>
#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <memory>

class QBluetoothLocalDevice;

Q_DECLARE_LOGGING_CATEGORY(lcBle)

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
    // User-facing Bluetooth problem (scan error, adapter off, "not a PM5", ...);
    // empty when everything is fine.
    Q_PROPERTY(QString scanError READ scanError NOTIFY scanningChanged)
    // Show unclassified Bluetooth LE devices ("Other devices") on the Devices page.
    Q_PROPERTY(bool showAllDevices READ showAllDevices WRITE setShowAllDevices NOTIFY preferencesChanged)

    Q_PROPERTY(DeviceListModel *trainers READ trainers CONSTANT)
    Q_PROPERTY(DeviceListModel *heartRateSensors READ heartRateSensors CONSTANT)
    Q_PROPERTY(DeviceListModel *rowers READ rowers CONSTANT)
    Q_PROPERTY(DeviceListModel *otherDevices READ otherDevices CONSTANT)

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
    // "Concept2 rowing service (direct 0x31 0x32 0x36)", "FTMS Rower Data (0x2AD1)", ...
    Q_PROPERTY(QString rowerDataSource READ rowerDataSource NOTIFY rowerDataChanged)
    // true while the stroke rate is computed from the stroke timing
    Q_PROPERTY(bool rowerStrokeRateEstimated READ rowerStrokeRateEstimated NOTIFY rowerDataChanged)

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
    QString scanError() const { return m_scanError; }
    bool showAllDevices() const { return m_showAll; }
    void setShowAllDevices(bool on);

    DeviceListModel *trainers() { return &m_trainers; }
    DeviceListModel *heartRateSensors() { return &m_hrSensors; }
    DeviceListModel *rowers() { return &m_rowers; }
    DeviceListModel *otherDevices() { return &m_others; }

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
    QString rowerDataSource() const { return m_rower ? m_rower->dataSourceText() : QString(); }
    bool rowerStrokeRateEstimated() const { return m_rower && m_rower->strokeRateEstimated(); }

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
    // Connects an unclassified device or a trainer-list entry with the rower
    // driver (Concept2 PM5 service, FTMS rower data as fallback) and remembers
    // it as a rower (fallback when the PM5 is not recognised from its
    // advertisement).
    Q_INVOKABLE void connectAsRower(const QString &id);
    // Plain-text Bluetooth report (adapter, Qt version, every device seen,
    // classification, errors) for the diagnostics dialog / bug reports.
    Q_INVOKABLE QString diagnosticsReport() const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE void clearScanError();

public slots:
    // Entry point for every scan result (deviceDiscovered and deviceUpdated).
    // Merges it with what is already known about the device, (re)classifies
    // it and moves it between the lists. Public so tests can feed it.
    void handleDeviceInfo(const QBluetoothDeviceInfo &info, bool updated = false);

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
    // Name-only rower check: "PM5 ..." (any case) or containing "Concept2",
    // excluding SkiErg/BikeErg monitors ("PM5 430123456 Ski"/" Bike").
    static Kind classifyName(const QString &name);
    // Device name without BlueZ's placeholder alias: when BlueZ has not
    // received a name yet, Device1.Alias is the address ("D4-36-39-AA-BB-CC").
    static QString effectiveName(const QBluetoothDeviceInfo &info);
    // User-facing text for a QBluetoothDeviceDiscoveryAgent::Error.
    static QString scanErrorText(int error, const QString &qtText);
    static QString kindName(Kind k);
    static constexpr int kScanWindowMs = 60000;   // one Scan press
    // Moves a device to the rowers list and remembers it as a rower (releases
    // a trainer connection to it, clears it as known trainer). Used by
    // connectAsRower() and when a trainer connection finds rowing services.
    // Public for tests.
    void moveToRowers(const QString &id);

private:
    struct DiagEntry
    {
        QString id;
        QString name;
        QString address;
        int rssi = 0;
        QStringList uuids;
        QStringList manufacturerIds;
        QString coreConfig;
        QString ftmsServiceData;       // parsed FTMS advertisement, empty if none
        QStringList serviceDataIds;    // "0x1826=01 10 00"
        Kind kind = Kind::Unknown;
        int reports = 0;
        QDateTime firstSeen, lastSeen;
    };

    static QString idFor(const QBluetoothDeviceInfo &info);

    void doStartScan(bool clearLists = true);
    void ensureScanning();
    void onScanWindowEnded(const QString &how);
    void setScanError(const QString &text);
    void addDiag(const QString &text);
    void recordDiag(const QString &id, const QBluetoothDeviceInfo &info, Kind kind);
    void recordGatt(const QString &id, const QString &summary);
    void rememberRower(const QString &id, bool on);
    void onTrainerIsRower(const QString &id);
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
    DeviceListModel m_others;
    QHash<QString, QBluetoothDeviceInfo> m_infos;
    QHash<QString, Kind> m_kinds;          // last classification per device id
    QSet<QString> m_forcedRowers;          // "Connect as rower" / detected after connecting (persisted)
    QHash<QString, QPair<QDateTime, QString>> m_gatt;   // last GATT summary per device
    QHash<QString, DiagEntry> m_diag;
    QStringList m_diagEvents;
    QTimer m_scanWindow;
    QDateTime m_scanStartedAt;
    QString m_scanError;
    bool m_showAll = false;
    bool m_scanAfterPowerOn = false;
    bool m_userStop = false;
    int m_autoRescans = 0;                 // extra scan windows while a known device is missing
    int m_ioRetries = 0;
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
