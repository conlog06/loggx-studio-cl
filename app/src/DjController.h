// Arbeitsbereich „DJ“: Bibliothek, zwei Decks, Mixer, Effekte, Mitschnitt
// und KI-Stems. Für QML als „dj“ verfügbar.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "loggx/engine/Dj.h"

class AppController;
class WavWriter;

class DjController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantMap deckA READ deckA NOTIFY decksChanged)
    Q_PROPERTY(QVariantMap deckB READ deckB NOTIFY decksChanged)
    Q_PROPERTY(double crossfader READ crossfader WRITE setCrossfader NOTIFY mixerChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY mixerChanged)
    Q_PROPERTY(double masterLevel READ masterLevel NOTIFY decksChanged)
    Q_PROPERTY(bool quantize READ quantize WRITE setQuantize NOTIFY mixerChanged)
    Q_PROPERTY(int fxType READ fxType NOTIFY mixerChanged)
    Q_PROPERTY(double fxBeats READ fxBeats NOTIFY mixerChanged)
    Q_PROPERTY(double fxWet READ fxWet NOTIFY mixerChanged)
    Q_PROPERTY(int fxTarget READ fxTarget NOTIFY mixerChanged)
    Q_PROPERTY(bool fxOn READ fxOn NOTIFY decksChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY decksChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool working READ working NOTIFY workChanged)
    Q_PROPERTY(double workProgress READ workProgress NOTIFY workChanged)
    Q_PROPERTY(QString workLabel READ workLabel NOTIFY workChanged)
    Q_PROPERTY(bool aiAvailable READ aiAvailable CONSTANT)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY libraryChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY libraryChanged)
    // Bibliothek wie in DJ-Programmen: Sammlung, Verlauf, Playlists
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY libraryChanged)
    Q_PROPERTY(int view READ view WRITE setView NOTIFY libraryChanged)   // -1 Sammlung, -2 Verlauf, -3 Ordner, ≥0 Playlist
    // Quellen: Cloud-Ordner (Google Drive, iCloud, OneDrive, Dropbox), Musik, eigene Ordner
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList browseItems READ browseItems NOTIFY browseChanged)
    Q_PROPERTY(QString browsePath READ browsePath NOTIFY browseChanged)
    Q_PROPERTY(QString browseRoot READ browseRoot NOTIFY browseChanged)
    Q_PROPERTY(QString browseTitle READ browseTitle NOTIFY browseChanged)
    Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY cursorChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY libraryChanged)
    // Tempo-Erkennung: Bereich (BPM), in den gefaltet wird
    Q_PROPERTY(double bpmMin READ bpmMin WRITE setBpmMin NOTIFY settingsChanged)
    Q_PROPERTY(double bpmMax READ bpmMax WRITE setBpmMax NOTIFY settingsChanged)
    // Audio: Ausgang, Kopfhörer (0 aus, 1 Kanäle 3/4 des Ausgangs, 2 eigenes Gerät)
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY settingsChanged)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY settingsChanged)
    Q_PROPERTY(int cueMode READ cueMode WRITE setCueMode NOTIFY settingsChanged)
    Q_PROPERTY(QString headphoneDevice READ headphoneDevice WRITE setHeadphoneDevice NOTIFY settingsChanged)
    Q_PROPERTY(int bufferMs READ bufferMs WRITE setBufferMs NOTIFY settingsChanged)
    Q_PROPERTY(QString audioInfo READ audioInfo NOTIFY settingsChanged)
    Q_PROPERTY(double headMix READ headMix WRITE setHeadMix NOTIFY mixerChanged)
    Q_PROPERTY(double headVolume READ headVolume WRITE setHeadVolume NOTIFY mixerChanged)
    Q_PROPERTY(double defaultTempoRange READ defaultTempoRange WRITE setDefaultTempoRange NOTIFY settingsChanged)
    Q_PROPERTY(bool autoKeyLock READ autoKeyLock WRITE setAutoKeyLock NOTIFY settingsChanged)
    Q_PROPERTY(bool autoGain READ autoGain WRITE setAutoGain NOTIFY settingsChanged)        // Lautheit angleichen
    Q_PROPERTY(bool autoStems READ autoStems WRITE setAutoStems NOTIFY settingsChanged)     // Stems beim Laden
    // Stem-Engine: 0 = eingebaut (sofort, ohne Download), 1 = KI (beste Qualität)
    Q_PROPERTY(int stemEngine READ stemEngine WRITE setStemEngine NOTIFY settingsChanged)
    Q_PROPERTY(bool lockPlaying READ lockPlaying WRITE setLockPlaying NOTIFY settingsChanged)
    Q_PROPERTY(int waveStyle READ waveStyle WRITE setWaveStyle NOTIFY settingsChanged)       // 0 3-Band, 1 Blau, 2 Weiß
    // Bibliothek anpassen: Höhe (Trenner ziehen), groß (Decks kompakt), Spalten, Zeilenhöhe
    Q_PROPERTY(double libraryHeight READ libraryHeight WRITE setLibraryHeight NOTIFY layoutChanged)
    Q_PROPERTY(bool libraryMax READ libraryMax WRITE setLibraryMax NOTIFY layoutChanged)
    Q_PROPERTY(QStringList columns READ columns WRITE setColumns NOTIFY layoutChanged)
    Q_PROPERTY(int rowSize READ rowSize WRITE setRowSize NOTIFY layoutChanged)
    // Sampler: 8 Plätze (eingebaute Klänge oder eigene Dateien)
    Q_PROPERTY(QVariantList sampler READ sampler NOTIFY samplerChanged)
    Q_PROPERTY(double samplerVolume READ samplerVolume WRITE setSamplerVolume NOTIFY samplerChanged)
    // Sichtbare Sekunden der mitlaufenden Wellenformen (Zoom)
    Q_PROPERTY(double waveZoom READ waveZoom WRITE setWaveZoom NOTIFY waveZoomChanged)

