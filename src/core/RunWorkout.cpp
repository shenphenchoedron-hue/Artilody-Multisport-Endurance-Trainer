#include "RunWorkout.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace watt {

// --- RunStep -------------------------------------------------------------------

bool RunStep::isRamp() const
{
    return std::abs(end - start) > 1e-6;
}

double RunStep::valueAt(double t) const
{
    if (!isRamp() || durationSec <= 0)
        return start;
    const double f = std::clamp(t / double(durationSec), 0.0, 1.0);
    return start + (end - start) * f;
}

double RunStep::speedAt(double t, double antKmh) const
{
    const double v = valueAt(t);
    return absolute ? v : v * antKmh;
}

double RunStep::fractionAt(double t, double antKmh) const
{
    const double v = valueAt(t);
    if (!absolute)
        return v;
    return antKmh > 0 ? v / antKmh : 0.0;
}

QJsonObject RunStep::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("type"), blockTypeToString(type));
    o.insert(QStringLiteral("duration"), durationSec);
    o.insert(QStringLiteral("mode"), absolute ? QStringLiteral("speed") : QStringLiteral("ant"));
    o.insert(QStringLiteral("start"), start);
    o.insert(QStringLiteral("end"), end);
    o.insert(QStringLiteral("incline"), inclinePercent);
    return o;
}

RunStep RunStep::fromJson(const QJsonObject &o)
{
    RunStep s;
    s.type = blockTypeFromString(o.value(QStringLiteral("type")).toString());
    s.name = o.value(QStringLiteral("name")).toString(blockTypeDisplayName(s.type));
    s.durationSec = std::clamp(o.value(QStringLiteral("duration")).toInt(300), 5, 6 * 3600);
    s.absolute = o.value(QStringLiteral("mode")).toString() == QLatin1String("speed");
    const double lo = s.absolute ? 1.0 : 0.3;
    const double hi = s.absolute ? 30.0 : 1.6;
    s.start = std::clamp(o.value(QStringLiteral("start")).toDouble(s.absolute ? 10.0 : 0.8), lo, hi);
    s.end = std::clamp(o.value(QStringLiteral("end")).toDouble(s.start), lo, hi);
    s.inclinePercent = std::clamp(o.value(QStringLiteral("incline")).toDouble(1.0), -5.0, 25.0);
    return s;
}

QVariantMap RunStep::toVariant(double antKmh) const
{
    QVariantMap m;
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("type"), blockTypeToString(type));
    m.insert(QStringLiteral("typeName"), blockTypeDisplayName(type));
    m.insert(QStringLiteral("duration"), durationSec);
    m.insert(QStringLiteral("mode"), absolute ? QStringLiteral("speed") : QStringLiteral("ant"));
    m.insert(QStringLiteral("start"), start);
    m.insert(QStringLiteral("end"), end);
    m.insert(QStringLiteral("ramp"), isRamp());
    m.insert(QStringLiteral("incline"), inclinePercent);
    m.insert(QStringLiteral("speed"), speedAt(0, antKmh));
    m.insert(QStringLiteral("endSpeed"), speedAt(durationSec, antKmh));
    m.insert(QStringLiteral("percent"), int(std::lround(fractionAt(0, antKmh) * 100)));
    m.insert(QStringLiteral("endPercent"), int(std::lround(fractionAt(durationSec, antKmh) * 100)));
    m.insert(QStringLiteral("zone"), runzones::zoneForFraction(fractionAt(durationSec / 2.0, antKmh)));
    return m;
}

RunStep RunStep::fromVariant(const QVariantMap &m)
{
    QJsonObject o = QJsonObject::fromVariantMap(m);
    return fromJson(o);
}

bool RunStep::operator==(const RunStep &o) const
{
    return name == o.name && type == o.type && durationSec == o.durationSec && absolute == o.absolute
        && qFuzzyCompare(1.0 + start, 1.0 + o.start) && qFuzzyCompare(1.0 + end, 1.0 + o.end)
        && qFuzzyCompare(1.0 + inclinePercent, 1.0 + o.inclinePercent);
}

// --- RunWorkout ----------------------------------------------------------------

int RunWorkout::totalDurationSec() const
{
    int total = 0;
    for (const RunStep &s : steps)
        total += s.durationSec;
    return total;
}

int RunWorkout::stepIndexAt(double second) const
{
    if (second < 0)
        return -1;
    double start = 0;
    for (int i = 0; i < steps.size(); ++i) {
        start += steps.at(i).durationSec;
        if (second < start)
            return i;
    }
    return -1;
}

