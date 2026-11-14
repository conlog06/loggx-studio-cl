// Minimales Test-Hilfsmittel ohne externe Abhängigkeit.
#pragma once

#include <cstdio>

inline int g_failures = 0;

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
