#include "Parallel.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace loggx::detail {

int parallelCores() {
    // LOGGX_THREADS erlaubt Tests mit fester Thread-Zahl.
    static const int cores = [] {
        if (const char* env = std::getenv("LOGGX_THREADS")) {
            const int n = std::atoi(env);
            if (n > 0) return n;
        }
        return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }();
    return cores;
}

namespace {

class Pool {
public:
    Pool() {
        const int n = parallelCores() - 1;   // der Aufrufer rechnet selbst mit
        for (int i = 0; i < n; ++i) m_workers.emplace_back([this] { loop(); });
    }
    ~Pool() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        for (auto& t : m_workers) t.join();
    }

    void run(int tasks, const std::function<void(int)>& fn) {
        std::unique_lock<std::mutex> busy(m_busy, std::try_to_lock);
        if (!busy.owns_lock() || m_workers.empty()) {
            for (int i = 0; i < tasks; ++i) fn(i);
            return;
        }
        Job job;
        job.fn = &fn;
        job.tasks = tasks;
        job.pending.store(tasks);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_job = &job;
            ++m_generation;
        }
        m_wake.notify_all();
        work(job);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_done.wait(lock, [&] { return job.pending.load() == 0 && job.active == 0; });
        m_job = nullptr;
    }

private:
    struct Job {
        const std::function<void(int)>* fn = nullptr;
        int tasks = 0;
        std::atomic<int> next{0};
        std::atomic<int> pending{0};
        int active = 0;   // Helfer, die gerade mitrechnen (unter m_mutex)
    };

    void work(Job& job) {
        for (;;) {
            const int i = job.next.fetch_add(1);
            if (i >= job.tasks) return;
            (*job.fn)(i);
            if (job.pending.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_done.notify_all();
            }
        }
    }

    void loop() {
        std::uint64_t seen = 0;
        for (;;) {
            Job* job = nullptr;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [&] { return m_quit || (m_job && m_generation != seen); });
                if (m_quit) return;
                seen = m_generation;
                job = m_job;
                ++job->active;
            }
            work(*job);
            std::lock_guard<std::mutex> lock(m_mutex);
            if (--job->active == 0 && job->pending.load() == 0) m_done.notify_all();
        }
    }

    std::vector<std::thread> m_workers;
    std::mutex m_busy;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    Job* m_job = nullptr;
    std::uint64_t m_generation = 0;
    bool m_quit = false;
};

}  // namespace

void runParallel(int tasks, const std::function<void(int)>& fn) {
    static Pool pool;
    pool.run(tasks, fn);
}

}  // namespace loggx::detail
