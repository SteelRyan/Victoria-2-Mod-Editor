// events.py — events and decisions: reading, judging at the start, chances, readable lines. Line by line.
#include "core/events.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/mapfiles.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace events {

using pypath::join;

const int YEARS = 10;
const double LN2 = std::log(2.0);
const QStringList BLOCKS = {u"AND"_s, u"OR"_s, u"NOT"_s};
const QString NO_ID = u"\0None"_s;

// ---------------------------------------------------------------------- small Python pieces
std::optional<double> _num(const OptStr &v) { return v ? py::to_float(*v) : std::nullopt; }
double _num(const OptStr &v, double dflt) { return _num(v).value_or(dflt); }

bool yes(const OptStr &v)
{
    const QString s = py::lower(py::strip(v ? *v : u"None"_s));
    return s == u"yes" || s == u"true" || s == u"1";
}

Tri not3(Tri v) { return v ? Tri(!*v) : std::nullopt; }

static bool has(const Node &n) { return n.has_key && !n.key.isEmpty(); }   // `c.key` is truthy
static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }

static const QRegularExpression &rx(const QString &pattern, bool icase = false)
{
    static QHash<QString, QRegularExpression> cache[2];
    auto &c = cache[icase ? 1 : 0];
    auto it = c.find(pattern);
    if (it == c.end()) {
        QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
        if (icase)
            opts |= QRegularExpression::CaseInsensitiveOption;
        it = c.insert(pattern, QRegularExpression(u"(*LF)"_s + pattern, opts));
    }
    return *it;
}
// re.match(pattern, s): anchored at the start
static QRegularExpressionMatch rmatch(const QString &pattern, const QString &s, bool icase = false)
{
    return rx(pattern, icase).match(s, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
}
static bool is_tag(const QString &s) { return countryhist::tag_ok(s); }     // ^[A-Z][A-Z0-9]{2}$
static bool plain_key(QString s)                                         // ^[A-Za-z0-9_.\-]+$
{
    if (s.endsWith(u'\n'))
        s.chop(1);
    if (s.isEmpty())
        return false;
    for (QChar ch : s) {
        const char16_t u = ch.unicode();
        if (!((u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'_' ||
              u == u'.' || u == u'-'))
            return false;
    }
    return true;
}
static QString uniq_join(const QStringList &l) { return l.join(u", "_s); }
static QStringList uniq(const QStringList &l)                            // list(dict.fromkeys(l))
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &x : l)
        if (!seen.contains(x)) {
            seen.insert(x);
            out << x;
        }
    return out;
}
static QString leading_ws(const QString &t, qsizetype from)              // re.match(r"[ \t]*", t[from:]).group(0)
{
    qsizetype i = from;
    while (i < t.size() && (t[i] == u' ' || t[i] == u'\t'))
        ++i;
    return t.mid(from, i - from);
}
static QString fmt_s(const OptStr &s) { return s ? *s : u"None"_s; }    // "%s" % x

// ---------------------------------------------------------------------- tokens / changes
void _tokens(View nodes, QSet<QString> &out)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (has(c))
            out.insert(c.key);
        if (!c.is_block)
            out.insert(c.value);
        else
            _tokens(View(c.kids), out);
    }
}
QSet<QString> _tokens(View nodes)
{
    QSet<QString> out;
    _tokens(nodes, out);
    return out;
}

const QSet<QString> CHANGE_KEYS = {
    u"set_country_flag"_s, u"clr_country_flag"_s, u"set_global_flag"_s,    u"clr_global_flag"_s,     u"government"_s,
    u"nationalvalue"_s,    u"political_reform"_s, u"social_reform"_s,      u"civilized"_s,           u"primary_culture"_s,
    u"religion"_s,         u"change_tag"_s,       u"ruling_party_ideology"_s, u"activate_technology"_s, u"set_province_flag"_s,
    u"clr_province_flag"_s, u"secede_province"_s, u"add_core"_s,           u"remove_core"_s,         u"capital"_s,
    u"trade_goods"_s};

void _changes(View nodes, QSet<QString> &out)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (!c.has_key)
            continue;
        const QString &k = c.key;
        if ((k == u"add_country_modifier" || k == u"add_province_modifier" || k == u"remove_country_modifier") &&
            c.is_block) {
            const OptStr nm = _val(View(c.kids), u"name"_s);
            if (nm && !nm->isEmpty())
                out.insert(*nm);
        } else if ((k == u"remove_country_modifier" || k == u"remove_province_modifier") && !c.is_block) {
            out.insert(c.value);
        } else if ((k == u"set_variable" || k == u"change_variable") && c.is_block) {
            const OptStr w = _val(View(c.kids), u"which"_s);
            if (w && !w->isEmpty())
                out.insert(*w);
        } else if (CHANGE_KEYS.contains(k) && !c.is_block) {
            out.insert(c.value);
            out.insert(k);
        } else if (c.is_block) {
            _changes(View(c.kids), out);
        }
    }
}
QSet<QString> _changes(View nodes)
{
    QSet<QString> out;
    _changes(nodes, out);
    return out;
}

// ---------------------------------------------------------------------- categories
static const QString CAT_RE = u"^[ \\t]*#[ \\t]*categor(?:y|ies)[ \\t]*:[ \\t]*(.*?)[ \\t]*$"_s;
static const QString FILE_CAT_RE = u"^[ \\t]*#[ \\t]*file[ \\t]+categor(?:y|ies)[ \\t]*:[ \\t]*(.*?)[ \\t]*$"_s;

QStringList split_cats(const OptStr &s)
{
    QStringList out;
    for (const QString &c : py::split(s.value_or(QString()), u","_s))
        if (!py::strip(c).isEmpty())
            out << py::strip(c);
    return uniq(out);
}

qsizetype _line_start(const QString &text, qsizetype pos)
{
    // text.rfind("\n", 0, pos) + 1
    if (pos <= 0)
        return 0;
    return text.lastIndexOf(u'\n', std::min(pos, text.size()) - 1) + 1;
}

static QString rstrip_cr(QString s)
{
    while (s.endsWith(u'\r'))
        s.chop(1);
    return s;
}

std::optional<CatLine> cat_line_above(const QString &text, qsizetype pos)
{
    // (start, end, names) of the "#category:" comment right above the line of `pos` (other comments between are
    // fine, a blank line isn't), or None.
    qsizetype j = _line_start(text, pos);
    for (int n = 0; n < 6; ++n) {
        if (j == 0)
            return std::nullopt;
        const qsizetype pe = j - 1;
        const qsizetype ps = _line_start(text, pe);
        const QString line = rstrip_cr(text.mid(ps, pe - ps));
        if (py::strip(line).isEmpty())
            return std::nullopt;
        const auto m = rmatch(CAT_RE, line, true);
        if (m.hasMatch())
            return CatLine{ps, pe + 1, split_cats(m.captured(1))};
        if (!py::strip(line).startsWith(u'#'))
            return std::nullopt;
        j = ps;
    }
    return std::nullopt;
}

std::optional<CatLine> file_cat_line(const QString &text)
{
    // (start, end, names) of the "#file category:" comment among the file's first lines, or None.
    qsizetype pos = 0;
    for (int n = 0; n < 15; ++n) {
        qsizetype e = text.indexOf(u'\n', pos);
        e = e < 0 ? text.size() : e;
        const QString line = rstrip_cr(text.mid(pos, e - pos));
        const auto m = rmatch(FILE_CAT_RE, line, true);
        if (m.hasMatch())
            return CatLine{pos, std::min(text.size(), e + 1), split_cats(m.captured(1))};
        if (!py::strip(line).isEmpty() && !py::strip(line).startsWith(u'#'))
            return std::nullopt;
        if (e >= text.size())
            return std::nullopt;
        pos = e + 1;
    }
    return std::nullopt;
}

std::vector<Block> iter_blocks(const Nodes &nodes, const QString &folder)
{
    // (uid, node, kind) of every event (folder "events") or decision ("decisions") of a file. uid: kind, id / key
    // and how many of the same came before — stable while the file is edited around them.
    std::vector<Block> out;
    QHash<QString, long long> seen;
    if (folder == u"events") {
        for (const Node &n : nodes)
            if ((is(n, u"country_event"_s) || is(n, u"province_event"_s)) && n.is_block) {
                const OptStr id = _val(View(n.kids), u"id"_s);
                const QString base = n.key + u':' + ((id && !id->isEmpty()) ? *id : u"?"_s);
                seen[base] += 1;
                out.push_back({u"%1#%2"_s.arg(base).arg(seen[base]), &n, n.key});
            }
    } else {
        for (const Node &top : nodes)
            if (is(top, u"political_decisions"_s) && top.is_block)
                for (const Node &n : top.kids)
                    if (has(n) && n.is_block) {
                        const QString base = u"decision:"_s + n.key;
                        seen[base] += 1;
                        out.push_back({u"%1#%2"_s.arg(base).arg(seen[base]), &n, u"decision"_s});
                    }
    }
    return out;
}

