// nodegraph.py — Scripts → ⧉ Nodes (see nodegraph.h).
#include "ui/nodegraph.h"
#include "ui/drawtext.h"
#include "core/document.h"
#include "core/events.h"
#include "core/eventtools.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/reid.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace nodegraph {

namespace bc = blockcode;

static const OrderedMap<QString, QString> COLOURS_0 = {
    {u"cond"_s, u"#5b8fd6"_s}, {u"eff"_s, u"#d98a3d"_s}, {u"goal"_s, u"#d9703d"_s}, {u"entry"_s, u"#c9a15a"_s},
    {u"opt"_s, u"#c9a15a"_s}, {u"weight"_s, u"#9a7fd1"_s}, {u"mod"_s, u"#4fb3a5"_s}, {u"list"_s, u"#c76b8f"_s}};
OrderedMap<QString, QString> COLOURS = COLOURS_0;
const OrderedMap<QString, QString> TYPE_NAME = {
    {u"cond"_s, u"conditions"_s}, {u"eff"_s, u"effects"_s}, {u"goal"_s, u"war goals"_s}, {u"opt"_s, u"choices"_s},
    {u"weight"_s, u"how often"_s}, {u"mod"_s, u"what changes it"_s}, {u"list"_s, u"possibilities"_s}};
const QString RED = u"#ff786e"_s;

long long count_inside(const PItem &item)
{
    long long n = 0;
    std::vector<const PList *> stack;
    if (item.list)
        stack.push_back(&item.items);
    while (!stack.empty()) {
        const PList *l = stack.back();
        stack.pop_back();
        for (const PPtr &c : *l)
            if (c->k != OptStr(bc::NOTE)) {
                n += 1;
                if (c->list)
                    stack.push_back(&c->items);
            }
    }
    return n;
}

const QStringList CONTAINERS = {u"trigger"_s, u"immediate"_s, u"potential"_s, u"allow"_s, u"effect"_s};
const OrderedMap<QString, QString> ENTRY_TYPES = {
    {u"id"_s, u"int"_s}, {u"fire_only_once"_s, u"bool"_s}, {u"major"_s, u"bool"_s}, {u"is_triggered_only"_s, u"bool"_s},
    {u"news"_s, u"bool"_s}, {u"allow_multiple_instances"_s, u"bool"_s}, {u"alert"_s, u"bool"_s},
    {u"election"_s, u"bool"_s}};
const OrderedMap<QString, QStringList> ENTRY_MORE = {
    {u"event"_s, {u"picture"_s, u"fire_only_once"_s, u"major"_s, u"is_triggered_only"_s, u"allow_multiple_instances"_s,
                  u"news"_s}},
    {u"decision"_s, {u"picture"_s, u"news"_s, u"alert"_s}}};
const QStringList PLAIN = {u"num"_s,  u"int"_s,   u"bool"_s,  u"text"_s, u"event"_s,
                           u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s};
const std::vector<What> WHAT = {
    {u"and"_s, u"AND — all of them"_s, u"#6fbf73"_s}, {u"or"_s, u"OR — any of them"_s, u"#e3c341"_s},
    {u"not"_s, u"NOT"_s, u"#e0665e"_s}, {u"scope"_s, u"whose (a scope)"_s, u"#4fb3a5"_s},
    {u"flag"_s, u"flag"_s, u"#d977c8"_s}, {u"var"_s, u"kept number (variable)"_s, u"#a488e0"_s},
    {u"modifier"_s, u"modifier"_s, u"#b98a5e"_s}, {u"event"_s, u"event"_s, u"#e8b04a"_s},
    {u"nation"_s, u"nation"_s, u"#5fb8e0"_s}, {u"place"_s, u"province / state"_s, u"#8fbf5a"_s},
    {u"name"_s, u"a name of the mod"_s, u"#c9bf8a"_s}, {u"yesno"_s, u"yes / no"_s, u"#93a0b8"_s},
    {u"number"_s, u"number"_s, u"#7f93c9"_s}, {u"chance"_s, u"chance"_s, u"#c76b8f"_s},
    {u"other"_s, u"not in the catalogue"_s, u"#7b8089"_s}};

static OrderedMap<QString, QString> what_col_0()
{
    OrderedMap<QString, QString> m;
    for (const What &w : WHAT)
        m.insert(w.key, w.colour);
    return m;
}
OrderedMap<QString, QString> WHAT_COL = what_col_0();

// Colour schemes (the user, 2026-10-26: "the color schemes and windows could be improved further"). "vivid" is the
// look of 2026-10-10 (full-colour heads, dark letters); "calm" has muted colours of one lightness, dark heads with a
// stripe of the colour and light letters; "contrast" is for bright rooms / weak eyes.
const OrderedMap<QString, Scheme> &SCHEMES()
{
    static const OrderedMap<QString, Scheme> s = {
        {u"calm"_s,
         Scheme{{{u"cond"_s, u"#6f9fd8"_s}, {u"eff"_s, u"#d9a066"_s}, {u"goal"_s, u"#d98466"_s}, {u"entry"_s, u"#c8ad6e"_s},
                 {u"opt"_s, u"#c8ad6e"_s}, {u"weight"_s, u"#a993d6"_s}, {u"mod"_s, u"#68b8ab"_s}, {u"list"_s, u"#c98aa6"_s}},
                {{u"and"_s, u"#79b77d"_s}, {u"or"_s, u"#d7bf5c"_s}, {u"not"_s, u"#d47a72"_s}, {u"scope"_s, u"#5fb3a8"_s},
                 {u"flag"_s, u"#c98bc0"_s}, {u"var"_s, u"#a58fd4"_s}, {u"modifier"_s, u"#b9946a"_s},
                 {u"event"_s, u"#d9b062"_s}, {u"nation"_s, u"#6fb0d6"_s}, {u"place"_s, u"#94b86c"_s},
                 {u"name"_s, u"#bdb38e"_s}, {u"yesno"_s, u"#9aa5b8"_s}, {u"number"_s, u"#8ea0cc"_s},
                 {u"chance"_s, u"#c98aa6"_s}, {u"other"_s, u"#80868f"_s}},
                u"tint"_s, u"#15181d"_s, u"#1e2229"_s, u"#121418"_s, u"dots"_s, u"#e9e3d6"_s, 3.2, std::nullopt}},
        {u"vivid"_s, Scheme{COLOURS_0, what_col_0(), u"fill"_s, u"#1a1d23"_s, u"#232830"_s, u"#14171c"_s, u"lines"_s,
                            u"#e9e3d6"_s, 3.6, std::nullopt}},
        {u"contrast"_s,
         Scheme{{{u"cond"_s, u"#4da6ff"_s}, {u"eff"_s, u"#ff9a2e"_s}, {u"goal"_s, u"#ff7a3d"_s}, {u"entry"_s, u"#ffd36b"_s},
                 {u"opt"_s, u"#ffd36b"_s}, {u"weight"_s, u"#c09bff"_s}, {u"mod"_s, u"#3fe0c8"_s}, {u"list"_s, u"#ff7ab8"_s}},
                {{u"and"_s, u"#5ce36a"_s}, {u"or"_s, u"#ffe14a"_s}, {u"not"_s, u"#ff5a50"_s}, {u"scope"_s, u"#3fe0c8"_s},
                 {u"flag"_s, u"#ff7ae8"_s}, {u"var"_s, u"#c09bff"_s}, {u"modifier"_s, u"#e0a060"_s},
                 {u"event"_s, u"#ffc040"_s}, {u"nation"_s, u"#4dd2ff"_s}, {u"place"_s, u"#a8f060"_s},
                 {u"name"_s, u"#f0e6a8"_s}, {u"yesno"_s, u"#c8d2e6"_s}, {u"number"_s, u"#a8b8ff"_s},
                 {u"chance"_s, u"#ff7ab8"_s}, {u"other"_s, u"#a0a6b0"_s}},
                u"fill"_s, u"#0b0c0e"_s, u"#16181c"_s, u"#000000"_s, u"lines"_s, u"#ffffff"_s, 4.4, u"#8d97a8"_s}}};
    return s;
}
const std::vector<std::pair<QString, QString>> SCHEME_NAMES = {
    {u"calm"_s, u"Calm"_s}, {u"vivid"_s, u"Vivid"_s}, {u"contrast"_s, u"High contrast"_s}};
Scheme STYLE = *SCHEMES().get(u"calm"_s);

void set_scheme(const QString &name)
{
    // Every node and wire takes this scheme's colours (the tables are changed in place: everything reads them).
    const Scheme *s = SCHEMES().get(name);
    if (!s)
        s = SCHEMES().get(u"calm"_s);
    STYLE = *s;
    for (const auto &kv : s->where)
        COLOURS.insert(kv.first, kv.second);
    for (const auto &kv : s->what)
        WHAT_COL.insert(kv.first, kv.second);
}

QColor _mix(const QColor &a, const QColor &b, double t)
{
    // Colour a mixed with b: t = how much of a.
    return QColor(int(a.red() * t + b.red() * (1 - t)), int(a.green() * t + b.green() * (1 - t)),
                  int(a.blue() * t + b.blue() * (1 - t)));
}

static const bool _contrast = (set_scheme(u"contrast"_s), true);

QString what_of(const bc::Spec *spec)
{
    // What a line of the catalogue is, for the colour of its head.
    if (!spec)
        return u"other"_s;
    if (spec->key == u"AND" || spec->key == u"OR" || spec->key == u"NOT")
        return spec->key.toLower();
    const QString kind = spec->kind();
    if (kind == u"chance")
        return u"chance"_s;
    if (kind == u"scope" || spec->key == u"limit")
        return u"scope"_s;
    static const QString NONE = u"\x01None"_s;           // (Python's None in the set of types)
    QSet<QString> types = {spec->vtype.value_or(NONE)};
    if (spec->fields)
        for (const auto &f : *spec->fields)
            types.insert(f.second);
    static const std::vector<std::pair<QString, QSet<QString>>> KINDS = {
        {u"flag"_s, {u"cflag"_s, u"gflag"_s, u"pflag"_s}}, {u"var"_s, {u"var"_s}}, {u"modifier"_s, {u"mod"_s}},
        {u"event"_s, {u"event"_s}}, {u"nation"_s, {u"tag"_s, u"tagc"_s, u"tagbool"_s, u"tagonly"_s}},
        {u"place"_s, {u"prov"_s, u"region"_s, u"continent"_s, u"terrain"_s}}};
    for (const auto &[what, tys] : KINDS)
        if (types.intersects(tys))
            return what;
    const QSet<QString> NUMS = {u"num"_s, u"int"_s};
    if (types.intersects(NUMS) && (types - QSet<QString>{u"num"_s, u"int"_s, u"fields"_s, u"block"_s, NONE}).isEmpty())
        return u"number"_s;
    if (types == QSet<QString>{u"bool"_s})
        return u"yesno"_s;
    return !(types - QSet<QString>{u"num"_s, u"int"_s, u"bool"_s, u"text"_s, u"fields"_s, u"block"_s, NONE}).isEmpty()
               ? u"name"_s
               : u"number"_s;
}

QFont _font(bool bold, int px)
{
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

static std::optional<qsizetype> _index(const PList &lst, const PItem *it)
{
    for (size_t i = 0; i < lst.size(); ++i)
        if (lst[i].get() == it)
            return qsizetype(i);
    return std::nullopt;
}

static QString kstr(const OptStr &k) { return k ? *k : u"None"_s; }   // str(item["k"])

static qsizetype first_option(const PList &lst)
{
    for (size_t i = 0; i < lst.size(); ++i)
        if (lst[i]->k == OptStr(u"option"_s))
            return qsizetype(i);
    return qsizetype(lst.size());
}

// ---------------------------------------------------------------------------------------------- Sock
PList *Sock::lst(bool make) const
{
    if (!sub)
        return holder;
    PItem *b = bc::block(*holder, *sub);
    if (!b && make) {
        const qsizetype at = *sub == u"limit" ? 0 : first_option(*holder);
        b = bc::block(*holder, *sub, true, at);
    }
    return b ? &b->items : nullptr;
}

std::vector<PPtr> Sock::members() const
{
    std::vector<PPtr> out;
    PList *l = lst();
    if (!l)
        return out;
    for (const PPtr &it : *l)
        if (it->k != OptStr(bc::NOTE) && (!own || own(*it)))
            out.push_back(it);
    return out;
}

// ---------------------------------------------------------------------------------------------- Wire
Wire::Wire(NodeItem *node_) : node(node_)
{
    setZValue(1);
    setPen(QPen(QColor(10, 10, 10), 8));
    setFlag(QGraphicsItem::ItemIsSelectable, true);
}

QPainterPath Wire::shape() const
{
    QPainterPathStroker s;
    s.setWidth(14);
    return s.createStroke(path());
}

void Wire::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QColor col(COLOURS.value(node->type_(), u"#999999"_s));
    p->setBrush(Qt::NoBrush);
    const double lw = STYLE.line;
    p->setPen(QPen(QColor(10, 10, 10), lw + 3));
    p->drawPath(path());
    p->setPen(isSelected() ? QPen(QColor(theme::GOLD), lw + 0.6, Qt::DashLine) : QPen(col, lw));
    p->drawPath(path());
}

