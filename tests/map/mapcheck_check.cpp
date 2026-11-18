// mapcheck.run_checks against Python, the C++ side of tools/check_mapcheck.py.  Usage: med_mapcheck --dump <out> <work>
#include "../core/nopopup.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/history.h"
#include "core/localisation.h"
#include "core/oob.h"
#include "core/production.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "map/mapcheck.h"
#include "map/newprov.h"
#include "map/positions_geo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace mapcheck;
using mapview::MapData;

static QString h(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

static QString val(const Val &x)
{
    if (x.is_none())
        return u"None"_s;
    if (const auto *b = std::get_if<bool>(&x.v))
        return *b ? u"True"_s : u"False"_s;
    if (const auto *i = std::get_if<long long>(&x.v))
        return QString::number(*i);
    if (const auto *d = std::get_if<double>(&x.v))
        return py::repr_float(*d);
    if (const auto *s = std::get_if<QString>(&x.v))
        return py::repr_str(*s);
    QStringList p;
    for (const Val &y : x.list())
        p << val(y);
    return u'[' + p.join(u", "_s) + u']';
}
static QString opt(const OptStr &s) { return s ? py::repr_str(*s) : u"None"_s; }

static QStringList cats_lines(const QString &tag, const std::vector<Category> &cats)
{
    QStringList out;
    for (const Category &c : cats) {
        const QString fix = c.fix ? u"%1|%2|%3"_s.arg(c.fix->label, c.fix->what, val(c.fix->arg)) : u"None"_s;
        out << u"%1 cat %2 %3 %4 tab %5 fix %6 one %7 note %8 items %9"_s.arg(
            tag, c.key, py::repr_str(c.title), c.severity, opt(c.tab), fix, opt(c.fix_one), h(c.note),
            QString::number(c.items.size()));
        for (const Item &it : c.items) {
            QStringList ps;
            for (long long p : it.pids)
                ps << QString::number(p);
            const QString pt = it.point ? u"(%1, %2)"_s.arg(py::repr_float(it.point->first), py::repr_float(it.point->second))
                                        : u"None"_s;
            out << u"  %1 | [%2] | %3 | %4"_s.arg(it.text, ps.join(u", "_s), pt, val(it.data));
        }
    }
    return out;
}

static int dump(const QString &out_dir, const QString &work, bool crash = false)
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
    // --crash (C++ only, 2026-11-08): the test mods that crashed / hung the game (reference\crash tests 2026-11-08 —
    // made in the work-area game by variants.py); each must show its problem in the Errors tab
    const std::vector<M> crash_mods = {{u"zzc_building_prod"_s, u"Victoria 2\\mod\\zzc_building_prod"_s, {}, false},
                                       {u"zzc_goods_missing"_s, u"Victoria 2\\mod\\zzc_goods_missing"_s, {}, false},
                                       {u"zzc_countries_nofile"_s, u"Victoria 2\\mod\\zzc_countries_nofile"_s, {}, false},
                                       {u"zzc_pop_prov"_s, u"Victoria 2\\mod\\zzc_pop_prov"_s, {}, false}};
    const QStringList crash_want = {u"crash_production"_s, u"crash_production"_s, u"crash_countries"_s, u"pop_provinces"_s};
    int crash_fails = 0, crash_i = 0;
    for (const M &mm : crash ? crash_mods : mods) {
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
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        d.maps.insert(u"positions"_s, std::make_shared<positions::PositionsFile>(m, md.h));
        d.maps.insert(u"provinces_bmp"_s, std::make_shared<provbmp::ProvinceBitmap>(m, md));
        auto defs = std::make_shared<newprov::ProvinceDefs>(m, md, *d.hist);
        d.maps.insert(u"definitions"_s, defs);
        d.maps.insert(u"terrain_bmp"_s, std::make_shared<layerbmp::IndexedBitmap>(m, md, u"terrain.bmp"_s, u"terrain_bmp"_s));
        d.maps.insert(u"rivers_bmp"_s, std::make_shared<layerbmp::IndexedBitmap>(m, md, u"rivers.bmp"_s, u"rivers_bmp"_s));
        d.maps.insert(u"islands"_s, std::make_shared<store::StoreOf<history::IslandFlags>>(*d.hist));
        d.maps.insert(u"loc"_s, std::make_shared<localisation::LocEdits>(m));
        m.nation_data = std::make_shared<countryhist::NationData>(m);
        m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
        auto nn = std::make_shared<countryhist::NewNations>(m, *m.country_hist);
        auto ne = std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date);
        nn->nations = ne.get();
        d.maps.insert(u"new_nations"_s, nn);
        d.maps.insert(u"nations"_s, ne);
        d.maps.insert(u"oob"_s, std::make_shared<oob::OOBEdits>(m, *m.country_hist, d.date));
        d.maps.insert(u"diplomacy"_s, std::make_shared<diplomacy::DiploEdits>(m, d.date));
        auto store = std::make_shared<scriptstore::ScriptStore>(m);
        d.maps.insert(u"scripts"_s, store);
        techdata::TechDB tdb(m);
        production::ProdDB pdb(m);
        d.maps.insert(u"techs"_s, std::make_shared<techdata::TechEdits>(m, tdb));
        d.maps.insert(u"production"_s, std::make_shared<production::ProdEdits>(m, pdb));
        d.maps.insert(u"owners_top"_s, std::make_shared<history::OwnersOnTop>(d));
        d.maps.insert(u"start_blocks"_s, std::make_shared<history::StartBlocks>(d, m));
        d.coastal = std::vector<bool>(md.coastal.begin(), md.coastal.end());
        QSet<long long> sea;
        for (const auto &[p, pr] : m.provinces)
            if (pr.sea)
                sea.insert(p);
        adjacency::AdjGeometry geo(md, sea);
        std::vector<std::uint8_t> valid = md.sea;
        valid[0] = 0;
        for (long long t : d.map<adjacency::AdjacencyFile>(u"adjacency"_s).skip_coast())
            if (0 < t && t < static_cast<long long>(valid.size()))
                valid[size_t(t)] = 0;
        GeometryCache gc;
        if (crash) {
            const QString want = crash_want[crash_i++];
            long long n = -1;
            QString first;
            for (const Category &cc : run_checks(m, md, d, gc, valid, &geo))
                if (cc.key == want) {
                    n = static_cast<long long>(cc.items.size());
                    first = cc.items.empty() ? QString() : cc.items[0].text;
                }
            std::printf("%s %-22s %-17s %lld item(s)  %s\n", n > 0 ? "ok  " : "FAIL", qPrintable(mm.shortn),
                        qPrintable(want), n, qUtf8Printable(first));
            crash_fails += n <= 0;
            continue;
        }
        QStringList out = cats_lines(u"clean"_s, run_checks(m, md, d, gc, valid, &geo));
        if (mm.shortn == u"ferrum_mare") {
            d.last_error = u"First line\n\n   \nSecond line"_s;
            std::vector<long long> land;
            for (const auto &[p, pr] : m.provinces)
                if (!pr.sea)
                    land.push_back(p);
            const newprov::Plan pl = newprov::plan(d, *defs, u"Neu Land"_s, false, land[3]);
            newprov::create_province(d, pl.pid, pl.rec, pl.rid, pl.continent, pl.climate, pl.want);
            d.map<history::IslandFlags>(u"islands"_s).marked = {land[5], land[6]};
            store->set_text(u"history/wars/zz_check.txt"_s,
                            u"name = \"Check war\"\nadd_attacker = ZZZ\nadd_defender = QQ1\nadd_defender = ZZZ\n"_s);
            QStringList tags;
            for (const auto &kv : m.countries)
                if (ne->hist.files.contains(kv.first))
                    tags << kv.first;
            std::sort(tags.begin(), tags.end());
            ne->edit(tags[4], [](countryhist::NationState &st) {
                st[u"capital"_s] = u"0"_s;
                st.upper_house = {};
                st.upper_house.insert(u"liberal"_s, u"30"_s);
                st.upper_house.insert(u"conservative"_s, u"20"_s);
                st.cultures << u"zz_culture"_s;
            });
            out += cats_lines(u"planted"_s, run_checks(m, md, d, gc, valid, &geo));
        }
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %6lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
    }
    if (crash)
        std::printf("%s: %d failed\n", crash_fails ? "FAIL" : "PASS", crash_fails);
    return crash ? (crash_fails ? 1 : 0) : 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 3 && a[1] == u"--crash")
            return dump(QDir::temp().filePath(u"mapcheck_crash"_s), a[2], true);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
