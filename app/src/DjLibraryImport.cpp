// Fremde Bibliotheken übernehmen: rekordbox (Datei → Exportieren → „Collection
// im XML-Format“) und Apple Music/iTunes („Library.xml“). Übernommen werden
// Titel, Interpret, Album, Genre, Bewertung, Tempo/Beatgrid, Tonart, Hot Cues
// und Playlists. Streaming-Titel ohne lokale Datei werden übersprungen.
#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

#include "DjController.h"

namespace {

// „file://localhost/C:/Musik/a.mp3“ (rekordbox) bzw. „file:///Users/…“ → Pfad
QString pathFromLocation(QString loc) {
    if (loc.startsWith(QLatin1String("file://localhost"), Qt::CaseInsensitive)) loc = loc.mid(16);
    else if (loc.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) loc = loc.mid(7);
    else return {};
    QString path = QUrl::fromPercentEncoding(loc.toUtf8());
    static const QRegularExpression drive(QStringLiteral("^/[A-Za-z]:/"));
    if (drive.match(path).hasMatch()) path = path.mid(1);
    return path;
}

// Tonart „Am“, „F#m“, „Ebm“, „C“ oder Camelot „8A“ → Grundton (0 = C), Moll
bool parseKey(const QString& text, int& tonic, bool& minor) {
    const QString t = text.trimmed();
    static const QRegularExpression camelot(QStringLiteral("^(1[0-2]|[1-9])([ABab])$"));
    if (const auto m = camelot.match(t); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        minor = m.captured(2).toUpper() == QLatin1String("A");
        // Camelot 8A = a-Moll, 8B = C-Dur; jede Stufe = Quinte
        const int base = minor ? 9 : 0;
        tonic = ((base + (n - 8) * 7) % 12 + 12) % 12;
        return true;
    }
    static const QRegularExpression musical(QStringLiteral("^([A-Ga-g])([#b♯♭]?)(m|min|moll)?$"));
    const auto m = musical.match(t);
    if (!m.hasMatch()) return false;
    static const int steps[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    tonic = steps[m.captured(1).toUpper().at(0).unicode() - 'A'];
    const QString acc = m.captured(2);
    if (acc == QLatin1String("#") || acc == QStringLiteral("♯")) tonic = (tonic + 1) % 12;
    if (acc == QLatin1String("b") || acc == QStringLiteral("♭")) tonic = (tonic + 11) % 12;
    minor = !m.captured(3).isEmpty();
    return true;
}

// Einfache plist → QVariant (dict → QVariantMap, array → QVariantList)
QVariant readPlistValue(QXmlStreamReader& xml) {
    const QString tag = xml.name().toString();
    if (tag == QLatin1String("dict")) {
        QVariantMap map;
        QString key;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("key")) key = xml.readElementText();
            else map.insert(key, readPlistValue(xml));
        }
        return map;
    }
    if (tag == QLatin1String("array")) {
        QVariantList list;
        while (xml.readNextStartElement()) list << readPlistValue(xml);
        return list;
    }
    if (tag == QLatin1String("true") || tag == QLatin1String("false")) {
        xml.skipCurrentElement();
        return tag == QLatin1String("true");
    }
    if (tag == QLatin1String("integer")) return xml.readElementText().toLongLong();
    if (tag == QLatin1String("real")) return xml.readElementText().toDouble();
    return xml.readElementText();
}

}  // namespace

DjController::Entry& DjController::addEntry(const QString& path) {
    if (Entry* e = entry(path)) return *e;
    Entry e;
    e.path = path;
    e.title = QFileInfo(path).completeBaseName();
    e.added = QDateTime::currentMSecsSinceEpoch();
    m_entries.push_back(e);
    return m_entries.back();
}

int DjController::importLibraryXml(const QUrl& url) {
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Datei kann nicht gelesen werden: %1").arg(file));
        return -1;
    }
    const QByteArray head = f.peek(4096);
    f.close();
    QString error;
    int count = -1;
    if (head.contains("DJ_PLAYLISTS")) count = importRekordbox(file, &error);
    else if (head.contains("<plist")) count = importItunes(file, &error);
    else error = tr("Unbekanntes Format – erwartet wird eine rekordbox-XML oder die „Library.xml“ von Apple Music/iTunes");
    if (count < 0) {
        setStatus(error);
        return -1;
    }
    // Geladene Decks übernehmen Raster und Hot Cues sofort
    for (int d = 0; d < 2; ++d) {
        const QString path = m_loaded[static_cast<std::size_t>(d)];
        if (const Entry* e = entry(path)) {
            applyGrid(path);
            for (int i = 0; i < 8; ++i)
                if (e->hot[static_cast<std::size_t>(i)] >= 0) m_mixer.setHotCuePoint(d, i, e->hot[static_cast<std::size_t>(i)]);
        }
    }
    saveLibrary();
    emit libraryChanged();
    analyzeNext();
    return count;
}

