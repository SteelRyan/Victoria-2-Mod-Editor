// mapsize.py, the data half — line by line (numpy's float32 sums as numpy does them: grid::pairwise_sum_f).
#include "map/mapsize.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "grid/npmath.h"
#include "map/fourk.h"
#include "map/imgio.h"
#include "map/positions_geo.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRectF>
#include <algorithm>
#include <functional>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using pypath::join;

namespace mapsize {

const QStringList BITMAPS = {u"provinces.bmp"_s, u"terrain.bmp"_s, u"rivers.bmp"_s};
const QStringList COLORMAPS = {u"colormap.dds"_s, u"colormap_water.dds"_s, u"colormap_political.dds"_s,
                               u"colormap_no_shadow.dds"_s};   // (no_shadow: C++ only, 2026-11-08 — it was left uncut)
const QStringList MINIMAPS = {u"minimap.dds"_s, u"minimap.tga"_s};   // gfx/interface/…: the whole map in the corner
static const QRegularExpression X_RE(uR"((\bx\s*=\s*)(-?\d+(?:\.\d+)?))"_s, QRegularExpression::UseUnicodePropertiesOption);
static const QRegularExpression Y_RE(uR"((\by\s*=\s*)(-?\d+(?:\.\d+)?))"_s, QRegularExpression::UseUnicodePropertiesOption);
static const QRegularExpression PAIR_RE(uR"((\bx\s*=\s*)(-?\d+(?:\.\d+)?)(\s*\n?\s*y\s*=\s*)(-?\d+(?:\.\d+)?))"_s,
                                        QRegularExpression::UseUnicodePropertiesOption);

std::optional<long long> tile_side(long long width)
{
    // The side of the square tiles the game lays over a map this wide: the smallest divisor from TILE_FROM up.
    for (long long s = TILE_FROM; s < py::floordiv(width, 2LL) + 1; ++s)
        if (width % s == 0)
            return s;
    return std::nullopt;
}

bool width_ok(long long width)
{
    const auto s = tile_side(width);
    return py::mod(width, WIDTH_UNIT) == 0 && s && TILE_MIN <= *s && *s <= TILE_MAX;
}

bool height_ok(long long height, long long)
{
    // NOT yet tried in the game — a multiple of 8 like every width that loaded
    return py::mod(height, HEIGHT_UNIT) == 0 && height >= MIN_HEIGHT;
}

std::vector<long long> allowed_height_cuts(long long height, long long width)
{
    std::vector<long long> out;
    for (long long h = height - 1; h > MIN_HEIGHT - 1; --h)
        if (height_ok(h, width))
            out.push_back(height - h);
    return out;
}

std::vector<long long> allowed_cuts(long long width, std::optional<long long> least)
{
    const long long lo = least.value_or(MIN_WIDTH);
    std::vector<long long> out;
    for (long long w = width - WIDTH_UNIT; w > lo - 1; w -= WIDTH_UNIT)
        if (width_ok(w))
            out.push_back(width - w);
    return out;
}

std::vector<long long> allowed_grows(long long width)
{
    std::vector<long long> out;
    for (long long w = width + WIDTH_UNIT; w <= width * GROW_MAX; w += WIDTH_UNIT)
        if (width_ok(w))
            out.push_back(w - width);
    return out;
}

std::vector<long long> allowed_height_grows(long long height, long long width)
{
    std::vector<long long> out;
    for (long long h = height + 1; h <= height * GROW_MAX; ++h)
        if (height_ok(h, width))
            out.push_back(h - height);
    return out;
}

long long mirror_row(long long y, long long h)
{
    if (h <= 1)
        return 0;
    long long m = y % (2 * h);
    if (m < 0)
        m += 2 * h;
    return m < h ? m : 2 * h - 1 - m;
}

QImage clamp_copy(const QImage &img, long long x, long long y, long long w, long long h)
{
    // img's (x, y, w, h); a column outside the picture is its nearest edge column, a row outside it the rows inside
    // mirrored at that edge (a grown map's pictures; 2026-11-17, seen in the game: the north edge row repeated 80 times
    // drew the colour map out into grey streaks)
    const QImage src = img.convertToFormat(QImage::Format_ARGB32);
    QImage out(int(std::max(1LL, w)), int(std::max(1LL, h)), QImage::Format_ARGB32);
    const long long iw = src.width(), ih = src.height();
    for (long long j = 0; j < out.height(); ++j) {
        const auto *sr = reinterpret_cast<const QRgb *>(src.constScanLine(int(mirror_row(y + j, ih))));
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(int(j)));
        for (long long i = 0; i < out.width(); ++i)
            d[i] = sr[std::clamp(x + i, 0LL, iw - 1)];
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the work (no window)
static long long le(const QByteArray &b, int at, int n, bool sign = false)
{
    unsigned long long v = 0;
    for (int i = n - 1; i >= 0; --i)
        v = (v << 8) | static_cast<unsigned char>(b[at + i]);
    if (sign && n == 4)
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
    return static_cast<long long>(v);
}
static void put4(QByteArray &b, int at, long long v)
{
    const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
    for (int i = 0; i < 4; ++i)
        b[at + i] = char((u >> (8 * i)) & 0xFF);
}

Bmp read_bmp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(("cannot read " + path).toStdString());
    const QByteArray raw = f.readAll();
    if (!raw.startsWith("BM"))
        throw py::ValueError((pypath::basename(path) + u" is not a bitmap"_s).toStdString());
    const long long off = le(raw, 10, 4), w = le(raw, 18, 4, true), h = le(raw, 22, 4, true), bpp = le(raw, 28, 2);
    if (le(raw, 30, 4) != 0 || (bpp != 8 && bpp != 24 && bpp != 32))
        throw py::ValueError((pypath::basename(path) + u" is not an uncompressed 8 / 24 / 32-bit bitmap"_s).toStdString());
    return {raw, off, w, h, bpp};
}

QByteArray cut_bmp(const QByteArray &raw, long long off, long long w, long long h, long long bpp, long long left,
                   long long right)
{
    // The bitmap's bytes with `left` columns off its left end and `right` off its right end (its palette, its row
    // order and everything else stay as they are).
    const long long bpx = bpp / 8, rows = std::llabs(h), stride = (w * bpx + 3) & ~3LL;
    if (raw.size() < off + stride * rows)
        throw py::ValueError("buffer is smaller than requested size");
    const long long nw = w - left - right, nstride = (nw * bpx + 3) & ~3LL;
    QByteArray out(qsizetype(nstride * rows), '\0');
    for (long long r = 0; r < rows; ++r)
        std::memcpy(out.data() + r * nstride, raw.constData() + off + r * stride + left * bpx, size_t(nw * bpx));
    QByteArray head = raw.left(qsizetype(off));
    put4(head, 2, off + nstride * rows);
    put4(head, 18, nw);
    put4(head, 34, nstride * rows);
    return head + out;
}

QByteArray cut_bmp_rows(const QByteArray &raw, long long off, long long w, long long h, long long bpp, long long first,
                        long long last)
{
    // The bitmap's bytes with `first` of its STORED rows off the start and `last` off the end.
    const long long bpx = bpp / 8, rows = std::llabs(h), stride = (w * bpx + 3) & ~3LL;
    if (raw.size() < off + stride * rows)
        throw py::ValueError("buffer is smaller than requested size");
    const long long nh = rows - first - last;
    QByteArray head = raw.left(qsizetype(off));
    put4(head, 2, off + stride * nh);
    put4(head, 22, h > 0 ? nh : -nh);
    put4(head, 34, stride * nh);
    return head + raw.mid(qsizetype(off + first * stride), qsizetype(std::max(0LL, nh) * stride));
}

bool stored_first_is_north(const mapview::MapData &md, const QByteArray &raw, long long off, long long w, long long h,
                           long long bpp)
{
    return stored_first_is_north(md.idmap, raw, off, w, h, bpp);
}

bool stored_first_is_north(const grid::Grid<std::uint16_t> &idmap, const QByteArray &raw, long long off, long long w,
                           long long h, long long bpp)
{
    // Is the bitmap's first stored row the map's north edge (md.idmap's row 0)? Compared by where the provinces change.
    const long long bpx = bpp / 8, stride = (w * bpx + 3) & ~3LL, rows = std::llabs(h);
    auto changes = [&](long long r) {
        std::vector<std::uint8_t> out;
        const auto *p = reinterpret_cast<const unsigned char *>(raw.constData() + off + r * stride);
        auto val = [&](long long x) {
            const unsigned char *q = p + x * bpx;
            return std::int64_t(q[0]) * 65536 + (bpx > 1 ? std::int64_t(q[1]) * 256 : 0) + (bpx > 2 ? std::int64_t(q[2]) : 0);
        };
        for (long long x = 1; x < w; ++x)
            out.push_back(val(x) != val(x - 1));
        return out;
    };
    const auto a = changes(0), b = changes(rows - 1);
    long long na = 0, nb = 0;
    for (long long x = 1; x < w; ++x) {
        const bool top = idmap(0, x) != idmap(0, x - 1);
        na += bool(a[size_t(x - 1)]) == top;
        nb += bool(b[size_t(x - 1)]) == top;
    }
    return w > 1 && na >= nb;                       // (np.mean of the same length: the counts)
}

static QString shift_values(const QString &text, const QRegularExpression &re, long long by)
{
    // m.group(1) + ("%.*f" % (decimals, v - by) if "." in v else str(int(round(v - by))))
    QString out;
    qsizetype pos = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString v = m.captured(2);
        const double x = *py::to_float(v) - double(by);
        out += text.mid(pos, m.capturedStart() - pos) + m.captured(1) +
               (v.contains(u'.') ? py::fmt_f(x, int(v.size() - v.indexOf(u'.') - 1)) : QString::number(py::round_int(x)));
        pos = m.capturedEnd();
    }
    return out + text.mid(pos);
}

QString move_positions_y(const QString &text, long long south)
{
    // positions.txt after rows went off the SOUTH edge: every y counts from that edge, so it moves down.
    return south ? shift_values(text, Y_RE, south) : text;
}

std::pair<QString, std::vector<long long>> pull_in_positions(const QString &text, const mapview::MapData &md,
                                                             long long left, long long right, long long north,
                                                             long long south)
{
    return pull_in_positions(text, md.idmap, left, right, north, south);
}

