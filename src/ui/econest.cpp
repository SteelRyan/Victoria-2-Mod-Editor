// econest.py — see econest.h.
#include "ui/econest.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/maplook.h"
#include "core/moddata.h"
#include "core/production.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "grid/pyset.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"
#include "ui/prodpanel.h"
#include "ui/textselect.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using script::Node;

namespace econest {

const double GROWTH_ROOM = 1.5;                      // an RGO starts with room for this many times its workers

double _num(const OptStr &v, double d)
{
    if (!v)
        return d;
    const std::optional<double> f = py::to_float(*v);
    return f ? *f : d;
}

static double _numn(const Node &n, double d = 0.0) { return n.is_block ? d : _num(n.value, d); }

// ---------------------------------------------------------------------------------------------- the types
Types::Types(production::ProdEdits &prods)
{
    OrderedMap<QString, Node> raw;
    QStringList templates;
    for (const auto &[k, e] : prods.db.entries)
        if (e.tmpl)
            templates << k;
    QStringList keys = prods.keys();
    keys << templates;
    for (const QString &key : keys) {
        script::Nodes nodes;
        try {
            nodes = script::parse(prods.text(key));
        } catch (const std::exception &) {
            continue;
        }
        for (const Node &x : nodes)
            if (x.is_block && x.has_key && !x.key.isEmpty()) {
                raw.insert(key, x);
                break;
            }
    }
    for (const auto &[key, n] : raw) {
        if (templates.contains(key))
            continue;
        OptStr tk;
        Type d = _read(n, tk);
        const Node *tpl = raw.get(tk.value_or(QString()));
        if (tpl) {
            OptStr ignore;
            const Type b = _read(*tpl, ignore);
            // (Python: a line the type leaves empty — None, [], {}, 0.0, "" or False — is the template's)
            if (d.type.isEmpty()) d.type = b.type;
            if (d.workforce == 0.0) d.workforce = b.workforce;
            if (d.employees.empty()) d.employees = b.employees;
            if (d.output.isEmpty()) d.output = b.output;
            if (d.value == 0.0) d.value = b.value;
            if (d.inputs.isEmpty()) d.inputs = b.inputs;
            if (d.bonus.empty()) d.bonus = b.bonus;
            if (!d.farm) d.farm = b.farm;
            if (!d.mine) d.mine = b.mine;
            if (d.owner_pop.isEmpty()) d.owner_pop = b.owner_pop;
        }
        types.insert(key, d);
    }
    for (const auto &[k, d] : types)
        if (d.type == u"rgo" && !d.output.isEmpty())
            rgo_of.insert(d.output, k);
}

Type Types::_read(const Node &n, OptStr &tpl)
{
    Type d;
    tpl.reset();
    for (const Node &c : n.kids) {
        const QString k = c.has_key ? c.key.toLower() : QString();
        if ((k == u"template" || k == u"type") && !c.is_block) {
            if (k == u"template")
                tpl = c.value;
            else
                d.type = c.value;
        } else if (k == u"workforce") {
            d.workforce = _numn(c);
        } else if (k == u"output_goods" && !c.is_block) {
            d.output = c.value;
        } else if (k == u"value") {
            d.value = _numn(c);
        } else if (k == u"farm" || k == u"mine") {
            // (str(c.value) of a block is its list's text — never "yes")
            const bool yes = !c.is_block && c.value.toLower() == u"yes";
            (k == u"farm" ? d.farm : d.mine) = yes;
        } else if (k == u"owner" && c.is_block) {
            d.owner_pop.clear();
            for (const Node &x : c.kids)
                if (x.has_key && x.key == u"poptype" && !x.is_block) {
                    d.owner_pop = x.value;
                    break;
                }
        } else if (k == u"input_goods" && c.is_block) {
            d.inputs.clear();
            for (const Node &x : c.kids)
                if (x.has_key && !x.key.isEmpty() && !x.is_block)
                    d.inputs.insert(x.key, _num(x.value));
        } else if (k == u"employees" && c.is_block) {
            for (const Node &e : c.kids)
                if (e.is_block) {
                    OrderedMap<QString, QString> f;
                    for (const Node &x : e.kids)
                        if (x.has_key && !x.key.isEmpty() && !x.is_block)
                            f.insert(x.key.toLower(), x.value);
                    const QString *am = f.get(u"amount"_s), *mu = f.get(u"effect_multiplier"_s);
                    d.employees.push_back({f.value(u"poptype"_s), am ? _num(*am, 1.0) : 1.0, f.value(u"effect"_s),
                                           mu ? _num(*mu, 1.0) : 1.0});
                }
        } else if (k == u"bonus" && c.is_block) {
            OptStr g;
            bool have = false;
            double val = 0.0;
            for (const Node &x : c.kids) {
                if (x.has_key && x.key == u"trigger" && x.is_block) {
                    g.reset();
                    have = false;
                    for (const Node &y : x.kids)
                        if (y.has_key && y.key == u"trade_goods_in_state") {
                            have = true;
                            g = y.is_block ? OptStr() : OptStr(y.value);
                            break;
                        }
                } else if (x.has_key && x.key == u"value") {
                    val = _numn(x);
                }
            }
            if (have && g && !g->isEmpty())
                d.bonus.emplace_back(*g, val);
        }
    }
    return d;
}

// ---------------------------------------------------------------------------------------------- modifiers
const QStringList MODS = {u"farm_rgo_eff"_s, u"mine_rgo_eff"_s, u"rgo_output"_s, u"rgo_throughput"_s,
                          u"factory_output"_s, u"factory_throughput"_s, u"factory_input"_s};
const QStringList GOOD_MODS = {u"rgo_goods_output"_s, u"rgo_goods_throughput"_s, u"factory_goods_output"_s,
                               u"factory_goods_throughput"_s, u"factory_goods_input"_s};
const QSet<QString> NOT_EFFECTS = {u"limit"_s, u"chance"_s, u"ai_chance"_s, u"allow"_s};

static void _walk(const std::vector<Node> &nodes, Mods &out)
{
    for (const Node &c : nodes) {
        const QString k = c.has_key ? c.key.toLower() : QString();
        if (k.isEmpty() || NOT_EFFECTS.contains(k))
            continue;
        if (c.is_block) {
            if (GOOD_MODS.contains(k)) {
                Amounts &d = out.goods[k];
                for (const Node &x : c.kids)
                    if (x.has_key && !x.key.isEmpty() && !x.is_block)
                        d.insert(x.key, d.value(x.key, 0.0) + _num(x.value));
            } else {
                _walk(c.kids, out);                  // (an invention's  effect = { … } )
            }
        } else if (MODS.contains(k)) {
            out.num.insert(k, out.num.value(k, 0.0) + _num(c.value));
        } else if (k == u"activate_building") {
            out.buildings.insert(c.value);
        }
    }
}

Mods modifiers(const Node *node)
{
    Mods out;
    if (node && node->is_block)
        _walk(node->kids, out);
    return out;
}

// ---------------------------------------------------------------------------------------------- bonuses
Bonuses::Bonuses(document::Document &doc_) : doc(doc_)
{
    if (doc.maps.contains(u"techs"_s)) {
        techdata::TechEdits &te = doc.map<techdata::TechEdits>(u"techs"_s);
        QSet<QString> keys;
        for (const QString &k : te.keys())
            keys.insert(k);
        for (const QString &k : te.keys()) {
            try {
                tech.insert(k, modifiers(techdata::root(te.text(k)).get()));
            } catch (const std::exception &) {
                tech.insert(k, Mods());
            }
        }
        for (const QString &k : te.inventions()) {
            try {
                const QString text = te.text(k);
                Mods fx = modifiers(techdata::root(text).get());
                if (fx.any())
                    inv.emplace_back(k, techdata::tech_refs(text, keys), std::move(fx));
            } catch (const std::exception &) {
            }
        }
    }
    try {
        auto &ss = doc.map<scriptstore::ScriptStore>(u"scripts"_s);
        for (const Node &n : script::parse(ss.text(u"common/nationalvalues.txt"_s)))
            if (n.has_key && !n.key.isEmpty() && n.is_block)
                nv.insert(n.key, modifiers(&n));
    } catch (const std::exception &) {
    }
}

// the nation's technologies in the order Python walks set(st["techs"]): its set was filled in the order the file
// names them (src keeps that order), then copied
static std::vector<QString> tech_order(const countryhist::NationState &st)
{
    grid::PySet<QString> s;
    QSet<QString> seen;
    for (const auto &[k, _v] : st.src)
        if (k.first == u"tech" && st.techs.contains(k.second) && !seen.contains(k.second)) {
            s.add(k.second);
            seen.insert(k.second);
        }
    QStringList rest;
    for (const QString &t : st.techs)
        if (!seen.contains(t))
            rest << t;
    rest.sort();
    for (const QString &t : rest)
        s.add(t);
    return grid::PySet<QString>::copy_of(s).items();
}

const Bonuses::Got &Bonuses::of(const QString &tag)
{
    auto it = _of.find(tag);
    if (it != _of.end())
        return it->second;
    std::optional<countryhist::NationState> st;
    try {
        if (doc.maps.contains(u"nations"_s))
            st = doc.map<countryhist::NationEdits>(u"nations"_s).state(tag);
    } catch (const std::exception &) {
        st.reset();
    }
    std::vector<const Mods *> src;
    static const Mods NONE;
    std::vector<QString> have;
    QSet<QString> has;
    if (st) {
        have = tech_order(*st);
        for (const QString &k : have) {
            has.insert(k);
            const auto t = tech.constFind(k);
            src.push_back(t == tech.constEnd() ? &NONE : &*t);
        }
    }
    for (const auto &[k, need, fx] : inv) {
        bool all = !need.isEmpty();
        for (const QString &q : need)
            if (!has.contains(q)) {
                all = false;
                break;
            }
        if ((st && st->inventions.contains(k)) || all)
            src.push_back(&fx);
    }
    if (st) {
        const OptStr v = st->at(u"nationalvalue"_s);
        if (v && !v->isEmpty()) {
            const Mods *m = nv.get(*v);
            src.push_back(m ? m : &NONE);
        }
    }
    Got got;
    got.techs = static_cast<long long>(have.size());
    for (const Mods *d : src) {
        for (const auto &[k, v] : d->num)
            got.m.num.insert(k, got.m.num.value(k, 0.0) + v);
        for (const auto &[k, gd] : d->goods) {
            Amounts &dd = got.m.goods[k];
            for (const auto &[g, x] : gd)
                dd.insert(g, dd.value(g, 0.0) + x);
        }
        got.m.buildings.unite(d->buildings);
    }
    return _of.emplace(tag, std::move(got)).first->second;
}

double Bonuses::get(const QString &tag, const QString &key, const OptStr &good)
{
    const Got &b = of(tag);
    if (!good)
        return b.m.num.value(key, 0.0);
    const Amounts *d = b.m.goods.get(key);
    return d ? d->value(*good, 0.0) : 0.0;
}

// ---------------------------------------------------------------------------------------------- terrain
const Terrain &province_terrain(document::Document &doc, moddata::Mod &mod, mapview::MapData *md)
{
    // {province: (terrain type, {its modifiers})}: the terrain most of the province's pixels have in terrain.bmp
    // (ASSUMED to be how the game picks it) — kept on the document until terrain.bmp or the map changes
    static std::map<const document::Document *, std::pair<std::pair<long long, const void *>, Terrain>> cache;
    static const Terrain EMPTY;
    if (!md || !doc.maps.contains(u"terrain_bmp"_s))
        return EMPTY;
    layerbmp::IndexedBitmap &tb = doc.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s);
    const std::pair<long long, const void *> key{tb.version, &md->idmap};
    auto it = cache.find(&doc);
    if (it != cache.end() && it->second.first == key)
        return it->second.second;
    Terrain out;
    try {
        tb.load();
        if (tb.data && tb.data->h() == md->idmap.h() && tb.data->w() == md->idmap.w()) {
            const OrderedMap<long long, QString> types = layerbmp::terrain_types(mod);
            OrderedMap<QString, Amounts> cats;
            auto &ss = doc.map<scriptstore::ScriptStore>(u"scripts"_s);
            for (const Node &n : script::parse(ss.text(u"map/terrain.txt"_s)))
                if (n.has_key && n.key == u"categories" && n.is_block)
                    for (const Node &c : n.kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block) {
                            Amounts a;
                            for (const Node &x : c.kids)
                                if (x.has_key && !x.key.isEmpty() && !x.is_block)
                                    a.insert(x.key.toLower(), _num(x.value));
                            cats.insert(c.key, a);
                        }
            const long long n = md->n;
            std::vector<std::int64_t> cnt(static_cast<size_t>(n) * 256, 0);
            const auto &ids = md->idmap.flat();
            const auto &T = tb.data->flat();
            for (size_t i = 0; i < ids.size(); ++i) {
                const std::int64_t at = std::int64_t(ids[i]) * 256 + T[i];
                if (at < std::int64_t(cnt.size()))
                    cnt[size_t(at)] += 1;
            }
            for (const auto &[pid, pr] : mod.provinces) {
                if (pr.sea || pid >= n || pid < 0)
                    continue;
                const std::int64_t *row = cnt.data() + pid * 256;
                int major = 0;                       // (argmax: the first of the most)
                for (int t = 1; t < 256; ++t)
                    if (row[t] > row[major])
                        major = t;
                const QString *t = types.get(major);
                if (t && !t->isEmpty())
                    out.insert(pid, {*t, cats.value(*t)});
            }
        }
    } catch (const std::exception &) {
        out.clear();
    }
    auto &slot = cache[&doc];
    slot = {key, std::move(out)};
    return slot.second;
}

