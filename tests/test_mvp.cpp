#include <tuple>
// Tests der MVP-Funktionen der Engine: Transformation, Farbe, LUT,
// Effekte, Blenden, Keyframes, Titel, Instrumente, Mixer und H.265.
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <random>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ImageOps.h"
#include "loggx/engine/Dj.h"
#include "check.h"
#include "loggx/engine/Engine.h"
#include "loggx/engine/Plugins.h"

using namespace loggx;
namespace fs = std::filesystem;

namespace {

constexpr int kW = 320, kH = 180, kFps = 25, kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

// Ein Video, das komplett in einer Farbe leuchtet, mit 440-Hz-Ton.
bool makeClip(const std::string& path, int r, int g, int b, double seconds = 2.0) {
    ExportSettings s;
    s.path = path;
    s.width = kW;
    s.height = kH;
    s.fpsNum = kFps;
    s.sampleRate = kRate;
    s.quality = 3;
    Exporter e;
    if (!e.open(s)) return false;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(kW) * kH * 4);
    for (std::size_t p = 0; p < image.size(); p += 4) {
        image[p] = static_cast<std::uint8_t>(r);
        image[p + 1] = static_cast<std::uint8_t>(g);
        image[p + 2] = static_cast<std::uint8_t>(b);
        image[p + 3] = 255;
    }
    std::vector<float> audio(static_cast<std::size_t>(kRate / kFps) * 2);
    std::int64_t sample = 0;
    for (int i = 0; i < static_cast<int>(seconds * kFps); ++i) {
        if (!e.writeVideoFrame(image.data())) return false;
        for (std::size_t n = 0; n < audio.size() / 2; ++n, ++sample)
            audio[n * 2] = audio[n * 2 + 1] =
                0.5f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * sample / kRate));
        if (!e.writeAudio(audio.data(), audio.size() / 2)) return false;
    }
    return e.finish();
}

struct Px {
    int r, g, b;
};
Px at(const std::vector<std::uint8_t>& img, int x, int y, int w = kW) {
    const std::uint8_t* p = img.data() + (static_cast<std::size_t>(y) * w + x) * 4;
    return {p[0], p[1], p[2]};
}
bool near(Px p, int r, int g, int b, int tol = 30) {
    return std::abs(p.r - r) < tol && std::abs(p.g - g) < tol && std::abs(p.b - b) < tol;
}

double rms(const std::vector<float>& a, int channel = -1) {
    double sum = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (channel >= 0 && static_cast<int>(i % 2) != channel) continue;
        sum += static_cast<double>(a[i]) * a[i];
        ++n;
    }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0;
}

double peak(const std::vector<float>& a) {
    double p = 0;
    for (float v : a) p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

// Leistung bei einer Frequenz (Goertzel), linker Kanal.
double power(const std::vector<float>& a, double freq) {
    const std::size_t n = a.size() / 2;
    const double k = 2.0 * std::cos(2.0 * kPi * freq / kRate);
    double s1 = 0, s2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = a[i * 2] + k * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - k * s1 * s2) / static_cast<double>(n * n);
}

Project baseProject() {
    Project p = Project::createDefault();
    p.width = kW;
    p.height = kH;
    p.fpsNum = kFps;
    p.sampleRate = kRate;
    return p;
}

std::string videoTrack(Project& p, int which = 0) {   // 0 = unterste Videospur
    std::vector<std::string> ids;
    for (const auto& t : p.tracks)
        if (t.kind == TrackKind::Video) ids.push_back(t.id);
    return ids[ids.size() - 1 - static_cast<std::size_t>(which)];
}

std::string trackOf(Project& p, TrackKind kind) {
    for (const auto& t : p.tracks)
        if (t.kind == kind) return t.id;
    return {};
}

void testVideo(const fs::path& dir) {
    const std::string red = (dir / "rot.mp4").string();
    const std::string green = (dir / "gruen.mp4").string();
    CHECK(makeClip(red, 220, 30, 30));
    CHECK(makeClip(green, 0, 200, 0));
    MediaAsset ra, ga;
    CHECK(probeMedia(red, ra) && probeMedia(green, ga));

    Project p = baseProject();
    const std::string redId = p.addAsset(ra);
    const std::string greenId = p.addAsset(ga);
    const std::string c = p.addClip(videoTrack(p), redId, 0);
    Compositor comp;
    std::vector<std::uint8_t> img;
    auto render = [&](TimeUs t) {
        comp.setProject(std::make_shared<const Project>(p));
        comp.render(t, kW, kH, img);
    };

    // Verkleinern und verschieben: Mitte rot, Ecke schwarz, rechts versetzt.
    p.findClip(c)->scale = 0.5;
    p.findClip(c)->x = 0.25;
    render(kSecond / 2);
    CHECK(near(at(img, 240, 90), 220, 30, 30));
    CHECK(near(at(img, 100, 90), 0, 0, 0));
    CHECK(near(at(img, 240, 20), 0, 0, 0));

    // 90° drehen: das 16:9-Bild steht hochkant, oben/unten Rot, links/rechts schwarz.
    p.findClip(c)->scale = 0.5;
    p.findClip(c)->x = 0;
    p.findClip(c)->rotation = 90;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 20), 220, 30, 30));    // 0.5 * 320 = 160 hoch -> 10..170
    CHECK(near(at(img, 100, 90), 0, 0, 0));        // 0.5 * 180 = 90 breit -> 115..205
    CHECK(near(at(img, 160, 90), 220, 30, 30));
    p.findClip(c)->rotation = 0;
    p.findClip(c)->scale = 1;

    // Keyframes für die Größe: bei 0 s klein, bei 1 s voll.
    CHECK(p.setKeyframe(c, "scale", 0, 0.2));
    CHECK(p.setKeyframe(c, "scale", kSecond, 1.0));
    render(0);
    CHECK(near(at(img, 20, 20), 0, 0, 0));
    render(kSecond);
    CHECK(near(at(img, 20, 20), 220, 30, 30));
    CHECK(p.clearKeyframes(c, "scale"));

    // Schwarzweiß: Sättigung 0 ergibt Grau.
    p.findClip(c)->color.saturation = 0.0;
    render(kSecond / 2);
    {
        const Px px = at(img, 160, 90);
        CHECK(std::abs(px.r - px.g) < 4 && std::abs(px.g - px.b) < 4 && px.r > 40 && px.r < 120);
    }
    p.findClip(c)->color.saturation = 1.0;

    // Belichtung +1 Blende macht heller.
    p.findClip(c)->color.exposure = -1.0;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 110, 15, 15, 20));
    p.findClip(c)->color.exposure = 0.0;

    // LUT, die Rot und Blau vertauscht (2er-Würfel reicht dafür).
    const std::string lut = (dir / "tausch.cube").string();
    {
        std::ofstream f(lut);
        f << "# Test\nTITLE \"Tausch\"\nLUT_3D_SIZE 2\n";
        for (int b = 0; b < 2; ++b)
            for (int g = 0; g < 2; ++g)
                for (int r = 0; r < 2; ++r) f << b << ' ' << g << ' ' << r << '\n';
    }
    p.findClip(c)->color.lutPath = lut;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 30, 30, 220));
    p.findClip(c)->color.lutMix = 0.5;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 125, 30, 125));
    // Kaputte LUT wird ignoriert statt abzustürzen.
    p.findClip(c)->color.lutPath = (dir / "fehlt.cube").string();
    p.findClip(c)->color.lutMix = 1.0;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 220, 30, 30));
    p.findClip(c)->color = ColorGrade{};

    // Greenscreen: grüner Clip oben, Grün wird durchsichtig -> Rot darunter.
    const std::string top = p.addClip(videoTrack(p, 1), greenId, 0);
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 0, 200, 0));
    p.findClip(top)->fx.chromaKey = true;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 220, 30, 30));

    // Halbe Deckkraft oben ohne Key: Mischfarbe.
    p.findClip(top)->fx.chromaKey = false;
    p.findClip(top)->opacity = 0.5;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 110, 115, 15));
    CHECK(p.removeClip(top));

    // Blenden: am Anfang schwarz, nach der Hälfte der Blende halb hell.
    p.findClip(c)->fadeIn = kSecond / 2;
    render(0);
    CHECK(near(at(img, 160, 90), 0, 0, 0));
    render(kSecond / 4);
    CHECK(near(at(img, 160, 90), 110, 15, 15, 20));
    p.findClip(c)->fadeIn = 0;

    // Vignette: Ecken dunkler als die Mitte.
    p.findClip(c)->fx.vignette = 1.0;
    render(kSecond / 2);
    CHECK(at(img, 2, 2).r < at(img, 160, 90).r - 80);
    p.findClip(c)->fx.vignette = 0;

    // Titel: mit einem Test-Rasterer, der ein weißes Rechteck in die Mitte malt.
    const std::string title = p.addTitleClip(videoTrack(p, 1), 0, "Hallo");
    int rasterCalls = 0;
    comp.setTitleRasterizer([&](const Title& t, int w, int h, std::vector<std::uint8_t>& out) {
        ++rasterCalls;
        CHECK(t.text == "Hallo");
        for (int y = h / 3; y < 2 * h / 3; ++y)
            for (int x = w / 3; x < 2 * w / 3; ++x) {
                std::uint8_t* q = out.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                q[0] = q[1] = q[2] = q[3] = 255;
            }
    });
    p.findClip(title)->fadeIn = 0;
    render(kSecond / 2);
    CHECK(near(at(img, 160, 90), 255, 255, 255, 10));
    CHECK(near(at(img, 10, 10), 220, 30, 30));
    render(kSecond);   // gleicher Titel: aus dem Zwischenspeicher
    CHECK(rasterCalls == 1);

    // Weichzeichnen verwischt die Kante des Titels (transparenter Rand).
    const int sharpInside = at(img, 109, 90).g, sharpOutside = at(img, 104, 90).g;
    p.findClip(title)->fx.blur = 0.03;
    render(kSecond);
    const int blurInside = at(img, 109, 90).g, blurOutside = at(img, 104, 90).g;
    CHECK(sharpInside > 240 && sharpOutside < 60);
    CHECK(blurInside < sharpInside - 20 && blurOutside > sharpOutside + 20);
    // Der Text-Inhalt ändert sich durch den Effekt nicht: kein neues Rastern.
    CHECK(rasterCalls == 1);
}

