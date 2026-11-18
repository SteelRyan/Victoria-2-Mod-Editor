// positions.py, the placement half. Line by line; numpy's array steps written out in the same arithmetic order.
#include "map/positions_geo.h"
#include "core/pynum.h"
#include "grid/npmath.h"

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace positions {

namespace {
constexpr double PI = std::numbers::pi;

// np.nonzero(M) of a mask: (ys, xs), row-major
void nonzero(const Mask &M, std::vector<long long> &ys, std::vector<long long> &xs)
{
    ys.clear();
    xs.clear();
    for (long long y = 0; y < M.h(); ++y)
        for (long long x = 0; x < M.w(); ++x)
            if (M(y, x)) {
                ys.push_back(y);
                xs.push_back(x);
            }
}
// .mean() of an int array: the float64 sum is exact here (integers far below 2**53)
double mean_int(const std::vector<long long> &v)
{
    long long s = 0;
    for (long long x : v)
        s += x;
    return static_cast<double>(s) / static_cast<double>(v.size());
}
bool any(const Mask &m)
{
    for (std::uint8_t b : m.flat())
        if (b)
            return true;
    return false;
}
}  // namespace

// ---------------------------------------------------------------------------------------------- geometry
Mask _erode(const Mask &m, bool eight)
{
    const long long h = m.h(), w = m.w();
    Mask e = m;
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            if (!e(y, x))
                continue;
            bool v = (y == 0 || m(y - 1, x)) && (y + 1 >= h || m(y + 1, x)) && (x == 0 || m(y, x - 1)) &&
                     (x + 1 >= w || m(y, x + 1));
            if (eight)
                v = v && (y == 0 || x == 0 || m(y - 1, x - 1)) && (y == 0 || x + 1 >= w || m(y - 1, x + 1)) &&
                    (y + 1 >= h || x == 0 || m(y + 1, x - 1)) && (y + 1 >= h || x + 1 >= w || m(y + 1, x + 1));
            e(y, x) = v;
        }
    return e;
}

grid::Grid<float> dist_map(const Mask &mask)
{
    // Approximate distance to the outside (octagonal: alternating 4 / 8-neighbour erosion).
    grid::Grid<float> d(mask.h(), mask.w(), 0.0f);
    Mask cur = mask;
    long long k = 0;
    while (any(cur)) {
        for (size_t i = 0; i < cur.flat().size(); ++i)
            if (cur.flat()[i])
                d.flat()[i] += 1;
        cur = _erode(cur, k % 2 == 1);
        k += 1;
    }
    return d;
}

grid::Grid<std::int32_t> _run_right(const Mask &R)
{
    // how many set cells in a row from each cell to the right (the cumsum / maximum.accumulate trick, as counts)
    grid::Grid<std::int32_t> out(R.h(), R.w(), 0);
    for (long long y = 0; y < R.h(); ++y)
        for (long long x = R.w() - 1; x >= 0; --x)
            out(y, x) = R(y, x) ? 1 + (x + 1 < R.w() ? out(y, x + 1) : 0) : 0;
    return out;
}

grid::Grid<std::int32_t> _run_down(const Mask &R)
{
    grid::Grid<std::int32_t> out(R.h(), R.w(), 0);
    for (long long y = R.h() - 1; y >= 0; --y)
        for (long long x = 0; x < R.w(); ++x)
            out(y, x) = R(y, x) ? 1 + (y + 1 < R.h() ? out(y + 1, x) : 0) : 0;
    return out;
}

