#include "AppController.h"
#include "MicRecorder.h"
#include "ModelManager.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QSettings>
#include <QAudioFormat>
#include <QAudioSink>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QStandardPaths>

using namespace loggx;

namespace {

std::string str(const QString& s) { return s.toStdString(); }
QString qstr(const std::string& s) { return QString::fromStdString(s); }

double toDb(double gain) { return gain <= 0.001 ? -60.0 : 20.0 * std::log10(gain); }
double fromDb(double db) { return db <= -59.9 ? 0.0 : std::pow(10.0, db / 20.0); }

QColor argb(std::uint32_t c) { return QColor::fromRgba(c); }

const char* kindId(ClipKind k) {
    switch (k) {
        case ClipKind::Title: return "title";
        case ClipKind::Midi: return "midi";
        default: return "media";
    }
}

}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent), m_project(Project::createDefault()), m_mixerDevice(&m_mixer), m_liveDevice(&m_live) {
    connect(&m_preview, &PreviewWorker::frameReady, this, &AppController::frameReady);
    connect(&m_preview, &PreviewWorker::renderTime, this, [this](double ms) {
        m_previewMs = m_previewMs <= 0 ? ms : m_previewMs * 0.8 + ms * 0.2;
        // Auto-Qualität beim Abspielen: so groß wie möglich, solange die
        // Bilder rechtzeitig fertig werden.
        if (m_playing && m_previewQuality == 0) {
            const double frameMs = 1000.0 / std::max(1.0, m_project.fps());
            if (ms > frameMs * 0.9)
                m_playScale = std::max(0.3, m_playScale * 0.85);
            else if (ms < frameMs * 0.45)
                m_playScale = std::min(1.0, m_playScale * 1.08);
        }
        emit statsChanged();
    });
    m_midi = std::make_unique<MidiInput>();
    connect(this, &AppController::midiMessage, this, &AppController::handleMidi, Qt::QueuedConnection);
    m_models = new ModelManager(this);
    loadLicense();
    m_preview.start();
    m_cache.start();
    m_timer.setInterval(15);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AppController::tick);
    m_meterTimer.setInterval(40);
    connect(&m_meterTimer, &QTimer::timeout, this, &AppController::pollLevels);
    m_autosaveMinutes = QSettings().value(QStringLiteral("general/autosaveMinutes"), 1).toInt();
    m_autosaveTimer.setInterval(std::max(1, m_autosaveMinutes) * 60'000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty) autosaveNow();
    });
    if (m_autosaveMinutes > 0) m_autosaveTimer.start();
    // KI-Modelle auf Wunsch im Hintergrund holen (nach dem Start, ohne zu stören)
    m_aiAutoDownload = QSettings().value(QStringLiteral("ai/autoDownload"), false).toBool();
    if (m_aiAutoDownload) QTimer::singleShot(4000, this, [this] { downloadAllModels(); });
    checkRecovery();
    publish();
    m_dirty = false;
}

AppController::~AppController() {
    stopAudio();
    if (m_midi) m_midi->close();
    if (m_liveSink) m_liveSink->stop();
    if (m_exportWorker) {
        m_exportWorker->cancel();
        m_exportWorker->wait();
    }
    if (m_models) m_models->cancel();
    if (m_task) {
        m_task->disconnect();
        m_task->cancel();
        m_task->wait();
    }
    if (m_mic) m_mic->stop();
    // Ordentlich beendet: Notfallkopie ist nicht mehr nötig.
    if (!m_dirty) removeAutosave();
}

// ------------------------------------------------------------ Zustand

void AppController::publish() {
    m_shared = std::make_shared<const Project>(m_project);
    m_mixer.setProject(m_shared);
    m_cache.request(m_project);
    if (!m_selected.isEmpty() && !m_project.findClip(str(m_selected))) {
        m_selected.clear();
        emit selectionChanged();
    }
    setDirty(true);
    emit projectChanged();
    emit assetsChanged();
    emit clipChanged();
    emit undoChanged();
    requestFrame();
}

void AppController::resetState() {
    pause();
    m_undo.clear();
    m_interactive.reset();
    m_liveChanged = false;
    m_selected.clear();
    m_position = 0;
    emit selectionChanged();
    emit positionChanged();
}

void AppController::setStatus(const QString& text) {
    m_status = text;
    emit statusChanged();
}

void AppController::setDirty(bool dirty) {
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit fileChanged();
}

QString AppController::formatInfo() const {
    return QStringLiteral("%1×%2 · %3 fps")
        .arg(m_project.width)
        .arg(m_project.height)
        .arg(QString::number(m_project.fps(), 'f', m_project.fpsDen == 1 ? 0 : 3));
}

void AppController::setProjectName(const QString& name) {
    if (name == projectName() || name.trimmed().isEmpty()) return;
    apply([&](Project& p) {
        p.name = str(name.trimmed());
        return true;
    });
}

void AppController::setBpm(double bpm) {
    bpm = std::clamp(bpm, 20.0, 300.0);
    if (std::abs(bpm - m_project.bpm) < 1e-9) return;
    apply([&](Project& p) {
        p.bpm = bpm;
        return true;
    });
}

double AppController::masterGainDb() const { return toDb(m_project.masterGain); }

void AppController::setMasterGainDb(double db) {
    apply([&](Project& p) {
        p.masterGain = fromDb(std::clamp(db, -60.0, 6.0));
        return true;
    });
}

QStringList AppController::instrumentNames() const {
    QStringList list;
    for (int i = 0; i < kInstrumentCount; ++i)
        list << QString::fromUtf8(instrumentName(static_cast<InstrumentType>(i)));
    return list;
}

// ------------------------------------------------------------ Projekt

void AppController::newProject() {
    resetState();
    m_project = Project::createDefault();
    m_filePath.clear();
    m_beatGrid = false;
    emit gridChanged();
    publish();
    setDirty(false);
    removeAutosave();
    emit fileChanged();
    setShowStart(false);
    setStatus(tr("Neues Videoprojekt"));
}

