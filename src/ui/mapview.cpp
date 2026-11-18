// mapview.py, the widget half (StarItem … MapView), translated line by line. numpy's whole-array steps are loops over
// the same pixels in the same order; float32 blends stay float32 (NEP 50), Python floats stay double.
#include "ui/mapview.h"
#include "ui/drawtext.h"

#include "core/pynum.h"
#include "core/refs.h"
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace mapview {

const std::map<int, Rgb> HL_COLORS{{2, {95, 179, 179}}, {3, {232, 196, 120}}, {4, {95, 179, 179}}};
const std::map<int, double> HL_TINT{{1, 0.42}, {2, 0.50}, {3, 0.60}, {4, 0.25}};
const int CTX = 5;
const int CTX_FOREIGN = 20;
const Rgb CTX_RGB{236, 232, 220};
const std::vector<Rgb> MESH_COLORS{{80, 210, 230}, {235, 90, 200}, {150, 230, 70},
                                   {245, 170, 40}, {140, 120, 255}, {255, 110, 90}};

namespace {

using Mask = grid::Grid<std::uint8_t>;
using Rgba = std::array<std::uint8_t, 4>;
using RgbaImage = grid::Grid<Rgba>;

QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

std::uint32_t pack(int r, int g, int b) { return 0xFF000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b); }

std::array<std::uint8_t, 3> unpack(std::uint32_t v)
{
    return {std::uint8_t((v >> 16) & 255), std::uint8_t((v >> 8) & 255), std::uint8_t(v & 255)};
}

double ms_or_one(double m) { return m != 0.0 ? m : 1.0; }   // (Python: `x or 1.0`)

// an (h, w, 4) uint8 array as a QImage (Python: QImage(rgba.data, …, Format_RGBA8888).copy())
QImage rgba_image(const RgbaImage &a)
{
    QImage qi(int(a.w()), int(a.h()), QImage::Format_RGBA8888);
    for (std::ptrdiff_t y = 0; y < a.h(); ++y)
        std::memcpy(qi.scanLine(int(y)), a.row(y), size_t(a.w()) * 4);
    return qi;
}

void put(RgbaImage &a, const Mask &m, const Rgba &v)       // rgba[m] = v
{
    auto &f = a.flat();
    const auto &mf = m.flat();
    for (size_t i = 0; i < mf.size(); ++i)
        if (mf[i])
            f[i] = v;
}

Rgba with_alpha(const Rgb &c, int a)
{
    return {std::uint8_t(std::get<0>(c)), std::uint8_t(std::get<1>(c)), std::uint8_t(std::get<2>(c)), std::uint8_t(a)};
}

// idmap[y0:y1:k, x0:x1:k]
grid::Grid<std::uint16_t> id_window(const grid::Grid<std::uint16_t> &idmap, long long y0, long long y1, long long x0,
                                 long long x1, long long k = 1)
{
    const long long h = y1 > y0 ? (y1 - y0 + k - 1) / k : 0, w = x1 > x0 ? (x1 - x0 + k - 1) / k : 0;
    grid::Grid<std::uint16_t> out(h, w);
    for (long long y = 0; y < h; ++y) {
        const std::uint16_t *src = idmap.row(y0 + y * k);
        std::uint16_t *dst = out.row(y);
        for (long long x = 0; x < w; ++x)
            dst[x] = src[x0 + x * k];
    }
    return out;
}

// lut = zeros(n, float32); lut[pid] = max(float(v), 0.001); val = lut[ids]
grid::Grid<float> values(const FocusLayer &l, const MapData &d, const grid::Grid<std::uint16_t> &ids)
{
    std::vector<float> lut(size_t(d.n), 0.0f);
    for (const auto &[pid, v] : l.pids)
        if (0 < pid && pid < d.n)
            lut[size_t(pid)] = float(std::max(v, 0.001));
    grid::Grid<float> val(ids.h(), ids.w());
    for (size_t i = 0; i < ids.flat().size(); ++i)
        val.flat()[i] = lut[ids.flat()[i]];
    return val;
}

Mask positive(const grid::Grid<float> &val, bool *any)
{
    Mask m(val.h(), val.w());
    *any = false;
    for (size_t i = 0; i < val.flat().size(); ++i)
        if (val.flat()[i] > 0.0f)
            m.flat()[i] = 1, *any = true;
    return m;
}

Mask and_not(const Mask &a, const Mask &b)                // a & ~b
{
    Mask out(a.h(), a.w());
    for (size_t i = 0; i < a.flat().size(); ++i)
        out.flat()[i] = a.flat()[i] && !b.flat()[i];
    return out;
}

// e = m.copy(); e[1:, :] &= ids[1:, :] == ids[:-1, :]; … (pixels whose 4 neighbours are the same province)
template <typename T>
Mask same_as_neighbours(const Mask &m, const grid::Grid<T> &ids)
{
    Mask e = m;
    const long long h = m.h(), w = m.w();
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            if (!e(y, x))
                continue;
            const T v = ids(y, x);
            if ((y > 0 && ids(y - 1, x) != v) || (y < h - 1 && ids(y + 1, x) != v) || (x > 0 && ids(y, x - 1) != v) ||
                (x < w - 1 && ids(y, x + 1) != v))
                e(y, x) = 0;
        }
    return e;
}

// a small ordered dict of lists (Python's g["lines"] etc.: {key: [items]}, setdefault(key, []).extend(…))
template <typename K, typename V>
struct ODict {
    std::vector<std::pair<K, std::vector<V>>> items;
    std::map<K, size_t> at;
    std::vector<V> &operator[](const K &k)
    {
        auto it = at.find(k);
        if (it != at.end())
            return items[it->second].second;
        at.emplace(k, items.size());
        items.emplace_back(k, std::vector<V>());
        return items.back().second;
    }
    void extend(const ODict &o)
    {
        for (const auto &[k, v] : o.items) {
            auto &dst = (*this)[k];
            dst.insert(dst.end(), v.begin(), v.end());
        }
    }
};

std::vector<QLineF> segments(const std::vector<QPointF> &pts)   // zip(pts, pts[1:])
{
    std::vector<QLineF> out;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        out.emplace_back(pts[i], pts[i + 1]);
    return out;
}

constexpr double PI = 3.141592653589793;   // math.pi

bool is_unit(const QString &k) { return k == u"badge"_s || k == u"army"_s || k == u"anchor"_s || k == u"label"_s; }

}  // namespace

double line_grow(double s) { return std::min(3.0, std::max(0.45, std::pow(s / 1.5, 0.55))); }
double dot_grow(double s) { return std::min(2.0, std::max(0.25, std::pow(s / 2.5, 0.75))); }

// ---------------------------------------------------------------------------------------------- StarItem, CrownItem
StarItem::StarItem()
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(10);
    setAcceptedMouseButtons(Qt::NoButton);
}

void StarItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QFont f(u"Segoe UI Symbol"_s, 11);
    p->setFont(f);
    p->setPen(QColor(theme::BG));
    const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto &dd : d)
        drawtext::draw(*p, QRectF(-8 + dd[0], -9 + dd[1], 16, 16), Qt::AlignCenter, u"★"_s);
    p->setPen(QColor(theme::GOLD));
    drawtext::draw(*p, QRectF(-8, -9, 16, 16), Qt::AlignCenter, u"★"_s);
}

CrownItem::CrownItem()
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(11);
    setAcceptedMouseButtons(Qt::NoButton);
}

QRectF CrownItem::boundingRect() const
{
    const double r = R + 4;
    return {-r, -r, 2 * r, 2 * r};
}

void CrownItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QColor dark(theme::BG);
    // badge: dark outer edge, gold ring, dark inside
    p->setPen(QPen(dark, 6.5));
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(0, 0), R, R);
    QColor fill(dark);
    fill.setAlpha(235);
    p->setPen(QPen(QColor(theme::GOLD), 3.0));
    p->setBrush(fill);
    p->drawEllipse(QPointF(0, 0), R, R);
    // crown
    const QPolygonF crown{QPointF(-8.5, 5.5), QPointF(-9.5, -5.0), QPointF(-4.5, 0.0), QPointF(0.0, -8.0),
                          QPointF(4.5, 0.0),  QPointF(9.5, -5.0),  QPointF(8.5, 5.5)};
    p->setPen(QPen(dark, 1.4));
    p->setBrush(QColor(236, 196, 96));
    p->drawPolygon(crown);
    p->drawRoundedRect(QRectF(-8.5, 5.0, 17.0, 3.6), 1.2, 1.2);
    for (const QPointF &c : {QPointF(-9.5, -5.0), QPointF(0.0, -8.0), QPointF(9.5, -5.0)})
        p->drawEllipse(c, 1.9, 1.9);
}

// ---------------------------------------------------------------------------------------------- ShapeLayer
struct ShapeLayer::Groups {
    ODict<std::tuple<Rgb, double, bool>, QLineF> lines;
    ODict<double, QLineF> under;
    ODict<std::tuple<QString, Rgb, double>, QPointF> marks;
    ODict<std::pair<Rgb, double>, QPointF> diamonds;
    ODict<std::tuple<Rgb, double, double>, QPointF> xs;
    std::vector<const Shape *> single;
    ODict<double, QLineF> halo_lines;
    ODict<std::pair<QString, double>, QPointF> halo_marks;
    std::vector<const Shape *> halo_single, images;

    void extend(const Groups &t)
    {
        lines.extend(t.lines);
        under.extend(t.under);
        marks.extend(t.marks);
        diamonds.extend(t.diamonds);
        xs.extend(t.xs);
        single.insert(single.end(), t.single.begin(), t.single.end());
        halo_lines.extend(t.halo_lines);
        halo_marks.extend(t.halo_marks);
        halo_single.insert(halo_single.end(), t.halo_single.begin(), t.halo_single.end());
        images.insert(images.end(), t.images.begin(), t.images.end());
    }
};

namespace {

// what can be drawn in one call (same look): lines by (colour, width, dash); dots / squares by (kind, colour, size);
// diamonds and x marks by colour / size — plus the highlighted ones' halos
ShapeLayer::Groups group_of(const std::vector<const Shape *> &shapes, bool halos)
{
    ShapeLayer::Groups g;
    for (const Shape *sh : shapes) {
        const QString &kind = sh->kind;
        if (kind == u"image"_s) {                     // pictures in map pixels (a canal model): under the rest
            g.images.push_back(sh);
            continue;
        }
        if (halos && sh->halo) {
            if (kind == u"line"_s) {
                auto segs = segments(sh->pts);
                auto &dst = g.halo_lines[sh->width.value_or(2)];
                dst.insert(dst.end(), segs.begin(), segs.end());
            } else if (kind == u"dot"_s || kind == u"square"_s || kind == u"diamond"_s) {
                g.halo_marks[{kind, sh->size.value_or(3.5)}].push_back(sh->pts[0]);
            } else {
                g.halo_single.push_back(sh);
            }
        }
        if (kind == u"line"_s) {
            const double w = sh->width.value_or(2);
            const auto segs = segments(sh->pts);
            auto &dst = g.lines[{sh->color, w, sh->dash}];
            dst.insert(dst.end(), segs.begin(), segs.end());
            if (sh->outline && w >= 1.5) {
                auto &u = g.under[w];
                u.insert(u.end(), segs.begin(), segs.end());
            }
        } else if (kind == u"dot"_s || kind == u"square"_s) {
            g.marks[{kind, sh->color, sh->size.value_or(3.5)}].push_back(sh->pts[0]);
        } else if (kind == u"diamond"_s) {
            g.diamonds[{sh->color, sh->size.value_or(3.5)}].push_back(sh->pts[0]);
        } else if (kind == u"x"_s) {
            g.xs[{sh->color, sh->width.value_or(2), sh->size.value_or(3.5)}].push_back(sh->pts[0]);
        } else {
            g.single.push_back(sh);
        }
    }
    return g;
}

}  // namespace

