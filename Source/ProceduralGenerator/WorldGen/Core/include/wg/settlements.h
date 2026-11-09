#pragma once
#include <cstdint>
#include <vector>

#include "wg/climate.h"
#include "wg/config.h"
#include "wg/terrain.h"

namespace wg {

// Етап 3. Модульна генерація: розміщення комплексів з готових модулів-будівель.
// Типи комплексів відповідають наявним ассетам (адміністративні будівлі, брами,
// павільйони, святилища); Blueprint у UE використовуються лише як шаблони/будівлі.

enum class SettlementType : uint8_t {
    Capital,         // палацовий комплекс: брама, зали, житлові корпуси, павільйон
    FortressTown,    // адміністративний центр у долині: брами, корпуси, сторожові павільйони
    ShrineGrove,     // святилище в лісі на схилі
    GardenPavilion,  // павільйон-краєвид біля води / на узвишші
    WatchPost,       // сторожовий пост на узбережжі або хребті
    Count
};

// Ролі модулів. Кожна роль відображається на набір варіантів-мешів у UE.
enum class ModuleRole : uint8_t { GateMain, GateSide, HallMain, Residence, Pavilion, Shrine, GuardPavilion, Count };

const char* SettlementTypeNameUa(SettlementType t);
const char* ModuleRoleName(ModuleRole r);

// Скільки варіантів мешів доступно для кожної ролі (за бібліотекою користувача).
struct ModuleCatalog {
    int variants[static_cast<int>(ModuleRole::Count)] = {
        5,  // GateMain: Yeongchumun, Sinmumun, Heungnyemun, Gwanghwamun, Geonchunmun
        2,  // GateSide
        3,  // HallMain
        5,  // Residence
        2,  // Pavilion: Gyeonghoeru, Soswaewon
        2,  // Shrine: Seonwonjeon, Taewonjeon
        2,  // GuardPavilion: Dongsipjagak, Seosipjagak
    };
};

struct ModulePlacement {
    ModuleRole role;
    float x, y, z;     // світові метри (від північно-західного кута), z — висота майданчика
    float yawDeg;      // напрямок фасаду
    float scale;
    uint8_t variant;   // індекс варіанта меша в межах ролі
};

struct Settlement {
    uint32_t id = 0;
    SettlementType type = SettlementType::Capital;
    float x = 0, y = 0, z = 0;  // центр, z = висота майданчика (до вирівнювання)
    float yawDeg = 0;           // напрямок фасаду (вниз по схилу, «обличчям до низу»)
    float radiusM = 0;          // радіус зайнятої ділянки
    float footprintMaxSlope = 0;
    Biome biome = Biome::Ocean;
    float tempC = 0, humidity = 0, coastDistKm = 0;
    float score = 0;
    std::vector<ModulePlacement> modules;
};

struct SettlementParams {
    float densityPer1000Km2[static_cast<int>(SettlementType::Count)] = {0.0f, 1.5f, 2.0f, 3.0f, 2.5f};
    ModuleCatalog catalog;
};

// Порядок і результат детерміновані за seed. Вхід — рельєф і клімат (етапи 1 і 2).
std::vector<Settlement> GenerateSettlements(const WorldConfig& cfg, const TerrainResult& terrain,
                                            const ClimateResult& climate, const SettlementParams& params = {});

}  // namespace wg