// ---------------------------------------------------------------------------------------------- NodeItem
NodeItem::NodeItem(NodeWindow *win_, PPtr item_, const QString &kind_, const QString &mode_, const OptStr &scope_)
    : win(win_), item(std::move(item_)), kind(kind_), mode(mode_), scope(scope_), uid(++win_->_serial)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(2);
    describe();
    measure();
}

QString NodeItem::type_() const
{
    static const QHash<QString, QString> T = {{u"entry"_s, u"entry"_s}, {u"option"_s, u"opt"_s},
                                              {u"weight"_s, u"weight"_s}, {u"mod"_s, u"mod"_s},
                                              {u"chance"_s, u"list"_s}};
    return T.value(kind, mode);
}

static bool is_blk(const PItem &c) { return c.list; }

void NodeItem::describe()
{
    PItem &it = *item;
    std::vector<PPtr> rows;
    if (it.list)
        for (const PPtr &c : it.items)
            if (c->k && *c->k != bc::NOTE && !c->list)
                rows.push_back(c);
    auto W = [](const QString &label, const PPtr &item_, const QString &vtype, const QString &attr = u"v"_s,
                const OptStr &issue = std::nullopt) { return Widget{label, item_, vtype, attr, issue}; };
    title = it.k ? *it.k : u"(a word)"_s;
    PList *v = &it.items;
    if (kind == u"entry") {
        for (const PPtr &c : rows)
            widgets.push_back(W(*c->k, c, ENTRY_TYPES.value(*c->k, u"text"_s)));
        const OptStr sc = scope;
        const std::vector<std::pair<QString, QString>> names =
            win->entry_kind == u"event"
                ? std::vector<std::pair<QString, QString>>{{u"trigger"_s, u"cond"_s},
                                                           {u"mean_time_to_happen"_s, u"weight"_s},
                                                           {u"immediate"_s, u"eff"_s},
                                                           {u"option"_s, u"opt"_s}}
                : std::vector<std::pair<QString, QString>>{{u"potential"_s, u"cond"_s},
                                                           {u"allow"_s, u"cond"_s},
                                                           {u"effect"_s, u"eff"_s},
                                                           {u"ai_will_do"_s, u"weight"_s}};
        for (const auto &[name, typ] : names) {
            if (CONTAINERS.contains(name))
                socks.push_back(Sock{name, typ, sc, v, name, nullptr});
            else
                socks.push_back(Sock{name, typ, sc, v, std::nullopt,
                                     [n = name](const PItem &c) { return c.k == OptStr(n) && is_blk(c); },
                                     typ == u"weight"});
        }
        for (const QString &k : ENTRY_MORE.value(win->entry_kind))
            if (std::none_of(rows.begin(), rows.end(), [&](const PPtr &c) { return c->k == OptStr(k); }))
                missing << k;
    } else if (kind == u"option") {
        for (const PPtr &c : rows)
            if (*c->k == u"name")
                widgets.push_back(W(*c->k, c, u"text"_s));
        socks = {Sock{u"effects"_s, u"eff"_s, scope, v, std::nullopt,
                      [](const PItem &c) {
                          return c.k != OptStr(u"name"_s) && !(c.k == OptStr(u"ai_chance"_s) && is_blk(c));
                      }},
                 Sock{u"ai_chance"_s, u"weight"_s, scope, v, std::nullopt,
                      [](const PItem &c) { return c.k == OptStr(u"ai_chance"_s) && is_blk(c); }, true}};
    } else if (kind == u"weight") {
        for (const PPtr &c : rows)
            widgets.push_back(W(*c->k, c, u"num"_s));
        socks = {Sock{u"modifier"_s, u"mod"_s, scope, v, std::nullopt,
                      [](const PItem &c) { return c.k == OptStr(u"modifier"_s) && is_blk(c); }}};
        for (const QString &k : {u"factor"_s, u"months"_s, u"years"_s, u"days"_s})
            if (std::none_of(rows.begin(), rows.end(), [&](const PPtr &c) { return c->k == OptStr(k); }))
                missing << k;
    } else if (kind == u"mod") {
        for (const PPtr &c : rows)
            if (*c->k == u"factor")
                widgets.push_back(W(*c->k, c, u"num"_s));
        socks = {Sock{u"conditions"_s, u"cond"_s, scope, v, std::nullopt,
                      [](const PItem &c) { return c.k != OptStr(u"factor"_s) || is_blk(c); }}};
    } else if (kind == u"chance") {
        title = u"possibility"_s;
        widgets = {W(u"weight"_s, item, u"int"_s, u"k"_s)};
        socks = {Sock{u"effects"_s, u"eff"_s, scope, v, std::nullopt, nullptr}};
    } else {
        _line(rows);
    }
}

void NodeItem::_line(const std::vector<PPtr> &rows)
{
    PItem &it = *item;
    const bc::Env *env = win->env.get();
    PList *v = &it.items;
    const bool blk = it.list;
    const bc::Item val = bc::shallow(it);
    const bc::Spec *sp = bc::spec_for(it.k, mode, scope, env, &val);
    auto W = [](const QString &label, const PPtr &item_, const QString &vtype, const QString &attr = u"v"_s,
                const OptStr &issue = std::nullopt) { return Widget{label, item_, vtype, attr, issue}; };
    auto has_row = [&](const QString &f) {
        return std::any_of(rows.begin(), rows.end(), [&](const PPtr &c) { return c->k == OptStr(f); });
    };
    if (sp && sp->vtype == OptStr(u"event"_s) && blk) {       // country_event = { id = … days = … }
        spec = sp;
        what = u"event"_s;
        const OrderedMap<QString, QString> types = {{u"id"_s, u"event"_s}, {u"days"_s, u"int"_s}};
        for (const PPtr &c : rows)
            widgets.push_back(W(*c->k, c, types.value(*c->k, u"text"_s)));
        for (const auto &kv : types)
            if (!has_row(kv.first))
                missing << kv.first;
        return;
    }
    if (sp && ((sp->vtype == OptStr(u"block"_s) || sp->vtype == OptStr(u"fields"_s)) != blk) &&
        sp->vtype != OptStr(u"event"_s))
        sp = nullptr;
    spec = sp;
    what = what_of(sp);
    wrong = sp && !sp->fits(scope);
    if (sp && sp->keytype)
        widgets.push_back(W(u"which"_s, item, bc::KEY_TYPE.value(*sp->keytype), u"k"_s));
    if (!blk) {
        const OptStr issue = sp && sp->vtype == OptStr(u"option"_s) && it.k && env->issues.contains(*it.k)
                                 ? it.k
                                 : std::nullopt;
        const QString ty = sp ? sp->vtype.value_or(QString()) : u"text"_s;
        widgets.push_back(W(QString(), item, sp && sp->keytype == OptStr(u"tech"_s) ? u"num"_s : ty, u"v"_s, issue));
        return;
    }
    OrderedMap<QString, QString> types;
    if (sp && sp->fields)
        for (const auto &f : *sp->fields)
            types.insert(f.first, f.second);
    if (sp && sp->vtype == OptStr(u"fields"_s)) {
        for (const PPtr &c : rows)
            widgets.push_back(W(*c->k, c, types.value(*c->k, u"text"_s)));
        for (const auto &f : *sp->fields)
            if (!has_row(f.first))
                missing << f.first;
        return;
    }
    for (const PPtr &c : rows)
        if (types.contains(*c->k))
            widgets.push_back(W(*c->k, c, *types.get(*c->k)));
    for (const auto &kv : types)
        if (!has_row(kv.first))
            missing << kv.first;
    const QString inner = sp && sp->inner && (*sp->inner == u"cond" || *sp->inner == u"eff" || *sp->inner == u"goal")
                              ? *sp->inner
                              : mode;
    const OptStr in_scope = sp && sp->out && !sp->out->isEmpty() ? sp->out : scope;
    auto not_field = [types](const PItem &c) { return !(c.k && types.contains(*c.k) && !c.list); };
    if (sp && sp->inner == OptStr(u"list"_s))
        socks = {Sock{u"possibilities"_s, u"list"_s, in_scope, v, std::nullopt, [](const PItem &c) { return c.list; }}};
    else if (inner == u"eff")
        socks = {Sock{u"limit"_s, u"cond"_s, in_scope, v, u"limit"_s, nullptr},
                 Sock{u"effects"_s, u"eff"_s, in_scope, v, std::nullopt, [not_field](const PItem &c) {
                          return not_field(c) && !(c.k == OptStr(u"limit"_s) && c.list);
                      }}};
    else
        socks = {Sock{TYPE_NAME.value(inner, inner), inner, in_scope, v, std::nullopt, not_field}};
}

static QString attr_str(const Widget &wd)
{
    return wd.attr == u"k" ? kstr(wd.item->k) : wd.item->text;
}

QString NodeItem::shown(const Widget &wd) const
{
    // A value as the node shows it.
    const QString v = attr_str(wd);
    const QString &ty = wd.vtype;
    const bc::Env *env = win->env.get();
    if (PLAIN.contains(ty) || v.isEmpty())
        return v.isEmpty() ? u"—"_s : v;
    if (ty == u"prov")
        return env->prov_name(v);
    const QString name = env->name(v);
    return name.isEmpty() || py::lower(name) == py::lower(QString(v).replace(u'_', u' ')) || name == v
               ? v
               : u"%1 · %2"_s.arg(v, name);
}

void NodeItem::measure()
{
    prepareGeometryChange();
    const QFontMetricsF fm(_font()), fb(_font(true));
    double ww = fb.horizontalAdvance(title) + 80;
    for (const Sock &s : socks)
        ww = std::max(ww, fm.horizontalAdvance(s.name) + 70);
    for (const Widget &wd : widgets)
        ww = std::max(ww, fm.horizontalAdvance(wd.label) + fm.horizontalAdvance(shown(wd)) + 62);
    w = std::max(W_MIN, std::min(W_MAX, ww));
    h = HEAD + ROW * double(socks.size() + widgets.size() + (folded ? 1 : 0)) + 8;
    if (folded)
        w = std::max(w, 250.0);
}

QRectF NodeItem::fold_rect() const { return QRectF(w - (missing.isEmpty() ? 44 : 66), 5, 18, 18); }

QRectF NodeItem::unfold_rect() const
{
    return QRectF(12, HEAD + ROW * double(socks.size() + widgets.size()) + 1, w - 22, ROW - 2);
}

QRectF NodeItem::boundingRect() const { return QRectF(-13, -5, w + 26, h + 10); }

QPainterPath NodeItem::shape() const
{
    QPainterPath p;
    p.addRect(boundingRect());
    return p;
}

double NodeItem::sock_y(int i) const { return HEAD + ROW * i + ROW / 2; }

QPointF NodeItem::in_dot(int i) const { return mapToScene(QPointF(w, sock_y(i))); }   // (inputs on the right)

QPointF NodeItem::out_dot() const { return mapToScene(QPointF(0, HEAD / 2)); }        // (what a node gives: its left)

QRectF NodeItem::add_rect(int i) const { return QRectF(w - 30, HEAD + ROW * i + 2, 22, ROW - 4); }

QRectF NodeItem::pill_rect(int j) const
{
    const double y = HEAD + ROW * double(socks.size() + size_t(j));
    const QFontMetricsF fm(_font());
    const QString &lb = widgets[size_t(j)].label;
    const double x = 17 + (!lb.isEmpty() ? fm.horizontalAdvance(lb) + 10 : 0);
    return QRectF(x, y + 2, w - x - 10, ROW - 4);
}

QRectF NodeItem::more_rect() const { return QRectF(w - 44, 5, 18, 18); }

std::optional<Hit> NodeItem::hit(const QPointF &p)
{
    // What of the node is at a place (the node's own coordinates): (what, node, index).
    if (kind != u"entry" && py::hypot(p.x(), p.y() - HEAD / 2) <= 12)
        return Hit{u"out"_s, this, std::nullopt};
    for (int i = 0; i < int(socks.size()); ++i) {
        if (py::hypot(p.x() - w, p.y() - sock_y(i)) <= 12)
            return Hit{u"in"_s, this, i};
        if (add_rect(i).contains(p))
            return Hit{u"add"_s, this, i};
    }
    for (int j = 0; j < int(widgets.size()); ++j)
        if (pill_rect(j).contains(p))
            return Hit{u"pill"_s, this, j};
    if (!missing.isEmpty() && more_rect().contains(p))
        return Hit{u"more"_s, this, std::nullopt};
    if (folded && unfold_rect().contains(p))
        return Hit{u"unfold"_s, this, std::nullopt};
    if (!children.empty() && kind != u"entry" && fold_rect().contains(p))
        return Hit{u"fold"_s, this, std::nullopt};
    if (QRectF(0, 0, w, h).contains(p)) {
        const int i = p.y() >= HEAD ? int(py::floordiv(p.y() - HEAD, ROW)) : -1;
        return Hit{u"node"_s, this, 0 <= i && i < int(socks.size()) ? std::optional<int>(i) : std::nullopt};
    }
    return std::nullopt;
}

