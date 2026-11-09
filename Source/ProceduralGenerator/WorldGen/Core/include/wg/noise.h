// Власна реалізація градієнтних шумів (Perlin, Simplex), fBm та ridged-шуму.
// Без залежностей від Unreal Engine. Детерміновано за (x, y, seed).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wg {

// Цілочисельний хеш вузла решітки (avalanche-міксер).
inline uint32_t Hash2(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = seed * 0x9E3779B1u ^ static_cast<uint32_t>(x) * 0x85EBCA6Bu ^
                 static_cast<uint32_t>(y) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

enum class NoiseType : uint8_t { Perlin, Simplex };

namespace detail {

// 16 одиничних градієнтів (кроком 22.5 градуса): 8 з таблиці + їх від'ємні.
inline void Gradient(uint32_t h, double& gx, double& gy) {
    static constexpr double c[8] = {1.0,           0.92387953251, 0.70710678118, 0.38268343236,
                                    0.0,           -0.38268343236, -0.70710678118, -0.92387953251};
    static constexpr double s[8] = {0.0,           0.38268343236, 0.70710678118, 0.92387953251,
                                    1.0,           0.92387953251, 0.70710678118, 0.38268343236};
    const uint32_t k = (h >> 8) & 15u;
    const double sign = (k & 8u) ? -1.0 : 1.0;
    gx = sign * c[k & 7u];
    gy = sign * s[k & 7u];
}

inline double Fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

}  // namespace detail

// Класичний (покращений) шум Перліна, 2D. Результат приблизно в [-1, 1].
inline double Perlin2(double x, double y, uint32_t seed) {
    const double fx0 = std::floor(x), fy0 = std::floor(y);
    const int32_t xi = static_cast<int32_t>(fx0), yi = static_cast<int32_t>(fy0);
    const double fx = x - fx0, fy = y - fy0;
    double g0x, g0y, g1x, g1y, g2x, g2y, g3x, g3y;
    detail::Gradient(Hash2(xi, yi, seed), g0x, g0y);
    detail::Gradient(Hash2(xi + 1, yi, seed), g1x, g1y);
    detail::Gradient(Hash2(xi, yi + 1, seed), g2x, g2y);
    detail::Gradient(Hash2(xi + 1, yi + 1, seed), g3x, g3y);
    const double a = g0x * fx + g0y * fy;
    const double b = g1x * (fx - 1.0) + g1y * fy;
    const double c = g2x * fx + g2y * (fy - 1.0);
    const double d = g3x * (fx - 1.0) + g3y * (fy - 1.0);
    const double u = detail::Fade(fx), v = detail::Fade(fy);
    const double ab = a + (b - a) * u;
    const double cd = c + (d - c) * u;
    return (ab + (cd - ab) * v) * 1.41421356237;  // нормування до ~[-1, 1]
}

// Simplex-шум (Gustavson), 2D. Результат приблизно в [-1, 1].
inline double Simplex2(double x, double y, uint32_t seed) {
    constexpr double F2 = 0.3660254037844386;   // (sqrt(3)-1)/2
    constexpr double G2 = 0.21132486540518713;  // (3-sqrt(3))/6
    const double s = (x + y) * F2;
    const int32_t i = static_cast<int32_t>(std::floor(x + s));
    const int32_t j = static_cast<int32_t>(std::floor(y + s));
    const double t = (i + j) * G2;
    const double x0 = x - (i - t), y0 = y - (j - t);
    const int32_t i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
    const double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
    const double x2 = x0 - 1.0 + 2.0 * G2, y2 = y0 - 1.0 + 2.0 * G2;
    double n = 0.0, gx, gy;
    double t0 = 0.5 - x0 * x0 - y0 * y0;
    if (t0 > 0.0) {
        detail::Gradient(Hash2(i, j, seed), gx, gy);
        t0 *= t0;
        n += t0 * t0 * (gx * x0 + gy * y0);
    }
    double t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 > 0.0) {
        detail::Gradient(Hash2(i + i1, j + j1, seed), gx, gy);
        t1 *= t1;
        n += t1 * t1 * (gx * x1 + gy * y1);
    }
    double t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 > 0.0) {
        detail::Gradient(Hash2(i + 1, j + 1, seed), gx, gy);
        t2 *= t2;
        n += t2 * t2 * (gx * x2 + gy * y2);
    }
    return n * 99.0;  // коефіцієнт підібрано емпірично: діапазон ~[-1, 1] для одиничних градієнтів
}

inline double Noise2(NoiseType type, double x, double y, uint32_t seed) {
    return type == NoiseType::Perlin ? Perlin2(x, y, seed) : Simplex2(x, y, seed);
}

// Фрактальний броунівський рух: сума октав, нормована до [-1, 1].
inline double Fbm(NoiseType type, double x, double y, uint32_t seed, int octaves,
                  double lacunarity = 2.0, double gain = 0.5) {
    double sum = 0.0, amp = 1.0, norm = 0.0, f = 1.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * Noise2(type, x * f + i * 17.31, y * f - i * 9.17, seed + static_cast<uint32_t>(i) * 0x9E37u);
        norm += amp;
        amp *= gain;
        f *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

// Ridged multifractal: гострі хребти. Результат в [0, 1].
inline double Ridged(NoiseType type, double x, double y, uint32_t seed, int octaves,
                     double lacunarity = 2.0, double gain = 0.5) {
    double sum = 0.0, amp = 1.0, norm = 0.0, f = 1.0, weight = 1.0;
    for (int i = 0; i < octaves; ++i) {
        double n = Noise2(type, x * f + i * 31.7, y * f + i * 5.3, seed + static_cast<uint32_t>(i) * 0x7F4Au);
        n = 1.0 - std::fabs(n);
        n *= n;
        n *= weight;
        weight = std::clamp(n * 2.0, 0.0, 1.0);
        sum += n * amp;
        norm += amp;
        amp *= gain;
        f *= lacunarity;
    }
    return norm > 0.0 ? std::clamp(sum / norm, 0.0, 1.0) : 0.0;
}

// Кількість октав, які мають сенс при заданій довжині хвилі та розмірі клітинки
// (деталі менші за 2 клітинки дають лише аліасинг).
inline int OctavesFor(double wavelengthMeters, double cellMeters, int maxOctaves) {
    const double ratio = wavelengthMeters / (2.0 * cellMeters);
    int n = ratio >= 1.0 ? static_cast<int>(std::floor(std::log2(ratio))) + 1 : 1;
    return std::clamp(n, 1, maxOctaves);
}

}  // namespace wg
