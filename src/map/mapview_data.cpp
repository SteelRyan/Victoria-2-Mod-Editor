// mapview.py, the data half: MapData. Line by line; numpy's whole-array steps as plain loops in the same order
// where the order reaches a result (the neighbour sets).
#include "map/mapview_data.h"

#include <QImage>
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace mapview {

const Rgb SEA_RGB = {27, 36, 48};
const double WARN_SCALE = 0.85;      // the warning triangles (brigades over the cap): 15 % smaller than their pill
const Rgb STATE_BORDER_RGB = {14, 16, 20};
const long long SPARE_IDS = 2048;

namespace {
constexpr std::int32_t BIG = std::numeric_limits<std::int32_t>::max();

// Python's slice bounds a[start:stop] for a length
std::pair<long long, long long> py_range(long long start, long long stop, long long len)
{
    auto fix = [len](long long v) {
        if (v < 0)
            v += len;
        return std::clamp<long long>(v, 0, len);
    };
    const long long a = fix(start), b = fix(stop);
    return {a, std::max(a, b)};
}
}  // namespace

MapData::MapData(const moddata::Mod &mod, const QString &bmp_path)
{
    QImage img(bmp_path);
    if (img.isNull())
        throw std::runtime_error(u"Could not read %1"_s.arg(bmp_path).toStdString());
    img = img.convertToFormat(QImage::Format_RGB888);
    w = img.width();
    h = img.height();
    // the key of each pixel; the mod's bitmaps are stored upside down: always mirror top-to-bottom
    long long top = 0;
    for (const auto &[pid, _p] : mod.provinces)
        top = std::max(top, pid);
    n = top + 1 + SPARE_IDS;
    std::vector<std::uint16_t> lut(1u << 24, 0);
    for (const auto &[pid, pr] : mod.provinces) {
        const auto &[r, g, b] = pr.rgb;
        lut[(static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b)] =
            static_cast<std::uint16_t>(pid);
    }
    idmap = grid::Grid<std::uint16_t>(h, w);
    for (long long y = 0; y < h; ++y) {
        const uchar *row = img.constScanLine(static_cast<int>(h - 1 - y));
        std::uint16_t *out = idmap.row(y);
        for (long long x = 0; x < w; ++x) {
            const uchar *p = row + x * 3;
            out[x] = lut[(static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2]];
        }
    }
    sea.assign(static_cast<size_t>(n), 0);
    for (const auto &[pid, pr] : mod.provinces)
        sea[static_cast<size_t>(pid)] = pr.sea ? 1 : 0;
    sea[0] = 1;
    const grid::Grid<std::uint16_t> &idm = idmap;
    grid::Grid<std::uint8_t> pb(h, w, 0);
    for (long long y = 0; y < h; ++y)
        for (long long x = 1; x < w; ++x)
            if (idm(y, x) != idm(y, x - 1))
                pb(y, x) = 1;
    for (long long y = 1; y < h; ++y)
        for (long long x = 0; x < w; ++x)
            if (idm(y, x) != idm(y - 1, x))
                pb(y, x) = 1;
    _coastal_full();                                 // coastal land provinces: a pixel touching a sea pixel
    sea_border = grid::Grid<std::uint8_t>(h, w, 0);
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            const std::uint16_t left = idm(y, x == 0 ? w - 1 : x - 1);   // np.roll(idm, 1, axis=1)
            const bool sp = sea[idm(y, x)];
            sea_border(y, x) = (pb(y, x) && sp && sea[left] && idm(y, x) != 0) ? 1 : 0;
            if (sp)
                pb(y, x) = 0;                        // pb &= ~sea_px
        }
    prov_border = pb;
    _compute_adjacency();
    rebuild_regions(mod);
    _compute_stats();
}

void MapData::_coastal_full()
{
    coastal.assign(static_cast<size_t>(n), 0);
    const grid::Grid<std::uint16_t> &idm = idmap;
    auto pair = [&](std::uint16_t a, std::uint16_t b) {   // (a, b): the later pixel and the one before it
        const bool sa = sea[a], sb = sea[b];
        if (sb && !sa)
            coastal[a] = 1;
        if (sa && !sb)
            coastal[b] = 1;
    };
    for (long long y = 0; y < h; ++y)
        for (long long x = 1; x < w; ++x)
            pair(idm(y, x), idm(y, x - 1));
    for (long long y = 1; y < h; ++y)
        for (long long x = 0; x < w; ++x)
            pair(idm(y, x), idm(y - 1, x));
    coastal[0] = 0;
}

