// flagstudio.py — the flag studio: shapes on a canvas, one working state per flag, the nation's flags below.
#include "ui/flagstudio.h"
#include "core/fs.h"
#include "core/pyjson.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/imgio.h"
#include "map/nationextras_data.h"
#include "ui/colorwheel.h"
#include "ui/dialogs.h"
#include "ui/nationextras.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QAbstractItemView>
#include <QBuffer>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace flagstudio {

namespace {

constexpr double PI = 3.141592653589793;
double radians(double x) { return x * (PI / 180.0); }   // (CPython: x * degToRad)
double degrees(double x) { return x * (180.0 / PI); }   // (CPython: x * radToDeg)

const std::map<QString, std::pair<double, QString>> THICK = {
    {u"cross"_s, {1 / 3.0, u"Thickness of its arms"_s}},   {u"ring"_s, {0.32, u"Thickness of the ring"_s}},
    {u"frame"_s, {0.25, u"Thickness of the frame"_s}},     {u"crescent"_s, {0.26, u"Thickness of the crescent"_s}},
    {u"star"_s, {0.4, u"Size of its middle"_s}}};

bool thick_kind(const QString &k) { return THICK.count(k) > 0; }

QString rstrip_chars(QString s, QChar c)                  // str.rstrip(c)
{
    while (!s.isEmpty() && s.back() == c)
        s.chop(1);
    return s;
}

QString fmt2(double v) { return rstrip_chars(rstrip_chars(py::fmt_f(v, 2), u'0'), u'.'); }   // ("%.2f" % v).rstrip…

QString round2(double v)                                  // round(v, 2) as a key (== between floats)
{
    const double r = py::round_to(v, 2);
    return py::repr_float(r == 0.0 ? 0.0 : r);
}

quint32 crc32(const QByteArray &b)                        // zlib.crc32
{
    static quint32 table[256];
    static bool made = false;
    if (!made) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (char ch : b)
        c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

QByteArray rgb_bytes(const QImage &src)                   // the RGB888 rows without their padding
{
    const QImage img = src.convertToFormat(QImage::Format_RGB888);
    QByteArray out;
    out.reserve(img.width() * 3 * img.height());
    for (int y = 0; y < img.height(); ++y)
        out.append(reinterpret_cast<const char *>(img.constScanLine(y)), img.width() * 3);
    return out;
}

pyjson::Value num_value(const Num &n) { return n.is_int ? pyjson::Value(static_cast<long long>(n.v)) : pyjson::Value(n.v); }

Num num_of(const pyjson::Value &v)
{
    if (std::holds_alternative<long long>(v.v))
        return {double(std::get<long long>(v.v)), true};
    if (std::holds_alternative<bool>(v.v))
        return {std::get<bool>(v.v) ? 1.0 : 0.0, true};
    if (std::holds_alternative<double>(v.v))
        return {std::get<double>(v.v), false};
    throw std::runtime_error("not a number");
}

Num max1(const Num &n) { return n.v > 1.0 ? n : Num{1.0, false}; }   // max(1.0, n)

}  // namespace

const double DIAG = degrees(std::atan2(double(H), double(W)));
const std::vector<double> SNAP_ANGLES = [] {
    std::set<double> s;
    for (int a = -180; a <= 180; a += 15)
        s.insert(double(a));
    for (double a : {DIAG, -DIAG, 180 - DIAG, DIAG - 180})
        s.insert(a);
    return std::vector<double>(s.begin(), s.end());
}();
const std::vector<std::pair<QString, QString>> KINDS = {
    {u"rect"_s, u"▭ Rectangle"_s}, {u"ellipse"_s, u"● Disc"_s},       {u"triangle"_s, u"▲ Triangle"_s},
    {u"star"_s, u"★ Star"_s},      {u"cross"_s, u"✚ Cross"_s},        {u"diamond"_s, u"◆ Diamond"_s},
    {u"crescent"_s, u"☾ Crescent"_s}, {u"ring"_s, u"○ Ring"_s},       {u"frame"_s, u"▢ Frame"_s}};
const std::vector<std::pair<QString, QString>> PRESETS = {
    {u"plain"_s, u"One colour"_s},          {u"h2"_s, u"Two stripes"_s},
    {u"h3"_s, u"Three stripes"_s},          {u"v2"_s, u"Two bars"_s},
    {u"v3"_s, u"Three bars"_s},             {u"nordic"_s, u"Cross towards the pole"_s},
    {u"cross"_s, u"Cross in the middle"_s}, {u"saltire"_s, u"Diagonal cross"_s},
    {u"quarters"_s, u"Quarters"_s},         {u"canton"_s, u"Corner field"_s},
    {u"border"_s, u"Border"_s},             {u"chevron"_s, u"Triangle at the pole"_s},
    {u"band"_s, u"A band across"_s},        {u"disc"_s, u"A disc in the middle"_s}};

QString kind_name(const QString &kind, const QString &fallback)
{
    for (const auto &[k, t] : KINDS)
        if (k == kind)
            return t.section(u' ', 1, -1);
    return fallback;
}

OptStr app_dir_override;

QString designs_dir(const moddata::Mod &mod)
{
    const QString app = app_dir_override ? *app_dir_override
                                         : pypath::normpath(QCoreApplication::applicationDirPath());
    QString root = mod.root;
    while (!root.isEmpty() && (root.back() == u'\\' || root.back() == u'/'))
        root.chop(1);
    return pypath::join(app, {u"flagdesigns"_s, pypath::basename(root)});
}

Shape shape(const QString &kind, double x, double y, double w, double h, const QString &color, double rot, bool lock,
            std::optional<double> t)
{
    Shape sh;
    sh.kind = kind;
    sh.x = x;
    sh.y = y;
    sh.w = {w, false};
    sh.h = {h, false};
    sh.rot = rot;
    sh.color = color;
    sh.lock = lock;
    if (thick_kind(kind))
        sh.t = t ? *t : THICK.at(kind).first;
    return sh;
}

double thickness(const Shape &sh)
{
    if (sh.t)
        return *sh.t;
    return thick_kind(sh.kind) ? THICK.at(sh.kind).first : 0.3;
}

double norm_angle(double a) { return py::mod(a + 180.0, 360.0) - 180.0; }

double snap_angle(double a)
{
    a = norm_angle(a);
    double best = SNAP_ANGLES[0];
    for (double s : SNAP_ANGLES)
        if (std::abs(s - a) < std::abs(best - a))
            best = s;
    return norm_angle(best);
}

std::pair<QString, std::vector<Shape>> preset(const QString &key, const std::vector<QString> &c)
{
    const QString &c1 = c[0], &c2 = c[1], &c3 = c[2];
    auto R = [](double x, double y, double w, double h, const QString &col, double rot = 0) {
        return shape(u"rect"_s, x + w / 2.0, y + h / 2.0, w, h, col, rot);
    };
    if (key == u"h2")
        return {c1, {R(0, H / 2.0, W, H / 2.0, c2)}};
    if (key == u"h3")
        return {c1, {R(0, H / 3.0, W, H / 3.0, c2), R(0, 2 * H / 3.0, W, H / 3.0, c3)}};
    if (key == u"v2")
        return {c1, {R(W / 2.0, 0, W / 2.0, H, c2)}};
    if (key == u"v3")
        return {c1, {R(W / 3.0, 0, W / 3.0, H, c2), R(2 * W / 3.0, 0, W / 3.0, H, c3)}};
    if (key == u"nordic" || key == u"cross") {
        const double cx = key == u"nordic" ? W * 0.36 : W / 2.0;
        std::vector<Shape> out;
        for (const auto &[wd, col] : {std::pair{H * 0.26, c2}, std::pair{H * 0.12, c3}}) {
            out.push_back(shape(u"rect"_s, cx, H / 2.0, wd, H, col));
            out.push_back(shape(u"rect"_s, W / 2.0, H / 2.0, W, wd, col));
        }
        return {c1, out};
    }
    if (key == u"saltire") {
        const double d = py::hypot(W, H);
        std::vector<Shape> out;
        for (const auto &[wd, col] : {std::pair{H * 0.24, c2}, std::pair{H * 0.10, c3}}) {
            out.push_back(shape(u"rect"_s, W / 2.0, H / 2.0, d + wd, wd, col, DIAG));
            out.push_back(shape(u"rect"_s, W / 2.0, H / 2.0, d + wd, wd, col, -DIAG));
        }
        return {c1, out};
    }
    if (key == u"quarters")
        return {c1, {R(W / 2.0, 0, W / 2.0, H / 2.0, c2), R(0, H / 2.0, W / 2.0, H / 2.0, c2)}};
    if (key == u"canton")
        return {c1, {R(0, 0, W * 0.45, H / 2.0, c2)}};
    if (key == u"border")
        return {c1, {shape(u"frame"_s, W / 2.0, H / 2.0, W, H, c2, 0.0, false, 0.25)}};
    if (key == u"chevron")
        return {c1, {R(0, H / 2.0, W, H / 2.0, c2), shape(u"triangle"_s, W * 0.2, H / 2.0, H, W * 0.4, c3, 90)}};
    if (key == u"band")
        return {c1, {R(0, H * 0.36, W, H * 0.28, c2)}};
    if (key == u"disc")
        return {c1, {shape(u"ellipse"_s, W / 2.0, H / 2.0, H * 0.5, H * 0.5, c2)}};
    return {c1, {}};
}

QPainterPath shape_path(const Shape &sh)
{
    const double w = sh.w.v, h = sh.h.v;
    const QString &k = sh.kind;
    const double t = std::max(0.02, std::min(0.98, thickness(sh)));
    QPainterPath p;
    if (k == u"ellipse") {
        p.addEllipse(QRectF(-w / 2, -h / 2, w, h));
    } else if (k == u"triangle") {
        p.addPolygon(QPolygonF({QPointF(0, -h / 2), QPointF(w / 2, h / 2), QPointF(-w / 2, h / 2)}));
        p.closeSubpath();
    } else if (k == u"star") {
        QList<QPointF> pts;
        for (int i = 0; i < 10; ++i) {
            const double a = radians(double(-90 + i * 36));
            const double r = i % 2 == 0 ? 0.5 : 0.5 * t;
            pts << QPointF(std::cos(a) * r * w / 0.951, (std::sin(a) * r + 0.0477) * h / 0.905);
        }
        p.addPolygon(QPolygonF(pts));
        p.closeSubpath();
    } else if (k == u"cross") {
        const double a = std::min(w, h) * t;
        p.addRect(QRectF(-a / 2, -h / 2, a, h));
        QPainterPath q;
        q.addRect(QRectF(-w / 2, -a / 2, w, a));
        p = p.united(q);
    } else if (k == u"diamond") {
        p.addPolygon(QPolygonF({QPointF(0, -h / 2), QPointF(w / 2, 0), QPointF(0, h / 2), QPointF(-w / 2, 0)}));
        p.closeSubpath();
    } else if (k == u"crescent") {
        p.addEllipse(QRectF(-w / 2, -h / 2, w, h));
        QPainterPath q;
        q.addEllipse(QRectF(-w / 2 + w * t, -h * 0.41, w * 0.82, h * 0.82));
        p = p.subtracted(q);
    } else if (k == u"ring") {
        p.addEllipse(QRectF(-w / 2, -h / 2, w, h));
        const double a = std::min(w, h) * t / 2.0;            // the line's thickness
        if (w - 2 * a > 0.2 && h - 2 * a > 0.2) {
            QPainterPath q;
            q.addEllipse(QRectF(-w / 2 + a, -h / 2 + a, w - 2 * a, h - 2 * a));
            p = p.subtracted(q);
        }
    } else if (k == u"frame") {
        p.addRect(QRectF(-w / 2, -h / 2, w, h));
        const double a = std::min(w, h) * t / 2.0;
        if (w - 2 * a > 0.2 && h - 2 * a > 0.2) {
            QPainterPath q;
            q.addRect(QRectF(-w / 2 + a, -h / 2 + a, w - 2 * a, h - 2 * a));
            p = p.subtracted(q);
        }
    } else {
        p.addRect(QRectF(-w / 2, -h / 2, w, h));
    }
    return p;
}

QTransform shape_tf(const Shape &sh)
{
    QTransform t;
    t.translate(sh.x, sh.y);
    t.rotate(sh.rot);
    return t;
}

QRectF aabb(const Shape &sh) { return shape_tf(sh).mapRect(shape_path(sh).boundingRect()); }

void turn_group(std::vector<Shape> &shapes, const std::vector<std::pair<int, Shape>> &was, const QPointF &centre,
                double delta)
{
    const double a = radians(delta);
    const double ca = std::cos(a), sa = std::sin(a);
    for (const auto &[j, w0] : was) {
        Shape &sh = shapes[size_t(j)];
        if (sh.lock)
            continue;
        const double dx = w0.x - centre.x(), dy = w0.y - centre.y();
        sh.x = centre.x() + dx * ca - dy * sa;
        sh.y = centre.y() + dx * sa + dy * ca;
        sh.rot = norm_angle(w0.rot + delta);
    }
}

void paint_design(QPainter &p, const Design &design)
{
    p.fillRect(QRectF(0, 0, W, H), QColor(design.bg));
    if (design.base) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(0, 0, W, H), *design.base);
    }
    p.setPen(Qt::NoPen);
    for (const Shape &sh : design.shapes) {
        p.save();
        p.setTransform(shape_tf(sh), true);
        p.setBrush(QColor(sh.color));
        p.drawPath(shape_path(sh));
        p.restore();
    }
}

