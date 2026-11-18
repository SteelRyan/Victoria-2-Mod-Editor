// changelog.py (see changelog.h).
#include "ui/changelog.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "ui/colsizes.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFont>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringDecoder>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSet>
#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace changelog {

// the categories: first by the function of the editor a change comes from, then the ones of the GitHub releases
const QStringList FUNCTION_CATS = {
    u"Events"_s, u"Decisions"_s, u"Modifiers"_s, u"Reforms"_s, u"Unciv reforms"_s, u"Party issues"_s,
    u"Government forms"_s, u"National values"_s, u"Ideologies"_s, u"Rebels"_s, u"War goals"_s, u"Leader traits"_s,
    u"Wars"_s, u"Terrain"_s, u"Game settings"_s, u"Start dates"_s, u"Timed events"_s, u"News"_s, u"Pop types"_s,
    u"National focuses"_s, u"Crimes"_s, u"Cultures"_s, u"Unit colours"_s, u"Interface"_s, u"Pictures & music"_s,
    u"Nations"_s, u"Diplomacy"_s, u"Units"_s, u"Armies & navies"_s, u"Technology"_s, u"Tech schools"_s,
    u"Production"_s, u"Pops"_s, u"Provinces"_s, u"Map"_s, u"Names & texts"_s};
const QStringList RELEASE_CATS = {
    u"Bugfixes"_s, u"Gameplay"_s, u"Balance"_s, u"Flavor"_s, u"Map/Scenario"_s, u"Nations & Tags"_s, u"Military"_s,
    u"Assimilation & Culture"_s, u"Mechanics & Systems"_s, u"Quality of Life"_s, u"Various Optimizations"_s,
    u"Silly"_s, u"Reverts"_s, u"Removed"_s, u"Acknowledgments"_s, u"Other"_s};
const QStringList CATEGORIES = FUNCTION_CATS + RELEASE_CATS;
const QStringList ORDER = QStringList{u"Bugfixes"_s} + FUNCTION_CATS + RELEASE_CATS.mid(1);   // a new heading's place
const OrderedMap<QString, QString> TAB_CATS = {
    {u"pops"_s, u"Pops"_s}, {u"provinces"_s, u"Provinces"_s}, {u"map"_s, u"Map"_s}, {u"nations"_s, u"Nations"_s},
    {u"politics"_s, u"Reforms"_s}, {u"armies"_s, u"Armies & navies"_s}, {u"diplomacy"_s, u"Diplomacy"_s},
    {u"technology"_s, u"Technology"_s}, {u"production"_s, u"Production"_s}, {u"events"_s, u"Events"_s},
    {u"errors"_s, u"Bugfixes"_s}, {u"changelog"_s, u"Other"_s}, {u"library"_s, u"Pictures & music"_s},
    {u"warfare"_s, u"Units"_s}, {u"rules"_s, u"Game settings"_s}, {u"shortcuts"_s, u"Quality of Life"_s},
    {u"submods"_s, u"Other"_s}, {u"checksum"_s, u"Other"_s}, {u"econest"_s, u"Other"_s}, {u"popinf"_s, u"Pop types"_s}};
const QStringList PALETTE = {u"#c9a15a"_s, u"#6fa8dc"_s, u"#93c47d"_s, u"#e06666"_s, u"#b48ad6"_s, u"#f6b26b"_s,
                             u"#76c7c0"_s, u"#d5a6bd"_s, u"#a4c2f4"_s, u"#b6d7a8"_s, u"#ea9999"_s, u"#ffd966"_s};
static const QString COLOR_RE = u"§.|§"_s;
static const QString HEAD_RE = u"^\\*\\*(.+?)\\*\\*\\s*$"_s;
const QString MIME = u"application/x-modeditor-changelog-block"_s;

QString app_dir() { return pypath::normpath(moreviews::app_dir()); }

QString cat_color(const QString &name)
{
    const qsizetype i = CATEGORIES.indexOf(name);
    if (i >= 0)
        return PALETTE[i % PALETTE.size()];
    long long s = 0;
    for (const char32_t c : name.toUcs4())
        s += static_cast<long long>(c);
    return PALETTE[static_cast<qsizetype>(s % PALETTE.size())];
}

// ---------------------------------------------------------------------- small pieces (Python's dicts)
static Value obj(std::initializer_list<std::pair<QString, Value>> items)
{
    pyjson::Object o;
    for (const auto &i : items)
        o.insert(i.first, i.second);
    return Value(std::move(o));
}
static QString sget(const Value &d, const QString &k, const QString &fallback = QString())   // str(d.get(k, fb))
{
    const Value *v = d.get(k);
    return v ? v->str() : fallback;
}
static std::optional<long long> iget(const Value &d, const QString &k)                       // d.get(k) as an int
{
    const Value *v = d.get(k);
    if (!v)
        return std::nullopt;
    if (const auto *i = std::get_if<long long>(&v->v))
        return *i;
    if (const auto *b = std::get_if<bool>(&v->v))
        return *b ? 1 : 0;
    return std::nullopt;
}
static bool truthy(const Value &d, const QString &k)
{
    const Value *v = d.get(k);
    return v && v->truthy();
}
static QString cap(const QString &s) { return s.isEmpty() ? s : py::upper(s.left(1)) + s.mid(1); }
static QString uncap(const QString &s) { return s.isEmpty() ? s : py::lower(s.left(1)) + s.mid(1); }
static QString join_ws(const QString &s) { return py::split(s).join(u' '); }   // " ".join(s.split())
static QString os_error(const QString &path) { return u"[Errno 13] Permission denied: %1"_s.arg(py::repr_str(path)); }

// ---------------------------------------------------------------------- what changed in a script file
static bool kin(const script::Node &n) { return n.has_key && !n.key.isEmpty(); }
static QString val(const script::Node &n) { return py::strip(n.value); }

using Leaves = OrderedMap<QString, QString>;
static void leaves(const script::Nodes &nodes, const QString &pre, Leaves &out)
{
    // {path: value} of the plain values inside (repeated keys numbered)
    OrderedMap<QString, int> seen;
    for (const script::Node &n : nodes) {
        if (!kin(n))
            continue;
        QString k = pre + n.key;
        const int c = ++seen[k];
        if (c > 1)
            k += u"#%1"_s.arg(c);
        if (n.is_block)
            leaves(n.kids, k + u" › "_s, out);
        else
            out.insert(k, val(n));
    }
}
static Leaves leaves(const script::Nodes &nodes)
{
    Leaves out;
    leaves(nodes, QString(), out);
    return out;
}

struct Ent {
    const script::Node *node;
    QString text;
};
using Key = std::pair<QString, QString>;
using Ents = OrderedMap<Key, Ent>;

static Ents entries_of(const QString &text, const script::Nodes &nodes)
{
    // {(group, key): (node, text)} — the things a file is made of: events by id, the blocks inside wrapper blocks
    // (political_decisions = { … }, political_reforms = { … }, schools = { … }) and else the top-level blocks / values
    Ents out;
    for (const script::Node &n : nodes) {
        if (!kin(n))
            continue;
        if (n.is_block && (n.key == u"country_event" || n.key == u"province_event")) {
            QString eid = u"None"_s;
            for (const script::Node &c : n.kids)
                if (c.has_key && c.key == u"id" && !c.is_block) {
                    eid = val(c);
                    break;
                }
            out.insert({u"event"_s, eid}, {&n, py::slice(text, n.kstart, n.vend)});
            continue;
        }
        std::vector<const script::Node *> kids;
        if (n.is_block)
            for (const script::Node &c : n.kids)
                if (kin(c))
                    kids.push_back(&c);
        const bool all_blocks = std::all_of(kids.begin(), kids.end(), [](const script::Node *c) { return c->is_block; });
        if ((!kids.empty() && all_blocks && kids.size() > 1) || n.key.endsWith(u"_decisions") ||
            n.key.endsWith(u"_reforms") || n.key == u"party_issues" || n.key == u"schools") {
            for (const script::Node *c : kids)
                out.insert({n.key, c->key}, {c, py::slice(text, c->kstart, c->vend)});
        } else {
            out.insert({QString(), n.key}, {&n, py::slice(text, n.kstart, n.vend)});
        }
    }
    return out;
}

static QString norm(const QString &s)
{
    QString t = s;
    t.replace(py::rx(u"#[^\\n]*"_s), QString());
    return join_ws(t);
}

