#pragma once

#include "Gearing.h"
#include "Profile.h"
#include "HubGearCompensation.h"
#include "RideResistance.h"

#include <QObject>
#include <QPair>
#include <QVector>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace watt {

class Database;
class FreeRideSession;
class ProfileManager;
class RlvSession;
class SensorHub;

// Virtual shifting with a Zwift Click, shared by the Free Ride and the Video
// Ride. Exposed to QML as "Gears" (Artilody.Backend).
//
// Tied to the Zwift Click, not to the trainer (no manual switch):
//   * Zwift Click connected   -> virtual gears active (enabled()). The rider
//     shifts Zwift's fixed 24 virtual gears (zwiftgears::kRatios, start gear
//     12 = 2.40; the chainrings / cogs of the old gear setup are ignored but
//     kept in the database). How the gear reaches the trainer
//     (shiftingMode()):
//       "zwift" - the CONNECTED trainer exposes the Zwift trainer service and
//                 its handshake succeeded: Zwift hub protocol, the trainer gets
//                 incline + gear ratio + weights and does the physics
//                 (RideResistance::Path::GearHub);
//       "ftms"  - otherwise (no Zwift service, handshake running / failed):
//                 FTMS gradient virtual gears (gear-adjusted grade, path a);
//       "erg"   - only a trainer WITHOUT gradient simulation (FTMS ERG at the
//                 gear's watts; never instead of a working simulation).
//   * No Zwift Click          -> no virtual gears ("bike"): the rider shifts
//     the real cassette, the trainer gets plain FTMS gradient simulation
//     (route grade × difficulty) - even when the trainer has the Zwift
//     service (it may carry a normal cassette).
// Advanced (Bluetooth diagnostics only): hubOverride "off" never uses the
// Zwift protocol; shiftMethod "auto" / "erg" (experimental, not saved).
// setEnabled() forces virtual gears without a Click (tests / screenshots).
//
// Road model (roadModel()): mass = profile weight (default 75 kg) + bike
// weight; CdA = road::autoCda(profile weight, height) when the setup's
// cdaAuto is on (default), else the manual CdA; Crr of the setup. The rider
// + bike mass also goes to SensorHub::riderMassKg (setHub(): the virtual
// trainer's weight setting).
//
// Persistence: the bike (wheel, bike weight, CdA auto/manual, Crr, reference
// gear for the FTMS gradient path) is stored per profile in
// profiles.gear_setup (GearSetup JSON), written 1.5 s after the last change
// (flushSettings() writes at once). A different profile loads its own bike;
// while a ride runs (setBusyCheck()) the switch waits until reloadIfPending().
// The virtual gear itself starts at 12 when the app starts.
//
// Zwift Click routing (controllerShift(), the single place):
//   1. a video ride is running             -> RlvSession::controllerShift()
//   2. a free ride is running              -> FreeRideSession::controllerShift()
//   3. the Video Ride page is open (activePage "video")    -> RlvSession::controllerShift()
//   4. the Free Ride page is open (activePage "freeride")  -> FreeRideSession::controllerShift()
//   5. otherwise (e.g. Devices page)        -> shift the gear (test it on the Devices page)
// The sessions shift in their road modes and step the ERG target by ±10 W in
// ERG. Nothing ever changes the difficulty.
class VirtualGears : public QObject
{
    Q_OBJECT
    // virtual gears active = a Zwift Click is connected (no manual switch)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool clickConnected READ clickConnected NOTIFY clickConnectedChanged)
    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)

    // current gear
    Q_PROPERTY(QVariantMap gear READ gear NOTIFY gearChanged)
    Q_PROPERTY(QString gearLabel READ gearLabel NOTIFY gearChanged)            // "12"
    Q_PROPERTY(QString gearText READ gearText NOTIFY gearChanged)              // "Gear 12/24 (2.40)"
    Q_PROPERTY(int gearIndex READ gearIndex NOTIFY gearChanged)
    Q_PROPERTY(int gearCount READ gearCount NOTIFY gearChanged)
    Q_PROPERTY(double gearRatio READ gearRatio NOTIFY gearChanged)
    Q_PROPERTY(int frontIndex READ frontIndex NOTIFY gearChanged)
    Q_PROPERTY(int rearIndex READ rearIndex NOTIFY gearChanged)
    Q_PROPERTY(int frontTeeth READ frontTeeth NOTIFY gearChanged)
    Q_PROPERTY(int rearTeeth READ rearTeeth NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftUp READ canShiftUp NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftDown READ canShiftDown NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftFrontUp READ canShiftFrontUp NOTIFY gearChanged)
    Q_PROPERTY(bool canShiftFrontDown READ canShiftFrontDown NOTIFY gearChanged)

    // setup
    Q_PROPERTY(QVariantList gears READ gears NOTIFY gearSetupChanged)          // [{index, label, ratio, front, rear}]
    Q_PROPERTY(QVariantMap gearSetup READ gearSetup NOTIFY gearSetupChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY gearSetupChanged)
    Q_PROPERTY(bool sequential READ sequential WRITE setSequential NOTIFY gearSetupChanged)
    Q_PROPERTY(int referenceFrontTeeth READ referenceFrontTeeth NOTIFY gearSetupChanged)
    Q_PROPERTY(int referenceRearTeeth READ referenceRearTeeth NOTIFY gearSetupChanged)
    Q_PROPERTY(QString referenceLabel READ referenceLabel NOTIFY gearSetupChanged)
    Q_PROPERTY(QVariantList chainringPresets READ chainringPresets CONSTANT)
    Q_PROPERTY(QVariantList cassettePresets READ cassettePresets CONSTANT)
    Q_PROPERTY(QVariantList wheelPresets READ wheelPresets CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    // How FTMS virtual gears drive a trainer that simulates gradients:
    // "gradient" (always the default = simulation). "auto" / "erg" are hidden,
    // experimental options of the Bluetooth diagnostics, never saved: every
    // start is "gradient" again (a value stored by older versions is ignored).
    Q_PROPERTY(QString shiftMethod READ shiftMethod WRITE setShiftMethod NOTIFY shiftMethodChanged)
    // "zwift" | "ftms" | "erg" | "bike" (see the table above)
    Q_PROPERTY(QString shiftingMode READ shiftingMode NOTIFY shiftingChanged)
    // "Virtual shifting: Zwift protocol" / "Virtual shifting: FTMS gradient" /
    // "Shifting: your bike's gears" (status line of the Devices card / ride pages)
    Q_PROPERTY(QString shiftingStatus READ shiftingStatus NOTIFY shiftingChanged)
    // why: "Zwift protocol handshake…", "Zwift protocol failed (…) – FTMS gradient", ...
    Q_PROPERTY(QString shiftingDetail READ shiftingDetail NOTIFY shiftingChanged)
    // the Zwift protocol failed / handshake timed out (FTMS gradient fallback in use)
    Q_PROPERTY(bool shiftingProblem READ shiftingProblem NOTIFY shiftingChanged)
    // the connected trainer exposes the Zwift trainer service
    Q_PROPERTY(bool zwiftAvailable READ zwiftAvailable NOTIFY shiftingChanged)
    // the Zwift protocol carries the virtual gear right now
    Q_PROPERTY(bool zwiftActive READ zwiftActive NOTIFY shiftingChanged)
    // Advanced override (Bluetooth diagnostics): "auto" (default) | "off"
    // (never the Zwift protocol, FTMS gradient only). Saved ("gears/zwiftProtocol").
    Q_PROPERTY(QString hubOverride READ hubOverride WRITE setHubOverride NOTIFY shiftingChanged)
    // FTMS gradient virtual gears in use: the bike's real gear on the trainer
    // (reference) matters - the hint / detection prompt are shown
    Q_PROPERTY(bool referenceNeeded READ referenceNeeded NOTIFY shiftingChanged)
    // The real gear on the trainer measured from its speed and cadence
    // (GearRatioDetector); 0 / "" until detected. Never changes the saved
    // reference gear and is never used by itself: it only PROMPTS the rider
    // (referencePrompt, useDetectedGear()) - a trainer may report a simulated
    // speed instead of its flywheel speed.
    Q_PROPERTY(double detectedRatio READ detectedRatio NOTIFY detectionChanged)
    // "44×14" (nearest of the setup and the Zwift Cog presets, preferring the
    // reference chainring) or ""
    Q_PROPERTY(QString detectedLabel READ detectedLabel NOTIFY detectionChanged)
    // detected and more than kDetectMismatch away from the saved reference gear
    Q_PROPERTY(bool detectedDiffers READ detectedDiffers NOTIFY detectionChanged)
    // the detection was confirmed by later steady windows (GearRatioDetector::stable())
    Q_PROPERTY(bool detectedStable READ detectedStable NOTIFY detectionChanged)
    // the detected ratio follows the virtual gear: the trainer reports a simulated speed
    Q_PROPERTY(bool detectedLooksVirtual READ detectedLooksVirtual NOTIFY detectionChanged)
    // "Your trainer reports 44×14 (3.14) but the setting is 50×17 (2.94) —
    // use the detected gear?" while a stable detection differs from the
    // reference (and does not just follow the virtual gear); "" otherwise.
    Q_PROPERTY(QString referencePrompt READ referencePrompt NOTIFY detectionChanged)
    // the reference gear is outside the setup (Zwift Cog preset / detected)
    Q_PROPERTY(bool referenceCustom READ referenceCustom NOTIFY gearSetupChanged)
    Q_PROPERTY(QVariantList cogPresets READ cogPresets CONSTANT)
    // the real gear is heavier than kHeavyReferenceRatio: suggest a small one
    Q_PROPERTY(bool heavyRealGear READ heavyRealGear NOTIFY detectionChanged)
    Q_PROPERTY(double referenceRatio READ referenceRatio NOTIFY gearSetupChanged)

    // road model of the rider (profile weight / height + gear setup)
    Q_PROPERTY(double riderWeightKg READ riderWeightKg NOTIFY physicsChanged)
    Q_PROPERTY(double systemMassKg READ systemMassKg NOTIFY physicsChanged)     // rider + bike
    Q_PROPERTY(double cda READ cda NOTIFY physicsChanged)                       // the CdA in use
    Q_PROPERTY(bool cdaAuto READ cdaAuto NOTIFY physicsChanged)
    Q_PROPERTY(double autoCda READ autoCda NOTIFY physicsChanged)               // estimate for the profile
    // "Rider + bike: 86 kg · CdA 0.32 (auto)"
    Q_PROPERTY(QString physicsSummary READ physicsSummary NOTIFY physicsChanged)

public:
    static constexpr int kSaveDelayMs = 1500;
    static constexpr double kDetectMismatch = 0.05;      // 5 %
    static constexpr double kHeavyReferenceRatio = 2.3;  // above ~ 34×15: the flywheel spins fast

    VirtualGears(Database *db, ProfileManager *profiles, QObject *parent = nullptr);
    ~VirtualGears() override;

    // Routing targets for controllerShift() (main.cpp).
    void setSessions(FreeRideSession *freeRide, RlvSession *rlv);
    // While it returns true a profile switch does not replace the bike.
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }
    void reloadIfPending();
    // Tests: no save timer, flushSettings() writes.
    void setManualSave(bool manual) { m_manualSave = manual; }
    void flushSettings();

    bool enabled() const { return m_clickConnected || m_forced; }
    // Forces virtual gears without a Click (tests, screenshots, diagnostics);
    // not saved.
    void setEnabled(bool on);
    bool clickConnected() const { return m_clickConnected; }
    void setClickConnected(bool connected);
    QString activePage() const { return m_activePage; }
    void setActivePage(const QString &page);

    const Gearing &gearing() const { return m_gearing; }
    const GearSetup &setup() const { return m_gearing.setup(); }
    QVariantMap gear() const;
    QString gearLabel() const { return m_gearing.label(); }
    QString gearText() const;
    int gearIndex() const { return m_gearing.gearIndex(); }
    int gearCount() const { return m_gearing.gearCount(); }
    double gearRatio() const { return m_gearing.ratio(); }
    int frontIndex() const { return m_gearing.frontIndex(); }
    int rearIndex() const { return m_gearing.rearIndex(); }
    int frontTeeth() const { return m_gearing.frontTeeth(); }
    int rearTeeth() const { return m_gearing.rearTeeth(); }
    bool canShiftUp() const { return m_gearing.canShiftUp(); }
    bool canShiftDown() const { return m_gearing.canShiftDown(); }
    bool canShiftFrontUp() const { return m_gearing.canShiftFrontUp(); }
    bool canShiftFrontDown() const { return m_gearing.canShiftFrontDown(); }

    QVariantList gears() const;
    QVariantMap gearSetup() const;
    // "Zwift's 24 virtual gears · 700×25c"
    QString summary() const
    {
        return QStringLiteral("Zwift's 24 virtual gears · %1").arg(GearSetup::wheelName(setup().circumferenceMm));
    }
    bool sequential() const { return setup().sequential; }
    void setSequential(bool on);
    int referenceFrontTeeth() const { return setup().referenceFrontTeeth; }
    int referenceRearTeeth() const { return setup().referenceRearTeeth; }
    QString referenceLabel() const { return Gearing::labelFor(referenceFrontTeeth(), referenceRearTeeth()); }
    double referenceRatio() const { return setup().referenceRatio(); }
    // R_virtual / R_reference (> 1: the virtual gear is harder than the real one)
    double gearFactor() const;
    static QVariantList chainringPresets() { return GearSetup::chainringPresets(); }
    static QVariantList cassettePresets() { return GearSetup::cassettePresets(); }
    static QVariantList wheelPresets() { return GearSetup::wheelPresets(); }
    QString lastError() const { return m_lastError; }

    RideResistance::GearMethod method() const { return m_method; }
    QString shiftMethod() const { return RideResistance::methodName(m_method); }
    void setShiftMethod(const QString &method);
    QString shiftingMode() const;
    QString shiftingStatus() const;
    QString shiftingDetail() const;
    bool shiftingProblem() const;
    bool zwiftAvailable() const;
    bool zwiftActive() const;
    QString hubOverride() const { return m_hubOverride; }
    void setHubOverride(const QString &mode);   // "auto" | "off"
    bool referenceNeeded() const { return shiftingMode() == QLatin1String("ftms"); }
    // The method for a stored "gears/method" value: Gradient when nothing was
    // chosen (invalid QVariant) or the value is unknown; "auto" / "erg" only
    // when the rider picked them (setShiftMethod() is the only writer).
    static RideResistance::GearMethod methodFromSetting(const QVariant &stored);
    double detectedRatio() const { return m_detector.ratio(); }
    QString detectedLabel() const;
    bool detectedDiffers() const;
    bool detectedStable() const { return m_detector.stable(); }
    // the trainer's speed seems to follow the VIRTUAL gear (a simulated
    // speed, not the flywheel): such a detection is not offered
    bool detectedLooksVirtual() const { return m_detectLooksVirtual; }
    QString referencePrompt() const;
    Q_INVOKABLE void dismissReferencePrompt();
    bool referenceCustom() const { return setup().referenceCustom; }
    static QVariantList cogPresets() { return GearSetup::cogPresets(); }
    bool heavyRealGear() const;
    // The real gear ratio path a works with: the saved reference gear (the
    // detected one only after the rider accepted it, useDetectedGear()).
    double effectiveReferenceRatio() const { return referenceRatio(); }
    // --- Zwift hub path: the real gear on the trainer ---------------------------
    // The virtual gear goes out adapted to the bike's real gear on the trainer:
    // sent ratio = virtual ratio × hubGearFactor(). The logic, its model (an
    // unverified assumption: the trainer brakes as if the bike were in 34×14)
    // and the safeguards are in HubGearCompensation. Here: one record per
    // trainer id ("gears/hubGear/<id>"), loaded when that trainer connects;
    // saved only once its flywheel speed was verified.
    double hubRealRatio() const;        // 0 = unknown
    double hubGearFactor() const;       // 1.0 = no compensation
    // true: send virtual × hubGearFactor(); false: the real gear is unknown
    // (not measured yet, or a provisional value timed out) - RideResistance
    // sends the light fallback gear (soft start factor), ramped
    bool hubGearKnown() const;
    // forget this trainer's assessment (speed type, real gear, calibrates):
    // measured again from the next pedal strokes (Bluetooth diagnostics)
    Q_INVOKABLE void resetHubGearAssessment();
    const HubGearCompensation &hubGear() const { return m_hubComp; }
    // one trainer packet on the hub path (onTrainerSample(); tests)
    void addHubGearSample(const TrainerSample &sample);
    void tickHubGear();   // timeouts without packets (1 s timer)
    // Feeds the detector (also called from the hub's speed / cadence updates).
    void addTrainerSample(qint64 ms, double cadenceRpm, double speedMps);
    Q_INVOKABLE void clearDetection();
    // Saves the detected gear as "Bike on the trainer" (only on request).
    Q_INVOKABLE bool useDetectedGear();
    // "[Virtual gears]" section of the Bluetooth diagnostics report: method,
    // path, route / effective grade, grade sent, ERG target, trainer speed /
    // cadence / power, detected ratio, reference gear, demanded power - for
    // the session that drives the trainer (running ride, else the open page).
    QString diagnosticsText() const;
    // The same on one line (WATT_DEBUG_GEARS=1 log).
    QString debugLine() const;
    // Everything the gear resistance depends on, on one line (diagnostic log
    // "CFG"): virtual gears on/off, method, setup, current / reference /
    // detected gear, rider (profile name and weight only), bike, mass, CdA, Crr.
    QString settingsLine() const;

    // The rider of a running / paused ride (FreeRideSession / RlvSession call
    // this on start and clear it when the ride ends; owner = the session, a
    // clear by another owner is ignored). While set and different from the
    // current profile, roadModel(), systemMassKg (-> SensorHub::riderMassKg),
    // riderWeightKg and autoCda use it - switching profiles mid-ride does not
    // change the ride's physics.
    void setRideProfile(const QObject *owner, const Profile &rider);
    void clearRideProfile(const QObject *owner);
    // The rider the physics use (see setRideProfile()), else the current profile.
    const Profile &rider() const;

    // Road model for a rider of riderKg / heightCm on this bike (riderKg <= 0:
    // rider() weight, then kDefaultRiderKg; heightCm < 0: rider() height).
    road::Model roadModel(double gradePercent, double riderKg = 0, double heightCm = -1) const;
    road::Model roadModel(double gradePercent, const Profile &rider) const
    {
        return roadModel(gradePercent, rider.weightKg, rider.heightCm);
    }
    double riderWeightKg() const;
    double systemMassKg() const { return roadModel(0).massKg; }
    double cda() const { return roadModel(0).cda; }
    bool cdaAuto() const { return setup().cdaAuto; }
    double autoCda() const;
    QString physicsSummary() const;
    // Auto CdA for a weight / height (the setup dialog's preview).
    Q_INVOKABLE double autoCdaFor(double riderKg, double heightCm = 0) const { return road::autoCda(riderKg, heightCm); }
    // Keeps SensorHub::riderMassKg in sync with the rider + bike.
    void setHub(SensorHub *hub);

    Q_INVOKABLE bool shiftUp();
    Q_INVOKABLE bool shiftDown();
    Q_INVOKABLE bool shiftFrontUp();
    Q_INVOKABLE bool shiftFrontDown();
    Q_INVOKABLE bool setGear(int front, int rear);
    Q_INVOKABLE bool setGearIndex(int index);
    // Validates and stores the setup of the current profile; applies at once
    // to both rides. chainrings/cogs: numbers in any order. false + lastError.
    Q_INVOKABLE bool setGearSetup(const QVariantList &chainrings, const QVariantList &cogs, int circumferenceMm,
                                  bool sequential);
    // cdaAuto: estimate the CdA from the profile (cda is kept as the manual
    // value for when auto is turned off).
    Q_INVOKABLE bool setBikePhysics(double bikeWeightKg, double cda, double crr, bool cdaAuto = false);
    // Wheel circumference (mm) for the virtual speed (advanced).
    Q_INVOKABLE bool setWheelCircumference(int mm);
    // Teeth of the real gear on the trainer; snapped to the setup.
    Q_INVOKABLE bool setReferenceGear(int frontTeeth, int rearTeeth);
    // Any plausible real gear (GearSetup::kMin/kMaxReference*), also outside
    // the setup (Zwift Cog presets, detected gear).
    Q_INVOKABLE bool setReferenceGearAny(int frontTeeth, int rearTeeth);
    Q_INVOKABLE void resetGearSetup();
    Q_INVOKABLE QString validateGearSetup(const QVariantList &chainrings, const QVariantList &cogs,
                                          int circumferenceMm) const;
    Q_INVOKABLE QVariantList parseTeeth(const QString &text) const;
    // Gears of a setup that is not stored yet (setup dialog preview), easiest
    // first: [{index, label, ratio, front, rear, frontTeeth, rearTeeth, speedKmh, watts}]
    // (speed at cadenceRpm, watts on a flat road). [] when invalid.
    Q_INVOKABLE QVariantList previewGears(const QVariantList &chainrings, const QVariantList &cogs,
                                          int circumferenceMm, bool sequential, int cadenceRpm = 90) const;
    Q_INVOKABLE QVariantMap defaultGearSetup() const;
    // Reloads the saved setup of the current profile (pending changes are
    // written first, so nothing is lost).
    Q_INVOKABLE void reload();

