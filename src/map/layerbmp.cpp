// layerbmp.py — terrain.bmp / rivers.bmp as editable layers, and LandSea. Line by line.
#include "map/layerbmp.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace layerbmp {

namespace {
// int.from_bytes(raw[a:b], "little"[, signed]) — the slice as Python cuts it (shorter at the end of the file)
long long le(const QByteArray &raw, qsizetype a, qsizetype b, bool sign = false)
{
    unsigned long long v = 0;
    int n = 0;
    for (qsizetype i = a; i < b && i < raw.size(); ++i, ++n)
        v |= static_cast<unsigned long long>(static_cast<unsigned char>(raw[i])) << (8 * n);
    if (sign && n > 0 && n < 8 && (v >> (8 * n - 1)) & 1)
        v |= ~0ULL << (8 * n);
    return static_cast<long long>(v);
}
std::uint8_t at(const QByteArray &raw, qsizetype i)   // raw[i] (IndexError past the end)
{
    if (i < 0 || i >= raw.size())
        throw std::out_of_range("index out of range");
    return static_cast<std::uint8_t>(raw[i]);
}
QString sha1(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex()); }
QString oserror(const QString &path)                  // str(OSError) for a file that can't be opened
{
    if (!fs::exists(path))
        return u"[Errno 2] No such file or directory: %1"_s.arg(py::repr_str(path));
    return u"[Errno 13] Permission denied: %1"_s.arg(py::repr_str(path));
}
}  // namespace

IndexedBitmap::IndexedBitmap(moddata::Mod &mod_, const mapview::MapData &md_, const QString &filename_, const QString &kind_)
    : mod(mod_), md(md_), filename(filename_), kind(kind_), path(mod_.p({u"map"_s, filename_}))
{
}

