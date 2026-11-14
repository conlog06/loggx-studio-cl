#include "loggx/media/Media.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "FFmpegUtil.h"

namespace loggx {

namespace {

// Standbild-Formate, die FFmpeg als "Video mit einem Frame" öffnet.
bool isStillFormat(const AVFormatContext* fmt, const AVStream* st) {
    const std::string name = fmt->iformat && fmt->iformat->name ? fmt->iformat->name : "";
    if (name.find("image2") != std::string::npos || name.find("_pipe") != std::string::npos)
        return true;
    return st->nb_frames == 1 && fmt->duration <= 0;
}

bool openDecoder(AVFormatContext* fmt, int streamIndex, AVCodecContext** out, std::string* error) {
    const AVStream* st = fmt->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        if (error) *error = "Kein Decoder für dieses Format vorhanden";
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;
    int rc = avcodec_parameters_to_context(ctx, st->codecpar);
    if (rc >= 0) {
        ctx->pkt_timebase = st->time_base;
        ctx->thread_count = 0;   // FFmpeg wählt die Thread-Zahl
        rc = avcodec_open2(ctx, codec, nullptr);
    }
    if (rc < 0) {
        if (error) *error = ff::errorText(rc);
        avcodec_free_context(&ctx);
        return false;
    }
    *out = ctx;
    return true;
}

}  // namespace

// ------------------------------------------------------------------ probe

bool probeMedia(const std::string& path, MediaAsset& out, std::string* error) {
    ff::quietLogs();
    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (rc < 0) {
        if (error) *error = ff::errorText(rc);
        return false;
    }
    rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) {
        if (error) *error = ff::errorText(rc);
        avformat_close_input(&fmt);
        return false;
    }

    MediaAsset a;
    a.path = path;
    const auto fileName = std::filesystem::path(std::u8string(path.begin(), path.end()))
                              .filename().u8string();
    a.name.assign(fileName.begin(), fileName.end());

    const int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int au = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (v >= 0 && !(fmt->streams[v]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        const AVStream* st = fmt->streams[v];
        a.hasVideo = true;
        a.width = st->codecpar->width;
        a.height = st->codecpar->height;
        const AVRational sar = st->sample_aspect_ratio;
        if (sar.num > 0 && sar.den > 0 && sar.num != sar.den)
            a.width = static_cast<int>(av_rescale(a.width, sar.num, sar.den));
        const AVRational fr = av_guess_frame_rate(fmt, const_cast<AVStream*>(st), nullptr);
        a.fps = fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 0.0;
        a.isStill = isStillFormat(fmt, st);
    }
    a.hasAudio = au >= 0;
    if (!a.isStill && fmt->duration > 0)
        a.duration = av_rescale_q(fmt->duration, AVRational{1, AV_TIME_BASE}, ff::kMicro);
    avformat_close_input(&fmt);

    if (!a.hasVideo && !a.hasAudio) {
        if (error) *error = "Datei enthält weder Video noch Audio";
        return false;
    }
    if (!a.isStill && a.duration <= 0) {
        if (error) *error = "Dauer der Datei ist unbekannt";
        return false;
    }
    a.id = out.id;
    out = std::move(a);
    return true;
}

// ------------------------------------------------------------ VideoReader

struct VideoReader::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVFrame* frame = nullptr;     // zuletzt dekodiertes Bild
    AVFrame* scratch = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    int stream = -1;
    TimeUs startOffset = 0;       // start_time des Streams
    TimeUs framePts = 0;
    TimeUs frameEnd = 0;
    TimeUs nominalFrameDur = 33'333;
    bool haveFrame = false;
    bool eof = false;
    bool still = false;
    bool alpha = false;

    // Cache des zuletzt skalierten Bilds.
    TimeUs cachePts = -1;
    int cacheW = 0, cacheH = 0;
    std::vector<std::uint8_t> cache;

    ~Impl() { close(); }

