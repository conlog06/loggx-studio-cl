// Test-Plugin (VST3): Verstärkung mit einem Parameter, Komponente und
// Controller in einem Objekt, Zustand speicherbar.
#include <atomic>
#include <cstring>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
}  // namespace Vst
}  // namespace Steinberg

namespace {
const TUID kCid = INLINE_UID(0x4C585445, 0x53544741, 0x494E3031, 0x32333435);

void copy16(String128 dst, const char* s) {
    int i = 0;
    for (; s[i] && i < 127; ++i) dst[i] = static_cast<TChar>(s[i]);
    dst[i] = 0;
}

class Gain : public IComponent, public IAudioProcessor, public IEditController {
public:
    std::atomic<uint32> ref{1};
    double gain = 0.5;   // normiert: 0,5 → Faktor 1 (0..2)

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IComponent::iid) || FUnknownPrivate::iidEqual(iid, IPluginBase::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
            *obj = static_cast<IComponent*>(this);
        else if (FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid))
            *obj = static_cast<IAudioProcessor*>(this);
        else if (FUnknownPrivate::iidEqual(iid, IEditController::iid))
            *obj = static_cast<IEditController*>(this);
        else
            return *obj = nullptr, kNoInterface;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++ref; }
    uint32 PLUGIN_API release() override {
        const uint32 r = --ref;
        if (r == 0) delete this;
        return r;
    }
    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }
    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType t, BusDirection) override { return t == kAudio ? 1 : 0; }
    tresult PLUGIN_API getBusInfo(MediaType t, BusDirection d, int32 i, BusInfo& b) override {
        if (t != kAudio || i != 0) return kResultFalse;
        b.mediaType = kAudio;
        b.direction = d;
        b.channelCount = 2;
        copy16(b.name, "Stereo");
        b.busType = kMain;
        b.flags = BusInfo::kDefaultActive;
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kResultFalse; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
    tresult PLUGIN_API setState(IBStream* s) override {
        double g = 0.5;
        int32 got = 0;
        if (!s || s->read(&g, sizeof g, &got) != kResultOk || got != sizeof g) return kResultFalse;
        gain = g;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* s) override {
        int32 w = 0;
        return s ? s->write(&gain, sizeof gain, &w) : kResultFalse;
    }
    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement*, int32) override { return kResultOk; }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement& a) override { return a = SpeakerArr::kStereo, kResultOk; }
    tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    tresult PLUGIN_API process(ProcessData& d) override {
        if (auto* ch = d.inputParameterChanges)
            for (int32 i = 0; i < ch->getParameterCount(); ++i) {
                auto* q = ch->getParameterData(i);
                int32 off = 0;
                ParamValue v = 0;
                if (q && q->getParameterId() == 3 && q->getPointCount() > 0 && q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) gain = v;
            }
        const float g = static_cast<float>(gain * 2.0);
        for (int32 c = 0; c < 2; ++c)
            for (int32 i = 0; i < d.numSamples; ++i) d.outputs[0].channelBuffers32[c][i] = d.inputs[0].channelBuffers32[c][i] * g;
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override { return 0; }
    // IEditController
    tresult PLUGIN_API setComponentState(IBStream* s) override { return setState(s); }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo& p) override {
        if (i != 0) return kResultFalse;
        std::memset(&p, 0, sizeof p);
        p.id = 3;
        copy16(p.title, "Gain");
        copy16(p.shortTitle, "Gain");
        copy16(p.units, "x");
        p.defaultNormalizedValue = 0.5;
        p.flags = ParameterInfo::kCanAutomate;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override { return kResultFalse; }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kResultFalse; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v * 2; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v / 2; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id == 3 ? gain : 0; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override { return id == 3 ? (gain = v, kResultOk) : kResultFalse; }
    tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
    IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }
};

class Factory : public IPluginFactory {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* i) override {
        std::memset(i, 0, sizeof *i);
        std::strcpy(i->vendor, "LoggX");
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* i) override {
        if (index != 0) return kResultFalse;
        std::memset(i, 0, sizeof *i);
        std::memcpy(i->cid, kCid, sizeof(TUID));
        i->cardinality = PClassInfo::kManyInstances;
        std::strcpy(i->category, kVstAudioEffectClass);
        std::strcpy(i->name, "LX Test Gain VST3");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override {
        if (!FUnknownPrivate::iidEqual(cid, kCid)) return *obj = nullptr, kResultFalse;
        auto* g = new Gain;
        const tresult r = g->queryInterface(iid, obj);
        g->release();
        return r;
    }
};
Factory g_factory;
}  // namespace

#if defined(_WIN32)
#define LX_EXPORT extern "C" __declspec(dllexport)
#else
#define LX_EXPORT extern "C" __attribute__((visibility("default")))
#endif
LX_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory() { return &g_factory; }
LX_EXPORT bool InitDll() { return true; }
LX_EXPORT bool ExitDll() { return true; }
LX_EXPORT bool ModuleEntry(void*) { return true; }
LX_EXPORT bool ModuleExit() { return true; }
#ifdef __APPLE__
LX_EXPORT bool bundleEntry(void*) { return true; }
LX_EXPORT bool bundleExit() { return true; }
#endif
