#include "VirtualGears.h"

#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvSession.h"
#include "SensorHub.h"
#include "DiagLog.h"

#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
// "gears/enabled" (the old "Use virtual gears" switch), "gears/method" and
// "gears/backend" of earlier versions are ignored: virtual gears follow the
// Zwift Click, the method is always Gradient unless chosen in the diagnostics.
const QString kSettingHubOverride = QStringLiteral("gears/zwiftProtocol");

QVector<int> toIntList(const QVariantList &list, bool *ok)
{
    QVector<int> out;
    *ok = true;
    for (const QVariant &v : list) {
        bool good = false;
        const double d = v.toDouble(&good);
        if (!good || !std::isfinite(d) || d != std::floor(d)) {
            *ok = false;
            return {};
        }
        out.append(int(d));
    }
    return out;
}
} // namespace

RideResistance::GearMethod VirtualGears::methodFromSetting(const QVariant &stored)
{
    // Gradient (simulation) unless the value names another method. Since
    // 1.3.2 the constructor no longer reads a stored method at all.
    if (!stored.isValid() || stored.isNull())
        return RideResistance::GearMethod::Gradient;
    return RideResistance::parseMethod(stored.toString()).value_or(RideResistance::GearMethod::Gradient);
}

VirtualGears::VirtualGears(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &VirtualGears::saveNow);
    connect(this, &VirtualGears::gearSetupChanged, this, &VirtualGears::physicsChanged);
    connect(this, &VirtualGears::gearSetupChanged, this, &VirtualGears::detectionChanged);   // detectedDiffers / heavyRealGear
    connect(this, &VirtualGears::physicsChanged, this, &VirtualGears::syncHub);
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        emit physicsChanged();   // weight / height may have changed
        if (m_profiles->profileId() == m_profileId)
            return;   // same rider, e.g. FTP edited
        if (m_busy && m_busy()) {
            m_reloadPending = true;   // keep the bike of the running ride
            return;
        }
        load();
    });
    m_method = RideResistance::GearMethod::Gradient;   // always simulation by default
    m_hubOverride = m_db->setting(kSettingHubOverride, QStringLiteral("auto")).toString() == QLatin1String("off")
                        ? QStringLiteral("off") : QStringLiteral("auto");
    connect(this, &VirtualGears::enabledChanged, this, &VirtualGears::updateShifting);
    connect(this, &VirtualGears::shiftMethodChanged, this, &VirtualGears::updateShifting);
    m_detectClock.start();
    load();
}

VirtualGears::~VirtualGears()
{
    saveNow();
}

void VirtualGears::setHub(SensorHub *hub)
{
    if (m_hub)
        disconnect(m_hub, nullptr, this, nullptr);
    m_hub = hub;
    if (m_hub) {
        connect(m_hub, &SensorHub::speedChanged, this, &VirtualGears::sampleHub);
        connect(m_hub, &SensorHub::cadenceChanged, this, &VirtualGears::sampleHub);
        connect(m_hub, &SensorHub::trainerConnectedChanged, this, [this] {
            if (!m_hub->trainerConnected())
                clearDetection();   // another trainer / bike may come
        });
        connect(m_hub, &SensorHub::zwiftHubChanged, this, &VirtualGears::updateShifting);
        connect(m_hub, &SensorHub::trainerConnectedChanged, this, &VirtualGears::updateShifting);
        connect(m_hub, &SensorHub::trainerSimulationSupportChanged, this, &VirtualGears::updateShifting);
    }
    syncHub();
    syncHubBackend();
    updateShifting();
}

void VirtualGears::syncHubBackend()
{
    // the CONNECTED trainer may start the Zwift handshake only while virtual
    // gears are active (Zwift Click connected) - never just because the
    // trainer supports it
    if (m_hub)
        m_hub->setZwiftHubWanted(enabled() && m_hubOverride != QLatin1String("off"));
}

void VirtualGears::setHubOverride(const QString &mode)
{
    const QString m = mode == QLatin1String("off") ? QStringLiteral("off") : QStringLiteral("auto");
    if (m == m_hubOverride)
        return;
    m_hubOverride = m;
    m_db->setSetting(kSettingHubOverride, m_hubOverride);
    DiagLog::write("VSHIFT", QStringLiteral("advanced override: Zwift protocol %1").arg(m == QLatin1String("off")
                                                                                         ? QStringLiteral("OFF (FTMS gradient only)")
                                                                                         : QStringLiteral("automatic")));
    syncHubBackend();
    updateShifting();
    emit shiftingChanged();
}

bool VirtualGears::zwiftAvailable() const
{
    return m_hub && m_hub->trainerConnected() && m_hub->trainerHasZwiftHub();
}

bool VirtualGears::zwiftActive() const
{
    return enabled() && m_hubOverride != QLatin1String("off") && m_hub && m_hub->trainerZwiftHubActive();
}

QString VirtualGears::shiftingMode() const
{
    if (!enabled())
        return QStringLiteral("bike");
    if (zwiftActive())
        return QStringLiteral("zwift");
    // handshake running: the session already uses the hub path (nothing is
    // sent to the trainer until the handshake is done, never FTMS meanwhile)
    if (m_hubOverride != QLatin1String("off") && m_hub && m_hub->zwiftHubWanted()
        && m_hub->trainerZwiftHubState() == SensorHub::HubStarting)
        return QStringLiteral("zwift");
    if (m_method == RideResistance::GearMethod::Erg
        || (m_hub && m_hub->trainerConnected() && !RideResistance::gearSimulationPossible(m_hub)))
        return QStringLiteral("erg");
    return QStringLiteral("ftms");
}

