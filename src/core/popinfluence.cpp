// popinfluence.py, the window-less half — see popinfluence.h.
#include "core/popinfluence.h"
#include "core/cbcheck.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using script::Node;

namespace popinfluence {

const std::vector<std::pair<QString, std::pair<QString, QString>>> PER_TARGET = {
    {u"promote_to"_s, {u"Promotion / demotion"_s, u"becoming %s"_s}},
    {u"ideologies"_s, {u"Ideology"_s, u"leaning towards %s"_s}},
    {u"issues"_s, {u"Issue"_s, u"wanting %s"_s}}};
const std::vector<std::pair<QString, QString>> SINGLE = {
    {u"migration_target"_s, u"Moving inside the country: how much a province draws them"_s},
    {u"country_migration_target"_s, u"Emigrating: how much a nation draws them"_s},
    {u"promotion_chance"_s, u"Being promoted"_s},
    {u"demotion_chance"_s, u"Being demoted"_s},
    {u"migration_chance"_s, u"Moving inside the country"_s},
    {u"colonialmigration_chance"_s, u"Moving to the colonies"_s},
    {u"emigration_chance"_s, u"Emigrating"_s},
    {u"assimilation_chance"_s, u"Taking on the nation's culture (assimilation)"_s},
    {u"conversion_chance"_s, u"Taking on the nation's religion (conversion)"_s}};

const std::pair<QString, QString> *per_target(const QString &head)
{
    for (const auto &[k, v] : PER_TARGET)
        if (k == head)
            return &v;
    return nullptr;
}

const QString *single(const QString &head)
{
    for (const auto &[k, v] : SINGLE)
        if (k == head)
            return &v;
    return nullptr;
}

QString _pretty(const QString &k) { return QString(k).replace(u'_', u' '); }

Span _line_span(const QString &text, qsizetype kstart, qsizetype vend)
{
    qsizetype a = py::rfind(text, u'\n', kstart) + 1;
    if (!py::strip(py::slice(text, a, kstart)).isEmpty())
        a = kstart;
    qsizetype b = text.indexOf(u'\n', vend);
    b = b < 0 ? text.size() : b + 1;
    const QString after = py::strip(py::slice(text, vend, b));
    if (!after.isEmpty() && !after.startsWith(u'#'))
        b = vend;
    return {a, b};
}

std::optional<double> _num(const QString &s) { return py::to_float(py::strip(s)); }

static QString collapse(const QString &s) { return py::split(s).join(u' '); }   // " ".join(s.split())

// ---------------------------------------------------------------------------------------------- the chances
std::vector<Chance> read_chances(scriptstore::ScriptStore &store, const QStringList &files)
{
    std::vector<Chance> out;
    for (const QString &rel : files) {
        QString text;
        script::Nodes nodes;
        try {
            text = store.text(rel);
            nodes = script::parse(text);
        } catch (const std::exception &) {
            continue;
        }
        const QString stem = pypath::splitext_stem(pypath::basename(rel));
        auto chance = [&](const Node &n, const QString &head, const QString &target, const QStringList &path) {
            Chance ch;
            ch.rel = rel;
            ch.pop = stem;
            ch.head = head;
            ch.target = target;
            ch.path = path;
            ch.span = {n.vstart + 1, n.vend - 1};
            auto mod = [&](const Node &m, std::optional<long long> group) -> size_t {
                const Node *f = nullptr;
                for (const Node &x : m.kids)
                    if (x.has_key && x.key == u"factor" && !x.is_block) {
                        f = &x;
                        break;
                    }
                QStringList cond;
                for (const Node &x : m.kids)
                    if (&x != f && x.has_key && !x.key.isEmpty())
                        cond << collapse(text.mid(x.kstart, x.vend - x.kstart));
                Card d;
                d.factor = f ? py::strip(f->value) : QString();
                if (f)
                    d.fspan = Span{f->vstart, f->vend};
                d.span = _line_span(text, m.kstart, m.vend);
                d.node = {m.vstart, m.vend};
                d.src = py::slice(text, m.vstart + 1, m.vend - 1);
                d.cond = cond.join(u", "_s);
                d.group = group;
                ch.mods.push_back(std::move(d));
                return ch.mods.size() - 1;
            };
            long long g = 0;
            QHash<QString, long long> sigs;
            for (const Node &c : n.kids) {
                if (c.has_key && c.key == u"factor" && !c.is_block) {
                    ch.base = py::strip(c.value);
                    ch.bspan = Span{c.vstart, c.vend};
                } else if (c.has_key && c.key == u"modifier" && c.is_block) {
                    mod(c, std::nullopt);
                } else if (c.has_key && c.key == u"group" && c.is_block) {
                    g += 1;
                    std::vector<size_t> ms;
                    for (const Node &m : c.kids)
                        if (m.has_key && m.key == u"modifier" && m.is_block)
                            ms.push_back(mod(m, g));
                    QStringList sp;                  // the group's cards: (factor, cond) each — one text
                    for (size_t i : ms)
                        sp << ch.mods[i].factor + QChar(0x1f) + ch.mods[i].cond;
                    const QString sig = sp.join(QChar(0x1e));
                    const long long k = sigs.value(sig, -1) + 1;
                    sigs.insert(sig, k);
                    for (size_t i : ms)
                        ch.mods[i].gid = sig + QChar(0x1d) + QString::number(k);
                }
            }
            out.push_back(std::move(ch));
        };
        for (const Node &n : nodes) {
            if (!(n.has_key && !n.key.isEmpty() && n.is_block))
                continue;
            if (per_target(n.key)) {
                for (const Node &c : n.kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        chance(c, n.key, c.key, {n.key, c.key});
            } else {
                bool any_mg = false, any_mgf = false;
                for (const Node &x : n.kids) {
                    if (!x.has_key)
                        continue;
                    if (x.key == u"modifier" || x.key == u"group")
                        any_mg = any_mgf = true;
                    else if (x.key == u"factor")
                        any_mgf = true;
                }
                if ((single(n.key) || any_mg) && any_mgf)
                    chance(n, n.key, QString(), {n.key});
            }
        }
    }
    return out;
}

QString modifier_text(const QString &factor, const QString &cond, const QString &indent)
{
    QStringList lines{indent + u"modifier = {"_s, indent + u"\tfactor = "_s + factor};
    for (const QString &ln : py::splitlines(cond))
        if (!py::strip(ln).isEmpty())
            lines << indent + u"\t"_s + py::strip(ln);
    lines << indent + u"}"_s;
    return lines.join(u'\n') + u'\n';
}

std::optional<QString> cond_ok(const QString &cond)
{
    if (py::strip(cond).isEmpty())
        return u"It needs at least one condition (like  literacy = 0.5  or  nationalvalue = nv_order)."_s;
    if (cond.count(u'{') != cond.count(u'}'))
        return u"The { and } don't pair up."_s;
    if (!cbcheck::doubled_equals(cond).empty())     // (a tester's Modus, 2026-10-21: the game crashed on this at start)
        return u"Two = in a row ( … = = … ): the game can't read that line and crashes while it loads — take one out."_s;
    const script::Nodes ns = script::parse(cond);   // (the parser never raises: "can't be read as script" never comes)
    if (ns.empty())
        return u"Every condition is  key = value  or  key = { … }."_s;
    for (const Node &n : ns)
        if (!n.has_key || n.key.isEmpty())
            return u"Every condition is  key = value  or  key = { … }."_s;
    for (const Node &n : ns)
        if (n.key == u"factor")
            return u"The factor has its own box — leave it out here."_s;
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------- step groups
static const QRegularExpression STEP_RE(uR"(^(not = \{ )?([A-Za-z_]+) = (-?\d+(?:\.\d+)?)( \})?$)"_s,
                                        QRegularExpression::CaseInsensitiveOption);
static const QRegularExpression NUM_RE(uR"(-?\d+(?:\.\d+)?)"_s);
static const QRegularExpression KEY_HASH_RE(uR"(([A-Za-z_]+)\s*=\s*#)"_s);
static const QRegularExpression NOT_RE(uR"(^\s*not\s*=\s*\{)"_s, QRegularExpression::CaseInsensitiveOption);

static double fnum(const QString &s) { return py::to_float(s).value_or(0.0); }

std::optional<Ladder> _tmpl_steps(const std::vector<Card> &mods, const std::vector<long long> &idx)
{
    QStringList conds;
    for (long long i : idx)
        conds << collapse(mods[size_t(i)].cond);
    long long n0 = 0;
    for (auto it = NUM_RE.globalMatch(conds[0]); it.hasNext(); it.next())
        n0 += 1;
    for (long long pos = 0; pos < n0; ++pos) {
        QSet<QString> tm;
        std::vector<double> vals;
        for (const QString &c : conds) {
            std::vector<QRegularExpressionMatch> ns;
            for (auto it = NUM_RE.globalMatch(c); it.hasNext();)
                ns.push_back(it.next());
            if (static_cast<long long>(ns.size()) <= pos)
                break;
            const QRegularExpressionMatch &n = ns[size_t(pos)];
            tm.insert(c.left(n.capturedStart()) + u"#"_s + c.mid(n.capturedEnd()));
            vals.push_back(fnum(n.captured()));
        }
        const std::set<double> distinct(vals.begin(), vals.end());
        if (vals.size() != size_t(conds.size()) || tm.size() != 1 || distinct.size() != vals.size())
            continue;
        const QString t = *tm.begin();
        const QRegularExpressionMatch k = KEY_HASH_RE.match(t);
        const bool neg = NOT_RE.match(t).hasMatch();
        std::vector<std::pair<double, long long>> steps;
        for (size_t i = 0; i < vals.size(); ++i)
            steps.emplace_back(vals[i], idx[i]);
        std::sort(steps.begin(), steps.end());
        Ladder best;
        best.key = k.hasMatch() ? py::lower(k.captured(1)) : QString();
        best.neg = neg;
        best.tmpl = t;
        std::vector<double> thr;
        for (const auto &[v, i] : steps) {
            best.idx.push_back(i);
            thr.push_back(v);
        }
        best.thr = thr;
        return best;
    }
    return std::nullopt;
}

std::vector<Ladder> _loose_ladders(const std::vector<Card> &mods, long long least)
{
    struct Found {
        QString key;
        bool neg;
        QString rest;
        std::vector<std::pair<double, long long>> steps;
    };
    std::vector<Found> found;                        // (a dict keyed (key, neg, rest, sign, gid): in its order)
    QHash<QString, size_t> at;
    for (size_t i = 0; i < mods.size(); ++i) {
        const Card &m = mods[i];
        const std::optional<double> v = _num(m.factor);
        if (!v || *v == 0.0)
            continue;
        const QStringList parts = py::split(m.cond, u", "_s);
        for (qsizetype k = 0; k < parts.size(); ++k) {
            const QRegularExpressionMatch h = STEP_RE.match(collapse(parts[k]));
            if (!h.hasMatch() || h.captured(1).isEmpty() != h.captured(4).isEmpty())
                continue;
            QStringList others = parts;
            others.removeAt(k);
            const QString rest = others.join(u", "_s);
            const QString key = py::lower(h.captured(2));
            const bool neg = !h.captured(1).isEmpty();
            const QString fk = key + QChar(0x1f) + (neg ? u"1"_s : u"0"_s) + QChar(0x1f) + rest + QChar(0x1f) +
                               (*v > 0 ? u"1"_s : u"0"_s) + QChar(0x1f) + (m.gid ? u"g"_s + *m.gid : u"-"_s);
            auto it = at.constFind(fk);
            size_t slot;
            if (it == at.constEnd()) {
                slot = found.size();
                at.insert(fk, slot);
                found.push_back({key, neg, rest, {}});
            } else {
                slot = *it;
            }
            found[slot].steps.emplace_back(fnum(h.captured(3)), static_cast<long long>(i));
        }
    }
    std::vector<size_t> order(found.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return found[a].steps.size() > found[b].steps.size(); });
    std::vector<Ladder> out;
    QSet<long long> used;
    for (size_t o : order) {
        const Found &f = found[o];
        std::vector<std::pair<double, long long>> steps;
        for (const auto &si : f.steps)
            if (!used.contains(si.second))
                steps.push_back(si);
        std::sort(steps.begin(), steps.end());
        std::set<double> distinct;
        for (const auto &si : steps)
            distinct.insert(si.first);
        if (static_cast<long long>(steps.size()) >= least && distinct.size() == steps.size()) {
            Ladder L;
            L.key = f.key;
            L.neg = f.neg;
            L.rest = f.rest;
            std::vector<double> thr;
            for (const auto &[s, i] : steps) {
                used.insert(i);
                L.idx.push_back(i);
                thr.push_back(s);
            }
            L.thr = thr;
            out.push_back(std::move(L));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Ladder &a, const Ladder &b) {
        return *std::min_element(a.idx.begin(), a.idx.end()) < *std::min_element(b.idx.begin(), b.idx.end());
    });
    return out;
}

std::vector<Ladder> ladders(const std::vector<Card> &mods)
{
    std::vector<Ladder> out;
    QSet<long long> used;
    std::vector<std::pair<QString, std::vector<long long>>> groups;   // (gid, raising?) -> card numbers, in order
    QHash<QString, size_t> at;
    for (size_t i = 0; i < mods.size(); ++i) {
        const std::optional<double> v = _num(mods[i].factor);
        if (v && *v != 0.0 && mods[i].gid) {
            const QString k = *mods[i].gid + QChar(0x1c) + (*v > 0 ? u"1"_s : u"0"_s);
            auto it = at.constFind(k);
            if (it == at.constEnd()) {
                at.insert(k, groups.size());
                groups.push_back({k, {static_cast<long long>(i)}});
            } else {
                groups[*it].second.push_back(static_cast<long long>(i));
            }
        }
    }
    for (const auto &[_k, idx] : groups) {
        if (idx.size() < 2)
            continue;
        std::vector<Card> sub;                       // (the step guess on the group's own cards)
        for (long long i : idx) {
            Card c = mods[size_t(i)];
            c.gid.reset();
            sub.push_back(std::move(c));
        }
        std::optional<Ladder> lad;
        for (Ladder &L : _loose_ladders(sub, 2))
            if (L.idx.size() == idx.size()) {
                lad = std::move(L);
                break;
            }
        if (lad) {
            for (long long &k : lad->idx)
                k = idx[size_t(k)];
        } else {
            lad = _tmpl_steps(mods, idx);
        }
        if (!lad) {
            lad = Ladder();
            lad->kind = u"group"_s;
            lad->idx = idx;
        }
        if (lad->kind.isEmpty())
            lad->kind = u"steps"_s;
        lad->grouped = true;
        for (long long i : lad->idx)
            used.insert(i);
        out.push_back(std::move(*lad));
    }
    std::vector<Card> rest = mods;
    for (size_t i = 0; i < rest.size(); ++i)
        if (used.contains(static_cast<long long>(i)))
            rest[i].factor = u"0"_s;
    for (Ladder &L : _loose_ladders(rest, 3)) {
        L.kind = u"steps"_s;
        L.grouped = false;
        out.push_back(std::move(L));
    }
    std::stable_sort(out.begin(), out.end(), [](const Ladder &a, const Ladder &b) {
        return *std::min_element(a.idx.begin(), a.idx.end()) < *std::min_element(b.idx.begin(), b.idx.end());
    });
    return out;
}

std::vector<long long> ladder_ticks(const Ladder &lad, long long j)
{
    const auto n = static_cast<qsizetype>(lad.idx.size());
    if (lad.neg)                                     // idx[j:] / idx[:j + 1], Python's slices
        return std::vector<long long>(lad.idx.begin() + py::py_index(qsizetype(j), n), lad.idx.end());
    return std::vector<long long>(lad.idx.begin(), lad.idx.begin() + py::py_index(qsizetype(j + 1), n));
}

}  // namespace popinfluence
