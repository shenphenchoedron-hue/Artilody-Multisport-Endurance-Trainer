#pragma once

#include "DeviceListModel.h"
#include "SensorDevices.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

class QBluetoothLocalDevice;

Q_DECLARE_LOGGING_CATEGORY(lcBle)

namespace watt {

class Database;
class GradientTest;
class SensorHub;

// Discovers and manages Bluetooth LE sensors (FTMS trainers, HRS heart-rate
// sensors, Concept2 PM5 rowers) plus optional simulated devices. Feeds all measurements into the
// SensorHub and forwards ERG requests to the trainer. Exposed to QML as "Devices".
class DeviceManager : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("GradientTest.h")
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
    // Handlebar controllers for virtual shifting (Zwift Click v1/v2).
    Q_PROPERTY(DeviceListModel *controllers READ controllers CONSTANT)

    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY devicesChanged)
    Q_PROPERTY(QString connectedSummary READ connectedSummary NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerId READ trainerId NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerName READ trainerName NOTIFY devicesChanged)
    Q_PROPERTY(QString trainerProtocol READ trainerProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int trainerState READ trainerState NOTIFY devicesChanged)
    Q_PROPERTY(int trainerRssi READ trainerRssi NOTIFY devicesChanged)
    Q_PROPERTY(QStringList trainerCapabilities READ trainerCapabilities NOTIFY devicesChanged)
    Q_PROPERTY(bool trainerSupportsControl READ trainerSupportsControl NOTIFY devicesChanged)
    // What the trainer ACTUALLY does, from its answers (TrainerDevice::trainerMode()):
    // "simulation" / "erg" / "resistance" / "stopped" / "no control" / "unknown", "" = none.
    Q_PROPERTY(QString trainerMode READ trainerMode NOTIFY trainerStatusChanged)
    // One line: "Simulation – trainer accepted −2.00 %" or the problem
    // ("Trainer rejected the gradient command (Invalid parameter)").
    Q_PROPERTY(QString trainerControlStatus READ trainerControlStatus NOTIFY trainerStatusChanged)
    Q_PROPERTY(bool trainerControlProblem READ trainerControlProblem NOTIFY trainerStatusChanged)
    // "This trainer doesn't simulate descents below 0 %" (Supported Inclination Range) or "".
    Q_PROPERTY(QString trainerInclinationNote READ trainerInclinationNote NOTIFY trainerStatusChanged)
    // Devices > Bluetooth Diagnostics > Gradient test
    Q_PROPERTY(watt::GradientTest *gradientTest READ gradientTest CONSTANT)
    Q_PROPERTY(QString heartRateId READ heartRateId NOTIFY devicesChanged)
    Q_PROPERTY(QString heartRateName READ heartRateName NOTIFY devicesChanged)
    Q_PROPERTY(QString heartRateProtocol READ heartRateProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int heartRateState READ heartRateState NOTIFY devicesChanged)
    // Controllers (up to kMaxControllers at once, e.g. both Click v2 pods).
    // The flat values describe the first connected (else connecting) one;
    // controllerBattery is the lowest known level (-1 = unknown).
    Q_PROPERTY(QString controllerId READ controllerId NOTIFY devicesChanged)
    Q_PROPERTY(QString controllerName READ controllerName NOTIFY devicesChanged)
    Q_PROPERTY(QString controllerProtocol READ controllerProtocol NOTIFY devicesChanged)
    Q_PROPERTY(int controllerState READ controllerState NOTIFY devicesChanged)
    Q_PROPERTY(bool controllerConnected READ controllerConnected NOTIFY devicesChanged)
    Q_PROPERTY(int controllerCount READ controllerCount NOTIFY devicesChanged)
    Q_PROPERTY(int controllerBattery READ controllerBattery NOTIFY devicesChanged)

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
    // watts of the running ERG test (0 = none) and a one-line status for the
    // Devices page: "ERG test running – 150 W · stops after 5 min, ..."
    Q_PROPERTY(int ergTestWatts READ ergTestWatts NOTIFY ergTestChanged)
    Q_PROPERTY(QString ergTestStatus READ ergTestStatus NOTIFY ergTestChanged)

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
    DeviceListModel *controllers() { return &m_controllers; }

    int connectedCount() const;
    QString connectedSummary() const;
    QString trainerId() const;
    QString trainerName() const;
    QString trainerProtocol() const;
    int trainerState() const;
    int trainerRssi() const;
    QStringList trainerCapabilities() const;
    bool trainerSupportsControl() const;
    QString trainerMode() const;
    QString trainerControlStatus() const;
    bool trainerControlProblem() const;
    QString trainerInclinationNote() const;
    GradientTest *gradientTest() const { return m_gradientTest; }
    bool gradientTestRunning() const;
    QString heartRateId() const;
    QString heartRateName() const;
    QString heartRateProtocol() const;
    int heartRateState() const;
    QString controllerId() const;
    QString controllerName() const;
    QString controllerProtocol() const;
    int controllerState() const;
    bool controllerConnected() const;
    int controllerCount() const;      // connected controllers
    int controllerBattery() const;
    QList<ControllerDevice *> controllerDevices() const;
    // Virtual trainer: the bike stays in this gear ratio (virtual shifting
    // on, see VirtualGears), 0 = the simulated rider shifts himself.
    void setSimulatedFixedGear(double ratio);
    // Developer/screenshot aid: a connected virtual Zwift Click ("sim:click";
    // "sim:click2" etc. for a second pod). Goes through connectDevice(), so
    // kMaxControllers applies; listed under controllers and reconnectable
    // from there (not remembered as a known controller).
    void connectSimulatedController(const QString &id = QStringLiteral("sim:click"));
    // Developer aid: a button press on the virtual Zwift Click (same route as
    // a real Click: shiftRequested -> controllerShift). false without one.
    bool pressSimulatedController(int direction, const QString &id = QStringLiteral("sim:click"));
    // The active trainer object (may be null), e.g. for diagnostics.
    TrainerDevice *trainerDevice() const { return m_trainer; }
    static constexpr int kMaxControllers = 2;
    // ble/knownControllers keeps the most recently used ids only.
    static constexpr int kMaxKnownControllers = 4;
    // Both pods of a Click v2 / Zwift Ride may report the same press (the
    // left pod's buttons also arrive through the right one): a ±1 shift equal
    // to one of ANOTHER controller less than this long ago is dropped. A
    // double press on the same controller is never merged.
    static constexpr int kShiftDedupeMs = 80;
    QStringList knownControllers() const { return m_knownControllers; }

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
    int ergTestWatts() const { return m_ergTest ? m_ergTestWatts : 0; }
    QString ergTestStatus() const;
    // The ERG test stops by itself after this long (restarted by every new target).
    static constexpr int kErgTestTimeoutMs = 5 * 60 * 1000;
    int ergTestTimeoutMs() const { return m_ergTestTimeout.interval(); }
    void setErgTestTimeoutForTesting(int ms) { m_ergTestTimeout.setInterval(ms); }
    // The open page (VirtualGears::activePage, wired in main.cpp): leaving
    // "devices" stops a running ERG / gradient test.
    void setActivePage(const QString &page);

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectDevice(const QString &id);
    Q_INVOKABLE void disconnectDevice(const QString &id);
    Q_INVOKABLE void startErgTest(int watts);
    Q_INVOKABLE void stopErgTest();
    // Gradient test (simulation only): false + a log line when it cannot run.
    Q_INVOKABLE bool startGradientTest();
    Q_INVOKABLE void stopGradientTest();
    // Stops a running ERG test and gradient test (the trainer gets the
    // sessions' last command back); reason goes to the log. true when one ran.
    // Called when the Devices page is left, a session starts or a session
    // sends a resistance request, and by the ERG test timeout.
    Q_INVOKABLE bool stopTrainerTests(const QString &reason);
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
    // Extra sections appended to diagnosticsReport() (main.cpp: "[Virtual gears]").
    void setExtraDiagnostics(std::function<QString()> provider) { m_extraDiagnostics = std::move(provider); }
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
    void trainerStatusChanged();
    void rowerChanged(watt::RowerDevice *rower);   // active rower object replaced (may be null)
    void rowerDataChanged();                        // live rower values updated
    // Controller buttons (Zwift Click): +1 = plus (shift up / harder),
    // -1 = minus (shift down / easier); includes auto-repeat of held buttons.
    void controllerShift(int direction);
    void controllerShiftUp();
    void controllerShiftDown();
    void controllerButtonsChanged(bool plus, bool minus);

