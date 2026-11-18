// oob.py — orders of battle (history/units). Line by line.
#include "core/oob.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace oob {

using pypath::join;
using script::Node;
using script::Nodes;

const QStringList LEVELS = {u"Hostile"_s, u"Opposed"_s, u"Neutral"_s, u"Cordial"_s, u"Friendly"_s, u"In sphere"_s};
const std::vector<std::tuple<int, int, int>> ARMY_COLORS = {
    {236, 92, 84},  {82, 156, 240}, {96, 196, 110},  {240, 184, 64},  {178, 116, 230}, {64, 206, 204},
    {244, 134, 196}, {170, 206, 72}, {246, 138, 60}, {140, 150, 255}, {220, 220, 220}, {120, 214, 160}};

QString ordinal(long long n)
{
    const long long h = py::mod(n, 100LL);
    if (10 <= h && h <= 20)
        return u"%1th"_s.arg(n);
    const long long t = py::mod(n, 10LL);
    return u"%1%2"_s.arg(n).arg(t == 1 ? u"st"_s : t == 2 ? u"nd"_s : t == 3 ? u"rd"_s : u"th"_s);
}

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static OptInt digits_int(const OptStr &v)    // int(v) if str(v).isdigit() else None
{
    return (v && py::isdigit(*v)) ? OptInt(py::to_int(*v)) : std::nullopt;
}

// ---------------------------------------------------------------------- unit types
const QHash<QString, long long> UnitTypes::ORDER = {{u"infantry"_s, 0}, {u"support"_s, 1}, {u"cavalry"_s, 2},
                                                    {u"special"_s, 3},  {u"transport"_s, 10}, {u"light_ship"_s, 11},
                                                    {u"big_ship"_s, 12}};

UnitTypes::UnitTypes(moddata::Mod &mod_) : mod(mod_)
{
    QStringList dirs;
    if (mod.base && !mod.base->isEmpty())
        dirs << join(*mod.base, u"units"_s);
    dirs << join(mod.root, u"units"_s);           // the mod's files win (same file name)
    OrderedMap<QString, QString> files;
    for (const QString &d : dirs)
        if (fs::isdir(d))
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".txt"))
                    files.insert(py::lower(fn), join(d, fn));
    for (const auto &[_k, path] : files) {
        Nodes nodes;
        try {
            nodes = script::parse(script::read_text(path));
        } catch (const fs::OSError &) {
            continue;
        }
        for (const Node &n : nodes) {
            if (!(has(n) && n.is_block))
                continue;
            OrderedMap<QString, QString> f;
            for (const Node &c : n.kids)
                if (has(c) && !c.is_block)
                    f.insert(c.key, c.value);
            const QString *t = f.get(u"type"_s);
            if (t && (*t == u"land" || *t == u"naval")) {
                const QString *l = mod.loc.get(n.key);
                types.insert(n.key, UnitType{*t == u"naval", f.value(u"unit_type"_s),
                                             (l && !l->isEmpty()) ? *l : py::title(QString(n.key).replace(u'_', u' '))});
            }
        }
    }
}

QStringList UnitTypes::land() const
{
    QStringList out;
    for (const auto &[k, v] : types)
        if (!v.naval)
            out << k;
    std::stable_sort(out.begin(), out.end(), [&](const QString &a, const QString &b) {
        const UnitType &x = *types.get(a), &y = *types.get(b);
        return std::make_pair(ORDER.value(x.unit_type, 5), x.name) < std::make_pair(ORDER.value(y.unit_type, 5), y.name);
    });
    return out;
}

QStringList UnitTypes::naval() const
{
    QStringList out;
    for (const auto &[k, v] : types)
        if (v.naval)
            out << k;
    std::stable_sort(out.begin(), out.end(), [&](const QString &a, const QString &b) {
        const UnitType &x = *types.get(a), &y = *types.get(b);
        return std::make_pair(ORDER.value(x.unit_type, 15), x.name) < std::make_pair(ORDER.value(y.unit_type, 15), y.name);
    });
    return out;
}

QString UnitTypes::name(const OptStr &key) const
{
    if (key)
        if (const UnitType *t = types.get(*key))
            return t->name;
    return py::title(QString((key && !key->isEmpty()) ? *key : u"?"_s).replace(u'_', u' '));
}

QString UnitTypes::word(const OptStr &key) const
{
    // re.sub(r"_(offensive|defensive)$", "", key or "")
    QString base = key.value_or(QString());
    for (const QString &s : {u"_offensive"_s, u"_defensive"_s}) {
        if (base.endsWith(s)) {
            base.chop(s.size());
            break;
        }
        if (base.endsWith(s + u'\n')) {           // ($ also before a last "\n")
            base = base.left(base.size() - s.size() - 1) + u'\n';
            break;
        }
    }
    return types.contains(base) ? name(base) : name(key);
}

// ---------------------------------------------------------------------- brigade slots
static Num as_num(double v) { return Num{v, std::floor(v) == v && std::isfinite(v)}; }

SlotRules::SlotRules(const moddata::Mod &mod)
{
    QStringList texts;
    for (const OptStr &root : {OptStr(mod.root), mod.base}) {
        if (!root || root->isEmpty())
            continue;
        const QString path = join(*root, {u"common"_s, u"defines.lua"_s});
        if (fs::isfile(path)) {
            QString t = script::read_text(path);
            static const QRegularExpression LUA_COMMENT(u"--[^\\n]*"_s);   // (without the Lua comments)
            texts << t.replace(LUA_COMMENT, QString());
        }
    }
    const std::vector<std::tuple<Num *, QString, double>> KEYS = {
        {&first, u"POP_MIN_SIZE_FOR_REGIMENT"_s, 1000},
        {&per, u"POP_SIZE_PER_REGIMENT"_s, 3000},
        {&noncore, u"POP_MIN_SIZE_FOR_REGIMENT_NONCORE_MULTIPLIER"_s, 3},
        {&colony, u"POP_MIN_SIZE_FOR_REGIMENT_COLONY_MULTIPLIER"_s, 5},
        {&protectorate, u"POP_MIN_SIZE_FOR_REGIMENT_PROTECTORATE_MULTIPLIER"_s, 8}};
    for (const auto &[attr, key, usual] : KEYS) {
        std::optional<double> val;
        const QRegularExpression rx(u"\\b"_s + key + u"\\s*=\\s*([\\d.]+)"_s, QRegularExpression::UseUnicodePropertiesOption);
        for (const QString &text : texts) {
            const auto m = rx.match(text);
            if (m.hasMatch()) {
                const auto f = py::to_float(m.captured(1));
                if (!f)
                    throw py::ValueError(("could not convert string to float: '" + m.captured(1) + "'").toStdString());
                val = *f;
                break;
            }
        }
        if (!val) {
            missing << key;
            val = usual;
        }
        *attr = as_num(*val);
    }
}

