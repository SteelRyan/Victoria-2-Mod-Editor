// popsim.py — The sample province of Game rules → Pop influences: a stand-in for any province (the mod's own mix of
// pop types in whole per cent, 100,000 people) and what the ticked influences make of it after a time. The rules
// are the measured ones (popfacts.h); who moves IN is not simulated (it depends on every other province).
#pragma once
#include "core/ordered_map.h"
#include "core/popfacts.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace document {
class Document;
}
namespace moddata {
class Mod;
}

namespace popsim {

extern const long long SIZE;
extern const QHash<QString, int> STRATA;
extern const std::vector<std::pair<long long, QString>> TIMES;
extern const double FOCUS_SHARE;

OrderedMap<QString, long long> whole_shares(const OrderedMap<QString, long long> &counts, long long total = 100);
OrderedMap<QString, long long> sample(const document::Document *doc, const moddata::Mod &mod, const QStringList &types);

// a chance's number: one for every pop type, or {pop type: number}
struct NVal {
    bool is_map = false;
    double x = 0.0;
    QHash<QString, double> m;
    NVal() = default;
    NVal(double v) : x(v) {}
    NVal(QHash<QString, double> v) : is_map(true), m(std::move(v)) {}
};
using Numbers = QHash<QString, NVal>;          // "promotion", "demotion", "migration", "emigration", "colonial"
using Shares = QHash<QString, double>;
using Weights = OrderedMap<QString, double>;   // target type -> weight
using NFn = std::function<Numbers(const QString &, const Shares &)>;
using WFn = std::function<Weights(const QString &, const Shares &)>;
using NArg = std::variant<Numbers, NFn>;
using WArg = std::variant<QHash<QString, Weights>, WFn>;

struct Result {
    OrderedMap<QString, double> after;
    double left = 0.0;
    OrderedMap<std::pair<QString, QString>, double> moved;
};
Result run(const OrderedMap<QString, double> &start, const QHash<QString, QString> &strata, double months, const NArg &n,
           const WArg &weights, const popfacts::Defs &defs, double eff,
           const std::optional<std::pair<QString, double>> &focus = std::nullopt, long long every = 1);

// run() for a REAL place, its pops kept apart by culture and religion (popsim.run_groups)
using Group = std::pair<QString, QString>;    // (culture, religion)
using GKey = std::pair<QString, Group>;      // (pop type, group)
using GNFn = std::function<QHash<QString, double>(const QString &, const Group &, const Shares &)>;
using GWFn = std::function<Weights(const QString &, const Group &, const Shares &)>;
using Turn = std::function<std::pair<std::optional<Group>, std::optional<Group>>(const Group &)>;
struct GResult {
    OrderedMap<GKey, double> after;
    double left = 0.0;
    OrderedMap<std::pair<QString, QString>, double> moved;
    OrderedMap<std::pair<Group, Group>, double> changed;
};
GResult run_groups(const OrderedMap<GKey, double> &start, const QHash<QString, QString> &strata, double months,
                   const GNFn &n, const GWFn &weights, const popfacts::Defs &defs, double eff,
                   const std::optional<std::pair<QString, double>> &focus, const Turn &turn, long long every = 1);

bool class_ok(const QString &cond, const QString &strata);
double decay(double people, double number, double scale, double months);

}  // namespace popsim
