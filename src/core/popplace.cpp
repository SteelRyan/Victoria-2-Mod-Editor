// popplace.py — see popplace.h.
#include "core/popplace.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace popplace {

const QStringList GROUP_FACT_PREFIXES = {u"culture = "_s, u"religion = "_s, u"is_culture_group = "_s,
                                         u"has_pop_culture = "_s, u"has_pop_religion = "_s};
const QStringList GROUP_FLAGS = {u"is_primary_culture = yes"_s, u"is_accepted_culture = yes"_s,
                                 u"is_state_religion = yes"_s, u"culture = this"_s};

Place::Place(moddata::Mod &mod_, document::Document &doc_, const std::vector<long long> &pids_, const QString &what_,
             events::World *world_)
    : mod(mod_), doc(doc_), what(what_), world(world_)
{
    for (long long p : pids_)
        if (const moddata::Province *pr = mod.provinces.get(p); pr && !pr->sea)
            pids.push_back(p);
    if (!world) {
        _own = std::make_unique<events::World>(mod, doc);
        world = _own.get();
    }
    OrderedMap<QString, long long> owners;
    for (long long p : pids) {
        const OptStr &o = doc.prov_state(p).owner;
        if (o && !o->isEmpty())
            owners[*o] += 1;
    }
    if (!owners.isEmpty()) {
        long long most = -1;
        for (const auto &[o, n] : owners)          // (max: the first of the most)
            if (n > most) {
                most = n;
                owner = o;
            }
    }
    const countryhist::NationState *st = owner ? world->st(owner) : nullptr;
    auto get = [st](const QString &k) -> OptStr {
        if (!st)
            return std::nullopt;
        const OptStr v = st->at(k);
        return v && !v->isEmpty() ? v : std::nullopt;
    };
    primary = get(u"primary_culture"_s);
    if (st)
        for (const QString &c : st->cultures)
            accepted.insert(c);
    if (primary)
        accepted.insert(*primary);
    religion = get(u"religion"_s);
    nv = get(u"nationalvalue"_s);
    const QString civ = py::lower(py::strip(get(u"civilized"_s).value_or(QString())));
    if (!civ.isEmpty())
        civilized = civ == u"yes";
    double lit[2];
    int i = 0;
    for (const QString &k : {u"literacy"_s, u"non_state_culture_literacy"_s}) {
        const double v = events::_num(st ? st->at(k) : OptStr(), 0.0);
        lit[i++] = std::max(0.0, std::min(1.0, v > 1.0 ? v / 100.0 : v));
    }
    lit_acc = lit[0];
    lit_other = lit[1];
    std::vector<std::pair<double, long long>> lr;
    for (long long p : pids) {
        long long size = 0;
        if (const auto *ps = doc.pops.get(p))
            for (const moddata::Pop &pop : *ps) {
                const StartKey key{pop.type, {pop.culture && !pop.culture->isEmpty() ? *pop.culture : u"?"_s,
                                              pop.religion && !pop.religion->isEmpty() ? *pop.religion : u"?"_s}};
                start[key] += pop.size;
                size += pop.size;
            }
        const std::optional<long long> r = doc.prov_state(p).life_rating;
        if (r)
            lr.emplace_back(double(*r), std::max(1LL, size));
    }
    for (const auto &[k, v] : start)
        total += v;
    if (!lr.empty()) {
        py::Sum num;
        long long den = 0;
        for (const auto &[r, s] : lr) {
            num.add(r * double(s));
            den += s;
        }
        life_rating = num.value() / double(den);
    }
}

Place::~Place() = default;

// ------------------------------------------------------------------ the groups
OrderedMap<Group, long long> Place::groups() const
{
    OrderedMap<Group, long long> out;
    for (const auto &[k, v] : start)
        out[k.second] += v;
    return out;
}

std::pair<QString, bool> Place::kind(const Group &g) const
{
    const QString k = primary && g.first == *primary ? u"primary"_s : accepted.contains(g.first) ? u"accepted"_s
                                                                                                 : u"other"_s;
    return {k, religion ? g.second == *religion : false};
}

