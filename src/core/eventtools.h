// eventtools.py — Small helpers for events and decisions: every event id with where it is defined (and which are
// doubled), the next free id, a fresh event / decision with every part ready to fill in, and putting it into a file.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace scriptstore {
class ScriptStore;
}

namespace eventtools {

using OptStr = std::optional<QString>;
using Place = std::tuple<QString, qsizetype, qsizetype>;   // (rel, position of the id's value, its length)
using Ids = OrderedMap<QString, std::vector<Place>>;

extern const QStringList EVENT_KEYS;
bool key_ok(const QString &s);               // KEY_RE: ^[A-Za-z_][A-Za-z0-9_]*$

// after: texts that stand in for the store's (Python: reidwin.After — the store as it would be with them)
Ids event_ids(const moddata::Mod &mod, scriptstore::ScriptStore &store, const OrderedMap<QString, QString> *after = nullptr);
// renumber: the events at `places` [(rel, pos, id)] get free ids -> ({rel: new text}, [(old id, new id, rel)])
std::pair<OrderedMap<QString, QString>, std::vector<std::tuple<QString, QString, QString>>>
renumber(const moddata::Mod &mod, scriptstore::ScriptStore &store, const std::vector<std::tuple<QString, qsizetype, QString>> &places);
Ids duplicate_ids(const moddata::Mod &mod, scriptstore::ScriptStore &store);
OrderedMap<QString, std::vector<std::pair<QString, qsizetype>>> decision_keys(const moddata::Mod &mod,
                                                                             scriptstore::ScriptStore &store);
QString free_id(const QStringList &used, const QStringList &near = {}, const QSet<QString> &taken = {});
QStringList ids_of_file(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel);
QString event_text(const QString &kind, const QString &eid, long long options = 1, bool triggered = false,
                   const QString &picture = {});
QStringList decision_lines(const QString &picture = {});
QString add_event(const QString &text, const QString &block);
QString add_decision(QString text, const QString &key, const QString &picture = {});
OptStr safe_file(const QString &name);

}  // namespace eventtools
