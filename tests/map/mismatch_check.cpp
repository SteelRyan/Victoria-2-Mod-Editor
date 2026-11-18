// mismatch against Python, the C++ side of tools/check_mismatch.py.  Usage: med_mismatch --dump <out> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/pypath.h"
#include "map/mismatch.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <array>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using mapview::MapData;

template <typename T>
static QString hv(const std::vector<T> &x)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char *>(x.data()),
                                                                                qsizetype(x.size() * sizeof(T))),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}
static long long ones(const grid::Grid<std::uint8_t> &g)
{
    long long n = 0;
    for (std::uint8_t b : g.flat())
        n += b != 0;
    return n;
}
static const QStringList KINDS = {u"t_sea"_s, u"t_land"_s, u"r_water"_s, u"r_sea"_s};

static void report(mismatch::Mismatch &mm, const MapData &md, QStringList &out)
{
    const auto w = mm.whole();
    const auto [rt, rr] = mm.ready();
    std::vector<std::pair<QString, long long>> cs;
    for (const auto &[k, n] : mm.counts())
        cs.emplace_back(k, n);
    out << u"ready %1 counts %2"_s.arg(pyform::L(rt, rr), v(cs));
    for (const auto &[k, i] : w) {
        out << u"  whole %1 %2 %3"_s.arg(k).arg(i.size()).arg(hv(i));
        std::vector<std::int64_t> pick(i.begin(), i.begin() + std::min<size_t>(3, i.size()));
        pick.insert(pick.end(), i.end() - std::min<size_t>(2, i.size()), i.end());
        for (std::int64_t p : pick) {
            const long long x = p % md.w, y = p / md.w;
            out << u"    at %1,%2 %3%4"_s.arg(x).arg(y).arg(v(mm.kind_at(x, y)), mm.describe(x, y));
        }
    }
    const std::vector<std::array<long long, 4>> wins = {{0, 64, 0, 64},
                                                        {md.h / 3, md.h / 3 + 200, md.w / 4, md.w / 4 + 300},
                                                        {md.h - 50, md.h + 20, md.w - 70, md.w + 10}};
    for (const auto &[y0, y1, x0, x1] : wins) {
        QStringList p;
        for (const auto &[k, a] : mm.window(y0, y1, x0, x1))
            p << pyform::list({pyform::v(k),
                               pyform::list({QString::number(a.h()), QString::number(a.w())}),
                               QString::number(ones(a)), pyform::v(hv(a.flat()))});
        out << u"  window %1 %2 %3 %4 %5"_s.arg(y0).arg(y1).arg(x0).arg(x1).arg(pyform::list(p));
        QStringList o;
        for (const auto &[a, c] : mismatch::overlay(mm, KINDS, y0, y1, x0, x1))
            o << pyform::L(ones(a), c);
        out << u"  overlay "_s + pyform::list(o);
    }
    mismatch::Flat idx;
    for (long long i = 5; i < md.w * md.h; i += 1231)
        idx.push_back(i);
    {
        QStringList p;
        for (const auto &[k, a] : mm.at(idx))
            p << pyform::L(k, ones(a), hv(a.flat()));
        out << u"  at %1 %2"_s.arg(idx.size()).arg(pyform::list(p));
    }
    const std::vector<std::pair<QStringList, bool>> tries = {
        {KINDS, false}, {{u"t_land"_s, u"r_sea"_s}, false}, {KINDS, true}, {{u"r_water"_s}, true}};
    for (const auto &[kinds, use_idx] : tries)
        for (const std::optional<long long> land : {std::optional<long long>(), std::optional<long long>(7)}) {
            QStringList p;
            for (const auto &f : mm.fix(kinds, use_idx ? std::optional(idx) : std::nullopt, land))
                p << pyform::L(f.bitmap->filename, static_cast<long long>(f.idx.size()), hv(f.idx), hv(f.vals));
            out << u"  fix %1 %2 %3 %4"_s.arg(v(kinds), use_idx ? u"idx"_s : u"all"_s, v(land), pyform::list(p));
        }
}

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
    for (const M &mo : mods) {
        const QString root = pypath::join(work, mo.rel);
        QStringList replace = mo.replace;
        if (mo.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mo.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        layerbmp::IndexedBitmap tb(m, md, u"terrain.bmp"_s, u"terrain_bmp"_s);
        layerbmp::IndexedBitmap rb(m, md, u"rivers.bmp"_s, u"rivers_bmp"_s);
        QStringList out;
        {
            mismatch::Mismatch none(m, md, nullptr);
            const auto [a, b] = none.ready();
            out << u"no landsea %1 %2"_s.arg(pyform::L(a, b), v(none.describe(3, 3)));
        }
        layerbmp::LandSea ls(m, md, tb, rb);
        mismatch::Mismatch mm(m, md, &ls);
        report(mm, md, out);
        const auto [ok_t, ok_r] = mm.ready();
        out << u"pattern"_s;
        const long long n = md.w * md.h;
        long long k = 0;
        for (long long p = 11; p < n; p += 997, ++k) {
            if (ok_t) {
                const long long vals[3] = {ls.ocean_fill, ls.plains.value_or(0), 3};
                tb.data->flat()[size_t(p)] = std::uint8_t(vals[k % 3]);
            }
            if (ok_r) {
                const long long vals[3] = {254, 255, 3};
                rb.data->flat()[size_t(p)] = std::uint8_t(vals[(k / 3) % 3]);
            }
        }
        mismatch::Mismatch mm2(m, md, &ls);
        report(mm2, md, out);
        QFile fh(QDir(out_dir).filePath(mo.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mo.shortn), static_cast<long long>(out.size()));
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
