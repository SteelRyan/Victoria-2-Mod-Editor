// techdata + production against Python, the C++ side of tools/check_techprod.py.
// Usage: med_techprod --dump <out folder> <work folder>   |   med_techprod --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/oob.h"
#include "core/production.h"
#include "core/pypath.h"
#include "core/techdata.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
namespace td = techdata;
namespace pr = production;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString err_name(const std::exception &e)   // the error's Python name: our messages start with it
{
    const QString w = QString::fromUtf8(e.what());
    if (dynamic_cast<const py::ValueError *>(&e))
        return u"ValueError"_s;
    const qsizetype c = w.indexOf(u':');
    return c > 0 ? w.left(c) : u"RuntimeError"_s;
}
static QString tryh(const std::function<QString()> &fn)
{
    try {
        return h(fn());
    } catch (const std::exception &e) {
        return u"!"_s + err_name(e);
    }
}
static QString items_v(const OrderedMap<QString, QString> &m)
{
    QStringList p;
    for (const auto &[k, x] : m)
        p << L(k, x);
    return pyform::list(p);
}
static QString goods_v(const pr::Goods &g)
{
    QStringList p;
    for (const auto &[k, a] : g)
        p << L(k, a);
    return pyform::list(p);
}
static QString opts_v(const td::Kind &k)
{
    if (!k.options)
        return u"None"_s;
    QStringList p;
    for (const auto &[a, b] : *k.options)
        p << L(a, b);
    return pyform::list(p);
}
static QString desc_v(const pr::Desc &d)
{
    QStringList bon;
    for (const pr::Bonus &b : d.bonuses) {
        QStringList conds;
        for (const auto &[a, c] : b.conds)
            conds << L(a, c);
        bon << pyform::list({v(b.index), pyform::list(conds), v(b.value)});
    }
    return pyform::list({v(d.kind), v(d.tmpl), v(d.output), v(d.value), goods_v(d.inputs), goods_v(d.own_eff),
                         v(d.has_own_eff), goods_v(d.efficiency), pyform::list(bon), v(d.farm), v(d.mine), v(d.coastal),
                         v(d.workforce)});
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
    for (const M &md : mods) {
        const QString root = pypath::join(work, md.rel);
        QStringList replace = md.replace;
        if (md.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, md.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        td::TechDB db(m);
        QStringList out{u"techs "_s + v(db.techs()), u"invs "_s + v(db.inventions())};
        QSet<QString> techs;
        for (const QString &k : db.techs())
            techs.insert(k);
        for (const auto &[k, e] : db.entries) {
            const QString t = e.text;
            out << u"e %1 %2 %3 %4 %5 %6"_s.arg(v(k), e.kind, v(e.file),
                                                L(static_cast<long long>(e.span.first), static_cast<long long>(e.span.second)),
                                                h(t))
                       .arg(e.order);
            out << u"  f "_s + items_v(td::fields(t)) + u" y %1"_s.arg(td::year_of(t));
            if (e.kind == u"inv")
                out << u"  refs "_s + v(td::tech_refs(t, techs)) + u' ' + v(td::simple_limit(t, techs));
            out << u"  ed "_s + v(QStringList{tryh([&] { return td::set_value(t, {0}, u"7"_s); }),
                                              tryh([&] { return td::remove_node(t, {0}); }),
                                              tryh([&] { return td::remove_node(t, {-1}); }),
                                              tryh([&] { return td::add_line(t, {}, u"zz_eff"_s, u"0.5"_s); }),
                                              tryh([&] { return td::add_line(t, {0}, u"zz_in"_s, u"1"_s); }),
                                              tryh([&] { return td::add_block(t, {}, u"zz_blk"_s, {u"a = 1"_s, u"b = 2"_s}); }),
                                              tryh([&] { return td::replace_key(t, {}, u"zz_key"_s); })});
            out << u"  chk "_s + v(QStringList{td::check_block(t, k), td::check_block(t, u"zz"_s), td::check_block(t + u'}', k),
                                               td::check_block(py::head(t, -1), k), td::check_block(t + u"\nx = { }"_s, k)});
        }
        td::EffectSchema es(m, db);
        td::ConditionSchema cs(m, db);
        const QStringList keys = es.keys();
        out << u"ekeys "_s + v(keys);
        QStringList mods_(es.modifiers.begin(), es.modifiers.end());
        std::sort(mods_.begin(), mods_.end());
        out << u"estats "_s + v(es.unit_stats) + u' ' + v(mods_) + u' ' + v(es.buildings);
        for (const QString &k : keys) {
            const td::Kind kd = es.kind(k);
            out << u"ek %1 %2 %3 %4"_s.arg(v(k), kd.kind, opts_v(kd), v(es.example(k)));
        }
        QStringList parents{u"army_base"_s, u"rgo_goods_output"_s};
        for (const auto &kv : es.units->types)
            if (parents.size() < 4)
                parents << kv.first;
        for (const QString &parent : parents)
            for (qsizetype i = 0; i < keys.size() && i < 20; ++i)
                out << u"ekp %1 %2 %3"_s.arg(v(parent), v(keys[i]), es.kind(keys[i], parent).kind);
        for (const QString &k : cs.keys()) {
            const td::Kind kd = cs.kind(k);
            out << u"ck %1 %2 %3 %4 %5"_s.arg(v(k), v(cs.name(k)), kd.kind, opts_v(kd), v(cs.example(k)));
        }
        QStringList names;
        for (qsizetype i = 0; i < keys.size() && i < 200; ++i)
            names << td::effect_name(m.loc, keys[i]);
        names << td::pretty(u"a_b"_s) << td::pretty(u""_s) << td::pretty(std::nullopt);
        out << u"names "_s + v(names);
        td::TechEdits te(m, db);
        std::vector<td::OptStr> areas;
        for (const QString &k : te.keys()) {
            const td::OptStr a = te.area_of(k);
            if (std::find(areas.begin(), areas.end(), a) == areas.end())
                areas.push_back(a);
        }
        for (const td::OptStr &a : areas)
            out << u"col %1 %2"_s.arg(v(a), v(te.column(a)));
        QStringList seq;
        for (const auto &[f, s] : te._file_sequences())
            seq << L(pypath::basename(f), s);
        out << u"seq "_s + pyform::list(seq) << u"tdirty "_s + v(te.dirty());
        // --- production
        pr::ProdDB pdb(m);
        pr::ProdEdits pe(m, pdb);
        QStringList tpl, blds, bldt, costs, pops;
        for (const auto &[k, t] : pdb.templates)
            tpl << pyform::list({v(k), v(t.type), v(t.workforce), goods_v(t.efficiency)});
        for (const auto &[a, b] : pdb.buildings)
            blds << L(a, b);
        for (const auto &[k, x] : pdb.building_text)
            bldt << pyform::list({v(k), v(static_cast<long long>(x.start)), v(static_cast<long long>(x.end)), v(h(x.text)), v(x.type)});
        for (const auto &[g, c] : pdb.costs)
            costs << pyform::list({v(g), c.start ? v(static_cast<long long>(*c.start)) : u"None"_s,
                                   v(static_cast<long long>(c.end)), v(c.value)});
        for (const auto &[p, x] : pdb.pop_files)
            pops << pyform::list({v(p), v(pypath::basename(x.path)), v(x.in_mod), v(h(x.text))});
        out << u"porder "_s + v(pdb.order) << u"ptypes "_s + v(pdb.types()) << u"ptpl "_s + v(pe.templates())
            << u"tpl "_s + pyform::list(tpl) << u"blds "_s + pyform::list(blds) << u"bldo "_s + v(pdb.bld_order)
            << u"bldt "_s + pyform::list(bldt) << u"costs "_s + pyform::list(costs) << u"pops "_s + pyform::list(pops);
        for (const QString &k : pdb.order) {
            const pr::ProdEntry &e = *pdb.entries.get(k);
            out << u"p %1 %2 %3 %4"_s.arg(v(k), L(static_cast<long long>(e.span.first), static_cast<long long>(e.span.second)),
                                          h(e.text), e.tmpl ? u"True"_s : u"False"_s);
            out << u"  d "_s + desc_v(pr::describe(pdb, e.text));
            out << u"  i "_s + desc_v(pe.info(k));
        }
        for (const QString &b : pdb.bld_order) {
            const pr::BuildingDesc d = pr::describe_building(pdb.building_text.get(b)->text);
            out << u"b %1 %2"_s.arg(v(b), pyform::list({v(d.type), goods_v(d.cost), v(d.production_type), v(d.time),
                                                        v(d.max_level), v(d.extra)}));
        }
        for (const QString &t : pe.templates()) {
            const auto [a, b] = pe.users_of(t);
            out << u"users %1 %2"_s.arg(v(t), L(a, b));
        }
        QStringList pc;
        for (const auto &[g, c] : pe.costs())
            pc << L(g, c);
        out << u"pcosts "_s + pyform::list(pc) + u' ' + v(pe.buildings());
        QStringList goods;
        for (const auto &kv : pdb.costs)
            goods << kv.first;
        for (const QString &p : pe.poptypes()) {
            const QString t = pe.pop_text(p);
            const auto nd = pe.needs(p);
            QStringList nl;
            for (const auto &[k, lst] : nd)
                nl << pyform::list({v(k), goods_v(lst)});
            out << u"need %1 %2"_s.arg(v(p), pyform::list(nl));
            const pr::Goods &life = *nd.get(u"life_needs"_s);
            const QString first = !life.empty() ? life[0].first : (!goods.isEmpty() ? goods[0] : u"grain"_s);
            out << u"  ns "_s + v(QStringList{tryh([&] { return pr::need_set(t, u"life_needs"_s, first, u"2.5"_s); }),
                                              tryh([&] { return pr::need_set(t, u"life_needs"_s, first, std::nullopt); }),
                                              tryh([&] { return pr::need_set(t, u"everyday_needs"_s, u"zz_good"_s, u"1"_s); }),
                                              tryh([&] { return pr::need_set(t, u"zz_needs"_s, u"zz_good"_s, u"1"_s); })});
        }
        out << u"newkey "_s + v(QStringList{pe.new_key(u"Glass Factory!"_s), pe.new_key(u""_s), pe.new_key(u"___"_s),
                                            pe.new_key(!pdb.order.isEmpty() ? pdb.order[0] : u"x"_s)});
        for (const QString &kind : pr::KINDS)
            out << u"tt %1 %2"_s.arg(kind, h(pe.template_text(kind, u"zz_new"_s, u"zz_out"_s)));
        QStringList fk;
        for (const QString &x : pdb.types())
            if (pr::describe(pdb, pdb.entries.get(x)->text).kind == u"factory" && fk.size() < 6)
                fk << x;
        fk << u"zz_none"_s;
        for (const QString &k : fk) {
            const auto bt = pe.building_text(k);
            out << u"bt %1 %2"_s.arg(v(k), bt ? L(bt->first, h(bt->second)) : u"None"_s);
        }
        out << u"pdirty "_s + v(pe.dirty());
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static void say(const QString &s)
{
    const QByteArray b = (s + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
}
static QString rels(const std::optional<QStringList> &paths, const QString &root)
{
    QStringList r;
    for (const QString &p : paths.value_or(QStringList()))
        r << pypath::relpath(p, root);
    return v(r);
}
static QString pyb(bool b) { return b ? u"True"_s : u"False"_s; }

static int edit(const QString &root, const QString &backups, const QString &base)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    m.nation_data = std::make_shared<countryhist::NationData>(m);
    td::TechDB db(m);
    td::TechEdits te(m, db);
    const QStringList techs = db.techs();
    const QString t0 = techs[0], t1 = techs[3];
    te.set(t0, td::set_value(te.text(t0), {1}, u"1777"_s));
    te.set(t1, td::add_line(te.text(t1), {}, u"zz_effect"_s, u"0.25"_s));
    const td::OptStr a = te.area_of(t1);
    te.add(u"zz_new_tech"_s, te.file_of(t1), u"zz_new_tech = {\n\tarea = %1\n\tyear = 1840\n\tcost = 3600\n}"_s.arg(a.value_or(u"None"_s)));
    const QStringList col = te.column(a);
    std::vector<std::pair<QString, QString>> same;
    for (qsizetype i = 0; i + 1 < col.size(); ++i)
        if (td::year_of(te.text(col[i])) == td::year_of(te.text(col[i + 1])))
            same.emplace_back(col[i], col[i + 1]);
    if (!same.empty()) {
        const bool moved = te.move_to(a, same[0].first, same[0].second);
        say(u"same-year [("_s + v(same[0].first) + u", "_s + v(same[0].second) + u")] "_s + pyb(moved));
    } else {
        say(u"same-year [] None"_s);
    }
    const QStringList invs = db.inventions();
    te.add(u"zz_new_inv"_s, db.entries.get(invs[0])->file,
           u"zz_new_inv = {\n\tlimit = { %1 = 1 }\n\tchance = { base = 1 }\n}"_s.arg(t1), u"inv"_s);
    te.remove(invs[1]);
    say(u"dirty "_s + v(te.dirty()));
    te.restore(te.snapshot());
    say(u"save "_s + rels(te.save(backups), root));
    say(u"after %1 %2 %3"_s.arg(v(te.dirty())).arg(db.entries.size()).arg(m.nation_data->techs.size()));
    te.remove(db.techs()[5]);
    try {
        say(u"removed tech "_s + v(te.dirty()));
    } catch (const std::exception &e) {
        say(u"removed tech !"_s + err_name(e));
    }
    // --- production
    pr::ProdDB pdb(m);
    pr::ProdEdits pe(m, pdb);
    QStringList facts;
    for (const QString &k : pdb.types())
        if (pr::describe(pdb, pdb.entries.get(k)->text).kind == u"factory")
            facts << k;
    const QString k0 = facts[0];
    {
        const auto r = td::root(pe.text(k0));
        qsizetype vi = -1;
        for (size_t i = 0; i < r->kids.size(); ++i)
            if (r->kids[i].has_key && r->kids[i].key == u"value") {
                vi = qsizetype(i);
                break;
            }
        pe.set(k0, td::set_value(pe.text(k0), {vi}, u"1.75"_s));
    }
    const QString nk = pe.new_key(u"Zz Widget Factory"_s);
    pe.add(nk, pe.template_text(u"factory"_s, nk, u"zz_widgets"_s));
    const auto bt = pe.building_text(nk);
    pe.new_buildings.insert(nk, *bt);
    pe.remove(facts.last());
    QStringList goods;
    for (const auto &kv : pdb.costs)
        goods << kv.first;
    pe.set_other(u"cost"_s, goods[0], u"33.5"_s);
    pe.set_other(u"cost"_s, goods[1], pdb.costs.get(goods[1])->value);
    const QString b0 = pdb.bld_order[0];
    {
        QString t = pdb.building_text.get(b0)->text;
        const qsizetype at = t.indexOf(u'{');
        if (at >= 0)
            t.replace(at, 1, u"{\n\tzz_line = 1"_s);
        pe.set_other(u"bld"_s, b0, t);
    }
    const QString p0 = pe.poptypes()[0];
    pe.set_other(u"need"_s, p0, pr::need_set(pe.pop_text(p0), u"luxury_needs"_s, u"zz_good"_s, u"0.5"_s));
    say(u"pdirty "_s + v(pe.dirty()));
    pe.restore(pe.snapshot());
    say(u"psave "_s + rels(pe.save(backups), root));
    say(u"pafter %1 %2 %3"_s.arg(v(pe.dirty()), pyb(pdb.entries.contains(nk)), pyb(pdb.entries.contains(facts.last()))));
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 5 && a[1] == u"--edit")
            return edit(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