Num SlotRules::multiplier(long long colonial, bool is_core) const
{
    // The one multiplier of a province: its colonial level (0 none, 1 protectorate, 2 colony) and whether its owner
    // has a core on it.
    if (colonial == 1)
        return protectorate;
    if (colonial)
        return colony;
    return is_core ? Num{1, true} : noncore;
}

// Python's float // float (floatobject.c float_floor_div)
static double float_floordiv(double vx, double wx)
{
    double mod = std::fmod(vx, wx);
    double div = (vx - mod) / wx;
    if (mod != 0.0) {
        if ((wx < 0) != (mod < 0)) {
            mod += wx;
            div -= 1.0;
        }
    }
    double floordiv;
    if (div != 0.0) {
        floordiv = std::floor(div);
        if (div - floordiv > 0.5)
            floordiv += 1.0;
    } else {
        floordiv = std::copysign(0.0, vx / wx);
    }
    return floordiv;
}

long long SlotRules::brigades(long long soldiers, const Num &mult) const
{
    // Brigades ONE soldier pop of this size raises (in a province with this multiplier).
    const long long s = soldiers;
    const Num m = mult.v != 0 ? mult : Num{1, true};             // (multiplier or 1)
    const bool ints = per.is_int && m.is_int;
    const double step = per.v * m.v;
    if (double(s) < first.v || step <= 0)
        return 0;
    if (ints)
        return 1 + py::floordiv(s, static_cast<long long>(step));
    return 1 + py::trunc_int(float_floordiv(double(s), step));
}

// ---------------------------------------------------------------------- one OOB file
static OrderedMap<QString, QString> _fields(const Nodes &nodes)
{
    OrderedMap<QString, QString> f;
    for (const Node &c : nodes)
        if (has(c) && !c.is_block)
            f.insert(c.key, c.value);
    return f;
}
static OptStr get(const OrderedMap<QString, QString> &f, const QString &k)
{
    const QString *p = f.get(k);
    return p ? OptStr(*p) : std::nullopt;
}

OOB empty_oob() { return OOB{}; }

ArmyKey _army_key(const Army &a)
{
    ArmyKey k{a.name, a.location, {}, a.extra};   // (its general is in `extra`)
    for (const Regiment &r : a.regiments)
        k.regs.emplace_back(r.name, r.type, r.home);
    return k;
}

NavyKey _navy_key(const Navy &n)
{
    NavyKey k{n.name, n.location, {}, n.extra};
    for (const Ship &s : n.ships)
        k.ships.emplace_back(s.name, s.type);
    return k;
}

RelKey _rel_key(const Relation &r) { return {r.value, r.level, r.influence_value}; }

const QStringList LEADER_KEYS{u"name"_s, u"date"_s, u"type"_s, u"personality"_s, u"background"_s, u"prestige"_s};

QString Leader::get(const QString &k, const QString &dflt) const
{
    for (const auto &[key, v] : fields)
        if (key == k)
            return v;
    return dflt;
}

void Leader::set(const QString &k, const QString &v)
{
    for (auto &[key, val] : fields)
        if (key == k) {
            val = v;
            return;
        }
    const qsizetype rank = LEADER_KEYS.indexOf(k);
    auto at = fields.end();
    if (rank >= 0)
        for (auto it = fields.begin(); it != fields.end(); ++it)
            if (const qsizetype r = LEADER_KEYS.indexOf(it->first); r < 0 || r > rank) {
                at = it;
                break;
            }
    fields.insert(at, {k, v});
}

LeaderKey _leader_key(const Leader &l) { return {l.fields, l.extra}; }

OOBKey oob_key(const OOB &o)
{
    // (an army without brigades / a navy without ships is not written, so it is no change either)
    OOBKey k;
    for (const Army &a : o.armies)
        if (!a.regiments.empty() || a.src)
            k.armies.push_back(_army_key(a));
    for (const Navy &n : o.navies)
        if (!n.ships.empty() || n.src)
            k.navies.push_back(_navy_key(n));
    for (const auto &[t, r] : o.relations)
        k.relations.emplace_back(t, _rel_key(r));
    std::sort(k.relations.begin(), k.relations.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });   // (tags are unique)
    for (const Leader &l : o.leaders)
        k.leaders.push_back(_leader_key(l));
    return k;
}

QStringList _comments(const QString &text, const Node &n)
{
    // The comment lines standing directly inside a block (not inside its own sub-blocks): kept with the unit's other
    // unmanaged text, so rewriting the unit doesn't lose them.
    QStringList out;
    long long depth = 0;
    for (const QString &ln : py::slice(text, n.vstart + 1, n.vend - 1).split(u'\n')) {
        const QString s = py::strip(ln);
        if (depth == 0 && s.startsWith(u'#'))
            out << s;
        const QString code = s.split(u'#').first();
        depth += code.count(u'{') - code.count(u'}');
    }
    return out;
}