QString file_category(const QString &rel, const QString &group)
{
    // the category of a change by the file (and the block in it) it is in — the function of the editor
    const QString r = py::lower(rel);
    if (r.startsWith(u"events/"))
        return u"Events"_s;
    if (r.startsWith(u"decisions/"))
        return u"Decisions"_s;
    if (r.startsWith(u"units/"))
        return u"Units"_s;
    if (r.endsWith(u"issues.txt")) {
        if (group == u"party_issues")
            return u"Party issues"_s;
        if (group == u"economic_reforms" || group == u"military_reforms")
            return u"Unciv reforms"_s;
        return u"Reforms"_s;
    }
    static const std::vector<std::pair<QString, QString>> ENDS = {
        {u"cb_types.txt"_s, u"War goals"_s}, {u"traits.txt"_s, u"Leader traits"_s},
        {u"defines.lua"_s, u"Game settings"_s}, {u"national_focus.txt"_s, u"National focuses"_s},
        {u"rebel_types.txt"_s, u"Rebels"_s}, {u"ideologies.txt"_s, u"Ideologies"_s}, {u"crime.txt"_s, u"Crimes"_s},
        {u"map/terrain.txt"_s, u"Terrain"_s}, {u"bookmarks.txt"_s, u"Start dates"_s},
        {u"on_actions.txt"_s, u"Timed events"_s}, {u"country_colors.txt"_s, u"Unit colours"_s},
        {u"cultures.txt"_s, u"Cultures"_s}};
    for (const auto &[end, cat] : ENDS)
        if (r.endsWith(end))
            return cat;
    if (r.startsWith(u"news/"))
        return u"News"_s;
    if (r.startsWith(u"interface/"))
        return u"Interface"_s;
    if (r.startsWith(u"poptypes/"))
        return u"Pop types"_s;
    if (r.startsWith(u"history/wars"))
        return u"Wars"_s;
    if (r.endsWith(u"governments.txt"))
        return u"Government forms"_s;
    if (r.endsWith(u"nationalvalues.txt"))
        return u"National values"_s;
    if (r.endsWith(u"common/technology.txt"))
        return group == u"schools" ? u"Tech schools"_s : u"Technology"_s;
    if (r.startsWith(u"technologies/") || r.startsWith(u"inventions/"))
        return u"Technology"_s;
    if (r.contains(u"modifiers"))
        return u"Modifiers"_s;
    for (const QString end : {u"production_types.txt"_s, u"goods.txt"_s, u"buildings.txt"_s, u"pop_types.txt"_s})
        if (r.endsWith(end))
            return u"Production"_s;
    if (r.startsWith(u"localisation/"))
        return u"Names & texts"_s;
    if (r.startsWith(u"history/units"))
        return u"Armies & navies"_s;
    if (r.startsWith(u"history/diplomacy"))
        return u"Diplomacy"_s;
    if (r.startsWith(u"history/pops"))
        return u"Pops"_s;
    if (r.startsWith(u"history/provinces"))
        return u"Provinces"_s;
    if (r.startsWith(u"history/countries") || r.startsWith(u"common/countries"))
        return u"Nations"_s;
    if (r.startsWith(u"map/"))
        return u"Map"_s;
    return u"Other"_s;
}

static QString pretty(const QString &path)
{
    // the last key of a path (without the "#2" of repeated keys), readable
    const qsizetype at = path.lastIndexOf(u" › "_s);
    QString last = at < 0 ? path : path.mid(at + 3);
    last.replace(py::rx(u"#\\d+$"_s), QString());
    return last.replace(u'_', u' ');
}

static QString what_of(const QString &rel)
{
    const QString r = py::lower(rel);
    static const std::vector<std::pair<QString, QString>> PRE = {
        {u"events/"_s, u"event"_s}, {u"decisions/"_s, u"decision"_s}, {u"units/"_s, u"unit"_s},
        {u"technologies/"_s, u"technology"_s}, {u"inventions/"_s, u"invention"_s}, {u"poptypes/"_s, u"pop type"_s}};
    for (const auto &[pre, w] : PRE)
        if (r.startsWith(pre))
            return w;
    static const std::vector<std::pair<QString, QString>> ENDS = {
        {u"governments.txt"_s, u"government form"_s}, {u"nationalvalues.txt"_s, u"national value"_s},
        {u"common/technology.txt"_s, u"technology school"_s}, {u"issues.txt"_s, u"reform"_s},
        {u"cb_types.txt"_s, u"war goal"_s}, {u"traits.txt"_s, u"leader trait"_s},
        {u"national_focus.txt"_s, u"national focus"_s}, {u"rebel_types.txt"_s, u"rebel type"_s},
        {u"ideologies.txt"_s, u"ideology"_s}, {u"crime.txt"_s, u"crime"_s}};
    for (const auto &[end, w] : ENDS)
        if (r.endsWith(end))
            return w;
    if (r.contains(u"modifiers"))
        return u"modifier"_s;
    return QString();
}

static QString loc_of(const Loc &loc, const QString &key)     // (empty: None)
{
    const OptStr t = loc && !key.isEmpty() ? loc(key) : std::nullopt;
    if (!t || t->isEmpty())
        return QString();
    QString s = *t;
    s.replace(py::rx(COLOR_RE), QString());
    return py::strip(s);
}

static QString name_of(const Key &key, const script::Node &node, const Loc &loc, const QString &what)
{
    const auto &[group, k] = key;
    if (group == u"event") {
        OptStr title;
        if (node.is_block)
            for (const script::Node &c : node.kids)
                if (c.has_key && c.key == u"title" && !c.is_block) {
                    title = val(c);
                    break;
                }
        const QString tt = title ? loc_of(loc, py::strip(*title, u"\""_s)) : QString();
        return u"event %1%2"_s.arg(k, !tt.isEmpty() ? u" “%1”"_s.arg(tt) : QString());
    }
    QString nice = loc_of(loc, k);
    if (nice.isEmpty())
        nice = loc_of(loc, k + u"_title"_s);
    if (nice.isEmpty())
        nice = QString(k).replace(u'_', u' ');
    if (group.endsWith(u"_reforms") || group == u"party_issues")
        return u"%1 “%2”"_s.arg(group == u"party_issues" ? u"party issue"_s : u"reform"_s, nice);
    return !what.isEmpty() ? u"%1 “%2”"_s.arg(what, nice) : u"“%1”"_s.arg(nice);
}

static QStringList leaf_diffs(const Leaves &la, const Leaves &lb)
{
    QStringList out;
    QStringList keys;
    for (const auto &kv : lb)
        keys << kv.first;
    for (const auto &kv : la)
        if (!lb.contains(kv.first))
            keys << kv.first;
    for (const QString &k : keys) {
        if (!la.contains(k))
            out << u"%1 %2 (new)"_s.arg(pretty(k), *lb.get(k));
        else if (!lb.contains(k))
            out << u"no more %1"_s.arg(pretty(k));
        else if (*la.get(k) != *lb.get(k))
            out << u"%1 %2 → %3"_s.arg(pretty(k), *la.get(k), *lb.get(k));
    }
    return out;
}

static Value file_entry(const QString &text, const QString &section, const QString &rel)
{
    return obj({{u"text"_s, text}, {u"section"_s, section}, {u"src"_s, u"file"_s}, {u"rel"_s, rel}});
}

static pyjson::Array event_changes(const QString &rel, const QString &old, const QString &new_, const Loc &loc)
{
    // an events / decisions file: one line for each event (by its id) or decision that was added, edited or
    // deleted — not what was changed inside it
    pyjson::Array out;
    const script::Nodes na = py::strip(old).isEmpty() ? script::Nodes() : script::parse(old);
    const script::Nodes nb = py::strip(new_).isEmpty() ? script::Nodes() : script::parse(new_);
    const Ents a = entries_of(old, na), b = entries_of(new_, nb);
    const QString what = what_of(rel);
    auto put = [&](const Key &key, const script::Node &node, const QString &how, bool added) {
        const QString nm = name_of(key, node, loc, what);
        out.push_back(obj({{u"text"_s, u"%1 %2"_s.arg(cap(nm), how)}, {u"section"_s, file_category(rel, key.first)},
                           {u"src"_s, u"file"_s}, {u"rel"_s, rel}, {u"key"_s, key.second}, {u"added"_s, added}}));
    };
    for (const auto &[key, e] : b) {
        const Ent *x = a.get(key);
        if (!x)
            put(key, *e.node, u"added"_s, true);
        else if (join_ws(x->text) != join_ws(e.text))
            put(key, *e.node, u"edited"_s, false);
    }
    for (const auto &[key, e] : a)
        if (!b.contains(key))
            put(key, *e.node, u"deleted"_s, false);
    if (out.empty() && py::strip(old).isEmpty())
        out.push_back(file_entry(u"Added %1"_s.arg(rel), file_category(rel), rel));
    return out;
}

static pyjson::Array nation_changes(const QString &rel, const QString &old, const QString &new_, const Loc &loc)
{
    // a nation's file (common/countries/…): its map colour, and its parties by name
    const QString who = py::slice(rel.mid(rel.lastIndexOf(u'/') + 1), 0, -4);
    pyjson::Array out;
    auto put = [&](const QString &text) {
        out.push_back(obj({{u"text"_s, text}, {u"section"_s, u"Nations"_s}, {u"src"_s, u"file"_s}, {u"rel"_s, rel}}));
    };
    using Fields = OrderedMap<QString, QString>;
    auto read = [](const QString &text) {
        OptStr colour;
        OrderedMap<QString, Fields> parties;
        for (const script::Node &n : script::parse(text)) {
            if (n.has_key && n.key == u"color") {
                QStringList ds;
                auto it = py::rx(u"\\d+"_s).globalMatch(py::slice(text, n.vstart, n.vend));
                while (it.hasNext())
                    ds << it.next().captured(0);
                colour = ds.join(u' ');
            } else if (n.has_key && n.key == u"party" && n.is_block) {
                Fields f;
                for (const script::Node &c : n.kids)
                    if (kin(c) && !c.is_block)
                        f.insert(c.key, val(c));
                const QString name = py::strip(f.value(u"name"_s, u"?"_s), u"\""_s);
                parties.insert(!parties.contains(name) ? name : u"%1 (%2)"_s.arg(name, f.value(u"start_date"_s, u"?"_s)), f);
            }
        }
        return std::pair{colour, parties};
    };
    const auto [ca, pa] = read(old);
    const auto [cb, pb] = read(new_);
    if (ca != cb)
        put(u"%1: map colour %2 → %3"_s.arg(who, ca && !ca->isEmpty() ? *ca : u"—"_s, cb && !cb->isEmpty() ? *cb : u"—"_s));
    auto nice_of = [&](const QString &name) {
        const QString n = loc_of(loc, name.split(u" ("_s)[0]);
        return n.isEmpty() ? name : n;
    };
    for (const auto &[name, f] : pb) {
        const Fields *g = pa.get(name);
        if (!g) {
            put(u"%1: new party “%2”"_s.arg(who, nice_of(name)));
        } else if (!(*g == f)) {
            const QStringList ds = leaf_diffs(*g, f).mid(0, 4);
            put(u"%1: party “%2”: %3"_s.arg(who, nice_of(name), ds.isEmpty() ? u"changed"_s : ds.join(u", "_s)));
        }
    }
    for (const auto &[name, f] : pa)
        if (!pb.contains(name))
            put(u"%1: party “%2” removed"_s.arg(who, nice_of(name)));
    if (out.empty())
        put(u"%1: its file changed"_s.arg(who));
    return out;
}

