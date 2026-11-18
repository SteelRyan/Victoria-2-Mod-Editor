// production.py — production types, prices, buildings, pop needs. Line by line.
#include "core/production.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/techdata.h"

#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace production {

using pypath::join;
using script::Node;
using script::Nodes;

const QStringList KINDS = {u"rgo"_s, u"artisan"_s, u"factory"_s};
static OrderedMap<QString, QString> om(std::initializer_list<std::pair<QString, QString>> l)
{
    OrderedMap<QString, QString> m;
    for (const auto &kv : l)
        m.insert(kv.first, kv.second);
    return m;
}
const OrderedMap<QString, QString> KIND_NAME = om({{u"rgo"_s, u"RGO"_s}, {u"artisan"_s, u"Artisans"_s}, {u"factory"_s, u"Factory"_s}});
const std::vector<std::pair<QString, QString>> NEEDS = {
    {u"life_needs"_s, u"Life needs"_s}, {u"everyday_needs"_s, u"Everyday needs"_s}, {u"luxury_needs"_s, u"Luxury needs"_s}};
const OrderedMap<QString, QString> BUILDING_KIND = om({{u"factory"_s, u"Factory building"_s}, {u"fort"_s, u"Fort"_s},
                                                       {u"naval_base"_s, u"Naval base"_s}, {u"infrastructure"_s, u"Railroad"_s}});
const double EFF_MISSING = 0.75;

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static OrderedMap<QString, QString> fields_of(const Nodes &nodes)
{
    OrderedMap<QString, QString> f;
    for (const Node &c : nodes)
        if (has(c) && !c.is_block)
            f.insert(c.key, c.value);
    return f;
}
static OptStr get(const OrderedMap<QString, QString> &f, const QString &k)
{
    const QString *p = f.get(k);
    return p ? OptStr(*p) : std::nullopt;
}
static OptStr or_(const OptStr &a, const OptStr &b) { return (a && !a->isEmpty()) ? a : b; }   // a or b
static const Nodes &kids_of(const std::shared_ptr<const Node> &r)
{
    static const Nodes NONE;
    return r ? r->kids : NONE;
}

QString pretty(const OptStr &key)
{
    if (!key || key->isEmpty())
        return u"—"_s;
    return py::title(py::strip(QString(*key).replace(u'_', u' ')));
}

Goods goods_block(const Nodes &nodes, const QString &key)
{
    const Node *b = nullptr;
    for (const Node &c : nodes)
        if (is(c, key) && c.is_block) {
            b = &c;
            break;
        }
    Goods out;
    if (b)
        for (const Node &c : b->kids)
            if (has(c) && !c.is_block)
                if (const auto f = py::to_float(c.value))
                    out.emplace_back(c.key, *f);
    return out;
}

double num(const OptStr &v, double dflt) { return num_or_none(v).value_or(dflt); }
std::optional<double> num_or_none(const OptStr &v) { return v ? py::to_float(*v) : std::nullopt; }

// ---------------------------------------------------------------------- ProdDB
ProdDB::ProdDB(moddata::Mod &mod_) : mod(mod_) { load(); }