bool AppController::openProject(const QUrl& url) { return openProjectFile(url.toLocalFile()); }

bool AppController::openProjectFile(const QString& path) {
    Project loaded;
    std::string error;
    if (!Project::loadFile(str(path), loaded, &error)) {
        setStatus(tr("Projekt kann nicht geöffnet werden: %1").arg(qstr(error)));
        return false;
    }
    resetState();
    m_project = std::move(loaded);
    m_filePath = path;
    publish();
    setDirty(false);
    emit fileChanged();

    int missing = 0;
    for (const auto& a : m_project.assets)
        if (!QFileInfo::exists(qstr(a.path))) ++missing;
    setStatus(missing > 0 ? tr("Geöffnet – %1 Mediendatei(en) nicht gefunden").arg(missing)
                          : tr("Projekt geöffnet"));
    m_beatGrid = musicMode();
    emit gridChanged();
    addRecent(path);
    setShowStart(false);
    return true;
}

bool AppController::saveProject() {
    if (m_filePath.isEmpty()) return false;
    std::string error;
    if (!m_project.saveFile(str(m_filePath), &error)) {
        setStatus(tr("Speichern fehlgeschlagen: %1").arg(qstr(error)));
        return false;
    }
    setDirty(false);
    removeAutosave();
    addRecent(m_filePath);
    setStatus(tr("Gespeichert"));
    return true;
}

bool AppController::saveProjectAs(const QUrl& url) {
    QString path = url.toLocalFile();
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".loggx"), Qt::CaseInsensitive)) path += QStringLiteral(".loggx");
    const QString previous = m_filePath;
    m_filePath = path;
    if (!saveProject()) {
        m_filePath = previous;
        return false;
    }
    emit fileChanged();
    return true;
}

void AppController::setProjectFormat(int width, int height, int fpsNum, int fpsDen) {
    if (fpsDen < 1) fpsDen = 1;
    const double fps = static_cast<double>(fpsNum) / fpsDen;
    if (width < 16 || height < 16 || width > 16384 || height > 16384 || fps < 1 || fps > 240) return;
    if ((width & ~1) == m_project.width && (height & ~1) == m_project.height && fpsNum == m_project.fpsNum &&
        fpsDen == m_project.fpsDen)
        return;
    apply([&](Project& p) {
        p.width = width & ~1;
        p.height = height & ~1;
        p.fpsNum = fpsNum;
        p.fpsDen = fpsDen;
        return true;
    });
    setStatus(tr("Projektformat: %1").arg(formatInfo()));
}

// ---------------------------------------------------- Automatisch sichern

QString AppController::autosaveDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/autosave");
}

