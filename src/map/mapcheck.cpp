// mapcheck.py — the Errors tab's checks. Line by line.
#include "map/mapcheck.h"
#include "core/provbuild.h"
#include "core/cbcheck.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/entryview_data.h"
#include "core/eventtools.h"
#include "core/fs.h"
#include "core/history.h"
#include "map/iconstrips_data.h"
#include "map/mapsize.h"
#include "map/nationextras_data.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/oob.h"
#include "core/popcaps.h"
#include "core/popcheck.h"
#include "core/production.h"
#include "core/provrenumber.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/staterefs.h"
#include "grid/pyset.h"
#include "map/newprov.h"
#include "map/provbmp.h"
#include "map/provpanel_data.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

using namespace Qt::StringLiterals;

namespace mapcheck {
// the first index >= i whose byte is not 0 (n when none) — zeros skipped 8 at a time (most masks are nearly empty)
static size_t next_nonzero(const std::uint8_t *p, size_t i, size_t n)
{
    for (; i < n && (i & 7); ++i)
        if (p[i])
            return i;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t v;
        std::memcpy(&v, p + i, 8);
        if (v)
            break;
    }
    for (; i < n; ++i)
        if (p[i])
            return i;
    return n;
}

Pieces province_pieces(const grid::Grid<std::int32_t> &idm)
{
    // Runs of one id along each row, joined across rows where two runs of the same id overlap (union-find). The
    // union always keeps the smaller root, so a piece's root is its lowest run — the order np.unique gives the roots
    // and the run the Python takes as the piece's first. (C++ only, 2026-11-10, the performance round: the runs of
    // two rows are walked side by side instead of per-pixel run numbers and a sorted list of joins; the same pieces.)
    const std::ptrdiff_t H = idm.h(), W = idm.w();
    const std::int64_t N = std::int64_t(H) * W;
    std::vector<std::int64_t> flat_start, run_pid;
    std::vector<size_t> row_first(static_cast<size_t>(H) + 1, 0);
    for (std::ptrdiff_t y = 0; y < H; ++y) {
        row_first[size_t(y)] = flat_start.size();
        const std::int32_t *r = idm.row(y);
        for (std::ptrdiff_t x = 0; x < W; ++x)
            if (x == 0 || r[x] != r[x - 1]) {
                flat_start.push_back(std::int64_t(y) * W + x);
                run_pid.push_back(r[x]);
            }
    }
    row_first[size_t(H)] = flat_start.size();
    const size_t R = flat_start.size();
    std::vector<std::int32_t> parent(R);         // (runs: far fewer than 2^31)
    for (size_t i = 0; i < R; ++i)
        parent[i] = std::int32_t(i);
    auto find = [&](std::int32_t x) {
        std::int32_t root = x;
        while (parent[size_t(root)] != root)
            root = parent[size_t(root)];
        while (parent[size_t(x)] != root) {
            const std::int32_t nx = parent[size_t(x)];
            parent[size_t(x)] = root;
            x = nx;
        }
        return root;
    };
    auto end_of = [&](size_t k, size_t row_end, std::int64_t row0) {   // a run's end column
        return k + 1 < row_end ? flat_start[k + 1] - row0 : std::int64_t(W);
    };
    for (std::ptrdiff_t y = 1; y < H; ++y) {
        const std::int64_t r0a = std::int64_t(y - 1) * W, r0b = std::int64_t(y) * W;
        size_t i = row_first[size_t(y - 1)], j = row_first[size_t(y)];
        const size_t ie = row_first[size_t(y)], je = row_first[size_t(y) + 1];
        while (i < ie && j < je) {                   // (run i of the row above and run j always overlap here)
            const std::int64_t a1 = end_of(i, ie, r0a), b1 = end_of(j, je, r0b);
            if (run_pid[i] == run_pid[j]) {
                const std::int32_t rx = find(std::int32_t(i)), ry = find(std::int32_t(j));
                if (rx != ry) {
                    if (rx < ry)
                        parent[size_t(ry)] = rx;
                    else
                        parent[size_t(rx)] = ry;
                }
            }
            if (a1 < b1)
                ++i;
            else if (b1 < a1)
                ++j;
            else {
                ++i;
                ++j;
            }
        }
    }
    // the roots in ascending order (np.unique), each piece's size; its first run is its root
    std::vector<std::int32_t> slot(R, -1);       // root -> its position among the roots
    Pieces out;
    for (size_t i = 0; i < R; ++i)
        if (find(std::int32_t(i)) == std::int32_t(i)) {
            slot[i] = std::int32_t(out.pid.size());
            out.pid.push_back(run_pid[i]);
            out.first.push_back(flat_start[i]);
        }
    out.size.assign(out.pid.size(), 0);
    for (size_t i = 0; i < R; ++i)
        out.size[size_t(slot[size_t(find(std::int32_t(i)))])] += (i + 1 < R ? flat_start[i + 1] : N) - flat_start[i];
    return out;
}

const Pieces &pieces_of(const mapview::MapData &md)
{
    static struct {
        std::optional<std::tuple<const void *, long long, long long, long long, long long>> key;
        Pieces p;
    } kept;
    const auto k = std::make_tuple(static_cast<const void *>(&md), md.uid, md.version, md.idmap.h(), md.idmap.w());
    if (kept.key != k) {
        grid::Grid<std::int32_t> idm(md.idmap.h(), md.idmap.w());
        for (size_t i = 0; i < idm.flat().size(); ++i)
            idm.flat()[i] = md.idmap.flat()[i];
        kept.p = province_pieces(idm);
        kept.key = k;
    }
    return kept.p;
}

std::vector<std::int64_t> province_part_sizes(const mapview::MapData &md, long long pid)
{
    if (!(0 < pid && pid < md.n) || md.area[pid] == 0 || md.x1[pid] < 0)
        return {};
    const long long y0 = md.y0[pid], y1 = md.y1[pid] + 1, x0 = md.x0[pid], x1 = md.x1[pid] + 1;
    grid::Grid<std::int32_t> sub(y1 - y0, x1 - x0);
    for (long long y = y0; y < y1; ++y)
        for (long long x = x0; x < x1; ++x)
            sub(y - y0, x - x0) = md.idmap(y, x) == pid ? 1 : 0;
    const Pieces p = province_pieces(sub);
    std::vector<std::int64_t> out;
    for (size_t k = 0; k < p.pid.size(); ++k)
        if (p.pid[k] == 1)
            out.push_back(p.size[k]);
    std::stable_sort(out.begin(), out.end(), std::greater<>());
    return out;
}



using ll = long long;

const QString SEV_ERROR = u"error"_s, SEV_WARN = u"warn"_s, SEV_INFO = u"info"_s;

bool Val::truthy() const
{
    if (const auto *b = std::get_if<bool>(&v))
        return *b;
    if (const auto *i = std::get_if<long long>(&v))
        return *i != 0;
    if (const auto *d = std::get_if<double>(&v))
        return *d != 0;
    if (const auto *s = std::get_if<QString>(&v))
        return !s->isEmpty();
    if (const auto *l = std::get_if<std::vector<Val>>(&v))
        return !l->empty();
    return false;
}

Val tup(std::initializer_list<Val> items) { return Val(std::vector<Val>(items)); }

static Val ints(const std::vector<long long> &l)
{
    std::vector<Val> out;
    for (long long x : l)
        out.emplace_back(x);
    return Val(out);
}
static Val texts(const QStringList &l)
{
    std::vector<Val> out;
    for (const QString &x : l)
        out.emplace_back(x);
    return Val(out);
}

QString _n(long long n, const QString &one, const QString &many) { return u"%1 %2"_s.arg(n).arg(n == 1 ? one : many); }

static Category cat(const QString &key, const QString &title, const QString &severity, std::vector<Item> items,
                    std::optional<Fix> fix = std::nullopt, const QString &note = QString(),
                    OptStr tab = u"map"_s, OptStr fix_one = std::nullopt)
{
    return Category{key, title, severity, std::move(items), std::move(fix), note, std::move(tab), std::move(fix_one)};
}
static Item item(const QString &text, std::vector<long long> pids = {}, std::optional<XY> point = std::nullopt,
                 Val data = Val())
{
    return Item{text, std::move(pids), point, std::move(data), std::nullopt};
}

// ---------------------------------------------------------------------------------------------- geometry
UnknownColours unknown_colours(const QString &bmp_path,
                                                                 const grid::Grid<std::uint16_t> &idm,
                                                                 const std::vector<Rgb> &province_rgbs)
{
    // Colours on the map that no province uses: {(r, g, b): (pixel count, flat index of one pixel)}. Pixels erased
    // in the editor still have their province's colour in the file: those are skipped.
    UnknownColours out;
    bool any0 = false;
    for (std::uint16_t v : idm.flat())
        if (v == 0) {
            any0 = true;
            break;
        }
    if (!any0)
        return out;
    // (the file read once per change of it — C++ only, 2026-11-10: read again after every stroke, 38 ms)
    static std::optional<std::tuple<QString, qint64, qint64>> img_key;
    static QImage kept_img;
    const QFileInfo fi(bmp_path);
    const auto ik = std::make_tuple(fi.absoluteFilePath(), fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1,
                                    fi.exists() ? fi.size() : -1);
    if (img_key != ik || !fi.exists()) {
        kept_img = QImage(bmp_path).convertToFormat(QImage::Format_RGB888);
        img_key = ik;
    }
    const QImage &img = kept_img;
    const long long w = img.width(), h = img.height();
    std::map<std::int64_t, std::pair<long long, long long>> found;   // key -> (count, first flat index)
    for (long long y = 0; y < idm.h(); ++y) {
        const uchar *row = img.constScanLine(int(h - 1 - y));          // the map view is flipped like MapData
        for (long long x = 0; x < idm.w() && x < w; ++x) {
            if (idm(y, x) != 0)
                continue;
            const std::int64_t k = (std::int64_t(row[3 * x]) << 16) | (std::int64_t(row[3 * x + 1]) << 8) | row[3 * x + 2];
            auto it = found.find(k);
            if (it == found.end())
                found.emplace(k, std::make_pair(1LL, y * idm.w() + x));
            else
                it->second.first += 1;
        }
    }
    std::set<std::int64_t> known;
    for (const auto &[r, g, b] : province_rgbs)
        known.insert((std::int64_t(r) << 16) | (std::int64_t(g) << 8) | b);
    for (const auto &[k, cf] : found)
        if (!known.count(k))
            out.emplace_back(Rgb{int((k >> 16) & 255), int((k >> 8) & 255), int(k & 255)}, cf);
    return out;
}

static double mtime_of(const QString &path)
{
    const QFileInfo fi(path);
    return fi.isFile() ? double(fi.lastModified().toMSecsSinceEpoch()) / 1000.0 : 0.0;
}

std::optional<BitmapInfo> GeometryCache::bitmap(const QString &path)
{
    // (width, height, 8-bit palette?) of a bitmap, cached per file change.
    const auto bkey = std::make_pair(path, mtime_of(path));
    auto it = _bmps.find(bkey);
    if (it == _bmps.end()) {
        const QImage img(path);
        it = _bmps.emplace(bkey, img.isNull() ? std::nullopt
                                             : std::optional<BitmapInfo>(BitmapInfo{img.width(), img.height(),
                                                                                    img.format() == QImage::Format_Indexed8}))
                 .first;
    }
    return it->second;
}

std::pair<const Pieces *, const UnknownColours *>
GeometryCache::get(const mapview::MapData &md, const QString &bmp_path, const std::vector<Rgb> &province_rgbs)
{
    const auto k = std::make_tuple(static_cast<const void *>(&md.idmap), md.version, mtime_of(bmp_path));
    if (this->key != k) {
        pieces = pieces_of(md);
        unknown = unknown_colours(bmp_path, md.idmap, province_rgbs);
        zero_small = provbmp::small_pieces(md.idmap, STRAY_MAX, 0, &pieces);
        this->key = k;
    }
    return {&pieces, &unknown};
}

// ---------------------------------------------------------------------------------------------- checks
XY _pt(const mapview::MapData &md, long long flat)
{
    return {double(py::mod(flat, md.w)) + 0.5, double(py::floordiv(flat, md.w)) + 0.5};
}

QString fmt_n(long long n) { return py::fmt_commas(double(n), 0); }

XY _centre(const mapview::MapData &md, long long pid) { return {md.cx[pid] + 0.5, md.cy[pid] + 0.5}; }