QString VirtualGears::shiftingStatus() const
{
    const QString mode = shiftingMode();
    if (mode == QLatin1String("zwift"))
        return QStringLiteral("Virtual shifting: Zwift protocol");
    if (mode == QLatin1String("ftms"))
        return QStringLiteral("Virtual shifting: FTMS gradient");
    if (mode == QLatin1String("erg"))
        return QStringLiteral("Virtual shifting: FTMS ERG (trainer without gradient simulation)");
    return QStringLiteral("Shifting: your bike's gears");
}

bool VirtualGears::shiftingProblem() const
{
    return enabled() && m_hubOverride != QLatin1String("off") && m_hub && m_hub->trainerConnected()
           && m_hub->trainerZwiftHubState() == SensorHub::HubFailed;
}

QString VirtualGears::shiftingDetail() const
{
    if (!enabled())
        return QStringLiteral("No Zwift Click connected – shift your bike's real gears; the trainer simulates the road grade.");
    if (!m_hub || !m_hub->trainerConnected())
        return QStringLiteral("Zwift Click connected – Zwift's 24 virtual gears. Connect a trainer.");
    if (m_hubOverride == QLatin1String("off") && m_hub->trainerHasZwiftHub())
        return QStringLiteral("Zwift protocol turned off in Bluetooth diagnostics – FTMS gradient.");
    switch (m_hub->trainerZwiftHubState()) {
    case SensorHub::HubActive:
        return QStringLiteral("The trainer does the physics (incline + gear ratio).");
    case SensorHub::HubStarting:
        return QStringLiteral("Zwift protocol handshake… (the trainer gets the road and gear once it is done)");
    case SensorHub::HubAvailable:
        return QStringLiteral("Trainer has the Zwift trainer service – starting the Zwift protocol…");
    case SensorHub::HubFailed: {
        const QString why = m_hub->trainerZwiftHubStatus();
        return QStringLiteral("Zwift protocol failed%1 – using FTMS gradient.")
            .arg(why.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(why));
    }
    default:
        break;
    }
    if (shiftingMode() == QLatin1String("erg"))
        return QStringLiteral("This trainer cannot simulate gradients – the gear's power is set with ERG.");
    return QStringLiteral("Trainer without the Zwift trainer service – gear-adjusted grade via FTMS.");
}

void VirtualGears::updateShifting()
{
    const QString mode = shiftingMode();
    if (mode != m_lastMode) {
        DiagLog::write("VSHIFT", QStringLiteral("shifting mode %1 -> %2: %3 | %4")
                                     .arg(m_lastMode.isEmpty() ? QStringLiteral("-") : m_lastMode, mode, shiftingStatus(),
                                          shiftingDetail()));
        m_lastMode = mode;
    }
    emit shiftingChanged();
}

void VirtualGears::sampleHub()
{
    if (!m_hub || !m_hub->trainerConnected() || !m_hub->trainerReportsSpeed() || !m_hub->cadenceReported())
        return;
    addTrainerSample(m_detectClock.elapsed(), m_hub->cadence(), m_hub->speedMps());
}

void VirtualGears::addTrainerSample(qint64 ms, double cadenceRpm, double speedMps)
{
    const bool wasStable = m_detector.stable();
    const int confirmations = m_detector.confirmations();
    const bool changed = m_detector.addSample(ms, cadenceRpm, speedMps, setup().circumferenceMm);
    // Both inputs in the log (a trainer may report a simulated speed rather
    // than its flywheel speed): every 5 s and at every result.
    if (changed || confirmations != m_detector.confirmations() || ms - m_lastDetectLogMs >= 5000) {
        m_lastDetectLogMs = ms;
        const double sampleRatio = cadenceRpm > 0 && setup().circumferenceMm > 0
                                       ? speedMps / (cadenceRpm / 60.0 * setup().circumferenceMm / 1000.0) : 0.0;
        DiagLog::write("GEAR", QStringLiteral("detect: trainer speed %1 km/h cadence %2 rpm -> ratio %3 | result %4 (%5 confirmations%6) "
                                              "virtual gear %7 (%8) reference %9 (%10)")
                                   .arg(speedMps * 3.6, 0, 'f', 1)
                                   .arg(cadenceRpm, 0, 'f', 0)
                                   .arg(sampleRatio, 0, 'f', 3)
                                   .arg(m_detector.valid() ? QString::number(m_detector.ratio(), 'f', 3) : QStringLiteral("-"))
                                   .arg(m_detector.confirmations())
                                   .arg(m_detector.stable() ? QStringLiteral(", stable") : QString())
                                   .arg(gearLabel())
                                   .arg(gearRatio(), 0, 'f', 3)
                                   .arg(referenceLabel())
                                   .arg(referenceRatio(), 0, 'f', 3));
    }
    if (changed || confirmations != m_detector.confirmations()) {
        // a "real gear" equal to the current virtual gear (that is not the
        // reference) means the trainer reports its simulated speed
        const double ref = referenceRatio();
        const bool looksVirtual = enabled() && m_detector.valid() && std::abs(gearRatio() - ref) > ref * kDetectMismatch
                                  && std::abs(m_detector.ratio() - gearRatio()) <= gearRatio() * 0.02;
        if (looksVirtual != m_detectLooksVirtual) {
            m_detectLooksVirtual = looksVirtual;
            DiagLog::write("GEAR", looksVirtual ? QStringLiteral("detected ratio follows the VIRTUAL gear: the trainer seems to report a "
                                                                 "simulated speed - no reference gear suggestion")
                                                : QStringLiteral("detected ratio no longer follows the virtual gear"));
            emit detectionChanged();
        }
    }
    if (changed || wasStable != m_detector.stable())
        emit detectionChanged();
}

