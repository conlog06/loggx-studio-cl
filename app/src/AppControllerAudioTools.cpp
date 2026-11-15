// AppController: Musik-Analyse (Tempo, Tonart), Tonart/Tempo ändern,
// Stimme entfernen/isolieren und Song-weite Änderungen.
#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "AppController.h"
#include "ModelManager.h"

#include <QSettings>

using namespace loggx;

namespace {
QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }
}  // namespace

void AppController::runTask(const QString& label, TaskWorker::Job job, std::function<void()> finished) {
    m_workLabel = label;
    m_workProgress = 0.0;
    m_task = new TaskWorker(std::move(job), this);
    connect(m_task, &TaskWorker::progress, this, [this](double p) {
        m_workProgress = p;
        emit workChanged();
    }, Qt::QueuedConnection);
    connect(m_task, &QThread::finished, this, [this, finished] {
        m_task->deleteLater();
        if (finished) finished();
        m_task = nullptr;
        emit workChanged();
    });
    emit workChanged();
    m_task->start();
}

void AppController::cancelWork() {
    if (m_task) m_task->cancel();
    if (m_downloading && m_models) m_models->cancel();
}

bool AppController::aiAvailable() const { return aiSeparationAvailable(); }

void AppController::setSeparationMethod(int method) {
    method = std::clamp(method, 0, 2);
    if (method == m_separationMethod) return;
    m_separationMethod = method;
    QSettings().setValue(QStringLiteral("ai/method"), method);
    emit workChanged();
}

void AppController::ensureModels(const QStringList& ids, std::function<void()> then) {
    for (const QString& id : ids) {
        const StemModelInfo* model = findStemModel(str(id));
        if (!model || m_models->isReady(*model)) continue;
        // Dieses fehlt: laden, danach den Rest prüfen
        m_downloading = true;
        m_workLabel = tr("Lade KI-Modell „%1“ (%2 MB) …").arg(qstr(model->name)).arg(model->bytes / 1000000);
        m_workProgress = 0.0;
        emit workChanged();
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_models, &ModelManager::progress, this, [this](qint64 got, qint64 total) {
            m_workProgress = total > 0 ? static_cast<double>(got) / static_cast<double>(total) : 0.0;
            emit workChanged();
        });
        m_models->download(*model, [this, ids, then, conn](bool ok, const QString& error) {
            disconnect(*conn);
            m_downloading = false;
            emit workChanged();
            if (!ok) {
                setStatus(error);
                return;
            }
            ensureModels(ids, then);   // nächstes fehlendes Modell
        });
        return;
    }
    if (then) then();
}

QStringList AppController::keyNames(bool minor) const {
    QStringList list;
    for (int i = 0; i < 12; ++i) list << qstr(keyName(i, minor));
    return list;
}

bool AppController::analyzeSelected() {
    if (m_task) return false;
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected));
    const MediaAsset* asset = c && c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    if (!asset || !asset->hasAudio) {
        setStatus(tr("Zuerst einen Clip mit Ton auswählen"));
        return false;
    }
    const std::string path = asset->path, assetId = asset->id;
    auto beat = std::make_shared<BeatInfo>();
    auto key = std::make_shared<KeyInfo>();
    runTask(tr("Analysiere Tempo und Tonart …"),
            [path, beat, key](const std::function<void(double)>& progress, const std::atomic<bool>&) {
                *beat = detectBeats(path);
                progress(0.5);
                *key = detectKey(path);
                progress(1.0);
            },
            [this, assetId, beat, key] {
                if (m_task && m_task->cancelled()) return;
                for (auto& a : m_project.assets) {
                    if (a.id != assetId) continue;
                    a.bpm = beat->bpm;
                    a.firstBeat = beat->firstBeat;
                    a.key = key->tonic;
                    a.keyMinor = key->minor;
                }
                publish();   // ohne Undo-Schritt: nur Messwerte
                setDirty(true);
                QStringList parts;
                if (beat->bpm > 0) parts << tr("%1 BPM").arg(QString::number(beat->bpm, 'f', 1));
                if (key->tonic >= 0) parts << qstr(keyName(key->tonic, key->minor));
                setStatus(parts.isEmpty() ? tr("Weder Takt noch Tonart sicher erkannt")
                                          : tr("Erkannt: %1").arg(parts.join(QStringLiteral(" · "))));
            });
    return true;
}

void AppController::setClipKey(int tonic) {
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected));
    const MediaAsset* asset = c && c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    if (!asset || asset->key < 0 || tonic < 0 || tonic > 11) return;
    // Kürzester Weg: −6 … +5 Halbtöne
    int diff = ((tonic - asset->key) % 12 + 12) % 12;
    if (diff > 5) diff -= 12;
    setClipProperty(QStringLiteral("pitch"), diff);
    setStatus(tr("Tonart: %1 (%2%3 Halbtöne)").arg(qstr(keyName(tonic, asset->keyMinor))).arg(diff > 0 ? QStringLiteral("+") : QString()).arg(diff));
}

void AppController::setClipBpm(double bpm) {
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected));
    const MediaAsset* asset = c && c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    if (!asset || asset->bpm <= 0.0 || bpm <= 0.0) return;
    const double speed = bpm / asset->bpm;
    if (speed < 0.25 || speed > 4.0) {
        setStatus(tr("So weit lässt sich das Tempo nicht ändern (0,25× bis 4×)"));
        return;
    }
    const std::string id = c->id;
    if (!apply([&](Project& p) { return p.setClipSpeed(id, speed); }))
        setStatus(tr("Kein Platz: der Clip würde in den nächsten hineinragen"));
    else
        setStatus(tr("Tempo: %1 BPM (Tonhöhe bleibt)").arg(QString::number(bpm, 'f', 1)));
}

