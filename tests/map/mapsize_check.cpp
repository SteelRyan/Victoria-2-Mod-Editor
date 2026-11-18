// mapsize's data half against Python, the C++ side of tools/check_mapsize.py.
// Usage: med_mapsize --dump <out> <work>  |  --run <copy> <backups> <left> <right> <north> <south> <work>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/script.h"
#include "map/imgio.h"
#include "map/mapsize.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using mapview::MapData;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString img_h(const QImage &img)
{
    const QImage im = img.convertToFormat(QImage::Format_RGBA8888);
    QByteArray rows;
    for (int y = 0; y < im.height(); ++y)
        rows.append(reinterpret_cast<const char *>(im.constScanLine(y)), qsizetype(im.width()) * 4);
    return u"%1x%2 %3"_s.arg(im.width()).arg(im.height()).arg(h(rows));
}
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }

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
    bool first = true;
    for (const M &mm : mods) {
        const QString root = pypath::join(work, mm.rel);
        QStringList replace = mm.replace;
        if (mm.find) {
            auto [list, _b] = moddata::discover_mods(pypath::join(BASE, u"mod"_s));
            for (const auto &e : list)
                if (e.folder && pypath::normcase(pypath::abspath(*e.folder)) == pypath::normcase(pypath::abspath(root)))
                    replace = e.replace;
        }
        moddata::Mod m(root, mm.shortn == u"vanilla" ? std::nullopt : std::optional<QString>(BASE), std::nullopt, replace);
        m.load();
        MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
        QStringList out;
        if (first) {
            first = false;
            std::vector<std::optional<long long>> ts;
            for (long long w = 1000; w < 6200; w += 37)
                ts.push_back(mapsize::tile_side(w));
            out << u"tiles "_s + v(ts);
            std::vector<long long> ok;
            for (long long w = 1000; w < 6200; w += 8)
                if (mapsize::width_ok(w))
                    ok.push_back(w);
            out << u"ok "_s + v(ok);
            out << u"hcuts "_s + v(mapsize::allowed_height_cuts(600, 1200));
        }
        auto head = [](std::vector<long long> x, size_t n) {
            x.resize(std::min(n, x.size()));
            return x;
        };
        out << u"cuts "_s + v(head(mapsize::allowed_cuts(md.w), 40));
        out << u"cuts least "_s + v(mapsize::allowed_cuts(md.w, 3000));
        out << u"hcuts "_s + v(head(mapsize::allowed_height_cuts(md.h, md.w), 20));
        std::vector<std::uint8_t> sea(size_t(md.n), 0);
        for (const auto &[pid, pr] : m.provinces)
            if (pid < md.n && pr.sea)
                sea[size_t(pid)] = 1;
        std::vector<long long> cuts = head(mapsize::allowed_cuts(md.w), 3);
        if (cuts.empty())
            cuts = {8};
        for (const auto &f : mapsize::files_of(m)) {
            out << u"file %1 %2"_s.arg(f.name, tf(f.own));
            if (mapsize::BITMAPS.contains(f.name)) {
                const auto b = mapsize::read_bmp(f.path);
                out << u"  bmp %1 %2 %3 %4"_s.arg(b.off).arg(b.w).arg(b.h).arg(b.bpp);
                for (long long c : cuts)
                    out << u"  cut %1 %2 %3"_s.arg(c).arg(h(mapsize::cut_bmp(b.raw, b.off, b.w, b.h, b.bpp, c / 2, c - c / 2)),
                                                         h(mapsize::cut_bmp(b.raw, b.off, b.w, b.h, b.bpp, c, 0)));
                out << u"  rows %1 %2"_s.arg(h(mapsize::cut_bmp_rows(b.raw, b.off, b.w, b.h, b.bpp, 8, 16)),
                                             h(mapsize::cut_bmp_rows(b.raw, b.off, b.w, b.h, b.bpp, 0, 40)));
                if (b.bpp == 24)
                    out << u"  north "_s + tf(mapsize::stored_first_is_north(md, b.raw, b.off, b.w, b.h, b.bpp));
            } else if (f.name == u"positions.txt") {
                const QString text = script::read_text(f.path);
                for (size_t i = 0; i < cuts.size() && i < 2; ++i) {
                    const long long c = cuts[i];
                    const auto [gone, part] = mapsize::affected(md, c / 2, c - c / 2, 16, 24);
                    QStringList ps;
                    for (long long p : part)
                        ps << QString::number(p);
                    out << u"  affected %1 %2 %3 %4"_s.arg(gone.size()).arg(v(head(gone, 10))).arg(part.size())
                               .arg(h((u'[' + ps.join(u", "_s) + u']').toUtf8()));
                    const QString moved = mapsize::move_positions_y(mapsize::move_positions(text, c / 2, gone), 24);
                    const auto [pulled, fixed] = mapsize::pull_in_positions(moved, md, c / 2, c - c / 2, 16, 24);
                    out << u"  moved %1 pulled %2 fixed %3 %4"_s.arg(h(moved.toUtf8()), h(pulled.toUtf8())).arg(fixed.size())
                               .arg(v(head(fixed, 12)));
                }
            } else {
                const auto img = imgio::read_image(f.path);
                if (img && !img->isNull())
                    out << u"  image %1 north %2"_s.arg(img_h(*img), tf(mapsize::image_rows_north_first(*img, md, sea)));
            }
        }
        const auto mmf = mapsize::minimap_of(m);
        out << u"minimap "_s + (mmf ? pyform::L(mmf->name, mmf->own) : u"None"_s);
        if (mmf) {
            const auto img = imgio::read_image(mmf->path);
            if (img && !img->isNull()) {
                out << u"  north "_s + tf(mapsize::image_rows_north_first(*img, md, sea));
                const auto fit = mapsize::minimap_fit(md, sea, *img);
                out << u"  fit "_s + (fit ? u"%1 %2 %3 %4"_s.arg(fit->left).arg(fit->width).arg(py::repr_float(fit->best),
                                                                                         py::repr_float(fit->now))
                                          : u"None"_s);
                out << u"  cut "_s + img_h(mapsize::cut_minimap(*img, md.w, cuts[0] / 2, cuts[0] - cuts[0] / 2));
                out << u"  refit %1 %2"_s.arg(img_h(mapsize::refit_minimap(*img, md.w, -120, md.w + 300)),
                                              img_h(mapsize::refit_minimap(*img, md.w, 200, md.w - 500)));
            }
        }
        long long nb = 1;
        for (long long k : {5LL, 3LL, 1LL})
            if ((md.h - k) % 8) {
                nb = k;
                break;
            }
        for (const auto &bad : std::vector<std::array<long long, 4>>{{-1, 0, 0, 0}, {0, 0, 0, 0}, {3, 0, 0, 0}, {0, 0, nb, 0}}) {
            const QString bv = v(std::vector<long long>(bad.begin(), bad.end()));
            try {
                mapsize::apply_cut(m, md, bad[0], bad[1], QDir(out_dir).filePath(u"never"_s), true, true, bad[2], bad[3]);
                out << u"bad %1: no error"_s.arg(bv);
            } catch (const py::ValueError &e) {
                out << u"bad %1: %2"_s.arg(bv, QString::fromUtf8(e.what()));
            }
        }
        QFile fh(QDir(out_dir).filePath(mm.shortn + u".txt"_s));
        if (!fh.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 3;
        fh.write((out.join(u'\n') + u'\n').toUtf8());
        std::printf("%-12s %5lld lines\n", qPrintable(mm.shortn), static_cast<long long>(out.size()));
    }
    return 0;
}

static int run(const QStringList &a)
{
    const QString root = a[2], backups = a[3], work = a[8];
    const QStringList REPLACE = {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s};
    moddata::Mod m(root, pypath::join(work, u"Victoria 2"_s), std::nullopt, REPLACE);
    m.load();
    MapData md(m, m.p({u"map"_s, u"provinces.bmp"_s}));
    const auto [written, bdir0] = mapsize::apply_cut(m, md, a[4].toLongLong(), a[5].toLongLong(), backups, true, true,
                                                     a[6].toLongLong(), a[7].toLongLong());
    QStringList rel;
    for (const QString &p : written)
        rel << pypath::relpath(p, root).replace(u'\\', u'/');
    QString bdir = pypath::relpath(bdir0, backups).replace(u'\\', u'/');
    bdir.replace(QRegularExpression(u"\\d{8}_\\d{6}"_s), u"STAMP"_s);
    const QByteArray o = (u"written %1 backups %2\n"_s.arg(v(rel), v(bdir))).toUtf8();
    std::fwrite(o.constData(), 1, size_t(o.size()), stdout);
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
        if (a.size() >= 9 && a[1] == u"--run")
            return run(a);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
