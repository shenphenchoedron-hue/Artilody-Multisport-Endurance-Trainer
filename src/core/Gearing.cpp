#include "Gearing.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace watt {

namespace {

QVector<int> sortedUnique(QVector<int> v)
{
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

QJsonArray toArray(const QVector<int> &v)
{
    QJsonArray a;
    for (int x : v)
        a.append(x);
    return a;
}

QVector<int> fromArray(const QJsonArray &a)
{
    QVector<int> v;
    for (const QJsonValue &x : a) {
        if (!x.isDouble())
            return {};
        v.append(x.toInt());
    }
    return v;
}

QVariantMap preset(const QString &name, const QVector<int> &values)
{
    QVariantList list;
    for (int v : values)
        list.append(v);
    QStringList text;
    for (int v : values)
        text << QString::number(v);
    return QVariantMap { { QStringLiteral("name"), name },
                         { QStringLiteral("values"), list },
                         { QStringLiteral("text"), text.join(QLatin1Char(',')) } };
}

} // namespace

// --- GearSetup -----------------------------------------------------------------

GearSetup GearSetup::defaults()
{
    GearSetup s;
    // start in the middle of the sequential list
    const QVector<Gear> seq = Gearing::buildSequence(s);
    if (!seq.isEmpty()) {
        const Gear &g = seq.at(seq.size() / 2);
        s.frontIndex = g.front;
        s.rearIndex = g.rear;
    }
    return s;
}

bool GearSetup::validate(const QVector<int> &chainrings, const QVector<int> &cogs, int circumferenceMm, QString *error)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    const QVector<int> rings = sortedUnique(chainrings);
    const QVector<int> sprockets = sortedUnique(cogs);
    if (rings.isEmpty() || rings.size() > kMaxChainrings)
        return fail(QStringLiteral("Enter 1 to %1 chainrings").arg(kMaxChainrings));
    if (sprockets.isEmpty() || sprockets.size() > kMaxCogs)
        return fail(QStringLiteral("Enter 1 to %1 cassette cogs").arg(kMaxCogs));
    for (int t : rings) {
        if (t < kMinTeeth || t > kMaxChainringTeeth)
            return fail(QStringLiteral("Chainrings must have %1–%2 teeth").arg(kMinTeeth).arg(kMaxChainringTeeth));
    }
    for (int t : sprockets) {
        if (t < kMinTeeth || t > kMaxCogTeeth)
            return fail(QStringLiteral("Cogs must have %1–%2 teeth").arg(kMinTeeth).arg(kMaxCogTeeth));
    }
    if (circumferenceMm < kMinCircumferenceMm || circumferenceMm > kMaxCircumferenceMm)
        return fail(QStringLiteral("Wheel circumference must be %1–%2 mm").arg(kMinCircumferenceMm).arg(kMaxCircumferenceMm));
    if (error)
        error->clear();
    return true;
}

bool GearSetup::isValid(QString *error) const
{
    return validate(chainrings, cogs, circumferenceMm, error);
}

GearSetup GearSetup::normalized() const
{
    GearSetup s = *this;
    s.chainrings = sortedUnique(chainrings);
    s.cogs = sortedUnique(cogs);
    if (s.chainrings.isEmpty())
        s.chainrings = GearSetup().chainrings;
    if (s.cogs.isEmpty())
        s.cogs = GearSetup().cogs;
    s.circumferenceMm = std::clamp(circumferenceMm, kMinCircumferenceMm, kMaxCircumferenceMm);
    s.frontIndex = std::clamp(frontIndex, 0, int(s.chainrings.size()) - 1);
    s.rearIndex = std::clamp(rearIndex, 0, int(s.cogs.size()) - 1);
    s.bikeWeightKg = std::isfinite(bikeWeightKg) ? std::clamp(bikeWeightKg, 0.0, 60.0) : 9.0;
    s.cda = std::isfinite(cda) ? std::clamp(cda, 0.1, 1.0) : 0.32;
    s.crr = std::isfinite(crr) ? std::clamp(crr, 0.001, 0.05) : 0.004;
    if (!s.chainrings.contains(s.referenceFrontTeeth))
        s.referenceFrontTeeth = s.chainrings.constLast();
    if (!s.cogs.contains(s.referenceRearTeeth)) {
        int best = s.cogs.constFirst();
        for (int c : std::as_const(s.cogs)) {
            if (std::abs(c - kDefaultReferenceCog) < std::abs(best - kDefaultReferenceCog))
                best = c;
        }
        s.referenceRearTeeth = best;
    }
    return s;
}