int RunWorkout::stepStartSec(int index) const
{
    int start = 0;
    for (int i = 0; i < index && i < steps.size(); ++i)
        start += steps.at(i).durationSec;
    return start;
}

double RunWorkout::targetSpeedAt(double second, double antKmh) const
{
    const int i = stepIndexAt(second);
    if (i < 0)
        return 0.0;
    return steps.at(i).speedAt(second - stepStartSec(i), antKmh);
}

double RunWorkout::inclineAt(double second) const
{
    const int i = stepIndexAt(second);
    return i < 0 ? 0.0 : steps.at(i).inclinePercent;
}

int RunWorkout::antTestStepIndex() const
{
    int best = -1;
    for (int i = 0; i < steps.size(); ++i) {
        const RunStep &s = steps.at(i);
        if (s.type == BlockType::WarmUp || s.type == BlockType::Recovery || s.type == BlockType::CoolDown)
            continue;
        if (best < 0 || s.durationSec > steps.at(best).durationSec)
            best = i;
    }
    return best;
}

namespace {
QString minutesText(int sec)
{
    if (sec % 60 == 0)
        return QStringLiteral("%1 min").arg(sec / 60);
    if (sec < 60)
        return QStringLiteral("%1 s").arg(sec);
    return QStringLiteral("%1:%2 min").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
}
bool isWork(const RunStep &s)
{
    return s.type == BlockType::Interval || s.type == BlockType::Steady || s.type == BlockType::Ramp;
}
} // namespace

QString RunWorkout::summary(double antKmh) const
{
    if (steps.isEmpty())
        return QStringLiteral("Empty workout");
    if (isAntTest()) {
        const int t = antTestStepIndex();
        return t >= 0 ? QStringLiteral("%1 AnT test").arg(minutesText(steps.at(t).durationSec)) : QStringLiteral("AnT test");
    }
    int best = -1;
    for (int i = 0; i < steps.size(); ++i) {
        if (!isWork(steps.at(i)))
            continue;
        if (best < 0 || steps.at(i).fractionAt(0, antKmh) > steps.at(best).fractionAt(0, antKmh) + 1e-6)
            best = i;
    }
    if (best < 0)
        best = 0;
    const RunStep &b = steps.at(best);
    int count = 0;
    for (const RunStep &s : steps) {
        if (s.durationSec == b.durationSec && std::abs(s.start - b.start) < 1e-6 && std::abs(s.end - b.end) < 1e-6
            && s.absolute == b.absolute)
            ++count;
    }
    if (b.isRamp())
        return QStringLiteral("%1 %2 → %3 km/h").arg(minutesText(b.durationSec))
            .arg(b.speedAt(0, antKmh), 0, 'f', 1).arg(b.speedAt(b.durationSec, antKmh), 0, 'f', 1);
    const QString speed = QStringLiteral("%1 km/h").arg(b.speedAt(0, antKmh), 0, 'f', 1);
    if (count > 1)
        return QStringLiteral("%1 × %2 @ %3").arg(count).arg(minutesText(b.durationSec), speed);
    return QStringLiteral("%1 @ %2").arg(minutesText(b.durationSec), speed);
}

double RunWorkout::estimatedRtss(double antKmh) const
{
    const int total = totalDurationSec();
    if (total <= 0 || antKmh <= 0)
        return 0.0;
    double sum = 0;
    for (const RunStep &s : steps) {
        for (int t = 0; t < s.durationSec; t += 5) {
            const double v = running::gradeAdjustedKmh(s.speedAt(t + 2.5, antKmh), s.inclinePercent) / antKmh;
            sum += std::pow(v, 4.0) * std::min(5, s.durationSec - t);
        }
    }
    const double ifactor = std::pow(sum / total, 0.25);
    return total / 3600.0 * ifactor * ifactor * 100.0;
}

QVariantList RunWorkout::segments(double antKmh) const
{
    QVariantList out;
    int start = 0;
    for (const RunStep &s : steps) {
        QVariantMap m = s.toVariant(antKmh);
        m.insert(QStringLiteral("start"), start);
        m.insert(QStringLiteral("fraction"), s.fractionAt(s.durationSec / 2.0, antKmh));
        out.append(m);
        start += s.durationSec;
    }
    return out;
}

QJsonArray RunWorkout::stepsToJson() const
{
    QJsonArray a;
    for (const RunStep &s : steps)
        a.append(s.toJson());
    return a;
}

