// staterefs.py — Where the mod's scripts name a state (its region.txt key), before a state is deleted (Sam's item
// 2). A state key stands in a script as a scope (TEX_132 = { add_core = USA }) or in a condition (region = CHI_1557).
// The fix writes the provinces the state had instead: a scope becomes one block per province, `region = KEY`
// becomes province_id = 220 (several: OR = { province_id = … }).
#pragma once
#include "core/moddata.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace scriptstore {
class ScriptStore;
}

namespace staterefs {

extern const QStringList FOLDERS;

struct Place {
    QString rel;
    qsizetype pos = 0;
    long long line = 0;
    QString text;
    QString kind;   // "scope" / "region" / "other"
};
std::pair<long long, QString> _line_of(const QString &text, qsizetype pos);
std::optional<qsizetype> _block_end(const QString &text, qsizetype brace);
std::vector<Place> find(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &key);
std::pair<QString, long long> fixed_text(QString text, const QString &key, const std::vector<long long> &provinces);

}  // namespace staterefs
