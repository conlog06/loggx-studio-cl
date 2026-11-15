// AppController: Startbildschirm, Musikprojekte, Beat-Raster, Musik-Export,
// Live-Spielen mit Aufnahme, MIDI-Geräte, Lizenz und Admin.
#include <algorithm>
#include <cmath>
#include <thread>

#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSettings>
#include <QSysInfo>

#include "AppController.h"
#include "loggx/core/Midi.h"

using namespace loggx;

namespace {

QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }

#ifndef LOGGX_LICENSE_PUBKEY
#define LOGGX_LICENSE_PUBKEY ""
#endif

bool compiledPublicKey(PublicKey& key) {
    const std::string hex = LOGGX_LICENSE_PUBKEY;
    return !hex.empty() && fromHex(hex, key.data(), key.size());
}

}  // namespace

// ------------------------------------------------- Start und Projekte

void AppController::setShowStart(bool show) {
    if (show == m_showStart) return;
    if (show) pause();
    m_showStart = show;
    emit startChanged();
}

QVariantList AppController::recentProjects() const {
    QVariantList list;
    const QStringList paths = QSettings().value(QStringLiteral("recent")).toStringList();
    for (const QString& path : paths) {
        const QFileInfo info(path);
        QVariantMap m;
        m[QStringLiteral("path")] = path;
        m[QStringLiteral("name")] = info.completeBaseName();
        m[QStringLiteral("folder")] = QDir::toNativeSeparators(info.absolutePath());
        m[QStringLiteral("exists")] = info.exists();
        m[QStringLiteral("date")] = info.exists() ? info.lastModified().toString(QStringLiteral("dd.MM.yyyy · HH:mm")) : QString();
        list.append(m);
    }
    return list;
}

void AppController::addRecent(const QString& path) {
    if (path.isEmpty()) return;
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("recent")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    list.removeAll(abs);
    list.prepend(abs);
    while (list.size() > 12) list.removeLast();
    settings.setValue(QStringLiteral("recent"), list);
    emit recentChanged();
}

void AppController::forgetRecent(const QString& path) {
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("recent")).toStringList();
    list.removeAll(path);
    settings.setValue(QStringLiteral("recent"), list);
    emit recentChanged();
}

void AppController::newMusicProject() { newFromTemplate(true, 1920, 1080, 30, 120); }

void AppController::newFromTemplate(bool music, int width, int height, double fps, double bpm) {
    resetState();
    m_project = music ? Project::createMusic() : Project::createDefault();
    m_project.width = std::max(16, width) & ~1;
    m_project.height = std::max(16, height) & ~1;
    fps = std::clamp(fps, 1.0, 240.0);
    if (std::abs(fps - std::round(fps)) < 1e-6) {
        m_project.fpsNum = static_cast<int>(std::lround(fps));
        m_project.fpsDen = 1;
    } else {   // NTSC-Raten: 23,976 = 24000/1001 usw.
        m_project.fpsNum = static_cast<int>(std::lround(fps * 1.001)) * 1000;
        m_project.fpsDen = 1001;
    }
    m_project.bpm = std::clamp(bpm, 20.0, 300.0);
    m_filePath.clear();
    m_beatGrid = music;
    emit gridChanged();
    publish();
    setDirty(false);
    removeAutosave();
    emit fileChanged();
    setShowStart(false);
    setStatus(music ? tr("Neues Musikprojekt – %1 BPM").arg(m_project.bpm) : tr("Neues Videoprojekt – %1").arg(formatInfo()));
}

// ---------------------------------------------- Beat-Raster, Übergänge

void AppController::setBeatGrid(bool on) {
    if (on == m_beatGrid) return;
    m_beatGrid = on;
    emit gridChanged();
}

QStringList AppController::transitionNames() const {
    QStringList list;
    for (int i = 0; i < kTransitionCount; ++i) list << QString::fromUtf8(transitionName(static_cast<TransitionType>(i)));
    return list;
}

