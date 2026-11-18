// blockcode.py — the block catalogue, the item tree, the mod's names. Line by line.
#include "core/blockcode.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/events.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/scriptstore.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace blockcode {

#include "core/blockcode_catalogue.inc"

using script::Node;
using script::Nodes;

static OrderedMap<QString, QString> om(std::initializer_list<std::pair<QString, QString>> l)
{
    OrderedMap<QString, QString> m;
    for (const auto &kv : l)
        m.insert(kv.first, kv.second);
    return m;
}

const OrderedMap<QString, QString> SCOPE_NAME = om({{u"c"_s, u"a nation"_s}, {u"p"_s, u"a province"_s}, {u"s"_s, u"a pop"_s}});
const OrderedMap<QString, QString> MODE_NAME = om({{u"cond"_s, u"a condition"_s}, {u"eff"_s, u"an effect"_s}, {u"goal"_s, u"a war goal"_s}});
const OrderedMap<QString, QString> MODE_NAMES = om({{u"cond"_s, u"conditions"_s}, {u"eff"_s, u"effects"_s}, {u"goal"_s, u"war goals"_s}});
// a key that is a name: what its slot takes (the type its picker shows), and how it looks in the script
const OrderedMap<QString, QString> KEY_TYPE = om({{u"tag"_s, u"tagonly"_s}, {u"prov"_s, u"prov"_s}, {u"region"_s, u"region"_s},
                                                  {u"poptype"_s, u"poptype"_s}, {u"issue"_s, u"issue"_s}, {u"tech"_s, u"tech"_s},
                                                  {u"ideology"_s, u"ideology"_s}, {u"option"_s, u"option"_s}, {u"goods"_s, u"goods"_s}});
const OrderedMap<QString, QString> KEY_SCRIPT = om({{u"tag"_s, u"ENG"_s}, {u"prov"_s, u"300"_s}, {u"region"_s, u"ENG_300"_s},
                                                    {u"poptype"_s, u"farmers"_s}, {u"issue"_s, u"slavery"_s}, {u"tech"_s, u"railroad"_s},
                                                    {u"ideology"_s, u"liberal"_s}, {u"option"_s, u"jingoism"_s}, {u"goods"_s, u"coal"_s}});
// what goes into a value slot, in a few words (shown in the block's picture) …
const OrderedMap<QString, QString> SLOT_NAME = om({
    {u"num"_s, u"number"_s}, {u"int"_s, u"whole number"_s}, {u"bool"_s, u"yes / no"_s}, {u"tag"_s, u"nation"_s},
    {u"tagbool"_s, u"nation / yes"_s}, {u"prov"_s, u"province nr."_s}, {u"cflag"_s, u"flag name"_s}, {u"gflag"_s, u"flag name"_s},
    {u"pflag"_s, u"flag name"_s}, {u"var"_s, u"name"_s}, {u"mod"_s, u"modifier"_s}, {u"culture"_s, u"culture"_s},
    {u"cgroup"_s, u"culture group"_s}, {u"religion"_s, u"religion"_s}, {u"goods"_s, u"goods"_s}, {u"tech"_s, u"technology"_s},
    {u"invention"_s, u"invention"_s}, {u"ideology"_s, u"ideology"_s}, {u"gov"_s, u"government"_s}, {u"nv"_s, u"national value"_s},
    {u"poptype"_s, u"pop type"_s}, {u"option"_s, u"reform"_s}, {u"region"_s, u"state"_s}, {u"continent"_s, u"continent"_s},
    {u"building"_s, u"building"_s}, {u"unit"_s, u"unit type"_s}, {u"crime"_s, u"crime"_s}, {u"cb"_s, u"war goal"_s},
    {u"terrain"_s, u"terrain"_s}, {u"strata"_s, u"poor / middle / rich"_s}, {u"text"_s, u"text"_s}, {u"event"_s, u"event"_s},
    {u"tagc"_s, u"nation"_s}, {u"tagonly"_s, u"nation"_s}, {u"issue"_s, u"reform"_s}, {u"personality"_s, u"trait"_s},
    {u"background"_s, u"trait"_s}});
// … and said in full (the help beside the blocks)
const OrderedMap<QString, QString> VALUE_HELP = om({
    {u"num"_s, u"a number — with a point for parts (0.25), with a minus to take away (-5)"_s},
    {u"int"_s, u"a whole number (no point)"_s},
    {u"bool"_s, u"yes or no"_s},
    {u"tag"_s, u"a nation, picked from the list — or THIS (the nation the event is about) / FROM (the nation that sent it)"_s},
    {u"tagbool"_s, u"yes / no, or a nation picked from the list"_s},
    {u"prov"_s, u"a province's number (the Provinces page and the map show them)"_s},
    {u"cflag"_s, u"a flag's name — any word you like (no spaces). An effect sets it, conditions ask for it"_s},
    {u"gflag"_s, u"a flag's name — any word you like (no spaces), one for the whole world"_s},
    {u"pflag"_s, u"a flag's name — any word you like (no spaces), set on a province"_s},
    {u"var"_s, u"the name of a number the nation keeps — any word you like (no spaces)"_s},
    {u"mod"_s, u"one of the mod's modifiers (Scripts → Modifiers makes new ones)"_s},
    {u"event"_s, u"another event, by its id"_s},
    {u"text"_s, u"a text or a name"_s},
    {u"tagc"_s, u"a nation, picked from the list — or “culture”: the union nation of its culture group"_s},
    {u"tagonly"_s, u"a nation, picked from the list"_s},
    {u"issue"_s, u"a reform or a party issue (slavery, vote franchise, trade policy …)"_s},
    {u"personality"_s, u"a leader's personality, from common/traits.txt"_s},
    {u"background"_s, u"a leader's background, from common/traits.txt"_s}});
