// G2 for moddata, the C++ side: the same lines as tools/golden_g2.py, from the C++ Mod.
// Usage: med_g2_moddata <work folder> <out folder>
#include "nopopup.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace moddata;

// ---- one value in the shared form (see golden_g2.py: v())
[[maybe_unused]] static QString v(std::nullptr_t) { return u"None"_s; }
static QString v(bool b) { return b ? u"True"_s : u"False"_s; }
[[maybe_unused]] static QString v(int i) { return QString::number(i); }
static QString v(long long i) { return QString::number(i); }
static QString v(double d) { return py::repr_float(d); }
static QString v(const QString &s) { return py::repr_str(s); }
[[maybe_unused]] static QString v(const char16_t *s) { return py::repr_str(QString::fromUtf16(s)); }
template <typename T>
static QString v(const std::optional<T> &o) { return o ? v(*o) : u"None"_s; }
static QString v(const Rgb &c) { return u"[%1, %2, %3]"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c)); }
static QString v(const QStringList &l)
{
    QStringList p;
    for (const QString &s : l)
        p << v(s);
    return u'[' + p.join(u", "_s) + u']';
}
template <typename T>
static QString v(const std::vector<T> &l)
{
    QStringList p;
    for (const T &x : l)
        p << v(x);
    return u'[' + p.join(u", "_s) + u']';
}
template <typename A, typename B>
static QString v(const std::pair<A, B> &x) { return u'[' + v(x.first) + u", "_s + v(x.second) + u']'; }
// a list made on the spot: L(a, b, c) -> "[a, b, c]"
template <typename... T>
static QString L(const T &...xs)
{
    QStringList p{v(xs)...};
    return u'[' + p.join(u", "_s) + u']';
}

