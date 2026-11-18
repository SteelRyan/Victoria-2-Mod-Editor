// culturefile.py + religionfile.py — common/cultures.txt and common/religion.txt edited by text splices.
#include "core/culturefile.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QDateTime>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace culturefile {

const QRegularExpression KEY_RE(u"^[a-z][a-z0-9_]*$"_s);

using pypath::join;
using script::Node;

const QString LOC_FILE = u"zz_modeditor_cultures.csv"_s;
const QString OLD_LOC_FILE = u"zz_popeditor_cultures.csv"_s;   // name used by the old PopEditor build

static QString stamp_now() { return QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s); }
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

QString _nl(const QString &text) { return (text.contains(u"\r\n"_s) || text.isEmpty()) ? u"\r\n"_s : u"\n"_s; }

std::vector<const Node *> _groups(const script::Nodes &parsed)
{
    std::vector<const Node *> out;
    for (const Node &g : parsed)
        if (g.has_key && !g.key.isEmpty() && g.is_block)
            out.push_back(&g);
    return out;
}

static std::pair<const Node *, const Node *> _find(const script::Nodes &parsed, const QString &cid)
{
    for (const Node *g : _groups(parsed))
        for (const Node &c : g->kids)
            if (c.has_key && c.key == cid && c.is_block)
                return {g, &c};
    return {nullptr, nullptr};
}

std::pair<qsizetype, qsizetype> _line_span(const QString &text, qsizetype s, qsizetype e)
{
    // Whole lines covering [s, e) when the span sits on its own lines (trailing comments included).
    const qsizetype ls = rfind_nl(text, s) + 1;
    if (!blank(slice(text, ls, s)))
        return {s, e};
    const qsizetype j = text.indexOf(u'\n', e);
    const QString rest = py::strip(slice(text, e, j >= 0 ? j : text.size()));
    if (!rest.isEmpty() && !rest.startsWith(u'#'))
        return {ls, e};
    return {ls, j >= 0 ? j + 1 : text.size()};
}

QString _insert_in_group(const QString &text, const QString &gid, QString block)
{
    const script::Nodes parsed = script::parse(text);
    const Node *g = nullptr;
    for (const Node *x : _groups(parsed))
        if (x->key == gid) {
            g = x;
            break;
        }
    if (!g)
        throw py::ValueError(("culture group " + gid + " not found").toStdString());
    const QString nl = _nl(text);
    if (!block.endsWith(u'\n'))
        block += nl;
    const qsizetype close = g->vend - 1;
    const qsizetype ls = rfind_nl(text, close) + 1;
    if (blank(slice(text, ls, close)) && ls > g->kstart)
        return text.left(ls) + block + text.mid(ls);
    return text.left(close) + nl + block + text.mid(close);
}

QString _sub_first(const QString &pattern, const QString &text, const std::function<QString(const QStringList &)> &repl)
{
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end())
        it = cache.insert(pattern, QRegularExpression(pattern, QRegularExpression::UseUnicodePropertiesOption));
    const QRegularExpressionMatch m = it->match(text);
    if (!m.hasMatch())
        return text;
    return text.left(m.capturedStart()) + repl(m.capturedTexts()) + text.mid(m.capturedEnd());
}

QString _strip_first_comment(const QString &b)
{
    // first, sep, rest = b.partition("\n"); first = re.sub(r"\s*#.*$", "", first.rstrip("\r")) + "\r"-if-it-had-one
    const qsizetype k = b.indexOf(u'\n');
    QString first = k >= 0 ? b.left(k) : b;
    const QString rest = k >= 0 ? b.mid(k) : QString();       // (sep + rest)
    const bool cr = first.endsWith(u'\r');
    while (first.endsWith(u'\r'))
        first.chop(1);
    const qsizetype h = first.indexOf(u'#');
    if (h >= 0) {
        qsizetype s = h;                                       // \s* before the # goes too
        while (s > 0 && py::is_py_space(first[s - 1].unicode()))
            --s;
        first = first.left(s);
    }
    return first + (cr ? u"\r"_s : QString()) + rest;
}

