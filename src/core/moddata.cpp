// moddata.py — Loading and saving Victoria 2 mod data (map definitions, regions, cultures, pops...).
// A line-by-line translation; comments in "quotes" or Python's own words are the original's.
#include "core/moddata.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace moddata {

using pypath::join;
using script::first;
using script::Node;
using script::Nodes;

const OrderedMap<QString, Rgb> VANILLA_POPTYPES = [] {
    OrderedMap<QString, Rgb> m;
    m.insert(u"aristocrats"_s, {75, 25, 100});
    m.insert(u"artisans"_s, {127, 0, 0});
    m.insert(u"bureaucrats"_s, {40, 160, 160});
    m.insert(u"capitalists"_s, {20, 60, 200});
    m.insert(u"clergymen"_s, {180, 180, 180});
    m.insert(u"clerks"_s, {127, 127, 255});
    m.insert(u"craftsmen"_s, {100, 60, 20});
    m.insert(u"farmers"_s, {40, 160, 40});
    m.insert(u"labourers"_s, {120, 100, 60});
    m.insert(u"officers"_s, {200, 160, 0});
    m.insert(u"slaves"_s, {60, 60, 60});
    m.insert(u"soldiers"_s, {100, 110, 50});
    return m;
}();
const QStringList POPTYPE_ORDER = {u"bureaucrats"_s, u"aristocrats"_s, u"capitalists"_s, u"bankers"_s,
                                   u"officers"_s,    u"clergymen"_s,   u"clerks"_s,      u"artisans"_s,
                                   u"craftsmen"_s,   u"soldiers"_s,    u"farmers"_s,     u"labourers"_s,
                                   u"slaves"_s};
const QString FREE_NAME = u"UNUSED (deleted in ModEditor)"_s;
static const Rgb GREY{128, 128, 128};

// ---------------------------------------------------------------------- small Python pieces
static inline bool exists(const QString &p) { return fs::exists(p); }
static inline bool isdir(const QString &p) { return fs::isdir(p); }
static inline bool isfile(const QString &p) { return fs::isfile(p); }

static QStringList sorted(QStringList l)
{
    std::stable_sort(l.begin(), l.end());
    return l;
}
static QStringList sorted_lower(QStringList l)          // sorted(..., key=str.lower)
{
    std::stable_sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    return l;
}
static QStringList split_parts(const QString &rel) { return rel.split(u'/'); }

// "%s: %s" % (what, e) for an OSError
static QString err_text(const fs::OSError &e) { return e.text(); }

// re.match(r"\s*(\d+)", s) / re.match(r"^\s*(\d+)", s) -> the number, or nothing. \s and \d are Unicode for re.
static std::optional<long long> lead_int(const QString &s)
{
    qsizetype i = 0;
    while (i < s.size() && py::is_py_space(s[i].unicode()))
        ++i;
    const qsizetype j0 = i;
    while (i < s.size() && s[i].category() == QChar::Number_DecimalDigit)
        ++i;
    if (i == j0)
        return std::nullopt;
    return py::to_int(s.mid(j0, i - j0));
}

QString text_hash(const QString &text)
{
    // sha1 of text.encode("latin-1", "replace"): a character above 255 becomes ONE '?' (a surrogate pair too)
    QByteArray b;
    b.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t u = text[i].unicode();
        if (u < 256) {
            b.append(char(u));
            continue;
        }
        if (QChar::isHighSurrogate(u) && i + 1 < text.size() && QChar::isLowSurrogate(text[i + 1].unicode()))
            ++i;
        b.append('?');
    }
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex());
}

// {c.key: c.value for c in nodes if c.key and isinstance(c.value, str)}
static OrderedMap<QString, QString> str_fields(const Nodes &nodes)
{
    OrderedMap<QString, QString> v;
    for (const Node &c : nodes)
        if (c.has_key && !c.key.isEmpty() && !c.is_block)
            v.insert(c.key, c.value);
    return v;
}
static inline bool keyed_block(const Node &n) { return n.has_key && !n.key.isEmpty() && n.is_block; }
static inline bool truthy_key(const Node &n) { return n.has_key && !n.key.isEmpty(); }

template <typename T>
static QString loc_or(const OrderedMap<QString, QString> &loc, const QString &k, const T &fallback)
{
    const QString *v = loc.get(k);
    return (v && !v->isEmpty()) ? *v : QString(fallback);
}

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }

// ---------------------------------------------------------------------- folders
bool is_mod_folder(const QString &path)
{
    return !path.isEmpty() && isfile(join(path, {u"map"_s, u"definition.csv"_s})) &&
           isdir(join(path, {u"history"_s, u"pops"_s}));
}

bool is_game_folder(const QString &path)
{
    return !path.isEmpty() && isfile(join(path, {u"map"_s, u"definition.csv"_s})) && isdir(join(path, u"common"_s));
}

ModFileValues _mod_file_values(const QString &path)
{
    ModFileValues vals;
    try {
        for (const Node &n : script::parse(script::read_text(path))) {
            if (n.has_key && n.key == u"replace_path" && !n.is_block) {
                QString r = n.value;
                r.replace(u'\\', u'/');
                vals.replace_path << py::strip(r, u"/"_s);
            } else if (n.has_key && n.key == u"dependencies" && n.is_block) {   // a submod names the mods it sits on
                for (const Node &c : n.kids)
                    if (!c.has_key && !c.is_block)
                        vals.dependencies << c.value;
            } else if (truthy_key(n) && !n.is_block) {
                if (!vals.keys.contains(n.key))
                    vals.keys.insert(n.key, n.value);
            }
        }
    } catch (const fs::OSError &) {
    }
    return vals;
}

OptStr find_base_game(const QString &mods_dir)
{
    // The vanilla game folder: the parent of the mod folder (normal install), or a game folder placed inside it
    // (e.g. a copied 'Victoria 2' folder next to the mods).
    const QString parent = pypath::dirname(pypath::abspath(mods_dir));
    if (is_game_folder(parent))
        return parent;
    try {
        for (const QString &d : sorted(fs::listdir(mods_dir))) {
            const QString full = join(mods_dir, d);
            if (isdir(full) && is_game_folder(full) &&
                (isfile(join(full, u"v2game.exe"_s)) || isdir(join(full, u"mod"_s))))
                return full;
        }
    } catch (const fs::OSError &) {
    }
    return std::nullopt;
}

ModEntry _describe(const QString &name, const OptStr &folder, const OptStr &modfile, const OptStr &base)
{
    ModEntry m;
    m.name = name;
    m.folder = folder;
    m.modfile = modfile;
    if (!folder)
        m.note = u"mod folder not found"_s;
    else if (!isdir(join(*folder, {u"history"_s, u"pops"_s})))
        m.note = u"has no history/pops of its own"_s;
    else if (isfile(join(*folder, {u"map"_s, u"definition.csv"_s})))
        m.ok = true;
    else if (base) {
        m.ok = true;
        m.note = u"uses the base game map"_s;
    } else
        m.note = u"no map/definition.csv (and base game not found)"_s;
    return m;
}

