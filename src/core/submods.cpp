// submods.py — submods and their working copies. Line by line.
#include "core/submods.h"
#include "core/fs.h"
#include "core/modcopy.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace submods {

const QString WORK_FOLDER = u"working copies"_s;          // next to the editor
// folders of which the game reads every file, each a list of things of its own: there an empty file of the same
// name is how a submod takes a parent's file away
const QStringList EMPTY_TAKES_AWAY = {u"history/"_s, u"events/"_s, u"decisions/"_s, u"localisation/"_s, u"units/"_s,
                                      u"inventions/"_s};
const QStringList NEVER_EMPTY = {u"history/wars/"_s};      // the game crashes on an empty war file
// The Python editor's rule of 2026-10-08 (measured from outside, too wide: all of history/). C++ no longer uses
// them: counts() below has the game's exact rule since 2026-11-09.
const QStringList CHECKSUM_FOLDERS = {u"common/"_s, u"history/"_s,     u"events/"_s,      u"decisions/"_s,    u"news/"_s,
                                      u"battleplans/"_s, u"units/"_s, u"inventions/"_s, u"technologies/"_s, u"poptypes/"_s};
const QStringList CHECKSUM_FILES = {u"map/default.map"_s};

static bool starts_any(const QString &s, const QStringList &prefixes)
{
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const QString &p) { return s.startsWith(p); });
}
static QString slashes(QString s) { return s.replace(u'\\', u'/'); }
static QString below(const QString &root, const QString &rel) { return pypath::join(root, rel.split(u'/')); }

bool counts(const QString &rel)
{
    // Does a file with this path below the mod change the game's checksum?
    // C++ only (2026-11-09): the game's exact rule, checked against the game's own numbers (map/checksum.h,
    // tests/map/gamesum_check.cpp). The Python editor's CHECKSUM_FOLDERS guess counted all of history/ (pops too:
    // they do not count) and missed script/ and the map files of the map cache's key.
    const QString r = slashes(rel), l = py::lower(r);
    if (!std::all_of(r.begin(), r.end(), [](QChar c) { return c.unicode() < 128; }))
        return false;                                 // (the game leaves non-ASCII names out)
    auto in = [&](const QString &dir, const QString &ext) { return l.startsWith(dir + u'/') && r.endsWith(ext); };
    if (in(u"common"_s, u".txt"_s) || in(u"common"_s, u".lua"_s) || in(u"script"_s, u".lua"_s) ||
        in(u"battleplans"_s, u".txt"_s) || in(u"battleplans"_s, u".pln"_s))
        return true;
    for (const QString &d : {u"news"_s, u"poptypes"_s, u"events"_s, u"missions"_s, u"units"_s, u"inventions"_s,
                             u"technologies"_s, u"decisions"_s, u"history/countries"_s, u"history/diplomacy"_s,
                             u"history/provinces"_s, u"history/wars"_s, u"history/leaders"_s, u"history/units"_s})
        if (in(d, u".txt"_s))
            return true;
    // map/default.map; the files it names count through the map cache's key (the usual names)
    static const QStringList FILES = {u"map/default.map"_s,  u"map/definition.csv"_s, u"map/provinces.bmp"_s,
                                      u"map/positions.txt"_s, u"map/terrain.bmp"_s,  u"map/terrain.txt"_s,
                                      u"map/adjacencies.csv"_s, u"map/cache/time"_s, u"v2game.exe"_s};
    return FILES.contains(l);
}

std::vector<std::tuple<QString, bool, long long>> holds(const QString &root)
{
    // What a submod's folder holds: [(path below it, changes the checksum, bytes)], the counted ones first.
    std::vector<std::tuple<QString, bool, long long>> out;
    if (fs::isdir(root))
        for (const auto &[_k, e] : tree_state(root))
            out.emplace_back(std::get<0>(e), counts(std::get<0>(e)), std::get<1>(e));
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        const bool x = !std::get<1>(a), y = !std::get<1>(b);
        if (x != y)
            return x < y;
        return py::lower(std::get<0>(a)) < py::lower(std::get<0>(b));
    });
    return out;
}