QString GearSetup::wheelName(int mm)
{
    for (const QVariant &v : wheelPresets()) {
        const QVariantMap w = v.toMap();
        if (w.value(QStringLiteral("mm")).toInt() == mm)
            return w.value(QStringLiteral("name")).toString();
    }
    return QStringLiteral("%1 mm").arg(mm);
}

QString GearSetup::summary() const
{
    const GearSetup s = normalized();
    QStringList rings;
    for (int i = int(s.chainrings.size()) - 1; i >= 0; --i)
        rings << QString::number(s.chainrings.at(i));
    const QString ringText = s.chainrings.size() == 1 ? QStringLiteral("1× %1").arg(s.chainrings.constFirst())
                                                      : rings.join(QLatin1Char('/'));
    const QString cassette = QStringLiteral("%1–%2 %3-speed")
                                 .arg(s.cogs.constFirst())
                                 .arg(s.cogs.constLast())
                                 .arg(s.cogs.size());
    const int count = s.sequential ? int(Gearing::buildSequence(s).size()) : int(s.chainrings.size() * s.cogs.size());
    return QStringLiteral("%1 · %2 · %3 · %4 gears").arg(ringText, cassette, wheelName(s.circumferenceMm)).arg(count);
}

QJsonObject GearSetup::toJson() const
{
    return QJsonObject { { QStringLiteral("chainrings"), toArray(chainrings) },
                         { QStringLiteral("cogs"), toArray(cogs) },
                         { QStringLiteral("circumferenceMm"), circumferenceMm },
                         { QStringLiteral("sequential"), sequential },
                         { QStringLiteral("front"), frontIndex },
                         { QStringLiteral("rear"), rearIndex },
                         { QStringLiteral("bikeWeightKg"), bikeWeightKg },
                         { QStringLiteral("cda"), cda },
                         { QStringLiteral("crr"), crr },
                         { QStringLiteral("refFront"), referenceFrontTeeth },
                         { QStringLiteral("refRear"), referenceRearTeeth } };
}

GearSetup GearSetup::fromJson(const QJsonObject &o)
{
    GearSetup s = defaults();
    const QVector<int> rings = fromArray(o.value(QStringLiteral("chainrings")).toArray());
    const QVector<int> cogs = fromArray(o.value(QStringLiteral("cogs")).toArray());
    const int circ = o.value(QStringLiteral("circumferenceMm")).toInt(kDefaultCircumferenceMm);
    if (validate(rings, cogs, circ)) {
        s.chainrings = rings;
        s.cogs = cogs;
        s.circumferenceMm = circ;
        s.frontIndex = o.value(QStringLiteral("front")).toInt(-1);
        s.rearIndex = o.value(QStringLiteral("rear")).toInt(-1);
        const QVector<int> sortedRings = sortedUnique(rings);
        const QVector<int> sortedCogs = sortedUnique(cogs);
        if (s.frontIndex < 0 || s.frontIndex >= sortedRings.size() || s.rearIndex < 0
            || s.rearIndex >= sortedCogs.size()) {
            s.frontIndex = int(sortedRings.size()) - 1;
            s.rearIndex = int(sortedCogs.size()) / 2;
        }
    }
    s.sequential = o.value(QStringLiteral("sequential")).toBool(true);
    s.bikeWeightKg = o.value(QStringLiteral("bikeWeightKg")).toDouble(9.0);
    s.cda = o.value(QStringLiteral("cda")).toDouble(0.32);
    s.crr = o.value(QStringLiteral("crr")).toDouble(0.004);
    s.referenceFrontTeeth = o.value(QStringLiteral("refFront")).toInt(s.referenceFrontTeeth);
    s.referenceRearTeeth = o.value(QStringLiteral("refRear")).toInt(s.referenceRearTeeth);
    return s.normalized();
}

