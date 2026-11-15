// AppController: Vorschauqualität, Projektformat, Dateien hineinziehen,
// Kopieren/Einfügen, Standbild-Export und Mikrofon-Aufnahme.
#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMediaDevices>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "AppController.h"
#include "MicRecorder.h"
#include "TitleRenderer.h"
#include "loggx/core/Midi.h"

using namespace loggx;

namespace {

QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }

bool isVideoAsset(const MediaAsset& a) { return a.hasVideo || a.isStill; }

// Spur für ein Medium: gewünschte Spur, wenn sie passt, sonst die übliche
// (Bild auf die unterste Videospur, Ton auf die oberste Audiospur); fehlt
// eine passende Spur, wird sie angelegt.
std::string trackFor(Project& p, const MediaAsset& asset, int wanted) {
    const TrackKind kind = isVideoAsset(asset) ? TrackKind::Video : TrackKind::Audio;
    if (wanted >= 0 && wanted < static_cast<int>(p.tracks.size()) &&
        p.tracks[static_cast<std::size_t>(wanted)].kind == kind)
        return p.tracks[static_cast<std::size_t>(wanted)].id;
    std::string found;
    for (const auto& t : p.tracks) {
        if (t.kind != kind) continue;
        found = t.id;
        if (kind == TrackKind::Audio) break;   // oberste Audiospur
    }
    return found.empty() ? p.addTrack(kind) : found;
}

}  // namespace

// ------------------------------------------------------------- Vorschau

void AppController::setPreviewQuality(int quality) {
    quality = std::clamp(quality, 0, 3);
    if (quality == m_previewQuality) return;
    m_previewQuality = quality;
    QSettings().setValue(QStringLiteral("preview/quality"), quality);
    emit adminChanged();
    requestFrame();
}

void AppController::setViewerSize(int width, int height) {
    if (width < 16 || height < 16 || (width == m_viewerW && height == m_viewerH)) return;
    const bool grew = width > m_viewerW || height > m_viewerH;
    m_viewerW = width;
    m_viewerH = height;
    if (grew || m_previewQuality == 0) requestFrame();
}

void AppController::requestFrame() {
    const double pw = std::max(16, m_project.width), ph = std::max(16, m_project.height);
    double scale = 1.0;
    switch (m_previewQuality) {
    case 1: scale = 1.0; break;
    case 2: scale = 0.5; break;
    case 3: scale = 0.25; break;
    default: {
        // So groß wie die Vorschaufläche, nie größer als das Projekt.
        const double vw = m_viewerW > 0 ? m_viewerW : 1280, vh = m_viewerH > 0 ? m_viewerH : 720;
        scale = std::min({vw / pw, vh / ph, 1.0});
        if (m_playing) scale *= m_playScale;
        break;
    }
    }
    const int w = std::max(2, static_cast<int>(std::lround(pw * scale)) & ~1);
    const int h = std::max(2, static_cast<int>(std::lround(ph * scale)) & ~1);
    m_preview.request(m_shared, m_position, w, h);
    const QString size = QStringLiteral("%1×%2").arg(w).arg(h);
    if (size != m_previewSize) {
        m_previewSize = size;
        emit statsChanged();
    }
}

void AppController::setBeatsPerBar(int beats) {
    beats = std::clamp(beats, 1, 12);
    if (beats == m_project.beatsPerBar) return;
    apply([&](Project& p) {
        p.beatsPerBar = beats;
        return true;
    });
}

// -------------------------------------------------- Dateien hineinziehen

QString AppController::dropFiles(const QList<QUrl>& urls, int trackIndex, double seconds) {
    QString projectPath;
    QStringList midiFiles, mediaFiles;
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (path.isEmpty()) continue;
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("loggx"))
            projectPath = path;
        else if (ext == QLatin1String("mid") || ext == QLatin1String("midi"))
            midiFiles << path;
        else if (QFileInfo(path).isFile())
            mediaFiles << path;
    }

    // Erst prüfen (ohne das Projekt anzufassen), dann alles in einem Schritt.
    struct Item {
        QString path;
        std::string existingId;
        MediaAsset asset;
    };
    std::vector<Item> items;
    QStringList failed;
    for (const QString& path : mediaFiles) {
        Item item{path, {}, {}};
        for (const auto& a : m_project.assets)
            if (qstr(a.path) == path) item.existingId = a.id;
        if (item.existingId.empty()) {
            std::string error;
            if (!probeMedia(str(path), item.asset, &error)) {
                failed << QFileInfo(path).fileName();
                continue;
            }
        }
        items.push_back(std::move(item));
    }

    TimeUs at = seconds < 0 ? m_position : static_cast<TimeUs>(seconds * 1e6);
    int imported = 0, placed = 0;
    std::string lastClip;
    if (!items.empty()) {
        apply([&](Project& p) {
            bool changed = false;
            for (auto& item : items) {
                std::string id = item.existingId;
                if (id.empty()) {
                    id = p.addAsset(item.asset);
                    ++imported;
                    changed = true;
                }
                if (trackIndex < -1) continue;
                const MediaAsset* asset = p.findAsset(id);
                if (!asset) continue;
                const std::string trackId = trackFor(p, *asset, trackIndex);
                const std::string clipId = p.addClip(trackId, id, at);
                if (clipId.empty()) continue;
                at = p.findClip(clipId)->end();   // mehrere Dateien hintereinander
                lastClip = clipId;
                ++placed;
                changed = true;
            }
            return changed;
        });
    }
    for (const QString& path : midiFiles) {
        MidiImportResult res;
        std::string error;
        const TimeUs midiAt = at;
        if (apply([&](Project& p) { return importMidi(str(path), p, midiAt, &res, &error); }))
            ++placed;
        else
            failed << QFileInfo(path).fileName();
    }
    if (!lastClip.empty()) setSelectedClipId(qstr(lastClip));

    if (!failed.isEmpty())
        setStatus(tr("Nicht lesbar: %1").arg(failed.join(QStringLiteral(", "))));
    else if (placed > 0)
        setStatus(tr("%1 Datei(en) in die Timeline gelegt").arg(placed));
    else if (imported > 0)
        setStatus(tr("%1 Datei(en) importiert").arg(imported));
    return projectPath;
}

