#include "VirtualGears.h"

#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvSession.h"
#include "SensorHub.h"

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
const QString kSettingEnabled = QStringLiteral("gears/enabled");

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

VirtualGears::VirtualGears(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &VirtualGears::saveNow);
    connect(this, &VirtualGears::gearSetupChanged, this, &VirtualGears::physicsChanged);
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
    m_enabled = m_db->setting(kSettingEnabled, false).toBool();
    load();
}

VirtualGears::~VirtualGears()
{
    saveNow();
}

void VirtualGears::setHub(SensorHub *hub)
{
    m_hub = hub;
    syncHub();
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
    m_gearing = Gearing(GearSetup::fromJsonText(json));
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

void VirtualGears::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    m_db->setSetting(kSettingEnabled, on);
    emit enabledChanged();
}

void VirtualGears::setClickConnected(bool connected)
{
    if (m_clickConnected == connected)
        return;
    m_clickConnected = connected;
    if (connected) {
        // recognised: ride with the saved bike, shifting virtually
        reload();
        setEnabled(true);
    }
    emit clickConnectedChanged();
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
    if (!m_enabled) {
        setEnabled(true);
        emit notice(QStringLiteral("Zwift Click detected – using virtual gears"));
    }
    if (m_rlv && (m_rlv->active() || m_rlv->waiting())) {
        m_rlv->controllerShift(direction);
        return;
    }
    if (m_freeRide && (m_freeRide->active() || m_freeRide->waiting())) {
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

road::Model VirtualGears::roadModel(double gradePercent, double riderKg, double heightCm) const
{
    if (!(riderKg > 0))
        riderKg = m_profiles->current().weightKg;
    if (!(riderKg > 0))
        riderKg = road::kDefaultRiderKg;
    riderKg = std::max(30.0, riderKg);
    if (heightCm < 0)
        heightCm = m_profiles->current().heightCm;
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
    const double w = m_profiles->current().weightKg;
    return w > 0 ? w : road::kDefaultRiderKg;
}

double VirtualGears::autoCda() const
{
    return road::autoCda(riderWeightKg(), m_profiles->current().heightCm);
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

QVariantList VirtualGears::gears() const
{
    QVariantList out;
    const QVector<Gear> list = setup().sequential ? m_gearing.sequence() : m_gearing.allGears();
    for (int i = 0; i < list.size(); ++i) {
        const Gear &g = list.at(i);
        out.append(QVariantMap { { QStringLiteral("index"), i },
                                 { QStringLiteral("label"), Gearing::labelFor(g.frontTeeth, g.rearTeeth) },
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
    scheduleSave();
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
    if (frontTeeth == cur.referenceFrontTeeth && rearTeeth == cur.referenceRearTeeth)
        return true;
    GearSetup s = cur;
    s.referenceFrontTeeth = frontTeeth;
    s.referenceRearTeeth = rearTeeth;
    m_gearing.setSetup(s);
    setupChanged(false);
    return true;
}

void VirtualGears::resetGearSetup()
{
    m_gearing = Gearing(GearSetup::defaults());
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
