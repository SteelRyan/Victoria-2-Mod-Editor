// adjacency.py, the data half (AdjacencyFile and its helpers). Line by line.
#include "core/adjacency.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace adjacency {

using pypath::join;

static constexpr auto RX = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption;
const QRegularExpression MARK_RE(u"^\\s*#\\s*meme river:\\s*(.*?)\\s*=\\s*sea tiles?\\s*([\\d ,]+?)\\s*$"_s, RX);
const QString NC_HEAD = u"# No-coast sea tiles (ModEditor): shores of these sea tiles need no coast crossings"_s;
const QRegularExpression NC_HEAD_RE(u"^\\s*#\\s*no-coast sea tiles\\b"_s, RX);
const QRegularExpression NC_RE(u"^\\s*#\\s*no_coast\\s*=\\s*([\\d\\s]*)$"_s, RX);
const QStringList TYPES = {u"sea"_s, u"canal"_s, u"impassable"_s, u"land"_s};   // land: accepted (the user, 2026-10-15)
const QRegularExpression RIVER_RE(u"^\\s*meme[- ]?river\\s*(\\d+)"_s, RX);

const std::vector<Category> CATEGORIES = {
    {u"strait"_s, u"Straits (sea crossing with a sea tile)"_s, {95, 200, 230}},
    {u"river"_s, u"Crossings without sea tile (Meme rivers)"_s, {90, 130, 255}},
    {u"coast"_s, u"Coast crossings (landing penalty)"_s, {110, 200, 110}},
    {u"canal"_s, u"Canals"_s, {230, 185, 70}},
    {u"impassable"_s, u"Impassable"_s, {235, 70, 60}},
    {u"land"_s, u"Land links (type land, e.g. a bridge)"_s, {200, 140, 255}},
    {u"other"_s, u"Other"_s, {170, 170, 170}},
};
// what each kind is — the legend's tooltips (Sam's item 9)
const OrderedMap<QString, QString> CAT_HELP = [] {
    OrderedMap<QString, QString> m;
    m.insert(u"strait"_s, u"type sea between two land provinces, through a sea tile: troops cross by sea; a fleet in "
                          u"that sea tile can block it"_s);
    m.insert(u"river"_s, u"type sea between two land provinces with no sea tile (through 0): a crossing nothing can "
                         u"block — Meme rivers are drawn with these"_s);
    m.insert(u"coast"_s, u"type sea between a sea tile and a land province: the editor's coast crossings (the landing "
                         u"penalty)"_s);
    m.insert(u"canal"_s, u"type canal between two sea tiles, through the land province that holds it; Data = the canal "
                         u"number (enable_canal)"_s);
    m.insert(u"impassable"_s, u"type impassable between two provinces that touch: nothing crosses that border"_s);
    m.insert(u"land"_s, u"type land: a land link between two provinces (often across a strait, e.g. Istanbul – Üsküdar "
                        u"over the Sea of Marmara) — an accepted type, used by several mods"_s);
    m.insert(u"other"_s, u"a row the editor can't place in any kind above (an unknown type, or a sea row between two "
                         u"sea tiles)"_s);
    return m;
}();
const OrderedMap<QString, Rgb> CAT_COLOR = [] {
    OrderedMap<QString, Rgb> m;
    for (const Category &c : CATEGORIES)
        m.insert(c.key, c.color);
    return m;
}();
const Rgb MISSING_COLOR{255, 70, 200};          // hot pink: stands out from the red selection mesh
const Rgb STALE_COLOR{255, 150, 40};
const Rgb PREVIEW_COLOR{255, 255, 255};

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }
static QString join_ids(const std::vector<long long> &ids, size_t from, size_t to)
{
    QStringList s;
    for (size_t i = from; i < std::min(to, ids.size()); ++i)
        s << QString::number(ids[i]);
    return s.join(u' ');
}