QVector<RunStep> RunWorkout::stepsFromJson(const QJsonArray &a)
{
    QVector<RunStep> out;
    out.reserve(a.size());
    for (const QJsonValue &v : a)
        out.append(RunStep::fromJson(v.toObject()));
    return out;
}

QVariantMap RunWorkout::toVariant(double antKmh) const
{
    QVariantMap m;
    m.insert(QStringLiteral("workoutId"), id);
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("category"), category);
    m.insert(QStringLiteral("description"), description);
    m.insert(QStringLiteral("builtIn"), builtIn);
    m.insert(QStringLiteral("antTest"), isAntTest());
    m.insert(QStringLiteral("totalSec"), totalDurationSec());
    m.insert(QStringLiteral("summary"), summary(antKmh));
    m.insert(QStringLiteral("rtss"), int(std::lround(estimatedRtss(antKmh))));
    m.insert(QStringLiteral("stepCount"), int(steps.size()));
    m.insert(QStringLiteral("segments"), segments(antKmh));
    return m;
}

namespace {
RunStep step(BlockType t, const QString &name, double minutes, double start, double end = -1, double incline = 1.0)
{
    RunStep s;
    s.type = t;
    s.name = name;
    s.durationSec = int(std::lround(minutes * 60));
    s.start = start;
    s.end = end < 0 ? start : end;
    s.inclinePercent = incline;
    return s;
}
} // namespace

QVector<RunWorkout> RunWorkout::builtInLibrary()
{
    QVector<RunWorkout> lib;
    {
        RunWorkout w;
        w.name = QStringLiteral("AnT Test – 30 min");
        w.category = antTestCategory();
        w.description = QStringLiteral(
            "Find your anaerobic threshold (AnT). After the warm-up, run 30 minutes as fast as you can hold "
            "an EVEN pace – set the belt yourself, the target is only your current AnT. Your new AnT speed and "
            "heart rate are the averages of the last 20 minutes. Keep the incline at 1 % and repeat the test "
            "every 6–8 weeks.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Warm Up"), 10, 0.65, 0.82),
            step(BlockType::Steady, QStringLiteral("AnT Test – even, hard pace"), 30, 1.00),
            step(BlockType::CoolDown, QStringLiteral("Cool Down"), 5, 0.65, 0.55),
        };
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Easy Run 40");
        w.category = QStringLiteral("Endurance");
        w.description = QStringLiteral("Relaxed aerobic running in zone 2 – conversational pace.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Warm Up"), 5, 0.65, 0.74),
            step(BlockType::Steady, QStringLiteral("Easy"), 30, 0.78),
            step(BlockType::CoolDown, QStringLiteral("Cool Down"), 5, 0.72, 0.62),
        };
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Threshold 3 × 10");
        w.category = QStringLiteral("Intervals");
        w.description = QStringLiteral("Three 10-minute blocks at your AnT speed with 3 minutes easy jogging. "
                                       "Raises the speed you can hold before lactate accumulates.");
        w.steps.append(step(BlockType::WarmUp, QStringLiteral("Warm Up"), 10, 0.65, 0.82));
        for (int r = 0; r < 3; ++r) {
            w.steps.append(step(BlockType::Interval, QStringLiteral("Threshold"), 10, 1.00));
            if (r < 2)
                w.steps.append(step(BlockType::Recovery, QStringLiteral("Jog"), 3, 0.70));
        }
        w.steps.append(step(BlockType::CoolDown, QStringLiteral("Cool Down"), 7, 0.70, 0.60));
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Tempo 25");
        w.category = QStringLiteral("Endurance");
        w.description = QStringLiteral("25 minutes comfortably hard at 90 % of AnT speed.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Warm Up"), 10, 0.65, 0.80),
            step(BlockType::Steady, QStringLiteral("Tempo"), 25, 0.90),
            step(BlockType::CoolDown, QStringLiteral("Cool Down"), 8, 0.70, 0.60),
        };
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("VO₂ Max 6 × 3");
        w.category = QStringLiteral("Intervals");
        w.description = QStringLiteral("Six hard 3-minute efforts at 110 % of AnT speed with 2 minutes jogging.");
        w.steps.append(step(BlockType::WarmUp, QStringLiteral("Warm Up"), 12, 0.65, 0.85));
        for (int r = 0; r < 6; ++r) {
            w.steps.append(step(BlockType::Interval, QStringLiteral("VO₂ Max"), 3, 1.10));
            if (r < 5)
                w.steps.append(step(BlockType::Recovery, QStringLiteral("Jog"), 2, 0.65));
        }
        w.steps.append(step(BlockType::CoolDown, QStringLiteral("Cool Down"), 8, 0.68, 0.58));
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Hill Repeats 8 × 90 s");
        w.category = QStringLiteral("Intervals");
        w.description = QStringLiteral("Eight 90-second climbs at 6 % incline, easy jog on the flat between them.");
        w.steps.append(step(BlockType::WarmUp, QStringLiteral("Warm Up"), 10, 0.65, 0.80));
        for (int r = 0; r < 8; ++r) {
            w.steps.append(step(BlockType::Interval, QStringLiteral("Hill"), 1.5, 0.88, -1, 6.0));
            w.steps.append(step(BlockType::Recovery, QStringLiteral("Jog"), 2, 0.65, -1, 1.0));
        }
        w.steps.append(step(BlockType::CoolDown, QStringLiteral("Cool Down"), 6, 0.68, 0.58));
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Progression Run 40");
        w.category = QStringLiteral("Endurance");
        w.description = QStringLiteral("Start easy and finish close to threshold – the speed rises steadily.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Warm Up"), 5, 0.65, 0.75),
            step(BlockType::Ramp, QStringLiteral("Progression"), 30, 0.76, 0.98),
            step(BlockType::CoolDown, QStringLiteral("Cool Down"), 5, 0.70, 0.60),
        };
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Long Run 75");
        w.category = QStringLiteral("Endurance");
        w.description = QStringLiteral("An hour in zone 2 – the backbone of endurance.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Warm Up"), 10, 0.65, 0.76),
            step(BlockType::Steady, QStringLiteral("Endurance"), 60, 0.80),
            step(BlockType::CoolDown, QStringLiteral("Cool Down"), 5, 0.72, 0.62),
        };
        lib.append(w);
    }
    {
        RunWorkout w;
        w.name = QStringLiteral("Recovery Jog 30");
        w.category = QStringLiteral("Recovery");
        w.description = QStringLiteral("Very easy jogging to recover between hard days.");
        w.steps = {
            step(BlockType::WarmUp, QStringLiteral("Easy Start"), 3, 0.60, 0.68),
            step(BlockType::Recovery, QStringLiteral("Jog"), 24, 0.70),
            step(BlockType::CoolDown, QStringLiteral("Walk / Jog"), 3, 0.65, 0.50),
        };
        lib.append(w);
    }
    for (RunWorkout &w : lib)
        w.builtIn = true;
    return lib;
}

