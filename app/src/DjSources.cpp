// Quellen für die DJ-Bibliothek: Cloud-Ordner (Google Drive, iCloud Drive,
// OneDrive, Dropbox), der Musik-Ordner und eigene Ordner (USB, NAS …).
// Die Cloud-Dienste werden über ihre Desktop-Apps eingebunden: deren
// Sync-Ordner werden automatisch gefunden und lassen sich wie Ordner
// durchsuchen; Dateien, die nur online liegen, lädt das System beim Öffnen.
#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#include "DjController.h"

namespace {

bool isAudio(const QFileInfo& f) {
    static const QStringList audio = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("aiff"),
                                      QStringLiteral("aif"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("ogg"),
                                      QStringLiteral("opus"), QStringLiteral("alac"), QStringLiteral("wma")};
    return audio.contains(f.suffix().toLower());
}

void addIfDir(QVariantList& list, QStringList& seen, const QString& name, const QString& path, const QString& kind) {
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || seen.contains(clean) || !QFileInfo(clean).isDir()) return;
    seen << clean;
    list << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("path"), clean}, {QStringLiteral("kind"), kind}, {QStringLiteral("custom"), false}};
}

}  // namespace

QVariantList DjController::sources() const {
    QVariantList list;
    QStringList seen;
    const QString home = QDir::homePath();
    addIfDir(list, seen, tr("Musik"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QStringLiteral("music"));
    // Google Drive (Desktop-App): Windows eigenes Laufwerk „G:\\Meine Ablage“, macOS ~/Library/CloudStorage
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        for (const QString& sub : {QStringLiteral("My Drive"), QStringLiteral("Meine Ablage")})
            addIfDir(list, seen, QStringLiteral("Google Drive"), vol.rootPath() + QLatin1Char('/') + sub, QStringLiteral("gdrive"));
    }
    const QDir cloud(home + QStringLiteral("/Library/CloudStorage"));
    for (const QString& d : cloud.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.startsWith(QLatin1String("GoogleDrive"))) {
            for (const QString& sub : {QStringLiteral("My Drive"), QStringLiteral("Meine Ablage")})
                addIfDir(list, seen, QStringLiteral("Google Drive"), cloud.filePath(d) + QLatin1Char('/') + sub, QStringLiteral("gdrive"));
        } else if (d.startsWith(QLatin1String("OneDrive"))) {
            addIfDir(list, seen, QStringLiteral("OneDrive"), cloud.filePath(d), QStringLiteral("onedrive"));
        } else if (d.startsWith(QLatin1String("Dropbox"))) {
            addIfDir(list, seen, QStringLiteral("Dropbox"), cloud.filePath(d), QStringLiteral("dropbox"));
        }
    }
    addIfDir(list, seen, QStringLiteral("Google Drive"), home + QStringLiteral("/Google Drive"), QStringLiteral("gdrive"));
    // iCloud Drive: Windows-App „iCloudDrive“, macOS „Mobile Documents“
    addIfDir(list, seen, QStringLiteral("iCloud Drive"), home + QStringLiteral("/iCloudDrive"), QStringLiteral("icloud"));
    addIfDir(list, seen, QStringLiteral("iCloud Drive"), home + QStringLiteral("/Library/Mobile Documents/com~apple~CloudDocs"), QStringLiteral("icloud"));
    // OneDrive, Dropbox
    addIfDir(list, seen, QStringLiteral("OneDrive"), qEnvironmentVariable("OneDrive"), QStringLiteral("onedrive"));
    addIfDir(list, seen, QStringLiteral("OneDrive"), home + QStringLiteral("/OneDrive"), QStringLiteral("onedrive"));
    addIfDir(list, seen, QStringLiteral("Dropbox"), home + QStringLiteral("/Dropbox"), QStringLiteral("dropbox"));
    addIfDir(list, seen, tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QStringLiteral("folder"));
    // Eigene Ordner
    for (const QString& p : QSettings().value(QStringLiteral("dj/sources")).toStringList()) {
        const QString clean = QDir::cleanPath(p);
        if (seen.contains(clean)) continue;
        seen << clean;
        list << QVariantMap{{QStringLiteral("name"), QFileInfo(clean).fileName().isEmpty() ? clean : QFileInfo(clean).fileName()},
                            {QStringLiteral("path"), clean},
                            {QStringLiteral("kind"), QStringLiteral("folder")},
                            {QStringLiteral("custom"), true},
                            {QStringLiteral("missing"), !QFileInfo(clean).isDir()}};
    }
    return list;
}

