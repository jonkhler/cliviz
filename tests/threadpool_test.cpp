#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include "threadpool.h"

using namespace cliviz;

TEST(ThreadPool, CreatesRequestedThreads) {
    ThreadPool pool(4);
    EXPECT_EQ(pool.thread_count(), 4u);
}

TEST(ThreadPool, ParallelForExecutesAllThreads) {
    ThreadPool pool(4);
    std::atomic<uint32_t> counter{0};

    pool.parallel_for([&](uint32_t /*id*/, uint32_t /*n*/) {
        counter.fetch_add(1);
    });

    EXPECT_EQ(counter.load(), 4u);
}

TEST(ThreadPool, EachThreadGetsUniqueId) {
    ThreadPool pool(4);
    std::vector<std::atomic<bool>> seen(4);
    for (auto& s : seen) s = false;

    pool.parallel_for([&](uint32_t id, uint32_t n) {
        EXPECT_EQ(n, 4u);
        EXPECT_LT(id, 4u);
        seen[id] = true;
    });

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(seen[i].load()) << "Thread " << i << " was not executed";
    }
}

TEST(ThreadPool, CanRunMultipleBatches) {
    ThreadPool pool(2);
    std::atomic<uint32_t> total{0};

    for (int batch = 0; batch < 10; ++batch) {
        pool.parallel_for([&](uint32_t /*id*/, uint32_t /*n*/) {
            total.fetch_add(1);
        });
    }

    EXPECT_EQ(total.load(), 20u); // 2 threads × 10 batches
}

TEST(ThreadPool, ParallelRowBandPartitioning) {
    // Simulate row-band partitioning of a pixel buffer
    constexpr uint32_t HEIGHT = 100;
    std::vector<uint8_t> pixels(HEIGHT, 0);
    ThreadPool pool(4);

    pool.parallel_for([&](uint32_t id, uint32_t n) {
        uint32_t row_start = (HEIGHT * id) / n;
        uint32_t row_end = (HEIGHT * (id + 1)) / n;
        for (uint32_t y = row_start; y < row_end; ++y) {
            pixels[y] = static_cast<uint8_t>(id + 1);
        }
    });

    // All rows should be written
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        EXPECT_GT(pixels[y], 0u) << "Row " << y << " was not processed";
    }
}