bool AppController::autosaveNow() {
    const QString dir = autosaveDir();
    if (!QDir().mkpath(dir)) return false;
    std::string error;
    if (!m_project.saveFile(str(dir + QStringLiteral("/autosave.loggx")), &error)) return false;
    QJsonObject info;
    info[QStringLiteral("original")] = m_filePath;
    info[QStringLiteral("name")] = projectName();
    info[QStringLiteral("time")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QFile f(dir + QStringLiteral("/autosave.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(info).toJson());
    return true;
}

void AppController::removeAutosave() {
    const QString dir = autosaveDir();
    QFile::remove(dir + QStringLiteral("/autosave.loggx"));
    QFile::remove(dir + QStringLiteral("/autosave.json"));
}

void AppController::checkRecovery() {
    const QString dir = autosaveDir();
    const QString file = dir + QStringLiteral("/autosave.loggx");
    QFile meta(dir + QStringLiteral("/autosave.json"));
    if (!QFileInfo::exists(file) || !meta.open(QIODevice::ReadOnly)) return;
    const QJsonObject info = QJsonDocument::fromJson(meta.readAll()).object();
    m_recoveryFile = file;
    m_recoveryOriginal = info.value(QStringLiteral("original")).toString();
    const QDateTime time = QDateTime::fromString(info.value(QStringLiteral("time")).toString(), Qt::ISODate);
    m_recoveryInfo = tr("„%1“ wurde nicht gespeichert (automatisch gesichert am %2).")
                         .arg(info.value(QStringLiteral("name")).toString(),
                              time.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
    emit recoveryChanged();
}

bool AppController::recoverAutosave() {
    if (m_recoveryFile.isEmpty()) return false;
    Project loaded;
    std::string error;
    const bool ok = Project::loadFile(str(m_recoveryFile), loaded, &error);
    if (ok) {
        resetState();
        m_project = std::move(loaded);
        // Zurück an den ursprünglichen Speicherort, aber als ungespeichert.
        m_filePath = m_recoveryOriginal;
        publish();
        setDirty(true);
        emit fileChanged();
        setStatus(tr("Wiederhergestellt – bitte speichern"));
    } else {
        setStatus(tr("Wiederherstellen fehlgeschlagen: %1").arg(qstr(error)));
    }
    m_recoveryFile.clear();
    emit recoveryChanged();
    return ok;
}

void AppController::discardRecovery() {
    removeAutosave();
    m_recoveryFile.clear();
    emit recoveryChanged();
}

// ------------------------------------------------- Medien und Timeline

QVariantList AppController::assets() const {
    QVariantList list;
    for (const auto& a : m_project.assets) {
        QVariantMap m;
        m[QStringLiteral("id")] = qstr(a.id);
        m[QStringLiteral("name")] = qstr(a.name);
        m[QStringLiteral("path")] = qstr(a.path);
        m[QStringLiteral("duration")] = a.duration / 1e6;
        m[QStringLiteral("hasVideo")] = a.hasVideo;
        m[QStringLiteral("hasAudio")] = a.hasAudio;
        m[QStringLiteral("isStill")] = a.isStill;
        QString info;
        if (a.isStill)
            info = tr("Bild · %1×%2").arg(a.width).arg(a.height);
        else if (a.hasVideo)
            info = tr("Video · %1×%2 · %3").arg(a.width).arg(a.height).arg(timecode(a.duration / 1e6));
        else
            info = tr("Audio · %1").arg(timecode(a.duration / 1e6));
        m[QStringLiteral("info")] = info;
        m[QStringLiteral("missing")] = !QFileInfo::exists(qstr(a.path));
        list.append(m);
    }
    return list;
}

QVariantList AppController::tracks() const {
    QVariantList list;
    for (int i = 0; i < static_cast<int>(m_project.tracks.size()); ++i) {
        const Track& t = m_project.tracks[static_cast<std::size_t>(i)];
        QVariantMap m;
        m[QStringLiteral("index")] = i;
        m[QStringLiteral("id")] = qstr(t.id);
        m[QStringLiteral("name")] = qstr(t.name);
        m[QStringLiteral("kind")] = t.kind == TrackKind::Video   ? QStringLiteral("video")
                                    : t.kind == TrackKind::Audio ? QStringLiteral("audio")
                                                                 : QStringLiteral("instrument");
        m[QStringLiteral("muted")] = t.muted;
        m[QStringLiteral("solo")] = t.solo;
        m[QStringLiteral("gainDb")] = toDb(t.gain);
        m[QStringLiteral("pan")] = t.pan;
        m[QStringLiteral("instrument")] = static_cast<int>(t.instrument);
        m[QStringLiteral("eqLow")] = t.fx.eqLow;
        m[QStringLiteral("eqMid")] = t.fx.eqMid;
        m[QStringLiteral("eqHigh")] = t.fx.eqHigh;
        m[QStringLiteral("compressor")] = t.fx.compressor;
        m[QStringLiteral("reverb")] = t.fx.reverb;
        m[QStringLiteral("delay")] = t.fx.delay;
        m[QStringLiteral("empty")] = t.clips.empty();
        bool sound = t.kind != TrackKind::Video;
        for (const auto& c : t.clips)
            if (c.kind == ClipKind::Media)
                if (const MediaAsset* a = m_project.findAsset(c.assetId); a && a->hasAudio) sound = true;
        m[QStringLiteral("hasSound")] = sound;
        list.append(m);
    }
    return list;
}

void AppController::importMedia(const QList<QUrl>& urls) {
    int added = 0;
    QString lastError;
    Project before = m_project;
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const bool known = std::any_of(m_project.assets.begin(), m_project.assets.end(),
                                       [&](const MediaAsset& a) { return qstr(a.path) == path; });
        if (known) continue;
        MediaAsset asset;
        std::string error;
        if (!probeMedia(str(path), asset, &error)) {
            lastError = tr("%1: %2").arg(QFileInfo(path).fileName(), qstr(error));
            continue;
        }
        m_project.addAsset(std::move(asset));
        ++added;
    }
    if (added > 0) {
        m_undo.push(std::move(before));
        publish();
    }
    if (!lastError.isEmpty())
        setStatus(tr("Import fehlgeschlagen – %1").arg(lastError));
    else if (added > 0)
        setStatus(tr("%1 Datei(en) importiert").arg(added));
}

bool AppController::removeAsset(const QString& assetId) {
    const bool ok = apply([&](Project& p) { return p.removeAsset(str(assetId)); });
    if (!ok) setStatus(tr("Die Datei wird noch in der Timeline verwendet"));
    return ok;
}

int AppController::trackIndexOf(TrackKind kind, bool last) const {
    int index = -1;
    for (int i = 0; i < static_cast<int>(m_project.tracks.size()); ++i) {
        if (m_project.tracks[static_cast<std::size_t>(i)].kind != kind) continue;
        index = i;
        if (!last) break;
    }
    return index;
}

bool AppController::addAssetToTimeline(const QString& assetId) {
    const MediaAsset* asset = m_project.findAsset(str(assetId));
    if (!asset) return false;
    // Videos auf die unterste Videospur, Ton auf die oberste Audiospur.
    const int index = asset->hasVideo ? trackIndexOf(TrackKind::Video, true)
                                      : trackIndexOf(TrackKind::Audio, false);
    return index >= 0 && addClipAt(assetId, index, position());
}

bool AppController::addClipAt(const QString& assetId, int trackIndex, double seconds) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return false;
    std::string newId;
    const bool ok = apply([&](Project& p) {
        newId = p.addClip(p.tracks[static_cast<std::size_t>(trackIndex)].id, str(assetId),
                          static_cast<TimeUs>(std::max(0.0, seconds) * 1e6));
        return !newId.empty();
    });
    if (ok)
        setSelectedClipId(qstr(newId));
    else
        setStatus(tr("Diese Datei passt nicht auf diese Spur"));
    return ok;
}

bool AppController::addTitle() {
    // Titel auf die oberste Videospur, damit sie über dem Bild liegen.
    const int index = trackIndexOf(TrackKind::Video, false);
    if (index < 0) return false;
    std::string newId;
    const bool ok = apply([&](Project& p) {
        newId = p.addTitleClip(p.tracks[static_cast<std::size_t>(index)].id, m_position,
                               tr("Titel").toStdString());
        return !newId.empty();
    });
    if (ok) setSelectedClipId(qstr(newId));
    return ok;
}

bool AppController::addMidiClip() {
    // Bevorzugt die Spur des ausgewählten Clips, sonst die erste Instrumentspur.
    int index = -1;
    const Track* owner = nullptr;
    if (!m_selected.isEmpty() && m_project.findClip(str(m_selected), &owner) &&
        owner->kind == TrackKind::Instrument) {
        for (int i = 0; i < static_cast<int>(m_project.tracks.size()); ++i)
            if (&m_project.tracks[static_cast<std::size_t>(i)] == owner) index = i;
    }
    if (index < 0) index = trackIndexOf(TrackKind::Instrument, false);
    if (index < 0) {
        addInstrumentTrack();
        index = trackIndexOf(TrackKind::Instrument, false);
    }
    // Am Takt ausrichten.
    const TimeUs bar = m_project.beatDuration() * m_project.beatsPerBar;
    const TimeUs start = bar > 0 ? m_position / bar * bar : m_position;
    std::string newId;
    const bool ok = apply([&](Project& p) {
        newId = p.addMidiClip(p.tracks[static_cast<std::size_t>(index)].id, start);
        return !newId.empty();
    });
    if (ok) setSelectedClipId(qstr(newId));
    return ok;
}

void AppController::splitAtPlayhead() {
    const TimeUs t = m_position;
    const bool ok = apply([&](Project& p) {
        // Ist ein Clip ausgewählt, der unter dem Abspielkopf liegt, nur
        // diesen teilen – sonst alle Clips an dieser Stelle.
        if (!m_selected.isEmpty()) {
            const Clip* c = p.findClip(str(m_selected));
            if (c && t > c->start && t < c->end()) return !p.splitClip(c->id, t).empty();
        }
        std::vector<std::string> ids;
        for (const auto& track : p.tracks)
            for (const auto& c : track.clips)
                if (t > c.start && t < c.end()) ids.push_back(c.id);
        bool any = false;
        for (const auto& id : ids) any = !p.splitClip(id, t).empty() || any;
        return any;
    });
    if (!ok) setStatus(tr("Kein Clip unter dem Abspielkopf"));
}

void AppController::deleteSelected(bool ripple) {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) { return p.removeClip(id, ripple); });
}