std::pair<std::optional<Group>, std::optional<Group>> Place::turn(const Group &g) const
{
    const auto [k, state_rel] = kind(g);
    std::optional<Group> to_a, to_c;
    if (k == u"other" && primary && religion)
        to_a = Group{*primary, *religion};
    if (k != u"other" && !state_rel && religion)
        to_c = Group{g.first, *religion};
    return {to_a, to_c};
}

double Place::literacy() const
{
    long long acc = 0;
    for (const auto &[k, v] : start)
        if (accepted.contains(k.second.first))
            acc += v;
    const double tot = total ? double(total) : 1.0;
    return (double(acc) * lit_acc + double(total - acc) * lit_other) / tot;
}

void Place::fill_case(popcase::Case &c) const
{
    if (total) {
        c.vals.insert(u"literacy"_s, py::round_to(literacy(), 2));
        c.vals.insert(u"total_pops"_s, double(total));
    }
    if (life_rating)
        c.vals.insert(u"life_rating"_s, double(py::round_int(*life_rating)));
    if (nv)
        c.set_fact(u"nationalvalue = "_s + *nv, true);
    if (civilized)
        c.set_fact(u"civilized = yes"_s, *civilized);
}

popcase::Case Place::case_for(const popcase::Case &c, const Group &g) const
{
    const auto [k, state_rel] = kind(g);
    popcase::Case out;
    out.vals = c.vals;
    QSet<QString> facts;
    for (const QString &f : c.facts) {
        bool drop = GROUP_FLAGS.contains(f);
        for (const QString &pre : GROUP_FACT_PREFIXES)
            drop = drop || f.startsWith(pre);
        if (!drop)
            facts.insert(f);
    }
    const QString &cu = g.first, &re = g.second;
    facts << u"culture = "_s + cu << u"religion = "_s + re << u"has_pop_culture = "_s + cu
          << u"has_pop_religion = "_s + re;
    const QString grp = world->culture_group.value(cu);
    if (!grp.isEmpty())
        facts.insert(u"is_culture_group = "_s + grp);
    if (k == u"primary")
        facts << u"is_primary_culture = yes"_s << u"culture = this"_s;
    if (k != u"other")
        facts.insert(u"is_accepted_culture = yes"_s);
    if (state_rel)
        facts.insert(u"is_state_religion = yes"_s);
    out.facts = facts;
    return out;
}

QString Place::label(const std::function<QString(const QString &)> &name_of) const
{
    if (pids.empty())
        return QString();
    if (what == u"state") {
        const OptStr r = mod.provinces.get(pids[0])->region;
        const moddata::Region *reg = r && !r->isEmpty() ? mod.regions.get(*r) : nullptr;
        long long first = pids[0];
        if (reg)
            for (long long p : reg->provinces)
                if (mod.provinces.contains(p)) {
                    first = p;
                    break;
                }
        QString name;
        if (r && !r->isEmpty() && !name_of(*r).isEmpty() && QString(name_of(*r)).replace(u' ', u'_') != *r)
            name = name_of(*r);
        else
            name = mod.provinces.get(first)->name;    // (an unnamed state: its first province's name)
        const qsizetype n = qsizetype(pids.size());
        return u"the state of "_s + name + u" ("_s + QString::number(n) + u" province"_s + (n == 1 ? QString() : u"s"_s) +
               u")"_s;
    }
    const long long p = pids[0];
    return u"#"_s + QString::number(p) + u' ' + mod.provinces.get(p)->name;
}

QString Place::group_name(const Group &g, const std::function<QString(const QString &)> &name_of) const
{
    const auto [k, state_rel] = kind(g);
    const QString word = k == u"primary" ? u"primary culture"_s : k == u"accepted" ? u"accepted"_s : u"not accepted"_s;
    return name_of(g.first) + u' ' + name_of(g.second) + u" ("_s + word + (state_rel ? QString() : u", other religion"_s) +
           u")"_s;
}

}  // namespace popplace
