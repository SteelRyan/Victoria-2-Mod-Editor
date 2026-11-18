// popfacts.py — the measured rules of the pop chances. Line by line.
#include "core/popfacts.h"
#include "core/pynum.h"
#include "core/pyre.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace popfacts {

const QString SHARE_LABEL = u"The nation's administrative efficiency"_s;
const QString SHARE_TIP = u"Promotion is multiplied by it (measured). The game shows it on the budget screen; it is 100 % "
                          u"when the nation's bureaucrats are MAX_BUREAUCRACY_PERCENTAGE of its people (defines.lua). "
                          u"Herradem at the start of Ferrum Mare: 23 %."_s;
const OrderedMap<QString, QString> SCALES = {
    {u"promotion_chance"_s, u"PROMOTION_SCALE"_s},          {u"demotion_chance"_s, u"PROMOTION_SCALE"_s},
    {u"migration_chance"_s, u"IMMIGRATION_SCALE"_s},        {u"colonialmigration_chance"_s, u"IMMIGRATION_SCALE"_s},
    {u"emigration_chance"_s, u"IMMIGRATION_SCALE"_s},       {u"assimilation_chance"_s, u"ASSIMILATION_SCALE"_s},
    {u"conversion_chance"_s, u"CONVERSION_SCALE"_s}};
const OrderedMap<QString, QString> GUESS = {};
const OrderedMap<QString, QString> VERB = {
    {u"promotion_chance"_s, u"are promoted"_s},
    {u"demotion_chance"_s, u"are demoted"_s},
    {u"migration_chance"_s, u"move to another province of the nation"_s},
    {u"colonialmigration_chance"_s, u"move to the colonies"_s},
    {u"emigration_chance"_s, u"emigrate"_s},
    {u"assimilation_chance"_s, u"take on the primary culture (and the state religion)"_s},
    {u"conversion_chance"_s, u"take on the state religion"_s}};
const OrderedMap<QString, QString> WHO = {
    {u"demotion_chance"_s, u" Demotion and promotion both happen in the same month."_s},
    {u"assimilation_chance"_s, u" Only pops of a culture the nation does not accept."_s},
    {u"conversion_chance"_s, u" Only pops of another religion; a pop of a not-accepted culture assimilated instead."_s},
    {u"migration_chance"_s, u" They arrive where the province weights (migration target) draw them."_s},
    {u"emigration_chance"_s, u" Which nation they go to was not looked at."_s},
    {u"colonialmigration_chance"_s, u" Which colony they go to was not looked at."_s}};
const QStringList WEIGHTS = {u"promote_to"_s, u"ideologies"_s, u"issues"_s, u"migration_target"_s,
                             u"country_migration_target"_s};

Defs defines(const QString &text)
{
    Defs out;
    const QString S = py::PY_S;
    auto it = py::rx(u"(?m)^"_s + S + u"*([A-Z_0-9]+)"_s + S + u"*="_s + S + u"*(-?\\d+(?:\\.\\d+)?)"_s + S + u"*,"_s)
                  .globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!out.contains(m.captured(1)))
            out.insert(m.captured(1), py::to_float(m.captured(2)).value_or(0.0));
    }
    return out;
}

double share(const Defs &) { return 1.0; }

static QString rstrip_ch(QString s, QChar c)
{
    while (s.endsWith(c))
        s.chop(1);
    return s;
}

QString num(double x)
{
    return std::fabs(x) < 1000 ? rstrip_ch(rstrip_ch(py::fmt_f(x, 3), u'0'), u'.') : QString::number(py::round_int(x));
}

QString people(double n)
{
    if (n >= 10)
        return u"%1 a month (%2 a year)"_s.arg(py::fmt_commas(n, 0), py::fmt_commas(n * 12, 0));
    if (n >= 1)
        return u"%1 a month (%2 a year)"_s.arg(py::fmt_f(n, 1), QString::number(py::round_int(n * 12)));
    if (n * 12 >= 1)
        return u"about %1 a year"_s.arg(py::round_int(n * 12));
    return u"less than 1 a year"_s;
}

