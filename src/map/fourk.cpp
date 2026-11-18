// fourk.py — line by line. numpy's float32 arithmetic is done in float, in numpy's order (fourk.h).
#include "map/fourk.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "grid/npmath.h"
#include "map/imgio.h"
#include "map/mapmarks.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

using namespace Qt::StringLiterals;

namespace fourk {

const int S = 2;
const long long PLAIN_LIMIT = 15LL * 1024 * 1024;
const QStringList PICTURE_EXT = {u".dds"_s, u".tga"_s, u".png"_s, u".bmp"_s, u".jpg"_s};
const QString REPORT = u"4K submod report.txt"_s;
const QStringList PICTURE_TREES = {u"gfx/interface"_s, u"gfx/pictures"_s};
const QString FONT_TREE = u"gfx/fonts"_s;
static const QSet<QString> GUI_XY = {u"position"_s, u"size"_s, u"bordersize"_s, u"offset"_s};   // blocks { x = .. y = .. }
static const QSet<QString> GUI_ONE = {u"maxwidth"_s, u"maxheight"_s, u"spacing"_s, u"step"_s};  // single numbers
static const OrderedMap<QString, std::pair<QSet<QString>, QSet<QString>>> GFX = {   // (blocks { x y }, single numbers)
    {u"corneredtilespritetype"_s, {{u"size"_s, u"bordersize"_s}, {}}},
    {u"progressbartype"_s, {{u"size"_s}, {}}},
    {u"linecharttype"_s, {{u"size"_s}, {u"linewidth"_s}}},
    {u"piecharttype"_s, {{}, {u"size"_s}}},
    {u"barcharttype"_s, {{u"size"_s}, {}}},
    {u"scrollingsprite"_s, {{u"size"_s}, {u"step"_s}}},
    {u"tilespritetype"_s, {{u"size"_s}, {}}}};
static QSet<QString> sprites_2d()
{
    QSet<QString> s = {u"spritetype"_s, u"textspritetype"_s, u"maskedshieldtype"_s};
    for (const auto &kv : GFX)
        s.insert(kv.first);
    return s;
}
static const QSet<QString> SPRITES_2D = sprites_2d();
// The currency sign in a text is one of these sprites (see SMALL_BASE): {slot: (the sprite whose picture it gets, its
// side in doubled pixels — None = just doubled)}.
static const std::vector<std::tuple<QString, QString, std::optional<long long>>> MONEY_ICONS = {
    {u"gfx_tooltip_money"_s, u"gfx_tooltip_money"_s, std::nullopt},
    {u"gfx_tooltip_money_small"_s, u"gfx_tooltip_money"_s, std::nullopt},
    {u"gfx_tooltip_money_big"_s, u"gfx_tooltip_money_big"_s, 56}};
const QStringList TEXTURE_KEYS = {u"texturefile"_s, u"texturefile1"_s, u"texturefile2"_s};
static const auto RX = QRegularExpression::UseUnicodePropertiesOption;
bool NUM(const QString &s)
{
    static const QRegularExpression re(uR"(^[+-]?(\d+\.?\d*|\.\d+)$)"_s, RX);
    return re.match(s).hasMatch();
}
static QString low(const OptStr &s) { return s ? py::lower(*s) : QString(); }

// ------------------------------------------------------------------------------------------------ the game's files
QStringList Files::roots(const QString &sub) const
{
    QStringList out;
    if (mod.base && !mod.base->isEmpty() && !mod.replaced(sub.split(u'/')))
        out << *mod.base;
    out << mod.root;
    return out;
}

static void walk(const QString &dir, const std::function<void(const QString &)> &file)
{
    // os.walk, top-down: the folder's files, then each folder in it
    std::vector<fs::DirEntry> es;
    try {
        es = fs::scandir(dir);
    } catch (const std::exception &) {
        return;
    }
    std::vector<QString> dirs;
    for (const fs::DirEntry &e : es)
        if (e.is_dir)
            dirs.push_back(e.path);
        else
            file(e.path);
    for (const QString &d : dirs)
        walk(d, file);
}

OrderedMap<QString, QString> Files::tree(const QString &sub) const
{
    OrderedMap<QString, QString> out;
    for (const QString &root : roots(sub)) {         // the mod last: it wins
        const QString top = pypath::join(root, sub.split(u'/'));
        walk(top, [&](const QString &full) {
            out.insert(py::lower(pypath::relpath(full, root).replace(u'\\', u'/')), full);
        });
    }
    return out;
}

std::optional<std::pair<QString, QString>> Files::find(const QString &rel0) const
{
    // A picture a script names: as named, else with the other ending (the game takes .dds for .tga and the other way
    // round) -> (path below the game folder, lower case; file) or None.
    const QString rel = py::lower(py::strip(QString(rel0).replace(u"\\\\"_s, u"/"_s).replace(u'\\', u'/')));
    const QString stem = pypath::splitext_stem(rel), ext = rel.mid(stem.size());
    for (const QString &cand : {rel, stem + (ext == u".dds" ? u".tga"_s : u".dds"_s)}) {
        const QString full = mod.p(cand.split(u'/'));
        if (fs::isfile(full))
            return std::make_pair(cand, full);
    }
    return std::nullopt;
}

QString Files::true_rel(const QString &full) const
{
    // The path below its root, with the capitals the file really has.
    for (const OptStr &root : {OptStr(mod.root), mod.base})
        if (root && !root->isEmpty() && pypath::normcase(full).startsWith(pypath::normcase(*root) + u'\\'))
            return pypath::relpath(full, *root);
    throw PyError(u"ValueError"_s, full);
}

// ---------------------------------------------------------------------------------------------- .gui / .gfx texts
QString times(const QString &v, std::optional<double> factor, bool whole)
{
    const double f = *py::to_float(v) * (factor ? *factor : double(S));
    if (whole)                                       // str(int(round(f))): half to even
        return QString::number(static_cast<long long>(std::nearbyint(f)));
    if (f == std::floor(f) && std::isfinite(f))
        return QString::number(static_cast<long long>(f));
    QString s = py::fmt_f(f, 4);
    while (s.endsWith(u'0'))
        s.chop(1);
    while (s.endsWith(u'.'))
        s.chop(1);
    return s;
}

OptStr val_of(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name && !c.is_block)
            return c.value;
    return std::nullopt;
}
static const script::Node *kid_of(const script::Node &node, const QString &name)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name)
            return &c;
    return nullptr;
}
static QString key_of(const script::Node &n) { return n.has_key ? py::lower(n.key) : QString(); }

Scaled scale_script(const QString &text, bool is_gui, std::optional<double> factor, bool whole)
{
    auto times = [&](const QString &v) { return fourk::times(v, factor, whole); };
    const script::Nodes tree = script::parse(text);
    std::vector<std::tuple<qsizetype, qsizetype, QString>> edits;
    Scaled res;
    auto num = [&](const script::Node &n) {
        return !n.is_block && NUM(n.value) && text.mid(n.vstart, n.vend - n.vstart) == n.value;
    };
    auto xy = [&](const script::Node &block) {
        for (const script::Node &n : block.kids)
            if (n.has_key && !n.key.isEmpty() && (key_of(n) == u"x" || key_of(n) == u"y") && num(n))
                edits.emplace_back(n.vstart, n.vend, times(n.value));
    };
    auto add_frame = [&](const QString &t, long long n) {
        if (auto *s = res.frames.get(t))
            s->insert(n);
        else
            res.frames.insert(t, {n});
    };
    auto place = [&](const script::Node &block) {
        // A positionType: the game's code reads its x / y as a start point or a step in pixels — or as a COUNT (how
        // many entries per column, after how many to wrap). The name says which.
        const QString name = low(val_of(block, u"name"_s));
        const script::Node *pos = kid_of(block, u"position"_s);
        if (!pos || !pos->is_block || name.contains(u"wrap_count"))
            return;                                  // (x = entries per row, y not used)
        for (const script::Node &n : pos->kids)
            if (n.has_key && !n.key.isEmpty() && (key_of(n) == u"x" || key_of(n) == u"y") && num(n)) {
                if (key_of(n) == u"x" && (name.contains(u"countpercol_and_") || name.contains(u"countperrow_and_")))
                    continue;                        // (x = a count, y = the offset in pixels)
                edits.emplace_back(n.vstart, n.vend, times(n.value));
            }
    };
    std::function<void(const script::Nodes &)> gui = [&](const script::Nodes &nodes) {
        for (const script::Node &n : nodes) {
            const QString k = key_of(n);
            if (n.is_block) {
                if (k == u"positiontype")
                    place(n);
                else if (GUI_XY.contains(k))
                    xy(n);
                else
                    gui(n.kids);
            } else if (GUI_ONE.contains(k) && num(n))
                edits.emplace_back(n.vstart, n.vend, times(n.value));
        }
    };
    std::function<void(const script::Nodes &)> textures = [&](const script::Nodes &nodes) {
        for (const script::Node &n : nodes)
            if (n.is_block)
                textures(n.kids);
            else if (n.has_key && !n.key.isEmpty() && TEXTURE_KEYS.contains(py::lower(n.key)) && !n.value.isEmpty())
                add_frame(n.value, 1);
    };
    auto gfx = [&](const script::Nodes &nodes) {
        for (const script::Node &top : nodes) {
            if (!top.is_block)
                continue;
            const QString tk = key_of(top);
            if (tk == u"fonts") {                    // system fonts (FPS, lists): their height
                for (const script::Node &f : top.kids) {
                    const script::Node *h = f.is_block ? kid_of(f, u"height"_s) : nullptr;
                    if (f.is_block && low(val_of(f, u"name"_s)).contains(u"loading"))
                        continue;                    // (the game scales its loading screen to the screen itself)
                    if (h && num(*h))
                        edits.emplace_back(h->vstart, h->vend, times(h->value));
                }
                continue;
            }
            if (tk != u"spritetypes")
                continue;                            // (objectTypes: things on the map, sized in map units)
            for (const script::Node &sp : top.kids) {
                if (!sp.is_block)
                    continue;
                const QString k = key_of(sp);
                if (SPRITES_2D.contains(k)) {
                    const OptStr n = val_of(sp, u"noofframes"_s);
                    for (const QString &key : TEXTURE_KEYS) {
                        const OptStr t = val_of(sp, key);
                        if (t && !t->isEmpty())
                            add_frame(*t, n && !n->isEmpty() && NUM(*n) ? py::trunc_int(*py::to_float(*n)) : 1);
                    }
                }
                const auto *bo = GFX.get(k);
                for (const script::Node &a : sp.kids) {
                    const QString ak = key_of(a);
                    if (a.is_block && bo && bo->first.contains(ak))
                        xy(a);
                    else if (!a.is_block && bo && bo->second.contains(ak) && num(a))
                        edits.emplace_back(a.vstart, a.vend, times(a.value));
                }
            }
        }
    };
    if (is_gui) {
        gui(tree);
        textures(tree);                              // the pictures behind text boxes and buttons named in a .gui
    } else
        gfx(tree);
    std::sort(edits.begin(), edits.end(), std::greater<>());
    QString out = text;
    for (const auto &[s, e, fresh] : edits)
        out = out.left(s) + fresh + out.mid(e);
    res.text = out;
    res.count = static_cast<long long>(edits.size());
    return res;
}

OptStr check_script(const QString &old, const QString &fresh)
{
    // The scaled text read again: same structure, and every number either untouched or exactly doubled.
    using Item = std::pair<OptStr, OptStr>;
    std::function<void(const script::Nodes &, std::vector<Item> &)> flat = [&](const script::Nodes &nodes,
                                                                                std::vector<Item> &out) {
        for (const script::Node &n : nodes) {
            out.emplace_back(n.has_key ? OptStr(n.key) : std::nullopt, n.is_block ? std::nullopt : OptStr(n.value));
            if (n.is_block)
                flat(n.kids, out);
        }
    };
    std::vector<Item> a, b;
    flat(script::parse(old), a);
    flat(script::parse(fresh), b);
    auto s = [](const OptStr &x) { return x ? *x : u"None"_s; };
    if (a.size() != b.size())
        return u"structure changed (%1 -> %2 values)"_s.arg(a.size()).arg(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        const auto &[ka, va] = a[i];
        const auto &[kb, vb] = b[i];
        if (ka != kb || va.has_value() != vb.has_value())
            return u"structure changed at %1"_s.arg(s(ka));
        if (va != vb && !(NUM(va.value_or(QString())) && NUM(vb.value_or(QString())) &&
                          std::fabs(*py::to_float(*vb) - *py::to_float(*va) * S) < 1e-6))
            return u"%1: %2 -> %3"_s.arg(s(ka), s(va), s(vb));
    }
    return std::nullopt;
}

// -------------------------------------------------------------------------------------------- pictures: scaling
Pic Pic::part(long long y0, long long y1, long long x0, long long x1) const
{
    y1 = std::min(y1, h);
    x1 = std::min(x1, w);
    Pic out(std::max(0LL, y1 - y0), std::max(0LL, x1 - x0), ch);
    for (long long y = 0; y < out.h; ++y)
        std::memcpy(&out.d[size_t(y * out.w * ch)], &d[size_t(((y0 + y) * w + x0) * ch)], size_t(out.w * ch));
    return out;
}

Pic Pic::channels(int from, int n) const
{
    Pic out(h, w, n);
    for (long long i = 0; i < h * w; ++i)
        for (int c = 0; c < n; ++c)
            out.d[size_t(i * n + c)] = d[size_t(i * ch + from + c)];
    return out;
}

