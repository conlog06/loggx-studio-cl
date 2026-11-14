// Zentrale Schnittstelle zwischen Oberfläche (QML) und Engine: hält das
// Projekt, Undo/Redo, Auswahl, Wiedergabe und Export.
#pragma once

#include <memory>
#include <optional>

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "Workers.h"
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
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY assetsChanged)
    Q_PROPERTY(QString selectedClipId READ selectedClipId WRITE setSelectedClipId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap clip READ clip NOTIFY clipChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool stretchAvailable READ stretchAvailable CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // ---- Zugriff für die C++-Elemente (Timeline)
    const loggx::Project& project() const { return m_project; }
    loggx::TimeUs positionUs() const { return m_position; }
    bool moveClipLive(const QString& clipId, const QString& trackId, loggx::TimeUs start);
    bool trimStartLive(const QString& clipId, loggx::TimeUs start);
    bool trimEndLive(const QString& clipId, loggx::TimeUs end);
    void seekUs(loggx::TimeUs t);

    // ---- Eigenschaften
    QString projectName() const { return QString::fromStdString(m_project.name); }
    void setProjectName(const QString& name);
    QString filePath() const { return m_filePath; }
    bool dirty() const { return m_dirty; }
    QString formatInfo() const;
    int projectWidth() const { return m_project.width; }
    int projectHeight() const { return m_project.height; }
    double position() const { return m_position / 1e6; }
    double duration() const { return m_project.duration() / 1e6; }
    bool playing() const { return m_playing; }
    QVariantList assets() const;
    QString selectedClipId() const { return m_selected; }
    void setSelectedClipId(const QString& id);
    QVariantMap clip() const;
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    bool exporting() const { return m_exportWorker != nullptr; }
    double exportProgress() const { return m_exportProgress; }
    QString status() const { return m_status; }
    bool stretchAvailable() const { return loggx::AudioMixer::hasStretcher(); }

    // ---- Projekt
    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QUrl& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE void setProjectFormat(int width, int height, int fps);
    bool openProjectFile(const QString& path);

    // ---- Medien und Timeline
    Q_INVOKABLE void importMedia(const QList<QUrl>& urls);
    Q_INVOKABLE bool removeAsset(const QString& assetId);
    Q_INVOKABLE bool addAssetToTimeline(const QString& assetId);
    Q_INVOKABLE bool addClipAt(const QString& assetId, int trackIndex, double seconds);
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void deleteSelected(bool ripple = false);
    Q_INVOKABLE void addTrack(bool video);
    Q_INVOKABLE void toggleTrackMute(int trackIndex);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Für Regler und Ziehen mit der Maus: alle Änderungen zwischen begin
    // und end ergeben zusammen einen einzigen Undo-Schritt.
    Q_INVOKABLE void beginInteractive();
    Q_INVOKABLE void endInteractive();
    Q_INVOKABLE void setClipProperty(const QString& name, double value);

    // ---- Wiedergabe
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE QString timecode(double seconds) const;

    // ---- Export
    Q_INVOKABLE void exportVideo(const QUrl& url, int width, int height);
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
    void startAudio();
    void stopAudio();

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

    PreviewWorker m_preview;
    loggx::AudioMixer m_mixer;
    MixerDevice m_mixerDevice;
    QAudioSink* m_sink = nullptr;

    ExportWorker* m_exportWorker = nullptr;
    double m_exportProgress = 0.0;
};