const QString NOTE = u"#"_s;

static bool tag_re(const QString &s) { return countryhist::tag_ok(s); }
static bool num_re(const QString &s) { return py::re_match(u"^-?(\\d+(\\.\\d*)?|\\.\\d+)$"_s, s).hasMatch(); }
static bool plain_re(const QString &s) { return py::re_match(u"^[A-Za-z0-9_.\\-]+$"_s, s).hasMatch(); }

// ---------------------------------------------------------------------- the catalogue
Spec::Spec(const QString &key_, const QString &mode_, const QString &cat_, QString scopes_, const QString &text_,
           const QString &value)
    : key(key_), mode(mode_), cat(cat_), text(text_)
{
    hidden = scopes_.startsWith(u'-');           // understood in a script, not offered in the list
    while (scopes_.startsWith(u'-'))
        scopes_.remove(0, 1);
    scopes = scopes_;
    if (key.startsWith(u'<'))
        keytype = key.mid(1, key.size() - 2);
    auto parse_fields = [&](const QStringList &words) {
        std::vector<std::pair<QString, QString>> out;
        for (const QString &x : words) {
            const QStringList p = x.split(u':');
            QString f = p[0];
            if (f.endsWith(u'?')) {
                f.chop(1);
                optional.insert(f);
            }
            out.emplace_back(f, p.value(1));
        }
        return out;
    };
    const QString v = py::strip(value);
    if (v.startsWith(u'{')) {
        vtype = u"fields"_s;
        fields = parse_fields(py::split(py::strip(v, u"{} "_s)));
    } else if (v.startsWith(u"->")) {
        vtype = u"block"_s;
        const QStringList rest = py::split(v.mid(2));
        const QString head = rest.isEmpty() ? QString() : rest[0];
        if (head == u"c" || head == u"p" || head == u"s") {
            out = head;
        } else if (head == u"cond" || head == u"list" || head == u"goal") {
            out = QString();
            inner = head;
        } else {
            out = QString();
        }
        if ((head == u"=" || head == u"goal") && rest.size() > 1)   // a block with fields of its own before the blocks
            fields = parse_fields(rest.mid(1));
    } else {
        vtype = v;
    }
}

QString Spec::kind() const
{
    if (vtype == OptStr(u"block"_s)) {
        if (key == u"AND" || key == u"OR" || key == u"NOT" || key == u"limit")
            return u"logic"_s;
        if (key == u"random" || key == u"random_list")
            return u"chance"_s;
        if (inner == OptStr(u"goal"_s))
            return u"eff"_s;
        return u"scope"_s;
    }
    return mode;
}

QString Spec::script() const
{
    const QString k = keytype && KEY_SCRIPT.contains(*keytype) ? KEY_SCRIPT.value(*keytype) : key;
    if (vtype == OptStr(u"block"_s)) {
        QString f;
        if (fields)
            for (const auto &[n, _t] : *fields)
                f += n + u" = … "_s;
        return k + u" = { "_s + f + u"… }"_s;
    }
    if (vtype == OptStr(u"fields"_s)) {
        QStringList f;
        for (const auto &[n, _t] : *fields)
            f << n + u" = …"_s;
        return k + u" = { "_s + f.join(u' ') + u" }"_s;
    }
    return k + u" = …"_s;
}

bool Spec::fits(const OptStr &scope) const { return scopes.contains(u'*') || !scope || scopes.contains(*scope); }

std::vector<Spec> _read(const QString &text, const QString &mode)
{
    std::vector<Spec> out;
    QString cat;
    for (QString ln : py::strip(text).split(u'\n')) {
        ln = py::strip(ln);
        if (ln.isEmpty())
            continue;
        if (ln.startsWith(u'#')) {
            cat = py::strip(ln.mid(1));
            continue;
        }
        const QStringList parts = ln.split(u'|');
        const QStringList head = py::split(py::strip(parts[0]));
        out.emplace_back(head[1], mode, cat, head[0], py::strip(parts[1]), py::strip(parts[2]));
    }
    return out;
}

const std::vector<Spec> &COND()
{
    static const std::vector<Spec> s = _read(QString::fromUtf8(CATALOGUE_CONDITIONS), u"cond"_s);
    return s;
}
const std::vector<Spec> &EFF()
{
    static const std::vector<Spec> s = _read(QString::fromUtf8(CATALOGUE_EFFECTS), u"eff"_s);
    return s;
}
const std::vector<Spec> &GOAL()
{
    static const std::vector<Spec> s = _read(QString::fromUtf8(CATALOGUE_GOALS), u"goal"_s);
    return s;
}
const std::vector<Spec> &OF_MODE(const QString &mode)
{
    return mode == u"cond" ? COND() : mode == u"eff" ? EFF() : GOAL();
}

struct Index {                                // BY_KEY / BY_NAME
    QHash<QString, OrderedMap<QString, std::vector<const Spec *>>> by_key, by_name;
};
static const Index &index()
{
    static const Index idx = [] {
        Index i;
        for (const std::vector<Spec> *l : {&COND(), &EFF(), &GOAL()})
            for (const Spec &s : *l) {
                if (s.keytype)
                    i.by_name[s.mode][*s.keytype].push_back(&s);
                else
                    i.by_key[s.mode][py::lower(s.key)].push_back(&s);
            }
        return i;
    }();
    return idx;
}

