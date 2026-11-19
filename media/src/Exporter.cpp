#include <array>
#include <cmath>
#include "loggx/media/Media.h"

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstring>

#include "FFmpegUtil.h"

namespace loggx {

namespace {

// Reihenfolge: bester Software-Encoder, dann die Hardware-Encoder der
// Plattformen (macOS, NVIDIA, Intel, AMD, Windows). Für H.264 zuletzt der
// immer vorhandene MPEG-4-Encoder.
const char* const kH264Encoders[] = {"libx264", "h264_videotoolbox", "h264_nvenc", "h264_qsv",
                                     "h264_amf", "h264_mf", "libopenh264", "mpeg4"};
const char* const kH265Encoders[] = {"libx265", "hevc_videotoolbox", "hevc_nvenc", "hevc_qsv",
                                     "hevc_amf", "hevc_mf"};

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

std::int64_t defaultBitrate(int w, int h, double fps, int quality, bool hevc) {
    // Bits pro Pixel und Bild: 1080p30 bei "normal" ≈ 6 Mbit/s, 4K30 ≈ 25 Mbit/s.
    static const double bpp[4] = {0.05, 0.1, 0.16, 0.25};
    double bits = static_cast<double>(w) * h * fps * bpp[std::clamp(quality, 0, 3)];
    if (hevc) bits *= 0.6;   // H.265 braucht für gleiche Qualität weniger
    return static_cast<std::int64_t>(std::clamp(bits, 1.0e6, 200.0e6));
}

std::string extensionOf(const std::string& path) {
    const auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext;
}

std::vector<AVSampleFormat> supportedSampleFormats(const AVCodecContext* ctx, const AVCodec* codec) {
    std::vector<AVSampleFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &count) >= 0 && configs)
        out.assign(static_cast<const AVSampleFormat*>(configs), static_cast<const AVSampleFormat*>(configs) + count);
#else
    (void)ctx;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    for (const AVSampleFormat* f = codec->sample_fmts; f && *f != AV_SAMPLE_FMT_NONE; ++f) out.push_back(*f);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
    return out;
}

}  // namespace

std::string ffmpegVersion() { return av_version_info(); }