QString set_cats(const QString &text, const QString &folder, const OptStr &uid, const QStringList &names)
{
    // The text with the categories of one entry (uid) — or of the file itself (uid None) — set to `names` (no names:
    // the comment goes).
    const QString nl = u"\n"_s;
    if (!uid) {
        const auto old = file_cat_line(text);
        const QString line = names.isEmpty() ? QString() : u"#file category: "_s + uniq_join(names) + nl;
        if (old)
            return text.left(old->start) + line + text.mid(old->end);
        return names.isEmpty() ? text : line + nl + text;
    }
    const Nodes nodes = script::parse(text);
    for (const Block &b : iter_blocks(nodes, folder)) {
        if (b.uid != *uid)
            continue;
        const Node &n = *b.node;
        const auto old = cat_line_above(text, n.kstart);
        const qsizetype ls = _line_start(text, n.kstart);
        const bool alone = py::strip(text.mid(ls, n.kstart - ls)).isEmpty();
        const QString indent = alone ? leading_ws(text, ls) : QString();
        const QString line = names.isEmpty() ? QString() : indent + u"#category: "_s + uniq_join(names) + nl;
        if (old)
            return text.left(old->start) + line + text.mid(old->end);
        if (names.isEmpty())
            return text;
        if (!alone)                               // something before it on its line: a line of its own
            return text.left(n.kstart) + nl + u"#category: "_s + uniq_join(names) + nl + text.mid(n.kstart);
        return text.left(ls) + line + text.mid(ls);
    }
    return text;
}

const QString LIT_LINE = u"#literacy event"_s;
static const QString LIT_RE = u"^[ \\t]*#[ \\t]*literacy[ \\t_\\-]*(?:event|script|decision)\\b"_s;

bool lit_re_match(const QString &line) { return rmatch(LIT_RE, line, true).hasMatch(); }

std::optional<Span> lit_line_above(const QString &text, qsizetype pos)
{
    qsizetype j = _line_start(text, pos);
    for (int n = 0; n < 6; ++n) {
        if (j == 0)
            return std::nullopt;
        const qsizetype pe = j - 1;
        const qsizetype ps = _line_start(text, pe);
        const QString line = rstrip_cr(text.mid(ps, pe - ps));
        if (py::strip(line).isEmpty())
            return std::nullopt;
        if (lit_re_match(line))
            return Span{ps, pe + 1};
        if (!py::strip(line).startsWith(u'#'))
            return std::nullopt;
        j = ps;
    }
    return std::nullopt;
}

QString set_lit_line(const QString &text, const QString &folder, const QString &uid, bool on)
{
    const Nodes nodes = script::parse(text);
    for (const Block &b : iter_blocks(nodes, folder)) {
        if (b.uid != uid)
            continue;
        const Node &n = *b.node;
        const auto old = lit_line_above(text, n.kstart);
        if (old && !on)
            return text.left(old->first) + text.mid(old->second);
        if (old || !on)
            return text;
        const qsizetype ls = _line_start(text, n.kstart);
        if (!py::strip(text.mid(ls, n.kstart - ls)).isEmpty())   // something before it on its line: its own line
            return text.left(n.kstart) + u'\n' + LIT_LINE + u'\n' + text.mid(n.kstart);
        return text.left(ls) + leading_ws(text, ls) + LIT_LINE + u'\n' + text.mid(ls);
    }
    return text;
}

QSet<std::pair<QString, QString>> lit_marked(const QStringList &files, const std::function<QString(const QString &)> &text_of)
{
    // {(file, uid)} of the events / decisions that carry the line (read from the texts as they are now).
    QSet<std::pair<QString, QString>> out;
    for (const QString &rel : files) {
        const QString text = text_of(rel);
        // LIT_FIND = (?im)^[ \t]*#[ \t]*literacy[ \t_\-]*(?:event|script|decision)\b
        if (!rx(u"(?m)"_s + LIT_RE, true).match(text).hasMatch())
            continue;
        const Nodes nodes = script::parse(text);
        for (const Block &b : iter_blocks(nodes, rel.split(u'/')[0]))
            if (lit_line_above(text, b.node->kstart))
                out.insert({rel, b.uid});
    }
    return out;
}

QStringList EventCats::get(const CatKey &key) const
{
    if (!db)
        return {};
    const auto &[rel, uid] = key;
    if (!uid)
        return db->file_cats.value(rel);
    Entry *const *e = db->by_uid.get({rel, *uid});
    return e ? (*e)->cats : QStringList();
}

void EventCats::set(const CatKey &key, const QStringList &names0)
{
    const auto &[rel, uid] = key;
    QStringList names;
    for (const QString &n : names0)
        if (!n.isEmpty())
            names << n;
    names = uniq(names);
    store.set_text(rel, set_cats(store.text(rel), rel.split(u'/')[0], uid, names));
    if (db) {                                    // at once (the text is read again a moment later)
        if (!uid)
            db->file_cats.insert(rel, names);
        else if (Entry *const *e = db->by_uid.get({rel, *uid}))
            (*e)->cats = names;
    }
}

QStringList EventCats::all_names() const
{
    if (!db)
        return {};
    QStringList out;
    for (const EntryP &e : db->entries)
        out += e->cats;
    for (const auto &kv : db->file_cats)
        out += kv.second;
    return uniq(out);
}

std::optional<NodeList> _block(View nodes, const QString &key)
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (is(nodes[i], key) && nodes[i].is_block) {
            NodeList l;
            for (const Node &c : nodes[i].kids)
                l.push_back(&c);
            return l;
        }
    return std::nullopt;
}

OptStr _val(View nodes, const QString &key)
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (is(nodes[i], key) && !nodes[i].is_block)
            return nodes[i].value;
    return std::nullopt;
}

// ---------------------------------------------------------------------- EventDB
static QStringList glob_txt(const QString &d)    // sorted(glob.glob(os.path.join(d, "*.txt")))
{
    QStringList out;
    if (!fs::isdir(d))
        return out;
    for (const QString &n : fs::listdir(d))
        if (!n.startsWith(u'.') && py::lower(n).endsWith(u".txt"))
            out << join(d, n);
    std::sort(out.begin(), out.end());
    return out;
}

QStringList glob_txt_recursive(const QString &d)
{
    // glob.glob(os.path.join(d, "**", "*.txt"), recursive=True): the folder itself, then its folders depth first
    // (hidden ones left out), each one's *.txt files in the order the folder lists them.
    // (2026-11-10: one listing per folder, whether a name is a folder taken from that listing — it was listed twice
    //  and the disk asked about every name; a link is still asked about, as isdir follows it)
    std::vector<std::pair<QString, QStringList>> dirs;   // (folder, its *.txt) in glob's order
    std::function<void(const QString &)> walk = [&](const QString &full) {
        const size_t at = dirs.size();
        dirs.push_back({full, {}});
        std::vector<fs::DirEntry> es;
        try {
            es = fs::scandir(full);
        } catch (const fs::OSError &) {
            return;
        }
        QStringList txt;
        for (const fs::DirEntry &e : es)
            if (!e.name.startsWith(u'.') && py::lower(e.name).endsWith(u".txt"))
                txt << join(full, e.name);
        dirs[at].second = std::move(txt);
        for (const fs::DirEntry &e : es) {
            if (e.name.startsWith(u'.'))
                continue;
            const QString p = join(full, e.name);
            if (e.is_dir || (!e.is_file && fs::isdir(p)))
                walk(p);
        }
    };
    walk(d);
    QStringList out;
    for (const auto &[_dir, txt] : dirs)
        out += txt;
    return out;
}

// script::read_text, the text decoded only when its bytes hold one of the words (latin-1: a character per byte, so the
// test is the same on the bytes); nullopt when none is there. Raises as read_text does.
static OptStr text_with(const QString &path, std::initializer_list<QByteArrayView> words)
{
    const std::optional<QByteArray> got = fs::read_bytes(path);
    if (!got)
        return script::read_text(path);
    const QByteArray &b = *got;
    for (const QByteArrayView w : words)
        if (b.contains(w))
            return QString::fromLatin1(b);
    return std::nullopt;
}

EventDB::EventDB(moddata::Mod &mod_, scriptstore::ScriptStore *store_, const EventDB *prev)
    : mod(mod_), store(store_), _prev(prev)
{
    _load();
}

QStringList EventDB::_files(const QString &folder) const
{
    const QString d = mod.p({folder});
    QStringList out = glob_txt(d);
    if (store) {                                  // the editor's texts (doc.maps["scripts"])
        QStringList kept;
        for (const QString &p : out)
            if (!store->gone.contains(folder + u'/' + pypath::basename(p)))
                kept << p;
        out = kept;
        QSet<QString> have;
        for (const QString &p : out)
            have.insert(py::lower(pypath::basename(p)));
        QStringList rels;
        for (const auto &kv : store->docs)
            rels << kv.first;
        std::sort(rels.begin(), rels.end());
        for (const QString &rel : rels) {         // made in this session, not written yet
            const QString name = rel.mid(rel.lastIndexOf(u'/') + 1);
            if (rel.startsWith(folder + u'/') && rel.count(u'/') == 1 && !store->gone.contains(rel) &&
                !have.contains(py::lower(name)) && py::lower(rel).endsWith(u".txt"))
                out << join(d, name);
        }
    }
    return out;
}

std::pair<QString, std::shared_ptr<const Nodes>> EventDB::_read(const QString &rel, const QString &path)
{
    QString text;
    if (store)
        text = store->text(rel);
    else
        text = script::read_text(path).replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    std::shared_ptr<const Nodes> nodes;
    if (_prev && _prev->texts.contains(rel) && *_prev->texts.get(rel) == text)
        nodes = *_prev->trees.get(rel);
    else
        nodes = std::make_shared<const Nodes>(script::parse(text));
    texts.insert(rel, text);
    trees.insert(rel, nodes);
    return {text, nodes};
}