void VirtualGears::clearDetection()
{
    m_detectLooksVirtual = false;
    if (!m_detector.valid()) {
        m_detector.clear();
        return;
    }
    m_detector.clear();
    emit detectionChanged();
}

bool VirtualGears::detectedTeeth(int *front, int *rear) const
{
    if (!m_detector.valid())
        return false;
    const double r = m_detector.ratio();
    // candidates: the gears of the setup and the Zwift Cog presets
    QVector<QPair<int, int>> candidates;
    for (int f : setup().chainrings)
        for (int c : setup().cogs)
            candidates.append({ f, c });
    for (const QVariant &v : GearSetup::cogPresets()) {
        const QVariantMap m = v.toMap();
        candidates.append({ m.value(QStringLiteral("front")).toInt(), m.value(QStringLiteral("rear")).toInt() });
    }
    double bestErr = 1e9;
    for (const auto &c : std::as_const(candidates)) {
        const double err = std::abs(double(c.first) / c.second - r) / r;
        if (err < bestErr) {
            bestErr = err;
            *front = c.first;
            *rear = c.second;
        }
    }
    if (bestErr > 0.03)
        return false;
    // several combinations give about the same ratio (44×14 ≈ 50×16): the
    // rider's chainring is the likelier one
    for (const auto &c : std::as_const(candidates)) {
        const double err = std::abs(double(c.first) / c.second - r) / r;
        if (c.first == referenceFrontTeeth() && err <= bestErr + 0.005) {
            *front = c.first;
            *rear = c.second;
            break;
        }
    }
    return true;
}

QString VirtualGears::referencePrompt() const
{
    // only for FTMS gradient virtual gears (the Zwift protocol and the real
    // gears do not use the reference gear)
    if (!referenceNeeded() || !detectedDiffers() || !detectedStable() || m_detectLooksVirtual)
        return QString();
    if (m_promptDismissedRatio > 0 && std::abs(m_promptDismissedRatio - m_detector.ratio()) <= m_detector.ratio() * 0.02)
        return QString();
    const QString label = detectedLabel();
    return QStringLiteral("Your trainer reports %1(%2) but the setting is %3 (%4) — use the detected gear?")
        .arg(label.isEmpty() ? QString() : label + QLatin1Char(' '))
        .arg(m_detector.ratio(), 0, 'f', 2)
        .arg(referenceLabel())
        .arg(referenceRatio(), 0, 'f', 2);
}

void VirtualGears::dismissReferencePrompt()
{
    if (!m_detector.valid())
        return;
    m_promptDismissedRatio = m_detector.ratio();
    DiagLog::write("GEAR", QStringLiteral("reference gear suggestion %1 dismissed by the rider").arg(m_detector.ratio(), 0, 'f', 3));
    emit detectionChanged();
}

QString VirtualGears::detectedLabel() const
{
    int f = 0, c = 0;
    return detectedTeeth(&f, &c) ? Gearing::labelFor(f, c) : QString();
}

bool VirtualGears::useDetectedGear()
{
    int f = 0, c = 0;
    if (!detectedTeeth(&f, &c) || !setReferenceGearAny(f, c))
        return false;
    DiagLog::write("GEAR", QStringLiteral("rider accepted the detected gear: reference now %1 (%2)")
                               .arg(referenceLabel())
                               .arg(referenceRatio(), 0, 'f', 3));
    return true;
}

bool VirtualGears::detectedDiffers() const
{
    const double ref = referenceRatio();
    return m_detector.valid() && ref > 0 && std::abs(m_detector.ratio() - ref) > ref * kDetectMismatch;
}

bool VirtualGears::heavyRealGear() const
{
    return effectiveReferenceRatio() > kHeavyReferenceRatio;
}

namespace {
struct GearsState
{
    QString session = QStringLiteral("none");
    QString path = QStringLiteral("-");
    QString decision;
    double routeGrade = 0, effectiveGrade = 0;
    bool hasGrade = false;
    int demanded = 0;
    double wantedGrade = 0, vRef = 0;
};
} // namespace