public slots:
    // Zwift Click buttons (+1 plus/harder, -1 minus/easier), see the table above.
    void controllerShift(int direction);

signals:
    void enabledChanged();
    void clickConnectedChanged();
    void activePageChanged();
    void gearChanged();
    void gearSetupChanged();
    void lastErrorChanged();
    void physicsChanged();
    void shiftMethodChanged();
    void shiftingChanged();
    void detectionChanged();
    void shifted(int direction);            // the gear changed by a shift (+1 harder)
    void notice(const QString &text);       // short message for a toast

private:
    void syncHubBackend();
    void enabledMaybeChanged(bool before);
    void updateShifting();
    void load();
    void afterShift(bool shifted, int direction);
    void setupChanged(bool gearToo);
    void scheduleSave();
    void saveNow();
    void setError(const QString &e);
    void syncHub();
    bool detectedTeeth(int *front, int *rear) const;   // nearest combination of the setup within 3 %
    static QVariantMap setupToMap(const GearSetup &s);

    Database *m_db;
    ProfileManager *m_profiles;
    QPointer<FreeRideSession> m_freeRide;
    QPointer<RlvSession> m_rlv;
    QPointer<SensorHub> m_hub;
    std::function<bool()> m_busy;
    const QObject *m_rideOwner = nullptr;
    Profile m_rideProfile;

    Gearing m_gearing;
    qint64 m_profileId = -1;
    bool m_reloadPending = false;
    bool m_forced = false;
    bool m_clickConnected = false;
    QString m_hubOverride = QStringLiteral("auto");
    QString m_lastMode;          // shiftingMode() last logged
    QString m_activePage;
    QString m_lastError;
    QTimer m_saveTimer;
    bool m_savePending = false;
    bool m_manualSave = false;
    RideResistance::GearMethod m_method = RideResistance::GearMethod::Gradient;
    GearRatioDetector m_detector;
    QElapsedTimer m_detectClock;
    bool m_detectLooksVirtual = false;
    HubGearCompensation m_hubComp;
    QString m_hubTrainerId;              // the trainer m_hubComp belongs to ("" = none / unknown)
    bool m_hubResetDone = false;
    QString hubGearKey() const;
    HubGearCompensation::Record loadHubGearRecord() const;
    void saveHubGearRecord();
    void resetHubGear();                 // (re)load the record when the trainer changes
    void noteHubGearChanges(int changes);
    void onTrainerSample(const TrainerSample &sample);
    QTimer m_hubGearTick;
    double m_promptDismissedRatio = 0;   // dismissReferencePrompt() for this detection
    qint64 m_lastDetectLogMs = -100000;
};

} // namespace watt