void EventDB::_load()
{
    for (const QString &folder : {u"events"_s, u"decisions"_s}) {
        const bool ev = folder == u"events";
        for (const QString &path : _files(folder)) {
            const QString fn = pypath::basename(path);
            const QString rel = folder + u'/' + fn;
            std::pair<QString, std::shared_ptr<const Nodes>> got;
            try {
                got = _read(rel, path);
            } catch (const fs::OSError &e) {
                errors << e.text();
                continue;
            }
            const QString &text = got.first;
            const auto &tree = got.second;
            const auto fc = file_cat_line(text);
            file_cats.insert(rel, fc ? fc->names : QStringList());
            files << rel;
            for (const Block &b : iter_blocks(*tree, folder)) {
                const Node &n = *b.node;
                auto e = std::make_shared<Entry>();
                e->kind = ev ? n.key : u"decision"_s;
                e->key = ev ? QString() : n.key;
                e->file = fn;
                e->node = &n;
                e->tree = tree;
                e->text = text;
                e->span = {n.kstart, n.vend};
                e->uid = b.uid;
                e->rel = rel;
                by_uid.insert({rel, b.uid}, e.get());
                const auto cl = cat_line_above(text, n.kstart);
                e->cats = cl ? cl->names : QStringList();
                const View v(n.kids);
                e->picture = _val(v, u"picture"_s).value_or(QString());
                if (ev) {
                    e->id = _val(v, u"id"_s);
                    e->key = (e->id && !e->id->isEmpty()) ? *e->id : u"?"_s;
                    e->title = _val(v, u"title"_s).value_or(QString());
                    e->desc = _val(v, u"desc"_s).value_or(QString());
                    e->trigger = _block(v, u"trigger"_s);
                    e->immediate = _block(v, u"immediate"_s);
                    auto or_no = [&](const QString &k) {
                        const OptStr x = _val(v, k);
                        return yes((x && !x->isEmpty()) ? x : OptStr(u"no"_s));
                    };
                    e->once = or_no(u"fire_only_once"_s);
                    e->triggered_only = or_no(u"is_triggered_only"_s);
                    e->major = or_no(u"major"_s);
                    for (size_t i = 0; i < v.size(); ++i) {
                        const Node &mt = v[i];
                        if (!(is(mt, u"mean_time_to_happen"_s) && mt.is_block))
                            continue;
                        double days = 0.0;
                        for (const Node &c : mt.kids) {
                            if ((is(c, u"days"_s) || is(c, u"months"_s) || is(c, u"years"_s)) && !c.is_block) {
                                const double x = _num(c.value, 0.0);
                                days += x * (c.key == u"days" ? 1 : c.key == u"months" ? 30 : 365);
                                e->unit = c.key;
                            } else if (is(c, u"modifier"_s) && c.is_block) {
                                const double f = _num(_val(View(c.kids), u"factor"_s), 1.0);
                                NodeList conds;
                                for (const Node &x : c.kids)
                                    if (!is(x, u"factor"_s))
                                        conds.push_back(&x);
                                e->mods.emplace_back(f, conds);
                            }
                        }
                        e->mtth = days > 0 ? std::optional<double>(days) : std::nullopt;
                        break;
                    }
                    for (size_t i = 0; i < v.size(); ++i) {
                        const Node &c = v[i];
                        if (is(c, u"option"_s) && c.is_block) {
                            Option o;
                            o.name = _val(View(c.kids), u"name"_s).value_or(QString());
                            for (const Node &x : c.kids)
                                if (!(is(x, u"name"_s) || is(x, u"ai_chance"_s)))
                                    o.effects.push_back(&x);
                            o.ai_chance = _block(View(c.kids), u"ai_chance"_s);
                            e->options.push_back(o);
                        }
                    }
                } else {
                    e->title = n.key + u"_title"_s;
                    e->desc = n.key + u"_desc"_s;
                    e->potential = _block(v, u"potential"_s);
                    e->allow = _block(v, u"allow"_s);
                    e->effect = _block(v, u"effect"_s);
                    e->ai_will_do = _block(v, u"ai_will_do"_s);
                }
                entries.push_back(e);
                if (ev && e->id && !e->id->isEmpty())
                    by_id.insert(*e->id, e.get());
            }
        }
    }
    // an event whose own effects change what its trigger checks (a flag, a modifier, the government …) happens once
    // per nation (until something changes it back) — not again every MTTH
    for (const EntryP &e : entries) {
        if (e->is_event() && e->trigger && !e->trigger->empty()) {
            const QSet<QString> trig = _tokens(View(e->trigger));
            QSet<QString> effs;
            for (const Option &o : e->options)
                _changes(View(o.effects), effs);
            _changes(View(e->immediate), effs);
            e->self_limit = trig.intersects(effs);
        }
    }
    // what fires what, and what the mod ever sets
    for (const EntryP &e : entries) {
        for (const Option &o : e->options)
            _scan(View(o.effects), e.get());
        _scan(View(e->immediate), e.get());
        _scan(View(e->effect), e.get());
    }
    _scan_history();
    _prev = nullptr;
    try {
        for (const Node &n : script::parse(script::read_text(mod.p({u"common"_s, u"on_actions.txt"_s})))) {
            if (!(has(n) && n.is_block))
                continue;
            for (const Node &c : n.kids) {
                const OptStr ev = c.is_block ? std::nullopt : OptStr(c.value);
                if (ev && !ev->isEmpty() && py::isdigit(py::strip(*ev)))
                    fired_by[py::strip(*ev)].push_back({nullptr, u"%1 (common/on_actions.txt, weight %2)"_s.arg(
                                                                     n.key, c.has_key ? c.key : u"None"_s)});
            }
        }
    } catch (const fs::OSError &) {
    }
    for (const EntryP &e : entries) {             // elections: the game fires election events itself
        if (!e->is_event())
            continue;
        const OptStr el = _val(View(e->node->kids), u"election"_s);
        if (el && !el->isEmpty() && yes(el))
            fired_by[e->id ? *e->id : NO_ID].push_back({nullptr, u"an election (the game fires it for its issue group)"_s});
    }
}

void EventDB::_scan(View nodes, Entry *src)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        const QString k = c.has_key ? c.key : QString();
        if (c.has_key && (k == u"country_event" || k == u"province_event")) {
            if (!c.is_block) {
                fired_by[py::strip(c.value)].push_back({src, u"effect"_s});
            } else {
                const OptStr id = _val(View(c.kids), u"id"_s);
                if (id && !id->isEmpty()) {
                    const OptStr d = _val(View(c.kids), u"days"_s);
                    fired_by[py::strip(*id)].push_back(
                        {src, (d && !d->isEmpty()) ? u"effect, after %1 days"_s.arg(*d) : u"effect"_s});
                }
            }
            continue;
        }
        if (c.has_key) {
            if (k == u"set_country_flag" && !c.is_block)
                set_flags.insert(c.value);
            else if (k == u"set_global_flag" && !c.is_block)
                set_gflags.insert(c.value);
            else if (k == u"add_country_modifier" && c.is_block) {
                const OptStr nm = _val(View(c.kids), u"name"_s);
                if (nm && !nm->isEmpty())
                    set_mods.insert(*nm);
            } else if (k == u"add_province_modifier" && c.is_block) {
                const OptStr nm = _val(View(c.kids), u"name"_s);
                if (nm && !nm->isEmpty())
                    set_pmods.insert(*nm);
            } else if ((k == u"set_variable" || k == u"change_variable") && c.is_block) {
                const OptStr w = _val(View(c.kids), u"which"_s);
                if (w && !w->isEmpty())
                    set_vars.insert(*w);
            }
        }
        if (c.is_block)
            _scan(View(c.kids), src);
    }
}

using HistorySets = std::array<QSet<QString>, 5>;

void EventDB::_scan_history()
{
    // Flags and modifiers the start history already sets (read once per mod).
    auto cached = std::static_pointer_cast<const HistorySets>(mod.event_history_sets);
    if (!cached) {
        HistorySets mine{set_flags, set_gflags, set_mods, set_pmods, set_vars};
        set_flags.clear();
        set_gflags.clear();
        set_mods.clear();
        set_pmods.clear();
        set_vars.clear();
        _scan_history_files();
        cached = std::make_shared<const HistorySets>(HistorySets{set_flags, set_gflags, set_mods, set_pmods, set_vars});
        mod.event_history_sets = std::const_pointer_cast<HistorySets>(cached);
        set_flags = mine[0];
        set_gflags = mine[1];
        set_mods = mine[2];
        set_pmods = mine[3];
        set_vars = mine[4];
    }
    set_flags.unite((*cached)[0]);
    set_gflags.unite((*cached)[1]);
    set_mods.unite((*cached)[2]);
    set_pmods.unite((*cached)[3]);
    set_vars.unite((*cached)[4]);
}

void EventDB::_scan_history_files()
{
    for (const QString &sub : {u"countries"_s, u"provinces"_s}) {
        const QString d = mod.p({u"history"_s, sub});
        if (!fs::isdir(d))
            continue;
        for (const QString &path : glob_txt_recursive(d)) {
            OptStr got;
            try {
                got = text_with(path, {"flag", "modifier"});
            } catch (const fs::OSError &) {
                continue;
            }
            if (!got)
                continue;
            const QString &txt = *got;
            if (txt.contains(u"flag"_s) || txt.contains(u"modifier"_s)) {
                const Nodes nodes = script::parse(txt);
                _scan(View(nodes), nullptr);
            }
        }
    }
}

