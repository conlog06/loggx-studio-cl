#include "DjController.h"

#include <algorithm>
#include <cmath>

#include <QAudioFormat>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include "AppController.h"
#include "MicRecorder.h"
#include "ModelManager.h"
#include "loggx/engine/Engine.h"
#include "loggx/media/Media.h"

using namespace loggx;

namespace {

QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }

int pickRate() {
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return 48000;
    for (int rate : {48000, 44100}) {
        QAudioFormat f;
        f.setSampleRate(rate);
        f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Float);
        if (dev.isFormatSupported(f)) return rate;
    }
    return 48000;
}

QString timeText(double s) {
    if (s < 0) s = 0;
    const int m = static_cast<int>(s) / 60;
    return QStringLiteral("%1:%2.%3")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(static_cast<int>(s) % 60, 2, 10, QLatin1Char('0'))
        .arg(static_cast<int>((s - std::floor(s)) * 10));
}

}  // namespace

// Liefert dem Audiogerät fortlaufend den Mix.
class DjController::Device : public QIODevice {
public:
    explicit Device(DjMixer& mixer) : m_mixer(mixer) {}
    qint64 bytesAvailable() const override { return 1 << 20; }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const std::size_t frames = std::min<std::size_t>(static_cast<std::size_t>(maxlen / 8), 1024);
        if (frames == 0) return 0;
        m_mixer.render(reinterpret_cast<float*>(data), frames);
        return static_cast<qint64>(frames * 8);
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    DjMixer& m_mixer;
};

DjController::DjController(AppController* app, QObject* parent) : QObject(parent), m_app(app), m_mixer(pickRate()) {
    loadLibrary();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit tick();
        if (++m_frame % 4 == 0) emit decksChanged();
        // Mitschnitt aus dem Audio-Thread in die Datei schreiben
        if (m_writer) {
            std::vector<float> chunk;
            {
                std::lock_guard<std::mutex> lock(m_recMutex);
                chunk.swap(m_recBuffer);
            }
            if (!chunk.empty()) m_writer->write(reinterpret_cast<const char*>(chunk.data()), static_cast<qint64>(chunk.size() * 4));
        }
    });
    m_timer.start(16);
    m_mixer.setFx(DjFx::Echo, m_fxBeats, m_fxWet, m_fxTarget);
}

DjController::~DjController() {
    if (m_writer) stopRecording();
    if (m_cancel) m_cancel->store(true);
    if (m_sink) m_sink->stop();
}

bool DjController::aiAvailable() const { return aiSeparationAvailable(); }

void DjController::setStatus(const QString& text) {
    m_status = text;
    emit statusChanged();
}

// ------------------------------------------------------------ Bibliothek

void DjController::loadLibrary() {
    const QVariantList list = QSettings().value(QStringLiteral("dj/library")).toList();
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.path = m.value(QStringLiteral("path")).toString();
        if (e.path.isEmpty()) continue;
        e.title = m.value(QStringLiteral("title")).toString();
        e.artist = m.value(QStringLiteral("artist")).toString();
        e.bpm = m.value(QStringLiteral("bpm")).toDouble();
        e.firstBeat = m.value(QStringLiteral("firstBeat")).toDouble();
        e.duration = m.value(QStringLiteral("duration")).toDouble();
        e.key = m.value(QStringLiteral("key"), -1).toInt();
        e.minor = m.value(QStringLiteral("minor")).toBool();
        e.analyzed = m.value(QStringLiteral("analyzed")).toBool();
        m_entries.push_back(e);
        if (!e.analyzed) m_analyzeQueue << e.path;
    }
    analyzeNext();
}