// ---------------------------------------------------------------------------------------------- the estimate
static QString pystr(const OptStr &s) { return s ? *s : u"None"_s; }

Estimate estimate(document::Document &doc, moddata::Mod &mod, mapview::MapData *md)
{
    Estimate res;
    if (!doc.maps.contains(u"production"_s)) {
        res.notes << u"The economy files could not be read."_s;
        return res;
    }
    production::ProdEdits &prods = doc.map<production::ProdEdits>(u"production"_s);
    Types T(prods);
    Bonuses B(doc);
    const Terrain &terrain = province_terrain(doc, mod, md);
    QHash<QString, double> _pc;
    auto price = [&](const QString &g) {             // (each good's price looked up once per estimate)
        auto it = _pc.constFind(g);
        if (it != _pc.constEnd())
            return *it;
        const double v = prods.cost(g);
        _pc.insert(g, v);
        return v;
    };
    OrderedMap<QString, QString> by_building;
    for (const auto &[key, _t] : T.types) {
        const OptStr b = prods.building_of(key);
        if (b && !b->isEmpty())
            by_building.insert(*b, key);
    }
    // the pops that work in farms and mines: whoever a farm or mine template employs
    QStringList rgo_pops;
    {
        QSet<QString> s;
        for (const auto &[_k, t] : T.types)
            if (t.type == u"rgo")
                for (const Employee &e : t.employees)
                    s.insert(e.pop);
        rgo_pops = QStringList(s.begin(), s.end());
        rgo_pops.sort();
    }
    std::vector<std::pair<QString, const Type *>> art_types;
    for (const auto &[k, t] : T.types)
        if (t.type == u"artisan" && !t.output.isEmpty() && t.value != 0.0)
            art_types.emplace_back(k, &t);
    QStringList art_pops;
    {
        QSet<QString> s;
        for (const auto &[_k, t] : art_types)
            s.insert(t->owner_pop.isEmpty() ? u"artisans"_s : t->owner_pop);
        art_pops = QStringList(s.begin(), s.end());
        art_pops.sort();
        if (art_pops.isEmpty())
            art_pops << u"artisans"_s;
    }
    QSet<QString> late;                              // goods a nation has to unlock first ( available_from_start = no )
    try {
        auto &ss = doc.map<scriptstore::ScriptStore>(u"scripts"_s);
        for (const Node &grp : script::parse(ss.text(u"common/goods.txt"_s))) {
            if (!grp.is_block)
                continue;
            for (const Node &c : grp.kids) {
                if (!(c.has_key && !c.key.isEmpty() && c.is_block))
                    continue;
                for (const Node &x : c.kids)
                    if (x.has_key && x.key == u"available_from_start" && !x.is_block &&
                        py::lower(py::strip(x.value)) == u"no") {
                        late.insert(c.key);
                        break;
                    }
            }
        }
    } catch (const std::exception &) {
        late.clear();
    }
    OrderedMap<QString, QString> factory_of;
    for (const auto &[b, k] : by_building) {
        const Type &t = *T.types.get(k);
        if (!t.output.isEmpty())
            factory_of.insert(t.output, b);
    }
    QStringList notes;
    OrderedMap<std::pair<QString, QString>, std::vector<long long>> groups;   // (owner, state) -> [province]
    for (const auto &[pid, pr] : mod.provinces) {
        if (pr.sea)
            continue;
        const history::ProvState &st = doc.prov_state(pid);
        if (!st.owner || st.owner->isEmpty())
            continue;
        groups[{*st.owner, pr.region && !pr.region->isEmpty() ? *pr.region : u"#"_s + QString::number(pid)}].push_back(pid);
    }
    auto pops_of = [&](long long pid) {
        OrderedMap<QString, long long> out;
        for (const auto &p : doc.province_pops(pid))
            out[p.type] += p.size;
        return out;
    };
    std::map<QString, std::vector<std::pair<QString, const Type *>>> art_open;   // nation -> what its artisans can pick
    auto open_types = [&](const QString &owner) -> const std::vector<std::pair<QString, const Type *>> & {
        auto it = art_open.find(owner);
        if (it != art_open.end())
            return it->second;
        const QSet<QString> &unlocked = B.of(owner).m.buildings;
        std::vector<std::pair<QString, const Type *>> got;
        for (const auto &kt : art_types)
            if (!late.contains(kt.second->output) ||
                (factory_of.contains(kt.second->output) && unlocked.contains(*factory_of.get(kt.second->output))))
                got.push_back(kt);
        return art_open.emplace(owner, std::move(got)).first->second;
    };
    for (const auto &[key, pids] : groups) {
        const QString &owner = key.first, &rid = key.second;
        const moddata::Region *reg = mod.regions.get(rid);
        const QString sname = reg && !reg->name.isEmpty() ? reg->name : rid;
        OrderedMap<QString, long long> state_pops;
        QSet<QString> goods_here;
        std::vector<std::tuple<long long, QString, long long>> facts;
        for (long long pid : pids) {
            const history::ProvState st = doc.prov_state(pid);   // (a copy: the document's cache may grow)
            const OrderedMap<QString, long long> pp = pops_of(pid);
            for (const auto &[k, v] : pp)
                state_pops[k] += v;
            if (st.trade_goods && !st.trade_goods->isEmpty()) {
                const QString good = *st.trade_goods;
                goods_here.insert(good);
                const Type *t = T.types.get(T.rgo_of.value(good));
                if (!t) {
                    notes << u"%1 (#%2): no farm / mine type makes %3"_s.arg(mod.provinces.get(pid)->name,
                                                                            QString::number(pid), good);
                } else {
                    const double wf = t->workforce != 0.0 ? t->workforce : 40000.0;
                    long long workers = 0;
                    for (const QString &k : rgo_pops)
                        workers += pp.value(k, 0);
                    const QString kind = t->mine && !t->farm ? u"mine"_s : u"farm"_s;
                    const double eff = B.get(owner, kind + u"_rgo_eff"_s);
                    const auto *tr = terrain.get(pid);
                    const QString tname = tr ? tr->first : QString();
                    const double teff = tr ? tr->second.value(kind + u"_rgo_eff"_s, 0.0) : 0.0;
                    const double thr = B.get(owner, u"rgo_throughput"_s) + B.get(owner, u"rgo_goods_throughput"_s, good) + eff;
                    const double outb = eff + B.get(owner, u"rgo_output"_s) + B.get(owner, u"rgo_goods_output"_s, good) + teff;
                    const double mult = std::max(0.0, 1.0 + thr) * std::max(0.0, 1.0 + outb);
                    const double out = t->value * double(workers) / wf * mult;
                    RgoRow r;
                    r.owner = owner;
                    r.state = sname;
                    r.pid = pid;
                    r.province = mod.provinces.get(pid)->name;
                    r.good = good;
                    r.workers = workers;
                    for (const QString &k : rgo_pops)
                        if (pp.value(k, 0))
                            r.kinds << k;
                    if (r.kinds.isEmpty())
                        for (const Employee &e : t->employees)
                            r.kinds << e.pop;
                    r.size = workers ? std::max(1LL, static_cast<long long>(std::ceil(GROWTH_ROOM * double(workers) / wf))) : 1;
                    r.base = t->value * double(workers) / wf;
                    r.mult = mult;
                    r.thr = thr;
                    r.outb = outb;
                    r.terrain = tname;
                    r.out = out;
                    r.money = out * price(good);
                    res.rgo.push_back(std::move(r));
                }
            }
            for (const history::Factory &f : st.factories)
                facts.emplace_back(pid, pystr(f.building), f.level ? f.level : 1);
            long long crowd = 0;
            for (const QString &k : art_pops)
                crowd += pp.value(k, 0);
            if (crowd) {
                const auto &types_open = open_types(owner);
                if (!types_open.empty()) {           // artisans: each pop picks ONE product — the expectation
                    const double share = 1.0 / double(types_open.size());
                    ArtRow a;
                    for (const auto &[_k, t] : types_open) {
                        const double k_ = double(crowd) / (t->workforce != 0.0 ? t->workforce : 10000.0) * share;
                        Part pt;
                        pt.good = t->output;
                        pt.cap = t->value * k_;
                        for (const auto &[g, am] : t->inputs)
                            pt.need.insert(g, am * k_);
                        a.parts.push_back(std::move(pt));
                    }
                    a.owner = owner;
                    a.state = sname;
                    a.pid = pid;
                    a.province = mod.provinces.get(pid)->name;
                    a.people = crowd;
                    a.choices = static_cast<long long>(types_open.size());
                    res.artisans.push_back(std::move(a));
                }
            }
        }
        if (facts.empty())
            continue;
        // the state's factory workers shared among its factories by their places
        struct Slot {
            long long pid;
            QString b;
            long long lvl;
            const Type *t;
            Amounts places;
        };
        std::vector<Slot> slots;
        for (const auto &[pid, b, lvl] : facts) {
            const Type *t = T.types.get(by_building.value(b));
            if (!t) {
                notes << u"%1: the building %2 has no production type"_s.arg(sname, b);
                continue;
            }
            const double wf = t->workforce != 0.0 ? t->workforce : 10000.0;
            Amounts places;
            for (const Employee &e : t->employees)
                places.insert(e.pop, double(lvl) * wf * e.amount);
            slots.push_back({pid, b, lvl, t, places});
        }
        Amounts want;
        for (const Slot &s : slots)
            for (const auto &[k, v] : s.places)
                want.insert(k, want.value(k, 0.0) + v);
        for (const Slot &s : slots) {
            const Type &t = *s.t;
            const double wf = t.workforce != 0.0 ? t.workforce : 10000.0;
            const double lw = double(s.lvl) * wf;
            Amounts got;
            for (const auto &[k, v] : s.places) {
                const double w = want.value(k, 0.0);
                got.insert(k, w != 0.0 ? std::min(v, double(state_pops.value(k, 0)) * v / w) : 0.0);
            }
            py::Sum employed;
            for (const auto &[_k, v] : got)
                employed.add(v);
            double thr = 0.0, bonus = 0.0;
            for (const Employee &e : t.employees) {
                if (e.effect == u"output")
                    bonus += lw != 0.0 ? e.mult * got.value(e.pop, 0.0) / lw : 0.0;
                else
                    thr += e.mult * got.value(e.pop, 0.0) / wf;
            }
            QStringList local;
            py::Sum lb;
            for (const auto &[g, v] : t.bonus)
                if (goods_here.contains(g)) {
                    local << g;
                    lb.add(v);
                }
            bonus += lb.value();
            const QString &g_out = t.output;
            const double nat_thr = B.get(owner, u"factory_throughput"_s) + B.get(owner, u"factory_goods_throughput"_s, g_out);
            const double nat_out = B.get(owner, u"factory_output"_s) + B.get(owner, u"factory_goods_output"_s, g_out);
            const double scale = thr * std::max(0.0, 1.0 + nat_thr);
            const double out = t.value * scale * std::max(0.0, 1.0 + bonus + nat_out);
            const double nat_in = B.get(owner, u"factory_input"_s) + B.get(owner, u"factory_goods_input"_s, g_out);
            FacRow r;
            for (const auto &[g, a] : t.inputs)
                r.need.insert(g, a * scale * std::max(0.0, 1.0 + nat_in));
            r.owner = owner;
            r.state = sname;
            r.pid = s.pid;
            r.building = s.b;
            r.level = s.lvl;
            r.good = g_out;
            r.places = s.places;
            r.got = got;
            r.full = lw != 0.0 ? employed.value() / lw : 0.0;
            r.cap = out;
            r.local = local;
            r.bonus = bonus + nat_out;
            for (const auto &[k, _v] : s.places)
                r.idle.insert(k, std::max(0.0, double(state_pops.value(k, 0)) - want.value(k, 0.0)));
            res.factories.push_back(std::move(r));
        }
    }
    // --- nobody makes more than his inputs allow: where the world makes less of a good than its factories and
    // artisans want as an input, each gets his share of it
    Amounts raw;
    for (const RgoRow &r : res.rgo)
        raw.insert(r.good, raw.value(r.good, 0.0) + r.out);
    std::vector<const Part *> makers;
    std::vector<Part> fparts;
    fparts.reserve(res.factories.size());
    for (const FacRow &r : res.factories)
        fparts.push_back({r.good, r.cap, r.need});
    for (const Part &p : fparts)
        makers.push_back(&p);
    for (const ArtRow &r : res.artisans)
        for (const Part &p : r.parts)
            makers.push_back(&p);
    Amounts demand;
    for (const Part *m : makers)
        for (const auto &[g, a] : m->need)
            demand.insert(g, demand.value(g, 0.0) + a);
    Amounts avail;
    auto least = [&](const Amounts &need) {          // min([avail.get(i, 1.0) for i in need] or [1.0])
        bool first = true;
        double s = 1.0;
        for (const auto &[i, _a] : need) {
            const double v = avail.value(i, 1.0);
            if (first || v < s)
                s = v;
            first = false;
        }
        return s;
    };
    for (int round = 0; round < 10; ++round) {
        Amounts supply = raw;
        for (const Part *m : makers) {
            const double s = least(m->need);
            supply.insert(m->good, supply.value(m->good, 0.0) + m->cap * s);
        }
        Amounts nw;
        for (const auto &[g, d] : demand)
            nw.insert(g, d > 0 ? std::min(1.0, supply.value(g, 0.0) / d) : 1.0);
        bool done = true;
        for (const auto &[g, v] : nw)
            if (!(std::fabs(v - avail.value(g, 1.0)) < 1e-4)) {
                done = false;
                break;
            }
        avail = nw;
        if (done)
            break;
    }
    auto shortest = [&](const Amounts &need) -> std::pair<double, QString> {   // min of (avail, good) pairs
        if (need.isEmpty())
            return {1.0, QString()};
        bool first = true;
        std::pair<double, QString> best;
        for (const auto &[i, _a] : need) {
            const std::pair<double, QString> c{avail.value(i, 1.0), i};
            if (first || c < best)
                best = c;
            first = false;
        }
        return best;
    };
    for (FacRow &r : res.factories) {
        const auto [s, why] = shortest(r.need);
        r.avail = s;
        r.shortg = s < 0.999 ? why : QString();
        r.out = r.cap * s;
        r.inputs.clear();
        for (const auto &[g, a] : r.need)
            r.inputs.insert(g, a * s);
        r.money_out = r.out * price(r.good);
        py::Sum mi;
        for (const auto &[g, a] : r.inputs)
            mi.add(a * price(g));
        r.money_in = mi.value();
    }
    for (ArtRow &r : res.artisans) {
        for (const Part &p : r.parts) {
            const double s = shortest(p.need).first;
            r.made.insert(p.good, r.made.value(p.good, 0.0) + p.cap * s);
            r.could.insert(p.good, r.could.value(p.good, 0.0) + p.cap);
            for (const auto &[i, a] : p.need)
                r.inputs.insert(i, r.inputs.value(i, 0.0) + a * s);
        }
        py::Sum m;
        for (const auto &[g, v] : r.made)
            m.add(v * price(g));
        r.money = m.value();
        r.parts.clear();
    }
    notes.removeDuplicates();
    std::sort(notes.begin(), notes.end());
    res.notes = notes;
    for (const auto &[g, v] : avail)
        if (v < 0.999)
            res.avail.insert(g, v);
    res.demand = demand;
    return res;
}

