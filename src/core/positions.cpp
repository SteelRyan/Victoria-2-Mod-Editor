// positions.py, the file half: PositionsFile and the entry format. Line by line.
#include "core/positions.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"

#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Qt::StringLiterals;
using pypath::join;

namespace positions {

const QStringList POINT_KEYS = {u"unit"_s, u"text_position"_s, u"building_construction"_s, u"military_construction"_s,
                                u"factory"_s};
const QStringList BUILDING_KEYS = {u"fort"_s, u"naval_base"_s, u"railroad"_s};
const QStringList PLACED = {u"unit"_s,    u"text_position"_s, u"fort"_s,    u"railroad"_s, u"building_construction"_s,
                            u"military_construction"_s, u"factory"_s, u"naval_base"_s};
const OrderedMap<QString, QString> KIND_NAMES = {
    {u"unit"_s, u"Unit"_s}, {u"text_position"_s, u"Name"_s}, {u"fort"_s, u"Fort"_s}, {u"naval_base"_s, u"Naval base"_s},
    {u"railroad"_s, u"Railroad"_s}, {u"building_construction"_s, u"Building construction"_s},
    {u"military_construction"_s, u"Military construction"_s}, {u"factory"_s, u"Factory"_s}};
const QString CRASH = u"naval base in the history but no naval base position — the game crashes when it draws it"_s;

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }

XY name_center(const XY &pos, double th, double scale)
{
    // Display centre of a name whose text_position (display) is `pos`; th = clockwise display angle.
    const double h = TEXT_H * scale * ANCHOR_UP;
    return {pos.first + std::sin(th) * h, pos.second - std::cos(th) * h};
}

XY name_anchor(const XY &center, double th, double scale)
{
    // text_position (display) for a name centred at `center`.
    const double h = TEXT_H * scale * ANCHOR_UP;
    return {center.first - std::sin(th) * h, center.second + std::cos(th) * h};
}

// == of two floats as Python's tuples compare them: the same object counts as equal (a NaN read once stays itself)
static bool same_num(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
static bool same_opt(const std::optional<double> &a, const std::optional<double> &b)
{
    return a.has_value() == b.has_value() && (!a || same_num(*a, *b));
}

bool Entry::operator==(const Entry &o) const
{
    if (pts.size() != o.pts.size())
        return false;
    for (const auto &[k, v] : pts) {
        const XY *w = o.pts.get(k);
        if (!w || !same_num(v.first, w->first) || !same_num(v.second, w->second))
            return false;
    }
    return same_opt(text_rotation, o.text_rotation) && same_opt(text_scale, o.text_scale) &&
           same_opt(naval_rot, o.naval_rot) && extra == o.extra && bp_extra == o.bp_extra && br_extra == o.br_extra;
}

static std::optional<double> _num(const script::Node &n)
{
    return n.is_block ? std::nullopt : py::to_float(n.value);
}

static std::optional<XY> _xy(const script::Node &node)
{
    std::optional<std::optional<double>> x, y;      // (the last x / y wins, as in a dict)
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.is_block && (c.key == u"x" || c.key == u"y"))
            (c.key == u"x" ? x : y) = _num(c);
    if (!x || !*x || !y || !*y)
        return std::nullopt;
    return XY{**x, **y};
}

Entry parse_entry(const QString &text, const script::Node &node)
{
    // A province block -> entry (points in game coordinates, unknown parts kept as raw text).
    Entry e;
    for (const script::Node &c : node.kids) {
        const QString raw = text.mid(c.kstart, c.vend - c.kstart);
        const bool k = c.has_key;
        if (k && POINT_KEYS.contains(c.key) && c.is_block && _xy(c)) {
            e.pts.insert(c.key, *_xy(c));
        } else if (k && (c.key == u"text_rotation" || c.key == u"text_scale") && !c.is_block && _num(c)) {
            (c.key == u"text_rotation" ? e.text_rotation : e.text_scale) = _num(c);
        } else if (k && c.key == u"building_position" && c.is_block) {
            for (const script::Node &d : c.kids)
                if (d.has_key && BUILDING_KEYS.contains(d.key) && d.is_block && _xy(d))
                    e.pts.insert(d.key, *_xy(d));
                else
                    e.bp_extra << text.mid(d.kstart, d.vend - d.kstart);
        } else if (k && c.key == u"building_rotation" && c.is_block) {
            for (const script::Node &d : c.kids)
                if (d.has_key && d.key == u"naval_base" && !d.is_block && _num(d))
                    e.naval_rot = _num(d);
                else
                    e.br_extra << text.mid(d.kstart, d.vend - d.kstart);
        } else {
            e.extra << raw;
        }
    }
    return e;
}

