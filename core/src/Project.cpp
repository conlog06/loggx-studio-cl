#include "loggx/core/Project.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace loggx {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void sortClips(Track& track) {
    std::sort(track.clips.begin(), track.clips.end(),
              [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

// Wie viel Timeline-Zeit das Quellmaterial ab `in` bei `speed` noch hergibt.
TimeUs maxDurationFor(const MediaAsset& asset, TimeUs in, double speed) {
    if (asset.isStill || asset.duration <= 0) return INT64_MAX / 4;
    return static_cast<TimeUs>(static_cast<double>(asset.duration - in) / speed);
}

bool trackAccepts(const Track& track, const MediaAsset& asset) {
    return track.kind == TrackKind::Video ? asset.hasVideo : asset.hasAudio;
}

const char* kindName(TrackKind k) { return k == TrackKind::Video ? "video" : "audio"; }

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

}  // namespace

Project Project::createDefault() {
    Project p;
    // Neue Videospuren landen oben, deshalb zuerst die untere anlegen.
    p.addTrack(TrackKind::Video, "Video 1");
    p.addTrack(TrackKind::Video, "Video 2");
    p.addTrack(TrackKind::Audio, "Audio 1");
    p.addTrack(TrackKind::Audio, "Musik");
    return p;
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
        t.name = std::string(kind == TrackKind::Video ? "Video " : "Audio ") + std::to_string(n);
    } else {
        t.name = name;
    }
    // Videospuren oben einfügen (neue Spur liegt über den alten), Audiospuren unten.
    if (kind == TrackKind::Video) {
        tracks.insert(tracks.begin(), t);
        return tracks.front().id;
    }
    tracks.push_back(t);
    return tracks.back().id;
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
    const MediaAsset* asset = findAsset(clip->assetId);
    if (!asset) return false;

    const TimeUs delta = newStart - clip->start;
    const TimeUs newDuration = clip->duration - delta;
    TimeUs newIn = clip->in + static_cast<TimeUs>(static_cast<double>(delta) * clip->speed);
    if (asset->isStill) newIn = 0;
    if (newStart < 0 || newDuration < kMinClipDuration || newIn < 0) return false;
    if (!rangeFree(*track, newStart, newDuration, clipId)) return false;

    clip->start = newStart;
    clip->duration = newDuration;
    clip->in = newIn;
    return true;
}

bool Project::trimClipEnd(const std::string& clipId, TimeUs newEnd) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip) return false;
    const MediaAsset* asset = findAsset(clip->assetId);
    if (!asset) return false;

    const TimeUs newDuration = newEnd - clip->start;
    if (newDuration < kMinClipDuration) return false;
    if (newDuration > maxDurationFor(*asset, clip->in, clip->speed)) return false;
    if (!rangeFree(*track, clip->start, newDuration, clipId)) return false;
    clip->duration = newDuration;
    return true;
}

bool Project::setClipSpeed(const std::string& clipId, double speed) {
    Track* track = nullptr;
    Clip* clip = findClip(clipId, &track);
    if (!clip || !(speed >= 0.1 && speed <= 10.0)) return false;

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
            if (c.assetId == assetId) return false;
    const auto it = std::find_if(assets.begin(), assets.end(),
                                 [&](const MediaAsset& a) { return a.id == assetId; });
    if (it == assets.end()) return false;
    assets.erase(it);
    return true;
}

// ---------------------------------------------------------------- JSON

std::string Project::toJson(const std::string& baseDir) const {
    json j;
    j["format"] = "loggx";
    j["version"] = 1;
    j["name"] = name;
    j["video"] = {{"width", width}, {"height", height}, {"fpsNum", fpsNum}, {"fpsDen", fpsDen}};
    j["audio"] = {{"sampleRate", sampleRate}};
    j["nextId"] = nextId;

    j["assets"] = json::array();
    for (const auto& a : assets) {
        json ja = {{"id", a.id},         {"name", a.name},         {"path", a.path},
                   {"duration", a.duration}, {"width", a.width},   {"height", a.height},
                   {"fps", a.fps},       {"hasVideo", a.hasVideo}, {"hasAudio", a.hasAudio},
                   {"isStill", a.isStill}};
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
        jt["clips"] = json::array();
        for (const auto& c : t.clips) {
            json jc = {{"id", c.id},       {"asset", c.assetId}, {"start", c.start},
                       {"in", c.in},       {"duration", c.duration}, {"speed", c.speed},
                       {"gain", c.gain},   {"pitch", c.pitch},   {"opacity", c.opacity}};
            if (c.mask.shape != MaskShape::None) {
                jc["mask"] = {{"shape", shapeName(c.mask.shape)}, {"cx", c.mask.cx},
                              {"cy", c.mask.cy}, {"w", c.mask.w}, {"h", c.mask.h},
                              {"feather", c.mask.feather}, {"invert", c.mask.invert}};
            }
            jt["clips"].push_back(std::move(jc));
        }
        j["tracks"].push_back(std::move(jt));
    }
    return j.dump(2);
}

bool Project::fromJson(const std::string& text, const std::string& baseDir, Project& out,
                       std::string* error) {
    try {
        const json j = json::parse(text);
        if (j.value("format", "") != "loggx") throw std::runtime_error("Keine LoggX-Projektdatei");
        if (j.value("version", 0) > 1)
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
        if (j.contains("audio")) p.sampleRate = j["audio"].value("sampleRate", p.sampleRate);
        if (p.width < 16 || p.height < 16 || p.fpsNum < 1 || p.fpsDen < 1 || p.sampleRate < 8000)
            throw std::runtime_error("Ungültige Projekteinstellungen");
        p.nextId = j.value("nextId", std::uint64_t{1});

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
            t.kind = jt.value("kind", "video") == "audio" ? TrackKind::Audio : TrackKind::Video;
            t.muted = jt.value("muted", false);
            t.gain = jt.value("gain", 1.0);
            for (const auto& jc : jt.value("clips", json::array())) {
                Clip c;
                c.id = jc.at("id").get<std::string>();
                c.assetId = jc.at("asset").get<std::string>();
                c.start = jc.value("start", TimeUs{0});
                c.in = jc.value("in", TimeUs{0});
                c.duration = jc.value("duration", TimeUs{0});
                c.speed = jc.value("speed", 1.0);
                c.gain = jc.value("gain", 1.0);
                c.pitch = jc.value("pitch", 0.0);
                c.opacity = jc.value("opacity", 1.0);
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
                // Kaputte Clips überspringen statt das ganze Projekt abzulehnen.
                if (!p.findAsset(c.assetId) || c.duration < kMinClipDuration || c.start < 0 ||
                    c.in < 0 || !(c.speed >= 0.1 && c.speed <= 10.0))
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
