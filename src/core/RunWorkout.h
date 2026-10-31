#pragma once

#include "Workout.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace watt {

// Running on a treadmill: workouts are prescribed as SPEED, either as a
// fraction of the runner's anaerobic threshold (AnT) speed or as an absolute
// speed in km/h, plus a suggested treadmill incline. The app never controls
// the treadmill; the runner sets the belt and the page shows how close the
// actual speed is to the target.

// One block of a running workout.
struct RunStep
{
    QString name;
    BlockType type = BlockType::Steady;
    int durationSec = 300;
    // false: start / end are fractions of the AnT speed (0.85 = 85 % AnT)
    // true:  start / end are absolute speeds in km/h
    bool absolute = false;
    double start = 0.80;
    double end = 0.80;           // == start unless the block is a ramp
    double inclinePercent = 1.0; // suggested treadmill incline

    bool isRamp() const;
    double valueAt(double secondsIntoStep) const;
    // Target belt speed (km/h) at t for the given AnT speed.
    double speedAt(double secondsIntoStep, double antKmh) const;
    // Intensity as a fraction of the AnT speed (absolute steps: speed / AnT).
    double fractionAt(double secondsIntoStep, double antKmh) const;

    QJsonObject toJson() const;
    static RunStep fromJson(const QJsonObject &o);
    QVariantMap toVariant(double antKmh) const;
    static RunStep fromVariant(const QVariantMap &m);
    bool operator==(const RunStep &o) const;
    bool operator!=(const RunStep &o) const { return !(*this == o); }
};

// The form RunSession runs. Running workouts are STORED as Workout (sport
// "running", table workouts, Builder, .awt files): fromWorkout() / toWorkout()
// convert. Interval start / end are fractions of the AnT speed there, and
// Interval::inclinePercent is the suggested incline.
struct RunWorkout
{
    static QString antTestCategory() { return Workout::antTestCategory(); }

    qint64 id = -1;
    QString name;
    QString category = QStringLiteral("Intervals");   // Endurance, Intervals, Recovery, AnT Test, Custom
    QString description;
    bool builtIn = false;
    QVector<RunStep> steps;

    bool isAntTest() const { return category == antTestCategory(); }
    int totalDurationSec() const;
    int stepIndexAt(double second) const;   // -1 outside
    int stepStartSec(int index) const;
    double targetSpeedAt(double second, double antKmh) const;    // 0 outside
    double inclineAt(double second) const;
    // AnT test: the test block (the longest step that is not warm-up /
    // recovery / cool-down); -1 when there is none.
    int antTestStepIndex() const;
    // "3 × 10 min @ 14.2 km/h" / "30 min AnT test"
    QString summary(double antKmh) const;
    // Estimated running TSS (time × IF², IF = normalized intensity vs AnT).
    double estimatedRtss(double antKmh) const;
    // [{start, duration, speed, endSpeed, fraction, zone, incline, name, type}]
    QVariantList segments(double antKmh) const;

    QJsonArray stepsToJson() const;
    static QVector<RunStep> stepsFromJson(const QJsonArray &a);
    QVariantMap toVariant(double antKmh) const;

    // absolute (km/h) steps become fractions of antKmh
    Workout toWorkout(double antKmh = running::kDefaultAntKmh) const;
    static RunWorkout fromWorkout(const Workout &w);

    // The built-in running workouts (ids -1; seeded into the database as
    // built-in Workouts of sport "running", see builtInWorkouts()).
    static QVector<RunWorkout> builtInLibrary();
    static QVector<Workout> builtInWorkouts();
};

// Running zones by % of the AnT speed (Z1 … Z6, same colours as the power zones):
//   Z1 < 78 %  recovery     Z2 78–88 %  endurance    Z3 88–95 %  tempo
//   Z4 95–100 % threshold   Z5 100–106 % VO2max      Z6 > 106 %  anaerobic
namespace runzones {
int zoneForFraction(double fractionOfAnt);
QString zoneName(int zone);
QVariantList table(double antKmh);   // [{zone, name, from, to}] km/h
} // namespace runzones

namespace running {
// (kDefaultAntKmh / kMinAntKmh / kMaxAntKmh: Workout.h)
// The AnT test: the threshold is the mean belt speed (and heart rate) of the
// LAST kAntEvalSec of the test block (30-minute time trial).
constexpr int kAntEvalSec = 20 * 60;
// Speed that costs the same on the flat (ACSM: VO2 = 0.2 v + 0.9 v g).
double gradeAdjustedKmh(double kmh, double inclinePercent);
// Estimated step cadence (steps / min) for the runner animation.
int estimatedCadence(double kmh);
// Running energy (kcal) per second, ACSM running equation.
double kcalPerSecond(double kmh, double inclinePercent, double weightKg);
// "4:17" per km (empty for < 1 km/h)
QString paceText(double kmh);

struct AntTestResult
{
    bool valid = false;
    double speedKmh = 0;
    int heartRate = 0;
    double inclinePercent = 0;
    QString message;   // why it is not valid / how it was evaluated
};
// speed / incline / hr: one value per recorded second of the WHOLE run;
// testStart / testEnd: the test block in workout seconds (record index).
AntTestResult evaluateAntTest(const QVector<double> &speedKmh, const QVector<double> &inclinePercent,
                              const QVector<int> &heartRate, int testStartSec, int testEndSec);
} // namespace running

// One AnT test / threshold value of a profile (table ant_tests).
struct AntTest
{
    qint64 id = -1;
    qint64 profileId = -1;
    QDateTime testedAt;
    double speedKmh = running::kDefaultAntKmh;
    int heartRate = 0;          // 0 = unknown
    double inclinePercent = 1.0;
    qint64 rideId = -1;         // the test run, -1 = set by hand
    QString note;
};

} // namespace watt