QImage render(const Design &design, int scale)
{
    QImage img(W * scale, H * scale, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(0, 0, W * scale, H * scale);
    p.scale(scale, scale);
    paint_design(p, design);
    p.end();
    return img;
}

QImage final_image(const Design &design)
{
    return render(design, 4).scaled(W, H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

std::pair<QImage, double> asymmetry(const Design &design, const QString &mode)
{
    const QImage img = render(design, 2).convertToFormat(QImage::Format_RGB888);
    const int w = img.width(), h = img.height();
    auto px = [&](int x, int y, int c) { return int(img.constScanLine(y)[x * 3 + c]); };
    QImage out(w, h, QImage::Format_RGBA8888);
    long long n = 0;
    for (int y = 0; y < h; ++y) {
        uchar *o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            bool diff = false;
            if (mode == u"lr" || mode == u"both") {
                int m = 0;
                for (int c = 0; c < 3; ++c)
                    m = std::max(m, std::abs(px(x, y, c) - px(w - 1 - x, y, c)));
                diff = diff || m > 40;
            }
            if (mode == u"tb" || mode == u"both") {
                int m = 0;
                for (int c = 0; c < 3; ++c)
                    m = std::max(m, std::abs(px(x, y, c) - px(x, h - 1 - y, c)));
                diff = diff || m > 40;
            }
            uchar *q = o + x * 4;
            if (!diff) {
                q[0] = q[1] = q[2] = q[3] = 0;
                continue;
            }
            ++n;
            const bool hatch = ((x + y) / 2) % 2 == 0;
            q[0] = 255;
            q[1] = hatch ? 40 : 255;
            q[2] = hatch ? 200 : 255;
            q[3] = 235;
        }
    }
    return {out, double(n) / double(qint64(w) * h)};
}

quint32 image_crc(const QImage &img) { return crc32(rgb_bytes(img)); }

QString design_json(const Design &design, quint32 crc)
{
    pyjson::Array shapes;
    for (const Shape &sh : design.shapes) {
        pyjson::Object o;
        o.insert(u"kind"_s, sh.kind);
        o.insert(u"x"_s, sh.x);
        o.insert(u"y"_s, sh.y);
        o.insert(u"w"_s, num_value(sh.w));
        o.insert(u"h"_s, num_value(sh.h));
        o.insert(u"rot"_s, sh.rot);
        o.insert(u"color"_s, sh.color);
        o.insert(u"lock"_s, sh.lock);
        if (sh.t)
            o.insert(u"t"_s, *sh.t);
        shapes.push_back(std::move(o));
    }
    pyjson::Object d;
    d.insert(u"bg"_s, design.bg);
    d.insert(u"shapes"_s, std::move(shapes));
    d.insert(u"crc"_s, static_cast<long long>(crc));
    if (design.base) {
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QBuffer::WriteOnly);
        design.base->save(&buf, "PNG");
        d.insert(u"base"_s, QString::fromLatin1(ba.toBase64()));
    }
    return pyjson::dumps(d);
}

std::optional<std::pair<Design, std::optional<long long>>> design_from_json(const QString &text)
{
    const std::optional<pyjson::Value> v = pyjson::loads(text);
    if (!v || !v->is_obj())
        return std::nullopt;
    const pyjson::Value *bg = v->get(u"bg"_s), *shapes = v->get(u"shapes"_s);
    if (!bg || !shapes || !shapes->is_arr())
        return std::nullopt;
    Design d;
    d.bg = bg->str();
    try {
        for (const pyjson::Value &s : shapes->arr()) {
            if (!s.is_obj())
                return std::nullopt;
            Shape sh;
            auto g = [&](const QString &k) -> const pyjson::Value & {
                const pyjson::Value *x = s.get(k);
                if (!x)
                    throw std::runtime_error("missing");
                return *x;
            };
            sh.kind = g(u"kind"_s).str();
            sh.x = num_of(g(u"x"_s)).v;
            sh.y = num_of(g(u"y"_s)).v;
            sh.w = num_of(g(u"w"_s));
            sh.h = num_of(g(u"h"_s));
            sh.rot = num_of(g(u"rot"_s)).v;
            sh.color = g(u"color"_s).str();
            sh.lock = g(u"lock"_s).truthy();
            if (const pyjson::Value *t = s.get(u"t"_s))
                sh.t = num_of(*t).v;
            d.shapes.push_back(sh);
        }
    } catch (const std::exception &) {
        return std::nullopt;
    }
    if (const pyjson::Value *b = v->get(u"base"_s); b && b->truthy()) {
        QImage img;
        img.loadFromData(QByteArray::fromBase64(b->str().toLatin1()), "PNG");
        d.base = img;
    }
    std::optional<long long> crc;
    if (const pyjson::Value *c = v->get(u"crc"_s); c && std::holds_alternative<long long>(c->v))
        crc = std::get<long long>(c->v);
    return std::make_pair(d, crc);
}

// ---------------------------------------------------------------------------------------------- the canvas

Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setFixedSize(W * S + 2 * M, H * S + 2 * M);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void Canvas::set_sel(const std::vector<int> &idx)
{
    const int n = int(design.shapes.size());
    std::vector<int> out;
    for (int i : idx)
        if (0 <= i && i < n)
            out.push_back(i);
    sels = out;
}

std::vector<Shape *> Canvas::chosen()
{
    std::vector<Shape *> out;
    for (int i : sels)
        if (0 <= i && i < int(design.shapes.size()))
            out.push_back(&design.shapes[size_t(i)]);
    return out;
}

std::optional<QRectF> Canvas::group_rect()
{
    std::optional<QRectF> r;
    for (Shape *sh : chosen())
        r = !r ? aabb(*sh) : r->united(aabb(*sh));
    return r;
}

QPointF Canvas::to_flag(const QPointF &pt) const { return QPointF((pt.x() - M) / S, (pt.y() - M) / S); }

QPointF Canvas::_c(const QPointF &q) const { return QPointF(M + q.x() * S, M + q.y() * S); }

std::vector<std::pair<QString, QPointF>> Canvas::_handles(const Shape &sh) const
{
    const QTransform t = shape_tf(sh);
    const double w = sh.w.v / 2.0, h = sh.h.v / 2.0;
    return {{u"tl"_s, _c(t.map(QPointF(-w, -h)))},
            {u"tr"_s, _c(t.map(QPointF(w, -h)))},
            {u"br"_s, _c(t.map(QPointF(w, h)))},
            {u"bl"_s, _c(t.map(QPointF(-w, h)))},
            {u"rot"_s, _c(t.map(QPointF(0, -h - 34.0 / S)))}};
}

static QPointF handle(const std::vector<std::pair<QString, QPointF>> &hs, const QString &name)
{
    for (const auto &[n, pt] : hs)
        if (n == name)
            return pt;
    return {};
}

std::optional<QPointF> Canvas::_group_knob()
{
    const std::optional<QRectF> r = group_rect();
    if (!r)
        return std::nullopt;
    return _c(QPointF(r->center().x(), r->top() - 34.0 / S));
}

int Canvas::_hit(const QPointF &fp) const
{
    for (int i = int(design.shapes.size()) - 1; i >= 0; --i) {
        const Shape &sh = design.shapes[size_t(i)];
        bool ok = false;
        const QTransform inv = shape_tf(sh).inverted(&ok);
        if (!ok)
            continue;
        const QPointF lp = inv.map(fp);
        const bool thin = std::min(sh.w.v, sh.h.v) < 3 ||
                          ((sh.kind == u"ring" || sh.kind == u"frame") && thickness(sh) < 0.2);
        if (shape_path(sh).contains(lp) ||
            (thin && QRectF(-sh.w.v / 2 - 1, -sh.h.v / 2 - 1, sh.w.v + 2, sh.h.v + 2).contains(lp)))
            return i;
    }
    return -1;
}

std::pair<std::vector<double>, std::vector<double>> Canvas::snap_lines(const std::vector<int> &skip) const
{
    std::vector<double> xs{0, W / 4.0, W / 3.0, W / 2.0, 2 * W / 3.0, 3 * W / 4.0, double(W)};
    std::vector<double> ys{0, H / 4.0, H / 3.0, H / 2.0, 2 * H / 3.0, 3 * H / 4.0, double(H)};
    for (int i = 0; i < int(design.shapes.size()); ++i) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end())
            continue;
        const QRectF r = aabb(design.shapes[size_t(i)]);
        xs.insert(xs.end(), {r.left(), r.center().x(), r.right()});
        ys.insert(ys.end(), {r.top(), r.center().y(), r.bottom()});
    }
    return {xs, ys};
}

