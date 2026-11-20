// RAW-Fotos (CR2/CR3, NEF, ARW, DNG, RAF, ORF, RW2, …): werden beim Import
// einmal mit LibRaw entwickelt (Kamera-Weißabgleich, 16 Bit, sRGB) und als
// 16-Bit-TIFF im Zwischenspeicher abgelegt. Dieses Bild wird dann wie jedes
// andere Foto verwendet; das Original bleibt unverändert.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "loggx/media/Media.h"

#ifdef LOGGX_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace loggx {

namespace {
std::mutex g_rawMutex;
std::string g_rawCache;

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

// Unkomprimiertes 16-Bit-RGB-TIFF (Little Endian)
bool writeTiff16(const std::filesystem::path& file, const std::uint16_t* rgb, int w, int h) {
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(w) * h * 6;
    const std::uint16_t entries = 11;
    const std::uint32_t ifdOffset = 8;
    const std::uint32_t bpsOffset = ifdOffset + 2 + entries * 12 + 4;
    const std::uint32_t dataOffset = bpsOffset + 6;
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto tag = [&](std::uint16_t id, std::uint16_t type, std::uint32_t count, std::uint32_t value) {
        u16(id), u16(type), u32(count), u32(value);
    };
    out.write("II", 2);
    u16(42);
    u32(ifdOffset);
    u16(entries);
    tag(256, 4, 1, static_cast<std::uint32_t>(w));   // Breite
    tag(257, 4, 1, static_cast<std::uint32_t>(h));   // Höhe
    tag(258, 3, 3, bpsOffset);                       // 16/16/16 Bit
    tag(259, 3, 1, 1);                               // unkomprimiert
    tag(262, 3, 1, 2);                               // RGB
    tag(273, 4, 1, dataOffset);                      // Bilddaten
    tag(277, 3, 1, 3);                               // 3 Kanäle
    tag(278, 4, 1, static_cast<std::uint32_t>(h));   // eine Bahn
    tag(279, 4, 1, dataBytes);
    tag(284, 3, 1, 1);                               // verschachtelt
    tag(339, 3, 1, 1);                               // vorzeichenlos
    u32(0);
    u16(16), u16(16), u16(16);
    out.write(reinterpret_cast<const char*>(rgb), dataBytes);
    return static_cast<bool>(out);
}
#ifdef LOGGX_HAVE_LIBRAW
// Rückruf-Signatur je nach LibRaw-Version (int oder INT64 als Position)
template <class F>
struct ThirdArg;
template <class R, class A, class B, class C>
struct ThirdArg<R (*)(A, B, C)> {
    using type = C;
};
template <class Off>
void onDataError(void* flag, const char*, Off) {
    *static_cast<bool*>(flag) = true;
}
#endif
}  // namespace

bool rawSupported() {
#ifdef LOGGX_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}

bool isRawPhoto(const std::string& path) {
    std::string ext = u8path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* const exts[] = {".cr2", ".cr3", ".crw", ".nef", ".nrw", ".arw", ".srf", ".sr2", ".dng", ".raf", ".orf",
                                       ".rw2", ".pef", ".srw", ".x3f", ".3fr", ".fff", ".iiq", ".mef", ".mos", ".erf", ".kdc", ".rwl"};
    return std::find(std::begin(exts), std::end(exts), ext) != std::end(exts);
}

void setRawCacheDir(const std::string& dir) {
    std::lock_guard lock(g_rawMutex);
    g_rawCache = dir;
}

std::string developRaw(const std::string& path, std::string* error) {
#ifndef LOGGX_HAVE_LIBRAW
    if (error) *error = "RAW-Unterstützung (LibRaw) fehlt in diesem Build";
    (void)path;
    return {};
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src = u8path(path);
    // Zwischenspeicher-Name aus Pfad, Größe und Änderungszeit
    const auto size = fs::file_size(src, ec);
    const auto mtime = fs::last_write_time(src, ec).time_since_epoch().count();
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : path) h = (h ^ c) * 1099511628211ULL;
    h ^= static_cast<std::uint64_t>(size) * 31 + static_cast<std::uint64_t>(mtime);
    fs::path dir;
    {
        std::lock_guard lock(g_rawMutex);
        dir = g_rawCache.empty() ? fs::temp_directory_path(ec) / "LXStudio-RAW" : u8path(g_rawCache);
    }
    fs::create_directories(dir, ec);
    char name[40];
    std::snprintf(name, sizeof name, "%016llx.tiff", static_cast<unsigned long long>(h));
    const fs::path out = dir / name;
    const auto u8 = out.u8string();
    const std::string outStr(u8.begin(), u8.end());
    if (fs::exists(out, ec) && fs::file_size(out, ec) > 0) return outStr;

    auto raw = std::make_unique<LibRaw>();
    // Beschädigte Daten melden statt ein verdorbenes Bild zu liefern
    bool corrupted = false;
    raw->set_dataerror_handler(&onDataError<ThirdArg<data_callback>::type>, &corrupted);
    auto& p = raw->imgdata.params;
    p.use_camera_wb = 1;     // Weißabgleich der Kamera
    p.output_bps = 16;
    p.output_color = 1;      // sRGB
    p.highlight = 2;         // Lichter mischen statt ausfressen
    p.use_camera_matrix = 1;
#if defined(_WIN32) && defined(LIBRAW_WIN32_UNICODEPATHS)
    int rc = raw->open_file(src.wstring().c_str());
#else
    int rc = raw->open_file(path.c_str());
#endif
    if (rc == LIBRAW_SUCCESS) rc = raw->unpack();
    if (rc == LIBRAW_SUCCESS) rc = raw->dcraw_process();
    if (rc != LIBRAW_SUCCESS) {
        if (error) *error = std::string("RAW lässt sich nicht lesen: ") + libraw_strerror(rc);
        return {};
    }
    if (corrupted) {
        if (error) *error = "RAW-Datei ist beschädigt (unvollständig kopiert?)";
        return {};
    }
    libraw_processed_image_t* img = raw->dcraw_make_mem_image(&rc);
    if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3 || img->bits != 16) {
        if (img) LibRaw::dcraw_clear_mem(img);
        if (error) *error = "RAW-Entwicklung fehlgeschlagen";
        return {};
    }
    const fs::path tmp = out.string() + ".part";
    const bool ok = writeTiff16(tmp, reinterpret_cast<const std::uint16_t*>(img->data), img->width, img->height);
    LibRaw::dcraw_clear_mem(img);
    if (!ok) {
        if (error) *error = "RAW: Zwischenspeicher nicht beschreibbar";
        return {};
    }
    fs::rename(tmp, out, ec);
    return ec ? std::string() : outStr;
#endif
}

}  // namespace loggx
