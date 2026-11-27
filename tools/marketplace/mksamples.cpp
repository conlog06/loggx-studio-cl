// Erzeugt die Start-Vorlagen des Marktplatzes (nur Titel und MIDI, keine Medien).
// Bauen: g++ -std=c++20 -Icore/include tools/marketplace/mksamples.cpp build/libloggx_core.a -o /tmp/mksamples
#include <loggx/core/Project.h>

#include <cstdio>
#include <filesystem>
#include <string>

using namespace loggx;
namespace fs = std::filesystem;

static Clip& clip(Project& p, const std::string& id) { return *p.findClip(id); }
static void key(Clip& c, const char* param, std::initializer_list<std::pair<double, double>> pts) {
    for (auto [t, v] : pts) c.keys[param].push_back({static_cast<TimeUs>(t * kSecond), v});
}
static void save(Project& p, const std::string& dir) {
    fs::create_directories(dir);
    std::string err;
    if (!p.saveFile(dir + "/project.loggx", &err)) std::fprintf(stderr, "%s: %s\n", dir.c_str(), err.c_str());
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "samples";
    {   // YouTube-Intro
        Project p;
        p.name = "YouTube-Intro";
        const auto t1 = p.addTrack(TrackKind::Video, "Titel"), t2 = p.addTrack(TrackKind::Video, "Untertitel");
        auto& a = clip(p, p.addTitleClip(t1, 0, "DEIN KANAL"));
        a.duration = 5 * kSecond; a.fadeIn = 600'000; a.fadeOut = 500'000;
        a.title.size = 0.16; a.title.posY = 0.45; a.title.color = 0xFFFFFFFF;
        key(a, "scale", {{0, 0.85}, {1.2, 1.0}, {5, 1.05}});
        auto& b = clip(p, p.addTitleClip(t2, 800'000, "Neues Video jeden Freitag"));
        b.duration = 4'200'000; b.fadeIn = 500'000; b.fadeOut = 500'000;
        b.title.size = 0.055; b.title.bold = false; b.title.posY = 0.62; b.title.color = 0xFFFFC23D;
        key(b, "y", {{0, 0.03}, {1.0, 0.0}});
        save(p, out + "/youtube-intro");
    }
    {   // Story / Reel hochkant
        Project p;
        p.name = "Story Hochformat";
        p.width = 1080; p.height = 1920;
        const auto t1 = p.addTrack(TrackKind::Video, "Oben"), t2 = p.addTrack(TrackKind::Video, "Unten");
        auto& a = clip(p, p.addTitleClip(t1, 0, "3 TIPPS"));
        a.duration = 6 * kSecond; a.fadeIn = 300'000;
        a.title.size = 0.08; a.title.posY = 0.16; a.title.box = 0.0;
        key(a, "scale", {{0, 1.3}, {0.4, 1.0}});
        auto& b = clip(p, p.addTitleClip(t2, 400'000, "Hier dein Text"));
        b.duration = 5'600'000; b.fadeIn = 300'000; b.fadeOut = 300'000;
        b.title.size = 0.04; b.title.posY = 0.82; b.title.box = 0.65; b.title.bold = true;
        save(p, out + "/story-hochformat");
    }
    {   // Bauchbinde
        Project p;
        p.name = "Bauchbinde";
        const auto t1 = p.addTrack(TrackKind::Video, "Name"), t2 = p.addTrack(TrackKind::Video, "Funktion");
        auto& a = clip(p, p.addTitleClip(t1, 0, "Max Mustermann"));
        a.duration = 5 * kSecond; a.fadeIn = 400'000; a.fadeOut = 400'000;
        a.title.size = 0.06; a.title.align = 0; a.title.posY = 0.78; a.title.box = 0.75;
        key(a, "x", {{0, -0.25}, {0.5, 0.0}});
        auto& b = clip(p, p.addTitleClip(t2, 250'000, "Gründer · LXStudio"));
        b.duration = 4'750'000; b.fadeIn = 400'000; b.fadeOut = 400'000;
        b.title.size = 0.035; b.title.bold = false; b.title.align = 0; b.title.posY = 0.86; b.title.color = 0xFF9FD0FF;
        key(b, "x", {{0, -0.25}, {0.6, 0.0}});
        save(p, out + "/bauchbinde");
    }
    {   // Lo-Fi Beat
        Project p;
        p.name = "Lo-Fi Beat 85 BPM";
        p.mode = ProjectMode::Music;
        p.bpm = 85;
        const TimeUs beat = p.beatDuration(), bar = beat * 4;
        auto mk = [&](const char* name, InstrumentType inst) {
            const auto id = p.addTrack(TrackKind::Instrument, name);
            p.findTrack(id)->instrument = inst;
            return id;
        };
        const auto drums = mk("Drums", InstrumentType::Drums), keys = mk("Keys", InstrumentType::ElectricPiano),
                   bass = mk("Bass", InstrumentType::Bass);
        auto& d = clip(p, p.addMidiClip(drums, 0, bar * 4));
        for (int b = 0; b < 4; ++b) {
            const TimeUs o = bar * b;
            d.notes.push_back({36, o, beat / 2, 0.9});
            d.notes.push_back({36, o + beat * 2 + beat / 2, beat / 2, 0.75});
            d.notes.push_back({38, o + beat, beat / 2, 0.8});
            d.notes.push_back({38, o + beat * 3, beat / 2, 0.8});
            for (int h = 0; h < 8; ++h) d.notes.push_back({42, o + beat / 2 * h + (h % 2 ? beat / 12 : 0), beat / 4, h % 2 ? 0.35 : 0.55});
        }
        // Akkorde: Fmaj7 – Em7 – Dm7 – Cmaj7
        const int chords[4][4] = {{53, 57, 60, 64}, {52, 55, 59, 62}, {50, 53, 57, 60}, {48, 52, 55, 59}};
        const int roots[4] = {41, 40, 38, 36};
        auto& k = clip(p, p.addMidiClip(keys, 0, bar * 4));
        auto& bs = clip(p, p.addMidiClip(bass, 0, bar * 4));
        for (int b = 0; b < 4; ++b) {
            for (int n : chords[b]) k.notes.push_back({n, bar * b, bar - beat / 4, 0.55});
            bs.notes.push_back({roots[b], bar * b, beat * 2 - beat / 4, 0.8});
            bs.notes.push_back({roots[b] + 7, bar * b + beat * 2 + beat / 2, beat, 0.65});
        }
        save(p, out + "/lofi-beat-85");
    }
    return 0;
}
