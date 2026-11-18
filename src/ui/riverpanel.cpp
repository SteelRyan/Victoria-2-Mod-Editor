#include "ui/riverpanel.h"

#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "grid/pyset.h"
#include "grid/random.h"
#include "map/layertools.h"
#include "map/mapcheck.h"
#include "map/mismatch.h"
#include "map/provbmp.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>
#include <random>
#include <tuple>
#include <unordered_map>

using namespace Qt::StringLiterals;

namespace riverpanel {

const std::vector<int> WIDTHS = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const Rgb LAND_RGB{70, 76, 84};
const Rgb HALO_RGB{34, 38, 44};
const Rgb PROBLEM_RGB{255, 0, 0};           // pixels that break a rivers.bmp rule: pure red, nothing else is
const Rgb SELECT_RGB{255, 225, 60};         // rivers picked with the Select tool

static std::vector<std::pair<QVariant, QString>> width_styles()
{
    std::vector<std::pair<QVariant, QString>> out = {{u"mod"_s, u"Like the mod: one thin pixel, then 4"_s},
                                                     {u"widen"_s, u"Widening: 2 → 4 → 6 → 7 to the mouth"_s}};
    for (int w : WIDTHS)
        out.emplace_back(w, u"Fixed width %1"_s.arg(w));
    return out;
}
const std::vector<std::pair<QVariant, QString>> WIDTH_STYLES = width_styles();

static std::array<std::uint8_t, 3> px(const Rgb &c)
{
    return {std::uint8_t(std::get<0>(c)), std::uint8_t(std::get<1>(c)), std::uint8_t(std::get<2>(c))};
}

std::array<std::array<std::uint8_t, 3>, 256> display_colors()
{
    // Colours the map uses for river pixels (brighter than the file's palette, readable on dark land). Merges are
    // orange here so that red only ever means "this pixel breaks a rule".
    std::array<std::array<std::uint8_t, 3>, 256> c{};
    c[SOURCE] = {70, 235, 100};
    c[MERGE] = {255, 150, 40};
    for (size_t i = 0; i < WIDTHS.size(); ++i) {
        const double t = double(i) / double(WIDTHS.size() - 1);
        c[size_t(WIDTHS[i])] = {std::uint8_t(int(160 - 130 * t)), std::uint8_t(int(230 - 150 * t)), 255};
    }
    return c;
}

// ---------------------------------------------------------------------------------------------- RiverView
RiverView::RiverView(const mapview::MapData &md_, const layerbmp::IndexedBitmap &rb, RiverBox *box_)
    : md(md_), tb(rb), box(box_), disp(display_colors())
{
}

mapview::RgbImage RiverView::colors(long long y0, long long y1, long long x0, long long x1)
{
    const long long H = y1 - y0, W = x1 - x0;
    const grid::Grid<std::uint8_t> &data = *tb.data;
    mapview::RgbImage rgb(H, W);
    // (C++ only, 2026-11-10, the performance round — the same picture: row by row, the colours taken once, and the
    // halo (a land pixel next to a river, 4 sides) tested where it is drawn instead of a dilated copy)
    const auto SEA = px(mapview::SEA_RGB), LANDC = px(LAND_RGB), HALO = px(HALO_RGB);
    const std::array<std::uint8_t, 3> BLACK{0, 0, 0};
    const std::uint8_t *sea_of = md.sea.data();
    for (long long y = 0; y < H; ++y) {
        const std::uint16_t *ir = md.idmap.row(y + y0) + x0;
        const std::uint8_t *dr = data.row(y + y0) + x0;
        const std::uint8_t *du = y > 0 ? data.row(y + y0 - 1) + x0 : nullptr;       // (rows of the window only)
        const std::uint8_t *dd = y + 1 < H ? data.row(y + y0 + 1) + x0 : nullptr;
        auto *out = rgb.row(y);
        for (long long x = 0; x < W; ++x) {
            const std::uint16_t id = ir[x];
            const std::uint8_t v = dr[x];
            if (v < 12)
                out[x] = disp[v];
            else if (id == 0)
                out[x] = BLACK;
            else if (sea_of[id])
                out[x] = SEA;
            else {
                const bool by_river = (du && du[x] < 12) || (dd && dd[x] < 12) || (x > 0 && dr[x - 1] < 12) ||
                                      (x + 1 < W && dr[x + 1] < 12);
                out[x] = by_river ? HALO : LANDC;
            }
        }
    }
    if (box && box->selected && !box->selected->empty())
        for (std::int64_t f : *box->selected) {
            const long long ys = f / md.w, xs = f % md.w;
            if (ys >= y0 && ys < y1 && xs >= x0 && xs < x1)
                rgb(ys - y0, xs - x0) = px(SELECT_RGB);
        }
    const auto PROB = px(PROBLEM_RGB);
    // (the whole map: the masks the checks keep — the same river_problem_masks, counted once per change)
    const bool whole = y0 == 0 && x0 == 0 && y1 == data.h() && x1 == data.w();
    mapcheck::Masks own;
    if (!whole)
        own = mapcheck::river_problem_masks(md, data, y0, y1, x0, x1);
    for (const auto &[k, m] : whole ? mapcheck::river_masks_whole(md, tb) : own) {
        const std::uint8_t *mf = m.flat().data();
        auto *of = rgb.flat().data();
        for (size_t i = 0, n = m.flat().size(); i < n; ++i)
            if (mf[i])
                of[i] = PROB;
    }
    if (const auto top = on_top(y0, y1, x0, x1))
        for (const Top &t : *top) {
            const auto c = px(*t.rgb);
            const std::uint8_t *mf = t.mask.flat().data();
            auto *of = rgb.flat().data();
            for (size_t i = 0, n = t.mask.flat().size(); i < n; ++i)
                if (mf[i])
                    of[i] = c;
        }
    return rgb;
}

std::optional<std::vector<mapview::PixelLayer::Top>> RiverView::on_top(long long y0, long long y1, long long x0,
                                                                      long long x1)
{
    // Land ↔ water that doesn't match the provinces (the tester, 2026-10-15): water under land pink, land / a river on
    // a sea tile green (mismatch.py) — over the border shading, so they show on coasts.
    mismatch::Mismatch *mm = box && box->doc && box->panel ? box->panel->mismatch() : nullptr;
    if (!mm)
        return std::nullopt;
    std::vector<Top> out;
    for (auto &[m, c] : mismatch::overlay(*mm, {u"r_water"_s, u"r_sea"_s}, y0, y1, x0, x1))
        out.push_back(Top{std::move(m), c, {}});
    if (out.empty())
        return std::nullopt;
    return out;
}

// ---------------------------------------------------------------------------------------------- the plan
void RiverPlan::clear()
{
    start.reset();
    end.reset();
    via.clear();
    branches.clear();
    seed = 1;
    written.clear();
    info.clear();
    stage.reset();
    mouth.reset();
    paths.clear();
}

bool RiverPlan::active() const
{
    return start.has_value() || !written.isEmpty() || stage.has_value();
}

store::Snap RiverPlan::snapshot() const
{
    State s{start, end, via, branches, seed, {}, info, stage, mouth, paths};
    for (const auto &[k, v] : written)
        s.written.emplace_back(k, v);
    std::sort(s.written.begin(), s.written.end());
    return store::box(std::move(s));
}

void RiverPlan::restore(const store::Snap &snap)
{
    const State &s = store::unbox<State>(snap);
    start = s.start;
    end = s.end;
    via = s.via;
    branches = s.branches;
    seed = s.seed;
    written.clear();                             // (dict(sorted items): the keys in order)
    for (const auto &[k, v] : s.written)
        written.insert(k, v);
    info = s.info;
    stage = s.stage;
    mouth = s.mouth;
    paths = s.paths;
}

// ---------------------------------------------------------------------------------------------- routing
Mask _dilate(const Mask &m, bool eight)
{
    const long long H = m.h(), W = m.w();
    Mask o = m;
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x) {
            if (o(y, x))
                continue;
            bool on = (y > 0 && m(y - 1, x)) || (y < H - 1 && m(y + 1, x)) || (x > 0 && m(y, x - 1)) ||
                      (x < W - 1 && m(y, x + 1));
            if (!on && eight)
                on = (y > 0 && x > 0 && m(y - 1, x - 1)) || (y > 0 && x < W - 1 && m(y - 1, x + 1)) ||
                     (y < H - 1 && x > 0 && m(y + 1, x - 1)) || (y < H - 1 && x < W - 1 && m(y + 1, x + 1));
            o(y, x) = on;
        }
    return o;
}

static bool any(const Mask &m)
{
    return std::any_of(m.flat().begin(), m.flat().end(), [](std::uint8_t v) { return v != 0; });
}
static Mask op(const Mask &a, const Mask &b, char how)   // a & b, a | b, a & ~b
{
    Mask o(a.h(), a.w(), 0);
    for (size_t i = 0; i < o.flat().size(); ++i)
        o.flat()[i] = how == '&' ? (a.flat()[i] && b.flat()[i])
                                 : how == '|' ? (a.flat()[i] || b.flat()[i]) : (a.flat()[i] && !b.flat()[i]);
    return o;
}
static std::uint8_t at(const Mask &m, YX q) { return m(q.first, q.second); }

