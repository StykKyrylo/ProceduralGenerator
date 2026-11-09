#pragma once
#include <cstdint>

#include "wg/config.h"
#include "wg/grid.h"
#include "wg/terrain.h"

namespace wg {

enum class Biome : uint8_t {
    Ocean,
    Beach,
    Rock,
    AlpineTundra,
    BorealForest,
    ColdSteppe,
    Wetland,
    TemperateForest,
    TemperateGrassland,
    DryScrub,
    SubtropicalForest,
    Savanna,
    Desert,
    Count
};

const char* BiomeNameUa(Biome b);
void BiomeColor(Biome b, uint8_t& r, uint8_t& g, uint8_t& bl);

struct ClimateResult {
    Grid<float> temperature;  // середньорічна, град. C
    Grid<float> humidity;     // індекс вологості [0, 1]
    Grid<uint8_t> biome;      // значення enum Biome
};

// Етап 2. Правило-орієнтований клімат на основі карти висот:
//  температура = широтний градієнт - gradient*висота + шум;
//  вологість  = перенесення вологи вітром з орографічними опадами (дощова тінь)
//               і висотним осушенням;
//  біом       = перший збіг у таблиці правил за (T, H, висота, схил).
ClimateResult GenerateClimate(const WorldConfig& cfg, const TerrainResult& terrain);

// Класифікація біому за таблицею правил (публічна для тестів та етапу 3).
Biome ClassifyBiome(float heightM, float slope, float tempC, float humidity, bool coastal);

}  // namespace wg
