// End-to-End-Test der Medien- und Render-Engine. Erzeugt sein Testmaterial
// selbst (kein ffmpeg-Programm nötig): vier Sekunden Video, jede Sekunde
// eine andere Farbe, dazu ein 440-Hz-Ton.
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}
#include "loggx/engine/Engine.h"

using namespace loggx;
namespace fs = std::filesystem;

namespace {

constexpr int kW = 320, kH = 180, kFps = 25, kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

struct Rgb {
    int r, g, b;
};
const Rgb kColors[4] = {{220, 30, 30}, {30, 200, 30}, {30, 30, 220}, {235, 235, 235}};

bool makeSource(const std::string& path) {
    ExportSettings s;
    s.path = path;
    s.width = kW;
    s.height = kH;
    s.fpsNum = kFps;
    s.sampleRate = kRate;
    Exporter e;
    std::string error;
    if (!e.open(s, &error)) {
        std::fprintf(stderr, "Exporter: %s\n", error.c_str());
        return false;
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(kW) * kH * 4);
    std::vector<float> audio(static_cast<std::size_t>(kRate / kFps) * 2);
    std::int64_t sample = 0;
    for (int i = 0; i < 4 * kFps; ++i) {
        const Rgb c = kColors[i / kFps];
        for (std::size_t p = 0; p < image.size(); p += 4) {
            image[p] = static_cast<std::uint8_t>(c.r);
            image[p + 1] = static_cast<std::uint8_t>(c.g);
            image[p + 2] = static_cast<std::uint8_t>(c.b);
            image[p + 3] = 255;
        }
        if (!e.writeVideoFrame(image.data())) return false;
        for (std::size_t n = 0; n < audio.size() / 2; ++n, ++sample) {
            const float v = 0.5f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * sample / kRate));
            audio[n * 2] = audio[n * 2 + 1] = v;
        }
        if (!e.writeAudio(audio.data(), audio.size() / 2)) return false;
    }
    return e.finish(&error);
}

// ProRes 422 HQ / 4444 in MOV (Schnitt-Master): öffnen, schreiben, wieder lesen
void testProRes(const fs::path& dir) {
    for (auto codec : {ExportSettings::Codec::ProRes422HQ, ExportSettings::Codec::ProRes4444}) {
        ExportSettings s;
        s.path = (dir / (codec == ExportSettings::Codec::ProRes4444 ? "p4444.mov" : "p422.mov")).string();
        s.width = kW;
        s.height = kH;
        s.fpsNum = kFps;
        s.sampleRate = kRate;
        s.codec = codec;
        Exporter e;
        std::string error;
        CHECK(e.open(s, &error));
        if (!error.empty()) std::fprintf(stderr, "ProRes: %s\n", error.c_str());
        CHECK(e.videoEncoderName() == "prores_ks");
        std::vector<std::uint8_t> image(static_cast<std::size_t>(kW) * kH * 4, 128);
        std::vector<float> audio(static_cast<std::size_t>(kRate / kFps) * 2, 0.1f);
        for (int i = 0; i < kFps; ++i) {
            CHECK(e.writeVideoFrame(image.data()));
            CHECK(e.writeAudio(audio.data(), audio.size() / 2));
        }
        CHECK(e.finish(&error));
        MediaAsset a;
        CHECK(probeMedia(s.path, a));
        CHECK(a.hasVideo && a.hasAudio && a.width == kW && a.height == kH);
    }
}

