#include "loggx/core/Project.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace loggx {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kFormatVersion = 2;

void sortClips(Track& track) {
    std::sort(track.clips.begin(), track.clips.end(),
              [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

// Wie viel Timeline-Zeit das Quellmaterial ab `in` bei `speed` noch hergibt.
TimeUs maxDurationFor(const Project& p, const Clip& c, TimeUs in, double speed) {
    if (c.kind != ClipKind::Media) return INT64_MAX / 4;
    const MediaAsset* asset = p.findAsset(c.assetId);
    if (!asset || asset->isStill || asset->duration <= 0) return INT64_MAX / 4;
    return static_cast<TimeUs>(static_cast<double>(asset->duration - in) / speed);
}

bool trackAccepts(const Track& track, const MediaAsset& asset) {
    if (track.kind == TrackKind::Video) return asset.hasVideo;
    if (track.kind == TrackKind::Audio) return asset.hasAudio;
    return false;
}

const char* kindName(TrackKind k) {
    switch (k) {
        case TrackKind::Video: return "video";
        case TrackKind::Audio: return "audio";
        default: return "instrument";
    }
}

TrackKind kindFromName(const std::string& s) {
    if (s == "audio") return TrackKind::Audio;
    if (s == "instrument") return TrackKind::Instrument;
    return TrackKind::Video;
}

const char* clipKindName(ClipKind k) {
    switch (k) {
        case ClipKind::Title: return "title";
        case ClipKind::Midi: return "midi";
        default: return "media";
    }
}

ClipKind clipKindFromName(const std::string& s) {
    if (s == "title") return ClipKind::Title;
    if (s == "midi") return ClipKind::Midi;
    return ClipKind::Media;
}

const char* const kInstrumentIds[kInstrumentCount] = {"piano", "epiano", "synth", "bass",
                                                      "pad",   "strings", "drums"};

const char* shapeName(MaskShape s) {
    switch (s) {
        case MaskShape::Rectangle: return "rectangle";
        case MaskShape::Ellipse: return "ellipse";
        default: return "none";
    }
}

MaskShape shapeFromName(const std::string& s) {
    if (s == "rectangle") return MaskShape::Rectangle;
    if (s == "ellipse") return MaskShape::Ellipse;
    return MaskShape::None;
}

std::string u8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

fs::path fromU8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

void shiftKeys(Clip& c, TimeUs delta) {
    for (auto& [name, list] : c.keys)
        for (auto& k : list) k.t -= delta;
}

}  // namespace

const char* instrumentName(InstrumentType type) {
    switch (type) {
        case InstrumentType::Piano: return "Klavier";
        case InstrumentType::ElectricPiano: return "E-Piano";
        case InstrumentType::Synth: return "Synthesizer";
        case InstrumentType::Bass: return "Bass";
        case InstrumentType::Pad: return "Fläche (Pad)";
        case InstrumentType::Strings: return "Streicher";
        case InstrumentType::Drums: return "Schlagzeug";
    }
    return "?";
}

const char* transitionName(TransitionType type) {
    switch (type) {
        case TransitionType::None: return "Kein";
        case TransitionType::Crossfade: return "Überblenden";
        case TransitionType::DipBlack: return "Über Schwarz";
        case TransitionType::DipWhite: return "Über Weiß";
        case TransitionType::Wipe: return "Wischen";
        case TransitionType::Slide: return "Schieben";
        case TransitionType::WipeUp: return "Wischen nach oben";
        case TransitionType::SlideUp: return "Schieben nach oben";
        case TransitionType::Circle: return "Kreis";
        case TransitionType::Zoom: return "Zoom";
        case TransitionType::BlurDissolve: return "Weich überblenden";
        case TransitionType::Pixelate: return "Verpixeln";
    }
    return "?";
}

bool isKeyframeParam(const std::string& name) {
    for (const char* p : kKeyframeParams)
        if (name == p) return true;
    return false;
}

// ----------------------------------------------------------------- Clip

bool ColorGrade::isNeutral() const {
    return exposure == 0.0 && contrast == 1.0 && saturation == 1.0 && temperature == 0.0 &&
           tint == 0.0 && lift == 0.0 && gamma == 1.0 && gain == 1.0 &&
           (lutPath.empty() || lutMix <= 0.0);
}

double Clip::baseValue(const std::string& param) const {
    if (param == "opacity") return opacity;
    if (param == "gainDb") return gain <= 0.001 ? -60.0 : 20.0 * std::log10(gain);
    if (param == "x") return x;
    if (param == "y") return y;
    if (param == "scale") return scale;
    if (param == "rotation") return rotation;
    if (param == "blur") return fx.blur;
    return 0.0;
}

double Clip::valueAt(const std::string& param, TimeUs t) const {
    const auto it = keys.find(param);
    if (it == keys.end() || it->second.empty()) return baseValue(param);
    const auto& list = it->second;
    const TimeUs rel = t - start;
    if (rel <= list.front().t) return list.front().value;
    if (rel >= list.back().t) return list.back().value;
    for (std::size_t i = 1; i < list.size(); ++i) {
        if (rel <= list[i].t) {
            const Keyframe& a = list[i - 1];
            const Keyframe& b = list[i];
            const double f = b.t == a.t ? 1.0
                                        : static_cast<double>(rel - a.t) / static_cast<double>(b.t - a.t);
            return a.value + (b.value - a.value) * f;
        }
    }
    return list.back().value;
}

double Clip::fadeAt(TimeUs t) const {
    double f = 1.0;
    if (fadeIn > 0) f = std::min(f, static_cast<double>(t - start) / static_cast<double>(fadeIn));
    if (fadeOut > 0) f = std::min(f, static_cast<double>(end() - t) / static_cast<double>(fadeOut));
    return std::clamp(f, 0.0, 1.0);
}

bool Clip::hasTransform() const {
    if (x != 0.0 || y != 0.0 || scale != 1.0 || rotation != 0.0) return true;
    for (const char* p : {"x", "y", "scale", "rotation"}) {
        const auto it = keys.find(p);
        if (it != keys.end() && !it->second.empty()) return true;
    }
    return false;
}

bool Clip::hasKeys() const {
    for (const auto& [name, list] : keys)
        if (!list.empty()) return true;
    return false;
}

// -------------------------------------------------------------- Project

Project Project::createDefault() {
    Project p;
    // Neue Videospuren landen oben, deshalb zuerst die untere anlegen.
    p.addTrack(TrackKind::Video, "Video 1");
    p.addTrack(TrackKind::Video, "Video 2");
    p.addTrack(TrackKind::Audio, "Audio 1");
    p.addTrack(TrackKind::Audio, "Musik");
    p.addTrack(TrackKind::Instrument, "Instrument 1");
    return p;
}

Project Project::createMusic() {
    Project p;
    p.mode = ProjectMode::Music;
    p.name = "Neuer Song";
    p.addTrack(TrackKind::Audio, "Audio 1");
    const std::string melody = p.addTrack(TrackKind::Instrument, "Melodie");
    const std::string bass = p.addTrack(TrackKind::Instrument, "Bass");
    const std::string drums = p.addTrack(TrackKind::Instrument, "Schlagzeug");
    p.findTrack(melody)->instrument = InstrumentType::ElectricPiano;
    p.findTrack(bass)->instrument = InstrumentType::Bass;
    p.findTrack(drums)->instrument = InstrumentType::Drums;
    return p;
}

TimeUs Project::nearestBeat(TimeUs t) const {
    const double beat = 60.0 / bpm * kSecond;
    const double n = std::round(static_cast<double>(t - beatOffset) / beat);
    return beatOffset + static_cast<TimeUs>(n * beat);
}

const Clip* Project::previousAdjacent(const Track& track, const Clip& c) const {
    for (const auto& o : track.clips)
        if (o.id != c.id && std::llabs(o.end() - c.start) <= 1000) return &o;
    return nullptr;
}

const Clip* Project::nextAdjacent(const Track& track, const Clip& c) const {
    for (const auto& o : track.clips)
        if (o.id != c.id && std::llabs(c.end() - o.start) <= 1000) return &o;
    return nullptr;
}

int Project::cutOnBeats(const std::string& clipId, int everyBeats) {
    const Clip* c = findClip(clipId);
    if (!c || everyBeats < 1) return 0;
    std::vector<TimeUs> cuts;
    const double beat = 60.0 / bpm * kSecond;
    // Erster Schlag nach dem Clip-Anfang, dann jeder n-te.
    std::int64_t n = static_cast<std::int64_t>(std::ceil(static_cast<double>(c->start - beatOffset) / beat));
    for (TimeUs t = beatTime(n); t < c->end(); t = beatTime(n += everyBeats))
        if (t - c->start >= kMinClipDuration && c->end() - t >= kMinClipDuration) cuts.push_back(t);
    // Von hinten schneiden: der linke Teil behält jeweils die id.
    std::string current = clipId;
    int done = 0;
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
        if (!splitClip(current, *it).empty()) ++done;
    return done;
}

TimeUs Project::duration() const {
    TimeUs d = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips) d = std::max(d, c.end());
    return d;
}

const MediaAsset* Project::findAsset(const std::string& id) const {
    for (const auto& a : assets)
        if (a.id == id) return &a;
    return nullptr;
}

Track* Project::findTrack(const std::string& id) {
    for (auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* Project::findTrack(const std::string& id) const {
    return const_cast<Project*>(this)->findTrack(id);
}

Clip* Project::findClip(const std::string& id, Track** track) {
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == id) {
                if (track) *track = &t;
                return &c;
            }
    return nullptr;
}

const Clip* Project::findClip(const std::string& id, const Track** track) const {
    Track* t = nullptr;
    const Clip* c = const_cast<Project*>(this)->findClip(id, &t);
    if (track) *track = t;
    return c;
}

std::string Project::addAsset(MediaAsset asset) {
    asset.id = "a" + std::to_string(nextId++);
    assets.push_back(std::move(asset));
    return assets.back().id;
}

std::string Project::addTrack(TrackKind kind, const std::string& name) {
    Track t;
    t.id = "t" + std::to_string(nextId++);
    t.kind = kind;
    if (name.empty()) {
        int n = 1;
        for (const auto& other : tracks)
            if (other.kind == kind) ++n;
        const char* base = kind == TrackKind::Video   ? "Video "
                           : kind == TrackKind::Audio ? "Audio "
                                                      : "Instrument ";
        t.name = base + std::to_string(n);
    } else {
        t.name = name;
    }
    if (kind == TrackKind::Instrument) t.gain = 0.8;
    // Videospuren oben einfügen (neue Spur liegt über den alten), Audio- und
    // Instrumentspuren unten.
    if (kind == TrackKind::Video) {
        tracks.insert(tracks.begin(), t);
        return tracks.front().id;
    }
    tracks.push_back(t);
    return tracks.back().id;
}

bool Project::removeTrack(const std::string& trackId) {
    const auto it = std::find_if(tracks.begin(), tracks.end(),
                                 [&](const Track& t) { return t.id == trackId; });
    if (it == tracks.end() || !it->clips.empty()) return false;
    tracks.erase(it);
    return true;
}

bool Project::rangeFree(const Track& track, TimeUs start, TimeUs duration,
                        const std::string& ignoreClipId) const {
    const TimeUs end = start + duration;
    for (const auto& c : track.clips) {
        if (c.id == ignoreClipId) continue;
        if (start < c.end() && c.start < end) return false;
    }
    return true;
}

TimeUs Project::firstFreeStart(const Track& track, TimeUs desired, TimeUs duration) const {
    TimeUs start = std::max<TimeUs>(0, desired);
    // Clips sind sortiert; bei Kollision hinter den kollidierenden Clip springen.
    for (const auto& c : track.clips) {
        if (start < c.end() && c.start < start + duration) start = c.end();
    }
    return start;
}

std::string Project::addClip(const std::string& trackId, const std::string& assetId,
                             TimeUs start) {
    Track* track = findTrack(trackId);
    const MediaAsset* asset = findAsset(assetId);
    if (!track || !asset || !trackAccepts(*track, *asset)) return {};

    Clip c;
    c.assetId = assetId;
    c.duration = asset->isStill || asset->duration <= 0 ? kStillImageDuration : asset->duration;
    c.start = firstFreeStart(*track, start, c.duration);
    c.id = "c" + std::to_string(nextId++);
    track->clips.push_back(c);
    sortClips(*track);
    return c.id;
}

std::string Project::pasteClip(const Clip& source, const std::string& trackId, TimeUs start) {
    Track* track = findTrack(trackId);
    if (!track || source.duration <= 0) return {};
    bool fits = false;
    switch (source.kind) {
    case ClipKind::Title: fits = track->kind == TrackKind::Video; break;
    case ClipKind::Midi: fits = track->kind == TrackKind::Instrument; break;
    case ClipKind::Media:
        if (const MediaAsset* asset = findAsset(source.assetId)) fits = trackAccepts(*track, *asset);
        break;
    }
    if (!fits) return {};
    Clip c = source;
    c.start = firstFreeStart(*track, std::max<TimeUs>(0, start), c.duration);
    c.id = "c" + std::to_string(nextId++);
    track->clips.push_back(c);
    sortClips(*track);
    return c.id;
}

bool Project::retime(double newBpm) {
    if (!(newBpm >= 20.0 && newBpm <= 300.0) || bpm <= 0.0) return false;
    const double f = bpm / newBpm;   // Faktor für alle Zeiten
    if (std::abs(f - 1.0) < 1e-9) return true;
    auto scale = [f](TimeUs t) { return static_cast<TimeUs>(std::llround(static_cast<double>(t) * f)); };
    // Erst prüfen, dann ändern (alles oder nichts).
    for (const auto& t : tracks)
        if (t.kind == TrackKind::Audio)
            for (const auto& c : t.clips)
                if (c.kind == ClipKind::Media && !(c.speed / f >= 0.25 && c.speed / f <= 4.0)) return false;
    for (auto& t : tracks) {
        if (t.kind == TrackKind::Video) continue;
        for (auto& c : t.clips) {
            c.start = scale(c.start);
            c.duration = std::max(kMinClipDuration, scale(c.duration));
            c.fadeIn = scale(c.fadeIn);
            c.fadeOut = scale(c.fadeOut);
            c.transitionDuration = scale(c.transitionDuration);
            for (auto& [name, list] : c.keys)
                for (auto& k : list) k.t = scale(k.t);
            if (c.kind == ClipKind::Midi) {
                c.in = scale(c.in);
                for (auto& n : c.notes) {
                    n.start = scale(n.start);
                    n.length = std::max<TimeUs>(1000, scale(n.length));
                }
            } else if (c.kind == ClipKind::Media) {
                c.speed /= f;   // Quellbereich bleibt gleich
            }
        }
        sortClips(t);
    }
    beatOffset = scale(beatOffset);
    bpm = newBpm;
    return true;
}

int Project::transposeAll(int semitones) {
    int changed = 0;
    for (auto& t : tracks) {
        // Schlagzeug nicht transponieren (sonst andere Trommeln)
        if (t.kind == TrackKind::Video || (t.kind == TrackKind::Instrument && t.instrument == InstrumentType::Drums)) continue;
        for (auto& c : t.clips) {
            if (c.kind == ClipKind::Title) continue;
            const double limit = c.kind == ClipKind::Midi ? 24.0 : 12.0;
            const double next = std::clamp(c.pitch + semitones, -limit, limit);
            if (next != c.pitch) {
                c.pitch = next;
                ++changed;
            }
        }
    }
    return changed;
}

std::string Project::addTitleClip(const std::string& trackId, TimeUs start,
                                  const std::string& text) {
    Track* track = findTrack(trackId);
    if (!track || track->kind != TrackKind::Video) return {};
    Clip c;
    c.kind = ClipKind::Title;
    c.title.text = text;
    c.duration = kTitleDuration;
    c.fadeIn = c.fadeOut = 300'000;
    c.start = firstFreeStart(*track, start, c.duration);
    c.id = "c" + std::to_string(nextId++);
    track->clips.push_back(c);
    sortClips(*track);
    return c.id;
}

std::string Project::addMidiClip(const std::string& trackId, TimeUs start, TimeUs duration) {
    Track* track = findTrack(trackId);
    if (!track || track->kind != TrackKind::Instrument) return {};
    Clip c;
    c.kind = ClipKind::Midi;
    // Standard: vier Takte.
    c.duration = duration > 0 ? duration : beatDuration() * beatsPerBar * 4;
    c.start = firstFreeStart(*track, start, c.duration);
    c.id = "c" + std::to_string(nextId++);
    track->clips.push_back(c);
    sortClips(*track);
    return c.id;
}

std::string Project::splitClip(const std::string& clipId, TimeUs at) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip) return {};
    if (at - clip->start < kMinClipDuration || clip->end() - at < kMinClipDuration) return {};

    Clip right = *clip;
    right.id = "c" + std::to_string(nextId++);
    right.in = clip->sourceTime(at);
    right.start = at;
    right.duration = clip->end() - at;
    // Keyframes bleiben an ihrer Stelle auf der Timeline.
    shiftKeys(right, at - clip->start);
    // An der Schnittstelle nicht doppelt blenden.
    right.fadeIn = 0;
    clip->fadeOut = 0;
    clip->duration = at - clip->start;
    track->clips.push_back(right);   // macht `clip` ungültig, wird nicht mehr benutzt
    sortClips(*track);
    return right.id;
}

