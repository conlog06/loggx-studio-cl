// Standard-MIDI-Dateien (.mid) lesen und schreiben.
#pragma once

#include <string>

#include "loggx/core/Project.h"

namespace loggx {

// Schreibt alle Instrumentspuren als Typ-1-Datei (eine MIDI-Spur pro
// Instrumentspur, Tempo des Projekts). Schlagzeug liegt auf Kanal 10.
bool exportMidi(const Project& project, const std::string& path, std::string* error = nullptr);

struct MidiImportResult {
    int tracks = 0;          // angelegte Instrumentspuren
    int notes = 0;
    double fileBpm = 0.0;    // erstes Tempo der Datei (0 = keins angegeben)
};

// Legt für jede MIDI-Spur mit Noten eine Instrumentspur mit einem MIDI-Clip
// an, beginnend bei `at`. Tempowechsel werden in echte Zeit umgerechnet.
bool importMidi(const std::string& path, Project& project, TimeUs at, MidiImportResult* result = nullptr,
                std::string* error = nullptr);

}  // namespace loggx