QString fmt_entry(long long pid, const Entry &e, const QString &nl)
{
    QStringList L = {u"%1 = {"_s.arg(pid)};
    auto pt = [&](const QString &key, const XY &xy, const QString &ind) {
        L << ind + key + u" = {"_s << ind + u"\tx = "_s + py::fmt_f(xy.first, 6) << ind + u"\ty = "_s + py::fmt_f(xy.second, 6)
          << ind + u'}';
    };
    const auto &p = e.pts;
    for (const QString &k : {u"unit"_s, u"text_position"_s})
        if (const XY *v = p.get(k))
            pt(k, *v, u"\t"_s);
    if (e.text_rotation)
        L << u"\ttext_rotation = "_s + py::fmt_f(*e.text_rotation, 6);
    if (e.text_scale)
        L << u"\ttext_scale = "_s + py::fmt_f(*e.text_scale, 6);
    for (const QString &k : {u"building_construction"_s, u"military_construction"_s, u"factory"_s})
        if (const XY *v = p.get(k))
            pt(k, *v, u"\t"_s);
    QStringList bps;
    for (const QString &k : BUILDING_KEYS)
        if (p.contains(k))
            bps << k;
    if (!bps.isEmpty() || !e.bp_extra.isEmpty()) {
        L << u"\tbuilding_position = {"_s;
        for (const QString &k : bps)
            pt(k, *p.get(k), u"\t\t"_s);
        for (const QString &x : e.bp_extra)
            L << u"\t\t"_s + x;
        L << u"\t}"_s;
    }
    if (e.naval_rot || !e.br_extra.isEmpty()) {
        L << u"\tbuilding_rotation = {"_s;
        if (e.naval_rot)
            L << u"\t\tnaval_base = "_s + py::fmt_f(*e.naval_rot, 6);
        for (const QString &x : e.br_extra)
            L << u"\t\t"_s + x;
        L << u"\t}"_s;
    }
    for (const QString &x : e.extra)
        L << u"\t"_s + x;
    L << u"}"_s;
    return L.join(nl);
}

// ---------------------------------------------------------------------------------------------- PositionsFile
PositionsFile::PositionsFile(moddata::Mod &mod_, long long map_h) : mod(mod_), H(map_h)
{
    path = mod.p({u"map"_s, u"positions.txt"_s});
    if (fs::isfile(path))
        _load(script::read_text(path));
}

void PositionsFile::_load(const QString &text)
{
    hash = moddata::text_hash(text);
    nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    entries.clear();
    spans.clear();
    for (const script::Node &n : script::parse(text))
        if (n.has_key && !n.key.isEmpty() && py::isdigit(n.key) && n.is_block) {
            const long long pid = py::to_int(n.key);
            entries.insert(pid, std::make_shared<const Entry>(parse_entry(text, n)));
            spans.insert(pid, {n.kstart, n.vend});
        }
    orig = entries;
}

XY PositionsFile::to_game(const XY &xy) const
{
    return {py::round_to(xy.first, 6), py::round_to(double(H) - xy.second, 6)};
}

Entry PositionsFile::get(long long pid) const
{
    const EntryP *e = entries.get(pid);
    return e ? **e : Entry();
}

void PositionsFile::set_entry(long long pid, Entry e) { entries.insert(pid, std::make_shared<const Entry>(std::move(e))); }

void PositionsFile::set_point(long long pid, const QString &what, const std::optional<XY> &disp_xy)
{
    Entry e = get(pid);
    if (!disp_xy) {
        e.pts.remove(what);
        if (what == u"naval_base")
            e.naval_rot.reset();
    } else {
        e.pts.insert(what, to_game(*disp_xy));
    }
    set_entry(pid, std::move(e));
}