void DjController::saveLibrary() const {
    QVariantList list;
    for (const Entry& e : m_entries) {
        QVariantMap m;
        m[QStringLiteral("path")] = e.path;
        m[QStringLiteral("title")] = e.title;
        m[QStringLiteral("artist")] = e.artist;
        m[QStringLiteral("bpm")] = e.bpm;
        m[QStringLiteral("firstBeat")] = e.firstBeat;
        m[QStringLiteral("duration")] = e.duration;
        m[QStringLiteral("key")] = e.key;
        m[QStringLiteral("minor")] = e.minor;
        m[QStringLiteral("analyzed")] = e.analyzed;
        list << m;
    }
    QSettings().setValue(QStringLiteral("dj/library"), list);
}

QVariantList DjController::library() const {
    std::vector<const Entry*> rows;
    const QString needle = m_search.trimmed();
    for (const Entry& e : m_entries)
        if (needle.isEmpty() || e.title.contains(needle, Qt::CaseInsensitive) || e.artist.contains(needle, Qt::CaseInsensitive))
            rows.push_back(&e);
    const int col = std::abs(m_sort) % 10;
    const bool desc = m_sort < 0;
    auto less = [col](const Entry* a, const Entry* b) {
        switch (col) {
        case 1: return a->artist.compare(b->artist, Qt::CaseInsensitive) < 0;
        case 2: return a->bpm < b->bpm;
        case 3: {
            // Camelot: Zahl, dann A vor B
            const int na = a->key < 0 ? 99 : std::stoi(camelotKey(a->key, a->minor));
            const int nb = b->key < 0 ? 99 : std::stoi(camelotKey(b->key, b->minor));
            return na != nb ? na < nb : (a->minor && !b->minor);
        }
        case 4: return a->duration < b->duration;
        default: return a->title.compare(b->title, Qt::CaseInsensitive) < 0;
        }
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Entry* a, const Entry* b) { return desc ? less(b, a) : less(a, b); });
    QVariantList list;
    for (const Entry* e : rows) {
        QVariantMap m;
        m[QStringLiteral("path")] = e->path;
        m[QStringLiteral("title")] = e->title;
        m[QStringLiteral("artist")] = e->artist;
        m[QStringLiteral("bpm")] = e->bpm;
        m[QStringLiteral("key")] = qstr(camelotKey(e->key, e->minor));
        m[QStringLiteral("keyName")] = e->key >= 0 ? qstr(keyName(e->key, e->minor)) : QString();
        m[QStringLiteral("duration")] = e->duration;
        m[QStringLiteral("durationText")] = QStringLiteral("%1:%2").arg(static_cast<int>(e->duration) / 60).arg(static_cast<int>(e->duration) % 60, 2, 10, QLatin1Char('0'));
        m[QStringLiteral("analyzed")] = e->analyzed;
        m[QStringLiteral("stems")] = hasStems(e->path);
        m[QStringLiteral("onDeck")] = e->path == m_loaded[0] ? 1 : e->path == m_loaded[1] ? 2 : 0;
        m[QStringLiteral("exists")] = QFileInfo::exists(e->path);
        list << m;
    }
    return list;
}

void DjController::setSearch(const QString& text) {
    if (text == m_search) return;
    m_search = text;
    emit libraryChanged();
}

void DjController::setSortColumn(int column) {
    m_sort = column;
    emit libraryChanged();
}

void DjController::importFiles(const QList<QUrl>& urls) {
    int added = 0;
    for (const QUrl& url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const QFileInfo info(path);
        if (info.isDir()) {   // ganzer Ordner
            QList<QUrl> inner;
            for (const QFileInfo& f : QDir(path).entryInfoList(QDir::Files))
                inner << QUrl::fromLocalFile(f.absoluteFilePath());
            importFiles(inner);
            continue;
        }
        const QString ext = info.suffix().toLower();
        static const QStringList audio = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("aiff"),
                                          QStringLiteral("aif"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("ogg"),
                                          QStringLiteral("opus"), QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv")};
        if (!audio.contains(ext)) continue;
        const QString abs = info.absoluteFilePath();
        if (std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.path == abs; })) continue;
        Entry e;
        e.path = abs;
        e.title = info.completeBaseName();
        m_entries.push_back(e);
        m_analyzeQueue << abs;
        ++added;
    }
    if (added > 0) {
        saveLibrary();
        emit libraryChanged();
        setStatus(tr("%1 Track(s) hinzugefügt – Tempo und Tonart werden ermittelt …").arg(added));
        analyzeNext();
    }
}