// ------------------------------------------------------------------ colour maps cut in their own format (C++ only)
// 2026-11-08, the tester's map: map/terrain/colormap.dds (16-bit A1R5G5B5, 12 smaller copies) could not be read and was
// left uncut WITHOUT A WORD, colormap_no_shadow.dds (256 colours, a palette after the header) was not on the list, and
// the cut ones were written back as 32-bit pictures without their smaller copies. Now: the pixels are cut where they
// are, byte for byte, in the picture's own format; the smaller copies (mipmaps) are made again, as many as before.
static double median_of(std::vector<int> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? double(v[n / 2]) : (double(v[n / 2 - 1]) + double(v[n / 2])) / 2.0;
}

static std::uint32_t le32(const QByteArray &d, qsizetype at)
{
    return std::uint32_t(uchar(d[at])) | (std::uint32_t(uchar(d[at + 1])) << 8) | (std::uint32_t(uchar(d[at + 2])) << 16) |
           (std::uint32_t(uchar(d[at + 3])) << 24);
}
static void put32(QByteArray &d, qsizetype at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        d[at + i] = char((v >> (8 * i)) & 0xFF);
}

static QByteArray dds_plain(long long w, long long h, const QByteArray &level0, long long bpp, std::uint32_t pf_flags,
                            const std::uint32_t masks[4], const QByteArray &head0, const QByteArray &palette, long long mips)
{
    // a header like head0's for w × h, the palette (if any), level0 and `mips` - 1 smaller copies
    QByteArray head = head0;
    put32(head, 12, std::uint32_t(h));
    put32(head, 16, std::uint32_t(w));
    std::uint32_t flags = le32(head, 8);
    if (flags & 0x80000)                             // DDSD_LINEARSIZE
        put32(head, 20, std::uint32_t(w * h * bpp));
    else {
        flags |= 0x8;                                // DDSD_PITCH
        put32(head, 20, std::uint32_t(w * bpp));
    }
    long long most = 1;
    for (long long s = std::max(w, h); s > 1; s /= 2)
        ++most;
    mips = std::clamp<long long>(mips, 1, most);
    if (mips > 1) {
        flags |= 0x20000;                            // DDSD_MIPMAPCOUNT
        put32(head, 28, std::uint32_t(mips));
        put32(head, 108, le32(head, 108) | 0x400008); // DDSCAPS_MIPMAP | DDSCAPS_COMPLEX
    } else {
        flags &= ~0x20000u;
        put32(head, 28, 0);
        put32(head, 108, le32(head, 108) & ~0x400008u);
    }
    put32(head, 8, flags);
    QByteArray out = head + palette + level0;
    const bool paletted = !palette.isEmpty() || (pf_flags & 0x20);
    QByteArray cur = level0;
    long long cw = w, ch = h;
    auto px = [&](const QByteArray &d, long long x, long long y, long long rw) {
        std::uint32_t v = 0;
        for (long long k = 0; k < bpp; ++k)
            v |= std::uint32_t(uchar(d[(y * rw + x) * bpp + k])) << (8 * k);
        return v;
    };
    for (long long lv = 1; lv < mips; ++lv) {
        const long long nw = std::max(1LL, cw / 2), nh = std::max(1LL, ch / 2);
        QByteArray nx(nw * nh * bpp, '\0');
        for (long long y = 0; y < nh; ++y)
            for (long long x = 0; x < nw; ++x) {
                const long long x0 = std::min(2 * x, cw - 1), x1 = std::min(2 * x + 1, cw - 1);
                const long long y0 = std::min(2 * y, ch - 1), y1 = std::min(2 * y + 1, ch - 1);
                const std::uint32_t p[4] = {px(cur, x0, y0, cw), px(cur, x1, y0, cw), px(cur, x0, y1, cw), px(cur, x1, y1, cw)};
                std::uint32_t v = p[0];                  // (palette indices, unmasked bits: the first pixel's)
                if (!paletted) {
                    std::uint32_t covered = 0;
                    for (int m = 0; m < 4; ++m)
                        covered |= masks[m];
                    v = p[0] & ~covered;
                    for (int m = 0; m < 4; ++m) {
                        const std::uint32_t mk = masks[m];
                        if (!mk)
                            continue;
                        int sh = 0;
                        while (!((mk >> sh) & 1))
                            ++sh;
                        const std::uint32_t sum = ((p[0] & mk) >> sh) + ((p[1] & mk) >> sh) + ((p[2] & mk) >> sh) +
                                                  ((p[3] & mk) >> sh);
                        v |= (((sum + 2) / 4) << sh) & mk;
                    }
                }
                for (long long k = 0; k < bpp; ++k)
                    nx[(y * nw + x) * bpp + k] = char((v >> (8 * k)) & 0xFF);
            }
        out += nx;
        cur = nx;
        cw = nw;
        ch = nh;
    }
    return out;
}

QByteArray cut_dds(const QString &path, long long a, long long b, long long ta, long long tb, QString *why)
{
    // A .dds picture with a columns off its left end, b off its right, ta rows off its first rows, tb off its last.
    // Plain pixels (8 / 16 / 24 / 32 bit, a palette too) keep their format; DXT pictures come out as plain 32-bit.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *why = u"can't be read"_s;
        return {};
    }
    const QByteArray d = f.readAll();
    if (d.size() < 128 || !d.startsWith("DDS ")) {
        *why = u"is no .dds picture"_s;
        return {};
    }
    const long long h = le32(d, 12), w = le32(d, 16);
    const std::uint32_t flags = le32(d, 8), pf = le32(d, 80), bits = le32(d, 88);
    const long long mips = (flags & 0x20000) ? std::max<long long>(1, le32(d, 28)) : 1;
    const long long nw = w - a - b, nh = h - ta - tb;
    if (nw < 1 || nh < 1) {
        *why = u"would have no pixels left"_s;
        return {};
    }
    std::uint32_t masks[4] = {le32(d, 92), le32(d, 96), le32(d, 100), le32(d, 104)};
    if (!(pf & 4) && bits % 8 == 0 && bits >= 8 && bits <= 32) {
        const long long bpp = bits / 8;
        const qsizetype pal = (pf & 0x20) ? 1024 : 0;  // DDPF_PALETTEINDEXED8: 256 colours after the header
        if (d.size() < 128 + pal + w * h * bpp) {
            *why = u"is shorter than its size says"_s;
            return {};
        }
        QByteArray level0;
        level0.reserve(nw * nh * bpp);
        for (long long j = 0; j < nh; ++j) {          // (a negative amount grows it: edge columns repeated, rows mirrored)
            const char *row = d.constData() + 128 + pal + mirror_row(j + ta, h) * w * bpp;
            if (a >= 0 && b >= 0)
                level0.append(row + a * bpp, nw * bpp);
            else
                for (long long i = 0; i < nw; ++i)
                    level0.append(row + std::clamp(i + a, 0LL, w - 1) * bpp, bpp);
        }
        return dds_plain(nw, nh, level0, bpp, pf, masks, d.left(128), d.mid(128, pal), mips);
    }
    // compressed: read, cut, written as plain A8R8G8B8 (blue first) with as many smaller copies as before
    const auto img = imgio::read_dds(path);
    if (!img || img->isNull()) {
        *why = u"is in a format the editor can't read"_s;
        return {};
    }
    const QImage c = (std::min({a, b, ta, tb}) < 0 ? clamp_copy(*img, a, ta, nw, nh)
                                                   : img->copy(int(a), int(ta), int(nw), int(nh)))
                         .convertToFormat(QImage::Format_ARGB32);
    QByteArray level0;
    level0.reserve(nw * nh * 4);
    for (long long y = 0; y < nh; ++y)
        level0.append(reinterpret_cast<const char *>(c.constScanLine(int(y))), nw * 4);   // (ARGB32 = B G R A bytes)
    QByteArray head(128, '\0');
    head.replace(0, 4, "DDS ");
    put32(head, 4, 124);
    put32(head, 8, 0x1007);                          // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    put32(head, 76, 32);
    put32(head, 80, 0x41);                           // RGB | ALPHAPIXELS
    put32(head, 88, 32);
    const std::uint32_t m32[4] = {0xFF0000, 0xFF00, 0xFF, 0xFF000000};
    for (int i = 0; i < 4; ++i)
        put32(head, 92 + 4 * i, m32[i]);
    put32(head, 108, 0x1000);                        // DDSCAPS_TEXTURE
    return dds_plain(nw, nh, level0, 4, 0x41, m32, head, {}, mips);
}

static QByteArray plain32_head()
{
    // a plain A8R8G8B8 header (blue first in the file); dds_plain fills in the size and the smaller copies
    QByteArray head(128, '\0');
    head.replace(0, 4, "DDS ");
    put32(head, 4, 124);
    put32(head, 8, 0x1007);                          // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    put32(head, 76, 32);
    put32(head, 80, 0x41);                           // RGB | ALPHAPIXELS
    put32(head, 88, 32);
    const std::uint32_t m32[4] = {0xFF0000, 0xFF00, 0xFF, 0xFF000000};
    for (int i = 0; i < 4; ++i)
        put32(head, 92 + 4 * i, m32[i]);
    put32(head, 108, 0x1000);                        // DDSCAPS_TEXTURE
    return head;
}

