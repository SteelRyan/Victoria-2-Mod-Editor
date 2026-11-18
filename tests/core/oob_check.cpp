// oob (+ diplomacy's relation helpers) against Python, the C++ side of tools/check_oob.py.
// Usage: med_oob --dump <out folder> <work folder>   |   med_oob --edit <mod copy> <backups> <base game>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/oob.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace oob;
using pyform::L;
using pyform::v;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString num_v(const Num &n) { return n.is_int ? v(static_cast<long long>(n.v)) : v(n.v); }
static QString span_v(const std::optional<Span> &s)
{
    return s ? L(static_cast<long long>(s->first), static_cast<long long>(s->second)) : u"None"_s;
}
static Centers centers(long long n)
{
    Centers c;
    c.cx = [](long long p) { return py::mod(double(p) * 1.5, 97.0); };
    c.cy = [](long long p) { return py::mod(double(p) * 0.7, 53.0); };
    c.n = n;
    return c;
}

static QString oob_v(const OOB &o)
{
    QStringList arm, nav, rel;
    for (const Army &a : o.armies) {
        QStringList regs;
        for (const Regiment &r : a.regiments)
            regs << L(r.name, r.type, r.home);
        arm << pyform::list({v(a.name), v(a.location), pyform::list(regs), v(a.extra), span_v(a.src)});
    }
    for (const Navy &n : o.navies) {
        QStringList ships;
        for (const Ship &s : n.ships)
            ships << L(s.name, s.type);
        nav << pyform::list({v(n.name), v(n.location), pyform::list(ships), v(n.extra), span_v(n.src)});
    }
    for (const auto &[t, r] : o.relations)
        rel << pyform::list({v(t), v(r.value), v(r.level), v(r.influence_value), span_v(r.src)});
    return pyform::list({pyform::list(arm), pyform::list(nav), pyform::list(rel)});
}