void NodeItem::_plus(QPainter *p, const QRectF &r)
{
    const QPointF c = r.center();
    p->drawRoundedRect(r, 4, 4);
    p->drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
    p->drawLine(QPointF(c.x(), c.y() - 5), QPointF(c.x(), c.y() + 5));
}

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QColor ctx(COLOURS.value(type_(), u"#8a8f98"_s));             // where it is: a condition, an effect …
    const QColor col = kind == u"line" && what ? QColor(WHAT_COL.value(*what)) : ctx;   // what it is
    const bool loose = !parent_ && kind != u"entry";
    const bool tint = STYLE.head == u"tint";
    const QRectF body(0, 0, w, h);
    const QColor bodyc(STYLE.body);
    if (tint) {                                                          // a soft shadow under the node
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 90));
        p->drawRoundedRect(body.translated(3, 4), 8, 8);
    }
    p->setPen(QPen(QColor(10, 10, 10), !tint ? 3 : 1.6));
    p->setBrush(bodyc);
    p->drawRoundedRect(body, 7, 7);
    if (STYLE.edge && !STYLE.edge->isEmpty()) {                          // a light rim: the box stands off the background
        p->setPen(QPen(QColor(*STYLE.edge), 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(body.adjusted(-2.5, -2.5, 2.5, 2.5), 9, 9);
    }
    QPainterPath head;
    head.addRoundedRect(QRectF(1.5, 1.5, w - 3, HEAD), 6, 6);
    if (tint) {                                                          // dark head, the colour as a stripe on top
        p->fillPath(head, _mix(col, bodyc, 0.30));
        p->fillRect(QRectF(1.5, HEAD - 7, w - 3, 8.5), _mix(col, bodyc, 0.30));
        QPainterPath stripe;
        stripe.addRoundedRect(QRectF(1.5, 1.5, w - 3, 5), 3, 3);
        p->fillPath(stripe, col);
    } else {
        p->fillPath(head, col);
        p->fillRect(QRectF(1.5, HEAD - 7, w - 3, 8.5), col);
    }
    if (kind == u"line") {                                               // the left edge: condition blue / effect orange
        QPainterPath edge;
        edge.addRoundedRect(QRectF(1.5, 1.5, tint ? 7 : 9, h - 3), 4, 4);
        p->fillPath(edge, ctx);
        p->fillRect(QRectF(5, 1.5, tint ? 3.5 : 5.5, h - 3), ctx);
        if (!tint) {
            p->setPen(QPen(QColor(10, 10, 10), 1.5));
            p->drawLine(QPointF(10.5, 1.5), QPointF(10.5, h - 1.5));
        }
    }
    const QColor ink = tint ? QColor(STYLE.ink) : QColor(18, 14, 8);
    p->setFont(_font(true));
    p->setPen(ink);
    const QString t = (wrong ? u"⚠ "_s : QString()) + title;
    drawtext::draw(*p, QRectF(18, 0, w - 68, HEAD), Qt::AlignVCenter | Qt::AlignLeft,
                p->fontMetrics().elidedText(t, Qt::ElideRight, int(w - 70)));
    if (!missing.isEmpty()) {
        p->setPen(QPen(ink, 2));
        p->setBrush(Qt::NoBrush);
        _plus(p, more_rect());
    }
    p->setFont(_font());
    for (int i = 0; i < int(socks.size()); ++i) {
        const Sock &s = socks[size_t(i)];
        const double y = HEAD + ROW * i;
        const QColor sc(COLOURS.value(s.type, u"#999999"_s));
        p->setPen(sc.lighter(125));
        drawtext::draw(*p, QRectF(18, y, w - 52, ROW), Qt::AlignVCenter | Qt::AlignLeft, s.name);
        p->setPen(QPen(sc.lighter(125), 2));
        p->setBrush(_mix(sc, bodyc, 0.18));
        _plus(p, add_rect(i));
        p->setPen(QPen(QColor(10, 10, 10), 2.5));
        p->setBrush(s.count ? sc.lighter(120) : _mix(sc, bodyc, 0.22));
        p->drawEllipse(QPointF(w, sock_y(i)), 8, 8);
    }
    for (int j = 0; j < int(widgets.size()); ++j) {
        const Widget &wd = widgets[size_t(j)];
        const double y = HEAD + ROW * double(socks.size() + size_t(j));
        const QRectF r = pill_rect(j);
        if (!wd.label.isEmpty()) {
            p->setPen(QColor(theme::INK_DIM));
            drawtext::draw(*p, QRectF(17, y, r.left() - 17, ROW), Qt::AlignVCenter | Qt::AlignLeft, wd.label);
        }
        p->setPen(QPen(QColor(10, 10, 10), 1.5));
        p->setBrush(QColor(STYLE.field));
        p->drawRoundedRect(r, 5, 5);
        p->setPen(QColor(STYLE.ink));
        drawtext::draw(*p, r.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(shown(wd), Qt::ElideRight, int(r.width() - 14)));
    }
    if (!children.empty() && kind != u"entry") {                         // ▾: fold what is wired in
        const QRectF r = fold_rect();
        p->setPen(QPen(ink, 2));
        p->setBrush(ink);
        const QPointF c = r.center();
        QPainterPath tri(QPointF(c.x() - 6, c.y() - 3));
        tri.lineTo(c.x() + 6, c.y() - 3);
        tri.lineTo(c.x(), c.y() + 5);
        tri.closeSubpath();
        p->drawPath(tri);
    }
    if (folded) {                                                        // ▸ n lines inside
        const QRectF r = unfold_rect();
        p->setPen(QPen(QColor(10, 10, 10), 1.5));
        p->setBrush(QColor(u"#3a3323"_s));
        p->drawRoundedRect(r, 5, 5);
        p->setPen(QColor(theme::GOLD));
        p->setFont(_font(true));
        drawtext::draw(*p, r.adjusted(8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    u"▸  %1 line%2 inside — click to open"_s.arg(inside).arg(inside == 1 ? QString() : u"s"_s));
        p->setFont(_font());
    }
    if (kind != u"entry") {
        p->setPen(QPen(QColor(10, 10, 10), 2.5));
        p->setBrush(ctx.lighter(130));
        p->drawEllipse(QPointF(0, HEAD / 2), 8, 8);
    }
    if (isSelected() || loose) {
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(isSelected() ? QColor(theme::GOLD) : QColor(theme::WARN), 2.6,
                       isSelected() ? Qt::SolidLine : Qt::DashLine));
        p->drawRoundedRect(body.adjusted(-3, -3, 3, 3), 9, 9);
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == QGraphicsItem::ItemPositionHasChanged) {
        win->pos[item.get()] = pos();
        win->route(this);
    } else if (change == QGraphicsItem::ItemSelectedHasChanged) {
        QTimer::singleShot(0, win, [w = QPointer<NodeWindow>(win)] {
            if (w)
                w->tell();
        });
    }
    return QGraphicsItem::itemChange(change, value);
}

// ---------------------------------------------------------------------------------------------- Canvas
Canvas::Canvas(NodeWindow *win_) : win(win_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setScene(new QGraphicsScene(this));
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setBackgroundBrush(QColor(u"#1a1d23"_s));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
}

static long long grid0(double v)                         // int(v) - int(v) % 40 (Python's % floors)
{
    return py::floordiv((long long)v, 40LL) * 40;
}

void Canvas::drawBackground(QPainter *p, const QRectF &rect)
{
    p->fillRect(rect, QColor(STYLE.canvas));
    if (STYLE.grid == u"dots") {                         // calm: a soft dot every 40 px
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 22));
        const long long x0 = grid0(rect.left()), y0 = grid0(rect.top());
        for (long long y = y0; double(y) < rect.bottom(); y += 40)
            for (long long x = x0; double(x) < rect.right(); x += 40)
                p->drawEllipse(QPointF(double(x), double(y)), 1.6, 1.6);
        return;
    }
    p->setPen(QPen(QColor(255, 255, 255, 14), 0));
    for (long long x = grid0(rect.left()); double(x) < rect.right(); x += 40)
        p->drawLine(QPointF(double(x), rect.top()), QPointF(double(x), rect.bottom()));
    for (long long y = grid0(rect.top()); double(y) < rect.bottom(); y += 40)
        p->drawLine(QPointF(rect.left(), double(y)), QPointF(rect.right(), double(y)));
}