QByteArray recolor_dds(const QString &path, const QImage &pic0, QString *why)
{
    // A new top picture (C++ only, Map → Draw → Colours, 2026-11-10) written in the format of the .dds at `path`: plain
    // 16 / 24 / 32-bit pixels by their own masks, with as many smaller copies as the file had (made again); a picture
    // with a palette, a grey one or a DXT one as plain 32-bit (a tint needs every colour), smaller copies kept in number.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *why = u"can't be read"_s;
        return {};
    }
    const QByteArray d = f.read(128);
    if (d.size() < 128 || !d.startsWith("DDS ")) {
        *why = u"is no .dds picture"_s;
        return {};
    }
    const long long h = le32(d, 12), w = le32(d, 16);
    if (pic0.width() != w || pic0.height() != h) {
        *why = u"is %1 × %2, the new picture %3 × %4"_s.arg(w).arg(h).arg(pic0.width()).arg(pic0.height());
        return {};
    }
    const QImage pic = pic0.convertToFormat(QImage::Format_RGBA8888);
    const std::uint32_t flags = le32(d, 8), pf = le32(d, 80), bits = le32(d, 88);
    const long long mips = (flags & 0x20000) ? std::max<long long>(1, le32(d, 28)) : 1;
    const std::uint32_t masks[4] = {le32(d, 92), le32(d, 96), le32(d, 100), le32(d, 104)};
    auto enc = [](int v, std::uint32_t m) -> std::uint32_t {
        if (!m)
            return 0;
        int sh = 0;
        while (!((m >> sh) & 1))
            ++sh;
        const std::uint32_t mx = m >> sh;
        return ((std::uint32_t(v) * mx + 127) / 255) << sh;
    };
    if (!(pf & 4) && !(pf & 0x20) && !(pf & 0x20000) && (pf & 0x40) && bits % 8 == 0 && bits >= 16 && bits <= 32 &&
        masks[0] && masks[1] && masks[2]) {
        const long long bpp = bits / 8;
        const bool alpha = (pf & 0x1) && masks[3];
        QByteArray level0(w * h * bpp, '\0');
        for (long long y = 0; y < h; ++y) {
            const uchar *p = pic.constScanLine(int(y));
            for (long long x = 0; x < w; ++x) {
                std::uint32_t v = enc(p[4 * x], masks[0]) | enc(p[4 * x + 1], masks[1]) | enc(p[4 * x + 2], masks[2]);
                if (alpha)
                    v |= enc(p[4 * x + 3], masks[3]);
                for (long long k = 0; k < bpp; ++k)
                    level0[(y * w + x) * bpp + k] = char((v >> (8 * k)) & 0xFF);
            }
        }
        return dds_plain(w, h, level0, bpp, pf, masks, d, {}, mips);
    }
    // DXT without smaller copies (Ferrum Mare's colour maps): packed again the same way; every 4 × 4 block whose
    // pixels didn't change keeps its bytes, so what wasn't tinted stays exactly as it was
    const QByteArray cc = d.mid(84, 4);
    if ((pf & 4) && mips == 1 && (cc == "DXT1" || cc == "DXT3" || cc == "DXT5")) {
        const QString pack = QString::fromLatin1(cc);
        QByteArray data = fourk::dxt_bytes(fourk::qimage_rgba(pic), pack);
        const qsizetype bs = cc == "DXT1" ? 8 : 16;
        f.seek(0);
        const QByteArray all = f.readAll();
        const auto old0 = decode_dds(path);
        const long long bw = (w + 3) / 4, bh = (h + 3) / 4;   // (an edge block holds fewer pixels)
        if (old0 && old0->size() == pic.size() && data.size() == bw * bh * bs && all.size() >= 128 + data.size()) {
            const QImage old = old0->convertToFormat(QImage::Format_RGBA8888);
            for (long long by = 0; by < bh; ++by)
                for (long long bx = 0; bx < bw; ++bx) {
                    bool same = true;
                    const size_t len = size_t(std::min<long long>(4, w - bx * 4) * 4);
                    for (long long k = 0; k < std::min<long long>(4, h - by * 4) && same; ++k)
                        same = !std::memcmp(pic.constScanLine(int(by * 4 + k)) + bx * 16,
                                            old.constScanLine(int(by * 4 + k)) + bx * 16, len);
                    if (same) {
                        const qsizetype at = (by * bw + bx) * bs;
                        data.replace(at, bs, all.constData() + 128 + at, bs);
                    }
                }
        }
        return d + data;
    }
    const QImage c = pic.convertToFormat(QImage::Format_ARGB32);
    QByteArray level0;
    level0.reserve(w * h * 4);
    for (long long y = 0; y < h; ++y)
        level0.append(reinterpret_cast<const char *>(c.constScanLine(int(y))), w * 4);   // (ARGB32 = B G R A bytes)
    const std::uint32_t m32[4] = {0xFF0000, 0xFF00, 0xFF, 0xFF000000};
    return dds_plain(w, h, level0, 4, 0x41, m32, plain32_head(), {}, mips);
}

std::optional<QImage> decode_dds(const QString &path)
{
    // The top picture of any .dds the cut handles: plain pixels by their masks, 256 colours by the palette, else DXT.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray d = f.readAll();
    if (d.size() < 128 || !d.startsWith("DDS "))
        return std::nullopt;
    const long long h = le32(d, 12), w = le32(d, 16);
    const std::uint32_t pf = le32(d, 80), bits = le32(d, 88);
    if ((pf & 4) || bits % 8 || bits < 8 || bits > 32)
        return imgio::read_dds(path);
    const long long bpp = bits / 8;
    const qsizetype pal = (pf & 0x20) ? 1024 : 0;
    if (d.size() < 128 + pal + w * h * bpp)
        return std::nullopt;
    const std::uint32_t masks[4] = {le32(d, 92), le32(d, 96), le32(d, 100), le32(d, 104)};
    auto chan = [](std::uint32_t v, std::uint32_t m) -> int {
        if (!m)
            return -1;
        int sh = 0;
        while (!((m >> sh) & 1))
            ++sh;
        const std::uint32_t mx = m >> sh;
        return int((((v & m) >> sh) * 255 + mx / 2) / mx);
    };
    const bool lum = pf & 0x20000;
    QImage img(int(w), int(h), QImage::Format_RGBA8888);
    const uchar *src = reinterpret_cast<const uchar *>(d.constData()) + 128 + pal;
    for (long long y = 0; y < h; ++y) {
        uchar *o = img.scanLine(int(y));
        for (long long x = 0; x < w; ++x) {
            std::uint32_t v = 0;
            for (long long k = 0; k < bpp; ++k)
                v |= std::uint32_t(src[(y * w + x) * bpp + k]) << (8 * k);
            int r, g, b, a = 255;
            if (pal) {                               // PALETTEENTRY: red, green, blue, flags
                const uchar *pe = reinterpret_cast<const uchar *>(d.constData()) + 128 + 4 * (v & 0xFF);
                r = pe[0], g = pe[1], b = pe[2];
            } else if (lum) {
                r = g = b = std::max(0, chan(v, masks[0]));
                if (const int al = chan(v, masks[3]); al >= 0)
                    a = al;
            } else {
                r = std::max(0, chan(v, masks[0]));
                g = std::max(0, chan(v, masks[1]));
                b = std::max(0, chan(v, masks[2]));
                if (const int al = chan(v, masks[3]); al >= 0)
                    a = al;
            }
            o[4 * x] = uchar(r), o[4 * x + 1] = uchar(g), o[4 * x + 2] = uchar(b), o[4 * x + 3] = uchar(a);
        }
    }
    return img;
}

static double drawn_share(const QImage &pic0, double med_out[3] = nullptr)
{
    // the share of (every 4th) pixel far from the picture's most common colour: what is drawn on it
    const QImage pic = pic0.convertToFormat(QImage::Format_RGBA8888);
    const long long st = 4, pw = pic.width() / st, ph = pic.height() / st;
    if (pw < 1 || ph < 1)
        return 0;
    std::vector<int> ch[3];
    for (long long y = 0; y < ph; ++y) {
        const uchar *p = pic.constScanLine(int(y * st));
        for (long long x = 0; x < pw; ++x)
            for (int k = 0; k < 3; ++k)
                ch[k].push_back(p[4 * x * st + k]);
    }
    const double med[3] = {median_of(ch[0]), median_of(ch[1]), median_of(ch[2])};
    if (med_out)
        std::copy(med, med + 3, med_out);
    long long n = 0;
    for (size_t i = 0; i < ch[0].size(); ++i)
        n += (std::fabs(ch[0][i] - med[0]) + std::fabs(ch[1][i] - med[1]) + std::fabs(ch[2][i] - med[2])) > 40;
    return double(n) / double(ch[0].size());
}

static bool one_colour(const QString &path)
{
    // a picture with (almost) nothing drawn on it fits any map: stretched, it stains nothing (Ferrum Mare's flat
    // political / no-shadow maps). Read once per file state.
    static std::map<QString, std::pair<QString, bool>> cache;
    const QFileInfo fi(path);
    const QString key = u"%1 %2"_s.arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
    if (const auto it = cache.find(path); it != cache.end() && it->second.first == key)
        return it->second.second;
    const auto pic = decode_dds(path);
    const bool flat = pic && !pic->isNull() && drawn_share(*pic) < 0.02;
    cache[path] = {key, flat};
    return flat;
}