// ---------------------------------------------------------------------------------------------- totals
OrderedMap<QString, Tot> goods_totals(const Estimate &data)
{
    OrderedMap<QString, Tot> out;
    auto pv = [](Tot &d, long long pid) -> Vals & {
        Vals *p = d.p.get(pid);
        if (!p) {
            d.p.insert(pid, Vals{0.0, 0.0, 0.0, 0.0});
            p = d.p.get(pid);
        }
        return *p;
    };
    auto add = [](Amounts &dd, const QString &k, double v) { dd.insert(k, dd.value(k, 0.0) + v); };
    auto addp = [](OrderedMap<long long, double> &dd, long long k, double v) { dd.insert(k, dd.value(k, 0.0) + v); };
    for (const RgoRow &r : data.rgo) {
        Tot &d = out[r.good];
        add(d.rgo, r.owner, r.out);
        addp(d.prov, r.pid, r.out);
        pv(d, r.pid)[0] += r.out;
    }
    for (const FacRow &r : data.factories) {
        {
            Tot &d = out[r.good];
            add(d.fac, r.owner, r.out);
            addp(d.prov, r.pid, r.out);
            pv(d, r.pid)[1] += r.out;
        }
        for (const auto &[g, a] : r.inputs) {
            Tot &e = out[g];
            add(e.use, r.owner, a);
            pv(e, r.pid)[3] += a;
        }
    }
    for (const ArtRow &r : data.artisans) {
        for (const auto &[g, v] : r.made) {
            Tot &d = out[g];
            add(d.art, r.owner, v);
            addp(d.prov, r.pid, v);
            pv(d, r.pid)[2] += v;
        }
        for (const auto &[g, a] : r.inputs) {
            Tot &e = out[g];
            add(e.art_use, r.owner, a);
            pv(e, r.pid)[3] += a;
        }
    }
    return out;
}

const Tot NOTHING;
const std::vector<Level> LEVELS = {{u"continent"_s, u"By continent"_s, u"continents"_s},
                                   {u"nation"_s, u"By nation"_s, u"nations"_s},
                                   {u"state"_s, u"By state"_s, u"states"_s},
                                   {u"province"_s, u"By province"_s, u"provinces"_s}};
const QStringList LEVEL_KEYS = {u"continent"_s, u"nation"_s, u"state"_s, u"province"_s};
const QStringList MAKERS = {u"rgo"_s, u"fac"_s, u"art"_s};

QString plural_of(const QString &level)
{
    for (const Level &l : LEVELS)
        if (l.key == level)
            return l.plural;
    return QString();
}

static const Amounts &maker(const Tot &d, const QString &k)
{
    return k == u"rgo" ? d.rgo : k == u"fac" ? d.fac : d.art;
}

static double values_sum(const Amounts &a)
{
    py::Sum s;
    for (const auto &[_k, v] : a)
        s.add(v);
    return s.value();
}

double made(const Tot &d, const QString &tag)
{
    py::Sum s;
    for (const QString &k : MAKERS)
        s.add(tag.isEmpty() ? values_sum(maker(d, k)) : maker(d, k).value(tag, 0.0));
    return s.value();
}

double used(const Tot &d, const QString &tag)
{
    if (!tag.isEmpty())
        return d.use.value(tag, 0.0) + d.art_use.value(tag, 0.0);
    return values_sum(d.use) + values_sum(d.art_use);
}

const Rgb SEA{18, 26, 38}, EMPTY_LAND{44, 46, 52};
const double LOW = 0.25;                             // where on the colour scale the smallest maker stands

Rgb shade(double v, double top)
{
    if (v <= 0 || top <= 0)
        return EMPTY_LAND;
    return maplook::ramp(LOW + (1.0 - LOW) * std::pow(std::min(v, top) / top, 0.5));
}

QString fmt(double v, int digits)
{
    return py::fmt_commas(v, std::fabs(v) < 1000 || digits == 0 ? digits : 0);
}

QString pct(double v)
{
    return py::fmt_f(100.0 * v, v < 0.1 ? 1 : 0) + u" %"_s;
}

static QString plus_f(double v)                      // "%+.2f" % v if abs(v) < 1000 else "{:+,.0f}".format(v)
{
    QString s = std::fabs(v) < 1000 ? py::fmt_f(v, 2) : py::fmt_commas(v, 0);
    if (!s.startsWith(u'-'))
        s.prepend(u'+');
    return s;
}

static QString plus_i(long long v) { return (v >= 0 ? u"+"_s : QString()) + QString::number(v); }   // "%+d"
static QString commas(long long v) { return py::fmt_commas(double(v), 0); }   // "{:,}".format(int)
static QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }
static QString esc(const QString &s) { return QString(s).replace(u"&"_s, u"&amp;"_s).replace(u"<"_s, u"&lt;"_s); }
static int lvi(const QString &lv) { return int(LEVEL_KEYS.indexOf(lv)); }
static QString chop1(const QString &s) { return s.left(s.size() - 1); }   // s[:-1]

// ---------------------------------------------------------------------------------------------- small widgets
Legend::Legend(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(26);
    setMinimumWidth(260);
}

void Legend::set(const QString &mid_, const QString &top_)
{
    mid = mid_;
    top = top_;
    update();
}

void Legend::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(font());
    f.setPixelSize(12);
    p.setFont(f);
    const QFontMetrics fm = p.fontMetrics();
    const int x0 = 78;
    const int x1 = width() - std::max(70, fm.horizontalAdvance(top) + 14);
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(qc(EMPTY_LAND));
    p.drawRoundedRect(6, 5, 16, 16, 3, 3);
    p.setPen(QColor(theme::INK_DIM));
    p.drawText(26, 18, u"nothing"_s);
    const int n = 48;
    const double w = double(x1 - x0) / double(n);
    p.setPen(Qt::NoPen);
    for (int k = 0; k < n; ++k) {
        p.setBrush(qc(maplook::ramp(LOW + (1.0 - LOW) * (k + 0.5) / n)));
        p.drawRect(int(x0 + k * w), 5, int(w) + 1, 16);
    }
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(x0, 5, x1 - x0, 16);
    if (!mid.isEmpty()) {                            // (the colours go by the square root: the middle is a quarter)
        const int xm = int(py::floordiv(static_cast<long long>(x0 + x1), 2LL));
        p.drawLine(xm, 5, xm, 21);
        p.setPen(QColor(12, 12, 12));
        p.drawText(xm + 4, 18, mid);
    }
    p.setPen(QColor(theme::INK));
    p.drawText(x1 + 6, 18, top);
}

void BarDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const QVariant v = index.data(Qt::UserRole + 2);
    if (v.isValid() && v.toDouble() != 0.0) {
        const QRect r = opt.rect.adjusted(3, 4, -3, -4);
        p->save();
        QColor c(theme::GOLD);
        c.setAlpha(80);
        p->setPen(QPen(QColor(10, 10, 10, 200), 1));
        p->setBrush(c);
        p->drawRect(r.left(), r.top(), std::max(3, int(r.width() * std::min(1.0, v.toDouble()))), r.height());
        p->restore();
    }
    QStyledItemDelegate::paint(p, opt, index);
}

bool _Row::operator<(const QTreeWidgetItem &other) const
{
    const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
    const QVariant a = data(c, Qt::UserRole), b = other.data(c, Qt::UserRole);
    if (a.isValid() && b.isValid())
        return a.toDouble() < b.toDouble();
    return py::lower(text(c)) < py::lower(other.text(c));
}

void _EconMap::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && ids)
        Q_EMIT doubleClicked(int(at(e->position())));
}

void _EconMap::mousePressEvent(QMouseEvent *e)
{
    _rpress = e->button() == Qt::RightButton ? std::optional<QPointF>(e->position()) : std::nullopt;
    regionimport::SourceMap::mousePressEvent(e);
}

void _EconMap::mouseReleaseEvent(QMouseEvent *e)
{
    const std::optional<QPointF> rp = _rpress;
    _rpress.reset();
    regionimport::SourceMap::mouseReleaseEvent(e);
    if (rp && e->button() == Qt::RightButton && (e->position() - *rp).manhattanLength() < 5)
        Q_EMIT rightClicked();
}

// ---------------------------------------------------------------------------------------------- the page
static QPushButton *seg_button(const QString &text)
{
    auto *sb = new QPushButton(text);
    sb->setObjectName(u"seg"_s);
    sb->setCheckable(true);
    return sb;
}