void Canvas::wheelEvent(QWheelEvent *e)
{
    const double f = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const double z = transform().m11() * f;
    if (0.15 <= z && z <= 3) {
        win->close_editors();
        scale(f, f);
    }
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    win->close_editors();
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        _pan = _pan_from = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const QPoint vp = e->position().toPoint();
    const std::optional<Hit> h = win->hit(mapToScene(vp));
    const Qt::KeyboardModifiers mods = e->modifiers();
    if (e->button() == Qt::LeftButton && h && (h->what == u"out" || h->what == u"in")) {
        win->start_wire(*h);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && !h && !itemAt(vp)) {
        if (mods & Qt::ControlModifier) {                // Ctrl + drag on the canvas: a frame — what it holds is picked
            setDragMode(QGraphicsView::RubberBandDrag);
            _band = true;
            QGraphicsView::mousePressEvent(e);
            return;
        }
        scene()->clearSelection();                       // empty canvas: the view is dragged along
        _pan = _pan_from = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::LeftButton && h && (mods & Qt::ShiftModifier)) {
        NodeItem *node = h->node;                        // Shift + click: the box and every box below it (wired into
        QGraphicsScene *sc = scene();                    // it, at any depth) — dragged together
        sc->clearSelection();
        std::vector<NodeItem *> stack = {node};
        _keep.clear();
        while (!stack.empty()) {
            NodeItem *n = stack.back();
            stack.pop_back();
            n->setSelected(true);
            _keep.push_back(n);
            stack.insert(stack.end(), n->children.begin(), n->children.end());
        }
        _press = std::make_pair(std::optional<Hit>(), vp);
        QMouseEvent e2(e->type(), e->position(), e->globalPosition(), e->button(), e->buttons(), Qt::NoModifier);
        QGraphicsView::mousePressEvent(&e2);             // (pressed on a picked box: all of them move)
        return;
    }
    _press = std::make_pair(h, vp);
    QGraphicsView::mousePressEvent(e);
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (_pan) {
        const QPointF d = e->position() - *_pan;
        _pan = e->position();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(d.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - int(d.y()));
        return;
    }
    if (win->dragging) {
        win->drag_wire(mapToScene(e->position().toPoint()));
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e)
{
    const QPoint vp = e->position().toPoint();
    if (_pan) {
        const bool still = (e->position() - *_pan_from).manhattanLength() < 5;
        _pan.reset();
        unsetCursor();
        if (still && e->button() == Qt::RightButton && !win->hit(mapToScene(vp)))
            win->add_anywhere(mapToScene(vp), vp);       // a right click on the canvas: the menu
        return;
    }
    if (win->dragging) {
        win->end_wire(mapToScene(vp), vp);
        return;
    }
    if (_band) {
        QGraphicsView::mouseReleaseEvent(e);
        setDragMode(QGraphicsView::NoDrag);
        _band = false;
        int n = 0;
        for (QGraphicsItem *it : scene()->selectedItems())
            if (dynamic_cast<NodeItem *>(it))
                ++n;
        win->say(n ? u"%1 box%2 picked — drag one of them to move them all (Delete takes them out)"_s.arg(n).arg(
                         n == 1 ? QString() : u"es"_s)
                   : QString());
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    std::vector<NodeItem *> keep;
    keep.swap(_keep);
    if (!keep.empty()) {                                 // (a Shift + click: the branch stays picked)
        for (NodeItem *n : keep)
            n->setSelected(true);
        win->say(u"%1 boxes picked — the box and every box below it; drag one to move them all"_s.arg(keep.size()));
    }
    std::optional<std::pair<std::optional<Hit>, QPoint>> press;
    press.swap(_press);
    if (press && press->first && (vp - press->second).manhattanLength() < 6) {
        const QString &w = press->first->what;
        if (w == u"pill" || w == u"add" || w == u"more" || w == u"fold" || w == u"unfold") {
            const Hit hh = *press->first;
            const long long uid = hh.node->uid;
            QTimer::singleShot(0, win, [win_ = QPointer<NodeWindow>(win), hh, uid] {
                if (!win_)
                    return;
                NodeItem *n = win_->find(uid);
                if (!n)
                    return;                              // (Python: `if node not in self.nodes`)
                Hit h2 = hh;
                h2.node = n;
                win_->act(h2);
            });
        }
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPoint vp = e->position().toPoint();
    if (!win->hit(mapToScene(vp)) && !itemAt(vp)) {
        win->add_anywhere(mapToScene(vp), vp);
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void Canvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        win->delete_selected();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

// ---------------------------------------------------------------------------------------------- Picker
Picker::Picker(QWidget *parent) : QFrame(parent)
{
    setStyleSheet(u"QFrame { background: %1; border: 2px solid %2; border-radius: 6px; } "
                  "QLabel { border: none; } QListWidget { border: 1px solid %3; }"_s.arg(theme::PANEL, theme::GOLD,
                                                                                         theme::LINE));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    head = new QLabel(QString());
    head->setWordWrap(true);
    head->setStyleSheet(u"QLabel { color: %1; font-weight: 700; }"_s.arg(theme::GOLD));
    lay->addWidget(head);
    edit = new QLineEdit();
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &t) { fill(t); });
    edit->installEventFilter(this);
    lay->addWidget(edit);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    cats = new QListWidget();
    cats->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cats->setTextElideMode(Qt::ElideRight);
    cats->setFixedWidth(230);
    connect(cats, &QListWidget::currentRowChanged, this, [this](int) { fill(edit->text()); });
    row->addWidget(cats);
    lst = new QListWidget();
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setWordWrap(true);
    connect(lst, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { take(it); });
    row->addWidget(lst, 1);
    lay->addLayout(row, 1);
    count = new QLabel(QString());
    count->setObjectName(u"faint"_s);
    lay->addWidget(count);
    hide();
}

void Picker::open(const QPoint &vp, const QString &title, const std::vector<Row> &rows_, Take on_take_,
                  const OptStr &free_)
{
    // rows: [(text, data, colour or None[, group])]; free: what a typed text is (it is handed on as typed). With
    // groups the menu is wide and has them on the left.
    rows = rows_;
    on_take = std::move(on_take_);
    free = free_;
    head->setText(title);
    QStringList groups;
    for (const Row &r : rows)
        if (r.group && !r.group->isEmpty() && !groups.contains(*r.group))
            groups << *r.group;
    cats->blockSignals(true);
    cats->clear();
    if (!groups.isEmpty()) {
        cats->addItem(u"Everything  (%1)"_s.arg(rows.size()));
        cats->item(0)->setData(Qt::UserRole, QVariant());
        for (const QString &gname : groups) {
            const auto n = std::count_if(rows.begin(), rows.end(), [&](const Row &r) { return r.group == OptStr(gname); });
            auto *it = new QListWidgetItem(u"%1  (%2)"_s.arg(gname).arg(n));
            it->setData(Qt::UserRole, gname);
            for (const Row &r : rows)
                if (r.group == OptStr(gname) && r.colour && !r.colour->isEmpty()) {
                    it->setForeground(QColor(*r.colour));
                    break;
                }
            cats->addItem(it);
        }
        cats->setCurrentRow(0);
    }
    cats->blockSignals(false);
    cats->setVisible(!groups.isEmpty());
    QWidget *par = parentWidget();
    const int w = std::min(!groups.isEmpty() ? 900 : 560, par->width() - 20);
    const int h = std::min(!groups.isEmpty() ? 560 : 440, par->height() - 20);
    setGeometry(std::max(6, std::min(vp.x(), par->width() - w - 6)), std::max(6, std::min(vp.y(), par->height() - h - 6)),
                w, h);
    edit->setPlaceholderText(u"Type a few letters to search all of it …   (↑ ↓, Enter; Esc closes)"_s);
    edit->clear();
    fill(QString());
    show();
    raise();
    edit->setFocus();
}

void Picker::fill(const QString &text)
{
    const QStringList words = py::split(py::lower(text));
    QListWidgetItem *cur = cats->currentItem();
    QVariant group = cur && cats->count() ? cur->data(Qt::UserRole) : QVariant();
    if (!words.isEmpty())
        group = QVariant();                              // (a search goes through every group)
    lst->clear();
    int n = 0;
    std::vector<QListWidgetItem *> first;
    OptStr last;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row &r = rows[i];
        if (group.isValid() && r.group != OptStr(group.toString()))
            continue;
        const QString low = py::lower(r.text);
        if (std::all_of(words.begin(), words.end(), [&](const QString &w) { return low.contains(w); })) {
            const bool grp = r.group && !r.group->isEmpty();
            if (grp && !group.isValid() && r.group != last && words.isEmpty()) {
                auto *hd = new QListWidgetItem(*r.group);
                hd->setFlags(Qt::NoItemFlags);
                hd->setForeground(QColor(theme::INK_FAINT));
                lst->addItem(hd);
            }
            last = r.group;
            auto *it = new QListWidgetItem(r.text);
            it->setData(Qt::UserRole, QVariantList{u"row"_s, qlonglong(i)});
            if (r.colour && !r.colour->isEmpty())
                it->setForeground(QColor(*r.colour));
            if (!words.isEmpty() && low.startsWith(words[0]))
                first.push_back(it);
            else
                lst->addItem(it);
            n += 1;
            if (n >= 600)
                break;
        }
    }
    for (size_t k = 0; k < first.size(); ++k)            // (what begins with the typed letters comes first)
        lst->insertItem(int(k), first[k]);
    if (free && !text.trimmed().isEmpty()) {
        auto *it = new QListWidgetItem(u"“%1” — %2"_s.arg(text.trimmed(), *free));
        it->setData(Qt::UserRole, QVariantList{u"free"_s, text.trimmed()});
        lst->addItem(it);
    }
    count->setText(u"%1 of %2%3"_s.arg(n).arg(rows.size()).arg(words.isEmpty() ? QString() : u" — searched in every group"_s));
    _step(0, 0);
}

void Picker::_step(int d, std::optional<int> start)
{
    // The picked row moved by d (group heads are stepped over).
    const int n = lst->count();
    if (!n)
        return;
    auto pmod = [n](long long a) { return int(a - py::floordiv(a, (long long)n) * n); };   // Python's %
    int r = start ? *start : lst->currentRow();
    r = pmod(r + d);
    for (int k = 0; k < n; ++k) {
        if (lst->item(r)->data(Qt::UserRole).isValid()) {
            lst->setCurrentRow(r);
            return;
        }
        r = pmod(r + (d ? d : 1));
    }
}

bool Picker::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == edit && e->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent *>(e)->key();
        if ((k == Qt::Key_Down || k == Qt::Key_Up) && lst->count()) {
            _step(k == Qt::Key_Down ? 1 : -1);
            return true;
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            take(lst->currentItem());
            return true;
        }
        if (k == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QFrame::eventFilter(obj, e);
}

void Picker::take(QListWidgetItem *it)
{
    if (!it || !it->data(Qt::UserRole).isValid())
        return;
    hide();
    if (!on_take)
        return;
    const QVariantList d = it->data(Qt::UserRole).toList();
    const bool is_free = d[0].toString() == u"free";
    Take fn;
    fn.swap(on_take);
    fn(is_free ? PickData{d[1].toString(), QString(), nullptr} : rows[size_t(d[1].toLongLong())].data, is_free);
}

// ---------------------------------------------------------------------------------------------- NodeWindow
NodeWindow::NodeWindow(moddata::Mod &mod_, QWidget *parent) : QWidget(parent, Qt::Window), mod(mod_)
{
    setWindowTitle(u"Nodes"_s);
    setObjectName(u"nodewin"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(u"QWidget#nodewin { background: %1; }"_s.arg(theme::BG));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(12, 8, 12, 8);
    bar->setSpacing(12);
    head = new QLabel(u"Pick an event or a decision in the list of the Scripts page."_s);
    head->setObjectName(u"title"_s);
    bar->addWidget(head);
    bar->addStretch(1);
    auto *b = new QPushButton(u"＋ Add …"_s);
    b->setToolTip(u"The menu of everything that can be added, by group (also: right click or double-click on the "
                  "empty canvas)"_s);
    connect(b, &QPushButton::clicked, this, [this] {
        add_anywhere(view->mapToScene(view->viewport()->rect().center()),
                     view->viewport()->rect().center() - QPoint(450, 280));
    });
    bar->addWidget(b);
    add_btn = b;
    b = new QPushButton(u"Tidy"_s);
    b->setToolTip(u"Lay every node out again: the event on the right, what goes into a node to its left"_s);
    connect(b, &QPushButton::clicked, this, [this] {
        layout_nodes();
        fit();
    });
    bar->addWidget(b);
    b = new QPushButton(u"Fit"_s);
    b->setToolTip(u"Show all of it"_s);
    connect(b, &QPushButton::clicked, this, [this] { fit(); });
    bar->addWidget(b);
    bar->addSpacing(10);
    side_btn = new QPushButton(u"Side panel ▸"_s);
    side_btn->setCheckable(true);
    side_btn->setChecked(true);
    side_btn->setToolTip(u"The picked node in words, the templates for new events, the key of the colours"_s);
    connect(side_btn, &QPushButton::toggled, this, [this](bool on) {
        side->setVisible(on);
        side_btn->setText(on ? u"Side panel ▸"_s : u"◂ Side panel"_s);
    });
    bar->addWidget(side_btn);
    lay->addLayout(bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    view = new Canvas(this);
    split->addWidget(view);
    side = _side_panel();
    split->addWidget(side);
    split->setStretchFactor(0, 1);
    split->setSizes({1300, 360});
    lay->addWidget(split, 1);
    info = new QLabel(QString());
    info->setWordWrap(true);
    lay->addWidget(info);
    say(QString());
    auto *hint = new QLabel(u"Drag the canvas: move · Ctrl + drag the canvas: pick the boxes in a frame · Shift + click a "
                            "box: it and every box below it (drag to move them together) · Wheel: zoom · Right click / "
                            "double-click the canvas or “＋ Add …”: "
                            "everything, by group · ＋ at an input: what fits there · Click a value: pick it · Drag from a "
                            "dot: a wire (let go over the canvas: a new node) · Delete: the picked wire / node goes · "
                            "Wires into one input are read top to bottom."_s);
    hint->setWordWrap(true);
    hint->setObjectName(u"faint"_s);
    hint->setContentsMargins(12, 4, 12, 6);
    lay->addWidget(hint);
    line = new QLineEdit(view);
    line->hide();
    connect(line, &QLineEdit::returnPressed, this, [this] { _line_done(); });
    line->installEventFilter(this);
    picker = new Picker(view);
    auto *su = new QShortcut(QKeySequence::Undo, this);
    connect(su, &QShortcut::activated, this, [this] { Q_EMIT undoRequested(); });
    auto *sr = new QShortcut(QKeySequence::Redo, this);
    connect(sr, &QShortcut::activated, this, [this] { Q_EMIT redoRequested(); });
    set_colours(u"contrast"_s, false);
}

// ------------------------------------------------------------------ the side panel
QScrollArea *NodeWindow::_side_panel()
{
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    sc->setMinimumWidth(300);
    auto *inner = new QWidget();
    inner->setObjectName(u"sideInner"_s);
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    // --- the picked node
    auto *s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"The picked node"_s, u"label"_s));
    ins_title = new QLabel(u"Click a node."_s);
    ins_title->setWordWrap(true);
    ins_title->setTextFormat(Qt::RichText);
    ins_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    s->lay->addWidget(ins_title);
    ins_text = new QLabel(QString());
    ins_text->setWordWrap(true);
    ins_text->setTextFormat(Qt::PlainText);
    ins_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ins_text->setStyleSheet(u"QLabel { font-family: %1; color: %2; background: %3; padding: 6px; "
                            "border-radius: 4px; }"_s.arg(theme::MONO, theme::INK, theme::PANEL));
    s->lay->addWidget(ins_text);
    follow_box = new QWidget();
    auto *fl = new QVBoxLayout(follow_box);
    fl->setContentsMargins(0, 4, 0, 0);
    auto *row = new QHBoxLayout();
    follow_btn = new QPushButton(u"＋ Follow-up event"_s);
    follow_btn->setObjectName(u"primary"_s);
    follow_btn->setToolTip(u"A new event, called by this choice (is_triggered_only), in the same file — the next step "
                           "of a chain. Its texts are there to change; its id follows the mod's ID system."_s);
    connect(follow_btn, &QPushButton::clicked, this, [this] { follow_up(); });
    row->addWidget(follow_btn);
    row->addWidget(widgets::label(u"after"_s, u"sub"_s, false));
    follow_days = new widgets::Stepper(0, 3650, 30);
    row->addWidget(follow_days);
    row->addWidget(widgets::label(u"days"_s, u"sub"_s, false));
    row->addStretch(1);
    fl->addLayout(row);
    follow_choices = new widgets::Stepper(1, 6, 1);
    row = new QHBoxLayout();
    row->addWidget(widgets::label(u"its choices"_s, u"sub"_s, false));
    row->addWidget(follow_choices);
    row->addStretch(1);
    fl->addLayout(row);
    s->lay->addWidget(follow_box);
    follow_box->hide();
    open_btn = new QPushButton(u"Open the event it calls ▸"_s);
    connect(open_btn, &QPushButton::clicked, this, [this] { _open_called(); });
    open_btn->hide();
    s->lay->addWidget(open_btn);
    lay->addWidget(s);
    // --- templates
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Templates — new events with their structure"_s, u"label"_s));
    s->lay->addWidget(widgets::label(u"Put into the file of the event shown (at its end), every id from the mod's ID "
                                     "system, the texts already there to change. Then a choice grows the next step "
                                     "with “＋ Follow-up event”."_s,
                                     u"faint"_s));
    auto *grid = new QGridLayout();
    grid->setSpacing(4);
    int i = 0;
    for (const eventtemplates::Template &t : eventtemplates::TEMPLATES) {
        auto *b = new QPushButton(t.name);
        b->setToolTip(t.what);
        b->setStyleSheet(u"QPushButton { text-align: left; padding: 5px 8px; }"_s);
        connect(b, &QPushButton::clicked, this, [this, kk = t.key] { add_template(kk); });
        grid->addWidget(b, i++, 0);
    }
    s->lay->addLayout(grid);
    lay->addWidget(s);
    // --- the key
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Key"_s, u"label"_s));
    key_lbl = new QLabel(QString());
    key_lbl->setWordWrap(true);
    key_lbl->setTextFormat(Qt::RichText);
    s->lay->addWidget(key_lbl);
    lay->addWidget(s);
    lay->addStretch(1);
    sc->setWidget(inner);
    return sc;
}

void NodeWindow::_key()
{
    const QString dot = u"<span style=\"color:%1; font-size:15px\">●</span>&nbsp;%2"_s;
    const std::vector<std::pair<QString, QString>> where = {{u"condition"_s, u"cond"_s}, {u"effect"_s, u"eff"_s},
                                                            {u"choice"_s, u"opt"_s}, {u"how often"_s, u"weight"_s},
                                                            {u"what changes it"_s, u"mod"_s}};
    QStringList a, b;
    for (const auto &[n, c] : where)
        a << dot.arg(COLOURS.value(c), n);
    for (const What &w : WHAT)
        b << dot.arg(WHAT_COL.value(w.key), w.name);
    key_lbl->setText(u"<b>Left edge, dot and wire — where it is</b><br>"_s + a.join(u"<br>"_s) +
                     u"<br><br><b>Head — what it is</b><br>"_s + b.join(u"<br>"_s));
}

void NodeWindow::set_colours(const QString &name, bool save)
{
    set_scheme(name);
    for (auto it = scheme_btn.begin(); it != scheme_btn.end(); ++it)
        it.value()->setChecked(it.key() == name);
    _key();
    if (save)
        settings_::settings()->setValue(u"nodes/scheme"_s, name);
    view->scene()->update();
    view->viewport()->update();
}

void NodeWindow::_inspect(NodeItem *n)
{
    // The side panel's part about the picked node.
    follow_box->setVisible(n && n->kind == u"option");
    OptStr called;
    if (n && n->kind == u"line" && n->what == OptStr(u"event"_s)) {
        const PItem &v = *n->item;
        if (v.list) {
            for (const PPtr &c : v.items)
                if (c->k == OptStr(u"id"_s)) {
                    called = c->list ? u"[…]"_s : c->text;    // (str() of a block: never an id)
                    break;
                }
        } else {
            called = v.text;
        }
    }
    _called = called;
    open_btn->setVisible(called && !called->isEmpty());
    if (!n) {
        ins_title->setText(u"Click a node."_s);
        ins_text->setText(QString());
        return;
    }
    const QString col = n->kind == u"line" && n->what ? WHAT_COL.value(*n->what) : COLOURS.value(n->type_(), u"#888888"_s);
    static const QHash<QString, QString> WHERE = {{u"cond"_s, u"a condition"_s}, {u"eff"_s, u"an effect"_s},
                                                  {u"opt"_s, u"a choice"_s},     {u"weight"_s, u"how often"_s},
                                                  {u"mod"_s, u"what changes it"_s}, {u"list"_s, u"a possibility"_s},
                                                  {u"goal"_s, u"a war goal"_s}};
    const QString t = n->type_();
    const QString where = t == u"entry" ? u"the %1 itself"_s.arg(entry_kind) : WHERE.value(t, t);
    ins_title->setText(u"<span style='color:%1; font-weight:700; font-size:15px'>%2</span><br>"
                       "<span style='color:%3'>%4</span>"_s.arg(col, n->title, theme::INK_DIM, where));
    QString text;
    try {
        text = bc::dump({bc::from_ref(*n->item)}, 0);
    } catch (...) {
        text.clear();
    }
    const QStringList lines = py::splitlines(text);
    ins_text->setText(lines.mid(0, 40).join(u'\n') +
                      (lines.size() > 40 ? u"\n… %1 more lines"_s.arg(lines.size() - 40) : QString()));
}

void NodeWindow::_open_called()
{
    if (_called && !_called->isEmpty())
        Q_EMIT entryWanted(*_called);
}

// ------------------------------------------------------------------ templates, follow-ups
QStringList NodeWindow::_new_ids(int n)
{
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    eventtools::Ids used = eventtools::event_ids(mod, store);
    QStringList out;
    for (int k = 0; k < n; ++k) {
        OptStr got = rel ? reid::suggest(mod, store, *rel) : std::nullopt;
        QSet<QString> taken(out.begin(), out.end());
        if (!got || used.contains(*got) || out.contains(*got)) {
            QStringList keys;
            for (const auto &kv : used)
                keys << kv.first;
            const QStringList near = rel ? eventtools::ids_of_file(mod, store, *rel) : QStringList();
            got = eventtools::free_id(keys, near + out, taken);
        }
        if (used.contains(*got) || out.contains(*got)) {
            QStringList keys;
            for (const auto &kv : used)
                keys << kv.first;
            got = eventtools::free_id(keys + out, {}, taken);
        }
        out << *got;
        used.insert(*got, {eventtools::Place(rel.value_or(QString()), 0, 0)});
    }
    return out;
}

void NodeWindow::follow_up()
{
    // The picked choice calls a new event (triggered only), put right after this event in its file.
    NodeItem *n = nullptr;
    for (QGraphicsItem *it : view->scene()->selectedItems())
        if (auto *x = dynamic_cast<NodeItem *>(it); x && x->kind == u"option") {
            n = x;
            break;
        }
    if (!n || !doc || !span)
        return;
    const QString kind = entry_scope == u"p" ? u"province_event"_s : u"country_event"_s;
    const QString eid = _new_ids(1)[0];
    PList &lst = n->item->items;
    qsizetype at = qsizetype(lst.size());
    for (size_t k = 0; k < lst.size(); ++k)
        if (lst[k]->k == OptStr(u"ai_chance"_s) && lst[k]->list) {
            at = qsizetype(k);
            break;
        }
    lst.insert(lst.begin() + at, bc::pitem(kind, PList{bc::pitem(u"id"_s, eid),
                                                       bc::pitem(u"days"_s, QString::number(follow_days->value()))}));
    const auto [block, names] = eventtemplates::follow_up(eid, kind, int(follow_choices->value()));
    const QString title = n->title;
    changed(u"follow-up event %1 called by %2"_s.arg(eid, title), block, names);
    say(u"Follow-up event %1 made right after this one, called by the choice after %2 days — “Open the event it calls ▸” "
        "on the new line opens it."_s.arg(eid).arg(follow_days->value()));
}

void NodeWindow::add_template(const QString &key)
{
    if (!doc || !rel) {
        say(u"Show an event first (pick one on the Scripts page): the template goes into its file."_s, true);
        return;
    }
    int need = 0;
    for (const eventtemplates::Template &t : eventtemplates::TEMPLATES)
        if (t.key == key) {
            need = t.ids;
            break;
        }
    const QStringList ids = _new_ids(need);
    const eventtemplates::Made made = eventtemplates::make(key, ids);
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString old = store.text(*rel);
    const QString nl = old.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QString stripped = old;
    while (!stripped.isEmpty() && (stripped.back() == u'\r' || stripped.back() == u'\n'))
        stripped.chop(1);
    const QString new_ = stripped + nl + nl + QString(made.text).replace(u"\n"_s, nl) + nl;
    const QString nm = eventtemplates::NAME.value(key);
    const QString s = ids.size() > 1 ? u"s"_s : QString();
    Q_EMIT scriptNamesEdit(u"Template “%1”: event%2 %3"_s.arg(nm, s, ids.join(u", "_s)), *rel, new_, made.names);
    say(u"“%1” put at the end of %2: event%3 %4. Opening the first one…"_s.arg(nm, *rel, s, ids.join(u", "_s)));
    QTimer::singleShot(0, this, [this, first = made.first] { Q_EMIT entryWanted(first); });
}

bool NodeWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == line && e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
        _edit_of.reset();
        line->hide();
        return true;
    }
    if (obj == line && e->type() == QEvent::FocusOut && line->isVisible())
        QTimer::singleShot(0, this, [this] { _line_done(); });
    return QWidget::eventFilter(obj, e);
}

