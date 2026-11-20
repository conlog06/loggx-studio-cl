// Eigene Vorlagen: Projekt als Vorlage speichern (mit Medien und Vorschaubild),
// als .lxpack-Paket weitergeben und wieder einlesen, neue Projekte daraus.
// .lxpack = ZIP (unkomprimiert gespeichert) mit manifest.json, project.loggx,
// preview.png und media/.
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "AppController.h"
#include "TitleRenderer.h"

using namespace loggx;

namespace {
QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }

QString templatesRoot() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/templates");
    QDir().mkpath(dir);
    return dir;
}

QString slug(const QString& name) {
    QString s = name.trimmed();
    s.replace(QRegularExpression(QStringLiteral("[^\\w\\- ]")), QString());
    s.replace(QLatin1Char(' '), QLatin1Char('-'));
    return s.isEmpty() ? QStringLiteral("Vorlage") : s.left(60);
}

QString uniqueDir(const QString& base) {
    QString dir = templatesRoot() + QLatin1Char('/') + base;
    for (int i = 2; QFileInfo::exists(dir); ++i) dir = templatesRoot() + QLatin1Char('/') + base + QLatin1Char('-') + QString::number(i);
    return dir;
}

// ---- minimales ZIP (Methode 0 = gespeichert), CRC-32
quint32 crc32(const QByteArray& d) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (char ch : d) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void put16(QByteArray& b, quint16 v) { b.append(static_cast<char>(v & 0xFF)).append(static_cast<char>(v >> 8)); }
void put32(QByteArray& b, quint32 v) { put16(b, static_cast<quint16>(v & 0xFFFF)), put16(b, static_cast<quint16>(v >> 16)); }
quint16 get16(const QByteArray& b, qint64 at) { return static_cast<quint16>(static_cast<quint8>(b[at]) | (static_cast<quint8>(b[at + 1]) << 8)); }
quint32 get32(const QByteArray& b, qint64 at) { return get16(b, at) | (static_cast<quint32>(get16(b, at + 2)) << 16); }

bool writeZip(const QString& file, const QString& rootDir) {
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return false;
    QByteArray central;
    quint16 count = 0;
    QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QByteArray name = QDir(rootDir).relativeFilePath(path).toUtf8();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = f.readAll();
        if (data.size() > 0x7FFFFFFF) return false;   // > 2 GB pro Datei: nicht für Vorlagen gedacht
        const quint32 crc = crc32(data), offset = static_cast<quint32>(out.pos());
        QByteArray h;
        put32(h, 0x04034b50), put16(h, 20), put16(h, 0x0800), put16(h, 0), put16(h, 0), put16(h, 0);
        put32(h, crc), put32(h, static_cast<quint32>(data.size())), put32(h, static_cast<quint32>(data.size()));
        put16(h, static_cast<quint16>(name.size())), put16(h, 0);
        out.write(h + name);
        out.write(data);
        QByteArray c;
        put32(c, 0x02014b50), put16(c, 20), put16(c, 20), put16(c, 0x0800), put16(c, 0), put16(c, 0), put16(c, 0);
        put32(c, crc), put32(c, static_cast<quint32>(data.size())), put32(c, static_cast<quint32>(data.size()));
        put16(c, static_cast<quint16>(name.size())), put16(c, 0), put16(c, 0), put16(c, 0), put16(c, 0), put32(c, 0), put32(c, offset);
        central += c + name;
        ++count;
    }
    const quint32 cdOffset = static_cast<quint32>(out.pos());
    out.write(central);
    QByteArray e;
    put32(e, 0x06054b50), put16(e, 0), put16(e, 0), put16(e, count), put16(e, count);
    put32(e, static_cast<quint32>(central.size())), put32(e, cdOffset), put16(e, 0);
    out.write(e);
    return true;
}

