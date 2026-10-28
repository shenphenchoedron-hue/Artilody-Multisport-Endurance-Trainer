#include "HubGearCompensation.h"

#include "Gearing.h"   // road::kGravity

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
bool plausible(double r)
{
    return std::isfinite(r) && r >= HubGearCompensation::kMinRatio && r <= HubGearCompensation::kMaxRatio;
}
QString num(double v, int d = 3) { return QString::number(v, 'f', d); }
} // namespace

const char *HubGearCompensation::speedName(Speed s)
{
    switch (s) {
    case Speed::Unknown: return "unknown";
    case Speed::Flywheel: return "flywheel";
    case Speed::FollowsGear: return "follows the gear";
    }
    return "unknown";
}

QString HubGearCompensation::Record::toString() const
{
    const char *sp = speed == Speed::Flywheel ? "flywheel" : speed == Speed::FollowsGear ? "follows" : "unknown";
    return QStringLiteral("%1;%2;%3").arg(realRatio, 0, 'f', 6).arg(QLatin1String(sp)).arg(calibrates ? 1 : 0);
}

HubGearCompensation::Record HubGearCompensation::Record::fromString(const QString &s)
{
    Record r;
    const QStringList parts = s.split(QLatin1Char(';'));
    if (parts.size() != 3)
        return r;
    const QString sp = parts.at(1);
    r.speed = sp == QLatin1String("flywheel") || sp == QLatin1String("1") ? Speed::Flywheel
              : sp == QLatin1String("follows")                         ? Speed::FollowsGear
                                                                       : Speed::Unknown;
    r.realRatio = r.speed == Speed::Flywheel ? parts.at(0).toDouble() : 0.0;
    if (!plausible(r.realRatio))
        r.realRatio = 0;
    r.calibrates = parts.at(2) == QLatin1String("1");
    return r;
}

void HubGearCompensation::reset()
{
    reset(Record());
}

void HubGearCompensation::reset(const Record &saved)
{
    m_record = saved;
    if (m_record.speed != Speed::Flywheel || !plausible(m_record.realRatio))
        m_record.realRatio = 0;   // a ratio is only reused once the speed was verified
    m_candidate = 0;
    m_provisionalSinceMs = -1;
    m_expired = false;
    m_samples.clear();
    m_windowSent = 0;
    m_last = Window();
    m_powerConfirmed = false;
    m_votes = 0;
    m_lastSpeed = -1;
    m_lastSpeedMs = 0;
    m_note.clear();
}

bool HubGearCompensation::provisional() const
{
    return m_record.speed == Speed::Unknown && m_candidate > 0 && !m_expired;
}

double HubGearCompensation::realRatio() const
{
    switch (m_record.speed) {
    case Speed::FollowsGear:
        return 0.0;
    case Speed::Flywheel:
        return m_candidate > 0 ? m_candidate : m_record.realRatio;   // this ride's window wins
    case Speed::Unknown:
        return provisional() ? m_candidate : 0.0;
    }
    return 0.0;
}

double HubGearCompensation::factor() const
{
    const double r = realRatio();
    if (!(r > 0) || m_record.calibrates)
        return 1.0;
    return std::clamp(kTrainerRatio / r, kMinFactor, kMaxFactor);
}

int HubGearCompensation::finish(double factorBefore, const Record &recordBefore) const
{
    int changes = NoChange;
    if (std::abs(factor() - factorBefore) >= 0.005)
        changes |= FactorChanged;
    if (m_record != recordBefore)
        changes |= RecordChanged;
    return changes;
}

void HubGearCompensation::addNote(const QString &n)
{
    m_note = m_note.isEmpty() ? n : m_note + QStringLiteral("; ") + n;
}

int HubGearCompensation::update(const Input &in)
{
    m_note.clear();
    int changes = checkTimeout(in.sample.ms);
    changes |= gearSample(in);
    changes |= powerSample(in);
    return changes;
}

int HubGearCompensation::tick(qint64 ms)
{
    m_note.clear();
    return checkTimeout(ms);
}

int HubGearCompensation::checkTimeout(qint64 ms)
{
    if (!provisional() || m_provisionalSinceMs < 0 || ms - m_provisionalSinceMs < kUnverifiedMaxMs)
        return NoChange;
    const double factorBefore = factor();
    const Record recordBefore = m_record;
    m_expired = true;
    addNote(QStringLiteral("provisional real gear %1 not verified within %2 s - dropped (light fallback gear)")
                .arg(num(m_candidate)).arg(kUnverifiedMaxMs / 1000));
    return finish(factorBefore, recordBefore);
}