void MapData::_compute_adjacency()
{
    const grid::Grid<std::uint16_t> &idm = idmap;
    adjacent.clear();
    for (int dir = 0; dir < 2; ++dir) {
        std::vector<std::int64_t> keys;
        auto take = [&](std::int64_t a, std::int64_t b) {
            if (a != b)
                keys.push_back(std::min(a, b) * 65536 + std::max(a, b));
        };
        if (dir == 0) {
            for (long long y = 0; y < h; ++y)
                for (long long x = 1; x < w; ++x)
                    take(idm(y, x), idm(y, x - 1));
        } else {
            for (long long y = 1; y < h; ++y)
                for (long long x = 0; x < w; ++x)
                    take(idm(y, x), idm(y - 1, x));
        }
        std::sort(keys.begin(), keys.end());             // np.unique
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (std::int64_t k : keys) {
            const std::int64_t p = k / 65536, q = k % 65536;
            adjacent[p].add(q);
            adjacent[q].add(p);
        }
    }
    // the map wraps around horizontally: the first and last pixel columns touch
    grid::PySet<std::pair<std::int64_t, std::int64_t>> wrap;
    for (long long y = 0; y < h; ++y)
        wrap.add({idm(y, 0), idm(y, w - 1)});
    for (const auto &[p, q] : wrap.items())
        if (p != q) {
            adjacent[p].add(q);
            adjacent[q].add(p);
        }
}

void MapData::_compute_stats()
{
    const grid::Grid<std::uint16_t> &idm = idmap;
    area.assign(static_cast<size_t>(n), 0);
    std::vector<double> xs(static_cast<size_t>(n), 0.0), ys(static_cast<size_t>(n), 0.0);
    x0.assign(static_cast<size_t>(n), BIG);
    y0.assign(static_cast<size_t>(n), BIG);
    x1.assign(static_cast<size_t>(n), -1);
    y1.assign(static_cast<size_t>(n), -1);
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            const std::uint16_t p = idm(y, x);
            area[p] += 1;
            xs[p] += static_cast<double>(x);         // (np.bincount with float32 weights: sums of whole numbers, exact)
            ys[p] += static_cast<double>(y);
            y0[p] = std::min<std::int32_t>(y0[p], static_cast<std::int32_t>(y));
            y1[p] = static_cast<std::int32_t>(y);
            x0[p] = std::min<std::int32_t>(x0[p], static_cast<std::int32_t>(x));
            x1[p] = std::max<std::int32_t>(x1[p], static_cast<std::int32_t>(x));
        }
    cx.assign(static_cast<size_t>(n), -1.0);
    cy.assign(static_cast<size_t>(n), -1.0);
    for (long long p = 0; p < n; ++p)
        if (area[p] > 0) {
            cx[p] = xs[p] / static_cast<double>(area[p]);
            cy[p] = ys[p] / static_cast<double>(area[p]);
        }
}

void MapData::rebuild_regions(const moddata::Mod &mod)
{
    // (Re)compute the state index per province and the state border mask (after moves).
    region_ids.clear();
    for (const auto &[rid, _r] : mod.regions)
        region_ids << rid;
    std::sort(region_ids.begin(), region_ids.end());
    reg_idx.assign(static_cast<size_t>(n), 0);
    for (qsizetype i = 0; i < region_ids.size(); ++i)
        for (long long pid : mod.regions.value(region_ids[i]).provinces)
            if (pid >= 0 && pid < n && reg_idx[pid] == 0)
                reg_idx[pid] = static_cast<std::int32_t>(i + 1);
    const grid::Grid<std::uint16_t> &idm = idmap;
    state_border = grid::Grid<std::uint8_t>(h, w, 0);
    for (long long y = 0; y < h; ++y)
        for (long long x = 1; x < w; ++x) {
            const std::int32_t a = reg_idx[idm(y, x)], b = reg_idx[idm(y, x - 1)];
            if (a != b && a > 0 && b > 0)
                state_border(y, x) = state_border(y, x - 1) = 1;
        }
    for (long long y = 1; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            const std::int32_t a = reg_idx[idm(y, x)], b = reg_idx[idm(y - 1, x)];
            if (a != b && a > 0 && b > 0)
                state_border(y, x) = state_border(y - 1, x) = 1;
        }
    _key.reset();
}