static GearsState gearsState(const VirtualGears *g, const RlvSession *rlv, const FreeRideSession *fr)
{
    GearsState st;
    const bool onRlv = rlv && (rlv->active() || ((!fr || !fr->active()) && g->activePage() != QLatin1String("freeride")));
    if (onRlv) {
        st.session = QStringLiteral("video ride (%1, %2)").arg(rlv->state(), rlv->mode());
        st.path = rlv->resistancePath();
        st.routeGrade = rlv->gradePercent();
        st.effectiveGrade = rlv->effectiveGradePercent();
        st.hasGrade = true;
        st.demanded = rlv->gearTargetWatts();
        st.wantedGrade = rlv->gearSimulationGrade();
        st.decision = rlv->resistance().gearDebug().decision;
        st.vRef = rlv->resistance().gearDebug().referenceSpeedMps;
    } else if (fr) {
        st.session = QStringLiteral("free ride (%1, %2)").arg(fr->state(), fr->mode());
        st.path = fr->resistancePath();
        st.routeGrade = st.effectiveGrade = fr->gradePercent();
        st.hasGrade = true;
        st.demanded = fr->gearTargetWatts();
        st.wantedGrade = fr->gearSimulationGrade();
        st.decision = fr->resistance().gearDebug().decision;
        st.vRef = fr->resistance().gearDebug().referenceSpeedMps;
    }
    if (st.path != QLatin1String("gearHybrid"))
        st.decision.clear();
    return st;
}

static QString trainerCommand(const SensorHub *hub)
{
    if (!hub)
        return QStringLiteral("-");
    if (hub->simulationActive())
        return QStringLiteral("SIM grade %1 %  Crr %2  Cw %3 kg/m")
            .arg(hub->requestedGrade(), 0, 'f', 2)
            .arg(hub->simulationCrr(), 0, 'f', 4)
            .arg(hub->simulationCw(), 0, 'f', 2);
    if (hub->ergActive())
        return QStringLiteral("ERG %1 W").arg(hub->requestedTarget());
    return QStringLiteral("free ride (minimum)");
}

QString VirtualGears::diagnosticsText() const
{
    const GearsState st = gearsState(this, m_rlv, m_freeRide);
    QString r;
    QTextStream out(&r);
    out << "[Virtual gears]\n";
    out << "Zwift Click: " << (m_clickConnected ? "connected" : "-") << "  virtual gears: "
        << (enabled() ? (m_clickConnected ? "active" : "active (forced)") : "off (real gears)")
        << "  FTMS method: " << shiftMethod() << "  Zwift protocol override: " << m_hubOverride << "\n";
    out << shiftingStatus() << " – " << shiftingDetail() << "\n";
    out << "Session: " << st.session << "  path: " << st.path;
    if (!st.decision.isEmpty())
        out << " -> " << st.decision;
    out << "\n";
    out << "Gear: " << gearText() << " (Zwift's 24 gears)"
        << "  reference (saved): " << referenceLabel() << QStringLiteral(" (%1)").arg(referenceRatio(), 0, 'f', 3)
        << "  detected: "
        << (m_detector.valid() ? QStringLiteral("%1 (%2)").arg(detectedLabel().isEmpty() ? QStringLiteral("?") : detectedLabel())
                                     .arg(detectedRatio(), 0, 'f', 3)
                               : QStringLiteral("not yet"))
        << QStringLiteral("  in use: %1").arg(effectiveReferenceRatio(), 0, 'f', 3) << "\n";
    if (st.hasGrade)
        out << QStringLiteral("Route grade: %1 %  effective grade: %2 %\n")
                   .arg(st.routeGrade, 0, 'f', 2)
                   .arg(st.effectiveGrade, 0, 'f', 2);
    out << QStringLiteral("Demanded (gear) power: %1 W  gradient path would send: %2 %  v_ref: %3 km/h\n")
               .arg(st.demanded)
               .arg(st.wantedGrade, 0, 'f', 2)
               .arg(st.vRef * 3.6, 0, 'f', 1);
    out << "Trainer command: " << trainerCommand(m_hub) << "\n";
    if (m_hub) {
        out << QStringLiteral("Trainer: %1  speed %2 km/h (%3)  cadence %4 rpm (%5)  power %6 W  gradient simulation: %7\n")
                   .arg(m_hub->trainerConnected() ? QStringLiteral("connected") : QStringLiteral("not connected"))
                   .arg(m_hub->speedKmh(), 0, 'f', 1)
                   .arg(m_hub->trainerReportsSpeed() ? QStringLiteral("reported") : QStringLiteral("not reported"))
                   .arg(m_hub->cadence())
                   .arg(m_hub->cadenceReported() ? QStringLiteral("reported") : QStringLiteral("not reported"))
                   .arg(m_hub->power())
                   .arg(m_hub->trainerSimulationSupport() > 0    ? QStringLiteral("supported")
                        : m_hub->trainerSimulationSupport() == 0 ? QStringLiteral("not supported")
                                                                 : QStringLiteral("unknown"));
    }
    out << "Road model: " << physicsSummary() << "\n";
    return r;
}

QString VirtualGears::debugLine() const
{
    const GearsState st = gearsState(this, m_rlv, m_freeRide);
    return QStringLiteral("method=%1 session=%2 path=%3%4 grade=%5 eff=%6 demand=%7W wantGrade=%8 vRef=%9km/h "
                          "gear=%10 ref=%11 det=%12 cmd[%13] trainer[v=%14 cad=%15 P=%16]")
        .arg(shiftMethod(), st.session, st.path, st.decision.isEmpty() ? QString() : QStringLiteral("->") + st.decision)
        .arg(st.routeGrade, 0, 'f', 2)
        .arg(st.effectiveGrade, 0, 'f', 2)
        .arg(st.demanded)
        .arg(st.wantedGrade, 0, 'f', 2)
        .arg(st.vRef * 3.6, 0, 'f', 1)
        .arg(gearLabel())
        .arg(referenceRatio(), 0, 'f', 3)
        .arg(detectedRatio(), 0, 'f', 3)
        .arg(trainerCommand(m_hub))
        .arg(m_hub ? m_hub->speedKmh() : 0.0, 0, 'f', 1)
        .arg(m_hub ? m_hub->cadence() : 0)
        .arg(m_hub ? m_hub->power() : 0);
}

