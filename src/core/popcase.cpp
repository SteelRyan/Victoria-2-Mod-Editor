// popcase.py — the case of Pop influences and the reading of a card's conditions. Line by line.
#include "core/popcase.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace popcase {

const OrderedMap<QString, Var> VARS = {
    {u"military_spending"_s, {u"Military spending"_s, 0.0, 1.0, 0.5, u"pct"_s}},
    {u"education_spending"_s, {u"Education spending"_s, 0.0, 1.0, 0.5, u"pct"_s}},
    {u"crime_fighting"_s, {u"Administration spending"_s, 0.0, 1.0, 0.5, u"pct"_s}},
    {u"social_spending"_s, {u"Social spending"_s, 0.0, 1.0, 0.5, u"pct"_s}},
    {u"literacy"_s, {u"Literacy of the pop"_s, 0.0, 1.0, 0.3, u"pct"_s}},
    {u"consciousness"_s, {u"Consciousness"_s, 0.0, 10.0, 3.0, u"ten"_s}},
    {u"militancy"_s, {u"Militancy"_s, 0.0, 10.0, 1.0, u"ten"_s}},
    {u"life_needs"_s, {u"Life needs met"_s, 0.0, 1.0, 1.0, u"pct"_s}},
    {u"everyday_needs"_s, {u"Everyday needs met"_s, 0.0, 1.0, 0.7, u"pct"_s}},
    {u"luxury_needs"_s, {u"Luxury needs met"_s, 0.0, 1.0, 0.2, u"pct"_s}},
    {u"unemployment"_s, {u"Unemployed"_s, 0.0, 1.0, 0.0, u"pct"_s}},
    {u"agree_with_ruling_party"_s, {u"Agrees with the ruling party"_s, 0.0, 1.0, 0.5, u"pct"_s}},
    {u"political_reform_want"_s, {u"Wants political reform"_s, 0.0, 1.0, 0.2, u"pct"_s}},
    {u"social_reform_want"_s, {u"Wants social reform"_s, 0.0, 1.0, 0.2, u"pct"_s}},
    {u"recruited_percentage"_s, {u"Soldiers already in regiments"_s, 0.0, 1.0, 0.25, u"pct"_s}},
    {u"revanchism"_s, {u"Revanchism"_s, 0.0, 1.0, 0.0, u"pct"_s}},
    {u"war_exhaustion"_s, {u"War exhaustion"_s, 0.0, 100.0, 0.0, u"num"_s}},
    {u"plurality"_s, {u"Plurality"_s, 0.0, 100.0, 30.0, u"num"_s}},
    {u"life_rating"_s, {u"Life rating of the province"_s, 0.0, 50.0, 35.0, u"num"_s}},
    {u"total_pops"_s, {u"People in the province"_s, 0.0, 2000000.0, 100000.0, u"num"_s}}};

static OrderedMap<QString, QString> make_alias()
{
    OrderedMap<QString, QString> out;
    for (const QString &s : {u"poor"_s, u"middle"_s, u"rich"_s})
        for (const QString &k : {u"life_needs"_s, u"everyday_needs"_s, u"luxury_needs"_s, u"militancy"_s})
            out.insert(u"%1_strata_%2"_s.arg(s, k), k);
    return out;
}
const OrderedMap<QString, QString> ALIAS = make_alias();
const QSet<QString> SCOPES = {u"and"_s,   u"country"_s,        u"state_scope"_s,   u"location"_s,
                              u"owner"_s, u"province_scope"_s, u"capital_scope"_s, u"pop"_s};
const QSet<QString> FACTS_ON = {u"controlled_by = owner"_s,      u"culture = this"_s,     u"is_primary_culture = yes"_s,
                                u"is_accepted_culture = yes"_s,  u"civilized = yes"_s,    u"has_factories = yes"_s,
                                u"is_state_religion = yes"_s};

std::optional<double> _num(const QString &v) { return py::to_float(py::strip(v)); }

QString _norm(const QString &text) { return py::split(text).join(u' '); }

static Tree leaf(const QString &t, const QString &a, double num = 0.0, bool want = true)
{
    Tree x;
    x.t = t;
    x.a = a;
    x.num = num;
    x.want = want;
    return x;
}