std::pair<double, std::optional<double>> Canvas::_near(const std::vector<double> &vals, const std::vector<double> &lines,
                                                       double tol)
{
    double best_abs = tol + 1, best_d = 0.0;
    std::optional<double> best_ln;
    for (double v : vals)
        for (double ln : lines) {
            const double d = ln - v;
            if (std::abs(d) < best_abs) {
                best_abs = std::abs(d);
                best_d = d;
                best_ln = ln;
            }
        }
    if (best_abs <= tol)
        return {best_d, best_ln};
    return {0.0, std::nullopt};
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(u"#0d1014"_s));
    p.setRenderHint(QPainter::Antialiasing);
    p.save();
    p.translate(M, M);
    p.setClipRect(QRectF(0, 0, W * S, H * S));
    p.scale(S, S);
    paint_design(p, design);
    p.restore();
    if (asym && _asym_img) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(QRectF(M, M, W * S, H * S), *_asym_img);
        p.setPen(QPen(QColor(255, 40, 200), 2, Qt::DashLine));
        if (*asym == u"lr" || *asym == u"both")
            p.drawLine(QPointF(M + W * S / 2.0, M - 14), QPointF(M + W * S / 2.0, M + H * S + 14));
        if (*asym == u"tb" || *asym == u"both")
            p.drawLine(QPointF(M - 14, M + H * S / 2.0), QPointF(M + W * S + 14, M + H * S / 2.0));
    }
    p.setPen(QPen(QColor(theme::GOLD), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(M - 1, M - 1, W * S + 2, H * S + 2));
    for (const auto &[kind, v] : _guides) {                         // the lines the shape snapped to
        p.setPen(QPen(QColor(80, 230, 255), 2, Qt::DashLine));
        if (kind == u"v") {
            p.drawLine(QPointF(M + v * S, 4), QPointF(M + v * S, height() - 4));
        } else if (kind == u"h") {
            p.drawLine(QPointF(4, M + v * S), QPointF(width() - 4, M + v * S));
        } else {                                                    // the flag's diagonal (a turn snapped to it)
            if (v > 0)
                p.drawLine(QPointF(M, M), QPointF(M + W * S, M + H * S));
            else
                p.drawLine(QPointF(M, M + H * S), QPointF(M + W * S, M));
        }
    }
    const std::vector<Shape *> ch = chosen();
    for (Shape *sh : ch) {
        const auto hs = _handles(*sh);
        const QColor col = sh->lock ? QColor(u"#ff786e"_s) : QColor(u"#ffffff"_s);
        const QPolygonF poly({handle(hs, u"tl"_s), handle(hs, u"tr"_s), handle(hs, u"br"_s), handle(hs, u"bl"_s)});
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0), 4));
        p.drawPolygon(poly);
        p.setPen(QPen(col, 2, Qt::DashLine));
        p.drawPolygon(poly);
    }
    if (ch.size() == 1 && !ch[0]->lock) {
        const auto hs = _handles(*ch[0]);
        const QPointF tl = handle(hs, u"tl"_s), tr = handle(hs, u"tr"_s);
        const QPointF top((tl.x() + tr.x()) / 2.0, (tl.y() + tr.y()) / 2.0);
        _knob(p, top, handle(hs, u"rot"_s));
        for (const QString &name : {u"tl"_s, u"tr"_s, u"br"_s, u"bl"_s}) {
            const QPointF h = handle(hs, name);
            p.setPen(QPen(QColor(0, 0, 0), 2));
            p.setBrush(QColor(u"#ffffff"_s));
            p.drawRect(QRectF(h.x() - 6, h.y() - 6, 12, 12));
        }
    } else if (ch.size() > 1) {
        const QRectF r = *group_rect();
        const QRectF box(_c(r.topLeft()), _c(r.bottomRight()));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(theme::GOLD), 2, Qt::DotLine));
        p.drawRect(box.adjusted(-4, -4, 4, 4));
        if (std::any_of(ch.begin(), ch.end(), [](Shape *s) { return !s->lock; }))
            _knob(p, QPointF(box.center().x(), box.top() - 4), *_group_knob());
    }
    p.end();
}

void Canvas::_knob(QPainter &p, const QPointF &top, const QPointF &at)
{
    p.setPen(QPen(QColor(0, 0, 0), 4));
    p.drawLine(top, at);
    p.setPen(QPen(QColor(theme::GOLD), 2));
    p.drawLine(top, at);
    p.setPen(QPen(QColor(0, 0, 0), 2));
    p.setBrush(QColor(theme::GOLD));
    p.drawEllipse(at, 9, 9);
}

void Canvas::refresh_asym()
{
    if (asym) {
        auto [img, share] = asymmetry(design, *asym);
        _asym_img = img;
        asym_share = share;
    } else {
        _asym_img.reset();
        asym_share = 0.0;
    }
}

void Canvas::redraw()
{
    refresh_asym();
    update();
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    setFocus();
    const QPointF pt = e->position();
    const QPointF fp = to_flag(pt);
    std::vector<Shape> &shapes = design.shapes;
    const std::vector<Shape *> ch = chosen();
    const bool ctrl = bool(e->modifiers() & Qt::ControlModifier);
    auto near = [&](const QPointF &hp) { return py::hypot(hp.x() - pt.x(), hp.y() - pt.y()) <= 12; };
    if (!ctrl && ch.size() == 1 && !ch[0]->lock) {
        for (const auto &[name, hp] : _handles(*ch[0]))
            if (near(hp)) {
                _drag = Drag{name == u"rot" ? u"rot"_s : u"size"_s, fp, {{sel(), *ch[0]}}, {}};
                return;
            }
    }
    if (!ctrl && ch.size() > 1 && std::any_of(ch.begin(), ch.end(), [](Shape *s) { return !s->lock; }) &&
        near(*_group_knob())) {
        std::vector<std::pair<int, Shape>> was;
        for (int i : sels)
            was.emplace_back(i, shapes[size_t(i)]);
        _drag = Drag{u"grot"_s, fp, was, group_rect()->center()};
        return;
    }
    const int i = _hit(fp);
    if (ctrl) {                                                     // Ctrl+click: one more / one fewer
        if (i >= 0) {
            const auto it = std::find(sels.begin(), sels.end(), i);
            if (it != sels.end())
                sels.erase(it);
            else
                sels.push_back(i);
            Q_EMIT picked();
            update();
        }
        return;
    }
    if (i < 0) {
        if (!sels.empty()) {
            sels.clear();
            Q_EMIT picked();
            update();
        }
        return;
    }
    const auto it = std::find(sels.begin(), sels.end(), i);
    if (it == sels.end()) {
        sels = {i};
    } else {                                                        // (the one clicked is the one the fields show)
        sels.erase(it);
        sels.push_back(i);
    }
    Q_EMIT picked();
    if (std::any_of(sels.begin(), sels.end(), [&](int j) { return !shapes[size_t(j)].lock; })) {
        std::vector<std::pair<int, Shape>> was;
        for (int j : sels)
            was.emplace_back(j, shapes[size_t(j)]);
        _drag = Drag{u"move"_s, fp, was, {}};
    }
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!_drag || !(e->buttons() & Qt::LeftButton))
        return;
    const QString how = _drag->how;
    const QPointF start = _drag->start;
    const std::vector<std::pair<int, Shape>> was = _drag->was;
    std::vector<Shape> &shapes = design.shapes;
    const QPointF fp = to_flag(e->position());
    const bool free = bool(e->modifiers() & Qt::AltModifier) || !snap;
    _guides.clear();
    std::vector<int> keys;
    for (const auto &[j, _w] : was)
        keys.push_back(j);
    if (how == u"move") {
        const double dx = fp.x() - start.x(), dy = fp.y() - start.y();
        for (const auto &[j, w0] : was)
            if (!shapes[size_t(j)].lock) {
                shapes[size_t(j)].x = w0.x + dx;
                shapes[size_t(j)].y = w0.y + dy;
            }
        const Shape &lead = shapes[size_t(sel())];
        if (!free && !lead.lock) {
            const auto [xs, ys] = snap_lines(keys);
            const QRectF r = *group_rect();
            const auto [sx, lx] = _near({lead.x, r.left(), r.right(), r.center().x()}, xs);
            const auto [sy, ly] = _near({lead.y, r.top(), r.bottom(), r.center().y()}, ys);
            for (int j : keys)
                if (!shapes[size_t(j)].lock) {
                    shapes[size_t(j)].x += sx;
                    shapes[size_t(j)].y += sy;
                }
            if (lx)
                _guides.emplace_back(u"v"_s, *lx);
            if (ly)
                _guides.emplace_back(u"h"_s, *ly);
        }
    } else if (how == u"size") {
        const auto &[j, w0] = was.front();
        Shape &sh = shapes[size_t(j)];
        bool ok = false;
        const QPointF lp = shape_tf(w0).inverted(&ok).map(fp);
        Num w{std::max(1.0, std::abs(lp.x()) * 2), false}, h{std::max(1.0, std::abs(lp.y()) * 2), false};
        if ((e->modifiers() & Qt::ShiftModifier) && w0.w.v > 0 && w0.h.v > 0) {   // keep its proportions
            const double k = std::max(w.v / w0.w.v, h.v / w0.h.v);
            w = {w0.w.v * k, false};
            h = {w0.h.v * k, false};
        } else if (!free && py::mod(std::abs(w0.rot), 180.0) < 0.01) {
            const auto [xs, ys] = snap_lines({j});
            const auto [dx, lx] = _near({w0.x + w.v / 2.0, w0.x - w.v / 2.0}, xs);
            const auto [dy, ly] = _near({w0.y + h.v / 2.0, w0.y - h.v / 2.0}, ys);
            if (lx) {
                w = {std::abs(*lx - w0.x) * 2, false};
                _guides.emplace_back(u"v"_s, *lx);
            }
            if (ly) {
                h = {std::abs(*ly - w0.y) * 2, false};
                _guides.emplace_back(u"h"_s, *ly);
            }
        } else if (!free) {
            w = {double(py::round_int(w.v)), true};
            h = {double(py::round_int(h.v)), true};
        }
        sh.w = max1(w);
        sh.h = max1(h);
    } else if (how == u"rot") {
        const auto &[j, w0] = was.front();
        double a = degrees(std::atan2(fp.x() - w0.x, -(fp.y() - w0.y)));
        if (!free) {
            a = snap_angle(a);
            if (std::min(std::abs(std::abs(a) - DIAG), std::abs(std::abs(a) - (180 - DIAG))) < 0.01)
                _guides.emplace_back(u"d"_s, (a > 0) == (std::abs(a) < 90) ? 1.0 : -1.0);
        }
        shapes[size_t(j)].rot = norm_angle(a);
    } else {                                                        // the picked shapes turned together
        const QPointF c = _drag->centre;
        const double a0 = degrees(std::atan2(start.x() - c.x(), -(start.y() - c.y())));
        const double a1 = degrees(std::atan2(fp.x() - c.x(), -(fp.y() - c.y())));
        double d = norm_angle(a1 - a0);
        if (!free)
            d = snap_angle(d);
        turn_group(shapes, was, c, d);
    }
    redraw();
    Q_EMIT changed();
}