std::array<double, 4> cov2(const std::vector<double> &a, const std::vector<double> &b)
{
    // np.cov(np.vstack([a, b])): rows less their mean (numpy's pairwise sum), then X @ X.T through BLAS, times
    // 1 / (N - 1). BLAS's sum order belongs to the CPU's kernel (Python's own result differs between machines):
    // here a sequential FMA sum in blocks of 512, which met numpy's on 82 % of the provinces measured; the placed
    // names did not change on any of 3,655 provinces with a plain sum instead (2026-10-20).
    const size_t n = a.size();
    std::vector<double> X[2] = {a, b};
    for (auto &r : X) {
        const double avg = grid::np_sum(r) / static_cast<double>(n);
        for (double &x : r)
            x -= avg;
    }
    const double f = 1.0 / static_cast<double>(n - 1);
    auto dot = [&](const std::vector<double> &p, const std::vector<double> &q) {
        double tot = 0.0;
        for (size_t s = 0; s < n; s += 512) {
            double acc = 0.0;
            for (size_t k = s; k < std::min(n, s + 512); ++k)
                acc = std::fma(p[k], q[k], acc);
            tot = tot + acc;
        }
        return tot;
    };
    const double c00 = dot(X[0], X[0]), c01 = dot(X[0], X[1]), c11 = dot(X[1], X[1]);
    return {c00 * f, c01 * f, c01 * f, c11 * f};
}

namespace {
// LAPACK's dlaev2: eigen decomposition of [[a, b], [b, c]] — rt1 the eigenvalue of larger absolute value,
// (cs1, sn1) its unit eigenvector
void dlaev2(double a, double b, double c, double &rt1, double &rt2, double &cs1, double &sn1)
{
    const double sm = a + c, df = a - c, adf = std::fabs(df), tb = b + b, ab = std::fabs(tb);
    double acmx, acmn;
    if (std::fabs(a) > std::fabs(c)) {
        acmx = a;
        acmn = c;
    } else {
        acmx = c;
        acmn = a;
    }
    double rt;
    if (adf > ab)
        rt = adf * std::sqrt(1.0 + (ab / adf) * (ab / adf));
    else if (adf < ab)
        rt = ab * std::sqrt(1.0 + (adf / ab) * (adf / ab));
    else
        rt = ab * std::sqrt(2.0);
    int sgn1, sgn2;
    if (sm < 0.0) {
        rt1 = 0.5 * (sm - rt);
        sgn1 = -1;
        rt2 = (acmx / rt1) * acmn - (b / rt1) * b;
    } else if (sm > 0.0) {
        rt1 = 0.5 * (sm + rt);
        sgn1 = 1;
        rt2 = (acmx / rt1) * acmn - (b / rt1) * b;
    } else {
        rt1 = 0.5 * rt;
        rt2 = -0.5 * rt;
        sgn1 = 1;
    }
    double cs;
    if (df >= 0.0) {
        cs = df + rt;
        sgn2 = 1;
    } else {
        cs = df - rt;
        sgn2 = -1;
    }
    if (std::fabs(cs) > ab) {
        const double ct = -tb / cs;
        sn1 = 1.0 / std::sqrt(1.0 + ct * ct);
        cs1 = ct * sn1;
    } else if (ab == 0.0) {
        cs1 = 1.0;
        sn1 = 0.0;
    } else {
        const double tn = -cs / tb;
        cs1 = 1.0 / std::sqrt(1.0 + tn * tn);
        sn1 = tn * cs1;
    }
    if (sgn1 == sgn2) {
        const double tn = cs1;
        cs1 = -sn1;
        sn1 = tn;
    }
}
}  // namespace

