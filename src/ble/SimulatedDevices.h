#pragma once

#include "SensorDevices.h"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace watt {

// Virtual trainer for demos, development and testing without hardware.
// Follows ERG targets with a realistic lag and noise.
//
// Gradient simulation (setSimulation): the trainer brake resists like the
// road, P = (Crr·m·g·cosθ + m·g·sinθ + ½·Cw·v²)·v at the wheel speed
// v = cadence/60 · gear ratio · 2.105 m (700×25c, the app's default wheel),
// with Crr / Cw as sent (FTMS 0x11)
// and m = massKg(): its "rider weight setting", which DeviceManager keeps at
// SensorHub::riderMassKg (rider + bike of the app's road model) - no
// hard-coded weight, so a lighter rider gets a lighter trainer.
// Virtual gears off (fixed gear ratio 0): the simulated rider pedals ~85 rpm
// and shifts a 22-speed bike (34×28 .. 50×11) one gear per second towards
// the effort he wants for that grade (free ride power + kSimWattsPerPercent
// per % grade, 0.5..1.8 × the free ride power), so a climb first makes the
// pedals heavier and then the rider shifts down.
// Virtual gears on (setFixedGearRatio(r > 0), the rider shifts with a Zwift
// Click): the bike stays in the reference gear r. No shifting, no effort
// target: the power is the brake at the cadence - the (gear-adjusted) grade
// in simulation, the ERG target in ERG, the flywheel's basic resistance
// (grade 0) in free ride. So a harder virtual gear (= a steeper
// gear-adjusted grade / a higher ERG target from the app) gives more watts,
// an easier one fewer, climbs more. The reported speed is the wheel speed of
// the reference gear.
// The simulated rider (riderFtp() > 0, DeviceManager: the profile FTP) has a
// power capacity: FTP sustained, bursts up to kBurstFactor × FTP from a
// reserve that lasts ~kBurstSec at the full burst and refills below FTP
// (~kRecoverSec at half FTP). He prefers ~kSimCadenceRpm (±2); when the gear
// asks for more than his capacity there, the cadence drops until P(cadence)
// = capacity (not below kSimMinCadenceRpm); in easy gears (below ~55 % FTP)
// he spins up towards kSimMaxCadenceRpm. riderFtp() 0: no limit, steady
// ~kSimCadenceRpm (tests).
// WATT_SIM_TRAINER_NO_SIM=1 makes it a trainer without simulation support
// (demo of the fallback).
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

    static constexpr double kDefaultMassKg = 84.0;          // 75 kg rider + 9 kg bike
    static constexpr double kSimWattsPerPercent = 9.0;
    static constexpr double kWheelCircumferenceM = 2.105;   // 700×25c (GearSetup default)
    static constexpr double kSimCadenceRpm = 88.0;          // virtual gears: preferred cadence
    static constexpr double kSimMinCadenceRpm = 40.0;       // grinding a far too hard gear
    static constexpr double kSimMaxCadenceRpm = 105.0;      // spinning an easy gear
    static constexpr double kSimEasyShare = 0.55;           // below this share of FTP the cadence rises
    static constexpr double kBurstFactor = 1.5;             // short efforts up to 1.5 × FTP
    static constexpr double kBurstSec = 60.0;               // reserve at the full burst
    static constexpr double kRecoverSec = 120.0;            // reserve refill below FTP

    void setFreeRidePower(int watts) { m_freeRidePower = watts; }
    void setSimulationSupported(bool on);
    void setFixedGearRatio(double ratio) { m_fixedRatio = ratio > 0 ? ratio : 0.0; }
    double fixedGearRatio() const { return m_fixedRatio; }
    // Rider + bike (kg) the brake simulates with (ignored unless 20..400).
    void setMassKg(double kg);
    double massKg() const { return m_massKg; }
    // Simulated rider's FTP (W); <= 0: no power limit.
    void setRiderFtp(int watts) { m_ftp = std::max(0, watts); }
    int riderFtp() const { return m_ftp; }
    // Power (W) the rider can give right now: FTP · (1 + (kBurstFactor − 1) · reserve).
    double riderCapacity() const;
    double riderReserve() const { return m_reserve; }
    // Cadence the rider settles at in the fixed (reference) gear for the
    // current resistance (see the class comment).
    double riderCadence() const;
    void setNoise(bool on) { m_noise = on; }   // tests: deterministic values
    double currentPower() const { return m_power; }
    bool simulating() const { return m_sim; }
    bool ergActive() const { return m_erg; }      // holds targetPower()
    int targetPower() const { return m_target; }
    double simulationGrade() const { return m_grade; }
    double wheelSpeedMps() const { return m_wheelSpeed; }
    int gearIndex() const { return m_gear; }
    double cadenceRpm() const { return m_cadence; }
    // The effort (W) the simulated rider shifts towards (real gears only).
    double riderGoal() const;
    static int gearCount();
    // Brake power (W) at wheel speed v for the current simulation parameters.
    double brakePower(double speedMps) const;
    double brakePowerAt(double speedMps, double gradePercent) const;
    // One simulation second (driven by a 1 s timer; public for tests).
    void step();

private:
    double gearRatio(int index) const;
    double fixedGearDemand(double cadenceRpm) const;   // brake power at that cadence

    QTimer m_timer;
    double m_power = 0;
    double m_cadence = 0;
    int m_target = 0;
    bool m_erg = false;
    bool m_sim = false;
    bool m_simSupported = true;
    bool m_noise = true;
    double m_grade = 0, m_wind = 0, m_crr = 0.004, m_cw = 0.39;
    double m_massKg = kDefaultMassKg;
    int m_ftp = 0;
    double m_reserve = 1.0;   // burst reserve 0..1
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
