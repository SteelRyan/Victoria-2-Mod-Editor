// gamefont against Python, the C++ side of tools/check_gamefont.py.  Usage: med_gamefont --dump <out> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/pypath.h"
#include "map/gamefont.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using OptStr = std::optional<QString>;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString img_hash(const QImage &img)
{
    return h(QByteArray(reinterpret_cast<const char *>(img.constBits()), qsizetype(img.sizeInBytes())));
}
static QString rgb(const gamefont::Rgb &c) { return L(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

static const QStringList TEXTS = {
    u""_s, u"Hello world"_s, u"§YYellow§! and §Rred§W white §gand grey§b black"_s,
    u"A long text that has to be broken into several lines because it is much wider than the box it goes in"_s,
    u"Line one\\nLine two\nLine three"_s, u"The flag of @ENG and @XXX and @eng here"_s,
    u"$COUNTRY$ owns $CAPITAL$ and $NOPE$"_s, u"  spaces   between    words  "_s,
    u"Ünïcödé — “quotes” € ¤ \u0081 x"_s, u"§"_s, u"ends with §"_s, u"a;b"_s, u"$"_s, u"back\\slash \\n"_s,
    u"§Q odd §"_s, QString(u"word"_s).repeated(30)};

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const OrderedMap<QString, QString> SAMPLES = {{u"COUNTRY"_s, u"Great Britain"_s}, {u"CAPITAL"_s, u"London"_s}};
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
        gamefont::Fonts fonts(m);
        fonts.load();
        QStringList out, tl;
        for (const auto &[k, e] : *fonts.table) {
            QStringList cs;
            for (const auto &[c, x] : e.codes)
                cs << L(c).chopped(1) + u", "_s + rgb(x) + u']';
            tl << L(k, e.fname).chopped(1) + u", "_s + rgb(e.rgb) + u", "_s + pyform::list(cs) + u']';
        }
        out << u"table "_s + pyform::list(tl);
        std::vector<OptStr> names;
        const qsizetype step = std::max<qsizetype>(1, fonts.table->size() / 12);
        qsizetype idx = 0;
        for (const auto &kv : *fonts.table)
            if (idx++ % step == 0)
                names.push_back(kv.first);
        names.push_back(u"arial12"_s);
        names.push_back(u"nope"_s);
        names.push_back(std::nullopt);
        for (const OptStr &name : names) {
            const auto got = fonts.get(name);
            if (!got) {
                out << u"font %1 None"_s.arg(v(name));
                continue;
            }
            gamefont::Font &f = *got->font;
            QStringList cr;
            for (const auto &[id, c] : f.chars) {
                QStringList cc;
                for (long long x : c)
                    cc << QString::number(x);
                cr << u"(%1, (%2))"_s.arg(id).arg(cc.join(u", "_s));
            }
            QStringList codes;
            for (const auto &kv : got->codes)
                codes << kv.first;
            std::sort(codes.begin(), codes.end());
            out << u"font %1 rgb %2 line %3 base %4 chars %5 %6 codes %7"_s.arg(v(name), rgb(got->rgb))
                       .arg(f.line)
                       .arg(f.base)
                       .arg(f.chars.size())
                       .arg(h((u'[' + cr.join(u", "_s) + u']').toUtf8()), v(codes));
            QStringList adv;
            for (QChar c : u"aW §€\u0081Ü"_s)
                adv << QString::number(f.advance(c));
            out << u"  adv "_s + pyform::list(adv);
            for (const gamefont::Rgb &col : {gamefont::Rgb{255, 255, 255}, gamefont::Rgb{255, 189, 0}, gamefont::Rgb{10, 200, 30}})
                out << u"  tinted %1 %2"_s.arg(rgb(col), img_hash(f.tinted(col)));
            for (const QString &t : TEXTS)
                for (const auto &[mw, mws] : std::vector<std::pair<double, QString>>{{0, u"0"_s}, {140, u"140"_s}, {300.5, u"300.5"_s}}) {
                    const auto lay = gamefont::layout(fonts, name, t, mw, SAMPLES);
                    if (!lay)
                        continue;
                    QStringList ls;
                    for (const auto &ln : lay->lines) {
                        QStringList ws;
                        for (const auto &w : ln)
                            ws << pyform::list({v(w.kind), w.kind != u"f" ? v(w.text) : L(static_cast<long long>(w.img.width()),
                                                                                           static_cast<long long>(w.img.height())),
                                                rgb(w.colour), QString::number(w.width)});
                        ls << pyform::list(ws);
                    }
                    out << u"  layout %1 %2 widest %3 %4"_s.arg(v(t.left(30)), mws).arg(lay->widest).arg(pyform::list(ls));
                }
            const std::vector<std::tuple<QString, bool, double, QString>> modes = {
                {u"left"_s, false, 0, u"0"_s}, {u"centre"_s, false, 0, u"0"_s}, {u"right"_s, true, 40, u"40"_s},
                {u"CENTER"_s, true, 17.5, u"17.5"_s}};
            for (const auto &[fmt, clip, mh, mhs] : modes) {
                QImage img(320, 160, QImage::Format_ARGB32_Premultiplied);
                img.fill(0);
                QStringList res;
                {
                    QPainter p(&img);
                    for (qsizetype i = 2; i < 7; ++i) {
                        const auto [a, b, c] = gamefont::draw(p, fonts, name, TEXTS[i], 3, 2, 300, mh, fmt, clip, SAMPLES);
                        res << L(a, b, c);
                    }
                }
                out << u"  draw %1 %2 %3 %4 %5"_s.arg(fmt, clip ? u"True"_s : u"False"_s, mhs, pyform::list(res), img_hash(img));
            }
        }
        for (const QString tag : {u"ENG"_s, u"fra"_s, u"USA"_s, u"XXX"_s})
            for (long long hh : {16LL, 5LL, 30LL}) {
                const auto &f = fonts.flag(tag, hh);
                out << u"flag %1 %2 %3"_s.arg(tag).arg(hh).arg(
                    f ? u"%1x%2 %3"_s.arg(f->width()).arg(f->height()).arg(img_hash(*f)) : u"None"_s);
            }
        if (mo.shortn == u"vanilla")
            for (const QString &t : TEXTS) {
                QStringList a, b;
                for (const auto &[s, x] : gamefont::check(t))
                    a << L(s, x);
                for (const auto &[s, x] : gamefont::check(t, {u"country"_s, u"Capital"_s}, {u"eng"_s, u"FRA"_s}))
                    b << L(s, x);
                out << u"check %1 %2"_s.arg(v(t), pyform::list(a));
                out << u"check known "_s + pyform::list(b);
            }
        QFile fh(QDir(out_dir).filePath(mo.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mo.shortn), static_cast<long long>(out.size()));
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