std::optional<std::vector<YX>> route(const Mask &passable, const std::vector<double> &cst, YX start, const Mask &gl,
                                     std::optional<YX> target, double radius)
{
    const long long H = passable.h(), W = passable.w();
    const auto &pas = passable.flat();
    const auto &goals = gl.flat();
    const long long ty = target ? target->first : 0, tx = target ? target->second : 0;
    const bool use_h = target.has_value();
    auto h = [&](long long i) {
        if (!use_h)
            return 0.0;
        const long long y = i / W, x = i % W;
        return std::max(0.0, double(std::llabs(y - ty) + std::llabs(x - tx)) - radius);
    };
    const long long s = start.first * W + start.second;
    std::vector<double> dist(static_cast<size_t>(H * W), 1e30);
    std::vector<std::int64_t> prev(static_cast<size_t>(H * W), -1);
    dist[size_t(s)] = 0.0;
    using E = std::tuple<double, double, long long>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> heap;
    heap.emplace(h(s), 0.0, s);
    while (!heap.empty()) {
        const auto [f, g, i] = heap.top();
        heap.pop();
        if (g > dist[size_t(i)])
            continue;
        if (goals[size_t(i)] && i != s) {
            std::vector<long long> out{i};
            while (prev[size_t(out.back())] >= 0)
                out.push_back(prev[size_t(out.back())]);
            std::vector<YX> path;
            for (auto it = out.rbegin(); it != out.rend(); ++it)
                path.emplace_back(*it / W, *it % W);
            return path;
        }
        const long long y = i / W, x = i % W;
        for (long long j : {y > 0 ? i - W : -1, y < H - 1 ? i + W : -1, x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1}) {
            if (j < 0 || !pas[size_t(j)])
                continue;
            const double ng = g + cst[size_t(j)];
            if (ng < dist[size_t(j)]) {
                dist[size_t(j)] = ng;
                prev[size_t(j)] = i;
                heap.emplace(ng + h(j), ng, j);
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------- Generator
std::array<long long, 4> Generator::_window(double scale) const
{
    const RiverPlan &p = plan;
    std::vector<Pt> pts;
    for (const auto &q : {p.start, p.end})
        if (q)
            pts.push_back(*q);
    pts.insert(pts.end(), p.via.begin(), p.via.end());
    for (const Branch &b : p.branches)
        pts.push_back(b.at);
    for (const Branch &b : p.branches)
        if (b.source)
            pts.push_back(*b.source);
    double mnx = pts[0].first, mxx = mnx, mny = pts[0].second, mxy = mny;
    for (const Pt &q : pts) {
        mnx = std::min(mnx, q.first);
        mxx = std::max(mxx, q.first);
        mny = std::min(mny, q.second);
        mxy = std::max(mxy, q.second);
    }
    long long extra = 0;
    for (const Branch &b : p.branches)
        extra = std::max(extra, b.length);
    const double span = std::max(mxx - mnx, mxy - mny);
    const long long m = (long long)(std::max({40.0, span * 0.4, double(extra) * 1.3}) * scale);
    return {std::max(0LL, (long long)mnx - m), std::max(0LL, (long long)mny - m), std::min(md.w, (long long)mxx + m + 1),
            std::min(md.h, (long long)mxy + m + 1)};
}

std::optional<OrderedMap<long long, int>> Generator::run()
{
    // -> {flat index: value} or None (error says why). Tries with bends first; if the bends lead nowhere, without
    // them, then in a larger area (a way around another river may need more room).
    const RiverPlan &p = plan;
    if (!p.start || !p.end) {
        error = u"Set the source and the mouth first."_s;
        return std::nullopt;
    }
    std::optional<QString> first_error;
    for (const auto &[scale, bends] : {std::pair{1.0, true}, std::pair{1.0, false}, std::pair{3.0, true},
                                       std::pair{3.0, false}}) {
        if (!bends && meander == 0)
            continue;
        error.reset();
        _bends = bends;
        auto out = _run_once(scale);
        if (out)
            return out;
        if (!first_error || first_error->isEmpty())
            first_error = error;
        if (error && !error->isEmpty() && !error->startsWith(u"No way"))
            break;                                   // not a routing problem: retrying won't help
    }
    error = first_error;
    const bool no_way = first_error && first_error->startsWith(u"No way");
    if (no_way && !_same_land())
        error = u"The source and the mouth aren't connected over land — pick a mouth on the same land."_s;
    else if (no_way)
        error = u"No way to the mouth without crossing another river (rivers can't cross) — move the source or the "
                u"mouth, or end it on that river instead."_s;
    return std::nullopt;
}

bool Generator::_same_land()
{
    // Is the mouth on the same land as the source (ignoring rivers)?
    const auto [x0, y0, x1, y1] = box;
    Mask land(y1 - y0, x1 - x0, 0);
    for (long long y = y0; y < y1; ++y)
        for (long long x = x0; x < x1; ++x) {
            const std::uint16_t id = md.idmap(y, x);
            land(y - y0, x - x0) = !md.sea[id] && id != 0;
        }
    const auto s = _snap(*plan.start, land);
    if (!s)
        return false;
    const Mask comp = provbmp::component(land, s->first, s->second);
    const long long ex = (long long)plan.end->first - x0, ey = (long long)plan.end->second - y0;
    for (long long y = std::max(0LL, ey - 12); y < std::min((long long)comp.h(), ey + 13); ++y)
        for (long long x = std::max(0LL, ex - 12); x < std::min((long long)comp.w(), ex + 13); ++x)
            if (comp(y, x))
                return true;
    return false;
}

std::optional<OrderedMap<long long, int>> Generator::_run_once(double scale)
{
    const RiverPlan &p = plan;
    box = _window(scale);
    const auto [x0, y0, x1, y1] = box;
    const long long H = y1 - y0, W = x1 - x0;
    R = grid::Grid<std::uint8_t>(H, W);
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x)
            R(y, x) = (*rb.data)(y + y0, x + x0);
    // the plan's own current pixels don't count as obstacles (they are replaced)
    for (const auto &[i, ov] : p.written) {
        const long long yy = i / md.w, xx = i % md.w;
        if (y0 <= yy && yy < y1 && x0 <= xx && xx < x1)
            R(yy - y0, xx - x0) = std::uint8_t(ov.first);
    }
    Mask sea_real(H, W, 0), water(H, W, 0);
    riv = Mask(H, W, 0);
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x) {
            const std::uint16_t id = md.idmap(y + y0, x + x0);
            sea_real(y, x) = md.sea[id] && id != 0;
            water(y, x) = md.sea[id] || id == 0 || R(y, x) == 254;
            riv(y, x) = R(y, x) < 12;
        }
    const Mask near1 = _dilate(riv, true);
    // at least 2 pixels of land between rivers — only where one joins another may they come closer
    base_ok_basic = Mask(H, W, 0);
    for (size_t i = 0; i < base_ok_basic.flat().size(); ++i)
        base_ok_basic.flat()[i] = !water.flat()[i] && !near1.flat()[i];
    base_ok = op(base_ok_basic, _dilate(near1, true), '-');
    coast = op(_dilate(sea_real), water, '-');
    // cost of entering a pixel: 1 + small-scale jitter (natural pixel steps) + broad noise (valleys to follow) +
    // hills / mountains cost more + keep off the coast. The big bends come from the displaced guide points.
    Flat gy(static_cast<size_t>(H * W)), gx(static_cast<size_t>(H * W));
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x) {
            gy[size_t(y * W + x)] = y + y0;
            gx[size_t(y * W + x)] = x + x0;
        }
    const auto n1 = layertools::value_noise(gy, gx, 3.2, p.seed + 3);
    const auto n2 = layertools::value_noise(gy, gx, 7.5, p.seed + 5);
    const auto n3 = layertools::value_noise(gy, gx, 20.0, p.seed);
    const float m3 = float(0.35 * meander);
    std::array<float, 256> tcost{};
    for (const auto &[i, t] : layerbmp::terrain_types(mod))
        if (i < 256)
            tcost[size_t(i)] = t == u"mountain" ? 1.8f : t == u"hills" ? 0.8f : t == u"arctic" ? 0.5f : 0.0f;
    cost.assign(static_cast<size_t>(H * W), 0.0f);
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x) {
            const size_t k = size_t(y * W + x);
            const float noise = (2.6f * n1[k] + 1.0f * n2[k]) + m3 * n3[k];
            float c = 1.0f + noise;
            if (rb_terrain)
                c = c + tcost[(*rb_terrain->data)(y + y0, x + x0)];
            cost[k] = float(double(c) + (coast.flat()[k] ? 6.0 : 0.0));
        }
    const auto src = _snap(*p.start, base_ok);
    if (!src) {
        error = u"The source must be on land, away from other rivers."_s;
        return std::nullopt;
    }
    const auto mo = _mouth();
    if (!mo)
        return std::nullopt;
    // main river: through the forced points in order, bent by displaced guide points in between
    std::vector<YX> points{*src};
    for (const Pt &v : p.via) {
        const auto q = _snap(v, base_ok);
        if (!q) {
            error = u"A forced point is on water or right next to another river — move it."_s;
            return std::nullopt;
        }
        points.push_back(*q);
    }
    grid::Generator rng(std::uint64_t(p.seed));
    points.push_back(mo->target);
    auto [pts, forced] = _meanders(points, base_ok, rng);
    pts.pop_back();                                  // the mouth itself is reached through `goals`
    forced.pop_back();
    std::optional<Mask> near_ok;
    if (mo->merge)
        near_ok = op(base_ok_basic, _dilate(_dilate(mo->goals, true), true), '&');
    auto path0 = _chain(pts, forced, mo->goals, mo->target, mo->radius, base_ok, mo->merge,
                        near_ok ? &*near_ok : nullptr);
    if (!path0)
        return std::nullopt;
    grid::Generator rng99(std::uint64_t(p.seed + 99));
    std::vector<YX> path = _roughen(*path0, op(base_ok, coast, '-'), rng99);
    if (path.size() < 3) {
        error = u"The river is too short."_s;
        return std::nullopt;
    }
    const auto vals = _widths((long long)path.size(), mo->merge);
    OrderedMap<long long, int> out;
    for (size_t k = 0; k < path.size(); ++k)
        out.insert((path[k].first + y0) * md.w + (path[k].second + x0), vals[k]);
    main = path;
    // branches (tributaries): join the river at a point, from a source `length` px away
    Mask own_all(H, W, 0);
    for (const YX &q : path)
        own_all(q.first, q.second) = 1;
    branch_info.clear();
    bpaths.clear();
    for (size_t bi = 0; bi < p.branches.size(); ++bi) {
        const Branch &b = p.branches[bi];
        const std::vector<YX> *parent_path = nullptr;
        if (!b.parent)
            parent_path = &path;
        else if (*b.parent < (long long)bpaths.size() && bpaths[size_t(*b.parent)])
            parent_path = &*bpaths[size_t(*b.parent)];
        std::optional<std::vector<YX>> bp;
        if (parent_path && !parent_path->empty())
            bp = _branch(b, (long long)bi, *parent_path, own_all);
        bpaths.push_back(bp);
        if (!bp) {
            branch_info.push_back(std::nullopt);
            continue;
        }
        for (const YX &q : *bp)
            own_all(q.first, q.second) = 1;
        const auto bv = _widths((long long)bp->size(), true, true);
        for (size_t k = 0; k < bp->size(); ++k)
            out.insert(((*bp)[k].first + y0) * md.w + ((*bp)[k].second + x0), bv[k]);
        branch_info.push_back((long long)bp->size());
    }
    length = (long long)path.size();
    mouth = mo->merge ? u"river"_s : u"sea"_s;
    return out;
}

// --- small jogs: long straight stretches get 1-pixel sidesteps, like hand-drawn rivers
std::vector<YX> Generator::_roughen(std::vector<YX> path, const Mask &allowed, grid::Generator &rng, bool keep_end)
{
    // Insert sidesteps into straight runs of 7+ pixels. A jog is kept only if its new pixels are allowed, touch no
    // other part of the river and make no 2×2 block.
    long long i = 1;
    while (i < (long long)path.size() - 8) {
        const YX d{path[size_t(i)].first - path[size_t(i - 1)].first, path[size_t(i)].second - path[size_t(i - 1)].second};
        long long run = 1;
        while (i + run < (long long)path.size() &&
               YX{path[size_t(i + run)].first - path[size_t(i + run - 1)].first,
                  path[size_t(i + run)].second - path[size_t(i + run - 1)].second} == d)
            ++run;
        bool jogged = false;
        if (run >= 7 && rng.random() < 0.85) {
            const long long s = i + rng.integers(1, std::max(2LL, std::min(run - 5, 6LL)));   // first shifted pixel
            const long long k = run - (s - i) - 2 >= 2 ? rng.integers(2, std::min(5LL, run - (s - i) - 2) + 1) : 0;
            if (k >= 2 && s + k < (long long)path.size() - 1 && (!keep_end || s + k < (long long)path.size() - 2)) {
                const bool first_plus = rng.random() < 0.5;
                for (long long sign : {first_plus ? 1LL : -1LL, first_plus ? -1LL : 1LL}) {
                    const long long py_ = d.second * sign, px_ = d.first * sign;   // perpendicular to the run
                    const YX a = path[size_t(s - 1)], b = path[size_t(s + k)];     // stay; the pixels between move
                    std::vector<YX> nw{{a.first + py_, a.second + px_}};
                    for (long long j = s; j < s + k; ++j)
                        nw.emplace_back(path[size_t(j)].first + py_, path[size_t(j)].second + px_);
                    nw.emplace_back(b.first + py_, b.second + px_);
                    std::vector<YX> cand(path.begin(), path.begin() + s);
                    cand.insert(cand.end(), nw.begin(), nw.end());
                    cand.insert(cand.end(), path.begin() + (s + k), path.end());
                    if (_valid_run(cand, s, s + (long long)nw.size(), allowed)) {
                        path = std::move(cand);
                        jogged = true;
                        i = s + (long long)nw.size() + 1;   // the rest of a long run gets more jogs
                        break;
                    }
                }
            }
        }
        if (!jogged)
            i += std::max(1LL, run);
    }
    return path;
}

static std::uint64_t key(long long y, long long x)
{
    return (std::uint64_t(y + (1LL << 31)) << 32) | std::uint64_t(std::uint32_t(x + (1LL << 31)));
}

bool Generator::_valid_run(const std::vector<YX> &path, long long s, long long e, const Mask &allowed)
{
    const long long H = allowed.h(), W = allowed.w();
    std::unordered_map<std::uint64_t, long long> pos;
    pos.reserve(path.size() * 2);
    for (size_t j = 0; j < path.size(); ++j)
        pos[key(path[j].first, path[j].second)] = (long long)j;
    if (pos.size() != path.size())
        return false;
    auto get = [&](long long y, long long x) -> std::optional<long long> {
        const auto it = pos.find(key(y, x));
        return it == pos.end() ? std::nullopt : std::optional<long long>(it->second);
    };
    for (long long j = std::max(0LL, s - 1); j < std::min((long long)path.size(), e + 1); ++j) {
        const auto [y, x] = path[size_t(j)];
        if (!(0 <= y && y < H && 0 <= x && x < W))
            return false;
        if (s <= j && j < e && !allowed(y, x))
            return false;
        for (const auto &[dy, dx] : {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}}) {
            const auto o = get(y + dy, x + dx);
            if (o && std::llabs(*o - j) != 1)
                return false;                        // touches another part of itself
        }
        if (get(y + 1, x) && get(y, x + 1) && get(y + 1, x + 1))
            return false;
    }
    return true;
}

// --- bends: guide points displaced sideways, recursively (midpoint displacement)
std::pair<std::vector<YX>, std::vector<bool>> Generator::_meanders(const std::vector<YX> &ctrl, const Mask &ok,
                                                                   grid::Generator &rng)
{
    // ctrl: window points (y, x) the river must pass. -> (points, forced flags) with bend points in between.
    std::vector<YX> pts{ctrl[0]};
    std::vector<bool> forced{true};
    for (size_t k = 0; k + 1 < ctrl.size(); ++k) {
        const auto mids = _subdivide(ctrl[k], ctrl[k + 1], ok, rng, 0);
        pts.insert(pts.end(), mids.begin(), mids.end());
        pts.push_back(ctrl[k + 1]);
        forced.insert(forced.end(), mids.size(), false);
        forced.push_back(true);
    }
    return {pts, forced};
}

