#include <cmath>

#include "check.h"
#include "loggx/core/Project.h"
#include "loggx/core/UndoStack.h"

using namespace loggx;

namespace {

MediaAsset videoAsset(TimeUs duration) {
    MediaAsset a;
    a.path = "/tmp/video.mp4";
    a.name = "video.mp4";
    a.duration = duration;
    a.width = 1920;
    a.height = 1080;
    a.fps = 30;
    a.hasVideo = true;
    a.hasAudio = true;
    return a;
}

std::string firstTrack(const Project& p, TrackKind kind) {
    for (const auto& t : p.tracks)
        if (t.kind == kind) return t.id;
    return {};
}

void testAddAndOverlap() {
    Project p = Project::createDefault();
    const std::string asset = p.addAsset(videoAsset(10 * kSecond));
    const std::string v = firstTrack(p, TrackKind::Video);
    const std::string a = firstTrack(p, TrackKind::Audio);

    const std::string c1 = p.addClip(v, asset, 0);
    CHECK(!c1.empty());
    CHECK(p.duration() == 10 * kSecond);

    // Gleiche Position: Clip rutscht hinter den vorhandenen.
    const std::string c2 = p.addClip(v, asset, 2 * kSecond);
    CHECK(p.findClip(c2)->start == 10 * kSecond);

    // Video mit Ton darf auch auf eine Audiospur.
    CHECK(!p.addClip(a, asset, 0).empty());

    // Reines Audio darf nicht auf eine Videospur.
    MediaAsset music;
    music.duration = 5 * kSecond;
    music.hasAudio = true;
    const std::string m = p.addAsset(music);
    CHECK(p.addClip(v, m, 0).empty());
    CHECK(p.addClip("gibt-es-nicht", asset, 0).empty());
}

void testSplitTrimMove() {
    Project p = Project::createDefault();
    const std::string asset = p.addAsset(videoAsset(10 * kSecond));
    const std::string v = firstTrack(p, TrackKind::Video);
    const std::string c1 = p.addClip(v, asset, 1 * kSecond);

    const std::string c2 = p.splitClip(c1, 4 * kSecond);
    CHECK(!c2.empty());
    CHECK(p.findClip(c1)->duration == 3 * kSecond);
    CHECK(p.findClip(c2)->start == 4 * kSecond);
    CHECK(p.findClip(c2)->in == 3 * kSecond);
    CHECK(p.findClip(c2)->duration == 7 * kSecond);
    // Schnitt außerhalb oder direkt am Rand ist nicht erlaubt.
    CHECK(p.splitClip(c1, 1 * kSecond).empty());
    CHECK(p.splitClip(c1, 20 * kSecond).empty());

    // Anfang trimmen verschiebt auch den Einstiegspunkt.
    CHECK(p.trimClipStart(c2, 5 * kSecond));
    CHECK(p.findClip(c2)->in == 4 * kSecond);
    CHECK(p.findClip(c2)->duration == 6 * kSecond);
    // Nicht über den Anfang des Materials hinaus zurückziehen ...
    CHECK(p.trimClipStart(c2, 4 * kSecond));
    CHECK(!p.trimClipStart(c1, 500000));
    // ... und nicht in den Nachbarclip hinein.
    CHECK(!p.trimClipStart(c2, 2 * kSecond));

    // Ende nicht länger ziehen, als Material vorhanden ist.
    CHECK(!p.trimClipEnd(c2, 30 * kSecond));
    CHECK(p.trimClipEnd(c2, 8 * kSecond));
    CHECK(p.findClip(c2)->end() == 8 * kSecond);

    // Verschieben: Überlappung wird abgelehnt, freie Stelle angenommen.
    CHECK(!p.moveClip(c2, v, 2 * kSecond));
    CHECK(p.moveClip(c2, v, 20 * kSecond));
    CHECK(p.findClip(c2)->start == 20 * kSecond);
    // Auf eine Spur anderer Art geht nicht.
    CHECK(!p.moveClip(c2, firstTrack(p, TrackKind::Audio), 0));
    // Auf die zweite Videospur schon.
    const std::string v2 = p.tracks[1].id;
    CHECK(p.moveClip(c2, v2, 0));
    const Track* owner = nullptr;
    CHECK(p.findClip(c2, &owner) && owner->id == v2);
}

void testSpeedAndRipple() {
    Project p = Project::createDefault();
    const std::string asset = p.addAsset(videoAsset(10 * kSecond));
    const std::string v = firstTrack(p, TrackKind::Video);
    const std::string c1 = p.addClip(v, asset, 0);
    const std::string c2 = p.addClip(v, asset, 10 * kSecond);

    CHECK(p.setClipSpeed(c1, 2.0));
    CHECK(p.findClip(c1)->duration == 5 * kSecond);
    CHECK(p.findClip(c1)->sourceTime(1 * kSecond) == 2 * kSecond);
    // Verlangsamen stößt an den Nachbarn und wird dort gekürzt.
    CHECK(p.setClipSpeed(c1, 0.5));
    CHECK(p.findClip(c1)->end() == 10 * kSecond);
    CHECK(!p.setClipSpeed(c1, 0.0));

    CHECK(p.removeClip(c1, true));
    CHECK(p.findClip(c2)->start == 0);
    CHECK(!p.removeAsset(asset));
    CHECK(p.removeClip(c2));
    CHECK(p.removeAsset(asset));
}

void testJsonRoundTrip() {
    Project p = Project::createDefault();
    p.name = "Testfilm äöü";
    p.width = 1280;
    p.height = 720;
    p.fpsNum = 30000;
    p.fpsDen = 1001;
    const std::string asset = p.addAsset(videoAsset(10 * kSecond));
    const std::string c = p.addClip(firstTrack(p, TrackKind::Video), asset, 500000);
    Clip* clip = p.findClip(c);
    clip->gain = 0.5;
    clip->pitch = -3;
    clip->opacity = 0.75;
    clip->mask.shape = MaskShape::Ellipse;
    clip->mask.feather = 0.1;
    clip->mask.invert = true;

    Project q;
    std::string error;
    CHECK(Project::fromJson(p.toJson(), {}, q, &error));
    CHECK(q.name == p.name);
    CHECK(q.width == 1280 && q.height == 720 && q.fpsNum == 30000 && q.fpsDen == 1001);
    CHECK(q.assets.size() == 1 && q.tracks.size() == p.tracks.size());
    const Clip* loaded = q.findClip(c);
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->start == 500000 && loaded->duration == 10 * kSecond);
        CHECK(loaded->gain == 0.5 && loaded->pitch == -3 && loaded->opacity == 0.75);
        CHECK(loaded->mask.shape == MaskShape::Ellipse && loaded->mask.invert);
    }
    // Neue ids dürfen nach dem Laden nicht mit alten kollidieren.
    CHECK(q.addAsset(videoAsset(kSecond)) != asset);

    CHECK(!Project::fromJson("{ kaputt", {}, q, &error));
    CHECK(!Project::fromJson("{\"format\":\"etwas anderes\"}", {}, q, &error));
    CHECK(!Project::fromJson("{\"format\":\"loggx\",\"version\":99}", {}, q, &error));
}

