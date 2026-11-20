// Plugin-Suche und gemeinsame Hilfen (dynamische Bibliotheken, Ordner).
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "loggx/engine/Plugins.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace loggx {

namespace fs = std::filesystem;

namespace {
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string u8str(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string env(const char* name) {
#ifdef _WIN32
    char* v = nullptr;
    std::size_t n = 0;
    if (_dupenv_s(&v, &n, name) != 0 || !v) return {};
    std::string out(v);
    std::free(v);
    return out;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}
void splitPaths(const std::string& list, std::vector<std::string>& out) {
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::size_t a = 0;
    while (a <= list.size()) {
        const std::size_t b = list.find(sep, a);
        const std::string part = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
}
}  // namespace

DynLib::~DynLib() { close(); }

bool DynLib::open(const std::string& file, std::string* error) {
    close();
#ifdef _WIN32
    m_handle = LoadLibraryW(u8path(file).wstring().c_str());
    if (!m_handle && error) *error = "Bibliothek lässt sich nicht laden (Fehler " + std::to_string(GetLastError()) + ")";
#else
    m_handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle && error) {
        const char* e = dlerror();
        *error = e ? e : "Bibliothek lässt sich nicht laden";
    }
#endif
    return m_handle != nullptr;
}

void* DynLib::symbol(const char* name) const {
    if (!m_handle) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return dlsym(m_handle, name);
#endif
}

void DynLib::close() {
    if (!m_handle) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    dlclose(m_handle);
#endif
    m_handle = nullptr;
}

std::vector<std::string> defaultPluginDirs() {
    std::vector<std::string> d;
    splitPaths(env("CLAP_PATH"), d);
    splitPaths(env("VST3_PATH"), d);
#ifdef _WIN32
    const std::string common = env("COMMONPROGRAMFILES"), local = env("LOCALAPPDATA");
    if (!common.empty()) d.push_back(common + "\\CLAP"), d.push_back(common + "\\VST3");
    if (!local.empty()) d.push_back(local + "\\Programs\\Common\\CLAP"), d.push_back(local + "\\Programs\\Common\\VST3");
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    d.push_back("/Library/Audio/Plug-Ins/CLAP");
    d.push_back("/Library/Audio/Plug-Ins/VST3");
    if (!home.empty()) d.push_back(home + "/Library/Audio/Plug-Ins/CLAP"), d.push_back(home + "/Library/Audio/Plug-Ins/VST3");
#else
    const std::string home = env("HOME");
    if (!home.empty()) d.push_back(home + "/.clap"), d.push_back(home + "/.vst3");
    for (const char* p : {"/usr/lib/clap", "/usr/local/lib/clap", "/usr/lib/vst3", "/usr/local/lib/vst3"}) d.push_back(p);
#endif
    return d;
}

std::vector<PluginInfo> scanPlugins(const std::vector<std::string>& extraDirs, const std::function<void(const std::string&)>& progress,
                                    const std::vector<std::string>& skip) {
    std::vector<std::string> dirs = defaultPluginDirs();
    dirs.insert(dirs.end(), extraDirs.begin(), extraDirs.end());
    std::vector<PluginInfo> out;
    std::set<std::string> seen;
    for (const std::string& dir : dirs) {
        std::error_code ec;
        const fs::path root = u8path(dir);
        if (!fs::is_directory(root, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".clap" && ext != ".vst3") continue;
            // Bundles (Ordner) nicht weiter durchsuchen
            if (it->is_directory(ec)) it.disable_recursion_pending();
            const std::string path = u8str(it->path());
            if (!seen.insert(path).second) continue;
            if (std::find(skip.begin(), skip.end(), path) != skip.end()) continue;   // stürzte beim letzten Mal ab
            if (progress) progress(path);
            auto found = ext == ".clap" ? scanClap(path) : scanVst3(path);
            out.insert(out.end(), found.begin(), found.end());
        }
    }
    auto au = scanAudioUnits();
    out.insert(out.end(), au.begin(), au.end());
    std::sort(out.begin(), out.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return a.name == b.name ? a.format < b.format : a.name < b.name;
    });
    return out;
}

std::unique_ptr<AudioPlugin> loadPlugin(const PluginInfo& info, int rate, int maxBlock, std::string* error) {
    if (info.format == "clap") return loadClap(info, rate, maxBlock, error);
    if (info.format == "vst3") return loadVst3(info, rate, maxBlock, error);
    if (info.format == "au") return loadAudioUnit(info, rate, maxBlock, error);
    if (error) *error = "Unbekanntes Plugin-Format";
    return nullptr;
}

#ifndef __APPLE__
std::vector<PluginInfo> scanAudioUnits() { return {}; }
std::unique_ptr<AudioPlugin> loadAudioUnit(const PluginInfo&, int, int, std::string* error) {
    if (error) *error = "Audio Units gibt es nur auf dem Mac";
    return nullptr;
}
#endif

}  // namespace loggx
