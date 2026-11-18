// techdata.py — technologies and inventions, edited in place. Line by line.
#include "core/techdata.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/oob.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace techdata {

using pypath::join;

const QSet<QString> TECH_BASE = {u"area"_s, u"year"_s, u"cost"_s, u"unciv_military"_s, u"ai_chance"_s};
const QSet<QString> INV_BASE = {u"limit"_s, u"chance"_s, u"effect"_s, u"news"_s};

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }

QString pretty(const OptStr &key)
{
    if (!key || key->isEmpty())
        return u"—"_s;
    return py::capitalize(py::strip(QString(*key).replace(u'_', u' ')));
}

QString effect_name(const OrderedMap<QString, QString> &loc, const QString &key)
{
    // What the game calls a modifier (MODIFIER_X, X_TECH, MILITARY_X …), else the key written out.
    const QString k = py::upper(key);
    for (const QString &cand : {key, u"MODIFIER_"_s + k, k + u"_TECH"_s, u"MILITARY_"_s + k, k, u"TECH_"_s + k}) {
        const QString *v = loc.get(cand);
        if (v && !v->isEmpty() && !v->contains(u'$') && !v->contains(u'§'))
            return *v;
    }
    return pretty(key);
}

// ---------------------------------------------------------------------- block text edits
std::shared_ptr<const Node> root(const QString &text)
{
    // a block's text -> its tree (asked for again and again; read only)
    static QHash<QString, std::shared_ptr<const Node>> roots;
    static QSet<QString> none;                    // (texts whose root is None — cached too)
    if (const auto it = roots.constFind(text); it != roots.constEnd())
        return *it;
    if (none.contains(text))
        return nullptr;
    if (roots.size() + none.size() > 8000) {
        roots.clear();
        none.clear();
    }
    auto tree = std::make_shared<const Nodes>(script::parse(text));
    if (!tree->empty() && (*tree)[0].is_block) {
        std::shared_ptr<const Node> r(tree, &(*tree)[0]);
        roots.insert(text, r);
        return r;
    }
    none.insert(text);
    return nullptr;
}

const Node &node_at(const QString &text, const std::vector<qsizetype> &ipath, std::shared_ptr<const Node> &keep)
{
    keep = root(text);
    if (!keep)
        throw std::runtime_error("AttributeError: 'NoneType' object has no attribute 'value'");
    const Node *n = keep.get();
    for (qsizetype i : ipath) {
        const qsizetype m = qsizetype(n->kids.size());
        const qsizetype j = i < 0 ? i + m : i;     // (Python: a negative index counts from the end)
        if (j < 0 || j >= m)
            throw std::out_of_range("IndexError: list index out of range");
        n = &n->kids[size_t(j)];
    }
    return *n;
}

QString _nl(const QString &text) { return text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s; }

QString set_value(const QString &text, const std::vector<qsizetype> &ipath, const QString &value)
{
    std::shared_ptr<const Node> keep;
    const Node &n = node_at(text, ipath, keep);
    return py::head(text, n.vstart) + value + py::tail(text, n.vend);
}

QString remove_node(const QString &text, const std::vector<qsizetype> &ipath)
{
    std::shared_ptr<const Node> keep;
    const Node &n = node_at(text, ipath, keep);
    const qsizetype s = n.kstart;
    qsizetype e = n.vend;
    const qsizetype ls = py::rfind(text, u'\n', s) + 1;
    qsizetype le = text.indexOf(u'\n', e);
    le = le < 0 ? text.size() : le;
    const QString after = py::strip(py::slice(text, e, le));
    if (py::strip(py::slice(text, ls, s)).isEmpty() && (after.isEmpty() || after.startsWith(u'#')))
        // the whole line (a trailing comment on it goes too)
        return py::head(text, ls) + py::tail(text, std::min(text.size(), le + 1));
    while (e < text.size() && (text[e] == u' ' || text[e] == u'\t'))
        ++e;
    return py::head(text, s) + py::tail(text, e);
}

static QString leading_ws(const QString &t, qsizetype from)   // re.match(r"[ \t]*", t[from:]).group(0)
{
    qsizetype i = std::max<qsizetype>(0, from);
    const qsizetype a = i;
    while (i < t.size() && (t[i] == u' ' || t[i] == u'\t'))
        ++i;
    return t.mid(a, i - a);
}

