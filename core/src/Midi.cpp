#include "loggx/core/Midi.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>

namespace loggx {

namespace {

constexpr int kPpq = 480;

std::filesystem::path pathOf(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

void putVarLen(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7) > 0) buf[n++] = 0x80 | (v & 0x7F);
    while (n > 0) out.push_back(buf[--n]);
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}

void putMeta(std::vector<std::uint8_t>& out, std::uint8_t type, const std::string& data) {
    out.push_back(0x00);
    out.push_back(0xFF);
    out.push_back(type);
    putVarLen(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

int programFor(InstrumentType t) {
    switch (t) {
        case InstrumentType::Piano: return 0;
        case InstrumentType::ElectricPiano: return 4;
        case InstrumentType::Synth: return 81;
        case InstrumentType::Bass: return 33;
        case InstrumentType::Pad: return 89;
        case InstrumentType::Strings: return 48;
        case InstrumentType::Drums: return 0;
    }
    return 0;
}

InstrumentType instrumentFor(int program, int channel) {
    if (channel == 9) return InstrumentType::Drums;
    if (program >= 4 && program <= 7) return InstrumentType::ElectricPiano;
    if (program < 8) return InstrumentType::Piano;
    if (program >= 32 && program <= 39) return InstrumentType::Bass;
    if (program >= 40 && program <= 51) return InstrumentType::Strings;
    if (program >= 80 && program <= 87) return InstrumentType::Synth;
    if (program >= 88 && program <= 95) return InstrumentType::Pad;
    return InstrumentType::Piano;
}

}  // namespace

// ------------------------------------------------------------- Export

bool exportMidi(const Project& project, const std::string& path, std::string* error) {
    const double usPerTick = 60.0 / project.bpm * kSecond / kPpq;
    auto ticks = [&](TimeUs t) { return static_cast<std::uint32_t>(std::max(0.0, std::round(t / usPerTick))); };

    std::vector<std::vector<std::uint8_t>> chunks;
    // Spur 0: Tempo und Takt
    {
        std::vector<std::uint8_t> t;
        putMeta(t, 0x03, project.name);
        const std::uint32_t mpqn = static_cast<std::uint32_t>(std::lround(60'000'000.0 / project.bpm));
        putMeta(t, 0x51, std::string{static_cast<char>(mpqn >> 16), static_cast<char>(mpqn >> 8),
                                     static_cast<char>(mpqn)});
        putMeta(t, 0x58, std::string{static_cast<char>(project.beatsPerBar), 2, 24, 8});
        putMeta(t, 0x2F, {});
        chunks.push_back(std::move(t));
    }

    int channel = 0;
    for (const Track& track : project.tracks) {
        if (track.kind != TrackKind::Instrument) continue;
        const bool drums = track.instrument == InstrumentType::Drums;
        const int ch = drums ? 9 : channel;
        if (!drums) channel = (channel + 1) % 16 == 9 ? channel + 2 : (channel + 1) % 16;

        struct Ev {
            std::uint32_t tick;
            bool on;
            int pitch, vel;
        };
        std::vector<Ev> events;
        for (const Clip& c : track.clips) {
            if (c.kind != ClipKind::Midi) continue;
            for (const Note& n : c.notes) {
                if (n.start < c.in || n.start >= c.in + c.duration) continue;
                const TimeUs on = c.start + (n.start - c.in);
                const TimeUs off = std::min(on + n.length, c.end());
                const int pitch = std::clamp(n.pitch + static_cast<int>(std::lround(c.pitch)), 0, 127);
                const int vel = std::clamp(static_cast<int>(std::lround(n.velocity * 127)), 1, 127);
                events.push_back({ticks(on), true, pitch, vel});
                events.push_back({std::max(ticks(on) + 1, ticks(off)), false, pitch, 0});
            }
        }
        std::sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return !a.on && b.on;   // Note-aus vor Note-an
        });
        std::vector<std::uint8_t> t;
        putMeta(t, 0x03, track.name);
        t.push_back(0x00);
        t.push_back(static_cast<std::uint8_t>(0xC0 | ch));
        t.push_back(static_cast<std::uint8_t>(programFor(track.instrument)));
        std::uint32_t last = 0;
        for (const Ev& e : events) {
            putVarLen(t, e.tick - last);
            last = e.tick;
            t.push_back(static_cast<std::uint8_t>((e.on ? 0x90 : 0x80) | ch));
            t.push_back(static_cast<std::uint8_t>(e.pitch));
            t.push_back(static_cast<std::uint8_t>(e.on ? e.vel : 64));
        }
        putMeta(t, 0x2F, {});
        chunks.push_back(std::move(t));
    }

