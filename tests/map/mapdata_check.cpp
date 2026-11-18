// mapview.MapData against Python, the C++ side of tools/check_mapdata.py.  Usage: med_mapdata --dump <out> <work>
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "map/mapview_data.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <set>

using namespace Qt::StringLiterals;
using mapview::MapData;

static QString hb(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), static_cast<qsizetype>(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}
template <typename V> static QString hv(const V &v) { return hb(v.data(), v.size() * sizeof(v[0])); }
static QString list_v(const std::vector<long long> &l)
{
    QStringList p;
    for (long long x : l)
        p << QString::number(x);
    return u'[' + p.join(u", "_s) + u']';
}
static QString box_v(const std::optional<std::array<long long, 4>> &b)
{
    if (!b)
        return u"None"_s;
    return u"(%1, %2, %3, %4)"_s.arg((*b)[0]).arg((*b)[1]).arg((*b)[2]).arg((*b)[3]);
}

static QStringList state_lines(const QString &tag, MapData &md)
{
    QStringList out = {u"%1 size %2 %3 %4"_s.arg(tag).arg(md.w).arg(md.h).arg(md.n)};
    auto line = [&](const char *name, const char *dtype, const QString &hash) {
        out << u"%1 %2 %3 %4"_s.arg(tag, QString::fromLatin1(name), QString::fromLatin1(dtype), hash);
    };
    line("idmap", "uint16", hv(md.idmap.flat()));
    line("sea", "bool", hv(md.sea));
    line("coastal", "bool", hv(md.coastal));
    line("prov_border", "bool", hv(md.prov_border.flat()));
    line("sea_border", "bool", hv(md.sea_border.flat()));
    line("state_border", "bool", hv(md.state_border.flat()));
    line("area", "int64", hv(md.area));
    line("cx", "float64", hv(md.cx));
    line("cy", "float64", hv(md.cy));
    line("x0", "int32", hv(md.x0));
    line("y0", "int32", hv(md.y0));
    line("x1", "int32", hv(md.x1));
    line("y1", "int32", hv(md.y1));
    line("reg_idx", "int32", hv(md.reg_idx));
    const QByteArray rj = md.region_ids.join(u'\n').toUtf8();
    out << u"%1 regions %2 %3"_s.arg(tag).arg(md.region_ids.size()).arg(hb(rj.constData(), rj.size()));
    QStringList adj;
    for (const auto &[p, s] : md.adjacent)
        adj << u"(%1, %2)"_s.arg(p).arg(list_v(s.items()));
    const QByteArray aj = (u'[' + adj.join(u", "_s) + u']').toUtf8();
    out << u"%1 adjacent %2 %3"_s.arg(tag).arg(md.adjacent.size()).arg(hb(aj.constData(), aj.size()));
    qsizetype i = 0;
    for (const auto &[p, s] : md.adjacent) {
        if (i++ >= 30)
            break;
        out << u"%1   %2 %3"_s.arg(tag).arg(p).arg(list_v(s.items()));
    }
    const QString k = hv(md.pix_key().flat()), m2 = hv(md.pix_key_mix(2).flat()), m3 = hv(md.pix_key_mix(3).flat());
    out << u"%1 key %2 mix2 %3 mix3 %4"_s.arg(tag, k, m2, m3);
    out << u"%1 version %2"_s.arg(tag).arg(md.version);
    return out;
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
        QStringList out = state_lines(u"load"_s, md);
        {
            QStringList at;
            for (const auto &[x, y] : std::vector<std::pair<double, double>>{
                     {0, 0}, {10.7, 3.2}, {double(md.w - 1), double(md.h - 1)}, {double(md.w), 0}, {-1, 5}, {md.w / 2.0, md.h / 2.0}})
                at << QString::number(md.province_at(x, y));
            out << u"at ["_s + at.join(u", "_s) + u']';
        }
        out << u"bbox %1 %2 %3"_s.arg(box_v(md.bbox({1, 2, 3})), box_v(md.bbox({})), box_v(md.bbox({0, md.n + 5, 7})));
        // the edits (the same as check_mapdata.py's edits())
        std::vector<long long> land;
        for (long long p = 1; p < md.n && land.size() < 400; ++p)
            if (md.area[p] > 0 && !md.sea[p])
                land.push_back(p);
        const long long a = land[land.size() / 3], b = land[land.size() / 2];
        const std::vector<std::pair<std::array<long long, 4>, long long>> edits = {
            {{md.x0[a], md.y0[a], md.x0[a] + 7, md.y0[a] + 5}, b},
            {{0, md.h / 2, 4, md.h / 2 + 3}, a},
            {{md.w - 3, 10, md.w, 14}, b},
            {{md.w / 4, md.h / 4, md.w / 4 + 300, md.h / 4 + 200}, a},
            {{md.x0[b], md.y0[b], md.x1[b] + 1, md.y1[b] + 1}, 0}};
        for (size_t i = 0; i < edits.size(); ++i) {
            const auto &[box, pid] = edits[i];
            const auto [x0, y0, x1, y1] = box;
            std::set<long long> ids = {pid};
            for (long long y = y0; y < std::min(y1, md.h); ++y)
                for (long long x = x0; x < std::min(x1, md.w); ++x) {
                    ids.insert(md.idmap(y, x));
                    md.idmap(y, x) = static_cast<std::uint16_t>(pid);
                }
            md.refresh_masks(box);
            md.refresh_ids(std::vector<long long>(ids.begin(), ids.end()), box);
            out << u"edit %1 (%2, %3, %4, %5) %6 ids"_s.arg(i).arg(x0).arg(y0).arg(x1).arg(y1).arg(ids.size());
            out += state_lines(u"e%1"_s.arg(i), md);
        }
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