ShapeLayer::ShapeLayer()
{
    setZValue(8);
    setAcceptedMouseButtons(Qt::NoButton);
    // so paint() gets the area actually being redrawn (not the whole map) and can skip the rest
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
}

void ShapeLayer::set_bounds(long long w, long long h)
{
    prepareGeometryChange();
    _rect = QRectF(-20, -20, double(w + 40), double(h + 40));
}

void ShapeLayer::set_shapes(std::vector<Shape> s)
{
    shapes = std::move(s);
    _hit.clear();
    for (size_t i = 0; i < shapes.size(); ++i) {
        const Shape &sh = shapes[i];
        if (!sh.tag)
            continue;
        const QVariant kind = sh.tag->value(0);
        const auto &pts = sh.pts;
        if (sh.kind == u"line"_s) {
            for (size_t j = 0; j + 1 < pts.size(); ++j)
                _hit.push_back({pts[j].x(), pts[j].y(), pts[j + 1].x(), pts[j + 1].y(), (long long)i, kind, 0.0,
                                sh.width.value_or(2), sh.min_scale});
        } else {
            const bool big = sh.kind == u"x"_s || sh.kind == u"diamond"_s || sh.kind == u"army"_s ||
                             sh.kind == u"anchor"_s;
            _hit.push_back({pts[0].x(), pts[0].y(), pts[0].x(), pts[0].y(), (long long)i, kind, 1.0,
                            sh.size.value_or(3.5) * (big ? 1.3 : 1.0), sh.min_scale});
        }
    }
    _box.assign(shapes.size(), {});
    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto &pts = shapes[i].pts;
        if (pts.empty())
            throw std::runtime_error("min() arg is an empty sequence");
        double x0 = pts[0].x(), y0 = pts[0].y(), x1 = x0, y1 = y0;
        for (const QPointF &q : pts) {
            x0 = std::min(x0, q.x()), y0 = std::min(y0, q.y());
            x1 = std::max(x1, q.x()), y1 = std::max(y1, q.y());
        }
        _box[i] = {x0, y0, x1, y1, shapes[i].min_scale};
    }
    _gcache.reset();
    update();
}

// Where a list of rubber-band shapes is drawn (map pixels, with room for outlines and markers at the zoom `scale`);
// None for a kind whose size isn't known here (texts, pictures, badges …: then the whole layer is drawn again).
static std::optional<QRectF> temp_extent(const std::vector<Shape> &v, double scale)
{
    static const QSet<QString> simple = {u"line"_s, u"dot"_s, u"x"_s, u"diamond"_s, u"square"_s, u"circle"_s, u"poly"_s};
    const double s = ms_or_one(scale);
    QRectF r;
    for (const Shape &sh : v) {
        if (!simple.contains(sh.kind))
            return std::nullopt;
        // screen-sized parts (outline, marker, line width) as map pixels, generously; a circle's radius both ways
        const double px = 64.0 + 4.0 * (sh.width.value_or(0.0) + sh.size.value_or(0.0) + sh.radius);
        const double m = px / s + sh.radius + 2.0;
        auto add = [&](const QPointF &q) { r |= QRectF(q.x() - m, q.y() - m, 2 * m, 2 * m); };
        for (const QPointF &q : sh.pts)
            add(q);
        for (const auto &poly : sh.polys)
            for (const QPointF &q : poly)
                add(q);
    }
    return r;
}

void ShapeLayer::set_temp(std::vector<Shape> s)
{
    // (C++ only, 2026-11-14 — the user: "drawing lags") only where the old and the new rubber bands are: the brush
    // circle follows every mouse move, and a whole-map update drew the whole view again each time (the border lines
    // of Terrain / Rivers above all: 80 % of a terrain brush move on ZRM's Legacy)
    const auto was = temp_extent(temp, _scale), now = temp_extent(s, _scale);
    temp = std::move(s);
    if (was && now) {
        const QRectF r = (*was | *now) & boundingRect();
        if (!r.isEmpty())
            update(r);
        return;
    }
    update();
}

std::optional<QVariantList> ShapeLayer::hit(double x, double y, double tol, const std::optional<QStringList> &kinds) const
{
    if (_hit.empty())
        return std::nullopt;
    const double s = ms_or_one(_scale);
    // hit area = what is drawn (marker radius / half the line width) plus a little, at least `tol`
    const double dg = dot_grow(s), lg = line_grow(s);
    long long best = -1;
    double best_v = std::numeric_limits<double>::infinity();
    for (const Seg &h : _hit) {
        if (!(h.min_scale <= s))
            continue;
        if (kinds && !kinds->contains(h.kind.toString()))
            continue;
        double rs = (h.marker > 0 ? h.reach * dg + 3.0 : h.reach * lg / 2 + 3.0) / s;
        rs = std::max(rs, tol);
        const double dx = h.x2 - h.x1, dy = h.y2 - h.y1;
        const double ll = dx * dx + dy * dy;
        double t = ll > 0 ? ((x - h.x1) * dx + (y - h.y1) * dy) / ll : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double ex = h.x1 + t * dx - x, ey = h.y1 + t * dy - y;
        const double d = std::sqrt(ex * ex + ey * ey);
        if (!(d <= rs))
            continue;
        const double v = d / rs;
        if (v < best_v)                                // np.argmin: the first of equal values
            best_v = v, best = h.i;
    }
    if (best < 0)
        return std::nullopt;
    return shapes[size_t(best)].tag;
}

std::vector<long long> ShapeLayer::_visible(const QRectF &ex, double s) const
{
    std::vector<long long> out;
    if (_box.empty() || shapes.empty())
        return out;
    const double m = 40.0 / s;                         // markers / thick lines reach past their points
    for (size_t i = 0; i < _box.size(); ++i) {
        const auto &b = _box[i];
        if (b[2] >= ex.left() - m && b[0] <= ex.right() + m && b[3] >= ex.top() - m && b[1] <= ex.bottom() + m &&
            b[4] <= s)
            out.push_back((long long)i);
    }
    return out;
}

void ShapeLayer::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    if (shapes.empty() && temp.empty())
        return;
    const double s = ms_or_one(p->worldTransform().m11());
    _scale = s;
    // sizes follow the zoom: small when zoomed out (so provinces stay visible), big when zoomed in
    const double grow = line_grow(s);
    const double dot = dot_grow(s);
    p->setRenderHint(QPainter::Antialiasing);
    const QColor outline(12, 14, 18);
    const std::vector<long long> idx = _visible(opt->exposedRect, s);
    // the batches of the visible shapes are kept: zoomed out every repaint shows the same ones
    if (!_gcache || _gcache_key != idx) {
        std::vector<const Shape *> v;
        for (long long i : idx)
            v.push_back(&shapes[size_t(i)]);
        _gcache = std::make_shared<Groups>(group_of(v, true));
        _gcache_key = idx;
    }
    const Groups *gp = _gcache.get();
    Groups merged;
    if (!temp.empty()) {                               // rubber bands while editing: never kept
        std::vector<const Shape *> v;
        for (const Shape &t : temp)
            v.push_back(&t);
        merged = *_gcache;
        merged.extend(group_of(v, false));
        gp = &merged;
    }
    const Groups &g = *gp;
    // round line ends only when few shapes are in view: with thousands (zoomed out) they cost 6x as much —
    // then the lines end flat and their dark outline square (it still frames the ends)
    const bool few = idx.size() + temp.size() <= 400;
    const Qt::PenCapStyle cap_line = few ? Qt::RoundCap : Qt::FlatCap;
    const Qt::PenCapStyle cap_under = few ? Qt::RoundCap : Qt::SquareCap;
    // 0) pictures (x, y, w, h in map pixels), sharp — each picture pixel where it falls on the map's pixels
    for (const Shape *sh : g.images) {
        p->save();
        p->setOpacity(sh->opacity);
        p->setRenderHint(QPainter::SmoothPixmapTransform, sh->smooth);
        p->drawImage(sh->rect, sh->img);
        if (sh->frame) {                               // a thin frame: where the picture ends (no thick outline)
            QPen fp(qc(*sh->frame), 1.2);
            fp.setCosmetic(true);
            fp.setStyle(Qt::DashLine);
            p->setOpacity(0.9);
            p->setPen(fp);
            p->setBrush(Qt::NoBrush);
            p->drawRect(sh->rect);
        }
        p->restore();
    }
    // 1) dark underlay below every line, so lines stand out on any background
    for (const auto &[w, segs] : g.under.items) {
        QPen pen(outline, w * grow + 3 * std::pow(grow, 0.5));
        pen.setCosmetic(true);
        pen.setCapStyle(cap_under);
        p->setPen(pen);
        p->drawLines(segs.data(), int(segs.size()));
    }
    // 2) white halo behind highlighted shapes (batched like the shapes themselves)
    const QColor white(255, 255, 255);
    for (const auto &[w, segs] : g.halo_lines.items) {
        QPen pen(white, w * grow + 6 * std::pow(grow, 0.5));
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        p->drawLines(segs.data(), int(segs.size()));
    }
    for (const auto &[ks, pts] : g.halo_marks.items) {
        const auto &[kind, size] = ks;
        const double r = size * dot + 3 * std::pow(dot, 0.5);
        QPen pen(white, 2 * r * (kind == u"diamond"_s ? 1.3 : 1.0));
        pen.setCosmetic(true);
        pen.setCapStyle(kind != u"square"_s ? Qt::RoundCap : Qt::SquareCap);
        p->setPen(pen);
        p->drawPoints(pts.data(), int(pts.size()));
    }
    for (const Shape *sh : g.halo_single)
        _draw_one(p, *sh, s, grow, dot, true, outline);
    // 3) lines
    for (const auto &[k, segs] : g.lines.items) {
        const auto &[col, w, dash] = k;
        QPen pen(qc(col), w * grow);
        pen.setCosmetic(true);
        pen.setCapStyle(cap_line);
        if (dash)
            pen.setStyle(Qt::DashLine);
        p->setPen(pen);
        p->drawLines(segs.data(), int(segs.size()));
    }
    // 4) dots and squares: a thick cosmetic pen draws a whole group of round / square points at once
    for (const auto &[k, pts] : g.marks.items) {
        const auto &[kind, col, size] = k;
        const double d = 2 * size * dot;
        const Qt::PenCapStyle cap = kind == u"dot"_s ? Qt::RoundCap : Qt::SquareCap;
        const std::pair<QColor, double> passes[2] = {{outline, d + 2.0}, {qc(col), d}};
        for (const auto &[c, w] : passes) {
            QPen pen(c, w);
            pen.setCosmetic(true);
            pen.setCapStyle(cap);
            p->setPen(pen);
            p->drawPoints(pts.data(), int(pts.size()));
        }
    }
    // 5) diamonds: one path per colour / size
    for (const auto &[k, pts] : g.diamonds.items) {
        const auto &[col, size] = k;
        const double r = size * dot * 1.3 / s;
        QPainterPath path;
        for (const QPointF &q : pts) {
            const double x = q.x(), y = q.y();
            path.addPolygon(QPolygonF{QPointF(x, y - r), QPointF(x + r, y), QPointF(x, y + r), QPointF(x - r, y),
                                      QPointF(x, y - r)});
        }
        p->setPen(QPen(outline, 0));
        p->setBrush(qc(col));
        p->drawPath(path);
    }
    // 6) x marks: two strokes each, all of one colour / size in one call (an outline pass, then the colour)
    for (const auto &[k, pts] : g.xs.items) {
        const auto &[col, w, size] = k;
        const double r = size * dot / s;
        std::vector<QLineF> segs;
        for (const QPointF &q : pts) {
            const double x = q.x(), y = q.y();
            segs.emplace_back(x - r, y - r, x + r, y + r);
            segs.emplace_back(x - r, y + r, x + r, y - r);
        }
        const double ww = w * std::pow(dot, 0.8);
        for (int pass = 0; pass < 2; ++pass) {
            QPen xp(pass == 0 ? outline : qc(col), ww + (pass == 0 ? 2.5 * std::pow(dot, 0.5) : 0));
            xp.setCosmetic(true);
            xp.setCapStyle(pass == 0 ? cap_under : cap_line);
            p->setPen(xp);
            p->drawLines(segs.data(), int(segs.size()));
        }
    }
    // 7) the rest one by one
    for (const Shape *sh : g.single)
        _draw_one(p, *sh, s, grow, dot, false, outline);
    p->setBrush(Qt::NoBrush);
}