// ------------------------------------------ Kopieren, Einfügen, Standbild

bool AppController::copySelected() {
    const Track* owner = nullptr;
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected), &owner);
    if (!c) {
        setStatus(tr("Zuerst einen Clip auswählen"));
        return false;
    }
    m_clipboard = *c;
    m_clipboardKind = owner->kind;
    emit clipboardChanged();
    setStatus(tr("Clip kopiert"));
    return true;
}

bool AppController::pasteAtPlayhead() {
    if (!m_clipboard) return false;
    // Zielspur: Spur des ausgewählten Clips (wenn passend), sonst die erste passende.
    std::string trackId;
    const Track* owner = nullptr;
    if (!m_selected.isEmpty() && m_project.findClip(str(m_selected), &owner) && owner->kind == m_clipboardKind)
        trackId = owner->id;
    for (const auto& t : m_project.tracks)
        if (trackId.empty() && t.kind == m_clipboardKind) trackId = t.id;
    if (trackId.empty()) {
        setStatus(tr("Keine passende Spur zum Einfügen"));
        return false;
    }
    std::string newId;
    const Clip clip = *m_clipboard;
    const TimeUs at = m_position;
    const bool ok = apply([&](Project& p) {
        newId = p.pasteClip(clip, trackId, at);
        return !newId.empty();
    });
    if (ok) setSelectedClipId(qstr(newId));
    return ok;
}

bool AppController::duplicateSelected() {
    const Track* owner = nullptr;
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected), &owner);
    if (!c) return false;
    const Clip clip = *c;
    const std::string trackId = owner->id;
    std::string newId;
    const bool ok = apply([&](Project& p) {
        newId = p.pasteClip(clip, trackId, clip.end());
        return !newId.empty();
    });
    if (ok) setSelectedClipId(qstr(newId));
    return ok;
}

bool AppController::exportStill(const QUrl& url) {
    QString path = url.toLocalFile();
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");
    Compositor compositor;
    compositor.setTitleRasterizer(qtTitleRasterizer());
    compositor.setProject(m_shared);
    std::vector<std::uint8_t> pixels;
    const int w = m_project.width, h = m_project.height;
    compositor.render(m_position, w, h, pixels);
    QImage image(pixels.data(), w, h, w * 4, QImage::Format_RGBA8888);
    const bool ok = image.convertToFormat(QImage::Format_RGB888).save(path);
    setStatus(ok ? tr("Standbild gespeichert (%1×%2)").arg(w).arg(h) : tr("Standbild konnte nicht gespeichert werden"));
    return ok;
}

// ------------------------------------------------------------- Mikrofon

QStringList AppController::audioInputs() const {
    QStringList list;
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) list << d.description();
    return list;
}

void AppController::setAudioInput(int index) {
    if (index == m_audioInput) return;
    m_audioInput = std::max(0, index);
    QSettings().setValue(QStringLiteral("mic/input"), m_audioInput);
    emit micChanged();
}

bool AppController::startMicRecording() {
    if (m_mic || m_recording) return false;
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (m_audioInput >= 0 && m_audioInput < inputs.size()) device = inputs[m_audioInput];
    if (device.isNull()) {
        setStatus(tr("Kein Mikrofon gefunden"));
        return false;
    }
    // Ordner: neben dem Projekt, sonst im Musikordner.
    QString folder;
    if (!m_filePath.isEmpty())
        folder = QFileInfo(m_filePath).absolutePath() + QLatin1Char('/') + projectName() + tr(" – Aufnahmen");
    else
        folder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/LoggX Aufnahmen");
    if (!QDir().mkpath(folder)) {
        setStatus(tr("Ordner für Aufnahmen kann nicht angelegt werden"));
        return false;
    }
    const QString path = folder + QLatin1Char('/') +
                         tr("Aufnahme %1.wav").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
    auto* mic = new MicRecorder(this);
    QString error;
    if (!mic->start(device, path, &error)) {
        delete mic;
        setStatus(tr("Aufnahme nicht möglich: %1").arg(error));
        return false;
    }
    m_mic = mic;
    m_micStart = m_position;
    auto* meter = new QTimer(mic);
    connect(meter, &QTimer::timeout, this, [this] {
        if (!m_mic) return;
        m_micLevel = m_mic->takePeak();
        emit micLevelChanged();
    });
    meter->start(50);
    emit micChanged();
    play();   // Projekt läuft mit (Kopfhörer verhindern Rückkopplung)
    setStatus(tr("Mikrofon nimmt auf: %1").arg(device.description()));
    return true;
}

