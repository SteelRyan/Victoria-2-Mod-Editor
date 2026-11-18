// events against Python, the C++ side of tools/check_events.py.  Usage: med_events --dump <out folder> <work folder>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/events.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace events;
using pyform::L;
using pyform::v;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString vtri(const Tri &t) { return t ? v(*t) : u"None"_s; }
static QString vtris(const std::vector<Tri> &ts)
{
    QStringList p;
    for (const Tri &t : ts)
        p << vtri(t);
    return pyform::list(p);
}
template <typename L_>
static QString nl(const std::optional<L_> &x) { return x ? v(static_cast<long long>(x->size())) : u"None"_s; }
static QString sorted_v(const QSet<QString> &s)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(l);
}
static QString scope_v(const Scope &s)
{
    return s.kind == u'c' ? L(u"c"_s, s.tag) : L(u"p"_s, s.pid);
}
static QString eff_v(const std::vector<Line> &ls)
{
    QStringList p;
    for (const Line &l : ls)
        p << L(l.depth, l.text, l.link);
    return pyform::list(p);
}
static QString cond_v(const std::vector<CondLine> &ls)
{
    QStringList p;
    for (const CondLine &l : ls)
        p << pyform::list({v(l.depth), v(l.text), l.has_state ? vtri(l.state) : u"''"_s});
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
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        m.nation_data = std::make_shared<countryhist::NationData>(m);
        m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
        d.maps.insert(u"nations"_s, std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date));
        d.maps.insert(u"diplomacy"_s, std::make_shared<diplomacy::DiploEdits>(m, d.date));
        EventDB db(m);
        QStringList out;
        QStringList fc;
        for (const auto &[k, x] : db.file_cats)
            fc << L(k, x);
        out << u"files "_s + v(db.files) << u"errors "_s + v(db.errors) << u"file_cats "_s + pyform::list(fc);
        out << u"set_flags "_s + sorted_v(db.set_flags) << u"set_gflags "_s + sorted_v(db.set_gflags)
            << u"set_mods "_s + sorted_v(db.set_mods) << u"set_pmods "_s + sorted_v(db.set_pmods)
            << u"set_vars "_s + sorted_v(db.set_vars);
        for (const auto &[k, lst] : db.fired_by) {
            QStringList p;
            for (const Fired &f : lst)
                p << L(f.src ? f.src->uid : OptStr(), f.how);
            out << u"fired %1 %2"_s.arg(k == NO_ID ? u"None"_s : v(k), pyform::list(p));
        }
        for (const auto &[key, who] : db.history_holders()) {
            QStringList ws;
            for (const store::Key &x : who)
                ws << x.str();
            std::sort(ws.begin(), ws.end());
            out << u"holder %1 %2 %3"_s.arg(key.first, v(key.second), v(ws));
        }
        for (const EntryP &ep : db.entries) {
            const Entry &e = *ep;
            QStringList emods, opts;
            for (const auto &[f, c] : e.mods)
                emods << L(f, static_cast<long long>(c.size()));
            for (const Option &o : e.options)
                opts << pyform::list({v(o.name), v(static_cast<long long>(o.effects.size())), nl(o.ai_chance)});
            out << u"e "_s + pyform::list({v(e.kind), v(e.key), v(e.id), v(e.file),
                                          L(static_cast<long long>(e.span.first), static_cast<long long>(e.span.second)),
                                          v(e.title), v(e.desc), v(e.picture), v(e.mtth), v(e.unit), pyform::list(emods),
                                          v(e.once), v(e.triggered_only), v(e.major), v(e.self_limit), pyform::list(opts),
                                          v(e.uid), v(e.rel), v(e.cats), nl(e.trigger), nl(e.potential), nl(e.allow),
                                          nl(e.effect), nl(e.ai_will_do), nl(e.immediate)});
            const Class c = classify(e);
            QString cv = u"None"_s;
            if (c.kind == u"tags")
                cv = sorted_v(c.tags);
            else if (c.kind == u"group")
                cv = v(c.group);
            out << u"  name %1 %2 class %3 %4"_s.arg(v(db.name(e)), v(db.description(e)), c.kind, cv);
            for (const Option &o : e.options)
                out << u"  eff "_s + eff_v(effect_lines(db, View(o.effects)));
            out << u"  eff "_s + eff_v(effect_lines(db, View(e.immediate)));
            out << u"  eff "_s + eff_v(effect_lines(db, View(e.effect)));
            for (const auto *blk : {&e.trigger, &e.potential, &e.allow})
                out << u"  cond "_s + cond_v(cond_lines(db, nullptr, View(*blk)));
        }
        World w(m, d);
        Judge judge(w, db);
        PotentialJudge pjudge(w, db);
        QStringList gps(w.gps.begin(), w.gps.end());
        std::sort(gps.begin(), gps.end());
        out << u"world "_s + pyform::list({v(w.start_year), v(static_cast<long long>(w.owner.size())), v(w.tags), v(gps),
                                          v(static_cast<long long>(w.cont.size())),
                                          v(static_cast<long long>(w.culture_group.size())),
                                          v(static_cast<long long>(w.party_ideology.size())),
                                          v(static_cast<long long>(w.issues.size())), v(static_cast<long long>(w.techs.size())),
                                          v(static_cast<long long>(w.inventions.size()))});
        for (size_t i = 0; i < db.entries.size(); ++i) {
            const Entry &e = *db.entries[i];
            const std::vector<Scope> scopes = scopes_of(w, e);
            std::vector<Scope> pick;
            for (size_t j = 0; j < scopes.size(); ++j)
                if (j < 2 || (j - 2) % 53 == 0)
                    pick.push_back(scopes[j]);
            for (const Scope &s : pick) {
                const Chance c = chance(judge, e, s);
                const Tri p = potential(pjudge, e, s);
                out << u"ch %1 %2 %3"_s.arg(i).arg(scope_v(s),
                                                   pyform::list({vtri(c.can), vtris(c.years),
                                                                 c.mtth ? L(c.mtth->first, c.mtth->second) : u"None"_s,
                                                                 L(c.p.first, c.p.second), L(c.times.first, c.times.second),
                                                                 vtri(c.allow), vtri(p)}));
            }
            if (!scopes.empty())
                for (const auto *blk : {&e.trigger, &e.potential, &e.allow})
                    out << u"  cs "_s + cond_v(cond_lines(db, &judge, View(*blk), scopes[0], w.start_year));
        }
        for (const auto &[name, jj] : {std::pair{u"unknown"_s, static_cast<Judge *>(&judge)},
                                        std::pair{u"punknown"_s, static_cast<Judge *>(&pjudge)}}) {
            QStringList p;
            for (const auto &[k, n] : jj->unknown)
                p << L(k, n);
            out << name + u' ' + pyform::list(p);
        }
        {
            std::vector<std::pair<QString, QString>> lit;
            for (const auto &x : lit_marked(db.files, [&](const QString &rel) { return db.texts.value(rel); }))
                lit.push_back(x);
            std::sort(lit.begin(), lit.end());
            QStringList p;
            for (const auto &x : lit)
                p << L(x.first, x.second);
            out << u"lit "_s + pyform::list(p);
        }
        for (const QString &rel : db.files) {
            const QString text = db.texts.value(rel), folder = rel.split(u'/')[0];
            const script::Nodes nodes = script::parse(text);
            const std::vector<Block> blocks = iter_blocks(nodes, folder);
            QStringList row{v(h(set_cats(text, folder, std::nullopt, {u"X"_s, u"Y"_s}))),
                            v(h(set_cats(text, folder, std::nullopt, {})))};
            std::vector<Block> pick;
            for (size_t j = 0; j < blocks.size() && j < 2; ++j)
                pick.push_back(blocks[j]);
            if (!blocks.empty())
                pick.push_back(blocks.back());
            for (const Block &b : pick) {
                const auto la = lit_line_above(text, b.node->kstart);
                row << v(b.uid) << v(h(set_cats(text, folder, b.uid, {u"A"_s, u"B"_s, u"A"_s})))
                    << v(h(set_cats(text, folder, b.uid, {}))) << v(h(set_lit_line(text, folder, b.uid, true)))
                    << v(h(set_lit_line(text, folder, b.uid, false)))
                    << v(v(cat_line_above(text, b.node->kstart).has_value()))
                    << v(la ? L(static_cast<long long>(la->first), static_cast<long long>(la->second)) : u"None"_s);
            }
            out << u"text %1 %2"_s.arg(rel, pyform::list(row));
        }
        QStringList days;
        for (const std::optional<double> &x : {std::optional<double>(), std::optional<double>(0.0), std::optional<double>(59.4),
                                               std::optional<double>(60.0), std::optional<double>(75.0),
                                               std::optional<double>(729.9), std::optional<double>(730.0),
                                               std::optional<double>(1000.0), std::optional<double>(45.5)})
            days << fmt_days(x);
        out << u"days "_s + v(days);
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
    QCoreApplication app(argc, argv);
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