QString GearSetup::toJsonText() const
{
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

GearSetup GearSetup::fromJsonText(const QString &text)
{
    if (text.trimmed().isEmpty())
        return defaults();
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject())
        return defaults();
    return fromJson(doc.object());
}

bool GearSetup::operator==(const GearSetup &o) const
{
    return chainrings == o.chainrings && cogs == o.cogs && circumferenceMm == o.circumferenceMm
           && sequential == o.sequential && frontIndex == o.frontIndex && rearIndex == o.rearIndex
           && qFuzzyCompare(bikeWeightKg + 1, o.bikeWeightKg + 1) && qFuzzyCompare(cda + 1, o.cda + 1)
           && qFuzzyCompare(crr + 1, o.crr + 1) && referenceFrontTeeth == o.referenceFrontTeeth
           && referenceRearTeeth == o.referenceRearTeeth;
}

QVariantList GearSetup::chainringPresets()
{
    return {
        preset(QStringLiteral("50/34 compact"), { 50, 34 }),
        preset(QStringLiteral("52/36 semi-compact"), { 52, 36 }),
        preset(QStringLiteral("53/39 standard"), { 53, 39 }),
        preset(QStringLiteral("48/35 (SRAM wide)"), { 48, 35 }),
        preset(QStringLiteral("46/33 (SRAM XPLR)"), { 46, 33 }),
        preset(QStringLiteral("48/31 (GRX)"), { 48, 31 }),
        preset(QStringLiteral("46/30 (GRX)"), { 46, 30 }),
        preset(QStringLiteral("50/39/30 triple"), { 50, 39, 30 }),
        preset(QStringLiteral("1× 38"), { 38 }),
        preset(QStringLiteral("1× 40"), { 40 }),
        preset(QStringLiteral("1× 42"), { 42 }),
        preset(QStringLiteral("1× 44"), { 44 }),
        preset(QStringLiteral("1× 32 (MTB)"), { 32 }),
    };
}

QVariantList GearSetup::cassettePresets()
{
    return {
        preset(QStringLiteral("11-28 11-speed"), { 11, 12, 13, 14, 15, 17, 19, 21, 23, 25, 28 }),
        preset(QStringLiteral("11-30 11-speed"), { 11, 12, 13, 14, 15, 17, 19, 21, 24, 27, 30 }),
        preset(QStringLiteral("11-32 11-speed"), { 11, 12, 13, 14, 16, 18, 20, 22, 25, 28, 32 }),
        preset(QStringLiteral("11-34 11-speed"), { 11, 13, 15, 17, 19, 21, 23, 25, 27, 30, 34 }),
        preset(QStringLiteral("11-28 12-speed"), { 11, 12, 13, 14, 15, 16, 17, 19, 21, 23, 25, 28 }),
        preset(QStringLiteral("11-30 12-speed"), { 11, 12, 13, 14, 15, 16, 17, 19, 21, 24, 27, 30 }),
        preset(QStringLiteral("11-34 12-speed"), { 11, 12, 13, 14, 15, 17, 19, 21, 24, 27, 30, 34 }),
        preset(QStringLiteral("10-33 12-speed (SRAM)"), { 10, 11, 12, 13, 14, 15, 17, 19, 21, 24, 28, 33 }),
        preset(QStringLiteral("10-36 12-speed (SRAM)"), { 10, 11, 12, 13, 15, 17, 19, 21, 24, 28, 32, 36 }),
        preset(QStringLiteral("10-42 11-speed 1×"), { 10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42 }),
        preset(QStringLiteral("10-44 12-speed 1× (XPLR)"), { 10, 11, 13, 15, 17, 19, 21, 24, 28, 32, 38, 44 }),
        preset(QStringLiteral("10-50 12-speed 1× (Eagle)"), { 10, 12, 14, 16, 18, 21, 24, 28, 32, 36, 42, 50 }),
    };
}

QVariantList GearSetup::wheelPresets()
{
    auto w = [](const QString &name, int mm) {
        return QVariantMap { { QStringLiteral("name"), name }, { QStringLiteral("mm"), mm } };
    };
    return {
        w(QStringLiteral("700×23c"), 2096),
        w(QStringLiteral("700×25c"), 2105),
        w(QStringLiteral("700×28c"), 2136),
        w(QStringLiteral("700×32c"), 2155),
        w(QStringLiteral("700×35c"), 2168),
        w(QStringLiteral("700×40c"), 2200),
        w(QStringLiteral("650b×47"), 2124),
        w(QStringLiteral("26×2.1"), 2068),
        w(QStringLiteral("27.5×2.25"), 2182),
        w(QStringLiteral("29×2.2"), 2298),
    };
}

