// popcheck.py — Pops the mod can't have (the tester, 2026-10-15): a pop TYPE that isn't a pop type of this mod (an
// error), a culture / religion that isn't defined, pops of a province number that isn't in definition.csv. Each
// comes with what to do: import it from a mod that has it, or change those pops into one this mod has; provinces that
// don't exist: their blocks go from the pop files on Apply (document::DropPopBlocks).
// import_from needs importer's Importer (kind B): it follows with it.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace document {
class Document;
}
namespace scriptstore {
class ScriptStore;
}

namespace popcheck {

using Where = std::pair<std::optional<QString>, std::optional<long long>>;   // (file, line)
struct Problem {
    std::vector<long long> pids;
    long long pops = 0, size = 0;
    std::optional<Where> where;
};
struct Problems {
    OrderedMap<QString, Problem> type, culture, religion;
    OrderedMap<long long, Problem> province;
};

QSet<QString> valid_types(const moddata::Mod &mod, scriptstore::ScriptStore *store);
Problems problems(document::Document &doc, const moddata::Mod &mod);
long long count(const Problems &probs);
QString where_text(const std::optional<Where> &where);
std::vector<std::pair<QString, QString>> sources_with(const moddata::Mod &mod, const QString &kind, const QString &key);
long long change_into(document::Document &doc, const QString &field, const QString &old, const QString &nw,
                      const std::optional<QList<long long>> &pids = std::nullopt);

}  // namespace popcheck