pyjson::Array script_changes(scriptstore::ScriptStore &store, const Loc &loc)
{
    // [{text, section, src, rel}] for every changed script file (its text on disk vs the editor's)
    pyjson::Array out;
    QStringList rels = store.dirty();
    std::sort(rels.begin(), rels.end());
    for (const QString &rel : rels) {
        const QString old = store.orig.value(rel);
        const QString new_ = store.text(rel);
        if (store.gone.contains(rel)) {                     // a file taken out of the mod
            out.push_back(file_entry(u"Removed %1"_s.arg(rel), file_category(rel), rel));
            continue;
        }
        const QString low = py::lower(rel);
        const bool ev = low.startsWith(u"events/") || low.startsWith(u"decisions/");
        if (ev && !py::strip(new_).isEmpty()) {            // by id: added / edited / deleted
            for (Value &v : event_changes(rel, old, new_, loc))
                out.push_back(v);
            continue;
        }
        if (py::strip(old).isEmpty() || py::strip(new_).isEmpty()) {   // a new file / one emptied to override the game's
            out.push_back(file_entry(u"%1 %2"_s.arg(!py::strip(new_).isEmpty() ? u"Added"_s : u"Removed"_s, rel),
                                     file_category(rel), rel));
            continue;
        }
        if (low.endsWith(u".lua")) {                        // the game settings: one value per line
            static const QRegularExpression pat(
                u"(*LF)^%1*([A-Za-z_]\\w*)%1*=%1*([^,\\n{]+?)%1*,?%1*(--.*)?$"_s.arg(py::PY_S),
                QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption);
            auto values = [](const QString &t) {
                OrderedMap<QString, QString> d;
                auto it = pat.globalMatch(t);
                while (it.hasNext()) {
                    const auto m = it.next();
                    d.insert(m.captured(1), m.captured(2));
                }
                return d;
            };
            const auto a = values(old), b = values(new_);
            for (const auto &[k, v] : b) {
                const QString *x = a.get(k);
                if (!x || *x != v)
                    out.push_back(file_entry(u"%1: %2 → %3"_s.arg(k, x ? *x : u"—"_s, v), file_category(rel), rel));
            }
            continue;
        }
        if (ev) {                                           // by id only: added / edited / deleted
            for (Value &v : event_changes(rel, old, new_, loc))
                out.push_back(v);
            continue;
        }
        if (low.startsWith(u"common/countries/")) {         // a nation's file: its colour and its parties by name
            for (Value &v : nation_changes(rel, old, new_, loc))
                out.push_back(v);
            continue;
        }
        if (low.startsWith(u"poptypes/") || low.startsWith(u"history/wars/")) {   // one thing per file
            const QString base = py::slice(rel.mid(rel.lastIndexOf(u'/') + 1), 0, -4);
            const QString nice = loc_of(loc, base);
            const QString who = u"%1 “%2”"_s.arg(low.startsWith(u"poptypes/") ? u"Pop type"_s : u"War"_s,
                                                 nice.isEmpty() ? base : nice);
            const QStringList ds = leaf_diffs(leaves(script::parse(old)), leaves(script::parse(new_)));
            out.push_back(file_entry(!ds.isEmpty() && ds.size() <= 4 ? u"%1: %2"_s.arg(who, ds.join(u", "_s))
                                                                     : u"Reworked %1"_s.arg(uncap(who)),
                                     file_category(rel), rel));
            continue;
        }
        const script::Nodes na = script::parse(old), nb = script::parse(new_);
        const Ents a = entries_of(old, na), b = entries_of(new_, nb);
        const QString what = what_of(rel);
        auto put = [&](const QString &text, const Key &key, bool added) {
            out.push_back(obj({{u"text"_s, text}, {u"section"_s, file_category(rel, key.first)}, {u"src"_s, u"file"_s},
                               {u"rel"_s, rel}, {u"key"_s, key.second}, {u"added"_s, added}}));
        };
        for (const auto &[key, e] : b)
            if (!a.contains(key))
                put(u"Added %1"_s.arg(name_of(key, *e.node, loc, what)), key, true);
        for (const auto &[key, e] : a)
            if (!b.contains(key))
                put(u"Removed %1"_s.arg(name_of(key, *e.node, loc, what)), key, false);
        for (const auto &[key, e] : b) {
            const Ent *x = a.get(key);
            if (!x || norm(x->text) == norm(e.text))
                continue;
            const Leaves la = x->node->is_block ? leaves(x->node->kids) : Leaves{{QString(), val(*x->node)}};
            const Leaves lb = e.node->is_block ? leaves(e.node->kids) : Leaves{{QString(), val(*e.node)}};
            const bool reform = key.first.endsWith(u"_reforms") || key.first == u"party_issues";
            OrderedMap<QString, QStringList> diffs;   // reforms: per option; else all under ""
            QStringList keys;
            for (const auto &kv : lb)
                keys << kv.first;
            for (const auto &kv : la)
                if (!lb.contains(kv.first))
                    keys << kv.first;
            for (const QString &k : keys) {
                const qsizetype at = k.indexOf(u" › "_s);
                const QString head = at < 0 ? k : k.left(at), rest = at < 0 ? QString() : k.mid(at + 3);
                const bool by_option = reform && !rest.isEmpty();
                const QString grp = by_option ? head : QString(), leaf = by_option ? rest : k;
                QString d;
                if (!la.contains(k))
                    d = u"%1 %2 (new)"_s.arg(pretty(leaf), *lb.get(k));
                else if (!lb.contains(k))
                    d = u"no more %1"_s.arg(pretty(leaf));
                else if (*la.get(k) != *lb.get(k))
                    d = !k.isEmpty() ? u"%1 %2 → %3"_s.arg(pretty(leaf), *la.get(k), *lb.get(k))
                                     : u"%1 → %2"_s.arg(*la.get(k), *lb.get(k));
                else
                    continue;
                diffs[grp].append(d);
            }
            const QString nm = name_of(key, *e.node, loc, what);
            if (diffs.isEmpty())
                diffs.insert(QString(), {});
            for (const auto &[grp, ds] : diffs) {
                QString who = cap(nm);
                if (!grp.isEmpty()) {
                    const QString g = loc_of(loc, grp);
                    who = u"%1 → %2"_s.arg(who, !g.isEmpty() ? g : QString(grp).replace(u'_', u' '));
                }
                put(!ds.isEmpty() && ds.size() <= 4
                        ? u"%1: %2"_s.arg(who, ds.join(u", "_s))
                        : u"Reworked %1"_s.arg(!who.startsWith(u"“") ? uncap(who) : who),
                    key, false);
            }
        }
    }
    return out;
}

pyjson::Array edit_steps(const std::vector<std::tuple<QString, QString, bool>> &steps)
{
    auto key_of = [](const QString &lb) {
        return lb.contains(u'=') ? py::strip(lb.left(lb.lastIndexOf(u'='))) : lb;
    };
    QHash<QString, QString> last;
    for (const auto &[lb, tab, scr] : steps)
        last.insert(key_of(lb), lb);
    pyjson::Array out;
    QSet<QString> seen;
    for (const auto &[lb, tab, scr] : steps) {
        const QString key = key_of(lb);
        if (last.value(key) == lb && !seen.contains(lb)) {
            seen.insert(lb);
            const QString cat = lb.startsWith(u"Name ") ? u"Names & texts"_s : TAB_CATS.value(tab, u"Other"_s);
            out.push_back(obj({{u"text"_s, lb}, {u"section"_s, cat}, {u"src"_s, u"edit"_s}, {u"script"_s, scr}}));
        }
    }
    return out;
}

// ---------------------------------------------------------------------- the notes
QString notes_markdown(const pyjson::Array &sections)
{
    QStringList out;
    for (const Value &s : sections) {
        if (!truthy(s, u"blocks"_s))
            continue;
        if (!out.isEmpty())
            out << QString();
        out << u"**%1**"_s.arg(sget(s, u"name"_s));
        for (const Value &b : s.get(u"blocks"_s)->arr()) {
            QStringList lines;
            for (const QString &ln : py::splitlines(py::strip(sget(b, u"text"_s))))
                if (!py::strip(ln).isEmpty())
                    lines << py::rstrip(ln);
            if (lines.isEmpty())
                lines << QString();
            out << u"- "_s + lines[0];
            for (qsizetype i = 1; i < lines.size(); ++i)
                out << u"  "_s + lines[i];
        }
    }
    return out.join(u'\n') + (!out.isEmpty() ? u"\n"_s : QString());
}

