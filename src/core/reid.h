// reid.py — Re-ID: give a mod's events new ids, neatly and in order, without breaking a single call — and the mod's
// own ID SYSTEM (a model the maker picks once), written down where the mod keeps it and followed by every new event.
//
// Where an event is DEFINED: events/*.txt  `country_event = { id = N …` / `province_event = { id = N …`.
// Where it is CALLED (every one of them is rewritten with it): country_event = N · province_event = N ·
// country_event = { id = N days = … } in events, decisions, common/, inventions, technologies; common/on_actions.txt
// on_xxx = { weight = N } (and one level deeper). The text keys (EVTNAME…) are left as they are.
//
// Models: blocks (a block of ids per file), running (one number for the whole mod), tidy (each file from its own
// lowest id). The system is kept in the mod's folder in "ModEditor ID system.txt" (the game does not read it), and
// described for people in "ModEditor ID system.md".
//
// `after`: texts that stand in for the store's — Python's reidwin.After (the store as it would be with them).
#pragma once
#include "core/eventtools.h"
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace scriptstore {
class ScriptStore;
}

namespace reid {

using Texts = OrderedMap<QString, QString>;
using OptStr = std::optional<QString>;

extern const QString SYSTEM_FILE, DOC_FILE;
extern const OrderedMap<QString, QString> MODELS;
extern const QStringList CALL_FOLDERS;

struct System {
    QString model = QStringLiteral("blocks");
    long long start = 10000, block = 1000;
    std::vector<std::pair<QString, long long>> files;   // (rel, first id)
};

eventtools::Ids defined(const moddata::Mod &mod, scriptstore::ScriptStore &store, bool own_only = true,
                        const Texts *after = nullptr);
bool is_own(const moddata::Mod &mod, const QString &rel);
eventtools::Ids calls(const moddata::Mod &mod, scriptstore::ScriptStore &store);

std::optional<System> read_system(const QString &text);
QString write_system(const System &sysd);

struct Plan {
    OrderedMap<QString, QString> mapping;   // {old: new}
    System sys;                             // every file's first id filled in
    QStringList notes;
};
// files: the event files to renumber (empty = all the mod's own)
Plan plan(const moddata::Mod &mod, scriptstore::ScriptStore &store, const System &sysd, const QStringList &files = {});

struct Counts {
    long long defined = 0, calls = 0, files = 0;
};
std::pair<Texts, Counts> rewrite(const moddata::Mod &mod, scriptstore::ScriptStore &store,
                                 const OrderedMap<QString, QString> &mapping);

OptStr next_id(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel,
               const std::optional<System> &sysd, const QStringList &taken = {}, const Texts *after = nullptr);
OptStr suggest(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel);
QString document(const moddata::Mod &mod, scriptstore::ScriptStore &store, const System &sysd,
                 const Texts *after = nullptr);

}  // namespace reid