// Liest nur gespeicherte (unkomprimierte) Einträge – so schreibt LXStudio Pakete
bool readZip(const QString& file, const QString& destDir, QString* error) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return *error = QObject::tr("Datei lässt sich nicht öffnen"), false;
    const QByteArray z = in.readAll();
    qint64 eocd = -1;
    for (qint64 i = z.size() - 22; i >= std::max<qint64>(0, z.size() - 65557); --i)
        if (get32(z, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) return *error = QObject::tr("Kein gültiges Vorlagenpaket"), false;
    const quint16 n = get16(z, eocd + 10);
    qint64 p = get32(z, eocd + 16);
    const QString root = QDir(destDir).canonicalPath();
    for (quint16 i = 0; i < n; ++i) {
        if (p + 46 > z.size() || get32(z, p) != 0x02014b50) return *error = QObject::tr("Paket beschädigt"), false;
        const quint16 method = get16(z, p + 10), nameLen = get16(z, p + 28), extraLen = get16(z, p + 30), commentLen = get16(z, p + 32);
        const quint32 crc = get32(z, p + 16), size = get32(z, p + 20), local = get32(z, p + 42);
        const QString name = QString::fromUtf8(z.mid(p + 46, nameLen));
        p += 46 + nameLen + extraLen + commentLen;
        if (method != 0) return *error = QObject::tr("Paket ist komprimiert – nur mit LXStudio erstellte Pakete werden unterstützt"), false;
        // Keine Pfade außerhalb des Zielordners (../, absolute Pfade)
        const QString target = QDir::cleanPath(destDir + QLatin1Char('/') + name);
        if (name.contains(QLatin1String("..")) || QDir::isAbsolutePath(name) || !target.startsWith(root + QLatin1Char('/')))
            return *error = QObject::tr("Ungültiger Pfad im Paket"), false;
        if (local + 30 > static_cast<quint64>(z.size())) return *error = QObject::tr("Paket beschädigt"), false;
        const qint64 data = local + 30 + get16(z, local + 26) + get16(z, local + 28);
        const QByteArray bytes = z.mid(data, size);
        if (bytes.size() != static_cast<qint64>(size) || crc32(bytes) != crc) return *error = QObject::tr("Paket beschädigt (Prüfsumme)"), false;
        QDir().mkpath(QFileInfo(target).path());
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) return *error = QObject::tr("Kann nicht schreiben"), false;
    }
    return true;
}
}  // namespace

QVariantList AppController::templates() const {
    QVariantList list;
    QDir root(templatesRoot());
    for (const QFileInfo& d : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
        QFile mf(d.filePath() + QStringLiteral("/manifest.json"));
        if (!mf.open(QIODevice::ReadOnly) || !QFileInfo::exists(d.filePath() + QStringLiteral("/project.loggx"))) continue;
        const QJsonObject m = QJsonDocument::fromJson(mf.readAll()).object();
        const QString preview = d.filePath() + QStringLiteral("/preview.png");
        list << QVariantMap{{QStringLiteral("dir"), d.filePath()},
                            {QStringLiteral("name"), m.value(QStringLiteral("name")).toString(d.fileName())},
                            {QStringLiteral("description"), m.value(QStringLiteral("description")).toString()},
                            {QStringLiteral("author"), m.value(QStringLiteral("author")).toString()},
                            {QStringLiteral("format"), m.value(QStringLiteral("format")).toString()},
                            {QStringLiteral("music"), m.value(QStringLiteral("music")).toBool()},
                            {QStringLiteral("preview"), QFileInfo::exists(preview) ? QUrl::fromLocalFile(preview).toString() : QString()}};
    }
    return list;
}

