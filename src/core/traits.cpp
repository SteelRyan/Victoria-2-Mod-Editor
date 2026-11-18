// traits — see traits.h.
#include "core/traits.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace traits {

const QStringList LISTS{u"personality"_s, u"background"_s};
const QStringList STATS{u"attack"_s,     u"defence"_s,    u"morale"_s,     u"organisation"_s, u"reconnaissance"_s,
                        u"speed"_s,      u"attrition"_s,  u"experience"_s, u"reliability"_s};

QString stat_word(const QString &stat) { return stat; }

bool is_percent(const QString &stat)
{
    return stat == u"morale" || stat == u"organisation" || stat == u"reconnaissance" || stat == u"speed" ||
           stat == u"experience";
}

double Trait::get(const QString &stat) const
{
    const double *p = v.get(stat);
    return p ? *p : 0.0;
}

const Trait *Lists::find(const QString &key) const
{
    for (const auto *l : {&personality, &background})
        for (const Trait &t : *l)
            if (t.key == key)
                return &t;
    return nullptr;
}

QString Lists::list_of(const QString &key) const
{
    for (const Trait &t : personality)
        if (t.key == key)
            return u"personality"_s;
    for (const Trait &t : background)
        if (t.key == key)
            return u"background"_s;
    return {};
}

static std::optional<double> as_num(const QString &s)
{
    bool ok = false;
    const double d = py::strip(s).toDouble(&ok);
    return ok && std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

namespace {
struct Place {                               // a trait in the text
    QString list;
    const script::Node *node;
};
struct Parsed {
    script::Nodes nodes;
    std::vector<const script::Node *> blocks[2];   // the list blocks: personality, background
    std::vector<Place> traits;
};
Parsed parse_nodes(const QString &text)
{
    Parsed p;
    try {
        p.nodes = script::parse(text);
    } catch (...) {
        p.nodes.clear();
    }
    for (const script::Node &n : p.nodes)
        for (int i = 0; i < 2; ++i)
            if (n.has_key && n.is_block && n.key == LISTS[i]) {
                p.blocks[i].push_back(&n);
                for (const script::Node &c : n.kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        p.traits.push_back({LISTS[i], &c});
            }
    return p;
}
Trait trait_of(const QString &text, const script::Node &c)
{
    Trait t;
    t.key = c.key;
    for (const script::Node &k : c.kids) {
        const std::optional<double> d = k.has_key && !k.is_block ? as_num(k.value) : std::nullopt;
        if (d)
            t.v.insert(k.key, *d);
        else
            t.other << py::slice(text, k.kstart, k.vend);
    }
    return t;
}
QString nl_of(const QString &text) { return text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s; }
QString trait_text(const Trait &t, const QString &nl)
{
    QStringList lines{t.key + u" = {"_s};
    for (const auto &[k, v] : t.v)
        lines << u"\t\t%1 = %2"_s.arg(k, number(v));
    for (const QString &o : t.other)
        lines << u"\t\t"_s + o;
    lines << u"\t}"_s;
    return lines.join(nl);
}
using Edit = std::tuple<qsizetype, qsizetype, QString>;
QString apply(QString text, std::vector<Edit> edits)
{
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) {
        return std::get<0>(a) > std::get<0>(b) || (std::get<0>(a) == std::get<0>(b) && std::get<1>(a) > std::get<1>(b));
    });
    for (const auto &[s, e, rep] : edits)
        text = text.left(s) + rep + text.mid(e);
    return text;
}
std::pair<qsizetype, qsizetype> whole_lines(const QString &text, qsizetype s, qsizetype e)
{
    while (s > 0 && (text[s - 1] == u' ' || text[s - 1] == u'\t'))
        --s;
    while (e < text.size() && (text[e] == u' ' || text[e] == u'\t'))
        ++e;
    if (text.mid(e, 2) == u"\r\n")
        e += 2;
    else if (text.mid(e, 1) == u"\n")
        e += 1;
    return {s, e};
}
// a rewrite of the traits in `keys` of `list` by fn(trait)
template <typename F>
QString rewrite(const QString &text, const QString &list, const QSet<QString> &keys, F fn)
{
    const Parsed p = parse_nodes(text);
    const QString nl = nl_of(text);
    std::vector<Edit> edits;
    for (const Place &pl : p.traits) {
        if (pl.list != list || !keys.contains(pl.node->key))
            continue;
        Trait t = trait_of(text, *pl.node);
        const Trait before = t;
        fn(t);
        if (!(t.v == before.v))
            edits.emplace_back(pl.node->kstart, pl.node->vend, trait_text(t, nl));
    }
    return apply(text, edits);
}
// new traits' texts put after the list's last trait (or into its block, or a new block at the end)
QString insert(const QString &text, const QString &list, const QStringList &bodies)
{
    if (bodies.isEmpty())
        return text;
    const Parsed p = parse_nodes(text);
    const QString nl = nl_of(text);
    const script::Node *last = nullptr;
    for (const Place &pl : p.traits)
        if (pl.list == list && (!last || pl.node->vend > last->vend))
            last = pl.node;
    QString joined;
    for (const QString &b : bodies)
        joined += nl + u'\t' + b;
    if (last)
        return apply(text, {{last->vend, last->vend, joined}});
    const int li = LISTS.indexOf(list);
    if (li >= 0 && !p.blocks[li].empty()) {
        const script::Node *b = p.blocks[li].back();
        const qsizetype at = b->vend - 1;      // (the closing brace)
        return apply(text, {{at, at, joined.mid(nl.size()) + nl}});
    }
    QString t = text;
    if (!t.isEmpty() && !t.endsWith(u'\n'))
        t += nl;
    return t + list + u" = {"_s + joined + nl + u"}"_s + nl;
}
}  // namespace