void AppController::matchClipTempo() { setClipBpm(m_project.bpm); }

bool AppController::separateSelected(bool keepVocals, int method) {
    if (working()) return false;
    if (method < 0) method = m_separationMethod;
    if (method < 2 && !aiSeparationAvailable()) {
        setStatus(tr("Diese Version hat keine KI eingebaut – Trennung ohne KI"));
        method = 2;
    }
    const Track* owner = nullptr;
    const Clip* c = m_selected.isEmpty() ? nullptr : m_project.findClip(str(m_selected), &owner);
    const MediaAsset* asset = c && c->kind == ClipKind::Media ? m_project.findAsset(c->assetId) : nullptr;
    if (!asset || !asset->hasAudio) {
        setStatus(tr("Zuerst einen Clip mit Musik auswählen"));
        return false;
    }
    // Ziel: neben dem Projekt, sonst im Musikordner
    QString folder = m_filePath.isEmpty()
                         ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/LoggX Stems")
                         : QFileInfo(m_filePath).absolutePath() + QLatin1Char('/') + projectName() + tr(" – Stems");
    if (!QDir().mkpath(folder)) {
        setStatus(tr("Ordner kann nicht angelegt werden: %1").arg(folder));
        return false;
    }
    const QString base = folder + QLatin1Char('/') + QFileInfo(qstr(asset->path)).completeBaseName();
    const QString vocalsPath = base + tr(" – Gesang.wav");
    const QString instPath = base + tr(" – Instrumental.wav");
    const std::string input = asset->path;
    const int rate = m_project.sampleRate;
    const Clip source = *c;
    auto result = std::make_shared<SeparationResult>();
    const StemModelInfo* model = method < 2 ? findStemModel(method == 0 ? "vocals-hq" : "vocals") : nullptr;
    const QString modelPath = model ? m_models->pathFor(*model) : QString();
    const QString label = (keepVocals ? tr("Isoliere Gesang") : tr("Entferne Gesang")) + (model ? tr(" mit KI …") : QStringLiteral(" …"));
    const QStringList needed = model ? QStringList{qstr(model->id)} : QStringList{};
    ensureModels(needed, [=, this] {
    runTask(label,
            [=](const std::function<void(double)>& progress, const std::atomic<bool>& cancel) {
                if (model)
                    *result = separateStemsAi(input, {StemJob{model, str(modelPath), str(vocalsPath)}}, str(instPath), false,
                                              progress, &cancel);
                else
                    *result = separateVocals(input, str(vocalsPath), str(instPath), rate, progress, &cancel);
            },
            [=, this] {
                if (!result->ok) {
                    setStatus(result->error.empty() ? tr("Trennung fehlgeschlagen") : qstr(result->error));
                    return;
                }
                MediaAsset va, ia;
                if (!probeMedia(str(vocalsPath), va) || !probeMedia(str(instPath), ia)) {
                    setStatus(tr("Trennung fertig, aber die Dateien sind nicht lesbar"));
                    return;
                }
                std::string newSelection;
                apply([&](Project& p) {
                    // Zwei neue Spuren mit Clips an exakt derselben Stelle wie das Original.
                    auto place = [&](MediaAsset a, const QString& trackName, bool muted) {
                        const std::string assetId = p.addAsset(std::move(a));
                        const std::string trackId = p.addTrack(TrackKind::Audio, str(trackName));
                        if (Track* t = p.findTrack(trackId)) t->muted = muted;
                        Clip copy = source;
                        copy.assetId = assetId;
                        copy.gain = source.gain > 0.0 ? source.gain : 1.0;
                        copy.keys.erase("gainDb");
                        return p.pasteClip(copy, trackId, source.start);
                    };
                    const std::string inst = place(ia, tr("Instrumental"), keepVocals);
                    const std::string voc = place(va, tr("Gesang"), !keepVocals);
                    newSelection = keepVocals ? voc : inst;
                    // Original verstummen lassen (Bild bleibt)
                    if (Clip* orig = p.findClip(source.id)) orig->gain = 0.0;
                    return true;
                });
                if (!newSelection.empty()) setSelectedClipId(qstr(newSelection));
                setStatus(keepVocals ? tr("Gesang isoliert – Instrumental liegt stumm darunter (Gesangsanteil %1 %)")
                                           .arg(std::lround(result->vocalShare * 100))
                                     : tr("Gesang entfernt – die Gesangsspur liegt stumm darunter (Gesangsanteil %1 %)")
                                           .arg(std::lround(result->vocalShare * 100)));
            });
    });
    return true;
}

bool AppController::setSongTempo(double bpm) {
    const double old = m_project.bpm;
    const bool ok = apply([&](Project& p) { return p.retime(bpm); });
    setStatus(ok ? tr("Song-Tempo: %1 → %2 BPM").arg(QString::number(old, 'f', 1), QString::number(bpm, 'f', 1))
                 : tr("Dieses Tempo geht nicht: ein Audio-Clip würde schneller als 4× oder langsamer als 0,25×"));
    return ok;
}

int AppController::transposeSong(int semitones) {
    int changed = 0;
    apply([&](Project& p) {
        changed = p.transposeAll(semitones);
        return changed > 0;
    });
    setStatus(changed > 0 ? tr("Song um %1%2 Halbtöne transponiert (%3 Clips, Schlagzeug bleibt)")
                                .arg(semitones > 0 ? QStringLiteral("+") : QString())
                                .arg(semitones)
                                .arg(changed)
                          : tr("Nichts zu transponieren"));
    return changed;
}