EconWorkspace::EconWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(8);
    auto *seg = new QHBoxLayout();
    seg->setSpacing(0);
    const QStringList pages = {u"Production chains"_s, u"Output estimate"_s};
    for (int i = 0; i < 2; ++i) {
        QPushButton *sb = seg_button(pages[i]);      // the two pages of Economy
        sb->setChecked(i == 1);
        if (i) {
            connect(sb, &QPushButton::clicked, this, [sb] { sb->setChecked(true); });
        } else {
            chains_btn = sb;
            connect(sb, &QPushButton::clicked, this, [this, sb] {
                sb->setChecked(false);
                Q_EMIT chainsRequested();
            });
        }
        seg->addWidget(sb);
    }
    seg->addStretch(1);
    lay->addLayout(seg);
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(widgets::label(u"Output estimate"_s, u"title"_s, false));
    row->addSpacing(18);
    const std::map<QString, QString> tips = {
        {u"continent"_s, u"Every continent: what its provinces make (map/continent.txt)"_s},
        {u"nation"_s, u"Every nation: what its provinces make"_s},
        {u"state"_s, u"Every state (a state split between nations: one for each owner)"_s},
        {u"province"_s, u"Every province: its farm or mine, its factories, its artisans"_s}};
    for (const Level &l : LEVELS) {                  // for the whole page: the map and "who makes it"
        QPushButton *sb = seg_button(l.title);
        sb->setChecked(l.key == by);
        sb->setToolTip(tips.at(l.key));
        const QString kk = l.key;
        connect(sb, &QPushButton::clicked, this, [this, kk] { _set_by(kk); });
        row->addWidget(sb);
        by_btn[l.key] = sb;
    }
    row->addStretch(1);
    lay->addLayout(row);
    focus_bar = new QWidget();                       // "Showing only: …" — clear that the numbers are narrowed
    auto *fb = new QHBoxLayout(focus_bar);
    fb->setContentsMargins(10, 4, 6, 4);
    focus_lbl = new QLabel(QString());
    focus_lbl->setTextFormat(Qt::RichText);
    fb->addWidget(focus_lbl, 1);
    focus_x = new QPushButton(u"✕ Back to the whole world"_s);
    focus_x->setToolTip(u"The goods list for the whole world again"_s);
    connect(focus_x, &QPushButton::clicked, this, [this] { set_focus(std::nullopt); });
    fb->addWidget(focus_x);
    focus_bar->setObjectName(u"focusBar"_s);
    focus_bar->setAttribute(Qt::WA_StyledBackground, true);
    focus_bar->setStyleSheet(u"#focusBar { border: 2px solid %1; border-radius: 6px; }"_s.arg(theme::GOLD));
    focus_bar->hide();
    lay->addWidget(focus_bar);
    summary = new QLabel(QString());
    summary->setObjectName(u"sub"_s);
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::RichText);
    lay->addWidget(summary);
    split = new QSplitter(Qt::Vertical);
    split->setChildrenCollapsible(false);
    auto *top = new QSplitter(Qt::Horizontal);
    top->setChildrenCollapsible(false);
    auto box = [](QVBoxLayout *&bl, QLabel *&h) {
        auto *b = new QWidget();
        bl = new QVBoxLayout(b);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        h = new QLabel(QString());
        h->setObjectName(u"sub"_s);
        h->setWordWrap(true);
        h->setTextFormat(Qt::RichText);
        bl->addWidget(h);
        return b;
    };
    // --- the goods: one number each
    QVBoxLayout *bl;
    QWidget *b = box(bl, goods_head);
    goods = _tree({u"Good"_s, u"A day"_s, u"Largest maker"_s, u"Used"_s, u"Left"_s, u"Worth £"_s}, {0, 2});
    const QStringList gtips = {
        QString(), u"What is made of it a day (farms, mines and factories together)"_s, QString(),
        u"What the factories need of it a day as an input"_s,
        u"Made minus used by the factories: what is left for the pops and for sale (below 0: more is needed than is made)"_s,
        u"What is made of it a day, at the good's price"_s};
    for (int c = 0; c < gtips.size(); ++c)
        goods->headerItem()->setToolTip(c, gtips[c]);
    goods->setIconSize(QSize(22, 22));
    goods->setItemDelegateForColumn(2, new BarDelegate(goods));
    goods->sortByColumn(5, Qt::DescendingOrder);
    connect(goods, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *) { _good_picked(it); });
    bl->addWidget(goods, 1);
    top->addWidget(b);
    auto *right = new QSplitter(Qt::Vertical);
    right->setChildrenCollapsible(false);
    // --- the map of the picked good
    b = box(bl, g_title);
    auto *r2 = new QHBoxLayout();
    r2->setSpacing(0);
    legend = new Legend();
    r2->addWidget(legend, 1);
    bl->addLayout(r2);
    map = new _EconMap();
    connect(map, &_EconMap::doubleClicked, this, [this](int pid) { _map_double(pid); });
    connect(map, &_EconMap::rightClicked, this, [this] { detach(); });
    map->numbers = false;
    map->pick_outline = true;
    map->spread_extra = true;
    map->setMinimumSize(420, 220);
    map->group_of = [this](long long pid, Qt::KeyboardModifiers) {
        const std::optional<Unit> u = unit_of(pid, by);
        const std::vector<long long> *got = u ? members(by).get(*u) : nullptr;
        return got && !got->empty() ? *got : std::vector<long long>{pid};
    };
    connect(map, &regionimport::SourceMap::changed, this, [this] { _map_picked(); });
    connect(map, &regionimport::SourceMap::hovered, this, [this](int pid) { _hover(pid); });
    bl->addWidget(map, 1);
    hover = widgets::label(QString(), u"sub"_s, false);
    hover->setMinimumWidth(10);
    hover->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    bl->addWidget(hover);
    right->addWidget(b);
    // --- who makes it
    b = box(bl, rank_head);
    auto *gr = new QHBoxLayout();
    gr->setSpacing(0);
    gr->addWidget(widgets::label(u"Group the rows:"_s, u"sub"_s, false));
    gr->addSpacing(8);
    const std::vector<std::pair<QString, QString>> gb = {{QString(), u"Off — highest first"_s},
                                                         {u"continent"_s, u"by continent"_s},
                                                         {u"nation"_s, u"by nation"_s},
                                                         {u"state"_s, u"by state"_s}};
    for (const auto &[k, text] : gb) {
        QPushButton *sb = seg_button(text);
        sb->setChecked(k == group);
        sb->setToolTip(!k.isEmpty() ? u"The same rows, under what they belong to — each group with its own total, still "
                                      u"highest first inside"_s
                                    : u"One list, highest first"_s);
        const QString kk = k;
        connect(sb, &QPushButton::clicked, this, [this, kk] { _set_group(kk); });
        gr->addWidget(sb);
        group_btn[k] = sb;
    }
    gr->addStretch(1);
    bl->addLayout(gr);
    rank = _tree({u"Nation"_s, u"Belongs to"_s, u"A day"_s, u"Share"_s, u"Farms, mines"_s, u"Factories"_s, u"Artisans"_s,
                  u"Used"_s, u"Left"_s},
                 {0, 1});
    const std::vector<std::pair<int, QString>> rtips = {
        {2, u"What it makes of the good a day"_s},
        {3, u"Its share of what the whole world makes"_s},
        {4, u"Made by its farms and mines (RGOs)"_s},
        {5, u"Made by its factories"_s},
        {6, u"What its artisans are EXPECTED to make: every artisan pop picks one product, by chance and profit — here "
            u"they are spread evenly over what they can make"_s},
        {7, u"What its factories and artisans use of the good a day"_s},
        {8, u"Made minus used: below 0 it has to bring it in"_s}};
    for (const auto &[c, tip] : rtips)
        rank->headerItem()->setToolTip(c, tip);
    rank->setItemDelegateForColumn(3, new BarDelegate(rank));
    rank->setSortingEnabled(false);                  // (sorted by _show_good itself: far quicker)
    QHeaderView *hd = rank->header();
    hd->setSectionsClickable(true);
    hd->setSortIndicatorShown(true);
    hd->setSortIndicator(2, Qt::DescendingOrder);
    connect(hd, &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) { _show_good(); });
    connect(rank, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *) { _rank_picked(it); });
    connect(rank, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) { _rank_double(it); });
    rank->setMinimumHeight(145);                     // (never squeezed away by the close look below)
    bl->addWidget(rank, 1);
    rank_hint = rank_head;                           // (how to use it: the heading's tooltip — no line of its own)
    right->addWidget(b);
    right->setStretchFactor(0, 6);
    right->setStretchFactor(1, 4);
    right->setSizes({520, 330});
    top->addWidget(right);
    top->setStretchFactor(0, 4);
    top->setStretchFactor(1, 8);
    top->setSizes({590, 1120});
    split->addWidget(top);
    // --- the close look: what is inside what is picked, in plain lines — the problems first, in colour
    close_box = new QWidget();
    close_box->setMinimumHeight(90);
    auto *cl = new QVBoxLayout(close_box);
    cl->setContentsMargins(0, 4, 0, 0);
    cl->setSpacing(4);
    auto *ch = new QHBoxLayout();
    close_head = new QLabel(QString());
    close_head->setTextFormat(Qt::RichText);
    close_head->setWordWrap(true);
    ch->addWidget(close_head, 1);
    close_map = new QPushButton(u"Jump to Provinces & states tab ▸"_s);
    close_map->setToolTip(u"Opens the Provinces & states tab with its provinces picked (a state: the States sub-tab)"_s);
    connect(close_map, &QPushButton::clicked, this, [this] {
        QList<long long> pids(_close_pids.begin(), _close_pids.end());
        const QString lv = _sel ? _sel->first : focus ? focus->first : QString();
        Q_EMIT selectRequested(pids, lv);
    });
    ch->addWidget(close_map);
    close_x = new QPushButton(u"✕"_s);
    close_x->setToolTip(u"Nothing picked: the whole world again (also: click the sea, right-click the map, Esc)"_s);
    connect(close_x, &QPushButton::clicked, this, [this] { detach(); });
    ch->addWidget(close_x);
    cl->addLayout(ch);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setFrameShape(QFrame::NoFrame);
    close_text = new QLabel(QString());
    close_text->setTextFormat(Qt::RichText);
    close_text->setWordWrap(true);
    close_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    close_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sc->setWidget(close_text);
    cl->addWidget(sc, 1);
    split->addWidget(close_box);
    close_box->hide();
    lay->addWidget(split, 1);
    notes = new QLabel(QString());
    notes->setWordWrap(true);
    notes->setStyleSheet(u"QLabel { color: %1; font-weight: 600; }"_s.arg(theme::WARN));
    notes->hide();
    lay->addWidget(notes);
    lay->addWidget(widgets::label(
        u"An estimate from the mod's own numbers, checked against saves of the real game. A farm or mine: the good's "
        u"value × workers ÷ 40 000 × the nation's bonuses (its technologies, the inventions it starts with, its "
        u"national value) and the terrain's. A factory: value × craftsmen ÷ its workforce × their multiplier, with the "
        u"bonuses — as if every place were filled and every input bought (in the game's first month they made about "
        u"85 % of that). Artisans: spread evenly over the products they can make (each pop picks one, by chance and "
        u"profit — good by good the game can be half or double). Pending edits count."_s,
        u"faint"_s));
}

QTreeWidget *EconWorkspace::_tree(const QStringList &heads, const std::vector<int> &wide)
{
    // wide: the columns that take the room left (names); the others are as wide as their numbers
    auto *t = new QTreeWidget();
    t->setColumnCount(int(heads.size()));
    t->setHeaderLabels(heads);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    t->setTextElideMode(Qt::ElideRight);
    t->setSortingEnabled(true);
    QHeaderView *hd = t->header();
    hd->setStretchLastSection(false);
    for (int i = 0; i < heads.size(); ++i)
        hd->setSectionResizeMode(i, std::find(wide.begin(), wide.end(), i) != wide.end() ? QHeaderView::Stretch
                                                                                           : QHeaderView::ResizeToContents);
    return t;
}

// ------------------------------------------------------------------ data
QString EconWorkspace::name_of(const QString &tag) const
{
    const moddata::Country *c = mod.countries.get(tag);
    return c && !c->name.isEmpty() ? c->name : tag;
}

QString EconWorkspace::good_name(const QString &g) const
{
    OptStr t;
    if (doc && doc->maps.contains(u"loc"_s))
        t = doc->map<localisation::LocEdits>(u"loc"_s).text(g);
    else if (const QString *s = mod.loc.get(g))
        t = *s;
    return t && !t->isEmpty() ? *t : QString(g).replace(u'_', u' ');
}

void EconWorkspace::refresh(document::Document *d)
{
    doc = d;
    const std::tuple<long long, size_t, const void *> sig{d->pending_count(), d->undo_stack.size(), d};
    if (_sig && *_sig == sig && data)
        return;
    _sig = sig;
    data = estimate(*d, mod, md);
    tot = goods_totals(*data);
    _own.clear();
    _prov_of.clear();
    _cont.clear();
    _mem.clear();
    _names.clear();
    if (d->maps.contains(u"continent"_s))
        for (const auto &[name, pids] : d->map<mapfiles::MembershipFile>(u"continent"_s).lists)
            for (long long p : pids)
                if (!_cont.contains(p))
                    _cont.insert(p, name);
    for (const auto &[pid, pr] : mod.provinces)
        if (!pr.sea) {
            const OptStr o = d->prov_state(pid).owner;
            if (o && !o->isEmpty()) {
                _own.insert(pid, *o);
                _prov_of[*o].push_back(pid);
            }
        }
    Amounts owners;
    for (const RgoRow &r : data->rgo)
        owners.insert(r.owner, owners.value(r.owner, 0.0) + r.money);
    for (const FacRow &r : data->factories)
        owners.insert(r.owner, owners.value(r.owner, 0.0) + r.money_out);
    for (const ArtRow &r : data->artisans)
        owners.insert(r.owner, owners.value(r.owner, 0.0) + r.money);
    worth = owners;                                  // nation -> what it makes a day, in £
    if (focus && !(focus->first != u"province" ? members(focus->first).contains(focus->second)
                                               : _own.contains(focus->second.toLongLong())))
        focus.reset();                               // (it is gone: a province given away, a state emptied …)
    _fill();
}

// ------------------------------------------------------------------ the focus: what the goods list is of
QString EconWorkspace::_tag() const { return focus && focus->first == u"nation" ? focus->second : QString(); }

static std::vector<long long> py_set_order(const std::vector<long long> &list)   // set(list), walked
{
    grid::PySet<long long> s;
    for (long long p : list)
        s.add(p);
    return s.items();
}

std::optional<std::vector<long long>> EconWorkspace::_fpids()
{
    if (!focus)
        return std::nullopt;
    const auto &[lv, u] = *focus;
    if (lv == u"province")
        return std::vector<long long>{u.toLongLong()};
    const std::vector<long long> *m = members(lv).get(u);
    return py_set_order(m ? *m : std::vector<long long>{});
}

QString EconWorkspace::focus_name()
{
    if (!focus)
        return u"the world"_s;
    const auto [lv, u] = *focus;
    const QString b = belongs(lv, u);
    return unit_name(lv, u) + (b.isEmpty() ? QString() : u" ("_s + b + u")"_s);
}

void EconWorkspace::set_focus(const std::optional<Pick> &f)
{
    focus = f;
    _fill();
}

// ------------------------------------------------------------------ continent / nation / state / province
std::optional<Unit> EconWorkspace::unit_of(long long pid, const QString &level)
{
    if (level == u"province")
        return QString::number(pid);
    if (level == u"continent") {
        const QString *c = _cont.get(pid);
        return c ? std::optional<Unit>(*c) : std::nullopt;
    }
    const QString *o = _own.get(pid);
    if (!o)
        return std::nullopt;
    if (level == u"nation")
        return *o;
    const moddata::Province *pr = mod.provinces.get(pid);
    return *o + u"|"_s + (pr && pr->region && !pr->region->isEmpty() ? *pr->region : u"#"_s + QString::number(pid));
}

const OrderedMap<Unit, std::vector<long long>> &EconWorkspace::members(const QString &level)
{
    auto it = _mem.find(level);
    if (it != _mem.end())
        return it->second;
    OrderedMap<Unit, std::vector<long long>> got;
    for (const auto &[pid, _o] : _own)
        if (const auto u = unit_of(pid, level))
            got[*u].push_back(pid);
    return _mem.emplace(level, std::move(got)).first->second;
}