std::vector<ColourMapOff> colormaps_off(const moddata::Mod &mod, long long map_w, long long map_h)
{
    // map/terrain colour maps whose shape stands for a map more than 10 % wider / narrower than this one
    std::vector<ColourMapOff> out;
    for (const QString &c : COLORMAPS) {
        const QString p = mod.p({u"map"_s, u"terrain"_s, c});
        QFile f(p);
        if (!fs::isfile(p) || !f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray head = f.read(128);
        if (head.size() < 128 || !head.startsWith("DDS "))
            continue;
        const long long h = le32(head, 12), w = le32(head, 16);
        if (h < 1 || w < 1)
            continue;
        const double span = double(map_h) * double(w) / double(h);
        if (std::fabs(span - double(map_w)) / double(map_w) > 0.10 && !one_colour(p))
            out.push_back({u"map/terrain/"_s + c, p, w, h, span});
    }
    return out;
}

std::vector<ColourMapOff> colormaps_to_fit(const moddata::Mod &mod, const grid::Grid<std::uint16_t> &idmap,
                                           const std::vector<std::uint8_t> &sea)
{
    // Of those, the ones a cut puts right: they match the map's land well once cut, and clearly better than as the game
    // stretches them now (the game's own Earth colour maps under an invented map match neither way: not listed).
    // Worked out once per file state and map size (a second the first time for a big picture).
    static std::map<QString, std::pair<QString, std::optional<std::pair<double, double>>>> cache;
    std::vector<ColourMapOff> out;
    for (ColourMapOff c : colormaps_off(mod, idmap.w(), idmap.h())) {
        const QFileInfo fi(c.path);
        const QString key = u"%1 %2 %3 %4"_s.arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(idmap.w()).arg(idmap.h());
        auto it = cache.find(c.path);
        if (it == cache.end() || it->second.first != key) {
            std::optional<std::pair<double, double>> r;
            if (const auto pic = decode_dds(c.path); pic && !pic->isNull()) {
                double now = -2;
                if (const auto f = colormap_fit(idmap, sea, *pic, &now))
                    r = std::make_pair(now, f->second);
            }
            it = cache.insert_or_assign(c.path, std::make_pair(key, r)).first;
        }
        const auto &r = it->second.second;
        if (r && r->second >= 0.25 && r->first < r->second - 0.02) {
            c.now = r->first;
            c.cut = r->second;
            out.push_back(c);
        }
    }
    return out;
}

std::optional<std::pair<long long, double>> colormap_fit(const grid::Grid<std::uint16_t> &idmap,
                                                         const std::vector<std::uint8_t> &sea, const QImage &pic0,
                                                         double *now)
{
    // Which map column the picture's first column stands for (it may start before the map: negative), found by laying
    // the map's land over what the picture draws (far from its most common colour). Rows: north first, both.
    const QImage pic = pic0.convertToFormat(QImage::Format_RGBA8888);
    const long long PW = pic.width(), PH = pic.height(), W = idmap.w(), H = idmap.h();
    if (PW < 16 || PH < 8)
        return std::nullopt;
    const long long st = 4, pw = PW / st, ph = PH / st;   // every 4th pixel each way: plenty for the shape
    std::vector<int> ch[3];
    for (long long y = 0; y < ph; ++y) {
        const uchar *p = pic.constScanLine(int(y * st));
        for (long long x = 0; x < pw; ++x)
            for (int k = 0; k < 3; ++k)
                ch[k].push_back(p[4 * x * st + k]);
    }
    const double med[3] = {median_of(ch[0]), median_of(ch[1]), median_of(ch[2])};
    std::vector<double> drawn(size_t(pw * ph));
    double dm = 0;
    for (size_t i = 0; i < drawn.size(); ++i) {
        drawn[i] = (std::fabs(ch[0][i] - med[0]) + std::fabs(ch[1][i] - med[1]) + std::fabs(ch[2][i] - med[2])) > 40 ? 1 : 0;
        dm += drawn[i];
    }
    dm /= double(drawn.size());
    if (dm < 0.02 || dm > 0.98)
        return std::nullopt;
    double dn = 0;
    for (double &v : drawn) {
        v -= dm;
        dn += v * v;
    }
    const double span = double(H) * double(PW) / double(PH);
    std::vector<long long> rows(static_cast<size_t>(ph));
    for (long long y = 0; y < ph; ++y)
        rows[size_t(y)] = std::min(H - 1, (long long)((double(y * st) + 0.5) / double(PH) * double(H)));
    bool flip = false;                               // the game's colour maps are stored south row first
    auto score = [&](long long left, double sp) {
        std::vector<double> s(drawn.size());
        double sm = 0;
        for (long long x = 0; x < pw; ++x) {
            const long long c = left + (long long)((double(x * st) + 0.5) / double(PW) * sp);
            for (long long y = 0; y < ph; ++y) {
                double v = 0;
                if (c >= 0 && c < W) {
                    const long long r = rows[size_t(y)];
                    const std::uint16_t id = idmap(flip ? H - 1 - r : r, c);
                    v = (id > 0 && !sea[id]) ? 1 : 0;
                }
                s[size_t(y * pw + x)] = v;
                sm += v;
            }
        }
        sm /= double(s.size());
        double num = 0, sn = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            const double a = s[i] - sm;
            num += a * drawn[i];
            sn += a * a;
        }
        return sn > 0 && dn > 0 ? num / std::sqrt(sn * dn) : -1.0;
    };
    const long long lo = std::min(0LL, -(long long)std::ceil(span - double(W))) - 8, hi = std::max(8LL, W - (long long)span + 8);
    long long best = 0;
    double bc = -2;
    bool best_flip = false;
    for (const bool fl : {true, false}) {
        flip = fl;
        for (long long left = lo; left <= hi; left += 8)
            if (const double c = score(left, span); c > bc) {
                bc = c;
                best = left;
                best_flip = fl;
            }
    }
    flip = best_flip;
    const long long b0 = best;
    for (long long left = b0 - 8; left <= b0 + 8; ++left)
        if (const double c = score(left, span); c > bc) {
            bc = c;
            best = left;
        }
    if (now)                                         // as the game draws it: stretched over the whole map
        *now = score(0, double(W));
    return std::make_pair(best, bc);
}

QByteArray fit_colormap(const QString &path, const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &sea,
                        QString *why)
{
    // the colour map cut to the columns of the map (in its own format), or empty and why not
    const auto pic = decode_dds(path);
    if (!pic || pic->isNull()) {
        *why = u"can't be read"_s;
        return {};
    }
    double now = -2;
    const auto fit = colormap_fit(idmap, sea, *pic, &now);
    if (!fit || fit->second < 0.25) {
        *why = u"doesn't match the map's land well enough to be placed (best match %1)"_s.arg(fit ? py::fmt_f(fit->second, 2) : u"none"_s);
        return {};
    }
    if (now >= fit->second - 0.02) {                 // painted for the stretch: cutting it would put it out of place
        *why = u"already lies on the map's land as the game stretches it (match %1; cut %2) — left as it is"_s.arg(
            py::fmt_f(now, 2), py::fmt_f(fit->second, 2));
        return {};
    }
    const long long PW = pic->width(), PH = pic->height(), W = idmap.w(), H = idmap.h();
    const double span = double(H) * double(PW) / double(PH);
    const long long a = py::round_int(double(-fit->first) / span * double(PW));
    const long long keep = py::round_int(double(W) / span * double(PW));
    const long long b = PW - a - keep;
    if (a < 0 || b < 0) {
        *why = u"shows less than the whole map (it starts %1 map columns %2) — it can't be cut to fit"_s.arg(
            std::llabs(fit->first)).arg(fit->first > 0 ? u"in"_s : u"out"_s);
        return {};
    }
    return cut_dds(path, a, b, 0, 0, why);
}

grid::Grid<std::uint16_t> ids_on_disk(const moddata::Mod &mod, bool distinct_unknown)
{
    // The province map as the files on disk say NOW (provinces.bmp + definition.csv), the way MapData reads it (rows
    // mirrored: row 0 = the north edge). (C++ only, 2026-11-08: a cut that first deletes provinces runs Apply, which
    // closes the numbers up — the window's MapData still has the OLD numbers, and positions.txt the new ones. The
    // tester's map: 11 renumbered provinces kept their places off the map, the game crashed at the campaign start.)
    QImage img(mod.p({u"map"_s, mod.provinces_bmp}));
    if (img.isNull())
        throw std::runtime_error("cannot read provinces.bmp");
    img = img.convertToFormat(QImage::Format_RGB888);
    std::vector<std::uint16_t> lut(1u << 24, 0);
    auto lead = [](const QString &s) -> std::optional<int> {
        static const QRegularExpression RE(u"^\\s*(\\d+)"_s);
        const auto m = RE.match(s);
        return m.hasMatch() ? std::optional<int>(m.captured(1).toInt()) : std::nullopt;
    };
    for (const QString &line : py::splitlines(script::read_text(mod.p({u"map"_s, u"definition.csv"_s})))) {
        const QStringList parts = line.split(u';');
        if (parts.size() < 4 || !py::isdigit(py::strip(parts[0])))
            continue;
        const auto r = lead(parts[1]), g = lead(parts[2]), b = lead(parts[3]);
        const long long pid = py::strip(parts[0]).toLongLong();
        if (!r || !g || !b || pid <= 0 || pid > 65535 || *r > 255 || *g > 255 || *b > 255)
            continue;
        if (parts.size() > 4 && py::strip(parts[4]) == moddata::FREE_NAME)
            continue;
        lut[(std::uint32_t(*r) << 16) | (std::uint32_t(*g) << 8) | std::uint32_t(*b)] = std::uint16_t(pid);
    }
    const long long w = img.width(), h = img.height();
    grid::Grid<std::uint16_t> ids(h, w);
    std::uint16_t next_free = 65534;
    for (long long y = 0; y < h; ++y) {
        const uchar *row = img.constScanLine(int(h - 1 - y));
        std::uint16_t *out = ids.row(y);
        for (long long x = 0; x < w; ++x) {
            const uchar *q = row + x * 3;
            const std::uint32_t c = (std::uint32_t(q[0]) << 16) | (std::uint32_t(q[1]) << 8) | q[2];
            if (!lut[c] && distinct_unknown && c && next_free > 4096)
                lut[c] = next_free--;
            out[x] = lut[c];
        }
    }
    return ids;
}

QString keep_inside(const QString &text, long long w, long long h, long long *moved)
{
    // Last line of defence: every x / y pair still outside a w × h map is put on its nearest edge pixel's middle.
    // (A place off the map is what the game crashed on — 2026-11-08, see ids_on_disk.)
    QString out;
    qsizetype pos = 0;
    long long n = 0;
    for (auto it = PAIR_RE.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const double x = *py::to_float(m.captured(2)), y = *py::to_float(m.captured(4));
        if (0 <= x && x < double(w) && 0 <= y && y < double(h))
            continue;
        const double cx = std::clamp(x, 0.5, double(w) - 0.5), cy = std::clamp(y, 0.5, double(h) - 0.5);
        out += text.mid(pos, m.capturedStart() - pos) + m.captured(1) + py::fmt_f(cx, 3) + m.captured(3) + py::fmt_f(cy, 3);
        pos = m.capturedEnd();
        ++n;
    }
    if (moved)
        *moved = n;
    return n ? out + text.mid(pos) : text;
}

std::pair<QString, std::vector<long long>> pull_in_positions(const QString &text, const grid::Grid<std::uint16_t> &idmap,
                                                             long long left, long long right, long long north,
                                                             long long south)
{
    // A place that now lies outside the map is put on the middle of what its province keeps.
    struct { const grid::Grid<std::uint16_t> &idmap; long long w, h; } md{idmap, idmap.w(), idmap.h()};
    const long long nw = md.w - left - right, nh = md.h - north - south;
    std::vector<const script::Node *> blocks;
    const script::Nodes nodes = script::parse(text);
    for (const script::Node &n : nodes)
        if (n.has_key && !n.key.isEmpty() && py::isdigit(n.key) && n.is_block)
            blocks.push_back(&n);
    std::stable_sort(blocks.begin(), blocks.end(), [](const auto *a, const auto *b) { return -a->kstart < -b->kstart; });
    QString out = text;
    std::vector<long long> fixed;
    auto inside = [&](const QRegularExpressionMatch &m) {
        const double x = *py::to_float(m.captured(2)), y = *py::to_float(m.captured(4));
        return 0 <= x && x < double(nw) && 0 <= y && y < double(nh);
    };
    for (const script::Node *n : blocks) {
        const QString body = text.mid(n->vstart, n->vend - n->vstart);
        bool any_out = false;
        for (auto it = PAIR_RE.globalMatch(body); it.hasNext();)
            if (!inside(it.next()))
                any_out = true;
        if (!any_out)
            continue;
        const long long pid = py::to_int(n->key);
        std::vector<long long> ys, xs;
        for (long long y = 0; y < nh; ++y)
            for (long long x = 0; x < nw; ++x)
                if (md.idmap(north + y, left + x) == pid) {
                    ys.push_back(y);
                    xs.push_back(x);
                }
        if (xs.empty())
            continue;                                // (gone altogether: its block was taken out before)
        long long sx = 0, sy = 0;
        for (size_t i = 0; i < xs.size(); ++i) {
            sx += xs[i];
            sy += ys[i];
        }
        const double mx = double(sx) / double(xs.size()), my = double(sy) / double(ys.size());
        size_t k = 0;
        double bd = 0;
        for (size_t i = 0; i < xs.size(); ++i) {     // a pixel of its own, nearest its middle
            const double d = (double(xs[i]) - mx) * (double(xs[i]) - mx) + (double(ys[i]) - my) * (double(ys[i]) - my);
            if (i == 0 || d < bd) {
                bd = d;
                k = i;
            }
        }
        const double cx = double(xs[k]), cy = double(nh - 1 - ys[k]);
        QString nb;
        qsizetype pos = 0;
        for (auto it = PAIR_RE.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            nb += body.mid(pos, m.capturedStart() - pos);
            nb += inside(m) ? m.captured(0)
                            : m.captured(1) + py::fmt_f(cx, 3) + m.captured(3) + py::fmt_f(cy, 3);
            pos = m.capturedEnd();
        }
        nb += body.mid(pos);
        out = out.left(n->vstart) + nb + out.mid(n->vend);
        fixed.push_back(pid);
    }
    std::sort(fixed.begin(), fixed.end());
    return {out, fixed};
}

