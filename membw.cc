#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cerrno>
#include <atomic>
#include <cmath>

#if defined(__ARM_NEON)
#include <arm_neon.h>
typedef int32x4_t veci32;
static inline veci32 vec_zero()
{
    return vdupq_n_s32(0);
}
static inline veci32 vec_add(veci32 lhs, veci32 rhs)
{
    return vaddq_s32(lhs, rhs);
}
static inline veci32 vec_load(int32_t const *rhs)
{
    return vld1q_s32(rhs);
}
static inline int vec_movemask(veci32 rhs)
{
    // get the sign bit of each byte
    uint8x16_t bytes    = vreinterpretq_u8_s32(rhs);
    uint8x16_t highbits = vshrq_n_u8(bytes, 7); // 0/1 per byte

    // weight each bit position with unique powers of two
    // so we can sum-reduce into the final bitmask
    static const uint8x16_t bitpos =
        { 1,2,4,8,0x10,0x20,0x40,0x80, 1,2,4,8,0x10,0x20,0x40,0x80 };

    uint8x16_t weighted = vandq_u8(highbits, bitpos);

    // parallel reductions (tree), no scalar loop:
    // fold 16 -> 8 -> 4 -> 2 bytes; 
    //   result holds {lo_mask, hi_mask, _, _, _, _, _, _}
    uint8x8_t lo = vget_low_u8(weighted);
    uint8x8_t hi = vget_high_u8(weighted);

    // 8 bytes: pairwise sums of lo/hi
    uint8x8_t s = vpadd_u8(lo, hi);   
    
    // 4 bytes
    s = vpadd_u8(s, s);               
    
    // 2 bytes: [lo_mask, hi_mask, 0, 0, 0, 0, 0, 0]
    s = vpadd_u8(s, s);               

    // pack the two 8-bit halves into a 16-bit mask
    return vget_lane_u16(vreinterpret_u16_u8(s), 0);
}
#elif defined(__AVX2__)
#include <immintrin.h>
typedef __m256i veci32;
static inline veci32 vec_zero()
{
    return _mm256_setzero_si256();
}
static inline veci32 vec_add(veci32 lhs, veci32 rhs)
{
    return _mm256_add_epi32(lhs, rhs);
}
static inline veci32 vec_load(int32_t const *rhs)
{
    return _mm256_loadu_si256(
        reinterpret_cast<__m256i const *>(rhs));
}
static inline int vec_movemask(veci32 rhs)
{
    return _mm256_movemask_epi8(rhs);
}
#elif defined(__SSE2__)
#include <emmintrin.h>
typedef __m128i veci32;
static inline veci32 vec_zero()
{
    return _mm_setzero_si128();
}
static inline veci32 vec_add(veci32 lhs, veci32 rhs)
{
    return _mm_add_epi32(lhs, rhs);
}
static inline veci32 vec_load(int32_t const *rhs)
{
    return _mm_loadu_si128(
        reinterpret_cast<__m128i const *>(rhs));
}
static inline int vec_movemask(veci32 rhs)
{
    return _mm_movemask_epi8(rhs);
}
#else
#include <array>
typedef std::array<int32_t, 4> veci32;
static inline veci32 vec_zero()
{
    return { 0, 0, 0, 0 };
}
static inline veci32 vec_add(veci32 lhs, veci32 rhs)
{
    return {
        lhs[0] + rhs[0],
        lhs[1] + rhs[1],
        lhs[2] + rhs[2],
        lhs[3] + rhs[3]
    };
}
static inline veci32 vec_load(int32_t const *rhs)
{
    return {
        rhs[0],
        rhs[1],
        rhs[2],
        rhs[3]
    };
}
static inline int vec_movemask(veci32 rhs)
{
    return (!!(rhs[0])) |
        (!!(rhs[1]) << 1) |
        (!!(rhs[2]) << 2) |
        (!!(rhs[3]) << 3);
}
#endif

static inline veci32 vec_load(veci32 const *rhs)
{
    return vec_load(reinterpret_cast<int32_t const *>(rhs));
}