void AppController::addTrack(bool video) {
    apply([&](Project& p) {
        p.addTrack(video ? TrackKind::Video : TrackKind::Audio);
        return true;
    });
}

void AppController::addInstrumentTrack() {
    apply([&](Project& p) {
        p.addTrack(TrackKind::Instrument);
        return true;
    });
}

bool AppController::removeTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return false;
    const bool ok = apply([&](Project& p) {
        return p.removeTrack(p.tracks[static_cast<std::size_t>(trackIndex)].id);
    });
    if (!ok) setStatus(tr("Nur leere Spuren lassen sich entfernen"));
    return ok;
}

void AppController::toggleTrackMute(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return;
    apply([&](Project& p) {
        Track& t = p.tracks[static_cast<std::size_t>(trackIndex)];
        t.muted = !t.muted;
        return true;
    });
}

void AppController::setTrackProperty(int trackIndex, const QString& name, double value) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return;
    apply([&](Project& p) {
        Track& t = p.tracks[static_cast<std::size_t>(trackIndex)];
        if (name == QLatin1String("gainDb")) t.gain = fromDb(std::clamp(value, -60.0, 6.0));
        else if (name == QLatin1String("pan")) t.pan = std::clamp(value, -1.0, 1.0);
        else if (name == QLatin1String("muted")) t.muted = value != 0.0;
        else if (name == QLatin1String("solo")) t.solo = value != 0.0;
        else if (name == QLatin1String("instrument"))
            t.instrument = static_cast<InstrumentType>(std::clamp(static_cast<int>(value), 0, kInstrumentCount - 1));
        else if (name == QLatin1String("eqLow")) t.fx.eqLow = std::clamp(value, -12.0, 12.0);
        else if (name == QLatin1String("eqMid")) t.fx.eqMid = std::clamp(value, -12.0, 12.0);
        else if (name == QLatin1String("eqHigh")) t.fx.eqHigh = std::clamp(value, -12.0, 12.0);
        else if (name == QLatin1String("compressor")) t.fx.compressor = std::clamp(value, 0.0, 1.0);
        else if (name == QLatin1String("reverb")) t.fx.reverb = std::clamp(value, 0.0, 1.0);
        else if (name == QLatin1String("delay")) t.fx.delay = std::clamp(value, 0.0, 1.0);
        else return false;
        return true;
    });
}

void AppController::undo() {
    endInteractive();
    if (m_undo.undo(m_project)) publish();
}

void AppController::redo() {
    endInteractive();
    if (m_undo.redo(m_project)) publish();
}

void AppController::selectClipAtPlayhead(bool videoOnly) {
    for (const auto& track : m_project.tracks) {
        if (videoOnly && track.kind != TrackKind::Video) continue;
        for (const auto& c : track.clips)
            if (m_position >= c.start && m_position < c.end()) {
                setSelectedClipId(qstr(c.id));
                return;
            }
    }
}

void AppController::selectNeighbourClip(int direction) {
    // Nächster/vorheriger Clip auf den Videospuren, Abspielkopf springt hin.
    const Clip* best = nullptr;
    for (const auto& track : m_project.tracks) {
        if (track.kind != TrackKind::Video) continue;
        for (const auto& c : track.clips) {
            const bool after = c.start > m_position;
            const bool before = c.start < m_position && !(m_position >= c.start && m_position < c.end());
            if ((direction > 0 && after && (!best || c.start < best->start)) ||
                (direction < 0 && before && (!best || c.start > best->start)))
                best = &c;
        }
    }
    if (!best) return;
    setSelectedClipId(qstr(best->id));
    seekUs(best->start);
}

void AppController::beginInteractive() {
    if (!m_interactive) {
        m_interactive = m_project;
        m_liveChanged = false;
    }
}

void AppController::endInteractive() {
    if (!m_interactive) return;
    if (m_liveChanged) m_undo.push(std::move(*m_interactive));
    m_interactive.reset();
    m_liveChanged = false;
    emit undoChanged();
}

bool AppController::moveClipLive(const QString& clipId, const QString& trackId, TimeUs start) {
    return apply([&](Project& p) { return p.moveClip(str(clipId), str(trackId), start); });
}

bool AppController::trimStartLive(const QString& clipId, TimeUs start) {
    return apply([&](Project& p) { return p.trimClipStart(str(clipId), start); });
}