void Canvas::mouseReleaseEvent(QMouseEvent *)
{
    if (_drag) {
        bool moved = false;
        for (const auto &[j, w0] : _drag->was)
            if (!(design.shapes[size_t(j)] == w0))
                moved = true;
        _drag.reset();
        _guides.clear();
        update();
        if (moved)
            Q_EMIT committed();
    }
}

void Canvas::keyPressEvent(QKeyEvent *e)
{
    std::optional<std::pair<int, int>> step;
    switch (e->key()) {
    case Qt::Key_Left: step = {-1, 0}; break;
    case Qt::Key_Right: step = {1, 0}; break;
    case Qt::Key_Up: step = {0, -1}; break;
    case Qt::Key_Down: step = {0, 1}; break;
    default: break;
    }
    std::vector<Shape *> free;
    for (Shape *sh : chosen())
        if (!sh->lock)
            free.push_back(sh);
    if (step && !free.empty()) {
        const double k = (e->modifiers() & Qt::ShiftModifier) ? 0.25 : 1.0;
        for (Shape *sh : free) {
            sh->x += step->first * k;
            sh->y += step->second * k;
        }
        redraw();
        Q_EMIT changed();
        Q_EMIT committed();
        return;
    }
    QWidget::keyPressEvent(e);
}

_Thumb::_Thumb(QWidget *parent) : QLabel(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
}

void _Thumb::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT clicked();
}

void _Thumb::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT doubleClicked();
}

// ---------------------------------------------------------------------------------------------- the window

static QPushButton *small_button(const QString &text, const QString &pad)
{
    auto *b = new QPushButton(text);
    b->setStyleSheet(u"QPushButton { padding: %1; }"_s.arg(pad));
    return b;
}

FlagStudio::FlagStudio(moddata::Mod &mod_, QWidget *parent) : QDialog(parent), mod(mod_)
{
    setWindowFlags(Qt::Window);
    setWindowTitle(u"Flag studio"_s);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    outer->addLayout(top, 1);
    // --- left: where to start from, the shapes
    auto *left = new QWidget();
    left->setObjectName(u"side"_s);
    left->setFixedWidth(330);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(12, 10, 12, 10);
    ll->setSpacing(6);
    head = label(u"Flag studio"_s, u"title"_s);
    ll->addWidget(head);
    // another nation without leaving (the user, 2026-10-19): picked here, or clicked on the map — the work on this
    // one is kept (works) and comes back when it is picked again
    auto *row = new QHBoxLayout();
    row->setSpacing(5);
    {
        std::vector<const moddata::Country *> cs;
        for (const auto &[t, c] : mod.countries)
            cs.push_back(&c);
        std::stable_sort(cs.begin(), cs.end(), [](const moddata::Country *a, const moddata::Country *b) {
            return a->name < b->name;
        });
        std::vector<dialogs::PickItem> items;
        for (const moddata::Country *c : cs)
            items.push_back({c->tag, u"%1 (%2)"_s.arg(c->name, c->tag), c->color, std::nullopt});
        nation_pick = new dialogs::PickCombo(items);
    }
    nation_pick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    nation_pick->setToolTip(u"Another nation's flags (what you did on this one is kept)"_s);
    connect(nation_pick, &QComboBox::activated, this, [this](int) {
        const dialogs::Data d = nation_pick->current();
        if (d && !d->isEmpty())
            Q_EMIT nationWanted(*d);
    });
    row->addWidget(nation_pick, 1);
    auto *b = new QPushButton(u"Pick on the map ▸"_s);
    b->setToolTip(u"The studio steps aside; click a province on the map — its owner's flags open here"_s);
    connect(b, &QPushButton::clicked, this, [this] { Q_EMIT pickOnMap(); });
    row->addWidget(b);
    ll->addLayout(row);
    ll->addWidget(_bold(u"Start from a layout"_s));
    row = new QHBoxLayout();
    row->setSpacing(5);
    preset_box = new QComboBox();
    preset_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[k, t] : PRESETS)
        preset_box->addItem(t, k);
    preset_box->setCurrentIndex(preset_box->findData(u"h3"_s));
    row->addWidget(preset_box, 1);
    for (int i = 0; i < 3; ++i) {
        auto *cb = new colorwheel::ColorButton(colors[size_t(i)]);
        cb->setFixedSize(34, 28);
        connect(cb, &colorwheel::ColorButton::changed, this, [this, i](const QString &c) { colors[size_t(i)] = c; });
        row->addWidget(cb);
        cbtn.push_back(cb);
    }
    ll->addLayout(row);
    b = new QPushButton(u"Use this layout (replaces the shapes)"_s);
    connect(b, &QPushButton::clicked, this, [this] { _use_preset(); });
    ll->addWidget(b);
    ll->addWidget(_bold(u"… or take another flag as the baseline"_s));
    ll->addWidget(label(u"The flag picked lies under your shapes: draw over it, keep what you like."_s, u"faint"_s));
    row = new QHBoxLayout();
    row->setSpacing(5);
    base_tag = new QComboBox();
    base_tag->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(base_tag, &QComboBox::activated, this, [this](int) { _fill_base_variants(); });
    row->addWidget(base_tag, 3);
    base_var = new QComboBox();
    base_var->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(base_var, 2);
    ll->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(5);
    b = new QPushButton(u"Take it as the baseline"_s);
    connect(b, &QPushButton::clicked, this, [this] { _take_base(); });
    row->addWidget(b, 1);
    clear_base = new QPushButton(u"No baseline"_s);
    connect(clear_base, &QPushButton::clicked, this, [this] { _drop_base(); });
    row->addWidget(clear_base);
    ll->addLayout(row);
    ll->addWidget(_bold(u"Add a shape"_s));
    auto *host = new QWidget();
    auto *wr = new widgets::WrapRow(host, 4);
    for (const auto &[k, t] : KINDS) {
        QPushButton *kb = small_button(t, u"5px 8px"_s);
        const QString kk = k;
        connect(kb, &QPushButton::clicked, this, [this, kk] { _add(kk); });
        wr->addWidget(kb);
    }
    ll->addWidget(host);
    ll->addWidget(_bold(u"The shapes (the last one lies on top)"_s));
    ll->addWidget(label(u"Ctrl+click picks several — here or on the flag."_s, u"faint"_s));
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderHidden(true);
    lst->setRootIsDecorated(false);
    lst->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setColumnWidth(0, 210);
    connect(lst, &QTreeWidget::itemSelectionChanged, this, [this] { _list_picked(); });
    ll->addWidget(lst, 1);
    host = new QWidget();
    wr = new widgets::WrapRow(host, 4);
    const std::vector<std::tuple<QString, QString, std::function<void()>>> tools = {
        {u"▲"_s, u"One up (further on top)"_s, [this] { _order(1); }},
        {u"▼"_s, u"One down (further below)"_s, [this] { _order(-1); }},
        {u"Copy"_s, u"Copy the picked shapes (Ctrl+C) — paste them here or into another flag"_s,
         [this] { copy_shapes(); }},
        {u"Paste"_s, u"Paste the copied shapes into this flag (Ctrl+V)"_s, [this] { paste_shapes(); }},
        {u"Mirror ↔"_s, u"A copy of the picked shapes mirrored to the other side (left ↔ right)"_s,
         [this] { _mirror(u"lr"_s); }},
        {u"Mirror ↕"_s, u"A copy of the picked shapes mirrored to the other half (top ↔ bottom)"_s,
         [this] { _mirror(u"tb"_s); }},
        {u"Flip ↔"_s, u"The picked shapes themselves flipped to the other side"_s, [this] { _mirror(u"lr"_s, false); }},
        {u"Flip ↕"_s, u"The picked shapes themselves flipped to the other half"_s, [this] { _mirror(u"tb"_s, false); }},
        {u"✕ Delete"_s, u"Take the picked shapes off (Del)"_s, [this] { _del(); }}};
    for (const auto &[t, tip, fn] : tools) {
        QPushButton *tb = small_button(t, u"5px 9px"_s);
        tb->setToolTip(tip);
        const std::function<void()> f = fn;
        connect(tb, &QPushButton::clicked, this, [f] { f(); });
        wr->addWidget(tb);
    }
    ll->addWidget(host);
    top->addWidget(left);
    // --- centre: the canvas
    auto *mid = new QWidget();
    auto *ml = new QVBoxLayout(mid);
    ml->setContentsMargins(10, 8, 10, 6);
    ml->setSpacing(6);
    auto *bar = new QHBoxLayout();
    bar->setSpacing(8);
    undo_btn = new QPushButton(u"↶ Undo"_s);
    connect(undo_btn, &QPushButton::clicked, this, [this] { undo(); });
    redo_btn = new QPushButton(u"↷ Redo"_s);
    connect(redo_btn, &QPushButton::clicked, this, [this] { redo(); });
    bar->addWidget(undo_btn);
    bar->addWidget(redo_btn);
    snap_box = new QCheckBox(u"Snap to edges, halves, thirds, quarters, other shapes and the diagonal"_s);
    snap_box->setChecked(true);
    connect(snap_box, &QCheckBox::toggled, this, [this](bool on) { canvas->snap = on; });
    bar->addWidget(snap_box);
    bar->addStretch(1);
    asym_box = new QCheckBox(u"Show asymmetries"_s);
    connect(asym_box, &QCheckBox::toggled, this, [this](bool) { _asym_changed(); });
    bar->addWidget(asym_box);
    asym_mode = new QComboBox();
    asym_mode->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    asym_mode->setFixedWidth(150);
    for (const auto &[k, t] : {std::pair{u"lr"_s, u"left ↔ right"_s}, std::pair{u"tb"_s, u"top ↔ bottom"_s},
                               std::pair{u"both"_s, u"both"_s}})
        asym_mode->addItem(t, k);
    connect(asym_mode, &QComboBox::activated, this, [this](int) { _asym_changed(); });
    bar->addWidget(asym_mode);
    ml->addLayout(bar);
    editing = new QLabel(QString());
    editing->setAlignment(Qt::AlignCenter);
    ml->addWidget(editing);
    canvas = new Canvas(mid);
    connect(canvas, &Canvas::changed, this, [this] { _canvas_changed(); });
    connect(canvas, &Canvas::committed, this, [this] { _commit_step(); });
    connect(canvas, &Canvas::picked, this, [this] { _canvas_picked(); });
    ml->addWidget(canvas, 0, Qt::AlignHCenter);
    info = label(QString(), u"sub"_s);
    ml->addWidget(info);
    ml->addWidget(label(u"Drag a shape to move it · Ctrl+click picks several: they move, turn and mirror together · "
                        u"white corners resize it around its middle (Shift keeps its proportions) · the gold knob "
                        u"turns it: every 15° and the flag's diagonal (%1°) · Alt: no snapping · arrow keys "
                        u"nudge by 1 (Shift: ¼) · Ctrl+C / Ctrl+V copy shapes, also from one flag to another."_s.arg(
                            py::fmt_f(DIAG, 1)),
                        u"faint"_s));
    ml->addStretch(1);
    top->addWidget(mid, 1);
    // --- right: the shape picked
    auto *right = new QWidget();
    right->setObjectName(u"side"_s);
    right->setFixedWidth(330);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(12, 10, 12, 10);
    rl->setSpacing(6);
    row = new QHBoxLayout();
    row->addWidget(_bold(u"Background"_s), 1);
    bg_btn = new colorwheel::ColorButton(u"#1f4fa3"_s);
    connect(bg_btn, &colorwheel::ColorButton::changed, this, [this](const QString &c) { _bg(c); });
    connect(bg_btn, &colorwheel::ColorButton::preview, this, [this](const QString &c) { _bg(c, false); });
    row->addWidget(bg_btn);
    rl->addLayout(row);
    sh_title = _bold(u"The shape picked"_s);
    rl->addWidget(sh_title);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Colour"_s, u"sub"_s, false), 0, 0);
    col_btn = new colorwheel::ColorButton(u"#ffffff"_s);
    connect(col_btn, &colorwheel::ColorButton::changed, this,
            [this](const QString &c) { _prop(u"color"_s, [c](Shape &s) { s.color = c; }); });
    connect(col_btn, &colorwheel::ColorButton::preview, this,
            [this](const QString &c) { _prop(u"color"_s, [c](Shape &s) { s.color = c; }, false); });
    g->addWidget(col_btn, 0, 1, Qt::AlignLeft);
    const std::vector<std::tuple<QString, QString, QString>> fl = {{u"x"_s, u"Middle, from the left"_s, u"0 – 93"_s},
                                                                   {u"y"_s, u"Middle, from the top"_s, u"0 – 64"_s},
                                                                   {u"w"_s, u"Width"_s, QString()},
                                                                   {u"h"_s, u"Height"_s, QString()}};
    int r = 1;
    for (const auto &[k, t, tip] : fl) {
        g->addWidget(label(t, u"sub"_s, false), r, 0);
        auto *ed = new QLineEdit();
        ed->setFixedWidth(90);
        ed->setAlignment(Qt::AlignRight);
        ed->setToolTip((!tip.isEmpty() ? u"%1 — "_s.arg(tip) : QString()) +
                       u"in the flag's own pixels (it is 93 wide, 64 high)"_s);
        const QString kk = k;
        connect(ed, &QLineEdit::editingFinished, this, [this, kk, ed] { _typed(kk, ed); });
        g->addWidget(ed, r, 1, Qt::AlignLeft);
        fields.emplace_back(k, ed);
        ++r;
    }
    g->addWidget(label(u"Turned by"_s, u"sub"_s, false), 5, 0);
    rot = new widgets::SliderField(-180, 180, 180, 0, u"°"_s, false, 1);
    rot->set_snaps({-135, -90, -45, 0, 45, 90, 135, DIAG, -DIAG, 180 - DIAG, DIAG - 180}, 0.012);
    rot->setToolTip(u"Degrees, clockwise. With several shapes picked they turn together around their middle."_s);
    connect(rot, &widgets::SliderField::valueChanged, this, [this](double v) { _turn_to(v); });
    g->addWidget(rot, 5, 1);
    thick_label = label(u"Thickness"_s, u"sub"_s, false);
    g->addWidget(thick_label, 6, 0);
    thick = new widgets::SliderField(2, 98, 98, 33, u"%"_s);
    thick->setToolTip(u"How thick the lines of the shape are (a ring, a cross, a frame, a crescent) — for a "
                      u"star: how fat its middle is"_s);
    connect(thick, &widgets::SliderField::valueChanged, this, [this](double v) {
        const double t = v / 100.0;
        _prop(u"t"_s, [t](Shape &s) { s.t = t; });
    });
    g->addWidget(thick, 6, 1);
    lock = new QCheckBox(u"Locked in place (can't be dragged, resized or turned)"_s);
    connect(lock, &QCheckBox::toggled, this, [this](bool on) { _prop(u"lock"_s, [on](Shape &s) { s.lock = on; }); });
    g->addWidget(lock, 7, 0, 1, 2);
    rl->addLayout(g);
    host = new QWidget();
    wr = new widgets::WrapRow(host, 4);
    const std::vector<std::tuple<QString, QString, QString>> places = {
        {u"Centre ↔"_s, u"Its middle onto the flag's vertical centre line"_s, u"cx"_s},
        {u"Centre ↕"_s, u"Its middle onto the flag's horizontal centre line"_s, u"cy"_s},
        {u"Fill the flag"_s, u"As wide and high as the flag, in the middle"_s, u"fill"_s},
        {u"Upright"_s, u"Turned back to 0°"_s, u"up"_s},
        {u"Sideways"_s, u"Turned to 90° (on its side)"_s, u"side"_s},
        {u"Corner ╲"_s,
         u"Turned to the flag's diagonal, from the top left corner to the bottom right one (%1°); a rectangle "
         u"becomes a band exactly from corner to corner"_s.arg(py::fmt_f(DIAG, 1)),
         u"d1"_s},
        {u"Corner ╱"_s,
         u"Turned to the flag's other diagonal, from the bottom left corner to the top right one (−%1°); a "
         u"rectangle becomes a band exactly from corner to corner"_s.arg(py::fmt_f(DIAG, 1)),
         u"d2"_s}};
    for (const auto &[t, tip, how] : places) {
        QPushButton *pb = small_button(t, u"5px 9px"_s);
        pb->setToolTip(tip);
        const QString hw = how;
        connect(pb, &QPushButton::clicked, this, [this, hw] { _place(hw); });
        wr->addWidget(pb);
    }
    rl->addWidget(host);
    rl->addSpacing(8);
    rl->addWidget(_bold(u"As the game shows it (93 × 64)"_s));
    small = new QLabel();
    small->setFixedSize(W * 2 + 4, H * 2 + 4);
    small->setAlignment(Qt::AlignCenter);
    small->setStyleSheet(u"border: 2px solid %1; background: %2;"_s.arg(theme::LINE, theme::BG));
    rl->addWidget(small, 0, Qt::AlignLeft);
    rl->addStretch(1);
    top->addWidget(right);
    // --- below: the nation's flags
    auto *foot = new QWidget();
    foot->setObjectName(u"side"_s);
    auto *fo = new QVBoxLayout(foot);
    fo->setContentsMargins(12, 8, 12, 10);
    fo->setSpacing(6);
    foot_title = _bold(u"This nation's flags"_s);
    fo->addWidget(foot_title);
    thumb_host = new QWidget();
    thumb_row = new widgets::WrapRow(thumb_host, 10);
    fo->addWidget(thumb_host);
    row = new QHBoxLayout();
    row->setSpacing(10);
    set_btn = new QPushButton(u"Set this flag"_s);
    set_btn->setObjectName(u"apply"_s);
    connect(set_btn, &QPushButton::clicked, this, [this] { _commit(); });
    row->addWidget(set_btn);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    row->addWidget(warn, 1);
    fo->addLayout(row);
    outer->addWidget(foot);
    const std::vector<std::pair<QString, std::function<void()>>> keys = {
        {u"Ctrl+Z"_s, [this] { undo(); }},        {u"Ctrl+Y"_s, [this] { redo(); }},
        {u"Ctrl+Shift+Z"_s, [this] { redo(); }},  {u"Delete"_s, [this] { _del(); }},
        {u"Ctrl+D"_s, [this] { _dup(); }},        {u"Ctrl+C"_s, [this] { copy_shapes(); }},
        {u"Ctrl+V"_s, [this] { paste_shapes(); }}, {u"Ctrl+A"_s, [this] { _select_all(); }}};
    for (const auto &[k, fn] : keys) {
        auto *sc = new QShortcut(QKeySequence(k), this);
        const std::function<void()> f = fn;
        connect(sc, &QShortcut::activated, this, [f] { f(); });
    }
    auto [bg, shapes] = preset(u"h3"_s, colors);
    canvas->design = Design{bg, std::nullopt, shapes};
    _last = _state();
    _sync();
}