void DjController::removeTrack(const QString& path) {
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.path == path; }),
                    m_entries.end());
    m_analyzeQueue.removeAll(path);
    saveLibrary();
    emit libraryChanged();
}

void DjController::analyzeNext() {
    if (m_analyzing || m_analyzeQueue.isEmpty()) return;
    const QString path = m_analyzeQueue.takeFirst();
    m_analyzing = true;
    struct Result {
        MediaTags tags;
        BeatInfo beat;
        KeyInfo key;
        double duration = 0;
        bool ok = false;
    };
    auto result = std::make_shared<Result>();
    QThread* thread = QThread::create([path, result] {
        const std::string p = str(path);
        MediaAsset asset;
        if (!probeMedia(p, asset) || !asset.hasAudio) return;
        readTags(p, result->tags);
        result->duration = static_cast<double>(asset.duration) / 1e6;
        // Mono mit 22,05 kHz genügt für Tempo und Tonart (höchstens 4 Minuten aus der Mitte)
        constexpr int kRate = 22050;
        AudioReader reader;
        if (!reader.open(p, kRate)) return;
        std::vector<float> mono, buf(8192 * 2);
        for (;;) {
            const std::size_t got = reader.read(buf.data(), 8192);
            for (std::size_t i = 0; i < got; ++i) mono.push_back(0.5f * (buf[i * 2] + buf[i * 2 + 1]));
            if (got < 8192 || mono.size() > static_cast<std::size_t>(kRate) * 60 * 12) break;
        }
        const std::size_t span = std::min<std::size_t>(mono.size(), static_cast<std::size_t>(kRate) * 240);
        const std::size_t from = (mono.size() - span) / 2;
        result->beat = detectBeatsFromSamples(mono.data() + from, span, kRate);
        result->beat.firstBeat += static_cast<TimeUs>(static_cast<double>(from) / kRate * 1e6);
        result->key = detectKeyFromSamples(mono.data() + from, span, kRate);
        result->ok = true;
    });
    connect(thread, &QThread::finished, this, [this, thread, path, result] {
        thread->deleteLater();
        m_analyzing = false;
        for (Entry& e : m_entries) {
            if (e.path != path) continue;
            // Schon über ein Deck analysiert? Dann diese Werte behalten.
            if (e.analyzed && (path == m_loaded[0] || path == m_loaded[1])) continue;
            e.analyzed = true;
            if (result->ok) {
                if (!result->tags.title.empty()) e.title = qstr(result->tags.title);
                e.artist = qstr(result->tags.artist);
                e.duration = result->duration;
                e.bpm = result->beat.bpm;
                if (e.bpm > 0) e.firstBeat = std::fmod(static_cast<double>(result->beat.firstBeat) / 1e6, 60.0 / e.bpm);
                e.key = result->key.tonic;
                e.minor = result->key.minor;
            }
        }
        saveLibrary();
        emit libraryChanged();
        if (m_analyzeQueue.isEmpty()) setStatus(tr("Bibliothek analysiert"));
        analyzeNext();
    });
    thread->start(QThread::LowPriority);
}

// ------------------------------------------------------------ Stems (KI)

QString DjController::stemFolder(const QString& path) const {
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/dj-stems/") +
           QString::fromLatin1(hash);
}

bool DjController::hasStems(const QString& path) const {
    const QString dir = stemFolder(path);
    for (const char* n : {"vocals.wav", "drums.wav", "bass.wav", "inst.wav"})
        if (!QFileInfo::exists(dir + QLatin1Char('/') + QLatin1String(n))) return false;
    return true;
}

