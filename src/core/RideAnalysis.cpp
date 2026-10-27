#include "RideAnalysis.h"

#include <algorithm>
#include <cmath>

namespace watt::analysis {

double normalizedPower(const QVector<int> &p)
{
    if (p.isEmpty())
        return 0.0;
    constexpr int window = 30;
    if (p.size() < window) {
        double sum = 0;
        for (int v : p)
            sum += v;
        return sum / p.size();
    }
    double rolling = 0;
    for (int i = 0; i < window; ++i)
        rolling += p.at(i);
    double sum4 = std::pow(rolling / window, 4.0);
    int count = 1;
    for (int i = window; i < p.size(); ++i) {
        rolling += p.at(i) - p.at(i - window);
        sum4 += std::pow(rolling / window, 4.0);
        ++count;
    }
    return std::pow(sum4 / count, 0.25);
}

RideStats analyze(const QVector<Sample> &samples, int ftp, const ZoneSet &zones)
{
    RideStats st;
    if (samples.isEmpty())
        return st;

    QVector<int> power;
    power.reserve(samples.size());
    double sumPower = 0, sumHr = 0, sumCad = 0;
    int hrCount = 0, cadCount = 0;
    for (const Sample &s : samples) {
        const int pw = std::max(0, s.power);
        power.append(pw);
        sumPower += pw;
        st.maxPower = std::max(st.maxPower, pw);
        if (s.heartRate > 0) {
            sumHr += s.heartRate;
            ++hrCount;
            st.maxHeartRate = std::max(st.maxHeartRate, s.heartRate);
        }
        if (s.cadence > 0) {
            sumCad += s.cadence;
            ++cadCount;
        }
        if (ftp > 0)
            ++st.zoneSeconds[size_t(zones.zoneForWatts(pw, ftp))];
    }
    st.durationSec = samples.size();
    st.avgPower = sumPower / samples.size();
    st.avgHeartRate = hrCount ? sumHr / hrCount : 0.0;
    st.avgCadence = cadCount ? sumCad / cadCount : 0.0;
    st.kilojoules = sumPower / 1000.0;   // 1 Hz => joules = sum of watts
    st.normalizedPower = normalizedPower(power);
    if (ftp > 0) {
        st.intensityFactor = st.normalizedPower / ftp;
        st.tss = (st.durationSec * st.normalizedPower * st.intensityFactor) / (ftp * 3600.0) * 100.0;
    }
    return st;
}

void applyStats(RideRecord &r, const RideStats &st)
{
    r.durationSec = st.durationSec;
    r.avgPower = st.avgPower;
    r.maxPower = st.maxPower;
    r.normalizedPower = st.normalizedPower;
    r.avgHeartRate = st.avgHeartRate;
    r.maxHeartRate = st.maxHeartRate;
    r.avgCadence = st.avgCadence;
    r.kilojoules = st.kilojoules;
    r.intensityFactor = st.intensityFactor;
    r.tss = st.tss;
    r.zoneSeconds = st.zoneSeconds;
}

int bestAveragePower(const QVector<Sample> &samples, int windowSec)
{
    if (windowSec <= 0 || samples.size() < windowSec)
        return 0;
    qint64 rolling = 0;
    for (int i = 0; i < windowSec; ++i)
        rolling += std::max(0, samples.at(i).power);
    qint64 best = rolling;
    for (int i = windowSec; i < samples.size(); ++i) {
        rolling += std::max(0, samples.at(i).power) - std::max(0, samples.at(i - windowSec).power);
        best = std::max(best, rolling);
    }
    return int(std::lround(double(best) / windowSec));
}

int estimateFtpFromRamp(const QVector<Sample> &samples)
{
    const int best = bestAveragePower(samples, kRampTestWindowSec);
    if (best <= 0)
        return 0;
    return int(std::lround(kRampTestFtpFactor * best));
}

int plausibleTwentyMinuteFtp(const QVector<Sample> &samples)
{
    if (samples.size() < kTwentyMinuteTestWindowSec)
        return 0;
    const int best = bestAveragePower(samples, kTwentyMinuteTestWindowSec);
    if (best <= 0)
        return 0;
    const int ftp = int(std::lround(kTwentyMinuteTestFtpFactor * best));
    return ftp >= Profile::kMinFtp && ftp <= Profile::kMaxFtp ? ftp : 0;
}

FtpEstimate ftpTestEstimate(const QVector<Sample> &samples, Workout::FtpTestKind kind)
{
    FtpEstimate e;
    switch (kind) {
    case Workout::FtpTestKind::Ramp:
        e.method = QStringLiteral("ramp");
        e.bestPower = bestAveragePower(samples, kRampTestWindowSec);
        e.ftp = plausibleRampFtp(samples);
        break;
    case Workout::FtpTestKind::TwentyMinute:
        e.method = QStringLiteral("20min");
        e.bestPower = bestAveragePower(samples, kTwentyMinuteTestWindowSec);
        e.ftp = plausibleTwentyMinuteFtp(samples);
        break;
    case Workout::FtpTestKind::None:
        break;
    }
    return e;
}

int plausibleRampFtp(const QVector<Sample> &samples)
{
    if (samples.size() < kMinFtpTestSec)
        return 0;
    const int ftp = estimateFtpFromRamp(samples);
    return ftp >= Profile::kMinFtp && ftp <= Profile::kMaxFtp ? ftp : 0;
}

} // namespace watt::analysis
