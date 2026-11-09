#include "wg/terrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "wg/parallel.h"

namespace wg {
namespace {

constexpr double kDegToRad = 0.017453292519943295;

inline double Smooth(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Continent {
    double field;  // >threshold = суша
    double rx, ry; // координати у системі повернутого еліпса (частки розміру світу)
};

class TerrainSampler {
public:
    explicit TerrainSampler(const WorldConfig& cfg)
        : cfg_(cfg), tp_(cfg.terrain), type_(cfg.terrain.noise),
          cosR_(std::cos(cfg.terrain.shapeRotationDeg * kDegToRad)),
          sinR_(std::sin(cfg.terrain.shapeRotationDeg * kDegToRad)) {}

    // Поле континенту: 1 - відстань до центру деформованого еліпса + шум узбережжя.
    Continent ContinentAt(double xKm, double yKm) const {
        double u = xKm / cfg_.widthKm - 0.5;
        double v = yKm / cfg_.heightKm - 0.5;
        const double wl = 170.0;
        const double wx = Fbm(type_, xKm / wl + 3.1, yKm / wl + 7.7, cfg_.seed ^ 0xA1u, 3);
        const double wy = Fbm(type_, xKm / wl - 5.3, yKm / wl + 1.9, cfg_.seed ^ 0xB2u, 3);
        u += wx * tp_.warpStrength;
        v += wy * tp_.warpStrength;
        Continent c;
        c.rx = u * cosR_ + v * sinR_;
        c.ry = -u * sinR_ + v * cosR_;
        const double d = std::sqrt((c.rx / tp_.axisEW) * (c.rx / tp_.axisEW) +
                                   (c.ry / tp_.axisNS) * (c.ry / tp_.axisNS));
        const double coast = Fbm(type_, xKm / 55.0, yKm / 55.0, cfg_.seed ^ 0xC3u, 4) * tp_.coastNoise +
                             Fbm(type_, xKm / 17.0, yKm / 17.0, cfg_.seed ^ 0xD4u, 3) * tp_.coastNoise * 0.4;
        c.field = 1.0 - d + coast;
        return c;
    }

    // Нормована висота суші в [0, ~1] до глобального масштабування.
    double LandShape(double xKm, double yKm, const Continent& c, double inlandN) const {
        const double cell = cfg_.cellMeters;
        // Гірські хребти: головний уздовж сходу (Тхебек) + діагональний (Собек).
        const double ridgeShift = 0.06 * Fbm(type_, yKm / 110.0 + 11.0, 0.5, cfg_.seed ^ 0x11u, 2);
        const double g1 = Gauss(c.rx - (tp_.ridgeCenter + ridgeShift), tp_.ridgeWidth);
        const double mod1 = 0.55 + 0.45 * (0.5 + 0.5 * Fbm(type_, yKm / 45.0, xKm / 300.0, cfg_.seed ^ 0x12u, 3));
        const double diag = 0.05 + 0.25 * c.ry;
        const double g2 = Gauss(c.rx - diag, 0.07) * Smooth(-0.25, 0.05, c.ry);
        const double mod2 = 0.55 + 0.45 * (0.5 + 0.5 * Fbm(type_, xKm / 40.0, yKm / 40.0, cfg_.seed ^ 0x13u, 3));
        const double massif = Smooth(0.35, 0.85, 0.5 + 0.5 * Fbm(type_, xKm / 70.0, yKm / 70.0, cfg_.seed ^ 0x14u, 3)) * 0.30;
        const double M = std::pow(std::clamp(g1 * mod1 + tp_.ridge2Strength * g2 * mod2 + massif * inlandN, 0.0, 1.0), 1.4);

        const int octR = OctavesFor(60000.0, cell, 7);
        const int octH = OctavesFor(7000.0, cell, 4);
        const int octD = OctavesFor(1500.0, cell, 3);
        // Ridged з domain warping: хребти замість замкнених «комірок».
        const double wr1 = Fbm(type_, xKm / 45.0 + 9.7, yKm / 45.0 + 2.1, cfg_.seed ^ 0x24u, 3);
        const double wr2 = Fbm(type_, xKm / 45.0 - 4.3, yKm / 45.0 + 6.6, cfg_.seed ^ 0x25u, 3);
        const double ridged = Ridged(type_, xKm / 60.0 + 0.5 * wr1, yKm / 60.0 + 0.5 * wr2, cfg_.seed ^ 0x21u, octR);
        const double uplift = 0.5 + 0.5 * Fbm(type_, xKm / 30.0, yKm / 30.0, cfg_.seed ^ 0x26u, 3);
        const double hills = 0.5 + 0.5 * Fbm(type_, xKm / 7.0, yKm / 7.0, cfg_.seed ^ 0x22u, octH);
        const double detail = Fbm(type_, xKm / 1.5, yKm / 1.5, cfg_.seed ^ 0x23u, octD);

        const double ramp = Smooth(0.0, 0.06, inlandN * 0.5);  // пологе узбережжя
        const double base = 0.04 + 0.12 * hills;
        const double mount = M * (0.15 + 0.55 * ridged + 0.30 * uplift) * 0.85;
        return ramp * (base + mount + 0.008 * detail) + 0.0;
    }

private:
    static double Gauss(double d, double sigma) { return std::exp(-(d * d) / (2.0 * sigma * sigma)); }

    const WorldConfig& cfg_;
    const TerrainParams& tp_;
    NoiseType type_;
    double cosR_, sinR_;
};

}  // namespace

TerrainResult GenerateTerrain(const WorldConfig& cfg) {
    const int W = std::max(2, static_cast<int>(std::lround(cfg.widthKm * 1000.0 / cfg.cellMeters)));
    const int H = std::max(2, static_cast<int>(std::lround(cfg.heightKm * 1000.0 / cfg.cellMeters)));
    const double cellKm = cfg.cellMeters / 1000.0;
    TerrainSampler sampler(cfg);

    // 1) Калібрування порогу суші на грубій вибірці поля континенту.
    const double landFraction = std::clamp(cfg.terrain.targetLandKm2 / (cfg.widthKm * cfg.heightKm), 0.02, 0.98);
    const int cw = std::min(W, 400), ch = std::max(2, static_cast<int>(static_cast<double>(cw) * H / W));
    std::vector<double> coarse(static_cast<size_t>(cw) * ch);
    WgParallelFor(ch, cfg.threads, [&](int y) {
        for (int x = 0; x < cw; ++x) {
            const double xKm = (x + 0.5) / cw * cfg.widthKm, yKm = (y + 0.5) / ch * cfg.heightKm;
            coarse[static_cast<size_t>(y) * cw + x] = sampler.ContinentAt(xKm, yKm).field;
        }
    });
    std::vector<double> sorted = coarse;
    const size_t k = static_cast<size_t>((1.0 - landFraction) * static_cast<double>(sorted.size()));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(k), sorted.end());
    const double t = sorted[k];
    const double cLow = *std::min_element(coarse.begin(), coarse.end());
    const double cHigh = *std::max_element(coarse.begin(), coarse.end());

