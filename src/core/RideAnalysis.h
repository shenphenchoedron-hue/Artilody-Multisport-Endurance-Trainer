#pragma once

#include "Profile.h"
#include "Ride.h"
#include "Workout.h"
#include "Zones.h"

#include <QVector>
#include <array>

namespace watt {

struct RideStats
{
    int durationSec = 0;
    double avgPower = 0;
    int maxPower = 0;
    double normalizedPower = 0;
    double avgHeartRate = 0;     // ignores samples without HR
    int maxHeartRate = 0;
    double avgCadence = 0;       // ignores samples with cadence 0 (coasting)
    double kilojoules = 0;
    double intensityFactor = 0;
    double tss = 0;
    std::array<int, kZoneCount> zoneSeconds {};
};

namespace analysis {

// Normalized Power (Coggan): 30 s rolling average, 4th power mean, 4th root.
// Samples are assumed to be 1 Hz.
double normalizedPower(const QVector<int> &powerPerSecond);
RideStats analyze(const QVector<Sample> &samples, int ftp, const ZoneSet &zones = {});
// Fills the summary fields of a RideRecord from stats.
void applyStats(RideRecord &record, const RideStats &stats);

// Best rolling average power over windowSec seconds (1 Hz samples), rounded
// to whole watts. 0 when there are fewer samples than the window.
int bestAveragePower(const QVector<Sample> &samples, int windowSec);

// Ramp test: FTP = round(0.75 x best 1-minute power).
constexpr int kRampTestWindowSec = 60;
constexpr double kRampTestFtpFactor = 0.75;
// Minimum recorded riding time before a ramp test result is trusted.
constexpr int kMinFtpTestSec = 5 * 60;

// 0 when there is not enough data (< 60 samples or best power <= 0).
int estimateFtpFromRamp(const QVector<Sample> &samples);
// The estimate only when it is plausible: at least kMinFtpTestSec of samples
// and within the valid profile FTP range (50-600 W); otherwise 0.
int plausibleRampFtp(const QVector<Sample> &samples);

// 20-minute test: FTP = round(0.95 x best 20-minute power).
constexpr int kTwentyMinuteTestWindowSec = 20 * 60;
constexpr double kTwentyMinuteTestFtpFactor = 0.95;
// 0 with fewer than 20 minutes of samples or outside the profile FTP range.
int plausibleTwentyMinuteFtp(const QVector<Sample> &samples);

struct FtpEstimate
{
    int ftp = 0;          // 0: no plausible result
    int bestPower = 0;    // best 1-minute (ramp) / 20-minute power
    QString method;       // "ramp" / "20min", empty for None
};
// The result of an FTP test ride evaluated with the workout's protocol.
FtpEstimate ftpTestEstimate(const QVector<Sample> &samples, Workout::FtpTestKind kind);

} // namespace analysis
} // namespace watt
