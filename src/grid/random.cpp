// numpy's default_rng, line by line after numpy/random/bit_generator.pyx (SeedSequence), numpy/random/src/pcg64
// (PCG64) and numpy/random/src/distributions (bounded integers, doubles).
#include "grid/random.h"

#include <intrin.h>
#include <cmath>
#include <stdexcept>

namespace grid {

namespace {
constexpr std::uint32_t INIT_A = 0x43b0d7e5u, MULT_A = 0x931e8875u, INIT_B = 0x8b51f9ddu, MULT_B = 0x58f38dedu;
constexpr std::uint32_t MIX_MULT_L = 0xca01f9ddu, MIX_MULT_R = 0x4973f715u;
constexpr int XSHIFT = 16;
constexpr std::size_t POOL_SIZE = 4;

std::uint32_t hashmix(std::uint32_t value, std::uint32_t &hash_const)
{
    value ^= hash_const;
    hash_const *= MULT_A;
    value *= hash_const;
    value ^= value >> XSHIFT;
    return value;
}
std::uint32_t mix(std::uint32_t x, std::uint32_t y)
{
    std::uint32_t result = MIX_MULT_L * x - MIX_MULT_R * y;
    result ^= result >> XSHIFT;
    return result;
}

U128 mul(const U128 &a, const U128 &b)               // a * b mod 2^128
{
    U128 r;
    std::uint64_t hi;
    r.lo = _umul128(a.lo, b.lo, &hi);
    r.hi = hi + a.lo * b.hi + a.hi * b.lo;
    return r;
}
U128 add(const U128 &a, const U128 &b)
{
    U128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
    return r;
}
const U128 MULT{2549297995355413924ULL, 4865540595714422341ULL};   // PCG_DEFAULT_MULTIPLIER_128

void step(PCG64 &g) { g.state = add(mul(g.state, MULT), g.inc); }
}  // namespace

SeedSequence::SeedSequence(std::uint64_t n)
{
    // _coerce_to_uint32_array(int): the int's 32-bit words, lowest first ([0] for 0)
    std::vector<std::uint32_t> entropy;
    if (n == 0)
        entropy.push_back(0);
    while (n > 0) {
        entropy.push_back(static_cast<std::uint32_t>(n & 0xFFFFFFFFu));
        n >>= 32;
    }
    pool.assign(POOL_SIZE, 0);
    std::uint32_t hash_const = INIT_A;
    for (std::size_t i = 0; i < POOL_SIZE; ++i)
        pool[i] = hashmix(i < entropy.size() ? entropy[i] : 0u, hash_const);
    for (std::size_t i_src = 0; i_src < POOL_SIZE; ++i_src)
        for (std::size_t i_dst = 0; i_dst < POOL_SIZE; ++i_dst)
            if (i_src != i_dst)
                pool[i_dst] = mix(pool[i_dst], hashmix(pool[i_src], hash_const));
    for (std::size_t i_src = POOL_SIZE; i_src < entropy.size(); ++i_src)
        for (std::size_t i_dst = 0; i_dst < POOL_SIZE; ++i_dst)
            pool[i_dst] = mix(pool[i_dst], hashmix(entropy[i_src], hash_const));
}

std::vector<std::uint32_t> SeedSequence::generate_state32(std::size_t n_words) const
{
    std::vector<std::uint32_t> state(n_words);
    std::uint32_t hash_const = INIT_B;
    for (std::size_t i = 0; i < n_words; ++i) {
        std::uint32_t data_val = pool[i % pool.size()];
        data_val ^= hash_const;
        hash_const *= MULT_B;
        data_val *= hash_const;
        data_val ^= data_val >> XSHIFT;
        state[i] = data_val;
    }
    return state;
}

std::vector<std::uint64_t> SeedSequence::generate_state64(std::size_t n_words) const
{
    const std::vector<std::uint32_t> w = generate_state32(n_words * 2);   // (viewed as uint64, little endian)
    std::vector<std::uint64_t> out(n_words);
    for (std::size_t i = 0; i < n_words; ++i)
        out[i] = static_cast<std::uint64_t>(w[2 * i]) | (static_cast<std::uint64_t>(w[2 * i + 1]) << 32);
    return out;
}

PCG64::PCG64(const SeedSequence &seq)
{
    const std::vector<std::uint64_t> val = seq.generate_state64(4);
    const U128 initstate{val[0], val[1]}, initseq{val[2], val[3]};
    // pcg_setseq_128_srandom_r
    state = U128{};
    inc = U128{(initseq.hi << 1) | (initseq.lo >> 63), (initseq.lo << 1) | 1u};
    step(*this);
    state = add(state, initstate);
    step(*this);
}

std::uint64_t PCG64::next64()
{
    step(*this);                                     // (numpy steps first, then outputs the new state)
    const std::uint64_t x = state.hi ^ state.lo;
    const unsigned rot = static_cast<unsigned>(state.hi >> 58);   // state >> 122
    return (x >> rot) | (x << ((64u - rot) & 63u));
}

std::uint32_t PCG64::next32()
{
    if (has_uint32) {
        has_uint32 = false;
        return uinteger;
    }
    const std::uint64_t next = next64();
    has_uint32 = true;
    uinteger = static_cast<std::uint32_t>(next >> 32);
    return static_cast<std::uint32_t>(next & 0xFFFFFFFFu);
}

double PCG64::next_double() { return static_cast<double>(next64() >> 11) * (1.0 / 9007199254740992.0); }

std::uint64_t Generator::bounded(std::uint64_t rng)
{
    if (rng == 0)
        return 0;
    if (rng <= 0xFFFFFFFFULL) {
        if (rng == 0xFFFFFFFFULL)
            return bit.next32();
        // buffered_bounded_lemire_uint32
        const std::uint32_t r = static_cast<std::uint32_t>(rng);
        const std::uint32_t rng_excl = r + 1;
        std::uint64_t m = static_cast<std::uint64_t>(bit.next32()) * rng_excl;
        std::uint32_t leftover = static_cast<std::uint32_t>(m & 0xFFFFFFFFu);
        if (leftover < rng_excl) {
            const std::uint32_t threshold = (0xFFFFFFFFu - r) % rng_excl;
            while (leftover < threshold) {
                m = static_cast<std::uint64_t>(bit.next32()) * rng_excl;
                leftover = static_cast<std::uint32_t>(m & 0xFFFFFFFFu);
            }
        }
        return m >> 32;
    }
    if (rng == 0xFFFFFFFFFFFFFFFFULL)
        return bit.next64();
    // bounded_lemire_uint64
    const std::uint64_t rng_excl = rng + 1;
    std::uint64_t hi;
    std::uint64_t lo = _umul128(bit.next64(), rng_excl, &hi);
    if (lo < rng_excl) {
        const std::uint64_t threshold = (0xFFFFFFFFFFFFFFFFULL - rng) % rng_excl;
        while (lo < threshold)
            lo = _umul128(bit.next64(), rng_excl, &hi);
    }
    return hi;
}

std::int64_t Generator::integers(std::int64_t low, std::int64_t high)
{
    if (high <= low)
        throw std::invalid_argument("high <= low");
    const std::uint64_t rng = static_cast<std::uint64_t>(high - 1) - static_cast<std::uint64_t>(low);
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) + bounded(rng));
}