    void close() {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&frame);
        av_frame_free(&scratch);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        haveFrame = false;
    }

    // Dekodiert das nächste Bild nach `scratch`. false = Dateiende oder Fehler.
    bool decodeNext() {
        for (;;) {
            int rc = avcodec_receive_frame(dec, scratch);
            if (rc >= 0) return true;
            if (rc != AVERROR(EAGAIN)) return false;   // EOF oder Fehler

            for (;;) {
                rc = av_read_frame(fmt, pkt);
                if (rc < 0) {
                    avcodec_send_packet(dec, nullptr);   // Decoder leeren
                    break;
                }
                if (pkt->stream_index == stream) {
                    rc = avcodec_send_packet(dec, pkt);
                    av_packet_unref(pkt);
                    if (rc >= 0 || rc == AVERROR(EAGAIN)) break;
                    // Defekte Pakete überspringen und weiterlesen.
                    continue;
                }
                av_packet_unref(pkt);
            }
        }
    }

    void acceptScratch() {
        const AVRational tb = fmt->streams[stream]->time_base;
        std::int64_t pts = scratch->best_effort_timestamp;
        TimeUs ptsUs = pts == AV_NOPTS_VALUE ? frameEnd
                                             : av_rescale_q(pts, tb, ff::kMicro) - startOffset;
        TimeUs dur = scratch->duration > 0 ? av_rescale_q(scratch->duration, tb, ff::kMicro)
                                           : nominalFrameDur;
        if (dur <= 0) dur = nominalFrameDur;
        av_frame_unref(frame);
        av_frame_move_ref(frame, scratch);
        framePts = ptsUs;
        frameEnd = ptsUs + dur;
        haveFrame = true;
    }

    void seekTo(TimeUs t) {
        const AVRational tb = fmt->streams[stream]->time_base;
        const std::int64_t ts = av_rescale_q(std::max<TimeUs>(0, t) + startOffset, ff::kMicro, tb);
        av_seek_frame(fmt, stream, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
        haveFrame = false;
        eof = false;
    }

    // Stellt sicher, dass `frame` das Bild für Zeitpunkt t enthält.
    bool advanceTo(TimeUs t) {
        if (still) {
            if (haveFrame) return true;
            if (!decodeNext()) return false;
            acceptScratch();
            return true;
        }
        // Rückwärts oder weiter Sprung nach vorn: neu positionieren.
        if (!haveFrame || t < framePts || t > frameEnd + 2 * kSecond) seekTo(t);

        while (!haveFrame || t >= frameEnd) {
            if (eof) break;                       // hinter dem Ende: letztes Bild halten
            if (!decodeNext()) {
                eof = true;
                break;
            }
            acceptScratch();
        }
        return haveFrame;
    }
};