void testInstruments() {
    // Jedes Instrument klingt, und Blöcke ergeben dasselbe wie am Stück.
    for (int i = 0; i < kInstrumentCount; ++i) {
        const auto type = static_cast<InstrumentType>(i);
        const int pitch = type == InstrumentType::Drums ? 38 : 60;
        std::vector<float> whole(kRate / 2, 0.0f), parts(kRate / 2, 0.0f);
        synth::renderNote(type, pitch, 0.9, kRate / 4, 0, whole.data(), whole.size(), kRate);
        synth::renderNote(type, pitch, 0.9, kRate / 4, 0, parts.data(), 1000, kRate);
        synth::renderNote(type, pitch, 0.9, kRate / 4, 1000, parts.data() + 1000,
                          parts.size() - 1000, kRate);
        double energy = 0, diff = 0;
        for (std::size_t n = 0; n < whole.size(); ++n) {
            energy += whole[n] * whole[n];
            diff += std::abs(whole[n] - parts[n]);
        }
        CHECK(energy > 1.0);
        CHECK(diff < 1e-3);
        CHECK(peak(whole) < 1.0);
    }
    // Der Bass spielt die richtige Tonhöhe (A2 = 110 Hz).
    std::vector<float> mono(kRate, 0.0f);
    synth::renderNote(InstrumentType::Bass, 45, 1.0, kRate, 0, mono.data(), mono.size(), kRate);
    std::vector<float> stereo(mono.size() * 2);
    for (std::size_t i = 0; i < mono.size(); ++i) stereo[i * 2] = stereo[i * 2 + 1] = mono[i];
    CHECK(power(stereo, 110.0) > 20 * power(stereo, 103.8));
    CHECK(power(stereo, 110.0) > 20 * power(stereo, 116.5));
}

void testMixer(const fs::path& dir) {
    Project p = baseProject();
    p.bpm = 120;
    const std::string inst = trackOf(p, TrackKind::Instrument);
    p.findTrack(inst)->instrument = InstrumentType::ElectricPiano;
    const std::string midi = p.addMidiClip(inst, 0, kSecond);
    p.findClip(midi)->notes = {{69, 0, kSecond / 2, 1.0}};   // A4 = 440 Hz

    AudioMixer mixer;
    std::vector<float> buf(static_cast<std::size_t>(kRate) * 2);
    auto render = [&](std::int64_t pos, std::size_t frames) {
        buf.assign(frames * 2, 0.0f);
        mixer.setProject(std::make_shared<const Project>(p));
        mixer.render(pos, buf.data(), frames);
    };

    render(0, kRate / 4);
    CHECK(rms(buf) > 0.02);
    CHECK(power(buf, 440.0) > 10 * power(buf, 415.3));

    // Notenlänge ist begrenzt: nach Loslassen + Ausklang Stille.
    render(kRate, kRate / 4);
    CHECK(rms(buf) < 1e-4);

    // Clip-Tonhöhe transponiert MIDI: +12 Halbtöne -> 880 Hz.
    p.findClip(midi)->pitch = 12;
    render(0, kRate / 4);
    CHECK(power(buf, 880.0) > 10 * power(buf, 440.0));
    p.findClip(midi)->pitch = 0;

    // Notenausschnitt: liegt die Note vor dem sichtbaren Bereich, klingt nichts.
    CHECK(p.trimClipStart(midi, kSecond / 10));
    render(0, kRate / 4);
    CHECK(rms(buf) < 1e-4);
    p.findClip(midi)->in = 0;
    p.findClip(midi)->start = 0;
    p.findClip(midi)->duration = kSecond;

    // Panorama ganz links: rechter Kanal stumm.
    p.findTrack(inst)->pan = -1.0;
    render(0, kRate / 4);
    CHECK(rms(buf, 0) > 0.02 && rms(buf, 1) < 1e-4);
    p.findTrack(inst)->pan = 0;

    // Solo: eine andere Spur auf Solo -> Instrument verstummt.
    p.findTrack(trackOf(p, TrackKind::Audio))->solo = true;
    render(0, kRate / 4);
    CHECK(rms(buf) < 1e-6);
    p.findTrack(trackOf(p, TrackKind::Audio))->solo = false;

    // Pegelanzeige liefert Werte für Spur und Master.
    render(0, kRate / 4);
    const MeterLevels levels = mixer.takeLevels();
    CHECK(levels.tracks.count(inst) && levels.tracks.at(inst) > 0.05f && levels.master > 0.05f);
    CHECK(mixer.takeLevels().master == 0.0f);

    // Hall verlängert den Klang über das Notenende hinaus.
    p.findClip(midi)->notes = {{69, 0, kSecond / 10, 1.0}};
    p.findTrack(inst)->instrument = InstrumentType::Bass;
    render(0, kRate);
    std::vector<float> dryTail(buf.begin() + kRate / 2 * 2, buf.begin() + kRate / 2 * 2 + kRate / 4 * 2);
    p.findTrack(inst)->fx.reverb = 0.8;
    render(0, kRate);
    std::vector<float> wetTail(buf.begin() + kRate / 2 * 2, buf.begin() + kRate / 2 * 2 + kRate / 4 * 2);
    CHECK(rms(wetTail) > 10 * rms(dryTail) + 1e-4);
    p.findTrack(inst)->fx.reverb = 0;

    // EQ: Bässe -12 dB macht einen tiefen Ton deutlich leiser.
    p.findClip(midi)->notes = {{36, 0, kSecond, 1.0}};   // C2 ≈ 65 Hz
    render(0, kRate / 2);
    const double before = power(buf, 65.4);
    p.findTrack(inst)->fx.eqLow = -12;
    render(0, kRate / 2);
    CHECK(power(buf, 65.4) < before * 0.25);
    p.findTrack(inst)->fx.eqLow = 0;

    // Limiter: viele laute Töne gleichzeitig übersteuern nicht.
    p.findTrack(inst)->instrument = InstrumentType::Synth;
    p.findTrack(inst)->gain = 4.0;
    p.findClip(midi)->notes.clear();
    for (int n = 48; n < 72; n += 3) p.findClip(midi)->notes.push_back({n, 0, kSecond, 1.0});
    render(0, kRate / 2);
    CHECK(peak(buf) <= 0.892);
    CHECK(rms(buf) > 0.2);
    CHECK(mixer.takeLevels().reduction < -1.0f);

    // Video-Ton mit Lautstärke-Keyframe: von stumm auf voll.
    const std::string clipFile = (dir / "ton.mp4").string();
    CHECK(makeClip(clipFile, 10, 10, 10));
    MediaAsset asset;
    CHECK(probeMedia(clipFile, asset));
    Project q = baseProject();
    const std::string c = q.addClip(trackOf(q, TrackKind::Audio), q.addAsset(asset), 0);
    CHECK(q.setKeyframe(c, "gainDb", 0, -60));
    CHECK(q.setKeyframe(c, "gainDb", kSecond, 0));
    AudioMixer m2;
    m2.setProject(std::make_shared<const Project>(q));
    std::vector<float> early(kRate / 10 * 2), late(kRate / 10 * 2);
    m2.render(0, early.data(), kRate / 10);
    m2.render(kRate + kRate / 10, late.data(), kRate / 10);
    CHECK(rms(early) < 0.05 && rms(late) > 0.3);
}

void testExport(const fs::path& dir) {
    const std::string src = (dir / "quelle.mp4").string();
    CHECK(makeClip(src, 30, 30, 220, 1.0));
    MediaAsset asset;
    CHECK(probeMedia(src, asset));
    Project p = baseProject();
    p.addClip(videoTrack(p), p.addAsset(asset), 0);
    // Instrument dazu, damit Video- und Instrumentton gemeinsam exportiert werden.
    const std::string midi = p.addMidiClip(trackOf(p, TrackKind::Instrument), 0, kSecond);
    p.findClip(midi)->notes = {{60, 0, kSecond / 2, 0.8}};

    for (const auto codec : {ExportSettings::Codec::H264, ExportSettings::Codec::H265}) {
        ExportSettings s;
        s.path = (dir / (codec == ExportSettings::Codec::H265 ? "h265.mp4" : "h264.mp4")).string();
        s.width = kW;
        s.height = kH;
        s.codec = codec;
        s.quality = 2;
        const RenderResult r = renderProject(p, s);
        if (codec == ExportSettings::Codec::H265 && !r.ok) {
            std::printf("Hinweis: kein H.265-Encoder (%s)\n", r.error.c_str());
            continue;
        }
        CHECK(r.ok);
        std::printf("Export %s mit %s\n", codec == ExportSettings::Codec::H265 ? "H.265" : "H.264",
                    r.videoEncoder.c_str());
        MediaAsset out;
        CHECK(probeMedia(s.path, out));
        CHECK(out.hasVideo && out.hasAudio && out.width == kW);
        VideoReader reader;
        std::vector<std::uint8_t> img;
        CHECK(reader.open(s.path) && reader.frameAt(500000, kW, kH, img));
        CHECK(near(at(img, 160, 90), 30, 30, 220));
    }
}

