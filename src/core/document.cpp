// document.py — In-memory scenario being edited: pops per province, state capitals, undo/redo and pop operations.
// Line by line. Where Python adds a pop to a list it is walking through, the C++ goes by index (a vector moves its
// items when it grows; a Python list of dicts does not).
#include "core/document.h"
#include "core/adjacency.h"
#include "core/positions.h"
#include "core/mapfiles.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/store_snaps.h"
#include "core/localisation.h"
#include "core/scriptstore.h"
#include "grid/pyset.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace document {

using history::Factory;
using history::ProvinceHistory;
using moddata::Region;
using store::Items;
using store::Key;

const int UNDO_STEPS = 1000;
const double LIVE_MERGE = 1.5;

static double monotonic()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
static QString plural(long long n) { return n == 1 ? QString() : u"s"_s; }

std::vector<long long> distribute(long long total, const std::vector<double> &weights0)
{
    // Split an integer total by weights. Exact sum, largest remainder, ties go to earlier entries.
    const size_t n = weights0.size();
    if (n == 0)
        return {};
    std::vector<double> weights = weights0;
    py::Sum ws;                                   // (Python's sum: compensated — sum([0.1, 0.2, 0.3]) == 0.6)
    for (double w : weights)
        ws.add(std::max(0.0, w));
    double wsum = ws.value();
    if (wsum <= 0) {
        weights.assign(n, 1.0);
        wsum = double(n);
    }
    std::vector<double> raw(n);
    std::vector<long long> out(n);
    long long sum = 0;
    for (size_t i = 0; i < n; ++i) {
        raw[i] = double(total) * std::max(0.0, weights[i]) / wsum;
        out[i] = static_cast<long long>(std::trunc(raw[i]));      // floor for non-negative totals
        sum += out[i];
    }
    const long long rest = total - sum;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const double fa = -(raw[a] - double(out[a])), fb = -(raw[b] - double(out[b]));
        return fa != fb ? fa < fb : a < b;
    });
    // order[:rest] — Python's slice: a negative rest leaves out that many from the end
    const long long count = rest >= 0 ? std::min<long long>(rest, static_cast<long long>(n)) : std::max<long long>(0, static_cast<long long>(n) + rest);
    for (long long k = 0; k < count; ++k)
        out[order[size_t(k)]] += 1;
    return out;
}

bool PopFilter::match(const Pop &p) const
{
    return (!types || types->contains(p.type)) && (!cultures || cultures->contains(p.culture)) &&
           !no_types.contains(p.type) && !no_cultures.contains(p.culture) && !no_religions.contains(p.religion);
}

// ---------------------------------------------------------------------- the small stores of this module
UtilityMarks::UtilityMarks(moddata::Mod &m, Document &d) : mod(m), doc(d)
{
    for (const auto &kv : mod.utility)
        saved.insert(kv.first);
}

void UtilityMarks::set(const QStringList &rids, bool on)
{
    for (const QString &rid : rids)
        mod.set_utility(rid, on);
    doc._sync_regions();
}

Snap UtilityMarks::snapshot() const
{
    QSet<QString> s;
    for (const auto &kv : mod.utility)
        s.insert(kv.first);
    return store::box(s);
}

void UtilityMarks::restore(const Snap &snap)
{
    const QSet<QString> &want = store::unbox<QSet<QString>>(snap);
    QStringList have;
    for (const auto &kv : mod.utility)
        have << kv.first;
    for (const QString &rid : have)
        if (!want.contains(rid))
            mod.set_utility(rid, false);
    for (const QString &rid : want)
        if (mod.regions.contains(rid))
            mod.set_utility(rid, true);
    doc._sync_regions();
}

