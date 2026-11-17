#include "ModelManager.h"

#include <QCoreApplication>

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

ModelManager::ModelManager(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString ModelManager::folder() const {
    return m_folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/models")
                              : m_folder;
}

// Mit dem Installer (Vollversion) mitgelieferte Modelle liegen neben dem
// Programm (Windows: „models“, macOS: Contents/Resources/models)
QString ModelManager::bundledPath(const loggx::StemModelInfo& model) const {
    if (!m_folder.isEmpty()) return {};   // Tests: nur der eigene Ordner
    const QString file = QString::fromStdString(model.file);
    for (const QString& dir : {QCoreApplication::applicationDirPath() + QStringLiteral("/models"),
                               QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/models")}) {
        const QFileInfo info(dir + QLatin1Char('/') + file);
        if (info.exists() && info.size() == model.bytes) return info.absoluteFilePath();
    }
    return {};
}

QString ModelManager::pathFor(const loggx::StemModelInfo& model) const {
    const QString bundled = bundledPath(model);
    if (!bundled.isEmpty()) return bundled;
    return folder() + QLatin1Char('/') + QString::fromStdString(model.file);
}

bool ModelManager::isReady(const loggx::StemModelInfo& model) const {
    const QFileInfo info(pathFor(model));
    return info.exists() && info.size() == model.bytes;
}

void ModelManager::download(const loggx::StemModelInfo& model, Done done) {
    if (m_reply) {
        done(false, tr("Es läuft schon ein Download"));
        return;
    }
    if (!QDir().mkpath(folder())) {
        done(false, tr("Ordner kann nicht angelegt werden: %1").arg(folder()));
        return;
    }
    m_model = model;
    m_done = std::move(done);
    m_hash.reset();
    m_file.setFileName(pathFor(model) + QStringLiteral(".part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finish(false, tr("Datei kann nicht geschrieben werden: %1").arg(m_file.fileName()));
        return;
    }
    const QUrl url = m_base.isValid() ? QUrl(m_base.toString() + QLatin1Char('/') + QString::fromStdString(model.file))
                                      : QUrl(QString::fromStdString(model.url));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LoggX-Studio"));
    m_reply = m_net->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray data = m_reply->readAll();
        m_hash.addData(data);
        m_file.write(data);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit progress(got, total > 0 ? total : m_model.bytes);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QByteArray rest = m_reply->readAll();
        m_hash.addData(rest);
        m_file.write(rest);
        m_file.close();
        const bool netOk = m_reply->error() == QNetworkReply::NoError;
        const QString netError = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (!netOk) {
            finish(false, tr("Download fehlgeschlagen: %1").arg(netError));
            return;
        }
        if (QString::fromLatin1(m_hash.result().toHex()) != QString::fromStdString(m_model.sha256)) {
            finish(false, tr("Download beschädigt (Prüfsumme stimmt nicht) – bitte noch einmal versuchen"));
            return;
        }
        QFile::remove(pathFor(m_model));
        if (!QFile::rename(m_file.fileName(), pathFor(m_model))) {
            finish(false, tr("Modell kann nicht gespeichert werden"));
            return;
        }
        finish(true, QString());
    });
}

void ModelManager::cancel() {
    if (m_reply) m_reply->abort();
}

void ModelManager::finish(bool ok, const QString& error) {
    if (m_file.isOpen()) m_file.close();
    if (!ok) QFile::remove(m_file.fileName());
    Done done = std::move(m_done);
    m_done = nullptr;
    if (done) done(ok, error);
}