bool image_rows_north_first(const QImage &img0, const mapview::MapData &md, const std::vector<std::uint8_t> &sea)
{
    return image_rows_north_first(img0, md.idmap, sea);
}

bool image_rows_north_first(const QImage &img0, const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &sea)
{
    const long long mh = idmap.h(), mw = idmap.w();
    // Does a picture of the whole map have the north edge in its first row? Its rows laid against the map's land,
    // both ways round. (np.corrcoef's dot is BLAS: only which way fits better is used.)
    const QImage im = img0.convertToFormat(QImage::Format_RGBA8888);
    const long long iw = im.width(), ih = im.height();
    if (ih < 2 || iw < 1)
        return true;
    std::vector<double> g(static_cast<size_t>(ih)), land(static_cast<size_t>(ih));
    std::vector<float> row(static_cast<size_t>(iw));
    for (long long y = 0; y < ih; ++y) {
        const uchar *p = im.constScanLine(int(y));
        for (long long x = 0; x < iw; ++x)
            row[size_t(x)] = (((-0.0f + float(p[4 * x])) + float(p[4 * x + 1])) + float(p[4 * x + 2])) / 3.0f;
        g[size_t(y)] = double(grid::np_mean_f(row));
        const long long r = std::min(py::trunc_int((double(y) + 0.5) / double(ih) * double(mh)), mh - 1);
        long long cnt = 0, n = 0;
        for (long long x = 0; x < mw; x += 16, ++n) {
            const std::uint16_t id = idmap(r, x);
            cnt += id < sea.size() && !sea[id] && id > 0;
        }
        land[size_t(y)] = double(float(double(cnt) / double(n)));
    }
    auto corr = [&](const std::vector<double> &a) {
        const auto c = positions::cov2(a, land);
        return c[1] / std::sqrt(c[0]) / std::sqrt(c[3]);
    };
    std::vector<double> gr(g.rbegin(), g.rend());
    const double c1 = corr(g), c2 = corr(gr);
    if (std::isnan(c1) || std::isnan(c2))
        return true;
    return std::fabs(std::clamp(c1, -1.0, 1.0)) >= std::fabs(std::clamp(c2, -1.0, 1.0));
}

std::pair<std::vector<long long>, std::vector<long long>> affected(const mapview::MapData &md, long long left,
                                                                  long long right, long long north, long long south)
{
    return affected(md.idmap, md.n, left, right, north, south);
}

std::pair<std::vector<long long>, std::vector<long long>> affected(const grid::Grid<std::uint16_t> &idmap, long long n,
                                                                  long long left, long long right, long long north,
                                                                  long long south)
{
    // (provinces that lose every pixel, provinces that lose a part) for a cut.
    struct { const grid::Grid<std::uint16_t> &idmap; long long w, h, n; } md{idmap, idmap.w(), idmap.h(), n};
    std::vector<long long> total(size_t(md.n), 0), kept(size_t(md.n), 0);
    for (long long y = 0; y < md.h; ++y)
        for (long long x = 0; x < md.w; ++x) {
            const std::uint16_t v = md.idmap(y, x);
            total[v] += 1;
            if (y >= north && y < md.h - south && x >= left && x < md.w - right)
                kept[v] += 1;
        }
    std::vector<long long> gone, part;
    for (long long p = 1; p < md.n; ++p) {
        if (!total[size_t(p)])
            continue;
        if (kept[size_t(p)] == 0)
            gone.push_back(p);
        else if (kept[size_t(p)] < total[size_t(p)])
            part.push_back(p);
    }
    return {gone, part};
}

QString south_whole_note(long long nh, long long side)
{
    return u"%1 px high after a cut from the south is a whole number of the map's tiles (%2 tiles of %3 px) \u2014 "
           u"such cuts crashed the game while it loaded the map (tried: 1440 and 1728 px from the base game, even "
           u"with the edge made sea; the same heights cut from the north loaded). Take 8 rows more or fewer off the "
           u"bottom, or take rows off the top instead."_s.arg(nh).arg(nh / side).arg(side);
}

std::vector<long long> edge_band_gone(const grid::Grid<std::uint16_t> &idmap, long long n,
                                      const std::vector<std::uint8_t> &sea, long long left, long long right,
                                      long long north, long long south)
{
    // (C++ only) land provinces a cut leaves ONLY in the edge it makes sea (the two southernmost rows when the height
    // changes, the two westernmost columns after a cut from the west): they disappear as well
    const long long w = idmap.w(), h = idmap.h(), nw = w - left - right, nh = h - north - south;
    if (std::min({left, right, north, south}) < 0)
        return {};
    const bool rows = (north || south) && nh > 2, cols = left > 0 && nw > 2;
    if (!(rows || cols) || n <= 0)
        return {};
    std::vector<char> in_band(static_cast<size_t>(n), 0), outside(static_cast<size_t>(n), 0);
    for (long long y = north; y < h - south; ++y)
        for (long long x = left; x < w - right; ++x) {
            const long long v = idmap(y, x);
            if (v <= 0 || v >= n || (size_t(v) < sea.size() && sea[size_t(v)]))
                continue;
            const bool band = (rows && y >= h - south - 2) || (cols && x < left + 2);
            (band ? in_band : outside)[size_t(v)] = 1;
        }
    std::vector<long long> out;
    for (long long v = 1; v < n; ++v)
        if (in_band[size_t(v)] && !outside[size_t(v)])
            out.push_back(v);
    return out;
}

QString move_positions(const QString &text, long long left, const std::vector<long long> &gone_v)
{
    // every x moved by the columns cut off the left end; the blocks of the provinces that disappeared taken out
    const std::set<long long> gone(gone_v.begin(), gone_v.end());
    std::vector<std::pair<qsizetype, qsizetype>> cuts;
    for (const script::Node &n : script::parse(text))
        if (n.has_key && !n.key.isEmpty() && n.is_block && py::isdigit(n.key) && gone.count(py::to_int(n.key)))
            cuts.emplace_back(n.kstart, n.vend);
    std::sort(cuts.begin(), cuts.end(), std::greater<>());
    QString out = text;
    for (const auto &[a, b] : cuts) {
        qsizetype la = (a > 0 ? out.lastIndexOf(u'\n', a - 1) : -1) + 1;
        const qsizetype e = out.indexOf(u'\n', b);
        if (!py::strip(out.mid(la, a - la)).isEmpty())
            la = a;
        out = out.left(la) + out.mid(e >= 0 ? e + 1 : out.size());
    }
    return left ? shift_values(out, X_RE, left) : out;
}

std::vector<MapFile> files_of(const moddata::Mod &mod)
{
    // [(name, path, the mod's own file?)] of the map files a cut touches (those that exist).
    QStringList names = BITMAPS;
    names << u"positions.txt"_s;
    for (const QString &c : COLORMAPS)
        names << u"terrain/"_s + c;
    std::vector<MapFile> out;
    for (const QString &name : names) {
        const QString p = mod.p(QStringList{u"map"_s} + name.split(u'/'));
        if (fs::isfile(p))
            out.push_back({name, p, mod.in_mod(p)});
    }
    return out;
}

std::optional<MapFile> minimap_of(const moddata::Mod &mod)
{
    for (const QString &n : MINIMAPS) {
        const QString p = mod.p({u"gfx"_s, u"interface"_s, n});
        if (fs::isfile(p))
            return MapFile{n, p, mod.in_mod(p)};
    }
    return std::nullopt;
}

QImage cut_minimap(const QImage &img, long long map_w, long long left, long long right)
{
    // The same share cut off each end as the map loses, stretched back to its own size.
    const long long iw = img.width(), ih = img.height();
    const double a = double(iw) * double(left) / double(map_w), b = double(iw) * double(right) / double(map_w);
    QImage out(int(iw), int(ih), QImage::Format_RGBA8888);
    out.fill(0);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(0, 0, double(iw), double(ih)), img.convertToFormat(QImage::Format_RGBA8888),
                QRectF(a, 0, double(iw) - a - b, double(ih)));
    p.end();
    return out;
}