OOB parse_oob(const QString &text, const QSet<QString> &tags)
{
    // -> oob; each army / navy / relation remembers its source span (`src`) and the text of anything inside it the
    // editor doesn't manage (`extra`, e.g. a leader block).
    OOB out;
    for (const Node &n : script::parse(text)) {
        if (is(n, u"army"_s) && n.is_block) {
            const auto f = _fields(n.kids);
            Army a;
            for (const Node &c : n.kids) {
                if (is(c, u"regiment"_s) && c.is_block) {
                    const auto g = _fields(c.kids);
                    a.regiments.push_back({g.value(u"name"_s), g.value(u"type"_s, u"infantry"_s), digits_int(get(g, u"home"_s))});
                } else if (!(is(c, u"name"_s) || is(c, u"location"_s))) {
                    a.extra << py::slice(text, c.kstart, c.vend);
                }
            }
            a.extra += _comments(text, n);
            a.name = f.value(u"name"_s);
            a.location = digits_int(get(f, u"location"_s));
            a.src = Span{n.kstart, n.vend};
            out.armies.push_back(a);
        } else if (is(n, u"navy"_s) && n.is_block) {
            const auto f = _fields(n.kids);
            Navy v;
            for (const Node &c : n.kids) {
                if (is(c, u"ship"_s) && c.is_block) {
                    const auto g = _fields(c.kids);
                    v.ships.push_back({g.value(u"name"_s), g.value(u"type"_s)});
                } else if (!(is(c, u"name"_s) || is(c, u"location"_s))) {
                    v.extra << py::slice(text, c.kstart, c.vend);
                }
            }
            v.extra += _comments(text, n);
            v.name = f.value(u"name"_s);
            v.location = digits_int(get(f, u"location"_s));
            v.src = Span{n.kstart, n.vend};
            out.navies.push_back(v);
        } else if (is(n, u"leader"_s) && n.is_block) {
            Leader l;
            for (const Node &c : n.kids) {
                if (has(c) && !c.is_block)
                    l.fields.emplace_back(c.key, c.value);
                else
                    l.extra << py::slice(text, c.kstart, c.vend);
            }
            l.extra += _comments(text, n);
            l.src = Span{n.kstart, n.vend};
            out.leaders.push_back(l);
        } else if (has(n) && n.is_block && (tags.contains(n.key) || countryhist::tag_ok(n.key))) {
            const auto f = _fields(n.kids);
            if (f.contains(u"value"_s) || f.contains(u"level"_s) || f.contains(u"influence_value"_s))
                out.relations.insert(n.key, Relation{get(f, u"value"_s), get(f, u"level"_s), get(f, u"influence_value"_s),
                                                     Span{n.kstart, n.vend}, std::nullopt});
        }
    }
    for (Army &a : out.armies)
        a.orig = _army_key(a);
    for (Navy &v : out.navies)
        v.orig = _navy_key(v);
    for (auto &[_t, r] : out.relations)
        r.orig = _rel_key(r);
    for (Leader &l : out.leaders)
        l.orig = _leader_key(l);
    return out;
}

QString army_text(const Army &a, const QString &nl)
{
    QStringList lines = {u"army = {"_s, u"\tname = \"%1\""_s.arg(a.name)};
    if (a.location)
        lines << u"\tlocation = %1"_s.arg(*a.location);
    for (const QString &e : a.extra)
        lines << u'\t' + e;
    for (size_t i = 0; i < a.regiments.size(); ++i) {
        const Regiment &r = a.regiments[i];
        lines << (i < 2 ? QString() : u"\t"_s);   // the mod's files: blank, blank, then tab-only lines
        lines << u"\tregiment = {"_s << u"\t\tname= \"%1\""_s.arg(r.name) << u"\t\ttype = "_s + r.type;
        if (r.home)
            lines << u"\t\thome = %1"_s.arg(*r.home);
        lines << u"\t}"_s;
    }
    lines << u"}"_s;
    return lines.join(nl);
}

QString navy_text(const Navy &n, const QString &nl)
{
    QStringList lines = {u"navy = {"_s, u"\tname = \"%1\""_s.arg(n.name)};
    if (n.location)
        lines << u"\tlocation = %1"_s.arg(*n.location);
    for (const QString &e : n.extra)
        lines << u'\t' + e;
    if (!n.ships.empty())
        lines << u"\t"_s;
    for (const Ship &s : n.ships)
        lines << u"\tship = { name= \"%1\" type = %2 }"_s.arg(s.name, s.type);
    lines << u"}"_s;
    return lines.join(nl);
}

QString relation_text(const QString &tag, const Relation &r, const QString &nl)
{
    if (!r.level && !r.influence_value)
        return u"%1 = { value = %2 }"_s.arg(tag, (r.value && !r.value->isEmpty()) ? *r.value : u"0"_s);
    QStringList lines{tag + u" = {"_s};
    if (r.value)
        lines << u"\tvalue = %1\t\t#relation"_s.arg(*r.value);
    if (r.level)
        lines << u"\tlevel = %1\t\t#3=cordial, 4 = friendly, 5 sphere"_s.arg(*r.level);
    if (r.influence_value)
        lines << u"\tinfluence_value = "_s + *r.influence_value;
    lines << u"}"_s;
    return lines.join(nl);
}

QString leader_block_text(const Leader &l, const QString &nl)
{
    // the base game's way: one tab in, the name in quotes, the rest as they are
    QStringList lines{u"leader = {"_s};
    for (const auto &[k, v] : l.fields)
        lines << (k == u"name" ? u"\t%1 = \"%2\""_s.arg(k, QString(v).remove(u'"')) : u"\t%1 = %2"_s.arg(k, v));
    for (const QString &e : l.extra)
        lines << u'\t' + e;
    lines << u"}"_s;
    return lines.join(nl);
}

static qsizetype trailing_newline_run(const QString &t)
{
    // re.search(r"(\r?\n[ \t]*)+$", t).start(), or -1: the start of the newlines (and their indents) the text ends with
    qsizetype j = t.size(), start = -1;
    while (true) {
        qsizetype k = j;
        while (k > 0 && (t[k - 1] == u' ' || t[k - 1] == u'\t'))
            --k;
        if (k > 0 && t[k - 1] == u'\n') {
            k -= 1;
            if (k > 0 && t[k - 1] == u'\r')
                k -= 1;
            start = k;
            j = k;
        } else {
            break;
        }
    }
    return start;
}

