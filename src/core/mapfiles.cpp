// mapfiles.py — climate / continent membership files and default.map. Line by line.
#include "core/mapfiles.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QFile>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace mapfiles {

using pypath::join;
using script::Node;
using script::Nodes;

const int PER_LINE = 25;
const OrderedMap<QString, QStringList> VANILLA_FALLBACK = [] {
    OrderedMap<QString, QStringList> m;
    m.insert(u"climate"_s, {u"mild_climate"_s, u"temperate_climate"_s, u"harsh_climate"_s, u"inhospitable_climate"_s});
    m.insert(u"continent"_s, {u"europe"_s, u"asia"_s, u"africa"_s, u"north_america"_s, u"south_america"_s, u"oceania"_s});
    return m;
}();
const QString NOTE_MARK = u"never remove"_s;
const OrderedMap<QString, QString> NOTES = [] {
    OrderedMap<QString, QString> m;
    m.insert(u"climate"_s,
             u"# Vanilla climate: never remove it, the base game expects it to exist. (ModEditor will not delete it.)"_s);
    m.insert(u"continent"_s, u"# Vanilla continent: never remove it, base-game events, decisions and triggers refer to it. "
                             u"(ModEditor will not delete it.)"_s);
    return m;
}();
const QStringList COMMON_MODIFIERS = {
    u"farm_rgo_size"_s, u"farm_rgo_eff"_s, u"mine_rgo_size"_s, u"mine_rgo_eff"_s, u"max_attrition"_s, u"attrition"_s,
    u"local_rgo_output"_s, u"local_rgo_throughput"_s, u"local_factory_output"_s, u"local_factory_throughput"_s,
    u"local_artisan_output"_s, u"local_repair"_s, u"local_ship_build"_s, u"supply_limit"_s, u"movement_cost"_s,
    u"defence"_s, u"combat_width"_s, u"life_rating"_s, u"immigrant_attract"_s, u"immigrant_push"_s,
    u"population_growth"_s, u"assimilation_rate"_s, u"local_RGO_output"_s, u"boost_strongest_party"_s,
    u"poor_life_needs"_s, u"middle_life_needs"_s, u"rich_life_needs"_s, u"number_of_voters"_s, u"goods_demand"_s,
    u"education_efficiency"_s, u"tax_efficiency"_s};

static qsizetype rfind_nl(const QString &t, qsizetype end)
{
    if (end <= 0)
        return -1;
    return t.lastIndexOf(u'\n', std::min(end, t.size()) - 1);
}
static QString slice(const QString &t, qsizetype a, qsizetype b)
{
    a = std::clamp<qsizetype>(a, 0, t.size());
    b = std::clamp<qsizetype>(b, 0, t.size());
    return b > a ? t.mid(a, b - a) : QString();
}
static bool blank(const QString &s) { return py::strip(s).isEmpty(); }
static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }
static QString ids_join(const std::vector<long long> &ids, size_t from, size_t to)
{
    QStringList s;
    for (size_t i = from; i < std::min(to, ids.size()); ++i)
        s << QString::number(ids[i]);
    return s.join(u' ');
}
static void apply_edits(QString &text, std::vector<Edit> edits)
{
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) {
        return std::make_pair(-std::get<0>(a), -std::get<1>(a)) < std::make_pair(-std::get<0>(b), -std::get<1>(b));
    });
    for (const auto &[s, e, rep] : edits)
        text = slice(text, 0, s) + rep + text.mid(e);
}
static bool is_digit_value(const Node &c) { return !c.has_key && !c.is_block && py::isdigit(c.value); }

// ---------------------------------------------------------------------- MembershipFile
MembershipFile::MembershipFile(moddata::Mod &mod_, const QString &kind_) : mod(mod_), kind(kind_)
{
    path = mod.p({u"map"_s, kind + u".txt"_s});
    vanilla = _vanilla_names();
    if (fs::isfile(path))
        _load();
    on_disk = names;
    orig = snapshot();
}

QSet<QString> MembershipFile::_vanilla_names() const
{
    if (mod.base) {
        const QString p = join(*mod.base, {u"map"_s, kind + u".txt"_s});
        if (fs::isfile(p)) {
            QSet<QString> n;
            for (const Node &x : script::parse(script::read_text(p)))
                if (x.has_key && !x.key.isEmpty() && x.is_block)
                    n.insert(x.key);
            if (!n.isEmpty())
                return n;
        }
    }
    const QStringList fb = VANILLA_FALLBACK.value(kind);
    return QSet<QString>(fb.begin(), fb.end());
}