QString _culture_block(const QString &key, const Rgb &color, const OptStr &tb, const OptStr &tk, const QString &nl)
{
    const auto [r, g, b] = color;
    if (tb && !tb->isEmpty()) {
        QString bl = _sub_first(u"^(\\s*)"_s + QRegularExpression::escape(*tk) + u"(\\s*=)"_s, *tb,
                                [&](const QStringList &m) { return m[1] + key + m[2]; });
        bl = _sub_first(u"color\\s*=\\s*\\{[^}]*\\}"_s, bl,
                        [&](const QStringList &) { return u"color = {%1 %2 %3}"_s.arg(r).arg(g).arg(b); });
        // drop a trailing comment on the first line (it described the template culture)
        return _strip_first_comment(bl);
    }
    return QStringList{u"\t%1 = {"_s.arg(key), u"\t\tcolor = {%1 %2 %3}"_s.arg(r).arg(g).arg(b),
                       u"\t\tfirst_names = { John }"_s, u"\t\tlast_names = { Smith }"_s, u"\t}"_s, QString()}
        .join(nl);
}

QString apply_changes(moddata::Mod &mod, const std::vector<std::pair<QString, OptStr>> &new_groups,
                      const std::vector<NewCulture> &new_cultures, const std::vector<std::pair<QString, QString>> &moves,
                      const QString &backup_root)
{
    // Write culture changes. Returns the backup folder.
    QString path = mod.p({u"common"_s, u"cultures.txt"_s});
    moddata::PendingScript *sink = mod.pending_script;   // the editor: the change waits for Apply
    if (!sink && !mod.in_mod(path))
        path = mod.adopt(path);                            // (the base game's file: copied into the mod first)
    QString text = sink ? sink->text(u"common/cultures.txt"_s) : script::read_text(path);
    const QString orig = text;
    const QString nl = _nl(text);

    for (const auto &[gid, tmpl] : new_groups) {
        QStringList lines{u"%1 = {"_s.arg(gid)};
        const script::Nodes parsed = script::parse(text);
        const auto groups = _groups(parsed);
        const Node *tg = nullptr;
        if (tmpl && !tmpl->isEmpty())
            for (const Node *x : groups)
                if (x->key == *tmpl) {
                    tg = x;
                    break;
                }
        if (!tg && !groups.empty())
            tg = groups[0];
        if (tg) {
            for (const Node &n : tg->kids)
                if (n.has_key && !n.key.isEmpty() && !n.is_block && n.key != u"union")
                    lines << u"\t%1 = %2"_s.arg(n.key, n.value);
        } else {
            lines << u"\tleader = european"_s << u"\tunit = EuropeanGC"_s;
        }
        lines << u"}"_s;
        if (!text.endsWith(u'\n'))
            text += nl;
        text += nl + lines.join(nl) + nl;
    }

    for (const NewCulture &nc : new_cultures) {
        OptStr tb, tk;
        // template: the requested culture, else the first culture of the target group, else any
        QStringList cand;
        if (nc.tmpl && !nc.tmpl->isEmpty())
            cand << *nc.tmpl;
        {
            const script::Nodes parsed = script::parse(text);
            for (const Node *x : _groups(parsed))
                if (x->key == nc.group) {
                    for (const Node &c : x->kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block)
                            cand << c.key;
                    break;
                }
            for (const Node *g2 : _groups(parsed))
                for (const Node &c : g2->kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        cand << c.key;
            for (const QString &k : cand) {
                const auto [_g, c] = _find(parsed, k);
                if (c) {
                    const auto [s, e] = _line_span(text, c->kstart, c->vend);
                    tb = slice(text, s, e);
                    tk = k;
                    break;
                }
            }
        }
        text = _insert_in_group(text, nc.group, _culture_block(nc.key, nc.color, tb, tk, nl));
    }

    for (const auto &[cid, gid] : moves) {
        const script::Nodes parsed = script::parse(text);
        const auto [g, c] = _find(parsed, cid);
        if (!c || g->key == gid)
            continue;
        const auto [s, e] = _line_span(text, c->kstart, c->vend);
        const QString block = slice(text, s, e);
        text = text.left(s) + text.mid(e);
        text = _insert_in_group(text, gid, block);
    }

    // sanity check before writing: the result must still parse into the same groups + new ones
    QStringList after;
    {
        const script::Nodes parsed = script::parse(text);
        for (const Node *g : _groups(parsed))
            after << g->key;
    }
    QStringList want;
    {
        const script::Nodes parsed = script::parse(orig);
        for (const Node *g : _groups(parsed))
            want << g->key;
    }
    for (const auto &kv : new_groups)
        want << kv.first;
    for (const QString &gid : want)
        if (!after.contains(gid))
            throw std::runtime_error(("Internal error: group " + gid +
                                      " lost while editing cultures.txt — nothing was written.").toStdString());
    std::vector<std::pair<QString, QString>> names;
    for (const NewCulture &nc : new_cultures)
        if (nc.name && !nc.name->isEmpty())
            names.emplace_back(nc.key, *nc.name);
    for (const auto &kv : new_groups)
        names.emplace_back(kv.first, kv.first);
    if (sink) {
        QStringList what;
        for (const NewCulture &nc : new_cultures)
            what << nc.key;
        for (const auto &kv : new_groups)
            what << kv.first;
        QStringList mv;
        for (const auto &kv : moves)
            mv << kv.first;
        what += mv.mid(0, 3);
        sink->put(u"Cultures: %1"_s.arg(what.isEmpty() ? u"changed"_s : what.join(u", "_s)), u"common/cultures.txt"_s,
                  text, names, u"culture"_s);
        return u"written on Apply"_s;
    }
    const QString bdir = join(backup_root, {u"common"_s, stamp_now()});
    fs::makedirs(bdir);
    fs::copy2(path, join(bdir, u"cultures.txt"_s));
    script::write_text(path, text);
    if (!names.empty())
        _write_localisation(mod, names, bdir);
    return bdir;
}

void _write_localisation(moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &entries, const QString &bdir,
                         const QString &filename, const OptStr &)
{
    // Names of new cultures / religions (and their groups): keys the mod already defines stay where they are (only
    // new ones are written) — new ones go to the editor's own localisation file, in their section.
    const QString section = filename.contains(u"religion"_s) ? u"religion"_s : u"culture"_s;
    QStringList keys;
    for (const auto &kv : entries)
        keys << kv.first;
    const auto have = localisation::defined_where(mod, keys);
    OrderedMap<QString, QString> names, secs;
    for (const auto &[k, n] : entries)
        if (!n.isEmpty() && !have.contains(k))
            names.insert(k, n);
    for (const auto &kv : names)
        secs.insert(kv.first, section);
    localisation::write_names(mod, names, bdir, secs);
}

}  // namespace culturefile

