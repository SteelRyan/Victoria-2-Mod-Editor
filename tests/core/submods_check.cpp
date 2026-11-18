// submods against Python, the C++ side of tools/check_submods.py.
// Usage: med_submods --dump <out folder> <work folder>  |  --step <n> <sandbox>
#include "nopopup.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/submods.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace submods;
using pyform::L;
using pyform::v;

static const QStringList RELS = {u"common/goods.txt"_s, u"x/y.txt"_s, u"History/Provinces/a.txt"_s, u"map/default.map"_s,
                                 u"MAP/DEFAULT.MAP"_s, u"map/region.txt"_s, u"interface/a.gui"_s, u"localisation/x.csv"_s,
                                 u"poptypes/farmers.txt"_s, u"news/a.txt"_s, u"a.txt"_s, u"gfx\\flags\\ENG.tga"_s, u""_s};
static QStringList names()
{
    return {u"My Sub"_s, u""_s, u" "_s, u"a/b"_s, u"a\\b"_s, u"x:y"_s, u"x*y"_s, u"q?"_s, u"q\""_s, u"<a>"_s, u"p|q"_s,
            u"a\nb"_s, QString(80, u'x'), QString(81, u'x'), u"dot."_s, u"space "_s, u"Über"_s, u"ok-name_1"_s};
}
static const QStringList TEXTS = {u"plain"_s, u"caf\u00e9 \u20ac \u2026 \u0152"_s, u"\u0081\u008d\u0080\u009f"_s,
                                  u"\u4e2d\u6587"_s, u"\U0001F600!"_s, u"a\r\nb"_s};

