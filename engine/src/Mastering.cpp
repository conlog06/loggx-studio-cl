// Mastering-Assistent: misst die Lautheit (LUFS nach ITU-R BS.1770, mit
// Gating), formt den Klang leicht (Bass straffen, etwas Glanz), bringt den
// Mix auf die Ziel-Lautheit und begrenzt die Spitzen (Limiter mit Vorschau).
#include <algorithm>
#include <cmath>

#include "AudioFx.h"
#include "loggx/engine/Engine.h"

namespace loggx {

double measureLufs(const std::vector<float>& stereo, int rate) {
    // K-Filter: Höhenanhebung (+4 dB ab ~1,7 kHz) und Hochpass (~38 Hz)
    Biquad shelf, hp;
    shelf.design(Biquad::Type::HighShelf, 1681.97, 4.0, 0.707, rate);
    hp.design(Biquad::Type::HighPass, 38.13, 0.0, 0.5, rate);
    const std::size_t frames = stereo.size() / 2;
    const std::size_t block = static_cast<std::size_t>(rate * 0.4), step = static_cast<std::size_t>(rate * 0.1);
    if (frames < block) return -70.0;
    std::vector<double> sq(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double e = 0;
        for (int c = 0; c < 2; ++c) {
            const double y = hp.process(shelf.process(stereo[i * 2 + c], c), c);
            e += y * y;
        }
        sq[i] = e;
    }
    // 400-ms-Blöcke mit 75 % Überlappung
    std::vector<double> blocks;
    double sum = 0;
    for (std::size_t i = 0; i < block; ++i) sum += sq[i];
    for (std::size_t start = 0; start + block <= frames; start += step) {
        if (start > 0) {
            for (std::size_t i = start - step; i < start; ++i) sum -= sq[i];
            for (std::size_t i = start + block - step; i < start + block; ++i) sum += sq[i];
        }
        blocks.push_back(sum / static_cast<double>(block));
    }
    auto lufs = [](double ms) { return -0.691 + 10.0 * std::log10(std::max(ms, 1e-12)); };
    double total = 0;
    int n = 0;
    for (double b : blocks)
        if (lufs(b) > -70.0) total += b, ++n;   // absolutes Gate
    if (n == 0) return -70.0;
    const double relGate = lufs(total / n) - 10.0;   // relatives Gate
    total = 0;
    n = 0;
    for (double b : blocks)
        if (lufs(b) > -70.0 && lufs(b) > relGate) total += b, ++n;
    return n == 0 ? -70.0 : lufs(total / n);
}

MasteringResult masterAudio(std::vector<float>& stereo, int rate, MasteringPreset preset) {
    MasteringResult r;
    r.lufsBefore = measureLufs(stereo, rate);
    if (preset == MasteringPreset::Off || r.lufsBefore <= -69.0) {
        r.lufsAfter = r.lufsBefore;
        return r;
    }
    const double target = preset == MasteringPreset::Club ? -9.0 : preset == MasteringPreset::Podcast ? -16.0 : -14.0;
    const double ceiling = preset == MasteringPreset::Podcast ? std::pow(10.0, -1.5 / 20.0) : std::pow(10.0, -1.0 / 20.0);
    const std::size_t frames = stereo.size() / 2;
    // 1) Klangformung: Rumpeln weg, Bass straffen, etwas Luft
    Biquad rumble, low, air;
    rumble.design(Biquad::Type::HighPass, 28.0, 0.0, 0.707, rate);
    low.design(Biquad::Type::LowShelf, 90.0, preset == MasteringPreset::Podcast ? -2.0 : 0.8, 0.707, rate);
    air.design(Biquad::Type::HighShelf, 10000.0, preset == MasteringPreset::Podcast ? 0.5 : 1.5, 0.707, rate);
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
            stereo[i * 2 + c] = air.process(low.process(rumble.process(stereo[i * 2 + c], c), c), c);
    // 2) Auf Ziel-Lautheit bringen; der Limiter nimmt danach etwas weg → zweimal anpassen
    double gainDb = target - measureLufs(stereo, rate);
    std::vector<float> work;
    for (int pass = 0; pass < 2; ++pass) {
        work = stereo;
        const float g = static_cast<float>(std::pow(10.0, gainDb / 20.0));
        for (float& x : work) x *= g;
        // 3) Limiter: 5 ms Vorschau, weiches Lösen (80 ms), Decke −1 dBFS
        const std::size_t look = static_cast<std::size_t>(rate * 0.005);
        const double release = std::exp(-1.0 / (rate * 0.08));
        std::vector<float> peak(frames);
        for (std::size_t i = 0; i < frames; ++i) peak[i] = std::max(std::abs(work[i * 2]), std::abs(work[i * 2 + 1]));
        // gleitendes Maximum über die Vorschau
        std::vector<float> ahead(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            float m = 0;
            for (std::size_t j = i; j < std::min(frames, i + look); ++j) m = std::max(m, peak[j]);
            ahead[i] = m;
        }
        double env = 1.0;
        for (std::size_t i = 0; i < frames; ++i) {
            const double need = ahead[i] > ceiling ? ceiling / ahead[i] : 1.0;
            env = need < env ? need : 1.0 - (1.0 - env) * release;
            env = std::min(env, need);
            r.maxReductionDb = std::max(r.maxReductionDb, -20.0 * std::log10(env));
            work[i * 2] = static_cast<float>(std::clamp(work[i * 2] * env, -ceiling, ceiling));
            work[i * 2 + 1] = static_cast<float>(std::clamp(work[i * 2 + 1] * env, -ceiling, ceiling));
        }
        const double got = measureLufs(work, rate);
        if (pass == 0) gainDb += std::clamp(target - got, 0.0, 3.0);   // Limiter-Verlust ausgleichen (max. 3 dB)
        r.lufsAfter = got;
    }
    r.gainDb = gainDb;
    stereo.swap(work);
    return r;
}

}  // namespace loggx