QVector<int> GearSetup::parseTeethList(const QString &text)
{
    static const QRegularExpression sep(QStringLiteral("[\\s,;/\\-]+"));
    QVector<int> out;
    const QStringList parts = text.split(sep, Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v <= 0)
            return {};
        out.append(v);
    }
    return out;
}

// --- Gearing -------------------------------------------------------------------

Gearing::Gearing()
    : Gearing(GearSetup::defaults())
{
}

Gearing::Gearing(const GearSetup &setup)
{
    m_setup = setup.normalized();
    rebuild();
    snapToSequence();
}

void Gearing::setSetup(const GearSetup &setup)
{
    const double previous = ratio();
    const GearSetup n = setup.normalized();
    const bool indicesGiven = setup.frontIndex == n.frontIndex && setup.rearIndex == n.rearIndex;
    m_setup = n;
    rebuild();
    if (!indicesGiven)
        selectNearest(previous);
    snapToSequence();
}

void Gearing::setSequential(bool on)
{
    if (m_setup.sequential == on)
        return;
    m_setup.sequential = on;
    snapToSequence();
}

void Gearing::snapToSequence()
{
    // sequential mode always sits on a gear that is part of the sequence
    if (!m_setup.sequential)
        return;
    const int idx = sequenceIndex();
    if (idx >= 0) {
        m_setup.frontIndex = m_sequence.at(idx).front;
        m_setup.rearIndex = m_sequence.at(idx).rear;
    }
}

void Gearing::rebuild()
{
    m_sequence = buildSequence(m_setup);
}

Gear Gearing::gearAt(int front, int rear) const
{
    Gear g;
    g.front = front;
    g.rear = rear;
    g.frontTeeth = m_setup.chainrings.value(front);
    g.rearTeeth = m_setup.cogs.value(rear);
    g.ratio = g.rearTeeth > 0 ? double(g.frontTeeth) / g.rearTeeth : 0.0;
    return g;
}

int Gearing::frontTeeth() const { return m_setup.chainrings.value(m_setup.frontIndex); }
int Gearing::rearTeeth() const { return m_setup.cogs.value(m_setup.rearIndex); }

double Gearing::ratio() const
{
    const int r = rearTeeth();
    return r > 0 ? double(frontTeeth()) / r : 0.0;
}

QString Gearing::labelFor(int frontTeeth, int rearTeeth)
{
    return QStringLiteral("%1×%2").arg(frontTeeth).arg(rearTeeth);
}

QString Gearing::label() const { return labelFor(frontTeeth(), rearTeeth()); }
Gear Gearing::current() const { return gearAt(m_setup.frontIndex, m_setup.rearIndex); }

QVector<Gear> Gearing::allGears() const
{
    QVector<Gear> all;
    for (int f = 0; f < m_setup.chainrings.size(); ++f)
        for (int r = 0; r < m_setup.cogs.size(); ++r)
            all.append(gearAt(f, r));
    std::stable_sort(all.begin(), all.end(), [](const Gear &a, const Gear &b) { return a.ratio < b.ratio; });
    return all;
}

QVector<Gear> Gearing::buildSequence(const GearSetup &setupIn, double tolerance)
{
    // Shift pattern of a real bike, easiest first: every chainring but the
    // largest is used from the biggest cog down to the middle of the
    // cassette, then the next ring continues with its first cog that is
    // clearly harder (> tolerance) than the last gear; the largest ring runs
    // down to the smallest cog. Gears closer than `tolerance` to the previous
    // one are skipped, so the list is strictly increasing with distinct
    // steps and no ratio ever repeats (no zig-zagging between rings).
    const GearSetup s = setupIn.normalized();
    QVector<Gear> out;
    const int cogCount = int(s.cogs.size());
    const int middle = cogCount / 2;   // index into the ascending cog list
    for (int f = 0; f < s.chainrings.size(); ++f) {
        const bool lastRing = f == s.chainrings.size() - 1;
        for (int r = cogCount - 1; r >= 0; --r) {
            if (!lastRing && r < middle)
                break;
            Gear g;
            g.front = f;
            g.rear = r;
            g.frontTeeth = s.chainrings.at(f);
            g.rearTeeth = s.cogs.at(r);
            g.ratio = double(g.frontTeeth) / g.rearTeeth;
            if (!out.isEmpty() && g.ratio <= out.constLast().ratio * (1.0 + tolerance))
                continue;
            out.append(g);
        }
    }
    return out;
}

