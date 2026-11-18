// importer.py, the rest of it (find_sources is core\importer): taking things over from another mod (or from the game
// itself) — events with their chains, decisions, nations (their files, or with their land), modifiers, governments,
// national values, cultures, religions, units, pop types, war goals, inventions, game settings … and plain files.
// What is picked is looked through for what it needs and the mod doesn't have — events it calls, modifiers, cultures …
// it names, its texts, pictures, flags and icons — and those come along (each can be left out). Everything is queued
// as ONE undoable step; Apply writes it.
// Python walks sets of texts here (the names a thing uses, the events it calls) and their order reaches the files:
// grid::PySet<QString> — CPython's order with PYTHONHASHSEED=0 (Python's own default order changes per run).
#pragma once
#include "core/document.h"
#include "core/history.h"
#include "core/importer.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/scriptstore.h"
#include "grid/pyset.h"

#include <QImage>
#include <map>
#include <set>
#include <tuple>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace localisation {
class LocEdits;
}

namespace importer {

using OptStr = std::optional<QString>;

struct Cat {                                     // one kind of thing (CATS)
    QString id, title;
    bool special = false;
    QString about, how, rel, folder;
    QStringList base, skip;
};
extern const std::vector<Cat> CATS;
const Cat &cat_of(const QString &id);            // (Python: CAT[id])
extern const QSet<QString> HARD;
extern const QRegularExpression WORD, CALLS;
extern const QStringList TEXT_EXT;
QString norm(const QString &s);

class Source {                                   // the other mod, read like the game reads it
public:
    Source(const QString &folder, const std::optional<QString> &base);
    QString root, name;
    QStringList replace;
    std::unique_ptr<moddata::Mod> mod;
    std::unique_ptr<scriptstore::ScriptStore> store;
    QString text(const QString &rel);
    std::optional<QString> loc(const QString &key) const;
    QStringList tag_keys(const QString &tag);

private:
    std::optional<OrderedMap<QString, QStringList>> _by_tag;
};

using Id = std::pair<QString, QString>;          // (kind, key)
struct Item {                                    // one thing of the other mod that can be taken over
    QString cat, key, name;
    OptStr group;
    QString rel;
    std::optional<QStringList> path;
    QString text, status;
    // extra:
    grid::PySet<QString> calls;
    bool has_calls = false;
    QString file, xname, adj;                    // nations ("name" of extra: xname)
    OptStr hist;
    QString sec, xkey, note;                     // settings
    Id id() const { return {cat, key}; }
};
using ItemP = std::shared_ptr<const Item>;

struct Needed {                                  // (Item, why, it comes, hard)
    ItemP dep;
    QString why;
    bool comes, hard;
};
struct IconOps {                                 // ([(sprite, frame, picture)], {sprite: [(picture, sisters)]}, {id: text})
    std::vector<std::tuple<QString, long long, QImage>> puts;
    OrderedMap<QString, std::vector<std::pair<QImage, OrderedMap<QString, QImage>>>> adds;
    std::map<Id, QString> texts;
};
struct Options {
    QString nation_mode = QStringLiteral("files"), event_conflict = QStringLiteral("renumber");
    bool icons = true;
};
struct Plan {                                    // what an import will do
    std::vector<ItemP> items;                    // every Item that comes (picked and needed)
    std::vector<Needed> needed;
    OrderedMap<QString, QString> loc, files, flags;
    QStringList warnings, notes;
    OrderedMap<QString, std::pair<std::vector<long long>, std::vector<long long>>> land;
    long long icons = 0;
    Options options;
    IconOps icon_ops;
};

struct TEntry {                                  // tindex: (rel, path, text)
    OptStr rel;
    QStringList path;
    QString text;
};

class Importer {
public:
    Importer(moddata::Mod &mod, document::Document &doc, Source &src);
    moddata::Mod &mod;
    document::Document &doc;
    Source &src;
    scriptstore::ScriptStore &store;

    OptStr tloc(const QString &key);
    QString nice(const QString &key, const QStringList &alts = {}) const;
    bool map_same();
    struct Block {
        OptStr group;
        QString key, rel;
        QStringList path;
        QString text;
    };
    std::vector<Block> _blocks(const Cat &cat, const moddata::Mod &m, scriptstore::ScriptStore &st);
    const OrderedMap<QString, TEntry> &tindex(const QString &cid);
    void forget();
    const std::vector<ItemP> &items(const QString &cid);
    ItemP find(const QString &cid, const QString &key);
    QString _status(const QString &cid, const QString &key, const QString &text);
    std::vector<ItemP> _scan_blocks(const QString &cid);
    std::vector<ItemP> _scan_events(const QString &cid);
    std::vector<ItemP> _scan_decisions(const QString &cid);
    std::vector<ItemP> _scan_nations(const QString &cid);
    struct Row {
        QString value, note;
        long long line;
    };
    static OrderedMap<std::pair<QString, QString>, Row> _rows(const QString &text);
    std::vector<ItemP> _scan_settings(const QString &cid);
    std::vector<ItemP> _scan_files(const QString &cid);
    grid::PySet<QString> chain(const QString &eid);
    struct Names {
        OrderedMap<QString, QStringList> word;   // name -> [kinds] the other mod has and this one doesn't
        OrderedMap<QString, QString> opts;       // reform option -> its issue
    };
    const Names &names();
    QString _history(const Item &item);
    std::vector<std::tuple<QString, QString, QString>> needs(const Item &item);
    Plan plan(const std::vector<Id> &picked, const std::set<Id> &left_out = {}, const Options &options = {},
              const std::set<Id> &also = {});
    void _decorate(Plan &plan);
    std::pair<QString, long long> _adapt_history(const QString &text);
    std::pair<std::vector<long long>, std::vector<long long>> _land(const QString &tag);
    IconOps _icons(const Plan &plan);
    QString apply(const Plan &plan, const OptStr &label = std::nullopt);
    QString _orig_text(const QString &rel);

private:
    std::map<QString, std::vector<ItemP>> _items;
    std::map<QString, OrderedMap<QString, TEntry>> _tindex;
    std::optional<Names> _names;
    std::unique_ptr<history::ProvinceHistory> _hist;
    std::optional<bool> _map_same;
    struct Rules {
        QSet<QString> src_keys, mine, issues, opts;
    };
    std::optional<Rules> _rules;
};

// Mod.sync_cultures: cultures and religions as the editor's texts have them now -> true when read again
bool sync_cultures(moddata::Mod &mod, scriptstore::ScriptStore &store, localisation::LocEdits *loc = nullptr);
// popcheck.import_from: the pop type (culture, religion) `key` imported from the mod in `folder` -> the message
QString import_from(moddata::Mod &mod, document::Document &doc, const QString &kind, const QString &key,
                    const QString &folder);

}  // namespace importer