int HubGearCompensation::gearSample(const Input &in)
{
    if (m_record.speed == Speed::FollowsGear)
        return NoChange;
    const TrainerSample &s = in.sample;
    const double sentRatio = in.sent.gearRatio;
    // actively pedaling, on one coherent packet
    const bool active = s.fresh(kMaxFieldAgeMs) && s.powerW >= kMinActiveWatts && s.cadenceRpm >= kMinCadenceRpm
                        && s.cadenceRpm <= kMaxCadenceRpm && s.speedMps >= kMinSpeedMps && sentRatio > 0
                        && in.circumferenceMm > 0;
    const double r = active ? s.speedMps / (s.cadenceRpm / 60.0 * in.circumferenceMm / 1000.0) : 0.0;
    if (!active || !plausible(r)) {
        m_samples.clear();   // freewheeling, stale or missing fields: start over
        return NoChange;
    }
    if (!m_samples.isEmpty()
        && (std::abs(sentRatio - m_windowSent) > m_windowSent * 0.005 || s.ms < m_samples.constLast().ms
            || s.ms - m_samples.constLast().ms > kMaxGapMs))
        m_samples.clear();
    if (m_samples.isEmpty())
        m_windowSent = sentRatio;
    m_samples.append({ s.ms, r, s.cadenceRpm });
    if (m_samples.size() < 3 || s.ms - m_samples.constFirst().ms < kWindowMs)
        return NoChange;
    QVector<double> rs;
    double cadLo = m_samples.constFirst().cadence, cadHi = cadLo;
    for (const Sample &x : std::as_const(m_samples)) {
        rs.append(x.ratio);
        cadLo = std::min(cadLo, x.cadence);
        cadHi = std::max(cadHi, x.cadence);
    }
    std::sort(rs.begin(), rs.end());
    const double median = rs.at(rs.size() / 2);
    if ((rs.constLast() - rs.constFirst()) / median > kMaxSpread || (cadHi - cadLo) / cadHi > kMaxCadenceSpread) {
        m_samples.removeFirst();   // still settling (lagging cadence): slide on
        return NoChange;
    }
    m_samples.clear();   // windows do not overlap: each is its own measurement
    return accept({ median, m_windowSent }, s.ms);
}

int HubGearCompensation::accept(const Window &w, qint64 ms)
{
    const double factorBefore = factor();
    const Record recordBefore = m_record;
    if (m_last.ratio > 0 && m_last.sent > 0) {
        const double sentStep = w.sent / m_last.sent;
        const double step = w.ratio / m_last.ratio;
        if (std::abs(sentStep - 1.0) >= kVerifyStep) {
            if (std::abs(step - 1.0) <= kVerifyTolerance) {
                if (m_record.speed != Speed::Flywheel)
                    addNote(QStringLiteral("trainer speed is the flywheel speed: sent gear %1 -> %2, real gear %3 -> %4 (unchanged)")
                                .arg(num(m_last.sent), num(w.sent), num(m_last.ratio), num(w.ratio)));
                m_record.speed = Speed::Flywheel;
            } else if (std::abs(step / sentStep - 1.0) <= kVerifyTolerance) {
                addNote(QStringLiteral("trainer speed FOLLOWS the sent gear (%1 -> %2, ratio %3 -> %4): "
                                       "real-gear compensation OFF for this trainer")
                            .arg(num(m_last.sent), num(w.sent), num(m_last.ratio), num(w.ratio)));
                m_record = Record();
                m_record.speed = Speed::FollowsGear;   // saved: the trainer stays off after a reconnect
                m_candidate = 0;
                m_last = w;
                return finish(factorBefore, recordBefore);
            }
        }
    }
    m_last = w;
    if (!(m_candidate > 0) || std::abs(w.ratio - m_candidate) > m_candidate * kUpdateThreshold) {
        const bool first = !(m_candidate > 0);
        m_candidate = w.ratio;
        // the time limit counts from the FIRST provisional value of the
        // connection - drifting values cannot keep it alive
        if (m_record.speed == Speed::Unknown && !m_expired && m_provisionalSinceMs < 0)
            m_provisionalSinceMs = ms;
        addNote(QStringLiteral("%1 real gear %2 (speed %3%4)")
                    .arg(first ? QStringLiteral("measured") : QStringLiteral("new"), num(w.ratio),
                         QString::fromLatin1(speedName(m_record.speed)),
                         m_record.speed == Speed::Unknown
                             ? (m_expired ? QStringLiteral(", unverified: not used") : QStringLiteral(", provisional - not saved"))
                             : QString()));
    }
    // saved only once the speed's meaning is verified
    if (m_record.speed == Speed::Flywheel && std::abs(m_record.realRatio - m_candidate) > 0.0005)
        m_record.realRatio = m_candidate;
    return finish(factorBefore, recordBefore);
}

