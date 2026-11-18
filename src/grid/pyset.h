// PySet<K>: CPython's set (Objects/setobject.c), reproduced so that walking a set gives the order Python gives —
// where that order reaches a result (MapData.adjacent: sets of province numbers, walked by the literacy replay's
// ring spread and the map checks). The table, linear probes, perturbation, dummies, resizing and the hashes are
// CPython's: hash(int) and hash((int, int)) (tuplehash, the xxHash scheme of Python 3.8+).
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace grid {

// hash(int) of CPython for a 64-bit value: modulo 2**61 - 1, the sign kept, -1 becomes -2
inline std::int64_t py_hash(std::int64_t x)
{
    constexpr std::uint64_t P = (1ULL << 61) - 1;
    const bool neg = x < 0;
    std::uint64_t a = neg ? (0ULL - static_cast<std::uint64_t>(x)) : static_cast<std::uint64_t>(x);
    a %= P;
    std::int64_t h = neg ? -static_cast<std::int64_t>(a) : static_cast<std::int64_t>(a);
    return h == -1 ? -2 : h;
}
// hash((a, b)) of CPython (tuplehash)
inline std::int64_t py_hash(const std::pair<std::int64_t, std::int64_t> &t)
{
    constexpr std::uint64_t P1 = 11400714785074694791ULL, P2 = 14029467366897019727ULL, P5 = 2870177450012600261ULL;
    std::uint64_t acc = P5;
    for (std::int64_t item : {t.first, t.second}) {
        const std::uint64_t lane = static_cast<std::uint64_t>(py_hash(item));
        acc += lane * P2;
        acc = (acc << 31) | (acc >> 33);
        acc *= P1;
    }
    acc += 2ULL ^ (P5 ^ 3527539ULL);
    if (acc == static_cast<std::uint64_t>(-1))
        return 1546275796;
    return static_cast<std::int64_t>(acc);
}

// hash(str) of CPython with hash randomization off (PYTHONHASHSEED=0: the SipHash-1-3 key is all zero) — over the
// string's own storage: 1, 2 or 4 bytes per character, as wide as its widest character needs. With a random seed
// (Python's default) the order of a set of texts differs from run to run in Python itself; this is the order of
// the seed-0 run, the one the checks compare with.
inline std::int64_t py_hash_bytes(const unsigned char *in, std::size_t n)
{
    if (n == 0)
        return 0;
    auto rotl = [](std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); };
    std::uint64_t v0 = 0x736f6d6570736575ULL, v1 = 0x646f72616e646f6dULL, v2 = 0x6c7967656e657261ULL,
                  v3 = 0x7465646279746573ULL;
    auto round = [&] {
        v0 += v1; v2 += v3; v1 = rotl(v1, 13) ^ v0; v3 = rotl(v3, 16) ^ v2; v0 = rotl(v0, 32);
        v2 += v1; v0 += v3; v1 = rotl(v1, 17) ^ v2; v3 = rotl(v3, 21) ^ v0; v2 = rotl(v2, 32);
    };
    std::uint64_t b = static_cast<std::uint64_t>(n) << 56;
    std::size_t left = n;
    while (left >= 8) {
        std::uint64_t mi = 0;
        for (int i = 7; i >= 0; --i)
            mi = (mi << 8) | in[i];
        in += 8;
        left -= 8;
        v3 ^= mi;
        round();
        v0 ^= mi;
    }
    std::uint64_t t = 0;
    for (std::size_t i = left; i > 0; --i)
        t = (t << 8) | in[i - 1];
    b |= t;
    v3 ^= b;
    round();
    v0 ^= b;
    v2 ^= 0xff;
    round();
    round();
    round();
    const std::int64_t x = static_cast<std::int64_t>((v0 ^ v1) ^ (v2 ^ v3));
    return x == -1 ? -2 : x;
}
template <typename S>
inline auto py_hash(const S &s) -> decltype(s.toUcs4(), std::int64_t())
{
    const auto u = s.toUcs4();
    char32_t top = 0;
    for (char32_t c : u)
        top = std::max(top, c);
    const std::size_t kind = top < 0x100 ? 1 : top < 0x10000 ? 2 : 4;
    std::vector<unsigned char> data;
    data.reserve(static_cast<std::size_t>(u.size()) * kind);
    for (char32_t c : u)
        for (std::size_t k = 0; k < kind; ++k)
            data.push_back(static_cast<unsigned char>((c >> (8 * k)) & 0xFF));
    return py_hash_bytes(data.data(), data.size());
}
// hash(None) is a constant since Python 3.12; a text or None (OptStr) hashes as the one it holds
template <typename T>
inline std::int64_t py_hash(const std::optional<T> &o)
{
    return o ? py_hash(*o) : std::int64_t(0xFCA86420);
}
// hash((a, b)) of CPython (tuplehash) for any two hashable things — texts: the Import page's (kind, key) ticks
template <typename A, typename B>
inline auto py_hash(const std::pair<A, B> &t) -> decltype(py_hash(t.first), py_hash(t.second), std::int64_t())
{
    constexpr std::uint64_t P1 = 11400714785074694791ULL, P2 = 14029467366897019727ULL, P5 = 2870177450012600261ULL;
    std::uint64_t acc = P5;
    for (const std::uint64_t lane : {static_cast<std::uint64_t>(py_hash(t.first)), static_cast<std::uint64_t>(py_hash(t.second))}) {
        acc += lane * P2;
        acc = (acc << 31) | (acc >> 33);
        acc *= P1;
    }
    acc += 2ULL ^ (P5 ^ 3527539ULL);
    if (acc == static_cast<std::uint64_t>(-1))
        return 1546275796;
    return static_cast<std::int64_t>(acc);
}

