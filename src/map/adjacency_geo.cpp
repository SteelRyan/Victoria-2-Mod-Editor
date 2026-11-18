// adjacency.py, the map half: AdjGeometry and the checks. Line by line; numpy's float32 steps in float.
#include "map/adjacency_geo.h"
#include "core/pynum.h"

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Qt::StringLiterals;

namespace adjacency {

AdjGeometry::AdjGeometry(const mapview::MapData &md_, const QSet<long long> &sea_ids) : md(md_)
{
    sea_lut.assign(static_cast<size_t>(md.n), 0);
    for (long long p : sea_ids)
        if (0 < p && p < md.n)
            sea_lut[static_cast<size_t>(p)] = 1;
}

void AdjGeometry::reset()
{
    _px.clear();
    _near.clear();
    _anchors.clear();
    _border.reset();
    _thin.reset();
    _coast_pairs.reset();
    _river_cache.clear();
}

// --- pixels
std::optional<AdjGeometry::Pixels> AdjGeometry::pixels(long long p)
{
    // (xs, ys) of a province (cached for small ones)
    if (auto it = _px.constFind(p); it != _px.cend())
        return *it;
    if (!(0 < p && p < md.n) || md.area[p] == 0)
        return std::nullopt;
    const long long x0 = md.x0[p], y0 = md.y0[p], x1 = md.x1[p] + 1, y1 = md.y1[p] + 1;
    Pixels c;
    for (long long y = y0; y < y1; ++y)
        for (long long x = x0; x < x1; ++x)
            if (md.idmap(y, x) == p) {
                c.first.push_back((static_cast<float>(x - x0) + static_cast<float>(x0)) + 0.5f);
                c.second.push_back((static_cast<float>(y - y0) + static_cast<float>(y0)) + 0.5f);
            }
    if (md.area[p] < 60000)
        _px.insert(p, c);
    return c;
}

std::optional<Pt> AdjGeometry::nearest(long long p, double x, double y)
{
    const std::tuple<long long, long long, long long> key{p, py::trunc_int(x), py::trunc_int(y)};
    if (auto it = _near.find(key); it != _near.end())
        return it->second;
    const auto c = pixels(p);
    if (!c)
        return std::nullopt;
    const auto &[xs, ys] = *c;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fw = static_cast<float>(md.w);
    size_t best = 0;
    float bv = 0;
    for (size_t i = 0; i < xs.size(); ++i) {
        float dx = std::fabs(xs[i] - fx);
        dx = std::min(dx, fw - dx);                  // the map wraps around: the short way counts
        const float dy = ys[i] - fy;
        const float v = dx * dx + dy * dy;
        if (i == 0 || v < bv) {
            best = i;
            bv = v;
        }
    }
    const Pt pt{static_cast<double>(xs[best]), static_cast<double>(ys[best])};
    _near.emplace(key, pt);
    return pt;
}

std::optional<Pt> AdjGeometry::inner(long long p)
{
    // A pixel of p near its centroid.
    if (!(0 < p && p < md.n) || md.area[p] == 0)
        return std::nullopt;
    return nearest(p, md.cx[p] + 0.5, md.cy[p] + 0.5);
}

std::optional<Pt> AdjGeometry::border_point(long long a, long long b)
{
    // A point on the shared border of two adjacent provinces (None if they don't touch).
    if (!_border)
        _build_borders();
    const auto it = _border->constFind({std::min(a, b), std::max(a, b)});
    return it != _border->cend() ? std::optional<Pt>(*it) : std::nullopt;
}

void AdjGeometry::_build_borders()
{
    const auto &idm = md.idmap;
    const long long h = md.h, w = md.w;
    std::vector<std::int64_t> k;
    std::vector<double> xs, ys;
    auto put = [&](std::int64_t a, std::int64_t b, double x, double y) {
        k.push_back(std::min(a, b) * 65536 + std::max(a, b));
        xs.push_back(x);
        ys.push_back(y);
    };
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x + 1 < w; ++x)
            if (idm(y, x + 1) != idm(y, x))
                put(idm(y, x + 1), idm(y, x), static_cast<double>(x) + 1.0, static_cast<double>(y) + 0.5);
    for (long long y = 0; y + 1 < h; ++y)
        for (long long x = 0; x < w; ++x)
            if (idm(y + 1, x) != idm(y, x))
                put(idm(y + 1, x), idm(y, x), static_cast<double>(x) + 0.5, static_cast<double>(y) + 1.0);
    for (long long y = 0; y < h; ++y)                 // horizontal wrap-around (the game's map is a cylinder)
        if (idm(y, 0) != idm(y, w - 1))
            put(idm(y, 0), idm(y, w - 1), 0.0, static_cast<double>(y) + 0.5);
    std::vector<size_t> order(k.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t i, size_t j) { return k[i] < k[j]; });
    _border.emplace();
    for (size_t s = 0; s < order.size();) {
        size_t e = s;
        while (e < order.size() && k[order[e]] == k[order[s]])
            ++e;
        const size_t mid = s + (e - s) / 2;
        const std::int64_t q = k[order[s]];
        _border->insert({q / 65536, q % 65536}, {xs[order[mid]], ys[order[mid]]});
        s = e;
    }
}

