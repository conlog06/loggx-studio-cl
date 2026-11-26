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

// Kopfhörer auf eigenem Gerät: der Audio-Thread des Hauptausgangs schreibt
// hinein, das zweite Gerät liest (begrenzt, damit keine Latenz wächst).
struct DjController::CueRing {
    std::mutex mutex;
    std::vector<float> data;   // Stereo, verschachtelt
    std::size_t limit = 48000 / 5 * 2;
    void push(const float* x, std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex);
        data.insert(data.end(), x, x + n);
        if (data.size() > limit) data.erase(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(data.size() - limit));
    }
    std::size_t pop(float* out, std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::size_t take = std::min(n, data.size());
        std::copy(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(take), out);
        data.erase(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(take));
        return take;
    }
};

// Liefert dem Audiogerät fortlaufend den Mix. Bei 4 Kanälen (z. B. DDJ-FLX4):
// 1/2 = Master, 3/4 = Kopfhörer.
class DjController::Device : public QIODevice {
public:
    Device(DjMixer& mixer, int channels, std::shared_ptr<CueRing> ring)
        : m_mixer(mixer), m_channels(channels), m_ring(std::move(ring)) {}
    qint64 bytesAvailable() const override { return 1 << 20; }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const std::size_t frameBytes = static_cast<std::size_t>(m_channels) * 4;
        const std::size_t frames = std::min<std::size_t>(static_cast<std::size_t>(maxlen) / frameBytes, 1024);
        if (frames == 0) return 0;
        auto* out = reinterpret_cast<float*>(data);
        const bool cue = m_channels >= 4 || m_ring;
        if (m_channels == 2 && !cue) {
            m_mixer.render(out, frames);
            return static_cast<qint64>(frames * frameBytes);
        }
        m_master.resize(frames * 2);
        m_cue.resize(frames * 2);
        m_mixer.render(m_master.data(), frames, m_cue.data());
        if (m_ring) m_ring->push(m_cue.data(), frames * 2);
        for (std::size_t k = 0; k < frames; ++k) {
            float* f = out + k * static_cast<std::size_t>(m_channels);
            std::fill(f, f + m_channels, 0.0f);
            f[0] = m_master[k * 2];
            f[1] = m_master[k * 2 + 1];
            if (m_channels >= 4) {
                f[2] = m_cue[k * 2];
                f[3] = m_cue[k * 2 + 1];
            }
        }
        return static_cast<qint64>(frames * frameBytes);
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    DjMixer& m_mixer;
    int m_channels;
    std::shared_ptr<CueRing> m_ring;
    std::vector<float> m_master, m_cue;
};

class DjController::CueDevice : public QIODevice {
public:
    explicit CueDevice(std::shared_ptr<CueRing> ring) : m_ring(std::move(ring)) {}
    qint64 bytesAvailable() const override { return 1 << 20; }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(maxlen) / 8, 1024) * 2;
        if (n == 0) return 0;
        auto* out = reinterpret_cast<float*>(data);
        const std::size_t got = m_ring->pop(out, n);
        std::fill(out + got, out + n, 0.0f);
        return static_cast<qint64>(n * 4);
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    std::shared_ptr<CueRing> m_ring;
};

DjController::DjController(AppController* app, QObject* parent) : QObject(parent), m_app(app), m_mixer(pickRate()) {
    m_clock.start();
    // Jede Änderung der Bibliothek verwirft den Zwischenspeicher (vor QML)
    connect(this, &DjController::libraryChanged, this, [this] {
        m_rowsValid = false;
        m_libValid = false;
    });
    QSettings settings;
    m_bpmMin = settings.value(QStringLiteral("dj/bpmMin"), 70.0).toDouble();
    m_bpmMax = settings.value(QStringLiteral("dj/bpmMax"), 180.0).toDouble();
    m_outputDevice = settings.value(QStringLiteral("dj/outputDevice")).toString();
    m_headDevice = settings.value(QStringLiteral("dj/headphoneDevice")).toString();
    m_cueMode = settings.value(QStringLiteral("dj/cueMode"), 1).toInt();
    m_bufferMs = settings.value(QStringLiteral("dj/bufferMs"), 30).toInt();
    m_defaultRange = settings.value(QStringLiteral("dj/tempoRange"), 8.0).toDouble();
    m_autoKeyLock = settings.value(QStringLiteral("dj/autoKeyLock"), false).toBool();
    m_autoGain = settings.value(QStringLiteral("dj/autoGain"), true).toBool();
    m_autoStems = settings.value(QStringLiteral("dj/autoStems"), false).toBool();
    m_stemEngine = settings.value(QStringLiteral("dj/stemEngine"), 0).toInt();
    m_lockPlaying = settings.value(QStringLiteral("dj/lockPlaying"), true).toBool();
    m_waveStyle = settings.value(QStringLiteral("dj/waveStyle"), 0).toInt();
    m_libraryHeight = settings.value(QStringLiteral("dj/libraryHeight"), 230.0).toDouble();
    m_libraryMax = settings.value(QStringLiteral("dj/libraryMax"), false).toBool();
    m_columns = settings.value(QStringLiteral("dj/columns"), QStringList{QStringLiteral("artist"), QStringLiteral("genre"), QStringLiteral("bpm"), QStringLiteral("key"),
                                                                           QStringLiteral("time"), QStringLiteral("rating"), QStringLiteral("added")}).toStringList();
    m_rowSize = settings.value(QStringLiteral("dj/rowSize"), 1).toInt();
    m_range = {m_defaultRange, m_defaultRange};
    m_mixer.setHeadphones(m_headMix, m_headVolume);
    m_waveZoom = settings.value(QStringLiteral("dj/waveZoom"), 8.0).toDouble();
    loadPadFx();
    m_mixer.setSamplerVolume(settings.value(QStringLiteral("dj/samplerVolume"), 0.8).toDouble());
    initSampler();
    loadLibrary();
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit tick();
        if (++m_frame % 4 == 0) emit decksChanged();
        // Sampler: abgelaufene Klänge melden
        bool samplerDirty = false;
        for (int i = 0; i < 8; ++i)
            if (m_samplePlaying[static_cast<std::size_t>(i)] && !m_mixer.samplePlaying(i)) {
                m_samplePlaying[static_cast<std::size_t>(i)] = false;
                samplerDirty = true;
            }
        if (samplerDirty) emit samplerChanged();
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
    if (m_cueSink) m_cueSink->stop();
    delete m_device;
    delete m_cueDevice;
}

bool DjController::aiAvailable() const { return aiSeparationAvailable(); }

void DjController::setStatus(const QString& text) {
    m_status = text;
    emit statusChanged();
}

// ------------------------------------------------------------ Bibliothek

namespace {
// Dateiname als Titel: „Movin__To_The_Sun_-_HUGEL“ → „Movin To The Sun - HUGEL“
QString niceName(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.simplified();
}
constexpr double kGainTarget = -13.0;   // RMS-Ziel in dBFS für Auto-Gain
constexpr int kAnalysisVersion = 3;   // 0.7: neue Tempo-Erkennung (3: ganzzahlig einrasten)
}

