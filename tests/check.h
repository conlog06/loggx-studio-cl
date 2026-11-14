// Minimales Test-Hilfsmittel ohne externe Abhängigkeit.
#pragma once

#include <cstdio>

inline int g_failures = 0;

// Ausgabe sofort schreiben, damit sie bei einem Absturz nicht verloren geht.
inline const bool g_unbuffered = (std::setvbuf(stdout, nullptr, _IONBF, 0), true);

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

inline int finish(const char* name) {
    if (g_failures == 0) {
        std::printf("%s: alle Prüfungen bestanden\n", name);
        return 0;
    }
    std::printf("%s: %d Prüfung(en) fehlgeschlagen\n", name, g_failures);
    return 1;
}