// HDR (HLG): H.265 Main 10, BT.2020, Transfer ARIB STD-B67 im Container
void testHdr(const fs::path& dir) {
    ExportSettings s;
    s.path = (dir / "hdr.mp4").string();
    s.width = kW;
    s.height = kH;
    s.fpsNum = kFps;
    s.sampleRate = kRate;
    s.codec = ExportSettings::Codec::H265;
    s.hdr = true;
    Exporter e;
    std::string error;
    if (!e.open(s, &error)) {
        std::printf("HDR übersprungen: %s\n", error.c_str());
        return;
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(kW) * kH * 4, 255);   // SDR-Weiß
    std::vector<float> audio(static_cast<std::size_t>(kRate / kFps) * 2, 0.0f);
    for (int i = 0; i < 10; ++i) {
        CHECK(e.writeVideoFrame(image.data()));
        CHECK(e.writeAudio(audio.data(), audio.size() / 2));
    }
    CHECK(e.finish(&error));
    AVFormatContext* fmt = nullptr;
    CHECK(avformat_open_input(&fmt, s.path.c_str(), nullptr, nullptr) == 0);
    avformat_find_stream_info(fmt, nullptr);
    bool found = false;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters* p = fmt->streams[i]->codecpar;
        if (p->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        found = true;
        CHECK(p->color_trc == AVCOL_TRC_ARIB_STD_B67);
        CHECK(p->color_primaries == AVCOL_PRI_BT2020);
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(p->format));
        CHECK(d && d->comp[0].depth == 10);
        std::printf("HDR: %s, %s, %s\n", e.videoEncoderName().c_str(), d ? d->name : "?", av_color_transfer_name(p->color_trc));
    }
    CHECK(found);
    avformat_close_input(&fmt);
    // H.264 + HDR wird abgelehnt
    ExportSettings h264 = s;
    h264.codec = ExportSettings::Codec::H264;
    h264.path = (dir / "hdr264.mp4").string();
    Exporter e2;
    CHECK(!e2.open(h264, &error));
}

// Masken-Tracking: ein strukturiertes Quadrat wandert nach rechts unten
void testMaskTracking(const fs::path& dir) {
    const int w = 320, h = 180, fps = 25, frames = 40;
    ExportSettings s;
    s.path = (dir / "bewegung.mp4").string();
    s.width = w;
    s.height = h;
    s.fpsNum = fps;
    s.sampleRate = kRate;
    s.audio = false;
    s.quality = 3;
    Exporter e;
    std::string error;
    CHECK(e.open(s, &error));
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4);
    auto posAt = [&](int f) { return std::pair<double, double>{60.0 + f * 3.0, 50.0 + f * 1.5}; };
    for (int f = 0; f < frames; ++f) {
        const auto [ox, oy] = posAt(f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* p = &img[(static_cast<std::size_t>(y) * w + x) * 4];
                const double lx = x - ox, ly = y - oy;
                std::uint8_t v = 40;
                if (std::abs(lx) < 18 && std::abs(ly) < 18) v = ((static_cast<int>(lx + 18) / 6 + static_cast<int>(ly + 18) / 6) % 2) ? 230 : 90;
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        CHECK(e.writeVideoFrame(img.data()));
    }
    CHECK(e.finish(&error));
    MediaAsset asset;
    CHECK(probeMedia(s.path, asset, &error));
    Project project = Project::createDefault();
    project.width = w;
    project.height = h;
    project.fpsNum = fps;
    const std::string assetId = project.addAsset(asset);
    const std::string clipId = project.addClip(project.tracks[1].id, assetId, 0);
    Clip* clip = project.findClip(clipId);
    CHECK(clip != nullptr);
    clip->mask.shape = MaskShape::Ellipse;
    clip->mask.cx = posAt(0).first / w;
    clip->mask.cy = posAt(0).second / h;
    clip->mask.w = 40.0 / w;
    clip->mask.h = 40.0 / h;
    const MaskTrackResult r = trackMask(project, clipId, 0);
    if (!r.error.empty()) std::fprintf(stderr, "Tracking: %s\n", r.error.c_str());
    CHECK(r.error.empty() && r.frames >= frames - 3);
    CHECK(!r.x.empty() && r.x.size() == r.y.size());
    double maxErr = 0;
    for (std::size_t i = 0; i < r.x.size(); ++i) {
        const int f = static_cast<int>(std::lround(r.x[i].t / 1e6 * fps));
        const auto [ex, ey] = posAt(f);
        maxErr = std::max(maxErr, std::hypot(r.x[i].value * w - ex, r.y[i].value * h - ey));
    }
    std::printf("Masken-Tracking: %d Bilder, max. Abweichung %.1f px\n", r.frames, maxErr);
    CHECK(maxErr < 5.0);
}