QString write_oob(QString text, const OOB &want)
{
    // The file's text with `want` applied (unchanged items keep their exact text).
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QSet<QString> tags;
    for (const auto &kv : want.relations)
        tags.insert(kv.first);
    const OOB have = !text.isEmpty() ? parse_oob(text, tags) : empty_oob();
    std::vector<std::tuple<qsizetype, qsizetype, QString>> edits;   // (start, end, replacement)
    const qsizetype n = text.size();
    auto remove = [&](const Span &span) {
        auto [s, e] = span;
        // the whole lines, with the blank line before them
        while (s > 0 && (text[s - 1] == u' ' || text[s - 1] == u'\t'))
            --s;
        while (e < n && (text[e] == u' ' || text[e] == u'\t'))
            ++e;
        if (text.mid(e, 2) == u"\r\n")
            e += 2;
        else if (text.mid(e, 1) == u"\n")
            e += 1;
        const qsizetype m = trailing_newline_run(text.left(s));
        if (m >= 0 && s > 0)
            s = m > 0 ? m + nl.size() : s;
        edits.emplace_back(s, e, QString());
    };
    auto span_key = [](const Span &s) { return std::make_pair(static_cast<long long>(s.first), static_cast<long long>(s.second)); };
    {
        std::map<std::pair<long long, long long>, const Army *> wanted_src;
        for (const Army &x : want.armies)
            if (x.src)
                wanted_src[span_key(*x.src)] = &x;
        for (const Army &x : have.armies) {
            const auto it = wanted_src.find(span_key(*x.src));
            const Army *w = it == wanted_src.end() ? nullptr : it->second;
            if (!w || w->regiments.empty())       // gone, or emptied: not kept
                remove(*x.src);
            else if (!(_army_key(*w) == *x.orig) || w->extra != x.extra)
                edits.emplace_back(x.src->first, x.src->second, army_text(*w, nl));
        }
    }
    {
        std::map<std::pair<long long, long long>, const Navy *> wanted_src;
        for (const Navy &x : want.navies)
            if (x.src)
                wanted_src[span_key(*x.src)] = &x;
        for (const Navy &x : have.navies) {
            const auto it = wanted_src.find(span_key(*x.src));
            const Navy *w = it == wanted_src.end() ? nullptr : it->second;
            if (!w || w->ships.empty())
                remove(*x.src);
            else if (!(_navy_key(*w) == *x.orig) || w->extra != x.extra)
                edits.emplace_back(x.src->first, x.src->second, navy_text(*w, nl));
        }
    }
    {
        std::map<std::pair<long long, long long>, const Leader *> wanted_src;
        for (const Leader &x : want.leaders)
            if (x.src)
                wanted_src[span_key(*x.src)] = &x;
        for (const Leader &x : have.leaders) {
            const auto it = wanted_src.find(span_key(*x.src));
            const Leader *w = it == wanted_src.end() ? nullptr : it->second;
            if (!w)
                remove(*x.src);
            else if (!(_leader_key(*w) == *x.orig))
                edits.emplace_back(x.src->first, x.src->second, leader_block_text(*w, nl));
        }
    }
    for (const auto &[tag, r] : have.relations) {
        const Relation *w = want.relations.get(tag);
        if (!w)
            remove(*r.src);
        else if (!(_rel_key(*w) == *r.orig))
            edits.emplace_back(r.src->first, r.src->second, relation_text(tag, *w, nl));
    }
    // new items: after the last one of their kind (armies before navies before relations)
    std::vector<const Army *> new_armies;
    for (const Army &a : want.armies)
        if (!a.src && !a.regiments.empty())
            new_armies.push_back(&a);
    std::vector<const Navy *> new_navies;
    for (const Navy &v : want.navies)
        if (!v.src && !v.ships.empty())
            new_navies.push_back(&v);
    std::vector<std::pair<QString, const Relation *>> new_rels;
    for (const auto &[t, r] : want.relations)
        if (!r.src)
            new_rels.emplace_back(t, &r);
    std::optional<qsizetype> last_army, last_navy, first_rel, last_rel, last_rel_start;
    for (const Army &a : have.armies)
        last_army = std::max(last_army.value_or(a.src->second), a.src->second);
    for (const Navy &v : have.navies)
        last_navy = std::max(last_navy.value_or(v.src->second), v.src->second);
    for (const auto &[_t, r] : have.relations) {
        first_rel = std::min(first_rel.value_or(r.src->first), r.src->first);
        last_rel = std::max(last_rel.value_or(r.src->second), r.src->second);
        last_rel_start = std::max(last_rel_start.value_or(r.src->first), r.src->first);
    }
    {
        // new leaders: after the pool's last one; with none yet, at the top of the file (the base game's place)
        QStringList parts;
        for (const Leader &l : want.leaders)
            if (!l.src)
                parts << leader_block_text(l, nl);
        std::optional<qsizetype> last_leader;
        for (const Leader &l : have.leaders)
            last_leader = std::max(last_leader.value_or(l.src->second), l.src->second);
        if (!parts.isEmpty()) {
            const QString body = parts.join(nl + nl);
            if (last_leader)
                edits.emplace_back(*last_leader, *last_leader, nl + nl + body);
            else
                edits.emplace_back(0, 0, body + nl + nl);
        }
    }
    if (!new_armies.empty()) {
        QStringList parts;
        for (const Army *a : new_armies)
            parts << army_text(*a, nl);
        const QString body = parts.join(nl + nl);
        if (last_army)
            edits.emplace_back(*last_army, *last_army, nl + nl + body);
        else
            edits.emplace_back(0, 0, body + nl + nl);
    }
    if (!new_navies.empty()) {
        QStringList parts;
        for (const Navy *v : new_navies)
            parts << navy_text(*v, nl);
        const QString body = parts.join(nl + nl);
        const std::optional<qsizetype> at = last_navy ? last_navy : last_army;
        if (at)
            edits.emplace_back(*at, *at, nl + nl + body);
        else
            edits.emplace_back(0, 0, body + nl + nl);
    }
    if (!new_rels.empty()) {
        // one-line relations are listed line by line, blocks with a blank line between them (like the mod's files)
        QStringList parts;
        for (const auto &[t, r] : new_rels)
            parts << relation_text(t, *r, nl);
        QString body = parts[0];
        for (qsizetype i = 1; i < parts.size(); ++i)
            body += ((parts[i - 1].contains(nl) || parts[i].contains(nl)) ? nl + nl : nl) + parts[i];
        if (last_rel) {
            const QString prev = py::slice(text, *last_rel_start, *last_rel);
            edits.emplace_back(*last_rel, *last_rel, ((prev.contains(nl) || parts[0].contains(nl)) ? nl + nl : nl) + body);
        } else {
            QString pre;
            if (py::strip(text).isEmpty())
                pre = QString();                  // new armies / navies end with a blank line
            else
                pre = (text.endsWith(u'\n') || text.endsWith(u'\r')) ? nl : nl + nl;
            edits.emplace_back(n, n, pre + u"#Relations"_s + nl + nl + body + nl);
        }
    }
    // same position: what was added first comes first in the file
    std::vector<size_t> order(edits.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return std::make_tuple(-std::get<0>(edits[a]), -std::get<1>(edits[a]), -static_cast<long long>(a)) <
               std::make_tuple(-std::get<0>(edits[b]), -std::get<1>(edits[b]), -static_cast<long long>(b));
    });
    for (size_t i : order) {
        const auto &[s, e, rep] = edits[i];
        text = py::head(text, s) + rep + py::tail(text, e);
    }
    return text;
}

