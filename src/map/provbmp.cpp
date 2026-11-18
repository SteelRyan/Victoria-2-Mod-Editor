// provbmp.py — provinces.bmp as an editable layer, and its region helpers. Line by line.
#include "map/provbmp.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/mapcheck.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace provbmp {

namespace {
long long le(const QByteArray &raw, qsizetype a, qsizetype b, bool sign = false)   // int.from_bytes(raw[a:b], "little")
{
    unsigned long long v = 0;
    int n = 0;
    for (qsizetype i = a; i < b && i < raw.size(); ++i, ++n)
        v |= static_cast<unsigned long long>(static_cast<unsigned char>(raw[i])) << (8 * n);
    if (sign && n > 0 && n < 8 && (v >> (8 * n - 1)) & 1)
        v |= ~0ULL << (8 * n);
    return static_cast<long long>(v);
}
QString sha1(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex()); }
QString oserror(const QString &path)
{
    if (!fs::exists(path))
        return u"[Errno 2] No such file or directory: %1"_s.arg(py::repr_str(path));
    return u"[Errno 13] Permission denied: %1"_s.arg(py::repr_str(path));
}
}  // namespace

QString file_hash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw fs::OSError(oserror(path).toStdString(), 2);
    QCryptographicHash h(QCryptographicHash::Sha1);
    while (!f.atEnd())
        h.addData(f.read(1 << 22));
    return QString::fromLatin1(h.result().toHex());
}

ProvinceBitmap::ProvinceBitmap(moddata::Mod &mod_, mapview::MapData &md_)
    : mod(mod_), md(md_), path(mod_.p({u"map"_s, u"provinces.bmp"_s}))
{
}

void ProvinceBitmap::restore_to(long long v)
{
    std::optional<Box> box;
    Ids ids;
    while (version > v) {
        const Diff d = diffs[static_cast<size_t>(version - 1)];
        const auto [b, i] = _write(d.idx, d.old);
        box = _union(box, b);
        ids.insert(i.begin(), i.end());
        version -= 1;
    }
    while (version < v && version < static_cast<long long>(diffs.size())) {
        const Diff d = diffs[static_cast<size_t>(version)];
        const auto [b, i] = _write(d.idx, d.nw);
        box = _union(box, b);
        ids.insert(i.begin(), i.end());
        version += 1;
    }
    if (box)
        _changed(*box, ids);
}

QStringList ProvinceBitmap::dirty() const { return version != saved ? QStringList{u"provinces.bmp"_s} : QStringList(); }

std::optional<Recovery> ProvinceBitmap::recovery_state() const
{
    // the pixels changed since the file was read, with their ids now
    if (diffs.empty() || version == 0)
        return std::nullopt;
    Recovery r;
    for (long long i = 0; i < version; ++i)
        r.idx.insert(r.idx.end(), diffs[static_cast<size_t>(i)].idx.begin(), diffs[static_cast<size_t>(i)].idx.end());
    std::sort(r.idx.begin(), r.idx.end());
    r.idx.erase(std::unique(r.idx.begin(), r.idx.end()), r.idx.end());
    for (std::int64_t i : r.idx)
        r.vals.push_back(md.idmap.flat()[static_cast<size_t>(i)]);
    return r;
}

void ProvinceBitmap::recover(const std::optional<Recovery> &state)
{
    if (!state)
        return;
    std::vector<std::uint16_t> now;
    for (std::int64_t i : state->idx)
        now.push_back(md.idmap.flat()[static_cast<size_t>(i)]);
    commit(state->idx, now, state->vals);
}