void testKeyframesAndFades() {
    Project p = Project::createDefault();
    const std::string asset = p.addAsset(videoAsset(10 * kSecond));
    const std::string v = firstTrack(p, TrackKind::Video);
    const std::string c = p.addClip(v, asset, 2 * kSecond);

    // Ohne Keyframes gilt der Grundwert.
    CHECK(p.findClip(c)->valueAt("opacity", 5 * kSecond) == 1.0);
    CHECK(p.setKeyframe(c, "opacity", 2 * kSecond, 0.0));
    CHECK(p.setKeyframe(c, "opacity", 4 * kSecond, 1.0));
    CHECK(!p.setKeyframe(c, "gibt-es-nicht", 0, 1.0));
    const Clip* clip = p.findClip(c);
    CHECK(std::abs(clip->valueAt("opacity", 3 * kSecond) - 0.5) < 1e-9);
    CHECK(clip->valueAt("opacity", 1 * kSecond) == 0.0);    // vor dem ersten: erster Wert
    CHECK(clip->valueAt("opacity", 9 * kSecond) == 1.0);    // nach dem letzten: letzter Wert
    // Gleiche Stelle überschreibt.
    CHECK(p.setKeyframe(c, "opacity", 4 * kSecond, 0.8));
    CHECK(p.findClip(c)->keys.at("opacity").size() == 2);

    // Trimmen vorne lässt Keyframes an ihrer Timeline-Position.
    CHECK(p.trimClipStart(c, 3 * kSecond));
    CHECK(std::abs(p.findClip(c)->valueAt("opacity", 3500000) - 0.6) < 1e-9);
    // Teilen: beide Hälften behalten die Kurve.
    const std::string right = p.splitClip(c, 3500000);
    CHECK(std::abs(p.findClip(right)->valueAt("opacity", 3750000) - 0.7) < 1e-9);
    CHECK(std::abs(p.findClip(c)->valueAt("opacity", 3250000) - 0.5) < 1e-9);

    // Letzten Keyframe entfernen: sein Wert wird Grundwert.
    CHECK(p.removeKeyframe(right, "opacity", 2 * kSecond));
    CHECK(p.removeKeyframe(right, "opacity", 4 * kSecond));
    CHECK(!p.findClip(right)->hasKeys());
    CHECK(std::abs(p.findClip(right)->opacity - 0.8) < 1e-9);

    // Blenden.
    Clip* r = p.findClip(right);
    r->fadeIn = kSecond / 4;
    r->fadeOut = kSecond;
    CHECK(r->fadeAt(r->start) == 0.0);
    CHECK(std::abs(r->fadeAt(r->start + kSecond / 8) - 0.5) < 1e-9);
    CHECK(r->fadeAt(r->start + kSecond / 2) == 1.0);
    CHECK(std::abs(r->fadeAt(r->end() - kSecond / 2) - 0.5) < 1e-9);
}

