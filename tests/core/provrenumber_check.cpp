// provrenumber against Python, the C++ side of tools/check_provrenumber.py.
// Usage: med_provrenumber --dump <out folder> <work folder>  |  --edit <mod copy> <backups> <base game> <job.json>
#include "nopopup.h"
#include "core/fs.h"
#include "core/provrenumber.h"
#include "core/pypath.h"
#include "core/script.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace provrenumber;
using pyform::L;
using pyform::v;

static const QHash<QString, QString> MAPPING = {{u"12"_s, u"7"_s}, {u"300"_s, u"301"_s}};
static const QSet<QString> DROP = {u"55"_s, u"56"_s};
static const QStringList SCRIPTS = {
    u"country_event = {\n\ttrigger = {\n\t\towns = 55\n\t\t12 = { owner = { tag = ENG } }\n\t}\n\toption = {\n\t\tname = A\n"
    u"\t\t55 = { add_core = THIS }\n\t\tcapital = 56\n\t\tprovince_id = 300\n\t}\n}\n"_s,
    u"political_decisions = { d = { potential = { 56 = { is_core = ENG } } allow = { OR = { owns = 55 controls = 12 } } "
    u"effect = { 12 = { 55 = { x = y } } add_core = 56 } } }"_s,
    u"a = { owns = 55 }\n55 = { b = c }\ncapital = 12 # 55 = {\n\"55\" = { }\n"_s,
    u"immediate = { 55 = { unclosed\n"_s,
    u"trigger = { 12 = 3 } effect = { home = 12.5 x = 12 12 }"_s,
    u"option = {\n\t55 = {\n\t\towns = 56\n\t}\n\towns = 55\n}\n"_s,
    u"limit = { owns = 12\u008513 = { } 12 = { } }"_s,
    u"effect = { OWNS = 55 Capital = 12 }"_s,
    u""_s,
    u"effect = {   capital = 56   }\nnext = 1"_s,
    u"}} owns = 55 { option = { 300 = { 56 = { } } } }"_s,
    u"decision = { allow = { 55 = { } } } effect = { location = 56 } 56 = { }"_s};
static const QStringList LISTS = {u"region_1 = { 12 300 55 }\nx = 12\n12 = { 300 }\n"_s,
                                  u"12 = {\n\t300 = { }\n}\n300 = 1\n 12 = 3 { 12 }"_s, u"sea_starts = { 1 12 300 3000 }"_s,
                                  u""_s, u"12"_s};
struct CutCase {
    QString t;
    qsizetype a, b;
};
static const std::vector<CutCase> CUTS = {{u"a = 1\n  b = 2  \nc = 3"_s, 9, 14}, {u"a = 1 b = 2"_s, 6, 11}, {u"x"_s, 0, 1},
                                          {u"\n\nx\n"_s, 2, 3}, {u"ab\ncd"_s, 0, 2}, {u"ab\ncd"_s, 3, 5}, {u"  x  "_s, 2, 3}};
