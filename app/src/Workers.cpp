#include "Workers.h"

#include "TitleRenderer.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cstring>

using namespace loggx;

// ---------------------------------------------------------- PreviewWorker

PreviewWorker::PreviewWorker(QObject* parent) : QThread(parent) {}

PreviewWorker::~PreviewWorker() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();
    wait();
}

void PreviewWorker::request(std::shared_ptr<const Project> project, TimeUs time, int width, int height) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_project = std::move(project);
        m_time = time;
        m_width = width;
        m_height = height;
        m_pending = true;
    }
    m_wake.notify_one();
}

void PreviewWorker::run() {
    Compositor compositor;
    compositor.setTitleRasterizer(qtTitleRasterizer());
    std::shared_ptr<const Project> current;
    std::vector<std::uint8_t> pixels;

    for (;;) {
        std::shared_ptr<const Project> project;
        TimeUs time;
        int w, h;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_pending || m_quit; });
            if (m_quit) return;
            project = m_project;
            time = m_time;
            w = std::max(2, m_width & ~1);
            h = std::max(2, m_height & ~1);
            m_pending = false;
        }
        if (!project) continue;
        if (project != current) {
            current = project;
            compositor.setProject(current);
        }

        QElapsedTimer timer;
        timer.start();
        compositor.render(time, w, h, pixels);

        QImage image(w, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; ++y)
            std::memcpy(image.scanLine(y), pixels.data() + static_cast<std::size_t>(y) * w * 4,
                        static_cast<std::size_t>(w) * 4);
        emit renderTime(timer.nsecsElapsed() / 1e6);
        emit frameReady(image);
    }
}

// ----------------------------------------------------------- ExportWorker

ExportWorker::ExportWorker(Project project, ExportSettings settings, QObject* parent, Mode mode,
                           QString stemsExtension)
    : QThread(parent),
      m_project(std::move(project)),
      m_settings(std::move(settings)),
      m_mode(mode),
      m_stemsExt(std::move(stemsExtension)) {}

void ExportWorker::run() {
    auto progressFn = [this](double p) { emit progress(p); };
    RenderResult result;
    std::vector<std::string> files;
    if (m_mode == Audio)
        result = renderAudio(m_project, m_settings, progressFn, &m_cancel);
    else if (m_mode == Stems)
        result = renderStems(m_project, m_settings.path, m_stemsExt.toStdString(), m_settings.audioBits, &files,
                             progressFn, &m_cancel);
    else
        result = renderProject(m_project, m_settings, progressFn, &m_cancel, qtTitleRasterizer());
    QString message;
    if (result.ok && m_mode == Stems)
        message = tr("%1 Stems exportiert").arg(files.size());
    else if (result.ok && m_mode == Audio)
        message = tr("Audio-Export fertig");
    else if (result.ok)
        message = tr("Export fertig (%1 Bilder, Encoder %2)")
                      .arg(result.frames)
                      .arg(QString::fromStdString(result.videoEncoder));
    else if (result.cancelled)
        message = tr("Export abgebrochen");
    else
        message = tr("Export fehlgeschlagen: %1").arg(QString::fromStdString(result.error));
    emit done(result.ok, result.cancelled, message);
}

// ------------------------------------------------------------ MixerDevice

MixerDevice::MixerDevice(AudioMixer* mixer, QObject* parent)
    : QIODevice(parent), m_mixer(mixer) {}

void MixerDevice::start(std::int64_t positionSamples) {
    m_position = positionSamples;
    if (!isOpen()) open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void MixerDevice::stop() {
    if (isOpen()) close();
}

qint64 MixerDevice::bytesAvailable() const {
    // Der Mix endet nie von selbst; hinter dem Projektende kommt Stille.
    return 1 << 20;
}

qint64 MixerDevice::readData(char* data, qint64 maxSize) {
    const qint64 frameBytes = 2 * static_cast<qint64>(sizeof(float));
    const qint64 frames = maxSize / frameBytes;
    if (frames <= 0) return 0;
    const std::int64_t position = m_position.fetch_add(frames);
    m_mixer->render(position, reinterpret_cast<float*>(data), static_cast<std::size_t>(frames));
    return frames * frameBytes;
}

// ------------------------------------------------------------- LiveDevice

LiveDevice::LiveDevice(LiveSynth* synth, QObject* parent) : QIODevice(parent), m_synth(synth) {}

qint64 LiveDevice::readData(char* data, qint64 maxSize) {
    const qint64 frameBytes = 2 * static_cast<qint64>(sizeof(float));
    // Kleine Blöcke: geringe Verzögerung zwischen Taste und Ton.
    const qint64 frames = std::min<qint64>(maxSize / frameBytes, 512);
    if (frames <= 0) return 0;
    m_synth->render(reinterpret_cast<float*>(data), static_cast<std::size_t>(frames));
    return frames * frameBytes;
}
