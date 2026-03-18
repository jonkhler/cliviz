#include "threadpool.h"

namespace cliviz {

ThreadPool::ThreadPool(uint32_t num_threads) {
    n_threads = num_threads > 0 ? num_threads
                                : std::max(1u, std::thread::hardware_concurrency());
    workers.reserve(n_threads);
    for (uint32_t i = 0; i < n_threads; ++i) {
        workers.emplace_back(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        stop = true;
    }
    cv_start.notify_all();
    for (auto& w : workers) {
        w.join();
    }
}

void ThreadPool::parallel_for(std::function<void(uint32_t, uint32_t)> fn) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        task = std::move(fn);
        pending = n_threads;
        ++generation;
    }
    cv_start.notify_all();

    // Wait for all workers to complete
    std::unique_lock<std::mutex> lock(mtx);
    cv_done.wait(lock, [this] { return pending == 0; });
}

void ThreadPool::worker_loop(uint32_t id) {
    uint64_t my_gen = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        cv_start.wait(lock, [this, my_gen] { return stop || generation > my_gen; });
        if (stop) return;
        my_gen = generation;
        auto fn = task;
        uint32_t nt = n_threads;
        lock.unlock();

        fn(id, nt);

        if (pending.fetch_sub(1) == 1) {
            cv_done.notify_one();
        }
    }
}

} // namespace cliviz
