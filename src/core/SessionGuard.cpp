#include "SessionGuard.h"

#include "FreeRideSession.h"
#include "RlvSession.h"
#include "RowingSession.h"
#include "RunSession.h"
#include "TrainingSession.h"
#include "VirtualGears.h"

#include <cmath>

namespace watt {

namespace {
const QString kWorkout = QStringLiteral("workout");
const QString kFreeRide = QStringLiteral("freeride");
const QString kVideo = QStringLiteral("video");
const QString kRowing = QStringLiteral("rowing");
const QString kGpx = QStringLiteral("gpx");
const QString kRun = QStringLiteral("run");
} // namespace

SessionGuard::SessionGuard(TrainingSession *training, FreeRideSession *freeRide, RlvSession *rlv,
                           RowingSession *rowing, VirtualGears *gears, QObject *parent)
    : QObject(parent)
    , m_training(training)
    , m_freeRide(freeRide)
    , m_rlv(rlv)
    , m_rowing(rowing)
    , m_gears(gears)
    , m_debug(qEnvironmentVariableIntValue("WATT_DEBUG_RLVUI") > 0)
{
    wire();
    if (m_training)
        connect(m_training, &TrainingSession::stateChanged, this, &SessionGuard::onSessionChanged);
    if (m_freeRide)
        connect(m_freeRide, &FreeRideSession::stateChanged, this, &SessionGuard::onSessionChanged);
    if (m_rlv)
        connect(m_rlv, &RlvSession::stateChanged, this, &SessionGuard::onSessionChanged);
    if (m_rowing)
        connect(m_rowing, &RowingSession::stateChanged, this, &SessionGuard::onSessionChanged);
    if (m_gears)
        connect(m_gears, &VirtualGears::activePageChanged, this, &SessionGuard::onPageChanged);
}

SessionGuard::~SessionGuard()
{
    unwireRun();
    unwireGpx();
    unwire();
}

void SessionGuard::setGpxSession(RlvSession *gpx)
{
    if (gpx == m_gpx)
        return;
    if (m_gpx) {
        unwireGpx();
        disconnect(m_gpx, nullptr, this, nullptr);
    }
    m_gpx = gpx;
    if (m_gpx) {
        wireGpx();
        connect(m_gpx, &RlvSession::stateChanged, this, &SessionGuard::onSessionChanged);
    }
    onSessionChanged();
}

void SessionGuard::setRunSession(RunSession *run)
{
    if (run == m_run)
        return;
    if (m_run) {
        unwireRun();
        disconnect(m_run, nullptr, this, nullptr);
    }
    m_run = run;
    if (m_run) {
        wireRun();
        connect(m_run, &RunSession::stateChanged, this, &SessionGuard::onSessionChanged);
    }
    onSessionChanged();
}

// The Treadmill Run against every other session: in progress blocks, an
// empty paused one is given up by the other's start.
void SessionGuard::wireRun()
{
    RunSession *run = m_run;
    if (!run)
        return;
    const QString reason = QStringLiteral("A treadmill run is in progress. Finish it first.");
    if (TrainingSession *t = m_training) {
        run->setStartBlocker(kWorkout, [t] { return t->inProgress(); },
                             QStringLiteral("A workout is in progress on Watt Training. Finish it before running."));
        t->setStartBlocker(kRun, [run] { return run->inProgress(); });
        t->setAbandonCanceller(kRun, [run] { run->abandon(); });
    }
    if (FreeRideSession *fr = m_freeRide) {
        run->setStartBlocker(kFreeRide, [fr] { return fr->inProgress(); },
                             QStringLiteral("A free ride is in progress. Finish it before running."));
        run->setAbandonCanceller(kFreeRide, [fr] { fr->abandon(); });
        fr->setStartBlocker(kRun, [run] { return run->inProgress(); });
        fr->setAbandonCanceller(kRun, [run] { run->abandon(); });
    }
    for (RlvSession *ride : { m_rlv, m_gpx }) {
        if (!ride)
            continue;
        const QString key = ride == m_gpx ? kGpx : kVideo;
        run->setStartBlocker(key, [ride] { return ride->inProgress(); },
                             ride == m_gpx ? QStringLiteral("A GPX ride is in progress. Finish it before running.")
                                           : QStringLiteral("A video ride is in progress. Finish it before running."));
        run->setAbandonCanceller(key, [ride] { ride->abandon(); });
        ride->setStartBlocker(kRun, [run] { return run->inProgress(); });
        ride->setAbandonCanceller(kRun, [run] { run->abandon(); });
    }
    if (RowingSession *row = m_rowing) {
        run->setStartBlocker(kRowing, [row] { return row->inProgress(); },
                             QStringLiteral("A row is in progress. Finish it before running."));
        run->setAbandonCanceller(kRowing, [row] { row->abandon(); });
        row->setStartBlocker(kRun, [run] { return run->inProgress(); }, reason);
        row->setAbandonCanceller(kRun, [run] { run->abandon(); });
    }
}

void SessionGuard::unwireRun()
{
    if (!m_run)
        return;
    for (const QString &k : { kWorkout, kFreeRide, kVideo, kGpx, kRowing }) {
        m_run->setStartBlocker(k, {});
        m_run->setAbandonCanceller(k, {});
    }
    if (m_training) {
        m_training->setStartBlocker(kRun, {});
        m_training->setAbandonCanceller(kRun, {});
    }
    if (m_freeRide) {
        m_freeRide->setStartBlocker(kRun, {});
        m_freeRide->setAbandonCanceller(kRun, {});
    }
    for (RlvSession *ride : { m_rlv, m_gpx }) {
        if (ride) {
            ride->setStartBlocker(kRun, {});
            ride->setAbandonCanceller(kRun, {});
        }
    }
    if (m_rowing) {
        m_rowing->setStartBlocker(kRun, {});
        m_rowing->setAbandonCanceller(kRun, {});
    }
}

// The GPX Ride against the free ride, the row and the video ride (the same
// rules as the video ride: in progress blocks, an empty paused one is given up).
void SessionGuard::wireGpx()
{
    RlvSession *gpx = m_gpx;
    if (!gpx)
        return;
    if (RowingSession *row = m_rowing) {
        gpx->setStartBlocker(kRowing, [row] { return row->inProgress(); });
        gpx->setAbandonCanceller(kRowing, [row] { row->abandon(); });
        row->setStartBlocker(kGpx, [gpx] { return gpx->inProgress(); },
                             QStringLiteral("A GPX ride is in progress. Finish it before rowing."));
        row->setAbandonCanceller(kGpx, [gpx] { gpx->abandon(); });
    }
    if (FreeRideSession *fr = m_freeRide) {
        gpx->setStartBlocker(kFreeRide, [fr] { return fr->inProgress(); });
        fr->setStartBlocker(kGpx, [gpx] { return gpx->inProgress(); });
        fr->setAbandonCanceller(kGpx, [gpx] { gpx->abandon(); });
    }
    if (RlvSession *rlv = m_rlv) {
        gpx->setStartBlocker(kVideo, [rlv] { return rlv->inProgress(); });
        gpx->setAbandonCanceller(kVideo, [rlv] { rlv->abandon(); });
        rlv->setStartBlocker(kGpx, [gpx] { return gpx->inProgress(); });
        rlv->setAbandonCanceller(kGpx, [gpx] { gpx->abandon(); });
    }
}

void SessionGuard::unwireGpx()
{
    if (!m_gpx)
        return;
    for (const QString &k : { kRowing, kFreeRide, kVideo }) {
        m_gpx->setStartBlocker(k, {});
        m_gpx->setAbandonCanceller(k, {});
    }
    if (m_rowing) {
        m_rowing->setStartBlocker(kGpx, {});
        m_rowing->setAbandonCanceller(kGpx, {});
    }
    if (m_freeRide) {
        m_freeRide->setStartBlocker(kGpx, {});
        m_freeRide->setAbandonCanceller(kGpx, {});
    }
    if (m_rlv) {
        m_rlv->setStartBlocker(kGpx, {});
        m_rlv->setAbandonCanceller(kGpx, {});
    }
}

// Blockers ("in progress" only) and cancellers ("empty paused" sessions are
// given up) between Free Ride, Video Ride and rowing. The workout's
// (TrainingSession) are set by each session's setTrainingSession().
void SessionGuard::wire()
{
    FreeRideSession *fr = m_freeRide;
    RlvSession *rlv = m_rlv;
    RowingSession *row = m_rowing;
    if (fr && row) {
        fr->setStartBlocker(kRowing, [row] { return row->inProgress(); });
        fr->setAbandonCanceller(kRowing, [row] { row->abandon(); });
        row->setStartBlocker(kFreeRide, [fr] { return fr->inProgress(); },
                             QStringLiteral("A free ride is in progress. Finish it before rowing."));
        row->setAbandonCanceller(kFreeRide, [fr] { fr->abandon(); });
    }
    if (rlv && row) {
        rlv->setStartBlocker(kRowing, [row] { return row->inProgress(); });
        rlv->setAbandonCanceller(kRowing, [row] { row->abandon(); });
        row->setStartBlocker(kVideo, [rlv] { return rlv->inProgress(); },
                             QStringLiteral("A video ride is in progress. Finish it before rowing."));
        row->setAbandonCanceller(kVideo, [rlv] { rlv->abandon(); });
    }
    if (fr && rlv) {
        // (an empty paused free ride is given up by RlvSession::start()
        // itself, see RlvSession::setFreeRideSession())
        rlv->setStartBlocker(kFreeRide, [fr] { return fr->inProgress(); });
        fr->setStartBlocker(kVideo, [rlv] { return rlv->inProgress(); });
        fr->setAbandonCanceller(kVideo, [rlv] { rlv->abandon(); });
    }
}

void SessionGuard::unwire()
{
    if (m_freeRide) {
        m_freeRide->setStartBlocker(kRowing, {});
        m_freeRide->setAbandonCanceller(kRowing, {});
        m_freeRide->setStartBlocker(kVideo, {});
        m_freeRide->setAbandonCanceller(kVideo, {});
    }
    if (m_rlv) {
        m_rlv->setStartBlocker(kRowing, {});
        m_rlv->setAbandonCanceller(kRowing, {});
        m_rlv->setStartBlocker(kFreeRide, {});
    }
    if (m_rowing) {
        m_rowing->setStartBlocker(kFreeRide, {});
        m_rowing->setAbandonCanceller(kFreeRide, {});
        m_rowing->setStartBlocker(kVideo, {});
        m_rowing->setAbandonCanceller(kVideo, {});
    }
}

void SessionGuard::onPageChanged()
{
    onSessionChanged();
}

void SessionGuard::onSessionChanged()
{
    // (each ignores its own changes: active() / its page)
    if (m_rlv)
        m_rlv->otherSessionChanged();
    if (m_gpx)
        m_gpx->otherSessionChanged();
    if (m_freeRide)
        m_freeRide->otherSessionChanged();
    emit changed();
    if (!m_debug)
        return;
    const QString d = describe();
    if (d == m_lastDescription)
        return;
    m_lastDescription = d;
    const QVariantMap b = videoBlocker();
    qInfo("RLVUI sessions: %s | video page blocked by: %s", qPrintable(d),
          b.isEmpty() ? "nothing" : qPrintable(b.value(QStringLiteral("text")).toString()));
}

QVariantMap SessionGuard::blockerFor(const QString &kind) const
{
    auto make = [](const QString &k, const QString &page, const QString &pageTitle, const QString &what,
                   bool paused, int elapsedSec, const QString &text) {
        return QVariantMap { { QStringLiteral("kind"), k },
                             { QStringLiteral("page"), page },
                             { QStringLiteral("pageTitle"), pageTitle },
                             { QStringLiteral("what"), what },
                             { QStringLiteral("paused"), paused },
                             { QStringLiteral("elapsedSec"), elapsedSec },
                             { QStringLiteral("text"), text } };
    };
    if (kind != kWorkout && m_training && m_training->inProgress()) {
        const bool p = m_training->paused();
        return make(kWorkout, QStringLiteral("dashboard"), QStringLiteral("Watt Training"), QStringLiteral("workout"), p,
                    m_training->elapsedSec(),
                    p ? QStringLiteral("A workout is paused on Watt Training.")
                      : QStringLiteral("A workout is running on Watt Training."));
    }
    if (kind != kFreeRide && m_freeRide && m_freeRide->inProgress()) {
        const bool p = m_freeRide->paused();
        return make(kFreeRide, QStringLiteral("freeride"), QStringLiteral("Free Ride"), QStringLiteral("free ride"), p,
                    m_freeRide->elapsedSeconds(),
                    p ? QStringLiteral("A free ride is paused on the Free Ride page.")
                      : QStringLiteral("A free ride is in progress on the Free Ride page."));
    }
    if (kind != kVideo && m_rlv && m_rlv->inProgress()) {
        const bool p = m_rlv->paused();
        return make(kVideo, QStringLiteral("video"), QStringLiteral("Video Ride"), QStringLiteral("video ride"), p,
                    m_rlv->elapsedSeconds(),
                    p ? QStringLiteral("A video ride is paused on the Video Ride page.")
                      : QStringLiteral("A video ride is in progress on the Video Ride page."));
    }
    if (kind != kGpx && m_gpx && m_gpx->inProgress()) {
        const bool p = m_gpx->paused();
        return make(kGpx, QStringLiteral("gpx"), QStringLiteral("GPX Ride"), QStringLiteral("GPX ride"), p,
                    m_gpx->elapsedSeconds(),
                    p ? QStringLiteral("A GPX ride is paused on the GPX Ride page.")
                      : QStringLiteral("A GPX ride is in progress on the GPX Ride page."));
    }
    if (kind != kRun && m_run && m_run->inProgress()) {
        const bool p = m_run->paused();
        return make(kRun, QStringLiteral("treadmill"), QStringLiteral("Treadmill Run"), QStringLiteral("run"), p,
                    int(std::lround(m_run->elapsedSeconds())),
                    p ? QStringLiteral("A treadmill run is paused on the Treadmill Run page.")
                      : QStringLiteral("A treadmill run is in progress on the Treadmill Run page."));
    }
    if (kind != kRowing && m_rowing && m_rowing->inProgress()) {
        const bool p = m_rowing->paused();
        return make(kRowing, QStringLiteral("rowing"), QStringLiteral("Rowing"), QStringLiteral("row"), p,
                    int(std::lround(m_rowing->elapsedSeconds())),
                    p ? QStringLiteral("A row is paused on the Rowing page.")
                      : QStringLiteral("A row is in progress on the Rowing page."));
    }
    return {};
}

qint64 SessionGuard::endSession(const QString &kind, bool save)
{
    qint64 id = -1;
    if (kind == kWorkout && m_training && m_training->active()) {
        if (save)
            id = m_training->stop();
        else
            m_training->discard();
    } else if (kind == kFreeRide && m_freeRide && m_freeRide->active()) {
        id = m_freeRide->finish(save);
    } else if (kind == kVideo && m_rlv && m_rlv->active()) {
        id = m_rlv->finish(save);
    } else if (kind == kGpx && m_gpx && m_gpx->active()) {
        id = m_gpx->finish(save);
    } else if (kind == kRowing && m_rowing && m_rowing->active()) {
        id = m_rowing->finish(save);
    } else if (kind == kRun && m_run && m_run->active()) {
        id = m_run->finish(save);
    } else {
        return -1;
    }
    if (m_debug)
        qInfo("RLVUI endSession(%s, %s) -> ride %lld", qPrintable(kind), save ? "save" : "discard", id);
    emit sessionEnded(kind, id, save);
    return id;
}

QString SessionGuard::describe() const
{
    auto rec = [](bool has, int sec) { return has ? QStringLiteral("(%1 s)").arg(sec) : QStringLiteral("(empty)"); };
    QString w = QStringLiteral("-");
    if (m_training) {
        w = m_training->running() ? QStringLiteral("running")
          : m_training->paused() ? QStringLiteral("paused") + rec(m_training->hasRecording(), m_training->elapsedSec())
          : m_training->state() == TrainingSession::Finished ? QStringLiteral("finished") : QStringLiteral("idle");
    }
    auto st = [&](const QString &s, bool paused, bool has, int sec) { return paused ? s + rec(has, sec) : s; };
    const QString gpx = m_gpx ? st(m_gpx->state(), m_gpx->paused(), m_gpx->hasRecording(), m_gpx->elapsedSeconds())
                              : QStringLiteral("-");
    return QStringLiteral("workout=%1 freeRide=%2 video=%3 rowing=%4 page=%5%6")
        .arg(w,
             m_freeRide ? st(m_freeRide->state(), m_freeRide->paused(), m_freeRide->hasRecording(),
                             m_freeRide->elapsedSeconds())
                        : QStringLiteral("-"),
             m_rlv ? st(m_rlv->state(), m_rlv->paused(), m_rlv->hasRecording(), m_rlv->elapsedSeconds())
                   : QStringLiteral("-"),
             m_rowing ? st(m_rowing->state(), m_rowing->paused(), m_rowing->hasRecording(),
                           int(m_rowing->elapsedSeconds()))
                      : QStringLiteral("-"),
             m_gears ? m_gears->activePage() : QStringLiteral("-"),
             (m_gpx ? QStringLiteral(" gpx=") + gpx : QString())
                 + (m_run ? QStringLiteral(" run=") + st(m_run->state(), m_run->paused(), m_run->hasRecording(),
                                                         int(m_run->elapsedSeconds()))
                          : QString()));
}

} // namespace watt
