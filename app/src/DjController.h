// Arbeitsbereich „DJ“: Bibliothek, zwei Decks, Mixer, Effekte, Mitschnitt
// und KI-Stems. Für QML als „dj“ verfügbar.
#pragma once

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

public:
    explicit DjController(AppController* app, QObject* parent = nullptr);
    ~DjController() override;

    // ---- Bibliothek
    QVariantList library() const;
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void removeTrack(const QString& path);
    Q_INVOKABLE void prepareStems(const QString& path);   // KI: Gesang, Schlagzeug, Bass, Rest
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

private:
    struct Entry {
        QString path, title, artist;
        double bpm = 0, firstBeat = 0, duration = 0;
        int key = -1;
        bool minor = false;
        bool analyzed = false;
    };
    QVariantMap deckInfo(int deck) const;
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
    Device* m_device = nullptr;
    QAudioSink* m_sink = nullptr;
    bool m_active = false;

    // Mitschnitt
    std::mutex m_recMutex;
    std::vector<float> m_recBuffer;
    WavWriter* m_writer = nullptr;
    QString m_recPath;
};