std::pair<Box, Ids> ProvinceBitmap::_write(const std::vector<std::int64_t> &idx, const std::vector<std::uint16_t> &ids)
{
    // Write ids to pixels (no undo, no derived update). Returns (box, affected province ids).
    auto &flat = md.idmap.flat();
    Ids aff;
    for (std::int64_t i : idx)
        aff.insert(flat[static_cast<size_t>(i)]);   // (before)
    for (size_t k = 0; k < idx.size(); ++k)
        flat[static_cast<size_t>(idx[k])] = ids[k];
    for (std::uint16_t v : ids)
        aff.insert(v);
    long long x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (size_t k = 0; k < idx.size(); ++k) {
        const long long y = py::floordiv(static_cast<long long>(idx[k]), md.w), x = py::mod(static_cast<long long>(idx[k]), md.w);
        if (!k) {
            x0 = x1 = x;
            y0 = y1 = y;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    if (idx.empty())
        throw std::invalid_argument("zero-size array to reduction operation minimum which has no identity");
    return {Box{x0, y0, x1 + 1, y1 + 1}, aff};
}

std::optional<Box> ProvinceBitmap::paint_live(const std::vector<std::int64_t> &idx, long long new_id)
{
    // Change pixels during a stroke (shown at once, recorded by commit() at the end). -> box or None.
    if (idx.empty())
        return std::nullopt;
    const auto [box, _ids] = _write(idx, std::vector<std::uint16_t>(idx.size(), static_cast<std::uint16_t>(new_id)));
    md.refresh_masks(box);
    return box;
}

std::optional<Box> ProvinceBitmap::commit(const std::vector<std::int64_t> &idx0, const std::vector<std::uint16_t> &old0,
                                          const std::vector<std::uint16_t> &nw0)
{
    // Record a finished change (pixels `idx` go from `old` to `new`) as the next version and update everything
    // derived from the map. Call it inside Document.edit(..., lists=["provinces_bmp"]).
    Diff d;
    for (size_t i = 0; i < idx0.size(); ++i)
        if (old0[i] != nw0[i]) {
            d.idx.push_back(idx0[i]);
            d.old.push_back(old0[i]);
            d.nw.push_back(nw0[i]);
        }
    if (d.idx.empty())
        return std::nullopt;
    diffs.erase(diffs.begin() + version, diffs.end());
    if (saved > version)
        saved = -1;                              // the saved state can't be reached by undo any more
    diffs.push_back(d);
    const auto [box, ids] = _write(d.idx, d.nw);
    version += 1;
    _changed(box, ids);
    return box;
}

void ProvinceBitmap::_changed(const Box &box, const Ids &ids)
{
    md.refresh_masks(box);
    md.refresh_ids(std::vector<long long>(ids.begin(), ids.end()), box);
    for (const auto &fn : listeners)
        fn(box, ids);
}

std::optional<QStringList> ProvinceBitmap::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    path = mod.adopt(path);                      // (the base game's file: copied into the mod first)
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw fs::OSError(oserror(path).toStdString(), 2);
    const QByteArray orig = f.readAll();
    f.close();
    if (hash && sha1(orig) != *hash)
        throw std::runtime_error("map/provinces.bmp was changed on disk after it was loaded. Reload before saving.");
    const QByteArray data = encode(orig);
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    const QString bdir = pypath::join(backup_root, {u"map"_s, stamp});
    fs::makedirs(bdir);
    fs::copy2(path, pypath::join(bdir, u"provinces.bmp"_s));
    const QString tmp = path + u".modeditor_tmp"_s;
    {
        QFile t(fs::watched(tmp));
        if (!t.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw fs::OSError(oserror(tmp).toStdString(), 13);
        t.write(data);
    }
    fs::replace(tmp, path);
    hash = sha1(data);
    saved = version;
    return QStringList{path};
}

void ProvinceBitmap::reload_file()
{
    // Put every pixel back to what provinces.bmp on disk says (used when discarding unsaved painting that undo
    // can't reach any more).
    const QImage img = QImage(path).convertToFormat(QImage::Format_RGB888);
    std::vector<std::uint16_t> lut(1u << 24, 0);
    for (const auto &[pid, pr] : mod.provinces) {
        const auto &[r, g, b] = pr.rgb;
        lut[(static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b)] =
            static_cast<std::uint16_t>(pid);
    }
    std::vector<std::int64_t> idx;
    std::vector<std::uint16_t> vals;
    for (long long y = 0; y < md.h; ++y) {
        const uchar *row = img.constScanLine(static_cast<int>(md.h - 1 - y));
        for (long long x = 0; x < md.w; ++x) {
            const uchar *p = row + x * 3;
            const std::uint16_t disk = lut[(static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2]];
            if (disk != md.idmap(y, x)) {
                idx.push_back(y * md.w + x);
                vals.push_back(disk);
            }
        }
    }
    if (!idx.empty()) {
        const auto [box, ids] = _write(idx, vals);
        _changed(box, ids);
    }
    diffs.clear();
    version = 0;
    saved = 0;
}

void ProvinceBitmap::remember_file()
{
    // Take the file's hash (call once before the first edit, to detect outside changes on save).
    if (!hash && fs::isfile(path))
        hash = file_hash(path);
}

QByteArray ProvinceBitmap::encode(const QByteArray &orig) const
{
    // The whole file for the current pixels: the original header + 24-bit BGR rows.
    const long long off = le(orig, 10, 14);
    const long long w = le(orig, 18, 22, true), h = le(orig, 22, 26, true);
    const long long bpp = le(orig, 28, 30), comp = le(orig, 30, 34);
    if (bpp != 24 || comp != 0 || std::llabs(h) != md.h || w != md.w)
        throw std::runtime_error("provinces.bmp is not a plain 24-bit bitmap of the expected size — not saved.");
    std::vector<std::array<std::uint8_t, 3>> lut(static_cast<size_t>(md.n), {0, 0, 0});
    for (const auto &[pid, pr] : mod.provinces)
        if (0 < pid && pid < md.n) {
            const auto &[r, g, b] = pr.rgb;
            lut[static_cast<size_t>(pid)] = {static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(r)};
        }
    const long long stride = (w * 3 + 3) & ~3LL;
    if (off < 0 || off + stride * md.h > orig.size())
        throw std::runtime_error("buffer is smaller than requested size");   // (np.frombuffer: ValueError)
    // rows = lut[idmap] (view rows == file rows); a top-down BMP: rows the other way round
    auto rowsrc = [&](long long r) { return h < 0 ? md.h - 1 - r : r; };   // file row r shows view row rowsrc(r)
    QByteArray out(static_cast<qsizetype>(stride * md.h), '\0');
    for (long long r = 0; r < md.h; ++r) {
        const std::uint16_t *ids = md.idmap.row(rowsrc(r));
        uchar *o = reinterpret_cast<uchar *>(out.data()) + r * stride;
        for (long long x = 0; x < w; ++x)
            std::memcpy(o + x * 3, lut[ids[x]].data(), 3);
    }
    // pixels of no province: ones that were like that in the file keep their colour; ones that were erased in the
    // editor (their file colour still belongs to a province) become black. (zs = src[zero] in VIEW order; rows[zero]
    // = zs writes them at the same view positions into the rows as they stand — for a top-down file those are the
    // file's rows: reproduced as Python does it)
    bool any_zero = false;
    for (std::uint16_t v : md.idmap.flat())
        if (v == 0) {
            any_zero = true;
            break;
        }
    if (any_zero) {
        std::vector<std::uint8_t> known(1u << 24, 0);
        for (const auto &[pid, pr] : mod.provinces) {
            const auto &[r, g, b] = pr.rgb;
            known[(static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b)] = 1;
        }
        const uchar *body = reinterpret_cast<const uchar *>(orig.constData()) + off;
        for (long long y = 0; y < md.h; ++y)
            for (long long x = 0; x < md.w; ++x) {
                if (md.idmap(y, x) != 0)
                    continue;
                const long long sr = h < 0 ? md.h - 1 - y : y;     // src = body (… [::-1] when top-down): view row y
                const uchar *s = body + sr * stride + x * 3;
                std::uint8_t px[3] = {s[0], s[1], s[2]};
                const std::uint32_t key = (static_cast<std::uint32_t>(px[2]) << 16) | (static_cast<std::uint32_t>(px[1]) << 8) | px[0];
                if (known[key])
                    px[0] = px[1] = px[2] = 0;
                std::memcpy(out.data() + y * stride + x * 3, px, 3);   // rows[zero] = zs: at row y of `rows`
            }
    }
    return orig.left(off) + out + orig.mid(off + stride * md.h);
}

std::optional<Box> _union(const std::optional<Box> &a, const std::optional<Box> &b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return Box{std::min((*a)[0], (*b)[0]), std::min((*a)[1], (*b)[1]), std::max((*a)[2], (*b)[2]), std::max((*a)[3], (*b)[3])};
}

// ---------------------------------------------------------------------------------------------- regions
grid::Grid<std::uint8_t> component(const grid::Grid<std::uint8_t> &mask, long long y, long long x)
{
    // The 4-connected part of `mask` that contains (y, x), as a bool array (runs + union-find).
    const long long H = mask.h(), W = mask.w();
    grid::Grid<std::uint8_t> out(H, W, 0);
    if (!mask(y, x))
        return out;
    const long long N = H * W;
    std::vector<std::uint8_t> start(static_cast<size_t>(N), 0);
    for (long long r = 0; r < H; ++r)
        for (long long c = 0; c < W; ++c)
            start[static_cast<size_t>(r * W + c)] = c == 0 ? mask(r, 0) : (mask(r, c) && !mask(r, c - 1));
    std::vector<std::int64_t> run_of(static_cast<size_t>(N));
    std::int64_t cs = 0, R = 0;
    for (long long i = 0; i < N; ++i) {
        cs += start[static_cast<size_t>(i)];
        run_of[static_cast<size_t>(i)] = cs - 1;
        R += start[static_cast<size_t>(i)];
    }
    for (long long i = 0; i < N; ++i)
        if (!mask.flat()[static_cast<size_t>(i)])
            run_of[static_cast<size_t>(i)] = -1;
    std::vector<std::int64_t> e;
    for (long long r = 0; r + 1 < H; ++r)
        for (long long c = 0; c < W; ++c) {
            const size_t up = static_cast<size_t>(r * W + c), dn = static_cast<size_t>((r + 1) * W + c);
            if (mask.flat()[dn] && mask.flat()[up] && (start[dn] || start[up]))
                e.push_back(run_of[up] * R + run_of[dn]);
        }
    std::sort(e.begin(), e.end());
    e.erase(std::unique(e.begin(), e.end()), e.end());
    std::vector<std::int64_t> parent(static_cast<size_t>(R));
    for (std::int64_t i = 0; i < R; ++i)
        parent[static_cast<size_t>(i)] = i;
    auto find = [&](std::int64_t q) {
        std::int64_t r = q;
        while (parent[static_cast<size_t>(r)] != r)
            r = parent[static_cast<size_t>(r)];
        while (parent[static_cast<size_t>(q)] != r) {
            const std::int64_t nq = parent[static_cast<size_t>(q)];
            parent[static_cast<size_t>(q)] = r;
            q = nq;
        }
        return r;
    };
    for (std::int64_t k : e) {
        const std::int64_t ru = find(k / R), rv = find(k % R);
        if (ru != rv)
            parent[static_cast<size_t>(std::max(ru, rv))] = std::min(ru, rv);
    }
    const std::int64_t root = find(run_of[static_cast<size_t>(y * W + x)]);
    for (long long i = 0; i < N; ++i)
        if (mask.flat()[static_cast<size_t>(i)] && find(run_of[static_cast<size_t>(i)]) == root)
            out.flat()[static_cast<size_t>(i)] = 1;
    return out;
}

std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> small_pieces(const grid::Grid<std::uint16_t> &idm, long long limit,
                                                                            std::optional<long long> only_id,
                                                                            const mapcheck::Pieces *pieces)
{
    const long long H = idm.h(), W = idm.w();
    std::optional<mapcheck::Pieces> own;
    if (!pieces) {
        grid::Grid<std::int32_t> idm32(H, W);
        for (size_t i = 0; i < idm.flat().size(); ++i)
            idm32.flat()[i] = idm.flat()[i];
        own = mapcheck::province_pieces(idm32);
    }
    const mapcheck::Pieces &pc = pieces ? *pieces : *own;
    OrderedMap<std::int64_t, long long> per;     // Counter(ppid)
    for (std::int64_t p : pc.pid)
        per[p] += 1;
    std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> out;
    for (size_t k = 0; k < pc.pid.size(); ++k) {
        const std::int64_t p = pc.pid[k], s = pc.size[k], rep = pc.first[k];
        if (s >= limit)
            continue;
        if (!only_id) {
            if (p == 0 || per.value(p) < 2)
                continue;
        } else if (p != *only_id) {
            continue;
        }
        const long long y = rep / W, x = rep % W;
        const long long y0 = std::max(0LL, y - limit), y1 = std::min(H, y + limit + 1);
        const long long x0 = std::max(0LL, x - limit), x1 = std::min(W, x + limit + 1);
        // the 4-connected part of p inside that window that holds (y, x) — (C++ only, 2026-11-14) a flood fill over its
        // own few pixels instead of a union-find over the whole window (twice per stroke for every small piece of the
        // map: 30-40 ms on ZRM's Legacy); the same pixels, sorted into the window's row order as before
        const long long sh = y1 - y0, sw = x1 - x0;
        std::vector<std::int64_t> ys, xs, idx;
        if (std::int64_t(idm(y, x)) == p) {
            std::vector<std::uint8_t> seen(static_cast<size_t>(sh * sw), 0);
            std::vector<std::int64_t> todo{(y - y0) * sw + (x - x0)};
            seen[static_cast<size_t>(todo[0])] = 1;
            while (!todo.empty()) {
                const std::int64_t q = todo.back();
                todo.pop_back();
                const long long r = q / sw, c = q % sw;
                idx.push_back((r + y0) * W + (c + x0));
                const long long nr[4] = {r, r, r + 1, r - 1}, nc[4] = {c + 1, c - 1, c, c};
                for (int d = 0; d < 4; ++d)
                    if (0 <= nr[d] && nr[d] < sh && 0 <= nc[d] && nc[d] < sw) {
                        const std::int64_t nq = nr[d] * sw + nc[d];
                        if (!seen[static_cast<size_t>(nq)] && std::int64_t(idm(nr[d] + y0, nc[d] + x0)) == p) {
                            seen[static_cast<size_t>(nq)] = 1;
                            todo.push_back(nq);
                        }
                    }
            }
            std::sort(idx.begin(), idx.end());
            for (std::int64_t f : idx) {
                ys.push_back(f / W - y0);
                xs.push_back(f % W - x0);
            }
        }
        // neighbours of the piece (4-neighbour, not the piece's own id, not "no province")
        OrderedMap<std::int64_t, long long> nb;
        bool any = false;
        for (const auto &[dy, dx] : {std::pair<int, int>{0, 1}, {0, -1}, {1, 0}, {-1, 0}})
            for (size_t i = 0; i < ys.size(); ++i) {
                const long long yy = ys[i] + y0 + dy, xx = py::mod(xs[i] + x0 + dx, W);
                if (yy < 0 || yy >= H)
                    continue;
                const std::int64_t v = idm.flat()[static_cast<size_t>(yy * W + xx)];
                if (v != p && v != 0) {
                    nb[v] += 1;
                    any = true;
                }
            }
        if (any) {
            // Counter(nb).most_common(1): the highest count, the first counted among equals
            std::int64_t best = nb.begin()->first;
            long long bc = nb.begin()->second;
            for (const auto &[v, c] : nb)
                if (c > bc) {
                    best = v;
                    bc = c;
                }
            out.emplace_back(idx, best);
        }
    }
    return out;
}

}  // namespace provbmp