bool accepts(const Spec &spec, const Item &it, const Env *env)
{
    // Does the block take the value as it is written? (several blocks can share a key: military_score = 50 /
    // military_score = THIS; scaled_militancy with an ideology / with an issue)
    const bool blk = it.list;
    if (spec.vtype == OptStr(u"event"_s))
        return true;
    if (spec.vtype == OptStr(u"block"_s))
        return blk;
    if (spec.vtype == OptStr(u"fields"_s)) {
        if (!blk)
            return false;
        QSet<QString> fs_;
        for (const auto &f : *spec.fields)
            fs_.insert(f.first);
        for (const Item &c : it.items)
            if (c.k && *c.k != NOTE && !fs_.contains(*c.k))
                return false;
        return true;
    }
    if (blk)
        return false;
    const QString &v = it.text;
    const QString ty = spec.vtype.value_or(QString());
    if (ty == u"num" || ty == u"int")
        return num_re(v);
    if (ty == u"bool")
        return py::lower(v) == u"yes" || py::lower(v) == u"no";
    if (ty == u"prov")
        return py::isdigit(v);
    static const QSet<QString> LEGAL = {u"tag"_s, u"tagc"_s, u"tagbool"_s, u"tagonly"_s, u"culture"_s, u"cgroup"_s,
                                        u"religion"_s, u"option"_s};
    if (env && LEGAL.contains(ty))
        return env->legal(ty, v);
    return true;
}

QStringList names_of(const OptStr &key, const Env *env)
{
    // The kinds of name a key is in this mod (a nation's tag, a province number, a state, a pop type …).
    QStringList out;
    if (!env || !key)
        return out;
    const QString &k = *key;
    if (tag_re(k) && env->mod.countries.contains(k))
        out << u"tag"_s;
    if (py::isdigit(k) && env->mod.provinces.contains(py::to_int(k)))
        out << u"prov"_s;
    if (env->mod.regions.contains(k))
        out << u"region"_s;
    if (env->issues.contains(k))
        out << u"issue"_s;
    if (env->techs.contains(k))
        out << u"tech"_s;
    if (env->option_set.contains(k))
        out << u"option"_s;
    if (env->ideologies.contains(k))
        out << u"ideology"_s;
    if (env->goods.contains(k))
        out << u"goods"_s;
    if (env->poptypes.contains(k))
        out << u"poptype"_s;
    return out;
}

const Spec *spec_for(const OptStr &key, const QString &mode, const OptStr &scope, const Env *env, const Item *value)
{
    // The catalogue's entry for a key in a place (mode "cond" / "eff" / "goal", scope "c" / "p" / "s"), or None. With
    // the value as it is written, the entry that takes it is picked (a key can have several); the best fit for the
    // scope first. A nation's tag, a province number, a state, a pop type as a key are blocks about that one; a reform
    // as a key picks its option; a technology as a key asks whether it is researched.
    if (!key)
        return nullptr;
    const QString &k = *key;
    std::vector<const Spec *> lst;
    const auto &idx = index();
    if (const auto it = idx.by_key.constFind(mode); it != idx.by_key.cend())
        if (const auto *l = it->get(py::lower(k)))
            lst = *l;
    if (!lst.empty() && env && k != u"AND" && k != u"NOT" && tag_re(k) && env->mod.countries.contains(k))
        lst.clear();                              // (a nation's tag that reads like a word of the script: WAR, TAG)
    if (lst.empty()) {
        if (const auto it = idx.by_name.constFind(mode); it != idx.by_name.cend())
            for (const QString &kind : names_of(key, env))
                if (const auto *l = it->get(kind))
                    lst.insert(lst.end(), l->begin(), l->end());
    }
    if (lst.empty())
        return nullptr;
    if (value) {
        std::vector<const Spec *> ok;
        for (const Spec *s : lst)
            if (accepts(*s, *value, env))
                ok.push_back(s);
        if (ok.empty()) {                         // written another way: the entry of the same shape, if any
            const bool blk = value->list;
            for (const Spec *s : lst) {
                const bool bf = s->vtype == OptStr(u"block"_s) || s->vtype == OptStr(u"fields"_s);
                if (bf == blk && s->vtype != OptStr(u"fields"_s))
                    ok.push_back(s);
            }
            if (ok.empty())
                return lst[0]->vtype == OptStr(u"event"_s) ? lst[0] : nullptr;
        }
        lst = ok;
    }
    for (const Spec *s : lst)
        if (scope && s->scopes.contains(*scope))
            return s;
    for (const Spec *s : lst)
        if (s->scopes.contains(u'*'))
            return s;
    return lst[0];
}

// ---------------------------------------------------------------------- the tree
Items from_nodes(const Nodes &nodes, const QString &text)
{
    // Script nodes -> [item].
    Items out;
    for (const Node &n : nodes) {
        const OptStr k = n.has_key ? OptStr(n.key) : std::nullopt;
        if (n.is_block)
            out.push_back(Item::of(k, from_nodes(n.kids, text)));
        else
            out.push_back(Item::of(k, n.value, py::slice(text, n.vstart, n.vstart + 1) == u"\""));
    }
    return out;
}