pyjson::Array parse_body(const QString &body)
{
    // notes typed as text ("**Heading**" and "- " lines) as sections of blocks (the older drafts)
    pyjson::Array sections;
    Value cur;
    for (const QString &ln : py::splitlines(body)) {
        const QString s = py::strip(ln);
        const auto m = py::re_match(HEAD_RE, s);
        if (m.hasMatch()) {
            cur = obj({{u"name"_s, py::strip(m.captured(1))}, {u"blocks"_s, pyjson::Array{}}});
            sections.push_back(cur);
        } else if (s.startsWith(u"- ")) {
            if (cur.is_null()) {
                cur = obj({{u"name"_s, u"Other"_s}, {u"blocks"_s, pyjson::Array{}}});
                sections.push_back(cur);
            }
            cur.obj()[u"blocks"_s].arr().push_back(obj({{u"text"_s, py::strip(s.mid(2))}}));
        } else if (!s.isEmpty() && !cur.is_null() && !cur.obj()[u"blocks"_s].arr().empty()) {
            Value &b = cur.obj()[u"blocks"_s].arr().back();
            b.obj()[u"text"_s] = Value(b.obj()[u"text"_s].str() + u"\n"_s + s);
        }
    }
    return sections;
}

QString bump(const QString &version)
{
    const auto m = py::re_match(u"^(.*?)(\\d+)$"_s, version);
    if (!m.hasMatch())
        return !version.isEmpty() ? version : u"1.0.0"_s;
    const QString n = m.captured(2);
    QString digits;                                 // str(int(n) + 1), any length
    for (const QChar c : n)
        digits += QChar(u'0' + c.digitValue());
    qsizetype i = digits.size() - 1;
    while (i >= 0 && digits[i] == u'9')
        digits[i--] = u'0';
    if (i < 0)
        digits.prepend(u'1');
    else
        digits[i] = QChar(digits[i].unicode() + 1);
    while (digits.size() > 1 && digits[0] == u'0')
        digits.remove(0, 1);
    while (digits.size() < n.size())                // .zfill(len(n))
        digits.prepend(u'0');
    return m.captured(1) + digits;
}

void NoWheelCombo::wheelEvent(QWheelEvent *e) { e->ignore(); }

NoWheelCombo *cat_combo(const QString &current, const QStringList &extra)
{
    auto *cb = new NoWheelCombo();
    cb->setEditable(true);
    cb->setInsertPolicy(QComboBox::NoInsert);
    cb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QStringList names = CATEGORIES;
    for (const QString &x : extra)
        if (!CATEGORIES.contains(x))
            names << x;
    if (!names.contains(current))
        names << current;
    cb->addItems(names);
    cb->insertSeparator(FUNCTION_CATS.size());
    cb->setCurrentIndex(cb->findText(current));
    return cb;
}

QPushButton *tool_btn(const QString &text, const QString &tip, std::function<void()> fn, int width)
{
    auto *b = new QPushButton(text);
    b->setFixedWidth(width);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    QObject::connect(b, &QPushButton::clicked, b, [fn = std::move(fn)] { fn(); });
    return b;
}

Handle::Handle(BlockCard *card) : QLabel(u"⠿"_s), card(card)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setCursor(Qt::OpenHandCursor);
    setToolTip(u"Drag to move this block"_s);
    setFixedWidth(26);
    setAlignment(Qt::AlignCenter);
    QFont f(font());
    f.setPointSizeF(f.pointSizeF() + 5);
    setFont(f);
}

void Handle::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        _press = e->position().toPoint();
}

void Handle::mouseMoveEvent(QMouseEvent *e)
{
    if (!_press || (e->position().toPoint() - *_press).manhattanLength() < QApplication::startDragDistance())
        return;
    _press.reset();
    auto *drag = new QDrag(card);
    auto *md = new QMimeData();
    md->setData(MIME, QString::number(card->bid).toUtf8());
    drag->setMimeData(md);
    QPixmap pm = card->grab();
    if (pm.width() > 520)
        pm = pm.copy(0, 0, 520, pm.height());
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(14, std::min(18, pm.height() / 2)));
    NotesBoard *board = card->board;
    drag->exec(Qt::MoveAction);
    QTimer::singleShot(0, board, [board] { board->apply_drop(); });   // (after the drag: the blocks are built again)
}

void Handle::mouseReleaseEvent(QMouseEvent *) { _press.reset(); }

BlockCard::BlockCard(NotesBoard *board, const Value &sec, const Value &block, const QStringList &extra_cats)
    : board(board), bid(iget(block, u"id"_s).value_or(0))
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setObjectName(u"legoBlock"_s);
    setStyleSheet(u"QFrame#legoBlock { background: %1; border: 1px solid %2; border-left: 6px solid %3; "
                  u"border-radius: 5px; }"_s.arg(theme::PANEL_ALT, theme::LINE, cat_color(sec.get(u"name"_s)->str())));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 6, 8, 6);
    lay->setSpacing(8);
    lay->addWidget(new Handle(this));
    const bool is_open = board->open_id == bid;
    const QString btext = sget(block, u"text"_s);
    if (is_open) {
        editor = new QPlainTextEdit();
        editor->setPlainText(btext);
        editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const qsizetype n = std::max<qsizetype>(3, std::min<qsizetype>(12, btext.count(u'\n') + btext.size() / 70 + 2));
        editor->setFixedHeight(editor->fontMetrics().lineSpacing() * static_cast<int>(n) + 16);
        lay->addWidget(editor, 1);
    } else {
        text = new QLabel(btext);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setMinimumWidth(120);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setStyleSheet(u"border: none; background: transparent; color: %1;"_s.arg(theme::INK));
        lay->addWidget(text, 1);
    }
    cat = cat_combo(sec.get(u"name"_s)->str(), extra_cats);
    cat->setFixedWidth(190);
    cat->setToolTip(u"The category this block stands under"_s);
    connect(cat, &QComboBox::activated, this, [this](int) { this->board->set_category(bid, cat->currentText()); });
    connect(cat->lineEdit(), &QLineEdit::editingFinished, this,
            [this] { this->board->set_category(bid, cat->currentText()); });
    lay->addWidget(cat);
    const long long id = bid;
    lay->addWidget(tool_btn(u"▲"_s, u"Move up"_s, [board, id] { board->move_block(id, -1); }));
    lay->addWidget(tool_btn(u"▼"_s, u"Move down"_s, [board, id] { board->move_block(id, 1); }));
    if (is_open)
        lay->addWidget(tool_btn(u"✓ Close"_s, u"Keep the text and close the block"_s, [this] { _close(); }, 86));
    else
        lay->addWidget(tool_btn(u"✎ Open"_s, u"Open the block to rewrite its text"_s, [board, id] { board->open_block(id); }, 86));
    lay->addWidget(tool_btn(u"↩ Back"_s, u"Take it out of the notes — back to the changes on the left"_s,
                            [board, id] { board->send_back(id); }, 80));
}

void BlockCard::_close() { board->close_block(bid, editor->toPlainText()); }

void BlockCard::mouseDoubleClickEvent(QMouseEvent *)
{
    if (board->open_id != bid)
        board->open_block(bid);
}

SectionHead::SectionHead(NotesBoard *board, const Value &sec, const QStringList &extra_cats)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 10, 8, 2);
    lay->setSpacing(8);
    const QString name = sec.get(u"name"_s)->str();
    auto *chip = new QLabel();
    chip->setFixedSize(14, 14);
    chip->setStyleSheet(u"background: %1; border-radius: 3px;"_s.arg(cat_color(name)));
    lay->addWidget(chip);
    NoWheelCombo *cb = cat_combo(name, extra_cats);
    cb->setFixedWidth(260);
    QFont f(cb->font());
    f.setBold(true);
    cb->setFont(f);
    cb->setToolTip(u"The heading — pick or type another name to retitle the whole category"_s);
    connect(cb, &QComboBox::activated, this, [board, name, cb](int) { board->rename_section(name, cb->currentText()); });
    connect(cb->lineEdit(), &QLineEdit::editingFinished, this,
            [board, name, cb] { board->rename_section(name, cb->currentText()); });
    lay->addWidget(cb);
    const qsizetype n = static_cast<qsizetype>(sec.get(u"blocks"_s)->arr().size());
    lay->addWidget(label(u"%1 block%2"_s.arg(n).arg(n == 1 ? QString() : u"s"_s), u"faint"_s, false));
    lay->addStretch(1);
    lay->addWidget(tool_btn(u"▲"_s, u"Move the whole category up"_s, [board, name] { board->move_section(name, -1); }));
    lay->addWidget(tool_btn(u"▼"_s, u"Move the whole category down"_s, [board, name] { board->move_section(name, 1); }));
}

// ---------------------------------------------------------------------- the board
NotesBoard::NotesBoard(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 6, 0);
    lay->setSpacing(5);
    marker = new QFrame(this);
    marker->setStyleSheet(u"background: %1; border-radius: 2px;"_s.arg(theme::GOLD));
    marker->hide();
}

static pyjson::Array &blocks_of(const Value &s) { return const_cast<Value &>(*s.get(u"blocks"_s)).arr(); }

void NotesBoard::set_sections(const Value &sections_)
{
    sections = sections_;
    long long top = 0;
    for (const Value &s : secs())
        for (const Value &b : blocks_of(s))
            top = std::max(top, iget(b, u"id"_s).value_or(0));
    next_id = top + 1;
    for (Value &s : secs())
        for (Value &b : blocks_of(s))
            if (!truthy(b, u"id"_s))
                b.obj().insert(u"id"_s, Value(next_id++));
    rebuild();
}