// ======================================================================== religionfile.py
namespace religionfile {

using culturefile::_groups;
using culturefile::_insert_in_group;
using culturefile::_line_span;
using culturefile::_nl;
using culturefile::_sub_first;
using pypath::join;
using script::Node;

const QString LOC_FILE = u"zz_modeditor_religions.csv"_s;

QString _color(const Rgb &rgb)
{
    QStringList parts;
    for (int c : {std::get<0>(rgb), std::get<1>(rgb), std::get<2>(rgb)}) {
        QString s = py::fmt_f(c / 255.0, 3);
        while (s.endsWith(u'0'))
            s.chop(1);
        while (s.endsWith(u'.'))
            s.chop(1);
        parts << (s.isEmpty() ? u"0"_s : s);
    }
    return u"{ %1 }"_s.arg(parts.join(u' '));
}

QString _religion_block(const QString &key, const Rgb &rgb, long long icon, const OptStr &tb, const OptStr &tk,
                        const QString &nl)
{
    if (tb && !tb->isEmpty()) {
        QString b = _sub_first(u"^(\\s*)"_s + QRegularExpression::escape(*tk) + u"(\\s*=)"_s, *tb,
                               [&](const QStringList &m) { return m[1] + key + m[2]; });
        b = _sub_first(u"color\\s*=\\s*\\{[^}]*\\}"_s, b, [&](const QStringList &) { return u"color = "_s + _color(rgb); });
        static const QRegularExpression ICON(u"\\bicon\\s*=\\s*\\d+"_s, QRegularExpression::UseUnicodePropertiesOption);
        if (ICON.match(b).hasMatch())
            b = _sub_first(u"\\bicon\\s*=\\s*\\d+"_s, b, [&](const QStringList &) { return u"icon = %1"_s.arg(icon); });
        else
            b = _sub_first(u"(=\\s*\\{)"_s, b, [&](const QStringList &m) { return m[1] + nl + u"\t\ticon = %1"_s.arg(icon); });
        return culturefile::_strip_first_comment(b);
    }
    return QStringList{u"\t%1 = {"_s.arg(key), u"\t\ticon = %1"_s.arg(icon), u"\t\tcolor = %1"_s.arg(_color(rgb)),
                       u"\t}"_s, QString()}
        .join(nl);
}

static std::pair<const Node *, const Node *> _find(const script::Nodes &parsed, const QString &rid)
{
    for (const Node *g : _groups(parsed))
        for (const Node &r : g->kids)
            if (r.has_key && r.key == rid && r.is_block)
                return {g, &r};
    return {nullptr, nullptr};
}

QString apply_changes(moddata::Mod &mod, const std::vector<std::pair<QString, OptStr>> &new_groups,
                      const std::vector<NewReligion> &new_religions,
                      const std::vector<std::pair<QString, QString>> &moves, const QString &backup_root)
{
    QString path = mod.p({u"common"_s, u"religion.txt"_s});
    moddata::PendingScript *sink = mod.pending_script;
    if (!sink && !mod.in_mod(path))
        path = mod.adopt(path);
    QString text = sink ? sink->text(u"common/religion.txt"_s) : script::read_text(path);
    const QString orig = text;
    const QString nl = _nl(text);
    for (const auto &kv : new_groups) {
        if (!text.endsWith(u'\n'))
            text += nl;
        text += nl + u"%1 = {%2}"_s.arg(kv.first, nl) + nl;
    }
    for (const NewReligion &nr : new_religions) {
        OptStr tb, tk;
        const script::Nodes parsed = script::parse(text);
        QStringList cand;
        for (const Node *g : _groups(parsed))
            if (g->key == nr.group) {
                for (const Node &c : g->kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        cand << c.key;
                break;
            }
        for (const Node *g2 : _groups(parsed))
            for (const Node &c : g2->kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    cand << c.key;
        for (const QString &k : cand) {
            const auto [_g, r] = _find(parsed, k);
            if (r) {
                const auto [s, e] = _line_span(text, r->kstart, r->vend);
                tb = text.mid(s, e - s);
                tk = k;
                break;
            }
        }
        text = _insert_in_group(text, nr.group, _religion_block(nr.key, nr.color, (nr.icon && *nr.icon) ? *nr.icon : 1, tb, tk, nl));
    }
    for (const auto &[rid, gid] : moves) {
        const script::Nodes parsed = script::parse(text);
        const auto [g, r] = _find(parsed, rid);
        if (!r || g->key == gid)
            continue;
        const auto [s, e] = _line_span(text, r->kstart, r->vend);
        const QString block = text.mid(s, e - s);
        text = text.left(s) + text.mid(e);
        text = _insert_in_group(text, gid, block);
    }
    OrderedMap<QString, QStringList> after;
    {
        const script::Nodes parsed = script::parse(text);
        for (const Node *g : _groups(parsed)) {
            QStringList ks;
            for (const Node &c : g->kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    ks << c.key;
            after.insert(g->key, ks);
        }
    }
    QStringList want;
    {
        const script::Nodes parsed = script::parse(orig);
        for (const Node *g : _groups(parsed))
            want << g->key;
    }
    for (const auto &kv : new_groups)
        want << kv.first;
    for (const QString &gid : want)
        if (!after.contains(gid))
            throw std::runtime_error(("Internal error: group " + gid +
                                      " lost while editing religion.txt — nothing was written.").toStdString());
    for (const NewReligion &nr : new_religions)
        if (!after.value(nr.group).contains(nr.key))
            throw std::runtime_error(("Internal error: religion " + nr.key + " not written — nothing was written.").toStdString());
    std::vector<std::pair<QString, QString>> names;
    for (const NewReligion &nr : new_religions)
        if (nr.name && !nr.name->isEmpty())
            names.emplace_back(nr.key, *nr.name);
    for (const auto &kv : new_groups) {
        QString t = kv.first;
        t.replace(u'_', u' ');
        names.emplace_back(kv.first, py::title(t));
    }
    if (sink) {
        QStringList what;
        for (const NewReligion &nr : new_religions)
            what << nr.key;
        for (const auto &kv : new_groups)
            what << kv.first;
        QStringList mv;
        for (const auto &kv : moves)
            mv << kv.first;
        what += mv.mid(0, 3);
        sink->put(u"Religions: %1"_s.arg(what.isEmpty() ? u"changed"_s : what.join(u", "_s)), u"common/religion.txt"_s,
                  text, names, u"religion"_s);
        return u"written on Apply"_s;
    }
    const QString bdir = join(backup_root, {u"common"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    fs::makedirs(bdir);
    fs::copy2(path, join(bdir, u"religion.txt"_s));
    script::write_text(path, text);
    if (!names.empty())
        culturefile::_write_localisation(mod, names, bdir, LOC_FILE, std::nullopt);
    return bdir;
}

}  // namespace religionfile
