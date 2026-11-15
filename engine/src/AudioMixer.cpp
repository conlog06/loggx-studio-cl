#include "loggx/engine/Engine.h"

#include "AudioFx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#ifdef LOGGX_WITH_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace loggx {

namespace {

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

struct AudioMixer::TrackState {
    TrackFxProcessor fx;
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
        const MediaAsset* asset =
            clip && clip->kind == ClipKind::Media ? m_project->findAsset(clip->assetId) : nullptr;
        if (!asset || asset->path != it->second->path)
            it = m_sources.erase(it);
        else
            ++it;
    }
    for (auto it = m_trackStates.begin(); it != m_trackStates.end();) {
        if (!m_project || !m_project->findTrack(it->first))
            it = m_trackStates.erase(it);
        else
            ++it;
    }
}

MeterLevels AudioMixer::takeLevels() {
    std::lock_guard<std::mutex> lock(m_mutex);
    MeterLevels out = m_levels;
    m_levels = MeterLevels{};
    return out;
}

namespace {

double dbToLinear(double db) { return db <= -59.9 ? 0.0 : std::pow(10.0, db / 20.0); }

// Lautstärke eines Clips (Keyframes, Blenden) für jedes Sample eines Blocks.
void applyClipGain(const Clip& clip, std::int64_t firstSample, int rate, float* stereo,
                   std::size_t frames) {
    const TimeUs t0 = firstSample * kSecond / rate;
    const TimeUs t1 = (firstSample + static_cast<std::int64_t>(frames)) * kSecond / rate;
    const double g0 = dbToLinear(clip.valueAt("gainDb", t0));
    const double g1 = dbToLinear(clip.valueAt("gainDb", t1));
    const bool fades = clip.fadeIn > 0 || clip.fadeOut > 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double f = frames > 1 ? static_cast<double>(i) / static_cast<double>(frames) : 0.0;
        double g = g0 + (g1 - g0) * f;
        if (fades) {
            // Blenden gelten nur innerhalb des Clips (Übergänge spielen darüber hinaus).
            const TimeUs t = (firstSample + static_cast<std::int64_t>(i)) * kSecond / rate;
            g *= clip.fadeAt(std::clamp(t, clip.start, clip.end() - 1));
        }
        stereo[i * 2] *= static_cast<float>(g);
        stereo[i * 2 + 1] *= static_cast<float>(g);
    }
}

bool crossfadeLike(TransitionType t) {
    return t == TransitionType::Crossfade || t == TransitionType::Wipe || t == TransitionType::Slide;
}

// Lautstärkeverlauf durch Übergänge an Anfang und Ende eines Clips.
struct TransitionRamps {
    TimeUs from = 0, to = 0;   // hörbarer Bereich (über die Clipgrenzen hinaus)
    TimeUs inA = 0, inB = 0;   // Einblenden von inA bis inB (inA == inB: keins)
    TimeUs outA = 0, outB = 0; // Ausblenden
    double gainAt(TimeUs t) const {
        double g = 1.0;
        constexpr double halfPi = 1.5707963267948966;
        if (inB > inA && t < inB) g *= std::sin(halfPi * std::clamp(static_cast<double>(t - inA) / (inB - inA), 0.0, 1.0));
        if (outB > outA && t > outA) g *= std::cos(halfPi * std::clamp(static_cast<double>(t - outA) / (outB - outA), 0.0, 1.0));
        return g;
    }
};

// Wie viel Material ein Clip vor seinem Anfang bzw. nach seinem Ende noch
// hat (in Timeline-Zeit). Erzeugte Inhalte und Standbilder haben beliebig viel.
TimeUs handleBefore(const Project& p, const Clip& c) {
    const MediaAsset* a = c.kind == ClipKind::Media ? p.findAsset(c.assetId) : nullptr;
    if (!a || a->duration <= 0) return INT64_MAX / 4;
    return static_cast<TimeUs>(static_cast<double>(c.in) / c.speed);
}
TimeUs handleAfter(const Project& p, const Clip& c) {
    const MediaAsset* a = c.kind == ClipKind::Media ? p.findAsset(c.assetId) : nullptr;
    if (!a || a->duration <= 0) return INT64_MAX / 4;
    const double used = static_cast<double>(c.in) + static_cast<double>(c.duration) * c.speed;
    return std::max<TimeUs>(0, static_cast<TimeUs>((static_cast<double>(a->duration) - used) / c.speed));
}

