#pragma once
// Audio-Plugins: CLAP, VST3 und (macOS) Audio Units als Effekte auf Spuren.
// Alles im Prozess; Verarbeitung Stereo, 32 Bit Float.
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace loggx {

struct PluginInfo {
    std::string format;   // "clap", "vst3", "au"
    std::string id;       // CLAP-Id, VST3-Klassen-Id (hex), AU "typ:subtyp:hersteller"
    std::string name, vendor, version;
    std::string path;     // Datei/Bundle (AU: leer)
    bool instrument = false;
};

struct PluginParam {
    std::string id;
    std::string name;
    double min = 0, max = 1, def = 0, value = 0;
    bool stepped = false;
    std::string unit;
};

class AudioPlugin {
public:
    virtual ~AudioPlugin() = default;
    const PluginInfo& info() const { return m_info; }
    void setInfo(const PluginInfo& i) { m_info = i; }
    // Audio-Thread: Stereo, verschachtelt, an Ort und Stelle
    virtual void process(float* interleaved, std::size_t frames) = 0;
    // Haupt-Thread
    virtual std::vector<PluginParam> params() = 0;
    virtual void setParam(const std::string& id, double value) = 0;
    virtual std::string saveState() = 0;            // Rohdaten des Plugins
    virtual bool loadState(const std::string& data) = 0;
    virtual bool hasEditor() { return false; }
    // Eigene Oberfläche des Plugins in ein natives Fenster (HWND / NSView)
    virtual bool openEditor(void* nativeParent, int& width, int& height) { (void)nativeParent, (void)width, (void)height; return false; }
    virtual void closeEditor() {}
    // Plugin meldet eine neue Fenstergröße
    std::function<void(int, int)> onResize;
    // In der Plugin-Oberfläche wurde etwas geändert
    std::function<void()> onEdited;

protected:
    PluginInfo m_info;
};

// Standard-Ordner je System (+ CLAP_PATH/VST3_PATH aus der Umgebung)
std::vector<std::string> defaultPluginDirs();
// Sucht Plugins (Effekte und Instrumente); AU über das System
std::vector<PluginInfo> scanPlugins(const std::vector<std::string>& extraDirs = {},
                                    const std::function<void(const std::string&)>& progress = {},
                                    const std::vector<std::string>& skip = {});
std::unique_ptr<AudioPlugin> loadPlugin(const PluginInfo& info, int sampleRate, int maxBlock, std::string* error = nullptr);

// intern
std::vector<PluginInfo> scanClap(const std::string& path);
std::vector<PluginInfo> scanVst3(const std::string& path);
std::vector<PluginInfo> scanAudioUnits();
std::unique_ptr<AudioPlugin> loadClap(const PluginInfo& info, int rate, int maxBlock, std::string* error);
std::unique_ptr<AudioPlugin> loadVst3(const PluginInfo& info, int rate, int maxBlock, std::string* error);
std::unique_ptr<AudioPlugin> loadAudioUnit(const PluginInfo& info, int rate, int maxBlock, std::string* error);

// Kleine Hilfe: dynamische Bibliothek
class DynLib {
public:
    DynLib() = default;
    ~DynLib();
    DynLib(const DynLib&) = delete;
    DynLib& operator=(const DynLib&) = delete;
    bool open(const std::string& file, std::string* error = nullptr);
    void* symbol(const char* name) const;
    bool isOpen() const { return m_handle != nullptr; }
    void* nativeHandle() const { return m_handle; }
    void close();

private:
    void* m_handle = nullptr;
};

}  // namespace loggx