QString add_line(const QString &text, const std::vector<qsizetype> &ipath, const QString &key, const QString &value)
{
    // `key = value` as the last line inside the block at ipath ([] = the whole block).
    std::shared_ptr<const Node> keep;
    const Node &n = node_at(text, ipath, keep);
    const QString nl = _nl(text);
    const qsizetype close = n.vend - 1;                       // the block's '}'
    const qsizetype line_start = py::rfind(text, u'\n', close) + 1;
    const QString parent_indent = leading_ws(text, py::rfind(text, u'\n', n.kstart) + 1);
    QString child_indent = parent_indent + u'\t';
    if (!n.is_block && !n.value.isEmpty())        // (Python: `for c in n.value` walks the text's letters)
        throw std::runtime_error("AttributeError: 'str' object has no attribute 'kstart'");
    for (const Node &c : n.kids) {
        const qsizetype ls = py::rfind(text, u'\n', c.kstart) + 1;
        const QString ci = leading_ws(text, ls);
        if (!ci.isEmpty())
            child_indent = ci;
        break;
    }
    const QString fresh = child_indent + key + u" = "_s + value;
    const Node *ai = nullptr;
    if (ipath.empty())
        for (const Node &c : n.kids)
            if (is(c, u"ai_chance"_s) && c.is_block) {
                ai = &c;
                break;
            }
    if (ai) {
        // a technology's effects go above its ai_chance block
        const qsizetype ls = py::rfind(text, u'\n', ai->kstart) + 1;
        if (py::strip(py::slice(text, ls, ai->kstart)).isEmpty())
            return py::head(text, ls) + fresh + nl + py::tail(text, ls);
    }
    if (py::strip(py::slice(text, line_start, close)).isEmpty())   // '}' on its own line
        return py::head(text, line_start) + fresh + nl + py::tail(text, line_start);
    const QString before = py::slice(text, close - 1, close);
    const QString pre = (before == u" " || before == u"\t") ? QString() : u" "_s;
    return py::head(text, close) + pre + key + u" = "_s + value + u' ' + py::tail(text, close);
}

QString add_block(const QString &text, const std::vector<qsizetype> &ipath, const QString &key, const QStringList &lines)
{
    // `key = {` + lines + `}` on lines of their own, indented like the block's other lines.
    std::shared_ptr<const Node> keep;
    const Node &n = node_at(text, ipath, keep);
    const QString nl = _nl(text);
    QString child_indent = leading_ws(text, py::rfind(text, u'\n', n.kstart) + 1) + u'\t';
    if (!n.is_block && !n.value.isEmpty())
        throw std::runtime_error("AttributeError: 'str' object has no attribute 'kstart'");
    for (const Node &c : n.kids) {
        const QString ci = leading_ws(text, py::rfind(text, u'\n', c.kstart) + 1);
        if (!ci.isEmpty())
            child_indent = ci;
        break;
    }
    QString inner;
    for (const QString &l : lines)
        inner += nl + child_indent + u'\t' + l;
    return add_line(text, ipath, key, u"{"_s + inner + nl + child_indent + u'}');
}

QString check_block(const QString &text, const QString &key)
{
    // '' if the text is one block `key = { … }` with balanced braces, else what's wrong.
    static const QRegularExpression COMMENT(u"#[^\\n]*"_s);
    long long depth = 0;
    QString bare = text;                          // (a named copy: replace() returns a reference into it)
    bare.replace(COMMENT, QString());
    for (QChar ch : bare) {
        depth += ch == u'{';
        depth -= ch == u'}';
        if (depth < 0)
            return u"A '}' too many."_s;
    }
    if (depth)
        return u"%1 '{' not closed."_s.arg(depth);
    const Nodes nodes = script::parse(text);
    if (nodes.size() != 1 || !nodes[0].is_block)
        return u"It must be exactly one block: %1 = { … }"_s.arg(key);
    if (!(nodes[0].has_key && nodes[0].key == key))
        return u"The name must stay %1 (renaming a technology / invention breaks every reference to it)."_s.arg(key);
    return {};
}

// ---------------------------------------------------------------------- the files
TechDB::TechDB(moddata::Mod &mod_) : mod(mod_) { load(); }

QStringList TechDB::_files(const QString &folder) const
{
    OrderedMap<QString, QString> out;
    QStringList bases;
    if (mod.base && !mod.base->isEmpty())
        bases << *mod.base;
    bases << mod.root;
    for (const QString &base : bases) {
        const QString d = join(base, folder);
        if (!fs::isdir(d))
            continue;
        QStringList names = fs::listdir(d);
        std::sort(names.begin(), names.end());
        for (const QString &fn : names)
            if (py::lower(fn).endsWith(u".txt"))
                out.insert(py::lower(fn), join(d, fn));
    }
    QStringList ks;
    for (const auto &kv : out)
        ks << kv.first;
    std::sort(ks.begin(), ks.end());
    QStringList paths;
    for (const QString &k : ks)
        paths << out.value(k);
    return paths;
}

void TechDB::load()
{
    entries.clear();
    order.clear();
    order.insert(u"tech"_s, {});
    order.insert(u"inv"_s, {});
    generation += 1;
    for (const auto &[kind, folder] : {std::pair{u"tech"_s, u"technologies"_s}, std::pair{u"inv"_s, u"inventions"_s}}) {
        for (const QString &path : _files(folder)) {
            const QString text = script::read_text(path);
            for (const Node &n : script::parse(text)) {
                if (has(n) && n.is_block && !entries.contains(n.key)) {
                    QStringList &o = order[kind];
                    entries.insert(n.key, TechEntry{kind, path, {n.kstart, n.vend}, py::slice(text, n.kstart, n.vend), o.size()});
                    o << n.key;
                }
            }
        }
    }
}

OrderedMap<QString, QString> fields(const QString &text)
{
    OrderedMap<QString, QString> f;
    if (const auto r = root(text))
        for (const Node &c : r->kids)
            if (has(c) && !c.is_block)
                f.insert(c.key, c.value);
    return f;
}

