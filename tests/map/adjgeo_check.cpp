// adjacency's map half against Python, the C++ side of tools/check_adjgeo.py.  Usage: med_adjgeo --dump <out> <work>
#include "../core/nopopup.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/adjacency_geo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace adjacency;
using mapview::MapData;

static QString hb(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), static_cast<qsizetype>(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}
static QString pt(const std::optional<Pt> &p)
{
    return p ? u"(%1, %2)"_s.arg(py::repr_float(p->first), py::repr_float(p->second)) : u"None"_s;
}
template <typename R> static QString pairs_txt(const R &s)
{
    QStringList l;
    for (const auto &[a, b] : s)
        l << u"%1,%2"_s.arg(a).arg(b);
    return l.join(u' ');
}
static QString rp_txt(const RiverPairs &d)
{
    QStringList l;
    for (const auto &[k, v] : d)
        l << u"%1,%2:%3%4%5"_s.arg(k.first).arg(k.second).arg(py::repr_float(v.dist), pt(v.pa), pt(v.pb));
    return l.join(u' ');
}
static QString list_i(const std::vector<long long> &l)
{
    QStringList p;
    for (long long x : l)
        p << QString::number(x);
    return u'[' + p.join(u", "_s) + u']';
}
static QString anchors_txt(const std::optional<std::pair<Pt, Pt>> &a)
{
    return a ? pt(a->first) + u' ' + pt(a->second) : u"None"_s;
}

static QStringList status_lines(const QString &tag, AdjacencyFile &adj, AdjGeometry &geo, const moddata::Mod &mod)
{
    QStringList out;
    const CoastStatus st = coast_status(adj, geo);
    out << u"%1 coast pairs %2: %3"_s.arg(tag).arg(st.pairs.size()).arg(pairs_txt(st.pairs.items()));
    out << u"%1 coast missing %2: %3"_s.arg(tag).arg(st.missing.size()).arg(pairs_txt(st.missing));
    std::vector<long long> stale;
    for (const Row &r : st.stale)
        stale.push_back(r.uid);
    out << u"%1 coast stale %2"_s.arg(tag, list_i(stale));
    out << u"%1 coast river_pairs %2: %3"_s.arg(tag).arg(st.river_pairs.size()).arg(pairs_txt(st.river_pairs.items()));
    out << u"%1 coast nc_pairs %2: %3"_s.arg(tag).arg(st.nc_pairs.size()).arg(pairs_txt(st.nc_pairs.items()));
    for (const auto &[name, v] : river_status(adj, geo)) {
        out << u"%1 river %2 expected %3"_s.arg(tag, name, rp_txt(v.expected));
        out << u"%1 river %2 missing %3"_s.arg(tag, name, pairs_txt(v.missing));
    }
    for (const auto &[uid, texts] : problems(adj, geo, mod))
        out << u"%1 problem %2: %3"_s.arg(tag).arg(uid).arg(texts.join(u" | "_s));
    return out;
}

