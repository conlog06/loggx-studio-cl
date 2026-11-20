// VST3-Host gegen die offiziellen Schnittstellen (Steinberg pluginterfaces,
// MIT). Effekt-Plugins, Stereo, 32 Bit. Eigene kleine Umsetzungen von Stream,
// Parameterwarteschlange, Nachrichten und Host-Anwendung.
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

#include "loggx/engine/Plugins.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace loggx {

namespace fs = std::filesystem;

namespace {
fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string fromU16(const char16* s) {
    std::string out;
    for (; s && *s; ++s) {
        const char32_t c = static_cast<char32_t>(*s);
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += static_cast<char>(0xC0 | (c >> 6)), out += static_cast<char>(0x80 | (c & 0x3F));
        else out += static_cast<char>(0xE0 | (c >> 12)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

std::string uidHex(const TUID uid) {
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    for (int i = 0; i < 16; ++i) s += hex[(static_cast<unsigned char>(uid[i]) >> 4) & 15], s += hex[static_cast<unsigned char>(uid[i]) & 15];
    return s;
}
bool uidFromHex(const std::string& s, TUID out) {
    if (s.size() != 32) return false;
    for (int i = 0; i < 16; ++i) out[i] = static_cast<char>(std::stoi(s.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16));
    return true;
}

// ---- kleine COM-Basis
template <class... I>
class Unknown : public I... {
public:
    virtual ~Unknown() = default;
    uint32 PLUGIN_API addRef() override { return ++m_ref; }
    uint32 PLUGIN_API release() override {
        const uint32 r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

protected:
    std::atomic<uint32> m_ref{1};
};
#define LX_QUERY(Iface)                                         \
    if (FUnknownPrivate::iidEqual(iid, Iface::iid)) {           \
        addRef();                                               \
        *obj = static_cast<Iface*>(this);                       \
        return kResultOk;                                       \
    }

class MemoryStream final : public Unknown<IBStream> {
public:
    std::string data;
    int64 pos = 0;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IBStream)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    tresult PLUGIN_API read(void* buffer, int32 n, int32* got) override {
        const int64 avail = std::max<int64>(0, static_cast<int64>(data.size()) - pos);
        const int32 k = static_cast<int32>(std::min<int64>(n, avail));
        if (k > 0) std::memcpy(buffer, data.data() + pos, static_cast<std::size_t>(k));
        pos += k;
        if (got) *got = k;
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 n, int32* written) override {
        if (pos + n > static_cast<int64>(data.size())) data.resize(static_cast<std::size_t>(pos + n));
        std::memcpy(data.data() + pos, buffer, static_cast<std::size_t>(n));
        pos += n;
        if (written) *written = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64* result) override {
        if (mode == kIBSeekCur) p += pos;
        else if (mode == kIBSeekEnd) p += static_cast<int64>(data.size());
        pos = std::clamp<int64>(p, 0, static_cast<int64>(data.size()));
        if (result) *result = pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* p) override {
        if (p) *p = pos;
        return kResultOk;
    }
};

class ParamQueue final : public Unknown<IParamValueQueue> {
public:
    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> points;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IParamValueQueue)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
    tresult PLUGIN_API getPoint(int32 i, int32& offset, ParamValue& v) override {
        if (i < 0 || i >= static_cast<int32>(points.size())) return kResultFalse;
        offset = points[static_cast<std::size_t>(i)].first;
        v = points[static_cast<std::size_t>(i)].second;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset, ParamValue v, int32& index) override {
        index = static_cast<int32>(points.size());
        points.emplace_back(offset, v);
        return kResultOk;
    }
};

class ParamChanges final : public Unknown<IParameterChanges> {
public:
    std::vector<ParamQueue*> queues;   // wiederverwendet
    int32 used = 0;
    ~ParamChanges() override {
        for (auto* q : queues) q->release();
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IParameterChanges)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    void clear() {
        for (int32 i = 0; i < used; ++i) queues[static_cast<std::size_t>(i)]->points.clear();
        used = 0;
    }
    int32 PLUGIN_API getParameterCount() override { return used; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < used ? queues[static_cast<std::size_t>(i)] : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (int32 i = 0; i < used; ++i)
            if (queues[static_cast<std::size_t>(i)]->id == id) return index = i, queues[static_cast<std::size_t>(i)];
        if (used == static_cast<int32>(queues.size())) queues.push_back(new ParamQueue);
        ParamQueue* q = queues[static_cast<std::size_t>(used)];
        q->id = id;
        q->points.clear();
        index = used++;
        return q;
    }
};

class AttrList final : public Unknown<IAttributeList> {
public:
    std::map<std::string, int64> ints;
    std::map<std::string, double> floats;
    std::map<std::string, std::u16string> strings;
    std::map<std::string, std::string> bins;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IAttributeList)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    tresult PLUGIN_API setInt(AttrID id, int64 v) override { return ints[id] = v, kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64& v) override {
        auto it = ints.find(id);
        return it == ints.end() ? kResultFalse : (v = it->second, kResultOk);
    }
    tresult PLUGIN_API setFloat(AttrID id, double v) override { return floats[id] = v, kResultOk; }
    tresult PLUGIN_API getFloat(AttrID id, double& v) override {
        auto it = floats.find(id);
        return it == floats.end() ? kResultFalse : (v = it->second, kResultOk);
    }
    tresult PLUGIN_API setString(AttrID id, const TChar* s) override {
        std::u16string v;
        for (; s && *s; ++s) v += static_cast<char16_t>(*s);
        strings[id] = v;
        return kResultOk;
    }
    tresult PLUGIN_API getString(AttrID id, TChar* s, uint32 sizeBytes) override {
        auto it = strings.find(id);
        if (it == strings.end() || sizeBytes < sizeof(TChar)) return kResultFalse;
        const std::size_t n = std::min<std::size_t>(it->second.size(), sizeBytes / sizeof(TChar) - 1);
        for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<TChar>(it->second[i]);
        s[n] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* d, uint32 size) override {
        bins[id] = std::string(static_cast<const char*>(d), size);
        return kResultOk;
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& d, uint32& size) override {
        auto it = bins.find(id);
        if (it == bins.end()) return kResultFalse;
        d = it->second.data();
        size = static_cast<uint32>(it->second.size());
        return kResultOk;
    }
};

class Message final : public Unknown<IMessage> {
public:
    std::string msgId;
    AttrList* attrs = new AttrList;
    ~Message() override { attrs->release(); }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IMessage)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    FIDString PLUGIN_API getMessageID() override { return msgId.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { msgId = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { return attrs; }
};

class HostApp final : public Unknown<IHostApplication> {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IHostApplication)
        LX_QUERY(FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    tresult PLUGIN_API getName(String128 name) override {
        const char* n = "LXStudio";
        int i = 0;
        for (; n[i]; ++i) name[i] = static_cast<TChar>(n[i]);
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(cid, IMessage::iid) && FUnknownPrivate::iidEqual(iid, IMessage::iid)) {
            *obj = static_cast<IMessage*>(new Message);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(cid, IAttributeList::iid) && FUnknownPrivate::iidEqual(iid, IAttributeList::iid)) {
            *obj = static_cast<IAttributeList*>(new AttrList);
            return kResultOk;
        }
        *obj = nullptr;
        return kResultFalse;
    }
};
HostApp* g_host = new HostApp;   // lebt so lange wie das Programm

// ---- Modul (Bibliothek + Fabrik)
struct Vst3Module {
    DynLib lib;
#ifdef __APPLE__
    CFBundleRef bundle = nullptr;
#endif
    IPluginFactory* factory = nullptr;
    using ExitFn = bool (*)();
    ExitFn exitFn = nullptr;
    ~Vst3Module() {
        if (factory) factory->release();
        if (exitFn) exitFn();
#ifdef __APPLE__
        if (bundle) CFRelease(bundle);
#endif
    }
};
std::mutex g_vmMutex;
std::map<std::string, std::weak_ptr<Vst3Module>> g_vmodules;

std::shared_ptr<Vst3Module> openVst3(const std::string& path, std::string* error) {
    std::lock_guard lock(g_vmMutex);
    if (auto m = g_vmodules[path].lock()) return m;
    auto m = std::make_shared<Vst3Module>();
    using FactoryFn = IPluginFactory*(PLUGIN_API*)();
    FactoryFn getFactory = nullptr;
#ifdef __APPLE__
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), true);
    m->bundle = url ? CFBundleCreate(nullptr, url) : nullptr;
    if (url) CFRelease(url);
    if (!m->bundle || !CFBundleLoadExecutable(m->bundle)) {
        if (error) *error = "VST3-Bundle lässt sich nicht laden";
        return nullptr;
    }
    using EntryFn = bool (*)(CFBundleRef);
    auto entry = reinterpret_cast<EntryFn>(CFBundleGetFunctionPointerForName(m->bundle, CFSTR("bundleEntry")));
    if (entry && !entry(m->bundle)) {
        if (error) *error = "VST3: bundleEntry fehlgeschlagen";
        return nullptr;
    }
    m->exitFn = reinterpret_cast<Vst3Module::ExitFn>(CFBundleGetFunctionPointerForName(m->bundle, CFSTR("bundleExit")));
    getFactory = reinterpret_cast<FactoryFn>(CFBundleGetFunctionPointerForName(m->bundle, CFSTR("GetPluginFactory")));
#else
    std::error_code ec;
    fs::path file = u8path(path);
    if (fs::is_directory(file, ec)) {
#ifdef _WIN32
        file = file / "Contents" / "x86_64-win" / file.filename();
#else
        file = file / "Contents" / "x86_64-linux" / (file.stem().string() + ".so");
#endif
    }
    const auto u8 = file.u8string();
    if (!m->lib.open(std::string(u8.begin(), u8.end()), error)) return nullptr;
#ifdef _WIN32
    if (auto init = reinterpret_cast<bool (*)()>(m->lib.symbol("InitDll"))) init();
    m->exitFn = reinterpret_cast<Vst3Module::ExitFn>(m->lib.symbol("ExitDll"));
#else
    if (auto entry = reinterpret_cast<bool (*)(void*)>(m->lib.symbol("ModuleEntry")); entry && !entry(m->lib.nativeHandle())) {
        if (error) *error = "VST3: ModuleEntry fehlgeschlagen";
        return nullptr;
    }
    m->exitFn = reinterpret_cast<Vst3Module::ExitFn>(m->lib.symbol("ModuleExit"));
#endif
    getFactory = reinterpret_cast<FactoryFn>(m->lib.symbol("GetPluginFactory"));
#endif
    m->factory = getFactory ? getFactory() : nullptr;
    if (!m->factory) {
        if (error) *error = "Kein gültiges VST3-Plugin";
        return nullptr;
    }
    g_vmodules[path] = m;
    return m;
}

