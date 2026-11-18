// mapmodes.py — the window-less half: the extra map modes and day 1 replayed for literacy. Line by line.
#include "core/mapmodes.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/maplook.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "core/pyre.h"
#include "core/pytext.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mapmodes {

using events::Entry;
using events::Node;
using events::Nodes;
using events::OptStr;
using events::Scope;
using events::Tri;
using events::View;

const Rgb DIM = {44, 46, 52};
const Rgb NOBODY = {58, 60, 66};
const std::vector<std::pair<QString, QString>> POP_KINDS = {
    {u"bureaucrats"_s, u"Bureaucrats"_s}, {u"clergymen"_s, u"Clergy"_s}, {u"soldiers"_s, u"Soldiers"_s}};
const QSet<QString> RANDOM = {u"random_owned"_s,    u"random_state"_s, u"random_country"_s,
                              u"random_pop"_s,      u"random_province"_s, u"random"_s,
                              u"random_list"_s,     u"random_neighbor_province"_s, u"random_neighbor_country"_s};
const QSet<QString> STATE_KEYS = {u"literacy"_s,        u"add_core"_s,        u"remove_core"_s,
                                  u"set_country_flag"_s, u"clr_country_flag"_s, u"set_global_flag"_s,
                                  u"clr_global_flag"_s,  u"country_event"_s,   u"province_event"_s};

bool tag_re(const QString &s)
{
    // re.match(r"^[A-Z][A-Z0-9]{2}$", s) by hand (2026-11-10: a regex for every effect of the day-1 replay); Python's
    // `$` also matches before a final "\n"
    const qsizetype n = s.size();
    if (!(n == 3 || (n == 4 && s[3] == u'\n')))
        return false;
    auto up = [](QChar c) { return c >= u'A' && c <= u'Z'; };
    auto dig = [](QChar c) { return c >= u'0' && c <= u'9'; };
    return up(s[0]) && (up(s[1]) || dig(s[1])) && (up(s[2]) || dig(s[2]));
}

static Tri and3(const std::vector<Tri> &vals)
{
    Tri out = true;
    for (const Tri &v : vals) {
        if (v == Tri(false))
            return false;
        if (!v)
            out = std::nullopt;
    }
    return out;
}
static Tri or3(const std::vector<Tri> &vals)
{
    Tri out = false;
    for (const Tri &v : vals) {
        if (v == Tri(true))
            return true;
        if (!v)
            out = std::nullopt;
    }
    return out;
}

std::vector<std::pair<QString, QString>> tech_kinds(const moddata::Mod &mod)
{
    // [(folder, name)] of the mod's kinds of technology (army, navy, commerce …), in the game's order
    QStringList seen;
    if (mod.nation_data)
        for (const auto &[_t, info] : mod.nation_data->techs)
            if (!info.folder.isEmpty() && !seen.contains(info.folder))
                seen << info.folder;
    std::vector<std::pair<QString, QString>> out;
    for (const QString &f : seen) {
        const QString l = mod.loc.value(f);
        out.emplace_back(f, py::strip(!l.isEmpty() ? l : QString(f).replace(u'_', u' ')));
    }
    return out;
}

std::vector<std::pair<QString, QString>> extra_modes(const moddata::Mod &mod, const QString &tab)
{
    // The map modes of this file for a rail page: [(key, text)]
    std::vector<std::pair<QString, QString>> by_country = {{u"accepted"_s, u"Accepted pops (by country)"_s}};
    for (const auto &[k, t] : POP_KINDS)
        if (mod.poptypes.contains(k))
            by_country.emplace_back(u"pt_"_s + k, u"%1 (by country)"_s.arg(t));
    std::vector<std::pair<QString, QString>> out;
    if (tab == u"pops") {
        out = {{u"literacy"_s, u"Literacy at the start"_s}};
    } else if (tab == u"nations") {
        out = {{u"plurality"_s, u"Plurality"_s}, {u"literacy"_s, u"Literacy at the start"_s},
               {u"tech_all"_s, u"Technology (all)"_s}};
        for (const auto &[f, n] : tech_kinds(mod))
            out.emplace_back(u"tech:"_s + f, u"Technology: %1"_s.arg(n));
    } else {
        return {};
    }
    out.insert(out.end(), by_country.begin(), by_country.end());
    return out;
}

bool handles(const QString &mode)
{
    return mode == u"plurality" || mode == u"literacy" || mode == u"tech_all" || mode == u"accepted" ||
           mode.startsWith(u"tech:") || mode.startsWith(u"pt_");
}

// ---------------------------------------------------------------------------------------------- day 1, replayed
Tri StartJudge::prov(const QString &k, const QString &v, std::optional<long long> pid, const Scope &self,
                     std::optional<long long> year)
{
    if (k == u"is_core") {
        const OptStr t = _tag(v, self);
        if (!t)
            return std::nullopt;
        const auto *c = pid ? sim.cores.get(*pid) : nullptr;
        return c && c->contains(*t);
    }
    return events::Judge::prov(k, v, pid, self, year);
}