static const Node *limit_of(const Node *r)
{
    if (r)
        for (const Node &c : r->kids)
            if (is(c, u"limit"_s) && c.is_block)
                return &c;
    return nullptr;
}

QStringList tech_refs(const QString &text, const QSet<QString> &techs)
{
    // Technologies an invention's limit asks for (anywhere inside it).
    const auto r = root(text);
    QStringList out;
    std::function<void(const Nodes &)> walk = [&](const Nodes &nodes) {
        for (const Node &c : nodes) {
            if (c.has_key && techs.contains(c.key) && !out.contains(c.key))
                out << c.key;
            if (c.is_block)
                walk(c.kids);
        }
    };
    if (const Node *lim = limit_of(r.get()))
        walk(lim->kids);
    return out;
}

bool simple_limit(const QString &text, const QSet<QString> &techs)
{
    // True when the limit is only `tech = 1` lines (then the editor can change it).
    const auto r = root(text);
    const Node *lim = limit_of(r.get());
    if (!lim)
        return false;
    return std::all_of(lim->kids.begin(), lim->kids.end(),
                       [&](const Node &c) { return c.has_key && techs.contains(c.key) && !c.is_block; });
}

QString replace_key(const QString &text, const std::vector<qsizetype> &ipath, const QString &new_key)
{
    std::shared_ptr<const Node> keep;
    const Node &n = node_at(text, ipath, keep);
    return py::head(text, n.kstart) + new_key + py::tail(text, n.kstart + n.key.size());
}

long long year_of(const QString &text)
{
    // int(float(fields(text).get("year", 0))), a ValueError -> 0
    const auto f = fields(text);
    const QString *y = f.get(u"year"_s);
    if (!y)
        return 0;
    const auto v = py::to_float(*y);
    return v ? py::trunc_int(*v) : 0;
}

QString mod_target(const moddata::Mod &mod, const QString &path)
{
    // Where a change to the file `path` is written: the file itself when it is the mod's — a base game file gets its
    // own copy in the mod, at the same place below the mod's folder. (Tester's report 2026-10-15: a mod kept in the
    // game's own mod folder is below the game's folder too — the mod is asked first.)
    if (mod.in_mod(path))
        return path;
    if (mod.base && !mod.base->isEmpty() &&
        pypath::normcase(pypath::abspath(path)).startsWith(pypath::normcase(pypath::abspath(*mod.base)) + u'\\'))
        return join(mod.root, pypath::relpath(path, *mod.base));
    throw std::runtime_error((path + u" is neither the mod's file nor one of the base game's — not written."_s).toStdString());
}

// ---------------------------------------------------------------------- TechEdits
QStringList TechEdits::keys() const
{
    QStringList out;
    for (const QString &k : db.techs())
        if (!removed.contains(k))
            out << k;
    for (const auto &[k, v] : added)
        if (!db.entries.contains(k) && v.kind == u"tech")
            out << k;
    return out;
}

QStringList TechEdits::inventions() const
{
    // The inventions as they are now: the files' (without the deleted) and the new ones.
    QStringList out;
    for (const QString &k : db.inventions())
        if (!removed.contains(k))
            out << k;
    for (const auto &[k, v] : added)
        if (v.kind == u"inv")
            out << k;
    return out;
}

QString TechEdits::file_of(const QString &key) const
{
    if (const Added *a = added.get(key))
        return a->file;
    return db.entries.get(key)->file;
}

bool TechEdits::exists(const QString &key) const
{
    return (db.entries.contains(key) && !removed.contains(key)) || added.contains(key);
}

void TechEdits::remove(const QString &key)
{
    // A technology goes (a new one is simply dropped; one from the files is deleted from its file on Apply). The
    // caller drops a new one's name too (LocEdits.drop).
    if (added.contains(key)) {
        added.remove(key);
    } else {
        removed.insert(key);
        want.remove(key);
    }
    for (auto &kv : order)
        kv.second.removeAll(key);
}

bool TechEdits::move_to(const OptStr &area, const QString &key, const QString &target)
{
    // Drag and drop: `key` to the place of `target` — only among technologies of the same year.
    const QStringList old = column(area);
    if (!old.contains(key) || !old.contains(target) || key == target)
        return false;
    const long long y = year_of(text(key));
    if (year_of(text(target)) != y)
        return false;
    // dropped on a later card: after it; on an earlier card: before it
    QStringList col = old;
    col.removeAll(key);
    const qsizetype ti = col.indexOf(target);
    col.insert(old.indexOf(key) < old.indexOf(target) ? ti + 1 : ti, key);
    order.insert(area, col);
    return true;
}

QString TechEdits::text(const QString &key) const
{
    if (const Added *a = added.get(key))
        return a->text;
    if (const QString *w = want.get(key))
        return *w;
    return db.entries.get(key)->text;
}

void TechEdits::set(const QString &key, const QString &t)
{
    if (Added *a = added.get(key))
        a->text = t;
    else if (t == db.entries.get(key)->text)
        want.remove(key);
    else
        want.insert(key, t);
}

