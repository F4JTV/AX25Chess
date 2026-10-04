/*
 * main.cpp - AX25Chess: chess over AX.25 packet radio, Dire Wolf inside.
 *
 * One Qt Quick interface for the desktop and the phone.  The start-up order
 * follows AX25Chat's, where each step was learned on a real phone:
 *
 *  - Dire Wolf looks for its data files in the current directory: they are
 *    compiled in and written to the application data folder, which becomes
 *    the current directory - the same on every platform.
 *  - On Android the USB PTT backend is installed before the modem can
 *    start, the screen is kept on and the system bars hidden (again each
 *    time the application comes back to the front).
 *  - The microphone permission is asked first and the station started only
 *    then; the notification permission after; the foreground service last,
 *    because its type depends on the permissions granted.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AndroidNotify.h"
#include "AndroidPtt.h"
#include "AndroidUi.h"
#include "AppConfig.h"
#include "AppController.h"
#include "BoardView.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml>
#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

namespace {

// Dire Wolf's tables (tocalls.yaml, symbols-new.txt), compiled in, written
// where the core looks for them.  Returns the folder, empty on failure.
QString prepareDataFolder()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("direwolf-data"));
    if (!QDir().mkpath(dir)) return QString();
    for (const QString name : {QStringLiteral("tocalls.yaml"), QStringLiteral("symbols-new.txt")}) {
        const QString target = QDir(dir).filePath(name);
        QFile source(QStringLiteral(":/direwolf-data/") + name);
        if (!source.exists()) continue;
        if (QFileInfo(target).size() == source.size()) continue;
        QFile::remove(target);
        if (source.copy(target)) QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    }
    return dir;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // No organisation name: the settings live in ~/.config/AX25Chess,
    // not ~/.config/AX25Chess/AX25Chess.
    app.setApplicationName(QStringLiteral("AX25Chess"));
    app.setApplicationVersion(QStringLiteral(AX25CHESS_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/ax25chess.png")));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Chess over AX.25 packet radio"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("config"), QStringLiteral("path to config.json"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("modem-conf"), QStringLiteral("use this direwolf.conf for the modem"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("no-modem"), QStringLiteral("do not start the modem at start-up")});
    parser.process(app);

    const QString dataFolder = prepareDataFolder();
    if (!dataFolder.isEmpty()) QDir::setCurrent(dataFolder);

    const QString configPath = parser.isSet(QStringLiteral("config"))
                                   ? QFileInfo(parser.value(QStringLiteral("config"))).absoluteFilePath()
                                   : QString();
    AppConfig config = AppConfig::load(configPath);

    BoardView::loadPieceFont();
    qmlRegisterType<BoardView>("AX25Chess.Board", 1, 0, "BoardView");

#ifdef Q_OS_ANDROID
    // The transmitter is keyed from Java (UsbPtt); the core learns about it
    // before the modem can start.
    androidptt::install();
    androidnotify::install();
    androidui::installDeviceServices();
    androidui::keepScreenOn(config.ui.keepScreenOn);
    androidui::hideSystemBars();
#endif

    AppController controller(config);
    controller.setOverrides(parser.isSet(QStringLiteral("no-modem")),
                            parser.isSet(QStringLiteral("modem-conf"))
                                ? QFileInfo(parser.value(QStringLiteral("modem-conf"))).absoluteFilePath()
                                : QString());
    QQmlApplicationEngine engine;
    controller.setRetranslateHook([&engine] { engine.retranslate(); });
    controller.applyLanguage();
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // The module is compiled in under /qt/qml; the URL form works on every
    // Qt 6, loadFromModule() only from 6.5.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AX25Chess/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

#ifdef Q_OS_ANDROID
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) androidui::hideSystemBars();
    });
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0) && defined(Q_OS_ANDROID)
    // One request at a time: Android shows one dialog, and a second request
    // made while the first is pending is dropped.
    auto afterMicrophone = [&controller] {
        controller.startup();
        androidnotify::requestPermission();
    };
    if (app.checkPermission(QMicrophonePermission()) == Qt::PermissionStatus::Granted) {
        afterMicrophone();
    } else {
        app.requestPermission(QMicrophonePermission(), &controller, [afterMicrophone, &controller](const QPermission &p) {
            if (p.status() != Qt::PermissionStatus::Granted) {
                emit controller.announce(QObject::tr("Microphone"),
                                         QObject::tr("Without the microphone permission the modem cannot receive. "
                                                     "Grant it in the Android settings of AX25Chess."));
            }
            afterMicrophone();
        });
    }
#else
    QTimer::singleShot(200, &controller, [&controller] { controller.startup(); });
#endif
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] { androidui::stopKeepAlive(); });

    // AX25CHESS_SCREENSHOT_DIR=<folder> [AX25CHESS_SCREENSHOT_SIZE=412x860]:
    // render every page into PNG files and quit.  Used with the offscreen
    // platform to document and check the interface without a device.
    const QString shotDir = qEnvironmentVariable("AX25CHESS_SCREENSHOT_DIR");
    if (!shotDir.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList size = qEnvironmentVariable("AX25CHESS_SCREENSHOT_SIZE", QStringLiteral("1240x800")).split('x');
        if (window && size.size() == 2) {
            window->setVisibility(QWindow::Windowed);   // full screen would ignore the size
            window->resize(size.at(0).toInt(), size.at(1).toInt());
        }
        const QString prefix = qEnvironmentVariable("AX25CHESS_SCREENSHOT_PREFIX", QStringLiteral("shot"));
        auto *stepper = new QTimer(&app);
        auto index = std::make_shared<int>(-1);
        QObject::connect(stepper, &QTimer::timeout, &app, [window, shotDir, prefix, index, stepper] {
            if (!window) { QCoreApplication::exit(2); return; }
            const int pages = window->property("wide").toBool() ? 6 : 5;
            if (*index >= 0) window->grabWindow().save(QDir(shotDir).filePath(QStringLiteral("%1-%2.png").arg(prefix).arg(*index)));
            if (++*index >= pages) { stepper->stop(); QCoreApplication::quit(); return; }
            window->setProperty("pageIndex", *index);
        });
        stepper->start(1500);
    }
    return app.exec();
}