static double cubic(double x)
{
    x = std::fabs(x);
    const double a = -0.5;
    return x < 1.0 ? ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0 : x < 2.0 ? (((x - 5.0) * x + 8.0) * x - 4.0) * a : 0.0;
}

namespace {
struct Coeffs {
    std::vector<std::array<long long, 4>> j;
    std::vector<std::array<float, 4>> w;
};
Coeffs coeffs(long long n)
{
    // For doubling a line of n pixels: for each of the 2n new pixels the 4 old ones it is made of and their weights
    // (bicubic; at the ends the pixels that don't exist are left out and the rest weigh more).
    static std::mutex m;
    static std::map<long long, Coeffs> cache;
    {
        std::lock_guard<std::mutex> g(m);
        if (auto it = cache.find(n); it != cache.end())
            return it->second;
    }
    Coeffs got;
    got.j.resize(size_t(n * S));
    got.w.resize(size_t(n * S));
    for (long long X = 0; X < n * S; ++X) {
        const double c = (double(X) + 0.5) / S;
        const long long base = static_cast<long long>(std::floor(c - 1.5));
        double w[4];
        for (int k = 0; k < 4; ++k) {
            const long long j = base + k;
            w[k] = (j < 0 || j >= n) ? 0.0 : cubic(double(j) + 0.5 - c);
            got.j[size_t(X)][size_t(k)] = std::clamp(j, 0LL, n - 1);
        }
        const double sum = 0.0 + w[0] + w[1] + w[2] + w[3];
        for (int k = 0; k < 4; ++k)
            got.w[size_t(X)][size_t(k)] = static_cast<float>(w[k] / sum);
    }
    std::lock_guard<std::mutex> g(m);
    if (cache.size() > 400)
        cache.clear();
    cache[n] = got;
    return got;
}
inline float round_clip(float v)                     // np.clip(np.floor(out + 0.5), 0, 255) in float32
{
    return std::clamp(std::floor(v + 0.5f), 0.0f, 255.0f);
}
}  // namespace

Pic up(const Pic &a)
{
    // (h, w, c) uint8 -> (2h, 2w, c) uint8, bicubic; rounded after each of the two passes.
    const long long h = a.h, w = a.w;
    const int ch = a.ch;
    const Coeffs cx = coeffs(w);
    std::vector<float> mid(size_t(h * w * S * ch));
    for (long long y = 0; y < h; ++y)
        for (long long X = 0; X < w * S; ++X) {
            const auto &j = cx.j[size_t(X)];
            const auto &wt = cx.w[size_t(X)];
            for (int c = 0; c < ch; ++c) {
                float acc = 0.0f;
                for (int k = 0; k < 4; ++k)
                    acc = acc + float(a.at(y, j[size_t(k)], c)) * wt[size_t(k)];
                mid[size_t((y * w * S + X) * ch + c)] = round_clip(acc);
            }
        }
    const Coeffs cy = coeffs(h);
    Pic out(h * S, w * S, ch);
    const long long W2 = w * S;
    for (long long Y = 0; Y < h * S; ++Y) {
        const auto &j = cy.j[size_t(Y)];
        const auto &wt = cy.w[size_t(Y)];
        for (long long X = 0; X < W2; ++X)
            for (int c = 0; c < ch; ++c) {
                float acc = 0.0f;
                for (int k = 0; k < 4; ++k)
                    acc = acc + mid[size_t((j[size_t(k)] * W2 + X) * ch + c)] * wt[size_t(k)];
                out.d[size_t((Y * W2 + X) * ch + c)] = static_cast<std::uint8_t>(round_clip(acc));
            }
    }
    return out;
}

Pic bigger(const Pic &rgba)
{
    // (h, w, 4) -> double size. Colour is weighed by opacity (no dark or bright fringes at see-through edges); where
    // the result is fully see-through the plainly scaled colour is kept — some textures carry colour there.
    const long long n = rgba.h * rgba.w;
    int lo = 255, hi = 0;
    for (long long i = 0; i < n; ++i) {
        lo = std::min<int>(lo, rgba.d[size_t(i * 4 + 3)]);
        hi = std::max<int>(hi, rgba.d[size_t(i * 4 + 3)]);
    }
    if (n == 0)
        throw PyError(u"ValueError"_s, u"zero-size array to reduction operation minimum which has no identity"_s);
    Pic out(rgba.h * S, rgba.w * S, 4);
    if (lo == hi) {                                  // one opacity everywhere: nothing to weigh
        const Pic u = up(rgba.channels(0, 3));
        for (long long i = 0; i < out.h * out.w; ++i) {
            for (int c = 0; c < 3; ++c)
                out.d[size_t(i * 4 + c)] = u.d[size_t(i * 3 + c)];
            out.d[size_t(i * 4 + 3)] = std::uint8_t(lo);
        }
        return out;
    }
    Pic pm(rgba.h, rgba.w, 4);
    bool colour_under_clear = false;
    for (long long i = 0; i < n; ++i) {
        const std::uint32_t al = rgba.d[size_t(i * 4 + 3)];
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t v = std::uint32_t(rgba.d[size_t(i * 4 + c)]) * al + 128;
            pm.d[size_t(i * 4 + c)] = std::uint8_t((v + (v >> 8)) >> 8);
            if (al == 0 && rgba.d[size_t(i * 4 + c)])
                colour_under_clear = true;
        }
        pm.d[size_t(i * 4 + 3)] = std::uint8_t(al);
    }
    const Pic u = up(pm);
    bool any_clear = false;
    for (long long i = 0; i < out.h * out.w; ++i) {
        const std::uint32_t a = u.d[size_t(i * 4 + 3)];
        const std::uint32_t safe = std::max<std::uint32_t>(a, 1);
        for (int c = 0; c < 3; ++c)
            out.d[size_t(i * 4 + c)] = std::uint8_t(std::min<std::uint32_t>(255, std::uint32_t(u.d[size_t(i * 4 + c)]) * 255 / safe));
        out.d[size_t(i * 4 + 3)] = std::uint8_t(a);
        any_clear = any_clear || a == 0;
    }
    if (any_clear && colour_under_clear) {
        const Pic plain = up(rgba.channels(0, 3));
        for (long long i = 0; i < out.h * out.w; ++i)
            if (out.d[size_t(i * 4 + 3)] == 0)
                for (int c = 0; c < 3; ++c)
                    out.d[size_t(i * 4 + c)] = plain.d[size_t(i * 3 + c)];
    } else if (any_clear) {
        for (long long i = 0; i < out.h * out.w; ++i)
            if (out.d[size_t(i * 4 + 3)] == 0)
                for (int c = 0; c < 3; ++c)
                    out.d[size_t(i * 4 + c)] = 0;
    }
    return out;
}

QImage to_qimage(const Pic &rgba)
{
    return QImage(rgba.d.data(), int(rgba.w), int(rgba.h), int(rgba.w * 4), QImage::Format_RGBA8888).copy();
}

Pic qimage_rgba(const QImage &img0)
{
    const QImage img = img0.convertToFormat(QImage::Format_RGBA8888);
    Pic out(img.height(), img.width(), 4);
    for (long long y = 0; y < out.h; ++y)
        std::memcpy(&out.d[size_t(y * out.w * 4)], img.constScanLine(int(y)), size_t(out.w * 4));
    return out;
}

