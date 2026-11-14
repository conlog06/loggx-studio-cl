// Berechnet im Hintergrund Wellenformen und Vorschaubilder für die Timeline.
#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QImage>
#include <QThread>

#include "loggx/core/Project.h"

class MediaCache : public QThread {
    Q_OBJECT
public:
    struct Entry {
        // Spitzenwerte (0..1) im Abstand von peakStep.
        std::vector<float> peaks;
        loggx::TimeUs peakStep = 10'000;
        // Vorschaubilder mit ihrem Zeitpunkt im Material.
        std::vector<std::pair<loggx::TimeUs, QImage>> thumbs;
        bool done = false;
    };

    explicit MediaCache(QObject* parent = nullptr);
    ~MediaCache() override;

    // Stellt alle Dateien des Projekts in die Warteschlange (bekannte nicht).
    void request(const loggx::Project& project);
    std::shared_ptr<const Entry> entry(const std::string& path) const;

signals:
    void updated();

protected:
    void run() override;

private:
    struct Job {
        loggx::MediaAsset asset;
    };
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_queue;
    std::map<std::string, std::shared_ptr<const Entry>> m_entries;
    std::map<std::string, bool> m_known;
    bool m_quit = false;
};
