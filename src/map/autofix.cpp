// autofix.py — line by line.
#include "map/autofix.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "map/mapcheck.h"
#include "map/newprov.h"
#include "map/provbmp.h"

#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace autofix {

static const int N4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int DIAG[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
using YX = std::pair<long long, long long>;

// ---------------------------------------------------------------------------------------------- rivers.bmp
std::pair<Flat, std::vector<std::uint8_t>> river_repairs(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R,
                                                         const std::vector<std::pair<double, double>> &points, int rounds)
{
    // Repairs, in this order: river on water / no province -> water (254) / land (255); 2x2 block -> the pixel with
    // the fewest other river neighbours becomes land; diagonal-only joint -> a connecting pixel on land, as wide as
    // the river; source mid-river -> the river's width, a lone source -> land; merge at a river's end -> the river's
    // width, a lone merge -> land. A spot that can't be decided is left as it is and stays listed.
    const long long H = R.h(), W = R.w();
    grid::Grid<std::uint8_t> Rc = R;
    long long y0 = 0, y1 = H, x0 = 0, x1 = W;
    std::optional<grid::Grid<std::uint8_t>> near_m;
    if (!points.empty()) {
        std::vector<long long> xs, ys;
        for (const auto &p : points) {
            xs.push_back(py::trunc_int(p.first));
            ys.push_back(py::trunc_int(p.second));
        }
        y0 = std::max(0LL, *std::min_element(ys.begin(), ys.end()) - 8);
        y1 = std::min(H, *std::max_element(ys.begin(), ys.end()) + 9);
        x0 = std::max(0LL, *std::min_element(xs.begin(), xs.end()) - 8);
        x1 = std::min(W, *std::max_element(xs.begin(), xs.end()) + 9);
        near_m.emplace(y1 - y0, x1 - x0, 0);
        for (size_t i = 0; i < xs.size(); ++i) {
            const long long ya = std::max(0LL, ys[i] - 2 - y0), yb = std::min(near_m->h(), std::max(0LL, ys[i] + 3 - y0));
            const long long xa = std::max(0LL, xs[i] - 2 - x0), xb = std::min(near_m->w(), std::max(0LL, xs[i] + 3 - x0));
            for (long long y = ya; y < yb; ++y)
                for (long long x = xa; x < xb; ++x)
                    (*near_m)(y, x) = 1;
        }
    }
    const auto &ids = md.idmap;
    const auto &sea = md.sea;
    auto riv = [&](long long y, long long x) { return 0 <= y && y < H && 0 <= x && x < W && Rc(y, x) < 12; };
    auto land = [&](long long y, long long x) { return 0 <= y && y < H && 0 <= x && x < W && ids(y, x) != 0 && !sea[ids(y, x)]; };
    auto n4 = [&](long long y, long long x) {
        std::vector<YX> out;
        for (const auto &d : N4)
            if (riv(y + d[0], x + d[1]))
                out.emplace_back(y + d[0], x + d[1]);
        return out;
    };
    auto nd = [&](long long y, long long x) {
        std::vector<YX> out;
        for (const auto &d : DIAG)
            if (riv(y + d[0], x + d[1]))
                out.emplace_back(y + d[0], x + d[1]);
        return out;
    };
    auto width = [&](long long y, long long x) {
        int best = -1;
        for (const YX &q : n4(y, x))
            if (Rc(q.first, q.second) >= 2)
                best = std::max(best, int(Rc(q.first, q.second)));
        return best >= 0 ? best : 2;
    };
    auto blank = [&](long long y, long long x) {
        return std::uint8_t(ids(y, x) != 0 && sea[ids(y, x)] ? mapcheck::RIVER_SEA : mapcheck::RIVER_LAND);
    };
    auto makes_block = [&](long long y, long long x) {
        for (int dy : {-1, 0})
            for (int dx : {-1, 0}) {
                bool all = true;
                for (int a : {0, 1})
                    for (int b : {0, 1}) {
                        const long long cy = y + dy + a, cx = x + dx + b;
                        if (!((cy == y && cx == x) || riv(cy, cx)))
                            all = false;
                    }
                if (all)
                    return true;
            }
        return false;
    };
    for (int round = 0; round < rounds; ++round) {
        const mapcheck::Masks masks = mapcheck::river_problem_masks(md, Rc, y0, y1, x0, x1);
        auto spots = [&](const QString &key) {
            const auto &m = *masks.get(key);
            std::vector<YX> out;
            for (long long y = 0; y < m.h(); ++y)
                for (long long x = 0; x < m.w(); ++x)
                    if (m(y, x) && (!near_m || (*near_m)(y, x)))
                        out.emplace_back(y + y0, x + x0);
            return out;
        };
        bool changed = false;
        for (const auto &[y, x] : spots(u"water"_s)) {
            Rc(y, x) = blank(y, x);
            changed = true;
        }
        // 2x2 blocks: one pixel of each block becomes land
        for (const auto &[y, x] : spots(u"block"_s)) {
            if (!(riv(y, x) && riv(y, x + 1) && riv(y + 1, x) && riv(y + 1, x + 1)))
                continue;                                  // not a block's top-left corner (or already mended)
            const std::vector<YX> cells = {{y, x}, {y, x + 1}, {y + 1, x}, {y + 1, x + 1}};
            auto score = [&](const YX &c) {
                long long out = 0;
                for (const YX &q : n4(c.first, c.second))
                    out += std::find(cells.begin(), cells.end(), q) == cells.end();
                return std::make_pair(out, Rc(c.first, c.second) < 2);   // fewest links out; keep sources / merges
            };
            YX best = cells[0];
            for (const YX &c : cells)
                if (score(c) < score(best))
                    best = c;
            Rc(best.first, best.second) = blank(best.first, best.second);
            changed = true;
        }
        // diagonal-only joints: a connecting pixel
        for (const auto &[y, x] : spots(u"diag"_s)) {
            if (!riv(y, x) || !n4(y, x).empty())
                continue;
            const auto qs = nd(y, x);
            if (qs.empty())
                continue;
            const auto [qy, qx] = qs[0];
            const std::vector<YX> cands = {{y, qx}, {qy, x}};
            std::vector<YX> ok;
            for (const YX &c : cands)
                if (land(c.first, c.second) && !makes_block(c.first, c.second))
                    ok.push_back(c);
            if (ok.empty())
                for (const YX &c : cands)
                    if (land(c.first, c.second))
                        ok.push_back(c);
            if (ok.empty())
                continue;
            const int v = Rc(y, x) >= 2 ? Rc(y, x) : (Rc(qy, qx) >= 2 ? Rc(qy, qx) : 2);
            Rc(ok[0].first, ok[0].second) = std::uint8_t(v);
            changed = true;
        }
        for (const auto &[y, x] : spots(u"source"_s)) {
            const size_t k = n4(y, x).size();
            if (k >= 2) {
                Rc(y, x) = std::uint8_t(width(y, x));
                changed = true;
            } else if (k == 0 && nd(y, x).empty()) {
                Rc(y, x) = blank(y, x);
                changed = true;
            }
        }
        for (const auto &[y, x] : spots(u"merge"_s)) {
            const size_t k = n4(y, x).size();
            if (k == 1) {
                Rc(y, x) = std::uint8_t(width(y, x));
                changed = true;
            } else if (k == 0 && nd(y, x).empty()) {
                Rc(y, x) = blank(y, x);
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    Flat idx;
    std::vector<std::uint8_t> vals;
    for (long long y = y0; y < y1; ++y)
        for (long long x = x0; x < x1; ++x)
            if (Rc(y, x) != R(y, x)) {
                idx.push_back(y * W + x);
                vals.push_back(Rc(y, x));
            }
    return {idx, vals};
}

// ---------------------------------------------------------------------------------------------- provinces.bmp
std::optional<long long> _vote(const grid::Grid<std::uint16_t> &idm, const Flat &ys, const Flat &xs, long long own)
{
    const long long H = idm.h(), W = idm.w();
    std::vector<long long> order;                   // (a Counter: first-seen order breaks a tie)
    std::map<long long, long long> cnt;
    for (const auto &d : N4)
        for (size_t i = 0; i < ys.size(); ++i) {
            const long long yy = ys[i] + d[0], xx = py::mod(xs[i] + d[1], W);
            if (yy < 0 || yy >= H)
                continue;
            const long long v = idm(yy, xx);
            if (v != own && v != 0) {
                if (!cnt.count(v))
                    order.push_back(v);
                cnt[v] += 1;
            }
        }
    if (order.empty())
        return std::nullopt;
    long long best = order[0];
    for (long long v : order)
        if (cnt[v] > cnt[best])
            best = v;
    return best;
}

static std::optional<std::pair<Flat, Flat>> piece_at(const grid::Grid<std::uint16_t> &idm, long long pid, double xf,
                                                     double yf, long long radius)
{
    const long long H = idm.h(), W = idm.w();
    const long long x = py::trunc_int(xf), y = py::trunc_int(yf);
    if (!(0 <= y && y < H && 0 <= x && x < W) || idm(y, x) != pid)
        return std::nullopt;
    const long long y0 = std::max(0LL, y - radius), y1 = std::min(H, y + radius + 1);
    const long long x0 = std::max(0LL, x - radius), x1 = std::min(W, x + radius + 1);
    grid::Grid<std::uint8_t> m(y1 - y0, x1 - x0, 0);
    for (long long yy = y0; yy < y1; ++yy)
        for (long long xx = x0; xx < x1; ++xx)
            m(yy - y0, xx - x0) = idm(yy, xx) == pid;
    const auto comp = provbmp::component(m, y - y0, x - x0);
    Flat ys, xs;
    for (long long yy = 0; yy < comp.h(); ++yy)
        for (long long xx = 0; xx < comp.w(); ++xx)
            if (comp(yy, xx)) {
                ys.push_back(yy + y0);
                xs.push_back(xx + x0);
            }
    return std::make_pair(ys, xs);
}

std::optional<std::pair<Flat, long long>> piece_owner(const grid::Grid<std::uint16_t> &idm, long long pid, double x,
                                                      double y, long long radius)
{
    const auto p = piece_at(idm, pid, x, y, radius);
    if (!p)
        return std::nullopt;
    const auto to = _vote(idm, p->first, p->second, pid);
    if (!to)
        return std::nullopt;
    Flat flat;
    for (size_t i = 0; i < p->first.size(); ++i)
        flat.push_back(p->first[i] * idm.w() + p->second[i]);
    return std::make_pair(flat, *to);
}

Flat piece_pixels(const grid::Grid<std::uint16_t> &idm, long long pid, double x, double y, long long radius)
{
    const auto p = piece_at(idm, pid, x, y, radius);
    Flat flat;
    if (p)
        for (size_t i = 0; i < p->first.size(); ++i)
            flat.push_back(p->first[i] * idm.w() + p->second[i]);
    return flat;
}

static grid::Grid<std::int32_t> file_colours_read(const QString &bmp_path, long long h, long long w);

grid::Grid<std::int32_t> file_colours(const QString &bmp_path, long long h, long long w)
{
    return *file_colours_kept(bmp_path, h, w);
}

std::shared_ptr<const grid::Grid<std::int32_t>> file_colours_kept(const QString &bmp_path, long long h, long long w)
{
    static std::optional<std::tuple<QString, qint64, qint64, long long, long long>> key;
    static std::shared_ptr<const grid::Grid<std::int32_t>> kept;
    const QFileInfo fi(bmp_path);
    const auto k = std::make_tuple(fi.absoluteFilePath(), fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1,
                                   fi.exists() ? fi.size() : -1, h, w);
    if (kept && key == k)
        return kept;
    kept.reset();
    key.reset();
    auto g = std::make_shared<const grid::Grid<std::int32_t>>(file_colours_read(bmp_path, h, w));
    kept = g;
    key = k;
    return g;
}

static grid::Grid<std::int32_t> file_colours_read(const QString &bmp_path, long long h, long long w)
{
    const QImage img = QImage(bmp_path).convertToFormat(QImage::Format_RGB888);
    if (img.height() != h || img.width() != w)
        throw std::runtime_error("provinces.bmp has another size than the map in the editor");
    grid::Grid<std::int32_t> out(h, w, 0);
    for (long long y = 0; y < h; ++y) {
        const uchar *row = img.constScanLine(int(h - 1 - y));    // the map view's orientation
        for (long long x = 0; x < w; ++x)
            out(y, x) = (std::int32_t(row[3 * x]) << 16) | (std::int32_t(row[3 * x + 1]) << 8) | row[3 * x + 2];
    }
    return out;
}

std::pair<grid::Grid<std::int32_t>, long long> labels4(const grid::Grid<std::uint8_t> &mask)
{
    // runs + union-find
    const long long H = mask.h(), W = mask.w();
    grid::Grid<std::int32_t> out(H, W, 0);
    std::vector<std::uint8_t> start(size_t(H * W), 0);
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x)
            start[size_t(y * W + x)] = mask(y, x) && (x == 0 || !mask(y, x - 1));
    std::vector<std::int64_t> run_of(size_t(H * W));
    std::int64_t c = 0;
    for (size_t i = 0; i < run_of.size(); ++i) {
        c += start[i];
        run_of[i] = c - 1;
    }
    const std::int64_t R = c;
    if (!R)
        return {out, 0};
    std::vector<std::int64_t> e;
    for (long long y = 0; y + 1 < H; ++y)
        for (long long x = 0; x < W; ++x) {
            const size_t up = size_t(y * W + x), dn = size_t((y + 1) * W + x);
            if (mask(y + 1, x) && mask(y, x) && (start[dn] || start[up]))
                e.push_back(run_of[up] * R + run_of[dn]);
        }
    std::sort(e.begin(), e.end());
    e.erase(std::unique(e.begin(), e.end()), e.end());
    std::vector<std::int64_t> parent(static_cast<size_t>(R));
    for (std::int64_t i = 0; i < R; ++i)
        parent[size_t(i)] = i;
    auto find = [&](std::int64_t q) {
        std::int64_t r = q;
        while (parent[size_t(r)] != r)
            r = parent[size_t(r)];
        while (parent[size_t(q)] != r) {
            const std::int64_t nq = parent[size_t(q)];
            parent[size_t(q)] = r;
            q = nq;
        }
        return r;
    };
    for (std::int64_t k : e) {
        const std::int64_t ru = find(k / R), rv = find(k % R);
        if (ru != rv)
            parent[size_t(std::max(ru, rv))] = std::min(ru, rv);
    }
    std::vector<std::int64_t> roots(static_cast<size_t>(R));
    for (std::int64_t i = 0; i < R; ++i)
        roots[size_t(i)] = find(i);
    std::vector<std::int64_t> uniq = roots;
    std::sort(uniq.begin(), uniq.end());
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    std::map<std::int64_t, std::int32_t> lab;
    for (size_t k = 0; k < uniq.size(); ++k)
        lab[uniq[k]] = std::int32_t(k);
    for (size_t i = 0; i < run_of.size(); ++i)
        if (mask.flat()[i])
            out.flat()[i] = lab[roots[size_t(run_of[i])]] + 1;
    return {out, static_cast<long long>(uniq.size())};
}

std::vector<std::pair<Flat, long long>> colour_areas(const grid::Grid<std::uint16_t> &idm,
                                                     const grid::Grid<std::int32_t> &rgb_keys,
                                                     const std::vector<std::tuple<int, int, int>> &colours)
{
    const long long W = idm.w();
    Flat zero;
    for (size_t i = 0; i < idm.flat().size(); ++i)
        if (idm.flat()[i] == 0)
            zero.push_back(std::int64_t(i));
    std::vector<std::pair<Flat, long long>> out;
    for (const auto &[r, g, b] : colours) {
        const std::int32_t k = (r << 16) | (g << 8) | b;
        Flat flat;
        for (std::int64_t i : zero)
            if (rgb_keys.flat()[size_t(i)] == k)
                flat.push_back(i);
        if (flat.empty())
            continue;
        Flat ys, xs;
        for (std::int64_t f : flat) {
            ys.push_back(py::floordiv(f, W));
            xs.push_back(py::mod(f, W));
        }
        const long long y0 = *std::min_element(ys.begin(), ys.end()), x0 = *std::min_element(xs.begin(), xs.end());
        const long long y1 = *std::max_element(ys.begin(), ys.end()) + 1, x1 = *std::max_element(xs.begin(), xs.end()) + 1;
        grid::Grid<std::uint8_t> sub(y1 - y0, x1 - x0, 0);
        for (size_t i = 0; i < flat.size(); ++i)
            sub(ys[i] - y0, xs[i] - x0) = 1;
        const auto lab = labels4(sub).first;
        std::map<std::int32_t, std::vector<size_t>> groups;   // (a stable sort by label: flat order inside)
        for (size_t i = 0; i < flat.size(); ++i)
            groups[lab(ys[i] - y0, xs[i] - x0)].push_back(i);
        for (const auto &[_l, sel] : groups) {
            Flat sy, sx, sf;
            for (size_t i : sel) {
                sy.push_back(ys[i]);
                sx.push_back(xs[i]);
                sf.push_back(flat[i]);
            }
            if (const auto to = _vote(idm, sy, sx, 0))
                out.emplace_back(sf, *to);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- history
QString history_folder(const moddata::Mod &mod, const mapview::MapData &md, const history::ProvinceHistory &hist,
                       long long pid)
{
    // history/provinces/<folder> for a new history file: where most of its state's (else its neighbours') history
    // files are, else the biggest folder.
    auto folders = [&](const std::vector<long long> &ps) -> std::optional<QString> {
        std::vector<QString> order;                  // (a Counter: first seen wins a tie)
        std::map<QString, long long> cnt;
        for (long long q : ps) {
            const auto *fl = hist.files.get(q);
            if (!fl || fl->empty() || !(*fl)[0]->rel.contains(u'/'))
                continue;
            const QString f = (*fl)[0]->rel.split(u'/')[0];
            if (!cnt.count(f))
                order.push_back(f);
            cnt[f] += 1;
        }
        if (order.empty())
            return std::nullopt;
        QString best = order[0];
        for (const QString &f : order)
            if (cnt[f] > cnt[best])
                best = f;
        return best;
    };
    const moddata::Province *pr = mod.provinces.get(pid);
    const moddata::Region *reg = pr && pr->region && !pr->region->isEmpty() ? mod.regions.get(*pr->region) : nullptr;
    std::vector<std::vector<long long>> groups;
    groups.push_back(reg ? reg->provinces : std::vector<long long>());
    groups.push_back(md.neighbours(pid));
    std::vector<long long> all;
    for (const auto &kv : hist.files)
        all.push_back(kv.first);
    groups.push_back(all);
    for (const auto &g : groups) {
        std::vector<long long> ps;
        for (long long q : g)
            if (q != pid)
                ps.push_back(q);
        if (const auto f = folders(ps))
            return *f;
    }
    return u"new"_s;
}

QString history_rel(const moddata::Mod &mod, const mapview::MapData &md, const history::ProvinceHistory &hist,
                    long long pid)
{
    QString name = py::strip(mod.provinces.get(pid)->name);
    if (name.isEmpty())
        name = u"Province %1"_s.arg(pid);
    return u"%1/%2 - %3.txt"_s.arg(history_folder(mod, md, hist, pid), QString::number(pid), newprov::safe_file_name(name));
}

}  // namespace autofix
