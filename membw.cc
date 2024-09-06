#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <vector>

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
static int vec_movemask(veci32 rhs)
{
    // Reinterpret the int32x4_t as an int8x16_t
    int8x16_t byte_vec = vreinterpretq_s8_s32(rhs);

    // Shift right by 7 to move the sign bit
    // to the least significant bit of each byte
    uint8x16_t sign_bits = vshrq_n_u8(
        vreinterpretq_u8_s8(byte_vec), 7);

    // Narrow the result into an 8-bit mask (first 8 lanes)
    uint64_t low = vgetq_lane_u64(
        vreinterpretq_u64_u8(sign_bits), 0);
    uint64_t high = vgetq_lane_u64(
        vreinterpretq_u64_u8(sign_bits), 1);

    // Combine the high and low parts into a 16-bit integer mask
    return static_cast<uint16_t>((high << 8) | low);
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
    return _mm256_add_epi8(lhs, rhs);
}
static inline veci32 vec_load(int32_t const * rhs)
{
    return _mm256_load_si256(
        reinterpret_cast<__m256i const *>(rhs));
}
static int vec_movemask(veci32 rhs)
{
    return _mm256_movemask_epi8(rhs);
}
#elif defined(__SSE2__)
#include <xmmintrin.h>
typedef __m128i veci32;
static inline veci32 vec_zero()
{
    return _mm_setzero_si128();
}
static inline veci32 vec_add(veci32 lhs, veci32 rhs)
{
    return _mm_add_epi8(lhs, rhs);
}
static inline veci32 vec_load(int32_t const *rhs)
{
    return _mm_load_si128(
        reinterpret_cast<__m128i const *>(rhs));
}
static int vec_movemask(veci32 rhs)
{
    return _mm_movemask_epi8(rhs);
}
#else
#include <array>
typedef alignas(16) std::array<int32_t, 4> veci32;
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
    }
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
        "",
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

int measure(size_t max, int64_t duration_ns)
{
    uint64_t size = max << 10;

    std::cout << "Measuring " <<
        engineering(size) << "B: ";

    std::vector<char> mem_block(size);
    veci32 *mem = (veci32*)mem_block.data();

    if (!mem) {
        int err = errno;
        std::cerr << "Memory allocation failed: " <<
            strerror(err) << "\n";
        return EXIT_FAILURE;
    }

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
        reinterpret_cast<uint64_t*>(mem)[i] = seed & mask_63bits;
        seed *= multiplier;
    }

    // Little gcc trick to make it forget
    // everything it knows about memory content
    // This prevents it being too clever to do the work
    __asm__ __volatile__ ("" : : : "memory");

    std::chrono::steady_clock::time_point en, st =
        std::chrono::steady_clock::now();

    veci32 tot1 = vec_zero();
    veci32 tot2 = vec_zero();
    uint64_t ns;
    size_t outer_iters = std::max(1ULL, size ? 16777216ULL / size : 0);
    size_t bytes = 0;
    do {
        for (size_t p = 0; p < outer_iters; ++p) {
            for (size_t i = 0; i < size; i += sizeof(veci32) * 2) {
                veci32 rhs1 = vec_load(mem + (i / (sizeof(veci32) * 2)));
                veci32 rhs2 = vec_load(mem + (i / (sizeof(veci32) * 2)) + 1);
                tot1 = vec_add(tot1, rhs1);
                tot2 = vec_add(tot2, rhs2);
            }
            // Little gcc trick to make it forget
            // everything it knows about memory content
            // This prevents it being too clever to do the work
            __asm__ __volatile__ ("" : : : "memory");
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

    std::cout << engineering(bytes_per_sec, true, true) << "B/s\n";

    return 0;
}

int internal_main(int argc, char const * const *argv)
{
    size_t max = 1048576;

    int64_t duration_ns = 1000000000ULL;

    if (argc > 2)
        duration_ns = atoll(argv[2]);

    if (argc > 1)
        max = atoll(argv[1]);

    if (argc == 1 || max == 0) {
        for (int i = 1; i <= 1048576; i += i)
            measure(i, duration_ns);

        return EXIT_SUCCESS;
    }

    if (!max)
        max = 1048576;

    int result = EXIT_FAILURE;

    if (max)
        result = measure(max, duration_ns);

    return result;
}

int main(int argc, char const * const *argv)
{
    int result = internal_main(argc, argv);
    std::cout << "Press ENTER to exit\n";
    std::string input;
    std::getline(std::cin, input);
    return result;
}

