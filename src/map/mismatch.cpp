// mismatch.py — line by line.
#include "map/mismatch.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace mismatch {

const std::vector<Kind> KINDS = {
    {u"t_sea"_s, u"Terrain on a sea tile (terrain.bmp says land, the province is sea)"_s, {255, 140, 0}, u"terrain"_s},
    {u"t_land"_s, u"Land without terrain (a land province, ocean in terrain.bmp)"_s, {255, 255, 0}, u"terrain"_s},
    {u"r_water"_s, u"Water in rivers.bmp under land (land in both other maps)"_s, {255, 70, 200}, u"rivers"_s},
    {u"r_sea"_s, u"Land or a river in rivers.bmp on a sea tile"_s, {110, 255, 140}, u"rivers"_s},
};
Rgb colour_of(const QString &kind)
{
    for (const Kind &k : KINDS)
        if (k.key == kind)
            return k.colour;
    return {0, 0, 0};
}
QString text_of(const QString &kind)
{
    for (const Kind &k : KINDS)
        if (k.key == kind)
            return k.text;
    return {};
}
const OrderedMap<QString, QStringList> OF_LAYER = {{u"terrain"_s, {u"t_sea"_s, u"t_land"_s}},
                                                   {u"rivers"_s, {u"r_water"_s, u"r_sea"_s}},
                                                   {u"prov"_s, {u"t_sea"_s, u"t_land"_s, u"r_water"_s, u"r_sea"_s}}};

std::pair<bool, bool> Mismatch::ready()
{
    // (terrain ok, rivers ok) — LandSea knows the ocean indexes, the water value, plains and land.
    if (!ls)
        return {false, false};
    return ls->_prepare();
}

Masks Mismatch::_masks(const std::vector<std::uint16_t> &ids, const std::vector<std::uint8_t> *T,
                       const std::vector<std::uint8_t> *R, long long h, long long w, bool ok_t, bool ok_r) const
{
    Masks out;
    const size_t n = ids.size();
    auto grid_of = [&](const std::function<bool(size_t)> &f) {
        grid::Grid<std::uint8_t> g(h, w, 0);
        for (size_t i = 0; i < n; ++i)
            g.flat()[i] = f(i);
        return g;
    };
    auto sea = [&](size_t i) { return ids[i] != 0 && md.sea[ids[i]]; };
    auto land = [&](size_t i) { return ids[i] != 0 && !md.sea[ids[i]]; };
    if (ok_t) {
        out.insert(u"t_sea"_s, grid_of([&](size_t i) { return sea(i) && !ls->ocean[(*T)[i]]; }));
        out.insert(u"t_land"_s, grid_of([&](size_t i) { return land(i) && bool(ls->ocean[(*T)[i]]); }));
    }
    if (ok_r) {
        out.insert(u"r_water"_s, grid_of([&](size_t i) {
                       return land(i) && (*R)[i] == ls->river_sea && (!ok_t || !ls->ocean[(*T)[i]]);
                   }));
        out.insert(u"r_sea"_s, grid_of([&](size_t i) { return sea(i) && (*R)[i] != ls->river_sea; }));
    }
    return out;
}

// the four kinds at one pixel (bit 0 t_sea, 1 t_land, 2 r_water, 3 r_sea) — _masks' rules, without the per-pixel
// function calls (C++ only, 2026-11-10, the performance round: the whole map took 85 ms after every stroke)
static inline unsigned kinds_at(std::uint16_t id, std::uint8_t t, std::uint8_t r, const std::vector<std::uint8_t> &sea,
                                const layerbmp::LandSea &ls, bool ok_t, bool ok_r)
{
    if (!id)
        return 0;
    const bool s = sea[id];
    unsigned k = 0;
    if (ok_t) {
        const bool oc = ls.ocean[t];
        k |= (s && !oc) ? 1u : 0u;
        k |= (!s && oc) ? 2u : 0u;
    }
    if (ok_r) {
        k |= (!s && r == ls.river_sea && (!ok_t || !ls.ocean[t])) ? 4u : 0u;
        k |= (s && r != ls.river_sea) ? 8u : 0u;
    }
    return k;
}

