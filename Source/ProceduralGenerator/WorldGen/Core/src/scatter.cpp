#include "wg/scatter.h"

#include <algorithm>
#include <cmath>

#include "wg/noise.h"

namespace wg {
namespace {

using B = Biome;
using V = VegType;

// Дані таблиці: перша колонка після біому — тип, далі: щільність/га, макс. схил,
// кластеризація, вплив вологості, діапазон масштабу.
const std::vector<ScatterRule> kRules = {
    // Помірний ліс
    {B::TemperateForest, V::BroadleafTree, 220, 0.70f, 0.6f, 0.5f, 0.8f, 1.3f},
    {B::TemperateForest, V::ConiferTree, 60, 0.80f, 0.8f, 0.0f, 0.8f, 1.2f},
    {B::TemperateForest, V::Shrub, 300, 0.80f, 0.5f, 0.3f, 0.6f, 1.2f},
    {B::TemperateForest, V::Grass, 1200, 0.90f, 0.3f, 0.3f, 0.7f, 1.3f},
    {B::TemperateForest, V::Flower, 40, 0.50f, 0.9f, 0.3f, 0.7f, 1.2f},
    {B::TemperateForest, V::Rock, 8, 1.50f, 0.6f, 0.0f, 0.5f, 1.5f},
    // Бореальний ліс
    {B::BorealForest, V::ConiferTree, 280, 0.80f, 0.6f, 0.3f, 0.8f, 1.3f},
    {B::BorealForest, V::BroadleafTree, 40, 0.70f, 0.8f, 0.0f, 0.7f, 1.1f},
    {B::BorealForest, V::Shrub, 150, 0.80f, 0.5f, 0.0f, 0.6f, 1.1f},
    {B::BorealForest, V::Grass, 400, 0.90f, 0.3f, 0.0f, 0.6f, 1.0f},
    {B::BorealForest, V::Rock, 20, 1.50f, 0.6f, 0.0f, 0.5f, 1.6f},
    // Субтропічний ліс
    {B::SubtropicalForest, V::SubtropicalTree, 260, 0.70f, 0.5f, 0.5f, 0.8f, 1.4f},
    {B::SubtropicalForest, V::Shrub, 350, 0.80f, 0.5f, 0.3f, 0.7f, 1.3f},
    {B::SubtropicalForest, V::Grass, 800, 0.90f, 0.3f, 0.3f, 0.7f, 1.3f},
    {B::SubtropicalForest, V::Rock, 6, 1.50f, 0.6f, 0.0f, 0.5f, 1.4f},
    // Помірні луки
    {B::TemperateGrassland, V::Grass, 2500, 0.90f, 0.3f, 0.3f, 0.7f, 1.3f},
    {B::TemperateGrassland, V::Flower, 150, 0.60f, 0.9f, 0.4f, 0.7f, 1.2f},
    {B::TemperateGrassland, V::Shrub, 40, 0.70f, 0.8f, 0.0f, 0.6f, 1.1f},
    {B::TemperateGrassland, V::BroadleafTree, 6, 0.60f, 1.0f, 0.0f, 0.8f, 1.2f},
    {B::TemperateGrassland, V::Rock, 6, 1.50f, 0.6f, 0.0f, 0.5f, 1.5f},
    // Холодний степ
    {B::ColdSteppe, V::Grass, 1200, 0.90f, 0.3f, 0.2f, 0.6f, 1.0f},
    {B::ColdSteppe, V::Shrub, 60, 0.70f, 0.7f, 0.0f, 0.5f, 1.0f},
    {B::ColdSteppe, V::Rock, 25, 1.50f, 0.6f, 0.0f, 0.5f, 1.6f},
    // Сухий чагарник
    {B::DryScrub, V::Shrub, 180, 0.80f, 0.6f, 0.2f, 0.5f, 1.1f},
    {B::DryScrub, V::Grass, 500, 0.90f, 0.5f, 0.2f, 0.5f, 1.0f},
    {B::DryScrub, V::Rock, 30, 1.50f, 0.6f, 0.0f, 0.5f, 1.6f},
    {B::DryScrub, V::BroadleafTree, 4, 0.60f, 1.0f, 0.0f, 0.7f, 1.0f},
    // Савана
    {B::Savanna, V::Grass, 1500, 0.90f, 0.3f, 0.2f, 0.7f, 1.2f},
    {B::Savanna, V::BroadleafTree, 10, 0.60f, 0.9f, 0.0f, 0.9f, 1.4f},
    {B::Savanna, V::Shrub, 60, 0.70f, 0.7f, 0.0f, 0.6f, 1.1f},
    // Пустеля
    {B::Desert, V::Rock, 20, 1.50f, 0.6f, 0.0f, 0.5f, 1.5f},
    {B::Desert, V::Shrub, 5, 0.60f, 0.9f, 0.0f, 0.4f, 0.8f},
    // Альпійська тундра
    {B::AlpineTundra, V::Grass, 300, 0.90f, 0.4f, 0.0f, 0.5f, 0.9f},
    {B::AlpineTundra, V::Rock, 80, 1.50f, 0.6f, 0.0f, 0.5f, 1.6f},
    {B::AlpineTundra, V::Boulder, 6, 1.50f, 0.7f, 0.0f, 0.8f, 1.8f},
    // Скелі
    {B::Rock, V::Rock, 120, 3.00f, 0.5f, 0.0f, 0.6f, 1.8f},
    {B::Rock, V::Boulder, 25, 3.00f, 0.7f, 0.0f, 0.8f, 2.0f},
    // Болото
    {B::Wetland, V::Reed, 1500, 0.20f, 0.5f, 0.0f, 0.8f, 1.3f},
    {B::Wetland, V::Grass, 600, 0.30f, 0.4f, 0.0f, 0.7f, 1.1f},
    {B::Wetland, V::Shrub, 50, 0.30f, 0.7f, 0.0f, 0.6f, 1.0f},
    {B::Wetland, V::BroadleafTree, 10, 0.30f, 0.9f, 0.0f, 0.8f, 1.1f},
    // Пляж
    {B::Beach, V::Rock, 10, 0.50f, 0.6f, 0.0f, 0.4f, 1.0f},
    {B::Beach, V::Grass, 50, 0.30f, 0.8f, 0.0f, 0.5f, 0.8f},
};

inline double Hash01(int32_t x, int32_t y, uint32_t salt) {
    return static_cast<double>(Hash2(x, y, salt)) / 4294967296.0;
}

// Білінійне читання поля (у метрах від кута світу).
float Sample(const Grid<float>& g, double xM, double yM, double cellM) {
    const double fx = std::clamp(xM / cellM - 0.5, 0.0, g.w - 1.0), fy = std::clamp(yM / cellM - 0.5, 0.0, g.h - 1.0);
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(g.w - 1, x0 + 1), y1 = std::min(g.h - 1, y0 + 1);
    const double tx = fx - x0, ty = fy - y0;
    const double a = g.at(x0, y0) * (1 - tx) + g.at(x1, y0) * tx;
    const double b = g.at(x0, y1) * (1 - tx) + g.at(x1, y1) * tx;
    return static_cast<float>(a * (1 - ty) + b * ty);
}

}  // namespace

const char* VegTypeNameUa(VegType t) {
    static const char* n[] = {"Листяне дерево", "Хвойне дерево", "Субтропічне дерево", "Кущ", "Трава",
                              "Квіти",          "Камінь",         "Валун",              "Очерет"};
    return n[static_cast<int>(t)];
}

const std::vector<ScatterRule>& DefaultScatterRules() { return kRules; }

std::vector<Instance> ScatterCell(const ScatterContext& ctx, int cellX, int cellY, double cs) {
    std::vector<Instance> out;
    const WorldConfig& cfg = ctx.cfg;
    const double cell = cfg.cellMeters;
    const double worldW = ctx.terrain.height.w * cell, worldH = ctx.terrain.height.h * cell;
    const double x0 = cellX * cs, y0 = cellY * cs, x1 = x0 + cs, y1 = y0 + cs;
    if (x0 >= worldW || y0 >= worldH) return out;
    const NoiseType nt = cfg.terrain.noise;

    // Комплекси, що перетинають комірку: наповнення в їхніх межах прибирається.
    std::vector<const Settlement*> near;
    if (ctx.settlements)
        for (const Settlement& st : *ctx.settlements)
            if (st.x + st.radiusM >= x0 && st.x - st.radiusM < x1 && st.y + st.radiusM >= y0 && st.y - st.radiusM < y1)
                near.push_back(&st);

    for (size_t ri = 0; ri < ctx.rules.size(); ++ri) {
        const ScatterRule& r = ctx.rules[ri];
        const double peakPerM2 = r.densityPerHa * 1.8 / 10000.0;  // верхня межа після всіх множників
        const double s = 1.0 / std::sqrt(peakPerM2);              // крок решітки
        const uint32_t salt = cfg.seed * 2654435761u + static_cast<uint32_t>(ri) * 40503u + 0x5CA7u;
        const int i0 = static_cast<int>(std::floor(x0 / s)) - 1, i1 = static_cast<int>(std::ceil(x1 / s)) + 1;
        const int j0 = static_cast<int>(std::floor(y0 / s)) - 1, j1 = static_cast<int>(std::ceil(y1 / s)) + 1;
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                // Решітка з джитером: кожен вузол належить комірці за своєю позицією.
                const double px = (i + 0.5 + (Hash01(i, j, salt) - 0.5) * 0.8) * s;
                const double py = (j + 0.5 + (Hash01(i, j, salt + 1) - 0.5) * 0.8) * s;
                if (px < x0 || px >= x1 || py < y0 || py >= y1) continue;
                if (px < 0 || py < 0 || px >= worldW || py >= worldH) continue;

                // Біом у точці з розмиттям меж: невеликий шумовий зсув позиції вибірки.
                const double wx = px + 120.0 * Fbm(nt, px / 400.0, py / 400.0, cfg.seed ^ 0x71u, 2);
                const double wy = py + 120.0 * Fbm(nt, px / 400.0 + 5.0, py / 400.0, cfg.seed ^ 0x72u, 2);
                const int bx = std::clamp(static_cast<int>(wx / cell), 0, ctx.terrain.height.w - 1);
                const int by = std::clamp(static_cast<int>(wy / cell), 0, ctx.terrain.height.h - 1);
                if (static_cast<Biome>(ctx.climate.biome.at(bx, by)) != r.biome) continue;

                bool cleared = false;
                for (const Settlement* st : near)
                    if (std::hypot(px - st->x, py - st->y) < st->radiusM * 0.92) { cleared = true; break; }
                // всередині комплексу лишається лише трохи трави
                if (cleared && (r.type != VegType::Grass || Hash01(i, j, salt + 5) > 0.25)) continue;

                const float slope = ctx.terrain.slope.at(bx, by);
                if (slope > r.maxSlope) continue;

                // Локальна щільність: кластеризація шумом + вплив вологості.
                const double cluster = 0.5 + 0.5 * Fbm(nt, px / 60.0, py / 60.0, salt ^ 0x33u, 3);
                const double clusterF = 1.0 - r.clusterStrength + r.clusterStrength * 2.2 * cluster;
                const double hum = ctx.climate.humidity.at(bx, by);
                const double humF = 1.0 + r.humidityGain * (hum - 0.5) * 2.0;
                const double local = std::clamp(clusterF * humF, 0.0, 1.8);
                if (Hash01(i, j, salt + 2) >= local / 1.8) continue;

                Instance inst;
                inst.type = r.type;
                inst.x = static_cast<float>(px);
                inst.y = static_cast<float>(py);
                inst.z = Sample(ctx.terrain.height, px, py, cell);
                inst.yawDeg = static_cast<float>(Hash01(i, j, salt + 3) * 360.0);
                inst.scale = static_cast<float>(r.scaleMin + (r.scaleMax - r.scaleMin) * Hash01(i, j, salt + 4));
                out.push_back(inst);
            }
        }
    }
    return out;
}

double ExpectedInstanceCount(const ScatterContext& ctx) {
    // Середня щільність ~ densityPerHa (множники кластеризації мають середнє ~1).
    std::vector<double> perHa(static_cast<size_t>(Biome::Count), 0.0);
    for (const ScatterRule& r : ctx.rules) perHa[static_cast<size_t>(r.biome)] += r.densityPerHa;
    const double haPerCell = ctx.cfg.cellMeters * ctx.cfg.cellMeters / 10000.0;
    double total = 0.0;
    for (uint8_t b : ctx.climate.biome.data) total += perHa[b] * haPerCell;
    return total;
}

}  // namespace wg