std::optional<double> per_month(const QString &head, double value, long long size, const Defs &defs, double eff)
{
    if (!SCALES.contains(head))
        return std::nullopt;
    const QString name = SCALES.value(head);
    if (!defs.contains(name))
        return std::nullopt;
    if (value <= 0)
        return 0.0;
    return static_cast<double>(size) * value * defs.value(name) * (head == u"promotion_chance" ? eff : 1.0);
}

QString say(const QString &head, double value, long long size, const Defs &defs, double eff, bool short_)
{
    const auto n0 = per_month(head, value, size, defs, eff);
    if (!n0)
        return QString();
    const double n = *n0;
    if (n <= 0)
        return !short_ ? u"nobody moves"_s : QString();
    if (short_) {
        if (n >= 10)
            return u"≈ %1 a month"_s.arg(py::fmt_commas(n, 0));
        if (n >= 1)
            return u"≈ %1 a month"_s.arg(py::fmt_f(n, 1));
        if (n * 12 >= 1)
            return u"≈ %1 a year"_s.arg(py::round_int(n * 12));
        return u"≈ under 1 a year"_s;
    }
    return u"%1 of %2 people"_s.arg(people(n), py::fmt_commas(static_cast<double>(size), 0));
}

std::pair<QString, bool> rule(const QString &head, const Defs &defs)
{
    const QString red = u"<span style='color:#ffb454'>"_s;
    if (SCALES.contains(head)) {
        const QString sc = SCALES.value(head);
        if (!defs.contains(sc))
            return {red + u"defines.lua has no %1 — the people a month can't be said.</span>"_s.arg(sc), false};
        const double s = defs.value(sc);
        const QString base = u"<b>Measured in the game:</b> the cards that hold are added up, the sum is multiplied by "
                             u"the block's own factor, and each month that share of the pop %1: pop × number × %2 (%3 in "
                             u"this mod's defines.lua)"_s.arg(VERB.value(head), sc, num(s));
        if (head == u"promotion_chance")
            return {base + u" × the nation's administrative efficiency. Of 100,000 people at number 1 and full "
                           u"efficiency: %1 a month. If no card holds, nobody is promoted."_s.arg(py::fmt_commas(100000 * s, 0)),
                    true};
        return {base + u". Of 100,000 people at number 1: %1 a month. If no card holds, nobody moves."_s.arg(
                           py::fmt_commas(100000 * s, 0)) + WHO.value(head),
                true};
    }
    if (GUESS.contains(head)) {
        const QString g = GUESS.value(head);
        return {red + u"Not measured in the game yet.</span> The sum of the cards that hold × the block's own factor "
                      u"is the number, as for promotion; how many people that moves (defines.lua has %1 = %2 for it) has "
                      u"not been tried."_s.arg(g, defs.contains(g) ? num(defs.value(g)) : u"—"_s),
                false};
    }
    if (head == u"promote_to")
        return {u"This number is the WEIGHT of this target, not how many move (that is “Being promoted / demoted” "
                u"under Every pop). <b>Measured:</b> each month all the people who move go to ONE target, picked at "
                u"random — the larger its weight against the others, the more often (over the years: shares in "
                u"proportion to the weights). A target none of whose cards hold, or with a weight below 0, is never "
                u"picked; a larger weight does not move more people."_s,
                false};
    if (head == u"migration_target")
        return {u"This number is how strongly a province draws the nation's migrants, against its other provinces. "
                u"<b>Measured:</b> here the cards are MULTIPLIED (its own factor × the product of the cards that "
                u"hold); a province made 1000 times as attractive took in nearly all of them."_s,
                false};
    return {u"This number is a weight against the other targets. "_s + red +
                u"What the game does with it has not been measured.</span>"_s,
            false};
}

}  // namespace popfacts
