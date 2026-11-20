// Audio Units (macOS): Effekte über AudioToolbox (C-Schnittstelle).
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#include "loggx/engine/Plugins.h"

namespace loggx {

namespace {
std::string fourcc(OSType v) {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) s[static_cast<std::size_t>(i)] = static_cast<char>((v >> (24 - 8 * i)) & 0xFF);
    return s;
}
OSType toOSType(const std::string& s) {
    OSType v = 0;
    for (std::size_t i = 0; i < 4; ++i) v = (v << 8) | static_cast<unsigned char>(i < s.size() ? s[i] : ' ');
    return v;
}
std::string cfString(CFStringRef s) {
    if (!s) return {};
    char buf[512];
    return CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8) ? std::string(buf) : std::string();
}

class AuPlugin final : public AudioPlugin {
public:
    AudioUnit unit = nullptr;
    std::vector<float> inL, inR;
    int maxBlock = 4096;
    Float64 sampleTime = 0;

    ~AuPlugin() override {
        if (!unit) return;
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
    }

    static OSStatus input(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* io) {
        auto* self = static_cast<AuPlugin*>(ref);
        if (io->mNumberBuffers < 2) return kAudioUnitErr_InvalidParameter;
        std::memcpy(io->mBuffers[0].mData, self->inL.data(), frames * sizeof(float));
        std::memcpy(io->mBuffers[1].mData, self->inR.data(), frames * sizeof(float));
        return noErr;
    }

    void process(float* io, std::size_t frames) override {
        std::size_t done = 0;
        std::vector<float> outL, outR;
        while (done < frames) {
            const std::size_t n = std::min<std::size_t>(frames - done, static_cast<std::size_t>(maxBlock));
            inL.resize(n), inR.resize(n), outL.assign(n, 0.0f), outR.assign(n, 0.0f);
            float* p = io + done * 2;
            for (std::size_t i = 0; i < n; ++i) inL[i] = p[i * 2], inR[i] = p[i * 2 + 1];
            struct {
                AudioBufferList list;
                AudioBuffer second;
            } buffers{};
            buffers.list.mNumberBuffers = 2;
            buffers.list.mBuffers[0] = {1, static_cast<UInt32>(n * sizeof(float)), outL.data()};
            buffers.second = {1, static_cast<UInt32>(n * sizeof(float)), outR.data()};
            AudioTimeStamp ts{};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = sampleTime;
            AudioUnitRenderActionFlags flags = 0;
            if (AudioUnitRender(unit, &flags, &ts, 0, static_cast<UInt32>(n), &buffers.list) == noErr)
                for (std::size_t i = 0; i < n; ++i) p[i * 2] = outL[i], p[i * 2 + 1] = outR[i];
            sampleTime += static_cast<Float64>(n);
            done += n;
        }
    }