void DjController::loadLibrary() {
    QSettings settings;
    const QVariantList list = settings.value(QStringLiteral("dj/library")).toList();
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.path = m.value(QStringLiteral("path")).toString();
        if (e.path.isEmpty()) continue;
        e.title = m.value(QStringLiteral("title")).toString();
        e.artist = m.value(QStringLiteral("artist")).toString();
        e.album = m.value(QStringLiteral("album")).toString();
        e.genre = m.value(QStringLiteral("genre")).toString();
        e.comment = m.value(QStringLiteral("comment")).toString();
        e.source = m.value(QStringLiteral("source")).toString();
        e.bpm = m.value(QStringLiteral("bpm")).toDouble();
        e.firstBeat = m.value(QStringLiteral("firstBeat")).toDouble();
        e.duration = m.value(QStringLiteral("duration")).toDouble();
        e.key = m.value(QStringLiteral("key"), -1).toInt();
        e.minor = m.value(QStringLiteral("minor")).toBool();
        e.analyzed = m.value(QStringLiteral("analyzed")).toBool();
        e.version = m.value(QStringLiteral("version"), 0).toInt();
        e.gridEdited = m.value(QStringLiteral("gridEdited")).toBool();
        e.rating = m.value(QStringLiteral("rating")).toInt();
        e.loudness = m.value(QStringLiteral("loudness")).toDouble();
        e.plays = m.value(QStringLiteral("plays")).toInt();
        e.added = m.value(QStringLiteral("added")).toLongLong();
        const QVariantList hot = m.value(QStringLiteral("hot")).toList();
        for (int i = 0; i < 8 && i < hot.size(); ++i) e.hot[static_cast<std::size_t>(i)] = hot[i].toDouble();
        m_entries.push_back(e);
        // Neue Erkennung: alte Analysen (ohne Handarbeit) erneuern
        if (!e.analyzed || (e.version < kAnalysisVersion && !e.gridEdited)) m_analyzeQueue << e.path;
    }
    for (const QVariant& v : settings.value(QStringLiteral("dj/playlists")).toList()) {
        const QVariantMap m = v.toMap();
        m_playlists.push_back({m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("paths")).toStringList()});
    }
    m_history = settings.value(QStringLiteral("dj/history")).toStringList();
    analyzeNext();
}

void DjController::saveLibrary() const {
    QVariantList list;
    for (const Entry& e : m_entries) {
        QVariantMap m;
        m[QStringLiteral("path")] = e.path;
        m[QStringLiteral("title")] = e.title;
        m[QStringLiteral("artist")] = e.artist;
        if (!e.album.isEmpty()) m[QStringLiteral("album")] = e.album;
        if (!e.genre.isEmpty()) m[QStringLiteral("genre")] = e.genre;
        if (!e.comment.isEmpty()) m[QStringLiteral("comment")] = e.comment;
        if (!e.source.isEmpty()) m[QStringLiteral("source")] = e.source;
        m[QStringLiteral("bpm")] = e.bpm;
        m[QStringLiteral("firstBeat")] = e.firstBeat;
        m[QStringLiteral("duration")] = e.duration;
        m[QStringLiteral("key")] = e.key;
        m[QStringLiteral("minor")] = e.minor;
        m[QStringLiteral("analyzed")] = e.analyzed;
        m[QStringLiteral("version")] = e.version;
        m[QStringLiteral("gridEdited")] = e.gridEdited;
        m[QStringLiteral("rating")] = e.rating;
        m[QStringLiteral("loudness")] = e.loudness;
        m[QStringLiteral("plays")] = e.plays;
        m[QStringLiteral("added")] = e.added;
        QVariantList hot;
        bool anyHot = false;
        for (double h : e.hot) {
            hot << h;
            anyHot = anyHot || h >= 0;
        }
        if (anyHot) m[QStringLiteral("hot")] = hot;
        list << m;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("dj/library"), list);
    QVariantList pl;
    for (const Playlist& p : m_playlists)
        pl << QVariantMap{{QStringLiteral("name"), p.name}, {QStringLiteral("paths"), p.paths}};
    settings.setValue(QStringLiteral("dj/playlists"), pl);
    settings.setValue(QStringLiteral("dj/history"), m_history);
}

DjController::Entry* DjController::entry(const QString& path) {
    for (Entry& e : m_entries)
        if (e.path == path) return &e;
    return nullptr;
}

// Zwischenspeicher: Sortieren/Filtern nur nach Änderungen (große Bibliotheken)
std::vector<const DjController::Entry*> DjController::rows() const {
    if (m_rowsValid && m_rowsEntryCount == m_entries.size()) {
        std::vector<const Entry*> out;
        out.reserve(m_rowsCache.size());
        for (std::size_t i : m_rowsCache) out.push_back(&m_entries[i]);
        return out;
    }
    std::vector<const Entry*> rows = computeRows();
    m_rowsCache.clear();
    for (const Entry* e : rows) m_rowsCache.push_back(static_cast<std::size_t>(e - m_entries.data()));
    m_rowsEntryCount = m_entries.size();
    m_rowsValid = true;
    return rows;
}

std::vector<const DjController::Entry*> DjController::computeRows() const {
    std::vector<const Entry*> rows;
    const QString needle = m_search.trimmed();
    auto matches = [&](const Entry& e) {
        return needle.isEmpty() || e.title.contains(needle, Qt::CaseInsensitive) || e.artist.contains(needle, Qt::CaseInsensitive) ||
               e.album.contains(needle, Qt::CaseInsensitive) || e.genre.contains(needle, Qt::CaseInsensitive) ||
               e.comment.contains(needle, Qt::CaseInsensitive);
    };
    auto find = [&](const QString& path) -> const Entry* {
        for (const Entry& e : m_entries)
            if (e.path == path) return &e;
        return nullptr;
    };
    if (m_view == -3) return rows;   // Ordner-Ansicht hat eigene Liste
    if (m_view == -1) {
        for (const Entry& e : m_entries)
            if (matches(e)) rows.push_back(&e);
    } else {
        const QStringList& paths = m_view == -2 ? m_history : m_playlists[static_cast<std::size_t>(m_view)].paths;
        // Verlauf: neueste zuerst
        for (int i = 0; i < paths.size(); ++i) {
            const QString& p = m_view == -2 ? paths[paths.size() - 1 - i] : paths[i];
            if (const Entry* e = find(p); e && matches(*e)) rows.push_back(e);
        }
    }
    const int col = std::abs(m_sort) % 20;
    const bool desc = m_sort < 0;
    if (col == 0 && m_view != -1) return rows;   // Playlist-/Verlaufsreihenfolge behalten
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
        case 5: return a->genre.compare(b->genre, Qt::CaseInsensitive) < 0;
        case 6: return a->rating < b->rating;
        case 7: return a->added < b->added;
        case 8: return a->plays < b->plays;
        case 9: return a->album.compare(b->album, Qt::CaseInsensitive) < 0;
        case 10: return a->comment.compare(b->comment, Qt::CaseInsensitive) < 0;
        default: return a->title.compare(b->title, Qt::CaseInsensitive) < 0;
        }
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Entry* a, const Entry* b) { return desc ? less(b, a) : less(a, b); });
    return rows;
}