std::vector<std::pair<QString, QString>> stray_mod_files(const moddata::Mod &mod)
{
    // [(rel below <mod>/mod, how it compares)] of a `mod` folder at the top of the mod (made by older ModEditors — the
    // game never reads it). Each file is compared with the mod's own file it stands for (the path after mod/<name>/).
    const QString top = pypath::join(mod.root, u"mod"_s);
    if (!fs::isdir(top))
        return {};
    std::vector<std::pair<QString, QString>> out;
    QDirIterator it(top, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString full = QDir::toNativeSeparators(it.next());
        QString rel = pypath::relpath(full, mod.root).replace(u'\\', u'/');
        const QStringList parts = rel.split(u'/');
        std::optional<QString> real;
        if (parts.size() > 2)
            real = pypath::join(mod.root, parts.mid(2));
        QString how;
        const QString tail = parts.mid(2).join(u'/');
        if (real && fs::isfile(*real)) {
            bool same = false;
            QFile a(*real), b(full);
            if (a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly))
                same = a.readAll() == b.readAll();
            how = same ? u"the same as the mod's %1"_s.arg(tail)
                       : u"DIFFERS from the mod's %1 — it holds edits that never reached it"_s.arg(tail);
        } else {
            how = u"the mod has no such file"_s;
        }
        out.emplace_back(rel, how);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Category> session_checks(moddata::Mod &mod, const mapview::MapData &md, document::Document &doc)
{
    // Problems of the unsaved edits: factories over the state rules (removed on Apply), new provinces that aren't
    // painted / finished, and the last failed Apply.
    std::vector<Category> cats;
    if (doc.last_error && !doc.last_error->isEmpty()) {
        std::vector<Item> items;
        for (const QString &line : py::splitlines(*doc.last_error))
            if (!py::strip(line).isEmpty())
                items.push_back(item(line));
        if (items.empty())
            items.push_back(item(*doc.last_error));
        cats.push_back(cat(u"apply"_s, u"Apply failed — nothing after the error was written"_s, SEV_ERROR, items,
                           std::nullopt, QString(), std::nullopt));
    }
    std::vector<Item> items;
    for (const document::FactoryProblem &fp : doc.factory_problems(doc.session_rids())) {
        QString name = fp.f.building.value_or(u"None"_s);
        if (fp.f.building)
            if (const moddata::Building *b = mod.buildings.get(*fp.f.building))
                name = b->name;
        const moddata::Province *pr = mod.provinces.get(fp.pid);
        Item it = item(u"%1 in #%2 %3 (state %4): %5"_s.arg(name, QString::number(fp.pid), pr ? pr->name : u"?"_s, fp.rid,
                                                             fp.reason),
                       {fp.pid},
                       (0 < fp.pid && fp.pid < md.n && md.area[fp.pid]) ? std::optional<XY>(_centre(md, fp.pid)) : std::nullopt,
                       tup({fp.rid, fp.pid, fp.f.building, fp.reason}));
        it.factory = fp;
        items.push_back(it);
    }
    std::optional<Fix> fix;
    if (!items.empty())
        fix = Fix{u"Remove these %1 factories now"_s.arg(items.size()), u"factories"_s, Val()};
    cats.push_back(cat(u"factories"_s, u"Factories that break the state rules — removed on Apply"_s, SEV_ERROR, items, fix,
                       u"States edited in this session may have at most 8 factories, and shipyards need a coastal state. "
                       u"Remove others in Provinces to keep these, or let Apply remove them (from earlier history they "
                       u"are removed at their source: every start date)."_s,
                       u"provinces"_s, u"Remove this factory"_s));
    const store::StoreP *dp = doc.maps.get(u"definitions"_s);
    auto *defs = dp ? dynamic_cast<newprov::ProvinceDefs *>(dp->get()) : nullptr;
    if (defs && !defs->added.isEmpty()) {
        std::vector<Item> its;
        std::vector<long long> pids;
        for (const auto &kv : defs->added)
            pids.push_back(kv.first);
        std::sort(pids.begin(), pids.end());
        auto &bmp = doc.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
        for (long long pid : pids) {
            const auto [k, text] = newprov::new_status(*defs, bmp, md, pid);
            if (k != u"done")
                its.push_back(item(u"#%1 %2: %3"_s.arg(QString::number(pid), defs->added.get(pid)->name, text), {pid},
                                   md.area[pid] ? std::optional<XY>(_centre(md, pid)) : std::nullopt, Val(k)));
        }
        long long ready = 0;
        for (const Item &it : its)
            ready += it.data == Val(u"todo"_s) || it.data == Val(u"changed"_s);
        std::optional<Fix> f2;
        if (ready)
            f2 = Fix{u"Finish the "_s + _n(ready, u"painted new province"_s, u"painted new provinces"_s), u"newprov"_s, Val()};
        cats.push_back(cat(u"newprov"_s, u"New provinces not finished (Drawing)"_s, SEV_ERROR, its, f2,
                           u"A province that isn't on the map, or has no positions and coast crossings, can crash or "
                           u"confuse the game. Paint it and press Finish in Drawing."_s,
                           u"map"_s, u"Finish this one"_s));
    }
    return cats;
}

const std::vector<std::pair<QString, QString>> RIVER_RULES = {
    {u"block"_s, u"river 2 pixels wide or more (rivers must be 1 pixel wide)"_s},
    {u"diag"_s, u"river pixel joined only diagonally (rivers need side-by-side pixels)"_s},
    {u"water"_s, u"river pixel on water / no province"_s},
    {u"source"_s, u"source (green) that isn't at the end of a river"_s},
    {u"merge"_s, u"merge that doesn't sit between its river and the river it joins"_s},
};

Masks river_problem_masks(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R, long long y0,
                          std::optional<long long> y1o, long long x0, std::optional<long long> x1o)
{
    // {rule: bool mask} of the rivers.bmp pixels in the window that break a rule (computed with a 1 px margin so
    // window edges are judged like the rest). Every pixel of a too-wide 2x2 block is marked.
    const long long H = R.h(), W = R.w();
    const long long y1 = y1o.value_or(H), x1 = x1o.value_or(W);
    const long long Y0 = std::max(0LL, y0 - 1), X0 = std::max(0LL, x0 - 1), Y1 = std::min(H, y1 + 1), X1 = std::min(W, x1 + 1);
    const long long h = std::max(0LL, Y1 - Y0), w = std::max(0LL, X1 - X0);
    // (C++ only, 2026-11-10, the performance round — the same rules as before, ~70 ms → a few: the river pixels of the
    // window with its margin in a grid with a ring of "no river" around it, so no neighbour needs a bounds test; the
    // masks are made for the asked window only)
    const long long PW = w + 2;
    std::vector<std::uint8_t> riv(static_cast<size_t>((h + 2) * PW), 0);   // padded: (y + 1, x + 1)
    for (long long y = 0; y < h; ++y) {
        const std::uint8_t *rr = R.row(Y0 + y) + X0;
        std::uint8_t *pr = riv.data() + (y + 1) * PW + 1;
        for (long long x = 0; x < w; ++x)
            pr[x] = rr[x] < 12;
    }
    // tl(y, x): the 2x2 block whose top-left is (y, x) is all river (padded like riv; the ring stays 0)
    std::vector<std::uint8_t> tl(riv.size(), 0);
    for (long long y = 0; y + 1 < h; ++y) {
        const std::uint8_t *a = riv.data() + (y + 1) * PW + 1, *b = a + PW;
        std::uint8_t *t = tl.data() + (y + 1) * PW + 1;
        for (long long x = 0; x + 1 < w; ++x)
            t[x] = a[x] & a[x + 1] & b[x] & b[x + 1];
    }
    const long long sy = y0 - Y0, sx = x0 - X0, hh = std::max(0LL, y1 - y0), ww = std::max(0LL, x1 - x0);
    grid::Grid<std::uint8_t> block(hh, ww, 0), diag(hh, ww, 0), water(hh, ww, 0), source(hh, ww, 0), merge(hh, ww, 0),
        landsea(hh, ww, 0);
    const std::uint8_t *sea_of = md.sea.data();
    for (long long oy = 0; oy < hh; ++oy) {
        const long long y = sy + oy;                       // (row in the margin window)
        const std::uint8_t *rr = R.row(Y0 + y) + X0;
        const std::uint16_t *ir = md.idmap.row(Y0 + y) + X0;
        const std::uint8_t *pu = riv.data() + y * PW + 1, *pc = pu + PW, *pd = pc + PW;
        const std::uint8_t *tu = tl.data() + y * PW + 1, *tc = tu + PW;
        std::uint8_t *ob = block.row(oy), *od = diag.row(oy), *ow = water.row(oy), *os = source.row(oy),
                     *om = merge.row(oy), *ol = landsea.row(oy);
        for (long long ox = 0; ox < ww; ++ox) {
            const long long x = sx + ox;
            const int n4 = pu[x] + pd[x] + pc[x - 1] + pc[x + 1];
            const int nd = pu[x - 1] + pu[x + 1] + pd[x - 1] + pd[x + 1];
            const std::uint16_t id = ir[x];
            const std::uint8_t rv = rr[x];
            const bool r = pc[x], is_sea = sea_of[id];
            const bool sea = is_sea && id != 0;
            ob[ox] = tc[x] | tc[x - 1] | tu[x] | tu[x - 1];
            od[ox] = r && n4 == 0 && nd > 0;
            ow[ox] = r && (is_sea || id == 0);
            os[ox] = rv == 0 && n4 != 1;
            om[ox] = rv == 1 && n4 != 2;
            // land / sea that don't match the province under it (listed in their own Errors-tab category)
            ol[ox] = (!sea && id != 0 && rv == RIVER_SEA) || (sea && rv == RIVER_LAND);
        }
    }
    Masks m;
    m.insert(u"block"_s, std::move(block));
    m.insert(u"diag"_s, std::move(diag));
    m.insert(u"water"_s, std::move(water));
    m.insert(u"source"_s, std::move(source));
    m.insert(u"merge"_s, std::move(merge));
    m.insert(u"landsea"_s, std::move(landsea));
    return m;
}

std::vector<long long> map_key(const mapview::MapData &md, const std::vector<const layerbmp::IndexedBitmap *> &layers)
{
    // What a result worked out from the province map and bitmap layers depends on.
    std::uint64_t hs = 1469598103934665603ULL;          // (Python: hash(md.sea.tobytes()))
    for (std::uint8_t b : md.sea)
        hs = (hs ^ b) * 1099511628211ULL;
    // (md.uid / pix_stamp, C++ only 2026-11-10: another map at the same address; pixels changed by a stroke not yet
    // finished — the Rivers view takes river_masks_whole too)
    std::vector<long long> k = {static_cast<long long>(reinterpret_cast<std::intptr_t>(&md.idmap)), md.version,
                                static_cast<long long>(hs), md.uid, md.pix_stamp};
    for (const auto *b : layers) {
        k.push_back(b ? b->stamp : -1);
        k.push_back(b && b->data ? static_cast<long long>(reinterpret_cast<std::intptr_t>(&*b->data)) : 0);
    }
    return k;
}

// (C++ only, 2026-11-10, the performance round) a whole-map result kept under map_key `old` (with more after it, as
// the caller adds) and asked for again under `now`: when nothing changed but pixels — of provinces.bmp (version [1],
// pix_stamp [4]) and of the bitmaps named (their stamps [5 + 2i]), each change in the list it keeps — the box to count
// again: the union of the changed boxes, a bitmap's grown by its margin (how far from a pixel the result reads it).
// {0, 0, 0, 0}: nothing to count; None: count everything.
static std::optional<std::array<long long, 4>>
box_since(const std::vector<long long> &old, const std::vector<long long> &now, const mapview::MapData &md,
          const std::vector<std::pair<const layerbmp::IndexedBitmap *, long long>> &layers)
{
    if (old.size() != now.size() || old.size() < 5 + 2 * layers.size())
        return std::nullopt;
    for (size_t i = 0; i < old.size(); ++i) {
        bool may = i == 1 || i == 4;
        for (size_t j = 0; j < layers.size(); ++j)
            may = may || i == 5 + 2 * j;
        if (!may && old[i] != now[i])
            return std::nullopt;
    }
    std::array<long long, 4> u{0, 0, 0, 0};
    bool any = false;
    auto add = [&](std::array<long long, 4> b, long long m) {
        if (b[2] <= b[0] || b[3] <= b[1])
            return;
        b = {std::max(0LL, b[0] - m), std::max(0LL, b[1] - m), std::min(md.w, b[2] + m), std::min(md.h, b[3] + m)};
        if (!any)
            u = b;
        else
            u = {std::min(u[0], b[0]), std::min(u[1], b[1]), std::max(u[2], b[2]), std::max(u[3], b[3])};
        any = true;
    };
    const auto mb = md.changed_since(old[4]);
    if (!mb)
        return std::nullopt;
    add(*mb, 0);
    for (size_t j = 0; j < layers.size(); ++j) {
        const long long was = old[5 + 2 * j];
        if (was == now[5 + 2 * j])
            continue;
        if (!layers[j].first)
            return std::nullopt;
        const auto b = layers[j].first->changed_since(was);
        if (!b)
            return std::nullopt;
        add(*b, layers[j].second);
    }
    return u;
}

const Masks &river_masks_whole(const mapview::MapData &md, const layerbmp::IndexedBitmap &rb)
{
    // river_problem_masks of the whole map — kept until the map or rivers.bmp changes (the checks and the error map
    // both ask for it, after every edit).
    static std::optional<std::vector<long long>> key;
    static Masks masks;
    const auto k = map_key(md, {&rb});
    if (key != k) {
        // (C++ only, 2026-11-10, the performance round) after pixel changes only, the changed box is counted again:
        // a mask reads the province id at the pixel itself and rivers.bmp within 1 pixel of it
        const auto box = key ? box_since(*key, k, md, {{&rb, 1}}) : std::nullopt;
        if (box) {
            const auto [bx0, by0, bx1, by1] = *box;
            if (bx1 > bx0 && by1 > by0) {
                const Masks sub = river_problem_masks(md, *rb.data, by0, by1, bx0, bx1);
                for (auto &[name, g] : masks) {
                    const grid::Grid<std::uint8_t> *s = sub.get(name);
                    if (!s)
                        continue;
                    for (long long y = by0; y < by1; ++y)
                        std::memcpy(g.row(y) + bx0, s->row(y - by0), size_t(bx1 - bx0));
                }
            }
        } else {
            masks = river_problem_masks(md, *rb.data);
        }
        key = k;
    }
    return masks;
}

Category _again(const Category &c) { return c; }      // (a kept category handed out again: a copy)

Category river_checks(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R, const Masks *masks0)
{
    // Pixels of rivers.bmp that break the rules the game expects.
    Masks own;
    if (!masks0)
        own = river_problem_masks(md, R);
    const Masks &masks = masks0 ? *masks0 : own;
    std::vector<Item> items;
    for (const auto &[key, text] : RIVER_RULES) {
        const grid::Grid<std::uint8_t> *m = masks.get(key);
        const bool blk = key == u"block";        // one line per block, at its top-left pixel
        // (C++ only, 2026-11-10: the top-left test made as the rows are read, no whole-map grid first)
        auto top_left = [&](long long y, long long x) {
            return y + 1 < R.h() && x + 1 < R.w() && R(y + 1, x + 1) < 12 && R(y, x + 1) < 12 && R(y + 1, x) < 12 &&
                   R(y, x) < 12;
        };
        // (the mask holds every pixel of a block, its top-left one among them: only marked pixels are looked at)
        const std::uint8_t *mp = m->flat().data();
        const size_t mn = m->flat().size();
        const long long mw = m->w();
        long long n = 0;
        for (size_t i = next_nonzero(mp, 0, mn); i < mn && n < 300; i = next_nonzero(mp, i + 1, mn)) {
            const long long y = static_cast<long long>(i) / mw, x = static_cast<long long>(i) % mw;
            if (!blk || top_left(y, x)) {
                // no province selection: its red hatching would hide the pixel
                items.push_back(item(u"%1 at %2, %3"_s.arg(text).arg(x).arg(y), {}, XY{double(x) + 0.5, double(y) + 0.5},
                                     tup({u"river"_s, key})));
                ++n;
            }
        }
    }
    std::optional<Fix> fix;
    if (!items.empty())
        fix = Fix{u"Repair all %1 automatically"_s.arg(_n(ll(items.size()), u"spot"_s, u"spots"_s)), u"rivers"_s, Val()};
    return cat(u"rivers"_s, u"Rivers (rivers.bmp)"_s, SEV_WARN, items, fix,
               u"Selecting one shows the river map there, with the broken pixels in red. Repair: rivers on water go, "
               u"2-pixel-wide spots lose a pixel, diagonal joints get a connecting pixel, misplaced sources / merges "
               u"become normal river. What can't be decided stays listed — fix it in Drawing → Rivers (double-click)."_s,
               u"map"_s, u"Repair this spot"_s);
}

static std::map<const moddata::Mod *, OrderedMap<long long, QString>> &terrain_cache()
{
    static std::map<const moddata::Mod *, OrderedMap<long long, QString>> c;   // (Python: mod._terrain_types)
    return c;
}

// landsea_masks of the window y0:y1, x0:x1 (the masks are the window's size)
static OrderedMap<QString, MaskPair> landsea_masks_in(const moddata::Mod &mod, const mapview::MapData &md,
                                                      layerbmp::IndexedBitmap *tb, layerbmp::IndexedBitmap *rb,
                                                      long long y0, long long y1, long long x0, long long x1)
{
    const long long h = y1 - y0, w = x1 - x0;
    OrderedMap<QString, MaskPair> out;
    // (C++ only, 2026-11-10: the tests are inlined — one call through std::function per pixel cost ~40 ms a map)
    auto make = [&](const grid::Grid<std::uint8_t> &B, const auto &is_sea_v, const auto &is_land_v) {
        MaskPair p{grid::Grid<std::uint8_t>(h, w, 0), grid::Grid<std::uint8_t>(h, w, 0)};
        const std::uint8_t *sea_of = md.sea.data();
        for (long long y = 0; y < h; ++y) {
            const std::uint16_t *ir = md.idmap.row(y0 + y) + x0;
            const std::uint8_t *br = B.row(y0 + y) + x0;
            std::uint8_t *a = p.first.row(y), *b = p.second.row(y);
            for (long long x = 0; x < w; ++x) {
                const std::uint16_t id = ir[x];
                if (id == 0)
                    continue;
                const bool sea = sea_of[id];
                a[x] = !sea && is_sea_v(br[x]);
                b[x] = sea && is_land_v(br[x]);
            }
        }
        return p;
    };
    if (tb && tb->load()) {
        auto &cache = terrain_cache();
        if (!cache.count(&mod))
            cache.emplace(&mod, layerbmp::terrain_types(mod));
        const auto &types = cache.at(&mod);
        std::vector<std::uint8_t> ocean(256, 0);
        bool anyo = false;
        for (const auto &[i, t] : types)
            if (t == u"ocean" && i < 256) {
                ocean[size_t(i)] = 1;
                anyo = true;
            }
        if (!anyo)
            ocean[254] = 1;
        const auto &T = *tb->data;
        out.insert(u"terrain"_s, make(T, [&](std::uint8_t v) { return bool(ocean[v]); },
                                      [&](std::uint8_t v) { return !ocean[v]; }));
    }
    if (rb && rb->load()) {
        const auto &Rr = *rb->data;
        out.insert(u"rivers"_s, make(Rr, [&](std::uint8_t v) { return v == RIVER_SEA; },
                                     [&](std::uint8_t v) { return v == RIVER_LAND; }));
    }
    return out;
}

OrderedMap<QString, MaskPair> landsea_masks(const moddata::Mod &mod, const mapview::MapData &md,
                                            layerbmp::IndexedBitmap *tb, layerbmp::IndexedBitmap *rb)
{
    // {"terrain" / "rivers": (land pixels the bitmap calls sea, sea pixels it calls land)} — pixels of no province are
    // left out; rivers drawn on the sea are a rivers.bmp rule problem of their own.
    return landsea_masks_in(mod, md, tb, rb, 0, md.idmap.h(), 0, md.idmap.w());
}

// landsea_masks kept, and after a change of provinces.bmp's pixels only (the same map, sea flags, terrain.bmp and
// rivers.bmp) counted again in the changed box only: each pixel's answer reads that pixel alone (C++ only, 2026-11-10,
// the performance round, like river_masks_whole)
const OrderedMap<QString, MaskPair> &landsea_masks_whole(const moddata::Mod &mod, const mapview::MapData &md,
                                                         layerbmp::IndexedBitmap *tb, layerbmp::IndexedBitmap *rb)
{
    static std::optional<std::vector<long long>> key;
    static OrderedMap<QString, MaskPair> masks;
    const bool t_ok = tb && tb->load(), r_ok = rb && rb->load();
    std::vector<long long> k = map_key(md, {tb, rb});
    k.insert(k.end(), {static_cast<long long>(reinterpret_cast<std::intptr_t>(&mod)), t_ok, r_ok});
    if (key != k) {
        const auto box = key ? box_since(*key, k, md, {{tb, 0}, {rb, 0}}) : std::nullopt;
        if (box) {
            const auto [bx0, by0, bx1, by1] = *box;
            if (bx1 > bx0 && by1 > by0) {
                const auto sub = landsea_masks_in(mod, md, tb, rb, by0, by1, bx0, bx1);
                for (auto &[name, pr] : masks) {
                    const MaskPair *s = sub.get(name);
                    if (!s)
                        continue;
                    for (long long y = by0; y < by1; ++y) {
                        std::memcpy(pr.first.row(y) + bx0, s->first.row(y - by0), size_t(bx1 - bx0));
                        std::memcpy(pr.second.row(y) + bx0, s->second.row(y - by0), size_t(bx1 - bx0));
                    }
                }
            }
        } else {
            masks = landsea_masks(mod, md, tb, rb);
        }
        key = std::move(k);
    }
    return masks;
}

std::vector<std::tuple<long long, long long, long long>> _by_province(const mapview::MapData &md,
                                                                     const grid::Grid<std::uint8_t> &mask)
{
    // [(province, pixel count, a pixel in the middle of them)] of the pixels in mask, most pixels first.
    std::map<long long, std::vector<long long>> by;     // (a stable sort by province: flat order inside each)
    const std::uint8_t *mp = mask.flat().data();
    const size_t mn = mask.flat().size();
    for (size_t i = next_nonzero(mp, 0, mn); i < mn; i = next_nonzero(mp, i + 1, mn))
        by[md.idmap.flat()[i]].push_back(static_cast<long long>(i));
    std::vector<std::tuple<long long, long long, long long>> out;
    for (const auto &[p, fs_] : by) {
        const long long n = static_cast<long long>(fs_.size());
        out.emplace_back(p, n, fs_[size_t(n / 2)]);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return -std::get<1>(a) < -std::get<1>(b); });
    return out;
}

std::vector<Category> landsea_checks(const moddata::Mod &mod, const mapview::MapData &md, layerbmp::IndexedBitmap *tb,
                                     layerbmp::IndexedBitmap *rb)
{
    // Errors: terrain.bmp / rivers.bmp say sea where a province is land, or land where it is a sea tile.
    std::vector<Category> cats;
    const auto &masks = landsea_masks_whole(mod, md, tb, rb);
    for (const auto &[kind, fname] : {std::pair{u"terrain"_s, u"terrain.bmp"_s}, {u"rivers"_s, u"rivers.bmp"_s}}) {
        const MaskPair *mp = masks.get(kind);
        if (!mp)
            continue;
        std::vector<std::pair<long long, Item>> items;
        long long total = 0;
        for (const auto &[mask, what] : {std::pair{&mp->first, u"sea"_s}, {&mp->second, u"land"_s}})
            for (const auto &[p, cnt, f] : _by_province(md, *mask)) {
                total += cnt;
                const moddata::Province *pr = mod.provinces.get(p);
                QString nm = pr ? py::strip(pr->name) : u"?"_s;
                if (nm.isEmpty())
                    nm = u"(unnamed)"_s;
                items.emplace_back(cnt, item(u"#%1 %2%3: %4 px %5 %6 in %7"_s.arg(
                                                 QString::number(p), nm, pr && pr->sea ? u" (sea tile)"_s : QString(),
                                                 fmt_n(cnt), cnt == 1 ? u"is"_s : u"are"_s, what, fname),
                                             {}, _pt(md, f), Val(p)));
            }
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.first < -b.first; });
        std::vector<Item> its;
        for (auto &ci : items)
            its.push_back(ci.second);
        std::optional<Fix> fix;
        if (!its.empty())
            fix = Fix{u"Make %1 follow the provinces (%2 px)"_s.arg(fname, fmt_n(total)), u"landsea"_s, Val(kind)};
        const bool ter = kind == u"terrain";
        cats.push_back(cat(
            u"landsea_"_s + kind, u"%1: land / sea don't match the provinces"_s.arg(fname), SEV_ERROR, its, fix,
            u"Land provinces need land in %1 and sea tiles need sea, else the game draws / treats them wrongly. On the "
            u"map the wrong pixels are %2. The fix makes land pixels %3 and sea pixels sea (one undo step, written on "
            u"Apply) — then detail them in Drawing → %4. Double-click a line to go there."_s
                .arg(fname, ter ? u"bright yellow / orange"_s : u"red"_s,
                     ter ? u"red plains (32), like newly painted land"_s : u"plain land (white)"_s, py::capitalize(kind)),
            u"map"_s, u"Fix this province's pixels"_s));
    }
    return cats;
}

const QStringList USELESS_ROW = {u"the two provinces don't touch on the map, so this does nothing"_s,
                                 u"from and to are the same province"_s};