Entry *EventDB::entry_at(const QString &rel, qsizetype pos)
{
    // The event / decision whose block has the position (or None).
    if (!_at) {
        _at.emplace();
        for (const EntryP &e : entries)
            (*_at)[e->rel.value_or(QString())].push_back(e.get());
        for (auto &lst : *_at)
            std::stable_sort(lst.begin(), lst.end(), [](const Entry *a, const Entry *b) { return a->span.first < b->span.first; });
    }
    const auto it = _at->find(rel);
    if (it == _at->end())
        return nullptr;
    for (auto e = it->rbegin(); e != it->rend(); ++e)
        if ((*e)->span.first <= pos)
            return pos <= (*e)->span.second ? *e : nullptr;
    return nullptr;
}

const EventDB::Holders &EventDB::history_holders()
{
    // {("cflag" / "gflag" / "pflag", name): {tag or pid}} — flags the start history sets, and on whom.
    if (mod.event_flag_holders)
        return *std::static_pointer_cast<const Holders>(mod.event_flag_holders);
    auto got = std::make_shared<Holders>();
    for (const QString &sub : {u"countries"_s, u"provinces"_s}) {
        const QString d = mod.p({u"history"_s, sub});
        if (!fs::isdir(d))
            continue;
        for (const QString &path : glob_txt_recursive(d)) {
            OptStr read;
            try {
                read = text_with(path, {"_flag"});
            } catch (const fs::OSError &) {
                continue;
            }
            if (!read || !read->contains(u"_flag"_s))
                continue;
            const QString &txt = *read;
            const QString name = pypath::basename(path);
            OptStr who;
            if (sub == u"countries") {
                who = name.left(3);
            } else {
                const auto m = rmatch(u"^\\s*(\\d+)"_s, name);
                if (m.hasMatch())
                    who = m.captured(1);
            }
            auto it = rx(u"set_(country|global|province)_flag\\s*=\\s*\"?([A-Za-z0-9_.\\-]+)"_s).globalMatch(txt);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString kind = m.captured(1) == u"country" ? u"cflag"_s
                                     : m.captured(1) == u"global" ? u"gflag"_s
                                                                  : u"pflag"_s;
                if (who && !who->isEmpty())
                    (*got)[{kind, m.captured(2)}].insert(sub == u"countries" ? store::Key(*who)
                                                                             : store::Key(py::to_int(*who)));
            }
        }
    }
    mod.event_flag_holders = got;
    return *got;
}

QString EventDB::name(const Entry &e) const
{
    OptStr t;
    if (!e.title.isEmpty())
        if (const QString *l = mod.loc.get(e.title))
            t = *l;
    if ((!t || t->isEmpty()) && !e.title.isEmpty() && !plain_key(e.title))
        t = e.title;                              // the title written as plain text
    if (t && !t->isEmpty())
        return *t;
    if (e.kind == u"decision")
        return QString(e.key).replace(u'_', u' ');
    return u"Event "_s + fmt_s(e.id);
}

QString EventDB::description(const Entry &e) const
{
    if (e.desc.isEmpty())
        return {};
    OptStr t;
    if (const QString *l = mod.loc.get(e.desc))
        t = *l;
    if ((!t || t->isEmpty()) && !plain_key(e.desc))
        t = e.desc;
    return t.value_or(QString());
}

// ---------------------------------------------------------------------- the start
World::World(moddata::Mod &mod_, document::Document &doc_) : mod(mod_), doc(doc_)
{
    start_year = doc.date ? std::get<0>(*doc.date) : 1836;
    for (const auto &[pid, pr] : mod.provinces) {
        if (pr.sea)
            continue;
        const history::ProvState &ps = doc.prov_state(pid);
        if (ps.owner && !ps.owner->isEmpty()) {
            owner.insert(pid, *ps.owner);
            owned[*ps.owner] << pid;
        }
    }
    const store::StoreP *np = doc.maps.get(u"nations"_s);
    if (!np)
        throw std::out_of_range("KeyError: 'nations'");
    nations = dynamic_cast<countryhist::NationEdits *>(np->get());
    for (const auto &kv : owned)
        if (mod.countries.contains(kv.first))
            tags << kv.first;
    std::sort(tags.begin(), tags.end());
    try {
        for (const QString &t : diplomacy::great_powers(mod, *nations))
            gps.insert(t);
    } catch (...) {
        gps.clear();
    }
    try {
        if (const store::StoreP *c = doc.maps.get(u"continent"_s)) {
            auto &mf = doc.map<mapfiles::MembershipFile>(u"continent"_s);
            (void)c;
            for (const auto &[pid, names] : mf.membership())
                if (!names.isEmpty())
                    cont.insert(pid, names[0]);
        }
    } catch (...) {
    }
    if (const store::StoreP *d = doc.maps.get(u"diplomacy"_s))
        dip = dynamic_cast<diplomacy::DiploEdits *>(d->get());
    for (const auto &[c, info] : mod.cultures)
        if (!info.group.isEmpty())
            culture_group.insert(c, info.group);
    if (countryhist::NationData *data = mod.nation_data.get()) {
        for (const QString &t : tags)
            for (const countryhist::Party &p : data->parties(t))
                party_ideology.insert({t, p.name}, p.ideology);
        reform_issue = data->reform_of;
        issues = data->reform_issues;
        for (const auto &kv : data->techs)
            techs.insert(kv.first);
        for (const auto &kv : data->inventions)
            inventions.insert(kv.first);
    }
}

const countryhist::NationState *World::st(const OptStr &tag)
{
    if (!tag) {                                   // (nations.state(None): a nation without a file — empty)
        if (!_none_state)
            _none_state = std::make_shared<countryhist::NationState>(nations->state(QString()));
        return _none_state.get();
    }
    if (!_state.contains(*tag)) {
        std::shared_ptr<countryhist::NationState> s;
        try {
            s = std::make_shared<countryhist::NationState>(nations->state(*tag));
        } catch (...) {
            s = nullptr;
        }
        _state.insert(*tag, s);
    }
    return _state.get(*tag)->get();
}

const history::ProvState &World::pst(long long pid) { return doc.prov_state(pid); }

// ---------------------------------------------------------------------- conditions
// py::upper(k) == "AND" / "OR" / "NOT" without a new text per node (judged thousands of times a second): an upper-cased
// text is never shorter, so a key of more than 3 letters is none of them; an ASCII key compares letter by letter; any
// other goes through py::upper as before. 1 = AND, 2 = OR, 3 = NOT, 0 = none.
static int logic_word(const QString &k)
{
    const qsizetype n = k.size();
    if (n < 2 || n > 3)
        return 0;
    const QChar *d = k.constData();
    for (qsizetype i = 0; i < n; ++i)
        if (d[i].unicode() >= 0x80) {
            const QString ku = py::upper(k);
            return ku == u"AND" ? 1 : ku == u"OR" ? 2 : ku == u"NOT" ? 3 : 0;
        }
    auto up = [d](qsizetype i) { const char16_t c = d[i].unicode(); return c >= u'a' && c <= u'z' ? char16_t(c - 32) : c; };
    if (n == 2)
        return up(0) == u'O' && up(1) == u'R' ? 2 : 0;
    if (up(0) == u'A' && up(1) == u'N' && up(2) == u'D')
        return 1;
    if (up(0) == u'N' && up(1) == u'O' && up(2) == u'T')
        return 3;
    return 0;
}
const QSet<QString> COUNTRY_SCALARS = {u"government"_s, u"primary_culture"_s, u"religion"_s, u"nationalvalue"_s,
                                       u"civilized"_s};

Tri Judge::all(View nodes, const Scope &sc, const Scope &self, std::optional<long long> year)
{
    Tri out = true;                               // and3: stops at the first False
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Tri v = one(nodes[i], sc, self, year);
        if (v == Tri(false))
            return false;
        if (!v)
            out = std::nullopt;
    }
    return out;
}

static Tri or3_of(Judge &j, const Nodes &nodes, const Scope &sc, const Scope &self, std::optional<long long> year)
{
    Tri out = false;                              // or3: stops at the first True
    for (const Node &x : nodes) {
        const Tri v = j.one(x, sc, self, year);
        if (v == Tri(true))
            return true;
        if (!v)
            out = std::nullopt;
    }
    return out;
}
static Tri and3(const std::vector<Tri> &vals)
{
    Tri out = true;
    for (const Tri &v : vals) {
        if (v == Tri(false))
            return false;
        if (!v)
            out = std::nullopt;
    }
    return out;
}
static Tri or3(const std::vector<Tri> &vals)
{
    Tri out = false;
    for (const Tri &v : vals) {
        if (v == Tri(true))
            return true;
        if (!v)
            out = std::nullopt;
    }
    return out;
}