    std::vector<std::uint8_t> file = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    file.push_back(static_cast<std::uint8_t>(chunks.size() >> 8));
    file.push_back(static_cast<std::uint8_t>(chunks.size()));
    file.push_back(kPpq >> 8);
    file.push_back(kPpq & 0xFF);
    for (const auto& c : chunks) {
        file.insert(file.end(), {'M', 'T', 'r', 'k'});
        put32(file, static_cast<std::uint32_t>(c.size()));
        file.insert(file.end(), c.begin(), c.end());
    }
    std::ofstream f(pathOf(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "Datei kann nicht geschrieben werden";
        return false;
    }
    f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return f.good();
}

// ------------------------------------------------------------- Import

namespace {

struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;
    std::uint8_t u8() {
        if (p >= end) throw std::runtime_error("Datei ist unvollständig");
        return *p++;
    }
    std::uint32_t u16() { return (u8() << 8) | u8(); }
    std::uint32_t u32() { return (u16() << 16) | u16(); }
    std::uint32_t varLen() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        throw std::runtime_error("Ungültige Längenangabe");
    }
};

struct RawNote {
    std::uint64_t on, off;
    int pitch;
    double velocity;
};

struct RawTrack {
    std::string name;
    int program = 0;
    std::map<int, std::vector<RawNote>> byChannel;
};

}  // namespace

