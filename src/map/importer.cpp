// importer.py — Source, Importer, the plan and apply. Line by line.
#include "map/importer.h"
#include "core/blocks.h"
#include "core/countryhist.h"
#include "core/entryview_data.h"
#include "core/eventtools.h"
#include "core/fs.h"
#include "core/issuepanel_data.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "map/iconstrips_data.h"
#include "map/imgio.h"
#include "map/moreviews_data.h"
#include "map/nationextras_data.h"

#include <QFileInfo>
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pypath::join;

namespace importer {

static const auto UCP = QRegularExpression::UseUnicodePropertiesOption;
const QRegularExpression WORD(uR"re("([^"\n]*)"|([A-Za-z_][A-Za-z0-9_.\-]*)|(\d+))re"_s, UCP);
const QRegularExpression CALLS(uR"((?:country_event|province_event)\s*=\s*(?:\{[^{}]*?\bid\s*=\s*)?(\d+))"_s, UCP);
const QStringList TEXT_EXT = {u".txt"_s, u".lua"_s, u".gui"_s, u".gfx"_s, u".map"_s};
static const QRegularExpression COMMENT(u"#[^\\n]*"_s);
static const QRegularExpression COLOUR_CODE(u"§.|§"_s);

static Cat C(const QString &id, const QString &title, const QString &how, const QString &rel, const QString &folder = {},
             const QStringList &base = {}, const QStringList &skip = {}, const QString &about = {})
{
    Cat c;
    c.id = id;
    c.title = title;
    c.how = how;
    c.rel = rel;
    c.folder = folder;
    c.base = base;
    c.skip = skip;
    c.about = about;
    return c;
}
static Cat S(const QString &id, const QString &title, const QString &about)
{
    Cat c;
    c.id = id;
    c.title = title;
    c.special = true;
    c.about = about;
    return c;
}
const std::vector<Cat> CATS = {
    S(u"events"_s, u"Events"_s, u"Events, by their id. “＋ its chain” takes every event it calls or is called by along."_s),
    S(u"decisions"_s, u"Decisions"_s, u"Decisions, with the events they fire."_s),
    S(u"nations"_s, u"Nations (tags)"_s,
      u"A nation of the other mod: its tag, colours, parties, history, flags and names. Only its files (it exists in "
      u"the game, you place it on the map yourself) — or with its land as the other mod has it (only when both mods "
      u"have the same map)."_s),
    C(u"modifiers"_s, u"Event modifiers"_s, u"top"_s, u"common/event_modifiers.txt"_s),
    C(u"triggered"_s, u"Triggered modifiers"_s, u"top"_s, u"common/triggered_modifiers.txt"_s),
    C(u"governments"_s, u"Government forms"_s, u"top"_s, u"common/governments.txt"_s),
    C(u"nv"_s, u"National values"_s, u"top"_s, u"common/nationalvalues.txt"_s),
    C(u"schools"_s, u"Technology schools"_s, u"inside"_s, u"common/technology.txt"_s, {}, {u"schools"_s}),
    C(u"ideologies"_s, u"Ideologies"_s, u"grouped"_s, u"common/ideologies.txt"_s),
    C(u"issues"_s, u"Reforms & party issues"_s, u"grouped"_s, u"common/issues.txt"_s),
    C(u"cultures"_s, u"Cultures"_s, u"grouped"_s, u"common/cultures.txt"_s),
    C(u"religions"_s, u"Religions"_s, u"grouped"_s, u"common/religion.txt"_s),
    C(u"cb"_s, u"War goals"_s, u"top"_s, u"common/cb_types.txt"_s, {}, {}, {u"peace_order"_s}),
    C(u"traits"_s, u"Leader traits"_s, u"grouped"_s, u"common/traits.txt"_s),
    C(u"rebels"_s, u"Rebel types"_s, u"top"_s, u"common/rebel_types.txt"_s),
    C(u"crimes"_s, u"Crimes"_s, u"top"_s, u"common/crime.txt"_s),
    C(u"focus"_s, u"National focuses"_s, u"grouped"_s, u"common/national_focus.txt"_s),
    C(u"units"_s, u"Unit types"_s, u"folder"_s, {}, u"units"_s),
    C(u"poptypes"_s, u"Pop types"_s, u"files"_s, {}, u"poptypes"_s),
    C(u"inventions"_s, u"Inventions"_s, u"folder"_s, {}, u"inventions"_s),
    C(u"techs"_s, u"Technologies"_s, u"folder"_s, {}, u"technologies"_s, {}, {},
      u"A technology only shows in the game when its area is one of the mod's technology folders "
      u"(common/technology.txt) — the import warns when it isn't."_s),
    C(u"goods"_s, u"Goods"_s, u"grouped"_s, u"common/goods.txt"_s, {}, {}, {},
      u"A good alone does nothing: it also needs something that makes it (Factories & production types) and pops "
      u"that buy it."_s),
    C(u"buildings"_s, u"Buildings"_s, u"top"_s, u"common/buildings.txt"_s),
    C(u"production"_s, u"Factories & production types"_s, u"top"_s, u"common/production_types.txt"_s),
    S(u"settings"_s, u"Game settings"_s, u"The values of common/defines.lua that differ between the two mods."_s),
    S(u"files"_s, u"Other files"_s,
      u"Whole files of the other mod — pictures, music, interface, news, anything the lists above don't cover. A file "
      u"your mod already has is replaced (the old one is backed up on Apply)."_s),
};
const Cat &cat_of(const QString &id)
{
    for (const Cat &c : CATS)
        if (c.id == id)
            return c;
    throw std::out_of_range(("no import kind " + id).toStdString());
}
const QSet<QString> HARD = {u"events"_s, u"modifiers"_s, u"triggered"_s, u"cb"_s, u"nations"_s, u"cultures"_s,
                            u"religions"_s, u"governments"_s, u"nv"_s, u"crimes"_s, u"rebels"_s, u"traits"_s,
                            u"focus"_s, u"schools"_s};
// the icon strips: (sprite, the line that picks a picture — or "order": by the place in the file)
static const OrderedMap<QString, std::pair<QString, QString>> CAT_STRIP = {
    {u"nv"_s, {u"GFX_politics_nationalvalue"_s, u"order"_s}}, {u"focus"_s, {u"GFX_province_natfocus_strip"_s, u"icon"_s}},
    {u"modifiers"_s, {u"GFX_modifier_icons"_s, u"icon"_s}}, {u"triggered"_s, {u"GFX_modifier_icons"_s, u"icon"_s}},
    {u"religions"_s, {u"GFX_icon_religion"_s, u"icon"_s}}, {u"crimes"_s, {u"GFX_crime_strip"_s, u"icon"_s}},
    {u"cb"_s, {u"GFX_diplo_wargoal_icons"_s, u"sprite_index"_s}}, {u"units"_s, {u"GFX_unit_strip"_s, u"icon"_s}},
    {u"rebels"_s, {u"GFX_politics_rebel_strip"_s, u"icon"_s}}, {u"poptypes"_s, {u"GFX_pops_small"_s, u"sprite"_s}}};

static QString no_comments(const QString &s) { return QString(s).remove(COMMENT); }
static QStringList findall1(const QRegularExpression &re, const QString &text)   // findall of a one-group pattern
{
    QStringList out;
    for (auto it = re.globalMatch(text); it.hasNext();)
        out << it.next().captured(1);
    return out;
}
// re.sub with a function of the match
static QString sub(const QRegularExpression &re, const QString &text, const std::function<QString(const QRegularExpressionMatch &)> &fn)
{
    QString out;
    qsizetype pos = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += text.mid(pos, m.capturedStart() - pos) + fn(m);
        pos = m.capturedEnd();
    }
    return out + text.mid(pos);
}
static QString strip_nl(const QString &s)          // .strip("\n")
{
    qsizetype a = 0, b = s.size();
    while (a < b && s[a] == u'\n')
        ++a;
    while (b > a && s[b - 1] == u'\n')
        --b;
    return s.mid(a, b - a);
}
static bool py_isupper(const QString &s)
{
    bool cased = false;
    for (const QChar c : s) {
        if (c.isLower() || c.isTitleCase())
            return false;
        cased = cased || c.isUpper();
    }
    return cased;
}
static QString basename_of(const QString &rel) { return rel.mid(rel.lastIndexOf(u'/') + 1); }
static QString stem_of(const QString &rel)
{
    const QString b = basename_of(rel);
    const qsizetype dot = b.lastIndexOf(u'.');
    return dot > 0 ? b.left(dot) : b;
}

QString norm(const QString &s)
{
    // A script text without its comments and spacing (what counts when two are compared).
    return py::split(no_comments(s)).join(u' ');
}

// ---------------------------------------------------------------------------------------------- Source
Source::Source(const QString &folder, const std::optional<QString> &base)
{
    root = pypath::abspath(folder);
    const QString parent = pypath::dirname(root);
    std::optional<QString> title;
    if (fs::isdir(parent))
        for (const QString &fn : fs::listdir(parent))
            if (py::lower(fn).endsWith(u".mod")) {
                const moddata::ModFileValues v = moddata::_mod_file_values(join(parent, fn));
                QString p = v.keys.value(u"path"_s);
                p.replace(u'\\', u'/');
                while (p.endsWith(u'/'))
                    p.chop(1);
                if (py::lower(pypath::basename(p)) == py::lower(pypath::basename(root))) {
                    replace = v.replace_path;
                    const QString nm = v.keys.value(u"name"_s);
                    title = nm.isEmpty() ? pypath::basename(root) : nm;
                }
            }
    name = title.value_or(pypath::basename(root));
    const bool is_base = base && !base->isEmpty() && pypath::normcase(pypath::abspath(*base)) == pypath::normcase(root);
    mod = std::make_unique<moddata::Mod>(root, is_base ? std::nullopt : base, name, replace);
    try {
        mod->loc = mod->_load_localisation();
    } catch (const std::exception &) {
        mod->loc = {};
    }
    store = std::make_unique<scriptstore::ScriptStore>(*mod);
}

QString Source::text(const QString &rel)
{
    try {
        return store->text(rel);
    } catch (const std::exception &) {
        return {};
    }
}

std::optional<QString> Source::loc(const QString &key) const
{
    const QString *t = mod->loc.get(key);
    return t ? std::optional<QString>(*t) : std::nullopt;
}

QStringList Source::tag_keys(const QString &tag)
{
    // The localisation keys that begin with a nation's tag (TAG, TAG_ADJ, TAG_monarchy …).
    if (!_by_tag) {
        _by_tag.emplace();
        for (const auto &kv : mod->loc) {
            const QString &k = kv.first;
            if (k.size() >= 3 && (k.size() == 3 || k[3] == u'_') && py_isupper(k.left(3)))
                (*_by_tag)[k.left(3)] << k;
        }
    }
    return _by_tag->value(tag);
}

// ---------------------------------------------------------------------------------------------- Importer
Importer::Importer(moddata::Mod &m, document::Document &d, Source &s)
    : mod(m), doc(d), src(s), store(d.map<scriptstore::ScriptStore>(u"scripts"_s))
{
}

OptStr Importer::tloc(const QString &key)
{
    OptStr t;
    if (const store::StoreP *lp = doc.maps.get(u"loc"_s))
        t = dynamic_cast<localisation::LocEdits &>(**lp).text(key);
    else if (const QString *x = mod.loc.get(key))
        t = *x;
    return t && !t->isEmpty() ? t : std::nullopt;
}

QString Importer::nice(const QString &key, const QStringList &alts) const
{
    for (const QString &k : QStringList{key} + alts) {
        const auto t = src.loc(k);
        if (t && !t->isEmpty())
            return py::strip(QString(*t).remove(COLOUR_CODE));
    }
    return QString(key).replace(u'_', u' ');
}

bool Importer::map_same()
{
    // Both mods have the same map (a province number means the same place).
    if (!_map_same) {
        const QString a = src.mod->p({u"map"_s, u"definition.csv"_s}), b = mod.p({u"map"_s, u"definition.csv"_s});
        if (!fs::isfile(a) || !fs::isfile(b))
            _map_same = false;
        else
            _map_same = norm(script::read_text(a).replace(u";"_s, u" ; "_s)) == norm(script::read_text(b).replace(u";"_s, u" ; "_s));
    }
    return *_map_same;
}

std::vector<Importer::Block> Importer::_blocks(const Cat &cat, const moddata::Mod &m, scriptstore::ScriptStore &st)
{
    // [(group, key, rel, path, text)] of a kind's things in a mod.
    std::vector<Block> out;
    auto text_of = [&](const QString &rel) {
        try {
            return st.text(rel);
        } catch (const std::exception &) {
            return QString();
        }
    };
    if (cat.how == u"files") {
        for (const QString &rel : entryview::_files(m, &st, cat.folder))
            out.push_back({std::nullopt, stem_of(rel), rel, {}, text_of(rel)});
        return out;
    }
    if (cat.how == u"folder") {
        for (const QString &rel : entryview::_files(m, &st, cat.folder)) {
            const QString text = text_of(rel);
            for (const script::Node &n : script::parse(text))
                if (n.has_key && !n.key.isEmpty() && n.is_block)
                    out.push_back({basename_of(rel), n.key, rel, {n.key}, text.mid(n.vstart + 1, n.vend - n.vstart - 2)});
        }
        return out;
    }
    const QString rel = cat.rel;
    const QString text = text_of(rel);
    const script::Nodes nodes = script::parse(text);
    auto inner = [&](const script::Node &n) { return text.mid(n.vstart + 1, n.vend - n.vstart - 2); };
    if (cat.how == u"inside") {
        if (const script::Node *node = blocks::find(nodes, cat.base))
            for (const script::Node &c : node->kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    out.push_back({std::nullopt, c.key, rel, cat.base + QStringList{c.key}, inner(c)});
    } else if (cat.how == u"grouped") {
        for (const script::Node &g : nodes)
            if (g.has_key && !g.key.isEmpty() && g.is_block)
                for (const script::Node &c : g.kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        out.push_back({g.key, c.key, rel, {g.key, c.key}, inner(c)});
    } else {
        for (const script::Node &n : nodes)
            if (n.has_key && !n.key.isEmpty() && n.is_block && !cat.skip.contains(n.key))
                out.push_back({std::nullopt, n.key, rel, {n.key}, inner(n)});
    }
    return out;
}

const OrderedMap<QString, TEntry> &Importer::tindex(const QString &cid)
{
    // {key: (rel, path, text)} of what the mod itself has of a kind.
    auto it = _tindex.find(cid);
    if (it != _tindex.end())
        return it->second;
    OrderedMap<QString, TEntry> idx;
    const Cat &cat = cat_of(cid);
    if (cid == u"events") {
        for (const QString &rel : entryview::_files(mod, &store, u"events"_s)) {
            const QString text = store.text(rel);
            for (const script::Node &n : script::parse(text))
                if (n.has_key && eventtools::EVENT_KEYS.contains(n.key) && n.is_block) {
                    std::optional<QString> eid;
                    for (const script::Node &c : n.kids)
                        if (c.has_key && c.key == u"id" && !c.is_block) {
                            eid = py::strip(c.value);
                            break;
                        }
                    if (eid && !eid->isEmpty() && !idx.contains(*eid))
                        idx.insert(*eid, {rel, {}, text.mid(n.kstart, n.vend - n.kstart)});
                }
        }
    } else if (cid == u"decisions") {
        for (const QString &rel : entryview::_files(mod, &store, u"decisions"_s)) {
            const QString text = store.text(rel);
            for (const script::Node &top : script::parse(text))
                if (top.has_key && top.key == u"political_decisions" && top.is_block)
                    for (const script::Node &n : top.kids)
                        if (n.has_key && !n.key.isEmpty() && n.is_block && !idx.contains(n.key))
                            idx.insert(n.key, {rel, {u"political_decisions"_s, n.key}, text.mid(n.vstart + 1, n.vend - n.vstart - 2)});
        }
    } else if (cid == u"nations") {
        for (const auto &[t, c] : mod.countries)
            idx.insert(t, {c.file.isEmpty() ? std::nullopt : OptStr(c.file), {}, QString()});
    } else if (!cat.special) {
        for (const Block &b : _blocks(cat, mod, store))
            if (!idx.contains(b.key))
                idx.insert(b.key, {b.rel, b.path, b.text});
    }
    return _tindex.emplace(cid, idx).first->second;
}

void Importer::forget()
{
    // After an import (or any edit): the mod's own lists are read again.
    _tindex.clear();
    _items.clear();
    _names.reset();
}

const std::vector<ItemP> &Importer::items(const QString &cid)
{
    auto it = _items.find(cid);
    if (it != _items.end())
        return it->second;
    std::vector<ItemP> v = cid == u"events"      ? _scan_events(cid)
                           : cid == u"decisions" ? _scan_decisions(cid)
                           : cid == u"nations"   ? _scan_nations(cid)
                           : cid == u"settings"  ? _scan_settings(cid)
                           : cid == u"files"     ? _scan_files(cid)
                                                 : _scan_blocks(cid);
    return _items.emplace(cid, std::move(v)).first->second;
}

ItemP Importer::find(const QString &cid, const QString &key)
{
    for (const ItemP &it : items(cid))
        if (it->key == key)
            return it;
    return nullptr;
}

QString Importer::_status(const QString &cid, const QString &key, const QString &text)
{
    const TEntry *t = tindex(cid).get(key);
    if (!t)
        return u"new"_s;
    return norm(t->text) == norm(text) ? u"same"_s : u"differs"_s;
}

std::vector<ItemP> Importer::_scan_blocks(const QString &cid)
{
    const Cat &cat = cat_of(cid);
    std::vector<ItemP> out;
    QSet<QString> seen;
    for (const Block &b : _blocks(cat, *src.mod, *src.store)) {
        if (seen.contains(b.key))
            continue;
        seen.insert(b.key);
        auto it = std::make_shared<Item>();
        it->cat = cid;
        it->key = b.key;
        it->name = nice(b.key, {b.key + u"_title"_s});
        it->group = b.group;
        it->rel = b.rel;
        it->path = b.path;
        it->text = b.text;
        it->status = _status(cid, b.key, b.text);
        out.push_back(it);
    }
    return out;
}

std::vector<ItemP> Importer::_scan_events(const QString &cid)
{
    std::vector<ItemP> out;
    for (const QString &rel : entryview::_files(*src.mod, src.store.get(), u"events"_s)) {
        const QString text = src.text(rel);
        for (const script::Node &n : script::parse(text)) {
            if (!(n.has_key && eventtools::EVENT_KEYS.contains(n.key) && n.is_block))
                continue;
            OrderedMap<QString, QString> f;
            for (const script::Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    f.insert(c.key, c.value);
            const QString eid = py::strip(f.value(u"id"_s));
            if (eid.isEmpty())
                continue;
            const QString body = text.mid(n.kstart, n.vend - n.kstart);
            grid::PySet<QString> all, me;
            for (const QString &c : findall1(CALLS, no_comments(body)))
                all.add(c);
            me.add(eid);
            const QString title = f.value(u"title"_s);
            QString name = src.loc(title).value_or(QString());
            if (name.isEmpty())
                name = title.contains(u' ') ? title : QString();
            if (name.isEmpty())
                name = u"Event %1"_s.arg(eid);
            auto it = std::make_shared<Item>();
            it->cat = cid;
            it->key = eid;
            it->name = u"%1 — %2"_s.arg(eid, QString(name).remove(COLOUR_CODE));
            it->group = basename_of(rel);
            it->rel = rel;
            it->text = body;
            it->status = _status(cid, eid, body);
            it->calls = all.difference(me);
            it->has_calls = true;
            out.push_back(it);
        }
    }
    return out;
}

std::vector<ItemP> Importer::_scan_decisions(const QString &cid)
{
    std::vector<ItemP> out;
    for (const QString &rel : entryview::_files(*src.mod, src.store.get(), u"decisions"_s)) {
        const QString text = src.text(rel);
        for (const script::Node &top : script::parse(text))
            if (top.has_key && top.key == u"political_decisions" && top.is_block)
                for (const script::Node &n : top.kids)
                    if (n.has_key && !n.key.isEmpty() && n.is_block) {
                        const QString inner = text.mid(n.vstart + 1, n.vend - n.vstart - 2);
                        auto it = std::make_shared<Item>();
                        it->cat = cid;
                        it->key = n.key;
                        it->name = nice(n.key + u"_title"_s, {n.key});
                        it->group = basename_of(rel);
                        it->rel = rel;
                        it->path = QStringList{u"political_decisions"_s, n.key};
                        it->text = inner;
                        it->status = _status(cid, n.key, inner);
                        for (const QString &c : findall1(CALLS, no_comments(inner)))
                            it->calls.add(c);
                        it->has_calls = true;
                        out.push_back(it);
                    }
    }
    return out;
}

std::vector<ItemP> Importer::_scan_nations(const QString &cid)
{
    std::vector<ItemP> out;
    const script::Nodes nodes = script::parse(src.text(u"common/countries.txt"_s));
    const QString hdir = src.mod->p({u"history"_s, u"countries"_s});
    OrderedMap<QString, QString> hist;
    static const QRegularExpression HRE(u"^([A-Za-z0-9]{3})\\s*-"_s, UCP);
    if (fs::isdir(hdir))
        for (const QString &fn : fs::listdir(hdir)) {
            const auto m = HRE.match(fn);
            if (m.hasMatch() && py::lower(fn).endsWith(u".txt") && !hist.contains(m.captured(1).toUpper()))
                hist.insert(m.captured(1).toUpper(), join(hdir, fn));
        }
    bool dyn = false;
    for (const script::Node &n : nodes) {
        if (n.has_key && n.key == u"dynamic_tags") {
            dyn = true;
            continue;
        }
        if (!(n.has_key && !n.key.isEmpty() && !n.is_block && n.key.size() == 3) || dyn)
            continue;
        const QString tag = n.key;
        const QString file = QString(n.value).replace(u'\\', u'/');
        auto it = std::make_shared<Item>();
        it->cat = cid;
        it->key = tag;
        const QString name = src.loc(tag).value_or(QString()).isEmpty() ? tag : *src.loc(tag);
        it->name = u"%1 (%2)"_s.arg(name, tag);
        it->rel = u"common/"_s + file;
        it->text = src.text(u"common/"_s + file);
        it->status = mod.countries.contains(tag) ? u"same"_s : u"new"_s;
        it->file = file;
        if (const QString *h = hist.get(tag))
            it->hist = *h;
        it->xname = name;
        const auto adj = src.loc(tag + u"_ADJ"_s);
        it->adj = adj && !adj->isEmpty() ? *adj : name;
        out.push_back(it);
    }
    std::stable_sort(out.begin(), out.end(), [](const ItemP &a, const ItemP &b) { return py::lower(a->xname) < py::lower(b->xname); });
    return out;
}

OrderedMap<std::pair<QString, QString>, Importer::Row> Importer::_rows(const QString &text)
{
    OrderedMap<std::pair<QString, QString>, Row> out;
    QStringList stack;
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &ln = lines[i];
        const qsizetype dd = ln.indexOf(u"--"_s);
        const QString code = dd >= 0 ? ln.left(dd) : ln;
        const auto ms = entryview::SECT_RE.match(code);
        if (ms.hasMatch()) {
            stack << ms.captured(1);
            continue;
        }
        if (py::strip(code).startsWith(u'}')) {
            if (!stack.isEmpty())
                stack.removeLast();
            continue;
        }
        const auto m = entryview::LINE_RE.match(ln);
        if (m.hasMatch())
            out.insert({stack.isEmpty() ? QString() : stack.last(), m.captured(2)},
                       {py::strip(m.captured(4)), py::strip(m.captured(7)), i});
    }
    return out;
}

std::vector<ItemP> Importer::_scan_settings(const QString &cid)
{
    const auto a = _rows(src.text(u"common/defines.lua"_s)), b = _rows(store.text(u"common/defines.lua"_s));
    std::vector<ItemP> out;
    for (const auto &[sk, r] : a) {
        const Row *mine = b.get(sk);
        if (mine && mine->value != r.value) {
            auto it = std::make_shared<Item>();
            it->cat = cid;
            it->key = sk.first + u'.' + sk.second;
            it->name = u"%1 = %2   (yours: %3)"_s.arg(sk.second, r.value, mine->value);
            it->group = sk.first.isEmpty() ? u"general"_s : sk.first;
            it->rel = u"common/defines.lua"_s;
            it->text = r.value;
            it->status = u"differs"_s;
            it->sec = sk.first;
            it->xkey = sk.second;
            it->note = r.note;
            out.push_back(it);
        }
    }
    return out;
}

std::vector<ItemP> Importer::_scan_files(const QString &cid)
{
    // Every file of the other mod's own folder (not the game's).
    std::vector<ItemP> out;
    const QString root = src.root;
    std::function<void(const QString &)> walk = [&](const QString &d) {
        QStringList dirs, files;
        for (const fs::DirEntry &e : fs::scandir(d))
            (e.is_dir ? dirs : files) << e.name;
        auto by_lower = [](const QString &x, const QString &y) { return py::lower(x) < py::lower(y); };
        std::stable_sort(dirs.begin(), dirs.end(), by_lower);
        std::stable_sort(files.begin(), files.end(), by_lower);
        QString reld = pypath::relpath(d, root).replace(u'\\', u'/');
        if (reld == u".")
            reld.clear();
        for (const QString &fn : files) {
            const QString rel = reld.isEmpty() ? fn : reld + u'/' + fn;
            if (reld.isEmpty())
                continue;                                // (readmes and the like beside the folders)
            const QString mine = join(mod.root, rel.split(u'/'));
            const QString p = join(d, fn);
            QString st;
            if (!fs::isfile(mine))
                st = u"new"_s;
            else
                st = QFileInfo(mine).size() == QFileInfo(p).size() ? u"same"_s : u"differs"_s;
            auto it = std::make_shared<Item>();
            it->cat = cid;
            it->key = rel;
            it->name = fn;
            it->group = reld;
            it->rel = rel;
            it->text = p;
            it->status = st;
            out.push_back(it);
        }
        for (const QString &sd : dirs)
            walk(join(d, sd));
    };
    walk(root);
    return out;
}

grid::PySet<QString> Importer::chain(const QString &eid)
{
    // Every event that hangs together with this one through calls (it calls them, or they call it).
    OrderedMap<QString, ItemP> evs;
    for (const ItemP &it : items(u"events"_s))
        evs.insert(it->key, it);
    OrderedMap<QString, grid::PySet<QString>> back;
    for (const auto &[k, it] : evs)
        for (const QString &c : it->calls.items())
            back[c].add(it->key);
    grid::PySet<QString> seen;
    QStringList todo = {eid};
    while (!todo.isEmpty() && seen.size() < 400) {
        const QString e = todo.takeLast();
        if (seen.contains(e) || !evs.contains(e))
            continue;
        seen.add(e);
        for (const QString &c : (*evs.get(e))->calls.items())
            todo << c;
        if (const auto *b = back.get(e))
            for (const QString &c : b->items())
                todo << c;
    }
    return seen;
}

const Importer::Names &Importer::names()
{
    // ({word: [kinds]} of names the other mod has and this one doesn't, {reform option: its issue} likewise).
    if (!_names) {
        Names n;
        for (const Cat &cat : CATS) {
            if (cat.special && cat.id != u"nations")
                continue;
            const auto &have = tindex(cat.id);
            for (const ItemP &it : items(cat.id))
                if (!have.contains(it->key))
                    n.word[it->key] << cat.id;
        }
        QSet<QString> mine;
        for (const script::Node &g : script::parse(store.text(u"common/issues.txt"_s)))
            if (g.is_block)
                for (const script::Node &i : g.kids)
                    if (i.is_block)
                        for (const script::Node &o : i.kids)
                            if (o.has_key && !o.key.isEmpty() && o.is_block)
                                mine.insert(o.key);
        for (const ItemP &it : items(u"issues"_s))
            for (const script::Node &o : script::parse(it->text))
                if (o.has_key && !o.key.isEmpty() && o.is_block && !mine.contains(o.key))
                    n.opts.insert(o.key, it->key);
        _names = n;
    }
    return *_names;
}

QString Importer::_history(const Item &item)
{
    if (!item.hist || item.hist->isEmpty() || !fs::isfile(*item.hist))
        return {};
    return script::read_text(*item.hist);
}

std::vector<std::tuple<QString, QString, QString>> Importer::needs(const Item &item)
{
    // [(kind, key, why)] of what the thing names that the mod doesn't have.
    if (item.cat == u"files" || item.cat == u"settings" || item.cat == u"cultures" || item.cat == u"religions")
        return {};
    const Names &nm = names();
    std::vector<std::tuple<QString, QString, QString>> out;
    if (item.cat == u"nations") {
        const QString hist = no_comments(_history(item));
        auto need = [&](const QString &cid, const QString &key, const QString &what) {
            if (const QStringList *w = nm.word.get(key); w && w->contains(cid))
                out.emplace_back(cid, key, u"%1 of %2"_s.arg(what, item.name));
        };
        for (const auto &[k, cid, what] : {std::tuple{u"primary_culture"_s, u"cultures"_s, u"the primary culture"_s},
                                           {u"culture"_s, u"cultures"_s, u"an accepted culture"_s},
                                           {u"religion"_s, u"religions"_s, u"the religion"_s},
                                           {u"government"_s, u"governments"_s, u"the government"_s},
                                           {u"nationalvalue"_s, u"nv"_s, u"the national value"_s},
                                           {u"schools"_s, u"schools"_s, u"the technology school"_s}}) {
            const QRegularExpression re(u"(?m)^\\s*%1\\s*=\\s*\"?([\\w.]+)"_s.arg(k), UCP);
            for (const QString &v : findall1(re, hist))
                need(cid, v, what);
        }
        static const QRegularExpression IDEO(u"\\bideology\\s*=\\s*(\\w+)"_s, UCP);
        for (const QString &v : findall1(IDEO, no_comments(item.text)))
            need(u"ideologies"_s, v, u"a party's ideology"_s);
        std::vector<std::tuple<QString, QString, QString>> uniq;   // list(dict.fromkeys(out))
        for (const auto &x : out)
            if (std::find(uniq.begin(), uniq.end(), x) == uniq.end())
                uniq.push_back(x);
        return uniq;
    }
    const QString text = no_comments(item.text);
    grid::PySet<QString> toks;
    for (auto it = WORD.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        toks.add(!m.captured(1).isEmpty() ? m.captured(1) : m.captured(2));
    }
    for (const QString &t : toks.items()) {
        if (const QStringList *w = nm.word.get(t))
            for (const QString &cid : *w)
                if (Id(cid, t) != item.id())
                    out.emplace_back(cid, t, u"named by "_s + item.name);
        if (const QString *iss = nm.opts.get(t); iss && Id(u"issues"_s, *iss) != item.id())
            out.emplace_back(u"issues"_s, *iss, u"its option “%1” is named by %2"_s.arg(t, item.name));
    }
    if (item.cat == u"events" || item.cat == u"decisions") {
        const auto &have = tindex(u"events"_s);
        QSet<QString> srcs;
        for (const ItemP &it : items(u"events"_s))
            srcs.insert(it->key);
        for (const QString &c : item.calls.items())
            if (srcs.contains(c) && !have.contains(c))
                out.emplace_back(u"events"_s, c, u"called by "_s + item.name);
    }
    return out;
}

Plan Importer::plan(const std::vector<Id> &picked0, const std::set<Id> &left_out, const Options &options, const std::set<Id> &also)
{
    Plan plan;
    plan.options = options;
    std::map<Id, ItemP> got;
    std::vector<ItemP> order, todo;
    std::vector<Id> picked = picked0;
    std::sort(picked.begin(), picked.end());
    picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    for (const auto &[cid, key] : picked) {
        const ItemP it = find(cid, key);
        if (it && !got.count(it->id())) {
            got[it->id()] = it;
            order.push_back(it);
            todo.push_back(it);
        }
    }
    OrderedMap<Id, Needed> needed;
    long long guard = 0;
    while (!todo.empty() && guard < 3000) {
        ++guard;
        const ItemP it = todo.front();
        todo.erase(todo.begin());
        for (const auto &[cid, key, why] : needs(*it)) {
            const Id k{cid, key};
            if (got.count(k) || needed.contains(k))
                continue;
            const ItemP dep = find(cid, key);
            if (!dep)
                continue;
            const bool hard = HARD.contains(cid);
            const bool comes = hard ? !left_out.count(k) : also.count(k) > 0;
            needed.insert(k, {dep, why, comes, hard});
            if (comes) {
                got[dep->id()] = dep;
                order.push_back(dep);
                todo.push_back(dep);
            }
        }
    }
    plan.items = order;
    for (const auto &kv : needed)
        plan.needed.push_back(kv.second);
    _decorate(plan);
    return plan;
}

void Importer::_decorate(Plan &plan)
{
    // The texts, pictures, flags, icons and warnings of a plan.
    grid::PySet<QString> provs;
    static const QRegularExpression PIC(uR"re(\bpicture\s*=\s*"?([^"\s}]+)"?)re"_s, UCP);
    static const QRegularExpression PROV1(uR"(\b(?:province_id|capital|owns|controls|move_pop)\s*=\s*(\d+))"_s, UCP);
    static const QRegularExpression PROV2(uR"((?m)^\s*(\d+)\s*=\s*\{)"_s, UCP);
    static const QRegularExpression AREA(uR"(\barea\s*=\s*(\w+))"_s, UCP);
    for (const ItemP &itp : plan.items) {
        const Item &it = *itp;
        if (it.cat == u"files" || it.cat == u"settings")
            continue;
        QString text = it.text;
        if (it.cat == u"nations" && it.hist && !it.hist->isEmpty() && fs::isfile(*it.hist))
            text += u"\n"_s + script::read_text(*it.hist);
        grid::PySet<QString> keys;
        for (const QString &k : {it.key, it.key + u"_title"_s, it.key + u"_desc"_s, u"desc_"_s + it.key, it.key + u"_ADJ"_s})
            keys.add(k);
        bool grouped = false;
        for (const Cat &c : CATS)
            if (c.id == it.cat)
                grouped = c.how == u"grouped";
        if (grouped && it.path && !it.path->isEmpty())
            keys.add((*it.path)[0]);              // its group's name too (a culture group the mod gets new)
        for (auto m = WORD.globalMatch(no_comments(text)); m.hasNext();) {
            const auto x = m.next();
            keys.add(!x.captured(1).isEmpty() ? x.captured(1) : x.captured(2));
        }
        if (it.cat == u"nations")
            for (const QString &k : src.tag_keys(it.key))
                keys.add(k);
        for (const QString &k : keys.items())
            if (!k.isEmpty() && !plan.loc.contains(k)) {
                const auto t = src.loc(k);
                if (t && !t->isEmpty() && !tloc(k))
                    plan.loc.insert(k, *t);
            }
        if (it.cat == u"events" || it.cat == u"decisions") {
            const auto m = PIC.match(text);
            const OptStr name = m.hasMatch() ? OptStr(m.captured(1)) : (it.cat == u"decisions" ? OptStr(it.key) : std::nullopt);
            if (name && !name->isEmpty())
                for (const QString &ext : {u".tga"_s, u".dds"_s}) {
                    const QString rel = u"gfx/pictures/%1/%2%3"_s.arg(it.cat, *name, ext);
                    const QString p = src.mod->p(rel.split(u'/'));
                    if (fs::isfile(p) && !fs::isfile(mod.p(rel.split(u'/'))))
                        plan.files.insert(rel, p);
                }
            if (!map_same()) {
                for (const QString &x : findall1(PROV1, text))
                    provs.add(x);
                for (const QString &x : findall1(PROV2, text))
                    provs.add(x);
            }
        }
        if (it.cat == u"nations") {
            const QString fdir = src.mod->p({u"gfx"_s, u"flags"_s});
            const QString own = join(src.root, {u"gfx"_s, u"flags"_s});
            QStringList ds = {own};
            if (pypath::normcase(fdir) != pypath::normcase(own))
                ds << fdir;
            for (const QString &d : ds)
                if (fs::isdir(d))
                    for (const QString &fn : fs::listdir(d))
                        if (fn.toUpper().startsWith(it.key) && py::lower(fn).endsWith(u".tga") &&
                            (fn.size() == 7 || (fn.size() > 3 && fn[3] == u'_')) && !plan.flags.contains(fn) &&
                            !fs::isfile(mod.p({u"gfx"_s, u"flags"_s, fn})))
                            plan.flags.insert(fn, join(d, fn));
            if (it.status == u"new" && !map_same())
                plan.notes << u"%1 comes without a capital (the other mod's map is another one): pick it on the Nations "
                              u"page once it has land."_s.arg(it.name);
            if (it.status != u"new") {
                plan.warnings << u"%1 is in your mod already — it is left as it is."_s.arg(it.name);
            } else if (plan.options.nation_mode == u"land") {
                if (!map_same()) {
                    plan.warnings << u"%1: the two mods have different maps — it comes with its files only; give it its "
                                     u"land on the Provinces page."_s.arg(it.name);
                } else {
                    plan.land.insert(it.key, _land(it.key));
                    const auto &[own2, cores] = *plan.land.get(it.key);
                    plan.notes << u"%1 gets %2 province%3 (and a core in %4 more) — whoever holds them now loses them."_s.arg(
                        it.name, QString::number(own2.size()), own2.size() == 1 ? QString() : u"s"_s, QString::number(cores.size()));
                }
            }
        }
        if (it.cat == u"techs") {
            const auto m = AREA.match(it.text);
            if (m.hasMatch() && !store.text(u"common/technology.txt"_s).contains(m.captured(1)))
                plan.warnings << u"Technology %1 belongs to the area “%2”, which your mod's technology folders "
                                 u"(common/technology.txt) don't have — it won't show until the area is added there."_s.arg(
                                     it.name, m.captured(1));
        }
        if (it.cat == u"events" && it.status == u"differs") {
            const QString how = plan.options.event_conflict;
            const QString what = how == u"renumber" ? u"the imported one gets a new id"_s
                                 : how == u"replace" ? u"yours is replaced"_s
                                                     : u"it is skipped"_s;
            plan.notes << u"Event %1: your mod has another event with this id — %2."_s.arg(it.key, what);
        } else if (it.status == u"differs" && it.cat != u"events" && it.cat != u"nations" && it.cat != u"settings" &&
                   it.cat != u"files") {
            plan.notes << u"%1 “%2”: your mod has one of this name — it is replaced by the other mod's."_s.arg(cat_of(it.cat).title, it.name);
        }
    }
    if (!provs.empty()) {
        QStringList ps = provs.items().empty() ? QStringList() : QStringList();
        for (const QString &p : provs.items())
            ps << p;
        std::stable_sort(ps.begin(), ps.end(), [](const QString &a, const QString &b) { return py::to_int(a) < py::to_int(b); });
        const qsizetype n = ps.size();
        plan.warnings << u"The two mods have different maps, and what you take names %1 province number%2 (%3%4): there "
                         u"they mean other places — check them after the import."_s.arg(
                             QString::number(n), n == 1 ? QString() : u"s"_s, ps.mid(0, 8).join(u", "_s),
                             n > 8 ? u" …"_s : QString());
    }
    long long soft = 0;
    for (const Needed &nd : plan.needed) {
        if (!nd.comes && nd.hard)
            plan.warnings << u"%1 “%2” is left out though it is %3 — the game will not know it."_s.arg(
                cat_of(nd.dep->cat).title, nd.dep->name, nd.why);
        else if (!nd.comes)
            soft += 1;
    }
    if (soft)
        plan.notes << u"%1 thing%2 named that your mod doesn't have and that would change its rules (technologies, "
                      u"reforms, goods …) — they stay out unless you tick them; a line asking for one is then simply never "
                      u"true."_s.arg(QString::number(soft), soft == 1 ? u" is"_s : u"s are"_s);
    for (const ItemP &it : plan.items)
        if (it->cat == u"nations" && it->status == u"new") {
            const long long n = _adapt_history(_history(*it)).second;
            if (n)
                plan.notes << u"%1: %2 line%3 of its history (technologies, inventions, reforms your mod doesn't have) %4 "
                              u"left out."_s.arg(it->name, QString::number(n), n == 1 ? QString() : u"s"_s,
                                                 n == 1 ? u"is"_s : u"are"_s);
        }
    if (plan.options.icons) {
        try {
            plan.icon_ops = _icons(plan);
        } catch (const std::exception &e) {      // (the pictures are a bonus: never stop an import)
            plan.icon_ops = {};
            plan.warnings << u"The icons could not be read (%1) — the things come without their pictures."_s.arg(
                QString::fromUtf8(e.what()));
        }
    } else {
        plan.icon_ops = {};
    }
    long long ic = static_cast<long long>(plan.icon_ops.puts.size());
    for (const auto &kv : plan.icon_ops.adds)
        ic += static_cast<long long>(kv.second.size());
    plan.icons = ic;
}

std::pair<QString, long long> Importer::_adapt_history(const QString &text)
{
    // A nation's history made to fit this mod's rules: the lines giving technologies, inventions or reforms the mod
    // doesn't have go. -> (text, how many lines went).
    if (!_rules) {
        Rules r;
        for (const QString &cid : {u"techs"_s, u"inventions"_s, u"issues"_s})
            for (const ItemP &it : items(cid))
                r.src_keys.insert(it->key);
        for (const QString &cid : {u"techs"_s, u"inventions"_s})
            for (const auto &kv : tindex(cid))
                r.mine.insert(kv.first);
        for (const auto &kv : tindex(u"issues"_s))
            r.issues.insert(kv.first);
        for (const script::Node &g : script::parse(store.text(u"common/issues.txt"_s)))
            if (g.is_block)
                for (const script::Node &i : g.kids)
                    if (i.is_block)
                        for (const script::Node &o : i.kids)
                            if (o.has_key && !o.key.isEmpty() && o.is_block)
                                r.opts.insert(o.key);
        _rules = r;
    }
    static const QRegularExpression LN(uR"re(^\s*([A-Za-z_][\w.]*)\s*=\s*"?([\w.]+)"?\s*(#.*)?\r?$)re"_s, UCP);
    QStringList out;
    long long n = 0;
    for (const QString &ln : text.split(u'\n')) {
        const auto m = LN.match(ln);
        if (m.hasMatch() && _rules->src_keys.contains(m.captured(1))) {
            const QString k = m.captured(1), v = m.captured(2);
            if ((!_rules->mine.contains(k) && !_rules->issues.contains(k)) || (_rules->issues.contains(k) && !_rules->opts.contains(v))) {
                ++n;
                continue;
            }
        }
        out << ln;
    }
    return {out.join(u'\n'), n};
}

std::pair<std::vector<long long>, std::vector<long long>> Importer::_land(const QString &tag)
{
    if (!_hist)
        _hist = std::make_unique<history::ProvinceHistory>(*src.mod);
    std::vector<long long> own, cores;
    for (const auto &kv : _hist->files) {
        const long long pid = kv.first;
        const moddata::Province *pr = mod.provinces.get(pid);
        if (!pr || pr->sea)
            continue;
        const history::ProvState st = _hist->state_at(pid, doc.date);
        if (st.owner == OptStr(tag))
            own.push_back(pid);
        else if (st.cores.contains(tag))
            cores.push_back(pid);
    }
    return {own, cores};
}

IconOps Importer::_icons(const Plan &plan)
{
    // The pictures of the things that pick one from an icon strip.
    iconstrips::Strips sst(*src.mod, src.store.get(), nullptr);
    iconstrips::Strips &tst = iconstrips::strips_for(mod, doc);
    const iconstrips::Sprites ssp = sst.sprites(), tsp = tst.sprites();
    IconOps ops;
    auto frame = [](iconstrips::Strips &strips, const iconstrips::Sprites &sps, const QString &sprite,
                    std::optional<long long> n) -> std::optional<QImage> {
        const iconstrips::Sprite *sp = sps.get(sprite);
        if (!sp || !n)
            return std::nullopt;
        const auto img = strips.image(sprite, &sps);
        if (!img || !(1 <= *n && *n <= sp->frames))
            return std::nullopt;
        const long long fw = img->width() / sp->frames;
        return img->copy(int((*n - 1) * fw), 0, int(fw), img->height()).convertToFormat(QImage::Format_ARGB32);
    };
    OrderedMap<QString, long long> count;
    for (const ItemP &itp : plan.items) {
        const Item &it = *itp;
        const auto *cs = CAT_STRIP.get(it.cat);
        if (!cs || it.status != u"new")
            continue;
        const auto &[sprite, how] = *cs;
        if (!ssp.contains(sprite) || !tsp.contains(sprite))
            continue;
        const iconstrips::Use *use = iconstrips::use_of(sprite);
        const long long first = use ? use->first : 1;
        const long long tframes = tsp.get(sprite)->frames;
        if (how == u"order") {
            long long idx = -1;
            const auto &all = items(it.cat);
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i]->key == it.key) {
                    idx = static_cast<long long>(i);
                    break;
                }
            const auto pic = frame(sst, ssp, sprite, idx + first);
            if (!pic)
                continue;
            if (!count.contains(sprite))
                count.insert(sprite, static_cast<long long>(tindex(it.cat).size()));
            count[sprite] += 1;
            const long long at = count.value(sprite) + first - 1;
            const long long added = ops.adds.contains(sprite) ? static_cast<long long>(ops.adds.get(sprite)->size()) : 0;
            if (at <= tframes + added && at <= tframes)
                ops.puts.emplace_back(sprite, at, *pic);
            else
                ops.adds[sprite].push_back({*pic, {}});
            continue;
        }
        const QRegularExpression re(u"\\b%1\\s*=\\s*(\\d+)"_s.arg(how), UCP);
        const auto m = re.match(it.text);
        if (!m.hasMatch())
            continue;
        const auto n = iconstrips::pic_of(use, py::to_int(m.captured(1)));   // (the picture: crimes count from 0)
        const auto pic = frame(sst, ssp, sprite, n);
        if (!pic)
            continue;
        const auto mine = frame(tst, tsp, sprite, n);
        if (mine && mine->size() == pic->size() && *mine == *pic)
            continue;                                    // the mod's strip has the same picture at this number
        // the other mod's own pictures for the sister strips (the small / mini ones), not this one squeezed
        OrderedMap<QString, QImage> sisters;
        const QStringList fam = tst.family(sprite);
        for (qsizetype i = 1; i < fam.size(); ++i)
            if (ssp.contains(fam[i]))
                if (const auto f = frame(sst, ssp, fam[i], n))
                    sisters.insert(fam[i], *f);
        ops.adds[sprite].push_back({*pic, sisters});
        const long long fresh = tframes + static_cast<long long>(ops.adds.get(sprite)->size());
        ops.texts[it.id()] = it.text.left(m.capturedStart(1)) + QString::number(iconstrips::value_of(use, fresh)) +
                             it.text.mid(m.capturedEnd(1));
    }
    return ops;
}

QString Importer::_orig_text(const QString &rel)
{
    try {
        return store.text(rel);
    } catch (const std::exception &) {
        return {};
    }
}

QString Importer::apply(const Plan &plan, const OptStr &label)
{
    // Queue the plan as one undoable step. -> a few words on what was done.
    const Options &opt = plan.options;
    OrderedMap<QString, QString> texts;                  // rel -> the mod's file with the things put in
    auto cur = [&](const QString &rel) -> QString {
        if (!texts.contains(rel))
            texts.insert(rel, _orig_text(rel));
        return *texts.get(rel);
    };
    const IconOps &icons = plan.icon_ops;
    OrderedMap<QString, QString> loc = plan.loc;
    QSet<QString> coming_tags;
    for (const auto &kv : mod.countries)
        coming_tags.insert(kv.first);
    for (const ItemP &it : plan.items)
        if (it->cat == u"nations")
            coming_tags.insert(it->key);
    OrderedMap<QString, long long> done;
    std::vector<ItemP> nations;
    OrderedMap<QString, QString> files;                  // rel -> ("file", path)
    // --- events: ids the mod uses already
    OrderedMap<QString, QString> renum;
    std::vector<ItemP> ev_items;
    for (const ItemP &it : plan.items)
        if (it->cat == u"events")
            ev_items.push_back(it);
    if (!ev_items.empty()) {
        const auto used = eventtools::event_ids(mod, store);
        QStringList used_ids;
        for (const auto &kv : used)
            used_ids << kv.first;
        QSet<QString> taken;
        for (const ItemP &it : ev_items)
            if (it->status == u"differs" && opt.event_conflict == u"renumber") {
                const QString rel = u"events/"_s + it->group.value_or(QString());
                QStringList near_;
                for (const auto &[i, lst] : used)
                    if (std::any_of(lst.begin(), lst.end(), [&](const auto &pl) { return std::get<0>(pl) == rel; }))
                        near_ << i;
                QSet<QString> tk = taken;
                for (const auto &kv : renum)
                    tk.insert(kv.second);
                renum.insert(it->key, eventtools::free_id(used_ids, near_, tk));
                taken.insert(*renum.get(it->key));
            }
    }
    auto renumbered = [&](QString text) {
        for (const auto &[old, nw] : renum) {
            const QRegularExpression a(u"(\\bid\\s*=\\s*)%1\\b"_s.arg(old), UCP);
            const QRegularExpression b(u"((?:country_event|province_event)\\s*=\\s*)%1\\b"_s.arg(old), UCP);
            const QRegularExpression c(u"\\b(EVT[A-Za-z_]*?)%1\\b"_s.arg(old), UCP);
            const auto keep1 = [&](const QRegularExpressionMatch &m) { return m.captured(1) + nw; };
            text = sub(a, text, keep1);
            text = sub(b, text, keep1);
            text = sub(c, text, keep1);
        }
        return text;
    };
    if (!renum.isEmpty()) {
        QStringList keys;
        for (const auto &kv : loc)
            keys << kv.first;
        for (const QString &k : keys)
            for (const auto &[old, nw] : renum) {
                const QRegularExpression re(u"^(EVT[A-Za-z_]*?)%1$"_s.arg(old), UCP);
                const auto m = re.match(k);
                if (m.hasMatch()) {
                    const QString t = *loc.get(k);
                    loc.remove(k);
                    loc.insert(m.captured(1) + nw, t);
                    break;
                }
            }
    }
    for (const ItemP &itp : plan.items) {
        const Item &it = *itp;
        const QString &cid = it.cat;
        if (cid == u"events") {
            if (it.status == u"same" || (it.status == u"differs" && opt.event_conflict == u"skip"))
                continue;
            const QString body = renumbered(it.text);
            const QString rel = u"events/"_s + it.group.value_or(QString());
            bool replaced = false;
            if (it.status == u"differs" && opt.event_conflict == u"replace") {
                const QString trel = *tindex(u"events"_s).get(it.key)->rel;
                const QString t = cur(trel);
                for (const script::Node &n : script::parse(t))
                    if (n.has_key && eventtools::EVENT_KEYS.contains(n.key) && n.is_block &&
                        std::any_of(n.kids.begin(), n.kids.end(), [&](const script::Node &c) {
                            return !c.is_block && c.has_key && c.key == u"id" && py::strip(c.value) == it.key;
                        })) {
                        texts.insert(trel, t.left(n.kstart) + body + t.mid(n.vend));
                        done[cid] += 1;
                        replaced = true;
                        break;
                    }
            }
            if (replaced)
                continue;
            texts.insert(rel, eventtools::add_event(cur(rel), body));
        } else if (cid == u"decisions") {
            if (it.status == u"same")
                continue;
            const TEntry *t = tindex(cid).get(it.key);
            const QString rel = t ? *t->rel : u"decisions/"_s + it.group.value_or(QString());
            QString text = cur(rel);
            if (!blocks::find(script::parse(text), {u"political_decisions"_s})) {
                QString head = text;
                while (head.endsWith(u'\n'))
                    head.chop(1);
                text = (py::strip(text).isEmpty() ? QString() : head + u"\n\n"_s) + u"political_decisions = {\n}\n"_s;
            }
            const QString inner = u"\n"_s + issuepanel::reindent(strip_nl(renumbered(it.text)), 2) + u"\n\t"_s;
            texts.insert(rel, blocks::set_block(text, {u"political_decisions"_s, it.key}, inner));
        } else if (cid == u"nations") {
            if (it.status == u"new")
                nations.push_back(itp);
            continue;
        } else if (cid == u"settings") {
            const QString rel = u"common/defines.lua"_s;
            QStringList lines = cur(rel).split(u'\n');
            const auto rows = _rows(cur(rel));
            const Row *row = rows.get({it.sec, it.xkey});
            if (!row)
                continue;
            const auto m = entryview::LINE_RE.match(lines[row->line]);
            const QString g5 = m.captured(5).isEmpty() ? u","_s : m.captured(5);
            lines[row->line] = m.captured(1) + m.captured(2) + m.captured(3) + it.text + g5 + m.captured(6);
            texts.insert(rel, lines.join(u'\n'));
        } else if (cid == u"files") {
            bool is_text = false;
            for (const QString &e : TEXT_EXT)
                is_text = is_text || py::lower(it.key).endsWith(e);
            if (is_text) {
                if (!fs::isfile(it.text))
                    continue;
                texts.insert(it.key, script::read_text(it.text).replace(u"\r\n"_s, u"\n"_s));
            } else {
                files.insert(it.key, it.text);
            }
        } else {
            if (it.status == u"same")
                continue;
            const Cat &cat = cat_of(cid);
            const auto ib = icons.texts.find(it.id());
            QString body = ib != icons.texts.end() ? ib->second : it.text;
            const TEntry *t = tindex(cid).get(it.key);
            if (cat.how == u"files") {
                texts.insert(it.rel, body);
                done[cid] += 1;
                continue;
            }
            const QString rel = t ? *t->rel : it.rel;
            const QStringList path = t ? t->path : it.path.value_or(QStringList());
            QString text = cur(rel);
            if (cat.how == u"grouped" && !t && !blocks::find(script::parse(text), path.mid(0, 1))) {
                // a group the mod doesn't have: made, with the group's own lines (a culture group's leaders …)
                QStringList lines;
                const QString stext = src.text(it.rel);
                const script::Nodes sn = script::parse(stext);
                if (const script::Node *g = blocks::find(sn, path.mid(0, 1)))
                    for (const script::Node &c : g->kids)
                        if (c.has_key && !c.key.isEmpty() && !c.is_block && !(c.key == u"union" && !coming_tags.contains(c.value)))
                            lines << stext.mid(c.kstart, c.vend - c.kstart);
                text = blocks::add_block(text, {}, path[0], lines);
            } else if (cat.how == u"inside" && !blocks::find(script::parse(text), path.mid(0, path.size() - 1))) {
                text = blocks::add_block(text, {}, path[0], {});
            }
            if (cid == u"cultures") {                        // (a culture's nation that isn't here: that line goes)
                static const QRegularExpression PRIM(uR"((?m)^[ \t]*primary[ \t]*=[ \t]*(\w+)[ \t]*\r?\n?)"_s, UCP);
                body = sub(PRIM, body, [&](const QRegularExpressionMatch &m) {
                    return coming_tags.contains(m.captured(1)) ? m.captured(0) : QString();
                });
            }
            const QString inner = u"\n"_s + issuepanel::reindent(strip_nl(body), path.size()) + u"\n"_s +
                                  QString(u"\t"_s).repeated(int(path.size() - 1));
            texts.insert(rel, blocks::set_block(text, path, inner));
        }
        done[cid] += 1;
    }
    // --- nations: their own files; the unit colours
    struct Rec {
        QString tag, name, adj;
        moddata::Rgb color;
        QString file;
        QString htext;
    };
    std::vector<Rec> recs;
    QSet<QString> used_files;
    for (const auto &kv : mod.countries)
        used_files.insert(py::lower(kv.second.file));
    static const QRegularExpression CAP(uR"((?m)^[ \t]*capital[ \t]*=[ \t]*(\d+)[^\n]*)"_s, UCP);
    for (const ItemP &itp : nations) {
        const Item &it = *itp;
        const QString &tag = it.key;
        QString file = it.file;
        if (used_files.contains(py::lower(file)) || fs::isfile(mod.p(QStringList{u"common"_s} + file.split(u'/'))))
            file = u"%1_%2.txt"_s.arg(file.left(file.size() - 4), tag);
        used_files.insert(py::lower(file));
        QString htext = _adapt_history(_history(it)).first;
        const auto cap = CAP.match(htext);
        const auto *landp = plan.land.get(tag);
        const std::vector<long long> own = landp ? landp->first : std::vector<long long>();
        if (cap.hasMatch()) {
            const long long c = py::to_int(cap.captured(1));
            if (!mod.provinces.contains(c) || !map_same() || (!own.empty() && std::find(own.begin(), own.end(), c) == own.end()))
                htext = htext.left(cap.capturedStart()) + (own.empty() ? QString() : u"capital = %1"_s.arg(own[0])) +
                        htext.mid(cap.capturedEnd());
        }
        const auto color = script::color_of(script::parse(it.text)).value_or(moddata::Rgb{128, 128, 128});
        texts.insert(u"common/"_s + file, QString(it.text).replace(u"\r\n"_s, u"\n"_s));
        recs.push_back({tag, it.xname, it.adj, color, file, htext});
        const QString stext = src.text(u"common/country_colors.txt"_s);
        const script::Nodes cn = script::parse(stext);
        const script::Node *cc = blocks::find(cn, {tag});
        if (cc && !blocks::find(script::parse(cur(u"common/country_colors.txt"_s)), {tag}))
            texts.insert(u"common/country_colors.txt"_s, blocks::set_block(cur(u"common/country_colors.txt"_s), {tag},
                                                                          stext.mid(cc->vstart + 1, cc->vend - cc->vstart - 2)));
        done[u"nations"_s] += 1;
    }
    for (const auto &[rel, p] : plan.files)
        files.insert(rel, p);
    OrderedMap<QString, QString> keep;
    for (const auto &[rel, t] : texts)
        if (scriptstore::canon(t) != scriptstore::canon(_orig_text(rel)) || store.gone.contains(rel))
            keep.insert(rel, t);
    texts = keep;
    OrderedMap<QString, std::pair<std::vector<long long>, std::vector<long long>>> land;
    for (const auto &[tag, v] : plan.land)
        if (std::any_of(recs.begin(), recs.end(), [&](const Rec &r) { return r.tag == tag; }))
            land.insert(tag, v);
    std::set<long long> hp;
    for (const auto &kv : land) {
        hp.insert(kv.second.first.begin(), kv.second.first.end());
        hp.insert(kv.second.second.begin(), kv.second.second.end());
    }
    const QList<long long> hpids(hp.begin(), hp.end());
    if (texts.isEmpty() && files.isEmpty() && recs.empty() && loc.isEmpty() && plan.flags.isEmpty() && icons.puts.empty() &&
        icons.adds.isEmpty())
        return u"Nothing to bring: your mod has all of it already."_s;
    auto get = [&](const QString &k) -> store::Store * {
        const store::StoreP *p = doc.maps.get(k);
        return p ? p->get() : nullptr;
    };
    auto *nn = dynamic_cast<countryhist::NewNations *>(get(u"new_nations"_s));
    auto *assets = dynamic_cast<moreviews::AssetEdits *>(get(u"assets"_s));
    auto *fe = dynamic_cast<nationextras::FlagEdits *>(get(u"flags"_s));
    auto *lc = dynamic_cast<localisation::LocEdits *>(get(u"loc"_s));
    auto run = [&]() {
        for (const auto &[rel, t] : texts)
            store.set_text(rel, t);
        for (const Rec &r : recs)
            nn->add_imported(r.tag, r.name, r.adj, r.color, r.file, std::nullopt, r.htext);
        if (lc)
            for (const auto &[k, t] : loc)
                lc->set(k, t, std::nullopt);
        if (assets)
            for (const auto &[rel, p] : files)
                assets->fresh.insert(rel, {u"file"_s, {}, p});
        if (fe && !plan.flags.isEmpty()) {
            OrderedMap<QString, QImage> imgs;
            for (const auto &[fn, p] : plan.flags)
                if (const auto img = imgio::read_image(p))
                    imgs.insert(fn, *img);
            fe->put(imgs);
        }
        if (assets && (!icons.puts.empty() || !icons.adds.isEmpty())) {
            iconstrips::Strips &st = iconstrips::strips_for(mod, doc);
            for (const auto &[sprite, at, pic] : icons.puts)
                for (const auto &[r, a] : st.put(sprite, at, pic))
                    assets->fresh.insert(r, a);
            for (const auto &[sprite, pics] : icons.adds) {
                std::vector<iconstrips::Filler> fl;
                for (const auto &[p, sis] : pics)
                    fl.push_back({u"image"_s, 0, p, sis});
                const auto res = st.extend(sprite, fl);
                if (const auto *ok = std::get_if<0>(&res); ok && !ok->first.isEmpty()) {
                    for (const auto &[r, a] : ok->first)
                        assets->fresh.insert(r, a);
                    for (const auto &[r, t] : ok->second)
                        store.set_text(r, t);
                }
            }
        }
        for (const auto &[tag, oc] : land) {
            std::vector<long long> all = oc.first;
            all.insert(all.end(), oc.second.begin(), oc.second.end());
            for (long long pid : all) {
                history::ProvState stt = history::clone_state(doc.prov_state(pid));
                if (std::find(oc.first.begin(), oc.first.end(), pid) != oc.first.end())
                    stt.owner = stt.controller = tag;
                if (!stt.cores.contains(tag))
                    stt.cores << tag;
                doc.prov_want.insert(pid, stt);
            }
        }
    };
    QStringList ws;
    for (const auto &[c, n] : done)
        ws << u"%1 %2"_s.arg(n).arg(py::lower(cat_of(c).title));
    const QString what = ws.isEmpty() ? u"texts and pictures"_s : ws.join(u", "_s);
    doc.edit(label ? *label : u"Import from %1: %2"_s.arg(src.name, what), {}, run, {}, hpids,
             {u"scripts"_s, u"loc"_s, u"assets"_s, u"flags"_s, u"new_nations"_s, u"nations"_s});
    sync_cultures(mod, store, lc);
    forget();
    return u"Imported from %1: %2%3%4%5 — one step (Ctrl+Z takes it all back); Apply writes it."_s.arg(
        src.name, what, loc.isEmpty() ? QString() : u", %1 texts"_s.arg(loc.size()),
        (files.isEmpty() && plan.flags.isEmpty()) ? QString() : u", %1 pictures / flags"_s.arg(files.size() + plan.flags.size()),
        plan.icons ? u", %1 icons"_s.arg(plan.icons) : QString());
}

// ---------------------------------------------------------------------------------------------- outside the class
bool sync_cultures(moddata::Mod &mod, scriptstore::ScriptStore &store, localisation::LocEdits *loc)
{
    // Cultures and religions as the editor's texts have them now (an import brought some, an undo took them back,
    // the file was edited in Library). -> true when the lists were read again.
    const QStringList rels = {u"common/cultures.txt"_s, u"common/religion.txt"_s};
    QStringList texts;
    try {
        for (const QString &r : rels)
            texts << store.text(r);
    } catch (const std::exception &) {
        return false;
    }
    const std::optional<QStringList> old = mod._cult_sig;   // (Python: a tuple of the texts' hashes)
    if (old == texts)
        return false;
    mod._cult_sig = texts;
    if (!old && store.orig.value(rels[0]) == texts[0] && store.orig.value(rels[1]) == texts[1] &&
        store.orig.contains(rels[0]) && store.orig.contains(rels[1]))
        return false;                                    // (the first look: nothing was changed yet)
    const auto keep_c = mod.cultures;
    const auto keep_cg = mod.culture_groups;
    const auto keep_r = mod.religions;
    const auto keep_rg = mod.religion_groups;
    auto put_back = [&] {
        mod.cultures = keep_c;
        mod.culture_groups = keep_cg;
        mod.religions = keep_r;
        mod.religion_groups = keep_rg;
    };
    try {
        mod._load_cultures(texts[0]);
        mod._load_religions(texts[1]);
    } catch (const std::exception &) {                   // (a half-typed file: the lists stay as they were)
        put_back();
        return false;
    }
    if (mod.cultures.isEmpty() || mod.religions.isEmpty()) {
        put_back();
        return false;
    }
    if (loc) {                                           // (names typed / brought in this session)
        for (auto &[k, c] : mod.cultures)
            if (const auto t = loc->text(k); t && !t->isEmpty())
                c.name = *t;
        for (auto &[k, c] : mod.religions)
            if (const auto t = loc->text(k); t && !t->isEmpty())
                c.name = *t;
    }
    return true;
}

QString import_from(moddata::Mod &mod, document::Document &doc, const QString &kind, const QString &key, const QString &folder)
{
    // Import the pop type (culture, religion) `key` from the mod in `folder` — the Import page's own way: with what it
    // needs (its name, its icon …), one undo step, written on Apply. -> the message.
    Source src(folder, mod.base);
    Importer imp(mod, doc, src);
    const QString cat = kind == u"type" ? u"poptypes"_s : kind == u"culture" ? u"cultures"_s : u"religions"_s;
    const QString what = kind == u"type" ? u"pop type"_s : kind;
    const Plan plan = imp.plan({{cat, key}});
    if (!std::any_of(plan.items.begin(), plan.items.end(), [&](const ItemP &it) { return it->id() == Id(cat, key); }))
        throw std::runtime_error(u"%1 has no %2 %3 to import"_s.arg(src.name, what, key).toStdString());
    QString msg = imp.apply(plan, u"Import %1 %2 from %3"_s.arg(what, key, src.name));
    QStringList out;
    long long n = 0;
    for (const Needed &nd : plan.needed)
        if (!nd.comes) {
            if (n < 4)
                out << u"%1 (%2)"_s.arg(nd.dep->name, nd.why);
            ++n;
        }
    if (n)                                               // (the other mod's rules it names: they stay out — say which)
        msg += u" Left out (%1's rules, not in your mod): %2 — its lines about them are never true here; the Import page "
               u"can bring them."_s.arg(src.name, out.join(u"; "_s) + (n > 4 ? u" …"_s : QString()));
    return msg;
}

}  // namespace importer