int Gearing::sequenceIndex() const
{
    if (m_sequence.isEmpty())
        return -1;
    const double r = ratio();
    int best = 0;
    double bestDiff = 1e9;
    for (int i = 0; i < m_sequence.size(); ++i) {
        const Gear &g = m_sequence.at(i);
        if (g.front == m_setup.frontIndex && g.rear == m_setup.rearIndex)
            return i;
        const double d = std::abs(g.ratio - r);
        if (d < bestDiff) {
            bestDiff = d;
            best = i;
        }
    }
    return best;
}

void Gearing::selectNearest(double target)
{
    if (!(target > 0))
        return;
    const QVector<Gear> candidates = m_setup.sequential ? m_sequence : allGears();
    double bestDiff = 1e9;
    for (const Gear &g : candidates) {
        const double d = std::abs(g.ratio - target);
        if (d < bestDiff - 1e-9) {
            bestDiff = d;
            m_setup.frontIndex = g.front;
            m_setup.rearIndex = g.rear;
        }
    }
}

int Gearing::gearIndex() const
{
    if (m_setup.sequential)
        return sequenceIndex();
    const QVector<Gear> all = allGears();
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).front == m_setup.frontIndex && all.at(i).rear == m_setup.rearIndex)
            return i;
    }
    return 0;
}

int Gearing::gearCount() const
{
    return m_setup.sequential ? int(m_sequence.size()) : int(m_setup.chainrings.size() * m_setup.cogs.size());
}

bool Gearing::canShiftUp() const
{
    if (m_setup.sequential)
        return sequenceIndex() < m_sequence.size() - 1;
    return m_setup.rearIndex > 0;
}

bool Gearing::canShiftDown() const
{
    if (m_setup.sequential)
        return sequenceIndex() > 0;
    return m_setup.rearIndex < m_setup.cogs.size() - 1;
}

bool Gearing::canShiftFrontUp() const
{
    if (m_setup.sequential)
        return canShiftUp();
    return m_setup.frontIndex < m_setup.chainrings.size() - 1;
}

bool Gearing::canShiftFrontDown() const
{
    if (m_setup.sequential)
        return canShiftDown();
    return m_setup.frontIndex > 0;
}

bool Gearing::setGearIndex(int index)
{
    if (m_setup.sequential) {
        if (index < 0 || index >= m_sequence.size())
            return false;
        const Gear &g = m_sequence.at(index);
        m_setup.frontIndex = g.front;
        m_setup.rearIndex = g.rear;
        return true;
    }
    const QVector<Gear> all = allGears();
    if (index < 0 || index >= all.size())
        return false;
    m_setup.frontIndex = all.at(index).front;
    m_setup.rearIndex = all.at(index).rear;
    return true;
}

bool Gearing::shiftUp()
{
    if (!canShiftUp())
        return false;
    if (m_setup.sequential)
        return setGearIndex(sequenceIndex() + 1);
    --m_setup.rearIndex;
    return true;
}

bool Gearing::shiftDown()
{
    if (!canShiftDown())
        return false;
    if (m_setup.sequential)
        return setGearIndex(sequenceIndex() - 1);
    ++m_setup.rearIndex;
    return true;
}

bool Gearing::shiftFrontUp()
{
    if (!canShiftFrontUp())
        return false;
    if (m_setup.sequential)
        return setGearIndex(std::min(int(m_sequence.size()) - 1, sequenceIndex() + kSequentialJump));
    ++m_setup.frontIndex;
    return true;
}

bool Gearing::shiftFrontDown()
{
    if (!canShiftFrontDown())
        return false;
    if (m_setup.sequential)
        return setGearIndex(std::max(0, sequenceIndex() - kSequentialJump));
    --m_setup.frontIndex;
    return true;
}