bool importMidi(const std::string& path, Project& project, TimeUs at, MidiImportResult* result,
                std::string* error) {
    try {
        std::ifstream f(pathOf(path), std::ios::binary);
        if (!f) throw std::runtime_error("Datei kann nicht geöffnet werden");
        const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Reader r{data.data(), data.data() + data.size()};
        if (data.size() < 14 || std::string(data.begin(), data.begin() + 4) != "MThd")
            throw std::runtime_error("Keine MIDI-Datei");
        r.p += 4;
        const std::uint32_t headerLen = r.u32();
        const std::uint32_t format = r.u16();
        const std::uint32_t trackCount = r.u16();
        const std::uint32_t division = r.u16();
        r.p += headerLen - 6;
        if (division & 0x8000) throw std::runtime_error("SMPTE-Zeitbasis wird nicht unterstützt");
        if (format > 1) throw std::runtime_error("MIDI-Typ 2 wird nicht unterstützt");
        const double ppq = division;

        std::map<std::uint64_t, std::uint32_t> tempos;   // Tick -> µs pro Viertel
        std::vector<RawTrack> tracks;
        for (std::uint32_t ti = 0; ti < trackCount && r.p + 8 <= r.end; ++ti) {
            const std::string id(r.p, r.p + 4);
            r.p += 4;
            const std::uint32_t len = r.u32();
            if (r.p + len > r.end) throw std::runtime_error("Spur ist unvollständig");
            if (id != "MTrk") {
                r.p += len;
                continue;
            }
            Reader t{r.p, r.p + len};
            r.p += len;
            RawTrack track;
            std::map<std::pair<int, int>, std::vector<std::pair<std::uint64_t, double>>> open;
            std::uint64_t tick = 0;
            std::uint8_t status = 0;
            while (t.p < t.end) {
                tick += t.varLen();
                std::uint8_t b = t.u8();
                if (b == 0xFF) {
                    const std::uint8_t type = t.u8();
                    const std::uint32_t l = t.varLen();
                    if (t.p + l > t.end) throw std::runtime_error("Meta-Ereignis unvollständig");
                    if (type == 0x51 && l == 3) tempos[tick] = (t.p[0] << 16) | (t.p[1] << 8) | t.p[2];
                    if (type == 0x03) track.name.assign(t.p, t.p + l);
                    t.p += l;
                    if (type == 0x2F) break;
                    continue;
                }
                if (b == 0xF0 || b == 0xF7) {
                    t.p += t.varLen();
                    continue;
                }
                std::uint8_t d1;
                if (b & 0x80) {
                    status = b;
                    d1 = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? t.u8() : t.u8();
                } else {
                    if (!status) throw std::runtime_error("Ungültiger Laufstatus");
                    d1 = b;
                }
                const int kind = status & 0xF0, ch = status & 0x0F;
                if (kind == 0xC0) {
                    track.program = d1;
                    continue;
                }
                if (kind == 0xD0) continue;
                const std::uint8_t d2 = t.u8();
                if (kind == 0x90 && d2 > 0) {
                    open[{ch, d1}].push_back({tick, d2 / 127.0});
                } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                    auto& stack = open[{ch, d1}];
                    if (!stack.empty()) {
                        const auto [on, vel] = stack.front();
                        stack.erase(stack.begin());
                        track.byChannel[ch].push_back({on, std::max(tick, on + 1), d1, vel});
                    }
                }
            }
            tracks.push_back(std::move(track));
        }

        // Tempokarte: Ticks -> Mikrosekunden
        if (tempos.empty() || tempos.begin()->first != 0) tempos[0] = tempos.empty() ? 500000 : tempos.begin()->second;
        auto toUs = [&](std::uint64_t tick) {
            double us = 0;
            auto it = tempos.begin();
            while (true) {
                auto next = std::next(it);
                const std::uint64_t until = next == tempos.end() ? tick : std::min(tick, next->first);
                us += static_cast<double>(until - it->first) * it->second / ppq;
                if (next == tempos.end() || next->first >= tick) break;
                it = next;
            }
            return static_cast<TimeUs>(us);
        };

        MidiImportResult res;
        res.fileBpm = 60'000'000.0 / tempos.begin()->second;
        const TimeUs bar = project.beatDuration() * project.beatsPerBar;
        int unnamed = 1;
        for (const RawTrack& rt : tracks) {
            for (const auto& [ch, notes] : rt.byChannel) {
                if (notes.empty()) continue;
                std::string name = rt.name.empty() ? "MIDI " + std::to_string(unnamed++) : rt.name;
                if (rt.byChannel.size() > 1) name += " (Kanal " + std::to_string(ch + 1) + ")";
                const std::string trackId = project.addTrack(TrackKind::Instrument, name);
                project.findTrack(trackId)->instrument = instrumentFor(rt.program, ch);
                TimeUs endUs = 0;
                std::vector<Note> converted;
                for (const RawNote& n : notes) {
                    const TimeUs on = toUs(n.on), off = toUs(n.off);
                    converted.push_back({n.pitch, on, std::max<TimeUs>(10'000, off - on), n.velocity});
                    endUs = std::max(endUs, off);
                }
                const TimeUs length = bar > 0 ? (endUs + bar - 1) / bar * bar : endUs;
                const std::string clipId = project.addMidiClip(trackId, at, std::max(length, bar));
                if (Clip* c = project.findClip(clipId)) c->notes = std::move(converted);
                res.tracks++;
                res.notes += static_cast<int>(notes.size());
            }
        }
        if (res.tracks == 0) throw std::runtime_error("Die Datei enthält keine Noten");
        if (result) *result = res;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

}  // namespace loggx