void TechEdits::add(const QString &key, const QString &file, const QString &t, const QString &k)
{
    added.insert(key, Added{file, t, k});
}

OptStr TechEdits::area_of(const QString &key) const
{
    const auto f = fields(text(key));
    const QString *a = f.get(u"area"_s);
    return a ? OptStr(*a) : std::nullopt;
}

QStringList TechEdits::column(const OptStr &area, const OrderedMap<QString, OptStr> *areas_) const
{
    // The technologies of an area, top first: by year; same years in the chosen / file order.
    OrderedMap<QString, OptStr> own;
    if (!areas_) {
        for (const QString &k : keys())
            own.insert(k, area_of(k));
        areas_ = &own;
    }
    const auto &areas = *areas_;
    auto area_is = [&](const QString &k) { return areas.contains(k) ? *areas.get(k) == area : !area; };   // .get(k) == area
    QStringList base;
    for (const QString &k : order.value(area))
        if (area_is(k) && exists(k))
            base << k;
    for (const QString &k : keys())
        if (area_is(k) && !base.contains(k))
            base << k;
    QHash<QString, long long> years;
    for (const QString &k : base)
        years.insert(k, year_of(text(k)));
    std::stable_sort(base.begin(), base.end(), [&](const QString &a, const QString &b) { return years[a] < years[b]; });
    return base;
}

void TechEdits::move(const OptStr &area, const QString &key, long long step)
{
    // Same-year technologies: `key` one place earlier (-1) / later (+1).
    QStringList col = column(area);
    const qsizetype i = col.indexOf(key);
    if (i < 0)
        throw py::ValueError("'" + key.toStdString() + "' is not in list");
    const qsizetype j = i + step;
    if (0 <= j && j < col.size() && year_of(text(col[j])) == year_of(text(key))) {
        col.swapItemsAt(i, j);
        order.insert(area, col);
    }
}

store::Snap TechEdits::snapshot() const { return store::box(TechSnap{want, added, order, removed}); }

void TechEdits::restore(const store::Snap &snap)
{
    const TechSnap &s = store::unbox<TechSnap>(snap);
    want = s.want;
    added = s.added;
    order = s.order;
    removed = s.removed;
}

OrderedMap<QString, QStringList> TechEdits::_file_sequences() const
{
    // {file: [tech keys in the order the file should have them]} (areas as they first appear in the file).
    OrderedMap<QString, OptStr> areas;
    OrderedMap<QString, QString> home;
    for (const QString &k : keys()) {
        areas.insert(k, area_of(k));
        home.insert(k, db.entries.contains(k) ? db.entries.get(k)->file : added.get(k)->file);
    }
    OrderedMap<QString, std::vector<OptStr>> files;
    for (const QString &k : db.techs()) {
        const QString *hk = home.get(k);
        if (!hk)                                  // (Python: home[k] — a removed technology is a KeyError)
            throw std::out_of_range(("KeyError: '" + k + "'").toStdString());
        auto &lst = files[*hk];
        const auto f = fields(db.entries.get(k)->text);
        const QString *a = f.get(u"area"_s);
        const OptStr area = a ? OptStr(*a) : std::nullopt;
        if (std::find(lst.begin(), lst.end(), area) == lst.end())
            lst.push_back(area);
    }
    OrderedMap<QString, QStringList> out;
    for (const auto &[f, area_order] : files) {
        QStringList seq;
        std::vector<OptStr> extra;
        for (const QString &k : keys())
            if (home.value(k) == f && std::find(area_order.begin(), area_order.end(), *areas.get(k)) == area_order.end())
                extra.push_back(*areas.get(k));
        std::vector<OptStr> all = area_order;
        for (size_t i = 0; i < extra.size(); ++i)
            if (std::find(extra.begin(), extra.begin() + qsizetype(i), extra[i]) == extra.begin() + qsizetype(i))
                all.push_back(extra[i]);
        for (const OptStr &a : all)
            for (const QString &k : column(a, &areas))
                if (home.value(k) == f)
                    seq << k;
        out.insert(f, seq);
    }
    for (const auto &[k, v] : added) {
        if (v.kind == u"inv")
            out[v.file] << k;                     // a new invention: after what the file holds
        else if (!out.contains(v.file))
            out.insert(v.file, {k});
    }
    return out;
}

QStringList TechEdits::dirty() const
{
    // (Python keeps a memo of this by its inputs — the answer is the same)
    QStringList out, a, b, c;
    for (const auto &[k, t] : want)
        if (t != db.entries.get(k)->text)
            a << k;
    for (const auto &kv : added)
        b << kv.first;
    for (const QString &k : removed)
        c << u"removed "_s + k;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    std::sort(c.begin(), c.end());
    out << a << b << c;
    for (const auto &[f, seq] : _file_sequences()) {
        QStringList orig, have;
        for (const QString &k : db.techs())
            if (db.entries.get(k)->file == f)
                orig << k;
        for (const QString &k : seq)
            if (db.entries.contains(k))
                have << k;
        if (have != orig)
            out << u"order of "_s + pypath::basename(f);
    }
    return out;
}

