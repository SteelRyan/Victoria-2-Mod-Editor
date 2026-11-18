// popsim.py — the sample province of Pop influences. Line by line.
#include "core/popsim.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace popsim {

const long long SIZE = 100000;
const QHash<QString, int> STRATA = {{u"poor"_s, 0}, {u"middle"_s, 1}, {u"rich"_s, 2}};
const std::vector<std::pair<long long, QString>> TIMES = {
    {1, u"1 month"_s}, {12, u"1 year"_s}, {60, u"5 years"_s}, {120, u"10 years"_s}, {300, u"25 years"_s}};
const double FOCUS_SHARE = 0.9;      // measured: a focus of 0.15 raised the promotion number by 0.13 – 0.14 (two runs)

OrderedMap<QString, long long> whole_shares(const OrderedMap<QString, long long> &counts, long long total)
{
    // {key: whole per cent} of counts, adding up to `total` (largest remainders)
    long long sum = 0;
    for (const auto &[_k, v] : counts)
        sum += v;
    const double s = sum ? static_cast<double>(sum) : 1.0;
    OrderedMap<QString, double> raw;
    OrderedMap<QString, long long> out;
    for (const auto &[k, v] : counts) {
        raw.insert(k, static_cast<double>(v * total) / s);
        out.insert(k, py::trunc_int(raw.value(k)));
    }
    long long got = 0;
    for (const auto &[_k, v] : out)
        got += v;
    const long long left = total - got;
    QStringList keys;
    for (const auto &[k, _v] : raw)
        keys << k;
    std::stable_sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b) {
        return -(raw.value(a) - static_cast<double>(out.value(a))) < -(raw.value(b) - static_cast<double>(out.value(b)));
    });
    const qsizetype take = left >= 0 ? std::min<qsizetype>(left, keys.size()) : std::max<qsizetype>(0, keys.size() + left);
    for (qsizetype i = 0; i < take; ++i)
        out[keys[i]] += 1;
    return out;
}

OrderedMap<QString, long long> sample(const document::Document *doc, const moddata::Mod &, const QStringList &types)
{
    // The sample province: {pop type: people} — the mod's pops by type, in whole per cent of 100,000.
    OrderedMap<QString, long long> counts;
    if (doc)
        for (const auto &[_pid, pops] : doc->pops)
            for (const moddata::Pop &p : pops)
                counts[p.type] += p.size;
    OrderedMap<QString, long long> kept;
    for (const auto &[k, v] : counts)
        if (types.contains(k))
            kept.insert(k, v);
    if (kept.isEmpty())
        for (const QString &t : types)
            kept.insert(t, 1);
    OrderedMap<QString, long long> out;
    for (const auto &[k, v] : whole_shares(kept))
        out.insert(k, py::floordiv(v * SIZE, 100LL));
    return out;
}

namespace {
struct Step {
    double rate;
    Weights tg;
};
struct PlanEntry {
    std::vector<Step> steps;
    double out_rate;
};
}  // namespace

