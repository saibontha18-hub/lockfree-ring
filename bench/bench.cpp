/*
 * bench_ring — how much does the lock actually cost?
 *
 * Two scenarios, both moving N ints from producer to consumer:
 *   ping-pong: one thread alternates push/pop — no contention, measures
 *              raw per-op cost.
 *   pipeline:  producer and consumer on separate threads, spinning on
 *              full/empty the way an ISR-to-task handoff would.
 */
#include <chrono>
#include <cstdio>
#include <thread>

#include "ring.h"
#include "mutex_ring.h"

static const unsigned N = 2000000;

template <typename Ring>
static double ping_pong()
{
    Ring r;
    int v = 0;
    const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
    for (unsigned i = 0; i < N; ++i) {
        while (!r.push((int)i)) {
        }
        while (!r.pop(v)) {
        }
    }
    const std::chrono::steady_clock::time_point t1 =
        std::chrono::steady_clock::now();
    const std::chrono::duration<double> dt = t1 - t0;
    return dt.count() / N * 1e9;         /* ns per item, round trip */
}

template <typename Ring>
static double pipeline()
{
    Ring r;
    unsigned long long sum = 0;
    const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
    std::thread prod([&]() {
        for (unsigned i = 0; i < N; ++i)
            while (!r.push((int)i)) {
            }
    });
    std::thread cons([&]() {
        int v = 0;
        for (unsigned i = 0; i < N; ++i) {
            while (!r.pop(v)) {
            }
            sum += (unsigned)v;
        }
    });
    prod.join();
    cons.join();
    const std::chrono::steady_clock::time_point t1 =
        std::chrono::steady_clock::now();
    /* Checksum so the compiler can't delete the consumer loop. */
    if (sum != (unsigned long long)N * (N - 1) / 2)
        std::printf("checksum mismatch: %llu\n", sum);
    const std::chrono::duration<double> dt = t1 - t0;
    return dt.count() / N * 1e9;         /* ns per item */
}

int main()
{
    const double lf_pp = ping_pong<SpscRing<int, 4096> >();
    const double lf_pl = pipeline<SpscRing<int, 4096> >();
    const double mx_pp = ping_pong<MutexRing<int, 4096> >();
    const double mx_pl = pipeline<MutexRing<int, 4096> >();

    std::printf("lockfree-ring benchmark — %u items, -O2\n", N);
    std::printf("%-10s %14s %14s\n", "ring", "ping-pong", "pipeline");
    std::printf("%-10s %14s %14s\n", "", "(ns/item)", "(ns/item)");
    std::printf("%-10s %14.1f %14.1f\n", "SpscRing", lf_pp, lf_pl);
    std::printf("%-10s %14.1f %14.1f\n", "MutexRing", mx_pp, mx_pl);
    return 0;
}
