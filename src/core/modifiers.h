// modifiers.py — The mod's event modifiers (common/event_modifiers.txt): what each one does (its effects: attribute
// = value), its icon, its name in the game, and which events / decisions add, remove or check it. Edits change the
// blocks' text in place (comments and layout are kept) and are written on Apply (doc.maps["event_mods"]).
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace scriptstore {
class ScriptStore;
}
namespace events {
struct Entry;
class EventDB;
}

namespace modifiers {

extern const QStringList FILE_PARTS;         // FILE = ("common", "event_modifiers.txt") (C's FILE is taken)
extern const std::vector<QStringList> ATTR_SOURCES;
extern const QString REL;
bool name_ok(const QString &s);              // NAME_RE.match: ^[A-Za-z_][A-Za-z0-9_]*$
bool num_ok(const QString &s);               // NUM_RE.match: ^-?\d+(\.\d+)?$

using Attrs = std::vector<std::pair<QString, QString>>;
Attrs _attrs_of(const script::Nodes &nodes);
Attrs block_attrs(const QString &text);      // [(key, value)] of a modifier block's text (its icon included)
QString set_attr(const QString &text, const QString &key, const QString &value);
QString del_attr(const QString &text, const QString &key);
QString new_block(const QString &name);

struct ModEntry {
    QString text;
    std::pair<qsizetype, qsizetype> span;
};

class ModifierDB {                           // the modifiers of common/event_modifiers.txt as a text has them
public:
    ModifierDB(moddata::Mod &mod, const std::optional<QString> &text = std::nullopt);
    moddata::Mod &mod;
    QString path, text;
    OrderedMap<QString, ModEntry> entries;   // name -> {"text", "span"}
    QStringList order;
    QStringList known_attrs;
    static void _collect(const script::Nodes &nodes, QSet<QString> &out);
    using Uses = OrderedMap<QString, std::vector<std::pair<const events::Entry *, QString>>>;
    Uses used_by(const events::EventDB &db) const;   // {modifier: [(entry, "adds" / "removes" / "checks")]}
};

class ModifierEdits : public store::Store {  // doc.maps["event_mods"]: read from / written into the editor's text
public:
    static constexpr const char *kind = "event_mods";
    ModifierEdits(moddata::Mod &mod, scriptstore::ScriptStore &store) : mod(mod), store(store) {}
    moddata::Mod &mod;
    scriptstore::ScriptStore &store;
    ModifierDB &db();
    QStringList names();
    bool exists(const QString &name);
    QString text(const QString &name);
    std::pair<qsizetype, qsizetype> span(const QString &name);
    void set(const QString &name, const QString &block);
    void add(const QString &name);
    void remove(const QString &name);
    Attrs attrs(const QString &name);
    // the text is doc.maps["scripts"]: nothing of its own to undo / count / write
    store::Snap snapshot() const override { return store::box(0); }
    void restore(const store::Snap &) override {}
    QStringList dirty() const override { return {}; }
    std::optional<QStringList> save(const QString &) override { return std::nullopt; }
    bool undo_after_apply() const override { return true; }

private:
    std::optional<QString> _text;
    std::unique_ptr<ModifierDB> _db;
};

}  // namespace modifiers
