#include "loggx/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef LOGGX_WITH_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace loggx {

namespace {

std::int64_t toSamples(TimeUs t, int rate) { return t * rate / kSecond; }

bool needsStretch(const Clip& c) {
    return std::abs(c.speed - 1.0) > 1e-6 || std::abs(c.pitch) > 1e-6;
}

}  // namespace

// Eine Audioquelle pro Clip: Decoder plus optionaler Stretcher. Liefert
// fortlaufend Samples ab einer clip-relativen Position.
struct AudioMixer::Source {
    AudioReader reader;
    std::string path;
    bool failed = false;
    int rate = 48000;

    std::int64_t nextRel = -1;   // nächste erwartete clip-relative Position
    TimeUs in = -1;
    double speed = 1.0;
    double pitch = 0.0;

#ifdef LOGGX_WITH_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    std::size_t toDrop = 0;
    std::vector<float> inBuf, left, right;
#endif

    void reposition(const Clip& clip, std::int64_t rel) {
        const TimeUs sourceUs =
            clip.in + static_cast<TimeUs>(static_cast<double>(rel) * kSecond / rate * clip.speed);
        reader.seek(sourceUs);
        nextRel = rel;
        in = clip.in;
        speed = clip.speed;
        pitch = clip.pitch;
#ifdef LOGGX_WITH_RUBBERBAND
        if (!needsStretch(clip)) {
            stretcher.reset();
            return;
        }
        const double pitchScale = std::pow(2.0, clip.pitch / 12.0);
        if (!stretcher) {
            using RB = RubberBand::RubberBandStretcher;
            stretcher = std::make_unique<RB>(
                static_cast<std::size_t>(rate), 2,
                RB::OptionProcessRealTime | RB::OptionPitchHighConsistency |
                    RB::OptionChannelsTogether,
                1.0 / clip.speed, pitchScale);
        } else {
            stretcher->reset();
            stretcher->setTimeRatio(1.0 / clip.speed);
            stretcher->setPitchScale(pitchScale);
        }
        // Anlaufverzögerung des Stretchers ausgleichen, damit der Ton
        // samplegenau an der Clip-Position beginnt.
        toDrop = stretcher->getStartDelay();
        const std::size_t pad = stretcher->getPreferredStartPad();
        if (pad > 0) {
            left.assign(pad, 0.0f);
            right.assign(pad, 0.0f);
            const float* ptrs[2] = {left.data(), right.data()};
            stretcher->process(ptrs, pad, false);
        }
#endif
    }

    void fetch(const Clip& clip, std::int64_t rel, float* out, std::size_t frames) {
        if (rel != nextRel || clip.in != in || clip.speed != speed) reposition(clip, rel);
        nextRel = rel + static_cast<std::int64_t>(frames);

#ifdef LOGGX_WITH_RUBBERBAND
        if (needsStretch(clip) != static_cast<bool>(stretcher)) {
            reposition(clip, rel);
            nextRel = rel + static_cast<std::int64_t>(frames);
        }
        if (stretcher) {
            if (clip.pitch != pitch) {
                pitch = clip.pitch;
                stretcher->setPitchScale(std::pow(2.0, clip.pitch / 12.0));
            }
            int silentFeeds = 0;
            while (stretcher->available() < static_cast<int>(frames + toDrop)) {
                std::size_t need =
                    std::clamp<std::size_t>(stretcher->getSamplesRequired(), 256, 4096);
                inBuf.resize(need * 2);
                const std::size_t got = reader.read(inBuf.data(), need);
                if (got < need)
                    std::fill(inBuf.begin() + static_cast<std::ptrdiff_t>(got * 2), inBuf.end(),
                              0.0f);
                // Am Dateiende nur begrenzt Stille nachschieben, um den
                // Stretcher zu leeren.
                if (got == 0 && ++silentFeeds > 32) break;
                left.resize(need);
                right.resize(need);
                for (std::size_t i = 0; i < need; ++i) {
                    left[i] = inBuf[i * 2];
                    right[i] = inBuf[i * 2 + 1];
                }
                const float* ptrs[2] = {left.data(), right.data()};
                stretcher->process(ptrs, need, false);
            }
            left.resize(std::max(frames, toDrop));
            right.resize(std::max(frames, toDrop));
            float* outPtrs[2] = {left.data(), right.data()};
            while (toDrop > 0 && stretcher->available() > 0) {
                const std::size_t n = stretcher->retrieve(
                    outPtrs, std::min<std::size_t>(toDrop, static_cast<std::size_t>(
                                                               stretcher->available())));
                if (n == 0) break;
                toDrop -= n;
            }
            const int avail = std::max(0, stretcher->available());
            const std::size_t n =
                stretcher->retrieve(outPtrs, std::min<std::size_t>(frames, static_cast<std::size_t>(avail)));
            for (std::size_t i = 0; i < n; ++i) {
                out[i * 2] = left[i];
                out[i * 2 + 1] = right[i];
            }
            std::fill(out + n * 2, out + frames * 2, 0.0f);
            return;
        }
#endif
        const std::size_t got = reader.read(out, frames);
        std::fill(out + got * 2, out + frames * 2, 0.0f);
    }
};

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer() = default;

bool AudioMixer::hasStretcher() {
#ifdef LOGGX_WITH_RUBBERBAND
    return true;
#else
    return false;
#endif
}

void AudioMixer::setProject(std::shared_ptr<const Project> project) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_project = std::move(project);
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        const Clip* clip = m_project ? m_project->findClip(it->first) : nullptr;
        const MediaAsset* asset = clip ? m_project->findAsset(clip->assetId) : nullptr;
        if (!asset || asset->path != it->second->path)
            it = m_sources.erase(it);
        else
            ++it;
    }
}

void AudioMixer::render(std::int64_t position, float* interleaved, std::size_t frames) {
    std::fill(interleaved, interleaved + frames * 2, 0.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_project) return;
    const int rate = m_project->sampleRate;
    const std::int64_t blockEnd = position + static_cast<std::int64_t>(frames);

    for (const Track& track : m_project->tracks) {
        if (track.muted) continue;
        for (const Clip& clip : track.clips) {
            const std::int64_t clipStart = toSamples(clip.start, rate);
            const std::int64_t clipEnd = toSamples(clip.end(), rate);
            const std::int64_t a = std::max(position, clipStart);
            const std::int64_t b = std::min(blockEnd, clipEnd);
            if (a >= b) continue;
            const MediaAsset* asset = m_project->findAsset(clip.assetId);
            if (!asset || !asset->hasAudio) continue;
            const double gain = clip.gain * track.gain;
            if (gain <= 0.0) continue;

            auto& source = m_sources[clip.id];
            if (!source) {
                source = std::make_unique<Source>();
                source->path = asset->path;
                source->rate = rate;
                source->failed = !source->reader.open(asset->path, rate);
            }
            if (source->failed) continue;

            const std::size_t n = static_cast<std::size_t>(b - a);
            m_scratch.resize(n * 2);
            source->fetch(clip, a - clipStart, m_scratch.data(), n);
            float* dst = interleaved + (a - position) * 2;
            const float g = static_cast<float>(gain);
            for (std::size_t i = 0; i < n * 2; ++i) dst[i] += m_scratch[i] * g;
        }
    }
    // Übersteuerung hart begrenzen (ein Limiter folgt mit dem Mixer im MVP).
    for (std::size_t i = 0; i < frames * 2; ++i)
        interleaved[i] = std::clamp(interleaved[i], -1.0f, 1.0f);
}

}  // namespace loggx
