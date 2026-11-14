// Prüft die Logik hinter der Oberfläche (AppController) ohne Fenster:
// Import, Schnitt, Undo/Redo, Speichern/Laden, Wiedergabe und Export.
#include <cmath>

#include <QFile>

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "TitleRenderer.h"

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
    QStandardPaths::setTestModeEnabled(true);   // Autosave nicht im echten Profil
    QDir(AppController::autosaveDir()).removeRecursively();
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
    QElapsedTimer wall;
    wall.start();
    app.play();
    CHECK(app.playing());
    waitFor([] { return false; }, 600);
    // Nie schneller als die echte Zeit; langsamer nur um die Startzeit des
    // Audiogeräts (auf ausgelasteten Testrechnern großzügig bemessen).
    CHECK(app.position() > 0.1 && app.position() <= wall.elapsed() / 1000.0 + 0.05);
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
    // ---- MVP: Keyframes über den Controller
    app.newProject();
    app.importMedia({QUrl::fromLocalFile(source)});
    const QString a2 = app.assets().first().toMap().value("id").toString();
    CHECK(app.addAssetToTimeline(a2));
    const QString vc = app.selectedClipId();
    app.seek(0);
    app.toggleKeyframe("opacity");                 // Keyframe bei 0 s mit Wert 1
    CHECK(app.clip().value("keyed").toMap().value("opacity").toBool());
    app.seek(2.0);
    app.setClipProperty("opacity", 0.0);           // setzt automatisch Keyframe bei 2 s
    app.seek(1.0);
    CHECK(std::abs(app.clip().value("opacity").toDouble() - 0.5) < 0.02);
    app.jumpToKeyframe(1);
    CHECK(std::abs(app.position() - 2.0) < 0.01);
    CHECK(app.clip().value("keyHere").toMap().value("opacity").toBool());
    app.toggleKeyframe("opacity");                 // entfernt den bei 2 s
    app.seek(1.5);
    CHECK(std::abs(app.clip().value("opacity").toDouble() - 1.0) < 1e-6);

    // Transformation, Farbe, Effekte, Blenden
    app.setClipProperty("scale", 0.5);
    app.setClipProperty("saturation", 0.0);
    app.setClipProperty("chromaKey", 1);
    app.setClipProperty("fadeIn", 0.5);
    CHECK(app.clip().value("scale").toDouble() == 0.5);
    CHECK(app.clip().value("saturation").toDouble() == 0.0 && !app.clip().value("colorNeutral").toBool());
    CHECK(app.clip().value("chromaKey").toBool());
    CHECK(std::abs(app.clip().value("fadeIn").toDouble() - 0.5) < 1e-6);
    app.resetColor();
    CHECK(app.clip().value("colorNeutral").toBool());
    // LUT: ungültige Datei wird abgelehnt.
    QFile bad(dir.filePath("kaputt.cube"));
    CHECK(bad.open(QIODevice::WriteOnly) && bad.write("LUT_3D_SIZE 2\n0 0 0\n") > 0);
    bad.close();
    CHECK(!app.setLut(QUrl::fromLocalFile(dir.filePath("kaputt.cube"))));
    QFile good(dir.filePath("gut.cube"));
    CHECK(good.open(QIODevice::WriteOnly));
    good.write("LUT_3D_SIZE 2\n");
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) good.write(QStringLiteral("%1 %2 %3\n").arg(r).arg(g).arg(b).toUtf8());
    good.close();
    CHECK(app.setLut(QUrl::fromLocalFile(dir.filePath("gut.cube"))));
    CHECK(app.clip().value("lutName").toString() == "gut.cube");

    // Titel mit echter Schrift: Text muss sichtbare Pixel erzeugen.
    app.seek(0);
    CHECK(app.addTitle());
    CHECK(app.clip().value("isTitle").toBool());
    app.setTitleText("LoggX");
    app.setTitleColor(QColor(255, 0, 0));
    CHECK(app.clip().value("text").toString() == "LoggX");
    {
        Title t;
        t.text = "LoggX";
        t.color = 0xFFFF0000;
        std::vector<std::uint8_t> px(320u * 180 * 4, 0);
        qtTitleRasterizer()(t, 320, 180, px);
        int red = 0;
        for (std::size_t i = 0; i < px.size(); i += 4)
            if (px[i + 3] > 200 && px[i] > 200 && px[i + 1] < 60) ++red;
        CHECK(red > 200);
    }

    // MIDI: Clip anlegen, Noten setzen/ändern/löschen, Instrument wählen.
    app.seek(0);
    CHECK(app.addMidiClip());
    const QString midi = app.selectedClipId();
    CHECK(app.clip().value("isMidi").toBool());
    loggx::Note n;
    n.pitch = 64;
    n.start = 0;
    n.length = 500000;
    CHECK(app.addNote(midi, n) == 0);
    n.pitch = 67;
    CHECK(app.updateNote(midi, 0, n));
    CHECK(app.project().findClip(midi.toStdString())->notes.at(0).pitch == 67);
    CHECK(app.addNote(midi, n) == 1);
    CHECK(app.removeNote(midi, 1));
    CHECK(!app.removeNote(midi, 5));
    const int instTrack = app.clip().value("trackIndex").toInt();
    app.setTrackProperty(instTrack, "instrument", 6);
    app.setTrackProperty(instTrack, "pan", -0.5);
    app.setTrackProperty(instTrack, "reverb", 0.4);
    const QVariantMap trackInfo = app.tracks().at(instTrack).toMap();
    CHECK(trackInfo.value("instrument").toInt() == 6 && trackInfo.value("pan").toDouble() == -0.5);
    CHECK(trackInfo.value("reverb").toDouble() == 0.4);
    app.setBpm(90);
    CHECK(app.bpm() == 90);
    app.setMasterGainDb(-6);
    CHECK(std::abs(app.masterGainDb() + 6) < 0.01);
    // Leere Spuren lassen sich entfernen, volle nicht.
    CHECK(!app.removeTrack(instTrack));
    app.addInstrumentTrack();
    CHECK(app.removeTrack(static_cast<int>(app.project().tracks.size()) - 1));

    // Export mit Titel und Instrument (H.265, falls vorhanden – sonst H.264).
    const QString out2 = dir.filePath("mvp.mp4");
    app.exportVideo(QUrl::fromLocalFile(out2), 320, 180, 0, 2);
    CHECK(waitFor([&] { return !app.exporting(); }, 60000));
    MediaAsset mvpOut;
    CHECK(probeMedia(out2.toStdString(), mvpOut) && mvpOut.hasAudio);

    // Automatisch sichern und nach "Absturz" wiederherstellen.
    CHECK(app.dirty());
    CHECK(app.autosaveNow());
    {
        AppController crashed;   // neuer Start: findet die Notfallkopie
        CHECK(crashed.recoveryAvailable());
        CHECK(crashed.recoverAutosave());
        CHECK(crashed.project().findClip(midi.toStdString()) != nullptr);
        CHECK(crashed.dirty() && !crashed.recoveryAvailable());
        crashed.discardRecovery();
    }
    {
        AppController clean;
        CHECK(!clean.recoveryAvailable());
    }

    CHECK(app.timecode(61.5) == QStringLiteral("00:01:01:15"));
    return finish("test_app");
}