std::optional<std::pair<qsizetype, qsizetype>> NotesBoard::_find(long long bid)
{
    for (size_t si = 0; si < secs().size(); ++si) {
        const auto &bl = blocks_of(secs()[si]);
        for (size_t bi = 0; bi < bl.size(); ++bi)
            if (iget(bl[bi], u"id"_s) == bid)
                return std::pair{static_cast<qsizetype>(si), static_cast<qsizetype>(bi)};
    }
    return std::nullopt;
}

Value NotesBoard::_section(const QString &name, bool create)
{
    for (const Value &s : secs())
        if (py::lower(sget(s, u"name"_s)) == py::lower(name))
            return s;
    if (!create)
        return Value();
    Value s = obj({{u"name"_s, name}, {u"blocks"_s, pyjson::Array{}}});
    const qsizetype last = ORDER.size();
    auto rank = [&](const QString &n) {
        const qsizetype i = ORDER.indexOf(n);
        return i < 0 ? last : i;
    };
    const qsizetype mine = rank(name);
    size_t at = secs().size();
    for (size_t i = 0; i < secs().size(); ++i)
        if (rank(sget(secs()[i], u"name"_s)) > mine) {
            at = i;
            break;
        }
    secs().insert(secs().begin() + static_cast<std::ptrdiff_t>(at), s);
    return s;
}

void NotesBoard::_done()
{
    pyjson::Array keep;
    for (const Value &s : secs())
        if (truthy(s, u"blocks"_s))
            keep.push_back(s);
    secs() = std::move(keep);                       // (in place: the draft holds the same list)
    rebuild();
    Q_EMIT edited();
}

Value NotesBoard::add_block(const QString &text, const QString &category, const Value &entry, bool quiet)
{
    Value b = obj({{u"id"_s, Value(next_id)}, {u"text"_s, text}, {u"entry"_s, entry}});
    ++next_id;
    blocks_of(_section(category)).push_back(b);
    if (!quiet)
        _done();
    return b;
}

void NotesBoard::move_block(long long bid, int delta)
{
    const auto at = _find(bid);
    if (!at)
        return;
    const auto [si, bi] = *at;
    auto &blocks = blocks_of(secs()[static_cast<size_t>(si)]);
    const qsizetype nb = bi + delta;
    const qsizetype ns = static_cast<qsizetype>(secs().size());
    if (0 <= nb && nb < static_cast<qsizetype>(blocks.size())) {
        std::swap(blocks[static_cast<size_t>(bi)], blocks[static_cast<size_t>(nb)]);
    } else if (0 <= si + delta && si + delta < ns) {   // over the edge: into the neighbouring category
        Value b = blocks[static_cast<size_t>(bi)];
        blocks.erase(blocks.begin() + bi);
        auto &other = blocks_of(secs()[static_cast<size_t>(si + delta)]);
        other.insert(delta < 0 ? other.end() : other.begin(), b);
    } else {
        return;
    }
    _done();
}

void NotesBoard::place_block(long long bid, qsizetype si, qsizetype index)
{
    const auto at = _find(bid);
    if (!at || !(0 <= si && si < static_cast<qsizetype>(secs().size())))
        return;
    const auto [osi, obi] = *at;
    auto &src = blocks_of(secs()[static_cast<size_t>(osi)]);
    Value b = src[static_cast<size_t>(obi)];
    src.erase(src.begin() + obi);
    if (osi == si && obi < index)
        --index;
    auto &target = blocks_of(secs()[static_cast<size_t>(si)]);
    const qsizetype i = std::max<qsizetype>(0, std::min<qsizetype>(index, static_cast<qsizetype>(target.size())));
    target.insert(target.begin() + i, b);
    _done();
}

void NotesBoard::set_category(long long bid, const QString &name_)
{
    const QString name = join_ws(name_);
    const auto at = _find(bid);
    if (!at || name.isEmpty() || py::lower(sget(secs()[static_cast<size_t>(at->first)], u"name"_s)) == py::lower(name))
        return;
    auto &src = blocks_of(secs()[static_cast<size_t>(at->first)]);
    Value b = src[static_cast<size_t>(at->second)];
    src.erase(src.begin() + at->second);
    blocks_of(_section(name)).push_back(b);
    _done();
}

void NotesBoard::move_section(const QString &name, int delta)
{
    qsizetype i = -1;
    for (size_t k = 0; k < secs().size(); ++k)
        if (sget(secs()[k], u"name"_s) == name) {
            i = static_cast<qsizetype>(k);
            break;
        }
    if (i < 0 || !(0 <= i + delta && i + delta < static_cast<qsizetype>(secs().size())))
        return;
    std::swap(secs()[static_cast<size_t>(i)], secs()[static_cast<size_t>(i + delta)]);
    _done();
}

void NotesBoard::rename_section(const QString &name, const QString &new__)
{
    const QString new_ = join_ws(new__);
    Value s = _section(name, false);
    if (s.is_null() || new_.isEmpty() || new_ == name)
        return;
    Value other = _section(new_, false);
    if (!other.is_null() && &other.obj() != &s.obj()) {   // there already: the two become one
        auto &ob = blocks_of(other);
        for (const Value &b : blocks_of(s))
            ob.push_back(b);
        s.obj().insert(u"blocks"_s, Value(pyjson::Array{}));
    } else {
        s.obj().insert(u"name"_s, Value(new_));
    }
    _done();
}

void NotesBoard::open_block(long long bid)
{
    _keep_open();
    open_id = bid;
    rebuild();
    for (const Row &r : _rows)
        if (auto *c = dynamic_cast<BlockCard *>(r.w); c && c->bid == bid)
            c->editor->setFocus();
}

void NotesBoard::_keep_open()
{
    // the text of the block that is open now is kept (another one is opened / a block moves)
    for (const Row &r : _rows) {
        auto *c = dynamic_cast<BlockCard *>(r.w);
        if (c && open_id == c->bid && c->editor) {
            const auto at = _find(c->bid);
            const QString text = py::strip(c->editor->toPlainText());
            if (at && !text.isEmpty())
                blocks_of(secs()[static_cast<size_t>(at->first)])[static_cast<size_t>(at->second)].obj().insert(
                    u"text"_s, Value(text));
        }
    }
}

void NotesBoard::close_block(long long bid, const QString &text_)
{
    const auto at = _find(bid);
    const QString text = py::strip(text_);
    if (at && !text.isEmpty())
        blocks_of(secs()[static_cast<size_t>(at->first)])[static_cast<size_t>(at->second)].obj().insert(u"text"_s,
                                                                                                      Value(text));
    open_id.reset();
    _done();
}

void NotesBoard::send_back(long long bid)
{
    const auto at = _find(bid);
    if (!at)
        return;
    Value s = secs()[static_cast<size_t>(at->first)];
    auto &bl = blocks_of(s);
    Value b = bl[static_cast<size_t>(at->second)];
    bl.erase(bl.begin() + at->second);
    if (open_id == bid)
        open_id.reset();
    b.obj().insert(u"category"_s, Value(sget(s, u"name"_s)));
    Q_EMIT sentBack(b);
    _done();
}

void NotesBoard::rebuild()
{
    _keep_open();
    QScrollBar *bar = scroll ? scroll->verticalScrollBar() : nullptr;
    const int pos = bar ? bar->value() : 0;
    while (lay->count()) {
        QLayoutItem *item = lay->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    _rows.clear();
    QStringList extra;
    for (const Value &s : secs())
        extra << sget(s, u"name"_s);
    if (std::none_of(secs().begin(), secs().end(), [](const Value &s) { return truthy(s, u"blocks"_s); })) {
        QLabel *hint = label(u"The notes are empty. Double-click a change on the left — or select several and press "
                             u"“Add to the notes ▸” — and it becomes a block here, under its category. Blocks can be "
                             u"moved (▲ ▼ or dragged by ⠿), given another category, opened to rewrite and sent back."_s,
                             u"faint"_s);
        lay->addWidget(hint);
    }
    for (size_t si = 0; si < secs().size(); ++si) {
        const Value s = secs()[si];
        if (!truthy(s, u"blocks"_s))
            continue;
        auto *h = new SectionHead(this, s, extra);
        lay->addWidget(h);
        _rows.push_back({h, static_cast<qsizetype>(si), std::nullopt});
        const auto bl = blocks_of(s);              // (a copy: the cards may change the list)
        for (size_t bi = 0; bi < bl.size(); ++bi) {
            auto *c = new BlockCard(this, s, bl[bi], extra);
            lay->addWidget(c);
            _rows.push_back({c, static_cast<qsizetype>(si), static_cast<qsizetype>(bi)});
        }
    }
    lay->addStretch(1);
    if (bar)
        QTimer::singleShot(0, bar, [bar, pos] { bar->setValue(pos); });
}

std::optional<std::tuple<qsizetype, qsizetype, int>> NotesBoard::_target(int y)
{
    // (section index, block index, y of the line) a block dropped at y goes to
    std::optional<std::tuple<qsizetype, qsizetype, int>> last;
    for (const Row &r : _rows) {
        const QRect g = r.w->geometry();
        if (y < g.center().y()) {
            if (!r.bi) {                              // above a heading: the end of the category before
                if (last)
                    return std::tuple{std::get<0>(*last), std::get<1>(*last) + 1, std::get<2>(*last)};
                return std::tuple{r.si, qsizetype(0), g.bottom() + 2};
            }
            return std::tuple{r.si, *r.bi, g.top() - 3};
        }
        last = std::tuple{r.si, r.bi ? *r.bi : qsizetype(-1), g.bottom() + 2};
    }
    if (last)
        return std::tuple{std::get<0>(*last), std::get<1>(*last) + 1, std::get<2>(*last)};
    return std::nullopt;
}

void NotesBoard::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(MIME))
        e->acceptProposedAction();
}