QLabel *FlagStudio::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

void FlagStudio::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)   // (a dialog would close)
        return;
    QDialog::keyPressEvent(e);
}

// ------------------------------------------------------------------ the design, undo

Design FlagStudio::_state() const { return canvas->design; }

void FlagStudio::_set_state(const Design &st, const std::vector<int> &sels)
{
    canvas->design = st;
    canvas->set_sel(sels);
    _sync();
}

void FlagStudio::_commit_step()
{
    _undo->push_back(_last);
    if (_undo->size() > 100)
        _undo->erase(_undo->begin(), _undo->end() - 100);
    _redo = std::make_shared<std::vector<Design>>();
    _last = _state();
    if (Entry *e = _entry())
        e->dirty = true;
    _sync();
    _show_cells();
}

void FlagStudio::undo()
{
    if (!_undo->empty()) {
        _redo->push_back(_last);
        _last = _undo->back();
        _undo->pop_back();
        _set_state(_last);
        _touch();
    }
}

void FlagStudio::redo()
{
    if (!_redo->empty()) {
        _undo->push_back(_last);
        _last = _redo->back();
        _redo->pop_back();
        _set_state(_last);
        _touch();
    }
}

void FlagStudio::_touch()
{
    if (Entry *e = _entry())
        e->dirty = true;
    _show_cells();
}

void FlagStudio::_sync()
{
    _building = true;
    const Design &d = design();
    canvas->set_sel(canvas->sels);
    bg_btn->set(d.bg, false);
    lst->blockSignals(true);
    lst->clear();
    for (size_t i = 0; i < d.shapes.size(); ++i) {
        const Shape &sh = d.shapes[i];
        const QColor c(sh.color);
        auto *it = new QTreeWidgetItem(QStringList{u"%1  %2"_s.arg(i + 1).arg(kind_name(sh.kind, sh.kind)),
                                                   sh.lock ? u"locked"_s : QString()});
        it->setIcon(0, widgets::swatch_icon({c.red(), c.green(), c.blue()}, 14));
        it->setData(0, Qt::UserRole, int(i));
        lst->addTopLevelItem(it);
    }
    _select_in_list();
    lst->blockSignals(false);
    _show_shape();
    clear_base->setEnabled(d.base.has_value());
    undo_btn->setEnabled(!_undo->empty());
    redo_btn->setEnabled(!_redo->empty());
    _building = false;
    canvas->redraw();
    _show_small();
}

