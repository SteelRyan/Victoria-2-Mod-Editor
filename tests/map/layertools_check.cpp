// layertools + grid/npmath against numpy, the C++ side of tools/check_layertools.py.  Usage: med_layertools <in.txt>
#include "../core/nopopup.h"
#include "grid/npmath.h"
#include "map/layertools.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string hex32(float f)
{
    unsigned char b[4];
    std::memcpy(b, &f, 4);
    char s[9];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
    return s;
}
static std::string hex64(double d)
{
    unsigned char b[8];
    std::memcpy(b, &d, 8);
    std::string s;
    char t[3];
    for (unsigned char c : b) {
        std::snprintf(t, sizeof t, "%02x", c);
        s += t;
    }
    return s;
}
static float unhex32(const std::string &s)
{
    unsigned char b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = static_cast<unsigned char>(std::stoul(s.substr(i * 2, 2), nullptr, 16));
    float f;
    std::memcpy(&f, b, 4);
    return f;
}
static double unhex64(const std::string &s)
{
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = static_cast<unsigned char>(std::stoul(s.substr(i * 2, 2), nullptr, 16));
    double d;
    std::memcpy(&d, b, 8);
    return d;
}
template <typename T> static std::string join(const std::vector<T> &v, std::string (*f)(T))
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
        out += (i ? " " : "") + f(v[i]);
    return out;
}
static std::string i2s(std::int64_t v) { return std::to_string(v); }
static std::string i32s(std::int32_t v) { return std::to_string(v); }

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::ifstream in(argv[1]);
    std::string line, out;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string kind;
        s >> kind;
        if (kind == "noise") {
            long long n, seed;
            std::string sc;
            s >> n >> sc >> seed;
            const double scale = std::strtod(sc.c_str(), nullptr);
            std::vector<std::int64_t> ys(n), xs(n);
            for (auto &v : ys)
                s >> v;
            for (auto &v : xs)
                s >> v;
            out += "v " + join<float>(layertools::value_noise(ys, xs, scale, seed), hex32) + "\n";
            out += "p " + join<float>(layertools::patch_noise(ys, xs, seed, scale), hex32) + "\n";
        } else if (kind == "depth") {
            long long H, W;
            s >> H >> W;
            grid::Grid<std::uint8_t> mask(H, W);
            for (auto &v : mask.flat()) {
                int x;
                s >> x;
                v = static_cast<std::uint8_t>(x);
            }
            const auto d = layertools::depth(mask);
            out += "d " + join<std::int32_t>(d.flat(), i32s) + "\n";
            std::vector<std::int64_t> gy, gx;
            for (long long y = 0; y < H; ++y)
                for (long long x = 0; x < W; ++x) {
                    gy.push_back(y);
                    gx.push_back(x);
                }
            const auto wob = layertools::patch_noise(gy, gx, 5, 14.0);
            for (long long n_ : {1, 2, 4, 7})
                for (int wi = 0; wi < 3; ++wi) {
                    const std::optional<long long> width = wi == 0 ? std::nullopt : std::optional<long long>(wi == 1 ? 1 : 3);
                    out += "r " + join<std::int64_t>(layertools::ring_levels(d.flat(), n_, width), i2s) + " | " +
                           join<std::int64_t>(layertools::ring_levels(d.flat(), n_, width, &wob), i2s) + "\n";
                }
        } else if (kind == "wl") {
            long long n;
            s >> n;
            std::vector<std::int64_t> ys(n), xs(n);
            for (auto &v : ys)
                s >> v;
            for (auto &v : xs)
                s >> v;
            std::string bar;
            s >> bar;
            std::vector<double> w;
            std::string t;
            while (s >> t)
                w.push_back(std::strtod(t.c_str(), nullptr));
            out += "w " + join<std::int64_t>(layertools::weighted_levels(layertools::patch_noise(ys, xs, 11, 6.0), w), i2s) + "\n";
        } else if (kind == "line") {
            long long a, b, c, d;
            s >> a >> b >> c >> d;
            std::string l;
            for (const auto &[x, y] : layertools::line4(a, b, c, d))
                l += (l.empty() ? "" : " ") + std::to_string(x) + "," + std::to_string(y);
            out += "l " + l + "\n";
        } else if (kind == "disc") {
            std::string a, b, c;
            s >> a >> b >> c;
            out += "c " + join<std::int64_t>(layertools::disc(std::strtod(a.c_str(), nullptr), std::strtod(b.c_str(), nullptr),
                                                               std::strtod(c.c_str(), nullptr), 37, 28), i2s) + "\n";
        } else if (kind == "sum") {
            long long n;
            s >> n;
            std::vector<double> a(n);
            for (auto &v : a) {
                std::string t;
                s >> t;
                v = unhex64(t);
            }
            out += "s " + hex64(grid::np_sum(a)) + "\n";
        } else if (kind == "q") {
            long long n;
            s >> n;
            std::vector<float> a32(n);
            std::vector<double> a64(n), qs;
            std::string t;
            for (auto &v : a32) {
                s >> t;
                v = unhex32(t);
            }
            s >> t;
            for (auto &v : a64) {
                s >> t;
                v = unhex64(t);
            }
            s >> t;
            while (s >> t)
                qs.push_back(unhex64(t));
            out += "q " + join<double>(grid::np_quantile(a32, qs), hex64) + " | " + join<double>(grid::np_quantile(a64, qs), hex64) + "\n";
        }
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