void eigh2(const std::array<double, 4> &A, std::array<double, 2> &w, std::array<double, 4> &v)
{
    // np.linalg.eigh (LAPACK dsyevd, lower) of a symmetric 2x2: the tridiagonal form is the matrix itself, dsteqr
    // splits it when the off-diagonal is negligible, else solves the block with dlaev2; eigenvalues sorted
    // ascending. Checked against numpy on 10,952 provinces' covariances, bit for bit (2026-10-20).
    constexpr double EPS = 0x1p-53, EPS2 = EPS * EPS, SAFMIN = 2.2250738585072014e-308;
    double d0 = A[0], d1 = A[3];
    const double e = A[2];
    double z00 = 1, z01 = 0, z10 = 0, z11 = 1;      // z[row][col]
    const double tst = std::fabs(e);
    const bool split = tst == 0.0 || tst <= (std::sqrt(std::fabs(d0)) * std::sqrt(std::fabs(d1))) * EPS;
    if (!split) {
        const double small = std::fabs(d1) < std::fabs(d0) ? (EPS2 * std::fabs(d1)) * std::fabs(d0) + SAFMIN
                                                           : (EPS2 * std::fabs(d0)) * std::fabs(d1) + SAFMIN;
        if (!(tst * tst <= small)) {
            double rt1, rt2, c, s;
            dlaev2(d0, e, d1, rt1, rt2, c, s);
            z00 = c;
            z01 = -s;
            z10 = s;
            z11 = c;
            d0 = rt1;
            d1 = rt2;
        }
    }
    if (d1 < d0) {
        std::swap(d0, d1);
        std::swap(z00, z01);
        std::swap(z10, z11);
    }
    w = {d0, d1};
    v = {z00, z01, z10, z11};
}

// ---------------------------------------------------------------------------------------------- PositionPlacer
PositionPlacer::PositionPlacer(const mapview::MapData &md_, std::vector<std::uint8_t> sv) : md(md_), sea_valid(std::move(sv)) {}

PositionPlacer::Crop PositionPlacer::crop(long long pid, long long pad) const
{
    const long long x0 = std::max<long long>(0, md.x0[pid] - pad), y0 = std::max<long long>(0, md.y0[pid] - pad);
    const long long x1 = std::min<long long>(md.w, md.x1[pid] + 1 + pad), y1 = std::min<long long>(md.h, md.y1[pid] + 1 + pad);
    Crop c{x0, y0, grid::Grid<std::uint16_t>(std::max<long long>(0, y1 - y0), std::max<long long>(0, x1 - x0))};
    for (long long y = 0; y < c.sub.h(); ++y)
        for (long long x = 0; x < c.sub.w(); ++x)
            c.sub(y, x) = md.idmap(y0 + y, x0 + x);
    return c;
}

std::shared_ptr<const PositionPlacer::Info> PositionPlacer::info(long long pid)
{
    if (auto it = _cache.find(pid); it != _cache.end())
        return it->second;
    Crop c = crop(pid);
    Mask M(c.sub.h(), c.sub.w(), 0);
    for (size_t i = 0; i < M.flat().size(); ++i)
        M.flat()[i] = c.sub.flat()[i] == pid;
    auto inf = std::make_shared<const Info>(Info{c.x0, c.y0, std::move(c.sub), M, dist_map(M)});
    if (_cache.size() > 400)
        _cache.clear();
    _cache[pid] = inf;
    return inf;
}

long long name_len(const QString &name)
{
    return std::max<long long>(1, py::strip(name).toUcs4().size());
}