bool AppController::detectTempo() {
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected));
    const MediaAsset* asset = c && c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    if (!asset || !asset->hasAudio) {
        setStatus(tr("Zuerst einen Clip mit Musik auswählen"));
        return false;
    }
    const BeatInfo info = detectBeats(asset->path);
    if (info.bpm <= 0.0) {
        setStatus(tr("Kein gleichmäßiger Takt gefunden"));
        return false;
    }
    // Tempo und ersten Schlag auf die Timeline umrechnen (Clip-Tempo beachten).
    const double bpm = std::clamp(info.bpm * c->speed, 20.0, 300.0);
    const TimeUs beat = static_cast<TimeUs>(60.0 / bpm * kSecond);
    TimeUs first = c->start + static_cast<TimeUs>(static_cast<double>(info.firstBeat - c->in) / c->speed);
    first = ((first % beat) + beat) % beat;
    apply([&](Project& p) {
        p.bpm = bpm;
        p.beatOffset = first;
        return true;
    });
    m_beatGrid = true;
    emit gridChanged();
    setStatus(tr("Tempo erkannt: %1 BPM (Sicherheit %2 %)")
                  .arg(QString::number(bpm, 'f', bpm == std::round(bpm) ? 0 : 1))
                  .arg(std::lround(info.confidence * 100)));
    return true;
}

int AppController::cutOnBeats(int everyBeats) {
    if (m_selected.isEmpty()) {
        setStatus(tr("Zuerst einen Clip auswählen"));
        return 0;
    }
    int cuts = 0;
    apply([&](Project& p) {
        cuts = p.cutOnBeats(str(m_selected), everyBeats);
        return cuts > 0;
    });
    setStatus(cuts > 0 ? tr("%1 Schnitte auf den Beat").arg(cuts) : tr("Im Clip liegt kein passender Schlag"));
    return cuts;
}

// ------------------------------------------------------ Musik-Dateien

void AppController::exportAudio(const QUrl& url, int bits, bool tail) {
    if (m_exportWorker) return;
    QString path = url.toLocalFile();
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".wav");
    ExportSettings s;
    s.path = str(path);
    s.audioBits = bits;
    s.video = false;
    s.audioTail = tail;
    startExport(s, ExportWorker::Audio);
}

void AppController::exportStems(const QUrl& folder, const QString& extension, int bits) {
    if (m_exportWorker) return;
    const QString dir = folder.toLocalFile();
    if (dir.isEmpty()) return;
    ExportSettings s;
    s.path = str(QDir(dir).filePath(projectName() + QStringLiteral(" – Stems")));
    s.audioBits = bits;
    startExport(s, ExportWorker::Stems, extension.isEmpty() ? QStringLiteral("wav") : extension);
}

bool AppController::exportMidiFile(const QUrl& url) {
    QString path = url.toLocalFile();
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".mid");
    std::string error;
    const bool ok = exportMidi(m_project, str(path), &error);
    setStatus(ok ? tr("MIDI-Datei gespeichert") : tr("MIDI-Export fehlgeschlagen: %1").arg(qstr(error)));
    return ok;
}

bool AppController::importMidiFile(const QUrl& url) {
    MidiImportResult res;
    std::string error;
    const TimeUs at = m_position;
    const bool ok = apply([&](Project& p) { return importMidi(str(url.toLocalFile()), p, at, &res, &error); });
    if (ok)
        setStatus(tr("MIDI importiert: %1 Spur(en), %2 Noten (Datei: %3 BPM)")
                      .arg(res.tracks)
                      .arg(res.notes)
                      .arg(QString::number(res.fileBpm, 'f', 0)));
    else
        setStatus(tr("MIDI-Import fehlgeschlagen: %1").arg(qstr(error)));
    return ok;
}

// ------------------------------------------------------- Live spielen