Tri Judge::one(const Node &c, const Scope &sc, const Scope &self, std::optional<long long> year)
{
    if (!c.has_key)
        return true;
    const QString &k = c.key;
    const int lw = logic_word(k);
    if (lw == 1)
        return c.is_block ? all(View(c.kids), sc, self, year) : Tri(true);
    if (lw == 2)
        return c.is_block ? or3_of(*this, c.kids, sc, self, year) : Tri(true);
    if (lw == 3)                             // NOT = { a b } is "not (a or b)" in Paradox scripts
        return c.is_block ? not3(or3_of(*this, c.kids, sc, self, year)) : Tri(true);
    if (c.is_block)
        return scope(c, sc, self, year);
    const Tri r = sc.kind == u'c' ? country(k, c.value, sc.tag, self, year) : prov(k, c.value, sc.pid, self, year);
    if (!r)
        unknown[k] += 1;
    return r;
}

Tri Judge::scope(const Node &c, const Scope &sc, const Scope &self, std::optional<long long> year)
{
    const QString &k = c.key;
    const View v(c.kids);
    if (k == u"limit")
        return all(v, sc, self, year);
    if (k == u"THIS" || k == u"ROOT")
        return all(v, self, self, year);
    if (k == u"FROM")
        return std::nullopt;
    if (is_tag(k) && w.mod.countries.contains(k))
        return all(v, Scope::c(k), self, year);
    if (py::isdigit(k))
        return all(v, Scope::p(py::to_int(k)), self, year);
    if ((k == u"owner" || k == u"controller") && sc.kind == u'p') {
        OptStr o;
        const OptStr own = sc.pid ? OptStr(w.owner.value(*sc.pid)) : std::nullopt;
        const OptStr owner_of = (own && !own->isEmpty()) ? own : std::nullopt;
        if (k == u"owner")
            o = owner_of;
        else {
            const OptStr ctl = sc.pid ? w.pst(*sc.pid).controller : std::nullopt;
            o = (ctl && !ctl->isEmpty()) ? ctl : owner_of;
        }
        return (o && !o->isEmpty()) ? all(v, Scope::c(o), self, year) : Tri(false);
    }
    if (k == u"capital_scope" && sc.kind == u'c') {
        const auto cap = _capital(sc.tag);
        return (cap && *cap) ? all(v, Scope::p(cap), self, year) : Tri(false);
    }
    static const QSet<QString> OWNED = {u"any_owned_province"_s, u"any_owned"_s, u"any_core"_s, u"all_core"_s,
                                        u"any_state"_s};
    if (OWNED.contains(k) && sc.kind == u'c') {
        QList<long long> pids = sc.tag ? w.owned.value(*sc.tag) : QList<long long>();
        if (k == u"any_core" || k == u"all_core") {
            pids.clear();
            for (const auto &kv : w.owner)
                if (sc.tag && w.pst(kv.first).cores.contains(*sc.tag))
                    pids << kv.first;
        }
        std::vector<Tri> vals;
        for (long long p : pids)
            vals.push_back(all(v, Scope::p(p), self, year));
        return k == u"all_core" ? and3(vals) : or3(vals);
    }
    if (k == u"any_country" || k == u"all_country") {
        std::vector<Tri> vals;
        for (const QString &t : w.tags)
            vals.push_back(all(v, Scope::c(t), self, year));
        return k == u"all_country" ? and3(vals) : or3(vals);
    }
    static const QSet<QString> MAYBE = {u"any_neighbor_province"_s, u"any_neighbor_country"_s, u"any_sphere_member"_s,
                                        u"any_greater_power"_s, u"war_countries"_s, u"any_pop"_s, u"sphere_owner"_s,
                                        u"overlord"_s, u"cultural_union"_s, u"location"_s};
    if (MAYBE.contains(k))
        return std::nullopt;
    if (k == u"upper_house" && sc.kind == u'c') {
        const countryhist::NationState *st = w.st(sc.tag);
        const OptStr ide = _val(v, u"ideology"_s);
        const auto val = _num(_val(v, u"value"_s));
        if (!st || !ide || !val)
            return std::nullopt;
        const QString *share_s = st->upper_house.get(*ide);
        const double share = _num(share_s ? OptStr(*share_s) : std::nullopt, 0.0) / 100.0;
        return share >= *val;
    }
    if (k == u"check_variable") {
        const OptStr which = _val(v, u"which"_s);
        const double val = _num(_val(v, u"value"_s), 0.0);
        if (which && !which->isEmpty() && !db.set_vars.contains(*which))
            return 0.0 >= val;
        return std::nullopt;
    }
    if (const moddata::Region *r = w.mod.regions.get(k)) {   // a state (STA_x = { … }): all its provinces
        std::vector<long long> pids;
        for (long long p : r->provinces)
            if (w.owner.contains(p))
                pids.push_back(p);
        if (pids.empty())
            return false;
        Tri out = true;                           // and3 over a generator: stops at the first False
        for (long long p : pids) {
            const Tri x = all(v, Scope::p(p), self, year);
            if (x == Tri(false))
                return false;
            if (!x)
                out = std::nullopt;
        }
        return out;
    }
    return std::nullopt;
}

std::optional<long long> Judge::_capital(const OptStr &tag)
{
    const countryhist::NationState *st = w.st(tag);
    const auto cap = st ? _num(st->at(u"capital"_s)) : std::nullopt;
    return cap ? std::optional<long long>(py::trunc_int(*cap)) : std::nullopt;
}

OptStr Judge::_tag(const QString &v, const Scope &self)
{
    if (v == u"THIS" || v == u"ROOT") {
        if (self.kind == u'c')
            return self.tag;
        if (!self.pid || !w.owner.contains(*self.pid))
            return std::nullopt;
        return w.owner.value(*self.pid);
    }
    if (v == u"FROM")
        return std::nullopt;
    return v;
}

static QString low_strip(const OptStr &s) { return py::lower(py::strip(s.value_or(QString()))); }

Tri Judge::country(const QString &k, const QString &v, const OptStr &tag, const Scope &self, std::optional<long long> year)
{
    if (!tag)
        return std::nullopt;
    const countryhist::NationState *st = w.st(tag);
    const bool exists = !w.owned.value(*tag).isEmpty();
    if (k == u"year") {
        const auto y = _num(v);
        return (!year || !y) ? Tri() : Tri(double(*year) >= *y);
    }
    if (k == u"month" || k == u"Month")
        return std::nullopt;
    if (k == u"tag") {
        const OptStr t = _tag(v, self);
        return t ? Tri(*tag == *t) : Tri();
    }
    if (k == u"exists") {
        if (is_tag(v))                            // exists = TAG
            return !w.owned.value(v).isEmpty();
        return exists == yes(v);
    }
    if (k == u"ai")
        return std::nullopt;
    if (k == u"war" || k == u"is_mobilised" || k == u"is_disarmed" || k == u"has_recently_lost_war" || k == u"is_at_war")
        return !yes(v);
    if (k == u"is_greater_power")
        return w.gps.contains(*tag) == yes(v);
    if (k == u"is_secondary_power")
        return std::nullopt;
    if (k == u"rank") {
        const auto r = _num(v);
        if (!r)
            return std::nullopt;
        if (w.gps.contains(*tag) && *r >= 8)
            return true;
        return *r > 8 ? Tri() : (!w.gps.contains(*tag) ? Tri(false) : Tri());
    }
    if (!st)
        return std::nullopt;
    if (COUNTRY_SCALARS.contains(k)) {
        const QString val = low_strip(st->at(k));
        const QString want = low_strip(v);
        if (k == u"civilized")
            return (val == u"yes") == yes(v);
        if (want == u"this" || want == u"root") {
            const countryhist::NationState *other = w.st(_tag(u"THIS"_s, self));
            return other ? Tri(val == low_strip(other->at(k))) : Tri();
        }
        return val == want;
    }
    if (k == u"is_primary_culture") {
        if (v == u"THIS" || v == u"ROOT")
            return std::nullopt;
        return st->at(u"primary_culture"_s) == OptStr(v);
    }
    if (k == u"is_accepted_culture") {
        if (v == u"THIS" || v == u"ROOT")
            return std::nullopt;
        return st->at(u"primary_culture"_s) == OptStr(v) || st->cultures.contains(v);
    }
    if (k == u"is_culture_group") {
        if (v == u"THIS" || v == u"ROOT")
            return std::nullopt;
        const OptStr pc = st->at(u"primary_culture"_s);
        return pc && w.culture_group.contains(*pc) && w.culture_group.value(*pc) == v;
    }
    if (k == u"capital") {
        const auto cap = _capital(tag);
        const auto n = _num(v);
        return (!cap || !n) ? Tri() : Tri(*cap == py::trunc_int(*n));
    }
    if (k == u"owns") {
        const auto p = _num(v);
        if (!p)
            return std::nullopt;
        const long long pid = py::trunc_int(*p);
        return w.owner.contains(pid) && w.owner.value(pid) == *tag;
    }
    if (k == u"num_of_cities") {
        const auto n = _num(v);
        return n ? Tri(double(w.owned.value(*tag).size()) >= *n) : Tri();
    }
    if (k == u"plurality" || k == u"literacy" || k == u"prestige" || k == u"consciousness" ||
        k == u"average_consciousness") {
        const QString key = k == u"average_consciousness" ? u"consciousness"_s : k;
        auto have = _num(st->at(key));
        const auto want = _num(v);
        if (!have || !want)
            return std::nullopt;
        if (k == u"literacy" && *have > 1.0)
            *have /= 100.0;
        return *have >= *want;
    }
    if (k == u"ruling_party_ideology") {
        const OptStr rp = st->at(u"ruling_party"_s);
        const QString ide = rp ? w.party_ideology.value({*tag, *rp}) : QString();
        return ide.isEmpty() ? Tri() : Tri(ide == v);
    }
    if (k == u"tech_school" || k == u"schools")
        return st->at(u"schools"_s).value_or(QString()) == v;
    if (k == u"invention")
        return st->inventions.contains(v);
    if (w.techs.contains(k) && _num(v))
        return st->techs.contains(k) == (*_num(v) >= 1);
    if (w.issues.contains(k)) {                   // a reform: its option
        const QString *o = st->reforms.get(k);
        return o && *o == v;
    }
    if (k == u"has_country_flag")
        return !db.set_flags.contains(v) ? Tri(false) : Tri();
    if (k == u"has_global_flag")
        return !db.set_gflags.contains(v) ? Tri(false) : Tri();
    if (k == u"has_country_modifier")
        return !db.set_mods.contains(v) ? Tri(false) : Tri();
    if (k == u"is_vassal") {
        if (!w.dip)
            return std::nullopt;
        return w.dip->overlord(*tag).has_value() == yes(v);
    }
    if (k == u"is_colonial" || k == u"colonial_nation")
        return std::nullopt;
    if (k == u"continent") {
        const auto cap = _capital(tag);
        if (!cap)
            return std::nullopt;
        return w.cont.contains(*cap) && w.cont.value(*cap) == v;
    }
    if (k == u"is_core") {
        const auto p = _num(v);
        return p ? Tri(w.pst(py::trunc_int(*p)).cores.contains(*tag)) : Tri();
    }
    return std::nullopt;                          // (government_weight and the rest: can't tell)
}