std::optional<Fit> minimap_fit(const mapview::MapData &md, const std::vector<std::uint8_t> &sea, const QImage &img0)
{
    // Which columns of the map the minimap picture stands for, found by laying the map's land over what is drawn.
    if (img0.isNull() || img0.width() < 16 || img0.height() < 8)
        return std::nullopt;
    const QImage img = img0.convertToFormat(QImage::Format_RGBA8888);
    const long long iw = img.width(), ih = img.height(), N = iw * ih;
    std::vector<int> ch[3];
    for (auto &c : ch)
        c.reserve(static_cast<size_t>(N));
    for (long long y = 0; y < ih; ++y) {
        const uchar *p = img.constScanLine(int(y));
        for (long long x = 0; x < iw; ++x)
            for (int k = 0; k < 3; ++k)
                ch[k].push_back(p[4 * x + k]);
    }
    const double paper[3] = {median_of(ch[0]), median_of(ch[1]), median_of(ch[2])};
    std::vector<float> mini(static_cast<size_t>(N));
    for (size_t i = 0; i < size_t(N); ++i) {
        const double s = ((-0.0 + std::fabs(ch[0][i] - paper[0])) + std::fabs(ch[1][i] - paper[1])) + std::fabs(ch[2][i] - paper[2]);
        mini[i] = s > 60 ? 1.0f : 0.0f;              // what is drawn on the paper
    }
    const float mm = grid::np_mean_f(mini);
    if (!(0.02f < mm && mm < 0.98f))
        return std::nullopt;
    const long long step = 8;
    const long long n = (md.w + step - 1) / step;
    std::vector<float> land(static_cast<size_t>(ih * n));           // ih x (map width / 8)
    for (long long y = 0; y < ih; ++y) {
        const long long r = std::min(py::trunc_int((double(y) + 0.5) / double(ih) * double(md.h)), md.h - 1);
        for (long long c = 0; c < n; ++c) {
            const std::uint16_t id = md.idmap(r, c * step);
            land[size_t(y * n + c)] = (!sea[id] && id > 0) ? 1.0f : 0.0f;
        }
    }
    std::vector<float> mc(static_cast<size_t>(N)), sq(static_cast<size_t>(N));
    for (size_t i = 0; i < size_t(N); ++i) {
        mc[i] = mini[i] - mm;
        sq[i] = mc[i] * mc[i];
    }
    double mn = double(std::sqrt(grid::np_sum_f(sq)));
    if (mn == 0)
        mn = 1.0;
    std::vector<double> xs(static_cast<size_t>(iw));
    for (long long i = 0; i < iw; ++i)
        xs[size_t(i)] = (double(i) + 0.5) / double(iw);
    std::vector<float> s(static_cast<size_t>(N)), sc(static_cast<size_t>(N)), prod(static_cast<size_t>(N));
    std::vector<long long> idx(static_cast<size_t>(iw));
    auto match = [&](long long left, long long width) {
        for (long long i = 0; i < iw; ++i)
            idx[size_t(i)] = py::trunc_int((double(left) + xs[size_t(i)] * double(width)) / double(step));
        for (long long y = 0; y < ih; ++y)
            for (long long i = 0; i < iw; ++i) {
                const long long c = idx[size_t(i)];
                s[size_t(y * iw + i)] = (c >= 0 && c < n) ? land[size_t(y * n + c)] : 0.0f;
            }
        const float m = grid::np_mean_f(s);
        for (size_t i = 0; i < size_t(N); ++i) {
            sc[i] = s[i] - m;
            sq[i] = sc[i] * sc[i];
            prod[i] = sc[i] * mc[i];
        }
        const double d = double(std::sqrt(grid::np_sum_f(sq)));
        return d != 0 ? double(grid::np_sum_f(prod)) / (d * mn) : -1.0;
    };
    const long long w = md.w;
    double bc = match(0, w);
    long long bl = 0, bw = w;
    const double now = bc;
    for (long long width = py::trunc_int(double(w) * 0.6); width < py::trunc_int(double(w) * 1.6); width += 64)
        for (long long left = py::trunc_int(-double(w) * 0.35); left < py::trunc_int(double(w) * 0.25); left += 64) {
            const double c = match(left, width);
            if (c > bc) {
                bc = c;
                bl = left;
                bw = width;
            }
        }
    const long long l0 = bl, w0 = bw;
    for (long long width = w0 - 64; width < w0 + 65; width += 8)
        for (long long left = l0 - 64; left < l0 + 65; left += 8) {
            const double c = match(left, width);
            if (c > bc) {
                bc = c;
                bl = left;
                bw = width;
            }
        }
    return Fit{bl, bw, bc, now};
}

QImage refit_minimap(const QImage &img0, long long map_w, long long left, long long width)
{
    // A minimap picture that stands for the map's columns left … left + width, redrawn so that it shows the map from
    // its first to its last column (the same size; where the map goes on past the old picture: its paper).
    const QImage img = img0.convertToFormat(QImage::Format_RGBA8888);
    const long long iw = img.width(), ih = img.height();
    std::vector<int> ch[4];
    for (long long y = 0; y < ih; ++y) {
        const uchar *p = img.constScanLine(int(y));
        for (long long x = 0; x < iw; ++x)
            for (int k = 0; k < 4; ++k)
                ch[k].push_back(p[4 * x + k]);
    }
    QImage out(int(iw), int(ih), QImage::Format_RGBA8888);
    out.fill(QColor(int(median_of(ch[0])), int(median_of(ch[1])), int(median_of(ch[2])), int(median_of(ch[3]))));
    const double k = double(iw) / double(width);       // picture pixels per map column, as it is now
    double sx0 = double(0 - left) * k, sx1 = double(map_w - left) * k;
    double span = sx1 - sx0;
    if (span == 0)
        span = 1.0;
    double tx0 = 0.0, tx1 = double(iw);
    if (sx0 < 0) {                                     // the map begins before the picture does: paper there
        tx0 = -sx0 / span * double(iw);
        sx0 = 0.0;
    }
    if (sx1 > double(iw)) {                            // … or goes on past its end
        tx1 = double(iw) - (sx1 - double(iw)) / span * double(iw);
        sx1 = double(iw);
    }
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(tx0, 0, tx1 - tx0, double(ih)), img, QRectF(sx0, 0, sx1 - sx0, double(ih)));
    p.end();
    return out;
}

// (C++ only, 2026-11-14) a bitmap grown by add_first / add_last new columns (cols) or STORED rows; each new pixel is
// pick(stored row, column, out): column -1 / w = a new pixel left / right of that stored row, stored row -1 / rows = a
// new row before / after the stored ones (at that column)
static QByteArray grow_bmp(const Bmp &b, bool cols, long long add_first, long long add_last,
                           const std::function<void(long long, long long, unsigned char *)> &pick)
{
    const long long bpx = b.bpp / 8, rows = std::llabs(b.h), stride = (b.w * bpx + 3) & ~3LL;
    if (b.raw.size() < b.off + stride * rows)
        throw py::ValueError("buffer is smaller than requested size");
    const long long nw = cols ? b.w + add_first + add_last : b.w, nrows = cols ? rows : rows + add_first + add_last;
    const long long nstride = (nw * bpx + 3) & ~3LL;
    QByteArray out(qsizetype(nstride * nrows), '\0');
    unsigned char px[4] = {0, 0, 0, 0};
    for (long long r = 0; r < nrows; ++r) {
        char *dst = out.data() + r * nstride;
        if (cols) {
            const char *src = b.raw.constData() + b.off + r * stride;
            pick(r, -1, px);
            for (long long i = 0; i < add_first; ++i)
                std::memcpy(dst + i * bpx, px, size_t(bpx));
            std::memcpy(dst + add_first * bpx, src, size_t(b.w * bpx));
            pick(r, b.w, px);
            for (long long i = 0; i < add_last; ++i)
                std::memcpy(dst + (add_first + b.w + i) * bpx, px, size_t(bpx));
        } else {
            const long long sr = r - add_first;
            if (0 <= sr && sr < rows) {
                std::memcpy(dst, b.raw.constData() + b.off + sr * stride, size_t(b.w * bpx));
            } else {
                for (long long x = 0; x < b.w; ++x) {
                    pick(sr < 0 ? -1 : rows, x, px);
                    std::memcpy(dst + x * bpx, px, size_t(bpx));
                }
            }
        }
    }
    QByteArray head = b.raw.left(qsizetype(b.off));
    put4(head, 2, b.off + nstride * nrows);
    put4(head, 18, nw);
    put4(head, 22, b.h > 0 ? nrows : -nrows);
    put4(head, 34, nstride * nrows);
    return head + out;
}

