// history.py — Province history (history/provinces/*/*.txt): read state at a date, write edits into a dated block.
// Edits for a scenario are written into a `<scenario date> = { ... }` block of the province's main history file
// (created when missing). The rest of the file is left untouched.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace history {

using Date = std::tuple<long long, long long, long long>;
using OptStr = std::optional<QString>;
using Span = std::pair<qsizetype, qsizetype>;

extern const QStringList SCALAR_KEYS;
extern const QString ISLAND_FLAG;              // written as set_province_flag = island
extern const QStringList ISLAND_NAMES;         // read as the same flag (e.g. typed by hand)
extern const QStringList LEVEL_KEYS;
extern const QSet<QString> MANAGED;

QString date_key(const Date &d);
std::optional<Date> start_date(const moddata::Mod &mod);

struct FactorySrc {                             // f["src"]: where a factory stands on disk (identity matters)
    QString path;
    Span span;
    std::optional<Span> level_span;
    std::optional<Date> date;
    long long level = 1;
    bool operator==(const FactorySrc &o) const = default;
};
struct Factory {
    OptStr building;
    long long level = 1;
    bool upgrade = true;
    std::shared_ptr<const FactorySrc> src;      // nullptr: made in the editor
    bool operator==(const Factory &o) const
    {
        // (as Python's dict ==: the source compared by what it says, not by being the same object)
        return building == o.building && level == o.level && upgrade == o.upgrade &&
               (src == o.src || (src && o.src && *src == *o.src));
    }
};
struct ProvState {                              // empty_state() / state_at(): one province's history at a date
    OptStr owner, controller;
    QStringList cores;
    std::optional<long long> life_rating;
    OptStr trade_goods;
    long long colonial = 0, fort = 0, naval_base = 0, railroad = 0;
    std::vector<Factory> factories;
    QStringList flags;
    // C++ only (the user 2026-11-16: new province buildings): the levels of the mod's OTHER province buildings
    // (buildings.txt, province = yes), by key — only levels above 0 are kept, so two states compare alike
    std::map<QString, long long> more;
    long long level_of(const QString &k) const  // want[k] for colonial / fort / naval_base / railroad / the others
    {
        if (k == u"colonial")
            return colonial;
        if (k == u"fort")
            return fort;
        if (k == u"naval_base")
            return naval_base;
        if (k == u"railroad")
            return railroad;
        const auto it = more.find(k);
        return it == more.end() ? 0 : it->second;
    }
    void set_level(const QString &k, long long v)
    {
        if (k == u"colonial")
            colonial = v;
        else if (k == u"fort")
            fort = v;
        else if (k == u"naval_base")
            naval_base = v;
        else if (k == u"railroad")
            railroad = v;
        else if (v)
            more[k] = v;
        else
            more.erase(k);
    }
    bool operator==(const ProvState &o) const = default;
};
ProvState empty_state();

struct PlainKey {                               // ProvinceHistory.plain_key: what a state says, not where it stands
    OptStr owner, controller;
    QStringList cores;                          // sorted
    std::optional<long long> life_rating;
    OptStr trade_goods;
    long long colonial, fort, naval_base, railroad;
    std::vector<std::tuple<QString, long long, bool>> factories;   // sorted
    std::map<QString, long long> more;
    bool operator==(const PlainKey &o) const = default;
};

class HistFile {
public:
    HistFile(const QString &rel, const QString &path, const std::optional<QString> &text = std::nullopt);
    void _set(const QString &text);
    void reload();
    QString rel, path;
    bool pending = false;                       // made in the editor (a new province): not on disk until Apply
    bool base = false;                          // read from the base game (written into the mod when it changes)
    QString text, hash;
    script::Nodes nodes;
};
using HistFileP = std::shared_ptr<HistFile>;

struct DatedBlock {                             // dated_blocks(): (date, file rel path, key/value lines)
    Date date;
    QString rel;
    std::vector<std::pair<OptStr, QString>> lines;
};

class ProvinceHistory {
public:
    explicit ProvinceHistory(moddata::Mod &mod);
    moddata::Mod &mod;
    OrderedMap<long long, std::vector<HistFileP>> files;   // pid -> [HistFile] in load order
    QString hdir;
    long long from_base = 0;    // files the base game gives (the mod has none of that name and keeps "history")
    // C++ only (2026-11-16): the mod's province buildings besides fort / naval_base / railroad (buildings.txt order) —
    // read and written in the history files like those three
    QStringList more;
    QStringList levels() const;                 // colonial, fort, naval_base, railroad, then `more`
    bool managed(const QString &key) const;     // MANAGED, or one of `more`

