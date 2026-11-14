// Rendert ein .loggx-Projekt ohne Oberfläche in eine Videodatei.
//   loggx-render projekt.loggx ausgabe.mp4 [--size 1920x1080] [--codec h265] [--quality 2]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "loggx/engine/Engine.h"

#ifdef LOGGX_WITH_QT_TITLES
#include <QGuiApplication>

#include "TitleRenderer.h"
#endif

int main(int argc, char** argv) {
#ifdef LOGGX_WITH_QT_TITLES
    // Schriften brauchen eine QGuiApplication; ohne Bildschirm reicht "offscreen".
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication qtApp(argc, argv);
#endif
    if (argc < 3) {
        std::fprintf(stderr,
                     "Aufruf: loggx-render <projekt.loggx> <ausgabe.mp4> "
                     "[--size BxH] [--codec h264|h265] [--quality 0-3] [--encoder NAME]\n");
        return 2;
    }
    loggx::Project project;
    std::string error;
    if (!loggx::Project::loadFile(argv[1], project, &error)) {
        std::fprintf(stderr, "Projekt kann nicht geladen werden: %s\n", error.c_str());
        return 1;
    }

    loggx::ExportSettings settings;
    settings.path = argv[2];
    settings.width = project.width;
    settings.height = project.height;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--size") == 0) {
            if (std::sscanf(argv[i + 1], "%dx%d", &settings.width, &settings.height) != 2) {
                std::fprintf(stderr, "Ungültige Größe: %s\n", argv[i + 1]);
                return 2;
            }
        } else if (std::strcmp(argv[i], "--encoder") == 0) {
            settings.videoEncoder = argv[i + 1];
        } else if (std::strcmp(argv[i], "--codec") == 0) {
            settings.codec = std::strcmp(argv[i + 1], "h265") == 0 ? loggx::ExportSettings::Codec::H265
                                                                  : loggx::ExportSettings::Codec::H264;
        } else if (std::strcmp(argv[i], "--quality") == 0) {
            settings.quality = std::atoi(argv[i + 1]);
        } else {
            std::fprintf(stderr, "Unbekannte Option: %s\n", argv[i]);
            return 2;
        }
    }

    loggx::TitleRasterizer titles;
#ifdef LOGGX_WITH_QT_TITLES
    titles = qtTitleRasterizer();
#else
    for (const auto& t : project.tracks)
        for (const auto& c : t.clips)
            if (c.kind == loggx::ClipKind::Title && !titles) {
                std::fprintf(stderr, "Hinweis: ohne Qt gebaut, Titel werden nicht gezeichnet\n");
                titles = [](const loggx::Title&, int, int, std::vector<std::uint8_t>&) {};
            }
#endif
    int lastPercent = -1;
    const loggx::RenderResult result = loggx::renderProject(project, settings, [&](double p) {
        const int percent = static_cast<int>(p * 100.0);
        if (percent != lastPercent) {
            lastPercent = percent;
            std::fprintf(stderr, "\r%3d %%", percent);
            std::fflush(stderr);
        }
    }, nullptr, titles);
    std::fprintf(stderr, "\n");
    if (!result.ok) {
        std::fprintf(stderr, "Export fehlgeschlagen: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("%lld Bilder geschrieben (%s) -> %s\n", static_cast<long long>(result.frames),
                result.videoEncoder.c_str(), settings.path.c_str());
    return 0;
}