QVariantList DjController::library() const {
    if (m_libValid && m_rowsValid && m_rowsEntryCount == m_entries.size()) return m_libCache;
    QVariantList list;
    for (const Entry* e : rows()) {
        QVariantMap m;
        m[QStringLiteral("path")] = e->path;
        m[QStringLiteral("title")] = e->title;
        m[QStringLiteral("artist")] = e->artist;
        m[QStringLiteral("album")] = e->album;
        m[QStringLiteral("genre")] = e->genre;
        m[QStringLiteral("comment")] = e->comment;
        m[QStringLiteral("bpm")] = e->bpm;
        m[QStringLiteral("key")] = qstr(camelotKey(e->key, e->minor));
        m[QStringLiteral("keyName")] = e->key >= 0 ? qstr(keyName(e->key, e->minor)) : QString();
        m[QStringLiteral("duration")] = e->duration;
        m[QStringLiteral("durationText")] = QStringLiteral("%1:%2").arg(static_cast<int>(e->duration) / 60).arg(static_cast<int>(e->duration) % 60, 2, 10, QLatin1Char('0'));
        m[QStringLiteral("analyzed")] = e->analyzed;
        m[QStringLiteral("loudness")] = e->loudness;
        m[QStringLiteral("gridEdited")] = e->gridEdited;
        m[QStringLiteral("rating")] = e->rating;
        m[QStringLiteral("plays")] = e->plays;
        m[QStringLiteral("addedText")] = e->added > 0 ? QDateTime::fromMSecsSinceEpoch(e->added).toString(QStringLiteral("dd.MM.yy")) : QString();
        m[QStringLiteral("source")] = e->source;
        int hot = 0;
        for (double h : e->hot) hot += h >= 0 ? 1 : 0;
        m[QStringLiteral("hotCount")] = hot;
        m[QStringLiteral("stems")] = hasStems(e->path);
        m[QStringLiteral("onDeck")] = e->path == m_loaded[0] ? 1 : e->path == m_loaded[1] ? 2 : 0;
        m[QStringLiteral("exists")] = QFileInfo::exists(e->path);
        list << m;
    }
    m_libCache = list;
    m_libValid = true;
    return list;
}

int DjController::trackCount() const { return static_cast<int>(rows().size()); }

QString DjController::pathAt(int row) const {
    const auto r = rows();
    return row >= 0 && row < static_cast<int>(r.size()) ? r[static_cast<std::size_t>(row)]->path : QString();
}

