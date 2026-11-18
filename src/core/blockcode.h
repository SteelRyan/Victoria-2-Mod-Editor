// blockcode.py — Vic2 script as building blocks: the catalogue of what the game understands (conditions, effects, the
// blocks that say "whose" — scopes), each with a plain sentence, the kind of value it takes and where it is legal (a
// nation, a province, a pop) — and the tree the block builder edits: read from a script's text, written back as tidy
// text.
//
// A line the catalogue doesn't know is kept exactly as written (a "raw" block): nothing of an existing event is ever
// lost by opening it here.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
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
namespace refs {
class RefIndex;
}
namespace events {
class EventDB;
}

namespace blockcode {

using OptStr = std::optional<QString>;
using Choices = std::vector<std::pair<QString, QString>>;   // [(key, name)]

extern const OrderedMap<QString, QString> SCOPE_NAME, MODE_NAME, MODE_NAMES, KEY_TYPE, KEY_SCRIPT, SLOT_NAME, VALUE_HELP;
extern const QString NOTE;                    // "#": the key of a comment item; its value is the text after the "#"

// ---------------------------------------------------------------------------------------------- the catalogue
struct Spec {
    QString key, mode, cat, scopes, text;
    OptStr vtype;                            // "fields" / "block" / a value type
    std::optional<std::vector<std::pair<QString, QString>>> fields;   // [(field, type)] of a block of fields
    OptStr out;                              // the scope inside a block of blocks ("c", "p", "s" or "" = the same)
    OptStr inner;                            // "cond" / "list" / "goal": what its blocks are (default: like itself)
    OptStr keytype;                          // the key is a name of the mod (a nation …)
    QSet<QString> optional;                  // fields that may be left out
    bool hidden = false;                     // understood in a script, not offered in the list
    Spec(const QString &key, const QString &mode, const QString &cat, QString scopes, const QString &text,
         const QString &value);
    bool is_block() const { return vtype == OptStr(QStringLiteral("block")); }
    QString kind() const;                    // what it is, for its colour
    QString script() const;                  // how the block looks in the script, in short
    bool fits(const OptStr &scope) const;
};
std::vector<Spec> _read(const QString &text, const QString &mode);
const std::vector<Spec> &COND();
const std::vector<Spec> &EFF();
const std::vector<Spec> &GOAL();
const std::vector<Spec> &OF_MODE(const QString &mode);

// ---------------------------------------------------------------------------------------------- the tree
struct Item {                                // {"k": key or None, "v": text or [item], "q": it was in quotes}
    OptStr k;
    bool list = false;
    QString text;
    std::vector<Item> items;
    bool q = false;
    static Item of(const OptStr &k, const QString &text, bool quoted = false) { return Item{k, false, text, {}, quoted}; }
    static Item of(const OptStr &k, std::vector<Item> items) { return Item{k, true, QString(), std::move(items), false}; }
};
using Items = std::vector<Item>;

class Env;
bool accepts(const Spec &spec, const Item &it, const Env *env = nullptr);
QStringList names_of(const OptStr &key, const Env *env);
const Spec *spec_for(const OptStr &key, const QString &mode, const OptStr &scope, const Env *env = nullptr,
                     const Item *value = nullptr);

Items from_nodes(const script::Nodes &nodes, const QString &text);
Items read_items(const QString &text);
QString _val(const Item &it);
struct Span {                                 // (first line, last line, item) — lines from 0
    long long first, last;
    const Item *item;
};
QString dump(const Items &items, long long depth = 0, std::vector<Span> *spans = nullptr, long long line = 0);
const Item *child(const Items &items, const OptStr &key);
OptStr value(const Items &items, const QString &key, const OptStr &dflt = std::nullopt);
Item *block(Items &items, const QString &key, bool make = false, std::optional<qsizetype> at = std::nullopt);
void set_value(Items &items, const QString &key, const OptStr &val, bool quoted = false,
               std::optional<qsizetype> at = std::nullopt);
Item new_item(const Spec &spec, const Env *env = nullptr);
QString _default(const QString &vtype, const Env *env);

// ---------------------------------------------------------------------------------------------- the tree by reference
// Python's items are dicts held by reference: the node window and the block builder keep an item while it moves from
// one list to another, and its identity (id(item)) is its key. PItem is that item: the same fields, the lists hold
// pointers (a list's identity is &item->items).
struct PItem;
using PPtr = std::shared_ptr<PItem>;
using PList = std::vector<PPtr>;
struct PItem {
    OptStr k;
    bool list = false;
    QString text;
    PList items;
    bool q = false;
};
PPtr pitem(const OptStr &k, const QString &text, bool quoted = false);
PPtr pitem(const OptStr &k, PList items);
PPtr to_ref(const Item &it);
PList to_ref(const Items &items);
Item from_ref(const PItem &it);
Items from_ref(const PList &items);
Item shallow(const PItem &it);               // its own lines without what is inside them: enough for accepts / spec_for
PItem *block(PList &items, const QString &key, bool make = false, std::optional<qsizetype> at = std::nullopt);

// entryview._files: the .txt files of a folder — the mod's, and the game's own that the mod doesn't replace (here:
// Env needs it; the Pages port will call this one)
QStringList files_of(const moddata::Mod &mod, const scriptstore::ScriptStore *store, const QString &folder,
                     bool hide_empty = false);

// ---------------------------------------------------------------------------------------------- the mod's names
class Env {                                  // what the mod has, for the pickers: choices(type), names
public:
    Env(moddata::Mod &mod, document::Document *doc, const refs::RefIndex *refs = nullptr, events::EventDB *db = nullptr);
    moddata::Mod &mod;
    document::Document *doc;
    const refs::RefIndex *refs;
    events::EventDB *db;
    scriptstore::ScriptStore *store = nullptr;
    OrderedMap<QString, QStringList> issues;
    QStringList options;
    QSet<QString> techs, inventions, option_set, ideologies, goods, poptypes;

    OptStr loc(const QString &key) const;
    QString name(const QString &key) const;
    QString prov_name(const QString &pid) const;
    QStringList _keys(const QString &rel, const QStringList &path = {}, int depth = 1) const;
    std::optional<Choices> choices(const QString &vtype, const OptStr &issue = std::nullopt) const;
    bool free_text(const QString &vtype) const;
    OptStr first(const QString &vtype) const;
    bool legal(const QString &vtype, const QString &v, const OptStr &issue = std::nullopt) const;
    void forget(const QString &vtype) { _cache.remove(vtype); }   // Python's env._cache.pop(vtype, None)

private:
    mutable QHash<QString, std::optional<Choices>> _cache;
};

QString sentence(const Item &it, const Spec *spec, const Env &env);

}  // namespace blockcode
