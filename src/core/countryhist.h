// countryhist.py — Nations: history/countries/<TAG> - <name>.txt (a nation at a start date) plus what the game
// defines around it — technologies (technologies/*.txt, folders from common/technology.txt), inventions, reforms
// (common/issues.txt), governments, national values, tech schools and each nation's parties
// (common/countries/<file>.txt).
//
// Edits for a start date are written into that date's block (`1836.1.1 = { … }`) of the nation's history file (made
// when missing), like the province history. Taking away something the file gives earlier (a technology, an
// invention, an accepted culture) is done where it is given: that line is commented out (`#muzzle_loaded_rifles = 1`),
// the way the mod's own files keep the next steps of a tech line.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace scriptstore {
class ScriptStore;
}

namespace countryhist {

using Date = history::Date;
using OptStr = std::optional<QString>;
using Span = std::pair<qsizetype, qsizetype>;
using Rgb = std::tuple<int, int, int>;

extern const QStringList SCALARS;
extern const QStringList UPPER_HOUSE_IDEOLOGIES;

QString date_key(const Date &d);

// ---------------------------------------------------------------------------------------------- game data
struct TechInfo {
    QString area, year, folder;
    long long index = 0;
    bool operator==(const TechInfo &o) const = default;
};
struct InventionInfo {
    QString file;
    QStringList needs;
};
struct Reform {                              // (section, issue, [options])
    QString section, issue;
    QStringList options;
};
struct Party {                               // (name, ideology, start, end)
    QString name, ideology, start, end;
    bool operator==(const Party &o) const = default;
};

class NationData {
public:
    explicit NationData(moddata::Mod &mod);
    moddata::Mod &mod;
    std::vector<std::pair<QString, QStringList>> folders;   // [(folder, [area, …])]
    QStringList schools;
    OrderedMap<QString, TechInfo> techs;
    OrderedMap<QString, QStringList> area_techs;            // area -> [tech in order]
    OrderedMap<QString, InventionInfo> inventions;
    std::vector<Reform> reforms;                            // political_reforms / social_reforms
    OrderedMap<QString, QString> reform_of;                 // option -> issue (the unciv reforms' too)
    QSet<QString> reform_issues;                            // (the unciv reforms' too)
    // C++ only (the user 2026-11-18: "set what unciv reforms are already taken by the nation"): economic_reforms /
    // military_reforms of issues.txt — a nation's history names them as the other reforms (`land_reform =
    // yes_land_reform`), so they are read and written with them; unciv_progress: an option's
    // civilization_progress_modifier
    std::vector<Reform> unciv;
    OrderedMap<QString, double> unciv_progress;
    QStringList governments, nationalvalues;
    // the editor's texts (the parties edited on the page) — shared: as in Python, the store it was given stays alive
    // after the document is made again (a check run before the Nations page is shown next reads it)
    std::shared_ptr<scriptstore::ScriptStore> store;

    static const QStringList LIST_FILES;
    void reload_lists(scriptstore::ScriptStore &store);
    void reinit();                             // nd.__init__(mod) again (techdata.TechEdits.save): store / _party_text stay
    const std::vector<Party> &parties(const QString &tag);
    QString folder_counts(const QSet<QString> &techs) const;

private:
    QStringList _files(const QString &folder) const;
    void _load();
    OrderedMap<QString, std::vector<Party>> _parties;
    OrderedMap<QString, OptStr> _party_text;                // (Python: hash(text))
};

// ---------------------------------------------------------------------------------------------- history
struct Src {                                 // st["src"][(what, key)]: where the file gives it
    std::optional<Date> date;
    Span span;
    bool operator==(const Src &o) const = default;
};
using SrcKey = std::pair<QString, QString>;  // ("culture" / "tech" / "invention", key)

struct NationState {                         // empty_nation() / state_at(): one nation at a date
    OrderedMap<QString, OptStr> scalars;     // every key of SCALARS, None until a file says it
    QStringList cultures;
    OrderedMap<QString, QString> reforms;
    QSet<QString> techs, inventions;
    OrderedMap<QString, QString> upper_house;
    OrderedMap<SrcKey, Src> src;
    OptStr &operator[](const QString &k) { return scalars[k]; }
    OptStr at(const QString &k) const { return scalars.value(k); }
    bool operator==(const NationState &o) const = default;   // (as Python's dict ==)
};
NationState empty_nation();
inline NationState clone(const NationState &st) { return st; }

struct NationKey {                           // key_of: what counts for 'changed' (sources left out)
    std::vector<OptStr> scalars;
    QStringList cultures;
    std::vector<std::pair<QString, QString>> reforms, upper_house;   // sorted
    QStringList techs, inventions;                                   // sorted
    bool operator==(const NationKey &o) const = default;
};
NationKey key_of(const NationState &st);

struct NodesOf {                             // nodes(): (path or None, text, nodes)
    OptStr path;
    QString text;
    std::shared_ptr<const std::pair<QString, script::Nodes>> held;
    const script::Nodes &nodes() const;
};

class CountryHistory {
public:
    CountryHistory(moddata::Mod &mod, NationData &data);
    moddata::Mod &mod;
    NationData &data;
    QString dir;
    OrderedMap<QString, QString> files;      // tag -> path (always inside the mod)
    OrderedMap<QString, QString> base_src;   // tag -> the base game's file, for a nation the mod has no file of
    OrderedMap<QString, QString> virtual_;   // tag -> history text of nations made in the editor (not on disk yet)
    OrderedMap<QString, std::shared_ptr<const std::pair<QString, script::Nodes>>> _nodes;

