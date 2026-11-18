// positions against Python, the C++ side of tools/check_positions.py.
// Usage: med_positions --dump <out> <work>  |  --run <copy> <bare copy> <backups> <work>
#include "../core/nopopup.h"
#include "core/adjacency.h"
#include "core/history.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/positions_geo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace positions;
using mapview::MapData;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString f(double x) { return py::repr_float(x); }
static QString pt(const std::optional<XY> &p) { return p ? u"(%1, %2)"_s.arg(f(p->first), f(p->second)) : u"None"_s; }
static QString name_of(long long p)
{
    const QString s = u"Abcdefghijklmnopqrstuvwxyz"_s.left(p % 19 + 1);
    return p % 17 == 0 ? u' ' + s + u' ' : s;
}
static QString list_i(const QList<long long> &l)
{
    QStringList p;
    for (long long x : l)
        p << QString::number(x);
    return u'[' + p.join(u", "_s) + u']';
}
static QString list_s(const QStringList &l)
{
    QStringList p;
    for (const QString &x : l)
        p << py::repr_str(x);
    return u'[' + p.join(u", "_s) + u']';
}
static QString fit_txt(const std::optional<NameFit> &r, double H)
{
    // what reaches the file, and the raw centre and angle to 10 digits (check_positions.py says why)
    if (!r)
        return u"None"_s;
    const double s2 = py::round_to(std::min(SCALE_MAX, std::max(0.6, r->scale * 0.9)), 2);
    const XY an = name_anchor(r->centre, r->angle, s2);
    const QString w = u"%1 %2 %3 %4"_s.arg(py::fmt_f(py::round_to(an.first, 6), 6), py::fmt_f(py::round_to(H - an.second, 6), 6),
                                           py::fmt_f(py::round_to(py::mod(-r->angle, 2 * std::numbers::pi), 6), 6), f(s2));
    return u"(%1, %2) %3 %4 %5 written %6"_s.arg(py::fmt_g(r->centre.first, 10), py::fmt_g(r->centre.second, 10),
                                                 py::fmt_g(r->angle, 10), f(r->scale), r->fits ? u"True"_s : u"False"_s, w);
}
static QString naval_txt(const std::optional<Naval> &n) { return n ? pt(n->at) + u' ' + f(n->rot) : u"None"_s; }
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }
static QString points_txt(const OrderedMap<QString, XY> &pp)
{
    QStringList l;
    for (const auto &[k, v] : pp)
        l << k + u'=' + pt(v);
    return l.join(u' ');
}

static std::vector<std::uint8_t> valid_of(const MapData &md, adjacency::AdjacencyFile &adj)
{
    std::vector<std::uint8_t> v = md.sea;
    v[0] = 0;
    for (const auto &kv : adj.river_tiles())
        if (0 < kv.first && kv.first < md.n)
            v[kv.first] = 0;
    for (long long t : adj.no_coast)
        if (0 < t && t < md.n)
            v[t] = 0;
    return v;
}