Tri StartJudge::country(const QString &k, const QString &v, const OptStr &tag, const Scope &self,
                        std::optional<long long> year)
{
    if (k == u"has_country_flag")
        return sim.cflags.value(tag).contains(v);
    if (k == u"has_global_flag")
        return sim.gflags.contains(v);
    if (k == u"is_core" && tag) {
        const auto p = events::_num(OptStr(v));
        if (!p)
            return std::nullopt;
        const auto *c = sim.cores.get(py::trunc_int(*p));
        return c && c->contains(*tag);
    }
    return events::Judge::country(k, v, tag, self, year);
}

Tri StartJudge::scope(const Node &c, const Scope &sc, const Scope &self, std::optional<long long> year)
{
    if ((c.key == u"any_core" || c.key == u"all_core") && sc.kind == u'c') {
        std::vector<Tri> vals;
        for (long long p : sim.cores_of(sc.tag))
            vals.push_back(all(c.kids, Scope::p(p), self, year));
        return c.key == u"all_core" ? and3(vals) : or3(vals);
    }
    if (c.key == u"any_neighbor_province" && sc.kind == u'p') {
        Tri out = false;                             // or3 over a generator: stops at the first True
        if (sc.pid)
            for (long long q : sim.neighbours(*sc.pid)) {
                const Tri v = all(c.kids, Scope::p(q), self, year);
                if (v == Tri(true))
                    return true;
                if (!v)
                    out = std::nullopt;
            }
        return out;
    }
    return events::Judge::scope(c, sc, self, year);
}

StartSim::StartSim(moddata::Mod &mod, document::Document &doc, events::EventDB *db, Neighbours md,
                   std::function<QString(const QString &)> text_of)
    : mod(mod), doc(doc), db(db), md(std::move(md)), text_of(std::move(text_of))
{
    w = std::make_unique<events::World>(mod, doc);
    year = w->start_year;
    if (db)
        judge = std::make_unique<StartJudge>(*w, *db, *this);
    for (const auto &[pid, pr] : mod.provinces)
        if (!pr.sea) {
            const QStringList &c = doc.prov_state(pid).cores;
            cores.insert(pid, QSet<QString>(c.begin(), c.end()));
        }
    QList<long long> pids;
    for (const auto &[pid, _c] : cores)
        pids << pid;
    for (long long pid : pids) {
        const OptStr tag = w->owner.contains(pid) ? OptStr(w->owner.value(pid)) : std::nullopt;
        const auto [a, b] = _file_values(tag);
        const QSet<OptStr> acc = accepted(tag);
        base.insert(pid, {a, b});
        std::vector<double> l;
        for (const moddata::Pop &p : doc.province_pops(pid))
            l.push_back(acc.contains(p.culture) ? a : b);
        lit.insert(pid, l);
    }
    _run_day1();
}

std::pair<double, double> StartSim::_file_values(const OptStr &tag)
{
    const countryhist::NationState *st = (tag && !tag->isEmpty()) ? w->st(tag) : nullptr;
    if (!st)
        return {0.0, 0.0};
    double out[2];
    int i = 0;
    for (const QString &k : {u"literacy"_s, u"non_state_culture_literacy"_s}) {
        double v = events::_num(st->at(k), 0.0);
        if (v == 0.0)
            v = 0.0;                                 // (`or 0.0`: -0.0 too)
        out[i++] = std::max(0.0, std::min(1.0, v > 1.0 ? v / 100.0 : v));
    }
    return {out[0], out[1]};
}

const QSet<OptStr> &StartSim::accepted(const OptStr &tag)
{
    auto it = _culture.find(tag);
    if (it == _culture.end()) {
        const countryhist::NationState *st = (tag && !tag->isEmpty()) ? w->st(tag) : nullptr;
        QSet<OptStr> got;
        if (st) {
            got.insert(st->at(u"primary_culture"_s));
            for (const QString &c : st->cultures)
                got.insert(c);
        }
        it = _culture.insert(tag, got);
    }
    return *it;
}

std::vector<long long> StartSim::neighbours(long long pid)
{
    std::vector<long long> out;
    if (md)
        for (long long q : md(pid))
            if (cores.contains(q))
                out.push_back(q);
    return out;
}

std::vector<long long> StartSim::cores_of(const OptStr &tag)
{
    std::vector<long long> out;
    for (const auto &[p, c] : cores)
        if (tag && c.contains(*tag))
            out.push_back(p);
    return out;
}

// --- which scripts run on day 1: the ones the mod marks, nothing else
void StartSim::_run_day1()
{
    if (!db || !text_of)
        return;
    const auto marked = events::lit_marked(db->files, text_of);
    std::vector<Entry *> ents;
    for (const auto &e : db->entries)
        if (e->rel && e->uid && marked.contains({*e->rel, *e->uid}))
            ents.push_back(e.get());
    if (!ents.empty()) {
        how = u"marked"_s;
        for (Entry *e : ents)
            _marked(e, 0);
    }
}

static QString who_text(const QStringList &who, const QString &none)
{
    const QString s = who.mid(0, 3).join(u", "_s) + (who.size() > 3 ? u" …"_s : QString());
    return s.isEmpty() ? none : s;
}