void testTransitions(const fs::path& dir) {
    const std::string red = (dir / "t_rot.mp4").string();
    const std::string blue = (dir / "t_blau.mp4").string();
    CHECK(makeClip(red, 220, 30, 30, 2.0));
    CHECK(makeClip(blue, 30, 30, 220, 2.0));
    MediaAsset ra, ba;
    CHECK(probeMedia(red, ra) && probeMedia(blue, ba));
    Project p = baseProject();
    const std::string v = videoTrack(p);
    const std::string a = p.addClip(v, p.addAsset(ra), 0);
    const std::string b = p.addClip(v, p.addAsset(ba), 0);   // rutscht direkt hinter a
    CHECK(p.findClip(a)->duration == p.findClip(b)->start);
    CHECK(p.trimClipEnd(a, kSecond));
    CHECK(p.moveClip(b, v, kSecond));
    Clip* cb = p.findClip(b);
    cb->transitionDuration = 400000;   // 0,8 .. 1,2 s
    Compositor comp;
    std::vector<std::uint8_t> img;
    auto render = [&](TimeUs t) {
        comp.setProject(std::make_shared<const Project>(p));
        comp.render(t, kW, kH, img);
    };

    // Überblenden: Mitte = Mischfarbe, außerhalb unverändert.
    cb->transition = TransitionType::Crossfade;
    render(700000);
    CHECK(near(at(img, 160, 90), 220, 30, 30));
    render(kSecond);
    CHECK(near(at(img, 160, 90), 125, 30, 125));
    render(1300000);
    CHECK(near(at(img, 160, 90), 30, 30, 220));

    // Über Schwarz: in der Mitte schwarz, kurz davor dunkles Rot.
    cb->transition = TransitionType::DipBlack;
    render(kSecond);
    CHECK(near(at(img, 160, 90), 0, 0, 0, 12));
    render(900000);
    CHECK(near(at(img, 160, 90), 110, 15, 15, 25));
    cb->transition = TransitionType::DipWhite;
    render(kSecond);
    CHECK(near(at(img, 160, 90), 255, 255, 255, 12));

    // Wischen: links schon Blau, rechts noch Rot.
    cb->transition = TransitionType::Wipe;
    render(kSecond);
    CHECK(near(at(img, 40, 90), 30, 30, 220));
    CHECK(near(at(img, 280, 90), 220, 30, 30));

    // Schieben: zur Hälfte -> linke Hälfte Rot (verschoben), rechte Blau.
    cb->transition = TransitionType::Slide;
    render(kSecond);
    CHECK(near(at(img, 40, 90), 220, 30, 30));
    CHECK(near(at(img, 280, 90), 30, 30, 220));

    // Neue Übergänge, jeweils in der Mitte (Bild 320×180)
    cb->transition = TransitionType::WipeUp;
    render(kSecond);
    CHECK(near(at(img, 160, 20), 220, 30, 30));    // oben noch Rot
    CHECK(near(at(img, 160, 160), 30, 30, 220));   // unten schon Blau
    cb->transition = TransitionType::SlideUp;
    render(kSecond);
    CHECK(near(at(img, 160, 20), 220, 30, 30));
    CHECK(near(at(img, 160, 160), 30, 30, 220));
    cb->transition = TransitionType::Circle;
    render(kSecond);
    CHECK(near(at(img, 160, 90), 30, 30, 220));    // Mitte Blau
    CHECK(near(at(img, 2, 2), 220, 30, 30));       // Ecke noch Rot
    cb->transition = TransitionType::Zoom;
    render(1190000);
    CHECK(near(at(img, 160, 90), 30, 30, 220));
    render(810000);
    CHECK(near(at(img, 160, 90), 220, 30, 30));
    cb->transition = TransitionType::BlurDissolve;
    render(kSecond);
    CHECK(near(at(img, 160, 90), 125, 30, 125, 35));
    cb->transition = TransitionType::Pixelate;
    render(900000);
    CHECK(near(at(img, 160, 90), 220, 30, 30));
    render(1100000);
    CHECK(near(at(img, 160, 90), 30, 30, 220));
    for (int i = 0; i < kTransitionCount; ++i) CHECK(std::string(transitionName(static_cast<TransitionType>(i))) != "?");

    // Ton beim Überblenden: kein Loch in der Mitte (gleiche Leistung).
    cb->transition = TransitionType::Crossfade;
    AudioMixer mixer;
    mixer.setProject(std::make_shared<const Project>(p));
    std::vector<float> mid(kRate / 20 * 2), dip(kRate / 20 * 2);
    mixer.render(kRate - kRate / 40, mid.data(), kRate / 20);
    CHECK(rms(mid) > 0.25);
    // Über Schwarz: genau am Schnitt still.
    cb->transition = TransitionType::DipBlack;
    AudioMixer mixer2;
    mixer2.setProject(std::make_shared<const Project>(p));
    mixer2.render(kRate - 240, dip.data(), 480);
    dip.resize(480 * 2);
    CHECK(rms(dip) < 0.05);
}

void testAudioExport(const fs::path& dir) {
    Project p = baseProject();
    p.bpm = 120;
    const std::string inst = trackOf(p, TrackKind::Instrument);
    p.findTrack(inst)->instrument = InstrumentType::Piano;
    p.findTrack(inst)->fx.reverb = 0.5;
    const std::string midi = p.addMidiClip(inst, 0, kSecond);
    p.findClip(midi)->notes = {{69, 0, kSecond / 2, 1.0}};
    // zweite Spur: Bass, damit es zwei Stems gibt
    const std::string bassTrack = p.addTrack(TrackKind::Instrument, "Bass");
    p.findTrack(bassTrack)->instrument = InstrumentType::Bass;
    p.findClip(p.addMidiClip(bassTrack, 0, kSecond))->notes = {{45, 0, kSecond / 2, 1.0}};

    struct Fmt {
        const char* ext;
        bool required;
    };
    for (const Fmt f : {Fmt{"wav", true}, Fmt{"flac", true}, Fmt{"m4a", true}, Fmt{"mp3", false},
                        Fmt{"ogg", false}, Fmt{"opus", false}}) {
        ExportSettings s;
        s.path = (dir / (std::string("song.") + f.ext)).string();
        const RenderResult r = renderAudio(p, s);
        if (!r.ok && !f.required) {
            std::printf("Hinweis: .%s nicht verfügbar (%s)\n", f.ext, r.error.c_str());
            continue;
        }
        CHECK(r.ok);
        MediaAsset out;
        CHECK(probeMedia(s.path, out));
        CHECK(out.hasAudio && !out.hasVideo);
        // Ausklang: länger als das Projekt (Hall), aber höchstens 5 s mehr.
        CHECK(out.duration > kSecond + 100000 && out.duration < 6 * kSecond + 200000);
        AudioReader reader;
        std::vector<float> buf(kRate / 4 * 2);
        CHECK(reader.open(s.path, kRate) && reader.read(buf.data(), kRate / 4) == static_cast<std::size_t>(kRate / 4));
        CHECK(rms(buf) > 0.02);
    }
    // 24 Bit WAV
    ExportSettings s24;
    s24.path = (dir / "song24.wav").string();
    s24.audioBits = 24;
    CHECK(renderAudio(p, s24).ok);
    CHECK(fs::file_size(s24.path) > fs::file_size((dir / "song.wav").string()) * 14 / 10);
    // Ohne Ausklang: genau so lang wie das Projekt.
    ExportSettings sNoTail;
    sNoTail.path = (dir / "kurz.wav").string();
    CHECK(renderAudio(p, sNoTail, {}, nullptr, false).ok);
    MediaAsset shortOut;
    CHECK(probeMedia(sNoTail.path, shortOut) && std::llabs(shortOut.duration - kSecond) < 30000);
    // Unbekannte Endung
    ExportSettings bad;
    bad.path = (dir / "song.xyz").string();
    CHECK(!renderAudio(p, bad).ok);

    // Stems: eine Datei pro klingender Spur
    std::vector<std::string> files;
    const RenderResult stems = renderStems(p, (dir / "stems").string(), "wav", 16, &files);
    CHECK(stems.ok && files.size() == 2);
    if (files.size() == 2) {
        CHECK(fs::path(files[0]).filename().string().rfind("01 ", 0) == 0);
        AudioReader r1, r2;
        std::vector<float> b1(kRate / 4 * 2), b2(kRate / 4 * 2);
        CHECK(r1.open(files[0], kRate) && r2.open(files[1], kRate));
        r1.read(b1.data(), kRate / 4);
        r2.read(b2.data(), kRate / 4);
        // Erste Datei: Klavier (440 Hz), zweite: Bass (110 Hz)
        CHECK(power(b1, 440.0) > 5 * power(b1, 110.0));
        CHECK(power(b2, 110.0) > 5 * power(b2, 440.0));
    }
}

void testBeatsAndLive(const fs::path& dir) {
    // Drum-Loop mit 128 BPM, erster Schlag bei 0,2 s
    const double bpm = 128.0;
    const std::size_t total = static_cast<std::size_t>(kRate) * 12;
    std::vector<float> mono(total, 0.0f);
    const double beatSec = 60.0 / bpm;
    for (int i = 0; 0.2 + i * beatSec < 11.5; ++i) {
        const std::size_t at = static_cast<std::size_t>((0.2 + i * beatSec) * kRate);
        const int pitch = i % 2 == 0 ? 36 : 38;
        synth::renderNote(InstrumentType::Drums, pitch, 1.0, kRate / 8, 0, mono.data() + at,
                          std::min<std::size_t>(kRate / 2, total - at), kRate);
        const std::size_t off = at + static_cast<std::size_t>(beatSec / 2 * kRate);
        if (off < total)
            synth::renderNote(InstrumentType::Drums, 42, 0.6, kRate / 16, 0, mono.data() + off,
                              std::min<std::size_t>(kRate / 4, total - off), kRate);
    }
    const BeatInfo direct = detectBeatsFromSamples(mono.data(), mono.size(), kRate);
    std::printf("Tempo erkannt: %.2f BPM, erster Schlag %.3f s, Sicherheit %.2f\n", direct.bpm,
                direct.firstBeat / 1e6, direct.confidence);
    CHECK(std::abs(direct.bpm - bpm) < 1.0);
    // Phase modulo Schlaglänge
    const double phase = std::fmod(direct.firstBeat / 1e6 - 0.2 + 10 * beatSec, beatSec);
    CHECK(phase < 0.03 || beatSec - phase < 0.03);

    // Über eine Datei (WAV)
    const std::string wav = (dir / "loop.wav").string();
    {
        ExportSettings s;
        s.path = wav;
        s.video = false;
        Exporter e;
        CHECK(e.open(s));
        std::vector<float> st(total * 2);
        for (std::size_t i = 0; i < total; ++i) st[i * 2] = st[i * 2 + 1] = mono[i] * 0.8f;
        CHECK(e.writeAudio(st.data(), total) && e.finish());
    }
    const BeatInfo fromFile = detectBeats(wav);
    CHECK(std::abs(fromFile.bpm - bpm) < 1.0);
    // Stille: kein Tempo
    std::vector<float> silence(kRate * 4, 0.0f);
    CHECK(detectBeatsFromSamples(silence.data(), silence.size(), kRate).bpm == 0.0 ||
          detectBeatsFromSamples(silence.data(), silence.size(), kRate).confidence < 0.2);

    // Live spielen: Ton beim Drücken, Ausklang nach dem Loslassen, dann Stille.
    LiveSynth live;
    live.setSampleRate(kRate);
    std::vector<float> buf(1024 * 2);
    live.render(buf.data(), 1024);
    CHECK(rms(buf) == 0.0);
    live.noteOn(69, 1.0, InstrumentType::Synth);
    double held = 0;
    for (int i = 0; i < 10; ++i) {
        live.render(buf.data(), 1024);
        held = rms(buf);
    }
    CHECK(held > 0.03);
    live.noteOff(69);
    for (int i = 0; i < 30; ++i) live.render(buf.data(), 1024);   // > 0,2 s Ausklang
    CHECK(rms(buf) < 1e-4);
    CHECK(live.activeVoices() == 0);
    live.noteOn(60, 1.0, InstrumentType::Piano);
    live.noteOn(64, 1.0, InstrumentType::Piano);
    CHECK(live.activeVoices() == 2);
    live.allNotesOff();
    for (int i = 0; i < 40; ++i) live.render(buf.data(), 1024);
    CHECK(live.activeVoices() == 0);
}

}  // namespace

