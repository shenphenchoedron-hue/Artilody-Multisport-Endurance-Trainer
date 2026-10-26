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
#include "RowingRecords.h"
#include "RowingSession.h"
#include "SensorHub.h"
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
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSize>
#include <QTimer>
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

    // Free ride (virtual gearing / ERG); exclusive with workouts and rowing.
    FreeRideSession freeRide(&db, &profiles, &hub);
    freeRide.setTrainingSession(&training);
    freeRide.setStartBlocker(QStringLiteral("rowing"), [&rowing] { return rowing.active(); });
    rowing.setStartBlocker(QStringLiteral("freeRide"), [&freeRide] { return freeRide.active(); },
                           QStringLiteral("A free ride is running. Stop it before rowing."));
    QObject::connect(&freeRide, &FreeRideSession::rideSaved, &history, &HistoryManager::showNewRide);
    // Real Life Video rides; exclusive with workouts, rowing and free rides.
    // The gearing (setup + current gear) is shared with the free ride.
    RlvLibrary videos(&db);
    // Developer aid: WATT_RLV_DIR=<folder> (or WATT_SCREENSHOT_RLV=<route folder>)
    // adds a folder to the video library for this run only (not saved).
    const QString shotRlvPath = shotDir.isEmpty() ? QString() : qEnvironmentVariable("WATT_SCREENSHOT_RLV");
    for (const QString &dir : { qEnvironmentVariable("WATT_RLV_DIR"), shotRlvPath }) {
        if (!dir.isEmpty())
            videos.addTemporaryFolder(dir);
    }
    videos.rescan();
    RlvSession rlv(&db, &profiles, &hub, &videos);
    rlv.setFreeRideSession(&freeRide);
    rlv.setTrainingSession(&training);
    rlv.setStartBlocker(QStringLiteral("freeRide"), [&freeRide] { return freeRide.active(); });
    rlv.setStartBlocker(QStringLiteral("rowing"), [&rowing] { return rowing.active(); });
    freeRide.setStartBlocker(QStringLiteral("rlv"), [&rlv] { return rlv.active() || rlv.waiting(); });
    rowing.setStartBlocker(QStringLiteral("rlv"), [&rlv] { return rlv.active(); },
                           QStringLiteral("A video ride is running. Stop it before rowing."));
    QObject::connect(&rlv, &RlvSession::rideSaved, &history, &HistoryManager::showNewRide);

    // Zwift Click buttons: shift (gears) / ±10 W (ERG) on the free ride, or
    // shift (gears) / ±10 % difficulty (power mode) on a running video ride.
    QObject::connect(&devices, &DeviceManager::controllerShift, &rlv, [&](int direction) {
        if (rlv.active() || rlv.waiting())
            rlv.controllerShift(direction);
        else
            freeRide.controllerShift(direction);
    });
    QObject::connect(&devices, &DeviceManager::devicesChanged, &freeRide,
                     [&] { freeRide.setClickConnected(devices.controllerConnected()); });
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

    // Default workout on the (cycling) dashboard: the selected workout when it
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
    qmlRegisterSingletonInstance(uri, 1, 0, "Videos", &videos);
    qmlRegisterSingletonInstance(uri, 1, 0, "Rlv", &rlv);
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
    // page (dashboard.png, freeride.png, rowing.png, workouts.png, calendar.png,
    // builder.png, history.png, devices.png) plus page<N>.png in sidebar order.
    //   WATT_SCREENSHOT_STEP_MS        step time (default 1500 ms)
    //   WATT_SCREENSHOT_ROWING=1       live row on the virtual rower instead of
    //                                  the cycling workout
    //   WATT_SCREENSHOT_ROWING_MODE    ghost (default) | solo | plan
    //   WATT_SCREENSHOT_ROWING_SEC     seconds rowed before rowing.png (default 40)
    //   WATT_SCREENSHOT_FREERIDE=1     live free ride on the virtual trainer instead of
    //                                  the cycling workout (freeride.png taken last)
    //   WATT_SCREENSHOT_FREERIDE_MODE  gears (default) | erg | free
    //   WATT_SCREENSHOT_FREERIDE_SEC   seconds ridden before freeride.png (default 40)
    //   WATT_SCREENSHOT_FREERIDE_FINISH=1  finish the ride (not saved) before the capture
    //   WATT_SCREENSHOT_RLV=<route>    live video ride (folder, .xml or .gpx) on the virtual
    //                                  trainer instead of the cycling workout (page "rlv" last)
    //   WATT_SCREENSHOT_RLV_SEC        seconds ridden before rlv.png (default 40)
    //   WATT_SCREENSHOT_MENU           comma list of menus to capture instead of the
    //                                  pages (menu-<name>.png): calendar, workouts,
    //                                  new, workouts-export, history, profile, builder
    //                                  (music: the music library dialog,
    //                                  gearsetup: the free ride gear setup dialog,
    //                                  settings: the settings dialog)
    //   WATT_SCREENSHOT_MUSIC=1        seed a long demo playlist (see above)
    //   WATT_SCREENSHOT_SIZE           window size, e.g. 900x560 (default 1584x992)
    const QStringList shotMenus = qEnvironmentVariable("WATT_SCREENSHOT_MENU")
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool shotRowing = !shotDir.isEmpty() && qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING") > 0;
    const bool shotFreeRide = !shotDir.isEmpty() && !shotRowing
                              && qEnvironmentVariableIntValue("WATT_SCREENSHOT_FREERIDE") > 0;
    const bool shotRlv = !shotRlvPath.isEmpty() && !shotRowing && !shotFreeRide;
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
            const QString mode = qEnvironmentVariable("WATT_SCREENSHOT_FREERIDE_MODE", QStringLiteral("gears"));
            freeRide.setMode(mode);
            if (mode == QLatin1String("erg"))
                freeRide.setTargetWatts(200);
            else
                freeRide.setGrade(1.5);
        }
        if (shotRlv && !rlv.selectPath(shotRlvPath))
            qWarning("Screenshot mode: could not load the video route %s: %s", qPrintable(shotRlvPath),
                     qPrintable(rlv.lastError()));

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
                                 : shotRlv    ? QStringLiteral("rlv")
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
        const bool finishFreeRide = shotFreeRide && qEnvironmentVariableIntValue("WATT_SCREENSHOT_FREERIDE_FINISH") > 0;
        clock->start();
        QObject::connect(timer, &QTimer::timeout, &app, [=, &training, &rowing, &freeRide, &rlv] {
            const int s = (*step)++;
            if (s == 1) {
                if (shotRowing)
                    rowing.start();
                else if (shotFreeRide)
                    freeRide.start();
                else if (shotRlv)
                    rlv.start();
                else
                    training.start();
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

    const int rc = app.exec();
    // Never lose a ride on exit - except the demo sessions of screenshot mode.
    if (training.armedOrActive()) {
        if (shotDir.isEmpty())
            training.stop();
        else
            training.discard();
    }
    if (rowing.active())
        rowing.finish(shotDir.isEmpty());
    if (freeRide.active() || freeRide.waiting())
        freeRide.finish(shotDir.isEmpty());
    if (!shotRlvPath.isEmpty())
        qInfo("Screenshot mode: video ride %s, %s, %.0f m of %.0f m, %.1f km/h, video %lld ms, rate %.2f",
              qPrintable(rlv.routeName()), qPrintable(rlv.state()), rlv.distanceMeters(), rlv.totalDistanceM(),
              rlv.speedKmh(), rlv.videoPosition(), rlv.playbackRate());
    if (rlv.active() || rlv.waiting())
        rlv.finish(shotDir.isEmpty());

    return rc;
}