const grid::Grid<std::int32_t> &MapData::pix_key()
{
    // Per pixel: province id × 8 + its border class (1 province border, 2 state border, 4 sea border) — cached
    if (!_key) {
        grid::Grid<std::int32_t> k(h, w);
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x)
                k(y, x) = (static_cast<std::int32_t>(idmap(y, x)) << 3) + prov_border(y, x) +
                          (static_cast<std::int32_t>(state_border(y, x)) << 1) + (static_cast<std::int32_t>(sea_border(y, x)) << 2);
        _key = std::move(k);
        _mix_key.clear();
    }
    return *_key;
}

const grid::Grid<std::int32_t> &MapData::pix_key_mix(std::int32_t Q)
{
    // Like pix_key, for stripe tables (Q colours per province along x + y): (id·Q + phase)·8 + class.
    const grid::Grid<std::int32_t> &key = pix_key();
    auto it = _mix_key.find(Q);
    if (it == _mix_key.end()) {
        grid::Grid<std::int32_t> got(h, w);
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x) {
                const std::int32_t k = key(y, x);
                got(y, x) = static_cast<std::int32_t>(static_cast<std::uint32_t>(k) * static_cast<std::uint32_t>(Q) -
                                                      static_cast<std::uint32_t>(k & 7) * static_cast<std::uint32_t>(Q - 1) +
                                                      static_cast<std::uint32_t>(((x + y) % Q) * 8));
            }
        it = _mix_key.emplace(Q, std::move(got)).first;
    }
    return it->second;
}

std::optional<std::array<long long, 4>> MapData::changed_since(long long stamp) const
{
    if (stamp == pix_stamp)
        return std::array<long long, 4>{0, 0, 0, 0};
    if (stamp > pix_stamp || pix_log.empty() || pix_log.front().first > stamp + 1)
        return std::nullopt;
    std::array<long long, 4> u{w, h, 0, 0};
    for (const auto &[s, b] : pix_log)
        if (s > stamp) {
            u[0] = std::min(u[0], b[0]);
            u[1] = std::min(u[1], b[1]);
            u[2] = std::max(u[2], b[2]);
            u[3] = std::max(u[3], b[3]);
        }
    u[0] = std::max(0LL, u[0]);
    u[1] = std::max(0LL, u[1]);
    u[2] = std::min(w, u[2]);
    u[3] = std::min(h, u[3]);
    if (u[2] <= u[0] || u[3] <= u[1])
        return std::array<long long, 4>{0, 0, 0, 0};
    return u;
}