void StartSim::_marked(Entry *e, long long depth)
{
    // An entry that carries the line, replayed for everyone it runs for on day 1.
    if (_ran.contains(e))
        return;
    _ran.insert(e);
    if (!e->is_event()) {
        QStringList who;
        for (const QString &t : w->tags)
            if (judge->all(e->potential, Scope::c(t), Scope::c(t), year) != Tri(false) &&
                judge->all(e->allow, Scope::c(t), Scope::c(t), year) != Tri(false))
                who << t;
        std::vector<std::function<void()>> runs;
        for (const QString &tag : who)
            runs.push_back([this, tag, e] { run(e->effect, Scope::c(tag), Scope::c(tag), 0); });
        _root(u"decision %1 (%2)"_s.arg(e->key, who_text(who, u"nobody can take it"_s)), runs);
    } else if (e->triggered_only) {
        std::vector<Entry *> srcs;
        for (const events::Fired &f : db->fired_by.value(e->id.value_or(events::NO_ID)))
            if (f.src)
                srcs.push_back(f.src);
        if (!srcs.empty() && depth < 3) {            // what fires it is replayed: it gets there with the right nation
            for (Entry *s : srcs)
                _marked(s, depth + 1);
        } else {
            std::vector<std::function<void()>> runs;
            if (!w->tags.isEmpty())
                runs.push_back([this, e] { _event(e, Scope::c(w->tags[0]), 0); });
            _root(u"event %1 (nothing fires it on day 1: replayed once)"_s.arg(e->id.value_or(u"None"_s)), runs);
        }
    } else if (e->province()) {
        std::vector<long long> who;
        for (const auto &[p, _c] : cores)
            if (judge->all(e->trigger, Scope::p(p), Scope::p(p), year) != Tri(false))
                who.push_back(p);
        std::vector<std::function<void()>> runs;
        for (long long p : who)
            runs.push_back([this, p, e] { _event(e, Scope::p(p), 0); });
        _root(u"province event %1 (%2 provinces)"_s.arg(e->id.value_or(u"None"_s), QString::number(who.size())), runs);
    } else {
        QStringList who;
        for (const QString &t : w->tags)
            if (judge->all(e->trigger, Scope::c(t), Scope::c(t), year) != Tri(false))
                who << t;
        std::vector<std::function<void()>> runs;
        for (const QString &tag : who)
            runs.push_back([this, tag, e] { _event(e, Scope::c(tag), 0); });
        _root(u"event %1 (%2)"_s.arg(e->id.value_or(u"None"_s), who_text(who, u"its trigger holds for nobody"_s)), runs);
    }
}

void StartSim::_root(const QString &label, const std::vector<std::function<void()>> &runs)
{
    // One marked script, replayed (and named among the sources).
    sources << label;
    for (const auto &fn : runs)
        fn();
}

bool StartSim::_relevant(const Nodes &nodes)
{
    // Is there anything in here that the replay follows?
    auto it = _rel.find(&nodes);
    if (it != _rel.end())
        return *it;
    bool yes = false;
    for (const Node &n : nodes)
        if ((n.has_key && STATE_KEYS.contains(n.key)) || (n.is_block && _relevant(n.kids))) {
            yes = true;
            break;
        }
    _rel.insert(&nodes, yes);
    return yes;
}

static QString scope_key(const OptStr &id, const Scope &sc)
{
    return (id ? u"1"_s + *id : u"0"_s) + u'\x1f' + sc.kind + u'\x1f' +
           (sc.kind == u'c' ? (sc.tag ? u"1"_s + *sc.tag : u"0"_s) : (sc.pid ? u"1"_s + QString::number(*sc.pid) : u"0"_s));
}

void StartSim::_event(Entry *e, const Scope &sc, long long depth)
{
    const QString key = scope_key(e->id, sc);
    if (depth > 8 || _fired.contains(key))
        return;
    _fired.insert(key);
    if (e->immediate && !e->immediate->empty())
        run(*e->immediate, sc, sc, depth);
    if (!e->options.empty())
        run(e->options[0].effects, sc, sc, depth);
}

void StartSim::_fire(const Node &n, Scope sc, long long depth)
{
    OptStr i;
    double days;
    if (n.is_block) {
        i = events::_val(n.kids, u"id"_s);
        days = events::_num(events::_val(n.kids, u"days"_s), 0.0);
        if (days == 0.0)
            days = 0.0;
    } else {
        i = n.value;
        days = 0.0;
    }
    Entry *e = (i && !i->isEmpty()) ? db->by_id.value(py::strip(*i), nullptr) : nullptr;
    if (!e || days > 1)
        return;
    if (sc.kind == u'p' && !e->province()) {
        const QString o = sc.pid ? w->owner.value(*sc.pid) : QString();
        if (!o.isEmpty())
            sc = Scope::c(o);
    }
    sources << u"→ event %1"_s.arg(e->id.value_or(u"None"_s));
    _event(e, sc, depth + 1);
}

// --- effects
OptStr StartSim::_tag(const QString &v, const Scope &root)
{
    if (v == u"THIS" || v == u"ROOT") {
        if (root.kind == u'c')
            return root.tag;
        if (root.pid && w->owner.contains(*root.pid))
            return w->owner.value(*root.pid);
        return std::nullopt;
    }
    return tag_re(v) ? OptStr(v) : std::nullopt;
}

bool StartSim::_passes(const std::vector<const Nodes *> &limits, const Scope &sc, const Scope &root)
{
    for (const Nodes *l : limits)
        if (judge->all(*l, sc, root, year) != Tri(true))
            return false;
    return true;
}

