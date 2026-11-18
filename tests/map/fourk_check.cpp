// fourk against Python, the C++ side of tools/check_fourk.py.  Usage: med_fourk --dump <out> <work>  |  --run <side>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/fourk.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString r(const QString &s) { return py::repr_str(s); }
static QByteArray read(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static int dump(const QString &out_dir, const QString &work)
{
    using namespace fourk;
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
    for (const M &mo : mods) {
        const QString root = pypath::join(work, mo.rel);
        QStringList replace = mo.replace;
        if (mo.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mo.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        QStringList out;
        if (mo.shortn == u"vanilla") {
            QStringList t;
            for (const QString x : {u"0"_s, u"-0"_s, u"3"_s, u"1.5"_s, u"0.33333"_s, u"-12.125"_s, u".5"_s, u"+7"_s,
                                    u"1.00005"_s, u"250.12345"_s})
                t << times(x);
            out << u"times "_s + v(t);
        }
        const Plan j = plan(m);
        auto rel_of = [&](const QString &p) { return pypath::relpath(p, work).replace(u'\\', u'/'); };
        QStringList parts;
        for (const auto &[rl, f] : j.scripts)
            parts << u"(%1, %2)"_s.arg(r(rl), r(rel_of(f)));
        out << u"scripts %1 %2"_s.arg(j.scripts.size()).arg(h((u'[' + parts.join(u", "_s) + u']').toUtf8()));
        parts.clear();
        QStringList strips;
        for (const PicJob &p : j.pictures) {
            parts << u"(%1, %2, %3)"_s.arg(r(p.rel), r(rel_of(p.full))).arg(p.frames);
            if (p.frames != 1)
                strips << L(p.rel, p.frames);
        }
        out << u"pictures %1 %2"_s.arg(j.pictures.size()).arg(h((u'[' + parts.join(u", "_s) + u']').toUtf8()));
        out << u"  strips "_s + pyform::list(strips);
        QStringList fl;
        for (const auto &[rl, f, sh] : j.fonts) {
            QStringList ss;
            for (const auto &s : sh)
                ss << s.first;
            fl << L(rl, rel_of(f), ss);
        }
        out << u"fonts "_s + pyform::list(fl);
        QStringList cs;
        for (const auto &c : j.cursors)
            cs << c.first;
        out << u"cursors "_s + v(cs);
        out << u"missing "_s + v(j.missing);
        QStringList mn;
        for (const auto &[k, sv] : j.money)
            mn << L(k, rel_of(sv.first), sv.second);
        out << u"money "_s + pyform::list(mn);
        for (const auto &[rl, f] : j.scripts) {
            QByteArray raw = read(f);
            if (raw.startsWith("\xef\xbb\xbf"))
                raw = raw.mid(3);
            const QString text = QString::fromLatin1(raw);
            const Scaled sc = scale_script(text, rl.endsWith(u".gui"));
            QStringList fr;
            for (const auto &[t, s] : sc.frames) {
                QStringList ns;
                for (long long n : s)
                    ns << QString::number(n);
                fr << u"(%1, [%2])"_s.arg(r(t), ns.join(u", "_s));
            }
            out << u"  %1 %2 %3 frames %4 check %5"_s.arg(rl)
                       .arg(sc.count)
                       .arg(h(sc.text.toLatin1()), h((u'[' + fr.join(u", "_s) + u']').toUtf8()), v(check_script(text, sc.text)));
        }
        for (const auto &[rl, f, sh] : j.fonts) {
            const ScaledFnt sf = scale_fnt(QString::fromLatin1(read(f)));
            QStringList bs;
            for (const auto &b : sf.boxes)
                bs << u"[%1, %2, %3, %4]"_s.arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3]);
            out << u"  %1 %2 boxes %3 %4 sheet %5"_s.arg(rl, h(sf.text.toLatin1()))
                       .arg(sf.boxes.size())
                       .arg(h((u'[' + bs.join(u", "_s) + u']').toUtf8()), L(sf.sheet.first, sf.sheet.second));
        }
        QFile fh(QDir(out_dir).filePath(mo.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mo.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int run(const QString &side)
{
    moddata::Mod m(pypath::join(side, u"mod"_s), pypath::join(side, u"base"_s), std::nullopt, {});
    QStringList out;
    for (int k = 1; k <= 2; ++k) {
        const fourk::Report rep = fourk::build(m, pypath::join(side, u"out"_s));
        out << u"build %1 %2"_s.arg(k).arg(pyform::list(
                   {L(u"scripts"_s, rep.scripts), L(u"numbers"_s, rep.numbers), L(u"pictures"_s, rep.pictures),
                    L(u"fonts"_s, rep.fonts), L(u"cursors"_s, rep.cursors), L(u"bytes"_s, rep.bytes),
                    L(u"files"_s, rep.files)}));
        out << u"  megapixels "_s + py::fmt_f(rep.megapixels, 3);
        out << u"  notes "_s + v(rep.notes);
        out << u"  problems "_s + v(rep.problems);
        out << u"  missing "_s + v(rep.missing);
        out << u"  gone "_s + v(rep.gone);
        if (k == 1)
            fs::remove(pypath::join(side, {u"base"_s, u"gfx"_s, u"interface"_s, u"zz_test"_s, u"noise.bmp"_s}));
    }
    const QByteArray o = (out.join(u'\n') + u'\n').toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

// C++ only: one build with other sizes (the 4K screen's sliders), its notes and problems — for a look and a game test
static int sizes(const QStringList &a)
{
    moddata::Mod m(a[2], a[3] == u"-" ? std::nullopt : std::optional<QString>(a[3]), std::nullopt, {});
    fourk::Sizes s;
    s.topbar = a[5].toDouble();
    s.unitpanel = a[6].toDouble();
    s.marks = a[7].toDouble();
    s.names = a[8].toDouble();
    const fourk::Report r2 = fourk::build(m, a[4], nullptr, 0, s);
    std::printf("files %lld, pictures %lld, fonts %lld, scripts %lld\n", r2.files, r2.pictures, r2.fonts, r2.scripts);
    for (const QString &n : r2.notes)
        if (!n.contains(u"DXT1 with"_s))
            std::printf("note  %s\n", qPrintable(n));
    for (const QString &n : r2.problems)
        std::printf("PROBLEM %s\n", qPrintable(n));
    for (const QString &n : r2.gone)
        std::printf("gone  %s\n", qPrintable(n));
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 9 && a[1] == u"--sizes")       // --sizes <mod> <base|-> <out> topbar unitpanel marks names
            return sizes(a);
        if (a.size() >= 3 && a[1] == u"--read-sizes") {   // --read-sizes <a 4K submod's report file>
            QFile f(a[2]);
            if (!f.open(QIODevice::ReadOnly))
                return 3;
            const fourk::Sizes s = fourk::read_sizes(QString::fromUtf8(f.readAll()));
            std::printf("topbar %g unitpanel %g marks %g names %g\n", s.topbar, s.unitpanel, s.marks, s.names);
            return 0;
        }
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 3 && a[1] == u"--run")
            return run(a[2]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