InstrumentType AppController::liveInstrument() const {
    const Track* owner = nullptr;
    if (!m_recordTrack.isEmpty())
        if (const Track* t = m_project.findTrack(str(m_recordTrack))) return t->instrument;
    if (!m_selected.isEmpty() && m_project.findClip(str(m_selected), &owner) && owner->kind == TrackKind::Instrument)
        return owner->instrument;
    for (const auto& t : m_project.tracks)
        if (t.kind == TrackKind::Instrument) return t.instrument;
    return InstrumentType::Piano;
}

void AppController::ensureLiveAudio() {
    if (m_liveSink) return;
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) return;
    QAudioFormat format;
    format.setSampleRate(m_project.sampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) return;
    m_live.setSampleRate(m_project.sampleRate);
    m_liveSink = new QAudioSink(device, format, this);
    // ca. 25 ms Puffer: kurze Verzögerung zwischen Taste und Ton
    m_liveSink->setBufferSize(static_cast<qsizetype>(m_project.sampleRate / 40) * 8);
    if (!m_liveDevice.isOpen()) m_liveDevice.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    m_liveSink->start(&m_liveDevice);
}

TimeUs AppController::clockPosition() const {
    return m_playing ? m_playStart + m_clock.nsecsElapsed() / 1000 : m_position;
}

void AppController::liveNoteOn(int pitch, double velocity) {
    pitch = std::clamp(pitch, 0, 127);
    if (m_held.contains(pitch)) return;
    ensureLiveAudio();
    m_live.noteOn(pitch, std::clamp(velocity, 0.05, 1.0), liveInstrument());
    m_held.append(pitch);
    if (m_recording && m_playing) m_recorded.push_back({pitch, velocity, clockPosition(), -1});
    emit liveChanged();
}

void AppController::liveNoteOff(int pitch) {
    m_live.noteOff(pitch);
    m_held.removeAll(pitch);
    if (m_recording)
        for (auto& n : m_recorded)
            if (n.pitch == pitch && n.off < 0) n.off = clockPosition();
    emit liveChanged();
}

void AppController::allNotesOff() {
    m_live.allNotesOff();
    for (int p : m_held) liveNoteOff(p);
    m_held.clear();
    emit liveChanged();
}

QVariantList AppController::heldNotes() const {
    QVariantList list;
    for (int p : m_held) list.append(p);
    return list;
}

void AppController::setKeyboardOctave(int octave) {
    octave = std::clamp(octave, 0, 8);
    if (octave == m_octave) return;
    allNotesOff();
    m_octave = octave;
    emit liveChanged();
}

void AppController::setQuantizeRecording(bool on) {
    m_quantize = on;
    emit liveChanged();
}

void AppController::startRecording() {
    if (m_recording) return;
    // Zielspur: Spur des ausgewählten MIDI-Clips, sonst erste Instrumentspur.
    const Track* owner = nullptr;
    QString track;
    if (!m_selected.isEmpty() && m_project.findClip(str(m_selected), &owner) && owner->kind == TrackKind::Instrument)
        track = qstr(owner->id);
    if (track.isEmpty()) {
        int index = trackIndexOf(TrackKind::Instrument, false);
        if (index < 0) {
            addInstrumentTrack();
            index = trackIndexOf(TrackKind::Instrument, false);
        }
        track = qstr(m_project.tracks[static_cast<std::size_t>(index)].id);
    }
    m_recordTrack = track;
    m_recordStart = m_position;
    m_recorded.clear();
    m_recording = true;
    emit recordingChanged();
    play();
    setStatus(tr("Aufnahme läuft – spielen auf der Tastatur"));
}