void FlagStudio::_select_in_list()
{
    lst->clearSelection();
    for (int i : canvas->sels)
        if (QTreeWidgetItem *it = lst->topLevelItem(i))
            it->setSelected(true);
    if (canvas->sel() >= 0)
        lst->setCurrentItem(lst->topLevelItem(canvas->sel()), 0, QItemSelectionModel::NoUpdate);
}

void FlagStudio::_show_small()
{
    small->setPixmap(QPixmap::fromImage(final_image(design())).scaled(W * 2, H * 2));
    if (canvas->asym) {
        const double sh = canvas->asym_share;
        info->setText(sh < 0.0005 ? u"Symmetric (%1)."_s.arg(asym_mode->currentText())
                                  : u"Asymmetric: %1 % of the flag differs from its mirror picture (%2) — marked in "
                                    u"pink and white."_s.arg(py::fmt_f(sh * 100, 1), asym_mode->currentText()));
        info->setStyleSheet(u"color: %1;"_s.arg(sh < 0.0005 ? u"#7fd68a"_s : u"#ff9ad8"_s));
    } else {
        const size_t n = canvas->sels.size();
        info->setText(u"%1 shape(s)%2%3%4"_s.arg(design().shapes.size())
                          .arg(design().base ? u" over a baseline flag"_s : QString(),
                               n > 1 ? u" · %1 picked"_s.arg(n) : QString(),
                               !clip.empty() ? u" · %1 copied (Ctrl+V pastes them)"_s.arg(clip.size()) : QString()));
        info->setStyleSheet(QString());
    }
}

Shape *FlagStudio::_shape()
{
    const int s = canvas->sel();
    return 0 <= s && s < int(design().shapes.size()) ? &design().shapes[size_t(s)] : nullptr;
}

void FlagStudio::_show_shape()
{
    Shape *sh = _shape();
    const size_t n = canvas->sels.size();
    const bool was = _building;
    _building = true;
    sh_title->setText(n > 1 ? u"%1 shapes picked — the fields show the %2"_s.arg(n).arg(
                                  py::lower(kind_name(sh->kind, QString())))
                            : u"The shape picked: %1"_s.arg(sh ? kind_name(sh->kind, sh->kind) : u"none"_s));
    col_btn->setEnabled(sh != nullptr);
    rot->setEnabled(sh != nullptr);
    lock->setEnabled(sh != nullptr);
    for (const auto &[_k, ed] : fields)
        ed->setEnabled(sh != nullptr);
    bool th = false;
    if (sh)
        for (Shape *s : canvas->chosen())
            if (thick_kind(s->kind))
                th = true;
    thick->setEnabled(th);
    thick_label->setText(sh && thick_kind(sh->kind) ? THICK.at(sh->kind).second : u"Thickness"_s);
    if (sh) {
        col_btn->set(sh->color, false);
        for (const auto &[k, ed] : fields)
            if (!ed->hasFocus()) {
                const double v = k == u"x" ? sh->x : k == u"y" ? sh->y : k == u"w" ? sh->w.v : sh->h.v;
                ed->setText(fmt2(v));
            }
        rot->blockSignals(true);
        rot->setValue(sh->rot);
        rot->blockSignals(false);
        thick->blockSignals(true);
        thick->setValue(double(py::round_int(thickness(*sh) * 100)));
        thick->blockSignals(false);
        lock->setChecked(sh->lock);
    }
    _building = was;
}

// ------------------------------------------------------------------ the canvas and the list

void FlagStudio::_canvas_changed()
{
    _show_shape();
    _show_small();
}

void FlagStudio::_canvas_picked()
{
    lst->blockSignals(true);
    _select_in_list();
    lst->blockSignals(false);
    _show_shape();
    _show_small();
}

void FlagStudio::_list_picked()
{
    std::vector<int> idx;
    for (QTreeWidgetItem *it : lst->selectedItems())
        idx.push_back(it->data(0, Qt::UserRole).toInt());
    QTreeWidgetItem *cur_it = lst->currentItem();
    const std::optional<int> c = cur_it ? std::optional<int>(cur_it->data(0, Qt::UserRole).toInt()) : std::nullopt;
    std::vector<int> out;
    for (int i : idx)
        if (!c || i != *c)
            out.push_back(i);
    std::sort(out.begin(), out.end());
    if (c && std::find(idx.begin(), idx.end(), *c) != idx.end())
        out.push_back(*c);
    canvas->set_sel(out);
    canvas->update();
    _show_shape();
    _show_small();
}

void FlagStudio::_select_all()
{
    std::vector<int> all;
    for (int i = 0; i < int(design().shapes.size()); ++i)
        all.push_back(i);
    canvas->set_sel(all);
    _canvas_picked();
    canvas->update();
}

void FlagStudio::_asym_changed()
{
    canvas->asym = asym_box->isChecked() ? OptStr(asym_mode->currentData().toString()) : std::nullopt;
    canvas->redraw();
    _show_small();
}

// ------------------------------------------------------------------ changes

void FlagStudio::_prop(const QString &key, const std::function<void(Shape &)> &set, bool commit)
{
    const std::vector<Shape *> ch = canvas->chosen();
    if (ch.empty() || _building)
        return;
    for (Shape *sh : ch) {
        if (key == u"t" && !thick_kind(sh->kind))
            continue;
        set(*sh);
    }
    if (commit) {
        if (_last.shapes != design().shapes)
            _commit_step();
    } else {
        canvas->redraw();
        _show_small();
    }
}

void FlagStudio::_turn_to(double angle)
{
    Shape *sh = _shape();
    if (!sh || _building)
        return;
    const std::vector<Shape *> ch = canvas->chosen();
    if (ch.size() > 1) {
        std::vector<std::pair<int, Shape>> was;
        for (int i : canvas->sels)
            was.emplace_back(i, design().shapes[size_t(i)]);
        turn_group(design().shapes, was, canvas->group_rect()->center(), norm_angle(angle - sh->rot));
    } else if (!sh->lock) {
        sh->rot = norm_angle(angle);
    }
    if (_last.shapes != design().shapes)
        _commit_step();
}

void FlagStudio::_typed(const QString &key, QLineEdit *ed)
{
    Shape *sh = _shape();
    if (!sh)
        return;
    const std::optional<double> parsed = py::to_float(py::strip(QString(ed->text()).replace(u","_s, u"."_s)));
    if (!parsed) {
        _show_shape();
        return;
    }
    double v = *parsed;
    if (key == u"w" || key == u"h")
        v = std::max(1.0, v);
    v = std::max(-200.0, std::min(400.0, v));
    const double old = key == u"x" ? sh->x : key == u"y" ? sh->y : key == u"w" ? sh->w.v : sh->h.v;
    if (std::abs(v - old) > 1e-6) {
        if (key == u"x" || key == u"y") {                         // (several picked: they keep their places)
            const double d = v - old;
            for (Shape *s : canvas->chosen())
                if (!s->lock || s == sh)
                    (key == u"x" ? s->x : s->y) += d;
        } else {
            (key == u"w" ? sh->w : sh->h) = Num{v, false};
        }
        _commit_step();
    }
}

void FlagStudio::_bg(const QString &c, bool commit)
{
    if (_building)
        return;
    design().bg = c;
    if (commit) {
        _commit_step();
    } else {
        canvas->redraw();
        _show_small();
    }
}

void FlagStudio::_add(const QString &kind)
{
    QString col = u"#ffffff"_s;
    for (const QString &c : colors)
        if (c != design().bg) {
            col = c;
            break;
        }
    std::pair<double, double> size{H * 0.5, H * 0.5};
    if (kind == u"rect")
        size = {W / 3.0, double(H)};
    else if (kind == u"cross")
        size = {H * 0.6, H * 0.6};
    else if (kind == u"frame")
        size = {W * 0.6, H * 0.6};
    design().shapes.push_back(shape(kind, W / 2.0, H / 2.0, size.first, size.second, col));
    canvas->set_sel({int(design().shapes.size()) - 1});
    _commit_step();
}

void FlagStudio::_del()
{
    if (!canvas->sels.empty()) {
        const std::set<int> gone(canvas->sels.begin(), canvas->sels.end());
        std::vector<Shape> keep;
        for (size_t i = 0; i < design().shapes.size(); ++i)
            if (!gone.count(int(i)))
                keep.push_back(design().shapes[i]);
        design().shapes = keep;
        canvas->set_sel({});
        _commit_step();
    }
}

void FlagStudio::_insert(std::vector<Shape> fresh, const std::vector<int> &after)
{
    std::vector<Shape> &shapes = design().shapes;
    const int at = !after.empty() ? *std::max_element(after.begin(), after.end()) + 1 : int(shapes.size());
    const int n = int(fresh.size());
    shapes.insert(shapes.begin() + at, fresh.begin(), fresh.end());
    std::vector<int> sel;
    for (int i = at; i < at + n; ++i)
        sel.push_back(i);
    canvas->set_sel(sel);
    _commit_step();
}

void FlagStudio::_dup()
{
    std::vector<int> idx = canvas->sels;
    std::sort(idx.begin(), idx.end());
    std::vector<Shape> ch;
    for (int i : idx) {
        Shape s = design().shapes[size_t(i)];
        s.x += 4;
        s.y += 4;
        s.lock = false;
        ch.push_back(s);
    }
    if (!ch.empty())
        _insert(ch, canvas->sels);
}

void FlagStudio::copy_shapes()
{
    std::vector<int> idx = canvas->sels;
    std::sort(idx.begin(), idx.end());
    std::vector<Shape> ch;
    for (int i : idx)
        ch.push_back(design().shapes[size_t(i)]);
    if (!ch.empty()) {
        clip = ch;
        _show_small();
    }
}

void FlagStudio::paste_shapes()
{
    if (clip.empty())
        return;
    std::vector<Shape> fresh = clip;
    std::set<std::tuple<QString, QString, QString>> have;
    for (const Shape &s : design().shapes)
        have.insert({s.kind, round2(s.x), round2(s.y)});
    auto taken = [&] {
        for (const Shape &s : fresh)
            if (have.count({s.kind, round2(s.x), round2(s.y)}))
                return true;
        return false;
    };
    while (taken())
        for (Shape &s : fresh) {
            s.x += 4;
            s.y += 4;
        }
    for (Shape &s : fresh)
        s.lock = false;
    _insert(fresh);
}

void FlagStudio::_mirror(const QString &how, bool as_copy)
{
    std::vector<int> idx = canvas->sels;
    std::sort(idx.begin(), idx.end());
    if (idx.empty())
        return;
    std::vector<Shape> &shapes = design().shapes;
    auto flip = [&](Shape &s) {
        if (how == u"lr") {
            s.x = W - s.x;
            s.rot = norm_angle(-s.rot);
        } else {
            s.y = H - s.y;
            s.rot = norm_angle(180 - s.rot);
        }
    };
    if (as_copy) {
        std::vector<Shape> fresh;
        for (int i : idx) {
            Shape s = shapes[size_t(i)];
            s.lock = false;
            flip(s);
            fresh.push_back(s);
        }
        _insert(fresh, idx);
    } else {
        for (int i : idx)
            if (!shapes[size_t(i)].lock)
                flip(shapes[size_t(i)]);
        _commit_step();
    }
}