static QStringList dump(const QString &work, const QString &shortn, const QString &rel, const QStringList &replace_in,
                        bool find_replace)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QString root = pypath::join(work, rel);
    QStringList replace = replace_in;
    if (find_replace) {
        auto [mods, _b] = discover_mods(pypath::join(BASE, u"mod"_s));
        const QString full = pypath::normcase(pypath::abspath(root));
        for (const ModEntry &m : mods)
            if (m.folder && pypath::normcase(pypath::abspath(*m.folder)) == full) {
                replace = m.replace;
                break;
            }
    }
    const OptStr base = shortn == u"vanilla" ? OptStr() : OptStr(BASE);
    Mod m(root, base, std::nullopt, replace);
    m.load();
    QStringList out;
    auto w = [&out](const QString &s) { out << s; };
    w(u"mod "_s + L(m.name(), m.root, m.base, m.replace));
    w(u"warnings "_s + v(m.warnings));
    QString lj;
    for (const auto &[k, val] : m.loc)
        lj += k + QChar(0x1f) + val + QChar(0x1e);
    w(u"loc %1 %2"_s.arg(m.loc.size()).arg(QString::fromLatin1(
        QCryptographicHash::hash(lj.toUtf8(), QCryptographicHash::Sha1).toHex())));
    std::vector<QString> lf;
    QStringList lfs;
    for (const auto &[p, own] : m.loc_files())
        lfs << L(p, own);
    w(u"loc_files ["_s + lfs.join(u", "_s) + u']');
    for (const auto &[pid, p] : m.provinces)
        w(u"prov "_s + L(pid, p.rgb, p.name, p.sea, p.region));
    QStringList fsl;
    for (const auto &[k, c] : m.free_slots)
        fsl << L(k, c);
    w(u"free_slots ["_s + fsl.join(u", "_s) + u']');
    w(u"provinces_bmp "_s + v(m.provinces_bmp) + u" region_hash "_s + v(m.region_hash));
    for (const auto &[rid, r] : m.regions)
        w(u"region "_s + L(rid, r.id, r.name, r.provinces));
    for (const auto &[rid, r] : m.utility)
        w(u"utility "_s + L(rid, r.id, r.name, r.provinces));
    w(u"region_order "_s + v(m.region_order));
    QStringList rk(m.region_keys_on_disk.begin(), m.region_keys_on_disk.end());
    std::sort(rk.begin(), rk.end());
    w(u"region_keys_on_disk "_s + v(rk));
    for (const auto &[k, c] : m.countries)
        w(u"country "_s + L(k, c.tag, c.name, c.color, c.dynamic, c.file));
    for (const auto &[k, c] : m.cultures)
        w(u"culture "_s + L(k, c.group, c.name, c.color));
    w(u"culture_groups "_s + v(m.culture_groups));
    for (const auto &[k, c] : m.religions)
        w(u"religion "_s + L(k, c.group, c.name, c.color, c.icon, c.pagan));
    w(u"religion_groups "_s + v(m.religion_groups));
    for (const auto &[k, c] : m.poptypes)
        w(u"poptype "_s + L(k, c.name, c.color, c.source));
    QStringList pts;
    for (const auto &kv : m.poptypes)
        pts << kv.first;
    std::stable_sort(pts.begin(), pts.end(),
                     [&](const QString &a, const QString &b) { return m.poptype_sort_key(a) < m.poptype_sort_key(b); });
    w(u"poptype_order "_s + v(pts));
    for (const auto &[pid, entries] : m.history) {
        QStringList e;
        for (const auto &[r, nodes] : entries)
            e << L(r, static_cast<long long>(nodes.size()));
        w(u"history ["_s + v(pid) + u", ["_s + e.join(u", "_s) + u"]]"_s);
    }
    QStringList bm;
    for (const auto &[k, b] : m.bookmarks)
        bm << L(k, b);
    w(u"bookmarks ["_s + bm.join(u", "_s) + u']');
    for (const auto &[k, b] : m.buildings)
        w(u"building "_s + L(k, b.type, b.max_level, b.one_per_state, b.port, b.production_type, b.name));
    for (const auto &[k, g] : m.goods)
        w(u"good "_s + L(k, g.group, g.name, g.color));
    w(u"goods_groups "_s + v(m.goods_groups));
    for (const auto &[k, p] : m.production)
        w(u"production "_s + L(k, p.type, p.template_, p.output, p.coastal, p.farm, p.mine));
    QStringList rg;
    for (const auto &[k, g] : m.rgo_goods())
        rg << L(k, g);
    w(u"rgo_goods ["_s + rg.join(u", "_s) + u']');
    w(u"factory_types "_s + v(m.factory_types()));
    for (const auto &[k, u] : m.unlocks())
        w(u"unlock "_s + L(k, u.year, u.cost, u.by, u.kind, u.missing));
    QStringList bkeys;
    for (const auto &kv : m.buildings)
        bkeys << kv.first;
    for (const QString &k : bkeys) {
        const FactoryInfo fi = m.factory_info(k);
        w(u"factory_info "_s + L(k, fi.output, fi.output_name, fi.group, fi.group_name, fi.group_order, fi.coastal));
    }
    QStringList us;
    for (const auto &x : m.utility_suggestions())
        us << L(x.first, x.second);
    w(u"utility_suggestions ["_s + us.join(u", "_s) + u']');
    const auto scs = m.scenarios();
    QStringList sl;
    OptStr sc;
    for (const ScenarioInfo &s : scs) {
        sl << L(s.name, s.files, s.isDate, s.bookmark);
        if (!sc && script::parse_date(s.name))
            sc = s.name;
    }
    w(u"scenarios ["_s + sl.join(u", "_s) + u']');
    if (sc) {
        m.apply_history(*sc);
        for (const auto &[pid, p] : m.provinces)
            w(u"hist "_s + L(pid, p.owner, p.controller, p.cores, p.life_rating, p.trade_goods, p.history_file));
        QStringList pf;
        for (const auto &[k, p] : m.pop_files(*sc))
            pf << L(k, p);
        w(u"pop_files ["_s + pf.join(u", "_s) + u']');
        const PopLoad pl = load_pop_files(m, *sc);
        w(u"pop_problems "_s + v(pl.problems));
        QStringList ph;
        for (const auto &[k, h] : pl.files)
            ph << L(k, h);
        w(u"pop_hashes ["_s + ph.join(u", "_s) + u']');
        for (const auto &[pid, e] : pl.provinces) {
            w(u"popprov "_s + L(pid, e.file, std::make_pair(static_cast<long long>(e.block.first),
                                                             static_cast<long long>(e.block.second)),
                                 static_cast<long long>(e.open), static_cast<long long>(e.close), e.files, e.line,
                                 e.other));
            for (const Pop &p : e.pops) {
                QStringList ex;
                for (const auto &[k, t] : p.extra)
                    ex << L(k, t);
                const auto sp = std::make_pair(static_cast<long long>(p.o->span.first),
                                               static_cast<long long>(p.o->span.second));
                QString ss = u"None"_s;
                if (p.o->size_span)
                    ss = L(static_cast<long long>(p.o->size_span->first), static_cast<long long>(p.o->size_span->second));
                w(u"pop ["_s + QStringList{v(p.type), v(p.culture), v(p.religion), v(p.size),
                                          u'[' + ex.join(u", "_s) + u']', v(sp), ss, v(p.o->line)}.join(u", "_s) + u']');
            }
        }
    }
    if (shortn == u"vanilla") {
        auto [mods, b] = discover_mods(pypath::join(BASE, u"mod"_s));
        for (const ModEntry &d : mods)
            w(u"discover "_s + L(d.name, d.folder, d.modfile, d.ok, d.note, d.replace, d.deps, d.user_dir));
        w(u"discover_base "_s + v(b));
    }
    return out;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 3)
        return 2;
    const QString work = QString::fromLocal8Bit(argv[1]), outdir = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(outdir);
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
        const QStringList lines = dump(work, md.shortn, md.rel, md.replace, md.find);
        QFile f(QDir(outdir).filePath(md.shortn + u".txt"_s));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        f.write((lines.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %7lld lines\n", qPrintable(md.shortn), static_cast<long long>(lines.size()));
    }
    return 0;
}
