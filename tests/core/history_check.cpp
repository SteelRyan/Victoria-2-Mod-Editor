// history against Python, the C++ side of tools/check_history.py.
// Usage: med_history --dump <out folder> <work folder>   |   med_history --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/history.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace history;
using pyform::L;
using pyform::v;

static QString st_line(const ProvState &st)
{
    QStringList facts;
    for (const Factory &f : st.factories) {
        const FactorySrc &s = *f.src;
        facts << L(f.building, f.level, f.upgrade, s.path,
                   std::make_pair(static_cast<long long>(s.span.first), static_cast<long long>(s.span.second)),
                   s.level_span ? std::optional<std::pair<long long, long long>>(std::make_pair(
                                      static_cast<long long>(s.level_span->first), static_cast<long long>(s.level_span->second)))
                                : std::nullopt,
                   s.date, s.level);
    }
    return pyform::list({v(st.owner), v(st.controller), v(st.cores), v(st.life_rating), v(st.trade_goods),
                         v(st.colonial), v(st.fort), v(st.naval_base), v(st.railroad), v(st.flags),
                         pyform::list(facts)});
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
        ProvinceHistory h(m);
        std::optional<Date> d;
        for (const QString &s : m.pop_scenarios())
            if ((d = script::parse_date(s)))
                break;
        QStringList out;
        out << u"start "_s + v(h.start()) + u" from_base %1"_s.arg(h.from_base);
        out << u"warnings "_s + v(m.warnings);
        for (const auto &[pid, fsl] : h.files) {
            QStringList fl;
            for (const HistFileP &f : fsl)
                fl << L(f->rel, f->path, f->pending, f->base, f->hash);
            out << u"files ["_s + v(pid) + u", "_s + pyform::list(fl) + u']';
            const HistFileP mf = h.main_file(pid);
            out << u"main %1 %2"_s.arg(pid).arg(mf ? v(mf->path) : u"None"_s);
        }
        QList<long long> pids;
        for (const auto &kv : h.files)
            pids << kv.first;
        std::sort(pids.begin(), pids.end());
        for (long long pid : pids) {
            out << u"st0 %1 %2"_s.arg(pid).arg(st_line(h.state_at(pid, std::nullopt)));
            if (d) {
                out << u"std %1 %2"_s.arg(pid).arg(st_line(h.state_at(pid, d)));
                out << u"stx %1 %2"_s.arg(pid).arg(st_line(h.state_at(pid, d, false)));
            }
            QStringList blocks;
            for (const DatedBlock &b : h.dated_blocks(pid)) {
                QStringList ls;
                for (const auto &[k, val] : b.lines)
                    ls << L(k, val);
                blocks << pyform::list({v(b.date), v(b.rel), pyform::list(ls)});
            }
            out << u"dated %1 %2"_s.arg(pid).arg(pyform::list(blocks));
        }
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static QString rels(const QStringList &paths, const QString &root)
{
    QStringList r;
    for (const QString &p : paths)
        r << u"'"_s + pypath::relpath(p, root) + u"'"_s;
    return u'[' + r.join(u", "_s) + u']';
}

static int edit(const QString &root, const QString &backups, const QString &base)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    ProvinceHistory h(m);
    const Date start = *h.start();
    QList<long long> pids;
    for (const auto &kv : h.files)
        pids << kv.first;
    std::sort(pids.begin(), pids.end());
    QList<long long> owned, with_fact;
    for (long long p : pids)
        if (h.state_at(p, start).owner)
            owned << p;
    for (long long p : owned)
        if (!h.state_at(p, start).factories.empty())
            with_fact << p;
    const long long p1 = owned[0], p2 = owned[1], p4 = owned[3];
    long long p3 = -1;                              // (four different provinces)
    for (long long p : with_fact)
        if (p != p1 && p != p2 && p != p4) {
            p3 = p;
            break;
        }
    QSet<QString> tagset;
    for (long long p : owned)
        tagset.insert(*h.state_at(p, start).owner);
    QStringList tags(tagset.begin(), tagset.end());
    std::sort(tags.begin(), tags.end());
    const QString sname = date_key(start);
    // 1) at the start date: owner + core, life rating + fort, a factory removed, a factory added
    {
        OrderedMap<long long, ProvState> ch;
        ProvState w = clone_state(h.state_at(p1, start));
        w.owner = (w.owner == tags[0]) ? tags[1] : tags[0];
        w.cores << *w.owner;
        ch.insert(p1, w);
        w = clone_state(h.state_at(p2, start));
        w.life_rating = 33;
        w.fort = 2;
        ch.insert(p2, w);
        w = clone_state(h.state_at(p3, start));
        w.factories.erase(w.factories.begin());
        ch.insert(p3, w);
        w = clone_state(h.state_at(p4, start));
        Factory nf;
        nf.building = u"zz_factory"_s;
        nf.level = 2;
        nf.upgrade = false;
        w.factories.push_back(nf);
        ch.insert(p4, w);
        std::printf("start-date save: %s\n", qPrintable(rels(h.save(ch, start, backups, sname), root)));
    }
    // 2) a later date: a nation that owns nothing yet (written on top), a controller, a core removed
    const Date later{std::get<0>(start) + 4, 1, 1};
    const long long p5 = owned[5], p6 = owned[6];
    const ProvState orig6 = clone_state(h.state_at(p6, later));
    {
        OrderedMap<long long, ProvState> ch;
        ProvState w = clone_state(h.state_at(p5, later));
        w.owner = u"ZZZ"_s;
        w.controller = std::nullopt;
        w.cores = {u"ZZZ"_s};
        ch.insert(p5, w);
        w = clone_state(h.state_at(p6, later));
        w.controller = tags[2];
        w.cores.removeFirst();
        ch.insert(p6, w);
        std::printf("later save: %s\n", qPrintable(rels(h.save(ch, later, backups, date_key(later)), root)));
    }
    // 3) undone: p6 wanted again as before (its file goes back to the letter)
    {
        OrderedMap<long long, ProvState> ch;
        ch.insert(p6, orig6);
        std::printf("undo save: %s\n", qPrintable(rels(h.save(ch, later, backups, date_key(later)), root)));
    }
    // 4) island flags: two on, one off (one the editor just put on)
    IslandFlags isl(h);
    isl.set({owned[7], owned[8]}, true);
    std::printf("islands on: %s\n", qPrintable(rels(isl.save(backups).value_or(QStringList()), root)));
    isl.set({owned[8]}, false);
    std::printf("islands off: %s\n", qPrintable(rels(isl.save(backups).value_or(QStringList()), root)));
    // 5) new (empty) history files for two provinces that have none
    OrderedMap<long long, QString> plan;
    for (const auto &kv : m.provinces)
        if (!h.files.contains(kv.first) && plan.size() < 2)
            plan.insert(kv.first, u"zz_new/%1 - New.txt"_s.arg(kv.first));
    NewHistFiles nh(h);
    nh.add_many(plan);
    std::printf("new files: %s\n", qPrintable(rels(nh.save(backups), root)));
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