std::vector<Category> adjacency_checks(moddata::Mod &mod, const mapview::MapData &, document::Document &doc,
                                       adjacency::AdjGeometry *geo, double gap)
{
    // map/adjacencies.csv problems (geo: the AdjGeometry of the map). Items carry data for the fixes:
    // ("row", uid, a, b, fix) / ("pair", sea, land) / ("bank", a, b, river name) / ("river", name, tiles).
    using namespace adjacency;
    const store::StoreP *ap = doc.maps.get(u"adjacency"_s);
    if (!ap || !geo)
        return {};
    AdjacencyFile &adj = doc.map<AdjacencyFile>(u"adjacency"_s);
    const auto &provs = mod.provinces;
    auto nm = [&](long long p) {
        const moddata::Province *pr = provs.get(p);
        QString n = pr ? py::strip(pr->name) : u"?"_s;
        if (pr && n.isEmpty())
            n = u"(unnamed)"_s;
        return u"#%1 %2"_s.arg(QString::number(p), n);
    };
    static const OrderedMap<QString, QString> KIND = {
        {u"coast"_s, u"coast crossing"_s}, {u"strait"_s, u"strait"_s}, {u"river"_s, u"Meme-river crossing"_s},
        {u"canal"_s, u"canal"_s}, {u"impassable"_s, u"impassable"_s}, {u"land"_s, u"land link"_s}, {u"other"_s, u"row"_s}};
    auto kind = [&](const Row &r) { return KIND.value(adj.category(r)); };
    const CoastStatus st = coast_status(adj, *geo);
    const auto probs = problems(adj, *geo, mod);
    QSet<long long> stale_ids;
    for (const Row &r : st.stale)
        stale_ids.insert(r.uid);
    OrderedMap<long long, Row> rows;
    for (const Row &r : adj.rows)
        rows.insert(r.uid, r);
    std::vector<Category> cats;
    // Meme rivers whose crossings are in the file but that aren't marked (their shores then count as coasts)
    std::vector<Item> items;
    for (const DetectedRiver &f : detect_rivers(adj, *geo)) {
        QStringList ts;
        for (long long t : f.tiles)
            ts << u"#%1"_s.arg(t);
        items.push_back(item(u"%1: %2 crossings over sea tile%3 %4 — not marked as a Meme river"_s.arg(
                                 f.name, QString::number(f.rows), f.tiles.size() > 1 ? u"s"_s : QString(), ts.join(u", "_s)),
                             f.tiles, std::nullopt, tup({u"river"_s, f.name, ints(f.tiles)})));
    }
    std::optional<Fix> fix;
    if (!items.empty())
        fix = Fix{u"Mark all "_s + _n(ll(items.size()), u"Meme river"_s, u"Meme rivers"_s), u"adj_mark"_s, Val()};
    cats.push_back(cat(u"adj_rivers"_s, u"Adjacencies: Meme rivers that aren't marked"_s, SEV_WARN, items, fix,
                       u"These crossings run over thin sea tiles like a Meme river, but the tiles aren't marked as one "
                       u"(\"# Meme river: … = sea tile …\" in adjacencies.csv). Marked, their shores need no coast "
                       u"crossings and both banks get checked — so mark them BEFORE adding missing coast crossings."_s,
                       u"map"_s, u"Mark this Meme river"_s));
    // missing coast crossings
    const QString com = adj.coast_comment();
    items.clear();
    for (const auto &[s, l] : st.missing)
        items.push_back(item(u"%1 ↔ %2: no coast crossing"_s.arg(nm(s), nm(l)), {s, l}, std::nullopt, tup({u"pair"_s, s, l})));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Add all %1 missing coast crossings"_s.arg(items.size()), u"adj_add_coast"_s, Val()};
    cats.push_back(cat(u"adj_missing"_s, u"Adjacencies: missing coast crossings"_s, SEV_WARN, items, fix,
                       u"Every border of a sea tile and a land province needs a row in adjacencies.csv (any type); the "
                       u"fix adds sea tile;province;sea;0;0;%1. Shores of Meme rivers and no-coast tiles need none — "
                       u"mark unmarked Meme rivers first (the group above)."_s.arg(com),
                       u"map"_s, u"Add this coast crossing"_s));
    // stale coast rows
    items.clear();
    for (const Row &r : st.stale)
        items.push_back(item(u"%1 ↔ %2: coast crossing, but they don't touch (stale row)"_s.arg(nm(r.a), nm(r.b)), {r.a, r.b},
                             std::nullopt, tup({u"row"_s, r.uid, r.a, r.b, u"delete"_s})));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Remove all "_s + _n(ll(items.size()), u"stale row"_s, u"stale rows"_s), u"adj_rows"_s, Val()};
    cats.push_back(cat(u"adj_stale"_s, u"Adjacencies: coast rows whose tiles don't touch (stale)"_s, SEV_WARN, items, fix,
                       u"Leftovers (usually from vanilla or from moved coasts): the sea tile and the province don't "
                       u"touch on the map, so the row does nothing."_s,
                       u"map"_s, u"Remove this row"_s));
    // duplicates / rows that do nothing / other wrong rows
    std::vector<Item> dup, useless, other;
    for (const auto &[uid, ts] : probs) {
        const Row *r = rows.get(uid);
        if (!r || stale_ids.contains(uid))
            continue;
        const QString head = u"%1 → %2 (%3%4)"_s.arg(nm(r->a), nm(r->b), kind(*r),
                                                     r->through ? u" via #%1"_s.arg(r->through) : QString());
        for (const QString &t : ts) {
            if (t.contains(u"stale row"_s))
                continue;
            if (t == u"duplicate of another row") {
                dup.push_back(item(head + u" is in the file twice"_s, {r->a, r->b}, std::nullopt,
                                   tup({u"row"_s, uid, r->a, r->b, u"delete"_s})));
            } else if (USELESS_ROW.contains(t)) {
                useless.push_back(item(head + u": "_s + t, {r->a, r->b}, std::nullopt,
                                       tup({u"row"_s, uid, r->a, r->b, u"delete"_s})));
            } else {
                const Val fx = t.endsWith(u"is not a province in definition.csv") ? Val(u"delete"_s)
                               : t.startsWith(u"Data is only used by canals")      ? Val(u"data0"_s)
                                                                                   : Val();
                std::vector<long long> ps;
                for (long long p : {r->a, r->b})
                    if (provs.contains(p))
                        ps.push_back(p);
                other.push_back(item(head + u": "_s + t, ps, std::nullopt, tup({u"row"_s, uid, r->a, r->b, fx})));
            }
        }
    }
    fix.reset();
    if (!dup.empty())
        fix = Fix{u"Remove all "_s + _n(ll(dup.size()), u"duplicate"_s, u"duplicates"_s), u"adj_rows"_s, Val()};
    cats.push_back(cat(u"adj_dup"_s, u"Adjacencies: duplicate rows"_s, SEV_WARN, dup, fix,
                       u"The same crossing is in adjacencies.csv more than once; the first stays."_s, u"map"_s,
                       u"Remove this duplicate"_s));
    fix.reset();
    if (!useless.empty())
        fix = Fix{u"Remove all "_s + _n(ll(useless.size()), u"row"_s, u"rows"_s), u"adj_rows"_s, Val()};
    cats.push_back(cat(u"adj_useless"_s, u"Adjacencies: rows that do nothing"_s, SEV_WARN, useless, fix,
                       u"Impassable borders between provinces that don't touch, and rows from a province to itself: the "
                       u"game has nothing to apply them to."_s,
                       u"map"_s, u"Remove this row"_s));
    long long nfix = 0;
    for (const Item &it : other)
        nfix += it.data[4].truthy();
    fix.reset();
    if (nfix)
        fix = Fix{u"Fix the %1 automatically"_s.arg(_n(nfix, u"row that can be"_s, u"rows that can be"_s)), u"adj_rows"_s, Val()};
    cats.push_back(cat(u"adj_other"_s, u"Adjacencies: other rows that look wrong"_s, SEV_WARN, other, fix,
                       u"Rows with a province that isn't in definition.csv are removed; a Data value on a non-canal row "
                       u"is set to 0. The rest need a decision — double-click to edit the row in Adjacencies."_s,
                       u"map"_s, u"Fix this row"_s));
    // missing Meme-river bank crossings
    items.clear();
    for (const auto &[name, v] : river_status(adj, *geo, gap))
        for (const auto &[a, b] : v.missing)
            items.push_back(item(u"%1: %2 ↔ %3 have no bank crossing"_s.arg(name, nm(a), nm(b)), {a, b}, std::nullopt,
                                 tup({u"bank"_s, a, b, name})));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Add all "_s + _n(ll(items.size()), u"missing bank crossing"_s, u"missing bank crossings"_s),
                  u"adj_add_bank"_s, Val()};
    cats.push_back(cat(u"adj_banks"_s, u"Adjacencies: missing Meme-river bank crossings"_s, SEV_WARN, items, fix,
                       u"Provinces facing each other across a marked Meme river should be joined by a crossing named "
                       u"after the river (a;b;sea;0;0;<river>)."_s,
                       u"map"_s, u"Add this bank crossing"_s));
    return cats;
}

static Val cb_data(const std::optional<cbcheck::Data> &d)
{
    if (!d)
        return Val();
    std::vector<Val> out;
    for (const cbcheck::Val &x : *d) {
        if (const auto *i = std::get_if<long long>(&x))
            out.emplace_back(*i);
        else if (const auto *s = std::get_if<QString>(&x))
            out.emplace_back(*s);
        else
            out.emplace_back();
    }
    return Val(out);
}

std::vector<Category> war_goal_checks(moddata::Mod &mod, document::Document &doc, scriptstore::ScriptStore &store)
{
    // The war goals (common/cb_types.txt) — brackets, whole or not, peace_order, texts — and every script that names a
    // modifier or a war goal that doesn't exist.
    QSet<QString> fresh;
    if (const store::StoreP *lp = doc.maps.get(u"loc"_s))
        if (auto *loc = dynamic_cast<localisation::LocEdits *>(lp->get()))
            for (const auto &kv : loc->fresh)
                fresh.insert(py::lower(kv.first));
    auto has_text = [&](const QString &k) { return refs::loc_text(mod, k).has_value() || fresh.contains(py::lower(k)); };
    QStringList rels;
    for (const QString &folder : cbcheck::SCAN_FOLDERS)
        rels += entryview::_files(mod, &store, folder);
    long long frames = 0;
    try {                                        // how many pictures the strip of war goals has
        static std::map<std::pair<const void *, const void *>, std::unique_ptr<iconstrips::Strips>> strips_of;
        auto &sp = strips_of[{&store, &doc}];
        if (!sp)
            sp = std::make_unique<iconstrips::Strips>(mod, &store);
        const auto sprites = sp->sprites();
        if (const iconstrips::Sprite *s = sprites.get(u"GFX_diplo_wargoal_icons"_s))
            frames = s->frames;
    } catch (const std::exception &) {
        frames = 0;
    }
    QStringList own;
    for (const QString &folder : cbcheck::BRACKET_FOLDERS)
        for (const QString &r : entryview::_files(mod, &store, folder))
            if (entryview::in_mod(mod, store, r))
                own << r;
    static std::map<const void *, cbcheck::Cache> caches;   // (Python: store._ref_scan_cache)
    const cbcheck::Report rep = cbcheck::run(
        [&](const QString &r) { return store.text(r); }, rels, has_text, frames,
        [&](const QString &k) { return refs::loc_text(mod, k); }, &caches[&store],
        [&](const QString &r) { return entryview::in_mod(mod, store, r); }, own);
    std::vector<Category> cats;
    std::vector<Item> items;
    long long n_fix = 0;
    for (const cbcheck::F &f : rep.brackets) {
        items.push_back(item(f.text, {}, std::nullopt, cb_data(f.data)));
        n_fix += f.data && !f.data->empty() && std::get_if<QString>(&(*f.data)[0]) &&
                 *std::get_if<QString>(&(*f.data)[0]) == u"close";
    }
    std::optional<Fix> fix;
    if (n_fix)
        fix = Fix{u"Close the last block of %1"_s.arg(n_fix == 1 ? u"the file"_s : u"the %1 files"_s.arg(n_fix)),
                  u"close_brackets"_s, Val()};
    cats.push_back(cat(
        u"brackets"_s, u"Script files whose brackets don't balance"_s, SEV_ERROR, items, fix,
        u"The mod's own files in common, events, decisions, inventions, poptypes, technologies, units and history/wars: "
        u"every { needs its }. The game reads on without a word — an event, decision or modifier after a { that is "
        u"never closed becomes part of the one before it and no longer exists; lines after a } too many stand outside "
        u"everything. None of the game's own 988 script files has this. Which bracket it is, is a guess from the "
        u"indentation. Only a file that simply lacks the } at its very end can be closed automatically; for the others "
        u"double-click opens the line in Scripts."_s,
        std::nullopt, u"Close its last block"_s));
    items.clear();
    for (const cbcheck::F &f : rep.equals)
        items.push_back(item(f.text, {}, std::nullopt, cb_data(f.data)));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Make each doubled = one (%1 file%2)"_s.arg(items.size()).arg(items.size() == 1 ? u""_s : u"s"_s),
                  u"fix_equals"_s, Val()};
    cats.push_back(cat(
        u"equals"_s, u"Script files with two = in a row (the game crashes on it)"_s, SEV_ERROR, items, fix,
        u"The mod's own files in common, events, decisions, inventions, poptypes, technologies, units and history/wars: "
        u"“key = = value” is no line the game can read. Seen in the game (2026-10-21): one “poor_strata_life_needs = = "
        u"0.95” in a pop type file and the game crashes while it loads (Loading Databases…), before its menu. The "
        u"editor reads past it as if the key weren't there. The fix keeps one = and the value after it; double-click "
        u"opens the line in Scripts."_s,
        std::nullopt, u"Make it one ="_s));
    // C++ only (2026-11-16): a province building of the mod without its picture slot in the province window — the crash
    // (with a fix that says what it writes); since 2026-11-17 what only looks wrong is a warning of its own, without a
    // fix (the user: a fix button nobody understands must not be there)
    items.clear();
    std::vector<Item> looks;
    for (const auto &p : provbuild::window_problems(
             store.text(u"common/buildings.txt"_s), store.text(u"interface/province_interface.gui"_s),
             [&](const QString &k) {
                 const QString *l = mod.loc.get(k);
                 return l && !l->isEmpty() ? *l : k;
             }))
        (p.crash ? items : looks).push_back(item(p.text + u"."_s, {}, std::nullopt, tup({u"build_window"_s, p.key})));
    if (!items.empty())                          // (only then: the Python editor has no such group)
        cats.push_back(cat(
        u"build_window"_s, u"Province buildings without a picture slot in the province window (the game crashes)"_s,
        SEV_ERROR, items, Fix{u"Add the missing picture slots"_s, u"fix_build_window"_s, Val()},
        u"What crashes: every province building in common/buildings.txt (the ones with province = yes, such as fort, "
        u"naval_base, railroad and the mod's own) gets a row in the game's province window, and the game looks for a "
        u"picture for that row in a slot named build_icon0, build_icon1, … (in interface/province_interface.gui). If "
        u"the slot for a building is missing, the game starts fine but crashes the moment you open any province "
        u"(tested in the game, 2026-11-16).\nWhat the fix writes: the missing slots, each with a picture strip of its "
        u"own (gfx/interface, named in interface/modeditor_buildings.gfx), and the window made tall enough for its "
        u"rows — the same that happens when you make a building in Game rules → Building types. Nothing else is "
        u"changed; Ctrl+Z takes it back."_s,
        std::nullopt, u"Add the slots"_s));
    if (!looks.empty())
        cats.push_back(cat(
        u"build_window_look"_s, u"Province window: building rows that won't look right (no crash)"_s, SEV_WARN, looks,
        std::nullopt,
        u"Seen in the game, none of them crashes: rows that lie on top of the army / navy buttons when the window is "
        u"too short for its rows, and pictures of rows 4 and later that the game draws in every row. Game rules → "
        u"Building types shows the window as the game draws it, and lets you lower the rows or make the window taller."_s,
        std::nullopt, std::nullopt));
    items.clear();
    bool any_close = false;
    for (const cbcheck::F &f : rep.broken) {
        const Val d = f.data && !f.data->empty() ? cb_data(f.data)
                             : tup({u"ref"_s, f.rel, f.pos ? Val(static_cast<long long>(*f.pos)) : Val(), Val(f.goal)});
        items.push_back(item(f.text, {}, std::nullopt, d));
        any_close = any_close || (f.data && !f.data->empty() && std::get_if<QString>(&(*f.data)[0]) &&
                                  *std::get_if<QString>(&(*f.data)[0]) == u"close");
    }
    fix.reset();
    if (any_close)
        fix = Fix{u"Close the last war goal"_s, u"close_brackets"_s, Val()};
    cats.push_back(cat(
        u"cb_broken"_s, u"War goals: brackets that don't close — war goals the game can't read"_s, SEV_ERROR, items, fix,
        u"common/cb_types.txt. The game reads on without a word: with a { that is never closed every war goal after it "
        u"becomes part of the one before and no longer exists — and an event, decision or war that names one of them "
        u"crashes the game. Which bracket it is is a guess from the indentation. Double-click opens the line in "
        u"Scripts."_s,
        std::nullopt, u"Close it"_s));
    QSet<QString> made_names;
    for (const cbcheck::F &f : rep.no_mod)
        if (f.data && f.data->size() > 3)
            if (const auto *s = std::get_if<QString>(&(*f.data)[3]))
                made_names.insert(*s);
    const long long made = made_names.size();
    fix.reset();
    if (!rep.no_mod.empty())
        fix = Fix{u"Make the %1 missing modifier%2 (empty, in event_modifiers.txt)"_s.arg(made).arg(made == 1 ? u""_s : u"s"_s),
                  u"make_modifiers"_s, Val()};
    items.clear();
    for (const cbcheck::F &f : rep.no_mod)
        items.push_back(item(f.text, {}, std::nullopt, cb_data(f.data)));
    cats.push_back(cat(
        u"ref_modifier"_s, u"Modifiers named that don't exist (crash)"_s, SEV_ERROR, items, fix,
        u"A war goal, event, decision or other script names a modifier that is in none of event_modifiers.txt, "
        u"static_modifiers.txt and triggered_modifiers.txt (the mod's own file replaces the game's whole file: a "
        u"modifier of the game that the mod's file left out is gone). add_…_modifier of such a name crashes the game "
        u"the moment the line runs — some multiplayer mods do exactly that on purpose to throw a player out; has_… is "
        u"never true and remove_… does nothing, and in a war goal such a line has crashed the game too. The fix makes "
        u"an empty modifier of that name (it does nothing until you give it effects in Events → Modifiers); where the "
        u"name is a slip, double-click and correct the line instead."_s,
        std::nullopt, u"Make this modifier (empty)"_s));
    items.clear();
    for (const cbcheck::F &f : rep.no_cb)
        items.push_back(item(f.text, {}, std::nullopt, cb_data(f.data)));
    cats.push_back(cat(
        u"ref_cb"_s, u"War goals named that don't exist"_s, SEV_ERROR, items, std::nullopt,
        u"An event, decision or war at the start gives or uses a war goal that common/cb_types.txt doesn't have (the "
        u"mod's file replaces the game's: the game's own war goals are only there when the mod's file keeps them). "
        u"Double-click opens the line in Scripts."_s,
        std::nullopt));
    items.clear();
    for (const cbcheck::F &f : rep.lines)
        items.push_back(item(f.text, {}, std::nullopt,
                             tup({u"cb"_s, Val(f.goal), f.pos ? Val(static_cast<long long>(*f.pos)) : Val()})));
    cats.push_back(cat(
        u"cb_lines"_s, u"War goals: lines missing or wrong"_s, SEV_WARN, items, std::nullopt,
        u"Held against the game's own 31 war goals and 1,486 war goals of 22 mods: every war goal of the game has "
        u"sprite_index, is_triggered_only, months, the four …_factor lines, the three break_truce_… lines, "
        u"truce_months and war_name; a demand (po_…) that picks a state or a nation needs allowed_states / "
        u"allowed_countries. Whether the game copes without a line isn't tested — what its value should be is your "
        u"choice, so there is no automatic fix. Double-click opens the war goal in Scripts → War goals."_s,
        std::nullopt));
    items.clear();
    for (const cbcheck::F &f : rep.order)
        items.push_back(item(f.text, {}, std::nullopt, cb_data(f.data)));
    fix.reset();
    if (!rep.order.empty())
        fix = Fix{u"Put peace_order right (%1)"_s.arg(rep.order.size()), u"cb_order"_s, Val()};
    cats.push_back(cat(
        u"cb_order"_s, u"War goals: peace_order"_s, SEV_WARN, items, fix,
        u"peace_order at the top of common/cb_types.txt is the order in which the war goals of a peace are carried out; "
        u"the game lists every one of its war goals there. The fix adds the missing ones at the end and takes out "
        u"names no war goal has (annex stays: the game lists it too)."_s,
        std::nullopt, u"Put this one right"_s));
    items.clear();
    for (const cbcheck::F &f : rep.texts) {
        Val d = cb_data(f.data);
        std::vector<Val> l = d.is_none() ? std::vector<Val>() : d.list();
        l.emplace_back(f.goal);
        items.push_back(item(f.text, {}, std::nullopt, Val(l)));
    }
    fix.reset();
    const long long nt = static_cast<long long>(rep.texts.size());
    if (nt)
        fix = Fix{nt > 1 ? u"Give all %1 a placeholder text"_s.arg(nt) : u"Give it a placeholder text"_s, u"names"_s, Val()};
    cats.push_back(cat(
        u"cb_texts"_s, u"War goals: texts missing"_s, SEV_WARN, items, fix,
        u"A war goal shows four texts — its name, _desc, _setup, _short — and the war it starts takes its name from "
        u"NORMAL_<war_name> / AGRESSION_<war_name>. Where one is missing the game shows the bare key. The placeholder is "
        u"the key written out (for a war name: the game's plain “… War”); write the real text in Scripts → War goals. "
        u"Texts the game itself lacks aren't listed."_s,
        std::nullopt, u"Use the placeholder"_s));
    return cats;
}


