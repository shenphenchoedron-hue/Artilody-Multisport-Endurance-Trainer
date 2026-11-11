#include "RiderAvatar.h"

#include "Route3DScene.h"
#include "Rowing3DItem.h"

#include <QColor>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QWaitCondition>

#include <atomic>
#include <map>

namespace watt {

using route3d::GlbModel;

namespace {

// (the male runner's model is very broad and muscular across the shoulders:
// narrower, and its shoulders / arms / upper chest slimmer)
constexpr float kMaleRunnerShoulders = .88f;
constexpr float kMaleRunnerSlim = .25f;
// (both runners are broader across the hips than the classic one whose
// motion they run with: the legs turned in a little, the feet under the body)
constexpr float kMaleRunnerLegsIn = 3.5f, kFemaleRunnerLegsIn = 4.f;
// (the female rower's model has a very long, thin neck: twice the classic rower's length)
constexpr float kFemaleRowerNeck = .65f, kFemaleRowerNeckWidth = 1.35f;

struct ModelInfo
{
    RiderAvatar::Kind kind;
    const char *id;
    const char *name;
    const char *resource;
    const char *env;        // a file to try instead (testing a new model)
    float shoulders = 1.f;  // < 1: narrower shoulders (GlbModel::setShoulderWidth())
    float slim = 0.f;       // > 0: a slimmer upper body (GlbModel::slimUpperBody())
    float legsIn = 0.f;     // degrees: the legs turned in (GlbModel::setLegsIn())
    float neck = 1.f;       // < 1: a shorter neck (GlbModel::setNeckLength())
    float neckWidth = 1.f;  // > 1: a thicker neck (GlbModel::setNeckWidth())
};
const ModelInfo kModels[] = {
    { RiderAvatar::Cyclist, "male", "Male cyclist", ":/models/rider_male.glb", "WATT_RIDER_MALE_GLB" },
    { RiderAvatar::Cyclist, "female", "Female cyclist", ":/models/rider_female.glb", "WATT_RIDER_FEMALE_GLB" },
    { RiderAvatar::Runner, "male", "Male runner", ":/models/runner_male.glb", "WATT_RUNNER_MALE_GLB", kMaleRunnerShoulders,
      kMaleRunnerSlim, kMaleRunnerLegsIn },
    { RiderAvatar::Runner, "female", "Female runner", ":/models/runner_female.glb", "WATT_RUNNER_FEMALE_GLB", 1.f, 0.f,
      kFemaleRunnerLegsIn },
    { RiderAvatar::Rower, "male", "Male rower", ":/models/rower_male.glb", "WATT_ROWER_MALE_GLB" },
    { RiderAvatar::Rower, "female", "Female rower", ":/models/rower_female.glb", "WATT_ROWER_FEMALE_GLB", 1.f, 0.f, 0.f,
      kFemaleRowerNeck, kFemaleRowerNeckWidth },
};

QString pathOf(const ModelInfo &m)
{
    const QString env = qEnvironmentVariable(m.env);
    return env.isEmpty() ? QString::fromLatin1(m.resource) : env;
}

const ModelInfo *info(RiderAvatar::Kind kind, const QString &id)
{
    for (const ModelInfo &m : kModels)
        if (m.kind == kind && id == QLatin1String(m.id))
            return &m;
    return nullptr;
}

std::shared_ptr<const GlbModel> load(const ModelInfo &m)
{
    const bool runner = m.kind == RiderAvatar::Runner, rower = m.kind == RiderAvatar::Rower;
    const QString path = pathOf(m);
    if (!QFile::exists(path))
        return {};
    auto g = std::make_shared<GlbModel>();
    g->setLegsIn(m.legsIn);
    g->setNeckLength(m.neck);
    g->setNeckWidth(m.neckWidth);
    QString error;
    // a runner moves as the classic runner (its motion looks right; a Mixamo
    // clip of its own may not), moved onto its skeleton
    const auto motion = runner ? runnerModel() : nullptr;
    const auto outfit = runner ? GlbModel::Outfit::RunnerAvatar : rower ? GlbModel::Outfit::RowerAvatar : GlbModel::Outfit::Avatar;
    if (!g->loadFile(path, &error, outfit, motion.get())) {
        qWarning("Route3D: avatar %s (%s) not used: %s", m.name, qPrintable(path), qPrintable(error));
        return {};
    }
    if (m.shoulders != 1.f)
        g->setShoulderWidth(m.shoulders);
    if (m.slim > 0.f)
        g->slimUpperBody(m.slim);
    if (!runner && !g->canRide()) {
        qWarning("Route3D: avatar %s (%s) lacks the joints needed to pose it (Mixamo skeleton) - not used", m.name,
                 qPrintable(path));
        return {};
    }
    if (!g->textured())
        qWarning("Route3D: avatar %s (%s) has no usable base colour texture - coloured by the app", m.name,
                 qPrintable(path));
    return g;
}

// A runner walking: the running figure with the classic walker's motion -
// a copy of it, its animation replaced (no second load)
std::shared_ptr<const GlbModel> loadWalking(const ModelInfo &m, const std::shared_ptr<const GlbModel> &running)
{
    const auto walker = walkerModel();
    if (!running || !walker || !running->retargeted())
        return {};
    return running->withMotion(*walker);
}

// The loaded figures, shared by every view (any thread): a slot per model
// and walking / not, loaded once on the thread pool.
struct Slot
{
    std::shared_ptr<const GlbModel> figure;
    bool done = false, started = false;
    std::vector<std::function<void()>> waiting;
};
QMutex g_mutex;
QWaitCondition g_loaded;
std::map<std::pair<const ModelInfo *, bool>, Slot> g_slots;

void finish(const ModelInfo *m, bool walking, std::shared_ptr<const GlbModel> figure)
{
    std::vector<std::function<void()>> callbacks;
    {
        QMutexLocker lock(&g_mutex);
        Slot &s = g_slots[{ m, walking }];
        s.figure = std::move(figure);
        s.done = true;
        callbacks.swap(s.waiting);
        g_loaded.wakeAll();
    }
    for (auto &f : callbacks)
        f();
}

void loadSlot(const ModelInfo *m, bool walking);

// (g_mutex held) starts the slot's load if it has not started
void start(const ModelInfo *m, bool walking)
{
    Slot &s = g_slots[{ m, walking }];
    if (s.started)
        return;
    s.started = true;
    QThreadPool::globalInstance()->start([m, walking] { loadSlot(m, walking); });
}

void loadSlot(const ModelInfo *m, bool walking)
{
    if (!walking) {
        finish(m, false, load(*m));
        return;
    }
    // (the walking figure is made from the running one: wait for it)
    std::shared_ptr<const GlbModel> running;
    {
        QMutexLocker lock(&g_mutex);
        start(m, false);
        Slot &r = g_slots[{ m, false }];
        while (!r.done)
            g_loaded.wait(&g_mutex);
        running = r.figure;
    }
    finish(m, true, loadWalking(*m, running));
}

std::shared_ptr<const GlbModel> get(const ModelInfo *m, bool walking, bool wait, bool *pending)
{
    if (pending)
        *pending = false;
    if (!m)
        return {};
    QMutexLocker lock(&g_mutex);
    start(m, walking);
    Slot &s = g_slots[{ m, walking }];
    if (wait)
        while (!s.done)
            g_loaded.wait(&g_mutex);
    if (pending)
        *pending = !s.done;
    return s.done ? s.figure : nullptr;
}

// the figure chosen - or, when its model file is not there, another of its
// kind that is (the male, then the female); null: none at all
const ModelInfo *chosen(RiderAvatar::Kind kind, const QString &model)
{
    if (const ModelInfo *m = info(kind, model); m && QFile::exists(pathOf(*m)))
        return m;
    for (const ModelInfo &m : kModels)
        if (m.kind == kind && QFile::exists(pathOf(m)))
            return &m;
    return nullptr;
}

} // namespace

const char *RiderAvatar::garmentKey(GlbModel::Garment g)
{
    switch (g) {
    case GlbModel::Jersey: return "jersey";
    case GlbModel::Shorts: return "shorts";
    case GlbModel::Helmet: return "helmet";
    case GlbModel::Shoes: return "shoes";
    case GlbModel::Hair: return "hair";
    default: return "";
    }
}

RiderAvatar RiderAvatar::fromVariant(const QVariantMap &values, Kind kind)
{
    RiderAvatar a;
    a.kind = kind;
    const QString model = values.value(QStringLiteral("model")).toString();
    if (info(kind, model))
        a.model = model;   // (anything else - an old "classic" - the male figure)
    for (int g = GlbModel::Jersey; g < GlbModel::GarmentCount; ++g) {
        const QColor c(values.value(QLatin1String(garmentKey(GlbModel::Garment(g)))).toString());
        if (c.isValid())
            a.colors.color[g] = QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF()));
    }
    const QColor bike(values.value(QStringLiteral("bike")).toString());
    if (bike.isValid() && kind == Cyclist)
        a.bikeColor = QVector3D(float(bike.redF()), float(bike.greenF()), float(bike.blueF()));
    const QColor boat(values.value(QStringLiteral("boat")).toString());
    if (boat.isValid() && kind == Rower)
        a.boatColor = QVector3D(float(boat.redF()), float(boat.greenF()), float(boat.blueF()));
    return a;
}

