// imgio.py — the game's pictures, read and written. Line by line (numpy's array steps as plain loops).
#include "map/imgio.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QFile>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace imgio {

namespace {
using Bytes = QByteArray;

std::optional<Bytes> read_all(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

struct Fail : std::exception {};                 // (Python: an exception the reader turns into None)

std::uint8_t at(const Bytes &d, qsizetype i)
{
    if (i < 0 || i >= d.size())
        throw Fail();
    return static_cast<std::uint8_t>(d[i]);
}
std::uint16_t u16(const Bytes &d, qsizetype i) { return static_cast<std::uint16_t>(at(d, i) | (at(d, i + 1) << 8)); }
std::uint32_t u32(const Bytes &d, qsizetype i)
{
    return static_cast<std::uint32_t>(at(d, i)) | (static_cast<std::uint32_t>(at(d, i + 1)) << 8) |
           (static_cast<std::uint32_t>(at(d, i + 2)) << 16) | (static_cast<std::uint32_t>(at(d, i + 3)) << 24);
}

// _to_qimage: rows of w * 4 RGBA bytes, as its own picture
QImage to_qimage(const std::vector<std::uint8_t> &rgba, int w, int h)
{
    if (w <= 0 || h <= 0)
        return QImage();
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        std::memcpy(img.scanLine(y), rgba.data() + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);
    return img;
}

// the picture as RGBA8888 rows of w * 4 bytes, top row first
std::vector<std::uint8_t> rgba_rows(const QImage &src, int &w, int &h)
{
    const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
    w = img.width();
    h = img.height();
    std::vector<std::uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * w * 4, img.constScanLine(y), static_cast<size_t>(w) * 4);
    return out;
}

void write_bytes(const QString &path, const QByteArray &data)
{
    fs::makedirs(pypath::dirname(path));
    QFile f(fs::watched(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw fs::OSError("open: " + path.toStdString(), 13);
    f.write(data);
}

void put16(QByteArray &b, std::uint16_t v)
{
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>(v >> 8));
}
void put32(QByteArray &b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}
QByteArray tga_head(int w, int h, int bpp, int desc)   // struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, bpp, desc)
{
    QByteArray b;
    b.append(char(0));
    b.append(char(0));
    b.append(char(2));
    put16(b, 0);
    put16(b, 0);
    b.append(char(0));
    put16(b, 0);
    put16(b, 0);
    put16(b, static_cast<std::uint16_t>(w));
    put16(b, static_cast<std::uint16_t>(h));
    b.append(static_cast<char>(bpp));
    b.append(static_cast<char>(desc));
    return b;
}
}  // namespace

std::optional<QImage> read_tga(const QString &path)
{
    try {
        const auto data0 = read_all(path);
        if (!data0)
            return std::nullopt;
        const Bytes &data = *data0;
        const int idlen = at(data, 0), cmap = at(data, 1), typ = at(data, 2);
        const int w = u16(data, 12), h = u16(data, 14), bpp = at(data, 16), desc = at(data, 17);
        if (cmap || (typ != 2 && typ != 10) || (bpp != 16 && bpp != 24 && bpp != 32) || !w || !h)
            return std::nullopt;
        const int n = bpp / 8;
        qsizetype pos = 18 + idlen;
        const qsizetype count = static_cast<qsizetype>(w) * h;
        std::vector<std::uint8_t> px(static_cast<size_t>(count) * n, 0);   // (np.empty: rows a cut-off file never fills)
        if (typ == 2) {
            if (pos + count * n > data.size())
                throw Fail();                    // (frombuffer: buffer is smaller than requested)
            std::memcpy(px.data(), data.constData() + pos, static_cast<size_t>(count) * n);
        } else {                                 // run-length packets
            qsizetype i = 0;
            while (i < count && pos < data.size()) {
                const int head = at(data, pos);
                pos += 1;
                qsizetype k = (head & 0x7F) + 1;
                if (head & 0x80) {
                    if (pos + n > data.size())
                        throw Fail();
                    for (qsizetype j = i; j < std::min(count, i + k); ++j)
                        std::memcpy(px.data() + j * n, data.constData() + pos, n);
                    pos += n;
                } else {
                    k = std::min(k, count - i);
                    if (pos + k * n > data.size())
                        throw Fail();
                    std::memcpy(px.data() + i * n, data.constData() + pos, static_cast<size_t>(k * n));
                    pos += k * n;
                }
                i += k;
            }
        }
        const bool flip = !(desc & 0x20);        // stored bottom row first
        std::vector<std::uint8_t> rgba(static_cast<size_t>(count) * 4);
        for (int y = 0; y < h; ++y) {
            const int sy = flip ? h - 1 - y : y;
            for (int x = 0; x < w; ++x) {
                const std::uint8_t *p = px.data() + (static_cast<size_t>(sy) * w + x) * n;
                std::uint8_t *o = rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
                if (n == 2) {                    // 5 bits a colour
                    const std::uint16_t v = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
                    o[0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
                    o[1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
                    o[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
                    o[3] = 255;
                } else {
                    o[0] = p[2];
                    o[1] = p[1];
                    o[2] = p[0];
                    o[3] = n == 4 ? p[3] : 255;
                }
            }
        }
        return to_qimage(rgba, w, h);
    } catch (...) {
        return std::nullopt;
    }
}

void write_tga(const QString &path, const QImage &img)
{
    int w, h;
    const auto arr = rgba_rows(img, w, h);
    QByteArray out = tga_head(w, h, 24, 0);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t *p = arr.data() + (static_cast<size_t>(y) * w + x) * 4;
            out.append(static_cast<char>(p[2]));
            out.append(static_cast<char>(p[1]));
            out.append(static_cast<char>(p[0]));
        }
    write_bytes(path, out);
}

void write_tga32(const QString &path, const QImage &img)
{
    int w, h;
    const auto arr = rgba_rows(img, w, h);
    QByteArray out = tga_head(w, h, 32, 8);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t *p = arr.data() + (static_cast<size_t>(y) * w + x) * 4;
            out.append(static_cast<char>(p[2]));
            out.append(static_cast<char>(p[1]));
            out.append(static_cast<char>(p[0]));
            out.append(static_cast<char>(p[3]));
        }
    write_bytes(path, out);
}

void write_dds32(const QString &path, const QImage &img)
{
    int w, h;
    const auto arr = rgba_rows(img, w, h);
    QByteArray out("DDS ");
    for (std::uint32_t v : {124u, 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u, static_cast<std::uint32_t>(h),
                            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(w) * 4u, 0u, 0u})
        put32(out, v);
    out.append(QByteArray(44, '\0'));
    for (std::uint32_t v : {32u, 0x41u, 0u, 32u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u})
        put32(out, v);
    for (std::uint32_t v : {0x1000u, 0u, 0u, 0u, 0u})
        put32(out, v);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint8_t *p = arr.data() + (static_cast<size_t>(y) * w + x) * 4;
            out.append(static_cast<char>(p[2]));
            out.append(static_cast<char>(p[1]));
            out.append(static_cast<char>(p[0]));
            out.append(static_cast<char>(p[3]));
        }
    write_bytes(path, out);
}

void write_image(const QString &path, const QImage &img)
{
    if (py::lower(path).endsWith(u".dds"))
        write_dds32(path, img);
    else
        write_tga32(path, img);
}

namespace {
std::uint16_t c565(std::uint16_t c, int ch)          // _565: one channel of a 5-6-5 colour, to 0..255
{
    if (ch == 0)
        return static_cast<std::uint16_t>((((c >> 11) & 31) * 255 + 15) / 31);
    if (ch == 1)
        return static_cast<std::uint16_t>((((c >> 5) & 63) * 255 + 31) / 63);
    return static_cast<std::uint16_t>(((c & 31) * 255 + 15) / 31);
}
}  // namespace

std::optional<QImage> read_dds(const QString &path)
{
    try {
        const auto data0 = read_all(path);
        if (!data0)
            return std::nullopt;
        const Bytes &data = *data0;
        if (data.left(4) != QByteArray("DDS "))
            return std::nullopt;
        const std::uint32_t h = u32(data, 12), w = u32(data, 16);
        const std::uint32_t pf_flags = u32(data, 80);
        const QByteArray four = data.mid(84, 4);
        const std::uint32_t bits = u32(data, 88);
        const qsizetype body = 128;
        const qsizetype blen = std::max<qsizetype>(0, data.size() - body);
        if ((pf_flags & 4) && (four == "DXT1" || four == "DXT3" || four == "DXT5")) {
            const qsizetype bw = (static_cast<qsizetype>(w) + 3) / 4, bh = (static_cast<qsizetype>(h) + 3) / 4;
            const qsizetype size = four == "DXT1" ? 8 : 16;
            if (bw * bh * size > blen)
                throw Fail();                    // (reshape of a cut-off body)
            const qsizetype W = bw * 4, H = bh * 4;
            std::vector<std::uint8_t> full(static_cast<size_t>(W) * H * 4);
            for (qsizetype b = 0; b < bw * bh; ++b) {
                const unsigned char *blk = reinterpret_cast<const unsigned char *>(data.constData()) + body + b * size;
                const unsigned char *cb = blk + size - 8;
                const std::uint16_t c0 = static_cast<std::uint16_t>(cb[0] | (cb[1] << 8));
                const std::uint16_t c1 = static_cast<std::uint16_t>(cb[2] | (cb[3] << 8));
                std::uint16_t pal[4][4];
                for (int ch = 0; ch < 3; ++ch) {
                    pal[0][ch] = c565(c0, ch);
                    pal[1][ch] = c565(c1, ch);
                }
                for (int k = 0; k < 4; ++k)
                    pal[k][3] = 255;
                const bool four_ = four != "DXT1";
                const bool gt = c0 > c1;
                for (int ch = 0; ch < 3; ++ch) {
                    const std::uint16_t p0 = pal[0][ch], p1 = pal[1][ch];
                    if (four_ || gt) {
                        pal[2][ch] = static_cast<std::uint16_t>((2 * p0 + p1) / 3);
                        pal[3][ch] = static_cast<std::uint16_t>((p0 + 2 * p1) / 3);
                    } else {
                        pal[2][ch] = static_cast<std::uint16_t>((p0 + p1) / 2);
                        pal[3][ch] = 0;
                    }
                }
                if (!four_)
                    pal[3][3] = gt ? 255 : 0;
                const std::uint32_t idx = static_cast<std::uint32_t>(cb[4]) | (static_cast<std::uint32_t>(cb[5]) << 8) |
                                          (static_cast<std::uint32_t>(cb[6]) << 16) | (static_cast<std::uint32_t>(cb[7]) << 24);
                std::uint8_t px[16][4];
                for (int i = 0; i < 16; ++i) {
                    const int sel = (idx >> (2 * i)) & 3;
                    for (int ch = 0; ch < 4; ++ch)
                        px[i][ch] = static_cast<std::uint8_t>(pal[sel][ch]);
                }
                if (four == "DXT3") {
                    std::uint64_t bitsa = 0;
                    for (int i = 0; i < 8; ++i)
                        bitsa += static_cast<std::uint64_t>(blk[i]) << (8 * i);
                    for (int i = 0; i < 16; ++i)
                        px[i][3] = static_cast<std::uint8_t>(((bitsa >> (4 * i)) & 15) * 17);
                } else if (four == "DXT5") {
                    const std::uint16_t a0 = blk[0], a1 = blk[1];
                    std::uint16_t apal[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
                    const bool agt = a0 > a1;
                    for (int i = 1; i < 7; ++i)
                        apal[i + 1] = agt ? static_cast<std::uint16_t>(((7 - i) * a0 + i * a1) / 7)
                                          : (i < 5 ? static_cast<std::uint16_t>(((5 - i) * a0 + i * a1) / 5)
                                                   : static_cast<std::uint16_t>(i == 5 ? 0 : 255));
                    std::uint64_t bitsa = 0;
                    for (int i = 0; i < 6; ++i)
                        bitsa += static_cast<std::uint64_t>(blk[2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i)
                        px[i][3] = static_cast<std::uint8_t>(apal[(bitsa >> (3 * i)) & 7]);
                }
                const qsizetype by = b / bw, bx = b % bw;
                for (int i = 0; i < 16; ++i) {
                    const qsizetype y = by * 4 + i / 4, x = bx * 4 + i % 4;
                    std::memcpy(full.data() + (y * W + x) * 4, px[i], 4);
                }
            }
            std::vector<std::uint8_t> rgba(static_cast<size_t>(w) * h * 4);
            for (qsizetype y = 0; y < static_cast<qsizetype>(h); ++y)
                std::memcpy(rgba.data() + y * w * 4, full.data() + y * W * 4, static_cast<size_t>(w) * 4);
            return to_qimage(rgba, static_cast<int>(w), static_cast<int>(h));
        }
        if (bits == 24 || bits == 32) {          // plain pixels, blue first
            const int n = static_cast<int>(bits / 8);
            const qsizetype count = static_cast<qsizetype>(w) * h;
            if (count * n > blen)
                throw Fail();
            const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData()) + body;
            std::vector<std::uint8_t> rgba(static_cast<size_t>(count) * 4);
            for (qsizetype i = 0; i < count; ++i) {
                rgba[i * 4] = p[i * n + 2];
                rgba[i * 4 + 1] = p[i * n + 1];
                rgba[i * 4 + 2] = p[i * n];
                rgba[i * 4 + 3] = n == 4 ? p[i * n + 3] : 255;
            }
            return to_qimage(rgba, static_cast<int>(w), static_cast<int>(h));
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<QImage> read_image(const QString &path)
{
    const QString low = py::lower(path);
    std::optional<QImage> img = low.endsWith(u".tga") ? read_tga(path) : low.endsWith(u".dds") ? read_dds(path) : std::nullopt;
    if (!img) {
        QImage q(path);
        if (!q.isNull())
            img = q;
    }
    return img;
}

}  // namespace imgio
