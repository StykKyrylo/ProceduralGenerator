#pragma once
#include <cstdint>
#include <vector>

#include "wg/climate.h"
#include "wg/config.h"
#include "wg/settlements.h"
#include "wg/terrain.h"

namespace wg {

// Абстрактні категорії наповнення. У UE кожна відображається на набір реальних мешів
// (Data Asset зі списком варіантів), тут лише тип.
enum class VegType : uint8_t {
    BroadleafTree, ConiferTree, SubtropicalTree, Shrub, Grass, Flower, Rock, Boulder, Reed, Count
};

const char* VegTypeNameUa(VegType t);

struct Instance {
    VegType type;
    float x, y, z;   // метри від північно-західного кута світу; z — висота
    float yawDeg;
    float scale;
};

// Правило наповнення: який шар, у якому біомі, з якою щільністю та обмеженнями.
struct ScatterRule {
    Biome biome;
    VegType type;
    float densityPerHa;     // середня щільність (екземплярів на гектар)
    float maxSlope;         // tan; на крутіших схилах шар не з'являється
    float clusterStrength;  // 0 = рівномірно, 1 = сильно згустками (шумова маска)
    float humidityGain;     // як сильно вологість збільшує щільність (0 = ні)
    float scaleMin, scaleMax;
};

const std::vector<ScatterRule>& DefaultScatterRules();

// Дані, потрібні для розстановки (посилання, без копій).
struct ScatterContext {
    const WorldConfig& cfg;
    const TerrainResult& terrain;
    const ClimateResult& climate;
    const std::vector<ScatterRule>& rules;
    // Необов'язково: комплекси, з-під яких прибирається наповнення (інтеграція етапу 3).
    const std::vector<Settlement>* settlements = nullptr;
};

// Детермінована розстановка в квадраті [cellX*cs, (cellX+1)*cs) x [cellY*cs, ...) метрів.
// Результат залежить лише від (seed, координат), а не від порядку чи сусідніх комірок,
// тому її можна викликати при завантаженні комірки World Partition і отримувати
// однаковий світ. Екземпляри не перетинають межі комірок.
std::vector<Instance> ScatterCell(const ScatterContext& ctx, int cellX, int cellY, double cellSizeM);

// Теоретична кількість екземплярів у світі, якщо розставити все (для оцінки навантаження).
double ExpectedInstanceCount(const ScatterContext& ctx);

}  // namespace wg
