// refs + popcaps against Python, the C++ side of tools/check_refs.py.  Usage: med_refs --dump <out folder> <work folder>
#include "nopopup.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/events.h"
#include "core/fs.h"
#include "core/popcaps.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace refs;
using pyform::L;
using pyform::v;

static const QString NEW_EVENT = u"\ncountry_event = {\n\tid = 990077\n\ttitle = zz_t\n\ttrigger = { has_country_flag = zz_newflag "
                                 "has_global_flag = zz_never }\n\toption = {\n\t\tname = zz_o\n\t\tset_country_flag = zz_newflag\n\t\t"
                                 "add_country_modifier = { name = zz_newmod duration = 10 }\n\t\tcountry_event = 990077\n\t}\n}\n"_s;
static const QString CAP_LINES = u"\npromote_to = {\n\tsoldiers = {\n\t\tmodifier = { factor = -100 soldiers = 0.05 }\n\t\tmodifier = { "
                                 "factor = -200 state_scope = { soldiers = 0.1 } }\n\t}\n\tclergymen = {\n\t\tgroup = { modifier = { "
                                 "factor = -100 clergymen = 0.02 } }\n\t}\n}\n"_s;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString ref_v(const Ref &r)
{
    return pyform::list({v(static_cast<long long>(r.pos)), v(static_cast<long long>(r.length)), v(r.kind), v(r.target),
                         v(r.role), v(r.ok), v(r.scope)});
}
static QString refs_v(const std::vector<Ref> &l)
{
    QStringList p;
    for (const Ref &r : l)
        p << ref_v(r);
    return pyform::list(p);
}
static QString use_v(const Use &u) { return L(u.rel, static_cast<long long>(u.pos), u.role); }
static QString sorted_v(const QSet<QString> &s, qsizetype n = -1)
{
    QStringList l(s.begin(), s.end());
    std::sort(l.begin(), l.end());
    return v(n >= 0 ? l.mid(0, n) : l);
}
static QString caps_v(const popcaps::Caps &cs)
{
    QStringList p;
    for (const auto &[t, e] : cs) {
        QStringList lines;
        for (const popcaps::CapLine &l : e.lines)
            lines << pyform::list({v(l.rel), v(l.line), v(l.src), v(l.scope), v(l.share), v(l.factor)});
        p << pyform::list({v(t), v(e.province), v(e.state), pyform::list(lines), v(e.open)});
    }
    return pyform::list(p);
}
static QString over_v(const std::vector<popcaps::Over> &ov, size_t n)
{
    QStringList p;
    for (size_t i = 0; i < ov.size() && i < n; ++i) {
        const popcaps::Over &o = ov[i];
        const QString where = std::holds_alternative<long long>(o.where) ? v(std::get<long long>(o.where))
                                                                         : v(std::get<QString>(o.where));
        p << pyform::list({v(o.type), v(o.scope), v(o.cap), where, v(o.have), v(o.total)});
    }
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
        m.nation_data = std::make_shared<countryhist::NationData>(m);
        events::EventDB db(m);
        auto text_of = [&](const QString &r) { return script::read_text(m.p(r.split(u'/'))); };
        const QString pd = m.p({u"poptypes"_s});
        QStringList pops;
        if (fs::isdir(pd))
            for (const QString &f : fs::listdir(pd))
                if (py::lower(f).endsWith(u".txt"))
                    pops << u"poptypes/"_s + f;
        std::sort(pops.begin(), pops.end());
        const QStringList extra = QStringList{REBEL_REL} + pops.mid(0, 4);
        RefIndex ri(m, db, text_of, extra);
        QStringList cbs;
        for (const auto &[a, b, k] : ri.cb_spans)
            cbs << L(static_cast<long long>(a), static_cast<long long>(b), k);
        QStringList out{u"sets "_s + pyform::list({sorted_v(ri.modifiers, 50), v(static_cast<long long>(ri.modifiers.size())),
                                                   sorted_v(ri.cbs), sorted_v(ri.rebels), pyform::list(cbs)})};
        QStringList rels;
        for (const auto &kv : ri.spans)
            rels << kv.first;
        for (qsizetype i = 0; i < rels.size(); ++i) {
            const auto &lst = *ri.spans.get(rels[i]);
            out << u"rel %1 %2 %3"_s.arg(v(rels[i])).arg(lst.size()).arg(h(refs_v(lst)));
            if (i % 5 == 0)
                for (size_t j = 0; j < lst.size() && j < 300; ++j)
                    out << u"  "_s + ref_v(lst[j]);
        }
        std::vector<ThingKey> keys;
        for (const auto &kv : ri.uses)
            keys.push_back(kv.first);
        for (const ThingKey &k : keys) {
            const auto &u = *ri.uses.get(k);
            const auto def = ri.definition(k.first, k.second);
            out << u"use %1 %2 %3 %4"_s.arg(L(k.first, k.second)).arg(u.size())
                       .arg(u.empty() ? u"None"_s : use_v(u[0]),
                            def ? L(def->first, static_cast<long long>(def->second)) : u"None"_s);
        }
        for (size_t i = 0, n = 0; i < keys.size() && n < 400; i += 11, ++n) {
            const ThingKey &k = keys[i];
            out << u"name %1 %2"_s.arg(L(k.first, k.second), v(ri.name_of(k.first, k.second)));
            out << u"  info "_s + v(ri.info_html(k.first, k.second));
            out << u"  lines "_s + v(ri.usage_lines(k.first, k.second, 3));
        }
        for (qsizetype i = 0; i < rels.size(); i += 7) {
            const QString &r = rels[i];
            const auto &lst = *ri.spans.get(r);
            const qsizetype n = !lst.empty() ? lst.back().pos + 10 : 50;
            for (qsizetype p : {qsizetype(0), n / 3, n / 2, !lst.empty() ? lst[0].pos : qsizetype(1),
                                !lst.empty() ? lst[0].pos + lst[0].length : qsizetype(2)}) {
                const Ref *a = ri.at(r, p);
                out << u"at %1 %2 %3"_s.arg(v(r)).arg(p).arg(a ? ref_v(*a) : u"None"_s);
            }
            out << u"rng %1 %2"_s.arg(v(r), h(refs_v(ri.in_range(r, n / 4, n / 2))));
        }
        // --- a file changed
        QStringList ev_rels;
        for (const QString &r : rels)
            if (r.startsWith(u"events/"))
                ev_rels << r;
        if (!ev_rels.isEmpty()) {
            const QString r0 = ev_rels[0];
            ri.update(r0, db.texts.value(r0) + NEW_EVENT);
            QString mt;
            try {
                mt = text_of(MODS_REL);
            } catch (const fs::OSError &) {
            }
            ri.update(MODS_REL, mt + u"\nzz_newmod = {\n\ticon = 1\n}\n"_s);
            for (const QString &r : {r0, MODS_REL}) {
                const auto &lst = *ri.spans.get(r);
                out << u"upd %1 %2 %3"_s.arg(v(r)).arg(lst.size()).arg(h(refs_v(lst)));
                for (const Ref &x : lst)
                    if (x.target.contains(u"zz_"_s) || x.target == u"990077")
                        out << u"  "_s + ref_v(x);
            }
            for (const ThingKey &k : std::vector<ThingKey>{{u"cflag"_s, u"zz_newflag"_s}, {u"gflag"_s, u"zz_never"_s},
                                                            {u"modifier"_s, u"zz_newmod"_s}, {u"event"_s, u"990077"_s},
                                                            {u"loc"_s, u"zz_t"_s}}) {
                QStringList us;
                for (const Use &u : ri.usages(k.first, k.second))
                    us << use_v(u);
                out << u"after %1 %2 %3"_s.arg(L(k.first, k.second), pyform::list(us), v(ri.info_html(k.first, k.second)));
            }
        }
        // --- popcaps
        QString sc;
        for (const QString &s : m.pop_scenarios())
            if (script::parse_date(s)) {
                sc = s;
                break;
            }
        document::Document d(m, sc);
        const popcaps::Caps caps = popcaps::read_caps(m);
        out << u"caps "_s + caps_v(caps) << u"over "_s + over_v(popcaps::over_cap(m, d, caps), 200);
        const QStringList changed = pops.mid(0, 2);
        auto tx = [&](const QString &r) {
            const QString t = script::read_text(m.p(r.split(u'/')));
            return changed.contains(r) ? t + CAP_LINES : t;
        };
        const popcaps::Caps caps2 = popcaps::read_caps(m, tx);
        out << u"caps2 "_s + caps_v(caps2);
        const auto ov = popcaps::over_cap(m, d, caps2);
        out << u"over2 %1 %2"_s.arg(ov.size()).arg(over_v(ov, 200));
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