// unit markers, drawn in screen pixels at the shape's point: "badge" (a disc with a number; hollow = a ring), "army"
// (a shield with a number), "anchor" (a navy), "label" (text under the point)
void ShapeLayer::_draw_unit(QPainter *p, const Shape &sh, double s, double dot, bool halo_pass)
{
    const QString &kind = sh.kind;
    const double x = sh.pts[0].x(), y = sh.pts[0].y();
    const double k = std::max(0.55, std::min(1.35, dot * 1.25));   // medium size: never tiny, not huge zoomed in
    const double r = sh.size.value_or(8.0) * k;
    const QColor dark(12, 14, 18);
    p->save();
    p->translate(x, y);
    p->scale(1.0 / s, 1.0 / s);
    if (halo_pass) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255));
        if (kind == u"army"_s) {
            p->drawRoundedRect(QRectF(-r * 1.25 - 4, -r * 1.25 - 4, r * 2.5 + 8, r * 2.5 + 8), 6, 6);
        } else if (kind == u"badge"_s) {
            const QString &text = sh.text;
            QFont f(u"Segoe UI"_s, int(std::max<long long>(6, py::round_int(r * 0.95))));
            f.setBold(true);
            const double w = std::max(2 * r, (!text.isEmpty() ? QFontMetricsF(f).horizontalAdvance(text) : 0.0) + r * 0.9);
            if (sh.warn) {
                p->scale(WARN_SCALE, WARN_SCALE);
                p->drawPolygon(QPolygonF{QPointF(0, -r * 1.55 - 6), QPointF(w / 2 + r * 0.45 + 6, r * 1.0 + 4),
                                         QPointF(-w / 2 - r * 0.45 - 6, r * 1.0 + 4)});
            } else {
                p->drawRoundedRect(QRectF(-w / 2 - 4, -r - 4, w + 8, 2 * r + 8), r + 4, r + 4);
            }
        } else if (kind != u"label"_s) {
            p->drawEllipse(QPointF(0, 0), r + 4, r + 4);
        }
        p->restore();
        return;
    }
    const QColor col = qc(sh.color);
    const QString &text = sh.text;
    auto light = [](const QColor &c) {
        return (c.red() * 0.3 + c.green() * 0.59 + c.blue() * 0.11) < 150 ? QColor(255, 255, 255) : QColor(20, 20, 24);
    };
    if (kind == u"label"_s) {
        QFont f(u"Segoe UI"_s, int(std::max<long long>(7, py::round_int(9 * k))));
        f.setBold(true);
        QPainterPath path;
        path.addText(0, 0, f, text);
        const QRectF br = path.boundingRect();
        path.translate(-br.width() / 2 - br.left(), r * 0.0 + sh.dy * k - br.top() * 0.0);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(dark, 3.2));
        p->setBrush(Qt::NoBrush);
        p->drawPath(path);
        p->setPen(Qt::NoPen);
        p->setBrush(col);
        p->drawPath(path);
        p->restore();
        return;
    }
    std::optional<QColor> tcol;
    if (kind == u"badge"_s) {
        // a disc, or a pill as wide as its text ("2/5")
        QFont f(u"Segoe UI"_s, int(std::max<long long>(6, py::round_int(r * 0.95))));
        f.setBold(true);
        const double tw = !text.isEmpty() ? QFontMetricsF(f).horizontalAdvance(text) : 0.0;
        const double w = std::max(2 * r, tw + r * 0.9);
        const QRectF box(-w / 2, -r, w, 2 * r);
        if (sh.warn) {
            // a warning triangle (road sign) in the badge's colours, the text in its lower part; the pill's footprint
            // (same width, a little taller for the point): it scales exactly like the pills, 15 % smaller
            p->scale(WARN_SCALE, WARN_SCALE);
            const QPolygonF tri{QPointF(0, -r * 1.55), QPointF(w / 2 + r * 0.45, r * 1.0),
                                QPointF(-w / 2 - r * 0.45, r * 1.0)};
            p->setPen(QPen(dark, 4.5));
            p->setBrush(Qt::NoBrush);
            p->drawPolygon(tri);
            p->setPen(QPen(QColor(255, 214, 64), 2.4));
            p->setBrush(col);
            p->drawPolygon(tri);
            const QColor tc = light(col);
            if (!text.isEmpty() && r >= 5) {
                p->setFont(f);
                p->setPen(tc);
                drawtext::draw(*p, box.translated(0, r * 0.2), Qt::AlignCenter, text);
            }
            p->restore();
            return;
        }
        QColor tc;
        if (sh.hollow) {
            p->setBrush(QColor(20, 22, 28, 215));
            p->setPen(QPen(dark, 5.0));
            p->drawRoundedRect(box, r, r);
            p->setPen(QPen(col, 2.6));
            p->drawRoundedRect(box, r, r);
            tc = col;
        } else {
            p->setPen(QPen(dark, 2.0));
            p->setBrush(col);
            p->drawRoundedRect(box, r, r);
            if (sh.ring) {
                p->setPen(QPen(qc(*sh.ring), 2.2));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(box.adjusted(1.5, 1.5, -1.5, -1.5), r - 1.5, r - 1.5);
            }
            tc = light(col);
        }
        if (!text.isEmpty() && r >= 5) {
            p->setFont(f);
            p->setPen(tc);
            drawtext::draw(*p, box, Qt::AlignCenter, text);
        }
        p->restore();
        return;
    } else if (kind == u"army"_s) {
        const double w = r * 2.3, h = r * 2.5;
        QPainterPath shield;
        shield.moveTo(-w / 2, -h / 2);
        shield.lineTo(w / 2, -h / 2);
        shield.lineTo(w / 2, h * 0.08);
        shield.quadTo(w / 2, h * 0.38, 0, h / 2);
        shield.quadTo(-w / 2, h * 0.38, -w / 2, h * 0.08);
        shield.closeSubpath();
        p->setPen(QPen(dark, 4.2));
        p->setBrush(Qt::NoBrush);
        p->drawPath(shield);
        p->setPen(QPen(QColor(255, 255, 255, 235), 1.8));
        p->setBrush(col);
        p->drawPath(shield);
        tcol = QColor(255, 255, 255);
    } else {                                           // anchor: a navy
        p->setPen(QPen(dark, 4.0));
        p->setBrush(QColor(28, 44, 70));
        p->drawEllipse(QPointF(0, 0), r * 1.1, r * 1.1);
        p->setPen(QPen(col, 2.2));
        p->drawEllipse(QPointF(0, 0), r * 1.1, r * 1.1);
        QPen pen(col, std::max(1.6, 1.9 * k));
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        const double a = r * 0.62;
        p->drawLine(QPointF(0, -a), QPointF(0, a));
        p->drawLine(QPointF(-a * 0.55, -a * 0.45), QPointF(a * 0.55, -a * 0.45));
        p->drawArc(QRectF(-a * 0.9, -a * 0.2, a * 1.8, a * 1.2), 200 * 16, 140 * 16);
        p->drawEllipse(QPointF(0, -a - 1.2), 1.4, 1.4);
    }
    if (!text.isEmpty() && tcol && r >= 6) {
        QFont f(u"Segoe UI"_s, int(std::max<long long>(6, py::round_int(r * (text.size() < 3 ? 0.95 : 0.75)))));
        f.setBold(true);
        p->setFont(f);
        p->setPen(*tcol);
        QRectF box(-r * 1.3, -r * 1.3, r * 2.6, r * 2.6);
        if (kind == u"army"_s)
            box.translate(0, -r * 0.12);
        drawtext::draw(*p, box, Qt::AlignCenter, text);
    }
    p->restore();
}

void ShapeLayer::_draw_one(QPainter *p, const Shape &sh, double s, double grow, double dot, bool halo_pass,
                           const QColor &outline)
{
    if (is_unit(sh.kind)) {
        _draw_unit(p, sh, s, dot, halo_pass);
        return;
    }
    if (sh.kind == u"poly"_s) {                        // filled outlines in map pixels (a canal model from above)
        QPainterPath path;
        for (const auto &poly : sh.polys)
            path.addPolygon(QPolygonF(QList<QPointF>(poly.begin(), poly.end())));
        if (halo_pass) {
            QPen hp(QColor(255, 255, 255), 2.5 + 4 * std::pow(dot, 0.5));
            hp.setCosmetic(true);
            hp.setJoinStyle(Qt::RoundJoin);
            p->setPen(hp);
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            return;
        }
        QColor fill = qc(sh.color);
        fill.setAlpha(sh.alpha);
        QPen op(QColor(12, 14, 18), sh.width.value_or(2.0) * std::max(0.8, std::pow(dot, 0.5)));
        op.setCosmetic(true);
        op.setJoinStyle(Qt::RoundJoin);
        if (sh.dash)
            op.setStyle(Qt::DashLine);
        p->setPen(op);
        p->setBrush(fill);
        p->drawPath(path);
        p->setBrush(Qt::NoBrush);
        return;
    }
    const QColor col = halo_pass ? QColor(255, 255, 255) : qc(sh.color);
    const double w = sh.width.value_or(2) * grow + (halo_pass ? 6 * std::pow(grow, 0.5) : 0);
    const QString &kind = sh.kind;
    const auto &pts = sh.pts;
    QPen pen(col, w);
    pen.setCosmetic(true);
    pen.setCapStyle(Qt::RoundCap);
    if (sh.dash && !halo_pass)
        pen.setStyle(Qt::DashLine);
    if (kind == u"line"_s) {
        p->setPen(pen);
        const auto segs = segments(pts);
        p->drawLines(segs.data(), int(segs.size()));
        return;
    }
    const double x = pts[0].x(), y = pts[0].y();
    if (kind == u"circle"_s) {                         // radius in map pixels (brush outline / problem ring)
        const double rr = sh.radius;
        const double lw = sh.width.value_or(1.5);
        const std::pair<QColor, double> passes[2] = {{QColor(12, 14, 18), lw + 2.5}, {col, lw}};
        for (const auto &[c, ww] : passes) {
            QPen cp(c, ww);
            cp.setCosmetic(true);
            p->setPen(cp);
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(QPointF(x, y), rr, rr);
        }
        return;
    }
    const double r = (sh.size.value_or(3.5) * dot + (halo_pass ? 3 * std::pow(dot, 0.5) : 0)) / s;
    if (kind == u"dot"_s) {
        p->setPen(QPen(!halo_pass ? outline : col, 0));
        p->setBrush(col);
        p->drawEllipse(QPointF(x, y), r, r);
    } else if (kind == u"x"_s) {
        const double ww = sh.width.value_or(2) * std::pow(dot, 0.8) + (halo_pass ? 6 * std::pow(dot, 0.5) : 0);
        const std::pair<QColor, double> passes[2] = {{!halo_pass ? outline : col, 2.5 * std::pow(dot, 0.5)}, {col, 0}};
        for (const auto &[c, extra] : passes) {
            QPen xp(QColor(c), ww + extra);
            xp.setCosmetic(true);
            xp.setCapStyle(Qt::RoundCap);
            p->setPen(xp);
            p->drawLine(QPointF(x - r, y - r), QPointF(x + r, y + r));
            p->drawLine(QPointF(x - r, y + r), QPointF(x + r, y - r));
            if (halo_pass)
                break;
        }
    } else if (kind == u"diamond"_s || kind == u"square"_s) {
        p->setPen(QPen(!halo_pass ? outline : col, 0));
        p->setBrush(col);
        if (kind == u"square"_s)
            p->drawRect(QRectF(x - r, y - r, 2 * r, 2 * r));
        else
            p->drawPolygon(QPolygonF{QPointF(x, y - r * 1.3), QPointF(x + r * 1.3, y), QPointF(x, y + r * 1.3),
                                     QPointF(x - r * 1.3, y)});
    }
}