std::vector<std::string> availableEncoders() {
    std::vector<std::string> out;
    for (const char* n : {"libx264", "libx265", "h264_videotoolbox", "hevc_videotoolbox", "h264_nvenc", "hevc_nvenc",
                          "h264_qsv", "hevc_qsv", "h264_amf", "hevc_amf", "h264_mf", "hevc_mf", "aac", "libmp3lame",
                          "mp3_mf", "flac", "libvorbis", "libopus"})
        if (avcodec_find_encoder_by_name(n)) out.push_back(n);
    return out;
}

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
    // HDR: RGBA (BT.709, SDR) → RGB48 (BT.2020, HLG) vor der YUV-Umrechnung
    bool hdr = false;
    std::vector<std::uint16_t> rgb48;
    std::array<float, 256> toLinear{};
    std::vector<std::uint16_t> hlgLut;   // linear (0..1, 4096 Stufen) → HLG-Signal 16 Bit
    void initHdr() {
        for (int i = 0; i < 256; ++i) toLinear[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(i / 255.0, 2.4));   // BT.1886
        hlgLut.resize(4097);
        const double a = 0.17883277, b = 1.0 - 4.0 * a, c = 0.5 - a * std::log(4.0 * a);
        for (int i = 0; i <= 4096; ++i) {
            // SDR-Weiß (1.0) → HLG 75 %: E = linear · 0.2647 (BT.2408)
            const double e = i / 4096.0 * 0.26496256;
            const double v = e <= 1.0 / 12.0 ? std::sqrt(3.0 * e) : a * std::log(12.0 * e - b) + c;
            hlgLut[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(std::clamp(v, 0.0, 1.0) * 65535.0 + 0.5);
        }
    }
    void convertHdr(const std::uint8_t* rgba, int w, int h) {
        rgb48.resize(static_cast<std::size_t>(w) * h * 3);
        // BT.709 → BT.2020 (lineare Primärfarben, BT.2087)
        static const float m[9] = {0.6274f, 0.3293f, 0.0433f, 0.0691f, 0.9195f, 0.0114f, 0.0164f, 0.0880f, 0.8956f};
        for (std::size_t i = 0, n = static_cast<std::size_t>(w) * h; i < n; ++i) {
            const float r = toLinear[rgba[i * 4]], g = toLinear[rgba[i * 4 + 1]], bl = toLinear[rgba[i * 4 + 2]];
            const float o[3] = {m[0] * r + m[1] * g + m[2] * bl, m[3] * r + m[4] * g + m[5] * bl, m[6] * r + m[7] * g + m[8] * bl};
            for (int k = 0; k < 3; ++k)
                rgb48[i * 3 + static_cast<std::size_t>(k)] = hlgLut[static_cast<std::size_t>(std::clamp(o[k], 0.0f, 1.0f) * 4096.0f + 0.5f)];
        }
    }
    std::int64_t videoFrames = 0;
    std::int64_t audioSamples = 0;
    std::vector<float> audioFifo;   // interleaved stereo
    std::vector<float> padBuf;
    SwrContext* swr = nullptr;
    std::string encoderName;
    bool headerWritten = false;
    bool finished = false;

    ~Impl() { close(); }

    void close() {
        swr_free(&swr);
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
        const bool prores = std::strcmp(name, "prores_ks") == 0;
        c->pix_fmt = prores ? (s.codec == ExportSettings::Codec::ProRes4444 ? AV_PIX_FMT_YUVA444P10LE : AV_PIX_FMT_YUV422P10LE)
                            : pickPixelFormat(c, codec);
        c->bit_rate = prores ? 0 : s.videoBitrate;
        c->gop_size = std::max(1, s.fpsNum / s.fpsDen) * 2;   // Keyframe alle 2 s
        // B-Bilder nur mit den Software-Encodern: die Hardware-Encoder liefern damit je
        // nach Treiber uneinheitliche Zeitstempel.
        c->max_b_frames =
            std::strcmp(name, "libx264") == 0 || std::strcmp(name, "libx265") == 0 ? 2 : 0;
        c->color_range = AVCOL_RANGE_MPEG;
        c->colorspace = AVCOL_SPC_BT709;
        c->color_primaries = AVCOL_PRI_BT709;
        c->color_trc = AVCOL_TRC_BT709;
        if (s.hdr) {
            // HLG: Signalisierung im Datenstrom (VUI) und im Container
            c->colorspace = AVCOL_SPC_BT2020_NCL;
            c->color_primaries = AVCOL_PRI_BT2020;
            c->color_trc = AVCOL_TRC_ARIB_STD_B67;
            if (!prores) {
                bool ten = false;
                for (const AVPixelFormat* p = codec->pix_fmts; p && *p != AV_PIX_FMT_NONE; ++p)
                    if (*p == AV_PIX_FMT_YUV420P10LE || *p == AV_PIX_FMT_P010LE) {
                        c->pix_fmt = *p;
                        ten = true;
                        if (*p == AV_PIX_FMT_YUV420P10LE) break;
                    }
                if (!ten) {   // Encoder kann kein 10 Bit → nächsten versuchen
                    avcodec_free_context(&c);
                    return false;
                }
            }
        }
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        AVDictionary* opts = nullptr;
        if (std::strcmp(name, "libx264") == 0 || std::strcmp(name, "libx265") == 0)
            av_dict_set(&opts, "preset", "medium", 0);
        // Über 4K (z. B. 8K) braucht der Lookahead sonst viele Gigabyte Speicher.
        const bool huge = static_cast<long long>(s.width) * s.height > 3840LL * 2160;
        if (std::strcmp(name, "libx264") == 0 && huge) {
            av_dict_set(&opts, "rc-lookahead", "8", 0);
            av_dict_set(&opts, "x264-params", "sync-lookahead=0", 0);
            c->thread_count = 4;
        }
        if (prores) {
            // Profil 3 = 422 HQ, 4 = 4444; Hersteller-Kennung wie bei Apple
            av_dict_set(&opts, "profile", s.codec == ExportSettings::Codec::ProRes4444 ? "4" : "3", 0);
            av_dict_set(&opts, "vendor", "apl0", 0);
            if (s.codec == ExportSettings::Codec::ProRes4444) av_dict_set(&opts, "alpha_bits", "16", 0);
        }
        if (std::strcmp(name, "libx265") == 0) {
            std::string params = huge ? "log-level=error:rc-lookahead=8:frame-threads=2:lookahead-slices=4" : "log-level=error";
            if (s.hdr) params += ":atc-sei=18";   // HLG-Kennung (alternative transfer) für Geräte
            av_dict_set(&opts, "x265-params", params.c_str(), 0);
            if (s.hdr) av_dict_set(&opts, "profile", "main10", 0);
        }
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
        // Codec: im Video immer AAC, sonst nach Dateiendung.
        std::vector<std::string> names;
        bool twentyFour = s.audioBits >= 24;
        const std::string ext = s.video ? "" : extensionOf(s.path);
        const bool prores = s.video && (s.codec == ExportSettings::Codec::ProRes422HQ || s.codec == ExportSettings::Codec::ProRes4444);
        if (prores) {   // Schnittformat: Ton unkomprimiert (24 Bit)
            names = {"pcm_s24le"};
            twentyFour = true;
        } else if (s.video || ext == "m4a" || ext == "aac" || ext == "mp4") names = {"aac"};
        else if (ext == "wav") names = {twentyFour ? "pcm_s24le" : "pcm_s16le"};
        else if (ext == "aif" || ext == "aiff") names = {twentyFour ? "pcm_s24be" : "pcm_s16be"};
        else if (ext == "flac") names = {"flac"};
        else if (ext == "mp3") names = {"libmp3lame", "mp3_mf"};
        else if (ext == "ogg") names = {"libvorbis", "vorbis"};
        else if (ext == "opus") names = {"libopus", "opus"};
        else {
            if (error) *error = "Unbekanntes Audioformat ." + ext;
            return false;
        }
        const AVCodec* codec = nullptr;
        for (const auto& n : names)
            if ((codec = avcodec_find_encoder_by_name(n.c_str()))) break;
        if (!codec) {
            if (error) *error = "Für ." + ext + " ist auf diesem Rechner kein Encoder vorhanden";
            return false;
        }
        aenc = avcodec_alloc_context3(codec);
        aenc->sample_rate = s.sampleRate;
        aenc->time_base = AVRational{1, s.sampleRate};
        av_channel_layout_default(&aenc->ch_layout, 2);

        // Sample-Format: Fließkomma bevorzugt, bei 24 Bit 32-Bit-Ganzzahl.
        const auto formats = supportedSampleFormats(aenc, codec);
        auto has = [&](AVSampleFormat f) { return std::find(formats.begin(), formats.end(), f) != formats.end(); };
        AVSampleFormat fmtChoice = formats.empty() ? AV_SAMPLE_FMT_FLTP : formats.front();
        static const AVSampleFormat pref24[] = {AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT};
        static const AVSampleFormat pref16[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P};
        for (AVSampleFormat f : twentyFour ? pref24 : pref16)
            if (has(f)) {
                fmtChoice = f;
                break;
            }
        aenc->sample_fmt = fmtChoice;
        if (codec->id == AV_CODEC_ID_FLAC && twentyFour) aenc->bits_per_raw_sample = 24;
        if (codec->id == AV_CODEC_ID_MP3) aenc->bit_rate = 320000;
        else if (codec->id == AV_CODEC_ID_AAC) aenc->bit_rate = s.video ? s.audioBitrate : 256000;
        else if (codec->id == AV_CODEC_ID_OPUS) aenc->bit_rate = 192000;
        else if (codec->id == AV_CODEC_ID_VORBIS) {
            aenc->flags |= AV_CODEC_FLAG_QSCALE;
            aenc->global_quality = 7 * FF_QP2LAMBDA;
        }
        if (std::strcmp(codec->name, "vorbis") == 0 || std::strcmp(codec->name, "opus") == 0)
            aenc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER) aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int rc = avcodec_open2(aenc, codec, nullptr);
        if (rc < 0) {
            if (error) *error = std::string("Audio-Encoder ") + codec->name + ": " + ff::errorText(rc);
            return false;
        }
        // Umwandlung aus Stereo-Float (interleaved) ins Format des Encoders.
        AVChannelLayout stereo{};
        av_channel_layout_default(&stereo, 2);
        const int sr = swr_alloc_set_opts2(&swr, &aenc->ch_layout, aenc->sample_fmt, s.sampleRate, &stereo,
                                           AV_SAMPLE_FMT_FLT, s.sampleRate, 0, nullptr);
        av_channel_layout_uninit(&stereo);
        if (sr < 0 || swr_init(swr) < 0) {
            if (error) *error = "Audio-Umwandlung nicht möglich";
            return false;
        }
        return true;
    }

    // Größe eines Audio-Rahmens; bei PCM frei wählbar.
    int audioFrameSize() const {
        if (aenc->frame_size > 0 && !(aenc->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
            return aenc->frame_size;
        return 1024;
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
        // Letzter Rahmen: kürzer erlaubt, sonst mit Stille auffüllen.
        const bool shortOk = aenc->codec->capabilities &
                             (AV_CODEC_CAP_VARIABLE_FRAME_SIZE | AV_CODEC_CAP_SMALL_LAST_FRAME);
        const int size = shortOk ? frames : audioFrameSize();
        av_frame_unref(aframe);
        aframe->format = aenc->sample_fmt;
        aframe->sample_rate = aenc->sample_rate;
        aframe->nb_samples = size;
        av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
        if (av_frame_get_buffer(aframe, 0) < 0) return false;
        const float* in = interleaved;
        if (frames < size) {
            padBuf.assign(static_cast<std::size_t>(size) * 2, 0.0f);
            std::copy(interleaved, interleaved + frames * 2, padBuf.begin());
            in = padBuf.data();
        }
        const std::uint8_t* inPtr = reinterpret_cast<const std::uint8_t*>(in);
        if (swr_convert(swr, aframe->data, size, &inPtr, size) < 0) return false;
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
    if (!s.video && !s.audio) {
        if (error) *error = "Weder Bild noch Ton ausgewählt";
        return false;
    }
    if (s.video && (s.width < 16 || s.height < 16 || s.width % 2 || s.height % 2 || s.fpsNum < 1 ||
                    s.fpsDen < 1)) {
        if (error) *error = "Ungültige Exportgröße (Breite und Höhe müssen gerade sein)";
        return false;
    }
    const bool hevc = s.codec == ExportSettings::Codec::H265;
    if (s.video && s.hdr && s.codec == ExportSettings::Codec::H264) {
        if (error) *error = "HDR geht nur mit H.265 oder ProRes";
        return false;
    }
    if (s.videoBitrate <= 0)
        s.videoBitrate = defaultBitrate(s.width, s.height, static_cast<double>(s.fpsNum) / s.fpsDen,
                                        s.quality, hevc);

    int rc = avformat_alloc_output_context2(&d->fmt, nullptr, nullptr, s.path.c_str());
    if (rc < 0 || !d->fmt) {
        if (error) *error = "Unbekanntes Ausgabeformat: " + ff::errorText(rc);
        return false;
    }

    bool ok = !s.video;
    if (!s.video) {
        // nur Ton
    } else if (!s.videoEncoder.empty()) {
        ok = d->openVideoEncoder(s.videoEncoder.c_str());
    } else if (s.codec == ExportSettings::Codec::ProRes422HQ || s.codec == ExportSettings::Codec::ProRes4444) {
        ok = d->openVideoEncoder("prores_ks");
    } else {
        if (hevc) {
            for (const char* name : kH265Encoders)
                if ((ok = d->openVideoEncoder(name))) break;
        } else {
            for (const char* name : kH264Encoders)
                if ((ok = d->openVideoEncoder(name))) break;
        }
    }
    if (!ok) {
        if (error) *error = hevc && s.hdr ? "Kein H.265-Encoder mit 10 Bit (HDR) verfügbar"
                          : hevc ? "Kein H.265-Encoder auf diesem Rechner verfügbar"
                          : s.codec >= ExportSettings::Codec::ProRes422HQ ? "ProRes-Encoder (prores_ks) fehlt in dieser FFmpeg-Version"
                                 : "Kein Video-Encoder verfügbar";
        d->close();
        return false;
    }
    if (s.video) {
        d->vstream = avformat_new_stream(d->fmt, nullptr);
        d->vstream->time_base = d->venc->time_base;
        d->vstream->avg_frame_rate = d->venc->framerate;
        avcodec_parameters_from_context(d->vstream->codecpar, d->venc);
        // Apple-Geräte spielen H.265 in MP4/MOV nur mit dem Kennzeichen "hvc1".
        if (d->venc->codec_id == AV_CODEC_ID_HEVC)
            d->vstream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    }

    if (s.audio) {
        if (!d->openAudioEncoder(error)) {
            d->close();
            return false;
        }
        d->astream = avformat_new_stream(d->fmt, nullptr);
        d->astream->time_base = d->aenc->time_base;
        avcodec_parameters_from_context(d->astream->codecpar, d->aenc);

        d->aframe = av_frame_alloc();
    }

    d->pkt = av_packet_alloc();
    if (s.video) {
    d->vframe = av_frame_alloc();
    d->vframe->format = d->venc->pix_fmt;
    d->vframe->width = s.width;
    d->vframe->height = s.height;
    d->hdr = s.hdr;
    if (d->hdr) d->initHdr();
    d->sws = sws_getContext(s.width, s.height, d->hdr ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGBA, s.width, s.height,
                            d->venc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (av_frame_get_buffer(d->vframe, 0) < 0 || !d->sws) {
        if (error) *error = "Kein Speicher für Video";
        d->close();
        return false;
    }
    // RGB (voller Bereich) nach BT.709 mit Videopegel umrechnen.
    const int* coeffs = sws_getCoefficients(d->hdr ? SWS_CS_BT2020 : SWS_CS_ITU709);
    sws_setColorspaceDetails(d->sws, sws_getCoefficients(SWS_CS_DEFAULT), 1, coeffs, 0, 0,
                             1 << 16, 1 << 16);
    }

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
    if (!d->headerWritten || d->finished || !d->venc) return false;
    if (av_frame_make_writable(d->vframe) < 0) return false;
    const std::uint8_t* src[4] = {rgba, nullptr, nullptr, nullptr};
    int srcStride[4] = {d->s.width * 4, 0, 0, 0};
    if (d->hdr) {
        d->convertHdr(rgba, d->s.width, d->s.height);
        src[0] = reinterpret_cast<const std::uint8_t*>(d->rgb48.data());
        srcStride[0] = d->s.width * 6;
    }
    sws_scale(d->sws, src, srcStride, 0, d->s.height, d->vframe->data, d->vframe->linesize);
    d->vframe->pts = d->videoFrames++;
    if (avcodec_send_frame(d->venc, d->vframe) < 0) return false;
    return d->drain(d->venc, d->vstream);
}

bool Exporter::writeAudio(const float* interleaved, std::size_t frames) {
    if (!d->headerWritten || d->finished) return false;
    if (!d->aenc) return true;
    d->audioFifo.insert(d->audioFifo.end(), interleaved, interleaved + frames * 2);
    const std::size_t size = static_cast<std::size_t>(d->audioFrameSize());
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
    if (d->venc) {
        ok = avcodec_send_frame(d->venc, nullptr) >= 0 && ok;
        ok = d->drain(d->venc, d->vstream) && ok;
    }
    const int rc = av_write_trailer(d->fmt);
    if (rc < 0) ok = false;
    if (!ok && error) *error = rc < 0 ? ff::errorText(rc) : "Fehler beim Kodieren";
    d->close();
    d->headerWritten = false;
    return ok;
}

}  // namespace loggx
