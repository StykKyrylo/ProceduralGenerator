#pragma once
#include <atomic>
#include <thread>
#include <vector>

namespace wg {

inline int ResolveThreads(int requested) {
    if (requested > 0) return requested;
    const unsigned n = std::thread::hardware_concurrency();
    return n ? static_cast<int>(n) : 1;
}

// Паралельний цикл по індексах [0, n). Ітерації мають бути незалежними
// (тоді результат не залежить від кількості потоків).
template <class F>
void WgParallelFor(int n, int threads, F&& f) {
    threads = ResolveThreads(threads);
    if (threads <= 1 || n <= 1) {
        for (int i = 0; i < n; ++i) f(i);
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (;;) {
                const int i = next.fetch_add(1);
                if (i >= n) break;
                f(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

}  // namespace wg
