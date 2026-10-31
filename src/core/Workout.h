#pragma once

#include "Interval.h"
#include "Zones.h"

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <cmath>

namespace watt {

// One coloured bar of a workout profile chart.
struct Segment
{
    int startSec = 0;
    int durationSec = 0;
    int watts = 0;
    int zone = 0;   // 0..5
};

// Sport a workout / ride belongs to. Stored as lower-case text in the DB
// ("cycling" is the default for everything created before schema v2).
namespace sport {
inline QString cycling() { return QStringLiteral("cycling"); }
inline QString rowing() { return QStringLiteral("rowing"); }
// Treadmill runs (RunSession). Running workouts are stored like all other
// workouts; their intensities are fractions of the runner's AnT speed.
inline QString running() { return QStringLiteral("running"); }
// Maps any input to a known sport; unknown/empty -> cycling.
QString normalize(const QString &value);
QStringList all();
} // namespace sport

namespace running {
// Default / plausible AnT (anaerobic threshold) speed of a runner, km/h.
constexpr double kDefaultAntKmh = 12.0;
constexpr double kMinAntKmh = 5.0;
constexpr double kMaxAntKmh = 26.0;
// Running workouts reuse the "watts" of the workout code (segments, block
// watts, charts) in units of 0.1 km/h: their "FTP" is the AnT speed in these
// units (Profile::ftpFor("running")), so 100 % = AnT and 125 = 12.5 km/h.
constexpr int kUnitsPerKmh = 10;
inline int speedUnits(double kmh) { return int(std::lround(kmh * kUnitsPerKmh)); }
} // namespace running

// FTP selection for a sport: rowing workouts are prescribed as % of the
// rider's rowing FTP, everything else as % of the cycling FTP.
// Wave 2 (rowing UI / PM5 sessions) should always go through these helpers
// (or Profile::ftpFor()) instead of reading Profile::ftp directly.
int ftpForSport(const QString &sport, int ftp, int rowingFtp);
// Target watts of an interval at secondInInterval for the given sport.
int targetWatts(const Interval &interval, double secondInInterval, const QString &sport, int ftp, int rowingFtp);
inline int targetWatts(const Interval &interval, const QString &sport, int ftp, int rowingFtp)
{
    return targetWatts(interval, 0.0, sport, ftp, rowingFtp);
}

struct Workout
{
    qint64 id = -1;
    QString name;
    QString category = QStringLiteral("Intervals");   // Endurance, Intervals, Recovery, FTP Tests, Custom
    QString focus;          // short focus text, e.g. "Sweet Spot"
    QString description;
    bool favorite = false;
    bool builtIn = false;
    int timesUsed = 0;
    QString sport = sport::cycling();   // "cycling", "rowing" or "running"
    QVector<Interval> intervals;

    bool isRowing() const { return sport == sport::rowing(); }
    bool isRunning() const { return sport == sport::running(); }
    // Ridden on the bike trainer (Watt Training, Video / GPX Ride)
    bool isCycling() const { return !isRowing() && !isRunning(); }

    int totalDurationSec() const;
    int intervalIndexAt(int second) const;   // -1 when outside the workout
    int intervalStartSec(int index) const;
    int targetWattsAt(double second, int ftp) const;

    double averagePercent() const;           // time weighted mean intensity
    double intensityFactor() const;          // 4th-power mean, like NP / FTP
    double estimatedTss() const;

    QString summary(int ftp) const;          // "3 × 12 min @ 225 W"
    QString intervalsLabel() const;          // "3 × 12 min"

    QVector<Segment> segments(int ftp, const ZoneSet &zones, int rampStepSec = 60) const;
    static QVariantList segmentsToVariant(const QVector<Segment> &segments);

    QJsonArray intervalsToJson() const;
    static QVector<Interval> intervalsFromJson(const QJsonArray &array);

    static QVector<Workout> builtInLibrary();

    // Workouts in this category are FTP tests (incl. personal copies).
    static QString ftpTestCategory() { return QStringLiteral("FTP Tests"); }
    bool isFtpTest() const { return category == ftpTestCategory(); }
    // How an FTP test is evaluated (the category alone does not say):
    //   Ramp         - "ramp" in the name / focus, or the main block (longest
    //                  one that is not warm-up / recovery / cool-down) is a
    //                  rising ramp: FTP = 75 % of the best 1-minute power
    //   TwentyMinute - otherwise a block of >= kTwentyMinuteTestSec that is not
    //                  warm-up / recovery / cool-down: FTP = 95 % of the best
    //                  20-minute power
    //   None         - not an FTP test, or neither protocol is recognisable
    //                  (no FTP result is offered)
    enum class FtpTestKind { None, Ramp, TwentyMinute };
    static constexpr int kTwentyMinuteTestSec = 20 * 60;
    FtpTestKind ftpTestKind() const;
    // Running: the AnT (anaerobic threshold) test, evaluated by RunSession.
    static QString antTestCategory() { return QStringLiteral("AnT Test"); }
    bool isAntTest() const { return isRunning() && category == antTestCategory(); }
    // The built-in FTP test (and the built-in AnT test) are the basis for
    // FTP / AnT testing and can never be deleted.
    bool isProtected() const { return builtIn && (isFtpTest() || isAntTest()); }
};

int zoneForInterval(const Interval &interval, double percent, const ZoneSet &zones);

} // namespace watt