void StartSim::run(View nodes, const Scope &sc, const Scope &root, long long depth)
{
    auto owner_of = [&](long long p) -> OptStr { return w->owner.contains(p) ? OptStr(w->owner.value(p)) : std::nullopt; };
    for (size_t ix = 0; ix < nodes.size(); ++ix) {
        const Node &n = nodes[ix];
        if (!n.has_key || n.key == u"limit")
            continue;
        const QString &k = n.key;
        if (k == u"country_event" || k == u"province_event") {
            _fire(n, sc, depth);
            continue;
        }
        if (!n.is_block) {
            const QString &v = n.value;
            if (k == u"add_core" && sc.kind == u'p') {
                const OptStr t = _tag(v, root);
                if (t && !t->isEmpty()) {
                    if (!sc.pid)
                    continue;
                QSet<QString> &c = cores[*sc.pid];
                    if (!c.contains(*t)) {
                        c.insert(*t);
                        ++core_changes;
                    }
                }
            } else if (k == u"remove_core" && sc.kind == u'p') {
                const OptStr t = _tag(v, root);
                QSet<QString> *c = sc.pid ? cores.get(*sc.pid) : nullptr;
                if (t && c && c->contains(*t)) {
                    c->remove(*t);
                    ++core_changes;
                }
            } else if (k == u"set_country_flag" && sc.kind == u'c') {
                cflags[sc.tag].insert(v);
            } else if (k == u"clr_country_flag" && sc.kind == u'c') {
                cflags[sc.tag].remove(v);
            } else if (k == u"set_global_flag") {
                gflags.insert(v);
            } else if (k == u"clr_global_flag") {
                gflags.remove(v);
            }
            continue;
        }
        if (!_relevant(n.kids))
            continue;
        const Nodes &body = n.kids;
        std::vector<const Nodes *> limits;
        for (const Node &c : body)
            if (c.has_key && c.key == u"limit" && c.is_block)
                limits.push_back(&c.kids);
        if (k == u"any_pop") {
            _pops(body, limits, sc, root);
        } else if (k == u"THIS" || k == u"ROOT") {
            run(body, root, root, depth);
        } else if (tag_re(k) && mod.countries.contains(k)) {
            run(body, Scope::c(k), root, depth);
        } else if (py::isdigit(k)) {
            const long long p = py::to_int(k);
            if (cores.contains(p))
                run(body, Scope::p(p), root, depth);
        } else if (const moddata::Region *rg = mod.any_region(k)) {
            std::vector<long long> pids;
            for (long long p : rg->provinces)
                if (cores.contains(p))
                    pids.push_back(p);
            if (sc.kind == u'c' && std::any_of(pids.begin(), pids.end(), [&](long long p) { return owner_of(p) == sc.tag; })) {
                std::vector<long long> mine;
                for (long long p : pids)
                    if (owner_of(p) == sc.tag)
                        mine.push_back(p);
                pids = mine;
            }
            for (long long p : pids)
                run(body, Scope::p(p), root, depth);
        } else if (k == u"any_country" || k == u"all_country") {
            for (const QString &t : QStringList(w->tags))
                if (_passes(limits, Scope::c(t), root))
                    run(body, Scope::c(t), root, depth);
        } else if ((k == u"all_core" || k == u"any_core") && sc.kind == u'c') {
            for (long long p : cores_of(sc.tag))
                if (_passes(limits, Scope::p(p), root))
                    run(body, Scope::p(p), root, depth);
        } else if ((k == u"any_owned" || k == u"any_owned_province" || k == u"any_state") && sc.kind == u'c') {
            const QList<long long> owned = sc.tag ? w->owned.value(*sc.tag) : QList<long long>();
            for (long long p : owned)
                if (_passes(limits, Scope::p(p), root))
                    run(body, Scope::p(p), root, depth);
        } else if (k == u"any_neighbor_province" && sc.kind == u'p') {
            for (long long q : neighbours(*sc.pid))
                if (_passes(limits, Scope::p(q), root))
                    run(body, Scope::p(q), root, depth);
        } else if ((k == u"owner" || k == u"controller") && sc.kind == u'p') {
            const OptStr o = sc.pid ? owner_of(*sc.pid) : std::nullopt;
            if (o && !o->isEmpty() && _passes(limits, Scope::c(o), root))
                run(body, Scope::c(o), root, depth);
        } else if (k == u"capital_scope" && sc.kind == u'c') {
            const auto cap = judge->_capital(sc.tag);
            if (cap && cores.contains(*cap))
                run(body, Scope::p(*cap), root, depth);
        } else if (k == u"state_scope" && sc.kind == u'p') {
            const moddata::Province *pr = sc.pid ? mod.provinces.get(*sc.pid) : nullptr;
            const moddata::Region *r = (pr && pr->region && !pr->region->isEmpty()) ? mod.regions.get(*pr->region) : nullptr;
            const std::vector<long long> ps = r ? r->provinces : std::vector<long long>{*sc.pid};
            for (long long p : ps)
                if (cores.contains(p) && owner_of(p) == owner_of(*sc.pid))
                    run(body, Scope::p(p), root, depth);
        } else if (RANDOM.contains(k)) {
            notes[u"random"_s] += 1;
        } else if (k == u"FROM" || k == u"any_neighbor_country" || k == u"any_greater_power" || k == u"any_sphere_member" ||
                   k == u"overlord" || k == u"sphere_owner" || k == u"cultural_union" || k == u"war_countries") {
            notes[u"scope"_s] += 1;
        } else if (py::re_match(u"^[A-Za-z]+_\\d+$"_s, k).hasMatch() || (tag_re(k) && !mod.countries.contains(k))) {
            bad_scopes[k] += 1;                      // looks like a state / a tag, and there is none of that name
        }
    }
}

