#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace loggx {

RenderResult renderProject(const Project& project, ExportSettings settings,
                           const std::function<void(double)>& progress,
                           const std::atomic<bool>* cancel, TitleRasterizer titles) {
    RenderResult result;
    const TimeUs duration = project.duration();
    if (duration <= 0) {
        result.error = "Das Projekt enthält keine Clips";
        return result;
    }
    settings.fpsNum = project.fpsNum;
    settings.fpsDen = project.fpsDen;
    settings.sampleRate = project.sampleRate;

    Exporter exporter;
    if (!exporter.open(settings, &result.error)) return result;
    result.videoEncoder = exporter.videoEncoderName();

    const auto shared = std::make_shared<const Project>(project);
    Compositor compositor;
    compositor.setProject(shared);
    compositor.setTitleRasterizer(std::move(titles));
    AudioMixer mixer;
    mixer.setProject(shared);

    // Bild i liegt bei i * fpsDen / fpsNum Sekunden (exakt in Ganzzahlen).
    const std::int64_t totalFrames = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(
               std::ceil(static_cast<double>(duration) * project.fpsNum /
                         (static_cast<double>(kSecond) * project.fpsDen))));
    std::vector<std::uint8_t> image;
    std::vector<float> audio;
    std::int64_t audioPos = 0;

    for (std::int64_t i = 0; i < totalFrames; ++i) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            break;
        }
        const TimeUs t = i * kSecond * project.fpsDen / project.fpsNum;
        compositor.render(t, settings.width, settings.height, image);
        if (!exporter.writeVideoFrame(image.data())) {
            result.error = "Fehler beim Schreiben des Videos";
            break;
        }
        if (settings.audio) {
            // Audio bis zum Ende dieses Bilds nachziehen, damit beide
            // Spuren in der Datei verzahnt bleiben.
            const std::int64_t target =
                (i + 1) * project.sampleRate * project.fpsDen / project.fpsNum;
            const std::size_t n = static_cast<std::size_t>(target - audioPos);
            audio.resize(n * 2);
            mixer.render(audioPos, audio.data(), n);
            audioPos = target;
            if (!exporter.writeAudio(audio.data(), n)) {
                result.error = "Fehler beim Schreiben des Tons";
                break;
            }
        }
        result.frames = i + 1;
        if (progress && (i % 5 == 0 || i + 1 == totalFrames))
            progress(static_cast<double>(i + 1) / static_cast<double>(totalFrames));
    }

    std::string finishError;
    const bool finished = exporter.finish(&finishError);
    if (result.cancelled || !result.error.empty()) return result;
    if (!finished) {
        result.error = finishError.empty() ? "Datei konnte nicht abgeschlossen werden" : finishError;
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace loggx

// ------------------------------------------------------------------ Audio

namespace loggx {

RenderResult renderAudio(const Project& project, ExportSettings settings,
                         const std::function<void(double)>& progress,
                         const std::atomic<bool>* cancel, bool tail) {
    RenderResult result;
    const TimeUs duration = project.duration();
    if (duration <= 0) {
        result.error = "Das Projekt enthält keine Clips";
        return result;
    }
    settings.video = false;
    settings.audio = true;
    settings.sampleRate = project.sampleRate;

    Exporter exporter;
    if (!exporter.open(settings, &result.error)) return result;

    AudioMixer mixer;
    mixer.setProject(std::make_shared<const Project>(project));
    const int rate = project.sampleRate;
    const std::int64_t total = duration * rate / kSecond;
    const std::int64_t maxTail = tail && settings.audioTail ? 5 * static_cast<std::int64_t>(rate) : 0;
    constexpr std::size_t block = 4096;
    std::vector<float> buf(block * 2);
    std::vector<float> mastered;
    std::int64_t pos = 0;
    while (pos < total + maxTail) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            break;
        }
        const std::size_t n = static_cast<std::size_t>(
            pos < total ? std::min<std::int64_t>(block, total - pos) : static_cast<std::int64_t>(block));
        mixer.render(pos, buf.data(), n);
        // Im Ausklang: aufhören, sobald es still ist (unter -80 dBFS).
        if (pos >= total) {
            float peak = 0.0f;
            for (std::size_t i = 0; i < n * 2; ++i) peak = std::max(peak, std::abs(buf[i]));
            if (peak < 1e-4f) break;
        }
        if (settings.mastering > 0) {
            // Mastering braucht den ganzen Mix (Lautheit über das Stück)
            mastered.insert(mastered.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n * 2));
        } else if (!exporter.writeAudio(buf.data(), n)) {
            result.error = "Fehler beim Schreiben des Tons";
            break;
        }
        pos += static_cast<std::int64_t>(n);
        result.frames = pos;
        if (progress) progress(std::min(1.0, static_cast<double>(pos) / static_cast<double>(total)) * (settings.mastering > 0 ? 0.7 : 1.0));
    }
    if (settings.mastering > 0 && !result.cancelled && result.error.empty()) {
        result.mastering = masterAudio(mastered, rate, static_cast<MasteringPreset>(std::clamp(settings.mastering, 0, 3)));
        if (progress) progress(0.9);
        for (std::size_t i = 0; i < mastered.size() / 2; i += block) {
            const std::size_t n = std::min(block, mastered.size() / 2 - i);
            if (!exporter.writeAudio(mastered.data() + i * 2, n)) {
                result.error = "Fehler beim Schreiben des Tons";
                break;
            }
        }
    }
    std::string finishError;
    const bool finished = exporter.finish(&finishError);
    if (result.cancelled || !result.error.empty()) return result;
    if (!finished) {
        result.error = finishError.empty() ? "Datei konnte nicht abgeschlossen werden" : finishError;
        return result;
    }
    if (progress) progress(1.0);
    result.ok = true;
    return result;
}

