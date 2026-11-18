// events.py — Events (events/*.txt) and decisions (decisions/*.txt): read them, tell which nations (provinces, for
// province events) can have them at the scenario's start, how likely within 10 years, and list their effects
// readably.
//
// An event happens by itself when its trigger holds, on average after its mean time to happen (MTTH: after that time
// half the nations it could happen to have had it — the chance to have it within t days is 1 − ½^(t / MTTH)); its
// MTTH modifiers multiply the time while their conditions hold. An event with `is_triggered_only` happens only when
// something fires it (another event, a decision, common/on_actions.txt). A decision is taken by the nation when it is
// shown (potential) and allowed (allow).
//
// Conditions are judged from the start: the nation's history at the scenario's date, who owns which province. Each
// answer is yes / no / can't tell (None) — flags, modifiers and variables the mod never sets anywhere are known to be
// off; ones it sets, the AI, wars later on and the like can't be told from the start. `year` is checked for each of
// the 10 years.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace scriptstore {
class ScriptStore;
}
namespace document {
class Document;
}
namespace countryhist {
class NationEdits;
struct NationState;
}
namespace diplomacy {
class DiploEdits;
}

namespace events {

using script::Node;
using script::Nodes;
using OptStr = std::optional<QString>;
using Tri = std::optional<bool>;             // True / False / None (can't tell)
using NodeList = std::vector<const Node *>;  // a list of nodes (a block's children, or some of them)
using Span = std::pair<qsizetype, qsizetype>;

extern const int YEARS;
extern const double LN2;
extern const QStringList BLOCKS;

std::optional<double> _num(const OptStr &v);                     // float(v), None on TypeError / ValueError
double _num(const OptStr &v, double dflt);
bool yes(const OptStr &v);                                       // str(v).strip().lower() in ("yes", "true", "1")
Tri not3(Tri v);

// a view over a list of nodes: a block's children (Nodes) or a picked list (NodeList); `nodes or ()` when empty
class View {
public:
    View() = default;
    View(const Nodes &n) : a_(&n) {}
    View(const NodeList &l) : b_(&l) {}
    View(const std::optional<NodeList> &l) : b_(l ? &*l : nullptr) {}
    size_t size() const { return a_ ? a_->size() : b_ ? b_->size() : 0; }
    const Node &operator[](size_t i) const { return a_ ? (*a_)[i] : *(*b_)[i]; }
    bool empty() const { return size() == 0; }

private:
    const Nodes *a_ = nullptr;
    const NodeList *b_ = nullptr;
};

// ---------------------------------------------------------------------------------------------- reading
struct Option {
    QString name;
    NodeList effects;
    std::optional<NodeList> ai_chance;
};

struct Entry {                               // one event or decision
    QString kind;                            // country_event / province_event / decision
    QString key;
    OptStr id;
    QString file;
    Span span;
    QString title, desc, picture;
    const Node *node = nullptr;
    std::shared_ptr<const Nodes> tree;       // (keeps the file's parse alive)
    std::optional<NodeList> trigger, potential, allow, effect, ai_will_do, immediate;
    std::optional<double> mtth;              // base days
    QString unit;
    std::vector<std::pair<double, NodeList>> mods;   // [(factor, [condition nodes])]
    bool once = false, triggered_only = false, major = false, self_limit = false;
    std::vector<Option> options;             // [(name key, [effect nodes], ai_chance node)]
    QString text;
    OptStr uid, rel;
    QStringList cats;
    bool is_event() const { return kind != u"decision"; }
    bool province() const { return kind == u"province_event"; }
};
using EntryP = std::shared_ptr<Entry>;

QSet<QString> _tokens(View nodes);
void _tokens(View nodes, QSet<QString> &out);
extern const QSet<QString> CHANGE_KEYS;
QSet<QString> _changes(View nodes);
void _changes(View nodes, QSet<QString> &out);

// ---------------------------------------------------------------------------------------------- categories
QStringList split_cats(const OptStr &s);
qsizetype _line_start(const QString &text, qsizetype pos);
struct CatLine {
    qsizetype start, end;
    QStringList names;
};
std::optional<CatLine> cat_line_above(const QString &text, qsizetype pos);
std::optional<CatLine> file_cat_line(const QString &text);
struct Block {                               // iter_blocks: (uid, node, kind)
    QString uid;
    const Node *node;
    QString kind;
};
std::vector<Block> iter_blocks(const Nodes &nodes, const QString &folder);
QString set_cats(const QString &text, const QString &folder, const OptStr &uid, const QStringList &names);

extern const QString LIT_LINE;
bool lit_re_match(const QString &line);      // LIT_RE.match
std::optional<Span> lit_line_above(const QString &text, qsizetype pos);
QString set_lit_line(const QString &text, const QString &folder, const QString &uid, bool on);
QSet<std::pair<QString, QString>> lit_marked(const QStringList &files, const std::function<QString(const QString &)> &text_of);

class EventDB;

class EventCats : public store::Store {      // doc.maps["event_cats"]: the user's categories (the files' comments)
public:
    static constexpr const char *kind = "event_cats";
    EventCats(moddata::Mod &mod, scriptstore::ScriptStore &store) : mod(mod), store(store) {}
    moddata::Mod &mod;
    scriptstore::ScriptStore &store;
    EventDB *db = nullptr;
    void attach(EventDB *d) { db = d; }
    using CatKey = std::pair<QString, OptStr>;   // (rel file, uid) — (rel file, None) for a file
    QStringList get(const CatKey &key) const;
    void set(const CatKey &key, const QStringList &names);
    QStringList all_names() const;
    // the texts are doc.maps["scripts"]: nothing of its own to undo / count / write
    store::Snap snapshot() const override { return store::box(0); }
    void restore(const store::Snap &) override {}
    QStringList dirty() const override { return {}; }
    std::optional<QStringList> save(const QString &) override { return std::nullopt; }
    bool undo_after_apply() const override { return true; }
};

std::optional<NodeList> _block(View nodes, const QString &key);
OptStr _val(View nodes, const QString &key);

extern const QString NO_ID;                  // fired_by's key for an event without an id (Python: None)

struct Fired {
    Entry *src;                              // None: on_actions, an election, the history
    QString how;
};

class EventDB {
public:
    // store: the editor's texts (doc.maps["scripts"]); without it the files on disk. prev: the EventDB before —
    // files whose text didn't change keep their parse.
    EventDB(moddata::Mod &mod, scriptstore::ScriptStore *store = nullptr, const EventDB *prev = nullptr);
    moddata::Mod &mod;
    scriptstore::ScriptStore *store;
    OrderedMap<QString, std::shared_ptr<const Nodes>> trees;   // rel -> parse of the file
    OrderedMap<QString, QString> texts;                        // rel -> the text that was parsed
    OrderedMap<std::pair<QString, QString>, Entry *> by_uid;   // (rel, uid) -> Entry
    std::vector<EntryP> entries;
    OrderedMap<QString, Entry *> by_id;                        // event id -> Entry
    OrderedMap<QString, std::vector<Fired>> fired_by;          // event id -> [(Entry or None, how)]
    QSet<QString> set_flags, set_gflags, set_mods, set_pmods, set_vars;
    OrderedMap<QString, QStringList> file_cats;                // "events/<file>" -> [categories] (the file's own)
    QStringList files;                                         // "events/<file>", "decisions/<file>" in order
    QStringList errors;
    // (Python's id(db) for "the same one?": an address can be used again once a db is gone — a number never is)
    long long serial = next_serial();
    static long long next_serial()
    {
        static std::atomic<long long> n{0};
        return ++n;
    }