bool Project::moveClip(const std::string& clipId, const std::string& targetTrackId,
                       TimeUs newStart) {
    Track* from = nullptr;
    Clip* clip = findClip(clipId, &from);
    Track* to = findTrack(targetTrackId);
    if (!clip || !to || to->kind != from->kind) return false;
    newStart = std::max<TimeUs>(0, newStart);
    if (!rangeFree(*to, newStart, clip->duration, clipId)) return false;

    if (to == from) {
        clip->start = newStart;
        sortClips(*from);
        return true;
    }
    Clip moved = *clip;
    moved.start = newStart;
    from->clips.erase(from->clips.begin() + (clip - from->clips.data()));
    to->clips.push_back(moved);
    sortClips(*to);
    return true;
}

bool Project::trimClipStart(const std::string& clipId, TimeUs newStart) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip) return false;
    const MediaAsset* asset = clip->kind == ClipKind::Media ? findAsset(clip->assetId) : nullptr;
    if (clip->kind == ClipKind::Media && !asset) return false;

    const TimeUs delta = newStart - clip->start;
    const TimeUs newDuration = clip->duration - delta;
    TimeUs newIn = clip->in + static_cast<TimeUs>(static_cast<double>(delta) * clip->speed);
    if (clip->kind == ClipKind::Title || (asset && asset->isStill)) newIn = 0;
    if (newStart < 0 || newDuration < kMinClipDuration || newIn < 0) return false;
    if (!rangeFree(*track, newStart, newDuration, clipId)) return false;

    clip->start = newStart;
    clip->duration = newDuration;
    clip->in = newIn;
    shiftKeys(*clip, delta);
    return true;
}

