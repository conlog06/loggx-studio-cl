// LoggX Studio – Projektmodell und Timeline-Operationen.
// Alle Zeiten sind Mikrosekunden (TimeUs), damit Video (Frames) und Audio
// (Samples) dieselbe Zeitachse teilen, ohne Rundungsfehler aufzusummieren.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace loggx {

using TimeUs = std::int64_t;

inline constexpr TimeUs kSecond = 1'000'000;
inline constexpr TimeUs kMinClipDuration = 20'000;      // 20 ms
inline constexpr TimeUs kStillImageDuration = 5 * kSecond;
inline constexpr TimeUs kTitleDuration = 4 * kSecond;

// Instrument-Spuren spielen MIDI-Clips mit einem eingebauten Klangerzeuger.
enum class TrackKind { Video, Audio, Instrument };
enum class ClipKind { Media, Title, Midi };
enum class MaskShape { None, Rectangle, Ellipse };
enum class InstrumentType { Piano, ElectricPiano, Synth, Bass, Pad, Strings, Drums };
// Videoprojekt (alle Arbeitsbereiche) oder reines Musikprojekt.
enum class ProjectMode { Video, Music };
// Übergang am Anfang eines Clips zum direkt davor liegenden Clip derselben Spur.
// Neue Arten nur hinten anhängen (Werte stehen in Projektdateien).
enum class TransitionType { None, Crossfade, DipBlack, DipWhite, Wipe, Slide,
                            WipeUp, SlideUp, Circle, Zoom, BlurDissolve, Pixelate };
inline constexpr int kTransitionCount = 12;
const char* transitionName(TransitionType type);

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
    std::string videoCodec;   // z. B. "h264", "av1" (nur zur Laufzeit, für Meldungen)
    // Ergebnis der Musik-Analyse (0 / -1 = noch nicht analysiert)
    double bpm = 0.0;
    TimeUs firstBeat = 0;
    int key = -1;          // 0 = C … 11 = H
    bool keyMinor = false;
};

// Maske in normierten Koordinaten des Ausgabebilds (0..1).
struct Mask {
    MaskShape shape = MaskShape::None;
    double cx = 0.5, cy = 0.5;   // Mittelpunkt
    double w = 0.5, h = 0.5;     // Breite/Höhe
    double feather = 0.0;        // weiche Kante, Anteil der kürzeren Bildseite
    bool invert = false;
};

// Farbkorrektur. Werte so gewählt, dass alles auf Standard "nichts tut".
struct ColorGrade {
    double exposure = 0.0;      // Blendenstufen, -3..3
    double contrast = 1.0;      // 0..2
    double saturation = 1.0;    // 0 = schwarzweiß, 2 = doppelt
    double temperature = 0.0;   // -1 kalt .. 1 warm
    double tint = 0.0;          // -1 grün .. 1 magenta
    double lift = 0.0;          // Schatten, -0.5..0.5
    double gamma = 1.0;         // Mitten, 0.2..3
    double gain = 1.0;          // Lichter, 0..2
    std::string lutPath;        // .cube-Datei (3D-LUT)
    double lutMix = 1.0;

    bool isNeutral() const;
};

struct Effects {
    double blur = 0.0;          // Radius als Anteil der Bildhöhe, 0..0.05
    bool chromaKey = false;
    double keyR = 0.0, keyG = 1.0, keyB = 0.0;   // Schlüsselfarbe 0..1
    double keyTolerance = 0.3;
    double keySoftness = 0.1;
    double vignette = 0.0;      // 0..1
    double sharpen = 0.0;       // Schärfen 0..1
    double grain = 0.0;         // Filmkorn 0..1
    double pixelate = 0.0;      // Verpixeln 0..1
    double glow = 0.0;          // Leuchten 0..1
    double rgbSplit = 0.0;      // Farbversatz (chromatische Aberration) 0..1
    double sepia = 0.0;         // 0..1
    bool invert = false;        // Negativ
    bool mirrorH = false;       // horizontal spiegeln
    bool mirrorV = false;       // vertikal spiegeln
    bool aiCutout = false;      // KI-Freistellung (Hintergrund entfernen)
    double cutoutEdge = 0.4;    // Kante 0 weich … 1 hart

    bool isNeutral() const {
        return blur <= 0.0 && !chromaKey && vignette <= 0.0 && sharpen <= 0.0 && grain <= 0.0 && pixelate <= 0.0 &&
               glow <= 0.0 && rgbSplit <= 0.0 && sepia <= 0.0 && !invert && !mirrorH && !mirrorV && !aiCutout;
    }
};