const Node *MembershipFile::_id_node(const Node &n, bool second) const
{
    // The node holding a name's province ids (None if it has none). `second`: the name was seen before.
    if (!n.is_block)
        return nullptr;
    if (kind == u"continent") {
        const Node *p = script::first(n.kids, u"provinces"_s);
        return (p && p->is_block) ? p : nullptr;
    }
    // climate: a block of bare numbers (the modifier block has key = value lines); an empty block is the modifier
    // block the first time and the id list the second time
    const bool all_bare = std::all_of(n.kids.begin(), n.kids.end(), [](const Node &c) { return !c.has_key; });
    if ((!n.kids.empty() && all_bare) || (n.kids.empty() && second))
        return &n;
    return nullptr;
}

const Node *MembershipFile::_mod_node(const Node &n, bool second) const
{
    // The node holding a name's modifiers (climate: the key=value block; continent: the node).
    if (!n.is_block)
        return nullptr;
    if (kind == u"continent")
        return &n;
    const bool any_key = std::any_of(n.kids.begin(), n.kids.end(), [](const Node &c) { return c.has_key; });
    if (any_key || (n.kids.empty() && !second))
        return &n;
    return nullptr;
}

std::vector<const Node *> MembershipFile::_scalar_children(const Node &node)
{
    std::vector<const Node *> out;
    for (const Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && c.key != u"provinces" && !c.is_block)
            out.push_back(&c);
    return out;
}

void MembershipFile::_load()
{
    const QString text = script::read_text(path);
    hash = moddata::text_hash(text);
    for (const Node &n : script::parse(text)) {
        if (!(n.has_key && !n.key.isEmpty() && n.is_block))
            continue;
        const bool second = names.contains(n.key);
        if (!second) {
            names << n.key;
            if (!lists.contains(n.key))
                lists.insert(n.key, {});
            if (!mods.contains(n.key))
                mods.insert(n.key, {});
        }
        if (const Node *idn = _id_node(n, second)) {
            auto &l = lists[n.key];
            for (long long i : script::bare_ints(idn->kids))
                l.push_back(i);
        }
        if (const Node *mn = _mod_node(n, second))
            for (const Node *c : _scalar_children(*mn))
                mods[n.key].emplace_back(c->key, c->value);
    }
}

QStringList MembershipFile::of(long long pid) const
{
    // All names a province is listed under (normally exactly one).
    QStringList out;
    for (const QString &k : names)
        if (const auto *l = lists.get(k); l && std::find(l->begin(), l->end(), pid) != l->end())
            out << k;
    return out;
}

OrderedMap<long long, QStringList> MembershipFile::membership() const
{
    OrderedMap<long long, QStringList> out;
    for (const QString &k : names)
        if (const auto *l = lists.get(k))
            for (long long pid : *l)
                out[pid] << k;
    return out;
}

QStringList MembershipFile::dirty() const
{
    QStringList out;
    static const std::vector<long long> NOL;
    static const Mods NOM;
    for (const QString &k : names) {
        const auto *l = lists.get(k), *ol = orig.lists.get(k);
        const auto *m = mods.get(k), *om = orig.mods.get(k);
        if (!orig.names.contains(k) || (l ? *l : NOL) != (ol ? *ol : NOL) || (m ? *m : NOM) != (om ? *om : NOM))
            out << k;
    }
    return out + removed();
}

QStringList MembershipFile::removed() const
{
    // Names that are in the file on disk but have been removed in the editor.
    QStringList out;
    for (const QString &k : on_disk)
        if (!names.contains(k))
            out << k;
    return out;
}

bool MembershipFile::is_vanilla(const QString &name) const { return vanilla.contains(name); }

QStringList MembershipFile::known_modifier_keys() const
{
    QSet<QString> keys(COMMON_MODIFIERS.begin(), COMMON_MODIFIERS.end());
    for (const auto &kv : mods)
        for (const auto &[k, _v] : kv.second)
            keys.insert(k);
    QStringList out(keys.begin(), keys.end());
    // sorted(..., key=str.lower); ties under lower case keep Python's set order (not fixed) — here: then by text
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        const QString la = py::lower(a), lb = py::lower(b);
        return la != lb ? la < lb : a < b;
    });
    return out;
}