// ---------------------------------------------------------------------------------------------- EdgeLayer
EdgeLayer::EdgeLayer()
{
    setZValue(4);
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
}

void EdgeLayer::set_data(MapData *d)
{
    prepareGeometryChange();
    data = d;
    _rect = d ? QRectF(0, 0, double(d->w), double(d->h)) : QRectF();
    update();
}

void EdgeLayer::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    const MapData *d = data;
    if (!d || !(prov || state))
        return;
    const double s = ms_or_one(p->worldTransform().m11());
    if (s < MIN_SCALE)
        return;
    const QRectF ex = opt->exposedRect;
    const long long x0 = std::max(0LL, (long long)ex.left() - 1), y0 = std::max(0LL, (long long)ex.top() - 1);
    const long long x1 = std::min(d->w, (long long)ex.right() + 2), y1 = std::min(d->h, (long long)ex.bottom() + 2);
    if (x1 - x0 < 2 || y1 - y0 < 2 || (x1 - x0) * (y1 - y0) > 4'000'000)
        return;
    const auto ids = id_window(d->idmap, y0, y1, x0, x1);
    struct L {
        grid::Grid<std::int32_t> arr;
        QColor col;
        double w;
    };
    std::vector<L> layers;
    if (prov) {
        grid::Grid<std::int32_t> a(ids.h(), ids.w());
        for (size_t i = 0; i < ids.flat().size(); ++i)
            a.flat()[i] = ids.flat()[i];
        layers.push_back({std::move(a), QColor(20, 20, 24, 200), 1.0});
    }
    if (state && !d->reg_idx.empty()) {
        grid::Grid<std::int32_t> a(ids.h(), ids.w());
        for (size_t i = 0; i < ids.flat().size(); ++i)
            a.flat()[i] = d->reg_idx[ids.flat()[i]];
        layers.push_back({std::move(a), QColor(0, 0, 0, 255), 2.2});
    }
    for (const L &l : layers) {
        const auto &arr = l.arr;
        std::vector<QLineF> segs;
        for (long long y = 0; y + 1 < arr.h(); ++y)        // between rows: a horizontal edge
            for (long long x = 0; x < arr.w(); ++x)
                if (arr(y + 1, x) != arr(y, x))
                    segs.emplace_back(double(x + x0), double(y + y0 + 1), double(x + x0 + 1), double(y + y0 + 1));
        for (long long y = 0; y < arr.h(); ++y)            // between columns: a vertical edge
            for (long long x = 0; x + 1 < arr.w(); ++x)
                if (arr(y, x + 1) != arr(y, x))
                    segs.emplace_back(double(x + x0 + 1), double(y + y0), double(x + x0 + 1), double(y + y0 + 1));
        if (!segs.empty()) {
            QPen pen(l.col, l.w);
            pen.setCosmetic(true);
            pen.setCapStyle(Qt::FlatCap);
            p->setPen(pen);
            p->drawLines(segs.data(), int(segs.size()));
        }
    }
}

// ---------------------------------------------------------------------------------------------- NameLayer
NameLayer::NameLayer() : font(u"Georgia"_s)
{
    setZValue(7);
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    font.setPixelSize(100);
    font.setBold(true);
}

void NameLayer::set_bounds(long long w, long long h)
{
    prepareGeometryChange();
    _rect = QRectF(-20, -20, double(w + 40), double(h + 40));
}

void NameLayer::set_items(std::vector<NameItem> its)
{
    items = std::move(its);
    _arr.clear();
    for (const NameItem &it : items)
        _arr.push_back({it.cx, it.cy, std::max(it.w, it.h), it.h});
    update();
}

// the name's outline (built once per text), its advance width and height at 100 px
const NameLayer::PathOf &NameLayer::_path(const QString &text)
{
    auto it = _paths.find(text);
    if (it == _paths.end()) {
        const QFontMetricsF fm(font);
        double tw = fm.horizontalAdvance(text);
        if (tw == 0.0)
            tw = 1.0;
        const double th = fm.ascent() + fm.descent();
        QPainterPath path;
        path.addText(QPointF(-tw / 2, -th / 2 + fm.ascent()), font, text);
        it = _paths.emplace(text, PathOf{path, tw, th}).first;
    }
    return it->second;
}

void NameLayer::paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *)
{
    const double s = ms_or_one(p->worldTransform().m11());
    if (s < 0.35 || items.empty())
        return;
    const QRectF ex = opt->exposedRect;
    std::vector<size_t> idx;
    for (size_t i = 0; i < _arr.size(); ++i) {
        const auto &a = _arr[i];
        if (a[0] + a[2] >= ex.left() && a[0] - a[2] <= ex.right() && a[1] + a[2] >= ex.top() &&
            a[1] - a[2] <= ex.bottom() && a[3] * s >= 3)
            idx.push_back(i);
    }
    if (idx.empty())
        return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    for (size_t i : idx) {
        const NameItem &it = items[i];
        const auto [pm, sc] = _image(it.text, it.rgb, it.w, it.h, it.h * s);
        p->save();
        p->translate(it.cx, it.cy);
        p->rotate(it.angle * (180.0 / PI));
        p->scale(sc / s, sc / s);
        p->drawPixmap(QPointF(-pm.width() / 2.0, -pm.height() / 2.0), pm);
        p->restore();
    }
}

// the name pre-drawn (fill + dark outline) at about its on-screen height `hs`, so panning only copies images. Images
// come in size steps of 12%; one within 3 steps (~1.4x) is reused while zooming. Returns (pixmap, on-screen px per
// image px).
std::pair<QPixmap, double> NameLayer::_image(const QString &text, const Rgb &rgb, double w, double h, double hs)
{
    const long long want = py::round_int(std::log(std::max(hs, 1.0)) / std::log(1.12));
    const auto key = std::make_tuple(text, std::get<0>(rgb), std::get<1>(rgb), std::get<2>(rgb), py::round_to(w / h, 3));
    auto it = _imgs.find(key);
    if (it == _imgs.end()) {
        if (_imgs.size() > 8000)
            _imgs.clear();
        it = _imgs.emplace(key, std::vector<std::pair<long long, QPixmap>>()).first;
    }
    auto &steps = it->second;
    const std::pair<long long, QPixmap> *best = nullptr;
    for (const auto &st : steps)                       // min(steps, key=abs(k - want)): the first of equals
        if (!best || std::llabs(st.first - want) < std::llabs(best->first - want))
            best = &st;
    if (!best || std::llabs(best->first - want) > 3) {
        QPixmap pm = _draw_name(text, rgb, w, h, std::pow(1.12, double(want)));
        auto at = std::find_if(steps.begin(), steps.end(), [&](const auto &st) { return st.first == want; });
        if (at != steps.end())
            at->second = pm;
        else
            steps.emplace_back(want, pm);
        return {pm, hs / std::pow(1.12, double(want))};
    }
    return {best->second, hs / std::pow(1.12, double(best->first))};
}

QPixmap NameLayer::_draw_name(const QString &text, const Rgb &rgb, double w, double h, double hq)
{
    const double wq = hq * w / h;
    const int pad = 3;
    QImage img(int(std::ceil(wq)) + 2 * pad, int(std::ceil(hq)) + 2 * pad, QImage::Format_ARGB32_Premultiplied);
    img.fill(0);
    const PathOf &po = _path(text);
    QPainter q(&img);
    q.setRenderHint(QPainter::Antialiasing);
    q.translate(img.width() / 2.0, img.height() / 2.0);
    q.scale(wq / po.tw, hq / po.th);
    q.setPen(QPen(QColor(12, 14, 18, 200), 2.2 * 100 / std::max(8.0, hq)));
    q.setBrush(Qt::NoBrush);
    q.drawPath(po.path);
    q.setPen(Qt::NoPen);
    q.setBrush(qc(rgb));
    q.drawPath(po.path);
    q.end();
    return QPixmap::fromImage(img);
}

// ---------------------------------------------------------------------------------------------- MapView
MapView::MapView(QWidget *parent) : QGraphicsView(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setScene(new QGraphicsScene(this));
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    // the view paints every pixel itself (background brush), so Qt can scroll by shifting the picture and only
    // draw the strip that comes into view, instead of repainting everything on each pan step
    setBackgroundBrush(QColor(theme::BG));
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAutoFillBackground(false);
    item = new BaseItem();
    scene()->addItem(item);
    // selection layer: red outline + crosshatch on its own item, so it can pulse cheaply
    sel_item = new QGraphicsPixmapItem();
    sel_item->setTransformationMode(Qt::FastTransformation);
    sel_item->setZValue(5);
    sel_item->setAcceptedMouseButtons(Qt::NoButton);
    scene()->addItem(sel_item);
    // focus layer: what the Events tab's editor has under the cursor (a nation, provinces, a culture …)
    focus_item = new QGraphicsPixmapItem();
    focus_item->setTransformationMode(Qt::FastTransformation);
    focus_item->setZValue(6);
    focus_item->setAcceptedMouseButtons(Qt::NoButton);
    focus_item->setVisible(false);
    scene()->addItem(focus_item);
    // culture / religion patterns: drawn once at the map's own resolution — they scale with the map
    pattern_item = new QGraphicsPixmapItem();
    pattern_item->setTransformationMode(Qt::SmoothTransformation);
    pattern_item->setZValue(5.5);
    pattern_item->setAcceptedMouseButtons(Qt::NoButton);
    pattern_item->setVisible(false);
    scene()->addItem(pattern_item);
    // lines / markers (adjacencies); click_hook(x, y, tol, modifiers) -> true if it used the click
    shape_layer = new ShapeLayer();
    shape_layer->setVisible(false);
    scene()->addItem(shape_layer);
    name_layer = new NameLayer();
    name_layer->setVisible(false);
    scene()->addItem(name_layer);
    edge_layer = new EdgeLayer();                      // borders between the pixels (terrain / rivers: no tint)
    edge_layer->setVisible(false);
    scene()->addItem(edge_layer);
    _pulse = new QTimer(this);
    _pulse->setInterval(40);
    connect(_pulse, &QTimer::timeout, this, &MapView::_tick);
    // the pulse pauses while the camera moves: with a big selection every pulse step would repaint the whole view
    // and fight with the panning / zooming
    _calm = new QTimer(this);
    _calm->setSingleShot(true);
    _calm->setInterval(350);
    connect(_calm, &QTimer::timeout, this, &MapView::_resume_pulse);
    _focus_timer = new QTimer(this);                   // (Python makes it at the first zoom that needs it)
    _focus_timer->setSingleShot(true);
    _focus_timer->setInterval(160);
    connect(_focus_timer, &QTimer::timeout, this, &MapView::_render_focus);
}

