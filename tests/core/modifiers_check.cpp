// modifiers against Python, the C++ side of tools/check_modifiers.py.  Usage: med_modifiers --dump <out> <work>
#include "nopopup.h"
#include "core/events.h"
#include "core/modifiers.h"
#include "core/pypath.h"
#include "pyform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace modifiers;
using pyform::L;
using pyform::v;

static const QStringList SAMPLES = {u"a"_s,  u"_x1"_s,  u"1a"_s,    u""_s,   u"ab-c"_s,                u"ab\n"_s, u"-1"_s,
                                    u"1.5"_s, u"-0.25"_s, u"1."_s, u".5"_s, u"١٢"_s, u"1e3"_s, u"  2  "_s};

static QString h(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
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
        ModifierDB db(m);
        QStringList out{u"path "_s + v(db.path), u"order "_s + v(db.order), u"known "_s + v(db.known_attrs),
                        u"text "_s + h(db.text)};
        for (const QString &name : db.order) {
            const ModEntry &e = *db.entries.get(name);
            const Attrs attrs = block_attrs(e.text);
            const QString first = attrs.empty() ? u"icon"_s : attrs.back().first;
            QStringList al;
            for (const auto &a : attrs)
                al << L(a.first, a.second);
            out << u"m %1 %2 %3 %4"_s.arg(v(name), L(static_cast<long long>(e.span.first), static_cast<long long>(e.span.second)),
                                          h(e.text), pyform::list(al));
            out << u"  ed "_s + L(h(set_attr(e.text, u"icon"_s, u"7"_s)), h(set_attr(e.text, u"zz_attr"_s, u"1.5"_s)),
                                  h(del_attr(e.text, first)), h(del_attr(e.text, u"zz_none"_s)));
        }
        events::EventDB edb(m);
        for (const auto &[name, lst] : db.used_by(edb)) {
            QStringList p;
            for (const auto &[e, how] : lst)
                p << L(e->uid, how);
            out << u"used %1 %2"_s.arg(v(name), pyform::list(p));
        }
        QStringList rules;
        for (const QString &s : SAMPLES)
            rules << L(name_ok(s), num_ok(s));
        out << u"rules "_s + pyform::list(rules) << u"new "_s + v(new_block(u"zz_new"_s));
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
