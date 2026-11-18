// popfacts.py — What the game does with the chances of the pop files, MEASURED in the running game (Victoria 2 3.04
// with Ferrum Mare 1.2.000, 2026-10-12/13). A chance block's number = its own factor × the SUM of its modifiers that
// hold; people a month = pop × number × the define's scale (× administrative efficiency for promotion). The full
// list of what was measured, and what was not, is the Python module's docstring.
#pragma once
#include "core/ordered_map.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

namespace popfacts {

using Defs = OrderedMap<QString, double>;

extern const QString SHARE_LABEL, SHARE_TIP;
extern const OrderedMap<QString, QString> SCALES, GUESS, VERB, WHO;
extern const QStringList WEIGHTS;

Defs defines(const QString &text);             // {NAME: number} of a defines.lua
double share(const Defs &defs);
QString num(double x);
QString people(double n);                      // a count of people a month, in words
std::optional<double> per_month(const QString &head, double value, long long size, const Defs &defs, double eff);
QString say(const QString &head, double value, long long size, const Defs &defs, double eff, bool short_ = false);
std::pair<QString, bool> rule(const QString &head, const Defs &defs);

}  // namespace popfacts