MapView::~MapView() = default;

// ------------------------------------------------------------------ data
void MapView::set_data(MapData *d)
{
    data = d;
    hl.assign(size_t(d->n), 0);
    shape_layer->set_bounds(d->w, d->h);
    name_layer->set_bounds(d->w, d->h);
    // generous margins so the map can be panned/zoomed around any point, even its edges
    scene()->setSceneRect(QRectF(double(-d->w), double(-d->h), double(d->w * 3), double(d->h * 3)));
}

// pixel colours of a map window. Q = 0: one colour per province; Q > 0: Q colours per province, laid out as
// diagonal stripes (x + y) with a period of Q pixels — a "mix" view where each colour covers its share
RgbImage MapView::_colors(const ColorTable &table, long long y0, long long y1, long long x0, long long x1)
{
    if (table.layer)                                   // a pixel layer (terrain.bmp / rivers.bmp view)
        return table.layer->colors(y0, y1, x0, x1);
    const auto ids = id_window(data->idmap, y0, y1, x0, x1);
    RgbImage out(ids.h(), ids.w());
    const auto &t = table.rgb;
    for (long long y = 0; y < ids.h(); ++y)
        for (long long x = 0; x < ids.w(); ++x) {
            size_t at = ids(y, x);
            if (table.Q)
                at = size_t(at * table.Q + py::mod(x0 + x + y0 + y, table.Q));
            out(y, x) = {t[at * 3], t[at * 3 + 1], t[at * 3 + 2]};
        }
    return out;
}

// table (n, 3) or (n, Q, 3) -> packed 0xFFRRGGBB colours per (id[, q], border class): class bit 1 = province border,
// 2 = state border, 4 = sea border (as _shade does it pixel by pixel)
std::vector<std::uint32_t> MapView::_shaded_table(const ColorTable &table, const QString &view_mode)
{
    const bool st = view_mode == u"state"_s;
    const size_t rows = table.rgb.size() / 3;
    std::vector<std::uint32_t> out(rows * 8);
    for (size_t i = 0; i < rows; ++i)
        for (int c = 0; c < 8; ++c) {
            std::uint16_t v[3] = {table.rgb[i * 3], table.rgb[i * 3 + 1], table.rgb[i * 3 + 2]};
            if (c & 1)
                for (auto &x : v)
                    x = st ? x * 4 / 5 : x * 11 / 20;
            if (c & 2) {
                if (st)
                    v[0] = std::get<0>(STATE_BORDER_RGB), v[1] = std::get<1>(STATE_BORDER_RGB),
                    v[2] = std::get<2>(STATE_BORDER_RGB);
                else
                    for (auto &x : v)
                        x = x * 3 / 10;
            }
            if (c & 4)
                for (auto &x : v)
                    x = std::min(255, x * 13 / 10 + 6);
            out[i * 8 + size_t(c)] = pack(v[0], v[1], v[2]);
        }
    return out;
}

// darken province / state borders and lighten sea-tile borders (in place) — a pixel layer's window
void MapView::_shade(RgbImage &base, const MapData &d, long long y0, long long x0, const QString &view_mode)
{
    const bool st = view_mode == u"state"_s;
    for (long long y = 0; y < base.h(); ++y)
        for (long long x = 0; x < base.w(); ++x) {
            auto &px = base(y, x);
            if (d.prov_border(y0 + y, x0 + x))
                for (auto &c : px)
                    c = std::uint8_t(st ? c * 4 / 5 : c * 11 / 20);
            if (d.state_border(y0 + y, x0 + x)) {
                if (st)
                    px = {std::uint8_t(std::get<0>(STATE_BORDER_RGB)), std::uint8_t(std::get<1>(STATE_BORDER_RGB)),
                          std::uint8_t(std::get<2>(STATE_BORDER_RGB))};
                else
                    for (auto &c : px)
                        c = std::uint8_t(c * 3 / 10);
            }
            if (d.sea_border(y0 + y, x0 + x))
                for (auto &c : px)
                    c = std::uint8_t(std::min(255, c * 13 / 10 + 6));
        }
}

// the shaded map colours of a window as 0xFFRRGGBB
std::vector<std::uint32_t> MapView::_render_window(const ColorTable &table, const QString &view_mode, long long y0,
                                                   long long y1, long long x0, long long x1,
                                                   const std::vector<std::uint32_t> *packed)
{
    MapData &d = *data;
    std::vector<std::uint32_t> out(size_t((y1 - y0) * (x1 - x0)));
    if (table.layer) {                                 // a pixel layer (terrain.bmp / rivers.bmp view): pixel by pixel
        RgbImage rgb = table.layer->colors(y0, y1, x0, x1);
        if (!table.layer->pure_pixels())               // (terrain / rivers: the exact colours — the borders are the
            _shade(rgb, d, y0, x0, view_mode);         //  EdgeLayer's lines between pixels)
        for (size_t i = 0; i < out.size(); ++i) {
            const auto &px = rgb.flat()[i];
            out[i] = pack(px[0], px[1], px[2]);
        }
        return out;
    }
    std::vector<std::uint32_t> own;
    if (!packed) {
        own = _shaded_table(table, view_mode);
        packed = &own;
    }
    // Q > 0 — stripes: Q colours per province along the diagonal (x + y), period Q
    const auto &key = table.Q ? d.pix_key_mix(std::int32_t(table.Q)) : d.pix_key();
    size_t i = 0;
    for (long long y = y0; y < y1; ++y) {
        const std::int32_t *row = key.row(y);
        for (long long x = x0; x < x1; ++x)
            out[i++] = (*packed)[size_t(row[x])];
    }
    return out;
}

// table: colours per province id (or stripes, or a pixel layer). Rebuilds the whole map image.
void MapView::render_base(const ColorTable &table, const QString &view_mode)
{
    MapData *d = data;
    _table = table;
    _view_mode = view_mode;
    const bool pure = table.layer && table.layer->pure_pixels();
    if (edge_layer->data != d)
        edge_layer->set_data(d);
    edge_layer->setVisible(pure);
    edge_layer->update();
    if (table.layer)
        _packed.reset();
    else
        _packed = _shaded_table(table, view_mode);
    _base32 = _render_window(table, view_mode, 0, d->h, 0, d->w, _packed ? &*_packed : nullptr);
    has_base = true;
    _on_top(table, 0, d->h, 0, d->w);
    _img32 = _base32;
    std::vector<long long> on;
    for (size_t i = 0; i < hl.size(); ++i)
        if (hl[i])
            on.push_back((long long)i);
    const auto box = d->bbox(on);
    if (box)
        _overlay(*box);
    hl_box = box;
    _push_full();
}

// pixels a pixel-layer view wants shown unshaded (e.g. terrain.bmp land / sea mismatches): painted over the border
// shading, so they stay bright on coasts and borders
void MapView::_on_top(const ColorTable &table, long long y0, long long y1, long long x0, long long x1)
{
    if (!table.layer && !table.on_top)
        return;
    const auto got = table.layer ? table.layer->on_top(y0, y1, x0, x1) : table.on_top(y0, y1, x0, x1);
    if (!got)
        return;
    const long long W = data->w, ww = x1 - x0;
    for (const PixelLayer::Top &t : *got) {
        for (std::int64_t f : t.idx) {
            std::uint32_t &v = _base32[size_t((y0 + f / ww) * W + x0 + f % ww)];
            if (!t.rgb) {
                const auto c = unpack(v);
                v = pack(int(c[0] * 0.3), int(c[1] * 0.3), int(c[2] * 0.3));
            } else {
                v = pack(std::get<0>(*t.rgb), std::get<1>(*t.rgb), std::get<2>(*t.rgb));
            }
        }
        for (long long y = 0; y < t.mask.h(); ++y)
            for (long long x = 0; x < t.mask.w(); ++x) {
                if (!t.mask(y, x))
                    continue;
                std::uint32_t &v = _base32[size_t((y0 + y) * W + x0 + x)];
                if (!t.rgb) {                          // darken (e.g. the edge around problem pixels)
                    const auto c = unpack(v);
                    v = pack(int(c[0] * 0.3), int(c[1] * 0.3), int(c[2] * 0.3));
                } else {
                    v = pack(std::get<0>(*t.rgb), std::get<1>(*t.rgb), std::get<2>(*t.rgb));
                }
            }
    }
}

// redraw the map image inside box (after pixels there changed), with the current colours
void MapView::redraw_box(const std::array<long long, 4> &box)
{
    if (!has_base || !_table)
        return;
    const MapData &d = *data;
    const long long x0 = std::max(0LL, box[0] - 2), y0 = std::max(0LL, box[1] - 2);
    const long long x1 = std::min(d.w, box[2] + 2), y1 = std::min(d.h, box[3] + 2);
    const auto win = _render_window(*_table, _view_mode, y0, y1, x0, x1, _packed ? &*_packed : nullptr);
    const long long W = d.w, ww = x1 - x0;
    for (long long y = y0; y < y1; ++y)
        std::copy_n(win.begin() + (y - y0) * ww, ww, _base32.begin() + (y * W + x0));
    _on_top(*_table, y0, y1, x0, x1);
    for (long long y = y0; y < y1; ++y)
        std::copy_n(_base32.begin() + (y * W + x0), ww, _img32.begin() + (y * W + x0));
    _overlay({x0, y0, x1, y1});
    _push_rect({x0, y0, x1, y1});
}

// red outline (2px) + faint red crosshatch over the given provinces, gently pulsing
void MapView::set_selection_layer(const std::vector<long long> &pids0, bool force)
{
    const MapData &d = *data;
    std::vector<long long> pids;
    for (long long p : pids0)
        if (0 < p && p < d.n && d.area[size_t(p)] > 0)
            pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    if (pids == sel_ids && !force)
        return;
    sel_ids = pids;
    const auto box = d.bbox(pids);
    if (!box) {
        sel_item->setVisible(false);
        _pulse->stop();
        return;
    }
    const long long x0 = std::max(0LL, (*box)[0] - 2), y0 = std::max(0LL, (*box)[1] - 2);
    const long long x1 = std::min(d.w, (*box)[2] + 2), y1 = std::min(d.h, (*box)[3] + 2);
    std::vector<std::uint8_t> lut(size_t(d.n), 0);
    for (long long p : pids)
        lut[size_t(p)] = 1;
    const auto ids = id_window(d.idmap, y0, y1, x0, x1);
    Mask m(ids.h(), ids.w());
    for (size_t i = 0; i < ids.flat().size(); ++i)
        m.flat()[i] = lut[ids.flat()[i]];
    // inner edge 2px thick: selected pixels within 2px of a non-selected pixel
    const Mask inner = _shrink(m, 2);
    const Mask edge = and_not(m, inner);
    RgbaImage rgba(m.h(), m.w(), Rgba{0, 0, 0, 0});
    for (long long y = 0; y < m.h(); ++y)
        for (long long x = 0; x < m.w(); ++x) {
            const long long xs = x0 + x, ys = y0 + y;
            if (inner(y, x) && (py::mod(xs + ys, 8LL) == 0 || py::mod(xs - ys, 8LL) == 0))
                rgba(y, x) = {235, 45, 45, 150};
            if (edge(y, x))
                rgba(y, x) = {255, 40, 40, 255};
        }
    sel_item->setPixmap(QPixmap::fromImage(rgba_image(rgba)));
    sel_item->setPos(double(x0), double(y0));
    sel_item->setVisible(true);
    if (!_pulse->isActive() && !_calm->isActive())
        _pulse->start();
}