bool AppController::trimEndLive(const QString& clipId, TimeUs end) {
    return apply([&](Project& p) { return p.trimClipEnd(str(clipId), end); });
}

int AppController::addNote(const QString& clipId, const Note& note) {
    int index = -1;
    apply([&](Project& p) {
        Clip* c = p.findClip(str(clipId));
        if (!c || c->kind != ClipKind::Midi) return false;
        c->notes.push_back(note);
        index = static_cast<int>(c->notes.size()) - 1;
        return true;
    });
    return index;
}

bool AppController::updateNote(const QString& clipId, int index, const Note& note) {
    return apply([&](Project& p) {
        Clip* c = p.findClip(str(clipId));
        if (!c || index < 0 || index >= static_cast<int>(c->notes.size())) return false;
        Note n = note;
        n.pitch = std::clamp(n.pitch, 0, 127);
        n.length = std::max<TimeUs>(10'000, n.length);
        n.start = std::max<TimeUs>(0, n.start);
        n.velocity = std::clamp(n.velocity, 0.05, 1.0);
        Note& old = c->notes[static_cast<std::size_t>(index)];
        if (old.pitch == n.pitch && old.start == n.start && old.length == n.length &&
            old.velocity == n.velocity)
            return false;
        old = n;
        return true;
    });
}

bool AppController::removeNote(const QString& clipId, int index) {
    return apply([&](Project& p) {
        Clip* c = p.findClip(str(clipId));
        if (!c || index < 0 || index >= static_cast<int>(c->notes.size())) return false;
        c->notes.erase(c->notes.begin() + index);
        return true;
    });
}

void AppController::setSelectedClipId(const QString& id) {
    if (id == m_selected) return;
    m_selected = id;
    emit selectionChanged();
    emit clipChanged();
}

QVariantMap AppController::clip() const {
    QVariantMap m;
    const Track* track = nullptr;
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected), &track);
    if (!c) return m;
    const MediaAsset* asset = c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    const TimeUs t = std::clamp(m_position, c->start, c->end());
    QString name = asset ? qstr(asset->name) : QString();
    if (c->kind == ClipKind::Title) name = tr("Titel: %1").arg(qstr(c->title.text).section(QLatin1Char('\n'), 0, 0));
    if (c->kind == ClipKind::Midi) name = tr("MIDI-Clip (%1 Noten)").arg(c->notes.size());

    m[QStringLiteral("id")] = qstr(c->id);
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("kind")] = QString::fromLatin1(kindId(c->kind));
    m[QStringLiteral("isVideo")] = track->kind == TrackKind::Video;
    m[QStringLiteral("isTitle")] = c->kind == ClipKind::Title;
    m[QStringLiteral("isMidi")] = c->kind == ClipKind::Midi;
    m[QStringLiteral("isMedia")] = c->kind == ClipKind::Media;
    m[QStringLiteral("hasAudio")] = (asset && asset->hasAudio) || c->kind == ClipKind::Midi;
    m[QStringLiteral("start")] = c->start / 1e6;
    m[QStringLiteral("duration")] = c->duration / 1e6;
    m[QStringLiteral("pitch")] = c->pitch;
    m[QStringLiteral("speed")] = c->speed;
    m[QStringLiteral("fadeIn")] = c->fadeIn / 1e6;
    m[QStringLiteral("fadeOut")] = c->fadeOut / 1e6;

    // Animierbare Werte: am Abspielkopf ausgewertet.
    QVariantMap keyed, keyHere;
    for (const char* p : kKeyframeParams) {
        const std::string name = p;
        m[QString::fromLatin1(p)] = c->valueAt(name, t);
        const auto it = c->keys.find(name);
        const bool has = it != c->keys.end() && !it->second.empty();
        keyed[QString::fromLatin1(p)] = has;
        bool here = false;
        if (has)
            for (const auto& k : it->second)
                if (std::llabs(k.t - (m_position - c->start)) < 1000) here = true;
        keyHere[QString::fromLatin1(p)] = here;
    }
    m[QStringLiteral("keyed")] = keyed;
    m[QStringLiteral("keyHere")] = keyHere;

    m[QStringLiteral("maskShape")] = static_cast<int>(c->mask.shape);
    m[QStringLiteral("maskX")] = c->mask.cx;
    m[QStringLiteral("maskY")] = c->mask.cy;
    m[QStringLiteral("maskW")] = c->mask.w;
    m[QStringLiteral("maskH")] = c->mask.h;
    m[QStringLiteral("maskFeather")] = c->mask.feather;
    m[QStringLiteral("maskInvert")] = c->mask.invert;

    const ColorGrade& g = c->color;
    m[QStringLiteral("exposure")] = g.exposure;
    m[QStringLiteral("contrast")] = g.contrast;
    m[QStringLiteral("saturation")] = g.saturation;
    m[QStringLiteral("temperature")] = g.temperature;
    m[QStringLiteral("tint")] = g.tint;
    m[QStringLiteral("lift")] = g.lift;
    m[QStringLiteral("gamma")] = g.gamma;
    m[QStringLiteral("colorGain")] = g.gain;
    m[QStringLiteral("lutName")] = g.lutPath.empty() ? QString() : QFileInfo(qstr(g.lutPath)).fileName();
    m[QStringLiteral("lutMix")] = g.lutMix;
    m[QStringLiteral("colorNeutral")] = g.isNeutral();

    m[QStringLiteral("chromaKey")] = c->fx.chromaKey;
    m[QStringLiteral("keyColor")] = QColor::fromRgbF(static_cast<float>(c->fx.keyR),
                                                     static_cast<float>(c->fx.keyG),
                                                     static_cast<float>(c->fx.keyB));
    m[QStringLiteral("keyTolerance")] = c->fx.keyTolerance;
    m[QStringLiteral("keySoftness")] = c->fx.keySoftness;
    m[QStringLiteral("vignette")] = c->fx.vignette;
    m[QStringLiteral("sharpen")] = c->fx.sharpen;
    m[QStringLiteral("grain")] = c->fx.grain;
    m[QStringLiteral("pixelate")] = c->fx.pixelate;
    m[QStringLiteral("glow")] = c->fx.glow;
    m[QStringLiteral("rgbSplit")] = c->fx.rgbSplit;
    m[QStringLiteral("sepia")] = c->fx.sepia;
    m[QStringLiteral("invert")] = c->fx.invert;
    m[QStringLiteral("mirrorH")] = c->fx.mirrorH;
    m[QStringLiteral("mirrorV")] = c->fx.mirrorV;

    m[QStringLiteral("text")] = qstr(c->title.text);
    m[QStringLiteral("titleSize")] = c->title.size;
    m[QStringLiteral("titleColor")] = argb(c->title.color);
    m[QStringLiteral("titleBold")] = c->title.bold;
    m[QStringLiteral("titleAlign")] = c->title.align;
    m[QStringLiteral("titlePosY")] = c->title.posY;
    m[QStringLiteral("titleBox")] = c->title.box;
    m[QStringLiteral("titleShadow")] = c->title.shadow;

    m[QStringLiteral("instrument")] = static_cast<int>(track->instrument);
    for (int i = 0; i < static_cast<int>(m_project.tracks.size()); ++i)
        if (&m_project.tracks[static_cast<std::size_t>(i)] == track) m[QStringLiteral("trackIndex")] = i;
    m[QStringLiteral("hasKeys")] = c->hasKeys();
    m[QStringLiteral("hasPrevious")] = m_project.previousAdjacent(*track, *c) != nullptr;
    // Musik-Analyse der Quelle und was davon nach Tempo/Tonhöhe übrig ist
    const bool analyzed = asset && (asset->bpm > 0.0 || asset->key >= 0);
    m[QStringLiteral("analyzed")] = analyzed;
    if (analyzed) {
        m[QStringLiteral("origBpm")] = asset->bpm;
        m[QStringLiteral("origKey")] = asset->key;
        m[QStringLiteral("origMinor")] = asset->keyMinor;
        m[QStringLiteral("origKeyName")] = asset->key >= 0 ? qstr(keyName(asset->key, asset->keyMinor)) : QString();
        const int now = asset->key >= 0 ? ((asset->key + static_cast<int>(std::lround(c->pitch))) % 12 + 12) % 12 : -1;
        m[QStringLiteral("keyNow")] = now;
        m[QStringLiteral("keyNameNow")] = now >= 0 ? qstr(keyName(now, asset->keyMinor)) : QString();
        m[QStringLiteral("bpmNow")] = asset->bpm * c->speed;
    }
    m[QStringLiteral("transition")] = static_cast<int>(c->transition);
    m[QStringLiteral("transitionDuration")] = c->transitionDuration / 1e6;
    m[QStringLiteral("noteCount")] = static_cast<int>(c->notes.size());
    return m;
}

