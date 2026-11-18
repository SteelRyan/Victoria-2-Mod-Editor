// provrenumber.py — After provinces were deleted: the numbers are closed up and nothing is left that names a
// province wrongly. The highest province moves into each freed number (its definition.csv row, history file, pops,
// places, state, continent, climate, adjacency rows, sea_starts entry, units, capitals, name, scripts); a script
// that names a deleted province names its heir instead, or (no heir) is taken out: a condition becomes
// `always = no`, an effect is removed. Everything is worked out first (plan), then written with backups (apply).
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace provrenumber {

extern const QSet<QString> PROV_KEYS, EFFECT_BLOCKS;
extern const QStringList SCRIPT_FOLDERS;
extern const QString NEVER;
// (C++ only, 2026-11-09) the comment left where a place naming a deleted province was taken out (%1: its number);
// the Errors tab lists every one
extern const QString GONE_NOTE;

struct Job {
    OrderedMap<long long, long long> moves;                    // the province that moves -> the number it gets
    std::vector<long long> dropped;                            // numbers whose row simply goes
    OrderedMap<long long, std::optional<long long>> heirs;     // deleted province -> its heir (nullopt: none)
    QHash<long long, QString> names;                           // province -> its name
    OrderedMap<QString, QString> texts;                        // path -> new text
    std::vector<std::pair<QString, QString>> renames;          // (old path, new path): history files
    // (C++ only, 2026-11-08) mod files written EMPTY after the renames: a game history file of a moved or deleted
    // number would otherwise still be read (the game reads its own file unless the mod has one of the same name)
    QStringList blanks;
    OrderedMap<QString, long long> counts;                     // what was changed, by kind
    long long retargeted = 0;
    long long neutral = 0;
    std::vector<std::pair<QString, long long>> left;           // (file, how often)
    long long top = 0;

    QString summary() const;
};

using Heirs = OrderedMap<long long, std::optional<long long>>;
// ({province that moves: its new number}, [freed numbers above every province left])
std::pair<OrderedMap<long long, long long>, std::vector<long long>> moves_for(const std::vector<long long> &real,
                                                                           const std::vector<long long> &free,
                                                                           const Heirs &heirs = {});
// mode "list" (bare numbers in lists) or "top" (keys of the outermost blocks) -> (text, how many replaced)
std::pair<QString, long long> rewrite(QString text, const QHash<QString, QString> &mapping, const QString &mode);
QString _cut(const QString &text, qsizetype a, qsizetype b);
struct ScriptResult {
    QString text;
    OrderedMap<QString, long long> hits;   // {number: places replaced}
    long long cut = 0;                     // places taken out
    long long left = 0;                    // places left alone (neither a condition nor an effect)
};
ScriptResult rewrite_script(QString text, const QHash<QString, QString> &mapping, const QSet<QString> &drop = {});

// (C++ only, 2026-11-09) the places a script names a province: `key = N` with a province key (PROV_KEYS) and
// `N = { … }` scopes — not inside a comment, not the chance weights right inside a random_list. Also the notes a
// delete left (GONE_NOTE): note = true, num = the deleted number. In text order of each kind.
struct ProvRef {
    qsizetype pos;
    long long num;
    bool note;
};
std::vector<ProvRef> province_refs(const QString &text);

std::optional<Job> plan(moddata::Mod &mod);
// -> (files written, backup folder)
std::pair<QStringList, QString> apply(moddata::Mod &mod, const Job &job, const QString &backup_root);

}  // namespace provrenumber