// Zeitfenster einer Ton-Überblendung zwischen prev und cur: möglichst mittig
// auf dem Schnitt, aber nur so weit, wie beide Clips Material haben. Fehlt
// es ganz, bleibt es ein harter Schnitt (statt Stille oder Versatz).
std::pair<TimeUs, TimeUs> crossfadeWindow(const Project& p, const Clip& prev, const Clip& cur) {
    TimeUs d = std::min({cur.transitionDuration, prev.duration, cur.duration});
    const TimeUs before = handleBefore(p, cur), after = handleAfter(p, prev);
    d = std::min(d, before + after);
    const TimeUs lo = cur.start - std::min(before, d);
    const TimeUs hi = cur.start + std::min(after, d) - d;
    const TimeUs ws = std::clamp(cur.start - d / 2, lo, std::max(lo, hi));
    return {ws, d};
}

TransitionRamps rampsFor(const Project& p, const Track& track, const Clip& c) {
    TransitionRamps r;
    r.from = c.start;
    r.to = c.end();
    if (const Clip* prev = p.previousAdjacent(track, c); prev && c.transition != TransitionType::None) {
        const TimeUs d = std::min({c.transitionDuration, prev->duration, c.duration});
        if (crossfadeLike(c.transition)) {
            const auto [ws, wd] = crossfadeWindow(p, *prev, c);
            r.from = std::min(c.start, ws);
            r.inA = ws;
            r.inB = ws + wd;
        } else {
            r.inA = c.start;
            r.inB = c.start + d / 2;
        }
    }
    if (const Clip* next = p.nextAdjacent(track, c); next && next->transition != TransitionType::None) {
        const TimeUs d = std::min({next->transitionDuration, c.duration, next->duration});
        if (crossfadeLike(next->transition)) {
            const auto [ws, wd] = crossfadeWindow(p, c, *next);
            r.to = std::max(c.end(), ws + wd);
            r.outA = ws;
            r.outB = ws + wd;
        } else {
            r.outA = c.end() - d / 2;
            r.outB = c.end();
        }
    }
    return r;
}

}  // namespace

