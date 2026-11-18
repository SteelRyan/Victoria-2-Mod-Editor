// blocks.py — careful edits of a script's text by a path of keys. Line by line.
#include "core/blocks.h"
#include "core/pynum.h"
#include "core/pytext.h"

#include <QSet>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace blocks {

static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }

const Node *find(const Nodes &nodes0, const QStringList &path)
{
    // The block node at a path of keys (the first of each name), or None.
    const Nodes *nodes = &nodes0;
    const Node *node = nullptr;
    for (const QString &key : path) {
        node = nullptr;
        for (const Node &n : *nodes)
            if (is(n, key) && n.is_block) {
                node = &n;
                break;
            }
        if (!node)
            return nullptr;
        nodes = &node->kids;
    }
    return node;
}

std::vector<Node> children(const QString &text, const QStringList &path)
{
    // [(key, value, is_block)] inside the block at the path.
    const Nodes nodes = script::parse(text);
    const Node *node = find(nodes, path);
    std::vector<Node> out;
    if (node)
        for (const Node &c : node->kids)
            if (c.has_key && !c.key.isEmpty())
                out.push_back(c);
    return out;
}

static QString leading_ws(const QString &t, qsizetype from)   // re.match(r"[ \t]*", t[from:]).group(0)
{
    qsizetype i = from;
    while (i < t.size() && (t[i] == u' ' || t[i] == u'\t'))
        ++i;
    return t.mid(from, i - from);
}

QString _indent_inside(const QString &text, const Node &node)
{
    // The indentation of the block's own lines (its first child's, else one tab more than the block's line).
    for (const Node &c : node.kids) {
        const qsizetype ls = py::rfind(text, u'\n', c.kstart) + 1;
        if (py::strip(py::slice(text, ls, c.kstart)).isEmpty())
            return py::slice(text, ls, c.kstart);
    }
    const qsizetype ls = py::rfind(text, u'\n', node.kstart) + 1;
    return leading_ws(text, ls) + u'\t';
}

QString _insert_before_close(const QString &text, const Node &node, const QString &line)
{
    const qsizetype close = node.vend - 1;       // the block's "}"
    const qsizetype ls = py::rfind(text, u'\n', close) + 1;
    const QString ind = _indent_inside(text, node);
    if (!py::strip(py::slice(text, ls, close)).isEmpty())   // "… }" on one line: the new line before the brace
        return py::head(text, close) + u'\n' + ind + line + u'\n' + py::tail(text, close);
    return py::head(text, ls) + ind + line + u'\n' + py::tail(text, ls);
}

static std::optional<std::pair<qsizetype, QString>> _like_place(const QString &text, const Nodes &kids, const OptStr &like,
                                                                const QStringList &path, const QString &key)
{
    // Where a new line `key = …` belongs so the block reads as it does in `like` (the file on disk): the start of the
    // line of the first child that comes after `key` there — or None.
    if (!like)
        return std::nullopt;
    const Nodes lnodes = script::parse(*like);
    std::vector<OptStr> order;
    if (path.isEmpty()) {
        for (const Node &c : lnodes)
            order.push_back(c.has_key ? OptStr(c.key) : std::nullopt);
    } else if (const Node *node = find(lnodes, path)) {
        for (const Node &c : node->kids)
            order.push_back(c.has_key ? OptStr(c.key) : std::nullopt);
    }
    const auto it = std::find(order.begin(), order.end(), OptStr(key));
    if (it == order.end())
        return std::nullopt;
    QSet<OptStr> after;
    for (auto j = it + 1; j != order.end(); ++j)
        after.insert(*j);
    for (const Node &c : kids) {
        if (after.contains(c.has_key ? OptStr(c.key) : std::nullopt)) {
            const qsizetype ls = py::rfind(text, u'\n', c.kstart) + 1;
            if (py::strip(py::slice(text, ls, c.kstart)).isEmpty())
                return std::make_pair(ls, py::slice(text, ls, c.kstart));
            return std::nullopt;
        }
    }
    return std::nullopt;
}

