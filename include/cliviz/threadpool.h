#pragma once

#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace cliviz {

// Fork-join parallelism for row-band processing.
// Spawns threads per call (no persistent worker pool — avoids deadlock
// complexity; thread creation cost is negligible vs. per-frame render work).
class ThreadPool {
public:
    explicit ThreadPool(uint32_t num_threads = 0);
    ~ThreadPool();

    // Execute `fn(thread_id, num_threads)` on each thread, wait for all to finish.
    // One chunk runs on the calling thread; N-1 threads are spawned.
    void parallel_for(std::function<void(uint32_t, uint32_t)> fn);

    uint32_t thread_count() const { return n_threads; }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    uint32_t n_threads;
};

} // namespace cliviz