void AppController::stopRecording() {
    if (!m_recording) return;
    const TimeUs end = clockPosition();
    pause();
    m_recording = false;
    emit recordingChanged();
    for (auto& n : m_recorded)
        if (n.off < 0) n.off = end;
    std::vector<RecordedNote> notes = m_recorded;
    m_recorded.clear();
    const std::string trackId = str(m_recordTrack);
    m_recordTrack.clear();
    if (notes.empty()) {
        setStatus(tr("Aufnahme beendet – keine Noten gespielt"));
        return;
    }
    const TimeUs beat = m_project.beatDuration();
    const TimeUs step = beat / 4;   // Sechzehntel
    auto q = [&](TimeUs t) {
        if (!m_quantize) return t;
        const TimeUs rel = t - m_project.beatOffset;
        return m_project.beatOffset + static_cast<TimeUs>(std::llround(static_cast<double>(rel) / step)) * step;
    };
    TimeUs first = INT64_MAX, last = 0;
    for (auto& n : notes) {
        n.on = std::max<TimeUs>(0, q(n.on));
        n.off = std::max(n.on + (m_quantize ? step : 20'000), q(n.off));
        first = std::min(first, n.on);
        last = std::max(last, n.off);
    }
    const QString selected = m_selected;
    std::string newClip;
    apply([&](Project& p) {
        Clip* target = nullptr;
        if (Clip* c = p.findClip(str(selected)); c && c->kind == ClipKind::Midi) {
            const Track* owner = nullptr;
            p.findClip(c->id, &owner);
            if (owner && owner->id == trackId && first >= c->start && first < c->end()) target = c;
        }
        if (!target) {
            const TimeUs bar = beat * p.beatsPerBar;
            const TimeUs start = std::max<TimeUs>(0, (first - p.beatOffset) / bar * bar + p.beatOffset);
            const TimeUs length = std::max(bar, ((last - start) + bar - 1) / bar * bar);
            newClip = p.addMidiClip(trackId, start, length);
            target = p.findClip(newClip);
            if (!target) return false;
        } else if (last > target->end()) {
            p.trimClipEnd(target->id, last);
            target = p.findClip(target->id);
        }
        for (const auto& n : notes) {
            if (n.on < target->start || n.on >= target->end()) continue;
            target->notes.push_back({n.pitch, n.on - target->start + target->in, n.off - n.on, n.velocity});
        }
        return true;
    });
    if (!newClip.empty()) setSelectedClipId(qstr(newClip));
    setStatus(tr("%1 Noten aufgenommen").arg(notes.size()));
}

void AppController::refreshMidiInputs() {
    m_midiPorts = m_midi->ports();
    emit liveChanged();
}

bool AppController::openMidiInput(int index) {
    if (index < 0) {
        m_midi->close();
        emit liveChanged();
        return true;
    }
    QString error;
    const bool ok = m_midi->open(index, [this](int s, int a, int b) { emit midiMessage(s, a, b); }, &error);
    setStatus(ok ? tr("MIDI-Gerät verbunden: %1").arg(m_midiPorts.value(index)) : tr("MIDI-Gerät: %1").arg(error));
    if (ok) ensureLiveAudio();
    emit liveChanged();
    return ok;
}

int AppController::midiInput() const { return m_midi ? m_midi->openIndex() : -1; }
bool AppController::midiAvailable() const { return MidiInput::available(); }

void AppController::handleMidi(int status, int data1, int data2) {
    const int kind = status & 0xF0;
    if (kind == 0x90 && data2 > 0)
        liveNoteOn(data1, data2 / 127.0);
    else if (kind == 0x80 || (kind == 0x90 && data2 == 0))
        liveNoteOff(data1);
    else if (kind == 0xB0 && (data1 == 123 || data1 == 120))   // Alle Noten aus
        allNotesOff();
}

// ------------------------------------------------------ Lizenz, Admin

void AppController::loadLicense() {
    QSettings settings;
    m_simulateFree = settings.value(QStringLiteral("admin/simulateFree"), false).toBool();
    m_previewQuality = std::clamp(settings.value(QStringLiteral("preview/quality"), 0).toInt(), 0, 3);
    m_audioInput = std::max(0, settings.value(QStringLiteral("mic/input"), 0).toInt());
    m_separationMethod = std::clamp(settings.value(QStringLiteral("ai/method"), 0).toInt(), 0, 2);
    PublicKey pub;
    if (!compiledPublicKey(pub)) {
        m_editionLicensed = Edition::Admin;   // Entwicklerversion
        m_licenseHolder = tr("Entwicklerversion");
        return;
    }
    m_editionLicensed = Edition::Free;
    m_licenseHolder.clear();
    const QString key = settings.value(QStringLiteral("license/key")).toString();
    if (key.isEmpty()) return;
    License l;
    std::string error;
    if (verifyLicense(str(key), pub, l, &error, str(QDate::currentDate().toString(Qt::ISODate)))) {
        m_editionLicensed = l.edition;
        m_licenseHolder = qstr(l.name);
    } else {
        m_licenseError = qstr(error);
    }
}

bool AppController::licensingActive() const {
    PublicKey pub;
    return compiledPublicKey(pub);
}

QString AppController::edition() const {
    if (m_simulateFree) return QStringLiteral("free");
    if (!licensingActive()) return QStringLiteral("dev");
    return QString::fromLatin1(editionId(m_editionLicensed));
}

QString AppController::editionName() const {
    QString name = !licensingActive() ? tr("Entwicklerversion") : QString::fromLatin1(loggx::editionName(m_editionLicensed));
    if (m_simulateFree) name += tr(" (Free simuliert)");
    return name;
}

bool AppController::isPro() const { return !m_simulateFree && m_editionLicensed != Edition::Free; }
bool AppController::isAdmin() const { return m_editionLicensed == Edition::Admin; }

void AppController::setSimulateFree(bool on) {
    if (!isAdmin() || on == m_simulateFree) return;
    m_simulateFree = on;
    QSettings().setValue(QStringLiteral("admin/simulateFree"), on);
    emit licenseChanged();
}

bool AppController::activateLicense(const QString& key) {
    PublicKey pub;
    if (!compiledPublicKey(pub)) {
        m_licenseError = tr("Diese Entwicklerversion braucht keine Lizenz");
        return false;
    }
    License l;
    std::string error;
    if (!verifyLicense(str(key), pub, l, &error, str(QDate::currentDate().toString(Qt::ISODate)))) {
        m_licenseError = qstr(error);
        emit licenseChanged();
        return false;
    }
    QSettings().setValue(QStringLiteral("license/key"), key.trimmed());
    m_editionLicensed = l.edition;
    m_licenseHolder = qstr(l.name);
    m_licenseError.clear();
    emit licenseChanged();
    setStatus(tr("Lizenz aktiviert: %1").arg(QString::fromLatin1(loggx::editionName(l.edition))));
    return true;
}

void AppController::removeLicense() {
    QSettings().remove(QStringLiteral("license/key"));
    loadLicense();
    emit licenseChanged();
}

QVariantMap AppController::systemInfo() const {
    QVariantMap m;
    m[QStringLiteral("version")] = QCoreApplication::applicationVersion();
    m[QStringLiteral("qt")] = QString::fromLatin1(qVersion());
    m[QStringLiteral("ffmpeg")] = qstr(ffmpegVersion());
    m[QStringLiteral("system")] = QSysInfo::prettyProductName() + QStringLiteral(" · ") + QSysInfo::currentCpuArchitecture();
    m[QStringLiteral("threads")] = static_cast<int>(std::thread::hardware_concurrency());
    m[QStringLiteral("rubberband")] = AudioMixer::hasStretcher();
    m[QStringLiteral("rtmidi")] = MidiInput::available();
    QStringList enc;
    for (const auto& e : availableEncoders()) enc << qstr(e);
    m[QStringLiteral("encoders")] = enc.join(QStringLiteral(", "));
    m[QStringLiteral("settings")] = QSettings().fileName();
    m[QStringLiteral("autosave")] = autosaveDir();
    return m;
}