void FlagStudio::_order(int step)
{
    std::vector<Shape> &shapes = design().shapes;
    std::vector<int> idx = canvas->sels;
    std::sort(idx.begin(), idx.end());
    if (step > 0)
        std::reverse(idx.begin(), idx.end());
    if (idx.empty())
        return;
    for (int i : idx)
        if (!(0 <= i + step && i + step < int(shapes.size())))
            return;
    std::vector<int> fresh;
    for (int i : idx) {
        const int j = i + step;
        if (std::find(fresh.begin(), fresh.end(), j) != fresh.end())
            return;
        std::swap(shapes[size_t(i)], shapes[size_t(j)]);
        fresh.push_back(j);
    }
    std::sort(fresh.begin(), fresh.end());
    canvas->set_sel(fresh);
    _commit_step();
}

void FlagStudio::_place(const QString &how)
{
    std::vector<Shape *> ch;
    for (Shape *s : canvas->chosen())
        if (!s->lock)
            ch.push_back(s);
    if (ch.empty())
        return;
    const double diag = py::hypot(W, H);
    for (Shape *sh : ch) {
        if (how == u"cx") {
            sh->x = W / 2.0;
        } else if (how == u"cy") {
            sh->y = H / 2.0;
        } else if (how == u"up") {
            sh->rot = 0.0;
        } else if (how == u"side") {
            sh->rot = 90.0;
        } else if (how == u"d1" || how == u"d2") {
            sh->rot = how == u"d1" ? DIAG : -DIAG;
            if (sh->kind == u"rect") {                           // a band exactly from corner to corner
                const Num thin = sh->h.v < sh->w.v ? sh->h : sh->w;   // min(w, h): the first when equal
                sh->x = W / 2.0;
                sh->y = H / 2.0;
                sh->w = Num{diag + thin.v, false};
                sh->h = thin;
            }
        } else {
            sh->x = W / 2.0;
            sh->y = H / 2.0;
            sh->w = Num{double(W), false};
            sh->h = Num{double(H), false};
            sh->rot = 0.0;
        }
    }
    if (_last.shapes != design().shapes)
        _commit_step();
}

void FlagStudio::_use_preset()
{
    auto [bg, shapes] = preset(preset_box->currentData().toString(), colors);
    canvas->design = Design{bg, std::nullopt, shapes};
    canvas->set_sel({});
    _commit_step();
}

// ------------------------------------------------------------------ baselines

OrderedMap<QString, QStringList> FlagStudio::_flag_files() const
{
    OrderedMap<QString, QStringList> out;
    QStringList roots{pypath::join(mod.root, {u"gfx"_s, u"flags"_s})};
    if (mod.base && !mod.base->isEmpty() && !mod.replaced({u"gfx"_s, u"flags"_s}))
        roots << pypath::join(*mod.base, {u"gfx"_s, u"flags"_s});
    QSet<QString> seen;
    for (const QString &d : roots)
        if (fs::isdir(d))
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".tga"_s) && !seen.contains(py::lower(fn))) {
                    seen.insert(py::lower(fn));
                    out[py::upper(fn.left(3))].append(fn);
                }
    const nationextras::FlagEdits *fe = doc && doc->maps.contains(u"flags"_s)
                                            ? &doc->map<nationextras::FlagEdits>(u"flags"_s)
                                            : nullptr;
    if (fe) {
        for (const auto &[fn, _img] : fe->fresh)
            if (!seen.contains(py::lower(fn)))
                out[py::upper(fn.left(3))].append(fn);
        for (const QString &fn : fe->gone) {
            QStringList keep;
            for (const QString &f : out.value(py::upper(fn.left(3))))
                if (py::lower(f) != py::lower(fn))
                    keep << f;
            out.insert(py::upper(fn.left(3)), keep);
        }
    }
    OrderedMap<QString, QStringList> res;
    for (const auto &[t, v] : out)
        if (!v.isEmpty()) {
            QStringList s = v;
            std::stable_sort(s.begin(), s.end(), [](const QString &a, const QString &b) {
                return std::make_pair(a.size(), py::lower(a)) < std::make_pair(b.size(), py::lower(b));
            });
            res.insert(t, s);
        }
    return res;
}

void FlagStudio::_fill_base_tags()
{
    _files = _flag_files();
    const QVariant cd = base_tag->currentData();
    const QString keep = cd.isValid() && !cd.toString().isEmpty() ? cd.toString() : tag.value_or(QString());
    base_tag->clear();
    std::vector<QString> ts;
    for (const auto &[t, _v] : _files)
        ts.push_back(t);
    auto key = [&](const QString &t) {
        const moddata::Country *c = mod.countries.get(t);
        return std::make_pair(t != tag.value_or(QString()), c ? c->name : t);
    };
    std::stable_sort(ts.begin(), ts.end(), [&](const QString &a, const QString &b) { return key(a) < key(b); });
    for (const QString &t : ts) {
        const moddata::Country *c = mod.countries.get(t);
        base_tag->addItem(c ? u"%1  (%2)"_s.arg(c->name, t) : t, t);
    }
    const int i = base_tag->findData(keep);
    base_tag->setCurrentIndex(std::max(0, i));
    _fill_base_variants();
}

void FlagStudio::_fill_base_variants()
{
    base_var->clear();
    const QVariant cd = base_tag->currentData();
    if (!cd.isValid())
        return;
    for (const QString &fn : _files.value(cd.toString())) {
        QString sfx = pypath::splitext_stem(fn).mid(3);
        while (sfx.startsWith(u'_'))
            sfx.remove(0, 1);
        const QString name = py::capitalize(QString(sfx).replace(u'_', u' '));
        base_var->addItem(name.isEmpty() ? u"Normal"_s : name, fn);
    }
}

std::optional<QImage> FlagStudio::_flag_image(const QString &fn) const
{
    const nationextras::FlagEdits *fe = doc && doc->maps.contains(u"flags"_s)
                                            ? &doc->map<nationextras::FlagEdits>(u"flags"_s)
                                            : nullptr;
    std::optional<QImage> img = fe ? fe->image(fn) : std::nullopt;
    if (!img && (!fe || !fe->gone.contains(fn))) {
        const QString p = mod.p({u"gfx"_s, u"flags"_s, fn});
        img = fs::isfile(p) ? imgio::read_image(p) : std::nullopt;
    }
    return img;
}

bool FlagStudio::_take_base(const OptStr &fn0)
{
    OptStr fn = fn0 && !fn0->isEmpty() ? fn0 : std::nullopt;
    if (!fn && base_var->currentData().isValid())
        fn = base_var->currentData().toString();
    const std::optional<QImage> img = fn && !fn->isEmpty() ? _flag_image(*fn) : std::nullopt;
    if (!img) {
        warn->setText(u"That flag can't be read."_s);
        return false;
    }
    warn->setText(QString());
    design().base = img->convertToFormat(QImage::Format_RGB32);
    _commit_step();
    return true;
}

void FlagStudio::_drop_base()
{
    if (design().base) {
        design().base.reset();
        _commit_step();
    }
}

// ------------------------------------------------------------------ the nation's flags: one is being worked on

OrderedMap<QString, FlagStudio::Entry> &FlagStudio::_work() { return works[tag.value_or(QString())]; }

FlagStudio::Entry *FlagStudio::_entry(const OptStr &sfx)
{
    if (!tag || tag->isEmpty())
        return nullptr;
    return _work().get(sfx ? *sfx : cur);
}

QString FlagStudio::_name(const QString &sfx) const
{
    for (const auto &[s, n] : variants)
        if (s == sfx)
            return n;
    QString s = sfx;
    while (s.startsWith(u'_'))
        s.remove(0, 1);
    s = py::capitalize(s);
    return s.isEmpty() ? u"Normal"_s : s;
}

FlagStudio::Entry &FlagStudio::_load_entry(const QString &sfx)
{
    const QString fn = u"%1%2.tga"_s.arg(tag.value_or(QString()), sfx);
    std::optional<QImage> img = _flag_image(fn);
    std::optional<Design> design;
    QString how = u"new"_s;
    if (img) {
        img = img->convertToFormat(QImage::Format_RGB32);
        how = u"picture"_s;
        const QString p = pypath::join(designs_dir(mod), {fn + u".json"_s});
        if (fs::isfile(p)) {                                        // made here before: its shapes are still known
            QFile f(p);
            if (f.open(QIODevice::ReadOnly)) {
                const auto got = design_from_json(QString::fromUtf8(f.readAll()));
                if (got && got->second && *got->second == (long long)image_crc(*img)) {
                    design = got->first;
                    how = u"shapes"_s;
                }
            }
        }
        if (!design)
            design = Design{u"#808080"_s, img, {}};
    } else {
        design = Design{colors[0], std::nullopt, {}};
    }
    Entry e;
    e.design = *design;
    e.how = how;
    if (img)
        e.crc = image_crc(*img);
    _work().insert(sfx, e);
    return *_work().get(sfx);
}

void FlagStudio::_stash()
{
    if (Entry *e = _entry()) {
        e->design = _state();
        e->undo = _undo;
        e->redo = _redo;
    }
}

void FlagStudio::_show_entry(const QString &sfx)
{
    Entry *ep = _work().get(sfx);
    Entry &e = ep ? *ep : _load_entry(sfx);
    cur = sfx;
    _undo = e.undo;
    _redo = e.redo;
    canvas->design = e.design;
    canvas->set_sel({});
    _last = _state();
    _sync();
    _show_cells();
}

void FlagStudio::select_flag(const QString &sfx)
{
    if (std::none_of(variants.begin(), variants.end(), [&](const auto &v) { return v.first == sfx; }))
        return;
    if (sfx != cur || !_entry()) {
        _stash();
        _show_entry(sfx);
    }
    warn->setText(QString());
}

void FlagStudio::open_for(document::Document *d, const QString &t, const OptStr &sfx)
{
    if (tag)
        _stash();
    if (d != doc)
        works.clear();
    doc = d;
    tag = t;
    const moddata::Country *c = mod.countries.get(t);
    const QString name = c ? c->name : t;
    head->setText(u"Flag studio — %1 (%2)"_s.arg(name, t));
    const int i = nation_pick->find(t);
    if (i >= 0) {
        nation_pick->blockSignals(true);
        nation_pick->setCurrentIndex(i);
        nation_pick->blockSignals(false);
    }
    setWindowTitle(u"Flag studio — %1"_s.arg(name));
    warn->setText(QString());
    cur = QString();                                                // (always the normal flag to begin with)
    refresh(d, true);
    _show_entry(sfx ? *sfx : QString());
    show();
    raise();
    activateWindow();
}

