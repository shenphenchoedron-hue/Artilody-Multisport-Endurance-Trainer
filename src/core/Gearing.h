#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace watt {

// Bike gearing for virtual shifting (free ride page).
//
// Conventions
//  * chainrings and cogs are stored sorted ascending by teeth
//    (e.g. chainrings [34, 50], cogs [11, 12, ..., 34]).
//  * frontIndex indexes chainrings (0 = smallest ring = easiest),
//    rearIndex indexes cogs (0 = smallest cog = hardest).
//  * "up" always means harder (higher ratio), "down" easier.
//
// Two shifting modes:
//  * derailleur: front and rear shift separately like a real bike
//    (shiftUp/Down move the rear, shiftFrontUp/Down the front).
//  * sequential: one list of gears, easiest first, like Zwift's virtual
//    gears: the smaller rings down to mid-cassette, then the next ring, gears
//    within kDuplicateTolerance of the previous one skipped (see
//    buildSequence()); shiftUp/Down step through that list,
//    shiftFrontUp/Down jump kSequentialJump gears.
struct GearSetup
{
    static constexpr int kMinTeeth = 9;
    static constexpr int kMaxChainringTeeth = 70;
    static constexpr int kMaxCogTeeth = 60;
    static constexpr int kMaxChainrings = 3;
    static constexpr int kMaxCogs = 14;
    static constexpr int kMinCircumferenceMm = 1000;
    static constexpr int kMaxCircumferenceMm = 3000;
    static constexpr int kDefaultCircumferenceMm = 2105;   // 700x25c

    QVector<int> chainrings { 34, 50 };
    QVector<int> cogs { 11, 12, 13, 14, 15, 17, 19, 21, 24, 27, 30, 34 };   // 11-34 12-speed
    int circumferenceMm = kDefaultCircumferenceMm;
    bool sequential = true;
    // current gear (indices into chainrings / cogs, see conventions above)
    int frontIndex = 1;
    int rearIndex = 6;
    // road model of the rider's bike (rider weight and height come from the
    // profile; mass = rider + bike)
    double bikeWeightKg = 9.0;
    // Air drag: cdaAuto (default) estimates the CdA from the rider's size
    // (road::autoCda()); otherwise `cda` is the rider's own value.
    bool cdaAuto = true;
    double cda = 0.32;      // m^2, manual value (hoods position)
    double crr = 0.004;     // rolling resistance, good road tyres
    // Reference gear: the real gear the bike sits in on the trainer while
    // shifting virtually ("leave your bike in this gear"). Teeth, not indices;
    // normalized() snaps them to the setup (largest ring / cog nearest 17).
    int referenceFrontTeeth = 50;
    int referenceRearTeeth = 17;
    static constexpr int kDefaultReferenceCog = 17;
    // The reference gear is not a combination of the setup (e.g. a single
    // 14T Zwift Cog on the trainer, or a detected gear): then normalized()
    // keeps it as long as it is within kMin/kMaxReference* teeth.
    bool referenceCustom = false;
    static constexpr int kMinReferenceRing = 20, kMaxReferenceRing = 60;
    static constexpr int kMinReferenceCog = 9, kMaxReferenceCog = 40;
    static constexpr int kZwiftCogTeeth = 14;
    // Reference gear presets for a trainer with a single cog (Zwift Cog,
    // 14T): [{label: "50×14 (Zwift Cog)", front: 50, rear: 14, ratio}]
    static QVariantList cogPresets();

    static GearSetup defaults();
    // Checks the lists/values; on error returns false and sets *error.
    // Order and duplicates are fixed by normalized() and are no error.
    static bool validate(const QVector<int> &chainrings, const QVector<int> &cogs, int circumferenceMm,
                         QString *error = nullptr);
    // Sorted ascending, duplicates removed, indices and physics clamped.
    GearSetup normalized() const;
    bool isValid(QString *error = nullptr) const;