void AppController::stopMicRecording() {
    if (!m_mic) return;
    pause();
    const double seconds = m_mic->seconds();
    const QString path = m_mic->stop();
    m_mic->deleteLater();
    m_mic = nullptr;
    m_micLevel = 0.0;
    emit micLevelChanged();
    emit micChanged();
    if (seconds < 0.1) {
        QFile::remove(path);
        setStatus(tr("Aufnahme zu kurz – verworfen"));
        return;
    }
    MediaAsset asset;
    std::string error;
    if (!probeMedia(str(path), asset, &error)) {
        setStatus(tr("Aufnahme gespeichert, aber nicht lesbar: %1").arg(qstr(error)));
        return;
    }
    // Zielspur: ausgewählte Audiospur, sonst die erste Audiospur mit Platz,
    // sonst eine neue Spur „Aufnahme“.
    const Track* owner = nullptr;
    std::string wanted;
    if (!m_selected.isEmpty() && m_project.findClip(str(m_selected), &owner) && owner->kind == TrackKind::Audio)
        wanted = owner->id;
    const TimeUs start = m_micStart;
    std::string clipId;
    apply([&](Project& p) {
        const std::string assetId = p.addAsset(asset);
        const TimeUs duration = p.findAsset(assetId)->duration;
        std::string trackId;
        if (const Track* t = wanted.empty() ? nullptr : p.findTrack(wanted); t && p.rangeFree(*t, start, duration))
            trackId = t->id;
        for (const auto& t : p.tracks)
            if (trackId.empty() && t.kind == TrackKind::Audio && p.rangeFree(t, start, duration)) trackId = t.id;
        if (trackId.empty()) trackId = p.addTrack(TrackKind::Audio, tr("Aufnahme").toStdString());
        clipId = p.addClip(trackId, assetId, start);
        return true;
    });
    if (!clipId.empty()) setSelectedClipId(qstr(clipId));
    setStatus(tr("Aufnahme eingefügt (%1 s)").arg(QString::number(seconds, 'f', 1)));
}

// ------------------------------------------------------------ Spuren

bool AppController::deleteTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return false;
    const QString name = qstr(m_project.tracks[static_cast<std::size_t>(trackIndex)].name);
    const bool ok = apply([&](Project& p) {
        p.tracks.erase(p.tracks.begin() + trackIndex);
        return true;
    });
    if (ok) {
        if (!m_selected.isEmpty() && !m_project.findClip(str(m_selected))) setSelectedClipId(QString());
        setStatus(tr("Spur „%1“ gelöscht (Strg+Z holt sie zurück)").arg(name));
    }
    return ok;
}

bool AppController::renameTrack(int trackIndex, const QString& name) {
    const QString clean = name.trimmed();
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size()) || clean.isEmpty()) return false;
    return apply([&](Project& p) {
        p.tracks[static_cast<std::size_t>(trackIndex)].name = str(clean.left(60));
        return true;
    });
}

bool AppController::moveTrack(int trackIndex, int direction) {
    const int target = trackIndex + (direction < 0 ? -1 : 1);
    const int n = static_cast<int>(m_project.tracks.size());
    if (trackIndex < 0 || trackIndex >= n || target < 0 || target >= n) return false;
    return apply([&](Project& p) {
        std::swap(p.tracks[static_cast<std::size_t>(trackIndex)], p.tracks[static_cast<std::size_t>(target)]);
        return true;
    });
}

bool AppController::cutSelected() {
    if (!copySelected()) return false;
    deleteSelected(false);
    setStatus(tr("Clip ausgeschnitten"));
    return true;
}

bool AppController::openAudioInMusicEditor(const QString& path, double bpm) {
    if (!QFileInfo(path).isFile()) {
        setStatus(tr("Datei nicht gefunden: %1").arg(path));
        return false;
    }
    newFromTemplate(true, 1920, 1080, 30, bpm > 0 ? bpm : 120.0);
    m_project.name = str(QFileInfo(path).completeBaseName());
    dropFiles({QUrl::fromLocalFile(path)}, -1, 0.0);
    // Projekt-Tempo = Tempo des Songs: Raster passt sofort
    setDirty(true);
    emit fileChanged();
    setStatus(tr("„%1“ im Musik-Editor geöffnet").arg(QFileInfo(path).completeBaseName()));
    return true;
}