    QString _now(const QString &tag, const QString &path) const;
    NodesOf nodes(const QString &tag);
    void _apply(NationState &st, const std::vector<const script::Node *> &nodes, const std::optional<Date> &date) const;
    NationState state_at(const QString &tag, const std::optional<Date> &date, bool include_date = true);
    const std::optional<Date> &start();
    static Span _span(const QString &text, const script::Node &n);
    std::vector<std::tuple<qsizetype, qsizetype, QString>> _top_edits(const QString &text, const script::Nodes &nodes,
                                                                      const NationState &want, const QString &nl,
                                                                      const Date &start);
    QStringList save(const OrderedMap<QString, NationState> &changes, const Date &date, const QString &backup_root);

private:
    std::optional<std::optional<Date>> _start;
    struct Was {
        Date date;
        QString before, after;
        NationKey said;
    };
    OrderedMap<QString, Was> _was;           // path -> (date, text before, text after, what it said before)
    QStringList _tech_order(const QSet<QString> &techs) const;
};

bool tag_ok(const QString &tag);             // TAG_RE.match
extern const QSet<QString> RESERVED;
QString safe_name(const QString &name);

QStringList party_names(const QString &ctext, const std::optional<Date> &date = std::nullopt);
std::pair<QString, QString> generic_party(const moddata::Mod &mod, const QString &tag);
struct WithParty {
    QString ctext, htext;
    OrderedMap<QString, QString> texts;      // {localisation key: text} to add
    QString did;                             // what was done, or ""
};
WithParty with_party(const moddata::Mod &mod, const QString &tag, QString ctext, const QString &htext,
                     const std::optional<Date> &date = std::nullopt);

class NationEdits;

struct NewNation {                           // NewNations.added[tag]
    QString name, adj;
    Rgb color;
    QString template_, file;
    OptStr ctext, htext;                     // (imported: its own files, as they were in the other mod)
    bool stored = false;                     // (imported, ctext None: the countries file is one of the editor's texts)
    bool imported = false;                   // (the record has "ctext" / "htext" / "stored" keys)
    bool operator==(const NewNation &o) const = default;
};

class NewNations : public store::Store {     // doc.maps["new_nations"]
public:
    static constexpr const char *kind = "new_nations";
    NewNations(moddata::Mod &mod, CountryHistory &hist);
    moddata::Mod &mod;
    CountryHistory &hist;
    NationEdits *nations = nullptr;          // (the capital picked for a new nation goes to its file's top)
    OrderedMap<QString, NewNation> added;
    QStringList party_notes;
    std::optional<Date> date;                // (Python: getattr(self, "date", None) — nothing sets it)

    static QString history_text(const QString &template_text);
    QString problem(const QString &tag, const QString &name) const;
    void add(const QString &tag, const QString &name, const QString &adj, const Rgb &color, const QString &tmpl);
    void add_imported(const QString &tag, const QString &name, const QString &adj, const Rgb &color,
                      const QString &file, const OptStr &ctext, const OptStr &htext);
    void _show();

    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

class NationEdits : public store::Store {    // doc.maps["nations"]: {tag: wanted state at the scenario's date}
public:
    static constexpr const char *kind = "nations";
    NationEdits(CountryHistory &hist, const std::optional<Date> &date) : hist(hist), date(date) {}
    CountryHistory &hist;
    std::optional<Date> date;
    OrderedMap<QString, NationState> want;

    NationState state(const QString &tag) const;
    void edit(const QString &tag, const std::function<void(NationState &)> &fn);

    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    bool partial_undo() const override { return true; }
    bool has_rebase() const override { return true; }
    store::Snap rebase_begin() override;
    store::Snap rebase(const store::Snap &snap, const store::Snap &old) override;
    std::optional<QStringList> save(const QString &backup_root) override;

    mutable OrderedMap<QString, NationState> _orig;   // (history.StartBlocks forgets a nation's once it is written)
    const NationState &_orig_state(const QString &tag) const;
};

}  // namespace countryhist