void AppController::setClipProperty(const QString& name, double value) {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    const std::string key = str(name);
    const TimeUs t = m_position;
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;

        if (isKeyframeParam(key)) {
            // Wertebereiche
            double v = value;
            if (key == "opacity") v = std::clamp(v, 0.0, 1.0);
            else if (key == "gainDb") v = std::clamp(v, -60.0, 12.0);
            else if (key == "scale") v = std::clamp(v, 0.01, 10.0);
            else if (key == "x" || key == "y") v = std::clamp(v, -2.0, 2.0);
            else if (key == "rotation") v = std::clamp(v, -360.0, 360.0);
            else if (key == "blur") v = std::clamp(v, 0.0, 0.05);
            const auto it = c->keys.find(key);
            if (it != c->keys.end() && !it->second.empty())
                return p.setKeyframe(id, key, std::clamp(t, c->start, c->end()), v);
            if (key == "opacity") c->opacity = v;
            else if (key == "gainDb") c->gain = fromDb(v);
            else if (key == "scale") c->scale = v;
            else if (key == "x") c->x = v;
            else if (key == "y") c->y = v;
            else if (key == "rotation") c->rotation = v;
            else if (key == "blur") c->fx.blur = v;
            return true;
        }
        if (key == "pitch") c->pitch = std::clamp(value, -12.0, 12.0);
        else if (key == "speed") return p.setClipSpeed(id, value);
        else if (key == "transition") c->transition = static_cast<TransitionType>(std::clamp(static_cast<int>(value), 0, kTransitionCount - 1));
        else if (key == "transitionDuration") c->transitionDuration = static_cast<TimeUs>(std::clamp(value, 0.04, 5.0) * 1e6);
        else if (key == "fadeIn") c->fadeIn = static_cast<TimeUs>(std::clamp(value, 0.0, c->duration / 1e6) * 1e6);
        else if (key == "fadeOut") c->fadeOut = static_cast<TimeUs>(std::clamp(value, 0.0, c->duration / 1e6) * 1e6);
        else if (key == "maskShape") c->mask.shape = static_cast<MaskShape>(std::clamp(static_cast<int>(value), 0, 2));
        else if (key == "maskX") c->mask.cx = std::clamp(value, 0.0, 1.0);
        else if (key == "maskY") c->mask.cy = std::clamp(value, 0.0, 1.0);
        else if (key == "maskW") c->mask.w = std::clamp(value, 0.01, 2.0);
        else if (key == "maskH") c->mask.h = std::clamp(value, 0.01, 2.0);
        else if (key == "maskFeather") c->mask.feather = std::clamp(value, 0.0, 0.5);
        else if (key == "maskInvert") c->mask.invert = value != 0.0;
        else if (key == "exposure") c->color.exposure = std::clamp(value, -4.0, 4.0);
        else if (key == "contrast") c->color.contrast = std::clamp(value, 0.0, 2.0);
        else if (key == "saturation") c->color.saturation = std::clamp(value, 0.0, 2.0);
        else if (key == "temperature") c->color.temperature = std::clamp(value, -1.0, 1.0);
        else if (key == "tint") c->color.tint = std::clamp(value, -1.0, 1.0);
        else if (key == "lift") c->color.lift = std::clamp(value, -0.5, 0.5);
        else if (key == "gamma") c->color.gamma = std::clamp(value, 0.2, 3.0);
        else if (key == "colorGain") c->color.gain = std::clamp(value, 0.0, 2.0);
        else if (key == "lutMix") c->color.lutMix = std::clamp(value, 0.0, 1.0);
        else if (key == "chromaKey") c->fx.chromaKey = value != 0.0;
        else if (key == "keyTolerance") c->fx.keyTolerance = std::clamp(value, 0.0, 1.0);
        else if (key == "keySoftness") c->fx.keySoftness = std::clamp(value, 0.0, 1.0);
        else if (key == "vignette") c->fx.vignette = std::clamp(value, 0.0, 1.0);
        else if (key == "sharpen") c->fx.sharpen = std::clamp(value, 0.0, 1.0);
        else if (key == "grain") c->fx.grain = std::clamp(value, 0.0, 1.0);
        else if (key == "pixelate") c->fx.pixelate = std::clamp(value, 0.0, 1.0);
        else if (key == "glow") c->fx.glow = std::clamp(value, 0.0, 1.0);
        else if (key == "rgbSplit") c->fx.rgbSplit = std::clamp(value, 0.0, 1.0);
        else if (key == "sepia") c->fx.sepia = std::clamp(value, 0.0, 1.0);
        else if (key == "invert") c->fx.invert = value != 0.0;
        else if (key == "mirrorH") c->fx.mirrorH = value != 0.0;
        else if (key == "mirrorV") c->fx.mirrorV = value != 0.0;
        else if (key == "titleSize") c->title.size = std::clamp(value, 0.01, 0.5);
        else if (key == "titleBold") c->title.bold = value != 0.0;
        else if (key == "titleAlign") c->title.align = std::clamp(static_cast<int>(value), 0, 2);
        else if (key == "titlePosY") c->title.posY = std::clamp(value, 0.0, 1.0);
        else if (key == "titleBox") c->title.box = std::clamp(value, 0.0, 1.0);
        else if (key == "titleShadow") c->title.shadow = value != 0.0;
        else return false;
        return true;
    });
}

