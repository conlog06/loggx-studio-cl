// Rendert ein .loggx-Projekt ohne Oberfläche in eine Videodatei.
//   loggx-render projekt.loggx ausgabe.mp4 [--size 1920x1080] [--encoder libx264]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "loggx/engine/Engine.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "Aufruf: loggx-render <projekt.loggx> <ausgabe.mp4> "
                     "[--size BxH] [--encoder NAME]\n");
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
        } else {
            std::fprintf(stderr, "Unbekannte Option: %s\n", argv[i]);
            return 2;
        }
    }

    int lastPercent = -1;
    const loggx::RenderResult result = loggx::renderProject(project, settings, [&](double p) {
        const int percent = static_cast<int>(p * 100.0);
        if (percent != lastPercent) {
            lastPercent = percent;
            std::fprintf(stderr, "\r%3d %%", percent);
            std::fflush(stderr);
        }
    });
    std::fprintf(stderr, "\n");
    if (!result.ok) {
        std::fprintf(stderr, "Export fehlgeschlagen: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("%lld Bilder geschrieben (%s) -> %s\n", static_cast<long long>(result.frames),
                result.videoEncoder.c_str(), settings.path.c_str());
    return 0;
}
