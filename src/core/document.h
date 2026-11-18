// document.py — In-memory scenario being edited: pops per province, state capitals, undo/redo and pop operations.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace adjacency {
class AdjacencyFile;
}
namespace positions {
class PositionsFile;
}

namespace document {

using moddata::Pop;
using history::ProvState;
using OptStr = std::optional<QString>;
using store::Snap;
using store::StoreP;

extern const int UNDO_STEPS;      // undo steps kept (the oldest falls off — said in the Undo button's tooltip)
extern const double LIVE_MERGE;   // s: keystrokes in one file closer than this are one undo step

std::vector<long long> distribute(long long total, const std::vector<double> &weights);
inline Pop copy_pop(const Pop &p) { return p; }      // "o" (source info) is shared on purpose
using PopKey = std::tuple<QString, OptStr, OptStr>;
inline PopKey pop_key(const Pop &p) { return {p.type, p.culture, p.religion}; }

class PopFilter {
    // Selected pop = type is checked AND culture is checked. `types` / `cultures` are allow-lists (None = all);
    // `no_types` / `no_cultures` are exclusions (what the UI uses).
public:
    PopFilter(std::optional<QSet<QString>> types = std::nullopt, std::optional<QSet<OptStr>> cultures = std::nullopt,
              QSet<QString> no_types = {}, QSet<OptStr> no_cultures = {}, QSet<OptStr> no_religions = {})
        : types(std::move(types)), cultures(std::move(cultures)), no_types(std::move(no_types)),
          no_cultures(std::move(no_cultures)), no_religions(std::move(no_religions))
    {
    }
    std::optional<QSet<QString>> types;
    std::optional<QSet<OptStr>> cultures;
    QSet<QString> no_types;
    QSet<OptStr> no_cultures, no_religions;
    bool match(const Pop &p) const;
};

class Document;

class UtilityMarks : public store::Store {   // doc.maps["utility"]
public:
    UtilityMarks(moddata::Mod &mod, Document &doc);
    moddata::Mod &mod;
    Document &doc;
    QSet<QString> saved;                       // the marks as they are kept
    void set(const QStringList &rids, bool on);
    Snap snapshot() const override;
    void restore(const Snap &snap) override;
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

class DropStates : public store::Store {     // doc.maps["drop_states"]: only the undo record
public:
    QSet<QString> rids;
    Snap snapshot() const override { return store::box(rids); }
    void restore(const Snap &s) override { rids = store::unbox<QSet<QString>>(s); }
    QStringList dirty() const override { return {}; }        // counted with the states (dirty_regions)
    std::optional<QStringList> save(const QString &) override { return std::nullopt; }
    bool undo_after_apply() const override { return true; }
};

class DropPopBlocks : public store::Store {  // popcheck.py: doc.maps["drop_pop_blocks"]
public:
    QSet<long long> pids;
    Snap snapshot() const override { return store::box(pids); }
    void restore(const Snap &s) override { pids = store::unbox<QSet<long long>>(s); }
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &) override { return std::nullopt; }
    bool undo_after_apply() const override { return true; }
};

// one picture of what a step touches (Python: the 4-tuple of _snapshot)
struct Picture {
    OrderedMap<long long, std::vector<Pop>> pops;
    OrderedMap<QString, std::vector<long long>> regs;
    OrderedMap<long long, std::optional<ProvState>> provs;
    OrderedMap<QString, Snap> lists;
    bool operator==(const Picture &o) const;
};
struct Step {
    QString label;
    Picture before, after;
};
using StepP = std::shared_ptr<Step>;          // (identity matters: the live typing step is found by it)

struct StateKey {                              // Document._state_key
    OptStr owner, controller;
    QStringList cores;
    std::optional<long long> life_rating;
    OptStr trade_goods;
    long long colonial, fort, naval_base, railroad;
    std::vector<std::tuple<QString, long long, bool, QString, long long, long long>> factories;
    std::map<QString, long long> more;          // (C++ only: the mod's other province buildings)
    bool operator==(const StateKey &o) const = default;
};

struct FactoryProblem {
    QString rid;
    long long pid;
    history::Factory f;
    QString reason;
};

class Document {
public:
    Document(moddata::Mod &mod, const QString &scenario);
    moddata::Mod &mod;
    QString scenario;
    OrderedMap<long long, moddata::PopProvince> src;
    OrderedMap<QString, QString> file_hashes;
    QStringList problems;
    OrderedMap<long long, std::vector<Pop>> pops;
    OrderedMap<QString, std::vector<long long>> region_orig;
    std::optional<history::Date> date;
    std::shared_ptr<history::ProvinceHistory> hist;
    OrderedMap<long long, ProvState> prov_orig;   // pid -> state at the scenario date as on disk (cached)
    OrderedMap<long long, ProvState> prov_want;   // pid -> edited state (only provinces changed in this session)
    QSet<long long> prov_touched;
    OrderedMap<QString, StoreP> maps;
    QSet<long long> touched;
    QSet<QString> regions_touched;
    std::vector<StepP> undo_stack, redo_stack;
    std::vector<std::function<void(const QSet<long long> &)>> listeners;
    std::pair<long long, QStringList> undo_lost{0, {}};
    std::vector<std::pair<QString, QString>> apply_failures;
    long long region_version = 0;
    std::optional<std::vector<bool>> coastal;      // per province (set by the main window)
    std::optional<QString> last_error;             // the last failed Apply (set by the main window; Errors tab)