Pic resized(const Pic &rgba, long long w, long long h)
{
    // A picture at another size (smooth; colour weighed by opacity).
    QImage img = to_qimage(rgba).convertToFormat(QImage::Format_RGBA8888_Premultiplied)
                     .scaled(int(w), int(h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return qimage_rgba(img);
}

Pic scale_frames(const Pic &rgba, long long frames)
{
    // A strip of frames: each one on its own, nothing bleeds over into its neighbour.
    if (frames > 1 && rgba.w % frames == 0) {
        const long long fw = rgba.w / frames;
        Pic out(rgba.h * S, rgba.w * S, 4);
        for (long long i = 0; i < frames; ++i) {
            const Pic b = bigger(rgba.part(0, rgba.h, i * fw, (i + 1) * fw));
            for (long long y = 0; y < b.h; ++y)
                std::memcpy(&out.d[size_t((y * out.w + i * fw * S) * 4)], &b.d[size_t(y * b.w * 4)], size_t(b.w * 4));
        }
        return out;
    }
    return bigger(rgba);
}

// -------------------------------------------------------------------------------------------- pictures: reading
namespace {
std::uint32_t u32(const QByteArray &d, long long at)
{
    if (at < 0 || at + 4 > d.size())
        throw PyError(u"error"_s, u"unpack_from requires a buffer of at least %1 bytes for unpacking 4 bytes at offset %2 "
                                  "(actual buffer size is %3)"_s.arg(at + 4).arg(at).arg(d.size()));
    std::uint32_t v;
    std::memcpy(&v, d.constData() + at, 4);
    return v;
}
std::uint16_t u16(const QByteArray &d, long long at)
{
    if (at < 0 || at + 2 > d.size())
        throw PyError(u"error"_s, u"unpack_from requires a buffer of at least %1 bytes for unpacking 2 bytes at offset %2 "
                                  "(actual buffer size is %3)"_s.arg(at + 2).arg(at).arg(d.size()));
    std::uint16_t v;
    std::memcpy(&v, d.constData() + at, 2);
    return v;
}
std::optional<std::vector<std::uint32_t>> mask_channel(const std::vector<std::uint32_t> &v, std::uint32_t mask)
{
    if (!mask)
        return std::nullopt;
    const int shift = std::countr_zero(mask);
    const std::uint32_t top = mask >> shift;
    std::vector<std::uint32_t> out(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        out[i] = std::uint32_t(((v[i] >> shift) & top) * 255u) / top;   // (uint32, as numpy: may wrap)
    return out;
}
QByteArray read_all(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw PyError(u"FileNotFoundError"_s, u"[Errno 2] No such file or directory: %1"_s.arg(py::repr_str(path)));
    return f.readAll();
}
}  // namespace

std::pair<Pic, Info> read_picture(const QString &path)
{
    // -> ((h, w, 4), info) or raises.
    const QByteArray data = read_all(path);
    if (data.startsWith("DDS ")) {
        const long long h = u32(data, 12), w = u32(data, 16);
        const std::uint32_t flags = u32(data, 80);
        u32(data, 84);
        const QByteArray fourcc = data.mid(84, 4);
        const std::uint32_t bits = u32(data, 88), rm = u32(data, 92), gm = u32(data, 96), bm = u32(data, 100),
                            am = u32(data, 104);
        if (flags & 4) {
            QString pack = QString::fromLatin1(fourcc);
            if (pack == u"DXT2")
                pack = u"DXT3"_s;
            else if (pack == u"DXT4")
                pack = u"DXT5"_s;
            const auto img = imgio::read_dds(path);
            if (!img || !(pack == u"DXT1" || pack == u"DXT3" || pack == u"DXT5"))
                throw PyError(u"ValueError"_s, u"a .dds packing this editor does not read: "_s + QString::fromLatin1(fourcc));
            return {qimage_rgba(*img), Info{u"dds"_s, pack, true, false, std::nullopt}};
        }
        const long long n = bits / 8;
        if ((flags & 0x20) && n == 1 && data.size() >= 128 + 1024 + w * h) {   // 256 colours: a table, then one byte a pixel
            Pic rgba(h, w, 4);
            const auto *pal = reinterpret_cast<const std::uint8_t *>(data.constData()) + 128;
            const auto *idx = pal + 1024;
            for (long long i = 0; i < w * h; ++i) {
                for (int c = 0; c < 3; ++c)
                    rgba.d[size_t(i * 4 + c)] = pal[idx[i] * 4 + c];
                rgba.d[size_t(i * 4 + 3)] = 255;
            }
            return {rgba, Info{u"dds"_s, std::nullopt, false, false, std::nullopt}};
        }
        if (!(n >= 1 && n <= 4) || data.size() < 128 + w * h * n)
            throw PyError(u"ValueError"_s, u"a .dds layout this editor does not read (%1 bits)"_s.arg(bits));
        std::vector<std::uint32_t> v(size_t(w * h), 0);
        const auto *px = reinterpret_cast<const std::uint8_t *>(data.constData()) + 128;
        for (long long i = 0; i < w * h; ++i)
            for (long long k = 0; k < n; ++k)
                v[size_t(i)] |= std::uint32_t(px[i * n + k]) << (8 * k);
        Pic rgba(h, w, 4);
        bool grey = (flags & 0x20000) || (rm && !gm && !bm) || (n == 1 && !am);
        if (grey) {
            const auto lum = *mask_channel(v, rm ? rm : 0xFF);
            for (long long i = 0; i < w * h; ++i)
                rgba.d[size_t(i * 4)] = rgba.d[size_t(i * 4 + 1)] = rgba.d[size_t(i * 4 + 2)] = std::uint8_t(lum[size_t(i)]);
        } else {
            const std::uint32_t ms[3] = {rm, gm, bm};
            for (int c = 0; c < 3; ++c) {
                const auto ch = mask_channel(v, ms[c]);
                for (long long i = 0; i < w * h; ++i)
                    rgba.d[size_t(i * 4 + c)] = ch ? std::uint8_t((*ch)[size_t(i)]) : 0;
            }
        }
        const auto a = ((flags & 1) || (flags & 2)) && am ? mask_channel(v, am) : std::nullopt;
        for (long long i = 0; i < w * h; ++i)
            rgba.d[size_t(i * 4 + 3)] = a ? std::uint8_t((*a)[size_t(i)]) : 255;
        if (n == 1 && am && !rm) {                   // alpha only
            for (long long i = 0; i < w * h; ++i)
                rgba.d[size_t(i * 4)] = rgba.d[size_t(i * 4 + 1)] = rgba.d[size_t(i * 4 + 2)] = 0;
            grey = false;
        }
        return {rgba, Info{u"dds"_s, std::nullopt, a.has_value(), grey, std::nullopt}};
    }
    const QString kind = data.startsWith("\x89PNG\r\n\x1a\n") ? u"png"_s : data.startsWith("\xff\xd8") ? u"jpg"_s
                         : data.startsWith("BM")                 ? u"bmp"_s
                                                                 : u"tga"_s;
    if (kind == u"tga") {
        auto img = imgio::read_tga(path);
        if (!img) {
            img = QImage(path);
            if (img->isNull())
                throw PyError(u"ValueError"_s, u"a .tga layout this editor does not read"_s);
        }
        const int bits = data.size() > 17 ? std::uint8_t(data[16]) : 32;
        if (data.size() <= 2)
            throw PyError(u"IndexError"_s, u"index out of range"_s);
        const int t = std::uint8_t(data[2]);
        return {qimage_rgba(*img), Info{u"tga"_s, std::nullopt, std::nullopt, t == 3 || t == 11, bits == 32 ? 32 : 24}};
    }
    const QImage img = QImage::fromData(data);
    if (img.isNull())
        throw PyError(u"ValueError"_s, u"not a picture this editor reads"_s);
    return {qimage_rgba(img), Info{kind, std::nullopt, img.hasAlphaChannel(), std::nullopt, std::nullopt}};
}

// -------------------------------------------------------------------------------------------- pictures: writing
namespace {
void put32(QByteArray &b, std::uint32_t v) { b.append(reinterpret_cast<const char *>(&v), 4); }
void put16(QByteArray &b, std::uint16_t v) { b.append(reinterpret_cast<const char *>(&v), 2); }
void put8(QByteArray &b, std::uint8_t v) { b.append(char(v)); }
}  // namespace

QByteArray dds_head(long long w, long long h, const OptStr &pack, long long bits, bool alpha, bool grey, long long size)
{
    QByteArray head;
    if (pack && !pack->isEmpty()) {
        for (std::uint32_t v : {124u, 0x1u | 0x2u | 0x4u | 0x1000u | 0x80000u, std::uint32_t(h), std::uint32_t(w),
                                std::uint32_t(size), 0u, 0u})
            put32(head, v);
        head.append(44, '\0');
        put32(head, 32);
        put32(head, 0x4);
        head.append(pack->toLatin1().leftJustified(4, '\0', true));
        for (int i = 0; i < 5; ++i)
            put32(head, 0);
    } else {
        for (std::uint32_t v : {124u, 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u, std::uint32_t(h), std::uint32_t(w),
                                std::uint32_t((w * bits + 7) / 8), 0u, 0u})
            put32(head, v);
        head.append(44, '\0');
        if (grey && bits == 16)                      // grey + see-through (A8L8): the alpha in the high byte
            for (std::uint32_t v : {32u, 0x20001u, 0u, 16u, 0xFFu, 0u, 0u, 0xFF00u})
                put32(head, v);
        else if (grey)
            for (std::uint32_t v : {32u, 0x20000u, 0u, 8u, 0xFFu, 0u, 0u, 0u})
                put32(head, v);
        else
            for (std::uint32_t v : {32u, alpha ? 0x41u : 0x40u, 0u, std::uint32_t(bits), 0x00FF0000u, 0x0000FF00u,
                                    0x000000FFu, alpha ? 0xFF000000u : 0u})
                put32(head, v);
    }
    QByteArray out("DDS ");
    out += head;
    for (std::uint32_t v : {0x1000u, 0u, 0u, 0u, 0u})
        put32(out, v);
    return out;
}

namespace {
using Block = std::array<std::array<std::uint8_t, 4>, 16>;   // 16 pixels (rows of 4), up to 4 channels

std::vector<Block> blocks_of(const Pic &a)
{
    // (h, w, c) -> blocks of 4 x 4 pixels, rows of blocks top to bottom; the picture widened to a multiple of 4 by
    // repeating its last row / column
    const long long H = (a.h + 3) / 4 * 4, W = (a.w + 3) / 4 * 4;
    std::vector<Block> out;
    out.reserve(size_t(H / 4 * W / 4));
    for (long long by = 0; by < H / 4; ++by)
        for (long long bx = 0; bx < W / 4; ++bx) {
            Block b{};
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const long long sy = std::min(by * 4 + y, a.h - 1), sx = std::min(bx * 4 + x, a.w - 1);
                    for (int c = 0; c < a.ch; ++c)
                        b[size_t(y * 4 + x)][size_t(c)] = a.at(sy, sx, c);
                }
            out.push_back(b);
        }
    return out;
}

using F3 = std::array<float, 3>;
std::uint16_t q565(const F3 &c0)
{
    F3 c;
    for (int k = 0; k < 3; ++k)
        c[size_t(k)] = std::clamp(c0[size_t(k)], 0.0f, 255.0f);
    const auto r = static_cast<std::uint16_t>(std::floor(c[0] * 31.0f / 255.0f + 0.5f));
    const auto g = static_cast<std::uint16_t>(std::floor(c[1] * 63.0f / 255.0f + 0.5f));
    const auto b = static_cast<std::uint16_t>(std::floor(c[2] * 31.0f / 255.0f + 0.5f));
    return std::uint16_t((r << 11) | (g << 5) | b);
}
F3 un565(std::uint16_t c)
{
    const float r = float((c >> 11) & 31), g = float((c >> 5) & 63), b = float(c & 31);
    return {std::floor((r * 255.0f + 15.0f) / 31.0f), std::floor((g * 255.0f + 31.0f) / 63.0f),
            std::floor((b * 255.0f + 15.0f) / 31.0f)};
}
struct Assigned {
    std::array<int, 16> sel;
    std::array<F3, 4> pal;
};
Assigned assign(const std::array<F3, 16> &px, std::uint16_t c0, std::uint16_t c1)
{
    Assigned a;
    const F3 p0 = un565(c0), p1 = un565(c1);
    a.pal[0] = p0;
    a.pal[1] = p1;
    for (int k = 0; k < 3; ++k) {
        a.pal[2][size_t(k)] = std::floor((2.0f * p0[size_t(k)] + p1[size_t(k)]) / 3.0f);
        a.pal[3][size_t(k)] = std::floor((p0[size_t(k)] + 2.0f * p1[size_t(k)]) / 3.0f);
    }
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        float bd = 0;
        for (int m = 0; m < 4; ++m) {
            float d = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float e = px[size_t(i)][size_t(k)] - a.pal[size_t(m)][size_t(k)];
                d = d + e * e;
            }
            if (m == 0 || d < bd) {
                bd = d;
                best = m;
            }
        }
        a.sel[size_t(i)] = best;
    }
    return a;
}
float err_of(const std::array<F3, 16> &px, const Assigned &a)
{
    float e[48];
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) {
            const float d = px[size_t(i)][size_t(k)] - a.pal[size_t(a.sel[size_t(i)])][size_t(k)];
            e[i * 3 + k] = d * d;
        }
    return 0.0f + grid::pairwise_sum_f(e, 48);
}

std::array<std::uint8_t, 8> dxt_colour(const Block &blk)
{
    // two colours of 16 bits and 2 bits a pixel. The two colours are the ends of the pixels' main direction, set once
    // more by what the 4 steps between them can best show.
    std::array<F3, 16> px;
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k)
            px[size_t(i)][size_t(k)] = float(blk[size_t(i)][size_t(k)]);
    F3 mean;
    for (int k = 0; k < 3; ++k) {
        float s = 0.0f;
        for (int i = 0; i < 16; ++i)
            s = s + px[size_t(i)][size_t(k)];
        mean[size_t(k)] = s / 16.0f;
    }
    std::array<F3, 16> d;
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k)
            d[size_t(i)][size_t(k)] = px[size_t(i)][size_t(k)] - mean[size_t(k)];
    float cov[3][3];
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k) {
            float s = 0.0f;
            for (int i = 0; i < 16; ++i)
                s = s + d[size_t(i)][size_t(j)] * d[size_t(i)][size_t(k)];
            cov[j][k] = s;
        }
    F3 v = {1.0f, 1.0f, 1.0f};
    for (int it = 0; it < 6; ++it) {                 // the main direction of each block's colours
        F3 nv;
        for (int j = 0; j < 3; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
                s = s + cov[j][k] * v[size_t(k)];
            nv[size_t(j)] = s;
        }
        float sq = 0.0f;
        for (int k = 0; k < 3; ++k)
            sq = sq + nv[size_t(k)] * nv[size_t(k)];
        const float nrm = std::max(std::sqrt(sq), 1e-9f);
        for (int k = 0; k < 3; ++k)
            v[size_t(k)] = nv[size_t(k)] / nrm;
    }
    float tmax = 0, tmin = 0;
    for (int i = 0; i < 16; ++i) {
        float s = 0.0f;
        for (int j = 0; j < 3; ++j)
            s = s + d[size_t(i)][size_t(j)] * v[size_t(j)];
        if (i == 0 || s > tmax)
            tmax = i == 0 ? s : std::max(tmax, s);
        if (i == 0 || s < tmin)
            tmin = i == 0 ? s : std::min(tmin, s);
    }
    F3 hi, lo;
    for (int k = 0; k < 3; ++k) {
        hi[size_t(k)] = mean[size_t(k)] + tmax * v[size_t(k)];
        lo[size_t(k)] = mean[size_t(k)] + tmin * v[size_t(k)];
    }
    std::uint16_t c0 = q565(hi), c1 = q565(lo);
    const Assigned first = assign(px, c0, c1);
    std::array<int, 16> sel = first.sel;
    // once more: the two ends that fit best when every pixel keeps the step it was given (least squares)
    static const float WA[4] = {1.0f, 0.0f, float(2.0 / 3.0), float(1.0 / 3.0)};
    float wa[16], wb[16], waa[16], wbb[16], wab[16];
    for (int i = 0; i < 16; ++i) {
        wa[i] = WA[sel[size_t(i)]];
        wb[i] = 1.0f - wa[i];
        waa[i] = wa[i] * wa[i];
        wbb[i] = wb[i] * wb[i];
        wab[i] = wa[i] * wb[i];
    }
    const float aa = 0.0f + grid::pairwise_sum_f(waa, 16), bb = 0.0f + grid::pairwise_sum_f(wbb, 16),
                ab = 0.0f + grid::pairwise_sum_f(wab, 16);
    const float det = aa * bb - ab * ab;
    const bool ok = det > 1e-4f;
    F3 ax, bx;
    for (int j = 0; j < 3; ++j) {
        float sa = 0.0f, sb = 0.0f;
        for (int i = 0; i < 16; ++i) {
            sa = sa + wa[i] * px[size_t(i)][size_t(j)];
            sb = sb + wb[i] * px[size_t(i)][size_t(j)];
        }
        ax[size_t(j)] = sa;
        bx[size_t(j)] = sb;
    }
    const float safe = ok ? det : 1.0f;
    F3 n0, n1;
    for (int j = 0; j < 3; ++j) {
        n0[size_t(j)] = (bb * ax[size_t(j)] - ab * bx[size_t(j)]) / safe;
        n1[size_t(j)] = (aa * bx[size_t(j)] - ab * ax[size_t(j)]) / safe;
    }
    const std::uint16_t r0 = ok ? q565(n0) : c0, r1 = ok ? q565(n1) : c1;
    const Assigned second = assign(px, r0, r1);
    const float e1 = err_of(px, first), e2 = err_of(px, second);
    if (e2 < e1) {
        c0 = r0;
        c1 = r1;
        sel = second.sel;
    }
    // the 4-step kind needs colour 0 to be the bigger number: swap (and the pixels' steps with it)
    if (c0 < c1) {
        std::swap(c0, c1);
        static const int SW[4] = {1, 0, 3, 2};
        for (int &s : sel)
            s = SW[s];
    }
    if (c0 == c1)
        sel.fill(0);
    std::uint32_t bits = 0;
    for (int i = 0; i < 16; ++i)
        bits += std::uint32_t(sel[size_t(i)]) << (2 * i);
    return {std::uint8_t(c0 & 255), std::uint8_t(c0 >> 8), std::uint8_t(c1 & 255), std::uint8_t(c1 >> 8),
            std::uint8_t(bits & 255), std::uint8_t((bits >> 8) & 255), std::uint8_t((bits >> 16) & 255),
            std::uint8_t((bits >> 24) & 255)};
}

std::array<std::uint8_t, 8> dxt5_alpha(const Block &blk)
{
    // highest and lowest opacity and 3 bits a pixel (8 steps between them)
    int a[16];
    for (int i = 0; i < 16; ++i)
        a[i] = blk[size_t(i)][3];
    const int a0 = *std::max_element(a, a + 16), a1 = *std::min_element(a, a + 16);
    int steps[8] = {a0, a1};
    for (int i = 1; i <= 6; ++i)
        steps[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        for (int m = 1; m < 8; ++m)
            if (std::abs(a[i] - steps[m]) < std::abs(a[i] - steps[best]))
                best = m;
        if (a0 == a1)
            best = 0;
        bits += std::uint64_t(best) << (3 * i);
    }
    std::array<std::uint8_t, 8> out{std::uint8_t(a0), std::uint8_t(a1)};
    for (int i = 0; i < 6; ++i)
        out[size_t(2 + i)] = std::uint8_t((bits >> (8 * i)) & 255);
    return out;
}

