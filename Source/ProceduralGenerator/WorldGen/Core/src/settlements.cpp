#include "wg/settlements.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "wg/noise.h"
#include "wg/parallel.h"

namespace wg {
namespace {

using B = Biome;

inline double Hash01(int32_t a, int32_t b, uint32_t salt) { return Hash2(a, b, salt) / 4294967296.0; }

// Плавний перехід 0 -> 1 між a і b (a<b).
inline double Ramp(double x, double a, double b) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
// Трапеція: підйом a->b, плато, спад c->d.
inline double Band(double x, double a, double b, double c, double d) { return Ramp(x, a, b) * (1.0 - Ramp(x, c, d)); }
inline double Flat(double slope, double full, double zero) { return 1.0 - Ramp(slope, full, zero); }

bool IsForest(B b) { return b == B::TemperateForest || b == B::BorealForest || b == B::SubtropicalForest; }
bool IsOpenLowland(B b) { return b == B::TemperateGrassland || b == B::TemperateForest || b == B::SubtropicalForest; }
// Сухі відкриті біоми теж придатні, але гірші (воду треба привозити).
double LowlandWeight(B b) {
    if (IsOpenLowland(b)) return 1.0;
    return (b == B::DryScrub || b == B::ColdSteppe || b == B::Savanna) ? 0.55 : 0.0;
}

struct ModuleSlot { ModuleRole role; float lx, ly; };

struct Spec {
    float radiusM;
    float maxFootprintSlope;  // макс. середній схил у межах ділянки (щоб не треба було терраформити)
    std::vector<ModuleSlot> modules;
};

const Spec& SpecOf(SettlementType t) {
    using R = ModuleRole;
    static const Spec specs[] = {
        // Capital
        {600.0f, 0.07f,
         {{R::GateMain, 0, 480}, {R::GuardPavilion, -60, 480}, {R::GuardPavilion, 60, 480}, {R::GateSide, -330, 200},
          {R::GateSide, 330, 200}, {R::HallMain, 0, 100}, {R::HallMain, 0, -120}, {R::Residence, -150, -40},
          {R::Residence, 150, -40}, {R::Residence, -150, -220}, {R::Residence, 150, -220}, {R::Pavilion, 0, -330},
          {R::Shrine, 260, -260}, {R::Shrine, -260, -260}}},
        // FortressTown
        {300.0f, 0.09f,
         {{R::GateMain, 0, 260}, {R::GuardPavilion, -110, 260}, {R::GuardPavilion, 110, 260}, {R::GateSide, -200, 0},
          {R::GateSide, 200, 0}, {R::Residence, -90, 60}, {R::Residence, 90, 60}, {R::Residence, 0, -80}}},
        // ShrineGrove
        {110.0f, 0.25f, {{R::GateSide, 0, 90}, {R::Shrine, 0, -10}, {R::Shrine, 55, -60}}},
        // GardenPavilion
        {60.0f, 0.15f, {{R::Pavilion, 0, 0}, {R::Pavilion, 40, -30}}},
        // WatchPost
        {50.0f, 0.30f, {{R::GuardPavilion, 0, 0}, {R::GateSide, 0, 35}}},
    };
    return specs[static_cast<int>(t)];
}

struct Coarse {
    int w = 0, h = 0, f = 1;
    double cellM = 0;
    Grid<float> height, slope, temp, hum, coastKm, relief, smooth;
    Grid<uint8_t> biome, ocean;
};

// Ковзне середнє (box blur) із префіксними сумами.
Grid<float> BoxBlur(const Grid<float>& g, int r) {
    Grid<float> tmp(g.w, g.h), out(g.w, g.h);
    std::vector<double> ps(static_cast<size_t>(std::max(g.w, g.h)) + 1);
    for (int y = 0; y < g.h; ++y) {
        ps[0] = 0;
        for (int x = 0; x < g.w; ++x) ps[x + 1] = ps[x] + g.at(x, y);
        for (int x = 0; x < g.w; ++x) {
            const int a = std::max(0, x - r), b = std::min(g.w - 1, x + r);
            tmp.at(x, y) = static_cast<float>((ps[b + 1] - ps[a]) / (b - a + 1));
        }
    }
    for (int x = 0; x < g.w; ++x) {
        ps[0] = 0;
        for (int y = 0; y < g.h; ++y) ps[y + 1] = ps[y] + tmp.at(x, y);
        for (int y = 0; y < g.h; ++y) {
            const int a = std::max(0, y - r), b = std::min(g.h - 1, y + r);
            out.at(x, y) = static_cast<float>((ps[b + 1] - ps[a]) / (b - a + 1));
        }
    }
    return out;
}

Coarse BuildCoarse(const WorldConfig& cfg, const TerrainResult& t, const ClimateResult& c) {
    Coarse k;
    k.f = std::max(1, static_cast<int>(std::lround(250.0 / cfg.cellMeters)));
    k.w = (t.height.w + k.f - 1) / k.f;
    k.h = (t.height.h + k.f - 1) / k.f;
    k.cellM = cfg.cellMeters * k.f;
    const double cellKm = k.cellM / 1000.0;
    k.height = Grid<float>(k.w, k.h);
    k.slope = Grid<float>(k.w, k.h);
    k.temp = Grid<float>(k.w, k.h);
    k.hum = Grid<float>(k.w, k.h);
    k.biome = Grid<uint8_t>(k.w, k.h);
    k.ocean = Grid<uint8_t>(k.w, k.h);
    WgParallelFor(k.h, cfg.threads, [&](int cy) {
        for (int cx = 0; cx < k.w; ++cx) {
            double sh = 0, ss = 0, st = 0, sm = 0;
            int land = 0, n = 0;
            for (int y = cy * k.f; y < std::min(t.height.h, (cy + 1) * k.f); ++y)
                for (int x = cx * k.f; x < std::min(t.height.w, (cx + 1) * k.f); ++x) {
                    ++n;
                    const float h = t.height.at(x, y);
                    if (h <= 0.0f) continue;
                    ++land;
                    sh += h; ss += t.slope.at(x, y); st += c.temperature.at(x, y); sm += c.humidity.at(x, y);
                }
            const int mx = std::min(t.height.w - 1, cx * k.f + k.f / 2), my = std::min(t.height.h - 1, cy * k.f + k.f / 2);
            k.ocean.at(cx, cy) = land * 2 < n;
            k.height.at(cx, cy) = land ? static_cast<float>(sh / land) : 0.0f;
            k.slope.at(cx, cy) = land ? static_cast<float>(ss / land) : 0.0f;
            k.temp.at(cx, cy) = land ? static_cast<float>(st / land) : 0.0f;
            k.hum.at(cx, cy) = land ? static_cast<float>(sm / land) : 1.0f;
            k.biome.at(cx, cy) = c.biome.at(mx, my);
        }
    });
    // Відстань до океану (chamfer, км).
    const float kFar = 1e9f;
    k.coastKm = Grid<float>(k.w, k.h, kFar);
    for (int y = 0; y < k.h; ++y) for (int x = 0; x < k.w; ++x) if (k.ocean.at(x, y)) k.coastKm.at(x, y) = 0.0f;
    auto relax = [&](int x, int y, int dx, int dy, float w) {
        if (!k.coastKm.inside(x + dx, y + dy)) return;
        k.coastKm.at(x, y) = std::min(k.coastKm.at(x, y), k.coastKm.at(x + dx, y + dy) + w);
    };
    const float a = static_cast<float>(cellKm), d = static_cast<float>(cellKm * 1.41421356);
    for (int y = 0; y < k.h; ++y) for (int x = 0; x < k.w; ++x) { relax(x, y, -1, 0, a); relax(x, y, 0, -1, a); relax(x, y, -1, -1, d); relax(x, y, 1, -1, d); }
    for (int y = k.h - 1; y >= 0; --y) for (int x = k.w - 1; x >= 0; --x) { relax(x, y, 1, 0, a); relax(x, y, 0, 1, a); relax(x, y, 1, 1, d); relax(x, y, -1, 1, d); }
    // Відносна висота: різниця з середнім у радіусі ~2 км (долина < 0, пагорб > 0).
    const Grid<float> mean2km = BoxBlur(k.height, std::max(1, static_cast<int>(std::lround(2.0 / cellKm))));
    k.relief = Grid<float>(k.w, k.h);
    for (size_t i = 0; i < k.relief.size(); ++i) k.relief.data[i] = k.height.data[i] - mean2km.data[i];
    k.smooth = BoxBlur(k.height, 2);
    return k;
}

double Score(SettlementType type, const Coarse& k, int x, int y) {
    const double h = k.height.at(x, y), s = k.slope.at(x, y), hum = k.hum.at(x, y), tmp = k.temp.at(x, y);
    const double coast = k.coastKm.at(x, y), rel = k.relief.at(x, y);
    const B b = static_cast<B>(k.biome.at(x, y));
    if (k.ocean.at(x, y) || b == B::Ocean || b == B::Beach) return 0.0;
    switch (type) {
        case SettlementType::Capital: {
            const double bio = std::max(0.25, LowlandWeight(b));
            // столиця тяжіє до центру світу
            const double cdx = static_cast<double>(x) / k.w - 0.5, cdy = static_cast<double>(y) / k.h - 0.5;
            const double centrality = 1.0 - 0.8 * std::min(1.0, std::hypot(cdx, cdy) / 0.5);
            return Flat(s, 0.02, 0.07) * Band(h, -1, 5, 120, 400) * Band(coast, 0.5, 8, 80, 200) *
                   Ramp(hum, 0.10, 0.30) * Band(tmp, 6, 9, 17, 20) * bio * centrality;
        }
        case SettlementType::FortressTown: {
            const double bio = b == B::BorealForest ? 0.6 : LowlandWeight(b);
            return Flat(s, 0.03, 0.09) * Band(h, 5, 30, 500, 900) * Ramp(-rel, 10, 60) * Ramp(hum, 0.10, 0.30) * bio;
        }
        case SettlementType::ShrineGrove: {
            if (!IsForest(b)) return 0.0;
            return Band(s, 0.04, 0.10, 0.25, 0.40) * Band(h, 80, 200, 900, 1400) * Ramp(hum, 0.3, 0.5);
        }
        case SettlementType::GardenPavilion: {
            const double bio = LowlandWeight(b);
            const double view = std::max(Ramp(rel, 5, 30), 0.6 * (1.0 - Ramp(coast, 0.5, 6.0)));
            return bio * Flat(s, 0.05, 0.15) * Band(h, 0, 5, 400, 800) * view * Ramp(hum, 0.10, 0.30);
        }
        case SettlementType::WatchPost: {
            if (b == B::Rock) return 0.0;
            return (1.0 - Ramp(coast, 0.3, 2.0)) * Ramp(h, 3, 12) * Ramp(rel, 5, 25) * Flat(s, 0.15, 0.40);
        }
        default: return 0.0;
    }
}

struct Candidate { double score; int x, y; };

}  // namespace

const char* SettlementTypeNameUa(SettlementType t) {
    static const char* n[] = {"Палацовий комплекс", "Адмін-центр (фортеця)", "Святилище", "Павільйон-краєвид", "Сторожовий пост"};
    return n[static_cast<int>(t)];
}

const char* ModuleRoleName(ModuleRole r) {
    static const char* n[] = {"GateMain", "GateSide", "HallMain", "Residence", "Pavilion", "Shrine", "GuardPavilion"};
    return n[static_cast<int>(r)];
}

std::vector<Settlement> GenerateSettlements(const WorldConfig& cfg, const TerrainResult& terrain,
                                            const ClimateResult& climate, const SettlementParams& params) {
    const Coarse k = BuildCoarse(cfg, terrain, climate);
    std::vector<Settlement> result;
    uint32_t nextId = 1;
    const double landKm2 = terrain.landKm2;

    for (int ti = 0; ti < static_cast<int>(SettlementType::Count); ++ti) {
        const SettlementType type = static_cast<SettlementType>(ti);
        const Spec& spec = SpecOf(type);
        int count = type == SettlementType::Capital
                        ? 1
                        : std::max(1, static_cast<int>(std::lround(params.densityPer1000Km2[ti] * landKm2 / 1000.0)));
        const uint32_t salt = cfg.seed * 2246822519u + static_cast<uint32_t>(ti) * 3266489917u;

        // Кандидати з оцінкою (з невеликим детермінованим шумом для розсіювання вибору).
        std::vector<Candidate> cand;
        for (int y = 1; y < k.h - 1; ++y)
            for (int x = 1; x < k.w - 1; ++x) {
                double s = Score(type, k, x, y);
                if (s < 0.2) continue;
                s *= 0.85 + 0.30 * Hash01(x, y, salt);
                cand.push_back({s, x, y});
            }
        std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
            return a.score != b.score ? a.score > b.score : (a.y != b.y ? a.y < b.y : a.x < b.x);
        });