void ProdDB::load()
{
    path = mod.p({u"common"_s, u"production_types.txt"_s});
    const QString text = fs::isfile(path) ? script::read_text(path) : QString();
    entries.clear();
    templates.clear();
    order.clear();
    for (const Node &n : script::parse(text)) {
        if (!(has(n) && n.is_block))
            continue;
        const auto f = fields_of(n.kids);
        ProdEntry e{{n.kstart, n.vend}, py::slice(text, n.kstart, n.vend), !f.contains(u"output_goods"_s)};
        entries.insert(n.key, e);
        order << n.key;
        if (e.tmpl)
            templates.insert(n.key, Template{get(f, u"type"_s), get(f, u"workforce"_s), goods_block(n.kids, u"efficiency"_s)});
    }
    // buildings: every block (factory buildings point at their production type)
    bpath = mod.p({u"common"_s, u"buildings.txt"_s});
    buildings.clear();
    building_text.clear();
    bld_order.clear();
    if (fs::isfile(bpath)) {
        const QString btext = script::read_text(bpath);
        for (const Node &n : script::parse(btext))
            if (has(n) && n.is_block) {
                const auto f = fields_of(n.kids);
                building_text.insert(n.key, BuildingText{n.kstart, n.vend, py::slice(btext, n.kstart, n.vend), get(f, u"type"_s)});
                bld_order << n.key;
                if (const OptStr pt = get(f, u"production_type"_s); pt && !pt->isEmpty())
                    buildings.insert(*pt, n.key);
            }
    }
    // goods.txt: each good's price (the value's place in the file, for a surgical change)
    gpath = mod.p({u"common"_s, u"goods.txt"_s});
    costs.clear();
    if (fs::isfile(gpath)) {
        const QString gtext = script::read_text(gpath);
        for (const Node &grp : script::parse(gtext)) {
            if (!(has(grp) && grp.is_block))
                continue;
            for (const Node &g : grp.kids)
                if (has(g) && g.is_block) {
                    const Node *c = nullptr;
                    for (const Node &x : g.kids)
                        if (is(x, u"cost"_s) && !x.is_block) {
                            c = &x;
                            break;
                        }
                    costs.insert(g.key, c ? Cost{c->vstart, c->vend, c->value} : Cost{std::nullopt, g.vstart + 1, u"0"_s});
                }
        }
    }
    // pop types: the mod's file, else the base game's
    pop_files.clear();
    for (const auto &kv : mod.poptypes) {
        const QString p = mod.p({u"poptypes"_s, kv.first + u".txt"_s});
        if (fs::isfile(p))
            pop_files.insert(kv.first, PopFile{p, script::read_text(p), mod.in_mod(p)});
    }
}

QStringList ProdDB::types() const
{
    QStringList out;
    for (const QString &k : order)
        if (!entries.get(k)->tmpl)
            out << k;
    return out;
}

// ---------------------------------------------------------------------- reading a block
Desc describe(const ProdDB &db, const QString &text, const std::function<QString(const QString &)> &tpl_text)
{
    // A production type's text, read. An own `efficiency` block REPLACES the template's — an empty one means none
    // (measured in the game 2026-10-15). tpl_text(name): a template's text as edited (else: as on disk).
    const auto r = techdata::root(text);
    const Nodes &nodes = kids_of(r);
    const auto f = fields_of(nodes);
    const OptStr tname = get(f, u"template"_s);
    Template tpl = db.templates.value(tname.value_or(QString()));
    const bool tpl_known = db.templates.contains(tname.value_or(QString()));
    if (tpl_text && tname && db.templates.contains(*tname)) {
        const auto tr = techdata::root(tpl_text(*tname));
        tpl.efficiency = goods_block(kids_of(tr), u"efficiency"_s);
    }
    Desc d;
    d.kind = or_(get(f, u"type"_s), tpl_known ? tpl.type : std::nullopt).value_or(QString());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (!(is(c, u"bonus"_s) && c.is_block))
            continue;
        Bonus b{static_cast<long long>(i), {}, u"0"_s};
        for (const Node &x : c.kids)
            if (is(x, u"trigger"_s) && x.is_block) {
                for (const Node &y : x.kids)
                    if (has(y) && !y.is_block)
                        b.conds.emplace_back(y.key, y.value);
                break;
            }
        for (const Node &x : c.kids)
            if (is(x, u"value"_s)) {
                b.value = x.is_block ? QString() : x.value;   // (Python would keep a block's list here)
                break;
            }
        d.bonuses.push_back(b);
    }
    d.own_eff = goods_block(nodes, u"efficiency"_s);
    d.has_own_eff = std::any_of(nodes.begin(), nodes.end(), [](const Node &c) { return is(c, u"efficiency"_s) && c.is_block; });
    const OptStr v = get(f, u"value"_s);
    d.value = v ? py::to_float(*v).value_or(0.0) : 0.0;
    d.tmpl = tname;
    const QString out = py::strip(get(f, u"output_goods"_s).value_or(QString()));
    d.output = out.isEmpty() ? std::nullopt : OptStr(out);
    d.inputs = goods_block(nodes, u"input_goods"_s);
    d.efficiency = d.has_own_eff ? d.own_eff : (tpl_known || (tpl_text && tname) ? tpl.efficiency : Goods());
    d.farm = get(f, u"farm"_s) == OptStr(u"yes"_s);
    d.mine = get(f, u"mine"_s) == OptStr(u"yes"_s);
    d.coastal = get(f, u"is_coastal"_s) == OptStr(u"yes"_s);
    d.workforce = or_(get(f, u"workforce"_s), tpl_known ? tpl.workforce : std::nullopt);
    return d;
}