bool Gearing::setGear(int front, int rear)
{
    if (front < 0 || front >= m_setup.chainrings.size() || rear < 0 || rear >= m_setup.cogs.size())
        return false;
    m_setup.frontIndex = front;
    m_setup.rearIndex = rear;
    return true;
}

double Gearing::speedFor(double cadenceRpm, double ratio, int circumferenceMm)
{
    if (!(cadenceRpm > 0) || !(ratio > 0) || circumferenceMm <= 0)
        return 0.0;
    return cadenceRpm / 60.0 * ratio * (circumferenceMm / 1000.0);
}

double Gearing::virtualSpeed(double cadenceRpm) const
{
    return speedFor(cadenceRpm, ratio(), m_setup.circumferenceMm);
}

// --- road model ----------------------------------------------------------------

namespace road {

double resistance(const Model &m, double v)
{
    const double theta = std::atan(m.gradePercent / 100.0);
    const double weight = m.massKg * kGravity;
    const double rolling = v > 0 ? m.crr * weight * std::cos(theta) : 0.0;
    const double gravity = weight * std::sin(theta);
    const double aero = 0.5 * m.airDensity * m.cda * v * v;
    return rolling + gravity + aero;
}

double powerForSpeed(const Model &m, double v)
{
    if (!(v > 0))
        return 0.0;
    const double eff = m.efficiency > 0 ? m.efficiency : 1.0;
    return resistance(m, v) * v / eff;
}

double speedForPower(const Model &m, double watts)
{
    if (!(watts > 0)) {
        // coasting equilibrium (only > 0 downhill)
        if (resistance(m, 0.01) >= 0)
            return 0.0;
        double lo = 0.0, hi = kMaxSpeedMps;
        for (int i = 0; i < 60; ++i) {
            const double mid = (lo + hi) / 2;
            if (resistance(m, mid) < 0)
                lo = mid;
            else
                hi = mid;
        }
        return lo;
    }
    // powerForSpeed is monotonic above the coasting speed: bisection.
    double lo = 0.0, hi = kMaxSpeedMps;
    if (powerForSpeed(m, hi) < watts)
        return hi;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2;
        if (powerForSpeed(m, mid) < watts)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) / 2;
}

double coastDeceleration(const Model &m, double v)
{
    if (!(m.massKg > 0))
        return 0.0;
    return resistance(m, v) / m.massKg;
}

double integrateSpeed(const Model &m, double v, double watts, double dt, double extraMassKg, double minDriveSpeedMps,
                      double stepSec)
{
    if (!(dt > 0))
        return v;
    const double mass = std::max(1.0, m.massKg + extraMassKg);
    const double p = std::max(0.0, watts) * (m.efficiency > 0 ? m.efficiency : 1.0);
    const int steps = std::max(1, int(std::ceil(dt / std::max(1e-3, stepSec) - 1e-9)));
    const double h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        const double drive = p / std::max(v, minDriveSpeedMps);
        const double a = (drive - resistance(m, v)) / mass;
        v = std::clamp(v + a * h, 0.0, kMaxSpeedMps);
    }
    return v;
}

double gearSimulationGrade(const Model &roadModel, double vt, double gearFactor, const TrainerModel &t, double minGrade,
                           double maxGrade, double minSpeedMps)
{
    if (!(vt >= minSpeedMps) || !(gearFactor > 0) || !(t.massKg > 0))
        return std::clamp(roadModel.gradePercent, minGrade, maxGrade);
    const double vv = vt * gearFactor;
    const double power = powerForSpeed(roadModel, vv);
    const double force = power / vt;
    const double weight = t.massKg * kGravity;
    const double k = (force - 0.5 * t.cwKgPerM * vt * vt) / weight;
    const double crr = std::max(0.0, t.crr);
    const double theta = std::asin(std::clamp(k / std::sqrt(1.0 + crr * crr), -1.0, 1.0)) - std::atan(crr);
    const double grade = std::tan(std::clamp(theta, -1.4, 1.4)) * 100.0;
    return std::clamp(grade, minGrade, maxGrade);
}

} // namespace road

} // namespace watt
