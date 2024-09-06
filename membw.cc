#include <iostream>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <vector>
#include <xmmintrin.h>

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
    __m128i *mem = (__m128i*)mem_block.data();

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

    __m128i tot1 = _mm_setzero_si128();
    __m128i tot2 = _mm_setzero_si128();
    uint64_t ns;
    size_t outer_iters = std::max(1ULL, size ? 16777216ULL / size : 0);
    size_t bytes = 0;
    do {
        for (size_t p = 0; p < outer_iters; ++p) {
            for (size_t i = 0; i < size; i += 64) {
                __m128i rhs1 = _mm_load_si128(mem + (i >> 5));
                __m128i rhs2 = _mm_load_si128(mem + (i >> 5) + 1);
                tot1 = _mm_add_epi8(tot1, rhs1);
                tot2 = _mm_add_epi8(tot2, rhs2);
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

    tot1 = _mm_add_epi8(tot1, tot2);

    int volatile dummy = _mm_movemask_epi8(tot1);

    double bytes_per_sec = bytes * 1e9 / ns;

    std::cout << engineering(bytes_per_sec, true, true) << "B/s\n";

    return 0;
}

int main(int argc, char **argv)
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
