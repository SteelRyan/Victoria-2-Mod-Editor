// popfacts + popcase + popsim against Python, the C++ side of tools/check_popsim.py.
// Usage: med_popsim --dump <out folder> <work folder>
#include "nopopup.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/popcase.h"
#include "core/popfacts.h"
#include "core/popsim.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "pyform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;

static const QStringList HEADS = {u"promotion_chance"_s,  u"demotion_chance"_s,   u"migration_chance"_s,
                                  u"colonialmigration_chance"_s, u"emigration_chance"_s, u"assimilation_chance"_s,
                                  u"conversion_chance"_s};
static const QStringList PER_TARGET = {u"promote_to"_s, u"ideologies"_s, u"issues"_s};
static const std::vector<double> NUMS = {0.0, -0.0004, 0.0004, 0.5, 1.0, 3.14159, 999.9996, 1000.0, 2500.5, 3500.5,
                                         -1234.4, 12.3456789};
static const std::vector<double> VALUES = {-1.0, 0.0, 0.001, 0.5, 1.0, 3.7, 250.0};
static const std::vector<long long> SIZES = {100000, 3, 1234567};
static const std::vector<double> SHOW = {0.0, 0.123, 0.005, 0.5, 1.0, 12.35, 0.25, 1234567.5, 2.5};
static const std::vector<std::vector<std::pair<QString, long long>>> SHARES = {
    {{u"a"_s, 3}, {u"b"_s, 1}, {u"c"_s, 1}},
    {{u"x"_s, 1}},
    {{u"a"_s, 1}, {u"b"_s, 1}, {u"c"_s, 1}},
    {{u"a"_s, 0}, {u"b"_s, 0}},
    {},
    {{u"a"_s, 7}, {u"b"_s, 7}, {u"c"_s, 7}, {u"d"_s, 7}, {u"e"_s, 7}, {u"f"_s, 7}},
    {{u"p"_s, 123456}, {u"q"_s, 2}, {u"r"_s, 99999}}};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
template <typename M> static QString items_v(const M &m, size_t limit = size_t(-1))
{
    QStringList p;
    size_t i = 0;
    for (const auto &[a, b] : m) {
        if (i++ >= limit)
            break;
        p << L(a, b);
    }
    return pyform::list(p);
}
static QString sorted_v(const QSet<QString> &s)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(l);
}

static QStringList sums()
{
    QStringList out;
    for (int n = 1; n <= 40; ++n) {
        std::vector<double> xs;
        for (int i = 0; i < n; ++i)
            xs.push_back(((i * 7919) % 1000) / 7.0 * std::pow(10.0, double((i % 7) - 3)) * (i % 3 == 0 ? -1 : 1));
        out << u"sum %1 %2"_s.arg(QString::number(n), v(py::sum(xs)));
    }
    out << u"sum x %1 %2 %3"_s.arg(v(py::sum(std::vector<double>(10, 0.1))), v(py::sum(std::vector<double>{1e16, 1.0, -1e16})),
                                   v(py::sum(std::vector<double>{1e308, 1e308, -1e308})));
    const std::vector<std::vector<double>> DIST = {{0.1, 0.2, 0.3}, {0.7, 0.3}, {1 / 3.0, 1 / 3.0, 1 / 3.0},
                                                   std::vector<double>(10, 0.1), {5.0, 0.0, 3.0}, {0.3, 0.6, 0.1},
                                                   {1e-17, 1.0, 1e-17}, {0.1, 0.7, 0.2, 0.3, 0.4}, {-1.0, 0.0},
                                                   {2.675, 0.005, 1.115}};
    for (long long total : {6LL, 7LL, 10LL, 100LL, 1000LL, 999999LL})
        for (const auto &ws : DIST) {
            QStringList d;
            for (long long x : document::distribute(total, ws))
                d << QString::number(x);
            out << u"dist %1 %2 [%3]"_s.arg(QString::number(total), v(ws), d.join(u", "_s));
        }
    return out;
}

static QString tree_v(const popcase::Tree &t)
{
    if (t.t == u"and" || t.t == u"or" || t.t == u"nor") {
        QStringList k;
        for (const popcase::Tree &c : t.kids)
            k << tree_v(c);
        return u"[%1, [%2]]"_s.arg(v(t.t), k.join(u", "_s));
    }
    if (t.t == u"strata")
        return L(t.t, t.a);
    if (t.t == u"fact")
        return L(t.t, t.a, t.want);
    return L(t.t, t.a, t.num);
}