struct Title {
    std::string text = "Titel";
    std::string font;           // leer = Standardschrift
    double size = 0.09;         // Schriftgröße als Anteil der Bildhöhe
    std::uint32_t color = 0xFFFFFFFF;   // 0xAARRGGBB
    bool bold = true;
    int align = 1;              // 0 links, 1 Mitte, 2 rechts
    double posY = 0.5;          // vertikale Mitte des Texts, 0..1
    double box = 0.0;           // Deckkraft eines Hintergrundbalkens 0..1
    bool shadow = true;
};

struct Note {
    int pitch = 60;             // MIDI-Notennummer, 60 = C4
    TimeUs start = 0;           // relativ zum Anfang des Clip-Inhalts
    TimeUs length = 250'000;
    double velocity = 0.8;      // 0..1
};

struct Keyframe {
    TimeUs t = 0;               // relativ zum Clip-Anfang auf der Timeline
    double value = 0.0;
};

// Parameter, die sich mit Keyframes animieren lassen.
inline const char* const kKeyframeParams[] = {"opacity", "gainDb", "x", "y", "scale", "rotation", "blur", "maskX", "maskY"};
bool isKeyframeParam(const std::string& name);

struct Clip {
    std::string id;
    ClipKind kind = ClipKind::Media;
    std::string assetId;     // nur bei Media
    TimeUs start = 0;        // Position auf der Timeline
    TimeUs in = 0;           // Einstiegspunkt im Quellmaterial / MIDI-Inhalt
    TimeUs duration = 0;     // Länge auf der Timeline
    double speed = 1.0;      // 2.0 = doppelt so schnell (nur Media)
    double gain = 1.0;       // linearer Lautstärkefaktor
    double pitch = 0.0;      // Halbtöne, unabhängig vom Tempo
    double opacity = 1.0;
    TimeUs fadeIn = 0;       // Einblenden (Bild und Ton)
    TimeUs fadeOut = 0;      // Ausblenden
    double x = 0.0, y = 0.0; // Verschiebung als Anteil von Breite/Höhe
    double scale = 1.0;
    double rotation = 0.0;   // Grad, im Uhrzeigersinn
    Mask mask;
    ColorGrade color;
    Effects fx;
    Title title;             // nur bei Title
    std::vector<Note> notes; // nur bei Midi
    std::map<std::string, std::vector<Keyframe>> keys;   // sortiert nach t
    TransitionType transition = TransitionType::None;     // zum vorherigen Clip
    TimeUs transitionDuration = 500'000;                  // Gesamtlänge, mittig am Schnitt

    TimeUs end() const { return start + duration; }
    // Zeit im Quellmaterial für einen Timeline-Zeitpunkt innerhalb des Clips.
    TimeUs sourceTime(TimeUs timelineTime) const {
        return in + static_cast<TimeUs>(static_cast<double>(timelineTime - start) * speed);
    }
    // Wert eines Parameters zum Timeline-Zeitpunkt t (Keyframes linear
    // interpoliert, sonst der Grundwert).
    double valueAt(const std::string& param, TimeUs t) const;
    double baseValue(const std::string& param) const;
    // Faktor 0..1 für Ein-/Ausblenden zum Timeline-Zeitpunkt t.
    double fadeAt(TimeUs t) const;
    bool hasTransform() const;
    bool hasKeys() const;
};

struct TrackEffects {
    double eqLow = 0.0, eqMid = 0.0, eqHigh = 0.0;   // dB, -12..12
    double compressor = 0.0;    // Stärke 0..1
    double reverb = 0.0;        // Anteil 0..1
    double delay = 0.0;         // Anteil 0..1 (Echo im Takt: punktierte Achtel)

    bool isNeutral() const {
        return eqLow == 0.0 && eqMid == 0.0 && eqHigh == 0.0 && compressor <= 0.0 &&
               reverb <= 0.0 && delay <= 0.0;
    }
};

struct Track {
    std::string id;
    std::string name;
    TrackKind kind = TrackKind::Video;
    bool muted = false;
    bool solo = false;
    double gain = 1.0;
    double pan = 0.0;        // -1 links .. 1 rechts
    InstrumentType instrument = InstrumentType::Piano;
    TrackEffects fx;
    std::vector<Clip> clips;   // immer nach start sortiert, überlappungsfrei

    bool hasSound() const { return kind != TrackKind::Video; }
};

struct Project {
    std::string name = "Unbenannt";
    int width = 1920;
    int height = 1080;
    int fpsNum = 30;
    int fpsDen = 1;
    int sampleRate = 48000;
    double bpm = 120.0;
    int beatsPerBar = 4;
    double masterGain = 1.0;
    ProjectMode mode = ProjectMode::Video;
    TimeUs beatOffset = 0;       // Lage des ersten Schlags (Beat-Raster)
    std::vector<MediaAsset> assets;
    std::vector<Track> tracks;   // Reihenfolge = Anzeige von oben nach unten
    std::uint64_t nextId = 1;

