// mapmodes (window-less half) against Python, the C++ side of tools/check_mapmodes.py.
// Usage: med_mapmodes --dump <out folder> <work folder> <adjacent.json (written by the Python side)>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/events.h"
#include "core/mapmodes.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "pyform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace mapmodes;
using pyform::L;
using pyform::v;

static const QStringList MODES = {u"plurality"_s, u"literacy"_s, u"tech_all"_s, u"accepted"_s, u"tech:army_tech"_s,
                                  u"pt_soldiers"_s, u"pt_"_s, u"tech"_s, u"x"_s, u""_s, u"Literacy"_s};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString pairs_v(const std::vector<std::pair<QString, QString>> &l)
{
    QStringList p;
    for (const auto &[a, b] : l)
        p << L(a, b);
    return pyform::list(p);
}
static QString sorted_v(const QSet<QString> &s)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(l);
}
static QString dvec_v(const std::vector<double> &l, size_t limit = size_t(-1))
{
    QStringList p;
    for (size_t i = 0; i < l.size() && i < limit; ++i)
        p << v(l[i]);
    return pyform::list(p);
}

static QStringList sim_lines(const QString &name, StartSim &sim, const std::vector<long long> &pids)
{
    QStringList out;
    out << u"%1 how %2 sources %3 %4"_s.arg(name, sim.how, QString::number(sim.sources.size()), v(sim.sources.mid(0, 40)));
    {
        QStringList notes, bad;
        for (const auto &[k, n] : sim.notes)
            notes << L(k, n);
        std::vector<std::pair<QString, long long>> bs(sim.bad_scopes.begin(), sim.bad_scopes.end());
        std::sort(bs.begin(), bs.end());
        for (const auto &[k, n] : bs)
            bad << L(k, n);
        out << u"%1 notes %2 bad %3 set_lines %4 core_changes %5"_s.arg(name, pyform::list(notes), pyform::list(bad),
                                                                         QString::number(sim.set_lines),
                                                                         QString::number(sim.core_changes));
    }
    std::vector<long long> ch(sim.changed.begin(), sim.changed.end());
    std::sort(ch.begin(), ch.end());
    {
        QStringList p;
        for (size_t i = 0; i < ch.size() && i < 60; ++i)
            p << QString::number(ch[i]);
        out << u"%1 changed %2 [%3]"_s.arg(name, QString::number(ch.size()), p.join(u", "_s));
    }
    {
        QStringList p;
        for (const auto &[pid, c] : sim.cores)
            p << pyform::list({QString::number(pid), sorted_v(c)});
        out << u"%1 cores %2"_s.arg(name, h(pyform::list(p)));
    }
    {
        std::vector<std::pair<QString, QStringList>> cf;
        for (auto it = sim.cflags.cbegin(); it != sim.cflags.cend(); ++it)
            if (!it.value().isEmpty()) {
                QStringList f(it.value().begin(), it.value().end());
                std::sort(f.begin(), f.end());
                cf.emplace_back(it.key().value_or(QString()), f);
            }
        std::sort(cf.begin(), cf.end());
        QStringList p;
        for (const auto &[t, f] : cf)
            p << pyform::list({v(t), v(f)});
        out << u"%1 cflags %2 gflags %3"_s.arg(name, pyform::list(p), sorted_v(sim.gflags));
    }
    {
        QStringList a, b;
        for (const auto &[pid, x] : sim.lit)
            a << pyform::list({QString::number(pid), dvec_v(x)});
        for (const auto &[pid, x] : sim.base)
            b << pyform::list({QString::number(pid), L(x.first, x.second)});
        out << u"%1 lit %2 base %3"_s.arg(name, h(pyform::list(a)), h(pyform::list(b)));
    }
    std::vector<std::pair<long long, std::optional<double>>> vals;
    for (long long p : pids)
        vals.emplace_back(p, sim.province(p));
    {
        QStringList p;
        for (const auto &[pid, x] : vals)
            p << pyform::list({QString::number(pid), v(x)});
        out << u"%1 province %2"_s.arg(name, h(pyform::list(p)));
    }
    for (size_t i = 0; i < vals.size() && i < 40; ++i) {
        const auto *l = sim.lit.get(vals[i].first);
        out << u"  %1 %2 %3 %4"_s.arg(name, QString::number(vals[i].first), v(vals[i].second),
                                     l ? dvec_v(*l, 6) : u"[]"_s);
    }
    for (size_t i = 0; i < ch.size() && i < 40; ++i) {
        const auto b = sim.base.value(ch[i], {0.0, 0.0});
        out << u"  %1 set %2 %3 %4"_s.arg(name, QString::number(ch[i]), v(sim.province(ch[i])), L(b.first, b.second));
    }
    out << u"%1 caption %2"_s.arg(name, v(sim.caption()));
    return out;
}

static int dump(const QString &out_dir, const QString &work, const QString &adj_json)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QFile jf(adj_json);
    if (!jf.open(QIODevice::ReadOnly))
        return 4;
    const QJsonObject adj_all = QJsonDocument::fromJson(jf.readAll()).object();
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
    for (const M &md_ : mods) {
        const QString root = pypath::join(work, md_.rel);
        QStringList replace = md_.replace;
        if (md_.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, md_.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        m.nation_data = std::make_shared<countryhist::NationData>(m);
        m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        auto sp = std::make_shared<scriptstore::ScriptStore>(m);
        d.maps.insert(u"scripts"_s, sp);
        d.maps.insert(u"nations"_s, std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date));
        d.maps.insert(u"diplomacy"_s, std::make_shared<diplomacy::DiploEdits>(m, d.date));
        scriptstore::ScriptStore &store = *sp;
        events::EventDB db(m, &store);
        // the neighbours from the C++ MapData (its sets walked in Python's order); Python's own list stays in
        // adjacent.json as a cross-check
        const mapview::MapData mdata(m, m.p({u"map"_s, m.provinces_bmp}));
        {
            long long same = 0, differ = 0;
            for (const QJsonValue &e : adj_all.value(md_.shortn).toArray()) {
                const QJsonArray a = e.toArray();
                std::vector<long long> l;
                for (const QJsonValue &q : a[1].toArray())
                    l.push_back(q.toInteger());
                (mdata.neighbours(a[0].toInteger()) == l ? same : differ) += 1;
            }
            if (differ)
                std::fprintf(stderr, "%s: %lld neighbour lists differ from Python's (%lld same)\n", qPrintable(md_.shortn),
                             differ, same);
        }
        const Neighbours nb = [&mdata](long long p) { return mdata.neighbours(p); };
        QStringList out;
        out << u"tech_kinds "_s + pairs_v(tech_kinds(m));
        for (const QString &tab : {u"pops"_s, u"nations"_s, u"provinces"_s})
            out << u"modes %1 %2"_s.arg(tab, pairs_v(extra_modes(m, tab)));
        {
            QStringList p;
            for (const QString &x : MODES)
                p << v(handles(x));
            out << u"handles "_s + pyform::list(p);
        }
        std::vector<long long> land;
        for (const auto &[pid, pr] : m.provinces)
            if (!pr.sea)
                land.push_back(pid);
        {
            StartSim sim(m, d, &db, nb, [&store](const QString &rel) { return store.text(rel); });
            out += sim_lines(u"marked"_s, sim, land);
        }
        {
            StartSim sim(m, d, nullptr, nb);
            out += sim_lines(u"plain"_s, sim, land);
        }
        QFile f(QDir(out_dir).filePath(md_.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md_.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 5 && a[1] == u"--dump")
            return dump(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