int HubGearCompensation::powerSample(const Input &in)
{
    const TrainerSample &s = in.sample;
    const double prevSpeed = m_lastSpeed;
    const qint64 prevMs = m_lastSpeedMs;
    if (s.hasSpeed) {
        m_lastSpeed = s.speedMps;
        m_lastSpeedMs = s.ms;
    }
    if (m_record.calibrates || m_record.speed == Speed::FollowsGear || !s.fresh(kMaxFieldAgeMs) || !s.hasSpeed)
        return NoChange;
    const double f = factor();
    const HubSimulation &sent = in.sent;
    if (std::abs(f - 1.0) < kPowerMinFactorGap || !(in.virtualRatio > 0) || !(sent.gearRatio > 0))
        return NoChange;   // H1 and H2 hardly differ
    if (std::abs(sent.gearRatio - in.virtualRatio * f) > in.virtualRatio * f * 0.01)
        return NoChange;   // another gear was requested right now (ramp, shift in flight)
    if (s.cadenceRpm < 50 || s.cadenceRpm > kMaxCadenceRpm || s.powerW < kMinActiveWatts || s.speedMps < 3.0
        || prevSpeed < 0 || s.ms <= prevMs || s.ms - prevMs > 3000)
        return NoChange;
    const double accel = (s.speedMps - prevSpeed) / ((s.ms - prevMs) / 1000.0);
    if (std::abs(accel) > 0.15)
        return NoChange;   // the flywheel's inertia adds or gives back power
    const double mass = sent.riderKg + sent.bikeKg > 0 ? sent.riderKg + sent.bikeKg : in.fallbackMassKg;
    const double theta = std::atan(sent.inclinePercent / 100.0);
    auto roadPower = [&](double v) {
        return (0.5 * sent.cw * v * v + sent.crr * mass * road::kGravity * std::cos(theta)
                + mass * road::kGravity * std::sin(theta))
               * v;
    };
    const double v1 = s.cadenceRpm / 60.0 * in.circumferenceMm / 1000.0 * in.virtualRatio;
    const double p1 = roadPower(v1);
    const double p2 = roadPower(v1 * f);
    if (std::max(p1, p2) < 40 || std::min(p1, p2) < 10 || std::max(p1, p2) / std::min(p1, p2) < 1.5)
        return NoChange;
    const double e1 = std::abs(std::log(s.powerW / p1));
    const double e2 = std::abs(std::log(s.powerW / p2));
    if (e2 + 0.3 < e1)
        ++m_votes;
    else if (e1 + 0.3 < e2)
        --m_votes;
    else
        return NoChange;
    const double factorBefore = f;
    const Record recordBefore = m_record;
    if (m_votes >= kPowerVotes) {
        m_record.calibrates = true;
        addNote(QStringLiteral("measured power %1 W fits a trainer that calibrates the real gear itself (%2 W) rather than "
                               "%3 W - real-gear compensation OFF for this trainer")
                    .arg(s.powerW).arg(num(p2, 0), num(p1, 0)));
    } else if (m_votes <= -kPowerVotes) {
        m_votes = -kPowerVotes;   // keeps checking: the trainer may start calibrating later
        if (!m_powerConfirmed) {
            m_powerConfirmed = true;
            addNote(QStringLiteral("measured power %1 W confirms the compensated load (expected %2 W; without it %3 W)")
                        .arg(s.powerW).arg(num(p1, 0), num(roadPower(v1 / f), 0)));
        }
    }
    return finish(factorBefore, recordBefore);
}

} // namespace watt
