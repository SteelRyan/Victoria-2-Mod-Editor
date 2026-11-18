// G4 for moddata's writers, the C++ side: exactly the edits of tools/g4_moddata.py (py_run), on its own copy.
// Usage: med_g4_moddata <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/moddata.h"
#include "core/pypath.h"

#include <QCoreApplication>
#include <QSet>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace moddata;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 4)
        return 2;
    const QString root = QString::fromLocal8Bit(argv[1]), backups = QString::fromLocal8Bit(argv[2]);
    const QString base = QString::fromLocal8Bit(argv[3]);
    try {
        Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
        m.load();
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        const PopLoad pl = load_pop_files(m, sc);
        OrderedMap<long long, std::vector<Pop>> current;
        for (const auto &[pid, e] : pl.provinces)
            current.insert(pid, e.pops);
        std::vector<long long> sel;
        for (const auto &[pid, e] : pl.provinces)
            if (e.pops.size() >= 2 && sel.size() < 8)
                sel.push_back(pid);
        const long long a = sel[0], b = sel[1], c = sel[2], d = sel[3], e = sel[4], f = sel[5], g = sel[6], h = sel[7];
        current[a][0].size += 7;
        current[b].pop_back();
        const Pop t = current[c][0];
        Pop np;
        np.type = t.type;
        np.culture = t.culture;
        np.religion = t.religion;
        np.size = 123;
        current[c].push_back(np);
        current[d].clear();
        current[e][0].culture = u"zz_culture"_s;
        current[f][0].extra.emplace_back(u"militancy"_s, u"3.5"_s);
        long long fresh = -1;
        for (const auto &[pid, p] : m.provinces)
            if (!pl.provinces.contains(pid)) {      // (every land province has a block: a sea one)
                fresh = pid;
                break;
            }
        Pop fp;
        fp.type = t.type;
        fp.culture = t.culture;
        fp.religion = t.religion;
        fp.size = 50;
        current.insert(fresh, {fp});
        const QString first_file = pl.files.begin()->first;
        OrderedMap<long long, QString> targets;
        targets.insert(fresh, first_file);
        const auto [written, _bdir] =
            save_pops(m, sc, pl.provinces, pl.files, current, {a, b, c, d, e, f, fresh}, targets, backups);
        QStringList wq;
        for (const QString &w : written)
            wq << u"'"_s + w + u"'"_s;
        std::printf("save_pops wrote [%s]\n", qPrintable(wq.join(u", "_s)));
        std::printf("strip_pop_blocks wrote %lld files\n",
                    static_cast<long long>(strip_pop_blocks(m, QSet<long long>{g, h}, backups).size()));
        std::vector<QString> big;
        for (const auto &[rid, r] : m.regions)
            if (r.provinces.size() >= 2)
                big.push_back(rid);
        const QString r1 = big[0], r2 = big[1];
        OrderedMap<QString, std::vector<long long>> changed;
        std::vector<long long> rev(m.regions.get(r1)->provinces.rbegin(), m.regions.get(r1)->provinces.rend());
        changed.insert(r1, rev);
        changed.insert(r2, {});
        changed.insert(u"ZZZ_NEW_STATE"_s, {m.regions.get(r2)->provinces[0]});
        std::printf("save_regions: %s\n", save_regions(m, changed, backups) ? "True" : "False");
        const QString adopted = m.adopt(pypath::join(base, {u"map"_s, u"climate.txt"_s}));
        std::printf("adopt: %s\n", qPrintable(pypath::relpath(adopted, root)));
        m.create_scenario(u"1900.1.1"_s, sc);
        std::printf("create_scenario done\n");
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