Tree compile_cond(const QString &src, const QStringList &types)
{
    // (Python keeps the trees in a module-wide cache by (src, len(types)))
    static QHash<QString, QHash<qsizetype, Tree>> cache;
    auto c1 = cache.find(src);
    if (c1 != cache.end()) {
        auto c2 = c1->find(types.size());
        if (c2 != c1->end())
            return *c2;
    }
    script::Nodes nodes;
    try {
        nodes = script::parse(src);
    } catch (...) {
        nodes.clear();
    }
    std::function<std::optional<Tree>(const script::Node &)> one = [&](const script::Node &n) -> std::optional<Tree> {
        const QString k = py::lower(n.has_key ? n.key : QString());
        if (n.is_block) {
            std::vector<Tree> kids;
            for (const script::Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty())
                    if (auto x = one(c))
                        kids.push_back(*x);
            Tree t;
            t.kids = kids;
            if (k == u"not") {
                t.t = u"nor"_s;
                return t;
            }
            if (k == u"or") {
                t.t = u"or"_s;
                return t;
            }
            if (SCOPES.contains(k)) {
                t.t = u"and"_s;
                return t;
            }
            return leaf(u"fact"_s, _norm(py::slice(src, n.kstart, n.vend)), 0.0, true);   // a block not read: a fact
        }
        const QString v = py::strip(py::strip(n.value), u"\""_s);
        const std::optional<double> num = _num(v);
        if (k == u"factor")
            return std::nullopt;
        if (k == u"strata")
            return leaf(u"strata"_s, py::lower(v));
        const QString k2 = ALIAS.contains(k) ? ALIAS.value(k) : k;
        if (num && VARS.contains(k2))
            return leaf(u"var"_s, k2, *num);
        if (num && types.contains(k))
            return leaf(u"share"_s, k, *num);
        if (py::lower(v) == u"yes" || py::lower(v) == u"no")
            return leaf(u"fact"_s, k + u" = yes"_s, 0.0, py::lower(v) == u"yes");
        return leaf(u"fact"_s, k + u" = "_s + v, 0.0, true);
    };
    Tree tree;
    tree.t = u"and"_s;
    for (const script::Node &n : nodes)
        if (n.has_key && !n.key.isEmpty())
            if (auto x = one(n))
                tree.kids.push_back(*x);
    cache[src][types.size()] = tree;
    return tree;
}

std::tuple<QSet<QString>, QSet<QString>, bool> parts(const Tree &tree)
{
    QSet<QString> vs, fs;
    bool dyn = false;
    std::function<void(const Tree &)> walk = [&](const Tree &t) {
        if (t.t == u"and" || t.t == u"or" || t.t == u"nor") {
            for (const Tree &c : t.kids)
                walk(c);
        } else if (t.t == u"var") {
            vs.insert(t.a);
        } else if (t.t == u"fact") {
            fs.insert(t.a);
        } else if (t.t == u"share") {
            dyn = true;
        }
    };
    walk(tree);
    return {vs, fs, dyn};
}

void Case::reset()
{
    vals.clear();
    for (const auto &[k, v] : VARS)
        vals.insert(k, v.start);
    facts = FACTS_ON;
}

bool Case::holds(const Tree &tree, const QString &strata, const Shares &shares) const
{
    const QString &t = tree.t;
    if (t == u"and")
        return std::all_of(tree.kids.begin(), tree.kids.end(), [&](const Tree &c) { return holds(c, strata, shares); });
    if (t == u"or")
        return std::any_of(tree.kids.begin(), tree.kids.end(), [&](const Tree &c) { return holds(c, strata, shares); });
    if (t == u"nor")
        return !std::any_of(tree.kids.begin(), tree.kids.end(), [&](const Tree &c) { return holds(c, strata, shares); });
    if (t == u"var")
        return vals.value(tree.a, 0.0) >= tree.num - 1e-9;
    if (t == u"share")
        return shares.value(tree.a, 0.0) >= tree.num - 1e-12;
    if (t == u"strata")
        return strata == tree.a;
    if (t == u"fact")
        return facts.contains(tree.a) == tree.want;
    return true;
}