BuildingDesc describe_building(const QString &text)
{
    const auto r = techdata::root(text);
    const Nodes &nodes = kids_of(r);
    const auto f = fields_of(nodes);
    return BuildingDesc{or_(get(f, u"type"_s), std::nullopt).value_or(QString()), goods_block(nodes, u"goods_cost"_s),
                        get(f, u"production_type"_s), get(f, u"time"_s), get(f, u"max_level"_s), get(f, u"cost"_s)};
}

static const Node *top_block(const Nodes &nodes, const QString &key)
{
    for (const Node &n : nodes)
        if (is(n, key) && n.is_block)
            return &n;
    return nullptr;
}

OrderedMap<QString, Goods> needs_of(const QString &text)
{
    // A pop type file's text -> {life_needs: [(good, amount)], everyday_needs: […], luxury_needs: […]}.
    const Nodes nodes = script::parse(text);
    OrderedMap<QString, Goods> out;
    for (const auto &[k, _t] : NEEDS)
        out.insert(k, goods_block(nodes, k));
    return out;
}

QString edit_needs(QString text, const QString &kind, const std::function<QString(const QString &)> &fn)
{
    // A pop type file with its `kind` block changed by fn(block text) -> block text; a missing block is added at the
    // end of the file first.
    Nodes nodes = script::parse(text);
    const Node *n = top_block(nodes, kind);
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    if (!n) {
        text = py::rstrip(text) + nl + nl + kind + u" = {"_s + nl + u'}' + nl;
        nodes = script::parse(text);
        n = top_block(nodes, kind);
    }
    return py::head(text, n->kstart) + fn(py::slice(text, n->kstart, n->vend)) + py::tail(text, n->vend);
}

QString need_set(const QString &text, const QString &kind, const QString &good, const OptStr &amount)
{
    // good = amount in the needs block (added when it isn't there; amount None takes it out).
    return edit_needs(text, kind, [&](const QString &block) -> QString {
        const auto r = techdata::root(block);
        std::optional<qsizetype> i;
        for (size_t j = 0; j < kids_of(r).size(); ++j)
            if (is(kids_of(r)[j], good) && !kids_of(r)[j].is_block) {
                i = qsizetype(j);
                break;
            }
        if (!amount)
            return i ? techdata::remove_node(block, {*i}) : block;
        if (!i)
            return techdata::add_line(block, {}, good, *amount);
        return techdata::set_value(block, {*i}, *amount);
    });
}

// ---------------------------------------------------------------------- ProdEdits
QStringList ProdEdits::keys() const
{
    QStringList out;
    for (const QString &k : db.types())
        if (!removed.contains(k))
            out << k;
    for (const auto &kv : added)
        out << kv.first;
    return out;
}

bool ProdEdits::exists(const QString &key) const
{
    return (db.entries.contains(key) && !removed.contains(key)) || added.contains(key);
}

QString ProdEdits::text(const QString &key) const
{
    if (const QString *a = added.get(key))
        return *a;
    if (const QString *w = want.get(key))
        return *w;
    return db.entries.get(key)->text;
}

Desc ProdEdits::info(const QString &key) const
{
    return describe(db, text(key), [this](const QString &k) { return text(k); });   // (its template as edited)
}

QStringList ProdEdits::templates() const
{
    QStringList out;
    for (const QString &k : db.order)
        if (db.entries.get(k)->tmpl)
            out << k;
    return out;
}