Masks Mismatch::window(long long y0, long long y1, long long x0, long long x1)
{
    const auto [ok_t, ok_r] = ready();
    y1 = std::min(y1, md.h);
    x1 = std::min(x1, md.w);
    const long long h = std::max(0LL, y1 - y0), w = std::max(0LL, x1 - x0);
    grid::Grid<std::uint8_t> g[4];
    const bool on[4] = {ok_t, ok_t, ok_r, ok_r};
    for (int k = 0; k < 4; ++k)
        if (on[k])
            g[k] = grid::Grid<std::uint8_t>(h, w, 0);
    for (long long y = y0; (ok_t || ok_r) && y < y0 + h; ++y) {
        const std::uint16_t *ir = md.idmap.row(y);
        const std::uint8_t *tr = ok_t ? ls->terrain.data->row(y) : nullptr;
        const std::uint8_t *rr = ok_r ? ls->rivers.data->row(y) : nullptr;
        for (long long x = x0; x < x0 + w; ++x) {
            const unsigned k = kinds_at(ir[x], tr ? tr[x] : 0, rr ? rr[x] : 0, md.sea, *ls, ok_t, ok_r);
            if (k)
                for (int b = 0; b < 4; ++b)
                    if (k & (1u << b))
                        g[b](y - y0, x - x0) = 1;
        }
    }
    Masks out;
    static const QString KEYS[4] = {u"t_sea"_s, u"t_land"_s, u"r_water"_s, u"r_sea"_s};
    for (int k = 0; k < 4; ++k)
        if (on[k])
            out.insert(KEYS[k], std::move(g[k]));
    return out;
}

Masks Mismatch::at(const Flat &idx)
{
    const auto [ok_t, ok_r] = ready();
    std::vector<std::uint16_t> ids;
    std::vector<std::uint8_t> T, R;
    for (std::int64_t i : idx) {
        ids.push_back(md.idmap.flat()[size_t(i)]);
        if (ok_t)
            T.push_back(ls->terrain.data->flat()[size_t(i)]);
        if (ok_r)
            R.push_back(ls->rivers.data->flat()[size_t(i)]);
    }
    return _masks(ids, ok_t ? &T : nullptr, ok_r ? &R : nullptr, 1, static_cast<long long>(idx.size()), ok_t, ok_r);
}

std::vector<long long> Mismatch::_sig() const
{
    // (an undo and a new edit can reach the same version number: the bitmaps' stamps go up with every change)
    std::vector<long long> s = {md.version};
    for (const layerbmp::IndexedBitmap *b : {ls ? &ls->terrain : nullptr, ls ? &ls->rivers : nullptr}) {
        s.push_back(b ? b->stamp : -1);
        s.push_back(b && b->data ? static_cast<long long>(reinterpret_cast<std::intptr_t>(&*b->data)) : 0);
    }
    return s;
}

const OrderedMap<QString, Flat> &Mismatch::whole()
{
    // {kind: flat indexes} of the whole map (kept until a map changes).
    const auto sig = _sig();
    if (!_counts || _counts->first != sig) {
        // one pass over the map straight into the four lists (the same order: flat index ascending)
        const auto [ok_t, ok_r] = ready();
        Flat f[4];
        for (long long y = 0; (ok_t || ok_r) && y < md.h; ++y) {
            const std::uint16_t *ir = md.idmap.row(y);
            const std::uint8_t *tr = ok_t ? ls->terrain.data->row(y) : nullptr;
            const std::uint8_t *rr = ok_r ? ls->rivers.data->row(y) : nullptr;
            for (long long x = 0; x < md.w; ++x)
                if (const unsigned k = kinds_at(ir[x], tr ? tr[x] : 0, rr ? rr[x] : 0, md.sea, *ls, ok_t, ok_r))
                    for (int b = 0; b < 4; ++b)
                        if (k & (1u << b))
                            f[b].push_back(y * md.w + x);
        }
        OrderedMap<QString, Flat> fl;
        static const QString KEYS[4] = {u"t_sea"_s, u"t_land"_s, u"r_water"_s, u"r_sea"_s};
        for (int k = 0; k < 4; ++k)
            if ((k < 2 && ok_t) || (k >= 2 && ok_r))
                fl.insert(KEYS[k], std::move(f[k]));
        _counts = std::make_pair(sig, std::move(fl));
    }
    return _counts->second;
}

OrderedMap<QString, long long> Mismatch::counts()
{
    OrderedMap<QString, long long> out;
    for (const auto &[k, v] : whole())
        out.insert(k, static_cast<long long>(v.size()));
    return out;
}