std::vector<std::int64_t> Generator::integers(std::int64_t low, std::int64_t high, std::size_t n)
{
    // (one fill: numpy's buffer is per call, but for int64 the 32-bit "buffer" is the generator's own has_uint32)
    std::vector<std::int64_t> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(integers(low, high));
    return out;
}

double Generator::random() { return bit.next_double(); }

#include "grid/ziggurat.inc"

double Generator::standard_normal()
{
    static constexpr double R = 3.6541528853610087963519472518, INV_R = 0.27366123732975827203338247596;
    for (;;) {
        std::uint64_t r = bit.next64();
        const int idx = int(r & 0xff);
        r >>= 8;
        const bool sign = r & 0x1;
        const std::uint64_t rabs = (r >> 1) & 0x000fffffffffffffULL;
        double x = double(rabs) * wi_double[idx];
        if (sign)
            x = -x;
        if (rabs < ki_double[idx])
            return x;                            // 99.3 % of the time
        if (idx == 0) {
            for (;;) {
                const double xx = -INV_R * std::log1p(-bit.next_double());
                const double yy = -std::log1p(-bit.next_double());
                if (yy + yy > xx * xx)
                    return ((rabs >> 8) & 0x1) ? -(R + xx) : R + xx;
            }
        }
        if ((fi_double[idx - 1] - fi_double[idx]) * bit.next_double() + fi_double[idx] < std::exp(-0.5 * x * x))
            return x;
    }
}