QVariantMap DjController::trackInfo(const QString& path) const {
    const QVariantList list = library();
    for (const QVariant& v : list)
        if (v.toMap().value(QStringLiteral("path")).toString() == path) return v.toMap();
    for (const Entry& e : m_entries)
        if (e.path == path) return {{QStringLiteral("path"), e.path}, {QStringLiteral("title"), e.title}, {QStringLiteral("bpm"), e.bpm}};
    return {};
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
        e.title = niceName(info.completeBaseName());
        e.added = QDateTime::currentMSecsSinceEpoch();
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
    for (Playlist& p : m_playlists) p.paths.removeAll(path);
    m_history.removeAll(path);
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
        double loudness = 0;   // RMS in dBFS
        bool ok = false;
    };
    auto result = std::make_shared<Result>();
    const double lo = m_bpmMin, hi = m_bpmMax;
    QThread* thread = QThread::create([path, result, lo, hi] {
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
        result->beat = detectBeatsFromSamples(mono.data() + from, span, kRate, lo, hi);
        result->beat.firstBeat += static_cast<TimeUs>(static_cast<double>(from) / kRate * 1e6);
        result->key = detectKeyFromSamples(mono.data() + from, span, kRate);
        double sum = 0;
        for (std::size_t i = from; i < from + span; ++i) sum += static_cast<double>(mono[i]) * mono[i];
        result->loudness = span > 0 ? 10.0 * std::log10(std::max(1e-12, sum / static_cast<double>(span))) : 0.0;
        result->ok = true;
    });
    connect(thread, &QThread::finished, this, [this, thread, path, result] {
        thread->deleteLater();
        m_analyzing = false;
        if (Entry* e = entry(path)) {
            e->analyzed = true;
            e->version = kAnalysisVersion;
            if (result->ok) {
                if (!result->tags.title.empty()) e->title = qstr(result->tags.title);
                if (!result->tags.artist.empty()) e->artist = qstr(result->tags.artist);
                if (!result->tags.album.empty()) e->album = qstr(result->tags.album);
                if (!result->tags.genre.empty()) e->genre = qstr(result->tags.genre);
                e->duration = result->duration;
                e->loudness = result->loudness;
                // Von Hand (oder aus rekordbox) gesetztes Raster bleibt
                if (!e->gridEdited) {
                    e->bpm = result->beat.bpm;
                    if (e->bpm > 0) e->firstBeat = std::fmod(static_cast<double>(result->beat.firstBeat) / 1e6, 60.0 / e->bpm);
                    applyGrid(path);
                }
                if (e->key < 0 || e->source.isEmpty()) {
                    e->key = result->key.tonic;
                    e->minor = result->key.minor;
                }
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
    if (m_app->working()) {
        setStatus(tr("Bitte warten, bis die laufende Aufgabe fertig ist"));
        return;
    }
    const QString dir = stemFolder(path);
    if (!QDir().mkpath(dir)) {
        setStatus(tr("Ordner kann nicht angelegt werden: %1").arg(dir));
        return;
    }
    const bool ai = m_stemEngine == 1 && aiSeparationAvailable();
    // Läuft im Hintergrund; danach Decks mit Stems neu laden
    auto run = [this, path, dir](bool ai) {
        m_working = true;
        m_workProgress = 0;
        m_workLabel = (ai ? tr("KI trennt Stems: %1") : tr("Stems werden getrennt: %1")).arg(QFileInfo(path).completeBaseName());
        emit workChanged();
        auto result = std::make_shared<SeparationResult>();
        m_cancel = std::make_shared<std::atomic<bool>>(false);
        auto cancel = m_cancel;
        std::vector<StemJob> jobs;
        if (ai) {
            for (const char* id : {"vocals", "drums", "bass"}) {
                const StemModelInfo* model = findStemModel(id);
                jobs.push_back({model, str(m_app->models()->pathFor(*model)), str(dir + QLatin1Char('/') + QLatin1String(id) + QStringLiteral(".wav"))});
            }
        }
        auto report = [this](double p) {
            QMetaObject::invokeMethod(this, [this, p] {
                m_workProgress = p;
                emit workChanged();
            }, Qt::QueuedConnection);
        };
        QThread* thread = QThread::create([path, dir, jobs, result, cancel, ai, report] {
            if (ai) {
                *result = separateStemsAi(str(path), jobs, str(dir + QStringLiteral("/inst.wav")), false, report, cancel.get());
            } else {
                *result = separateStemsDsp(str(path), str(dir + QStringLiteral("/vocals.wav")), str(dir + QStringLiteral("/drums.wav")),
                                           str(dir + QStringLiteral("/bass.wav")), str(dir + QStringLiteral("/inst.wav")), report, cancel.get());
            }
        });
        connect(thread, &QThread::finished, this, [this, thread, path, dir, result, ai] {
            thread->deleteLater();
            m_working = false;
            emit workChanged();
            if (!result->ok) {
                QDir(dir).removeRecursively();
                setStatus(result->error.empty() ? tr("Stem-Trennung fehlgeschlagen") : qstr(result->error));
                return;
            }
            QFile marker(dir + QStringLiteral("/engine.txt"));
            if (marker.open(QIODevice::WriteOnly)) marker.write(ai ? "ai" : "dsp");
            emit libraryChanged();
            for (int d = 0; d < 2; ++d)
                if (m_loaded[static_cast<std::size_t>(d)] == path && !m_mixer.playing(d)) loadTrack(d, path);
            setStatus(ai ? tr("KI-Stems bereit: Gesang, Schlagzeug, Bass, Instrumente")
                         : tr("Stems bereit (eingebaute Engine) – für noch sauberere Trennung: Einstellungen → KI"));
        });
        thread->start();
    };
    if (ai)
        m_app->ensureModels({QStringLiteral("vocals"), QStringLiteral("drums"), QStringLiteral("bass")}, [run] { run(true); },
                            [this, run](const QString& error) {
                                // KI-Modelle nicht ladbar: eingebaute Engine nehmen
                                setStatus(tr("KI-Modelle nicht verfügbar (%1) – Stems mit der eingebauten Engine").arg(error));
                                run(false);
                            });
    else run(false);
}

void DjController::setStemEngine(int engine) {
    m_stemEngine = std::clamp(engine, 0, 1);
    QSettings().setValue(QStringLiteral("dj/stemEngine"), m_stemEngine);
    emit settingsChanged();
}

void DjController::recomputeStems(const QString& path) {
    QDir(stemFolder(path)).removeRecursively();
    emit libraryChanged();
    prepareStems(path);
}

void DjController::cancelWork() {
    if (m_cancel) m_cancel->store(true);
}

// ------------------------------------------------------------ Decks

namespace {
QAudioDevice findOutput(const QString& name) {
    if (!name.isEmpty())
        for (const QAudioDevice& d : QMediaDevices::audioOutputs())
            if (d.description() == name) return d;
    return QMediaDevices::defaultAudioOutput();
}
}  // namespace

QStringList DjController::outputDevices() const {
    QStringList list;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) list << d.description();
    return list;
}

void DjController::ensureAudio() {
    if (m_sink) {
        if (m_sink->state() == QAudio::StoppedState) m_sink->start(m_device);
        if (m_cueSink && m_cueSink->state() == QAudio::StoppedState) m_cueSink->start(m_cueDevice);
        return;
    }
    const QAudioDevice dev = findOutput(m_outputDevice);
    if (dev.isNull()) {
        m_audioInfo = tr("Kein Audioausgang gefunden");
        emit settingsChanged();
        setStatus(m_audioInfo);
        return;
    }
    QAudioFormat format;
    format.setSampleRate(m_mixer.sampleRate());
    format.setSampleFormat(QAudioFormat::Float);
    // Kopfhörer über Kanäle 3/4 (DJ-Controller mit eingebauter Soundkarte)
    int channels = 2;
    if (m_cueMode == 1) {
        format.setChannelCount(4);
        if (dev.isFormatSupported(format)) channels = 4;
    }
    format.setChannelCount(channels);
    if (!dev.isFormatSupported(format)) {
        m_audioInfo = tr("%1 unterstützt %2 Hz nicht").arg(dev.description()).arg(m_mixer.sampleRate());
        emit settingsChanged();
        setStatus(m_audioInfo);
        return;
    }
    if (m_cueMode == 2) {
        const QAudioDevice head = findOutput(m_headDevice);
        QAudioFormat hf = format;
        hf.setChannelCount(2);
        if (!head.isNull() && head.id() != dev.id() && head.isFormatSupported(hf)) {
            m_ring = std::make_shared<CueRing>();
            m_ring->limit = static_cast<std::size_t>(m_mixer.sampleRate()) / 8 * 2;
            m_cueDevice = new CueDevice(m_ring);
            m_cueDevice->open(QIODevice::ReadOnly);
            m_cueSink = new QAudioSink(head, hf, this);
            m_cueSink->setBufferSize(static_cast<qsizetype>(m_mixer.sampleRate() * m_bufferMs / 1000) * 8);
        }
    }
    m_device = new Device(m_mixer, channels, m_ring);
    m_device->open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(dev, format, this);
    m_sink->setBufferSize(static_cast<qsizetype>(m_mixer.sampleRate() * std::clamp(m_bufferMs, 5, 200) / 1000) * channels * 4);
    m_sink->start(m_device);
    if (m_cueSink) m_cueSink->start(m_cueDevice);
    m_audioInfo = tr("%1 · %2 Kanäle · %3 Hz").arg(dev.description()).arg(channels).arg(m_mixer.sampleRate());
    if (m_cueMode == 1 && channels == 4) m_audioInfo += tr(" · Kopfhörer: Kanäle 3/4");
    else if (m_cueSink) m_audioInfo += tr(" · Kopfhörer: %1").arg(findOutput(m_headDevice).description());
    else if (m_cueMode != 0) m_audioInfo += tr(" · Kopfhörer: kein eigener Ausgang verfügbar");
    emit settingsChanged();
}

void DjController::restartAudio() {
    if (m_sink) m_sink->stop();
    if (m_cueSink) m_cueSink->stop();
    delete m_sink;
    delete m_cueSink;
    delete m_device;
    delete m_cueDevice;
    m_sink = m_cueSink = nullptr;
    m_device = nullptr;
    m_cueDevice = nullptr;
    m_ring.reset();
    if (m_active || m_mixer.playing(0) || m_mixer.playing(1) || m_writer) ensureAudio();
}

void DjController::setActive(bool active) {
    m_active = active;
    if (active) ensureAudio();
}

void DjController::loadTrack(int deck, const QString& path) {
    deck &= 1;
    if (m_lockPlaying && m_mixer.playing(deck)) {
        setStatus(tr("Deck %1 spielt gerade – erst anhalten (Einstellungen → DJ → Spielendes Deck sperren)").arg(deck + 1));
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
        Entry* e = entry(path);
        if (!e) {
            Entry fresh;
            fresh.path = path;
            fresh.added = QDateTime::currentMSecsSinceEpoch();
            m_entries.push_back(fresh);
            e = &m_entries.back();
        }
        if (e->analyzed && e->bpm > 0) {
            (*track)->bpm = e->bpm;
            (*track)->firstBeat = e->firstBeat;
            if (e->key >= 0) {
                (*track)->key = e->key;
                (*track)->minor = e->minor;
            }
        } else {
            e->analyzed = true;
            e->version = kAnalysisVersion;
            e->bpm = (*track)->bpm;
            e->firstBeat = (*track)->firstBeat;
            e->key = (*track)->key;
            e->minor = (*track)->minor;
            e->duration = (*track)->duration();
            if (!(*track)->title.empty()) e->title = qstr((*track)->title);
            e->artist = qstr((*track)->artist);
            m_analyzeQueue.removeAll(path);
        }
        const std::array<double, 8> hot = e->hot;
        saveLibrary();
        ensureAudio();
        m_mixer.load(deck, *track);
        for (int i = 0; i < 8; ++i) m_mixer.setHotCuePoint(deck, i, hot[static_cast<std::size_t>(i)]);
        if (m_autoKeyLock) m_mixer.setKeyLock(deck, true);
        // Lautheit angleichen (Auto-Gain): Trim so, dass alle Tracks gleich laut sind
        if (e->loudness >= 0 || e->loudness < -80) {
            const std::vector<float>& mix = (*track)->mix;
            double sum = 0;
            const std::size_t step = std::max<std::size_t>(1, mix.size() / 2000000);
            std::size_t n = 0;
            for (std::size_t i = 0; i + 1 < mix.size(); i += 2 * step, ++n) {
                const double v = 0.5 * (mix[i] + mix[i + 1]);
                sum += v * v;
            }
            if (n > 0) e->loudness = 10.0 * std::log10(std::max(1e-12, sum / static_cast<double>(n)));
        }
        if (m_autoGain && e->loudness < 0 && e->loudness > -80)
            m_mixer.setTrim(deck, std::clamp(std::pow(10.0, (kGainTarget - e->loudness) / 20.0), 0.35, 2.0));
        if (m_autoStems && !hasStems(path)) QTimer::singleShot(0, this, [this, path] { prepareStems(path); });
        m_loaded[static_cast<std::size_t>(deck)] = path;
        emit decksChanged();
        emit libraryChanged();
        emit deckLoaded(deck);
        setStatus(tr("Deck %1: %2").arg(deck + 1).arg(qstr((*track)->title)));
    });
    thread->start();
}

void DjController::togglePlay(int deck) {
    ensureAudio();
    const bool was = m_mixer.playing(deck);
    m_mixer.togglePlay(deck);
    // Verlauf: jeder gestartete Track einmal (wie die History in DJ-Programmen)
    const QString path = m_loaded[static_cast<std::size_t>(deck & 1)];
    if (!was && m_mixer.playing(deck) && !path.isEmpty() && (m_history.isEmpty() || m_history.last() != path)) {
        m_history.removeAll(path);
        m_history << path;
        while (m_history.size() > 500) m_history.removeFirst();
        if (Entry* e = entry(path)) ++e->plays;
        saveLibrary();
        emit libraryChanged();
    }
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
    const bool empty = m_mixer.hotCue(deck, index) < 0;
    m_mixer.jumpHotCue(deck, index);
    if (empty) syncHotCues(deck);
    emit decksChanged();
}

void DjController::clearHotCue(int deck, int index) {
    m_mixer.clearHotCue(deck, index);
    syncHotCues(deck);
    emit decksChanged();
}

// Hot Cues gehören zum Track: in der Bibliothek merken
void DjController::syncHotCues(int deck) {
    Entry* e = entry(m_loaded[static_cast<std::size_t>(deck & 1)]);
    if (!e) return;
    for (int i = 0; i < 8; ++i) e->hot[static_cast<std::size_t>(i)] = m_mixer.hotCue(deck, i);
    saveLibrary();
    emit libraryChanged();
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
    m[QStringLiteral("pfl")] = m_mixer.pfl(deck);
    // Phrasen (Intro/Up/Chorus/Down/Outro) – nur neu berechnen, wenn sich Track/Raster ändern
    if (t) {
        auto& cache = m_phraseCache[static_cast<std::size_t>(deck & 1)];
        const double gb = m_mixer.gridBpm(deck), gf = m_mixer.gridFirstBeat(deck);
        if (cache.track != t.get() || cache.bpm != gb || cache.first != gf) {
            cache.track = t.get();
            cache.bpm = gb;
            cache.first = gf;
            cache.list.clear();
            static const char* names[] = {"Intro", "Up", "Chorus", "Down", "Outro"};
            int count[5] = {0, 0, 0, 0, 0};
            for (const DjPhrase& p : analyzeDjPhrases(*t, gb, gf)) {
                const int k = static_cast<int>(p.kind);
                const QString name = QString::fromLatin1(names[k]);
                // wie bei rekordbox nummeriert: INTRO 1, UP 1, UP 2, CHORUS 1 …
                cache.list << QVariantMap{{QStringLiteral("start"), p.start / t->duration()},
                                          {QStringLiteral("end"), p.end / t->duration()},
                                          {QStringLiteral("kind"), name},
                                          {QStringLiteral("label"), QStringLiteral("%1 %2").arg(name.toUpper()).arg(++count[k])}};
            }
        }
        m[QStringLiteral("phrases")] = cache.list;
    }
    // Tonart nach Key Shift (und ohne Key Lock nach Tempo) – wie „aktuelle Tonart“
    const int shift = m_mixer.keyShift(deck);
    m[QStringLiteral("keyShift")] = shift;
    if (t && t->key >= 0) {
        const double tempoSemis = m_mixer.keyLock(deck) ? 0.0 : 12.0 * std::log2(1.0 + m_mixer.tempo(deck) / 100.0);
        const int now = ((t->key + shift + static_cast<int>(std::lround(tempoSemis))) % 12 + 12) % 12;
        m[QStringLiteral("keyNow")] = qstr(camelotKey(now, t->minor));
    }
    if (!t) return m;
    const double pos = m_mixer.position(deck);
    m[QStringLiteral("path")] = qstr(t->path);
    m[QStringLiteral("title")] = qstr(t->title);
    m[QStringLiteral("artist")] = qstr(t->artist);
    m[QStringLiteral("bpm")] = m_mixer.gridBpm(deck);
    m[QStringLiteral("bpmNow")] = m_mixer.currentBpm(deck);
    m[QStringLiteral("scratching")] = m_mixer.scratching(deck);
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
    if (const double gb = m_mixer.gridBpm(deck); gb > 0) {
        const double beats = (pos - m_mixer.gridFirstBeat(deck)) / (60.0 / gb);
        // Takte bis zum nächsten Hot Cue (wie die „Bars to Cue“-Anzeige)
        double next = -1;
        for (int i = 0; i < 8; ++i) {
            const double h = m_mixer.hotCue(deck, i);
            if (h > pos + 0.05 && (next < 0 || h < next)) next = h;
        }
        // Kein Hot Cue voraus: Memory-Cue, sonst nächster 16-Takt-Abschnitt (Phrase)
        const double cuePt = m_mixer.cuePoint(deck);
        if (cuePt > pos + 0.05 && (next < 0 || cuePt < next)) next = cuePt;
        bool phrase = false;
        if (next < 0) {
            const double phraseLen = 16.0 * 4.0;
            next = m_mixer.gridFirstBeat(deck) + (std::floor(beats / phraseLen) + 1.0) * phraseLen * (60.0 / gb);
            phrase = true;
        }
        m[QStringLiteral("barsToCue")] = (next - pos) / (60.0 / gb) / 4.0;
        m[QStringLiteral("barsToPhrase")] = phrase;
        const double ph = m_mixer.phaseOffset(deck);
        m[QStringLiteral("phase")] = std::isnan(ph) ? 0.0 : ph;
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
    m_fxType = std::clamp(type, 0, kDjFxCount - 1);
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

// ------------------------------------------------------------ Einstellungen

void DjController::setBpmMin(double v) {
    m_bpmMin = std::clamp(v, 40.0, 150.0);
    if (m_bpmMax < m_bpmMin * 1.9) m_bpmMax = std::min(260.0, m_bpmMin * 2.0 + 20);
    QSettings().setValue(QStringLiteral("dj/bpmMin"), m_bpmMin);
    QSettings().setValue(QStringLiteral("dj/bpmMax"), m_bpmMax);
    emit settingsChanged();
}

void DjController::setBpmMax(double v) {
    m_bpmMax = std::clamp(v, m_bpmMin * 1.9, 260.0);
    QSettings().setValue(QStringLiteral("dj/bpmMax"), m_bpmMax);
    emit settingsChanged();
}

void DjController::setOutputDevice(const QString& name) {
    if (name == m_outputDevice) return;
    m_outputDevice = name;
    QSettings().setValue(QStringLiteral("dj/outputDevice"), name);
    restartAudio();
    emit settingsChanged();
}

void DjController::setCueMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == m_cueMode) return;
    m_cueMode = mode;
    QSettings().setValue(QStringLiteral("dj/cueMode"), mode);
    restartAudio();
    emit settingsChanged();
}

void DjController::setHeadphoneDevice(const QString& name) {
    if (name == m_headDevice) return;
    m_headDevice = name;
    QSettings().setValue(QStringLiteral("dj/headphoneDevice"), name);
    restartAudio();
    emit settingsChanged();
}

void DjController::setBufferMs(int ms) {
    ms = std::clamp(ms, 5, 200);
    if (ms == m_bufferMs) return;
    m_bufferMs = ms;
    QSettings().setValue(QStringLiteral("dj/bufferMs"), ms);
    restartAudio();
    emit settingsChanged();
}

void DjController::setHeadMix(double v) {
    m_headMix = std::clamp(v, 0.0, 1.0);
    m_mixer.setHeadphones(m_headMix, m_headVolume);
    emit mixerChanged();
}

void DjController::setHeadVolume(double v) {
    m_headVolume = std::clamp(v, 0.0, 1.5);
    m_mixer.setHeadphones(m_headMix, m_headVolume);
    emit mixerChanged();
}

void DjController::setDefaultTempoRange(double v) {
    m_defaultRange = std::clamp(v, 4.0, 50.0);
    QSettings().setValue(QStringLiteral("dj/tempoRange"), m_defaultRange);
    for (int d = 0; d < 2; ++d) setTempoRange(d, m_defaultRange);
    emit settingsChanged();
}

void DjController::setAutoKeyLock(bool on) {
    m_autoKeyLock = on;
    QSettings().setValue(QStringLiteral("dj/autoKeyLock"), on);
    emit settingsChanged();
}

// ------------------------------------------------------------ Beatgrid

void DjController::applyGrid(const QString& path) {
    const Entry* e = entry(path);
    if (!e) return;
    for (int d = 0; d < 2; ++d)
        if (m_loaded[static_cast<std::size_t>(d)] == path) m_mixer.setBeatGrid(d, e->bpm, e->firstBeat);
    emit decksChanged();
}

void DjController::reanalyze(const QString& path) {
    Entry* e = entry(path);
    if (!e) return;
    e->gridEdited = false;
    e->analyzed = false;
    e->source.clear();
    m_analyzeQueue.removeAll(path);
    m_analyzeQueue.prepend(path);
    setStatus(tr("Tempo und Tonart werden neu ermittelt …"));
    analyzeNext();
}

void DjController::setTrackBpm(const QString& path, double bpm) {
    Entry* e = entry(path);
    if (!e || bpm < 20 || bpm > 400) return;
    // Raster am bisherigen ersten Schlag ausrichten
    double first = e->firstBeat;
    for (int d = 0; d < 2; ++d)
        if (m_loaded[static_cast<std::size_t>(d)] == path) first = m_mixer.gridFirstBeat(d);
    e->bpm = std::round(bpm * 100.0) / 100.0;
    e->firstBeat = std::fmod(std::max(0.0, first), 60.0 / e->bpm);
    e->gridEdited = true;
    applyGrid(path);
    saveLibrary();
    emit libraryChanged();
    setStatus(tr("Tempo gesetzt: %1 BPM").arg(e->bpm, 0, 'f', 2));
}

void DjController::multiplyBpm(int deck, double factor) {
    const QString path = m_loaded[static_cast<std::size_t>(deck & 1)];
    const double bpm = m_mixer.gridBpm(deck);
    if (path.isEmpty() || bpm <= 0) return;
    setTrackBpm(path, bpm * factor);
}

void DjController::setDeckBpm(int deck, double bpm) {
    const QString path = m_loaded[static_cast<std::size_t>(deck & 1)];
    if (!path.isEmpty()) setTrackBpm(path, bpm);
}

void DjController::setGridHere(int deck) {
    const QString path = m_loaded[static_cast<std::size_t>(deck & 1)];
    Entry* e = entry(path);
    const double bpm = m_mixer.gridBpm(deck);
    if (!e || bpm <= 0) return;
    e->bpm = bpm;
    e->firstBeat = std::fmod(m_mixer.position(deck), 60.0 / bpm);
    e->gridEdited = true;
    applyGrid(path);
    saveLibrary();
    setStatus(tr("Raster beginnt jetzt an dieser Stelle (Schlag 1)"));
}

void DjController::shiftGrid(int deck, double milliseconds) {
    const QString path = m_loaded[static_cast<std::size_t>(deck & 1)];
    Entry* e = entry(path);
    const double bpm = m_mixer.gridBpm(deck);
    if (!e || bpm <= 0) return;
    const double len = 60.0 / bpm;
    e->bpm = bpm;
    e->firstBeat = std::fmod(m_mixer.gridFirstBeat(deck) + milliseconds / 1000.0 + len * 64, len);
    e->gridEdited = true;
    applyGrid(path);
    saveLibrary();
}

double DjController::tap(int deck) {
    auto& taps = m_taps[static_cast<std::size_t>(deck & 1)];
    const qint64 now = m_clock.elapsed();
    if (!taps.empty() && now - taps.back() > 2000) taps.clear();
    taps.push_back(now);
    if (taps.size() > 16) taps.erase(taps.begin());
    if (taps.size() < 4) {
        setStatus(tr("Tap-Tempo: weiter im Takt tippen (%1/4)").arg(taps.size()));
        return 0.0;
    }
    const double avg = static_cast<double>(taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
    double bpm = 60000.0 / avg;
    while (bpm < m_bpmMin) bpm *= 2;
    while (bpm > m_bpmMax) bpm /= 2;
    bpm = std::round(bpm * 10.0) / 10.0;
    setDeckBpm(deck, bpm);
    return bpm;
}

// ------------------------------------------------------------ Jog, Loops, Vorhören

void DjController::setScratch(int deck, bool on) { m_mixer.setScratch(deck, on); }
void DjController::jogScratch(int deck, double seconds) { m_mixer.jogScratch(deck, seconds); }
void DjController::jogBend(int deck, double amount) { m_mixer.jogBend(deck, amount); }

void DjController::setPfl(int deck, bool on) {
    m_mixer.setPfl(deck, on);
    ensureAudio();
    emit decksChanged();
}

void DjController::eject(int deck) {
    if (m_mixer.playing(deck)) {
        setStatus(tr("Deck %1 spielt gerade – erst anhalten").arg((deck & 1) + 1));
        return;
    }
    m_mixer.load(deck, nullptr);
    m_loaded[static_cast<std::size_t>(deck & 1)].clear();
    emit decksChanged();
    emit libraryChanged();
}

void DjController::setLoopInOut(int deck, bool in) {
    // Loop In merkt die Stelle, Loop Out schließt den Loop auf ganze Schläge
    auto& start = m_loopIn[static_cast<std::size_t>(deck & 1)];
    const double pos = m_mixer.position(deck);
    const double bpm = m_mixer.gridBpm(deck);
    if (in || start < 0 || bpm <= 0) {
        start = pos;
        if (!in && bpm <= 0) setStatus(tr("Für Loops braucht der Track ein Tempo"));
        return;
    }
    const double len = 60.0 / bpm;
    double beats = std::max(0.25, std::round((pos - start) / len * 4.0) / 4.0);
    m_mixer.seek(deck, start);
    m_mixer.setLoop(deck, beats);
    m_mixer.seek(deck, pos);
    start = -1;
    emit decksChanged();
}

void DjController::reloop(int deck) {
    if (m_mixer.looping(deck)) m_mixer.exitLoop(deck);
    else m_mixer.setLoop(deck, m_mixer.loopBeats(deck) > 0 ? m_mixer.loopBeats(deck) : 4.0);
    emit decksChanged();
}

// ------------------------------------------------------------ Playlists, Cursor

QVariantList DjController::playlists() const {
    QVariantList list;
    for (const Playlist& p : m_playlists) list << QVariantMap{{QStringLiteral("name"), p.name}, {QStringLiteral("count"), p.paths.size()}};
    return list;
}

void DjController::setView(int view) {
    if (view == -3 && m_browsePath.isEmpty()) view = -1;
    if (view < -3 || view >= static_cast<int>(m_playlists.size())) view = -1;
    if (view == m_view) return;
    m_view = view;
    m_cursor = 0;
    emit libraryChanged();
    emit cursorChanged();
}

int DjController::createPlaylist(const QString& name) {
    QString n = name.trimmed().isEmpty() ? tr("Neue Playlist") : name.trimmed();
    m_playlists.push_back({n, {}});
    saveLibrary();
    emit libraryChanged();
    return static_cast<int>(m_playlists.size()) - 1;
}

void DjController::renamePlaylist(int index, const QString& name) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size()) || name.trimmed().isEmpty()) return;
    m_playlists[static_cast<std::size_t>(index)].name = name.trimmed();
    saveLibrary();
    emit libraryChanged();
}