void MembershipFile::assign(const QList<long long> &pids0, const QString &name)
{
    // Put provinces in exactly one list (removes them from all others, fixing duplicates).
    const QSet<long long> pids(pids0.begin(), pids0.end());
    for (const QString &k : names) {
        auto &l = lists[k];
        std::vector<long long> keep;
        for (long long p : l)
            if (!pids.contains(p))
                keep.push_back(p);
        l = keep;
    }
    std::vector<long long> add(pids.begin(), pids.end());
    std::sort(add.begin(), add.end());
    std::vector<long long> now = lists.value(name);
    now.insert(now.end(), add.begin(), add.end());
    lists.insert(name, now);
}

void MembershipFile::remove(const QStringList &rm)
{
    // Drop climates / continents (their provinces are left without one; move them first).
    QStringList bad;
    for (const QString &n : rm)
        if (is_vanilla(n))
            bad << n;
    if (!bad.isEmpty())
        throw py::ValueError(u"Vanilla %1s can't be removed: %2"_s.arg(kind, bad.join(u", "_s)).toStdString());
    for (const QString &n : rm) {
        names.removeOne(n);
        lists.remove(n);
        mods.remove(n);
    }
}

void MembershipFile::set_definitions(const OrderedMap<QString, Mods> &new_mods, const QStringList &new_names)
{
    // Replace modifiers ({name: [(k, v)]}) and add new names (empty province lists).
    for (const QString &n : new_names)
        if (!names.contains(n)) {
            names << n;
            lists.insert(n, {});
        }
    for (const auto &[n, lst] : new_mods)
        mods.insert(n, lst);
}

MembershipSnap MembershipFile::snapshot() const { return {names, lists, mods}; }

void MembershipFile::restore(const MembershipSnap &snap)
{
    names = snap.names;
    lists = snap.lists;
    mods = snap.mods;
}

std::pair<qsizetype, qsizetype> MembershipFile::_line_span(const QString &text, qsizetype s, qsizetype e)
{
    const qsizetype ls = rfind_nl(text, s) + 1;
    if (!blank(slice(text, ls, s)))
        return {s, e};
    const qsizetype j = text.indexOf(u'\n', e);
    const QString rest = py::strip(slice(text, e, j >= 0 ? j : text.size()));
    if (!rest.isEmpty() && !rest.startsWith(u'#'))
        return {ls, e};
    return {ls, j >= 0 ? j + 1 : text.size()};
}

Edit MembershipFile::_insert_before_close(const QString &text, const Node &node, const QStringList &lines,
                                          const QString &nl)
{
    const qsizetype close = node.vend - 1;
    const qsizetype ls = rfind_nl(text, close) + 1;
    QString body;
    for (const QString &l : lines)
        body += l + nl;
    if (blank(slice(text, ls, close)) && ls > node.vstart)
        return {ls, ls, body};
    return {close, close, nl + body};
}

std::vector<Edit> MembershipFile::_id_edits(const QString &text, const Node &idn, const QString &name,
                                            const QString &nl) const
{
    const QString ind = kind == u"continent" ? u"\t\t"_s : u"\t"_s;
    const std::vector<long long> &wl = *lists.get(name);
    const QSet<long long> want(wl.begin(), wl.end());
    std::vector<const Node *> have;
    for (const Node &c : idn.kids)
        if (is_digit_value(c))
            have.push_back(&c);
    QSet<long long> have_ids;
    for (const Node *c : have)
        have_ids.insert(py::to_int(c->value));
    std::vector<Edit> out;
    for (const Node *c : have) {
        if (!want.contains(py::to_int(c->value))) {
            qsizetype s = c->kstart;
            if (s > 0 && text[s - 1] == u' ')
                s -= 1;
            out.emplace_back(s, c->vend, QString());
        }
    }
    std::vector<long long> added;
    for (long long p : want)
        if (!have_ids.contains(p))
            added.push_back(p);
    std::sort(added.begin(), added.end());
    if (!added.empty())
        out.push_back(_insert_before_close(text, idn, {ind + ids_join(added, 0, added.size())}, nl));
    return out;
}

