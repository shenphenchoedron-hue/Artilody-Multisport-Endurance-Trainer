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

// Virtual Concept2 rower ("sim:rower"). Rows continuously once connected
// (like the virtual trainer), ~24-26 spm at ~150-200 W with slow variation.
// If a target power is set (DeviceManager forwards the training plan's
// requested target) the simulated athlete follows it. Pace/speed use the
// Concept2 relation watts = 2.80 / (s per m)^3; distance integrates speed.
class SimulatedRower : public RowerDevice
{
    Q_OBJECT
public:
    explicit SimulatedRower(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("sim:rower"); }
    QString name() const override { return QStringLiteral("Virtual Rower"); }
    QString protocol() const override { return QStringLiteral("Simulated Concept2 PM5"); }
    bool isSimulated() const override { return true; }
    void connectDevice() override;
    void disconnectDevice() override;

    void setTargetPower(int watts) { m_target = watts; }   // <= 0: free rowing
    void requestTargetPower(int watts) override { setTargetPower(watts); }
    void setFreeRowPower(int watts) { m_freePower = watts; }
    void setRowing(bool on) { m_rowing = on; }              // pause/resume the athlete
    bool isRowing() const { return m_rowing; }
    void setReportHeartRate(bool on) { m_reportHr = on; }

    // One simulation step of `dt` seconds (driven by a 250 ms timer).
    void step(double dt = 0.25);

private:
    QTimer m_timer;
    double m_t = 0;            // simulated workout time (s)
    double m_power = 0;        // smoothed "flywheel" power
    double m_phase = 0;        // stroke cycle 0..1
    double m_dist = 0;
    double m_kcal = 0;
    double m_hr = 0;
    double m_idle = 0;         // seconds since the last stroke
    double m_powerSum = 0;
    int m_strokes = 0;
    int m_target = 0;
    int m_freePower = 175;
    bool m_rowing = true;
    bool m_reportHr = true;
};

} // namespace watt