RenderResult renderStems(const Project& project, const std::string& folder,
                         const std::string& extension, int audioBits,
                         std::vector<std::string>* files,
                         const std::function<void(double)>& progress,
                         const std::atomic<bool>* cancel) {
    // Spuren, die tatsächlich etwas zum Klingen beitragen.
    std::vector<std::size_t> sounding;
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        const Track& t = project.tracks[i];
        bool sound = false;
        for (const Clip& c : t.clips) {
            if (c.kind == ClipKind::Midi && !c.notes.empty()) sound = true;
            if (c.kind == ClipKind::Media)
                if (const MediaAsset* a = project.findAsset(c.assetId); a && a->hasAudio) sound = true;
        }
        if (sound && !t.muted) sounding.push_back(i);
    }
    RenderResult total;
    if (sounding.empty()) {
        total.error = "Keine Spur mit Ton vorhanden";
        return total;
    }
    std::error_code ec;
    const std::filesystem::path dir(std::u8string(folder.begin(), folder.end()));
    std::filesystem::create_directories(dir, ec);
    for (std::size_t k = 0; k < sounding.size(); ++k) {
        Project single = project;
        for (std::size_t i = 0; i < single.tracks.size(); ++i) {
            single.tracks[i].solo = false;
            single.tracks[i].muted = i != sounding[k];
        }
        std::string name = project.tracks[sounding[k]].name;
        for (char& ch : name)
            if (std::strchr("\\/:*?\"<>|", ch)) ch = '_';
        char prefix[8];
        std::snprintf(prefix, sizeof(prefix), "%02zu ", k + 1);
        const std::filesystem::path file = dir / std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(prefix), reinterpret_cast<const char8_t*>(prefix) + std::strlen(prefix))) ;
        std::u8string full = file.u8string();
        const std::string tail = name + "." + extension;
        full.append(tail.begin(), tail.end());
        ExportSettings s;
        s.path.assign(full.begin(), full.end());
        s.audioBits = audioBits;
        const RenderResult r = renderAudio(single, s, [&](double p) {
            if (progress) progress((static_cast<double>(k) + p) / static_cast<double>(sounding.size()));
        }, cancel);
        if (!r.ok) return r;
        if (files) files->push_back(s.path);
        total.frames += r.frames;
    }
    total.ok = true;
    return total;
}

}  // namespace loggx