static void say(const QString &s)
{
    const QByteArray b = (s + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
}
static QString norm_state(const State &st)
{
    QStringList p;
    for (const auto &[k, e] : st) {
        const auto &[rel, size, mtime] = e;
        p << pyform::list({v(k), v(rel), v(size), mtime < 1.5e18 ? v(mtime) : v(u"now"_s)});
    }
    return pyform::list(p);
}
static QString holds_v(const std::vector<std::tuple<QString, bool, long long>> &h, size_t limit = size_t(-1))
{
    QStringList p;
    for (size_t i = 0; i < h.size() && i < limit; ++i)
        p << L(std::get<0>(h[i]), std::get<1>(h[i]), std::get<2>(h[i]));
    return pyform::list(p);
}
static QString chain_v(const std::vector<moddata::ModEntry> &mods, const moddata::ModEntry &e)
{
    try {
        QStringList got;
        for (const auto &m : chain(mods, e))
            got << m.name;
        return v(got);
    } catch (const py::ValueError &x) {
        return v(u"ValueError: "_s + QString::fromUtf8(x.what()));
    }
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString PORT = pypath::join(work, u"ModEditor Cpp"_s);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList out;
    {
        QStringList c;
        for (const QString &r : RELS)
            c << v(counts(r));
        out << u"counts "_s + pyform::list(c);
    }
    const std::vector<std::vector<std::tuple<QString, bool, long long>>> fl = {
        {}, {{u"a"_s, false, 1}}, {{u"a"_s, true, 1}}, {{u"a"_s, true, 1}, {u"b"_s, true, 2}}};
    for (const auto &files : fl)
        for (bool pending : {false, true}) {
            const auto r = verdict(files, pending);
            out << u"verdict %1 %2 %3"_s.arg(QString::number(files.size()), v(pending), L(r.first, r.second));
        }
    {
        QStringList c;
        for (const QString &n : names())
            c << v(name_ok(n));
        out << u"names "_s + pyform::list(c);
    }
    struct A {
        QString name, folder;
        QStringList deps;
        std::optional<QString> user_dir;
        QStringList replace;
    };
    for (const A &a : std::vector<A>{{u"A"_s, u"a"_s, {}, std::nullopt, {}},
                                     {u"Sub mod"_s, u"Sub mod"_s, {u"Parent"_s}, u"Sub mod"_s, {u"history"_s, u"events"_s}},
                                     {u"x"_s, u"y"_s, {u"P1"_s, u"P 2"_s}, u""_s, {}}}) {
        const QString t = modfile_text(a.name, a.folder, a.deps, a.user_dir, a.replace);
        out << u"modtext %1 %2"_s.arg(v(t), QString::fromLatin1(cp1252_replace(t).toHex()));
    }
    for (const QString &t : TEXTS)
        out << u"cp1252 %1 %2"_s.arg(v(t), QString::fromLatin1(cp1252_replace(t).toHex()));
    const QString mods_dir = pypath::join(BASE, u"mod"_s);
    QStringList mf;
    for (const QString &f : fs::listdir(mods_dir))
        if (f.toLower().endsWith(u".mod"))
            mf << f;
    std::sort(mf.begin(), mf.end());
    for (const QString &f : mf) {
        const ModfileValues vals = modfile_values(pypath::join(mods_dir, f));
        QStringList other;
        for (const auto &[k, x] : vals.other)
            other << L(k, x);
        out << u"modfile %1 %2 %3 %4"_s.arg(f, v(vals.replace_path), v(vals.dependencies), pyform::list(other));
    }
    out << u"modfile missing [['dependencies', []], ['replace_path', []]]"_s;
    {
        const ModfileValues m = modfile_values(pypath::join(mods_dir, u"zz_none.mod"_s));
        if (!m.dependencies.isEmpty() || !m.replace_path.isEmpty() || !m.other.isEmpty())
            out.last() = u"modfile missing (not empty)"_s;
    }
    auto [mods, _b] = moddata::discover_mods(mods_dir);
    for (const auto &e : mods)
        out << u"chain %1 %2"_s.arg(v(e.name), chain_v(mods, e));
    std::vector<moddata::ModEntry> fake;
    auto add = [&](const QString &n, const QStringList &deps, const QString &folder) {
        moddata::ModEntry e;
        e.name = n;
        e.deps = deps;
        e.folder = folder;
        fake.push_back(e);
    };
    add(u"A"_s, {u"B"_s}, u"a"_s);
    add(u"B"_s, {u"A"_s}, u"b"_s);
    add(u"C"_s, {u"Z"_s}, u"c"_s);
    add(u" d "_s, {u" a "_s}, u"d"_s);
    add(u"E"_s, {u"F"_s, u"G"_s}, u"e"_s);
    add(u"F"_s, {u"G"_s}, u"f"_s);
    add(u"G"_s, {}, u"g"_s);
    add(u"H"_s, {u"I"_s}, u"h"_s);
    add(u"I"_s, {}, u""_s);
    for (const auto &e : fake)
        out << u"chain fake %1 %2"_s.arg(v(e.name), chain_v(fake, e));
    const State st = tree_state(pypath::join(PORT, u"reference"_s));
    {
        QStringList p;
        qsizetype i = 0;
        for (const auto &[k, e] : st)
            if (i++ < 40)
                p << L(k, std::get<0>(e), std::get<1>(e));
        out << u"tree %1 %2"_s.arg(QString::number(st.size()), pyform::list(p));
    }
    out << u"holds "_s + holds_v(holds(pypath::join(PORT, u"docs"_s)), 30);
    out << u"holds none "_s + holds_v(holds(pypath::join(PORT, u"zz_none"_s)));
    {
        State s;
        s.insert(u"a"_s, {u"A"_s, 1, 2});
        s.insert(u"caf\u00e9/\"q\"\\\t\u007f"_s, {u"\U0001F600\n"_s, 0, -5});
        out << u"json "_s + v(QString::fromUtf8(state_json(s)));
    }
    out << u"statefile %1 %2"_s.arg(state_file(u"C:\\x\\work\\"_s), state_file(u"C:/x/work//"_s));
    QFile f(QDir(out_dir).filePath(u"g2.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("g2 %lld lines\n", static_cast<long long>(out.size()));
    return 0;
}

static QString recover_v(const std::optional<std::pair<QStringList, QStringList>> &r)
{
    return r ? pyform::list({v(r->first), v(r->second)}) : u"None"_s;
}

static int step(int n, const QString &box)
{
    const QString parent = pypath::join(box, {u"mods"_s, u"Parent"_s}), sub = pypath::join(box, {u"mods"_s, u"Sub"_s});
    const QString work = pypath::join(box, {u"work"_s, u"Sub"_s}), backups = pypath::join(box, u"backups"_s);
    const QStringList layers = {parent, sub};
    const QString game_dir = pypath::join(box, u"game"_s);
    const GameFile game = [&](const QString &rel) -> std::optional<QString> {
        const QString p = pypath::join(game_dir, rel.split(u'/'));
        return rel.startsWith(u"common/") && fs::isfile(p) ? std::optional<QString>(p) : std::nullopt;
    };
    if (n == 1) {
        say(u"recover "_s + recover_v(recover(work, layers, backups)));
        const Prepared info = prepare(layers, work, [](const QString &s) { say(u"say "_s + s); });
        State vw;
        for (const auto &[k, e] : info.view)
            vw.insert(k, {std::get<0>(e), std::get<2>(e), std::get<3>(e)});
        say(u"view "_s + norm_state(vw));
        say(u"state "_s + norm_state(info.state));
        QFile f(pypath::join(box, u"view.tsv"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 4;
        QStringList lines;
        for (const auto &[k, e] : info.view)
            lines << QStringList{k, std::get<0>(e), std::get<1>(e), QString::number(std::get<2>(e)),
                                 QString::number(std::get<3>(e))}.join(u'\t');
        f.write(lines.join(u'\n').toUtf8());
    } else if (n == 2) {
        View view;
        QFile f(pypath::join(box, u"view.tsv"_s));
        if (!f.open(QIODevice::ReadOnly))
            return 4;
        for (const QString &l : QString::fromUtf8(f.readAll()).split(u'\n')) {
            const QStringList p = l.split(u'\t');
            if (p.size() == 5)
                view.insert(p[0], {p[1], p[2], p[3].toLongLong(), p[4].toLongLong()});
        }
        const auto state = load_state(work);
        if (!state)
            return 5;
        say(u"loaded %1"_s.arg(state->size()));
        const Synced s = sync(work, view, *state, sub, backups, game);
        say(u"sync %1 %2 %3 %4"_s.arg(v(s.written), v(s.removed), v(s.emptied), v(s.cannot)));
        say(u"now "_s + norm_state(s.now));
        const auto h = holds(sub);
        const auto vd = verdict(h);
        say(u"holds %1 %2"_s.arg(holds_v(h), L(vd.first, vd.second)));
    } else if (n == 3) {
        say(u"recover "_s + recover_v(recover(work, layers, backups)));
        const Prepared info = prepare(layers, work);
        say(u"state "_s + norm_state(info.state));
        say(u"recover again "_s + recover_v(recover(work, layers, backups)));
    } else if (n == 4) {
        const QString mods_dir = pypath::join(box, u"mods"_s);
        moddata::ModEntry pe;
        pe.name = u"Parent Mod"_s;
        pe.folder = parent;
        for (const QString &name : {u"New Sub"_s, u" Spaced "_s, u"bad/name"_s, u"New Sub"_s, u"Sub"_s, u"dot."_s, u"Café"_s}) {
            try {
                const auto [folder, modfile] = create(mods_dir, pe, name);
                say(u"create %1 %2"_s.arg(v(pypath::relpath(folder, box)), v(pypath::relpath(modfile, box))));
            } catch (const py::ValueError &x) {
                say(u"create refused %1 ValueError %2"_s.arg(v(name), QString::fromUtf8(x.what())));
            }
        }
        write_modfile(pypath::join(mods_dir, u"Odd.mod"_s), u"Odd \u4e2d \u20ac"_s, u"Odd"_s, {u"A"_s, u"B"_s}, std::nullopt,
                      {u"history"_s});
        const ModfileValues m = modfile_values(pypath::join(mods_dir, u"New Sub.mod"_s));
        std::vector<std::pair<QString, QString>> items = {{u"dependencies"_s, v(m.dependencies)},
                                                          {u"replace_path"_s, v(m.replace_path)}};
        for (const auto &[k, x] : m.other)
            items.emplace_back(k, v(x));
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        QStringList p;
        for (const auto &[k, x] : items)
            p << pyform::list({v(k), x});
        say(u"values "_s + pyform::list(p));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 4 && a[1] == u"--step")
            return step(a[2].toInt(), a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