void testVocalsAndKey(const fs::path& dir) {
    // Mitte: „Gesang“ 440 Hz mit Obertönen und Vibrato; links 330 Hz, rechts 523 Hz.
    const std::size_t n = static_cast<std::size_t>(kRate) * 6;
    std::vector<float> mix(n * 2);
    double phase = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += 2 * kPi * 440.0 * (1.0 + 0.004 * std::sin(2 * kPi * 5 * t)) / kRate;
        const float voice = static_cast<float>(0.25 * std::sin(phase) + 0.08 * std::sin(2 * phase));
        const float left = static_cast<float>(0.25 * std::sin(2 * kPi * 330.0 * t));
        const float right = static_cast<float>(0.25 * std::sin(2 * kPi * 523.25 * t));
        mix[i * 2] = voice + left;
        mix[i * 2 + 1] = voice + 0.1f * left + right;
    }
    std::vector<float> vocals, inst;
    std::atomic<int> calls{0};
    const SeparationResult r = separateVocalsBuffer(mix, kRate, vocals, inst, [&](double) { ++calls; });
    CHECK(r.ok && calls > 0);
    std::printf("Stimmentrennung: Gesangsanteil %.2f, Gesang 440/330: %.1f dB, Instrumental 330/440: %.1f dB\n",
                r.vocalShare, 10 * std::log10(power(vocals, 440) / (power(vocals, 330) + 1e-12)),
                10 * std::log10(power(inst, 330) / (power(inst, 440) + 1e-12)));
    CHECK(power(vocals, 440) > 20 * power(vocals, 330));   // Gesang ohne linkes Instrument
    CHECK(power(inst, 330) > 5 * power(inst, 440));       // Instrumental fast ohne Gesang
    // Beide zusammen ergeben das Original.
    double err = 0;
    for (std::size_t i = 0; i < mix.size(); ++i) err = std::max(err, static_cast<double>(std::abs(vocals[i] + inst[i] - mix[i])));
    CHECK(err < 1e-4);
    // Mono lässt sich so nicht trennen.
    std::vector<float> mono(mix.size());
    for (std::size_t i = 0; i < n; ++i) mono[i * 2] = mono[i * 2 + 1] = mix[i * 2];
    const SeparationResult rm = separateVocalsBuffer(mono, kRate, vocals, inst);
    CHECK(!rm.ok && rm.mono);

    // Über Dateien: WAV hinein, zwei WAV heraus.
    const std::string in = (dir / "song.wav").string();
    {
        ExportSettings s;
        s.path = in;
        s.video = false;
        s.sampleRate = kRate;
        Exporter e;
        CHECK(e.open(s) && e.writeAudio(mix.data(), n) && e.finish());
    }
    const SeparationResult rf = separateVocals(in, (dir / "gesang.wav").string(), (dir / "instrumental.wav").string(), kRate);
    CHECK(rf.ok);
    MediaAsset va, ia;
    CHECK(probeMedia((dir / "gesang.wav").string(), va) && probeMedia((dir / "instrumental.wav").string(), ia));
    CHECK(std::llabs(va.duration - 6 * kSecond) < 50'000 && std::llabs(ia.duration - 6 * kSecond) < 50'000);

    // Tonart: Akkorde C – F – G – C (C-Dur) und a – d – E – a (a-Moll)
    auto chords = [&](const std::vector<std::vector<double>>& seq) {
        std::vector<float> m;
        for (const auto& chord : seq)
            for (int i = 0; i < kRate; ++i) {
                double v = 0;
                for (double midi : chord) v += std::sin(2 * kPi * 440.0 * std::pow(2.0, (midi - 69) / 12.0) * i / kRate);
                m.push_back(static_cast<float>(0.2 * v));
            }
        return m;
    };
    const auto cmaj = chords({{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {60, 64, 67}, {48, 60, 64, 67}});
    KeyInfo k = detectKeyFromSamples(cmaj.data(), cmaj.size(), kRate);
    std::printf("Tonart 1: %s (%.2f)\n", keyName(k.tonic, k.minor).c_str(), k.confidence);
    CHECK(k.tonic == 0 && !k.minor);
    const auto amin = chords({{57, 60, 64}, {62, 65, 69}, {64, 68, 71}, {57, 60, 64}, {45, 57, 60, 64}});
    k = detectKeyFromSamples(amin.data(), amin.size(), kRate);
    std::printf("Tonart 2: %s (%.2f)\n", keyName(k.tonic, k.minor).c_str(), k.confidence);
    CHECK(k.tonic == 9 && k.minor);
    CHECK(keyName(6, false) == "Fis-Dur" && keyName(1, true) == "cis-Moll");
}

void testEffects() {
    // Bildoperationen direkt: 4×2-Bild, links rot, rechts blau
    const int w = 4, h = 2;
    std::vector<std::uint8_t> img(w * h * 4);
    auto fill = [&] {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* p = img.data() + (y * w + x) * 4;
                p[0] = x < 2 ? 200 : 20;
                p[1] = 40;
                p[2] = x < 2 ? 20 : 200;
                p[3] = 255;
            }
    };
    fill();
    applyMirror(img, w, h, true, false);
    CHECK(img[0] == 20 && img[2] == 200);           // links jetzt blau
    fill();
    applyInvert(img, w, h);
    CHECK(img[0] == 55 && img[1] == 215 && img[3] == 255);
    fill();
    applyPixelate(img, w, h, 4);
    CHECK(img[0] == img[(w - 1) * 4] && std::abs(img[0] - 110) <= 1);   // ein Block = Mittelwert
    fill();
    applySepia(img, w, h, 1.0);
    CHECK(img[0] > img[1] && img[1] > img[2]);      // warmer Braunton
    fill();
    applyRgbSplit(img, w, h, 1);
    CHECK(img[1 * 4] == 20);                        // Rot von rechts nachgerückt
    std::vector<std::uint8_t> gray(64 * 64 * 4, 128);
    applyGrain(gray, 64, 64, 1.0, 7);
    int differs = 0;
    for (std::size_t i = 0; i < gray.size(); i += 4) differs += gray[i] != 128;
    CHECK(differs > 64 * 64 / 2);
    std::vector<std::uint8_t> edge(64 * 64 * 4, 255);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x)
            for (int c = 0; c < 3; ++c) edge[(y * 64 + x) * 4 + c] = 60;
    auto sharp = edge;
    applySharpen(sharp, 64, 64, 1.0);
    CHECK(sharp[(32 * 64 + 30) * 4] < 60);          // dunkle Seite an der Kante dunkler
    auto glow = edge;
    applyGlow(glow, 64, 64, 1.0);
    CHECK(glow[(32 * 64 + 29) * 4] > 60);           // Licht strahlt in die dunkle Seite

    // Effekte bleiben beim Speichern erhalten
    Project p = baseProject();
    const std::string v = videoTrack(p);
    const std::string t = p.addTitleClip(v, 0);
    Clip* c = p.findClip(t);
    c->fx.sepia = 0.5;
    c->fx.mirrorH = true;
    c->fx.grain = 0.25;
    c->transition = TransitionType::Zoom;
    Project back;
    CHECK(Project::fromJson(p.toJson(), {}, back));
    const Clip* bc = back.findClip(t);
    CHECK(bc && bc->fx.sepia == 0.5 && bc->fx.mirrorH && bc->fx.grain == 0.25 && !bc->fx.isNeutral());
}

// Spur im Speicher: Klicks im Takt, darunter ein Sinus
std::shared_ptr<DjTrack> makeDjTrack(double bpm, double seconds, double tone, bool stems = false) {
    auto t = std::make_shared<DjTrack>();
    t->sampleRate = kRate;
    t->title = "Test";
    const std::size_t n = static_cast<std::size_t>(seconds * kRate);
    t->mix.assign(n * 2, 0.0f);
    const double beat = 60.0 / bpm;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(i) / kRate;
        const double ph = std::fmod(s, beat);
        const float v = static_cast<float>(0.2 * std::sin(2 * kPi * tone * s) + (ph < 0.01 ? 0.5 * std::sin(2 * kPi * 3000 * s) : 0.0));
        t->mix[i * 2] = t->mix[i * 2 + 1] = v;
    }
    t->bpm = bpm;
    t->firstBeat = 0.0;
    if (stems) {   // „Gesang“ = Sinus, „Schlagzeug“ = Klicks
        for (auto& st : t->stems) st.assign(n * 2, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const double s = static_cast<double>(i) / kRate;
            const float voc = static_cast<float>(0.2 * std::sin(2 * kPi * tone * s));
            t->stems[0][i * 2] = t->stems[0][i * 2 + 1] = voc;
            t->stems[1][i * 2] = t->stems[1][i * 2 + 1] = t->mix[i * 2] - voc;
        }
        t->hasStems = true;
    }
    computeDjWaveform(*t);
    return t;
}

