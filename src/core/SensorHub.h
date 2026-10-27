#pragma once

#include "PowerSmoother.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>

namespace watt {

// Decouples the training logic from the concrete sensors. Device code (BLE or
// simulator) pushes measurements in; the training session reads them and asks
// for resistance changes through signals. Tests can drive the hub directly.
class SensorHub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int power READ power NOTIFY powerChanged)
    // Power for DISPLAY: the rolling average over the last
    // powerSmoothingSeconds when powerSmoothing is on, else the raw power.
    // Recording, ERG control and all ride statistics use the raw power.
    Q_PROPERTY(int displayPower READ displayPower NOTIFY displayPowerChanged)
    Q_PROPERTY(bool powerSmoothing READ powerSmoothing WRITE setPowerSmoothing NOTIFY powerSmoothingChanged)
    Q_PROPERTY(int powerSmoothingSeconds READ powerSmoothingSeconds WRITE setPowerSmoothingSeconds NOTIFY powerSmoothingChanged)
    Q_PROPERTY(int cadence READ cadence NOTIFY cadenceChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY heartRateChanged)
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY speedChanged)
    Q_PROPERTY(bool trainerConnected READ trainerConnected NOTIFY trainerConnectedChanged)
    Q_PROPERTY(bool heartRateConnected READ heartRateConnected NOTIFY heartRateConnectedChanged)
    Q_PROPERTY(int requestedTarget READ requestedTarget NOTIFY requestedTargetChanged)
    Q_PROPERTY(bool ergActive READ ergActive NOTIFY requestedTargetChanged)
    // Gradient simulation requested (FTMS 0x11) and the grade last requested.
    Q_PROPERTY(bool simulationActive READ simulationActive NOTIFY requestedTargetChanged)
    Q_PROPERTY(double requestedGrade READ requestedGrade NOTIFY requestedTargetChanged)
    // 1 = the trainer supports gradient simulation, 0 = it does not (feature
    // bit missing or command rejected), -1 = unknown / no trainer.
    Q_PROPERTY(int trainerSimulationSupport READ trainerSimulationSupport NOTIFY trainerSimulationSupportChanged)
    // The connected trainer reports its (flywheel) speed: set by the first
    // speed > 0 after it connected, cleared when it disconnects. Virtual gears
    // on a simulating trainer need it (see road::gearSimulationGrade()).
    Q_PROPERTY(bool trainerReportsSpeed READ trainerReportsSpeed NOTIFY trainerReportsSpeedChanged)
    // Rider + bike (kg) of the current profile and gear setup (set by
    // VirtualGears): the weight setting of the virtual trainer, which
    // simulates with the app's road model (a real trainer has its own).
    Q_PROPERTY(double riderMassKg READ riderMassKg NOTIFY riderMassChanged)