std::pair<bool, QString> verdict(const std::vector<std::tuple<QString, bool, long long>> &files, bool pending)
{
    // One sentence about the checksum for a submod holding `files` (from holds()).
    long long n = 0;
    for (const auto &f : files)
        if (std::get<1>(f))
            ++n;
    if (n || pending)
        return {false, u"CHANGES THE CHECKSUM — %1: in multiplayer every player needs this submod"_s.arg(
                           n ? u"%1 file%2 the game itself"_s.arg(QString::number(n), n == 1 ? u" changes"_s : u"s change"_s)
                             : u"pending edits that change the game itself"_s)};
    return {true, u"checksum unchanged — looks only: can be played with people who do not have it"_s};
}

bool name_ok(const QString &name)
{
    return py::re_match(u"^[^\\\\/:*?\"<>|\\r\\n]{1,80}$"_s, name).hasMatch();
}

// ---------------------------------------------------------------------------------------------- .mod files
ModfileValues modfile_values(const QString &path)
{
    // What a .mod file says: name, path, user_dir (texts), replace_path and dependencies (lists).
    ModfileValues vals;
    script::Nodes nodes;
    try {
        nodes = script::parse(script::read_text(path));
    } catch (const fs::OSError &) {
        return vals;
    }
    for (const script::Node &n : nodes) {
        if (n.has_key && n.key == u"replace_path" && !n.is_block) {
            vals.replace_path << py::strip(slashes(n.value), u"/"_s);
        } else if (n.has_key && n.key == u"dependencies" && n.is_block) {
            for (const script::Node &c : n.kids)
                if (!c.has_key && !c.is_block)
                    vals.dependencies << c.value;
        } else if (n.has_key && !n.key.isEmpty() && !n.is_block) {
            if (n.key != u"replace_path" && n.key != u"dependencies" && !vals.other.contains(n.key))
                vals.other.insert(n.key, n.value);
        }
    }
    return vals;
}

QString modfile_text(const QString &name, const QString &folder, const QStringList &deps,
                     const std::optional<QString> &user_dir, const QStringList &replace)
{
    QStringList lines = {u"name = \"%1\""_s.arg(name), u"path = \"mod/%1\""_s.arg(folder)};
    if (user_dir && !user_dir->isEmpty())
        lines << u"user_dir = \"%1\""_s.arg(*user_dir);
    if (!deps.isEmpty()) {
        QStringList q;
        for (const QString &d : deps)
            q << u'"' + d + u'"';
        lines << u"dependencies = { %1 }"_s.arg(q.join(u' '));
    }
    for (const QString &r : replace)
        lines << u"replace_path = \"%1\""_s.arg(r);
    return lines.join(u"\r\n"_s) + u"\r\n"_s;
}

QByteArray cp1252_replace(const QString &text)
{
    // Python's cp1252 codec with errors="replace": one "?" for every character it has no byte for
    static const char16_t HIGH[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    QByteArray out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t u = text[i].unicode();
        if (QChar::isHighSurrogate(u) && i + 1 < text.size() && QChar::isLowSurrogate(text[i + 1].unicode())) {
            out += '?';                               // (one code point, one "?")
            ++i;
            continue;
        }
        if (u < 0x80 || (u >= 0xA0 && u <= 0xFF)) {
            out += char(u);
            continue;
        }
        char b = '?';
        for (int k = 0; k < 32; ++k)
            if (HIGH[k] && HIGH[k] == u) {
                b = char(0x80 + k);
                break;
            }
        out += b;
    }
    return out;
}

