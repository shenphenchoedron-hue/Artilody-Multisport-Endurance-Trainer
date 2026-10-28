#pragma once

#include "FtmsProtocol.h"
#include "SensorDevices.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>

#include <optional>

namespace watt {

class GattLink;
class ZwiftHubLink;

// Smart trainer using the Fitness Machine Service (FTMS).
//
// Control point handling (one command in flight, answered by an indication):
//  * a command answered with Invalid Parameter (0x03) / Operation Failed
//    (0x04), or not answered within writeTimeoutMs(), is a problem: it is
//    logged, shown (controlStatus(), e.g. "Trainer rejected the gradient
//    command (Invalid parameter)") and retried with backoff (retryBaseMs(),
//    doubled per consecutive failure, at most kMaxRetries times; the latest
//    resistance request is what is retried). A later success clears it.
//  * Control Not Permitted (0x05): control is requested again (see
//    ftms::ControlRefusalGuard) and the last command re-sent.
//  * the trainer's ACTUAL mode (trainerMode()) comes only from accepted
//    commands and Fitness Machine Status (0x2ADA) notifications: simulation /
//    target power / resistance changed, stopped, reset, control permission
//    lost. Control lost / reset -> control is requested again and the current
//    command re-sent; stopped -> start + the current command again; a status
//    showing another mode than requested -> the current command once more
//    (rate limited, kModeRepairMs).
//  * Supported Inclination Range (0x2AD5): grades sent with 0x11 are clamped
//    to it; a minimum >= 0 % means the trainer cannot simulate descents
//    (inclinationNote()).
class BleTrainer : public TrainerDevice
{
    Q_OBJECT
public:
    BleTrainer(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter = QBluetoothAddress(),
               QObject *parent = nullptr);
    // Tests: any GattLink (takes ownership), e.g. a scripted fake trainer.
    BleTrainer(GattLink *link, const QString &id, const QString &name, QObject *parent = nullptr);

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

    QString trainerMode() const override;
    QString controlStatus() const override;
    bool controlProblem() const override;
    bool inclinationRangeKnown() const override { return m_inclination.has_value(); }
    double minInclinationPercent() const override { return m_inclination ? m_inclination->minimumPercent : -100.0; }
    double maxInclinationPercent() const override { return m_inclination ? m_inclination->maximumPercent : 100.0; }
    QString inclinationNote() const override;

    // Zwift hub protocol on this trainer's own link (ZwiftHubLink): detected
    // after the other services were inspected, started only when enabled
    // (the rider's opt-in) and the FTMS control point is ready.
    int zwiftHubState() const override;
    QString zwiftHubStatus() const override;
    void setZwiftHubEnabled(bool on) override;
    void setHubSimulation(const watt::HubSimulation &p) override;
    ZwiftHubLink *zwiftHub() const { return m_zhub; }

    // Rowing machine check after service discovery (public for tests).
    static bool looksLikeRower(const QList<QBluetoothUuid> &services, bool hasRowerData, bool hasIndoorBikeData);

    static constexpr int kMaxRetries = 3;
    static constexpr int kModeRepairMs = 5000;
    int writeTimeoutMs() const { return m_writeTimeout.interval(); }
    int retryBaseMs() const { return m_retryBaseMs; }
    // Tests: shorter timeouts (write timeout, first retry delay, control retry).
    void setTimingForTesting(int writeTimeoutMs, int retryBaseMs, int controlRetryMs);
    // Consecutive failed (rejected / unanswered) commands, 0 after a success.
    int failureStreak() const { return m_failStreak; }
    // The grade the trainer last accepted (valid while trainerMode() is "simulation").
    double acceptedGrade() const { return m_acceptedGrade; }

private:
    enum class Mode { Unknown, Simulation, Erg, Resistance, Stopped, NoControl };
    void init();
    void onServiceReady();
    void onNotification(const QBluetoothUuid &uuid, const QByteArray &value);
    void onMachineStatus(const QByteArray &value);
    void onRead(const QBluetoothUuid &uuid, const QByteArray &value);
    void onWriteFinished(bool failed);
    void onCommandFailed(const QByteArray &cmd, const QString &problem);
    void requestControl();
    void scheduleControlRetry();
    void restorePendingCommand();
    void sendLastRequest();
    void sendFreeRideCommand();
    // after control was granted with no session command pending (latest wins)
    void sendNeutralState();
    void dropQueuedControlCommands();
    void markSimulationRejected();
    void enqueue(const QByteArray &command);
    void sendNext();
    void setMode(Mode m);
    void setProblem(const QString &text);
    void reacquireControl(const QString &why);
    Mode expectedMode() const;
    static QString modeName(Mode m);
    double clampGrade(double gradePercent);
    void maybeStartHub();

    QString m_id;
    QString m_name;
    GattLink *m_link;
    ftms::Features m_features;
    bool m_featuresKnown = false;
    bool m_ready = false;             // control point indications configured
    bool m_hasControl = false;
    bool m_controlRequested = false;
    // Last command requested by the app, re-sent after control was (re)gained
    // or the link came back.
    // Hub: virtual shifting through the Zwift hub protocol (no 0x11 / 0x05).
    enum class Request { None, Target, FreeRide, Simulation, Hub };
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
    QTimer m_retry;                   // backoff after a rejected / unanswered command
    QByteArray m_retryCommand;        // non-resistance command to repeat (start, reset)
    int m_retryBaseMs = 1000;
    int m_failStreak = 0;
    QElapsedTimer m_ibdLogClock;      // Indoor Bike Data in the diagnostic log: <= 1 Hz

    Mode m_mode = Mode::Unknown;
    QString m_problem;                // "" = fine
    double m_acceptedGrade = 0;
    int m_acceptedTarget = 0;
    std::optional<ftms::InclinationRange> m_inclination;
    bool m_inclinationAbsent = false; // no 0x2AD5 characteristic (Jetblack Victory)
    bool m_clampLogged = false;       // "grade clamped" logged for the current clamping episode
    double m_lastClampedFrom = 0;
    QElapsedTimer m_modeRepairClock;  // last re-send because the status showed another mode
    QElapsedTimer m_reacquireClock;   // last control re-request after "control lost" / reset

    ZwiftHubLink *m_zhub = nullptr;
    bool m_zhubEnabled = false;
    HubSimulation m_lastHub;
    bool m_hubIgnoredLogged = false;
};

} // namespace watt
