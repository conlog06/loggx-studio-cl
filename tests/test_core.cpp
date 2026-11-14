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
    testUndo();
    return finish("test_core");
}
