#pragma once

#include "SensorDevices.h"

#include <QRandomGenerator>
#include <QTimer>

namespace watt {

// Virtual trainer for demos, development and testing without hardware.
// Follows ERG targets with a realistic lag and noise.
class SimulatedTrainer : public TrainerDevice
{
    Q_OBJECT
public:
    explicit SimulatedTrainer(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("sim:trainer"); }
    QString name() const override { return QStringLiteral("Virtual Trainer"); }
    QString protocol() const override { return QStringLiteral("Simulated FTMS"); }
    bool isSimulated() const override { return true; }
    void connectDevice() override;
    void disconnectDevice() override;

    void setTargetPower(int watts) override;
    void setFreeRide() override;
    QStringList capabilities() const override;
    bool supportsControl() const override { return true; }

    void setFreeRidePower(int watts) { m_freeRidePower = watts; }
    double currentPower() const { return m_power; }

private:
    void step();

    QTimer m_timer;
    double m_power = 0;
    double m_cadence = 0;
    int m_target = 0;
    bool m_erg = false;
    int m_freeRidePower = 160;
    int m_t = 0;
};

// Virtual heart-rate sensor that responds to the virtual trainer's load.
class SimulatedHeartRate : public HeartRateDevice
{
    Q_OBJECT
public:
    explicit SimulatedHeartRate(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("sim:hr"); }
    QString name() const override { return QStringLiteral("Virtual HR Sensor"); }
    QString protocol() const override { return QStringLiteral("Simulated HRS"); }
    bool isSimulated() const override { return true; }
    void connectDevice() override;
    void disconnectDevice() override;

    void setPhysiology(int restHr, int maxHr, int ftp);
    void setLoad(int watts) { m_load = watts; }

private:
    void step();

    QTimer m_timer;
    double m_hr = 0;
    int m_load = 0;
    int m_rest = 60, m_max = 185, m_ftp = 250;
};

} // namespace watt
