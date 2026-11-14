// Prüft die Logik hinter der Oberfläche (AppController) ohne Fenster:
// Import, Schnitt, Undo/Redo, Speichern/Laden, Wiedergabe und Export.
#include <cmath>

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>

#include "AppController.h"
#include "check.h"

using namespace loggx;

namespace {

bool makeSource(const QString& path) {
    ExportSettings s;
    s.path = path.toStdString();
    s.width = 320;
    s.height = 180;
    s.fpsNum = 25;
    Exporter e;
    if (!e.open(s)) return false;
    std::vector<std::uint8_t> image(320u * 180 * 4, 200);
    std::vector<float> audio(1920 * 2, 0.1f);
    for (int i = 0; i < 100; ++i) {   // 4 Sekunden
        if (!e.writeVideoFrame(image.data()) || !e.writeAudio(audio.data(), 1920)) return false;
    }
    return e.finish();
}

template <typename Pred>
bool waitFor(Pred pred, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return pred();
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication qapp(argc, argv);
    QTemporaryDir dir;
    const QString source = dir.filePath("quelle.mp4");
    CHECK(makeSource(source));

    AppController app;
    int frames = 0;
    QObject::connect(&app, &AppController::frameReady, [&](const QImage& image) {
        if (!image.isNull()) ++frames;
    });

    // Import: einmal zählt, doppelt und kaputt nicht.
    app.importMedia({QUrl::fromLocalFile(source), QUrl::fromLocalFile(source),
                     QUrl::fromLocalFile(dir.filePath("fehlt.mp4"))});
    CHECK(app.assets().size() == 1);
    CHECK(app.dirty());
    const QString asset = app.assets().first().toMap().value("id").toString();

    CHECK(app.addAssetToTimeline(asset));
    CHECK(std::abs(app.duration() - 4.0) < 0.2);
    CHECK(!app.selectedClipId().isEmpty());
    const QString first = app.selectedClipId();
    // Solange die Datei benutzt wird, lässt sie sich nicht entfernen.
    CHECK(!app.removeAsset(asset));

    // Teilen am Abspielkopf, dann Undo/Redo.
    app.seek(2.0);
    CHECK(std::abs(app.position() - 2.0) < 1e-6);
    app.splitAtPlayhead();
    std::size_t clips = 0;
    for (const auto& t : app.project().tracks) clips += t.clips.size();
    CHECK(clips == 2);
    app.undo();
    clips = 0;
    for (const auto& t : app.project().tracks) clips += t.clips.size();
    CHECK(clips == 1);
    CHECK(app.canRedo());
    app.redo();
    CHECK(app.project().findClip(first.toStdString())->duration == 2 * kSecond);

    // Ein Reglerzug mit vielen Zwischenwerten ist ein einziger Undo-Schritt.
    app.setSelectedClipId(first);
    app.beginInteractive();
    for (int db = -1; db >= -12; --db) app.setClipProperty("gainDb", db);
    app.endInteractive();
    CHECK(std::abs(app.clip().value("gainDb").toDouble() + 12.0) < 0.01);
    app.undo();
    CHECK(std::abs(app.clip().value("gainDb").toDouble()) < 0.01);
    app.redo();

    app.setClipProperty("maskShape", 2);
    app.setClipProperty("pitch", 5);
    CHECK(app.clip().value("maskShape").toInt() == 2);
    CHECK(app.clip().value("pitch").toDouble() == 5.0);

    // Ziehen in der Timeline: Überlappung abgelehnt, freie Stelle angenommen.
    const QString track = QString::fromStdString(app.project().tracks[1].id);
    app.beginInteractive();
    CHECK(!app.moveClipLive(first, track, 1 * kSecond));
    CHECK(app.moveClipLive(first, track, 10 * kSecond));
    app.endInteractive();
    CHECK(std::abs(app.duration() - 12.0) < 0.01);
    app.undo();
    CHECK(std::abs(app.duration() - 4.0) < 0.2);

    // Löschen mit Lücke schließen.
    app.deleteSelected(true);
    CHECK(app.selectedClipId().isEmpty());
    CHECK(std::abs(app.duration() - 2.0) < 0.2);
    app.undo();

    // Speichern und wieder laden.
    CHECK(!app.saveProject());   // noch kein Dateiname
    const QString file = dir.filePath("film");
    CHECK(app.saveProjectAs(QUrl::fromLocalFile(file)));
    CHECK(app.filePath().endsWith(".loggx"));
    CHECK(!app.dirty());
    const double savedDuration = app.duration();
    app.newProject();
    CHECK(app.duration() == 0.0 && app.assets().isEmpty() && !app.canUndo());
    CHECK(!app.openProject(QUrl::fromLocalFile(dir.filePath("gibt-es-nicht.loggx"))));
    CHECK(app.openProject(QUrl::fromLocalFile(file + ".loggx")));
    CHECK(app.duration() == savedDuration && !app.dirty());

    // Vorschau liefert Bilder.
    app.seek(1.0);
    CHECK(waitFor([&] { return frames > 0; }, 5000));

    // Wiedergabe läuft etwa in Echtzeit und hält am Ende an.
    app.seek(0);
    app.play();
    CHECK(app.playing());
    waitFor([] { return false; }, 500);
    CHECK(app.position() > 0.3 && app.position() < 0.9);
    app.seek(app.duration() - 0.2);
    CHECK(waitFor([&] { return !app.playing(); }, 3000));
    CHECK(std::abs(app.position() - app.duration()) < 1e-6);

    // Export im Hintergrund.
    const QString output = dir.filePath("export.mp4");
    app.exportVideo(QUrl::fromLocalFile(output), 320, 180);
    CHECK(app.exporting());
    CHECK(waitFor([&] { return !app.exporting(); }, 60000));
    MediaAsset exported;
    CHECK(probeMedia(output.toStdString(), exported));
    CHECK(exported.hasVideo && exported.hasAudio && exported.width == 320);
    CHECK(std::abs(exported.duration / 1e6 - app.duration()) < 0.2);

    // Bildgenaues Springen ohne Drift, auch bei 30 fps (33,333… ms pro Bild).
    app.seek(0);
    for (int i = 0; i < 45; ++i) app.stepFrames(1);
    CHECK(app.timecode(app.position()) == QStringLiteral("00:00:01:15"));
    app.stepFrames(-15);
    CHECK(app.timecode(app.position()) == QStringLiteral("00:00:01:00"));
    CHECK(app.timecode(61.5) == QStringLiteral("00:01:01:15"));
    return finish("test_app");
}