std::pair<std::vector<ModEntry>, OptStr> discover_mods(const QString &mods_dir0, const OptStr &base_game)
{
    // List the mods in a Victoria 2 'mod' folder. Mods without their own map/ use the base game's map.
    const QString mods_dir = pypath::abspath(mods_dir0);
    const QString parent = pypath::dirname(mods_dir);
    const OptStr base = base_game && !base_game->isEmpty() ? base_game : find_base_game(mods_dir);   // (C++ only: chosen)
    QSet<QString> seen;
    std::vector<ModEntry> mods;
    QStringList entries;
    try {
        entries = sorted_lower(fs::listdir(mods_dir));
    } catch (const fs::OSError &) {
        return {{}, base};
    }
    for (const QString &fn : entries) {
        if (!py::lower(fn).endsWith(u".mod"))
            continue;
        const ModFileValues v = _mod_file_values(join(mods_dir, fn));
        QString rel = v.keys.value(u"path"_s, QString());
        rel.replace(u'\\', u'/');
        QStringList candidates;
        if (!rel.isEmpty())
            candidates << join(parent, split_parts(rel)) << join(mods_dir, split_parts(rel).back());
        candidates << join(mods_dir, fn.left(fn.size() - 4));
        // (C++ only, 2026-11-17) a mod folder outside the game: "mod/<path>" read from the folder the .mod file lies in
        // (a safeguard's "mod/<mod>/ModEditor safeguards/<copy>")
        if (const QStringList parts = split_parts(rel); parts.size() > 2 && py::lower(parts[0]) == u"mod")
            candidates << join(mods_dir, parts.mid(1));
        OptStr folder;
        for (const QString &c : candidates)
            if (isdir(c)) {
                folder = c;
                break;
            }
        const QString *nm = v.keys.get(u"name"_s);
        const QString name = (nm && !nm->isEmpty()) ? *nm : fn.left(fn.size() - 4);
        if (folder)
            seen.insert(pypath::normcase(pypath::abspath(*folder)));
        ModEntry d = _describe(name, folder, join(mods_dir, fn), base);
        d.replace = v.replace_path;
        d.deps = v.dependencies;
        if (const QString *u = v.keys.get(u"user_dir"_s))
            d.user_dir = *u;
        mods.push_back(std::move(d));
    }
    for (const QString &d : entries) {
        const QString full = join(mods_dir, d);
        if (isdir(full) && !seen.contains(pypath::normcase(pypath::abspath(full))) &&
            isdir(join(full, u"history"_s))) {
            ModEntry desc;
            if (base && pypath::normcase(pypath::abspath(full)) == pypath::normcase(*base)) {
                desc = _describe(u"Vanilla game (%1)"_s.arg(d), full, std::nullopt, std::nullopt);
                desc.note = u"base game — edits change vanilla files"_s;
            } else {
                desc = _describe(d, full, std::nullopt, base);
            }
            desc.replace = {};
            mods.push_back(std::move(desc));
        }
    }
    _mark_submods(mods);
    return {mods, base};
}

void _mark_submods(std::vector<ModEntry> &mods)
{
    // A mod whose .mod file names dependencies is a submod: it holds only what it changes, the rest is its
    // parent's. It can be opened when its parents are here and can be opened themselves.
    OrderedMap<QString, size_t> by_name;
    for (size_t i = 0; i < mods.size(); ++i) {
        const QString k = py::lower(py::strip(mods[i].name));
        if (!by_name.contains(k))
            by_name.insert(k, i);
    }
    std::function<bool(size_t, QList<size_t>)> ok = [&](size_t mi, QList<size_t> seen) -> bool {
        const ModEntry &m = mods[mi];
        if (m.deps.isEmpty())
            return m.ok;
        if (seen.contains(mi) || !m.folder)
            return false;
        seen << mi;
        for (const QString &d : m.deps) {
            const size_t *j = by_name.get(py::lower(py::strip(d)));
            if (!j || !ok(*j, seen))
                return false;
        }
        return true;
    };
    for (size_t i = 0; i < mods.size(); ++i) {
        if (!mods[i].deps.isEmpty()) {
            const bool fine = ok(i, {});
            mods[i].ok = fine;
            mods[i].note = u"submod of %1%2"_s.arg(mods[i].deps.join(u", "_s),
                                                   fine ? QString() : u" — which is not in this folder"_s);
        }
    }
}

// ---------------------------------------------------------------------- Mod
Mod::Mod(const QString &root_, const OptStr &base_, const OptStr &name_, const QStringList &replace_)
{
    root = pypath::abspath(root_);
    if (base_ && !base_->isEmpty())
        base = pypath::abspath(*base_);
    if (base && pypath::normcase(*base) == pypath::normcase(root))
        base = std::nullopt;
    display_name = name_;
    for (const QString &r : replace_)
        replace << py::strip(r, u"/"_s);
}

bool Mod::replaced(const QStringList &parts) const
{
    // True if the .mod file replaces this path (the game then ignores the vanilla version).
    QString rel = parts.join(u'/');
    rel.replace(u'\\', u'/');
    for (const QString &r : replace)
        if (rel == r || rel.startsWith(r + u'/'))
            return true;
    return false;
}

QString Mod::p(const QStringList &parts) const
{
    const QString path = join(root, parts);
    if (base && !exists(path) && !replaced(parts)) {
        const QString alt = join(*base, parts);
        if (exists(alt))
            return alt;
    }
    return path;
}

std::vector<LayeredFile> Mod::layered(const QStringList &parts) const
{
    // The files of a folder the game reads in layers, sorted: the base game's files (unless the .mod file replaces
    // the folder) with the mod's laid over them; a mod file takes the place of the base file with the same
    // sub-folder and name. One level of sub-folders is looked into.
    OrderedMap<QString, LayeredFile> out;
    const QString mine = join(root, parts);
    std::vector<std::pair<QString, bool>> roots;
    if (base && !replaced(parts))
        roots.emplace_back(join(*base, parts), false);
    roots.emplace_back(mine, true);
    for (const auto &[d, own] : roots) {
        if (!isdir(d))
            continue;
        for (const QString &sub : sorted(fs::listdir(d))) {
            const QString sd = join(d, sub);
            std::vector<std::pair<QString, QString>> names;
            if (isdir(sd)) {
                for (const QString &fn : sorted(fs::listdir(sd)))
                    names.emplace_back(sub + u'/' + fn, join(sd, fn));
            } else {
                names.emplace_back(sub, sd);
            }
            for (const auto &[rel, path] : names)
                if (isfile(path))
                    out.insert(py::lower(rel), LayeredFile{rel, path, join(mine, split_parts(rel)), own});
        }
    }
    QStringList keys;
    for (const auto &kv : out)
        keys << kv.first;
    std::vector<LayeredFile> res;
    for (const QString &k : sorted(keys))
        res.push_back(*out.get(k));
    return res;
}

bool Mod::in_mod(const QString &path) const
{
    return pypath::normcase(pypath::abspath(path)).startsWith(pypath::normcase(root) + u'\\');
}

QString Mod::adopt(const QString &path)
{
    // Copy-on-write (the tester, 2026-10-17): a base game file the mod is about to change is copied into the mod
    // under the same path and the mod's copy is returned. A copy the user already made is used as it is. The base
    // game is never written.
    const QString full = pypath::abspath(path);
    if (in_mod(full)) {
        if (!isfile(full)) {                 // in neither the mod nor the game: the mod gets a new one
            fs::makedirs(pypath::dirname(full));
            script::write_text(full, QString());
        }
        return full;
    }
    if (!base)
        return path;
    const QString b = pypath::normcase(pypath::abspath(*base)) + u'\\';
    if (!pypath::normcase(full).startsWith(b))
        return path;
    const QString mine = join(root, full.mid(b.size()));
    if (!isfile(mine)) {
        fs::makedirs(pypath::dirname(mine));
        if (isfile(full))
            fs::copy2(full, mine);
        else                                 // (missing in the game too: an empty file to write into)
            script::write_text(mine, QString());
    }
    return mine;
}

QString Mod::pops_dir() const { return join(root, {u"history"_s, u"pops"_s}); }

QString Mod::name() const { return (display_name && !display_name->isEmpty()) ? *display_name : pypath::basename(root); }

template <typename F>
void Mod::_safe(const QString &what, F fn)
{
    try {
        fn();
    } catch (const fs::OSError &e) {
        warnings << u"%1: %2"_s.arg(what, err_text(e));
    }
}

void Mod::load()
{
    loc = _load_localisation();
    _load_definitions();
    _safe(u"default.map"_s, [&] { _load_default_map(); });
    _safe(u"region.txt"_s, [&] { _load_regions(); });
    _safe(u"countries.txt"_s, [&] { _load_countries(); });
    _safe(u"cultures.txt"_s, [&] { _load_cultures(); });
    _safe(u"religion.txt"_s, [&] { _load_religions(); });
    _load_poptypes();
    _load_history();
    _safe(u"bookmarks.txt"_s, [&] { _load_bookmarks(); });
    _safe(u"buildings.txt"_s, [&] { _load_buildings(); });
    _safe(u"goods.txt"_s, [&] { _load_goods(); });
    _safe(u"production_types.txt"_s, [&] { _load_production(); });
}

