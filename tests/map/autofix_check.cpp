// autofix against Python, the C++ side of tools/check_autofix.py.  Usage: med_autofix --dump <out> <work>
#include "../core/nopopup.h"
#include "core/history.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "map/autofix.h"
#include "map/layerbmp.h"
#include "map/mapcheck.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <map>

using namespace Qt::StringLiterals;
using mapview::MapData;

template <typename T>
static QString hv(const std::vector<T> &v)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char *>(v.data()),
                                                                                qsizetype(v.size() * sizeof(T))),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}
template <typename T>
static QString head6(const std::vector<T> &v)
{
    QStringList p;
    for (size_t i = 0; i < v.size() && i < 6; ++i)
        p << QString::number(v[i]);
    return u'[' + p.join(u", "_s) + u']';
}
static QString f(double x) { return py::repr_float(x); }

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
    for (const M &mm : mods) {
        const QString root = pypath::join(work, mm.rel);
        QStringList replace = mm.replace;
        if (mm.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mm.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        const QString bmp = m.p({u"map"_s, m.provinces_bmp});
        MapData md(m, bmp);
        QStringList out;
        layerbmp::IndexedBitmap rb(m, md, u"rivers.bmp"_s, u"rivers_bmp"_s);
        if (rb.load()) {
            const auto &R = *rb.data;
            const auto [idx, vals] = autofix::river_repairs(md, R);
            out << u"rivers all %1 %2 %3"_s.arg(idx.size()).arg(hv(idx), hv(vals));
            std::vector<std::pair<double, double>> spots;
            for (const auto &it : mapcheck::river_checks(md, R).items)
                if (spots.size() < 40)
                    spots.push_back(*it.point);
            for (size_t i = 0; i < spots.size() && i < 25; ++i) {
                const auto [i2, v2] = autofix::river_repairs(md, R, {spots[i]});
                out << u"  at (%1, %2) %3 %4 %5"_s.arg(f(spots[i].first), f(spots[i].second)).arg(i2.size()).arg(head6(i2), head6(v2));
            }
            const auto [i3, v3] = autofix::river_repairs(md, R, spots, 2);
            out << u"rivers spots %1 %2 %3"_s.arg(i3.size()).arg(hv(i3), hv(v3));
        }
        const auto &idm = md.idmap;
        grid::Grid<std::int32_t> i32(idm.h(), idm.w());
        for (size_t i = 0; i < i32.flat().size(); ++i)
            i32.flat()[i] = idm.flat()[i];
        const auto pc = mapcheck::province_pieces(i32);
        std::map<long long, std::vector<std::pair<long long, long long>>> by;
        for (size_t k = 0; k < pc.pid.size(); ++k)
            by[pc.pid[k]].push_back({pc.size[k], pc.first[k]});
        long long n = 0;
        for (auto &[p, lst] : by) {
            std::sort(lst.begin(), lst.end(), std::greater<>());
            for (size_t k = 1; k < lst.size(); ++k) {
                const auto [s, fl] = lst[k];
                if (s >= 20 || n >= 300)
                    continue;
                ++n;
                const double x = double(py::mod(fl, md.w)) + 0.5, y = double(py::floordiv(fl, md.w)) + 0.5;
                const auto r = autofix::piece_owner(idm, p, x, y);
                const auto pp = autofix::piece_pixels(idm, p, x, y);
                out << u"piece %1 %2 %3 %4 pixels %5 %6"_s.arg(p).arg(f(x), f(y),
                                                                 r ? u"%1 %2 %3"_s.arg(r->first.size()).arg(hv(r->first)).arg(r->second)
                                                                   : u"None"_s)
                           .arg(pp.size()).arg(hv(pp));
            }
        }
        out << u"piece off %1 %2"_s.arg(autofix::piece_owner(idm, 1, -5, 3) ? u"?"_s : u"None"_s)
                   .arg(autofix::piece_pixels(idm, 1, double(md.w + 3), 2).size());
        std::vector<mapcheck::Rgb> rgbs;
        for (const auto &kv : m.provinces)
            rgbs.push_back(kv.second.rgb);
        const auto unknown = mapcheck::unknown_colours(bmp, idm, rgbs);
        std::vector<std::tuple<int, int, int>> cols;
        for (const auto &u : unknown)
            if (cols.size() < 60)
                cols.push_back(u.first);
        const auto keys = autofix::file_colours(bmp, idm.h(), idm.w());
        for (const auto &[fl, to] : autofix::colour_areas(idm, keys, cols))
            out << u"area %1 %2 %3"_s.arg(fl.size()).arg(hv(fl)).arg(to);
        for (int k : {2, 3, 7}) {
            grid::Grid<std::uint8_t> mask(std::min<long long>(300, idm.h()), std::min<long long>(400, idm.w()), 0);
            for (long long y = 0; y < mask.h(); ++y)
                for (long long x = 0; x < mask.w(); ++x)
                    mask(y, x) = idm(y, x) % k == 0;
            const auto [lab, nl] = autofix::labels4(mask);
            out << u"labels %1 %2 %3"_s.arg(k).arg(nl).arg(hv(lab.flat()));
        }
        history::ProvinceHistory hist(m);
        std::vector<long long> land, picks;
        for (const auto &[p, pr] : m.provinces)
            if (!pr.sea)
                land.push_back(p);
        for (size_t i = 0; i < land.size(); i += 5)
            picks.push_back(land[i]);
        long long nh = 0;
        for (long long q : land) {
            const auto *fl = hist.files.get(q);
            if ((!fl || fl->empty()) && nh < 40) {
                picks.push_back(q);
                ++nh;
            }
        }
        for (long long p : picks)
            out << u"hist %1 %2 %3"_s.arg(p).arg(autofix::history_folder(m, md, hist, p), autofix::history_rel(m, md, hist, p));
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
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