public:
    explicit SensorHub(QObject *parent = nullptr);

    int power() const { return m_power; }
    int displayPower() const { return m_displayPower; }
    bool powerSmoothing() const { return m_smoothing; }
    int powerSmoothingSeconds() const { return m_smoother.windowMs() / 1000; }
    void setPowerSmoothing(bool on);
    void setPowerSmoothingSeconds(int seconds);   // clamped to 1..30
    int cadence() const { return m_cadence; }
    int heartRate() const { return m_heartRate; }
    double speedKmh() const { return m_speed; }
    double speedMps() const { return m_speed / 3.6; }
    bool trainerReportsSpeed() const { return m_speedReported; }
    // A cadence > 0 arrived since the trainer connected (cleared when it
    // disconnects): virtual gears take the reference gear's speed from it.
    bool cadenceReported() const { return m_cadenceReported; }
    // A connected trainer has reported neither a cadence nor a speed yet, and
    // has not been pedaled for kFeedbackProbeMs (power > 0) without them:
    // it may still report them (RideResistance::gearSimulationPossible()).
    // Changes are announced with cadenceReportedChanged().
    bool trainerFeedbackUnknown() const
    {
        return m_trainerConnected && !m_cadenceReported && !m_speedReported && !m_noFeedback;
    }
    static constexpr int kFeedbackProbeMs = 5000;
    bool trainerConnected() const { return m_trainerConnected; }
    bool heartRateConnected() const { return m_hrConnected; }
    int requestedTarget() const { return m_requestedTarget; }
    // Counts the resistance requests (target power, free ride, simulation):
    // lets a sender tell whether the trainer still has its last command.
    quint64 commandSerial() const { return m_commandSerial; }
    bool ergActive() const { return m_requestedTarget > 0; }
    bool simulationActive() const { return m_simActive; }
    double requestedGrade() const { return m_simActive ? m_simGrade : 0.0; }
    int trainerSimulationSupport() const { return m_simSupport; }
    bool trainerSimulationUnsupported() const { return m_simSupport == 0; }
    void setTrainerSimulationSupport(int support);   // set by the device layer

    // FTMS Set Indoor Bike Simulation Parameters defaults when no rider is
    // known: Crr 0.004 and Cw = ρ·CdA of the reference rider (1.225 · 0.32 =
    // 0.39 kg/m). The sessions send the rider's own values
    // (road::trainerModelFor()). Zwift sends a fixed Cw 0.51 kg/m (observed
    // 11 00 00 <grade> 28 33), i.e. ½·Cw = 0.255 vs ½·ρ·CdA = 0.196 - that
    // is not our road model and is no longer used.
    static constexpr double kDefaultCrr = 0.004;
    static constexpr double kDefaultCw = 0.39;

    double riderMassKg() const { return m_riderMassKg; }
    void setRiderMassKg(double kg);   // ignored unless 20..400
    // Crr / Cw of the last simulation request (FTMS 0x11).
    double simulationCrr() const { return m_simCrr; }
    double simulationCw() const { return m_simCw; }

    // Values older than this are treated as 0 (sensor dropped out).
    void setStaleTimeoutMs(int ms);
    // Tests: replace the monotonic clock (milliseconds) used for smoothing.
    void setClockForTesting(std::function<qint64()> clock) { m_clockFn = std::move(clock); }

public slots:
    void updatePower(int watts);
    void updateCadence(double rpm);
    void updateHeartRate(int bpm);
    void updateSpeed(double kmh);
    void setTrainerConnected(bool connected);
    void setHeartRateConnected(bool connected);
    // Called periodically by the session to zero out stale readings.
    void expireStale();
    // Re-evaluates displayPower (also driven by an internal timer while the
    // rolling average is moving).
    void refreshDisplayPower();

    // Control requests (consumed by the device layer).
    void requestTargetPower(int watts);
    void requestFreeRide();
    // Gradient simulation: the trainer makes climbs harder by itself.
    void requestSimulation(double gradePercent, double windSpeedMps = 0.0, double crr = kDefaultCrr,
                           double cwKgPerM = kDefaultCw);

signals:
    void powerChanged();
    void displayPowerChanged();
    void powerSmoothingChanged();
    void cadenceChanged();
    void heartRateChanged();
    void speedChanged();
    void trainerConnectedChanged();
    void heartRateConnectedChanged();
    void requestedTargetChanged();
    void trainerSimulationSupportChanged();
    void trainerReportsSpeedChanged();
    void cadenceReportedChanged();
    void riderMassChanged();

    void targetPowerRequested(int watts);
    void freeRideRequested();
    void simulationRequested(double gradePercent, double windSpeedMps, double crr, double cwKgPerM);

private:
    int m_power = 0;
    int m_cadence = 0;
    int m_heartRate = 0;
    double m_speed = 0;
    bool m_trainerConnected = false;
    bool m_hrConnected = false;
    int m_requestedTarget = 0;
    quint64 m_commandSerial = 0;
    bool m_simActive = false;
    double m_simGrade = 0, m_simWind = 0, m_simCrr = kDefaultCrr, m_simCw = kDefaultCw;
    int m_simSupport = -1;
    double m_riderMassKg = 84.0;
    bool m_speedReported = false;
    bool m_cadenceReported = false;
    bool m_noFeedback = false;        // pedaled kFeedbackProbeMs without cadence / speed
    qint64 m_poweredSinceMs = -1;
    int m_staleMs = 3000;
    QElapsedTimer m_powerAge, m_cadenceAge, m_hrAge, m_speedAge;

    qint64 now() const;
    PowerSmoother m_smoother { 3000 };
    bool m_smoothing = true;
    int m_displayPower = 0;
    qint64 m_lastSampleMs = -1;
    QElapsedTimer m_clock;
    std::function<qint64()> m_clockFn;
    QTimer m_displayTimer;
};

} // namespace watt