    // 2) Основний прохід: форма рельєфу (океан кодується від'ємними значеннями).
    TerrainResult res;
    res.seaThreshold = t;
    res.height = Grid<float>(W, H);
    std::vector<float> rowMax(static_cast<size_t>(H), 0.0f);
    WgParallelFor(H, cfg.threads, [&](int y) {
        float mx = 0.0f;
        for (int x = 0; x < W; ++x) {
            const double xKm = (x + 0.5) * cellKm, yKm = (y + 0.5) * cellKm;
            const Continent c = sampler.ContinentAt(xKm, yKm);
            if (c.field <= t) {
                const double depth = std::clamp((t - c.field) / std::max(1e-9, t - cLow), 0.0, 1.0);
                res.height.at(x, y) = static_cast<float>(-(0.5 + 199.5 * std::pow(depth, 0.7)));
            } else {
                const double inlandN = std::clamp((c.field - t) / std::max(1e-9, cHigh - t), 0.0, 1.0);
                const float v = static_cast<float>(std::max(0.0, sampler.LandShape(xKm, yKm, c, inlandN)));
                res.height.at(x, y) = v;
                mx = std::max(mx, v);
            }
        }
        rowMax[static_cast<size_t>(y)] = mx;
    });
    const float shapeMax = std::max(1e-6f, *std::max_element(rowMax.begin(), rowMax.end()));

    // 3) Нормування: найвища точка = maxHeightM, показник степеня формує низини.
    const double p = cfg.terrain.heightExponent;
    std::vector<int> landRows(static_cast<size_t>(H), 0);
    WgParallelFor(H, cfg.threads, [&](int y) {
        int land = 0;
        for (int x = 0; x < W; ++x) {
            float& v = res.height.at(x, y);
            if (v < 0.0f) continue;
            const double n = std::pow(static_cast<double>(v) / shapeMax, p);
            v = static_cast<float>(std::max(0.5, n * cfg.terrain.maxHeightM));
            ++land;
        }
        landRows[static_cast<size_t>(y)] = land;
    });
    long long landCells = 0;
    for (int v : landRows) landCells += v;
    res.landKm2 = static_cast<double>(landCells) * cellKm * cellKm;
    res.maxHeightM = 0.0;
    for (float v : res.height.data) res.maxHeightM = std::max(res.maxHeightM, static_cast<double>(v));

    // 4) Схили (центральні різниці), tan кута нахилу.
    res.slope = Grid<float>(W, H);
    WgParallelFor(H, cfg.threads, [&](int y) {
        const int y0 = std::max(0, y - 1), y1 = std::min(H - 1, y + 1);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - 1), x1 = std::min(W - 1, x + 1);
            const double dx = (res.height.at(x1, y) - res.height.at(x0, y)) / ((x1 - x0) * cfg.cellMeters);
            const double dy = (res.height.at(x, y1) - res.height.at(x, y0)) / ((y1 - y0) * cfg.cellMeters);
            res.slope.at(x, y) = static_cast<float>(std::sqrt(dx * dx + dy * dy));
        }
    });
    return res;
}

}  // namespace wg
