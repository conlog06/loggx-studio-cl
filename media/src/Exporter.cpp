#include "loggx/media/Media.h"

#include <algorithm>
#include <cstring>

#include "FFmpegUtil.h"

namespace loggx {

namespace {

// Reihenfolge: bester Software-Encoder, dann die Hardware-Encoder der
// Plattformen (macOS, NVIDIA, Intel, AMD, Windows), zuletzt der immer
// vorhandene MPEG-4-Encoder.
const char* const kVideoEncoders[] = {"libx264", "h264_videotoolbox", "h264_nvenc", "h264_qsv",
                                      "h264_amf", "h264_mf", "libopenh264", "mpeg4"};

AVPixelFormat pickPixelFormat(const AVCodecContext* ctx, const AVCodec* codec) {
    const AVPixelFormat* list = nullptr;
    int count = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* configs = nullptr;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs,
                                     &count) >= 0)
        list = static_cast<const AVPixelFormat*>(configs);
#else
    (void)ctx;
    list = codec->pix_fmts;
    for (const AVPixelFormat* p = list; p && *p != AV_PIX_FMT_NONE; ++p) ++count;
#endif
    if (!list || count <= 0) return AV_PIX_FMT_YUV420P;
    for (int i = 0; i < count; ++i)
        if (list[i] == AV_PIX_FMT_YUV420P) return list[i];
    for (int i = 0; i < count; ++i)
        if (list[i] == AV_PIX_FMT_NV12) return list[i];
    return list[0];
}

std::int64_t defaultBitrate(int w, int h, double fps) {
    // Rund 0,1 Bit pro Pixel und Bild: 1080p30 ≈ 6 Mbit/s, 4K30 ≈ 25 Mbit/s.
    const double bits = static_cast<double>(w) * h * fps * 0.1;
    return static_cast<std::int64_t>(std::clamp(bits, 1.0e6, 120.0e6));
}

}  // namespace

struct Exporter::Impl {
    ExportSettings s;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* venc = nullptr;
    AVCodecContext* aenc = nullptr;
    AVStream* vstream = nullptr;
    AVStream* astream = nullptr;
    AVFrame* vframe = nullptr;
    AVFrame* aframe = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    std::int64_t videoFrames = 0;
    std::int64_t audioSamples = 0;
    std::vector<float> audioFifo;   // interleaved stereo
    std::string encoderName;
    bool headerWritten = false;
    bool finished = false;

    ~Impl() { close(); }

