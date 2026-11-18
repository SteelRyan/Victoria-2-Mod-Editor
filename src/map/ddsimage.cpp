// ddsimage.py — .dds textures and the religion icons. Line by line; numpy's float32 steps kept in float (each
// operation rounded to single precision, as numpy does).
#include "map/ddsimage.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QFile>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Qt::StringLiterals;

namespace ddsimage {

namespace {
struct Fail : std::exception {};                 // (Python: ValueError — the file is too short)

std::uint32_t u32(const QByteArray &d, qsizetype i)
{
    const auto *p = reinterpret_cast<const unsigned char *>(d.constData()) + i;
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void rgb565(std::uint16_t c0, float out[3])
{
    const std::uint32_t c = c0;
    const std::uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = static_cast<float>((r * 255 + 15) / 31);
    out[1] = static_cast<float>((g * 255 + 31) / 63);
    out[2] = static_cast<float>((b * 255 + 15) / 31);
}

std::uint8_t to_u8(float v)                      // np.clip(v + 0.5, 0, 255).astype(np.uint8)
{
    float x = v + 0.5f;
    x = x < 0.0f ? 0.0f : (x > 255.0f ? 255.0f : x);
    return static_cast<std::uint8_t>(x);
}

std::vector<std::uint8_t> decode_dxt(const unsigned char *data, qsizetype len, qsizetype w, qsizetype h,
                                     const QByteArray &kind)
{
    const qsizetype bw = std::max<qsizetype>(1, (w + 3) / 4), bh = std::max<qsizetype>(1, (h + 3) / 4);
    const qsizetype n = bw * bh;
    const qsizetype bs = kind == "DXT1" ? 8 : 16;
    if (n * bs > len)
        throw Fail();
    const qsizetype W = bw * 4;
    std::vector<float> img(static_cast<size_t>(W) * bh * 4 * 4);
    for (qsizetype b = 0; b < n; ++b) {
        const unsigned char *blk = data + b * bs;
        const unsigned char *cb = blk + bs - 8;
        const std::uint16_t c0 = static_cast<std::uint16_t>(cb[0] | (cb[1] << 8));
        const std::uint16_t c1 = static_cast<std::uint16_t>(cb[2] | (cb[3] << 8));
        const std::uint64_t bits = static_cast<std::uint64_t>(cb[4]) | (static_cast<std::uint64_t>(cb[5]) << 8) |
                                   (static_cast<std::uint64_t>(cb[6]) << 16) | (static_cast<std::uint64_t>(cb[7]) << 24);
        float col0[3], col1[3];
        rgb565(c0, col0);
        rgb565(c1, col1);
        const bool four = kind == "DXT1" ? c0 > c1 : true;
        float pal[4][4];
        for (int k = 0; k < 4; ++k)
            pal[k][3] = 255.0f;
        for (int ch = 0; ch < 3; ++ch) {
            pal[0][ch] = col0[ch];
            pal[1][ch] = col1[ch];
            pal[2][ch] = four ? (2.0f * col0[ch] + col1[ch]) / 3.0f : (col0[ch] + col1[ch]) / 2.0f;
            pal[3][ch] = four ? (col0[ch] + 2.0f * col1[ch]) / 3.0f : 0.0f;
        }
        pal[3][3] = four ? 255.0f : 0.0f;
        float px[16][4];
        for (int i = 0; i < 16; ++i) {
            const int sel = static_cast<int>((bits >> (2 * i)) & 3);
            std::memcpy(px[i], pal[sel], sizeof px[i]);
        }
        if (kind == "DXT3") {
            for (int p = 0; p < 16; ++p)
                px[p][3] = static_cast<float>((blk[p / 2] >> (4 * (p % 2))) & 15) * 17.0f;
        } else if (kind == "DXT5") {
            const float a0 = blk[0], a1 = blk[1];
            std::uint64_t abits = 0;
            for (int i = 0; i < 6; ++i)
                abits |= static_cast<std::uint64_t>(blk[2 + i]) << (8 * i);
            float apal[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
            const bool big = a0 > a1;
            for (int i = 1; i < 7; ++i)
                apal[i + 1] = big ? (static_cast<float>(7 - i) * a0 + static_cast<float>(i) * a1) / 7.0f : 0.0f;
            for (int i = 1; i < 5; ++i)
                apal[i + 1] = big ? apal[i + 1] : (static_cast<float>(5 - i) * a0 + static_cast<float>(i) * a1) / 5.0f;
            apal[6] = big ? apal[6] : 0.0f;
            apal[7] = big ? apal[7] : 255.0f;
            for (int i = 0; i < 16; ++i)
                px[i][3] = apal[(abits >> (3 * i)) & 7];
        }
        const qsizetype by = b / bw, bx = b % bw;
        for (int i = 0; i < 16; ++i) {
            const qsizetype y = by * 4 + i / 4, x = bx * 4 + i % 4;
            std::memcpy(img.data() + (y * W + x) * 4, px[i], sizeof px[i]);
        }
    }
    std::vector<std::uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (qsizetype y = 0; y < h; ++y)
        for (qsizetype x = 0; x < w; ++x)
            for (int ch = 0; ch < 4; ++ch)
                out[(y * w + x) * 4 + ch] = to_u8(img[(y * W + x) * 4 + ch]);
    return out;
}

std::vector<std::uint8_t> decode_plain(const unsigned char *data, qsizetype len, qsizetype w, qsizetype h, std::uint32_t bits,
                                       const std::uint32_t masks[4])
{
    const qsizetype bpp = bits / 8;
    const qsizetype count = w * h;
    if (count * bpp > len)
        throw Fail();
    std::vector<std::uint8_t> out(static_cast<size_t>(count) * 4, 0);
    for (int ch = 0; ch < 4; ++ch) {
        const std::uint32_t m = masks[ch];
        if (m == 0) {
            for (qsizetype i = 0; i < count; ++i)
                out[i * 4 + ch] = ch == 3 ? 255 : 0;
            continue;
        }
        int shift = 0;                           // (m & -m).bit_length() - 1
        while (!((m >> shift) & 1u))
            ++shift;
        const std::uint32_t top = m >> shift;
        for (qsizetype i = 0; i < count; ++i) {
            std::uint32_t v = 0;
            for (qsizetype k = 0; k < bpp; ++k)
                v |= static_cast<std::uint32_t>(data[i * bpp + k]) << (8 * k);
            out[i * 4 + ch] = static_cast<std::uint8_t>(((v & m) >> shift) * 255u / top);   // (uint32: wraps as numpy's)
        }
    }
    return out;
}

QImage to_qimage(const std::vector<std::uint8_t> &rgba, qsizetype w, qsizetype h)
{
    if (w <= 0 || h <= 0)
        return QImage();
    QImage img(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGBA8888);
    for (qsizetype y = 0; y < h; ++y)
        std::memcpy(img.scanLine(static_cast<int>(y)), rgba.data() + y * w * 4, static_cast<size_t>(w) * 4);
    return img;
}

QString splitext_base(const QString &p)          // os.path.splitext(p)[0]
{
    const qsizetype sep = std::max(p.lastIndexOf(u'/'), p.lastIndexOf(u'\\'));
    const qsizetype dot = p.lastIndexOf(u'.');
    if (dot > sep)
        for (qsizetype i = sep + 1; i < dot; ++i)
            if (p[i] != u'.')
                return p.left(dot);
    return p;
}
}  // namespace

std::optional<QImage> load_dds(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray d = f.readAll();
    if (d.left(4) != QByteArray("DDS ") || d.size() < 128)
        return std::nullopt;
    const std::uint32_t h = u32(d, 12), w = u32(d, 16);
    const std::uint32_t pf_flags = u32(d, 80);
    const QByteArray fourcc = d.mid(84, 4);
    const std::uint32_t bits = u32(d, 88), rm = u32(d, 92), gm = u32(d, 96), bm = u32(d, 100), am = u32(d, 104);
    const auto *body = reinterpret_cast<const unsigned char *>(d.constData()) + 128;
    const qsizetype len = d.size() - 128;
    std::vector<std::uint8_t> rgba;
    try {
        if ((pf_flags & 0x4) && (fourcc == "DXT1" || fourcc == "DXT3" || fourcc == "DXT5")) {
            rgba = decode_dxt(body, len, w, h, fourcc);
        } else if ((pf_flags & 0x40) && (bits == 16 || bits == 24 || bits == 32)) {
            const std::uint32_t masks[4] = {rm, gm, bm, (pf_flags & 0x1) ? am : 0u};
            rgba = decode_plain(body, len, w, h, bits, masks);
        } else {
            return std::nullopt;
        }
    } catch (const Fail &) {
        return std::nullopt;
    }
    return to_qimage(rgba, w, h);
}

std::optional<QImage> load_texture(const QString &path)
{
    // A game texture: .dds through load_dds, anything else through Qt. The game also finds a .dds when the .gfx
    // names a .tga (and the other way round), so both are tried.
    const QString base = splitext_base(path);
    const QString ext = path.mid(base.size());
    QStringList cands = {path};
    for (const QString &e : {u".dds"_s, u".tga"_s, u".png"_s})
        if (e != py::lower(ext))
            cands << base + e;
    for (const QString &p : cands)
        if (fs::isfile(p)) {
            std::optional<QImage> img = py::lower(p).endsWith(u".dds") ? load_dds(p) : std::optional<QImage>(QImage(p));
            if (img && !img->isNull())
                return img;
        }
    return std::nullopt;
}

std::pair<std::optional<QString>, long long> _sprite(const moddata::Mod &mod, const QString &name)
{
    // (texturefile, noOfFrames) of a spriteType in the mod's or the base game's interface/*.gfx
    QStringList dirs = {pypath::join(mod.root, u"interface"_s)};
    if (mod.base && !mod.base->isEmpty())
        dirs << pypath::join(*mod.base, u"interface"_s);
    using V = OrderedMap<QString, QString>;
    std::function<std::optional<V>(const script::Nodes &)> walk = [&](const script::Nodes &nodes) -> std::optional<V> {
        for (const script::Node &n : nodes) {
            if (n.is_block) {
                if (n.has_key && n.key == u"spriteType") {
                    V v;
                    for (const script::Node &c : n.kids)
                        if (c.has_key && !c.key.isEmpty() && !c.is_block)
                            v.insert(c.key, c.value);
                    if (v.get(u"name"_s) && v.value(u"name"_s) == name)
                        return v;
                }
                if (auto r = walk(n.kids); r && !r->isEmpty())
                    return r;
            }
        }
        return std::nullopt;
    };
    for (const QString &d : dirs) {
        if (!fs::isdir(d))
            continue;
        QStringList fns = fs::listdir(d);
        std::sort(fns.begin(), fns.end());
        for (const QString &fn : fns) {
            if (!py::lower(fn).endsWith(u".gfx"))
                continue;
            QString text;
            try {
                text = script::read_text(pypath::join(d, fn));
            } catch (const fs::OSError &) {
                continue;
            }
            if (!text.contains(name))
                continue;
            const auto v = walk(script::parse(text));
            if (v && !v->isEmpty()) {
                long long frames = 1;
                try {
                    frames = py::to_int(v->value(u"noOfFrames"_s, u"1"_s));
                } catch (const py::ValueError &) {
                    frames = 1;
                }
                return {v->value(u"texturefile"_s, QString()), frames};
            }
        }
    }
    return {std::nullopt, 0};
}

const QList<QPixmap> &religion_icons(moddata::Mod &mod)
{
    if (mod.religion_icons)
        return *std::static_pointer_cast<QList<QPixmap>>(mod.religion_icons);
    auto out = std::make_shared<QList<QPixmap>>();
    auto [tex, frames] = _sprite(mod, u"GFX_icon_religion"_s);
    if (tex && !tex->isEmpty()) {
        const QString t = QString(*tex).replace(u"\\\\"_s, u"/"_s).replace(u'\\', u'/');
        const auto img = load_texture(mod.p(t.split(u'/')));
        if (img) {
            if (!frames)
                frames = std::max<long long>(1, py::floordiv(static_cast<long long>(img->width()),
                                                             std::max<long long>(1, img->height())));
            const long long fw = py::floordiv(static_cast<long long>(img->width()), frames);
            for (long long i = 0; i < frames; ++i)
                *out << QPixmap::fromImage(img->copy(static_cast<int>(i * fw), 0, static_cast<int>(fw), img->height()));
        }
    }
    mod.religion_icons = out;
    return *out;
}

}  // namespace ddsimage
