// Verteilt eine Schleife über alle CPU-Kerne (für Bildberechnungen).
#pragma once

#include <algorithm>
#include <thread>
#include <vector>

namespace loggx {

template <typename Fn>
void parallelRows(int begin, int end, Fn&& fn, int minRowsPerThread = 32) {
    const int rows = end - begin;
    if (rows <= 0) return;
    static const int cores = std::max(1u, std::thread::hardware_concurrency());
    const int threads = std::clamp(rows / std::max(1, minRowsPerThread), 1, cores);
    if (threads == 1) {
        fn(begin, end);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads - 1));
    const int chunk = (rows + threads - 1) / threads;
    for (int i = 1; i < threads; ++i) {
        const int a = begin + i * chunk;
        const int b = std::min(end, a + chunk);
        if (a < b) pool.emplace_back([&fn, a, b] { fn(a, b); });
    }
    fn(begin, std::min(end, begin + chunk));
    for (auto& t : pool) t.join();
}

}  // namespace loggx