VideoReader::VideoReader() : d(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;

bool VideoReader::open(const std::string& path, std::string* error) {
    ff::quietLogs();
    d->close();
    int rc = avformat_open_input(&d->fmt, path.c_str(), nullptr, nullptr);
    if (rc >= 0) rc = avformat_find_stream_info(d->fmt, nullptr);
    if (rc < 0) {
        if (error) *error = ff::errorText(rc);
        d->close();
        return false;
    }
    d->stream = av_find_best_stream(d->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (d->stream < 0) {
        if (error) *error = "Keine Videospur";
        d->close();
        return false;
    }
    if (!openDecoder(d->fmt, d->stream, &d->dec, error)) {
        d->close();
        return false;
    }
    AVStream* st = d->fmt->streams[d->stream];
    d->frame = av_frame_alloc();
    d->scratch = av_frame_alloc();
    d->pkt = av_packet_alloc();
    d->startOffset = st->start_time == AV_NOPTS_VALUE
                         ? 0 : av_rescale_q(st->start_time, st->time_base, ff::kMicro);
    const AVRational fr = av_guess_frame_rate(d->fmt, st, nullptr);
    if (fr.num > 0 && fr.den > 0)
        d->nominalFrameDur = av_rescale(kSecond, fr.den, fr.num);
    d->still = isStillFormat(d->fmt, st);
    const AVPixFmtDescriptor* desc =
        av_pix_fmt_desc_get(static_cast<AVPixelFormat>(st->codecpar->format));
    d->alpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    d->eof = false;
    d->cachePts = -1;
    return true;
}

bool VideoReader::isOpen() const { return d->fmt != nullptr; }
bool VideoReader::hasAlpha() const { return d->alpha; }

bool VideoReader::frameAt(TimeUs t, int w, int h, std::vector<std::uint8_t>& rgba) {
    if (!d->fmt || w <= 0 || h <= 0) return false;
    if (!d->advanceTo(t)) return false;

    const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
    if (d->cachePts == d->framePts && d->cacheW == w && d->cacheH == h &&
        d->cache.size() == bytes) {
        rgba = d->cache;
        return true;
    }

    const AVFrame* f = d->frame;
    d->sws = sws_getCachedContext(d->sws, f->width, f->height,
                                  static_cast<AVPixelFormat>(f->format), w, h,
                                  AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!d->sws) return false;
    // Farbmatrix und Pegelbereich der Quelle beachten. Ohne Angabe gilt wie
    // üblich BT.709 ab 720 Zeilen, sonst BT.601.
    int matrix = f->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    switch (f->colorspace) {
        case AVCOL_SPC_BT709: matrix = SWS_CS_ITU709; break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: matrix = SWS_CS_ITU601; break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: matrix = SWS_CS_BT2020; break;
        default: break;
    }
    sws_setColorspaceDetails(d->sws, sws_getCoefficients(matrix),
                             f->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    d->cache.resize(bytes);
    std::uint8_t* dst[4] = {d->cache.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {w * 4, 0, 0, 0};
    sws_scale(d->sws, f->data, f->linesize, 0, f->height, dst, dstStride);
    d->cachePts = d->framePts;
    d->cacheW = w;
    d->cacheH = h;
    rgba = d->cache;
    return true;
}

// ------------------------------------------------------------ AudioReader

struct AudioReader::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwrContext* swr = nullptr;
    int stream = -1;
    int outRate = 48000;
    TimeUs startOffset = 0;
    bool eof = false;

    // Nach einem Seek: Samples vor der Zielposition verwerfen.
    bool skipping = false;
    std::int64_t targetSample = 0;     // Zielposition in Ausgabe-Samples
    std::int64_t nextSample = 0;       // Position des nächsten dekodierten Samples

    std::vector<float> fifo;           // interleaved stereo
    std::size_t fifoPos = 0;
    std::vector<float> convert;

    ~Impl() { close(); }

    void close() {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
    }

    bool setupResampler() {
        swr_free(&swr);
        // Null-initialisieren: av_channel_layout_copy() gibt das Ziel vorher
        // frei – mit zufälligem Inhalt wäre das ein ungültiger Zeiger.
        AVChannelLayout out{};
        av_channel_layout_default(&out, 2);
        AVChannelLayout in{};
        if (dec->ch_layout.nb_channels > 0 && dec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_copy(&in, &dec->ch_layout);
        else
            av_channel_layout_default(&in, std::max(1, dec->ch_layout.nb_channels));
        int rc = swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_FLT, outRate, &in,
                                     dec->sample_fmt, dec->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in);
        if (rc < 0 || !swr) return false;
        return swr_init(swr) >= 0;
    }

    void pushConverted(const float* data, int frames, std::int64_t firstSample) {
        int skip = 0;
        if (skipping) {
            if (firstSample + frames <= targetSample) return;       // komplett davor
            skip = static_cast<int>(std::max<std::int64_t>(0, targetSample - firstSample));
            skipping = false;
        }
        fifo.insert(fifo.end(), data + skip * 2, data + frames * 2);
    }

    // Dekodiert weiter, bis neue Samples im FIFO liegen. false = Dateiende.
    bool decodeMore() {
        for (;;) {
            int rc = avcodec_receive_frame(dec, frame);
            if (rc >= 0) {
                const AVRational tb = fmt->streams[stream]->time_base;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE && skipping) {
                    // Nur direkt nach einem Seek dem Zeitstempel vertrauen;
                    // danach wird lückenlos weitergezählt.
                    const TimeUs us =
                        av_rescale_q(frame->best_effort_timestamp, tb, ff::kMicro) - startOffset;
                    nextSample = av_rescale(us, outRate, kSecond);
                }
                const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
                convert.resize(static_cast<std::size_t>(std::max(maxOut, 0)) * 2);
                std::uint8_t* outPtr = reinterpret_cast<std::uint8_t*>(convert.data());
                const int n = swr_convert(swr, &outPtr, maxOut,
                                          const_cast<const std::uint8_t**>(frame->extended_data),
                                          frame->nb_samples);
                av_frame_unref(frame);
                if (n > 0) {
                    const std::size_t before = fifo.size();
                    pushConverted(convert.data(), n, nextSample);
                    nextSample += n;
                    if (fifo.size() > before) return true;
                }
                continue;
            }
            if (rc != AVERROR(EAGAIN)) return false;

            for (;;) {
                rc = av_read_frame(fmt, pkt);
                if (rc < 0) {
                    avcodec_send_packet(dec, nullptr);
                    break;
                }
                if (pkt->stream_index == stream) {
                    rc = avcodec_send_packet(dec, pkt);
                    av_packet_unref(pkt);
                    if (rc >= 0 || rc == AVERROR(EAGAIN)) break;
                    continue;
                }
                av_packet_unref(pkt);
            }
        }
    }
};

AudioReader::AudioReader() : d(std::make_unique<Impl>()) {}
AudioReader::~AudioReader() = default;

bool AudioReader::open(const std::string& path, int sampleRate, std::string* error) {
    ff::quietLogs();
    d->close();
    d->outRate = sampleRate;
    int rc = avformat_open_input(&d->fmt, path.c_str(), nullptr, nullptr);
    if (rc >= 0) rc = avformat_find_stream_info(d->fmt, nullptr);
    if (rc < 0) {
        if (error) *error = ff::errorText(rc);
        d->close();
        return false;
    }
    d->stream = av_find_best_stream(d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (d->stream < 0) {
        if (error) *error = "Keine Audiospur";
        d->close();
        return false;
    }
    if (!openDecoder(d->fmt, d->stream, &d->dec, error) || !d->setupResampler()) {
        if (error && error->empty()) *error = "Audio kann nicht umgerechnet werden";
        d->close();
        return false;
    }
    const AVStream* st = d->fmt->streams[d->stream];
    d->frame = av_frame_alloc();
    d->pkt = av_packet_alloc();
    d->startOffset = st->start_time == AV_NOPTS_VALUE
                         ? 0 : av_rescale_q(st->start_time, st->time_base, ff::kMicro);
    d->eof = false;
    d->skipping = false;
    d->nextSample = 0;
    d->fifo.clear();
    d->fifoPos = 0;
    return true;
}

bool AudioReader::isOpen() const { return d->fmt != nullptr; }

void AudioReader::seek(TimeUs t) {
    if (!d->fmt) return;
    t = std::max<TimeUs>(0, t);
    const AVRational tb = d->fmt->streams[d->stream]->time_base;
    av_seek_frame(d->fmt, d->stream, av_rescale_q(t + d->startOffset, ff::kMicro, tb),
                  AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(d->dec);
    d->setupResampler();
    d->fifo.clear();
    d->fifoPos = 0;
    d->eof = false;
    d->skipping = true;
    d->targetSample = av_rescale(t, d->outRate, kSecond);
    d->nextSample = 0;
}

std::size_t AudioReader::read(float* interleaved, std::size_t frames) {
    if (!d->fmt) return 0;
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t available = (d->fifo.size() - d->fifoPos) / 2;
        if (available == 0) {
            d->fifo.clear();
            d->fifoPos = 0;
            if (d->eof || !d->decodeMore()) {
                d->eof = true;
                break;
            }
            continue;
        }
        const std::size_t n = std::min(available, frames - done);
        std::memcpy(interleaved + done * 2, d->fifo.data() + d->fifoPos, n * 2 * sizeof(float));
        d->fifoPos += n * 2;
        done += n;
    }
    return done;
}

}  // namespace loggx
