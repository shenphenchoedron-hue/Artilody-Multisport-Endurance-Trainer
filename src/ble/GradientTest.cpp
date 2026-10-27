#include "GradientTest.h"

#include "DiagLog.h"
#include "SensorDevices.h"
#include "SensorHub.h"

#include <QVariantMap>

#include <cmath>

namespace watt {

double GradientTest::Step::load() const
{
    if (force() > 0)
        return force();
    return cadence() > 0 ? power() / cadence() : 0.0;
}

GradientTest::GradientTest(SensorHub *hub, QObject *parent)
    : QObject(parent)
    , m_hub(hub)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &GradientTest::tick);
}

double GradientTest::stepGrade() const
{
    const auto g = grades();
    return m_step >= 0 && m_step < g.size() ? g.at(m_step) : 0.0;
}

QVariantList GradientTest::results() const
{
    QVariantList out;
    for (const Step &s : m_steps) {
        QVariantMap m;
        m.insert(QStringLiteral("grade"), s.grade);
        m.insert(QStringLiteral("power"), int(std::lround(s.power())));
        m.insert(QStringLiteral("speedKmh"), s.speedKmh());
        m.insert(QStringLiteral("cadence"), int(std::lround(s.cadence())));
        m.insert(QStringLiteral("force"), s.force());
        m.insert(QStringLiteral("samples"), s.samples);
        m.insert(QStringLiteral("accepted"), s.accepted);
        m.insert(QStringLiteral("trainerStatus"), s.trainerStatus);
        m.insert(QStringLiteral("done"), s.done);
        out.append(m);
    }
    return out;
}

bool GradientTest::start(TrainerDevice *trainer, double crr, double cwKgPerM, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (m_running)
        return fail(QStringLiteral("The gradient test is already running"));
    if (!trainer || trainer->state() != LinkState::Connected)
        return fail(QStringLiteral("Connect a trainer first"));
    if (trainer->simulationSupport() == 0)
        return fail(QStringLiteral("This trainer does not support gradient simulation"));
    m_trainer = trainer;
    m_crr = std::isfinite(crr) && crr > 0 ? crr : 0.004;
    m_cw = std::isfinite(cwKgPerM) && cwKgPerM > 0 ? cwKgPerM : 0.39;
    m_steps.clear();
    for (double g : grades()) {
        Step s;
        s.grade = g;
        m_steps.append(s);
    }
    m_verdict.clear();
    m_running = true;
    m_step = 0;
    DiagLog::write("GTEST", QStringLiteral("gradient test started on %1: steps %2 % x %3 s (measured after %4 s), Crr %5, Cw %6 kg/m "
                                           "- simulation only (FTMS 0x11), no ERG, no virtual gears")
                                .arg(trainer->name())
                                .arg(QStringLiteral("+2, 0, -2, -5"))
                                .arg(kStepSeconds)
                                .arg(kSettleSeconds)
                                .arg(m_crr, 0, 'f', 4)
                                .arg(m_cw, 0, 'f', 2));
    beginStep();
    m_timer.start();
    return true;
}

void GradientTest::beginStep()
{
    m_second = 0;
    m_status = QStringLiteral("Step %1 of %2: %3 % – keep pedaling steadily")
                   .arg(m_step + 1)
                   .arg(stepCount())
                   .arg(stepGrade(), 0, 'f', 0);
    sendGrade();
    emit changed();
}

void GradientTest::sendGrade()
{
    if (m_trainer)
        m_trainer->setSimulation(stepGrade(), 0.0, m_crr, m_cw);
}

