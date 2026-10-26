#pragma once

#include "Gearing.h"
#include "Profile.h"

#include <QObject>
#include <QPointer>
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

// Virtual shifting: the ONE gear setup and current gear of the rider, shared
// by the Free Ride and the Video Ride, configured on the Devices page.
// Exposed to QML as "Gears" (Artilody.Backend).
//
// Virtual gears work like the gears of a mechanical groupset: shifting
// changes the gear (and with it the speed at a given cadence and the power
// the road asks for), never the trainer difficulty.
//
// Road model (roadModel()): mass = profile weight (default 75 kg) + bike
// weight; CdA = road::autoCda(profile weight, height) when the setup's
// cdaAuto is on (default), else the manual CdA; Crr of the setup. The rider
// + bike mass also goes to SensorHub::riderMassKg (setHub(): the virtual
// trainer's weight setting).
//
// Persistence: the setup (chainrings, cogs, wheel, sequential, bike weight,
// CdA auto/manual, Crr, reference gear) and the current gear are stored per profile in
// profiles.gear_setup (GearSetup JSON, DB schema v3), written 1.5 s after the
// last change (flushSettings() writes at once). A different profile loads its
// own bike; while a ride runs (setBusyCheck()) the switch waits until
// reloadIfPending() (called by the sessions when a ride ends).
//
// enabled ("Use virtual gears", setting "gears/enabled", default off): when
// on, the riding sessions use the virtual gear in their road modes (Free Ride
// "Ride", Video Ride "Simulation"); when off the rider shifts the real bike
// and the trainer simulates the road grade. A Zwift Click turns it on when it
// connects (setClickConnected(true), which also reloads the saved setup) and
// when a button is pressed while it is off (notice()).
//
// Zwift Click routing (controllerShift(), the single place):
//   1. virtual gears off -> turn on + notice "Zwift Click detected – using virtual gears"
//   2. a video ride is armed/running        -> RlvSession::controllerShift()
//   3. a free ride is armed/running         -> FreeRideSession::controllerShift()
//   4. the Video Ride page is open (activePage "video")    -> RlvSession::controllerShift()
//   5. the Free Ride page is open (activePage "freeride")  -> FreeRideSession::controllerShift()
//   6. otherwise (e.g. Devices page)        -> shift the gear (test it on the Devices page)
// The sessions shift in their road modes and step the ERG target by ±10 W in
// ERG. Nothing ever changes the difficulty.
class VirtualGears : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool clickConnected READ clickConnected NOTIFY clickConnectedChanged)
    Q_PROPERTY(QString activePage READ activePage WRITE setActivePage NOTIFY activePageChanged)

    // current gear
    Q_PROPERTY(QVariantMap gear READ gear NOTIFY gearChanged)
    Q_PROPERTY(QString gearLabel READ gearLabel NOTIFY gearChanged)
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

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool clickConnected() const { return m_clickConnected; }
    void setClickConnected(bool connected);
    QString activePage() const { return m_activePage; }
    void setActivePage(const QString &page);

    const Gearing &gearing() const { return m_gearing; }
    const GearSetup &setup() const { return m_gearing.setup(); }
    QVariantMap gear() const;
    QString gearLabel() const { return m_gearing.label(); }
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
    QString summary() const { return setup().summary(); }
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

    // Road model for a rider of riderKg / heightCm on this bike (riderKg <= 0:
    // profile weight, then kDefaultRiderKg; heightCm < 0: profile height).
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
    // Teeth of the real gear on the trainer; snapped to the setup.
    Q_INVOKABLE bool setReferenceGear(int frontTeeth, int rearTeeth);
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
    void shifted(int direction);            // the gear changed by a shift (+1 harder)
    void notice(const QString &text);       // short message for a toast

private:
    void load();
    void afterShift(bool shifted, int direction);
    void setupChanged(bool gearToo);
    void scheduleSave();
    void saveNow();
    void setError(const QString &e);
    void syncHub();
    static QVariantMap setupToMap(const GearSetup &s);

    Database *m_db;
    ProfileManager *m_profiles;
    QPointer<FreeRideSession> m_freeRide;
    QPointer<RlvSession> m_rlv;
    QPointer<SensorHub> m_hub;
    std::function<bool()> m_busy;

    Gearing m_gearing;
    qint64 m_profileId = -1;
    bool m_reloadPending = false;
    bool m_enabled = false;
    bool m_clickConnected = false;
    QString m_activePage;
    QString m_lastError;
    QTimer m_saveTimer;
    bool m_savePending = false;
    bool m_manualSave = false;
};

} // namespace watt
