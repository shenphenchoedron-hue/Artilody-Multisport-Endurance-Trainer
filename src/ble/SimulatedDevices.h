#pragma once

#include "SensorDevices.h"

#include <QRandomGenerator>
#include <QTimer>

namespace watt {

// Virtual trainer for demos, development and testing without hardware.
// Follows ERG targets with a realistic lag and noise.
//
// Gradient simulation (setSimulation): the trainer brake resists like the
// road, P = (Crr·m·g·cosθ + m·g·sinθ + ½·Cw·v²)·v at the wheel speed
// v = cadence/60 · gear ratio · 2.096 m (m = kSimMassKg). The simulated
// rider pedals ~85 rpm and shifts a 22-speed bike (34×28 .. 50×11) one gear
// per second towards the effort he wants for that grade (free ride power +
// kSimWattsPerPercent per % grade, 0.5..1.8 × the free ride power), so a
// climb first makes the pedals heavier and then the rider shifts down.
// WATT_SIM_TRAINER_NO_SIM=1 makes it a trainer without simulation support
// (demo of the fallback).
// setFixedGearRatio(r > 0): the rider shifts virtually (Zwift Click) and
// leaves the bike in the reference gear r - the simulated rider no longer
// shifts, the brake power follows the (gear-adjusted) grade the app sends.
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
    void setSimulation(double gradePercent, double windSpeedMps, double crr, double cwKgPerM) override;
    int simulationSupport() const override { return m_simSupported ? 1 : 0; }
    QStringList capabilities() const override;
    bool supportsControl() const override { return true; }

    static constexpr double kSimMassKg = 83.0;
    static constexpr double kSimWattsPerPercent = 9.0;
    static constexpr double kWheelCircumferenceM = 2.096;

    void setFreeRidePower(int watts) { m_freeRidePower = watts; }
    void setSimulationSupported(bool on);
    void setFixedGearRatio(double ratio) { m_fixedRatio = ratio > 0 ? ratio : 0.0; }
    double fixedGearRatio() const { return m_fixedRatio; }
    void setNoise(bool on) { m_noise = on; }   // tests: deterministic values
    double currentPower() const { return m_power; }
    bool simulating() const { return m_sim; }
    double simulationGrade() const { return m_grade; }
    double wheelSpeedMps() const { return m_wheelSpeed; }
    int gearIndex() const { return m_gear; }
    static int gearCount();
    // Brake power (W) at wheel speed v for the current simulation parameters.
    double brakePower(double speedMps) const;
    // One simulation second (driven by a 1 s timer; public for tests).
    void step();

private:
    double gearRatio(int index) const;
    double riderGoal() const;

    QTimer m_timer;
    double m_power = 0;
    double m_cadence = 0;
    int m_target = 0;
    bool m_erg = false;
    bool m_sim = false;
    bool m_simSupported = true;
    bool m_noise = true;
    double m_grade = 0, m_wind = 0, m_crr = 0.004, m_cw = 0.51;
    double m_wheelSpeed = 0;
    int m_gear = 10;
    double m_fixedRatio = 0;
    int m_freeRidePower = 160;
    int m_t = 0;
};

// Virtual Zwift Click (developer/screenshot aid, never listed by a scan):
// connects at once, battery 85 %. pressButton() emits a shift request.
class SimulatedController : public ControllerDevice
{
    Q_OBJECT
public:
    explicit SimulatedController(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("sim:click"); }
    QString name() const override { return QStringLiteral("Zwift Click"); }
    QString protocol() const override { return QStringLiteral("Simulated Zwift Click"); }
    bool isSimulated() const override { return true; }
    void connectDevice() override;
    void disconnectDevice() override;
    void pressButton(int direction);
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
