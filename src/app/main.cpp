// Artilody Watts Trainer – indoor cycling power training
//
// main() only wires the modular back-end (core / ble / media) together and
// exposes it to the QML user interface. No business logic lives in QML.

#include "CoverImageProvider.h"

#include "Database.h"
#include "DeviceManager.h"
#include "FreeRideSession.h"
#include "HistoryManager.h"
#include "MusicPlayer.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvSession.h"
#include "VirtualGears.h"
#include "RowingRecords.h"
#include "RowingSession.h"
#include "SensorHub.h"
#include "SessionGuard.h"
#include "SimulatedDevices.h"
#include "Track.h"
#include "TrainingSession.h"
#include "WorkoutBuilder.h"
#include "WorkoutLibrary.h"
#include "WorkoutPlanner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSize>
#include <QTimer>
#include <cstdlib>
#include <memory>
#include <QStandardPaths>
#include <QTemporaryDir>

#ifdef Q_OS_MACOS
#include <QByteArray>
#include <QFileInfo>
#include <QStringList>
#include <climits>
#include <cstdio>
#include <mach-o/dyld.h>
#endif

using namespace watt;

namespace {
#ifdef Q_OS_MACOS
// An inherited QT_QPA_PLATFORM (e.g. "offscreen" from a test / CI shell) that
// names a platform plugin the deployed bundle doesn't ship (it ships only
// cocoa) makes QGuiApplication abort with "Could not find the Qt platform
// plugin". In a deployed .app, drop such a value so the app starts with cocoa.
// Screenshot mode needs the requested plugin: returns false with a clear error.
// Builds without Contents/PlugIns/platforms (build tree) are left untouched.
bool sanitizeQpaPlatform()
{
    const QByteArray value = qgetenv("QT_QPA_PLATFORM");
    if (value.trimmed().isEmpty())
        return true;
    char exe[PATH_MAX];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0)
        return true;
    const QString bundleDir = QFileInfo(QString::fromUtf8(exe)).canonicalPath()
        + QStringLiteral("/../PlugIns/platforms");
    if (!QFileInfo(bundleDir).isDir())
        return true;
    QStringList dirs { bundleDir };
    for (const QString &p : qEnvironmentVariable("QT_PLUGIN_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs << p + QStringLiteral("/platforms");
    dirs << qEnvironmentVariable("QT_QPA_PLATFORM_PLUGIN_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    // "name", "name:options" or a ';' separated fallback list
    for (const QByteArray &entry : value.split(';')) {
        const QString name = QString::fromLocal8Bit(entry.split(':').constFirst()).trimmed();
        if (name.isEmpty())
            continue;
        for (const QString &dir : std::as_const(dirs))
            if (QFileInfo::exists(dir + QStringLiteral("/libq") + name + QStringLiteral(".dylib")))
                return true;
    }
    if (!qEnvironmentVariableIsEmpty("WATT_SCREENSHOT_DIR")) {
        std::fprintf(stderr, "Screenshot mode: QT_QPA_PLATFORM=%s is not available in this app bundle "
                             "(%s). Add the platform plugin to the bundle or unset QT_QPA_PLATFORM.\n",
                     value.constData(), qPrintable(QDir::cleanPath(bundleDir)));
        return false;
    }
    std::fprintf(stderr, "Warning: ignoring QT_QPA_PLATFORM=%s (platform plugin not in the app bundle), "
                         "using the default platform\n", value.constData());
    qunsetenv("QT_QPA_PLATFORM");
    return true;
}
#endif

// Screenshot aid: a mouse click (press + release) in the middle of a QML
// item, delivered like a real click (so a disabled or covered item does not
// react). False when the item is hidden or outside the window.
bool clickItem(QQuickWindow *window, QQuickItem *item)
{
    if (!window || !item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
        return false;
    const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    if (p.x() < 0 || p.y() < 0 || p.x() >= window->width() || p.y() >= window->height())
        return false;
    const QPointF g = window->mapToGlobal(p.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, p, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, p, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    return true;
}

// Screenshot aid: "name: visible=1 enabled=1 effective=1" of a QML item.
QString describeItem(QQuickWindow *window, const QString &name)
{
    auto *item = window ? window->findChild<QQuickItem *>(name) : nullptr;
    if (!item)
        return name + QStringLiteral(": -");
    QString s = QStringLiteral("%1: visible=%2 enabled=%3 effective=%4")
                    .arg(name)
                    .arg(item->isVisible() ? 1 : 0)
                    .arg(item->property("enabled").toBool() ? 1 : 0)
                    .arg(item->isEnabled() ? 1 : 0);
    const QString text = item->property("text").toString();
    if (!text.isEmpty())
        s += QStringLiteral(" text=\"%1\"").arg(text);
    return s;
}

// Screenshot aid: click a combo box and report whether its popup opened (then close it).
QString probeComboClick(QQuickWindow *window, const QString &name)
{
    auto *combo = window ? window->findChild<QQuickItem *>(name) : nullptr;
    if (!clickItem(window, combo))
        return name + QStringLiteral(" click: not clickable");
    auto *popup = combo->property("popup").value<QObject *>();
    const bool opened = popup && popup->property("visible").toBool();
    if (opened)
        QMetaObject::invokeMethod(popup, "close");
    return name + (opened ? QStringLiteral(" click: popup opened") : QStringLiteral(" click: NO reaction"));
}
} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    if (!sanitizeQpaPlatform())
        return 1;
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Artilody"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("artilody.app"));
    // NOTE: applicationName/organizationName are intentionally kept at their
    // original values. Database::defaultPath() uses AppDataLocation, which is
    // derived from them; changing them would silently move (i.e. "lose") the
    // user's existing database. Only the user-visible display name changes.
    QGuiApplication::setApplicationName(QStringLiteral("Watt Trainer"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Artilody Watts Trainer"));
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
#ifdef Q_OS_LINUX
    // Matches the installed .desktop file (Wayland app_id -> correct icon).
    QGuiApplication::setDesktopFileName(QStringLiteral("artilody-watts-trainer"));
#endif

    // The UI ships its own look; "Basic" is fully customisable on all platforms.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon-256.png")));

    // --- back-end --------------------------------------------------------
    Database db;
    // Screenshot mode (see below) writes demo data (rides, records, a demo
    // workout): never into the user's real database. Without WATT_DB_PATH it
    // runs on a throw-away database.
    const QString shotDir = qEnvironmentVariable("WATT_SCREENSHOT_DIR");
    std::unique_ptr<QTemporaryDir> shotDbDir;
    QString dbPath = qEnvironmentVariable("WATT_DB_PATH");
    if (dbPath.isEmpty() && !shotDir.isEmpty()) {
        shotDbDir = std::make_unique<QTemporaryDir>();
        if (!shotDbDir->isValid()) {
            qCritical("Screenshot mode: could not create a temporary database (set WATT_DB_PATH)");
            return 1;
        }
        dbPath = shotDbDir->filePath(QStringLiteral("screenshot.sqlite"));
        qInfo("Screenshot mode: using temporary database %s", qPrintable(dbPath));
    }
    if (dbPath.isEmpty())
        dbPath = Database::defaultPath();
    if (!db.open(dbPath)) {
        qCritical("Could not open database %s: %s", qPrintable(dbPath), qPrintable(db.lastError()));
        return 1;
    }
    db.seedDefaults();

    SensorHub hub;
    // Display smoothing of the cycling power (Settings: "Smooth power").
    hub.setPowerSmoothing(db.setting(QStringLiteral("display/powerSmoothing"), true).toBool());
    hub.setPowerSmoothingSeconds(db.setting(QStringLiteral("display/powerSmoothingSec"), 3).toInt());
    QObject::connect(&hub, &SensorHub::powerSmoothingChanged, &db, [&db, &hub] {
        db.setSetting(QStringLiteral("display/powerSmoothing"), hub.powerSmoothing());
        db.setSetting(QStringLiteral("display/powerSmoothingSec"), hub.powerSmoothingSeconds());
    });
    ProfileManager profiles(&db);
    profiles.load();

    WorkoutLibrary library(&db, &profiles);
    library.reload();
    WorkoutBuilder builder(&db, &profiles);
    TrainingSession training(&db, &profiles, &hub);
    HistoryManager history(&db, &profiles);
    history.refresh();
    WorkoutPlanner planner(&db, &profiles);
    RowingRecords rowingRecords(&db, &profiles);

    DeviceManager devices(&db, &hub);
    // WATT_TRAINER_LOG=1 (on in screenshot mode): every resistance command the
    // device layer forwards to the trainer, on stderr.
    if (!shotDir.isEmpty() || qEnvironmentVariableIntValue("WATT_TRAINER_LOG") > 0) {
        QObject::connect(&hub, &SensorHub::targetPowerRequested, &app,
                         [](int w) { qInfo("trainer: set target power %d W (ERG)", w); });
        QObject::connect(&hub, &SensorHub::freeRideRequested, &app, [] { qInfo("trainer: free ride"); });
        QObject::connect(&hub, &SensorHub::simulationRequested, &app, [](double g, double, double crr, double cw) {
            qInfo("trainer: simulation grade %.2f %% Crr %.4f Cw %.2f kg/m", g, crr, cw);
        });
    }
    auto syncPhysiology = [&] {
        devices.setRiderPhysiology(profiles.restHr(), profiles.maxHr(), profiles.ftp());
    };
    QObject::connect(&profiles, &ProfileManager::profileChanged, &devices, syncPhysiology);
    syncPhysiology();

    // Rowing sessions on the active rower; only one session (cycling or
    // rowing) runs at a time. Created before initialize() so an auto-connected
    // rower is picked up.
    RowingSession rowing(&db, &profiles, &hub, &rowingRecords, &planner);
    rowing.setTrainingSession(&training);
    rowing.setRower(devices.rower());
    QObject::connect(&devices, &DeviceManager::rowerChanged, &rowing, &RowingSession::setRower);
    QObject::connect(&rowing, &RowingSession::rideSaved, &history, &HistoryManager::showNewRide);

    // Virtual shifting: one gear setup / current gear for the free ride and
    // the video ride, configured on the Devices page.
    VirtualGears gears(&db, &profiles);
    gears.setHub(&hub);   // rider + bike -> SensorHub::riderMassKg (virtual trainer)
    // Free ride (simulated road / ERG); exclusive with workouts and rowing.
    FreeRideSession freeRide(&db, &profiles, &hub, &gears);
    freeRide.setTrainingSession(&training);
    QObject::connect(&freeRide, &FreeRideSession::rideSaved, &history, &HistoryManager::showNewRide);
    // Real Life Video rides; exclusive with workouts, rowing and free rides.
    // The gearing (setup + current gear) is the shared VirtualGears.
    RlvLibrary videos(&db);
    // Developer aid: WATT_RLV_DIR=<folder> (or WATT_SCREENSHOT_RLV=<route folder>)
    // adds a folder to the video library for this run only (neither the folder
    // nor its routes are saved - RlvLibrary skips them in the "rlv/cache").
    const QString shotRlvPath = shotDir.isEmpty() ? QString() : qEnvironmentVariable("WATT_SCREENSHOT_RLV");
    for (const QString &dir : { qEnvironmentVariable("WATT_RLV_DIR"), shotRlvPath }) {
        if (!dir.isEmpty())
            videos.addTemporaryFolder(dir);
    }
    videos.rescan();   // in a worker thread; the list fills in when it is done
    RlvSession rlv(&db, &profiles, &hub, &gears, &videos);
    rlv.setFreeRideSession(&freeRide);   // an empty paused free ride is given up by the video ride's start
    rlv.setTrainingSession(&training);
    // Only one session at a time, without trapping the rider: a session in
    // PROGRESS (riding, or paused with a recording) blocks the others; one
    // paused before anything was recorded is given up by another session's
    // start. Sessions only start on an explicit Start (button / Space) -
    // pedaling or rowing never starts one. The pages show why their Start is
    // disabled, with the fix (QML "Sessions").
    SessionGuard sessions(&training, &freeRide, &rlv, &rowing, &gears);
    // The rider of a ride must still exist when it is saved.
    profiles.setBusyCheck([&] {
        return training.active() || rowing.active() || freeRide.active() || rlv.active();
    });
    QObject::connect(&rlv, &RlvSession::rideSaved, &history, &HistoryManager::showNewRide);

    // Calendar: Planner.startEntry() prepares the session of a plan entry
    // (workout, rowing plan, free ride, video ride).
    planner.setSessions(&training, &freeRide, &rlv, &videos);
    planner.setRowingStarter([&rowing](qint64 workoutId) { return rowing.setupPlan(workoutId); });
    // A saved Free Ride / Video Ride completes its active plan entry when it
    // qualifies (WorkoutPlanner: >= 50 % of the planned duration / 10 min;
    // route end or >= 90 % of the route).
    QObject::connect(&freeRide, &FreeRideSession::rideSaved, &planner, [&](qint64 rideId) {
        if (const auto ride = db.ride(rideId))
            planner.completeFreeRide(rideId, ride->durationSec, ride->profileId);
    });
    QObject::connect(&rlv, &RlvSession::rideSaved, &planner, [&](qint64 rideId) {
        const double total = rlv.totalDistanceM();
        const double share = total > 0 ? rlv.riddenMeters() / total : 0.0;
        if (const auto ride = db.ride(rideId))
            planner.completeVideoRide(rideId, rlv.routePath(), rlv.completed(), share, ride->profileId);
    });

    // Zwift Click buttons: always routed by VirtualGears::controllerShift()
    // (shift in the road modes, ±10 W in ERG, never the difficulty).
    gears.setSessions(&freeRide, &rlv);
    gears.setBusyCheck([&] { return freeRide.active() || rlv.active(); });
    QObject::connect(&devices, &DeviceManager::controllerShift, &gears, &VirtualGears::controllerShift);
    QObject::connect(&devices, &DeviceManager::devicesChanged, &gears,
                     [&] { gears.setClickConnected(devices.controllerConnected()); });
    // The virtual trainer keeps the bike in the reference gear while the
    // rider shifts virtually (screenshots / demos of both paths).
    auto syncSimGear = [&] { devices.setSimulatedFixedGear(gears.enabled() ? gears.referenceRatio() : 0.0); };
    QObject::connect(&gears, &VirtualGears::enabledChanged, &devices, syncSimGear);
    QObject::connect(&gears, &VirtualGears::gearSetupChanged, &devices, syncSimGear);
    syncSimGear();
    devices.initialize();

    MusicPlayer music(&db);
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList musicFolders {
        QDir(appDir).filePath(QStringLiteral("music")),
        QDir(appDir).filePath(QStringLiteral("../Resources/music")),   // macOS bundle
        QDir(appDir).filePath(QStringLiteral("../share/artilody-watts-trainer/music")), // Linux package
        QDir(appDir).filePath(QStringLiteral("../music")),
        QDir(appDir).filePath(QStringLiteral("../../music")),
        QDir(appDir).filePath(QStringLiteral("../../../../music")),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
    };
    // Screenshot aid: WATT_SCREENSHOT_MUSIC=1 adds a demo playlist with many
    // long-named tracks (plus a few extra playlists) to the screenshot database
    // so the music card and library can be checked with a full list.
    if (!shotDir.isEmpty() && qEnvironmentVariableIntValue("WATT_SCREENSHOT_MUSIC") > 0) {
        QString file;   // a real file, so the player shows no "file not found"
        for (const QString &folder : musicFolders.mid(0, musicFolders.size() - 1)) {
            const QStringList mp3 = QDir(folder).entryList({ QStringLiteral("*.mp3") }, QDir::Files);
            if (!mp3.isEmpty()) {
                file = QDir(folder).filePath(mp3.constFirst());
                break;
            }
        }
        const qint64 id = db.createPlaylist(QStringLiteral("Demo: Very Long Endurance Ride Playlist for Testing"));
        for (int i = 1; i <= 50; ++i) {
            Track t;
            t.path = file.isEmpty() ? QStringLiteral("/demo/track%1.mp3").arg(i) : file;
            t.title = QStringLiteral("Track %1 – An Extremely Long Song Title That Should Be Elided Neatly").arg(i);
            t.artist = QStringLiteral("The Orchestra of Unreasonably Long Artist Names feat. Guest %1").arg(i);
            t.durationMs = (180 + i * 7) * 1000;
            db.addTrack(id, t);
        }
        for (int i = 1; i <= 12; ++i)
            db.createPlaylist(QStringLiteral("Demo playlist %1 with a long name").arg(i));
        db.setSetting(QStringLiteral("music/playlist"), id);
    }
    music.initialize(musicFolders);

    // Keep views in sync with data changes.
    QObject::connect(&builder, &WorkoutBuilder::saved, &library, [&](qint64 id) {
        library.reload();
        library.setSelectedId(id);
    });
    // Calendar entries show workout name/duration/sport: refresh after edits.
    QObject::connect(&builder, &WorkoutBuilder::saved, &planner, &WorkoutPlanner::refresh);
    QObject::connect(&training, &TrainingSession::rideSaved, &history, &HistoryManager::showNewRide);
    // A completed (not stopped early) ride of a planned workout ticks off its
    // plan entry: the one started from the calendar, else today's open entry.
    QObject::connect(&training, &TrainingSession::workoutCompleted, &planner, [&](qint64 rideId, qint64 workoutId) {
        if (workoutId < 0)
            return;
        if (const auto ride = db.ride(rideId))
            planner.completeForRide(workoutId, ride->startedAt.date(), rideId, ride->profileId);
    });
    // Deleting/importing workouts changes plan entries (deleted ones vanish).
    QObject::connect(&library, &WorkoutLibrary::countChanged, &planner, &WorkoutPlanner::refresh);
    // Deleting a ride reopens the plan entry it completed.
    QObject::connect(&history, &HistoryManager::rideDeleted, &planner, &WorkoutPlanner::refresh);

    // Default workout on the Watt Training page: the selected workout when it
    // is a cycling workout, else the first cycling workout. Rowing workouts
    // never load into the cycling session.
    if (const qint64 cyclingId = library.firstWorkoutId(sport::cycling()); cyclingId >= 0)
        training.loadWorkout(cyclingId);

    // --- QML --------------------------------------------------------------
    const char *uri = "Artilody.Backend";
    qmlRegisterSingletonInstance(uri, 1, 0, "Profiles", &profiles);
    qmlRegisterSingletonInstance(uri, 1, 0, "Workouts", &library);
    qmlRegisterSingletonInstance(uri, 1, 0, "Builder", &builder);
    qmlRegisterSingletonInstance(uri, 1, 0, "Training", &training);
    qmlRegisterSingletonInstance(uri, 1, 0, "History", &history);
    qmlRegisterSingletonInstance(uri, 1, 0, "Devices", &devices);
    qmlRegisterSingletonInstance(uri, 1, 0, "Sensors", &hub);
    qmlRegisterSingletonInstance(uri, 1, 0, "Music", &music);
    qmlRegisterSingletonInstance(uri, 1, 0, "Planner", &planner);
    qmlRegisterSingletonInstance(uri, 1, 0, "RowingRecords", &rowingRecords);
    qmlRegisterSingletonInstance(uri, 1, 0, "Rowing", &rowing);
    qmlRegisterSingletonInstance(uri, 1, 0, "FreeRide", &freeRide);
    qmlRegisterSingletonInstance(uri, 1, 0, "Gears", &gears);
    qmlRegisterSingletonInstance(uri, 1, 0, "Videos", &videos);
    qmlRegisterSingletonInstance(uri, 1, 0, "Rlv", &rlv);
    qmlRegisterSingletonInstance(uri, 1, 0, "Sessions", &sessions);
    qmlRegisterUncreatableType<IntervalModel>(uri, 1, 0, "IntervalModel", QStringLiteral("Provided by Builder"));
    qmlRegisterUncreatableType<DeviceListModel>(uri, 1, 0, "DeviceListModel", QStringLiteral("Provided by Devices"));
    qmlRegisterUncreatableType<TrackListModel>(uri, 1, 0, "TrackListModel", QStringLiteral("Provided by Music"));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider(&music));
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    engine.rootContext()->setContextProperty(QStringLiteral("databasePath"), QDir::toNativeSeparators(dbPath));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Artilody", "Main");
#else
    engine.load(QUrl(QStringLiteral("qrc:/Artilody/Main.qml")));
#endif

    // Developer aid: WATT_SCREENSHOT_DIR=<dir> renders every page to PNG and quits
    // (used to compare the UI against docs/mockups). Files are named after the
    // page (dashboard.png, freeride.png, video.png, rowing.png, workouts.png, calendar.png,
    // builder.png, history.png, devices.png) plus page<N>.png in sidebar order.
    //   WATT_SCREENSHOT_STEP_MS        step time (default 1500 ms)
    //   WATT_SCREENSHOT_ROWING=1       live row on the virtual rower instead of
    //                                  the cycling workout
    //   WATT_SCREENSHOT_ROWING_MODE    ghost (default) | solo | plan
    //   WATT_SCREENSHOT_ROWING_SEC     seconds rowed before rowing.png (default 40)
    //   WATT_SCREENSHOT_ROWING_READY=1 only set the row up (state "ready", not started)
    //   WATT_SCREENSHOT_IDLE=1         do not start the cycling workout (Watt Training
    //                                  page ready, "press Start")
    //   WATT_SCREENSHOT_FREERIDE=1     live free ride on the virtual trainer instead of
    //                                  the cycling workout (freeride.png taken last)
    //   WATT_SCREENSHOT_FREERIDE_MODE  ride (default) | erg (ERG target 200 W) | gears
    //                                  (= ride with virtual gears on)
    //   WATT_SCREENSHOT_FREERIDE_SEC   seconds ridden before freeride.png (default 40)
    //   WATT_SCREENSHOT_FREERIDE_FINISH=1  finish the ride (not saved) before the capture
    //   WATT_SCREENSHOT_FREERIDE_IDLE=1    do not start the free ride (idle page)
    //   WATT_SCREENSHOT_SELECT=erg,ride,…  on the live page (Free Ride / Video Ride), one
    //                                  click per step on the page's mode selector: emits the
    //                                  SegmentedControl's activated(value) signal, i.e. the
    //                                  same QML handler a mouse click runs
    //   WATT_TRAINER_LOG=1             log every trainer command (target power, simulation,
    //                                  free ride) to stderr; on in screenshot mode
    //   WATT_SCREENSHOT_RLV=<route>    live video ride (folder, .xml or .gpx) on the virtual
    //                                  trainer instead of the cycling workout (page "video" last)
    //   WATT_SCREENSHOT_RLV_SEC        seconds ridden before video.png (default 40)
    //   WATT_SCREENSHOT_RLV_IDLE=1     only select the route (preview / poster frame, no ride)
    //   WATT_SCREENSHOT_RLV_START=<m>  start point in metres (default 0)
    //   WATT_SCREENSHOT_RLV_FINISH=1   finish the video ride (not saved) before the capture
    //   WATT_SCREENSHOT_RLV_WORKOUT=<workout name or id>  ride this cycling workout during the
    //                                  video ride, workout ERG on (a demo "Sweet Spot 3×10" is
    //                                  added to the screenshot database when no workout matches)
    //   WATT_SCREENSHOT_RLV_MODE       sim (default) | erg (ERG target 200 W) | gears
    //                                  (= sim with virtual gears on)
    //                                  (WATT_SIM_TRAINER_NO_SIM=1: virtual trainer without
    //                                  gradient simulation -> the fallback note)
    //   WATT_SCREENSHOT_MENU           comma list of menus to capture instead of the
    //                                  pages (menu-<name>.png): calendar, workouts,
    //                                  new, workouts-export, history, profile, builder
    //                                  (music: the music library dialog,
    //                                  gearsetup: the free ride gear setup dialog,
    //                                  settings: the settings dialog,
    //                                  calendar-add / calendar-add-freeride / calendar-add-video:
    //                                  the calendar's Add dialog on that tab,
    //                                  calendar-edit: the Edit dialog of today's first
    //                                  Free Ride / Video Ride)
    //   WATT_SCREENSHOT_GEARS=1        virtual gears on (Free Ride / Video Ride)
    //   WATT_SCREENSHOT_GEAR=34x27     start in this virtual gear
    //   WATT_SCREENSHOT_CLICK=1        a connected virtual Zwift Click (turns virtual
    //                                  gears on); devices.png shows the controllers
    //                                  section with the virtual gears card
    //   WATT_SCREENSHOT_MUSIC=1        seed a long demo playlist (see above)
    //   WATT_SCREENSHOT_SIZE           window size, e.g. 900x560 (default 1584x992)
    const QStringList shotMenus = qEnvironmentVariable("WATT_SCREENSHOT_MENU")
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool shotRowing = !shotDir.isEmpty() && qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING") > 0;
    const bool shotFreeRide = !shotDir.isEmpty() && !shotRowing
                              && qEnvironmentVariableIntValue("WATT_SCREENSHOT_FREERIDE") > 0;
    const bool shotRlv = !shotRlvPath.isEmpty() && !shotRowing && !shotFreeRide;
    const bool shotClick = !shotDir.isEmpty() && qEnvironmentVariableIntValue("WATT_SCREENSHOT_CLICK") > 0;
    QQuickWindow *shotWindow = !shotDir.isEmpty() && !engine.rootObjects().isEmpty()
        ? qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()) : nullptr;
    if (!shotDir.isEmpty() && !shotWindow)
        qWarning("Screenshot mode: no window to capture");
    if (shotWindow) {
        QQuickWindow *window = shotWindow;
        QDir().mkpath(shotDir);
        QSize shotSize(1584, 992);
        const QStringList sz = qEnvironmentVariable("WATT_SCREENSHOT_SIZE").split(QLatin1Char('x'));
        if (sz.size() == 2 && sz.at(0).toInt() > 0 && sz.at(1).toInt() > 0)
            shotSize = QSize(sz.at(0).toInt(), sz.at(1).toInt());
        window->resize(shotSize);
        devices.connectDevice(QStringLiteral("sim:trainer"));
        devices.connectDevice(QStringLiteral("sim:hr"));
        if (shotClick)
            devices.connectSimulatedController();
        if (qEnvironmentVariableIntValue("WATT_SCREENSHOT_GEARS") > 0)
            gears.setEnabled(true);
        // WATT_SCREENSHOT_GEAR=34x27: start in this virtual gear
        const QStringList shotGear = qEnvironmentVariable("WATT_SCREENSHOT_GEAR").split(QLatin1Char('x'));
        if (shotGear.size() == 2)
            gears.setGear(gears.setup().chainrings.indexOf(shotGear.at(0).toInt()),
                          gears.setup().cogs.indexOf(shotGear.at(1).toInt()));
        if (shotRowing) {
            devices.connectDevice(QStringLiteral("sim:rower"));
            const QString mode = qEnvironmentVariable("WATT_SCREENSHOT_ROWING_MODE", QStringLiteral("ghost"));
            if (mode == QLatin1String("plan")) {
                // demo rowing workout (screenshot databases only)
                if (rowing.rowingWorkouts().isEmpty()) {
                    Workout w;
                    w.name = QStringLiteral("Rowing Sweet Spot 3 × 4");
                    w.sport = sport::rowing();
                    w.category = QStringLiteral("Intervals");
                    w.focus = QStringLiteral("Sweet Spot");
                    auto block = [](const QString &name, int sec, double pct) {
                        Interval i;
                        i.name = name;
                        i.durationSec = sec;
                        i.startPercent = i.endPercent = pct;
                        return i;
                    };
                    w.intervals << block(QStringLiteral("Warm-up"), 120, 0.6);
                    for (int r = 0; r < 3; ++r)
                        w.intervals << block(QStringLiteral("Sweet Spot"), 240, 0.9)
                                    << block(QStringLiteral("Easy"), 120, 0.6);
                    w.intervals << block(QStringLiteral("Cool-down"), 180, 0.5);
                    db.saveWorkout(w);
                    library.reload();
                }
                const QVariantList list = rowing.rowingWorkouts();
                if (!list.isEmpty())
                    rowing.setupPlan(list.constFirst().toMap().value(QStringLiteral("workoutId")).toLongLong());
            } else if (mode == QLatin1String("solo")) {
                rowing.setupSolo(1000);
            } else {
                // a steady 4:20 ghost over 1000 m so the race has an opponent
                if (!rowingRecords.best(1000))
                    rowingRecords.saveIfBest(1000, 260.0, QVector<TracePoint> { { 0, 0 }, { 260, 1000 } });
                rowing.setupGhost(1000);
            }
        }
        if (shotFreeRide) {
            const QString mode = qEnvironmentVariable("WATT_SCREENSHOT_FREERIDE_MODE", QStringLiteral("ride"));
            freeRide.setMode(mode);
            if (mode == QLatin1String("gears"))
                gears.setEnabled(true);
            if (mode == QLatin1String("erg"))
                freeRide.setTargetWatts(200);
            else
                freeRide.setGrade(1.5);
        }
        if (shotRlv) {
            const QString mode = qEnvironmentVariable("WATT_SCREENSHOT_RLV_MODE", QStringLiteral("sim"));
            rlv.setMode(mode);
            if (mode == QLatin1String("gears"))
                gears.setEnabled(true);
            if (mode == QLatin1String("erg"))
                rlv.setTargetWatts(200);
        }
        if (shotRlv && !rlv.selectPath(shotRlvPath))
            qWarning("Screenshot mode: could not load the video route %s: %s", qPrintable(shotRlvPath),
                     qPrintable(rlv.lastError()));
        else if (shotRlv && qEnvironmentVariableIntValue("WATT_SCREENSHOT_RLV_START") > 0)
            rlv.seekToDistance(qEnvironmentVariableIntValue("WATT_SCREENSHOT_RLV_START"));
        if (const QString wanted = qEnvironmentVariable("WATT_SCREENSHOT_RLV_WORKOUT").trimmed();
            shotRlv && !wanted.isEmpty()) {
            qint64 workoutId = -1;
            bool isId = false;
            const qint64 asId = wanted.toLongLong(&isId);
            for (const Workout &w : db.workouts()) {
                if (w.isRowing() || w.intervals.isEmpty())
                    continue;
                if ((isId && w.id == asId) || w.name.compare(wanted, Qt::CaseInsensitive) == 0) {
                    workoutId = w.id;
                    break;
                }
            }
            if (workoutId < 0) {
                // demo workout (screenshot databases only)
                Workout w;
                w.name = isId ? QStringLiteral("Sweet Spot 3×10") : wanted;
                w.category = QStringLiteral("Intervals");
                w.focus = QStringLiteral("Sweet Spot");
                auto block = [](const QString &name, int sec, double from, double to) {
                    Interval i;
                    i.name = name;
                    i.durationSec = sec;
                    i.startPercent = from;
                    i.endPercent = to;
                    return i;
                };
                w.intervals << block(QStringLiteral("Warm-up"), 300, 0.5, 0.75);
                for (int r = 0; r < 3; ++r)
                    w.intervals << block(QStringLiteral("Sweet Spot"), 600, 0.9, 0.9)
                                << block(QStringLiteral("Easy"), 180, 0.55, 0.55);
                w.intervals << block(QStringLiteral("Cool-down"), 300, 0.6, 0.4);
                workoutId = db.saveWorkout(w);
                library.reload();
            }
            if (rlv.attachWorkout(workoutId))
                rlv.setWorkoutErg(true);
            else
                qWarning("Screenshot mode: could not attach the workout %s: %s", qPrintable(wanted),
                         qPrintable(rlv.lastError()));
        }

        if (!shotMenus.isEmpty()) {
            // open each menu, grab, close; then quit
            auto *menuTimer = new QTimer(&app);
            auto menuStep = std::make_shared<int>(0);
            QObject::connect(menuTimer, &QTimer::timeout, &app, [=] {
                const int s = (*menuStep)++;
                const int k = s / 2;
                if (k >= shotMenus.size()) {
                    QCoreApplication::quit();
                    return;
                }
                const QString name = shotMenus.at(k).trimmed();
                if (s % 2 == 0) {
                    QMetaObject::invokeMethod(window, "openScreenshotMenu", Q_ARG(QVariant, name));
                    return;
                }
                window->grabWindow().save(QDir(shotDir).filePath(QStringLiteral("menu-%1.png").arg(name)));
                QMetaObject::invokeMethod(window, "closeScreenshotMenu");
            });
            menuTimer->start(qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") > 0
                                 ? qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") : 1200);
        }

        // Rowing (or, for a free ride, the Free Ride) page last, so the
        // session has time to get going.
        const QString livePage = shotFreeRide ? QStringLiteral("freeride")
                                 : shotRlv    ? QStringLiteral("video")
                                              : QStringLiteral("rowing");
        const bool shotLive = shotRowing || shotFreeRide || shotRlv;
        const QStringList names = window->property("pageNames").toStringList();
        QList<int> order;
        for (int i = 0; i < names.size(); ++i)
            if (names.at(i) != livePage)
                order << i;
        if (names.contains(livePage))
            order << names.indexOf(livePage);
        const char *liveSecVar = shotFreeRide ? "WATT_SCREENSHOT_FREERIDE_SEC"
                                 : shotRlv    ? "WATT_SCREENSHOT_RLV_SEC"
                                              : "WATT_SCREENSHOT_ROWING_SEC";
        const int liveSec = qEnvironmentVariableIntValue(liveSecVar) > 0 ? qEnvironmentVariableIntValue(liveSecVar) : 40;

        auto *timer = new QTimer(&app);
        auto step = std::make_shared<int>(0);
        auto clock = std::make_shared<QElapsedTimer>();
        const bool finishRlv = shotRlv && qEnvironmentVariableIntValue("WATT_SCREENSHOT_RLV_FINISH") > 0;
        const bool rlvIdle = shotRlv && qEnvironmentVariableIntValue("WATT_SCREENSHOT_RLV_IDLE") > 0;
        const bool finishFreeRide = shotFreeRide && qEnvironmentVariableIntValue("WATT_SCREENSHOT_FREERIDE_FINISH") > 0;
        const bool freeRideIdle = shotFreeRide && qEnvironmentVariableIntValue("WATT_SCREENSHOT_FREERIDE_IDLE") > 0;
        const bool rowingReady = shotRowing && qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING_READY") > 0;
        const bool workoutIdle = qEnvironmentVariableIntValue("WATT_SCREENSHOT_IDLE") > 0;
        // WATT_SCREENSHOT_BLOCKER=<scenario> (with WATT_SCREENSHOT_RLV and
        // WATT_SCREENSHOT_RLV_IDLE=1) reproduces "Start greyed out on the Video
        // Ride page": something is tested on another page first, then the
        // Video Ride page is opened (last page, as usual):
        //   workout-running / workout-paused / workout-finished: the Watt
        //     Training page's Start, then Pause / Stop
        //   workout-loaded: a workout only loaded on Watt Training (not
        //     started, trainer disconnected); the trainer comes back (and
        //     pedals) on the Video Ride page - nothing may start or block
        //   freeride-paused / freeride-finished: a free ride on the Free Ride page,
        //     then Pause / Finish; freeride-erg: ERG held on the Free Ride page
        // Captures video-blocked.png and probes the page on stderr ("RLVUI": the
        // sessions, each control's enabled state and real clicks on the pickers,
        // the shift button and the difficulty slider).
        // WATT_SCREENSHOT_BLOCKER_FIX=end|discard then clicks the reason card's
        // "End & Save" / "Discard" (twice: confirm), probes again and captures video.png.
        const QString blocker = rlvIdle ? qEnvironmentVariable("WATT_SCREENSHOT_BLOCKER").trimmed() : QString();
        const QString blockerFix = qEnvironmentVariable("WATT_SCREENSHOT_BLOCKER_FIX").trimmed();
        auto blockerPhase = std::make_shared<int>(0);
        auto trainingStateText = [&training] {
            return training.running() ? QStringLiteral("running")
                 : training.paused() ? QStringLiteral("paused")
                 : training.state() == TrainingSession::Finished ? QStringLiteral("finished") : QStringLiteral("idle");
        };
        auto probeVideo = [=, &freeRide, &rlv, &rowing, &gears](const char *when) {
            qInfo("RLVUI probe (%s): page=%s workout=%s freeRide=%s rlv=%s rowing=%s trainer=%d hasRoute=%d",
                  when, qPrintable(gears.activePage()), qPrintable(trainingStateText()), qPrintable(freeRide.state()),
                  qPrintable(rlv.state()), qPrintable(rowing.state()), rlv.trainerConnected() ? 1 : 0, rlv.hasRoute() ? 1 : 0);
            for (const char *n : { "videoStartButton", "videoNoticeText", "videoNoticeGo", "videoNoticeEnd",
                                   "videoNoticeDiscard", "videoRouteCombo", "videoWorkoutCombo", "videoModeSelector",
                                   "videoShiftEasier", "videoShiftHarder", "videoDifficultySlider" })
                qInfo("RLVUI   %s", qPrintable(describeItem(window, QString::fromLatin1(n))));
            qInfo("RLVUI   %s", qPrintable(probeComboClick(window, QStringLiteral("videoRouteCombo"))));
            qInfo("RLVUI   %s", qPrintable(probeComboClick(window, QStringLiteral("videoWorkoutCombo"))));
            if (auto *b = window->findChild<QQuickItem *>(QStringLiteral("videoShiftHarder")); b && b->isVisible()) {
                const int before = gears.gearIndex();
                clickItem(window, b);
                qInfo("RLVUI   videoShiftHarder click: gear %d -> %d (%s)", before, gears.gearIndex(),
                      gears.gearIndex() != before ? "reacted" : "NO reaction");
                if (gears.gearIndex() != before)
                    gears.shiftDown();
            }
            if (auto *sl = window->findChild<QQuickItem *>(QStringLiteral("videoDifficultySlider")); sl && sl->isVisible()) {
                const int before = rlv.difficultyPercent();
                const bool clicked = clickItem(window, sl);
                qInfo("RLVUI   videoDifficultySlider click: %d %% -> %d %% (%s)", before, rlv.difficultyPercent(),
                      !clicked ? "not clickable" : rlv.difficultyPercent() != before ? "reacted" : "NO reaction");
                rlv.setDifficulty(before);
            }
        };
        auto selects = std::make_shared<QStringList>(
            qEnvironmentVariable("WATT_SCREENSHOT_SELECT").split(QLatin1Char(','), Qt::SkipEmptyParts));
        const QString selectorName = shotFreeRide ? QStringLiteral("freeRideModeSelector")
                                                  : QStringLiteral("videoModeSelector");
        clock->start();
        QObject::connect(timer, &QTimer::timeout, &app, [=, &training, &rowing, &freeRide, &rlv, &devices] {
            const int s = (*step)++;
            if (!blocker.isEmpty() && (s == 1 || s == 2)) {
                const bool workout = blocker.startsWith(QLatin1String("workout"));
                if (s == 1) {
                    qInfo("Screenshot mode: blocker scenario %s", qPrintable(blocker));
                    if (workout) {
                        if (blocker == QLatin1String("workout-loaded"))
                            devices.disconnectDevice(QStringLiteral("sim:trainer"));   // loaded only, not started
                        else
                            training.start();   // the Watt Training page's Start button
                    } else {
                        QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, QStringLiteral("freeride")));
                        if (blocker == QLatin1String("freeride-erg")) {
                            freeRide.setMode(QStringLiteral("erg"));
                            freeRide.setTargetWatts(150);
                        } else {
                            freeRide.start();
                        }
                    }
                } else if (blocker == QLatin1String("workout-paused")) {
                    training.pause();
                } else if (blocker == QLatin1String("workout-finished")) {
                    training.stop();
                } else if (blocker == QLatin1String("freeride-paused")) {
                    freeRide.pause();
                } else if (blocker == QLatin1String("freeride-finished")) {
                    freeRide.finish(false);
                }
                qInfo("Screenshot mode: blocker step %d: workout=%s freeRide=%s", s, qPrintable(trainingStateText()),
                      qPrintable(freeRide.state()));
                return;
            }
            // sessions never start by themselves (no auto-start on pedaling /
            // strokes): screenshot mode presses Start explicitly
            if (s == 1) {
                if (shotRowing) {
                    if (!rowingReady)
                        rowing.start();
                } else if (shotFreeRide) {
                    if (!freeRideIdle)
                        freeRide.start();
                } else if (shotRlv) {
                    if (!rlvIdle)
                        rlv.start();
                } else if (!workoutIdle) {
                    training.start();
                }
            }
            if (s < 3)
                return;
            const int k = (s - 3) / 2;
            if (k >= order.size())
                return;
            const int page = order.at(k);
            const QString name = names.value(page, QStringLiteral("page%1").arg(page));
            if ((s - 3) % 2 == 0) {
                QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page));
                if (blocker == QLatin1String("workout-loaded") && name == livePage)
                    devices.connectDevice(QStringLiteral("sim:trainer"));   // pedaling again, on the video page
                if (shotClick && name == QLatin1String("devices"))
                    QMetaObject::invokeMethod(window, "revealControllers");
                return;
            }
            if (shotLive && name == livePage && !selects->isEmpty()) {
                // a click on the page's mode selector (SegmentedControl.activated)
                const QString value = selects->takeFirst().trimmed();
                QObject *selector = window->findChild<QObject *>(selectorName);
                if (!selector)
                    qWarning("Screenshot mode: no %s", qPrintable(selectorName));
                else {
                    qInfo("Screenshot mode: selector %s -> %s", qPrintable(selectorName), qPrintable(value));
                    QMetaObject::invokeMethod(selector, "activated", Q_ARG(QString, value));
                }
                --(*step);
                return;
            }
            if (shotLive && name == livePage && clock->elapsed() < liveSec * 1000) {
                --(*step);   // keep rowing / riding, try again on the next tick
                return;
            }
            if (finishFreeRide && name == livePage && freeRide.active()) {
                freeRide.finish(false);   // summary card; captured on the next tick
                --(*step);
                return;
            }
            if (finishRlv && name == livePage && rlv.active()) {
                rlv.finish(false);   // summary card; captured on the next tick
                --(*step);
                return;
            }
            if (!blocker.isEmpty() && name == livePage && *blockerPhase == 0) {
                probeVideo("after the other page");
                window->grabWindow().save(QDir(shotDir).filePath(name + QStringLiteral("-blocked.png")));
                *blockerPhase = 1;
                if (blockerFix == QLatin1String("end") || blockerFix == QLatin1String("discard")) {
                    const QString btn = blockerFix == QLatin1String("end") ? QStringLiteral("videoNoticeEnd")
                                                                           : QStringLiteral("videoNoticeDiscard");
                    bool ok = clickItem(window, window->findChild<QQuickItem *>(btn));
                    if (ok && blockerFix == QLatin1String("discard"))
                        ok = clickItem(window, window->findChild<QQuickItem *>(btn));   // confirm
                    qInfo("Screenshot mode: clicked %s: %s -> workout=%s freeRide=%s", qPrintable(btn),
                          ok ? "ok" : "NOT CLICKABLE", qPrintable(trainingStateText()), qPrintable(freeRide.state()));
                    --(*step);
                    return;
                }
            } else if (!blocker.isEmpty() && name == livePage && *blockerPhase == 1) {
                probeVideo("after the fix");
                *blockerPhase = 2;
            }
            const QImage img = window->grabWindow();
            img.save(QDir(shotDir).filePath(name + QStringLiteral(".png")));
            img.save(QDir(shotDir).filePath(QStringLiteral("page%1.png").arg(page)));
            if (k + 1 >= order.size())
                QCoreApplication::quit();
        });
        if (shotMenus.isEmpty())
            timer->start(qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") > 0
                             ? qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") : 1500);
    }

    // Developer aid (virtual gears diagnostics):
    //   WATT_DEBUG_GEARS=1          one line per second on stderr: page/session, gear,
    //                               ratio, cadence, virtual speed, resistance path, road
    //                               grade, grade / ERG target sent, trainer speed, the
    //                               virtual trainer's internal state and the watts
    //   WATT_DEBUG_SHIFTS=+3,-6,+3  (screenshot mode only) shift sequence (gears per step), one gear every
    //                               WATT_DEBUG_SHIFT_SEC (default 5) seconds, starting
    //                               after WATT_DEBUG_SHIFT_DELAY_SEC (default 10); through
    //                               the virtual Zwift Click when connected (same route as a
    //                               real Click), else DeviceManager::controllerShift
    if (qEnvironmentVariableIntValue("WATT_DEBUG_GEARS") > 0) {
        auto *dbgTimer = new QTimer(&app);
        auto dbgClock = std::make_shared<QElapsedTimer>();
        dbgClock->start();
        QObject::connect(dbgTimer, &QTimer::timeout, &app, [&, dbgClock] {
            const bool rlvLive = rlv.active();
            const bool frLive = freeRide.active();
            const bool onRlv = rlvLive || (!frLive && gears.activePage() == QLatin1String("video"));
            QString sim = QStringLiteral("-");
            if (auto *t = qobject_cast<SimulatedTrainer *>(devices.trainerDevice())) {
                sim = QStringLiteral("%1 simGrade=%2 gear=%3 fixed=%4 cad=%5 v=%6 P=%7 erg=%8 goal=%9 "
                                     "m=%10kg Cw=%11 ftp=%12 cap=%13")
                          .arg(t->ergActive() ? QStringLiteral("ERG") : t->simulating() ? QStringLiteral("SIM")
                                                                                         : QStringLiteral("FREE"))
                          .arg(t->simulationGrade(), 0, 'f', 2)
                          .arg(t->gearIndex())
                          .arg(t->fixedGearRatio(), 0, 'f', 3)
                          .arg(t->cadenceRpm(), 0, 'f', 1)
                          .arg(t->wheelSpeedMps() * 3.6, 0, 'f', 1)
                          .arg(t->currentPower(), 0, 'f', 0)
                          .arg(t->targetPower())
                          .arg(t->riderGoal(), 0, 'f', 0)
                          .arg(t->massKg(), 0, 'f', 1)
                          .arg(hub.simulationCw(), 0, 'f', 2)
                          .arg(t->riderFtp())
                          .arg(t->riderFtp() > 0 ? t->riderCapacity() : 0.0, 0, 'f', 0);
            }
            const road::Model rm = gears.roadModel(0);
            const QString session = onRlv ? QStringLiteral("rlv:%1/%2 d=%3m grade=%4 eff=%5")
                                                .arg(rlv.state(), rlv.mode())
                                                .arg(rlv.distanceMeters(), 0, 'f', 0)
                                                .arg(rlv.gradePercent(), 0, 'f', 2)
                                                .arg(rlv.effectiveGradePercent(), 0, 'f', 2)
                                          : QStringLiteral("freeride:%1/%2 grade=%3")
                                                .arg(freeRide.state(), freeRide.mode())
                                                .arg(freeRide.gradePercent(), 0, 'f', 2);
            qInfo("GEARS t=%5.1f page=%s %s gear=%s ratio=%.3f factor=%.3f cad=%d vVirt=%.1f path=%s "
                  "road[m=%.1fkg rider=%.1f CdA=%.3f%s Crr=%.4f rho=%.3f eta=%.2f] "
                  "sent[%s grade=%.2f erg=%d Crr=%.4f Cw=%.2f] vTrainer=%.1f | sim[%s] | W=%d disp=%d",
                  dbgClock->elapsed() / 1000.0, qPrintable(gears.activePage()), qPrintable(session),
                  qPrintable(gears.gear().value(QStringLiteral("label")).toString()),
                  gears.gear().value(QStringLiteral("ratio")).toDouble(), gears.gearFactor(), hub.cadence(),
                  onRlv ? rlv.speedKmh() : freeRide.speedKmh(),
                  qPrintable(onRlv ? rlv.resistancePath() : freeRide.resistancePath()), rm.massKg,
                  gears.riderWeightKg(), rm.cda, gears.cdaAuto() ? "(auto)" : "(manual)", rm.crr, rm.airDensity,
                  rm.efficiency, hub.simulationActive() ? "SIM" : hub.ergActive() ? "ERG" : "FREE",
                  hub.requestedGrade(), hub.requestedTarget(), hub.simulationCrr(), hub.simulationCw(), hub.speedKmh(),
                  qPrintable(sim), hub.power(), hub.displayPower());
        });
        dbgTimer->start(1000);
    }
    // (WATT_DEBUG_SHIFTS only in screenshot mode: it shifts the real gears)
    if (const QString seq = shotDir.isEmpty() ? QString() : qEnvironmentVariable("WATT_DEBUG_SHIFTS");
        !seq.isEmpty()) {
        auto shifts = std::make_shared<QList<int>>();
        for (const QString &part : seq.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const int n = part.trimmed().toInt();
            for (int i = 0; i < std::abs(n); ++i)
                shifts->append(n > 0 ? 1 : -1);
        }
        const int every = qEnvironmentVariableIntValue("WATT_DEBUG_SHIFT_SEC") > 0
                              ? qEnvironmentVariableIntValue("WATT_DEBUG_SHIFT_SEC") : 5;
        const int delay = qEnvironmentVariableIsSet("WATT_DEBUG_SHIFT_DELAY_SEC")
                              ? qEnvironmentVariableIntValue("WATT_DEBUG_SHIFT_DELAY_SEC") : 10;
        auto *shiftTimer = new QTimer(&app);
        QObject::connect(shiftTimer, &QTimer::timeout, &app, [&devices, &gears, shifts, shiftTimer] {
            if (shifts->isEmpty()) {
                shiftTimer->stop();
                return;
            }
            const int dir = shifts->takeFirst();
            const bool viaClick = devices.pressSimulatedController(dir);
            if (!viaClick)
                emit devices.controllerShift(dir);
            qInfo("GEARS shift %s via %s -> %s", dir > 0 ? "harder" : "easier",
                  viaClick ? "virtual Click" : "controllerShift",
                  qPrintable(gears.gear().value(QStringLiteral("label")).toString()));
        });
        QTimer::singleShot(delay * 1000, shiftTimer, [shiftTimer, every] { shiftTimer->start(every * 1000); });
    }

    // Never lose a ride on exit - except the demo sessions of screenshot mode.
    // Runs on aboutToQuit (the event loop still runs, so the trainer's
    // "free ride" goes out over Bluetooth) and once more after exec() in case
    // the loop ended without it (no-op then).
    bool sessionsClosed = false;
    auto closeSessions = [&] {
        if (sessionsClosed)
            return;
        sessionsClosed = true;
        if (training.active()) {
            if (shotDir.isEmpty())
                training.stop();
            else
                training.discard();
        }
        if (rowing.active())
            rowing.finish(shotDir.isEmpty());
        if (freeRide.active())
            freeRide.finish(shotDir.isEmpty());
        if (!shotRlvPath.isEmpty())
            qInfo("Screenshot mode: video ride %s, %s, %.0f m of %.0f m, %.1f km/h, video %lld ms, rate %.2f, drift %.2f s",
                  qPrintable(rlv.routeName()), qPrintable(rlv.state()), rlv.distanceMeters(), rlv.totalDistanceM(),
                  rlv.speedKmh(), rlv.videoPosition(), rlv.playbackRate(), rlv.videoDriftSec());
        if (rlv.active())
            rlv.finish(shotDir.isEmpty());
    };
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        closeSessions();
        // Release the trainer (ERG / simulation of a ride, a workout or a page
        // hold) and give the device layer a moment to write it.
        if (hub.trainerConnected()) {
            hub.requestFreeRide();
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 300)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    });

    const int rc = app.exec();
    closeSessions();

    return rc;
}