// the mask grown by n pixels (4-neighbourhood steps)
grid::Grid<std::uint8_t> MapView::_grow(const grid::Grid<std::uint8_t> &m, int n)
{
    Mask g = m;
    const long long h = m.h(), w = m.w();
    for (int k = 0; k < n; ++k) {
        Mask e = g;
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x)
                if (!e(y, x) && ((y > 0 && g(y - 1, x)) || (y < h - 1 && g(y + 1, x)) || (x > 0 && g(y, x - 1)) ||
                                 (x < w - 1 && g(y, x + 1))))
                    e(y, x) = 1;
        g = std::move(e);
    }
    return g;
}

grid::Grid<std::uint8_t> MapView::_shrink(const grid::Grid<std::uint8_t> &m, int n)
{
    Mask g = m;
    const long long h = m.h(), w = m.w();
    for (int k = 0; k < n; ++k) {
        Mask e = g;
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x)
                if (e(y, x) && ((y > 0 && !g(y - 1, x)) || (y < h - 1 && !g(y + 1, x)) || (x > 0 && !g(y, x - 1)) ||
                                (x < w - 1 && !g(y, x + 1))))
                    e(y, x) = 0;
        g = std::move(e);
    }
    return g;
}

// markings over the map colours — each kind of thing in a look of its own, so they can't be confused:
// "outline" a nation (thick bright outline, dark rim outside) · "dashed" a state / region · "provinces" provinces
// (bright cyan rim inside, light hatch) · "provset" every province an event names (dense cyan hatching, thin rim) ·
// "dots" {pid: share} a culture (green rim, diagonal hatching, denser = more people) · "lines" a religion (yellow,
// level hatching). Drawn at a resolution that follows the zoom, so lines and dots keep their size on screen.
void MapView::set_focus_layer(const std::vector<FocusLayer> &layers0)
{
    std::vector<FocusLayer> layers;
    for (const FocusLayer &l : layers0)
        if (!l.pids.isEmpty())
            layers.push_back(l);
    std::vector<std::tuple<QString, std::optional<Rgb>, std::vector<std::pair<long long, double>>>> key;
    for (const FocusLayer &l : layers) {
        std::vector<std::pair<long long, double>> pv;
        for (const auto &[p, v] : l.pids)
            pv.emplace_back(p, py::round_to(v, 2));
        std::sort(pv.begin(), pv.end());
        key.emplace_back(l.style, l.rgb, std::move(pv));
    }
    if (_focus_key && key == *_focus_key)
        return;
    _focus_key = std::move(key);
    std::vector<FocusLayer> pat, rest;
    for (const FocusLayer &l : layers)
        (l.style == u"dots"_s || l.style == u"lines"_s ? pat : rest).push_back(l);
    _focus_layers = std::move(rest);
    _focus_k.reset();
    _pattern_layers = pat;
    _render_patterns(pat);
    _render_focus();
}

namespace {

std::vector<long long> marked(const std::vector<FocusLayer> &layers, const MapData &d)   // sorted({p for l … })
{
    std::set<long long> all;
    for (const FocusLayer &l : layers)
        for (const auto &[p, v] : l.pids)
            if (0 < p && p < d.n && d.area[size_t(p)] > 0)
                all.insert(p);
    return {all.begin(), all.end()};
}

}  // namespace

// a culture / religion: a rim just inside each province's border and hatching (denser = more of the people) — at the
// map's own pixels, so it zooms with the map (no change of pattern with the zoom)
void MapView::_render_patterns(const std::vector<FocusLayer> &layers)
{
    const MapData *d = data;
    if (!d || layers.empty()) {
        pattern_item->setVisible(false);
        return;
    }
    const auto box = d->bbox(marked(layers, *d));
    if (!box) {
        pattern_item->setVisible(false);
        return;
    }
    const long long x0 = std::max(0LL, (*box)[0] - 2), y0 = std::max(0LL, (*box)[1] - 2);
    const long long x1 = std::min(d->w, (*box)[2] + 2), y1 = std::min(d->h, (*box)[3] + 2);
    const auto ids = id_window(d->idmap, y0, y1, x0, x1);
    const long long h = ids.h(), w = ids.w();
    RgbaImage rgba(h, w, Rgba{0, 0, 0, 0});
    for (const FocusLayer &l : layers) {
        const auto val = values(l, *d, ids);
        bool any = false;
        const Mask m = positive(val, &any);
        if (!any)
            continue;
        const Rgb rgb = l.style == u"dots"_s ? Rgb{120, 235, 95} : Rgb{255, 226, 80};
        const Mask same = same_as_neighbours(m, ids);  // pixels whose neighbours are the same province
        const Mask rim = and_not(m, same);
        const bool dots = l.style == u"dots"_s;
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x) {
                if (!same(y, x))
                    continue;
                const float v = val(y, x);
                const long long gap = v >= 0.6f ? 3 : (v >= 0.25f ? 5 : 8);
                const long long xs = x0 + x, ys = y0 + y;
                if (dots ? py::mod(xs - ys, gap) == 0 : py::mod(ys, gap) == 0)
                    rgba(y, x) = with_alpha(rgb, 210);
            }
        put(rgba, rim, with_alpha(rgb, 255));
    }
    pattern_item->setPixmap(QPixmap::fromImage(rgba_image(rgba)));
    pattern_item->setPos(double(x0), double(y0));
    pattern_item->setVisible(true);
}

int MapView::_focus_step() const
{
    const double s = ms_or_one(transform().m11());
    return s < 1.0 ? std::max(1, std::min(8, int(1.0 / s + 0.5))) : 1;
}

// (x0, y0, x1, y1) of what the focus layer marks, or nothing
std::optional<std::array<long long, 4>> MapView::focus_box() const
{
    const MapData *d = data;
    std::vector<FocusLayer> layers = _focus_layers;
    layers.insert(layers.end(), _pattern_layers.begin(), _pattern_layers.end());
    if (!d || layers.empty())
        return std::nullopt;
    return d->bbox(marked(layers, *d));
}

void MapView::_render_focus()
{
    const MapData *d = data;
    const auto &layers = _focus_layers;
    if (!d || layers.empty()) {
        focus_item->setVisible(false);
        _focus_k = _focus_step();
        return;
    }
    const int k = _focus_step();
    _focus_k = k;
    const auto box = focus_box();
    if (!box) {
        focus_item->setVisible(false);
        return;
    }
    const long long m7 = 7 * k;
    long long x0 = std::max(0LL, (*box)[0] - m7), y0 = std::max(0LL, (*box)[1] - m7);
    const long long x1 = std::min(d->w, (*box)[2] + m7), y1 = std::min(d->h, (*box)[3] + m7);
    x0 -= x0 % k;
    y0 -= y0 % k;
    const auto ids = id_window(d->idmap, y0, y1, x0, x1, k);
    const long long h = ids.h(), w = ids.w();
    // pattern coordinates in layer pixels (stable while panning)
    const long long ys0 = y0 / k, xs0 = x0 / k;
    RgbaImage rgba(h, w, Rgba{0, 0, 0, 0});
    const std::map<QString, int> order{{u"lines"_s, 0}, {u"dots"_s, 1},   {u"provset"_s, 2},
                                       {u"provinces"_s, 3}, {u"dashed"_s, 4}, {u"outline"_s, 5}};
    auto order_of = [&](const FocusLayer &l) {
        auto it = order.find(l.style);
        return it == order.end() ? 9 : it->second;
    };
    std::vector<const FocusLayer *> sorted;
    for (const FocusLayer &l : layers)
        sorted.push_back(&l);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const FocusLayer *a, const FocusLayer *b) { return order_of(*a) < order_of(*b); });
    for (const FocusLayer *lp : sorted) {
        const FocusLayer &l = *lp;
        const auto val = values(l, *d, ids);
        bool any = false;
        const Mask m = positive(val, &any);
        if (!any)
            continue;
        const QString &style = l.style;
        if (style == u"outline"_s) {
            const Mask g3 = _grow(m, 3);
            const Mask g5 = _grow(g3, 2);
            put(rgba, and_not(g5, g3), {12, 12, 14, 235});
            put(rgba, and_not(g3, m), with_alpha(l.rgb.value_or(Rgb{255, 70, 190}), 255));
            put(rgba, and_not(m, _shrink(m, 1)), {12, 12, 14, 200});   // a dark rim inside too: it stands out
        } else if (style == u"dashed"_s) {
            const Mask ring = and_not(_grow(m, 2), m);
            for (long long y = 0; y < h; ++y)
                for (long long x = 0; x < w; ++x)
                    if (ring(y, x))
                        rgba(y, x) = py::mod(py::floordiv(xs0 + x + ys0 + y, 5LL), 2LL) == 0 ? Rgba{255, 255, 255, 255}
                                                                                             : Rgba{15, 15, 18, 255};
        } else if (style == u"provset"_s) {
            const Mask inner = _shrink(m, 1);
            for (long long y = 0; y < h; ++y)
                for (long long x = 0; x < w; ++x)
                    if (inner(y, x) && py::mod(xs0 + x + ys0 + y, 4LL) == 0)
                        rgba(y, x) = {90, 225, 255, 190};
            put(rgba, and_not(m, inner), {90, 225, 255, 220});
        } else if (style == u"provinces"_s) {
            const Rgb c = l.rgb.value_or(Rgb{90, 225, 255});
            const Mask inner = _shrink(m, 2);
            for (long long y = 0; y < h; ++y)
                for (long long x = 0; x < w; ++x)
                    if (inner(y, x) && py::mod((xs0 + x) - (ys0 + y), 6LL) == 0)
                        rgba(y, x) = with_alpha(c, 150);
            put(rgba, and_not(m, inner), with_alpha(c, 255));
            put(rgba, and_not(_grow(m, 1), m), {12, 12, 14, 235});
        } else if (style == u"dots"_s || style == u"lines"_s) {
            // a culture ("dots": green, diagonal hatching) / a religion ("lines": yellow, level hatching): a rim just
            // inside each province's own border, hatching the denser the more of its people
            const Rgb rgb = style == u"dots"_s ? Rgb{120, 235, 95} : Rgb{255, 226, 80};
            grid::Grid<std::uint16_t> ids_m(h, w);
            for (size_t i = 0; i < ids.flat().size(); ++i)
                ids_m.flat()[i] = m.flat()[i] ? ids.flat()[i] : 0;
            const Mask inner = same_as_neighbours(m, ids_m);   // pixels whose 4 neighbours are in the same province
            const Mask rim1 = and_not(m, inner);
            const Mask inner2 = _shrink(inner, 1);
            const Mask rim2 = and_not(inner, inner2);
            for (long long y = 0; y < h; ++y)
                for (long long x = 0; x < w; ++x) {
                    if (!inner2(y, x))
                        continue;
                    const float v = val(y, x);
                    const long long gap = v >= 0.6f ? 4 : (v >= 0.25f ? 6 : 9);
                    const long long xs = xs0 + x, ys = ys0 + y;
                    if (style == u"dots"_s ? py::mod(xs - ys, gap) == 0 : py::mod(ys, gap) == 0)
                        rgba(y, x) = with_alpha(rgb, 215);
                }
            put(rgba, rim2, {10, 10, 12, 230});
            put(rgba, rim1, with_alpha(rgb, 255));
        }
    }
    focus_item->setPixmap(QPixmap::fromImage(rgba_image(rgba)));
    focus_item->setScale(k);
    focus_item->setPos(double(x0), double(y0));
    focus_item->setVisible(true);
}