std::optional<QString> Mismatch::kind_at(long long x, long long y)
{
    // The kind of mismatch at a pixel (or None).
    if (!(0 <= x && x < md.w && 0 <= y && y < md.h))
        return std::nullopt;
    for (const auto &[k, v] : at({y * md.w + x}))
        if (v.flat()[0])
            return k;
    return std::nullopt;
}

QString Mismatch::describe(long long x, long long y)
{
    const auto k = kind_at(x, y);
    return k ? u"  ·  ⚠ "_s + text_of(*k) : QString();
}

std::vector<std::uint8_t> Mismatch::fix_values(const QString &kind, long long n, std::optional<long long> land_value) const
{
    // The values that make `n` pixels of a kind right (in the bitmap that kind is about).
    long long v;
    if (kind == u"t_sea")
        v = ls->ocean_fill;
    else if (kind == u"t_land")
        v = land_value ? *land_value : ls->plains.value_or(0);
    else if (kind == u"r_water")
        v = ls->river_none;
    else if (kind == u"r_sea")
        v = ls->river_sea;
    else
        throw std::out_of_range(kind.toStdString());
    return std::vector<std::uint8_t>(size_t(n), std::uint8_t(v));   // (np.full(…, np.uint8): the low byte)
}

layerbmp::IndexedBitmap *Mismatch::bitmap(const QString &kind) const
{
    return kind.startsWith(u"t_") ? &ls->terrain : &ls->rivers;
}

std::vector<Mismatch::Fix> Mismatch::fix(const QStringList &kinds, const std::optional<Flat> &idx,
                                         std::optional<long long> land_value)
{
    // [(bitmap, flat indexes, values)] that make the pixels right — every pixel of the kinds, or only those of idx.
    std::vector<Fix> out;
    if (!idx) {
        const auto &w = whole();
        for (const QString &k : kinds)
            if (const Flat *i = w.get(k); i && !i->empty())
                out.push_back({bitmap(k), *i, fix_values(k, static_cast<long long>(i->size()), land_value)});
        return out;
    }
    const Masks m = at(*idx);
    for (const QString &k : kinds) {
        const auto *g = m.get(k);
        if (!g)
            continue;
        Flat sel;
        for (size_t j = 0; j < idx->size(); ++j)
            if (g->flat()[j])
                sel.push_back((*idx)[j]);
        if (!sel.empty())
            out.push_back({bitmap(k), sel, fix_values(k, static_cast<long long>(sel.size()), land_value)});
    }
    return out;
}

std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> overlay(Mismatch &mm, const QStringList &kinds, long long y0,
                                                              long long y1, long long x0, long long x1)
{
    // [(mask, rgb)] of the mismatches of `kinds` in a window — the map paints them over its border shading.
    auto key = mm._overlay_key(y0, y1, x0, x1);
    if (mm._overlay && mm._overlay->key == key && mm._overlay->kinds == kinds)
        return mm._overlay->out;
    const Masks m = mm.window(y0, y1, x0, x1);
    std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> out;
    for (const QString &k : kinds)
        if (const auto *g = m.get(k))
            if (std::any_of(g->flat().begin(), g->flat().end(), [](std::uint8_t b) { return b != 0; }))
                out.emplace_back(*g, colour_of(k));
    mm._overlay = Mismatch::KeptOverlay{std::move(key), kinds, out};
    return out;
}

std::vector<long long> Mismatch::_overlay_key(long long y0, long long y1, long long x0, long long x1)
{
    // what window() reads: the maps (as whole() is kept by), the sea flags, the ocean indexes and the river water
    // value, and the window
    std::vector<long long> k = _sig();
    std::uint64_t hs = 1469598103934665603ULL;
    for (std::uint8_t b : md.sea)
        hs = (hs ^ b) * 1099511628211ULL;
    const auto [ok_t, ok_r] = ready();
    if (ls && (ok_t || ok_r)) {
        for (auto b : ls->ocean)
            hs = (hs ^ std::uint64_t(b)) * 1099511628211ULL;
        hs = (hs ^ std::uint64_t(ls->river_sea)) * 1099511628211ULL;
    }
    k.insert(k.end(), {static_cast<long long>(hs), ok_t, ok_r, static_cast<long long>(reinterpret_cast<std::intptr_t>(&md)),
                       md.uid, md.pix_stamp, md.w, md.h, y0, y1, x0, x1});
    return k;
}

}  // namespace mismatch