bool AppController::saveAsTemplate(const QString& name, const QString& description, const QString& author) {
    if (name.trimmed().isEmpty()) return false;
    const QString dir = uniqueDir(slug(name));
    QDir().mkpath(dir + QStringLiteral("/media"));
    capturePluginStates();
    Project p = m_project;
    p.name = str(name.trimmed());
    // Medien in die Vorlage kopieren (gleiche Namen bekommen eine Nummer)
    QSet<QString> used;
    for (auto& a : p.assets) {
        if (!QFileInfo::exists(qstr(a.path))) continue;
        QString file = QFileInfo(qstr(a.path)).fileName();
        for (int i = 2; used.contains(file); ++i) file = QFileInfo(qstr(a.path)).completeBaseName() + QStringLiteral("-%1.").arg(i) + QFileInfo(qstr(a.path)).suffix();
        used.insert(file);
        const QString target = dir + QStringLiteral("/media/") + file;
        if (!QFile::copy(qstr(a.path), target)) continue;
        a.path = str(target);
        a.sourcePath.clear();
    }
    std::string error;
    if (!p.saveFile(str(dir + QStringLiteral("/project.loggx")), &error)) {
        QDir(dir).removeRecursively();
        setStatus(tr("Vorlage konnte nicht gespeichert werden: %1").arg(qstr(error)));
        return false;
    }
    // Vorschaubild aus der Mitte
    Compositor comp;
    comp.setTitleRasterizer(qtTitleRasterizer());
    comp.setProject(std::make_shared<const Project>(p));
    const int pw = 480, ph = std::max(2, 480 * p.height / std::max(1, p.width));
    std::vector<std::uint8_t> px;
    comp.render(p.duration() / 2, pw, ph, px);
    QImage(px.data(), pw, ph, pw * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888).save(dir + QStringLiteral("/preview.png"));
    QJsonObject m{{QStringLiteral("name"), name.trimmed()},
                  {QStringLiteral("description"), description},
                  {QStringLiteral("author"), author},
                  {QStringLiteral("music"), p.mode == ProjectMode::Music},
                  {QStringLiteral("format"), p.mode == ProjectMode::Music ? tr("%1 BPM").arg(p.bpm) : QStringLiteral("%1×%2").arg(p.width).arg(p.height)},
                  {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                  {QStringLiteral("app"), QStringLiteral("LXStudio")},
                  {QStringLiteral("version"), 1}};
    QFile mf(dir + QStringLiteral("/manifest.json"));
    mf.open(QIODevice::WriteOnly);
    mf.write(QJsonDocument(m).toJson());
    emit templatesChanged();
    setStatus(tr("Vorlage „%1“ gespeichert").arg(name.trimmed()));
    return true;
}

bool AppController::newFromUserTemplate(const QString& dir) {
    Project loaded;
    std::string error;
    if (!Project::loadFile(str(dir + QStringLiteral("/project.loggx")), loaded, &error)) {
        setStatus(tr("Vorlage lässt sich nicht öffnen: %1").arg(qstr(error)));
        return false;
    }
    resetState();
    m_project = std::move(loaded);
    m_project.name = "Unbenannt";
    m_filePath.clear();
    m_beatGrid = musicMode();
    emit gridChanged();
    publish();
    setDirty(true);
    emit fileChanged();
    setShowStart(false);
    setStatus(tr("Neues Projekt aus Vorlage"));
    return true;
}

bool AppController::exportTemplate(const QString& dir, const QUrl& url) {
    QString file = url.toLocalFile();
    if (file.isEmpty()) return false;
    if (!file.endsWith(QStringLiteral(".lxpack"), Qt::CaseInsensitive)) file += QStringLiteral(".lxpack");
    const bool ok = writeZip(file, dir);
    setStatus(ok ? tr("Vorlagenpaket gespeichert – kann mit anderen geteilt werden") : tr("Vorlagenpaket konnte nicht gespeichert werden"));
    return ok;
}

bool AppController::importTemplatePack(const QUrl& url) {
    const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString dir = uniqueDir(slug(QFileInfo(file).completeBaseName()));
    QDir().mkpath(dir);
    QString error;
    if (!readZip(file, dir, &error) || !QFileInfo::exists(dir + QStringLiteral("/project.loggx"))) {
        QDir(dir).removeRecursively();
        setStatus(tr("Vorlagenpaket: %1").arg(error.isEmpty() ? tr("keine Vorlage enthalten") : error));
        return false;
    }
    if (!QFileInfo::exists(dir + QStringLiteral("/manifest.json"))) {
        QFile mf(dir + QStringLiteral("/manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(QJsonObject{{QStringLiteral("name"), QFileInfo(file).completeBaseName()}}).toJson());
    }
    emit templatesChanged();
    setStatus(tr("Vorlage „%1“ hinzugefügt").arg(QFileInfo(file).completeBaseName()));
    return true;
}

bool AppController::deleteTemplate(const QString& dir) {
    if (!QDir::cleanPath(dir).startsWith(QDir::cleanPath(templatesRoot()) + QLatin1Char('/'))) return false;
    const bool ok = QDir(dir).removeRecursively();
    emit templatesChanged();
    return ok;
}