void MapData::refresh_masks(const std::array<long long, 4> &box)
{
    // Recompute the border masks after pixels inside box = (x0, y0, x1, y1) changed.
    pix_stamp += 1;                              // (C++ only: every pixel change, a stroke's live ones too)
    pix_log.emplace_back(pix_stamp, box);
    while (pix_log.size() > 256)
        pix_log.pop_front();
    const auto [bx0, by0, bx1, by1] = box;
    // window with a 2-pixel margin; only its inner part (1 px beyond the box) is written back
    const long long X0 = std::max(0LL, bx0 - 2), Y0 = std::max(0LL, by0 - 2), X1 = std::min(w, bx1 + 2),
                    Y1 = std::min(h, by1 + 2);
    const long long WH = std::max(0LL, Y1 - Y0), WW = std::max(0LL, X1 - X0);
    auto W = [&](long long y, long long x) { return idmap(Y0 + y, X0 + x); };
    grid::Grid<std::uint8_t> pb(WH, WW, 0), seab(WH, WW, 0), sb(WH, WW, 0);
    for (long long y = 0; y < WH; ++y)
        for (long long x = 1; x < WW; ++x)
            if (W(y, x) != W(y, x - 1))
                pb(y, x) = 1;
    for (long long y = 1; y < WH; ++y)
        for (long long x = 0; x < WW; ++x)
            if (W(y, x) != W(y - 1, x))
                pb(y, x) = 1;
    for (long long y = 0; y < WH; ++y)
        for (long long x = 0; x < WW; ++x) {
            const std::uint16_t left = x > 0 ? W(y, x - 1) : (X0 > 0 ? idmap(Y0 + y, X0 - 1) : idmap(Y0 + y, w - 1));
            const bool sp = sea[W(y, x)];
            seab(y, x) = (pb(y, x) && sp && sea[left] && W(y, x) != 0) ? 1 : 0;
            if (sp)
                pb(y, x) = 0;
        }
    for (long long y = 0; y < WH; ++y)
        for (long long x = 1; x < WW; ++x) {
            const std::int32_t a = reg_idx[W(y, x)], b = reg_idx[W(y, x - 1)];
            if (a != b && a > 0 && b > 0)
                sb(y, x) = sb(y, x - 1) = 1;
        }
    for (long long y = 1; y < WH; ++y)
        for (long long x = 0; x < WW; ++x) {
            const std::int32_t a = reg_idx[W(y, x)], b = reg_idx[W(y - 1, x)];
            if (a != b && a > 0 && b > 0)
                sb(y, x) = sb(y - 1, x) = 1;
        }
    // write back all but the outer margin row / column (their neighbours outside the window are unknown), except
    // where the window touches the map edge
    const long long a0 = Y0 == 0 ? 0 : 1, b0 = X0 == 0 ? 0 : 1;
    const long long a1 = Y1 == h ? WH : WH - 1, b1 = X1 == w ? WW : WW - 1;
    for (long long y = a0; y < a1; ++y)
        for (long long x = b0; x < b1; ++x) {
            prov_border(Y0 + y, X0 + x) = pb(y, x);
            sea_border(Y0 + y, X0 + x) = seab(y, x);
            state_border(Y0 + y, X0 + x) = sb(y, x);
        }
    if (_key) {
        for (long long y = a0; y < a1; ++y)
            for (long long x = b0; x < b1; ++x) {
                const long long Y = Y0 + y, X = X0 + x;
                const std::int32_t k = (static_cast<std::int32_t>(idmap(Y, X)) << 3) + prov_border(Y, X) +
                                       (static_cast<std::int32_t>(state_border(Y, X)) << 1) +
                                       (static_cast<std::int32_t>(sea_border(Y, X)) << 2);
                (*_key)(Y, X) = k;
                for (auto &[Q, mk] : _mix_key)
                    mk(Y, X) = static_cast<std::int32_t>(static_cast<std::uint32_t>(k) * static_cast<std::uint32_t>(Q) -
                                                         static_cast<std::uint32_t>(k & 7) * static_cast<std::uint32_t>(Q - 1) +
                                                         static_cast<std::uint32_t>(((X + Y) % Q) * 8));
            }
    }
}

