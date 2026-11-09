#pragma once
#include <cstdint>

#include "wg/noise.h"

namespace wg {

// Параметри етапу 1: шумова генерація рельєфу.
struct TerrainParams {
    NoiseType noise = NoiseType::Simplex;
    double maxHeightM = 1950.0;        // найвища точка (Халласан ~1947 м)
    double targetLandKm2 = 100000.0;   // площа суші (Південна Корея ~100 тис. км2)
    // Форма материка: нахилений еліпс у частках розміру світу.
    double shapeRotationDeg = -12.0;
    double axisEW = 0.36;
    double axisNS = 0.50;
    double warpStrength = 0.10;        // domain warping форми
    double coastNoise = 0.16;          // порізаність берегової лінії
    // Гірські хребти (у координатах повернутого еліпса).
    double ridgeCenter = 0.16;         // головний хребет (аналог Тхебек) зі східного боку
    double ridgeWidth = 0.09;
    double ridge2Strength = 0.80;      // другий хребет (аналог Собек), діагональний
    double heightExponent = 1.30;      // >1: більше низин, вужчі піки
};

// Параметри етапу 2: правило-орієнтований клімат.
struct ClimateParams {
    double tempSouthC = 15.5;          // середньорічна температура на рівні моря, південь
    double tempNorthC = 10.5;          // ... північ
    double lapseCPerKm = 6.5;          // зниження температури з висотою
    double tempNoiseC = 1.0;
    double windFromDeg = 135.0;        // звідки дме вітер (компас: 0=пн, 90=сх, 135=пд-сх)
    double windFromDeg2 = 300.0;       // другий переважний вітер (зимовий мусон, з пн-зх)
    double wind2Weight = 0.40;
    double evapPerKm = 0.03;           // насичення повітря вологою над морем
    double baseLossPerKm = 0.003;      // фонові опади над сушею
    double orographicLossPerKm = 0.4;  // втрата вологи на 1 км підйому
    double altitudeDryness = 0.55;     // висока суха/холодна зона: частка втрати вологості на макс. висоті
    double inlandMoisture = 0.5;       // вологість повітря, що заходить з краю карти
};

struct WorldConfig {
    uint32_t seed = 1;
    double widthKm = 400.0;
    double heightKm = 440.0;
    double cellMeters = 100.0;
    int threads = 0;  // 0 = усі доступні
    TerrainParams terrain;
    ClimateParams climate;

    // Масштабує світ зі збереженням пропорцій і частки суші
    // (для демонстраційного варіанта: scale=0.1 -> площа 1/100).
    void ScaleWorld(double scale) {
        widthKm *= scale;
        heightKm *= scale;
        terrain.targetLandKm2 *= scale * scale;
    }
};

}  // namespace wg