    void close() {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&vframe);
        av_frame_free(&aframe);
        av_packet_free(&pkt);
        avcodec_free_context(&venc);
        avcodec_free_context(&aenc);
        if (fmt) {
            if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
            avformat_free_context(fmt);
            fmt = nullptr;
        }
    }

    bool openVideoEncoder(const char* name) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name);
        if (!codec) return false;
        AVCodecContext* c = avcodec_alloc_context3(codec);
        if (!c) return false;
        c->width = s.width;
        c->height = s.height;
        c->time_base = AVRational{s.fpsDen, s.fpsNum};
        c->framerate = AVRational{s.fpsNum, s.fpsDen};
        c->pix_fmt = pickPixelFormat(c, codec);
        c->bit_rate = s.videoBitrate;
        c->gop_size = std::max(1, s.fpsNum / s.fpsDen) * 2;   // Keyframe alle 2 s
        c->max_b_frames = 2;
        c->color_range = AVCOL_RANGE_MPEG;
        c->colorspace = AVCOL_SPC_BT709;
        c->color_primaries = AVCOL_PRI_BT709;
        c->color_trc = AVCOL_TRC_BT709;
        if (std::strcmp(name, "mpeg4") == 0) c->max_b_frames = 0;
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        AVDictionary* opts = nullptr;
        if (std::strcmp(name, "libx264") == 0) av_dict_set(&opts, "preset", "medium", 0);
        const int rc = avcodec_open2(c, codec, &opts);
        av_dict_free(&opts);
        if (rc < 0) {
            avcodec_free_context(&c);
            return false;
        }
        venc = c;
        encoderName = name;
        return true;
    }

    bool openAudioEncoder(std::string* error) {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            if (error) *error = "Kein AAC-Encoder vorhanden";
            return false;
        }
        aenc = avcodec_alloc_context3(codec);
        aenc->sample_rate = s.sampleRate;
        aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aenc->bit_rate = s.audioBitrate;
        aenc->time_base = AVRational{1, s.sampleRate};
        av_channel_layout_default(&aenc->ch_layout, 2);
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER) aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int rc = avcodec_open2(aenc, codec, nullptr);
        if (rc < 0) {
            if (error) *error = "AAC-Encoder: " + ff::errorText(rc);
            return false;
        }
        return true;
    }

    bool drain(AVCodecContext* enc, AVStream* st) {
        for (;;) {
            const int rc = avcodec_receive_packet(enc, pkt);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return true;
            if (rc < 0) return false;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            const int w = av_interleaved_write_frame(fmt, pkt);
            av_packet_unref(pkt);
            if (w < 0) return false;
        }
    }

    bool encodeAudioFrame(const float* interleaved, int frames) {
        if (av_frame_make_writable(aframe) < 0) return false;
        const int size = aframe->nb_samples;
        float* left = reinterpret_cast<float*>(aframe->data[0]);
        float* right = reinterpret_cast<float*>(aframe->data[1]);
        for (int i = 0; i < size; ++i) {
            left[i] = i < frames ? interleaved[i * 2] : 0.0f;
            right[i] = i < frames ? interleaved[i * 2 + 1] : 0.0f;
        }
        aframe->pts = audioSamples;
        audioSamples += size;
        if (avcodec_send_frame(aenc, aframe) < 0) return false;
        return drain(aenc, astream);
    }
};

Exporter::Exporter() : d(std::make_unique<Impl>()) {}
Exporter::~Exporter() = default;

std::string Exporter::videoEncoderName() const { return d->encoderName; }

