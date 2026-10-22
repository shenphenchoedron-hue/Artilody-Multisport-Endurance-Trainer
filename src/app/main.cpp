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
#include "SensorHub.h"
#include "TrainingSession.h"
#include "WorkoutBuilder.h"
#include "WorkoutLibrary.h"

#include <QDir>
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

    DeviceManager devices(&db, &hub);
    auto syncPhysiology = [&] {
        devices.setRiderPhysiology(profiles.restHr(), profiles.maxHr(), profiles.ftp());
    };
    QObject::connect(&profiles, &ProfileManager::profileChanged, &devices, syncPhysiology);
    syncPhysiology();
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

    // Default workout on the dashboard: the selected (first) workout.
    if (library.selectedId() >= 0)
        training.loadWorkout(library.selectedId());

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
    // (used to compare the UI against docs/mockups).
    const QString shotDir = qEnvironmentVariable("WATT_SCREENSHOT_DIR");
    if (!shotDir.isEmpty() && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QDir().mkpath(shotDir);
        window->resize(1584, 992);
        devices.connectDevice(QStringLiteral("sim:trainer"));
        devices.connectDevice(QStringLiteral("sim:hr"));
        auto *timer = new QTimer(&app);
        auto step = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, &app, [=, &training] {
            const int s = (*step)++;
            if (s == 1)
                training.start();
            if (s >= 3) {
                const int page = (s - 3) / 2;
                if ((s - 3) % 2 == 0) {
                    QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page));
                } else {
                    window->grabWindow().save(QDir(shotDir).filePath(QStringLiteral("page%1.png").arg(page)));
                    if (page >= 4)
                        QCoreApplication::quit();
                }
            }
        });
        timer->start(qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") > 0
                         ? qEnvironmentVariableIntValue("WATT_SCREENSHOT_STEP_MS") : 1500);
    }

    const int rc = app.exec();
    if (training.active())
        training.stop();   // never lose a ride on exit
    return rc;
}