    // Standardprojekt: zwei Video-, zwei Audiospuren und eine Instrumentspur.
    static Project createDefault();
    // Musikprojekt: Audiospur und Instrumentspuren für Melodie, Bass, Schlagzeug.
    static Project createMusic();

    TimeUs duration() const;
    double fps() const { return static_cast<double>(fpsNum) / fpsDen; }
    TimeUs beatDuration() const { return static_cast<TimeUs>(60.0 / bpm * kSecond); }
    // Zeitpunkt des n-ten Schlags (Beat-Raster, mit Versatz).
    TimeUs beatTime(std::int64_t n) const {
        return beatOffset + static_cast<TimeUs>(static_cast<double>(n) * 60.0 / bpm * kSecond);
    }
    // Nächster Schlag zu t.
    TimeUs nearestBeat(TimeUs t) const;

    const MediaAsset* findAsset(const std::string& id) const;
    Track* findTrack(const std::string& id);
    const Track* findTrack(const std::string& id) const;
    Clip* findClip(const std::string& id, Track** track = nullptr);
    const Clip* findClip(const std::string& id, const Track** track = nullptr) const;

    std::string addAsset(MediaAsset asset);   // vergibt die id
    std::string addTrack(TrackKind kind, const std::string& name = {});
    bool removeTrack(const std::string& trackId);   // nur leere Spuren

    bool rangeFree(const Track& track, TimeUs start, TimeUs duration,
                   const std::string& ignoreClipId = {}) const;
    TimeUs firstFreeStart(const Track& track, TimeUs desired, TimeUs duration) const;

    // Alle Operationen geben bei Erfolg true bzw. eine id zurück und lassen
    // das Projekt bei Misserfolg unverändert.
    std::string addClip(const std::string& trackId, const std::string& assetId, TimeUs start);
    std::string addTitleClip(const std::string& trackId, TimeUs start,
                             const std::string& text = "Titel");
    std::string addMidiClip(const std::string& trackId, TimeUs start, TimeUs duration = 0);
    // Kopie eines Clips (Kopieren/Einfügen, Duplizieren) an der ersten freien
    // Stelle ab start; die Spur muss zur Art des Clips passen.
    std::string pasteClip(const Clip& source, const std::string& trackId, TimeUs start);
    std::string splitClip(const std::string& clipId, TimeUs at);
    bool moveClip(const std::string& clipId, const std::string& targetTrackId, TimeUs newStart);
    bool trimClipStart(const std::string& clipId, TimeUs newStart);
    bool trimClipEnd(const std::string& clipId, TimeUs newEnd);
    bool setClipSpeed(const std::string& clipId, double speed);
    bool removeClip(const std::string& clipId, bool ripple = false);
    bool removeAsset(const std::string& assetId);   // nur wenn unbenutzt
    // Ganzen Song ins neue Tempo bringen: MIDI wird umgerechnet, Audio per
    // Time-Stretch angepasst, Clips rücken mit. Videospuren bleiben, wie sie
    // sind. false, wenn ein Audio-Clip dafür zu schnell/langsam würde.
    bool retime(double newBpm);
    // Alle MIDI- und Audio-Clips (nicht Video) um n Halbtöne transponieren.
    int transposeAll(int semitones);
    // Teilt einen Clip an jedem n-ten Schlag (Schnitt auf den Beat).
    int cutOnBeats(const std::string& clipId, int everyBeats);
    // Direkt angrenzender Clip vor bzw. nach c auf derselben Spur (±1 ms).
    const Clip* previousAdjacent(const Track& track, const Clip& c) const;
    const Clip* nextAdjacent(const Track& track, const Clip& c) const;

    // Keyframes. t ist ein Timeline-Zeitpunkt; gespeichert wird relativ zum Clip.
    bool setKeyframe(const std::string& clipId, const std::string& param, TimeUs t, double value);
    bool removeKeyframe(const std::string& clipId, const std::string& param, TimeUs t);
    bool clearKeyframes(const std::string& clipId, const std::string& param);

    // .loggx-Format (JSON). baseDir: Ordner der Projektdatei, damit
    // Medienpfade relativ gespeichert und wieder aufgelöst werden können.
    std::string toJson(const std::string& baseDir = {}) const;
    static bool fromJson(const std::string& text, const std::string& baseDir,
                         Project& out, std::string* error = nullptr);
    bool saveFile(const std::string& path, std::string* error = nullptr) const;
    static bool loadFile(const std::string& path, Project& out, std::string* error = nullptr);
};

// Für die Anzeige: deutscher Name eines Instruments.
const char* instrumentName(InstrumentType type);
inline constexpr int kInstrumentCount = 7;

}  // namespace loggx