struct MoveCase {
    std::vector<long long> real, free;
    std::vector<std::pair<long long, std::optional<long long>>> heirs;
};
static const std::vector<MoveCase> MOVES = {
    {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, {3, 5}, {}},
    {{1, 2, 4, 6, 7, 9}, {3, 5, 8, 10}, {}},
    {{1, 2, 4, 6, 7, 9}, {3, 5, 8, 10}, {{3, 9}, {5, std::nullopt}}},
    {{1, 2, 4, 6, 7, 9, 11}, {3, 5, 8}, {{3, 7}, {5, 9}, {8, 9}}},
    {{1, 2, 4, 6, 7, 9}, {3, 5}, {{3, 100}}},
    {{}, {1}, {}},
    {{5}, {}, {}},
    {{1, 2, 4, 6, 7, 9}, {3, 5}, {{5, 9}, {3, 9}}},
    {{10, 20, 30, 40}, {1, 2, 3}, {{2, 40}, {1, 30}, {3, 10}}}};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static void say(const QString &s)
{
    const QByteArray b = (s + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
}
template <typename M> static QString items_v(const M &m, size_t limit = size_t(-1))
{
    QStringList p;
    size_t i = 0;
    for (const auto &[a, b] : m) {
        if (i++ >= limit)
            break;
        p << L(a, b);
    }
    return pyform::list(p);
}
static QString ll_v(const std::vector<long long> &l)
{
    QStringList p;
    for (long long x : l)
        p << QString::number(x);
    return u'[' + p.join(u", "_s) + u']';
}

static QStringList summaries()
{
    QStringList out;
    out << Job().summary();
    {
        Job j;
        j.moves[9] = 3;
        j.names[9] = u" Foo "_s;
        j.top = 8;
        j.counts[u"script places of a moved province"_s] = 1;
        j.retargeted = 1;
        j.neutral = 1;
        j.left = {{u"events/a.txt"_s, 1}};
        out << j.summary();
    }
    {
        Job j;
        for (auto [a, b] : std::vector<std::pair<long long, long long>>{{20, 5}, {19, 2}, {18, 9}, {17, 1}, {16, 3}, {15, 4}})
            j.moves[a] = b;
        j.names = {{20, u"Twenty"_s}, {18, u"  "_s}, {16, u"Sixteen"_s}};
        j.top = 14;
        j.counts[u"script places of a moved province"_s] = 5;
        j.counts[u"names"_s] = 2;
        j.retargeted = 3;
        j.neutral = 2;
        j.left = {{u"a.txt"_s, 2}, {u"b.txt"_s, 1}, {u"c.txt"_s, 3}, {u"d.txt"_s, 1}, {u"e.txt"_s, 1}};
        out << j.summary();
    }
    {
        Job j;
        j.moves[4] = 2;
        j.moves[5] = 3;
        j.names = {{4, u"A"_s}, {5, u"B"_s}};
        j.top = 3;
        j.counts[u"script places of a moved province"_s] = 2;
        j.left = {{u"x.txt"_s, 1}};
        out << j.summary();
    }
    return out;
}

// os.walk + sorted names, the same as provrenumber._txt_files (a copy here: the module keeps its own private)
static void txt_files(const QString &folder, QStringList &out)
{
    QStringList names;
    try {
        names = fs::listdir(folder);
    } catch (const fs::OSError &) {
        return;
    }
    QStringList dirs, files;
    for (const QString &n : names)
        (fs::isdir(pypath::join(folder, n)) ? dirs : files) << n;
    std::sort(files.begin(), files.end());
    for (const QString &f : files)
        if (f.toLower().endsWith(u".txt"))
            out << pypath::join(folder, f);
    for (const QString &d : dirs)
        txt_files(pypath::join(folder, d), out);
}
static QStringList txt_files(const QString &folder)
{
    QStringList out;
    txt_files(folder, out);
    return out;
}
static QString relslash(const QString &p, const QString &root) { return pypath::relpath(p, root).replace(u'\\', u'/'); }

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList common;
    for (const MoveCase &c : MOVES) {
        Heirs hs;
        for (const auto &[x, hh] : c.heirs)
            hs[x] = hh;
        const auto [mv, dr] = moves_for(c.real, c.free, hs);
        common << u"moves %1 %2"_s.arg(items_v(mv), ll_v(dr));
    }
    for (qsizetype i = 0; i < LISTS.size(); ++i)
        for (const QString &mode : {u"list"_s, u"top"_s}) {
            const auto [nw, k] = rewrite(LISTS[i], MAPPING, mode);
            common << u"rw %1 %2 %3 %4"_s.arg(QString::number(i), mode, v(nw), QString::number(k));
        }
    for (const CutCase &c : CUTS)
        common << u"cut "_s + v(_cut(c.t, c.a, c.b));
    for (qsizetype i = 0; i < SCRIPTS.size(); ++i)
        for (const QSet<QString> &drop : {DROP, QSet<QString>()}) {
            const ScriptResult r = rewrite_script(SCRIPTS[i], MAPPING, drop);
            common << u"rs %1 %2 %3 %4 %5 %6"_s.arg(QString::number(i), QString::number(drop.size()), v(r.text),
                                                   items_v(r.hits), QString::number(r.cut), QString::number(r.left));
        }
    for (const QString &s : summaries())
        common << u"sum "_s + v(s);
    struct M {
        QString shortn, rel;
        QStringList replace;
        bool find;
    };
    const std::vector<M> mods = {
        {u"ferrum_mare"_s, u"Ferrum_Mare 1.2.000"_s,
         {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s}, false},
        {u"vanilla"_s, u"Victoria 2"_s, {}, false},
        {u"corgi"_s, u"Victoria 2\\mod\\CoRGI_puir 18.1"_s, {}, true},
        {u"modus"_s, u"Victoria 2\\mod\\Modus Omnino Demens 1.6"_s, {}, true}};
    for (const M &md : mods) {
        const QString root = pypath::join(work, md.rel);
        QStringList replace = md.replace;
        if (md.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, md.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        QStringList out = common;
        std::vector<long long> P;
        for (const auto &kv : m.provinces)
            P.push_back(kv.first);
        std::sort(P.begin(), P.end());
        QHash<QString, QString> mapping;
        for (size_t i = 0; i < P.size(); i += 5)
            mapping.insert(QString::number(P[i]), QString::number(P[i] + 7));
        QSet<QString> drop;
        for (size_t i = 2; i < P.size(); i += 11)
            if (!mapping.contains(QString::number(P[i])))
                drop.insert(QString::number(P[i]));
        out << u"prov %1 %2 %3"_s.arg(QString::number(P.size()), QString::number(mapping.size()), QString::number(drop.size()));
        long long nfiles = 0, total = 0;
        QStringList folders = SCRIPT_FOLDERS;
        folders << u"history/units"_s << u"history/countries"_s;
        for (const QString &folder : folders) {
            const QStringList files = txt_files(pypath::join(m.root, folder.split(u'/'))).mid(0, 25);
            for (const QString &p : files) {
                const ScriptResult r = rewrite_script(script::read_text(p), mapping, drop);
                // (nn: the text without the C++-only notes and without any spacing — the same on both sides)
                static const QRegularExpression NOTE(u"# ModEditor: named province \\d+, which was deleted"_s);
                static const QRegularExpression SPACE(u"[ \\t\\r\\n\\f\\x0b]+"_s);   // (PCRE's \v takes U+0085 too)
                const QString nn = h(QString(r.text).remove(NOTE).remove(SPACE));
                out << u"script %1 %2 %3 %4 %5 %6 nn %7"_s.arg(relslash(p, m.root), h(r.text), QString::number(r.hits.size()),
                                                               items_v(r.hits, 5), QString::number(r.cut),
                                                               QString::number(r.left), nn);
                ++nfiles;
                for (const auto &[_k, c] : r.hits)
                    total += c;
            }
        }
        out << u"script total %1 %2"_s.arg(QString::number(nfiles), QString::number(total));
        const std::vector<std::pair<QString, QString>> lists = {
            {u"region.txt"_s, u"list"_s}, {u"continent.txt"_s, u"list"_s}, {u"climate.txt"_s, u"list"_s}, {u"positions.txt"_s, u"top"_s}};
        for (const auto &[name, mode] : lists) {
            const QString p = m.p({u"map"_s, name});
            if (fs::isfile(p)) {
                const auto [nw, k] = rewrite(script::read_text(p), mapping, mode);
                out << u"map %1 %2 %3"_s.arg(name, h(nw), QString::number(k));
            }
        }
        if (fs::isdir(m.pops_dir()))
            for (const QString &p : txt_files(m.pops_dir()).mid(0, 6)) {
                const auto [nw, k] = rewrite(script::read_text(p), mapping, u"top"_s);
                out << u"pops %1 %2 %3"_s.arg(relslash(p, m.root), h(nw), QString::number(k));
            }
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static void report(const std::optional<Job> &job, const QString &root)
{
    if (!job) {
        say(u"job None"_s);
        return;
    }
    say(u"summary "_s + job->summary());
    say(u"moves %1 dropped %2 top %3"_s.arg(items_v(job->moves), ll_v(job->dropped), QString::number(job->top)));
    std::vector<std::pair<long long, std::optional<long long>>> hs(job->heirs.begin(), job->heirs.end());
    std::sort(hs.begin(), hs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    say(u"heirs "_s + items_v(hs));
    std::vector<std::pair<QString, long long>> cs(job->counts.begin(), job->counts.end());
    std::sort(cs.begin(), cs.end());
    say(u"counts %1 %2 %3 %4"_s.arg(items_v(cs), QString::number(job->retargeted), QString::number(job->neutral),
                                     items_v(job->left)));
    for (const auto &[p, t] : job->texts)
        say(u"  text %1 %2 %3"_s.arg(relslash(p, root), h(t), QString::number(t.size())));
    QStringList rn;
    for (const auto &[a, b] : job->renames)
        rn << L(relslash(a, root), relslash(b, root));
    say(u"renames "_s + pyform::list(rn));
}

static Heirs heirs_of(const QJsonArray &a)
{
    Heirs out;
    for (const QJsonValue &e : a) {
        const QJsonArray p = e.toArray();
        out[p[0].toInteger()] = p[1].isNull() ? std::nullopt : std::optional<long long>(p[1].toInteger());
    }
    return out;
}

static int edit(const QString &root, const QString &backups, const QString &base, const QString &job_file)
{
    QFile jf(job_file);
    if (!jf.open(QIODevice::ReadOnly))
        return 4;
    const QJsonObject spec = QJsonDocument::fromJson(jf.readAll()).object();
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    auto keys_v = [](const auto &m) {
        std::vector<long long> k;
        for (const auto &kv : m)
            k.push_back(kv.first);
        std::sort(k.begin(), k.end());
        return ll_v(k);
    };
    auto rels = [&](const QStringList &ps) {
        QStringList r;
        for (const QString &p : ps)
            r << relslash(p, root);
        return v(r);
    };
    moddata::Mod m(root, base, std::nullopt, REPLACE);
    m.load();
    say(u"free %1 %2"_s.arg(keys_v(m.free_slots), QString::number(m.provinces.size())));
    m.free_heirs = heirs_of(spec[u"first"].toArray());
    const auto job = plan(m);
    report(job, root);
    {
        const auto [written, bdir] = apply(m, *job, backups);
        say(u"written %1 %2"_s.arg(rels(written), pypath::basename(pypath::dirname(bdir))));
    }
    say(u"after %1 %2"_s.arg(keys_v(m.free_slots), keys_v(m.free_heirs)));
    moddata::Mod m2(root, base, std::nullopt, REPLACE);
    m2.load();
    long long top = 0;
    for (const auto &kv : m2.provinces)
        top = std::max(top, kv.first);
    say(u"again %1 %2 %3"_s.arg(keys_v(m2.free_slots), QString::number(m2.provinces.size()), QString::number(top)));
    report(plan(m2), root);
    m2.free_heirs = heirs_of(spec[u"second"].toArray());
    const auto job2 = plan(m2);
    report(job2, root);
    const auto [written, bdir] = apply(m2, *job2, backups);
    say(u"written 2 "_s + rels(written));
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 6 && a[1] == u"--edit")
            return edit(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
