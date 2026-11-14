#include "AppController.h"

#include <algorithm>
#include <cmath>

#include <QAudioFormat>
#include <QAudioSink>
#include <QFileInfo>
#include <QMediaDevices>

using namespace loggx;

namespace {

std::string str(const QString& s) { return s.toStdString(); }
QString qstr(const std::string& s) { return QString::fromStdString(s); }

double toDb(double gain) { return gain <= 0.001 ? -60.0 : 20.0 * std::log10(gain); }
double fromDb(double db) { return db <= -59.9 ? 0.0 : std::pow(10.0, db / 20.0); }

}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent), m_project(Project::createDefault()), m_mixerDevice(&m_mixer) {
    connect(&m_preview, &PreviewWorker::frameReady, this, &AppController::frameReady);
    m_preview.start();
    m_timer.setInterval(15);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AppController::tick);
    publish();
    m_dirty = false;
}

AppController::~AppController() {
    stopAudio();
    if (m_exportWorker) {
        m_exportWorker->cancel();
        m_exportWorker->wait();
    }
}

// ------------------------------------------------------------ Zustand

template <typename Fn>
bool AppController::apply(Fn&& fn) {
    Project before = m_project;
    if (!fn(m_project)) return false;
    if (m_interactive)
        m_liveChanged = true;
    else
        m_undo.push(std::move(before));
    publish();
    return true;
}

void AppController::publish() {
    m_shared = std::make_shared<const Project>(m_project);
    m_mixer.setProject(m_shared);
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
        .arg(QString::number(m_project.fps(), 'g', 4));
}

void AppController::setProjectName(const QString& name) {
    if (name == projectName() || name.trimmed().isEmpty()) return;
    apply([&](Project& p) {
        p.name = str(name.trimmed());
        return true;
    });
}

// ------------------------------------------------------------ Projekt

void AppController::newProject() {
    resetState();
    m_project = Project::createDefault();
    m_filePath.clear();
    publish();
    setDirty(false);
    emit fileChanged();
    setStatus(tr("Neues Projekt"));
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

void AppController::setProjectFormat(int width, int height, int fps) {
    if (width < 16 || height < 16 || fps < 1 || fps > 240) return;
    apply([&](Project& p) {
        p.width = width & ~1;
        p.height = height & ~1;
        p.fpsNum = fps;
        p.fpsDen = 1;
        return true;
    });
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

bool AppController::addAssetToTimeline(const QString& assetId) {
    const MediaAsset* asset = m_project.findAsset(str(assetId));
    if (!asset) return false;
    // Videos auf die unterste Videospur, Ton auf die oberste Audiospur.
    int index = -1;
    for (int i = 0; i < static_cast<int>(m_project.tracks.size()); ++i) {
        const TrackKind kind = m_project.tracks[static_cast<std::size_t>(i)].kind;
        if (asset->hasVideo && kind == TrackKind::Video) index = i;
        if (!asset->hasVideo && kind == TrackKind::Audio && index < 0) index = i;
    }
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

void AppController::toggleTrackMute(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return;
    apply([&](Project& p) {
        Track& t = p.tracks[static_cast<std::size_t>(trackIndex)];
        t.muted = !t.muted;
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
    const MediaAsset* asset = m_project.findAsset(c->assetId);
    m[QStringLiteral("id")] = qstr(c->id);
    m[QStringLiteral("name")] = asset ? qstr(asset->name) : QString();
    m[QStringLiteral("isVideo")] = track->kind == TrackKind::Video;
    m[QStringLiteral("hasAudio")] = asset && asset->hasAudio;
    m[QStringLiteral("start")] = c->start / 1e6;
    m[QStringLiteral("duration")] = c->duration / 1e6;
    m[QStringLiteral("gainDb")] = toDb(c->gain);
    m[QStringLiteral("pitch")] = c->pitch;
    m[QStringLiteral("speed")] = c->speed;
    m[QStringLiteral("opacity")] = c->opacity;
    m[QStringLiteral("maskShape")] = static_cast<int>(c->mask.shape);
    m[QStringLiteral("maskX")] = c->mask.cx;
    m[QStringLiteral("maskY")] = c->mask.cy;
    m[QStringLiteral("maskW")] = c->mask.w;
    m[QStringLiteral("maskH")] = c->mask.h;
    m[QStringLiteral("maskFeather")] = c->mask.feather;
    m[QStringLiteral("maskInvert")] = c->mask.invert;
    return m;
}

void AppController::setClipProperty(const QString& name, double value) {
    if (m_selected.isEmpty()) return;
    const std::string id = str(m_selected);
    apply([&](Project& p) {
        Clip* c = p.findClip(id);
        if (!c) return false;
        if (name == QLatin1String("gainDb")) c->gain = fromDb(std::clamp(value, -60.0, 12.0));
        else if (name == QLatin1String("pitch")) c->pitch = std::clamp(value, -12.0, 12.0);
        else if (name == QLatin1String("speed")) return p.setClipSpeed(id, value);
        else if (name == QLatin1String("opacity")) c->opacity = std::clamp(value, 0.0, 1.0);
        else if (name == QLatin1String("maskShape"))
            c->mask.shape = static_cast<MaskShape>(std::clamp(static_cast<int>(value), 0, 2));
        else if (name == QLatin1String("maskX")) c->mask.cx = std::clamp(value, 0.0, 1.0);
        else if (name == QLatin1String("maskY")) c->mask.cy = std::clamp(value, 0.0, 1.0);
        else if (name == QLatin1String("maskW")) c->mask.w = std::clamp(value, 0.01, 2.0);
        else if (name == QLatin1String("maskH")) c->mask.h = std::clamp(value, 0.01, 2.0);
        else if (name == QLatin1String("maskFeather")) c->mask.feather = std::clamp(value, 0.0, 0.5);
        else if (name == QLatin1String("maskInvert")) c->mask.invert = value != 0.0;
        else return false;
        return true;
    });
}

// --------------------------------------------------------- Wiedergabe

void AppController::requestFrame() { m_preview.request(m_shared, m_position); }

void AppController::setPosition(TimeUs t) {
    t = std::max<TimeUs>(0, t);
    if (t == m_position) return;
    m_position = t;
    emit positionChanged();
    requestFrame();
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

void AppController::play() {
    if (m_playing || m_project.duration() <= 0) return;
    if (m_position >= m_project.duration()) setPosition(0);
    m_playing = true;
    m_playStart = m_position;
    m_clock.start();
    startAudio();
    m_timer.start();
    emit playingChanged();
}

void AppController::pause() {
    if (!m_playing) return;
    m_playing = false;
    m_timer.stop();
    stopAudio();
    emit playingChanged();
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
    if (t >= end) {
        setPosition(end);
        pause();
        return;
    }
    setPosition(t);
}

void AppController::seekUs(TimeUs t) {
    const bool wasPlaying = m_playing;
    if (wasPlaying) pause();
    // Auf ganze Bilder einrasten, damit Schnitte bildgenau sitzen.
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

void AppController::exportVideo(const QUrl& url, int width, int height) {
    if (m_exportWorker) return;
    QString path = url.toLocalFile();
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".mp4");
    if (m_project.duration() <= 0) {
        setStatus(tr("Das Projekt enthält keine Clips"));
        return;
    }
    pause();

    ExportSettings settings;
    settings.path = str(path);
    settings.width = std::max(16, width) & ~1;
    settings.height = std::max(16, height) & ~1;

    m_exportProgress = 0.0;
    m_exportWorker = new ExportWorker(m_project, settings, this);
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