// ---------------------------------------------------------------------- all OOBs
OOBEdits::OOBEdits(moddata::Mod &mod_, countryhist::CountryHistory &hist_, const std::optional<Date> &date_)
    : mod(mod_), hist(hist_), date(date_)
{
    if (!mod.unit_types)
        mod.unit_types = std::make_shared<UnitTypes>(mod);
    units = mod.unit_types.get();
    if (!mod.slot_rules)
        mod.slot_rules = std::make_shared<SlotRules>(mod);
    rules = mod.slot_rules.get();
}

std::pair<OptStr, OptStr> OOBEdits::file_of(const QString &tag) const
{
    // (path, referenced name or None). The history's `oob = "…"` at the date (a dated block can switch it).
    if (const auto *got = _files.get(tag))
        return *got;
    OptStr ref;
    const countryhist::NodesOf no = hist.nodes(tag);
    const Nodes &nodes = no.nodes();
    for (const Node &n : nodes)
        if (is(n, u"oob"_s) && !n.is_block)
            ref = n.value;
    if (date) {
        std::vector<std::pair<Date, const Node *>> dated;
        for (const Node &n : nodes) {
            const auto d = has(n) ? script::parse_date(n.key) : std::nullopt;
            if (d && n.is_block && *d <= *date)
                dated.emplace_back(*d, &n);
        }
        std::stable_sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (size_t i = 1; i < dated.size(); ++i)   // (Python sorts (date, Node): two blocks of one date can't be ordered)
            if (dated[i].first == dated[i - 1].first)
                throw std::runtime_error("TypeError: '<' not supported between instances of 'Node' and 'Node'");
        for (const auto &[_d, b] : dated)
            for (const Node &c : b->kids)
                if (is(c, u"oob"_s) && !c.is_block)
                    ref = c.value;
    }
    OptStr path;
    if (ref && !ref->isEmpty()) {
        const QStringList rel = QString(*ref).replace(u'\\', u'/').split(u'/');
        const QString cand = join(mod.root, QStringList{u"history"_s, u"units"_s} + rel);
        path = fs::isfile(cand) ? cand : mod.p(QStringList{u"history"_s, u"units"_s} + rel);
    }
    _files.insert(tag, {path, ref});
    return *_files.get(tag);
}

bool OOBEdits::exists(const QString &tag) const
{
    const auto [path, _r] = file_of(tag);
    return path && !path->isEmpty() && fs::isfile(*path);
}

const OOB &OOBEdits::_orig_state(const QString &tag) const
{
    if (!_orig.contains(tag)) {
        const auto [path, _ref] = file_of(tag);
        if (path && !path->isEmpty() && fs::isfile(*path)) {
            QSet<QString> tags;
            for (const auto &kv : mod.countries)
                tags.insert(kv.first);
            _orig.insert(tag, parse_oob(script::read_text(*path), tags));
        } else {
            _orig.insert(tag, empty_oob());
        }
    }
    return *_orig.get(tag);
}

const OOB &OOBEdits::state(const QString &tag) const
{
    if (const OOB *w = want.get(tag))
        return *w;
    return _orig_state(tag);
}

void OOBEdits::edit(const QString &tag, const std::function<void(OOB &)> &fn)
{
    OOB st = clone_oob(state(tag));
    fn(st);
    want.insert(tag, st);
}

QSet<QString> OOBEdits::tags_with_units() const
{
    QSet<QString> tags;
    for (const auto &kv : want)
        tags.insert(kv.first);
    for (const auto &kv : mod.countries)
        if (exists(kv.first))
            tags.insert(kv.first);
    return tags;
}

void OOBEdits::_finish(StateSlots &e) const
{
    // A state's provinces: each province's cap = what its soldier pops raise, every pop on its own (the game doesn't
    // pool soldiers); the state's cap is their sum.
    e.sold.clear();
    for (const auto &[n, pid] : e.pid_list)
        e.sold[pid] += n;
    e.pids.clear();
    for (const auto &kv : e.sold)
        e.pids << kv.first;
    std::stable_sort(e.pids.begin(), e.pids.end(), [&](long long a, long long b) {
        return std::make_pair(-e.sold.value(a), a) < std::make_pair(-e.sold.value(b), b);
    });
    e.pcap.clear();
    for (long long pid : e.pids) {
        long long c = 0;
        const Num m = e.mult.contains(pid) ? *e.mult.get(pid) : Num{1, true};
        for (long long n : e.pop_sizes.value(pid))
            c += rules->brigades(n, m);
        e.pcap.insert(pid, c);
    }
    e.cap = 0;
    for (const auto &kv : e.pcap)
        e.cap += kv.second;
}

Slots OOBEdits::state_slots(document::Document &doc, const QString &tag) const
{
    return all_slots(doc, tag).value(tag);
}