void NodeWindow::say(const QString &text, bool bad)
{
    info->setText(text);
    info->setStyleSheet(u"QLabel { background: %1; color: %2; font-weight: 600; padding: 6px 12px; }"_s.arg(
        theme::PANEL_ALT, bad ? RED : theme::INK));
}

void NodeWindow::tell()
{
    // The picked node in plain words.
    std::vector<NodeItem *> sel;
    for (QGraphicsItem *it : view->scene()->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(it))
            sel.push_back(n);
    if (sel.size() != 1) {
        _inspect(nullptr);
        return;
    }
    NodeItem *n = sel[0];
    _inspect(n);
    QString t;
    if (n->kind == u"line") {
        if (!n->spec) {
            t = u"%1 — a line the catalogue doesn't know: kept as it is written."_s.arg(kstr(n->item->k));
        } else {
            try {
                t = u"%1 — %2"_s.arg(kstr(n->item->k), bc::sentence(bc::from_ref(*n->item), n->spec, *env));
            } catch (...) {
                t = u"%1 — %2"_s.arg(kstr(n->item->k), n->spec->text);
            }
            if (n->wrong) {
                QStringList about;
                for (const QChar c : n->spec->scopes)
                    if (bc::SCOPE_NAME.contains(QString(c)))
                        about << bc::SCOPE_NAME.value(QString(c));
                t += u"   ⚠ This is about %1, but here %2 is meant."_s.arg(
                    about.join(u" or "_s),
                    n->scope && bc::SCOPE_NAME.contains(*n->scope) ? bc::SCOPE_NAME.value(*n->scope)
                                                                   : u"something else"_s);
            }
        }
    } else {
        static const QHash<QString, QString> T = {
            {u"option"_s, u"A choice of the event: its text's key, what it does, how likely the computer takes it."_s},
            {u"weight"_s, u"How often / how likely: a base number, and what changes it."_s},
            {u"mod"_s, u"Changes the number (× factor) while its conditions hold."_s},
            {u"chance"_s, u"One possibility of a random list: its weight and what happens."_s}};
        t = n->kind == u"entry" ? u"The %1 itself: its own lines, and what goes into it."_s.arg(entry_kind)
                                : T.value(n->kind);
    }
    if (!n->parent_ && n->kind != u"entry")
        t += u"   (loose: not in the script until its dot is wired into an input)"_s;
    say(t);
}