        const double minDistM = type == SettlementType::Capital ? 0.0 : 600.0 * std::sqrt(landKm2 / count);
        const int rc = std::max(1, static_cast<int>(std::ceil(spec.radiusM / k.cellM)));
        int placed = 0;
        for (const Candidate& cd : cand) {
            if (placed >= count) break;
            const double px = (cd.x + 0.5) * k.cellM, py = (cd.y + 0.5) * k.cellM;
            // 1) ділянка повністю на суші та достатньо пласка
            bool ok = cd.x - rc >= 0 && cd.y - rc >= 0 && cd.x + rc < k.w && cd.y + rc < k.h;
            double maxSlope = 0.0;
            for (int yy = cd.y - rc; ok && yy <= cd.y + rc; ++yy)
                for (int xx = cd.x - rc; xx <= cd.x + rc; ++xx) {
                    const double dx = (xx - cd.x) * k.cellM, dy = (yy - cd.y) * k.cellM;
                    if (dx * dx + dy * dy > spec.radiusM * spec.radiusM) continue;
                    if (k.ocean.at(xx, yy)) { ok = false; break; }
                    maxSlope = std::max(maxSlope, static_cast<double>(k.slope.at(xx, yy)));
                }
            if (!ok || maxSlope > spec.maxFootprintSlope) continue;
            // 2) відстані до вже розміщених
            for (const Settlement& s : result) {
                const double d = std::hypot(px - s.x, py - s.y);
                const double need = s.type == type ? std::max(minDistM, spec.radiusM + s.radiusM + 150.0)
                                                   : spec.radiusM + s.radiusM + 150.0;
                if (d < need) { ok = false; break; }
            }
            if (!ok) continue;

            Settlement st;
            st.id = nextId++;
            st.type = type;
            st.x = static_cast<float>(px);
            st.y = static_cast<float>(py);
            const int fx = std::clamp(static_cast<int>(px / cfg.cellMeters), 0, terrain.height.w - 1);
            const int fy = std::clamp(static_cast<int>(py / cfg.cellMeters), 0, terrain.height.h - 1);
            st.z = terrain.height.at(fx, fy);
            st.radiusM = spec.radiusM;
            st.footprintMaxSlope = static_cast<float>(maxSlope);
            st.biome = static_cast<Biome>(k.biome.at(cd.x, cd.y));
            st.tempC = k.temp.at(cd.x, cd.y);
            st.humidity = k.hum.at(cd.x, cd.y);
            st.coastDistKm = k.coastKm.at(cd.x, cd.y);
            st.score = static_cast<float>(cd.score);

            // Орієнтація «обличчям вниз по схилу» (принцип «спиною до гори, обличчям до низу»);
            // на рівному місці — у бік моря.
            double fxv = 0.0, fyv = 1.0;
            const double gx = (k.smooth.at(cd.x + 1, cd.y) - k.smooth.at(cd.x - 1, cd.y)) / (2.0 * k.cellM);
            const double gy = (k.smooth.at(cd.x, cd.y + 1) - k.smooth.at(cd.x, cd.y - 1)) / (2.0 * k.cellM);
            if (std::hypot(gx, gy) > 0.004) {
                fxv = -gx; fyv = -gy;
            } else {
                const double cgx = (k.coastKm.at(cd.x + 1, cd.y) - k.coastKm.at(cd.x - 1, cd.y));
                const double cgy = (k.coastKm.at(cd.x, cd.y + 1) - k.coastKm.at(cd.x, cd.y - 1));
                if (std::hypot(cgx, cgy) > 1e-6) { fxv = -cgx; fyv = -cgy; }
            }
            const double len = std::hypot(fxv, fyv);
            fxv /= len; fyv /= len;
            st.yawDeg = static_cast<float>(std::atan2(fyv, fxv) * 57.29577951308232);

            // Збірка комплексу з модулів за шаблоном (праворуч = (-fy, fx), вперед = (fx, fy)).
            int mi = 0;
            for (const ModuleSlot& m : spec.modules) {
                const double jx = (Hash01(static_cast<int32_t>(st.id), mi, cfg.seed ^ 0xA11Cu) - 0.5) * 6.0;
                const double jy = (Hash01(static_cast<int32_t>(st.id), mi, cfg.seed ^ 0xB22Du) - 0.5) * 6.0;
                const double lx = m.lx + jx, ly = m.ly + jy;
                ModulePlacement mp;
                mp.role = m.role;
                mp.x = static_cast<float>(st.x + (-fyv) * lx + fxv * ly);
                mp.y = static_cast<float>(st.y + fxv * lx + fyv * ly);
                mp.z = st.z;
                mp.yawDeg = st.yawDeg;
                mp.scale = static_cast<float>(0.95 + 0.10 * Hash01(static_cast<int32_t>(st.id), mi, cfg.seed ^ 0xC33Eu));
                const int nVar = std::max(1, params.catalog.variants[static_cast<int>(m.role)]);
                mp.variant = static_cast<uint8_t>(Hash2(static_cast<int32_t>(st.id), mi, cfg.seed ^ 0xD44Fu) % nVar);
                st.modules.push_back(mp);
                ++mi;
            }
            result.push_back(std::move(st));
            ++placed;
        }
    }
    return result;
}

}  // namespace wg