template <typename Map>
static auto sorted_items(const Map &m)
{
    std::vector<std::pair<typename std::decay_t<decltype(m.begin()->first)>, typename std::decay_t<decltype(m.begin()->second)>>> v(
        m.begin(), m.end());
    std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    return v;
}
static QString plural(long long n) { return n == 1 ? QString() : u"s"_s; }
static QString date_text(const history::Date &d)
{
    return u"%1.%2.%3"_s.arg(std::get<0>(d)).arg(std::get<1>(d)).arg(std::get<2>(d));
}

// ------------------------------------------------------------------ what crashes / hangs the game (C++ only)
// Measured 2026-11-08 with test mods over the plain game, each loaded to the main menu (ModEditor Cpp\reference\crash
// tests 2026-11-08): these four crash or hang it; unknown input / efficiency goods, an unknown template, goods costs,
// pop needs and a price of 0 do NOT (loaded). The production files are checked as Apply would write them.
static bool mod_isfile(const moddata::Mod &mod, const QStringList &parts);   // (below, by run_checks)

static void crash_checks(moddata::Mod &mod, document::Document &doc, std::vector<Category> &cats)
{
    std::vector<Item> items;
    if (const store::StoreP *pp_ = doc.maps.get(u"production"_s))
        if (auto *prod = dynamic_cast<production::ProdEdits *>(pp_->get())) {
            try {
                for (const QString &b : prod->buildings()) {
                    const production::BuildingDesc bd = prod->bld_info(b);
                    if (bd.production_type && !bd.production_type->isEmpty() && !prod->exists(*bd.production_type))
                        items.push_back(item(u"Building %1 makes production type “%2” — there is no such production type"_s
                                                 .arg(b, *bd.production_type),
                                             {}, std::nullopt, tup({u"crashbld"_s, b})));
                }
                for (const QString &k : prod->keys()) {
                    const production::Desc d = prod->info(k);
                    if (d.output && !d.output->isEmpty() && !mod.goods.contains(*d.output))
                        items.push_back(item(u"Production type %1 makes “%2” — goods.txt has no such good"_s.arg(k, *d.output),
                                             {}, std::nullopt, tup({u"crashprod"_s, k})));
                }
            } catch (const std::exception &) {
            }
        }
    cats.push_back(cat(u"crash_production"_s, u"Buildings / production types naming what doesn't exist (crash)"_s, SEV_ERROR,
                       items, std::nullopt,
                       u"Measured in the game: a building in common/buildings.txt whose production_type isn't in "
                       u"production_types.txt, or a production type whose output_goods isn't in goods.txt, crashes it while "
                       u"it loads (\"Loading Databases II\"). Economy → Production chains: give the building / production "
                       u"type one that exists (Edit as text), or bring the deleted one back (Ctrl+Z)."_s,
                       u"production"_s));
    items.clear();
    for (const auto &[tag, c] : mod.countries)
        if (!c.is_new && !c.file.isEmpty()) {
            QString rel = c.file;
            rel.replace(u'\\', u'/');
            if (!mod_isfile(mod, {u"common"_s, rel}))
                items.push_back(item(u"%1 = \"%2\" — no such file in common/ (mod or base game)"_s.arg(tag, c.file), {},
                                     std::nullopt, tup({u"crashcountry"_s, tag})));
        }
    cats.push_back(cat(u"crash_countries"_s, u"countries.txt names nation files that are missing (game hangs)"_s, SEV_ERROR,
                       items, std::nullopt,
                       u"Measured in the game: a line of common/countries.txt whose file doesn't exist makes it hang while "
                       u"it loads (it never reaches the main menu). Put the nation's file back in common/countries/, or "
                       u"take the line out of countries.txt."_s,
                       u"nations"_s));

    // --- (C++ only, 2026-11-09, the user's rule: a critical error) every nation needs its BASE flag,
    // gfx/flags/TAG.tga (the one without a government suffix), in the mod or the base game. A flag made in the flag
    // studio and not applied yet counts; one deleted and not applied yet doesn't.
    items.clear();
    const nationextras::FlagEdits *fe =
        doc.maps.contains(u"flags"_s) ? &doc.map<nationextras::FlagEdits>(u"flags"_s) : nullptr;
    for (const auto &[tag, c] : mod.countries) {
        const QString name = tag + u".tga"_s;
        bool have;
        if (fe && fe->fresh.contains(name))
            have = true;
        else if (fe && fe->gone.contains(name))
            have = false;
        else
            have = mod_isfile(mod, {u"gfx"_s, u"flags"_s, name});
        if (!have)
            items.push_back(item(u"%1 has no base flag: gfx/flags/%2 is in neither the mod nor the base game"_s.arg(tag, name),
                                 {}, std::nullopt, tup({u"nation_flag"_s, tag})));
    }
    cats.push_back(cat(u"flags_base"_s, u"Nations without their base flag"_s, SEV_ERROR, items, std::nullopt,
                       u"Every nation of common/countries.txt needs gfx/flags/TAG.tga — the flag without a government "
                       u"type (TAG_communist.tga and the others are only used under that government). Make it in Nations "
                       u"→ the nation → Flag studio, or copy a .tga of 93 × 64 into gfx/flags."_s,
                       u"nations"_s));
}

// ---------------------------------------------------------------------------------------------- file lookups
// (C++ only, 2026-11-10, the performance round) fs::isfile(mod.p(parts)) for many files of a few folders — a nation
// file and a flag per nation, 1,000+ disk lookups after every edit. The folders are listed instead, and a listing is
// kept until the folder's own time changes (Windows changes it when an entry is added, removed or renamed), looked at
// once per run of the checks. Only plain names go this way (ASCII, no trailing dot / space, no '.' / '..'): those
// Windows compares like toLower; anything else asks the disk as before.
namespace {
struct Listing {
    qint64 stamp = -2;
    long long seen_run = -1;
    QHash<QString, bool> names;                  // lower-case name -> a file (false: a folder)
};
std::map<QString, Listing> g_listings;
long long g_run = 0;

bool plain_rel(const QStringList &parts)
{
    if (parts.isEmpty())
        return false;
    for (const QString &whole : parts)
        for (const QString &seg : whole.split(u'/')) {
            if (seg.isEmpty() || seg == u"." || seg == u".." || seg.endsWith(u'.') || seg.endsWith(u' '))
                return false;
            for (QChar c : seg)
                if (c.unicode() < 0x20 || c.unicode() > 0x7e || QStringView(u"\\:*?\"<>|").contains(c))
                    return false;
        }
    return true;
}

int listed(const QString &full)                  // 0 nothing there, 1 a file, 2 a folder
{
    const QFileInfo fi(full);                    // (string work only: nothing is asked of the disk here)
    const QString dir = fi.path(), name = fi.fileName().toLower();
    Listing &l = g_listings[dir];
    if (l.seen_run != g_run) {
        l.seen_run = g_run;
        const QFileInfo di(dir);
        const qint64 st = di.isDir() ? di.lastModified().toMSecsSinceEpoch() : -1;
        if (st != l.stamp) {
            l.stamp = st;
            l.names.clear();
            if (st != -1) {
                QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
                while (it.hasNext()) {
                    it.next();
                    const QFileInfo e = it.fileInfo();
                    l.names.insert(e.fileName().toLower(), e.isFile());
                }
            }
        }
    }
    const auto f = l.names.constFind(name);
    return f == l.names.constEnd() ? 0 : (*f ? 1 : 2);
}
}  // namespace

// fs::isfile(mod.p(parts)), from the folder listings
static bool mod_isfile(const moddata::Mod &mod, const QStringList &parts)
{
    if (!plain_rel(parts))
        return fs::isfile(mod.p(parts));
    const int k = listed(pypath::join(mod.root, parts));
    if (k)                                       // (there: Mod.p takes it, a file or not)
        return k == 1;
    if (mod.base && !mod.replaced(parts))
        return listed(pypath::join(*mod.base, parts)) == 1;
    return false;
}

