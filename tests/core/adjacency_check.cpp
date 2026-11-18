// adjacency (AdjacencyFile) against Python, the C++ side of tools/check_adjacency.py.
// Usage: med_adjacency --dump <out folder> <work folder>   |   med_adjacency --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/adjacency.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace adjacency;
using pyform::L;
using pyform::v;

template <typename S>
static std::vector<long long> sorted_of(const S &s)
{
    std::vector<long long> out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
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
    const std::vector<M> list = {
        {u"ferrum_mare"_s, u"Ferrum_Mare 1.2.000"_s,
         {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s}, false},
        {u"vanilla"_s, u"Victoria 2"_s, {}, false},
        {u"corgi"_s, u"Victoria 2\\mod\\CoRGI_puir 18.1"_s, {}, true},
        {u"modus"_s, u"Victoria 2\\mod\\Modus Omnino Demens 1.6"_s, {}, true}};
    for (const M &md : list) {
        const QString root = pypath::join(work, md.rel);
        QStringList replace = md.replace;
        if (md.find) {
            auto [ms, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : ms)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, md.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        AdjacencyFile a(m);
        QStringList out;
        out << u"file %1 %2 nl %3 trailing %4 next %5"_s.arg(v(a.path), v(a.hash), v(a.nl), v(a.trailing_nl)).arg(a.next_uid);
        out << u"no_coast "_s + v(sorted_of(a.no_coast)) + u" nc_lines "_s + v(sorted_of(a._nc_lines));
        for (const auto &[t, uid] : a.lines)
            out << u"line %1 %2"_s.arg(v(uid), v(t));
        for (const Row &r : a.rows)
            out << u"row %1 %2"_s.arg(L(r.uid, r.a, r.b, r.type, r.through, r.data, r.comment), a.category(r));
        for (const Mark &k : a.marks)
            out << u"mark "_s + L(k.uid, k.name, k.tiles);
        out << u"coast_comment %1 river_names %2 canals %3 skip %4"_s.arg(v(a.coast_comment())).arg(a.river_names())
                   .arg(v(a.canal_numbers()), v(sorted_of(a.skip_coast())));
        out << u"pairs %1"_s.arg(a.pair_index().size());
        out << u"dirty "_s + v(a.dirty());
        QFile fh(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int edit(const QString &root, const QString &backups, const QString &base)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    AdjacencyFile a(m);
    const AdjacencyFile::Snap snap0 = a.snapshot();      // (before any edit: the undo target)
    QList<long long> land, sea;
    for (const auto &kv : m.provinces)
        (kv.second.sea ? sea : land) << kv.first;
    const long long rn = a.river_names();
    a.add({{land[0], land[1], u"sea"_s, sea[0], 0, u"zz strait"_s},
           {land[2], land[3], u"sea"_s, 0, 0, u"Meme-River %1"_s.arg(rn)},
           {sea[1], sea[2], u"canal"_s, land[4], 7, u"zz canal"_s},
           {land[5], land[6], u"impassable"_s, 0, 0, QString()},
           {land[7], sea[3], u"sea"_s, 0, 0, a.coast_comment()},
           {land[8], land[9], u"land"_s, 0, 0, u"zz bridge"_s}});
    const long long first = a.rows[0].uid;
    a.update(first, [](Row &r) {
        r.comment = u"zz changed"_s;
        r.data = 3;
    });
    a.del({a.rows[1].uid, a.rows[2].uid});
    a.set_no_coast({sea[4], sea[5], land[10]}, true);
    a.set_mark(u"Meme-River %1"_s.arg(rn), {sea[6], sea[7]});
    a.set_mark(u"Meme-River %1"_s.arg(rn + 1), {sea[7], sea[8]});
    a.rename_mark(u"Meme-River %1"_s.arg(rn + 1), u"zz renamed river"_s);
    a.unmark({sea[6]});
    std::printf("save 1: %s\n", qPrintable(pypath::relpath(*a.save(backups), root)));
    // undo all of it: the deleted rows come back where they stood (was_after), the new ones go
    a.restore(snap0);
    std::printf("dirty after restore: %lld\n", static_cast<long long>(a.dirty().size()));
    a.add({{land[11], land[12], u"impassable"_s, 0, 0, QString()}});
    std::printf("save 2: %s\n", qPrintable(pypath::relpath(*a.save(backups), root)));
    std::printf("canals %s skip %lld rivers %lld\n", qPrintable(v(a.canal_numbers())),
                static_cast<long long>(a.skip_coast().size()), a.river_names());
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