void DjController::deletePlaylist(int index) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) return;
    m_playlists.erase(m_playlists.begin() + index);
    if (m_view >= static_cast<int>(m_playlists.size()) || m_view == index) m_view = -1;
    saveLibrary();
    emit libraryChanged();
}

void DjController::addToPlaylist(int index, const QStringList& paths) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) return;
    QStringList& list = m_playlists[static_cast<std::size_t>(index)].paths;
    int added = 0;
    for (const QString& p : paths)
        if (!p.isEmpty() && !list.contains(p)) {
            list << p;
            ++added;
        }
    saveLibrary();
    emit libraryChanged();
    setStatus(tr("%1 Track(s) zu „%2“ hinzugefügt").arg(added).arg(m_playlists[static_cast<std::size_t>(index)].name));
}

void DjController::removeFromPlaylist(int index, const QString& path) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) return;
    m_playlists[static_cast<std::size_t>(index)].paths.removeAll(path);
    saveLibrary();
    emit libraryChanged();
}

void DjController::clearHistory() {
    m_history.clear();
    saveLibrary();
    emit libraryChanged();
}

void DjController::setCursor(int row) {
    const int n = trackCount();
    row = n == 0 ? 0 : std::clamp(row, 0, n - 1);
    if (row == m_cursor) return;
    m_cursor = row;
    emit cursorChanged();
}

