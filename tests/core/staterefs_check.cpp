// staterefs against Python, the C++ side of tools/check_staterefs.py.
// Usage: med_staterefs --dump <out folder> <work folder> <keys folder (written by the Python side)>
#include "nopopup.h"
#include "core/blockcode.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/staterefs.h"
#include "pyform.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <set>

using namespace Qt::StringLiterals;
using namespace staterefs;
using pyform::L;
using pyform::v;

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QStringList texts()
{
    return {u"TEX_1 = { add_core = USA }\n"_s,
            u"a = {\n\tTEX_1 = {\n\t\tx = { y = z }\n\t}\n}\n"_s,
            u"limit = { region = TEX_1 }"_s,
            u"region=TEX_1"_s,
            u"region = TEX_1 # TEX_1 = { }\n# TEX_1 = { x }\nTEX_1 = { \"}\" # }\n }"_s,
            u"TEX_1 = { unclosed"_s,
            u"XTEX_1 = { } TEX_10 = { } TEX_1_x = { } TEX_1"_s,
            u"\t  TEX_1\n\t=\n{ a = b }\nmore"_s,
            u"OR = { region = TEX_1 region = TEX_1 }"_s,
            QString(50, u'x') + u" region = TEX_1"_s,
            u"region ="_s + QString(45, u' ') + u"TEX_1"_s};
}
static QString ll_v(const std::vector<long long> &l)
{
    QStringList p;
    for (long long x : l)
        p << QString::number(x);
    return u'[' + p.join(u", "_s) + u']';
}

static int dump(const QString &out_dir, const QString &work, const QString &keys_dir)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList common;
    const QStringList T = texts();
    for (qsizetype i = 0; i < T.size(); ++i) {
        const QString &t = T[i];
        for (const std::vector<long long> &provs : {std::vector<long long>{}, {220}, {220, 221, 5}}) {
            const auto [nw, n] = fixed_text(t, u"TEX_1"_s, provs);
            common << u"fix %1 %2 %3 %4"_s.arg(QString::number(i), ll_v(provs), v(nw), QString::number(n));
        }
        const auto lo = _line_of(t, t.size() / 2);
        const auto be = t.contains(u'{') ? _block_end(t, t.indexOf(u'{')) : std::nullopt;
        common << u"line %1 %2 %3"_s.arg(QString::number(i), L(lo.first, lo.second),
                                        be ? QString::number(*be) : u"None"_s);
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
        scriptstore::ScriptStore store(m);
        QStringList out = common;
        long long nfiles = 0;
        for (const QString &f : FOLDERS)
            for (const QString &r : blockcode::files_of(m, &store, f))
                if (py::lower(r).endsWith(u".txt"))
                    ++nfiles;
        QFile kf(QDir(keys_dir).filePath(md.shortn + u".txt"_s));
        if (!kf.open(QIODevice::ReadOnly))
            return 4;
        const QString kt = QString::fromUtf8(kf.readAll());
        const QStringList keys = kt.isEmpty() ? QStringList() : kt.split(u'\n');
        out << u"files %1 keys %2"_s.arg(QString::number(nfiles), v(keys));
        for (const QString &key : keys) {
            const auto places = find(m, store, key);
            QStringList p;
            for (size_t i = 0; i < places.size() && i < 12; ++i)
                p << L(places[i].rel, places[i].pos, places[i].line, places[i].text, places[i].kind);
            out << u"key %1 %2 %3"_s.arg(key, QString::number(places.size()), pyform::list(p));
            QStringList rels;
            for (const Place &pl : places)
                if (!rels.contains(pl.rel))
                    rels << pl.rel;
            std::sort(rels.begin(), rels.end());
            for (const QString &r : rels.mid(0, 4)) {
                const auto [nw, n] = fixed_text(store.text(r), key, m.regions.value(key).provinces);
                out << u"  fixed %1 %2 %3 %4"_s.arg(r, h(nw), QString::number(n), QString::number(nw.size()));
            }
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
        if (a.size() >= 5 && a[1] == u"--dump")
            return dump(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
