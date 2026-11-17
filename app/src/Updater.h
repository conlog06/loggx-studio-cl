// Automatische Updates über GitHub Releases: prüft beim Start (einmal am Tag),
// lädt das passende Paket (Windows: Setup.exe, macOS: .dmg), prüft die
// Prüfsumme und installiert. Für QML als „updater“ verfügbar.
#pragma once

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
#include <QNetworkReply>

class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)

public:
    explicit Updater(QObject* parent = nullptr);

    QString currentVersion() const;
    bool available() const { return m_available; }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString pageUrl() const { return m_page; }
    bool checking() const { return m_checking; }
    bool downloading() const { return m_reply != nullptr && m_downloading; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    bool canInstall() const { return m_asset.isValid(); }

    Q_INVOKABLE void check(bool manual = true);
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    void checkOnStartup();

    // Für Tests: Versionen vergleichen („0.11.0-build.42“ > „0.11.0-build.7“)
    static bool isNewer(const QString& remoteTag, const QString& version, int build);
    void setApiUrl(const QUrl& url) { m_api = url; }

signals:
    void changed();
    void quitRequested();   // Installer läuft: App beenden

private:
    void handleReleases(const QByteArray& json, bool manual);
    void finishDownload();
    void setStatus(const QString& s);

    QNetworkAccessManager* m_net;
    QPointer<QNetworkReply> m_reply;
    QUrl m_api;
    QUrl m_asset;
    QString m_assetName, m_digest;
    bool m_available = false, m_checking = false, m_downloading = false, m_autoCheck = true;
    QString m_latest, m_notes, m_page, m_status;
    double m_progress = 0;
    QFile m_file;
};
