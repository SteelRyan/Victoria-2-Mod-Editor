// popcaps.py — Where the pop type files shut a pop type's growth off — and who starts above it.
//
// In poptypes/<type>.txt, `promote_to = { soldiers = { … } }` says how gladly pops of that type become soldiers. A
// modifier in there with a factor of -100 or lower that asks for nothing but the share the target type already has
//
//     modifier = { factor = -100  soldiers = 0.05 }                       (of the pop's province)
//     modifier = { factor = -100  state_scope = { soldiers = 0.05 } }     (of its state)
//
// is a shutdown: from that share on, nobody of that type becomes a soldier any more. Where every type that can become
// a soldier at all has such a line, the highest of their shares is the mod's own ceiling — a province (or state) that
// STARTS with more of them than that has more than the mod's rules ever let grow.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace document {
class Document;
}

namespace popcaps {

extern const double SHUT;                    // a factor this low (or lower) switches a promotion off

struct CapLine {                             // (file, line, from type, scope, share, factor)
    QString rel;
    long long line;
    QString src, scope;
    double share, factor;
};
struct Cap {
    std::optional<double> province, state;
    std::vector<CapLine> lines;
    QStringList open;                        // types that can become it without such a line
};
using Caps = OrderedMap<QString, Cap>;

std::optional<std::pair<QString, double>> _share(const script::Node &mod_node, const QString &ptype);
Caps read_caps(const moddata::Mod &mod, const std::function<QString(const QString &)> &text_of = nullptr);

struct Over {                                // (pop type, scope, cap, where, have, total)
    QString type, scope;
    double cap;
    std::variant<long long, QString> where;  // a province id / a state key
    long long have, total;
};
std::vector<Over> over_cap(const moddata::Mod &mod, const document::Document &doc, const Caps &caps);

}  // namespace popcaps