std::pair<QStringList, QString> apply_cut(moddata::Mod &mod, const mapview::MapData &md, long long left, long long right,
                                          const QString &backup_root, bool check, bool minimap, long long north,
                                          long long south, bool dry)
{
    // Cut the map. Everything is read and worked out first — nothing is written unless all of it can be; each of the
    // mod's own files is copied to the backup folder before it is replaced.
    const long long w = md.w, H = md.h;
    const long long nw = w - left - right, nh = H - north - south;
    const bool grow = std::min({left, right, north, south}) < 0;   // (C++ only: negative amounts add)
    if (grow && (std::max({left, right, north, south}) > 0 || ((left || right) && (north || south))))
        throw py::ValueError("grow the width or the height, one at a time — and nothing cut at the same time");
    if (!(left + right + north + south) || nw < 1 || nh < 1 ||
        (check && (left + right) && (nw < MIN_WIDTH || !width_ok(nw) || (grow && nw > w * GROW_MAX))))
        throw py::ValueError(u"%1 px is not a width the game is known to load"_s.arg(nw).toStdString());
    if (check && (north + south) && !height_ok(nh, nw))
        throw py::ValueError(u"%1 px is not a height the game is known to load"_s.arg(nh).toStdString());
    if (check && south > 0 && !grow)                 // (C++ only, tried 2026-11-18: see south_whole_note)
        if (const auto ts = tile_side(nw); ts && nh % *ts == 0)
            throw py::ValueError(south_whole_note(nh, *ts).toStdString());
    // the provinces as the files on disk number them now (a delete before the cut closed the numbers up: md is older)
    const grid::Grid<std::uint16_t> disk = ids_on_disk(mod);
    // which way up each picture is: laid against the map on disk with every colour told apart (C++ only, 2026-11-17: a
    // north cut first deletes the provinces it takes away; their pixels, now without a number, made the north row look
    // empty, the check took the SOUTH for north and cut 600 rows off the wrong end — 257 land provinces lost)
    const grid::Grid<std::uint16_t> disk_all = (north || south) ? ids_on_disk(mod, true) : disk;
    if (disk.w() != w || disk.h() != H)
        throw py::ValueError(u"provinces.bmp on disk is %1 × %2, the editor has %3 × %4 — read the mod again first"_s
                                 .arg(disk.w()).arg(disk.h()).arg(w).arg(H).toStdString());
    const auto gone = grow ? std::vector<long long>{} : affected(disk, 65536, left, right, north, south).first;
    // which numbers are sea: default.map as it is on disk NOW (C++ only, 2026-11-17: a height cut that first deleted the
    // provinces it takes away closed the numbers up, so mod.provinces still had the OLD numbers — renumbered sea zones
    // read as land, and the cut was refused after the deletion had been written)
    std::vector<std::uint8_t> sea(65536, 0);
    {
        const script::Nodes dm = script::parse(script::read_text(mod.p({u"map"_s, u"default.map"_s})));
        for (const QString &key : {u"sea_starts"_s, u"lakes"_s})
            for (const script::Node &n : dm)
                if (n.key == key && n.is_block) {
                    for (long long pid : script::bare_ints(n.kids))
                        if (0 < pid && pid < 65536)
                            sea[size_t(pid)] = 1;
                    break;
                }
    }
    auto sea_of = [&](long long y, long long x) {
        const std::uint16_t id = disk(y, x);
        return id < sea.size() && sea[id] != 0;
    };
    // growing the width: the edge pixel each row's new pixels continue — its own when it is sea, else the nearest sea
    // pixel up or down the edge column (so no land is drawn out into a stripe)
    std::vector<long long> src_l, src_r, src_n, src_s;
    // growing the height: the same along the edge row — each new pixel continues the nearest sea pixel of that row
    // (2026-11-17, seen in the game: repeating the north edge row drew every Arctic inlet and every strip of land out
    // into a long stripe; now the new rows are sea, like the new columns)
    if (grow && (north || south))
        for (const long long row : {0LL, H - 1}) {
            std::vector<long long> lf(size_t(w), -1), rt(size_t(w), -1), out(size_t(w), 0);
            for (long long x = 0, last = -1; x < w; ++x)
                lf[size_t(x)] = last = sea_of(row, x) ? x : last;
            for (long long x = w - 1, last = -1; x >= 0; --x)
                rt[size_t(x)] = last = sea_of(row, x) ? x : last;
            for (long long x = 0; x < w; ++x) {
                const long long l = lf[size_t(x)], r = rt[size_t(x)];
                out[size_t(x)] = l < 0 && r < 0 ? x : l < 0 ? r : r < 0 ? l : (x - l <= r - x ? l : r);
            }
            (row == 0 ? src_n : src_s) = std::move(out);
        }
    if (grow && (left || right))
        for (const long long col : {0LL, w - 1}) {
            std::vector<long long> up(size_t(H), -1), down(size_t(H), -1), out(size_t(H), 0);
            for (long long y = 0, last = -1; y < H; ++y)
                up[size_t(y)] = last = sea_of(y, col) ? y : last;
            for (long long y = H - 1, last = -1; y >= 0; --y)
                down[size_t(y)] = last = sea_of(y, col) ? y : last;
            for (long long y = 0; y < H; ++y) {
                const long long u = up[size_t(y)], d = down[size_t(y)];
                out[size_t(y)] = u < 0 && d < 0 ? y : u < 0 ? d : d < 0 ? u : (y - u <= d - y ? u : d);
            }
            (col == 0 ? src_l : src_r) = std::move(out);
        }
    bool first_north_grow = true;                     // which stored row is north (provinces.bmp says; all alike)
    if (grow) {
        const Bmp pb = read_bmp(mod.p({u"map"_s, mod.provinces_bmp}));
        first_north_grow = stored_first_is_north(disk_all, pb.raw, pb.off, pb.w, pb.h, pb.bpp);
    }
    // a bitmap grown: provinces.bmp continues the edge's nearest sea pixels (src_l / src_r / src_n / src_s; the edge
    // pixel itself only where its edge has no sea); terrain.bmp on the sea the value most of the sea along that edge
    // has, on land the edge pixel; rivers.bmp: that sea value on the sea, "no river" (white) on land
    auto grow_map_bmp = [&](const Bmp &b, const QString &name) -> QByteArray {
        if (b.w != w || std::llabs(b.h) != H)
            throw py::ValueError(u"%1 is %2 × %3, provinces.bmp is %4 × %5 — make them the same size first"_s.arg(name)
                                     .arg(b.w).arg(std::llabs(b.h)).arg(w).arg(H).toStdString());
        const long long bpx = b.bpp / 8, rows = std::llabs(b.h), stride = (b.w * bpx + 3) & ~3LL;
        const int kind = name == u"terrain.bmp"_s ? 1 : name == u"rivers.bmp"_s ? 2 : 0;
        if (kind && bpx != 1)
            throw py::ValueError(u"%1 is not an 8-bit bitmap — the map was not grown"_s.arg(name).toStdString());
        auto at = [&](long long r, long long x) {
            return reinterpret_cast<const unsigned char *>(b.raw.constData() + b.off + r * stride + x * bpx);
        };
        auto ed = [&](long long r) { return first_north_grow ? r : H - 1 - r; };   // stored row <-> map row
        const bool cols = left || right;
        // (stored row, column, sea?) of the edge pixel a new pixel continues
        auto source = [&](long long r, long long x) -> std::tuple<long long, long long, bool> {
            if (cols) {
                const long long col = x < 0 ? 0 : w - 1;
                const long long y = (x < 0 ? src_l : src_r)[size_t(ed(r))];
                return {ed(y), col, sea_of(y, col)};
            }
            const long long sr = r < 0 ? 0 : rows - 1;
            const long long mr = ed(sr);              // (the map row: 0 = north)
            const long long sx = (mr == 0 ? src_n : src_s)[size_t(x)];
            return {sr, sx, sea_of(mr, sx)};
        };
        std::array<long long, 256> cnt{};
        if (kind) {
            if (cols) {
                for (long long r = 0; r < rows; ++r)
                    for (const long long x : {-1LL, w})
                        if (const auto [sr, sc, sv] = source(r, x); sv)
                            ++cnt[*at(sr, sc)];
            } else {
                const long long first = first_north_grow ? -north : -south, last = first_north_grow ? -south : -north;
                for (long long x = 0; x < w; ++x)
                    for (const long long r : {-1LL, rows})
                        if ((r < 0 ? first : last) > 0)
                            if (const auto [sr, sc, sv] = source(r, x); sv)
                                ++cnt[*at(sr, sc)];
            }
        }
        const auto best = std::max_element(cnt.begin(), cnt.end());
        const std::uint8_t sea_val = std::uint8_t(best - cnt.begin());
        std::uint8_t none = 255;                      // rivers.bmp: the palette's white
        if (kind == 2) {
            const long long pal = 14 + le(b.raw, 14, 4);
            for (int i = 0; i < 256 && pal + 4 * i + 3 <= b.off; ++i)
                if (std::uint8_t(b.raw[pal + 4 * i]) == 255 && std::uint8_t(b.raw[pal + 4 * i + 1]) == 255 &&
                    std::uint8_t(b.raw[pal + 4 * i + 2]) == 255) {
                    none = std::uint8_t(i);
                    break;
                }
        }
        auto pick = [&](long long r, long long x, unsigned char *out) {
            const auto [sr, sc, sv] = source(r, x);
            if (kind == 0 || (kind == 1 && !sv))
                std::memcpy(out, at(sr, sc), size_t(bpx));
            else if (kind == 1)
                out[0] = sea_val;
            else
                out[0] = sv && *best ? sea_val : none;
        };
        if (cols)
            return grow_bmp(b, true, -left, -right, pick);
        return grow_bmp(b, false, first_north_grow ? -north : -south, first_north_grow ? -south : -north, pick);
    };
    struct Todo {
        QString name, path;
        QByteArray bytes;
        QString text;
        QImage img;
        int kind;                                    // 0 bytes, 1 text, 2 dds
    };
    std::vector<Todo> todo;
    bool first_north = true;
    auto rows_of = [&](const QImage &img, long long ih) {
        // (rows off the picture's first end, off its last end) — the same share as the map loses
        const long long a = py::round_int(double(ih) * double(north) / double(H));
        const long long b = py::round_int(double(ih) * double(south) / double(H));
        return image_rows_north_first(img, disk_all, sea) ? std::make_pair(a, b) : std::make_pair(b, a);
    };
    for (const MapFile &mf : files_of(mod)) {
        if (BITMAPS.contains(mf.name)) {
            const Bmp b = read_bmp(mf.path);
            if (b.w != w || std::llabs(b.h) != H)
                throw py::ValueError(u"%1 is %2 × %3, provinces.bmp is %4 × %5 — make them the same size first"_s.arg(mf.name)
                                         .arg(b.w).arg(std::llabs(b.h)).arg(w).arg(H).toStdString());
            if (grow) {
                todo.push_back({mf.name, mf.path, grow_map_bmp(b, mf.name), {}, {}, 0});
                continue;
            }
            QByteArray nb = (left || right) ? cut_bmp(b.raw, b.off, b.w, b.h, b.bpp, left, right) : b.raw;
            if (north || south) {
                if (mf.name == u"provinces.bmp")
                    first_north = stored_first_is_north(disk_all, b.raw, b.off, b.w, b.h, b.bpp);   // (C++ only: the map
                    // on disk NOW, 2026-11-17 — after the provinces a north cut takes away were deleted, the window's old
                    // map no longer matched the file's north row, and the SOUTH was cut instead)
                const auto [first, last] = first_north ? std::make_pair(north, south) : std::make_pair(south, north);
                nb = cut_bmp_rows(nb, b.off, nw, b.h, b.bpp, first, last);
            }
            todo.push_back({mf.name, mf.path, nb, {}, {}, 0});
        } else if (mf.name == u"positions.txt") {
            const QString moved = move_positions_y(move_positions(script::read_text(mf.path), left, gone), south);
            todo.push_back({mf.name, mf.path, {},
                            grow ? keep_inside(moved, nw, nh)   // (growing: everything stays on the map, moved along)
                                 : keep_inside(pull_in_positions(moved, disk, left, right, north, south).first, nw, nh),
                            {}, 1});
        } else if (mf.name.endsWith(u".dds"_s)) {
            // (C++ only) cut in its own format; a picture that can't be cut stops the whole cut — nothing written
            QFile hf(mf.path);
            QByteArray head;
            if (hf.open(QIODevice::ReadOnly))
                head = hf.read(128);
            if (head.size() < 128 || !head.startsWith("DDS "))
                throw py::ValueError(u"map/%1 is no .dds picture the editor can read — the map was not cut"_s.arg(mf.name)
                                         .toStdString());
            const long long ih = le32(head, 12), iw = le32(head, 16);
            const long long a = py::round_int(double(iw) * double(left) / double(w));
            const long long b = py::round_int(double(iw) * double(right) / double(w));
            std::pair<long long, long long> tt{0, 0};
            if (north || south) {
                const auto img = imgio::read_image(mf.path);
                tt = (img && !img->isNull()) ? rows_of(*img, ih)
                                             : std::make_pair(py::round_int(double(ih) * double(north) / double(H)),
                                                              py::round_int(double(ih) * double(south) / double(H)));
            }
            QString why;
            const QByteArray cut = cut_dds(mf.path, a, b, tt.first, tt.second, &why);
            if (cut.isEmpty())
                throw py::ValueError(u"map/%1 %2 — the map was not cut (nothing written)"_s.arg(mf.name, why).toStdString());
            todo.push_back({mf.name, mf.path, cut, {}, {}, 0});
        } else {
            const auto img = imgio::read_image(mf.path);
            if (!img || img->isNull())
                continue;
            const long long iw = img->width(), ih = img->height();
            const long long a = py::round_int(double(iw) * double(left) / double(w));
            const long long b = py::round_int(double(iw) * double(right) / double(w));
            const auto [ta, tb] = (north || south) ? rows_of(*img, ih) : std::make_pair(0LL, 0LL);
            todo.push_back({mf.name, mf.path, {}, {},
                            grow ? clamp_copy(*img, a, ta, iw - a - b, ih - ta - tb)
                                 : img->copy(int(a), int(ta), int(std::max(1LL, iw - a - b)), int(std::max(1LL, ih - ta - tb))),
                            2});
        }
    }
    const auto mm = minimap ? minimap_of(mod) : std::nullopt;
    if (mm && mm->name.endsWith(u".dds"_s)) {
        const auto img = imgio::read_image(mm->path);
        if (img && !img->isNull()) {
            const long long miw = img->width();
            const long long ma = py::round_int(double(miw) * double(left) / double(w));
            const long long mb = py::round_int(double(miw) * double(right) / double(w));
            QImage nimg = !(left || right) ? *img
                          : grow ? clamp_copy(*img, ma, 0, miw - ma - mb, img->height())
                                       .scaled(int(miw), img->height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 : cut_minimap(*img, w, left, right);
            if (north || south) {                    // the same share off each edge, stretched back to its size
                const long long ih = nimg.height();
                const auto [ta, tb] = rows_of(*img, ih);
                // growing: the new rows in the colour most of that edge row has (the map gets sea there; a mirrored or
                // repeated minimap would show land or streaks — 2026-11-17)
                auto padded = [&]() {
                    const QImage src = nimg.convertToFormat(QImage::Format_ARGB32);
                    QImage out(src.width(), int(std::max(1LL, ih - ta - tb)), QImage::Format_ARGB32);
                    auto common = [&](int r) {
                        std::map<QRgb, long long> n;
                        const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(r));
                        for (int i = 0; i < src.width(); ++i)
                            ++n[s[i]];
                        return std::max_element(n.begin(), n.end(), [](const auto &p, const auto &q) {
                                   return p.second < q.second;
                               })->first;
                    };
                    const QRgb top = common(0), bottom = common(src.height() - 1);
                    for (int j = 0; j < out.height(); ++j) {
                        const long long sy = ta + j;
                        auto *d = reinterpret_cast<QRgb *>(out.scanLine(j));
                        if (0 <= sy && sy < src.height())
                            std::memcpy(d, src.constScanLine(int(sy)), size_t(src.width()) * 4);
                        else
                            std::fill(d, d + src.width(), sy < 0 ? top : bottom);
                    }
                    return out;
                };
                const QImage part = grow ? padded()
                                         : nimg.copy(0, int(ta), nimg.width(), int(std::max(1LL, ih - ta - tb)));
                nimg = part.scaled(nimg.width(), int(ih), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            todo.push_back({u"../gfx/interface/"_s + mm->name, mm->path, {}, {}, nimg, 2});
        }
    }
    // (C++ only, measured in the game 2026-11-17) the base game cut from the south to 2128, 2104, 2096 px crashed
    // while loading with land or a river in the two southernmost rows; with those rows sea it loaded and a campaign
    // ran. 2026-11-18 more cuts showed the same at whole numbers of tiles too (rivers there: 1440, 1728 px), so the
    // rows are now made sea at every new height (the earlier exception for whole numbers of tiles is gone).
    const bool fn = grow ? first_north_grow : first_north;
    // (C++ only, measured in the game 2026-11-17/18 with the base game cut and grown many ways):
    //  - rows: whenever the height changes the two SOUTHERNMOST rows are made sea and carry no river (a river there
    //    crashed the game at "Creating Rivers...", land at "REcreating quads" / "writing quad mesh"; one row was not
    //    enough), and the two NORTHERNMOST rows carry no river (a 400-row cut from the north left rivers there and
    //    crashed the game at "Creating Rivers..."; without them it loaded)
    //  - columns: after a cut from the WEST the two westernmost columns are made sea and carry no river: owned land
    //    there crashed the game at the main menu as soon as it drew the country borders (it looks at the pixel to the
    //    left without a check); the east edge and the north edge are skipped by the game, so they are left alone
    // A land pixel takes the nearest sea pixel of its row (columns: of its column), a river pixel the nearest pixel
    // that is no river; terrain.bmp follows provinces.bmp.
    const bool seal_rows = (north || south) && nh > 2;
    const bool seal_west = left > 0 && nw > 2;
    if (seal_rows || seal_west) {
        auto head = [](const QByteArray &b) {
            return std::array<long long, 4>{le(b, 10, 4), le(b, 18, 4, true), le(b, 22, 4, true), le(b, 28, 2)};
        };
        auto nearest_of = [](const std::vector<char> &keep) {
            const long long n = static_cast<long long>(keep.size());
            std::vector<long long> out(size_t(n), -1);
            for (long long i = 0, last = -1; i < n; ++i)
                out[size_t(i)] = last = keep[size_t(i)] ? i : last;
            for (long long i = n - 1, last = -1; i >= 0; --i) {
                last = keep[size_t(i)] ? i : last;
                const long long l = out[size_t(i)];
                out[size_t(i)] = l < 0 ? last : last < 0 ? l : (i - l <= last - i ? l : last);
            }
            return out;
        };
        std::set<long long> wiped;                   // land provinces whose pixels there are repainted
        for (Todo &t : todo) {
            const bool prov = t.name == u"provinces.bmp"_s, riv = t.name == u"rivers.bmp"_s;
            const bool terr = t.name == u"terrain.bmp"_s;
            if (t.kind != 0 || !(prov || riv || terr))
                continue;
            const auto [off, bw, bh, bpp] = head(t.bytes);
            const long long bpx = bpp / 8, rows = std::llabs(bh), stride = (bw * bpx + 3) & ~3LL;
            if (bw != nw || rows != nh || bpx < 1 || (riv && bpx != 1))
                continue;
            auto at = [&](long long ny, long long x) {   // (map rows: 0 = north)
                const long long sr = fn ? ny : nh - 1 - ny;
                return reinterpret_cast<unsigned char *>(t.bytes.data() + off + sr * stride) + x * bpx;
            };
            // one line of pixels (a row or a column of the new map): what stays, and where each other pixel takes
            // its new value from
            auto fix_line = [&](const std::vector<std::pair<long long, long long>> &line, bool land_too,
                                const QString &what) {
                if (!land_too && !riv)
                    return;
                std::vector<char> keep(line.size());
                for (size_t i = 0; i < line.size(); ++i) {
                    const auto [ny, x] = line[i];
                    const long long oy = ny + north;     // (the row of the map before; a grown row is sea)
                    keep[i] = (prov || terr) ? (oy < 0 || oy >= H || sea_of(oy, x + left)) : *at(ny, x) >= 12;
                }
                const std::vector<long long> src = nearest_of(keep);
                for (size_t i = 0; i < line.size(); ++i) {
                    if (keep[i])
                        continue;
                    const auto [ny, x] = line[i];
                    if (src[i] < 0) {
                        if (terr)
                            continue;                    // (provinces.bmp refuses the cut for this line)
                        if (prov)
                            throw py::ValueError(u"the %1 would have no sea at all \u2014 the game crashes on land there "
                                                 u"(tried); cut a different amount"_s.arg(what).toStdString());
                        *at(ny, x) = 255;                // (no pixel without a river in the line: the palette's white)
                        continue;
                    }
                    if (prov && ny + north >= 0 && ny + north < H)
                        if (const long long id = disk(ny + north, x + left); id > 0)
                            wiped.insert(id);
                    const auto [sy, sx] = line[size_t(src[i])];
                    std::memcpy(at(ny, x), at(sy, sx), size_t(bpx));
                }
            };
            if (seal_rows) {
                for (const long long ny : {nh - 1, nh - 2, 0LL, 1LL}) {
                    std::vector<std::pair<long long, long long>> line;
                    for (long long x = 0; x < nw; ++x)
                        line.emplace_back(ny, x);
                    fix_line(line, ny >= nh - 2, u"two southernmost rows"_s);   // (the north rows: rivers only)
                }
            }
            if (seal_west)
                for (const long long c : {0LL, 1LL}) {
                    std::vector<std::pair<long long, long long>> line;
                    for (long long ny = 0; ny < nh; ++ny)
                        line.emplace_back(ny, c);
                    fix_line(line, true, u"two westernmost columns"_s);
                }
        }
        // a land province with no pixel left crashes the game (tried). (A dry run comes from the page BEFORE it deletes
        // such provinces — it finds them itself with edge_band_gone — so the dry run leaves them to the real cut.)
        if (!wiped.empty() && !dry) {
            std::set<long long> seen;
            const long long y_end = seal_rows ? std::min(H, H - south - 2) : std::min(H, H - south);
            for (long long y = std::max(0LL, north); y < y_end; ++y)   // (grown: amounts < 0)
                for (long long x = std::max(0LL, left) + (seal_west ? 2 : 0); x < std::min(w, w - right); ++x)
                    if (const long long id = disk(y, x); wiped.count(id))
                        seen.insert(id);
            QStringList lost;
            for (const long long id : wiped)
                if (!seen.count(id))
                    lost << QString::number(id);
            if (!lost.isEmpty())
                throw py::ValueError(u"province %1 would only be left on the edge of the map that is made sea (the two "
                                     u"southernmost rows / the two westernmost columns), where the game crashes on land "
                                     u"(tried) \u2014 tick \u201cDelete the provinces that disappear\u201d, cut a different "
                                     u"amount, or delete it first"_s.arg(lost.join(u", "_s)).toStdString());
        }
    }
    if (dry)
        return {};
    const QString bdir = join(backup_root, {u"map size"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
    QStringList written;
    for (Todo &t : todo) {
        const QString dst = pypath::normpath(join(mod.root, QStringList{u"map"_s} + t.name.split(u'/')));
        const QString name = QString(t.name).replace(u"../"_s, QString());
        if (fs::isfile(dst)) {
            const QString bk = join(bdir, name.split(u'/'));
            fs::makedirs(pypath::dirname(bk));
            fs::copy2(dst, bk);
        }
        fs::makedirs(pypath::dirname(dst));
        if (t.kind == 0) {
            QFile f(fs::watched(dst));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                throw std::runtime_error(("cannot write " + dst).toStdString());
            f.write(t.bytes);
        } else if (t.kind == 1) {
            script::write_text(dst, t.text);
        } else {
            imgio::write_dds32(dst, t.img);
        }
        written << dst;
    }
    return {written, bdir};
}

}  // namespace mapsize
