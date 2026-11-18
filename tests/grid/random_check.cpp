// grid/random against numpy, the C++ side of tools/check_random.py.  Usage: med_random <ops.txt>
#include "../core/nopopup.h"
#include "grid/random.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string u128_dec(const grid::U128 &v)   // the 128-bit number in decimal
{
    std::uint64_t hi = v.hi, lo = v.lo;
    std::string out;
    while (hi || lo) {
        // divide (hi, lo) by 10
        const std::uint64_t qh = hi / 10, rh = hi % 10;
        // (rh * 2^64 + lo) / 10
        const unsigned __int64 big_hi = rh;
        std::uint64_t rem;
        const std::uint64_t ql = _udiv128(big_hi, lo, 10, &rem);
        out.insert(out.begin(), static_cast<char>('0' + rem));
        hi = qh;
        lo = ql;
    }
    return out.empty() ? "0" : out;
}

static std::string repr(double d)
{
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".en") == std::string::npos)
        s += ".0";
    // Python writes 1e-05 where to_chars writes 1e-05 too; positive exponents get a '+' in Python
    const auto e = s.find('e');
    if (e != std::string::npos && s[e + 1] != '-' && s[e + 1] != '+')
        s.insert(e + 1, "+");
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::ifstream in(argv[1]);
    std::string line;
    std::getline(in, line);
    std::vector<unsigned long long> seeds;
    {
        std::istringstream s(line);
        unsigned long long x;
        while (s >> x)
            seeds.push_back(x);
    }
    std::vector<std::vector<std::string>> ops;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::vector<std::string> o;
        std::string w;
        while (s >> w)
            o.push_back(w);
        if (!o.empty())
            ops.push_back(o);
    }
    std::string out;
    for (unsigned long long seed : seeds) {
        grid::Generator g(seed);
        out += "seed " + std::to_string(seed) + " state " + u128_dec(g.bit.state) + " inc " + u128_dec(g.bit.inc) + "\n";
        for (const auto &o : ops) {
            if (o[0] == "c") {
                const auto v = g.integers(std::stoll(o[1]), std::stoll(o[2]), std::stoull(o[3]));
                for (size_t i = 0; i < v.size(); ++i)
                    out += (i ? " " : "") + std::to_string(v[i]);
                out += "\n";
            } else if (o[0] == "n") {
                out += repr(g.normal(std::stod(o[1]), std::stod(o[2]))) + "\n";
            } else if (o[0] == "i") {
                out += std::to_string(g.integers(std::stoll(o[1]), std::stoll(o[2]))) + "\n";
            } else {
                out += repr(g.random()) + "\n";
            }
        }
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