QList<long long> PositionsFile::dirty_ids() const
{
    QList<long long> out;
    for (const auto &[pid, e] : entries) {
        const EntryP *o = orig.get(pid);
        if (!o || !(*o == e || **o == *e))
            out << pid;
    }
    for (const auto &kv : orig)
        if (!entries.contains(kv.first))
            out << kv.first;
    return out;
}

QStringList PositionsFile::dirty() const
{
    QStringList out;
    for (long long p : dirty_ids())
        out << QString::number(p);
    return out;
}

store::Snap PositionsFile::snapshot() const
{
    store::Items it;
    for (const auto &[pid, e] : entries)
        it.insert(store::Key(pid), std::make_shared<const EntrySnap>(e));
    return std::make_shared<const store::ItemsSnap>(it, false);
}

void PositionsFile::restore(const store::Snap &snap)
{
    entries.clear();
    for (const auto &[k, v] : dynamic_cast<const store::ItemsSnap &>(*snap).items)
        entries.insert(k.num(), dynamic_cast<const EntrySnap &>(*v).e);
}

std::optional<QStringList> PositionsFile::save(const QString &backup_root)
{
    const auto p = save_file(backup_root);
    return p ? std::optional<QStringList>(QStringList{*p}) : std::nullopt;
}

std::optional<QString> PositionsFile::save_file(const QString &backup_root)
{
    QList<long long> dirty = dirty_ids();
    if (dirty.isEmpty())
        return std::nullopt;
    if (fs::isfile(path))
        path = mod.adopt(path);                     // (the base game's file: copied into the mod first)
    QString text = fs::isfile(path) ? script::read_text(path) : QString();
    if (!text.isEmpty() && (!hash || moddata::text_hash(text) != *hash))
        throw std::runtime_error("map/positions.txt was changed on disk after it was loaded. Reload before saving.");
    struct Edit {
        qsizetype s, t;
        QString rep;
    };
    std::vector<Edit> edits;
    QStringList tail;
    std::sort(dirty.begin(), dirty.end());
    for (long long pid : dirty) {
        const EntryP *e = entries.get(pid);
        if (const auto *sp = spans.get(pid))
            edits.push_back({sp->first, sp->second, e ? fmt_entry(pid, **e, nl) : QString()});
        else if (e)
            tail << fmt_entry(pid, **e, nl);
    }
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.s < b.s; });
    QString out;
    qsizetype pos = 0;
    for (const Edit &ed : edits) {
        out += text.mid(pos, std::max<qsizetype>(0, ed.s - pos)) + ed.rep;
        pos = ed.t;
    }
    out += text.mid(pos);
    if (!tail.isEmpty()) {
        if (!out.isEmpty() && !out.endsWith(u'\n') && !out.endsWith(u'\r'))
            out += nl;
        out += tail.join(nl) + nl;
    }
    if (fs::isfile(path)) {
        const QString bdir = join(backup_root, {u"map"_s, stamp_now()});
        fs::makedirs(bdir);
        fs::copy2(path, join(bdir, u"positions.txt"_s));
    }
    script::write_text(path, out);
    _load(out);
    return path;
}

// ---------------------------------------------------------------------------------------------- history
static bool walk_naval(const script::Nodes &nodes)
{
    for (const script::Node &n : nodes) {
        if (n.has_key && n.key == u"naval_base" && !n.is_block) {
            const auto f = py::to_float(n.value);   // int(float(v)) > 0  (a ValueError is passed over)
            if (f && std::isfinite(*f) && py::trunc_int(*f) > 0)
                return true;
        } else if (n.is_block && n.has_key && py::isdigit(n.key.left(1)) && n.key.contains(u'.')) {
            if (walk_naval(n.kids))
                return true;
        }
    }
    return false;
}

QSet<long long> naval_in_history(const history::ProvinceHistory &hist)
{
    // Provinces whose history gives them a naval base (level > 0) at any date. The game draws those ports,
    // so they MUST have a naval_base position or the game crashes when it draws them.
    QSet<long long> out;
    for (const auto &[pid, files] : hist.files)
        if (std::any_of(files.begin(), files.end(), [](const history::HistFileP &hf) { return walk_naval(hf->nodes); }))
            out.insert(pid);
    return out;
}

}  // namespace positions