bool RiderAvatar::operator==(const RiderAvatar &o) const
{
    if (kind != o.kind || model != o.model || bikeColor != o.bikeColor || boatColor != o.boatColor)
        return false;
    for (int g = 0; g < GlbModel::GarmentCount; ++g)
        if (colors.color[g] != o.colors.color[g])
            return false;
    return true;
}

std::shared_ptr<const GlbModel> RiderAvatar::figure() const
{
    return get(chosen(kind, model), false, true, nullptr);
}

std::shared_ptr<const GlbModel> RiderAvatar::figureIfReady(bool *pending) const
{
    return get(chosen(kind, model), false, false, pending);
}

std::shared_ptr<const GlbModel> RiderAvatar::walkingFigure() const
{
    if (kind != Runner)
        return {};
    return get(chosen(kind, model), true, true, nullptr);
}

std::shared_ptr<const GlbModel> RiderAvatar::walkingFigureIfReady(bool *pending) const
{
    if (pending)
        *pending = false;
    if (kind != Runner)
        return {};
    return get(chosen(kind, model), true, false, pending);
}

void RiderAvatar::preload(std::function<void()> done) const
{
    const ModelInfo *m = chosen(kind, model);
    if (!m) {
        if (done)
            done();
        return;
    }
    const bool walking = kind == Runner;
    // done: when the figure (and the walking one) are loaded
    auto remaining = std::make_shared<std::atomic<int>>(walking ? 2 : 1);
    auto one = [remaining, done] {
        if (--*remaining == 0 && done)
            done();
    };
    std::vector<std::function<void()>> now;
    {
        QMutexLocker lock(&g_mutex);
        for (bool w : { false, true }) {
            if (w && !walking)
                continue;
            start(m, w);
            Slot &s = g_slots[{ m, w }];
            if (s.done)
                now.push_back(one);
            else
                s.waiting.push_back(one);
        }
    }
    for (auto &f : now)
        f();
}

std::vector<QVector3D> RiderAvatar::figureColors(const GlbModel &figure) const
{
    if (!figure.textured())
        return {};
    return figure.colorsFor(colors);
}

QVariantList RiderAvatar::models(Kind kind)
{
    QVariantList list;
    for (const ModelInfo &m : kModels) {
        if (m.kind != kind)
            continue;
        if (m.resource && !QFile::exists(pathOf(m)))
            continue;
        list.append(QVariantMap { { QStringLiteral("id"), QLatin1String(m.id) }, { QStringLiteral("name"), QLatin1String(m.name) } });
    }
    return list;
}

} // namespace watt