public:
    enum class Kind { Unknown, Trainer, HeartRate, Rower, Controller };
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
    void attachController(ControllerDevice *c);
    ControllerDevice *controllerFor(const QString &id) const;
    ControllerDevice *primaryController() const;
    void releaseController(const QString &id);
    void saveKnownControllers();
    void rememberController(const QString &id);
    void onControllerShift(const QString &id, int dir);
    void onControllerButtons(const QString &id, bool plus, bool minus);
    void emitControllerButtons();
    void releaseTrainer();
    // Re-sends the riding sessions' last command (SensorHub) to the trainer
    // after a test (ERG test, gradient test) used it.
    void restoreSessionCommand();
    // a session command may go to the trainer (no test running)
    bool sessionOwnsTrainer() const;
    // a session request arrived: end the tests (the request itself goes out next)
    void endTestsForSession();
    bool m_suppressRestore = false;
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
    DeviceListModel m_controllers;
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
    QList<QPointer<ControllerDevice>> m_controllerDevs;
    QStringList m_knownControllers;      // most recently used last
    QHash<QString, QPair<bool, bool>> m_controllerButtons;   // plus/minus per controller
    bool m_buttonsPlus = false, m_buttonsMinus = false;     // last controllerButtonsChanged()
    QElapsedTimer m_shiftClock;
    qint64 m_lastShiftMs = -1;
    int m_lastShiftDir = 0;
    QString m_lastShiftFrom;
    // last simulation request (FTMS 0x11) as sent to the trainer: stopErgTest()
    // restores it (SensorHub has no getter for the wind)
    double m_simWind = 0.0;
    QVariantList m_log;
    QDateTime m_lastScan;
    QString m_scanStatus;
    QString m_knownTrainer, m_knownHr, m_knownRower;
    std::function<QString()> m_extraDiagnostics;
    bool m_autoConnect = true;
    bool m_autoReconnect = true;
    bool m_allowMultiple = true;
    bool m_simulatorEnabled = true;
    double m_simFixedGear = 0;
    bool m_ergTest = false;
    int m_ergTestWatts = 0;
    QTimer m_ergTestTimeout;
    QString m_activePage;
    GradientTest *m_gradientTest = nullptr;
    int m_selectedAdapter = 0;
    int m_restHr = 60, m_maxHr = 185, m_ftp = 250;
    QTimer m_uiTimer;
};

} // namespace watt
