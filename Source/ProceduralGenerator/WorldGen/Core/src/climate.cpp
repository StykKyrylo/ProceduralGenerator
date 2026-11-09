#include "wg/climate.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "wg/parallel.h"

namespace wg {
namespace {

constexpr double kDegToRad = 0.017453292519943295;
constexpr float kInf = std::numeric_limits<float>::infinity();

// Рядок таблиці правил. Перше правило, якому відповідає клітинка, перемагає.
struct BiomeRule {
    Biome biome;
    float tMin, tMax;          // температура, C
    float hMin, hMax;          // індекс вологості
    float altMin, altMax;      // висота, м
    float slopeMin, slopeMax;  // tan кута нахилу
};

// Порядок має значення. Пороги підібрані під помірний клімат масштабу Кореї.
constexpr BiomeRule kRules[] = {
    {Biome::Rock,               -kInf, kInf,  0.0f, 1.0f, -kInf, kInf, 0.70f, kInf},   // крутий схил
    {Biome::Rock,               -kInf, -1.5f, 0.0f, 1.0f, -kInf, kInf, 0.0f,  kInf},   // вічна мерзлота/скелі
    {Biome::AlpineTundra,       -kInf, 3.0f,  0.0f, 1.0f, -kInf, kInf, 0.0f,  kInf},
    {Biome::Wetland,            3.0f,  kInf,  0.75f, 1.0f, -kInf, 40.0f, 0.0f, 0.02f},
    {Biome::BorealForest,       3.0f,  8.0f,  0.25f, 1.0f, -kInf, kInf, 0.0f,  kInf},
    {Biome::ColdSteppe,         3.0f,  8.0f,  0.0f,  0.25f, -kInf, kInf, 0.0f, kInf},
    {Biome::TemperateForest,    8.0f,  14.5f, 0.35f, 1.0f, -kInf, kInf, 0.0f,  kInf},
    {Biome::TemperateGrassland, 8.0f,  14.5f, 0.20f, 0.35f, -kInf, kInf, 0.0f, kInf},
    {Biome::DryScrub,           8.0f,  14.5f, 0.0f,  0.20f, -kInf, kInf, 0.0f, kInf},
    {Biome::SubtropicalForest,  14.5f, kInf,  0.40f, 1.0f, -kInf, kInf, 0.0f,  kInf},
    {Biome::Savanna,            14.5f, kInf,  0.20f, 0.40f, -kInf, kInf, 0.0f, kInf},
    {Biome::Desert,             14.5f, kInf,  0.0f,  0.20f, -kInf, kInf, 0.0f, kInf},
};

inline bool InRange(float v, float lo, float hi) { return v >= lo && v < hi; }

}  // namespace

const char* BiomeNameUa(Biome b) {
    static const char* names[] = {"Океан",          "Пляж",           "Скелі/гольці",     "Альпійська тундра",
                                  "Бореальний ліс", "Холодний степ",  "Болото",           "Помірний ліс",
                                  "Помірні луки",   "Сухий чагарник", "Субтропічний ліс", "Савана",
                                  "Пустеля"};
    return names[static_cast<int>(b)];
}

void BiomeColor(Biome b, uint8_t& r, uint8_t& g, uint8_t& bl) {
    static const uint8_t c[][3] = {{28, 64, 120},   {236, 220, 160}, {128, 124, 120}, {190, 205, 190},
                                   {40, 100, 80},   {170, 175, 120}, {90, 130, 130},  {40, 140, 50},
                                   {150, 190, 70},  {190, 165, 100}, {20, 110, 60},   {200, 170, 70},
                                   {225, 200, 130}};
    const auto& v = c[static_cast<int>(b)];
    r = v[0];
    g = v[1];
    bl = v[2];
}

Biome ClassifyBiome(float heightM, float slope, float tempC, float humidity, bool coastal) {
    if (heightM <= 0.0f) return Biome::Ocean;
    if (coastal && heightM < 6.0f) return Biome::Beach;
    for (const BiomeRule& r : kRules) {
        if (InRange(tempC, r.tMin, r.tMax) && humidity >= r.hMin && humidity <= r.hMax &&
            heightM >= r.altMin && heightM < r.altMax && slope >= r.slopeMin && slope < r.slopeMax)
            return r.biome;
    }
    return Biome::TemperateForest;
}

ClimateResult GenerateClimate(const WorldConfig& cfg, const TerrainResult& terrain) {
    const ClimateParams& cp = cfg.climate;
    const int W = terrain.height.w, H = terrain.height.h;
    const double cellKm = cfg.cellMeters / 1000.0;
    ClimateResult res;
    res.temperature = Grid<float>(W, H);
    res.humidity = Grid<float>(W, H);
    res.biome = Grid<uint8_t>(W, H);

    // --- Температура ---
    WgParallelFor(H, cfg.threads, [&](int y) {
        const double v = (y + 0.5) / H;  // 0 = північ
        const double sea = cp.tempNorthC + (cp.tempSouthC - cp.tempNorthC) * v;
        for (int x = 0; x < W; ++x) {
            const double xKm = (x + 0.5) * cellKm, yKm = (y + 0.5) * cellKm;
            const double alt = std::max(0.0f, terrain.height.at(x, y));
            const double noise = Fbm(cfg.terrain.noise, xKm / 40.0, yKm / 40.0, cfg.seed ^ 0x51u, 3);
            res.temperature.at(x, y) =
                static_cast<float>(sea - cp.lapseCPerKm * alt / 1000.0 + cp.tempNoiseC * noise);
        }
    });

    // --- Вологість: перенесення вологи вздовж вітру (схема проти потоку) ---
    // Рахується на грубій сітці (~1 км): орографія має залежати від великих форм
    // рельєфу, а не від дрібного шуму, і результат не повинен залежати від cellMeters.
    // Вітер дме у напрямку (wx, wy) у координатах сітки (x — схід, y — південь).
    const int f = std::max(1, static_cast<int>(std::lround(1000.0 / cfg.cellMeters)));
    const int cw = (W + f - 1) / f, ch = (H + f - 1) / f;
    const double cCellKm = cellKm * f;
    Grid<float> hc(cw, ch, 0.0f), oceanFrac(cw, ch, 0.0f);
    WgParallelFor(ch, cfg.threads, [&](int cy) {
        for (int cx = 0; cx < cw; ++cx) {
            double sumH = 0.0;
            int ocean = 0, n = 0;
            for (int y = cy * f; y < std::min(H, (cy + 1) * f); ++y)
                for (int x = cx * f; x < std::min(W, (cx + 1) * f); ++x) {
                    const float h = terrain.height.at(x, y);
                    sumH += std::max(0.0f, h);
                    ocean += h <= 0.0f;
                    ++n;
                }
            hc.at(cx, cy) = static_cast<float>(sumH / n);
            oceanFrac.at(cx, cy) = static_cast<float>(static_cast<double>(ocean) / n);
        }
    });

    auto sweep = [&](double windFromDeg) -> Grid<float> {
    const double th = windFromDeg * kDegToRad;
    const double wx = -std::sin(th), wy = std::cos(th);
    const int sx = wx >= 0 ? 1 : -1, sy = wy >= 0 ? 1 : -1;
    const double ax = std::fabs(wx), ay = std::fabs(wy);
    const double wa = ax / (ax + ay), wb = ay / (ax + ay);  // ваги сусідів проти потоку
    const double stepKm = cCellKm * (ax + ay);              // шлях за один крок
    const double stepM = stepKm * 1000.0;
    const double evap = 1.0 - std::exp(-cp.evapPerKm * stepKm);
    const double baseLoss = std::exp(-cp.baseLossPerKm * stepKm);

    Grid<float> moisture(cw, ch, 0.0f), flat(cw, ch, 0.0f), humidC(cw, ch, 1.0f);
    const int xBeg = sx > 0 ? 0 : cw - 1, xEnd = sx > 0 ? cw : -1;
    const int yBeg = sy > 0 ? 0 : ch - 1, yEnd = sy > 0 ? ch : -1;
    for (int y = yBeg; y != yEnd; y += sy) {
        for (int x = xBeg; x != xEnd; x += sx) {
            const int px = x - sx, py = y - sy;
            double mUp = 0.0, fUp = 0.0, hUp = 0.0;
            const double hHere = hc.at(x, y);
            if (wa > 0.0) {
                if (hc.inside(px, y)) { mUp += wa * moisture.at(px, y); fUp += wa * flat.at(px, y); hUp += wa * hc.at(px, y); }
                else { mUp += wa * cp.inlandMoisture; fUp += wa * cp.inlandMoisture; hUp += wa * hHere; }
            }
            if (wb > 0.0) {
                if (hc.inside(x, py)) { mUp += wb * moisture.at(x, py); fUp += wb * flat.at(x, py); hUp += wb * hc.at(x, py); }
                else { mUp += wb * cp.inlandMoisture; fUp += wb * cp.inlandMoisture; hUp += wb * hHere; }
            }
            const double oc = oceanFrac.at(x, y), lf = 1.0 - oc;
            // ділянка суші: орографічні опади і дощова тінь
            const double dh = std::max(0.0, hHere - hUp);
            const double upslope = dh / stepM;
            const double boost = 1.0 - std::exp(-upslope * 6.0);
            // 40% — континентальність (вологість без орографії), 60% — орографічна складова
            const double humidLand = 0.40 * fUp + 0.60 * mUp * (0.45 + 0.55 * boost);
            const double mLand = mUp * std::pow(baseLoss, lf) * std::exp(-cp.orographicLossPerKm * dh / 1000.0 * lf);
            // ділянка океану: насичення вологою
            const double mOcean = mUp + (1.0 - mUp) * evap;
            moisture.at(x, y) = static_cast<float>(oc * mOcean + lf * mLand);
            const double fOcean = fUp + (1.0 - fUp) * evap;
            flat.at(x, y) = static_cast<float>(oc * fOcean + lf * fUp * std::pow(baseLoss, lf));
            humidC.at(x, y) = static_cast<float>(oc >= 0.999 ? 1.0 : std::clamp(humidLand, 0.0, 1.0));
        }
    }
    return humidC;
    };
    const Grid<float> hA = sweep(cp.windFromDeg);
    const Grid<float> hB = sweep(cp.windFromDeg2);
    Grid<float> humidC(cw, ch, 1.0f);
    for (size_t i = 0; i < humidC.size(); ++i)
        humidC.data[i] = static_cast<float>((1.0 - cp.wind2Weight) * hA.data[i] + cp.wind2Weight * hB.data[i]);

    // Згладжування: прибирає діагональні артефакти схеми проти потоку (box blur, ~8 км).
    {
        const int r = std::max(1, static_cast<int>(std::lround(8.0 / cCellKm)));
        Grid<float> tmp(cw, ch);
        for (int pass = 0; pass < 2; ++pass) {
            WgParallelFor(ch, cfg.threads, [&](int y) {
                for (int x = 0; x < cw; ++x) {
                    double acc = 0.0; int n = 0;
                    for (int k = -r; k <= r; ++k) { const int xx = x + k; if (xx >= 0 && xx < cw) { acc += humidC.at(xx, y); ++n; } }
                    tmp.at(x, y) = static_cast<float>(acc / n);
                }
            });
            WgParallelFor(ch, cfg.threads, [&](int y) {
                for (int x = 0; x < cw; ++x) {
                    double acc = 0.0; int n = 0;
                    for (int k = -r; k <= r; ++k) { const int yy = y + k; if (yy >= 0 && yy < ch) { acc += tmp.at(x, yy); ++n; } }
                    humidC.at(x, y) = static_cast<float>(acc / n);
                }
            });
        }
    }
    // Білінійна інтерполяція на дрібну сітку + локальне висотне осушення.
    WgParallelFor(H, cfg.threads, [&](int y) {
        const double fy = std::clamp((y + 0.5) / f - 0.5, 0.0, ch - 1.0);
        const int y0 = static_cast<int>(fy), y1 = std::min(ch - 1, y0 + 1);
        const double ty = fy - y0;
        for (int x = 0; x < W; ++x) {
            const double fx = std::clamp((x + 0.5) / f - 0.5, 0.0, cw - 1.0);
            const int x0 = static_cast<int>(fx), x1 = std::min(cw - 1, x0 + 1);
            const double tx = fx - x0;
            const double top = humidC.at(x0, y0) * (1 - tx) + humidC.at(x1, y0) * tx;
            const double bot = humidC.at(x0, y1) * (1 - tx) + humidC.at(x1, y1) * tx;
            double humid = top * (1 - ty) + bot * ty;
            const float h = terrain.height.at(x, y);
            if (h > 0.0f) {
                // холодне високогір'я утримує менше вологи -> осушення з висотою
                const double altN = std::clamp(static_cast<double>(h) / cfg.terrain.maxHeightM, 0.0, 1.0);
                humid *= 1.0 - cp.altitudeDryness * (altN * altN * (3.0 - 2.0 * altN));
            } else {
                humid = 1.0;
            }
            res.humidity.at(x, y) = static_cast<float>(std::clamp(humid, 0.0, 1.0));
        }
    });

    // --- Біоми ---
    WgParallelFor(H, cfg.threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const float h = terrain.height.at(x, y);
            bool coastal = false;
            if (h > 0.0f) {
                static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (terrain.height.inside(nx, ny) && terrain.height.at(nx, ny) <= 0.0f) coastal = true;
                }
            }
            res.biome.at(x, y) = static_cast<uint8_t>(ClassifyBiome(h, terrain.slope.at(x, y),
                                                                  res.temperature.at(x, y),
                                                                  res.humidity.at(x, y), coastal));
        }
    });
    return res;
}

}  // namespace wg
