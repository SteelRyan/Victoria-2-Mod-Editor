// provdelete against Python, the C++ side of tools/check_provdelete.py.
// Usage: med_provdelete --dump <out> <work>  |  --run <copy> <backups> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/countryhist.h"
#include "core/oob.h"
#include "core/positions.h"
#include "core/pypath.h"
#include "core/script.h"
#include "map/newprov.h"
#include "map/provbmp.h"
#include "map/provdelete.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using mapview::MapData;
using OptStr = std::optional<QString>;

template <typename T>
static QString hv(const std::vector<T> &x)
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char *>(x.data()),
                                                                                qsizetype(x.size() * sizeof(T))),
                                                        QCryptographicHash::Sha1).toHex().left(16));
}

struct Setup {                                    // the document as the window makes it
    std::unique_ptr<MapData> md;
    std::unique_ptr<document::Document> d;
    std::unique_ptr<layerbmp::LandSea> landsea;
};
static Setup setup(moddata::Mod &m)
{
    Setup s;
    s.md = std::make_unique<MapData>(m, m.p({u"map"_s, m.provinces_bmp}));
    QString sc;
    for (const QString &x : m.pop_scenarios())
        if (script::parse_date(x)) {
            sc = x;
            break;
        }
    s.d = std::make_unique<document::Document>(m, sc);
    auto &d = *s.d;
    auto &md = *s.md;
    d.maps.insert(u"positions"_s, std::make_shared<positions::PositionsFile>(m, md.h));
    d.maps.insert(u"provinces_bmp"_s, std::make_shared<provbmp::ProvinceBitmap>(m, md));
    d.maps.insert(u"definitions"_s, std::make_shared<newprov::ProvinceDefs>(m, md, *d.hist));
    auto tb = std::make_shared<layerbmp::IndexedBitmap>(m, md, u"terrain.bmp"_s, u"terrain_bmp"_s);
    auto rb = std::make_shared<layerbmp::IndexedBitmap>(m, md, u"rivers.bmp"_s, u"rivers_bmp"_s);
    d.maps.insert(u"terrain_bmp"_s, tb);
    d.maps.insert(u"rivers_bmp"_s, rb);
    m.nation_data = std::make_shared<countryhist::NationData>(m);
    m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
    d.maps.insert(u"nations"_s, std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date));
    d.maps.insert(u"oob"_s, std::make_shared<oob::OOBEdits>(m, *m.country_hist, d.date));
    d.coastal = std::vector<bool>(md.coastal.begin(), md.coastal.end());
    s.landsea = std::make_unique<layerbmp::LandSea>(m, md, *tb, *rb);
    return s;
}

static std::vector<std::pair<QString, std::vector<long long>>> sets_of(moddata::Mod &m, MapData &md, document::Document &d)
{
    auto &nat = d.map<countryhist::NationEdits>(u"nations"_s);
    auto &ob = d.map<oob::OOBEdits>(u"oob"_s);
    std::vector<long long> land, caps, army, coast, seas;
    for (const auto &[p, pr] : m.provinces) {
        if (!pr.sea && md.area[p] > 0)
            land.push_back(p);
        if (pr.sea && md.area[p] > 0)
            seas.push_back(p);
    }
    QStringList tags;
    for (const auto &kv : m.countries)
        if (nat.hist.files.contains(kv.first))
            tags << kv.first;
    std::sort(tags.begin(), tags.end());
    for (const QString &t : tags) {
        const OptStr c = nat.state(t).at(u"capital"_s);
        if (c && !c->isEmpty() && py::isdigit(*c) && m.provinces.contains(py::to_int(*c)) && md.area[py::to_int(*c)] > 0)
            caps.push_back(py::to_int(*c));
    }
    QStringList ut(ob.tags_with_units().begin(), ob.tags_with_units().end());
    std::sort(ut.begin(), ut.end());
    for (const QString &t : ut)
        for (const auto &a : ob.state(t).armies)
            if (a.location && m.provinces.contains(*a.location))
                army.push_back(*a.location);
    for (long long p : land)
        if (md.coastal[size_t(p)])
            coast.push_back(p);
    std::vector<long long> reg;
    std::vector<std::pair<QString, moddata::Region>> rs(m.regions.begin(), m.regions.end());
    std::sort(rs.begin(), rs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[rid, r] : rs)
        if (r.provinces.size() >= 3) {
            reg = r.provinces;
            break;
        }
    auto sl = [](const std::vector<long long> &x, size_t a, size_t b) {
        return std::vector<long long>(x.begin() + std::min(a, x.size()), x.begin() + std::min(b, x.size()));
    };
    long long top = 0;
    for (const auto &kv : m.provinces)
        top = std::max(top, kv.first);
    return {{u"capital"_s, sl(caps, 2, 3)}, {u"coast"_s, sl(coast, 10, 11)}, {u"sea"_s, sl(seas, 5, 6)},
            {u"state"_s, reg}, {u"army"_s, sl(army, 0, 2)}, {u"mixed"_s, {land[40], land[41], caps[5], coast[30]}},
            {u"top"_s, {top}}};
}