void DjController::moveCursor(int delta) { setCursor(m_cursor + delta); }

void DjController::loadCursor(int deck) {
    const QString path = pathAt(m_cursor);
    if (!path.isEmpty()) loadTrack(deck, path);
}

// ------------------------------------------------------------ Online-Suche, Editor

QString DjController::searchUrl(const QString& service, const QString& path) const {
    QString query;
    for (const Entry& e : m_entries)
        if (e.path == path) query = (e.artist.isEmpty() ? QString() : e.artist + QLatin1Char(' ')) + e.title;
    if (query.isEmpty()) query = QFileInfo(path).completeBaseName();
    const QString q = QString::fromLatin1(QUrl::toPercentEncoding(query));
    if (service == QLatin1String("beatport")) return QStringLiteral("https://www.beatport.com/search?q=") + q;
    if (service == QLatin1String("soundcloud")) return QStringLiteral("https://soundcloud.com/search?q=") + q;
    if (service == QLatin1String("spotify")) return QStringLiteral("https://open.spotify.com/search/") + q;
    if (service == QLatin1String("applemusic")) return QStringLiteral("https://music.apple.com/search?term=") + q;
    if (service == QLatin1String("tidal")) return QStringLiteral("https://listen.tidal.com/search?q=") + q;
    return QStringLiteral("https://www.youtube.com/results?search_query=") + q;
}