    const std::optional<Date> &start();
    void _top_edits(const HistFile &hf, const QString &text, const ProvState &want, const QString &nl,
                    std::vector<std::tuple<qsizetype, qsizetype, QString>> &out);
    HistFileP main_file(long long pid) const;
    void add_virtual(long long pid, const QString &rel, const QString &text);
    void remove_virtual(long long pid);
    void _apply(ProvState &st, const std::vector<const script::Node *> &nodes, const HistFile &hf,
                const std::optional<Date> &date) const;
    ProvState state_at(long long pid, const std::optional<Date> &date, bool include_date = true) const;
    std::vector<DatedBlock> dated_blocks(long long pid) const;
    static QString _nl(const QString &text);
    static Span _line_span(const QString &text, qsizetype s, qsizetype e);
    static void _owner_on_top(const HistFile &hf, const QString &text, const ProvState &want,
                              std::vector<std::tuple<qsizetype, qsizetype, QString>> &out, const QString &nl);
    static PlainKey plain_key(const ProvState &st);
    QStringList save(const OrderedMap<long long, ProvState> &changes, const Date &date, const QString &backup_root,
                     const QString &scenario);

private:
    std::optional<std::optional<Date>> _start;
    struct Was {
        Date date;
        QString before, after;
        OrderedMap<long long, PlainKey> states;
    };
    OrderedMap<QString, Was> _was;              // path -> the last Apply's text before / after (undo to the letter)
};

bool is_island_flag(const script::Node &n);
std::vector<const script::Node *> island_flag_nodes(const script::Nodes &nodes);

class IslandFlags {                             // doc.maps["islands"]
public:
    static constexpr bool undo_after_apply = true;
    explicit IslandFlags(ProvinceHistory &hist);
    ProvinceHistory &hist;
    QSet<long long> orig, marked;
    QSet<long long> _scan() const;
    void set(const QList<long long> &pids, bool on);
    QSet<long long> snapshot() const { return marked; }
    void restore(const QSet<long long> &snap) { marked = snap; }
    QList<long long> dirty() const;
    static QString add_to(const QString &text);
    static QString remove_from(const QString &text);
    std::optional<QStringList> save(const QString &backup_root);
};

class NewHistFiles {                            // doc.maps["hist_new"]
public:
    explicit NewHistFiles(ProvinceHistory &hist) : hist(hist) {}
    ProvinceHistory &hist;
    OrderedMap<long long, QString> made;        // pid -> rel path under history/provinces
    void add_many(const OrderedMap<long long, QString> &plan);
    std::vector<std::pair<long long, QString>> snapshot() const;
    void restore(const std::vector<std::pair<long long, QString>> &snap);
    QList<long long> dirty() const;
    QStringList save(const QString &backup_root);
};

ProvState clone_state(const ProvState &st);     // (a value copy: factory src stays shared, as in Python)

}  // namespace history

namespace document {
class Document;
}

namespace history {

// doc.maps["owners_top"]: provinces whose owner at the start comes from a dated block although that nation owns
// nothing in the undated history — the game crashes creating such a nation while it loads. On Apply their owner /
// controller (and the owner's core) are written at the top of their files instead (one undoable step to ask).
class OwnersOnTop : public store::Store {
public:
    static constexpr const char *kind = "owners_top";
    explicit OwnersOnTop(document::Document &doc) : doc(doc) {}
    document::Document &doc;
    QSet<long long> pids;
    void add(const QList<long long> &ps);
    store::Snap snapshot() const override { return store::box(pids); }
    void restore(const store::Snap &s) override { pids = store::unbox<QSet<long long>>(s); }
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
    bool undo_after_apply() const override { return true; }   // (its undo steps stay true when the file is written)
};

// Blocks dated on or before the start date (start_date of defines.lua) — pointless, the undated part IS the start
// (user's rule, a light warning): ([province], [nation tag]).
std::pair<std::vector<long long>, QStringList> start_blocks(document::Document &doc, moddata::Mod &mod);

// doc.maps["start_blocks"]: provinces / nations whose file has a block dated on or before the start date, to be
// folded into the undated part on Apply (the Errors tab's fix: the file never has to be opened as text).
class StartBlocks : public store::Store {
public:
    static constexpr const char *kind = "start_blocks";
    StartBlocks(document::Document &doc, moddata::Mod &mod) : doc(doc), mod(mod) {}
    document::Document &doc;
    moddata::Mod &mod;
    QSet<long long> pids;
    QSet<QString> tags;
    void add(const QList<long long> &ps = {}, const QStringList &ts = {});
    store::Snap snapshot() const override { return store::box(std::make_pair(pids, tags)); }
    void restore(const store::Snap &s) override
    {
        std::tie(pids, tags) = store::unbox<std::pair<QSet<long long>, QSet<QString>>>(s);
    }
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
    bool undo_after_apply() const override { return true; }
};

}  // namespace history