QString EconWorkspace::unit_name(const QString &level, const Unit &u)
{
    const QString k = level + QChar(0x1f) + u;
    const auto it = _names.constFind(k);
    if (it != _names.constEnd())
        return *it;
    const QString got = _unit_name(level, u);
    _names.insert(k, got);
    return got;
}

QString EconWorkspace::_unit_name(const QString &level, const Unit &u)
{
    auto tr = [this](const QString &k) -> QString {
        if (doc && doc->maps.contains(u"loc"_s))
            return doc->map<localisation::LocEdits>(u"loc"_s).text(k).value_or(QString());
        return mod.loc.value(k);
    };
    if (level == u"province") {
        const moddata::Province *pr = mod.provinces.get(u.toLongLong());
        return u"%1 (#%2)"_s.arg(pr ? pr->name : u"?"_s, u);
    }
    if (level == u"nation")
        return name_of(u);
    if (level == u"continent") {
        const QString t = tr(u);
        return !t.isEmpty() ? t : py::title(QString(u).replace(u'_', u' '));
    }
    const QString rid = u.mid(u.indexOf(u'|') + 1);
    const moddata::Region *reg = mod.regions.get(rid);
    QString nm;
    if (reg && !reg->name.isEmpty())
        nm = reg->name;
    else {
        const QString t = tr(rid);
        nm = !t.isEmpty() ? t : rid;
    }
    if (nm == rid) {                                 // no name in the mod: named after its capital, as in the game
        const std::vector<long long> *mm = members(u"state"_s).get(u);
        QSet<long long> mine;
        if (mm)
            for (long long p : *mm)
                mine.insert(p);
        std::optional<long long> cap;
        if (reg)
            for (long long p : reg->provinces)
                if (mine.contains(p)) {
                    cap = p;
                    break;
                }
        if (!cap && !mine.isEmpty())
            cap = *std::min_element(mine.begin(), mine.end());
        const moddata::Province *pr = cap ? mod.provinces.get(*cap) : nullptr;
        nm = pr && !pr->name.isEmpty() ? pr->name : rid;
    }
    return nm;
}

QString EconWorkspace::belongs(const QString &level, const Unit &u)
{
    const QString k = u"b"_s + QChar(0x1f) + level + QChar(0x1f) + u;
    const auto it = _names.constFind(k);
    if (it != _names.constEnd())
        return *it;
    const QString got = _belongs(level, u);
    _names.insert(k, got);
    return got;
}

QString EconWorkspace::_belongs(const QString &level, const Unit &u)
{
    // what a row belongs to, in words: a state's nation; a province's state and nation
    std::optional<long long> p0;
    if (level == u"province")
        p0 = u.toLongLong();
    else if (const std::vector<long long> *m = members(level).get(u); m && !m->empty())
        p0 = m->front();
    const QString *o = p0 ? _own.get(*p0) : nullptr;
    if (level == u"state")
        return o ? name_of(*o) : QString();
    if (level == u"province") {
        const std::optional<Unit> s = p0 ? unit_of(*p0, u"state"_s) : std::nullopt;
        return s ? unit_name(u"state"_s, *s) + u" · "_s + name_of(o ? *o : QString()) : QString();
    }
    return QString();
}

OrderedMap<Unit, Vals> EconWorkspace::by_unit(const Tot &d, const QString &level, const QSet<long long> *pids)
{
    OrderedMap<Unit, Vals> out;
    for (const auto &[pid, v] : d.p) {
        if (pids && !pids->contains(pid))
            continue;
        const std::optional<Unit> u = unit_of(pid, level);
        if (!u)
            continue;
        Vals *acc = out.get(*u);
        if (!acc) {
            out.insert(*u, Vals{0.0, 0.0, 0.0, 0.0});
            acc = out.get(*u);
        }
        for (int i = 0; i < 4; ++i)
            (*acc)[i] += v[i];
    }
    return out;
}

void EconWorkspace::set_nation(const QString &tag)
{
    set_focus(tag.isEmpty() ? std::nullopt : std::optional<Pick>(Pick{u"nation"_s, tag}));
}

void EconWorkspace::show_good(const QString &g)
{
    for (int i = 0; i < goods->topLevelItemCount(); ++i)
        if (goods->topLevelItem(i)->data(0, Qt::UserRole + 1).toString() == g) {
            goods->setCurrentItem(goods->topLevelItem(i));
            return;
        }
}

// ------------------------------------------------------------------ the goods
void EconWorkspace::_num(QTreeWidgetItem *it, int c, double v, const QString &text)
{
    it->setText(c, text);
    it->setData(c, Qt::UserRole, v);
    it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
}

static const Tot &tot_of(const OrderedMap<QString, Tot> &tot, const QString &g)
{
    const Tot *d = tot.get(g);
    return d ? *d : NOTHING;
}

void EconWorkspace::_fill()
{
    if (!data)
        return;
    const std::optional<std::vector<long long>> fp = _fpids();
    QSet<long long> fps;
    if (fp)
        for (long long p : *fp)
            fps.insert(p);
    const bool tag = fp.has_value();                 // (narrowed: to a continent, a nation, a state or a province)
    production::ProdEdits *prods =
        doc->maps.contains(u"production"_s) ? &doc->map<production::ProdEdits>(u"production"_s) : nullptr;
    auto price = [prods](const QString &g) { return prods ? prods->cost(g) : 0.0; };
    focus_bar->setVisible(tag);
    if (tag)
        focus_lbl->setText(u"<b>Showing only %1</b> — the goods list, the summary and the close look are of it. "
                           u"Double-click another row below to switch."_s.arg(focus_name()));
    auto mine_of = [&](const Tot &d) {
        py::Sum s;
        for (long long q : *fp)
            if (const Vals *v = d.p.get(q)) {
                py::Sum t;
                for (int i = 0; i < 3; ++i)
                    t.add((*v)[i]);
                s.add(t.value());
            }
        return s.value();
    };
    auto use_of = [&](const Tot &d) {
        py::Sum s;
        for (long long q : *fp)
            if (const Vals *v = d.p.get(q))
                s.add((*v)[3]);
        return s.value();
    };
    const QHash<QString, QPixmap> &icons = prodpanel::goods_icons(mod);
    QStringList order;
    {
        QSet<QString> seen;
        for (const auto &[g, _v] : mod.goods)
            if (!seen.contains(g)) {
                seen.insert(g);
                order << g;
            }
        for (const auto &[g, _v] : tot)
            if (!seen.contains(g)) {
                seen.insert(g);
                order << g;
            }
    }
    const QBrush dim{QColor(theme::INK_FAINT)}, err{QColor(theme::ERROR_)};
    goods->blockSignals(true);
    goods->setSortingEnabled(false);
    goods->clear();
    goods->headerItem()->setText(2, !tag ? u"Largest maker"_s : u"Share of the world"_s);
    long long n_made = 0;
    QTreeWidgetItem *pick = nullptr;
    for (const QString &g : order) {
        const Tot &d = tot_of(tot, g);
        const double world = made(d);
        const double mine = tag ? mine_of(d) : world;
        const double use = tag ? use_of(d) : used(d);
        auto *it = new _Row(goods, QStringList{good_name(g), QString(), QString(), QString(), QString(), QString()});
        it->setData(0, Qt::UserRole + 1, g);
        if (icons.contains(g))
            it->setIcon(0, QIcon(icons.value(g)));
        _num(it, 1, mine, mine ? fmt(mine, 2) : u"—"_s);
        if (tag) {
            const double sh = world ? mine / world : 0.0;
            _num(it, 2, sh, mine ? pct(sh) : u"—"_s);
            it->setData(2, Qt::UserRole + 2, sh);
        } else {
            Amounts by_;
            for (const QString &k : MAKERS)
                for (const auto &[o, v] : maker(d, k))
                    by_.insert(o, by_.value(o, 0.0) + v);
            OptStr best;
            double bv = 0.0;
            for (const auto &[o, v] : by_)
                if (!best || v > bv) {
                    best = o;
                    bv = v;
                }
            const double sh = best && world ? bv / world : 0.0;
            _num(it, 2, sh, best ? name_of(*best) + u"  "_s + pct(sh) : u"—"_s);
            it->setTextAlignment(2, Qt::AlignLeft | Qt::AlignVCenter);
            it->setData(2, Qt::UserRole + 2, sh);
        }
        _num(it, 3, use, use ? fmt(use, 2) : u"—"_s);
        const double left = mine - use;
        _num(it, 4, left, (mine || use) ? plus_f(left) : u"—"_s);
        _num(it, 5, mine * price(g), mine ? fmt(mine * price(g)) : u"—"_s);
        it->setToolTip(0, u"%1 (%2): price £ %3; the world makes %4 a day — farms and mines %5, factories %6, "
                          u"artisans %7 (expected); factories and artisans use %8"_s
                              .arg(good_name(g), g, fmt(price(g), 2), fmt(world, 2), fmt(values_sum(d.rgo), 2),
                                   fmt(values_sum(d.fac), 2), fmt(values_sum(d.art), 2), fmt(used(d), 2)));
        if (left < -1e-9) {
            it->setForeground(4, err);
            it->setToolTip(4, u"The factories need more of it than is dug, grown and made%1: to be bought%2"_s.arg(
                                  tag ? u" here"_s : u" in the whole world"_s,
                                  tag ? u" abroad"_s : u" from nobody — they will stand still"_s));
        }
        if (!mine) {
            for (int c = 0; c < 6; ++c)
                if (!(c == 4 && left < -1e-9) && !(c == 3 && use))
                    it->setForeground(c, dim);
        } else {
            n_made += 1;
        }
        if (good && g == *good)
            pick = it;
    }
    goods->setSortingEnabled(true);
    if (!pick && goods->topLevelItemCount()) {
        pick = goods->topLevelItem(0);
        good = pick->data(0, Qt::UserRole + 1).toString();
    }
    if (pick)
        goods->setCurrentItem(pick);
    goods->blockSignals(false);
    py::Sum s_rgo, s_fac, s_art, s_in;
    for (const RgoRow &r : data->rgo)
        s_rgo.add(r.money);
    for (const FacRow &r : data->factories)
        s_fac.add(r.money_out);
    for (const ArtRow &r : data->artisans)
        s_art.add(r.money);
    for (const FacRow &r : data->factories)
        s_in.add(r.money_in);
    const double w_rgo = s_rgo.value(), w_fac = s_fac.value(), w_art = s_art.value(), w_in = s_in.value();
    const double w_all = w_rgo + w_fac + w_art;
    if (tag) {
        py::Sum a, b2, c, e;
        for (const RgoRow &r : data->rgo)
            if (fps.contains(r.pid))
                a.add(r.money);
        for (const FacRow &r : data->factories)
            if (fps.contains(r.pid))
                b2.add(r.money_out);
        for (const ArtRow &r : data->artisans)
            if (fps.contains(r.pid))
                c.add(r.money);
        for (const FacRow &r : data->factories)
            if (fps.contains(r.pid))
                e.add(r.money_in);
        const double m_rgo = a.value(), m_fac = b2.value(), m_art = c.value(), m_in = e.value();
        const QString flv = focus->first;            // its place among the others of its kind
        OrderedMap<Unit, double> wmap;
        auto addw = [&](long long pid, double v) {
            if (const auto u = unit_of(pid, flv))
                wmap.insert(*u, wmap.value(*u, 0.0) + v);
        };
        for (const RgoRow &r : data->rgo)
            addw(r.pid, r.money);
        for (const FacRow &r : data->factories)
            addw(r.pid, r.money_out);
        for (const ArtRow &r : data->artisans)
            addw(r.pid, r.money);
        const double mine_w = wmap.value(focus->second, 0.0);
        long long rank_no = 1;
        for (const auto &[_u, v] : wmap)
            if (v > mine_w)
                rank_no += 1;
        std::vector<std::pair<double, QString>> short_;
        for (const auto &[g, d] : tot) {
            const double uo = use_of(d), mo = mine_of(d);
            if (uo > mo + 1e-9)
                short_.emplace_back(use_of(d) - mine_of(d), g);
        }
        std::stable_sort(short_.begin(), short_.end(), [](const auto &x, const auto &y) { return y < x; });
        const QString fname = focus_name();
        QString shorts;
        if (!short_.empty()) {
            QStringList parts;
            for (size_t i = 0; i < short_.size() && i < 8; ++i)
                parts << fmt(short_[i].first, 1) + u' ' + good_name(short_[i].second);
            shorts = u" <span style='color:%1'><b>Its factories and artisans need more than it makes of:</b> %2 a day "
                     u"— to be brought in.</span>"_s.arg(theme::WARN, parts.join(u", "_s));
        }
        summary->setText(
            u"<b>%1</b> makes goods worth <b>£ %2 a day</b> — %3 of the world's £ %4 (place %5 of %6 %7): farms and "
            u"mines £ %8, factories £ %9 (for inputs worth £ %10), artisans £ %11 (expected). It makes %12 of the %13 "
            u"goods.%14"_s
                .arg(fname, fmt(m_rgo + m_fac + m_art), pct(w_all ? (m_rgo + m_fac + m_art) / w_all : 0.0), fmt(w_all),
                     QString::number(rank_no), QString::number(wmap.size()), plural_of(flv), fmt(m_rgo), fmt(m_fac))
                .arg(fmt(m_in), fmt(m_art), QString::number(n_made), QString::number(order.size()), shorts));
        goods_head->setText(u"<b>What %1 makes a day</b> — one line a good. Click a good: who makes it, on the map."_s.arg(
            unit_name(focus->first, focus->second)));
    } else {
        summary->setText(u"<b>The world</b> makes goods worth <b>£ %1 a day</b>: farms and mines £ %2, factories £ %3 "
                         u"(for inputs worth £ %4), artisans £ %5 (expected). %6 of the %7 goods are made by somebody at "
                         u"the start; %8 nations make something."_s
                             .arg(fmt(w_all), fmt(w_rgo), fmt(w_fac), fmt(w_in), fmt(w_art), QString::number(n_made),
                                  QString::number(order.size()), QString::number(worth.size())));
        goods_head->setText(u"<b>What the world makes a day</b> — one line a good. Click a good: who makes it, on the map."_s);
    }
    std::vector<std::pair<double, QString>> scarce;
    for (const auto &[g, v] : data->avail)
        scarce.emplace_back(v, g);
    std::stable_sort(scarce.begin(), scarce.end());
    QStringList lines;
    for (qsizetype i = 0; i < data->notes.size() && i < 8; ++i)
        lines << u"⚠ "_s + data->notes[i];
    if (!scarce.empty()) {
        QStringList parts;
        for (size_t i = 0; i < scarce.size() && i < 12; ++i)
            parts << u"%1 %2 %"_s.arg(good_name(scarce[i].second), QString::number(py::round_int(100 * scarce[i].first)));
        lines << u"Inputs the world is short of (its factories and artisans get this share of what they want, and make "
                 u"that much less): "_s + parts.join(u", "_s);
    }
    notes->setText(lines.join(u'\n'));
    notes->setVisible(!lines.isEmpty());
    _show_good();
    _fill_close();
}