int DjController::importRekordbox(const QString& file, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    QXmlStreamReader xml(&f);
    QHash<QString, QString> byId;   // TrackID → Pfad
    int tracks = 0, missing = 0, lists = 0;
    QStringList folder;              // Ordner-Pfad der Playlists
    std::vector<int> nodeKinds;      // je offenem NODE: 0 Ordner, 1 Playlist
    int currentList = -1, keyType = 0;
    Entry* current = nullptr;
    bool inCollection = false, inPlaylists = false, tempoSeen = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            const auto a = xml.attributes();
            if (name == QLatin1String("COLLECTION")) inCollection = true;
            else if (name == QLatin1String("PLAYLISTS")) inPlaylists = true;
            else if (inCollection && name == QLatin1String("TRACK")) {
                current = nullptr;
                tempoSeen = false;
                const QString path = pathFromLocation(a.value(QLatin1String("Location")).toString());
                if (path.isEmpty()) continue;   // Streaming o. Ä.
                if (!QFileInfo::exists(path)) ++missing;
                Entry& e = addEntry(path);
                current = &e;
                byId.insert(a.value(QLatin1String("TrackID")).toString(), path);
                auto text = [&](const char* key) { return a.value(QLatin1String(key)).toString().trimmed(); };
                if (!text("Name").isEmpty()) e.title = text("Name");
                if (!text("Artist").isEmpty()) e.artist = text("Artist");
                if (!text("Album").isEmpty()) e.album = text("Album");
                if (!text("Genre").isEmpty()) e.genre = text("Genre");
                if (!text("Comments").isEmpty()) e.comment = text("Comments");
                if (const double total = text("TotalTime").toDouble(); total > 0) e.duration = total;
                if (const int r = text("Rating").toInt(); r > 0) e.rating = std::clamp((r + 25) / 51, 0, 5);
                if (const QDate d = QDate::fromString(text("DateAdded"), Qt::ISODate); d.isValid())
                    e.added = d.startOfDay().toMSecsSinceEpoch();
                int tonic = -1;
                bool minor = false;
                if (parseKey(text("Tonality"), tonic, minor)) {
                    e.key = tonic;
                    e.minor = minor;
                }
                if (const double bpm = text("AverageBpm").toDouble(); bpm > 0) {
                    e.bpm = bpm;
                    e.analyzed = true;
                    e.gridEdited = true;
                }
                e.source = QStringLiteral("rekordbox");
                ++tracks;
            } else if (inCollection && current && name == QLatin1String("TEMPO")) {
                // Erster TEMPO-Eintrag: Start des Rasters (Battito = Schlag im Takt)
                const double bpm = a.value(QLatin1String("Bpm")).toDouble();
                if (!tempoSeen && bpm > 0) {
                    tempoSeen = true;
                    const double start = a.value(QLatin1String("Inizio")).toDouble();
                    const int beat = std::clamp(a.value(QLatin1String("Battito")).toInt(), 1, 4);
                    const double len = 60.0 / bpm;
                    current->bpm = bpm;
                    current->firstBeat = std::fmod(start + ((5 - beat) % 4) * len, len);
                    current->analyzed = true;
                    current->gridEdited = true;
                }
            } else if (inCollection && current && name == QLatin1String("POSITION_MARK")) {
                const int num = a.value(QLatin1String("Num")).toInt();
                const int type = a.value(QLatin1String("Type")).toInt();
                if (num >= 0 && num < 8 && type != 4) current->hot[static_cast<std::size_t>(num)] = a.value(QLatin1String("Start")).toDouble();
            } else if (inPlaylists && name == QLatin1String("NODE")) {
                const int type = a.value(QLatin1String("Type")).toInt();
                const QString n = a.value(QLatin1String("Name")).toString();
                if (type == 1) {
                    QStringList full = folder;
                    full << n;
                    full.removeAll(QStringLiteral("ROOT"));
                    m_playlists.push_back({full.join(QStringLiteral(" / ")), {}});
                    currentList = static_cast<int>(m_playlists.size()) - 1;
                    keyType = a.value(QLatin1String("KeyType")).toInt();
                    ++lists;
                } else {
                    folder << n;
                }
                nodeKinds.push_back(type == 1 ? 1 : 0);
            } else if (inPlaylists && currentList >= 0 && name == QLatin1String("TRACK")) {
                const QString key = a.value(QLatin1String("Key")).toString();
                const QString path = keyType == 1 ? pathFromLocation(key) : byId.value(key);
                if (!path.isEmpty()) m_playlists[static_cast<std::size_t>(currentList)].paths << path;
            }
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("COLLECTION")) inCollection = false;
            else if (name == QLatin1String("PLAYLISTS")) inPlaylists = false;
            else if (inCollection && name == QLatin1String("TRACK")) current = nullptr;
            else if (inPlaylists && name == QLatin1String("NODE") && !nodeKinds.empty()) {
                if (nodeKinds.back() == 1) currentList = -1;
                else if (!folder.isEmpty()) folder.removeLast();
                nodeKinds.pop_back();
            }
        }
    }
    if (xml.hasError()) {
        if (error) *error = tr("rekordbox-XML fehlerhaft: %1").arg(xml.errorString());
        return -1;
    }
    // Ohne Tonart/Dauer: nachträglich analysieren (Raster bleibt aus rekordbox)
    for (const Entry& e : m_entries)
        if ((e.key < 0 || e.duration <= 0 || e.bpm <= 0) && !m_analyzeQueue.contains(e.path)) m_analyzeQueue << e.path;
    setStatus(tr("rekordbox: %1 Tracks, %2 Playlists übernommen%3")
                  .arg(tracks)
                  .arg(lists)
                  .arg(missing > 0 ? tr(" (%1 Dateien nicht gefunden)").arg(missing) : QString()));
    return tracks;
}