std::optional<QStringList> TechEdits::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    const QString bdir = join(backup_root, {u"technologies"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    const auto seqs = _file_sequences();
    QSet<QString> touched;
    for (const auto &kv : want)
        touched.insert(db.entries.get(kv.first)->file);
    for (const auto &kv : added)
        touched.insert(kv.second.file);
    for (const QString &k : removed)
        touched.insert(db.entries.get(k)->file);
    for (const auto &[f, seq] : seqs) {
        QStringList orig, have;
        for (const QString &k : db.techs())
            if (db.entries.get(k)->file == f)
                orig << k;
        for (const QString &k : seq)
            if (db.entries.contains(k))
                have << k;
        if (have != orig)
            touched.insert(f);
    }
    QStringList paths(touched.begin(), touched.end());
    std::sort(paths.begin(), paths.end());
    QStringList written;
    for (const QString &path : paths) {
        QString text = fs::isfile(path) ? script::read_text(path) : QString();
        const QString dst = mod_target(mod, path);
        if (fs::isfile(dst)) {
            fs::makedirs(bdir);
            fs::copy2(dst, join(bdir, pypath::basename(dst)));
        }
        const QString nl = _nl(text);
        // the file's blocks (technologies and inventions) in their slots; technologies in the wanted order
        QStringList slots_;
        for (const QString &k : db.order.value(u"tech"_s) + db.order.value(u"inv"_s))
            if (db.entries.get(k)->file == path)
                slots_ << k;
        std::stable_sort(slots_.begin(), slots_.end(), [&](const QString &a, const QString &b) {
            return db.entries.get(a)->span.first < db.entries.get(b)->span.first;
        });
        QStringList tech_slots;
        for (const QString &k : slots_)
            if (db.entries.get(k)->kind == u"tech")
                tech_slots << k;
        const QStringList seq = seqs.contains(path) ? seqs.value(path) : tech_slots;
        QHash<QString, QString> fill;
        for (qsizetype i = 0; i < tech_slots.size() && i < seq.size(); ++i)
            fill.insert(tech_slots[i], seq[i]);
        for (const QString &k : slots_)
            if (db.entries.get(k)->kind == u"inv")
                fill.insert(k, k);
        std::vector<std::tuple<qsizetype, qsizetype, QString>> edits;
        for (const QString &slot : slots_) {
            const TechEntry &se = *db.entries.get(slot);
            const auto [s, e] = se.span;
            if (se.kind == u"tech" && tech_slots.indexOf(slot) >= seq.size()) {
                // fewer technologies than before (a row removed): the block and its line go
                qsizetype ls = py::rfind(text, u'\n', s) + 1;
                qsizetype le = text.indexOf(u'\n', e);
                le = le < 0 ? text.size() : le + 1;
                while (ls > 1 && py::strip(py::slice(text, py::rfind(text, u'\n', ls - 1) + 1, ls)).isEmpty())
                    ls = py::rfind(text, u'\n', ls - 1) + 1;          // the blank lines above it too
                edits.emplace_back(ls, le, QString());
                continue;
            }
            if (se.kind == u"inv" && removed.contains(slot)) {
                const qsizetype ls = py::rfind(text, u'\n', s) + 1;   // an invention deleted: its block and line go
                qsizetype le = text.indexOf(u'\n', e);
                le = le < 0 ? text.size() : le + 1;
                edits.emplace_back(ls, le, QString());
                continue;
            }
            const QString k = fill.value(slot, slot);
            const QString fresh = this->text(k);
            if (fresh != se.text)
                edits.emplace_back(s, e, fresh);
        }
        const QStringList rest = seq.mid(tech_slots.size());
        if (!rest.isEmpty()) {
            const qsizetype at = !tech_slots.isEmpty() ? db.entries.get(tech_slots.last())->span.second : text.size();
            QString ins;
            for (const QString &k : rest)
                ins += nl + nl + this->text(k);
            edits.emplace_back(at, at, ins);
        }
        std::stable_sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) {
            return std::make_pair(-std::get<0>(a), -std::get<1>(a)) < std::make_pair(-std::get<0>(b), -std::get<1>(b));
        });
        for (const auto &[s, e, fresh] : edits)
            text = py::head(text, s) + fresh + py::tail(text, e);
        fs::makedirs(pypath::dirname(dst));
        script::write_text(dst, text);
        written << dst;
    }
    // new technologies: the picture of the one above them as a start (gfx/pictures/tech/<key>.tga)
    for (const auto &[k, v] : added) {
        if (v.kind == u"inv")
            continue;
        const QStringList col = column(area_of(k));
        const qsizetype i = col.indexOf(k);
        const OptStr prev = i > 0 ? OptStr(col[i - 1]) : std::nullopt;
        if (prev && !prev->isEmpty()) {
            for (const QString &ext : {u".tga"_s, u".dds"_s}) {
                const QString src = mod.p({u"gfx"_s, u"pictures"_s, u"tech"_s, *prev + ext});
                const QString dst = join(mod.root, {u"gfx"_s, u"pictures"_s, u"tech"_s, k + ext});
                if (fs::isfile(src) && !fs::exists(dst)) {
                    fs::makedirs(pypath::dirname(dst));
                    fs::copy2(src, dst);
                    written << dst;
                    break;
                }
            }
        }
    }
    // read back what was written: every change must be in the files the game reads (the tester's report — an Apply
    // that "reverted" without a word); when not, the edits stay pending and Apply says so
    OrderedMap<QString, QString> expect;
    for (const auto &kv : want)
        expect.insert(kv.first, this->text(kv.first));
    for (const auto &kv : added)
        expect.insert(kv.first, this->text(kv.first));
    const QSet<QString> gone = removed;
    db.load();
    QStringList bad;
    for (const auto &[k, t] : expect)
        if (!db.entries.contains(k) || db.entries.get(k)->text != t)
            bad << k;
    std::sort(bad.begin(), bad.end());
    QStringList bad_gone;
    for (const QString &k : gone)
        if (db.entries.contains(k))
            bad_gone << k;
    QStringList wrong;
    for (const QString &p : written)
        if (!mod.in_mod(p))
            wrong << p;
    if (!bad.isEmpty() || !bad_gone.isEmpty() || !wrong.isEmpty()) {
        QStringList all = bad + bad_gone;
        std::sort(all.begin(), all.end());       // (Python: sorted(bad) where bad includes the removed ones)
        throw std::runtime_error(
            (u"technologies / inventions not written as wanted: %1%2 — the edits are kept, nothing is lost"_s
                 .arg(all.mid(0, 8).join(u", "_s),
                      wrong.isEmpty() ? QString() : u"; written outside the mod: %1"_s.arg(wrong.mid(0, 3).join(u", "_s))))
                .toStdString());
    }
    want.clear();
    added.clear();
    order.clear();
    removed.clear();
    if (countryhist::NationData *nd = mod.nation_data.get()) {
        try {
            nd->reinit();                         // the Nations tab's tech screen sees the new order / rows
        } catch (...) {
        }
    }
    return written;
}