OrderedMap<QString, Slots> OOBEdits::all_slots(document::Document &doc, const OptStr &only) const
{
    // {tag: slots} in one pass over the pops.
    OrderedMap<QString, Slots> out;
    struct Status {
        QString tag, region;
        long long level;
        bool core;
    };
    QHash<long long, Status> status;                          // pid -> (owner, state, colonial level, has a core)
    OrderedMap<std::pair<QString, QString>, std::vector<long long>> levels;   // (owner, state) -> colonial levels
    for (const auto &[pid, pops] : doc.pops) {
        const moddata::Province *pr = mod.provinces.get(pid);
        if (!pr || pr->sea || !pr->region || pr->region->isEmpty())
            continue;
        const history::ProvState &st = doc.prov_state(pid);
        const OptStr tag = st.owner;
        if (!tag || tag->isEmpty() || (only && *tag != *only))
            continue;
        const long long level = st.colonial;
        status.insert(pid, Status{*tag, *pr->region, level, st.cores.contains(*tag)});
        if (level)
            levels[{*tag, *pr->region}].push_back(level);
        std::vector<long long> sizes;
        long long n = 0;
        for (const moddata::Pop &p : pops)
            if (p.type == u"soldiers") {
                sizes.push_back(p.size);
                n += p.size;
            }
        if (n > 0) {
            StateSlots &e = out[*tag][*pr->region];
            e.soldiers += n;
            e.pid_list.emplace_back(n, pid);
            e.pop_sizes.insert(pid, sizes);
        }
    }
    for (auto &[tag, per_state] : out) {
        for (auto &[region, e] : per_state) {
            // a province's own colonial level first; without one, its state's (the level most of the state's colonial
            // provinces have, the higher one on a tie)
            const std::vector<long long> ls = levels.value({tag, region});
            long long state_level = 0;
            if (!ls.empty()) {
                std::pair<long long, long long> best{-1, 0};
                for (long long x : ls) {
                    const std::pair<long long, long long> key{static_cast<long long>(std::count(ls.begin(), ls.end(), x)), x};
                    if (key > best)
                        best = key;
                }
                state_level = best.second;
            }
            for (const auto &kv : e.pop_sizes) {
                const long long pid = kv.first;
                const Status &s = status.value(pid);
                const long long level = s.level ? s.level : state_level;
                e.mult.insert(pid, rules->multiplier(level, s.core));
                e.why.insert(pid, level == 1 ? u"protectorate"_s : level ? u"colony"_s : s.core ? u"core"_s : u"no core"_s);
            }
            _finish(e);
        }
    }
    return out;
}

OrderedMap<long long, long long> OOBEdits::province_caps(const Slots &sl)
{
    OrderedMap<long long, long long> out;
    for (const auto &[_s, e] : sl)
        for (const auto &[pid, c] : e.pcap)
            out.insert(pid, c);
    return out;
}

OrderedMap<long long, long long> OOBEdits::used(const std::optional<QStringList> &tags) const
{
    // {pid: regiments raised there} over every nation's armies (or only `tags`).
    QStringList ts;
    if (tags)
        ts = *tags;
    else {
        const QSet<QString> s = tags_with_units();   // (Python iterates a set: the counts are the same either way)
        ts = QStringList(s.begin(), s.end());
    }
    OrderedMap<long long, long long> out;
    for (const QString &t : ts)
        for (const Army &a : state(t).armies)
            for (const Regiment &r : a.regiments)
                if (r.home)
                    out[*r.home] += 1;
    return out;
}

QSet<std::pair<long long, long long>> OOBEdits::over_cap(const QString &tag, const OrderedMap<long long, long long> &pcap) const
{
    // {(army, regiment)} of the nation's brigades beyond their province's cap (the later ones in the file).
    QHash<long long, long long> seen;
    QHash<long long, long long> seen_none;   // (a brigade without a home: Python's key None)
    QSet<std::pair<long long, long long>> out;
    const auto &armies = state(tag).armies;
    for (size_t ai = 0; ai < armies.size(); ++ai)
        for (size_t ri = 0; ri < armies[ai].regiments.size(); ++ri) {
            const OptInt h = armies[ai].regiments[ri].home;
            long long &n = h ? seen[*h] : seen_none[0];
            n += 1;
            if (n > (h ? pcap.value(*h, 0) : 0))
                out.insert({static_cast<long long>(ai), static_cast<long long>(ri)});
        }
    return out;
}

QList<long long> OOBEdits::new_homes(document::Document &doc, const Centers &md, const QString &tag, OptInt near,
                                     long long k, const std::optional<Slots> &slots_, const QList<long long> &exclude) const
{
    // Up to k provinces with soldiers left for new brigades, nearest to province `near` first.
    const Slots sl = slots_ ? *slots_ : state_slots(doc, tag);
    const auto pcap = province_caps(sl);
    auto usedc = used(QStringList{tag});
    for (long long h : exclude)                   // brigades about to leave these provinces
        usedc[h] -= 1;
    OrderedMap<long long, long long> free;
    for (const auto &[pid, c] : pcap)
        if (c > usedc.value(pid, 0))
            free.insert(pid, c - usedc.value(pid, 0));
    double nx = 0.0, ny = 0.0;
    if (near && *near && 0 < *near && *near < md.n) {
        nx = md.cx(*near);
        ny = md.cy(*near);
    }
    QList<long long> order;
    for (const auto &kv : free)
        order << kv.first;
    std::stable_sort(order.begin(), order.end(), [&](long long a, long long b) {
        const double da = std::pow(md.cx(a) - nx, 2) + std::pow(md.cy(a) - ny, 2);
        const double db = std::pow(md.cx(b) - nx, 2) + std::pow(md.cy(b) - ny, 2);
        return std::make_pair(da, a) < std::make_pair(db, b);
    });
    QList<long long> out;
    for (long long pid : order)
        for (long long j = 0; j < free.value(pid); ++j) {
            if (out.size() >= k)
                return out;
            out << pid;
        }
    return out;
}

store::Snap OOBEdits::snapshot() const
{
    store::Items it;
    for (const auto &[t, s] : want)
        it.insert(store::Key(t), store::box(clone_oob(s)));
    return std::make_shared<const store::ItemsSnap>(it, false);
}