void EconWorkspace::_good_picked(QTreeWidgetItem *it)
{
    if (it) {
        good = it->data(0, Qt::UserRole + 1).toString();
        _show_good();
    }
}

// ------------------------------------------------------------------ the picked good: who makes it, the map
void EconWorkspace::_show_good()
{
    if (!good || !data)
        return;
    const QString g = *good;
    const Tot &d = tot_of(tot, g);
    const double world = made(d), w_rgo = values_sum(d.rgo), w_fac = values_sum(d.fac), w_use = used(d);
    const double w_art = values_sum(d.art);
    const double price = doc->maps.contains(u"production"_s) ? doc->map<production::ProdEdits>(u"production"_s).cost(g) : 0.0;
    const QString nm = good_name(g);
    QString paren;
    if (w_fac || w_art) {
        QStringList xs;
        for (const QString &x : {w_rgo ? u"farms and mines "_s + fmt(w_rgo, 2) : QString(),
                                 w_fac ? u"factories "_s + fmt(w_fac, 2) : QString(),
                                 w_art ? u"artisans %1, expected"_s.arg(fmt(w_art, 2)) : QString()})
            if (!x.isEmpty())
                xs << x;
        paren = u" ("_s + xs.join(u", "_s) + u")"_s;
    }
    g_title->setText(u"<span style='font-size:17px; font-weight:700; color:%1'>%2</span> &nbsp; the world makes <b>%3 a "
                     u"day</b>%4 — worth £ %5. Factories and artisans use %6 a day%7."_s
                         .arg(theme::GOLD, nm, fmt(world, 2), paren, fmt(world * price),
                              w_use ? fmt(w_use, 2) : u"none of it"_s,
                              w_use > world + 1e-9 ? u": <span style='color:%1'><b>%2 more than is made</b></span>"_s.arg(
                                                         theme::ERROR_, fmt(w_use - world, 2))
                                                   : QString()));
    const QString lv = by;
    // narrowed to one thing: a finer level lists only what is inside it (a nation's states); a coarser one lists all,
    // the one it lies in picked
    const bool inside = focus && lvi(focus->first) < lvi(lv);
    QSet<long long> fp_in;
    if (inside) {
        const auto fpl = _fpids();                   // (kept: a loop over *temporary reads freed memory)
        for (long long p : *fpl)
            fp_in.insert(p);
    }
    OrderedMap<Unit, Vals> units = by_unit(d, lv, inside ? &fp_in : nullptr);
    std::vector<Unit> makers;
    for (const auto &[u, v] : units)
        if (v[0] + v[1] + v[2] > 0)
            makers.push_back(u);
    for (const std::optional<Pick> *pk : {&_sel, &focus}) {   // what is picked always has its row
        const std::optional<Pick> &pick_ = *pk;
        if (!pick_ || lvi(pick_->first) < lvi(lv))
            continue;
        std::vector<long long> ps;
        if (pick_->first == u"province")
            ps = {pick_->second.toLongLong()};
        else if (const auto *m = members(pick_->first).get(pick_->second))
            ps = *m;
        const std::optional<Unit> want = !ps.empty() && _own.contains(ps[0]) ? unit_of(ps[0], lv) : std::nullopt;
        bool meets = !inside;
        for (long long p : ps)
            meets = meets || fp_in.contains(p);
        if (want && !units.contains(*want) && meets)
            units.insert(*want, Vals{0.0, 0.0, 0.0, 0.0});
    }
    const QString plural = plural_of(lv);
    std::vector<double> top3;
    for (const Unit &u : makers) {
        py::Sum s;
        for (int i = 0; i < 3; ++i)
            s.add((*units.get(u))[i]);
        top3.push_back(s.value());
    }
    std::stable_sort(top3.begin(), top3.end(), [](double a, double b) { return b < a; });
    if (top3.size() > 3)
        top3.resize(3);
    const QString where = inside ? u" in "_s + unit_name(focus->first, focus->second) : QString();
    std::optional<Unit> mine_u;                      // the row of the focus itself (or the one holding it)
    if (focus && !inside) {
        const auto fps = _fpids();
        mine_u = fps && !fps->empty() ? unit_of(fps->front(), lv) : std::nullopt;
    }
    rank_head->setText(u"<b>Who makes %1</b> — %2 %3%4%5"_s.arg(
        nm, QString::number(makers.size()), makers.size() != 1 ? plural : chop1(plural), where,
        world && makers.size() > 3 ? u"; the first 3 make %1 of it"_s.arg(pct(py::sum(top3) / world)) : QString()));
    const std::map<QString, QString> heads = {{u"continent"_s, u"Continent"_s}, {u"nation"_s, u"Nation"_s},
                                              {u"state"_s, u"State"_s}, {u"province"_s, u"Province"_s}};
    rank->headerItem()->setText(0, heads.at(lv));
    rank->setColumnHidden(1, lv == u"continent" || lv == u"nation" || !group.isEmpty());
    rank->headerItem()->setText(1, lv == u"state" ? u"Belongs to (nation)"_s : u"Belongs to (state · nation)"_s);
    for (const auto &[k, b] : group_btn)             // only a coarser level can group the rows
        b->setVisible(k.isEmpty() || lvi(k) < lvi(lv));
    if (!group.isEmpty() && lvi(group) >= lvi(lv))
        _set_group(QString(), false);
    rank_hint->setToolTip(u"Click a row or the map: picked here and on the map, its close look below. Double-click (row "
                          u"or map): the map goes there, the goods list on the left shows what IT makes. Click the sea, "
                          u"right-click the map or Esc: the whole world again."_s);
    const QBrush err{QColor(theme::ERROR_)}, faint{QColor(theme::INK_FAINT)}, gold{QColor(theme::GOLD)};
    const Qt::Alignment right = Qt::AlignRight | Qt::AlignVCenter;
    rank->setUpdatesEnabled(false);
    rank->blockSignals(true);
    rank->clear();
    rank->setRootIsDecorated(!group.isEmpty());
    // sorted HERE, once; a click on a column title sorts by it the same way
    const int col = rank->header()->sortIndicatorSection();
    const bool desc = rank->header()->sortIndicatorOrder() == Qt::DescendingOrder;
    using RowT = std::tuple<QString, Unit, Vals>;
    auto order = [&](std::vector<RowT> rows) {
        if (col == 0 || col == 1) {
            std::vector<std::pair<QString, size_t>> ks;
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto &[l, u, _v] = rows[i];
                ks.emplace_back(py::lower(col == 0 ? unit_name(l, u) : belongs(l, u)), i);
            }
            std::stable_sort(ks.begin(), ks.end(),
                             [desc](const auto &a, const auto &b) { return desc ? b.first < a.first : a.first < b.first; });
            std::vector<RowT> out;
            for (const auto &k : ks)
                out.push_back(rows[k.second]);
            return out;
        }
        auto key = [col](const Vals &v) {
            const double m = v[0] + v[1] + v[2];
            switch (col) {
            case 2: case 3: return m;
            case 4: return v[0];
            case 5: return v[1];
            case 6: return v[2];
            case 7: return v[3];
            default: return m - v[3];
            }
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const RowT &a, const RowT &b) {
            const double ka = key(std::get<2>(a)), kb = key(std::get<2>(b));
            return desc ? kb < ka : ka < kb;
        });
        return rows;
    };
    auto num = [&](QTreeWidgetItem *it, int c, const QString &text) {
        it->setText(c, text);
        it->setTextAlignment(c, right);
    };
    auto fill = [&](QTreeWidgetItem *it, const Vals &v, const QString &tip, const QString &level, const Unit &u) {
        const double m = v[0] + v[1] + v[2], use = v[3];
        it->setData(0, Qt::UserRole + 1, u);
        it->setData(0, Qt::UserRole + 3, level);
        it->setToolTip(0, tip);
        const double sh = world ? m / world : 0.0;
        num(it, 2, m ? fmt(m, 2) : u"—"_s);
        num(it, 3, m ? pct(sh) : u"—"_s);
        it->setData(3, Qt::UserRole + 2, sh);
        const std::pair<int, double> cs[] = {{4, v[0]}, {5, v[1]}, {6, v[2]}, {7, use}};
        for (const auto &[c, x] : cs)
            num(it, c, x ? fmt(x, 2) : u"—"_s);
        const double left = m - use;
        num(it, 8, plus_f(left));
        if (left < -1e-9)
            it->setForeground(8, err);
        if (!m && !use) {                            // (picked, but makes none of it: there, dimmed)
            for (int c = 0; c < 9; ++c)
                it->setForeground(c, faint);
            it->setToolTip(0, tip + u" — makes none of it"_s);
        }
    };
    auto row = [&](const Unit &u, const Vals &v) {
        const QString name = unit_name(lv, u), b = belongs(lv, u);
        QStringList cols{name, b};
        for (int i = 0; i < 7; ++i)
            cols << QString();
        auto *it = new QTreeWidgetItem(cols);
        fill(it, v, lv == u"state" || lv == u"province" ? name + u" — "_s + b : name + u" ("_s + u + u")"_s, lv, u);
        if (mine_u && u == *mine_u) {                // what the page is narrowed to: its row stands out
            QFont f(it->font(0));
            f.setBold(true);
            for (int c = 0; c < 9; ++c) {
                it->setFont(c, f);
                if (c != 8)
                    it->setForeground(c, gold);
            }
        }
        return it;
    };
    auto pl = [&](size_t n) { return n != 1 ? plural : chop1(plural); };
    QTreeWidgetItem *mine_row = nullptr;
    if (!group.isEmpty()) {                          // under what it belongs to, with that one's own total
        OrderedMap<std::optional<QString>, std::pair<Vals, std::vector<RowT>>> groups;
        for (const auto &[u, v] : units) {
            std::vector<long long> pids;
            if (lv == u"province")
                pids = {u.toLongLong()};
            else if (const auto *m = members(lv).get(u))
                pids = *m;
            const std::optional<QString> gu = !pids.empty() ? unit_of(pids[0], group) : std::nullopt;
            auto *gp = groups.get(gu);
            if (!gp) {
                groups.insert(gu, {Vals{0.0, 0.0, 0.0, 0.0}, {}});
                gp = groups.get(gu);
            }
            for (int i = 0; i < 4; ++i)
                gp->first[i] += v[i];
            gp->second.emplace_back(lv, u, v);
        }
        QList<QTreeWidgetItem *> tops;
        std::vector<RowT> grows;
        for (const auto &[gu, gv] : groups)
            if (gu)
                grows.emplace_back(group, *gu, gv.first);
        for (const auto &[_l, gu, gv] : order(grows)) {
            const std::vector<RowT> kids = order(groups.get(std::optional<QString>(gu))->second);
            const size_t n_in = kids.size();
            const QString gname = unit_name(group, gu);
            QStringList cols{u"%1  (%2 %3)"_s.arg(gname, QString::number(n_in), pl(n_in))};
            for (int i = 0; i < 8; ++i)
                cols << QString();
            auto *it = new QTreeWidgetItem(cols);
            fill(it, gv, u"%1: %2 %3"_s.arg(gname, QString::number(n_in), pl(n_in)), group, gu);
            QFont f(it->font(0));
            f.setBold(true);
            for (int c = 0; c < 9; ++c)
                it->setFont(c, f);
            it->setForeground(0, gold);
            QList<QTreeWidgetItem *> ch;
            for (const auto &[_l2, u, v] : kids)
                ch << row(u, v);
            it->addChildren(ch);
            tops << it;
            if (mine_u)
                for (QTreeWidgetItem *c : ch)
                    if (c->data(0, Qt::UserRole + 1).toString() == *mine_u) {
                        mine_row = c;
                        break;
                    }
        }
        QList<QTreeWidgetItem *> loose;
        if (const auto *none = groups.get(std::nullopt))
            for (const auto &[_l, u, v] : order(none->second))
                loose << row(u, v);
        rank->addTopLevelItems(tops + loose);
        for (QTreeWidgetItem *it : tops)
            it->setExpanded(true);
    } else {
        std::vector<RowT> rows;
        for (const auto &[u, v] : units)
            rows.emplace_back(lv, u, v);
        QList<QTreeWidgetItem *> items;
        for (const auto &[_l, u, v] : order(rows))
            items << row(u, v);
        rank->addTopLevelItems(items);
        if (mine_u)
            for (QTreeWidgetItem *c : items)
                if (c->data(0, Qt::UserRole + 1).toString() == *mine_u) {
                    mine_row = c;
                    break;
                }
    }
    QTreeWidgetItem *pick = mine_row;
    if (QTreeWidgetItem *kept = _find_sel(lv))      // the row picked before stays picked
        pick = kept;
    if (pick) {
        rank->setCurrentItem(pick);
        rank->scrollToItem(pick, QAbstractItemView::PositionAtCenter);
    }
    rank->blockSignals(false);
    rank->setUpdatesEnabled(true);
    _paint_map(QString());
    if (pick) {
        _light(pick);
    } else if (_sel) {                               // a finer view than the pick: it stays lit on the map
        const auto [slv, su] = *_sel;
        if (slv == u"province") {
            _set_picked({su.toLongLong()});
        } else {
            const auto *m = members(slv).get(su);
            _set_picked(m ? *m : std::vector<long long>{});
        }
        map->update();
        _fill_close();
    }
}