void Mod::_load_buildings()
{
    // buildings.txt: key -> {type (factory/fort/naval_base/infrastructure), max_level, one_per_state, port}.
    buildings_from(script::read_text(p({u"common"_s, u"buildings.txt"_s})));
}

void Mod::buildings_from(const QString &text)
{
    buildings.clear();
    for (const Node &n : script::parse(text)) {
        if (!keyed_block(n))
            continue;
        const auto v = str_fields(n.kids);
        long long max_level = 1;
        if (const QString *ml = v.get(u"max_level"_s)) {
            if (auto f = py::to_float(*ml)) {
                try {
                    max_level = py::trunc_int(*f);
                } catch (const py::ValueError &) {     // (Python: OverflowError for inf — see PROGRESS "found")
                    max_level = 1;
                }
            }
        }
        Building b;
        b.id = n.key;
        b.type = v.value(u"type"_s, QString());
        b.max_level = max_level;
        b.one_per_state = v.value(u"one_per_state"_s) == u"yes";
        b.port = v.value(u"port"_s) == u"yes";
        b.province = v.value(u"province"_s) == u"yes";
        if (const QString *pt = v.get(u"production_type"_s))
            b.production_type = *pt;
        QString nice = n.key;
        nice.replace(u'_', u' ');
        b.name = loc_or(loc, n.key, py::capitalize(nice));
        buildings.insert(n.key, b);
    }
}

FactoryInfo Mod::factory_info(const QString &key)
{
    // {output, group, group_name, coastal} for a factory building, from production_types/goods.
    const Building *b = buildings.get(key);
    const QString pkey = (b && b->production_type && !b->production_type->isEmpty()) ? *b->production_type : key;
    const Production *prod = production.get(pkey);
    FactoryInfo fi;
    fi.output = prod ? prod->output : std::nullopt;
    const Good *g = fi.output ? goods.get(*fi.output) : nullptr;
    fi.group = (fi.output && g) ? OptStr(g->group) : std::nullopt;
    fi.output_name = g ? g->name : (fi.output ? *fi.output : u"?"_s);
    QString grp_nice = fi.group && !fi.group->isEmpty() ? *fi.group : u"other"_s;
    grp_nice.replace(u'_', u' ');
    const QString *gl = fi.group ? loc.get(*fi.group) : nullptr;
    fi.group_name = (gl && !gl->isEmpty()) ? *gl : py::capitalize(grp_nice);
    const qsizetype gi = fi.group ? goods_groups.indexOf(*fi.group) : -1;
    fi.group_order = gi >= 0 ? gi : 99;
    fi.coastal = prod && prod->coastal;
    return fi;
}

QStringList Mod::factory_types()
{
    // Factory buildings grouped by the goods category of what they produce, then in the order they are likely
    // unlocked (available from start -> tech year -> invention's required tech year).
    struct K {
        long long order;
        long long year;
        double cost;
        QString name;
    };
    std::vector<std::pair<K, QString>> rows;
    for (const auto &[k, b] : buildings) {
        if (b.type != u"factory")
            continue;
        const FactoryInfo fi = factory_info(k);
        const Unlock *u = unlocks().get(k);
        rows.push_back({K{fi.group_order, u ? u->year : 0, u ? u->cost : 0.0, py::lower(b.name)}, k});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return std::tie(a.first.order, a.first.year, a.first.cost, a.first.name) <
               std::tie(b.first.order, b.first.year, b.first.cost, b.first.name);
    });
    QStringList out;
    for (const auto &r : rows)
        out << r.second;
    return out;
}

static void walk(const Nodes &nodes, const std::function<void(const Node &)> &fn)
{
    for (const Node &n : nodes) {
        fn(n);
        if (n.is_block)
            walk(n.kids, fn);
    }
}

const OrderedMap<QString, Unlock> &Mod::unlocks()
{
    // {building: {"year", "by", "kind"}} from `activate_building` in technologies/ and inventions/.
    if (_unlocks)
        return *_unlocks;
    auto files = [&](const QString &folder) {
        QStringList out;
        const QString d = p({folder});
        if (!isdir(d))
            return out;
        for (const QString &f : sorted(fs::listdir(d)))
            if (py::lower(f).endsWith(u".txt"))
                out << join(d, f);
        return out;
    };
    OrderedMap<QString, std::pair<long long, double>> tech_year;
    OrderedMap<QString, Unlock> found;
    auto better = [&](const QString &b, long long year, double cost) {
        const Unlock *f = found.get(b);
        return !f || std::tie(year, cost) < std::tie(f->year, f->cost);
    };
    for (const QString &path : files(u"technologies"_s)) {
        Nodes nodes;
        try {
            nodes = script::parse(script::read_text(path));
        } catch (const fs::OSError &) {
            continue;
        }
        for (const Node &t : nodes) {
            if (!keyed_block(t))
                continue;
            const Node *y = first(t.kids, u"year"_s), *c = first(t.kids, u"cost"_s);
            long long year = 9999;
            double cost = 0;
            try {
                if (y && !y->is_block) {
                    auto f = py::to_float(y->value);
                    if (!f)
                        throw py::ValueError("float");
                    year = py::trunc_int(*f);
                }
                if (c && !c->is_block) {
                    auto f = py::to_float(c->value);
                    if (!f)
                        throw py::ValueError("float");
                    cost = *f;
                }
            } catch (const py::ValueError &) {
                year = 9999;
                cost = 0;
            }
            tech_year.insert(t.key, {year, cost});
            QStringList acts;
            walk(t.kids, [&](const Node &n) {
                if (n.has_key && n.key == u"activate_building" && !n.is_block)
                    acts << n.value;
            });
            for (const QString &b : acts)
                if (better(b, year, cost))
                    found.insert(b, Unlock{year, cost, loc_or(loc, t.key, t.key), u"technology"_s, std::nullopt});
        }
    }
    for (const QString &path : files(u"inventions"_s)) {
        Nodes nodes;
        try {
            nodes = script::parse(script::read_text(path));
        } catch (const fs::OSError &) {
            continue;
        }
        for (const Node &inv : nodes) {
            if (!keyed_block(inv))
                continue;
            QStringList acts;
            std::vector<std::pair<long long, double>> years;
            walk(inv.kids, [&](const Node &n) {
                if (n.has_key && n.key == u"activate_building" && !n.is_block)
                    acts << n.value;
            });
            if (acts.isEmpty())
                continue;
            const Node *lim = first(inv.kids, u"limit"_s);
            QStringList unknown;
            if (lim && lim->is_block) {
                walk(lim->kids, [&](const Node &n) {
                    if (n.has_key && tech_year.contains(n.key))
                        years.push_back(*tech_year.get(n.key));
                    else if (truthy_key(n) && !n.is_block && (n.value == u"1" || n.value == u"yes") &&
                             n.key != u"AND" && n.key != u"OR" && n.key != u"NOT" && !n.key.startsWith(u"has_"))
                        unknown << n.key;
                });
            }
            std::pair<long long, double> yc{9999, 0};
            if (!years.empty())
                yc = *std::max_element(years.begin(), years.end());
            for (const QString &b : acts)
                if (better(b, yc.first, yc.second))
                    // e.g. a misspelt technology name: the invention may never fire
                    found.insert(b, Unlock{yc.first, yc.second, loc_or(loc, inv.key, inv.key), u"invention"_s,
                                           (years.empty() && !unknown.isEmpty()) ? OptStr(unknown.join(u", "_s))
                                                                                 : std::nullopt});
        }
    }
    _unlocks = found;
    return *_unlocks;
}

void Mod::_load_production()
{
    // production_types.txt: templates carry the type (factory / rgo / artisan); production types reference a
    // template and name their output goods.
    const Nodes nodes = script::parse(script::read_text(p({u"common"_s, u"production_types.txt"_s})));
    OrderedMap<QString, QString> templates;
    for (const Node &n : nodes) {
        if (keyed_block(n)) {
            const Node *t = first(n.kids, u"type"_s);
            if (t && !t->is_block && !first(n.kids, u"output_goods"_s))
                templates.insert(n.key, t->value);
        }
    }
    production.clear();
    for (const Node &n : nodes) {
        if (!keyed_block(n) || templates.contains(n.key))
            continue;
        const auto v = str_fields(n.kids);
        const QString *ty = v.get(u"type"_s);
        Production pr;
        pr.type = (ty && !ty->isEmpty()) ? *ty : templates.value(v.value(u"template"_s, QString()), QString());
        if (const QString *tp = v.get(u"template"_s))
            pr.template_ = *tp;
        const QString og = py::strip(v.value(u"output_goods"_s, QString()));
        if (!og.isEmpty())
            pr.output = og;
        pr.coastal = v.value(u"is_coastal"_s) == u"yes";
        pr.farm = v.value(u"farm"_s) == u"yes";
        pr.mine = v.value(u"mine"_s) == u"yes";
        production.insert(n.key, pr);
    }
}

