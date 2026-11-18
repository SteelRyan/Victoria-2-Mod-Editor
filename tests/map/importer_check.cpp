// importer against Python, the C++ side of tools/check_importer.py.
// Usage: med_importer --dump <out> <work>  |  --run <copy> <backups> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "map/importer.h"
#include "map/moreviews_data.h"
#include "map/nationextras_data.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace importer;

static QString h(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString r(const QString &s) { return py::repr_str(s); }
static QString r(const OptStr &s) { return s ? py::repr_str(*s) : u"None"_s; }
static QString tup(const QStringList &parts) { return u'(' + parts.join(u", "_s) + u')'; }
static QString lst(const QStringList &parts) { return u'[' + parts.join(u", "_s) + u']'; }
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }

struct Target {
    std::unique_ptr<moddata::Mod> mod;
    std::unique_ptr<document::Document> doc;
};
static Target target(const QString &root, const QString &work)
{
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    Target t;
    t.mod = std::make_unique<moddata::Mod>(root, pypath::join(work, u"Victoria 2"_s), std::nullopt, REPLACE);
    moddata::Mod &m = *t.mod;
    m.load();
    QString sc;
    for (const QString &x : m.pop_scenarios())
        if (script::parse_date(x)) {
            sc = x;
            break;
        }
    t.doc = std::make_unique<document::Document>(m, sc);
    auto &d = *t.doc;
    d.maps.insert(u"loc"_s, std::make_shared<localisation::LocEdits>(m));
    d.maps.insert(u"flags"_s, std::make_shared<nationextras::FlagEdits>(m));
    d.maps.insert(u"assets"_s, std::make_shared<moreviews::AssetEdits>(m));
    m.nation_data = std::make_shared<countryhist::NationData>(m);
    m.country_hist = std::make_shared<countryhist::CountryHistory>(m, *m.nation_data);
    auto nn = std::make_shared<countryhist::NewNations>(m, *m.country_hist);
    auto ne = std::make_shared<countryhist::NationEdits>(*m.country_hist, d.date);
    nn->nations = ne.get();
    d.maps.insert(u"new_nations"_s, nn);
    d.maps.insert(u"nations"_s, ne);
    d.maps.insert(u"scripts"_s, std::make_shared<scriptstore::ScriptStore>(m));
    return t;
}

static std::vector<Id> picks(Importer &imp)
{
    std::vector<Id> out;
    for (const Cat &cat : CATS) {
        const auto &its = imp.items(cat.id);
        long long nnew = 0, ndif = 0;
        std::vector<Id> nw, df;
        for (const ItemP &it : its) {
            if (it->status == u"new" && nnew < 2) {
                nw.push_back(it->id());
                ++nnew;
            }
            if (it->status == u"differs" && ndif < 1) {
                df.push_back(it->id());
                ++ndif;
            }
        }
        out.insert(out.end(), nw.begin(), nw.end());
        out.insert(out.end(), df.begin(), df.end());
    }
    return out;
}