Items read_items(const QString &text)
{
    // A script text -> [item], comments kept: a comment is the item {"k": "#", "v": its text} where it stood (one at the
    // end of a line follows the line it was on).
    struct Tok {
        QChar kind;
        QString text;
        bool quoted;
    };
    std::vector<Tok> toks;
    qsizetype i = 0;
    const qsizetype n = text.size();
    static const QString WS = u" \t\r\n﻿"_s, STOP = u" \t\r\n{}=#\""_s;
    while (i < n) {
        const QChar c = text[i];
        if (WS.contains(c)) {
            ++i;
        } else if (c == u'#') {
            qsizetype j = text.indexOf(u'\n', i);
            j = j < 0 ? n : j;
            toks.push_back({u'#', py::rstrip(py::slice(text, i + 1, j)), false});
            i = j;
        } else if (c == u'{' || c == u'}' || c == u'=') {
            toks.push_back({c, QString(c), false});
            ++i;
        } else if (c == u'"') {
            qsizetype j = text.indexOf(u'"', i + 1);
            j = j < 0 ? n - 1 : j;
            toks.push_back({u's', py::slice(text, i + 1, j), true});
            i = j + 1;
        } else {
            qsizetype j = i;
            while (j < n && !STOP.contains(text[j]))
                ++j;
            toks.push_back({u's', py::slice(text, i, j), false});
            i = j;
        }
    }
    qsizetype pos = 0;
    const qsizetype ntok = qsizetype(toks.size());
    std::function<Items(int)> block = [&](int depth) {
        Items items;
        while (pos < ntok) {
            const Tok &t = toks[size_t(pos)];
            if (t.kind == u'}') {
                if (depth > 0)
                    return items;
                ++pos;
            } else if (t.kind == u'#') {
                items.push_back(Item::of(NOTE, t.text));
                ++pos;
            } else if (t.kind == u'=') {
                ++pos;
            } else if (t.kind == u'{') {
                ++pos;
                Items sub = block(depth + 1);
                ++pos;
                items.push_back(Item::of(std::nullopt, std::move(sub)));
            } else if (pos + 1 < ntok && toks[size_t(pos + 1)].kind == u'=') {
                const QString key = t.text;
                pos += 2;
                Items late;
                while (pos < ntok && toks[size_t(pos)].kind == u'#') {   // (a comment between "=" and the value)
                    late.push_back(Item::of(NOTE, toks[size_t(pos)].text));
                    ++pos;
                }
                if (pos >= ntok)
                    break;
                const Tok &v = toks[size_t(pos)];
                if (v.kind == u'{') {
                    ++pos;
                    Items sub = block(depth + 1);
                    ++pos;
                    items.push_back(Item::of(key, std::move(sub)));
                } else if (v.kind == u's') {
                    ++pos;
                    items.push_back(Item::of(key, v.text, v.quoted));
                }
                items.insert(items.end(), late.begin(), late.end());
            } else {
                ++pos;
                items.push_back(Item::of(std::nullopt, t.text, t.quoted));
            }
        }
        return items;
    };
    return block(0);
}

QString _val(const Item &it)
{
    const QString &v = it.text;
    return (it.q || !plain_re(v)) ? u"\""_s + v + u"\""_s : v;
}

QString dump(const Items &items, long long depth, std::vector<Span> *spans, long long line)
{
    // [item] -> text, one tab a level; a block of bare values (a list) stays on one line. With `spans`: (first line,
    // last line, item) of every item is added to it — where it stands in the text (lines from 0).
    const QString ind(depth, u'\t');
    QStringList lines;
    for (const Item &it : items) {
        QString s;
        const QString kpre = it.k ? *it.k + u" = "_s : QString();
        if (it.k == OptStr(NOTE) && !it.list) {   // a comment: "#" and its text, as it was
            s = ind + u'#' + QString(it.text).replace(u'\n', u' ');
        } else if (it.list) {
            const bool flat = !it.items.empty() && std::all_of(it.items.begin(), it.items.end(),
                                                               [](const Item &c) { return !c.k && !c.list; });
            if (flat) {
                QStringList vs;
                for (const Item &c : it.items)
                    vs << _val(c);
                s = ind + kpre + u"{ "_s + vs.join(u' ') + u" }"_s;
            } else if (it.items.empty()) {
                s = ind + kpre + u"{\n"_s + ind + u'}';
            } else {
                s = ind + kpre + u"{\n"_s + dump(it.items, depth + 1, spans, line + 1) + u'\n' + ind + u'}';
            }
        } else if (!it.k) {
            s = ind + _val(it);
        } else {
            s = ind + *it.k + u" = "_s + _val(it);
        }
        const long long nl = s.count(u'\n') + 1;
        if (spans)
            spans->push_back(Span{line, line + nl - 1, &it});
        line += nl;
        lines << s;
    }
    return lines.join(u'\n');
}

const Item *child(const Items &items, const OptStr &key)
{
    for (const Item &c : items)
        if (c.k == key)
            return &c;
    return nullptr;
}

OptStr value(const Items &items, const QString &key, const OptStr &dflt)
{
    const Item *c = child(items, key);
    return (c && !c->list) ? OptStr(c->text) : dflt;
}

Item *block(Items &items, const QString &key, bool make, std::optional<qsizetype> at)
{
    // The block `key` among items (made — at index `at` or the end — when asked for).
    for (Item &c : items)
        if (c.k == OptStr(key) && c.list)
            return &c;
    if (!make)
        return nullptr;
    const qsizetype where = at ? std::clamp<qsizetype>(*at, 0, qsizetype(items.size())) : qsizetype(items.size());
    return &*items.insert(items.begin() + where, Item::of(key, Items()));
}