OrderedMap<QString, QString> Mod::rgo_goods() const
{
    // {good: "Farms"/"Mines"/"RGO"} — goods produced by RGO production types (raw resources).
    OrderedMap<QString, QString> out;
    for (const auto &[_k, pr] : production)
        if (pr.type == u"rgo" && pr.output)
            out.insert(*pr.output, pr.mine ? u"Mines"_s : (pr.farm ? u"Farms"_s : u"RGO"_s));
    return out;
}

void Mod::_load_goods()
{
    // goods.txt: key -> {name, color, group}.
    goods.clear();
    goods_groups.clear();
    for (const Node &g : script::parse(script::read_text(p({u"common"_s, u"goods.txt"_s})))) {
        if (keyed_block(g))
            goods_groups << g.key;
        if (!keyed_block(g))
            continue;
        for (const Node &c : g.kids)
            if (keyed_block(c)) {
                auto col = script::color_of(c.kids);
                goods.insert(c.key, Good{c.key, g.key, loc_or(loc, c.key, c.key), col ? *col : GREY});
            }
    }
}

std::vector<std::pair<QString, bool>> Mod::loc_files() const
{
    // [(path, in the mod?)] of the localisation files in the order the game reads them — MEASURED in the game
    // (2026-10-15): ONE alphabetical order over the mod's and the base game's files together; a mod file with the
    // same name as a base game file takes its place. The first definition of a key is the one the game uses.
    OrderedMap<QString, std::pair<QString, bool>> files;
    if (base && !replaced({u"localisation"_s})) {
        const QString d = join(*base, u"localisation"_s);
        if (isdir(d))
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".csv"))
                    files.insert(py::lower(fn), {join(d, fn), false});
    }
    const QString d = join(root, u"localisation"_s);
    if (isdir(d))
        for (const QString &fn : fs::listdir(d))
            if (py::lower(fn).endsWith(u".csv"))
                files.insert(py::lower(fn), {join(d, fn), true});
    QStringList keys;
    for (const auto &kv : files)
        keys << kv.first;
    std::vector<std::pair<QString, bool>> out;
    for (const QString &k : sorted(keys))
        out.push_back(*files.get(k));
    return out;
}

OrderedMap<QString, QString> Mod::_load_localisation()
{
    OrderedMap<QString, QString> l;
    for (const auto &[path, _own] : loc_files()) {
        QString text;
        try {
            text = script::read_text(path);
        } catch (const fs::OSError &) {
            continue;
        }
        for (const QString &line : py::splitlines(text)) {
            if (line.isEmpty() || line.startsWith(u'#'))
                continue;
            const QStringList parts = line.split(u';');
            if (parts.size() >= 2 && !parts[0].isEmpty() && !parts[1].isEmpty() && !l.contains(parts[0]))
                l.insert(parts[0], parts[1]);
        }
    }
    return l;
}

void Mod::_load_definitions()
{
    provinces.clear();
    free_slots.clear();               // numbers kept in definition.csv for provinces deleted in the editor
    for (const QString &line : py::splitlines(script::read_text(p({u"map"_s, u"definition.csv"_s})))) {
        const QStringList parts = line.split(u';');
        if (parts.size() < 4 || !py::isdigit(py::strip(parts[0])))
            continue;
        long long pid;
        Rgb rgb;
        try {
            pid = py::to_int(parts[0]);
            // (the number's leading digits, like the game: the base game writes Zouar's blue as "223.")
            auto r = lead_int(parts[1]), g = lead_int(parts[2]), b = lead_int(parts[3]);
            if (!r || !g || !b)
                continue;                 // (AttributeError: no match)
            rgb = {int(*r), int(*g), int(*b)};
        } catch (const py::ValueError &) {
            continue;
        }
        const QString dname = parts.size() > 4 ? py::strip(parts[4]) : QString();
        if (dname == FREE_NAME) {         // a deleted province's number: not a province any more
            free_slots.insert(pid, rgb);
            continue;
        }
        const QString *ln = loc.get(u"PROV%1"_s.arg(pid));
        const QString name = (ln && !ln->isEmpty()) ? *ln : (!dname.isEmpty() ? dname : u"Province %1"_s.arg(pid));
        Province pr;
        pr.id = pid;
        pr.rgb = rgb;
        pr.name = name;
        provinces.insert(pid, pr);
    }
}

void Mod::_load_default_map()
{
    const Nodes dm = script::parse(script::read_text(p({u"map"_s, u"default.map"_s})));
    for (const QString &key : {u"sea_starts"_s, u"lakes"_s}) {
        const Node *n = first(dm, key);
        if (n && n->is_block)
            for (long long pid : script::bare_ints(n->kids))
                if (Province *pr = provinces.get(pid))
                    pr->sea = true;
    }
    const Node *n = first(dm, u"provinces"_s);
    provinces_bmp = (n && !n->is_block) ? n->value : u"provinces.bmp"_s;
}

void Mod::_load_regions()
{
    // region.txt: the states. Regions the user marked as utility states go to self.utility instead: they are no
    // state, and a province's state is the first real state that lists it.
    const QString path = p({u"map"_s, u"region.txt"_s});
    const QString text = script::read_text(path);
    region_hash = text_hash(text);
    regions.clear();
    utility.clear();
    region_order.clear();
    region_keys_on_disk.clear();
    const QStringList marks = utility_marks();
    const QSet<QString> marked(marks.begin(), marks.end());
    for (const Node &n : script::parse(text)) {
        if (!keyed_block(n))
            continue;
        region_keys_on_disk.insert(n.key);
        region_order << n.key;
        std::vector<long long> ids;
        for (long long i : script::bare_ints(n.kids))
            if (provinces.contains(i))
                ids.push_back(i);
        Region r{n.key, loc_or(loc, n.key, n.key), ids};
        if (marked.contains(n.key)) {
            utility.insert(n.key, r);
            continue;
        }
        regions.insert(n.key, r);
        for (long long pid : ids) {
            Province *pr = provinces.get(pid);
            if (!pr->region)
                pr->region = n.key;
        }
    }
}

QString Mod::_utility_key() const { return u"utilityStates/%1"_s.arg(pypath::basename(root)); }

QStringList Mod::utility_marks() const
{
    const QVariant v = settings_::settings()->value(_utility_key());
    if (!v.isValid() || v.isNull())
        return {};
    if (v.typeId() == QMetaType::QStringList || v.typeId() == QMetaType::QVariantList)
        return v.toStringList();
    const QString s = v.toString();
    return s.isEmpty() ? QStringList() : QStringList{s};
}

void Mod::set_utility(const QString &rid, bool on)
{
    // Mark / unmark a region as a utility state (the caller re-derives the provinces' states).
    if (on && regions.contains(rid)) {
        utility.insert(rid, *regions.get(rid));
        regions.remove(rid);
    } else if (!on && utility.contains(rid)) {
        regions.insert(rid, *utility.get(rid));
        utility.remove(rid);
        // keep the file order (the first state listing a province is its state)
        std::vector<std::pair<QString, Region>> items(regions.begin(), regions.end());
        auto order = [&](const QString &k) {
            const qsizetype i = region_order.indexOf(k);
            return i >= 0 ? i : qsizetype(1000000);
        };
        std::stable_sort(items.begin(), items.end(),
                         [&](const auto &a, const auto &b) { return order(a.first) < order(b.first); });
        regions.clear();
        for (auto &kv : items)
            regions.insert(kv.first, kv.second);
    }
}

