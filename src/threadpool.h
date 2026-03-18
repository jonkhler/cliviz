#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>

namespace cliviz {

// Simple fork-join thread pool for parallel row-band processing.
// Workers are created once and reused across frames.
class ThreadPool {
public:
    explicit ThreadPool(uint32_t num_threads = 0);
    ~ThreadPool();

    // Execute `fn(thread_id, num_threads)` on each worker, wait for all to finish.
    void parallel_for(std::function<void(uint32_t, uint32_t)> fn);

    uint32_t thread_count() const { return n_threads; }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    uint32_t n_threads;
    std::vector<std::thread> workers;
    std::function<void(uint32_t, uint32_t)> task;
    std::mutex mtx;
    std::condition_variable cv_start;
    std::condition_variable cv_done;
    std::atomic<uint32_t> pending{0};
    bool stop = false;
    uint64_t generation = 0;

    void worker_loop(uint32_t id);
};

} // namespace cliviz