void testDj() {
    DjMixer dj(kRate);
    auto a = makeDjTrack(120, 30, 440, true);
    auto b = makeDjTrack(125, 30, 330);
    CHECK(!a->wave.empty() && a->wave.size() == static_cast<std::size_t>(30 * DjTrack::kWaveRate));
    CHECK(camelotKey(9, true) == "8A" && camelotKey(0, false) == "8B" && camelotKey(7, false) == "9B");
    dj.load(0, a);
    dj.load(1, b);
    std::vector<float> out(1024 * 2);
    auto run = [&](double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * kRate / 1024); ++i) dj.render(out.data(), 1024);
    };
    // Abspielen und Tempo
    dj.play(0);
    run(1.0);
    CHECK(std::abs(dj.position(0) - 1.0) < 0.03);
    dj.setTempo(0, 10.0);
    CHECK(std::abs(dj.currentBpm(0) - 132.0) < 1e-9);
    const double p0 = dj.position(0);
    run(1.0);
    CHECK(std::abs(dj.position(0) - p0 - 1.1) < 0.03);
    // Sync: B übernimmt Tempo und Phase von A
    dj.seek(1, 3.37);
    dj.sync(1);
    CHECK(std::abs(dj.currentBpm(1) - dj.currentBpm(0)) < 1e-6);
    auto phase = [&](int deck, double bpm) { double x = dj.position(deck) / (60.0 / bpm); return x - std::floor(x); };
    double diff = std::abs(phase(0, 120) - phase(1, 125));
    CHECK(std::min(diff, 1 - diff) < 0.01);
    dj.setSyncLock(1, true);
    dj.play(1);
    dj.setTempo(0, 0.0);
    run(0.1);
    CHECK(std::abs(dj.currentBpm(1) - 120.0) < 1e-6);   // folgt dem Master
    CHECK(dj.masterDeck() == 0);
    // Crossfader ganz nach links: nur A (440 Hz), B (330 Hz) stumm
    dj.setCrossfader(-1.0);
    std::vector<float> mono;
    for (int i = 0; i < 20; ++i) {
        dj.render(out.data(), 1024);
        mono.insert(mono.end(), out.begin(), out.end());
    }
    CHECK(power(mono, 440) > 50 * power(mono, 330));
    // EQ: Tiefen und Mitten aus → Sinus fast weg
    dj.setEq(0, 0, -1.0);
    dj.setEq(0, 1, -1.0);
    run(0.2);
    mono.clear();
    for (int i = 0; i < 10; ++i) {
        dj.render(out.data(), 1024);
        mono.insert(mono.end(), out.begin(), out.end());
    }
    const double eqOff = power(mono, 440);
    dj.setEq(0, 0, 0.0);
    dj.setEq(0, 1, 0.0);
    run(0.2);
    mono.clear();
    for (int i = 0; i < 10; ++i) {
        dj.render(out.data(), 1024);
        mono.insert(mono.end(), out.begin(), out.end());
    }
    CHECK(power(mono, 440) > 20 * eqOff);
    // Stems: Gesang aus → kein 440 Hz mehr
    dj.setStem(0, DjStem::Vocals, false);
    run(0.1);
    mono.clear();
    for (int i = 0; i < 10; ++i) {
        dj.render(out.data(), 1024);
        mono.insert(mono.end(), out.begin(), out.end());
    }
    CHECK(power(mono, 440) * 100 < power(mono, 3000) + 1e-9 || power(mono, 440) < 1e-6);
    dj.setStem(0, DjStem::Vocals, true);
    // Loop über 4 Schläge (2 s bei 120 BPM) hält die Position fest
    dj.setLoop(0, 4);
    CHECK(dj.looping(0) && dj.loopBeats(0) == 4.0);
    const double start = dj.position(0);
    run(5.0);
    CHECK(dj.position(0) >= std::floor(start / 0.5) * 0.5 - 0.01 && dj.position(0) < start + 2.1);
    dj.setLoop(0, 4);   // gleiche Länge noch einmal = aus
    CHECK(!dj.looping(0));
    // Hot Cues (Quantize: auf den Schlag)
    dj.seek(0, 10.13);
    dj.setHotCue(0, 2);
    CHECK(std::abs(dj.hotCue(0, 2) - 10.0) < 1e-9);
    dj.seek(0, 20.0);
    dj.jumpHotCue(0, 2);
    CHECK(std::abs(dj.position(0) - 10.0) < 1e-6);
    dj.clearHotCue(0, 2);
    CHECK(dj.hotCue(0, 2) < 0);
    dj.beatJump(0, 4);
    CHECK(std::abs(dj.position(0) - 12.0) < 1e-6);
    // Cue: angehalten setzen, beim Abspielen zurückspringen
    dj.pause(0);
    dj.seek(0, 5.02);
    dj.cue(0);
    CHECK(std::abs(dj.cuePoint(0) - 5.0) < 1e-9);
    dj.play(0);
    run(0.5);
    dj.cue(0);
    CHECK(!dj.playing(0) && std::abs(dj.position(0) - 5.0) < 1e-6);
    // Key Lock rendert weiter Ton
    dj.play(0);
    dj.setKeyLock(0, true);
    dj.setTempo(0, 8.0);
    double energy = 0;
    for (int i = 0; i < 30; ++i) {
        dj.render(out.data(), 1024);
        for (float v : out) energy += v * v;
    }
    CHECK(energy > 1.0);
    dj.setKeyLock(0, false);
    // Mitschnitt und Bremse
    std::size_t recorded = 0;
    dj.setRecorder([&](const float*, std::size_t n) { recorded += n; });
    dj.setFx(DjFx::Brake, 1.0, 1.0, 0);
    dj.setFxOn(true);
    run(1.0);
    CHECK(!dj.playing(0) && !dj.fxOn());
    CHECK(recorded >= static_cast<std::size_t>(kRate * 0.9));
    // Echo auf den Master verändert das Signal
    dj.setFx(DjFx::Echo, 0.5, 0.8, 2);
    dj.setFxOn(true);
    run(0.5);
    CHECK(dj.fxOn());
    dj.setFxOn(false);
}

// KI-Freistellung: helles Objekt in der Mitte vor ruhigem Hintergrund
void testCutout() {
    const char* env = std::getenv("LOGGX_TEST_MODEL_DIR");
    const std::string model = env ? std::string(env) + "/u2netp.onnx" : std::string();
    CHECK(findStemModel("cutout") && findStemModel("cutout-human"));
    if (!aiSeparationAvailable() || model.empty() || !fs::exists(model)) {
        std::printf("Freistellung übersprungen (kein Modell)\n");
        return;
    }
    const int w = 256, h = 192;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &img[(static_cast<std::size_t>(y) * w + x) * 4];
            const double dx = (x - w / 2.0) / 50.0, dy = (y - h / 2.0) / 65.0;
            const bool in = dx * dx + dy * dy < 1.0;
            p[0] = in ? 230 : 40 + (x % 7);
            p[1] = in ? 120 : 70;
            p[2] = in ? 40 : 90;
            p[3] = 255;
        }
    std::vector<std::uint8_t> alpha;
    std::string error;
    setCutoutModel("");
    CHECK(!cutoutMask(img.data(), w, h, alpha, &error));   // ohne Modell: Fehler statt Absturz
    setCutoutModel(model);
    CHECK(cutoutMask(img.data(), w, h, alpha, &error));
    if (!error.empty()) std::printf("Freistellung: %s\n", error.c_str());
    CHECK(alpha.size() == static_cast<std::size_t>(w) * h);
    const int centre = alpha[static_cast<std::size_t>(h / 2) * w + w / 2];
    const int corner = alpha[5 * w + 5];
    std::printf("Freistellung: Mitte %d, Ecke %d\n", centre, corner);
    CHECK(centre > 200 && corner < 40);
    setCutoutModel("");
}

// KI-Trennung nur, wenn ein Modell bereitliegt (CI lädt es herunter):
// LOGGX_TEST_MODEL_DIR mit kuielab_a_vocals.onnx, optional song.mp3.
void testAi(const fs::path& dir) {
    CHECK(stemModels().size() >= 4 && findStemModel("vocals-hq") && findStemModel("bass"));
    const char* env = std::getenv("LOGGX_TEST_MODEL_DIR");
    if (!aiSeparationAvailable() || !env) {
        std::printf("KI-Test übersprungen (%s)\n", aiSeparationAvailable() ? "kein Modell" : "ohne ONNX Runtime gebaut");
        return;
    }
    const StemModelInfo& model = *findStemModel("vocals");
    const std::string modelPath = (fs::path(env) / model.file).string();
    // Synthetisch: läuft durch, Länge stimmt, keine NaN
    std::vector<float> mix(44100 * 4 * 2);
    for (std::size_t i = 0; i < mix.size() / 2; ++i)
        mix[i * 2] = mix[i * 2 + 1] = static_cast<float>(0.3 * std::sin(2 * kPi * 220.0 * i / 44100.0));
    std::vector<float> stem;
    std::string error;
    int calls = 0;
    CHECK(runStemModel(model, modelPath, mix, stem, false, [&](double) { ++calls; }, nullptr, &error));
    CHECK(stem.size() == mix.size() && calls > 0);
    bool finite = true;
    for (float v : stem) finite = finite && std::isfinite(v);
    CHECK(finite);
    // Echter Song: Gesang hat einen deutlichen, aber nicht vollen Anteil
    const fs::path song = fs::path(env) / "song.mp3";
    if (fs::exists(song)) {
        const SeparationResult r = separateStemsAi(song.string(), {StemJob{&model, modelPath, (dir / "ki_gesang.wav").string()}},
                                                   (dir / "ki_inst.wav").string());
        std::printf("KI-Trennung: Gesangsanteil %.0f %%\n", r.vocalShare * 100);
        CHECK(r.ok && r.vocalShare > 0.05 && r.vocalShare < 0.8);
        MediaAsset a;
        CHECK(probeMedia((dir / "ki_gesang.wav").string(), a) && a.hasAudio);
    }
}

