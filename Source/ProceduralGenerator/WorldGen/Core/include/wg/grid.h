#pragma once
#include <cstddef>
#include <vector>

namespace wg {

// Проста 2D-сітка. x — схід, y — південь (рядок 0 — північ).
template <class T>
struct Grid {
    int w = 0, h = 0;
    std::vector<T> data;

    Grid() = default;
    Grid(int width, int height, T init = T()) : w(width), h(height), data(static_cast<size_t>(width) * height, init) {}

    T& at(int x, int y) { return data[static_cast<size_t>(y) * w + x]; }
    const T& at(int x, int y) const { return data[static_cast<size_t>(y) * w + x]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    size_t size() const { return data.size(); }
};

}  // namespace wg
