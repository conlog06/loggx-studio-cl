// KI-Modelle: bei Bedarf herunterladen (mit Prüfsumme) und lokal ablegen.
#pragma once

#include <functional>

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QNetworkReply>
#include <QUrl>

#include "loggx/engine/Engine.h"

class QNetworkAccessManager;

class ModelManager : public QObject {
    Q_OBJECT
public:
    explicit ModelManager(QObject* parent = nullptr);
    QString folder() const;
    QString pathFor(const loggx::StemModelInfo& model) const;
    bool isReady(const loggx::StemModelInfo& model) const;
    bool busy() const { return m_reply != nullptr; }
    // Für Tests/Spiegel: statt der eingetragenen Adresse <base>/<Datei> laden.
    void setBaseUrl(const QUrl& base) { m_base = base; }
    void setFolder(const QString& folder) { m_folder = folder; }

    using Done = std::function<void(bool ok, const QString& error)>;
    void download(const loggx::StemModelInfo& model, Done done);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);

private:
    void finish(bool ok, const QString& error);
    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    loggx::StemModelInfo m_model;
    Done m_done;
    QUrl m_base;
    QString m_folder;
};
