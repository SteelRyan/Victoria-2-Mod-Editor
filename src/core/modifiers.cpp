// modifiers.py — the event modifiers (common/event_modifiers.txt). Line by line.
#include "core/modifiers.h"
#include "core/events.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/scriptstore.h"

#include <QSet>
#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace modifiers {

using script::Node;
using script::Nodes;

const QStringList FILE_PARTS = {u"common"_s, u"event_modifiers.txt"_s};
const std::vector<QStringList> ATTR_SOURCES = [] {
    std::vector<QStringList> v;
    for (const QString &f : {u"event_modifiers.txt"_s, u"static_modifiers.txt"_s, u"triggered_modifiers.txt"_s,
                             u"crime.txt"_s, u"national_focus.txt"_s, u"terrain.txt"_s, u"nationalvalues.txt"_s})
        v.push_back({u"common"_s, f});
    return v;
}();
const QString REL = FILE_PARTS.join(u'/');

bool name_ok(const QString &s0)
{
    QString s = s0;
    if (s.endsWith(u'\n'))                       // ($ also before a last "\n")
        s.chop(1);
    if (s.isEmpty())
        return false;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const char16_t u = s[i].unicode();
        const bool letter = (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || u == u'_';
        if (!(letter || (i > 0 && u >= u'0' && u <= u'9')))
            return false;
    }
    return true;
}

bool num_ok(const QString &s0)
{
    QString s = s0;
    if (s.endsWith(u'\n'))
        s.chop(1);
    qsizetype i = 0;
    const qsizetype n = s.size();
    auto digits = [&] {
        const qsizetype a = i;
        while (i < n && s[i].category() == QChar::Number_DecimalDigit)
            ++i;
        return i > a;
    };
    if (i < n && s[i] == u'-')
        ++i;
    if (!digits())
        return false;
    if (i < n && s[i] == u'.') {
        ++i;
        if (!digits())
            return false;
    }
    return i == n;
}

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }

Attrs _attrs_of(const Nodes &nodes)
{
    Attrs out;
    for (const Node &c : nodes)
        if (has(c) && !c.is_block)
            out.emplace_back(c.key, c.value);
    return out;
}

static const Node *top_of(const Nodes &nodes)
{
    for (const Node &n : nodes)
        if (has(n) && n.is_block)
            return &n;
    return nullptr;
}

Attrs block_attrs(const QString &text)
{
    const Nodes nodes = script::parse(text);
    const Node *top = top_of(nodes);
    return top ? _attrs_of(top->kids) : Attrs();
}

static qsizetype rfind_nl(const QString &t, qsizetype end)   // t.rfind("\n", 0, end)
{
    if (end < 0)
        end = std::max<qsizetype>(0, t.size() + end);
    if (end <= 0)
        return -1;
    return t.lastIndexOf(u'\n', std::min(end, t.size()) - 1);
}

QString set_attr(const QString &text, const QString &key, const QString &value)
{
    // The block's text with `key = value` (changed where it is, or a new line before the closing brace).
    const Nodes nodes = script::parse(text);
    const Node *top = top_of(nodes);
    if (!top)
        throw std::runtime_error("AttributeError: 'NoneType' object has no attribute 'value'");
    for (const Node &c : top->kids)
        if (c.has_key && c.key == key && !c.is_block)
            return text.left(c.vstart) + value + text.mid(c.vend);
    // (Python's slices: a block without its closing brace has close = -1, "all but the last character")
    const qsizetype close = text.lastIndexOf(u'}');
    const qsizetype ls = py::rfind(text, u'\n', close) + 1;
    if (!py::strip(py::slice(text, ls, close)).isEmpty())   // "... }" on one line
        return py::head(text, close) + u"\n\t%1 = %2\n"_s.arg(key, value) + py::tail(text, close);
    return py::head(text, ls) + u"\t%1 = %2\n"_s.arg(key, value) + py::tail(text, ls);
}

QString del_attr(const QString &text, const QString &key)
{
    const Nodes nodes = script::parse(text);
    const Node *top = top_of(nodes);
    if (!top)
        throw std::runtime_error("AttributeError: 'NoneType' object has no attribute 'value'");
    for (const Node &c : top->kids) {
        if (c.has_key && c.key == key && !c.is_block) {
            const qsizetype ls = rfind_nl(text, c.kstart) + 1;
            qsizetype le = text.indexOf(u'\n', c.vend);
            le = le < 0 ? text.size() : le + 1;
            if (py::strip(text.mid(ls, c.kstart - ls)).isEmpty() && py::strip(text.mid(c.vend, le - c.vend)).isEmpty())
                return text.left(ls) + text.mid(le);         // the whole line
            return text.left(c.kstart) + text.mid(c.vend);
        }
    }
    return text;
}

QString new_block(const QString &name) { return name + u" = {\n\ticon = 1\n}\n"_s; }