std::vector<Edit> MembershipFile::_mod_edits(const QString &text, const Node &mn, const QString &name,
                                             const QString &nl) const
{
    const Mods want = mods.value(name);
    OrderedMap<QString, QString> wd;
    for (const auto &[k, v] : want)
        if (!wd.contains(k))
            wd.insert(k, v);
    QSet<QString> seen;
    std::vector<Edit> out;
    for (const Node *c : _scalar_children(mn)) {
        if (seen.contains(c->key) || !wd.contains(c->key)) {
            const auto [s, e] = _line_span(text, c->kstart, c->vend);
            out.emplace_back(s, e, QString());
        } else if (*wd.get(c->key) != c->value) {
            out.emplace_back(c->vstart, c->vend, *wd.get(c->key));
        }
        seen.insert(c->key);
    }
    QStringList add;
    for (const auto &[k, v] : want)
        if (!seen.contains(k))
            add << u"\t%1 = %2"_s.arg(k, v);
    if (!add.isEmpty())
        out.push_back(_insert_before_close(text, mn, add, nl));
    return out;
}

QString MembershipFile::_new_entry(const QString &name, const QString &nl) const
{
    std::vector<long long> ids = lists.value(name);
    std::sort(ids.begin(), ids.end());
    QStringList rows;
    for (size_t j = 0; j < ids.size(); j += PER_LINE)
        rows << ids_join(ids, j, j + PER_LINE);
    QStringList ml;
    for (const auto &[k, v] : mods.value(name))
        ml << u"\t%1 = %2"_s.arg(k, v);
    QStringList parts;
    if (kind == u"climate") {
        parts << QString() << name + u" ="_s << u"{"_s << ml << u"}"_s << QString() << name + u" = {"_s;
        for (const QString &r : rows)
            parts << u'\t' + r;
        parts << u"}"_s << QString();
        return parts.join(nl);
    }
    parts << QString() << name + u" = {"_s << u"\tprovinces = {"_s;
    for (const QString &r : rows)
        parts << u"\t\t"_s + r;
    parts << u"\t}"_s << ml << u"}"_s << QString();
    return parts.join(nl);
}

std::pair<qsizetype, qsizetype> MembershipFile::_cut_span(const QString &text, qsizetype s, qsizetype e)
{
    // Span of a whole top-level entry: its lines, plus one blank line after it.
    qsizetype ls = rfind_nl(text, s) + 1;
    if (!blank(slice(text, ls, s)))
        ls = s;
    const qsizetype j = text.indexOf(u'\n', e);
    if (j >= 0 && blank(slice(text, e, j))) {
        e = j + 1;
        const qsizetype k = text.indexOf(u'\n', e);
        if (k >= 0 && blank(slice(text, e, k)))
            e = k + 1;
    }
    return {ls, e};
}

std::vector<Edit> MembershipFile::_note_edits(const QString &text, const std::vector<const Node *> &nodes,
                                              const QString &nl) const
{
    // Insert the 'never remove' comment above vanilla definitions that don't have it yet.
    std::vector<Edit> out;
    for (const Node *n : nodes) {
        if (!is_vanilla(n->key))
            continue;
        const qsizetype ls = rfind_nl(text, n->kstart) + 1;
        if (!blank(slice(text, ls, n->kstart)))
            continue;
        const QString head = py::rstrip(text.left(ls));
        const QString prev = head.mid(head.lastIndexOf(u'\n') + 1);
        if (py::lstrip(prev).startsWith(u'#') && py::lower(prev).contains(NOTE_MARK))
            continue;
        out.emplace_back(ls, ls, NOTES.value(kind) + nl);
    }
    return out;
}

bool MembershipFile::missing_notes() const
{
    // True if some vanilla definition in the mod's file still lacks the 'never remove' comment.
    if (!(hash && mod.in_mod(path)))
        return false;
    const QString text = script::read_text(path);
    const Nodes parsed = script::parse(text);
    std::vector<const Node *> nodes;
    for (const Node &n : parsed)
        if (n.has_key && !n.key.isEmpty() && n.is_block)
            nodes.push_back(&n);
    return !_note_edits(text, nodes, u"\n"_s).empty();
}

