#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "AppController.h"
#include "DjController.h"
#include "DjMidi.h"
#include "Updater.h"
#include "DjWaveItem.h"
#include "IconItem.h"
#include "Items.h"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    // Interner Name bleibt (Einstellungen, Modelle, Bibliothek); angezeigt wird „LXStudio“
    QGuiApplication::setApplicationName(QStringLiteral("LoggX Studio"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("LXStudio"));
    QGuiApplication::setOrganizationName(QStringLiteral("LoggX"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LOGGX_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/loggx-256.png")));
    // Eigener Stil (app/qml/LoggXStyle), fehlende Elemente kommen aus "Basic".
    QQuickStyle::setStyle(QStringLiteral("LoggXStyle"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));

    // Dunkles Farbschema für alle Standard-Bedienelemente.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x16, 0x18, 0x1d));
    palette.setColor(QPalette::WindowText, QColor(0xd7, 0xdb, 0xe4));
    palette.setColor(QPalette::Base, QColor(0x10, 0x12, 0x16));
    palette.setColor(QPalette::AlternateBase, QColor(0x1b, 0x1e, 0x24));
    palette.setColor(QPalette::Text, QColor(0xd7, 0xdb, 0xe4));
    palette.setColor(QPalette::Button, QColor(0x2a, 0x2e, 0x38));
    palette.setColor(QPalette::ButtonText, QColor(0xd7, 0xdb, 0xe4));
    palette.setColor(QPalette::Highlight, QColor(0x4f, 0x8c, 0xff));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x2e, 0x38));
    palette.setColor(QPalette::ToolTipText, QColor(0xd7, 0xdb, 0xe4));
    palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x91, 0xa0));
    palette.setColor(QPalette::Mid, QColor(0x2d, 0x31, 0x3b));
    palette.setColor(QPalette::Dark, QColor(0x0c, 0x0d, 0x10));
    palette.setColor(QPalette::Light, QColor(0x3a, 0x3f, 0x4c));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6a, 0x70, 0x7d));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6a, 0x70, 0x7d));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6a, 0x70, 0x7d));
    QGuiApplication::setPalette(palette);

    qmlRegisterUncreatableType<AppController>("LoggX", 1, 0, "AppController",
                                              QStringLiteral("Wird von der App bereitgestellt"));
    qmlRegisterType<ViewerItem>("LoggX", 1, 0, "Viewer");
    qmlRegisterType<TimelineItem>("LoggX", 1, 0, "Timeline");
    qmlRegisterType<PianoRollItem>("LoggX", 1, 0, "PianoRoll");
    qmlRegisterType<ScopeItem>("LoggX", 1, 0, "Scope");
    qmlRegisterType<IconItem>("LoggX", 1, 0, "Icon");
    qmlRegisterType<DjWaveItem>("LoggX", 1, 0, "DjWave");
    qmlRegisterUncreatableType<DjController>("LoggX", 1, 0, "DjController", QStringLiteral("nur als dj"));
    qmlRegisterUncreatableType<DjMidi>("LoggX", 1, 0, "DjMidi", QStringLiteral("nur als djmidi"));
    qmlRegisterUncreatableType<Updater>("LoggX", 1, 0, "Updater", QStringLiteral("nur als updater"));

    AppController controller;
    DjController dj(&controller);
    DjMidi djmidi(&dj);
    Updater updater;
    QObject::connect(&updater, &Updater::quitRequested, &app, [] { QTimer::singleShot(400, qApp, &QCoreApplication::quit); });

    // Aufruf: "LoggX Studio" [projekt.loggx] [--music] [--page N] [--tab N]
    //         [--select CLIP] [--seek SEK] [--license] [--screenshot bild.png]
    // --screenshot speichert ein Bild des Fensters und beendet sich (für
    // automatische Prüfungen der Oberfläche).
    QString screenshot;
    int screenshotDelay = 1500;
    bool showLicense = false;
    QUrl mainQml(QStringLiteral("qrc:/qml/Main.qml"));
    bool pageGiven = false;
    bool fileGiven = false;
    int startPage = 1;   // 0 Medien, 1 Schnitt, 2 Farbe, 3 Musik, 4 Export, 6 DJ
    bool showSettings = false;
    int settingsTab = 0;
    int startTab = 0;    // Musik: 0 Noten, 1 Mixer
    const QStringList args = QGuiApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size())
            screenshot = args[++i];
        else if (args[i] == QLatin1String("--screenshot-delay") && i + 1 < args.size())
            screenshotDelay = args[++i].toInt();
        else if (args[i] == QLatin1String("--page") && i + 1 < args.size())
            startPage = qBound(0, args[++i].toInt(), 6), pageGiven = true;
        else if (args[i] == QLatin1String("--music"))
            controller.newMusicProject();
        else if (args[i] == QLatin1String("--main") && i + 1 < args.size())   // Entwicklung: andere Oberfläche laden
            mainQml = QUrl::fromLocalFile(args[++i]);
        else if (args[i] == QLatin1String("--dj-import") && i + 1 < args.size())   // Entwicklung/Bildschirmfotos
            dj.importFiles({QUrl::fromLocalFile(args[++i])});
        else if (args[i] == QLatin1String("--dj-deck") && i + 2 < args.size()) {
            const int deck = args[++i].toInt();
            dj.loadTrack(deck, QFileInfo(args[++i]).absoluteFilePath());
        }
        else if (args[i] == QLatin1String("--license"))
            showLicense = true;
        else if (args[i] == QLatin1String("--settings") && i + 1 < args.size())   // Einstellungen offen (Tab)
            showSettings = true, settingsTab = args[++i].toInt();
        else if (args[i] == QLatin1String("--tab") && i + 1 < args.size())
            startTab = qBound(0, args[++i].toInt(), 1);
        else if (args[i] == QLatin1String("--select") && i + 1 < args.size())
            controller.setSelectedClipId(args[++i]);
        else if (args[i] == QLatin1String("--seek") && i + 1 < args.size())
            controller.seek(args[++i].toDouble());
        else if (!args[i].startsWith(QLatin1Char('-')))
            controller.openProjectFile(args[i]), fileGiven = true;
    }
    // Einstellung „Beim Start“: Startbildschirm, letztes Projekt oder DJ
    if (!pageGiven && !fileGiven && !showSettings) {
        if (controller.startMode() == 2) {
            startPage = 6;
        } else if (controller.startMode() == 1) {
            const QVariantList recent = controller.recentProjects();
            if (!recent.isEmpty()) {
                const QString last = recent.first().toMap().value(QStringLiteral("path")).toString();
                if (QFileInfo::exists(last) && controller.openProjectFile(last)) controller.setShowStart(false);
            }
        }
    }

    if (pageGiven) controller.setShowStart(false);
    if (controller.musicMode() && !pageGiven) startPage = 3;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("dj"), &dj);
    engine.rootContext()->setContextProperty(QStringLiteral("djmidi"), &djmidi);
    engine.rootContext()->setContextProperty(QStringLiteral("updater"), &updater);
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), startPage);
    engine.rootContext()->setContextProperty(QStringLiteral("startTab"), startTab);
    engine.rootContext()->setContextProperty(QStringLiteral("showLicense"), showLicense);
    engine.rootContext()->setContextProperty(QStringLiteral("showSettings"), showSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsTab"), settingsTab);
    engine.load(mainQml);
    if (screenshot.isEmpty()) updater.checkOnStartup();   // nicht bei Tests/Bildschirmfotos
    if (engine.rootObjects().isEmpty()) return 1;

    if (!screenshot.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QTimer::singleShot(screenshotDelay, window, [window, screenshot] {
            const bool ok = window && window->grabWindow().save(screenshot);
            QGuiApplication::exit(ok ? 0 : 1);
        });
    }
    return QGuiApplication::exec();
}
