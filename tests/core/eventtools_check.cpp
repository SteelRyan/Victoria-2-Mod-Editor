// eventtools against Python, the C++ side of tools/check_eventtools.py.  Usage: med_eventtools --dump <out> <work>
#include "nopopup.h"
#include "core/eventtools.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "pyform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace eventtools;
using pyform::L;
using pyform::v;

static const QStringList TEXTS = {
    u""_s, u"\n\n"_s, u"a = 1"_s, u"a = 1\n\n\n"_s, u"political_decisions = {\n}\n"_s, u"political_decisions = { x = { } }"_s,
    u"# only a note\n"_s, u"political_decisions = {\n\tfoo = {\n\t\tallow = { }\n\t}\n}\n\nother = { }\n"_s};
static const QStringList NAMES = {u""_s,    u"  "_s,          u"My events.txt"_s, u"a/b.txt"_s, u"x\\y.TXT"_s,
                                  u"..."_s, u".hidden"_s,     u"über ünd.txt"_s,  u"a.b.c"_s,   u" -_ "_s,
                                  u"Test #1!.txt"_s, u"name."_s, u"x.txt.bak"_s};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString places_v(const std::vector<Place> &l)
{
    QStringList p;
    for (const auto &[r, a, b] : l)
        p << L(r, a, b);
    return pyform::list(p);
}
template <typename P> static QString places2_v(const std::vector<P> &l)
{
    QStringList p;
    for (const auto &[r, a] : l)
        p << L(r, a);
    return pyform::list(p);
}
static QString ids_v(const Ids &ids)
{
    QStringList p;
    for (const auto &[k, l] : ids)
        p << pyform::list({v(k), places_v(l)});
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
        scriptstore::ScriptStore store(m);
        QStringList out;
        const Ids ids = event_ids(m, store);
        QStringList keys;
        for (const auto &kv : ids)
            keys << kv.first;
        out << u"ids %1 %2"_s.arg(ids.size()).arg(h(ids_v(ids)));
        {
            qsizetype i = 0;
            for (const auto &[k, l] : ids) {
                if (i++ >= 40)
                    break;
                out << u"  id %1 %2"_s.arg(v(k), places_v(l));
            }
        }
        const Ids again = event_ids(m, store);
        out << u"again "_s + v(h(ids_v(again)) == h(ids_v(ids)));
        const Ids dup = duplicate_ids(m, store);
        out << u"dup %1"_s.arg(dup.size());
        {
            qsizetype i = 0;
            for (const auto &[k, l] : dup) {
                if (i++ >= 60)
                    break;
                out << u"  dup %1 %2"_s.arg(v(k), places_v(l));
            }
        }
        const auto dk = decision_keys(m, store);
        {
            QStringList p;
            for (const auto &[k, l] : dk)
                p << pyform::list({v(k), places2_v(l)});
            out << u"dec %1 %2"_s.arg(dk.size()).arg(h(pyform::list(p)));
            qsizetype i = 0;
            for (const auto &[k, l] : dk) {
                if (i++ >= 30)
                    break;
                out << u"  dec %1 %2"_s.arg(v(k), places2_v(l));
            }
        }
        QStringList files;
        for (const auto &[k, l] : ids)
            for (const auto &[r, _p, _n] : l)
                if (!files.contains(r))
                    files << r;
        for (const QString &r : files.mid(0, 8)) {
            const QStringList of = ids_of_file(m, store, r);
            out << u"file %1 %2 %3 free %4 %5 %6"_s.arg(r).arg(of.size()).arg(v(of.mid(0, 10)), free_id(keys, of),
                                                                         free_id(keys, of, {free_id(keys, of)}),
                                                                         free_id(keys, of.mid(0, 1)));
        }
        out << u"free all %1 %2 %3"_s.arg(free_id(keys), free_id(keys, {}),
                                         free_id({}, {}, {u"100000"_s, u"100001"_s}));
        std::vector<std::tuple<QString, qsizetype, QString>> places;
        for (const auto &[k, l] : dup)
            for (size_t i = 1; i < l.size(); ++i)
                places.emplace_back(std::get<0>(l[i]), std::get<1>(l[i]), k);
        if (!files.isEmpty())
            places.emplace_back(files[0], 0, u"zz_not_there"_s);
        const auto [texts, done] = renumber(m, store, places);
        {
            QStringList p;
            for (size_t i = 0; i < done.size() && i < 40; ++i)
                p << L(std::get<0>(done[i]), std::get<1>(done[i]), std::get<2>(done[i]));
            out << u"renumber %1 %2"_s.arg(done.size()).arg(pyform::list(p));
            for (const auto &[r, t] : texts)
                out << u"  text %1 %2 %3"_s.arg(r, h(t)).arg(t.size());
        }
        for (const QString &kind : EVENT_KEYS)
            for (long long opts : {0LL, 1LL, 3LL, 9LL})
                for (bool trig : {false, true})
                    for (const QString &pic : {u""_s, u"pic_a"_s})
                        out << u"evt %1 %2 %3 %4 %5"_s.arg(kind).arg(opts).arg(v(trig), v(pic),
                                                                       h(event_text(kind, u"4711"_s, opts, trig, pic)));
        out << u"dl %1 %2"_s.arg(v(decision_lines()), v(decision_lines(u"p"_s)));
        const QString blk = event_text(u"country_event"_s, u"1"_s, 2);
        for (qsizetype i = 0; i < TEXTS.size(); ++i)
            out << u"add %1 %2 %3 %4"_s.arg(i).arg(h(add_event(TEXTS[i], blk)), h(add_decision(TEXTS[i], u"zz_dec"_s)),
                                                 h(add_decision(TEXTS[i], u"zz_dec"_s, u"pic"_s)));
        QStringList drels, all;
        for (const auto &[k, l] : dk)
            for (const auto &[r, _p] : l) {
                if (drels.isEmpty())
                    drels << r;
                if (!all.contains(r))
                    all << r;
            }
        std::sort(all.begin(), all.end());
        drels += all.mid(0, 6);
        for (const QString &r : drels) {
            const QString t = store.text(r);
            out << u"adddec %1 %2 %3"_s.arg(r, h(add_decision(t, u"zz_new"_s)), h(add_event(t, blk)));
        }
        {
            QStringList p;
            for (const QString &n : NAMES)
                p << v(safe_file(n));
            out << u"safe "_s + pyform::list(p);
            QStringList q;
            for (const QString &n : NAMES + QStringList{u"abc"_s, u"_x1"_s, u"1a"_s, u"a-b"_s})
                q << v(key_ok(n));
            out << u"key "_s + pyform::list(q);
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
    QGuiApplication app(argc, argv);              // (the editor's texts are QTextDocuments)
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