std::vector<YX> Generator::_subdivide(YX a, YX b, const Mask &ok, grid::Generator &rng, int depth)
{
    const double f = _bends ? meander / 10.0 : 0.0;
    const double L = py::hypot(double(b.first - a.first), double(b.second - a.second));
    if (f <= 0 || L < 12 || depth > 6)
        return {};
    const double vy = double(b.first - a.first) / L, vx = double(b.second - a.second) / L;
    const double off = std::clamp(rng.normal(0.0, 0.24 * f), -0.42, 0.42) * L;
    const double my = double(a.first + b.first) / 2.0 + vx * off, mx = double(a.second + b.second) / 2.0 - vy * off;
    const auto m = _snap_w(py::round_int(my), py::round_int(mx), ok, std::max(3LL, (long long)(L / 6)));
    if (!m)
        return {};
    std::vector<YX> out = _subdivide(a, *m, ok, rng, depth + 1);
    out.push_back(*m);
    const auto rest = _subdivide(*m, b, ok, rng, depth + 1);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::optional<YX> Generator::_snap_w(long long y, long long x, const Mask &ok, long long r)
{
    const long long H = ok.h(), W = ok.w();
    std::optional<std::pair<long long, YX>> best;
    for (long long dy = -r; dy <= r; ++dy)
        for (long long dx = -r; dx <= r; ++dx) {
            const long long yy = y + dy, xx = x + dx;
            if (0 <= yy && yy < H && 0 <= xx && xx < W && ok(yy, xx)) {
                const long long d = dx * dx + dy * dy;
                if (!best || d < best->first)
                    best = std::pair{d, YX{yy, xx}};
            }
        }
    return best ? std::optional<YX>(best->second) : std::nullopt;
}

std::optional<std::vector<YX>> Generator::_chain(const std::vector<YX> &pts, const std::vector<bool> &forced,
                                                 const Mask &goals, YX target, double radius, const Mask &base_ok_,
                                                 bool merge, const Mask *near_ok)
{
    // Route pts[0] → pts[1] → … → a goal pixel. Bend points that can't be reached are skipped; forced ones must be
    // reached. The river never touches itself and keeps 2 px of land from its own earlier course.
    // near_ok: pixels close to the goal that may be used although they are nearer to the other river (the approach
    // to a join).
    std::vector<YX> path{pts[0]};
    const long long H = base_ok_.h(), W = base_ok_.w();
    auto count_forced = [&](size_t k) {
        long long n = 0;
        for (size_t j = 1; j <= k && j < forced.size(); ++j)
            n += forced[j];
        return n;
    };
    for (size_t k = 1; k <= pts.size(); ++k) {
        const bool last = k == pts.size();
        Mask before(H, W, 0);
        for (size_t j = 0; j + 1 < path.size(); ++j)
            before(path[j].first, path[j].second) = 1;
        const Mask dil_before = _dilate(before);
        Mask ok = op(base_ok_, dil_before, '-');
        if (path.size() > 8) {                       // its own course further back: 2 px of land (not the last bends)
            Mask older(H, W, 0);
            for (size_t j = 0; j + 8 < path.size(); ++j)
                older(path[j].first, path[j].second) = 1;
            ok = op(ok, _dilate(_dilate(older, true), true), '-');
        }
        ok(path.back().first, path.back().second) = 1;
        // running close alongside its own earlier course looks wrong (hairpins): make it expensive
        Mask close = before;
        for (int r = 0; r < 3; ++r)
            close = _dilate(close, true);
        std::vector<double> cst(cost.size());
        for (size_t i = 0; i < cst.size(); ++i)
            cst[i] = double(cost[i]) + (close.flat()[i] ? 5.0 : 0.0);
        std::optional<std::vector<YX>> seg;
        if (last) {
            const Mask okl = near_ok ? op(ok, op(*near_ok, dil_before, '-'), '|') : ok;
            const Mask g = merge ? op(goals, dil_before, '-') : op(goals, ok, '&');
            seg = route(op(okl, g, '|'), cst, path.back(), g, target, radius);
            if (!seg) {
                error = u"No way over land to the mouth without touching another river — move the points or lower "
                        u"the meander."_s;
                return std::nullopt;
            }
        } else {
            const YX q = pts[k];
            if (!at(ok, q)) {
                if (forced[k]) {
                    error = u"The river can't reach forced point %1 — move it."_s.arg(count_forced(k));
                    return std::nullopt;
                }
                continue;
            }
            Mask g(H, W, 0);
            g(q.first, q.second) = 1;
            seg = route(ok, cst, path.back(), g, q, 0);
            if (!seg) {
                if (forced[k]) {
                    error = u"No way to forced point %1 — move it."_s.arg(count_forced(k));
                    return std::nullopt;
                }
                continue;
            }
            // a bend point that is only reachable by a big detour (e.g. behind another river) is dropped
            const long long straight = std::llabs(q.first - path.back().first) + std::llabs(q.second - path.back().second);
            if (!forced[k] && double(seg->size()) > 2.2 * double(straight) + 8)
                continue;
        }
        path.insert(path.end(), seg->begin() + 1, seg->end());
    }
    return path;
}

std::optional<YX> Generator::_snap(const Pt &pt, const Mask &ok, long long r) const
{
    // Nearest pixel (window y, x) to map point pt that is ok, within r px.
    const long long cx = (long long)pt.first - box[0], cy = (long long)pt.second - box[1];
    std::optional<std::pair<long long, YX>> best;
    for (long long dy = -r; dy <= r; ++dy)
        for (long long dx = -r; dx <= r; ++dx) {
            const long long y = cy + dy, x = cx + dx;
            if (0 <= y && y < ok.h() && 0 <= x && x < ok.w() && ok(y, x)) {
                const long long d = dx * dx + dy * dy;
                if (!best || d < best->first)
                    best = std::pair{d, YX{y, x}};
            }
        }
    return best ? std::optional<YX>(best->second) : std::nullopt;
}

std::optional<Generator::Mouth> Generator::_mouth()
{
    // Goal pixels for the end: land next to the sea near the end point, or next to another river (merge).
    const auto [x0, y0, x1, y1] = box;
    const long long ex = (long long)plan.end->first - x0, ey = (long long)plan.end->second - y0;
    const long long H = riv.h(), W = riv.w();
    auto d2 = [&](long long y, long long x) { return (y - ey) * (y - ey) + (x - ex) * (x - ex); };
    Mask near(H, W, 0);
    if (plan.mouth != u"sea"_s)
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x)
                near(y, x) = riv(y, x) && d2(y, x) <= 16;
    const bool any_near = any(near);
    if (plan.mouth == u"river"_s && !any_near) {
        error = u"There's no river to join where the mouth is — move the mouth onto the river."_s;
        return std::nullopt;
    }
    if (any_near) {
        // every free spot next to that river within a few pixels of the click can take the red pixel (the router
        // picks one it can reach — a spot on the far bank can't be reached without crossing)
        Mask goals(H, W, 0);
        for (long long jy = 0; jy < H; ++jy)
            for (long long jx = 0; jx < W; ++jx) {
                if (!near(jy, jx))
                    continue;
                for (const auto &[dy, dx] : {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}}) {
                    const long long y = jy + dy, x = jx + dx;
                    if (!(0 <= y && y < H && 0 <= x && x < W) || riv(y, x) || goals(y, x))
                        continue;
                    const std::uint16_t id = md.idmap(y + y0, x + x0);
                    if (md.sea[id] || R(y, x) == 254 || id == 0)
                        continue;
                    // the red pixel may touch only one river pixel, and needs a free pixel to be reached from
                    long long touch = 0;
                    bool entry = false;
                    for (const auto &[ddy, ddx] : {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}}) {
                        const long long qy = y + ddy, qx = x + ddx;
                        if (0 <= qy && qy < H && 0 <= qx && qx < W) {
                            touch += riv(qy, qx);
                            entry = entry || base_ok_basic(qy, qx);
                        }
                    }
                    if (touch == 1 && entry)
                        goals(y, x) = 1;
                }
            }
        if (!any(goals)) {
            error = u"The river it should join has no free spot there — end it a bit further along."_s;
            return std::nullopt;
        }
        return Mouth{goals, {ey, ex}, 5, true};
    }
    Mask goals(H, W, 0);
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x)
            goals(y, x) = coast(y, x) && base_ok(y, x) && d2(y, x) <= 12 * 12;
    if (!any(goals)) {
        error = u"End the river on the coast (or on the sea next to it), or on another river."_s;
        return std::nullopt;
    }
    return Mouth{goals, {ey, ex}, 12, false};
}

std::optional<std::vector<YX>> Generator::_branch(const Branch &b, long long bi, const std::vector<YX> &path,
                                                  const Mask &own_all)
{
    const auto [x0, y0, x1, y1] = box;
    const long long H = own_all.h(), W = own_all.w();
    const long long ax = (long long)b.at.first - x0, ay = (long long)b.at.second - y0;
    const long long n = (long long)path.size();
    if (n < 8)
        return std::nullopt;
    long long k0 = 3;
    {
        long long best = -1;
        for (long long i = 3; i < n - 3; ++i) {
            const long long d = (path[size_t(i)].first - ay) * (path[size_t(i)].first - ay) +
                                (path[size_t(i)].second - ax) * (path[size_t(i)].second - ax);
            if (best < 0 || d < best) {
                best = d;
                k0 = i;
            }
        }
    }
    // the red pixel must touch the river at exactly one pixel; at a stair-step corner no neighbour does, so the river
    // pixels next to the chosen one are tried as well
    const Mask own1 = _dilate(own_all, true);
    const Mask ok = op(base_ok, _dilate(own1, true), '-');   // 2 px of land from the rivers of this system
    const Mask basic = op(base_ok_basic, own1, '-');         // 1 px — only for the approach to the join
    // join spots on both banks along a short stretch around the chosen point: a branch can't cross the river, so each
    // source joins on its own bank (the router picks the spot it can reach)
    Mask goals(H, W, 0);
    for (long long k = std::max(3LL, k0 - 8); k < std::min(n - 3, k0 + 9); ++k) {
        const YX Jk = path[size_t(k)];
        for (const auto &[dy, dx] : {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}}) {
            const long long y = Jk.first + dy, x = Jk.second + dx;
            if (!(0 <= y && y < H && 0 <= x && x < W) || own_all(y, x) || !base_ok_basic(y, x))
                continue;
            long long touch = 0;
            bool entry = false;                     // the branch needs a free pixel to arrive from
            for (const auto &[ddy, ddx] : {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}}) {
                const long long qy = y + ddy, qx = x + ddx;
                if (0 <= qy && qy < H && 0 <= qx && qx < W) {
                    touch += own_all(qy, qx);
                    entry = entry || basic(qy, qx);
                }
            }
            if (touch == 1 && entry)
                goals(y, x) = 1;
        }
    }
    if (!any(goals))
        return std::nullopt;
    const Mask near_ok = op(basic, _dilate(_dilate(goals, true), true), '&');
    std::vector<YX> vias;
    if (b.via)
        for (const Pt &v : *b.via)
            if (const auto q = _snap(v, ok))
                vias.push_back(*q);
    const YX J = path[size_t(k0)];
    const long long L = b.length;
    std::vector<YX> cands;
    if (b.source) {
        if (const auto s0 = _snap(*b.source, ok))
            cands.push_back(*s0);
    } else {
        // automatically: `length` px away, off to one side of the river and a little upstream; if that side has no
        // room (sea, another river), other angles, distances and the other side are tried
        const YX a = path[size_t(std::max(0LL, k0 - 4))], c = path[size_t(std::min(n - 1, k0 + 4))];
        double dy = double(c.first - a.first), dx = double(c.second - a.second);
        double ln = py::hypot(dy, dx);
        if (ln == 0)
            ln = 1.0;
        dy /= ln;
        dx /= ln;
        grid::Generator rng(std::uint64_t(plan.seed * 31 + bi));
        const long long side = rng.random() < 0.5 ? 1 : -1;
        for (long long s : {side, -side}) {
            const double py_ = -dx * double(s), px_ = dy * double(s);   // perpendicular
            const double by = py_ * 0.8 - dy * 0.45, bx = px_ * 0.8 - dx * 0.45;
            for (int ang : {0, 25, -25, 50, -50}) {
                const double rad = double(ang) * (std::numbers::pi / 180.0);
                const double ca = std::cos(rad), sa = std::sin(rad);
                const double vy = by * ca - bx * sa, vx = by * sa + bx * ca;
                for (double f : {1.0, 0.7, 1.3}) {
                    const long long ty = (long long)(double(J.first) + vy * double(L) * f),
                                    tx = (long long)(double(J.second) + vx * double(L) * f);
                    if (!(0 <= ty && ty < H && 0 <= tx && tx < W))
                        continue;
                    const auto q = _snap(Pt(double(tx + x0), double(ty + y0)), ok, std::max(4LL, L / 6));
                    if (q && std::find(cands.begin(), cands.end(), *q) == cands.end())
                        cands.push_back(*q);
                }
            }
        }
    }
    const auto err = error;
    const bool bends = _bends;
    std::optional<std::vector<YX>> found;
    for (size_t ci = 0; ci < std::min<size_t>(12, cands.size()); ++ci) {
        std::vector<bool> tries = bends ? std::vector<bool>{true, false} : std::vector<bool>{false};
        for (bool with_bends : tries) {
            _bends = with_bends;
            grid::Generator rng(std::uint64_t(plan.seed * 31 + bi + 7));
            std::vector<YX> ctrl{cands[ci]};
            ctrl.insert(ctrl.end(), vias.begin(), vias.end());
            ctrl.push_back(J);
            auto [pts, forced] = _meanders(ctrl, ok, rng);
            pts.pop_back();
            forced.pop_back();
            found = _chain(pts, forced, goals, J, 10, ok, true, &near_ok);
            if (found && !found->empty())
                break;
        }
        if (found && !found->empty())
            break;
    }
    _bends = bends;
    error = err;                                     // a branch without room is reported per branch, not as the river's
    if (found && !found->empty()) {
        grid::Generator rng(std::uint64_t(plan.seed * 31 + bi + 99));
        found = _roughen(*found, op(ok, coast, '-'), rng);
        return found;
    }
    return std::nullopt;
}

std::vector<int> Generator::_widths(long long n, bool merge, bool branch) const
{
    std::vector<int> vals;
    const bool fixed = width_style.typeId() == QMetaType::Int;
    const QString st = fixed ? QString() : width_style.toString();
    auto t_of = [&](long long i) { return double(i) / double(std::max(1LL, n - 1)); };
    if (st == u"widen" && !branch) {
        vals.push_back(SOURCE);
        for (long long i = 1; i < n; ++i) {
            const double t = t_of(i);
            vals.push_back(t < 0.35 ? 2 : t < 0.65 ? 4 : t < 0.9 ? 6 : 7);
        }
    } else if (st == u"widen") {                     // branches widen too, but stay smaller
        vals.push_back(SOURCE);
        for (long long i = 1; i < n; ++i)
            vals.push_back(t_of(i) < 0.5 ? 2 : 4);
    } else if (fixed) {
        vals.push_back(SOURCE);
        vals.insert(vals.end(), size_t(std::max(0LL, n - 1)), width_style.toInt());
    } else {
        vals = {SOURCE, 2};
        vals.insert(vals.end(), size_t(std::max(0LL, n - 2)), 4);
    }
    if (merge && !vals.empty())
        vals.back() = MERGE;
    if ((long long)vals.size() > n)
        vals.resize(size_t(std::max(0LL, n)));
    return vals;
}

// ---------------------------------------------------------------------------------------------- tracing
static const std::array<YX, 4> N4 = {YX{0, 1}, YX{0, -1}, YX{1, 0}, YX{-1, 0}};

