// Zentrale Schnittstelle zwischen Oberfläche (QML) und Engine: hält das
// Projekt, Undo/Redo, Auswahl, Wiedergabe, Mixer, Autosave und Export.
#pragma once

#include <cmath>
#include <memory>
#include <optional>

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "MediaCache.h"
#include "MidiInput.h"
#include "Workers.h"
#include "loggx/core/License.h"
#include "loggx/core/Project.h"
#include "loggx/core/UndoStack.h"
#include "loggx/engine/Engine.h"

class QAudioSink;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY fileChanged)
    Q_PROPERTY(QString formatInfo READ formatInfo NOTIFY projectChanged)
    Q_PROPERTY(int projectWidth READ projectWidth NOTIFY projectChanged)
    Q_PROPERTY(int projectHeight READ projectHeight NOTIFY projectChanged)
    Q_PROPERTY(int projectFps READ projectFps NOTIFY projectChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
    Q_PROPERTY(QString selectedClipId READ selectedClipId WRITE setSelectedClipId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap clip READ clip NOTIFY clipChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool stretchAvailable READ stretchAvailable CONSTANT)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY projectChanged)
    Q_PROPERTY(double masterGainDb READ masterGainDb WRITE setMasterGainDb NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(QStringList instrumentNames READ instrumentNames CONSTANT)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryInfo READ recoveryInfo NOTIFY recoveryChanged)
    // Projektmodus und Startbildschirm
    Q_PROPERTY(bool musicMode READ musicMode NOTIFY projectChanged)
    Q_PROPERTY(bool showStart READ showStart WRITE setShowStart NOTIFY startChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentChanged)
    // Beat-Raster
    Q_PROPERTY(bool beatGrid READ beatGrid WRITE setBeatGrid NOTIFY gridChanged)
    Q_PROPERTY(QStringList transitionNames READ transitionNames CONSTANT)
    // Live spielen und Aufnahme
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(int keyboardOctave READ keyboardOctave WRITE setKeyboardOctave NOTIFY liveChanged)
    Q_PROPERTY(QStringList midiInputs READ midiInputs NOTIFY liveChanged)
    Q_PROPERTY(int midiInput READ midiInput NOTIFY liveChanged)
    Q_PROPERTY(bool midiAvailable READ midiAvailable CONSTANT)
    Q_PROPERTY(bool quantizeRecording READ quantizeRecording WRITE setQuantizeRecording NOTIFY liveChanged)
    Q_PROPERTY(QVariantList heldNotes READ heldNotes NOTIFY liveChanged)
    // Lizenz und Admin
    Q_PROPERTY(QString edition READ edition NOTIFY licenseChanged)
    Q_PROPERTY(QString editionName READ editionName NOTIFY licenseChanged)
    Q_PROPERTY(QString licenseHolder READ licenseHolder NOTIFY licenseChanged)
    Q_PROPERTY(bool licensingActive READ licensingActive CONSTANT)
    Q_PROPERTY(bool isPro READ isPro NOTIFY licenseChanged)
    Q_PROPERTY(bool isAdmin READ isAdmin NOTIFY licenseChanged)
    Q_PROPERTY(bool simulateFree READ simulateFree WRITE setSimulateFree NOTIFY licenseChanged)
    // Vorschau: 0 Auto (Fenstergröße, beim Abspielen angepasst), 1 Voll (Projekt),
    // 2 Halb, 3 Viertel
    Q_PROPERTY(int previewQuality READ previewQuality WRITE setPreviewQuality NOTIFY adminChanged)
    Q_PROPERTY(QString previewSize READ previewSize NOTIFY statsChanged)
    Q_PROPERTY(int projectFpsNum READ projectFpsNum NOTIFY projectChanged)
    Q_PROPERTY(int projectFpsDen READ projectFpsDen NOTIFY projectChanged)
    Q_PROPERTY(int beatsPerBar READ beatsPerBar WRITE setBeatsPerBar NOTIFY projectChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    // Mikrofon-Aufnahme
    Q_PROPERTY(QStringList audioInputs READ audioInputs NOTIFY micChanged)
    Q_PROPERTY(int audioInput READ audioInput WRITE setAudioInput NOTIFY micChanged)
    Q_PROPERTY(bool micRecording READ micRecording NOTIFY micChanged)
    Q_PROPERTY(double micLevel READ micLevel NOTIFY micLevelChanged)
    // Hintergrundarbeit (Analyse, Stimmentrennung)
    Q_PROPERTY(bool working READ working NOTIFY workChanged)
    Q_PROPERTY(double workProgress READ workProgress NOTIFY workChanged)
    Q_PROPERTY(QString workLabel READ workLabel NOTIFY workChanged)
    Q_PROPERTY(bool aiAvailable READ aiAvailable CONSTANT)
    Q_PROPERTY(int separationMethod READ separationMethod WRITE setSeparationMethod NOTIFY workChanged)
    // KI-Modelle: Liste mit Status, automatischer Download im Hintergrund
    Q_PROPERTY(QVariantList aiModels READ aiModels NOTIFY modelsChanged)
    Q_PROPERTY(bool aiAutoDownload READ aiAutoDownload WRITE setAiAutoDownload NOTIFY modelsChanged)
    Q_PROPERTY(QString aiModelFolder READ aiModelFolder CONSTANT)
    // Allgemein: Start, Sicherung
    Q_PROPERTY(int startMode READ startMode WRITE setStartMode NOTIFY generalChanged)       // 0 Start, 1 letztes Projekt, 2 DJ
    Q_PROPERTY(int autosaveMinutes READ autosaveMinutes WRITE setAutosaveMinutes NOTIFY generalChanged)   // 0 = aus
    Q_PROPERTY(double previewMs READ previewMs NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap systemInfo READ systemInfo CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // ---- Zugriff für die C++-Elemente (Timeline, Piano-Roll)
    const loggx::Project& project() const { return m_project; }
    loggx::TimeUs positionUs() const { return m_position; }
    MediaCache& mediaCache() { return m_cache; }
    bool moveClipLive(const QString& clipId, const QString& trackId, loggx::TimeUs start);
    bool trimStartLive(const QString& clipId, loggx::TimeUs start);
    bool trimEndLive(const QString& clipId, loggx::TimeUs end);
    void seekUs(loggx::TimeUs t);
    int addNote(const QString& clipId, const loggx::Note& note);   // Index oder -1
    bool updateNote(const QString& clipId, int index, const loggx::Note& note);
    bool removeNote(const QString& clipId, int index);

    // ---- Eigenschaften
    QString projectName() const { return QString::fromStdString(m_project.name); }
    void setProjectName(const QString& name);
    QString filePath() const { return m_filePath; }
    bool dirty() const { return m_dirty; }
    QString formatInfo() const;
    int projectWidth() const { return m_project.width; }
    int projectHeight() const { return m_project.height; }
    int projectFps() const { return static_cast<int>(std::lround(m_project.fps())); }
    double position() const { return m_position / 1e6; }
    double duration() const { return m_project.duration() / 1e6; }
    bool playing() const { return m_playing; }
    QVariantList assets() const;
    QVariantList tracks() const;
    QString selectedClipId() const { return m_selected; }
    void setSelectedClipId(const QString& id);
    QVariantMap clip() const;
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    bool exporting() const { return m_exportWorker != nullptr; }
    double exportProgress() const { return m_exportProgress; }
    QString status() const { return m_status; }
    bool stretchAvailable() const { return loggx::AudioMixer::hasStretcher(); }
    double bpm() const { return m_project.bpm; }
    void setBpm(double bpm);
    double masterGainDb() const;
    void setMasterGainDb(double db);
    QVariantMap levels() const { return m_levelMap; }
    QStringList instrumentNames() const;
    bool recoveryAvailable() const { return !m_recoveryFile.isEmpty(); }
    QString recoveryInfo() const { return m_recoveryInfo; }

    // ---- Projektmodus, Start, zuletzt geöffnet
    bool musicMode() const { return m_project.mode == loggx::ProjectMode::Music; }
    bool showStart() const { return m_showStart; }
    void setShowStart(bool show);
    QVariantList recentProjects() const;
    Q_INVOKABLE void newMusicProject();
    // Vorlage: Video mit Größe/fps oder Musik mit Tempo
    // fps auch krumm (23.976, 29.97, 59.94 → x/1001)
    Q_INVOKABLE void newFromTemplate(bool music, int width, int height, double fps, double bpm);
    Q_INVOKABLE void forgetRecent(const QString& path);
    Q_INVOKABLE bool openRecent(const QString& path) { return openProjectFile(path); }

    // ---- Beat-Raster, Tempo, Übergänge
    bool beatGrid() const { return m_beatGrid; }
    void setBeatGrid(bool on);
    QStringList transitionNames() const;
    Q_INVOKABLE bool detectTempo();            // aus dem ausgewählten Audio-Clip
    Q_INVOKABLE int cutOnBeats(int everyBeats);

    // ---- Musik-Dateien
    Q_INVOKABLE void exportAudio(const QUrl& url, int bits = 16, bool tail = true, int mastering = 0);
    Q_INVOKABLE double projectLoudness();   // LUFS des aktuellen Mixes (Mastering-Assistent)
    Q_INVOKABLE void exportStems(const QUrl& folder, const QString& extension, int bits = 16);
    Q_INVOKABLE bool exportMidiFile(const QUrl& url);
    Q_INVOKABLE bool importMidiFile(const QUrl& url);

    // ---- Live spielen (Computer-/MIDI-Tastatur) und Aufnahme
    Q_INVOKABLE void liveNoteOn(int pitch, double velocity = 0.8);
    Q_INVOKABLE void liveNoteOff(int pitch);
    Q_INVOKABLE void allNotesOff();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void refreshMidiInputs();
    Q_INVOKABLE bool openMidiInput(int index);
    bool recording() const { return m_recording; }
    int keyboardOctave() const { return m_octave; }
    void setKeyboardOctave(int octave);
    QStringList midiInputs() const { return m_midiPorts; }
    int midiInput() const;
    bool midiAvailable() const;
    bool quantizeRecording() const { return m_quantize; }
    void setQuantizeRecording(bool on);
    QVariantList heldNotes() const;
    loggx::InstrumentType liveInstrument() const;

    // ---- Lizenz und Admin
    QString edition() const;
    QString editionName() const;
    QString licenseHolder() const { return m_licenseHolder; }
    bool licensingActive() const;
    bool isPro() const;
    bool isAdmin() const;
    bool simulateFree() const { return m_simulateFree; }
    void setSimulateFree(bool on);
    int previewQuality() const { return m_previewQuality; }
    void setPreviewQuality(int quality);
    QString previewSize() const { return m_previewSize; }
    // Größe der Vorschaufläche in echten Pixeln (Viewer meldet sich).
    Q_INVOKABLE void setViewerSize(int width, int height);
    double previewMs() const { return m_previewMs; }
    QVariantMap systemInfo() const;
    Q_INVOKABLE bool activateLicense(const QString& key);
    Q_INVOKABLE void removeLicense();
    Q_INVOKABLE QString licenseError() const { return m_licenseError; }

    // ---- Projekt
    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QUrl& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE void setProjectFormat(int width, int height, int fpsNum, int fpsDen = 1);
    int projectFpsNum() const { return m_project.fpsNum; }
    int projectFpsDen() const { return m_project.fpsDen; }
    int beatsPerBar() const { return m_project.beatsPerBar; }
    void setBeatsPerBar(int beats);

    // ---- Dateien hineinziehen: Medien importieren und (trackIndex >= 0 oder
    // -1 = passende Spur) an seconds in die Timeline legen; -2 = nur importieren.
    // Gibt den Pfad eines mitgezogenen Projekts zurück (zum Öffnen in QML).
    Q_INVOKABLE QString dropFiles(const QList<QUrl>& urls, int trackIndex = -1, double seconds = -1);

    // ---- Kopieren, Einfügen, Duplizieren, Standbild
    Q_INVOKABLE bool copySelected();
    Q_INVOKABLE bool pasteAtPlayhead();
    Q_INVOKABLE bool duplicateSelected();
    bool canPaste() const { return m_clipboard.has_value(); }
    Q_INVOKABLE bool exportStill(const QUrl& url);

    // ---- Mikrofon
    QStringList audioInputs() const;
    int audioInput() const { return m_audioInput; }
    void setAudioInput(int index);
    bool micRecording() const { return m_mic != nullptr; }
    double micLevel() const { return m_micLevel; }
    Q_INVOKABLE bool startMicRecording();
    Q_INVOKABLE void stopMicRecording();

    // ---- Tonart, Tempo, Stimme (ausgewählter Audio-/Video-Clip)
    bool working() const { return m_task != nullptr || m_downloading; }
    double workProgress() const { return m_workProgress; }
    QString workLabel() const { return m_workLabel; }
    Q_INVOKABLE bool analyzeSelected();                 // Tempo + Tonart der Quelle
    Q_INVOKABLE bool trackSelectedMask();               // Maske ab Abspielkopf verfolgen
    // KI-Freistellung: 0 schnell (mitgeliefert), 1 Personen beste Qualität (Download)
    Q_INVOKABLE int cutoutQuality() const;
    Q_INVOKABLE void setCutoutQuality(int q);
    Q_INVOKABLE void setAiCutout(bool on);               // für den gewählten Clip
    void updateCutoutModel();
    Q_INVOKABLE void setClipKey(int tonic);             // per Tonhöhe (Halbtöne)
    Q_INVOKABLE void setClipBpm(double bpm);            // per Tempo (Time-Stretch)
    Q_INVOKABLE void matchClipTempo();                  // auf Projekt-Tempo
    // method: 0 = KI beste Qualität, 1 = KI schnell, 2 = ohne KI
    Q_INVOKABLE bool separateSelected(bool keepVocals, int method = -1);
    bool aiAvailable() const;
    int separationMethod() const { return m_separationMethod; }
    QVariantList aiModels() const;
    bool aiAutoDownload() const { return m_aiAutoDownload; }
    void setAiAutoDownload(bool on);
    QString aiModelFolder() const;
    Q_INVOKABLE void downloadAllModels();
    Q_INVOKABLE void deleteModel(const QString& id);
    int startMode() const;
    void setStartMode(int mode);
    int autosaveMinutes() const { return m_autosaveMinutes; }
    void setAutosaveMinutes(int minutes);
    Q_INVOKABLE void clearRecent();
    void setSeparationMethod(int method);
    class ModelManager* models() const { return m_models; }
    // Fehlende KI-Modelle laden (mit Fortschritt), danach then() aufrufen.
    void ensureModels(const QStringList& ids, std::function<void()> then);
    Q_INVOKABLE void cancelWork();
    Q_INVOKABLE QStringList keyNames(bool minor) const;
    // ---- ganzer Song
    Q_INVOKABLE bool setSongTempo(double bpm);
    Q_INVOKABLE int transposeSong(int semitones);
    bool openProjectFile(const QString& path);

    // ---- Automatisches Sichern (alle 60 s, falls geändert)
    Q_INVOKABLE bool autosaveNow();
    Q_INVOKABLE bool recoverAutosave();
    Q_INVOKABLE void discardRecovery();
    static QString autosaveDir();

    // ---- Medien und Timeline
    Q_INVOKABLE void importMedia(const QList<QUrl>& urls);
    Q_INVOKABLE bool removeAsset(const QString& assetId);
    Q_INVOKABLE bool addAssetToTimeline(const QString& assetId);
    Q_INVOKABLE bool addClipAt(const QString& assetId, int trackIndex, double seconds);
    Q_INVOKABLE bool addTitle();
    Q_INVOKABLE bool addMidiClip();
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void deleteSelected(bool ripple = false);
    Q_INVOKABLE void addTrack(bool video);
    Q_INVOKABLE void addInstrumentTrack();
    Q_INVOKABLE bool removeTrack(int trackIndex);
    // Spur samt Inhalt löschen (ein Undo-Schritt), umbenennen, verschieben
    Q_INVOKABLE bool deleteTrack(int trackIndex);
    Q_INVOKABLE bool renameTrack(int trackIndex, const QString& name);
    Q_INVOKABLE bool moveTrack(int trackIndex, int direction);
    Q_INVOKABLE bool cutSelected();
    // Audiodatei als neues Musikprojekt öffnen (z. B. aus dem DJ-Bereich)
    Q_INVOKABLE bool openAudioInMusicEditor(const QString& path, double bpm = 0.0);
    Q_INVOKABLE void toggleTrackMute(int trackIndex);
    Q_INVOKABLE void setTrackProperty(int trackIndex, const QString& name, double value);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void selectClipAtPlayhead(bool videoOnly = true);
    Q_INVOKABLE void selectNeighbourClip(int direction);

    // Für Regler und Ziehen mit der Maus: alle Änderungen zwischen begin
    // und end ergeben zusammen einen einzigen Undo-Schritt.
    Q_INVOKABLE void beginInteractive();
    Q_INVOKABLE void endInteractive();
    // Setzt eine Eigenschaft des ausgewählten Clips. Hat der Parameter
    // Keyframes, wird der Keyframe am Abspielkopf gesetzt.
    Q_INVOKABLE void setClipProperty(const QString& name, double value);
    Q_INVOKABLE void setTitleText(const QString& text);
    Q_INVOKABLE void setTitleColor(const QColor& color);
    Q_INVOKABLE void setKeyColor(const QColor& color);
    Q_INVOKABLE bool setLut(const QUrl& url);
    Q_INVOKABLE void resetColor();
    Q_INVOKABLE void toggleKeyframe(const QString& param);
    Q_INVOKABLE void jumpToKeyframe(int direction);

    // ---- Wiedergabe
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE QString timecode(double seconds) const;

    // ---- Export (codec: 0 H.264, 1 H.265; quality 0..3)
    Q_INVOKABLE void exportVideo(const QUrl& url, int width, int height, int codec = 0,
                                 int quality = 1, bool hdr = false);
    Q_INVOKABLE void cancelExport();

signals:
    void projectChanged();
    void fileChanged();
    void positionChanged();
    void playingChanged();
    void assetsChanged();
    void selectionChanged();
    void clipChanged();
    void undoChanged();
    void exportChanged();
    void statusChanged();
    void levelsChanged();
    void recoveryChanged();
    void modelsChanged();
    void generalChanged();
    void startChanged();
    void recentChanged();
    void gridChanged();
    void recordingChanged();
    void liveChanged();
    void licenseChanged();
    void adminChanged();
    void statsChanged();
    void clipboardChanged();
    void micChanged();
    void micLevelChanged();
    void workChanged();
    void midiMessage(int status, int data1, int data2);
    void frameReady(const QImage& image);

private:
    template <typename Fn>
    bool apply(Fn&& fn);
    void publish();
    void resetState();
    void setStatus(const QString& text);
    void setDirty(bool dirty);
    void setPosition(loggx::TimeUs t);
    void requestFrame();
    void tick();
    void pollLevels();
    void startAudio();
    void stopAudio();
    void removeAutosave();
    void checkRecovery();
    int trackIndexOf(loggx::TrackKind kind, bool last) const;
    void addRecent(const QString& path);
    void loadLicense();
    void ensureLiveAudio();
    void handleMidi(int status, int data1, int data2);
    loggx::TimeUs clockPosition() const;
    void startExport(loggx::ExportSettings settings, int mode, const QString& stemsExt = {});

    loggx::Project m_project;
    std::shared_ptr<const loggx::Project> m_shared;
    loggx::UndoStack<loggx::Project> m_undo;
    std::optional<loggx::Project> m_interactive;
    bool m_liveChanged = false;

    QString m_filePath;
    bool m_dirty = false;
    QString m_selected;
    QString m_status;

    loggx::TimeUs m_position = 0;
    bool m_playing = false;
    loggx::TimeUs m_playStart = 0;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QTimer m_meterTimer;
    QVariantMap m_levelMap;

    QTimer m_autosaveTimer;
    QString m_recoveryFile;
    QString m_recoveryOriginal;
    QString m_recoveryInfo;

    PreviewWorker m_preview;
    MediaCache m_cache;
    loggx::AudioMixer m_mixer;
    MixerDevice m_mixerDevice;
    QAudioSink* m_sink = nullptr;

    ExportWorker* m_exportWorker = nullptr;
    double m_exportProgress = 0.0;

    bool m_showStart = true;
    bool m_beatGrid = false;

    // Live
    loggx::LiveSynth m_live;
    LiveDevice m_liveDevice;
    QAudioSink* m_liveSink = nullptr;
    std::unique_ptr<class MidiInput> m_midi;
    QStringList m_midiPorts;
    int m_octave = 5;   // C4 (MIDI 60)
    bool m_quantize = true;
    QList<int> m_held;
    bool m_recording = false;
    loggx::TimeUs m_recordStart = 0;
    QString m_recordTrack;
    struct RecordedNote {
        int pitch;
        double velocity;
        loggx::TimeUs on, off;
    };
    std::vector<RecordedNote> m_recorded;

    // Lizenz / Admin
    loggx::Edition m_editionLicensed = loggx::Edition::Free;
    QString m_licenseHolder;
    QString m_licenseError;
    bool m_simulateFree = false;
    double m_previewMs = 0.0;

    // Vorschau
    int m_previewQuality = 0;
    int m_viewerW = 0, m_viewerH = 0;
    double m_playScale = 1.0;
    QString m_previewSize;

    // Zwischenablage (Clip + Art der Quellspur)
    std::optional<loggx::Clip> m_clipboard;
    loggx::TrackKind m_clipboardKind = loggx::TrackKind::Video;

    // Mikrofon
    int m_audioInput = 0;
    class MicRecorder* m_mic = nullptr;
    double m_micLevel = 0.0;
    loggx::TimeUs m_micStart = 0;

    // Hintergrundarbeit
    TaskWorker* m_task = nullptr;
    class ModelManager* m_models = nullptr;
    bool m_downloading = false;
    int m_separationMethod = 0;
    bool m_aiAutoDownload = false;
    int m_autosaveMinutes = 1;
    double m_workProgress = 0.0;
    QString m_workLabel;
    void runTask(const QString& label, TaskWorker::Job job, std::function<void()> finished);
};

// Führt eine Änderung am Projekt aus: Undo-Schritt (außer bei laufender
// Interaktion), dann Engine und Oberfläche benachrichtigen.
template <typename Fn>
bool AppController::apply(Fn&& fn) {
    loggx::Project before = m_project;
    if (!fn(m_project)) return false;
    if (m_interactive)
        m_liveChanged = true;
    else
        m_undo.push(std::move(before));
    publish();
    return true;
}
