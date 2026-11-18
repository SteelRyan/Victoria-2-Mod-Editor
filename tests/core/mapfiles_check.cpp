// mapfiles against Python, the C++ side of tools/check_mapfiles.py.
// Usage: med_mapfiles --dump <out folder> <work folder>   |   med_mapfiles --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/mapfiles.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace mapfiles;
using pyform::L;
using pyform::v;

static QString mods_v(const Mods &m)
{
    QStringList p;
    for (const auto &[k, val] : m)
        p << L(k, val);
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
        QStringList out;
        for (const QString kind : {u"climate"_s, u"continent"_s}) {
            MembershipFile f(m, kind);
            QStringList van(f.vanilla.begin(), f.vanilla.end());
            std::sort(van.begin(), van.end());
            out << u"%1 path %2 hash %3 vanilla %4"_s.arg(kind, v(f.path), v(f.hash), v(van));
            out << u"%1 names %2 on_disk %3"_s.arg(kind, v(f.names), v(f.on_disk));
            for (const auto &[k, lst] : f.lists)
                out << u"%1 list %2 %3"_s.arg(kind, v(k), v(lst));
            for (const auto &[k, lst] : f.mods)
                out << u"%1 mods %2 %3"_s.arg(kind, v(k), mods_v(lst));
            QStringList mem;
            for (const auto &[p, ks] : f.membership())
                mem << L(p, ks);
            out << u"%1 membership %2"_s.arg(kind, pyform::list(mem));
            QStringList known = f.known_modifier_keys();
            std::sort(known.begin(), known.end());
            out << u"%1 known %2"_s.arg(kind, v(known));
            out << u"%1 dirty %2 missing_notes %3"_s.arg(kind, v(f.dirty()), v(f.missing_notes()));
            QStringList ofs;
            long long n = 0;
            for (const auto &kv : m.provinces) {
                if (n++ >= 50)
                    break;
                ofs << v(f.of(kv.first));
            }
            out << u"%1 of %2"_s.arg(kind, pyform::list(ofs));
        }
        DefaultMapFile d(m);
        out << u"default %1 %2 %3 %4 %5"_s.arg(v(d.path), v(d.max_provinces), v(d.sea), v(d.definitions), v(d.hash));
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
    QList<long long> land;
    for (const auto &kv : m.provinces)
        if (!kv.second.sea)
            land << kv.first;
    for (const QString kind : {u"climate"_s, u"continent"_s}) {
        MembershipFile f(m, kind);
        const QString a = f.names[0], b = f.names[1];
        f.assign(land.mid(0, 7), b);                     // moved from wherever they were
        OrderedMap<QString, Mods> mods;
        Mods ma(f.mods.value(a));
        if (!ma.empty())
            ma.erase(ma.begin());
        ma.emplace_back(u"zz_new_modifier"_s, u"0.25"_s);
        mods.insert(a, ma);
        const Mods mb = f.mods.value(b);
        if (!mb.empty()) {
            Mods nb{{mb[0].first, u"9.5"_s}};
            nb.insert(nb.end(), mb.begin() + 1, mb.end());
            mods.insert(b, nb);
        }
        f.set_definitions(mods, {u"zz_new_"_s + kind});
        OrderedMap<QString, Mods> m2;
        m2.insert(u"zz_new_"_s + kind, {{u"supply_limit"_s, u"3"_s}});
        f.set_definitions(m2, {});
        f.assign(land.mid(7, 5), u"zz_new_"_s + kind);
        std::printf("%s save: %s\n", qPrintable(kind), qPrintable(pypath::relpath(*f.save(backups, true), root)));
        f.assign(land.mid(12, 2), a);                    // a second save on top of the first
        std::printf("%s save 2: %s\n", qPrintable(kind), qPrintable(pypath::relpath(*f.save(backups), root)));
    }
    DefaultMapFile d(m);
    std::vector<long long> sea(d.sea.begin() + (d.sea.empty() ? 0 : 1), d.sea.end());
    sea.push_back(land[20]);
    sea.push_back(land[21]);
    d.sea = sea;
    if (d.max_provinces)
        *d.max_provinces += 1;
    std::printf("default.map: %s\n", qPrintable(pypath::relpath(*d.save(backups), root)));
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