std::set<YX> trace_network(const grid::Grid<std::uint8_t> &R, long long y, long long x, size_t limit)
{
    // All river pixels (y, x) 4-connected to (y, x): a whole river network.
    const long long H = R.h(), W = R.w();
    std::set<YX> seen{{y, x}};
    std::vector<YX> stack{{y, x}};
    while (!stack.empty() && seen.size() < limit) {
        const YX c = stack.back();
        stack.pop_back();
        for (const auto &[dy, dx] : N4) {
            const YX q{c.first + dy, c.second + dx};
            if (0 <= q.first && q.first < H && 0 <= q.second && q.second < W && !seen.count(q) &&
                R(q.first, q.second) < 12) {
                seen.insert(q);
                stack.push_back(q);
            }
        }
    }
    return seen;
}

std::pair<std::vector<Traced>, std::map<YX, long long>> split_branches(const grid::Grid<std::uint8_t> &R,
                                                                       const std::set<YX> &comp,
                                                                       const std::function<bool(YX)> &is_sea)
{
    // Rivers of a network: one per source, followed pixel by pixel to its end.
    const long long H = R.h(), W = R.w();
    auto n4 = [&](YX p) {
        std::vector<YX> out;
        for (const auto &[dy, dx] : N4)
            if (0 <= p.first + dy && p.first + dy < H && 0 <= p.second + dx && p.second + dx < W)
                out.emplace_back(p.first + dy, p.second + dx);
        return out;
    };
    auto r = [&](YX q) { return R(q.first, q.second); };
    std::map<YX, long long> owner;
    std::vector<Traced> branches;
    for (const YX &s : comp) {                       // (a std::set: sorted, as sorted(...))
        if (r(s) != 0)
            continue;
        Traced t;
        t.path = {s};
        std::set<YX> inpath{s};
        std::optional<YX> prev;
        YX cur = s;
        for (;;) {
            if (r(cur) == 1 && prev) {               // red: this river flows into the next one
                std::optional<YX> nb;
                for (const YX &q : n4(cur))
                    if (comp.count(q) && q != *prev) {
                        nb = q;
                        break;
                    }
                t.end = u"river"_s;
                t.end_at = nb;
                break;
            }
            std::vector<YX> nbs;
            for (const YX &q : n4(cur))
                if (comp.count(q) && (!prev || q != *prev) && !inpath.count(q))
                    nbs.push_back(q);
            std::optional<YX> nonred, red, owned;
            for (const YX &q : nbs) {
                if (!owner.count(q)) {
                    if (r(q) != 1 && !nonred)
                        nonred = q;
                    if (r(q) == 1 && !red)
                        red = q;
                } else if (!owned) {
                    owned = q;
                }
            }
            YX nxt;
            if (nonred) {
                nxt = *nonred;
            } else if (red) {
                nxt = *red;
            } else if (owned) {
                t.end = u"river"_s;                  // runs into a traced river without a red pixel
                t.end_at = owned;
                break;
            } else {
                bool sea = false;
                for (const YX &q : n4(cur))
                    if (is_sea(q)) {
                        sea = true;
                        break;
                    }
                t.end = sea ? u"sea"_s : u"land"_s;
                break;
            }
            prev = cur;
            cur = nxt;
            t.path.push_back(cur);
            inpath.insert(cur);
        }
        const long long i = (long long)branches.size();
        for (const YX &q : t.path)
            owner.emplace(q, i);
        branches.push_back(std::move(t));
    }
    for (Traced &b : branches)
        if (b.end == u"river" && b.end_at) {
            const auto it = owner.find(*b.end_at);
            if (it != owner.end())
                b.parent = it->second;
        }
    return {branches, owner};
}

std::vector<long long> subtree(const std::vector<Traced> &branches, long long root)
{
    // Indexes of `root` and every river flowing into it (directly or further up), parents before children.
    std::map<long long, std::vector<long long>> kids;
    for (long long i = 0; i < (long long)branches.size(); ++i)
        if (branches[size_t(i)].parent && i != root)
            kids[*branches[size_t(i)].parent].push_back(i);
    std::vector<long long> out;
    std::vector<long long> queue{root};
    size_t head = 0;
    while (head < queue.size()) {
        const long long i = queue[head++];
        if (std::find(out.begin(), out.end(), i) != out.end())
            continue;
        out.push_back(i);
        const auto it = kids.find(i);
        if (it != kids.end())
            queue.insert(queue.end(), it->second.begin(), it->second.end());
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- panel
const std::vector<Step> STEPS = {
    {u"source"_s, u"Source"_s, u"click on land where the river starts"_s},
    {u"mouth"_s, u"Mouth"_s, u"click the coast / sea — or another river it flows into"_s},
    {u"via"_s, u"Points it must pass"_s, u"optional: click them in order"_s},
    {u"branches"_s, u"Branches"_s, u"optional: click where a branch joins, then its source"_s}};
const QStringList GUIDE = {u"source"_s, u"mouth"_s, u"via"_s, u"branches"_s};

QVariantList Tag::list() const
{
    QVariantList l{u"rplan"_s, kind, i};
    if (j)
        l << *j;
    return l;
}

std::optional<Tag> Tag::of(const std::optional<QVariantList> &hit)
{
    if (!hit || hit->size() < 3 || (*hit)[0].toString() != u"rplan")
        return std::nullopt;
    Tag t{(*hit)[1].toString(), (*hit)[2].toLongLong(), std::nullopt};
    if (hit->size() > 3)
        t.j = (*hit)[3].toLongLong();
    return t;
}

std::function<long long()> RiverBox::random_seed = [] {
    // np.random.default_rng().integers(1, 1 << 30): fresh entropy each time
    static std::random_device rd;
    return (long long)(1 + (std::uint64_t(rd()) << 32 | rd()) % ((1ULL << 30) - 1));
};

RiverBox::RiverBox(moddata::Mod &mod_, const mapview::MapData &md_, drawlink::Panel *panel_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_), panel(panel_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- tool
    auto *s = new widgets::Section();
    auto *trow = new QHBoxLayout();
    trow->setSpacing(0);
    auto *g = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"system"_s, u"River system"_s},
                                                                      {u"select"_s, u"Select"_s},
                                                                      {u"pen"_s, u"Pen"_s},
                                                                      {u"eraser"_s, u"Eraser"_s},
                                                                      {u"fix"_s, u"Fix"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"system");
        connect(b, &QPushButton::toggled, this, [this](bool on) {
            if (on)
                _tool_changed();
        });
        g->addButton(b);
        tool_btns[k] = b;
        tool_keys << k;
        trow->addWidget(b, 1);
    }
    s->lay->addLayout(trow);
    lay->addWidget(s);

    // --- river system: the guide
    s = sys_section = new widgets::Section();
    new_btn = new QPushButton(u"＋  New river system"_s);
    new_btn->setObjectName(u"primary"_s);
    new_btn->setToolTip(u"Place the source, the mouth, points it must pass and branches — one after another — "
                        u"then generate the whole river system"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { start_guide(); });
    s->lay->addWidget(new_btn);
    steps_host = new QWidget();
    auto *sl = new QVBoxLayout(steps_host);
    sl->setContentsMargins(0, 2, 0, 2);
    sl->setSpacing(3);
    for (const Step &st : STEPS) {
        QLabel *lb = widgets::label(QString(), u"sub"_s);
        lb->setTextFormat(Qt::RichText);
        step_lbls[st.key] = lb;
        sl->addWidget(lb);
    }
    auto *row = new QHBoxLayout();
    back_btn = new QPushButton(u"◂ Back"_s);
    back_btn->setToolTip(u"Remove the last point placed"_s);
    connect(back_btn, &QPushButton::clicked, this, [this] { back(); });
    next_btn = new QPushButton(u"Next ▸"_s);
    connect(next_btn, &QPushButton::clicked, this, [this] { next_step(); });
    auto_btn = new QPushButton(u"Auto source"_s);
    auto_btn->setToolTip(u"Let the generator place this branch's source (Branch length away)"_s);
    connect(auto_btn, &QPushButton::clicked, this, [this] { auto_source(); });
    cancel_btn = new QPushButton(u"✕ Cancel"_s);
    connect(cancel_btn, &QPushButton::clicked, this, [this] { cancel_guide(); });
    for (QPushButton *b : {back_btn, next_btn, auto_btn, cancel_btn})
        row->addWidget(b, 1);
    sl->addLayout(row);
    gen_btn = new QPushButton(u"▶  Generate river system"_s);
    gen_btn->setObjectName(u"primary"_s);
    connect(gen_btn, &QPushButton::clicked, this, [this] { generate_now(); });
    sl->addWidget(gen_btn);
    s->lay->addWidget(steps_host);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(widgets::label(u"Meander"_s, u"sub"_s, false), 0, 0);
    meander = new widgets::SliderField(0, 10, 10, 5);
    meander->setToolTip(u"0 = the most direct way, 10 = very winding (a generated river system follows at once)"_s);
    connect(meander, &widgets::SliderField::valueChanged, this, [this](double) { regenerate(false, true); });
    grid->addWidget(meander, 0, 1);
    grid->addWidget(widgets::label(u"Width"_s, u"sub"_s, false), 1, 0);
    width = new QComboBox();
    for (const auto &[k, t] : WIDTH_STYLES)
        width->addItem(t, k);
    width->setCurrentIndex(width->findData(u"widen"_s));
    connect(width, &QComboBox::activated, this, [this](int) { regenerate(false, true); });
    grid->addWidget(width, 1, 1);
    grid->addWidget(widgets::label(u"Branch length"_s, u"sub"_s, false), 2, 0);
    blen = new widgets::SliderField(8, 200, 1000, 40, u"px"_s, true);
    blen->setToolTip(u"Length of branches whose source the generator places (Auto source). Changing it re-lays the "
                     u"selected branch at once if its source is automatic."_s);
    connect(blen, &widgets::SliderField::valueChanged, this, [this](double v) { _branch_len(v); });
    grid->addWidget(blen, 2, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    done_host = new QWidget();
    row = new QHBoxLayout(done_host);
    row->setContentsMargins(0, 0, 0, 0);
    regen_btn = new QPushButton(u"⟳ Regenerate"_s);
    regen_btn->setObjectName(u"primary"_s);
    regen_btn->setToolTip(u"Lay the whole river system out again (new random course, same points)"_s);
    connect(regen_btn, &QPushButton::clicked, this, [this] { regenerate(true); });
    edit_btn = new QPushButton(u"＋ Points / branches"_s);
    edit_btn->setToolTip(u"Place more points or branches, then generate again"_s);
    connect(edit_btn, &QPushButton::clicked, this, [this] { edit_points(); });
    done_btn = new QPushButton(u"Done"_s);
    done_btn->setToolTip(u"Keep this river system (its points disappear)"_s);
    connect(done_btn, &QPushButton::clicked, this, [this] { done(); });
    del_btn = new QPushButton(u"Delete"_s);
    del_btn->setToolTip(u"Remove this river system from the map"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { delete_plan(); });
    for (QPushButton *b : {regen_btn, edit_btn, done_btn, del_btn})
        row->addWidget(b, 1);
    s->lay->addWidget(done_host);
    // the river of the system you work on: pick it by clicking one of its points (or ◂ ▸)
    cur_host = new QWidget();
    auto *cl = new QVBoxLayout(cur_host);
    cl->setContentsMargins(0, 4, 0, 0);
    cl->setSpacing(4);
    cur_info = widgets::label(QString(), u"sub"_s);
    cur_info->setTextFormat(Qt::RichText);
    cl->addWidget(cur_info);
    row = new QHBoxLayout();
    prev_node = new QPushButton(u"◂"_s);
    prev_node->setToolTip(u"Previous point"_s);
    prev_node->setFixedWidth(36);
    connect(prev_node, &QPushButton::clicked, this, [this] { step_node(-1); });
    next_node = new QPushButton(u"▸"_s);
    next_node->setToolTip(u"Next point"_s);
    next_node->setFixedWidth(36);
    connect(next_node, &QPushButton::clicked, this, [this] { step_node(1); });
    addpt_btn = new QPushButton(u"＋ Point"_s);
    addpt_btn->setToolTip(u"Then click the map: a point the highlighted river must pass"_s);
    connect(addpt_btn, &QPushButton::clicked, this, [this] { arm(u"point"_s); });
    addbr_btn = new QPushButton(u"＋ Branch"_s);
    addbr_btn->setToolTip(u"Then click where a new branch joins the highlighted river, then its source"_s);
    connect(addbr_btn, &QPushButton::clicked, this, [this] { arm(u"branch"_s); });
    rmnode_btn = new QPushButton(u"✕ Point"_s);
    rmnode_btn->setToolTip(u"Remove the selected point (a branch's join point removes the whole branch)"_s);
    connect(rmnode_btn, &QPushButton::clicked, this, [this] { remove_node(); });
    auto2_btn = new QPushButton(u"Auto source"_s);
    connect(auto2_btn, &QPushButton::clicked, this, [this] { auto_source_armed(); });
    for (QPushButton *b : {prev_node, next_node, addpt_btn, addbr_btn, rmnode_btn, auto2_btn})
        row->addWidget(b, b == prev_node || b == next_node ? 0 : 1);
    cl->addLayout(row);
    s->lay->addWidget(cur_host);
    plan_info = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(plan_info);
    plan_help = widgets::label(u"After generating: drag a point to move it (the rivers follow) · right-click the map "
                               u"to add a point the river must pass · right-click a point to remove it."_s,
                               u"faint"_s);
    s->lay->addWidget(plan_help);
    lay->addWidget(s);

    // --- select
    s = sel_section = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Click a river: that river (and the rivers flowing into it). Shift+click: the "
                                     u"whole river network. Then regenerate or delete it."_s,
                                     u"faint"_s));
    sel_info = widgets::label(u"Nothing selected."_s, u"sub"_s);
    s->lay->addWidget(sel_info);
    row = new QHBoxLayout();
    sel_regen = new QPushButton(u"⟳ Regenerate selected"_s);
    sel_regen->setObjectName(u"primary"_s);
    sel_regen->setToolTip(u"New course from the same source to the same mouth; the rivers flowing into it keep their "
                          u"sources and join the new course"_s);
    connect(sel_regen, &QPushButton::clicked, this, [this] { regenerate_selected(); });
    sel_del = new QPushButton(u"Delete selected"_s);
    connect(sel_del, &QPushButton::clicked, this, [this] { delete_selected(); });
    sel_clear = new QPushButton(u"Clear"_s);
    connect(sel_clear, &QPushButton::clicked, this, [this] { set_selection(std::nullopt); });
    for (QPushButton *b : {sel_regen, sel_del, sel_clear})
        row->addWidget(b, 1);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- pen / eraser
    s = pen_section = new widgets::Section();
    s->lay->addWidget(widgets::label(u"River colours"_s));
    row = new QHBoxLayout();
    row->setSpacing(4);
    const auto disp = display_colors();
    std::vector<std::pair<int, QString>> vals{{SOURCE, u"S"_s}, {MERGE, u"M"_s}};
    for (int w : WIDTHS)
        vals.emplace_back(w, QString::number(w));
    for (const auto &[v, t] : vals) {
        auto *b = new QPushButton(t);
        b->setFixedSize(30, 28);
        const auto &c = disp[size_t(v)];
        b->setStyleSheet(u"QPushButton { background: rgb(%1,%2,%3); color: #111; border: 1px solid %4; "
                         u"border-radius: 3px; padding: 0; font-size: 10px; } QPushButton:checked { border: 3px "
                         u"solid %5; font-weight: 700; }"_s
                             .arg(c[0]).arg(c[1]).arg(c[2])
                             .arg(theme::LINE, theme::GOLD));
        b->setCheckable(true);
        const int vv = v;
        connect(b, &QPushButton::clicked, this, [this, vv] { set_value(vv); });
        sw[v] = b;
        row->addWidget(b);
    }
    row->addStretch(1);
    s->lay->addLayout(row);
    val_info = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(val_info);
    scope_row = new QWidget();
    auto *r1 = new QHBoxLayout(scope_row);
    r1->setContentsMargins(0, 0, 0, 0);
    r1->addWidget(widgets::label(u"Draw on"_s, u"sub"_s, false));
    pen_scope = new QComboBox();
    pen_scope->addItem(u"Land only"_s, u"land"_s);
    pen_scope->addItem(u"Everything (also sea / no province)"_s, u"all"_s);
    pen_scope->addItem(u"Only existing river pixels (change their colour)"_s, u"rivers"_s);
    r1->addWidget(pen_scope, 1);
    s->lay->addWidget(scope_row);
    size_row = new QWidget();
    auto *r2 = new QHBoxLayout(size_row);
    r2->setContentsMargins(0, 0, 0, 0);
    r2->addWidget(widgets::label(u"Eraser size"_s, u"sub"_s, false));
    size_spin = new widgets::SliderField(1, 40, 200, 5, u"px"_s, true);
    connect(size_spin, &widgets::SliderField::valueChanged, this, [this](double) { handler.preview(); });
    r2->addWidget(size_spin, 1);
    s->lay->addWidget(size_row);
    lay->addWidget(s);

    s = new widgets::Section();
    QLabel *legend = widgets::label(u"On this map: green = source · blue = river (lighter = thinner) · orange = merge "
                                    u"(where a river flows into another) · <b style='color:#ff0000'>red = a pixel "
                                    u"that breaks a rule</b> (see the Errors tab) · yellow = selected."_s,
                                    u"faint"_s);
    s->lay->addWidget(legend);
    legend->setTextFormat(Qt::RichText);
    status = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(status);
    lay->addWidget(s);
    set_value(4);
    _tool_changed();
}

