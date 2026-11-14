#include "MediaCache.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "loggx/media/Media.h"

using namespace loggx;

MediaCache::MediaCache(QObject* parent) : QThread(parent) {}

MediaCache::~MediaCache() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    wait();
}

void MediaCache::request(const Project& project) {
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& a : project.assets) {
            if (m_known.count(a.path)) continue;
            m_known[a.path] = true;
            m_queue.push_back({a});
            added = true;
        }
    }
    if (added) m_wake.notify_one();
}

std::shared_ptr<const MediaCache::Entry> MediaCache::entry(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(path);
    return it == m_entries.end() ? nullptr : it->second;
}

void MediaCache::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || !m_queue.empty(); });
            if (m_quit) return;
            job = m_queue.front();
            m_queue.pop_front();
        }
        auto e = std::make_shared<Entry>();
        const MediaAsset& a = job.asset;

        // Vorschaubilder zuerst (schneller sichtbar), höchstens 120 Stück.
        if (a.hasVideo && a.width > 0 && a.height > 0) {
            VideoReader reader;
            if (reader.open(a.path)) {
                const int th = 54;
                const int tw = std::max(8, static_cast<int>(std::lround(th * static_cast<double>(a.width) / a.height)));
                const TimeUs step = a.isStill ? kSecond
                                              : std::max<TimeUs>(kSecond, a.duration / 120);
                const TimeUs end = a.isStill ? 1 : a.duration;
                std::vector<std::uint8_t> px;
                for (TimeUs t = 0; t < end; t += step) {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (m_quit) return;
                    }
                    if (!reader.frameAt(t, tw, th, px)) break;
                    QImage img(tw, th, QImage::Format_RGBA8888);
                    for (int y = 0; y < th; ++y)
                        std::memcpy(img.scanLine(y), px.data() + static_cast<std::size_t>(y) * tw * 4,
                                    static_cast<std::size_t>(tw) * 4);
                    e->thumbs.emplace_back(t, img);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries[a.path] = std::make_shared<Entry>(*e);
        }
        emit updated();

        // Wellenform: Spitzenwert je 10 ms, bei niedriger Abtastrate gelesen.
        if (a.hasAudio) {
            AudioReader reader;
            constexpr int rate = 8000;
            if (reader.open(a.path, rate)) {
                const std::size_t per = rate / 100;
                std::vector<float> buf(per * 2 * 100);
                std::size_t got;
                while ((got = reader.read(buf.data(), buf.size() / 2)) > 0) {
                    for (std::size_t i = 0; i < got; i += per) {
                        float pk = 0.0f;
                        for (std::size_t j = i; j < std::min(got, i + per); ++j)
                            pk = std::max({pk, std::abs(buf[j * 2]), std::abs(buf[j * 2 + 1])});
                        e->peaks.push_back(pk);
                    }
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_quit) return;
                }
            }
        }
        e->done = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_entries[a.path] = e;
        }
        emit updated();
    }
}