// ---------------------------------------------------------------------- effect schema
std::vector<std::pair<QString, long long>> Counter::most_common() const
{
    std::vector<std::pair<QString, long long>> v(n.begin(), n.end());
    std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    return v;
}

bool _num(const QString &v) { return py::to_float(v).has_value(); }

static const QSet<QString> SKIP = {u"icon"_s,          u"naval_icon"_s,   u"priority"_s,  u"active"_s,
                                   u"limit_per_port"_s, u"min_port_level"_s, u"build_time"_s, u"supply_consumption_score"_s,
                                   u"weighted_value"_s, u"sprite"_s,       u"colonial_points"_s};

EffectSchema::EffectSchema(moddata::Mod &mod_, const TechDB &db) : mod(mod_)
{
    if (!mod.unit_types)
        mod.unit_types = std::make_shared<oob::UnitTypes>(mod);
    units = mod.unit_types.get();
    for (const auto &kv : mod.goods)
        goods << kv.first;
    std::sort(goods.begin(), goods.end());
    const QString path = mod.p({u"common"_s, u"buildings.txt"_s});
    if (fs::isfile(path))
        for (const Node &n : script::parse(script::read_text(path)))
            if (has(n) && n.is_block)
                buildings << n.key;
    Counter stats;
    std::function<void(const std::vector<const Node *> &, const OptStr &)> walk;
    walk = [&](const std::vector<const Node *> &nodes, const OptStr &parent) {
        for (const Node *cp : nodes) {
            const Node &c = *cp;
            if (!has(c))
                continue;
            Info &e = info[{parent, c.key}];
            if (c.is_block) {
                e.block += 1;
                std::vector<const Node *> kids;
                for (const Node &cc : c.kids) {
                    kids.push_back(&cc);
                    if (has(cc)) {
                        e.children.add(cc.key);
                        if (units->types.contains(c.key))
                            stats.add(cc.key);
                    }
                }
                walk(kids, c.key);
            } else {
                e.values.add(c.value);
            }
        }
    };
    for (const QString &k : db.techs()) {
        const auto r = root(db.entries.get(k)->text);
        std::vector<const Node *> l;
        if (r)
            for (const Node &c : r->kids)
                if (!(c.has_key && TECH_BASE.contains(c.key)))
                    l.push_back(&c);
        walk(l, std::nullopt);
    }
    for (const QString &k : db.inventions()) {
        const auto r = root(db.entries.get(k)->text);
        if (!r)
            continue;
        for (const Node &c : r->kids) {
            if (is(c, u"effect"_s) && c.is_block) {
                std::vector<const Node *> l;
                for (const Node &x : c.kids)
                    l.push_back(&x);
                walk(l, std::nullopt);
            } else if (has(c) && !INV_BASE.contains(c.key)) {
                walk({&c}, std::nullopt);
            }
        }
    }
    for (const auto &kv : stats.most_common())
        unit_stats << kv.first;
    // unit stats and modifiers the game knows (units/*.txt, common/*modifiers*.txt …)
    QStringList dirs;
    if (mod.base && !mod.base->isEmpty())
        dirs << join(*mod.base, u"units"_s);
    dirs << join(mod.root, u"units"_s);
    for (const QString &d : dirs) {
        if (!fs::isdir(d))
            continue;
        for (const QString &fn : fs::listdir(d)) {
            if (!py::lower(fn).endsWith(u".txt"))
                continue;
            for (const Node &n : script::parse(script::read_text(join(d, fn))))
                if (n.is_block)
                    for (const Node &c : n.kids)
                        if (has(c) && !c.is_block && _num(c.value) && !SKIP.contains(c.key) && !unit_stats.contains(c.key))
                            unit_stats << c.key;
        }
    }
    QStringList roots;
    if (mod.base && !mod.base->isEmpty())
        roots << *mod.base;
    roots << mod.root;
    for (const QString &r : roots)
        for (const QString &fn : {u"event_modifiers.txt"_s, u"static_modifiers.txt"_s, u"triggered_modifiers.txt"_s,
                                  u"crime.txt"_s, u"national_focus.txt"_s, u"nationalvalues.txt"_s}) {
            const QString p = join(r, {u"common"_s, fn});
            if (fs::isfile(p))
                for (const Node &n : script::parse(script::read_text(p)))
                    if (n.is_block)
                        for (const Node &c : n.kids)
                            if (has(c) && !c.is_block && !SKIP.contains(c.key) && _num(c.value))
                                modifiers.insert(c.key);
        }
}