void StartSim::_pops(const Nodes &body, const std::vector<const Nodes *> &limits, const Scope &sc, const Scope &root)
{
    std::optional<double> val;
    bool found = false;
    for (const Node &c : body)
        if (c.has_key && c.key == u"literacy" && !c.is_block) {
            val = events::_num(OptStr(c.value));
            found = true;
            break;
        }
    if (!found || !val)
        return;
    const double v = std::max(0.0, std::min(1.0, *val));
    ++set_lines;
    std::vector<long long> pids;
    if (sc.kind == u'p') {
        if (sc.pid)
            pids.push_back(*sc.pid);
    } else if (sc.tag) {
        for (long long p : w->owned.value(*sc.tag))
            pids.push_back(p);
    }
    for (long long pid : pids) {
        std::vector<double> *lst = lit.get(pid);
        if (!lst || lst->empty())
            continue;
        if (limits.empty()) {
            *lst = std::vector<double>(lst->size(), v);
            changed.insert(pid);
            continue;
        }
        const std::vector<moddata::Pop> &pops = doc.province_pops(pid);
        for (size_t i = 0; i < pops.size(); ++i) {
            if (i >= lst->size())
                continue;
            bool ok = true;
            for (const Nodes *l : limits)
                if (_pop_ok(*l, pid, pops[i], (*lst)[i], root) != Tri(true)) {
                    ok = false;
                    break;
                }
            if (ok) {
                (*lst)[i] = v;
                changed.insert(pid);
            }
        }
    }
}

Tri StartSim::_pop_ok(const Nodes &nodes, long long pid, const moddata::Pop &pop, double lit_, const Scope &root)
{
    Tri out = true;                                  // and3 over a generator: stops at the first False
    for (const Node &c : nodes) {
        const Tri v = _pop_one(c, pid, pop, lit_, root);
        if (v == Tri(false))
            return false;
        if (!v)
            out = std::nullopt;
    }
    return out;
}

Tri StartSim::_pop_one(const Node &c, long long pid, const moddata::Pop &pop, double lit_, const Scope &root)
{
    if (!c.has_key)
        return true;
    const QString &k = c.key;
    const QString ku = py::upper(k);
    auto or3_kids = [&]() -> Tri {
        Tri out = false;
        for (const Node &x : c.kids) {
            const Tri v = _pop_one(x, pid, pop, lit_, root);
            if (v == Tri(true))
                return true;
            if (!v)
                out = std::nullopt;
        }
        return out;
    };
    if (ku == u"AND")
        return c.is_block ? _pop_ok(c.kids, pid, pop, lit_, root) : Tri(true);
    if (ku == u"OR")
        return c.is_block ? or3_kids() : Tri(true);
    if (ku == u"NOT")
        return c.is_block ? events::not3(or3_kids()) : Tri(true);
    const OptStr owner = w->owner.contains(pid) ? OptStr(w->owner.value(pid)) : std::nullopt;
    if (c.is_block) {
        if (k == u"location")
            return judge->all(c.kids, Scope::p(pid), root, year);
        if (k == u"country" || k == u"owner")
            return (owner && !owner->isEmpty()) ? judge->all(c.kids, Scope::c(owner), root, year) : Tri(false);
        notes[u"condition"_s] += 1;
        return std::nullopt;
    }
    const QString &v = c.value;
    const countryhist::NationState *st = (owner && !owner->isEmpty()) ? w->st(owner) : nullptr;
    if (k == u"type" || k == u"pop_type" || k == u"has_pop_type" || k == u"is_pop_type")
        return pop.type == v;
    if (k == u"culture" || k == u"has_pop_culture")
        return pop.culture == OptStr(v);
    if (k == u"religion" || k == u"has_pop_religion")
        return pop.religion == OptStr(v);
    if (k == u"is_primary_culture")
        return !st ? Tri() : Tri((pop.culture == st->at(u"primary_culture"_s)) == events::yes(OptStr(v)));
    if (k == u"is_accepted_culture")
        return !st ? Tri() : Tri((pop.culture && st->cultures.contains(*pop.culture)) == events::yes(OptStr(v)));
    if (k == u"is_culture_group")
        return pop.culture && w->culture_group.contains(*pop.culture) && w->culture_group.value(*pop.culture) == v;
    if (k == u"is_state_religion")
        return !st ? Tri() : Tri((pop.religion == st->at(u"religion"_s)) == events::yes(OptStr(v)));
    if (k == u"strata")
        return std::nullopt;                         // (mod.poptypes hold no strata: Python's info.get("strata") is None)
    if (k == u"literacy") {
        const auto x = events::_num(OptStr(v));
        return !x ? Tri() : Tri(lit_ >= *x);
    }
    const Tri r = judge->prov(k, v, pid, root, year);
    if (!r)
        notes[u"condition"_s] += 1;
    return r;
}