static QString fixes_txt(const std::vector<oob::Fix> &todo)
{
    QStringList p;
    for (const oob::Fix &f : todo)
        p << (f.what == u"home" ? pyform::L(f.what, f.tag, f.a, f.b) : pyform::L(f.what, f.tag, f.a));
    return pyform::list(p);
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
        Setup s = setup(m);
        auto &md = *s.md;
        auto &d = *s.d;
        QStringList out;
        for (const auto &[name, pids] : sets_of(m, md, d)) {
            out << u"set %1 %2"_s.arg(name, v(pids));
            const auto rf = provdelete::refill(md, pids);
            out << u"  refill %1 %2 %3"_s.arg(rf.idx.size()).arg(hv(rf.idx), hv(rf.nw));
            const auto heirs = provdelete::heirs_of(pids, rf.old, rf.nw);
            std::vector<std::pair<long long, long long>> hs(heirs.begin(), heirs.end());
            std::sort(hs.begin(), hs.end());
            out << u"  heirs "_s + v(hs);
            const auto [caps, todo] = provdelete::followers(d, pids, heirs);
            out << u"  followers %1 %2"_s.arg(v(std::vector<std::pair<QString, std::optional<long long>>>(caps.begin(), caps.end())),
                                              fixes_txt(todo));
            QStringList ms;
            for (const auto &[k, x] : provdelete::script_mentions(m, pids))
                ms << pyform::L(k, x);
            out << u"  mentions "_s + pyform::list(ms);
            for (const QString &line : provdelete::describe(d, md, pids))
                out << u"  | "_s + line;
        }
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int run(const QString &root, const QString &backups, const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    moddata::Mod m(root, BASE, std::nullopt, REPLACE);
    m.load();
    Setup s = setup(m);
    auto &md = *s.md;
    auto &d = *s.d;
    OrderedMap<QString, std::vector<long long>> sets;
    for (const auto &[k, x] : sets_of(m, md, d))
        sets.insert(k, x);
    QStringList out;
    auto &defs = d.map<newprov::ProvinceDefs>(u"definitions"_s);
    const long long n1 = provdelete::delete_provinces(d, md, *sets.get(u"mixed"_s), u"neighbours"_s, s.landsea.get());
    out << u"deleted %1 dirty %2"_s.arg(n1).arg(v(defs.dirty()));
    std::vector<long long> two = *sets.get(u"coast"_s);
    two.insert(two.end(), sets.get(u"sea"_s)->begin(), sets.get(u"sea"_s)->end());
    const long long n2 = provdelete::delete_provinces(d, md, two, u"blank"_s, s.landsea.get());
    out << u"deleted %1 dirty %2"_s.arg(n2).arg(v(defs.dirty()));
    const auto u = d.undo();
    const auto r = d.redo();
    out << u"undo %1 redo %2"_s.arg(v(u), v(r));
    const auto [written, err] = d.save(backups);
    QStringList rel;
    for (const QString &p : written)
        rel << pypath::relpath(p, root).replace(u'\\', u'/');
    std::sort(rel.begin(), rel.end());
    OptStr bdir;
    if (err) {
        bdir = pypath::relpath(*err, backups).replace(u'\\', u'/');
        bdir->replace(QRegularExpression(u"\\d{8}_\\d{6}"_s), u"STAMP"_s);
    }
    out << u"saved %1 backups %2"_s.arg(v(rel), v(bdir));
    std::vector<std::pair<long long, moddata::Rgb>> fs_(m.free_slots.begin(), m.free_slots.end());
    std::sort(fs_.begin(), fs_.end());
    std::vector<std::pair<long long, std::optional<long long>>> hs(m.free_heirs.begin(), m.free_heirs.end());
    std::sort(hs.begin(), hs.end());
    out << u"free %1 heirs %2"_s.arg(v(fs_), v(hs));
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
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