std::optional<QString> MembershipFile::save(const QString &backup_root, bool force)
{
    // Write the changes. `force`: also write when only the vanilla comments are missing.
    const QStringList changed = dirty();
    if (changed.isEmpty() && !(force && missing_notes()))
        return std::nullopt;
    path = mod.adopt(path);                    // (the base game's file: copied into the mod first)
    QString text = script::read_text(path);
    if (hash && moddata::text_hash(text) != *hash)     // (None: there was no file when read)
        throw std::runtime_error(("map/" + kind + ".txt was changed on disk after it was loaded. Reload before saving.")
                                     .toStdString());
    const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    const MembershipSnap &o = orig;
    const QStringList rm = removed();
    const QSet<QString> removed_set(rm.begin(), rm.end());
    std::vector<Edit> edits;
    QSet<QString> ids_done, mods_done, seen;
    const Nodes parsed = script::parse(text);
    std::vector<const Node *> nodes;
    for (const Node &n : parsed)
        if (n.has_key && !n.key.isEmpty() && n.is_block)
            nodes.push_back(&n);
    for (const Edit &e : _note_edits(text, nodes, nl))
        edits.push_back(e);
    static const std::vector<long long> NOL;
    static const Mods NOM;
    auto lists_changed = [&](const QString &k) {
        const auto *l = lists.get(k), *ol = o.lists.get(k);
        return (l ? *l : NOL) != (ol ? *ol : NOL);
    };
    auto mods_changed = [&](const QString &k) {
        const auto *m = mods.get(k), *om = o.mods.get(k);
        return (m ? *m : NOM) != (om ? *om : NOM);
    };
    for (const Node *n : nodes) {
        const bool second = seen.contains(n->key);
        seen.insert(n->key);
        if (removed_set.contains(n->key)) {
            const auto [s, e] = _cut_span(text, n->kstart, n->vend);
            edits.emplace_back(s, e, QString());
            continue;
        }
        if (!changed.contains(n->key))
            continue;
        const Node *idn = _id_node(*n, second);
        if (idn && !ids_done.contains(n->key) && lists_changed(n->key)) {
            ids_done.insert(n->key);
            for (const Edit &e : _id_edits(text, *idn, n->key, nl))
                edits.push_back(e);
        }
        const Node *mn = _mod_node(*n, second);
        if (mn && !mods_done.contains(n->key) && mods_changed(n->key)) {
            mods_done.insert(n->key);
            for (const Edit &e : _mod_edits(text, *mn, n->key, nl))
                edits.push_back(e);
        }
    }
    QString tail;
    for (const QString &k : changed) {
        if (removed_set.contains(k))
            continue;
        if (!on_disk.contains(k)) {            // brand-new climate / continent
            tail += _new_entry(k, nl);
            continue;
        }
        if (!ids_done.contains(k) && lists_changed(k)) {
            if (kind == u"climate") {          // climate that had no id list yet
                std::vector<long long> ids = lists.value(k);
                std::sort(ids.begin(), ids.end());
                QStringList parts{QString(), k + u" = {"_s};
                for (size_t j = 0; j < ids.size(); j += PER_LINE)
                    parts << u'\t' + ids_join(ids, j, j + PER_LINE);
                parts << u"}"_s << QString();
                tail += parts.join(nl);
            } else {
                throw std::runtime_error(("Continent " + k + " has no provinces = { } block to write into.").toStdString());
            }
        }
        if (!mods_done.contains(k) && mods_changed(k)) {
            if (kind == u"climate") {          // climate that had no modifier block yet
                QStringList parts{QString(), k + u" ="_s, u"{"_s};
                for (const auto &[mk, mv] : mods.value(k))
                    parts << u"\t%1 = %2"_s.arg(mk, mv);
                parts << u"}"_s << QString();
                tail += parts.join(nl);
            }
        }
    }
    if (!tail.isEmpty()) {
        const QString pre = (text.endsWith(u'\n') || text.endsWith(u'\r') || text.isEmpty()) ? QString() : nl;
        edits.emplace_back(text.size(), text.size(), pre + tail);
    }
    const QString bdir = join(backup_root, {u"map"_s, stamp_now()});
    fs::makedirs(bdir);
    fs::copy2(path, join(bdir, kind + u".txt"_s));
    apply_edits(text, edits);
    script::write_text(path, text);
    hash = moddata::text_hash(text);
    on_disk = names;
    orig = snapshot();
    return path;
}

// ---------------------------------------------------------------------- DefaultMapFile
DefaultMapFile::DefaultMapFile(moddata::Mod &mod_) : mod(mod_)
{
    path = mod.p({u"map"_s, u"default.map"_s});
    if (fs::isfile(path))
        _load(script::read_text(path));
    orig = snapshot();
}

