#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace loggx::ff {

inline std::string errorText(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

inline constexpr AVRational kMicro{1, 1'000'000};

// FFmpeg meldet sonst jede Encoder-Statistik auf der Konsole.
inline void quietLogs() {
    static const bool once = (av_log_set_level(AV_LOG_ERROR), true);
    (void)once;
}

}  // namespace loggx::ff