// --- name
std::optional<NameFit> PositionPlacer::fit_name(long long pid, const QString &name, bool rotate)
{
    const auto inf = info(pid);
    const Info &I = *inf;
    std::vector<long long> ys, xs;
    nonzero(I.M, ys, xs);
    if (xs.empty())
        return std::nullopt;
    const long long n = name_len(name);
    size_t am = 0;                                  // most central pixel: the first largest D
    for (size_t i = 1; i < I.D.flat().size(); ++i)
        if (I.D.flat()[i] > I.D.flat()[am])
            am = i;
    const long long pr = static_cast<long long>(am) / I.D.w(), pcol = static_cast<long long>(am) % I.D.w();
    const XY pole{static_cast<double>(pcol) + 0.5, static_cast<double>(pr) + 0.5};
    std::vector<double> angles = {0.0};
    if (rotate && xs.size() > 12) {
        const double cx = mean_int(xs), cy = mean_int(ys);
        std::vector<double> a(xs.size()), b(ys.size());
        for (size_t i = 0; i < xs.size(); ++i) {
            a[i] = static_cast<double>(xs[i]) - cx;
            b[i] = static_cast<double>(ys[i]) - cy;
        }
        std::array<double, 2> w;
        std::array<double, 4> v;
        eigh2(cov2(a, b), w, v);
        if (w[1] > 1.6 * std::max(w[0], 1e-6)) {
            double ang = std::atan2(v[3], v[1]);
            if (ang > PI / 2)
                ang -= PI;
            else if (ang <= -PI / 2)
                ang += PI;
            if (0.15 < std::fabs(ang) && std::fabs(ang) <= 1.25)
                angles.insert(angles.end(), {ang, ang - 0.2, ang + 0.2});
        }
    }
    std::optional<std::array<double, 4>> best;      // (cx, cy, scale, angle)
    for (double th : angles) {
        const auto r = _fit_at(I.M, xs, ys, pole, n, th);
        if (!r)
            continue;
        if (!best)
            best = std::array<double, 4>{(*r)[0], (*r)[1], (*r)[2], th};
        else if ((*best)[3] == 0.0 && th != 0.0) {
            if ((*r)[2] >= (*best)[2] * 1.25)       // tilt only when it fits clearly bigger
                best = std::array<double, 4>{(*r)[0], (*r)[1], (*r)[2], th};
        } else if ((*r)[2] > (*best)[2])
            best = std::array<double, 4>{(*r)[0], (*r)[1], (*r)[2], th};
    }
    if (!best)
        return NameFit{{pole.first + double(I.x0), pole.second + double(I.y0)}, 0.0, SCALE_MIN, false};
    return NameFit{{(*best)[0] + double(I.x0), (*best)[1] + double(I.y0)}, (*best)[3], (*best)[2], true};
}

std::optional<std::array<double, 3>> PositionPlacer::_fit_at(const Mask &M, const std::vector<long long> &xs,
                                                             const std::vector<long long> &ys, const XY &pole,
                                                             long long n, double th) const
{
    const double c = std::cos(th), s = std::sin(th);
    double umin = 0, vmin = 0, umax = 0, vmax = 0;
    for (size_t i = 0; i < xs.size(); ++i) {
        const double px = static_cast<double>(xs[i]) + 0.5, py_ = static_cast<double>(ys[i]) + 0.5;
        const double u = px * c + py_ * s, v = -px * s + py_ * c;
        if (i == 0 || u < umin)
            umin = u;
        if (i == 0 || u > umax)
            umax = u;
        if (i == 0 || v < vmin)
            vmin = v;
        if (i == 0 || v > vmax)
            vmax = v;
    }
    umin -= 1;
    vmin -= 1;
    const long long W = static_cast<long long>(std::ceil(umax - umin)) + 2;
    const long long Hh = static_cast<long long>(std::ceil(vmax - vmin)) + 2;
    Mask R(Hh, W, 0);
    for (long long j = 0; j < Hh; ++j) {
        const double gv = (static_cast<double>(j) + vmin) + 0.5;
        for (long long i = 0; i < W; ++i) {
            const double gu = (static_cast<double>(i) + umin) + 0.5;
            const double X = gu * c - gv * s, Y = gu * s + gv * c;
            const double fx = std::floor(X), fy = std::floor(Y);
            if (fx >= 0 && fy >= 0 && fx < double(M.w()) && fy < double(M.h()))
                R(j, i) = M(static_cast<long long>(fy), static_cast<long long>(fx));
        }
    }
    const auto runr = _run_right(R);
    const double pu = pole.first * c + pole.second * s - umin;
    const double pv = -pole.first * s + pole.second * c - vmin;

    auto place = [&](double sc) -> std::optional<XY> {
        const long long ww = static_cast<long long>(std::ceil(CHAR_W * double(n) * sc + 2));
        const long long hh = static_cast<long long>(std::ceil(TEXT_H * sc + 2));
        if (ww > W || hh > Hh)
            return std::nullopt;
        Mask okr(Hh, W, 0);
        for (size_t k = 0; k < okr.flat().size(); ++k)
            okr.flat()[k] = runr.flat()[k] >= ww;
        const auto rd = _run_down(okr);
        std::optional<XY> got;
        double bv = 0;
        for (long long y = 0; y < Hh; ++y)
            for (long long x = 0; x < W; ++x)
                if (rd(y, x) >= hh) {
                    const double cu = static_cast<double>(x) + double(ww) / 2.0, cv = static_cast<double>(y) + double(hh) / 2.0;
                    const double d = (cu - pu) * (cu - pu) + (cv - pv) * (cv - pv);
                    if (!got || d < bv) {
                        got = XY{cu, cv};
                        bv = d;
                    }
                }
        return got;
    };

    double lo = SCALE_MIN, hi = SCALE_MAX;
    if (!place(lo))
        return std::nullopt;
    for (int k = 0; k < 12; ++k) {
        const double mid = (lo + hi) / 2;
        if (place(mid))
            lo = mid;
        else
            hi = mid;
    }
    const XY cuv = *place(lo);
    const double uu = cuv.first + umin, vv = cuv.second + vmin;
    return std::array<double, 3>{uu * c - vv * s, uu * s + vv * c, lo};
}