// ------------------------------------------------------------------ state
layerbmp::IndexedBitmap &RiverBox::rb() const
{
    return doc->map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s);
}

RiverPlan &RiverBox::plan() const
{
    return doc->map<RiverPlan>(u"river_plan"_s);
}

QString RiverBox::tool() const
{
    for (const QString &k : tool_keys)
        if (tool_btns.at(k)->isChecked())
            return k;
    return u"system"_s;
}

static QString fmt(long long n) { return widgets::fmt(n); }
static QString plural(long long n, const QString &s = u"s"_s) { return n == 1 ? QString() : s; }

void RiverBox::set_value(int v)
{
    value = v;
    for (const auto &[k, b] : sw)
        b->setChecked(k == v);
    QString what;
    if (v == SOURCE)
        what = u"source (green): the first pixel of a river"_s;
    else if (v == MERGE)
        what = u"merge (orange here, red in the file): the last pixel of a river that flows into another one"_s;
    else
        what = u"width %1 (%2)"_s.arg(v).arg(v <= 3 ? u"thin"_s : v >= 8 ? u"wide"_s : u"medium"_s);
    val_info->setText(u"Pen colour: %1 — %2 · %3 px on the map"_s.arg(v).arg(what, fmt(counts[size_t(v)])));
}

void RiverBox::_tool_changed()
{
    const QString t = tool();
    sys_section->setVisible(t == u"system");
    sel_section->setVisible(t == u"select");
    pen_section->setVisible(t == u"pen" || t == u"eraser" || t == u"fix");
    size_row->setVisible(t == u"eraser" || t == u"fix");
    scope_row->setVisible(t == u"pen");
    handler.cancel();
    armed.reset();
    if (t != u"select" && selected)
        set_selection(std::nullopt);
    _update_highlight();
    _update_ui();
    Q_EMIT overlayChanged();
    Q_EMIT bannerChanged();
}

static std::vector<std::int64_t> bincount(const grid::Grid<std::uint8_t> &g)
{
    std::vector<std::int64_t> c(256, 0);
    for (std::uint8_t v : g.flat())
        ++c[v];
    return c;
}

void RiverBox::refresh(document::Document *doc_)
{
    doc = doc_;
    layerbmp::IndexedBitmap &r = rb();
    if (r.load()) {
        counts = bincount(*r.data);
        long long n = 0;
        for (long long k = 0; k < r.version; ++k)
            n += (long long)r.diffs[size_t(k)].idx.size();
        status->setText(r.version ? u"%1 change%2 · %3 px — written to rivers.bmp on Apply"_s.arg(r.version).arg(
                                        plural(r.version), fmt(n))
                                  : u"No changes to rivers.bmp yet."_s);
    } else {
        status->setText(u"⚠ "_s + r.error.value_or(u"None"_s));
    }
    set_value(value);
    _update_ui();
}

// ------------------------------------------------------------------ the guide
QString RiverBox::banner() const
{
    // Instruction shown at the top of the map while placing points.
    if (!doc || tool() != u"system")
        return QString();
    const RiverPlan &p = plan();
    const auto st = p.stage;
    if (st == u"done"_s && armed) {
        const auto cb = cur_branch();
        const QString name = !cb ? u"the main river"_s : u"branch %1"_s.arg(*cb + 1);
        if (*armed == u"point")
            return u"Click a point %1 must pass (it's highlighted)"_s.arg(name);
        if (*armed == u"branch")
            return u"Click where the new branch joins %1 (highlighted)"_s.arg(name);
        return u"Click the SOURCE of the new branch — or press Auto source"_s;
    }
    if (st == u"source"_s)
        return u"①  Click the SOURCE of the new river (on land)"_s;
    if (st == u"mouth"_s)
        return u"②  Click the MOUTH — on the coast / sea, or on another river it should flow into"_s;
    if (st == u"via"_s)
        return u"③  Optional: click points the river must pass (in order) — then Next ▸ or ▶ Generate"_s;
    if (st == u"branches"_s) {
        if (!p.branches.empty() && p.branches.back().await_)
            return u"④  Now click the SOURCE of this branch — or press Auto source"_s;
        return u"④  Optional: click where a branch joins the river, then its source — then ▶ Generate"_s;
    }
    return QString();
}

static QString num(double v) { return QString::number((long long)v); }   // "%d" of a float: truncated

void RiverBox::_update_ui()
{
    if (!doc)
        return;
    const RiverPlan &p = plan();
    const auto st = p.stage;
    const bool guiding = st && GUIDE.contains(*st);
    new_btn->setVisible(!guiding);
    steps_host->setVisible(guiding);
    done_host->setVisible(st == u"done"_s);
    plan_help->setVisible(st == u"done"_s);
    cur_host->setVisible(st == u"done"_s);
    if (st == u"done"_s) {
        const auto cb = cur_branch();
        const QString name = !cb ? u"the main river"_s : u"branch %1"_s.arg(*cb + 1);
        const QString node = cur_node ? describe(cur_node) : QString();
        QString arm_t;
        if (armed == u"point"_s)
            arm_t = u"click the map: a point %1 must pass"_s.arg(name);
        else if (armed == u"branch"_s)
            arm_t = u"click where the new branch joins %1"_s.arg(name);
        else if (armed == u"bsrc"_s)
            arm_t = u"click the source of the new branch — or Auto source"_s;
        cur_info->setText(u"Working on <b style='color:%1'>%2</b>%3%4"_s.arg(
            theme::GOLD, name,
            !node.isEmpty() ? u" · %1"_s.arg(node.split(u" ("_s)[0].toLower()) : u" (click one of its points)"_s,
            !arm_t.isEmpty() ? u"<br><b style='color:%1'>→ %2</b>"_s.arg(theme::GOLD, arm_t) : QString()));
        rmnode_btn->setEnabled(cur_node && QStringList{u"via"_s, u"bvia"_s, u"branch"_s, u"bsrc"_s}.contains(cur_node->kind));
        auto2_btn->setVisible(armed == u"bsrc"_s);
        const bool has_nodes = !nodes().empty();
        prev_node->setEnabled(has_nodes);
        next_node->setEnabled(has_nodes);
    }
    const long long cur = guiding ? GUIDE.indexOf(*st) : -1;
    for (long long i = 0; i < (long long)STEPS.size(); ++i) {
        const Step &step = STEPS[size_t(i)];
        QString val;
        if (step.key == u"source") {
            if (p.start)
                val = u"%1, %2"_s.arg(num(p.start->first), num(p.start->second));
        } else if (step.key == u"mouth") {
            if (p.end)
                val = u"%1, %2"_s.arg(num(p.end->first), num(p.end->second));
        } else if (step.key == u"via") {
            if (!p.via.empty())
                val = u"%1 placed"_s.arg(p.via.size());
        } else {
            long long wait = 0;
            for (const Branch &b : p.branches)
                wait += b.await_;
            if (!p.branches.empty())
                val = u"%1 placed"_s.arg(p.branches.size()) + (wait ? u" — waiting for its source"_s : QString());
        }
        QString txt;
        if (i == cur)
            txt = u"<b style='color:%1'>%2. %3</b> — %4%5"_s.arg(theme::GOLD).arg(i + 1).arg(
                step.title, step.hint, !val.isEmpty() ? u" · <b>%1</b>"_s.arg(val) : QString());
        else if (i < cur || !val.isEmpty())
            txt = u"<span style='color:%1'>✔</span> %2. %3 %4"_s.arg(theme::OK).arg(i + 1).arg(
                step.title, !val.isEmpty() ? u"· %1"_s.arg(val) : QString());
        else
            txt = u"<span style='color:%1'>%2. %3 — %4</span>"_s.arg(theme::INK_FAINT).arg(i + 1).arg(step.title, step.hint);
        step_lbls[step.key]->setText(txt);
    }
    back_btn->setEnabled(p.start.has_value());
    const bool waiting = !p.branches.empty() && p.branches.back().await_;
    next_btn->setVisible((st == u"via"_s || st == u"branches"_s) && !waiting);
    next_btn->setText((st == u"via"_s && p.via.empty()) || (st == u"branches"_s && p.branches.empty()) ? u"Skip ▸"_s
                                                                                                     : u"Next ▸"_s);
    if (st == u"branches"_s)
        next_btn->setVisible(false);
    auto_btn->setVisible(st == u"branches"_s && waiting);
    gen_btn->setEnabled(p.start && p.end);
    plan_info->setText(st == u"done"_s ? p.info : QString());
    plan_info->setVisible(st == u"done"_s);
}

void RiverBox::_plan_edit(const QString &label_text, const std::function<void(RiverPlan &)> &fn, bool regenerate_,
                          bool new_seed)
{
    // Change the plan (fn(plan)) — and lay the rivers out again if asked — as one undo step.
    RiverPlan &p = plan();
    std::optional<QString> msg;
    doc->edit(label_text, {},
              [&] {
                  fn(p);
                  if (new_seed) {
                      p.seed = (p.seed * 1103515245 + 12345) % 2147483647;
                      if (!p.seed)
                          p.seed = 1;
                  }
                  if (regenerate_ && p.start && p.end)
                      msg = _generate();
              },
              {}, {}, {u"rivers_bmp"_s, u"river_plan"_s});
    if (cur_node && !node_pos(*cur_node))
        cur_node.reset();                            // its point was removed (or undone)
    if (p.stage != u"done"_s)
        armed.reset();
    _update_ui();
    _update_highlight();
    Q_EMIT overlayChanged();
    Q_EMIT bannerChanged();
    if (msg && !msg->isEmpty())
        Q_EMIT message(*msg);
}

void RiverBox::start_guide()
{
    if (!doc)
        return;
    tool_btns[u"system"_s]->setChecked(true);
    _plan_edit(u"New river system"_s, [](RiverPlan &p) {
        p.clear();                                   // a river system in progress is kept as it is
        p.stage = u"source"_s;
    });
}