QStringList EffectSchema::keys() const
{
    // Every effect a technology / invention can have: what the mod uses, every modifier the game knows, the unit
    // types (their stats), the unlocks.
    QSet<QString> s = modifiers;
    for (const auto &kv : info)
        if (!kv.first.first)
            s.insert(kv.first.second);
    for (const auto &kv : units->types)
        s.insert(kv.first);
    for (const QString &k : {u"activate_building"_s, u"activate_unit"_s, u"army_base"_s, u"navy_base"_s,
                             u"rgo_goods_output"_s, u"rgo_goods_throughput"_s, u"factory_goods_output"_s,
                             u"factory_goods_throughput"_s, u"rgo_size"_s})
        s.insert(k);
    QStringList out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

Counter EffectSchema::_values(const QString &key, const OptStr &parent) const
{
    Counter out;
    for (const auto &[pk, e] : info)
        if (pk.second == key && (!parent || pk.first == parent))
            out.update(e.values);
    return out;
}

static bool all_yes_no(const Counter &vals)
{
    for (const auto &kv : vals.n) {
        const QString l = py::lower(kv.first);
        if (l != u"yes" && l != u"no")
            return false;
    }
    return true;
}
static bool all_num(const Counter &vals)
{
    for (const auto &kv : vals.n)
        if (!_num(kv.first))
            return false;
    return true;
}

QString EffectSchema::_good_name(const QString &g) const
{
    if (const moddata::Good *e = mod.goods.get(g))
        return e->name;                           // (Python: e.get("name", pretty(g)) — a good always has its name)
    const QString *l = mod.loc.get(g);
    return (l && !l->isEmpty()) ? *l : pretty(g);
}

bool EffectSchema::_goods_block(const QString &key) const
{
    QSet<QString> kids;
    for (const auto &[pk, e] : info)
        if (pk.second == key)
            for (const auto &kv : e.children.n)
                kids.insert(kv.first);
    if (kids.isEmpty())
        return false;
    for (const QString &c : kids)
        if (!mod.goods.contains(c))
            return false;
    return true;
}

Kind EffectSchema::kind(const QString &key, const OptStr &parent) const
{
    const auto &loc = mod.loc;
    using Opts = std::vector<std::pair<QString, QString>>;
    if (key == u"activate_building") {
        Opts o;
        for (const QString &b : buildings)
            o.emplace_back(b, pretty(b));
        return {u"choice"_s, o};
    }
    if (key == u"activate_unit") {
        Opts o;
        for (const QString &u : units->land() + units->naval())
            o.emplace_back(u, units->name(u));
        return {u"choice"_s, o};
    }
    if (parent && units->types.contains(*parent))
        return {u"number"_s, std::nullopt};
    if (parent && _goods_block(*parent))
        return {u"number"_s, std::nullopt};
    if (units->types.contains(key) || key == u"army_base" || key == u"navy_base") {
        Opts o;
        for (const QString &s : unit_stats)
            o.emplace_back(s, effect_name(loc, s));
        return {u"block"_s, o};
    }
    static const QSet<QString> GOODS_KEYS = {u"rgo_goods_output"_s,   u"rgo_goods_throughput"_s,   u"factory_goods_output"_s,
                                             u"factory_goods_throughput"_s, u"rgo_size"_s,      u"artisan_goods_output"_s,
                                             u"artisan_goods_throughput"_s};
    if (GOODS_KEYS.contains(key)) {
        Opts o;
        for (const QString &g : goods)
            o.emplace_back(g, _good_name(g));
        return {u"block"_s, o};
    }
    if (parent && (*parent == u"army_base" || *parent == u"navy_base" || units->types.contains(*parent)))
        return {u"number"_s, std::nullopt};
    if (_goods_block(key)) {
        Opts o;
        for (const QString &g : goods)
            o.emplace_back(g, mod.goods.contains(g) ? mod.goods.get(g)->name : pretty(g));
        return {u"block"_s, o};
    }
    long long blocks = 0;
    for (const auto &[pk, e] : info)
        if (pk.second == key)
            blocks += e.block;
    if (blocks) {
        OrderedMap<QString, long long> kids;
        for (const auto &[pk, e] : info)
            if (pk.second == key)
                for (const auto &[c, n] : e.children.n)
                    kids[c] += n;
        std::vector<std::pair<QString, long long>> v(kids.begin(), kids.end());
        std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        Opts o;
        for (const auto &kv : v)
            o.emplace_back(kv.first, effect_name(loc, kv.first));
        return {u"block"_s, o};
    }
    const Counter vals = _values(key, parent);
    if (!vals.empty() && all_yes_no(vals))
        return {u"bool"_s, std::nullopt};
    if (!vals.empty() && all_num(vals))
        return {u"number"_s, std::nullopt};
    if (!vals.empty()) {
        Opts o;
        for (const auto &kv : vals.most_common())
            o.emplace_back(kv.first, pretty(kv.first));
        return {u"choice"_s, o};
    }
    return {u"number"_s, std::nullopt};
}

QString EffectSchema::example(const QString &key, const OptStr &parent) const
{
    const Counter vals = _values(key, parent);      // (kept: the loop reads its member)
    for (const auto &kv : vals.n)
        if (_num(kv.first))
            return kv.first;
    return u"0.1"_s;
}

const QStringList ConditionSchema::LOGIC = {u"NOT"_s, u"OR"_s, u"AND"_s};

ConditionSchema::ConditionSchema(moddata::Mod &mod_, const TechDB &db) : mod(mod_)
{
    for (const QString &k : db.techs())
        techs.insert(k);
    for (const QString &k : db.inventions())
        invs.insert(k);
    std::function<void(const Nodes &)> walk = [&](const Nodes &nodes) {
        for (const Node &c : nodes) {
            if (!has(c) || c.key == u"factor")
                continue;
            Info &e = info[c.key];
            if (c.is_block) {
                e.block += 1;
                for (const Node &cc : c.kids)
                    if (has(cc))
                        e.children.add(cc.key);
                walk(c.kids);
            } else {
                e.values.add(c.value);
            }
        }
    };
    for (const QString &k : db.techs() + db.inventions()) {
        const auto r = root(db.entries.get(k)->text);
        if (!r)
            continue;
        for (const Node &c : r->kids)
            if ((is(c, u"chance"_s) || is(c, u"ai_chance"_s)) && c.is_block)
                for (const Node &m : c.kids)
                    if (is(m, u"modifier"_s) && m.is_block)
                        walk(m.kids);
    }
}

QStringList ConditionSchema::keys() const
{
    QSet<QString> s = techs + invs;
    for (const auto &kv : info)
        s.insert(kv.first);
    for (const QString &l : LOGIC)
        s.insert(l);
    QStringList out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

QString ConditionSchema::name(const QString &key) const
{
    if (techs.contains(key) || invs.contains(key)) {
        const QString *l = mod.loc.get(key);
        return u"Has "_s + ((l && !l->isEmpty()) ? *l : pretty(key));
    }
    if (key == u"NOT")
        return u"Not …"_s;
    if (key == u"OR")
        return u"One of …"_s;
    if (key == u"AND")
        return u"All of …"_s;
    return pretty(key);
}

Kind ConditionSchema::kind(const QString &key, const OptStr &) const
{
    using Opts = std::vector<std::pair<QString, QString>>;
    if (techs.contains(key))
        return {u"choice"_s, Opts{{u"1"_s, u"researched"_s}}};
    if (invs.contains(key))
        return {u"choice"_s, Opts{{u"yes"_s, u"invented"_s}, {u"1"_s, u"invented (1)"_s}}};
    const Info *e = info.get(key);
    if (LOGIC.contains(key) || (e && e->block)) {
        QSet<QString> kids;
        if (e)
            for (const auto &kv : e->children.n)
                kids.insert(kv.first);
        QSet<QString> opts = kids;
        if (LOGIC.contains(key) || key.startsWith(u"any_") || key.startsWith(u"all_"))
            for (const auto &kv : info)
                opts.insert(kv.first);
        QStringList ol(opts.begin(), opts.end());
        std::sort(ol.begin(), ol.end());
        Opts o;
        for (const QString &k : ol)
            o.emplace_back(k, name(k));
        return {u"block"_s, o};
    }
    const Counter vals = e ? e->values : Counter();
    if (!vals.empty() && all_yes_no(vals))
        return {u"bool"_s, std::nullopt};
    if (!vals.empty() && all_num(vals))
        return {u"number"_s, std::nullopt};
    if (!vals.empty()) {
        Opts o;
        for (const auto &kv : vals.most_common())
            o.emplace_back(kv.first, pretty(kv.first));
        return {u"choice"_s, o};
    }
    return {u"text"_s, std::nullopt};
}

QString ConditionSchema::example(const QString &key, const OptStr &) const
{
    if (const Info *e = info.get(key))
        for (const auto &kv : e->values.n)
            if (_num(kv.first))
                return kv.first;
    return u"1"_s;
}

}  // namespace techdata