std::array<std::uint8_t, 8> dxt3_alpha(const Block &blk)
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i)
        bits += std::uint64_t((std::uint32_t(blk[size_t(i)][3]) + 8) / 17) << (4 * i);   // 4 bits a pixel
    std::array<std::uint8_t, 8> out{};
    for (int i = 0; i < 8; ++i)
        out[size_t(i)] = std::uint8_t((bits >> (8 * i)) & 255);
    return out;
}
}  // namespace

QByteArray dxt_bytes(const Pic &rgba, const QString &pack)
{
    // the packed pixel data of a DXT1 / DXT3 / DXT5 .dds (worked on in slices: memory stays small)
    QByteArray out;
    for (long long y = 0; y < rgba.h; y += 256) {
        const auto blk = blocks_of(rgba.part(y, y + 256, 0, rgba.w));
        for (const Block &b : blk) {
            const auto col = dxt_colour(b);
            if (pack != u"DXT1") {
                const auto al = pack == u"DXT5" ? dxt5_alpha(b) : dxt3_alpha(b);
                out.append(reinterpret_cast<const char *>(al.data()), 8);
            }
            out.append(reinterpret_cast<const char *>(col.data()), 8);
        }
    }
    return out;
}

static void write_bytes(const QString &path, const QByteArray &data)
{
    const QString target = fs::watched(path);
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size())
        throw fs::OSError(u"[Errno 13] Permission denied: %1"_s.arg(py::repr_str(path)).toStdString(), 13);
}

OptStr write_picture(const QString &path, const Pic &rgba, const Info &info)
{
    // The doubled picture as the same kind of file it was. -> a note for the report, or None.
    const long long h = rgba.h, w = rgba.w;
    const QString &kind = info.kind;
    OptStr note;
    fs::makedirs(pypath::dirname(path));
    QByteArray file;
    if (kind == u"dds") {
        OptStr pack = info.pack;
        bool alpha = info.alpha.value_or(true);
        int amin = 255;
        long long stray = 0;
        for (long long i = 0; i < h * w; ++i) {
            const int a = rgba.d[size_t(i * 4 + 3)];
            amin = std::min(amin, a);
            stray += a < 255;
        }
        const bool opaque = amin == 255;
        const bool grey = info.grey.value_or(false);
        // A grey picture with see-through (A8L8) keeps it (2026-11-08, not in the Python editor: written as plain
        // grey it lost its see-through, ZRM's Legacy's bg_mainshadow behind every menu came out solid white).
        const bool grey_alpha = grey && info.alpha.value_or(false);
        if (grey && !pack && w * h * (grey_alpha ? 2 : 1) > PLAIN_LIMIT) {
            pack = (grey_alpha && !opaque) ? u"DXT5"_s : u"DXT1"_s;
            note = u"too big as a plain picture for the game -> "_s + *pack;
        }
        if (pack == u"DXT1" && !opaque) {
            pack = u"DXT5"_s;
            note = u"DXT1 with see-through pixels -> DXT5"_s;
        }
        if (!pack && !grey) {
            long long n = alpha ? 4 : 3;
            if (w * h * n > PLAIN_LIMIT && n == 4) {
                // see-through in a stray handful of pixels only: written without alpha, if it then stays plain
                if (double(stray) <= double(w * h) * 0.00002 && w * h * 3 <= PLAIN_LIMIT) {
                    n = 3;
                    alpha = false;
                    note = u"written without its alpha (%1 stray see-through pixels) to stay a plain picture"_s.arg(stray);
                }
            }
            if (w * h * n > PLAIN_LIMIT) {
                pack = (n == 3 || opaque) ? u"DXT1"_s : u"DXT5"_s;
                note = u"too big as a plain picture for the game -> "_s + *pack;
            }
        }
        QByteArray data;
        QByteArray head;
        if (pack) {
            data = dxt_bytes(rgba, *pack);
            head = dds_head(w, h, pack, 32, true, false, data.size());
        } else if (grey_alpha) {
            data.resize(qsizetype(w * h * 2));
            for (long long i = 0; i < w * h; ++i) {
                data[qsizetype(i * 2)] = char(rgba.d[size_t(i * 4)]);
                data[qsizetype(i * 2 + 1)] = char(rgba.d[size_t(i * 4 + 3)]);
            }
            head = dds_head(w, h, std::nullopt, 16, true, true);
        } else if (grey) {
            data.resize(qsizetype(w * h));
            for (long long i = 0; i < w * h; ++i)
                data[qsizetype(i)] = char(rgba.d[size_t(i * 4)]);
            head = dds_head(w, h, std::nullopt, 8, true, true);
        } else if (alpha) {
            data.resize(qsizetype(w * h * 4));
            for (long long i = 0; i < w * h; ++i) {
                data[qsizetype(i * 4)] = char(rgba.d[size_t(i * 4 + 2)]);
                data[qsizetype(i * 4 + 1)] = char(rgba.d[size_t(i * 4 + 1)]);
                data[qsizetype(i * 4 + 2)] = char(rgba.d[size_t(i * 4)]);
                data[qsizetype(i * 4 + 3)] = char(rgba.d[size_t(i * 4 + 3)]);
            }
            head = dds_head(w, h, std::nullopt, 32, true);
        } else {
            data.resize(qsizetype(w * h * 3));
            for (long long i = 0; i < w * h; ++i) {
                data[qsizetype(i * 3)] = char(rgba.d[size_t(i * 4 + 2)]);
                data[qsizetype(i * 3 + 1)] = char(rgba.d[size_t(i * 4 + 1)]);
                data[qsizetype(i * 3 + 2)] = char(rgba.d[size_t(i * 4)]);
            }
            head = dds_head(w, h, std::nullopt, 24, false);
        }
        file = head + data;
    } else if (kind == u"tga") {
        const int n = info.bits == 32 ? 4 : 3;
        put8(file, 0);
        put8(file, 0);
        put8(file, 2);
        put16(file, 0);
        put16(file, 0);
        put8(file, 0);
        put16(file, 0);
        put16(file, 0);
        put16(file, std::uint16_t(w));
        put16(file, std::uint16_t(h));
        put8(file, std::uint8_t(n * 8));
        put8(file, n == 4 ? 8 : 0);
        for (long long y = h - 1; y >= 0; --y)       // bottom row first, blue first
            for (long long x = 0; x < w; ++x) {
                put8(file, rgba.at(y, x, 2));
                put8(file, rgba.at(y, x, 1));
                put8(file, rgba.at(y, x, 0));
                if (n == 4)
                    put8(file, rgba.at(y, x, 3));
            }
        file.append(8, '\0');
        file.append("TRUEVISION-XFILE.\0", 18);
    } else {
        QImage img = to_qimage(rgba);
        if (kind == u"jpg" || !info.alpha.value_or(true))
            img = img.convertToFormat(QImage::Format_RGB888);
        QBuffer qb(&file);
        qb.open(QIODevice::WriteOnly);
        img.save(&qb, kind == u"png" ? "PNG" : kind == u"jpg" ? "JPG" : "BMP", kind == u"jpg" ? 95 : -1);
        qb.close();
    }
    write_bytes(path, file);
    const QString base = pypath::basename(path);
    QString ext = py::lower(base.mid(pypath::splitext_stem(base).size()));
    while (ext.startsWith(u'.'))
        ext.remove(0, 1);
    if (ext != kind && !(ext == u"jpeg" && kind == u"jpg"))
        note = (note ? *note + u"; "_s : u""_s) + u"is a %1 inside"_s.arg(kind.toUpper());
    return note;
}

// --------------------------------------------------------------------------------------------------------- fonts
// "base" of a font: the game picks the currency icon of a text by it (below ~17 the first, below 30 the second, else
// the third) and hangs the icon from it. Doubled it puts the icon on the line; a font that had the first icon (base up
// to SMALL_BASE) stays below 30 so that it gets the second slot — which holds the first icon doubled.
const long long SMALL_BASE = 16, SLOT_LIMIT = 30;
static const OrderedMap<QString, QStringList> FNT_KEYS = {
    {u"common"_s, {u"lineHeight"_s, u"scaleW"_s, u"scaleH"_s}},
    {u"char"_s, {u"x"_s, u"y"_s, u"width"_s, u"height"_s, u"xoffset"_s, u"yoffset"_s, u"xadvance"_s}},
    {u"kerning"_s, {u"amount"_s}},
    {u"info"_s, {u"size"_s}}};

ScaledFnt scale_fnt(const QString &text)
{
    ScaledFnt res;
    res.sheet = {0, 0};
    QStringList out;
    static const QRegularExpression kv(uR"((\w+)=(-?\d+))"_s, RX);
    static const QRegularExpression base_re(uR"(\b(base=)(\d+))"_s, RX);
    for (QString line : text.split(u'\n')) {
        const QString kind = py::strip(line.section(u' ', 0, 0));
        const QStringList *keys = FNT_KEYS.get(kind);
        if (keys) {
            OrderedMap<QString, QString> vals;
            for (auto it = kv.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                vals.insert(m.captured(1), m.captured(2));
            }
            auto iv = [&](const QString &k) { const QString *s = vals.get(k); return s ? py::to_int(*s) : 0LL; };
            if (kind == u"char")
                res.boxes.push_back({iv(u"x"_s), iv(u"y"_s), iv(u"width"_s), iv(u"height"_s)});
            else if (kind == u"common")
                res.sheet = {iv(u"scaleW"_s), iv(u"scaleH"_s)};
            for (const QString &k : *keys) {
                const QRegularExpression re(u"\\b(%1=)(-?\\d+)(\\s*)"_s.arg(k), RX);
                const auto m = re.match(line);
                if (m.hasMatch()) {
                    const QString g2 = m.captured(2), g3 = m.captured(3);
                    const QString num = QString::number(py::to_int(g2) * S);
                    const QString rep = !g3.isEmpty() ? m.captured(1) + (num + u" "_s).leftJustified(g2.size() + g3.size())
                                                      : m.captured(1) + num;
                    line = line.left(m.capturedStart()) + rep + line.mid(m.capturedEnd());
                }
            }
            if (kind == u"common") {
                const auto m = base_re.match(line);
                if (m.hasMatch()) {
                    const long long b = py::to_int(m.captured(2));
                    const long long nb = b <= SMALL_BASE ? std::min(b * S, SLOT_LIMIT - 1) : b * S;
                    line = line.left(m.capturedStart()) + m.captured(1) + QString::number(nb) + line.mid(m.capturedEnd());
                }
            }
        }
        out << line;
    }
    res.text = out.join(u'\n');
    return res;
}

std::pair<Pic, OptStr> scale_font_sheet(const Pic &rgba, const std::vector<std::array<long long, 4>> &boxes,
                                        const std::pair<long long, long long> &sheet)
{
    // Every letter scaled on its own (with see-through around it), so no letter picks up its neighbour's edge.
    const long long h = rgba.h, w = rgba.w;
    if (std::make_pair(w, h) != sheet || boxes.empty())
        return {bigger(rgba), u"sheet %1x%2, the .fnt says %3x%4: scaled whole"_s.arg(w).arg(h).arg(sheet.first).arg(sheet.second)};
    Pic out(h * S, w * S, 4);
    for (long long i = 0; i < out.h * out.w; ++i)
        out.d[size_t(i * 4)] = out.d[size_t(i * 4 + 1)] = out.d[size_t(i * 4 + 2)] = 255;
    const long long pad = 3;
    for (const auto &[x, y, bw, bh] : boxes) {
        if (bw <= 0 || bh <= 0 || x + bw > w || y + bh > h)
            continue;
        // (a negative x / y counts from the end in numpy: the same part as Python cuts)
        const long long x0 = x < 0 ? std::max(0LL, w + x) : x, y0 = y < 0 ? std::max(0LL, h + y) : y;
        const Pic src = rgba.part(y0, y0 + bh, x0, x0 + bw);
        if (src.w != bw || src.h != bh)
            throw PyError(u"ValueError"_s, u"could not broadcast input array"_s);
        Pic cell(bh + 2 * pad, bw + 2 * pad, 4);
        for (long long yy = 0; yy < bh; ++yy)
            std::memcpy(&cell.d[size_t(((pad + yy) * cell.w + pad) * 4)], &src.d[size_t(yy * bw * 4)], size_t(bw * 4));
        const Pic big = bigger(cell).part(pad * S, (pad + bh) * S, pad * S, (pad + bw) * S);
        for (long long yy = 0; yy < big.h; ++yy)
            std::memcpy(&out.d[size_t(((y0 * S + yy) * out.w + x0 * S) * 4)], &big.d[size_t(yy * big.w * 4)],
                        size_t(big.w * 4));
    }
    return {out, std::nullopt};
}

// ---- a font at any size (C++ only, 2026-11-15: the province names in 0.1 steps) -----------------------------------
static long long fl(double v) { return static_cast<long long>(std::floor(v + 1e-9)); }
static long long cl(double v) { return static_cast<long long>(std::ceil(v - 1e-9)); }
// a sheet's side at f: a power of two stays one (the next that holds the letters; the game's sheets are 256, 512,
// 1024 ... and a texture of another size may not draw right) — the letters keep their places, the rest is see-through
static long long side(long long v, double f)
{
    const long long c = cl(double(v) * f);
    if (v <= 0 || (v & (v - 1)) != 0)
        return c;
    long long p = 1;
    while (p < c)
        p *= 2;
    return p;
}