bool Exporter::open(const ExportSettings& settings, std::string* error) {
    ff::quietLogs();
    d->close();
    d->s = settings;
    ExportSettings& s = d->s;
    if (s.width < 16 || s.height < 16 || s.width % 2 || s.height % 2 || s.fpsNum < 1 ||
        s.fpsDen < 1) {
        if (error) *error = "Ungültige Exportgröße (Breite und Höhe müssen gerade sein)";
        return false;
    }
    if (s.videoBitrate <= 0)
        s.videoBitrate = defaultBitrate(s.width, s.height, static_cast<double>(s.fpsNum) / s.fpsDen);

    int rc = avformat_alloc_output_context2(&d->fmt, nullptr, nullptr, s.path.c_str());
    if (rc < 0 || !d->fmt) {
        if (error) *error = "Unbekanntes Ausgabeformat: " + ff::errorText(rc);
        return false;
    }

    bool ok = false;
    if (!s.videoEncoder.empty()) {
        ok = d->openVideoEncoder(s.videoEncoder.c_str());
    } else {
        for (const char* name : kVideoEncoders)
            if ((ok = d->openVideoEncoder(name))) break;
    }
    if (!ok) {
        if (error) *error = "Kein Video-Encoder verfügbar";
        d->close();
        return false;
    }
    d->vstream = avformat_new_stream(d->fmt, nullptr);
    d->vstream->time_base = d->venc->time_base;
    d->vstream->avg_frame_rate = d->venc->framerate;
    avcodec_parameters_from_context(d->vstream->codecpar, d->venc);

    if (s.audio) {
        if (!d->openAudioEncoder(error)) {
            d->close();
            return false;
        }
        d->astream = avformat_new_stream(d->fmt, nullptr);
        d->astream->time_base = d->aenc->time_base;
        avcodec_parameters_from_context(d->astream->codecpar, d->aenc);

        d->aframe = av_frame_alloc();
        d->aframe->format = d->aenc->sample_fmt;
        d->aframe->sample_rate = d->aenc->sample_rate;
        d->aframe->nb_samples = d->aenc->frame_size > 0 ? d->aenc->frame_size : 1024;
        av_channel_layout_copy(&d->aframe->ch_layout, &d->aenc->ch_layout);
        if (av_frame_get_buffer(d->aframe, 0) < 0) {
            if (error) *error = "Kein Speicher für Audio";
            d->close();
            return false;
        }
    }

    d->vframe = av_frame_alloc();
    d->vframe->format = d->venc->pix_fmt;
    d->vframe->width = s.width;
    d->vframe->height = s.height;
    d->pkt = av_packet_alloc();
    d->sws = sws_getContext(s.width, s.height, AV_PIX_FMT_RGBA, s.width, s.height,
                            d->venc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (av_frame_get_buffer(d->vframe, 0) < 0 || !d->sws) {
        if (error) *error = "Kein Speicher für Video";
        d->close();
        return false;
    }
    // RGB (voller Bereich) nach BT.709 mit Videopegel umrechnen.
    const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(d->sws, sws_getCoefficients(SWS_CS_DEFAULT), 1, coeffs, 0, 0,
                             1 << 16, 1 << 16);

    if (!(d->fmt->oformat->flags & AVFMT_NOFILE)) {
        rc = avio_open(&d->fmt->pb, s.path.c_str(), AVIO_FLAG_WRITE);
        if (rc < 0) {
            if (error) *error = "Datei kann nicht geschrieben werden: " + ff::errorText(rc);
            d->close();
            return false;
        }
    }
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    rc = avformat_write_header(d->fmt, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        if (error) *error = "Dateikopf: " + ff::errorText(rc);
        d->close();
        return false;
    }
    d->headerWritten = true;
    d->finished = false;
    d->videoFrames = 0;
    d->audioSamples = 0;
    d->audioFifo.clear();
    return true;
}

bool Exporter::writeVideoFrame(const std::uint8_t* rgba) {
    if (!d->headerWritten || d->finished) return false;
    if (av_frame_make_writable(d->vframe) < 0) return false;
    const std::uint8_t* src[4] = {rgba, nullptr, nullptr, nullptr};
    const int srcStride[4] = {d->s.width * 4, 0, 0, 0};
    sws_scale(d->sws, src, srcStride, 0, d->s.height, d->vframe->data, d->vframe->linesize);
    d->vframe->pts = d->videoFrames++;
    if (avcodec_send_frame(d->venc, d->vframe) < 0) return false;
    return d->drain(d->venc, d->vstream);
}

bool Exporter::writeAudio(const float* interleaved, std::size_t frames) {
    if (!d->headerWritten || d->finished) return false;
    if (!d->aenc) return true;
    d->audioFifo.insert(d->audioFifo.end(), interleaved, interleaved + frames * 2);
    const std::size_t size = static_cast<std::size_t>(d->aframe->nb_samples);
    std::size_t pos = 0;
    while ((d->audioFifo.size() - pos) / 2 >= size) {
        if (!d->encodeAudioFrame(d->audioFifo.data() + pos, static_cast<int>(size))) return false;
        pos += size * 2;
    }
    d->audioFifo.erase(d->audioFifo.begin(), d->audioFifo.begin() + static_cast<std::ptrdiff_t>(pos));
    return true;
}

bool Exporter::finish(std::string* error) {
    if (!d->headerWritten || d->finished) return false;
    d->finished = true;
    bool ok = true;
    if (d->aenc) {
        const int rest = static_cast<int>(d->audioFifo.size() / 2);
        if (rest > 0) ok = d->encodeAudioFrame(d->audioFifo.data(), rest) && ok;
        ok = avcodec_send_frame(d->aenc, nullptr) >= 0 && ok;
        ok = d->drain(d->aenc, d->astream) && ok;
    }
    ok = avcodec_send_frame(d->venc, nullptr) >= 0 && ok;
    ok = d->drain(d->venc, d->vstream) && ok;
    const int rc = av_write_trailer(d->fmt);
    if (rc < 0) ok = false;
    if (!ok && error) *error = rc < 0 ? ff::errorText(rc) : "Fehler beim Kodieren";
    d->close();
    d->headerWritten = false;
    return ok;
}

}  // namespace loggx
