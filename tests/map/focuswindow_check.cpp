// focuswindow against Python, the C++ side of tools/check_focuswindow.py.  Usage: med_focuswindow --dump <out> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/script.h"
#include "map/focuswindow.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
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
static QString xy(const focuswindow::XY &x) { return L(x.x, x.y, static_cast<long long>(x.s), static_cast<long long>(x.e)); }
static QString num(double x, bool is_int) { return is_int ? QString::number(static_cast<long long>(x)) : py::repr_float(x); }

class StubLook : public focuswindow::Look {
public:
    explicit StubLook(gamefont::Fonts *f) { fonts = f; }
    std::optional<QImage> sprite(const QString &) override { return std::nullopt; }
};

static int dump(const QString &out_dir, const QString &work)
{
    using namespace focuswindow;
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    std::vector<QPixmap> frames;
    for (int i = 0; i < 6; ++i) {
        QImage im(i ? 32 : 30, i ? 32 : 28, QImage::Format_ARGB32);
        im.fill(QColor(40 * i, 255 - 30 * i, (90 * i) % 256, 200 + i * 9));
        frames.push_back(QPixmap::fromImage(im));
    }
    std::vector<Row> rows;
    const int counts[] = {0, 1, 4, 9, 15, 30, 2, 7};
    for (int r = 0; r < 8; ++r) {
        Row row{u"g%1"_s.arg(r), u"Group number %1"_s.arg(r), {}};
        for (int c = 0; c < counts[r]; ++c)
            row.cells.emplace_back(u"f%1_%2"_s.arg(r).arg(c), (c * 3 + r) % 8);
        rows.push_back(row);
    }
    const auto words = [](const QString &k) -> OptStr {
        if (k == u"CLOSE")
            return u"Close it"_s;
        if (k == u"REMOVE_FOCUS")
            return u""_s;
        return std::nullopt;
    };
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
        const QString text = script::read_text(m.p(GUI.split(u'/')));
        QString bgp;
        for (const QString e : {u".dds"_s, u".tga"_s})
            if (fs::isfile(m.p({u"gfx"_s, u"interface"_s, u"province_natfocus_bg"_s + e}))) {
                bgp = m.p({u"gfx"_s, u"interface"_s, u"province_natfocus_bg"_s + e});
                break;
            }
        const QImage bg = fourk::to_qimage(fourk::read_picture(bgp).first);
        out << u"bg %1x%2 %3"_s.arg(bg.width()).arg(bg.height()).arg(img_hash(bg));
        const G g = *read(text);
        out << u"read "_s + pyform::list({L(u"start"_s).chopped(1) + u", "_s + xy(g.start) + u']',
                                         L(u"step"_s).chopped(1) + u", "_s + xy(g.step) + u']',
                                         L(u"icons"_s).chopped(1) + u", "_s + xy(g.icons) + u']',
                                         L(u"close"_s).chopped(1) + u", "_s + xy(g.close) + u']',
                                         L(u"cancel"_s).chopped(1) + u", "_s + xy(g.cancel) + u']'});
        const Details d = details(text);
        auto t3 = [](const std::tuple<QString, QString, QString> &t) { return L(std::get<0>(t), std::get<1>(t), std::get<2>(t)); };
        out << u"details "_s + pyform::list({L(u"name"_s, d.name), L(u"font"_s, d.font), L(u"name_w"_s, d.name_w),
                                            L(u"box"_s, d.box), L(u"item"_s, d.item), L(u"spacing"_s, d.spacing),
                                            L(u"close"_s).chopped(1) + u", "_s + t3(d.close) + u']',
                                            L(u"cancel"_s).chopped(1) + u", "_s + t3(d.cancel) + u']'});
        const auto [r0, s0, hd, ft] = drawn(g, bg);
        out << u"drawn "_s + L(r0, s0, hd, ft);
        auto span = [](const std::optional<Span> &s) {
            return s ? L(static_cast<long long>(s->first), static_cast<long long>(s->second)) : u"None"_s;
        };
        out << u"block %1 %2"_s.arg(span(_block(text, u"windowType"_s, u"national_focus_window"_s)),
                                    span(_block(text, u"windowType"_s, u"nope"_s)));
        for (long long rws : {1LL, 3LL, 5LL, 8LL, 12LL, 20LL})
            for (long long per : {3LL, 10LL, 25LL}) {
                const PlanOut pl = plan(g, bg, rws, per);
                out << u"plan %1 %2 %3"_s.arg(rws).arg(per).arg(pyform::list(
                           {L(u"step"_s, pl.step), L(u"width"_s, pl.width), L(u"extra"_s, pl.extra),
                            L(u"height"_s, pl.height), L(u"rows0"_s, pl.rows0), L(u"step0"_s, pl.step0),
                            L(u"fit0"_s, pl.fit0), L(u"head"_s, pl.head), L(u"foot"_s, pl.foot),
                            L(u"notes"_s).chopped(1) + u", "_s + v(pl.notes) + u']'}));
                if (per != 10 && rws != 1 && rws != 8 && rws != 20)
                    continue;
                const Fitted f = fit(text, bg, rws, per);
                out << u"  fit %1 %2x%3 %4"_s.arg(h(f.text->toUtf8())).arg(f.img.width()).arg(f.img.height()).arg(img_hash(f.img));
            }
        {
            const Fitted f = fit(u"windowType = { name = x }"_s, bg, 3, 3);
            out << u"fit odd "_s + L(f.text, f.why);
        }
        gamefont::Fonts fonts(m);
        StubLook stub(&fonts);
        const std::vector<std::tuple<Look *, OptStr, QStringList, int>> cases = {
            {nullptr, std::nullopt, {}, 4}, {&stub, u"f3_2"_s, {u"@g4"_s, u"f5_7"_s}, 8}, {&stub, std::nullopt, {}, 3}};
        for (const auto &[look, sel, multi, nrows] : cases) {
            const std::vector<Row> rs(rows.begin(), rows.begin() + nrows);
            const Drawn dr = picture(text, bg, rs, nrows != 3 ? frames : std::vector<QPixmap>{}, look, words, sel, multi);
            out << u"picture %1 %2x%3 %4"_s.arg(look ? u"True"_s : u"False"_s).arg(dr.img->width()).arg(dr.img->height()).arg(img_hash(*dr.img));
            QStringList hs;
            for (const Hit &x : dr.hits)
                hs << u"[%1, %2, %3, %4, %5]"_s.arg(num(x.x, x.x_int), num(x.y, true), num(x.w, x.w_int), num(x.h, true), v(x.key));
            out << u"  hits "_s + pyform::list(hs);
            out << u"  notes "_s + v(dr.notes);
        }
        {
            const Drawn dr = picture(u"x = 1"_s, bg, rows, frames, nullptr, words);
            out << u"picture odd "_s + (dr.img ? u"?"_s : u"[None, [], []]"_s);
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
