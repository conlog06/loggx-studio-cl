// LoggX Studio – Projektmodell und Timeline-Operationen.
// Alle Zeiten sind Mikrosekunden (TimeUs), damit Video (Frames) und Audio
// (Samples) dieselbe Zeitachse teilen, ohne Rundungsfehler aufzusummieren.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace loggx {

using TimeUs = std::int64_t;

inline constexpr TimeUs kSecond = 1'000'000;
inline constexpr TimeUs kMinClipDuration = 20'000;      // 20 ms
inline constexpr TimeUs kStillImageDuration = 5 * kSecond;

enum class TrackKind { Video, Audio };
enum class MaskShape { None, Rectangle, Ellipse };

struct MediaAsset {
    std::string id;
    std::string path;      // absoluter Pfad zur Laufzeit
    std::string name;
    TimeUs duration = 0;   // 0 bei Standbildern
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool isStill = false;
};

// Maske in normierten Koordinaten des Ausgabebilds (0..1).
struct Mask {
    MaskShape shape = MaskShape::None;
    double cx = 0.5, cy = 0.5;   // Mittelpunkt
    double w = 0.5, h = 0.5;     // Breite/Höhe
    double feather = 0.0;        // weiche Kante, Anteil der kürzeren Bildseite
    bool invert = false;
};

struct Clip {
    std::string id;
    std::string assetId;
    TimeUs start = 0;        // Position auf der Timeline
    TimeUs in = 0;           // Einstiegspunkt im Quellmaterial
    TimeUs duration = 0;     // Länge auf der Timeline
    double speed = 1.0;      // 2.0 = doppelt so schnell
    double gain = 1.0;       // linearer Lautstärkefaktor
    double pitch = 0.0;      // Halbtöne, unabhängig vom Tempo
    double opacity = 1.0;
    Mask mask;

    TimeUs end() const { return start + duration; }
    // Zeit im Quellmaterial für einen Timeline-Zeitpunkt innerhalb des Clips.
    TimeUs sourceTime(TimeUs timelineTime) const {
        return in + static_cast<TimeUs>(static_cast<double>(timelineTime - start) * speed);
    }
};

struct Track {
    std::string id;
    std::string name;
    TrackKind kind = TrackKind::Video;
    bool muted = false;
    double gain = 1.0;
    std::vector<Clip> clips;   // immer nach start sortiert, überlappungsfrei
};

struct Project {
    std::string name = "Unbenannt";
    int width = 1920;
    int height = 1080;
    int fpsNum = 30;
    int fpsDen = 1;
    int sampleRate = 48000;
    std::vector<MediaAsset> assets;
    std::vector<Track> tracks;   // Reihenfolge = Anzeige von oben nach unten
    std::uint64_t nextId = 1;

    // Standardprojekt: zwei Video- und zwei Audiospuren.
    static Project createDefault();

    TimeUs duration() const;
    double fps() const { return static_cast<double>(fpsNum) / fpsDen; }

    const MediaAsset* findAsset(const std::string& id) const;
    Track* findTrack(const std::string& id);
    const Track* findTrack(const std::string& id) const;
    Clip* findClip(const std::string& id, Track** track = nullptr);
    const Clip* findClip(const std::string& id, const Track** track = nullptr) const;

    std::string addAsset(MediaAsset asset);   // vergibt die id
    std::string addTrack(TrackKind kind, const std::string& name = {});

    bool rangeFree(const Track& track, TimeUs start, TimeUs duration,
                   const std::string& ignoreClipId = {}) const;
    TimeUs firstFreeStart(const Track& track, TimeUs desired, TimeUs duration) const;

    // Alle Operationen geben bei Erfolg true bzw. eine id zurück und lassen
    // das Projekt bei Misserfolg unverändert.
    std::string addClip(const std::string& trackId, const std::string& assetId, TimeUs start);
    std::string splitClip(const std::string& clipId, TimeUs at);
    bool moveClip(const std::string& clipId, const std::string& targetTrackId, TimeUs newStart);
    bool trimClipStart(const std::string& clipId, TimeUs newStart);
    bool trimClipEnd(const std::string& clipId, TimeUs newEnd);
    bool setClipSpeed(const std::string& clipId, double speed);
    bool removeClip(const std::string& clipId, bool ripple = false);
    bool removeAsset(const std::string& assetId);   // nur wenn unbenutzt

    // .loggx-Format (JSON). baseDir: Ordner der Projektdatei, damit
    // Medienpfade relativ gespeichert und wieder aufgelöst werden können.
    std::string toJson(const std::string& baseDir = {}) const;
    static bool fromJson(const std::string& text, const std::string& baseDir,
                         Project& out, std::string* error = nullptr);
    bool saveFile(const std::string& path, std::string* error = nullptr) const;
    static bool loadFile(const std::string& path, Project& out, std::string* error = nullptr);
};

}  // namespace loggx