Lists parse(const QString &text)
{
    Lists out;
    const Parsed p = parse_nodes(text);
    for (const Place &pl : p.traits)
        (pl.list == u"background" ? out.background : out.personality).push_back(trait_of(text, *pl.node));
    return out;
}

QString number(double v)
{
    if (std::floor(v) == v && std::fabs(v) < 1e15)
        return QString::number(static_cast<long long>(v));
    QString s = QString::number(v, 'f', 6);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s == u"-0" ? u"0"_s : s;
}

QString summary(const Trait &t)
{
    QStringList parts;
    for (const auto &[k, v] : t.v) {
        if (v == 0)
            continue;
        if (is_percent(k))
            parts << u"%1%2 % %3"_s.arg(v > 0 ? u"+"_s : QString(), number(v * 100), k);
        else
            parts << u"%1%2 %3"_s.arg(v > 0 ? u"+"_s : QString(), number(v), k);
    }
    return parts.isEmpty() ? u"no effect"_s : parts.join(u", "_s);
}

bool is_none(const QString &key) { return key == u"no_personality" || key == u"no_background"; }

long long vkey(double v) { return std::llround(v * 1e6); }
double vval(long long k) { return double(k) / 1e6; }

long long count(const std::vector<Trait> &list, bool with_none)
{
    return std::count_if(list.begin(), list.end(), [&](const Trait &t) { return with_none || !is_none(t.key); });
}

Dist dist(const std::vector<Trait> &list, const QString &stat, bool with_none)
{
    Dist d;
    const long long n = count(list, with_none);
    if (!n)
        return d;
    for (const Trait &t : list)
        if (with_none || !is_none(t.key))
            d[vkey(t.get(stat))] += 1.0 / double(n);
    return d;
}

Dist2 dist2(const std::vector<Trait> &list, const QString &sx, const QString &sy, bool with_none)
{
    Dist2 d;
    const long long n = count(list, with_none);
    if (!n)
        return d;
    for (const Trait &t : list)
        if (with_none || !is_none(t.key))
            d[{vkey(t.get(sx)), vkey(t.get(sy))}] += 1.0 / double(n);
    return d;
}

Dist add(const Dist &a, const Dist &b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    Dist d;
    for (const auto &[x, p] : a)
        for (const auto &[y, q] : b)
            d[x + y] += p * q;
    return d;
}

Dist2 add2(const Dist2 &a, const Dist2 &b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    Dist2 d;
    for (const auto &[x, p] : a)
        for (const auto &[y, q] : b)
            d[{x.first + y.first, x.second + y.second}] += p * q;
    return d;
}

QString set_stat(const QString &text, const QString &list, const QSet<QString> &keys, const QString &stat,
                 const std::optional<double> &value)
{
    return rewrite(text, list, keys, [&](Trait &t) {
        if (!value || *value == 0)
            t.v.remove(stat);
        else
            t.v[stat] = *value;
    });
}

QString add_to_stat(const QString &text, const QString &list, const QSet<QString> &keys, const QString &stat, double by)
{
    return rewrite(text, list, keys, [&](Trait &t) {
        const double v = vval(vkey(t.get(stat) + by));   // (no float noise: 0.1 + 0.05 = 0.15)
        if (v == 0)
            t.v.remove(stat);
        else
            t.v[stat] = v;
    });
}

QString free_key(const Lists &l, const QString &base)
{
    if (!l.find(base))
        return base;
    for (long long i = 2;; ++i)
        if (const QString k = u"%1_%2"_s.arg(base).arg(i); !l.find(k))
            return k;
}

QString copy_traits(const QString &text, const QString &list, const QStringList &keys, long long n,
                    std::vector<std::pair<QString, QString>> *made)
{
    Lists l = parse(text);
    const QString nl = nl_of(text);
    QStringList bodies;
    for (const QString &k : keys) {
        const Trait *t = l.find(k);
        if (!t)
            continue;
        // copies are named after the trait they copy ("aggressive_2"); a copy of a copy after the first one
        QString base = k;
        static const QRegularExpression TAIL(u"_\\d+$"_s);
        if (const auto m = TAIL.match(base); m.hasMatch() && l.find(base.left(m.capturedStart())))
            base = base.left(m.capturedStart());
        for (long long i = 0; i < n; ++i) {
            Trait c = *t;
            c.key = free_key(l, base);
            (list == u"background" ? l.background : l.personality).push_back(c);
            bodies << trait_text(c, nl);
            if (made)
                made->emplace_back(c.key, k);
        }
    }
    return insert(text, list, bodies);
}

QString remove_traits(const QString &text, const QString &list, const QSet<QString> &keys)
{
    const Parsed p = parse_nodes(text);
    std::vector<Edit> edits;
    for (const Place &pl : p.traits)
        if (pl.list == list && keys.contains(pl.node->key)) {
            const auto [s, e] = whole_lines(text, pl.node->kstart, pl.node->vend);
            edits.emplace_back(s, e, QString());
        }
    return apply(text, edits);
}

QString new_trait(const QString &text, const QString &list, const QString &key)
{
    Trait t;
    t.key = key;
    return insert(text, list, {trait_text(t, nl_of(text))});
}

}  // namespace traits
