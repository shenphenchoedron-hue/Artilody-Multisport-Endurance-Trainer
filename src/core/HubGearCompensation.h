#pragma once

#include "SensorHub.h"   // HubSimulation, TrainerSample

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace watt {

// Zwift hub path: the virtual gear adapted to the real gear the bike sits in
// on the trainer (pure logic; VirtualGears feeds it one update per trainer
// packet and keeps its record per trainer).
//
// MODEL - an assumption, not part of any published Zwift protocol: a trainer
// in the Zwift protocol brakes for v_trainer = v_flywheel × sent ratio /
// kTrainerRatio, i.e. it takes the flywheel as turning in 34×14 (34T chainring
// on the 14T Zwift Cog). 34/14 is SHIFTR's standard ratio for FE-C trainers
// (github.com/JuergenLeber/SHIFTR, VirtualShifting.md), not a JetBlack spec;
// it fits the user's Victory logs (power at gear 12 = road power of the
// flywheel speed, fit 2.2 … 2.5). Compensation:
//     sent ratio = virtual ratio × factor,  factor = kTrainerRatio / R_real
// Verifying that the trainer's speed is its flywheel speed does NOT verify
// this formula or the resulting load - only the power check touches that.
//
// R_real = trainer speed / (cadence × circumference), from WINDOWS of
// coherent packets (TrainerSample: all fields carried within kMaxFieldAgeMs)
// while ACTIVELY PEDALING: power >= kMinActiveWatts and cadence >=
// kMinCadenceRpm in every sample - freewheeling (0 W while the trainer still
// reports a lagging cadence, log of 1.3.4) never measures. A window spans
// >= kWindowMs at one unchanged sent ratio with ratio spread <= kMaxSpread
// and cadence spread <= kMaxCadenceSpread.
//
// Speed meaning (speed(), saved per trainer):
//   Unknown     - not tested yet
//   Flywheel    - two windows with sent ratios >= kVerifyStep apart gave the
//                 same R (±kVerifyTolerance): R is the real gear
//   FollowsGear - R moved with the sent ratio: a speed derived from the gear;
//                 compensation off for this trainer (saved: survives a
//                 reconnect; VirtualGears::resetHubGearAssessment() clears it)
// The test needs a change of the sent ratio: a shift, the soft start's light
// gear, or the compensation itself. So while Unknown a window's R is applied
// PROVISIONALLY (the start must get lighter) - never saved, and dropped
// after kUnverifiedMaxMs without verification (data or no data: tick()).
// Then realRatio() = 0: RideResistance falls back to the light soft-start
// gear, ramped from the last sent ratio (no harder gear appears suddenly);
// that change of the sent ratio is one more chance to verify.
//
// Reuse: a saved R only on the SAME trainer, only if its speed was verified
// as Flywheel, and only until this connection's first window.
//
// Power check (a trainer calibrating the real gear itself, like the Zwift
// Hub's "real gear ratio calibration", would be compensated twice): steady
// active samples compare the measured power with the road power of
// H1 (trainer not calibrated) v = cadence × circumference × virtual ratio and
// H2 (calibrated itself) v = the same × factor; H2 clearly closer kPowerVotes
// more often: calibrates (saved, compensation off). Keeps running.
class HubGearCompensation
{
public:
    enum class Speed { Unknown, Flywheel, FollowsGear };
    struct Record
    {
        double realRatio = 0;          // only with speed == Flywheel
        Speed speed = Speed::Unknown;
        bool calibrates = false;       // the trainer compensates the real gear itself
        bool operator==(const Record &o) const
        {
            return realRatio == o.realRatio && speed == o.speed && calibrates == o.calibrates;
        }
        bool operator!=(const Record &o) const { return !(*this == o); }
        QString toString() const;                       // "<ratio>;<speed>;<calibrates>"
        static Record fromString(const QString &s);     // malformed: empty record
    };
    // one trainer packet with what the trainer had at that moment
    struct Input
    {
        TrainerSample sample;
        HubSimulation sent;            // the last requested hub command (not confirmed by the trainer)
        double virtualRatio = 0;       // the gear the rider chose
        int circumferenceMm = 2105;
        double fallbackMassKg = 77;
    };

    static constexpr double kTrainerRatio = 34.0 / 14.0;   // 2.4286 (model)
    static constexpr double kMinFactor = 0.4;
    static constexpr double kMaxFactor = 2.0;
    static constexpr qint64 kMaxFieldAgeMs = 1500;   // every field of the packet this recent
    static constexpr double kMinActiveWatts = 20;     // pedaling, not freewheeling
    static constexpr double kMinCadenceRpm = 30;
    static constexpr double kMaxCadenceRpm = 130;
    static constexpr double kMinSpeedMps = 2.0;
    static constexpr double kMinRatio = 0.6;
    static constexpr double kMaxRatio = 6.0;
    static constexpr int kWindowMs = 2500;            // three of the trainer's 1 Hz packets
    static constexpr int kMaxGapMs = 1500;
    static constexpr double kMaxSpread = 0.05;        // steady riding in the logs: 2–4 %
    static constexpr double kMaxCadenceSpread = 0.08;
    static constexpr double kVerifyStep = 0.10;
    static constexpr double kVerifyTolerance = 0.05;
    static constexpr double kUpdateThreshold = 0.03;
    static constexpr qint64 kUnverifiedMaxMs = 20000; // provisional R without verification
    static constexpr int kPowerVotes = 20;
    static constexpr double kPowerMinFactorGap = 0.1;

    enum Change { NoChange = 0, FactorChanged = 1, RecordChanged = 2 };

    void reset();                        // unknown trainer
    void reset(const Record &saved);     // a new connection to a trainer with this record
    int update(const Input &in);         // one packet
    int tick(qint64 ms);                 // time passes without packets (timeouts)

    Speed speed() const { return m_record.speed; }
    double realRatio() const;            // in use, 0 = none (light fallback)
    double candidateRatio() const { return m_candidate; }   // this connection's latest window
    bool provisional() const;            // applied but not verified
    bool provisionalExpired() const { return m_expired; }
    double factor() const;               // 1.0 = no compensation
    bool calibrates() const { return m_record.calibrates; }
    bool powerConfirmed() const { return m_powerConfirmed; }
    Record record() const { return m_record; }   // what to save for this trainer
    QString lastNote() const { return m_note; }  // reason of the last change ("" = none)
    static const char *speedName(Speed s);

private:
    struct Sample { qint64 ms; double ratio; double cadence; };
    struct Window { double ratio = 0; double sent = 0; };
    int gearSample(const Input &in);
    int powerSample(const Input &in);
    int accept(const Window &w, qint64 ms);
    int checkTimeout(qint64 ms);
    int finish(double factorBefore, const Record &recordBefore) const;
    void addNote(const QString &n);

    Record m_record;
    double m_candidate = 0;              // latest window R of this connection
    qint64 m_provisionalSinceMs = -1;    // when the unverified R was applied
    bool m_expired = false;              // the provisional R timed out: unverified Rs stay unused
    QVector<Sample> m_samples;
    double m_windowSent = 0;
    Window m_last;
    bool m_powerConfirmed = false;
    int m_votes = 0;
    double m_lastSpeed = -1;
    qint64 m_lastSpeedMs = 0;
    QString m_note;
};

} // namespace watt