ScaledFnt scale_fnt_by(const QString &text, double f)
{
    ScaledFnt res;
    res.sheet = {0, 0};
    QStringList out;
    static const QRegularExpression kv(uR"((\w+)=(-?\d+))"_s, RX);
    static const QRegularExpression base_re(uR"(\b(base=)(-?\d+))"_s, RX);
    for (QString line : text.split(u'\n')) {
        const QString kind = py::strip(line.section(u' ', 0, 0));
        const QStringList *keys = FNT_KEYS.get(kind);
        if (keys) {
            OrderedMap<QString, long long> vals;
            for (auto it = kv.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                vals.insert(m.captured(1), py::to_int(m.captured(2)));
            }
            auto iv = [&](const QString &k) { const long long *v = vals.get(k); return v ? *v : 0LL; };
            if (kind == u"char")
                res.boxes.push_back({iv(u"x"_s), iv(u"y"_s), iv(u"width"_s), iv(u"height"_s)});
            else if (kind == u"common")
                res.sheet = {iv(u"scaleW"_s), iv(u"scaleH"_s)};
            auto scaled = [&](const QString &k, long long v) -> long long {
                if (kind == u"char" && (k == u"x" || k == u"y"))
                    return fl(double(v) * f);
                if (kind == u"char" && k == u"width")
                    return fl(double(iv(u"x"_s) + v) * f) - fl(double(iv(u"x"_s)) * f);
                if (kind == u"char" && k == u"height")
                    return fl(double(iv(u"y"_s) + v) * f) - fl(double(iv(u"y"_s)) * f);
                if (kind == u"common" && (k == u"scaleW" || k == u"scaleH"))
                    return side(v, f);
                return std::lround(double(v) * f);
            };
            for (const QString &k : *keys) {
                const QRegularExpression re(u"\\b(%1=)(-?\\d+)(\\s*)"_s.arg(k), RX);
                const auto m = re.match(line);
                if (m.hasMatch()) {
                    const QString g2 = m.captured(2), g3 = m.captured(3);
                    const QString num = QString::number(scaled(k, py::to_int(g2)));
                    const QString rep = !g3.isEmpty() ? m.captured(1) + (num + u" "_s).leftJustified(g2.size() + g3.size())
                                                      : m.captured(1) + num;
                    line = line.left(m.capturedStart()) + rep + line.mid(m.capturedEnd());
                }
            }
            if (kind == u"common") {
                const auto m = base_re.match(line);
                if (m.hasMatch())
                    line = line.left(m.capturedStart()) + m.captured(1) +
                           QString::number(std::lround(double(py::to_int(m.captured(2))) * f)) + line.mid(m.capturedEnd());
            }
        }
        out << line;
    }
    res.text = out.join(u'\n');
    return res;
}

std::pair<Pic, OptStr> scale_font_sheet_by(const Pic &rgba, const std::vector<std::array<long long, 4>> &boxes,
                                           const std::pair<long long, long long> &sheet, double f)
{
    // Every letter scaled on its own into its new box, so no letter picks up its neighbour's edge.
    const long long h = rgba.h, w = rgba.w, nw = side(w, f), nh = side(h, f);
    if (std::make_pair(w, h) != sheet || boxes.empty())
        return {resized(rgba, nw, nh),
                u"sheet %1x%2, the .fnt says %3x%4: scaled whole"_s.arg(w).arg(h).arg(sheet.first).arg(sheet.second)};
    Pic out(nh, nw, 4);
    for (long long i = 0; i < out.h * out.w; ++i)
        out.d[size_t(i * 4)] = out.d[size_t(i * 4 + 1)] = out.d[size_t(i * 4 + 2)] = 255;
    for (const auto &[x, y, bw, bh] : boxes) {
        if (bw <= 0 || bh <= 0 || x < 0 || y < 0 || x + bw > w || y + bh > h)
            continue;
        const long long X = fl(double(x) * f), Y = fl(double(y) * f);
        const long long W2 = fl(double(x + bw) * f) - X, H2 = fl(double(y + bh) * f) - Y;
        if (W2 <= 0 || H2 <= 0)
            continue;
        const Pic big = resized(rgba.part(y, y + bh, x, x + bw), W2, H2);
        for (long long yy = 0; yy < big.h && Y + yy < out.h; ++yy)
            std::memcpy(&out.d[size_t(((Y + yy) * out.w + X) * 4)], &big.d[size_t(yy * big.w * 4)],
                        size_t(std::min(big.w, out.w - X) * 4));
    }
    return {out, std::nullopt};
}

// ------------------------------------------------------------------------------------------------ mouse pointers
static std::vector<std::uint8_t> unpackbits_row(const std::uint8_t *row, long long nbytes)
{
    std::vector<std::uint8_t> out(size_t(nbytes * 8));
    for (long long i = 0; i < nbytes; ++i)
        for (int b = 0; b < 8; ++b)
            out[size_t(i * 8 + b)] = (row[i] >> (7 - b)) & 1;
    return out;
}

std::vector<CurImage> cur_images(const QByteArray &data)
{
    // A .cur file -> [(picture, hotspot x, hotspot y)]
    const long long count = u16(data, 4);
    u16(data, 0);
    std::vector<CurImage> out;
    for (long long i = 0; i < count; ++i) {
        const long long e = 6 + 16 * i;
        u32(data, e + 12);
        const long long hx = u16(data, e + 4), hy = u16(data, e + 6), size = u32(data, e + 8), off = u32(data, e + 12);
        const QByteArray blob = data.mid(qsizetype(off), qsizetype(size));
        if (blob.startsWith("\x89PNG\r\n\x1a\n")) {
            out.push_back({qimage_rgba(QImage::fromData(blob)), hx, hy});
            continue;
        }
        const long long hsize = u32(blob, 0);
        const long long w = std::int32_t(u32(blob, 4)), h2 = std::int32_t(u32(blob, 8));
        u16(blob, 12);
        const long long bits = u16(blob, 14);
        const long long h = std::llabs(h2) / 2;
        long long ncol = u32(blob, 32);
        if (!ncol)
            ncol = bits <= 8 ? (1LL << bits) : 0;
        auto need = [&](long long count_, long long offset) {
            if (count_ < 0 || offset < 0 || offset + count_ > blob.size())
                throw PyError(u"ValueError"_s, u"buffer is smaller than requested size"_s);
        };
        std::vector<std::array<std::uint8_t, 3>> pal;
        if (ncol) {
            need(ncol * 4, hsize);
            for (long long c = 0; c < ncol; ++c)
                pal.push_back({std::uint8_t(blob[qsizetype(hsize + c * 4 + 2)]), std::uint8_t(blob[qsizetype(hsize + c * 4 + 1)]),
                               std::uint8_t(blob[qsizetype(hsize + c * 4)])});
        }
        const long long p = hsize + 4 * ncol;
        const long long row = ((w * bits + 31) / 32) * 4, mrow = ((w + 31) / 32) * 4;
        need(row * h, p);
        const auto *base = reinterpret_cast<const std::uint8_t *>(blob.constData());
        Pic px(h, w, 4);
        bool any_alpha = false;
        for (long long y = 0; y < h; ++y) {
            const std::uint8_t *r = base + p + (h - 1 - y) * row;     // (rows bottom up: [::-1])
            if (bits == 32) {
                for (long long x = 0; x < w; ++x) {
                    px.at(y, x, 0) = r[x * 4 + 2];
                    px.at(y, x, 1) = r[x * 4 + 1];
                    px.at(y, x, 2) = r[x * 4];
                    px.at(y, x, 3) = r[x * 4 + 3];
                    any_alpha = any_alpha || r[x * 4 + 3];
                }
            } else if (bits == 24) {
                for (long long x = 0; x < w; ++x) {
                    px.at(y, x, 0) = r[x * 3 + 2];
                    px.at(y, x, 1) = r[x * 3 + 1];
                    px.at(y, x, 2) = r[x * 3];
                }
            } else {
                const auto bitsarr = unpackbits_row(r, row);
                if (pal.empty())
                    throw PyError(u"TypeError"_s, u"'NoneType' object is not subscriptable"_s);
                for (long long x = 0; x < w; ++x) {
                    long long idx = 0;
                    for (long long b = 0; b < bits; ++b)
                        idx += bitsarr[size_t(x * bits + b)] * (1LL << (bits - 1 - b));
                    const auto &c = pal[size_t(std::min<long long>(idx, static_cast<long long>(pal.size()) - 1))];
                    for (int k = 0; k < 3; ++k)
                        px.at(y, x, k) = c[size_t(k)];
                }
            }
        }
        if (bits != 32 || !any_alpha) {              // no alpha in the picture: the mask says what is see-through
            if (blob.size() >= p + row * h + mrow * h) {
                for (long long y = 0; y < h; ++y) {
                    const auto bitsarr = unpackbits_row(base + p + row * h + (h - 1 - y) * mrow, mrow);
                    for (long long x = 0; x < w; ++x)
                        px.at(y, x, 3) = bitsarr[size_t(x)] == 1 ? 0 : 255;
                }
            } else
                for (long long y = 0; y < h; ++y)
                    for (long long x = 0; x < w; ++x)
                        px.at(y, x, 3) = 255;
        }
        out.push_back({px, hx, hy});
    }
    return out;
}

QByteArray cur_bytes(const std::vector<CurImage> &images)
{
    std::vector<QByteArray> blobs;
    for (const CurImage &im : images) {
        const Pic &px = im.px;
        const long long h = px.h, w = px.w;
        QByteArray xr, mask;
        for (long long y = h - 1; y >= 0; --y)
            for (long long x = 0; x < w; ++x) {
                put8(xr, px.at(y, x, 2));
                put8(xr, px.at(y, x, 1));
                put8(xr, px.at(y, x, 0));
                put8(xr, px.at(y, x, 3));
            }
        const long long mrow = ((w + 31) / 32) * 4;
        for (long long y = h - 1; y >= 0; --y)
            for (long long byte = 0; byte < mrow; ++byte) {
                std::uint8_t v = 0;
                for (int b = 0; b < 8; ++b) {
                    const long long x = byte * 8 + b;
                    if (x < w && px.at(y, x, 3) == 0)
                        v |= std::uint8_t(1 << (7 - b));
                }
                put8(mask, v);
            }
        QByteArray blob;
        put32(blob, 40);
        put32(blob, std::uint32_t(std::int32_t(w)));
        put32(blob, std::uint32_t(std::int32_t(h * 2)));
        put16(blob, 1);
        put16(blob, 32);
        put32(blob, 0);
        put32(blob, std::uint32_t(xr.size() + mask.size()));
        for (int i = 0; i < 4; ++i)
            put32(blob, 0);
        blobs.push_back(blob + xr + mask);
    }
    QByteArray head;
    put16(head, 0);
    put16(head, 2);
    put16(head, std::uint16_t(images.size()));
    long long off = 6 + 16 * static_cast<long long>(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        const long long h = images[i].px.h, w = images[i].px.w;
        if (images[i].hx > 0xFFFF || images[i].hy > 0xFFFF)
            throw PyError(u"error"_s, u"'H' format requires 0 <= number <= 65535"_s);
        put8(head, std::uint8_t(w < 256 ? w : 0));
        put8(head, std::uint8_t(h < 256 ? h : 0));
        put8(head, 0);
        put8(head, 0);
        put16(head, std::uint16_t(images[i].hx));
        put16(head, std::uint16_t(images[i].hy));
        put32(head, std::uint32_t(blobs[i].size()));
        put32(head, std::uint32_t(off));
        off += blobs[i].size();
    }
    for (const QByteArray &b : blobs)
        head += b;
    return head;
}

QByteArray scale_cur(const QByteArray &data)
{
    std::vector<CurImage> big;
    for (const CurImage &im : cur_images(data))
        big.push_back({bigger(im.px), im.hx * S, im.hy * S});
    return cur_bytes(big);
}

QByteArray scale_ani(const QByteArray &data)
{
    // An animated pointer (RIFF 'ACON'): every frame is a .cur inside an 'icon' chunk.
    if (data.left(4) != "RIFF" || data.mid(8, 4) != "ACON")
        throw PyError(u"ValueError"_s, u"not an animated pointer"_s);
    auto chunks = [](const QByteArray &buf, long long pos, long long end) {
        std::vector<std::pair<QByteArray, QByteArray>> out;
        while (pos + 8 <= end) {
            const QByteArray cid = buf.mid(qsizetype(pos), 4);
            const long long size = u32(buf, pos + 4);
            out.emplace_back(cid, buf.mid(qsizetype(pos + 8), qsizetype(size)));
            pos += 8 + size + (size & 1);
        }
        return out;
    };
    auto pack = [](const QByteArray &cid, const QByteArray &body) {
        QByteArray out = cid;
        put32(out, std::uint32_t(body.size()));
        out += body;
        if (body.size() & 1)
            out.append('\0');
        return out;
    };
    QByteArray out;
    for (const auto &[cid, body] : chunks(data, 12, data.size())) {
        if (cid == "LIST" && body.left(4) == "fram") {
            QByteArray inner;
            for (const auto &[c, b] : chunks(body, 4, body.size()))
                inner += pack(c, c == "icon" ? scale_cur(b) : b);
            out += pack("LIST", QByteArray("fram") + inner);
        } else if (cid == "anih" && body.size() >= 36) {
            std::uint32_t f[9];
            std::memcpy(f, body.constData(), 36);
            f[3] *= S;                               // (width, height: 0 when the frames say it themselves)
            f[4] *= S;
            out += pack("anih", QByteArray(reinterpret_cast<const char *>(f), 36) + body.mid(36));
        } else
            out += pack(cid, body);
    }
    QByteArray res("RIFF");
    put32(res, std::uint32_t(out.size() + 4));
    res += "ACON";
    return res + out;
}

