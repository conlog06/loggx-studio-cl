#include "Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>

#ifndef LOGGX_VERSION
#define LOGGX_VERSION "0.0.0"
#endif
#ifndef LOGGX_BUILD
#define LOGGX_BUILD 0
#endif

namespace {
const char* kApi = "https://api.github.com/repos/conlog06/loggx-studio-cl/releases?per_page=15";

// „v0.11.0-build.42“ → (0.11.0, 42); „v1.0.0“ → (1.0.0, 0)
bool parseTag(const QString& tag, QVersionNumber& version, int& build) {
    static const QRegularExpression re(QStringLiteral("^v?(\\d+(?:\\.\\d+){1,3})(?:-build\\.(\\d+))?$"));
    const auto m = re.match(tag.trimmed());
    if (!m.hasMatch()) return false;
    version = QVersionNumber::fromString(m.captured(1));
    build = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
    return true;
}
}  // namespace

Updater::Updater(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)), m_api(QString::fromLatin1(kApi)) {
    m_autoCheck = QSettings().value(QStringLiteral("update/autoCheck"), true).toBool();
}

QString Updater::currentVersion() const {
    const int build = LOGGX_BUILD;
    return build > 0 ? QStringLiteral("%1 (Build %2)").arg(QStringLiteral(LOGGX_VERSION)).arg(build) : QStringLiteral(LOGGX_VERSION);
}

bool Updater::isNewer(const QString& remoteTag, const QString& version, int build) {
    QVersionNumber rv;
    int rb = 0;
    if (!parseTag(remoteTag, rv, rb)) return false;
    const QVersionNumber lv = QVersionNumber::fromString(version);
    const int cmp = QVersionNumber::compare(rv, lv);
    if (cmp != 0) return cmp > 0;
    // gleiche Version: höherer Build ist neuer (lokale Builds = 0 bekommen keine Builds derselben Version)
    return build > 0 && rb > build;
}

void Updater::setAutoCheck(bool on) {
    m_autoCheck = on;
    QSettings().setValue(QStringLiteral("update/autoCheck"), on);
    emit changed();
}

void Updater::setStatus(const QString& s) {
    m_status = s;
    emit changed();
}

void Updater::checkOnStartup() {
    if (!m_autoCheck) return;
    const qint64 last = QSettings().value(QStringLiteral("update/lastCheck")).toLongLong();
    if (QDateTime::currentSecsSinceEpoch() - last < 6 * 3600) return;   // höchstens alle 6 Stunden
    QTimer::singleShot(5000, this, [this] { check(false); });
}

void Updater::check(bool manual) {
    if (m_checking) return;
    m_checking = true;
    setStatus(tr("Suche nach Updates …"));
    QNetworkRequest req(m_api);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LXStudio/%1").arg(QStringLiteral(LOGGX_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        m_checking = false;
        QSettings().setValue(QStringLiteral("update/lastCheck"), QDateTime::currentSecsSinceEpoch());
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(manual ? tr("Update-Prüfung fehlgeschlagen: %1").arg(reply->errorString()) : QString());
            return;
        }
        handleReleases(reply->readAll(), manual);
    });
}

void Updater::handleReleases(const QByteArray& json, bool manual) {
    const QJsonArray releases = QJsonDocument::fromJson(json).array();
    m_available = false;
    m_asset.clear();
    for (const QJsonValue& v : releases) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("draft")).toBool()) continue;
        const QString tag = r.value(QStringLiteral("tag_name")).toString();
        if (!isNewer(tag, QStringLiteral(LOGGX_VERSION), LOGGX_BUILD)) continue;
        // Neueste passende Version (Liste ist nach Datum sortiert)
        m_available = true;
        m_latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
        m_notes = r.value(QStringLiteral("body")).toString().left(1500);
        m_page = r.value(QStringLiteral("html_url")).toString();
        for (const QJsonValue& a : r.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
#if defined(Q_OS_WIN)
            const bool fits = name.endsWith(QLatin1String("Setup.exe"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
            const bool fits = name.endsWith(QLatin1String(".dmg"), Qt::CaseInsensitive);
#else
            const bool fits = false;
#endif
            if (!fits) continue;
            m_asset = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            m_assetName = name;
            m_digest = asset.value(QStringLiteral("digest")).toString();   // „sha256:…“ (falls vorhanden)
        }
        break;
    }
    if (m_available) setStatus(tr("Update verfügbar: %1").arg(m_latest));
    else setStatus(manual ? tr("LXStudio ist aktuell (%1)").arg(currentVersion()) : QString());
}

