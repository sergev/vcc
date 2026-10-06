//
// The binary32 soft-float runtime (libc/common/float32.c) compiled for the host.  Its
// sqrtf is checked against the host's over every one of the 2^32 inputs, bit for bit,
// any NaN matching any NaN.  The work is split among threads, and still takes about half
// a minute, so the test is DISABLED_: not part of the suite, run by hand after a change
// to float32.c's sqrtf, with these options:
//
//   ./build/backend/msp430/msp430-tests --gtest_also_run_disabled_tests
//       --gtest_filter=Float32Host.DISABLED_SqrtfEveryInput
//
#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

extern "C" float f32_sqrtf(float);

namespace {

// The inputs lo..hi-1 whose result differs from the host's: their count, and the first.
void SqrtRange(uint64_t lo, uint64_t hi, std::atomic<uint64_t> &bad, std::atomic<uint64_t> &first)
{
    uint64_t n = 0;
    for (uint64_t u = lo; u < hi; u++) {
        float x;
        uint32_t b = (uint32_t)u;
        memcpy(&x, &b, 4);
        float got = f32_sqrtf(x), want = std::sqrt(x);
        uint32_t g, w;
        memcpy(&g, &got, 4);
        memcpy(&w, &want, 4);
        if (g != w && !(std::isnan(got) && std::isnan(want))) {
            if (n++ == 0) {
                uint64_t f = first.load();
                while (u < f && !first.compare_exchange_weak(f, u)) {
                }
            }
        }
    }
    bad += n;
}

} // namespace

TEST(Float32Host, DISABLED_SqrtfEveryInput)
{
    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads == 0)
        nthreads = 4;
    const uint64_t total = 1ULL << 32;
    std::atomic<uint64_t> bad{ 0 }, first{ total };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < nthreads; t++)
        threads.emplace_back(SqrtRange, total * t / nthreads, total * (t + 1) / nthreads,
                             std::ref(bad), std::ref(first));
    for (auto &th : threads)
        th.join();
    EXPECT_EQ(0u, bad.load()) << "first wrong input: 0x" << std::hex << first.load();
}