void RiverBox::click(double x, double y)
{
    RiverPlan &p = plan();
    const auto st = p.stage;
    if (!st) {
        _plan_edit(u"River source"_s, [&](RiverPlan &q) {
            q.clear();
            q.stage = u"mouth"_s;
            q.start = Pt(x, y);
        });
    } else if (st == u"source"_s) {
        _plan_edit(u"River source"_s, [&](RiverPlan &q) {
            q.start = Pt(x, y);
            q.stage = u"mouth"_s;
        });
    } else if (st == u"mouth"_s) {
        _plan_edit(u"River mouth"_s, [&](RiverPlan &q) {
            q.end = Pt(x, y);
            q.stage = u"via"_s;
        });
    } else if (st == u"via"_s) {
        _plan_edit(u"River: point to pass"_s, [&](RiverPlan &q) { q.via.emplace_back(x, y); });
    } else if (st == u"branches"_s) {
        if (!p.branches.empty() && p.branches.back().await_) {
            _plan_edit(u"Branch source"_s, [&](RiverPlan &q) {
                q.branches.back().source = Pt(x, y);
                q.branches.back().await_ = false;
            });
        } else {
            const long long L = (long long)blen->value();
            _plan_edit(u"Branch"_s, [&](RiverPlan &q) { q.branches.push_back(Branch{Pt(x, y), L, {}, {}, true, {}}); });
        }
    } else if (armed == u"point"_s) {
        armed.reset();
        add_point(cur_branch(), x, y);
    } else if (armed == u"branch"_s) {
        const long long L = (long long)blen->value();
        const auto par = cur_branch();
        armed = u"bsrc"_s;
        _plan_edit(u"Branch"_s, [&](RiverPlan &q) {
            q.branches.push_back(Branch{Pt(x, y), L, {}, par, true, std::vector<Pt>{}});
        });
    } else if (armed == u"bsrc"_s) {
        armed.reset();
        _plan_edit(
            u"Branch source"_s,
            [&](RiverPlan &q) {
                q.branches.back().source = Pt(x, y);
                q.branches.back().await_ = false;
            },
            true);
        cur_node = Tag{u"branch"_s, (long long)plan().branches.size() - 1, std::nullopt};
        _update_highlight();
    } else {
        Q_EMIT message(u"Click one of the river system's points to work on its river, drag points to move them, or "
                       u"＋ New river system for the next one."_s);
    }
}

void RiverBox::next_step()
{
    if (plan().stage == u"via"_s)
        _plan_edit(u"Branches"_s, [](RiverPlan &p) { p.stage = u"branches"_s; });
}

void RiverBox::auto_source()
{
    _plan_edit(u"Branch: automatic source"_s, [](RiverPlan &p) {
        if (!p.branches.empty())
            p.branches.back().await_ = false;
    });
}

void RiverBox::back()
{
    // Remove the last point placed (and step back if that empties a step).
    _plan_edit(u"Back"_s, [](RiverPlan &p) {
        if (p.stage == u"branches"_s) {
            if (!p.branches.empty()) {
                Branch &b = p.branches.back();
                if (b.source && !b.await_) {
                    b.source.reset();
                    b.await_ = true;
                } else {
                    p.branches.pop_back();
                }
            } else {
                p.stage = u"via"_s;
            }
        } else if (p.stage == u"via"_s) {
            if (!p.via.empty()) {
                p.via.pop_back();
            } else {
                p.stage = u"mouth"_s;
                p.end.reset();
            }
        } else if (p.stage == u"mouth"_s) {
            p.stage = u"source"_s;
            p.start.reset();
        }
    });
}

void RiverBox::cancel_guide()
{
    RiverPlan &p = plan();
    _plan_edit(u"Cancel river"_s, [](RiverPlan &q) {
        if (!q.written.isEmpty()) {
            q.stage = u"done"_s;                     // the rivers already on the map stay; only the new points go
            std::vector<Branch> keep;
            for (const Branch &b : q.branches)
                if (!b.await_)
                    keep.push_back(b);
            q.branches = keep;
        } else {
            q.clear();
        }
    });
    Q_EMIT message(p.written.isEmpty() ? u"Cancelled."_s : u"Back to the river system as it is."_s);
}

void RiverBox::generate_now()
{
    _plan_edit(
        u"Generate river system"_s,
        [](RiverPlan &p) {
            for (Branch &b : p.branches)
                b.await_ = false;
            p.stage = u"done"_s;
        },
        true);
}

void RiverBox::edit_points()
{
    _plan_edit(u"Add points / branches"_s, [](RiverPlan &p) { p.stage = u"via"_s; });
}

QString RiverBox::_generate()
{
    // Replace the plan's pixels by a new layout (inside an edit). Returns a message.
    layerbmp::IndexedBitmap &r = rb();
    RiverPlan &p = plan();
    Generator gen(mod, md, r, p, meander->value(), width->currentData());
    auto &tbm = doc->map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s);
    gen.rb_terrain = tbm.load() ? &tbm : nullptr;
    const auto out = gen.run();
    if (!out) {
        p.info = u"⚠ "_s + gen.error.value_or(QString());
        return p.info;
    }
    std::vector<long long> wk, ok;
    for (const auto &[i, v] : p.written)
        wk.push_back(i);
    for (const auto &[i, v] : *out)
        ok.push_back(i);
    grid::PySet<long long> idx = grid::PySet<long long>::copy_of(grid::PySet<long long>::from_keys(wk));
    idx.merge(grid::PySet<long long>::from_keys(ok));   // (set(p.written) | set(out): Python's walk order)
    Flat ii;
    std::vector<std::uint8_t> vv;
    OrderedMap<long long, std::pair<int, int>> written;
    const auto &flat = r.data->flat();
    for (long long i : idx.items()) {
        const std::pair<int, int> *w = p.written.get(i);
        const int before = w ? w->first : int(flat[size_t(i)]);
        ii.push_back(i);
        if (const int *v = out->get(i)) {
            vv.push_back(std::uint8_t(*v));
            written.insert(i, {before, *v});
        } else {
            vv.push_back(std::uint8_t(before));
        }
    }
    r.apply(ii, vv);
    p.written = written;
    const long long x0 = gen.box[0], y0 = gen.box[1];
    std::map<long long, Flat> paths;
    for (const YX &q : gen.main)
        paths[-1].push_back((q.first + y0) * md.w + (q.second + x0));
    for (size_t i = 0; i < gen.bpaths.size(); ++i)
        if (gen.bpaths[i] && !gen.bpaths[i]->empty())
            for (const YX &q : *gen.bpaths[i])
                paths[(long long)i].push_back((q.first + y0) * md.w + (q.second + x0));
    p.paths = paths;
    const auto &br = gen.branch_info;
    QStringList bl;
    for (const auto &b : br)
        bl << (b && *b ? u"%1 px"_s.arg(*b) : u"⚠ none (no room)"_s);
    p.info = u"River system: %1 px to the %2%3 · seed %4"_s.arg(gen.length).arg(
        gen.mouth, br.empty() ? QString() : u" · branches: %1"_s.arg(bl.join(u", "_s)), QString::number(p.seed));
    return u"River system laid out: %1 px into the %2%3. ⟳ Regenerate re-rolls it; Ctrl+Z undoes it."_s.arg(gen.length).arg(
        gen.mouth, br.empty() ? QString() : u" + %1 branch%2"_s.arg(br.size()).arg(plural((long long)br.size(), u"es"_s)));
}

void RiverBox::regenerate(bool new_seed, bool)
{
    if (!doc || plan().stage != u"done"_s || !plan().start || !plan().end)
        return;
    _plan_edit(u"Regenerate river system"_s, [](RiverPlan &) {}, true, new_seed);
}

static double hy(const Pt &a, double x, double y) { return py::hypot(a.first - x, a.second - y); }

void RiverBox::add_via(double x, double y)
{
    RiverPlan &p = plan();
    if (!p.start || !p.end)
        return;
    std::vector<Pt> pts{*p.start};
    pts.insert(pts.end(), p.via.begin(), p.via.end());
    pts.push_back(*p.end);
    size_t best = 0;
    double bd = 0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const double d = hy(pts[i], x, y) + hy(pts[i + 1], x, y) - hy(pts[i], pts[i + 1].first, pts[i + 1].second);
        if (i == 0 || d < bd) {
            bd = d;
            best = i;
        }
    }
    _plan_edit(u"River: point to pass"_s, [&](RiverPlan &q) { q.via.insert(q.via.begin() + qsizetype(best), Pt(x, y)); },
               p.stage == u"done"_s);
}

void RiverBox::move_point(const Tag &tag, double x, double y)
{
    const QString kind = tag.kind;
    const long long i = tag.i;
    _plan_edit(
        u"Move river point"_s,
        [&](RiverPlan &p) {
            if (kind == u"start") {
                p.start = Pt(x, y);
            } else if (kind == u"end") {
                p.end = Pt(x, y);
                p.mouth.reset();
            } else if (kind == u"via") {
                p.via[size_t(i)] = Pt(x, y);
            } else if (kind == u"branch") {
                p.branches[size_t(i)].at = Pt(x, y);
            } else if (kind == u"bsrc") {
                p.branches[size_t(i)].source = Pt(x, y);
            } else if (kind == u"bvia") {
                std::vector<Pt> v = p.branches[size_t(i)].via.value_or(std::vector<Pt>{});
                v[size_t(*tag.j)] = Pt(x, y);
                p.branches[size_t(i)].via = v;
            }
        },
        plan().stage == u"done"_s);
}

void RiverBox::remove_point(const Tag &tag)
{
    const QString kind = tag.kind;
    const long long i = tag.i;
    const bool is_done = plan().stage == u"done"_s;
    auto drop_branch = [i](RiverPlan &p) {
        std::set<long long> gone{i};
        for (long long j = 0; j < (long long)p.branches.size(); ++j)   // branches of a removed branch go too
            if (p.branches[size_t(j)].parent && gone.count(*p.branches[size_t(j)].parent))
                gone.insert(j);
        std::vector<long long> keep;
        for (long long j = 0; j < (long long)p.branches.size(); ++j)
            if (!gone.count(j))
                keep.push_back(j);
        std::map<long long, long long> remap;
        for (size_t n = 0; n < keep.size(); ++n)
            remap[keep[n]] = (long long)n;
        std::vector<Branch> out;
        for (long long j : keep) {
            Branch b = p.branches[size_t(j)];
            const auto it = b.parent ? remap.find(*b.parent) : remap.end();
            b.parent = it != remap.end() ? std::optional<long long>(it->second) : std::nullopt;
            out.push_back(b);
        }
        p.branches = out;
    };
    if (kind == u"via") {
        _plan_edit(u"Remove point"_s, [i](RiverPlan &p) { p.via.erase(p.via.begin() + qsizetype(i)); }, is_done);
    } else if (kind == u"bvia") {
        const long long j = *tag.j;
        _plan_edit(
            u"Remove point"_s,
            [i, j](RiverPlan &p) {
                std::vector<Pt> v = p.branches[size_t(i)].via.value_or(std::vector<Pt>{});
                v.erase(v.begin() + qsizetype(j));
                p.branches[size_t(i)].via = v;
            },
            is_done);
    } else if (kind == u"branch" || kind == u"bsrc") {
        _plan_edit(u"Remove branch"_s, drop_branch, is_done);
    }
}

// --- the river you work on, its points
std::optional<long long> RiverBox::cur_branch() const
{
    // None = the main river, i = branch i (from the selected point).
    if (cur_node && (cur_node->kind == u"branch" || cur_node->kind == u"bsrc" || cur_node->kind == u"bvia") &&
        cur_node->i < (long long)plan().branches.size())
        return cur_node->i;
    return std::nullopt;
}

std::vector<Tag> RiverBox::nodes() const
{
    // All points of the river system in order: main river first (source, points, mouth), then each branch (join, its
    // points, its source).
    const RiverPlan &p = plan();
    std::vector<Tag> out;
    if (p.start)
        out.push_back({u"start"_s, 0, {}});
    for (long long i = 0; i < (long long)p.via.size(); ++i)
        out.push_back({u"via"_s, i, {}});
    if (p.end)
        out.push_back({u"end"_s, 0, {}});
    for (long long i = 0; i < (long long)p.branches.size(); ++i) {
        const Branch &b = p.branches[size_t(i)];
        out.push_back({u"branch"_s, i, {}});
        if (b.via)
            for (long long j = 0; j < (long long)b.via->size(); ++j)
                out.push_back({u"bvia"_s, i, j});
        if (b.source)
            out.push_back({u"bsrc"_s, i, {}});
    }
    return out;
}

std::optional<Pt> RiverBox::node_pos(const Tag &tag) const
{
    const RiverPlan &p = plan();
    const QString &k = tag.kind;
    if (k == u"start")
        return p.start;
    if (k == u"end")
        return p.end;
    if (k == u"via")
        return tag.i < (long long)p.via.size() ? std::optional<Pt>(p.via[size_t(tag.i)]) : std::nullopt;
    if (tag.i < 0 || tag.i >= (long long)p.branches.size())
        return std::nullopt;
    const Branch &b = p.branches[size_t(tag.i)];
    if (k == u"branch")
        return b.at;
    if (k == u"bsrc")
        return b.source;
    if (k == u"bvia") {
        if (!b.via || !tag.j || *tag.j >= (long long)b.via->size())
            return std::nullopt;
        return (*b.via)[size_t(*tag.j)];
    }
    return std::nullopt;
}

void RiverBox::select_node(const Tag &tag, bool center)
{
    cur_node = tag;
    _update_ui();
    _update_highlight();
    Q_EMIT overlayChanged();
    Q_EMIT bannerChanged();
    const auto pos = node_pos(tag);
    if (center && pos && panel)
        panel->zoom_requested(pos->first, pos->second);
}

void RiverBox::step_node(int d)
{
    const auto ns = nodes();
    if (ns.empty())
        return;
    long long i = d > 0 ? -1 : 0;
    if (cur_node) {
        const auto it = std::find(ns.begin(), ns.end(), *cur_node);
        if (it != ns.end())
            i = it - ns.begin();
    }
    select_node(ns[size_t(py::mod(i + d, (long long)ns.size()))], true);
}