QTreeWidgetItem *EconWorkspace::_find_sel(const QString &lv)
{
    if (!_sel)
        return nullptr;
    const auto [slv, su] = *_sel;
    std::optional<Unit> want;
    if (slv == lv) {
        want = su;
    } else if (lvi(slv) > lvi(lv)) {
        std::vector<long long> pids;
        if (slv == u"province")
            pids = {su.toLongLong()};
        else if (const auto *m = members(slv).get(su))
            pids = *m;
        want = !pids.empty() ? unit_of(pids[0], lv) : std::nullopt;
    } else {
        return nullptr;                              // (a finer view: the coarser pick has no one row)
    }
    if (!want)
        return nullptr;
    for (QTreeWidgetItem *it : _items())
        if (it->data(0, Qt::UserRole + 3).toString() == lv && it->data(0, Qt::UserRole + 1).toString() == *want)
            return it;
    return nullptr;
}

void EconWorkspace::_paint_map(const std::optional<QString> &light)
{
    if (!md || !good || !doc)
        return;
    const Tot &d = tot_of(tot, *good);
    const long long n = md->n;
    const QString lv = by;
    const auto [sr, sg, sb] = SEA;
    std::vector<regionimport::Rgb8> lut(static_cast<size_t>(n),
                                        regionimport::Rgb8{std::uint8_t(sr), std::uint8_t(sg), std::uint8_t(sb)});
    OrderedMap<Unit, double> units;
    for (const auto &[u, v] : by_unit(d, lv))
        units.insert(u, v[0] + v[1] + v[2]);
    const double world = made(d);
    double top = 0.0;
    bool first = true;
    for (const auto &[_u, v] : units) {
        if (first || v > top)
            top = v;
        first = false;
    }
    OrderedMap<Unit, Rgb> col;
    for (const auto &[u, v] : units)
        col.insert(u, shade(v, top));
    auto rgb8 = [](const Rgb &c) {
        return regionimport::Rgb8{std::uint8_t(std::get<0>(c)), std::uint8_t(std::get<1>(c)), std::uint8_t(std::get<2>(c))};
    };
    for (const auto &[pid, pr] : mod.provinces) {
        if (pid >= n || pid < 0 || pr.sea)
            continue;
        Rgb c = EMPTY_LAND;
        if (_own.contains(pid))
            if (const auto u = unit_of(pid, lv))
                c = col.value(*u, EMPTY_LAND);
        lut[size_t(pid)] = rgb8(c);
    }
    if (lv != u"province") {                         // borders between the units of this level only
        OrderedMap<Unit, long long> num;
        _grp.assign(static_cast<size_t>(n), 0);
        for (const auto &[pid, pr] : mod.provinces)
            if (pid < n && pid >= 0 && !pr.sea) {
                const std::optional<Unit> u = _own.contains(pid) ? unit_of(pid, lv) : std::nullopt;
                if (!u) {
                    _grp[size_t(pid)] = 1;
                } else {
                    if (!num.contains(*u))
                        num.insert(*u, num.size() + 2);
                    _grp[size_t(pid)] = *num.get(*u);
                }
            }
        map->edge_ids = &_grp;
        map->edge_dark = 0.3;
    } else {
        map->edge_ids = nullptr;
        map->edge_dark = 0.72;
    }
    const bool fresh = map->ids == nullptr;
    map->set_map(&md->idmap, lut, !fresh);
    const auto &ce = map->centres();
    std::vector<std::tuple<double, double, QString>> extra;
    const auto &mem = members(lv);
    std::vector<std::pair<Unit, double>> ranked(units.begin(), units.end());
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return b.second < a.second; });
    for (size_t i = 0; i < ranked.size() && i < 8; ++i) {   // the largest makers named on the map
        const auto &[u, uv] = ranked[i];
        std::vector<long long> src;
        if (lv == u"province")
            src = {u.toLongLong()};
        else if (const auto *m = mem.get(u))
            src = *m;
        std::vector<long long> pids;
        for (long long p : src)
            if (p >= 0 && p < static_cast<long long>(ce.cnt.size()))
                pids.push_back(p);
        if (!pids.empty() && uv > 0 && (lv == u"province" || !world || uv / world >= 0.0005)) {
            long long p0 = pids[0];
            for (long long p : pids)
                if (ce.cnt[size_t(p)] > ce.cnt[size_t(p0)])
                    p0 = p;
            const QString nm = lv != u"province" ? unit_name(lv, u) : mod.provinces.get(u.toLongLong())->name;
            extra.emplace_back(ce.cx[size_t(p0)], ce.cy[size_t(p0)],
                               nm + u' ' + (lv == u"province" ? fmt(uv, 1) : world ? pct(uv / world) : QString()));
        }
    }
    if (lv == u"province")
        legend->set(top ? fmt(top / 4.0, 2) : QString(), top ? fmt(top, 2) + u" a day"_s : u"nobody makes it"_s);
    else
        legend->set(world ? pct(top / world / 4.0) : QString(),
                    world ? pct(top / world) + u" of the world"_s : u"nobody makes it"_s);
    map->extra = extra;
    if (light) {
        const std::vector<long long> *ps = !light->isEmpty() ? _prov_of.get(*light) : nullptr;
        _set_picked(ps ? *ps : std::vector<long long>{});
    }
    map->update();
    _hover(0);
}

void EconWorkspace::_set_by(const QString &k)
{
    by = k;
    for (const auto &[kk, b] : by_btn)
        b->setChecked(kk == k);
    _set_picked({});
    _show_good();
}

void EconWorkspace::_set_group(const QString &k, bool show)
{
    group = k;
    for (const auto &[kk, b] : group_btn)
        b->setChecked(kk == k);
    if (show)
        _show_good();
}

void EconWorkspace::_rank_double(QTreeWidgetItem *it)
{
    // double-click a row (a group's row too): the goods list on the left becomes what IT makes
    const QString lv = it->data(0, Qt::UserRole + 3).toString();
    const QVariant uv = it->data(0, Qt::UserRole + 1);
    if (!lv.isEmpty() && uv.isValid()) {
        const Unit u = uv.toString();
        if (lv == by)
            _sel = Pick{lv, u};
        set_focus(Pick{lv, u});
        jump_to(lv, u);
    }
}

void EconWorkspace::jump_to(const QString &lv, const Unit &u)
{
    // the map goes to it — the province, the state, the nation, the continent — zoomed to fit, lit
    if (!map->ids)
        return;
    std::vector<long long> src;
    if (lv == u"province")
        src = {u.toLongLong()};
    else if (const auto *m = members(lv).get(u))
        src = *m;
    const auto &ce = map->centres();
    std::vector<long long> pids;
    for (long long p : src)
        if (p >= 0 && p < static_cast<long long>(ce.cnt.size()) && ce.cnt[size_t(p)] > 0)
            pids.push_back(p);
    if (pids.empty())
        return;
    std::vector<double> r;
    for (long long p : pids)
        r.push_back(std::sqrt(ce.cnt[size_t(p)]));   // (a province's rough half size)
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (size_t i = 0; i < pids.size(); ++i) {
        const double cx = ce.cx[size_t(pids[i])], cy = ce.cy[size_t(pids[i])], s = r[i];
        x0 = i ? std::min(x0, cx - s) : cx - s;
        x1 = i ? std::max(x1, cx + s) : cx + s;
        y0 = i ? std::min(y0, cy - s) : cy - s;
        y1 = i ? std::max(y1, cy + s) : cy + s;
    }
    _set_picked(pids);
    _EconMap *m = map;
    const double H = double(m->ids->h()), W = double(m->ids->w());
    const double whole = std::min(m->width() / W, m->height() / H);   // (never further out than the whole map)
    const double bw = std::max(140.0, (x1 - x0) * 1.5), bh = std::max(140.0, (y1 - y0) * 1.5);
    m->scale = std::max(whole, std::min(24.0, std::min(m->width() / bw, m->height() / bh)));
    m->ox = (x0 + x1) / 2.0 - m->width() / m->scale / 2.0;
    m->oy = (y0 + y1) / 2.0 - m->height() / m->scale / 2.0;
    m->_last = 1;
    m->update();
    _hover(0);
}

QString EconWorkspace::_line(const QString &level, const Unit &u)
{
    // what one continent / nation / state / province makes of the picked good, in a line
    const Tot &d = tot_of(tot, *good);
    const double world = made(d);
    const Vals v = by_unit(d, level).value(u, Vals{0.0, 0.0, 0.0, 0.0});
    const double m = v[0] + v[1] + v[2];
    const QString b = belongs(level, u);
    return u"%1%2: %3 %4 a day%5"_s.arg(unit_name(level, u), b.isEmpty() ? QString() : u" ("_s + b + u")"_s,
                                         m ? fmt(m, 2) : u"no"_s, good_name(*good),
                                         m && world ? u" — %1 of the world's %2"_s.arg(pct(m / world), fmt(world, 2))
                                                    : QString());
}

void EconWorkspace::_hover(int pid)
{
    if (!good)
        return;
    const QString lv = by;
    const moddata::Province *pr = pid ? mod.provinces.get(pid) : nullptr;
    if (!pr || pr->sea) {
        const std::optional<long long> lit = _first_lit();
        const std::optional<Unit> u = lit && _own.contains(*lit) ? unit_of(*lit, lv) : std::nullopt;
        hover->setText(u ? _line(lv, *u)
                         : u"Move over the map: what a %1 makes of it. Click: picked in the list below. Wheel: zoom; "
                           u"right button: move."_s.arg(chop1(plural_of(lv))));
        return;
    }
    const std::optional<Unit> u = _own.contains(pid) ? unit_of(pid, lv) : std::nullopt;
    if (!u) {
        hover->setText(u"%1 (#%2): nobody's land"_s.arg(pr->name, QString::number(pid)));
        return;
    }
    const double here = tot_of(tot, *good).prov.value(pid, 0.0);
    hover->setText(_line(lv, *u) + (here && lv != u"province" ? u"   ·   %1 (#%2): %3 a day"_s.arg(
                                                                    pr->name, QString::number(pid), fmt(here, 2))
                                                              : QString()));
}

std::vector<QTreeWidgetItem *> EconWorkspace::_items()
{
    std::vector<QTreeWidgetItem *> out;
    for (int i = 0; i < rank->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = rank->topLevelItem(i);
        out.push_back(top);
        for (int j = 0; j < top->childCount(); ++j)
            out.push_back(top->child(j));
    }
    return out;
}

void EconWorkspace::_map_picked()
{
    // clicked on the map: the continent / nation / state / province (by the switch) picked in the list
    const std::optional<long long> lit = _first_lit();
    const std::optional<Unit> u = lit && _own.contains(*lit) ? unit_of(*lit, by) : std::nullopt;
    if (!u) {                                        // the sea, nobody's land: let go of everything
        detach();
        return;
    }
    _sel = Pick{by, *u};
    const std::vector<QTreeWidgetItem *> items = _items();
    QTreeWidgetItem *hit = nullptr;
    for (QTreeWidgetItem *it : items)
        if (it->data(0, Qt::UserRole + 3).toString() == by && it->data(0, Qt::UserRole + 1).toString() == *u) {
            hit = it;
            break;
        }
    if (!hit) {
        _show_good();                                // (it makes none of this good: its row is put in, then picked)
        return;
    }
    rank->blockSignals(true);
    rank->setCurrentItem(nullptr);
    rank->setCurrentItem(hit);
    rank->scrollToItem(hit, QAbstractItemView::PositionAtCenter);
    rank->blockSignals(false);
    _fill_close();
    _hover(0);
}

void EconWorkspace::_map_double(int pid)
{
    // double-click on the map: the same as double-clicking its row — the goods list of it, the map there
    const std::optional<Unit> u = _own.contains(pid) ? unit_of(pid, by) : std::nullopt;
    if (!u) {
        detach();
        return;
    }
    _sel = Pick{by, *u};
    set_focus(Pick{by, *u});
    jump_to(by, *u);
}

