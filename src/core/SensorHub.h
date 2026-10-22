#pragma once

#include <QElapsedTimer>
#include <QObject>

namespace watt {

// Decouples the training logic from the concrete sensors. Device code (BLE or
// simulator) pushes measurements in; the training session reads them and asks
// for resistance changes through signals. Tests can drive the hub directly.
class SensorHub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int power READ power NOTIFY powerChanged)
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
    int cadence() const { return m_cadence; }
    int heartRate() const { return m_heartRate; }
    double speedKmh() const { return m_speed; }
    bool trainerConnected() const { return m_trainerConnected; }
    bool heartRateConnected() const { return m_hrConnected; }
    int requestedTarget() const { return m_requestedTarget; }
    bool ergActive() const { return m_requestedTarget > 0; }

    // Values older than this are treated as 0 (sensor dropped out).
    void setStaleTimeoutMs(int ms) { m_staleMs = ms; }

public slots:
    void updatePower(int watts);
    void updateCadence(double rpm);
    void updateHeartRate(int bpm);
    void updateSpeed(double kmh);
    void setTrainerConnected(bool connected);
    void setHeartRateConnected(bool connected);
    // Called periodically by the session to zero out stale readings.
    void expireStale();

    // Control requests (consumed by the device layer).
    void requestTargetPower(int watts);
    void requestFreeRide();

signals:
    void powerChanged();
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
};

} // namespace watt