Result run(const OrderedMap<QString, double> &start, const QHash<QString, QString> &strata, double months, const NArg &n,
           const WArg &weights, const popfacts::Defs &defs, double eff,
           const std::optional<std::pair<QString, double>> &focus, long long every)
{
    const double ps = defs.value(u"PROMOTION_SCALE"_s, 0.0);
    const double ims = defs.value(u"IMMIGRATION_SCALE"_s, 0.0);
    OrderedMap<QString, double> cur = start;
    OrderedMap<std::pair<QString, QString>, double> moved;
    double left = 0.0;
    const bool live = std::holds_alternative<NFn>(n) || std::holds_alternative<WFn>(weights);
    auto cls = [&](const QString &t) { return STRATA.value(strata.value(t, u"poor"_s), 0); };

    auto make_plan = [&]() {
        py::Sum tsum;
        for (const auto &[_k, v] : cur)
            if (v > 0)
                tsum.add(v);
        const double tot = tsum.value() != 0.0 ? tsum.value() : 1.0;
        Shares shares;
        for (const auto &[k, v] : cur)
            shares.insert(k, std::max(0.0, v) / tot);
        OrderedMap<QString, PlanEntry> plan;
        QStringList keys;
        for (const auto &[k, _v] : cur)
            keys << k;
        for (const QString &t : keys) {
            if (cur.value(t) <= 0)
                continue;
            const Numbers nt = std::holds_alternative<NFn>(n) ? std::get<NFn>(n)(t, shares) : std::get<Numbers>(n);
            const Weights wt = std::holds_alternative<WFn>(weights) ? std::get<WFn>(weights)(t, shares)
                                                                    : std::get<QHash<QString, Weights>>(weights).value(t);
            auto of = [&](const QString &key) {    // a number: one for all, or {pop type: number}
                const NVal v = nt.value(key);
                return std::max(0.0, v.is_map ? v.m.value(t, 0.0) : v.x);
            };
            Weights w;
            for (const auto &[k, v] : wt)
                if (v > 0 && k != t)
                    w.insert(k, v);
            const int s = cls(t);
            Weights up, down;
            for (const auto &[k, v] : w) {
                if (cls(k) >= s)
                    up.insert(k, v);
                if (cls(k) <= s)
                    down.insert(k, v);
            }
            std::vector<Step> steps;
            double pn = of(u"promotion"_s);
            if (focus && pn > 0) {                   // a national focus on a pop type (measured): the number a bit
                const auto &[ft, fv] = *focus;       // higher, and ALL the promoted become that type — unless its
                pn += FOCUS_SHARE * fv;              // weight stays below 0
                if (ft != t && wt.contains(ft) && cls(ft) >= s && wt.value(ft) + fv > 0) {
                    up = Weights();
                    up.insert(ft, 1.0);
                }
            }
            if (!up.isEmpty() && pn > 0)
                steps.push_back({pn * ps * eff, up});
            if (!down.isEmpty() && of(u"demotion"_s) > 0)
                steps.push_back({of(u"demotion"_s) * ps, down});
            plan.insert(t, {steps, of(u"migration"_s) + of(u"emigration"_s) + of(u"colonial"_s)});
        }
        return plan;
    };
    OrderedMap<QString, PlanEntry> plan = make_plan();
    const long long nm = py::trunc_int(months);
    for (long long m = 0; m < nm; ++m) {
        if (live && m && py::mod(m, every) == 0)
            plan = make_plan();
        OrderedMap<QString, double> delta;
        for (const auto &[t, size] : cur) {
            if (size <= 0 || !plan.contains(t))
                continue;
            const PlanEntry &pe = *plan.get(t);
            if (pe.out_rate != 0.0) {
                const double go = std::min(size, size * pe.out_rate * ims);
                delta.insert(t, delta.value(t, 0.0) - go);
                left += go;
            }
            for (const Step &st : pe.steps) {     // (both are taken from the pop as it stood at the month's start)
                const double go = std::min(size, size * st.rate);
                py::Sum ts;
                for (const auto &[_k, v] : st.tg)
                    ts.add(v);
                const double tot = ts.value();
                delta.insert(t, delta.value(t, 0.0) - go);
                for (const auto &[k, v] : st.tg) {
                    const double part = go * v / tot;
                    delta.insert(k, delta.value(k, 0.0) + part);
                    moved.insert({t, k}, moved.value({t, k}, 0.0) + part);
                }
            }
        }
        for (const auto &[k, v] : delta)
            cur.insert(k, cur.value(k, 0.0) + v);
    }
    return {cur, left, moved};
}

namespace {
struct GPlan {
    std::vector<Step> steps;
    double out_rate;
    std::optional<std::pair<Group, double>> a, c;
};
}  // namespace