// ---- the tree by reference
PPtr pitem(const OptStr &k, const QString &text, bool quoted)
{
    auto p = std::make_shared<PItem>();
    p->k = k;
    p->text = text;
    p->q = quoted;
    return p;
}

PPtr pitem(const OptStr &k, PList items)
{
    auto p = std::make_shared<PItem>();
    p->k = k;
    p->list = true;
    p->items = std::move(items);
    return p;
}

PPtr to_ref(const Item &it)
{
    return it.list ? pitem(it.k, to_ref(it.items)) : pitem(it.k, it.text, it.q);
}

PList to_ref(const Items &items)
{
    PList out;
    out.reserve(items.size());
    for (const Item &c : items)
        out.push_back(to_ref(c));
    return out;
}

Item from_ref(const PItem &it)
{
    return it.list ? Item::of(it.k, from_ref(it.items)) : Item::of(it.k, it.text, it.q);
}

Items from_ref(const PList &items)
{
    Items out;
    out.reserve(items.size());
    for (const PPtr &c : items)
        out.push_back(from_ref(*c));
    return out;
}

Item shallow(const PItem &it)
{
    if (!it.list)
        return Item::of(it.k, it.text, it.q);
    Items kids;
    kids.reserve(it.items.size());
    for (const PPtr &c : it.items)
        kids.push_back(c->list ? Item::of(c->k, Items()) : Item::of(c->k, c->text, c->q));
    return Item::of(it.k, std::move(kids));
}

PItem *block(PList &items, const QString &key, bool make, std::optional<qsizetype> at)
{
    for (const PPtr &c : items)
        if (c->k == OptStr(key) && c->list)
            return c.get();
    if (!make)
        return nullptr;
    const qsizetype where = at ? std::clamp<qsizetype>(*at, 0, qsizetype(items.size())) : qsizetype(items.size());
    return items.insert(items.begin() + where, pitem(key, PList()))->get();
}

void set_value(Items &items, const QString &key, const OptStr &val, bool quoted, std::optional<qsizetype> at)
{
    auto it = std::find_if(items.begin(), items.end(), [&](const Item &c) { return c.k == OptStr(key) && !c.list; });
    if (!val) {
        if (it != items.end())
            items.erase(it);
        return;
    }
    if (it == items.end()) {
        const qsizetype where = at ? std::clamp<qsizetype>(*at, 0, qsizetype(items.size())) : qsizetype(items.size());
        items.insert(items.begin() + where, Item::of(key, *val, quoted));
    } else {
        it->text = *val;
        it->q = quoted || it->q;
    }
}

QString _default(const QString &vtype, const Env *env)
{
    if (vtype == u"bool" || vtype == u"tagbool")
        return u"yes"_s;
    if (vtype == u"cflag" || vtype == u"gflag" || vtype == u"pflag")   // (a name to start from: an empty one is no flag)
        return u"my_flag"_s;
    if (vtype == u"var")
        return u"my_number"_s;
    if (vtype == u"num" || vtype == u"int")
        return u"1"_s;
    if (vtype == u"strata")
        return u"poor"_s;
    if (env)
        if (const OptStr first = env->first(vtype))
            return *first;
    return vtype == u"tag" ? u"THIS"_s : QString();
}

Item new_item(const Spec &spec, const Env *env)
{
    // A fresh block of a catalogue entry, with values to start from (fields that may be left out are).
    static const OrderedMap<QString, QString> START = om({{u"duration"_s, u"365"_s}, {u"months"_s, u"12"_s}, {u"chance"_s, u"50"_s},
                                                          {u"call_ally"_s, u"yes"_s}, {u"name"_s, u"A Name"_s},
                                                          {u"limit_to_world_greatest_level"_s, u"yes"_s}});
    QString key = spec.key;
    if (spec.keytype) {                           // the key is a name: the first the mod has
        const OptStr f = env ? env->first(KEY_TYPE.value(*spec.keytype)) : std::nullopt;
        key = (f && !f->isEmpty()) ? *f : KEY_SCRIPT.value(*spec.keytype);
    }
    std::vector<std::pair<QString, QString>> fields;
    if (spec.fields)
        for (const auto &[f, t] : *spec.fields)
            if (!spec.optional.contains(f) || START.contains(f))
                fields.emplace_back(f, t);
    auto start_or_default = [&](const QString &f, const QString &t) {
        const QString s = START.value(f);
        return !s.isEmpty() ? s : _default(t, env);
    };
    if (spec.vtype == OptStr(u"block"_s)) {
        Items kids;
        for (const auto &[f, t] : fields)
            kids.push_back(Item::of(f, start_or_default(f, t), t == u"text"));
        if (spec.inner == OptStr(u"list"_s))
            kids = {Item::of(u"50"_s, Items()), Item::of(u"50"_s, Items())};
        else if (spec.inner == OptStr(u"goal"_s))
            kids.push_back(new_item(GOAL()[0], env));
        return Item::of(key, std::move(kids));
    }
    if (spec.vtype == OptStr(u"fields"_s)) {
        Items kids;
        for (const auto &[f, t] : fields)
            kids.push_back(Item::of(f, start_or_default(f, t), t == u"text"));
        return Item::of(key, std::move(kids));
    }
    if (spec.vtype == OptStr(u"option"_s) && spec.keytype == OptStr(u"issue"_s) && env) {
        if (const QStringList *opts = env->issues.get(key); opts && !opts->isEmpty())
            return Item::of(key, (*opts)[0]);
    }
    if (spec.keytype == OptStr(u"tech"_s))
        return Item::of(key, u"1"_s);
    const QString vt = spec.vtype.value_or(QString());
    return Item::of(key, _default(vt, env), vt == u"text");
}