static OOB changed(OOB o, const QStringList &tags)
{
    // A changed copy of an OOB (as tools/check_oob.py changed()).
    if (!o.armies.empty()) {
        Army &a = o.armies[0];
        a.name += u" II"_s;
        if (!a.regiments.empty())
            a.regiments.pop_back();
    }
    Army na;
    na.name = u"Army 99"_s;
    na.location = 1;
    na.regiments = {{u"1st Zz Regiment"_s, u"infantry"_s, 1}, {u"2nd Zz"_s, u"cavalry"_s, std::nullopt},
                    {u"3rd Zz"_s, u"artillery"_s, 2}};
    o.armies.push_back(na);
    Navy nv;
    nv.name = u"Zz Fleet"_s;
    nv.location = 3;
    nv.ships = {{u"Ship 1"_s, u"frigate"_s}};
    o.navies.push_back(nv);
    QStringList rels;
    for (const auto &kv : o.relations)
        rels << kv.first;
    if (!rels.isEmpty())
        o.relations.get(rels[0])->value = u"77"_s;
    if (rels.size() > 1)
        o.relations.remove(rels[1]);
    o.relations.insert(tags[0], Relation{u"-20"_s, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    o.relations.insert(tags[1], Relation{u"5"_s, u"4"_s, u"12.5"_s, std::nullopt, std::nullopt});
    return o;
}

static QString kv_ll(const OrderedMap<long long, long long> &m)
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
        auto oep = std::make_shared<OOBEdits>(m, *m.country_hist, d.date);
        d.maps.insert(u"oob"_s, oep);
        OOBEdits &oe = *oep;
        long long top = 0;
        for (const auto &kv : m.provinces)
            top = std::max(top, kv.first);
        const long long n = top + 1;
        const Centers md = centers(n);
        d.coastal = std::vector<bool>(size_t(n));
        for (long long p = 0; p < n; ++p)
            (*d.coastal)[size_t(p)] = p % 3 == 0;
        const UnitTypes &u = *oe.units;
        const SlotRules &r = *oe.rules;
        QStringList out;
        QStringList ty;
        for (const auto &[k, t] : u.types)
            ty << L(k, t.naval, t.unit_type, t.name);
        out << u"types "_s + pyform::list(ty) << u"land "_s + v(u.land()) << u"naval "_s + v(u.naval());
        QStringList names;
        for (const OptStr &k : {OptStr(u"infantry"_s), OptStr(u"artillery_offensive"_s), OptStr(u"zz_x"_s), OptStr()})
            names << u.name(k);
        for (const OptStr &k : {OptStr(u"infantry"_s), OptStr(u"artillery_offensive"_s), OptStr(u"zz_x"_s), OptStr()})
            names << u.word(k);
        out << u"names "_s + v(names);
        out << u"rules "_s + pyform::list({num_v(r.first), num_v(r.per), num_v(r.noncore), num_v(r.colony),
                                          num_v(r.protectorate), v(r.missing)});
        QStringList br;
        for (long long s : {0LL, 999LL, 1000LL, 2999LL, 3000LL, 9000LL, 25000LL})
            for (long long mm : {1LL, 3LL, 5LL, 8LL})
                br << v(r.brigades(s, Num{double(mm), true}));
        out << u"brig "_s + pyform::list(br);
        QStringList ords;
        for (long long x : {1, 2, 3, 4, 11, 12, 13, 21, 22, 101, 111, 112, 1000})
            ords << ordinal(x);
        out << u"ordinal "_s + v(ords);
        QStringList tags;
        for (const auto &kv : m.countries)
            tags << kv.first;
        for (const QString &t : tags) {
            const auto [path, ref] = oe.file_of(t);
            const OOB st = oe.state(t);
            out << u"oob %1 %2 %3 %4"_s.arg(t, v(path), v(ref), v(oe.exists(t)));
            if (!st.armies.empty() || !st.navies.empty() || !st.relations.isEmpty()) {
                out << u"  st "_s + oob_v(st);
                const QString text = (path && !path->isEmpty() && fs::isfile(*path)) ? script::read_text(*path) : QString();
                out << u"  same "_s + v(write_oob(text, st) == text);
                out << u"  w "_s + h(write_oob(text, changed(st, tags)));
                OOB gone = st;                     // (armies[1:], navies[:-1], no relations)
                if (!gone.armies.empty())
                    gone.armies.erase(gone.armies.begin());
                if (!gone.navies.empty())
                    gone.navies.pop_back();
                gone.relations.clear();
                out << u"  g "_s + h(write_oob(text, gone));
                QStringList ships;
                for (long long x : next_ship_numbers(st.navies, 3))
                    ships << QString::number(x);
                out << u"  names "_s + v(next_army_name(st.armies)) + u' ' + u'[' + ships.join(u", "_s) + u']';
                for (size_t ai = 0; ai < st.armies.size() && ai < 2; ++ai) {
                    QStringList all;
                    OrderedMap<QString, long long> start;
                    start.insert(u"Infantry"_s, 2);
                    for (const QString &s : {u"auto"_s, u"nation"_s, u"custom"_s})
                        all << v(name_regiments(st.armies[ai].regiments, u, s, u"{nth} {type} of {army} ({home}) {n}"_s,
                                                st.armies[ai].name, start, &m.provinces));
                    out << u"  regn "_s + pyform::list(all);
                }
            }
        }
        out << u"new "_s + h(write_oob(QString(), changed(empty_oob(), tags)));
        for (const auto &[t, per] : oe.all_slots(d)) {
            QStringList states;
            for (const auto &[st_, e] : per) {
                QStringList mult, why;
                for (const auto &[p, x] : e.mult)
                    mult << pyform::list({v(p), num_v(x)});
                for (const auto &[p, x] : e.why)
                    why << L(p, x);
                QStringList pids;
                for (long long p : e.pids)
                    pids << QString::number(p);
                states << pyform::list({v(st_), v(e.soldiers), u'[' + pids.join(u", "_s) + u']', kv_ll(e.sold),
                                        kv_ll(e.pcap), v(e.cap), pyform::list(mult), pyform::list(why)});
            }
            out << u"slots %1 %2"_s.arg(t, pyform::list(states));
        }
        {
            const auto u_ = oe.used();
            std::vector<std::pair<long long, long long>> used(u_.begin(), u_.end());
            std::sort(used.begin(), used.end());
            QStringList p;
            for (const auto &[a, b] : used)
                p << L(a, b);
            out << u"used "_s + pyform::list(p);
        }
        const QSet<QString> wu = oe.tags_with_units();
        QStringList with_units(wu.begin(), wu.end());
        std::sort(with_units.begin(), with_units.end());
        out << u"with_units "_s + v(with_units);
        for (qsizetype i = 0; i < with_units.size() && i < 40; ++i) {
            const QString &t = with_units[i];
            const auto pc = OOBEdits::province_caps(oe.state_slots(d, t));
            const auto oc_ = oe.over_cap(t, pc);
            std::vector<std::pair<long long, long long>> oc(oc_.begin(), oc_.end());
            std::sort(oc.begin(), oc.end());
            QStringList ocs;
            for (const auto &[a, b] : oc)
                ocs << L(a, b);
            out << u"over %1 %2"_s.arg(t, pyform::list(ocs));
            const OOB st = oe.state(t);
            const OptInt near_pid = st.armies.empty() ? OptInt() : st.armies[0].location;   // (windows.h: near)
            QStringList homes;
            for (long long x : oe.new_homes(d, md, t, near_pid, 5))
                homes << QString::number(x);
            out << u"homes %1 [%2]"_s.arg(t, homes.join(u", "_s));
            QStringList others;
            for (const auto &kv : st.relations)
                if (others.size() < 3)
                    others << kv.first;
            others += with_units.mid(0, 2);
            QStringList rels;
            for (const QString &o : others)
                rels << pyform::list({v(o), v(diplomacy::relation_value(oe, t, o)), v(diplomacy::level_of(oe, t, o)),
                                      v(diplomacy::influence_of(oe, t, o))});
            out << u"rel %1 %2"_s.arg(t, pyform::list(rels));
        }
        {
            QStringList p;
            for (const auto &[b, lords] : diplomacy::sphere_lords(oe, with_units))
                p << L(b, lords);
            out << u"spheres "_s + pyform::list(p);
        }
        std::vector<Fix> todo;
        for (qsizetype i = 0; i < with_units.size() && i < 25; ++i) {
            const QString &t = with_units[i];
            const OOB st = oe.state(t);
            if (!st.armies.empty() && !st.armies[0].regiments.empty()) {
                todo.push_back({u"home"_s, t, 0, 0, {}, {}});
                todo.push_back({u"reg_type"_s, t, 0, static_cast<long long>(st.armies[0].regiments.size()) - 1, {}, {}});
                todo.push_back({u"army_loc"_s, t, 0, 0, {}, {}});
                if (st.armies[0].regiments.size() > 2)
                    todo.push_back({u"over"_s, t, 0, 0, {}, {{0, 1}, {0, 2}}});
            }
            if (!st.navies.empty()) {
                todo.push_back({u"navy_loc"_s, t, 0, 0, {}, {}});
                if (!st.navies[0].ships.empty())
                    todo.push_back({u"ship_type"_s, t, 0, 0, {}, {}});
            }
            if (!st.relations.isEmpty())
                todo.push_back({u"rel_unknown"_s, t, 0, 0, st.relations.begin()->first, {}});
        }
        fix_units(d, md, todo)();
        for (qsizetype i = 0; i < with_units.size() && i < 25; ++i)
            out << u"fixed %1 %2"_s.arg(with_units[i], oob_v(oe.state(with_units[i])));
        out << u"dirty "_s + v(oe.dirty());
        QFile f(QDir(out_dir).filePath(md_.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md_.shortn), static_cast<long long>(out.size()));
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

static int edit(const QString &root, const QString &backups, const QString &base)
{
    moddata::Mod m(root, base, std::nullopt, {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    QString sc;
    for (const QString &s : m.pop_scenarios())
        if (script::parse_date(s)) {
            sc = s;
            break;
        }
    countryhist::NationData nd(m);
    countryhist::CountryHistory ch(m, nd);
    OOBEdits oe(m, ch, script::parse_date(sc));
    QStringList tags;
    for (const auto &kv : m.countries)
        if (ch.files.contains(kv.first))
            tags << kv.first;
    std::sort(tags.begin(), tags.end());
    QStringList have, none;
    for (const QString &t : tags)
        (oe.exists(t) ? have : none) << t;
    say(u"have %1 none %2"_s.arg(have.size()).arg(none.size()));
    auto update = [&](OOB &s) { s = changed(s, tags); };
    for (qsizetype i = 0; i < 3; ++i)
        oe.edit(have[i], update);
    oe.edit(have[3], [](OOB &s) { s.armies.clear(); });
    oe.edit(have[4], [](OOB &) {});
    oe.edit(none[0], update);
    QStringList noref;                            // … and one whose history names none
    for (const QString &t : none)
        if (!oe.file_of(t).second)
            noref << t;
    say(u"noref "_s + v(noref.mid(0, 3)));
    if (!noref.isEmpty())
        oe.edit(noref[0], update);
    say(u"dirty "_s + v(oe.dirty()));
    oe.restore(oe.snapshot());
    say(u"save "_s + rels(oe.save(backups), root));
    const OptStr ref = oe.file_of(none[0]).second;
    say(u"after %1 %2 %3"_s.arg(ref ? *ref : u"None"_s, oe.exists(none[0]) ? u"True"_s : u"False"_s, v(oe.dirty())));
    oe.edit(none[0], [](OOB &s) { s.navies.clear(); });
    say(u"save 2 "_s + rels(oe.save(backups), root));
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