void NotesBoard::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(MIME))
        return;
    const int y = e->position().toPoint().y();
    const auto t = _target(y);
    if (t) {
        marker->setGeometry(4, std::get<2>(*t) - 2, width() - 14, 4);
        marker->show();
        marker->raise();
    }
    if (scroll) {                                     // near the edge: the list scrolls along
        QScrollBar *bar = scroll->verticalScrollBar();
        const int vy = y - bar->value();
        if (vy < 40)
            bar->setValue(bar->value() - 24);
        else if (vy > scroll->viewport()->height() - 40)
            bar->setValue(bar->value() + 24);
    }
    e->acceptProposedAction();
}

void NotesBoard::dragLeaveEvent(QDragLeaveEvent *) { marker->hide(); }

void NotesBoard::dropEvent(QDropEvent *e)
{
    marker->hide();
    if (!e->mimeData()->hasFormat(MIME))
        return;
    bool ok = false;
    const long long bid = py::strip(QString::fromUtf8(e->mimeData()->data(MIME))).toLongLong(&ok);
    if (!ok)
        return;
    const auto t = _target(e->position().toPoint().y());
    if (t) {
        e->acceptProposedAction();
        _drop = std::tuple{bid, std::get<0>(*t), std::get<1>(*t)};
    }
}

void NotesBoard::apply_drop()
{
    const auto d = _drop;
    _drop.reset();
    marker->hide();
    if (d)
        place_block(std::get<0>(*d), std::get<1>(*d), std::get<2>(*d));
}

// ---------------------------------------------------------------------- the tab
ChangelogWorkspace::ChangelogWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    const QString mname = !mod.name().isEmpty() ? mod.name() : u"mod"_s;
    dir = pypath::join(app_dir(), {u"changelogs"_s, QString(mname).replace(py::rx(u"[^\\w .-]"_s), u"_"_s)});
    data = obj({{u"entries"_s, pyjson::Array{}}, {u"draft"_s, pyjson::Object{}}});
    _load();
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);

    // --- left: what was changed
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *s = new Section();
    s->lay->addWidget(label(u"Changes"_s, u"title"_s));
    s->lay->addWidget(label(u"Collected at every Apply: what changed in the mod's files and your edits in the other "
                            u"tabs, each with the category of the editor function it comes from. Double-click one — "
                            u"or select several and “Add to the notes ▸” — and it becomes a block on the right. "
                            u"Edits that are not applied yet are listed in grey and can't be added."_s,
                            u"faint"_s));
    auto *row = new QHBoxLayout();
    hide_used = new QCheckBox(u"Hide the ones already in the notes"_s);
    hide_used->setChecked(true);
    connect(hide_used, &QCheckBox::toggled, this, [this](bool) { _fill(); });
    row->addWidget(hide_used, 1);
    steps_box = new QCheckBox(u"Show every single edit step"_s);
    steps_box->setToolTip(u"Also the single steps behind the changes of the script files (they repeat them)"_s);
    steps_box->setChecked(false);
    connect(steps_box, &QCheckBox::toggled, this, [this](bool) { _fill(); });
    row->addWidget(steps_box);
    s->lay->addLayout(row);
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderLabels({u"Change"_s, u"Category"_s});
    lst->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"changelogList"_s, {std::nullopt, 160}, true);
    connect(lst, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) { _add_items({it}); });
    s->lay->addWidget(lst, 1);
    row = new QHBoxLayout();
    auto *add = new QPushButton(u"Add to the notes ▸"_s);
    connect(add, &QPushButton::clicked, this, [this] { _add_items(lst->selectedItems()); });
    row->addWidget(add, 1);
    auto *allb = new QPushButton(u"Add all not in the notes ▸"_s);
    connect(allb, &QPushButton::clicked, this, [this] { _add_all(); });
    row->addWidget(allb, 1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    move_to = new NoWheelCombo();
    move_to->addItem(u"Under its own category"_s, QVariant());
    for (const QString &sec : CATEGORIES)
        move_to->addItem(u"Under "_s + sec, sec);
    move_to->insertSeparator(1 + FUNCTION_CATS.size());
    move_to->setToolTip(u"Where “Add to the notes” puts the blocks"_s);
    move_to->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(move_to, 1);
    auto *forget = new QPushButton(u"Forget the selected"_s);
    forget->setToolTip(u"Take them off this list for good (the notes and the exported files stay as they are)"_s);
    connect(forget, &QPushButton::clicked, this, [this] { _forget(); });
    row->addWidget(forget);
    s->lay->addLayout(row);
    ll->addWidget(s, 1);
    s = new Section();
    s->lay->addWidget(label(u"A block of your own"_s, u"sub"_s));
    row = new QHBoxLayout();
    own = new QLineEdit();
    own->setPlaceholderText(u"e.g. Fixed the Iron Coast event firing twice"_s);
    connect(own, &QLineEdit::returnPressed, this, [this] { _add_own(); });
    row->addWidget(own, 3);
    own_sec = new NoWheelCombo();
    own_sec->addItems(RELEASE_CATS + FUNCTION_CATS);
    own_sec->insertSeparator(RELEASE_CATS.size());
    own_sec->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(own_sec, 1);
    auto *b = new QPushButton(u"＋ Add"_s);
    connect(b, &QPushButton::clicked, this, [this] { _add_own(); });
    row->addWidget(b);
    s->lay->addLayout(row);
    folder_note = label(QString(), u"faint"_s);
    s->lay->addWidget(folder_note);
    auto *ob = new QPushButton(u"Open the folder with the exported changes"_s);
    connect(ob, &QPushButton::clicked, this, [this] { _open_folder(); });
    s->lay->addWidget(ob);
    ll->addWidget(s);
    split->addWidget(left);

    // --- right: the notes
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    s = new Section();
    s->lay->addWidget(label(u"Release notes"_s, u"title"_s));
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Version"_s, u"sub"_s, false), 0, 0);
    version = new QLineEdit();
    connect(version, &QLineEdit::textEdited, this, [this](const QString &v) { _version_edited(v); });
    g->addWidget(version, 0, 1);
    g->addWidget(label(u"Title"_s, u"sub"_s, false), 1, 0);
    title = new QLineEdit();
    connect(title, &QLineEdit::textEdited, this, [this](const QString &) { _notes_changed(); });
    g->addWidget(title, 1, 1);
    s->lay->addLayout(g);
    row = new QHBoxLayout();
    const std::vector<std::pair<QString, std::function<void()>>> btns = {
        {u"Copy title"_s, [this] { _copy_title(); }}, {u"Copy notes"_s, [this] { _copy_notes(); }},
        {u"Save as .md"_s, [this] { _save_md(); }}, {u"Start the next version"_s, [this] { _next(); }}};
    for (const auto &[text, fn] : btns) {
        auto *bt = new QPushButton(text);
        connect(bt, &QPushButton::clicked, this, fn);
        row->addWidget(bt, 1);
    }
    s->lay->addLayout(row);
    status = label(QString(), u"faint"_s);
    s->lay->addWidget(status);
    rl->addWidget(s);
    auto *vs = new QSplitter(Qt::Vertical);
    vs->setChildrenCollapsible(false);
    s = new Section();
    s->lay->addWidget(label(u"The notes, block by block"_s, u"sub"_s));
    board = new NotesBoard();
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    sc->setWidget(board);
    board->scroll = sc;
    connect(board, &NotesBoard::edited, this, [this] { _notes_changed(); });
    connect(board, &NotesBoard::sentBack, this, [this](const Value &blk) { _sent_back(blk); });
    s->lay->addWidget(sc, 1);
    vs->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"How it looks (what “Copy notes” copies, as GitHub shows it)"_s, u"sub"_s));
    preview = new QTextBrowser();
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setOpenExternalLinks(false);
    s->lay->addWidget(preview, 1);
    vs->addWidget(s);
    vs->setStretchFactor(0, 3);
    vs->setStretchFactor(1, 1);
    vs->setSizes({620, 240});
    rl->addWidget(vs, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 7);
    split->setSizes({640, 1020});
    _save_timer = new QTimer(this);
    _save_timer->setSingleShot(true);
    _save_timer->setInterval(600);
    connect(_save_timer, &QTimer::timeout, this, [this] { _write(); });
    _show_draft();
}

// ------------------------------------------------------------------ store
void ChangelogWorkspace::_load()
{
    QFile f(pypath::join(dir, u"log.json"_s));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = f.readAll();
    if (raw.startsWith("\xEF\xBB\xBF"))             // (json.load: "Unexpected UTF-8 BOM" — a ValueError)
        return;
    QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = dec(raw);
    if (dec.hasError())                             // (UnicodeDecodeError — a ValueError)
        return;
    text.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');   // (text mode)
    const auto d = pyjson::loads(text);
    if (d && d->is_obj())
        for (const auto &[k, v] : d->obj())
            data.obj().insert(k, v);
}