template <typename K>
class PySet {
public:
    PySet() : table_(MINSIZE) {}

    bool contains(const K &key) const { return find(key, py_hash(key)) >= 0; }
    std::size_t size() const { return used_; }
    bool empty() const { return used_ == 0; }

    void add(const K &key)
    {
        const std::int64_t hash = py_hash(key);
        const std::size_t mask = table_.size() - 1;
        std::size_t i = static_cast<std::size_t>(hash) & mask;
        std::ptrdiff_t freeslot = -1;
        std::size_t perturb = static_cast<std::size_t>(hash);
        while (true) {
            std::size_t e = i;
            int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
            do {
                Entry &en = table_[e];
                if (en.state == UNUSED) {
                    if (freeslot >= 0) {             // a dummy on the way: the LAST one passed is taken (fill stays)
                        table_[freeslot] = {ACTIVE, key, hash};
                        ++used_;
                        return;
                    }
                    en = {ACTIVE, key, hash};
                    ++fill_;
                    ++used_;
                    if (fill_ * 5 < mask * 3)
                        return;
                    resize(used_ > 50000 ? used_ * 2 : used_ * 4);
                    return;
                }
                if (en.state == ACTIVE && en.hash == hash && en.key == key)
                    return;                          // already there
                if (en.state == DUMMY)
                    freeslot = static_cast<std::ptrdiff_t>(e);
                ++e;
            } while (probes--);
            perturb >>= PERTURB_SHIFT;
            i = (i * 5 + 1 + perturb) & mask;
        }
    }

    bool discard(const K &key)
    {
        const std::ptrdiff_t e = find(key, py_hash(key));
        if (e < 0)
            return false;
        table_[e].state = DUMMY;
        --used_;
        return true;
    }

    // s.pop(): the first live entry from the search finger on (wrapping), which then moves past it
    K pop()
    {
        const std::size_t mask = table_.size() - 1;
        std::size_t e = finger_ & mask;
        while (table_[e].state != ACTIVE)
            e = e + 1 > mask ? 0 : e + 1;
        const K key = table_[e].key;
        table_[e].state = DUMMY;
        --used_;
        finger_ = e + 1;
        return key;
    }