// Kopfhörer (Vorhören vor dem Fader), Scratchen, Beatgrid je Deck
void testDjTransitions() {
    DjMixer dj(kRate);
    auto a = makeDjTrack(120, 60, 440);
    auto b = makeDjTrack(124, 60, 330);
    dj.load(0, a);
    dj.load(1, b);
    std::vector<float> out(512 * 2);
    auto run = [&](double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * kRate / 512); ++i) dj.render(out.data(), 512);
    };
    // Weiche Regler: harter Fader-Sprung erzeugt keinen Knacks (Rampe über den Block)
    dj.play(0);
    dj.setCrossfader(-1.0);
    run(0.5);
    dj.setFader(0, 0.0);
    dj.render(out.data(), 512);
    float maxStep = 0;
    for (std::size_t i = 2; i < out.size(); i += 2) maxStep = std::max(maxStep, std::abs(out[i] - out[i - 2]));
    CHECK(maxStep < 0.08f);   // Sinus 440 Hz bei 48 kHz: Schritt ≤ ~0,06
    dj.setFader(0, 1.0);
    run(0.2);
    // Phasenregelung: B mit Sync-Lock, um 0,3 Schläge daneben gestartet → rastet ein
    dj.seek(1, 5.0 + 0.3 * 60.0 / 124.0);
    dj.setSyncLock(1, true);
    dj.play(1);
    run(0.1);
    const double before = std::abs(dj.phaseOffset(1));
    run(20.0);
    const double after = std::abs(dj.phaseOffset(1));
    std::printf("Phasenregelung: %.3f → %.4f Schläge\n", before, after);
    CHECK(before > 0.1 && after < 0.01);
    dj.pause(1);
    // Automatischer Übergang A → B über 8 Schläge, Bass-Tausch, startet auf dem nächsten Takt
    dj.setSyncLock(1, false);
    dj.seek(1, 0.0);
    dj.setCrossfader(-1.0);
    CHECK(dj.startTransition(1, 8, 1));
    CHECK(dj.transitionTarget() == 1 && dj.transitionProgress() == 0.0);
    run(2.05);   // höchstens ein Takt (2 s bei 120 BPM) Wartezeit
    CHECK(dj.playing(1));
    run(2.0);    // Mitte (8 Schläge = 4 s)
    const double mid = dj.transitionProgress();
    CHECK(mid > 0.3 && mid < 0.8 && std::abs(dj.crossfader()) < 0.8);
    run(2.5);
    CHECK(dj.transitionProgress() < 0 && dj.crossfader() == 1.0 && !dj.playing(0) && dj.playing(1));
    CHECK(dj.eq(0, 0) == 0.0 && dj.eq(1, 0) == 0.0);   // EQ wieder wie vorher
    CHECK(std::abs(dj.currentBpm(1) - 120.0) < 0.01);   // Tempo angeglichen
    // Filter- und Echo-Out-Übergang zurück, Abbrechen stellt alles zurück
    dj.seek(0, 0.0);
    CHECK(dj.startTransition(0, 4, 2, false));
    run(1.0);
    CHECK(dj.filter(1) > 0.1);
    dj.cancelTransition();
    CHECK(dj.filter(1) == 0.0 && dj.transitionProgress() < 0);
    CHECK(dj.startTransition(0, 4, 3, false));
    run(2.5);
    run(0.1);
    CHECK(dj.transitionProgress() < 0 && dj.crossfader() == -1.0);
    // Crossfader-Kurven
    dj.setCrossfaderCurve(2);
    CHECK(dj.crossfaderCurve() == 2);
    dj.setCrossfaderCurve(0);
}

void testDjCueAndJog() {
    DjMixer dj(kRate);
    auto a = makeDjTrack(120, 20, 440);
    dj.load(0, a);
    std::vector<float> out(1024 * 2), cue(1024 * 2);
    auto energy = [](const std::vector<float>& v) {
        double e = 0;
        for (float x : v) e += static_cast<double>(x) * x;
        return e / static_cast<double>(v.size());
    };
    dj.play(0);
    dj.setFader(0, 0.0);
    dj.setPfl(0, true);
    dj.setHeadphones(0.0, 1.0);
    for (int i = 0; i < 4; ++i) dj.render(out.data(), 1024, cue.data());
    CHECK(energy(out) < 1e-8);     // Fader zu: Master stumm
    CHECK(energy(cue) > 1e-3);     // Kopfhörer hört trotzdem
    dj.setPfl(0, false);
    dj.render(out.data(), 1024, cue.data());
    CHECK(energy(cue) < 1e-8);
    // Scratchen: angehalten, Jog oben bewegt die Position
    dj.pause(0);
    const double p0 = dj.position(0);
    dj.setScratch(0, true);
    dj.jogScratch(0, 0.25);
    for (int i = 0; i < 40; ++i) dj.render(out.data(), 1024);
    CHECK(std::abs(dj.position(0) - p0 - 0.25) < 0.03);
    dj.jogScratch(0, -0.1);   // rückwärts
    for (int i = 0; i < 40; ++i) dj.render(out.data(), 1024);
    CHECK(std::abs(dj.position(0) - p0 - 0.15) < 0.03);
    dj.setScratch(0, false);
    // Beatgrid ändern: BPM ×2 nur für dieses Deck
    dj.setBeatGrid(0, 240.0, 0.0);
    CHECK(std::abs(dj.currentBpm(0) - 240.0) < 1e-9 && std::abs(a->bpm - 120.0) < 1e-9);
    dj.setHotCuePoint(0, 3, 2.5);
    CHECK(std::abs(dj.hotCue(0, 3) - 2.5) < 1e-12);
    dj.setHotCuePoint(0, 3, 999.0);   // hinter dem Ende: leer
    CHECK(dj.hotCue(0, 3) < 0);
    // Key Shift: Tempo bleibt, Ton klingt weiter (Rubber Band)
    dj.setFader(0, 1.0);
    dj.seek(0, 1.0);
    dj.setKeyShift(0, 3);
    CHECK(dj.keyShift(0) == 3);
    dj.play(0);
    for (int i = 0; i < 20; ++i) dj.render(out.data(), 1024);
    dj.render(out.data(), 1024);
    CHECK(energy(out) > 1e-4);
    CHECK(std::abs(dj.position(0) - 1.0 - 21.0 * 1024 / kRate) < 0.1);
    dj.setKeyShift(0, 40);
    CHECK(dj.keyShift(0) == 12);
    dj.pause(0);
    // Sampler: klingt auch ohne laufendes Deck und hört am Ende auf
    auto clap = std::make_shared<const std::vector<float>>(synthDjSample(6, kRate));
    CHECK(clap->size() > 1000 && std::string(djSampleName(6)) == "Clap");
    dj.setSample(2, clap);
    CHECK(dj.hasSample(2) && !dj.hasSample(3));
    dj.triggerSample(2);
    dj.render(out.data(), 1024);
    CHECK(energy(out) > 1e-4 && dj.samplePlaying(2));
    for (int i = 0; i < 40; ++i) dj.render(out.data(), 1024);
    CHECK(!dj.samplePlaying(2));
    for (int k = 0; k < 8; ++k) CHECK(!synthDjSample(k, kRate).empty());
    // Alle Effekte: laufen, bleiben endlich, verändern den Klang
    dj.setKeyShift(0, 0);
    dj.setFader(0, 1.0);
    dj.play(0);
    for (int type = 3; type < kDjFxCount; ++type) {
        if (type == static_cast<int>(DjFx::Backspin)) continue;   // eigener Test unten
        dj.setFxOn(false);
        dj.seek(0, 2.0);
        std::vector<float> dry(1024 * 2);
        dj.render(dry.data(), 1024);
        dj.seek(0, 2.0);
        dj.setFx(static_cast<DjFx>(type), 0.5, 0.9, 2);
        dj.setFxOn(true);
        double diff = 0;
        bool finite = true;
        for (int i = 0; i < 30; ++i) {
            dj.render(out.data(), 1024);
            for (float x : out) finite = finite && std::isfinite(x);
        }
        dj.seek(0, 2.0);
        dj.render(out.data(), 1024);
        for (std::size_t i = 0; i < out.size(); ++i) diff += std::abs(out[i] - dry[i]);
        CHECK(finite);
        CHECK(diff > 1.0 || type == static_cast<int>(DjFx::Trans));   // Trans: Tor kann gerade offen sein
        CHECK(energy(out) > 1e-6);
    }
    dj.setFxOn(false);
    // Backspin: läuft rückwärts aus und stoppt das Deck
    dj.seek(0, 4.0);
    dj.play(0);
    dj.setFx(DjFx::Backspin, 0.5, 1.0, 0);
    dj.setFxOn(true);
    dj.render(out.data(), 256);
    CHECK(dj.position(0) < 4.0);
    for (int i = 0; i < 80 && dj.playing(0); ++i) dj.render(out.data(), 1024);
    CHECK(!dj.playing(0));
    dj.setFxOn(false);
    dj.play(0);
}

// Eingebaute Stem-Engine: Schlagzeug und Bass landen im richtigen Stem
// VST3-Klassen-Id wie aus der Suche (Byte-Reihenfolge ist je System verschieden)
std::string vst3Id(const std::string& name) {
    for (const auto& i : loggx::scanVst3(LOGGX_TEST_VST3))
        if (i.name == name) return i.id;
    return {};
}

