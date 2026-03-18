#include "threadpool.h"

namespace cliviz {

ThreadPool::ThreadPool(uint32_t num_threads) {
    n_threads = num_threads > 0 ? num_threads
                                : std::max(1u, std::thread::hardware_concurrency());
}

ThreadPool::~ThreadPool() = default;

void ThreadPool::parallel_for(std::function<void(uint32_t, uint32_t)> fn) {
    if (n_threads <= 1) {
        fn(0, 1);
        return;
    }

    // Spawn N-1 threads, run one chunk on the calling thread
    std::vector<std::thread> threads;
    threads.reserve(n_threads - 1);
    for (uint32_t i = 1; i < n_threads; ++i) {
        threads.emplace_back(fn, i, n_threads);
    }
    fn(0, n_threads);
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace cliviz
