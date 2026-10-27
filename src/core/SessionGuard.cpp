#include "SessionGuard.h"

#include "FreeRideSession.h"
#include "RlvSession.h"
#include "RowingSession.h"
#include "TrainingSession.h"
#include "VirtualGears.h"

#include <cmath>

namespace watt {

namespace {
const QString kWorkout = QStringLiteral("workout");
const QString kFreeRide = QStringLiteral("freeride");
const QString kVideo = QStringLiteral("video");
const QString kRowing = QStringLiteral("rowing");
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
    unwire();
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
    } else if (kind == kRowing && m_rowing && m_rowing->active()) {
        id = m_rowing->finish(save);
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
    return QStringLiteral("workout=%1 freeRide=%2 video=%3 rowing=%4 page=%5")
        .arg(w,
             m_freeRide ? st(m_freeRide->state(), m_freeRide->paused(), m_freeRide->hasRecording(),
                             m_freeRide->elapsedSeconds())
                        : QStringLiteral("-"),
             m_rlv ? st(m_rlv->state(), m_rlv->paused(), m_rlv->hasRecording(), m_rlv->elapsedSeconds())
                   : QStringLiteral("-"),
             m_rowing ? st(m_rowing->state(), m_rowing->paused(), m_rowing->hasRecording(),
                           int(m_rowing->elapsedSeconds()))
                      : QStringLiteral("-"),
             m_gears ? m_gears->activePage() : QStringLiteral("-"));
}

} // namespace watt