ModifierDB::ModifierDB(moddata::Mod &mod_, const std::optional<QString> &text_) : mod(mod_)
{
    path = mod.p(FILE_PARTS);
    if (text_)
        text = *text_;
    else
        text = fs::exists(path) ? script::read_text(path).replace(u"\r\n"_s, u"\n"_s) : QString();
    for (const Node &n : script::parse(text))
        if (has(n) && n.is_block && !entries.contains(n.key)) {
            entries.insert(n.key, ModEntry{text.mid(n.kstart, n.vend - n.kstart), {n.kstart, n.vend}});
            order << n.key;
        }
    if (!mod.modifier_attrs) {
        QSet<QString> attrs;
        for (const QStringList &parts : ATTR_SOURCES) {
            const QString p = mod.p(parts);
            if (fs::exists(p)) {
                try {
                    _collect(script::parse(script::read_text(p)), attrs);
                } catch (...) {
                }
            }
        }
        attrs.remove(u"icon"_s);
        QStringList l(attrs.begin(), attrs.end());
        std::sort(l.begin(), l.end());
        mod.modifier_attrs = l;
    }
    known_attrs = *mod.modifier_attrs;
}

void ModifierDB::_collect(const Nodes &nodes, QSet<QString> &out)
{
    for (const Node &n : nodes) {
        if (has(n) && n.is_block)
            _collect(n.kids, out);
        else if (has(n) && !n.is_block && num_ok(py::strip(n.value)))
            out.insert(n.key);
    }
}

ModifierDB::Uses ModifierDB::used_by(const events::EventDB &db) const
{
    Uses out;
    std::function<void(const events::Entry *, const Nodes &)> walk = [&](const events::Entry *e, const Nodes &nodes) {
        for (const Node &c : nodes) {
            const QString k = c.has_key ? c.key : QString();
            if (c.has_key && (k == u"add_country_modifier" || k == u"add_province_modifier") && c.is_block) {
                for (const Node &x : c.kids)
                    if (x.has_key && x.key == u"name" && !x.is_block) {
                        if (!x.value.isEmpty())
                            out[py::strip(x.value)].emplace_back(e, u"adds"_s);
                        break;
                    }
            } else if (c.has_key && (k == u"remove_country_modifier" || k == u"remove_province_modifier") && !c.is_block) {
                out[py::strip(c.value)].emplace_back(e, u"removes"_s);
            } else if (c.has_key && (k == u"has_country_modifier" || k == u"has_province_modifier") && !c.is_block) {
                out[py::strip(c.value)].emplace_back(e, u"checks"_s);
            }
            if (c.is_block)
                walk(e, c.kids);
        }
    };
    static const Nodes NONE;
    for (const events::EntryP &e : db.entries)
        walk(e.get(), (e->node && e->node->is_block) ? e->node->kids : NONE);
    for (auto &[k, lst] : out) {                 // list(dict.fromkeys(v)): each (entry, how) once
        std::vector<std::pair<const events::Entry *, QString>> u;
        for (const auto &x : lst)
            if (std::find(u.begin(), u.end(), x) == u.end())
                u.push_back(x);
        lst = u;
    }
    return out;
}

ModifierDB &ModifierEdits::db()
{
    const QString t = store.text(REL);
    if (!_text || *_text != t) {
        _text = t;
        _db = std::make_unique<ModifierDB>(mod, t);
    }
    return *_db;
}

QStringList ModifierEdits::names() { return db().order; }
bool ModifierEdits::exists(const QString &name) { return db().entries.contains(name); }
QString ModifierEdits::text(const QString &name) { return db().entries.get(name)->text; }
std::pair<qsizetype, qsizetype> ModifierEdits::span(const QString &name) { return db().entries.get(name)->span; }

void ModifierEdits::set(const QString &name, const QString &block)
{
    const auto [a, b] = db().entries.get(name)->span;
    const QString full = store.text(REL);
    store.set_text(REL, full.left(a) + block + full.mid(b));
}

void ModifierEdits::add(const QString &name)
{
    QString full = store.text(REL);
    while (full.endsWith(u'\n'))
        full.chop(1);
    store.set_text(REL, full + u"\n\n"_s + new_block(name));
}

void ModifierEdits::remove(const QString &name)
{
    auto [a, b] = db().entries.get(name)->span;
    const QString text = store.text(REL);
    const qsizetype ls = rfind_nl(text, a) + 1;
    qsizetype le = text.indexOf(u'\n', b);
    le = le < 0 ? text.size() : le + 1;
    if (py::strip(text.mid(ls, a - ls)).isEmpty() && py::strip(text.mid(b, le - b)).isEmpty()) {
        a = ls;
        b = le;
    }
    store.set_text(REL, text.left(a) + text.mid(b));
}

Attrs ModifierEdits::attrs(const QString &name) { return block_attrs(text(name)); }

}  // namespace modifiers