bool Project::trimClipEnd(const std::string& clipId, TimeUs newEnd) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip) return false;
    if (clip->kind == ClipKind::Media && !findAsset(clip->assetId)) return false;

    const TimeUs newDuration = newEnd - clip->start;
    if (newDuration < kMinClipDuration) return false;
    if (newDuration > maxDurationFor(*this, *clip, clip->in, clip->speed)) return false;
    if (!rangeFree(*track, clip->start, newDuration, clipId)) return false;
    clip->duration = newDuration;
    return true;
}

bool Project::setClipSpeed(const std::string& clipId, double speed) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip || clip->kind != ClipKind::Media || !(speed >= 0.1 && speed <= 10.0)) return false;

    // Der gezeigte Quellbereich bleibt gleich, die Timeline-Länge ändert sich.
    const double sourceLength = static_cast<double>(clip->duration) * clip->speed;
    TimeUs newDuration = static_cast<TimeUs>(std::llround(sourceLength / speed));
    newDuration = std::max(newDuration, kMinClipDuration);
    // Reicht der Platz bis zum nächsten Clip nicht, wird hinten gekürzt.
    for (const auto& other : track->clips) {
        if (other.id == clipId || other.start < clip->start) continue;
        newDuration = std::min(newDuration, other.start - clip->start);
    }
    if (newDuration < kMinClipDuration) return false;
    clip->speed = speed;
    clip->duration = newDuration;
    return true;
}

