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
    bool trainerConnected() const { return m_trainerConnected; }
    bool heartRateConnected() const { return m_hrConnected; }
    int requestedTarget() const { return m_requestedTarget; }
    bool ergActive() const { return m_requestedTarget > 0; }

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

    void targetPowerRequested(int watts);
    void freeRideRequested();

private:
    int m_power = 0;
    int m_cadence = 0;
    int m_heartRate = 0;
    double m_speed = 0;
    bool m_trainerConnected = false;
    bool m_hrConnected = false;
    int m_requestedTarget = 0;
    int m_staleMs = 3000;
    QElapsedTimer m_powerAge, m_cadenceAge, m_hrAge;

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