QStringList files_of(const moddata::Mod &mod, const scriptstore::ScriptStore *store, const QString &folder, bool hide_empty)
{
    // The .txt files of a folder: the mod's, and the game's own that the mod doesn't replace.
    OrderedMap<QString, QString> out;
    QStringList roots;
    const QStringList parts = folder.split(u'/');
    if (mod.base && !mod.base->isEmpty() && !mod.replaced(parts))   // (a folder the mod replaces: the game's don't count)
        roots << pypath::join(*mod.base, parts);
    roots << pypath::join(mod.root, parts);
    for (const QString &d : roots)
        if (fs::isdir(d))
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".txt"))
                    out.insert(py::lower(fn), folder + u'/' + fn);
    if (store) {
        for (const auto &kv : store->docs) {       // made in this session, not on disk yet
            const QString &rel = kv.first;
            if (rel.startsWith(folder + u'/') && rel.count(u'/') == folder.count(u'/') + 1) {
                const QString b = py::lower(pypath::basename(rel));
                if (!out.contains(b))
                    out.insert(b, rel);
            }
        }
    }
    QStringList res;
    for (const auto &kv : out) {
        const QString &r = kv.second;
        if (store && store->gone.contains(r))
            continue;
        if (hide_empty && store && py::strip(const_cast<scriptstore::ScriptStore *>(store)->text(r)).isEmpty())
            continue;
        res << r;
    }
    std::stable_sort(res.begin(), res.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    return res;
}

// ---------------------------------------------------------------------- the mod's names
Env::Env(moddata::Mod &mod_, document::Document *doc_, const refs::RefIndex *refs_, events::EventDB *db_)
    : mod(mod_), doc(doc_), refs(refs_), db(db_)
{
    if (doc)
        if (const store::StoreP *s = doc->maps.get(u"scripts"_s))
            store = dynamic_cast<scriptstore::ScriptStore *>(s->get());
    try {
        if (!store)
            throw std::runtime_error("AttributeError");
        for (const Node &grp : script::parse(store->text(u"common/issues.txt"_s)))
            if (grp.is_block)
                for (const Node &issue : grp.kids)
                    if (issue.has_key && !issue.key.isEmpty() && issue.is_block) {
                        QStringList opts;
                        for (const Node &o : issue.kids)
                            if (o.has_key && !o.key.isEmpty() && o.is_block)
                                opts << o.key;
                        issues.insert(issue.key, opts);
                        options += opts;
                    }
    } catch (...) {
    }
    if (const countryhist::NationData *data = mod.nation_data.get()) {
        for (const auto &kv : data->techs)
            techs.insert(kv.first);
        for (const auto &kv : data->inventions)
            inventions.insert(kv.first);
    }
    if (techs.isEmpty()) {                        // (read from the files when the nations' data isn't)
        for (const auto &[folder, into] : {std::pair{u"technologies"_s, &techs}, std::pair{u"inventions"_s, &inventions}})
            for (const QString &rel : files_of(mod, store, folder))
                for (const QString &k : _keys(rel))
                    into->insert(k);
    }
    for (const QString &o : options)
        option_set.insert(o);
    for (const QString &k : _keys(u"common/ideologies.txt"_s, {}, 2))
        ideologies.insert(k);
    for (const auto &kv : mod.goods)
        goods.insert(kv.first);
    for (const auto &kv : mod.poptypes)
        poptypes.insert(kv.first);
}

OptStr Env::loc(const QString &key) const
{
    OptStr t;
    const store::StoreP *lp = doc ? doc->maps.get(u"loc"_s) : nullptr;
    if (lp) {
        auto *lc = dynamic_cast<localisation::LocEdits *>(lp->get());
        t = lc ? lc->text(key) : std::nullopt;
    } else if (const QString *l = mod.loc.get(key)) {
        t = *l;
    }
    return (t && !t->isEmpty()) ? t : std::nullopt;
}

QString Env::name(const QString &key) const
{
    if (const moddata::Country *c = mod.countries.get(key))
        return !c->name.isEmpty() ? c->name : key;
    const OptStr l = loc(key);
    return l ? *l : QString(key).replace(u'_', u' ');
}

QString Env::prov_name(const QString &pid) const
{
    const moddata::Province *pr = py::isdigit(pid) ? mod.provinces.get(py::to_int(pid)) : nullptr;
    return pr ? u"%1 (%2)"_s.arg(pr->name, pid) : pid;
}

