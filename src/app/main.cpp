// Artilody Watts Trainer – indoor cycling power training
//
// main() only wires the modular back-end (core / ble / media) together and
// exposes it to the QML user interface. No business logic lives in QML.

#include "CoverImageProvider.h"

#include "Database.h"
#include "DeviceManager.h"
#include "HistoryManager.h"
#include "MusicPlayer.h"
#include "ProfileManager.h"
#include "RowingRecords.h"
#include "RowingSession.h"
#include "SensorHub.h"
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
#include <QTimer>
#include <memory>
#include <QStandardPaths>

using namespace watt;

int main(int argc, char *argv[])
{
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
    QString dbPath = qEnvironmentVariable("WATT_DB_PATH");
    if (dbPath.isEmpty())
        dbPath = Database::defaultPath();
    if (!db.open(dbPath)) {
        qCritical("Could not open database %s: %s", qPrintable(dbPath), qPrintable(db.lastError()));
        return 1;
    }
    db.seedDefaults();

    SensorHub hub;
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
    QObject::connect(&rowing, &RowingSession::rideSaved, &history, [&](qint64 id) {
        history.refresh();
        history.selectRide(id);
    });
    devices.initialize();

    MusicPlayer music(&db);
    const QString appDir = QCoreApplication::applicationDirPath();
    music.initialize({ QDir(appDir).filePath(QStringLiteral("music")),
                       QDir(appDir).filePath(QStringLiteral("../Resources/music")),   // macOS bundle
                       QDir(appDir).filePath(QStringLiteral("../share/artilody-watts-trainer/music")), // Linux package
                       QDir(appDir).filePath(QStringLiteral("../music")),
                       QDir(appDir).filePath(QStringLiteral("../../music")),
                       QDir(appDir).filePath(QStringLiteral("../../../../music")),
                       QStandardPaths::writableLocation(QStandardPaths::MusicLocation) });

    // Keep views in sync with data changes.
    QObject::connect(&builder, &WorkoutBuilder::saved, &library, [&](qint64 id) {
        library.reload();
        library.setSelectedId(id);
    });
    QObject::connect(&training, &TrainingSession::rideSaved, &history, [&](qint64 id) {
        history.refresh();
        history.selectRide(id);
    });
    // A finished ride of a planned workout ticks off that day's plan entry.
    QObject::connect(&training, &TrainingSession::rideSaved, &planner, [&](qint64 id) {
        if (const auto ride = db.ride(id); ride && ride->workoutId >= 0)
            planner.markCompletedForWorkout(ride->workoutId, ride->startedAt.date(), id);
    });
    // Deleting/importing workouts changes plan entries (deleted ones vanish).
    QObject::connect(&library, &WorkoutLibrary::countChanged, &planner, &WorkoutPlanner::refresh);

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
    // page (dashboard.png, workouts.png, calendar.png, builder.png, rowing.png,
    // history.png, devices.png) plus page<N>.png in sidebar order.
    //   WATT_SCREENSHOT_STEP_MS        step time (default 1500 ms)
    //   WATT_SCREENSHOT_ROWING=1       live row on the virtual rower instead of
    //                                  the cycling workout
    //   WATT_SCREENSHOT_ROWING_MODE    ghost (default) | solo | plan
    //   WATT_SCREENSHOT_ROWING_SEC     seconds rowed before rowing.png (default 40)
    const QString shotDir = qEnvironmentVariable("WATT_SCREENSHOT_DIR");
    const bool shotRowing = !shotDir.isEmpty() && qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING") > 0;
    if (!shotDir.isEmpty() && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QDir().mkpath(shotDir);
        window->resize(1584, 992);
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

        // Rowing page last, so the row has time to get going.
        const QStringList names = window->property("pageNames").toStringList();
        QList<int> order;
        for (int i = 0; i < names.size(); ++i)
            if (names.at(i) != QLatin1String("rowing"))
                order << i;
        if (names.contains(QStringLiteral("rowing")))
            order << names.indexOf(QStringLiteral("rowing"));
        const int rowingSec = qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING_SEC") > 0
                                  ? qEnvironmentVariableIntValue("WATT_SCREENSHOT_ROWING_SEC") : 40;

        auto *timer = new QTimer(&app);
        auto step = std::make_shared<int>(0);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QObject::connect(timer, &QTimer::timeout, &app, [=, &training, &rowing] {
            const int s = (*step)++;
            if (s == 1) {
                if (shotRowing)
                    rowing.start();
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
            if (shotRowing && name == QLatin1String("rowing") && clock->elapsed() < rowingSec * 1000) {
                --(*step);   // keep rowing, try again on the next tick
                return;
            }
            const QImage img = window->grabWindow();
            img.save(QDir(shotDir).filePath(name + QStringLiteral(".png")));
            img.save(QDir(shotDir).filePath(QStringLiteral("page%1.png").arg(page)));
            if (k + 1 >= order.size())
                QCoreApplication::quit();
        });
        timer->start(qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") > 0
                         ? qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") : 1500);
    }

    const int rc = app.exec();
    if (training.active())
        training.stop();   // never lose a ride on exit
    if (rowing.active())
        rowing.finish(!shotRowing);   // screenshot rows are demo data

    return rc;
}