void AudioMixer::render(std::int64_t position, float* interleaved, std::size_t frames) {
    std::fill(interleaved, interleaved + frames * 2, 0.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_project) return;
    const Project& project = *m_project;
    const int rate = project.sampleRate;
    const std::int64_t blockEnd = position + static_cast<std::int64_t>(frames);

    // Nach einem Sprung klingen Hall und Echo nicht von der alten Stelle nach.
    const bool jumped = position != m_expectedPosition;
    m_expectedPosition = blockEnd;

    bool anySolo = false;
    for (const Track& t : project.tracks)
        if (t.solo && t.hasSound()) anySolo = true;

    m_trackBuf.resize(frames * 2);
    for (const Track& track : project.tracks) {
        if (track.muted) continue;
        if (anySolo && !(track.solo && track.hasSound())) continue;
        std::fill(m_trackBuf.begin(), m_trackBuf.end(), 0.0f);
        bool anything = false;

        for (const Clip& clip : track.clips) {
            const std::int64_t clipStart = clip.start * rate / kSecond;
            const std::int64_t clipEnd = clip.end() * rate / kSecond;

            if (clip.kind == ClipKind::Midi) {
                const std::int64_t tail =
                    static_cast<std::int64_t>(synth::releaseTime(track.instrument) * rate);
                if (clipStart >= blockEnd || clipEnd + tail <= position) continue;
                m_scratch.assign(frames, 0.0f);   // mono
                for (const Note& note : clip.notes) {
                    // Nur Noten, die im sichtbaren Ausschnitt beginnen.
                    if (note.start < clip.in || note.start >= clip.in + clip.duration) continue;
                    const TimeUs onUs = clip.start + (note.start - clip.in);
                    const TimeUs offUs = std::min(onUs + note.length, clip.end());
                    const std::int64_t on = onUs * rate / kSecond;
                    const std::int64_t held = std::max<std::int64_t>(1, offUs * rate / kSecond - on);
                    if (on >= blockEnd || on + held + tail <= position) continue;
                    const int pitch = std::clamp(note.pitch + static_cast<int>(std::lround(clip.pitch)), 0, 127);
                    synth::renderNote(track.instrument, pitch, note.velocity, held, position - on,
                                      m_scratch.data(), frames, rate);
                }
                std::vector<float> stereo(frames * 2);
                for (std::size_t i = 0; i < frames; ++i) stereo[i * 2] = stereo[i * 2 + 1] = m_scratch[i];
                // Lautstärke-Keyframes und Blenden gelten nur innerhalb des Clips;
                // der Ausklang danach behält den Endwert.
                const Clip* c = &clip;
                applyClipGain(*c, position, rate, stereo.data(), frames);
                for (std::size_t i = 0; i < frames * 2; ++i) m_trackBuf[i] += stereo[i];
                anything = true;
                continue;
            }

            if (clip.kind != ClipKind::Media) continue;
            const TransitionRamps ramps = rampsFor(project, track, clip);
            const std::int64_t a = std::max(position, ramps.from * rate / kSecond);
            const std::int64_t b = std::min(blockEnd, ramps.to * rate / kSecond);
            if (a >= b) continue;
            const MediaAsset* asset = project.findAsset(clip.assetId);
            if (!asset || !asset->hasAudio) continue;

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
            applyClipGain(clip, a, rate, m_scratch.data(), n);
            if (ramps.inB > ramps.inA || ramps.outB > ramps.outA)
                for (std::size_t i = 0; i < n; ++i) {
                    const float g = static_cast<float>(ramps.gainAt((a + static_cast<std::int64_t>(i)) * kSecond / rate));
                    m_scratch[i * 2] *= g;
                    m_scratch[i * 2 + 1] *= g;
                }
            float* dst = m_trackBuf.data() + (a - position) * 2;
            for (std::size_t i = 0; i < n * 2; ++i) dst[i] += m_scratch[i];
            anything = true;
        }

        auto& state = m_trackStates[track.id];
        if (!state) state = std::make_unique<TrackState>();
        if (jumped) state->fx.reset();
        // Effekte laufen auch ohne Clip weiter, damit Hall und Echo ausklingen.
        if (!anything && track.fx.reverb <= 0.0 && track.fx.delay <= 0.0) continue;
        if (!track.fx.isNeutral())
            state->fx.process(m_trackBuf.data(), frames, track.fx, project.bpm, rate);

        // Lautstärke und Panorama (gleiche Leistung)
        const double angle = (std::clamp(track.pan, -1.0, 1.0) + 1.0) * 0.25 * 3.14159265358979323846;
        const float gl = static_cast<float>(track.gain * std::cos(angle) * 1.41421356);
        const float gr = static_cast<float>(track.gain * std::sin(angle) * 1.41421356);
        float peak = 0.0f;
        for (std::size_t i = 0; i < frames; ++i) {
            const float l = m_trackBuf[i * 2] * gl, r = m_trackBuf[i * 2 + 1] * gr;
            interleaved[i * 2] += l;
            interleaved[i * 2 + 1] += r;
            peak = std::max({peak, std::abs(l), std::abs(r)});
        }
        float& meter = m_levels.tracks[track.id];
        meter = std::max(meter, peak);
    }

    // Master: Lautstärke und Limiter (verhindert Übersteuern weich statt
    // hart abzuschneiden).
    const float master = static_cast<float>(project.masterGain);
    const float release = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * rate)));
    // -1 dBFS: Platz für Überschwinger des AAC-Encoders (sonst Clipping
    // beim Abspielen der fertigen Datei).
    constexpr float ceiling = 0.891f;
    float peak = 0.0f, minGain = 1.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        float l = interleaved[i * 2] * master, r = interleaved[i * 2 + 1] * master;
        const float level = std::max(std::abs(l), std::abs(r));
        const float needed = level * m_limiterGain > ceiling ? ceiling / level : 1.0f;
        if (needed < m_limiterGain)
            m_limiterGain = needed;
        else
            m_limiterGain += (1.0f - m_limiterGain) * release;
        l = std::clamp(l * m_limiterGain, -1.0f, 1.0f);
        r = std::clamp(r * m_limiterGain, -1.0f, 1.0f);
        interleaved[i * 2] = l;
        interleaved[i * 2 + 1] = r;
        peak = std::max({peak, std::abs(l), std::abs(r)});
        minGain = std::min(minGain, m_limiterGain);
    }
    m_levels.master = std::max(m_levels.master, peak);
    m_levels.reduction = std::min(m_levels.reduction, 20.0f * std::log10(std::max(minGain, 1e-4f)));
}

}  // namespace loggx