QString VirtualGears::settingsLine() const
{
    auto join = [](const QVector<int> &v) {
        QStringList out;
        for (int x : v)
            out << QString::number(x);
        return out.join(QLatin1Char('-'));
    };
    const GearSetup &s = setup();
    const road::Model rm = roadModel(0);
    const Profile &who = rider();
    return QStringLiteral("shifting=%1 zwiftOverride=%2 ").arg(shiftingMode(), m_hubOverride)
           + QStringLiteral("virtualGears=%1 method=%2 click=%3 gear=%4 (ratio %5) reference=%6 (ratio %7) "
                          "detected=%8 old-setup(ignored)[rings %9 cogs %10] wheel %11 mm %12 rider[%13 %14 kg] "
                          "bike %15 kg mass %16 kg CdA %17 (%18) Crr %19 rho %20 eta %21")
        .arg(enabled() ? QStringLiteral("on") : QStringLiteral("off"), shiftMethod(),
             m_clickConnected ? QStringLiteral("connected") : QStringLiteral("-"), gearLabel())
        .arg(gearRatio(), 0, 'f', 3)
        .arg(referenceLabel())
        .arg(referenceRatio(), 0, 'f', 3)
        .arg(m_detector.valid() ? QStringLiteral("%1 (%2)").arg(detectedLabel().isEmpty() ? QStringLiteral("?") : detectedLabel())
                                      .arg(detectedRatio(), 0, 'f', 3)
                                : QStringLiteral("-"))
        .arg(join(s.chainrings), join(s.cogs))
        .arg(s.circumferenceMm)
        .arg(QStringLiteral("zwift24"))
        .arg(who.name)
        .arg(riderWeightKg(), 0, 'f', 1)
        .arg(s.bikeWeightKg, 0, 'f', 1)
        .arg(rm.massKg, 0, 'f', 1)
        .arg(rm.cda, 0, 'f', 3)
        .arg(s.cdaAuto ? QStringLiteral("auto") : QStringLiteral("manual"))
        .arg(rm.crr, 0, 'f', 4)
        .arg(rm.airDensity, 0, 'f', 3)
        .arg(rm.efficiency, 0, 'f', 2);
}

void VirtualGears::setShiftMethod(const QString &method)
{
    const auto m = RideResistance::parseMethod(method);
    if (!m || *m == m_method)
        return;
    m_method = *m;   // not saved: the next start is Gradient again
    DiagLog::write("VSHIFT", QStringLiteral("advanced: FTMS virtual shifting method %1%2").arg(shiftMethod(),
                       m_method == RideResistance::GearMethod::Gradient ? QString() : QStringLiteral(" (experimental)")));
    emit shiftMethodChanged();
}

void VirtualGears::syncHub()
{
    if (m_hub)
        m_hub->setRiderMassKg(systemMassKg());
}

void VirtualGears::setSessions(FreeRideSession *freeRide, RlvSession *rlv)
{
    m_freeRide = freeRide;
    m_rlv = rlv;
}

// --- persistence -------------------------------------------------------------------

void VirtualGears::load()
{
    saveNow();   // a pending change belongs to the previous profile
    m_reloadPending = false;
    m_profileId = m_profiles->profileId();
    const QString json = m_profileId >= 0 ? m_db->gearSetupJson(m_profileId) : QString();
    // Zwift's fixed 24 gears: the stored chainrings / cogs (and their gear)
    // are kept in the database but not used; the current virtual gear stays
    const int gear = m_gearing.isZwift() ? m_gearing.zwiftGear() : zwiftgears::kStartGear;
    m_gearing = Gearing::zwift(GearSetup::fromJsonText(json), gear);
    emit gearSetupChanged();
    emit gearChanged();
}

void VirtualGears::reload()
{
    load();
}

void VirtualGears::reloadIfPending()
{
    if (m_reloadPending)
        load();
}

void VirtualGears::scheduleSave()
{
    m_savePending = true;
    if (!m_manualSave)
        m_saveTimer.start();
}

void VirtualGears::flushSettings()
{
    saveNow();
}

void VirtualGears::saveNow()
{
    m_saveTimer.stop();
    if (!m_savePending)
        return;
    m_savePending = false;
    if (m_profileId >= 0)
        m_db->setGearSetupJson(m_profileId, m_gearing.setup().toJsonText());
}

void VirtualGears::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

// --- switches ------------------------------------------------------------------------