QStringList Env::_keys(const QString &rel, const QStringList &path, int depth) const
{
    Nodes nodes;
    try {
        if (!store)
            return {};
        nodes = script::parse(store->text(rel));
    } catch (...) {
        return {};
    }
    const Nodes *cur = &nodes;
    static const Nodes NONE;
    for (const QString &k : path) {
        const Nodes *next = &NONE;
        for (const Node &n : *cur)
            if (n.has_key && n.key == k && n.is_block) {
                next = &n.kids;
                break;
            }
        cur = next;
    }
    QStringList out;
    if (depth == 2) {
        for (const Node &n : *cur)
            if (n.is_block)
                for (const Node &c : n.kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        out << c.key;
        return out;
    }
    for (const Node &n : *cur)
        if (n.has_key && !n.key.isEmpty() && n.is_block)
            out << n.key;
    return out;
}

static Choices sorted_by_name(Choices c)
{
    std::stable_sort(c.begin(), c.end(), [](const auto &a, const auto &b) { return py::lower(a.second) < py::lower(b.second); });
    return c;
}

std::optional<Choices> Env::choices(const QString &vtype, const OptStr &issue) const
{
    // [(key, name)] a value of this type may be, or None: anything typed.
    if (vtype == u"option" && issue && issues.contains(*issue)) {
        Choices out;
        for (const QString &o : issues.value(*issue))
            out.emplace_back(o, name(o));
        return out;
    }
    if (const auto it = _cache.constFind(vtype); it != _cache.cend())
        return *it;
    auto named = [&](const QStringList &keys) {
        Choices out;
        for (const QString &k : keys)
            out.emplace_back(k, name(k));
        return out;
    };
    auto keys_of = [](const auto &m) {
        QStringList out;
        for (const auto &kv : m)
            out << kv.first;
        return out;
    };
    auto set_keys = [](const QSet<QString> &s) {   // (Python: a set's order — equal names left to chance; here by key)
        QStringList l(s.begin(), s.end());
        std::sort(l.begin(), l.end());
        return l;
    };
    auto nations = [&] {
        Choices out;
        for (const auto &[t, c] : mod.countries)
            out.emplace_back(t, !c.name.isEmpty() ? c.name : t);
        return sorted_by_name(out);
    };
    std::optional<Choices> out = Choices();
    if (vtype == u"tag" || vtype == u"tagbool" || vtype == u"tagc") {
        Choices c = {{u"THIS"_s, u"this nation (THIS)"_s}, {u"FROM"_s, u"the nation it came from (FROM)"_s},
                     {u"owner"_s, u"the province's owner"_s}, {u"controller"_s, u"who holds the province"_s}};
        for (const auto &x : nations())
            c.push_back(x);
        if (vtype == u"tagbool")
            c.insert(c.begin(), {{u"yes"_s, u"yes"_s}, {u"no"_s, u"no"_s}});
        if (vtype == u"tagc")
            c.insert(c.begin(), {u"culture"_s, u"the union nation of its culture group"_s});
        out = c;
    } else if (vtype == u"tagonly") {
        out = nations();
    } else if (vtype == u"issue") {
        out = named(keys_of(issues));
    } else if (vtype == u"personality" || vtype == u"background") {
        out = named(_keys(u"common/traits.txt"_s, {vtype}));
    } else if (vtype == u"bool") {
        out = Choices{{u"yes"_s, u"yes"_s}, {u"no"_s, u"no"_s}};
    } else if (vtype == u"strata") {
        out = Choices{{u"poor"_s, u"poor"_s}, {u"middle"_s, u"middle class"_s}, {u"rich"_s, u"rich"_s}};
    } else if (vtype == u"culture") {
        out = sorted_by_name(named(keys_of(mod.cultures)));
    } else if (vtype == u"cgroup") {
        out = sorted_by_name(named(mod.culture_groups));
    } else if (vtype == u"religion") {
        out = sorted_by_name(named(keys_of(mod.religions)));
    } else if (vtype == u"goods") {
        out = sorted_by_name(named(keys_of(mod.goods)));
    } else if (vtype == u"poptype") {
        out = sorted_by_name(named(keys_of(mod.poptypes)));
    } else if (vtype == u"tech") {
        out = sorted_by_name(named(set_keys(techs)));
    } else if (vtype == u"invention") {
        out = sorted_by_name(named(set_keys(inventions)));
    } else if (vtype == u"ideology") {
        out = named(_keys(u"common/ideologies.txt"_s, {}, 2));
    } else if (vtype == u"gov") {
        out = named(_keys(u"common/governments.txt"_s));
    } else if (vtype == u"nv") {
        out = named(_keys(u"common/nationalvalues.txt"_s));
    } else if (vtype == u"option") {
        out = named(options);
    } else if (vtype == u"mod") {
        QStringList keys;
        for (const QString &rel : {u"common/event_modifiers.txt"_s, u"common/triggered_modifiers.txt"_s,
                                   u"common/static_modifiers.txt"_s})
            for (const QString &k : _keys(rel))
                if (!keys.contains(k))
                    keys << k;
        out = sorted_by_name(named(keys));
    } else if (vtype == u"region") {
        Choices c;
        for (const auto &[k, r] : mod.regions)
            c.emplace_back(k, !r.name.isEmpty() ? r.name : k);
        out = sorted_by_name(c);
    } else if (vtype == u"continent") {
        out = named(_keys(u"map/continent.txt"_s));
    } else if (vtype == u"building") {
        Choices c = named(_keys(u"common/buildings.txt"_s));
        c.emplace_back(u"factory"_s, u"any factory"_s);
        out = c;
    } else if (vtype == u"crime") {
        out = named(_keys(u"common/crime.txt"_s));
    } else if (vtype == u"cb") {
        QStringList ks;
        for (const QString &k : _keys(u"common/cb_types.txt"_s))
            if (k != u"peace_order")
                ks << k;
        out = named(ks);
    } else if (vtype == u"terrain") {
        out = named(_keys(u"map/terrain.txt"_s, {u"categories"_s}));
    } else if (vtype == u"unit") {
        QStringList keys;
        for (const QString &rel : files_of(mod, store, u"units"_s))
            keys += _keys(rel);
        out = named(keys);
    } else if (vtype == u"cflag" || vtype == u"gflag" || vtype == u"pflag" || vtype == u"var") {
        Choices c;
        if (refs)
            for (const auto &[k, lst] : refs->uses)
                if (k.first == vtype && !lst.empty())
                    c.emplace_back(k.second, k.second);
        std::sort(c.begin(), c.end());
        out = c;
    } else if (vtype == u"event") {
        Choices c;
        if (db)
            for (const events::EntryP &e : db->entries)
                if (e->is_event() && e->id && !e->id->isEmpty())
                    c.emplace_back(*e->id, u"%1 — %2"_s.arg(*e->id, db->name(*e)));
        out = c;
    } else {
        return std::nullopt;
    }
    _cache.insert(vtype, out);
    return out;
}

bool Env::free_text(const QString &vtype) const
{
    // A new name may be typed (a flag or number nothing uses yet, a text …).
    static const QSet<QString> FREE = {u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s, u"text"_s, u"tagbool"_s};
    return FREE.contains(vtype) || !choices(vtype);
}

OptStr Env::first(const QString &vtype) const
{
    if (vtype == u"prov") {
        if (mod.provinces.isEmpty())
            return std::nullopt;
        long long m = mod.provinces.begin()->first;
        for (const auto &kv : mod.provinces)
            m = std::min(m, kv.first);
        return QString::number(m);
    }
    static const QSet<QString> NONE = {u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s, u"text"_s, u"event"_s};
    if (NONE.contains(vtype))
        return std::nullopt;
    const auto ch = choices(vtype);
    return (ch && !ch->empty()) ? OptStr((*ch)[0].first) : std::nullopt;
}

bool Env::legal(const QString &vtype, const QString &v0, const OptStr &issue) const
{
    // Is the value one the game takes here?
    const QString v = py::strip(v0);
    if (vtype == u"num")
        return num_re(v);
    if (vtype == u"int")
        return py::re_match(u"^-?\\d+$"_s, v).hasMatch();
    if (vtype == u"prov")
        return py::isdigit(v) && mod.provinces.contains(py::to_int(v));
    if (vtype == u"cflag" || vtype == u"gflag" || vtype == u"pflag" || vtype == u"var")
        return plain_re(v);
    if (vtype == u"text")
        return !v.isEmpty() && !v.contains(u'"');
    if (vtype == u"event")
        return py::isdigit(v);
    if (vtype == u"tag" || vtype == u"tagbool" || vtype == u"tagc") {   // (the game reads this / THIS / This alike)
        const QString up = py::upper(v);
        if (up == u"THIS" || up == u"FROM" || v == u"owner" || v == u"controller" || mod.countries.contains(v))
            return true;
        if (vtype == u"tagbool")
            return v == u"yes" || v == u"no";
        if (vtype == u"tagc")
            return v == u"culture";
        return false;
    }
    const auto ch = choices(vtype, issue);
    if (!ch)
        return true;
    return std::any_of(ch->begin(), ch->end(), [&](const auto &kv) { return kv.first == v; });
}

QString sentence(const Item &it, const Spec *spec, const Env &env)
{
    // A block as one plain line (the network's hover text, the outline).
    if (!spec)
        return u"%1 = %2"_s.arg(it.k ? *it.k : u"None"_s, it.list ? u"{ … }"_s : it.text);
    auto show = [&](const QString &vtype, const QString &v) -> QString {
        if (vtype == u"prov")
            return env.prov_name(v);
        if (vtype == u"tagc" && v == u"culture")
            return u"the union nation of its culture group"_s;
        static const QSet<QString> PLAIN = {u"num"_s, u"int"_s, u"bool"_s, u"text"_s, u"event"_s,
                                            u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s};
        if (PLAIN.contains(vtype))
            return v;
        return env.name(v);
    };
    QString text = spec->text;
    if (spec->keytype)
        text.replace(u"{key}"_s, show(KEY_TYPE.value(*spec->keytype), it.k.value_or(u"None"_s)));
    static const Items NO_ITEMS;
    const Items &kids = it.list ? it.items : NO_ITEMS;
    if (spec->vtype == OptStr(u"block"_s)) {
        QString t = text;
        if (spec->fields)
            for (const auto &[f, ty] : *spec->fields)
                t.replace(u'{' + f + u'}', show(ty, *value(kids, f, u"?"_s)));
        return t;
    }
    if (spec->vtype == OptStr(u"fields"_s)) {
        QString t = text;
        if (it.list)
            for (const auto &[f, ty] : *spec->fields)
                t.replace(u'{' + f + u'}', show(ty, *value(kids, f, u"—"_s)));
        return t;
    }
    if (spec->vtype == OptStr(u"event"_s) && it.list)    // country_event = { id = 5 days = 30 }
        return u"%1 after %2 days"_s.arg(QString(text).replace(u"{}"_s, *value(kids, u"id"_s, u"?"_s)),
                                         *value(kids, u"days"_s, u"0"_s));
    return text.replace(u"{}"_s, !it.list ? show(spec->vtype.value_or(QString()), it.text) : u"{ … }"_s);
}

}  // namespace blockcode