void MapData::refresh_ids(const std::vector<long long> &ids0, const std::array<long long, 4> &box)
{
    // Recompute size, centre, box, neighbours and coast of provinces that gained / lost pixels.
    std::vector<long long> ids = ids0;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const grid::Grid<std::uint16_t> &idm = idmap;
    if (ids.size() > 60) {                           // a big change (e.g. a clean-up): one full pass is faster
        _compute_stats();
        _compute_adjacency();
        _coastal_full();
        version += 1;
        return;
    }
    const auto [bx0, by0, bx1, by1] = box;
    for (long long p : ids) {
        long long X0, Y0, X1, Y1;
        if (x1[p] >= 0) {
            X0 = std::min<long long>(x0[p], bx0);
            Y0 = std::min<long long>(y0[p], by0);
            X1 = std::max<long long>(x1[p] + 1, bx1);
            Y1 = std::max<long long>(y1[p] + 1, by1);
        } else {
            X0 = bx0, Y0 = by0, X1 = bx1, Y1 = by1;
        }
        const auto [ya, yb] = py_range(Y0, Y1, h);
        const auto [xa, xb] = py_range(X0, X1, w);
        long long cnt = 0, sx = 0, sy = 0, mnx = 0, mxx = 0, mny = 0, mxy = 0;
        for (long long y = ya; y < yb; ++y)
            for (long long x = xa; x < xb; ++x)
                if (idm(y, x) == p) {
                    if (!cnt) {
                        mnx = mxx = x;
                        mny = mxy = y;
                    }
                    ++cnt;
                    sx += x;
                    sy += y;
                    mnx = std::min(mnx, x);
                    mxx = std::max(mxx, x);
                    mny = std::min(mny, y);
                    mxy = std::max(mxy, y);
                }
        if (!cnt) {
            area[p] = 0;
            cx[p] = cy[p] = -1;
            x0[p] = y0[p] = BIG;
            x1[p] = y1[p] = -1;
        } else {
            area[p] = cnt;
            cx[p] = static_cast<double>(sx) / static_cast<double>(cnt);   // (xs.mean(): whole numbers, an exact sum)
            cy[p] = static_cast<double>(sy) / static_cast<double>(cnt);
            x0[p] = static_cast<std::int32_t>(mnx);
            x1[p] = static_cast<std::int32_t>(mxx);
            y0[p] = static_cast<std::int32_t>(mny);
            y1[p] = static_cast<std::int32_t>(mxy);
        }
    }
    // neighbours: look around each province's (new) box
    for (long long p : ids) {
        grid::PySet<long long> old;
        if (const auto *o = adjacent.get(p))
            old = *o;
        grid::PySet<long long> nw;
        if (area[p] > 0) {
            const long long X0 = std::max<long long>(0, x0[p] - 1), Y0 = std::max<long long>(0, y0[p] - 1);
            const long long X1 = std::min<long long>(w, x1[p] + 2), Y1 = std::min<long long>(h, y1[p] + 2);
            for (long long y = Y0; y < Y1; ++y)          // W[:, 1:] against W[:, :-1]
                for (long long x = X0 + 1; x < X1; ++x) {
                    const long long a = idm(y, x), b = idm(y, x - 1);
                    if (a != b && (a == p || b == p))
                        nw.add(a == p ? b : a);
                }
            for (long long y = Y0 + 1; y < Y1; ++y)      // W[1:, :] against W[:-1, :]
                for (long long x = X0; x < X1; ++x) {
                    const long long a = idm(y, x), b = idm(y - 1, x);
                    if (a != b && (a == p || b == p))
                        nw.add(a == p ? b : a);
                }
            if (X0 == 0 || X1 == w)                      // the map wraps around horizontally
                for (long long y = 0; y < h; ++y) {
                    const long long L = idm(y, 0), R = idm(y, w - 1);
                    if (L != R && (L == p || R == p))
                        nw.add(L == p ? R : L);
                }
        }
        nw.discard(p);
        for (long long q : old.items())
            if (!nw.contains(q))
                if (auto *s = adjacent.get(q))
                    s->discard(p);
        for (long long q : nw.items())
            adjacent[q].add(p);
        adjacent.insert(p, nw);
    }
    // coast (for naval bases): land that touches sea
    std::vector<long long> look = ids;
    for (long long p : ids)
        if (const auto *s = adjacent.get(p))
            for (long long q : s->items())
                look.push_back(q);
    for (long long p : look)
        if (0 < p && p < n) {
            bool any = false;
            if (const auto *s = adjacent.get(p))
                for (long long q : s->items())
                    if (q < n && sea[q])
                        any = true;
            coastal[p] = (!sea[p] && any) ? 1 : 0;
        }
    version += 1;
}

long long MapData::province_at(double x, double y) const
{
    if (0 <= x && x < w && 0 <= y && y < h)
        return idmap(static_cast<long long>(y), static_cast<long long>(x));
    return 0;
}

std::optional<std::array<long long, 4>> MapData::bbox(const std::vector<long long> &ids) const
{
    std::optional<std::array<long long, 4>> out;
    for (long long i : ids) {
        if (!(0 < i && i < n && x1[i] >= 0))
            continue;
        if (!out)
            out = std::array<long long, 4>{x0[i], y0[i], x1[i], y1[i]};
        else
            *out = {std::min<long long>((*out)[0], x0[i]), std::min<long long>((*out)[1], y0[i]),
                    std::max<long long>((*out)[2], x1[i]), std::max<long long>((*out)[3], y1[i])};
    }
    if (out) {
        (*out)[2] += 1;
        (*out)[3] += 1;
    }
    return out;
}

std::vector<long long> MapData::neighbours(long long pid) const
{
    if (const auto *s = adjacent.get(pid))
        return s->items();
    return {};
}

}  // namespace mapview