    QJsonObject toJson() const;
    // Missing/invalid values fall back to the defaults.
    static GearSetup fromJson(const QJsonObject &o);
    QString toJsonText() const;
    static GearSetup fromJsonText(const QString &text);   // empty/invalid -> defaults()

    bool operator==(const GearSetup &o) const;
    bool operator!=(const GearSetup &o) const { return !(*this == o); }

    double referenceRatio() const
    {
        return referenceRearTeeth > 0 ? double(referenceFrontTeeth) / referenceRearTeeth : 0.0;
    }
    // "50/34 · 11–34 12-speed · 700×25c · 16 gears" (gear count of the
    // current shifting mode)
    QString summary() const;
    static QString wheelName(int circumferenceMm);   // "700×25c" or "2105 mm"

    // Presets for the setup UI: [{name, values: [int]}] / [{name, mm}]
    static QVariantList chainringPresets();
    static QVariantList cassettePresets();
    static QVariantList wheelPresets();
    // "11,12,13" / "11-12-13" / "11 12 13" -> [11,12,13]; empty on any bad token
    static QVector<int> parseTeethList(const QString &text);
};

struct Gear
{
    int front = 0;   // chainring index
    int rear = 0;    // cog index
    int frontTeeth = 0;
    int rearTeeth = 0;
    double ratio = 0;
};

class Gearing
{
public:
    static constexpr double kDuplicateTolerance = 0.03;   // ratios within 3 % count as the same gear
    static constexpr int kSequentialJump = 3;

    Gearing();
    explicit Gearing(const GearSetup &setup);

    // Replaces the setup (normalized). The requested gear indices are used
    // when valid, else the gear with the nearest ratio to the previous one.
    void setSetup(const GearSetup &setup);
    const GearSetup &setup() const { return m_setup; }
    void setSequential(bool on);

    int frontIndex() const { return m_setup.frontIndex; }
    int rearIndex() const { return m_setup.rearIndex; }
    int frontTeeth() const;
    int rearTeeth() const;
    double ratio() const;
    QString label() const;        // "50×17"
    Gear current() const;

    // Position of the current gear: sequential -> index in sequence(),
    // derailleur -> rank of the current combination in allGears().
    // 0 = easiest.
    int gearIndex() const;
    int gearCount() const;

    bool canShiftUp() const;
    bool canShiftDown() const;
    bool canShiftFrontUp() const;
    bool canShiftFrontDown() const;
    bool shiftUp();          // harder
    bool shiftDown();        // easier
    bool shiftFrontUp();     // bigger ring (sequential: jump kSequentialJump gears harder)
    bool shiftFrontDown();
    bool setGear(int front, int rear);
    bool setGearIndex(int index);   // in the current mode's ordering (see gearIndex())

    // Sequential list (easiest first) for the current setup.
    const QVector<Gear> &sequence() const { return m_sequence; }
    // All combinations sorted by ratio (easiest first), no deduplication.
    QVector<Gear> allGears() const;

    static QVector<Gear> buildSequence(const GearSetup &setup, double tolerance = kDuplicateTolerance);
    static QString labelFor(int frontTeeth, int rearTeeth);

    // Virtual wheel speed (m/s) at the given cadence in the current gear:
    // cadence / 60 * ratio * circumference.
    double virtualSpeed(double cadenceRpm) const;
    static double speedFor(double cadenceRpm, double ratio, int circumferenceMm);

private:
    void rebuild();
    void snapToSequence();
    int sequenceIndex() const;          // exact match or nearest ratio
    void selectNearest(double ratio);
    Gear gearAt(int front, int rear) const;

    GearSetup m_setup;
    QVector<Gear> m_sequence;
};

