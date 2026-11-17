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
}

// Eingebaute Stem-Engine: Schlagzeug und Bass landen im richtigen Stem
void testMastering() {
    const int rate = 48000;
    // 10 s leiser Mix: Sinus 220 Hz + Bass-Puls, um −30 dBFS
    std::vector<float> mix(static_cast<std::size_t>(rate) * 10 * 2);
    for (std::size_t i = 0; i < mix.size() / 2; ++i) {
        const double t = static_cast<double>(i) / rate;
        const float v = static_cast<float>(0.03 * std::sin(2 * M_PI * 220 * t) +
                                           (std::fmod(t, 0.5) < 0.1 ? 0.06 * std::sin(2 * M_PI * 60 * t) : 0.0));
        mix[i * 2] = mix[i * 2 + 1] = v;
    }
    // Referenz: 1 kHz Sinus mit 0 dBFS Spitze ≈ −3,0 LUFS
    std::vector<float> ref(static_cast<std::size_t>(rate) * 3 * 2);
    for (std::size_t i = 0; i < ref.size() / 2; ++i)
        ref[i * 2] = ref[i * 2 + 1] = static_cast<float>(std::sin(2 * M_PI * 1000 * i / rate));
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
    testTempoDetection();
    testAi(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return finish("test_mvp");
}