GResult run_groups(const OrderedMap<GKey, double> &start, const QHash<QString, QString> &strata, double months,
                   const GNFn &n, const GWFn &weights, const popfacts::Defs &defs, double eff,
                   const std::optional<std::pair<QString, double>> &focus, const Turn &turn, long long every)
{
    const double ps = defs.value(u"PROMOTION_SCALE"_s, 0.0);
    const double ims = defs.value(u"IMMIGRATION_SCALE"_s, 0.0);
    const double ass = defs.value(u"ASSIMILATION_SCALE"_s, 0.0);
    const double cvs = defs.value(u"CONVERSION_SCALE"_s, 0.0);
    OrderedMap<GKey, double> cur = start;
    OrderedMap<std::pair<QString, QString>, double> moved;
    OrderedMap<std::pair<Group, Group>, double> changed;
    double left = 0.0;
    auto cls = [&](const QString &t) { return STRATA.value(strata.value(t, u"poor"_s), 0); };

    auto make_plan = [&]() {
        OrderedMap<QString, double> by_type;
        for (const auto &[k, v] : cur)
            by_type.insert(k.first, by_type.value(k.first, 0.0) + std::max(0.0, v));
        py::Sum tsum;
        for (const auto &[_k, v] : by_type)
            tsum.add(v);
        const double tot = tsum.value() != 0.0 ? tsum.value() : 1.0;
        Shares shares;
        for (const auto &[k, v] : by_type)
            shares.insert(k, v / tot);
        OrderedMap<GKey, GPlan> plan;
        std::vector<GKey> keys;
        for (const auto &[k, _v] : cur)
            keys.push_back(k);
        for (const GKey &key : keys) {
            const auto &[t, g] = key;
            if (cur.value(key) <= 0)
                continue;
            const QHash<QString, double> nt = n(t, g, shares);
            const Weights wt = weights(t, g, shares);
            auto of = [&](const QString &k) { return std::max(0.0, nt.value(k, 0.0)); };
            Weights w;
            for (const auto &[k, v] : wt)
                if (v > 0 && k != t)
                    w.insert(k, v);
            const int s = cls(t);
            Weights up, down;
            for (const auto &[k, v] : w) {
                if (cls(k) >= s)
                    up.insert(k, v);
                if (cls(k) <= s)
                    down.insert(k, v);
            }
            std::vector<Step> steps;
            double pn = of(u"promotion"_s);
            if (focus && pn > 0) {                   // (as in run())
                const auto &[ft, fv] = *focus;
                pn += FOCUS_SHARE * fv;
                if (ft != t && wt.contains(ft) && cls(ft) >= s && wt.value(ft) + fv > 0) {
                    up = Weights();
                    up.insert(ft, 1.0);
                }
            }
            if (!up.isEmpty() && pn > 0)
                steps.push_back({pn * ps * eff, up});
            if (!down.isEmpty() && of(u"demotion"_s) > 0)
                steps.push_back({of(u"demotion"_s) * ps, down});
            std::optional<Group> to_a, to_c;
            if (turn)
                std::tie(to_a, to_c) = turn(g);
            GPlan gp{steps, of(u"migration"_s) + of(u"emigration"_s) + of(u"colonial"_s), std::nullopt, std::nullopt};
            if (to_a)
                gp.a = std::make_pair(*to_a, of(u"assimilation"_s) * ass);
            if (to_c && !to_a)
                gp.c = std::make_pair(*to_c, of(u"conversion"_s) * cvs);
            plan.insert(key, gp);
        }
        return plan;
    };
    OrderedMap<GKey, GPlan> plan = make_plan();
    const long long nm = py::trunc_int(months);
    for (long long m = 0; m < nm; ++m) {
        if (m && py::mod(m, every) == 0)
            plan = make_plan();
        OrderedMap<GKey, double> delta;
        for (const auto &[key, size] : cur) {
            if (size <= 0 || !plan.contains(key))
                continue;
            const auto &[t, g] = key;
            const GPlan &pe = *plan.get(key);
            if (pe.out_rate != 0.0) {
                const double go = std::min(size, size * pe.out_rate * ims);
                delta.insert(key, delta.value(key, 0.0) - go);
                left += go;
            }
            for (const Step &st : pe.steps) {
                const double go = std::min(size, size * st.rate);
                py::Sum ts;
                for (const auto &[_k, v] : st.tg)
                    ts.add(v);
                const double tot = ts.value();
                delta.insert(key, delta.value(key, 0.0) - go);
                for (const auto &[k, v] : st.tg) {
                    const double part = go * v / tot;
                    const GKey to{k, g};
                    delta.insert(to, delta.value(to, 0.0) + part);
                    moved.insert({t, k}, moved.value({t, k}, 0.0) + part);
                }
            }
            // (assimilation starts in the second month — measured)
            for (const auto *turn_to : {m ? &pe.a : nullptr, &pe.c}) {
                if (turn_to && *turn_to && (*turn_to)->second > 0) {
                    const double go = std::min(size, size * (*turn_to)->second);
                    const GKey to{t, (*turn_to)->first};
                    delta.insert(key, delta.value(key, 0.0) - go);
                    delta.insert(to, delta.value(to, 0.0) + go);
                    const std::pair<Group, Group> ck{g, (*turn_to)->first};
                    changed.insert(ck, changed.value(ck, 0.0) + go);
                }
            }
        }
        for (const auto &[k, v] : delta)
            cur.insert(k, cur.value(k, 0.0) + v);
    }
    return {cur, left, moved, changed};
}

bool class_ok(const QString &cond, const QString &strata)
{
    // False when a card's conditions ask for another class than `strata` ( strata = rich , NOT = { strata = poor } )
    script::Nodes nodes;
    try {
        nodes = script::parse(QString(cond).replace(u", "_s, u"\n"_s));
    } catch (...) {
        return true;
    }
    std::function<bool(const script::Nodes &, bool)> walk = [&](const script::Nodes &ns, bool neg) {
        for (const script::Node &nd : ns) {
            const QString key = py::lower(nd.has_key ? nd.key : u"None"_s);
            if (nd.is_block) {
                if (!walk(nd.kids, neg ^ (key == u"not")))
                    return false;
            } else if (key == u"strata") {
                if ((py::lower(py::strip(nd.value)) == strata) == neg)
                    return false;
            }
        }
        return true;
    };
    return walk(nodes, false);
}

double decay(double people, double number, double scale, double months)
{
    // People of a group left after `months` when each month  number × scale  of them change over.
    const double r = std::max(0.0, std::min(1.0, std::max(0.0, number) * scale));
    return people * std::pow(1.0 - r, static_cast<double>(py::trunc_int(months)));
}

}  // namespace popsim