void RiverBox::arm(const QString &what)
{
    if (plan().stage != u"done"_s)
        return;
    armed = what;
    _update_ui();
    Q_EMIT bannerChanged();
}

void RiverBox::auto_source_armed()
{
    armed.reset();
    _plan_edit(
        u"Branch: automatic source"_s,
        [](RiverPlan &p) {
            if (!p.branches.empty())
                p.branches.back().await_ = false;
        },
        true);
}

void RiverBox::add_point(std::optional<long long> cb, double x, double y)
{
    // A point the river cb (None = main) must pass, put where it fits best along that river's points.
    if (!cb) {
        add_via(x, y);
        return;
    }
    const Branch &b = plan().branches[size_t(*cb)];
    std::vector<Pt> chain;
    if (b.source)
        chain.push_back(*b.source);
    if (b.via)
        chain.insert(chain.end(), b.via->begin(), b.via->end());
    chain.push_back(b.at);
    long long pos = 0;
    if (chain.size() >= 2) {
        double bd = 0;
        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            const double d =
                hy(chain[i], x, y) + hy(chain[i + 1], x, y) - hy(chain[i], chain[i + 1].first, chain[i + 1].second);
            if (i == 0 || d < bd) {
                bd = d;
                pos = (long long)i;
            }
        }
        if (!b.source)
            pos += 1;                                // chain has no source: index shifts by one
        pos = std::max(0LL, pos);
    }
    const long long c = *cb;
    _plan_edit(
        u"Branch: point to pass"_s,
        [c, pos, x, y](RiverPlan &p) {
            std::vector<Pt> v = p.branches[size_t(c)].via.value_or(std::vector<Pt>{});
            v.insert(v.begin() + qsizetype(std::min(pos, (long long)v.size())), Pt(x, y));
            p.branches[size_t(c)].via = v;
        },
        true);
}

void RiverBox::remove_node()
{
    if (cur_node) {
        const Tag tag = *cur_node;
        cur_node.reset();
        remove_point(tag);
    }
}

void RiverBox::_branch_len(double v)
{
    // Branch length: the selected branch (if its source is automatic) follows at once.
    if (!doc || plan().stage != u"done"_s)
        return;
    const auto cb = cur_branch();
    if (!cb || plan().branches[size_t(*cb)].source)
        return;
    const long long c = *cb, L = (long long)v;
    _plan_edit(u"Branch length"_s, [c, L](RiverPlan &p) { p.branches[size_t(c)].length = L; }, true);
}

void RiverBox::_update_highlight()
{
    // The river you work on is shown yellow (only once one of its points was picked).
    if (!doc)
        return;
    const RiverPlan &p = plan();
    std::optional<Flat> idx;
    if (tool() == u"system" && p.stage == u"done"_s && cur_node) {
        const auto cb = cur_branch();
        const auto it = p.paths.find(cb ? *cb : -1);
        if (it != p.paths.end() && !it->second.empty())
            idx = it->second;
    }
    if (tool() == u"system")
        _set_highlight(idx);
}

void RiverBox::_redraw(const std::optional<Flat> &old)
{
    long long mnx = 0, mny = 0, mxx = -1, mxy = -1;
    bool any_px = false;
    for (const std::optional<Flat> *a : {&old, const_cast<const std::optional<Flat> *>(&selected)})
        if (*a)
            for (std::int64_t f : **a) {
                const long long ys = f / md.w, xs = f % md.w;
                if (!any_px) {
                    mnx = mxx = xs;
                    mny = mxy = ys;
                    any_px = true;
                }
                mnx = std::min(mnx, xs);
                mxx = std::max(mxx, xs);
                mny = std::min(mny, ys);
                mxy = std::max(mxy, ys);
            }
    if (any_px && panel && panel->view)
        panel->view->redraw_box({mnx, mny, mxx + 1, mxy + 1});
}

void RiverBox::_set_highlight(const std::optional<Flat> &idx)
{
    const auto old = selected;
    selected = idx && !idx->empty() ? idx : std::nullopt;
    _redraw(old);
}

void RiverBox::done()
{
    if (!doc)
        return;
    cur_node.reset();
    armed.reset();
    _plan_edit(u"River system done"_s, [](RiverPlan &p) { p.clear(); });
    Q_EMIT message(u"River system kept. ＋ New river system for the next one."_s);
}

void RiverBox::delete_plan()
{
    layerbmp::IndexedBitmap &r = rb();
    RiverPlan &p = plan();
    if (p.written.isEmpty() && !p.active())
        return;
    _plan_edit(u"Delete river system"_s, [&r](RiverPlan &q) {
        if (!q.written.isEmpty()) {
            Flat ii;
            std::vector<std::uint8_t> vv;
            for (const auto &[i, ov] : q.written) {
                ii.push_back(i);
                vv.push_back(std::uint8_t(ov.first));
            }
            r.apply(ii, vv);
        }
        q.clear();
    });
    Q_EMIT message(u"River system removed. Ctrl+Z brings it back."_s);
}

// ------------------------------------------------------------------ select
bool RiverBox::_is_sea(YX q) const
{
    const std::uint16_t pid = md.idmap(q.first, q.second);
    return (md.sea[pid] && pid != 0) || (*rb().data)(q.first, q.second) == 254;
}

void RiverBox::select_at(double x, double y, bool network)
{
    const grid::Grid<std::uint8_t> &R = *rb().data;
    const long long xi = (long long)x, yi = (long long)y;
    std::optional<std::pair<long long, YX>> best;
    for (long long dy = -3; dy < 4; ++dy)
        for (long long dx = -3; dx < 4; ++dx) {
            const long long yy = yi + dy, xx = xi + dx;
            if (0 <= yy && yy < md.h && 0 <= xx && xx < md.w && R(yy, xx) < 12) {
                const long long d = dx * dx + dy * dy;
                if (!best || d < best->first)
                    best = std::pair{d, YX{yy, xx}};
            }
        }
    if (!best) {
        set_selection(std::nullopt);
        Q_EMIT message(u"No river there — click right on a river (or within 3 px of it)."_s);
        return;
    }
    const auto comp = trace_network(R, best->second.first, best->second.second);
    auto [branches, owner] = split_branches(R, comp, [this](YX q) { return _is_sea(q); });
    if (branches.empty()) {
        set_selection(std::nullopt);
        Q_EMIT message(u"⚠ That river has no source (green pixel), so its course can't be followed — fix it with the "
                       u"pen first."_s);
        return;
    }
    const auto hit = owner.find(best->second);
    long long root;
    if (network || hit == owner.end()) {
        std::vector<long long> roots;
        for (long long i = 0; i < (long long)branches.size(); ++i)
            if (!branches[size_t(i)].parent)
                roots.push_back(i);
        root = 0;
        if (!roots.empty()) {
            size_t bs = 0;
            root = roots[0];
            for (long long i : roots) {
                const size_t n = subtree(branches, i).size();
                if (n > bs) {
                    bs = n;
                    root = i;
                }
            }
        }
    } else {
        root = hit->second;
    }
    const auto members = subtree(branches, root);
    std::set<long long> pix;
    for (long long i : members)
        for (const YX &q : branches[size_t(i)].path)
            pix.insert(q.first * md.w + q.second);
    const Traced &rootb = branches[size_t(root)];
    const QString kind = rootb.end;
    const long long extra = (long long)members.size() - 1;
    long long extra_px = 0;
    for (size_t k = 1; k < members.size(); ++k)
        extra_px += (long long)branches[size_t(members[k])].path.size();
    const long long root_len = (long long)rootb.path.size();
    sel = Selection{std::move(branches), root, members, network};
    set_selection(Flat(pix.begin(), pix.end()));
    const QString ends = kind == u"river" ? u"flows into another river"_s
                                          : kind == u"sea" ? u"flows into the sea"_s : u"ends on land"_s;
    sel_info->setText(u"Selected: %1 of %2 px%3 — %4.%5"_s.arg(network ? u"river network"_s : u"river"_s).arg(root_len).arg(
        extra ? u" + %1 river%2 flowing into it (%3 px)"_s.arg(extra).arg(plural(extra)).arg(extra_px) : QString(), ends,
        kind == u"land" ? u" ⚠ It ends on land, so it can't be regenerated (give it a mouth first)."_s : QString()));
    sel_regen->setEnabled(kind != u"land");
}

void RiverBox::set_selection(const std::optional<Flat> &idx)
{
    const auto old = selected;
    selected = idx && !idx->empty() ? idx : std::nullopt;
    if (!selected) {
        sel.reset();
        sel_info->setText(u"Nothing selected."_s);
    }
    sel_regen->setEnabled(selected.has_value());
    sel_del->setEnabled(selected.has_value());
    sel_clear->setEnabled(selected.has_value());
    _redraw(old);
}

void RiverBox::regenerate_selected()
{
    if (!sel)
        return;
    const grid::Grid<std::uint8_t> &R = *rb().data;
    const auto &br = sel->branches;
    const long long root = sel->root;
    const auto members = sel->members;
    const Traced &rb_ = br[size_t(root)];
    if (rb_.end == u"land")
        return;
    std::map<long long, long long> pos;
    for (size_t n = 1; n < members.size(); ++n)
        pos[members[n]] = (long long)n - 1;
    std::vector<Branch> branches;
    for (size_t n = 1; n < members.size(); ++n) {
        const Traced &b = br[size_t(members[n])];
        Branch nb;
        const auto &jq = b.end_at;
        nb.at = jq ? Pt(double(jq->second) + 0.5, double(jq->first) + 0.5)
                   : Pt(double(b.path.back().second) + 0.5, double(b.path.back().first) + 0.5);
        nb.length = (long long)b.path.size();
        nb.source = Pt(double(b.path[0].second) + 0.5, double(b.path[0].first) + 0.5);
        if (b.parent && *b.parent != root) {
            const auto it = pos.find(*b.parent);
            if (it != pos.end())
                nb.parent = it->second;
        }
        nb.await_ = false;
        branches.push_back(nb);
    }
    OrderedMap<long long, std::pair<int, int>> written;
    for (long long i : members)
        for (const YX &q : br[size_t(i)].path) {
            const int before = _is_sea(q) ? 254 : LAND;
            written.insert(q.first * md.w + q.second, {before, int(R(q.first, q.second))});
        }
    const Pt start(double(rb_.path[0].second) + 0.5, double(rb_.path[0].first) + 0.5);
    Pt end;
    QString mouth;
    if (rb_.end == u"river") {
        end = Pt(double(rb_.end_at->second) + 0.5, double(rb_.end_at->first) + 0.5);
        mouth = u"river"_s;
    } else {
        end = Pt(double(rb_.path.back().second) + 0.5, double(rb_.path.back().first) + 0.5);
        mouth = u"sea"_s;
    }
    const long long seed = random_seed();
    set_selection(std::nullopt);
    tool_btns[u"system"_s]->setChecked(true);
    _plan_edit(
        u"Regenerate selected river%1"_s.arg(members.size() > 1 ? u"s"_s : QString()),
        [&](RiverPlan &p) {
            p.clear();
            p.start = start;
            p.end = end;
            p.mouth = mouth;
            p.branches = branches;
            p.written = written;
            p.seed = seed;
            p.stage = u"done"_s;
        },
        true);
}

void RiverBox::delete_selected()
{
    if (!selected)
        return;
    layerbmp::IndexedBitmap &r = rb();
    const Flat idx = *selected;
    std::vector<std::uint8_t> vals;
    for (std::int64_t i : idx)
        vals.push_back(_is_sea({i / md.w, i % md.w}) ? 254 : LAND);
    const long long n = (long long)idx.size();
    set_selection(std::nullopt);
    doc->edit(u"Delete river%1"_s.arg(n > 1 ? u"s"_s : QString()), {}, [&] { r.apply(idx, vals); }, {}, {},
              {u"rivers_bmp"_s});
    counts = bincount(*r.data);
    Q_EMIT message(u"Deleted %1 river pixels. Ctrl+Z brings them back."_s.arg(n));
}

// ------------------------------------------------------------------ overlay
std::vector<mapview::Shape> RiverBox::shapes() const
{
    if (!doc || tool() != u"system")
        return {};
    const RiverPlan &p = plan();
    std::vector<mapview::Shape> out;
    auto pt = [](const Pt &q) { return QPointF(q.first, q.second); };
    auto shape = [&](const QString &kind, std::vector<QPointF> pts, const Rgb &color) {
        mapview::Shape s;
        s.kind = kind;
        s.pts = std::move(pts);
        s.color = color;
        return s;
    };
    const bool guiding = p.stage && GUIDE.contains(*p.stage);
    if (guiding && p.start) {                        // the planned course, before it is generated
        std::vector<QPointF> course{pt(*p.start)};
        for (const Pt &v : p.via)
            course.push_back(pt(v));
        if (p.end)
            course.push_back(pt(*p.end));
        if (course.size() > 1) {
            auto s = shape(u"line"_s, course, {235, 235, 235});
            s.width = 2;
            s.dash = true;
            out.push_back(s);
        }
    }
    for (const Branch &b : p.branches)
        if (b.source) {
            auto s = shape(u"line"_s, {pt(b.at), pt(*b.source)}, {245, 170, 40});
            s.width = 2;
            s.dash = true;
            out.push_back(s);
        }
    auto marker = [&](const QString &kind, const Pt &q, const Rgb &c, double size, const Tag &tag) {
        auto s = shape(kind, {pt(q)}, c);
        s.size = size;
        s.tag = tag.list();
        s.halo = true;
        out.push_back(s);
    };
    if (p.start)
        marker(u"dot"_s, *p.start, {70, 235, 100}, 7, {u"start"_s, 0, {}});
    if (p.end)
        marker(u"dot"_s, *p.end, {90, 150, 255}, 7, {u"end"_s, 0, {}});
    for (long long i = 0; i < (long long)p.via.size(); ++i)
        marker(u"diamond"_s, p.via[size_t(i)], {245, 245, 245}, 6.5, {u"via"_s, i, {}});
    for (long long i = 0; i < (long long)p.branches.size(); ++i) {
        const Branch &b = p.branches[size_t(i)];
        marker(u"diamond"_s, b.at, {245, 170, 40}, 6.5, {u"branch"_s, i, {}});
        if (b.via)
            for (long long j = 0; j < (long long)b.via->size(); ++j)
                marker(u"diamond"_s, (*b.via)[size_t(j)], {245, 245, 245}, 5.5, {u"bvia"_s, i, j});
        if (b.source)
            marker(u"dot"_s, *b.source, {70, 235, 100}, 5.5, {u"bsrc"_s, i, {}});
    }
    const auto pos = cur_node ? node_pos(*cur_node) : std::nullopt;
    if (pos) {
        auto s = shape(u"circle"_s, {pt(*pos)}, {255, 215, 60});
        s.radius = 3.2;
        s.width = 3.0;
        out.push_back(s);
    }
    return out;
}