void Updater::install() {
    if (!m_available) return;
    if (!m_asset.isValid()) {   // z. B. Linux: Release-Seite öffnen
        QDesktopServices::openUrl(QUrl(m_page));
        return;
    }
    if (m_reply) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/LXStudio-Update");
    QDir().mkpath(dir);
    m_file.setFileName(dir + QLatin1Char('/') + m_assetName);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(tr("Update kann nicht gespeichert werden"));
        return;
    }
    QNetworkRequest req(m_asset);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LXStudio/%1").arg(QStringLiteral(LOGGX_VERSION)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloading = true;
    m_progress = 0;
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file.write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? static_cast<double>(got) / static_cast<double>(total) : 0.0;
        emit changed();
    });
    connect(m_reply, &QNetworkReply::finished, this, &Updater::finishDownload);
    setStatus(tr("Update wird geladen …"));
}

void Updater::cancel() {
    if (m_reply) m_reply->abort();
}

void Updater::finishDownload() {
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    m_downloading = false;
    reply->deleteLater();
    m_file.write(reply->readAll());
    m_file.close();
    if (reply->error() != QNetworkReply::NoError) {
        m_file.remove();
        setStatus(reply->error() == QNetworkReply::OperationCanceledError ? tr("Update abgebrochen") : tr("Download fehlgeschlagen: %1").arg(reply->errorString()));
        return;
    }
    // Prüfsumme von GitHub vergleichen, falls angegeben
    if (m_digest.startsWith(QLatin1String("sha256:"))) {
        QFile f(m_file.fileName());
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly)) hash.addData(&f);
        if (QString::fromLatin1(hash.result().toHex()) != m_digest.mid(7)) {
            m_file.remove();
            setStatus(tr("Update beschädigt (Prüfsumme falsch) – bitte erneut versuchen"));
            return;
        }
    }
    const QString path = m_file.fileName();
#if defined(Q_OS_WIN)
    // Setup still installieren und danach LXStudio wieder starten (/UPDATE)
    if (QProcess::startDetached(path, {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART"),
                                       QStringLiteral("/CLOSEAPPLICATIONS"), QStringLiteral("/UPDATE")})) {
        setStatus(tr("Update wird installiert – LXStudio startet gleich neu"));
        emit quitRequested();
        return;
    }
    setStatus(tr("Installer kann nicht gestartet werden"));
#elif defined(Q_OS_MACOS)
    // Neues Programm aus dem Abbild kopieren, sobald LXStudio beendet ist, dann neu starten
    const QString bundle = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).absolutePath();
    const QString script = QStringLiteral(
        "pid=%1; while kill -0 $pid 2>/dev/null; do sleep 0.5; done; "
        "mnt=$(mktemp -d); hdiutil attach -nobrowse -quiet -mountpoint \"$mnt\" \"%2\" && "
        "app=$(ls -d \"$mnt\"/*.app | head -1) && "
        "(ditto \"$app\" \"%3.new\" && rm -rf \"%3\" && mv \"%3.new\" \"%3\") ; "
        "hdiutil detach -quiet \"$mnt\"; xattr -dr com.apple.quarantine \"%3\" 2>/dev/null; open \"%3\"")
        .arg(QCoreApplication::applicationPid())
        .arg(path, bundle);
    if (QFileInfo(QFileInfo(bundle).absolutePath()).isWritable() && QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script})) {
        setStatus(tr("Update wird installiert – LXStudio startet gleich neu"));
        emit quitRequested();
        return;
    }
    // Kein Schreibrecht: Abbild öffnen, Nutzer zieht die App selbst
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    setStatus(tr("Update geladen – LXStudio in „Programme“ ziehen und ersetzen"));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}