QStringList UtilityMarks::dirty() const
{
    QSet<QString> now;
    for (const auto &kv : mod.utility)
        now.insert(kv.first);
    const QSet<QString> x = (now - saved) | (saved - now);
    QStringList out(x.begin(), x.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<QStringList> UtilityMarks::save(const QString &)
{
    if (!dirty().isEmpty()) {
        mod.save_utility();
        saved.clear();
        for (const auto &kv : mod.utility)
            saved.insert(kv.first);
    }
    return std::nullopt;
}

QStringList DropPopBlocks::dirty() const
{
    QList<long long> p(pids.begin(), pids.end());
    std::sort(p.begin(), p.end());
    QStringList out;
    for (long long x : p)
        out << QString::number(x);
    return out;
}

bool Picture::operator==(const Picture &o) const
{
    if (!(pops == o.pops && regs == o.regs && provs == o.provs) || lists.size() != o.lists.size())
        return false;
    for (const auto &[k, s] : lists) {
        const Snap *q = o.lists.get(k);
        if (!q || !store::same(*q, s))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------- Document
Document::Document(moddata::Mod &m, const QString &sc) : mod(m), scenario(sc)
{
    mod.apply_history(scenario);
    moddata::PopLoad pl = moddata::load_pop_files(mod, scenario);
    src = std::move(pl.provinces);
    file_hashes = std::move(pl.files);
    problems = std::move(pl.problems);
    for (const auto &[pid, e] : src)
        pops.insert(pid, e.pops);
    for (const auto &[rid, r] : mod.regions)
        region_orig.insert(rid, r.provinces);
    for (const auto &[rid, r] : mod.utility)
        region_orig.insert(rid, r.provinces);
    // province history (owner, cores, buildings, ...) at this scenario's date
    date = script::parse_date(scenario);
    if (!mod.hist)
        mod.hist = std::make_shared<ProvinceHistory>(mod);
    hist = mod.hist;
    // map/climate.txt and map/continent.txt memberships (not date-bound)
    for (const QString kind : {u"climate"_s, u"continent"_s})
        maps.insert(kind, std::make_shared<store::StoreOf<mapfiles::MembershipFile>>(mod, kind));
    maps.insert(u"adjacency"_s, std::make_shared<store::StoreOf<adjacency::AdjacencyFile>>(mod));
    maps.insert(u"default_map"_s, std::make_shared<store::StoreOf<mapfiles::DefaultMapFile>>(mod));
    maps.insert(u"drop_states"_s, std::make_shared<DropStates>());
    maps.insert(u"drop_pop_blocks"_s, std::make_shared<DropPopBlocks>());   // pops of provinces that don't exist
}

// ---------------------------------------------------------------------- queries
const std::vector<Pop> &Document::province_pops(long long pid) const
{
    static const std::vector<Pop> NONE;
    const auto *p = pops.get(pid);
    return p ? *p : NONE;
}

long long Document::total(long long pid) const
{
    long long t = 0;
    for (const Pop &p : province_pops(pid))
        t += p.size;
    return t;
}

const Region *Document::region(const QString &rid) const { return mod.regions.get(rid); }

std::optional<long long> Document::capital(const QString &rid) const
{
    const Region *r = mod.regions.get(rid);
    return (r && !r->provinces.empty()) ? std::optional<long long>(r->provinces[0]) : std::nullopt;
}

bool Document::is_dirty(long long pid) const
{
    const std::vector<Pop> &cur = province_pops(pid);
    static const std::vector<Pop> NONE;
    const auto *e = src.get(pid);
    const std::vector<Pop> &orig = e ? e->pops : NONE;
    if (cur.size() != orig.size())
        return true;
    QSet<const moddata::PopOrigin *> ids;
    for (const Pop &p : cur) {
        const auto &o = p.o;
        if (!o || p.type != o->type || p.culture != o->culture || p.religion != o->religion || p.size != o->size ||
            p.extra != o->extra)
            return true;
        ids.insert(o.get());
    }
    return ids.size() != qsizetype(cur.size());
}

std::vector<long long> Document::dirty_provinces() const
{
    std::vector<long long> out;
    for (long long pid : touched)
        if (is_dirty(pid))
            out.push_back(pid);
    std::sort(out.begin(), out.end());
    return out;
}

QStringList Document::dirty_regions() const
{
    QStringList out;
    const QSet<QString> &on_disk = mod.region_keys_on_disk;
    QSet<QString> rids = regions_touched;
    if (const StoreP *d = maps.get(u"drop_states"_s))
        rids |= dynamic_cast<const DropStates &>(**d).rids;
    for (const QString &rid : rids) {
        const Region *r = mod.any_region(rid);
        if (!r || (!on_disk.contains(rid) && r->provinces.empty()))
            continue;
        // changed — or an empty state still in region.txt: on Apply its entry goes (a state without provinces is
        // gone, the user's rule)
        if (r->provinces != region_orig.value(rid) || (r->provinces.empty() && mod.regions.contains(rid)))
            out << rid;
    }
    std::sort(out.begin(), out.end());
    return out;
}

QStringList Document::empty_states_on_disk() const
{
    // States region.txt lists without a single province (left over in the mod's file).
    QStringList out;
    for (const auto &[rid, r] : mod.regions)
        if (r.provinces.empty() && mod.region_keys_on_disk.contains(rid))
            out << rid;
    std::sort(out.begin(), out.end());
    return out;
}

QStringList Document::drop_empty_states(const QStringList &rids0)
{
    // The fix for empty states in region.txt: their entries go on Apply (one undo step).
    QStringList rids;
    for (const QString &r : rids0)
        if (const Region *rg = mod.regions.get(r); rg && rg->provinces.empty())
            rids << r;
    auto &drop = dynamic_cast<DropStates &>(**maps.get(u"drop_states"_s));
    if (!rids.isEmpty())
        edit(u"Empty states out of region.txt (%1)"_s.arg(rids.size()), {}, [&] {
            for (const QString &r : rids)
                drop.rids.insert(r);
        }, {}, {}, {u"drop_states"_s});
    return rids;
}

long long Document::pending_count() const
{
    long long n = static_cast<long long>(dirty_provinces().size()) + dirty_regions().size() +
                  static_cast<long long>(const_cast<Document *>(this)->dirty_histories().size());
    for (const auto &kv : maps)
        n += kv.second->dirty().size();
    return n;
}

void Document::set_definitions(const QString &kind, const OrderedMap<QString, std::vector<std::pair<QString, QString>>> &mods,
                               const QStringList &new_names, const QStringList &removed,
                               const OrderedMap<QString, QString> &moves)
{
    // Change climate / continent modifiers, add new ones and remove mod-added ones (one undo step).
    if (mods.isEmpty() && new_names.isEmpty() && removed.isEmpty())
        return;
    auto &mf = map<mapfiles::MembershipFile>(kind);
    QStringList what = new_names + removed;
    for (const auto &kv : mods)
        what << kv.first;
    edit(u"Edit %1s (%2)"_s.arg(kind, what.mid(0, 3).join(u", "_s)), {}, [&] {
        mf.set_definitions(mods, new_names);
        for (const QString &r : removed) {
            const QString *to = moves.get(r);
            const auto *l = mf.lists.get(r);
            if (to && !to->isEmpty() && l && !l->empty())
                mf.assign(QList<long long>(l->begin(), l->end()), *to);
        }
        mf.remove(removed);
    }, {}, {}, {kind});
}

void Document::adj_edit(const QString &label, const std::function<void(adjacency::AdjacencyFile &)> &fn)
{
    // fn(AdjacencyFile) changes map/adjacencies.csv rows; one undo step.
    adjacency::AdjacencyFile &adj = map<adjacency::AdjacencyFile>(u"adjacency"_s);
    edit(label, {}, [&] { fn(adj); }, {}, {}, {u"adjacency"_s});
}

void Document::pos_hist_edit(const QString &label, const QList<long long> &hpids,
                             const std::function<void(positions::PositionsFile &)> &fn)
{
    // One undo step that changes positions.txt entries AND the history of provinces `hpids`
    // (fn(PositionsFile) may set prov_want[pid]).
    positions::PositionsFile &pf = map<positions::PositionsFile>(u"positions"_s);
    edit(label, {}, [&] { fn(pf); }, {}, hpids, {u"positions"_s});
}

void Document::pos_edit(const QString &label, const std::function<void(positions::PositionsFile &)> &fn)
{
    // fn(PositionsFile) changes map/positions.txt entries; one undo step.
    positions::PositionsFile &pf = map<positions::PositionsFile>(u"positions"_s);
    edit(label, {}, [&] { fn(pf); }, {}, {}, {u"positions"_s});
}

void Document::set_membership(const QString &kind, const QList<long long> &pids, const QString &name)
{
    // Assign provinces to one climate / continent (removing them from every other one).
    auto &mf = map<mapfiles::MembershipFile>(kind);
    edit(u"%1 → %2"_s.arg(py::capitalize(kind), name), {}, [&] { mf.assign(pids, name); }, {}, {}, {kind});
}

// ---------------------------------------------------------------------- province history
const ProvState &Document::prov_state(long long pid)
{
    // Current (possibly edited) history state of a province at the scenario date.
    if (const ProvState *w = prov_want.get(pid))
        return *w;
    return _orig_state(pid);
}

void Document::levels_changed(const QStringList &more)
{
    if (!hist || hist->more == more)
        return;
    const QStringList old = hist->more;
    hist->more = more;
    QStringList added;
    for (const QString &k : more)
        if (!old.contains(k))
            added << k;
    for (auto &[pid, want] : prov_want) {
        for (auto it = want.more.begin(); it != want.more.end();)
            it = more.contains(it->first) ? std::next(it) : want.more.erase(it);
        if (!added.isEmpty()) {
            const ProvState f = hist->state_at(pid, date);
            for (const QString &k : added)
                want.set_level(k, f.level_of(k));
        }
    }
    prov_orig.clear();
}

const ProvState &Document::_orig_state(long long pid)
{
    if (!prov_orig.contains(pid))
        prov_orig.insert(pid, hist->state_at(pid, date));
    return *prov_orig.get(pid);
}

StateKey Document::_state_key(const ProvState &st)
{
    StateKey k{st.owner, st.controller, st.cores, st.life_rating, st.trade_goods,
               st.colonial, st.fort, st.naval_base, st.railroad, {}};
    std::sort(k.cores.begin(), k.cores.end());
    for (const Factory &f : st.factories)
        k.factories.emplace_back(f.building.value_or(QString()), f.level, f.upgrade,
                                 f.src ? f.src->path : QString(), f.src ? f.src->span.first : 0,
                                 f.src ? f.src->span.second : 0);
    std::sort(k.factories.begin(), k.factories.end());
    k.more = st.more;
    return k;
}

std::vector<long long> Document::dirty_histories()
{
    std::vector<long long> out;
    for (long long pid : prov_touched)
        if (const ProvState *w = prov_want.get(pid); w && !(_state_key(*w) == _state_key(_orig_state(pid))))
            out.push_back(pid);
    std::sort(out.begin(), out.end());
    return out;
}

void Document::edit_provinces(const QString &label, const QList<long long> &pids,
                              const std::function<void(long long, ProvState &)> &fn)
{
    // fn(pid, state) mutates a working copy of each province's history state.
    edit(label, {}, [&] {
        for (long long pid : pids) {
            ProvState st = history::clone_state(prov_state(pid));
            fn(pid, st);
            prov_want.insert(pid, st);
        }
    }, {}, pids);
}

bool Document::state_is_coastal(const QString &rid) const
{
    const Region *r = mod.regions.get(rid);
    if (!r || !coastal)
        return true;
    for (long long p : r->provinces)
        if (p < static_cast<long long>(coastal->size()) && (*coastal)[size_t(p)])
            return true;
    return false;
}

QStringList Document::session_rids() const
{
    // States touched in this session (moves, capital changes, edited provinces).
    QSet<QString> rids = regions_touched;
    for (long long pid : prov_touched)
        if (const auto *pr = mod.provinces.get(pid); pr && pr->region)
            rids.insert(*pr->region);
    QStringList out;
    for (const QString &r : rids)
        if (mod.regions.contains(r))
            out << r;
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<FactoryProblem> Document::factory_problems(const std::optional<QStringList> &rids)
{
    // [(rid, pid, factory, reason)] — factories over the 8-per-state limit (the last ones in the state's order)
    // and coastal-only factories (shipyards) in states without a coastal province.
    std::vector<FactoryProblem> out;
    QStringList all;
    if (rids)
        all = *rids;
    else
        for (const auto &kv : mod.regions)
            all << kv.first;
    for (const QString &rid : all) {
        const auto facts = state_factories(rid);
        const bool coast = state_is_coastal(rid);
        for (size_t i = 0; i < facts.size(); ++i) {
            const auto &[pid, f] = facts[i];
            if (i >= 8)
                out.push_back({rid, pid, f, u"over the limit of 8 factories per state"_s});
            else if (!coast && mod.factory_info(f.building.value_or(QString())).coastal)
                out.push_back({rid, pid, f, u"needs a coastal state"_s});
        }
    }
    return out;
}

void Document::remove_factories(const std::vector<FactoryProblem> &problems_)
{
    // Remove the given (rid, pid, factory, reason) entries as one undoable step.
    auto by_pid = std::make_shared<OrderedMap<long long, std::vector<Factory>>>();
    for (const FactoryProblem &p : problems_)
        (*by_pid)[p.pid].push_back(p.f);
    auto span_of = [](const Factory &f) { return f.src ? std::optional<history::Span>(f.src->span) : std::nullopt; };
    QList<long long> pids;
    for (const auto &kv : *by_pid)
        pids << kv.first;
    edit_provinces(u"Remove factories"_s, pids, [by_pid, span_of](long long pid, ProvState &st) {
        std::vector<Factory> &drop = (*by_pid)[pid];
        std::vector<Factory> keep;
        for (const Factory &f : st.factories) {
            auto hit = std::find_if(drop.begin(), drop.end(), [&](const Factory &d) {
                return d.building == f.building && span_of(d) == span_of(f);
            });
            if (hit != drop.end())
                drop.erase(hit);
            else
                keep.push_back(f);
        }
        st.factories = keep;
    });
}

std::vector<std::pair<long long, Factory>> Document::state_factories(const QString &rid)
{
    // [(pid, factory)] of all factories in a state (current, edited values).
    std::vector<std::pair<long long, Factory>> out;
    const Region *r = mod.regions.get(rid);
    if (!r)
        return out;
    const std::vector<long long> provs = r->provinces;
    for (long long pid : provs)
        for (const Factory &f : prov_state(pid).factories)
            out.emplace_back(pid, f);
    return out;
}

// ---------------------------------------------------------------------- undo
Picture Document::_snapshot(const QSet<long long> &pids, const QStringList &rids, const QSet<long long> &hpids,
                            const QStringList &lists)
{
    Picture s;
    for (long long pid : pids)
        s.pops.insert(pid, province_pops(pid));
    for (const QString &rid : rids)
        s.regs.insert(rid, _reg(rid).provinces);
    for (long long pid : hpids) {
        const ProvState *w = prov_want.get(pid);
        s.provs.insert(pid, w ? std::optional<ProvState>(history::clone_state(*w)) : std::nullopt);
    }
    for (const QString &k : lists)
        s.lists.insert(k, (*maps.get(k))->snapshot());
    return s;
}

std::vector<Pop> Document::_relink_pops(long long pid, std::vector<Pop> pops_) const
{
    // Pops put back by undo / redo point at their lines in the pop file as it is NOW (after an Apply the file was
    // read again, a step from before still points at the old lines): each takes the line of a pop of its own kind
    // — one of its size first; a pop that finds none is a new one.
    std::vector<std::shared_ptr<const moddata::PopOrigin>> lines;
    if (const auto *e = src.get(pid))
        for (const Pop &sp : e->pops)
            lines.push_back(sp.o);
    QSet<const moddata::PopOrigin *> alive;
    for (const auto &o : lines)
        alive.insert(o.get());
    if (std::all_of(pops_.begin(), pops_.end(), [&](const Pop &p) { return !p.o || alive.contains(p.o.get()); }))
        return pops_;
    std::vector<std::shared_ptr<const moddata::PopOrigin>> free;
    for (const auto &o : lines)
        if (std::none_of(pops_.begin(), pops_.end(), [&](const Pop &p) { return p.o.get() == o.get(); }))
            free.push_back(o);
    std::vector<size_t> stale;
    for (size_t i = 0; i < pops_.size(); ++i)
        if (pops_[i].o && !alive.contains(pops_[i].o.get()))
            stale.push_back(i);
    auto key_o = [](const moddata::PopOrigin &x) { return std::tie(x.type, x.culture, x.religion, x.extra); };
    auto key_p = [](const Pop &x) { return std::tie(x.type, x.culture, x.religion, x.extra); };
    for (const bool exact : {true, false}) {
        for (size_t i : stale) {
            Pop &p = pops_[i];
            if (p.o && alive.contains(p.o.get()))
                continue;
            auto hit = std::find_if(free.begin(), free.end(), [&](const auto &o) {
                return key_o(*o) == key_p(p) && (!exact || o->size == p.size);
            });
            if (hit != free.end()) {
                p.o = *hit;
                alive.insert(hit->get());
                free.erase(hit);
            }
        }
    }
    for (size_t i : stale)
        if (!(pops_[i].o && alive.contains(pops_[i].o.get())))
            pops_[i].o = nullptr;
    return pops_;
}

ProvState Document::_reanchor(long long pid, ProvState st)
{
    // A province's state put back by undo / redo: its factories point at their lines in the history file as it is
    // NOW: each takes the line of a factory of its own kind — one that finds none is a new one.
    if (st.factories.empty())
        return st;
    const ProvState orig = _orig_state(pid);
    std::vector<const history::FactorySrc *> live;
    for (const Factory &f : orig.factories)
        if (f.src)
            live.push_back(f.src.get());
    auto is_live = [&](const Factory &f) {
        return std::find(live.begin(), live.end(), f.src.get()) != live.end();
    };
    if (std::all_of(st.factories.begin(), st.factories.end(), [&](const Factory &f) { return !f.src || is_live(f); }))
        return st;
    std::vector<Factory> free;
    for (const Factory &f : orig.factories)
        if (f.src && std::none_of(st.factories.begin(), st.factories.end(),
                                  [&](const Factory &g) { return g.src.get() == f.src.get(); }))
            free.push_back(f);
    for (Factory &f : st.factories) {
        if (!f.src || is_live(f))
            continue;
        auto hit = std::find_if(free.begin(), free.end(), [&](const Factory &o) { return o.building == f.building; });
        if (hit != free.end()) {
            f.src = hit->src;
            free.erase(hit);
        } else {
            f.src = nullptr;
        }
    }
    return st;
}

const Items *Document::_items(const Snap &snap)
{
    // A kind's picture as {key: value} — for the kinds whose picture is a dict, or pairs (the localisation).
    const auto *p = dynamic_cast<const store::ItemsSnap *>(snap.get());
    return p ? &p->items : nullptr;
}

Snap Document::_part(const store::Store &mf, const Snap &target, const Snap &other) const
{
    // Undo / redo of ONE step puts back only what that step changed: the keys where its two pictures differ —
    // everything else stays as it is now (the tester's report, 2026-10-15).
    const Items *t = _items(target), *o = _items(other);
    const Snap now = mf.snapshot();
    const Items *cur = _items(now);
    if (!t || !o || !cur)
        return target;
    Items merged = *cur;
    const QSet<Key> extra = mf.has_part_keys() ? mf.part_keys(*t, *o) : QSet<Key>();
    QList<Key> keys;                              // (Python walks a set here; the order cannot reach the result)
    QSet<Key> seen;
    auto add = [&](const Key &k) {
        if (!seen.contains(k)) {
            seen.insert(k);
            keys << k;
        }
    };
    for (const auto &kv : *t)
        add(kv.first);
    for (const auto &kv : *o)
        add(kv.first);
    for (const Key &k : extra)
        add(k);
    for (const Key &k : keys) {
        const Snap *tv = t->get(k), *ov = o->get(k);
        const bool differ = (tv == nullptr) != (ov == nullptr) || (tv && !store::same(*tv, *ov));
        if (differ || extra.contains(k)) {
            if (tv)
                merged.insert(k, *tv);
            else
                merged.remove(k);
        }
    }
    const bool pairs = dynamic_cast<const store::ItemsSnap &>(*target).pairs;
    if (pairs) {                                  // tuple(sorted(merged.items()))
        std::vector<std::pair<Key, Snap>> v(merged.begin(), merged.end());
        std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        Items sorted_items;
        for (auto &kv : v)
            sorted_items.insert(kv.first, kv.second);
        merged = sorted_items;
    }
    return std::make_shared<const store::ItemsSnap>(merged, pairs);
}

void Document::_restore(const Picture &snap, const Picture *other)
{
    for (const auto &[k, s0] : snap.lists) {
        store::Store &mf = **maps.get(k);
        Snap s = s0;
        if (other && mf.partial_undo() && other->lists.contains(k))
            s = _part(mf, s, *other->lists.get(k));
        mf.restore(s);
    }
    for (const auto &[pid, lst] : snap.pops) {
        pops.insert(pid, _relink_pops(pid, lst));
        touched.insert(pid);
    }
    for (const auto &[rid, order] : snap.regs) {
        _reg(rid).provinces = order;
        regions_touched.insert(rid);
    }
    if (!snap.regs.isEmpty())
        _sync_regions();
    for (const auto &[pid, st] : snap.provs) {
        if (!st)
            prov_want.remove(pid);
        else
            prov_want.insert(pid, _reanchor(pid, history::clone_state(*st)));
        prov_touched.insert(pid);
    }
}

Document::Keep Document::_keep_undo()
{
    // Before Apply writes: what the undo steps need to stay true once the files have changed.
    Keep keep;
    for (long long pid : dirty_histories())
        keep.provs.insert(pid, history::clone_state(_orig_state(pid)));
    for (const auto &[kind, mf] : maps) {
        try {
            if (mf->dirty().isEmpty())
                continue;
            if (mf->has_rebase())
                keep.tokens.insert(kind, mf->rebase_begin());
            else if (!mf->undo_after_apply())
                keep.lost.insert(kind);         // new provinces, new nations, added files, armies …: once
        } catch (const std::exception &) {      // written, their steps can't be taken back
            keep.lost.insert(kind);
        }
    }
    return keep;
}

void Document::_rebase_undo(const Keep &keep)
{
    // After Apply wrote: the undo / redo steps are brought to the new state of the disk. A step that touches
    // something that can't be taken back once written goes, with every older one.
    QStringList lost(keep.lost.begin(), keep.lost.end());
    std::sort(lost.begin(), lost.end());
    undo_lost = {0, lost};                      // (steps that went, the kinds of thing that made them go)
    for (std::vector<StepP> *stack : {&undo_stack, &redo_stack}) {
        size_t cut = 0;
        for (size_t i = 0; i < stack->size(); ++i) {
            bool ok = true;
            for (Picture *snap : {&(*stack)[i]->before, &(*stack)[i]->after}) {
                for (const auto &[pid, old] : keep.provs)
                    if (auto *v = snap->provs.get(pid); v && !*v)
                        *v = history::clone_state(old);
                QStringList kinds;
                for (const auto &kv : snap->lists)
                    kinds << kv.first;
                for (const QString &kind : kinds) {
                    if (keep.lost.contains(kind)) {
                        ok = false;
                    } else if (keep.tokens.contains(kind)) {
                        try {
                            snap->lists.insert(kind, (*maps.get(kind))->rebase(*snap->lists.get(kind), *keep.tokens.get(kind)));
                        } catch (const std::exception &) {
                            ok = false;
                        }
                    }
                }
            }
            if (!ok)
                cut = i + 1;
        }
        if (stack == &undo_stack)
            undo_lost = {static_cast<long long>(cut), lost};
        stack->erase(stack->begin(), stack->begin() + static_cast<long long>(cut));
    }
}

void Document::_after_edit(const QString &label, const QSet<long long> &pids, const QStringList &rids,
                           const QSet<long long> &hpids, const QStringList &lists, Picture before)
{
    Picture after = _snapshot(pids, rids, hpids, lists);
    if (!(before == after)) {
        touched |= pids;
        for (const QString &r : rids)
            regions_touched.insert(r);
        prov_touched |= hpids;
        undo_stack.push_back(std::make_shared<Step>(Step{label, std::move(before), std::move(after)}));
        if (undo_stack.size() > size_t(UNDO_STEPS))
            undo_stack.erase(undo_stack.begin(), undo_stack.end() - UNDO_STEPS);
        redo_stack.clear();
        _live_reset();
        changed(pids);
    }
}

void Document::merge_steps(const StepP &mark, const QString &label)
{
    size_t from = 0;                              // (mark gone — the oldest steps fell off — : all of them)
    for (size_t i = undo_stack.size(); i-- > 0;)
        if (undo_stack[i] == mark) {
            from = i + 1;
            break;
        }
    if (from >= undo_stack.size())
        return;
    auto m = std::make_shared<Step>();
    m->label = label;
    auto first = [](auto &into, const auto &from_) {
        for (const auto &[k, v] : from_)
            if (!into.contains(k))
                into.insert(k, v);
    };
    auto last = [](auto &into, const auto &from_) {
        for (const auto &[k, v] : from_)
            into.insert(k, v);
    };
    for (size_t i = from; i < undo_stack.size(); ++i) {
        const Step &s = *undo_stack[i];
        first(m->before.pops, s.before.pops);
        first(m->before.regs, s.before.regs);
        first(m->before.provs, s.before.provs);
        first(m->before.lists, s.before.lists);
        last(m->after.pops, s.after.pops);
        last(m->after.regs, s.after.regs);
        last(m->after.provs, s.after.provs);
        last(m->after.lists, s.after.lists);
    }
    undo_stack.erase(undo_stack.begin() + std::ptrdiff_t(from), undo_stack.end());
    undo_stack.push_back(std::move(m));
    _live_reset();
}

std::optional<QString> Document::undo()
{
    flush_live();
    if (undo_stack.empty())
        return std::nullopt;
    StepP step = undo_stack.back();             // (the very same step moves over)
    undo_stack.pop_back();
    _busy += 1;
    try {
        _restore(step->before, &step->after);
    } catch (...) {
        _busy -= 1;
        throw;
    }
    _busy -= 1;
    redo_stack.push_back(step);
    _live_reset();
    QSet<long long> ch;
    for (const auto &kv : step->before.pops)
        ch.insert(kv.first);
    for (const auto &kv : step->before.provs)
        ch.insert(kv.first);
    changed(ch);
    return step->label;
}

std::optional<QString> Document::redo()
{
    flush_live();
    if (redo_stack.empty())
        return std::nullopt;
    StepP step = redo_stack.back();
    redo_stack.pop_back();
    _busy += 1;
    try {
        _restore(step->after, &step->before);
    } catch (...) {
        _busy -= 1;
        throw;
    }
    _busy -= 1;
    undo_stack.push_back(step);
    _live_reset();
    QSet<long long> ch;
    for (const auto &kv : step->after.pops)
        ch.insert(kv.first);
    for (const auto &kv : step->after.provs)
        ch.insert(kv.first);
    changed(ch);
    return step->label;
}

// ---------------------------------------------------------------------- live texts (typing) as undo steps
void Document::track_live(const QString &kind)
{
    // `kind` changes live (typing): its picture now is where the next typing step starts.
    if (!_live_kinds.contains(kind))
        _live_kinds << kind;
    if (const StoreP *mf = maps.get(kind))
        _live_base.insert(kind, (*mf)->snapshot());
}

void Document::_live_reset()
{
    _live_open.reset();
    for (const QString &kind : _live_kinds)
        if (const StoreP *mf = maps.get(kind)) {
            _live_base.insert(kind, (*mf)->snapshot());
            _live_sig.insert(kind, (*mf)->has_live_sig() ? OptStr((*mf)->live_sig()) : std::nullopt);
        }
}

bool Document::record_live(const QString &kind, const OptStr &key, const QString &label)
{
    // A live kind changed (typing in `key`). -> True when the undo list got (or grew) a step.
    if (_busy || !_live_kinds.contains(kind))
        return false;
    const StoreP *mfp = maps.get(kind);
    const Snap *base = _live_base.get(kind);
    if (!mfp || !base || !*base)
        return false;
    store::Store &mf = **mfp;
    const OptStr sig = mf.has_live_sig() ? OptStr(mf.live_sig()) : std::nullopt;
    if (sig && sig == _live_sig.value(kind))
        return false;                           // (only coloured, not changed: nothing to look at)
    _live_sig.insert(kind, sig);
    const Snap now = mf.snapshot();
    if (store::same(now, *base))
        return false;
    const double t = monotonic();
    StepP step;
    if (_live_open && _live_open->kind == kind && _live_open->key == key && t - _live_open->t < LIVE_MERGE &&
        !undo_stack.empty() && undo_stack.back() == _live_open->step) {
        const StepP last = undo_stack.back();
        Picture after;
        after.lists.insert(kind, now);
        step = std::make_shared<Step>(Step{last->label, last->before, after});
        undo_stack.back() = step;
    } else {
        Picture before, after;
        before.lists.insert(kind, *base);
        after.lists.insert(kind, now);
        step = std::make_shared<Step>(Step{label, before, after});
        undo_stack.push_back(step);
        if (undo_stack.size() > size_t(UNDO_STEPS))
            undo_stack.erase(undo_stack.begin(), undo_stack.end() - UNDO_STEPS);
    }
    redo_stack.clear();
    _live_open = LiveOpen{kind, key, t, step};
    _live_base.insert(kind, now);
    return true;
}

void Document::flush_live()
{
    // Typing that no listener reported yet (the listeners run first as a rule): a step of its own now.
    for (const QString &kind : _live_kinds) {
        const StoreP *mfp = maps.get(kind);
        const Snap *base = _live_base.get(kind);
        if (!mfp || _busy || !base || !*base)
            continue;
        store::Store &mf = **mfp;
        if (mf.has_live_sig() && OptStr(mf.live_sig()) == _live_sig.value(kind))
            continue;                           // (nothing changed since the last look)
        _live_sig.remove(kind);
        if (!store::same(mf.snapshot(), *base))
            record_live(kind, std::nullopt, u"Typed in the script texts"_s);
    }
    _live_open.reset();
}

std::pair<long long, QStringList> Document::step_scope(const Step &step) const
{
    // -> (how many changes the step holds, [short descriptions])
    const Picture &before = step.before, &after = step.after;
    QStringList out;
    QSet<long long> provs;
    long long other = 0;
    QSet<long long> pids;
    for (const auto &kv : before.pops)
        pids.insert(kv.first);
    for (const auto &kv : after.pops)
        pids.insert(kv.first);
    if (!pids.isEmpty()) {
        provs |= pids;
        out << u"pops of %1 province%2"_s.arg(pids.size()).arg(plural(pids.size()));
    }
    QSet<QString> rs;
    for (const auto &kv : before.regs)
        rs.insert(kv.first);
    for (const auto &kv : after.regs)
        rs.insert(kv.first);
    QStringList regs;
    for (const QString &r : rs)
        if (before.regs.value(r) != after.regs.value(r) || before.regs.contains(r) != after.regs.contains(r))
            regs << r;
    std::sort(regs.begin(), regs.end());
    if (!regs.isEmpty()) {
        other += regs.size();
        out << u"%1 state%2 (%3)"_s.arg(regs.size()).arg(plural(regs.size())).arg(regs.mid(0, 4).join(u", "_s));
    }
    QSet<long long> hp;
    for (const auto &kv : before.provs)
        hp.insert(kv.first);
    for (const auto &kv : after.provs)
        hp.insert(kv.first);
    if (!hp.isEmpty()) {
        provs |= hp;
        out << u"history of %1 province%2"_s.arg(hp.size()).arg(plural(hp.size()));
    }
    QSet<QString> ks;
    for (const auto &kv : before.lists)
        ks.insert(kv.first);
    for (const auto &kv : after.lists)
        ks.insert(kv.first);
    QStringList kinds(ks.begin(), ks.end());
    std::sort(kinds.begin(), kinds.end());
    static const OrderedMap<QString, QString> NICE = [] {
        OrderedMap<QString, QString> m;
        m.insert(u"scripts"_s, u"file"_s);
        m.insert(u"loc"_s, u"name"_s);
        m.insert(u"nations"_s, u"nation"_s);
        m.insert(u"oob"_s, u"army file"_s);
        m.insert(u"positions"_s, u"place on the map"_s);
        return m;
    }();
    for (const QString &k : kinds) {
        const Snap *bs = before.lists.get(k), *as = after.lists.get(k);
        const Items *b = bs ? _items(*bs) : nullptr, *a = as ? _items(*as) : nullptr;
        if (b && a) {
            QList<Key> keys;
            QSet<Key> all;
            for (const auto &kv : *b)
                all.insert(kv.first);
            for (const auto &kv : *a)
                all.insert(kv.first);
            for (const Key &x : all) {
                const Snap *bx = b->get(x), *ax = a->get(x);
                if ((bx == nullptr) != (ax == nullptr) || (bx && !store::same(*bx, *ax)))
                    keys << x;
            }
            std::stable_sort(keys.begin(), keys.end(), [](const Key &l, const Key &r) { return l.str() < r.str(); });
            QList<Key> kept;
            for (const Key &x : keys)
                if (x.is_int() || x.text() != QString(QChar(0)) + u"gone"_s)
                    kept << x;
            if (!kept.isEmpty())
                keys = kept;
            if (!keys.isEmpty()) {
                long long ints = 0;
                for (const Key &x : keys)
                    if (x.is_int()) {
                        provs.insert(x.num());                // (positions: by province)
                        ++ints;
                    }
                other += keys.size() - ints;
                QStringList first3;
                for (qsizetype i = 0; i < std::min<qsizetype>(3, keys.size()); ++i)
                    first3 << keys[i].str();
                out << u"%1 %2%3: %4%5"_s.arg(keys.size()).arg(NICE.value(k, k)).arg(plural(keys.size()))
                           .arg(first3.join(u", "_s), keys.size() > 3 ? u", …"_s : QString());
            }
        } else if (!((bs == nullptr) == (as == nullptr) && (!bs || store::same(*bs, *as)))) {
            QString nice = k;
            nice.replace(u'_', u' ');
            out << nice;
        }
    }
    return {provs.size() + other, out};
}

void Document::changed(const QSet<long long> &pids)
{
    for (auto &cb : listeners)
        cb(pids);
}

// ---------------------------------------------------------------------- operations
std::vector<Pop *> Document::selected_pops(const QList<long long> &pids, const PopFilter &flt)
{
    std::vector<Pop *> out;
    for (long long pid : pids)
        if (auto *l = pops.get(pid))
            for (Pop &p : *l)
                if (flt.match(p))
                    out.push_back(&p);
    return out;
}

void Document::_prune(const QList<long long> &pids)
{
    for (long long pid : pids)
        if (auto *l = pops.get(pid)) {
            std::vector<Pop> keep;
            for (const Pop &p : *l)
                if (p.size > 0)
                    keep.push_back(p);
            *l = keep;
        }
}

long long Document::_scale_run(const QList<long long> &pids, const PopFilter &flt, std::optional<double> percent,
                               std::optional<long long> new_total)
{
    // (the change itself, inside an edit step)
    {
        std::vector<Pop *> sel = selected_pops(pids, flt);
        long long cur = 0;
        for (const Pop *p : sel)
            cur += p->size;
        if (sel.empty() || cur <= 0)
            return 0;
        long long target;
        if (new_total) {
            target = std::max<long long>(0, *new_total);
        } else {
            target = py::round_int(double(cur) * (1 + *percent / 100.0));
            if (target == cur && *percent != 0)      // small pops: always move at least one
                target = cur + (*percent > 0 ? 1 : -1);
            target = std::max<long long>(0, target);
        }
        std::vector<double> w;
        for (const Pop *p : sel)
            w.push_back(double(p->size));
        const auto shares = distribute(target, w);
        for (size_t i = 0; i < sel.size(); ++i)
            sel[i]->size = shares[i];
        _prune(pids);
        return target - cur;
    }
}

long long Document::scale(const QList<long long> &pids, const PopFilter &flt, std::optional<double> percent,
                          std::optional<long long> new_total)
{
    // Scale the filtered pops of the given provinces by +percent% or to an exact new total.
    auto run = [&]() -> long long { return _scale_run(pids, flt, percent, new_total); };
    QString label;
    if (new_total) {
        label = u"Set total to %1"_s.arg(*new_total);
    } else {
        QString g = py::fmt_g(*percent);              // "%+g%%"
        if (!g.startsWith(u'-'))
            g = u'+' + g;
        label = g + u'%';
    }
    return edit(label, pids, run);
}

void Document::_add_to(long long pid, const Pop &tmpl, long long amount)
{
    // Add `amount` people of the template's type/culture/religion to province pid.
    if (amount <= 0)
        return;
    std::vector<Pop> &lst = pops[pid];
    const PopKey key = pop_key(tmpl);
    for (auto it = lst.rbegin(); it != lst.rend(); ++it)
        if (pop_key(*it) == key) {
            it->size += amount;
            return;
        }
    Pop p;
    p.type = tmpl.type;
    p.culture = tmpl.culture;
    p.religion = tmpl.religion;
    p.size = amount;
    lst.push_back(p);
}

long long Document::add_pop(const QList<long long> &pids, const QString &ptype, const OptStr &culture,
                            const OptStr &religion, long long size)
{
    // Add `size` people of one pop kind to each province.
    Pop t;
    t.type = ptype;
    t.culture = culture;
    t.religion = religion;
    return edit(u"Add %1 %2"_s.arg(culture.value_or(u"None"_s), ptype), pids, [&]() -> long long {
        for (long long pid : pids)
            _add_to(pid, t, size);
        return size * pids.size();
    });
}

long long Document::paste(const QList<long long> &pids, const std::vector<Pop> &tmpl, bool replace, double factor,
                          std::optional<long long> total)
{
    // Paste copied pops into each province: sizes × `factor`, or rescaled so each province receives `total`.
    std::vector<double> sizes;
    long long ssum = 0;
    for (const Pop &t : tmpl) {
        sizes.push_back(double(t.size));
        ssum += t.size;
    }
    return edit(u"Paste pops"_s, pids, [&]() -> long long {
        const long long amount = total ? *total : py::round_int(double(ssum) * factor);
        const auto shares = distribute(amount, sizes);
        for (long long pid : pids) {
            if (replace)
                pops.insert(pid, {});
            for (size_t i = 0; i < tmpl.size(); ++i)
                _add_to(pid, tmpl[i], shares[i]);
            _prune({pid});
        }
        return amount * pids.size();
    });
}

long long Document::remix(const QList<long long> &pids, const PopFilter &flt, const QString &field,
                          const OrderedMap<OptStr, long long> &targets)
{
    // Change the culture (or religion) mix of the filtered pops to `targets` {key: people}. Segments that shrink
    // give people to segments that grow, in proportion to how much each grows.
    return edit(u"Change %1 mix"_s.arg(field), pids, [&]() -> long long { return _remix_run(pids, flt, field, targets); });
}

// p[field]: the culture, the religion or the pop type (C++ fix 2026-11-13: the mix by pop type changed the
// religion — the field was read as "culture, else religion"; Python reads p[field] by its name)
static OptStr pop_get(const Pop &p, const QString &field)
{
    return field == u"type" ? OptStr(p.type) : field == u"culture" ? p.culture : p.religion;
}

static void pop_set(Pop &p, const QString &field, const OptStr &v)
{
    if (field == u"type")
        p.type = v.value_or(QString());
    else if (field == u"culture")
        p.culture = v;
    else
        p.religion = v;
}

long long Document::_remix_run(const QList<long long> &pids, const PopFilter &flt, const QString &field,
                               const OrderedMap<OptStr, long long> &targets)
{
    {
        std::vector<std::pair<long long, size_t>> sel;              // (pid, index)
        for (long long pid : pids)
            if (const auto *l = pops.get(pid))
                for (size_t i = 0; i < l->size(); ++i)
                    if (flt.match((*l)[i]))
                        sel.emplace_back(pid, i);
        OrderedMap<OptStr, long long> cur;
        for (const auto &[pid, i] : sel)
            cur[pop_get((*pops.get(pid))[i], field)] += (*pops.get(pid))[i].size;
        // Python walks set(cur) | set(targets): a set of texts, in CPython's order (PYTHONHASHSEED=0)
        std::vector<OptStr> ck, tk;
        for (const auto &kv : cur)
            ck.push_back(kv.first);
        for (const auto &kv : targets)
            tk.push_back(kv.first);
        auto keys = grid::PySet<OptStr>::copy_of(grid::PySet<OptStr>::from_keys(ck));
        keys.merge(grid::PySet<OptStr>::from_keys(tk));
        OrderedMap<OptStr, long long> gain;
        for (const OptStr &k : keys.items())
            gain.insert(k, targets.value(k, 0) - cur.value(k, 0));
        std::vector<std::pair<OptStr, long long>> gainers;
        for (const auto &kv : gain)
            if (kv.second > 0)
                gainers.push_back(kv);
        if (gainers.empty())
            return 0;
        std::vector<double> gw;
        for (const auto &g : gainers)
            gw.push_back(double(g.second));
        long long moved = 0;
        for (const auto &[loser, d] : gain) {
            if (d >= 0)
                continue;
            std::vector<std::pair<long long, size_t>> mine;
            std::vector<double> w;
            for (const auto &[pid, i] : sel)
                if (pop_get((*pops.get(pid))[i], field) == loser) {
                    mine.emplace_back(pid, i);
                    w.push_back(double((*pops.get(pid))[i].size));
                }
            const auto takes = distribute(-d, w);
            for (size_t j = 0; j < mine.size(); ++j) {
                const auto [pid, i] = mine[j];
                Pop &p = (*pops.get(pid))[i];
                const long long take = std::min(takes[j], p.size);
                if (take <= 0)
                    continue;
                p.size -= take;
                const auto split = distribute(take, gw);
                Pop tmpl = p;
                for (size_t g = 0; g < gainers.size(); ++g)
                    if (split[g] > 0) {
                        pop_set(tmpl, field, gainers[g].first);
                        _add_to(pid, tmpl, split[g]);
                    }
                moved += take;
            }
        }
        _prune(pids);
        return moved;
    }
}

long long Document::pop_change(const QList<long long> &pids, const PopFilter &flt, std::optional<long long> new_total,
                               const QString &field, const std::vector<std::pair<OptStr, double>> &shares)
{
    // The Pops page's one Apply (C++ only, the user 2026-11-13: "whenever there are two changes at the same time,
    // often one of them just does not happen"): a new total of the filtered pops and / or their mix as shares (%),
    // ONE undo step. The total first; the mix then shares out the new total (not the old one).
    QString label;
    if (new_total)
        label = u"Set total to %1"_s.arg(*new_total);
    if (!shares.empty())
        label += (label.isEmpty() ? QString() : u" + "_s) + u"change %1 mix"_s.arg(field);
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return edit(label, pids, [&]() -> long long {
        long long n = 0;
        if (new_total)
            n += _scale_run(pids, flt, std::nullopt, new_total);
        if (!shares.empty()) {
            long long total = 0;
            for (const Pop *p : selected_pops(pids, flt))
                total += p->size;
            std::vector<double> w;
            for (const auto &sh : shares)
                w.push_back(sh.second);
            const std::vector<long long> people = distribute(total, w);
            OrderedMap<OptStr, long long> targets;
            for (size_t i = 0; i < shares.size(); ++i)
                targets.insert(shares[i].first, people[i]);
            n += _remix_run(pids, flt, field, targets);
        }
        return n;
    });
}

long long Document::split(long long source, const std::vector<std::pair<long long, double>> &targets0,
                          const PopFilter &flt, long long keep_placeholders)
{
    // Move the filtered pops of `source` to targets [(pid, weight), ...]. A target may be `source` itself (= part
    // stays). Pops of size <= keep_placeholders are left in place. Returns the number of people moved.
    std::vector<std::pair<long long, double>> targets;
    for (const auto &t : targets0)
        if (t.second > 0)
            targets.push_back(t);
    QList<long long> pids{source};
    for (const auto &t : targets)
        if (!pids.contains(t.first))
            pids << t.first;
    std::vector<double> w;
    for (const auto &t : targets)
        w.push_back(t.second);
    return edit(u"Split %1"_s.arg(source), pids, [&]() -> long long {
        long long moved = 0;
        std::vector<size_t> idx;
        if (const auto *l = pops.get(source))
            for (size_t i = 0; i < l->size(); ++i)
                if (flt.match((*l)[i]))
                    idx.push_back(i);
        for (size_t i : idx) {
            Pop &p0 = (*pops.get(source))[i];
            if (keep_placeholders && p0.size <= keep_placeholders)
                continue;
            const auto shares = distribute(p0.size, w);
            p0.size = 0;
            for (size_t k = 0; k < targets.size(); ++k) {
                Pop &p = (*pops.get(source))[i];
                if (targets[k].first == source) {
                    p.size += shares[k];
                } else {
                    const Pop tmpl = p;
                    _add_to(targets[k].first, tmpl, shares[k]);
                    moved += shares[k];
                }
            }
        }
        _prune({source});
        return moved;
    });
}

long long Document::lower_to_cap(const std::vector<std::tuple<QString, QList<long long>, double>> &todo,
                                 const QStringList &into)
{
    // In each group of provinces the pops of a type come down to `share` of the people there. Who is over becomes
    // the commonest working type of their province (same culture and religion). One undo step. -> people moved.
    QSet<long long> pset;
    for (const auto &[_t, ps, _s] : todo)
        for (long long p : ps)
            pset.insert(p);
    QList<long long> pids(pset.begin(), pset.end());
    std::sort(pids.begin(), pids.end());
    auto worker = [&](long long pid, const QString &not_type) -> QString {
        OrderedMap<QString, long long> sizes;
        for (const Pop &p : province_pops(pid))
            if (into.contains(p.type) && p.type != not_type)
                sizes[p.type] += p.size;
        if (!sizes.isEmpty()) {
            const QString *best = nullptr;
            long long n = 0;
            for (const auto &[t, s] : sizes)
                if (!best || s > n) {
                    best = &t;
                    n = s;
                }
            return *best;
        }
        for (const QString &t : into)
            if (t != not_type)
                return t;
        return u"farmers"_s;
    };
    QSet<QString> ks;
    for (const auto &[t, _ps, _s] : todo)
        ks.insert(t);
    QStringList kinds(ks.begin(), ks.end());
    std::sort(kinds.begin(), kinds.end());
    const QString label = u"Bring %1 down to the ceiling (%2 place%3)"_s.arg(kinds.join(u", "_s))
                              .arg(todo.size()).arg(plural(static_cast<long long>(todo.size())));
    return edit(label, pids, [&]() -> long long {
        long long moved = 0;
        for (const auto &[ptype, ps, share] : todo) {
            std::vector<std::pair<long long, size_t>> mine;
            std::vector<double> w;
            long long tot = 0, have = 0;
            for (long long pid : ps)
                if (const auto *l = pops.get(pid))
                    for (size_t i = 0; i < l->size(); ++i) {
                        tot += (*l)[i].size;
                        if ((*l)[i].type == ptype) {
                            mine.emplace_back(pid, i);
                            w.push_back(double((*l)[i].size));
                            have += (*l)[i].size;
                        }
                    }
            const long long over = have - py::trunc_int(share * double(tot));
            if (over <= 0 || mine.empty())
                continue;
            const auto takes = distribute(over, w);
            for (size_t j = 0; j < mine.size(); ++j) {
                const auto [pid, i] = mine[j];
                Pop &p = (*pops.get(pid))[i];
                const long long take = std::min(takes[j], p.size);
                if (take > 0) {
                    p.size -= take;
                    Pop tmpl = p;
                    tmpl.type = worker(pid, ptype);
                    _add_to(pid, tmpl, take);
                    moved += take;
                }
            }
        }
        _prune(pids);
        return moved;
    });
}

long long Document::consolidate(const QString &rid, const QSet<QString> &types, std::optional<long long> capital_,
                                bool same_owner)
{
    // Move all pops of `types` from the state's other provinces into its capital (only from provinces with the
    // capital's owner when `same_owner`).
    const std::vector<long long> provs = mod.regions.get(rid)->provinces;
    const long long cap = (capital_ && *capital_) ? *capital_ : capital(rid).value_or(0);
    QList<long long> others;
    for (long long pid : provs)
        if (pid != cap)
            others << pid;
    if (same_owner) {
        const OptStr own = prov_state(cap).owner;
        QList<long long> keep;
        for (long long pid : others)
            if (prov_state(pid).owner == own)
                keep << pid;
        others = keep;
    }
    return edit(u"Consolidate %1"_s.arg(rid), QList<long long>{cap} + others, [&]() -> long long {
        long long moved = 0;
        for (long long pid : others)
            if (auto *l = pops.get(pid))
                for (size_t i = 0; i < l->size(); ++i) {
                    Pop &p = (*pops.get(pid))[i];
                    if (types.contains(p.type) && p.size > 0) {
                        const Pop tmpl = p;
                        _add_to(cap, tmpl, tmpl.size);
                        moved += tmpl.size;
                        (*pops.get(pid))[i].size = 0;
                    }
                }
        _prune(others);
        return moved;
    });
}

std::pair<bool, QString> Document::utility_members(const QString &rid, const QList<long long> &pids_, bool add)
{
    // Add provinces to / take them out of a utility state (region.txt, written on Apply; one undo step).
    const Region *r = mod.utility.get(rid);
    if (!r)
        return {false, u"%1 is not a utility state"_s.arg(rid)};
    const QSet<long long> have(r->provinces.begin(), r->provinces.end());
    std::vector<long long> change;
    for (long long p : pids_)
        if (mod.provinces.contains(p) && (add ? !have.contains(p) : have.contains(p)))
            change.push_back(p);
    if (change.empty())
        return {false, u"Nothing to change"_s};
    edit(u"%1 %2 province(s) %3 %4"_s.arg(add ? u"Add"_s : u"Remove"_s).arg(change.size())
             .arg(add ? u"to"_s : u"from"_s, rid),
         {}, [&] {
             Region &rr = *mod.utility.get(rid);
             if (add) {
                 rr.provinces.insert(rr.provinces.end(), change.begin(), change.end());
             } else {
                 const QSet<long long> gone(change.begin(), change.end());
                 std::vector<long long> keep;
                 for (long long p : rr.provinces)
                     if (!gone.contains(p))
                         keep.push_back(p);
                 rr.provinces = keep;
             }
         }, {rid});
    return {true, u"%1 %2 province(s) %3 %4 — written to map/region.txt on Apply."_s.arg(add ? u"Added"_s : u"Removed"_s)
                      .arg(change.size()).arg(add ? u"to"_s : u"from"_s, rid)};
}

Region &Document::_reg(const QString &rid)
{
    // The state `rid` — made again (empty) when an Apply took it out of region.txt and an undo step needs it.
    if (Region *r = mod.regions.get(rid))
        return *r;
    if (Region *r = mod.utility.get(rid))
        return *r;
    const QString *ln = mod.loc.get(rid);
    mod.regions.insert(rid, Region{rid, (ln && !ln->isEmpty()) ? *ln : rid, {}});
    if (!region_orig.contains(rid))
        region_orig.insert(rid, {});
    return *mod.regions.get(rid);
}

std::pair<bool, QString> Document::delete_state(const QString &rid, const OptStr &target,
                                                const std::function<void()> &extra, const QStringList &lists)
{
    // Delete state `rid`: its provinces join state `target`; the emptied state is gone — its entry leaves region.txt
    // on Apply. `extra` runs in the same undo step. One undo step. Returns (ok, message).
    if (!mod.regions.contains(rid))
        return {false, u"Unknown state %1"_s.arg(rid)};
    const std::vector<long long> moving = mod.regions.get(rid)->provinces;
    if (!moving.empty() && (!target || *target == rid || !mod.regions.contains(*target)))
        return {false, u"Pick the state the provinces of %1 should join"_s.arg(rid)};
    std::vector<long long> joins;
    if (!moving.empty())
        for (long long p : moving) {
            const auto &tp = mod.regions.get(*target)->provinces;
            if (std::find(tp.begin(), tp.end(), p) == tp.end())
                joins.push_back(p);
        }
    const long long have = !moving.empty() ? static_cast<long long>(state_factories(*target).size()) : 0;
    long long bring = 0;
    for (long long p : joins)
        bring += static_cast<long long>(prov_state(p).factories.size());
    QStringList rids{rid};
    if (!moving.empty())
        rids << *target;
    edit(u"Delete state %1"_s.arg(rid), {}, [&] {
        if (!joins.empty()) {
            auto &tp = mod.regions.get(*target)->provinces;
            tp.insert(tp.end(), joins.begin(), joins.end());
        }
        mod.regions.get(rid)->provinces.clear();
        _sync_regions();
        if (extra)
            extra();
    }, rids, {}, lists);
    QString msg = !moving.empty()
                      ? u"State %1 deleted — its %2 province(s) joined %3."_s.arg(rid).arg(moving.size()).arg(*target)
                      : u"State %1 deleted."_s.arg(rid);
    msg += u" Its entry leaves map/region.txt on Apply."_s;
    if (have + bring > 8)
        msg += u" ⚠ %1 now has %2 factories — the %3 over the limit are marked red and will be removed on Apply "
               u"unless you remove others first."_s.arg(target.value_or(u"None"_s)).arg(have + bring).arg(have + bring - 8);
    return {true, msg};
}

void Document::_sync_regions()
{
    // Re-derive each province's .region from the (edited) region lists; bump the version.
    for (auto &kv : mod.provinces)
        kv.second.region = std::nullopt;
    for (const auto &[rid, r] : mod.regions)
        for (long long pid : r.provinces)
            if (auto *pr = mod.provinces.get(pid); pr && !pr->region)
                pr->region = rid;
    region_version += 1;
}

// the trailing digits of a key: re.match(r"^(.*?)(\d+)$") — \d is any Unicode digit; $ also before one final "\n"
static std::optional<std::pair<QString, QString>> prefix_digits(const QString &s0)
{
    QString s = s0.endsWith(u'\n') ? s0.chopped(1) : s0;
    if (s.contains(u'\n'))
        return std::nullopt;                    // ('.' does not cross a line break)
    qsizetype i = s.size();
    while (i > 0 && s[i - 1].category() == QChar::Number_DecimalDigit)
        --i;
    if (i == s.size())
        return std::nullopt;
    return std::make_pair(s.left(i), s.mid(i));
}

QString Document::new_state_key(const OptStr &like) const
{
    // Next free key in the style of `like` (e.g. STA_12 -> STA_512 if 511 is the highest).
    QString prefix = u"STA_"_s;
    if (like && !like->isEmpty())
        if (auto m = prefix_digits(*like))
            prefix = m->first;
    std::vector<long long> nums;
    for (const auto &kv : mod.regions)
        if (kv.first.startsWith(prefix))
            if (auto m = prefix_digits(kv.first.mid(prefix.size())); m && m->first.isEmpty())
                nums.push_back(py::to_int(m->second));
    return prefix + QString::number(nums.empty() ? 1 : *std::max_element(nums.begin(), nums.end()) + 1);
}

std::pair<bool, QString> Document::move_to_state(const QList<long long> &pids0, const QString &target, bool create)
{
    // Move provinces into state `target` (created when `create`). Returns (ok, message).
    QList<long long> pids;
    for (long long p : pids0)
        if (mod.provinces.contains(p))
            pids << p;
    if (create && !mod.regions.contains(target)) {
        mod.regions.insert(target, Region{target, target, {}});
        if (!region_orig.contains(target))
            region_orig.insert(target, {});
    }
    if (!mod.regions.contains(target))
        return {false, u"Unknown state %1"_s.arg(target)};
    QList<long long> moving;
    for (long long p : pids)
        if (mod.provinces.get(p)->region != OptStr(target))
            moving << p;
    if (moving.isEmpty())
        return {false, u"Already in %1"_s.arg(target)};
    const long long have = static_cast<long long>(state_factories(target).size());
    long long bring = 0;
    for (long long p : moving)
        bring += static_cast<long long>(prov_state(p).factories.size());
    QSet<QString> rs{target};
    for (long long p : moving)
        if (const OptStr &r = mod.provinces.get(p)->region; r && !r->isEmpty())
            rs.insert(*r);
    QStringList rids(rs.begin(), rs.end());
    std::sort(rids.begin(), rids.end());
    QList<long long> lost_capitals;
    for (long long p : moving)
        if (const OptStr &r = mod.provinces.get(p)->region; r && !r->isEmpty() && capital(*r) == p)
            lost_capitals << p;
    edit(u"Move to %1"_s.arg(target), {}, [&] {
        for (long long p : moving) {
            const OptStr old = mod.provinces.get(p)->region;
            if (old && !old->isEmpty()) {
                auto &v = mod.regions.get(*old)->provinces;
                v.erase(std::find(v.begin(), v.end(), p));
            }
            mod.regions.get(target)->provinces.push_back(p);
        }
        _sync_regions();
    }, rids);
    QString msg = u"Moved %1 province(s) to %2."_s.arg(moving.size()).arg(target);
    if (have + bring > 8)
        msg += u" ⚠ %1 now has %2 factories — the %3 over the limit are marked red and will be removed on Apply "
               u"unless you remove others first."_s.arg(target).arg(have + bring).arg(have + bring - 8);
    if (!lost_capitals.isEmpty()) {
        QStringList ls;
        for (long long p : lost_capitals)
            ls << QString::number(p);
        msg += u" Note: #%1 was a state capital; the next province of its old state is capital now."_s.arg(ls.join(u", #"_s));
    }
    return {true, msg};
}

void Document::set_capital(const QString &rid, long long pid)
{
    const auto &provs = mod.regions.get(rid)->provinces;
    if (std::find(provs.begin(), provs.end(), pid) == provs.end() || provs[0] == pid)
        return;
    edit(u"State capital of %1"_s.arg(rid), {}, [&] {
        auto &v = mod.regions.get(rid)->provinces;
        std::vector<long long> now{pid};
        for (long long x : v)
            if (x != pid)
                now.push_back(x);
        v = now;
        _sync_regions();
    }, {rid});
}

// ---------------------------------------------------------------------- saving
QString Document::default_file_for(long long pid) const
{
    // Pop file for a province that has no block yet: where most provinces of its owner live.
    const auto *pr = mod.provinces.get(pid);
    const OptStr owner = pr ? pr->owner : std::nullopt;
    if (owner && !owner->isEmpty()) {
        OrderedMap<QString, long long> c;
        for (const auto &[q, e] : src)
            if (const auto *qp = mod.provinces.get(q); qp && qp->owner == owner)
                c[e.file] += 1;
        if (!c.isEmpty()) {
            const QString *best = nullptr;
            long long n = -1;
            for (const auto &[f, cnt] : c)
                if (cnt > n) {
                    n = cnt;
                    best = &f;
                }
            return *best;
        }
    }
    if (!src.isEmpty()) {
        long long nearest = 0, d = -1;
        for (const auto &kv : src)
            if (d < 0 || std::llabs(kv.first - pid) < d) {
                d = std::llabs(kv.first - pid);
                nearest = kv.first;
            }
        return src.get(nearest)->file;
    }
    return u"new_pops.txt"_s;
}

std::pair<QStringList, OptStr> Document::save(const QString &backup_root)
{
    flush_live();
    Keep keep = _keep_undo();
    // new provinces first: their history files must exist before history edits are written into them
    const StoreP *defs = maps.get(u"definitions"_s);
    QSet<long long> gone;                        // provinces deleted: their pop blocks go (every start date)
    if (defs)
        for (long long p : (*defs)->deleted())
            gone.insert(p);
    auto &dpb = dynamic_cast<DropPopBlocks &>(**maps.get(u"drop_pop_blocks"_s));
    gone |= dpb.pids;                            // … and those of province numbers that don't exist
    QStringList written;
    if (defs)
        if (auto p = (*defs)->save(backup_root); p && !p->isEmpty()) {
            written += *p;
            // (C++ only, 2026-11-08) the mod's own definition.csv now differs from the game's: default.map must
            // name it with its folder, or the game builds its province picture from the game's rows (crash)
            if (maps.contains(u"default_map"_s)) {
                auto &dm = map<mapfiles::DefaultMapFile>(u"default_map"_s);
                if (const auto w = dm.definitions_wanted())
                    dm.new_definitions = *w;
            }
        }
    std::vector<long long> dirty;
    for (long long p : dirty_provinces())
        if (!gone.contains(p))
            dirty.push_back(p);
    OrderedMap<long long, QString> targets;
    for (long long pid : dirty)
        if (!src.contains(pid))
            targets.insert(pid, default_file_for(pid));
    auto [pw, bdir] = moddata::save_pops(mod, scenario, src, file_hashes, pops, dirty, targets, backup_root);
    written += pw;
    if (!gone.isEmpty()) {
        written += moddata::strip_pop_blocks(mod, gone, backup_root);
        for (long long pid : gone) {
            prov_orig.remove(pid);
            prov_want.remove(pid);
        }
        dpb.pids.clear();
    }
    const std::vector<long long> hdirty = dirty_histories();
    std::optional<std::pair<QString, QString>> hist_failed;
    if (!hdirty.empty()) {
        try {
            if (!history_editable())
                throw std::runtime_error("This scenario folder is not a date, so province history can't be saved.");
            OrderedMap<long long, ProvState> ch;
            for (long long pid : hdirty)
                ch.insert(pid, *prov_want.get(pid));
            written += hist->save(ch, *date, backup_root, scenario);
            prov_want.clear();
            prov_orig.clear();
            prov_touched.clear();
        } catch (const std::exception &e) {      // (stays pending; the other files go on)
            hist_failed = std::make_pair(u"province history"_s, QString::fromUtf8(e.what()));
        }
    }
    // every file (kind) on its own (the tester, 2026-10-17): what fails stays pending with its reason
    apply_failures.clear();
    for (const auto &[key, mf] : maps) {
        std::optional<QStringList> p;
        try {
            p = mf->save(backup_root);
        } catch (const std::exception &e) {
            apply_failures.emplace_back(key, QString::fromUtf8(e.what()));
            continue;
        }
        if (p && !p->isEmpty()) {
            written += *p;
            if (key == u"islands") {
                // history files got a line: cached states hold old file positions (factory sources)
                prov_want.clear();
                prov_orig.clear();
                prov_touched.clear();
            }
        }
    }
    if (hist_failed)
        apply_failures.insert(apply_failures.begin(), *hist_failed);
    const QStringList regs = dirty_regions();
    if (!regs.isEmpty()) {
        OrderedMap<QString, std::vector<long long>> changed_regs;
        for (const QString &rid : regs)
            changed_regs.insert(rid, mod.any_region(rid)->provinces);
        moddata::save_regions(mod, changed_regs, backup_root);
        written << u"map/region.txt"_s;
        for (const QString &rid : regs)
            region_orig.insert(rid, mod.any_region(rid)->provinces);
        dynamic_cast<DropStates &>(**maps.get(u"drop_states"_s)).rids.clear();
        QStringList emptied;
        for (const QString &rid : regs)
            if (const Region *r = mod.regions.get(rid); r && r->provinces.empty())
                emptied << rid;
        for (const QString &rid : emptied)       // emptied states are gone (undo can make them again: _reg)
            mod.regions.remove(rid);
        if (!emptied.isEmpty())
            _sync_regions();
    }
    regions_touched.clear();
    // Reload pop layout from disk so source spans are fresh
    moddata::PopLoad pl = moddata::load_pop_files(mod, scenario);
    src = std::move(pl.provinces);
    file_hashes = std::move(pl.files);
    problems = std::move(pl.problems);
    pops.clear();
    for (const auto &[pid, e] : src)
        pops.insert(pid, e.pops);
    touched.clear();
    _rebase_undo(keep);                          // (undo works past an Apply: the steps stay, true to the new files)
    _live_reset();
    QSet<long long> all;
    for (const auto &kv : pops)
        all.insert(kv.first);
    changed(all);
    return {written, bdir};
}

// ---------------------------------------------------------------------- _PendingScripts (its document half)
QString PendingScripts::text(const QString &rel) { return doc.map<scriptstore::ScriptStore>(u"scripts"_s).text(rel); }

void PendingScripts::put(const QString &label, const QString &rel, const QString &text,
                         const std::vector<std::pair<QString, QString>> &names_, const QString &section)
{
    auto &store = doc.map<scriptstore::ScriptStore>(u"scripts"_s);
    localisation::LocEdits *loc = doc.maps.contains(u"loc"_s) ? &doc.map<localisation::LocEdits>(u"loc"_s) : nullptr;
    doc.edit(label, {}, [&] {
        store.set_text(rel, text);
        for (const auto &[k, v] : names_)
            if (loc && !v.isEmpty()) {
                loc->set(k, v, section);
                if (on_name)
                    on_name(k);
            }
    }, {}, {}, {u"scripts"_s, u"loc"_s});
}

void PendingScripts::names(const QString &label, const OrderedMap<QString, QString> &names_, const QString &section)
{
    if (!doc.maps.contains(u"loc"_s))
        return;
    auto &loc = doc.map<localisation::LocEdits>(u"loc"_s);
    if (on_name)
        for (const auto &kv : names_)
            on_name(kv.first);
    doc.edit(label, {}, [&] {
        for (const auto &[k, v] : names_)
            loc.set(k, v, section);
    }, {}, {}, {u"loc"_s});
}

}  // namespace document