void DjController::prepareStems(const QString& path) {
    if (m_working) return;
    if (!aiSeparationAvailable()) {
        setStatus(tr("Diese Version hat keine KI eingebaut"));
        return;
    }
    if (m_app->working()) {
        setStatus(tr("Bitte warten, bis die laufende Aufgabe fertig ist"));
        return;
    }
    const QString dir = stemFolder(path);
    if (!QDir().mkpath(dir)) {
        setStatus(tr("Ordner kann nicht angelegt werden: %1").arg(dir));
        return;
    }
    m_app->ensureModels({QStringLiteral("vocals"), QStringLiteral("drums"), QStringLiteral("bass")}, [this, path, dir] {
        m_working = true;
        m_workProgress = 0;
        m_workLabel = tr("KI trennt Stems: %1").arg(QFileInfo(path).completeBaseName());
        emit workChanged();
        auto result = std::make_shared<SeparationResult>();
        m_cancel = std::make_shared<std::atomic<bool>>(false);
        auto cancel = m_cancel;
        std::vector<StemJob> jobs;
        for (const char* id : {"vocals", "drums", "bass"}) {
            const StemModelInfo* model = findStemModel(id);
            jobs.push_back({model, str(m_app->models()->pathFor(*model)), str(dir + QLatin1Char('/') + QLatin1String(id) + QStringLiteral(".wav"))});
        }
        QThread* thread = QThread::create([this, path, dir, jobs, result, cancel] {
            *result = separateStemsAi(str(path), jobs, str(dir + QStringLiteral("/inst.wav")), false,
                                      [this](double p) {
                                          QMetaObject::invokeMethod(this, [this, p] {
                                              m_workProgress = p;
                                              emit workChanged();
                                          }, Qt::QueuedConnection);
                                      },
                                      cancel.get());
        });
        connect(thread, &QThread::finished, this, [this, thread, path, dir, result] {
            thread->deleteLater();
            m_working = false;
            emit workChanged();
            if (!result->ok) {
                QDir(dir).removeRecursively();
                setStatus(result->error.empty() ? tr("Stem-Trennung fehlgeschlagen") : qstr(result->error));
                return;
            }
            emit libraryChanged();
            // Geladene, angehaltene Decks gleich mit Stems neu laden
            for (int d = 0; d < 2; ++d)
                if (m_loaded[static_cast<std::size_t>(d)] == path && !m_mixer.playing(d)) loadTrack(d, path);
            setStatus(tr("Stems bereit: Gesang, Schlagzeug, Bass, Instrumente"));
        });
        thread->start();
    });
}

void DjController::cancelWork() {
    if (m_cancel) m_cancel->store(true);
}

// ------------------------------------------------------------ Decks

void DjController::ensureAudio() {
    if (m_sink) {
        if (m_sink->state() == QAudio::StoppedState) m_sink->start(m_device);
        return;
    }
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        setStatus(tr("Kein Audioausgang gefunden"));
        return;
    }
    QAudioFormat format;
    format.setSampleRate(m_mixer.sampleRate());
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (!dev.isFormatSupported(format)) {
        setStatus(tr("Der Audioausgang unterstützt %1 Hz Stereo nicht").arg(m_mixer.sampleRate()));
        return;
    }
    m_device = new Device(m_mixer);
    m_device->open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(dev, format, this);
    m_sink->setBufferSize(static_cast<qsizetype>(m_mixer.sampleRate() / 20) * 8);   // ~50 ms
    m_sink->start(m_device);
}

void DjController::setActive(bool active) {
    m_active = active;
    if (active) ensureAudio();
}

