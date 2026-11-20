// CLAP-Host (https://cleveraudio.org, MIT). Effekt-Plugins, Stereo.
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

#include "../../third_party/clap/include/clap/clap.h"
#include "loggx/engine/Plugins.h"

namespace loggx {

namespace fs = std::filesystem;

namespace {
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// Bibliothek eines .clap (macOS: Bundle → Contents/MacOS/<Datei>)
std::string clapBinary(const std::string& path) {
    std::error_code ec;
    const fs::path p = u8path(path);
    if (!fs::is_directory(p, ec)) return path;
    for (const auto& e : fs::directory_iterator(p / "Contents" / "MacOS", ec)) {
        const auto s = e.path().u8string();
        return std::string(s.begin(), s.end());
    }
    return path;
}

// Geladene Module teilen (init/deinit nur einmal je Datei)
struct ClapModule {
    DynLib lib;
    const clap_plugin_entry_t* entry = nullptr;
    ~ClapModule() {
        if (entry) entry->deinit();
    }
};
std::mutex g_modMutex;
std::map<std::string, std::weak_ptr<ClapModule>> g_modules;

std::shared_ptr<ClapModule> openModule(const std::string& path, std::string* error) {
    std::lock_guard lock(g_modMutex);
    if (auto m = g_modules[path].lock()) return m;
    auto m = std::make_shared<ClapModule>();
    if (!m->lib.open(clapBinary(path), error)) return nullptr;
    m->entry = static_cast<const clap_plugin_entry_t*>(m->lib.symbol("clap_entry"));
    if (!m->entry || !clap_version_is_compatible(m->entry->clap_version) || !m->entry->init(path.c_str())) {
        m->entry = nullptr;
        if (error) *error = "Kein gültiges CLAP-Plugin";
        return nullptr;
    }
    g_modules[path] = m;
    return m;
}

bool hasFeature(const clap_plugin_descriptor_t* d, const char* f) {
    for (const char* const* p = d->features; p && *p; ++p)
        if (std::strcmp(*p, f) == 0) return true;
    return false;
}

// Ereignisliste: erst Parameter (Zeit 0), dann Noten (nach Zeit sortiert)
struct EventList {
    std::vector<clap_event_param_value_t> events;
    std::vector<clap_event_note_t> notes;
    clap_input_events_t in{this,
                           [](const clap_input_events_t* l) {
                               auto* e = static_cast<EventList*>(l->ctx);
                               return static_cast<uint32_t>(e->events.size() + e->notes.size());
                           },
                           [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
                               auto* e = static_cast<EventList*>(l->ctx);
                               if (i < e->events.size()) return &e->events[i].header;
                               return &e->notes[i - e->events.size()].header;
                           }};
};
clap_output_events_t g_nullOut{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};

class ClapPlugin final : public AudioPlugin {
public:
    std::shared_ptr<ClapModule> mod;
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* paramsExt = nullptr;
    const clap_plugin_state_t* stateExt = nullptr;
    const clap_plugin_gui_t* guiExt = nullptr;
    clap_host_t host{};
    clap_host_gui_t hostGui{};
    clap_host_params_t hostParams{};
    std::atomic<bool> processing{false};
    bool guiCreated = false;
    std::mutex queueMutex;
    std::vector<std::pair<clap_id, double>> queue;
    std::vector<float> inL, inR, outL, outR;
    EventList events;