int DjController::importItunes(const QString& file, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    QXmlStreamReader xml(&f);
    QVariantMap root;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("plist")) {
            if (xml.readNextStartElement()) root = readPlistValue(xml).toMap();
            break;
        }
    }
    if (xml.hasError() || root.isEmpty()) {
        if (error) *error = tr("Library.xml fehlerhaft: %1").arg(xml.errorString());
        return -1;
    }
    QHash<QString, QString> byId;
    int tracks = 0, streams = 0, lists = 0;
    const QVariantMap all = root.value(QStringLiteral("Tracks")).toMap();
    for (auto it = all.begin(); it != all.end(); ++it) {
        const QVariantMap t = it.value().toMap();
        const QString path = pathFromLocation(t.value(QStringLiteral("Location")).toString());
        if (path.isEmpty()) {
            ++streams;   // nur in der Cloud/Streaming: keine Datei zum Abspielen
            continue;
        }
        if (t.value(QStringLiteral("Kind")).toString().contains(QLatin1String("Video"), Qt::CaseInsensitive)) continue;
        Entry& e = addEntry(path);
        byId.insert(it.key(), path);
        if (const QString v = t.value(QStringLiteral("Name")).toString(); !v.isEmpty()) e.title = v;
        if (const QString v = t.value(QStringLiteral("Artist")).toString(); !v.isEmpty()) e.artist = v;
        if (const QString v = t.value(QStringLiteral("Album")).toString(); !v.isEmpty()) e.album = v;
        if (const QString v = t.value(QStringLiteral("Genre")).toString(); !v.isEmpty()) e.genre = v;
        if (const QString v = t.value(QStringLiteral("Comments")).toString(); !v.isEmpty()) e.comment = v;
        if (const double ms = t.value(QStringLiteral("Total Time")).toDouble(); ms > 0) e.duration = ms / 1000.0;
        if (const int r = t.value(QStringLiteral("Rating")).toInt(); r > 0) e.rating = std::clamp(r / 20, 0, 5);
        if (const int c = t.value(QStringLiteral("Play Count")).toInt(); c > e.plays) e.plays = c;
        // BPM aus iTunes ist oft nur ganzzahlig/ungenau: erst nach der Analyse vergleichen
        if (e.bpm <= 0) e.bpm = t.value(QStringLiteral("BPM")).toDouble();
        if (e.source.isEmpty()) e.source = QStringLiteral("itunes");
        if (!e.analyzed && !m_analyzeQueue.contains(path)) m_analyzeQueue << path;
        ++tracks;
    }
    for (const QVariant& v : root.value(QStringLiteral("Playlists")).toList()) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("Master")).toBool() || p.contains(QStringLiteral("Distinguished Kind")) ||
            p.value(QStringLiteral("Folder")).toBool() || p.value(QStringLiteral("Visible"), true).toBool() == false)
            continue;
        Playlist list{p.value(QStringLiteral("Name")).toString(), {}};
        for (const QVariant& item : p.value(QStringLiteral("Playlist Items")).toList()) {
            const QString path = byId.value(item.toMap().value(QStringLiteral("Track ID")).toString());
            if (!path.isEmpty()) list.paths << path;
        }
        if (list.paths.isEmpty()) continue;
        m_playlists.push_back(list);
        ++lists;
    }
    setStatus(tr("Apple Music/iTunes: %1 Tracks, %2 Playlists übernommen%3")
                  .arg(tracks)
                  .arg(lists)
                  .arg(streams > 0 ? tr(" – %1 reine Streaming-Titel übersprungen (keine Datei)").arg(streams) : QString()));
    return tracks;
}