// --- name box
NameBox PositionPlacer::name_box(const XY &centre, double th, double scale, const QString &name)
{
    return {centre, th, CHAR_W * double(name_len(name)) * scale, TEXT_H * scale};
}

static std::vector<double> linspace(double start, double stop, long long num)
{
    // np.linspace(start, stop, num): i * step + start, the last one exactly stop
    std::vector<double> y(static_cast<size_t>(num));
    const double div = double(num - 1), delta = stop - start, step = delta / div;
    for (long long i = 0; i < num; ++i)
        y[i] = step == 0 ? (double(i) / div) * delta + start : double(i) * step + start;
    if (num > 1)
        y[num - 1] = stop;
    return y;
}

double PositionPlacer::name_clips(long long pid, const XY &centre, double th, double scale, const QString &name) const
{
    // Fraction of the name box that lies outside the province.
    const NameBox b = name_box(centre, th, scale, name);
    const auto us = linspace(-b.w / 2, b.w / 2, std::max<long long>(3, py::trunc_int(b.w) + 1));
    const auto vs = linspace(-b.h / 2, b.h / 2, std::max<long long>(3, py::trunc_int(b.h) + 1));
    const double c = std::cos(b.th), s = std::sin(b.th);
    long long out = 0;
    for (double gv : vs)
        for (double gu : us) {
            const double X = b.centre.first + gu * c - gv * s, Y = b.centre.second + gu * s + gv * c;
            const long long xi = std::clamp<long long>(static_cast<long long>(std::floor(X)), 0, md.w - 1);
            const long long yi = std::clamp<long long>(static_cast<long long>(std::floor(Y)), 0, md.h - 1);
            out += md.idmap(yi, xi) != pid;
        }
    return static_cast<double>(out) / static_cast<double>(us.size() * vs.size());
}

