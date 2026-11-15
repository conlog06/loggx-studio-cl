#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "AppController.h"
#include "IconItem.h"
#include "Items.h"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("LoggX Studio"));
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

    AppController controller;

    // Aufruf: "LoggX Studio" [projekt.loggx] [--page N] [--select CLIP] [--seek SEK]
    //         [--screenshot bild.png]
    // --screenshot speichert ein Bild des Fensters und beendet sich (für
    // automatische Prüfungen der Oberfläche).
    QString screenshot;
    int startPage = 1;   // 0 Medien, 1 Schnitt, 2 Farbe, 3 Musik, 4 Export
    int startTab = 0;    // Musik: 0 Noten, 1 Mixer
    const QStringList args = QGuiApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--screenshot") && i + 1 < args.size())
            screenshot = args[++i];
        else if (args[i] == QLatin1String("--page") && i + 1 < args.size())
            startPage = qBound(0, args[++i].toInt(), 4);
        else if (args[i] == QLatin1String("--tab") && i + 1 < args.size())
            startTab = qBound(0, args[++i].toInt(), 1);
        else if (args[i] == QLatin1String("--select") && i + 1 < args.size())
            controller.setSelectedClipId(args[++i]);
        else if (args[i] == QLatin1String("--seek") && i + 1 < args.size())
            controller.seek(args[++i].toDouble());
        else if (!args[i].startsWith(QLatin1Char('-')))
            controller.openProjectFile(args[i]);
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), startPage);
    engine.rootContext()->setContextProperty(QStringLiteral("startTab"), startTab);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    if (!screenshot.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QTimer::singleShot(1500, window, [window, screenshot] {
            const bool ok = window && window->grabWindow().save(screenshot);
            QGuiApplication::exit(ok ? 0 : 1);
        });
    }
    return QGuiApplication::exec();
}
