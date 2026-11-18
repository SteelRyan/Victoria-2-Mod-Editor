// layertools.py — noise, ring depth, lines, discs. Line by line; numpy's float32 steps in float.
#include "map/layertools.h"
#include "grid/npmath.h"

#include <algorithm>
#include <cmath>

namespace layertools {

std::vector<float> _hash01(const std::vector<std::int64_t> &gx, const std::vector<std::int64_t> &gy, long long seed)
{
    // A repeatable pseudo-random value in [0, 1) per integer grid point.
    const std::uint32_t s = static_cast<std::uint32_t>((static_cast<std::uint64_t>(seed) * 83492791ULL) & 0xFFFFFFFFULL);
    std::vector<float> out(gx.size());
    for (size_t i = 0; i < gx.size(); ++i) {
        std::uint32_t h = (static_cast<std::uint32_t>(gx[i]) * 73856093u) ^ (static_cast<std::uint32_t>(gy[i]) * 19349663u) ^ s;
        h ^= h >> 13;
        h *= 0x5BD1E995u;
        h ^= h >> 15;
        out[i] = static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
    }
    return out;
}

std::vector<float> value_noise(const std::vector<std::int64_t> &ys, const std::vector<std::int64_t> &xs, double scale,
                               long long seed)
{
    // Smooth noise in [0, 1) at map pixels (ys, xs): the same pixel always gets the same value for a seed.
    const float sc = static_cast<float>(scale);
    const size_t n = xs.size();
    std::vector<std::int64_t> x0(n), y0(n), x1(n), y1(n);
    std::vector<float> tx(n), ty(n);
    for (size_t i = 0; i < n; ++i) {
        const float fx = static_cast<float>(xs[i]) / sc, fy = static_cast<float>(ys[i]) / sc;
        const float fx0 = std::floor(fx), fy0 = std::floor(fy);
        float a = fx - fx0, b = fy - fy0;
        a = a * a * (3.0f - 2.0f * a);
        b = b * b * (3.0f - 2.0f * b);
        tx[i] = a;
        ty[i] = b;
        x0[i] = static_cast<std::int64_t>(fx0);
        y0[i] = static_cast<std::int64_t>(fy0);
        x1[i] = x0[i] + 1;
        y1[i] = y0[i] + 1;
    }
    const auto v00 = _hash01(x0, y0, seed), v10 = _hash01(x1, y0, seed);
    const auto v01 = _hash01(x0, y1, seed), v11 = _hash01(x1, y1, seed);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = (v00[i] * (1.0f - tx[i]) + v10[i] * tx[i]) * (1.0f - ty[i]) + (v01[i] * (1.0f - tx[i]) + v11[i] * tx[i]) * ty[i];
    return out;
}

std::vector<float> patch_noise(const std::vector<std::int64_t> &ys, const std::vector<std::int64_t> &xs, long long seed,
                               double scale)
{
    // Blotchy noise for the "patches" terrain style (two octaves).
    const auto a = value_noise(ys, xs, scale, seed), b = value_noise(ys, xs, scale / 2.6, seed + 7);
    std::vector<float> out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = 0.65f * a[i] + 0.35f * b[i];
    return out;
}

grid::Grid<std::int32_t> depth(const grid::Grid<std::uint8_t> &mask)
{
    // Distance of every pixel of `mask` from its edge in pixels (1 = on the edge), octagonal (alternating 4- and
    // 8-neighbour erosion). Pixels outside the array count as outside.
    const auto H = mask.h(), W = mask.w();
    grid::Grid<std::int32_t> d(H, W, 0);
    grid::Grid<std::uint8_t> cur = mask;
    std::int32_t k = 0;
    auto any = [&]() { return std::any_of(cur.flat().begin(), cur.flat().end(), [](std::uint8_t v) { return v != 0; }); };
    while (any()) {
        ++k;
        for (size_t i = 0; i < cur.flat().size(); ++i)
            if (cur.flat()[i])
                d.flat()[i] = k;
        grid::Grid<std::uint8_t> e = cur;
        for (std::ptrdiff_t y = 0; y < H; ++y)
            for (std::ptrdiff_t x = 0; x < W; ++x) {
                if (y == 0 || y == H - 1 || x == 0 || x == W - 1) {
                    e(y, x) = 0;
                    continue;
                }
                bool v = e(y, x) && cur(y - 1, x) && cur(y + 1, x) && cur(y, x - 1) && cur(y, x + 1);
                if (k % 2 == 0)
                    v = v && cur(y - 1, x - 1) && cur(y - 1, x + 1) && cur(y + 1, x - 1) && cur(y + 1, x + 1);
                e(y, x) = v ? 1 : 0;
            }
        cur = std::move(e);
    }
    return d;
}

std::vector<std::int64_t> ring_levels(const std::vector<std::int32_t> &d, long long n, std::optional<long long> width,
                                      const std::vector<float> *wobble)
{
    // Level (0 = outside … n-1 = centre) per depth value; width None = spread the n levels over the deepest point.
    const long long dmax = d.empty() ? 0 : *std::max_element(d.begin(), d.end());
    long long wd = width ? *width
                         : std::max<long long>(1, static_cast<long long>(std::ceil(static_cast<double>(dmax) /
                                                                                  static_cast<double>(std::max<long long>(1, n)))));
    const float wf = static_cast<float>(wd);
    std::vector<std::int64_t> out(d.size());
    for (size_t i = 0; i < d.size(); ++i) {
        float dd = static_cast<float>(d[i]) - 1.0f;
        if (wobble) {
            dd = dd + ((*wobble)[i] - 0.5f) * 0.9f * wf;
            if (d[i] == 1)
                dd = std::min(dd, static_cast<float>(static_cast<double>(wd) - 0.5));   // the outermost row stays outer
        }
        float lv = std::floor(dd / wf);
        lv = std::clamp(lv, 0.0f, static_cast<float>(n - 1));
        out[i] = static_cast<std::int64_t>(lv);
    }
    return out;
}

std::vector<std::int64_t> weighted_levels(const std::vector<float> &values, const std::vector<double> &weights)
{
    // Pick a level per value so that the levels get (about) the given shares: cuts at weighted quantiles.
    std::vector<double> w = weights;
    const double s = grid::np_sum(w);
    if (s > 0)
        for (double &x : w)
            x = x / s;
    else
        for (double &x : w)
            x = 1.0 / static_cast<double>(w.size());
    if (values.empty())
        return {};
    std::vector<double> cuts;
    if (w.size() > 1) {
        std::vector<double> cs = grid::np_cumsum(w);
        cs.pop_back();
        for (double &x : cs)
            x = std::clamp(x, 0.0, 1.0);
        cuts = grid::np_quantile(values, cs);
    }
    std::vector<std::int64_t> out(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        out[i] = grid::searchsorted_right(cuts, static_cast<double>(values[i]));
    return out;
}

std::vector<std::pair<long long, long long>> line4(long long x0, long long y0, long long x1, long long y1)
{
    // Pixels of a 4-connected line (every step moves one pixel horizontally OR vertically).
    std::vector<std::pair<long long, long long>> pts = {{x0, y0}};
    const long long dx = std::llabs(x1 - x0), dy = std::llabs(y1 - y0);
    const long long sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    long long x = x0, y = y0, ix = 0, iy = 0;
    while (ix < dx || iy < dy) {
        if (dy == 0 || (dx && (0.5 + static_cast<double>(ix)) / static_cast<double>(dx) <
                                  (0.5 + static_cast<double>(iy)) / static_cast<double>(dy))) {
            x += sx;
            ++ix;
        } else {
            y += sy;
            ++iy;
        }
        pts.emplace_back(x, y);
    }
    return pts;
}

std::vector<std::int64_t> disc(double x, double y, double size, long long w, long long h)
{
    // Flat pixel indexes of a brush disc of diameter `size` around map point (x, y).
    const double r = std::max(0.5, size / 2.0);
    const long long R = static_cast<long long>(std::ceil(r));
    const long long cx = static_cast<long long>(std::floor(x)), cy = static_cast<long long>(std::floor(y));
    std::vector<std::int64_t> out;
    for (long long dy = -R; dy <= R; ++dy)
        for (long long dx = -R; dx <= R; ++dx) {
            const double fx = static_cast<double>(dx) + 0.0, fy = static_cast<double>(dy) + 0.0;
            if (!(fx * fx + fy * fy <= r * r + 0.25))
                continue;
            const long long ys = cy + dy, xs = cx + dx;
            if (ys >= 0 && ys < h && xs >= 0 && xs < w)
                out.push_back(ys * w + xs);
        }
    return out;
}

}  // namespace layertools