// --- unit + buildings
OrderedMap<QString, XY> PositionPlacer::place_points(long long pid, const std::optional<NameBox> &nb)
{
    // -> {kind: display point} for unit and the land buildings.
    const auto inf = info(pid);
    const Info &I = *inf;
    const Mask &M = I.M;
    const auto &D = I.D;
    std::vector<long long> ys, xs;
    nonzero(M, ys, xs);
    if (xs.empty())
        return {};
    const double cy = mean_int(ys), cx = mean_int(xs);
    const long long h = M.h(), w = M.w();
    Mask box(h, w, 0);
    if (nb) {
        const double c = std::cos(nb->th), s = std::sin(nb->th);
        for (long long gy = 0; gy < h; ++gy)
            for (long long gx = 0; gx < w; ++gx) {
                const double px = (double(gx) + 0.5 + double(I.x0)) - nb->centre.first;
                const double py_ = (double(gy) + 0.5 + double(I.y0)) - nb->centre.second;
                const double u = px * c + py_ * s, v = -px * s + py_ * c;
                box(gy, gx) = std::fabs(u) <= nb->w / 2 + 1.5 && std::fabs(v) <= nb->h / 2 + 1.5;
            }
    }
    float dmaxf = D.flat().empty() ? 0.0f : D.flat()[0];
    for (float x : D.flat())
        dmaxf = std::max(dmaxf, x);
    const double dmax = dmaxf;
    const double thr = std::min(3.0, 0.5 * dmax);
    Mask freem(h, w, 0);
    bool any_free = false;
    for (size_t i = 0; i < M.flat().size(); ++i) {
        freem.flat()[i] = M.flat()[i] && !box.flat()[i] && double(D.flat()[i]) >= thr;
        any_free = any_free || freem.flat()[i];
    }
    const Mask &pick = any_free ? freem : M;
    long long ux = 0, uy = 0;
    double bs = 0;
    bool first = true;
    for (long long gy = 0; gy < h; ++gy)
        for (long long gx = 0; gx < w; ++gx) {
            const double central = double(D(gy, gx)) - 0.08 * std::sqrt((double(gy) - cy) * (double(gy) - cy) +
                                                                         (double(gx) - cx) * (double(gx) - cx));
            const double sc = pick(gy, gx) ? central : -1e9;
            if (first || sc > bs) {
                bs = sc;
                ux = gx;
                uy = gy;
                first = false;
            }
        }
    OrderedMap<QString, XY> out;
    out.insert(u"unit"_s, {double(ux) + 0.5 + double(I.x0), double(uy) + 0.5 + double(I.y0)});
    std::vector<std::pair<long long, long long>> chosen = {{ux, uy}};
    const double need = std::min(2.0, dmax);
    for (const QString &kind : {u"fort"_s, u"railroad"_s, u"building_construction"_s, u"military_construction"_s, u"factory"_s}) {
        std::optional<std::pair<long long, long long>> got;
        for (const auto &[sep, avoid_box] : {std::pair{3.0, true}, {2.0, true}, {1.5, false}, {0.0, false}}) {
            Mask cand(h, w, 0);
            bool anyc = false;
            for (size_t i = 0; i < M.flat().size(); ++i) {
                cand.flat()[i] = M.flat()[i] && double(D.flat()[i]) >= need && (!avoid_box || !box.flat()[i]);
                anyc = anyc || cand.flat()[i];
            }
            if (!anyc)
                cand = M;
            for (long long gy = 0; gy < h; ++gy)
                for (long long gx = 0; gx < w; ++gx)
                    for (const auto &[ccx, ccy] : chosen)
                        if (cand(gy, gx) &&
                            !(double((gx - ccx) * (gx - ccx) + (gy - ccy) * (gy - ccy)) >= sep * sep))
                            cand(gy, gx) = 0;
            if (any(cand)) {
                long long bx = 0, by = 0;
                double bd = 0;
                bool f1 = true;
                for (long long gy = 0; gy < h; ++gy)
                    for (long long gx = 0; gx < w; ++gx) {
                        const double dd = cand(gy, gx) ? double((gx - ux) * (gx - ux) + (gy - uy) * (gy - uy)) : 1e12;
                        if (f1 || dd < bd) {
                            bd = dd;
                            bx = gx;
                            by = gy;
                            f1 = false;
                        }
                    }
                got = {bx, by};
                break;
            }
        }
        if (!got)
            got = {ux, uy};
        chosen.push_back(*got);
        out.insert(kind, {double(got->first) + 0.5 + double(I.x0), double(got->second) + 0.5 + double(I.y0)});
    }
    return out;
}