QVector<Workout> RunWorkout::builtInWorkouts()
{
    QVector<Workout> out;
    for (const RunWorkout &r : builtInLibrary())
        out.append(r.toWorkout());
    return out;
}

Workout RunWorkout::toWorkout(double antKmh) const
{
    Workout w;
    w.id = id;
    w.name = name;
    w.category = category;
    w.description = description;
    w.builtIn = builtIn;
    w.sport = sport::running();
    for (const RunStep &s : steps) {
        Interval i;
        i.name = s.name;
        i.type = s.type;
        i.durationSec = s.durationSec;
        i.startPercent = s.fractionAt(0, antKmh);
        i.endPercent = s.fractionAt(s.durationSec, antKmh);
        if (!s.isRamp())
            i.endPercent = i.startPercent;
        i.inclinePercent = s.inclinePercent;
        w.intervals.append(i);
    }
    return w;
}

RunWorkout RunWorkout::fromWorkout(const Workout &w)
{
    RunWorkout r;
    r.id = w.id;
    r.name = w.name;
    r.category = w.category;
    r.description = w.description;
    r.builtIn = w.builtIn;
    for (const Interval &i : w.intervals) {
        RunStep s;
        s.name = i.name;
        s.type = i.type;
        s.durationSec = i.durationSec;
        s.absolute = false;
        s.start = i.startPercent;
        s.end = i.endPercent;
        s.inclinePercent = i.inclinePercent;
        r.steps.append(s);
    }
    return r;
}

// --- zones -----------------------------------------------------------------------