// AV1 (z. B. YouTube-Downloads) muss ein Bild liefern – über dav1d
void testAv1() {
    const std::string path = std::string(LOGGX_TEST_DATA) + "/av1-rot.mp4";
    MediaAsset a;
    CHECK(probeMedia(path, a));
    CHECK(a.hasVideo && a.videoCodec == "av1");
    VideoReader r;
    std::string error;
    CHECK(r.open(path, &error));
    std::vector<std::uint8_t> img;
    CHECK(r.frameAt(500000, 32, 18, img));
    CHECK(!img.empty() && img[0] > 200 && img[1] < 60 && img[2] < 60);   // rot
    std::printf("AV1: Bild dekodiert (%s)\n", error.c_str());
}

// Auto-Reframe 16:9 → 9:16: ein Objekt läuft von links nach rechts
void testAutoReframe(const fs::path& dir) {
    const int w = 320, h = 180, fps = 25, frames = 100;
    ExportSettings s;
    s.path = (dir / "reframe.mp4").string();
    s.width = w;
    s.height = h;
    s.fpsNum = fps;
    s.sampleRate = kRate;
    s.audio = false;
    s.quality = 3;
    Exporter e;
    std::string error;
    CHECK(e.open(s, &error));
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4);
    for (int f = 0; f < frames; ++f) {
        const double ox = 40.0 + f * 2.4, oy = 90.0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* p = &img[(static_cast<std::size_t>(y) * w + x) * 4];
                const double lx = x - ox, ly = y - oy;
                std::uint8_t v = 50;
                if (std::abs(lx) < 16 && std::abs(ly) < 24) v = ((static_cast<int>(lx + 16) / 4 + static_cast<int>(ly + 24) / 4) % 2) ? 240 : 20;
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        CHECK(e.writeVideoFrame(img.data()));
    }
    CHECK(e.finish(&error));
    MediaAsset asset;
    CHECK(probeMedia(s.path, asset, &error));
    Project project = Project::createDefault();
    const std::string assetId = project.addAsset(asset);
    const std::string clipId = project.addClip(project.tracks[1].id, assetId, 0);
    const ReframeResult r = autoReframe(project, clipId, 1080, 1920);
    CHECK(r.error.empty());
    CHECK(std::abs(r.scale - 16.0 / 9.0 * 16.0 / 9.0) < 0.05);   // 16:9 füllt 9:16
    CHECK(r.y.empty() && r.x.size() >= 3);
    const double maxX = (r.scale * 1.0 - 1.0) / 2.0;   // Grundbreite = Zielbreite
    std::printf("Auto-Reframe: Zoom %.2f, x %.3f → %.3f (Grenze %.3f), %zu Keyframes\n", r.scale, r.x.front().value, r.x.back().value, maxX, r.x.size());
    CHECK(r.x.front().value > 0.5 * maxX);    // Objekt links → Bild nach rechts schieben
    CHECK(r.x.back().value < -0.5 * maxX);    // Objekt rechts → nach links
    for (const Keyframe& k : r.x) CHECK(std::abs(k.value) <= maxX + 1e-9);
}