    template <typename T>
    T &map(const QString &kind)                    // the ported class behind a store: doc.map<MembershipFile>("climate")
    {
        StoreP s = *maps.get(kind);
        if (auto *w = dynamic_cast<store::StoreOf<T> *>(s.get()))
            return w->obj;
        return dynamic_cast<T &>(*s);
    }

    // ---- queries
    const std::vector<Pop> &province_pops(long long pid) const;
    long long total(long long pid) const;
    const moddata::Region *region(const QString &rid) const;
    std::optional<long long> capital(const QString &rid) const;
    bool is_dirty(long long pid) const;
    std::vector<long long> dirty_provinces() const;
    QStringList dirty_regions() const;
    QStringList empty_states_on_disk() const;
    QStringList drop_empty_states(const QStringList &rids);
    long long pending_count() const;
    void set_definitions(const QString &kind, const OrderedMap<QString, std::vector<std::pair<QString, QString>>> &mods,
                         const QStringList &new_names, const QStringList &removed = {},
                         const OrderedMap<QString, QString> &moves = {});
    void adj_edit(const QString &label, const std::function<void(adjacency::AdjacencyFile &)> &fn);
    void pos_hist_edit(const QString &label, const QList<long long> &hpids,
                       const std::function<void(positions::PositionsFile &)> &fn);
    void pos_edit(const QString &label, const std::function<void(positions::PositionsFile &)> &fn);
    void set_membership(const QString &kind, const QList<long long> &pids, const QString &name);

    // ---- province history
    bool history_editable() const { return date.has_value(); }
    const ProvState &prov_state(long long pid);
    // C++ only (2026-11-16): the mod's other province buildings changed (Building types): the history reads their
    // levels from now on; edited provinces take the levels their files give for a building new to the list
    void levels_changed(const QStringList &more);
    const ProvState &_orig_state(long long pid);
    static StateKey _state_key(const ProvState &st);
    std::vector<long long> dirty_histories();
    void edit_provinces(const QString &label, const QList<long long> &pids,
                        const std::function<void(long long, ProvState &)> &fn);
    bool state_is_coastal(const QString &rid) const;
    QStringList session_rids() const;
    std::vector<FactoryProblem> factory_problems(const std::optional<QStringList> &rids = std::nullopt);
    void remove_factories(const std::vector<FactoryProblem> &problems);
    std::vector<std::pair<long long, history::Factory>> state_factories(const QString &rid);

    // ---- undo
    Picture _snapshot(const QSet<long long> &pids, const QStringList &rids, const QSet<long long> &hpids,
                      const QStringList &lists);
    std::vector<Pop> _relink_pops(long long pid, std::vector<Pop> pops) const;
    ProvState _reanchor(long long pid, ProvState st);
    static const store::Items *_items(const Snap &snap);
    Snap _part(const store::Store &mf, const Snap &target, const Snap &other) const;
    void _restore(const Picture &snap, const Picture *other = nullptr);
    struct Keep {
        OrderedMap<long long, ProvState> provs;
        OrderedMap<QString, Snap> tokens;
        QSet<QString> lost;
    };
    Keep _keep_undo();
    void _rebase_undo(const Keep &keep);

    template <typename F>
    auto edit(const QString &label, const QList<long long> &pids, F fn, const QStringList &rids = {},
              const QList<long long> &hpids = {}, const QStringList &lists = {}) -> decltype(fn())
    {
        // Run fn() as one undoable step touching provinces `pids` (pops), regions `rids` and histories `hpids`.
        const QSet<long long> pset(pids.begin(), pids.end()), hset(hpids.begin(), hpids.end());
        flush_live();                            // typing not yet a step becomes one first (its own)
        Picture before = _snapshot(pset, rids, hset, lists);
        auto run = [&]() {                       // (busy only while fn runs: Python's try / finally)
            _busy += 1;
            struct Done {
                int &b;
                ~Done() { b -= 1; }
            } done{_busy};
            return fn();
        };
        if constexpr (std::is_void_v<decltype(fn())>) {
            run();
            _after_edit(label, pset, rids, hset, lists, std::move(before));
        } else {
            auto result = run();
            _after_edit(label, pset, rids, hset, lists, std::move(before));
            return result;
        }
    }
    void _after_edit(const QString &label, const QSet<long long> &pids, const QStringList &rids,
                     const QSet<long long> &hpids, const QStringList &lists, Picture before);
    std::optional<QString> undo();
    std::optional<QString> redo();
    // (C++ only) the steps made after `mark` (the last step before them, or nullptr) become ONE undo step: per kept
    // key the first step's "before" and the last step's "after" (several edits that belong together, e.g. a pasted
    // copy of provinces: new provinces, their pixels, pops, positions, states)
    StepP last_step() const { return undo_stack.empty() ? nullptr : undo_stack.back(); }
    void merge_steps(const StepP &mark, const QString &label);

