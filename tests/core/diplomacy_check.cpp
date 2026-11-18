// diplomacy against Python, the C++ side of tools/check_diplomacy.py.
// Usage: med_diplomacy --dump <out folder> <work folder>   |   med_diplomacy --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace diplomacy;
using pyform::L;
using pyform::v;

static QString rec(const Record &r)
{
    return L(r.id, r.kind, r.first, r.second, r.start, r.end, r.file,
             r.span ? std::optional<std::pair<long long, long long>>(std::make_pair(
                          static_cast<long long>(r.span->first), static_cast<long long>(r.span->second)))
                    : std::nullopt);
}
static QString ids(const std::vector<Record> &rs)
{
    std::vector<long long> out;
    for (const Record &r : rs)
        out.push_back(r.id);
    return v(out);
}
static QString items_v(const OrderedMap<QString, QString> &m)
{
    QStringList p;
    for (const auto &[k, x] : m)
        p << L(k, x);
    return pyform::list(p);
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
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
        std::optional<Date> sd;
        for (const QString &s : m.pop_scenarios())
            if ((sd = script::parse_date(s)))
                break;
        QStringList out;
        for (const std::optional<Date> &date : {sd, std::optional<Date>()}) {
            DiploEdits d(m, date);
            out << u"date %1 files %2"_s.arg(v(date), items_v(d.files));
            for (const Record &r : d.records)
                out << u"rec "_s + rec(r);
            out << u"at_start "_s + ids(d.at_start());
            for (const auto &kv : m.countries) {
                const auto ov = d.overlord(kv.first);
                out << u"nation %1 %2 %3"_s.arg(kv.first, ov ? L(ov->first, ov->second) : u"None"_s, ids(d.of(kv.first)));
            }
            out << u"dirty "_s + v(d.dirty());
        }
        countryhist::NationData nd(m);
        countryhist::CountryHistory ch(m, nd);
        countryhist::NationEdits ne(ch, sd);
        out << u"great "_s + v(great_powers(m, ne)) + u' ' + v(great_powers(m, ne, 3));
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static void say(const QString &s)
{
    const QByteArray b = (s + u'\n').toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
}
static QString rels(const std::optional<QStringList> &paths, const QString &root)
{
    QStringList r;
    for (const QString &p : paths.value_or(QStringList()))
        r << pypath::relpath(p, root);
    return v(r);
}
static QString tup(const std::optional<std::pair<QString, QString>> &x)    // print() of a tuple or None
{
    return x ? u"("_s + v(x->first) + u", "_s + v(x->second) + u")"_s : u"None"_s;
}

static int edit(const QString &root, const QString &backups, const QString &base)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    std::optional<Date> sd;
    for (const QString &s : m.pop_scenarios())
        if ((sd = script::parse_date(s)))
            break;
    DiploEdits d(m, sd);
    const std::vector<Record> act = d.at_start();
    say(u"at start %1 records %2"_s.arg(act.size()).arg(d.records.size()));
    const Record first = act[0];
    d.remove(act[1].id);
    d.add(u"vassal"_s, first.first, u"ZZ1"_s);
    d.add(u"vassal"_s, u"ZZ2"_s, u"ZZ3"_s);
    d.add(u"union"_s, u"ZZ4"_s, u"ZZ5"_s);
    d.add(u"guarantee"_s, u"ZZ6"_s, u"ZZ7"_s);
    d.set_subject(first.first, first.second, std::make_pair(u"substate"_s, first.first));
    d.set_treaty(u"ZZ2"_s, u"ZZ3"_s, u"vassal"_s, false);
    d.set_treaty(u"ZZ8"_s, u"ZZ9"_s, u"alliance"_s, true);
    d.set_treaty(u"ZZ8"_s, u"ZZ9"_s, u"alliance"_s, true);
    say(u"overlord "_s + tup(d.overlord(first.second)) + u' ' + tup(d.overlord(u"ZZ1"_s)));
    QStringList kinds;
    for (const Record &r : d.between(first.second, first.first))
        kinds << r.kind;
    say(u"between "_s + v(kinds));
    const store::Snap snap = d.snapshot();
    d.restore(snap);
    say(u"dirty "_s + v(d.dirty()));
    say(u"save "_s + rels(d.save(backups), root));
    say(u"after %1 %2"_s.arg(d.records.size()).arg(v(d.dirty())));
    d.set_treaty(u"ZZ8"_s, u"ZZ9"_s, u"alliance"_s, false);
    d.add(u"alliance"_s, u"ZZ8"_s, u"ZZA"_s);
    say(u"save 2 "_s + rels(d.save(backups), root));
    const auto none = d.save(backups);
    say(u"nothing "_s + (none ? rels(none, root) : u"None"_s));
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 5 && a[1] == u"--edit")
            return edit(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