// --- read-outs
std::optional<double> StartSim::province(long long pid)
{
    // Mean literacy of a province's people (None: nobody lives there).
    const std::vector<double> *lst = lit.get(pid);
    const std::vector<moddata::Pop> &pops = doc.province_pops(pid);
    long long tot = 0;
    for (const moddata::Pop &p : pops)
        tot += p.size;
    if (!lst || lst->empty() || tot <= 0)
        return std::nullopt;
    py::Sum s;
    for (size_t i = 0; i < lst->size() && i < pops.size(); ++i)
        s.add((*lst)[i] * static_cast<double>(pops[i].size));
    return s.value() / static_cast<double>(tot);
}

QString StartSim::caption()
{
    if (how != u"marked")
        return u"Literacy at the start, as the country files say. No event or decision is assigned as a literacy event — "
               u"if one sets literacy on day 1, tick “Literacy event” on it in Scripts (that writes the line %1 above "
               u"it) and it is counted in."_s.arg(events::LIT_LINE);
    long long n_src = 0;
    for (const QString &s : sources)
        if (!s.startsWith(u"→"))
            ++n_src;
    if (!n_src)
        n_src = 1;
    const QString pl = n_src == 1 ? QString() : u"s"_s;
    QString out = u"Literacy at the start: the country files, then the %1 event%2 / decision%3 assigned with “%4” — %5 "
                  u"(it sets %6 province%7)."_s.arg(QString::number(n_src), pl, pl, events::LIT_LINE,
                                                    sources.mid(0, 4).join(u' ') + (sources.size() > 4 ? u" …"_s : QString()),
                                                    QString::number(changed.size()), changed.size() == 1 ? QString() : u"s"_s);
    long long nb = 0;
    for (const auto &[_k, c] : bad_scopes)
        nb += c;
    if (nb) {
        QStringList ks;
        for (const auto &[k2, _c] : bad_scopes)
            ks << k2;
        std::sort(ks.begin(), ks.end());
        out += u" ⚠ %1 block%2 name%3 a state that isn't in region.txt (%4): the game skips %5."_s.arg(
            QString::number(nb), nb == 1 ? QString() : u"s"_s, nb == 1 ? u"s"_s : QString(), ks.mid(0, 6).join(u", "_s),
            nb == 1 ? u"it"_s : u"them"_s);
    }
    const long long rest = notes.value(u"random"_s) + notes.value(u"scope"_s) + notes.value(u"condition"_s);
    if (rest)
        out += u" Not followed: %1 random / unknown part%2."_s.arg(QString::number(rest), rest == 1 ? QString() : u"s"_s);
    return out;
}

// ---------------------------------------------------------------------------------------------- the modes
MoreModes::MoreModes(moddata::Mod &mod_, long long n_, Neighbours nb_, std::function<events::EventDB *(bool)> db_,
                     Ramp ramp_)
    : mod(mod_), n(n_), nb(std::move(nb_)), db(std::move(db_)), ramp(std::move(ramp_))
{
}

// --- data
QString MoreModes::_sig()
{
    // (id(doc), undo / redo lengths, pending, (rel, revision) of every open text, id(event_panel.db))
    auto *store = doc->maps.contains(u"scripts"_s)
                      ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    QStringList texts;
    if (store) {
        QStringList rels;
        for (const auto &[r, _d] : store->docs)
            rels << r;
        std::sort(rels.begin(), rels.end());
        for (const QString &r : rels)
            texts << r + u'\x01' + QString::number((*store->docs.get(r))->revision());
    }
    return u"%1|%2|%3|%4|%5"_s.arg(QString::number(reinterpret_cast<quintptr>(doc)),
                                   QString::number(doc->undo_stack.size()), QString::number(doc->redo_stack.size()),
                                   QString::number(doc->pending_count()), texts.join(u'\x02'));
}

bool MoreModes::has_line()
{
    if (!doc->maps.contains(u"scripts"_s))
        return false;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString sig = _sig();
    if (!_line || _line->first != sig) {
        QSet<QString> rels;
        for (const auto &[r, _d] : store.docs)
            if (r.startsWith(u"events/"_s) || r.startsWith(u"decisions/"_s))
                rels.insert(r);
        for (const QString &folder : {u"events"_s, u"decisions"_s}) {
            const QString d = pypath::join(mod.root, folder);
            if (fs::isdir(d))
                for (const QString &f : fs::listdir(d))
                    if (py::lower(f).endsWith(u".txt"_s))
                        rels.insert(folder + u'/' + f);
        }
        QStringList order(rels.begin(), rels.end());
        std::sort(order.begin(), order.end());
        bool found = false;
        for (const QString &r : order) {
            try {
                if (py::re_search(u"(?m)^[ \\t]*#[ \\t]*literacy[ \\t_\\-]*(?:event|script|decision)\\b"_s,
                                  store.text(r), 0, true).hasMatch()) {
                    found = true;
                    break;
                }
            } catch (const std::exception &) {
            }
        }
        _line = {sig, found};
    }
    return _line->second;
}

StartSim &MoreModes::sim()
{
    const bool line = has_line();
    events::EventDB *d = line ? db(true) : nullptr;   // an assigned literacy event: the scripts are read (once)
    const QString sig = _sig() + u'|' + QString::number(reinterpret_cast<quintptr>(db(false)));
    if (!_sim || _sim->first != sig) {
        auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
        std::unique_ptr<StartSim> s =
            line ? std::make_unique<StartSim>(mod, *doc, d, nb, [&store](const QString &rel) { return store.text(rel); })
                 : std::make_unique<StartSim>(mod, *doc, nullptr, nb);   // (none: the country files' values)
        _sim.emplace(sig, std::move(s));
    }
    return *_sim->second;
}