void OOBEdits::restore(const store::Snap &snap)
{
    want.clear();
    for (const auto &[k, v] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        want.insert(k.text(), store::unbox<OOB>(v));
}

QStringList OOBEdits::dirty() const
{
    QStringList out;
    for (const auto &[t, s] : want)
        if (!(oob_key(s) == oob_key(_orig_state(t))))
            out << t;
    return out;
}

std::optional<QStringList> OOBEdits::save(const QString &backup_root)
{
    const QStringList tags = dirty();
    if (tags.isEmpty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"history_units"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    QStringList written;
    for (const QString &tag : tags) {
        auto [path, ref] = file_of(tag);
        OptStr mod_path;
        if (path && !path->isEmpty() && mod.in_mod(*path))   // (with the separator: not a sibling mod "Name 4K")
            mod_path = path;
        QString r = ref ? *ref : QString();
        if (!mod_path) {
            // no file in the mod yet: history/units/<ref or TAG_oob.txt>, copied from the base game if it has it
            if (r.isEmpty())
                r = tag + u"_oob.txt"_s;
            mod_path = join(mod.root, QStringList{u"history"_s, u"units"_s} + QString(r).replace(u'\\', u'/').split(u'/'));
        }
        const QString text = fs::isfile(*mod_path)                     ? script::read_text(*mod_path)
                             : (path && !path->isEmpty() && fs::isfile(*path)) ? script::read_text(*path)
                                                                               : QString();
        if (fs::isfile(*mod_path)) {
            fs::makedirs(bdir);
            fs::copy2(*mod_path, join(bdir, pypath::basename(*mod_path)));
        }
        const QString fresh = write_oob(text, *want.get(tag));
        fs::makedirs(pypath::dirname(*mod_path));
        script::write_text(*mod_path, fresh);
        written << *mod_path;
        if (!file_of(tag).second) {
            if (const OptStr p = _add_oob_line(tag, r, bdir))
                written << *p;
        }
        _files.remove(tag);
    }
    want.clear();
    _orig.clear();
    return written;
}

OptStr OOBEdits::_add_oob_line(const QString &tag, const QString &ref, const QString &bdir)
{
    // `oob = "TAG_oob.txt"` in the nation's history (after its capital line).
    const QString path = hist.files.value(tag);
    if (path.isEmpty() || !fs::isfile(path))
        return std::nullopt;
    QString text = script::read_text(path);
    static const QRegularExpression HAS(u"(*LF)(?m)^[ \\t]*oob[ \\t]*="_s);
    if (HAS.match(text).hasMatch())
        return std::nullopt;
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    fs::makedirs(bdir);
    fs::copy2(path, join(bdir, pypath::basename(path)));
    const QString line = u"oob = \"%1\""_s.arg(ref);
    static const QRegularExpression CAP(u"(*LF)(?m)^capital[ \\t]*=[^\\r\\n]*\\r?\\n"_s);
    const auto m = CAP.match(text);
    text = m.hasMatch() ? text.left(m.capturedEnd(0)) + line + nl + text.mid(m.capturedEnd(0)) : line + nl + text;
    script::write_text(path, text);
    hist._nodes.remove(tag);
    return path;
}

// ---------------------------------------------------------------------- naming
const std::vector<std::pair<QString, QString>> NAME_SCHEMES = {
    {u"auto"_s, u"Auto — 1st Infantry Regiment (numbered per type in this army)"_s},
    {u"nation"_s, u"Numbered per type across the nation (continues after its other armies)"_s},
    {u"custom"_s, u"Custom pattern…"_s}};
const QString PATTERN_HELP =
    u"{nth} = 1st/2nd…, {n} = 1/2…, {type} = Infantry…, {army} = the army's name, {home} = its home province"_s;

QStringList name_regiments(const std::vector<Regiment> &regs, const UnitTypes &units, const QString &scheme,
                           const QString &pattern, const QString &army_name, const OrderedMap<QString, long long> &start,
                           const OrderedMap<long long, moddata::Province> *provinces)
{
    // Names for the regiments (in order). `start`: {type word: numbers already used} for 'nation' numbering.
    OrderedMap<QString, long long> count = start;
    QStringList out;
    for (const Regiment &r : regs) {
        const QString word = units.word(r.type);
        count[word] += 1;
        const long long n = count.value(word);
        if (scheme == u"custom" && !pattern.isEmpty()) {
            QString home;
            if (provinces && !provinces->isEmpty() && r.home)
                if (const moddata::Province *p = provinces->get(*r.home))
                    home = p->name;
            out << QString(pattern)
                       .replace(u"{nth}"_s, ordinal(n))
                       .replace(u"{n}"_s, QString::number(n))
                       .replace(u"{type}"_s, word)
                       .replace(u"{army}"_s, army_name)
                       .replace(u"{home}"_s, home);
        } else {
            out << u"%1 %2 Regiment"_s.arg(ordinal(n), word);
        }
    }
    return out;
}

static std::optional<long long> number_after(const QString &s, const QString &prefix)   // re.match(r"^Prefix (\d+)$")
{
    QString t = s;
    if (t.endsWith(u'\n'))
        t.chop(1);
    if (!t.startsWith(prefix))
        return std::nullopt;
    const QString d = t.mid(prefix.size());
    if (d.isEmpty())
        return std::nullopt;
    for (QChar c : d)
        if (c.category() != QChar::Number_DecimalDigit)
            return std::nullopt;
    return py::to_int(d);
}

QString next_army_name(const std::vector<Army> &armies)
{
    std::optional<long long> best;
    for (const Army &a : armies)
        if (const auto n = number_after(a.name, u"Army "_s))
            best = std::max(best.value_or(*n), *n);
    return u"Army %1"_s.arg(best.value_or(static_cast<long long>(armies.size())) + 1);
}

QList<long long> next_ship_numbers(const std::vector<Navy> &navies, long long k)
{
    long long top = 0;
    bool any = false;
    for (const Navy &n : navies)
        for (const Ship &s : n.ships)
            if (const auto x = number_after(s.name, u"Ship "_s)) {
                top = any ? std::max(top, *x) : *x;
                any = true;
            }
    const long long first = (any ? top : 0) + 1;
    QList<long long> out;
    for (long long i = first; i < first + k; ++i)
        out << i;
    return out;
}

// ---------------------------------------------------------------------- repairs
std::function<void()> fix_units(document::Document &doc, const Centers &md, const std::vector<Fix> &todo)
{
    // Brigades from foreign provinces / over a state's soldiers: to the nearest states with soldiers left (removed
    // when there are none); unknown types; armies not on land; navies away from the coast; relations with nations
    // that don't exist. -> fn() that does it: call it inside Document.edit(..., lists=["oob"]).
    moddata::Mod &mod = doc.mod;
    OOBEdits &oob = doc.map<OOBEdits>(u"oob"_s);
    OrderedMap<QString, std::vector<Fix>> by_tag;
    for (const Fix &d : todo)
        by_tag[d.tag].push_back(d);
    const UnitTypes &units = *oob.units;
    QString land_default = u"infantry"_s, sea_default = u"frigate"_s;
    if (!units.types.contains(u"infantry"_s)) {
        const QStringList l = units.land();
        land_default = l.isEmpty() ? u"infantry"_s : l[0];
    }
    if (!units.types.contains(u"frigate"_s)) {
        const QStringList l = units.naval();
        sea_default = l.isEmpty() ? u"frigate"_s : l[0];
    }
    auto capital = [&doc, &mod](const QString &tag) -> OptInt {
        const store::StoreP *np = doc.maps.get(u"nations"_s);
        auto *nat = np ? dynamic_cast<countryhist::NationEdits *>(np->get()) : nullptr;
        const OptStr cap = (nat && nat->hist.files.contains(tag)) ? nat->state(tag).at(u"capital"_s) : std::nullopt;
        if (cap && !cap->isEmpty() && py::isdigit(*cap) && mod.provinces.contains(py::to_int(*cap)))
            return py::to_int(*cap);
        return std::nullopt;
    };
    return [&doc, &mod, &oob, md, by_tag, land_default, sea_default, capital]() {
        for (const auto &[tag, ds] : by_tag) {
            QList<long long> owned;
            for (const auto &[p, pr] : mod.provinces)
                if (!pr.sea && doc.prov_state(p).owner == OptStr(tag))
                    owned << p;
            std::vector<std::pair<long long, long long>> moves;
            for (const Fix &d : ds) {
                if (d.what == u"home")
                    moves.emplace_back(d.a, d.b);
                else if (d.what == u"over")
                    moves.insert(moves.end(), d.moves.begin(), d.moves.end());
            }
            const OOB *st = &oob.state(tag);
            std::map<std::pair<long long, long long>, long long> new_home;
            QList<long long> ais;
            for (const auto &m : moves)
                if (!ais.contains(m.first))
                    ais << m.first;
            std::sort(ais.begin(), ais.end());
            for (long long ai : ais) {
                std::vector<std::pair<long long, long long>> regs;
                for (const auto &m : moves)
                    if (m.first == ai)
                        regs.push_back(m);
                OptInt near = st->armies[size_t(ai)].location;
                if (!near || !mod.provinces.contains(*near))   // (it stands on a province that is gone)
                    near = capital(tag);
                QList<long long> exclude;
                for (const auto &[a, r] : regs) {
                    const OptInt h = st->armies[size_t(a)].regiments[size_t(r)].home;
                    if (h && mod.provinces.contains(*h) && doc.prov_state(*h).owner == OptStr(tag))
                        exclude << *h;
                }
                const QList<long long> homes = oob.new_homes(doc, md, tag, near, static_cast<long long>(regs.size()),
                                                             std::nullopt, exclude);
                for (qsizetype i = 0; i < qsizetype(regs.size()) && i < homes.size(); ++i)
                    new_home[regs[size_t(i)]] = homes[i];
                // count them as used before the next army looks for homes
                oob.edit(tag, [&](OOB &s) {
                    for (const auto &m : regs)
                        if (new_home.contains(m))
                            s.armies[size_t(m.first)].regiments[size_t(m.second)].home = new_home[m];
                });
                st = &oob.state(tag);
            }
            oob.edit(tag, [&](OOB &s) {
                const OptInt cap = capital(tag);
                for (const Fix &d : ds) {
                    if (d.what == u"reg_type") {
                        s.armies[size_t(d.a)].regiments[size_t(d.b)].type = land_default;
                    } else if (d.what == u"ship_type") {
                        s.navies[size_t(d.a)].ships[size_t(d.b)].type = sea_default;
                    } else if (d.what == u"army_loc") {
                        s.armies[size_t(d.a)].location = cap ? cap : (owned.isEmpty() ? OptInt() : OptInt(owned[0]));
                    } else if (d.what == u"navy_loc") {
                        const OptInt cur = s.navies[size_t(d.a)].location;
                        const OptInt ref = (cur && mod.provinces.contains(*cur)) ? cur : cap;
                        QList<long long> cands;
                        if (doc.coastal)
                            for (long long p : owned)
                                if (p < qsizetype(doc.coastal->size()) && (*doc.coastal)[size_t(p)])
                                    cands << p;
                        if (!cands.isEmpty() && ref && *ref) {
                            long long best = cands[0];
                            double bd = 0;
                            bool first = true;
                            for (long long p : cands) {
                                const double dd = std::pow(md.cx(p) - md.cx(*ref), 2) + std::pow(md.cy(p) - md.cy(*ref), 2);
                                if (first || dd < bd) {
                                    best = p;
                                    bd = dd;
                                    first = false;
                                }
                            }
                            s.navies[size_t(d.a)].location = best;
                        }
                    } else if (d.what == u"rel_unknown") {
                        s.relations.remove(d.other);
                    }
                }
                // brigades that found no new home go
                std::vector<std::pair<long long, long long>> gone;
                for (const auto &m : moves)
                    if (!new_home.contains(m) && std::find(gone.begin(), gone.end(), m) == gone.end())
                        gone.push_back(m);
                std::sort(gone.begin(), gone.end(), std::greater<>());
                for (const auto &[a, r] : gone)
                    s.armies[size_t(a)].regiments.erase(s.armies[size_t(a)].regiments.begin() + r);
            });
        }
    };
}

}  // namespace oob