std::optional<std::pair<Pt, Pt>> AdjGeometry::anchors(long long a, long long b)
{
    // Two points to draw a line between two (usually non-adjacent) provinces: roughly their closest points.
    const std::pair<long long, long long> key{a, b};
    if (!_anchors.contains(key))
        _anchors.insert(key, _anchors_calc(a, b));
    return _anchors.value(key);
}

std::optional<std::pair<Pt, Pt>> AdjGeometry::_anchors_calc(long long a, long long b)
{
    const auto ib = inner(b);
    if (!ib || !inner(a))
        return std::nullopt;
    auto pa = nearest(a, ib->first, ib->second);
    const auto pb = nearest(b, pa->first, pa->second);
    pa = nearest(a, pb->first, pb->second);
    return std::make_pair(*pa, *pb);
}

// --- checks
const grid::PySet<long long> &AdjGeometry::thin_tiles()
{
    // Sea tiles that are only a pixel or two wide (Meme-river tiles).
    if (!_thin) {
        const auto &idm = md.idmap;
        const long long h = md.h, w = md.w;
        std::vector<std::int64_t> icount(static_cast<size_t>(md.n), 0);
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x) {
                const std::uint16_t v = idm(y, x);
                const bool in = (y == 0 || idm(y - 1, x) == v) && (y + 1 >= h || idm(y + 1, x) == v) &&
                                (x == 0 || idm(y, x - 1) == v) && (x + 1 >= w || idm(y, x + 1) == v);
                if (in)
                    icount[v] += 1;
            }
        _thin.emplace();
        for (long long p = 0; p < md.n; ++p)
            if (sea_lut[p] && md.area[p] > 0 && static_cast<double>(icount[p]) <= static_cast<double>(md.area[p]) * 0.15)
                _thin->add(p);
    }
    return *_thin;
}

const grid::PySet<Pair> &AdjGeometry::coast_pairs()
{
    // All (sea tile, land province) pairs that touch on the map.
    if (!_coast_pairs) {
        _coast_pairs.emplace();
        for (long long s = 0; s < md.n; ++s) {
            if (!sea_lut[s])
                continue;
            for (long long q : md.neighbours(s))     // (md.adjacent.get(s, ()), walked in Python's order)
                if (0 < q && q < md.n && !sea_lut[q])
                    _coast_pairs->add({s, q});
        }
    }
    return *_coast_pairs;
}

bool AdjGeometry::adjacent(long long a, long long b) const
{
    const auto *s = md.adjacent.get(a);
    return s && s->contains(b);
}

RiverPairs AdjGeometry::river_pairs(const std::vector<long long> &tiles, double gap)
{
    std::vector<long long> t = tiles;
    std::sort(t.begin(), t.end());
    const auto key = std::make_pair(t, py::round_to(gap, 3));
    auto it = _river_cache.find(key);
    if (it == _river_cache.end())
        it = _river_cache.emplace(key, _river_pairs(tiles, gap)).first;
    return it->second;
}

