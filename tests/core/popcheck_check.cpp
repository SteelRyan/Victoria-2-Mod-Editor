// popcheck (+ importer::find_sources) against Python, the C++ side of tools/check_popcheck.py.
// Usage: med_popcheck --dump <out folder> <work folder>
#include "nopopup.h"
#include "core/document.h"
#include "core/importer.h"
#include "core/popcheck.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "pyform.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace popcheck;
using pyform::L;
using pyform::v;

static QString g_work;
static QString rel_w(const QString &p) { return pypath::relpath(p, g_work).replace(u'\\', u'/'); }
static QString where_v(const std::optional<Where> &w)
{
    if (!w)
        return u"None"_s;
    return pyform::list({v(w->first), v(w->second)});
}
static QString pids_v(const std::vector<long long> &p)
{
    QStringList s;
    for (size_t i = 0; i < p.size() && i < 20; ++i)
        s << QString::number(p[i]);
    return u'[' + s.join(u", "_s) + u']';
}
static QStringList probs_lines(const QString &tag, const Problems &probs)
{
    QStringList out;
    const std::pair<QString, const OrderedMap<QString, Problem> *> kinds[3] = {
        {u"type"_s, &probs.type}, {u"culture"_s, &probs.culture}, {u"religion"_s, &probs.religion}};
    for (const auto &[kind, m] : kinds)
        for (const auto &[k, d] : *m)
            out << u"%1 %2 %3 %4 %5 %6 %7"_s.arg(tag, kind, v(k), pids_v(d.pids), QString::number(d.pops),
                                                 QString::number(d.size), where_v(d.where));
    for (const auto &[pid, d] : probs.province)
        out << u"%1 province %2 %3 %4 %5"_s.arg(tag, QString::number(pid), QString::number(d.pops), QString::number(d.size),
                                               where_v(d.where));
    return out;
}
static QString sources_v(const std::vector<std::pair<QString, QString>> &l)
{
    QStringList p;
    for (const auto &[n, f] : l)
        p << L(n, rel_w(f));
    return pyform::list(p);
}
static moddata::Pop pop(const QString &t, const std::optional<QString> &c, const std::optional<QString> &r, long long size)
{
    moddata::Pop p;
    p.type = t;
    p.culture = c;
    p.religion = r;
    p.size = size;
    return p;
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    g_work = work;
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
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        auto sp = std::make_shared<scriptstore::ScriptStore>(m);
        d.maps.insert(u"scripts"_s, sp);
        scriptstore::ScriptStore &store = *sp;
        QStringList out;
        out << u"sources "_s + sources_v(importer::find_sources(m));
        const Problems p0 = problems(d, m);
        out << u"count %1"_s.arg(count(p0));
        out += probs_lines(u"as is"_s, p0);
        std::vector<long long> land;
        for (const auto &[pid, pr] : m.provinces)
            if (!pr.sea && !d.province_pops(pid).empty() && land.size() < 3)
                land.push_back(pid);
        const QString real_type = m.poptypes.begin()->first;
        d.pops[land[0]].push_back(pop(u"zz_type"_s, u"zz_cult"_s, u"zz_rel"_s, 100));
        d.pops[land[1]].push_back(pop(u"zz_type"_s, u"zz_cult"_s, u"zz_rel"_s, 250));
        d.pops[land[1]].push_back(pop(u"zz_type2"_s, std::nullopt, std::nullopt, 7));
        d.pops[land[2]].push_back(pop(u"zz_empty"_s, u""_s, u"zz_rel"_s, 3));
        d.pops.insert(999999, {pop(u"farmers"_s, u"x"_s, u"y"_s, 10), pop(u"zz_type"_s, u"x"_s, u"y"_s, 5)});
        d.pops.insert(999998, {pop(u"farmers"_s, u"x"_s, u"y"_s, 1)});
        dynamic_cast<document::DropPopBlocks &>(**d.maps.get(u"drop_pop_blocks"_s)).pids.insert(999998);
        store.set_text(u"poptypes/zz_type2.txt"_s, u"sprite = 1\n"_s);
        store.set_text(u"poptypes/zz_empty.txt"_s, u"  \n"_s);
        {
            const QSet<QString> vt = valid_types(m, &store);
            QStringList extra;
            for (const QString &t : vt)
                if (!m.poptypes.contains(t))
                    extra << t;
            std::sort(extra.begin(), extra.end());
            out << u"valid %1 %2"_s.arg(QString::number(vt.size()), v(extra));
        }
        const Problems p1 = problems(d, m);
        out << u"count %1"_s.arg(count(p1));
        out += probs_lines(u"made"_s, p1);
        auto last = [&]() { return d.undo_stack.empty() ? u"None"_s : v(d.undo_stack.back()->label); };
        long long n = change_into(d, u"type"_s, u"zz_type"_s, real_type);
        out << u"change %1 %2 %3"_s.arg(QString::number(n), last(), QString::number(d.undo_stack.size()));
        n = change_into(d, u"culture"_s, u"zz_cult"_s, u"zz_cult2"_s, QList<long long>{land[0]});
        out << u"change %1 %2"_s.arg(QString::number(n), last());
        n = change_into(d, u"religion"_s, u"zz_none"_s, u"x"_s);
        out << u"change %1 %2"_s.arg(QString::number(n), QString::number(d.undo_stack.size()));
        out += probs_lines(u"after"_s, problems(d, m));
        const std::vector<std::pair<QString, QString>> asks = {{u"type"_s, u"serfs"_s},       {u"type"_s, u"farmers"_s},
                                                               {u"culture"_s, u"zz_cult"_s},  {u"culture"_s, u"british"_s},
                                                               {u"religion"_s, u"catholic"_s}, {u"religion"_s, u"zz_rel"_s}};
        for (const auto &[kind, key] : asks)
            out << u"with %1 %2 %3"_s.arg(kind, key, sources_v(sources_with(m, kind, key)));
        const std::vector<std::optional<Where>> ws = {std::nullopt,
                                                      Where(std::nullopt, std::nullopt),
                                                      Where(u"a.txt"_s, std::nullopt),
                                                      Where(u"a.txt"_s, 0),
                                                      Where(u"a.txt"_s, 12),
                                                      Where(u""_s, 5)};
        for (const auto &w : ws)
            out << u"where %1 %2"_s.arg(where_v(w), v(where_text(w)));
        QFile f(QDir(out_dir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
