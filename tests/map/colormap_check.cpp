// Map → Draw → Colours (C++ only, no Python twin): the colour map's maths and its file written back.
//   med_colormap_check <work> <out>
// 1. every colour map of the test mods read and written back unchanged: the same bytes where the format is kept
//    (plain pixels: the top picture byte for byte, the smaller copies made again), DXT / palette as plain 32-bit whose
//    picture reads back the same;
// 2. the distance transform against a brute-force one (squares of a stretched grid);
// 3. the tint: full strength without shading = the hue; keep shading = the hue lifted by each pixel's brightness;
// 4. a picture of a tint around a few vanilla provinces into <out> to look at.
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "core/moddata.h"
#include "map/colormap.h"
#include "map/mapsize.h"
#include "map/moreviews_data.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <cmath>
#include <cstdio>
#include <random>

using namespace Qt::StringLiterals;

static int fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
    if (!ok)
        ++fails;
}
static QByteArray read(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static std::uint32_t le32(const QByteArray &d, qsizetype at)
{
    return std::uint32_t(uchar(d[at])) | (std::uint32_t(uchar(d[at + 1])) << 8) | (std::uint32_t(uchar(d[at + 2])) << 16) |
           (std::uint32_t(uchar(d[at + 3])) << 24);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::printf("usage: med_colormap_check <work> <out>\n");
        return 2;
    }
    const QString work = QString::fromLocal8Bit(argv[1]), out = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out);

    // 1. written back unchanged
    const QStringList roots = {u"Victoria 2"_s, u"Ferrum_Mare 1.2.000"_s, u"Victoria 2/mod/CoRGI_puir 18.1"_s,
                               u"Victoria 2/mod/Modus Omnino Demens 1.6"_s};
    const QStringList names = {u"colormap.dds"_s, u"colormap_water.dds"_s, u"colormap_political.dds"_s,
                               u"colormap_no_shadow.dds"_s};
    for (const QString &r : roots)
        for (const QString &n : names) {
            const QString p = pypath::join(work, (r + u"/map/terrain/"_s + n).split(u'/'));
            const QByteArray d = read(p);
            if (d.isEmpty())
                continue;
            const auto pic = mapsize::decode_dds(p);
            if (!pic || pic->isNull()) {
                check(false, u"%1/%2 can't be read"_s.arg(r, n));
                continue;
            }
            QString why;
            const QByteArray b = mapsize::recolor_dds(p, pic->convertToFormat(QImage::Format_RGBA8888), &why);
            if (b.isEmpty()) {
                check(false, u"%1/%2 not written: %3"_s.arg(r, n, why));
                continue;
            }
            const std::uint32_t pf = le32(d, 80), bits = le32(d, 88);
            const long long w = le32(d, 16), h = le32(d, 12);
            const bool plain = !(pf & 4) && !(pf & 0x20) && !(pf & 0x20000) && (pf & 0x40) && bits >= 16 && bits <= 32 &&
                               bits % 8 == 0;
            const long long top = 128 + w * h * (bits / 8);
            const QString what = u"%1/%2 (%3 × %4, %5)"_s.arg(r, n).arg(w).arg(h).arg(
                plain ? u"%1 bit plain, %2 copies"_s.arg(bits).arg((le32(d, 8) & 0x20000) ? le32(d, 28) : 1)
                      : (pf & 4) ? u"DXT"_s : u"palette / grey"_s);
            if (plain) {
                check(b.left(top) == d.left(top), what + u": header + top picture the same bytes"_s);
                check(b.size() == d.size(), what + u": the same size with its smaller copies (%1 / %2)"_s.arg(b.size())
                                                     .arg(d.size()));
                long long diff = 0;
                for (qsizetype i = top; i < std::min(b.size(), d.size()); ++i)
                    diff += b[i] != d[i];
                std::printf("     smaller copies: %lld of %lld bytes differ from the file's own\n", diff,
                            (long long)(d.size() - top));
            } else if ((pf & 4) && !(le32(d, 8) & 0x20000)) {
                check(b == d, what + u": packed again, every block kept: the same bytes"_s);
            } else {
                const QString tmp = pypath::join(out, {u"back_"_s + n});
                QFile f(tmp);
                f.open(QIODevice::WriteOnly);
                f.write(b);
                f.close();
                const auto back = mapsize::decode_dds(tmp);
                check(back && back->convertToFormat(QImage::Format_RGBA8888) == pic->convertToFormat(QImage::Format_RGBA8888),
                      what + u": written as plain 32-bit, reads back the same picture"_s);
            }
        }

    // 2. distance against brute force (a stretched grid: 3 map px a column, 2.5 a row)
    {
        const colormap::Stretch s{60, 50, 20, 20};
        std::mt19937 rng(7);
        std::vector<std::uint8_t> m(400, 0);
        for (int k = 0; k < 6; ++k)
            m[rng() % 400] = 1;
        const auto d = colormap::distance(m, s);
        double worst = 0;
        for (long long y = 0; y < 20; ++y)
            for (long long x = 0; x < 20; ++x) {
                double best = 1e30;
                for (long long yy = 0; yy < 20; ++yy)
                    for (long long xx = 0; xx < 20; ++xx)
                        if (m[size_t(yy * 20 + xx)])
                            best = std::min(best, std::hypot((x - xx) * s.sx(), (y - yy) * s.sy()));
                worst = std::max(worst, std::fabs(best - d[size_t(y * 20 + x)]));
            }
        check(worst < 1e-3, u"distance transform = brute force (worst difference %1)"_s.arg(worst));
        const auto none = colormap::distance(std::vector<std::uint8_t>(400, 0), s);
        check(none[0] > 1e30f, u"no chosen pixel: every distance is huge"_s);
    }

    // 3. the tint
    {
        QImage pic(4, 1, QImage::Format_RGBA8888);
        const int vals[4][3] = {{10, 20, 30}, {100, 100, 100}, {200, 180, 160}, {50, 60, 70}};
        for (int x = 0; x < 4; ++x)
            pic.setPixelColor(x, 0, QColor(vals[x][0], vals[x][1], vals[x][2], 255 - x));
        colormap::Tint t;
        t.rgb = {120, 40, 200};
        t.strength = 1.0;
        t.keep_shading = false;
        const std::vector<float> w = {1.0f, 1.0f, 0.5f, 0.0f};
        QImage o = colormap::tinted(pic, w, t);
        check(o.pixelColor(0, 0) == QColor(120, 40, 200, 255) && o.pixelColor(1, 0) == QColor(120, 40, 200, 254),
              u"full weight, no shading: the hue itself, alpha kept"_s);
        check(o.pixelColor(2, 0) == QColor(160, 110, 180, 253), u"half weight: half way (%1)"_s.arg(o.pixelColor(2, 0).name()));
        check(o.pixelColor(3, 0) == QColor(50, 60, 70, 252), u"no weight: unchanged"_s);
        t.keep_shading = true;
        o = colormap::tinted(pic, std::vector<float>{1.0f, 1.0f, 0.0f, 0.0f}, t);
        const double l0 = colormap::luma(10, 20, 30), l1 = 100, ref = (l0 + l1) / 2;
        const QColor want0(int(std::lround(120 + l0 - ref)), int(std::lround(std::max(0.0, 40 + l0 - ref))),
                           int(std::lround(200 + l0 - ref)));
        check(o.pixelColor(0, 0).rgb() == want0.rgb(), u"keep shading: the hue lifted by the pixel's own brightness"_s);
        check(colormap::fade(0, 40) == 1.0f && colormap::fade(40, 40) == 0.0f && colormap::fade(20, 40) == 0.5f,
              u"fade: 1 at the provinces, ½ half way, 0 at the reach"_s);
    }

    // 4. a picture to look at: vanilla's colour map tinted around a stand-in "province" (a disc in the Balkans)
    {
        const QString p = pypath::join(work, {u"Victoria 2"_s, u"map"_s, u"terrain"_s, u"colormap.dds"_s});
        if (const auto pic = mapsize::decode_dds(p)) {
            const colormap::Stretch s{5616, 2160, pic->width(), pic->height()};
            grid::Grid<std::uint16_t> idmap(2160, 5616, 0);
            for (long long y = 0; y < 2160; ++y)
                for (long long x = 0; x < 5616; ++x)
                    idmap(y, x) = std::hypot(x - 3080.0, y - 640.0) < 60 ? 2 : 1;
            const auto mask = colormap::chosen_mask(idmap, {0, 0, 1}, s);
            const auto dist = colormap::distance(mask, s);
            std::vector<float> w(dist.size());
            for (size_t i = 0; i < w.size(); ++i)
                w[i] = colormap::fade(dist[i], 120);
            colormap::Tint t;
            t.rgb = {200, 60, 40};
            const QImage o = colormap::tinted(*pic, w, t);
            const QRect crop(int(2700 / s.sx()), int(s.ch - 1000 / s.sy()), int(800 / s.sx()), int(700 / s.sy()));
            o.copy(crop).mirrored(false, true).scaled(crop.width() * 3, crop.height() * 3).save(pypath::join(out, {u"tint_demo.png"_s}));
            pic->copy(crop).mirrored(false, true).scaled(crop.width() * 3, crop.height() * 3).save(
                pypath::join(out, {u"tint_before.png"_s}));
            std::printf("     pictures: %s\n", qPrintable(pypath::join(out, {u"tint_demo.png"_s})));
        }
    }
    // 5. Apply: a tint pending as an asset ("dds") written into a mod of its own, the game's file left as it was
    {
        const QString base = pypath::join(work, {u"Victoria 2"_s});
        const QString src = pypath::join(base, {u"map"_s, u"terrain"_s, u"colormap.dds"_s});
        const QByteArray before = read(src);
        const QString root = pypath::join(out, {u"applymod"_s});
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        moddata::Mod m(root, base, std::nullopt, {});
        moreviews::AssetEdits a(m);
        const auto pic = mapsize::decode_dds(src);
        std::vector<float> w(size_t(pic->width()) * size_t(pic->height()), 0.0f);
        for (size_t i = 0; i < w.size() / 3; ++i)
            w[i] = 1.0f;
        colormap::Tint t;
        const QImage tinted = colormap::tinted(*pic, w, t);
        moreviews::Asset as;
        as.how = u"dds"_s;
        as.img = tinted;
        as.path = src;
        a.fresh.insert(colormap::REL, as);
        a.save(pypath::join(out, {u"applybackups"_s}));
        const QString dst = pypath::join(root, {u"map"_s, u"terrain"_s, u"colormap.dds"_s});
        const QByteArray after = read(dst);
        const auto back = mapsize::decode_dds(dst);
        check(back && back->convertToFormat(QImage::Format_RGBA8888) == tinted, u"Apply: the mod's colormap.dds reads back as the tint"_s);
        check(after.size() == before.size() && after.left(128) == before.left(128),
              u"Apply: the game's format and header kept (%1 bytes)"_s.arg(after.size()));
        check(read(src) == before, u"Apply: the game's own file untouched"_s);
        check(a.fresh.isEmpty(), u"Apply: nothing left pending"_s);
    }
    std::printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