void AppController::setTitleText(const QString& text) {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c || c->kind != ClipKind::Title || c->title.text == str(text)) return false;
        c->title.text = str(text);
        return true;
    });
}

void AppController::setTitleColor(const QColor& color) {
    if (m_selected.isEmpty() || !color.isValid()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;
        c->title.color = color.rgba();
        return true;
    });
}

void AppController::setKeyColor(const QColor& color) {
    if (m_selected.isEmpty() || !color.isValid()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;
        c->fx.keyR = color.redF();
        c->fx.keyG = color.greenF();
        c->fx.keyB = color.blueF();
        return true;
    });
}

bool AppController::setLut(const QUrl& url) {
    if (m_selected.isEmpty()) return false;
    const QString path = url.isEmpty() ? QString() : url.toLocalFile();
    if (!path.isEmpty() && !validateLut(str(path))) {
        setStatus(tr("Diese LUT-Datei kann nicht gelesen werden (nur 3D-LUTs im .cube-Format)"));
        return false;
    }
    const std::string id = str(m_selected);
    return apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;
        c->color.lutPath = str(path);
        if (!path.isEmpty() && c->color.lutMix <= 0.0) c->color.lutMix = 1.0;
        return true;
    });
}

void AppController::resetColor() {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;
        c->color = ColorGrade{};
        return true;
    });
}

void AppController::toggleKeyframe(const QString& param) {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    const std::string key = str(param);
    const Clip* c = m_project.findClip(id);
    if (!c || !isKeyframeParam(key)) return;
    const TimeUs t = std::clamp(m_position, c->start, c->end());
    const double current = c->valueAt(key, t);
    apply([&](Project& p) {
        if (p.removeKeyframe(id, key, t)) return true;
        return p.setKeyframe(id, key, t, current);
    });
}

void AppController::jumpToKeyframe(int direction) {
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected));
    if (!c) return;
    std::optional<TimeUs> best;
    for (const auto& [name, list] : c->keys)
        for (const auto& k : list) {
            const TimeUs abs = c->start + k.t;
            if (direction > 0 && abs > m_position + 1000 && (!best || abs < *best)) best = abs;
            if (direction < 0 && abs < m_position - 1000 && (!best || abs > *best)) best = abs;
        }
    if (best) {
        pause();
        setPosition(*best);
        emit clipChanged();
    }
}

// --------------------------------------------------------- Wiedergabe


void AppController::setPosition(TimeUs t) {
    t = std::max<TimeUs>(0, t);
    if (t == m_position) return;
    m_position = t;
    emit positionChanged();
    requestFrame();
    // Animierte Werte im Inspector mitlaufen lassen.
    if (!m_selected.isEmpty()) {
        const Clip* c = m_project.findClip(str(m_selected));
        if (c && c->hasKeys()) emit clipChanged();
    }
}

void AppController::startAudio() {
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) return;   // kein Ausgabegerät: stumm weiterspielen
    QAudioFormat format;
    format.setSampleRate(m_project.sampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) {
        setStatus(tr("Das Audiogerät unterstützt %1 Hz Stereo nicht – Wiedergabe ohne Ton")
                      .arg(m_project.sampleRate));
        return;
    }
    m_sink = new QAudioSink(device, format, this);
    // Kleiner Puffer, damit Ton und Bild nah beieinander bleiben.
    m_sink->setBufferSize(static_cast<qsizetype>(m_project.sampleRate / 10) * 8);
    m_mixerDevice.start(m_position * m_project.sampleRate / kSecond);
    m_sink->start(&m_mixerDevice);
}