    ~ClapPlugin() override {
        if (!plugin) return;
        closeEditor();
        if (processing.exchange(false)) plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    uint32_t maxBlock = 4096;

    uint32_t audioInputs = 1;
    std::vector<int> held;          // klingende Tasten
    std::atomic<bool> panic{false};

    void process(float* io, std::size_t frames) override { processNotes(io, frames, {}); }

    void allNotesOff() override { panic = true; }

    void processNotes(float* io, std::size_t frames, const std::vector<NoteEvent>& notes) override {
        // In Blöcken bis zur beim Aktivieren genannten Größe
        std::size_t next = 0;
        for (std::size_t done = 0; done < frames;) {
            const std::size_t n = std::min<std::size_t>(frames - done, maxBlock);
            events.notes.clear();
            auto addNote = [&](uint32_t time, int key, double vel, bool on) {
                clap_event_note_t e{};
                e.header = {sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, static_cast<uint16_t>(on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
                e.note_id = -1;
                e.port_index = 0;
                e.channel = 0;
                e.key = static_cast<int16_t>(key);
                e.velocity = vel;
                events.notes.push_back(e);
            };
            if (panic.exchange(false)) {
                for (int k : held) addNote(0, k, 0.0, false);
                held.clear();
            }
            for (; next < notes.size() && notes[next].offset < done + n; ++next) {
                const NoteEvent& ne = notes[next];
                addNote(static_cast<uint32_t>(ne.offset >= done ? ne.offset - done : 0), ne.key, ne.velocity, ne.on);
                if (ne.on) held.push_back(ne.key);
                else if (auto it = std::find(held.begin(), held.end(), ne.key); it != held.end()) held.erase(it);
            }
            processBlock(io + done * 2, n);
            done += n;
        }
    }

    void processBlock(float* io, std::size_t frames) {
        if (!processing.load()) {
            plugin->start_processing(plugin);
            processing = true;
        }
        inL.resize(frames), inR.resize(frames), outL.resize(frames), outR.resize(frames);
        for (std::size_t i = 0; i < frames; ++i) inL[i] = io[i * 2], inR[i] = io[i * 2 + 1];
        events.events.clear();
        {
            std::unique_lock lock(queueMutex, std::try_to_lock);
            if (lock.owns_lock()) {
                for (const auto& [id, v] : queue) {
                    clap_event_param_value_t e{};
                    e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
                    e.param_id = id;
                    e.note_id = e.port_index = e.channel = e.key = -1;
                    e.value = v;
                    events.events.push_back(e);
                }
                queue.clear();
            }
        }
        float* ins[2] = {inL.data(), inR.data()};
        float* outs[2] = {outL.data(), outR.data()};
        clap_audio_buffer_t inBuf{ins, nullptr, 2, 0, 0}, outBuf{outs, nullptr, 2, 0, 0};
        clap_process_t p{};
        p.steady_time = -1;
        p.frames_count = static_cast<uint32_t>(frames);
        p.audio_inputs = audioInputs > 0 ? &inBuf : nullptr;
        p.audio_outputs = &outBuf;
        p.audio_inputs_count = audioInputs > 0 ? 1 : 0;
        p.audio_outputs_count = 1;
        p.in_events = &events.in;
        p.out_events = &g_nullOut;
        if (plugin->process(plugin, &p) == CLAP_PROCESS_ERROR) return;
        for (std::size_t i = 0; i < frames; ++i) io[i * 2] = outL[i], io[i * 2 + 1] = outR[i];
    }

    std::vector<PluginParam> params() override {
        std::vector<PluginParam> out;
        if (!paramsExt) return out;
        const uint32_t n = paramsExt->count(plugin);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t pi{};
            if (!paramsExt->get_info(plugin, i, &pi) || (pi.flags & CLAP_PARAM_IS_HIDDEN)) continue;
            PluginParam p;
            p.id = std::to_string(pi.id);
            p.name = pi.name;
            p.min = pi.min_value;
            p.max = pi.max_value;
            p.def = pi.default_value;
            p.stepped = (pi.flags & CLAP_PARAM_IS_STEPPED) != 0;
            double v = pi.default_value;
            paramsExt->get_value(plugin, pi.id, &v);
            p.value = v;
            out.push_back(std::move(p));
        }
        return out;
    }

    void setParam(const std::string& id, double value) override {
        const clap_id cid = static_cast<clap_id>(std::stoul(id));
        if (!processing.load() && paramsExt && paramsExt->flush) {
            // Nicht aktiv: direkt übergeben
            EventList l;
            clap_event_param_value_t e{};
            e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            e.param_id = cid;
            e.note_id = e.port_index = e.channel = e.key = -1;
            e.value = value;
            l.events.push_back(e);
            paramsExt->flush(plugin, &l.in, &g_nullOut);
            return;
        }
        std::lock_guard lock(queueMutex);
        queue.emplace_back(cid, value);
    }

    std::string saveState() override {
        std::string data;
        if (!stateExt) return data;
        clap_ostream_t os{&data, [](const clap_ostream_t* s, const void* buf, uint64_t size) -> int64_t {
                              static_cast<std::string*>(s->ctx)->append(static_cast<const char*>(buf), static_cast<std::size_t>(size));
                              return static_cast<int64_t>(size);
                          }};
        if (!stateExt->save(plugin, &os)) data.clear();
        return data;
    }

    bool loadState(const std::string& data) override {
        if (!stateExt || data.empty()) return false;
        struct Reader {
            const std::string* d;
            std::size_t pos;
        } r{&data, 0};
        clap_istream_t is{&r, [](const clap_istream_t* s, void* buf, uint64_t size) -> int64_t {
                              auto* rd = static_cast<Reader*>(s->ctx);
                              const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(size), rd->d->size() - rd->pos);
                              std::memcpy(buf, rd->d->data() + rd->pos, n);
                              rd->pos += n;
                              return static_cast<int64_t>(n);
                          }};
        return stateExt->load(plugin, &is);
    }

    static const char* guiApi() {
#ifdef _WIN32
        return CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
        return CLAP_WINDOW_API_COCOA;
#else
        return CLAP_WINDOW_API_X11;
#endif
    }

    bool hasEditor() override { return guiExt && guiExt->is_api_supported(plugin, guiApi(), false); }

    bool openEditor(void* parent, int& w, int& h) override {
        if (!hasEditor()) return false;
        closeEditor();
        if (!guiExt->create(plugin, guiApi(), false)) return false;
        guiCreated = true;
        clap_window_t win{};
        win.api = guiApi();
#ifdef _WIN32
        win.win32 = parent;
#elif defined(__APPLE__)
        win.cocoa = parent;
#else
        win.x11 = reinterpret_cast<clap_xwnd>(parent);
#endif
        uint32_t uw = 640, uh = 400;
        guiExt->get_size(plugin, &uw, &uh);
        if (!guiExt->set_parent(plugin, &win)) {
            closeEditor();
            return false;
        }
        guiExt->show(plugin);
        w = static_cast<int>(uw);
        h = static_cast<int>(uh);
        return true;
    }

    void closeEditor() override {
        if (!guiCreated) return;
        guiExt->hide(plugin);
        guiExt->destroy(plugin);
        guiCreated = false;
    }
};

const void* hostExtension(const clap_host_t* h, const char* id) {
    auto* self = static_cast<ClapPlugin*>(h->host_data);
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &self->hostGui;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &self->hostParams;
    return nullptr;
}
}  // namespace

std::vector<PluginInfo> scanClap(const std::string& path) {
    std::vector<PluginInfo> out;
    auto mod = openModule(path, nullptr);
    if (!mod) return out;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(mod->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) return out;
    const uint32_t n = factory->get_plugin_count(factory);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, i);
        if (!d) continue;
        PluginInfo info;
        info.format = "clap";
        info.id = d->id;
        info.name = d->name ? d->name : d->id;
        info.vendor = d->vendor ? d->vendor : "";
        info.version = d->version ? d->version : "";
        info.path = path;
        info.instrument = hasFeature(d, CLAP_PLUGIN_FEATURE_INSTRUMENT) && !hasFeature(d, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        out.push_back(std::move(info));
    }
    return out;
}

std::unique_ptr<AudioPlugin> loadClap(const PluginInfo& info, int rate, int maxBlock, std::string* error) {
    auto mod = openModule(info.path, error);
    if (!mod) return nullptr;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(mod->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) {
        if (error) *error = "CLAP: keine Plugin-Fabrik";
        return nullptr;
    }
    auto p = std::make_unique<ClapPlugin>();
    p->mod = mod;
    p->host = {CLAP_VERSION, p.get(), "LXStudio", "LoggX", "https://github.com/conlog06/loggx-studio-cl", "1.0",
               hostExtension, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
    p->hostGui.resize_hints_changed = [](const clap_host_t*) {};
    p->hostGui.request_resize = [](const clap_host_t* h, uint32_t w, uint32_t hh) {
        auto* self = static_cast<ClapPlugin*>(h->host_data);
        if (self->onResize) self->onResize(static_cast<int>(w), static_cast<int>(hh));
        return true;
    };
    p->hostGui.request_show = [](const clap_host_t*) { return false; };
    p->hostGui.request_hide = [](const clap_host_t*) { return false; };
    p->hostGui.closed = [](const clap_host_t*, bool) {};
    p->hostParams.rescan = [](const clap_host_t*, clap_param_rescan_flags) {};
    p->hostParams.clear = [](const clap_host_t*, clap_id, clap_param_clear_flags) {};
    p->hostParams.request_flush = [](const clap_host_t* h) {
        auto* self = static_cast<ClapPlugin*>(h->host_data);
        if (self->onEdited) self->onEdited();
    };
    p->plugin = factory->create_plugin(factory, &p->host, info.id.c_str());
    if (!p->plugin || !p->plugin->init(p->plugin)) {
        if (p->plugin) p->plugin->destroy(p->plugin);
        p->plugin = nullptr;
        if (error) *error = "CLAP: Plugin lässt sich nicht starten";
        return nullptr;
    }
    p->maxBlock = static_cast<uint32_t>(maxBlock);
    if (const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(p->plugin->get_extension(p->plugin, CLAP_EXT_AUDIO_PORTS)))
        p->audioInputs = ports->count(p->plugin, true);
    p->paramsExt = static_cast<const clap_plugin_params_t*>(p->plugin->get_extension(p->plugin, CLAP_EXT_PARAMS));
    p->stateExt = static_cast<const clap_plugin_state_t*>(p->plugin->get_extension(p->plugin, CLAP_EXT_STATE));
    p->guiExt = static_cast<const clap_plugin_gui_t*>(p->plugin->get_extension(p->plugin, CLAP_EXT_GUI));
    if (!p->plugin->activate(p->plugin, rate, 1, static_cast<uint32_t>(maxBlock))) {
        p->plugin->destroy(p->plugin);
        p->plugin = nullptr;
        if (error) *error = "CLAP: Plugin lässt sich nicht aktivieren";
        return nullptr;
    }
    p->setInfo(info);
    return p;
}

}  // namespace loggx