// pan (not zoom) so the box is in view, if none of it is
void MapView::show_box(const std::optional<std::array<long long, 4>> &box)
{
    if (!box)
        return;
    const auto [x0, y0, x1, y1] = *box;
    const QRectF vr = mapToScene(viewport()->rect()).boundingRect();
    if (vr.intersects(QRectF(double(x0), double(y0), double(x1 - x0), double(y1 - y0))))
        return;
    centerOn(QPointF((x0 + x1) / 2.0, (y0 + y1) / 2.0));
    _zoomed();
}

// called on every pan / zoom step: pause the pulse until the camera has been still for a moment
void MapView::camera_moved()
{
    if (_pulse->isActive())
        _pulse->stop();
    _calm->start();
}

void MapView::_resume_pulse()
{
    if (sel_item->isVisible() && !sel_ids.empty() && !_pulse->isActive())
        _pulse->start();
}

void MapView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    if (dx || dy)
        camera_moved();
}

void MapView::_tick()
{
    _phase = py::mod(_phase + 0.04 / 1.6, 1.0);        // 1.6 s per pulse
    sel_item->setOpacity(0.70 + 0.30 * (0.5 + 0.5 * std::cos(_phase * 2 * PI)));
}

// codes: {pid: code}; 1 selected, 2 split target, 3 split source, 4 hover-ish (CTX, CTX_FOREIGN + k)
void MapView::set_highlight(const OrderedMap<long long, long long> &codes)
{
    if (!has_base)
        return;
    std::vector<std::uint8_t> new_hl(hl.size(), 0);
    for (const auto &[pid, c] : codes)
        if (0 < pid && pid < data->n)
            new_hl[size_t(pid)] = std::uint8_t(c);
    if (new_hl == hl)
        return;
    std::vector<long long> changed;
    for (size_t i = 0; i < hl.size(); ++i)
        if (new_hl[i] != hl[i])
            changed.push_back((long long)i);
    hl = std::move(new_hl);
    const auto box = data->bbox(changed);
    if (!box)
        return;
    // the outline of unchanged neighbours may need redrawing too: pad by one pixel
    const std::array<long long, 4> b{std::max(0LL, (*box)[0] - 1), std::max(0LL, (*box)[1] - 1),
                                     std::min(data->w, (*box)[2] + 1), std::min(data->h, (*box)[3] + 1)};
    _overlay(b);
    _push_rect(b);
}

void MapView::_overlay(const std::array<long long, 4> &box)
{
    // One pass per pixel (2026-11-10, was several passes over the box + a std::set of every pixel's code): each pixel's
    // steps depend on its own code only and keep the order they had — hatch (state context), tint (HL_COLORS), outline
    // colour, outline blend — so the picture is the same to the last bit.
    const auto [x0, y0, x1, y1] = box;
    const MapData &d = *data;
    const long long W = d.w, h = y1 - y0, w = x1 - x0;
    std::vector<std::uint8_t> code(size_t(h * w));
    bool any = false;
    for (long long y = 0; y < h; ++y) {
        const std::uint16_t *ids = d.idmap.row(y0 + y) + x0;
        std::uint8_t *cr = code.data() + y * w;
        for (long long x = 0; x < w; ++x) {
            cr[x] = hl[ids[x]];
            any = any || cr[x];
        }
    }
    if (!any) {
        for (long long y = 0; y < h; ++y) {
            const std::uint32_t *src = _base32.data() + (y0 + y) * W + x0;
            std::uint32_t *dst = _img32.data() + (y0 + y) * W + x0;
            for (long long x = 0; x < w; ++x)
                dst[x] = 0xFF000000u | (src[x] & 0xFFFFFFu);
        }
        return;
    }
    using Px = std::array<std::uint8_t, 3>;
    auto blend = [](Px &px, const Rgb &c, float a, float b) {   // float32, as numpy
        px[0] = std::uint8_t(float(px[0]) * a + float(std::get<0>(c)) * b);
        px[1] = std::uint8_t(float(px[1]) * a + float(std::get<1>(c)) * b);
        px[2] = std::uint8_t(float(px[2]) * a + float(std::get<2>(c)) * b);
    };
    // what each code does (a code is 0 … 255)
    struct Step {
        std::uint8_t hatch = 0;                  // 1 = CTX lines, 2 = CTX_FOREIGN mesh
        bool tint = false, ctx = false;
        Rgb hatch_rgb{}, tint_rgb{};
        float ta = 0, tb = 0;
    };
    std::array<Step, 256> steps{};
    for (int c = 1; c < 256; ++c) {
        Step &s = steps[size_t(c)];
        if (c == CTX) {
            s.hatch = 1;
            s.hatch_rgb = CTX_RGB;
            s.ctx = true;
        } else if (c >= CTX_FOREIGN) {
            s.hatch = 2;
            s.hatch_rgb = MESH_COLORS[size_t((c - CTX_FOREIGN) % int(MESH_COLORS.size()))];
            s.ctx = true;
        }
        if (const auto it = HL_COLORS.find(c); it != HL_COLORS.end()) {
            const double tt = HL_TINT.at(c);
            s.tint = true;
            s.tint_rgb = it->second;
            s.ta = float(1 - tt);
            s.tb = float(tt);
        }
    }
    for (long long y = 0; y < h; ++y) {
        const std::uint8_t *cr = code.data() + y * w;
        const std::uint8_t *up = y > 0 ? cr - w : nullptr;
        const std::uint8_t *dn = y + 1 < h ? cr + w : nullptr;
        const std::uint32_t *src = _base32.data() + (y0 + y) * W + x0;
        std::uint32_t *dst = _img32.data() + (y0 + y) * W + x0;
        const long long ys = y0 + y;
        for (long long x = 0; x < w; ++x) {
            const std::uint8_t c = cr[x];
            if (!c) {
                dst[x] = 0xFF000000u | (src[x] & 0xFFFFFFu);
                continue;
            }
            Px px = unpack(src[x]);
            const Step &s = steps[c];
            const long long xs = x0 + x;
            if (s.hatch == 1) {
                if ((xs + ys) % 7 == 0)
                    blend(px, s.hatch_rgb, 0.55f, 0.45f);
            } else if (s.hatch == 2) {
                if ((xs + ys) % 7 == 0 || (xs - ys) % 7 == 0)
                    blend(px, s.hatch_rgb, 0.2f, 0.8f);
            }
            if (s.tint)
                blend(px, s.tint_rgb, s.ta, s.tb);
            // the outline: a highlighted pixel next to (inside the box) one of another code
            const bool edge = (x > 0 && cr[x - 1] != c) || (x + 1 < w && cr[x + 1] != c) || (up && up[x] != c) ||
                              (dn && dn[x] != c);
            if (edge) {
                if (s.tint)
                    px = {std::uint8_t(std::get<0>(s.tint_rgb)), std::uint8_t(std::get<1>(s.tint_rgb)),
                          std::uint8_t(std::get<2>(s.tint_rgb))};
                if (s.ctx)
                    blend(px, s.hatch_rgb, 0.5f, 0.5f);
            }
            dst[x] = pack(px[0], px[1], px[2]);
        }
    }
}
void MapView::_push_full()
{
    const QImage qi(reinterpret_cast<const uchar *>(_img32.data()), int(data->w), int(data->h), int(data->w * 4),
                    QImage::Format_RGB32);
    item->set(QPixmap::fromImage(qi));
}

void MapView::_push_rect(const std::array<long long, 4> &box)
{
    const auto [x0, y0, x1, y1] = box;
    const QImage qi(reinterpret_cast<const uchar *>(_img32.data() + (y0 * data->w + x0)), int(x1 - x0), int(y1 - y0),
                    int(data->w * 4), QImage::Format_RGB32);
    item->patch(qi, int(x0), int(y0));
}

// ---------------------------------------------------------------------------------------------- BaseItem
void BaseItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);   // (QGraphicsPixmapItem, Qt::FastTransformation)
    p->drawPixmap(QPointF(0, 0), pm);
}

void BaseItem::set(QPixmap p)
{
    if (p.size() != pm.size())
        prepareGeometryChange();
    pm = std::move(p);
    update();
}

void BaseItem::patch(const QImage &part, int x, int y)
{
    if (pm.isNull() || part.isNull())
        return;
    {
        QPainter p(&pm);                             // (the only copy: painted in place, nothing copied)
        p.drawImage(x, y, part);
    }
    update(QRectF(x, y, part.width(), part.height()));
}

// ------------------------------------------------------------------ the nation's capital (crown)
void MapView::set_nation_capital(std::optional<long long> pid)
{
    const MapData *d = data;
    if (!pid || !d || !(0 < *pid && *pid < d->n && d->area[size_t(*pid)] > 0)) {
        if (crown)
            crown->hide();
        return;
    }
    if (!crown) {
        crown = new CrownItem();
        scene()->addItem(crown);
    }
    crown->setPos(d->cx[size_t(*pid)] + 0.5, d->cy[size_t(*pid)] + 0.5);
    _scale_crown();
    crown->show();
}

// full size from zoom 1.5 up, smaller when zoomed out (down to 45 %) so it doesn't cover the map
void MapView::_scale_crown()
{
    if (crown)
        crown->setScale(std::min(1.0, std::max(0.45, std::pow(transform().m11() / 1.5, 0.6))));
}

// ------------------------------------------------------------------ capital stars
void MapView::set_capitals(const std::vector<long long> &pids)
{
    for (StarItem *s : stars) {
        scene()->removeItem(s);
        delete s;
    }
    stars.clear();
    const MapData *d = data;
    for (long long pid : pids)
        if (pid && 0 < pid && pid < d->n && d->area[size_t(pid)] > 0) {
            auto *s = new StarItem();
            s->capital = pid;
            s->setPos(d->cx[size_t(pid)] + 0.5, d->cy[size_t(pid)] + 0.5);
            scene()->addItem(s);
            stars.push_back(s);
        }
    _update_star_visibility();
}

void MapView::set_show_stars(bool on)
{
    show_stars = on;
    _update_star_visibility();
}

// capitals whose star is always shown (e.g. the states of the selection in province view)
void MapView::set_focus_capitals(const std::vector<long long> &pids)
{
    focus_capitals = QSet<long long>(pids.begin(), pids.end());
    _update_star_visibility();
}

void MapView::_update_star_visibility()
{
    const bool vis = show_stars && transform().m11() >= 0.45;
    for (StarItem *s : stars)
        s->setVisible(vis || focus_capitals.contains(s->capital));
}

// ------------------------------------------------------------------ navigation
void MapView::fit()
{
    if (data) {
        fitInView(QRectF(0, 0, double(data->w), double(data->h)), Qt::KeepAspectRatio);
        _zoomed();
    }
}

// zoom by factor f, keeping the map point under `pos` (view coords) fixed
void MapView::zoom_by(double f, bool anchor_mouse, std::optional<QPoint> pos)
{
    const double cur = transform().m11();
    f = std::max(0.05 / cur, std::min(24.0 / cur, f));
    if (!pos) {
        pos = anchor_mouse ? viewport()->mapFromGlobal(QCursor::pos()) : viewport()->rect().center();
        if (!viewport()->rect().contains(*pos))
            pos = viewport()->rect().center();
    }
    const QPointF before = mapToScene(*pos);
    camera_moved();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    scale(f, f);
    const QPointF after = mapToScene(*pos);
    const double s = transform().m11();
    QScrollBar *hb = horizontalScrollBar(), *vb = verticalScrollBar();
    hb->setValue(int(py::round_int(hb->value() + (before.x() - after.x()) * s)));
    vb->setValue(int(py::round_int(vb->value() + (before.y() - after.y()) * s)));
    _zoomed();
}