namespace runzones {
namespace {
constexpr double kBounds[] = { 0.78, 0.88, 0.95, 1.00, 1.06 };
}

int zoneForFraction(double f)
{
    // same bounds as ZoneSet::running()
    return ZoneSet::running().zoneForFraction(f + 1e-9);
}

QString zoneName(int zone)
{
    static const char *names[] = { "Recovery", "Endurance", "Tempo", "Threshold", "VO₂ Max", "Anaerobic" };
    return QString::fromUtf8(names[std::clamp(zone, 0, 5)]);
}

QVariantList table(double antKmh)
{
    QVariantList out;
    double from = 0;
    for (int z = 0; z < 6; ++z) {
        const double to = z < 5 ? kBounds[z] : 0.0;
        QVariantMap m;
        m.insert(QStringLiteral("zone"), z);
        m.insert(QStringLiteral("name"), zoneName(z));
        m.insert(QStringLiteral("fromPercent"), int(std::lround(from * 100)));
        m.insert(QStringLiteral("toPercent"), z < 5 ? int(std::lround(to * 100)) : 0);
        m.insert(QStringLiteral("from"), from * antKmh);
        m.insert(QStringLiteral("to"), z < 5 ? to * antKmh : 0.0);
        out.append(m);
        from = to;
    }
    return out;
}
} // namespace runzones

// --- running helpers ----------------------------------------------------------------

namespace running {

double gradeAdjustedKmh(double kmh, double inclinePercent)
{
    // ACSM: VO2 = 0.2 v + 0.9 v g  (v m/min) -> equal cost on the flat at v (1 + 4.5 g).
    // Downhill grades are not rewarded beyond -2 % (treadmills rarely go below 0).
    const double g = std::clamp(inclinePercent, -2.0, 30.0) / 100.0;
    return std::max(0.0, kmh * (1.0 + 4.5 * g));
}

int estimatedCadence(double kmh)
{
    if (kmh < 1.0)
        return 0;
    if (kmh < 6.5)   // walking
        return int(std::lround(std::clamp(95.0 + 4.5 * kmh, 100.0, 130.0)));
    return int(std::lround(std::clamp(140.0 + 2.4 * kmh, 150.0, 200.0)));
}

double kcalPerSecond(double kmh, double inclinePercent, double weightKg)
{
    if (kmh <= 0 || weightKg <= 0)
        return 0.0;
    const double vMin = kmh * 1000.0 / 60.0;   // m/min
    const double g = std::max(0.0, inclinePercent) / 100.0;
    const bool walking = kmh < 6.5;
    // ml O2 / kg / min (net, without resting 3.5)
    const double vo2 = walking ? 0.1 * vMin + 1.8 * vMin * g : 0.2 * vMin + 0.9 * vMin * g;
    // ~5 kcal per litre O2
    return vo2 * weightKg / 1000.0 * 5.0 / 60.0;
}

QString paceText(double kmh)
{
    if (kmh < 1.0)
        return QString();
    const int sec = int(std::lround(3600.0 / kmh));
    return QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
}

AntTestResult evaluateAntTest(const QVector<double> &speed, const QVector<double> &incline, const QVector<int> &hr,
                              int testStart, int testEnd)
{
    AntTestResult r;
    const int n = int(speed.size());
    const int end = std::min(testEnd, n);
    if (testEnd <= testStart || end < testEnd) {
        r.message = QStringLiteral("The 30-minute test block was not completed – no new AnT.");
        return r;
    }
    const int from = std::max(testStart, end - kAntEvalSec);
    if (end - from < kAntEvalSec * 3 / 4) {
        r.message = QStringLiteral("The test block is too short to evaluate.");
        return r;
    }
    double sumV = 0, sumI = 0;
    qint64 sumHr = 0;
    int hrSec = 0, moving = 0;
    for (int i = from; i < end; ++i) {
        const double v = speed.at(i);
        if (v > 1.0)
            ++moving;
        sumV += v;
        sumI += i < incline.size() ? incline.at(i) : 0.0;
        const int h = i < hr.size() ? hr.at(i) : 0;
        if (h > 0) {
            sumHr += h;
            ++hrSec;
        }
    }
    const int len = end - from;
    if (moving < len * 9 / 10) {
        r.message = QStringLiteral("The belt stood still for more than 10 % of the evaluated time – no new AnT.");
        return r;
    }
    r.speedKmh = sumV / len;
    r.inclinePercent = sumI / len;
    r.heartRate = hrSec >= len / 2 ? int(std::lround(double(sumHr) / hrSec)) : 0;
    if (r.speedKmh < kMinAntKmh || r.speedKmh > kMaxAntKmh) {
        r.message = QStringLiteral("The measured speed (%1 km/h) is outside the plausible range.").arg(r.speedKmh, 0, 'f', 1);
        return r;
    }
    r.valid = true;
    r.message = QStringLiteral("Mean of the last %1 minutes of the test").arg(len / 60);
    return r;
}

} // namespace running

} // namespace watt