std::string engineering(uint64_t n,
    bool pad = true, bool frac = false)
{
    uint64_t n2 = n * 10;
    static char const * const units[] = {
        " ",
        "k",
        "M",
        "G",
        "T",
        "P",
        "E"
    };

    size_t i;
    for (i = 0; n >= 1024; ++i) {
        n /= 1024;
        n2 /= 1024;
    }

    char const *padding = "  ";

    padding += n >= 100;
    padding += n >= 10;

    std::string result;

    result.reserve(48);

    result += padding;

    result += std::to_string(n);

    if (frac) {
        result += '.';
        result += std::to_string(n2 % 10);
    }

    result += units[i];

    return result;
}

int measure(size_t max, int64_t duration_ns, size_t channel_count)
{
    uint64_t size = max << 10;

    std::cout << "Measuring " <<
        engineering(size) << "B: ";

    std::vector<char> mem_block(size);
    veci32 volatile *mem = (veci32*)mem_block.data();

    // Dirty the pages with a value based on the time
    uint64_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    uint64_t mask_63bits = 0x7fffffffffffffff;

    // It's called a multiplicative congruent
    // pseudo-random number generator if you want
    // to look up what's going on here
    // I selected a 63 bit period one.
    uint64_t multiplier = 6364136223846793005LL;

    for (size_t i = 0, e = size / sizeof(uint64_t); i < e; ++i) {
        reinterpret_cast<uint64_t volatile *>(mem)[i] = seed & mask_63bits;
        seed *= multiplier;
    }

#if FORGET_MEMORY_TRICK
    // Little gcc trick to make it forget
    // everything it knows about memory content
    // This prevents it being too clever to do the work
    __asm__ __volatile__ ("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif

    std::chrono::steady_clock::time_point en, st =
        std::chrono::steady_clock::now();

    veci32 tot1 = vec_zero();
    veci32 tot2 = vec_zero();
    int64_t ns;
    size_t outer_iters = std::max(1ULL, size ? 16777216ULL / size : 0);
    uint64_t bytes = 0;
    do {
        for (size_t p = 0; p < outer_iters; ++p) {
            for (size_t i = 0; i + sizeof(veci32) * 2 <= size; 
                i += sizeof(veci32) * 2) {
                veci32 rhs1 = vec_load((veci32*)mem + (i / (sizeof(veci32) * 2)));
                veci32 rhs2 = vec_load((veci32*)mem + (i / (sizeof(veci32) * 2)) + 1);
                tot1 = vec_add(tot1, rhs1);
                tot2 = vec_add(tot2, rhs2);
            }
#if FORGET_MEMORY_TRICK
            // Little gcc trick to make it forget
            // everything it knows about memory content
            // This prevents it being too clever to do the work
            __asm__ __volatile__ ("" : : : "memory");
#else
            std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
        }

        en = std::chrono::steady_clock::now();

        ns = std::chrono::duration_cast<
            std::chrono::nanoseconds>(en - st)
            .count();

        bytes += size * outer_iters;
    } while (ns < duration_ns);

    tot1 = vec_add(tot1, tot2);

    int volatile dummy = vec_movemask(tot1);

    double bytes_per_sec = bytes * 1e9 / ns;

    double megatransfers = bytes_per_sec / (8e6 * channel_count);
    double roundedMT = std::floor((megatransfers + 
        199.999999) / 200) * 200;

    std::cout << engineering(bytes_per_sec, true, true) << "B/s [ " <<
        channel_count << " x " << roundedMT << "MT/s ]\n";

    return 0;
}

template<typename T>
int chase_with(size_t max, int64_t duration_ns)
{
    // It is given in KB, convert to bytes
    max <<= 10;

    static_assert(sizeof(T) <= 8);

    union wrapper {
        T value;
        char bloat[64];
    };

    max /= sizeof(wrapper);

    std::vector<wrapper> mem(max);

    size_t i;
    for (i = 0; i + 1 < max; ++i)
        mem[i].value = i + 1;
    mem[i++].value = 0;

    __asm__ __volatile__ ("" : : : "memory");

    std::cout << "Measuring " <<
        engineering(max * sizeof(wrapper)) << "B: ";
    std::cout.flush();

    int64_t volatile sink;

    int64_t volatile iters = 1000000000;
    int64_t ps{};
    wrapper volatile *p = mem.data();
    for (int pass = 0; ; ++pass) {
        auto st = std::chrono::steady_clock::now();

        size_t index = 0;
        int64_t i = 0;

        // Initial unrolled steps
        while ((i & 3) != 0 && i < iters) {
            index = p[index].value;
            ++i;
        }

        // Main loop with unrolling
        for (; i + 4 <= iters; i += 4) {
            index = p[index].value;
            index = p[index].value;
            index = p[index].value;
            index = p[index].value;
        }

        // Handle remaining iterations
        for (; i < iters; ++i) {
            index = p[index].value;
        }

        sink = index;

        auto en = std::chrono::steady_clock::now();

        ps = 1000 * std::chrono::duration_cast<
            std::chrono::nanoseconds>(en - st).count();

        // If we got a whole second, good enough
        if (pass || ps > duration_ns * 1000)
            break;

        double scale = (double)duration_ns * 1000 / ps;
        if (scale <= 1.0)
            break;

        iters *= scale;
    }

    ps /= iters;

    std::cout << ps << "ps latency\n";
    // " (" << (4e12/ps) << "/s)"

    return EXIT_SUCCESS;
}

int chase(size_t max)
{
    int64_t duration_ns = 1000000000;
    if (max > std::numeric_limits<uint32_t>::max())
        return chase_with<uint64_t>(max, duration_ns);
    if (max > std::numeric_limits<uint16_t>::max())
        return chase_with<uint32_t>(max, duration_ns);
    if (max > std::numeric_limits<uint8_t>::max())
        return chase_with<uint16_t>(max, duration_ns);
    return chase_with<uint8_t>(max, duration_ns);
}

int internal_main(int argc, char const * const *argv, bool &quiet)
{
    size_t memsize_kib = 0;

    int64_t duration_ns = 1000000000LL;

    size_t channel_count = 2;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            std::clog << argv[0] << 
                " [--memk N] [--ns N] [--channels N] [--quiet]\n";
            quiet = true;
            return 1;
        }
    }

    for (int i = 1; i < argc; ++i) {
        char const *this_arg = argv[i];

        //
        // All the no-arg ones are up here
        if (!strcmp("--quiet", this_arg)) {
            quiet = 1;
            continue;
        }

        // If we made it here, we expect an argument
        char const *next_arg = argv[i + 1];

        // Whine about missing argument once
        if (!next_arg || (next_arg[0] == '-' && next_arg[1] == '-')) {
            std::clog << "Missing " << this_arg << " argument"
                " or unknown option " << (next_arg?next_arg:"") << "\n";
            return 1;
        }

        if (!strcmp("--memk", this_arg)) {
            memsize_kib = strtoull(next_arg, nullptr, 10);
        } else if (!strcmp("--channels", this_arg)) {
            channel_count = strtoull(next_arg, nullptr, 10);
        } else if (!strcmp("--ns", this_arg)) {
            duration_ns = strtoull(next_arg, nullptr, 10);
        } else {
            std::clog << "Unknown argument: " << this_arg << "\n";
            return 1;
        }
        
        // Skip over the argument we consumed
        ++i;
    }

    if (argc == 1 || memsize_kib == 0) {
        for (int i = 1; i <= 1048576; i += i) {
            if (!chase)
                measure(i, duration_ns, channel_count);
            else
                chase_with<unsigned>(i, duration_ns);
        }

        return EXIT_SUCCESS;
    }

    int result = EXIT_FAILURE;

    if (memsize_kib)
        result = measure(memsize_kib, duration_ns, channel_count);

    return result;
}

int main(int argc, char const * const *argv)
{
    bool quiet = false;
    int result = internal_main(argc, argv, quiet);
    if (!quiet) {
        std::cerr << "Press ENTER to exit\n";
    std::string input;
    std::getline(std::cin, input);
    }
    return result;
}