bool Project::removeClip(const std::string& clipId, bool ripple) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip) return false;
    const TimeUs start = clip->start;
    const TimeUs length = clip->duration;
    track->clips.erase(track->clips.begin() + (clip - track->clips.data()));
    if (ripple) {
        for (auto& c : track->clips)
            if (c.start > start) c.start -= length;
    }
    return true;
}

bool Project::removeAsset(const std::string& assetId) {
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            if (c.kind == ClipKind::Media && c.assetId == assetId) return false;
    const auto it = std::find_if(assets.begin(), assets.end(),
                                 [&](const MediaAsset& a) { return a.id == assetId; });
    if (it == assets.end()) return false;
    assets.erase(it);
    return true;
}

bool Project::setKeyframe(const std::string& clipId, const std::string& param, TimeUs t,
                          double value) {
    Clip* clip = findClip(clipId);
    if (!clip || !isKeyframeParam(param)) return false;
    const TimeUs rel = std::clamp<TimeUs>(t - clip->start, 0, clip->duration);
    auto& list = clip->keys[param];
    // Innerhalb von 1 ms gilt als derselbe Keyframe.
    for (auto& k : list)
        if (std::llabs(k.t - rel) < 1000) {
            k.value = value;
            return true;
        }
    list.push_back({rel, value});
    std::sort(list.begin(), list.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
    return true;
}

bool Project::removeKeyframe(const std::string& clipId, const std::string& param, TimeUs t) {
    Clip* clip = findClip(clipId);
    if (!clip) return false;
    const auto it = clip->keys.find(param);
    if (it == clip->keys.end()) return false;
    const TimeUs rel = t - clip->start;
    auto& list = it->second;
    const auto k = std::find_if(list.begin(), list.end(),
                                [&](const Keyframe& kf) { return std::llabs(kf.t - rel) < 1000; });
    if (k == list.end()) return false;
    // Der Wert an dieser Stelle wird zum neuen Grundwert, wenn der letzte
    // Keyframe verschwindet.
    const double last = k->value;
    list.erase(k);
    if (list.empty()) {
        clip->keys.erase(it);
        if (param == "opacity") clip->opacity = last;
        else if (param == "gainDb") clip->gain = last <= -59.9 ? 0.0 : std::pow(10.0, last / 20.0);
        else if (param == "x") clip->x = last;
        else if (param == "y") clip->y = last;
        else if (param == "scale") clip->scale = last;
        else if (param == "rotation") clip->rotation = last;
        else if (param == "blur") clip->fx.blur = last;
    }
    return true;
}

bool Project::clearKeyframes(const std::string& clipId, const std::string& param) {
    Clip* clip = findClip(clipId);
    if (!clip) return false;
    return clip->keys.erase(param) > 0;
}

// ---------------------------------------------------------------- JSON

namespace {

json clipToJson(const Clip& c) {
    json jc = {{"id", c.id}, {"start", c.start}, {"in", c.in}, {"duration", c.duration}};
    if (c.kind != ClipKind::Media) jc["kind"] = clipKindName(c.kind);
    if (c.kind == ClipKind::Media) jc["asset"] = c.assetId;
    if (c.speed != 1.0) jc["speed"] = c.speed;
    if (c.gain != 1.0) jc["gain"] = c.gain;
    if (c.pitch != 0.0) jc["pitch"] = c.pitch;
    if (c.opacity != 1.0) jc["opacity"] = c.opacity;
    if (c.fadeIn > 0) jc["fadeIn"] = c.fadeIn;
    if (c.fadeOut > 0) jc["fadeOut"] = c.fadeOut;
    if (c.x != 0.0 || c.y != 0.0 || c.scale != 1.0 || c.rotation != 0.0)
        jc["transform"] = {{"x", c.x}, {"y", c.y}, {"scale", c.scale}, {"rotation", c.rotation}};
    if (c.mask.shape != MaskShape::None) {
        jc["mask"] = {{"shape", shapeName(c.mask.shape)}, {"cx", c.mask.cx},
                      {"cy", c.mask.cy}, {"w", c.mask.w}, {"h", c.mask.h},
                      {"feather", c.mask.feather}, {"invert", c.mask.invert}};
    }
    if (!c.color.isNeutral() || !c.color.lutPath.empty()) {
        const auto& g = c.color;
        jc["color"] = {{"exposure", g.exposure}, {"contrast", g.contrast},
                       {"saturation", g.saturation}, {"temperature", g.temperature},
                       {"tint", g.tint}, {"lift", g.lift}, {"gamma", g.gamma},
                       {"gain", g.gain}, {"lut", g.lutPath}, {"lutMix", g.lutMix}};
    }
    if (!c.fx.isNeutral()) {
        const auto& f = c.fx;
        jc["effects"] = {{"blur", f.blur}, {"chromaKey", f.chromaKey}, {"keyR", f.keyR},
                         {"keyG", f.keyG}, {"keyB", f.keyB}, {"keyTolerance", f.keyTolerance},
                         {"keySoftness", f.keySoftness}, {"vignette", f.vignette},
                         {"sharpen", f.sharpen}, {"grain", f.grain}, {"pixelate", f.pixelate},
                         {"glow", f.glow}, {"rgbSplit", f.rgbSplit}, {"sepia", f.sepia},
                         {"invert", f.invert}, {"mirrorH", f.mirrorH}, {"mirrorV", f.mirrorV}};
    }
    if (c.kind == ClipKind::Title) {
        const auto& t = c.title;
        jc["title"] = {{"text", t.text}, {"font", t.font}, {"size", t.size},
                       {"color", t.color}, {"bold", t.bold}, {"align", t.align},
                       {"posY", t.posY}, {"box", t.box}, {"shadow", t.shadow}};
    }
    if (c.kind == ClipKind::Midi) {
        json notes = json::array();
        for (const auto& n : c.notes)
            notes.push_back({n.pitch, n.start, n.length, n.velocity});
        jc["notes"] = std::move(notes);
    }
    if (c.transition != TransitionType::None)
        jc["transition"] = {{"type", static_cast<int>(c.transition)}, {"duration", c.transitionDuration}};
    if (c.hasKeys()) {
        json keys = json::object();
        for (const auto& [name, list] : c.keys) {
            if (list.empty()) continue;
            json arr = json::array();
            for (const auto& k : list) arr.push_back({k.t, k.value});
            keys[name] = std::move(arr);
        }
        jc["keys"] = std::move(keys);
    }
    return jc;
}

Clip clipFromJson(const json& jc) {
    Clip c;
    c.id = jc.at("id").get<std::string>();
    c.kind = clipKindFromName(jc.value("kind", "media"));
    c.assetId = jc.value("asset", "");
    c.start = jc.value("start", TimeUs{0});
    c.in = jc.value("in", TimeUs{0});
    c.duration = jc.value("duration", TimeUs{0});
    c.speed = jc.value("speed", 1.0);
    c.gain = jc.value("gain", 1.0);
    c.pitch = jc.value("pitch", 0.0);
    c.opacity = jc.value("opacity", 1.0);
    c.fadeIn = std::max<TimeUs>(0, jc.value("fadeIn", TimeUs{0}));
    c.fadeOut = std::max<TimeUs>(0, jc.value("fadeOut", TimeUs{0}));
    if (jc.contains("transform")) {
        const auto& t = jc["transform"];
        c.x = t.value("x", 0.0);
        c.y = t.value("y", 0.0);
        c.scale = t.value("scale", 1.0);
        c.rotation = t.value("rotation", 0.0);
    }
    if (jc.contains("mask")) {
        const auto& jm = jc["mask"];
        c.mask.shape = shapeFromName(jm.value("shape", "none"));
        c.mask.cx = jm.value("cx", 0.5);
        c.mask.cy = jm.value("cy", 0.5);
        c.mask.w = jm.value("w", 0.5);
        c.mask.h = jm.value("h", 0.5);
        c.mask.feather = jm.value("feather", 0.0);
        c.mask.invert = jm.value("invert", false);
    }
    if (jc.contains("color")) {
        const auto& g = jc["color"];
        c.color.exposure = g.value("exposure", 0.0);
        c.color.contrast = g.value("contrast", 1.0);
        c.color.saturation = g.value("saturation", 1.0);
        c.color.temperature = g.value("temperature", 0.0);
        c.color.tint = g.value("tint", 0.0);
        c.color.lift = g.value("lift", 0.0);
        c.color.gamma = g.value("gamma", 1.0);
        c.color.gain = g.value("gain", 1.0);
        c.color.lutPath = g.value("lut", "");
        c.color.lutMix = g.value("lutMix", 1.0);
    }
    if (jc.contains("effects")) {
        const auto& f = jc["effects"];
        c.fx.blur = f.value("blur", 0.0);
        c.fx.chromaKey = f.value("chromaKey", false);
        c.fx.keyR = f.value("keyR", 0.0);
        c.fx.keyG = f.value("keyG", 1.0);
        c.fx.keyB = f.value("keyB", 0.0);
        c.fx.keyTolerance = f.value("keyTolerance", 0.3);
        c.fx.keySoftness = f.value("keySoftness", 0.1);
        c.fx.vignette = f.value("vignette", 0.0);
        c.fx.sharpen = std::clamp(f.value("sharpen", 0.0), 0.0, 1.0);
        c.fx.grain = std::clamp(f.value("grain", 0.0), 0.0, 1.0);
        c.fx.pixelate = std::clamp(f.value("pixelate", 0.0), 0.0, 1.0);
        c.fx.glow = std::clamp(f.value("glow", 0.0), 0.0, 1.0);
        c.fx.rgbSplit = std::clamp(f.value("rgbSplit", 0.0), 0.0, 1.0);
        c.fx.sepia = std::clamp(f.value("sepia", 0.0), 0.0, 1.0);
        c.fx.invert = f.value("invert", false);
        c.fx.mirrorH = f.value("mirrorH", false);
        c.fx.mirrorV = f.value("mirrorV", false);
    }
    if (jc.contains("title")) {
        const auto& t = jc["title"];
        c.title.text = t.value("text", "");
        c.title.font = t.value("font", "");
        c.title.size = t.value("size", 0.09);
        c.title.color = t.value("color", std::uint32_t{0xFFFFFFFF});
        c.title.bold = t.value("bold", true);
        c.title.align = std::clamp(t.value("align", 1), 0, 2);
        c.title.posY = t.value("posY", 0.5);
        c.title.box = t.value("box", 0.0);
        c.title.shadow = t.value("shadow", true);
    }
    for (const auto& n : jc.value("notes", json::array())) {
        if (!n.is_array() || n.size() < 4) continue;
        Note note{n[0].get<int>(), n[1].get<TimeUs>(), n[2].get<TimeUs>(), n[3].get<double>()};
        if (note.pitch < 0 || note.pitch > 127 || note.length <= 0) continue;
        c.notes.push_back(note);
    }
    if (jc.contains("transition")) {
        const int type = jc["transition"].value("type", 0);
        if (type > 0 && type < kTransitionCount) c.transition = static_cast<TransitionType>(type);
        c.transitionDuration = std::clamp<TimeUs>(jc["transition"].value("duration", TimeUs{500000}), 40'000, 10 * kSecond);
    }
    if (jc.contains("keys")) {
        for (const auto& [name, arr] : jc["keys"].items()) {
            if (!isKeyframeParam(name)) continue;
            std::vector<Keyframe> list;
            for (const auto& k : arr)
                if (k.is_array() && k.size() >= 2) list.push_back({k[0].get<TimeUs>(), k[1].get<double>()});
            std::sort(list.begin(), list.end(),
                      [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
            if (!list.empty()) c.keys[name] = std::move(list);
        }
    }
    return c;
}

}  // namespace

std::string Project::toJson(const std::string& baseDir) const {
    json j;
    j["format"] = "loggx";
    j["version"] = kFormatVersion;
    j["name"] = name;
    j["video"] = {{"width", width}, {"height", height}, {"fpsNum", fpsNum}, {"fpsDen", fpsDen}};
    j["audio"] = {{"sampleRate", sampleRate}, {"masterGain", masterGain}};
    j["music"] = {{"bpm", bpm}, {"beatsPerBar", beatsPerBar}, {"beatOffset", beatOffset}};
    j["mode"] = mode == ProjectMode::Music ? "music" : "video";
    j["nextId"] = nextId;

    j["assets"] = json::array();
    for (const auto& a : assets) {
        json ja = {{"id", a.id},         {"name", a.name},         {"path", a.path},
                   {"duration", a.duration}, {"width", a.width},   {"height", a.height},
                   {"fps", a.fps},       {"hasVideo", a.hasVideo}, {"hasAudio", a.hasAudio},
                   {"isStill", a.isStill}};
        if (a.bpm > 0.0 || a.key >= 0)
            ja["analysis"] = {{"bpm", a.bpm}, {"firstBeat", a.firstBeat}, {"key", a.key}, {"minor", a.keyMinor}};
        if (!baseDir.empty()) {
            std::error_code ec;
            const fs::path rel = fs::relative(fromU8(a.path), fromU8(baseDir), ec);
            if (!ec && !rel.empty()) ja["relativePath"] = u8(rel);
        }
        j["assets"].push_back(std::move(ja));
    }

    j["tracks"] = json::array();
    for (const auto& t : tracks) {
        json jt = {{"id", t.id}, {"name", t.name}, {"kind", kindName(t.kind)},
                   {"muted", t.muted}, {"gain", t.gain}};
        if (t.solo) jt["solo"] = true;
        if (t.pan != 0.0) jt["pan"] = t.pan;
        if (t.kind == TrackKind::Instrument)
            jt["instrument"] = kInstrumentIds[static_cast<int>(t.instrument)];
        if (!t.fx.isNeutral())
            jt["effects"] = {{"eqLow", t.fx.eqLow}, {"eqMid", t.fx.eqMid},
                             {"eqHigh", t.fx.eqHigh}, {"compressor", t.fx.compressor},
                             {"reverb", t.fx.reverb}, {"delay", t.fx.delay}};
        jt["clips"] = json::array();
        for (const auto& c : t.clips) jt["clips"].push_back(clipToJson(c));
        j["tracks"].push_back(std::move(jt));
    }
    return j.dump(1);
}

bool Project::fromJson(const std::string& text, const std::string& baseDir, Project& out,
                       std::string* error) {
    try {
        const json j = json::parse(text);
        if (j.value("format", "") != "loggx") throw std::runtime_error("Keine LoggX-Projektdatei");
        if (j.value("version", 0) > kFormatVersion)
            throw std::runtime_error("Projekt stammt aus einer neueren LoggX-Version");

        Project p;
        p.name = j.value("name", p.name);
        if (j.contains("video")) {
            const auto& v = j["video"];
            p.width = v.value("width", p.width);
            p.height = v.value("height", p.height);
            p.fpsNum = v.value("fpsNum", p.fpsNum);
            p.fpsDen = v.value("fpsDen", p.fpsDen);
        }
        if (j.contains("audio")) {
            p.sampleRate = j["audio"].value("sampleRate", p.sampleRate);
            p.masterGain = j["audio"].value("masterGain", 1.0);
        }
        if (j.contains("music")) {
            p.bpm = std::clamp(j["music"].value("bpm", 120.0), 20.0, 300.0);
            p.beatsPerBar = std::clamp(j["music"].value("beatsPerBar", 4), 1, 16);
            p.beatOffset = j["music"].value("beatOffset", TimeUs{0});
        }
        if (p.width < 16 || p.height < 16 || p.fpsNum < 1 || p.fpsDen < 1 || p.sampleRate < 8000)
            throw std::runtime_error("Ungültige Projekteinstellungen");
        p.nextId = j.value("nextId", std::uint64_t{1});
        p.mode = j.value("mode", "video") == "music" ? ProjectMode::Music : ProjectMode::Video;

        for (const auto& ja : j.value("assets", json::array())) {
            MediaAsset a;
            a.id = ja.at("id").get<std::string>();
            a.name = ja.value("name", "");
            a.path = ja.value("path", "");
            a.duration = ja.value("duration", TimeUs{0});
            a.width = ja.value("width", 0);
            a.height = ja.value("height", 0);
            a.fps = ja.value("fps", 0.0);
            a.hasVideo = ja.value("hasVideo", false);
            a.hasAudio = ja.value("hasAudio", false);
            a.isStill = ja.value("isStill", false);
            if (ja.contains("analysis") && ja["analysis"].is_object()) {
                const auto& an = ja["analysis"];
                a.bpm = an.value("bpm", 0.0);
                a.firstBeat = an.value("firstBeat", TimeUs{0});
                a.key = std::clamp(an.value("key", -1), -1, 11);
                a.keyMinor = an.value("minor", false);
            }
            // Wurde der Projektordner verschoben, gilt der relative Pfad.
            const std::string rel = ja.value("relativePath", "");
            std::error_code ec;
            if (!baseDir.empty() && !rel.empty() && !fs::exists(fromU8(a.path), ec)) {
                const fs::path candidate = fromU8(baseDir) / fromU8(rel);
                if (fs::exists(candidate, ec)) a.path = u8(candidate.lexically_normal());
            }
            p.assets.push_back(std::move(a));
        }

        for (const auto& jt : j.value("tracks", json::array())) {
            Track t;
            t.id = jt.at("id").get<std::string>();
            t.name = jt.value("name", "");
            t.kind = kindFromName(jt.value("kind", "video"));
            t.muted = jt.value("muted", false);
            t.solo = jt.value("solo", false);
            t.gain = jt.value("gain", 1.0);
            t.pan = std::clamp(jt.value("pan", 0.0), -1.0, 1.0);
            const std::string inst = jt.value("instrument", "piano");
            for (int i = 0; i < kInstrumentCount; ++i)
                if (inst == kInstrumentIds[i]) t.instrument = static_cast<InstrumentType>(i);
            if (jt.contains("effects")) {
                const auto& f = jt["effects"];
                t.fx.eqLow = f.value("eqLow", 0.0);
                t.fx.eqMid = f.value("eqMid", 0.0);
                t.fx.eqHigh = f.value("eqHigh", 0.0);
                t.fx.compressor = f.value("compressor", 0.0);
                t.fx.reverb = f.value("reverb", 0.0);
                t.fx.delay = f.value("delay", 0.0);
            }
            for (const auto& jc : jt.value("clips", json::array())) {
                Clip c = clipFromJson(jc);
                // Kaputte Clips überspringen statt das ganze Projekt abzulehnen.
                const bool kindOk = (c.kind == ClipKind::Media && t.kind != TrackKind::Instrument &&
                                     p.findAsset(c.assetId)) ||
                                    (c.kind == ClipKind::Title && t.kind == TrackKind::Video) ||
                                    (c.kind == ClipKind::Midi && t.kind == TrackKind::Instrument);
                if (!kindOk || c.duration < kMinClipDuration || c.start < 0 || c.in < 0 ||
                    !(c.speed >= 0.1 && c.speed <= 10.0))
                    continue;
                if (!p.rangeFree(t, c.start, c.duration)) continue;
                t.clips.push_back(std::move(c));
            }
            sortClips(t);
            p.tracks.push_back(std::move(t));
        }
        out = std::move(p);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool Project::saveFile(const std::string& path, std::string* error) const {
    const fs::path file = fromU8(path);
    const std::string baseDir = u8(fs::absolute(file).parent_path());
    // Erst in eine temporäre Datei schreiben, damit ein Absturz beim
    // Speichern nie ein halbes Projekt hinterlässt.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "Datei kann nicht geschrieben werden";
            return false;
        }
        f << toJson(baseDir);
        if (!f.good()) {
            if (error) *error = "Schreibfehler";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    if (ec) {
        if (error) *error = ec.message();
        return false;
    }
    return true;
}

bool Project::loadFile(const std::string& path, Project& out, std::string* error) {
    const fs::path file = fromU8(path);
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (error) *error = "Datei kann nicht geöffnet werden";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return fromJson(ss.str(), u8(fs::absolute(file).parent_path()), out, error);
}

}  // namespace loggx