void testTitlesAndMidi() {
    Project p = Project::createDefault();
    const std::string v = firstTrack(p, TrackKind::Video);
    const std::string inst = firstTrack(p, TrackKind::Instrument);
    const std::string audio = firstTrack(p, TrackKind::Audio);
    CHECK(!inst.empty());

    const std::string title = p.addTitleClip(v, kSecond, "Hallo");
    CHECK(!title.empty() && p.findClip(title)->kind == ClipKind::Title);
    CHECK(p.addTitleClip(inst, 0).empty());
    CHECK(p.addTitleClip(audio, 0).empty());
    // Titel lassen sich beliebig verlängern.
    CHECK(p.trimClipEnd(title, 60 * kSecond));

    p.bpm = 120;   // ein Takt = 2 s
    const std::string midi = p.addMidiClip(inst, 0);
    CHECK(p.findClip(midi)->duration == 8 * kSecond);
    CHECK(p.addMidiClip(v, 0).empty());
    p.findClip(midi)->notes = {{60, 0, kSecond / 2, 0.9}, {64, kSecond, kSecond / 2, 0.7}};
    // MIDI-Clips sind nicht auf Audiospuren verschiebbar.
    CHECK(!p.moveClip(midi, audio, 0));
    // Vorne kürzen verschiebt den sichtbaren Ausschnitt, nicht die Noten.
    CHECK(p.trimClipStart(midi, kSecond / 2));
    CHECK(p.findClip(midi)->in == kSecond / 2);
    CHECK(p.findClip(midi)->notes.size() == 2);
    CHECK(!p.setClipSpeed(midi, 2.0));

    // Leere Spuren lassen sich entfernen, volle nicht.
    const std::string extra = p.addTrack(TrackKind::Instrument);
    CHECK(p.findTrack(extra)->name == "Instrument 2");
    CHECK(p.removeTrack(extra));
    CHECK(!p.removeTrack(inst));

    // Alles übersteht Speichern und Laden.
    Clip* t = p.findClip(title);
    t->title.color = 0xFFFF8800;
    t->title.align = 2;
    t->x = 0.1;
    t->scale = 1.5;
    t->rotation = 12;
    t->color.saturation = 0;
    t->color.lutPath = "/tmp/look.cube";
    t->fx.blur = 0.01;
    t->fx.chromaKey = true;
    p.findTrack(inst)->instrument = InstrumentType::Drums;
    p.findTrack(inst)->pan = -0.5;
    p.findTrack(inst)->solo = true;
    p.findTrack(inst)->fx.reverb = 0.3;
    p.bpm = 95;
    p.masterGain = 0.7;
    CHECK(p.setKeyframe(title, "scale", 2 * kSecond, 2.0));

    Project q;
    std::string error;
    CHECK(Project::fromJson(p.toJson(), {}, q, &error));
    const Clip* lt = q.findClip(title);
    const Clip* lm = q.findClip(midi);
    CHECK(lt && lm);
    if (lt && lm) {
        CHECK(lt->kind == ClipKind::Title && lt->title.text == "Hallo");
        CHECK(lt->title.color == 0xFFFF8800 && lt->title.align == 2);
        CHECK(lt->x == 0.1 && lt->scale == 1.5 && lt->rotation == 12);
        CHECK(lt->color.saturation == 0 && lt->color.lutPath == "/tmp/look.cube");
        CHECK(lt->fx.blur == 0.01 && lt->fx.chromaKey);
        CHECK(lt->keys.at("scale").size() == 1);
        CHECK(lm->kind == ClipKind::Midi && lm->notes.size() == 2 && lm->notes[1].pitch == 64);
    }
    const Track* li = q.findTrack(inst);
    CHECK(li && li->kind == TrackKind::Instrument && li->instrument == InstrumentType::Drums);
    CHECK(li && li->pan == -0.5 && li->solo && li->fx.reverb == 0.3);
    CHECK(q.bpm == 95 && q.masterGain == 0.7);

    // Version-1-Dateien (Prototyp) lassen sich weiter öffnen.
    const std::string v1 =
        "{\"format\":\"loggx\",\"version\":1,\"name\":\"alt\",\"assets\":[],"
        "\"tracks\":[{\"id\":\"t1\",\"name\":\"Video 1\",\"kind\":\"video\",\"clips\":[]}]}";
    CHECK(Project::fromJson(v1, {}, q, &error) && q.name == "alt" && q.bpm == 120);
}

void testUndo() {
    UndoStack<int> stack;
    int value = 1;
    stack.push(value);
    value = 2;
    stack.push(value);
    value = 3;
    CHECK(stack.undo(value) && value == 2);
    CHECK(stack.undo(value) && value == 1);
    CHECK(!stack.undo(value));
    CHECK(stack.redo(value) && value == 2);
    stack.push(value);   // neue Änderung verwirft den Redo-Zweig
    value = 9;
    CHECK(!stack.canRedo());
    CHECK(stack.undo(value) && value == 2);
}

}  // namespace

int main() {
    testAddAndOverlap();
    testSplitTrimMove();
    testSpeedAndRipple();
    testJsonRoundTrip();
    testKeyframesAndFades();
    testTitlesAndMidi();
    testUndo();
    return finish("test_core");
}