static QStringList plan_lines(const QString &tag, const Plan &plan)
{
    QStringList out;
    QStringList ids;
    for (const ItemP &it : plan.items)
        ids << pyform::L(it->cat, it->key);
    out << u"%1 items %2"_s.arg(tag, lst(ids));
    QStringList nd;
    for (const Needed &n : plan.needed)
        nd << pyform::L(n.dep->cat, n.dep->key, n.why, n.comes, n.hard);
    out << u"%1 needed %2"_s.arg(tag, lst(nd));
    QStringList lc;
    for (const auto &[k, t] : plan.loc)
        lc << tup({r(k), r(t)});
    out << u"%1 loc %2 %3"_s.arg(tag).arg(plan.loc.size()).arg(h(lst(lc)));
    QStringList fs_, fl;
    for (const auto &[k, p] : plan.files)
        fs_ << pyform::L(k, p);
    for (const auto &[k, p] : plan.flags)
        fl << pyform::L(k, p);
    out << u"%1 files %2 flags %3"_s.arg(tag, lst(fs_), lst(fl));
    QStringList ld;
    for (const auto &[k, oc] : plan.land)
        ld << pyform::L(k, oc.first, oc.second);
    out << u"%1 land %2 icons %3"_s.arg(tag, lst(ld)).arg(plan.icons);
    QStringList pu, ad, tx;
    for (const auto &[s, a, _p] : plan.icon_ops.puts)
        pu << pyform::L(s, a);
    for (const auto &[s, x] : plan.icon_ops.adds)
        ad << pyform::L(s, static_cast<long long>(x.size()));
    // (Python: a dict filled in the plan's order — std::map sorts; the plan's order is rebuilt here)
    for (const ItemP &it : plan.items) {
        const auto f = plan.icon_ops.texts.find(it->id());
        if (f != plan.icon_ops.texts.end() && !tx.contains(pyform::L(pyform::L(it->cat, it->key), h(f->second))))
            tx << u'[' + pyform::L(it->cat, it->key) + u", "_s + r(h(f->second)) + u']';
    }
    out << u"%1 icon ops %2 %3 %4"_s.arg(tag, lst(pu), lst(ad), lst(tx));
    for (const QString &w : plan.warnings)
        out << u"%1 warn %2"_s.arg(tag, w);
    for (const QString &n : plan.notes)
        out << u"%1 note %2"_s.arg(tag, n);
    return out;
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    Target tg = target(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), work);
    moddata::Mod &m = *tg.mod;
    document::Document &d = *tg.doc;
    const std::vector<std::pair<QString, QString>> sources = {
        {u"corgi"_s, pypath::join(BASE, {u"mod"_s, u"CoRGI_puir 18.1"_s})},
        {u"modus"_s, pypath::join(BASE, {u"mod"_s, u"Modus Omnino Demens 1.6"_s})}, {u"vanilla"_s, BASE}};
    for (const auto &[shortn, folder] : sources) {
        Source src(folder, m.base);
        Importer imp(m, d, src);
        QStringList out = {u"source %1 replace %2 map_same %3"_s.arg(src.name, v(src.replace), tf(imp.map_same()))};
        for (const Cat &cat : CATS) {
            const auto &its = imp.items(cat.id);
            QStringList all;
            for (const ItemP &it : its)
                all << tup({r(it->key), r(it->status), r(it->name), r(it->group), r(it->rel)});
            out << u"cat %1 %2 %3"_s.arg(cat.id).arg(its.size()).arg(h(lst(all)));
            for (size_t i = 0; i < its.size() && i < 4; ++i) {
                const Item &it = *its[i];
                out << u"  %1 | %2 | %3 | %4 | %5 | %6"_s.arg(it.key, it.status, it.name, it.group.value_or(u"None"_s), it.rel, h(it.text));
                if (it.cat != u"files") {
                    QStringList ns;
                    for (const auto &[a, b, c] : imp.needs(it))
                        ns << pyform::L(a, b, c);
                    out << u"    needs "_s + lst(ns);
                }
            }
            if (cat.id == u"events" || cat.id == u"decisions")
                for (size_t i = 0; i < its.size() && i < 6; ++i)
                    out << u"    calls %1 %2"_s.arg(its[i]->key, v(its[i]->calls.items()));
        }
        const auto &nm = imp.names();
        QStringList ws, os;
        for (const auto &[k, l] : nm.word) {
            QStringList ls;
            for (const QString &x : l)
                ls << r(x);
            ws << tup({r(k), lst(ls)});
        }
        for (const auto &[k, x] : nm.opts)
            os << tup({r(k), r(x)});
        out << u"names %1 %2 opts %3 %4"_s.arg(nm.word.size()).arg(h(lst(ws))).arg(nm.opts.size()).arg(h(lst(os)));
        const auto &evs = imp.items(u"events"_s);
        for (size_t i = 0; i < evs.size() && i < 30; i += 3)
            out << u"chain %1 %2"_s.arg(evs[i]->key, v(imp.chain(evs[i]->key).items()));
        const std::vector<Id> pk = picks(imp);
        out += plan_lines(u"plan"_s, imp.plan(pk));
        std::set<Id> left(pk.begin() + std::min<size_t>(3, pk.size()), pk.begin() + std::min<size_t>(5, pk.size()));
        std::set<Id> also;
        const auto &techs = imp.items(u"techs"_s);
        for (size_t i = 0; i < techs.size() && i < 3; ++i)
            also.insert({u"techs"_s, techs[i]->key});
        Options opt;
        opt.nation_mode = u"land"_s;
        opt.event_conflict = u"replace"_s;
        out += plan_lines(u"plan land"_s, imp.plan(std::vector<Id>(pk.begin(), pk.begin() + std::min<size_t>(12, pk.size())), left, opt, also));
        QFile fh(QDir(out_dir).filePath(shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int run(const QString &root, const QString &backups, const QString &work)
{
    Target tg = target(root, work);
    moddata::Mod &m = *tg.mod;
    document::Document &d = *tg.doc;
    QStringList out;
    const QString folder = pypath::join(pypath::join(work, u"Victoria 2"_s), {u"mod"_s, u"CoRGI_puir 18.1"_s});
    Source src(folder, m.base);
    Importer imp(m, d, src);
    const std::vector<Id> pk = picks(imp);
    QStringList chain;
    for (const ItemP &it : imp.items(u"events"_s))
        if (it->status == u"new") {
            for (const QString &e : imp.chain(it->key).items())
                chain << e;
            break;
        }
    std::sort(chain.begin(), chain.end());
    out << u"chain "_s + v(chain);
    std::vector<Id> p1 = pk;
    for (qsizetype i = 0; i < chain.size() && i < 20; ++i)
        p1.push_back({u"events"_s, chain[i]});
    Options o1;
    o1.event_conflict = u"renumber"_s;
    const Plan plan = imp.plan(p1, {}, o1);
    out += plan_lines(u"plan"_s, plan);
    out << u"apply "_s + imp.apply(plan);
    Options o2;
    o2.nation_mode = u"land"_s;
    o2.event_conflict = u"replace"_s;
    const Plan plan2 = imp.plan(std::vector<Id>(pk.begin(), pk.begin() + std::min<size_t>(10, pk.size())), {}, o2);
    out << u"apply2 "_s + imp.apply(plan2, u"Second import"_s);
    const auto u = d.undo();
    const auto rd = d.redo();
    out << u"undo %1 redo %2"_s.arg(v(u), v(rd));
    OptStr pt;
    for (const ItemP &it : imp.items(u"poptypes"_s))
        if (it->status == u"new") {
            pt = it->key;
            break;
        }
    if (pt)
        out << u"popcheck "_s + import_from(m, d, u"type"_s, *pt, folder);
    const auto [written, err] = d.save(backups);
    QStringList rel;
    for (const QString &p : written)
        rel << pypath::relpath(p, root).replace(u'\\', u'/');
    std::sort(rel.begin(), rel.end());
    OptStr bdir;
    if (err) {
        bdir = pypath::relpath(*err, backups).replace(u'\\', u'/');
        bdir->replace(QRegularExpression(u"\\d{8}_\\d{6}"_s), u"STAMP"_s);
    }
    out << u"saved %1 backups %2"_s.arg(v(rel), v(bdir));
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
