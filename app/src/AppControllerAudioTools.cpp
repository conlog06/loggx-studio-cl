// AppController: Musik-Analyse (Tempo, Tonart), Tonart/Tempo ändern,
// Stimme entfernen/isolieren und Song-weite Änderungen.
#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "AppController.h"

#include <QFile>

#include <QTimer>
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
    // Läuft schon ein Download (z. B. automatisch im Hintergrund): danach weiter
    if (m_models->busy()) {
        QTimer::singleShot(1000, this, [this, ids, then] { ensureModels(ids, then); });
        return;
    }
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
            emit modelsChanged();
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

// ------------------------------------------------------------ KI-Modelle

QVariantList AppController::aiModels() const {
    QVariantList list;
    for (const StemModelInfo& m : stemModels()) {
        list << QVariantMap{{QStringLiteral("id"), qstr(m.id)},
                            {QStringLiteral("name"), qstr(m.name)},
                            {QStringLiteral("stem"), qstr(m.stem)},
                            {QStringLiteral("mb"), static_cast<int>(m.bytes / 1000000)},
                            {QStringLiteral("ready"), m_models->isReady(m)},
                            {QStringLiteral("bundled"), !m_models->bundledPath(m).isEmpty()}};
    }
    return list;
}

QString AppController::aiModelFolder() const { return m_models->folder(); }

void AppController::setAiAutoDownload(bool on) {
    m_aiAutoDownload = on;
    QSettings().setValue(QStringLiteral("ai/autoDownload"), on);
    emit modelsChanged();
    if (on) downloadAllModels();
}

void AppController::downloadAllModels() {
    if (!aiAvailable()) return;
    QStringList ids;
    for (const StemModelInfo& m : stemModels())
        if (!m_models->isReady(m) && m.id != "cutout-human") ids << qstr(m.id);   // 176 MB nur bei Bedarf
    if (ids.isEmpty()) {
        setStatus(tr("Alle KI-Modelle sind bereit"));
        return;
    }
    ensureModels(ids, [this] {
        setStatus(tr("Alle KI-Modelle geladen – Stems und Vocal Remover laufen jetzt offline"));
        emit modelsChanged();
    });
}

void AppController::deleteModel(const QString& id) {
    const StemModelInfo* model = findStemModel(str(id));
    if (!model) return;
    QFile::remove(m_models->pathFor(*model));
    emit modelsChanged();
}

// ------------------------------------------------------------ Allgemein

int AppController::startMode() const { return QSettings().value(QStringLiteral("general/startMode"), 0).toInt(); }

void AppController::setStartMode(int mode) {
    QSettings().setValue(QStringLiteral("general/startMode"), std::clamp(mode, 0, 2));
    emit generalChanged();
}