void DjController::openInMusicEditor(const QString& path) {
    double bpm = 0;
    for (const Entry& e : m_entries)
        if (e.path == path) bpm = e.bpm;
    emit openInEditor(path, bpm);
}

void DjController::setRating(const QString& path, int stars) {
    if (Entry* e = entry(path)) {
        e->rating = std::clamp(stars, 0, 5);
        saveLibrary();
        emit libraryChanged();
    }
}

// ------------------------------------------------------------ Tonart

void DjController::setKeyShift(int deck, int semitones) {
    m_mixer.setKeyShift(deck, semitones);
    emit decksChanged();
}

// Key Sync: kleinste Verschiebung, damit die Tonart zum anderen Deck passt
// (gleiche Tonart; Moll/Dur über die Paralleltonart)
void DjController::keySync(int deck) {
    const int other = 1 - (deck & 1);
    auto a = m_mixer.track(deck), b = m_mixer.track(other);
    if (!a || !b || a->key < 0 || b->key < 0) {
        setStatus(tr("Key Sync braucht in beiden Decks einen Track mit Tonart"));
        return;
    }
    auto effective = [&](int d, const DjTrack& t) {
        const double tempoSemis = m_mixer.keyLock(d) ? 0.0 : 12.0 * std::log2(1.0 + m_mixer.tempo(d) / 100.0);
        int k = t.key + m_mixer.keyShift(d) + static_cast<int>(std::lround(tempoSemis));
        if (t.minor) k += 3;   // auf Dur-Parallele beziehen (a-Moll ~ C-Dur)
        return ((k % 12) + 12) % 12;
    };
    const int target = effective(other, *b);
    const int mine = effective(deck, *a) - m_mixer.keyShift(deck);
    int diff = ((target - mine) % 12 + 12) % 12;
    if (diff > 6) diff -= 12;
    m_mixer.setKeyLock(deck, true);
    m_mixer.setKeyShift(deck, diff);
    setStatus(tr("Key Sync: Deck %1 um %2 Halbtöne verschoben").arg((deck & 1) + 1).arg(diff > 0 ? QStringLiteral("+%1").arg(diff) : QString::number(diff)));
    emit decksChanged();
}