void Case::set_fact(const QString &text, bool on)
{
    if (on) {
        if (text.startsWith(u"nationalvalue = "_s)) {             // a nation has one national value
            QSet<QString> keep;
            for (const QString &f : facts)
                if (!f.startsWith(u"nationalvalue = "_s))
                    keep.insert(f);
            facts = keep;
        }
        facts.insert(text);
    } else {
        facts.remove(text);
    }
}

double Case::below(const QString &key, double number) const
{
    const Var &v = VARS.get(key) ? *VARS.get(key) : throw std::out_of_range("KeyError");
    const double step = v.kind == u"pct" ? 0.01 : v.kind == u"ten" ? 0.1 : 1.0;
    return std::max(v.lo, number - step);
}

static std::vector<const Tree *> sharing_last(const std::vector<Tree> &kids)
{
    std::vector<const Tree *> out;
    for (const Tree &c : kids)
        out.push_back(&c);
    std::stable_sort(out.begin(), out.end(), [](const Tree *a, const Tree *b) {
        const bool x = a->t == u"share" || a->t == u"strata", y = b->t == u"share" || b->t == u"strata";
        return !x && y;
    });
    return out;
}

bool Case::make(const Tree &tree, bool want, const QString &strata, const Shares &shares)
{
    const QString &t = tree.t;
    if (t == u"and") {
        if (want) {
            for (const Tree &c : tree.kids)
                make(c, true, strata, shares);
        } else {
            for (const Tree *c : sharing_last(tree.kids))
                if (make(*c, false, strata, shares))
                    break;
        }
    } else if (t == u"or") {
        if (want) {
            for (const Tree *c : sharing_last(tree.kids))
                if (make(*c, true, strata, shares))
                    break;
        } else {
            for (const Tree &c : tree.kids)
                make(c, false, strata, shares);
        }
    } else if (t == u"nor") {
        if (want) {
            for (const Tree &c : tree.kids)
                make(c, false, strata, shares);
        } else {
            for (const Tree *c : sharing_last(tree.kids))
                if (make(*c, true, strata, shares))
                    break;
        }
    } else if (t == u"var") {
        if (want)
            vals.insert(tree.a, std::max(vals.value(tree.a, 0.0), tree.num));
        else if (vals.value(tree.a, 0.0) >= tree.num)
            vals.insert(tree.a, below(tree.a, tree.num));
    } else if (t == u"fact") {
        set_fact(tree.a, want == tree.want);
    }
    return holds(tree, strata, shares) == want;
}

Block::Block(const Chance &ch, const QStringList &types)
{
    const std::optional<double> b = (ch.base && !ch.base->isEmpty()) ? _num(*ch.base) : std::optional<double>(1.0);
    base = b ? *b : 1.0;
    for (const ChanceMod &m : ch.mods) {
        const std::optional<double> f = _num(m.factor);
        if (f && *f != 0.0) {
            Tree tree = compile_cond(m.src, types);
            const bool dyn = std::get<2>(parts(tree));
            cards.push_back({*f, std::move(tree), dyn});
        }
    }
}

Block::Prepared Block::prepare(const Case &c, const QString &strata) const
{
    py::Sum st;
    for (const Card &cd : cards)
        if (!cd.dyn && c.holds(cd.tree, strata, {}))
            st.add(cd.f);
    std::vector<std::pair<double, const Tree *>> dyn;
    for (const Card &cd : cards)
        if (cd.dyn)
            dyn.emplace_back(cd.f, &cd.tree);
    return {st.value(), dyn};
}

double Block::number(const Case &c, const QString &strata, const Shares &shares, const Prepared *prepared) const
{
    const Prepared own = prepared ? Prepared() : prepare(c, strata);
    const Prepared &p = prepared ? *prepared : own;
    py::Sum s;
    for (const auto &[f, tree] : p.second)
        if (c.holds(*tree, strata, shares))
            s.add(f);
    return base * (p.first + s.value());
}

QString show(const QString &key, double value)
{
    const QString kind = VARS.get(key) ? VARS.get(key)->kind : throw std::out_of_range("KeyError");
    if (kind == u"pct")
        return u"%1 %"_s.arg(py::round_int(value * 100));
    if (kind == u"ten")
        return py::fmt_f(value, 1);
    return py::fmt_commas(value, 0);
}

}  // namespace popcase