void DjController::addSource(const QUrl& folder) {
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (!QFileInfo(path).isDir()) return;
    QStringList list = QSettings().value(QStringLiteral("dj/sources")).toStringList();
    if (!list.contains(path)) list << path;
    QSettings().setValue(QStringLiteral("dj/sources"), list);
    emit sourcesChanged();
    browse(path);
}

void DjController::removeSource(const QString& path) {
    QStringList list = QSettings().value(QStringLiteral("dj/sources")).toStringList();
    list.removeAll(QDir::cleanPath(path));
    QSettings().setValue(QStringLiteral("dj/sources"), list);
    if (m_browseRoot == QDir::cleanPath(path)) setView(-1);
    emit sourcesChanged();
}

void DjController::browse(const QString& path) {
    const QString clean = QDir::cleanPath(path);
    if (!QFileInfo(clean).isDir()) {
        setStatus(tr("Ordner nicht erreichbar: %1").arg(clean));
        return;
    }
    // Wurzel merken (Quelle), damit „Hoch“ nicht darüber hinaus geht
    bool insideRoot = !m_browseRoot.isEmpty() && (clean == m_browseRoot || clean.startsWith(m_browseRoot + QLatin1Char('/')));
    if (!insideRoot) m_browseRoot = clean;
    m_browsePath = clean;
    m_browseItems.clear();
    const QDir dir(clean);
    for (const QFileInfo& f : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (f.fileName().startsWith(QLatin1Char('.'))) continue;
        m_browseItems << QVariantMap{{QStringLiteral("name"), f.fileName()}, {QStringLiteral("path"), f.absoluteFilePath()}, {QStringLiteral("folder"), true}};
    }
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!isAudio(f)) continue;
        const QString abs = f.absoluteFilePath();
        QVariantMap m{{QStringLiteral("name"), f.completeBaseName()}, {QStringLiteral("path"), abs}, {QStringLiteral("folder"), false},
                      {QStringLiteral("size"), QStringLiteral("%1 MB").arg(f.size() / 1048576.0, 0, 'f', 1)}};
        // Schon in der Sammlung? Dann Tempo/Tonart zeigen
        for (const Entry& e : m_entries)
            if (e.path == abs) {
                m[QStringLiteral("bpm")] = e.bpm;
                m[QStringLiteral("key")] = QString::fromStdString(loggx::camelotKey(e.key, e.minor));
                m[QStringLiteral("inLibrary")] = true;
            }
        m_browseItems << m;
    }
    if (m_view != -3) {
        m_view = -3;
        emit libraryChanged();
    }
    emit browseChanged();
}

void DjController::browseUp() {
    if (m_browsePath.isEmpty() || m_browsePath == m_browseRoot) return;
    browse(QFileInfo(m_browsePath).absolutePath());
}

QString DjController::browseTitle() const {
    if (m_browsePath.isEmpty()) return {};
    if (m_browsePath == m_browseRoot) {
        for (const QVariant& v : sources())
            if (v.toMap().value(QStringLiteral("path")).toString() == m_browseRoot) return v.toMap().value(QStringLiteral("name")).toString();
    }
    return QFileInfo(m_browsePath).fileName();
}

void DjController::importPaths(const QStringList& paths) {
    QList<QUrl> urls;
    for (const QString& p : paths) urls << QUrl::fromLocalFile(p);
    importFiles(urls);
    if (m_view == -3) browse(m_browsePath);   // „in Sammlung“-Markierung aktualisieren
}
