// Map → Draw → Colours: the land colour map tinted around chosen provinces (C++ only). See colormap.h.
#include "map/colormap.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace colormap {

const QString REL = u"map/terrain/colormap.dds"_s;
const QString WATER_REL = u"map/terrain/colormap_water.dds"_s;

std::vector<std::uint8_t> chosen_mask(const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &chosen,
                                      const Stretch &s)
{
    std::vector<std::uint8_t> m(static_cast<size_t>(s.cw * s.ch), 0);
    for (long long r = 0; r < s.ch; ++r) {
        // file row r stands for map rows counted from the south: its middle
        const long long y = std::min(s.H - 1, (long long)((double(s.ch - 1 - r) + 0.5) * s.sy()));
        for (long long c = 0; c < s.cw; ++c) {
            const long long x = std::min(s.W - 1, (long long)((double(c) + 0.5) * s.sx()));
            const std::uint16_t id = idmap(y, x);
            if (id < chosen.size() && chosen[id])
                m[size_t(r * s.cw + c)] = 1;
        }
    }
    return m;
}

// one line of the exact distance transform (Felzenszwalb & Huttenlocher), squared distances, spacing a
static void edt_line(const double *f, double *d, long long n, double a, std::vector<long long> &v, std::vector<double> &z)
{
    const double INF = std::numeric_limits<double>::infinity(), a2 = a * a;
    long long k = -1;
    for (long long q = 0; q < n; ++q) {
        if (!std::isfinite(f[q]))
            continue;
        while (k >= 0) {
            const long long p = v[size_t(k)];
            const double s = ((f[q] + a2 * double(q) * double(q)) - (f[p] + a2 * double(p) * double(p))) /
                             (2.0 * a2 * double(q - p));
            if (s <= z[size_t(k)]) {
                --k;
                continue;
            }
            ++k;
            v[size_t(k)] = q;
            z[size_t(k)] = s;
            z[size_t(k) + 1] = INF;
            break;
        }
        if (k < 0) {
            k = 0;
            v[0] = q;
            z[0] = -INF;
            z[1] = INF;
        }
    }
    if (k < 0) {
        std::fill(d, d + n, INF);
        return;
    }
    long long j = 0;
    for (long long q = 0; q < n; ++q) {
        while (z[size_t(j) + 1] < double(q))
            ++j;
        const long long p = v[size_t(j)];
        d[q] = a2 * double(q - p) * double(q - p) + f[p];
    }
}

std::vector<float> distance(const std::vector<std::uint8_t> &mask, const Stretch &s)
{
    const long long w = s.cw, h = s.ch;
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> g(static_cast<size_t>(w * h));
    const size_t n = static_cast<size_t>(std::max(w, h));
    std::vector<double> f(n), d(n), z(n + 1);
    std::vector<long long> v(n);
    for (long long x = 0; x < w; ++x) {               // down the columns (map px per picture row: sy)
        for (long long y = 0; y < h; ++y)
            f[size_t(y)] = mask[size_t(y * w + x)] ? 0.0 : INF;
        edt_line(f.data(), d.data(), h, s.sy(), v, z);
        for (long long y = 0; y < h; ++y)
            g[size_t(y * w + x)] = d[size_t(y)];
    }
    std::vector<float> out(size_t(w * h));
    for (long long y = 0; y < h; ++y) {               // then along the rows (sx)
        edt_line(g.data() + y * w, d.data(), w, s.sx(), v, z);
        for (long long x = 0; x < w; ++x)
            out[size_t(y * w + x)] = std::isfinite(d[size_t(x)]) ? float(std::sqrt(d[size_t(x)]))
                                                                  : std::numeric_limits<float>::max();
    }
    return out;
}

float fade(float d, double reach)
{
    if (d <= 0.0f)
        return 1.0f;
    if (reach <= 0.0 || double(d) >= reach)
        return 0.0f;
    const double t = 1.0 - double(d) / reach;        // 1 at the edge of the provinces, 0 at the reach
    return float(t * t * (3.0 - 2.0 * t));           // (smooth at both ends)
}

float brush_fade(float d, double r)
{
    if (r <= 0.0 || double(d) >= r)
        return 0.0f;
    const double t = 1.0 - double(d) / r;
    return float(t * t * (3.0 - 2.0 * t));
}

double mean_luma(const QImage &pic, const std::vector<float> &w)
{
    const long long W = pic.width(), H = pic.height();
    if (w.size() != size_t(W * H) || pic.format() != QImage::Format_RGBA8888)
        return 0.0;
    double ref = 0, ws = 0;
    for (long long y = 0; y < H; ++y) {
        const uchar *p = pic.constScanLine(int(y));
        for (long long x = 0; x < W; ++x)
            if (const float a = w[size_t(y * W + x)]; a > 0.0f) {
                ref += a * luma(p[4 * x], p[4 * x + 1], p[4 * x + 2]);
                ws += a;
            }
    }
    return ws > 0 ? ref / ws : 0.0;
}

void tint_into(QImage &out, const QImage &base, const std::vector<float> &w, const Tint &t, double ref, long long c0,
               long long r0, long long c1, long long r1)
{
    const long long W = base.width(), H = base.height();
    if (w.size() != size_t(W * H) || out.size() != base.size())
        return;
    c0 = std::max(0LL, c0), r0 = std::max(0LL, r0), c1 = std::min(W, c1), r1 = std::min(H, r1);
    for (long long y = r0; y < r1; ++y) {
        const uchar *s = base.constScanLine(int(y));
        uchar *o = out.scanLine(int(y));
        for (long long x = c0; x < c1; ++x) {
            const uchar *px = s + 4 * x;
            uchar *ox = o + 4 * x;
            const float a0 = w[size_t(y * W + x)];
            if (a0 <= 0.0f) {
                std::copy_n(px, 4, ox);
                continue;
            }
            const double a = std::clamp(double(a0) * t.strength, 0.0, 1.0);
            const double lift = t.keep_shading ? luma(px[0], px[1], px[2]) - ref : 0.0;
            for (int c = 0; c < 3; ++c) {
                const double tgt = std::clamp(double(t.rgb[size_t(c)]) + lift, 0.0, 255.0);
                const double nv = double(px[c]) + a * (tgt - double(px[c]));
                ox[c] = uchar(std::clamp(std::lround(nv), 0L, 255L));
            }
            ox[3] = px[3];
        }
    }
}

QImage tinted(const QImage &pic0, const std::vector<float> &w, const Tint &t)
{
    const QImage pic = pic0.convertToFormat(QImage::Format_RGBA8888);
    QImage out = pic.copy();
    tint_into(out, pic, w, t, t.keep_shading ? mean_luma(pic, w) : 0.0, 0, 0, pic.width(), pic.height());
    return out;
}

}  // namespace colormap
