// (C++ only, 2026-11-08) colour maps: the cut in their own format (mapsize::cut_dds) and the fit of an uncut one to a
// cut map (colormaps_off / colormap_fit / fit_colormap) — the tester's WICLBHSQP map.
// Usage: med_colormapfit --cut <work> <out>      cut the game's own colour maps; sizes, formats, smaller copies, pixels
//        med_colormapfit --fit <mod> <work> <out> fit the mod's colour maps to its map; the result fits and lies right
#include "../core/nopopup.h"
#include "core/pypath.h"
#include "map/imgio.h"
#include "map/mapsize.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok)
        ++fails;
}
static std::uint32_t le32(const QByteArray &d, qsizetype at)
{
    return std::uint32_t(uchar(d[at])) | std::uint32_t(uchar(d[at + 1])) << 8 | std::uint32_t(uchar(d[at + 2])) << 16 |
           std::uint32_t(uchar(d[at + 3])) << 24;
}
static QByteArray read_all(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static bool save(const QString &p, const QByteArray &b)
{
    QFile f(p);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(b) == b.size();
}
static long long mip_bytes(long long w, long long h, std::uint32_t pf, std::uint32_t bits, std::uint32_t fourcc, long long mips)
{
    long long n = 0;
    for (long long i = 0; i < std::max(1LL, mips); ++i) {
        if (pf & 4)
            n += std::max(1LL, (w + 3) / 4) * std::max(1LL, (h + 3) / 4) * (fourcc == 0x31545844u ? 8 : 16);
        else
            n += w * h * (bits / 8);
        w = std::max(1LL, w / 2);
        h = std::max(1LL, h / 2);
    }
    return n;
}

static int cut(const QString &work, const QString &out)
{
    QDir().mkpath(out);
    const QString base = pypath::join(work, {u"Victoria 2"_s, u"map"_s, u"terrain"_s});
    for (const QString &c : mapsize::COLORMAPS) {
        const QString p = pypath::join(base, {c});
        if (!QFileInfo(p).isFile())
            continue;
        const QByteArray in = read_all(p);
        const long long h = le32(in, 12), w = le32(in, 16), mips = (le32(in, 8) & 0x20000) ? le32(in, 28) : 1;
        const std::uint32_t pf = le32(in, 80), fourcc = le32(in, 84), bits = le32(in, 88);
        std::printf("%s: %lldx%lld pf 0x%x bits %u mips %lld\n", qPrintable(c), w, h, pf, bits, mips);
        const long long a = w / 8, b = w / 16, ta = h / 16, tb = h / 32;   // columns off left / right, rows off top / bottom
        QString why;
        const QByteArray o = mapsize::cut_dds(p, a, b, ta, tb, &why);
        check(!o.isEmpty(), c + u" cut: "_s + (why.isEmpty() ? u"ok"_s : why));
        if (o.isEmpty())
            continue;
        const QString op = pypath::join(out, {c});
        save(op, o);
        const long long nh = le32(o, 12), nw = le32(o, 16), nm = (le32(o, 8) & 0x20000) ? le32(o, 28) : 1;
        const std::uint32_t npf = le32(o, 80), nbits = le32(o, 88), nfourcc = le32(o, 84);
        check(nw == w - a - b && nh == h - ta - tb, u"%1 size %2x%3"_s.arg(c).arg(nw).arg(nh));
        check(nm == mips || (mips > 1 && nm > 1), u"%1 smaller copies %2 (was %3)"_s.arg(c).arg(nm).arg(mips));
        const bool plain = !(pf & 4);
        if (plain)
            check(npf == pf && nbits == bits, u"%1 format kept (pf 0x%2 bits %3)"_s.arg(c).arg(npf, 0, 16).arg(nbits));
        const qsizetype pal = (npf & 0x20) ? 1024 : 0;
        check(o.size() == 128 + pal + mip_bytes(nw, nh, npf, nbits, nfourcc, nm),
              u"%1 bytes %2 = header + pictures"_s.arg(c).arg(o.size()));
        // the pixels: the cut picture is the original's middle (exactly for plain formats; DXT decodes both)
        const auto src = mapsize::decode_dds(p), dst = mapsize::decode_dds(op);
        check(src && dst && dst->width() == nw && dst->height() == nh, c + u" decodes"_s);
        if (src && dst) {
            const bool north_first = true;   // compare both ways: the rows that went off are top or bottom in the file
            long long same0 = 0, same1 = 0, tot = 0;
            for (int y = 0; y < nh; y += 7)
                for (int x = 0; x < nw; x += 5) {
                    const QRgb d = dst->pixel(x, y);
                    same0 += d == src->pixel(int(x + a), int(y + ta));
                    same1 += d == src->pixel(int(x + a), int(y + tb));
                    ++tot;
                }
            Q_UNUSED(north_first);
            const double best = double(std::max(same0, same1)) / double(tot);
            check(plain ? best == 1.0 : best > 0.6, u"%1 pixels: %2 of the samples equal the original's"_s.arg(c).arg(best));
        }
    }
    return fails ? 1 : 0;
}

static int fit(const QString &root, const QString &work, const QString &out)
{
    QDir().mkpath(out);
    moddata::Mod m(root, pypath::join(work, u"Victoria 2"_s), std::nullopt, {});
    m.load();
    mapview::MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
    std::vector<std::uint8_t> sea(size_t(md.n), 0);
    for (const auto &[pid, pr] : m.provinces)
        if (pid < md.n && pr.sea)
            sea[size_t(pid)] = 1;
    std::printf("map %lldx%lld\n", md.w, md.h);
    const auto off = mapsize::colormaps_off(m, md.w, md.h);
    for (const auto &c : off)
        std::printf("off: %s %lldx%lld stands for %.0f columns\n", qPrintable(c.rel), c.w, c.h, c.span);
    for (const auto &c : mapsize::colormaps_to_fit(m, md.idmap, sea))
        std::printf("to fit (Errors tab): %s match %.3f stretched, %.3f cut\n", qPrintable(c.rel), c.now, c.cut);
    check(!off.empty(), u"the uncut colour maps are found (%1)"_s.arg(off.size()));
    for (const auto &c : off) {
        const auto pic = mapsize::decode_dds(c.path);
        if (pic && qEnvironmentVariableIsSet("CMFIT_DUMP")) {     // (looking: the picture as read, the map's land)
            pic->save(pypath::join(out, {pypath::basename(c.rel) + u".png"_s}));
            QImage land(int(md.w), int(md.h), QImage::Format_Grayscale8);
            for (long long y = 0; y < md.h; ++y)
                for (long long x = 0; x < md.w; ++x) {
                    const std::uint16_t id = md.idmap(y, x);
                    land.scanLine(int(y))[x] = (id > 0 && !sea[id]) ? 255 : 0;
                }
            land.save(pypath::join(out, {u"land.png"_s}));
        }
        double now = -2;
        const auto f = pic ? mapsize::colormap_fit(md.idmap, sea, *pic, &now) : std::nullopt;
        if (f)
            std::printf("  %s: first column at map column %lld, match %.3f (stretched as now: %.3f)\n", qPrintable(c.rel),
                        f->first, f->second, now);
        QString why;
        const QByteArray o = mapsize::fit_colormap(c.path, md.idmap, sea, &why);
        check(!o.isEmpty(), c.rel + u" fitted: "_s + (why.isEmpty() ? u"ok"_s : why));
        if (o.isEmpty())
            continue;
        const QString op = pypath::join(out, {pypath::basename(c.rel)});
        save(op, o);
        const long long nh = le32(o, 12), nw = le32(o, 16);
        const double span = double(md.h) * double(nw) / double(nh);
        check(std::fabs(span - double(md.w)) / double(md.w) < 0.01,
              u"%1 now %2x%3 stands for %4 columns (map %5)"_s.arg(c.rel).arg(nw).arg(nh).arg(span, 0, 'f', 0).arg(md.w));
        const auto pic2 = mapsize::decode_dds(op);
        const auto f2 = pic2 ? mapsize::colormap_fit(md.idmap, sea, *pic2) : std::nullopt;
        {
            QString why2;
            const QByteArray again = mapsize::fit_colormap(op, md.idmap, sea, &why2);
            check(again.isEmpty() && why2.contains(u"already lies"_s), c.rel + u" fitted once more: refused — "_s + why2);
        }
        const double px = double(md.w) / double(nw);   // map columns a picture pixel stands for: 2 pixels' leeway
        check(f2 && double(std::llabs(f2->first)) <= std::ceil(2 * px) && f2->second >= (f ? f->second - 0.02 : 0.25),
              u"%1 fitted lies on the map: first column %2, match %3"_s.arg(c.rel).arg(f2 ? f2->first : -999)
                  .arg(f2 ? f2->second : 0.0, 0, 'f', 3));
    }
    return fails ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    int r = 2;
    try {
        if (a.size() >= 4 && a[1] == u"--cut")
            r = cut(a[2], a[3]);
        else if (a.size() >= 5 && a[1] == u"--fit")
            r = fit(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    std::printf("%s (%d failed)\n", r == 0 ? "ALL PASS" : "FAILURES", fails);
    return r;
}