static QString name_txt(const std::optional<QString> &n) { return n ? *n : u"None"_s; }
static QString name_repr(const std::optional<QString> &n) { return n ? py::repr_str(*n) : u"None"_s; }

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
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
    for (const M &mdm : mods) {
        const QString root = pypath::join(work, mdm.rel);
        QStringList replace = mdm.replace;
        if (mdm.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mdm.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        AdjacencyFile adj(m);
        QSet<long long> sea;
        for (const auto &[p, pr] : m.provinces)
            if (pr.sea)
                sea.insert(p);
        AdjGeometry geo(md, sea);
        QStringList out = {u"rows %1 marks %2 no_coast %3"_s.arg(adj.rows.size()).arg(adj.marks.size()).arg(adj.no_coast.size())};
        const std::vector<long long> thin = geo.thin_tiles().items();
        {
            QStringList l;
            for (long long t : thin)
                l << QString::number(t);
            out << u"thin %1: %2"_s.arg(thin.size()).arg(l.join(u' '));
        }
        const auto cp = geo.coast_pairs().items();
        out << u"coast_pairs %1: %2"_s.arg(cp.size()).arg(pairs_txt(cp));
        for (long long p = 1; p < md.n; p += 7) {
            const auto c = geo.pixels(p);
            if (!c) {
                out << u"px %1 None %2"_s.arg(p).arg(pt(geo.inner(p)));
                continue;
            }
            const auto &[xs, ys] = *c;
            const QString hx = hb(xs.data(), xs.size() * sizeof(float)), hy = hb(ys.data(), ys.size() * sizeof(float));
            const QString in = pt(geo.inner(p));
            const QString nr = pt(geo.nearest(p, md.cx[p] + 13.3, md.cy[p] - 7.9));
            const QString wr = pt(geo.nearest(p, 2.2, md.cy[p]));
            out << u"px %1 %2 %3 %4 inner %5 near %6 wrap %7"_s.arg(p).arg(xs.size()).arg(hx, hy, in, nr, wr);
            const std::vector<long long> nb = md.neighbours(p);
            for (size_t i = 0; i < nb.size() && i < 3; ++i) {
                const QString bp = pt(geo.border_point(p, nb[i]));
                out << u"  bp %1 %2 %3 anchors %4"_s.arg(p).arg(nb[i]).arg(bp, anchors_txt(geo.anchors(p, nb[i])));
            }
        }
        out << u"bp none %1 %2"_s.arg(pt(geo.border_point(1, 1)), pt(geo.border_point(0, md.n + 3)));
        for (const Row &r : adj.rows)
            out << u"row %1 %2 %3 %4"_s.arg(r.uid).arg(r.a).arg(r.b).arg(anchors_txt(geo.anchors(r.a, r.b)));
        const std::vector<long long> tl(thin.begin(), thin.begin() + std::min<size_t>(60, thin.size()));
        for (size_t i = 0; i < tl.size(); ++i) {
            out << u"rp %1 %2"_s.arg(tl[i]).arg(rp_txt(geo.river_pairs({tl[i]})));
            if (i + 1 < tl.size())
                out << u"rp2 %1 %2 %3"_s.arg(tl[i]).arg(tl[i + 1]).arg(rp_txt(geo._river_pairs({tl[i], tl[i + 1]}, 3.1)));
        }
        const auto found = detect_rivers(adj, geo);
        for (const DetectedRiver &f : found)
            out << u"detect %1 %2 %3 name_of %4"_s.arg(f.name, list_i(f.tiles)).arg(f.rows).arg(
                name_txt(river_name_of(adj, geo, f.tiles)));
        {
            QStringList l;
            for (size_t i = 0; i < tl.size() && i < 30; ++i)
                l << name_repr(river_name_of(adj, geo, {tl[i]}));
            out << u"name_of thin ["_s + l.join(u", "_s) + u']';
        }
        out += status_lines(u"s0"_s, adj, geo, m);
        for (size_t i = 0; i < found.size() && i < 6; ++i)
            adj.set_mark(found[i].name, QList<long long>(found[i].tiles.begin(), found[i].tiles.end()));
        const auto marked = adj.river_tiles();
        std::vector<long long> nc;
        for (long long t : tl)
            if (!marked.contains(t) && nc.size() < 4)
                nc.push_back(t);
        adj.set_no_coast(QList<long long>(nc.begin(), nc.end()));
        out << u"marked %1 no_coast %2"_s.arg(adj.marks.size()).arg(list_i(nc));
        out += status_lines(u"s1"_s, adj, geo, m);
        for (size_t i = 0; i < found.size() && i < 3; ++i)
            out << u"name_of marked %1 %2"_s.arg(found[i].name, name_txt(river_name_of(adj, geo, found[i].tiles)));
        geo.reset();
        const auto n_thin = geo.thin_tiles().size();
        const auto n_cp = geo.coast_pairs().size();
        out << u"after reset %1 %2 %3"_s.arg(n_thin).arg(n_cp).arg(pt(geo.inner(1)));
        QFile f(QDir(out_dir).filePath(mdm.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mdm.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