Tri Judge::prov(const QString &k, const QString &v, std::optional<long long> pid, const Scope &self,
                std::optional<long long> year)
{
    if (!pid)
        return std::nullopt;
    if (k == u"year") {
        const auto y = _num(v);
        return (!year || !y) ? Tri() : Tri(double(*year) >= *y);
    }
    if (k == u"month" || k == u"Month")
        return std::nullopt;
    if (k == u"province_id") {
        const auto p = _num(v);
        return p ? Tri(*pid == py::trunc_int(*p)) : Tri();
    }
    const history::ProvState &st = w.pst(*pid);
    const OptStr own = w.owner.contains(*pid) ? OptStr(w.owner.value(*pid)) : std::nullopt;
    if (k == u"owned_by" || k == u"owner") {
        const OptStr t = _tag(v, self);
        return t ? Tri(own == t) : Tri();
    }
    if (k == u"controlled_by") {
        const OptStr t = _tag(v, self);
        if (!t)
            return std::nullopt;
        const OptStr ctl = (st.controller && !st.controller->isEmpty()) ? st.controller : own;
        return ctl == t;
    }
    if (k == u"is_core") {
        const OptStr t = _tag(v, self);
        return t ? Tri(st.cores.contains(*t)) : Tri();
    }
    if (k == u"trade_goods")
        return st.trade_goods == OptStr(v);
    if (k == u"life_rating") {
        const auto want = _num(v);
        return (!st.life_rating || !want) ? Tri() : Tri(double(*st.life_rating) >= *want);
    }
    if (k == u"continent")
        return w.cont.contains(*pid) ? Tri(w.cont.value(*pid) == v) : Tri();
    if (k == u"is_colonial")
        return (st.colonial > 0) == yes(v);
    if (k == u"has_province_modifier")
        return !db.set_pmods.contains(v) ? Tri(false) : Tri();
    if (k == u"has_province_flag")
        return std::nullopt;
    if (k == u"is_capital") {
        if (!own || own->isEmpty())
            return std::nullopt;
        return (_capital(own) == std::optional<long long>(*pid)) == yes(v);
    }
    if (k == u"region") {
        const moddata::Province *pr = w.mod.provinces.get(*pid);
        return pr ? Tri(pr->region == OptStr(v)) : Tri();
    }
    static const QSet<QString> OWNERS = {u"exists"_s, u"tag"_s, u"civilized"_s, u"government"_s, u"is_greater_power"_s,
                                         u"has_country_flag"_s, u"has_global_flag"_s, u"war"_s, u"has_country_modifier"_s,
                                         u"primary_culture"_s, u"nationalvalue"_s};
    if (OWNERS.contains(k))                       // a nation's condition in a province: its owner's
        return (!own || own->isEmpty()) ? Tri() : country(k, v, own, self, year);
    return std::nullopt;
}

const QSet<QString> POT_COUNTRY = {u"tag"_s,     u"primary_culture"_s, u"is_primary_culture"_s, u"is_culture_group"_s,
                                   u"religion"_s, u"continent"_s,      u"capital"_s,            u"is_core"_s,
                                   u"owns"_s,    u"has_country_flag"_s, u"has_global_flag"_s,   u"has_country_modifier"_s};
const QSet<QString> POT_PROV = {u"province_id"_s, u"owned_by"_s,       u"owner"_s,            u"is_core"_s,
                                u"trade_goods"_s, u"continent"_s,      u"region"_s,           u"has_province_modifier"_s,
                                u"tag"_s,         u"primary_culture"_s, u"religion"_s,        u"has_country_flag"_s,
                                u"has_global_flag"_s, u"has_country_modifier"_s};

Tri PotentialJudge::country(const QString &k, const QString &v, const OptStr &tag, const Scope &self,
                            std::optional<long long>)
{
    return POT_COUNTRY.contains(k) ? Judge::country(k, v, tag, self, std::nullopt) : Tri();
}

Tri PotentialJudge::prov(const QString &k, const QString &v, std::optional<long long> pid, const Scope &self,
                         std::optional<long long>)
{
    return POT_PROV.contains(k) ? Judge::prov(k, v, pid, self, std::nullopt) : Tri();
}

Tri potential(Judge &pjudge, const Entry &e, const Scope &scope)
{
    // True / None (maybe) / False (never) — e for the scope, by who / where alone.
    return pjudge.all(View(e.is_event() ? e.trigger : e.potential), scope, scope, std::nullopt);
}

// ---------------------------------------------------------------------- chances
bool _has_year(View nodes)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (is(c, u"year"_s) || (c.is_block && _has_year(View(c.kids))))
            return true;
    }
    return false;
}

Chance chance(Judge &judge, const Entry &e, const Scope &scope)
{
    // The Chance of entry e for scope ("c", tag) or ("p", pid).
    Chance out;
    const long long y0 = judge.w.start_year;
    if (!e.is_event()) {
        out.can = judge.all(View(e.potential), scope, scope, y0);
        out.allow = out.can != Tri(false) ? judge.all(View(e.allow), scope, scope, y0) : Tri(false);
        return out;
    }
    bool yearly = _has_year(View(e.trigger));
    for (const auto &m : e.mods)
        yearly = yearly || _has_year(View(m.second));
    std::vector<Tri> states;
    const Tri first = judge.all(View(e.trigger), scope, scope, y0);
    for (int i = 0; i < YEARS; ++i)
        states.push_back((i == 0 || !yearly) ? first : judge.all(View(e.trigger), scope, scope, y0 + i));
    out.years = states;
    out.can = or3(states);
    if (out.can == Tri(false) || e.triggered_only || !e.mtth || *e.mtth == 0.0)
        return out;
    double p_lo_keep = 1.0, p_hi_keep = 1.0, t_lo = 0.0, t_hi = 0.0;
    std::optional<double> m_lo, m_hi;
    std::vector<std::pair<double, Tri>> cache;
    for (int i = 0; i < YEARS; ++i) {
        const Tri s = states[size_t(i)];
        if (s == Tri(false))
            continue;
        const long long y = y0 + i;
        double lo = *e.mtth, hi = *e.mtth;
        std::vector<std::pair<double, Tri>> mods;
        if (i == 0 || yearly) {
            for (const auto &[f, m] : e.mods)
                mods.emplace_back(f, judge.all(View(m), scope, scope, y));
            cache = mods;
        } else {
            mods = cache;
        }
        for (const auto &[f, c] : mods) {
            if (c == Tri(true)) {
                lo *= f;
                hi *= f;
            } else if (!c) {
                lo *= std::min(1.0, f);
                hi *= std::max(1.0, f);
            }
        }
        lo = std::max(lo, 1.0);
        hi = std::max(hi, 1.0);
        m_lo = m_lo ? std::min(*m_lo, lo) : lo;
        m_hi = m_hi ? std::max(*m_hi, hi) : hi;
        // this year: at best with the shortest time, at worst with the longest (and only if surely possible)
        p_hi_keep *= std::pow(0.5, 365.0 / lo);
        t_hi += LN2 * 365.0 / lo;
        if (s == Tri(true)) {
            p_lo_keep *= std::pow(0.5, 365.0 / hi);
            t_lo += LN2 * 365.0 / hi;
        }
    }
    out.mtth = m_lo ? std::optional<std::pair<double, double>>({*m_lo, *m_hi}) : std::nullopt;
    out.p = {1.0 - p_lo_keep, 1.0 - p_hi_keep};
    out.times = !(e.once || e.self_limit) ? std::make_pair(t_lo, t_hi)
                                          : std::make_pair(std::min(1.0, t_lo), std::min(1.0, t_hi));
    return out;
}