std::vector<Category> run_checks(moddata::Mod &mod, const mapview::MapData &md, document::Document &doc,
                                 GeometryCache &geo_cache, const std::vector<std::uint8_t> &valid_sea,
                                 adjacency::AdjGeometry *adj_geo, double gap)
{
    ++g_run;                                     // (the folder listings are looked at again, once each)
    // -> [Category]. valid_sea: per id — sea tiles that can hold a naval base. adj_geo: the map's AdjGeometry, for the
    // adjacencies.csv checks.
    std::vector<Category> cats = session_checks(mod, md, doc);
    auto &provs = mod.provinces;
    const long long n = md.n;
    grid::PySet<long long> on_map;                 // (a set: Python walks it for the tiny provinces)
    for (long long p = 1; p < n; ++p)
        if (md.area[p] > 0)
            on_map.add(p);
    std::vector<long long> land;
    for (long long p : on_map.items())
        if (provs.contains(p) && !provs.get(p)->sea)
            land.push_back(p);
    std::sort(land.begin(), land.end());
    const QSet<long long> land_set(land.begin(), land.end());
    std::vector<Rgb> rgbs;
    for (const auto &kv : provs)
        rgbs.push_back(kv.second.rgb);
    const auto [pieces, unknown] = geo_cache.get(md, mod.p({u"map"_s, u"provinces.bmp"_s}), rgbs);
    auto name_of = [&](long long p) { return provs.get(p)->name; };
    auto centre_if = [&](long long p) {
        return (0 < p && p < n && md.area[p]) ? std::optional<XY>(_centre(md, p)) : std::nullopt;
    };

    // --- colours / ids
    std::vector<Item> items;
    {
        std::vector<std::pair<Rgb, std::pair<long long, long long>>> u(unknown->begin(), unknown->end());
        std::stable_sort(u.begin(), u.end(), [](const auto &a, const auto &b) { return -a.second.first < -b.second.first; });
        for (const auto &[rgb, cf] : u) {
            if (rgb == Rgb{0, 0, 0})                 // black = no province, on purpose (not a problem)
                continue;
            const auto [r, g, b] = rgb;
            items.push_back(item(u"colour %1;%2;%3 — %4 pixel%5 not in definition.csv"_s.arg(r).arg(g).arg(b).arg(cf.first)
                                     .arg(plural(cf.first)),
                                 {}, _pt(md, cf.second), tup({r, g, b})));
        }
    }
    std::optional<Fix> fix;
    if (!items.empty())
        fix = Fix{u"Give all %1 to the provinces around them"_s.arg(_n(ll(items.size()), u"colour's pixels"_s, u"colours' pixels"_s)),
                  u"unknown"_s, Val()};
    cats.push_back(cat(u"unknown"_s, u"Colours on the map that no province uses"_s, SEV_WARN, items, fix,
                       u"Black (0;0;0) is the no-province colour and is not listed. Other colours that no province uses "
                       u"are usually painting leftovers (vanilla has some too, the game copes). The fix gives each area "
                       u"of such a colour to the province most of its neighbours belong to; or repaint it in Drawing, or "
                       u"add the colour to definition.csv."_s,
                       u"map"_s, u"Give this colour's pixels to the provinces around them"_s));
    QSet<long long> fresh;
    if (const store::StoreP *dp = doc.maps.get(u"definitions"_s))
        if (auto *defs = dynamic_cast<newprov::ProvinceDefs *>(dp->get()))
            for (const auto &kv : defs->added)
                fresh.insert(kv.first);
    std::vector<long long> lost;
    for (const auto &kv : provs)
        if (!on_map.contains(kv.first))
            lost.push_back(kv.first);
    std::sort(lost.begin(), lost.end());
    items.clear();
    long long old = 0;
    for (long long p : lost)
        if (!provs.get(p)->sea) {
            items.push_back(item(u"#%1 %2 is in definition.csv but has no pixel on the map%3"_s.arg(
                                     QString::number(p), name_of(p), fresh.contains(p) ? u" (new: paint it in Drawing)"_s : QString()),
                                 {p}, std::nullopt, tup({u"offmap"_s, p, fresh.contains(p)})));
            old += !fresh.contains(p);
        }
    fix.reset();
    if (old)
        fix = Fix{u"Delete the %1 that %2 no land"_s.arg(_n(old, u"province"_s, u"provinces"_s), old == 1 ? u"has"_s : u"have"_s),
                  u"delete_offmap"_s, Val()};
    cats.push_back(cat(u"offmap"_s, u"Land provinces without a pixel — the game crashes while it loads the map"_s, SEV_ERROR,
                       items, fix,
                       u"Tried in the game with Ferrum Mare: a land province of definition.csv without a single pixel on "
                       u"the map crashes it at “Creating Rivers…” (“Creating Texts…” when it has nothing else either) — "
                       u"with or without history, pops and a state. Either paint it back onto the map (Drawing lists them "
                       u"under “Not on the map”), or delete it: the fix is the same as Drawing → Delete province "
                       u"(history, pops, state and places go; Apply closes the numbers up)."_s,
                       u"map"_s, u"Delete this province"_s));
    items.clear();
    old = 0;
    for (long long p : lost)
        if (provs.get(p)->sea) {
            items.push_back(item(u"sea tile #%1 %2 is in definition.csv but has no pixel on the map"_s.arg(QString::number(p), name_of(p)),
                                 {p}, std::nullopt, tup({u"offmap"_s, p, fresh.contains(p)})));
            old += !fresh.contains(p);
        }
    fix.reset();
    if (old)
        fix = Fix{u"Delete the %1 without a pixel"_s.arg(_n(old, u"sea tile"_s, u"sea tiles"_s)), u"delete_offmap"_s, Val()};
    cats.push_back(cat(u"offmap_sea"_s, u"Sea tiles without a pixel"_s, SEV_WARN, items, fix,
                       u"Leftovers. The game copes with a sea tile that has no pixel as long as it is listed in "
                       u"sea_starts — mods that run have them (Lake Mweru in Femgirl_DoD, 19 in Ferrum Mare 1.0.17). "
                       u"Taken OUT of sea_starts it counts as land and crashes the game. The fix deletes them for good."_s,
                       u"map"_s, u"Delete this sea tile"_s));
    std::vector<long long> slots_;
    for (const auto &kv : mod.free_slots)
        slots_.push_back(kv.first);
    std::sort(slots_.begin(), slots_.end());
    items.clear();
    for (long long p : slots_)
        items.push_back(item(u"number %1 is in definition.csv without a province (“UNUSED”) — the game crashes on it"_s.arg(p)));
    fix.reset();
    if (!slots_.empty())
        fix = Fix{slots_.size() == 1 ? u"Close the numbers up: the highest province takes the unused number"_s
                                     : u"Close the numbers up: the highest provinces take the %1 unused numbers"_s.arg(slots_.size()),
                  u"renumber"_s, Val()};
    cats.push_back(cat(u"unused_rows"_s, u"definition.csv: numbers without a province"_s, SEV_ERROR, items, fix,
                       u"A province deleted in an earlier version of the editor left its number behind as an empty row. "
                       u"To the game that is a land province without a pixel, and it crashes on it (tried). So the "
                       u"highest provinces take those numbers — in every file that names them (each backed up) — and the "
                       u"mod is read again. Apply first if edits are pending."_s));
    items.clear();
    {
        const QString dpath = mod.p({u"map"_s, u"definition.csv"_s});
        if (fs::isfile(dpath)) {
            const QStringList all = py::splitlines(script::read_text(dpath));
            std::vector<QStringList> rows;
            for (qsizetype i = 1; i < all.size(); ++i)
                rows.push_back(all[i].split(u';'));
            auto id_ok = [](const QStringList &r) { return !r.isEmpty() && py::isdigit(py::strip(r[0])); };
            OrderedMap<QString, long long> ids;
            for (const auto &r : rows)
                if (id_ok(r))
                    ids[py::strip(r[0])] += 1;
            for (const auto &[k, v] : ids)
                if (v > 1)
                    items.push_back(item(u"id %1 is listed %2 times"_s.arg(k).arg(v), {py::to_int(k)}));
            auto col = [](const QStringList &r) {
                return QStringList{py::strip(r[1]), py::strip(r[2]), py::strip(r[3])};
            };
            OrderedMap<QString, std::pair<QStringList, long long>> cols;   // (keyed by the joined text)
            for (const auto &r : rows)
                if (id_ok(r) && r.size() >= 4) {
                    const QStringList c = col(r);
                    auto &e = cols[c.join(QChar(0x1f))];
                    e.first = c;
                    e.second += 1;
                }
            for (const auto &[_k, cv] : cols)
                if (cv.second > 1) {
                    std::vector<long long> who;
                    for (const auto &r : rows)
                        if (id_ok(r) && r.size() >= 4 && col(r) == cv.first)
                            who.push_back(py::to_int(r[0]));
                    QStringList ws;
                    for (long long p : who)
                        ws << u"#%1"_s.arg(p);
                    items.push_back(item(u"colour %1 is used by %2"_s.arg(cv.first.join(u';'), ws.join(u", "_s)), who));
                }
        }
    }
    cats.push_back(cat(u"defdup"_s, u"Duplicate ids / colours in definition.csv"_s, SEV_ERROR, items));

    // --- default.map
    const store::StoreP *dmp = doc.maps.get(u"default_map"_s);
    mapfiles::DefaultMapFile *dm = dmp ? &doc.map<mapfiles::DefaultMapFile>(u"default_map"_s) : nullptr;
    items.clear();
    fix.reset();
    if (dm && dm->max_provinces) {
        long long top = 0;
        for (const auto &kv : provs)
            top = std::max(top, kv.first);
        if (*dm->max_provinces != top + 1) {
            items.push_back(item(u"max_provinces = %1, but the highest province id is %2 → it must be %3 (highest id + 1)"_s
                                     .arg(*dm->max_provinces).arg(top).arg(top + 1)));
            fix = Fix{u"Set max_provinces = %1"_s.arg(top + 1), u"max_provinces"_s, Val(top + 1)};
        }
    }
    cats.push_back(cat(u"maxprov"_s, u"default.map: max_provinces"_s, SEV_ERROR, items, fix,
                       u"max_provinces is always the highest id + 1 — that difference of one is correct."_s));
    items.clear();
    fix.reset();
    if (dm) {
        OrderedMap<long long, long long> cnt;
        for (long long p : dm->sea)
            cnt[p] += 1;
        std::vector<long long> dup, undef, offm;
        for (const auto &[p, c] : cnt)
            if (c > 1)
                dup.push_back(p);
        std::sort(dup.begin(), dup.end());
        for (long long p : dup)
            items.push_back(item(u"sea tile #%1 is listed %2 times in sea_starts"_s.arg(p).arg(*cnt.get(p)), {p}, std::nullopt,
                                 tup({u"dup"_s, p})));
        std::set<long long> seas(dm->sea.begin(), dm->sea.end());
        for (long long p : seas)
            if (!provs.contains(p))
                undef.push_back(p);
        for (long long p : undef)
            items.push_back(item(u"sea_starts lists #%1, which isn't in definition.csv"_s.arg(p), {p}, std::nullopt,
                                 tup({u"undef"_s, p})));
        for (long long p : seas)
            if (provs.contains(p) && !on_map.contains(p))
                offm.push_back(p);
        for (long long p : offm)
            items.push_back(item(u"sea tile #%1 is in sea_starts but not on the map"_s.arg(p), {p}, std::nullopt,
                                 tup({u"offmap"_s, p})));
        if (!dup.empty() || !undef.empty())
            fix = Fix{u"Remove the %1 from sea_starts"_s.arg(_n(ll(dup.size() + undef.size()), u"wrong entry"_s, u"wrong entries"_s)),
                      u"sea_starts"_s, Val()};
        if (dm->definitions && !dm->definitions->isEmpty()) {
            QString d = *dm->definitions;
            d.replace(u'\\', u'/');
            const QString me = u"/mod/%1/"_s.arg(pypath::basename(mod.root));
            if (d.contains(u"/mod/"_s) && !d.contains(me))
                items.push_back(item(u"definitions = \"%1\" points to another mod folder than this one (%2)"_s.arg(
                    *dm->definitions, pypath::basename(mod.root))));
        }
    }
    cats.push_back(cat(u"seastarts"_s, u"default.map: sea_starts / definitions"_s, SEV_WARN, items, fix, QString(), u"map"_s,
                       u"Fix this entry"_s));

    // --- (C++ only, 2026-11-08) definitions = "definition.csv" while the mod has its own, different file
    items.clear();
    fix.reset();
    if (dm) {
        const auto want = dm->definitions_wanted();
        if (want && dm->new_definitions != want) {
            items.push_back(item(u"default.map says definitions = \"%1\" — the game then builds its province picture "
                                 u"from the BASE GAME's %1, not this mod's"_s.arg(*dm->definitions)));
            fix = Fix{u"Write definitions = \"%1\""_s.arg(*want), u"definitions_path"_s, Val(*want)};
        }
    }
    cats.push_back(cat(u"defpath"_s, u"default.map: definitions names no mod folder"_s, SEV_ERROR, items, fix,
                       u"Measured in the game (2026-11-08): with a bare file name the game's map cache "
                       u"(cache/provincecache.bin) is built from the base game's definition.csv even when the mod has "
                       u"its own. A renumbered or recoloured province then has no pixel in it, and the map crashes at "
                       u"random while loading. With \"../mod/<folder>/map/definition.csv\" (what every working mod "
                       u"writes) the same mod loaded every time."_s,
                       u"map"_s));

    // --- map shape (provinces marked as having islands keep their separate parts)
    QSet<long long> marked;
    if (doc.maps.contains(u"islands"_s))
        marked = doc.map<history::IslandFlags>(u"islands"_s).marked;
    std::map<long long, std::vector<std::pair<long long, long long>>> by;
    for (size_t k = 0; k < pieces->pid.size(); ++k)
        if (pieces->pid[k])
            by[pieces->pid[k]].push_back({pieces->size[k], pieces->first[k]});
    std::vector<Item> stray, parts;
    for (auto &[p, lst] : by) {
        if (lst.size() < 2)
            continue;
        std::sort(lst.begin(), lst.end(), std::greater<>());
        if (marked.contains(p))
            continue;
        const long long main = lst[0].first;
        for (size_t k = 1; k < lst.size(); ++k) {
            const auto [s, f] = lst[k];
            const bool sea = provs.contains(p) && provs.get(p)->sea;
            QString nm = provs.contains(p) ? py::strip(provs.get(p)->name) : u"?"_s;
            if (nm.isEmpty())
                nm = u"(unnamed)"_s;
            Item it = item(u"#%1 %2: %3 stray pixel%4 away from its main area (%5 px) — %6"_s.arg(
                               QString::number(p), nm, QString::number(s), plural(s), QString::number(main),
                               sea ? u"sea tile"_s : u"province"_s),
                           {p}, _pt(md, f), sea ? Val(u"sea"_s) : Val());
            (s < STRAY_MAX ? stray : parts).push_back(it);
        }
    }
    fix.reset();
    if (!stray.empty())
        fix = Fix{u"Give all %1 to the province around them"_s.arg(_n(ll(stray.size()), u"stray bit"_s, u"stray bits"_s)),
                  u"stray"_s, Val()};
    cats.push_back(cat(u"stray"_s, u"Stray pixels (small separate bits of a province)"_s, SEV_WARN, stray, fix,
                       u"Usually painting leftovers (vanilla has hundreds too, the game copes). The fix gives each bit to "
                       u"the province most of its neighbouring pixels belong to. A real small island? Tick \"Has "
                       u"islands\" for its province first (double-click → Drawing) — marked provinces are left alone."_s,
                       u"map"_s, u"Give this bit to the province around it"_s));
    QSet<long long> np_;                         // sea tiles can't carry the island flag
    for (const Item &it : parts)
        if (it.data != Val(u"sea"_s))
            np_.insert(it.pids[0]);
    fix.reset();
    if (!np_.isEmpty())
        fix = Fix{u"Mark all %1 as having islands"_s.arg(_n(np_.size(), u"province"_s, u"provinces"_s)), u"islands_mark"_s, Val()};
    cats.push_back(cat(u"parts"_s, u"Provinces in several larger parts"_s, SEV_INFO, parts, fix,
                       u"Often intended (e.g. a province with an island) — check they are. Intended ones: mark them as "
                       u"having islands (here, or \"Has islands\" in Drawing / Provinces) and they aren't listed any more. "
                       u"Unintended ones — and sea tiles, which can't be marked — double-click and paint them over."_s,
                       u"map"_s, u"Mark this province as having islands"_s));
    items.clear();
    {
        std::vector<long long> ms(marked.begin(), marked.end());
        std::sort(ms.begin(), ms.end());
        for (long long p : ms) {
            if (!provs.contains(p))
                continue;
            std::vector<long long> lst;
            if (by.count(p))
                for (const auto &sf : by.at(p))
                    lst.push_back(sf.first);
            std::sort(lst.begin(), lst.end(), std::greater<>());
            QString nm = py::strip(provs.get(p)->name);
            if (nm.isEmpty())
                nm = u"(unnamed)"_s;
            QString text;
            if (lst.size() > 1) {
                QStringList sz;
                for (size_t k = 0; k < lst.size() && k < 5; ++k)
                    sz << fmt_n(lst[k]);
                text = u"#%1 %2: %3 parts (%4 px)"_s.arg(QString::number(p), nm, QString::number(lst.size()),
                                                          sz.join(u" + "_s) + (lst.size() > 5 ? u" …"_s : QString()));
            } else {
                text = u"#%1 %2: in one piece — the island flag isn't needed"_s.arg(QString::number(p), nm);
            }
            items.push_back(item(text, {p}, centre_if(p)));
        }
    }
    fix.reset();
    if (!marked.isEmpty())
        fix = Fix{marked.size() > 1 ? u"Remove all %1 island flags — check these provinces again"_s.arg(marked.size())
                                    : u"Remove the island flag — check this province again"_s,
                  u"islands_clear"_s, Val()};
    cats.push_back(cat(u"islands"_s, u"Provinces marked as having islands"_s, SEV_INFO, items, fix,
                       u"Marked with \"Has islands\" in Provinces: their separate parts are meant to be, so they're not "
                       u"listed as stray pixels / several parts. Each carries set_province_flag = island in its history "
                       u"file. Remove them all to check every province again (e.g. after bigger map changes) — one undo "
                       u"step, written on Apply."_s,
                       u"map"_s, u"Remove this island flag"_s));
    items.clear();
    for (const auto &[idx, to] : geo_cache.zero_small) {
        const long long f = idx[idx.size() / 2];
        items.push_back(item(u"%1 of no province (black) inside #%2 %3"_s.arg(
                                 _n(ll(idx.size()), u"pixel"_s, u"pixels"_s), QString::number(to),
                                 provs.contains(to) ? py::strip(provs.get(to)->name) : u"?"_s),
                             {}, _pt(md, f), tup({u"zero"_s, ll(to)})));
    }
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Give all %1 to the province around them"_s.arg(_n(ll(items.size()), u"area"_s, u"areas"_s)), u"zero_small"_s, Val()};
    cats.push_back(cat(u"zero_small"_s, u"Small areas of no province (black)"_s, SEV_INFO, items, fix,
                       u"Black is the no-province colour on purpose — but specks smaller than %1 pixels inside land or "
                       u"sea are usually painting leftovers."_s.arg(STRAY_MAX),
                       u"map"_s, u"Give this area to the province around it"_s));
    items.clear();
    {
        std::vector<long long> om = on_map.items();
        std::stable_sort(om.begin(), om.end(), [&](long long a, long long b) { return md.area[a] < md.area[b]; });
        for (long long p : om)
            if (provs.contains(p) && md.area[p] < TINY) {
                QString nm = py::strip(provs.get(p)->name);
                if (nm.isEmpty())
                    nm = u"(unnamed)"_s;
                items.push_back(item(u"#%1 %2: only %3 pixel%4%5"_s.arg(QString::number(p), nm, QString::number(md.area[p]),
                                                                         plural(md.area[p]),
                                                                         provs.get(p)->sea ? u" (sea)"_s : QString()),
                                     {p}, _centre(md, p)));
            }
    }
    cats.push_back(cat(u"tiny"_s, u"Tiny provinces"_s, SEV_INFO, items));
    // (C++ only, 2026-11-18, the user's rule from experience) a LAND province larger than 20,000 pixels can crash the
    // game; land provinces of the base game and the mods looked at are all smaller. Sea zones are not listed: the base
    // game's reach ~70,000 px and load.
    items.clear();
    {
        std::vector<long long> om = on_map.items();
        std::stable_sort(om.begin(), om.end(), [&](long long a, long long b) { return md.area[a] > md.area[b]; });
        for (long long p : om)
            if (const auto *pr = provs.get(p); pr && !pr->sea && md.area[p] > HUGE_LAND) {
                QString nm = py::strip(pr->name);
                if (nm.isEmpty())
                    nm = u"(unnamed)"_s;
                items.push_back(item(u"#%1 %2: %3 pixels"_s.arg(QString::number(p), nm, QString::number(md.area[p])),
                                     {p}, _centre(md, p)));
            }
    }
    cats.push_back(cat(u"huge_land"_s, u"Land provinces larger than 20,000 pixels — the game can crash"_s, SEV_WARN, items,
                       std::nullopt,
                       u"Land provinces larger than 20,000 pixels can crash the game (seen by players). Every land "
                       u"province of the base game is smaller. Split it in Drawing (paint part of it as a new province) "
                       u"or give part of it to its neighbours. Sea zones are not listed: the base game's reach about "
                       u"70,000 pixels and load."_s));

    // --- names (localisation): the game shows the bare key where one is missing
    localisation::LocEdits *loc = nullptr;
    if (const store::StoreP *lp = doc.maps.get(u"loc"_s))
        loc = dynamic_cast<localisation::LocEdits *>(lp->get());
    auto named = [&](const QString &k) { return mod.loc.contains(k) || (loc && loc->fresh.contains(k)); };
    OrderedMap<long long, QString> dnames;
    {
        const QString dpath = mod.p({u"map"_s, u"definition.csv"_s});
        if (fs::isfile(dpath))
            for (const QString &r : py::splitlines(script::read_text(dpath))) {
                const QStringList f = r.split(u';');
                if (f.size() > 4 && py::isdigit(py::strip(f[0])))
                    dnames.insert(py::to_int(f[0]), py::strip(f[4]));
            }
    }
    items.clear();
    {
        std::vector<long long> om = on_map.items();
        std::sort(om.begin(), om.end());
        for (long long p : om)
            if (provs.contains(p) && !named(u"PROV%1"_s.arg(p))) {
                const QString dn = dnames.value(p);
                items.push_back(item(u"#%1 has no name (PROV%1)%2"_s.arg(QString::number(p),
                                                                         dn.isEmpty() ? QString() : u" — definition.csv calls it “%1”"_s.arg(dn)),
                                     {p}, _centre(md, p), tup({u"name"_s, u"PROV%1"_s.arg(p), dn, u"prov"_s})));
            }
    }
    for (const auto &[rid, r] : sorted_items(mod.regions))
        if (!r.provinces.empty() && !named(rid)) {
            const moddata::Province *cap = provs.get(r.provinces[0]);
            const QString cn = cap ? py::strip(cap->name) : QString();
            std::vector<long long> ps;
            if (land_set.contains(r.provinces[0]))
                ps.push_back(r.provinces[0]);
            items.push_back(item(u"State %1 has no name%2"_s.arg(rid, cn.isEmpty() ? QString() : u" — its capital is %1"_s.arg(cn)),
                                 ps, std::nullopt, tup({u"name"_s, rid, cn, u"state"_s})));
        }
    {
        QStringList ck, rk;
        for (const auto &kv : mod.cultures)
            ck << kv.first;
        for (const auto &kv : mod.religions)
            rk << kv.first;
        std::sort(ck.begin(), ck.end());
        std::sort(rk.begin(), rk.end());
        for (const auto &[keys, what, sec] : {std::tuple{&ck, u"Culture"_s, u"culture"_s}, {&rk, u"Religion"_s, u"religion"_s}})
            for (const QString &k : *keys)
                if (!named(k))
                    items.push_back(item(u"%1 %2 has no name"_s.arg(what, k), {}, std::nullopt,
                                         tup({u"name"_s, k, py::title(QString(k).replace(u'_', u' ')), sec})));
    }
    long long nfix = 0;
    for (const Item &it : items)
        nfix += it.data[2].truthy();
    fix.reset();
    if (nfix)
        fix = Fix{nfix > 1 ? u"Give all %1 their suggested names"_s.arg(nfix) : u"Give it the suggested name"_s, u"names"_s, Val()};
    cats.push_back(cat(u"names"_s, u"Names missing in the localisation"_s, SEV_WARN, items, fix,
                       u"The game shows the bare key (e.g. PROV3451 or STA_12) where a name is missing. Suggested: the "
                       u"name in definition.csv for provinces, the capital's name for states, the key written out for "
                       u"cultures / religions. New names go to the editor's localisation file (%1) on Apply; rename them "
                       u"any time in Provinces."_s.arg(localisation::own_file(mod)),
                       u"provinces"_s, u"Use the suggested name"_s));

    // --- nations at the start
    countryhist::NationEdits *nations = nullptr;
    if (const store::StoreP *np = doc.maps.get(u"nations"_s))
        nations = dynamic_cast<countryhist::NationEdits *>(np->get());
    if (nations && mod.nation_data && doc.date) {
        countryhist::NationData &data = *mod.nation_data;
        OrderedMap<QString, std::vector<long long>> owned;
        for (long long p : land) {
            const OptStr o = doc.prov_state(p).owner;
            if (o && !o->isEmpty())
                owned[*o].push_back(p);
        }
        items.clear();
        QStringList tags;
        for (const auto &kv : mod.countries)
            if (nations->hist.files.contains(kv.first) && kv.first != u"REB" && kv.first != u"UNC")
                tags << kv.first;
        std::sort(tags.begin(), tags.end());
        for (const QString &tag : tags) {
            const countryhist::NationState st = nations->state(tag);
            const QString name = u"%1 %2"_s.arg(tag, mod.countries.get(tag)->name);
            const std::vector<long long> mine = owned.value(tag);
            const OptStr capv = st.at(u"capital"_s);
            const std::optional<long long> cap =
                capv && !capv->isEmpty() && py::isdigit(*capv) ? std::optional(py::to_int(*capv)) : std::nullopt;
            if (!mine.empty() && (!cap || std::find(mine.begin(), mine.end(), *cap) == mine.end())) {
                long long best = mine[0];
                for (long long q : mine)                 // max(mine, key=(total, -q)): the first largest
                    if (std::make_pair(doc.total(q), -q) > std::make_pair(doc.total(best), -best))
                        best = q;
                items.push_back(item(u"%1: capital %2 is not one of its provinces — e.g. #%3 %4"_s.arg(
                                         name, cap && *cap ? u"#%1"_s.arg(*cap) : u"missing"_s, QString::number(best), name_of(best)),
                                     {mine[0]}, _centre(md, best),
                                     tup({u"nation"_s, tag, u"capital"_s, QString::number(best)})));
            }
            QStringList parties;
            for (const auto &pt : data.parties(tag))
                parties << pt.name;
            const OptStr rp = st.at(u"ruling_party"_s);
            if (!parties.isEmpty() && rp && !rp->isEmpty() && !parties.contains(*rp))
                items.push_back(item(u"%1: ruling party %2 isn't one of its parties"_s.arg(name, *rp), {}, std::nullopt,
                                     tup({u"nation"_s, tag, u"ruling_party"_s, parties[0]})));
            for (const QString &key : {u"primary_culture"_s, u"religion"_s, u"government"_s}) {
                const OptStr v = st.at(key);
                const bool in_pool = key == u"primary_culture" ? (v && mod.cultures.contains(*v))
                                     : key == u"religion"      ? (v && mod.religions.contains(*v))
                                                               : (v && data.governments.contains(*v));
                if (v && !v->isEmpty() && !in_pool)
                    items.push_back(item(u"%1: %2 %3 is not defined in the game files"_s.arg(name, QString(key).replace(u'_', u' '), *v),
                                         {}, std::nullopt, tup({u"nation"_s, tag, key, Val()})));
            }
            for (const QString &c : st.cultures)
                if (!mod.cultures.contains(c))
                    items.push_back(item(u"%1: accepted culture %2 is not defined"_s.arg(name, c), {}, std::nullopt,
                                         tup({u"nation"_s, tag, u"culture"_s, c})));
            if (!st.upper_house.isEmpty()) {
                double tot;
                std::vector<double> vals;
                bool ok = true;
                for (const auto &kv : st.upper_house) {
                    const auto f = py::to_float(kv.second);
                    if (!f) {
                        ok = false;
                        break;
                    }
                    vals.push_back(*f);
                }
                tot = ok ? py::sum(vals) : -1;
                if (std::fabs(tot - 100) > 0.5)
                    items.push_back(item(u"%1: upper house adds up to %2%, not 100%"_s.arg(name, py::fmt_g(tot)), {}, std::nullopt,
                                         tup({u"nation"_s, tag, u"upper_house"_s, Val()})));
            }
        }
        nfix = 0;
        for (const Item &it : items)
            nfix += !it.data[3].is_none() || it.data[2] == Val(u"upper_house"_s) || it.data[2] == Val(u"culture"_s);
        fix.reset();
        if (nfix)
            fix = Fix{u"Fix the %1 nation problem%2"_s.arg(nfix).arg(plural(nfix)), u"nations"_s, Val()};
        cats.push_back(cat(u"nations"_s, u"Nations at the start"_s, SEV_WARN, items, fix,
                           u"Capital: its most populous province · ruling party: its first party · accepted culture the "
                           u"game doesn't know: dropped · upper house: scaled to 100 %. Written into the scenario on Apply "
                           u"(for the start date: the file's undated part); double-click opens the nation."_s,
                           u"nations"_s, u"Fix this one"_s));
        // a nation owning nothing in the UNDATED province history but land at the start (from dated blocks) makes the
        // game crash while it loads (verified in game): it can't create a nation in its second history pass
        std::set<OptStr> alive;
        for (const auto &kv : doc.hist->files)
            alive.insert(doc.hist->state_at(kv.first, std::nullopt).owner);
        std::set<OptStr> pending;
        for (const auto &kv : doc.prov_want) {
            const OptStr o = doc.prov_state(kv.first).owner;
            if (!alive.count(o))
                pending.insert(o);
        }
        QSet<long long> queued;
        if (const store::StoreP *tp = doc.maps.get(u"owners_top"_s))
            if (auto *top = dynamic_cast<history::OwnersOnTop *>(tp->get()))
                queued = top->pids;
        items.clear();
        for (const auto &[tag, mine] : sorted_items(owned)) {
            if (alive.count(OptStr(tag)) || pending.count(OptStr(tag)) ||
                std::all_of(mine.begin(), mine.end(), [&](long long p) { return queued.contains(p); }))
                continue;
            const moddata::Country *c = mod.countries.get(tag);
            const QString nm = c ? c->name : tag;
            items.push_back(item(u"%1 %2 owns %3 province%4 at the start, all from dated history blocks (e.g. #%5 %6) and "
                                 u"none before — the game crashes while it loads"_s.arg(
                                     tag, nm, QString::number(mine.size()), plural(ll(mine.size())), QString::number(mine[0]),
                                     name_of(mine[0])),
                                 mine, _centre(md, mine[0]), tup({u"birth"_s, tag, ints(mine)})));
        }
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Write their owners at the top of the files (%1 nation%2)"_s.arg(items.size()).arg(plural(ll(items.size()))),
                      u"owners_top"_s, Val()};
        cats.push_back(cat(u"nation_birth"_s, u"Nations that only get land in a dated block (crash)"_s, SEV_ERROR, items, fix,
                           u"The game creates the nations that own land in the undated part of the province history; one "
                           u"that only gets land in a dated block (e.g. 1836.1.1 = { owner = … }) makes it crash while "
                           u"loading. The fix writes owner / controller / core at the top of those provinces' files on "
                           u"Apply (the editor does this by itself for provinces you give to such a nation)."_s,
                           u"nations"_s, u"Write its owners at the top"_s));
    }

    // a block dated on or before the start date: pointless, the undated part IS the start (user's rule) — light
    {
        auto [sb_p, sb_t] = history::start_blocks(doc, mod);
        history::StartBlocks *queued = nullptr;
        if (const store::StoreP *sp = doc.maps.get(u"start_blocks"_s))
            queued = dynamic_cast<history::StartBlocks *>(sp->get());
        std::vector<long long> ps;
        for (long long p : sb_p)
            if (!(queued && queued->pids.contains(p)))
                ps.push_back(p);
        QStringList ts;
        for (const QString &t : sb_t)
            if (!(queued && queued->tags.contains(t)))
                ts << t;
        if (!ps.empty() || !ts.isEmpty()) {
            const QString sd = date_text(*doc.hist->start());
            items.clear();
            for (long long p : ps)
                items.push_back(item(u"#%1 %2: its history file has a block dated %3 (or earlier) — the undated part already "
                                     u"is the start"_s.arg(QString::number(p), provs.contains(p) ? name_of(p) : QString(), sd),
                                     {p}, _centre(md, p), tup({u"startblock"_s, u"prov"_s, p})));
            for (const QString &t : ts) {
                const moddata::Country *c = mod.countries.get(t);
                items.push_back(item(u"%1 %2: its history file has a block dated %3 (or earlier)"_s.arg(t, c ? c->name : t, sd),
                                     {}, std::nullopt, tup({u"startblock"_s, u"nation"_s, t})));
            }
            fix = Fix{u"Fold them into the undated part (%1 file%2)"_s.arg(items.size()).arg(plural(ll(items.size()))),
                      u"start_blocks"_s, Val()};
            cats.push_back(cat(u"start_blocks"_s, u"Blocks dated on the start date (%1)"_s.arg(sd), SEV_WARN, items, fix,
                               u"The game starts on %1 (start_date in common/defines.lua): what a history file says "
                               u"without a date already holds then, so a block of that date only splits it. The fix merges "
                               u"the block into the undated part on Apply (a core it adds and removes again simply goes). "
                               u"Harmless for the game — tidiness."_s.arg(sd),
                               u"provinces"_s, u"Fold this one in"_s));
        }
    }

    // --- armies and navies at the start (history/units)
    oob::OOBEdits *oobp = nullptr;
    if (const store::StoreP *op = doc.maps.get(u"oob"_s))
        oobp = dynamic_cast<oob::OOBEdits *>(op->get());
    if (oobp) {
        oob::OOBEdits &ob = *oobp;
        items.clear();
        const auto &units = ob.units->types;
        const auto slots_all = ob.all_slots(doc);
        QStringList twu(ob.tags_with_units().begin(), ob.tags_with_units().end());
        std::sort(twu.begin(), twu.end());
        auto land_prov = [&](const std::optional<long long> &p) { return p && provs.contains(*p) && !provs.get(*p)->sea; };
        auto loc_txt = [](const std::optional<long long> &p) { return p ? QString::number(*p) : u"None"_s; };
        for (const QString &tag : twu) {
            if (!mod.countries.contains(tag))
                continue;
            const oob::OOB &st = ob.state(tag);
            const QString name = u"%1 %2"_s.arg(tag, mod.countries.get(tag)->name);
            const auto [path, ref] = ob.file_of(tag);
            if (ref && !ref->isEmpty() && !ob.exists(tag) && st.armies.empty() && st.navies.empty())
                items.push_back(item(u"%1: its history names history/units/%2, which doesn't exist"_s.arg(name, *ref), {},
                                     std::nullopt, tup({u"oob_file"_s, tag, Val()})));
            const oob::Slots sl = slots_all.value(tag);
            for (size_t ai = 0; ai < st.armies.size(); ++ai) {
                const oob::Army &a = st.armies[ai];
                if (!land_prov(a.location))
                    items.push_back(item(u"%1: %2 starts at %3 — not a land province"_s.arg(
                                             name, a.name, a.location ? u"#%1"_s.arg(*a.location) : u"no location"_s),
                                         {}, std::nullopt, tup({u"army_loc"_s, tag, ll(ai)})));
                for (size_t ri = 0; ri < a.regiments.size(); ++ri) {
                    const oob::Regiment &r = a.regiments[ri];
                    const oob::UnitType *ut = units.get(r.type);
                    if (!ut || ut->naval)
                        items.push_back(item(u"%1: %2 / %3 has unit type '%4', which isn't a land unit"_s.arg(name, a.name, r.name, r.type),
                                             {}, std::nullopt, tup({u"reg_type"_s, tag, ll(ai), ll(ri)})));
                    if (!land_prov(r.home)) {
                        items.push_back(item(u"%1: %2 / %3 comes from #%4 — not a land province"_s.arg(name, a.name, r.name, loc_txt(r.home)),
                                             {}, std::nullopt, tup({u"home"_s, tag, ll(ai), ll(ri)})));
                    } else if (doc.prov_state(*r.home).owner != OptStr(tag)) {
                        const OptStr ow = doc.prov_state(*r.home).owner;
                        items.push_back(item(u"%1: %2 / %3 comes from #%4 %5, owned by %6 — the game leaves it out at the start"_s.arg(
                                                 name, a.name, r.name, QString::number(*r.home), name_of(*r.home),
                                                 ow && !ow->isEmpty() ? *ow : u"nobody"_s),
                                             {*r.home}, _centre(md, *r.home), tup({u"home"_s, tag, ll(ai), ll(ri)})));
                    }
                }
            }
            const auto pcap = oob::OOBEdits::province_caps(sl);
            const auto over = ob.over_cap(tag, pcap);
            std::vector<std::pair<long long, long long>> ov(over.begin(), over.end());
            std::sort(ov.begin(), ov.end());
            std::map<long long, std::vector<Val>> per_home;
            for (const auto &[ai, ri] : ov) {
                const std::optional<long long> h = st.armies[size_t(ai)].regiments[size_t(ri)].home;
                if (land_prov(h) && doc.prov_state(*h).owner == OptStr(tag))
                    per_home[*h].push_back(tup({ai, ri}));
            }
            std::map<std::optional<long long>, long long> used_p;
            for (const oob::Army &a : st.armies)
                for (const oob::Regiment &r : a.regiments)
                    used_p[r.home] += 1;
            for (const auto &[h, regs] : per_home)
                items.push_back(item(u"%1: #%2 %3 raises %4 brigades, its soldier pops support %5 (the game keeps them, but "
                                     u"more than its pops can man)"_s.arg(name, QString::number(h), name_of(h),
                                                                          QString::number(used_p[h]),
                                                                          QString::number(pcap.value(h, 0))),
                                     {h}, _centre(md, h), tup({u"over"_s, tag, h, Val(regs)})));
            for (size_t ni = 0; ni < st.navies.size(); ++ni) {
                const oob::Navy &nv = st.navies[ni];
                const std::optional<long long> lc = nv.location;
                if (!lc || !provs.contains(*lc)) {
                    items.push_back(item(u"%1: %2 starts at #%3 — no such province"_s.arg(name, nv.name, loc_txt(lc)), {},
                                         std::nullopt, tup({u"navy_loc"_s, tag, ll(ni)})));
                } else if (!provs.get(*lc)->sea && doc.coastal && *lc < ll(doc.coastal->size()) && !(*doc.coastal)[size_t(*lc)]) {
                    items.push_back(item(u"%1: %2 starts at #%3 %4, which isn't coastal"_s.arg(name, nv.name, QString::number(*lc), name_of(*lc)),
                                         {*lc}, _centre(md, *lc), tup({u"navy_loc"_s, tag, ll(ni)})));
                }
                for (size_t si = 0; si < nv.ships.size(); ++si) {
                    const oob::Ship &s = nv.ships[si];
                    const oob::UnitType *ut = units.get(s.type);
                    if (!ut || !ut->naval)
                        items.push_back(item(u"%1: %2 / %3 has ship type '%4', which isn't a ship"_s.arg(name, nv.name, s.name, s.type),
                                             {}, std::nullopt, tup({u"ship_type"_s, tag, ll(ni), ll(si)})));
                }
            }
            QStringList others;
            for (const auto &kv : st.relations)
                others << kv.first;
            std::sort(others.begin(), others.end());
            for (const QString &other : others) {
                if (!mod.countries.contains(other))
                    items.push_back(item(u"%1: relation with %2, a nation that doesn't exist"_s.arg(name, other), {}, std::nullopt,
                                         tup({u"rel_unknown"_s, tag, other})));
                else if (other == tag)
                    items.push_back(item(u"%1: a relation with itself"_s.arg(name), {}, std::nullopt,
                                         tup({u"rel_unknown"_s, tag, other})));
            }
        }
        nfix = 0;
        for (const Item &it : items)
            nfix += it.data[0] != Val(u"oob_file"_s);
        fix.reset();
        if (nfix)
            fix = Fix{u"Fix the %1 army / navy problem%2"_s.arg(nfix).arg(plural(nfix)), u"oob"_s, Val()};
        cats.push_back(cat(u"oob"_s, u"Armies and navies at the start"_s, SEV_WARN, items, fix,
                           u"Brigades from another nation's province, or more than a state's soldiers allow: moved to the "
                           u"nearest of the nation's states with soldiers left (removed when there are none) · a unit type "
                           u"the game doesn't know: infantry / frigate · an army not on land: to the capital · a navy away "
                           u"from the coast: to the nearest coastal province of the nation · a relation with a nation that "
                           u"doesn't exist: removed. Double-click opens Armies."_s,
                           u"armies"_s, u"Fix this one"_s));
    }

    // --- diplomacy at the start
    diplomacy::DiploEdits *dip = nullptr;
    if (const store::StoreP *dp = doc.maps.get(u"diplomacy"_s))
        dip = dynamic_cast<diplomacy::DiploEdits *>(dp->get());
    if (dip && oobp) {
        items.clear();
        std::set<std::tuple<QString, QString, QString>> seen;
        OrderedMap<QString, std::vector<diplomacy::Record>> subject_of;
        for (const diplomacy::Record &r : dip->at_start()) {
            const QString what = u"%1 %2 → %3 (%4)"_s.arg(diplomacy::KIND_NAME.value(r.kind, r.kind), r.first, r.second, r.file);
            if (!mod.countries.contains(r.first) || !mod.countries.contains(r.second)) {
                QStringList bad;
                for (const QString &t : {r.first, r.second})
                    if (!mod.countries.contains(t))
                        bad << t;
                items.push_back(item(u"%1: %2 doesn't exist"_s.arg(what, bad.join(u" and "_s)), {}, std::nullopt,
                                     tup({u"treaty_remove"_s, r.id, r.first, r.second})));
                continue;
            }
            if (r.first == r.second) {
                items.push_back(item(u"%1: a nation with itself"_s.arg(what), {}, std::nullopt,
                                     tup({u"treaty_remove"_s, r.id, r.first, r.second})));
                continue;
            }
            const auto key = r.kind == u"alliance" ? std::make_tuple(r.kind, std::min(r.first, r.second), std::max(r.first, r.second))
                                                   : std::make_tuple(r.kind, r.first, r.second);
            if (seen.count(key)) {
                items.push_back(item(u"%1: listed twice"_s.arg(what), {}, std::nullopt,
                                     tup({u"treaty_remove"_s, r.id, r.first, r.second})));
                continue;
            }
            seen.insert(key);
            if (r.kind == u"vassal" || r.kind == u"substate")
                subject_of[r.second].push_back(r);
        }
        for (const auto &[t, recs] : sorted_items(subject_of)) {
            QStringList firsts;
            for (const auto &q : recs)
                firsts << q.first;
            for (size_t k = 1; k < recs.size(); ++k) {
                const diplomacy::Record &r = recs[k];
                items.push_back(item(u"%1 is the subject of %2 — %3 %4 → %5 is one too many"_s.arg(
                                         t, firsts.join(u" and "_s), diplomacy::KIND_NAME.value(r.kind), r.first, r.second),
                                     {}, std::nullopt, tup({u"treaty_remove"_s, r.id, r.first, r.second})));
            }
        }
        QSet<QString> gps;
        if (nations)
            for (const QString &g : diplomacy::great_powers(mod, *nations))
                gps.insert(g);
        QStringList twu(oobp->tags_with_units().begin(), oobp->tags_with_units().end());
        std::sort(twu.begin(), twu.end());
        for (const QString &t : twu) {
            if (gps.contains(t) || !mod.countries.contains(t))
                continue;
            for (const auto &[other, r] : sorted_items(oobp->state(t).relations))
                if (r.level || (r.influence_value && *r.influence_value != u"0"))
                    items.push_back(item(u"%1 %2 isn't a great power at the start, but its file gives it a standing with %3 "
                                         u"(level %4, influence %5) — the game ignores it"_s.arg(
                                             t, mod.countries.get(t)->name, other,
                                             r.level && !r.level->isEmpty() ? *r.level : u"—"_s,
                                             r.influence_value && !r.influence_value->isEmpty() ? *r.influence_value : u"0"_s),
                                         {}, std::nullopt, tup({u"nongp"_s, t, other})));
        }
        // (Python walks a SET of tags here — string hashes, a new order every run; the port takes them sorted)
        for (const auto &[t, ls0] : sorted_items(diplomacy::sphere_lords(*oobp, twu))) {
            QStringList ls;
            for (const QString &l : ls0)
                if (gps.contains(l))
                    ls << l;
            if (ls.isEmpty())
                ls = ls0;
            if (ls.size() > 1 && mod.countries.contains(t))
                items.push_back(item(u"%1 %2 is in %3 spheres (%4) — a nation can only be in one"_s.arg(
                                         t, mod.countries.get(t)->name, QString::number(ls.size()), ls.join(u", "_s)),
                                     {}, std::nullopt, tup({u"sphere"_s, t, texts(ls)})));
        }
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Fix the %1 diplomacy problem%2"_s.arg(items.size()).arg(plural(ll(items.size()))), u"diplo"_s, Val()};
        cats.push_back(cat(u"diplo"_s, u"Diplomacy at the start"_s, SEV_WARN, items, fix,
                           u"Treaties with a nation that doesn't exist, with itself, listed twice or a second overlord: "
                           u"removed · in several spheres: stays in the sphere with the most influence points (the others "
                           u"go to Friendly) · a standing (influence) of a nation that isn't a great power at the start "
                           u"(the top 8 westernized by prestige): removed. Double-click opens Diplomacy."_s,
                           u"diplomacy"_s, u"Fix this one"_s));
    }

    // --- wars at the start between nations the mod doesn't have (the game's own wars are inherited)
    scriptstore::ScriptStore *store = nullptr;
    if (const store::StoreP *sp = doc.maps.get(u"scripts"_s))
        store = dynamic_cast<scriptstore::ScriptStore *>(sp->get());
    if (store) {
        items.clear();
        for (const QString &rel : entryview::_files(mod, store, u"history/wars"_s, true)) {
            std::set<QString> bs;
            for (const QString &t : entryview::war_sides(store->text(rel)))
                if (!mod.countries.contains(t))
                    bs.insert(t);
            if (!bs.empty()) {
                const QStringList bad(bs.begin(), bs.end());
                const bool own = entryview::in_mod(mod, *store, rel);
                items.push_back(item(u"%1: %2 %3 not in the mod%4"_s.arg(
                                         rel.mid(rel.lastIndexOf(u'/') + 1), bad.join(u", "_s), bad.size() == 1 ? u"is"_s : u"are"_s,
                                         own ? QString()
                                             : u" (the game's own file: the mod would have to replace the history folder)"_s),
                                     {}, std::nullopt, own ? tup({u"war"_s, rel}) : Val()));
            }
        }
        try {                                    // --- events that share an id
            std::vector<Item> dup_items;
            auto ids = eventtools::duplicate_ids(mod, *store);
            std::vector<std::pair<QString, std::vector<eventtools::Place>>> idl(ids.begin(), ids.end());
            std::stable_sort(idl.begin(), idl.end(), [](const auto &a, const auto &b) {
                return std::make_pair(a.first.size(), a.first) < std::make_pair(b.first.size(), b.first);
            });
            for (const auto &[eid, lst] : idl) {
                const auto &first = lst[0];
                for (size_t k = 1; k < lst.size(); ++k) {
                    const auto &[rel, pos, _ln] = lst[k];
                    const QString t = store->text(rel);
                    const long long line = t.left(pos).count(u'\n') + 1;
                    dup_items.push_back(item(
                        u"Event id %1 is used %2 times — this one in %3 (line %4); the first is in %5. The game keeps only "
                        u"one event of an id."_s.arg(eid, QString::number(lst.size()), rel.mid(rel.indexOf(u'/') + 1),
                                                     QString::number(line),
                                                     std::get<0>(first).mid(std::get<0>(first).indexOf(u'/') + 1)),
                        {}, std::nullopt, tup({u"evid"_s, rel, ll(pos), eid})));
                }
            }
            std::optional<Fix> dfix;
            if (!dup_items.empty())
                dfix = Fix{u"Give the %1 later one%2 free ids"_s.arg(dup_items.size()).arg(plural(ll(dup_items.size()))),
                           u"event_ids"_s, Val()};
            cats.push_back(cat(u"event_ids"_s, u"Events with an id another event has too"_s, SEV_ERROR, dup_items, dfix,
                               u"Every event needs a number of its own. The fix gives the later event the next free id of "
                               u"its file (its title and text keys stay) — check what was meant to call it. Double-click "
                               u"opens it in Scripts."_s,
                               std::nullopt, u"Give this one a free id"_s));
        } catch (const std::exception &) {
        }
        // --- (C++ only, 2026-11-09, the user's wish) events / decisions naming a province that is gone: a number this
        // mod has no province for (MEASURED: the game reads it with no check — `owns = 2591` in the tester's cut map
        // crashed the campaign start), and the places a delete / map cut took out (their "# ModEditor:" note)
        try {
            using Hit = provrenumber::ProvRef;
            static QHash<QString, std::pair<size_t, std::vector<Hit>>> cache;   // rel -> (text hash, hits)
            std::vector<Item> gp;
            long long nmiss = 0;
            for (const QString folder : {u"events"_s, u"decisions"_s})
                for (const QString &rel : entryview::_files(mod, store, folder, true)) {
                    const QString t = store->text(rel);
                    const size_t h = qHash(t);
                    auto c = cache.find(rel);
                    if (c == cache.end() || c->first != h)
                        c = cache.insert(rel, {h, provrenumber::province_refs(t)});
                    for (const Hit &x : c->second) {
                        if (x.note) {
                            gp.push_back(item(u"%1 (line %2): named province %3, which was deleted — this condition is "
                                              u"never true now (or the effect was taken out). Check what it was for, "
                                              u"then delete the “# ModEditor:” note."_s
                                                  .arg(rel, QString::number(t.left(x.pos).count(u'\n') + 1),
                                                       QString::number(x.num)),
                                              {}, std::nullopt, tup({u"gone"_s, rel, ll(x.pos)})));
                        } else if (x.num > 0 && !mod.provinces.contains(x.num)) {
                            ++nmiss;
                            gp.push_back(item(u"%1 (line %2): names province %3, which this mod doesn't have — the game "
                                              u"crashes when it runs this"_s
                                                  .arg(rel, QString::number(t.left(x.pos).count(u'\n') + 1),
                                                       QString::number(x.num)),
                                              {}, std::nullopt,
                                              tup({u"missing"_s, rel, ll(x.pos), QString::number(x.num)})));
                        }
                    }
                }
            std::optional<Fix> gfix;
            if (nmiss)
                gfix = Fix{u"Take the %1 place%2 out"_s.arg(nmiss).arg(plural(nmiss)), u"script_prov"_s, Val()};
            cats.push_back(cat(u"script_prov"_s, u"Events and decisions naming provinces that are gone"_s, SEV_ERROR, gp,
                               gfix,
                               u"Measured in the game (2026-11-08): a province number in a condition or effect is read "
                               u"with no check — one this mod doesn't have crashes the game when the event or decision "
                               u"is looked at (`owns = 2591` crashed a cut map a few days into the campaign). The fix takes "
                               u"the place out the way a delete does: a condition becomes `always = no`, an effect is "
                               u"removed, and a “# ModEditor:” note stays. Notes stay listed here until you delete them, so "
                               u"you see what a delete or a map cut changed (e.g. an event about a canal that is gone). "
                               u"Double-click opens the place in Scripts."_s,
                               std::nullopt, u"Take this one out"_s));
        } catch (const std::exception &) {
        }
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Take the %1 war%2 out"_s.arg(items.size()).arg(plural(ll(items.size()))), u"wars"_s, Val()};
        cats.push_back(cat(u"wars"_s, u"Wars at the start between nations that don't exist"_s, SEV_WARN, items, fix,
                           u"history/wars. Taken out: the mod's file is deleted on Apply (with a backup) — never emptied, "
                           u"an empty war file crashes the game. Double-click opens Warfare → Wars at the start."_s,
                           std::nullopt, u"Take this one out"_s));
        try {                                    // --- the war goals, and names that don't exist
            for (Category &c : war_goal_checks(mod, doc, *store))
                cats.push_back(std::move(c));
        } catch (const std::exception &) {
        }
    }

    // --- pop files
    items.clear();
    for (qsizetype i = 0; i < doc.problems.size() && i < 600; ++i)
        items.push_back(item(doc.problems[i]));
    cats.push_back(cat(u"popfiles"_s, u"Pop files: lines that couldn't be read"_s, SEV_WARN, items, std::nullopt,
                       u"These lines of history/pops/%1 were skipped. Fix them in the file."_s.arg(doc.scenario), u"pops"_s));

    // --- pops above the share at which their type stops growing (poptypes/*.txt: promote_to with a factor of -100)
    popcaps::Caps caps;
    std::vector<popcaps::Over> over;
    try {
        caps = popcaps::read_caps(mod, store ? std::function<QString(const QString &)>([&](const QString &r) { return store->text(r); })
                                             : std::function<QString(const QString &)>());
        over = popcaps::over_cap(mod, doc, caps);
    } catch (const std::exception &) {
        caps = {};
        over = {};
    }
    std::map<QString, std::vector<popcaps::Over>> by_type;
    for (const auto &row : over)
        by_type[row.type].push_back(row);
    for (const auto &[t, e] : sorted_items(caps)) {
        if (!e.province && !e.state)
            continue;                                // (some type can still become it: no ceiling to be over)
        QString nm = mod.loc.value(t);
        if (nm.isEmpty())
            nm = QString(t).replace(u'_', u' ');
        nm = py::strip(nm);
        items.clear();
        for (const popcaps::Over &o : by_type[t]) {
            const long long extra = o.have - py::trunc_int(o.cap * double(o.total));
            const QString pct = py::fmt_f(100.0 * double(o.have) / double(o.total), 1);
            if (o.scope == u"province") {
                const long long where = std::get<long long>(o.where);
                const moddata::Province *pr = provs.get(where);
                items.push_back(item(u"#%1 %2: %3 % %4 (%5 of %6 people) — %7 over the %8 %"_s.arg(
                                         QString::number(where), pr ? pr->name : u"?"_s, pct, py::lower(nm), fmt_n(o.have),
                                         fmt_n(o.total), fmt_n(extra), py::fmt_g(o.cap * 100, 3)),
                                     {where}, centre_if(where), tup({u"popcap"_s, t, o.scope, where, o.cap})));
            } else {
                const QString where = std::get<QString>(o.where);
                std::vector<long long> ps;
                if (const moddata::Region *rg = mod.regions.get(where))
                    for (long long p : rg->provinces)
                        if (provs.contains(p))
                            ps.push_back(p);
                items.push_back(item(u"state %1: %2 % %3 (%4 of %5 people) — %6 over the %7 %"_s.arg(
                                         where, pct, py::lower(nm), fmt_n(o.have), fmt_n(o.total), fmt_n(extra),
                                         py::fmt_g(o.cap * 100, 3)),
                                     ps, !ps.empty() && md.area[ps[0]] ? std::optional<XY>(_centre(md, ps[0])) : std::nullopt,
                                     tup({u"popcap"_s, t, o.scope, where, o.cap})));
            }
        }
        QStringList whats;
        if (e.province)
            whats << u"%1 % of a province"_s.arg(py::fmt_g(*e.province * 100, 3));
        if (e.state)
            whats << u"%1 % of a state"_s.arg(py::fmt_g(*e.state * 100, 3));
        const QString what = whats.join(u" and "_s);
        std::set<QString> ls;
        for (const auto &l : e.lines)
            ls.insert(u"%1:%2"_s.arg(l.rel).arg(l.line));
        const QStringList lines(ls.begin(), ls.end());
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Bring all %1 down to the ceiling (the surplus become workers)"_s.arg(items.size()), u"popcap"_s, Val()};
        cats.push_back(cat(
            u"popcap_"_s + t, u"%1 above their ceiling (%2)"_s.arg(py::capitalize(nm), what), SEV_WARN, items, fix,
            u"The pop type files switch the growth of %1 off at %2: there every type that can become one has a promotion "
            u"line with a factor of -100 or lower (%3%4). These places START above that — more than the mod's own rules "
            u"ever let grow. The fix turns the surplus into the commonest working type of their province (farmers, "
            u"labourers or craftsmen; same culture and religion). Double-click a line to see its pops."_s.arg(
                py::lower(nm), what, lines.mid(0, 4).join(u", "_s), lines.size() > 4 ? u" …"_s : QString()),
            u"pops"_s, u"Bring this one down to the ceiling"_s));
    }

    // --- history / states
    const auto &hist = doc.hist->files;
    items.clear();
    for (long long p : land) {
        const auto *fl = hist.get(p);
        if (!fl || fl->empty())
            items.push_back(item(u"#%1 %2 has no history file"_s.arg(QString::number(p), name_of(p)), {p}, _centre(md, p)));
    }
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Make an empty history file for each (%1)"_s.arg(items.size()), u"nohist"_s, Val()};
    cats.push_back(cat(u"nohist"_s, u"Land provinces without a history file"_s, SEV_WARN, items, fix,
                       u"Provinces needs a history file to write a province's owner, cores, buildings into. The fix makes "
                       u"an empty one (history/provinces/<the folder of its state>/<id> - <name>.txt), written on Apply."_s,
                       u"map"_s, u"Make an empty history file for it"_s));
    items.clear();
    for (const auto &[p, _f] : sorted_items(hist))
        if (!land_set.contains(p) && (!provs.contains(p) || provs.get(p)->sea))
            items.push_back(item(u"history file for #%1, which is %2"_s.arg(QString::number(p),
                                                                           provs.contains(p) ? u"a sea tile"_s : u"not defined"_s),
                                 {p}));
    cats.push_back(cat(u"histsea"_s, u"History files for sea tiles / unknown ids"_s, SEV_INFO, items));
    // regions that look like utility states (super regions only grouping provinces, e.g. ANY_PROVINCE)
    const auto sugg = mod.utility_suggestions();
    QSet<QString> sugg_ids;
    items.clear();
    for (const auto &[rid, why] : sugg) {
        sugg_ids.insert(rid);
        items.push_back(item(u"%1 — %2"_s.arg(rid, why), {}, std::nullopt, tup({u"utility"_s, rid})));
    }
    fix.reset();
    if (!items.empty())
        fix = Fix{items.size() > 1 ? u"Mark all %1 as utility states"_s.arg(items.size()) : u"Mark it as a utility state"_s,
                  u"utility"_s, Val()};
    cats.push_back(cat(u"utility_suggest"_s, u"Regions that look like utility states (no real state)"_s, SEV_WARN, items, fix,
                       u"Some mods list super regions in region.txt only to group provinces for events and decisions (e.g. "
                       u"ANY_PROVINCE with every province in it). The game counts the FIRST region listing a province as "
                       u"its state, so these are nobody's state — but every province in them shows up below as “in 2 "
                       u"states”. Marking one as a utility state (kept per mod in the editor, nothing is written into the "
                       u"mod) makes the state checks ignore it; view and edit it in Utility states."_s,
                       u"provinces"_s, u"Mark it as a utility state"_s));
    OrderedMap<long long, QStringList> regs;
    for (const auto &[rid, r] : mod.regions)
        for (long long p : r.provinces)
            regs[p] << rid;
    std::vector<Item> sitems;
    for (long long p : land)
        if (!regs.contains(p) || regs.get(p)->isEmpty())
            sitems.push_back(item(u"#%1 %2 is in no state"_s.arg(QString::number(p), name_of(p)), {p}, _centre(md, p),
                                  tup({u"none"_s, p})));
    const auto regs_sorted = sorted_items(regs);
    for (const auto &[p, v] : regs_sorted)
        if (v.size() > 1 && land_set.contains(p)) {
            bool util = false;
            for (const QString &r : v)
                util = util || sugg_ids.contains(r);
            sitems.push_back(item(u"#%1 %2 is in %3 states: %4"_s.arg(QString::number(p), provs.contains(p) ? name_of(p) : u"?"_s,
                                                                     QString::number(v.size()), v.join(u", "_s)),
                                  {p}, std::nullopt, tup({u"multi"_s, p, texts(v), util})));
        }
    for (const auto &[p, v] : regs_sorted)
        if (!land_set.contains(p) && (!provs.contains(p) || provs.get(p)->sea))
            sitems.push_back(item(u"state %1 lists #%2, which is %3"_s.arg(v.join(u", "_s), QString::number(p),
                                                                           provs.contains(p) ? u"a sea tile"_s : u"not defined"_s),
                                  {p}, std::nullopt, tup({u"bad"_s, p, texts(v)})));
    std::optional<Fix> sfix;
    if (!sitems.empty())
        sfix = Fix{u"Fix all %1 (the state of the neighbours / keep one / drop sea tiles)"_s.arg(sitems.size()), u"states"_s, Val()};
    std::vector<Item> split_items, owner_items;
    // (meme-river crossings and land links join a state's parts: C++ only, the user 2026-11-13)
    const provpanel::Links slinks =
        provpanel::state_links(doc.maps.contains(u"adjacency"_s) ? &doc.map<adjacency::AdjacencyFile>(u"adjacency"_s) : nullptr);
    for (const auto &[rid, r] : sorted_items(mod.regions)) {
        std::vector<long long> ps;
        for (long long p : r.provinces)
            if (land_set.contains(p))
                ps.push_back(p);
        if (ps.size() < 2)
            continue;
        const auto prts = provpanel::state_components(mod, &md, rid, &slinks);
        if (prts.size() > 1) {
            size_t big = 0;
            for (size_t k = 1; k < prts.size(); ++k)
                if (prts[k].size() > prts[big].size())
                    big = k;
            std::vector<long long> cut;
            for (size_t k = 0; k < prts.size(); ++k)
                if (k != big)
                    cut.insert(cut.end(), prts[k].begin(), prts[k].end());
            std::sort(cut.begin(), cut.end());
            QStringList cs;
            for (size_t k = 0; k < cut.size() && k < 6; ++k)
                cs << u"#%1"_s.arg(cut[k]);
            split_items.push_back(item(u"%1 is in %2 unconnected parts (%3 apart from the rest)"_s.arg(
                                           rid, QString::number(prts.size()), cs.join(u", "_s) + (cut.size() > 6 ? u" …"_s : QString())),
                                       cut, md.area[cut[0]] ? std::optional<XY>(_centre(md, cut[0])) : std::nullopt));
        }
        std::vector<std::pair<OptStr, long long>> owners;   // (a Counter: first-seen order)
        for (long long p : ps) {
            const OptStr o = doc.prov_state(p).owner;
            auto oi = std::find_if(owners.begin(), owners.end(), [&](const auto &x) { return x.first == o; });
            if (oi == owners.end())
                owners.emplace_back(o, 1);
            else
                oi->second += 1;
        }
        if (owners.size() > 1) {
            std::vector<std::pair<OptStr, long long>> oc = owners;
            std::stable_sort(oc.begin(), oc.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
            QStringList os;
            for (const auto &[o, c] : oc)
                os << u"%1 ×%2"_s.arg(o && !o->isEmpty() ? *o : u"no owner"_s).arg(c);
            owner_items.push_back(item(u"%1 has %2 owners: %3"_s.arg(rid, QString::number(owners.size()), os.join(u", "_s)), ps,
                                       _centre(md, ps[0])));
        }
    }
    cats.push_back(cat(u"states_split"_s, u"States in unconnected parts"_s, SEV_INFO, split_items, std::nullopt,
                       u"Often on purpose (islands, colonies) — the game treats each part as the same state. Move "
                       u"provinces to another state in Provinces if it isn't."_s,
                       u"provinces"_s));
    cats.push_back(cat(u"states_owners"_s, u"States with several owners"_s, SEV_INFO, owner_items, std::nullopt,
                       u"The game splits such a state per owner at the start. Fine if intended."_s, u"provinces"_s));
    // states region.txt lists without a province: a state without provinces is gone (user's rule) — the fix takes their
    // entries out on Apply
    QSet<QString> gone_q = doc.regions_touched;
    if (const store::StoreP *dp = doc.maps.get(u"drop_states"_s))
        if (auto *ds = dynamic_cast<document::DropStates *>(dp->get()))
            gone_q += ds->rids;
    items.clear();
    for (const QString &rid : doc.empty_states_on_disk()) {
        if (gone_q.contains(rid))
            continue;
        const QString &rn = mod.regions.get(rid)->name;
        items.push_back(item(u"%1 — no province left%2"_s.arg(rid, rn != rid ? u" (“%1”)"_s.arg(rn) : QString()), {}, std::nullopt,
                             tup({u"emptystate"_s, rid})));
    }
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Take them out of region.txt (%1)"_s.arg(items.size()), u"empty_states"_s, Val()};
    cats.push_back(cat(u"empty_states"_s, u"States without provinces"_s, SEV_WARN, items, fix,
                       u"A state whose last province went elsewhere is gone: its entry leaves map/region.txt on Apply (the "
                       u"editor does this by itself when you empty or delete a state). These were already empty in the "
                       u"file. Its name in the localisation stays — harmless. Scripts that name it show up under the "
                       u"script checks."_s,
                       u"provinces"_s, u"Take this one out"_s));
    // pops the mod can't have (the tester, 2026-10-15: pops copied from another mod brought a pop type this mod lacks)
    const popcheck::Problems pp = popcheck::problems(doc, mod);
    for (const auto &[which, key, title, level, what] :
         {std::tuple{&pp.type, u"pop_types"_s, u"Pops of a pop type this mod doesn't have"_s, SEV_ERROR, u"pop type"_s},
          {&pp.culture, u"pop_cultures"_s, u"Pops of a culture that isn't defined"_s, SEV_WARN, u"culture"_s},
          {&pp.religion, u"pop_religions"_s, u"Pops of a religion that isn't defined"_s, SEV_WARN, u"religion"_s}}) {
        std::vector<std::pair<QString, popcheck::Problem>> ds(which->begin(), which->end());
        std::stable_sort(ds.begin(), ds.end(), [](const auto &a, const auto &b) { return -a.second.size < -b.second.size; });
        const QString field = key == u"pop_types" ? u"type"_s : key == u"pop_cultures" ? u"culture"_s : u"religion"_s;
        std::vector<Item> its;
        for (const auto &[k, d] : ds)
            its.push_back(item(u"%1 “%2”: %3 pop%4 (%5 people) in %6 province%7 · first: %8"_s.arg(
                                   py::capitalize(what), k, fmt_n(d.pops), plural(d.pops), fmt_n(d.size),
                                   QString::number(d.pids.size()), plural(ll(d.pids.size())), popcheck::where_text(d.where)),
                               d.pids, std::nullopt, tup({u"popfix"_s, field, k})));
        cats.push_back(cat(key, title, level, its, std::nullopt,
                           u"Pops copied from another mod can bring a %1 this mod doesn't have — the game can't make such "
                           u"pops. Population (the strip at the top) offers both ways out: import the %1 from a mod that has "
                           u"it, or change those pops into one this mod has. Double-click a line to go there; its provinces "
                           u"are marked on the map."_s.arg(what),
                           u"pops"_s));
    }
    items.clear();
    for (const auto &[pid, d] : sorted_items(pp.province))
        items.push_back(item(u"#%1 is not a province of this mod (definition.csv): %2 pops (%3 people) · %4"_s.arg(
                                 QString::number(pid), fmt_n(d.pops), fmt_n(d.size), popcheck::where_text(d.where)),
                             {}, std::nullopt, tup({u"popprov"_s, pid})));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Take them out of the pop files (%1 province%2, on Apply)"_s.arg(items.size()).arg(plural(ll(items.size()))),
                  u"pop_provinces"_s, Val()};
    // (C++ only: an ERROR since 2026-11-08 — measured in the game: pops for province 9999 crash it while it loads,
    // at "Executing History". The Python editor calls it a warning.)
    cats.push_back(cat(u"pop_provinces"_s, u"Pops of provinces that don't exist (crash)"_s, SEV_ERROR, items, fix,
                       u"A pop file names a province number that isn't in this mod's definition.csv. Measured in the "
                       u"game: it crashes while it loads (\"Executing History\"). The fix takes these blocks out of the "
                       u"pop files on Apply — of every start date; Ctrl+Z brings them back before that."_s,
                       u"pops"_s, u"Take this one out"_s));
    // a `mod` folder inside the mod: older ModEditors wrote technology / invention edits there when the mod is kept in
    // the game's own mod folder (the tester's report, 2026-10-15) — the game never reads it
    items.clear();
    for (const auto &[rel, how] : stray_mod_files(mod))
        items.push_back(item(u"%1 — %2"_s.arg(rel, how), {}, std::nullopt, tup({u"straymod"_s, rel})));
    fix.reset();
    if (!items.empty())
        fix = Fix{u"Move the folder out of the mod (%1 file%2, kept in the editor's backups)"_s.arg(items.size()).arg(plural(ll(items.size()))),
                  u"stray_mod"_s, Val()};
    cats.push_back(cat(u"stray_mod"_s, u"A stray 'mod' folder inside the mod"_s, SEV_WARN, items, fix,
                       u"An older ModEditor wrote technology / invention edits into <mod>/mod/… instead of the mod's own "
                       u"files when the mod sits in the game's mod folder — the game never reads that folder, so those "
                       u"edits never took effect. A file that DIFFERS from the mod's own holds edits that never arrived: "
                       u"make them again (they are listed here) before the folder goes. Nothing is deleted: the folder "
                       u"moves into the editor's backups."_s));
    // scripts that name a state that is gone (emptied / deleted): its key names nothing any more (Sam's item 2)
    items.clear();
    if (store) {
        QStringList gone;
        for (const auto &[rid, r] : mod.regions)
            if (r.provinces.empty())
                gone << rid;
        std::sort(gone.begin(), gone.end());
        for (const QString &rid : gone) {
            const std::vector<long long> was = doc.region_orig.value(rid);
            for (const staterefs::Place &x : staterefs::find(mod, *store, rid))
                items.push_back(item(u"%1:%2 names %3 — %4"_s.arg(x.rel, QString::number(x.line), rid, x.text.left(70)), was,
                                     std::nullopt, tup({u"goneref"_s, rid, x.rel, x.kind})));
        }
    }
    long long fixable = 0;
    for (const Item &it : items)
        fixable += (it.data[3] == Val(u"scope"_s) || it.data[3] == Val(u"region"_s)) && !it.pids.empty();
    fix.reset();
    if (fixable)
        fix = Fix{u"Write the provinces it had instead (%1)"_s.arg(fixable), u"gone_state_refs"_s, Val()};
    cats.push_back(cat(u"gone_state_refs"_s, u"Scripts naming a state that is gone"_s, SEV_WARN, items, fix,
                       u"The state has no province left, so its entry leaves region.txt — its key names nothing any more. "
                       u"The fix writes the provinces it had in region.txt instead: a scope KEY = { … } becomes one block "
                       u"per province, region = KEY becomes province_id = …"_s,
                       u"provinces"_s, u"Fix this file"_s));
    cats.push_back(cat(u"states"_s, u"States (region.txt)"_s, SEV_ERROR, sitems, sfix,
                       u"A province in no state joins the state most of its neighbours are in; one in several states stays "
                       u"in the one most of its neighbours share; sea tiles / unknown ids are taken out of the state lists. "
                       u"Provinces that are also in a region that looks like a utility state are left alone — mark that "
                       u"region as a utility state instead (above)."_s,
                       u"map"_s, u"Fix this one"_s));

    // --- continent / climate
    for (const QString &kind : {u"continent"_s, u"climate"_s}) {
        const auto mem = doc.map<mapfiles::MembershipFile>(kind).membership();
        items.clear();
        for (long long p : land) {
            const QStringList *m = mem.get(p);
            if (!m || m->isEmpty())
                items.push_back(item(u"#%1 %2 has no %3"_s.arg(QString::number(p), name_of(p), kind), {p}, _centre(md, p)));
        }
        for (long long p : land) {
            const QStringList *m = mem.get(p);
            if (m && m->size() > 1)
                items.push_back(item(u"#%1 %2 is in %3 %4s: %5"_s.arg(QString::number(p), name_of(p), QString::number(m->size()),
                                                                      kind, m->join(u", "_s)),
                                     {p}, _centre(md, p)));
        }
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Give the %1 the %2 of their neighbours"_s.arg(_n(ll(items.size()), u"province"_s, u"provinces"_s), kind),
                      u"from_neighbours"_s, Val(kind)};
        cats.push_back(cat(kind, py::capitalize(kind) + u"s"_s, SEV_WARN, items, fix, QString(), u"map"_s,
                           u"Give it the %1 of its neighbours"_s.arg(kind)));
    }

    // --- naval bases the history builds on provinces without a valid coast
    items.clear();
    for (long long p : land) {
        const long long lvl = doc.prov_state(p).naval_base;
        bool coast = false;
        for (long long q : md.neighbours(p))
            if (0 < q && q < ll(valid_sea.size()) && valid_sea[size_t(q)]) {
                coast = true;
                break;
            }
        if (lvl > 0 && !coast)
            items.push_back(item(u"#%1 %2: naval base %3 in the %4 history, but no valid coast"_s.arg(
                                     QString::number(p), name_of(p), QString::number(lvl), doc.scenario),
                                 {p}, _centre(md, p)));
    }
    fix.reset();
    if (!items.empty() && doc.history_editable())
        fix = Fix{u"Remove these %1 naval bases (%2 history)"_s.arg(QString::number(items.size()), doc.scenario), u"naval_inland"_s, Val()};
    cats.push_back(cat(u"naval"_s, u"Naval bases without a valid coast"_s, SEV_ERROR, items, fix,
                       u"A naval base needs a coast on a sea tile that isn't a Meme river / no-coast tile. Without a "
                       u"position the game crashes drawing it (see Positions)."_s,
                       u"provinces"_s, u"Remove this naval base"_s));

    // --- rivers.bmp rules (1 pixel wide, 4-connected, on land, green sources at an end, red merges between two)
    layerbmp::IndexedBitmap *rb = nullptr, *tb = nullptr;
    if (const store::StoreP *bp = doc.maps.get(u"rivers_bmp"_s))
        rb = dynamic_cast<layerbmp::IndexedBitmap *>(bp->get());
    if (rb && rb->load()) {
        const auto key = map_key(md, {rb});
        auto it = geo_cache.kept.find(u"rivers"_s);
        if (it == geo_cache.kept.end() || it->second.key != key)
            geo_cache.kept[u"rivers"_s] = {key, {river_checks(md, *rb->data, &river_masks_whole(md, *rb))}};
        cats.push_back(_again(geo_cache.kept[u"rivers"_s].cats[0]));
    }
    // --- (C++ only, measured in the game 2026-11-17/18 after Map size cuts of the base game) the map's edges:
    //  - on a map whose height is no whole number of its tiles, land or a river in the two SOUTHERNMOST rows crashed the
    //    game at load (a river at "Creating Rivers…", land at "REcreating quads"; one row of sea was not enough), and a
    //    river in the two NORTHERNMOST rows did too ("Creating Rivers…"). At a whole number of tiles (the base game,
    //    Ferrum Mare) land and rivers there loaded, and one Ferrum Mare cut in width loaded with land there: a warning.
    //  - OWNED land in the two WESTERNMOST columns crashed the game at the main menu when it drew the country borders
    //    (after cuts from the west; the east and north edges are not affected)
    {
        items.clear();
        const auto side = mapsize::tile_side(md.w);
        const bool rows = md.h > 2 && !(side && md.h % *side == 0), cols = md.w > 2;
        const bool have_rb = rb && rb->data && rb->data->h() == md.h && rb->data->w() == md.w;
        // land province -> (its pixels there, first pixel), per edge
        std::map<long long, std::pair<long long, long long>> south_land, west_land;
        long long nriv_s = 0, nriv_n = 0, rsx = -1, rsy = -1, rnx = -1, rny = -1;
        auto land_px = [&](std::map<long long, std::pair<long long, long long>> &to, long long y, long long x,
                           bool owned_only) {
            const long long id = md.idmap(y, x);
            const auto *pr = id > 0 ? provs.get(id) : nullptr;
            if (!pr || pr->sea || (owned_only && !doc.prov_state(id).owner))
                return;
            auto [it, first_px] = to.try_emplace(id, 0, x + (y << 32));
            ++it->second.first;
        };
        if (rows)
            for (const long long y : {md.h - 2, md.h - 1, 0LL, 1LL})
                for (long long x = 0; x < md.w; ++x) {
                    const bool south = y >= md.h - 2;
                    if (south)
                        land_px(south_land, y, x, false);
                    if (have_rb && (*rb->data)(y, x) < 12) {
                        auto &cnt = south ? nriv_s : nriv_n;
                        if (!cnt)
                            (south ? rsx : rnx) = x, (south ? rsy : rny) = y;
                        ++cnt;
                    }
                }
        if (cols)
            for (long long y = 0; y < md.h; ++y)
                for (const long long x : {0LL, 1LL})
                    land_px(west_land, y, x, true);
        auto list_land = [&](const std::map<long long, std::pair<long long, long long>> &m, const QString &where) {
            for (const auto &[id, nx] : m) {
                const long long x = nx.second & 0xffffffffLL, y = nx.second >> 32;
                items.push_back(item(u"#%1 %2: %3 pixel%4 of land in %5"_s.arg(
                                         QString::number(id), name_of(id), QString::number(nx.first),
                                         nx.first == 1 ? QString() : u"s"_s, where),
                                     {id}, XY{double(x) + 0.5, double(y) + 0.5}));
            }
        };
        list_land(south_land, u"the two southernmost rows"_s);
        if (nriv_s)
            items.push_back(item(u"rivers.bmp: %1 river pixel%2 in the two southernmost rows"_s.arg(nriv_s).arg(
                                     nriv_s == 1 ? QString() : u"s"_s),
                                 {}, XY{double(rsx) + 0.5, double(rsy) + 0.5}));
        if (nriv_n)
            items.push_back(item(u"rivers.bmp: %1 river pixel%2 in the two northernmost rows"_s.arg(nriv_n).arg(
                                     nriv_n == 1 ? QString() : u"s"_s),
                                 {}, XY{double(rnx) + 0.5, double(rny) + 0.5}));
        list_land(west_land, u"the two westernmost columns (owned)"_s);
        fix.reset();
        if (!items.empty())
            fix = Fix{u"Make the edges safe"_s, u"south_edge"_s, Val()};
        if (rows || !west_land.empty())
            cats.push_back(cat(u"south_edge"_s, u"Land or rivers on the map's edge — the game may crash while it loads"_s,
                               SEV_WARN, items, fix,
                               u"Tried in the game after Map size cuts. On a map whose height is no whole number of its "
                               u"tiles (the tile side comes from the width), land or a river pixel in the two southernmost "
                               u"rows crashed it while it loaded the map (a river at “Creating Rivers…”, land at "
                               u"“REcreating quads”; one row of sea was not enough), and so did a river in the two "
                               u"northernmost rows. Land of a country in the two westernmost columns crashed it at the main "
                               u"menu, when it drew the country borders. One such map loaded all the same, so this is a "
                               u"warning; the base game has only sea along those edges. The fix makes the two southernmost "
                               u"rows and the owned land of the two westernmost columns sea (each land pixel takes the "
                               u"nearest sea pixel of its row / column) and takes the rivers off those rows and columns "
                               u"(each river pixel takes the nearest pixel that is no river). Ctrl+Z takes it back."_s,
                               u"map"_s, u"Make the edges safe"_s));
    }
    // --- land / sea of terrain.bmp and rivers.bmp against the provinces
    if (const store::StoreP *bp = doc.maps.get(u"terrain_bmp"_s))
        tb = dynamic_cast<layerbmp::IndexedBitmap *>(bp->get());
    if (tb)
        tb->load();
    {
        const auto key = map_key(md, {tb, rb});
        auto it = geo_cache.kept.find(u"landsea"_s);
        if (it == geo_cache.kept.end() || it->second.key != key)
            geo_cache.kept[u"landsea"_s] = {key, landsea_checks(mod, md, tb, rb)};
        for (const Category &c : geo_cache.kept[u"landsea"_s].cats)
            cats.push_back(_again(c));
    }
    // --- adjacencies.csv
    for (Category &c : adjacency_checks(mod, md, doc, adj_geo, gap))
        cats.push_back(std::move(c));

    // --- bitmaps
    items.clear();
    for (const QString &f : {u"terrain.bmp"_s, u"rivers.bmp"_s}) {
        const auto info = geo_cache.bitmap(mod.p({u"map"_s, f}));
        if (!info) {
            items.push_back(item(u"%1 can't be read"_s.arg(f)));
            continue;
        }
        if (info->w != md.w || info->h != md.h)
            items.push_back(item(u"%1 is %2x%3, provinces.bmp is %4x%5 — they must be the same size"_s.arg(f).arg(info->w).arg(info->h)
                                     .arg(md.w).arg(md.h)));
        if (!info->indexed)
            items.push_back(item(u"%1 is not an 8-bit palette bitmap"_s.arg(f)));
    }
    cats.push_back(cat(u"bitmaps"_s, u"terrain.bmp / rivers.bmp"_s, SEV_ERROR, items));

    // --- production types with their own efficiency goods (unusual: they no longer follow their template)
    items.clear();
    if (const store::StoreP *pp_ = doc.maps.get(u"production"_s))
        if (auto *prod = dynamic_cast<production::ProdEdits *>(pp_->get())) {
            try {
                for (const QString &k : prod->keys()) {
                    const production::Desc d = prod->info(k);
                    if (d.kind == u"factory" && d.has_own_eff && d.tmpl && !d.tmpl->isEmpty()) {
                        QStringList ge;
                        for (const auto &[g, a] : d.own_eff)
                            ge << u"%1 %2"_s.arg(g, py::fmt_g(a));
                        items.push_back(item(u"%1: its own efficiency goods (%2) — the game uses them instead of %3's"_s.arg(
                            k, ge.isEmpty() ? u"none"_s : ge.join(u", "_s), *d.tmpl)));
                    }
                }
            } catch (const std::exception &) {
                items.clear();
            }
        }
    cats.push_back(cat(u"own_eff"_s, u"Factories with their own efficiency goods"_s, SEV_INFO, items, std::nullopt,
                       u"Measured in the game: a production type's own `efficiency` block replaces its template's list (an "
                       u"empty one: none) — it no longer follows the template. Fine when meant; Economy → the factory → "
                       u"\"Back to the template's list\" undoes it."_s,
                       u"production"_s));
    crash_checks(mod, doc, cats);
    return cats;
}

}  // namespace mapcheck