// ------------------------------------------------------------------ the entry
void NodeWindow::refresh(document::Document *doc_, const events::Entry *entry, refs::RefIndex *refs,
                         events::EventDB *db)
{
    if (doc_ != doc || !env) {
        doc = doc_;
        env = std::make_unique<bc::Env>(mod, doc_, refs, db);
    } else if (refs) {
        env->refs = refs;
        env->db = db;
        for (const QString &k : {u"cflag"_s, u"gflag"_s, u"pflag"_s, u"var"_s, u"event"_s})
            env->forget(k);
    }
    if (!entry)
        return;
    if (!doc_->maps.contains(u"scripts"_s))
        return;
    auto &store = doc_->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString erel = entry->rel.value_or(QString());
    const QString text = store.text(erel);
    const auto [a, b] = entry->span;
    const std::pair<QString, QString> ident(entry->kind, entry->is_event() ? entry->id.value_or(u"None"_s) : entry->key);
    const bool same = entry_id && ident == *entry_id && rel == OptStr(erel);
    const QString piece = py::slice(text, a, b).replace(u"\r\n"_s, u"\n"_s);
    if (same && OptStr(piece) == _written) {
        span = std::make_pair(a, b);             // (the nodes wrote this themselves: the graph stays as it is)
        return;
    }
    const QHash<QString, std::pair<OptStr, QPointF>> old = same ? _paths() : QHash<QString, std::pair<OptStr, QPointF>>();
    entry_id = ident;
    rel = erel;
    span = std::make_pair(a, b);
    entry_kind = entry->kind == u"decision" ? u"decision"_s : u"event"_s;
    entry_scope = entry->kind == u"province_event" ? u"p"_s : u"c"_s;
    depth = entry_kind == u"decision" ? 1 : 0;
    PPtr found;
    for (const bc::Item &it : bc::read_items(py::slice(text, a, b)))
        if (it.list) {
            found = bc::to_ref(it);
            break;
        }
    if (!found) {
        head->setText(u"This script could not be read: "_s);   // (Python: StopIteration, whose text is empty)
        return;
    }
    root = found;
    _written = piece;
    _dump = script();
    head->setText(u"%1 %2  —  %3"_s.arg(py::capitalize(entry_kind), ident.second, erel));
    setWindowTitle(u"Nodes — %1"_s.arg(ident.second));
    if (!same)
        loose.clear();
    pos.clear();
    opened.clear();
    closed.clear();
    fold_depth = _fold_depth();
    rebuild();
    bool keep = !old.isEmpty();
    std::vector<std::pair<NodeItem *, QPointF>> got;
    for (NodeItem *n : nodes) {
        const auto *g = old.contains(_path(n)) ? &*old.find(_path(n)) : nullptr;
        if (!g || g->first != n->item->k) {
            keep = false;
            break;
        }
        got.emplace_back(n, g->second);
    }
    if (keep) {
        for (const auto &[n, at] : got)          // (after an undo of a value: every node stays where it was)
            n->setPos(at);
        for (NodeItem *n : nodes)
            route(n);
    } else {
        QTimer::singleShot(0, this, [this] { fit(); });
    }
    QSet<QString> known;
    for (const Sock &s : root_node->socks)
        known.insert(s.name);
    hidden_parts.clear();
    for (const PPtr &c : root->items)
        if (c->list && !(c->k && known.contains(*c->k)))
            hidden_parts << kstr(c->k);
    say(!hidden_parts.isEmpty() ? u"Parts of this %1 the nodes don't show (they stay as they are): %2"_s.arg(
                                      entry_kind, hidden_parts.join(u", "_s))
                                : QString());
    if (fold_depth < 1000000000)
        say(u"A large %1 (%2 lines): it is opened folded — %3 nodes are on the canvas; a node with “▸ … lines inside” "
            "opens with a click, the ▾ in a node's head folds it again."_s.arg(entry_kind)
                .arg(count_inside(*root))
                .arg(nodes.size()));
}

long long NodeWindow::_fold_depth()
{
    // From which depth on nodes start folded: the deepest that keeps the nodes made within the budget (a small
    // event: nothing is folded).
    std::vector<long long> counts;
    std::vector<const PItem *> level = {root.get()};
    int d = 0;
    while (!level.empty() && d < 60) {
        counts.push_back(qsizetype(level.size()));
        std::vector<const PItem *> next;
        for (const PItem *it : level)
            if (it->list)
                for (const PPtr &c : it->items)
                    if (c->k != OptStr(bc::NOTE))
                        next.push_back(c.get());
        level.swap(next);
        d += 1;
    }
    long long sum = 0;
    for (long long c : counts)
        sum += c;
    if (double(sum) <= BUDGET * 1.6)
        return 1000000000;
    long long total = 0, depth_ = 1;
    for (size_t k = 0; k < counts.size(); ++k) {
        total += counts[k];
        if (total > BUDGET && k >= 2)
            break;
        depth_ = (long long)k;
    }
    return std::max(1LL, depth_ - 1);
}

void NodeWindow::set_folded(NodeItem *node, bool folded)
{
    // A node folded / opened (one level); the picture is laid out again around it.
    const PItem *key = node->item.get();
    (folded ? closed : opened).insert(key);
    (folded ? opened : closed).remove(key);
    const QPoint at = view->mapFromScene(node->scenePos());
    const PPtr item = node->item;
    pos.clear();
    rebuild();
    if (NodeItem *n = node_of(item.get())) {     // (the node stays where it was on the screen)
        const QPointF d = view->mapToScene(at) - n->scenePos();
        view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() -
                                              int(d.x() * view->transform().m11()));
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() - int(d.y() * view->transform().m22()));
        view->scene()->clearSelection();
        n->setSelected(true);
    }
    say(u"%1 %2 — %3 nodes on the canvas now."_s.arg(kstr(item->k), folded ? u"folded"_s : u"opened"_s)
            .arg(nodes.size()));
}

QString NodeWindow::_path(NodeItem *node)
{
    QStringList out;
    while (node) {
        if (node->home) {
            const auto i = _index(*node->home, node->item.get());
            out << u"%1,%2"_s.arg(node->psock).arg(i ? QString::number(*i) : u"None"_s);
        } else {
            out << (node->kind == u"entry" ? u"entry"_s : u"@%1"_s.arg(quintptr(node->item.get())));
        }
        node = node->parent_;
    }
    return out.join(u'|');
}

QHash<QString, std::pair<OptStr, QPointF>> NodeWindow::_paths()
{
    QHash<QString, std::pair<OptStr, QPointF>> out;
    for (NodeItem *n : nodes)
        out.insert(_path(n), {n->item->k, n->pos()});
    return out;
}

void NodeWindow::rebuild()
{
    // The scene, made again from the script's tree and the loose nodes; nodes stay where they were.
    QGraphicsScene *sc = view->scene();
    QSet<const PItem *> picked;
    for (QGraphicsItem *it : sc->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(it))
            picked.insert(n->item.get());
    line->hide();
    picker->hide();
    if (_drag_path) {                            // (sc.clear() takes it too)
        _drag_path = nullptr;
        dragging.reset();
    }
    sc->clear();
    nodes.clear();
    root_node = nullptr;
    _by_item.clear();
    _kept.clear();

    auto kid_kind = [](const Sock &sock) -> std::pair<QString, QString> {
        if (sock.type == u"opt")
            return {u"option"_s, u"eff"_s};
        if (sock.type == u"weight")
            return {u"weight"_s, u"cond"_s};
        if (sock.type == u"mod")
            return {u"mod"_s, u"cond"_s};
        if (sock.type == u"list")
            return {u"chance"_s, u"eff"_s};
        return {u"line"_s, sock.type};
    };
    std::function<NodeItem *(const PPtr &, const QString &, const QString &, const OptStr &, NodeItem *, int, int)> make;
    make = [&](const PPtr &it, const QString &kind, const QString &mode, const OptStr &scope, NodeItem *parent, int si,
               int depth_) -> NodeItem * {
        auto *n = new NodeItem(this, it, kind, mode, scope);
        n->depth = depth_;
        nodes.push_back(n);
        _by_item.insert(it.get(), n);
        if (parent) {
            n->parent_ = parent;
            n->psock = si;
            n->home = parent->socks[size_t(si)].lst();
            n->wire = new Wire(n);
            parent->children.push_back(n);
        }
        std::vector<std::vector<PPtr>> members;
        for (const Sock &s : n->socks)
            members.push_back(s.members());
        bool any = false;
        for (size_t k = 0; k < n->socks.size(); ++k) {
            n->socks[k].count = qsizetype(members[k].size());
            any = any || !members[k].empty();
        }
        if (any && kind != u"entry" &&
            (closed.contains(it.get()) || (depth_ >= fold_depth && !opened.contains(it.get())))) {
            n->folded = true;                    // (what is inside is not made)
            n->inside = count_inside(*it);
            n->measure();
            return n;
        }
        for (size_t i = 0; i < n->socks.size(); ++i) {
            const auto [k2, m2] = kid_kind(n->socks[i]);
            const OptStr s_scope = n->socks[i].scope;
            for (const PPtr &c : members[i])
                make(c, k2, m2, s_scope, n, int(i), depth_ + 1);
        }
        return n;
    };
    root_node = make(root, u"entry"_s, u"eff"_s, entry_scope, nullptr, -1, 0);
    const std::vector<Loose> ls = loose;
    for (const Loose &l : ls)
        make(l.item, l.kind, l.mode, l.scope, nullptr, -1, 0);
    if (!pos.contains(root.get())) {
        _layout();
    } else {
        for (NodeItem *n : nodes) {              // (parents come before what goes into them)
            std::optional<QPointF> at;
            if (pos.contains(n->item.get()))
                at = pos.value(n->item.get());
            if (!at && n->parent_) {             // a new node: to the left of what it goes into
                std::optional<double> below;
                for (NodeItem *k : n->parent_->children)
                    if (k->psock == n->psock && k != n && pos.contains(k->item.get())) {
                        const double y = pos.value(k->item.get()).y() + k->h;
                        below = below ? std::max(*below, y) : y;
                    }
                const QPointF pp = pos.value(n->parent_->item.get());
                at = QPointF(pp.x() + n->parent_->w + 80,
                             below ? *below + 14 : pp.y() + n->parent_->sock_y(n->psock) - HEAD / 2);
                std::vector<std::pair<QPointF, NodeItem *>> placed;
                for (NodeItem *k : nodes)
                    if (k != n && pos.contains(k->item.get()))
                        placed.emplace_back(pos.value(k->item.get()), k);
                bool moved = true;
                while (moved) {                  // (down, until it lies on no other node)
                    moved = false;
                    const QRectF me(at->x() - 20, at->y() - 10, n->w + 40, n->h + 20);
                    for (const auto &[q, k] : placed)
                        if (me.intersects(QRectF(q.x(), q.y(), k->w, k->h))) {
                            at = QPointF(at->x(), q.y() + k->h + 16);
                            moved = true;
                            break;
                        }
                }
            }
            pos.insert(n->item.get(), at ? *at : QPointF(0, 0));
        }
    }
    for (NodeItem *n : nodes) {
        n->setPos(pos.value(n->item.get()));
        sc->addItem(n);
        if (n->wire)
            sc->addItem(n->wire);
    }
    for (NodeItem *n : nodes) {
        route(n);
        if (picked.contains(n->item.get()))
            n->setSelected(true);
    }
    sc->setSceneRect(sc->itemsBoundingRect().adjusted(-3000, -3000, 3000, 3000));
}

void NodeWindow::fit()
{
    const QRectF r = view->scene()->itemsBoundingRect().adjusted(-60, -60, 60, 60);
    view->scene()->setSceneRect(r.adjusted(-3000, -3000, 3000, 3000));
    view->resetTransform();
    view->fitInView(r, Qt::KeepAspectRatio);
    if (view->transform().m11() > 1.15) {
        const double f = 1.15 / view->transform().m11();
        view->scale(f, f);
    }
}

void NodeWindow::_layout()
{
    // Places for every node (pos): the entry on the LEFT, what goes into a node to its right, top to bottom in the
    // script's order — read left to right like the script (the user, 2026-10-26: "highest on the left, lowest on the
    // right").
    const double gap_x = 80.0, gap_y = 14.0;
    QHash<NodeItem *, double> heights;
    std::function<double(NodeItem *)> height = [&](NodeItem *n) {
        const auto &ks = kids(n);
        std::vector<double> hs;
        for (NodeItem *k : ks)
            hs.push_back(height(k));
        const double v = std::max(n->h, py::sum(hs) + gap_y * double(std::max<qsizetype>(0, qsizetype(ks.size()) - 1)));
        heights.insert(n, v);
        return v;
    };
    std::function<void(NodeItem *, double, double)> place = [&](NodeItem *n, double left, double top) {
        pos.insert(n->item.get(), QPointF(left, top + (heights.value(n) - n->h) / 2.0));
        double y = top;
        for (NodeItem *k : kids(n)) {
            place(k, left + n->w + gap_x, y);
            y += heights.value(k) + gap_y;
        }
    };
    if (!root_node)
        return;
    height(root_node);
    place(root_node, 0.0, 0.0);
    double y = heights.value(root_node) + 70;
    for (NodeItem *n : nodes)
        if (!n->parent_ && n != root_node) {
            height(n);
            place(n, 0.0, y);
            y += heights.value(n) + 40;
        }
}

void NodeWindow::layout_nodes()
{
    _layout();
    for (NodeItem *n : nodes)
        n->setPos(pos.value(n->item.get()));
    for (NodeItem *n : nodes)
        route(n);
}

void NodeWindow::route(NodeItem *node)
{
    std::vector<NodeItem *> all = {node};
    all.insert(all.end(), node->children.begin(), node->children.end());
    for (NodeItem *n : all)
        if (n->wire && n->parent_ && n->scene() && n->parent_->scene())
            n->wire->setPath(_curve(n->out_dot(), n->parent_->in_dot(n->psock)));
}