// ------------------------------------------------------------------------------------------------- the whole job
static QString latin(const QByteArray &raw)
{
    return QString::fromLatin1(raw.startsWith("\xef\xbb\xbf") ? raw.mid(3) : raw);
}

Plan plan(const moddata::Mod &mod)
{
    // What there is to do.
    const Files files(mod);
    Plan job;
    {
        const auto t = files.tree(u"interface"_s);
        for (const auto &[rel, full] : t)
            if ((rel.endsWith(u".gui") || rel.endsWith(u".gfx")) && rel.count(u'/') == 1)
                job.scripts.emplace_back(rel, full);
        std::sort(job.scripts.begin(), job.scripts.end());
    }
    OrderedMap<QString, std::set<long long>> frames;
    for (const auto &[rel, full] : job.scripts)
        for (const auto &[t, ns] : scale_script(latin(read_all(full)), rel.endsWith(u".gui")).frames) {
            if (auto *s = frames.get(t))
                s->insert(ns.begin(), ns.end());
            else
                frames.insert(t, ns);
        }
    OrderedMap<QString, long long> strip;
    OrderedMap<QString, QString> extra;
    OrderedMap<QString, std::pair<QString, QString>> sprite_file;
    for (const auto &[rel, full] : job.scripts) {
        if (!rel.endsWith(u".gfx"))
            continue;
        script::Nodes tops;
        try {
            tops = script::parse(script::read_text(full));
        } catch (const std::exception &) {
            continue;
        }
        for (const script::Node &top : tops) {
            if (!top.is_block)
                continue;
            for (const script::Node &sp : top.kids) {
                if (!sp.is_block)
                    continue;
                const QString name = low(val_of(sp, u"name"_s));
                if (std::none_of(MONEY_ICONS.begin(), MONEY_ICONS.end(), [&](const auto &m) { return std::get<0>(m) == name; }))
                    continue;
                if (const auto hit = files.find(val_of(sp, u"texturefile"_s).value_or(QString())))
                    sprite_file.insert(name, *hit);
            }
        }
    }
    for (const auto &[name, src, side] : MONEY_ICONS)
        if (sprite_file.contains(name) && sprite_file.contains(src))
            job.money.insert(sprite_file.get(name)->first, {sprite_file.get(src)->second, side});
    QStringList missing;
    for (const auto &[t, ns] : frames) {
        const auto hit = files.find(t);
        if (!hit) {
            missing << t;
            continue;
        }
        const auto &[rel, full] = *hit;
        strip.insert(rel, ns.size() == 1 ? std::max(strip.value(rel, 1), *ns.rbegin()) : 1);
        bool in_trees = rel.startsWith(FONT_TREE + u'/') || rel.startsWith(u"gfx/flags/");
        for (const QString &p : PICTURE_TREES)
            in_trees = in_trees || rel.startsWith(p + u'/');
        if (!in_trees)
            extra.insert(rel, full);
    }
    auto is_pic = [](const QString &rel) {
        return std::any_of(PICTURE_EXT.begin(), PICTURE_EXT.end(), [&](const QString &e) { return rel.endsWith(e); });
    };
    auto sorted_items = [](const OrderedMap<QString, QString> &m) {
        std::vector<std::pair<QString, QString>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end());
        return v;
    };
    for (const QString &sub : PICTURE_TREES)
        for (const auto &[rel, full] : sorted_items(files.tree(sub)))
            if (is_pic(rel))
                job.pictures.push_back({rel, full, strip.value(rel, 1)});
    for (const auto &[rel, full] : sorted_items(extra))
        job.pictures.push_back({rel, full, strip.value(rel, 1)});
    const auto tree = files.tree(FONT_TREE);
    QSet<QString> used;
    for (const auto &[rel, full] : sorted_items(tree))
        if (rel.endsWith(u".fnt")) {
            std::vector<std::pair<QString, QString>> sheets;
            for (const QString ext : {u".tga"_s, u".dds"_s})
                if (const QString *s = tree.get(rel.left(rel.size() - 4) + ext)) {
                    sheets.emplace_back(rel.left(rel.size() - 4) + ext, *s);
                    used.insert(rel.left(rel.size() - 4) + ext);
                }
            job.fonts.emplace_back(rel, full, sheets);
        }
    for (const auto &[rel, full] : sorted_items(tree))
        if (is_pic(rel) && !used.contains(rel))
            job.pictures.push_back({rel, full, 1});
    for (const auto &[rel, full] : sorted_items(files.tree(u"gfx/cursors"_s))) {
        if (rel.endsWith(u".cur") || rel.endsWith(u".ani"))
            job.cursors.emplace_back(rel, full);
        else if (is_pic(rel))
            job.pictures.push_back({rel, full, 1});
    }
    std::sort(missing.begin(), missing.end());
    job.missing = missing;
    return job;
}

static QStringList old_list(const QString &out)
{
    // The files the last build wrote into `out` (the list at the end of its report), with /.
    QFile f(pypath::join(out, REPORT));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString text = QString::fromUtf8(f.readAll()).replace(u"\r\n"_s, u"\n"_s);   // (read in text mode)
    const qsizetype at = text.indexOf(u"\nFILES\n"_s);
    if (at < 0)
        return {};
    QStringList out_;
    for (const QString &ln : py::splitlines(text.mid(at + 7)))
        if (!py::strip(ln).isEmpty())
            out_ << py::strip(ln);
    return out_;
}

QString report_text(const moddata::Mod &mod, const Report &rep, const QStringList &made)
{
    auto block = [](const QString &title, const QStringList &lines) {
        return QStringList{u""_s, u"%1 (%2)"_s.arg(title).arg(lines.size())} + lines;
    };
    QStringList out = {
        u"4K interface submod of %1 - made by ModEditor on %2"_s.arg(mod.name(),
                                                                     QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm"_s)),
        u"%1 interface files (%2 numbers doubled), %3 pictures (%4 megapixels), %5 fonts, %6 mouse pointers"_s.arg(rep.scripts)
            .arg(rep.numbers)
            .arg(rep.pictures)
            .arg(py::fmt_f(rep.megapixels, 0))
            .arg(rep.fonts)
            .arg(rep.cursors),
        u""_s,
        u"To play: tick the mod AND this submod in the game's launcher. The pair has its own settings and saved"_s,
        u"games (Documents > Paradox Interactive > Victoria II > a folder named after both)."_s,
        u"Made again from the editor (Tools > Submods & 4K) it follows the mod; this file is written anew each time."_s};
    out += block(u"COULD NOT BE MADE"_s, rep.problems);
    out += block(u"WRITTEN DIFFERENTLY THAN THEY WERE"_s, rep.notes);
    out += block(u"NAMED BY A SPRITE BUT NOWHERE ON DISK - nothing is drawn there, with or without this submod"_s, rep.missing);
    out += block(u"MADE BY THE LAST BUILD, NO LONGER BY THIS ONE - taken out"_s, rep.gone);
    QStringList m = made;
    std::stable_sort(m.begin(), m.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    out += QStringList{u""_s, u"FILES"_s} + m;
    return out.join(u'\n') + u'\n';
}

// ---- two things the Python editor does not do: changed on the user's word, 2026-11-07 ------------------------------
// The map's own font (the province names: bitmap fonts tahoma_60 / mapfont_56) is drawn on the map, which a 4K screen
// shows bigger by itself; doubled, the names came out twice too big. Its files are written at their own size.
// The map marks (interface/mapicons.gui + .gfx: army counters, battles, sieges, colonising, mobilising) are made at
// 4/3 instead of 2 (doubled they covered whole provinces; the user: "make those small as well, by at least 1/3"), by
// mapmarks::make. Their text gets a copy of the mod's font about 4/3 as tall, kept at its own size (every font of the
// submod is doubled), as a bitmap font of its own: <font>_mapmark.
const double MARKS = 4.0 / 3.0;

Sizes read_sizes(const QString &report)
{
    Sizes s;
    auto num = [&report](const QString &pattern) -> std::optional<double> {
        const QRegularExpressionMatch m = QRegularExpression(pattern).match(report);
        bool ok = false;
        const double v = m.hasMatch() ? m.captured(1).toDouble(&ok) : 0.0;
        return ok && v > 0 ? std::optional<double>(v) : std::nullopt;
    };
    s.topbar = num(uR"re(interface/topbar\.gui: at × ([0-9.]+) of its size)re"_s).value_or(S);
    s.unitpanel = num(uR"re(interface/single_unitpanel\.gui: at × ([0-9.]+) of its size)re"_s).value_or(S);
    s.marks = num(uR"re(the map marks \([^)]*\) at × ([0-9.]+) of their size)re"_s).value_or(MARKS);
    // (no note that the map's font was kept: it was doubled — when the report has notes at all)
    if (const auto own = num(uR"re(the map's font \(province names\) - at × ([0-9.]+) of its size)re"_s)) {
        s.names = *own;                              // (a size of its own, since 2026-11-15)
        return s;
    }
    s.names = report.contains(u"the map's font (province names) - kept"_s) ||
                      !report.contains(u"WRITTEN DIFFERENTLY THAN THEY WERE"_s)
                  ? 1.0
                  : 2.0;
    return s;
}
static const QStringList MAP_FONTS = {u"tahoma_60"_s, u"mapfont_56"_s};
static const QByteArray BOM("\xef\xbb\xbf");

static void bitmapfonts(const script::Nodes &nodes, std::vector<const script::Node *> &out)
{
    for (const script::Node &n : nodes)
        if (n.is_block) {
            if (n.has_key && py::lower(n.key) == u"bitmapfont")
                out.push_back(&n);
            else
                bitmapfonts(n.kids, out);
        }
}

static QSet<QString> map_font_files(const QStringList &gfx_texts)
{
    // The font files (gfx/fonts/<name>, lower case, no ending) the map's province names are drawn with.
    QSet<QString> out;
    for (const QString &text : gfx_texts) {
        const script::Nodes tree = script::parse(text);
        std::vector<const script::Node *> fonts;
        bitmapfonts(tree, fonts);
        for (const script::Node *f : fonts)
            if (MAP_FONTS.contains(low(val_of(*f, u"name"_s))))
                if (const OptStr fn = val_of(*f, u"fontname"_s); fn && !fn->isEmpty())
                    out.insert(py::lower(*fn));
    }
    if (out.isEmpty())
        out.insert(u"mapfont_56"_s);
    return out;
}

static QString sub_first(const QString &text, const QRegularExpression &re, const QString &value)
{
    // The first match's group 2 replaced by value (groups 1 and 3 kept).
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return text;
    return text.left(m.capturedStart(2)) + value + text.mid(m.capturedEnd(2));
}

namespace {
struct Gfx {                                         // a .gfx file the game reads and its text
    QString full, text;
};
struct Sink {                                        // the files these steps write into the submod
    const Files &files;
    const QString &out;
    QStringList &made;
    QSet<QString> have;
    Sink(const Files &files_, const QString &out_, QStringList &made_) : files(files_), out(out_), made(made_)
    {
        for (const QString &m : made)
            have.insert(py::lower(m));
    }
    QString dst_for(const QString &rel)              // (rel: below the submod, with /)
    {
        if (!have.contains(py::lower(rel))) {
            have.insert(py::lower(rel));
            made << rel;
        }
        const QString dst = pypath::join(out, rel.split(u'/'));
        fs::makedirs(pypath::dirname(dst));
        return dst;
    }
    QString true_rel(const QString &full) const { return files.true_rel(full).replace(u'\\', u'/'); }
};
}  // namespace

static std::vector<Gfx> gfx_sources(const Plan &job)
{
    std::vector<Gfx> sources;
    for (const auto &[rel, full] : job.scripts)
        if (rel.endsWith(u".gfx"))
            sources.push_back({full, latin(read_all(full))});
    return sources;
}

static QString problem(const QString &rel, const std::exception &e);
static const QRegularExpression &FONT_USE_RE()       // font = "…" (the map marks)
{
    static const QRegularExpression re(uR"re((\bfont\s*=\s*")([^"]+)("))re"_s, QRegularExpression::CaseInsensitiveOption | RX);
    return re;
}
static const QRegularExpression &WIN_FONT_RE()       // font = "…", buttonFont = "…" (a window)
{
    static const QRegularExpression re(uR"re((\b(?:font|buttonFont)\s*=\s*")([^"]+)("))re"_s,
                                       QRegularExpression::CaseInsensitiveOption | RX);
    return re;
}

static QString own_fonts(Sink &sink, const std::vector<Gfx> &sources, const QString &gui_full, QString gui_text,
                         const QString &suffix, Report &rep, const QRegularExpression &font_re = FONT_USE_RE(),
                         const QSet<QString> &keep = {})
{
    // Every font a window uses, as a copy at its own size (every font of the submod is doubled): the font's files
    // copied as <fontname><suffix>, a bitmap font <font><suffix> written right after the font's own entry, and the
    // window's text pointed at it. (A copy another window has made already is used as it is.)
    static const QRegularExpression NAME_RE(uR"re((\bname\s*=\s*"?)([^"\s{}]+)("?))re"_s,
                                            QRegularExpression::CaseInsensitiveOption | RX);
    static const QRegularExpression FONTNAME_RE(uR"re((\bfontname\s*=\s*"?)([^"\s{}]+)("?))re"_s,
                                                QRegularExpression::CaseInsensitiveOption | RX);
    const Files &files = sink.files;
    const QString gui_rel = sink.true_rel(gui_full);
    QStringList used;
    for (auto it = font_re.globalMatch(gui_text); it.hasNext();)
        if (const QString f = it.next().captured(2); !used.contains(f) && !keep.contains(f))
            used << f;
    for (const QString &f : used) {
        // where the bitmap font is defined (the first .gfx naming it; the same name in other letters if not)
        const Gfx *src = nullptr;
        const script::Node *def = nullptr;
        script::Nodes tree;                          // (keeps def alive)
        for (int exact = 1; exact >= 0 && !def; --exact)
            for (const Gfx &g : sources) {
                script::Nodes t = script::parse(g.text);
                std::vector<const script::Node *> fonts;
                bitmapfonts(t, fonts);
                const auto hit = std::find_if(fonts.begin(), fonts.end(), [&](const script::Node *n) {
                    const OptStr nm = val_of(*n, u"name"_s);
                    return nm && (exact ? *nm == f : py::lower(*nm) == py::lower(f));
                });
                if (hit != fonts.end()) {
                    def = *hit;
                    tree = std::move(t);             // (a moved vector keeps its buffer: def stays good)
                    src = &g;
                    break;
                }
            }
        const OptStr fontname = def ? val_of(*def, u"fontname"_s) : std::nullopt;
        if (!def || !fontname || fontname->isEmpty() || fontname->contains(u'/') || fontname->contains(u'\\')) {
            rep.notes << u"%1: font %2 not found in a .gfx file - left as it is (drawn doubled)"_s.arg(gui_rel, f);
            continue;
        }
        const QString fresh = f + suffix, fresh_file = *fontname + suffix;
        // the font's files, as they are
        const QString stem = FONT_TREE + u"/"_s + py::lower(*fontname);
        const auto fnt = files.find(stem + u".fnt"_s);
        if (!fnt || fnt->first != stem + u".fnt") {
            rep.notes << u"%1: the font file %2.fnt not found - font %3 left as it is (drawn doubled)"_s.arg(
                gui_rel, *fontname, f);
            continue;
        }
        write_bytes(sink.dst_for(FONT_TREE + u"/"_s + fresh_file + u".fnt"_s), read_all(fnt->second));
        rep.fonts += 1;
        for (const QString ext : {u".tga"_s, u".dds"_s}) {
            const QString p = fnt->second.chopped(4) + ext;
            if (fs::isfile(p)) {
                write_bytes(sink.dst_for(FONT_TREE + u"/"_s + fresh_file + ext), read_all(p));
                rep.pictures += 1;
            }
        }
        // the bitmap font: a copy of the font's own entry, written right after it
        QString block = src->text.mid(def->kstart, def->vend - def->kstart);
        block = sub_first(block, FONTNAME_RE, fresh_file);
        block = sub_first(block, NAME_RE, fresh);
        const QString dst = pypath::join(sink.out, files.true_rel(src->full).split(u'\\'));
        if (!fs::isfile(dst)) {
            rep.notes << u"%1: not written - font %2 left as it is (drawn doubled)"_s.arg(sink.true_rel(src->full), f);
            continue;
        }
        const QByteArray raw = read_all(dst);
        const QString written = latin(raw);
        const script::Nodes wt = script::parse(written);
        std::vector<const script::Node *> wf;
        bitmapfonts(wt, wf);
        const bool there = std::any_of(wf.begin(), wf.end(),
                                       [&](const script::Node *n) { return val_of(*n, u"name"_s) == fresh; });
        if (!there) {
            const OptStr def_name = val_of(*def, u"name"_s);
            const auto at = std::find_if(wf.begin(), wf.end(),
                                         [&](const script::Node *n) { return val_of(*n, u"name"_s) == def_name; });
            if (at == wf.end())
                continue;
            const QString now = written.left((*at)->vend) + u"\n\t"_s + block + written.mid((*at)->vend);
            write_bytes(dst, (raw.startsWith(BOM) ? BOM : QByteArray()) + now.toLatin1());
        }
        // the window uses it
        QString swapped;
        qsizetype from = 0;
        for (auto it = font_re.globalMatch(gui_text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            if (m.captured(2) != f)
                continue;
            swapped += gui_text.mid(from, m.capturedStart(2) - from) + fresh;
            from = m.capturedEnd(2);
        }
        gui_text = swapped + gui_text.mid(from);
        rep.notes << u"%1: font %2 at its own size as %3 (files %4.*)"_s.arg(gui_rel, f, fresh,
                                                                           FONT_TREE + u"/"_s + fresh_file);
    }
    return gui_text;
}

