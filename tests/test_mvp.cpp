// Tests der MVP-Funktionen der Engine: Transformation, Farbe, LUT,
// Effekte, Blenden, Keyframes, Titel, Instrumente, Mixer und H.265.
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    CHECK(peak(buf) <= 0.98);
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

}  // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("loggx_mvp_" + std::to_string(stamp));
    fs::create_directories(dir);
    testVideo(dir);
    testInstruments();
    testMixer(dir);
    testExport(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return finish("test_mvp");
}
