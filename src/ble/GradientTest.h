#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace watt {

class SensorHub;
class TrainerDevice;

// "Gradient test" (Devices > Bluetooth Diagnostics): finds out whether a
// trainer really simulates descents. While the rider pedals steadily (any
// real gear, ~90 rpm) it sends FTMS Set Indoor Bike Simulation (0x11, plain
// simulation - no ERG, no virtual gears) at kGrades for kStepSeconds each and
// measures power / speed / cadence (the last kStepSeconds - kSettleSeconds of
// each step). On a road the resistance force drops with the grade; a trainer
// that clamps descents keeps braking like on the flat. User-started only;
// stop() ends it early. The owner (DeviceManager) blocks the riding sessions'
// commands while it runs and restores them afterwards.
class GradientTest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY changed)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    Q_PROPERTY(double stepGrade READ stepGrade NOTIFY changed)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY changed)
    // [{grade, power, speedKmh, cadence, force, samples, accepted, trainerStatus, done}]
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QString verdict READ verdict NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    static constexpr int kStepSeconds = 20;
    static constexpr int kSettleSeconds = 8;
    static constexpr int kResendSeconds = 5;
    static constexpr int kMinCadenceRpm = 30;
    static QVector<double> grades() { return { 2.0, 0.0, -2.0, -5.0 }; }

    struct Step
    {
        double grade = 0;
        double powerSum = 0, speedSum = 0, cadenceSum = 0;
        int samples = 0;
        bool accepted = false;       // the trainer was in simulation mode without a problem
        QString trainerStatus;       // TrainerDevice::controlStatus() at the end of the step
        bool done = false;
        double power() const { return samples ? powerSum / samples : 0.0; }
        double speedKmh() const { return samples ? speedSum / samples : 0.0; }
        double cadence() const { return samples ? cadenceSum / samples : 0.0; }
        // resistance force N = P / v (0 without speed)
        double force() const { return speedKmh() > 1.0 ? power() / (speedKmh() / 3.6) : 0.0; }
        // what is compared between the steps: force, else watts per rpm
        double load() const;
    };

    explicit GradientTest(SensorHub *hub, QObject *parent = nullptr);

    bool running() const { return m_running; }
    int stepIndex() const { return m_step; }
    int stepCount() const { return int(grades().size()); }
    double stepGrade() const;
    int secondsLeft() const { return m_running ? kStepSeconds - m_second : 0; }
    QVariantList results() const;
    QString verdict() const { return m_verdict; }
    QString status() const { return m_status; }
    const QVector<Step> &steps() const { return m_steps; }

    // false + *error when no connected trainer can simulate gradients.
    bool start(TrainerDevice *trainer, double crr, double cwKgPerM, QString *error = nullptr);
    void stop();
    // One second of the test (1 s timer; public so tests can drive it).
    void tick();
    // The verdict for measured steps (public for tests).
    static QString judge(const QVector<Step> &steps);

signals:
    void changed();
    void finished();   // completed or stopped: the owner restores the sessions' command

private:
    void beginStep();
    void finish(const QString &status);
    void sendGrade();

    SensorHub *m_hub;
    QPointer<TrainerDevice> m_trainer;
    QTimer m_timer;
    bool m_running = false;
    int m_step = 0;
    int m_second = 0;
    double m_crr = 0.004, m_cw = 0.39;
    QVector<Step> m_steps;
    QString m_verdict;
    QString m_status;
};

} // namespace watt