void VirtualGears::enabledMaybeChanged(bool before)
{
    if (enabled() == before)
        return;
    DiagLog::write("VSHIFT", enabled() ? QStringLiteral("virtual gears ACTIVE (%1) – Zwift's 24 gears, %2")
                                             .arg(m_clickConnected ? QStringLiteral("Zwift Click connected")
                                                                   : QStringLiteral("forced"),
                                                  gearText())
                                       : QStringLiteral("virtual gears OFF (no Zwift Click) – real gears, plain FTMS gradient simulation"));
    syncHubBackend();
    emit enabledChanged();
}

void VirtualGears::setEnabled(bool on)
{
    const bool before = enabled();
    m_forced = on;
    enabledMaybeChanged(before);
}

void VirtualGears::setClickConnected(bool connected)
{
    if (m_clickConnected == connected)
        return;
    const bool before = enabled();
    m_clickConnected = connected;
    emit clickConnectedChanged();
    enabledMaybeChanged(before);
}

void VirtualGears::setActivePage(const QString &page)
{
    if (m_activePage == page)
        return;
    m_activePage = page;
    emit activePageChanged();
}

void VirtualGears::controllerShift(int direction)
{
    if (direction == 0)
        return;
    // a ride that runs gets the buttons wherever the rider is;
    // a paused one only on its own page (else the open page's session does)
    const bool onVideo = m_activePage == QLatin1String("video");
    const bool onFreeRide = m_activePage == QLatin1String("freeride");
    if (m_rlv && (m_rlv->running() || (m_rlv->paused() && onVideo))) {
        m_rlv->controllerShift(direction);
        return;
    }
    if (m_freeRide && (m_freeRide->running() || (m_freeRide->paused() && onFreeRide))) {
        m_freeRide->controllerShift(direction);
        return;
    }
    if (m_rlv && m_activePage == QLatin1String("video")) {
        m_rlv->controllerShift(direction);
        return;
    }
    if (m_freeRide && m_activePage == QLatin1String("freeride")) {
        m_freeRide->controllerShift(direction);
        return;
    }
    if (direction > 0)
        shiftUp();
    else
        shiftDown();
}

// --- gear ------------------------------------------------------------------------------

double VirtualGears::gearFactor() const
{
    const double ref = referenceRatio();
    return ref > 0 ? m_gearing.ratio() / ref : 1.0;
}

const Profile &VirtualGears::rider() const
{
    // during a ride its rider (profile snapshot), unless that is the current
    // profile (then edits of weight / height apply at once)
    const Profile &cur = m_profiles->current();
    if (m_rideOwner && m_rideProfile.id != cur.id)
        return m_rideProfile;
    return cur;
}

void VirtualGears::setRideProfile(const QObject *owner, const Profile &rider)
{
    if (!owner)
        return;
    m_rideOwner = owner;
    m_rideProfile = rider;
    emit physicsChanged();
}

void VirtualGears::clearRideProfile(const QObject *owner)
{
    if (!owner || m_rideOwner != owner)
        return;   // another session's ride
    m_rideOwner = nullptr;
    m_rideProfile = Profile();
    emit physicsChanged();
}

road::Model VirtualGears::roadModel(double gradePercent, double riderKg, double heightCm) const
{
    if (!(riderKg > 0))
        riderKg = rider().weightKg;
    if (!(riderKg > 0))
        riderKg = road::kDefaultRiderKg;
    riderKg = std::max(30.0, riderKg);
    if (heightCm < 0)
        heightCm = rider().heightCm;
    const GearSetup &s = setup();
    road::Model m;
    m.massKg = riderKg + s.bikeWeightKg;
    m.cda = s.cdaAuto ? road::autoCda(riderKg, heightCm) : s.cda;
    m.crr = s.crr;
    m.gradePercent = gradePercent;
    return m;
}

double VirtualGears::riderWeightKg() const
{
    const double w = rider().weightKg;
    return w > 0 ? w : road::kDefaultRiderKg;
}

double VirtualGears::autoCda() const
{
    return road::autoCda(riderWeightKg(), rider().heightCm);
}

QString VirtualGears::physicsSummary() const
{
    const road::Model m = roadModel(0);
    return QStringLiteral("Rider + bike: %1 kg · CdA %2 (%3)")
        .arg(QString::number(m.massKg, 'f', std::abs(m.massKg - std::round(m.massKg)) < 0.05 ? 0 : 1))
        .arg(m.cda, 0, 'f', 2)
        .arg(cdaAuto() ? QStringLiteral("auto") : QStringLiteral("manual"));
}

QVariantMap VirtualGears::gear() const
{
    return QVariantMap { { QStringLiteral("front"), m_gearing.frontIndex() },
                         { QStringLiteral("rear"), m_gearing.rearIndex() },
                         { QStringLiteral("frontTeeth"), m_gearing.frontTeeth() },
                         { QStringLiteral("rearTeeth"), m_gearing.rearTeeth() },
                         { QStringLiteral("label"), m_gearing.label() },
                         { QStringLiteral("index"), m_gearing.gearIndex() },
                         { QStringLiteral("count"), m_gearing.gearCount() },
                         { QStringLiteral("ratio"), m_gearing.ratio() } };
}

QString VirtualGears::gearText() const
{
    return QStringLiteral("Gear %1/%2 (%3)").arg(m_gearing.gearIndex() + 1).arg(m_gearing.gearCount())
        .arg(m_gearing.ratio(), 0, 'f', 2);
}