static void map_marks(const Files &files, const Plan &job, const QString &out, Report &rep, QStringList &made,
                      double factor)
{
    const auto gui = files.find(mapmarks::GUI), gfx = files.find(mapmarks::GFX);
    if (!gui || !gfx)
        return;
    Sink sink(files, out, made);
    const std::vector<Gfx> sources = gfx_sources(job);
    QStringList texts;
    for (const Gfx &g : sources)
        texts << g.text;
    const QByteArray gui_raw = read_all(gui->second), gfx_raw = read_all(gfx->second);
    const mapmarks::Find find = [&](const QString &p) -> std::optional<QString> {
        if (const auto hit = files.find(p))
            return hit->second;
        return std::nullopt;
    };
    mapmarks::Made mk = mapmarks::make(latin(gui_raw), latin(gfx_raw), find, factor, mapmarks::fonts_of(texts));
    rep.notes << (std::abs(factor - MARKS) < 1e-9
                      ? u"%1: the map marks (army counters, battles, sieges …) at 4/3 of their size, not doubled"_s.arg(
                            sink.true_rel(gui->second))
                      : u"%1: the map marks (army counters, battles, sieges …) at × %2 of their size"_s.arg(
                            sink.true_rel(gui->second), py::fmt_g(factor)));
    for (const QString &n : mk.notes)
        rep.notes << u"%1: %2"_s.arg(sink.true_rel(gfx->second), n);
    // pictures and the two texts
    for (const auto &[rel, pd] : mk.pics) {
        const QString dst = sink.dst_for(rel);
        const OptStr note = write_picture(dst, pd->rgba, pd->info);
        rep.megapixels += double(pd->rgba.h * pd->rgba.w) / 1e6;
        rep.pictures += 1;
        if (note && !note->isEmpty())
            rep.notes << u"%1: %2"_s.arg(rel, *note);
    }
    // the counters' fonts: a copy at its own size, a bitmap font of its own
    const QString gui_text = own_fonts(sink, sources, gui->second, mk.gui, mapmarks::MAP_SUFFIX, rep);
    write_bytes(sink.dst_for(sink.true_rel(gui->second)),
                (gui_raw.startsWith(BOM) ? BOM : QByteArray()) + gui_text.toLatin1());
    write_bytes(sink.dst_for(sink.true_rel(gfx->second)),
                (gfx_raw.startsWith(BOM) ? BOM : QByteArray()) + mk.gfx.toLatin1());
}

// ---- windows at a size of their own (C++ only, 2026-11-09: the user's sliders on the 4K screen) ----------------------
// A window file (topbar.gui, single_unitpanel.gui) at another factor than 2: its positions and sizes × f (whole
// pixels); every sprite it names copied as <sprite>_4k_<tag> into a .gfx file of its own (interface/zz_4k_<tag>.gfx),
// pointing at its pictures at f × their size (frame by frame) in gfx/interface/4k_<tag>/; its fonts swapped for a font
// of the mod about f × as tall, written at its own size (as the map marks' fonts).
const QString OWN_SUFFIX = u"_4kown"_s;

static void own_size(const Files &files, const Plan &job, const QString &out, Report &rep, QStringList &made,
                     const QString &gui_rel, const QString &tag, double f)
{
    const auto gui = files.find(gui_rel);
    if (!gui)
        return;
    Sink sink(files, out, made);
    const std::vector<Gfx> sources = gfx_sources(job);
    // the sprites the game knows: name -> its entry (the first .gfx defining it) and its frames
    struct Def {
        QString block;
        long long frames = 1;
    };
    std::map<QString, Def> defs;
    QStringList texts;
    for (const Gfx &g : sources) {
        texts << g.text;
        for (const script::Node &top : script::parse(g.text)) {
            if (!top.is_block || py::lower(top.has_key ? top.key : QString()) != u"spritetypes")
                continue;
            for (const script::Node &sp : top.kids) {
                if (!sp.is_block)
                    continue;
                const OptStr nm = val_of(sp, u"name"_s);
                if (!nm || nm->isEmpty() || defs.count(*nm))
                    continue;
                const OptStr n = val_of(sp, u"noofframes"_s);
                defs[*nm] = {g.text.mid(sp.kstart, sp.vend - sp.kstart),
                             n && !n->isEmpty() && NUM(*n) ? py::trunc_int(*py::to_float(*n)) : 1};
            }
        }
    }
    const QByteArray gui_raw = read_all(gui->second);
    QString g = scale_script(latin(gui_raw), true, f, true).text;
    // a leader's portrait: the game puts the leader's own picture (gfx/interface/leaders, doubled with the rest) in the
    // place of this stand-in while it runs (seen 2026-11-09: a copy of the stand-in left the portrait doubled in a ×1
    // unit panel) — the stand-in keeps its name and the icon is drawn at f / 2 (scale, as in the game's own windows)
    static const QStringList SWAPPED = {u"GFX_leader_generic0"_s};
    static const QRegularExpression ICON_SPRITE(uR"re((\bspriteType\s*=\s*"?)([^"\s{}]+)("?))re"_s,
                                                QRegularExpression::CaseInsensitiveOption | RX);
    {
        QString with;
        qsizetype at = 0;
        int n = 0;
        for (auto it = ICON_SPRITE.globalMatch(g); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            if (!SWAPPED.contains(m.captured(2)))
                continue;
            with += g.mid(at, m.capturedEnd(0) - at) + u"\n\t\t\tscale = "_s + QString::number(f / S, 'g', 6);
            at = m.capturedEnd(0);
            ++n;
        }
        g = with + g.mid(at);
        if (n)
            rep.notes << u"%1: %2 leader portrait(s) drawn at %3 of the doubled pictures (scale)"_s.arg(gui_rel)
                             .arg(n)
                             .arg(QString::number(f / S, 'g', 6));
    }
    // the sprites it names: its own copies
    static const QRegularExpression REF(uR"re((\b(?:spriteType|quadTextureSprite)\s*=\s*"?)([^"\s{}]+)("?))re"_s,
                                        QRegularExpression::CaseInsensitiveOption | RX);
    std::map<QString, QString> renamed;
    QString fresh_gui;
    qsizetype from = 0;
    for (auto it = REF.globalMatch(g); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (!defs.count(m.captured(2)) || SWAPPED.contains(m.captured(2)))
            continue;
        const QString nn = m.captured(2) + u"_4k_"_s + tag;
        renamed[m.captured(2)] = nn;
        fresh_gui += g.mid(from, m.capturedStart(2) - from) + nn;
        from = m.capturedEnd(2);
    }
    g = fresh_gui + g.mid(from);
    static const QRegularExpression TEX(uR"re((texture[Ff]ile\d?\s*=\s*")([^"]+)("))re"_s, RX);
    static const QRegularExpression NAME(uR"re((\bname\s*=\s*"?)([^"\s{}]+)("?))re"_s,
                                         QRegularExpression::CaseInsensitiveOption | RX);
    const QString pic_dir = u"gfx/interface/4k_"_s + tag;
    std::map<QString, QString> pic_of;               // texture as written -> its copy (below the submod)
    QStringList blocks;
    for (const auto &[name, nn] : renamed) {
        const Def &d = defs[name];
        QString block = sub_first(d.block, NAME, nn);
        QString done;
        qsizetype at = 0;
        for (auto it = TEX.globalMatch(block); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            const QString path = m.captured(2);
            QString was = path;
            was.replace(u"\\\\"_s, u"/"_s);
            was.replace(u'\\', u'/');
            QString rel = pic_dir + u"/"_s + (py::lower(was).startsWith(u"gfx/") ? was.mid(4) : was);
            if (!pic_of.count(path)) {
                const auto full = files.find(was);
                if (!full) {
                    rep.notes << u"%1: %2 not found — left as it is"_s.arg(gui_rel, path);
                    continue;
                }
                try {
                    auto [rgba, info] = read_picture(full->second);
                    const Pic big = mapmarks::scale_picture(rgba, f, d.frames);
                    const OptStr note = write_picture(sink.dst_for(rel), big, info);
                    rep.megapixels += double(big.h * big.w) / 1e6;
                    rep.pictures += 1;
                    if (note && !note->isEmpty())
                        rep.notes << u"%1: %2"_s.arg(rel, *note);
                } catch (const Cancelled &) {
                    throw;
                } catch (const std::exception &e) {   // (reported; the window keeps the doubled picture)
                    rep.problems << problem(rel, e);
                    continue;
                }
                pic_of[path] = rel;
            }
            QString r = pic_of[path];
            r.replace(u"/"_s, u"\\\\"_s);
            done += block.mid(at, m.capturedStart(2) - at) + r;
            at = m.capturedEnd(2);
        }
        blocks << done + block.mid(at);
    }
    QString gfx = u"spriteTypes = {\n\t"_s + blocks.join(u"\n\n\t"_s) + u"\n}\n"_s;
    gfx = scale_script(gfx, false, f, true).text;
    write_bytes(sink.dst_for(u"interface/zz_4k_"_s + tag + u".gfx"_s), gfx.toLatin1());
    rep.scripts += 1;
    // its fonts: about f × as tall, at their own size
    // (a font without its size at the end of its name has no bigger one to pick: above 1.5 it stays doubled, as
    // every font of the submod, else it is written at its own size)
    std::map<QString, QString> swapped;
    QSet<QString> keep;
    const OrderedMap<QString, long long> fonts = mapmarks::fonts_of(texts);
    static const QRegularExpression SIZED(uR"((\d+)$)"_s, RX);
    QString sw;
    from = 0;
    for (auto it = WIN_FONT_RE().globalMatch(g); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString fresh = mapmarks::font_for(m.captured(2), f, fonts);
        if (fresh != m.captured(2))
            swapped[m.captured(2)] = fresh;
        else if (f > 1.5 && !SIZED.match(fresh).hasMatch())
            keep.insert(fresh);
        sw += g.mid(from, m.capturedStart(2) - from) + fresh;
        from = m.capturedEnd(2);
    }
    g = sw + g.mid(from);
    for (const auto &[a, b] : swapped)
        rep.notes << u"%1: font %2 → %3"_s.arg(gui_rel, a, b);
    for (const QString &k : keep)
        rep.notes << u"%1: font %2 doubled (no size in its name to pick a nearer one)"_s.arg(gui_rel, k);
    g = own_fonts(sink, sources, gui->second, g, OWN_SUFFIX, rep, WIN_FONT_RE(), keep);
    write_bytes(sink.dst_for(sink.true_rel(gui->second)), (gui_raw.startsWith(BOM) ? BOM : QByteArray()) + g.toLatin1());
    rep.notes << u"%1: at × %2 of its size (%3 pictures of its own in %4)"_s.arg(
        gui_rel, py::fmt_g(f), QString::number(pic_of.size()), pic_dir);
}