struct Ch {
    QString head, target;
    std::optional<QString> base;
    std::vector<std::pair<QString, QString>> mods;
};
static std::vector<Ch> chances(const QString &text)
{
    std::vector<Ch> out;
    auto one = [&](const script::Node &n, const QString &head, const QString &target) {
        Ch ch{head, target, std::nullopt, {}};
        auto mod = [&](const script::Node &m) {
            const script::Node *f = nullptr;
            for (const script::Node &x : m.kids)
                if (x.has_key && x.key == u"factor" && !x.is_block) {
                    f = &x;
                    break;
                }
            ch.mods.emplace_back(f ? py::strip(f->value) : QString(), py::slice(text, m.vstart + 1, m.vend - 1));
        };
        for (const script::Node &c : n.kids) {
            if (c.has_key && c.key == u"factor" && !c.is_block)
                ch.base = py::strip(c.value);
            else if (c.has_key && c.key == u"modifier" && c.is_block)
                mod(c);
            else if (c.has_key && c.key == u"group" && c.is_block)
                for (const script::Node &m : c.kids)
                    if (m.has_key && m.key == u"modifier" && m.is_block)
                        mod(m);
        }
        out.push_back(ch);
    };
    for (const script::Node &n : script::parse(text)) {
        if (!(n.has_key && !n.key.isEmpty() && n.is_block))
            continue;
        if (PER_TARGET.contains(n.key)) {
            for (const script::Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    one(c, n.key, c.key);
        } else if (HEADS.contains(n.key)) {
            one(n, n.key, QString());
        }
    }
    return out;
}

static QString case_v(bool ok, const popcase::Case &c)
{
    QStringList vals;
    for (const auto &[k, x] : c.vals)
        if (x != popcase::VARS.value(k).start)
            vals << L(k, x);
    return pyform::list({v(ok), sorted_v(c.facts), pyform::list(vals)});
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList common = sums();
    for (double x : NUMS)
        common << u"num %1 %2 %3"_s.arg(v(x), popfacts::num(x), popfacts::people(std::fabs(x) * 7.3));
    for (double x : {0.0, 0.05, 0.08, 0.5, 1.0, 9.99, 10.0, 12345.678})
        common << u"people %1 %2"_s.arg(v(x), popfacts::people(x));
    for (const auto &counts : SHARES) {
        OrderedMap<QString, long long> c;
        QStringList cv;
        for (const auto &[k, x] : counts) {
            c.insert(k, x);
            cv << L(k, x);
        }
        common << u"ws %1 %2"_s.arg(pyform::list(cv), items_v(popsim::whole_shares(c)));
    }
    for (double x : {0.0, 0.004, 0.5, 2.0})
        for (int m : {0, 1, 12, 300})
            common << u"decay %1 %2 %3 %4"_s.arg(v(x), QString::number(m), v(popsim::decay(20000, x, 0.004, m)),
                                                v(popsim::decay(5, -x, 300, m)));
    for (const auto &[k, _var] : popcase::VARS) {
        QStringList s;
        for (double x : SHOW)
            s << popcase::show(k, x);
        common << u"show %1 %2"_s.arg(k, v(s));
    }
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
        QStringList out = common;
        const popfacts::Defs defs = popfacts::defines(script::read_text(m.p({u"common"_s, u"defines.lua"_s})));
        out << u"defs %1 %2"_s.arg(QString::number(defs.size()), items_v(defs));
        QStringList heads = HEADS + PER_TARGET;
        heads << u"migration_target"_s << u"zz"_s;
        for (const QString &head : heads) {
            const auto r1 = popfacts::rule(head, defs), r2 = popfacts::rule(head, {});
            out << u"rule %1 %2 %3"_s.arg(head, L(r1.first, r1.second), L(r2.first, r2.second));
            for (double val : VALUES)
                for (long long size : SIZES)
                    out << u"  say %1 %2 %3 %4 %5 %6"_s.arg(
                        v(val), QString::number(size), v(popfacts::per_month(head, val, size, defs, 0.23)),
                        v(popfacts::say(head, val, size, defs, 0.23)), v(popfacts::say(head, val, size, defs, 1.0, true)),
                        v(popfacts::per_month(head, val, size, {}, 1.0)));
        }
        out << u"share "_s + v(popfacts::share(defs));
        QStringList types;
        for (const auto &kv : m.poptypes)
            types << kv.first;
        const QString pdir = m.p({u"poptypes"_s});
        QStringList files;
        if (fs::isdir(pdir))
            for (const QString &f : fs::listdir(pdir))
                if (py::lower(f).endsWith(u".txt"))
                    files << f;
        std::sort(files.begin(), files.end());
        long long n_cards = 0;
        popsim::Shares s03, s05;
        for (const QString &t : types) {
            s03.insert(t, 0.3);
            s05.insert(t, 0.5);
        }
        for (const QString &f : files) {
            for (const Ch &chn : chances(script::read_text(pypath::join(pdir, f)))) {
                popcase::Chance ch;
                ch.base = chn.base;
                for (const auto &[a, s] : chn.mods)
                    ch.mods.push_back({a, s});
                const popcase::Block b(ch, types);
                QStringList nums;
                for (const QString &strata : {u"poor"_s, u"middle"_s, u"rich"_s}) {
                    popcase::Case c;
                    const auto p = b.prepare(c, strata);
                    nums << pyform::list({v(p.first), v(static_cast<long long>(p.second.size())),
                                          v(b.number(c, strata, {}, &p)), v(b.number(c, strata, s03))});
                }
                out << u"block %1 %2 %3 %4 %5 %6 %7"_s.arg(f, chn.head, chn.target, v(chn.base), v(b.base),
                                                           QString::number(b.cards.size()), pyform::list(nums));
                for (const auto &[fac, src] : chn.mods) {
                    ++n_cards;
                    if (n_cards > 400)
                        continue;
                    const popcase::Tree tree = popcase::compile_cond(src, types);
                    const auto [vs, fs_, dyn] = popcase::parts(tree);
                    QStringList row = {tree_v(tree), sorted_v(vs), sorted_v(fs_), v(dyn), v(popcase::_num(fac))};
                    for (const QString &strata : {u"poor"_s, u"rich"_s}) {
                        popcase::Case c;
                        row << L(c.holds(tree, strata, {}), c.holds(tree, strata, s05));
                        bool ok = c.make(tree, true, strata, {});
                        row << case_v(ok, c);
                        ok = c.make(tree, false, strata, {});
                        row << case_v(ok, c);
                        row << L(popsim::class_ok(src, strata), popsim::class_ok(QString(src).replace(u"\n"_s, u", "_s), strata));
                    }
                    out << u"  card "_s + row.join(u' ');
                }
            }
        }
        out << u"cards %1"_s.arg(n_cards);
        {
            popcase::Case c;
            c.set_fact(u"nationalvalue = nv_order"_s, true);
            c.set_fact(u"nationalvalue = nv_liberty"_s, true);
            c.set_fact(u"war = yes"_s, true);
            c.set_fact(u"civilized = yes"_s, false);
            std::vector<double> bl;
            for (const auto &[k, _var] : popcase::VARS)
                bl.push_back(c.below(k, 0.5));
            out << u"facts %1 %2"_s.arg(sorted_v(c.facts), v(bl));
        }
        // --- the sample province and the run
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        const auto start_i = popsim::sample(&d, m, types);
        out << u"sample "_s + items_v(start_i);
        out << u"sample0 "_s + items_v(popsim::sample(nullptr, m, types.mid(0, 5)));
        OrderedMap<QString, double> start;
        for (const auto &[k, x] : start_i)
            start.insert(k, static_cast<double>(x));
        QHash<QString, QString> strata;
        const QStringList cls = {u"poor"_s, u"middle"_s, u"rich"_s};
        for (qsizetype i = 0; i < types.size(); ++i)
            strata.insert(types[i], cls[i % 3]);
        popsim::Numbers n;
        n.insert(u"promotion"_s, 1.5);
        n.insert(u"demotion"_s, QHash<QString, double>{{types[0], 2.0}, {types[1], 0.5}});
        n.insert(u"migration"_s, 0.3);
        n.insert(u"emigration"_s, 0.1);
        n.insert(u"colonial"_s, 0.0);
        QHash<QString, popsim::Weights> weights;
        for (qsizetype i = 0; i < types.size(); ++i) {
            popsim::Weights w;
            for (qsizetype j = 0; j < types.size(); ++j)
                w.insert(types[j], static_cast<double>(py::mod(i * 7 + j * 3, 5LL) - 1));
            weights.insert(types[i], w);
        }
        const popsim::NFn nfn = [&](const QString &t, const popsim::Shares &sh) {
            popsim::Numbers r;
            r.insert(u"promotion"_s, 1.0 + sh.value(types[0], 0.0) * 3);
            r.insert(u"demotion"_s, sh.value(t, 0.0));
            r.insert(u"migration"_s, QHash<QString, double>{{types[2], 0.7}});
            return r;
        };
        const popsim::WFn wfn = [&](const QString &, const popsim::Shares &sh) {
            popsim::Weights r;
            for (const QString &u : types)
                r.insert(u, sh.value(u, 0.0) * 10 - 0.5 + (u == types[3] ? 0.25 : 0.0));
            return r;
        };
        struct Way {
            QString name;
            popsim::NArg n;
            popsim::WArg w;
            long long every;
        };
        const std::vector<Way> ways = {{u"fixed"_s, n, weights, 1}, {u"live"_s, nfn, wfn, 1}, {u"live12"_s, nfn, wfn, 12},
                                       {u"half"_s, n, wfn, 5}};
        const std::vector<std::optional<std::pair<QString, double>>> focuses = {
            std::nullopt, std::pair<QString, double>(types[2], 0.15), std::pair<QString, double>(types[0], -5.0)};
        for (const auto &[months, _when] : popsim::TIMES)
            for (double eff : {0.23, 1.0})
                for (const auto &focus : focuses)
                    for (const Way &w : ways) {
                        const auto r = popsim::run(start, strata, static_cast<double>(months), w.n, w.w,
                                                   popfacts::defines(script::read_text(m.p({u"common"_s, u"defines.lua"_s}))),
                                                   eff, focus, w.every);
                        QStringList mv;
                        for (const auto &[k, x] : r.moved)
                            mv << pyform::list({L(k.first, k.second), v(x)});
                        out << u"run %1 %2 %3 %4 %5 %6 %7 %8 %9"_s.arg(
                            QString::number(months), v(eff), focus ? L(focus->first, focus->second) : u"None"_s, w.name,
                            h(items_v(r.after)), v(r.left), QString::number(mv.size()), h(pyform::list(mv)),
                            items_v(r.after, 4));
                    }
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