void DjController::loadTrack(int deck, const QString& path) {
    deck &= 1;
    if (m_mixer.playing(deck)) {
        setStatus(tr("Deck %1 spielt gerade – erst anhalten").arg(deck + 1));
        return;
    }
    if (m_loading[static_cast<std::size_t>(deck)]) return;
    m_loading[static_cast<std::size_t>(deck)] = true;
    emit decksChanged();
    const int rate = m_mixer.sampleRate();
    const bool stems = hasStems(path);
    const QString dir = stemFolder(path);
    auto track = std::make_shared<std::shared_ptr<DjTrack>>();
    auto error = std::make_shared<std::string>();
    QThread* thread = QThread::create([path, rate, stems, dir, track, error] {
        std::array<std::string, 4> files;
        const char* names[4] = {"vocals.wav", "drums.wav", "bass.wav", "inst.wav"};
        for (int i = 0; i < 4; ++i) files[static_cast<std::size_t>(i)] = str(dir + QLatin1Char('/') + QLatin1String(names[i]));
        *track = loadDjTrack(str(path), rate, error.get(), stems ? &files : nullptr);
    });
    connect(thread, &QThread::finished, this, [this, thread, deck, path, track, error] {
        thread->deleteLater();
        m_loading[static_cast<std::size_t>(deck)] = false;
        if (!*track) {
            setStatus(tr("Laden fehlgeschlagen: %1").arg(qstr(*error)));
            emit decksChanged();
            return;
        }
        // Bibliothek und Deck sollen dieselben Werte zeigen: vorhandene
        // Analyse übernehmen, sonst die des Decks in die Bibliothek schreiben.
        for (Entry& e : m_entries) {
            if (e.path != path) continue;
            if (e.analyzed && e.bpm > 0) {
                (*track)->bpm = e.bpm;
                (*track)->firstBeat = e.firstBeat;
                if (e.key >= 0) {
                    (*track)->key = e.key;
                    (*track)->minor = e.minor;
                }
            } else {
                e.analyzed = true;
                e.bpm = (*track)->bpm;
                e.firstBeat = (*track)->firstBeat;
                e.key = (*track)->key;
                e.minor = (*track)->minor;
                e.duration = (*track)->duration();
                if (!(*track)->title.empty()) e.title = qstr((*track)->title);
                e.artist = qstr((*track)->artist);
                m_analyzeQueue.removeAll(path);
                saveLibrary();
            }
        }
        ensureAudio();
        m_mixer.load(deck, *track);
        m_loaded[static_cast<std::size_t>(deck)] = path;
        emit decksChanged();
        emit libraryChanged();
        setStatus(tr("Deck %1: %2").arg(deck + 1).arg(qstr((*track)->title)));
    });
    thread->start();
}

void DjController::togglePlay(int deck) {
    ensureAudio();
    m_mixer.togglePlay(deck);
    emit decksChanged();
}

void DjController::cue(int deck) {
    m_mixer.cue(deck);
    emit decksChanged();
}

void DjController::sync(int deck) {
    m_mixer.sync(deck);
    emit decksChanged();
}

void DjController::setSyncLock(int deck, bool on) {
    m_mixer.setSyncLock(deck, on);
    if (on) m_mixer.sync(deck);
    emit decksChanged();
}

void DjController::setTempo(int deck, double percent) {
    const double range = m_range[static_cast<std::size_t>(deck & 1)];
    m_mixer.setTempo(deck, std::clamp(percent, -range, range));
    emit decksChanged();
}

void DjController::setTempoRange(int deck, double range) {
    m_range[static_cast<std::size_t>(deck & 1)] = std::clamp(range, 4.0, 50.0);
    setTempo(deck, m_mixer.tempo(deck));
}

void DjController::setKeyLock(int deck, bool on) {
    m_mixer.setKeyLock(deck, on);
    emit decksChanged();
}

void DjController::seek(int deck, double seconds) {
    m_mixer.seek(deck, seconds);
    emit decksChanged();
}

void DjController::seekRelative(int deck, double fraction) {
    if (auto t = m_mixer.track(deck)) seek(deck, std::clamp(fraction, 0.0, 1.0) * t->duration());
}

