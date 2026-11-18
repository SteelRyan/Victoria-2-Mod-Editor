// cbcheck (+ py::difflib) against Python, the C++ side of tools/check_cbcheck.py.  Usage: med_cbcheck --dump <out> <work>
#include "nopopup.h"
#include "core/cbcheck.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pydifflib.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace cbcheck;
using pyform::L;
using pyform::v;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString val_v(const Val &x)
{
    if (std::holds_alternative<long long>(x))
        return v(std::get<long long>(x));
    if (std::holds_alternative<QString>(x))
        return v(std::get<QString>(x));
    return u"None"_s;
}
static QString f_v(const F &f)
{
    QString data = u"None"_s;
    if (f.data) {
        QStringList p;
        for (const Val &x : *f.data)
            p << val_v(x);
        data = pyform::list(p);
    }
    return pyform::list({v(f.text), v(f.rel), f.pos ? v(static_cast<long long>(*f.pos)) : u"None"_s, v(f.goal), data});
}
static QStringList rep_lines(const QString &tag, const Report &rep)
{
    QStringList out{u"%1 goals %2 frames %3"_s.arg(tag, v(rep.goals)).arg(rep.frames)};
    const std::vector<std::pair<QString, const std::vector<F> *>> parts = {
        {u"broken"_s, &rep.broken}, {u"lines"_s, &rep.lines},   {u"order"_s, &rep.order},     {u"texts"_s, &rep.texts},
        {u"no_mod"_s, &rep.no_mod}, {u"no_cb"_s, &rep.no_cb},   {u"brackets"_s, &rep.brackets},
        {u"equals"_s, &rep.equals}};
    for (const auto &[name, lst] : parts)
        for (const F &f : *lst)
            out << u"  %1 %2"_s.arg(name, f_v(f));
    return out;
}
static QStringList mangle(const QStringList &words)
{
    QStringList out;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString &w = words[i];
        if (w.size() > 3) {
            QString rev = w;
            std::reverse(rev.begin(), rev.end());
            out << w.left(w.size() - 1) << w.mid(1) << w.left(2) + w.mid(3) << w.left(3) + u'x' + w.mid(3) << py::upper(w)
                << w + u's' << rev.left(4) + w.mid(4);
        }
        if (i > 40)
            break;
    }
    return out;
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
        auto files = [&](const QStringList &folders) {
            QStringList out;
            for (const QString &folder : folders) {
                const QString d = m.p(folder.split(u'/'));
                if (!fs::isdir(d))
                    continue;
                QStringList names = fs::listdir(d);
                std::sort(names.begin(), names.end());
                for (const QString &fn : names)
                    if (py::lower(fn).endsWith(u".txt"))
                        out << folder + u'/' + fn;
            }
            return out;
        };
        const QStringList rels = files(SCAN_FOLDERS), brels = files(BRACKET_FOLDERS);
        QHash<QString, QString> disk;
        auto text_of = [&](const QString &r) -> QString {
            if (!disk.contains(r))
                disk.insert(r, script::read_text(m.p(r.split(u'/'))));
            return disk.value(r);
        };
        auto has_text = [&](const QString &k) { return m.loc.contains(k); };
        auto game_text = [&](const QString &k) -> OptStr {
            const QString *t = m.loc.get(k);
            return t ? OptStr(*t) : std::nullopt;
        };
        auto own = [&](const QString &r) { return m.in_mod(m.p(r.split(u'/'))); };
        QStringList out{u"rels %1 brels %2"_s.arg(rels.size()).arg(brels.size())};
        for (long long frames : {0LL, 12LL}) {
            Cache cache;
            out += rep_lines(u"as-is"_s, run(text_of, rels, has_text, frames, game_text, &cache, own, brels));
        }
        QString base;
        try {
            base = text_of(REL);
        } catch (const fs::OSError &) {
            base = QString();
        }
        std::vector<std::pair<QString, QString>> muts;
        if (!py::strip(base).isEmpty()) {
            const qsizetype i = base.lastIndexOf(u'}');
            muts.emplace_back(u"no-last"_s, py::head(base, i) + py::tail(base, i + 1));
            const qsizetype j = base.indexOf(u'}', base.size() * 2 / 5);
            muts.emplace_back(u"no-middle"_s, py::head(base, j) + py::tail(base, j + 1));
            const script::Nodes nodes = script::parse(base);
            const Node *g = nullptr, *po = nullptr;
            for (const Node &n : nodes) {
                if (!g && n.has_key && !n.key.isEmpty() && n.is_block && !KNOWN.contains(n.key))
                    g = &n;
                if (!po && n.has_key && n.key == u"peace_order" && n.is_block)
                    po = &n;
            }
            if (g) {
                muts.emplace_back(u"extra"_s, py::head(base, g->vend) + u"\n}\n"_s + py::tail(base, g->vend));
                muts.emplace_back(u"twice"_s, base + u'\n' + py::slice(base, g->kstart, g->vend) + u'\n');
                const qsizetype at = base.indexOf(u'{', g->kstart) + 1;
                muts.emplace_back(u"bad-lines"_s, py::head(base, at) +
                                                      u"\n\tzz_bad = yes\n\tbadboy_factr = 1\n\tmonths = 1.5\n\tcrisis = maybe\n"
                                                      "\tcan_use = yes\n\tsprite_index = 99\n\tpo_remove_cores = yes\n\tpeace_cost_factor = { }\n"_s +
                                                      py::tail(base, at));
                muts.emplace_back(u"doubled"_s, py::head(base, at) +
                                                    u"\n\tmonths = = 12 # a = = b\n\tdesc = \"x = = y\"\n\tzz =\n= 3\n"
                                                    "\tNOT = { poor_strata_life_needs = = 0.95 }\n"_s +
                                                    py::tail(base, at));
            }
            if (po) {
                muts.emplace_back(u"no-order"_s, py::head(base, po->kstart) + py::tail(base, po->vend));
                muts.emplace_back(u"order-junk"_s, py::head(base, po->vend - 1) + u" zz_gone zz_gone "_s +
                                                       (g ? g->key : u"x"_s) + u' ' + py::tail(base, po->vend - 1));
            }
        }
        for (const auto &[name, t] : muts) {
            auto tx = [&](const QString &r) { return r == REL ? t : text_of(r); };
            const Report rep = run(tx, rels, has_text, 12, game_text, nullptr, own, QStringList{REL} + brels.mid(0, 3));
            out += rep_lines(name, rep);
            std::vector<std::pair<QString, OptStr>> todo;
            for (const F &f : rep.order)
                if (f.data) {
                    const Val &k = (*f.data)[1], &n = (*f.data)[2];
                    todo.emplace_back(std::get<QString>(k),
                                      std::holds_alternative<QString>(n) ? OptStr(std::get<QString>(n)) : std::nullopt);
                }
            const auto cbr = check_brackets(REL, t);
            out << u"  fix %1 close %2 cbr %3"_s.arg(h(fix_order(t, todo)), h(close_brackets(t)), cbr ? f_v(*cbr) : u"None"_s);
            const auto ceq = check_equals(REL, t);
            out << u"  eq %1 %2"_s.arg(h(fix_equals(t)), ceq ? f_v(*ceq) : u"None"_s);
        }
        QString mt;
        try {
            mt = text_of(MAKE_REL);
        } catch (const fs::OSError &) {
        }
        out << u"make "_s + h(make_modifiers(mt, {u"zz_mod"_s, u"bad name"_s, u"zz_mod"_s, u"zz_two"_s})) + u' ' +
                   h(make_modifiers(mt.size() > 3 ? py::head(mt, -3) : mt, {u"zz_mod"_s}));
        QStringList known(KNOWN.begin(), KNOWN.end());
        std::sort(known.begin(), known.end());
        for (const QString &w : mangle(known)) {
            py::SequenceMatcher s(known[w.size() % known.size()], w);
            out << u"dl %1 %2 %3 %4 %5"_s.arg(v(w), v(py::get_close_matches(w, known)), v(py::get_close_matches(w, known, 2, 0.8)),
                                              v(s.ratio()), v(s.quick_ratio()));
        }
        QString long_a, long_b;
        for (int i = 0; i < 60; ++i)
            long_a += u"abcde"_s;
        long_a += py::head(base, 300);
        for (int i = 0; i < 55; ++i)
            long_b += u"abxde"_s;
        long_b += py::slice(base, 50, 400);
        py::SequenceMatcher s(long_a, long_b);
        out << u"long %1 %2 %3"_s.arg(v(s.ratio()), v(s.quick_ratio()), v(s.real_quick_ratio()));
        QString rb = long_b;
        std::reverse(rb.begin(), rb.end());
        out << u"longcm "_s + v(py::get_close_matches(long_b, {long_a, py::head(long_a, 250), rb}, 3, 0.1));
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