void ChangelogWorkspace::_write()
{
    try {
        fs::makedirs(dir);
        const QString tmp = pypath::join(dir, u"log.json.tmp"_s);
        QFile f(fs::watched(tmp));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw fs::OSError(os_error(tmp).toStdString(), 13);
        f.write(pyjson::dumps(data, 1, false).replace(u"\n"_s, u"\r\n"_s).toUtf8());   // (text mode: "\r\n")
        f.close();
        fs::replace(tmp, pypath::join(dir, u"log.json"_s));
    } catch (const fs::OSError &e) {
        status->setText(u"⚠ Could not save the changelog: %1"_s.arg(e.text()));
    }
}

QString ChangelogWorkspace::_default_version()
{
    const Value *last = data.get(u"last_version"_s);
    if (last && last->truthy())
        return bump(last->str());
    const auto m = py::re_search(u"(\\d+(?:\\.\\d+)+)\\s*$"_s, pypath::basename(pypath::normpath(mod.root)));
    return m.hasMatch() ? bump(m.captured(1)) : u"1.0.0"_s;
}

QString ChangelogWorkspace::_mod_title()
{
    QString base = pypath::basename(pypath::normpath(mod.root));
    base.replace(py::rx(u"[\\s\\x1c-\\x1f_-]*v?\\d+(?:\\.\\d+)+%1*$"_s.arg(py::PY_S)), QString());
    base = py::strip(base.replace(u'_', u' '));
    return !base.isEmpty() ? base : (!mod.name().isEmpty() ? mod.name() : u"Mod"_s);
}

void ChangelogWorkspace::_show_draft()
{
    pyjson::Object &top = data.obj();
    if (!top.contains(u"draft"_s))
        top.insert(u"draft"_s, Value(pyjson::Object{}));
    Value d = *top.get(u"draft"_s);
    if (!truthy(d, u"version"_s))
        d.obj().insert(u"version"_s, Value(_default_version()));
    if (!truthy(d, u"title"_s))
        d.obj().insert(u"title"_s, Value(u"%1 v%2"_s.arg(_mod_title(), sget(d, u"version"_s))));
    if (!d.obj().contains(u"sections"_s)) {           // an older draft: its text as blocks
        const Value *body = d.get(u"body"_s);
        const QString text = body && body->truthy() ? body->str() : QString();
        d.obj().remove(u"body"_s);
        d.obj().insert(u"sections"_s, Value(parse_body(text)));
    }
    for (const auto &[w, v] : {std::pair{version, sget(d, u"version"_s)}, std::pair{title, sget(d, u"title"_s)}}) {
        w->blockSignals(true);
        w->setText(v);
        w->blockSignals(false);
    }
    board->open_id.reset();
    board->set_sections(*d.get(u"sections"_s));
    folder_note->setText(u"Every Apply is also written to %1 (mod and scenario noted)."_s.arg(
        pypath::join(dir, u"changes"_s)));
    _render();
}

void ChangelogWorkspace::_version_edited(const QString &v)
{
    const QString t = title->text();
    const Value *d = data.get(u"draft"_s);
    const QString old = d ? sget(*d, u"version"_s) : QString();
    if (!old.isEmpty() && t.endsWith(u"v"_s + old))   // the title follows the version
        title->setText(t.left(t.size() - old.size()) + py::strip(v));
    _notes_changed();
}

void ChangelogWorkspace::_notes_changed()
{
    pyjson::Object &top = data.obj();
    if (!top.contains(u"draft"_s))
        top.insert(u"draft"_s, Value(pyjson::Object{}));
    Value d = *top.get(u"draft"_s);
    d.obj().insert(u"version"_s, Value(version->text()));
    d.obj().insert(u"title"_s, Value(title->text()));
    d.obj().insert(u"sections"_s, board->sections);
    _save_timer->start();
    _render();
}

QString ChangelogWorkspace::markdown() { return notes_markdown(board->secs()); }

void ChangelogWorkspace::_render()
{
    preview->setMarkdown(u"## %1\n\n%2"_s.arg(title->text(), markdown().replace(u"\n**"_s, u"\n\n**"_s)));
}

// ------------------------------------------------------------------ the changes
static long long next_entry_id(const pyjson::Array &entries)
{
    long long top = 0;
    for (const Value &e : entries)
        top = std::max(top, iget(e, u"id"_s).value_or(0));
    return top + 1;
}

void ChangelogWorkspace::record(const pyjson::Array &changes, const QString &scenario)
{
    // called at Apply with what it wrote: into the list, and exported as a text file
    const QDateTime now = QDateTime::currentDateTime();
    const QString when = now.toString(u"yyyy-MM-dd HH:mm"_s);
    long long nid = next_entry_id(entries());
    QHash<QString, Value> in_notes;
    for (const Value &s : board->secs())
        for (const Value &b : blocks_of(s))
            if (!truthy(b, u"entry"_s))
                in_notes.insert(sget(b, u"text"_s), b);
    pyjson::Array new_;
    for (const Value &c : changes) {
        Value e(pyjson::Object(c.obj()));
        e.obj().insert(u"id"_s, Value(nid));
        e.obj().insert(u"when"_s, Value(when));
        e.obj().insert(u"scenario"_s, Value(scenario));
        e.obj().insert(u"used"_s, Value(false));
        auto it = in_notes.find(sget(e, u"text"_s));
        if (it != in_notes.end()) {                   // added to the notes before Apply
            it->obj().insert(u"entry"_s, Value(nid));
            e.obj().insert(u"used"_s, Value(true));
        }
        ++nid;
        entries().push_back(e);
        new_.push_back(e);
    }
    _write();
    _export(new_, now, scenario);
    if (isVisible())
        _fill();
}

void ChangelogWorkspace::_export(const pyjson::Array &entries_, const QDateTime &now, const QString &scenario)
{
    // one text file per Apply in changes/ (mod and scenario noted), and the same added to All changes.txt
    if (entries_.empty())
        return;
    const QString root = pypath::basename(pypath::normpath(mod.root));
    const QString mname = mod.name();
    QStringList lines = {u"Mod: %1%2"_s.arg(root, !mname.isEmpty() && mname != root ? u" (%1)"_s.arg(mname) : QString()),
                         u"Scenario: %1"_s.arg(!scenario.isEmpty() ? scenario : u"—"_s),
                         u"Applied: %1"_s.arg(now.toString(u"yyyy-MM-dd HH:mm:ss"_s)), QString()};
    OrderedMap<QString, QStringList> by;
    for (const Value &e : entries_) {
        if (sget(e, u"src"_s) == u"edit" && truthy(e, u"script"_s))
            continue;                                 // the single steps behind a file's change
        by[sget(e, u"section"_s, u"Other"_s)].append(sget(e, u"text"_s));
    }
    QStringList cats;
    for (const auto &kv : by)
        cats << kv.first;
    auto rank = [](const QString &c) {
        const qsizetype i = ORDER.indexOf(c);
        return i < 0 ? ORDER.size() : i;
    };
    std::stable_sort(cats.begin(), cats.end(), [&](const QString &a, const QString &b) { return rank(a) < rank(b); });
    for (const QString &cat : cats) {
        lines << cat;
        for (const QString &t : *by.get(cat))
            lines << u"- "_s + t;
        lines << QString();
    }
    QStringList steps;
    for (const Value &e : entries_)
        if (sget(e, u"src"_s) == u"edit" && truthy(e, u"script"_s))
            steps << sget(e, u"text"_s);
    if (!steps.isEmpty()) {
        lines << u"Edit steps behind the file changes"_s;
        for (const QString &t : steps)
            lines << u"- "_s + t;
        lines << QString();
    }
    const QString text = lines.join(u'\n');
    try {
        const QString d = pypath::join(dir, u"changes"_s);
        fs::makedirs(d);
        const QString name = u"%1  %2  %3.txt"_s.arg(
            now.toString(u"yyyy-MM-dd HH-mm-ss"_s), QString(root).replace(py::rx(u"[^\\w .-]"_s), u"_"_s),
            QString(!scenario.isEmpty() ? scenario : u"scenario"_s).replace(py::rx(u"[^\\w .-]"_s), u"_"_s));
        const QString p1 = pypath::join(d, name);
        QFile f(fs::watched(p1));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw fs::OSError(os_error(p1).toStdString(), 13);
        f.write(text.toUtf8());
        f.close();
        const QString p2 = pypath::join(d, u"All changes.txt"_s);
        QFile a(fs::watched(p2));
        if (!a.open(QIODevice::WriteOnly | QIODevice::Append))
            throw fs::OSError(os_error(p2).toStdString(), 13);
        a.write((QString(78, u'=') + u"\n"_s + text + u"\n"_s).toUtf8());
    } catch (const fs::OSError &e) {
        status->setText(u"⚠ Could not export the changes: %1"_s.arg(e.text()));
    }
}

void ChangelogWorkspace::refresh(document::Document *doc_, const pyjson::Array &pending)
{
    doc = doc_;
    _pending = pending;
    _fill();
}