std::pair<QStringList, QStringList> ProdEdits::users_of(const QString &tpl) const
{
    QStringList out, own;
    for (const QString &k : keys()) {
        const Desc d = info(k);
        if (d.tmpl == OptStr(tpl)) {
            out << k;
            if (d.has_own_eff)
                own << k;
        }
    }
    return {out, own};
}

double ProdEdits::eff_cost(const Desc &d, const std::function<double(const QString &)> &price) const
{
    // A day's efficiency goods at full staff, per level: Σ amount × price (MEASURED: bought every day).
    py::Sum s;                                    // (Python's sum: compensated)
    for (const auto &[g, a] : d.efficiency)
        s.add(a * price(g));
    return s.value();
}

void ProdEdits::set(const QString &key, const QString &t)
{
    if (added.contains(key))
        added.insert(key, t);
    else if (t == db.entries.get(key)->text)
        want.remove(key);
    else
        want.insert(key, t);
}

void ProdEdits::add(const QString &key, const QString &t, const std::optional<std::pair<QString, QString>> &building)
{
    added.insert(key, t);
    if (building)
        new_buildings.insert(key, *building);
}

void ProdEdits::remove(const QString &key)
{
    if (added.contains(key)) {
        added.remove(key);
        new_buildings.remove(key);
    } else {
        removed.insert(key);
        want.remove(key);
        if (const QString *b = db.buildings.get(key); b && !b->isEmpty())
            bld_want.remove(*b);
    }
}

OptStr ProdEdits::building_of(const QString &key) const
{
    if (const auto *nb = new_buildings.get(key))
        return nb->first;
    const QString *b = db.buildings.get(key);
    return b ? OptStr(*b) : std::nullopt;
}

double ProdEdits::cost(const QString &good) const
{
    OptStr v;
    if (const QString *w = cost_want.get(good))
        v = *w;
    if (!v) {
        const Cost *c = db.costs.get(good);
        v = c ? c->value : u"0"_s;
    }
    return num(v);
}

OrderedMap<QString, double> ProdEdits::costs() const
{
    OrderedMap<QString, double> out;
    for (const auto &kv : db.costs)
        out.insert(kv.first, cost(kv.first));
    return out;
}

QStringList ProdEdits::buildings() const
{
    // Every building shown on the chart: the file's (without those of removed factories) + new factory ones.
    QSet<OptStr> gone;
    for (const QString &k : removed) {
        const QString *b = db.buildings.get(k);
        gone.insert(b ? OptStr(*b) : std::nullopt);
    }
    QStringList out;
    for (const QString &b : db.bld_order)
        if (!gone.contains(b))
            out << b;
    for (const auto &[_k, bt] : new_buildings)
        if (!out.contains(bt.first))
            out << bt.first;
    return out;
}

QString ProdEdits::bld_text(const QString &b) const
{
    if (const QString *w = bld_want.get(b))
        return *w;
    for (const auto &[_k, bt] : new_buildings)
        if (bt.first == b)
            return bt.second;
    return db.building_text.get(b)->text;
}

BuildingDesc ProdEdits::bld_info(const QString &b) const { return describe_building(bld_text(b)); }

bool ProdEdits::bld_is_new(const QString &b) const
{
    for (const auto &[_k, bt] : new_buildings)
        if (bt.first == b)
            return true;
    return false;
}

QStringList ProdEdits::poptypes() const
{
    QStringList out;
    for (const auto &kv : db.pop_files)
        out << kv.first;
    return out;
}

QString ProdEdits::pop_text(const QString &pt) const
{
    if (store)                                    // the editor's script texts hold the pop type files
        return store->text(pop_rel(pt));
    if (const QString *w = need_want.get(pt))
        return *w;
    return db.pop_files.get(pt)->text;
}

QString ProdEdits::pop_rel(const QString &pt) const { return u"poptypes/"_s + pypath::basename(db.pop_files.get(pt)->path); }

OrderedMap<QString, Goods> ProdEdits::needs(const QString &pt) const { return needs_of(pop_text(pt)); }