QVariantList VirtualGears::gears() const
{
    QVariantList out;
    const QVector<Gear> list = m_gearing.sequence();
    for (int i = 0; i < list.size(); ++i) {
        const Gear &g = list.at(i);
        out.append(QVariantMap { { QStringLiteral("index"), i },
                                 { QStringLiteral("label"), m_gearing.isZwift() ? QString::number(i + 1)
                                                                                : Gearing::labelFor(g.frontTeeth, g.rearTeeth) },
                                 { QStringLiteral("ratio"), g.ratio },
                                 { QStringLiteral("front"), g.front },
                                 { QStringLiteral("rear"), g.rear } });
    }
    return out;
}

QVariantMap VirtualGears::setupToMap(const GearSetup &s)
{
    QVariantList rings, cogs;
    for (int t : s.chainrings)
        rings.append(t);
    for (int t : s.cogs)
        cogs.append(t);
    return QVariantMap { { QStringLiteral("chainrings"), rings },
                         { QStringLiteral("cogs"), cogs },
                         { QStringLiteral("circumferenceMm"), s.circumferenceMm },
                         { QStringLiteral("sequential"), s.sequential },
                         { QStringLiteral("bikeWeightKg"), s.bikeWeightKg },
                         { QStringLiteral("cdaAuto"), s.cdaAuto },
                         { QStringLiteral("cda"), s.cda },
                         { QStringLiteral("crr"), s.crr },
                         { QStringLiteral("referenceFrontTeeth"), s.referenceFrontTeeth },
                         { QStringLiteral("referenceRearTeeth"), s.referenceRearTeeth } };
}

QVariantMap VirtualGears::gearSetup() const
{
    return setupToMap(setup());
}

QVariantMap VirtualGears::defaultGearSetup() const
{
    return setupToMap(GearSetup::defaults());
}

void VirtualGears::afterShift(bool didShift, int direction)
{
    if (!didShift)
        return;
    // the virtual gear is not saved (Zwift's table, start gear 12)
    emit gearChanged();
    emit shifted(direction);
}

bool VirtualGears::shiftUp()
{
    const bool ok = m_gearing.shiftUp();
    afterShift(ok, 1);
    return ok;
}

bool VirtualGears::shiftDown()
{
    const bool ok = m_gearing.shiftDown();
    afterShift(ok, -1);
    return ok;
}

bool VirtualGears::shiftFrontUp()
{
    const bool ok = m_gearing.shiftFrontUp();
    afterShift(ok, 1);
    return ok;
}

bool VirtualGears::shiftFrontDown()
{
    const bool ok = m_gearing.shiftFrontDown();
    afterShift(ok, -1);
    return ok;
}

bool VirtualGears::setGear(int front, int rear)
{
    const double before = m_gearing.ratio();
    if (!m_gearing.setGear(front, rear))
        return false;
    afterShift(true, m_gearing.ratio() >= before ? 1 : -1);
    return true;
}

bool VirtualGears::setGearIndex(int index)
{
    const double before = m_gearing.ratio();
    if (!m_gearing.setGearIndex(index))
        return false;
    afterShift(true, m_gearing.ratio() >= before ? 1 : -1);
    return true;
}

void VirtualGears::setupChanged(bool gearToo)
{
    setError(QString());
    scheduleSave();
    emit gearSetupChanged();
    if (gearToo)
        emit gearChanged();
}

void VirtualGears::setSequential(bool on)
{
    if (sequential() == on)
        return;
    m_gearing.setSequential(on);
    setupChanged(true);
}

QString VirtualGears::validateGearSetup(const QVariantList &chainrings, const QVariantList &cogs,
                                        int circumferenceMm) const
{
    bool okRings = false, okCogs = false;
    const QVector<int> rings = toIntList(chainrings, &okRings);
    const QVector<int> sprockets = toIntList(cogs, &okCogs);
    if (!okRings)
        return QStringLiteral("Chainrings must be whole numbers");
    if (!okCogs)
        return QStringLiteral("Cogs must be whole numbers");
    QString error;
    if (!GearSetup::validate(rings, sprockets, circumferenceMm, &error))
        return error;
    return QString();
}

bool VirtualGears::setGearSetup(const QVariantList &chainrings, const QVariantList &cogs, int circumferenceMm, bool seq)
{
    const QString error = validateGearSetup(chainrings, cogs, circumferenceMm);
    if (!error.isEmpty()) {
        setError(error);
        return false;
    }
    bool ok = false;
    GearSetup s = setup();
    s.chainrings = toIntList(chainrings, &ok);
    s.cogs = toIntList(cogs, &ok);
    s.circumferenceMm = circumferenceMm;
    s.sequential = seq;
    s.frontIndex = s.rearIndex = -1;   // -> nearest ratio to the current gear
    m_gearing.setSetup(s);
    setupChanged(true);
    return true;
}

bool VirtualGears::setBikePhysics(double bikeWeightKg, double cda, double crr, bool cdaAuto)
{
    if (!std::isfinite(bikeWeightKg) || bikeWeightKg < 0 || bikeWeightKg > 60) {
        setError(QStringLiteral("Bike weight must be 0–60 kg"));
        return false;
    }
    if (!std::isfinite(cda) || cda < 0.1 || cda > 1.0) {
        setError(QStringLiteral("CdA must be 0.10–1.00 m²"));
        return false;
    }
    if (!std::isfinite(crr) || crr < 0.001 || crr > 0.05) {
        setError(QStringLiteral("Crr must be 0.001–0.050"));
        return false;
    }
    GearSetup s = setup();
    s.bikeWeightKg = bikeWeightKg;
    s.cdaAuto = cdaAuto;
    s.cda = cda;
    s.crr = crr;
    m_gearing.setSetup(s);
    setupChanged(false);
    return true;
}