RiverPairs AdjGeometry::_river_pairs(const std::vector<long long> &tiles, double gap) const
{
    // Land provinces facing each other across the given (thin) sea tiles, at most `gap` pixels apart:
    // {(a, b): (dist, (xa, ya), (xb, yb))} with a < b, only pairs that don't already touch by land.
    const long long R = static_cast<long long>(std::ceil(gap));
    std::vector<std::pair<long long, long long>> offs;
    for (long long dy = 0; dy <= R; ++dy)
        for (long long dx = -R; dx <= R; ++dx)
            if ((dy > 0 || dx > 0) && static_cast<double>(dx * dx + dy * dy) <= gap * gap + 1e-6)
                offs.emplace_back(dx, dy);
    RiverPairs best;
    for (long long t : tiles) {
        if (!(0 < t && t < md.n) || md.area[t] == 0)
            continue;
        const long long x0 = std::max<long long>(0, md.x0[t] - R - 1), y0 = std::max<long long>(0, md.y0[t] - R - 1);
        const long long x1 = std::min<long long>(md.w, md.x1[t] + R + 2), y1 = std::min<long long>(md.h, md.y1[t] + R + 2);
        const long long H = y1 - y0, W = x1 - x0;
        grid::Grid<std::uint8_t> mt(H, W, 0), nb(H, W, 0);
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x)
                mt(y, x) = md.idmap(y0 + y, x0 + x) == t;
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x)
                nb(y, x) = (y > 0 && mt(y - 1, x)) || (y + 1 < H && mt(y + 1, x)) || (x > 0 && mt(y, x - 1)) ||
                           (x + 1 < W && mt(y, x + 1));
        grid::Grid<std::int64_t> lab(H, W, 0);
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x) {
                const std::uint16_t v = md.idmap(y0 + y, x0 + x);
                if (nb(y, x) && !mt(y, x) && !sea_lut[v])
                    lab(y, x) = v;
            }
        for (const auto &[dx, dy] : offs) {
            const long long ya = 0, yb = H - dy;
            const long long xa0 = std::max<long long>(0, -dx), xa1 = W - std::max<long long>(0, dx);
            const double d = std::pow(static_cast<double>(dx * dx + dy * dy), 0.5);
            for (long long yy = 0; yy < yb - ya; ++yy)
                for (long long xx = 0; xx < xa1 - xa0; ++xx) {
                    const long long av = lab(ya + yy, xa0 + xx), bv = lab(ya + dy + yy, xa0 + dx + xx);
                    if (!(av > 0 && bv > 0 && av != bv))
                        continue;
                    long long a = av, b = bv;
                    Pt pa{static_cast<double>(xx + xa0 + x0) + 0.5, static_cast<double>(yy + y0) + 0.5};
                    Pt pb{pa.first + static_cast<double>(dx), pa.second + static_cast<double>(dy)};
                    if (a > b) {
                        std::swap(a, b);
                        std::swap(pa, pb);
                    }
                    const auto *cur = best.get({a, b});
                    if (!cur || d < cur->dist)
                        best.insert({a, b}, {d, pa, pb});
                }
        }
    }
    RiverPairs out;
    for (const auto &[k, v] : best)
        if (!adjacent(k.first, k.second))
            out.insert(k, v);
    return out;
}

std::vector<DetectedRiver> detect_rivers(AdjacencyFile &adj, AdjGeometry &geo, double min_share)
{
    // Meme rivers in the file that aren't marked yet: [(name, tiles, rows)]
    const auto marked = adj.river_tiles();
    OrderedMap<QString, std::vector<Row>> groups;
    for (const Row &r : adj.rows)
        if (adj.category(r) == u"river" && !r.comment.isEmpty() && !adj.mark_by_name(r.comment))
            groups[r.comment].push_back(r);
    std::vector<DetectedRiver> found;
    for (const auto &[name, rows] : groups) {
        QHash<long long, long long> c;
        for (const Row &r : rows) {
            const auto *sa = geo.md.adjacent.get(r.a);
            const auto *sb = geo.md.adjacent.get(r.b);
            if (!sa || !sb)
                continue;
            for (long long t : sa->items())
                if (sb->contains(t) && adj.sea.contains(t))
                    c[t] += 1;
        }
        std::vector<long long> tiles;
        for (auto it = c.cbegin(); it != c.cend(); ++it)
            if (static_cast<double>(it.value()) >= std::max(1.0, min_share * static_cast<double>(rows.size())))
                tiles.push_back(it.key());
        std::sort(tiles.begin(), tiles.end());
        if (!tiles.empty())
            found.push_back({name, tiles, static_cast<long long>(rows.size())});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &x, const auto &y) { return -x.rows < -y.rows; });
    QSet<long long> taken;
    std::vector<DetectedRiver> out;
    for (const DetectedRiver &f : found) {
        std::vector<long long> tiles;
        for (long long t : f.tiles)
            if (!taken.contains(t))
                tiles.push_back(t);
        for (long long t : tiles)
            taken.insert(t);
        std::vector<long long> keep;
        for (long long t : tiles)
            if (!marked.contains(t))
                keep.push_back(t);
        if (!keep.empty())
            out.push_back({f.name, keep, f.rows});
    }
    return out;
}

std::optional<QString> river_name_of(AdjacencyFile &adj, AdjGeometry &geo, const std::vector<long long> &tiles)
{
    // The name of the Meme river these sea tiles belong to, else the name the existing crossings across them use
    const auto marks = adj.river_tiles();
    for (long long t : tiles)
        if (const auto *m = marks.get(t))
            return m->name;
    OrderedMap<QString, long long> names;
    for (const Row &r : adj.rows)
        if (adj.category(r) == u"river" && !r.comment.isEmpty() &&
            std::any_of(tiles.begin(), tiles.end(), [&](long long t) { return geo.adjacent(r.a, t) && geo.adjacent(r.b, t); }))
            names[r.comment] += 1;
    if (names.isEmpty())
        return std::nullopt;
    QString best = names.begin()->first;
    long long bc = names.begin()->second;
    for (const auto &[n, c] : names)
        if (c > bc) {
            best = n;
            bc = c;
        }
    return best;
}