static moddata::Mod open_mod(const QString &work, const QString &shortn, const QString &rel, QStringList replace, bool find)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QString root = pypath::join(work, rel);
    if (find) {
        auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
        for (const auto &e : list)
            if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                replace = e.replace;
    }
    return moddata::Mod(root, shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    struct M {
        QString shortn, rel;
        QStringList replace;
        bool find;
    };
    const std::vector<M> mods = {
        {u"ferrum_mare"_s, u"Ferrum_Mare 1.2.000"_s,
         {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s}, false},
        {u"vanilla"_s, u"Victoria 2"_s, {}, false},
        {u"corgi"_s, u"Victoria 2\\mod\\CoRGI_puir 18.1"_s, {}, true},
        {u"modus"_s, u"Victoria 2\\mod\\Modus Omnino Demens 1.6"_s, {}, true}};
    for (const M &mm : mods) {
        moddata::Mod m = open_mod(work, mm.shortn, mm.rel, mm.replace, mm.find);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        PositionsFile pf(m, md.h);
        QStringList out = {u"entries %1 nl %2 hash %3 dirty %4"_s.arg(pf.entries.size()).arg(py::repr_str(pf.nl),
                                                                                         pf.hash.value_or(u"None"_s),
                                                                                         list_i(pf.dirty_ids()))};
        QStringList txt;
        for (const auto &[pid, e] : pf.entries)
            txt << fmt_entry(pid, *e, pf.nl);
        out << u"fmt "_s + h(txt.join(u'\n').toUtf8());
        auto show = [&](const QString &t) {
            QString s = t;
            s.replace(u"\r\n"_s, u"|"_s).replace(u'\n', u'|').replace(u'\t', u' ');
            out << u"  "_s + s;
        };
        for (qsizetype i = 0; i < std::min<qsizetype>(12, txt.size()); ++i)
            show(txt[i]);
        for (qsizetype i = std::max<qsizetype>(0, txt.size() - 3); i < txt.size(); ++i)
            show(txt[i]);
        std::vector<std::pair<long long, EntryP>> odd;
        for (const auto &[pid, e] : pf.entries)
            if (!e->extra.isEmpty() || !e->bp_extra.isEmpty() || !e->br_extra.isEmpty())
                odd.emplace_back(pid, e);
        out << u"odd %1"_s.arg(odd.size());
        for (size_t i = 0; i < odd.size() && i < 20; ++i)
            out << u"  %1 %2 %3 %4"_s.arg(odd[i].first).arg(list_s(odd[i].second->extra), list_s(odd[i].second->bp_extra),
                                                           list_s(odd[i].second->br_extra));
        history::ProvinceHistory hist(m);
        const QSet<long long> hn = naval_in_history(hist);
        QList<long long> hs(hn.begin(), hn.end());
        std::sort(hs.begin(), hs.end());
        out << u"naval_in_history %1 %2"_s.arg(hn.size()).arg(h(list_i(hs).toUtf8()));
        adjacency::AdjacencyFile adj(m);
        PositionPlacer pl(md, valid_of(md, adj));
        std::vector<long long> land;
        for (long long p = 1; p < md.n; ++p)
            if (md.area[p] > 0 && !md.sea[p])
                land.push_back(p);
        for (size_t i = 0; i < land.size(); ++i) {
            const long long p = land[i];
            const QString name = name_of(p);
            const Entry e = pf.get(p);
            const auto ns = pl.naval_spot(p);
            QString line = u"p %1 naval %2 ok %3"_s.arg(p).arg(naval_txt(ns), ns ? tf(pl.naval_ok(p, ns->at)) : u"-"_s);
            QStringList pr;
            for (const auto &[k, t] : check_entry(pl, md, p, e, name, double(md.h), ns.has_value(), hn.contains(p)))
                pr << k + u": "_s + t;
            out << line + u" check "_s + pr.join(u" / "_s);
            if (i % 3)
                continue;
            const auto r = pl.fit_name(p, name);
            out << u"  fit "_s + fit_txt(r, double(md.h));
            if (i % 9 == 0)
                out << u"  flat "_s + fit_txt(pl.fit_name(p, name, false), double(md.h));
            std::optional<NameBox> box;
            if (r) {
                box = PositionPlacer::name_box(r->centre, r->angle, r->scale, name);
                const QString c1 = f(pl.name_clips(p, r->centre, r->angle, r->scale, name));
                const QString c2 = f(pl.name_clips(p, r->centre, r->angle + 0.3, r->scale * 1.7, name));
                out << u"  clips %1 %2"_s.arg(c1, c2);
            }
            out << u"  points "_s + points_txt(pl.place_points(p, box));
            if (i % 9 == 0) {
                const auto pp = pl.place_points(p);
                out << u"  free "_s + points_txt(pp);
                const QString f1 = naval_txt(pl.fallback_naval(p)), f2 = naval_txt(pl.fallback_naval(p, XY{3.5, 7.25}));
                out << u"  fallback %1 / %2"_s.arg(f1, f2);
                if (!pp.isEmpty()) {
                    const XY u = *pp.get(u"unit"_s);
                    out << u"  ok %1 %2 %3"_s.arg(tf(pl.naval_ok(p, u)), tf(pl.naval_ok(p, {u.first + 9, u.second})),
                                                  tf(pl.naval_ok(p, {-3, 2})));
                }
            }
        }
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static Entry place_entry(PositionPlacer &pl, PositionsFile &pf, long long pid, const QString &name, Entry e, bool coast)
{
    // pospanel.place_entry, every kind (land kinds in a fixed order, as check_positions.py does it)
    const auto r = pl.fit_name(pid, name);
    std::optional<NameBox> box;
    if (r) {
        const double sc = py::round_to(std::min(SCALE_MAX, std::max(0.6, r->scale * 0.9)), 2);
        e.pts.insert(u"text_position"_s, pf.to_game(name_anchor(r->centre, r->angle, sc)));
        e.text_rotation = py::round_to(py::mod(-r->angle, 2 * std::numbers::pi), 6);
        e.text_scale = sc;
        box = PositionPlacer::name_box(r->centre, r->angle, sc, name);
    }
    const auto pts = pl.place_points(pid, box);
    for (const QString &k : {u"fort"_s, u"railroad"_s, u"building_construction"_s, u"military_construction"_s,
                             u"factory"_s, u"unit"_s})
        if (const XY *v = pts.get(k))
            e.pts.insert(k, pf.to_game(*v));
    std::optional<XY> near_xy;
    if (const XY *u = e.pts.get(u"unit"_s))
        near_xy = pf.to_disp(*u);
    const Naval ns = coast ? *pl.naval_spot(pid, near_xy) : pl.fallback_naval(pid, near_xy);
    e.pts.insert(u"naval_base"_s, pf.to_game(ns.at));
    e.naval_rot = py::round_to(ns.rot, 6);
    return e;
}

static int run(const QString &root, const QString &bare, const QString &backups, const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    QStringList out;
    moddata::Mod m(root, BASE, std::nullopt, REPLACE);
    m.load();
    MapData md(m, m.p({u"map"_s, u"provinces.bmp"_s}));
    PositionsFile pf(m, md.h);
    adjacency::AdjacencyFile adj(m);
    PositionPlacer pl(md, valid_of(md, adj));
    QList<long long> pids;
    for (const auto &kv : pf.entries)
        pids << kv.first;
    std::sort(pids.begin(), pids.end());
    const long long a = pids[10], b = pids[200], c = pids[201], d = pids.back();
    const auto s0 = pf.snapshot();
    pf.set_point(a, u"unit"_s, XY{100.25, 200.5});
    pf.set_point(a, u"naval_base"_s, std::nullopt);
    pf.set_point(b, u"fort"_s, XY{1.0000004, double(md.h) - 0.1234567});
    const auto s1 = pf.snapshot();
    pf.entries.remove(c);
    Entry e = pf.get(d);
    e.extra << u"custom = yes"_s;
    e.text_scale = 1.25;
    pf.set_entry(d, e);
    out << u"dirty "_s + list_i(pf.dirty_ids());
    pf.restore(s1);
    out << u"undo dirty "_s + list_i(pf.dirty_ids());
    pf.restore(s0);
    out << u"undo all dirty "_s + list_i(pf.dirty_ids());
    pf.set_point(a, u"unit"_s, XY{100.25, 200.5});
    pf.set_point(a, u"naval_base"_s, std::nullopt);
    pf.set_point(b, u"fort"_s, XY{1.0000004, double(md.h) - 0.1234567});
    pf.entries.remove(c);
    pf.set_entry(d, e);
    std::vector<long long> land;
    for (long long p = 1; p < md.n; ++p)
        if (md.area[p] > 0 && !md.sea[p])
            land.push_back(p);
    QList<long long> fresh, placed;
    for (long long p : land)
        if (!pf.entries.contains(p) && fresh.size() < 3)
            fresh << p;
    for (size_t i = 0; i < land.size() && placed.size() < 25; i += 97)
        placed << land[i];
    placed += fresh;
    for (long long p : placed) {
        const bool coast = pl.naval_spot(p).has_value();
        pf.set_entry(p, place_entry(pl, pf, p, name_of(p), pf.get(p), coast));
    }
    out << u"placed %1 fresh %2"_s.arg(list_i(placed), list_i(fresh));
    out << u"dirty "_s + list_i(pf.dirty_ids());
    out << u"saved "_s + pypath::relpath(*pf.save_file(backups), root);
    out << u"after dirty %1 entries %2 hash %3"_s.arg(list_i(pf.dirty_ids())).arg(pf.entries.size()).arg(*pf.hash);
    out << u"again "_s + (pf.save_file(backups) ? u"?"_s : u"None"_s);
    pf.set_point(pids[5], u"factory"_s, XY{5.5, 6.5});
    out << u"saved 2 "_s + pypath::relpath(*pf.save_file(backups), root);
    moddata::Mod mb(bare, BASE, std::nullopt, REPLACE);
    mb.load();
    PositionsFile pb(mb, md.h);
    out << u"bare path %1 entries %2"_s.arg(pypath::relpath(pb.path, work)).arg(pb.entries.size());
    QList<long long> bp;
    for (const auto &kv : pb.entries)
        bp << kv.first;
    std::sort(bp.begin(), bp.end());
    pb.set_point(bp[3], u"unit"_s, XY{10.5, 20.5});
    out << u"bare saved "_s + pypath::relpath(*pb.save_file(backups), bare);
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