// --- naval base
std::optional<Naval> PositionPlacer::naval_spot(long long pid, const std::optional<XY> &near0) const
{
    // -> (display point on the water, game rotation) or None if the province has no valid coast.
    const Crop cr = crop(pid, 3);
    const long long h = cr.sub.h(), w = cr.sub.w(), x0 = cr.x0, y0 = cr.y0;
    Mask M(h, w, 0), V(h, w, 0);
    for (size_t i = 0; i < M.flat().size(); ++i) {
        M.flat()[i] = cr.sub.flat()[i] == pid;
        V.flat()[i] = sea_valid[cr.sub.flat()[i]];
    }
    std::vector<long long> cys, cxs;
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x)
            if (M(y, x) && ((y > 0 && V(y - 1, x)) || (y + 1 < h && V(y + 1, x)) || (x > 0 && V(y, x - 1)) ||
                            (x + 1 < w && V(y, x + 1)))) {
                cys.push_back(y);
                cxs.push_back(x);
            }
    if (cys.empty())
        return std::nullopt;
    XY near_xy;
    if (near0) {
        near_xy = *near0;
    } else {
        std::vector<long long> ys, xs;
        nonzero(M, ys, xs);
        near_xy = {mean_int(xs) + 0.5 + double(x0), mean_int(ys) + 0.5 + double(y0)};
    }
    size_t bi = 0;
    double bd = 0;
    for (size_t i = 0; i < cxs.size(); ++i) {
        const double dx = double(cxs[i]) + 0.5 + double(x0) - near_xy.first, dy = double(cys[i]) + 0.5 + double(y0) - near_xy.second;
        const double d = dx * dx + dy * dy;
        if (i == 0 || d < bd) {
            bd = d;
            bi = i;
        }
    }
    const long long ly = cys[bi], lx = cxs[bi];
    std::optional<std::pair<long long, long long>> best;
    long long bscore = -1;
    for (const auto &[dy, dx] : {std::pair{-1LL, 0LL}, {1LL, 0LL}, {0LL, -1LL}, {0LL, 1LL}}) {
        const long long sy = ly + dy, sx = lx + dx;
        if (0 <= sy && sy < h && 0 <= sx && sx < w && V(sy, sx)) {
            long long sc = 0;
            for (long long y = std::max(0LL, sy - 1); y < std::min(h, sy + 2); ++y)
                for (long long x = std::max(0LL, sx - 1); x < std::min(w, sx + 2); ++x)
                    sc += V(y, x);
            if (sc > bscore) {
                best = {sx, sy};
                bscore = sc;
            }
        }
    }
    const auto [sx, sy] = *best;
    // direction land -> water, from the land around the spot
    const long long wy0 = std::max(0LL, ly - 4), wx0 = std::max(0LL, lx - 4);
    long long n = 0, sumx = 0, sumy = 0;
    for (long long y = wy0; y < std::min(h, ly + 5); ++y)
        for (long long x = wx0; x < std::min(w, lx + 5); ++x)
            if (M(y, x)) {
                ++n;
                sumx += x - wx0;
                sumy += y - wy0;
            }
    const double lcx = double(sumx) / double(n) + double(wx0) + 0.5;
    const double lcy = double(sumy) / double(n) + double(wy0) + 0.5;
    const double ang = std::atan2(double(sy) + 0.5 - lcy, double(sx) + 0.5 - lcx);
    const double rot = py::mod(ang + PI, 2 * PI);
    return Naval{{double(sx) + 0.5 + double(x0), double(sy) + 0.5 + double(y0)}, rot};
}

Naval PositionPlacer::fallback_naval(long long pid, const std::optional<XY> &near0)
{
    // A naval base spot for a province without a valid coast whose history still builds one (so the game has
    // somewhere to draw it): at any sea it touches, else on the province itself.
    std::vector<std::uint8_t> keep = sea_valid;
    sea_valid = md.sea;
    if (!sea_valid.empty())
        sea_valid[0] = 0;
    const auto ns = naval_spot(pid, near0);
    sea_valid = std::move(keep);
    if (ns)
        return *ns;
    XY near_xy;
    if (near0) {
        near_xy = *near0;
    } else {
        const auto inf = info(pid);
        size_t am = 0;
        for (size_t i = 1; i < inf->D.flat().size(); ++i)
            if (inf->D.flat()[i] > inf->D.flat()[am])
                am = i;
        const long long r = static_cast<long long>(am) / inf->D.w(), c = static_cast<long long>(am) % inf->D.w();
        near_xy = {double(c) + 0.5 + double(inf->x0), double(r) + 0.5 + double(inf->y0)};
    }
    return {near_xy, 0.0};
}