void Mod::save_utility()
{
    // The utility marks as they are now are kept (the editor's settings) — on Apply, like every change.
    QStringList keys;
    for (const auto &kv : utility)
        keys << kv.first;
    settings_::settings()->setValue(_utility_key(), sorted(keys));
}

const Region *Mod::any_region(const QString &rid) const
{
    const Region *r = regions.get(rid);
    return r ? r : utility.get(rid);
}

std::vector<std::pair<QString, QString>> Mod::utility_suggestions() const
{
    // [(region, reason)] — regions that look like utility states: the state of none of their provinces, or a
    // huge share of all land.
    long long land = 0;
    for (const auto &kv : provinces)
        land += !kv.second.sea;
    std::vector<std::pair<QString, QString>> out;
    for (const auto &[rid, r] : regions) {
        if (r.provinces.empty())
            continue;
        long long firsts = 0;
        for (long long pid : r.provinces)
            firsts += provinces.get(pid)->region == rid;
        const long long n = static_cast<long long>(r.provinces.size());
        if (firsts == 0)
            out.emplace_back(rid, u"the state of none of its %1 provinces (an earlier region lists each)"_s.arg(n));
        else if (land && double(n) >= std::max(60.0, double(land) * 0.2))
            out.emplace_back(rid, u"%1 provinces — %2% of the land"_s.arg(n).arg(py::floordiv(100 * n, land)));
    }
    return out;
}

void Mod::_load_countries()
{
    countries.clear();
    bool dynamic = false;
    for (const Node &n : script::parse(script::read_text(p({u"common"_s, u"countries.txt"_s})))) {
        if (n.has_key && n.key == u"dynamic_tags") {
            dynamic = true;               // every tag after this line is a dynamic (D01–D50 style) tag
            continue;
        }
        if (truthy_key(n) && !n.is_block && n.key.size() == 3) {
            std::optional<Rgb> color;
            QString rel = n.value;
            rel.replace(u'/', u'\\');
            const QString cpath = p({u"common"_s, rel});
            if (isfile(cpath)) {
                try {
                    color = script::color_of(script::parse(script::read_text(cpath)));
                } catch (const std::exception &) {
                }
            }
            countries.insert(n.key, Country{n.key, loc_or(loc, n.key, n.key), color ? *color : GREY, dynamic, n.value});
        }
    }
}

void Mod::_load_cultures(const std::optional<QString> &text)
{
    cult_version += 1;                    // (pickers built from the lists follow)
    cultures.clear();
    culture_groups.clear();               // group keys in file order
    for (const Node &g : script::parse(text ? *text : script::read_text(p({u"common"_s, u"cultures.txt"_s})))) {
        if (!keyed_block(g))
            continue;
        culture_groups << g.key;
        for (const Node &c : g.kids)
            if (keyed_block(c) && first(c.kids, u"color"_s)) {
                auto col = script::color_of(c.kids);
                cultures.insert(c.key, Culture{c.key, g.key, loc_or(loc, c.key, c.key), col ? *col : GREY});
            }
    }
}

void Mod::_load_religions(const std::optional<QString> &text)
{
    cult_version += 1;
    religions.clear();
    religion_groups.clear();              // group keys in file order
    for (const Node &g : script::parse(text ? *text : script::read_text(p({u"common"_s, u"religion.txt"_s})))) {
        if (!keyed_block(g))
            continue;
        religion_groups << g.key;
        for (const Node &r : g.kids) {
            if (!keyed_block(r))
                continue;
            const Node *ic = first(r.kids, u"icon"_s);
            std::optional<long long> icon;
            if (ic && !ic->is_block) {
                try {
                    icon = py::to_int(ic->value);
                } catch (const py::ValueError &) {
                    icon = std::nullopt;
                }
            }
            const Node *pagan = first(r.kids, u"pagan"_s);
            auto col = script::color_of(r.kids);
            religions.insert(r.key, Religion{r.key, g.key, loc_or(loc, r.key, r.key), col ? *col : GREY, icon,
                                             pagan && !pagan->is_block && pagan->value == u"yes"});
        }
    }
}

void Mod::_load_poptypes()
{
    // Pop types = the vanilla set (always) + whatever the mod's poptypes folder adds. "source" is "vanilla" or
    // "mod". Vanilla comes from the base game's poptypes folder when the game was found, else the built-in list.
    using Scan = OrderedMap<QString, std::optional<Rgb>>;
    auto scan = [](const OptStr &d) {
        Scan out;
        if (d && isdir(*d)) {
            for (const QString &fn : sorted(fs::listdir(*d))) {
                if (!py::lower(fn).endsWith(u".txt"))
                    continue;
                try {
                    out.insert(fn.left(fn.size() - 4), script::color_of(script::parse(script::read_text(join(*d, fn)))));
                } catch (const std::exception &) {
                    out.insert(fn.left(fn.size() - 4), std::nullopt);
                }
            }
        }
        return out;
    };
    const Scan base_scan = base ? scan(join(*base, u"poptypes"_s)) : Scan();
    Scan vanilla = base_scan;
    if (vanilla.isEmpty())                // `base or dict(VANILLA_POPTYPES)`
        for (const auto &[k, c] : VANILLA_POPTYPES)
            vanilla.insert(k, c);
    const Scan mod = scan(join(root, u"poptypes"_s));
    poptypes.clear();
    QStringList keys;
    for (const auto &kv : vanilla)
        keys << kv.first;
    for (const auto &kv : mod)
        if (!vanilla.contains(kv.first))
            keys << kv.first;
    for (const QString &key : keys) {
        std::optional<Rgb> color;
        if (const auto *m = mod.get(key); m && *m)
            color = *m;
        else if (const auto *v = vanilla.get(key); v && *v)
            color = *v;
        else if (const Rgb *vp = VANILLA_POPTYPES.get(key))
            color = *vp;
        poptypes.insert(key, PopType{key, loc_or(loc, key, py::capitalize(key)), color ? *color : GREY,
                                     vanilla.contains(key) ? u"vanilla"_s : u"mod"_s});
    }
}

std::pair<long long, QString> Mod::poptype_sort_key(const QString &t) const
{
    const qsizetype i = POPTYPE_ORDER.indexOf(t);
    return {i >= 0 ? i : 99, t};
}

void Mod::_load_history()
{
    // A province can be listed in several folders (e.g. empty vanilla placeholders): keep all entries and apply
    // them in load order.
    history.clear();
    for (const LayeredFile &f : layered({u"history"_s, u"provinces"_s})) {
        const QString fn = f.rel.mid(f.rel.lastIndexOf(u'/') + 1);
        std::optional<long long> num;
        try {
            num = lead_int(fn);
        } catch (const py::ValueError &) {
            num = std::nullopt;
        }
        if (!f.rel.contains(u'/') || !num || !py::lower(fn).endsWith(u".txt"))
            continue;
        Nodes nodes;
        try {
            nodes = script::parse(script::read_text(f.path));
        } catch (const fs::OSError &e) {
            warnings << u"history %1: %2"_s.arg(f.rel, err_text(e));
            continue;
        }
        history[*num].emplace_back(f.rel, std::move(nodes));
    }
}

void Mod::_load_bookmarks()
{
    bookmarks.clear();
    has_bookmarks = true;
    for (const Node &n : script::parse(script::read_text(p({u"common"_s, u"bookmarks.txt"_s})))) {
        if (n.has_key && n.key == u"bookmark" && n.is_block) {
            const Node *d = first(n.kids, u"date"_s), *nm = first(n.kids, u"name"_s);
            if (d)
                bookmarks.insert(d->value, nm ? loc.value(nm->value, nm->value) : QString());
        }
    }
}

