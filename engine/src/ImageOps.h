// Bildoperationen auf RGBA-Ebenen (8 Bit, nicht vormultipliziert).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "loggx/core/Project.h"

namespace loggx {

// 3D-LUT im .cube-Format (Adobe/Resolve).
struct Lut3D {
    int size = 0;
    float domainMin[3] = {0, 0, 0};
    float domainMax[3] = {1, 1, 1};
    std::vector<float> data;   // size^3 * 3, Rot läuft am schnellsten

    static std::shared_ptr<Lut3D> load(const std::string& path, std::string* error = nullptr);
    void apply(float& r, float& g, float& b) const;
};

void applyColorGrade(std::vector<std::uint8_t>& rgba, int w, int h, const ColorGrade& grade,
                     const Lut3D* lut);
void applyChromaKey(std::vector<std::uint8_t>& rgba, int w, int h, const Effects& fx);
void applyVignette(std::vector<std::uint8_t>& rgba, int w, int h, double amount);
// Weichzeichnen mit Radius in Pixeln (drei Box-Filter ≈ Gauß).
void applyBlur(std::vector<std::uint8_t>& rgba, int w, int h, double radius);

}  // namespace loggx