std::optional<RowFields> parse_row(const QString &line)
{
    const QString s = py::strip(line);
    if (s.isEmpty() || s.startsWith(u'#'))
        return std::nullopt;
    const QStringList f = s.split(u';');
    if (f.size() < 5)
        return std::nullopt;
    try {
        return RowFields{py::to_int(f[0]), py::to_int(f[1]), py::strip(f[2]), py::to_int(f[3]), py::to_int(f[4]),
                         py::strip(f.mid(5).join(u';'))};
    } catch (const py::ValueError &) {
        return std::nullopt;
    }
}

std::optional<std::pair<QString, std::vector<long long>>> parse_mark(const QString &line)
{
    const QRegularExpressionMatch m = MARK_RE.match(line);
    if (!m.hasMatch())
        return std::nullopt;
    std::vector<long long> tiles;
    // re.split(r"[ ,]+", group(2).strip()), the empty parts left out
    QString g = py::strip(m.captured(2));
    QString cur;
    for (QChar c : g + u' ') {
        if (c == u' ' || c == u',') {
            if (!cur.isEmpty())
                tiles.push_back(py::to_int(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (tiles.empty())
        return std::nullopt;
    return std::make_pair(py::strip(m.captured(1)), tiles);
}

QString fmt_mark(const Mark &m)
{
    return u"# Meme river: %1 = sea tile%2 %3"_s.arg(m.name, m.tiles.size() > 1 ? u"s"_s : QString(),
                                                      join_ids(m.tiles, 0, m.tiles.size()));
}

QString fmt_row(const Row &r)
{
    return u"%1;%2;%3;%4;%5;%6"_s.arg(r.a).arg(r.b).arg(r.type).arg(r.through).arg(r.data).arg(r.comment);
}

// ---------------------------------------------------------------------- AdjacencyFile
AdjacencyFile::AdjacencyFile(moddata::Mod &mod_) : mod(mod_)
{
    path = mod.p({u"map"_s, u"adjacencies.csv"_s});
    for (const auto &[p, pr] : mod.provinces)
        if (pr.sea)
            sea.insert(p);
    if (fs::isfile(path))
        _load(script::read_text(path));
}

void AdjacencyFile::_load(const QString &text)
{
    hash = moddata::text_hash(text);
    nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    trailing_nl = text.endsWith(u'\n') || text.endsWith(u'\r');
    lines.clear();
    rows.clear();
    rows_id += 1;
    orig.clear();
    _nc_lines.clear();
    marks.clear();
    orig_marks.clear();
    QSet<long long> nc;
    const QStringList ls = py::splitlines(text);
    for (qsizetype i = 0; i < ls.size(); ++i) {
        const QString &line = ls[i];
        if (auto mk = parse_mark(line)) {
            Mark k{next_uid, mk->first, mk->second};
            next_uid += 1;
            lines.emplace_back(line, k.uid);
            marks.push_back(k);
            orig_marks.insert(k.uid, k);
            continue;
        }
        const QRegularExpressionMatch m = NC_RE.match(line);
        if (m.hasMatch() || NC_HEAD_RE.match(line).hasMatch()) {
            if (m.hasMatch())
                for (const QString &x : py::split(m.captured(1)))
                    nc.insert(py::to_int(x));
            _nc_lines.insert(i);
            lines.emplace_back(line, std::nullopt);
            continue;
        }
        const auto v = parse_row(line);
        if (!v) {
            lines.emplace_back(line, std::nullopt);
            continue;
        }
        Row r{next_uid, v->a, v->b, v->type, v->through, v->data, v->comment};
        next_uid += 1;
        lines.emplace_back(line, r.uid);
        rows.push_back(r);
        orig.insert(r.uid, r);
    }
    no_coast = orig_no_coast = nc;
}

const Row *AdjacencyFile::get(long long uid) const
{
    for (const Row &r : rows)
        if (r.uid == uid)
            return &r;
    return nullptr;
}

QString AdjacencyFile::category(const Row &r) const
{
    if (r.type == u"canal")
        return u"canal"_s;
    if (r.type == u"impassable")
        return u"impassable"_s;
    if (r.type == u"land")
        return u"land"_s;
    if (r.type == u"sea") {
        const bool sa = sea.contains(r.a), sb = sea.contains(r.b);
        if (sa != sb)
            return u"coast"_s;
        if (!sa && !sb)
            return r.through ? u"strait"_s : u"river"_s;
    }
    return u"other"_s;
}

const OrderedMap<std::pair<long long, long long>, std::vector<Row>> &AdjacencyFile::pair_index()
{
    // {frozenset(a, b): [rows]} for the current rows (kept until the rows change: every change makes a new list of
    // rows, or a longer one)
    if (!_pairs || std::get<0>(*_pairs) != rows_id || std::get<1>(*_pairs) != rows.size()) {
        OrderedMap<std::pair<long long, long long>, std::vector<Row>> out;
        for (const Row &r : rows)
            out[{std::min(r.a, r.b), std::max(r.a, r.b)}].push_back(r);
        _pairs = std::make_tuple(rows_id, rows.size(), out);
    }
    return std::get<2>(*_pairs);
}

QString AdjacencyFile::coast_comment() const
{
    // Counter(...).most_common(1): the most common; among equals the one counted first
    OrderedMap<QString, long long> c;
    for (const Row &r : rows)
        if (category(r) == u"coast")
            c[r.comment] += 1;
    if (c.isEmpty())
        return u"#Coast"_s;
    const QString *best = nullptr;
    long long n = -1;
    for (const auto &[k, cnt] : c)
        if (cnt > n) {
            n = cnt;
            best = &k;
        }
    return *best;
}

long long AdjacencyFile::river_names() const
{
    // The next free Meme-River number.
    std::vector<long long> nums;
    for (const Row &r : rows) {
        const auto m = RIVER_RE.match(r.comment);
        if (m.hasMatch())
            nums.push_back(py::to_int(m.captured(1)));
    }
    for (const Mark &k : marks) {
        const auto m = RIVER_RE.match(k.name);
        if (m.hasMatch())
            nums.push_back(py::to_int(m.captured(1)));
    }
    return nums.empty() ? 1 : *std::max_element(nums.begin(), nums.end()) + 1;
}

OrderedMap<long long, Mark> AdjacencyFile::river_tiles() const
{
    // {sea tile: Mark} of the Meme rivers.
    OrderedMap<long long, Mark> out;
    for (const Mark &m : marks)
        for (long long t : m.tiles)
            out.insert(t, m);
    return out;
}

const Mark *AdjacencyFile::mark_by_name(const QString &name) const
{
    for (const Mark &m : marks)
        if (m.name == name)
            return &m;
    return nullptr;
}

QSet<long long> AdjacencyFile::skip_coast() const
{
    // Sea tiles whose shores need no coast crossings: no-coast tiles and Meme-river tiles.
    QSet<long long> out = no_coast;
    for (const auto &kv : river_tiles())
        out.insert(kv.first);
    return out;
}

std::vector<long long> AdjacencyFile::canal_numbers() const
{
    QSet<long long> s;
    for (const Row &r : rows)
        if (r.type == u"canal")
            s.insert(r.data);
    std::vector<long long> out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

QStringList AdjacencyFile::dirty() const
{
    // (Python: a list of uids and the word "no_coast"; here the uids as text)
    OrderedMap<long long, const Row *> cur;
    for (const Row &r : rows)
        cur.insert(r.uid, &r);
    QStringList out;
    for (const auto &[u, r] : cur) {
        const Row *o = orig.get(u);
        if (!o || !(*o == *r))
            out << QString::number(u);
    }
    for (const auto &kv : orig)
        if (!cur.contains(kv.first))
            out << QString::number(kv.first);
    if (no_coast != orig_no_coast)
        out << u"no_coast"_s;
    OrderedMap<long long, const Mark *> curm;
    for (const Mark &m : marks)
        curm.insert(m.uid, &m);
    for (const auto &[u, m] : curm) {
        const Mark *o = orig_marks.get(u);
        if (!o || !(*o == *m))
            out << QString::number(u);
    }
    for (const auto &kv : orig_marks)
        if (!curm.contains(kv.first))
            out << QString::number(kv.first);
    return out;
}

std::vector<long long> AdjacencyFile::add(const std::vector<RowFields> &specs)
{
    // specs: [(a, b, type, through, data, comment)] -> new uids.
    std::vector<long long> uids;
    for (const RowFields &s : specs) {
        Row r{next_uid, s.a, s.b, s.type, s.through, s.data, s.comment};
        next_uid += 1;
        rows.push_back(r);
        uids.push_back(r.uid);
    }
    return uids;
}

void AdjacencyFile::update(long long uid, const std::function<void(Row &)> &change)
{
    for (Row &r : rows)
        if (r.uid == uid) {
            change(r);
            return;
        }
}

void AdjacencyFile::del(const QList<long long> &uids0)
{
    const QSet<long long> uids(uids0.begin(), uids0.end());
    std::vector<Row> keep;
    for (const Row &r : rows)
        if (!uids.contains(r.uid))
            keep.push_back(r);
    rows = keep;
    rows_id += 1;
}

void AdjacencyFile::set_no_coast(const QList<long long> &tiles0, bool on)
{
    QSet<long long> tiles;
    for (long long t : tiles0)
        if (sea.contains(t))
            tiles.insert(t);
    no_coast = on ? (no_coast | tiles) : (no_coast - tiles);
}

void AdjacencyFile::set_mark(const QString &name, const QList<long long> &tiles0)
{
    // Make sea tiles the Meme river `name` (creates or extends its group; the tiles leave any other river).
    QSet<long long> tset(tiles0.begin(), tiles0.end());
    std::vector<long long> tiles(tset.begin(), tset.end());
    std::sort(tiles.begin(), tiles.end());
    std::vector<Mark> out;
    for (const Mark &m : marks) {
        if (m.name == name) {
            QSet<long long> u(m.tiles.begin(), m.tiles.end());
            u |= tset;
            std::vector<long long> t(u.begin(), u.end());
            std::sort(t.begin(), t.end());
            out.push_back(Mark{m.uid, m.name, t});
            continue;
        }
        std::vector<long long> rest;
        for (long long t : m.tiles)
            if (!tset.contains(t))
                rest.push_back(t);
        if (!rest.empty())
            out.push_back(Mark{m.uid, m.name, rest});
    }
    if (std::none_of(out.begin(), out.end(), [&](const Mark &m) { return m.name == name; })) {
        out.push_back(Mark{next_uid, name, tiles});
        next_uid += 1;
    }
    marks = out;
}

void AdjacencyFile::rename_mark(const QString &old, const QString &fresh)
{
    for (Mark &m : marks)
        if (m.name == old)
            m.name = fresh;
    std::vector<Row> out;
    for (Row r : rows) {
        if (r.comment == old && category(r) == u"river")
            r.comment = fresh;
        out.push_back(r);
    }
    rows = out;
    rows_id += 1;
}

void AdjacencyFile::unmark(const QList<long long> &tiles0)
{
    const QSet<long long> tiles(tiles0.begin(), tiles0.end());
    std::vector<Mark> out;
    for (const Mark &m : marks) {
        std::vector<long long> t;
        for (long long x : m.tiles)
            if (!tiles.contains(x))
                t.push_back(x);
        if (!t.empty())
            out.push_back(Mark{m.uid, m.name, t});
    }
    marks = out;
}

std::optional<qsizetype> AdjacencyFile::_block_start(const QString &name) const
{
    // Line index where the main block of a river's crossings starts (None if it has no rows on disk).
    std::vector<qsizetype> idx;
    for (qsizetype i = 0; i < qsizetype(lines.size()); ++i) {
        const OptUid &uid = lines[size_t(i)].second;
        if (uid && orig.contains(*uid) && orig.get(*uid)->comment == name)
            idx.push_back(i);
    }
    if (idx.empty())
        return std::nullopt;
    qsizetype j = idx[idx.size() / 2];
    while (true) {        // walk back over rows of the same river (and '#' / blank lines between them)
        qsizetype k = j - 1;
        while (k >= 0 && !lines[size_t(k)].second &&
               (py::strip(lines[size_t(k)].first).isEmpty() || py::strip(lines[size_t(k)].first) == u"#") &&
               !_nc_lines.contains(k))
            k -= 1;
        if (k >= 0 && lines[size_t(k)].second && orig.contains(*lines[size_t(k)].second) &&
            orig.get(*lines[size_t(k)].second)->comment == name)
            j = k;
        else
            return j;
    }
}

std::vector<Line> AdjacencyFile::_no_coast_block() const
{
    if (no_coast.isEmpty())
        return {};
    std::vector<long long> ids(no_coast.begin(), no_coast.end());
    std::sort(ids.begin(), ids.end());
    std::vector<Line> out{{NC_HEAD, std::nullopt}};
    for (size_t j = 0; j < ids.size(); j += 20)
        out.emplace_back(u"# no_coast = "_s + join_ids(ids, j, j + 20), std::nullopt);
    return out;
}

std::optional<QString> AdjacencyFile::save(const QString &backup_root)
{
    if (dirty().isEmpty())
        return std::nullopt;
    path = mod.adopt(path);                    // (the base game's file: copied into the mod first)
    if (fs::isfile(path)) {
        const QString text = script::read_text(path);
        if (!hash || moddata::text_hash(text) != *hash)
            throw std::runtime_error("map/adjacencies.csv was changed on disk after it was loaded. Reload before saving.");
    }
    OrderedMap<long long, Row> cur;
    for (const Row &r : rows)
        cur.insert(r.uid, r);
    OrderedMap<long long, Mark> curm;
    for (const Mark &m : marks)
        curm.insert(m.uid, m);
    const qsizetype end = qsizetype(lines.size()) - 1;
    OrderedMap<QString, qsizetype> last_line, last_name, mark_line;
    for (qsizetype i = 0; i < qsizetype(lines.size()); ++i) {
        const OptUid &uid = lines[size_t(i)].second;
        if (uid && orig.contains(*uid)) {
            const Row &o = *orig.get(*uid);
            last_line.insert(category(o), i);
            last_name.insert(o.comment, i);
            if (RIVER_RE.match(o.comment).hasMatch())
                last_line.insert(u"meme"_s, i);
        } else if (uid && orig_marks.contains(*uid)) {
            const Mark *cm = curm.get(*uid);
            mark_line.insert((cm ? *cm : *orig_marks.get(*uid)).name, i);
        }
    }
    OrderedMap<qsizetype, std::vector<Line>> before, after;
    OrderedMap<QString, std::vector<Line>> grouped;   // new Meme river without rows on disk -> [marker, its rows]
    for (const Mark &m : marks) {
        if (orig_marks.contains(m.uid))
            continue;
        if (const auto b = _block_start(m.name)) {
            std::vector<Line> &v = before[*b];
            if (!(*b > 0 && py::strip(lines[size_t(*b - 1)].first) == u"#"))
                v.emplace_back(u"#"_s, std::nullopt);
            v.emplace_back(fmt_mark(m), m.uid);
        } else {
            grouped.insert(m.name, {{u"#"_s, std::nullopt}, {fmt_mark(m), m.uid}});
        }
    }
    OrderedMap<long long, qsizetype> line_of;
    for (qsizetype i = 0; i < qsizetype(lines.size()); ++i)
        if (lines[size_t(i)].second)
            line_of.insert(*lines[size_t(i)].second, i);
    for (const Row &r : rows) {
        if (orig.contains(r.uid))
            continue;
        const Line item{fmt_row(r), r.uid};
        const bool river = category(r) == u"river" && !r.comment.isEmpty();
        const OptUid *back = was_after.get(r.uid);
        if (back && (!*back || line_of.contains(**back)))
            after[*back ? *line_of.get(**back) : qsizetype(-1)].push_back(item);    // where it stood before
        else if (river && last_name.contains(r.comment))
            after[*last_name.get(r.comment)].push_back(item);   // next to the rest of that river
        else if (river && mark_line.contains(r.comment))
            after[*mark_line.get(r.comment)].push_back(item);   // right below its marker
        else if (river && grouped.contains(r.comment))
            grouped[r.comment].push_back(item);
        else
            after[last_line.value(category(r), end)].push_back(item);
    }
    const qsizetype anchor = last_line.value(u"meme"_s, last_line.value(u"river"_s, end));
    for (const auto &kv : grouped)
        for (const Line &l : kv.second)
            after[anchor].push_back(l);
    std::vector<Line> body = after.value(-1);
    for (qsizetype i = 0; i < qsizetype(lines.size()); ++i) {
        if (_nc_lines.contains(i))
            continue;
        for (const Line &l : before.value(i))
            body.push_back(l);
        const auto &[t, uid] = lines[size_t(i)];
        if (!uid) {
            body.emplace_back(t, std::nullopt);
        } else if (orig.contains(*uid)) {
            if (const Row *c = cur.get(*uid))
                body.emplace_back(*c == *orig.get(*uid) ? t : fmt_row(*c), uid);
        } else if (orig_marks.contains(*uid)) {
            if (const Mark *c = curm.get(*uid))
                body.emplace_back(*c == *orig_marks.get(*uid) ? t : fmt_mark(*c), uid);
        }
        for (const Line &l : after.value(i))
            body.push_back(l);
    }
    // the no-coast list goes at the top (below the column header line, if there is one)
    const qsizetype head = (!body.empty() && !body[0].second &&
                            py::lower(py::lstrip(body[0].first)).startsWith(u"#from"))
                               ? 1
                               : 0;
    std::vector<Line> out(body.begin(), body.begin() + head);
    const std::vector<Line> ncb = _no_coast_block();
    out.insert(out.end(), ncb.begin(), ncb.end());
    out.insert(out.end(), body.begin() + head, body.end());
    QStringList texts;
    for (const Line &l : out)
        texts << l.first;
    const QString text = texts.join(nl) + ((trailing_nl || out.empty()) ? nl : QString());
    if (fs::isfile(path)) {
        const QString bdir = join(backup_root, {u"map"_s, stamp_now()});
        fs::makedirs(bdir);
        fs::copy2(path, join(bdir, u"adjacencies.csv"_s));
    }
    script::write_text(path, text);
    hash = moddata::text_hash(text);
    OptUid prev;                               // rows that went: the line each stood under (for an undo later)
    for (const auto &[_t, uid] : lines) {
        if (uid && orig.contains(*uid) && !cur.contains(*uid))
            was_after.insert(*uid, prev);
        else if (uid && (cur.contains(*uid) || curm.contains(*uid)))
            prev = uid;
    }
    for (const auto &kv : cur)
        was_after.remove(kv.first);
    lines = out;
    _nc_lines.clear();
    for (qsizetype i = head; i < head + qsizetype(ncb.size()); ++i)
        _nc_lines.insert(i);
    orig = cur;
    orig_marks = curm;
    orig_no_coast = no_coast;
    OrderedMap<long long, qsizetype> order;
    for (qsizetype i = 0; i < qsizetype(out.size()); ++i)
        if (out[size_t(i)].second)
            order.insert(*out[size_t(i)].second, i);
    std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
        return order.value(a.uid, qsizetype(1) << 30) < order.value(b.uid, qsizetype(1) << 30);
    });
    std::stable_sort(marks.begin(), marks.end(), [&](const Mark &a, const Mark &b) {
        return order.value(a.uid, qsizetype(1) << 30) < order.value(b.uid, qsizetype(1) << 30);
    });
    return path;
}

}  // namespace adjacency