void testPluginHost() {
    // CLAP und VST3: laden, Parameter, Verarbeitung, Zustand, Suche
    struct Case {
        const char* format;
        const char* path;
        const char* id;
        std::string param;
        double half;   // Parameterwert für Faktor 0,5
    };
    const Case cases[] = {{"clap", LOGGX_TEST_CLAP, "de.loggx.test.gain", "7", 0.5},
                          {"vst3", LOGGX_TEST_VST3, "", "3", 0.25}};
    for (const Case& c : cases) {
        loggx::PluginInfo info;
        info.format = c.format;
        info.path = c.path;
        info.id = std::string(c.format) == "vst3" ? vst3Id("LX Test Gain VST3") : c.id;
        CHECK(!info.id.empty());
        std::string err;
        auto p = loggx::loadPlugin(info, 48000, 512, &err);
        if (!p) std::fprintf(stderr, "%s: %s\n", c.format, err.c_str());
        CHECK(p != nullptr);
        if (!p) continue;
        const auto params = p->params();
        CHECK(params.size() == 1 && params[0].id == c.param && params[0].name == "Gain");
        std::vector<float> buf(1024 * 2);
        auto fill = [&] { for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<float>(std::sin(i * 0.01)) * 0.5f; };
        fill();
        const float ref = buf[200];
        p->process(buf.data(), 1024);
        CHECK(std::abs(buf[200] - ref) < 1e-5f);          // Standard: Faktor 1
        p->setParam(c.param, c.half);
        fill();
        p->process(buf.data(), 1024);
        CHECK(std::abs(buf[200] - ref * 0.5f) < 1e-5f);   // halbiert
        const std::string state = p->saveState();
        CHECK(!state.empty());
        auto q = loggx::loadPlugin(info, 48000, 512, &err);
        CHECK(q && q->loadState(state));
        fill();
        q->process(buf.data(), 1024);
        CHECK(std::abs(buf[200] - ref * 0.5f) < 1e-5f);   // Zustand übernommen
        CHECK(std::abs(q->params()[0].value - c.half) < 1e-9);
        // Große Blöcke (größer als maxBlock) werden aufgeteilt
        std::vector<float> big(5000 * 2, 0.25f);
        q->process(big.data(), 5000);
        CHECK(std::abs(big[9999] - 0.125f) < 1e-6f);
    }
    // Im Mix: Instrumentspur mit Test-CLAP (Faktor 0,5) → halbe Amplitude
    {
        loggx::Project proj = loggx::Project::createDefault();
        std::string inst;
        for (auto& t : proj.tracks)
            if (t.kind == loggx::TrackKind::Instrument) inst = t.id;
        const std::string mid = proj.addMidiClip(inst, 0, 2 * kSecond);
        proj.findClip(mid)->notes.push_back({60, 0, 1'500'000, 0.3});
        auto render = [](const loggx::Project& pr) {
            loggx::AudioMixer mx;
            mx.setProject(std::make_shared<const loggx::Project>(pr));
            std::vector<float> out(48000 * 2);
            mx.render(0, out.data(), 48000);
            double e = 0;
            for (float v : out) e += v * v;
            return std::sqrt(e);
        };
        const double plain = render(proj);
        loggx::PluginRef ref;
        ref.uid = "p1";
        ref.format = "clap";
        ref.pluginId = "de.loggx.test.gain";
        ref.path = LOGGX_TEST_CLAP;
        ref.name = "LX Test Gain";
        ref.params["7"] = 0.5;
        proj.findTrack(inst)->plugins.push_back(ref);
        const double withPlugin = render(proj);
        CHECK(plain > 0.1 && std::abs(withPlugin / plain - 0.5) < 0.01);
        proj.findTrack(inst)->plugins[0].bypass = true;
        CHECK(std::abs(render(proj) / plain - 1.0) < 0.01);
        // Speichern/Laden: Parameter und Zustand (base64) bleiben erhalten
        proj.findTrack(inst)->plugins[0].state = std::string("\0\x01\xff\x80Zustand", 11);
        loggx::Project q;
        std::string e;
        CHECK(loggx::Project::fromJson(proj.toJson(), {}, q, &e));
        const auto& pr = q.findTrack(inst)->plugins.at(0);
        CHECK(pr.params.at("7") == 0.5 && pr.bypass && pr.path == LOGGX_TEST_CLAP && pr.state == proj.findTrack(inst)->plugins[0].state);
        std::printf("Plugin im Mix: %.3f → %.3f\n", plain, withPlugin);
    }
    // Instrument-Plugins: Instrumentspur spielt ihre MIDI-Noten über das Plugin
    for (const auto& [format, path, id] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"clap", LOGGX_TEST_CLAP, "de.loggx.test.synth"}, {"vst3", LOGGX_TEST_VST3, vst3Id("LX Test Synth VST3")}}) {
        loggx::Project proj = loggx::Project::createDefault();
        std::string inst;
        for (auto& t : proj.tracks)
            if (t.kind == loggx::TrackKind::Instrument) inst = t.id;
        const std::string mid = proj.addMidiClip(inst, 0, 2 * kSecond);
        proj.findClip(mid)->notes.push_back({69, 100'000, 400'000, 1.0});   // A4 = 440 Hz, 0,1–0,5 s
        loggx::PluginRef ref;
        ref.uid = "inst-" + format;
        ref.format = format;
        ref.pluginId = id;
        ref.path = path;
        ref.name = "Synth";
        proj.findTrack(inst)->instrumentPlugin = ref;
        loggx::AudioMixer mx;
        mx.setProject(std::make_shared<const loggx::Project>(proj));
        std::vector<float> out(48000 * 2);
        for (int b = 0; b < 48000; b += 512) mx.render(b, out.data() + b * 2, std::min(512, 48000 - b));   // in Blöcken wie live
        auto rms = [&](int a, int e) {
            double sum = 0;
            for (int i = a; i < e; ++i) sum += out[static_cast<std::size_t>(i) * 2] * out[static_cast<std::size_t>(i) * 2];
            return std::sqrt(sum / (e - a));
        };
        int crossings = 0;
        for (int i = 9600; i < 19200; ++i) crossings += (out[static_cast<std::size_t>(i - 1) * 2] < 0) != (out[static_cast<std::size_t>(i) * 2] < 0);
        const double hz = crossings / 2.0 / 0.2;
        std::printf("Instrument-Plugin %s: vorher %.4f, Note %.3f (%.0f Hz), danach %.5f\n", format.c_str(), rms(0, 4000), rms(6000, 22000), hz, rms(26000, 48000));
        CHECK(mx.pluginErrors().empty());
        CHECK(rms(0, 4000) < 1e-4 && rms(6000, 22000) > 0.05 && rms(26000, 48000) < 1e-4);
        CHECK(std::abs(hz - 440.0) < 6.0);
        // Speichern/Laden behält das Instrument
        loggx::Project q;
        std::string e;
        CHECK(loggx::Project::fromJson(proj.toJson(), {}, q, &e) && q.findTrack(inst)->instrumentPlugin.pluginId == id);
    }
    const auto found = loggx::scanPlugins({LOGGX_TEST_PLUGDIR});
    int clap = 0, vst3 = 0;
    for (const auto& f : found) {
        if (f.format == "clap" && f.id == "de.loggx.test.gain") ++clap;
        if (f.format == "vst3" && f.name == "LX Test Gain VST3") ++vst3;
        if (f.name == "LX Test Synth") CHECK(f.instrument);   // VST3: nur über PClassInfo2 (hat das Test-Plugin nicht)
    }
    std::printf("Plugins: %zu gefunden (Test-CLAP %d, Test-VST3 %d)\n", found.size(), clap, vst3);
    CHECK(clap == 1 && vst3 == 1);
#ifdef __APPLE__
    // Audio Unit: Apples eingebauter Tiefpass (aufx:lpas:appl)
    int au = 0;
    for (const auto& f : found) au += f.format == "au";
    loggx::PluginInfo lp;
    lp.format = "au";
    lp.id = "aufx:lpas:appl";
    std::string auErr;
    auto a = loggx::loadPlugin(lp, 48000, 512, &auErr);
    if (!a) std::fprintf(stderr, "AU: %s\n", auErr.c_str());
    CHECK(a != nullptr && au > 0);
    if (a) {
        CHECK(!a->params().empty());
        a->setParam("0", 200.0);   // Grenzfrequenz 200 Hz
        std::vector<float> hi(4096 * 2);
        for (std::size_t i = 0; i < 4096; ++i) hi[i * 2] = hi[i * 2 + 1] = static_cast<float>(std::sin(2 * 3.14159265 * 8000.0 * i / 48000.0));
        a->process(hi.data(), 4096);
        float peak = 0;
        for (std::size_t i = 2048 * 2; i < hi.size(); ++i) peak = std::max(peak, std::abs(hi[i]));
        std::printf("AU: %d gefunden, 8 kHz nach Tiefpass: %.3f\n", au, peak);
        CHECK(peak < 0.2f);
        CHECK(!a->saveState().empty());
    }
#endif
}

// HTDemucs: STFT/iSTFT wie PyTorch (Rundweg verlustfrei); mit LOGGX_DEMUCS_DIR
// (Modell + Referenz aus dem Release „ki-modelle“) Vergleich mit dem Original.
void testDemucs() {
    const std::size_t n = 343980;
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = static_cast<float>(std::sin(i * 0.013) * 0.5 + std::sin(i * 0.31) * 0.2 + std::sin(i * 2.9) * 0.05);
    std::vector<std::complex<float>> z;
    demucs::spec(x.data(), n, z);
    CHECK(z.size() == 2048u * 336u);
    std::vector<float> y;
    demucs::ispec(z, 336, n, y);
    double err = 0;
    // Ränder sind (wie im Original) durch die Null-Frames nicht exakt
    for (std::size_t i = 4096; i < n - 4096; ++i) err = std::max(err, static_cast<double>(std::abs(x[i] - y[i])));
    std::printf("Demucs STFT-Rundweg: max. Fehler %.2e\n", err);
    CHECK(err < 1e-4);
    const char* dir = std::getenv("LOGGX_DEMUCS_DIR");
    if (!dir || !aiSeparationAvailable()) return;
    const std::string d = dir;
    std::vector<float> mix;
    {
        AudioReader r;
        CHECK(r.open(d + "/demucs_ref_mix.wav", 44100));
        std::vector<float> buf(8192 * 2);
        for (;;) {
            const std::size_t got = r.read(buf.data(), 8192);
            mix.insert(mix.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got * 2));
            if (got < 8192) break;
        }
    }
    std::vector<float> src[4];
    std::string error;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(runDemucs(d + "/htdemucs.onnx", mix, src, {}, nullptr, &error));
    std::printf("Demucs: %.1f s für %.1f s Audio %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
                mix.size() / 2 / 44100.0, error.c_str());
    const char* names[4] = {"drums", "bass", "other", "vocals"};
    for (int s = 0; s < 4; ++s) {
        AudioReader r;
        CHECK(r.open(d + "/demucs_ref_" + names[s] + ".wav", 44100));
        std::vector<float> ref(src[s].size()), buf(8192 * 2);
        std::size_t at = 0;
        for (;;) {
            const std::size_t got = r.read(buf.data(), 8192);
            for (std::size_t i = 0; i < got * 2 && at < ref.size(); ++i) ref[at++] = buf[i];
            if (got < 8192) break;
        }
        double e = 0, sig = 0;
        for (std::size_t i = 0; i < ref.size(); ++i) {
            sig += static_cast<double>(ref[i]) * ref[i];
            const double dd = ref[i] - src[s][i];
            e += dd * dd;
        }
        const double sdr = 10 * std::log10((sig + 1e-12) / (e + 1e-12));
        std::printf("  %-6s Übereinstimmung mit PyTorch: %.1f dB\n", names[s], sdr);
        CHECK(sdr > 40);
    }
}