void AppController::setAutosaveMinutes(int minutes) {
    m_autosaveMinutes = std::clamp(minutes, 0, 30);
    QSettings().setValue(QStringLiteral("general/autosaveMinutes"), m_autosaveMinutes);
    m_autosaveTimer.stop();
    if (m_autosaveMinutes > 0) {
        m_autosaveTimer.setInterval(m_autosaveMinutes * 60'000);
        m_autosaveTimer.start();
    }
    emit generalChanged();
}

void AppController::clearRecent() {
    QSettings().remove(QStringLiteral("recent"));
    emit recentChanged();
}

// ------------------------------------------------------------ Masken-Tracking

bool AppController::trackSelectedMask() {
    if (m_task) return false;
    const Clip* c = m_project.findClip(str(m_selected));
    if (!c || c->mask.shape == MaskShape::None) {
        setStatus(tr("Erst eine Maske auf den Clip legen"));
        return false;
    }
    const std::string id = c->id;
    const TimeUs from = std::clamp(m_position, c->start, c->end() - 1);
    auto snapshot = std::make_shared<const Project>(m_project);
    auto result = std::make_shared<MaskTrackResult>();
    runTask(tr("Verfolge Maske …"),
            [snapshot, id, from, result](const std::function<void(double)>& progress, const std::atomic<bool>& cancel) {
                *result = trackMask(*snapshot, id, from, [&](double p) {
                    progress(p);
                    return !cancel.load();
                });
            },
            [this, id, from, result] {
                if (!result->error.empty()) {
                    setStatus(qstr(result->error));
                    return;
                }
                if (result->x.size() < 2) {
                    setStatus(tr("Nichts zu verfolgen"));
                    return;
                }
                apply([&](Project& p) {
                    Clip* clip = p.findClip(id);
                    if (!clip) return false;
                    // Keyframes ab dem Startpunkt ersetzen, davor behalten
                    const TimeUs rel = from - clip->start;
                    for (const char* param : {"maskX", "maskY"}) {
                        auto& keys = clip->keys[param];
                        keys.erase(std::remove_if(keys.begin(), keys.end(), [rel](const Keyframe& k) { return k.t >= rel; }), keys.end());
                        const auto& add = std::string(param) == "maskX" ? result->x : result->y;
                        keys.insert(keys.end(), add.begin(), add.end());
                        std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
                    }
                    return true;
                });
                setStatus(result->lost ? tr("Maske über %1 Bilder verfolgt – danach verloren (verdeckt oder Schnitt)").arg(result->frames)
                                       : tr("Maske über %1 Bilder verfolgt").arg(result->frames));
            });
    return true;
}

// ------------------------------------------------------------ KI-Freistellung

int AppController::cutoutQuality() const { return QSettings().value(QStringLiteral("ai/cutoutQuality"), 0).toInt(); }

void AppController::updateCutoutModel() {
    // Gewähltes Modell, sonst das schnelle (wenn vorhanden)
    const StemModelInfo* want = findStemModel(cutoutQuality() == 1 ? "cutout-human" : "cutout");
    const StemModelInfo* fast = findStemModel("cutout");
    std::string path;
    if (want && m_models->isReady(*want)) path = str(m_models->pathFor(*want));
    else if (fast && m_models->isReady(*fast)) path = str(m_models->pathFor(*fast));
    if (path == cutoutModel()) return;
    setCutoutModel(path);
    publish();   // Vorschau neu zeichnen
}

void AppController::setCutoutQuality(int q) {
    QSettings().setValue(QStringLiteral("ai/cutoutQuality"), std::clamp(q, 0, 1));
    const QString id = q == 1 ? QStringLiteral("cutout-human") : QStringLiteral("cutout");
    const StemModelInfo* m = findStemModel(str(id));
    if (m && !m_models->isReady(*m) && aiAvailable()) ensureModels({id}, [this] { updateCutoutModel(); emit modelsChanged(); });
    updateCutoutModel();
    emit selectionChanged();
}

void AppController::setAiCutout(bool on) {
    if (on && !aiAvailable()) {
        setStatus(tr("Diese Version wurde ohne KI gebaut"));
        return;
    }
    setClipProperty(QStringLiteral("aiCutout"), on ? 1 : 0);
    if (!on) return;
    const QString id = cutoutQuality() == 1 ? QStringLiteral("cutout-human") : QStringLiteral("cutout");
    const StemModelInfo* m = findStemModel(str(id));
    if (m && !m_models->isReady(*m)) {
        ensureModels({id}, [this] {
            updateCutoutModel();
            setStatus(tr("KI-Freistellung bereit"));
            emit modelsChanged();
        });
    } else {
        updateCutoutModel();
        setStatus(tr("KI-Freistellung aktiv – Hintergrund wird entfernt (darunterliegende Spur scheint durch)"));
    }
}

// ------------------------------------------------------------ Auto-Reframe

bool AppController::autoReframe(int width, int height, bool onlySelected) {
    if (m_task) return false;
    if (width > 0 && height > 0 && (width != m_project.width || height != m_project.height))
        setProjectFormat(width, height, m_project.fpsNum, m_project.fpsDen);
    std::vector<std::string> ids;
    for (const Track& tr : m_project.tracks)
        for (const Clip& c : tr.clips) {
            const MediaAsset* a = m_project.findAsset(c.assetId);
            if (!a || !a->hasVideo || a->width <= 0) continue;
            if (onlySelected && c.id != str(m_selected)) continue;
            ids.push_back(c.id);
        }
    if (ids.empty()) {
        setStatus(onlySelected ? tr("Erst einen Videoclip wählen") : tr("Keine Videoclips im Projekt"));
        return false;
    }
    auto snapshot = std::make_shared<const Project>(m_project);
    auto results = std::make_shared<std::vector<std::pair<std::string, ReframeResult>>>();
    const int w = m_project.width, h = m_project.height;
    runTask(tr("Auto-Reframe …"),
            [snapshot, ids, w, h, results](const std::function<void(double)>& progress, const std::atomic<bool>& cancel) {
                for (std::size_t i = 0; i < ids.size() && !cancel.load(); ++i) {
                    ReframeResult r = loggx::autoReframe(*snapshot, ids[i], w, h, [&](double p) {
                        progress((static_cast<double>(i) + p) / static_cast<double>(ids.size()));
                        return !cancel.load();
                    });
                    if (!r.cancelled && r.error.empty()) results->emplace_back(ids[i], std::move(r));
                }
            },
            [this, results] {
                if (results->empty()) return;
                bool ai = false;
                apply([&](Project& p) {
                    for (auto& [id, r] : *results) {
                        Clip* c = p.findClip(id);
                        if (!c) continue;
                        c->keys.erase("scale");
                        c->scale = r.scale;
                        c->keys.erase("x");
                        c->keys.erase("y");
                        c->x = c->y = 0;
                        if (!r.x.empty()) c->keys["x"] = r.x;
                        if (!r.y.empty()) c->keys["y"] = r.y;
                        ai = ai || r.usedAi;
                    }
                    return true;
                });
                setStatus(tr("Auto-Reframe: %1 Clip(s) angepasst%2").arg(results->size()).arg(ai ? tr(" (Motiv per KI erkannt)") : QString()));
            });
    return true;
}

// ------------------------------------------------------------ Multicam

bool AppController::createMulticam(const QStringList& assetIds) {
    if (m_task) return false;
    std::vector<std::string> ids, paths;
    for (const QString& id : assetIds) {
        const MediaAsset* a = m_project.findAsset(str(id));
        if (!a || !a->hasVideo || a->isStill) continue;
        ids.push_back(a->id);
        paths.push_back(a->path);
    }
    if (ids.size() < 2) {
        setStatus(tr("Für Multicam mindestens zwei Videos wählen"));
        return false;
    }
    auto results = std::make_shared<std::vector<AudioSyncResult>>();
    runTask(tr("Synchronisiere Kameras über den Ton …"),
            [paths, results](const std::function<void(double)>& progress, const std::atomic<bool>& cancel) {
                *results = syncByAudio(paths, 10 * 60 * kSecond, [&](double p) {
                    progress(p);
                    return !cancel.load();
                });
            },
            [this, ids, results] {
                if (results->size() != ids.size()) return;
                std::vector<MulticamAngle> angles;
                int unsynced = 0;
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    const auto& r = (*results)[i];
                    if (!r.ok) ++unsynced;
                    angles.push_back({ids[i], r.ok ? r.offset : 0, {}});
                }
                std::string groupId;
                const int n = static_cast<int>(m_project.multicams.size()) + 1;
                apply([&](Project& p) {
                    groupId = p.createMulticam(str(tr("Multicam %1").arg(n)), angles, m_position);
                    return !groupId.empty();
                });
                emit multicamChanged();
                setStatus(groupId.empty() ? tr("Multicam konnte nicht angelegt werden")
                          : unsynced > 0 ? tr("Multicam mit %1 Kameras – %2 ohne passenden Ton (bei 0 angelegt, bitte prüfen). Tasten 1–9 schalten um.").arg(ids.size()).arg(unsynced)
                                         : tr("Multicam mit %1 Kameras synchronisiert. Tasten 1–9 schalten um – auch beim Abspielen.").arg(ids.size()));
            });
    return true;
}

