#pragma once
#include "wg/config.h"
#include "wg/grid.h"

namespace wg {

struct TerrainResult {
    Grid<float> height;  // метри над рівнем моря; суша > 0, океан < 0
    Grid<float> slope;   // tan(кута нахилу), безрозмірна
    double seaThreshold = 0.0;  // відкалібрований поріг поля континенту
    double landKm2 = 0.0;
    double maxHeightM = 0.0;
};

// Етап 1. Шумова генерація рельєфу.
// Площа суші калібрується квантилем поля континенту, тож для будь-якого
// розміру світу частка суші відповідає TerrainParams::targetLandKm2.
TerrainResult GenerateTerrain(const WorldConfig& cfg);

}  // namespace wg