void DjController::hotCue(int deck, int index) {
    m_mixer.jumpHotCue(deck, index);
    emit decksChanged();
}

void DjController::clearHotCue(int deck, int index) {
    m_mixer.clearHotCue(deck, index);
    emit decksChanged();
}

void DjController::setLoop(int deck, double beats) {
    m_mixer.setLoop(deck, beats);
    emit decksChanged();
}

void DjController::exitLoop(int deck) {
    m_mixer.exitLoop(deck);
    emit decksChanged();
}

void DjController::beatJump(int deck, double beats) {
    m_mixer.beatJump(deck, beats);
    emit decksChanged();
}

void DjController::setStem(int deck, int stem, bool on) {
    m_mixer.setStem(deck, static_cast<DjStem>(std::clamp(stem, 0, 3)), on);
    emit decksChanged();
}

void DjController::setEq(int deck, int band, double value) { m_mixer.setEq(deck, band, value); }
void DjController::setFilter(int deck, double value) { m_mixer.setFilter(deck, value); }
void DjController::setTrim(int deck, double value) { m_mixer.setTrim(deck, value); }
void DjController::setFader(int deck, double value) { m_mixer.setFader(deck, value); }

void DjController::nudge(int deck, double seconds) { m_mixer.seek(deck, m_mixer.position(deck) + seconds); }

QVariantMap DjController::deckInfo(int deck) const {
    QVariantMap m;
    const auto t = m_mixer.track(deck);
    m[QStringLiteral("loading")] = m_loading[static_cast<std::size_t>(deck & 1)];
    m[QStringLiteral("loaded")] = t != nullptr;
    m[QStringLiteral("range")] = m_range[static_cast<std::size_t>(deck & 1)];
    m[QStringLiteral("tempo")] = m_mixer.tempo(deck);
    m[QStringLiteral("eq")] = QVariantList{m_mixer.eq(deck, 0), m_mixer.eq(deck, 1), m_mixer.eq(deck, 2)};
    m[QStringLiteral("filter")] = m_mixer.filter(deck);
    m[QStringLiteral("fader")] = m_mixer.fader(deck);
    m[QStringLiteral("level")] = m_mixer.level(deck);
    m[QStringLiteral("keyLock")] = m_mixer.keyLock(deck);
    m[QStringLiteral("sync")] = m_mixer.syncLock(deck);
    m[QStringLiteral("master")] = m_mixer.masterDeck() == (deck & 1) && t != nullptr;
    if (!t) return m;
    const double pos = m_mixer.position(deck);
    m[QStringLiteral("path")] = qstr(t->path);
    m[QStringLiteral("title")] = qstr(t->title);
    m[QStringLiteral("artist")] = qstr(t->artist);
    m[QStringLiteral("bpm")] = t->bpm;
    m[QStringLiteral("bpmNow")] = m_mixer.currentBpm(deck);
    m[QStringLiteral("key")] = qstr(camelotKey(t->key, t->minor));
    m[QStringLiteral("keyName")] = t->key >= 0 ? qstr(keyName(t->key, t->minor)) : QString();
    m[QStringLiteral("duration")] = t->duration();
    m[QStringLiteral("position")] = pos;
    m[QStringLiteral("elapsedText")] = timeText(pos);
    m[QStringLiteral("remainingText")] = QStringLiteral("-") + timeText(t->duration() - pos);
    m[QStringLiteral("playing")] = m_mixer.playing(deck);
    m[QStringLiteral("cue")] = m_mixer.cuePoint(deck);
    m[QStringLiteral("looping")] = m_mixer.looping(deck);
    m[QStringLiteral("loopBeats")] = m_mixer.loopBeats(deck);
    QVariantList hot;
    for (int i = 0; i < 8; ++i) hot << m_mixer.hotCue(deck, i);
    m[QStringLiteral("hot")] = hot;
    m[QStringLiteral("hasStems")] = t->hasStems;
    m[QStringLiteral("stems")] = QVariantList{m_mixer.stem(deck, DjStem::Vocals), m_mixer.stem(deck, DjStem::Drums),
                                              m_mixer.stem(deck, DjStem::Bass), m_mixer.stem(deck, DjStem::Inst)};
    // Takt-Anzeige: Schlag im Takt (1–4) und Takte bis zum nächsten 32er-Block
    if (t->bpm > 0) {
        const double beats = (pos - t->firstBeat) / t->beatLength();
        m[QStringLiteral("beatInBar")] = static_cast<int>(std::floor(beats)) % 4 + 1;
        m[QStringLiteral("bar")] = static_cast<int>(std::floor(beats / 4)) + 1;
    }
    return m;
}