OrderedMap<QString, std::vector<long long>> MoreModes::_owners(const std::vector<long long> &land)
{
    OrderedMap<QString, std::vector<long long>> by;
    for (long long pid : land)
        if (const OptStr &o = doc->prov_state(pid).owner; o && !o->isEmpty())
            by[*o].push_back(pid);
    return by;
}

std::optional<countryhist::NationState> MoreModes::_nation(const QString &tag)
{
    try {
        return doc->map<countryhist::NationEdits>(u"nations"_s).state(tag);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

OrderedMap<QString, Rgb> MoreModes::_rank(const OrderedMap<QString, std::optional<double>> &values,
                                          const std::optional<QString> &mode)
{
    // {key: value} -> {key: colour}: the most the brightest, the least the dimmest (equal values: one colour) — or,
    // when the mode has ranges (◐), the colour of the range its value lies in
    std::vector<double> pos;
    for (const auto &[_k, v] : values)
        if (v && *v > 0)
            pos.push_back(*v);
    std::sort(pos.begin(), pos.end());
    pos.erase(std::unique(pos.begin(), pos.end()), pos.end());
    const bool rng = mode && maplook::uses_ranges(*mode);
    OrderedMap<QString, Rgb> out;
    for (const auto &[k, v] : values) {
        if (!v)
            out.insert(k, DIM);
        else if (rng)
            out.insert(k, maplook::colour(*mode, v, 0));
        else if (*v > 0) {
            const auto i = std::lower_bound(pos.begin(), pos.end(), *v) - pos.begin();
            const double t = 0.15 + 0.85 * double(i + 1) / double(pos.size());
            out.insert(k, mode ? maplook::colour(*mode, v, t) : ramp(t));
        } else
            out.insert(k, DIM);
    }
    return out;
}

std::vector<double> MoreModes::values(const QString &mode, const std::vector<long long> &land)
{
    const Facts &f = facts(mode, land);
    std::vector<double> out;
    for (const auto &[_k, x] : f.pid)
        if (x.v)
            out.push_back(mode == u"literacy"_s ? *x.v * 100.0 : *x.v);
    for (const auto &[_k, x] : f.tag)
        if (x.v)
            out.push_back(*x.v);
    return out;
}

const MoreModes::Facts &MoreModes::facts(const QString &mode, const std::vector<long long> &land)
{
    const QString sig = _sig() + u'|' + QString::number(reinterpret_cast<quintptr>(db(false))) + u'|' + mode;
    if (_facts && _facts->first == sig)
        return _facts->second;
    Facts out;
    const auto by = _owners(land);
    if (mode == u"literacy"_s) {
        StartSim &s = sim();
        for (long long pid : land) {
            const auto v = s.province(pid);
            if (v) {
                const auto *b = s.base.get(pid);
                const double a = b ? b->first : 0.0;
                out.pid.insert(pid, {v, u"literacy %1 %%2"_s.arg(
                                            py::fmt_f(*v * 100, 0),
                                            s.changed.contains(pid)
                                                ? u" (set on day 1; country file: %1 %)"_s.arg(py::fmt_f(a * 100, 0))
                                                : u" (country file)"_s)});
            }
        }
    } else if (mode == u"plurality"_s) {
        for (const auto &[tag, _p] : by) {
            const auto st = _nation(tag);
            const auto v = st ? events::_num(st->at(u"plurality"_s)) : std::nullopt;
            if (v)
                out.tag.insert(tag, {v, u"plurality %1 %"_s.arg(py::fmt_g(*v))});
        }
    } else if (mode == u"tech_all"_s || mode.startsWith(u"tech:"_s)) {
        static const OrderedMap<QString, countryhist::TechInfo> none;
        const auto &techs = mod.nation_data ? mod.nation_data->techs : none;
        const OptStr kind = mode.startsWith(u"tech:"_s) ? OptStr(mode.mid(5)) : std::nullopt;
        long long every = 0;
        for (const auto &[_t, inf] : techs)
            if (!kind || inf.folder == *kind)
                ++every;
        for (const auto &[tag, _p] : by) {
            const auto st = _nation(tag);
            long long have = 0;
            if (st)
                for (const QString &t : st->techs)
                    if (const auto *inf = techs.get(t); inf && (!kind || inf->folder == *kind))
                        ++have;
            out.tag.insert(tag, {static_cast<double>(have),
                                 u"%1 of %2 %3technologies"_s.arg(QString::number(have), QString::number(every),
                                                                  kind ? QString(*kind).replace(u'_', u' ') + u' '
                                                                       : QString())});
        }
    } else {
        const OptStr kind = mode.startsWith(u"pt_"_s) ? OptStr(mode.mid(3)) : std::nullopt;
        for (const auto &[tag, pids] : by) {
            const auto st = _nation(tag);
            QSet<OptStr> acc;
            if (st) {
                acc.insert(st->at(u"primary_culture"_s));
                for (const QString &c : st->cultures)
                    acc.insert(c);
            }
            long long tot = 0, k = 0;
            for (long long pid : pids)
                if (const auto *pops = doc->pops.get(pid))
                    for (const auto &p : *pops) {
                        tot += p.size;
                        if (kind ? p.type == *kind : acc.contains(p.culture))
                            k += p.size;
                    }
            if (tot > 0) {
                const double share = 100.0 * double(k) / double(tot);
                out.tag.insert(tag, {share, u"%1 % %2 (%3 of %4)"_s.arg(
                                                share < 10 ? py::fmt_f(share, 1) : py::fmt_f(share, 0),
                                                kind ? *kind : u"accepted"_s, py::fmt_commas(double(k), 0),
                                                py::fmt_commas(double(tot), 0))});
            }
        }
    }
    _facts.emplace(sig, std::move(out));
    return _facts->second;
}

// --- the map
void MoreModes::table(const QString &mode, std::vector<Rgb> &t, const std::vector<long long> &land)
{
    // the colour table of a mode (t: the table to fill, one row per province number)
    const Facts &f = facts(mode, land);
    for (long long pid : land)
        t[size_t(pid)] = NOBODY;
    if (mode == u"literacy"_s) {
        double top = 0.0;
        for (const auto &[_p, x] : f.pid)
            top = std::max(top, *x.v);
        if (!top)
            top = 1.0;                                // (the most literate province is the brightest)
        for (long long pid : land) {
            if (const Fact *x = f.pid.get(pid))
                t[size_t(pid)] = maplook::colour(mode, *x->v * 100.0, 0.06 + 0.94 * std::max(0.0, std::min(1.0, *x->v / top)));
            else if (const OptStr &o = doc->prov_state(pid).owner; o && !o->isEmpty())
                t[size_t(pid)] = DIM;
        }
        return;
    }
    OrderedMap<QString, Rgb> col;
    if (mode == u"plurality"_s) {
        for (const auto &[tag, x] : f.tag)
            col.insert(tag, maplook::colour(mode, x.v, 0.08 + 0.92 * std::max(0.0, std::min(1.0, *x.v / 100.0))));
    } else if (mode == u"tech_all"_s || mode.startsWith(u"tech:"_s)) {
        double top = 0;
        for (const auto &[_t, x] : f.tag)
            top = std::max(top, *x.v);
        for (const auto &[tag, x] : f.tag)
            col.insert(tag, (*x.v && top) || maplook::uses_ranges(mode)
                                ? maplook::colour(mode, x.v, 0.12 + 0.88 * *x.v / top) : DIM);
    } else {
        OrderedMap<QString, std::optional<double>> vals;
        for (const auto &[tag, x] : f.tag)
            vals.insert(tag, x.v);
        col = _rank(vals, mode);
    }
    for (long long pid : land)
        if (const OptStr &o = doc->prov_state(pid).owner; o && !o->isEmpty()) {
            const Rgb *c = col.get(*o);
            t[size_t(pid)] = c ? *c : DIM;
        }
}

std::vector<long long> MoreModes::_land() const
{
    std::vector<long long> land;
    for (const auto &[p, pr] : mod.provinces)
        if (!pr.sea && p < n)
            land.push_back(p);
    return land;
}

QString MoreModes::describe(const QString &mode, long long pid)
{
    // what the map says about a province, for the line under the map ("" for nothing)
    const Facts &f = facts(mode, _land());
    if (mode == u"literacy"_s) {
        const Fact *x = f.pid.get(pid);
        return x ? x->text : QString();
    }
    const OptStr &o = doc->prov_state(pid).owner;
    if (!o || o->isEmpty() || !f.tag.contains(*o))
        return QString();
    const auto *c = mod.countries.get(*o);
    return u"%1: %2"_s.arg(c ? c->name : *o, f.tag.get(*o)->text);
}

QString MoreModes::caption(const QString &mode)
{
    // one line about the mode, shown when it is chosen
    if (mode == u"literacy"_s) {
        const Facts &f = facts(mode, _land());
        double top = 0.0;
        for (const auto &[_p, x] : f.pid)
            top = std::max(top, *x.v);
        return u"%1 The brightest province has %2 %."_s.arg(sim().caption(), py::fmt_f(top * 100, 0));
    }
    if (mode == u"plurality"_s)
        return u"Plurality at the start, from each nation's history file. Brighter = higher (0 to 100 %)."_s;
    if (mode == u"tech_all"_s || mode.startsWith(u"tech:"_s)) {
        const Facts &f = facts(mode, _land());
        double top = 0;
        for (const auto &[_t, x] : f.tag)
            top = std::max(top, *x.v);
        const QString kind = mode.startsWith(u"tech:"_s) ? mode.mid(5).replace(u'_', u' ') + u' ' : QString();
        return u"How many %1technologies each nation starts with. Brighter = more; the brightest has %2."_s.arg(
            kind, QString::number(static_cast<long long>(top)));
    }
    if (mode == u"accepted"_s)
        return u"The share of each nation's people that are of its primary or an accepted culture — the nation with "
               u"the highest share is the brightest, the lowest the dimmest."_s;
    if (mode.startsWith(u"pt_"_s))
        return u"The share of %1 among each nation's people — the highest share the brightest, the lowest the dimmest. "
               u"(For one province: Population → Filtered pops.)"_s.arg(mode.mid(3));
    return QString();
}

}  // namespace mapmodes