void ProdEdits::set_other(const QString &what, const QString &key, const QString &value)
{
    // A price ("cost", good, value), a building ("bld", key, text) or a pop type file ("need", type, text).
    if (what == u"cost") {
        const Cost *c = db.costs.get(key);
        const QString orig = c ? c->value : u"0"_s;
        if (num_or_none(value) && num(value) == num(orig))
            cost_want.remove(key);
        else
            cost_want.insert(key, value);
    } else if (what == u"bld") {
        for (auto &[k, bt] : new_buildings)
            if (bt.first == key) {                // a new factory's building: its text itself
                bt.second = value;
                return;
            }
        if (value == db.building_text.get(key)->text)
            bld_want.remove(key);
        else
            bld_want.insert(key, value);
    } else if (what == u"need") {
        if (value == db.pop_files.get(key)->text)
            need_want.remove(key);
        else
            need_want.insert(key, value);
    }
}

store::Snap ProdEdits::snapshot() const
{
    return store::box(ProdSnap{want, added, removed, new_buildings, cost_want, bld_want, need_want});
}

void ProdEdits::restore(const store::Snap &snap)
{
    const ProdSnap &s = store::unbox<ProdSnap>(snap);
    want = s.want;
    added = s.added;
    removed = s.removed;
    new_buildings = s.new_buildings;
    cost_want = s.cost_want;
    bld_want = s.bld_want;
    need_want = s.need_want;
}

static QStringList sorted_with(const QString &prefix, const QStringList &ks)
{
    QStringList out;
    for (const QString &k : ks)
        out << prefix + k;
    std::sort(out.begin(), out.end());
    return out;
}
template <typename M>
static QStringList keys_of(const M &m)
{
    QStringList out;
    for (const auto &kv : m)
        out << kv.first;
    return out;
}

QStringList ProdEdits::dirty() const
{
    QStringList changed;
    for (const auto &[k, t] : want)
        if (t != db.entries.get(k)->text)
            changed << k;
    std::sort(changed.begin(), changed.end());
    return changed + sorted_with(QString(), keys_of(added)) + sorted_with(u"removed "_s, QStringList(removed.begin(), removed.end())) +
           sorted_with(u"price of "_s, keys_of(cost_want)) + sorted_with(u"building "_s, keys_of(bld_want)) +
           sorted_with(u"needs of "_s, keys_of(need_want));
}