QVariantMap AppController::multicam() const {
    QVariantMap m;
    if (m_project.multicams.empty()) return m;
    const MulticamGroup* g = &m_project.multicams.front();
    for (const auto& grp : m_project.multicams)
        if (m_project.angleAt(grp.id, m_position) >= 0) g = &grp;
    QVariantList angles;
    for (std::size_t i = 0; i < g->angles.size(); ++i) {
        const MediaAsset* a = m_project.findAsset(g->angles[i].assetId);
        const TimeUs s = g->start + g->angles[i].offset, e = s + (a ? a->duration : 0);
        angles << QVariantMap{{QStringLiteral("name"), qstr(g->angles[i].name)},
                              {QStringLiteral("file"), a ? qstr(a->name) : QString()},
                              {QStringLiteral("available"), m_position >= s && m_position < e}};
    }
    m[QStringLiteral("id")] = qstr(g->id);
    m[QStringLiteral("name")] = qstr(g->name);
    m[QStringLiteral("angles")] = angles;
    m[QStringLiteral("current")] = m_project.angleAt(g->id, m_position);
    return m;
}

bool AppController::switchAngle(int angle) {
    const QVariantMap mc = multicam();
    if (mc.isEmpty()) return false;
    std::string error;
    const std::string gid = str(mc.value(QStringLiteral("id")).toString());
    const TimeUs at = m_position;
    const bool ok = apply([&](Project& p) { return p.switchAngle(gid, angle, at, &error); });
    if (!ok && !error.empty()) setStatus(qstr(error));
    emit multicamChanged();
    return ok;
}