void ChangelogWorkspace::_fill()
{
    const bool steps = steps_box->isChecked();
    const bool hide = hide_used->isChecked();
    lst->clear();
    _item_data.clear();
    QFont bold(lst->font());
    bold.setBold(true);
    const QBrush faint{QColor(theme::INK_DIM)};
    QFont italic(lst->font());
    italic.setItalic(true);

    auto add_group = [&](const QString &title_, const pyjson::Array &entries_, bool pending) {
        std::vector<std::pair<Value, bool>> shown;
        for (const Value &e : entries_) {
            if (sget(e, u"src"_s) == u"edit" && truthy(e, u"script"_s) && !steps)
                continue;
            const bool used = truthy(e, u"used"_s) && !pending;
            if (used && hide)
                continue;
            shown.emplace_back(e, used);
        }
        if (shown.empty())
            return;
        auto *gi = new QTreeWidgetItem(QStringList{title_, QString()});
        gi->setFont(0, bold);
        gi->setForeground(0, QBrush(QColor(theme::GOLD)));
        gi->setFlags(gi->flags() & ~Qt::ItemIsSelectable);
        lst->addTopLevelItem(gi);
        for (const auto &[e, used] : shown) {
            const QString text = sget(e, u"text"_s), section = sget(e, u"section"_s, u"Other"_s);
            const QString rel = sget(e, u"rel"_s);
            auto *it = new QTreeWidgetItem(QStringList{(used ? u"✓ "_s : QString()) + text, section});
            it->setToolTip(0, text + (truthy(e, u"rel"_s) ? u"\n"_s + rel : QString()));
            it->setForeground(1, QBrush(QColor(cat_color(section))));
            if (pending) {                             // only what was applied can go into the notes
                it->setFlags(Qt::ItemIsEnabled);
                it->setForeground(0, QBrush(QColor(u"#7d848f"_s)));
                it->setForeground(1, QBrush(QColor(u"#7d848f"_s)));
                it->setFont(0, italic);
                it->setToolTip(0, u"Not applied yet — press Apply first; then it can go into the notes.\n"_s + text);
            } else {
                _item_data.insert(it, e);
            }
            if (used)
                it->setForeground(0, faint);
            gi->addChild(it);
        }
        gi->setExpanded(true);
    };

    if (!_pending.empty())
        add_group(u"Not applied yet — press Apply first, then they can go into the notes"_s, _pending, true);
    pyjson::Array own_;
    for (const Value &e : entries())
        if (sget(e, u"src"_s) == u"own")
            own_.push_back(e);
    if (!own_.empty())
        add_group(u"Your own blocks, taken back"_s, own_, false);
    std::map<std::pair<QString, QString>, pyjson::Array> by_when;   // (walked newest first below)
    for (const Value &e : entries())
        if (sget(e, u"src"_s) != u"own")
            by_when[{sget(e, u"when"_s, u"?"_s), sget(e, u"scenario"_s)}].push_back(e);
    for (auto it = by_when.rbegin(); it != by_when.rend(); ++it) {
        const auto &[when, scen] = it->first;
        const pyjson::Array &es = it->second;
        const long long n = std::count_if(es.begin(), es.end(), [](const Value &e) {
            return !(sget(e, u"src"_s) == u"edit" && truthy(e, u"script"_s));
        });
        add_group(u"Applied %1%2  (%3 change%4)"_s.arg(when, !scen.isEmpty() ? u" · scenario %1"_s.arg(scen) : QString())
                      .arg(n)
                      .arg(n == 1 ? QString() : u"s"_s),
                  es, false);
    }
    if (lst->topLevelItemCount() == 0) {
        auto *gi = new QTreeWidgetItem(QStringList{entries().empty() ? u"Nothing here yet — the changes appear after Apply."_s
                                                                     : u"Everything is in the notes already."_s,
                                                   QString()});
        gi->setFlags(Qt::NoItemFlags);
        lst->addTopLevelItem(gi);
    }
}

void ChangelogWorkspace::_add_items(const QList<QTreeWidgetItem *> &items)
{
    const QVariant fv = move_to->currentData();
    const QString forced = fv.isValid() ? fv.toString() : QString();
    int n = 0;
    for (QTreeWidgetItem *it : items) {
        auto at = _item_data.find(it);
        if (at == _item_data.end() || !at->truthy() || !at->obj().contains(u"id"_s) || it->text(0).startsWith(u"✓"))
            continue;                                 // (pending changes carry no entry: not addable)
        const Value e = *at;
        board->add_block(sget(e, u"text"_s), !forced.isEmpty() ? forced : sget(e, u"section"_s, u"Other"_s),
                         *e.get(u"id"_s), true);
        const auto id = iget(e, u"id"_s);
        for (Value &x : entries())
            if (iget(x, u"id"_s) == id)
                x.obj().insert(u"used"_s, Value(true));
        ++n;
    }
    if (n) {
        pyjson::Array keep;
        for (const Value &x : entries())
            if (!(sget(x, u"src"_s) == u"own" && truthy(x, u"used"_s)))
                keep.push_back(x);
        data.obj().insert(u"entries"_s, Value(std::move(keep)));
        board->_done();
        status->setText(u"%1 block%2 added to the notes."_s.arg(n).arg(n == 1 ? QString() : u"s"_s));
        _fill();
    }
}

void ChangelogWorkspace::_add_all()
{
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = lst->topLevelItem(i);
        for (int j = 0; j < g->childCount(); ++j)
            items << g->child(j);
    }
    _add_items(items);
}

void ChangelogWorkspace::_sent_back(const Value &block)
{
    // a block taken out of the notes: it is in the changes again (its own text kept if it was your own)
    const auto eid = iget(block, u"entry"_s);
    Value *hit = nullptr;
    if (eid)
        for (Value &x : entries())
            if (iget(x, u"id"_s) == eid) {
                hit = &x;
                break;
            }
    if (hit) {
        hit->obj().insert(u"used"_s, Value(false));
    } else if (std::none_of(_pending.begin(), _pending.end(),
                            [&](const Value &p) { return sget(p, u"text"_s) == sget(block, u"text"_s); })) {
        const long long nid = next_entry_id(entries());
        entries().push_back(obj({{u"id"_s, Value(nid)}, {u"text"_s, sget(block, u"text"_s)},
                                 {u"section"_s, sget(block, u"category"_s, u"Other"_s)}, {u"src"_s, u"own"_s},
                                 {u"used"_s, false}}));
    }
    status->setText(u"The block is back in the changes on the left."_s);
    QTimer::singleShot(0, this, [this] { _fill(); });
}

void ChangelogWorkspace::_forget()
{
    QSet<long long> ids;
    for (QTreeWidgetItem *it : lst->selectedItems()) {
        auto at = _item_data.find(it);
        if (at != _item_data.end())
            if (const auto id = iget(*at, u"id"_s))
                ids.insert(*id);
    }
    if (!ids.isEmpty()) {
        pyjson::Array keep;
        for (const Value &e : entries()) {
            const auto id = iget(e, u"id"_s);
            if (!(id && ids.contains(*id)))
                keep.push_back(e);
        }
        data.obj().insert(u"entries"_s, Value(std::move(keep)));
        _write();
        _fill();
    }
}

void ChangelogWorkspace::_add_own()
{
    const QString t = join_ws(own->text());
    if (t.isEmpty())
        return;
    board->add_block(t, own_sec->currentText());
    own->clear();
}

// ------------------------------------------------------------------ out
void ChangelogWorkspace::_open_folder()
{
    const QString d = pypath::join(dir, u"changes"_s);
    try {
        fs::makedirs(d);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(d)))
            throw fs::OSError(u"[WinError 2] The system cannot find the file specified: %1"_s.arg(py::repr_str(d)).toStdString(), 2);
    } catch (const fs::OSError &e) {
        status->setText(u"⚠ Could not open %1: %2"_s.arg(d, e.text()));
    }
}

void ChangelogWorkspace::_copy_title()
{
    QGuiApplication::clipboard()->setText(title->text());
    status->setText(u"Title copied — paste it as the GitHub release title."_s);
}

void ChangelogWorkspace::_copy_notes()
{
    board->_keep_open();
    QGuiApplication::clipboard()->setText(markdown());
    status->setText(u"Notes copied — paste them into the GitHub release description."_s);
}

OptStr ChangelogWorkspace::_save_md()
{
    board->_keep_open();
    const QString t = py::strip(title->text());
    const QString name = QString(!t.isEmpty() ? t : u"notes"_s).replace(py::rx(u"[^\\w .-]"_s), u"_"_s) + u".md"_s;
    const QString path = pypath::join(dir, name);
    try {
        fs::makedirs(dir);
        QFile f(fs::watched(path));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw fs::OSError(os_error(path).toStdString(), 13);
        f.write(u"# %1\n\n%2"_s.arg(title->text(), markdown()).toUtf8());
    } catch (const fs::OSError &e) {
        status->setText(u"⚠ Could not save: %1"_s.arg(e.text()));
        return std::nullopt;
    }
    status->setText(u"Saved: %1"_s.arg(path));
    Q_EMIT message(u"Changelog saved: "_s + path);
    return name;
}

void ChangelogWorkspace::_next()
{
    // the notes saved as .md; new empty notes for the next version
    const bool any = std::any_of(board->secs().begin(), board->secs().end(),
                                 [](const Value &s) { return truthy(s, u"blocks"_s); });
    if (any && !_save_md())
        return;
    const QString v = py::strip(version->text());
    data.obj().insert(u"last_version"_s, Value(v));
    const QString nv = bump(v);
    data.obj().insert(u"draft"_s, obj({{u"version"_s, nv}, {u"title"_s, u"%1 v%2"_s.arg(_mod_title(), nv)},
                                       {u"sections"_s, pyjson::Array{}}}));
    _write();
    _show_draft();
    _fill();
    status->setText(u"v%1 saved; the notes for v%2 start empty."_s.arg(v, nv));
}

}  // namespace changelog