    // set(o) / o.copy(): set_merge into an empty set (pre-sized; the table copied as it is when it can be)
    static PySet copy_of(const PySet &o)
    {
        PySet r;
        r.merge(o);
        return r;
    }
    // set(d) of a dict's keys (set_update_dict: ONE resize up front when the keys would fill the table — not the
    // resizes adding them one by one would make, so the table and its walk order can differ from add())
    static PySet from_keys(const std::vector<K> &keys)
    {
        PySet r;
        if ((r.fill_ + keys.size()) * 5 >= (r.table_.size() - 1) * 3)
            r.resize((r.used_ + keys.size()) * 2);
        for (const K &k : keys)
            r.add(k);
        return r;
    }
    // set_merge (self |= o for a set o)
    void merge(const PySet &o)
    {
        if (&o == this || o.used_ == 0)
            return;
        if ((fill_ + o.used_) * 5 >= (table_.size() - 1) * 3)
            resize((used_ + o.used_) * 2);
        if (fill_ == 0 && table_.size() == o.table_.size() && o.fill_ == o.used_) {
            for (std::size_t i = 0; i < table_.size(); ++i)
                if (o.table_[i].state == ACTIVE)
                    table_[i] = o.table_[i];
            fill_ = o.fill_;
            used_ = o.used_;
            return;
        }
        if (fill_ == 0) {
            fill_ = used_ = o.used_;
            for (const Entry &en : o.table_)
                if (en.state == ACTIVE)
                    insert_clean(en);
            return;
        }
        for (const Entry &en : o.table_)
            if (en.state == ACTIVE)
                add(en.key);
    }
    // self - o (set_difference; for a much larger self: a copy with o's keys discarded)
    PySet difference(const PySet &o) const
    {
        if ((used_ >> 2) > o.used_) {
            PySet r = copy_of(*this);
            for (const Entry &en : o.table_)
                if (en.state == ACTIVE)
                    r.discard(en.key);
            return r;
        }
        PySet r;
        for (const Entry &en : table_)
            if (en.state == ACTIVE && !o.contains(en.key))
                r.add(en.key);
        return r;
    }

    // the keys in the order Python walks them (the table's order)
    std::vector<K> items() const
    {
        std::vector<K> out;
        out.reserve(used_);
        for (const Entry &en : table_)
            if (en.state == ACTIVE)
                out.push_back(en.key);
        return out;
    }

private:
    static constexpr std::size_t MINSIZE = 8;
    static constexpr int LINEAR_PROBES = 9;
    static constexpr int PERTURB_SHIFT = 5;
    enum State : std::uint8_t { UNUSED, ACTIVE, DUMMY };
    struct Entry {
        State state = UNUSED;
        K key{};
        std::int64_t hash = 0;
    };
    std::vector<Entry> table_;
    std::size_t fill_ = 0, used_ = 0;
    std::size_t finger_ = 0;                         // where pop() looks first

    std::ptrdiff_t find(const K &key, std::int64_t hash) const   // set_lookkey
    {
        const std::size_t mask = table_.size() - 1;
        std::size_t i = static_cast<std::size_t>(hash) & mask;
        std::size_t perturb = static_cast<std::size_t>(hash);
        while (true) {
            std::size_t e = i;
            int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
            do {
                const Entry &en = table_[e];
                if (en.state == UNUSED)
                    return -1;
                if (en.state == ACTIVE && en.hash == hash && en.key == key)
                    return static_cast<std::ptrdiff_t>(e);
                ++e;
            } while (probes--);
            perturb >>= PERTURB_SHIFT;
            i = (i * 5 + 1 + perturb) & mask;
        }
    }

    void insert_clean(const Entry &en)               // set_insert_clean: into a table without dummies
    {
        const std::size_t mask = table_.size() - 1;
        std::size_t perturb = static_cast<std::size_t>(en.hash);
        std::size_t i = static_cast<std::size_t>(en.hash) & mask;
        while (true) {
            if (table_[i].state == UNUSED) {
                table_[i] = en;
                return;
            }
            if (i + LINEAR_PROBES <= mask)
                for (int j = 1; j <= LINEAR_PROBES; ++j)
                    if (table_[i + j].state == UNUSED) {
                        table_[i + j] = en;
                        return;
                    }
            perturb >>= PERTURB_SHIFT;
            i = (i * 5 + 1 + perturb) & mask;
        }
    }

    void resize(std::size_t minused)                 // set_table_resize: dummies dropped, the old table's order
    {
        std::size_t newsize = MINSIZE;
        while (newsize <= minused)
            newsize <<= 1;
        std::vector<Entry> old;
        old.swap(table_);
        table_.assign(newsize, Entry());
        fill_ = used_;
        for (const Entry &en : old)
            if (en.state == ACTIVE)
                insert_clean(en);
    }
};

}  // namespace grid