void Mod::apply_history(const QString &scenario)
{
    // Resolve owner/controller/cores at the scenario date (if the folder name is a date).
    const auto sdate = script::parse_date(scenario);
    for (auto &kv : provinces) {
        Province &pr = kv.second;
        pr.owner = pr.controller = pr.life_rating = pr.trade_goods = pr.history_file = std::nullopt;
        pr.cores.clear();
    }
    for (const auto &[pid, entries] : history) {
        Province *pr = provinces.get(pid);
        if (!pr)
            continue;
        pr->history_file = entries[0].first;
        for (const auto &e : entries)
            if (!e.second.empty()) {
                pr->history_file = e.first;
                break;
            }
        auto apply = [pr](const Nodes &items) {
            for (const Node &n : items) {
                if (n.is_block)
                    continue;
                if (!n.has_key)
                    continue;
                if (n.key == u"owner") pr->owner = n.value;
                else if (n.key == u"controller") pr->controller = n.value;
                else if (n.key == u"life_rating") pr->life_rating = n.value;
                else if (n.key == u"trade_goods") pr->trade_goods = n.value;
                else if (n.key == u"add_core" && !pr->cores.contains(n.value)) pr->cores << n.value;
                else if (n.key == u"remove_core" && pr->cores.contains(n.value)) pr->cores.removeOne(n.value);
            }
        };
        for (const auto &e : entries)
            apply(e.second);
        if (sdate) {
            std::vector<std::pair<std::tuple<long long, long long, long long>, const Node *>> dated;
            for (const auto &e : entries)
                for (const Node &n : e.second)
                    if (truthy_key(n) && n.is_block)
                        if (auto d = script::parse_date(n.key))
                            dated.emplace_back(*d, &n);
            std::stable_sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[d, n] : dated)
                if (d <= *sdate)
                    apply(n->kids);
        }
    }
}

std::vector<ScenarioInfo> Mod::scenarios()
{
    std::vector<ScenarioInfo> res;
    if (!has_bookmarks) {                 // listing before a full load()
        bookmarks.clear();
        _safe(u"bookmarks.txt"_s, [&] { _load_bookmarks(); });
        has_bookmarks = true;
    }
    for (const QString &d : pop_scenarios()) {
        ScenarioInfo s;
        s.name = d;
        s.files = pop_files(d).size();
        s.isDate = script::parse_date(d).has_value();
        if (const QString *b = bookmarks.get(d))
            s.bookmark = *b;
        res.push_back(s);
    }
    return res;
}

QStringList Mod::pop_scenarios() const
{
    // The start-date folders of history/pops: the mod's and (not replaced) the base game's.
    OrderedMap<QString, QString> names;
    QStringList roots{pops_dir()};
    if (base && !replaced({u"history"_s, u"pops"_s}))
        roots.prepend(join(*base, {u"history"_s, u"pops"_s}));
    for (const QString &r : roots)
        if (isdir(r))
            for (const QString &d : fs::listdir(r))
                if (isdir(join(r, d)))
                    names.insert(py::lower(d), d);
    QStringList keys;
    for (const auto &kv : names)
        keys << kv.first;
    QStringList out;
    for (const QString &k : sorted(keys))
        out << *names.get(k);
    return out;
}

OrderedMap<QString, QString> Mod::pop_files(const QString &scenario) const
{
    // {file name: path to read} of a start date, in layers: the mod's file where it has one, else the game's.
    OrderedMap<QString, std::pair<QString, QString>> out;
    QStringList roots;
    if (base && !replaced({u"history"_s, u"pops"_s}))
        roots << join(*base, {u"history"_s, u"pops"_s});
    roots << pops_dir();
    const QString want = py::lower(scenario);
    for (const QString &r : roots) {
        OptStr d;
        if (isdir(r))
            for (const QString &x : fs::listdir(r))
                if (py::lower(x) == want && isdir(join(r, x))) {
                    d = join(r, x);
                    break;
                }
        if (!d)
            continue;
        for (const QString &fn : fs::listdir(*d))
            if (py::lower(fn).endsWith(u".txt") && isfile(join(*d, fn)))
                out.insert(py::lower(fn), {fn, join(*d, fn)});
    }
    QStringList keys;
    for (const auto &kv : out)
        keys << kv.first;
    OrderedMap<QString, QString> res;
    for (const QString &k : sorted(keys)) {
        const auto *v = out.get(k);
        res.insert(v->first, v->second);
    }
    return res;
}

QString Mod::scenario_dir(const QString &scenario) const
{
    // The mod's own folder of a start date — the date must exist in the mod or in the base game.
    if (scenario.isEmpty() || scenario.contains(u'/') || scenario.contains(u'\\') || scenario == u"." ||
        scenario == u"..")
        throw py::ValueError("bad scenario name");
    const QString d = join(pops_dir(), scenario);
    if (!isdir(d) && pop_files(scenario).isEmpty())
        throw py::ValueError(("scenario folder not found: " + scenario).toStdString());
    return d;
}