static OrderedMap<QString, QString> om(std::initializer_list<std::pair<QString, QString>> l)
{
    OrderedMap<QString, QString> m;
    for (const auto &kv : l)
        m.insert(kv.first, kv.second);
    return m;
}
const OrderedMap<QString, QString> GROUP_KEYS = om({
    {u"primary_culture"_s, u"culture"_s},   {u"is_primary_culture"_s, u"culture"_s}, {u"is_accepted_culture"_s, u"culture"_s},
    {u"is_culture_group"_s, u"culture group"_s}, {u"culture"_s, u"culture"_s},  {u"religion"_s, u"religion"_s},
    {u"is_state_religion"_s, u"religion"_s}, {u"continent"_s, u"continent"_s},  {u"capital"_s, u"capital"_s},
    {u"province_id"_s, u"provinces"_s},     {u"region"_s, u"region"_s},         {u"owns"_s, u"provinces"_s},
    {u"is_core"_s, u"cores"_s},             {u"trade_goods"_s, u"RGO"_s},        {u"terrain"_s, u"terrain"_s}});

static bool owner_key(const Node &x)
{
    return (is(x, u"tag"_s) || is(x, u"owner"_s) || is(x, u"owned_by"_s)) && !x.is_block && is_tag(x.value);
}

std::pair<QSet<QString>, QStringList> restrict_of(View nodes)
{
    // (tags, group kinds) that a condition list limits to — tags only where every way through needs one.
    QSet<QString> tags;
    QStringList group;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (!c.has_key)
            continue;
        const int lw = logic_word(c.key);
        if (lw == 1 && c.is_block) {
            const auto [t, g] = restrict_of(View(c.kids));
            tags.unite(t);
            group += g;
        } else if (lw == 2 && c.is_block) {
            QSet<QString> ts;
            bool ok = true;
            for (const Node &x : c.kids) {
                if (!x.has_key)
                    continue;
                if (owner_key(x))
                    ts.insert(x.value);
                else if (logic_word(x.key) == 1 && x.is_block) {
                    const auto t = restrict_of(View(x.kids)).first;
                    if (!t.isEmpty())
                        ts.unite(t);
                    else
                        ok = false;
                } else {
                    ok = false;
                }
            }
            if (ok && !ts.isEmpty()) {
                tags.unite(ts);
            } else {
                for (const Node &x : c.kids)
                    if (x.has_key && GROUP_KEYS.contains(x.key)) {
                        group << GROUP_KEYS.value(x.key);
                        break;
                    }
            }
        } else if (owner_key(c)) {
            tags.insert(c.value);
        } else if (GROUP_KEYS.contains(c.key)) {
            group << GROUP_KEYS.value(c.key);
        }
    }
    return {tags, group};
}

bool player_only(View nodes)
{
    // `ai = no` needed (not inside OR / NOT): only the player's nation.
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (is(c, u"ai"_s) && !c.is_block && py::lower(py::strip(c.value)) == u"no")
            return true;
        if (has(c) && logic_word(c.key) == 1 && c.is_block && player_only(View(c.kids)))
            return true;
    }
    return false;
}

Class classify(const Entry &e)
{
    const View nodes(e.is_event() ? e.trigger : e.potential);
    const auto [tags, group] = restrict_of(nodes);
    if (!tags.isEmpty())
        return {u"tags"_s, tags, {}};
    if (player_only(nodes) || (!e.is_event() && player_only(View(e.allow))))
        return {u"player"_s, {}, {}};
    if (!group.isEmpty())
        return {u"group"_s, {}, uniq(group)};
    return {u"universal"_s, {}, {}};
}

std::vector<Scope> scopes_of(const World &world, const Entry &e)
{
    std::vector<Scope> out;
    if (e.province()) {
        QList<long long> pids;
        for (const auto &kv : world.owner)
            pids << kv.first;
        std::sort(pids.begin(), pids.end());
        for (long long p : pids)
            out.push_back(Scope::p(p));
    } else {
        for (const QString &t : world.tags)
            out.push_back(Scope::c(t));
    }
    return out;
}

// ---------------------------------------------------------------------- effects, readable
const QHash<QString, QString> NICE = {
    {u"prestige"_s, u"Prestige"_s}, {u"badboy"_s, u"Infamy"_s}, {u"money"_s, u"Money"_s}, {u"treasury"_s, u"Money"_s},
    {u"militancy"_s, u"Militancy"_s}, {u"consciousness"_s, u"Consciousness"_s}, {u"literacy"_s, u"Literacy"_s},
    {u"plurality"_s, u"Plurality"_s}, {u"research_points"_s, u"Research points"_s},
    {u"war_exhaustion"_s, u"War exhaustion"_s}, {u"life_rating"_s, u"Life rating"_s},
    {u"infrastructure"_s, u"Infrastructure"_s}, {u"scaled_militancy"_s, u"Militancy (scaled)"_s},
    {u"scaled_consciousness"_s, u"Consciousness (scaled)"_s}, {u"add_core"_s, u"Gains a core of"_s},
    {u"remove_core"_s, u"Loses the core of"_s}, {u"secede_province"_s, u"The province goes to"_s},
    {u"annex_to"_s, u"Is annexed by"_s}, {u"inherit"_s, u"Inherits"_s}, {u"change_tag"_s, u"Becomes"_s},
    {u"government"_s, u"Government →"_s}, {u"civilized"_s, u"Civilized"_s}, {u"set_country_flag"_s, u"Sets the flag"_s},
    {u"clr_country_flag"_s, u"Clears the flag"_s}, {u"set_global_flag"_s, u"Sets the global flag"_s},
    {u"clr_global_flag"_s, u"Clears the global flag"_s}, {u"nationalvalue"_s, u"National value →"_s},
    {u"primary_culture"_s, u"Primary culture →"_s}, {u"add_accepted_culture"_s, u"Accepts the culture"_s},
    {u"remove_accepted_culture"_s, u"No longer accepts"_s}, {u"religion"_s, u"Religion →"_s},
    {u"trade_goods"_s, u"The province's RGO →"_s}, {u"ruling_party_ideology"_s, u"Ruling party →"_s},
    {u"change_province_name"_s, u"Renamed"_s}, {u"capital"_s, u"Capital →"_s}, {u"release"_s, u"Releases"_s},
    {u"release_vassal"_s, u"Releases as a puppet"_s}, {u"create_vassal"_s, u"Makes a puppet of"_s},
    {u"end_war"_s, u"Ends the war with"_s}, {u"leave_alliance"_s, u"Leaves the alliance with"_s},
    {u"create_alliance"_s, u"Allies with"_s}, {u"relation"_s, u"Relations"_s}, {u"casus_belli"_s, u"Casus belli"_s},
    {u"add_casus_belli"_s, u"Casus belli"_s}, {u"war"_s, u"Declares war"_s}, {u"reduce_pop"_s, u"Pops shrink ×"_s},
    {u"add_country_modifier"_s, u"Country modifier"_s}, {u"add_province_modifier"_s, u"Province modifier"_s},
    {u"fort"_s, u"Fort"_s}, {u"naval_base"_s, u"Naval base"_s}, {u"railroad"_s, u"Railroad"_s},
    {u"activate_technology"_s, u"Gets the technology"_s}, {u"activate_invention"_s, u"Gets the invention"_s},
    {u"enable_ideology"_s, u"Enables the ideology"_s}, {u"social_reform"_s, u"Social reform →"_s},
    {u"political_reform"_s, u"Political reform →"_s}, {u"country_event"_s, u"Fires event"_s},
    {u"province_event"_s, u"Fires event"_s}, {u"any_country"_s, u"Every nation"_s}, {u"random_country"_s, u"One nation"_s},
    {u"any_owned"_s, u"Every owned province"_s}, {u"random_owned"_s, u"One owned province"_s},
    {u"any_pop"_s, u"Every pop"_s}, {u"random_pop"_s, u"One pop"_s}, {u"any_state"_s, u"Every state"_s},
    {u"random_state"_s, u"One state"_s}, {u"capital_scope"_s, u"The capital"_s}, {u"owner"_s, u"The owner"_s},
    {u"controller"_s, u"The controller"_s}, {u"limit"_s, u"Only if"_s}, {u"set_variable"_s, u"Sets the variable"_s},
    {u"change_variable"_s, u"Changes the variable"_s}, {u"upper_house"_s, u"Upper house"_s},
    {u"dominant_issue"_s, u"Issue support"_s}, {u"ideology"_s, u"Ideology support"_s},
    {u"diplomatic_influence"_s, u"Influence"_s}, {u"all_core"_s, u"Every core province"_s},
    {u"any_core"_s, u"Every core province"_s}, {u"random"_s, u"With a chance"_s}, {u"random_list"_s, u"One of"_s},
};