bool PositionPlacer::naval_ok(long long pid, const XY &disp_xy) const
{
    // Is a naval base point at the province's valid coast (on the water next to it, or its shore)?
    const long long x = static_cast<long long>(std::floor(disp_xy.first)), y = static_cast<long long>(std::floor(disp_xy.second));
    if (!(0 <= x && x < md.w && 0 <= y && y < md.h))
        return false;
    const std::uint16_t here = md.idmap(y, x);
    bool any_pid = false, any_valid = false;
    for (long long yy = std::max(0LL, y - 1); yy < std::min(md.h, y + 2); ++yy)
        for (long long xx = std::max(0LL, x - 1); xx < std::min(md.w, x + 2); ++xx) {
            any_pid = any_pid || md.idmap(yy, xx) == pid;
            any_valid = any_valid || sea_valid[md.idmap(yy, xx)];
        }
    if (sea_valid[here])
        return any_pid;
    if (here == pid)
        return any_valid;
    return false;
}

// ---------------------------------------------------------------------------------------------- check
std::vector<std::pair<QString, QString>> check_entry(PositionPlacer &placer, const mapview::MapData &md, long long pid,
                                                     const Entry &e, const QString &name, double H, bool want_naval,
                                                     bool hist_naval)
{
    std::vector<std::pair<QString, QString>> out;
    const auto &pts = e.pts;
    auto disp = [&](const XY &xy) { return XY{xy.first, H - xy.second}; };
    for (const QString &k : {u"unit"_s, u"text_position"_s})
        if (!pts.contains(k))
            out.emplace_back(k, u"no %1 position"_s.arg(KIND_NAMES.value(k).toLower()));
    for (const auto &[k, xy] : pts) {
        if (k == u"naval_base")
            continue;
        const XY d = disp(xy);
        if (md.province_at(double(py::trunc_int(d.first)), double(py::trunc_int(d.second))) != pid)
            out.emplace_back(k, u"%1 is outside the province"_s.arg(KIND_NAMES.value(k, k)));
    }
    if (pts.contains(u"text_position"_s) && !name.isEmpty()) {
        const double sc = e.text_scale ? *e.text_scale : 1.0;
        const double rot = (e.text_rotation && *e.text_rotation != 0.0) ? *e.text_rotation : 0.0;
        const double frac = placer.name_clips(pid, name_center(disp(*pts.get(u"text_position"_s)), -rot, sc), -rot, sc, name);
        if (frac > 0.02)
            out.emplace_back(u"text_position"_s, u"name sticks out of the province (%1%)"_s.arg(py::round_int(frac * 100)));
    }
    const bool has_nb = pts.contains(u"naval_base"_s);
    if (hist_naval && !has_nb)
        out.emplace_back(u"naval_base"_s, CRASH);
    else if (want_naval && !has_nb)
        out.emplace_back(u"naval_base"_s, u"coastal province without a naval base position"_s);
    else if (!want_naval && has_nb && !hist_naval)
        out.emplace_back(u"naval_base"_s, u"naval base, but no valid coast (only Meme river / no-coast / no sea)"_s);
    else if (want_naval && has_nb && !placer.naval_ok(pid, disp(*pts.get(u"naval_base"_s))))
        out.emplace_back(u"naval_base"_s, u"naval base is not at the coast (too far inland / out at sea)"_s);
    if (hist_naval && !want_naval)
        out.emplace_back(u"history"_s, u"its history builds a naval base, but it has no valid coast (fix the history)"_s);
    return out;
}

}  // namespace positions