    // ---- live texts (typing) as undo steps
    void track_live(const QString &kind);
    void _live_reset();
    bool record_live(const QString &kind, const OptStr &key, const QString &label);
    void flush_live();
    bool busy() const { return _busy != 0; }      // (Python reads doc._busy)
    std::pair<long long, QStringList> step_scope(const Step &step) const;
    void changed(const QSet<long long> &pids);

    // ---- operations
    std::vector<Pop *> selected_pops(const QList<long long> &pids, const PopFilter &flt);
    void _prune(const QList<long long> &pids);
    long long scale(const QList<long long> &pids, const PopFilter &flt, std::optional<double> percent = std::nullopt,
                    std::optional<long long> new_total = std::nullopt);
    void _add_to(long long pid, const Pop &tmpl, long long amount);
    long long add_pop(const QList<long long> &pids, const QString &ptype, const OptStr &culture,
                      const OptStr &religion, long long size);
    long long paste(const QList<long long> &pids, const std::vector<Pop> &tmpl, bool replace = false,
                    double factor = 1.0, std::optional<long long> total = std::nullopt);
    long long remix(const QList<long long> &pids, const PopFilter &flt, const QString &field,
                    const OrderedMap<OptStr, long long> &targets);
    long long pop_change(const QList<long long> &pids, const PopFilter &flt, std::optional<long long> new_total,
                         const QString &field, const std::vector<std::pair<OptStr, double>> &shares);
    long long _scale_run(const QList<long long> &pids, const PopFilter &flt, std::optional<double> percent,
                         std::optional<long long> new_total);
    long long _remix_run(const QList<long long> &pids, const PopFilter &flt, const QString &field,
                         const OrderedMap<OptStr, long long> &targets);
    long long split(long long source, const std::vector<std::pair<long long, double>> &targets, const PopFilter &flt,
                    long long keep_placeholders = 0);
    long long lower_to_cap(const std::vector<std::tuple<QString, QList<long long>, double>> &todo,
                           const QStringList &into = {QStringLiteral("farmers"), QStringLiteral("labourers"),
                                                      QStringLiteral("craftsmen")});
    long long consolidate(const QString &rid, const QSet<QString> &types, std::optional<long long> capital = std::nullopt,
                          bool same_owner = false);
    std::pair<bool, QString> utility_members(const QString &rid, const QList<long long> &pids, bool add);
    moddata::Region &_reg(const QString &rid);
    std::pair<bool, QString> delete_state(const QString &rid, const OptStr &target = std::nullopt,
                                          const std::function<void()> &extra = nullptr, const QStringList &lists = {});
    void _sync_regions();
    QString new_state_key(const OptStr &like = std::nullopt) const;
    std::pair<bool, QString> move_to_state(const QList<long long> &pids, const QString &target, bool create = false);
    void set_capital(const QString &rid, long long pid);

    // ---- saving
    QString default_file_for(long long pid) const;
    std::pair<QStringList, OptStr> save(const QString &backup_root);

private:
    int _busy = 0;                                // inside edit() / undo / redo: a live kind's change is no typing
    QStringList _live_kinds;                      // kinds that change live (typing) — kept in the order they came
    OrderedMap<QString, Snap> _live_base;         // kind -> its picture at the last step
    struct LiveOpen {
        QString kind;
        OptStr key;
        double t;
        StepP step;
    };
    std::optional<LiveOpen> _live_open;           // the typing step that is still growing
    OrderedMap<QString, OptStr> _live_sig;        // kind -> its cheap change mark at the last look
};

// mainwindow.py's _PendingScripts, its document half: what a dialog used to write at once (a new culture /
// religion) becomes a pending change of the document — one undo step (scripts + loc), written on Apply.
class PendingScripts : public moddata::PendingScript {
public:
    explicit PendingScripts(Document &doc) : doc(doc) {}
    Document &doc;
    std::function<void(const QString &)> on_name;     // (the window: which page a name belongs to — its _loc_tab)
    std::function<void()> on_sync;                     // (the window: importer::sync_cultures — map\, not core\)
    QString text(const QString &rel) override;
    void put(const QString &label, const QString &rel, const QString &text,
             const std::vector<std::pair<QString, QString>> &names, const QString &section) override;
    void names(const QString &label, const OrderedMap<QString, QString> &names, const QString &section) override;
    void sync() override
    {
        if (on_sync)
            on_sync();
    }
};

}  // namespace document