void Mod::create_scenario(const QString &name_, const OptStr &copy_from)
{
    // NAME_RE = ^[A-Za-z0-9_\-.]+$  ($ also before one final "\n")
    QString nm = name_;
    nm.replace(u' ', u'_');
    QString body = nm.endsWith(u'\n') ? nm.chopped(1) : nm;
    bool okname = !name_.isEmpty() && !body.isEmpty();
    for (QChar c : body)
        okname = okname && ((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') ||
                            c == u'_' || c == u'-' || c == u'.');
    if (!okname)
        throw py::ValueError("Invalid scenario name (use e.g. 1861.1.1)");
    const QString dst = join(pops_dir(), name_);
    if (exists(dst))
        throw py::ValueError("Scenario folder already exists");
    fs::makedirs(dst);
    if (copy_from)                        // (the start date as the game reads it: mod + base files)
        for (const auto &[fn, path] : pop_files(*copy_from))
            fs::copy2(path, join(dst, fn));
}

// ---------------------------------------------------------------------- pop files
// Python's str.rfind("\n", 0, end) / str.find("\n", start)
static qsizetype rfind_nl(const QString &t, qsizetype end)
{
    if (end <= 0)
        return -1;
    return t.lastIndexOf(u'\n', std::min(end, t.size()) - 1);
}
static inline QString slice(const QString &t, qsizetype a, qsizetype b)   // t[a:b]
{
    a = std::clamp<qsizetype>(a, 0, t.size());
    b = std::clamp<qsizetype>(b, 0, t.size());
    return b > a ? t.mid(a, b - a) : QString();
}
static inline bool blank(const QString &s) { return py::strip(s).isEmpty(); }

PopLoad load_pop_files(Mod &mod, const QString &scenario)
{
    // Parse every pop file of a scenario. provinces: {pid: {"file", "block", "close", "pops": [pop, …]}} where
    // each pop has an "o" with its source spans; files: {filename: sha1_of_text}.
    mod.scenario_dir(scenario);                          // (checks the name)
    PopLoad res;
    const OrderedMap<QString, QString> paths = mod.pop_files(scenario);   // the mod's files, the game's for the rest
    QStringList names;
    for (const auto &kv : paths)
        names << kv.first;
    for (const QString &fn : sorted_lower(names)) {
        QString text;
        try {
            text = script::read_text(*paths.get(fn));
        } catch (const fs::OSError &e) {
            res.problems << u"%1: %2"_s.arg(fn, err_text(e));
            continue;
        }
        res.files.insert(fn, text_hash(text));
        std::vector<qsizetype> nls;                      // (line numbers: where the pop stands — the checks)
        for (qsizetype i = 0; i < text.size(); ++i)
            if (text[i] == u'\n')
                nls.push_back(i);
        auto line_of = [&nls](qsizetype pos) {
            return static_cast<long long>(std::upper_bound(nls.begin(), nls.end(), pos - 1) - nls.begin()) + 1;
        };
        for (const Node &n : script::parse(text)) {
            if (!n.has_key || !py::isdigit(n.key) || !n.is_block) {
                const QString what = (n.has_key && !n.key.isEmpty()) ? n.key : slice(text, n.kstart, n.vend).left(30);
                res.problems << u"%1: unexpected top-level entry %2"_s.arg(fn, py::repr_str(what));
                continue;
            }
            const long long pid = py::to_int(n.key);
            PopProvince *entry = res.provinces.get(pid);
            if (!entry) {
                PopProvince e;
                e.file = fn;
                e.block = {n.kstart, n.vend};
                e.open = n.vstart;
                e.close = n.vend - 1;
                e.files = {fn};
                e.line = line_of(n.kstart);
                e.other = std::any_of(n.kids.begin(), n.kids.end(), [](const Node &c) { return !keyed_block(c); });
                res.provinces.insert(pid, e);
                entry = res.provinces.get(pid);
            } else {
                entry->files << fn;
                res.problems << u"province %1 is defined in several files: %2"_s.arg(pid).arg(entry->files.join(u", "_s));
            }
            for (const Node &c : n.kids) {
                if (!keyed_block(c))
                    continue;                            // non-pop content is left untouched on disk
                Pop pop;
                pop.type = c.key;
                std::optional<std::pair<qsizetype, qsizetype>> size_span;
                for (const Node &f : c.kids) {
                    if (f.has_key && (f.key == u"culture" || f.key == u"religion") && !f.is_block) {
                        (f.key == u"culture" ? pop.culture : pop.religion) = f.value;
                    } else if (f.has_key && f.key == u"size" && !f.is_block && !size_span) {
                        bool ok = false;
                        if (auto v = py::to_float(f.value)) {
                            try {
                                pop.size = py::round_int(*v);
                                size_span = std::make_pair(f.vstart, f.vend);
                                ok = true;
                            } catch (const py::ValueError &) {   // (nan: Python's ValueError too)
                            }
                        }
                        if (!ok)
                            pop.extra.emplace_back(u"size"_s, f.value);
                    } else {
                        pop.extra.emplace_back(f.has_key ? OptStr(f.key) : std::nullopt, slice(text, f.vstart, f.vend));
                    }
                }
                auto o = std::make_shared<PopOrigin>();
                o->file = fn;
                o->span = {c.kstart, c.vend};
                o->size_span = size_span;
                o->line = line_of(c.kstart);
                o->type = pop.type;
                o->culture = pop.culture;
                o->religion = pop.religion;
                o->size = pop.size;
                o->extra = pop.extra;
                pop.o = o;
                entry->pops.push_back(std::move(pop));
            }
        }
    }
    return res;
}

QString _render_pop(const Pop &pop, const QString &ind, const QString &ind2, const QString &nl)
{
    QStringList lines{u"%1%2 = {"_s.arg(ind, pop.type)};
    if (pop.culture && !pop.culture->isEmpty())
        lines << u"%1culture = %2"_s.arg(ind2, *pop.culture);
    if (pop.religion && !pop.religion->isEmpty())
        lines << u"%1religion = %2"_s.arg(ind2, *pop.religion);
    lines << u"%1size = %2"_s.arg(ind2).arg(pop.size);
    for (const auto &[k, v] : pop.extra)
        lines << ind2 + (k ? u"%1 = %2"_s.arg(*k, v) : v);
    lines << ind + u'}';
    return lines.join(nl);
}

std::optional<QString> _line_indent(const QString &text, qsizetype pos)
{
    const qsizetype ls = rfind_nl(text, pos) + 1;
    const QString s = slice(text, ls, pos);
    if (blank(s))
        return s;
    return std::nullopt;
}

std::pair<QString, QString> _file_style(const QString &text)
{
    const QString nl = (text.contains(u"\r\n"_s) || text.isEmpty()) ? u"\r\n"_s : u"\n"_s;
    // re.search(r"\n([ \t]+)[A-Za-z]", text)
    QString unit = u"    "_s;
    const qsizetype n = text.size();
    for (qsizetype i = text.indexOf(u'\n'); i >= 0; i = text.indexOf(u'\n', i + 1)) {
        qsizetype j = i + 1;
        while (j < n && (text[j] == u' ' || text[j] == u'\t'))
            ++j;
        if (j > i + 1 && j < n && ((text[j] >= u'A' && text[j] <= u'Z') || (text[j] >= u'a' && text[j] <= u'z'))) {
            const QString g = text.mid(i + 1, j - i - 1);
            unit = g.startsWith(u'\t') ? u"\t"_s : QString(std::min<qsizetype>(g.size(), 8), u' ');
            break;
        }
    }
    return {nl, unit};
}

std::pair<qsizetype, qsizetype> _delete_span(const QString &text, qsizetype s, qsizetype e)
{
    // Extend a span to whole lines when it sits alone on its lines, and swallow the blank separator lines
    // directly above it.
    qsizetype ls = rfind_nl(text, s) + 1;
    if (blank(slice(text, ls, s))) {
        qsizetype j = e;
        while (j < text.size() && (text[j] == u' ' || text[j] == u'\t'))
            ++j;
        qsizetype end;
        if (text.mid(j, 2) == u"\r\n")
            end = j + 2;
        else if ((j < text.size() && text[j] == u'\n') || j >= text.size())
            end = std::min(j + 1, text.size());
        else
            return {s, e};
        while (ls > 0) {
            const qsizetype prev = rfind_nl(text, ls - 1) + 1;
            const QString seg = slice(text, prev, ls);
            if (!blank(seg) || seg.count(u'\n') == 0)
                break;
            ls = prev;
        }
        return {ls, end};
    }
    return {s, e};
}

namespace {
struct Edit {
    qsizetype s, e;
    QString rep;
};
}

std::pair<QStringList, OptStr> save_pops(Mod &mod, const QString &scenario,
                                         const OrderedMap<long long, PopProvince> &provinces_src,
                                         const OrderedMap<QString, QString> &file_hashes,
                                         const OrderedMap<long long, std::vector<Pop>> &current,
                                         const std::vector<long long> &dirty,
                                         const OrderedMap<long long, QString> &target_files,
                                         const QString &backup_root)
{
    // Write changed provinces back surgically. Returns (written_files, backup_dir).
    const QString d = mod.scenario_dir(scenario);        // (written always into the mod's own folder)
    const OrderedMap<QString, QString> layers = mod.pop_files(scenario);
    OrderedMap<QString, std::vector<Edit>> edits;        // fn -> [(start, end, text)]
    OrderedMap<QString, std::vector<long long>> appends; // fn -> [pid]
    OrderedMap<QString, QString> texts;

    auto text_of = [&](const QString &fn) -> const QString & {
        if (!texts.contains(fn)) {
            QString path = join(d, fn);
            if (!exists(path))                           // a base game file: its whole text goes into the mod
                path = layers.value(fn, path);
            texts.insert(fn, exists(path) ? script::read_text(path) : QString());
            const QString *h = file_hashes.get(fn);
            if (h && text_hash(*texts.get(fn)) != *h)
                throw std::runtime_error((fn + u" was changed on disk after it was loaded.\n"
                                                u"Reload the scenario before saving."_s).toStdString());
        }
        return *texts.get(fn);
    };
    static const std::vector<Pop> NONE;
    for (long long pid : dirty) {
        const std::vector<Pop> *pp = current.get(pid);
        const std::vector<Pop> &pops = pp ? *pp : NONE;
        const PopProvince *src = provinces_src.get(pid);
        if (!src) {
            if (!pops.empty()) {
                const QString *tf = target_files.get(pid);
                if (!tf)
                    throw std::out_of_range("KeyError: target_files");
                appends[*tf].push_back(pid);
            }
            continue;
        }
        QHash<const PopOrigin *, const Pop *> kept;
        std::vector<const Pop *> new_pops;
        for (const Pop &p : pops) {
            if (p.o)
                kept.insert(p.o.get(), &p);
            else
                new_pops.push_back(&p);
        }
        const bool all_here = std::all_of(src->pops.begin(), src->pops.end(),
                                          [&](const Pop &op) { return op.o->file == src->file; });
        if (pops.empty() && !src->other && all_here) {
            // Province emptied completely: leave a clean empty block "N = {\n}"
            const QString &text = text_of(src->file);
            edits[src->file].push_back({src->open + 1, src->close, _file_style(text).first});
            continue;
        }
        OptStr pop_indent;
        for (const Pop &op : src->pops) {
            const PopOrigin &o = *op.o;
            const QString &text = text_of(o.file);
            const auto [nl, unit] = _file_style(text);
            const auto ind = _line_indent(text, o.span.first);
            if (!pop_indent && ind && o.file == src->file)
                pop_indent = ind;
            const Pop *p = kept.value(&o, nullptr);
            if (!p) {
                const auto [s, e] = _delete_span(text, o.span.first, o.span.second);
                edits[o.file].push_back({s, e, QString()});
            } else if (p->type != o.type || p->culture != o.culture || p->religion != o.religion ||
                       p->extra != o.extra || (!o.size_span && p->size != o.size)) {
                const QString i1 = ind ? *ind : unit;
                edits[o.file].push_back({o.span.first, o.span.second, _render_pop(*p, QString(), i1 + unit, nl)});
            } else if (p->size != o.size) {
                edits[o.file].push_back({o.size_span->first, o.size_span->second, QString::number(p->size)});
            }
        }
        if (!new_pops.empty()) {
            const QString fn = src->file;
            const QString &text = text_of(fn);
            const auto [nl, unit] = _file_style(text);
            const qsizetype close = src->close;
            const auto bi = _line_indent(text, src->block.first);
            const QString bind = (bi && !bi->isEmpty()) ? *bi : QString();
            const QString ind = pop_indent ? *pop_indent : bind + unit;
            QStringList parts;
            for (const Pop *p : new_pops)
                parts << _render_pop(*p, ind, ind + unit, nl);
            const QString body = parts.join(nl);
            const qsizetype ls = rfind_nl(text, close) + 1;
            if (blank(slice(text, ls, close)) && ls > src->block.first)
                edits[fn].push_back({ls, ls, body + nl});
            else
                edits[fn].push_back({close, close, nl + body + nl + bind});
        }
    }
    QStringList touched;
    for (const auto &kv : edits)
        touched << kv.first;
    for (const auto &kv : appends)
        if (!touched.contains(kv.first))
            touched << kv.first;
    touched = sorted(touched);
    if (touched.isEmpty())
        return {{}, std::nullopt};
    for (const QString &fn : touched)
        text_of(fn);                                     // verify hashes before writing anything

    const QString bdir = join(backup_root, {scenario, stamp_now()});
    fs::makedirs(bdir);
    fs::makedirs(d);
    for (const QString &fn : touched)
        if (exists(join(d, fn)))
            fs::copy2(join(d, fn), join(bdir, fn));

    for (const QString &fn : touched) {
        QString text = *texts.get(fn);
        const auto [nl, unit] = _file_style(text);
        std::vector<Edit> es = edits.value(fn);
        std::stable_sort(es.begin(), es.end(), [](const Edit &a, const Edit &b) {
            return std::make_pair(-a.s, -a.e) < std::make_pair(-b.s, -b.e);
        });
        for (const Edit &ed : es)
            text = slice(text, 0, ed.s) + ed.rep + text.mid(ed.e);
        for (long long pid : appends.value(fn)) {
            if (!text.isEmpty() && !text.endsWith(u'\n'))
                text += nl;
            QStringList lines{u"%1 = {"_s.arg(pid)};
            for (const Pop &p : *current.get(pid))
                lines << _render_pop(p, unit, unit + unit, nl);
            lines << u"}"_s;
            text += nl + lines.join(nl) + nl;
        }
        script::write_text(join(d, fn), text);
    }
    return {touched, bdir};
}

QStringList strip_pop_blocks(Mod &mod, const QSet<long long> &pids, const QString &backup_root)
{
    // Take the blocks of the provinces `pids` out of the pop files of EVERY start date (deleted provinces: a block
    // for a number that is no province must not stay behind). Each changed file is backed up. -> files written.
    QStringList written;
    if (pids.isEmpty())
        return written;
    QStringList marks;
    for (long long p : pids)
        marks << QString::number(p);
    const QString stamp = stamp_now();
    for (const QString &sc : mod.pop_scenarios()) {      // (every start date, the base game's files too)
        const QString d = join(mod.pops_dir(), sc);
        const OrderedMap<QString, QString> layers = mod.pop_files(sc);
        QStringList names;
        for (const auto &kv : layers)
            names << kv.first;
        for (const QString &fn : sorted_lower(names)) {
            const QString src = *layers.get(fn);
            const QString path = join(d, fn);             // (written into the mod, also when read from the game)
            QString text;
            try {
                text = script::read_text(src);
            } catch (const fs::OSError &) {
                continue;
            }
            if (!std::any_of(marks.begin(), marks.end(), [&](const QString &m) { return text.contains(m); }))
                continue;
            std::vector<std::pair<qsizetype, qsizetype>> cuts;
            for (const Node &n : script::parse(text))
                if (keyed_block(n) && py::isdigit(n.key) && pids.contains(py::to_int(n.key)))
                    cuts.emplace_back(n.kstart, n.vend);
            if (cuts.empty())
                continue;
            const QString bdir = join(backup_root, {u"deleted_provinces"_s, stamp, u"pops"_s, sc});
            fs::makedirs(bdir);
            fs::copy2(src, join(bdir, fn));
            std::stable_sort(cuts.begin(), cuts.end(), std::greater<>());
            for (auto [s, e] : cuts) {
                std::tie(s, e) = _delete_span(text, s, e);
                text = slice(text, 0, s) + text.mid(e);
            }
            fs::makedirs(d);
            script::write_text(path, text);
            written << path;
        }
    }
    return written;
}

OptStr save_regions(Mod &mod, const OrderedMap<QString, std::vector<long long>> &changed, const QString &backup_root)
{
    // changed: {region_id: [pid, ...]} new province order. Rewrites only those entries.
    QString path = mod.p({u"map"_s, u"region.txt"_s});
    QString text = script::read_text(path);
    const bool copied = !mod.in_mod(path);
    if (copied) {
        // the mod uses the base game's region.txt: the first change makes the mod's own copy — the base game's
        // file is never touched; the game then reads the mod's
        path = join(mod.root, {u"map"_s, u"region.txt"_s});
        fs::makedirs(pypath::dirname(path));
    }
    if (text_hash(text) != mod.region_hash)
        throw std::runtime_error("region.txt was changed on disk after it was loaded. Restart the editor before saving.");
    std::vector<Edit> edits;
    QSet<QString> found, gone;
    auto ids_text = [](const std::vector<long long> &ids) {
        QStringList s;
        for (long long i : ids)
            s << QString::number(i);
        return s.join(u' ');
    };
    for (const Node &n : script::parse(text)) {
        if (!(n.has_key && changed.contains(n.key) && n.is_block))
            continue;
        found.insert(n.key);
        const std::vector<long long> &ids = *changed.get(n.key);
        if (!ids.empty() || mod.utility.contains(n.key)) {
            edits.push_back({n.vstart, n.vend, u"{ "_s + ids_text(ids) + u" }"_s});
            continue;
        }
        // a state without provinces is gone: its whole line (with a comment behind it) leaves the file
        qsizetype s = rfind_nl(text, n.kstart) + 1;
        if (!blank(slice(text, s, n.kstart)))
            s = n.kstart;
        qsizetype e = n.vend;
        const qsizetype eol = text.indexOf(u'\n', e);
        const QString rest = py::strip(slice(text, e, eol >= 0 ? eol : text.size()));
        if (rest.isEmpty() || rest.startsWith(u'#'))
            e = eol >= 0 ? eol + 1 : text.size();
        edits.push_back({s, e, QString()});
        gone.insert(n.key);
    }
    // new states (created in the editor) are appended at the end of the file
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QStringList fresh;
    for (const auto &[k, ids] : changed)
        if (!found.contains(k) && !ids.empty())
            fresh << k;
    if (!fresh.isEmpty()) {
        QString tail = (text.endsWith(u'\n') || text.endsWith(u'\r') || text.isEmpty()) ? QString() : nl;
        for (const QString &k : fresh)
            tail += u"%1%2 = { %3 }%4"_s.arg(nl, k, ids_text(*changed.get(k)), nl);
        edits.push_back({text.size(), text.size(), tail});
    }
    if (edits.empty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"map"_s, stamp_now()});
    fs::makedirs(bdir);
    if (!copied)
        fs::copy2(path, join(bdir, u"region.txt"_s));
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return -a.s < -b.s; });
    for (const Edit &ed : edits)
        text = slice(text, 0, ed.s) + ed.rep + text.mid(ed.e);
    script::write_text(path, text);
    mod.region_hash = text_hash(text);
    for (const QString &k : fresh)
        mod.region_keys_on_disk.insert(k);
    for (const QString &k : gone)
        mod.region_keys_on_disk.remove(k);
    QStringList order;
    for (const QString &k : mod.region_order)
        if (!gone.contains(k))
            order << k;
    mod.region_order = order;
    return bdir;
}

}  // namespace moddata