bool VirtualGears::setReferenceGear(int frontTeeth, int rearTeeth)
{
    const GearSetup &cur = setup();
    if (!cur.chainrings.contains(frontTeeth) || !cur.cogs.contains(rearTeeth)) {
        setError(QStringLiteral("Choose a chainring and a cog of your bike"));
        return false;
    }
    if (frontTeeth == cur.referenceFrontTeeth && rearTeeth == cur.referenceRearTeeth && !cur.referenceCustom)
        return true;
    GearSetup s = cur;
    s.referenceFrontTeeth = frontTeeth;
    s.referenceRearTeeth = rearTeeth;
    s.referenceCustom = false;
    m_gearing.setSetup(s);
    setupChanged(false);
    return true;
}

bool VirtualGears::setReferenceGearAny(int frontTeeth, int rearTeeth)
{
    const GearSetup &cur = setup();
    if (cur.chainrings.contains(frontTeeth) && cur.cogs.contains(rearTeeth))
        return setReferenceGear(frontTeeth, rearTeeth);
    if (frontTeeth < GearSetup::kMinReferenceRing || frontTeeth > GearSetup::kMaxReferenceRing
        || rearTeeth < GearSetup::kMinReferenceCog || rearTeeth > GearSetup::kMaxReferenceCog) {
        setError(QStringLiteral("Chainring %1–%2 and cog %3–%4 teeth")
                     .arg(GearSetup::kMinReferenceRing).arg(GearSetup::kMaxReferenceRing)
                     .arg(GearSetup::kMinReferenceCog).arg(GearSetup::kMaxReferenceCog));
        return false;
    }
    if (frontTeeth == cur.referenceFrontTeeth && rearTeeth == cur.referenceRearTeeth && cur.referenceCustom)
        return true;
    GearSetup s = cur;
    s.referenceFrontTeeth = frontTeeth;
    s.referenceRearTeeth = rearTeeth;
    s.referenceCustom = true;
    m_gearing.setSetup(s);
    setupChanged(false);
    return true;
}

bool VirtualGears::setWheelCircumference(int mm)
{
    if (mm < GearSetup::kMinCircumferenceMm || mm > GearSetup::kMaxCircumferenceMm) {
        setError(QStringLiteral("Wheel circumference must be %1–%2 mm").arg(GearSetup::kMinCircumferenceMm)
                     .arg(GearSetup::kMaxCircumferenceMm));
        return false;
    }
    if (mm == setup().circumferenceMm)
        return true;
    GearSetup s = setup();
    s.circumferenceMm = mm;
    m_gearing.setSetup(s);
    setupChanged(true);   // the virtual speed changes
    return true;
}

void VirtualGears::resetGearSetup()
{
    m_gearing = Gearing::zwift(GearSetup::defaults(), m_gearing.isZwift() ? m_gearing.zwiftGear() : zwiftgears::kStartGear);
    setupChanged(true);
}

QVariantList VirtualGears::parseTeeth(const QString &text) const
{
    QVariantList out;
    for (int v : GearSetup::parseTeethList(text))
        out.append(v);
    return out;
}

QVariantList VirtualGears::previewGears(const QVariantList &chainrings, const QVariantList &cogs, int circumferenceMm,
                                        bool seq, int cadenceRpm) const
{
    if (!validateGearSetup(chainrings, cogs, circumferenceMm).isEmpty())
        return {};
    bool ok = false;
    GearSetup s = setup();
    s.chainrings = toIntList(chainrings, &ok);
    s.cogs = toIntList(cogs, &ok);
    s.circumferenceMm = circumferenceMm;
    s.sequential = seq;
    s.frontIndex = s.rearIndex = 0;
    const Gearing g(s);
    const QVector<Gear> list = seq ? g.sequence() : g.allGears();
    const road::Model flat = roadModel(0.0);
    const double cadence = std::max(0, cadenceRpm);
    QVariantList out;
    for (int i = 0; i < list.size(); ++i) {
        const Gear &gear = list.at(i);
        const double v = Gearing::speedFor(cadence, gear.ratio, g.setup().circumferenceMm);
        out.append(QVariantMap { { QStringLiteral("index"), i },
                                 { QStringLiteral("label"), Gearing::labelFor(gear.frontTeeth, gear.rearTeeth) },
                                 { QStringLiteral("ratio"), gear.ratio },
                                 { QStringLiteral("front"), gear.front },
                                 { QStringLiteral("rear"), gear.rear },
                                 { QStringLiteral("frontTeeth"), gear.frontTeeth },
                                 { QStringLiteral("rearTeeth"), gear.rearTeeth },
                                 { QStringLiteral("speedKmh"), std::round(v * 36.0) / 10.0 },
                                 { QStringLiteral("watts"), std::max(0, int(std::lround(road::powerForSpeed(flat, v)))) } });
    }
    return out;
}

} // namespace watt
