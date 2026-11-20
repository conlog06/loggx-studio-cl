// Test-Plugin (CLAP): Verstärkung mit einem Parameter, Zustand speicherbar.
#include <atomic>
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

const clap_plugin_factory_t kFactory = {[](const clap_plugin_factory_t*) -> uint32_t { return 1; },
                                        [](const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &kDesc : nullptr; }, create};
}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr; }};