QPainterPath NodeWindow::_curve(const QPointF &a, const QPointF &b)
{
    QPainterPath path(a);                        // a: a node's own dot (its left), b: an input (a node's right)
    const double dx = std::max(40.0, std::abs(b.x() - a.x()) / 2.0);
    path.cubicTo(a - QPointF(dx, 0), b + QPointF(dx, 0), b);
    return path;
}

std::optional<Hit> NodeWindow::hit(const QPointF &sp)
{
    for (QGraphicsItem *it : view->scene()->items(sp))
        if (auto *n = dynamic_cast<NodeItem *>(it))
            if (auto h = n->hit(n->mapFromScene(sp)))
                return h;
    return std::nullopt;
}

void NodeWindow::close_editors()
{
    if (line->isVisible())
        _line_done();
    picker->hide();
}

NodeItem *NodeWindow::node_of(const PItem *item)
{
    NodeItem *n = _by_item.value(item, nullptr);
    return n && n->item.get() == item ? n : nullptr;
}

NodeItem *NodeWindow::find(long long uid) const
{
    for (NodeItem *n : nodes)
        if (n->uid == uid)
            return n;
    return nullptr;
}

// ------------------------------------------------------------------ wires
void NodeWindow::start_wire(const Hit &h)
{
    dragging = h;
    _drag_path = new QGraphicsPathItem();
    _drag_path->setPen(QPen(QColor(theme::GOLD), 3.5, Qt::DashLine));
    _drag_path->setZValue(5);
    view->scene()->addItem(_drag_path);
}

void NodeWindow::drag_wire(const QPointF &sp)
{
    const Hit &d = *dragging;
    _drag_path->setPath(d.what == u"out" ? _curve(d.node->out_dot(), sp) : _curve(sp, d.node->in_dot(*d.i)));
}

void NodeWindow::end_wire(const QPointF &sp, const QPoint &vp)
{
    const Hit d = *dragging;
    dragging.reset();
    view->scene()->removeItem(_drag_path);
    delete _drag_path;
    _drag_path = nullptr;
    NodeItem *node = d.node;
    const std::optional<Hit> h = hit(sp);
    if (d.what == u"out") {
        if (h && h->node != node) {
            NodeItem *t = h->node;
            std::optional<int> si;
            if (h->what == u"in" || h->what == u"add" ||
                (h->what == u"node" && h->i && fits(node, t->socks[size_t(*h->i)])))
                si = h->i;
            else
                for (int k = 0; k < int(t->socks.size()); ++k)
                    if (fits(node, t->socks[size_t(k)])) {
                        si = k;
                        break;
                    }
            if (!si) {
                say(u"“%1” has no input for %2."_s.arg(t->title, TYPE_NAME.value(node->type_(), node->type_())), true);
                return;
            }
            wire(node, t, *si);
        } else if (!h) {
            add_around(node, sp, vp);
        }
    } else {
        if (h && h->node != node && h->node->kind != u"entry")
            wire(h->node, node, *d.i);
        else if (!h)
            add_for(node, *d.i, sp, vp);
    }
}

bool NodeWindow::fits(NodeItem *node, const Sock &sock)
{
    // May the node go into this input?
    if (node->type_() == sock.type)
        return true;
    if (node->kind == u"line" && (sock.type == u"cond" || sock.type == u"eff" || sock.type == u"goal")) {
        if (!node->spec)
            return true;
        const bc::Item val = bc::shallow(*node->item);
        return bc::spec_for(node->item->k, sock.type, sock.scope, env.get(), &val) != nullptr;
    }
    return false;
}

bool NodeWindow::_holds(const PItem &item, const PList *lst)
{
    if (!item.list)
        return false;
    if (&item.items == lst)
        return true;
    return std::any_of(item.items.begin(), item.items.end(), [&](const PPtr &c) { return _holds(*c, lst); });
}

void NodeWindow::unhook(NodeItem *node)
{
    // A node taken out of where it stands (the script's tree or the loose ones).
    const PItem *me = node->item.get();
    if (node->home) {
        PList *home = node->home;
        std::erase_if(*home, [&](const PPtr &x) { return x.get() == me; });
        NodeItem *p = node->parent_;
        if (p && p->socks[size_t(node->psock)].sub == OptStr(u"limit"_s) && home->empty()) {
            PList *h = p->socks[size_t(node->psock)].holder;
            std::erase_if(*h, [&](const PPtr &x) {
                if (x->list && &x->items == home) {
                    _kept.push_back(x);              // (node->home is inside it: kept as long as the node is)
                    return true;
                }
                return false;
            });
        }
    }
    std::erase_if(loose, [&](const Loose &l) { return l.item.get() == me; });
}

bool NodeWindow::wire(NodeItem *node, NodeItem *target, int si)
{
    const Sock sock = target->socks[size_t(si)];
    if (!fits(node, sock)) {
        say(u"%1 can't go into “%2” of %3: that input takes %4."_s.arg(node->title, sock.name, target->title,
                                                                        TYPE_NAME.value(sock.type, sock.type)),
            true);
        return false;
    }
    if (node == target || _holds(*node->item, &target->item->items)) {
        say(u"A node can't go into itself or into what hangs on it."_s, true);
        return false;
    }
    if (node->parent_ == target && node->psock == si)
        return false;
    if (sock.single) {
        for (const PPtr &m : sock.members())
            if (m.get() != node->item.get()) {
                say(u"“%1” takes one node only — take the old one out first (click its wire, Delete)."_s.arg(sock.name),
                    true);
                return false;
            }
    }
    const PPtr item = node->item;                // (kept: unhook may drop the last other hold on it)
    unhook(node);
    PList *lst = sock.lst(true);
    std::vector<std::pair<PPtr, NodeItem *>> mem;
    for (const PPtr &m : sock.members())
        mem.emplace_back(m, node_of(m.get()));
    const double y = node->pos().y();
    const PItem *before = nullptr;
    for (const auto &[m, n] : mem)
        if (n && n->pos().y() > y) {
            before = m.get();
            break;
        }
    qsizetype at;
    if (before)
        at = _index(*lst, before).value_or(qsizetype(lst->size()));
    else if (!mem.empty())
        at = _index(*lst, mem.back().first.get()).value_or(-1) + 1;
    else if (target->kind == u"entry" && !sock.sub)
        at = sock.type != u"opt" ? first_option(*lst) : qsizetype(lst->size());
    else
        at = qsizetype(lst->size());
    if (sock.type == u"weight")                  // (how often: the input says which line it is)
        item->k = sock.name;
    lst->insert(lst->begin() + std::clamp<qsizetype>(at, 0, qsizetype(lst->size())), item);
    changed(u"%1 wired into %2 (%3)"_s.arg(node->title, target->title, sock.name));
    return true;
}

// ------------------------------------------------------------------ values
void NodeWindow::act(const Hit &h)
{
    NodeItem *node = h.node;
    if (std::find(nodes.begin(), nodes.end(), node) == nodes.end())
        return;
    if (h.what == u"fold" || h.what == u"unfold") {
        set_folded(node, h.what == u"fold");
    } else if (h.what == u"pill") {
        edit_value(node, *h.i);
    } else if (h.what == u"add") {
        add_for(node, *h.i, std::nullopt, view->mapFromScene(node->mapToScene(node->add_rect(*h.i).bottomLeft())));
    } else if (h.what == u"more") {
        const QPoint vp = view->mapFromScene(node->mapToScene(node->more_rect().bottomLeft()));
        std::vector<Picker::Row> rows;
        for (const QString &f : node->missing)
            rows.push_back({f, PickData{f, QString(), nullptr}, std::nullopt, std::nullopt});
        const long long uid = node->uid;
        picker->open(vp, u"A line more in “%1”"_s.arg(node->title), rows, [this, uid](const PickData &f, bool) {
            if (NodeItem *n = find(uid))
                add_field(n, f.s);
        });
    }
}

void NodeWindow::add_field(NodeItem *node, const QString &f)
{
    OrderedMap<QString, QString> types;
    if (node->spec && node->spec->fields)
        for (const auto &x : *node->spec->fields)
            types.insert(x.first, x.second);
    QString ty = types.value(f);
    if (ty.isEmpty())
        ty = node->kind == u"entry" ? ENTRY_TYPES.value(f, u"text"_s) : u"num"_s;
    static const QHash<QString, QString> START = {{u"duration"_s, u"365"_s}, {u"months"_s, u"12"_s},
                                                  {u"years"_s, u"1"_s},      {u"days"_s, u"30"_s},
                                                  {u"factor"_s, u"1"_s},     {u"chance"_s, u"50"_s},
                                                  {u"picture"_s, u"none"_s}};
    QString val = START.value(f);
    if (!START.contains(f)) {
        val = bc::_default(ty, env.get());
        if (val.isEmpty())
            val = u"x"_s;
    }
    PList &items = node->item->items;
    qsizetype at = qsizetype(items.size());
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]->list) {
            at = qsizetype(i);
            break;
        }
    items.insert(items.begin() + at, bc::pitem(f, val, ty == u"text"));
    changed(u"%1: %2 added"_s.arg(node->title, f));
}

void NodeWindow::edit_value(NodeItem *node, int j)
{
    const Widget wd = node->widgets[size_t(j)];
    const QString ty = wd.vtype, cur = attr_str(wd);
    if (ty == u"bool" && (py::lower(cur) == u"yes" || py::lower(cur) == u"no")) {
        set_value(node, j, py::lower(cur) == u"yes" ? u"no"_s : u"yes"_s);
        return;
    }
    const QRectF r = node->mapRectToScene(node->pill_rect(j));
    const QPoint vp = view->mapFromScene(r.bottomLeft());
    std::optional<bc::Choices> ch;
    if (ty != u"num" && ty != u"int" && ty != u"text" && ty != u"prov")
        ch = env->choices(ty, wd.issue);
    if (ch && !ch->empty()) {
        auto same = [](const QString &k, const QString &n) {
            return py::lower(n) == py::lower(QString(k).replace(u'_', u' ')) || n == k;
        };
        std::vector<Picker::Row> rows;
        for (const auto &[k, n] : *ch)
            rows.push_back({same(k, n) ? k : u"%1  —  %2"_s.arg(k, n), PickData{k, QString(), nullptr}, std::nullopt,
                            std::nullopt});
        const OptStr free = env->free_text(ty) || ty == u"tag" || ty == u"tagc" ? OptStr(u"as it is typed"_s)
                                                                                 : std::nullopt;
        const long long uid = node->uid;
        picker->open(vp,
                     u"%1 of “%2”: %3"_s.arg(!wd.label.isEmpty() ? wd.label : u"The value"_s, node->title,
                                             bc::SLOT_NAME.value(ty, ty)),
                     rows,
                     [this, uid, j](const PickData &v, bool) {
                         if (NodeItem *n = find(uid))
                             set_value(n, j, v.s);
                     },
                     free);
        return;
    }
    const QPoint top = view->mapFromScene(r.topLeft());
    _edit_of = std::make_pair(node->uid, j);
    line->setGeometry(top.x(), top.y(), std::max(200, int(r.width() * view->transform().m11())), 28);
    line->setText(cur);
    line->setPlaceholderText(bc::SLOT_NAME.value(ty, ty));
    line->show();
    line->raise();
    line->setFocus();
    line->selectAll();
}

void NodeWindow::_line_done()
{
    if (!_edit_of) {
        line->hide();
        return;
    }
    const auto [uid, j] = *_edit_of;
    _edit_of.reset();
    const QString text = line->text().trimmed();
    line->hide();
    if (NodeItem *node = find(uid))
        set_value(node, j, text);
}

void NodeWindow::set_value(NodeItem *node, int j, const QString &val)
{
    const Widget wd = node->widgets[size_t(j)];
    PItem &it = *wd.item;
    const QString &attr = wd.attr, &ty = wd.vtype;
    if (val == attr_str(wd))
        return;
    if (val.isEmpty()) {
        const QSet<QString> opt = node->spec ? node->spec->optional : QSet<QString>();
        static const QStringList KEEP = {u"id"_s, u"title"_s, u"desc"_s, u"name"_s};
        if (attr == u"v" && &it != node->item.get() &&
            (opt.contains(wd.label) || node->kind == u"entry" || node->kind == u"weight") && !KEEP.contains(wd.label)) {
            std::erase_if(node->item->items, [&](const PPtr &x) { return x.get() == &it; });
            changed(u"%1: %2 taken out"_s.arg(node->title, wd.label));
        } else {
            say(u"An empty value isn't written — the line stays as it was."_s, true);
        }
        return;
    }
    if ((ty == u"num" || ty == u"int" || ty == u"prov") && !env->legal(ty, val)) {
        say(u"“%1” is not %2 — nothing written."_s.arg(val, bc::VALUE_HELP.value(ty, bc::SLOT_NAME.value(ty, ty))), true);
        return;
    }
    if (attr == u"k")
        it.k = val;
    else
        it.text = val;
    if (attr == u"v" && ty == u"text")
        it.q = it.q || !py::isalnum(QString(val).remove(u'_'));
    changed(u"%1%2 = %3"_s.arg(node->title, !wd.label.isEmpty() ? u" "_s + wd.label : QString(), val));
}

