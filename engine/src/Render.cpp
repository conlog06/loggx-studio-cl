#include "loggx/engine/Engine.h"

#include <cmath>

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