QString ProdEdits::new_key(const QString &base) const
{
    // re.sub(r"[^a-z0-9_]", "_", base.lower()).strip("_") or "production"
    QString k;
    for (QChar c : py::lower(base)) {
        const char16_t u = c.unicode();
        k += ((u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'_') ? c : QChar(u'_');
    }
    k = py::strip(k, u"_"_s);
    if (k.isEmpty())
        k = u"production"_s;
    long long n = 2;
    QString out = k;
    while (db.entries.contains(out) || added.contains(out) || db.building_text.contains(out)) {
        out = u"%1_%2"_s.arg(k).arg(n);
        n += 1;
    }
    return out;
}

QString ProdEdits::template_text(const QString &what, const QString &key, const QString &output) const
{
    // A new production type in the file's own style (the template most used by its kind).
    const QString nl = u"\r\n"_s;
    OrderedMap<QString, long long> tpls;
    for (const QString &k : db.types()) {
        const Desc d = describe(db, db.entries.get(k)->text);
        if (d.kind == what && d.tmpl && !d.tmpl->isEmpty())
            tpls[*d.tmpl] += 1;
    }
    OptStr tpl;
    long long top = 0;
    for (const auto &[t, n] : tpls)               // max(tpls, key=tpls.get): the first of the most used
        if (n > top) {
            tpl = t;
            top = n;
        }
    QStringList lines;
    if (what == u"artisan") {
        lines = {key + u" = {       "_s, u"\tinput_goods = {"_s, u"\t}"_s, u"\toutput_goods = "_s + output, u"\tvalue = 1"_s,
                 u"\t"_s, u"\towner = {"_s, u"\t\tpoptype = artisans "_s, u"\t\teffect = output"_s, u"\t}"_s, u"\t"_s,
                 u"\ttype = artisan"_s, u"\t"_s, u"\tworkforce = 10000"_s, u"}"_s};
    } else if (what == u"rgo") {
        lines = {key + u" = {"_s, u"\ttemplate = "_s + tpl.value_or(u"RGO_template_farmers"_s),
                 u"\toutput_goods = "_s + output + u' ', u"\tvalue = 1"_s, u"\tfarm = yes"_s, u"}"_s};
    } else {
        lines = {key + u" = {       "_s, u"\ttemplate = "_s + tpl.value_or(u"factory_template"_s), u"\tinput_goods = {"_s,
                 u"\t}"_s, u"\toutput_goods = "_s + output + u' ', u"\tvalue = 1"_s, u"}"_s};
    }
    return lines.join(nl);
}

std::optional<std::pair<QString, QString>> ProdEdits::building_text(const QString &prod_key) const
{
    // A factory building for a new factory: a copy of another factory building, pointing at it.
    const OptStr tpl = exists(prod_key) ? describe(db, text(prod_key)).tmpl : std::nullopt;
    std::vector<std::tuple<bool, bool, QString>> cands;
    for (const auto &[pt, b] : db.buildings) {
        const BuildingText *v = db.building_text.get(b);
        if (!v || v->type != OptStr(u"factory"_s) || !db.entries.contains(pt))
            continue;
        const bool same = describe(db, db.entries.get(pt)->text).tmpl == OptStr((tpl && !tpl->isEmpty()) ? *tpl : u"factory_template"_s);
        const bool normal = QString(v->text).replace(u'\t', u' ').contains(u"pop_build_factory = yes"_s);
        cands.emplace_back(!same, !normal, b);
    }
    if (cands.empty())
        return std::nullopt;
    std::sort(cands.begin(), cands.end());
    const QString b = std::get<2>(cands[0]);      // a normal factory of the same template (glass, cement …)
    const BuildingText &src = *db.building_text.get(b);
    const QString bkey = !db.building_text.contains(prod_key) ? prod_key : prod_key + u"_building"_s;
    QString t = src.text;
    // Python's \s / \S for str: Unicode white space and \x1c-\x1f
    const QString S = u"[\\s\\x1c-\\x1f]"_s, NS = u"[^\\s\\x1c-\\x1f]"_s;
    const auto opt = QRegularExpression::UseUnicodePropertiesOption;
    {
        const QRegularExpression rx(u"^"_s + S + u"*"_s + QRegularExpression::escape(b) + u"("_s + S + u"*=)"_s, opt);
        const auto m = rx.match(t);
        if (m.hasMatch())
            t = t.left(m.capturedStart(0)) + bkey + m.captured(1) + t.mid(m.capturedEnd(0));
    }
    {
        const QRegularExpression rx(u"(production_type"_s + S + u"*="_s + S + u"*)"_s + NS + u"+"_s, opt);
        const auto m = rx.match(t);
        if (m.hasMatch())
            t = t.left(m.capturedStart(0)) + m.captured(1) + prod_key + t.mid(m.capturedEnd(0));
    }
    t.replace(QRegularExpression(u"\\n[ \\t]*default_enabled"_s + S + u"*="_s + S + u"*yes[^\\n]*"_s, opt), QString());
    return std::make_pair(bkey, t);              // (unlocked by a tech / invention)
}

std::optional<QStringList> ProdEdits::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"production"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    QStringList written;
    auto backup = [&](const QString &dst, const QString &name) {
        if (fs::isfile(dst)) {
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, name));
        }
    };
    using Edits = std::vector<std::tuple<qsizetype, qsizetype, QString>>;
    auto apply = [](QString text, Edits edits) {
        std::stable_sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) {
            return std::make_pair(-std::get<0>(a), -std::get<1>(a)) < std::make_pair(-std::get<0>(b), -std::get<1>(b));
        });
        for (const auto &[s, e, t] : edits)
            text = py::head(text, s) + t + py::tail(text, e);
        return text;
    };
    auto line_span = [](const QString &text, qsizetype s, qsizetype e) {
        const qsizetype ls = py::rfind(text, u'\n', s) + 1;
        qsizetype le = text.indexOf(u'\n', e);
        le = le < 0 ? text.size() : le + 1;
        return std::make_pair(ls, le);
    };
    // production_types.txt
    if (!want.isEmpty() || !added.isEmpty() || !removed.isEmpty()) {
        const QString text = script::read_text(db.path);
        const QString dst = join(mod.root, {u"common"_s, u"production_types.txt"_s});
        backup(dst, u"production_types.txt"_s);
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        Edits edits;
        for (const auto &[k, t] : want) {
            const auto [s, e] = db.entries.get(k)->span;
            edits.emplace_back(s, e, t);
        }
        for (const QString &k : removed) {
            const auto [s, e] = db.entries.get(k)->span;
            const auto [ls, le] = line_span(text, s, e);
            edits.emplace_back(ls, le, QString());
        }
        // new ones after the last production type of their kind
        for (const auto &[k, t] : added) {
            const QString kind_ = describe(db, t).kind;
            std::optional<qsizetype> at;
            for (const QString &x : db.types())
                if (describe(db, db.entries.get(x)->text).kind == kind_)
                    at = std::max(at.value_or(db.entries.get(x)->span.second), db.entries.get(x)->span.second);
            const qsizetype a = at ? *at : text.size();
            edits.emplace_back(a, a, nl + nl + t);
        }
        script::write_text(dst, apply(text, edits));
        written << dst;
    }
    // buildings.txt: changed buildings, new factories' buildings in, removed factories' buildings out
    QStringList gone;
    for (const QString &k : removed)
        if (const QString *b = db.buildings.get(k))
            gone << *b;
    if (!new_buildings.isEmpty() || !gone.isEmpty() || !bld_want.isEmpty()) {
        const QString btext = script::read_text(db.bpath);
        const QString bdst = join(mod.root, {u"common"_s, u"buildings.txt"_s});
        backup(bdst, u"buildings.txt"_s);
        const QString bnl = btext.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        Edits bed;
        for (const auto &[b, t] : bld_want)
            if (!gone.contains(b))
                bed.emplace_back(db.building_text.get(b)->start, db.building_text.get(b)->end, t);
        for (const QString &b : gone) {
            const auto [ls, le] = line_span(btext, db.building_text.get(b)->start, db.building_text.get(b)->end);
            bed.emplace_back(ls, le, QString());
        }
        std::optional<qsizetype> lf;
        for (const auto &kv : db.building_text)
            if (kv.second.type == OptStr(u"factory"_s))
                lf = std::max(lf.value_or(kv.second.end), kv.second.end);
        const qsizetype last_factory = lf ? *lf : btext.size();
        for (const auto &[k, bt] : new_buildings)
            if (added.contains(k))
                bed.emplace_back(last_factory, last_factory, bnl + bnl + bt.second);
        script::write_text(bdst, apply(btext, bed));
        written << bdst;
    }
    // goods.txt: prices
    if (!cost_want.isEmpty()) {
        const QString gtext = script::read_text(db.gpath);
        const QString gdst = join(mod.root, {u"common"_s, u"goods.txt"_s});
        backup(gdst, u"goods.txt"_s);
        const QString gnl = gtext.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        Edits ged;
        for (const auto &[g, v] : cost_want) {
            const Cost &c = *db.costs.get(g);
            if (!c.start)                          // no cost line yet: the first line of the good's block
                ged.emplace_back(c.end, c.end, gnl + u"\t\tcost = "_s + v);
            else
                ged.emplace_back(*c.start, c.end, v);
        }
        script::write_text(gdst, apply(gtext, ged));
        written << gdst;
    }
    // poptypes/*.txt: needs (a base game pop type gets its own copy in the mod)
    for (const auto &[pt, t] : need_want) {
        const QString pdst = join(mod.root, {u"poptypes"_s, pt + u".txt"_s});
        backup(pdst, pt + u".txt"_s);
        fs::makedirs(pypath::dirname(pdst));
        script::write_text(pdst, t);
        written << pdst;
    }
    want.clear();
    added.clear();
    removed.clear();
    new_buildings.clear();
    cost_want.clear();
    bld_want.clear();
    need_want.clear();
    db.load();
    try {
        mod._load_production();
    } catch (...) {
    }
    return written;
}

}  // namespace production