void GradientTest::tick()
{
    if (!m_running)
        return;
    if (!m_trainer || m_trainer->state() != LinkState::Connected) {
        finish(QStringLiteral("Stopped: the trainer disconnected"));
        return;
    }
    ++m_second;
    Step &s = m_steps[m_step];
    const int power = m_hub->power();
    const double speed = m_hub->speedKmh();
    const int cadence = m_hub->cadence();
    const bool counted = m_second > kSettleSeconds && cadence >= kMinCadenceRpm;
    if (counted) {
        s.powerSum += power;
        s.speedSum += speed;
        s.cadenceSum += cadence;
        ++s.samples;
    }
    DiagLog::write("GTEST", QStringLiteral("step %1/%2 grade %3 % t=%4 s: power %5 W speed %6 km/h cadence %7 rpm%8  trainer: %9 (%10)")
                                .arg(m_step + 1)
                                .arg(stepCount())
                                .arg(s.grade, 0, 'f', 1)
                                .arg(m_second)
                                .arg(power)
                                .arg(speed, 0, 'f', 1)
                                .arg(cadence)
                                .arg(counted ? QString() : QStringLiteral(" (not counted)"))
                                .arg(m_trainer->trainerMode(), m_trainer->controlStatus()));
    if (m_second < kStepSeconds) {
        if (m_second % kResendSeconds == 0)
            sendGrade();   // keep the trainer on it (a trainer may time out or be reset)
        emit changed();
        return;
    }
    s.accepted = m_trainer->trainerMode() == QLatin1String("simulation") && !m_trainer->controlProblem();
    s.trainerStatus = m_trainer->controlStatus();
    s.done = true;
    DiagLog::write("GTEST", QStringLiteral("RESULT grade %1 %: power %2 W speed %3 km/h cadence %4 rpm force %5 N (%6 samples) "
                                           "trainer %7: %8")
                                .arg(s.grade, 0, 'f', 1)
                                .arg(s.power(), 0, 'f', 0)
                                .arg(s.speedKmh(), 0, 'f', 1)
                                .arg(s.cadence(), 0, 'f', 0)
                                .arg(s.force(), 0, 'f', 1)
                                .arg(s.samples)
                                .arg(s.accepted ? QStringLiteral("accepted") : QStringLiteral("NOT confirmed"), s.trainerStatus));
    if (m_step + 1 < stepCount()) {
        ++m_step;
        beginStep();
        return;
    }
    finish(QStringLiteral("Finished"));
}

void GradientTest::stop()
{
    if (m_running)
        finish(QStringLiteral("Stopped"));
}

void GradientTest::finish(const QString &status)
{
    m_timer.stop();
    m_running = false;
    m_status = status;
    m_verdict = judge(m_steps);
    QStringList table;
    for (const Step &s : std::as_const(m_steps))
        table << QStringLiteral("%1 %: %2 W, %3 km/h, %4 rpm, %5 N%6")
                     .arg(s.grade, 0, 'f', 0)
                     .arg(s.power(), 0, 'f', 0)
                     .arg(s.speedKmh(), 0, 'f', 1)
                     .arg(s.cadence(), 0, 'f', 0)
                     .arg(s.force(), 0, 'f', 1)
                     .arg(!s.done ? QStringLiteral(" (not run)") : s.accepted ? QString() : QStringLiteral(" (not confirmed)"));
    DiagLog::write("GTEST", QStringLiteral("gradient test %1. %2. Verdict: %3").arg(status.toLower(), table.join(QStringLiteral("; ")), m_verdict));
    emit changed();
    emit finished();
}

QString GradientTest::judge(const QVector<Step> &steps)
{
    auto find = [&](double g) -> const Step * {
        for (const Step &s : steps)
            if (std::abs(s.grade - g) < 1e-6 && s.done && s.samples > 0)
                return &s;
        return nullptr;
    };
    const Step *up = find(2.0), *flat = find(0.0), *d2 = find(-2.0), *d5 = find(-5.0);
    if (!flat || !d2 || !d5 || flat->load() <= 0)
        return QStringLiteral("Not enough data – pedal steadily (above %1 rpm) through all four steps").arg(kMinCadenceRpm);
    QString v;
    bool allAccepted = true;
    for (const Step &s : steps)
        allAccepted = allAccepted && (!s.done || s.accepted);
    const double r2 = d2->load() / flat->load();
    const double r5 = d5->load() / flat->load();
    if (r2 > 0.85 && r5 > 0.85)
        v = QStringLiteral("Power stops falling below 0 % → the trainer clamps descents (it brakes like on the flat)");
    else if (r2 < 0.9 && r5 < r2 * 0.9)
        v = QStringLiteral("Power keeps falling on descents → the trainer simulates negative grades");
    else
        v = QStringLiteral("Power falls only a little below 0 % → the trainer limits descents");
    if (up && up->load() > 0 && up->load() < flat->load() * 1.05)
        v += QStringLiteral(". Power hardly rises at +2 % – the trainer may ignore the grade");
    if (!allAccepted)
        v += QStringLiteral(". Not every gradient command was confirmed by the trainer – see the diagnostic log");
    return v;
}

} // namespace watt