void testMulticam() {
    using loggx::Project;
    Project p = Project::createDefault();
    auto cam = [&](const char* name, TimeUs dur) {
        loggx::MediaAsset a;
        a.path = std::string("/tmp/") + name;
        a.name = name;
        a.hasVideo = a.hasAudio = true;
        a.width = 1920;
        a.height = 1080;
        a.duration = dur;
        return p.addAsset(a);
    };
    const auto a = cam("a.mp4", 60 * kSecond), b = cam("b.mp4", 50 * kSecond), c = cam("c.mp4", 70 * kSecond);
    // b beginnt 5 s nach a, c beginnt 2 s vor a
    const std::string g = p.createMulticam("Konzert", {{a, 0, ""}, {b, 5 * kSecond, ""}, {c, -2 * kSecond, ""}}, 0);
    CHECK(!g.empty());
    const auto* grp = p.findMulticam(g);
    CHECK(grp && grp->start == 2 * kSecond && grp->angles[1].name == "Kamera 2");
    CHECK(p.tracks.front().id == grp->programTrack);
    // Programm: 0–2 s Kamera 3 (nur c läuft), ab 2 s Kamera 1, nach dem Ende von a wieder Kamera 3
    CHECK(p.angleAt(g, 1 * kSecond) == 2 && p.angleAt(g, 10 * kSecond) == 0 && p.angleAt(g, 63 * kSecond) == 2);
    // Umschalten bei 20 s auf Kamera 2: Quelle stimmt mit dem Versatz
    CHECK(p.switchAngle(g, 1, 20 * kSecond));
    CHECK(p.angleAt(g, 19 * kSecond) == 0 && p.angleAt(g, 21 * kSecond) == 1);
    const loggx::Track* pt = p.findTrack(grp->programTrack);
    const loggx::Clip* sw = nullptr;
    for (const auto& cl : pt->clips)
        if (cl.angle == 1) sw = &cl;
    CHECK(sw && sw->start == 20 * kSecond && sw->in == 13 * kSecond && sw->audioOff);   // 20 − 2 − 5
    // Kamera 2 endet bei 2 + 5 + 50 = 57 s → danach wieder die alte Kamera
    CHECK(p.angleAt(g, 58 * kSecond) == 0);
    // Zurück auf Kamera 1 bei 20 s: Clips werden wieder zusammengefasst
    const std::size_t before = pt->clips.size();
    CHECK(p.switchAngle(g, 0, 20 * kSecond));
    CHECK(p.findTrack(grp->programTrack)->clips.size() < before);
    // Kamera ohne Aufnahme an der Stelle → Fehler
    std::string err;
    CHECK(!p.switchAngle(g, 1, 3 * kSecond, &err) && !err.empty());
    // Speichern und laden
    Project q;
    std::string e2;
    CHECK(Project::fromJson(p.toJson(), {}, q, &e2));
    CHECK(q.multicams.size() == 1 && q.multicams[0].angles.size() == 3 && q.angleAt(g, 30 * kSecond) == 0);
    CHECK(q.switchAngle(g, 2, 30 * kSecond) && q.angleAt(g, 31 * kSecond) == 2);
}

void testMastering() {
    constexpr double kPi = 3.14159265358979323846;
    const int rate = 48000;
    // 10 s leiser Mix: Sinus 220 Hz + Bass-Puls, um −30 dBFS
    std::vector<float> mix(static_cast<std::size_t>(rate) * 10 * 2);
    for (std::size_t i = 0; i < mix.size() / 2; ++i) {
        const double t = static_cast<double>(i) / rate;
        const float v = static_cast<float>(0.03 * std::sin(2 * kPi * 220 * t) +
                                           (std::fmod(t, 0.5) < 0.1 ? 0.06 * std::sin(2 * kPi * 60 * t) : 0.0));
        mix[i * 2] = mix[i * 2 + 1] = v;
    }
    // Referenz: 1 kHz Sinus mit 0 dBFS Spitze ≈ −3,0 LUFS
    std::vector<float> ref(static_cast<std::size_t>(rate) * 3 * 2);
    for (std::size_t i = 0; i < ref.size() / 2; ++i)
        ref[i * 2] = ref[i * 2 + 1] = static_cast<float>(std::sin(2 * kPi * 1000 * i / rate));
    const double refL = loggx::measureLufs(ref, rate);
    CHECK(std::abs(refL - (-3.0 + 3.0)) < 1.0 || std::abs(refL - (-3.0)) < 1.0);   // Stereo: +3 dB
    const double before = loggx::measureLufs(mix, rate);
    const auto r = loggx::masterAudio(mix, rate, loggx::MasteringPreset::Streaming);
    CHECK(std::abs(r.lufsBefore - before) < 0.01);
    CHECK(std::abs(r.lufsAfter - (-14.0)) < 1.0);
    float peak = 0;
    for (float x : mix) peak = std::max(peak, std::abs(x));
    CHECK(peak <= std::pow(10.0f, -1.0f / 20.0f) + 1e-4f);
    auto club = mix;
    const auto c = loggx::masterAudio(club, rate, loggx::MasteringPreset::Club);
    CHECK(c.lufsAfter > r.lufsAfter + 2.0);
    std::printf("Mastering: Referenz %.2f LUFS, %.1f → %.1f LUFS, Club %.1f\n", refL, r.lufsBefore, r.lufsAfter, c.lufsAfter);
}

void testDspStems() {
    constexpr int R = 44100;
    std::vector<float> st(static_cast<std::size_t>(R) * 6 * 2, 0.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> U(-1, 1);
    for (std::size_t i = 0; i < st.size() / 2; ++i) {
        const double t = static_cast<double>(i) / R;
        const float bass = 0.4f * static_cast<float>(std::sin(2 * 3.14159265358979 * 55 * t));
        // Klicks (perkussiv) alle 0,5 s
        const double ph = std::fmod(t, 0.5);
        const float hit = ph < 0.01 ? 0.8f * U(rng) * static_cast<float>(1.0 - ph / 0.01) : 0.0f;
        st[i * 2] = st[i * 2 + 1] = bass + hit;
    }
    std::vector<float> drums, bass;
    CHECK(splitHarmonicPercussive(st, R, drums, bass));
    auto energyIn = [&](const std::vector<float>& v, bool hits) {
        double e = 0;
        for (std::size_t i = 0; i < v.size() / 2; ++i) {
            const double ph = std::fmod(static_cast<double>(i) / R, 0.5);
            if ((ph < 0.01) == hits) e += static_cast<double>(v[i * 2]) * v[i * 2];
        }
        return e;
    };
    // Zwischen den Klicks: Bass-Stem trägt fast alles; in den Klicks: Drums deutlich
    CHECK(energyIn(bass, false) > 10 * energyIn(drums, false));
    CHECK(energyIn(drums, true) > 0.2 * energyIn(st, true) * 0.5);
}

// Tempo-Erkennung auf synthetischen Mustern (inkl. „krummer“ Tempi und
// DnB, das gern halbiert erkannt wird)
void testTempoDetection() {
    constexpr int R = 22050;
    constexpr double PI = 3.14159265358979;
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> U(-1, 1);
    auto kick = [&](std::vector<float>& o, double t) {
        const std::size_t s = static_cast<std::size_t>(t * R);
        for (int i = 0; i < R / 4 && s + i < o.size(); ++i) {
            const double x = i / double(R), f = 50 + 90 * std::exp(-x * 30);
            o[s + i] += static_cast<float>(0.9 * std::sin(2 * PI * f * x) * std::exp(-x * 9));
        }
    };
    auto noise = [&](std::vector<float>& o, double t, double len, double amp) {
        const std::size_t s = static_cast<std::size_t>(t * R);
        float lp = 0;
        for (int i = 0; i < R * len && s + i < o.size(); ++i) {
            const float n = U(rng), hp = n - lp;
            lp = n;
            o[s + i] += static_cast<float>(amp * hp * std::exp(-i / (R * len / 4)));
        }
    };
    struct Case { double bpm; bool dnb; };
    for (const Case c : {Case{138, false}, Case{143.75, false}, Case{124, false}, Case{174, true}}) {
        std::vector<float> o(static_cast<std::size_t>(60 * R), 0.f);
        const double b = 60 / c.bpm;
        for (int k = 0; k < static_cast<int>(58 / b); ++k) {
            const double t = 0.05 + k * b;
            if (c.dnb) {
                if (k % 4 == 0) kick(o, t);
                if (k % 4 == 2) kick(o, t + b / 2);
                if (k % 2 == 1) noise(o, t, 0.12, 0.6);
                for (int q = 0; q < 4; ++q) noise(o, t + q * b / 4, 0.03, 0.12);
            } else {
                kick(o, t);
                noise(o, t + b / 2, 0.08, 0.25);
                if (k % 2 == 1) noise(o, t, 0.15, 0.4);
            }
        }
        const BeatInfo info = detectBeatsFromSamples(o.data(), o.size(), R);
        const double phase = std::remainder(info.firstBeat / 1e6 - 0.05, b);
        std::printf("Tempo %.2f -> %.2f (Phase %.1f ms)\n", c.bpm, info.bpm, phase * 1000);
        CHECK(std::abs(info.bpm - c.bpm) < 0.05);
        CHECK(std::abs(phase) < 0.02);
    }
}

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("loggx_mvp_" + std::to_string(stamp));
    fs::create_directories(dir);
    testVideo(dir);
    testInstruments();
    testMixer(dir);
    testExport(dir);
    testTransitions(dir);
    testAudioExport(dir);
    testBeatsAndLive(dir);
    testVocalsAndKey(dir);
    testEffects();
    testDj();
    testDjCueAndJog();
    testDspStems();
    testMastering();
    testMulticam();
    testDjTransitions();
    testDemucs();
    testPluginHost();
    testTempoDetection();
    testAi(dir);
    testCutout();
    std::error_code ec;
    fs::remove_all(dir, ec);
    return finish("test_mvp");
}