public:
    explicit DjController(AppController* app, QObject* parent = nullptr);
    ~DjController() override;

    // ---- Bibliothek
    QVariantList library() const;
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void importPaths(const QStringList& paths);
    Q_INVOKABLE void removeTrack(const QString& path);
    Q_INVOKABLE void prepareStems(const QString& path);   // KI: Gesang, Schlagzeug, Bass, Rest
    Q_INVOKABLE void reanalyze(const QString& path);
    Q_INVOKABLE void loadUrl(int deck, const QUrl& url);   // Datei aus Explorer/Finder direkt ins Deck
    Q_INVOKABLE void setTrackBpm(const QString& path, double bpm);
    Q_INVOKABLE void setRating(const QString& path, int stars);
    Q_INVOKABLE QVariantMap trackInfo(const QString& path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    // Playlists
    QVariantList playlists() const;
    int view() const { return m_view; }
    void setView(int view);
    Q_INVOKABLE int createPlaylist(const QString& name);
    Q_INVOKABLE void renamePlaylist(int index, const QString& name);
    Q_INVOKABLE void deletePlaylist(int index);
    Q_INVOKABLE void addToPlaylist(int index, const QStringList& paths);
    Q_INVOKABLE void removeFromPlaylist(int index, const QString& path);
    Q_INVOKABLE void clearHistory();
    QVariantList sources() const;
    Q_INVOKABLE void addSource(const QUrl& folder);
    Q_INVOKABLE void removeSource(const QString& path);
    Q_INVOKABLE void browse(const QString& path);
    Q_INVOKABLE void browseUp();
    QVariantList browseItems() const { return m_browseItems; }
    QString browsePath() const { return m_browsePath; }
    QString browseRoot() const { return m_browseRoot; }
    QString browseTitle() const;
    // Cursor (Browse-Regler am Controller, Pfeiltasten)
    int cursor() const { return m_cursor; }
    void setCursor(int row);
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void loadCursor(int deck);
    int trackCount() const;
    // Fremde Bibliotheken: rekordbox (XML-Export) und Apple Music/iTunes (Library.xml)
    Q_INVOKABLE int importLibraryXml(const QUrl& url);
    // Suche bei Streaming-Diensten/Shops: beatport, soundcloud, spotify, applemusic, tidal, youtube
    Q_INVOKABLE QString searchUrl(const QString& service, const QString& path) const;
    // Im Musik-Editor öffnen (QML fragt vorher nach ungespeicherten Änderungen)
    Q_INVOKABLE void openInMusicEditor(const QString& path);
    QString search() const { return m_search; }
    void setSearch(const QString& text);
    int sortColumn() const { return m_sort; }
    void setSortColumn(int column);   // 0 Titel, 1 Interpret, 2 BPM, 3 Tonart, 4 Dauer; negativ = absteigend

    // ---- Decks
    QVariantMap deckA() const { return deckInfo(0); }
    QVariantMap deckB() const { return deckInfo(1); }
    Q_INVOKABLE void loadTrack(int deck, const QString& path);
    Q_INVOKABLE void togglePlay(int deck);
    Q_INVOKABLE void cue(int deck);
    Q_INVOKABLE void sync(int deck);
    Q_INVOKABLE void setSyncLock(int deck, bool on);
    Q_INVOKABLE void setTempo(int deck, double percent);
    Q_INVOKABLE void setTempoRange(int deck, double range);   // 8, 16, 50
    Q_INVOKABLE void setKeyLock(int deck, bool on);
    Q_INVOKABLE void seek(int deck, double seconds);
    Q_INVOKABLE void seekRelative(int deck, double fraction);   // 0..1 der Spur
    Q_INVOKABLE void hotCue(int deck, int index);
    Q_INVOKABLE void clearHotCue(int deck, int index);
    Q_INVOKABLE void setLoop(int deck, double beats);
    Q_INVOKABLE void exitLoop(int deck);
    Q_INVOKABLE void beatJump(int deck, double beats);
    Q_INVOKABLE void setStem(int deck, int stem, bool on);
    Q_INVOKABLE void setEq(int deck, int band, double value);
    Q_INVOKABLE void setFilter(int deck, double value);
    Q_INVOKABLE void setTrim(int deck, double value);
    Q_INVOKABLE void setFader(int deck, double value);
    Q_INVOKABLE void nudge(int deck, double seconds);           // Jog-Rad
    Q_INVOKABLE void eject(int deck);
    Q_INVOKABLE void setPfl(int deck, bool on);
    // Beatgrid des Decks: BPM ×2/÷2, Raster hier beginnen, verschieben, Tap
    Q_INVOKABLE void multiplyBpm(int deck, double factor);
    Q_INVOKABLE void setDeckBpm(int deck, double bpm);
    Q_INVOKABLE void setGridHere(int deck);
    Q_INVOKABLE void shiftGrid(int deck, double milliseconds);
    Q_INVOKABLE double tap(int deck);                           // Tap-Tempo, liefert BPM (0 = noch zu wenig)
    // Jog-Rad (für Controller): Scratchen bzw. Bend
    Q_INVOKABLE void setScratch(int deck, bool on);
    Q_INVOKABLE void jogScratch(int deck, double seconds);
    Q_INVOKABLE void jogBend(int deck, double amount);
    Q_INVOKABLE void setLoopInOut(int deck, bool in);          // Loop In / Loop Out
    Q_INVOKABLE void reloop(int deck);
    // Tonart: Key Shift (Halbtöne) und Key Sync (an das andere Deck anpassen)
    Q_INVOKABLE void setKeyShift(int deck, int semitones);
    Q_INVOKABLE void keySync(int deck);
    // Sampler
    QVariantList sampler() const;
    Q_INVOKABLE void triggerSample(int slot);
    Q_INVOKABLE void stopSample(int slot);
    Q_INVOKABLE bool loadSample(int slot, const QUrl& file);
    Q_INVOKABLE void resetSample(int slot);
    double samplerVolume() const { return m_mixer.samplerVolume(); }
    void setSamplerVolume(double v);
    double waveZoom() const { return m_waveZoom; }
    void setWaveZoom(double seconds);
    Q_INVOKABLE void zoomWave(int steps);   // + = näher heran

    // ---- Mixer, Effekt, Mitschnitt
    double crossfader() const { return m_mixer.crossfader(); }
    void setCrossfader(double value);
    double masterVolume() const { return m_masterVolume; }
    void setMasterVolume(double value);
    double masterLevel() const { return m_mixer.masterLevel(); }
    bool quantize() const { return m_mixer.quantize(); }
    void setQuantize(bool on);
    int fxType() const { return m_fxType; }
    double fxBeats() const { return m_fxBeats; }
    double fxWet() const { return m_fxWet; }
    int fxTarget() const { return m_fxTarget; }
    bool fxOn() const { return m_mixer.fxOn(); }
    Q_INVOKABLE void setFx(int type, double beats, double wet, int target);
    Q_INVOKABLE void setFxOn(bool on);
    bool recording() const { return m_writer != nullptr; }
    double recordSeconds() const;
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE QString stopRecording();   // Pfad der WAV-Datei

    QString status() const { return m_status; }
    bool working() const { return m_working; }
    double workProgress() const { return m_workProgress; }
    QString workLabel() const { return m_workLabel; }
    bool aiAvailable() const;
    Q_INVOKABLE void cancelWork();

    // ---- Einstellungen
    double bpmMin() const { return m_bpmMin; }
    void setBpmMin(double v);
    double bpmMax() const { return m_bpmMax; }
    void setBpmMax(double v);
    QStringList outputDevices() const;
    QString outputDevice() const { return m_outputDevice; }
    void setOutputDevice(const QString& name);
    int cueMode() const { return m_cueMode; }
    void setCueMode(int mode);
    QString headphoneDevice() const { return m_headDevice; }
    void setHeadphoneDevice(const QString& name);
    int bufferMs() const { return m_bufferMs; }
    void setBufferMs(int ms);
    QString audioInfo() const { return m_audioInfo; }
    double headMix() const { return m_headMix; }
    void setHeadMix(double v);
    double headVolume() const { return m_headVolume; }
    void setHeadVolume(double v);
    double defaultTempoRange() const { return m_defaultRange; }
    void setDefaultTempoRange(double v);
    bool autoKeyLock() const { return m_autoKeyLock; }
    void setAutoKeyLock(bool on);
    Q_INVOKABLE void restartAudio();
    double libraryHeight() const { return m_libraryHeight; }
    void setLibraryHeight(double h);
    bool libraryMax() const { return m_libraryMax; }
    void setLibraryMax(bool on);
    QStringList columns() const { return m_columns; }
    void setColumns(const QStringList& columns);
    Q_INVOKABLE void toggleColumn(const QString& id);
    int rowSize() const { return m_rowSize; }
    void setRowSize(int size);
    bool autoGain() const { return m_autoGain; }
    void setAutoGain(bool on);
    bool autoStems() const { return m_autoStems; }
    int stemEngine() const { return m_stemEngine; }
    void setStemEngine(int engine);
    Q_INVOKABLE void recomputeStems(const QString& path);
    void setAutoStems(bool on);
    bool lockPlaying() const { return m_lockPlaying; }
    void setLockPlaying(bool on);
    int waveStyle() const { return m_waveStyle; }
    void setWaveStyle(int style);

    // Für die Wellenform-Anzeige
    std::shared_ptr<const loggx::DjTrack> track(int deck) const { return m_mixer.track(deck); }
    double position(int deck) const { return m_mixer.position(deck); }
    double cuePoint(int deck) const { return m_mixer.cuePoint(deck); }
    double hotCuePoint(int deck, int i) const { return m_mixer.hotCue(deck, i); }
    double loopBeats(int deck) const { return m_mixer.loopBeats(deck); }
    loggx::DjMixer& mixer() { return m_mixer; }

    // Audio aktiv halten (Arbeitsbereich sichtbar)
    Q_INVOKABLE void setActive(bool active);

signals:
    void libraryChanged();
    void decksChanged();
    void mixerChanged();
    void recordingChanged();
    void statusChanged();
    void workChanged();
    void tick();   // ~60 Hz für Wellenformen
    void mixRecorded(const QString& path);
    void cursorChanged();
    void settingsChanged();
    void openInEditor(const QString& path, double bpm);
    void deckLoaded(int deck);
    void samplerChanged();
    void layoutChanged();
    void sourcesChanged();
    void browseChanged();
    void waveZoomChanged();

private:
    struct Entry {
        QString path, title, artist, album, genre, comment, source;
        double bpm = 0, firstBeat = 0, duration = 0;
        int key = -1;
        bool minor = false;
        bool analyzed = false;
        int version = 0;          // Stand der Analyse (neue Erkennung → neu analysieren)
        bool gridEdited = false;  // von Hand oder aus rekordbox: nicht überschreiben
        int rating = 0, plays = 0;
        double loudness = 0;      // RMS in dBFS (0 = unbekannt)
        qint64 added = 0;         // ms seit 1970
        std::array<double, 8> hot{-1, -1, -1, -1, -1, -1, -1, -1};
    };
    struct Playlist {
        QString name;
        QStringList paths;
    };
    QVariantMap deckInfo(int deck) const;
    std::vector<const Entry*> rows() const;
    std::vector<const Entry*> computeRows() const;
    mutable std::vector<std::size_t> m_rowsCache;
    mutable std::size_t m_rowsEntryCount = 0;
    mutable bool m_rowsValid = false, m_libValid = false;
    mutable QVariantList m_libCache;
    Entry* entry(const QString& path);
    void syncHotCues(int deck);
    void applyGrid(const QString& path);
    int importRekordbox(const QString& file, QString* error);
    int importItunes(const QString& file, QString* error);
    Entry& addEntry(const QString& path);
    void setStatus(const QString& text);
    void saveLibrary() const;
    void loadLibrary();
    void analyzeNext();
    QString stemFolder(const QString& path) const;
    bool hasStems(const QString& path) const;
    void ensureAudio();

    AppController* m_app;
    loggx::DjMixer m_mixer;
    std::vector<Entry> m_entries;
    QString m_search;
    int m_sort = 0;
    QStringList m_analyzeQueue;
    bool m_analyzing = false;
    std::array<QString, 2> m_loaded;      // Pfad je Deck
    std::array<bool, 2> m_loading{false, false};
    std::array<double, 2> m_range{8.0, 8.0};
    std::vector<Playlist> m_playlists;
    QStringList m_history;
    int m_view = -1;
    QString m_browsePath, m_browseRoot;
    QVariantList m_browseItems;
    int m_cursor = 0;
    std::array<std::vector<qint64>, 2> m_taps;
    std::array<double, 2> m_loopIn{-1.0, -1.0};
    struct PhraseCache {
        const void* track = nullptr;
        double bpm = -1, first = -1;
        QVariantList list;
    };
    mutable std::array<PhraseCache, 2> m_phraseCache;
    QElapsedTimer m_clock;
    double m_bpmMin = 70.0, m_bpmMax = 180.0;
    QString m_outputDevice, m_headDevice, m_audioInfo;
    int m_cueMode = 1;
    int m_bufferMs = 30;
    double m_headMix = 0.0, m_headVolume = 1.0, m_defaultRange = 8.0;
    bool m_autoKeyLock = false;
    bool m_autoGain = true, m_autoStems = false, m_lockPlaying = true;
    int m_waveStyle = 0;
    int m_stemEngine = 0;
    double m_libraryHeight = 230;
    bool m_libraryMax = false;
    QStringList m_columns;
    int m_rowSize = 1;
    double m_waveZoom = 8.0;
    std::array<QString, 8> m_samplePaths;     // leer = eingebauter Klang
    std::array<bool, 8> m_samplePlaying{};
    void initSampler();
    double m_masterVolume = 1.0;
    int m_fxType = 0;
    double m_fxBeats = 0.75, m_fxWet = 0.5;
    int m_fxTarget = 2;
    QString m_status;
    bool m_working = false;
    double m_workProgress = 0;
    QString m_workLabel;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QTimer m_timer;
    int m_frame = 0;

    // Audio
    class Device;
    class CueDevice;
    struct CueRing;
    Device* m_device = nullptr;
    QAudioSink* m_sink = nullptr;
    CueDevice* m_cueDevice = nullptr;
    QAudioSink* m_cueSink = nullptr;
    std::shared_ptr<CueRing> m_ring;
    bool m_active = false;

    // Mitschnitt
    std::mutex m_recMutex;
    std::vector<float> m_recBuffer;
    WavWriter* m_writer = nullptr;
    QString m_recPath;
};
