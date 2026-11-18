// numpy's np.random.default_rng(seed): SeedSequence -> PCG64 -> Generator, reproduced exactly for the calls the
// editor makes (PLAN.md §4.3): integers(low, high[, n]) (dtype int64, high excluded) and random(). They decide the
// colours of new provinces, which are written into definition.csv. normal(loc, scale) (river wiggles): numpy's
// ziggurat with numpy's own tables (grid/ziggurat.inc, read out of numpy by tools/make_ziggurat.py) — the wiggles match too.
#pragma once
#include <cstdint>
#include <utility>
#include <optional>
#include <vector>

namespace grid {

struct U128 {
    std::uint64_t hi = 0, lo = 0;
};

// numpy.random.SeedSequence(entropy) for a non-negative int, pool size 4
class SeedSequence {
public:
    explicit SeedSequence(std::uint64_t entropy);
    std::vector<std::uint32_t> pool;
    std::vector<std::uint32_t> generate_state32(std::size_t n_words) const;
    std::vector<std::uint64_t> generate_state64(std::size_t n_words) const;   // dtype=np.uint64
};

// numpy.random.PCG64 (XSL-RR 128/64), with numpy's 32-bit buffer
class PCG64 {
public:
    explicit PCG64(const SeedSequence &seq);
    U128 state, inc;
    bool has_uint32 = false;
    std::uint32_t uinteger = 0;
    std::uint64_t next64();
    std::uint32_t next32();
    double next_double();
};

class Generator {
public:
    explicit Generator(std::uint64_t seed) : bit(SeedSequence(seed)) {}
    PCG64 bit;
    // integers(low, high): one int64 in [low, high)
    std::int64_t integers(std::int64_t low, std::int64_t high);
    // integers(low, high, n)
    std::vector<std::int64_t> integers(std::int64_t low, std::int64_t high, std::size_t n);
    double random();
    double standard_normal();                    // numpy's random_standard_normal (ziggurat)
    double normal(double loc, double scale) { return loc + scale * standard_normal(); }

private:
    std::uint64_t bounded(std::uint64_t rng);   // numpy's random_bounded_uint64 (Lemire, no mask), one value
};

// Python's random.Random(seed) (CPython's _randommodule.c: MT19937, init_by_array of the seed's 32-bit words), for the
// calls the editor makes: random(), randrange(n), shuffle(x) (diploweb's euler_grid, seed 7).
class PyRandom {
public:
    explicit PyRandom(std::uint64_t seed);
    std::uint32_t genrand_uint32();
    double random();                             // 53 bits: (a * 67108864 + b) / 2**53
    std::uint32_t getrandbits(int k);            // 0 < k <= 32
    long long randbelow(long long n);            // _randbelow_with_getrandbits
    long long randrange(long long n) { return randbelow(n); }
    template <typename T>
    void shuffle(std::vector<T> &x)
    {
        for (std::size_t i = x.size() - 1; x.size() > 1 && i > 0; --i) {
            const std::size_t j = static_cast<std::size_t>(randbelow(static_cast<long long>(i) + 1));
            std::swap(x[i], x[j]);
        }
    }

private:
    std::uint32_t mt[624];
    int mti = 625;
    void init_genrand(std::uint32_t s);
};
}  // namespace grid