void AppController::stopAudio() {
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    m_mixerDevice.stop();
}

void AppController::pollLevels() {
    const MeterLevels raw = m_mixer.takeLevels();
    // Anzeige 0..1 über -48..0 dBFS, mit langsamem Abfall.
    auto norm = [](float v) {
        const double db = v > 1e-6f ? 20.0 * std::log10(v) : -100.0;
        return std::clamp((db + 48.0) / 48.0, 0.0, 1.05);
    };
    QVariantMap next;
    for (const auto& t : m_project.tracks) {
        const QString id = qstr(t.id);
        const auto it = raw.tracks.find(t.id);
        const double now = it == raw.tracks.end() ? 0.0 : norm(it->second);
        next[id] = std::max(now, m_levelMap.value(id).toDouble() - 0.04);
    }
    next[QStringLiteral("master")] =
        std::max(norm(raw.master), m_levelMap.value(QStringLiteral("master")).toDouble() - 0.04);
    next[QStringLiteral("reduction")] = raw.reduction;
    m_levelMap = next;
    emit levelsChanged();
    if (!m_playing) {
        bool any = false;
        for (const auto& v : m_levelMap)
            if (v.toDouble() > 0.001) any = true;
        if (!any) m_meterTimer.stop();
    }
}

void AppController::play() {
    // Bei einer Aufnahme darf auch ein leeres Projekt laufen.
    const bool recording = m_recording || m_mic;
    if (m_playing || (m_project.duration() <= 0 && !recording)) return;
    if (!recording && m_position >= m_project.duration()) setPosition(0);
    m_playing = true;
    m_playStart = m_position;
    // Erst das Audiogerät öffnen (kann dauern), dann die Uhr starten –
    // sonst springt das Bild beim Start nach vorn.
    startAudio();
    m_clock.start();
    m_timer.start();
    m_meterTimer.start();
    emit playingChanged();
}

void AppController::pause() {
    if (!m_playing) return;
    m_playing = false;
    m_timer.stop();
    stopAudio();
    emit playingChanged();
    requestFrame();   // angehaltenes Bild in voller Vorschauqualität
}

void AppController::togglePlay() {
    if (m_playing)
        pause();
    else
        play();
}

void AppController::tick() {
    const TimeUs t = m_playStart + m_clock.nsecsElapsed() / 1000;
    const TimeUs end = m_project.duration();
    if (t >= end && !m_recording && !m_mic) {
        setPosition(end);
        pause();
        return;
    }
    setPosition(t);
}

void AppController::seekUs(TimeUs t) {
    const bool wasPlaying = m_playing;
    if (wasPlaying) pause();
    // Über den Bildindex rechnen, damit sich bei 29,97 oder 30 fps keine
    // Rundungsfehler aufsummieren.
    const std::int64_t num = m_project.fpsNum, den = m_project.fpsDen;
    const std::int64_t index = (std::max<TimeUs>(0, t) * num + kSecond * den / 2) / (kSecond * den);
    setPosition((index * kSecond * den + num - 1) / num);
    if (wasPlaying) play();
}

void AppController::seek(double seconds) { seekUs(static_cast<TimeUs>(seconds * 1e6)); }

void AppController::stepFrames(int frames) {
    pause();
    const std::int64_t num = m_project.fpsNum, den = m_project.fpsDen;
    const std::int64_t index = (m_position * num + kSecond * den / 2) / (kSecond * den) + frames;
    seekUs(std::max<std::int64_t>(0, index) * kSecond * den / num + 1);
}

QString AppController::timecode(double seconds) const {
    const double fps = m_project.fps();
    const qint64 totalFrames = static_cast<qint64>(std::floor(std::max(0.0, seconds) * fps + 1e-6));
    const qint64 perSecond = std::max<qint64>(1, static_cast<qint64>(std::lround(fps)));
    const qint64 f = totalFrames % perSecond;
    const qint64 s = totalFrames / perSecond;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

// ------------------------------------------------------------- Export

void AppController::exportVideo(const QUrl& url, int width, int height, int codec, int quality) {
    if (m_exportWorker) return;
    QString path = url.toLocalFile();
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".mp4");
    if (!isPro() && (std::min(width, height) > 1080 || codec == 1)) {
        setStatus(tr("4K und H.265 sind in der Pro-Edition enthalten"));
        return;
    }
    ExportSettings settings;
    settings.path = str(path);
    settings.width = std::max(16, width) & ~1;
    settings.height = std::max(16, height) & ~1;
    settings.codec = codec == 1 ? ExportSettings::Codec::H265 : ExportSettings::Codec::H264;
    settings.quality = std::clamp(quality, 0, 3);
    startExport(settings, ExportWorker::Video);
}

void AppController::startExport(ExportSettings settings, int mode, const QString& stemsExt) {
    if (m_project.duration() <= 0) {
        setStatus(tr("Das Projekt enthält keine Clips"));
        return;
    }
    pause();
    m_exportProgress = 0.0;
    m_exportWorker = new ExportWorker(m_project, settings, this, static_cast<ExportWorker::Mode>(mode), stemsExt);
    connect(m_exportWorker, &ExportWorker::progress, this, [this](double p) {
        m_exportProgress = p;
        emit exportChanged();
    });
    connect(m_exportWorker, &ExportWorker::done, this,
            [this](bool, bool, const QString& message) {
                m_exportWorker->wait();
                m_exportWorker->deleteLater();
                m_exportWorker = nullptr;
                emit exportChanged();
                setStatus(message);
            });
    m_exportWorker->start();
    emit exportChanged();
    setStatus(tr("Export läuft …"));
}

void AppController::cancelExport() {
    if (m_exportWorker) m_exportWorker->cancel();
}