void EconWorkspace::_rank_picked(QTreeWidgetItem *it)
{
    // a row clicked: lit on the map (a group's row: the whole group) — and remembered
    if (it && it->data(0, Qt::UserRole + 3).toString() == by)
        _sel = Pick{by, it->data(0, Qt::UserRole + 1).toString()};
    _light(it);
}

void EconWorkspace::_light(QTreeWidgetItem *it)
{
    _fill_close();
    const QVariant uv = it ? it->data(0, Qt::UserRole + 1) : QVariant();
    const QVariant lvv = it ? it->data(0, Qt::UserRole + 3) : QVariant();
    if (!uv.isValid() || !lvv.isValid()) {
        _set_picked({});
    } else {
        const QString level = lvv.toString();
        const Unit u = uv.toString();
        if (level == u"province") {
            _set_picked({u.toLongLong()});
        } else {
            const auto *m = members(level).get(u);
            _set_picked(m ? *m : std::vector<long long>{});
        }
    }
    map->update();
    _hover(0);
}

// ------------------------------------------------------------------ the close look: what is inside the pick
void EconWorkspace::detach()
{
    // nothing picked, nothing narrowed: the whole world again (✕, a click on the sea, a right-click, Esc)
    _sel.reset();
    _set_picked({});
    rank->setCurrentItem(nullptr);
    if (focus)
        set_focus(std::nullopt);
    else
        _show_good();
    _fill_close();
    map->update();
}

void EconWorkspace::_fill_close()
{
    const std::optional<Pick> sel = _sel ? _sel : focus;
    if (!data || !sel) {
        _close_pids.clear();
        close_box->hide();
        return;
    }
    const auto [lv, u] = *sel;
    QSet<long long> pids;
    if (lv == u"province")
        pids.insert(u.toLongLong());
    else if (const auto *m = members(lv).get(u))
        for (long long p : *m)
            pids.insert(p);
    _close_pids.assign(pids.begin(), pids.end());
    std::sort(_close_pids.begin(), _close_pids.end());
    production::ProdEdits *prods =
        doc->maps.contains(u"production"_s) ? &doc->map<production::ProdEdits>(u"production"_s) : nullptr;
    auto price_of = [prods](const QString &g) { return prods ? prods->cost(g) : 0.0; };
    const QString W = theme::WARN, E = theme::ERROR_, G = theme::GOLD;
    const QString gd = good.value_or(QString());
    const bool has_good = good.has_value();
    const bool big = lv == u"continent" || lv == u"nation" || pids.size() > 12;
    std::vector<const RgoRow *> rg;
    std::vector<const FacRow *> fa;
    std::vector<const ArtRow *> ar;
    for (const RgoRow &r : data->rgo)
        if (pids.contains(r.pid))
            rg.push_back(&r);
    for (const FacRow &r : data->factories)
        if (pids.contains(r.pid))
            fa.push_back(&r);
    for (const ArtRow &r : data->artisans)
        if (pids.contains(r.pid))
            ar.push_back(&r);
    QStringList out;
    // --- farms and mines
    if (!rg.empty()) {
        std::stable_sort(rg.begin(), rg.end(), [](const RgoRow *a, const RgoRow *b) { return -a->money < -b->money; });
        std::vector<const RgoRow *> idle;
        long long work = 0;
        py::Sum money;
        for (const RgoRow *r : rg) {
            if (!r->workers)
                idle.push_back(r);
            work += r->workers;
            money.add(r->money);
        }
        QString idle_s;
        if (!idle.empty()) {
            QStringList names;
            for (size_t i = 0; i < idle.size() && i < 6; ++i)
                names << esc(idle[i]->province);
            idle_s = u" <span style='color:%1'><b>%2 have nobody working them</b> (%3).</span>"_s.arg(
                W, QString::number(idle.size()), names.join(u", "_s) + (idle.size() > 6 ? u" …"_s : QString()));
        }
        out << u"<p><b style='color:%1'>Farms and mines</b> — %2, %3 workers, £ %4 a day.%5</p>"_s.arg(
            G, QString::number(rg.size()), commas(work), fmt(money.value()), idle_s);
        QString lines;
        const size_t lim = big ? std::min<size_t>(8, rg.size()) : rg.size();
        for (size_t i = 0; i < lim; ++i) {
            const RgoRow &r = *rg[i];
            QStringList why;
            if (std::fabs(r.thr) > 1e-9)
                why << u"throughput %1 %"_s.arg(plus_i(py::round_int(100 * r.thr)));
            if (std::fabs(r.outb) > 1e-9)
                why << u"output %1 %"_s.arg(plus_i(py::round_int(100 * r.outb)));
            if (!r.terrain.isEmpty())
                why << u"terrain "_s + esc(r.terrain);
            const bool hi = has_good && r.good == gd;
            lines += u"<li%1><b>%2</b>: %3 %4 a day (£ %5) — %6 %7, size %8, bonus × %9%10</li>"_s.arg(
                hi ? u" style='color:%1'"_s.arg(G) : QString(), esc(r.province), fmt(r.out, 2), esc(good_name(r.good)),
                fmt(r.money), commas(r.workers), esc(r.kinds.join(u" + "_s)), QString::number(r.size),
                py::fmt_f(r.mult, 2))
                         .arg(why.isEmpty() ? QString() : u" ("_s + why.join(u", "_s) + u")"_s);
        }
        if (big && rg.size() > 8)
            lines += u"<li>… and %1 more</li>"_s.arg(QString::number(rg.size() - 8));
        out << u"<ul style='margin-top:0'>%1</ul>"_s.arg(lines);
    }
    // --- factories: troubled ones first
    if (!fa.empty()) {
        auto trouble = [&](const FacRow &r) {
            QStringList t;
            if (!r.shortg.isEmpty())
                t << u"<span style='color:%1'>short of %2 — only %3 % of what its makers want exists</span>"_s.arg(
                    W, esc(good_name(r.shortg)), QString::number(py::round_int(100 * r.avail)));
            if (r.full < 0.5)
                t << u"<span style='color:%1'>only %2 % staffed</span>"_s.arg(W, QString::number(py::round_int(100 * r.full)));
            if (r.money_out - r.money_in < 0)
                t << u"<span style='color:%1'>loses £ %2 a day</span>"_s.arg(E, fmt(r.money_in - r.money_out));
            return t;
        };
        std::vector<std::pair<bool, const FacRow *>> keyed;
        for (const FacRow *r : fa)
            keyed.emplace_back(trouble(*r).isEmpty(), r);
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first)
                return !a.first;                     // (False < True: the troubled first)
            return -a.second->money_out < -b.second->money_out;
        });
        fa.clear();
        for (const auto &k : keyed)
            fa.push_back(k.second);
        py::Sum m_out_s, m_in_s, staffed_s, places_s;
        long long n_bad = 0;
        for (const FacRow *r : fa) {
            m_out_s.add(r->money_out);
            m_in_s.add(r->money_in);
            py::Sum g1, p1;
            for (const auto &[_k, v] : r->got)
                g1.add(v);
            for (const auto &[_k, v] : r->places)
                p1.add(v);
            staffed_s.add(g1.value());
            places_s.add(p1.value());
            if (!trouble(*r).isEmpty())
                n_bad += 1;
        }
        const double m_out = m_out_s.value(), m_in = m_in_s.value(), staffed = staffed_s.value(), places = places_s.value();
        out << u"<p><b style='color:%1'>Factories</b> — %2, %3 of %4 places filled (%5 %), make £ %6 a day from inputs "
               u"worth £ %7: <b>%8£ %9</b> left over.%10</p>"_s
                   .arg(G, QString::number(fa.size()), fmt(staffed, 0), fmt(places, 0),
                        QString::number(places ? py::round_int(100 * staffed / places) : 0), fmt(m_out), fmt(m_in),
                        m_out >= m_in ? QString() : u"−"_s, fmt(std::fabs(m_out - m_in)))
                   .arg(n_bad ? u" <span style='color:%1'><b>%2 with a problem</b> (listed first).</span>"_s.arg(
                                    W, QString::number(n_bad))
                              : QString());
        QString lines;
        const size_t lim = big ? std::min<size_t>(10, fa.size()) : fa.size();
        for (size_t i = 0; i < lim; ++i) {
            const FacRow &r = *fa[i];
            QStringList staff;
            for (const auto &[k, pv] : r.places)
                staff << u"%1 %2 of %3"_s.arg(k, fmt(r.got.value(k, 0.0), 0), fmt(pv, 0));
            std::vector<std::pair<QString, double>> sorted_in(r.inputs.begin(), r.inputs.end());
            std::sort(sorted_in.begin(), sorted_in.end());
            QStringList ins;
            for (const auto &[g, a] : sorted_in)
                ins << fmt(a, 1) + u' ' + esc(good_name(g));
            const bool hi = has_good && (r.good == gd || r.inputs.contains(gd));
            const QStringList t = trouble(r);
            QString bonus_s;
            if (r.bonus) {
                QStringList loc;
                for (const QString &g : r.local)
                    loc << esc(good_name(g));
                bonus_s = u" Bonus +%1 %%2."_s.arg(QString::number(py::round_int(100 * r.bonus)),
                                                  r.local.isEmpty() ? QString() : u" (%1 here)"_s.arg(loc.join(u", "_s)));
            }
            lines += u"<li%1><b>%2</b> (level %3, %4): %5 %6 a day (£ %7) from %8 (£ %9) — "_s
                         .arg(hi && t.isEmpty() ? u" style='color:%1'"_s.arg(G) : QString(),
                              esc(QString(r.building).replace(u'_', u' ')), QString::number(r.level), esc(r.state),
                              fmt(r.out, 2), esc(good_name(r.good)), fmt(r.money_out),
                              ins.isEmpty() ? u"nothing"_s : ins.join(u", "_s), fmt(r.money_in)) +
                     staff.join(u", "_s) + u"."_s + bonus_s +
                     (t.isEmpty() ? QString() : u" "_s + t.join(u"; "_s) + u"."_s) + u"</li>"_s;
        }
        if (big && fa.size() > 10)
            lines += u"<li>… and %1 more</li>"_s.arg(QString::number(fa.size() - 10));
        out << u"<ul style='margin-top:0'>%1</ul>"_s.arg(lines);
    } else if (!rg.empty() || !ar.empty()) {
        out << u"<p><b style='color:%1'>Factories</b> — none.</p>"_s.arg(G);
    }
    // --- artisans
    if (!ar.empty()) {
        long long people = 0, choices = 0;
        py::Sum money;
        Amounts mk;
        bool first = true;
        for (const ArtRow *r : ar) {
            people += r->people;
            choices = first ? r->choices : std::max(choices, r->choices);
            first = false;
            money.add(r->money);
            for (const auto &[g, v] : r->made)
                mk.insert(g, mk.value(g, 0.0) + v);
        }
        std::vector<std::pair<QString, double>> topk(mk.begin(), mk.end());
        std::stable_sort(topk.begin(), topk.end(), [&](const auto &a, const auto &b) {
            return -a.second * price_of(a.first) < -b.second * price_of(b.first);
        });
        if (topk.size() > 6)
            topk.resize(6);
        QStringList parts;
        for (const auto &[g, v] : topk)
            parts << ((has_good && g == gd) ? u"<b>%1 %2</b>"_s : u"%1 %2"_s).arg(fmt(v, 1), esc(good_name(g)));
        out << u"<p><b style='color:%1'>Artisans</b> — %2 people, choosing among %3 products each; expected to make £ %4 "
               u"a day: %5%6.</p>"_s
                   .arg(G, commas(people), QString::number(choices), fmt(money.value()), parts.join(u", "_s),
                        mk.size() > 6 ? u" …"_s : QString());
    }
    if (out.isEmpty())
        out << u"<p>Nothing is made here at the start: no farm or mine, no factory, no artisans.</p>"_s;
    const QString b = belongs(lv, u);
    const std::map<QString, QString> kinds = {{u"continent"_s, u"a continent"_s}, {u"nation"_s, u"a nation"_s},
                                              {u"state"_s, u"a state"_s}, {u"province"_s, u"a province"_s}};
    close_head->setText(u"<span style='font-size:15px; font-weight:700; color:%1'>Close look: %2</span>%3 &nbsp; <span "
                        u"style='color:%4'>(%5)</span>"_s.arg(G, esc(unit_name(lv, u)),
                                                              b.isEmpty() ? QString() : u" — "_s + esc(b),
                                                              theme::INK_FAINT, kinds.at(lv)));
    close_text->setText(out.join(QString()));
    if (close_box->isHidden()) {
        close_box->show();
        const int h = std::max(400, split->height());
        split->setSizes({int(h * 0.78), int(h * 0.22)});
    }
}

void EconWorkspace::_set_picked(const std::vector<long long> &pids)
{
    map->picked = std::set<long long>(pids.begin(), pids.end());
    _lit_list = pids;
}

std::optional<long long> EconWorkspace::_first_lit() const
{
    if (map->picked.empty())
        return std::nullopt;
    const std::set<long long> from(_lit_list.begin(), _lit_list.end());
    const std::vector<long long> order =
        py_set_order(from == map->picked ? _lit_list : std::vector<long long>(map->picked.begin(), map->picked.end()));
    return order.front();
}

}  // namespace econest
