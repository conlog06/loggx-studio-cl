// Prüft die Logik hinter der Oberfläche (AppController) ohne Fenster:
// Import, Schnitt, Undo/Redo, Speichern/Laden, Wiedergabe und Export.
#include <cmath>

#include <QFile>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "TitleRenderer.h"

#include "AppController.h"
#include "MicRecorder.h"
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
    QCoreApplication::setOrganizationName(QStringLiteral("LoggX-Test"));
    QCoreApplication::setApplicationName(QStringLiteral("test_app"));
    QSettings().clear();
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

    // ------------------------------------- Übergänge und Schnitt auf Beat
    {
        AppController v;
        v.newFromTemplate(false, 640, 360, 25, 120);
        CHECK(!v.musicMode() && !v.showStart());
        v.importMedia({QUrl::fromLocalFile(source)});
        CHECK(waitFor([&] { return v.assets().size() == 1; }, 10000));
        const QString asset = v.assets().first().toMap().value(QStringLiteral("id")).toString();
        CHECK(v.addAssetToTimeline(asset));
        const QString a = v.selectedClipId();
        CHECK(v.addAssetToTimeline(asset));
        const QString b = v.selectedClipId();
        CHECK(a != b && v.clip().value(QStringLiteral("hasPrevious")).toBool());
        v.setClipProperty(QStringLiteral("transition"), 1);
        v.setClipProperty(QStringLiteral("transitionDuration"), 0.8);
        const Clip* cb = v.project().findClip(b.toStdString());
        CHECK(cb && cb->transition == TransitionType::Crossfade && cb->transitionDuration == 800'000);
        CHECK(v.transitionNames().size() == kTransitionCount);
        v.setSelectedClipId(a);
        CHECK(v.cutOnBeats(4) == 1);          // 4 s bei 120 BPM: Schnitt nach einem Takt
        v.setBeatGrid(true);
        CHECK(v.beatGrid());

        // Export mit Übergang
        const QString out = dir.filePath("uebergang.mp4");
        v.exportVideo(QUrl::fromLocalFile(out), 320, 180, 0, 0);
        CHECK(waitFor([&] { return !v.exporting(); }, 60000));
        MediaAsset m;
        CHECK(probeMedia(out.toStdString(), m) && m.hasVideo && m.hasAudio);

        // Free simulieren: 4K und H.265 werden gesperrt
        CHECK(v.isAdmin() && v.isPro());
        v.setSimulateFree(true);
        CHECK(!v.isPro() && v.edition() == QStringLiteral("free"));
        v.exportVideo(QUrl::fromLocalFile(dir.filePath("4k.mp4")), 3840, 2160, 0, 0);
        CHECK(!v.exporting() && !QFile::exists(dir.filePath("4k.mp4")));
        v.setSimulateFree(false);
        CHECK(v.isPro());
        CHECK(!v.systemInfo().value(QStringLiteral("ffmpeg")).toString().isEmpty());
        v.discardRecovery();
    }

    // ---------------------------------------------------- Musikprojekt
    {
        AppController mu;
        mu.newMusicProject();
        CHECK(mu.musicMode() && mu.beatGrid() && std::abs(mu.bpm() - 120) < 1e-9);
        int instruments = 0;
        for (const auto& t : mu.project().tracks) instruments += t.kind == TrackKind::Instrument;
        CHECK(instruments >= 3);

        // Live spielen und aufnehmen (ohne Audiogerät läuft nur die Uhr)
        mu.startRecording();
        CHECK(mu.recording());
        waitFor([] { return false; }, 150);
        mu.liveNoteOn(60, 0.9);
        mu.liveNoteOn(64, 0.7);
        CHECK(mu.heldNotes().size() == 2);
        waitFor([] { return false; }, 300);
        mu.liveNoteOff(60);
        mu.liveNoteOff(64);
        CHECK(mu.heldNotes().isEmpty());
        mu.stopRecording();
        CHECK(!mu.recording() && !mu.playing());
        const Clip* rec = mu.project().findClip(mu.selectedClipId().toStdString());
        CHECK(rec && rec->kind == ClipKind::Midi && rec->notes.size() == 2);
        CHECK(rec && rec->start == 0 && rec->duration == 4 * mu.project().beatDuration());   // ein Takt
        mu.undo();
        CHECK(mu.project().findClip(rec ? rec->id : std::string()) == nullptr);
        mu.redo();

        // Audio-Export (FLAC 24 Bit), Stems und MIDI
        const QString flac = dir.filePath("song.flac");
        mu.exportAudio(QUrl::fromLocalFile(flac), 24, true);
        CHECK(waitFor([&] { return !mu.exporting(); }, 60000));
        MediaAsset fa;
        CHECK(probeMedia(flac.toStdString(), fa) && fa.hasAudio && !fa.hasVideo);
        QDir stems(dir.filePath("stems"));
        CHECK(QDir().mkpath(stems.path()));
        mu.exportStems(QUrl::fromLocalFile(stems.path()), QStringLiteral("wav"), 16);
        CHECK(waitFor([&] { return !mu.exporting(); }, 60000));
        QStringList wavs;
        QDirIterator it(stems.path(), {QStringLiteral("*.wav")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) wavs << it.next();
        CHECK(wavs.size() >= 1);
        const QString mid = dir.filePath("song.mid");
        CHECK(mu.exportMidiFile(QUrl::fromLocalFile(mid)));
        AppController back;
        back.newMusicProject();
        CHECK(back.importMidiFile(QUrl::fromLocalFile(mid)));
        int notes = 0;
        for (const auto& t : back.project().tracks)
            for (const auto& c : t.clips) notes += static_cast<int>(c.notes.size());
        CHECK(notes == 2);

        // Zuletzt geöffnet
        const QString saved = dir.filePath("song.loggx");
        CHECK(mu.saveProjectAs(QUrl::fromLocalFile(saved)));
        const QVariantList recent = mu.recentProjects();
        CHECK(!recent.isEmpty() && recent.first().toMap().value(QStringLiteral("path")).toString() == QFileInfo(saved).absoluteFilePath());
        AppController reopen;
        CHECK(reopen.openRecent(saved) && reopen.musicMode() && !reopen.showStart());
        reopen.forgetRecent(QFileInfo(saved).absoluteFilePath());
        for (const QVariant& r : reopen.recentProjects())
            CHECK(r.toMap().value(QStringLiteral("path")).toString() != QFileInfo(saved).absoluteFilePath());
        back.discardRecovery();
        mu.discardRecovery();
    }
    // --------------- Vorschau, Format, Hineinziehen, Kopieren, Standbild, WAV
    {
        AppController t;
        t.newFromTemplate(false, 1920, 1080, 29.97, 120);
        CHECK(t.projectFpsNum() == 30000 && t.projectFpsDen() == 1001);
        CHECK(t.formatInfo() == QStringLiteral("1920×1080 · 29.970 fps"));
        t.setProjectFormat(3840, 2160, 24000, 1001);
        CHECK(t.projectWidth() == 3840 && t.projectFpsNum() == 24000);
        t.setProjectFormat(3840, 2160, 50, 1);
        CHECK(std::abs(t.project().fps() - 50) < 1e-9);
        t.setBeatsPerBar(3);
        CHECK(t.beatsPerBar() == 3);

        // Vorschau folgt der Fenstergröße (Auto) bzw. der gewählten Stufe
        t.setPreviewQuality(0);
        t.setViewerSize(1600, 900);
        CHECK(t.previewSize() == QStringLiteral("1600×900"));
        t.setViewerSize(8000, 5000);
        CHECK(t.previewSize() == QStringLiteral("3840×2160"));   // nie größer als das Projekt
        t.setPreviewQuality(2);
        CHECK(t.previewSize() == QStringLiteral("1920×1080"));
        t.setPreviewQuality(3);
        CHECK(t.previewSize() == QStringLiteral("960×540"));
        t.setPreviewQuality(0);

        // Hineinziehen: nur importieren, in die Timeline, Projektdatei
        CHECK(t.dropFiles({QUrl::fromLocalFile(source)}, -2).isEmpty());
        CHECK(t.assets().size() == 1 && t.duration() == 0);
        t.seek(1.0);
        t.dropFiles({QUrl::fromLocalFile(source), QUrl::fromLocalFile(source)}, -1, 2.0);
        CHECK(t.assets().size() == 1);
        int clips = 0;
        for (const auto& tr : t.project().tracks) clips += static_cast<int>(tr.clips.size());
        CHECK(clips == 2);
        CHECK(std::abs(t.duration() - 10.0) < 0.2);   // ab 2 s, zweimal 4 s hintereinander
        const QString proj = dir.filePath("gezogen.loggx");
        CHECK(t.saveProjectAs(QUrl::fromLocalFile(proj)));
        CHECK(t.dropFiles({QUrl::fromLocalFile(proj)}) == proj);

        // Kopieren, Einfügen, Duplizieren
        const QString sel = t.selectedClipId();
        CHECK(!sel.isEmpty() && t.copySelected() && t.canPaste());
        t.seek(20.0);
        CHECK(t.pasteAtPlayhead());
        CHECK(t.selectedClipId() != sel && std::abs(t.clip().value(QStringLiteral("start")).toDouble() - 20.0) < 0.05);
        CHECK(t.duplicateSelected());
        CHECK(std::abs(t.clip().value(QStringLiteral("start")).toDouble() - 24.0) < 0.05);
        t.undo();
        CHECK(std::abs(t.duration() - 24.0) < 0.2);

        // Standbild in Projektauflösung
        t.setProjectFormat(640, 360, 25, 1);
        t.seek(3.0);
        const QString still = dir.filePath("standbild.png");
        CHECK(t.exportStill(QUrl::fromLocalFile(still)));
        const QImage img(still);
        CHECK(img.width() == 640 && img.height() == 360);
        t.discardRecovery();
    }
    {
        // WAV-Datei der Mikrofon-Aufnahme
        const QString wav = dir.filePath("mic.wav");
        WavWriter w;
        CHECK(w.create(wav, 48000, 2, 16, false));
        std::vector<qint16> buf(48000 * 2);
        for (std::size_t i = 0; i < buf.size() / 2; ++i)
            buf[i * 2] = buf[i * 2 + 1] = static_cast<qint16>(12000 * std::sin(2 * 3.14159265 * 440 * i / 48000.0));
        CHECK(w.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size() * 2)) > 0);
        CHECK(w.takePeak() > 0.3);
        CHECK(w.frames() == 48000);
        w.finish();
        MediaAsset m;
        CHECK(probeMedia(wav.toStdString(), m) && m.hasAudio && std::llabs(m.duration - kSecond) < 30'000);
    }
    // ------------------- Tempo/Tonart erkennen und ändern, Stimme entfernen
    {
        // Stereo-Song: Mitte 440 Hz + Klicks im 120er-Takt, links 330 Hz
        const QString song = dir.filePath("stereo.wav");
        {
            ExportSettings s;
            s.path = song.toStdString();
            s.video = false;
            Exporter e;
            CHECK(e.open(s));
            const int rate = s.sampleRate;
            std::vector<float> buf(static_cast<std::size_t>(rate) * 6 * 2);
            for (int i = 0; i < rate * 6; ++i) {
                const double t = static_cast<double>(i) / rate;
                const double click = std::fmod(t, 0.5) < 0.01 ? 0.6 * std::sin(2 * 3.14159265 * 2000 * t) : 0.0;
                const float voice = static_cast<float>(0.2 * std::sin(2 * 3.14159265 * 440 * t) + click);
                buf[static_cast<std::size_t>(i) * 2] = voice + static_cast<float>(0.2 * std::sin(2 * 3.14159265 * 330 * t));
                buf[static_cast<std::size_t>(i) * 2 + 1] = voice;
            }
            CHECK(e.writeAudio(buf.data(), buf.size() / 2) && e.finish());
        }
        AppController a;
        a.newFromTemplate(false, 640, 360, 25, 100);
        CHECK(a.dropFiles({QUrl::fromLocalFile(song)}) .isEmpty());
        const QString clipId = a.selectedClipId();
        CHECK(!clipId.isEmpty() && !a.clip().value(QStringLiteral("analyzed")).toBool());
        CHECK(a.analyzeSelected());
        CHECK(waitFor([&] { return !a.working(); }, 30000));
        CHECK(a.clip().value(QStringLiteral("analyzed")).toBool());
        const double bpm = a.clip().value(QStringLiteral("origBpm")).toDouble();
        CHECK(std::abs(bpm - 120.0) < 2.0);
        a.setClipBpm(90.0);
        CHECK(std::abs(a.clip().value(QStringLiteral("speed")).toDouble() - 90.0 / bpm) < 1e-6);
        a.matchClipTempo();
        CHECK(std::abs(a.clip().value(QStringLiteral("bpmNow")).toDouble() - 100.0) < 0.01);
        const int key = a.clip().value(QStringLiteral("origKey")).toInt();
        if (key >= 0) {
            a.setClipKey((key + 3) % 12);
            CHECK(a.clip().value(QStringLiteral("pitch")).toInt() == 3);
            a.setClipKey((key + 9) % 12);   // kürzer: −3
            CHECK(a.clip().value(QStringLiteral("pitch")).toInt() == -3);
        }
        CHECK(a.keyNames(true).size() == 12);
        // Analyse wird mit dem Projekt gespeichert
        const QString pf = dir.filePath("analyse.loggx");
        CHECK(a.saveProjectAs(QUrl::fromLocalFile(pf)));
        {
            AppController b;
            CHECK(b.openRecent(pf));
            CHECK(std::abs(b.project().assets.front().bpm - bpm) < 1e-9);
        }

        // Stimme entfernen: zwei neue Spuren, Original stumm
        const std::size_t tracksBefore = a.project().tracks.size();
        CHECK(a.separateSelected(false));
        CHECK(waitFor([&] { return !a.working(); }, 60000));
        CHECK(a.project().tracks.size() == tracksBefore + 2);
        CHECK(a.project().findClip(clipId.toStdString())->gain == 0.0);
        const Track* owner = nullptr;
        const Clip* inst = a.project().findClip(a.selectedClipId().toStdString(), &owner);
        CHECK(inst && owner && !owner->muted && owner->name == "Instrumental");
        CHECK(inst && inst->start == a.project().findClip(clipId.toStdString())->start);
        a.undo();
        CHECK(a.project().tracks.size() == tracksBefore);
        a.discardRecovery();
    }
    {
        // Song-Tempo und Transponieren
        AppController m;
        m.newMusicProject();
        CHECK(m.addMidiClip());
        Clip* mc = const_cast<Clip*>(m.project().findClip(m.selectedClipId().toStdString()));
        CHECK(mc != nullptr);
        const TimeUs len = mc->duration;
        CHECK(m.setSongTempo(60.0));
        CHECK(std::abs(m.bpm() - 60.0) < 1e-9);
        CHECK(m.project().findClip(m.selectedClipId().toStdString())->duration == 2 * len);
        CHECK(!m.setSongTempo(1000.0));
        CHECK(m.transposeSong(2) >= 1);
        CHECK(m.project().findClip(m.selectedClipId().toStdString())->pitch == 2.0);
        m.discardRecovery();
    }
    return finish("test_app");
}