void MapView::_zoomed()
{
    _update_star_visibility();
    _scale_crown();
    if (!_focus_layers.empty() && _focus_step() != _focus_k)
        _focus_timer->start();
    Q_EMIT zoomChanged(transform().m11());
}

void MapView::center_on_province(long long pid, std::optional<double> zoom_to)
{
    const MapData *d = data;
    if (!d || !(0 < pid && pid < d->n) || d->area[size_t(pid)] == 0)
        return;
    if (zoom_to && *zoom_to != 0.0) {
        const double cur = transform().m11();
        if (cur < *zoom_to)
            zoom_by(*zoom_to / cur, false);
    }
    centerOn(QPointF(d->cx[size_t(pid)], d->cy[size_t(pid)]));
}

void MapView::set_pixel_grid(bool on)
{
    pixel_grid = on;
    viewport()->update();
}

void MapView::drawForeground(QPainter *p, const QRectF &rect)
{
    QGraphicsView::drawForeground(p, rect);
    if (!pixel_grid || !data || transform().m11() < GRID_FROM)
        return;
    // the lines lie on whole map coordinates: the borders of the pixels (1 scene unit = 1 map pixel)
    const double x0 = std::max(0.0, std::floor(rect.left())), x1 = std::min(double(data->w), std::ceil(rect.right()));
    const double y0 = std::max(0.0, std::floor(rect.top())), y1 = std::min(double(data->h), std::ceil(rect.bottom()));
    if (x1 <= x0 || y1 <= y0)
        return;
    QVector<QLineF> lines;
    lines.reserve(qsizetype(x1 - x0 + y1 - y0 + 2));
    for (double x = x0; x <= x1; x += 1.0)
        lines << QLineF(x, y0, x, y1);
    for (double y = y0; y <= y1; y += 1.0)
        lines << QLineF(x0, y, x1, y);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);  // (hard lines)
    QPen pen(QColor(0, 0, 0, 120));
    pen.setCosmetic(true);
    pen.setWidth(1);
    p->setPen(pen);
    p->drawLines(lines);
    p->restore();
}

void MapView::wheelEvent(QWheelEvent *ev)
{
    double steps = ev->angleDelta().y() / 120.0;
    if (steps == 0.0 && (ev->modifiers() & Qt::ShiftModifier))   // (C++: some mice turn Shift + wheel sideways)
        steps = ev->angleDelta().x() / 120.0;
    MouseHandler *h = mouse_handler;
    if (steps != 0.0 && h && h->has_wheel() && h->wheel(steps, ev->modifiers()))
        return;
    if (steps != 0.0)
        zoom_by(std::pow(1.25, steps), true, ev->position().toPoint());
}

double MapView::_tol() const { return 7.0 / ms_or_one(transform().m11()); }

void MapView::mousePressEvent(QMouseEvent *ev)
{
    _press = ev->position();
    _last = ev->position();
    _handler_active = false;
    if (mouse_handler && ev->button() == Qt::LeftButton && data) {
        const QPointF sp = mapToScene(ev->position().toPoint());
        if (mouse_handler->press(sp.x(), sp.y(), _tol(), ev->modifiers())) {
            _handler_active = true;
            return;
        }
    }
    // middle button pans at once; right button pans once dragged (a right click is a click)
    _panning = ev->button() == Qt::MiddleButton;
    if (_panning)
        viewport()->setCursor(Qt::ClosedHandCursor);
}

void MapView::mouseMoveEvent(QMouseEvent *ev)
{
    const QPointF pos = ev->position();
    if (_handler_active || (mouse_handler && !_press && data)) {
        const QPointF sp = mapToScene(pos.toPoint());
        mouse_handler->move(sp.x(), sp.y());
        if (_handler_active) {
            _last = pos;
            const int x = int(sp.x()), y = int(sp.y());
            Q_EMIT provinceHovered(int(data->province_at(x, y)), x, y);
            return;
        }
    }
    if (_press && (ev->buttons() & (Qt::LeftButton | Qt::MiddleButton | Qt::RightButton))) {
        if (!_panning && (pos - *_press).manhattanLength() > 5) {
            _panning = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (_panning) {
            const QPointF delta = pos - *_last;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(delta.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() - int(delta.y()));
        }
    }
    _last = pos;
    if (data) {
        const QPointF sp = mapToScene(pos.toPoint());
        const int x = int(sp.x()), y = int(sp.y());
        Q_EMIT provinceHovered(int(data->province_at(x, y)), x, y);
    }
}

void MapView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (_handler_active) {
        _handler_active = false;
        _press.reset();
        const QPointF sp = mapToScene(ev->position().toPoint());
        mouse_handler->release(sp.x(), sp.y(), _tol(), ev->modifiers());
        return;
    }
    const bool was_pan = _panning;
    _panning = false;
    viewport()->setCursor(Qt::ArrowCursor);
    if (_press && !was_pan && ev->button() == Qt::RightButton && data && mouse_handler) {
        const QPointF sp = mapToScene(ev->position().toPoint());
        const bool took = mouse_handler->rclick(sp.x(), sp.y(), _tol(), ev->modifiers());
        if (took || !mouse_handler->rclick_passes()) {
            _press.reset();                            // (a tool that is always on — Canals — leaves other right-clicks
            return;                                    //  to the map: its menu)
        }
    }
    if (_press && !was_pan && ev->button() == Qt::RightButton && data) {
        const QPointF sp = mapToScene(ev->position().toPoint());
        Q_EMIT provinceRightClicked(int(data->province_at(sp.x(), sp.y())), ev->modifiers());
        _press.reset();
        return;
    }
    if (_press && !was_pan && ev->button() == Qt::LeftButton && data) {
        const QPointF sp = mapToScene(ev->position().toPoint());
        const double tol = _tol();
        if (click_hook && click_hook(sp.x(), sp.y(), tol, ev->modifiers())) {
            _press.reset();
            return;
        }
        Q_EMIT provinceClicked(int(data->province_at(sp.x(), sp.y())), ev->modifiers());
    }
    _press.reset();
}

// a line of text shown at the top of the map (e.g. the problem selected in the map check)
void MapView::set_banner(const QString &text, std::optional<Rgb> rgb)
{
    QLabel *lab = _banner;
    if (!lab) {
        lab = _banner = new QLabel(this);
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        lab->setWordWrap(true);
    }
    if (text.isEmpty()) {
        lab->hide();
        return;
    }
    const auto [r, g, b] = rgb.value_or(Rgb{230, 225, 210});
    lab->setStyleSheet(u"QLabel { background: rgba(12, 14, 18, 225); color: rgb(%1, %2, %3); "
                       "border: 1px solid rgb(%1, %2, %3); border-radius: 6px; padding: 7px 16px; "
                       "font-size: 15px; font-weight: 600; }"_s.arg(r).arg(g).arg(b));
    lab->setText(text);
    _place_banner();
    lab->show();
    lab->raise();
}

// a gold label at the top of the map while a mode is on (drawing) — hard to miss; a click on it calls `clicked`
// (e.g. stop drawing). Banners move below it.
void MapView::set_mode_pill(const QString &text, std::function<void()> clicked)
{
    QPushButton *pill = _pill;
    if (!pill) {
        pill = _pill = new QPushButton(this);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setStyleSheet(
            u"QPushButton { background: #c9a15a; color: #15130f; border: 2px solid #f0d8a0; border-radius: 16px; "
            "padding: 7px 24px; font-size: 15px; font-weight: 700; } QPushButton:hover { background: #dcb46a; }"_s);
        connect(pill, &QPushButton::clicked, this, [this] {
            if (_pill_cb)
                _pill_cb();
        });
        pill->hide();
    }
    _pill_cb = std::move(clicked);
    if (!text.isEmpty()) {
        pill->setText(text);
        pill->show();
        pill->raise();
    } else {
        pill->hide();
    }
    _place_banner();
}

void MapView::_place_banner()
{
    int top = 12;
    QPushButton *pill = _pill;
    if (pill && !pill->isHidden()) {
        pill->adjustSize();
        pill->move(std::max(10, int(py::floordiv(width() - pill->width(), 2LL))), 12);
        top = 12 + pill->height() + 8;
    }
    QLabel *lab = _banner;
    if (!lab)
        return;
    const int maxw = std::max(200, width() - 60);
    lab->setWordWrap(false);                           // one line when it fits, wrapped only when it doesn't
    lab->setMinimumWidth(0);
    lab->setMaximumWidth(16777215);
    lab->adjustSize();
    if (lab->width() > maxw) {
        lab->setWordWrap(true);
        lab->setFixedWidth(maxw);
        lab->adjustSize();
    }
    lab->move(std::max(10, int(py::floordiv(width() - lab->width(), 2LL))), top);
}

// a legend in the map's lower left corner: title, then rows — a coloured square a row — or rows with a key: they
// switch their kind on / off when clicked (legendToggled(key)). No rows: hidden.
void MapView::set_legend(const QString &title, const std::vector<LegendRow> &rows)
{
    QLabel *lab = _legend;
    if (!lab) {
        lab = _legend = new QLabel(this);
        lab->setTextFormat(Qt::RichText);
        lab->setStyleSheet(u"QLabel { background: rgba(12, 14, 18, 230); color: #e7e3d8; border: 1px solid #8a7346; "
                           "border-radius: 6px; padding: 8px 12px; font-size: 13px; }"_s);
        connect(lab, &QLabel::linkActivated, this, &MapView::legendToggled);
    }
    if (rows.empty()) {
        lab->hide();
        return;
    }
    const bool toggles = std::any_of(rows.begin(), rows.end(), [](const LegendRow &r) { return r.key.has_value(); });
    lab->setAttribute(Qt::WA_TransparentForMouseEvents, !toggles);   // (only a legend with switches takes clicks)
    QString html = u"<div style='color:#c9a15a; font-weight:700; font-size:14px; margin-bottom:4px'>%1</div>"_s.arg(
        refs::esc(title));
    for (const LegendRow &r : rows) {
        const auto [cr, cg, cb] = r.rgb;
        const QString sq = u"<span style='color:rgb(%1,%2,%3); font-size:17px'>■</span>"_s.arg(cr).arg(cg).arg(cb);
        if (r.key && !r.key->isEmpty()) {
            const bool on = r.on;
            const QString col = on ? u"#e7e3d8"_s : u"#7d8591"_s;
            html += u"<div><a href='"_s + refs::esc(*r.key) + u"' style='color:"_s + col +
                    u"; text-decoration:none'>"_s + (on ? u"☑"_s : u"☐"_s) + u"</a>&nbsp;"_s +
                    (on ? sq : u"<span style='color:#555; font-size:17px'>■</span>"_s) + u"&nbsp;"_s +
                    u"<a href='"_s + refs::esc(*r.key) + u"' style='color:"_s + col + u"; text-decoration:none'>"_s +
                    refs::esc(r.text) + u"</a></div>"_s;
        } else {
            html += u"<div>"_s + sq + u"&nbsp;"_s + refs::esc(r.text) + u"</div>"_s;
        }
    }
    lab->setText(html);
    lab->adjustSize();
    _place_legend();
    lab->show();
    lab->raise();
}

void MapView::_place_legend()
{
    if (QLabel *lab = _legend) {
        lab->setMaximumHeight(std::max(120, height() - 80));
        lab->move(12, std::max(10, height() - lab->height() - 12));
    }
}

void MapView::resizeEvent(QResizeEvent *ev)
{
    QGraphicsView::resizeEvent(ev);
    _place_banner();
    _place_legend();
}

void MapView::leaveEvent(QEvent *)
{
    Q_EMIT provinceHovered(0, -1, -1);
}

}  // namespace mapview
