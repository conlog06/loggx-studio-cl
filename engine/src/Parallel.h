// Verteilt eine Schleife über alle CPU-Kerne (für Bildberechnungen).
#pragma once

#include <algorithm>
#include <functional>

namespace loggx {

namespace detail {
int parallelCores();
// Führt fn(0..tasks-1) auf einem festen Thread-Pool aus (der Aufrufer rechnet
// mit). Ist der Pool belegt (zweiter Aufrufer, verschachtelter Aufruf),
// läuft alles im aufrufenden Thread – ohne Verklemmung.
void runParallel(int tasks, const std::function<void(int)>& fn);
}  // namespace detail

template <typename Fn>
void parallelRows(int begin, int end, Fn&& fn, int minRowsPerThread = 32) {
    const int rows = end - begin;
    if (rows <= 0) return;
    const int threads = std::clamp(rows / std::max(1, minRowsPerThread), 1, detail::parallelCores());
    if (threads == 1) {
        fn(begin, end);
        return;
    }
    // Mehr Stücke als Threads: gleicht ungleich teure Zeilen aus.
    const int pieces = std::min(rows, threads * 3);
    const int chunk = (rows + pieces - 1) / pieces;
    detail::runParallel(pieces, [&](int i) {
        const int a = begin + i * chunk;
        const int b = std::min(end, a + chunk);
        if (a < b) fn(a, b);
    });
}

}  // namespace loggx