template <class T>
T* queryIface(FUnknown* u) {
    T* out = nullptr;
    if (u && u->queryInterface(T::iid, reinterpret_cast<void**>(&out)) == kResultOk) return out;
    return nullptr;
}

class Vst3Plugin;

class Handler final : public Unknown<IComponentHandler, IPlugFrame> {
public:
    Vst3Plugin* owner = nullptr;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        LX_QUERY(IComponentHandler)
        LX_QUERY(IPlugFrame)
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_ref; }
    uint32 PLUGIN_API release() override {
        const uint32 r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override;
    tresult PLUGIN_API endEdit(ParamID) override;
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override;
};

class Vst3Plugin final : public AudioPlugin {
public:
    std::shared_ptr<Vst3Module> mod;
    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;
    IConnectionPoint *cpComp = nullptr, *cpCtrl = nullptr;
    Handler* handler = new Handler;
    IPlugView* view = nullptr;
    bool separateController = false;
    std::atomic<bool> processing{false};
    std::mutex queueMutex;
    std::vector<std::pair<ParamID, ParamValue>> queue;
    ParamChanges* changes = new ParamChanges;
    std::vector<float> inL, inR, outL, outR;
    int maxBlock = 4096;

    ~Vst3Plugin() override {
        closeEditor();
        if (processor && processing.exchange(false)) processor->setProcessing(false);
        if (component) component->setActive(false);
        if (cpComp && cpCtrl) cpComp->disconnect(cpCtrl), cpCtrl->disconnect(cpComp);
        if (cpComp) cpComp->release();
        if (cpCtrl) cpCtrl->release();
        if (controller) {
            controller->setComponentHandler(nullptr);
            if (separateController) controller->terminate();
            controller->release();
        }
        if (processor) processor->release();
        if (component) {
            component->terminate();
            component->release();
        }
        changes->release();
        handler->owner = nullptr;
        handler->release();
    }