static QString drop_colour_codes(const QString &s)  // re.sub("§.", "", s)
{
    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'§' && i + 1 < s.size() && s[i + 1] != u'\n') {
            ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

QString pretty_value(const moddata::Mod &mod, const QString &key, const QString &v)
{
    // A value readable: a nation's name for its tag, a province's name for its id, a good's name, localisation.
    if (is_tag(v) && mod.countries.contains(v)) {
        // (a tag that only marks something — its "name" a whole tooltip with colour codes — stays a tag)
        const QString name = py::strip(drop_colour_codes(mod.countries.get(v)->name).split(u"\\n"_s)[0]);
        return (!name.isEmpty() && name.size() < 60) ? u"%1 (%2)"_s.arg(name, v) : v;
    }
    if (key == u"secede_province" || key == u"capital" || key == u"owns" || key == u"province_id" ||
        (py::isdigit(v) && py::isdigit(key))) {
        if (py::isdigit(v))
            if (const moddata::Province *pr = mod.provinces.get(py::to_int(v)))
                return u"%1 (#%2)"_s.arg(pr->name).arg(py::to_int(v));
    }
    if (key == u"trade_goods" && mod.goods.contains(v))
        return mod.goods.get(v)->name;
    const QString *loc = mod.loc.get(v);
    return (loc && !loc->isEmpty() && loc->size() < 60) ? *loc : v;
}

static QString loc_or(const moddata::Mod &mod, const OptStr &k)   // mod.loc.get(k) or k
{
    if (k)
        if (const QString *l = mod.loc.get(*k); l && !l->isEmpty())
            return *l;
    return fmt_s(k);
}

void effect_lines(const EventDB &db, View nodes, long long depth, std::vector<Line> &out)
{
    // [(depth, text, link event id or None)] — effects (or conditions) as indented readable lines.
    const moddata::Mod &mod = db.mod;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (!c.has_key) {
            out.push_back({depth, c.is_block ? u"{…}"_s : c.value, std::nullopt});
            continue;
        }
        const QString &k = c.key;
        const QString label = NICE.value(k, QString(k).replace(u'_', u' '));
        if (k == u"country_event" || k == u"province_event") {
            const OptStr id = !c.is_block ? OptStr(c.value) : _val(View(c.kids), u"id"_s);
            const OptStr days = c.is_block ? _val(View(c.kids), u"days"_s) : std::nullopt;
            Entry *const *tgt = db.by_id.get(py::strip(id.value_or(QString())));
            const QString nm = tgt ? db.name(**tgt) : QString();
            out.push_back({depth,
                           u"Fires event #%1 %2%3"_s.arg(fmt_s(id), nm.isEmpty() ? QString() : u"“%1”"_s.arg(nm),
                                                         (days && !days->isEmpty()) ? u" after %1 days"_s.arg(*days) : QString()),
                           py::strip(id.value_or(QString()))});
            continue;
        }
        if (!c.is_block) {
            const QString &v = c.value;
            if (k == u"remove_country_modifier" || k == u"remove_province_modifier") {
                out.push_back({depth, u"Removes the modifier “%1”"_s.arg(loc_or(mod, py::strip(v))),
                               u"mod:"_s + py::strip(v)});
                continue;
            }
            const auto n = _num(v);
            static const QSet<QString> PLAIN = {u"secede_province"_s, u"capital"_s, u"owns"_s, u"add_core"_s,
                                                u"remove_core"_s, u"fort"_s, u"naval_base"_s, u"railroad"_s};
            if (NICE.contains(k) && n && !PLAIN.contains(k))
                out.push_back({depth, u"%1 %2%3"_s.arg(label, *n > 0 ? u"+"_s : QString(), v), std::nullopt});
            else
                out.push_back({depth, label + u' ' + pretty_value(mod, k, v), std::nullopt});
            continue;
        }
        const View kv(c.kids);
        if (k == u"add_country_modifier" || k == u"add_province_modifier") {
            const OptStr nm = _val(kv, u"name"_s), dur = _val(kv, u"duration"_s);
            out.push_back({depth,
                           u"%1 “%2”%3"_s.arg(label, loc_or(mod, nm),
                                              (dur && !dur->isEmpty() && *dur != u"-1") ? u" for %1 days"_s.arg(*dur)
                                                                                         : u" (for good)"_s),
                           (nm && !nm->isEmpty()) ? OptStr(u"mod:"_s + py::strip(*nm)) : std::nullopt});
            continue;
        }
        if (k == u"set_variable" || k == u"change_variable" || k == u"check_variable") {
            out.push_back({depth, u"%1 %2 %3"_s.arg(label, fmt_s(_val(kv, u"which"_s)), fmt_s(_val(kv, u"value"_s))),
                           std::nullopt});
            continue;
        }
        if (k == u"dominant_issue" || k == u"ideology" || k == u"upper_house") {
            auto first = [](const OptStr &a, const OptStr &b) { return (a && !a->isEmpty()) ? a : b; };
            const OptStr x = first(_val(kv, u"value"_s), _val(kv, u"ideology"_s));
            const OptStr y = first(_val(kv, u"factor"_s), _val(kv, u"value"_s));
            out.push_back({depth, u"%1: %2 %3"_s.arg(label, fmt_s(x), (y && !y->isEmpty()) ? *y : QString()),
                           std::nullopt});
            continue;
        }
        QString head = label;
        if (is_tag(k) && mod.countries.contains(k)) {
            head = u"For "_s + pretty_value(mod, k, k);
        } else if (py::isdigit(k) && mod.provinces.contains(py::to_int(k))) {
            out.push_back({depth, u"In %1:"_s.arg(pretty_value(mod, u"province_id"_s, k)), u"prov:"_s + k});
            effect_lines(db, kv, depth + 1, out);
            continue;
        } else if (mod.regions.contains(k)) {
            head = u"In the state "_s + k;
        }
        out.push_back({depth, head + u':', std::nullopt});
        effect_lines(db, kv, depth + 1, out);
    }
}

std::vector<Line> effect_lines(const EventDB &db, View nodes)
{
    std::vector<Line> out;
    effect_lines(db, nodes, 0, out);
    return out;
}

void cond_lines(const EventDB &db, Judge *judge, View nodes, const std::optional<Scope> &scope,
                std::optional<long long> year, long long depth, std::vector<CondLine> &out)
{
    // [(depth, text, state)] — conditions readable, each with what it is for `scope` (True / False / None; "" when
    // no scope is given).
    const moddata::Mod &mod = db.mod;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &c = nodes[i];
        if (!c.has_key)
            continue;
        const QString &k = c.key;
        const bool hs = scope.has_value();
        const Tri st = hs ? judge->one(c, *scope, *scope, year) : Tri();
        const QString ku = py::upper(k);
        if (c.is_block) {
            QString head;
            const View kv(c.kids);
            if (ku == u"OR")
                head = u"Any of:"_s;
            else if (ku == u"NOT")
                head = u"None of:"_s;
            else if (ku == u"AND")
                head = u"All of:"_s;
            else if (is_tag(k) && mod.countries.contains(k))
                head = pretty_value(mod, k, k) + u':';
            else if (py::isdigit(k) && mod.provinces.contains(py::to_int(k)))
                head = pretty_value(mod, u"province_id"_s, k) + u':';
            else if (k == u"check_variable" || k == u"upper_house") {
                const OptStr which = _val(kv, u"which"_s);
                out.push_back({depth,
                               u"%1 %2 ≥ %3"_s.arg(QString(k).replace(u'_', u' '),
                                                   fmt_s((which && !which->isEmpty()) ? which : _val(kv, u"ideology"_s)),
                                                   fmt_s(_val(kv, u"value"_s))),
                               hs, st});
                continue;
            } else
                head = NICE.value(k, QString(k).replace(u'_', u' ')) + u':';
            out.push_back({depth, head, hs, st});
            // inside another scope the states are that scope's: shown without them
            const std::optional<Scope> inner = BLOCKS.contains(ku) ? scope : std::nullopt;
            cond_lines(db, judge, kv, inner, year, depth + 1, out);
            continue;
        }
        const QString &v = c.value;
        const auto n = _num(v);
        QString txt = QString(k).replace(u'_', u' ');
        static const QSet<QString> IDS = {u"province_id"_s, u"capital"_s, u"owns"_s, u"owned_by"_s};
        static const QSet<QString> AMOUNTS = {u"num_of_cities"_s, u"literacy"_s, u"plurality"_s, u"prestige"_s,
                                              u"average_militancy"_s, u"average_consciousness"_s, u"life_rating"_s,
                                              u"rank"_s};
        if (k == u"year")
            txt = u"the year is %1 or later"_s.arg(v);
        else if (k == u"rank")
            txt = u"rank %1 or better"_s.arg(v);
        else if (k == u"has_country_flag" || k == u"has_global_flag" || k == u"has_province_flag")
            txt = u"%1 “%2”"_s.arg(txt, v);
        else if (k == u"has_country_modifier" || k == u"has_province_modifier")
            txt = u"%1 “%2”"_s.arg(txt, py::strip(v));
        else if ((n && !IDS.contains(k) && !py::isdigit(v)) || (n && AMOUNTS.contains(k)))
            txt = u"%1 ≥ %2"_s.arg(txt, v);
        else
            txt = u"%1 = %2"_s.arg(txt, pretty_value(mod, k, v));
        out.push_back({depth, txt, hs, st});
    }
}

std::vector<CondLine> cond_lines(const EventDB &db, Judge *judge, View nodes, const std::optional<Scope> &scope,
                                 std::optional<long long> year)
{
    std::vector<CondLine> out;
    cond_lines(db, judge, nodes, scope, year, 0, out);
    return out;
}

QString fmt_days(const std::optional<double> &d)
{
    if (!d)
        return u"—"_s;
    if (*d >= 730)
        return py::fmt_f(*d / 365.0, 1) + u" years"_s;
    if (*d >= 60)
        return py::fmt_f(*d / 30.0, 0) + u" months"_s;
    return py::fmt_f(*d, 0) + u" days"_s;
}

}  // namespace events
