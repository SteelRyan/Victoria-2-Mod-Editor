// layerbmp + provbmp + mapcheck.province_pieces against Python, the C++ side of tools/check_mapbmps.py.
// Usage: med_mapbmps --run <mod copy> <backups> <work>
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/layerbmp.h"
#include "map/mapcheck.h"
#include "map/provbmp.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;

static QString hb(const void *p, size_t n)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), static_cast<qsizetype>(n)),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}
template <typename V> static QString hv(const V &v) { return hb(v.data(), v.size() * sizeof(v[0])); }
static QString hs(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return hb(b.constData(), b.size());
}
static QString box_v(const std::array<long long, 4> &b) { return u"(%1, %2, %3, %4)"_s.arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3]); }
static QString obox_v(const std::optional<std::array<long long, 4>> &b) { return b ? box_v(*b) : u"None"_s; }
template <typename T> static QString ints_v(const T &l)
{
    QStringList p;
    for (const auto &x : l)
        p << QString::number(static_cast<long long>(x));
    return u'[' + p.join(u", "_s) + u']';
}
static QString rel(const QString &p, const QString &root) { return pypath::relpath(p, root); }

static int run(const QString &root, const QString &backups, const QString &work)
{
    QStringList out;
    auto say = [&](const QString &s) { out << s; };
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    moddata::Mod m(root, BASE, std::nullopt, REPLACE);
    m.load();
    mapview::MapData md(m, m.p({u"map"_s, u"provinces.bmp"_s}));
    {
        QStringList p;
        qsizetype i = 0;
        for (const auto &[k, t] : layerbmp::terrain_types(m))
            if (i++ < 40)
                p << u"(%1, %2)"_s.arg(k).arg(py::repr_str(t));
        say(u"types ["_s + p.join(u", "_s) + u']');
    }
    std::vector<std::unique_ptr<layerbmp::IndexedBitmap>> bms;
    for (const auto &[fn, kind] : std::vector<std::pair<QString, QString>>{{u"terrain.bmp"_s, u"terrain_bmp"_s},
                                                                          {u"rivers.bmp"_s, u"rivers_bmp"_s},
                                                                          {u"terrain_td.bmp"_s, u"terrain_bmp"_s},
                                                                          {u"missing.bmp"_s, u"x"_s},
                                                                          {u"default.map"_s, u"x"_s}}) {
        auto b = std::make_unique<layerbmp::IndexedBitmap>(m, md, fn, kind);
        const bool ok = b->load();
        say(u"load %1 %2 %3"_s.arg(fn, ok ? u"True"_s : u"False"_s, b->error.value_or(QString()).replace(root, u"<root>"_s)));
        if (ok) {
            QStringList pal;
            for (const auto &[r, g, bb] : b->palette)
                pal << u"(%1, %2, %3)"_s.arg(r).arg(g).arg(bb);
            say(u"  palette %1 %2 data (%3, %4) %5 %6 hash %7 stamp %8"_s.arg(b->palette.size())
                    .arg(hs(u'[' + pal.join(u", "_s) + u']'))
                    .arg(b->data->h())
                    .arg(b->data->w())
                    .arg(hv(b->data->flat()))
                    .arg(u"(%1, %2, %3)"_s.arg(std::get<0>(*b->_layout)).arg(std::get<1>(*b->_layout))
                             .arg(std::get<2>(*b->_layout) ? u"True"_s : u"False"_s))
                    .arg(*b->hash)
                    .arg(b->stamp));
            say(u"  index %1 %2"_s.arg(b->color_index({255, 255, 255}, -1)).arg(b->color_index({1, 2, 3}, 77)));
        }
        bms.push_back(std::move(b));
    }
    layerbmp::IndexedBitmap &T = *bms[0], &Rv = *bms[1], &TD = *bms[2];
    layerbmp::LandSea ls(m, md, T, Rv);
    const auto pr = ls._prepare();
    say(u"prepare (%1, %2)"_s.arg(pr.first ? u"True"_s : u"False"_s, pr.second ? u"True"_s : u"False"_s));
    {
        std::vector<long long> oc;
        for (size_t i = 0; i < ls.ocean.size(); ++i)
            if (ls.ocean[i])
                oc.push_back(static_cast<long long>(i));
        say(u"  ocean %1 fill %2 plains %3 river_sea %4 none %5"_s.arg(ints_v(oc)).arg(ls.ocean_fill)
                .arg(ls.plains ? QString::number(*ls.plains) : u"None"_s)
                .arg(ls.river_sea)
                .arg(ls.river_none));
    }
    const auto &flat = md.idmap.flat();
    std::vector<std::int64_t> land_all, sea_all;
    for (size_t i = 0; i < flat.size(); ++i) {
        if (!md.sea[flat[i]])
            land_all.push_back(static_cast<std::int64_t>(i));
        if (md.sea[flat[i]] && flat[i] != 0)
            sea_all.push_back(static_cast<std::int64_t>(i));
    }
    std::vector<std::int64_t> land_px, sea_px;
    for (size_t i = 0; i < land_all.size() && land_px.size() < 300; i += 997)
        land_px.push_back(land_all[i]);
    for (size_t i = 0; i < sea_all.size() && sea_px.size() < 300; i += 991)
        sea_px.push_back(sea_all[i]);
    const long long a_land = flat[static_cast<size_t>(land_px[0])], a_sea = flat[static_cast<size_t>(sea_px[0])];
    std::vector<std::int64_t> idx = land_px;
    idx.insert(idx.end(), sea_px.begin(), sea_px.end());
    std::vector<long long> nw(land_px.size(), a_sea);
    nw.insert(nw.end(), sea_px.size(), a_land);
    for (const auto &c : ls.changes(idx, nw))
        say(u"change %1 %2 %3 %4"_s.arg(c.bitmap->filename).arg(c.idx.size()).arg(hv(c.idx), hv(c.vals)));
    // --- edits of the terrain layer
    std::vector<layerbmp::Box> boxes;
    T.listeners.push_back([&](const layerbmp::Box &b) { boxes.push_back(b); });
    const long long w = md.w;
    T.apply({5, 6, 7, w + 5, 3 * w + 9}, {1, 2, 3, 4, 5});
    T.apply({5, 6}, {1, 9});
    {
        std::vector<std::int64_t> i;
        std::vector<std::uint8_t> v;
        for (int k = 0; k < 40; ++k) {
            i.push_back(10 * w + k);
            v.push_back(static_cast<std::uint8_t>(k % 7));
        }
        T.apply(i, v);
    }
    T.apply({5}, {T.data->flat()[5]});
    {
        QStringList bx;
        for (const auto &b : boxes)
            bx << box_v(b);
        say(u"terrain version %1 dirty %2 boxes [%3]"_s.arg(T.version).arg(T.dirty().isEmpty() ? u"[]"_s : u"['terrain.bmp']"_s, bx.join(u", "_s)));
    }
    T.restore_to(1);
    say(u"undo %1 %2 %3"_s.arg(T.version).arg(hv(T.data->flat()), box_v(boxes.back())));
    T.restore_to(3);
    {
        const auto st = T.recovery_state();
        say(u"redo %1 %2 recovery %3 %4"_s.arg(T.version).arg(hv(T.data->flat()), hv(st->idx), hv(st->vals)));
    }
    {
        const auto s = T.save(backups);
        say(u"saved %1 %2 dirty %3"_s.arg(rel(s->first(), root), *T.hash, T.dirty().isEmpty() ? u"[]"_s : u"['terrain.bmp']"_s));
    }
    T.live({w * 50 + 1, w * 50 + 2}, {8, 8});
    T.apply({w * 60}, {3});
    T.revert_unsaved();
    say(u"reverted %1 %2 %3 %4"_s.arg(T.version).arg(T.saved).arg(hv(T.data->flat())).arg(T.stamp));
    TD.apply({0, 1, w * (md.h - 1) + 3}, {7, 7, 7});
    say(u"td saved "_s + rel(TD.save(backups)->first(), root));
    Rv.apply({w * 7 + 7}, {static_cast<std::uint8_t>(ls.river_sea)});
    layerbmp::IndexedBitmap T2(m, md, u"terrain.bmp"_s, u"terrain_bmp"_s);
    T2.load();
    T2.apply({1}, {9});
    T2.revert_unsaved();
    say(u"t2 %1 %2"_s.arg(T2.version).arg(T2.dirty().isEmpty() ? u"[]"_s : u"['terrain.bmp']"_s));
    // --- provinces.bmp
    provbmp::ProvinceBitmap P(m, md);
    P.remember_file();
    say(u"prov hash "_s + P.hash.value_or(u"None"_s));
    std::vector<std::pair<provbmp::Box, provbmp::Ids>> got;
    P.listeners.push_back([&](const provbmp::Box &b, const provbmp::Ids &i) { got.emplace_back(b, i); });
    auto last = [&]() { return got.empty() ? u"None"_s : u"(%1, %2)"_s.arg(box_v(got.back().first), ints_v(got.back().second)); };
    const long long pid_a = a_land;
    const long long x0 = md.x0[pid_a], y0 = md.y0[pid_a];
    std::vector<std::int64_t> rect;
    for (int dy = 0; dy < 4; ++dy)
        for (int dx = 0; dx < 6; ++dx)
            rect.push_back((y0 + dy) * w + x0 + dx);
    say(u"live "_s + obox_v(P.paint_live({rect[0], rect[1], rect[2]}, 0)));
    auto cur = [&](const std::vector<std::int64_t> &ix) {
        std::vector<std::uint16_t> v;
        for (std::int64_t i : ix)
            v.push_back(md.idmap.flat()[static_cast<size_t>(i)]);
        return v;
    };
    {
        const auto b = P.commit(rect, cur(rect), std::vector<std::uint16_t>(rect.size(), static_cast<std::uint16_t>(a_sea)));
        say(u"commit %1 %2 %3"_s.arg(obox_v(b), last()).arg(P.version));
    }
    std::vector<std::int64_t> erase;
    for (int k = 0; k < 5; ++k)
        erase.push_back(rect[k] + 20 * w);
    {
        const auto b = P.commit(erase, cur(erase), std::vector<std::uint16_t>(5, 0));
        say(u"erase %1 %2 v%3"_s.arg(obox_v(b), last()).arg(P.version));
    }
    P.restore_to(0);
    say(u"undo %1 v%2 %3"_s.arg(got.empty() ? u"None"_s : box_v(got.back().first)).arg(P.version).arg(hv(md.idmap.flat())));
    P.restore_to(2);
    say(u"redo v%1 %2 area %3 %4"_s.arg(P.version).arg(hv(md.idmap.flat())).arg(md.area[pid_a]).arg(md.area[a_sea]));
    {
        const auto st = P.recovery_state();
        say(u"recovery %1 %2"_s.arg(hv(st->idx), hv(st->vals)));
    }
    QFile f(P.path);
    if (!f.open(QIODevice::ReadOnly))
        return 4;
    const QByteArray orig = f.readAll();
    f.close();
    {
        const QByteArray e = P.encode(orig);
        say(u"encode "_s + hb(e.constData(), e.size()));
        QByteArray td = orig;
        long long hh = 0;
        for (int i = 0; i < 4; ++i)
            hh |= static_cast<long long>(static_cast<unsigned char>(orig[22 + i])) << (8 * i);
        if (hh & 0x80000000LL)
            hh -= 0x100000000LL;
        const std::int32_t neg = static_cast<std::int32_t>(-hh);
        for (int i = 0; i < 4; ++i)
            td[22 + i] = static_cast<char>((static_cast<std::uint32_t>(neg) >> (8 * i)) & 0xFF);
        const QByteArray e2 = P.encode(td);
        say(u"encode td "_s + hb(e2.constData(), e2.size()));
        try {
            P.encode(orig.left(30) + QByteArray(10, '\0'));
            say(u"encode bad: no error"_s);
        } catch (...) {
            say(u"encode bad: refused"_s);
        }
        try {
            P.encode(orig.left(28) + QByteArray("\x08\0", 2) + orig.mid(30));
            say(u"encode 8 bit: no error"_s);
        } catch (const std::runtime_error &ex) {
            say(u"encode 8 bit: "_s + QString::fromUtf8(ex.what()));
        }
    }
    {
        const auto s = P.save(backups);
        say(u"saved %1 dirty %2"_s.arg(rel(s->first(), root), P.dirty().isEmpty() ? u"[]"_s : u"['provinces.bmp']"_s));
    }
    {
        std::vector<std::int64_t> r4;
        for (int k = 0; k < 4; ++k)
            r4.push_back(rect[k] + 40 * w);
        P.paint_live(r4, a_land);
    }
    P.reload_file();
    say(u"reloaded v%1 %2 %3"_s.arg(P.version).arg(hv(md.idmap.flat()), last()));
    say(u"parts %1 %2"_s.arg(ints_v(mapcheck::province_part_sizes(md, pid_a)), ints_v(mapcheck::province_part_sizes(md, 0))));
    for (const auto &[name, mod_root, base] : std::vector<std::tuple<QString, QString, std::optional<QString>>>{
             {u"fm"_s, root, BASE}, {u"vanilla"_s, BASE, std::nullopt}}) {
        moddata::Mod mm(mod_root, base, std::nullopt, base ? REPLACE : QStringList());
        mm.load();
        mapview::MapData mdd(mm, mm.p({u"map"_s, u"provinces.bmp"_s}));
        grid::Grid<std::int32_t> i32(mdd.h, mdd.w);
        for (size_t i = 0; i < mdd.idmap.flat().size(); ++i)
            i32.flat()[i] = mdd.idmap.flat()[i];
        const auto pc = mapcheck::province_pieces(i32);
        say(u"%1 pieces %2 %3 %4 %5"_s.arg(name).arg(pc.pid.size()).arg(hv(pc.pid), hv(pc.size), hv(pc.first)));
        for (const auto &[lim, only] : std::vector<std::pair<long long, std::optional<long long>>>{{30, std::nullopt}, {8, std::nullopt}, {40, 0}}) {
            const auto sp = provbmp::small_pieces(mdd.idmap, lim, only);
            QStringList all;
            for (const auto &[i, v] : sp)
                all << u"(%1, %2)"_s.arg(ints_v(i)).arg(v);
            say(u"  small %1 %2 %3 %4"_s.arg(lim).arg(only ? QString::number(*only) : u"None"_s).arg(sp.size())
                    .arg(hs(u'[' + all.join(u", "_s) + u']')));
            for (size_t k = 0; k < sp.size() && k < 5; ++k) {
                std::vector<std::int64_t> six(sp[k].first.begin(), sp[k].first.begin() + std::min<size_t>(6, sp[k].first.size()));
                say(u"    %1 %2 %3"_s.arg(sp[k].first.size()).arg(ints_v(six)).arg(sp[k].second));
            }
        }
        grid::Grid<std::uint8_t> mask(80, 120);
        const std::uint16_t ref = mdd.idmap(140, 360);
        for (long long y = 0; y < 80; ++y)
            for (long long x = 0; x < 120; ++x)
                mask(y, x) = mdd.idmap(100 + y, 300 + x) == ref ? 1 : 0;
        const auto comp = provbmp::component(mask, 40, 60);
        long long n = 0;
        for (std::uint8_t v : comp.flat())
            n += v;
        say(u"  component %1 %2"_s.arg(n).arg(hv(comp.flat())));
    }
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, static_cast<size_t>(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