void IndexedBitmap::_notify(const std::vector<std::int64_t> &idx)
{
    stamp += 1;
    if (idx.empty()) {
        stamp_log.emplace_back(stamp, Box{0, 0, 0, 0});
        while (stamp_log.size() > 256)
            stamp_log.pop_front();
        return;
    }
    long long x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (size_t i = 0; i < idx.size(); ++i) {
        const long long y = py::floordiv(static_cast<long long>(idx[i]), md.w), x = py::mod(static_cast<long long>(idx[i]), md.w);
        if (!i) {
            x0 = x1 = x;
            y0 = y1 = y;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    const Box box = {x0, y0, x1 + 1, y1 + 1};
    stamp_log.emplace_back(stamp, box);
    while (stamp_log.size() > 256)
        stamp_log.pop_front();
    for (const auto &fn : listeners)
        fn(box);
}

std::optional<Box> IndexedBitmap::changed_since(long long since) const
{
    if (since == stamp)
        return Box{0, 0, 0, 0};
    if (since > stamp || stamp_log.empty() || stamp_log.front().first > since + 1 || stamp_log.back().first != stamp)
        return std::nullopt;
    long long logged = 0;                        // (every step since must be in the list: a load isn't)
    for (const auto &[s, b] : stamp_log)
        logged += s > since;
    if (logged != stamp - since)
        return std::nullopt;
    Box u{md.w, md.h, 0, 0};
    for (const auto &[s, b] : stamp_log)
        if (s > since && b[2] > b[0] && b[3] > b[1]) {
            u[0] = std::min(u[0], b[0]);
            u[1] = std::min(u[1], b[1]);
            u[2] = std::max(u[2], b[2]);
            u[3] = std::max(u[3], b[3]);
        }
    if (u[2] <= u[0] || u[3] <= u[1])
        return Box{0, 0, 0, 0};
    return u;
}

bool IndexedBitmap::load()
{
    // Read the file (once). Returns False (and sets .error) if it can't be used.
    if (data)
        return true;
    if (error && !error->isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = u"%1 can't be read (%2)"_s.arg(filename, oserror(path));
        return false;
    }
    const QByteArray raw = f.readAll();
    if (raw.left(2) != "BM") {
        error = u"%1 is not a bitmap"_s.arg(filename);
        return false;
    }
    const long long off = le(raw, 10, 14), hsz = le(raw, 14, 18);
    const long long w = le(raw, 18, 22, true), h = le(raw, 22, 26, true);
    const long long bpp = le(raw, 28, 30), comp = le(raw, 30, 34);
    if (bpp != 8 || comp != 0) {
        error = u"%1 is not an uncompressed 8-bit palette bitmap"_s.arg(filename);
        return false;
    }
    if (w != md.w || std::llabs(h) != md.h) {
        error = u"%1 is %2x%3, provinces.bmp is %4x%5"_s.arg(filename).arg(w).arg(std::llabs(h)).arg(md.w).arg(md.h);
        return false;
    }
    long long ncol = le(raw, 46, 50);
    if (!ncol)
        ncol = 256;
    const long long pal0 = 14 + hsz;
    palette.clear();
    for (long long i = 0; i < ncol; ++i)
        palette.emplace_back(at(raw, pal0 + 4 * i + 2), at(raw, pal0 + 4 * i + 1), at(raw, pal0 + 4 * i));
    const long long stride = (w + 3) & ~3LL;
    if (off < 0 || off + stride * md.h > raw.size())
        throw std::runtime_error("buffer is smaller than requested size");   // (np.frombuffer: ValueError)
    grid::Grid<std::uint8_t> g(md.h, w);
    for (long long r = 0; r < md.h; ++r) {
        const long long src = h < 0 ? md.h - 1 - r : r;
        std::memcpy(g.row(r), raw.constData() + off + src * stride, static_cast<size_t>(w));
    }
    data = std::move(g);
    stamp += 1;
    _layout = std::make_tuple(static_cast<qsizetype>(off), static_cast<qsizetype>(stride), h < 0);
    hash = sha1(raw);
    return true;
}

long long IndexedBitmap::color_index(const Rgb &rgb, long long dflt) const
{
    for (size_t i = 0; i < palette.size(); ++i)
        if (palette[i] == rgb)
            return static_cast<long long>(i);
    return dflt;
}

void IndexedBitmap::restore_to(long long v)
{
    std::vector<std::int64_t> touched;
    auto &flat = data->flat();
    while (version > v) {
        const Diff &d = diffs[static_cast<size_t>(version - 1)];
        for (size_t i = 0; i < d.idx.size(); ++i)
            flat[static_cast<size_t>(d.idx[i])] = d.old[i];
        touched.insert(touched.end(), d.idx.begin(), d.idx.end());
        version -= 1;
    }
    while (version < v && version < static_cast<long long>(diffs.size())) {
        const Diff &d = diffs[static_cast<size_t>(version)];
        for (size_t i = 0; i < d.idx.size(); ++i)
            flat[static_cast<size_t>(d.idx[i])] = d.nw[i];
        touched.insert(touched.end(), d.idx.begin(), d.idx.end());
        version += 1;
    }
    if (!touched.empty())
        _notify(touched);
}

QStringList IndexedBitmap::dirty() const { return version != saved ? QStringList{filename} : QStringList(); }

std::optional<Recovery> IndexedBitmap::recovery_state() const
{
    // the pixels changed since the file was read, with their values now
    if (!data || diffs.empty() || version == 0)
        return std::nullopt;
    Recovery r;
    for (long long i = 0; i < version; ++i)
        r.idx.insert(r.idx.end(), diffs[static_cast<size_t>(i)].idx.begin(), diffs[static_cast<size_t>(i)].idx.end());
    std::sort(r.idx.begin(), r.idx.end());
    r.idx.erase(std::unique(r.idx.begin(), r.idx.end()), r.idx.end());
    for (std::int64_t i : r.idx)
        r.vals.push_back(data->flat()[static_cast<size_t>(i)]);
    return r;
}

void IndexedBitmap::recover(const std::optional<Recovery> &state)
{
    if (state)
        apply(state->idx, state->vals);
}

void IndexedBitmap::apply(const std::vector<std::int64_t> &idx0, const std::vector<std::uint8_t> &vals0)
{
    // Set pixels (flat view indexes) to palette values as the next version. Call inside Document.edit.
    if (!load())
        return;
    auto &flat = data->flat();
    Diff d;
    for (size_t i = 0; i < idx0.size(); ++i) {
        const std::uint8_t old = flat[static_cast<size_t>(idx0[i])];   // (old = flat[idx], taken before any is set)
        if (old != vals0[i]) {
            d.idx.push_back(idx0[i]);
            d.old.push_back(old);
            d.nw.push_back(vals0[i]);
        }
    }
    if (d.idx.empty())
        return;
    diffs.erase(diffs.begin() + version, diffs.end());
    if (saved > version)
        saved = -1;
    diffs.push_back(d);
    for (size_t i = 0; i < d.idx.size(); ++i)
        flat[static_cast<size_t>(d.idx[i])] = d.nw[i];
    version += 1;
    _notify(diffs.back().idx);
}

void IndexedBitmap::live(const std::vector<std::int64_t> &idx, const std::vector<std::uint8_t> &vals)
{
    // Write pixels during a stroke (shown at once; the stroke is recorded by apply() when it ends).
    if (!load() || idx.empty())
        return;
    auto &flat = data->flat();
    for (size_t i = 0; i < idx.size(); ++i)
        flat[static_cast<size_t>(idx[i])] = vals[i];
    _notify(idx);
}

void IndexedBitmap::revert_unsaved()
{
    // Discard unsaved changes (scenario switch / revert).
    if (version == saved)
        return;
    if (saved >= 0) {
        restore_to(saved);
    } else {
        data.reset();
        error.reset();
        load();
        if (data)
            _notify({0, static_cast<std::int64_t>(data->flat().size()) - 1});
    }
    diffs.clear();
    version = 0;
    saved = 0;
}

std::optional<QStringList> IndexedBitmap::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    path = mod.adopt(path);                          // (the base game's file: copied into the mod first)
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw fs::OSError(oserror(path).toStdString(), 2);
    const QByteArray orig = f.readAll();
    f.close();
    if (hash && sha1(orig) != *hash)
        throw std::runtime_error(u"map/%1 was changed on disk after it was loaded. Reload before saving."_s.arg(filename).toStdString());
    const auto [off, stride, topdown] = *_layout;
    if (off + stride * md.h > orig.size())
        throw std::runtime_error("buffer is smaller than requested size");
    QByteArray out(static_cast<qsizetype>(stride * md.h), '\0');
    for (long long r = 0; r < md.h; ++r) {
        const long long src = topdown ? md.h - 1 - r : r;   // rows = data[::-1] if topdown else data
        std::memcpy(out.data() + r * stride, data->row(src), static_cast<size_t>(md.w));
        // keep the row padding bytes as they were
        std::memcpy(out.data() + r * stride + md.w, orig.constData() + off + r * stride, static_cast<size_t>(stride - md.w));
    }
    const QByteArray bytes = orig.left(off) + out + orig.mid(off + stride * md.h);
    const QString stamp_s = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);
    const QString bdir = pypath::join(backup_root, {u"map"_s, stamp_s});
    fs::makedirs(bdir);
    fs::copy2(path, pypath::join(bdir, filename));
    const QString tmp = path + u".modeditor_tmp"_s;
    {
        QFile t(fs::watched(tmp));
        if (!t.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw fs::OSError(oserror(tmp).toStdString(), 13);
        t.write(bytes);
    }
    fs::replace(tmp, path);
    hash = sha1(bytes);
    saved = version;
    return QStringList{path};
}

// ---------------------------------------------------------------------------------------------- land / sea
OrderedMap<long long, QString> terrain_types(const moddata::Mod &mod)
{
    // {palette index: terrain type} from map/terrain.txt (the `categories` block is skipped)
    OrderedMap<long long, QString> out;
    script::Nodes nodes;
    try {
        nodes = script::parse(script::read_text(mod.p({u"map"_s, u"terrain.txt"_s})));
    } catch (const fs::OSError &) {
        return out;
    }
    for (const script::Node &n : nodes) {
        if (!(n.has_key && !n.key.isEmpty() && n.is_block) || n.key == u"categories")
            continue;
        std::optional<QString> t;
        for (const script::Node &c : n.kids)
            if (c.has_key && c.key == u"type" && !c.is_block) {
                t = c.value;
                break;
            }
        const script::Node *col = nullptr;
        for (const script::Node &c : n.kids)
            if (c.has_key && c.key == u"color" && c.is_block) {
                col = &c;
                break;
            }
        if (t && !t->isEmpty() && col)
            for (const script::Node &c : col->kids)
                if (!c.has_key && !c.is_block && py::isdigit(c.value)) {
                    const long long i = py::to_int(c.value);
                    if (!out.contains(i))
                        out.insert(i, *t);
                }
    }
    return out;
}

std::pair<bool, bool> LandSea::_prepare()
{
    if (_ready)
        return *_ready;
    const bool ok_t = terrain.load(), ok_r = rivers.load();
    const auto &idm = md.idmap.flat();
    auto sea_px = [&](size_t i) { return md.sea[idm[i]] && idm[i] != 0; };
    auto land_px = [&](size_t i) { return !md.sea[idm[i]]; };
    if (ok_t) {
        const auto types = terrain_types(mod);
        const auto &T = terrain.data->flat();
        std::vector<long long> used(256, 0), sea_vals(256, 0);
        for (size_t i = 0; i < T.size(); ++i) {
            if (land_px(i))
                used[T[i]] += 1;
            if (sea_px(i))
                sea_vals[T[i]] += 1;
        }
        std::vector<long long> ocean_l;
        for (const auto &[i, t] : types)
            if (t == u"ocean")
                ocean_l.push_back(i);
        std::sort(ocean_l.begin(), ocean_l.end());
        if (ocean_l.empty())
            ocean_l = {254, 255};
        ocean.assign(256, 0);
        for (long long i : ocean_l) {
            if (i < -256 || i >= 256)
                throw std::out_of_range("index out of bounds");   // (numpy: IndexError)
            ocean[static_cast<size_t>(i < 0 ? i + 256 : i)] = 1;
        }
        // max(ocean, key=sea_vals[i] if i < 256 else -1): the first of the highest
        long long best = ocean_l[0], bv = ocean_l[0] < 256 ? sea_vals[static_cast<size_t>(ocean_l[0])] : -1;
        for (long long i : ocean_l) {
            const long long v = i < 256 ? sea_vals[static_cast<size_t>(i)] : -1;
            if (v > bv) {
                best = i;
                bv = v;
            }
        }
        ocean_fill = best;
        std::vector<long long> pl;
        for (const auto &[i, t] : types)
            if (t == u"plains" && i < static_cast<long long>(terrain.palette.size()) && used[static_cast<size_t>(i)] > 0)
                pl.push_back(i);
        auto redness = [&](long long i) {
            const auto &[r, g, b] = terrain.palette[static_cast<size_t>(i)];
            return std::make_pair(static_cast<long long>(r - std::max(g, b)), used[static_cast<size_t>(i)]);
        };
        if (!pl.empty()) {
            long long b = pl[0];
            auto bk = redness(b);
            for (long long i : pl)
                if (redness(i) > bk) {
                    b = i;
                    bk = redness(i);
                }
            plains = b;
        } else {
            plains.reset();
        }
    }
    if (ok_r) {
        const auto &R = rivers.data->flat();
        std::vector<long long> cnt(256, 0);
        for (size_t i = 0; i < R.size(); ++i)
            if (sea_px(i))
                cnt[R[i]] += 1;
        river_sea = std::max_element(cnt.begin(), cnt.end()) - cnt.begin();   // np.argmax: the first of the highest
        river_none = rivers.color_index({255, 255, 255}, 255);
    }
    _ready = std::make_pair(ok_t && plains.has_value(), ok_r);
    return *_ready;
}

std::vector<LandSea::Change> LandSea::changes(const std::vector<std::int64_t> &idx, const std::vector<long long> &nw)
{
    // [(bitmap, flat indexes, values)] needed when pixels `idx` become province ids `new`
    const auto [ok_t, ok_r] = _prepare();
    std::vector<Change> out;
    auto land = [&](size_t i) { return nw[i] != 0 && !md.sea[static_cast<size_t>(nw[i])]; };
    auto sea = [&](size_t i) { return nw[i] != 0 && md.sea[static_cast<size_t>(nw[i])]; };
    if (ok_t) {
        const auto &flat = terrain.data->flat();
        std::vector<std::int64_t> to_land, to_sea;
        for (size_t i = 0; i < idx.size(); ++i) {
            const std::uint8_t tv = flat[static_cast<size_t>(idx[i])];
            if (land(i) && ocean[tv])
                to_land.push_back(idx[i]);
            if (sea(i) && !ocean[tv])
                to_sea.push_back(idx[i]);
        }
        if (!to_land.empty() || !to_sea.empty()) {
            Change c{&terrain, to_land, std::vector<std::uint8_t>(to_land.size(), static_cast<std::uint8_t>(*plains))};
            c.idx.insert(c.idx.end(), to_sea.begin(), to_sea.end());
            c.vals.insert(c.vals.end(), to_sea.size(), static_cast<std::uint8_t>(ocean_fill));
            out.push_back(std::move(c));
        }
    }
    if (ok_r) {
        const auto &flat = rivers.data->flat();
        std::vector<std::int64_t> to_land, to_sea;
        for (size_t i = 0; i < idx.size(); ++i) {
            const long long rv = flat[static_cast<size_t>(idx[i])];
            if (land(i) && rv == river_sea)
                to_land.push_back(idx[i]);
            if (sea(i) && rv != river_sea)
                to_sea.push_back(idx[i]);
        }
        if (!to_land.empty() || !to_sea.empty()) {
            Change c{&rivers, to_land, std::vector<std::uint8_t>(to_land.size(), static_cast<std::uint8_t>(river_none))};
            c.idx.insert(c.idx.end(), to_sea.begin(), to_sea.end());
            c.vals.insert(c.vals.end(), to_sea.size(), static_cast<std::uint8_t>(river_sea));
            out.push_back(std::move(c));
        }
    }
    return out;
}

}  // namespace layerbmp