static QString rstrip_nl(QString s)
{
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

QString set_value(const QString &text, const QStringList &path, const QString &key, const QString &value, const OptStr &like)
{
    // key = value inside the block at the path (changed where it is, or a new line before the closing brace — with
    // `like`, the file's text on disk, at the place it had there: taking a value off and putting it back gives the
    // same file again).
    const Nodes nodes = script::parse(text);
    if (path.isEmpty()) {                         // the file's own top level
        for (const Node &c : nodes)
            if (is(c, key) && !c.is_block)
                return py::head(text, c.vstart) + value + py::tail(text, c.vend);
        if (const auto at = _like_place(text, nodes, like, path, key))
            return py::head(text, at->first) + at->second + key + u" = "_s + value + u'\n' + py::tail(text, at->first);
        return rstrip_nl(text) + u'\n' + key + u" = "_s + value + u'\n';
    }
    const Node *node = find(nodes, path);
    if (!node)
        return text;
    for (const Node &c : node->kids)
        if (is(c, key) && !c.is_block)
            return py::head(text, c.vstart) + value + py::tail(text, c.vend);
    if (const auto at = _like_place(text, node->kids, like, path, key))
        return py::head(text, at->first) + at->second + key + u" = "_s + value + u'\n' + py::tail(text, at->first);
    return _insert_before_close(text, *node, key + u" = "_s + value);
}

QString del_value(const QString &text, const QStringList &path, const QString &key)
{
    // key = … taken out of the block at the path (the whole line when it stands alone).
    const Nodes nodes = script::parse(text);
    const Nodes *kids = &nodes;                   // the file's own top level
    if (!path.isEmpty()) {
        const Node *node = find(nodes, path);
        if (!node)
            return text;
        kids = &node->kids;
    }
    for (const Node &c : *kids) {
        if (is(c, key)) {
            const qsizetype ls = py::rfind(text, u'\n', c.kstart) + 1;
            qsizetype le = text.indexOf(u'\n', c.vend);
            le = le < 0 ? text.size() : le + 1;
            const QString rest = py::strip(py::slice(text, c.vend, le));
            if (py::strip(py::slice(text, ls, c.kstart)).isEmpty() && (rest.isEmpty() || rest.startsWith(u'#')))
                return py::head(text, ls) + py::tail(text, le);
            return py::head(text, c.kstart) + py::tail(text, c.vend);
        }
    }
    return text;
}

OptStr block_text(const QString &text, const QStringList &path)
{
    // The inside of the block at the path (between its braces), or None; an empty path: the whole file.
    if (path.isEmpty())
        return text;
    const Nodes nodes = script::parse(text);
    const Node *node = find(nodes, path);
    if (!node)
        return std::nullopt;
    return py::slice(text, node->vstart + 1, node->vend - 1);
}

std::vector<Node> children_top(const QString &text, const QStringList &path)
{
    // Like children(), and with an empty path the file's own top level.
    if (!path.isEmpty())
        return children(text, path);
    std::vector<Node> out;
    for (const Node &c : script::parse(text))
        if (c.has_key && !c.key.isEmpty())
            out.push_back(c);
    return out;
}

QString add_block(const QString &text, const QStringList &path, const QString &key, const QStringList &lines)
{
    // A new block key = { lines } at the end of the block at the path (empty path: at the end of the file).
    if (!path.isEmpty()) {
        const Nodes nodes = script::parse(text);
        const Node *node = find(nodes, path);
        if (!node)
            return text;
        const QString ind = _indent_inside(text, *node);
        QString body;
        for (const QString &ln : lines)
            body += u'\n' + ind + u'\t' + ln;
        return _insert_before_close(text, *node, key + u" = {"_s + body + u'\n' + ind + u'}');
    }
    QString body;
    for (const QString &ln : lines)
        body += u"\n\t"_s + ln;
    return rstrip_nl(text) + u"\n\n"_s + key + u" = {"_s + body + u"\n}\n"_s;
}

QString set_block(const QString &text, const QStringList &path, const QString &inner)
{
    // The inside of the block at the path replaced (the braces stay); a missing last block is added to its parent (or,
    // with a path of one key, to the end of the file). An empty path: the whole file's text.
    if (path.isEmpty())
        return inner;
    const Nodes nodes = script::parse(text);
    if (const Node *node = find(nodes, path))
        return py::head(text, node->vstart + 1) + inner + py::tail(text, node->vend - 1);
    if (path.size() == 1)
        return rstrip_nl(text) + u"\n\n"_s + path[0] + u" = {"_s + inner + u"}\n"_s;
    const Node *parent = find(nodes, path.mid(0, path.size() - 1));
    if (!parent)
        return text;
    return _insert_before_close(text, *parent, path.last() + u" = {"_s + inner + u'}');
}

}  // namespace blocks