// ------------------------------------------------------------ Sampler

void DjController::initSampler() {
    const QStringList saved = QSettings().value(QStringLiteral("dj/sampler")).toStringList();
    for (int i = 0; i < 8; ++i) {
        const QString path = i < saved.size() ? saved[i] : QString();
        if (!path.isEmpty() && loadSample(i, QUrl::fromLocalFile(path))) continue;
        m_samplePaths[static_cast<std::size_t>(i)].clear();
        m_mixer.setSample(i, std::make_shared<const std::vector<float>>(synthDjSample(i, m_mixer.sampleRate())));
    }
}

QVariantList DjController::sampler() const {
    QVariantList list;
    for (int i = 0; i < 8; ++i) {
        const QString& path = m_samplePaths[static_cast<std::size_t>(i)];
        list << QVariantMap{{QStringLiteral("name"), path.isEmpty() ? QString::fromLatin1(djSampleName(i)) : QFileInfo(path).completeBaseName()},
                            {QStringLiteral("custom"), !path.isEmpty()},
                            {QStringLiteral("playing"), m_samplePlaying[static_cast<std::size_t>(i)]}};
    }
    return list;
}

void DjController::triggerSample(int slot) {
    ensureAudio();
    m_mixer.triggerSample(slot);
    if (slot >= 0 && slot < 8) m_samplePlaying[static_cast<std::size_t>(slot)] = true;
    emit samplerChanged();
}

void DjController::stopSample(int slot) {
    m_mixer.stopSample(slot);
    if (slot >= 0 && slot < 8) m_samplePlaying[static_cast<std::size_t>(slot)] = false;
    emit samplerChanged();
}

bool DjController::loadSample(int slot, const QUrl& file) {
    if (slot < 0 || slot >= 8) return false;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    AudioReader reader;
    std::string error;
    if (!reader.open(str(path), m_mixer.sampleRate(), &error)) {
        setStatus(tr("Sample kann nicht geladen werden: %1").arg(qstr(error)));
        return false;
    }
    // höchstens 30 Sekunden
    const std::size_t maxFrames = static_cast<std::size_t>(m_mixer.sampleRate()) * 30;
    std::vector<float> data(maxFrames * 2);
    const std::size_t got = reader.read(data.data(), maxFrames);
    if (got == 0) return false;
    data.resize(got * 2);
    m_mixer.setSample(slot, std::make_shared<const std::vector<float>>(std::move(data)));
    m_samplePaths[static_cast<std::size_t>(slot)] = path;
    QStringList saved;
    for (const QString& p : m_samplePaths) saved << p;
    QSettings().setValue(QStringLiteral("dj/sampler"), saved);
    emit samplerChanged();
    return true;
}

void DjController::resetSample(int slot) {
    if (slot < 0 || slot >= 8) return;
    m_samplePaths[static_cast<std::size_t>(slot)].clear();
    m_mixer.setSample(slot, std::make_shared<const std::vector<float>>(synthDjSample(slot, m_mixer.sampleRate())));
    QStringList saved;
    for (const QString& p : m_samplePaths) saved << p;
    QSettings().setValue(QStringLiteral("dj/sampler"), saved);
    emit samplerChanged();
}

void DjController::setSamplerVolume(double v) {
    m_mixer.setSamplerVolume(v);
    QSettings().setValue(QStringLiteral("dj/samplerVolume"), m_mixer.samplerVolume());
    emit samplerChanged();
}

// ------------------------------------------------------------ Zoom

void DjController::setWaveZoom(double seconds) {
    seconds = std::clamp(seconds, 2.0, 32.0);
    if (std::abs(seconds - m_waveZoom) < 1e-9) return;
    m_waveZoom = seconds;
    QSettings().setValue(QStringLiteral("dj/waveZoom"), seconds);
    emit waveZoomChanged();
}

void DjController::zoomWave(int steps) { setWaveZoom(m_waveZoom * std::pow(0.8, steps)); }

// ------------------------------------------------------------ weitere DJ-Einstellungen

void DjController::setAutoGain(bool on) {
    m_autoGain = on;
    QSettings().setValue(QStringLiteral("dj/autoGain"), on);
    emit settingsChanged();
}

void DjController::setAutoStems(bool on) {
    m_autoStems = on;
    QSettings().setValue(QStringLiteral("dj/autoStems"), on);
    emit settingsChanged();
}

void DjController::setLockPlaying(bool on) {
    m_lockPlaying = on;
    QSettings().setValue(QStringLiteral("dj/lockPlaying"), on);
    emit settingsChanged();
}

void DjController::setWaveStyle(int style) {
    m_waveStyle = std::clamp(style, 0, 2);
    QSettings().setValue(QStringLiteral("dj/waveStyle"), m_waveStyle);
    emit settingsChanged();
    emit tick();
}

// ------------------------------------------------------------ Bibliothek-Ansicht

void DjController::setLibraryHeight(double h) {
    h = std::clamp(h, 100.0, 2000.0);
    if (std::abs(h - m_libraryHeight) < 0.5) return;
    m_libraryHeight = h;
    QSettings().setValue(QStringLiteral("dj/libraryHeight"), h);
    emit layoutChanged();
}

void DjController::setLibraryMax(bool on) {
    if (on == m_libraryMax) return;
    m_libraryMax = on;
    QSettings().setValue(QStringLiteral("dj/libraryMax"), on);
    emit layoutChanged();
}

void DjController::setColumns(const QStringList& columns) {
    if (columns == m_columns) return;
    m_columns = columns;
    QSettings().setValue(QStringLiteral("dj/columns"), columns);
    emit layoutChanged();
}

void DjController::toggleColumn(const QString& id) {
    QStringList c = m_columns;
    if (c.contains(id)) c.removeAll(id);
    else {
        // feste Reihenfolge beibehalten
        static const QStringList order = {QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("genre"), QStringLiteral("bpm"),
                                          QStringLiteral("key"), QStringLiteral("time"), QStringLiteral("rating"), QStringLiteral("plays"),
                                          QStringLiteral("comment"), QStringLiteral("added")};
        c << id;
        std::sort(c.begin(), c.end(), [&](const QString& a, const QString& b) { return order.indexOf(a) < order.indexOf(b); });
    }
    setColumns(c);
}

void DjController::setRowSize(int size) {
    size = std::clamp(size, 0, 2);
    if (size == m_rowSize) return;
    m_rowSize = size;
    QSettings().setValue(QStringLiteral("dj/rowSize"), size);
    emit layoutChanged();
}

void DjController::loadUrl(int deck, const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!QFileInfo(path).isFile()) return;
    importFiles({url});
    loadTrack(deck, QFileInfo(path).absoluteFilePath());
}