// ------------------------------------------------------------------ adding and removing nodes
std::vector<Picker::Row> NodeWindow::_spec_rows(const QString &mode, const OptStr &scope, bool blocks_only,
                                                const QString &prefix)
{
    // The menu's rows for conditions / effects: every line of the catalogue in its group. What is about something
    // else than this place (a province where a nation is meant) is not left out: it comes last in its group, dimmer,
    // and says so.
    std::vector<Picker::Row> fit, other;
    for (const bc::Spec &s : bc::OF_MODE(mode)) {
        if (s.hidden || (blocks_only && s.vtype != OptStr(u"block"_s)))
            continue;
        const QString key = s.keytype ? bc::KEY_SCRIPT.value(*s.keytype, s.key) : s.key;
        const QString what = s.keytype ? u"a %1 (here: %2)"_s.arg(bc::SLOT_NAME.value(bc::KEY_TYPE.value(*s.keytype),
                                                                                        *s.keytype),
                                                                   key)
                                       : QString();
        const QString text =
            u"%1  —  %2"_s.arg(key, QString(s.text).replace(u"{}"_s, u"…"_s).replace(u"{key}"_s, !what.isEmpty() ? what : u"…"_s));
        const QString group = prefix + (!s.cat.isEmpty() ? s.cat : u"Other"_s);
        if (!scope || s.fits(scope)) {
            fit.push_back({text, PickData{QString(), mode, &s}, WHAT_COL.value(what_of(&s)), group});
        } else {
            QStringList about;
            for (const QChar c : s.scopes)
                if (bc::SCOPE_NAME.contains(QString(c)))
                    about << bc::SCOPE_NAME.value(QString(c));
            other.push_back({u"%1   (about %2 — not what is meant here)"_s.arg(text, about.join(u" or "_s)),
                             PickData{QString(), mode, &s}, theme::INK_FAINT, group});
        }
    }
    std::vector<Picker::Row> all = fit;
    all.insert(all.end(), other.begin(), other.end());
    QStringList order;
    for (const Picker::Row &r : all)
        if (!order.contains(*r.group))
            order << *r.group;
    std::stable_sort(all.begin(), all.end(), [&](const Picker::Row &a, const Picker::Row &b) {
        return order.indexOf(*a.group) < order.indexOf(*b.group);
    });                                          // (stable: fitting first in a group)
    return all;
}

NodeWindow::Loose NodeWindow::_new_line(const PickData &pd, bool free, const QString &mode, const OptStr &scope)
{
    // -> the loose entry (item, kind, mode, scope) of what was picked in the list.
    if (free)
        return {bc::pitem(pd.s, u"yes"_s), u"line"_s, mode, scope};
    return {bc::to_ref(bc::new_item(*pd.spec, env.get())), u"line"_s, pd.mode, scope};
}

void NodeWindow::add_anywhere(const QPointF &sp, const QPoint &vp)
{
    std::vector<Picker::Row> rows = _spec_rows(u"cond"_s, std::nullopt, false, u"Conditions · "_s);
    const std::vector<Picker::Row> eff = _spec_rows(u"eff"_s, std::nullopt, false, u"Effects · "_s);
    rows.insert(rows.end(), eff.begin(), eff.end());
    rows.push_back({u"option  —  a choice of the event"_s, PickData{u"option"_s, QString(), nullptr}, COLOURS.value(u"opt"_s),
                    u"The event's own parts"_s});
    rows.push_back({u"modifier  —  changes how often / how likely, while its conditions hold"_s,
                    PickData{u"modifier"_s, QString(), nullptr}, COLOURS.value(u"mod"_s), u"The event's own parts"_s});
    picker->open(vp, u"A new node (loose until it is wired) — pick a group on the left, or type to search"_s, rows,
                 [this, sp](const PickData &pd, bool free) {
                     Loose nw;
                     if (pd.s == u"option" && !pd.spec && !free)
                         nw = {_option(), u"option"_s, u"eff"_s, entry_scope};
                     else if (pd.s == u"modifier" && !pd.spec && !free)
                         nw = {bc::pitem(u"modifier"_s, PList{bc::pitem(u"factor"_s, u"1"_s)}), u"mod"_s, u"cond"_s,
                               entry_scope};
                     else
                         nw = _new_line(pd, free, u"cond"_s, entry_scope);
                     loose.push_back(nw);
                     pos.insert(nw.item.get(), sp);
                     rebuild();
                     _pick(nw.item.get());
                     say(u"%1: a loose node — drag from its right dot into an input to make it part of the script."_s.arg(
                         kstr(nw.item->k)));
                 },
                 u"a line as it is typed"_s);
}

NodeItem *NodeWindow::_pick(const PItem *item)
{
    view->scene()->clearSelection();
    NodeItem *n = node_of(item);
    if (n)
        n->setSelected(true);
    return n;
}

PPtr NodeWindow::_option()
{
    long long n = 0;
    for (const PPtr &c : root->items)
        if (c->k == OptStr(u"option"_s))
            ++n;
    static const QString L = u"ABCDEFGHIJ"_s;
    return bc::pitem(u"option"_s, PList{bc::pitem(u"name"_s, u"EVTOPT%1%2"_s.arg(L[int(std::min(n, 9LL))]).arg(entry_id->second),
                                                  true)});
}

void NodeWindow::add_for(NodeItem *node, int i, const std::optional<QPointF> &sp, const QPoint &vp)
{
    // A new node for an input of a node, wired in at once. sp: where (None: to the left of the input).
    const Sock sock = node->socks[size_t(i)];
    if (sock.single && !sock.members().empty()) {
        say(u"“%1” takes one node only."_s.arg(sock.name), true);
        return;
    }
    const QString title = node->title, kind = node->kind;
    auto put = [this, sp, sock, title, kind](const PPtr &item) {
        if (sp)
            pos.insert(item.get(), *sp + QPointF(20, -HEAD / 2));   // (to the right of the input)
        PList *lst = sock.lst(true);
        qsizetype at = qsizetype(lst->size());
        if (kind == u"entry" && !sock.sub && sock.type != u"opt")
            at = first_option(*lst);
        lst->insert(lst->begin() + at, item);
        changed(u"%1 added to %2 (%3)"_s.arg(kstr(item->k), title, sock.name));
        _pick(item.get());
    };
    if (sock.type == u"opt")
        return put(_option());
    if (sock.type == u"weight") {
        const bool mtth = sock.name == u"mean_time_to_happen";
        return put(bc::pitem(sock.name, PList{bc::pitem(mtth ? u"months"_s : u"factor"_s, mtth ? u"12"_s : u"1"_s)}));
    }
    if (sock.type == u"mod")
        return put(bc::pitem(u"modifier"_s, PList{bc::pitem(u"factor"_s, u"1"_s)}));
    if (sock.type == u"list")
        return put(bc::pitem(u"50"_s, PList()));
    const std::vector<Picker::Row> rows = _spec_rows(sock.type, sock.scope);
    picker->open(vp,
                 u"Into “%1” of %2 — %3 about %4"_s.arg(sock.name, node->title, TYPE_NAME.value(sock.type, sock.type),
                                                       sock.scope && bc::SCOPE_NAME.contains(*sock.scope)
                                                           ? bc::SCOPE_NAME.value(*sock.scope)
                                                           : u"anything"_s),
                 rows,
                 [this, put, sock](const PickData &pd, bool free) {
                     put(_new_line(pd, free, sock.type, sock.scope).item);
                 },
                 u"a line as it is typed"_s);
}

void NodeWindow::add_around(NodeItem *node, const QPointF &sp, const QPoint &vp)
{
    // A wire let go over the canvas, coming from a node's dot: a new node that takes it.
    if (node->kind != u"line") {
        say(u"Wire it into the “%1” input of a node."_s.arg(TYPE_NAME.value(node->type_(), node->type_())));
        return;
    }
    const std::vector<Picker::Row> rows = _spec_rows(node->mode, std::nullopt, true);
    const PPtr item = node->item;
    const QString mode = node->mode;
    const OptStr scope = node->scope;
    picker->open(vp, u"A new node that takes “%1”"_s.arg(node->title), rows,
                 [this, sp, item, mode, scope](const PickData &pd, bool free) {
                     Loose nw;
                     if (free) {
                         nw = {bc::pitem(pd.s, PList()), u"line"_s, mode, scope};
                     } else {
                         nw = _new_line(pd, false, mode, scope);
                         std::erase_if(nw.item->items, [](const PPtr &c) { return c->list; });
                     }
                     loose.push_back(nw);
                     pos.insert(nw.item.get(), sp);
                     rebuild();
                     NodeItem *t = node_of(nw.item.get()), *src = node_of(item.get());
                     if (!t || !src)
                         return;
                     std::optional<int> si;
                     for (int k = 0; k < int(t->socks.size()); ++k)
                         if (fits(src, t->socks[size_t(k)])) {
                             si = k;
                             break;
                         }
                     if (!si || !wire(src, t, *si))
                         changed(u"%1 added"_s.arg(kstr(nw.item->k)));
                 },
                 u"a block of this name"_s);
}

void NodeWindow::delete_selected()
{
    QGraphicsScene *sc = view->scene();
    std::vector<Wire *> wires;
    std::vector<NodeItem *> gone;
    const QList<QGraphicsItem *> selected = sc->selectedItems();
    for (QGraphicsItem *it : selected)
        if (auto *w = dynamic_cast<Wire *>(it))
            wires.push_back(w);
    for (QGraphicsItem *it : selected)
        if (auto *n = dynamic_cast<NodeItem *>(it); n && n->kind != u"entry")
            gone.push_back(n);
    if (wires.empty() && gone.empty())
        return;
    auto in_gone = [&](NodeItem *n) { return std::find(gone.begin(), gone.end(), n) != gone.end(); };
    for (Wire *w : wires) {                      // a wire: the node stays, loose
        NodeItem *n = w->node;
        if (in_gone(n) || !n->parent_)
            continue;
        const OptStr scope = n->parent_->socks[size_t(n->psock)].scope;
        const PPtr keep = n->item;
        unhook(n);
        loose.push_back({keep, n->kind, n->mode, scope});
    }
    for (NodeItem *n : gone) {
        for (NodeItem *k : kids(n))              // what was wired into it stays, loose
            if (!in_gone(k)) {
                const OptStr scope = n->socks[size_t(k->psock)].scope;
                const PPtr keep = k->item;
                std::erase_if(*k->home, [&](const PPtr &x) { return x.get() == keep.get(); });
                loose.push_back({keep, k->kind, k->mode, scope});
            }
        unhook(n);
    }
    const QString what = !gone.empty() ? u"%1 node%2 removed"_s.arg(gone.size()).arg(gone.size() == 1 ? QString() : u"s"_s)
                                       : u"%1 wire%2 removed"_s.arg(wires.size()).arg(wires.size() == 1 ? QString() : u"s"_s);
    changed(what);
}

// ------------------------------------------------------------------ writing the script
QString NodeWindow::script()
{
    QString s = bc::dump({bc::from_ref(*root)}, depth);
    qsizetype i = 0;
    while (i < s.size() && s[i] == u'\t')
        ++i;
    return s.mid(i);
}

void NodeWindow::changed(const QString &what, const OptStr &after, const eventtemplates::Names &names)
{
    // The tree changed: the picture follows, and the script is written (one undo step). after: a text put right
    // after the entry (a follow-up event); names: texts for the localisation, in the same step.
    rebuild();
    say(what);
    if (!root || !doc || !span)
        return;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString text = store.text(*rel);
    const auto [a, b] = *span;
    const QString new_ = script();
    const bool has_after = after && !after->isEmpty();
    if (new_ == _dump && !has_after)             // (only loose nodes moved: the script is what it was)
        return;
    _dump = new_;
    _written = new_;
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    const QString out = nl == u"\r\n" ? QString(new_).replace(u"\n"_s, u"\r\n"_s) : new_;
    span = std::make_pair(a, a + out.size());
    const QString tail = has_after ? nl + nl + QString(*after).replace(u"\n"_s, nl) : QString();
    const QString label = u"Nodes — %1 %2: %3"_s.arg(entry_kind, entry_id->second, what);
    const QString full = py::head(text, a) + out + tail + py::tail(text, b);
    if (!names.isEmpty())
        Q_EMIT scriptNamesEdit(label, *rel, full, names);
    else
        Q_EMIT scriptEdit(label, *rel, full);
}

}  // namespace nodegraph