// ---------------------------------------------------------------------------------------------- Python's random
PyRandom::PyRandom(std::uint64_t seed)
{
    // random_seed: the absolute value as 32-bit words, least significant first (at least one word)
    std::vector<std::uint32_t> key;
    do {
        key.push_back(static_cast<std::uint32_t>(seed & 0xffffffffu));
        seed >>= 32;
    } while (seed);
    init_genrand(19650218u);
    std::size_t i = 1, j = 0;
    const std::size_t n = 624, len = key.size();
    for (std::size_t k = n > len ? n : len; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + static_cast<std::uint32_t>(j);
        ++i;
        ++j;
        if (i >= n) {
            mt[0] = mt[n - 1];
            i = 1;
        }
        if (j >= len)
            j = 0;
    }
    for (std::size_t k = n - 1; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - static_cast<std::uint32_t>(i);
        ++i;
        if (i >= n) {
            mt[0] = mt[n - 1];
            i = 1;
        }
    }
    mt[0] = 0x80000000u;
    mti = 624;
}

void PyRandom::init_genrand(std::uint32_t s)
{
    mt[0] = s;
    for (mti = 1; mti < 624; ++mti)
        mt[mti] = 1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<std::uint32_t>(mti);
}

std::uint32_t PyRandom::genrand_uint32()
{
    static const std::uint32_t mag01[2] = {0x0u, 0x9908b0dfu};
    constexpr int N = 624, M = 397;
    constexpr std::uint32_t UPPER = 0x80000000u, LOWER = 0x7fffffffu;
    std::uint32_t y;
    if (mti >= N) {
        int kk = 0;
        for (; kk < N - M; ++kk) {
            y = (mt[kk] & UPPER) | (mt[kk + 1] & LOWER);
            mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1u];
        }
        for (; kk < N - 1; ++kk) {
            y = (mt[kk] & UPPER) | (mt[kk + 1] & LOWER);
            mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1u];
        }
        y = (mt[N - 1] & UPPER) | (mt[0] & LOWER);
        mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1u];
        mti = 0;
    }
    y = mt[mti++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= (y >> 18);
    return y;
}

double PyRandom::random()
{
    const std::uint32_t a = genrand_uint32() >> 5, b = genrand_uint32() >> 6;
    return (double(a) * 67108864.0 + double(b)) * (1.0 / 9007199254740992.0);
}

std::uint32_t PyRandom::getrandbits(int k)
{
    return genrand_uint32() >> (32 - k);
}

long long PyRandom::randbelow(long long n)
{
    if (n <= 0)
        return 0;
    int k = 0;                                   // n.bit_length()
    for (unsigned long long x = static_cast<unsigned long long>(n); x; x >>= 1)
        ++k;
    if (k > 32)
        throw std::runtime_error("PyRandom.randbelow: more than 32 bits");
    long long r = getrandbits(k);
    while (r >= n)
        r = getrandbits(k);
    return r;
}
}  // namespace grid