// ------------------------------------------------------------ Mixer

void DjController::setCrossfader(double value) {
    m_mixer.setCrossfader(value);
    emit mixerChanged();
}

void DjController::setMasterVolume(double value) {
    m_masterVolume = std::clamp(value, 0.0, 1.5);
    m_mixer.setMasterVolume(m_masterVolume);
    emit mixerChanged();
}

void DjController::setQuantize(bool on) {
    m_mixer.setQuantize(on);
    emit mixerChanged();
}

void DjController::setFx(int type, double beats, double wet, int target) {
    m_fxType = std::clamp(type, 0, 2);
    m_fxBeats = beats;
    m_fxWet = wet;
    m_fxTarget = std::clamp(target, 0, 2);
    m_mixer.setFx(static_cast<DjFx>(m_fxType), m_fxBeats, m_fxWet, m_fxTarget);
    emit mixerChanged();
}

void DjController::setFxOn(bool on) {
    m_mixer.setFxOn(on);
    emit decksChanged();
}

// ------------------------------------------------------------ Mitschnitt

double DjController::recordSeconds() const {
    return m_writer ? static_cast<double>(m_writer->frames()) / m_mixer.sampleRate() : 0.0;
}

bool DjController::startRecording() {
    if (m_writer) return false;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/LoggX Mixes");
    if (!QDir().mkpath(folder)) {
        setStatus(tr("Ordner kann nicht angelegt werden: %1").arg(folder));
        return false;
    }
    m_recPath = folder + QLatin1Char('/') + tr("Mix %1.wav").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
    auto* writer = new WavWriter;
    if (!writer->create(m_recPath, m_mixer.sampleRate(), 2, 32, true)) {
        delete writer;
        setStatus(tr("Datei kann nicht geschrieben werden: %1").arg(m_recPath));
        return false;
    }
    m_writer = writer;
    ensureAudio();
    m_mixer.setRecorder([this](const float* data, std::size_t frames) {
        std::lock_guard<std::mutex> lock(m_recMutex);
        m_recBuffer.insert(m_recBuffer.end(), data, data + frames * 2);
    });
    emit recordingChanged();
    setStatus(tr("Mitschnitt läuft"));
    return true;
}

QString DjController::stopRecording() {
    if (!m_writer) return {};
    m_mixer.setRecorder(nullptr);
    {
        std::lock_guard<std::mutex> lock(m_recMutex);
        if (!m_recBuffer.empty())
            m_writer->write(reinterpret_cast<const char*>(m_recBuffer.data()), static_cast<qint64>(m_recBuffer.size() * 4));
        m_recBuffer.clear();
    }
    const double seconds = recordSeconds();
    m_writer->finish();
    delete m_writer;
    m_writer = nullptr;
    emit recordingChanged();
    setStatus(tr("Mix gespeichert (%1:%2): %3")
                  .arg(static_cast<int>(seconds) / 60)
                  .arg(static_cast<int>(seconds) % 60, 2, 10, QLatin1Char('0'))
                  .arg(QDir::toNativeSeparators(m_recPath)));
    emit mixRecorded(m_recPath);
    return m_recPath;
}