void DefaultMapFile::_load(const QString &text)
{
    hash = moddata::text_hash(text);
    _text = text;
    for (const Node &n : script::parse(text)) {
        if (n.has_key && n.key == u"max_provinces" && !n.is_block) {
            try {
                max_provinces = py::to_int(n.value);
                _max_span = std::make_pair(n.vstart, n.vend);
            } catch (const py::ValueError &) {
            }
        } else if (n.has_key && n.key == u"sea_starts" && n.is_block) {
            _sea_node = n;
            sea = script::bare_ints(n.kids);
        } else if (n.has_key && n.key == u"definitions" && !n.is_block) {
            definitions = py::strip(n.value, u"\""_s);
            _def_span = std::make_pair(n.vstart, n.vend);
        }
    }
}

std::optional<QString> DefaultMapFile::definitions_wanted() const
{
    if (!definitions || !_def_span || !mod.base)
        return std::nullopt;                   // (no such line: left alone)
    const QString name = *definitions;
    if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\'))
        return std::nullopt;                   // a path already (another mod's folder: the map check warns)
    const QString own = join(mod.root, {u"map"_s, name});
    if (!fs::isfile(own))
        return std::nullopt;                   // the mod has no file of that name: the game's one is meant
    const QString base = join(*mod.base, {u"map"_s, name});
    if (fs::isfile(base)) {
        QFile a(own), b(base);
        if (a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.size() == b.size() &&
            a.readAll() == b.readAll())
            return std::nullopt;               // the same rows as the game's: either one gives the same picture
    }
    return u"../mod/%1/map/%2"_s.arg(pypath::basename(mod.root), name);
}

QStringList DefaultMapFile::dirty() const
{
    QStringList out;
    if (max_provinces != std::get<0>(orig))
        out << u"max_provinces"_s;
    if (sea != std::get<1>(orig))
        out << u"sea_starts"_s;
    if (new_definitions && _def_span && *new_definitions != definitions.value_or(QString()))
        out << u"definitions"_s;
    return out;
}

void DefaultMapFile::dedupe_sea()
{
    QSet<long long> seen;
    std::vector<long long> out;
    for (long long p : sea)
        if (!seen.contains(p)) {
            seen.insert(p);
            out.push_back(p);
        }
    sea = out;
}

std::optional<QString> DefaultMapFile::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    path = mod.adopt(path);                    // (the base game's file: copied into the mod first)
    QString text = script::read_text(path);
    if (moddata::text_hash(text) != hash.value_or(QString()))
        throw std::runtime_error("map/default.map was changed on disk after it was loaded. Reload before saving.");
    std::vector<Edit> edits;
    if (max_provinces != std::get<0>(orig) && _max_span)
        edits.emplace_back(_max_span->first, _max_span->second,
                           max_provinces ? QString::number(*max_provinces) : u"None"_s);
    if (new_definitions && _def_span && *new_definitions != definitions.value_or(QString()))
        edits.emplace_back(_def_span->first, _def_span->second, u'"' + *new_definitions + u'"');
    if (sea != std::get<1>(orig) && _sea_node) {
        OrderedMap<long long, long long> want;
        for (long long p : sea)
            want[p] += 1;
        for (const Node &c : _sea_node->kids) {
            if (!is_digit_value(c))
                continue;                      // keep as many of each id as wanted, cut the rest
            const long long p = py::to_int(c.value);
            if (want.value(p, 0) > 0) {
                want[p] -= 1;
            } else {
                qsizetype s = c.kstart;
                while (s > 0 && (text[s - 1] == u' ' || text[s - 1] == u'\t'))
                    s -= 1;
                edits.emplace_back(s, c.vend, QString());
            }
        }
        std::vector<long long> extra;
        for (long long p : sea)
            if (want.value(p, 0) > 0)
                extra.push_back(p);
        if (!extra.empty()) {
            const qsizetype close = _sea_node->vend - 1;
            edits.emplace_back(close, close, u' ' + ids_join(extra, 0, extra.size()) + u' ');
        }
    }
    const QString bdir = join(backup_root, {u"map"_s, stamp_now()});
    fs::makedirs(bdir);
    fs::copy2(path, join(bdir, u"default.map"_s));
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return -std::get<0>(a) < -std::get<0>(b); });
    for (const auto &[s, e, rep] : edits)
        text = slice(text, 0, s) + rep + text.mid(e);
    script::write_text(path, text);
    _load(text);
    orig = snapshot();
    return path;
}

}  // namespace mapfiles