void write_modfile(const QString &path, const QString &name, const QString &folder, const QStringList &deps,
                   const std::optional<QString> &user_dir, const QStringList &replace)
{
    const QByteArray data = cp1252_replace(modfile_text(name, folder, deps, user_dir, replace));
    QFile f(fs::watched(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw fs::OSError("open: " + path.toStdString(), 13);
    f.write(data);
}

std::pair<QString, QString> create(const QString &mods_dir, const moddata::ModEntry &parent, QString name)
{
    // A new, empty submod of the mod `parent`: its folder and its .mod file in the game's mod folder.
    name = py::strip(name);
    if (!name_ok(name) || name.endsWith(u'.') || name.endsWith(u' '))
        throw py::ValueError("a submod's name is 1–80 characters without \\ / : * ? \" < > |");
    const QString folder = pypath::join(mods_dir, name);
    const QString modfile = pypath::join(mods_dir, name + u".mod"_s);
    if (fs::exists(folder) || fs::exists(modfile))
        throw py::ValueError("there is a folder or a .mod file with that name already");
    fs::makedirs(folder, false);
    write_modfile(modfile, name, name, {parent.name}, name);
    return {folder, modfile};
}

std::pair<QString, QString> create_new(const QString &mods_dir, QString name)
{
    name = py::strip(name);
    if (!name_ok(name) || name.endsWith(u'.') || name.endsWith(u' '))
        throw py::ValueError("a mod's name is 1–80 characters without \\ / : * ? \" < > |");
    const QString folder = pypath::join(mods_dir, name);
    const QString modfile = pypath::join(mods_dir, name + u".mod"_s);
    if (fs::exists(folder) || fs::exists(modfile))
        throw py::ValueError("there is a folder or a .mod file with that name already");
    fs::makedirs(pypath::join(folder, {u"history"_s, u"pops"_s}), false);
    write_modfile(modfile, name, name, {}, name);   // (its own user_dir: saves apart from the base game's)
    return {folder, modfile};
}

std::vector<moddata::ModEntry> chain(const std::vector<moddata::ModEntry> &mods, const moddata::ModEntry &entry)
{
    // The mods the game reads for `entry`, lowest first, `entry` itself last: its dependencies (and theirs) by the
    // name their .mod files give. ValueError when one is missing or they depend on each other in a circle.
    QHash<QString, const moddata::ModEntry *> by_name;
    for (const moddata::ModEntry &m : mods) {
        const QString k = py::lower(py::strip(m.name));
        if (!by_name.contains(k))
            by_name.insert(k, &m);
    }
    std::vector<const moddata::ModEntry *> out;
    QStringList seen;
    std::function<void(const moddata::ModEntry &)> add = [&](const moddata::ModEntry &m) {
        const QString key = py::lower(py::strip(m.name));
        if (seen.contains(key))
            throw py::ValueError(u"%1 depends on itself (through %2)"_s.arg(m.name, seen.join(u" → "_s)).toStdString());
        if (std::any_of(out.begin(), out.end(), [&](const moddata::ModEntry *x) { return x == &m; }))
            return;
        seen << key;
        for (const QString &d : m.deps) {
            const moddata::ModEntry *p = by_name.value(py::lower(py::strip(d)), nullptr);
            if (!p || !p->folder || p->folder->isEmpty())
                throw py::ValueError(u"its parent mod \"%1\" is not in this mod folder"_s.arg(d).toStdString());
            add(*p);
        }
        seen.removeLast();
        out.push_back(&m);
    };
    add(entry);
    std::vector<moddata::ModEntry> res;
    for (const auto *m : out)
        res.push_back(*m);
    return res;
}

// ---------------------------------------------------------------------------------------- the working copy
State tree_state(const QString &root0)
{
    // {path below root, lower case: (path as it is, size, time of the last change)} of every file.
    State out;
    const QString root = pypath::abspath(root0);
    const qsizetype cut = root.size() + 1;
    QStringList stack = {root};
    while (!stack.isEmpty()) {
        const QString d = stack.takeLast();
        std::vector<fs::DirEntry> entries;
        try {
            entries = fs::scandir(d);
        } catch (const fs::OSError &) {
            continue;
        }
        for (const fs::DirEntry &e : entries) {
            if (e.is_dir) {
                if (d == root && modcopy::is_safe_dir(e.name))   // (C++ only: a mod's safeguards are not the mod)
                    continue;
                stack << e.path;
            } else if (e.is_file) {
                const QString rel = slashes(e.path.mid(cut));
                out.insert(py::lower(rel), {rel, e.size, e.mtime_ns});
            }
        }
    }
    return out;
}

bool _same_bytes(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (!fa.exists() || !fb.exists())
        return false;
    if (QFileInfo(a).size() != QFileInfo(b).size())
        return false;
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    while (true) {
        const QByteArray x = fa.read(1 << 20), y = fb.read(1 << 20);
        if (x != y)
            return false;
        if (x.isEmpty())
            return true;
    }
}

QString state_file(const QString &work)
{
    QString w = work;
    while (w.endsWith(u'\\') || w.endsWith(u'/'))
        w.chop(1);
    return w + u".state.json"_s;
}

Prepared prepare(const QStringList &layers, const QString &work, const std::function<void(const QString &)> &say)
{
    // Make `work` the files of `layers` laid over each other (folders, lowest first — the submod's own folder is the
    // last). Only what differs is copied, what belongs to no layer any more is taken out of `work`.
    View merged, view;
    for (qsizetype i = 0; i < layers.size(); ++i) {
        const QString root = pypath::abspath(layers[i]);
        const State st = tree_state(root);
        for (const auto &[key, e] : st) {
            const auto &[rel, size, mtime] = e;
            merged.insert(key, {rel, below(root, rel), size, mtime});
        }
        if (i < layers.size() - 1)
            for (const auto &[key, _e] : st)
                view.insert(key, merged.value(key));
    }
    fs::makedirs(work);
    const State have = tree_state(work);
    std::vector<std::pair<QString, std::tuple<QString, QString, long long, long long>>> todo;
    for (const auto &[key, v] : merged) {
        const auto *h = have.get(key);
        if (!h || std::get<1>(*h) != std::get<2>(v) || std::get<2>(*h) != std::get<3>(v))
            todo.emplace_back(key, v);
    }
    for (size_t n = 0; n < todo.size(); ++n) {
        const auto &[key, v] = todo[n];
        const auto &[rel, src, _size, _mtime] = v;
        if (n % 200 == 0 && say)
            say(u"Preparing the submod's working copy… %1 of %2 files"_s.arg(QString::number(n), QString::number(todo.size())));
        const QString dst = below(work, rel);
        fs::makedirs(pypath::dirname(dst));
        if (const auto *h = have.get(key); h && std::get<0>(*h) != rel)   // same name, other capitals: the layer's
            fs::remove(below(work, std::get<0>(*h)));
        fs::copy2(src, dst);
    }
    for (const auto &[key, e] : have)
        if (!merged.contains(key)) {
            try {
                fs::remove(below(work, std::get<0>(e)));
            } catch (const fs::OSError &) {
            }
        }
    const State state = tree_state(work);
    save_state(work, state);
    return {view, state};
}

static QString json_str(const QString &s)
{
    // json.dumps of a str with ensure_ascii (Python's default)
    QString out = u"\""_s;
    for (const QChar ch : s) {
        const char16_t u = ch.unicode();
        switch (u) {
        case u'"': out += u"\\\""_s; break;
        case u'\\': out += u"\\\\"_s; break;
        case u'\n': out += u"\\n"_s; break;
        case u'\r': out += u"\\r"_s; break;
        case u'\t': out += u"\\t"_s; break;
        case u'\b': out += u"\\b"_s; break;
        case u'\f': out += u"\\f"_s; break;
        default:
            if (u >= 0x20 && u <= 0x7E)
                out += ch;
            else
                out += u"\\u%1"_s.arg(uint(u), 4, 16, u'0');
        }
    }
    return out + u'"';
}

QByteArray state_json(const State &state)
{
    QStringList items;
    for (const auto &[k, e] : state)
        items << json_str(k) + u": ["_s + json_str(std::get<0>(e)) + u", "_s + QString::number(std::get<1>(e)) + u", "_s +
                     QString::number(std::get<2>(e)) + u']';
    return (u'{' + items.join(u", "_s) + u'}').toUtf8();
}

void save_state(const QString &work, const State &state)
{
    QFile f(fs::watched(state_file(work)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(state_json(state));
}

namespace {
// enough of json.load for a state file: {"key": ["rel", size, mtime], …} in the file's order
struct JsonReader {
    const QString &s;
    qsizetype i = 0;
    void ws()
    {
        while (i < s.size() && (s[i] == u' ' || s[i] == u'\t' || s[i] == u'\n' || s[i] == u'\r'))
            ++i;
    }
    bool eat(QChar c)
    {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    std::optional<QString> str()
    {
        ws();
        if (i >= s.size() || s[i] != u'"')
            return std::nullopt;
        ++i;
        QString out;
        while (i < s.size()) {
            const QChar c = s[i++];
            if (c == u'"')
                return out;
            if (c.unicode() < 0x20)
                return std::nullopt;                   // (json's strict mode: no control characters)
            if (c != u'\\') {
                out += c;
                continue;
            }
            if (i >= s.size())
                return std::nullopt;
            const QChar e = s[i++];
            switch (e.unicode()) {
            case u'"': out += u'"'; break;
            case u'\\': out += u'\\'; break;
            case u'/': out += u'/'; break;
            case u'b': out += u'\b'; break;
            case u'f': out += u'\f'; break;
            case u'n': out += u'\n'; break;
            case u'r': out += u'\r'; break;
            case u't': out += u'\t'; break;
            case u'u': {
                if (i + 4 > s.size())
                    return std::nullopt;
                bool ok = false;
                const uint x = s.mid(i, 4).toUInt(&ok, 16);
                if (!ok)
                    return std::nullopt;
                out += QChar(char16_t(x));
                i += 4;
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    std::optional<long long> num()
    {
        ws();
        const qsizetype a = i;
        if (i < s.size() && s[i] == u'-')
            ++i;
        while (i < s.size() && s[i].isDigit())
            ++i;
        if (i == a)
            return std::nullopt;
        bool ok = false;
        const long long x = s.mid(a, i - a).toLongLong(&ok);
        return ok ? std::optional<long long>(x) : std::nullopt;
    }
};
}  // namespace

std::optional<State> load_state(const QString &work)
{
    QFile f(fs::watched(state_file(work)));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QString text = QString::fromUtf8(f.readAll());
    JsonReader r{text};
    State out;
    if (!r.eat(u'{'))
        return std::nullopt;
    if (!r.eat(u'}')) {
        do {
            const auto k = r.str();
            if (!k || !r.eat(u':') || !r.eat(u'['))
                return std::nullopt;
            const auto rel = r.str();
            if (!rel || !r.eat(u','))
                return std::nullopt;
            const auto size = r.num();
            if (!size || !r.eat(u','))
                return std::nullopt;
            const auto mtime = r.num();
            if (!mtime || !r.eat(u']'))
                return std::nullopt;
            out.insert(*k, {*rel, *size, *mtime});
        } while (r.eat(u','));
        if (!r.eat(u'}'))
            return std::nullopt;
    }
    r.ws();
    if (r.i != text.size())
        return std::nullopt;
    return out;
}

Synced sync(const QString &work, const View &view, const State &state, const QString &target, const QString &backup_root,
            const GameFile &game)
{
    // What changed in the working copy since `state` goes where it belongs (into the submod / out of it / an empty
    // file that stands for a parent's file that is gone); the submod's files are kept under backup_root first.
    Synced r;
    State now = tree_state(work);
    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s);

    auto keep = [&](const QString &rel) {
        const QString src = below(target, rel);
        if (fs::isfile(src)) {
            const QString bdir = pypath::join(backup_root, {u"submod"_s, stamp, pypath::dirname(rel)});
            fs::makedirs(bdir);
            fs::copy2(src, pypath::join(bdir, pypath::basename(rel)));
        }
    };
    auto drop = [&](const QString &rel) {
        const QString src = below(target, rel);
        if (fs::isfile(src)) {
            keep(rel);
            fs::remove(src);
            r.removed << rel;
            QString d = pypath::dirname(src);                // folders left empty go with it
            while (pypath::normcase(d) != pypath::normcase(pypath::abspath(target))) {
                try {
                    fs::rmdir(d);
                } catch (const fs::OSError &) {
                    break;
                }
                d = pypath::dirname(d);
            }
        }
    };

    for (const auto &[key, e] : now) {
        const auto &[rel, size, mtime] = e;
        const auto *old = state.get(key);
        if (old && std::get<1>(*old) == size && std::get<2>(*old) == mtime)
            continue;                                        // untouched since the last look
        const QString src = below(work, rel);
        const auto *par = view.get(key);
        if (par && _same_bytes(src, std::get<1>(*par))) {
            drop(rel);                                       // the parent's file again
            continue;
        }
        const std::optional<QString> own = (game && !par) ? game(rel) : std::nullopt;
        if (own && !own->isEmpty() && _same_bytes(src, *own)) {
            drop(rel);                                       // the game's own file again
            continue;
        }
        const QString dst = below(target, rel);
        if (fs::isfile(dst) && _same_bytes(src, dst))
            continue;
        keep(rel);
        fs::makedirs(pypath::dirname(dst));
        fs::copy2(src, dst);
        r.written << rel;
    }
    bool again = false;
    for (const auto &[key, e] : state) {
        const QString &rel = std::get<0>(e);
        if (now.contains(key))
            continue;
        if (!view.contains(key)) {
            drop(rel);
        } else if (starts_any(key, EMPTY_TAKES_AWAY) && !starts_any(key, NEVER_EMPTY) &&
                   (key.endsWith(u".txt") || key.endsWith(u".csv"))) {
            keep(rel);
            for (const QString &root : {target, work}) {     // (the working copy shows what the game reads)
                const QString dst = below(root, rel);
                fs::makedirs(pypath::dirname(dst));
                QFile f(fs::watched(dst));
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    throw fs::OSError("open: " + dst.toStdString(), 13);
            }
            r.emptied << rel;
            again = true;
        } else {
            drop(rel);
            r.cannot << rel;
        }
    }
    if (again)
        now = tree_state(work);
    save_state(work, now);
    r.now = now;
    return r;
}

std::optional<std::pair<QStringList, QStringList>> recover(const QString &work, const QStringList &layers,
                                                           const QString &backup_root)
{
    // Before a working copy is made anew: what an earlier session changed in it and never carried over is carried
    // over now. -> (written, removed) or None
    const auto state = load_state(work);
    if (!state || !fs::isdir(work))
        return std::nullopt;
    View view;
    for (qsizetype i = 0; i + 1 < layers.size(); ++i) {
        const QString root = pypath::abspath(layers[i]);
        for (const auto &[key, e] : tree_state(root)) {
            const auto &[rel, size, mtime] = e;
            view.insert(key, {rel, below(root, rel), size, mtime});
        }
    }
    const Synced s = sync(work, view, *state, layers.last(), backup_root);
    if (s.written.isEmpty() && s.removed.isEmpty() && s.emptied.isEmpty())
        return std::nullopt;
    return std::make_pair(s.written + s.emptied, s.removed);
}

QString real_root(const moddata::Mod &mod)
{
    // The folder the game reads this mod from (a submod is edited in a working copy elsewhere).
    return mod.submod ? mod.submod->root : mod.root;
}

}  // namespace submods