    QStringList _files(const QString &folder) const;
    std::pair<QString, std::shared_ptr<const Nodes>> _read(const QString &rel, const QString &path);
    void _load();
    void _scan(View nodes, Entry *src);
    void _scan_history();
    void _scan_history_files();
    Entry *entry_at(const QString &rel, qsizetype pos);
    using Holders = OrderedMap<std::pair<QString, QString>, QSet<store::Key>>;
    const Holders &history_holders();
    QString name(const Entry &e) const;
    QString description(const Entry &e) const;

private:
    const EventDB *_prev;
    std::optional<QHash<QString, std::vector<Entry *>>> _at;
};

// glob.glob(os.path.join(d, "**", "*.txt"), recursive=True), in its order
QStringList glob_txt_recursive(const QString &d);

// ---------------------------------------------------------------------------------------------- the start
class World {                                // who owns what, each nation's history state, great powers, diplomacy
public:
    World(moddata::Mod &mod, document::Document &doc);
    moddata::Mod &mod;
    document::Document &doc;
    long long start_year;
    OrderedMap<long long, QString> owner;
    OrderedMap<QString, QList<long long>> owned;
    countryhist::NationEdits *nations;
    QStringList tags;
    QSet<QString> gps;
    QHash<long long, QString> cont;
    diplomacy::DiploEdits *dip = nullptr;
    QHash<QString, QString> culture_group;
    QHash<std::pair<QString, QString>, QString> party_ideology;
    OrderedMap<QString, QString> reform_issue;
    QSet<QString> issues, techs, inventions;
    const countryhist::NationState *st(const OptStr &tag);   // nullptr: None
    const history::ProvState &pst(long long pid);

private:
    OrderedMap<QString, std::shared_ptr<countryhist::NationState>> _state;
    std::shared_ptr<countryhist::NationState> _none_state;
};

// ---------------------------------------------------------------------------------------------- conditions
extern const QSet<QString> COUNTRY_SCALARS;

struct Scope {                               // ("c", tag) or ("p", pid); either may be None
    QChar kind;
    OptStr tag;
    std::optional<long long> pid;
    static Scope c(const OptStr &t) { return Scope{u'c', t, std::nullopt}; }
    static Scope p(std::optional<long long> p) { return Scope{u'p', std::nullopt, p}; }
};

class Judge {                                // cond(nodes, scope, year) -> True / False / None
public:
    Judge(World &world, EventDB &db) : w(world), db(db) {}
    virtual ~Judge() = default;
    World &w;
    EventDB &db;
    OrderedMap<QString, long long> unknown;  // condition key -> how often it couldn't be told
    Tri all(View nodes, const Scope &sc, const Scope &self, std::optional<long long> year);
    Tri one(const Node &c, const Scope &sc, const Scope &self, std::optional<long long> year);
    virtual Tri scope(const Node &c, const Scope &sc, const Scope &self, std::optional<long long> year);
    std::optional<long long> _capital(const OptStr &tag);
    OptStr _tag(const QString &v, const Scope &self);
    virtual Tri country(const QString &k, const QString &v, const OptStr &tag, const Scope &self,
                        std::optional<long long> year);
    virtual Tri prov(const QString &k, const QString &v, std::optional<long long> pid, const Scope &self,
                     std::optional<long long> year);
};

extern const QSet<QString> POT_COUNTRY, POT_PROV;

class PotentialJudge : public Judge {        // could the entry ever happen here? (who / where alone)
public:
    using Judge::Judge;
    Tri country(const QString &k, const QString &v, const OptStr &tag, const Scope &self,
                std::optional<long long> year) override;
    Tri prov(const QString &k, const QString &v, std::optional<long long> pid, const Scope &self,
             std::optional<long long> year) override;
};

Tri potential(Judge &pjudge, const Entry &e, const Scope &scope);

// ---------------------------------------------------------------------------------------------- chances
struct Chance {
    Tri can = false;
    std::vector<Tri> years;
    std::optional<std::pair<double, double>> mtth;
    std::pair<double, double> p{0.0, 0.0}, times{0.0, 0.0};
    Tri allow;
};
bool _has_year(View nodes);
Chance chance(Judge &judge, const Entry &e, const Scope &scope);

extern const OrderedMap<QString, QString> GROUP_KEYS;
std::pair<QSet<QString>, QStringList> restrict_of(View nodes);
bool player_only(View nodes);
struct Class {                               // ("tags", {tags}) / ("player", None) / ("group", [kinds]) / ("universal", None)
    QString kind;
    QSet<QString> tags;
    QStringList group;
};
Class classify(const Entry &e);
std::vector<Scope> scopes_of(const World &world, const Entry &e);

// ---------------------------------------------------------------------------------------------- effects, readable
extern const QHash<QString, QString> NICE;
QString pretty_value(const moddata::Mod &mod, const QString &key, const QString &v);
struct Line {                                // (depth, text, link event id or None)
    long long depth;
    QString text;
    OptStr link;
};
void effect_lines(const EventDB &db, View nodes, long long depth, std::vector<Line> &out);
std::vector<Line> effect_lines(const EventDB &db, View nodes);
struct CondLine {                            // (depth, text, state) — state "" (no scope given): has_state false
    long long depth;
    QString text;
    bool has_state;
    Tri state;
};
void cond_lines(const EventDB &db, Judge *judge, View nodes, const std::optional<Scope> &scope,
                std::optional<long long> year, long long depth, std::vector<CondLine> &out);
std::vector<CondLine> cond_lines(const EventDB &db, Judge *judge, View nodes,
                                 const std::optional<Scope> &scope = std::nullopt,
                                 std::optional<long long> year = std::nullopt);
QString fmt_days(const std::optional<double> &d);

}  // namespace events