// Welcher der vier Testfarben ein Pixel am nächsten kommt.
int nearestColor(const std::vector<std::uint8_t>& rgba, int w, int x, int y) {
    const std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
    int best = -1;
    int bestDist = 1 << 30;   // int reicht: höchstens 3 * 255²
    for (int i = 0; i < 4; ++i) {
        const int dr = p[0] - kColors[i].r, dg = p[1] - kColors[i].g, db = p[2] - kColors[i].b;
        const int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    if (bestDist >= 3 * 60 * 60) {
        std::fprintf(stderr, "Pixel (%d,%d) = %d,%d,%d passt zu keiner Testfarbe\n", x, y, p[0],
                     p[1], p[2]);
        return -1;
    }
    return best;
}

bool isBlack(const std::vector<std::uint8_t>& rgba, int w, int x, int y) {
    const std::uint8_t* p = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
    return p[0] < 30 && p[1] < 30 && p[2] < 30;
}

double rms(const std::vector<float>& a) {
    double sum = 0;
    for (float v : a) sum += static_cast<double>(v) * v;
    return a.empty() ? 0 : std::sqrt(sum / static_cast<double>(a.size()));
}

// Frequenzschätzung über Nulldurchgänge des linken Kanals.
double frequency(const std::vector<float>& a) {
    int crossings = 0;
    for (std::size_t i = 2; i < a.size(); i += 2)
        if ((a[i - 2] < 0) != (a[i] < 0)) ++crossings;
    return crossings / 2.0 / (static_cast<double>(a.size() / 2) / kRate);
}

void run(const fs::path& dir) {
    const std::string source = (dir / "quelle.mp4").string();
    CHECK(makeSource(source));

    // --- Analyse
    MediaAsset asset;
    std::string error;
    CHECK(probeMedia(source, asset, &error));
    CHECK(asset.hasVideo && asset.hasAudio && !asset.isStill);
    CHECK(asset.width == kW && asset.height == kH);
    CHECK(std::abs(asset.duration - 4 * kSecond) < 150000);
    CHECK(std::abs(asset.fps - kFps) < 0.01);
    MediaAsset missing;
    CHECK(!probeMedia((dir / "fehlt.mp4").string(), missing, &error));

    // --- Video-Decoder: vorwärts, Sprung, rückwärts
    {
        VideoReader reader;
        CHECK(reader.open(source));
        std::vector<std::uint8_t> img;
        CHECK(reader.frameAt(500000, kW, kH, img) && nearestColor(img, kW, 10, 10) == 0);
        CHECK(reader.frameAt(2500000, kW, kH, img) && nearestColor(img, kW, 10, 10) == 2);
        CHECK(reader.frameAt(1500000, kW, kH, img) && nearestColor(img, kW, 10, 10) == 1);
        // Bildgenau an der Farbgrenze bei 1,0 s (Bild 25).
        CHECK(reader.frameAt(980000, kW, kH, img) && nearestColor(img, kW, 10, 10) == 0);
        CHECK(reader.frameAt(1000000, kW, kH, img) && nearestColor(img, kW, 10, 10) == 1);
        // Skalierung auf eine andere Größe.
        CHECK(reader.frameAt(3500000, 160, 90, img) && img.size() == 160u * 90 * 4 &&
              nearestColor(img, 160, 80, 45) == 3);
        // Hinter dem Ende bleibt das letzte Bild stehen.
        CHECK(reader.frameAt(60 * kSecond, kW, kH, img) && nearestColor(img, kW, 10, 10) == 3);
    }

    // --- Audio-Decoder
    {
        AudioReader reader;
        CHECK(reader.open(source, kRate));
        std::vector<float> buf(kRate);   // 0,5 s Stereo
        reader.seek(1 * kSecond);
        CHECK(reader.read(buf.data(), buf.size() / 2) == buf.size() / 2);
        CHECK(std::abs(rms(buf) - 0.3536) < 0.05);
        CHECK(std::abs(frequency(buf) - 440.0) < 10.0);
        // Bis zum Ende lesen: insgesamt rund 4 s, dann 0.
        reader.seek(0);
        std::size_t total = 0, got = 0;
        while ((got = reader.read(buf.data(), buf.size() / 2)) > 0) total += got;
        CHECK(std::abs(static_cast<double>(total) / kRate - 4.0) < 0.1);
    }

    // --- Projekt: Clip teilen, ersten Teil löschen -> Blau, dann Weiß
    Project project = Project::createDefault();
    project.width = kW;
    project.height = kH;
    project.fpsNum = kFps;
    project.sampleRate = kRate;
    const std::string assetId = project.addAsset(asset);
    const std::string track = project.tracks[1].id;   // Video 1
    const std::string first = project.addClip(track, assetId, 0);
    const std::string second = project.splitClip(first, 2 * kSecond);
    CHECK(!second.empty());
    CHECK(project.removeClip(first, true));
    // Der Container ist durch die AAC-Vorlaufzeit minimal länger als 4 s.
    CHECK(std::abs(project.duration() - 2 * kSecond) < 150000);
    CHECK(project.trimClipEnd(second, 2 * kSecond));
    CHECK(project.duration() == 2 * kSecond);

    {
        Compositor compositor;
        compositor.setProject(std::make_shared<const Project>(project));
        std::vector<std::uint8_t> img;
        compositor.render(500000, kW, kH, img);
        CHECK(nearestColor(img, kW, kW / 2, kH / 2) == 2);
        compositor.render(1500000, kW, kH, img);
        CHECK(nearestColor(img, kW, kW / 2, kH / 2) == 3);
        compositor.render(5 * kSecond, kW, kH, img);   // hinter dem Ende: schwarz
        CHECK(isBlack(img, kW, kW / 2, kH / 2));

        // Maske: Mitte sichtbar, Ecke schwarz; invertiert umgekehrt.
        Project masked = project;
        Clip* clip = masked.findClip(second);
        clip->mask.shape = MaskShape::Ellipse;
        clip->mask.w = 0.5;
        clip->mask.h = 0.5;
        compositor.setProject(std::make_shared<const Project>(masked));
        compositor.render(500000, kW, kH, img);
        CHECK(nearestColor(img, kW, kW / 2, kH / 2) == 2);
        CHECK(isBlack(img, kW, 5, 5));
        clip->mask.invert = true;
        compositor.setProject(std::make_shared<const Project>(masked));
        compositor.render(500000, kW, kH, img);
        CHECK(isBlack(img, kW, kW / 2, kH / 2));
        CHECK(nearestColor(img, kW, 5, 5) == 2);

        // Halbe Deckkraft über Schwarz: halbe Helligkeit.
        Project faded = project;
        faded.findClip(second)->opacity = 0.5;
        compositor.setProject(std::make_shared<const Project>(faded));
        compositor.render(1500000, kW, kH, img);
        const std::uint8_t* px = img.data() + (static_cast<std::size_t>(kH / 2) * kW + kW / 2) * 4;
        CHECK(std::abs(px[0] - 117) < 20 && std::abs(px[1] - 117) < 20);

        // Andere Zielgröße mit anderem Seitenverhältnis: Balken oben/unten.
        compositor.setProject(std::make_shared<const Project>(project));
        compositor.render(500000, 200, 200, img);
        CHECK(isBlack(img, 200, 100, 5));
        CHECK(nearestColor(img, 200, 100, 100) == 2);
    }

    // --- Audio-Mix
    {
        AudioMixer mixer;
        mixer.setProject(std::make_shared<const Project>(project));
        std::vector<float> buf(kRate * 2);   // 1 s
        mixer.render(kRate / 2, buf.data(), kRate);
        CHECK(std::abs(rms(buf) - 0.3536) < 0.05);
        CHECK(std::abs(frequency(buf) - 440.0) < 10.0);
        // Hinter dem Projektende ist Stille.
        mixer.render(10 * kRate, buf.data(), kRate);
        CHECK(rms(buf) < 1e-6);

        Project quiet = project;
        quiet.findClip(second)->gain = 0.5;
        mixer.setProject(std::make_shared<const Project>(quiet));
        mixer.render(kRate / 2, buf.data(), kRate);
        CHECK(std::abs(rms(buf) - 0.1768) < 0.03);

        Project muted = project;
        muted.tracks[1].muted = true;
        mixer.setProject(std::make_shared<const Project>(muted));
        mixer.render(kRate / 2, buf.data(), kRate);
        CHECK(rms(buf) < 1e-6);

        if (AudioMixer::hasStretcher()) {
            // Eine Oktave höher: doppelte Frequenz bei gleicher Länge.
            Project pitched = project;
            pitched.findClip(second)->pitch = 12;
            mixer.setProject(std::make_shared<const Project>(pitched));
            mixer.render(0, buf.data(), kRate);
            mixer.render(kRate, buf.data(), kRate / 2);
            buf.resize(kRate);
            CHECK(std::abs(frequency(buf) - 880.0) < 30.0);
            CHECK(rms(buf) > 0.2);
            buf.resize(kRate * 2);

            // Doppeltes Tempo: halbe Länge, gleiche Tonhöhe.
            Project fast = project;
            CHECK(fast.setClipSpeed(second, 2.0));
            CHECK(fast.duration() == 1 * kSecond);
            mixer.setProject(std::make_shared<const Project>(fast));
            mixer.render(0, buf.data(), kRate / 2);
            mixer.render(kRate / 2, buf.data(), kRate / 2);
            buf.resize(kRate);
            CHECK(std::abs(frequency(buf) - 440.0) < 20.0);
            CHECK(rms(buf) > 0.2);
        } else {
            std::printf("Hinweis: ohne Rubber Band gebaut, Pitch/Tempo nicht geprüft\n");
        }
    }

    // --- Speichern, laden, Export und Kontrolle der fertigen Datei
    const std::string projectFile = (dir / "test.loggx").string();
    CHECK(project.saveFile(projectFile, &error));
    Project loaded;
    CHECK(Project::loadFile(projectFile, loaded, &error));
    CHECK(loaded.duration() == project.duration());

    const std::string output = (dir / "export.mp4").string();
    ExportSettings settings;
    settings.path = output;
    settings.width = kW;
    settings.height = kH;
    double lastProgress = 0;
    const RenderResult result =
        renderProject(loaded, settings, [&](double p) { lastProgress = p; });
    CHECK(result.ok);
    CHECK(result.frames == 2 * kFps);
    CHECK(lastProgress == 1.0);
    std::printf("Video-Encoder: %s\n", result.videoEncoder.c_str());

    MediaAsset exported;
    CHECK(probeMedia(output, exported, &error));
    CHECK(exported.hasVideo && exported.hasAudio);
    CHECK(exported.width == kW && exported.height == kH);
    CHECK(std::abs(exported.duration - 2 * kSecond) < 150000);
    {
        VideoReader reader;
        CHECK(reader.open(output));
        std::vector<std::uint8_t> img;
        CHECK(reader.frameAt(500000, kW, kH, img) && nearestColor(img, kW, 160, 90) == 2);
        CHECK(reader.frameAt(1500000, kW, kH, img) && nearestColor(img, kW, 160, 90) == 3);
        AudioReader audio;
        CHECK(audio.open(output, kRate));
        std::vector<float> buf(kRate);
        audio.seek(500000);
        CHECK(audio.read(buf.data(), buf.size() / 2) == buf.size() / 2);
        CHECK(std::abs(frequency(buf) - 440.0) < 10.0);
    }

    // Abbrechen liefert keinen Erfolg.
    std::atomic<bool> cancel{true};
    settings.path = (dir / "abbruch.mp4").string();
    const RenderResult cancelled = renderProject(loaded, settings, {}, &cancel);
    CHECK(!cancelled.ok && cancelled.cancelled);

    // Leeres Projekt wird abgelehnt.
    CHECK(!renderProject(Project::createDefault(), settings).ok);
}

}  // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("loggx_test_" + std::to_string(stamp));
    fs::create_directories(dir);
    run(dir);
    testProRes(dir);
    testHdr(dir);
    testMaskTracking(dir);
    testAutoReframe(dir);
    testAv1();
    std::error_code ec;
    fs::remove_all(dir, ec);
    return finish("test_render");
}