// The road model of the app - ONE set of constants for virtual gears, the
// riding sessions (speed from power), the gear-adjusted grade (path a), the
// FTMS simulation parameters (trainerModelFor()) and the virtual trainer:
//
//   P = (Crr·m·g·cosθ + m·g·sinθ + ½·ρ·CdA·v²) · v / η
//
// m = rider (profile weight) + bike (gear setup, default 9 kg), θ =
// atan(grade), ρ = 1.225 kg/m³, CdA = autoCda(rider) or the manual value of
// the gear setup, Crr = 0.004 (gear setup), η = 0.97. No wind and no
// acceleration term here (integrateSpeed() adds inertia).
// Reference: 40 km/h on the flat, 86 kg, CdA 0.32 -> 306.3 W at the wheel,
// 315.8 W at the cranks (powerForSpeed()).
//
// Power convention (one for every path):
//  * the power the trainer measures / is set to (ERG targets, gearWatts(),
//    the displayed watts) is the rider's CRANK power P;
//  * the road - and a trainer simulating a grade - consumes the WHEEL power
//    P·η = F(v)·v. speedForPower()/integrateSpeed() use P·η; the grade for a
//    trainer (gradeForTrainerPower(), gearSimulationGrade(),
//    RideResistance::gearGrade()) is solved for the wheel power
//    (wheelPowerForSpeed() = powerForSpeed()·η). So in the reference gear the
//    gear-adjusted grade equals the road grade, and a trainer at grade g
//    whose rider pushes P rides at speedForPower(g, P) in every path.
namespace road {

constexpr double kGravity = 9.80665;
constexpr double kAirDensity = 1.225;           // kg/m^3, sea level 15 °C
constexpr double kDrivetrainEfficiency = 0.97;
constexpr double kMaxSpeedMps = 30.0;           // 108 km/h, bisection bound
constexpr double kDefaultCrr = 0.004;
constexpr double kDefaultBikeKg = 9.0;
// Reference rider of the CdA estimate: 75 kg, 1.75 m, hoods -> 0.32 m².
constexpr double kDefaultRiderKg = 75.0;        // also used when the profile has no weight
constexpr double kReferenceRiderKg = 75.0;
constexpr double kReferenceHeightCm = 175.0;
constexpr double kReferenceCda = 0.32;
constexpr double kMinCda = 0.15;
constexpr double kMaxCda = 0.60;

struct Model
{
    double massKg = kDefaultRiderKg + kDefaultBikeKg;   // rider + bike
    double cda = kReferenceCda;
    double crr = kDefaultCrr;
    double gradePercent = 0.0;
    double airDensity = kAirDensity;
    double efficiency = kDrivetrainEfficiency;
};

// Body surface area (m²), Du Bois & Du Bois (1916):
//   BSA = 0.007184 · W^0.425 · H^0.725   (W in kg, H in cm)
double bodySurfaceArea(double weightKg, double heightCm);
// CdA estimated from the rider's size ("Auto (from weight)"). The frontal
// area of a cyclist scales with the body surface area (Heil 2001, "Body
// mass scaling of projected frontal area in competitive cyclists", Eur J
// Appl Physiol 85: A_P ∝ H^0.725·M^0.425 - the Du Bois exponents), Cd stays
// the same, so relative to the reference rider (75 kg, 175 cm, 0.32 m² on
// the hoods):
//   with height:    CdA = 0.32 · BSA(m, h) / BSA(75, 175)
//   without height: CdA = 0.32 · (m / 75)^(2/3)
//                   (isometric scaling, h ∝ m^(1/3): 0.425 + 0.725/3 ≈ 2/3)
// e.g. 60 kg -> 0.276, 75 kg -> 0.320, 90 kg -> 0.361 m². heightCm <= 0:
// unknown. Clamped to kMinCda..kMaxCda; riderKg <= 0 -> kDefaultRiderKg.
double autoCda(double riderKg, double heightCm = 0);

// Resisting force (N) at speed v (m/s); negative downhill when gravity wins.
double resistance(const Model &m, double speedMps);
// Rider (crank) power (W) needed to hold speed v: F(v)·v/η; may be negative
// steeply downhill.
double powerForSpeed(const Model &m, double speedMps);
// Power at the wheel for speed v: F(v)·v = powerForSpeed()·η.
double wheelPowerForSpeed(const Model &m, double speedMps);
// Inverse of powerForSpeed() for P >= 0 (bisection in 0..kMaxSpeedMps).
double speedForPower(const Model &m, double watts);
// Deceleration (m/s^2, > 0 slows down, < 0 speeds up downhill) while coasting.
double coastDeceleration(const Model &m, double speedMps);
// Speed after riding dt seconds with `watts` at the cranks, with inertia:
//   (m + extraMass)·dv/dt = P·η / max(v, minDriveSpeed) − F(v)
// (explicit Euler in steps of at most stepSec), never below 0.
double integrateSpeed(const Model &m, double speedMps, double watts, double dt, double extraMassKg = 1.5,
                      double minDriveSpeedMps = 1.0, double stepSec = 0.05);

// Resistance model of a trainer in FTMS gradient simulation (op 0x11):
//   F = Crr·m·g·cosθ + m·g·sinθ + ½·Cw·v²   (Cw in kg/m, no wind)
// FTMS 0x11 carries wind, grade, Crr and Cw - no weight: a real trainer uses
// its own rider weight setting (its app / FE-C user data), the app assumes
// it equals rider + bike of the profile (see trainerModelFor()).
struct TrainerModel
{
    double massKg = kDefaultRiderKg + kDefaultBikeKg;
    double crr = kDefaultCrr;
    double cwKgPerM = 0.39;   // ρ·CdA of the reference rider (1.225 · 0.32)
};

// The FTMS simulation parameters for a rider: Cw = ρ·CdA (so ½·Cw·v² is the
// road's ½·ρ·CdA·v² - not Zwift's fixed 0.51 kg/m), Crr of the road, in the
// resolution FTMS sends them (Cw 0.01 kg/m, Crr 0.0001), so the grade solved
// with this model (path a) uses exactly the parameters the trainer gets.
// trainerMassKg > 0: the trainer's own rider weight setting when known
// (compensates a trainer that assumes another weight); else the road mass.
TrainerModel trainerModelFor(const Model &road, double trainerMassKg = 0);

// Grade (%) for FTMS 0x11 that makes a trainer braking at speed vt take
// `power` (W, wheel power - see the power convention above):
//     F = P / v_t = Crr·m·g·cosθ + m·g·sinθ + ½·Cw·v_t²
// solved exactly for θ (sinθ + Crr·cosθ = k, k = (F − ½·Cw·v_t²)/(m·g)):
//     θ = asin(k / √(1 + Crr²)) − atan(Crr)
// Returns tan θ in % clamped to minGrade..maxGrade (vt must be > 0).
// Monotonic: more power at the same speed = a steeper grade.
double gradeForTrainerPower(double power, double trainerSpeedMps, const TrainerModel &trainer, double minGrade = -10.0,
                            double maxGrade = 20.0);

// Virtual gears on a trainer that simulates gradients ("path a"): the bike
// stays in the reference gear, so the trainer's flywheel turns at v_t while
// the virtual bike rides at
//     v_v = v_t · R_virtual / R_reference          (gearFactor = R_v / R_ref)
// The trainer must take the road's wheel power for v_v on the road grade:
//     P = max(0, wheelPowerForSpeed(road, v_v))   (= crank power · η)
// (never negative: steeply downhill the road would push the pedals - the
// rider freewheels instead; this also keeps the result monotonic in the
// gear, a harder gear never gives a lower grade), delivered at v_t with
// gradeForTrainerPower(). Below minSpeedMps trainer speed (or a
// non-positive gear factor) the plain road grade (clamped) is returned.
// The sessions use RideResistance::gearGrade(), which takes v_t from the
// cadence (see there).
double gearSimulationGrade(const Model &road, double trainerSpeedMps, double gearFactor, const TrainerModel &trainer,
                           double minGrade = -10.0, double maxGrade = 20.0, double minSpeedMps = 1.0);

} // namespace road

} // namespace watt