    void queueParam(ParamID id, ParamValue v) {
        std::lock_guard lock(queueMutex);
        queue.emplace_back(id, v);
    }

    void process(float* io, std::size_t frames) override {
        if (!processing.load()) {
            processor->setProcessing(true);
            processing = true;
        }
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t n = std::min<std::size_t>(frames - done, static_cast<std::size_t>(maxBlock));
            inL.resize(n), inR.resize(n), outL.resize(n), outR.resize(n);
            float* p = io + done * 2;
            for (std::size_t i = 0; i < n; ++i) inL[i] = p[i * 2], inR[i] = p[i * 2 + 1];
            changes->clear();
            {
                std::unique_lock lock(queueMutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    for (const auto& [id, v] : queue) {
                        int32 idx = 0, pidx = 0;
                        if (auto* q = changes->addParameterData(id, idx)) q->addPoint(0, v, pidx);
                    }
                    queue.clear();
                }
            }
            float* ins[2] = {inL.data(), inR.data()};
            float* outs[2] = {outL.data(), outR.data()};
            AudioBusBuffers in{}, out{};
            in.numChannels = out.numChannels = 2;
            in.channelBuffers32 = ins;
            out.channelBuffers32 = outs;
            ProcessData d{};
            d.processMode = kRealtime;
            d.symbolicSampleSize = kSample32;
            d.numSamples = static_cast<int32>(n);
            d.numInputs = d.numOutputs = 1;
            d.inputs = &in;
            d.outputs = &out;
            d.inputParameterChanges = changes;
            if (processor->process(d) == kResultOk)
                for (std::size_t i = 0; i < n; ++i) p[i * 2] = outL[i], p[i * 2 + 1] = outR[i];
            done += n;
        }
    }

    std::vector<PluginParam> params() override {
        std::vector<PluginParam> out;
        if (!controller) return out;
        const int32 n = controller->getParameterCount();
        for (int32 i = 0; i < n; ++i) {
            ParameterInfo pi{};
            if (controller->getParameterInfo(i, pi) != kResultOk) continue;
            if ((pi.flags & ParameterInfo::kIsReadOnly) || (pi.flags & ParameterInfo::kIsHidden) || (pi.flags & ParameterInfo::kIsProgramChange)) continue;
            PluginParam p;
            p.id = std::to_string(pi.id);
            p.name = fromU16(pi.title);
            p.unit = fromU16(pi.units);
            p.min = 0;
            p.max = 1;
            p.def = pi.defaultNormalizedValue;
            p.stepped = pi.stepCount > 0;
            p.value = controller->getParamNormalized(pi.id);
            out.push_back(std::move(p));
        }
        return out;
    }

    void setParam(const std::string& id, double value) override {
        const ParamID pid = static_cast<ParamID>(std::stoul(id));
        value = std::clamp(value, 0.0, 1.0);
        if (controller) controller->setParamNormalized(pid, value);
        queueParam(pid, value);
    }

    std::string saveState() override {
        // Format: [4 Byte Länge Komponente][Komponente][Controller]
        auto* s1 = new MemoryStream;
        auto* s2 = new MemoryStream;
        component->getState(s1);
        if (controller) controller->getState(s2);
        const uint32_t n = static_cast<uint32_t>(s1->data.size());
        std::string out(reinterpret_cast<const char*>(&n), 4);
        out += s1->data;
        out += s2->data;
        s1->release();
        s2->release();
        return out;
    }

    bool loadState(const std::string& data) override {
        if (data.size() < 4) return false;
        uint32_t n = 0;
        std::memcpy(&n, data.data(), 4);
        if (4 + static_cast<std::size_t>(n) > data.size()) return false;
        auto* s1 = new MemoryStream;
        s1->data = data.substr(4, n);
        const bool ok = component->setState(s1) == kResultOk;
        if (controller) {
            s1->pos = 0;
            controller->setComponentState(s1);
            auto* s2 = new MemoryStream;
            s2->data = data.substr(4 + n);
            if (!s2->data.empty()) controller->setState(s2);
            s2->release();
        }
        s1->release();
        return ok;
    }

    static FIDString platformType() {
#ifdef _WIN32
        return kPlatformTypeHWND;
#elif defined(__APPLE__)
        return kPlatformTypeNSView;
#else
        return kPlatformTypeX11EmbedWindowID;
#endif
    }

    bool hasEditor() override {
#if defined(_WIN32) || defined(__APPLE__)
        return controller != nullptr;
#else
        return false;   // X11 bräuchte eine eigene Ereignisschleife (IRunLoop)
#endif
    }

    bool openEditor(void* parent, int& w, int& h) override {
        if (!hasEditor()) return false;
        closeEditor();
        view = controller->createView(ViewType::kEditor);
        if (!view) return false;
        if (view->isPlatformTypeSupported(platformType()) != kResultTrue) {
            view->release();
            view = nullptr;
            return false;
        }
        view->setFrame(handler);
        if (view->attached(parent, platformType()) != kResultOk) {
            view->release();
            view = nullptr;
            return false;
        }
        ViewRect r{};
        view->getSize(&r);
        w = r.getWidth();
        h = r.getHeight();
        return true;
    }

    void closeEditor() override {
        if (!view) return;
        view->removed();
        view->setFrame(nullptr);
        view->release();
        view = nullptr;
    }
};