    std::vector<PluginParam> params() override {
        std::vector<PluginParam> out;
        UInt32 size = 0;
        if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nullptr) != noErr) return out;
        std::vector<AudioUnitParameterID> ids(size / sizeof(AudioUnitParameterID));
        if (ids.empty() || AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size) != noErr) return out;
        for (AudioUnitParameterID id : ids) {
            AudioUnitParameterInfo pi{};
            UInt32 s = sizeof pi;
            if (AudioUnitGetProperty(unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            if (!(pi.flags & kAudioUnitParameterFlag_IsWritable)) continue;
            PluginParam p;
            p.id = std::to_string(id);
            p.name = (pi.flags & kAudioUnitParameterFlag_HasCFNameString) ? cfString(pi.cfNameString) : std::string(pi.name);
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && (pi.flags & kAudioUnitParameterFlag_CFNameRelease)) CFRelease(pi.cfNameString);
            p.min = pi.minValue;
            p.max = pi.maxValue;
            p.def = pi.defaultValue;
            p.stepped = pi.unit == kAudioUnitParameterUnit_Indexed || pi.unit == kAudioUnitParameterUnit_Boolean;
            AudioUnitParameterValue v = pi.defaultValue;
            AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &v);
            p.value = v;
            out.push_back(std::move(p));
        }
        return out;
    }

    void setParam(const std::string& id, double value) override {
        AudioUnitSetParameter(unit, static_cast<AudioUnitParameterID>(std::stoul(id)), kAudioUnitScope_Global, 0,
                              static_cast<AudioUnitParameterValue>(value), 0);
    }

    std::string saveState() override {
        CFPropertyListRef plist = nullptr;
        UInt32 size = sizeof plist;
        if (AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, &size) != noErr || !plist) return {};
        CFDataRef data = CFPropertyListCreateData(nullptr, plist, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
        CFRelease(plist);
        if (!data) return {};
        std::string out(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<std::size_t>(CFDataGetLength(data)));
        CFRelease(data);
        return out;
    }

    bool loadState(const std::string& state) override {
        CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(state.data()), static_cast<CFIndex>(state.size()));
        CFPropertyListRef plist = data ? CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr) : nullptr;
        if (data) CFRelease(data);
        if (!plist) return false;
        const bool ok = AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &plist, sizeof plist) == noErr;
        CFRelease(plist);
        AudioUnitParameter changed{unit, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
        AUParameterListenerNotify(nullptr, nullptr, &changed);
        return ok;
    }
};
}  // namespace

std::vector<PluginInfo> scanAudioUnits() {
    std::vector<PluginInfo> out;
    for (OSType type : {kAudioUnitType_Effect, kAudioUnitType_MusicEffect}) {
        AudioComponentDescription d{type, 0, 0, 0, 0};
        AudioComponent c = nullptr;
        while ((c = AudioComponentFindNext(c, &d))) {
            AudioComponentDescription cd{};
            AudioComponentGetDescription(c, &cd);
            CFStringRef name = nullptr;
            AudioComponentCopyName(c, &name);
            std::string full = cfString(name);
            if (name) CFRelease(name);
            PluginInfo info;
            info.format = "au";
            info.id = fourcc(cd.componentType) + ":" + fourcc(cd.componentSubType) + ":" + fourcc(cd.componentManufacturer);
            // Name ist „Hersteller: Plugin“
            const auto colon = full.find(": ");
            info.vendor = colon == std::string::npos ? "" : full.substr(0, colon);
            info.name = colon == std::string::npos ? full : full.substr(colon + 2);
            UInt32 version = 0;
            AudioComponentGetVersion(c, &version);
            info.version = std::to_string(version >> 16) + "." + std::to_string((version >> 8) & 0xFF) + "." + std::to_string(version & 0xFF);
            out.push_back(std::move(info));
        }
    }
    return out;
}

std::unique_ptr<AudioPlugin> loadAudioUnit(const PluginInfo& info, int rate, int maxBlock, std::string* error) {
    if (info.id.size() != 14) {
        if (error) *error = "AU: ungültige Kennung";
        return nullptr;
    }
    AudioComponentDescription d{toOSType(info.id.substr(0, 4)), toOSType(info.id.substr(5, 4)), toOSType(info.id.substr(10, 4)), 0, 0};
    AudioComponent c = AudioComponentFindNext(nullptr, &d);
    auto p = std::make_unique<AuPlugin>();
    if (!c || AudioComponentInstanceNew(c, &p->unit) != noErr) {
        if (error) *error = "AU: Plugin nicht gefunden";
        return nullptr;
    }
    p->maxBlock = maxBlock;
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = sizeof(float);
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    AudioUnitSetProperty(p->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AudioUnitSetProperty(p->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, sizeof fmt);
    UInt32 frames = static_cast<UInt32>(maxBlock);
    AudioUnitSetProperty(p->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof frames);
    AURenderCallbackStruct cb{&AuPlugin::input, p.get()};
    AudioUnitSetProperty(p->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(p->unit) != noErr) {
        if (error) *error = "AU: Plugin lässt sich nicht starten";
        return nullptr;
    }
    p->setInfo(info);
    return p;
}

}  // namespace loggx