CoastStatus coast_status(AdjacencyFile &adj, AdjGeometry &geo)
{
    // Which (sea, land) borders have a crossing row (any type counts). Shores of Meme-river tiles ("river_pairs")
    // and no-coast tiles ("nc_pairs") don't need one.
    const grid::PySet<Pair> &pairs0 = geo.coast_pairs();
    const auto rivers = adj.river_tiles();
    CoastStatus st;
    for (const Pair &p : pairs0.items())
        if (rivers.contains(p.first))
            st.river_pairs.add(p);
    for (const Pair &p : pairs0.items())
        if (adj.no_coast.contains(p.first) && !rivers.contains(p.first))
            st.nc_pairs.add(p);
    st.pairs = pairs0.difference(st.river_pairs).difference(st.nc_pairs);
    const auto &idx = adj.pair_index();
    for (const Pair &p : st.pairs.items())
        if (!idx.contains({std::min(p.first, p.second), std::max(p.first, p.second)}))
            st.missing.push_back(p);
    std::sort(st.missing.begin(), st.missing.end());
    for (const Row &r : adj.rows)
        if (adj.category(r) == u"coast" && !geo.adjacent(r.a, r.b))
            st.stale.push_back(r);
    return st;
}

OrderedMap<QString, RiverStatus> river_status(AdjacencyFile &adj, AdjGeometry &geo, double gap)
{
    // Bank crossings of the Meme rivers: {name: {"expected": {(a, b): (d, pa, pb)}, "missing": [(a, b)]}}
    const auto &idx = adj.pair_index();
    OrderedMap<QString, RiverStatus> out;
    for (const Mark &m : adj.marks) {
        RiverStatus rs;
        rs.expected = geo.river_pairs(m.tiles, gap);
        for (const auto &[k, _v] : rs.expected)
            if (!idx.contains({std::min(k.first, k.second), std::max(k.first, k.second)}))
                rs.missing.push_back(k);
        std::sort(rs.missing.begin(), rs.missing.end());
        out.insert(m.name, rs);
    }
    return out;
}

QStringList check_row(AdjacencyFile &adj, AdjGeometry &geo, const moddata::Mod &mod, const Row &r)
{
    // What looks wrong with one row (list of texts).
    QStringList out;
    const QString cat = adj.category(r);
    for (long long p : {r.a, r.b})
        if (!mod.provinces.contains(p))
            out << u"#%1 is not a province in definition.csv"_s.arg(p);
    if (r.through && !mod.provinces.contains(r.through))
        out << u"through #%1 is not a province"_s.arg(r.through);
    if (r.a == r.b)
        out << u"from and to are the same province"_s;
    if (!TYPES.contains(r.type))
        out << u"unknown type '%1'"_s.arg(r.type);
    if (cat == u"coast" && !geo.adjacent(r.a, r.b))
        out << u"sea tile and province don't touch on the map (stale row)"_s;
    if (r.type == u"impassable" && !geo.adjacent(r.a, r.b))
        out << u"the two provinces don't touch on the map, so this does nothing"_s;
    if (cat == u"strait" && !adj.sea.contains(r.through))
        out << u"through #%1 should be a sea tile"_s.arg(r.through);
    if (r.type == u"sea" && r.data)
        out << u"Data is only used by canals (should be 0)"_s;
    if (r.type == u"canal") {
        if (!adj.sea.contains(r.a) || !adj.sea.contains(r.b))
            out << u"a canal joins two sea tiles"_s;
        if (adj.sea.contains(r.through) || !r.through)
            out << u"through should be the land province that controls the canal"_s;
        if (r.data <= 0)
            out << u"canal number (Data) must be 1 or more"_s;
    }
    return out;
}

OrderedMap<long long, QStringList> problems(AdjacencyFile &adj, AdjGeometry &geo, const moddata::Mod &mod)
{
    // {uid: [text]} of rows that look wrong.
    OrderedMap<long long, QStringList> out;
    QHash<long long, long long> canal_ids;
    for (const Row &r : adj.rows)
        if (r.type == u"canal")
            canal_ids[r.data] += 1;
    std::map<std::tuple<long long, long long, QString, long long>, long long> seen;
    for (const Row &r : adj.rows) {
        for (const QString &t : check_row(adj, geo, mod, r))
            out[r.uid] << t;
        if (r.type == u"canal" && r.data > 0 && canal_ids.value(r.data) > 1)
            out[r.uid] << u"canal number %1 is used by %2 canals"_s.arg(r.data).arg(canal_ids.value(r.data));
        const auto key = std::make_tuple(std::min(r.a, r.b), std::max(r.a, r.b), r.type, r.through);
        if (seen.count(key))
            out[r.uid] << u"duplicate of another row"_s;
        seen[key] = r.uid;
    }
    return out;
}

}  // namespace adjacency