tresult PLUGIN_API Handler::performEdit(ParamID id, ParamValue v) {
    if (owner) owner->queueParam(id, v);
    return kResultOk;
}
tresult PLUGIN_API Handler::endEdit(ParamID) {
    if (owner && owner->onEdited) owner->onEdited();
    return kResultOk;
}
tresult PLUGIN_API Handler::resizeView(IPlugView* v, ViewRect* r) {
    if (!r) return kInvalidArgument;
    if (owner && owner->onResize) owner->onResize(r->getWidth(), r->getHeight());
    if (v) v->onSize(r);
    return kResultOk;
}
}  // namespace

std::vector<PluginInfo> scanVst3(const std::string& path) {
    std::vector<PluginInfo> out;
    auto mod = openVst3(path, nullptr);
    if (!mod) return out;
    PFactoryInfo fi{};
    mod->factory->getFactoryInfo(&fi);
    auto* f2 = queryIface<IPluginFactory2>(mod->factory);
    const int32 n = mod->factory->countClasses();
    for (int32 i = 0; i < n; ++i) {
        PClassInfo ci{};
        if (mod->factory->getClassInfo(i, &ci) != kResultOk || std::strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
        PluginInfo info;
        info.format = "vst3";
        info.id = uidHex(ci.cid);
        info.name = ci.name;
        info.vendor = fi.vendor;
        info.path = path;
        if (f2) {
            PClassInfo2 c2{};
            if (f2->getClassInfo2(i, &c2) == kResultOk) {
                info.vendor = c2.vendor[0] ? c2.vendor : info.vendor;
                info.version = c2.version;
                const std::string sub = c2.subCategories;
                info.instrument = sub.find("Instrument") != std::string::npos && sub.find("Fx") == std::string::npos;
            }
        }
        out.push_back(std::move(info));
    }
    if (f2) f2->release();
    return out;
}

std::unique_ptr<AudioPlugin> loadVst3(const PluginInfo& info, int rate, int maxBlock, std::string* error) {
    auto mod = openVst3(info.path, error);
    if (!mod) return nullptr;
    TUID cid{};
    if (!uidFromHex(info.id, cid)) {
        if (error) *error = "VST3: ungültige Klassen-Id";
        return nullptr;
    }
    auto p = std::make_unique<Vst3Plugin>();
    p->mod = mod;
    p->handler->owner = p.get();
    p->maxBlock = maxBlock;
    if (mod->factory->createInstance(cid, IComponent::iid, reinterpret_cast<void**>(&p->component)) != kResultOk || !p->component) {
        if (error) *error = "VST3: Plugin lässt sich nicht erzeugen";
        return nullptr;
    }
    if (p->component->initialize(g_host) != kResultOk) {
        p->component->release();
        p->component = nullptr;
        if (error) *error = "VST3: Plugin lässt sich nicht starten";
        return nullptr;
    }
    p->processor = queryIface<IAudioProcessor>(p->component);
    if (!p->processor) {
        if (error) *error = "VST3: kein Audio-Prozessor";
        return nullptr;
    }
    // Controller: im selben Objekt oder eigene Klasse
    p->controller = queryIface<IEditController>(p->component);
    if (!p->controller) {
        TUID ccid{};
        if (p->component->getControllerClassId(ccid) == kResultOk &&
            mod->factory->createInstance(ccid, IEditController::iid, reinterpret_cast<void**>(&p->controller)) == kResultOk && p->controller) {
            p->separateController = true;
            if (p->controller->initialize(g_host) != kResultOk) {
                p->controller->release();
                p->controller = nullptr;
            }
        }
    }
    if (p->controller) {
        p->controller->setComponentHandler(p->handler);
        if (p->separateController) {
            p->cpComp = queryIface<IConnectionPoint>(p->component);
            p->cpCtrl = queryIface<IConnectionPoint>(p->controller);
            if (p->cpComp && p->cpCtrl) p->cpComp->connect(p->cpCtrl), p->cpCtrl->connect(p->cpComp);
        }
        auto* s = new MemoryStream;
        if (p->component->getState(s) == kResultOk) {
            s->pos = 0;
            p->controller->setComponentState(s);
        }
        s->release();
    }
    // Stereo rein/raus
    SpeakerArrangement st = SpeakerArr::kStereo;
    const int32 nIn = p->component->getBusCount(kAudio, kInput), nOut = p->component->getBusCount(kAudio, kOutput);
    if (nOut < 1) {
        if (error) *error = "VST3: Plugin hat keinen Audio-Ausgang";
        return nullptr;
    }
    p->processor->setBusArrangements(nIn > 0 ? &st : nullptr, nIn > 0 ? 1 : 0, &st, 1);
    if (nIn > 0) p->component->activateBus(kAudio, kInput, 0, true);
    p->component->activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, maxBlock, static_cast<SampleRate>(rate)};
    if (p->processor->setupProcessing(setup) != kResultOk || p->component->setActive(true) != kResultOk) {
        if (error) *error = "VST3: Plugin lässt sich nicht einrichten";
        return nullptr;
    }
    p->setInfo(info);
    return p;
}

}  // namespace loggx