void FlagStudio::refresh(document::Document *d, bool rebuild)
{
    if (d != doc) {
        doc = d;
        works.clear();
        rebuild = true;
    }
    if (!tag || !d)
        return;
    const std::vector<std::pair<QString, QString>> vs = nationextras::flag_variants(d);
    if (rebuild || vs != variants) {
        std::map<QString, bool> ticked;
        for (const auto &[s, c] : cells)
            ticked[s] = c.tick->isChecked();
        variants = vs;
        while (thumb_row->count()) {
            QLayoutItem *item = thumb_row->takeAt(0);
            if (QWidget *w = item->widget()) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
            delete item;
        }
        cells.clear();
        for (const auto &[sfx, name] : variants) {
            auto *cell = new QFrame(thumb_host);
            cell->setObjectName(u"flagCell"_s);
            cell->setFixedWidth(168);
            auto *v = new QVBoxLayout(cell);
            v->setContentsMargins(6, 6, 6, 6);
            v->setSpacing(3);
            auto *cap = new QLabel(name, cell);
            cap->setAlignment(Qt::AlignCenter);
            v->addWidget(cap);
            auto *pic = new _Thumb(cell);
            pic->setFixedSize(W + 34, H + 24);
            pic->setAlignment(Qt::AlignCenter);
            pic->setCursor(Qt::PointingHandCursor);
            const QString s = sfx;
            connect(pic, &_Thumb::doubleClicked, this, [this, s] { select_flag(s); });
            v->addWidget(pic, 0, Qt::AlignHCenter);
            auto *state = new QLabel(QString(), cell);
            state->setAlignment(Qt::AlignCenter);
            state->setObjectName(u"faint"_s);
            v->addWidget(state);
            auto *tick = new QCheckBox(u"also set it as this"_s, cell);
            tick->setChecked(ticked.count(sfx) ? ticked[sfx] : false);
            tick->setToolTip(u"Tick: “Set this flag” also hands the flag in the middle on to this one"_s);
            connect(tick, &QCheckBox::toggled, this, [this](bool) { _show_cells(); });
            v->addWidget(tick, 0, Qt::AlignHCenter);
            auto *row = new QHBoxLayout();
            row->setSpacing(4);
            auto *ed = new QPushButton(u"Edit"_s, cell);
            ed->setStyleSheet(u"QPushButton { padding: 4px 8px; }"_s);
            ed->setToolTip(u"Work on this flag (a double-click on it does the same); the one you leave keeps "
                           u"everything"_s);
            connect(ed, &QPushButton::clicked, this, [this, s] { select_flag(s); });
            row->addWidget(ed);
            auto *rm = new QPushButton(u"✕ Delete"_s, cell);
            rm->setStyleSheet(u"QPushButton { padding: 4px 8px; }"_s);
            connect(rm, &QPushButton::clicked, this, [this, s] { _delete_flag(s); });
            row->addWidget(rm);
            v->addLayout(row);
            thumb_row->addWidget(cell);
            cell->show();
            cells.insert(sfx, Cell{cell, cap, pic, state, tick, ed, rm});
        }
        thumb_host->updateGeometry();
    }
    // a flag whose file changed outside the studio (an undo in the main window, Apply …) and that has nothing unsaved
    // here: read again
    QStringList keys;
    for (const auto &[s, _e] : _work())
        keys << s;
    for (const QString &sfx : keys) {
        Entry *e = _work().get(sfx);
        if (!e)
            continue;
        if (e->dirty || (sfx == cur && isVisible() && (!_undo->empty() || !_redo->empty())))
            continue;
        const std::optional<QImage> img = _flag_image(u"%1%2.tga"_s.arg(*tag, sfx));
        const std::optional<quint32> crc =
            img ? std::optional<quint32>(image_crc(img->convertToFormat(QImage::Format_RGB32))) : std::nullopt;
        if (crc != e->crc) {
            if (!e->design.shapes.empty()) {                        // shapes made here are never thrown away: the
                e->dirty = true;                                    // flag simply counts as "not set" again
                e->crc = crc;
                continue;
            }
            _work().remove(sfx);
            if (sfx == cur && isVisible())
                _show_entry(sfx);
        }
    }
    _fill_base_tags();
    _show_cells();
}

void FlagStudio::_show_cells()
{
    if (cells.isEmpty() || !tag || !doc)
        return;
    const nationextras::FlagEdits *fe = doc->maps.contains(u"flags"_s)
                                            ? &doc->map<nationextras::FlagEdits>(u"flags"_s)
                                            : nullptr;
    OrderedMap<QString, Entry> &work = _work();
    for (const auto &[sfx, name] : variants) {
        Cell *c = cells.get(sfx);
        if (!c)
            continue;
        const QString fn = u"%1%2.tga"_s.arg(*tag, sfx);
        const std::optional<QImage> file_img = _flag_image(fn);
        const Entry *e = work.get(sfx);
        const bool is_cur = sfx == cur;
        const bool own = fs::isfile(pypath::join(mod.root, {u"gfx"_s, u"flags"_s, fn}));
        const bool pending = fe && fe->fresh.contains(fn);
        std::optional<QImage> img;
        if (is_cur)
            img = final_image(design());
        else if (e && e->dirty)
            img = final_image(e->design);
        else
            img = file_img;
        if (!img) {
            c->pic->setPixmap(QPixmap());
            c->pic->setText(u"no flag"_s);
        } else {
            c->pic->setText(QString());
            c->pic->setPixmap(
                QPixmap::fromImage(*img).scaled(W + 28, H + 18, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        const bool dirty = e && e->dirty;
        c->state->setText((is_cur ? u"being edited · "_s : QString()) +
                          (dirty               ? u"not set yet"_s
                           : !file_img         ? u"no flag file yet"_s
                           : pending           ? u"set, written on Apply"_s
                           : own               ? u"as in the mod"_s
                                               : u"the game's own"_s));
        c->state->setStyleSheet(u"color: %1; font-size: 11px;"_s.arg(dirty ? u"#ffb347"_s : theme::INK_FAINT));
        c->cell->setStyleSheet(u"#flagCell { background: %1; border: 3px solid %2; border-radius: 6px; }"_s.arg(
            is_cur ? u"#2a2414"_s : theme::PANEL, is_cur ? theme::GOLD : theme::LINE_SOFT));
        c->cap->setText(is_cur ? u"✎ %1"_s.arg(name) : name);
        c->cap->setStyleSheet(u"font-weight: 700; color: %1;"_s.arg(is_cur ? theme::GOLD : theme::INK));
        c->pic->setStyleSheet(u"border: 2px solid %1; background: %2;"_s.arg(is_cur ? theme::GOLD : theme::LINE,
                                                                             theme::BG));
        c->pic->setToolTip(u"gfx/flags/%1 — double-click to work on it"_s.arg(fn));
        c->tick->setVisible(!is_cur);
        c->edit->setEnabled(!is_cur);
        c->rm->setEnabled(file_img && (own || pending));
        c->rm->setToolTip(own || pending ? u"Take this flag's file out of the mod (on Apply, with a backup); what you "
                                           u"made of it here stays in the studio"_s
                          : file_img     ? u"This flag is the game's own — the mod has no file of its own to delete"_s
                                         : u"There is no such flag file"_s);
    }
    QStringList also;
    for (const auto &[s, c] : cells)
        if (s != cur && c.tick->isChecked())
            also << py::lower(_name(s));
    const QString nm = _name(cur);
    set_btn->setText(u"Set this flag as “%1”%2"_s.arg(nm, !also.isEmpty() ? u" — and as: "_s + also.join(u", "_s)
                                                                          : QString()));
    editing->setText(u"Working on the %1 flag"_s.arg(py::lower(nm)));
    editing->setStyleSheet(u"color: %1; font-weight: 700; font-size: 14px;"_s.arg(theme::GOLD));
    const moddata::Country *c = mod.countries.get(*tag);
    foot_title->setText(u"The flags of %1 — the lit one is being worked on; double-click another (or Edit) to work "
                        u"on that one. Each keeps what you did to it."_s.arg(c ? c->name : *tag));
}

void FlagStudio::_delete_flag(const QString &sfx)
{
    const QString fn = u"%1%2.tga"_s.arg(tag.value_or(QString()), sfx);
    if (Entry *e = _work().get(sfx)) {
        e->dirty = !e->design.shapes.empty() || e->design.base.has_value() || sfx == cur;
        e->crc.reset();
    }
    Q_EMIT flagDelete(u"%1: %2 flag deleted"_s.arg(tag.value_or(QString()), py::lower(_name(sfx))), {fn});
    warn->setText(u"%1 goes on Apply (Ctrl+Z in the main window brings it back).%2"_s.arg(
        fn, sfx.isEmpty() ? u" The game expects a normal flag for every nation — set a new one before you play."_s
                          : QString()));
}

void FlagStudio::_commit()
{
    if (!tag || tag->isEmpty())
        return;
    QStringList which{cur};
    for (const auto &[s, c] : cells)
        if (s != cur && c.tick->isChecked())
            which << s;
    warn->setText(QString());
    _stash();
    const QImage img = final_image(design());
    const quint32 crc = image_crc(img);
    OrderedMap<QString, QImage> files;
    for (const QString &s : which)
        files.insert(u"%1%2.tga"_s.arg(*tag, s), img);
    {                                                               // the shapes are kept: the flag can be edited later
        const QString d = designs_dir(mod);
        if (QDir().mkpath(d)) {
            const QByteArray text = design_json(design(), crc).toUtf8();
            for (const auto &[fn, _i] : files) {
                QFile f(pypath::join(d, {fn + u".json"_s}));
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    break;
                f.write(text);
            }
        }
    }
    OrderedMap<QString, Entry> &work = _work();
    for (const QString &s : which) {                                // the ones it was handed to now are this flag
        if (s == cur) {
            if (Entry *e = work.get(s)) {
                e->dirty = false;
                e->crc = crc;
            }
        } else {
            Entry e;
            e.design = _state();
            e.how = u"shapes"_s;
            e.crc = crc;
            work.insert(s, e);
        }
    }
    for (const auto &[s, c] : cells)
        c.tick->setChecked(false);
    QStringList names;
    for (const QString &s : which)
        names << py::lower(_name(s));
    bool all = true, any = false;
    for (const auto &[fn, _i] : files) {
        const bool there = fs::isfile(mod.p({u"gfx"_s, u"flags"_s, fn}));
        all = all && there;
        any = any || there;
    }
    const QString verb = all ? u"edited"_s : !any ? u"added"_s : u"added / edited"_s;
    Q_EMIT flagEdit(u"%1: flag %2 (%3)"_s.arg(*tag, verb, names.join(u", "_s)), files);
    _show_cells();
    info->setText(u"Set as: %1 — written to gfx/flags on Apply."_s.arg(names.join(u", "_s)));
}

}  // namespace flagstudio