static QString problem(const QString &rel, const std::exception &e)
{
    if (const auto *p = dynamic_cast<const PyError *>(&e))
        return u"%1: %2: %3"_s.arg(rel, p->type, QString::fromStdString(p->what()));
    if (const auto *o = dynamic_cast<const fs::OSError *>(&e))
        return u"%1: %2: %3"_s.arg(rel, u"OSError"_s, QString::fromStdString(o->what()));
    if (dynamic_cast<const py::ValueError *>(&e))
        return u"%1: ValueError: %2"_s.arg(rel, QString::fromStdString(e.what()));
    return u"%1: %2: %3"_s.arg(rel, u"Exception"_s, QString::fromStdString(e.what()));
}

Report build(const moddata::Mod &mod, const QString &out, const Tick &tick0, int threads, const Sizes &sizes)
{
    // Write the 4K interface submod of `mod` into the folder `out`. tick(done, total, what) is called as the work goes
    // on, always from the calling thread (it may throw Cancelled). The pictures are made by several threads.
    const Tick tick = tick0 ? tick0 : [](long long, long long, const QString &) {};
    const Plan job = plan(mod);
    const Files files(mod);
    long long total = static_cast<long long>(job.scripts.size() + job.pictures.size() + job.cursors.size());
    for (const auto &f : job.fonts)
        total += 1 + static_cast<long long>(std::get<2>(f).size());
    long long done = 0;
    Report rep;
    rep.missing = job.missing;
    rep.out = out;
    const QStringList old = old_list(out);
    QStringList made;
    std::mutex made_lock;
    auto dst_of = [&](const QString &full) {
        const QString rel = files.true_rel(full);
        {
            std::lock_guard<std::mutex> g(made_lock);
            made << QString(rel).replace(u'\\', u'/');
        }
        return pypath::join(out, rel);
    };
    for (const auto &[rel, full] : job.scripts) {
        tick(done, total, rel);
        done += 1;
        const QByteArray raw = read_all(full);
        const bool bom = raw.startsWith("\xef\xbb\xbf");
        const QString text = latin(raw);
        const Scaled sc = scale_script(text, rel.endsWith(u".gui"));
        if (const OptStr bad = check_script(text, sc.text)) {
            rep.problems << u"%1: not written, the check failed: %2"_s.arg(rel, *bad);
            continue;
        }
        const QString dst = dst_of(full);
        fs::makedirs(pypath::dirname(dst));
        write_bytes(dst, (bom ? QByteArray("\xef\xbb\xbf") : QByteArray()) + sc.text.toLatin1());
        rep.scripts += 1;
        rep.numbers += sc.count;
    }
    QStringList gfx_texts;
    for (const auto &[rel, full] : job.scripts)
        if (rel.endsWith(u".gfx"))
            gfx_texts << latin(read_all(full));
    const QSet<QString> map_fonts = map_font_files(gfx_texts);
    for (const auto &[rel, full, sheets] : job.fonts) {
        tick(done, total, rel);
        done += 1;
        const bool map_font = map_fonts.contains(rel.mid(FONT_TREE.size() + 1).chopped(4));
        if (map_font && std::abs(sizes.names - 1.0) > 1e-9 && std::abs(sizes.names - 2.0) > 1e-9) {
            // (C++ only, 2026-11-15) the province names at a size of their own (× 1.1 … × 1.9)
            const ScaledFnt sf = scale_fnt_by(QString::fromLatin1(read_all(full)), sizes.names);
            const QString dst = dst_of(full);
            fs::makedirs(pypath::dirname(dst));
            write_bytes(dst, sf.text.toLatin1());
            rep.fonts += 1;
            rep.notes << u"%1: the map's font (province names) - at × %2 of its size"_s.arg(rel).arg(sizes.names);
            for (const auto &[srel, sfull] : sheets) {
                tick(done, total, srel);
                done += 1;
                try {
                    auto [rgba, info] = read_picture(sfull);
                    const auto [big, note] = scale_font_sheet_by(rgba, sf.boxes, sf.sheet, sizes.names);
                    if (info.kind == u"tga")
                        info.bits = 32;
                    else if (info.kind == u"dds" && info.pack == u"DXT1")
                        info.pack = u"DXT5"_s;
                    const OptStr n2 = write_picture(dst_of(sfull), big, info);
                    rep.megapixels += double(big.h * big.w) / 1e6;
                    rep.pictures += 1;
                    for (const OptStr &x : {note, n2})
                        if (x && !x->isEmpty())
                            rep.notes << u"%1: %2"_s.arg(srel, *x);
                } catch (const Cancelled &) {
                    throw;
                } catch (const std::exception &e) {
                    rep.problems << problem(srel, e);
                }
            }
            continue;
        }
        if (sizes.names < 1.5 && map_font) {   // the province names: as they are
            const QString dst = dst_of(full);
            fs::makedirs(pypath::dirname(dst));
            write_bytes(dst, read_all(full));
            rep.fonts += 1;
            rep.notes << u"%1: the map's font (province names) - kept at its own size"_s.arg(rel);
            for (const auto &[srel, sfull] : sheets) {
                tick(done, total, srel);
                done += 1;
                write_bytes(dst_of(sfull), read_all(sfull));
                rep.pictures += 1;
            }
            continue;
        }
        const ScaledFnt sf = scale_fnt(QString::fromLatin1(read_all(full)));
        const QString dst = dst_of(full);
        fs::makedirs(pypath::dirname(dst));
        write_bytes(dst, sf.text.toLatin1());
        rep.fonts += 1;
        for (const auto &[srel, sfull] : sheets) {
            tick(done, total, srel);
            done += 1;
            try {
                auto [rgba, info] = read_picture(sfull);
                const auto [big, note] = scale_font_sheet(rgba, sf.boxes, sf.sheet);
                if (info.kind == u"tga")
                    info.bits = 32;
                else if (info.kind == u"dds" && info.pack == u"DXT1")
                    info.pack = u"DXT5"_s;
                const OptStr n2 = write_picture(dst_of(sfull), big, info);
                rep.megapixels += double(big.h * big.w) / 1e6;
                rep.pictures += 1;
                for (const OptStr &x : {note, n2})
                    if (x && !x->isEmpty())
                        rep.notes << u"%1: %2"_s.arg(srel, *x);
            } catch (const Cancelled &) {
                throw;
            } catch (const std::exception &e) {     // (reported; the build goes on)
                rep.problems << problem(srel, e);
            }
        }
    }
    for (const auto &[rel, full] : job.cursors) {
        tick(done, total, rel);
        done += 1;
        try {
            const QByteArray data = read_all(full);
            const QByteArray fresh = rel.endsWith(u".cur") ? scale_cur(data) : scale_ani(data);
            const QString dst = dst_of(full);
            fs::makedirs(pypath::dirname(dst));
            write_bytes(dst, fresh);
            rep.cursors += 1;
        } catch (const std::exception &e) {
            rep.problems << problem(rel, e);
        }
    }

    // ---- the pictures, several at a time
    const auto &todo = job.pictures;
    std::mutex large;                                // (a large picture needs a lot of room: one of those at a time)
    auto one = [&](const PicJob &pj) -> std::pair<double, OptStr> {
        auto [rgba, info] = read_picture(pj.full);
        if (const auto *m = job.money.get(pj.rel)) { // a currency icon: the picture its slot needs (MONEY_ICONS)
            Pic big = bigger(read_picture(m->first).first);
            if (m->second)
                big = resized(big, *m->second, *m->second);
            return {double(big.h * big.w) / 1e6, write_picture(dst_of(pj.full), big, info)};
        }
        if (rgba.h * rgba.w > 1500000) {
            std::lock_guard<std::mutex> g(large);
            const Pic big = scale_frames(rgba, pj.frames);
            return {double(big.h * big.w) / 1e6, write_picture(dst_of(pj.full), big, info)};
        }
        const Pic big = scale_frames(rgba, pj.frames);
        return {double(big.h * big.w) / 1e6, write_picture(dst_of(pj.full), big, info)};
    };
    struct Result {
        QString rel;
        std::optional<std::pair<double, OptStr>> res;
        QString err;
    };
    std::atomic<size_t> nxt{0};
    std::atomic<bool> stop{false};
    std::mutex qm;
    std::condition_variable qc;
    std::deque<Result> results;
    auto worker = [&] {
        while (!stop) {
            const size_t i = nxt++;
            if (i >= todo.size())
                return;
            Result r{todo[i].rel, std::nullopt, {}};
            try {
                r.res = one(todo[i]);
            } catch (const std::exception &e) {     // (reported; the build goes on)
                r.err = problem(todo[i].rel, e);
            }
            {
                std::lock_guard<std::mutex> g(qm);
                results.push_back(std::move(r));
            }
            qc.notify_one();
        }
    };
    const int cpus = int(std::max(2u, std::thread::hardware_concurrency()));
    const int count = std::max(1, std::min({threads ? threads : cpus - 1, 8, todo.empty() ? 1 : int(todo.size())}));
    std::vector<std::thread> workers;
    for (int i = 0; i < count; ++i)
        workers.emplace_back(worker);
    struct Join {
        std::atomic<bool> &stop;
        std::vector<std::thread> &ws;
        ~Join()
        {
            stop = true;
            for (auto &w : ws)
                w.join();
        }
    } joiner{stop, workers};
    QString last;
    for (size_t k = 0; k < todo.size(); ++k) {
        Result r;
        while (true) {
            std::unique_lock<std::mutex> g(qm);
            if (qc.wait_for(g, std::chrono::milliseconds(100), [&] { return !results.empty(); })) {
                r = std::move(results.front());
                results.pop_front();
                break;
            }
            g.unlock();
            tick(done, total, last);                 // (the window stays alive, a Stop gets through)
        }
        done += 1;
        last = r.rel;
        tick(done, total, r.rel);
        if (!r.res) {
            rep.problems << r.err;
            continue;
        }
        rep.megapixels += r.res->first;
        rep.pictures += 1;
        if (r.res->second && !r.res->second->isEmpty())
            rep.notes << u"%1: %2"_s.arg(r.rel, *r.res->second);
    }
    try {
        map_marks(files, job, out, rep, made, sizes.marks);
    } catch (const Cancelled &) {
        throw;
    } catch (const std::exception &e) {             // (reported; the rest of the submod stands)
        rep.problems << problem(mapmarks::GUI, e);
    }
    // the unit panel is two files (seen in the game 2026-11-09): unitpanel.gui the head (army, leader, men; also the
    // window of several armies and the leader pick), single_unitpanel.gui the list of its regiments below it
    const std::tuple<QString, QString, double> own[3] = {
        {u"interface/topbar.gui"_s, u"topbar"_s, sizes.topbar},
        {u"interface/unitpanel.gui"_s, u"unithead"_s, sizes.unitpanel},
        {u"interface/single_unitpanel.gui"_s, u"unitpanel"_s, sizes.unitpanel}};
    for (const auto &[gui_rel, tag, f] : own) {
        if (std::abs(f - double(S)) < 1e-9)
            continue;
        try {
            tick(done, total, gui_rel);
            own_size(files, job, out, rep, made, gui_rel, tag, f);
        } catch (const Cancelled &) {
            throw;
        } catch (const std::exception &e) {         // (reported; the rest of the submod stands)
            rep.problems << problem(gui_rel, e);
        }
    }
    std::sort(rep.notes.begin(), rep.notes.end());
    std::sort(rep.problems.begin(), rep.problems.end());
    // what the last build made and this one does not (the mod has lost a picture) leaves the submod
    QSet<QString> now;
    for (const QString &m : made)
        now.insert(py::lower(m));
    for (const QString &rel : old) {
        const QString path = pypath::join(out, rel.split(u'/'));
        if (!now.contains(py::lower(rel)) && fs::isfile(path)) {
            fs::remove(path);
            rep.gone << rel;
        }
    }
    fs::makedirs(out);
    write_bytes(pypath::join(out, REPORT), report_text(mod, rep, made).replace(u"\n"_s, u"\r\n"_s).toUtf8());   // (text mode)
    tick(total, total, u""_s);
    walk(out, [&](const QString &p) {
        rep.bytes += QFileInfo(p).size();
        rep.files += 1;
    });
    return rep;
}

}  // namespace fourk