QString RiverBox::describe(const std::optional<Tag> &tag) const
{
    if (!tag)
        return QString();
    static const std::map<QString, QString> D = {
        {u"start"_s, u"River source (drag to move)"_s},
        {u"end"_s, u"River mouth (drag to move)"_s},
        {u"via"_s, u"Point the river must pass (drag to move, right-click to remove)"_s},
        {u"branch"_s, u"A branch joins here (drag to move, right-click to remove the branch)"_s},
        {u"bsrc"_s, u"Branch source (drag to move)"_s},
        {u"bvia"_s, u"Point the branch must pass (drag to move, right-click to remove)"_s}};
    const auto it = D.find(tag->kind);
    return it == D.end() ? QString() : it->second;
}

// ------------------------------------------------------------------ pen / eraser
std::vector<std::uint8_t> RiverBox::land_mask(const Flat &idx) const
{
    // Pixels the pen may draw on, by its "Draw on" choice.
    const QString sc = pen_scope->currentData().toString();
    const auto &flat = rb().data->flat();
    std::vector<std::uint8_t> ok(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        const size_t i = size_t(idx[k]);
        if (sc == u"all") {
            ok[k] = 1;
        } else if (sc == u"rivers") {
            ok[k] = flat[i] < 12;
        } else {
            const std::uint16_t id = md.idmap.flat()[i];
            ok[k] = id != 0 && !md.sea[id] && flat[i] != 254;
        }
    }
    return ok;
}

void RiverBox::commit(const QString &label_text, Flat idx, std::vector<std::uint8_t> nw, const QString &msg)
{
    layerbmp::IndexedBitmap &r = rb();
    {
        Flat ki;
        std::vector<std::uint8_t> kv;
        for (size_t k = 0; k < idx.size(); ++k)
            if (r.data->flat()[size_t(idx[k])] != nw[k]) {
                ki.push_back(idx[k]);
                kv.push_back(nw[k]);
            }
        idx = std::move(ki);
        nw = std::move(kv);
    }
    if (idx.empty())
        return;
    doc->edit(label_text, {}, [&] { r.apply(idx, nw); }, {}, {}, {u"rivers_bmp"_s});
    counts = bincount(*r.data);
    Q_EMIT message(msg + u" Ctrl+Z undoes it."_s);
}

QString RiverBox::pixel_info(long long x, long long y) const
{
    if (!doc || !rb().data || !(0 <= x && x < md.w && 0 <= y && y < md.h))
        return QString();
    const int v = (*rb().data)(y, x);
    QString what;
    if (v == SOURCE)
        what = u"source"_s;
    else if (v == MERGE)
        what = u"merge (flows into another river)"_s;
    else if (v == LAND)
        what = u"no river"_s;
    else if (v == 254)
        what = u"sea / no land"_s;
    else
        what = std::find(WIDTHS.begin(), WIDTHS.end(), v) != WIDTHS.end() ? u"river width %1"_s.arg(v)
                                                                          : u"value %1"_s.arg(v);
    return u"rivers.bmp %1 · %2"_s.arg(v).arg(what);
}

// ---------------------------------------------------------------------------------------------- RiverHandler
mapview::MapView *RiverHandler::view() const
{
    return b->panel ? b->panel->view : nullptr;
}

bool RiverHandler::cancel()
{
    const bool had = touched.has_value() || drag.has_value();
    if (touched && !touched->isEmpty()) {
        Flat idx;
        std::vector<std::uint8_t> old;
        for (const auto &[i, c] : *touched) {
            idx.push_back(i);
            old.push_back(std::uint8_t(c));
        }
        b->rb().live(idx, old);
    }
    touched.reset();
    last.reset();
    drag.reset();
    press_pt.reset();
    if (view())
        view()->shape_layer->set_temp({});
    return had;
}

void RiverHandler::_pen_to(double x, double y)
{
    const mapview::MapData &md = b->md;
    const long long xi = (long long)x, yi = (long long)y;
    const long long lx = (long long)last->first, ly = (long long)last->second;
    std::vector<std::pair<long long, long long>> pts;
    if (lx != xi || ly != yi)
        pts = layertools::line4(lx, ly, xi, yi);
    else
        pts = {{xi, yi}};
    Flat idx;
    for (const auto &[px_, py_] : pts)
        if (0 <= px_ && px_ < md.w && 0 <= py_ && py_ < md.h)
            idx.push_back(py_ * md.w + px_);
    last = Pt(double(xi), double(yi));
    if (idx.empty())
        return;
    const auto ok = b->land_mask(idx);
    Flat keep;
    for (size_t k = 0; k < idx.size(); ++k)
        if (ok[k] && !touched->contains(idx[k]))
            keep.push_back(idx[k]);
    if (keep.empty())
        return;
    const auto &flat = b->rb().data->flat();
    for (std::int64_t i : keep)
        touched->insert(i, flat[size_t(i)]);
    b->rb().live(keep, std::vector<std::uint8_t>(keep.size(), std::uint8_t(b->value)));
}

void RiverHandler::_fix_at(double x, double y)
{
    // The Fix brush (the tester, 2026-10-15): every pixel under it where rivers.bmp disagrees with the provinces is
    // made right — water under land becomes land (255), land / a river on a sea tile becomes water (254).
    const mapview::MapData &md = b->md;
    mismatch::Mismatch *mm = b->panel ? b->panel->mismatch() : nullptr;
    if (!mm)
        return;
    Flat idx;
    for (std::int64_t i : layertools::disc(x, y, b->size_spin->value(), md.w, md.h))
        if (!touched->contains(i))
            idx.push_back(i);
    if (idx.empty())
        return;
    for (const auto &f : mm->fix({u"r_water"_s, u"r_sea"_s}, idx)) {
        const auto &flat = b->rb().data->flat();
        for (std::int64_t k : f.idx)
            touched->insert(k, flat[size_t(k)]);
        b->rb().live(f.idx, f.vals);
    }
}

void RiverHandler::_erase_at(double x, double y)
{
    const mapview::MapData &md = b->md;
    const auto &flat = b->rb().data->flat();
    Flat idx;
    for (std::int64_t i : layertools::disc(x, y, b->size_spin->value(), md.w, md.h))
        if (flat[size_t(i)] < 12 && !touched->contains(i))
            idx.push_back(i);
    if (idx.empty())
        return;
    for (std::int64_t i : idx)
        touched->insert(i, flat[size_t(i)]);
    b->rb().live(idx, std::vector<std::uint8_t>(idx.size(), std::uint8_t(LAND)));
}

bool RiverHandler::press(double x, double y, double tol, Qt::KeyboardModifiers mods)
{
    if (mods & Qt::ControlModifier)
        return false;
    if (!b->doc || !b->rb().data)
        return true;
    const QString t = b->tool();
    if (t == u"system" || t == u"select") {
        drag = t == u"system" ? Tag::of(view()->shape_layer->hit(x, y, tol * 1.4)) : std::nullopt;
        press_pt = Pt(x, y);
        return true;
    }
    touched.emplace();
    if (t == u"pen") {
        last = Pt(double((long long)x), double((long long)y));
        _pen_to(x, y);
    } else if (t == u"fix") {
        last = Pt(x, y);
        _fix_at(x, y);
    } else {
        _erase_at(x, y);
    }
    return true;
}

void RiverHandler::preview()
{
    // Show the eraser at its new size where the mouse last was.
    mapview::MapView *v = view();
    const QString t = b->tool();
    if (!v || !(t == u"eraser" || t == u"fix") || !b->panel->edit_btn->isChecked())
        return;
    Pt pos;
    if (last_pos) {
        pos = *last_pos;
    } else {
        const QPointF c = v->mapToScene(v->viewport()->rect().center());
        pos = {c.x(), c.y()};
    }
    mapview::Shape s;
    s.kind = u"circle"_s;
    s.pts = {QPointF(pos.first, pos.second)};
    s.radius = std::max(0.5, b->size_spin->value() / 2.0);
    s.color = {255, 255, 255};
    v->shape_layer->set_temp({s});
}

void RiverHandler::move(double x, double y)
{
    last_pos = Pt(x, y);
    const QString t = b->tool();
    if (t == u"eraser" || t == u"fix") {
        mapview::Shape s;
        s.kind = u"circle"_s;
        s.pts = {QPointF(x, y)};
        s.radius = std::max(0.5, b->size_spin->value() / 2.0);
        s.color = {255, 255, 255};
        view()->shape_layer->set_temp({s});
    }
    if (drag) {
        mapview::Shape s;
        s.kind = u"dot"_s;
        s.pts = {QPointF(x, y)};
        s.color = {255, 255, 255};
        s.size = 7;
        view()->shape_layer->set_temp({s});
        return;
    }
    if (!touched)
        return;
    if (t == u"pen") {
        _pen_to(x, y);
    } else {
        const auto [lx, ly] = last.value_or(Pt(x, y));
        const long long n =
            std::max(1LL, (long long)(py::hypot(x - lx, y - ly) / std::max(0.5, b->size_spin->value() / 4.0)));
        for (long long i = 1; i <= n; ++i) {
            const double sx = lx + (x - lx) * double(i) / double(n), sy = ly + (y - ly) * double(i) / double(n);
            if (t == u"fix")
                _fix_at(sx, sy);
            else
                _erase_at(sx, sy);
        }
        last = Pt(x, y);
    }
}

static double round1(double v) { return py::round_to(v, 1); }

void RiverHandler::release(double x, double y, double tol, Qt::KeyboardModifiers mods)
{
    const QString t = b->tool();
    if (t == u"system" || t == u"select") {
        const auto dr = drag;
        const auto pp = press_pt;
        drag.reset();
        press_pt.reset();
        view()->shape_layer->set_temp({});
        if (!pp)
            return;
        const bool moved = py::hypot(x - pp->first, y - pp->second) > tol;
        if (t == u"select") {
            if (!moved)
                b->select_at(x, y, bool(mods & Qt::ShiftModifier));
        } else if (dr && moved) {
            b->move_point(*dr, round1(x), round1(y));
        } else if (dr) {
            b->select_node(*dr);                     // a click on a point: work on its river
        } else {
            b->click(round1(x), round1(y));
        }
        return;
    }
    if (!touched)
        return;
    const OrderedMap<long long, long long> tch = std::move(*touched);
    touched.reset();
    last.reset();
    if (tch.isEmpty())
        return;
    Flat idx;
    std::vector<std::uint8_t> old;
    for (const auto &[i, c] : tch) {
        idx.push_back(i);
        old.push_back(std::uint8_t(c));
    }
    auto &flat = b->rb().data->flat();
    std::vector<std::uint8_t> nw(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        nw[k] = flat[size_t(idx[k])];
        flat[size_t(idx[k])] = old[k];
    }
    const long long n = (long long)idx.size();
    if (t == u"pen")
        b->commit(u"River pen %1 px"_s.arg(n), idx, nw,
                  u"Drew %1 river pixel%2 (value %3)."_s.arg(n).arg(plural(n)).arg(b->value));
    else if (t == u"fix")
        b->commit(u"Rivers: %1 px land ↔ water made right"_s.arg(n), idx, nw,
                  u"%1 pixel%2 of rivers.bmp made right (land / water as the provinces say)."_s.arg(n).arg(plural(n)));
    else
        b->commit(u"River eraser %1 px"_s.arg(n), idx, nw, u"Erased %1 river pixel%2."_s.arg(n).arg(plural(n)));
}

bool RiverHandler::rclick(double x, double y, double tol, Qt::KeyboardModifiers)
{
    const QString t = b->tool();
    if (t == u"system") {
        const auto tag = Tag::of(view()->shape_layer->hit(x, y, tol * 1.4));
        if (tag)
            b->remove_point(*tag);
        else if (b->plan().start && b->plan().end)
            b->add_via(round1(x), round1(y));
        return true;
    }
    if (t == u"select")
        return true;
    const mapview::MapData &md = b->md;
    const long long xi = (long long)x, yi = (long long)y;
    if (0 <= xi && xi < md.w && 0 <= yi && yi < md.h) {
        const int v = (*b->rb().data)(yi, xi);
        if (v < 12) {
            b->set_value(v);
            Q_EMIT b->message(u"Pen colour %1 picked."_s.arg(v));
        }
    }
    return true;
}

bool RiverHandler::wheel(double steps, Qt::KeyboardModifiers mods)
{
    const QString t = b->tool();
    if (!(mods & drawlink::BRUSH_KEY) || !(t == u"eraser" || t == u"fix"))   // (C++: Shift; Ctrl + wheel zooms)
        return false;
    widgets::SliderField *s = b->size_spin;
    s->setValue(std::max(1.0, std::min(s->maximum(), s->value() + double(py::round_int(steps)))));
    return true;
}

}  // namespace riverpanel
