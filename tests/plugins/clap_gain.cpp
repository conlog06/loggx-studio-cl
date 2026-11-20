// Test-Plugin (CLAP): Verstärkung mit einem Parameter, Zustand speicherbar.
#include <atomic>
#include <cmath>
#include <cstring>

#include "clap/clap.h"

namespace {
struct Gain {
    clap_plugin_t plugin;
    const clap_host_t* host;
    std::atomic<double> gain{1.0};
};
const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
const clap_plugin_descriptor_t kDesc = {CLAP_VERSION, "de.loggx.test.gain", "LX Test Gain", "LoggX", "", "", "", "1.0", "Test", kFeatures};

Gain* self(const clap_plugin_t* p) { return static_cast<Gain*>(p->plugin_data); }

const clap_plugin_params_t kParams = {
    [](const clap_plugin_t*) -> uint32_t { return 1; },
    [](const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
        if (i != 0) return false;
        std::memset(info, 0, sizeof *info);
        info->id = 7;
        std::strcpy(info->name, "Gain");
        info->min_value = 0;
        info->max_value = 2;
        info->default_value = 1;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        return true;
    },
    [](const clap_plugin_t* p, clap_id id, double* v) { return id == 7 ? (*v = self(p)->gain.load(), true) : false; },
    [](const clap_plugin_t*, clap_id, double, char*, uint32_t) { return false; },
    [](const clap_plugin_t*, clap_id, const char*, double*) { return false; },
    [](const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
        for (uint32_t i = 0; i < in->size(in); ++i) {
            const auto* h = in->get(in, i);
            if (h->type == CLAP_EVENT_PARAM_VALUE) self(p)->gain = reinterpret_cast<const clap_event_param_value_t*>(h)->value;
        }
    }};
const clap_plugin_state_t kState = {
    [](const clap_plugin_t* p, const clap_ostream_t* s) {
        const double g = self(p)->gain;
        return s->write(s, &g, sizeof g) == sizeof g;
    },
    [](const clap_plugin_t* p, const clap_istream_t* s) {
        double g = 1;
        if (s->read(s, &g, sizeof g) != sizeof g) return false;
        self(p)->gain = g;
        return true;
    }};

const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (std::strcmp(id, kDesc.id) != 0) return nullptr;
    auto* g = new Gain;
    g->host = host;
    g->plugin = {&kDesc, g,
                 [](const clap_plugin_t*) { return true; },
                 [](const clap_plugin_t* p) { delete self(p); },
                 [](const clap_plugin_t*, double, uint32_t, uint32_t) { return true; },
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t*) { return true; },
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t* p, const clap_process_t* pr) -> clap_process_status {
                     kParams.flush(p, pr->in_events, pr->out_events);
                     const float g = static_cast<float>(self(p)->gain.load());
                     for (uint32_t c = 0; c < 2; ++c)
                         for (uint32_t i = 0; i < pr->frames_count; ++i)
                             pr->audio_outputs[0].data32[c][i] = pr->audio_inputs[0].data32[c][i] * g;
                     return CLAP_PROCESS_CONTINUE;
                 },
                 [](const clap_plugin_t*, const char* ext) -> const void* {
                     if (std::strcmp(ext, CLAP_EXT_PARAMS) == 0) return &kParams;
                     if (std::strcmp(ext, CLAP_EXT_STATE) == 0) return &kState;
                     return nullptr;
                 },
                 [](const clap_plugin_t*) {}};
    return &g->plugin;
}

// ---- Test-Instrument: Sinus in Tonhöhe der Taste, solange gehalten
const char* const kSynthFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
const clap_plugin_descriptor_t kSynthDesc = {CLAP_VERSION, "de.loggx.test.synth", "LX Test Synth", "LoggX", "", "", "", "1.0", "Test", kSynthFeatures};
struct Synth {
    clap_plugin_t plugin;
    double rate = 48000, phase = 0, freq = 0, amp = 0;
    int key = -1;
};
Synth* synth(const clap_plugin_t* p) { return static_cast<Synth*>(p->plugin_data); }
const clap_plugin_audio_ports_t kSynthPorts = {
    [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 0 : 1; },
    [](const clap_plugin_t*, uint32_t i, bool input, clap_audio_port_info_t* info) {
        if (input || i != 0) return false;
        std::memset(info, 0, sizeof *info);
        info->id = 0;
        std::strcpy(info->name, "Out");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }};
const clap_plugin_note_ports_t kSynthNotes = {
    [](const clap_plugin_t*, bool input) -> uint32_t { return input ? 1 : 0; },
    [](const clap_plugin_t*, uint32_t i, bool input, clap_note_port_info_t* info) {
        if (!input || i != 0) return false;
        std::memset(info, 0, sizeof *info);
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::strcpy(info->name, "Noten");
        return true;
    }};
const clap_plugin_t* createSynth(const clap_host_t*) {
    auto* s = new Synth;
    s->plugin = {&kSynthDesc, s,
                 [](const clap_plugin_t*) { return true; },
                 [](const clap_plugin_t* p) { delete synth(p); },
                 [](const clap_plugin_t* p, double rate, uint32_t, uint32_t) { return synth(p)->rate = rate, true; },
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t*) { return true; },
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t*) {},
                 [](const clap_plugin_t* p, const clap_process_t* pr) -> clap_process_status {
                     Synth* s = synth(p);
                     uint32_t ev = 0;
                     const uint32_t nev = pr->in_events->size(pr->in_events);
                     for (uint32_t i = 0; i < pr->frames_count; ++i) {
                         for (; ev < nev; ++ev) {
                             const auto* h = pr->in_events->get(pr->in_events, ev);
                             if (h->time > i) break;
                             if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
                                 const auto* n = reinterpret_cast<const clap_event_note_t*>(h);
                                 if (h->type == CLAP_EVENT_NOTE_ON) {
                                     s->key = n->key;
                                     s->freq = 440.0 * std::pow(2.0, (n->key - 69) / 12.0);
                                     s->amp = 0.5 * n->velocity;
                                 } else if (n->key == s->key) {
                                     s->amp = 0;
                                     s->key = -1;
                                 }
                             }
                         }
                         const float v = static_cast<float>(s->amp * std::sin(s->phase));
                         s->phase += 2 * 3.14159265358979 * s->freq / s->rate;
                         pr->audio_outputs[0].data32[0][i] = pr->audio_outputs[0].data32[1][i] = v;
                     }
                     return CLAP_PROCESS_CONTINUE;
                 },
                 [](const clap_plugin_t*, const char* ext) -> const void* {
                     if (std::strcmp(ext, CLAP_EXT_AUDIO_PORTS) == 0) return &kSynthPorts;
                     if (std::strcmp(ext, CLAP_EXT_NOTE_PORTS) == 0) return &kSynthNotes;
                     return nullptr;
                 },
                 [](const clap_plugin_t*) {}};
    return &s->plugin;
}

const clap_plugin_factory_t kFactory = {
    [](const clap_plugin_factory_t*) -> uint32_t { return 2; },
    [](const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &kDesc : i == 1 ? &kSynthDesc : nullptr; },
    [](const clap_plugin_factory_t* f, const clap_host_t* host, const char* id) -> const clap_plugin_t* {
        if (std::strcmp(id, kSynthDesc.id) == 0) return createSynth(host);
        return create(f, host, id);
    }};
}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr; }};
