// prodpanel.py — see prodpanel.h
#include "ui/prodpanel.h"
#include "ui/drawtext.h"

#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/settings.h"
#include "core/pyjson.h"
#include "core/techdata.h"
#include "map/ddsimage.h"
#include "ui/techpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringListModel>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using production::Goods;
using production::KIND_NAME;
using production::KINDS;
using production::NEEDS;
using production::num;
using production::pretty;
using script::Node;
using techpanel::ValueField;
using widgets::label;
using widgets::Section;

namespace prodpanel {

const double CARD_W = 256, GOOD_W = 184, GOOD_H = 44, NEED_W = 300;
const double SLOT = 30, BSLOT = 26;                  // height of one input slot on a card / on a building
const double FOOT = 24;                              // the money strip at the bottom of a production card
const OrderedMap<QString, std::pair<Rgb, Rgb>> KIND_RGB = {
    {u"rgo"_s, {{178, 204, 136}, {134, 166, 96}}}, {u"artisan"_s, {{170, 196, 232}, {120, 152, 204}}},
    {u"factory"_s, {{232, 168, 146}, {196, 118, 98}}}, {u"building"_s, {{186, 192, 204}, {138, 146, 162}}},
    {u"need"_s, {{214, 186, 236}, {168, 134, 206}}}};
const OrderedMap<QString, Rgb> NEED_KIND_RGB = {
    {u"life_needs"_s, {120, 196, 96}}, {u"everyday_needs"_s, {160, 128, 224}}, {u"luxury_needs"_s, {230, 176, 60}}};
const Rgb BOOST_RGB = {238, 70, 200};                // boosts: their own colour, unlike any good or card
const Rgb EFF_RGB = {50, 214, 255};                  // efficiency goods: their own colour too
const QString WHATIF_COL = u"#f0a030"_s;             // what-if prices (amber): only for the sums, never written
static const std::vector<std::pair<QString, QString>> FILTERS = {
    {u"rgo"_s, u"RGOs"_s}, {u"artisan"_s, u"Artisans"_s}, {u"factory"_s, u"Factories"_s},
    {u"building"_s, u"Buildings"_s}, {u"need"_s, u"Pop needs"_s}};
const Cid ALL = {u"all"_s, u"*"_s, 0};

namespace {

QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }
Rgb rgb_of(const QColor &c) { return {c.red(), c.green(), c.blue()}; }
QString css_rgb(const Rgb &c)
{
    return u"rgb(%1,%2,%3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}
QFont font_px(const QFont &base_font, int px, bool bold = true)
{
    QFont f(base_font);
    f.setBold(bold);
    f.setPixelSize(px);
    return f;
}
bool is(const Node &c, const QString &k) { return c.has_key && c.key == k; }
std::pair<QString, QString> pk(const Cid &n) { return {n.kind, n.key}; }
Cid from_pk(const std::pair<QString, QString> &p) { return {p.first, p.second, 0}; }
QString gfmt(double x) { return py::fmt_g(x); }   // "%g" % x
// a node in a path (the Explorer's unfolded branches): its kind and key
QString path_el(const Cid &n) { return n.kind + QChar(0x1f) + n.key; }
QString cid_key(const Cid &n) { return n.kind + QChar(0x1f) + n.key + QChar(0x1f) + QString::number(n.n); }
QString nk(std::initializer_list<QString> parts) { return QStringList(parts).join(QChar(0x1e)); }   // a net's key
const NodeInfo EMPTY_INFO;
QPushButton *x_button()
{
    auto *x = new QPushButton(u"✕"_s);
    x->setFixedWidth(28);
    x->setStyleSheet(u"QPushButton { padding: 0px; }"_s);
    return x;
}
std::vector<QString> keys_of(const QStringList &l) { return std::vector<QString>(l.begin(), l.end()); }

}  // namespace

QColor bright(const Rgb &rgb)
{
    // A good's colour made visible on the dark chart: dark colours (coal's navy, oil's black) are lifted.
    const QColor c = qc(rgb);
    float h, sat, v, a;
    c.getHsvF(&h, &sat, &v, &a);
    if (sat < 0.15)                                  // black / grey: a light grey
        return QColor::fromHsvF(0, 0, float(std::max<double>(v, 0.78)));
    if (v < 0.6)                                     // dark (coal's navy …): a light, softer version of it
        return QColor::fromHsvF(float(std::max<double>(h, 0.0)), float(std::min<double>(sat, 0.55)), 0.95f);
    return QColor::fromHsvF(float(std::max<double>(h, 0.0)), float(std::min(1.0, std::max<double>(sat, 0.35))),
                            float(std::max<double>(v, 0.82)));
}

int header_h(const QString &name, double width)
{
    // The name band on top of a card: one line, or two for a long name (never cut off).
    return name.size() <= static_cast<qsizetype>(py::trunc_int(20 * width / CARD_W)) ? 28 : 48;
}

std::vector<double> body_slots(double top, double bottom, long long n)
{
    // y offsets of n input slots evenly spread between top and bottom.
    std::vector<double> out;
    for (long long i = 0; i < n; ++i)
        out.push_back(top + (bottom - top) * double(i + 1) / double(n + 1));
    return out;
}

QString money(double v)
{
    const double a = std::fabs(v);
    const QString s = a >= 1e9 ? py::fmt_g(a / 1e9, 3) + u" bn"_s
                      : a >= 1e6 ? py::fmt_g(a / 1e6, 3) + u"M"_s
                      : a >= 10000 ? py::fmt_f(a / 1000, 0) + u"k"_s
                      : a >= 1000 ? py::fmt_f(a / 1000, 1) + u"k"_s
                      : a >= 100 ? py::fmt_f(a, 0)
                      : a >= 10 ? py::fmt_f(a, 1) : py::fmt_f(a, 2);
    return (v < 0 ? u"-"_s : QString()) + s;
}

QString amt(double a)
{
    // An amount on a card: 0.25, 200, 1.5M, 1 bn (never 1e+09).
    return std::fabs(a) >= 1e6 ? money(a) : gfmt(a);
}

QString nid_str(const Cid &n) { return n.kind + u"|"_s + n.key; }

Cid base(const Cid &n) { return {n.kind, n.key, 0}; }

Cid str_nid(const QString &s)
{
    const qsizetype i = s.indexOf(u'|');
    if (i >= 0)
        return {s.left(i), s.mid(i + 1), 0};
    return {u"prod"_s, s, 0};                        // an older setting: a production's key
}

static QHash<QString, QPixmap> _goods_icons(const moddata::Mod &mod)
{
    // {good: QPixmap} from the game's goods icon strip (GFX_resources — frame n = the n-th good of goods.txt).
    QHash<QString, QPixmap> out;
    try {
        auto [tex, frames] = ddsimage::_sprite(mod, u"GFX_resources"_s);
        if (tex) {
            const QString t = QString(*tex).replace(u"\\\\"_s, u"/"_s).replace(u"\\"_s, u"/"_s);
            const std::optional<QImage> img = ddsimage::load_texture(mod.p(t.split(u'/')));
            if (img && !img->isNull()) {
                std::vector<QString> goods;
                for (const auto &kv : mod.goods)
                    goods.push_back(kv.first);
                if (!frames)
                    frames = std::max<long long>(1, img->width() / std::max(1, img->height()));
                const long long fw = img->width() / frames;
                const long long first = frames > static_cast<long long>(goods.size()) ? 1 : 0;   // frame 0 is empty
                for (size_t i = 0; i < goods.size(); ++i) {
                    const long long f = static_cast<long long>(i) + first;
                    if (f < frames)
                        out.insert(goods[i], QPixmap::fromImage(img->copy(int(f * fw), 0, int(fw), img->height())));
                }
            }
        }
    } catch (const std::exception &) {
        out.clear();
    }
    return out;
}

const QHash<QString, QPixmap> &goods_icons(const moddata::Mod &mod)
{
    static std::map<const moddata::Mod *, QHash<QString, QPixmap>> cache;   // (Python: kept on the mod)
    auto it = cache.find(&mod);
    if (it == cache.end())
        it = cache.emplace(&mod, _goods_icons(mod)).first;
    return it->second;
}

// ---------------------------------------------------------------------------------------------- the chart
ProdChart::ProdChart(moddata::Mod &mod, QWidget *parent) : QWidget(parent), mod(mod)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

const NodeInfo &ProdChart::_info(const Cid &n) const
{
    const NodeInfo *d = info.get(n);
    return d ? *d : EMPTY_INFO;
}

void ProdChart::set_graph(std::shared_ptr<const prodtree::Scene> lay_, const OrderedMap<Cid, NodeInfo> &info_,
                          const std::vector<Meta> &meta_, const OrderedMap<Cid, QRectF> &ghosts_,
                          const OrderedMap<Cid, QRectF> &parked_at_, const std::optional<QRectF> &park_area_)
{
    lay = std::move(lay_);
    info = info_;
    meta = meta_;
    ghosts = ghosts_;
    parked_at = parked_at_;
    park_area = park_area_;
    nodes.clear();
    for (const auto &[n, r] : lay->rects)
        if (!ghosts.contains(n))
            nodes.insert(n, QRectF(r[0], r[1], r[2], r[3]));
    icons = goods_icons(mod);
    // line pieces as paths (the trunk of each net; each branch into a card), and a grid for hovering
    _paths.clear();
    _seg.clear();
    _grid.clear();
    bout.clear();
    for (const auto &[ni, lst] : lay->pieces) {
        Paths ps;
        for (const prodtree::Piece &pc : lst) {
            QPainterPath *path = &ps.trunk;
            if (pc.tgt) {
                const std::pair<Cid, QString> k{*pc.tgt, pc.style};
                auto f = std::find_if(ps.tg.begin(), ps.tg.end(), [&](const auto &e) { return e.first == k; });
                if (f == ps.tg.end()) {
                    ps.tg.emplace_back(k, QPainterPath());
                    f = ps.tg.end() - 1;
                }
                path = &f->second;
            }
            path->moveTo(pc.x1, pc.y1);
            path->lineTo(pc.x2, pc.y2);
            const long long i = static_cast<long long>(_seg.size());
            _seg.emplace_back(pc.x1, pc.y1, pc.x2, pc.y2, ni);
            const long long gx0 = py::trunc_int(py::floordiv(std::min(pc.x1, pc.x2), 200.0));
            const long long gx1 = py::trunc_int(py::floordiv(std::max(pc.x1, pc.x2), 200.0));
            const long long gy0 = py::trunc_int(py::floordiv(std::min(pc.y1, pc.y2), 200.0));
            const long long gy1 = py::trunc_int(py::floordiv(std::max(pc.y1, pc.y2), 200.0));
            for (long long gx = gx0; gx <= gx1; ++gx)
                for (long long gy = gy0; gy <= gy1; ++gy)
                    _grid[{gx, gy}].push_back(i);
        }
        _paths[ni] = std::move(ps);
    }
    for (const Meta &m : meta)
        for (const auto &[s, port] : m.src)
            if (port == u"bout")
                bout.insert(s);
    if (hover && hover->net && hover->ni >= static_cast<long long>(meta.size()))
        hover.reset();
    if (!_fitted && !nodes.isEmpty() && width() > 100)
        fit();
    update();
}

void ProdChart::fit()
{
    if (nodes.isEmpty())
        return;
    std::vector<QRectF> rects;
    for (const auto &kv : nodes)
        rects.push_back(kv.second);
    for (const auto &kv : ghosts)
        rects.push_back(kv.second);
    if (park_area)
        rects.push_back(*park_area);
    if (lay)
        for (const prodtree::Text &t : lay->texts)
            rects.emplace_back(t.x, t.y, t.w, double(t.px + 12));
    double x0 = rects[0].left(), y0 = rects[0].top(), x1 = rects[0].right(), y1 = rects[0].bottom();
    for (const QRectF &r : rects) {
        x0 = std::min(x0, r.left());
        y0 = std::min(y0, r.top());
        x1 = std::max(x1, r.right());
        y1 = std::max(y1, r.bottom());
    }
    const double w = x1 - x0 + 120, h = y1 - y0 + 140;
    zoom = std::max(0.03, std::min(1.0, std::min(double(width()) / w, double(height()) / h)));
    off = QPointF(30 - x0 * zoom, 30 - y0 * zoom);
    _fitted = true;
    update();
}

void ProdChart::center_on(const Cid &node)
{
    const QRectF *r = nodes.get(node);
    if (!r)                                          // the tree / Explorer: its first copy
        for (const auto &[n, rr] : nodes)
            if (base(n) == base(node)) {
                r = &rr;
                break;
            }
    if (!r)
        return;
    zoom = std::max(zoom, 0.8);
    off = QPointF(width() / 2.0 - r->center().x() * zoom, height() / 2.0 - r->center().y() * zoom);
    update();
}

void ProdChart::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    if (!_fitted && !nodes.isEmpty())
        fit();
}

QPointF ProdChart::_scene(const QPointF &pos) const
{
    return QPointF((pos.x() - off.x()) / zoom, (pos.y() - off.y()) / zoom);
}

std::optional<QRectF> ProdChart::_plus_box(const Cid &n, const QRectF &r) const
{
    // The ＋ / − button of a card that can unfold (outside it: left or right of its name band).
    const QString &side = _info(n).plus;
    if (side.isEmpty())
        return std::nullopt;
    const double cx = side == u"left" ? r.left() - 24 : r.right() + 26;
    return QRectF(cx - 17, r.top() + 1, 34, 34);
}

Hit ProdChart::_hit(const QPointF &pos) const
{
    const QPointF p = _scene(pos);
    for (const auto &[n, r] : nodes) {
        const auto b = _plus_box(n, r);
        if (b && b->contains(p))
            return {u"plus"_s, n};
    }
    for (const auto &[k, r] : parked_at)
        if (r.contains(p))
            return {u"parked"_s, k};
    for (const auto &[n, r] : nodes)
        if (r.contains(p))
            return {u"node"_s, n};
    return {};
}

std::optional<long long> ProdChart::_hit_line(const QPointF &pos) const
{
    const QPointF p = _scene(pos);
    const double tol = std::max(5.0, 7.0 / zoom);
    std::optional<long long> best;
    double bd = tol;
    const long long gx = py::trunc_int(py::floordiv(p.x(), 200.0)), gy = py::trunc_int(py::floordiv(p.y(), 200.0));
    QSet<long long> seen;
    for (long long dx : {-1, 0, 1})
        for (long long dy : {-1, 0, 1}) {
            auto it = _grid.find({gx + dx, gy + dy});
            if (it == _grid.end())
                continue;
            for (long long i : *it) {
                if (seen.contains(i))
                    continue;
                seen.insert(i);
                const auto &[x0, y0, x1, y1, ni] = _seg[static_cast<size_t>(i)];   // the piece's two ends (any angle)
                const double vx = x1 - x0, vy = y1 - y0;
                const double ll = vx * vx + vy * vy;
                const double t = ll ? std::max(0.0, std::min(1.0, ((p.x() - x0) * vx + (p.y() - y0) * vy) / ll)) : 0.0;
                const double cx = x0 + vx * t, cy = y0 + vy * t;
                const double d = std::pow(std::pow(p.x() - cx, 2.0) + std::pow(p.y() - cy, 2.0), 0.5);
                if (d < bd) {
                    best = ni;
                    bd = d;
                }
            }
        }
    return best;
}

QRectF ProdChart::_park_box(const QRectF &r) { return QRectF(r.left() + 6, r.top() + 6, 20, 20); }

// --- mouse
void ProdChart::wheelEvent(QWheelEvent *ev)
{
    const double f = ev->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const double z = std::max(0.03, std::min(3.0, zoom * f));
    const QPointF p = ev->position();
    const QPointF sc = _scene(p);
    zoom = z;
    off = QPointF(p.x() - sc.x() * z, p.y() - sc.y() * z);
    update();
}

void ProdChart::mousePressEvent(QMouseEvent *ev)
{
    _press = Press{ev->position(), off, false, ev->button(), _hit(ev->position())};
}

void ProdChart::mouseMoveEvent(QMouseEvent *ev)
{
    if (_press && (ev->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton))) {
        const QPointF d = ev->position() - _press->pos;
        if (d.manhattanLength() > 6 || _press->moved) {
            _press->moved = true;
            off = _press->off + d;
            setCursor(Qt::ClosedHandCursor);
            update();
        }
        return;
    }
    const Hit n = _hit(ev->position());
    if (n.what == u"plus") {
        const NodeInfo &d = _info(n.n);
        QToolTip::showText(ev->globalPosition().toPoint(),
                           d.open ? u"Fold it back"_s
                           : d.plus == u"left" ? u"Unfold what makes it (its own inputs, further left)"_s
                                               : u"Unfold what uses it (further right)"_s,
                           this);
        setCursor(Qt::PointingHandCursor);
        if (hover) {
            hover.reset();
            update();
        }
        return;
    }
    std::optional<Hover> hov;
    if (n && n.what != u"parked")
        hov = Hover{false, 0, n.n};
    if (!n) {
        const auto ni = _hit_line(ev->position());
        if (ni)
            hov = Hover{true, *ni, {}};
    }
    if (hov != hover) {
        hover = hov;
        update();
    }
    setCursor(n || hov ? Qt::PointingHandCursor : Qt::ArrowCursor);
    const QPoint gp = ev->globalPosition().toPoint();
    if (n) {
        if (n.what == u"parked") {
            const NodeInfo *d = info.get(n.n);
            QToolTip::showText(gp, u"%1 — parked: click to put it back into the flowchart"_s.arg(d ? d->name : n.n.key),
                               this);
        } else if (n.n.kind != u"good" && _park_box(*nodes.get(n.n)).contains(_scene(ev->position()))) {
            QToolTip::showText(gp, u"Park it: out of the chart, up into the holding area (click it there to bring it back)"_s,
                               this);
        } else {
            QToolTip::showText(gp, _tip(n.n), this);
        }
    } else if (hov) {
        QToolTip::showText(gp, meta[static_cast<size_t>(hov->ni)].title, this);
    } else {
        QToolTip::hideText();
    }
}

void ProdChart::leaveEvent(QEvent *ev)
{
    if (hover) {
        hover.reset();
        update();
    }
    QWidget::leaveEvent(ev);
}

void ProdChart::mouseDoubleClickEvent(QMouseEvent *ev)
{
    const Hit n = _hit(ev->position());
    if (n.what == u"node") {
        Q_EMIT isolated(base(n.n));
        return;
    }
    const QPointF p = _scene(ev->position());
    for (const auto &[k, r] : ghosts)                  // a parked card's empty place: back in
        if (r.contains(p)) {
            Q_EMIT unparked(k);
            return;
        }
}

void ProdChart::mouseReleaseEvent(QMouseEvent *ev)
{
    if (_press && !_press->moved) {
        const Hit n = _hit(ev->position());
        if (n.what == u"plus")
            Q_EMIT expanded(n.n);
        else if (n.what == u"parked")
            Q_EMIT unparked(n.n);
        else if (n && n.n.kind != u"good" &&
                 (_press->button == Qt::RightButton || _park_box(*nodes.get(n.n)).contains(_scene(ev->position()))))
            Q_EMIT parked(base(n.n));
        else if (n)
            Q_EMIT picked(n.n.kind, n.n.key, bool(ev->modifiers() & Qt::ControlModifier));
        else
            Q_EMIT picked(QString(), QString(), false);
    }
    _press.reset();
    setCursor(Qt::ArrowCursor);
}

QString ProdChart::_gname(const QString &g) const
{
    const moddata::Good *e = mod.goods.get(g);
    return e ? e->name : pretty(g);
}

QString ProdChart::_tip(const Cid &n) const
{
    const NodeInfo &d = _info(n);
    if (n.kind == u"good")
        return u"%1 — price %2%3\n%4"_s.arg(_gname(n.key), money(d.cost.value_or(0)), QString(), d.tip);
    return u"%1\n%2"_s.arg(info.contains(n) ? d.name : n.key, d.tip);
}

// --- painting
std::tuple<int, const QSet<Cid> *, bool> ProdChart::_net_state(long long ni) const
{
    // (trunk alpha, {target: alpha}) — what a hovered / selected line shows.
    const Meta &m = meta[static_cast<size_t>(ni)];
    if (hover) {
        const bool on = (hover->net && hover->ni == ni) || (!hover->net && m.members.contains(hover->n));
        return {on ? 255 : 26, nullptr, on};
    }
    if (focus.isEmpty())
        return {215, nullptr, false};
    bool src_in = false;
    for (const auto &[s, _p] : m.src)
        src_in = src_in || focus.contains(s);
    bool tgt_in = false;
    for (const Cid &t : m.tgts)
        tgt_in = tgt_in || focus.contains(t);
    const bool on = src_in && tgt_in;
    return {on ? 250 : 24, &focus, on};
}

void ProdChart::paintEvent(QPaintEvent *)
{
    static const prodtree::Scene none;
    const prodtree::Scene &L = lay ? *lay : none;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(34, 30, 26));
    p.translate(off);
    p.scale(zoom, zoom);
    const double lw = std::min(7.0, std::max(3.0, 1.3 / zoom));   // lines stay visible zoomed out
    const double RAD = 180.0 / std::numbers::pi;     // (math.degrees)
    for (const auto &[a0, a1, r_in, r_out, rgb] : L.wedges) {   // the needs sectors: soft colour
        QPainterPath path;
        const double d0 = -(a0 * RAD), span = -((a1 - a0) * RAD);
        path.arcMoveTo(QRectF(-r_out, -r_out, 2 * r_out, 2 * r_out), d0);
        path.arcTo(QRectF(-r_out, -r_out, 2 * r_out, 2 * r_out), d0, span);
        path.arcTo(QRectF(-r_in, -r_in, 2 * r_in, 2 * r_in), d0 + span, -span);
        path.closeSubpath();
        QColor c = qc(rgb);
        c.setAlpha(34);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(path);
    }
    for (const auto &g : L.guides) {                 // … and a clear border between them
        const QPointF a(g[0], g[1]), b(g[2], g[3]);
        p.setPen(QPen(QColor(16, 12, 8), 9, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setPen(QPen(QColor(196, 180, 146), 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        for (const QPointF &q : {a, b}) {
            p.setPen(QPen(QColor(16, 12, 8), 2));
            p.setBrush(QColor(196, 180, 146));
            p.drawEllipse(q, 9, 9);
        }
    }
    for (const prodtree::Text &tt : L.texts) {
        const QFont f = font_px(font(), int(tt.px));
        p.setFont(f);
        p.setPen(qc(tt.rgb));
        const Qt::Alignment al = tt.align == u"c" ? Qt::AlignHCenter : Qt::AlignLeft;
        drawtext::draw(p, QRectF(tt.x, tt.y, std::max(tt.w, 80.0), double(tt.px + 12)), al | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(tt.text, Qt::ElideRight, int(py::trunc_int(std::max(tt.w, 80.0)))));
    }
    std::map<long long, std::tuple<int, const QSet<Cid> *, bool>> states;
    std::vector<long long> order;
    for (long long ni = 0; ni < static_cast<long long>(meta.size()); ++ni)
        order.push_back(ni);
    std::stable_sort(order.begin(), order.end(),      // lit lines on top
                     [&](long long a, long long b) { return !std::get<2>(_net_state(a)) && std::get<2>(_net_state(b)); });
    for (long long ni : order) {
        auto pit = _paths.find(ni);
        if (pit == _paths.end())
            continue;
        const Meta &m = meta[static_cast<size_t>(ni)];
        const auto st3 = _net_state(ni);
        const auto &[alpha, f, on] = st3;
        states[ni] = st3;
        const Paths &ps = pit->second;
        const double w = lw * (on ? 1.35 : 1.0);
        const QColor tcol = _style_color(m.trunk, m);
        // dark casing first (a line crossing over another stays readable), then the colour
        QPen casing(QColor(16, 12, 8, std::min(230, alpha)), w + 3.2);
        casing.setCapStyle(Qt::RoundCap);
        casing.setJoinStyle(Qt::RoundJoin);
        p.setPen(casing);
        p.setBrush(Qt::NoBrush);
        p.drawPath(ps.trunk);
        for (const auto &e : ps.tg)
            p.drawPath(e.second);
        _set_pen(p, m.trunk, tcol, alpha, w);
        p.drawPath(ps.trunk);
        for (const auto &[ts, path] : ps.tg) {
            const auto &[t, st] = ts;
            const int a = f == nullptr ? alpha : (on && f->contains(t) ? 250 : 24);
            _set_pen(p, st, _style_color(st, m), a, w);
            p.drawPath(path);
            if (st == u"flowboost") {                // input and boost in one: a magenta stripe along it
                _set_pen(p, u"boost"_s, qc(BOOST_RGB), a, w * 0.55);
                p.drawPath(path);
            }
        }
        p.setPen(Qt::NoPen);
        if (const auto *dots = L.dots.get(ni))
            for (const auto &[x, y] : *dots) {
                p.setBrush(QColor(16, 12, 8, alpha));
                p.drawEllipse(QPointF(x, y), w * 1.25 + 1.6, w * 1.25 + 1.6);
                QColor c(tcol);
                c.setAlpha(alpha);
                p.setBrush(c);
                p.drawEllipse(QPointF(x, y), w * 1.25, w * 1.25);
            }
    }
    // a small icon on long runs: which good this lane carries
    if (zoom >= 0.42) {
        for (const auto &[ni, lst] : L.labels) {
            const OptStr &g = meta[static_cast<size_t>(ni)].good;
            if (!g || g->isEmpty() || !states.count(ni))
                continue;
            const int alpha = std::get<0>(states[ni]);
            p.setOpacity(alpha > 100 ? 1.0 : 0.18);
            for (const auto &[x, y] : lst) {
                const QRectF rr(x - 11, y - 11, 22, 22);
                p.setPen(QPen(QColor(16, 12, 8), 2));
                p.setBrush(QColor(236, 228, 208));
                p.drawEllipse(rr);
                _icon(p, *g, rr.adjusted(3, 3, -3, -3));
            }
        }
        p.setOpacity(1.0);
    }
    for (const auto &[k, r] : ghosts) {              // parked: a dashed outline where it was
        QPen pen(QColor(150, 140, 120, 150), 2);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(QColor(255, 255, 255, 10));
        p.drawRoundedRect(r, 6, 6);
        p.setFont(font_px(font(), 14, false));
        p.setPen(QColor(150, 140, 120, 190));
        const NodeInfo *d = info.get(k);
        drawtext::draw(p, r.adjusted(8, 6, -8, -6), Qt::AlignCenter | Qt::TextWordWrap,
                   u"parked: %1\n(double-click: back)"_s.arg(d ? d->name : k.key));
    }
    if (park_area && !parked_at.isEmpty()) {         // the Parked area above the flowchart
        const QRectF a = *park_area;
        QPen pen(QColor(150, 140, 120, 120), 2);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(QColor(255, 255, 255, 12));
        p.drawRoundedRect(a, 10, 10);
        p.setFont(font_px(font(), 18));
        p.setPen(QColor(200, 186, 150));
        drawtext::draw(p, QRectF(a.left() + 16, a.top() + 8, a.width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
                   u"Parked — click one to put it back"_s);
        for (const auto &[k, r] : parked_at) {
            _draw_node(p, k, r, false);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 38, 34, 150));     // greyed out
            p.drawRoundedRect(r, 6, 6);
        }
    }
    QSet<Cid> lit;
    if (hover) {
        if (hover->net) {
            lit = meta[static_cast<size_t>(hover->ni)].members;
        } else {
            lit.insert(hover->n);
            for (const Meta &m : meta)
                if (m.members.contains(hover->n))
                    lit.unite(m.members);
        }
    }
    if (sel) {
        const Cid sel_b = base(*sel);
        for (const auto &[n, r] : nodes)
            if (base(n) == sel_b || multi.contains(base(n))) {
                for (const auto &[grow, alpha] : {std::pair{46, 45}, {32, 85}, {20, 150}, {10, 230}}) {   // a bright halo
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(255, 206, 84, alpha));
                    p.drawRoundedRect(r.adjusted(-grow, -grow, grow, grow), 12 + grow / 2.0, 12 + grow / 2.0);
                }
                p.setPen(QPen(QColor(255, 240, 190), 4));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(-10, -10, 10, 10), 16, 16);
            }
    }
    for (const auto &[n, r] : nodes) {
        const bool dim = hover ? !lit.contains(n) : (!focus.isEmpty() && !focus.contains(n));
        _draw_node(p, n, r, dim);
        const NodeInfo &d = _info(n);
        if (!d.badge.isEmpty())
            _badge(p, r, d.badge, dim, d.badge_left);
        const auto pb = _plus_box(n, r);
        if (pb)
            _plus(p, *pb, d.open, dim);
    }
    // arrowheads last: on the card's edge
    for (const auto &[ni, lst] : L.arrows) {
        if (!states.count(ni))
            continue;
        const auto &[alpha, f, on] = states[ni];
        const Meta &m = meta[static_cast<size_t>(ni)];
        for (const prodtree::Arrow &ar : lst) {
            if (!ar.tgt || !nodes.contains(*ar.tgt))
                continue;
            const int a = f == nullptr ? alpha : (on && f->contains(*ar.tgt) ? 250 : 24);
            QColor c = _style_color(ar.style, m);
            c.setAlpha(a);
            const double s = 9.0 + lw * 0.6;
            const double x = ar.x, y = ar.y;
            QList<QPointF> pts;
            if (const double *ang = std::get_if<double>(&ar.dir)) {   // any angle (the needs epicentre)
                const double ca = std::cos(*ang), sa = std::sin(*ang);
                for (const auto &[dx, dy] : {std::pair{0.0, 0.0}, {-s * 1.25, -s * 0.75}, {-s * 1.25, s * 0.75}})
                    pts << QPointF(x + dx * ca - dy * sa, y + dx * sa + dy * ca);
            } else if (std::get<QString>(ar.dir) == u"right") {
                pts << QPointF(x, y) << QPointF(x - s * 1.25, y - s * 0.75) << QPointF(x - s * 1.25, y + s * 0.75);
            } else if (std::get<QString>(ar.dir) == u"down") {
                pts << QPointF(x, y) << QPointF(x - s * 0.75, y - s * 1.25) << QPointF(x + s * 0.75, y - s * 1.25);
            } else {
                pts << QPointF(x, y) << QPointF(x - s * 0.75, y + s * 1.25) << QPointF(x + s * 0.75, y + s * 1.25);
            }
            p.setPen(QPen(QColor(16, 12, 8, a), 1.6));
            p.setBrush(c);
            p.drawPolygon(pts.constData(), int(pts.size()));
        }
    }
    // boost values: a magenta tag on the line
    const QFont f = font_px(font(), 15);
    const QFontMetrics fm(f);
    p.setFont(f);
    for (const auto &[x, y, text] : L.pills) {
        const double w = fm.horizontalAdvance(text) + 16;
        const QRectF rr(x - w / 2, y - 13, w, 26);
        p.setPen(QPen(QColor(16, 12, 8), 2));
        p.setBrush(qc(BOOST_RGB));
        p.drawRoundedRect(rr, 12, 12);
        p.setPen(QColor(255, 255, 255));
        drawtext::draw(p, rr, Qt::AlignCenter, text);
    }
    p.end();
}

QColor ProdChart::_style_color(const QString &st, const Meta &m) const
{
    if (st == u"boost")
        return qc(BOOST_RGB);
    if (st == u"eff")
        return qc(EFF_RGB);
    return bright(m.rgb);
}

void ProdChart::_set_pen(QPainter &p, const QString &st, const QColor &col, int alpha, double w)
{
    QColor c(col);
    c.setAlpha(alpha);
    QPen pen(c, w);
    pen.setCapStyle(st == u"flow" || st == u"trunk" ? Qt::RoundCap : Qt::FlatCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (st == u"boost")
        pen.setDashPattern({1.4, 1.2});
    else if (st == u"eff")
        pen.setDashPattern({3.0, 1.6});
    p.setPen(pen);
}

void ProdChart::_icon(QPainter &p, const QString &g, const QRectF &rect) const
{
    auto it = icons.find(g);
    if (it != icons.end()) {
        p.drawPixmap(rect.toRect(), *it);
    } else {
        const moddata::Good *e = mod.goods.get(g);
        p.setBrush(e ? qc(e->color) : QColor(128, 128, 128));
        p.setPen(QPen(QColor(40, 30, 20), 1));
        p.drawEllipse(rect.adjusted(3, 3, -3, -3));
    }
}

void ProdChart::_plus(QPainter &p, const QRectF &b, bool is_open, bool dim) const
{
    // The ＋ (unfold) / − (fold) button: gold, big enough to hit.
    p.setOpacity(dim ? 0.3 : 1.0);
    p.setPen(QPen(QColor(16, 12, 8), 2.5));
    p.setBrush(is_open ? QColor(70, 60, 44) : QColor(theme::GOLD));
    p.drawEllipse(b);
    p.setPen(QPen(is_open ? QColor(theme::GOLD) : QColor(28, 20, 12), 4, Qt::SolidLine, Qt::RoundCap));
    const QPointF c = b.center();
    p.drawLine(QPointF(c.x() - 8, c.y()), QPointF(c.x() + 8, c.y()));
    if (!is_open)
        p.drawLine(QPointF(c.x(), c.y() - 8), QPointF(c.x(), c.y() + 8));
    p.setOpacity(1.0);
}

void ProdChart::_badge(QPainter &p, const QRectF &r, const QString &text, bool dim, bool left) const
{
    // A small tag on a card's top edge.
    const QFont f = font_px(font(), 13);
    const double bw = QFontMetrics(f).horizontalAdvance(text) + 18;
    const QRectF rr(left ? r.left() + 34 : r.right() - bw - 10, r.top() - 13, bw, 24);
    p.setOpacity(dim ? 0.22 : 1.0);
    p.setPen(QPen(QColor(16, 12, 8), 2));
    p.setBrush(QColor(70, 60, 44));
    p.drawRoundedRect(rr, 11, 11);
    p.setFont(f);
    p.setPen(QColor(theme::GOLD));
    drawtext::draw(p, rr, Qt::AlignCenter, text);
    p.setOpacity(1.0);
}

void ProdChart::_draw_node(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    const QString &look = _info(n).look;
    if (look == u"needsec")
        _draw_needsec(p, n, r, dim);
    else if (look == u"chip")
        _draw_chip(p, n, r, dim);
    else if (look == u"list")
        _draw_list(p, n, r, dim);
    else if (n.kind == u"good")
        _draw_good(p, n, r, dim);
    else if (n.kind == u"prod")
        _draw_card(p, n, r, dim);
}

void ProdChart::_draw_good(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    const QString &g = n.key;
    p.setOpacity(dim ? 0.22 : 1.0);
    const bool sel_ = sel && *sel == Cid{u"good"_s, g, 0};
    const NodeInfo &d = _info(n);
    const QString &sub = d.sub;
    const QColor frame = !sub.isEmpty() && d.sub_rgb ? qc(*d.sub_rgb) : QColor(110, 98, 80);
    p.setPen(QPen(sel_ ? QColor(theme::GOLD) : frame, sel_ ? 4 : (!sub.isEmpty() ? 3 : 2)));
    p.setBrush(QColor(50, 46, 40));
    p.drawRoundedRect(r, 9, 9);
    const moddata::Good *e = mod.goods.get(g);
    const QColor col = bright(e ? e->color : Rgb{128, 128, 128});
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawRoundedRect(QRectF(r.left() + 3, r.top() + 5, 7, r.height() - 10), 2, 2);
    _icon(p, g, QRectF(r.left() + 14, r.top() + 6, 32, 32));
    const QFont f = font_px(font(), 15);
    p.setFont(f);
    p.setPen(QColor(238, 230, 212));
    const QFontMetrics fm(f);
    double pw = 0;
    if (d.cost) {
        const QFont f2 = font_px(font(), 12);
        pw = QFontMetrics(f2).horizontalAdvance(money(*d.cost)) + 12;
        p.setFont(f2);
        p.setPen(QColor(236, 200, 110));
        drawtext::draw(p, QRectF(r.right() - pw - 6, r.top(), pw, r.height()), Qt::AlignVCenter | Qt::AlignRight, money(*d.cost));
        p.setFont(f);
        p.setPen(QColor(238, 230, 212));
    }
    if (!sub.isEmpty()) {
        drawtext::draw(p, QRectF(r.left() + 52, r.top() + 4, r.width() - 58 - pw, 26), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(_gname(g), Qt::ElideRight, int(py::trunc_int(r.width() - 60 - pw))));
        p.setFont(font_px(font(), 13));
        p.setPen(frame.lighter(125));
        drawtext::draw(p, QRectF(r.left() + 52, r.top() + 30, r.width() - 58, 24), Qt::AlignVCenter | Qt::AlignLeft, sub);
    } else {
        drawtext::draw(p, QRectF(r.left() + 52, r.top(), r.width() - 58 - pw, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(_gname(g), Qt::ElideRight, int(py::trunc_int(r.width() - 60 - pw))));
    }
    p.setOpacity(1.0);
}

std::pair<double, QColor> ProdChart::_card_base(QPainter &p, const Cid &n, const QRectF &r, bool dim,
                                                const std::pair<Rgb, Rgb> &rgb, bool arrow) const
{
    // The card: its colour, the name band, the ◉ park button — returns (header height, dark colour).
    const NodeInfo *dp = info.get(n);
    const NodeInfo &d = dp ? *dp : EMPTY_INFO;
    const QString name = dp ? d.name : n.key;
    const double hdr = d.hdr.value_or(28);
    p.setOpacity(dim ? 0.22 : 1.0);
    const bool sel_ = (sel && *sel == base(n)) || multi.contains(base(n));
    const auto &[top, bot] = rgb;
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, qc(top));
    grad.setColorAt(1, qc(bot));
    const QColor dark(54, 42, 30);
    if (arrow) {
        const double ay = r.top() + d.out.value_or(hdr + (r.height() - hdr) / 2);
        p.setPen(QPen(dark, 1.5));
        p.setBrush(qc(bot));
        const QPointF tri[3] = {QPointF(r.right() - 2, ay - 22), QPointF(r.right() + 16, ay), QPointF(r.right() - 2, ay + 22)};
        p.drawPolygon(tri, 3);
    }
    p.setPen(QPen(sel_ ? QColor(theme::GOLD) : dark, sel_ ? 5 : 1.8));
    p.setBrush(QBrush(grad));
    p.drawRoundedRect(r, 6, 6);
    const QRectF band(r.left() + 2, r.top() + 2, r.width() - 4, hdr - 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawRoundedRect(band, 5, 5);
    _park_dot(p, r, dark);
    p.setFont(font_px(font(), 16));
    p.setPen(QColor(28, 20, 12));
    drawtext::draw(p, QRectF(r.left() + 32, r.top() + 3, r.width() - 40, hdr - 4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
               name);
    return {hdr, dark};
}

void ProdChart::_slots(QPainter &p, const QRectF &r, const NodeInfo &d, const QColor &dark, double width, bool names) const
{
    // The input slots down the left edge: the good's icon, its amount (and name).
    const QFont small = font_px(font(), 12);
    const QFontMetrics fm(small);
    for (size_t i = 0; i < std::min(d.inputs.size(), d.slots.size()); ++i) {
        const auto &[g, txt] = d.inputs[i];
        const double y = r.top() + d.slots[i];
        const QRectF box(r.left() + 10, y - 12, 24, 24);
        p.setPen(QPen(dark, 1));
        p.setBrush(QColor(246, 238, 216));
        p.drawRect(box);
        _icon(p, g, box.adjusted(1, 1, -1, -1));
        p.setFont(small);
        p.setPen(QColor(40, 30, 20));
        if (names) {
            const QString t = u"%1  %2"_s.arg(_gname(g), txt);
            drawtext::draw(p, QRectF(box.right() + 5, y - 10, r.width() - 50, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(t, Qt::ElideRight, int(py::trunc_int(r.width() - 52))));
        } else {
            drawtext::draw(p, QRectF(box.right() + 3, y - 10, width, 20), Qt::AlignLeft | Qt::AlignVCenter, txt);
        }
    }
}

void ProdChart::_foot(QPainter &p, const QRectF &r, const NodeInfo &d) const
{
    // The money strip: inputs cost · output worth · what's left (green / red).
    if (!d.money)
        return;
    const QRectF fr(r.left() + 2, r.bottom() - FOOT, r.width() - 4, FOOT - 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 24, 18, 150));
    p.drawRoundedRect(fr, 4, 4);
    p.setFont(font_px(font(), 12));
    const auto [cin, cout] = *d.money;
    const double diff = cout - cin;
    p.setPen(QColor(236, 222, 190));
    drawtext::draw(p, fr.adjusted(8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, u"in %1 → out %2"_s.arg(money(cin), money(cout)));
    p.setPen(diff >= 0 ? QColor(120, 230, 120) : QColor(255, 120, 110));
    drawtext::draw(p, fr.adjusted(0, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, (diff >= 0 ? u"+"_s : QString()) + money(diff));
}

void ProdChart::_draw_card(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    // A production card in its kind's colour: its full name on a band across the top; input slots down the left edge;
    // the output big at the right with how much it makes right under it; the money strip at the bottom.
    const NodeInfo &d = _info(n);
    const auto *kr = KIND_RGB.get(d.kind);
    const auto [hdr, dark] = _card_base(p, n, r, dim, kr ? *kr : std::pair<Rgb, Rgb>{{210, 200, 170}, {180, 170, 140}});
    _slots(p, r, d, dark);
    const double foot = d.money ? FOOT : 0;
    const double body_top = r.top() + hdr, body_bot = r.bottom() - foot;
    const double ay = r.top() + d.out.value_or(hdr + (r.height() - hdr) / 2);
    const QFont small = font_px(font(), 12);
    const QRectF mid(r.left() + 86, body_top + 6, r.width() - 86 - 70, body_bot - body_top - 12);
    p.setFont(small);
    p.setPen(QColor(60, 44, 28));
    QString extra = KIND_NAME.value(d.kind, u"?"_s);
    if (d.bonuses)
        extra += u"\n★ %1 boost%2"_s.arg(d.bonuses).arg(d.bonuses == 1 ? u""_s : u"s"_s);
    drawtext::draw(p, mid, Qt::AlignCenter | Qt::TextWordWrap, extra);
    if (d.output && !d.output->isEmpty()) {
        const QRectF ob(r.right() - 62, ay - 30, 48, 48);
        p.setPen(QPen(dark, 1.4));
        p.setBrush(QColor(248, 242, 222));
        p.drawRect(ob);
        _icon(p, *d.output, ob.adjusted(2, 2, -2, -2));
        p.setFont(font_px(font(), 15));
        p.setPen(QColor(20, 92, 20));
        drawtext::draw(p, QRectF(ob.left() - 14, ob.bottom() + 1, ob.width() + 28, 20), Qt::AlignCenter, u"+"_s + gfmt(d.value));
    }
    if (bout.contains(n)) {                          // it boosts others (a building of it in the state)
        const double y = r.top() + d.bout.value_or(14);
        p.setPen(QPen(QColor(16, 12, 8), 2));
        p.setBrush(qc(BOOST_RGB));
        p.drawEllipse(QPointF(r.right(), y), 7, 7);
    }
    _foot(p, r, d);
    p.setOpacity(1.0);
}

QColor ProdChart::_band_frame(QPainter &p, const Cid &n, const QRectF &r, bool dim, const Rgb &top_rgb, const Rgb &bot_rgb) const
{
    p.setOpacity(dim ? 0.22 : 1.0);
    const bool sel_ = (sel && *sel == base(n)) || multi.contains(base(n));
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0, qc(top_rgb));
    grad.setColorAt(1, qc(bot_rgb));
    const QColor dark(54, 42, 30);
    p.setPen(QPen(sel_ ? QColor(theme::GOLD) : dark, sel_ ? 5 : 1.8));
    p.setBrush(QBrush(grad));
    p.drawRoundedRect(r, 6, 6);
    return dark;
}

void ProdChart::_park_dot(QPainter &p, const QRectF &r, const QColor &dark) const
{
    const QRectF pb = _park_box(r);
    p.setPen(QPen(dark, 1.4));
    p.setBrush(QColor(255, 255, 255, 140));
    p.drawEllipse(pb);
    p.setBrush(dark);
    p.drawEllipse(pb.adjusted(6, 6, -6, -6));
}

void ProdChart::_head(QPainter &p, const Cid &n, const QRectF &r, const QColor &dark, double h) const
{
    // The name band of a building / pop type card (in the pop type's colour) with its ◉ park button.
    const NodeInfo *dp = info.get(n);
    const NodeInfo &d = dp ? *dp : EMPTY_INFO;
    const QRectF band(r.left() + 2, r.top() + 2, r.width() - 4, h);
    p.setPen(Qt::NoPen);
    if (n.kind == u"need") {
        const QColor pc = qc(d.color.value_or(Rgb{160, 150, 190}));
        p.setBrush(QColor(int(py::trunc_int(pc.red() * 0.45 + 255 * 0.55)), int(py::trunc_int(pc.green() * 0.45 + 255 * 0.55)),
                          int(py::trunc_int(pc.blue() * 0.45 + 255 * 0.55))));
        p.drawRoundedRect(band, 5, 5);
        p.setBrush(pc);
        p.drawRoundedRect(QRectF(band.right() - 16, band.top() + 5, 10, band.height() - 10), 2, 2);
    } else {
        p.setBrush(QColor(255, 255, 255, 70));
        p.drawRoundedRect(band, 5, 5);
    }
    _park_dot(p, r, dark);
    const QFont f = font_px(font(), h > 26 ? 16 : 15);
    p.setFont(f);
    p.setPen(QColor(28, 20, 12));
    drawtext::draw(p, band.adjusted(30, 0, -22, 0), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(f).elidedText(dp ? d.name : n.key, Qt::ElideRight, int(py::trunc_int(band.width() - 54))));
}

std::pair<Rgb, Rgb> ProdChart::_frame_rgb(const Cid &n) const
{
    return n.kind == u"need" ? std::pair<Rgb, Rgb>{{236, 230, 216}, {214, 206, 190}} : *KIND_RGB.get(u"building"_s);
}

void ProdChart::_draw_needsec(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    // One kind of a pop type's needs (life / everyday / luxury) in the middle of the needs view.
    const NodeInfo &d = _info(n);
    const QColor c = qc(d.rgb.value_or(Rgb{150, 150, 150}));
    p.setOpacity(dim ? 0.22 : 1.0);
    const bool sel_ = sel && *sel == base(n);
    p.setPen(QPen(sel_ ? QColor(theme::GOLD) : c.darker(170), sel_ ? 5 : 3));
    p.setBrush(QColor(238, 232, 218));
    p.drawRoundedRect(r, 8, 8);
    const QRectF band(r.left() + 2, r.top() + 2, r.width() - 4, 34);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(band, 6, 6);
    QFont f = font_px(font(), 17);
    p.setFont(f);
    p.setPen(QColor(20, 16, 10));
    drawtext::draw(p, band.adjusted(12, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, d.title);
    f.setPixelSize(14);
    p.setFont(f);
    drawtext::draw(p, band.adjusted(12, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter, u"≈ %1"_s.arg(d.cost_text));
    const QFont small = font_px(font(), 13);
    const QFontMetrics fm(small);
    double y = r.top() + 44;
    const QColor dark(54, 42, 30);
    for (const auto &[g, txt] : d.goods) {
        const QRectF box(r.left() + 10, y + 2, 24, 24);
        p.setPen(QPen(dark, 1));
        p.setBrush(QColor(246, 238, 216));
        p.drawRect(box);
        _icon(p, g, box.adjusted(1, 1, -1, -1));
        p.setFont(small);
        p.setPen(QColor(40, 30, 20));
        const double aw = fm.horizontalAdvance(txt) + 8;
        drawtext::draw(p, QRectF(r.left() + 42, y, r.width() - 52 - aw, 28), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(_gname(g), Qt::ElideRight, int(py::trunc_int(r.width() - 52 - aw))));
        drawtext::draw(p, QRectF(r.left() + 42, y, r.width() - 52, 28), Qt::AlignRight | Qt::AlignVCenter, txt);
        y += 30;
    }
    if (!d.all.isEmpty()) {                          // the total of all three kinds
        p.setPen(QPen(c.darker(150), 2));
        p.drawLine(QPointF(r.left() + 10, r.bottom() - 32), QPointF(r.right() - 10, r.bottom() - 32));
        p.setFont(small);
        p.setPen(QColor(40, 30, 20));
        drawtext::draw(p, QRectF(r.left() + 12, r.bottom() - 30, r.width() - 24, 26), Qt::AlignRight | Qt::AlignVCenter,
                   u"All three needs ≈ %1"_s.arg(d.all));
    }
    p.setOpacity(1.0);
}

void ProdChart::_draw_chip(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    // A small card: a building or a pop type next to something — its name, the good its line brings at the left edge.
    const NodeInfo &d = _info(n);
    const auto [top, bot] = _frame_rgb(n);
    const QColor dark = _band_frame(p, n, r, dim, top, bot);
    _head(p, n, r, dark);
    _slots(p, r, d, dark);
    p.setFont(font_px(font(), 12));
    p.setPen(qc(d.sub_rgb.value_or(Rgb{60, 50, 40})));
    drawtext::draw(p, QRectF(r.left() + 92, r.top() + 30, r.width() - 100, r.height() - 34),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, d.sub);
    p.setOpacity(1.0);
}

void ProdChart::_draw_list(QPainter &p, const Cid &n, const QRectF &r, bool dim) const
{
    // A building or a pop type in the middle: every good it takes down the left edge, with name and amount.
    const NodeInfo &d = _info(n);
    const auto [top, bot] = _frame_rgb(n);
    const QColor dark = _band_frame(p, n, r, dim, top, bot);
    _head(p, n, r, dark, 30);
    const QFont small = font_px(font(), 12);
    p.setFont(small);
    for (const auto &[text, y, rgb] : d.groups) {
        const QColor c = qc(rgb);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(r.left() + 10, r.top() + y + 7, r.width() - 20, 3), 1.5, 1.5);
        p.setPen(c.darker(160));
        drawtext::draw(p, QRectF(r.left() + 12, r.top() + y - 11, r.width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    _slots(p, r, d, dark, 46, true);
    for (const auto &[text, y, rgb] : d.sums) {      // a pop type: what each kind of needs costs
        p.setFont(small);
        p.setPen(qc(rgb).darker(150));
        drawtext::draw(p, QRectF(r.left() + 12, r.top() + y, r.width() - 24, 18), Qt::AlignRight | Qt::AlignVCenter, text);
    }
    if (d.total) {
        const auto &[text, y] = *d.total;
        p.setPen(QPen(QColor(54, 42, 30), 2));
        p.drawLine(QPointF(r.left() + 10, r.top() + y - 2), QPointF(r.right() - 10, r.top() + y - 2));
        QFont big(small);
        big.setPixelSize(15);
        p.setFont(big);
        p.setPen(QColor(30, 22, 14));
        drawtext::draw(p, QRectF(r.left() + 12, r.top() + y + 2, r.width() - 24, 22), Qt::AlignRight | Qt::AlignVCenter, text);
    }
    if (!d.body.isEmpty()) {
        p.setFont(small);
        p.setPen(QColor(40, 36, 44));
        drawtext::draw(p, QRectF(r.left() + 10, r.bottom() - 30, r.width() - 20, 26), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(small).elidedText(d.body, Qt::ElideRight, int(py::trunc_int(r.width() - 20))));
    }
    p.setOpacity(1.0);
}

QString _pct(const QString &v)
{
    const auto f = py::to_float(v);
    return f ? gfmt(*f * 100) : v;
}

QString _cond(const moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &conds)
{
    QStringList out;
    for (const auto &[k, v] : conds) {
        if (k == u"trade_goods_in_state") {
            const moddata::Good *g = mod.goods.get(v);
            out << u"%1 in the state"_s.arg(g ? g->name : pretty(v));
        } else if (k == u"has_building") {
            const QString *l = mod.loc.get(v);
            out << u"a %1 in the state"_s.arg(l && !l->isEmpty() ? *l : pretty(v));
        } else {
            out << u"%1 = %2"_s.arg(k, v);
        }
    }
    return out.isEmpty() ? u"—"_s : out.join(u" and "_s);
}

// ---------------------------------------------------------------------------------------------- the workspace
static QString mny(double v) { return money(v); }   // (the member checkbox `money` hides it)
static QString pct_d(double v) { return gfmt(v * 100); }   // _pct of a number

static pyjson::Object load_json(const QString &key)
{
    try {
        const QString v = settings_::settings()->value(key, QString()).toString();
        if (v.isEmpty())
            return {};
        const auto j = pyjson::loads(v);
        return j && j->is_obj() ? j->obj() : pyjson::Object{};
    } catch (const std::exception &) {
        return {};
    }
}

static bool json_bool(const pyjson::Object &o, const QString &k, bool dflt)
{
    const pyjson::Value *v = o.get(k);
    return v ? v->truthy() : dflt;
}

ProductionWorkspace::ProductionWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *barl = new QVBoxLayout(bar);
    barl->setContentsMargins(16, 8, 16, 6);
    barl->setSpacing(6);
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(6);
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(6);
    barl->addLayout(row1);
    barl->addLayout(row2);
    const pyjson::Object saved = load_json(u"prodFilters"_s);
    for (const auto &[k, t] : FILTERS) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(json_bool(saved, k, true));
        connect(b, &QPushButton::clicked, this, [this] { _filters_changed(); });
        b->setIcon(widgets::swatch_icon(KIND_RGB.get(k)->second, 12));
        row2->addWidget(b);
        show_kind.insert(k, b);
    }
    row2->addSpacing(14);
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a good, production, building or pop need… (type, then pick)"_s);
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(260);
    const std::pair<QString, QString> segs[2] = {
        {u"Production chains"_s, u"Goods, factories, what makes what, pop needs"_s},
        {u"Output estimate"_s, u"What every farm, mine and factory turns out per day at the start, by nation"_s}};
    for (int i = 0; i < 2; ++i) {
        auto *sb = new QPushButton(segs[i].first);   // the two pages of Economy
        sb->setObjectName(u"seg"_s);
        sb->setCheckable(true);
        sb->setChecked(i == 0);
        sb->setToolTip(segs[i].second);
        if (i) {
            output_btn = sb;
            connect(sb, &QPushButton::clicked, this, [this, sb] {
                sb->setChecked(false);
                Q_EMIT outputRequested();
            });
        } else {
            connect(sb, &QPushButton::clicked, this, [sb] { sb->setChecked(true); });
        }
        row1->addWidget(sb);
    }
    row1->addSpacing(12);
    row1->addWidget(search, 1);
    fit_btn = new QPushButton(u"Fit"_s);
    fit_btn->setToolTip(u"All of it in view"_s);
    connect(fit_btn, &QPushButton::clicked, this, [this] { chart->fit(); });
    row1->addWidget(fit_btn);
    new_btn = new QPushButton(u"＋ New production"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { _new_form(); });
    row1->addWidget(new_btn);
    auto check = [&](const QString &text, const QString &key, bool dflt, const QString &tip) {
        auto *cb = new QCheckBox(text);
        cb->setChecked(json_bool(saved, key, dflt));
        cb->setToolTip(tip);
        connect(cb, &QCheckBox::toggled, this, [this](bool) { _filters_changed(); });
        row2->addWidget(cb);
        return cb;
    };
    boosts = check(u"Boosts"_s, u"boosts"_s, true,
                   u"Magenta, dotted, into the top of a card: a bonus (a trade good or a building in the state) boosts "
                   u"that production"_s);
    eff = check(u"Efficiency goods"_s, u"eff"_s, true,
                u"Cyan, dashed, into the bottom of a card: goods a factory uses to work better (from its template)"_s);
    eff->blockSignals(true);
    eff->setChecked(true);                           // always on at the start
    eff->blockSignals(false);
    flows = check(u"Inputs / outputs"_s, u"flows"_s, true,
                  u"The input / output lines — off (with Boosts on) shows only what boosts what"_s);
    goods_between = check(u"Goods between"_s, u"goods"_s, true,
                          u"Off: no goods boxes — productions connect straight to what uses what they make (one line "
                          u"per good, in the good's colour)"_s);
    combine = check(u"Combine input + boost"_s, u"combine"_s, true,
                    u"A boost from something that already feeds the production an input (the same production, or the "
                    u"good itself): no card of its own above — its boost rides on that input line (magenta stripe + "
                    u"its value). Off: a card and a line of its own"_s);
    money = check(u"Money"_s, u"money"_s, true,
                  u"Each production's inputs cost, its output's worth and what's left (from the prices in goods.txt — "
                  u"click a good to change its price)"_s);
    row2->addStretch(1);
    lay->addWidget(bar);
    try {
        const QStringList v = settings_::settings()->value(u"prodParked"_s).toStringList();
        for (const QString &s : v)
            parked.insert(pk(str_nid(s)));
    } catch (const std::exception &) {
    }
    iso_bar = new QWidget();                         // the Explorer's trail
    iso_bar->setObjectName(u"side"_s);
    auto *il = new QHBoxLayout(iso_bar);
    il->setContentsMargins(16, 4, 16, 6);
    crumbs = new QWidget();
    crumb_lay = new widgets::WrapRow(crumbs, 4);
    il->addWidget(crumbs, 1);
    iso_label = label(QString(), u"sub"_s);
    iso_label->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::GOLD));
    il->addWidget(iso_label, 1);
    back_btn = new QPushButton(u"◀ Back"_s);
    back_btn->setToolTip(u"Back to the one before (the trail: click any of them)"_s);
    connect(back_btn, &QPushButton::clicked, this, [this] { _ex_back(); });
    il->addWidget(back_btn, 0, Qt::AlignTop);
    iso_bar->hide();
    lay->addWidget(iso_bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    chart = new ProdChart(mod);
    connect(chart, &ProdChart::picked, this, [this](const QString &k, const QString &key, bool ctrl) { _picked(k, key, ctrl); });
    connect(chart, &ProdChart::parked, this, [this](const Cid &n) { park(n); });
    connect(chart, &ProdChart::expanded, this, [this](const Cid &n) { _toggle_open(n); });
    connect(chart, &ProdChart::isolated, this, [this](const Cid &n) { explore(n); });   // double-click: to the middle
    connect(chart, &ProdChart::unparked, this, [this](const Cid &n) { unpark(n); });
    split->addWidget(chart);
    side = new QWidget();
    sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(0);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setObjectName(u"sideScroll"_s);
    scroll->setWidget(side);
    scroll->setMinimumWidth(420);
    split->addWidget(scroll);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 2);
    split->setSizes({1300, 460});
    lay->addWidget(split, 1);
    _build_side();
}

// ------------------------------------------------------------------ settings
void ProductionWorkspace::_save_filters()
{
    pyjson::Object v;
    for (const auto &[k, b] : show_kind)
        v.insert(k, pyjson::Value(b->isChecked()));
    v.insert(u"boosts"_s, pyjson::Value(boosts->isChecked()));
    v.insert(u"eff"_s, pyjson::Value(eff->isChecked()));
    v.insert(u"flows"_s, pyjson::Value(flows->isChecked()));
    v.insert(u"goods"_s, pyjson::Value(goods_between->isChecked()));
    v.insert(u"money"_s, pyjson::Value(money->isChecked()));
    v.insert(u"view"_s, pyjson::Value(view));
    v.insert(u"combine"_s, pyjson::Value(combine->isChecked()));
    try {
        settings_::settings()->setValue(u"prodFilters"_s, pyjson::dumps(pyjson::Value(std::move(v))));
    } catch (const std::exception &) {
    }
}

void ProductionWorkspace::_filters_changed()
{
    _save_filters();
    _rebuild();
    _fit();
}

// ------------------------------------------------------------------ helpers
QString ProductionWorkspace::gname(const QString &g) const
{
    const moddata::Good *e = mod.goods.get(g);
    return e ? e->name : pretty(g);
}

QString ProductionWorkspace::pname(const QString &key) const
{
    // A production is named after what it makes (its kind shows in its colour).
    const OptStr out = edits ? production::describe(edits->db, edits->text(key)).output : std::nullopt;
    return out && !out->isEmpty() ? gname(*out) : pretty(key);
}

QString ProductionWorkspace::bname(const QString &b) const
{
    const QString *l = mod.loc.get(b);
    return l && !l->isEmpty() ? *l : pretty(b);
}

QString ProductionWorkspace::nname(const Cid &n) const
{
    if (n.kind == ALL.kind && n.key == ALL.key)
        return u"⌂ Everything"_s;
    if (n.kind == u"prod")
        return _names.value(n.key, n.key);
    if (n.kind == u"good")
        return gname(n.key);
    if (n.kind == u"bld")
        return bname(n.key);
    const moddata::PopType *pt = mod.poptypes.get(n.key);
    return (pt ? pt->name : n.key) + u" — needs"_s;
}

std::vector<std::pair<QString, QString>> ProductionWorkspace::goods_items() const
{
    std::vector<std::pair<QString, QString>> out;
    for (const auto &kv : mod.goods)
        out.emplace_back(kv.first, gname(kv.first));
    return out;
}

// ------------------------------------------------------------------ the side panel
void ProductionWorkspace::_build_side()
{
    // new production
    auto *s = new_section = new Section();
    s->lay->addWidget(label(u"New production"_s));
    auto *row = new QHBoxLayout();
    auto *grp = new QButtonGroup(this);
    for (const QString &k : KINDS) {
        auto *b = new QRadioButton(KIND_NAME.value(k));
        grp->addButton(b);
        row->addWidget(b);
        new_kind.insert(k, b);
    }
    (*new_kind.get(u"factory"_s))->setChecked(true);
    s->lay->addLayout(row);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Makes"_s, u"sub"_s, false), 0, 0);
    new_out = new QComboBox();
    new_out->setEditable(true);
    new_out->setInsertPolicy(QComboBox::NoInsert);
    new_out->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    g->addWidget(new_out, 0, 1);
    g->addWidget(label(u"Name (key)"_s, u"sub"_s, false), 1, 0);
    new_key = new QLineEdit();
    g->addWidget(new_key, 1, 1);
    s->lay->addLayout(g);
    new_building = new QCheckBox(u"Also make its factory building (buildings.txt) — unlock it with a technology or invention "
                                 u"(“Unlocks building”)"_s);
    new_building->setChecked(true);
    s->lay->addWidget(new_building);
    new_err = label(QString(), u"warn"_s);
    new_err->hide();
    s->lay->addWidget(new_err);
    row = new QHBoxLayout();
    row->addStretch(1);
    auto *cancel = new QPushButton(u"Cancel"_s);
    connect(cancel, &QPushButton::clicked, this, [this] { new_section->hide(); });
    auto *make = new QPushButton(u"Create"_s);
    connect(make, &QPushButton::clicked, this, [this] { _create(); });
    row->addWidget(cancel);
    row->addWidget(make);
    s->lay->addLayout(row);
    for (const auto &kv : new_kind)
        connect(kv.second, &QRadioButton::toggled, this, [this](bool) { _suggest_key(); });
    connect(new_out, &QComboBox::currentIndexChanged, this, [this](int) { _suggest_key(); });
    s->hide();
    sl->addWidget(s);
    // the selected card / good
    s = head = new Section();
    title = label(u"Click a card or a good in the chart"_s, u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    sl->addWidget(s);
    body = new QWidget();
    bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    sl->addWidget(body);
    sl->addStretch(1);
}

void ProductionWorkspace::_clear_body()
{
    while (bl->count()) {
        QLayoutItem *it = bl->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
}

// ------------------------------------------------------------------ refresh
void ProductionWorkspace::refresh(document::Document *doc_)
{
    doc = doc_;
    edits = doc && doc->maps.contains(u"production"_s) ? &doc->map<production::ProdEdits>(u"production"_s) : nullptr;
    if (!edits)
        return;
    if (new_out->count() == 0) {
        for (const auto &[g, t] : goods_items())
            new_out->addItem(t, g);
        techpanel::searchable(new_out);
    }
    _rebuild();
    _fill_side();
}

OrderedMap<Cid, IO> ProductionWorkspace::_model()
{
    // Every card of the chart: {node: (inputs [(good, amount)], output good or None)} and their names.
    production::ProdEdits &ed = *edits;
    _info.clear();
    for (const QString &k : ed.keys())
        _info.insert(k, ed.info(k));
    _names.clear();
    for (const auto &kv : _info)
        _names.insert(kv.first, pname(kv.first));
    QHash<QString, bool> show;
    for (const auto &[k, b] : show_kind)
        show.insert(k, b->isChecked());
    OrderedMap<Cid, IO> io;
    for (const auto &[k, d] : _info)
        if (show.value(KINDS.contains(d.kind) ? d.kind : u"factory"_s, true))
            io.insert({u"prod"_s, k, 0}, {d.inputs, d.output});
    _bld.clear();
    if (show.value(u"building"_s))
        for (const QString &b : ed.buildings()) {
            const production::BuildingDesc bi = ed.bld_info(b);
            _bld.insert(b, bi);
            io.insert({u"bld"_s, b, 0}, {bi.cost, std::nullopt});
        }
    _needs.clear();                                  // pop type -> [(life / everyday / luxury, good, amount)] in slot order
    if (show.value(u"need"_s)) {
        QHash<QString, long long> order;
        long long i = 0;
        for (const auto &kv : mod.goods)
            order.insert(kv.first, i++);
        for (const QString &pt : ed.poptypes()) {
            const OrderedMap<QString, Goods> nd = ed.needs(pt);
            std::vector<std::tuple<QString, QString, double>> slots;
            for (const auto &[kind, _t] : NEEDS) {
                Goods lst = nd.value(kind);
                std::stable_sort(lst.begin(), lst.end(), [&](const auto &a, const auto &b) {
                    return order.value(a.first, 999) < order.value(b.first, 999);
                });
                for (const auto &[g, a] : lst)
                    slots.emplace_back(kind, g, a);
            }
            if (!slots.empty()) {
                _needs.insert(pt, slots);
                Goods ins;
                for (const auto &[_k, g, a] : slots)
                    ins.emplace_back(g, a);
                io.insert({u"need"_s, pt, 0}, {ins, std::nullopt});
            }
        }
    }
    return io;
}

SortKey ProductionWorkspace::_sort_key(const Cid &n) const
{
    if (n.kind == u"prod") {
        const production::Desc *d = _info.get(n.key);
        const QString kind = d ? d->kind : QString();
        const long long r = kind == u"rgo" ? 0 : kind == u"artisan" ? 1 : kind == u"factory" ? 2 : 3;
        return {1, r, QString(), _names.value(n.key, n.key)};
    }
    if (n.kind == u"bld")
        return {2, 0, QString(), bname(n.key)};
    if (n.kind == u"need") {
        const auto [a, b] = mod.poptype_sort_key(n.key);
        return {3, a, b, n.key};
    }
    return {0, 0, QString(), gname(n.key)};
}

Rgb ProductionWorkspace::_gcol(const QString &g) const
{
    const moddata::Good *e = mod.goods.get(g);
    return e ? e->color : Rgb{160, 160, 160};
}

static double cost_of(const OrderedMap<QString, double> &costs, const QString &g) { return costs.value(g, 0.0); }

Look ProductionWorkspace::_look(const Cid &n, const OrderedMap<Cid, IO> &io, const OrderedMap<QString, double> &costs,
                                bool money_on, QString as_, const OptStr &good)
{
    // (layout spec, drawing) of card n: a production's card, a good's box, a building / pop type as a small card next
    // to something ("chip" — `good` is the one its line brings) or as a list card in the middle ("list").
    Look L;
    if (n.kind == u"good") {
        const QString &g = n.key;
        L.spec = {GOOD_W, GOOD_H, GOOD_H / 2, GOOD_H / 2, 0.0};
        L.slots = {GOOD_H / 2};
        QStringList made, used;
        for (const Cid &x : _producers.value(g))
            made << nname(x);
        for (const Cid &x : _consumers.value(g))
            used << nname(x);
        L.d.name = gname(g);
        L.d.cost = cost_of(costs, g);
        L.d.tip = u"made by "_s + (made.isEmpty() ? u"nothing"_s : made.join(u", "_s)) + u" · used by "_s +
                  (used.isEmpty() ? u"nothing"_s : used.join(u", "_s));
        return L;
    }
    const Goods &ins = io.get(base(n))->ins;
    if ((n.kind == u"bld" || n.kind == u"need") && as_ == u"card")
        as_ = u"chip"_s;
    if (n.kind == u"prod") {
        const production::Desc &d = *_info.get(n.key);
        const QString name = _names.value(n.key);
        const int hdr = header_h(name);
        py::Sum sin;
        for (const auto &[g, a] : d.inputs)
            sin.add(a * cost_of(costs, g));
        double cin = sin.value();
        double ceff = 0.0;
        if (d.kind == u"factory") {
            py::Sum se;
            for (const auto &[g, a] : d.efficiency)
                se.add(a * cost_of(costs, g));
            ceff = se.value();
        }
        cin += ceff;                                 // (efficiency goods are bought every day too — measured)
        const bool has_out = d.output && !d.output->isEmpty();
        const double cout = has_out ? d.value * cost_of(costs, *d.output) : 0.0;
        const double foot = money_on ? FOOT : 0;
        const double h = hdr + std::max(84.0, 12 + SLOT * double(std::max<size_t>(1, ins.size()))) + foot;
        const std::vector<double> slots = body_slots(hdr, h - foot, static_cast<long long>(ins.size()));
        const double outy = hdr + (h - foot - hdr) / 2;
        L.spec = {CARD_W, h, outy, hdr / 2.0, 16.0};
        L.slots = slots;
        QStringList boosts_l;
        for (const production::Bonus &x : d.bonuses)
            boosts_l << u"+"_s + _pct(x.value) + u"% with "_s + _cond(mod, x.conds);
        QStringList in_l;
        for (const auto &[g, a] : ins)
            in_l << gfmt(a) + u" "_s + gname(g);
        NodeInfo &D = L.d;
        D.name = name;
        D.kind = d.kind;
        D.hdr = hdr;
        D.slots = slots;
        D.out = outy;
        D.bout = hdr / 2.0;
        for (const auto &[g, a] : ins)
            D.inputs.emplace_back(g, amt(a));
        D.output = d.output;
        D.value = d.value;
        D.bonuses = static_cast<long long>(d.bonuses.size());
        if (money_on)
            D.money = std::pair{cin, cout};
        D.tip = KIND_NAME.value(d.kind, u"?"_s) + u" ("_s + n.key + u")\n"_s +
                (in_l.isEmpty() ? u"nothing"_s : in_l.join(u", "_s)) + u" → "_s + gfmt(d.value) + u" "_s +
                (has_out ? gname(*d.output) : u"?"_s) +
                (boosts_l.isEmpty() ? QString() : u"\nBoosts: "_s + boosts_l.join(u"; "_s)) + u"\nMoney: in "_s +
                mny(cin) +
                (ceff ? u" (inputs "_s + mny(cin - ceff) + u" + efficiency goods "_s + mny(ceff) + u")"_s
                      : QString()) +
                u", out "_s + mny(cout) + u", left "_s + mny(cout - cin);
        return L;
    }
    if (n.kind == u"bld") {
        const production::BuildingDesc &bi = *_bld.find(n.key);
        const QString name = bname(n.key);
        py::Sum st;
        for (const auto &[g, a] : ins)
            st.add(a * cost_of(costs, g));
        const double total = st.value();
        const QString kt = production::BUILDING_KIND.value(bi.type, pretty(bi.type));
        QStringList parts;
        for (const auto &[g, a] : ins)
            parts << amt(a) + u" "_s + gname(g);
        const QString time = bi.time && !bi.time->isEmpty() ? *bi.time : u"?"_s;
        const QString tip = u"Building "_s + n.key + u" — costs to build: "_s +
                            (parts.isEmpty() ? u"nothing"_s : parts.join(u", "_s)) + u" (≈ "_s + mny(total) +
                            u" at the base prices), "_s + time + u" days"_s;
        if (as_ == u"chip") {
            std::optional<double> a;
            for (const auto &[g, x] : ins)
                if (good && g == *good) {
                    a = x;
                    break;
                }
            L.spec = {250.0, 80.0, 40.0, 15.0, 0.0};
            L.slots = {55.0};
            L.d.look = u"chip"_s;
            L.d.name = name;
            if (a)
                L.d.inputs = {{*good, amt(*a)}};
            L.d.slots = {55.0};
            L.d.sub = kt + u" · "_s + time + u" days"_s;
            L.d.tip = tip;
            return L;
        }
        if (as_ == u"list") {
            std::vector<double> ys;
            for (size_t i = 0; i < ins.size(); ++i)
                ys.push_back(50.0 + SLOT * double(i));
            const double h = 50.0 + SLOT * double(ins.size()) + 30.0;
            L.spec = {330.0, h, h / 2, 16.0, 0.0};
            L.slots = ys;
            L.d.look = u"list"_s;
            L.d.name = name;
            for (const auto &[g, a] : ins)
                L.d.inputs.emplace_back(g, amt(a));
            L.d.slots = ys;
            L.d.body = kt + u" · "_s + time + u" days · ≈ "_s + mny(total);
            L.d.tip = tip;
            return L;
        }
        return _look(n, io, costs, money_on, u"chip"_s, good);
    }
    const QString &pt = n.key;                       // a pop type's needs
    const auto &slots = *_needs.get(pt);
    const QString name = nname(base(n));
    const moddata::PopType *ptp = mod.poptypes.get(pt);
    const Rgb color = ptp ? ptp->color : Rgb{160, 150, 190};
    OrderedMap<QString, double> basket;
    for (const auto &[k, _t] : NEEDS) {
        py::Sum s;
        for (const auto &[kk, g, a] : slots)
            if (kk == k)
                s.add(a * cost_of(costs, g));
        basket.insert(k, s.value());
    }
    auto basket_all = [&] {
        py::Sum s;
        for (const auto &kv : basket)
            s.add(kv.second);
        return s.value();
    };
    QStringList tparts;
    for (const auto &[kind, t] : NEEDS) {
        bool any = false;
        QStringList gl;
        for (const auto &[k, g, a] : slots)
            if (k == kind) {
                any = true;
                gl << gname(g) + u" "_s + amt(a);
            }
        if (any)
            tparts << t + u": "_s + gl.join(u", "_s) + u" (≈ "_s + mny(basket.value(kind)) + u")"_s;
    }
    const QString tip = u"What "_s + py::split(name, u" — "_s)[0] + u" buy (poptypes/"_s + pt + u".txt) — "_s +
                        tparts.join(u" · "_s);
    OrderedMap<QString, QString> titles;
    for (const auto &[k, t] : NEEDS)
        titles.insert(k, t);
    auto kind_rgb = [](const QString &k) { return NEED_KIND_RGB.value(k, Rgb{150, 150, 150}); };
    if (as_ == u"needsec") {                         // one kind of its needs (good = the kind) in the middle
        std::vector<std::pair<QString, double>> lst;
        for (const auto &[k, g, a] : slots)
            if (good && k == *good)
                lst.emplace_back(g, a);
        L.spec = {290.0, 44.0 + 30.0 * double(std::max<size_t>(1, lst.size())) + 12.0 + 30.0, 60.0, 18.0, 0.0};
        L.slots = {60.0};
        L.d.look = u"needsec"_s;
        L.d.name = name;
        L.d.title = titles.value(good.value_or(QString()), good.value_or(QString()));
        L.d.rgb = kind_rgb(good.value_or(QString()));
        L.d.cost_text = mny(basket.value(good.value_or(QString()), 0.0));
        L.d.all = mny(basket_all());
        for (const auto &[g, a] : lst)
            L.d.goods.emplace_back(g, amt(a));
        L.d.tip = tip;
        return L;
    }
    if (as_ == u"chip") {
        QStringList kinds;
        std::optional<double> a;
        for (const auto &[k, g, x] : slots)
            if (good && g == *good) {
                kinds << k;
                if (!a)
                    a = x;
            }
        static const QHash<QString, QString> SHORT = {
            {u"life_needs"_s, u"life"_s}, {u"everyday_needs"_s, u"everyday"_s}, {u"luxury_needs"_s, u"luxury"_s}};
        QStringList sk;
        for (const QString &k : kinds)
            sk << SHORT.value(k, k);
        const QString sub_ = !kinds.isEmpty() ? py::capitalize(sk.join(u" + "_s) + u" need"_s) : u"Pop needs"_s;
        const QColor rgb = !kinds.isEmpty() ? qc(NEED_KIND_RGB.value(kinds[0], Rgb{90, 80, 70})).darker(170) : QColor(60, 50, 40);
        L.spec = {250.0, 80.0, 40.0, 15.0, 0.0};
        L.slots = {55.0};
        L.d.look = u"chip"_s;
        L.d.name = name;
        L.d.color = color;
        if (a)
            L.d.inputs = {{*good, amt(*a)}};
        L.d.slots = {55.0};
        L.d.sub = sub_;
        L.d.sub_rgb = rgb_of(rgb);
        L.d.tip = tip;
        return L;
    }
    if (as_ == u"list") {
        std::vector<double> ys;
        std::vector<std::tuple<QString, double, Rgb>> groups, sums;
        double y = 40.0;
        OptStr prev;
        auto close = [&](const QString &kind) {
            sums.emplace_back(titles.value(kind, kind) + u" together ≈ "_s + mny(basket.value(kind, 0.0)),
                              y + 4.0, kind_rgb(kind));
        };
        for (const auto &[kind, _g, _a] : slots) {
            if (OptStr(kind) != prev) {
                if (prev) {
                    close(*prev);
                    y += 24.0;
                }
                groups.emplace_back(titles.value(kind, kind), y + 12.0, kind_rgb(kind));
                y += 30.0;
                prev = kind;
            }
            ys.push_back(y + SLOT / 2);
            y += SLOT;
        }
        if (prev) {
            close(*prev);
            y += 24.0;
        }
        const std::pair<QString, double> total{u"All needs ≈ "_s + mny(basket_all()) + u" a day"_s, y + 6.0};
        y += 32.0;
        const double h = y + 14.0;
        L.spec = {330.0, h, h / 2, 16.0, 0.0};
        L.slots = ys;
        L.d.look = u"list"_s;
        L.d.name = name;
        L.d.color = color;
        for (const auto &[_k, g, a] : slots)
            L.d.inputs.emplace_back(g, amt(a));
        L.d.slots = ys;
        L.d.groups = groups;
        L.d.sums = sums;
        L.d.total = total;
        L.d.tip = tip;
        return L;
    }
    return _look(n, io, costs, money_on, u"chip"_s, good);
}

static bool cid_less(const Cid &a, const Cid &b) { return std::tie(a.kind, a.key, a.n) < std::tie(b.kind, b.key, b.n); }

void ProductionWorkspace::_rebuild()
{
    if (!edits)
        return;
    OrderedMap<Cid, IO> io = _model();
    QHash<QString, std::vector<Cid>> producers, consumers;
    std::vector<Cid> ks;
    for (const auto &kv : io)
        ks.push_back(kv.first);
    std::sort(ks.begin(), ks.end(), cid_less);
    for (const Cid &n : ks) {
        const IO &x = *io.get(n);
        if (x.out && !x.out->isEmpty())
            producers[*x.out].push_back(n);
        for (const auto &[g, _a] : x.ins)
            consumers[g].push_back(n);
    }
    _producers = producers;
    _consumers = consumers;
    _io = io;
    std::vector<Cid> trail;
    for (const Cid &n : ex_trail)
        if (_valid(n, io))
            trail.push_back(n);
    ex_trail = trail;
    _skipped.clear();
    const OrderedMap<QString, double> costs = _prices();   // (what-if prices where set)
    const bool money_on = money->isChecked();
    Pass ps = _explore_pass(io);
    _lay = ps.sc;
    const prodtree::Scene &lay = *ps.sc;
    std::vector<Meta> meta;
    for (const prodtree::Net &net_ : lay.nets) {
        QSet<QString> styles;
        for (const auto &t : net_.tgt)
            styles.insert(std::get<2>(t));
        const QString trunk = styles.contains(u"flow"_s) || styles.contains(u"flowboost"_s) ? u"flow"_s
                              : styles.contains(u"boost"_s)                                 ? u"boost"_s
                                                                                            : u"eff"_s;
        const OptStr &g = net_.good;
        const QString what = net_.what && !net_.what->isEmpty() ? *net_.what
                             : g && !g->isEmpty() ? gname(*g)
                                                  : bname(net_.id.split(QChar(0x1e)).value(1)) + u" (a building in the state)"_s;
        QStringList srcs;
        for (const auto &[s, _p] : net_.src)
            srcs << nname(s);
        QStringList parts;
        for (const auto &[st, t] : {std::pair{u"flow"_s, u"into"_s}, {u"flowboost"_s, u"into (and boosts)"_s},
                                     {u"boost"_s, u"boosts"_s}, {u"eff"_s, u"makes work better"_s}}) {
            QStringList lst;
            for (const auto &x : net_.tgt)
                if (std::get<2>(x) == st)
                    lst << (std::get<0>(x) ? nname(*std::get<0>(x)) : QString());
            if (!lst.isEmpty())
                parts << t + u" "_s + lst.join(u", "_s);
        }
        Meta m;
        m.good = g;
        m.rgb = net_.rgb;
        m.trunk = trunk;
        m.src = net_.src;
        for (const auto &x : net_.tgt)
            if (std::get<0>(x)) {
                m.tgts.insert(*std::get<0>(x));
                m.members.insert(*std::get<0>(x));
            }
        for (const auto &[s, _p] : net_.src)
            m.members.insert(s);
        m.title = what + u" — from "_s + srcs.join(u", "_s) + u"\n"_s + parts.join(u"\n"_s);
        meta.push_back(std::move(m));
    }
    const std::set<std::pair<QString, QString>> parked_now = _skipped;   // the parked ones this view left out
    OrderedMap<Cid, QRectF> ghosts;
    for (const auto &pp : parked_now) {
        const Cid n = from_pk(pp);
        Look lk = _look(n, io, costs, money_on, u"card"_s, std::nullopt);
        ps.spec.insert(n, lk.spec);
        ps.slots.insert(n, lk.slots);
        ps.draw.insert(n, lk.d);
    }
    OrderedMap<Cid, QRectF> parked_at;
    std::optional<QRectF> park_area;
    std::vector<Cid> pkl;
    for (const auto &pp : parked_now)
        pkl.push_back(from_pk(pp));
    std::stable_sort(pkl.begin(), pkl.end(), [&](const Cid &a, const Cid &b) {
        const NodeInfo *da = ps.draw.get(a), *db = ps.draw.get(b);
        return (da ? da->name : a.key) < (db ? db->name : b.key);
    });
    if (!pkl.empty() && !lay.rects.isEmpty()) {
        double right = -INFINITY, left = INFINITY, top = INFINITY;
        for (const auto &[_n, r] : lay.rects) {
            const QRectF q(r[0], r[1], r[2], r[3]);
            right = std::max(right, q.right());
            left = std::min(left, q.left());
            top = std::min(top, q.top());
        }
        for (const prodtree::Text &t : lay.texts)
            top = std::min(top, t.y);
        const double width = right - 40;
        const long long per_row = std::max<long long>(1, py::trunc_int(py::floordiv(width, CARD_W + 60)));
        double hmax = 0;
        for (const Cid &n : pkl)
            hmax = std::max(hmax, ps.spec.value(n).h);
        const long long rows = (static_cast<long long>(pkl.size()) + per_row - 1) / per_row;
        const double area_h = 50 + double(rows) * (hmax + 26);
        const double y0 = top - area_h - 60;
        const double x0 = left;
        for (size_t i = 0; i < pkl.size(); ++i) {
            const long long ii = static_cast<long long>(i);
            const double x = x0 + 26 + double(ii % per_row) * (CARD_W + 60);
            const double y = y0 + 44 + double(ii / per_row) * (hmax + 26);
            const prodtree::Spec sp = ps.spec.value(pkl[i]);
            parked_at.insert(pkl[i], QRectF(x, y, sp.w, sp.h));
        }
        park_area = QRectF(x0, y0, std::min(width + 20, double(per_row) * (CARD_W + 60) + 40), area_h);
    }
    _draw = ps.draw;
    chart->set_graph(ps.sc, ps.draw, meta, ghosts, parked_at, park_area);
    _bars();
    _focus();
    // the search list
    QStringList items;
    _search_map.clear();
    for (const auto &kv : mod.goods) {
        const QString t = u"Good: "_s + gname(kv.first);
        items << t;
        _search_map[t] = {u"good"_s, kv.first, 0};
    }
    for (const auto &[n, _x] : io) {
        QString t;
        if (n.kind == u"prod")
            t = KIND_NAME.value(_info.get(n.key)->kind, u"?"_s) + u": "_s + _names.value(n.key) + u"  ("_s + n.key + u")"_s;
        else if (n.kind == u"bld")
            t = u"Building: "_s + bname(n.key) + u"  ("_s + n.key + u")"_s;
        else {
            const moddata::PopType *p = mod.poptypes.get(n.key);
            t = u"Pop needs: "_s + (p ? p->name : n.key);
        }
        items << t;
        _search_map[t] = n;
    }
    // ONE completer for good, only its words renewed: a new completer for every rebuild — and a pick rebuilds — took
    // the old one away while it was still handling the pick, and the editor crashed (the user, 2026-10-16)
    QCompleter *comp = search->completer();
    if (!comp) {
        comp = new QCompleter(new QStringListModel(search), search);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setFilterMode(Qt::MatchContains);
        comp->setMaxVisibleItems(18);
        connect(comp, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString &t) { _search_pick(t); });
        search->setCompleter(comp);
    }
    std::sort(items.begin(), items.end());
    static_cast<QStringListModel *>(comp->model())->setStringList(items);
}

// --- the Explorer
bool ProductionWorkspace::_valid(const OptCid &n, const OrderedMap<Cid, IO> &io) const
{
    return n && (*n == ALL || io.contains(*n) || (n->kind == u"good" && mod.goods.contains(n->key)));
}

OptCid ProductionWorkspace::_ex_centre() const
{
    return ex_trail.empty() ? OptCid() : OptCid(ex_trail.back());
}

bool ProductionWorkspace::_free(const Cid &n)
{
    // False for a parked card: a focused view leaves it out (and lists it in the Parked area).
    if (parked.count(pk(n))) {
        _skipped.insert(pk(n));
        return false;
    }
    return true;
}

static double slot_y(const Pass &ps, const Cid &cid, const Cid &n, const OptStr &g, const OrderedMap<Cid, IO> &io)
{
    // Where good g's line goes into a copy of card n.
    const std::vector<double> sl = ps.slots.value(cid);
    std::optional<size_t> idx;
    if (const IO *x = io.get(n))
        for (size_t i = 0; i < x->ins.size(); ++i)
            if (g && x->ins[i].first == *g) {
                idx = i;
                break;
            }
    if (idx && *idx < sl.size())
        return sl[*idx];
    return !sl.empty() ? sl[0] : ps.spec.value(cid).h / 2;
}

static prodtree::Port port(const Cid &c, const QString &p) { return {c, p}; }

Pass ProductionWorkspace::_explore_pass(const OrderedMap<Cid, IO> &io)
{
    Pass ps;
    prodtree::Scene &sc = *ps.sc;
    const auto &P = _producers;
    const auto &C = _consumers;
    production::ProdEdits &ed = *edits;
    OptCid c0 = _ex_centre();
    if (!_valid(c0, io)) {
        c0 = ALL;                                    // (_default_centre)
        ex_trail = {ALL};
    }
    _centre_cid.reset();
    const Cid c = *c0;
    if (c == ALL) {
        _build_all(io, ps);
        return ps;
    }
    if (!sel || base(*sel) != c)
        sel = c;
    const OrderedMap<QString, double> costs = _prices();
    const bool money_on = money->isChecked();
    long long cnt = 0;
    std::function<Cid(const Cid &, const QString &, const OptStr &)> put = [&](const Cid &n, const QString &as0,
                                                                            const OptStr &good) {
        QString as_ = as0;
        if ((n.kind == u"bld" || n.kind == u"need") && as_ == u"card")
            as_ = u"chip"_s;
        const Cid cid{n.kind, n.key, ++cnt};
        Look lk = _look(n, io, costs, money_on, as_, good);
        ps.spec.insert(cid, lk.spec);
        ps.slots.insert(cid, lk.slots);
        ps.draw.insert(cid, lk.d);
        return cid;
    };
    auto put1 = [&](const Cid &n) { return put(n, u"card"_s, std::nullopt); };
    const bool goods_on = goods_between->isChecked(), flows_on = flows->isChecked();
    const bool boosts_on = boosts->isChecked(), eff_on = eff->isChecked();
    auto col = [this](const QString &g) { return _gcol(g); };
    if (c.kind == u"need") {
        _build_hub(io, c, ps, put);
        return ps;
    }
    const Cid centre = put(c, c.kind == u"bld" ? u"list"_s : u"card"_s, std::nullopt);
    _centre_cid = centre;
    prodtree::Kids up;
    std::vector<prodtree::Edge> edges;
    const std::set<QStringList> opened = ex_open.count(pk(c)) ? ex_open[pk(c)] : std::set<QStringList>{};
    // combine input + boost: a boost whose source already feeds one of the centre's inputs — the good itself (a trade
    // good in the state) or the production making it (its building in the state) — has no card of its own; its boost
    // rides on that input line
    QHash<QString, Cid> b_of;
    for (const auto &kv : _info)
        if (const OptStr b = ed.building_of(kv.first))
            b_of.insert(*b, {u"prod"_s, kv.first, 0});
    QHash<QString, double> combined;                 // input good -> the boost riding on its line
    QSet<QString> comb_trade;
    std::set<std::pair<QString, std::pair<QString, QString>>> comb_makers;
    std::set<std::tuple<long long, QString, QString>> comb_skip;
    auto in_P = [&](const QString &g, const Cid &r) {
        const auto it = P.find(g);
        return it != P.end() && std::find(it->begin(), it->end(), r) != it->end();
    };
    if (combine->isChecked() && boosts_on && flows_on && c.kind == u"prod" && io.contains(c)) {
        QStringList ins_c;
        for (const auto &[g, _a] : io.get(c)->ins)
            ins_c << g;
        const auto &bonuses = _info.get(c.key)->bonuses;
        for (size_t bi_ = 0; bi_ < bonuses.size(); ++bi_) {
            const production::Bonus &bn = bonuses[bi_];
            for (const auto &[ck, cv] : bn.conds) {
                if (ck == u"trade_goods_in_state" && ins_c.contains(cv)) {
                    combined[cv] += num(bn.value, 0.0);
                    comb_trade.insert(cv);
                    comb_skip.insert({static_cast<long long>(bi_), ck, cv});
                } else if (ck == u"has_building" && b_of.contains(cv) && io.contains(b_of.value(cv))) {
                    const Cid r_ = b_of.value(cv);
                    QStringList gs;
                    for (const QString &g : ins_c)
                        if (in_P(g, r_) && _free(r_))
                            gs << g;
                    if (!gs.isEmpty()) {
                        combined[gs[0]] += num(bn.value, 0.0);
                        comb_makers.insert({gs[0], pk(r_)});
                        comb_skip.insert({static_cast<long long>(bi_), ck, cv});
                    }
                }
            }
        }
    }
    auto mk = [&](const Cid &n, const QString &as_ = u"card"_s, const OptStr &good = std::nullopt) {
        // A card inside an unfolded branch.
        const Cid cid = put(n, as_, good);
        ps.draw.get(cid)->inner = true;
        return cid;
    };
    std::vector<std::pair<double, QString>> pills_left;
    std::function<void(const Cid &, const Cid &, const QStringList &)> grow_left;
    auto unfold_left = [&](const Cid &pid, const Cid &p_, const QStringList &path) {
        // A maker's ＋: what makes it, further left (not for a loop back into the chain).
        const IO *x = io.get(p_);
        if (p_.kind != u"prod" || p_ == c || path.contains(path_el(p_)) || !x || x->ins.empty())
            return;
        QStringList key = QStringList{u"L"_s} + path;
        key << path_el(p_);
        NodeInfo &d = *ps.draw.get(pid);
        d.plus = u"left"_s;
        d.path = key;
        d.open = opened.count(key) > 0;
        if (opened.count(key))
            grow_left(pid, p_, path + QStringList{path_el(p_)});
    };
    grow_left = [&](const Cid &cid, const Cid &n, const QStringList &path) {
        // n's input goods and what makes them, left of its copy cid.
        const Goods ins = io.get(n)->ins;
        const std::vector<double> sl = ps.slots.value(cid);
        const bool deep = n != c || ins.size() <= 6 || !goods_on;
        for (size_t i = 0; i < ins.size(); ++i) {
            const QString g = ins[i].first;
            const double ty = i < sl.size() ? sl[i] : ps.spec.value(cid).h / 2;
            std::vector<Cid> ms;
            if (deep)
                for (const Cid &p_ : P.value(g))
                    if (p_ != n && _free(p_))
                        ms.push_back(p_);
            const QString st_in = cid == centre && combined.contains(g) ? u"flowboost"_s : u"flow"_s;
            if (st_in == u"flowboost")
                pills_left.emplace_back(ty, u"+"_s + pct_d(combined.value(g)) + u"%"_s);
            Cid tgt;
            double tty;
            prodtree::Port fan;
            QString key;
            const QString si = QString::number(i);
            if (goods_on) {
                const Cid gid = put1({u"good"_s, g, 0});
                up[cid].push_back(gid);
                const long long ni = sc.net(nk({u"xl"_s, cid_key(cid), si}), g, col(g), port(gid, u"out"_s));
                edges.push_back({gid, GOOD_H / 2, cid, ty, port(cid, u"slot|"_s + si), ni, st_in});
                tgt = gid;
                tty = GOOD_H / 2;
                fan = port(gid, u"good"_s);
                key = nk({u"xm"_s, cid_key(gid)});
            } else {
                tgt = cid;
                tty = ty;
                fan = port(cid, u"slot|"_s + si);
                key = nk({u"xl"_s, cid_key(cid), si});
            }
            for (const Cid &p_ : ms) {
                const Cid pid = !path.isEmpty() ? mk(p_) : put1(p_);
                up[tgt].push_back(pid);
                const long long ni2 = sc.net(key, g, col(g), port(pid, u"out"_s));
                const bool maker = comb_makers.count({g, pk(p_)}) > 0;
                const bool rides = cid == centre && (comb_trade.contains(g) || maker);
                edges.push_back({pid, *ps.spec.value(pid).out, tgt, tty, fan, ni2,
                                 rides && (!goods_on || maker) ? u"flowboost"_s : u"flow"_s});
                unfold_left(pid, p_, path);
            }
        }
    };
    // --- what it needs (left)
    if (flows_on && c.kind == u"good") {
        for (const Cid &p_ : P.value(c.key)) {
            if (!_free(p_))
                continue;
            const Cid pid = put1(p_);
            up[centre].push_back(pid);
            const long long ni = sc.net(nk({u"xl"_s, u"0"_s}), c.key, col(c.key), port(pid, u"out"_s));
            edges.push_back({pid, *ps.spec.value(pid).out, centre, GOOD_H / 2, port(Cid{}, u"in"_s), ni, u"flow"_s});
            unfold_left(pid, p_, {});
        }
    } else if (flows_on) {
        grow_left(centre, c, {});
    }
    // --- what boosts it (top), its efficiency goods (bottom)
    std::vector<prodtree::RowItem> top, bottom;
    if (c.kind == u"prod") {
        const production::Desc &d = *_info.get(c.key);
        if (boosts_on) {
            for (size_t bi_ = 0; bi_ < d.bonuses.size(); ++bi_) {
                const production::Bonus &bn = d.bonuses[bi_];
                for (const auto &[ck, cv] : bn.conds) {
                    const QString how = u"+"_s + _pct(bn.value) + u"% output"_s;
                    if (comb_skip.count({static_cast<long long>(bi_), ck, cv}))
                        continue;                    // it rides on its input line
                    Cid nd;
                    long long ni;
                    const QString tkey = nk({u"xt"_s, QString::number(top.size())});
                    if (ck == u"trade_goods_in_state" && mod.goods.contains(cv)) {
                        nd = put1({u"good"_s, cv, 0});
                        ni = sc.net(tkey, cv, col(cv), port(nd, u"out"_s), gname(cv) + u" in the state ("_s + how + u")"_s);
                    } else if (ck == u"has_building") {
                        const OptCid r = b_of.contains(cv) ? OptCid(b_of.value(cv)) : std::nullopt;
                        const Cid bc{u"bld"_s, cv, 0};
                        if (r && io.contains(*r) && *r != c && _free(*r))
                            nd = put1(*r);
                        else if (io.contains(bc) && _free(bc))
                            nd = put(bc, u"chip"_s, std::nullopt);
                        else
                            continue;
                        ni = sc.net(tkey, std::nullopt, BOOST_RGB, port(nd, u"out"_s),
                                    u"a "_s + bname(cv) + u" in the state ("_s + how + u")"_s);
                    } else {
                        continue;
                    }
                    top.push_back({nd, ni, u"boost"_s, u"+"_s + _pct(bn.value) + u"%"_s});
                }
            }
        }
        if (eff_on && d.kind == u"factory")
            for (const auto &[g, _a] : d.efficiency) {
                const Cid nd = put1({u"good"_s, g, 0});
                const long long ni = sc.net(nk({u"xb"_s, QString::number(bottom.size())}), g, col(g), port(nd, u"out"_s),
                                            gname(g) + u" (makes it work better)"_s);
                bottom.push_back({nd, ni, u"eff"_s, std::nullopt});
            }
    }
    // --- what uses it (right)
    std::optional<prodtree::BoxItem> box;
    std::vector<prodtree::Group> groups;
    const OptStr o = c.kind == u"good" ? OptStr(c.key) : (c.kind == u"prod" ? io.get(c)->out : std::nullopt);
    const bool has_o = o && !o->isEmpty();
    const OptStr bld_of_c = c.kind == u"prod" ? ed.building_of(c.key) : (c.kind == u"bld" ? OptStr(c.key) : std::nullopt);
    std::function<void(const Cid &, const Cid &, const QStringList &, prodtree::Kids &, std::vector<prodtree::Edge> &)>
        unfold_right = [&](const Cid &cid, const Cid &n, const QStringList &path, prodtree::Kids &down,
                           std::vector<prodtree::Edge> &tedges) {
            // A user's ＋: what uses what it makes, further right (not for a loop back into the chain).
            if (n.kind != u"prod" || n == c || path.contains(path_el(n)))
                return;
            const IO *x = io.get(n);
            const OptStr o2 = x ? x->out : std::nullopt;
            if (!o2 || o2->isEmpty())
                return;
            const std::vector<Cid> cons = C.value(*o2);
            if (!std::any_of(cons.begin(), cons.end(), [&](const Cid &u) { return u != n; }))
                return;
            const QStringList key = path + QStringList{path_el(n)};
            NodeInfo &d = *ps.draw.get(cid);
            d.plus = u"right"_s;
            d.path = key;
            d.open = opened.count(key) > 0;
            if (!opened.count(key))
                return;
            std::vector<Cid> users;
            for (const Cid &u : cons)
                if (u != n && _free(u))
                    users.push_back(u);
            std::stable_sort(users.begin(), users.end(), [&](const Cid &a, const Cid &b) { return _sort_key(a) < _sort_key(b); });
            Cid src;
            double sy;
            if (goods_on) {
                const Cid gid = put1({u"good"_s, *o2, 0});
                down[cid].push_back(gid);
                const long long ni = sc.net(nk({u"xrm"_s, cid_key(cid)}), *o2, col(*o2), port(cid, u"out"_s));
                tedges.push_back({cid, *ps.spec.value(cid).out, gid, GOOD_H / 2, port(cid, u"out"_s), ni, u"flow"_s});
                src = gid;
                sy = GOOD_H / 2;
            } else {
                src = cid;
                sy = *ps.spec.value(cid).out;
            }
            const long long ni = sc.net(nk({u"xru"_s, cid_key(src)}), *o2, col(*o2), port(src, u"out"_s));
            for (const Cid &u : users) {
                const Cid uid = mk(u, u"card"_s, *o2);
                down[src].push_back(uid);
                tedges.push_back({src, sy, uid, slot_y(ps, uid, u, *o2, io), port(src, u"out"_s), ni, u"flow"_s});
                unfold_right(uid, u, key, down, tedges);
            }
        };
    auto boost_of_v = [&](const Cid &n, const QString &ck_, const OptStr &cv_) {
        if (n.kind != u"prod" || !_info.contains(n.key))
            return 0.0;
        py::Sum s;
        for (const production::Bonus &bn : _info.get(n.key)->bonuses)
            for (const auto &[ck, cv] : bn.conds)
                if (ck == ck_ && cv_ && cv == *cv_)
                    s.add(num(bn.value, 0.0));
        return s.value();
    };
    auto has_bonus = [&](const QString &k, const QString &ck_, const OptStr &cv_) {
        for (const production::Bonus &bn : _info.get(k)->bonuses)
            for (const auto &[ck, cv] : bn.conds)
                if (ck == ck_ && cv_ && cv == *cv_)
                    return true;
        return false;
    };
    QStringList sorted_info;
    for (const auto &kv : _info)
        sorted_info << kv.first;
    std::sort(sorted_info.begin(), sorted_info.end());
    std::vector<Cid> bb_all;
    if (bld_of_c && !bld_of_c->isEmpty() && boosts_on)
        for (const QString &k : sorted_info) {
            const Cid pn{u"prod"_s, k, 0};
            if (io.contains(pn) && pn != c && has_bonus(k, u"has_building"_s, bld_of_c))
                bb_all.push_back(pn);
        }
    std::set<std::pair<QString, QString>> both_trade, both_bld, both;
    auto boost_of = [&](const Cid &n, const QString &ck_, const OptStr &cv_) -> OptStr {
        // What n gets from it: its bonuses with this trigger (+25%).
        const double v = boost_of_v(n, ck_, cv_);
        return v ? OptStr(u"+"_s + pct_d(v) + u"%"_s) : std::nullopt;
    };
    auto items = [&](std::vector<Cid> lst, const QString &entry, const QString &style, const QString &title_) {
        std::vector<prodtree::GroupItem> out;
        std::stable_sort(lst.begin(), lst.end(), [&](const Cid &a, const Cid &b) { return _sort_key(a) < _sort_key(b); });
        for (const Cid &n : lst) {
            const Cid cid = put(n, u"card"_s, o);
            prodtree::GroupItem it{cid, entry, std::nullopt, style, std::nullopt, std::nullopt};
            if (style == u"boost") {
                it.label = title_ == u"bb" ? boost_of(n, u"has_building"_s, bld_of_c) : boost_of(n, u"trade_goods_in_state"_s, o);
            } else if (title_ == u"Uses it" && both.count(pk(n))) {   // input and boost in one line
                it.style = u"flowboost"_s;
                const double v = (both_trade.count(pk(n)) ? boost_of_v(n, u"trade_goods_in_state"_s, o) : 0.0) +
                                 (both_bld.count(pk(n)) ? boost_of_v(n, u"has_building"_s, bld_of_c) : 0.0);
                it.label = u"+"_s + pct_d(v) + u"%"_s;
            }
            if (entry == u"in")
                it.ty = slot_y(ps, cid, n, o, io);
            prodtree::Kids down;
            std::vector<prodtree::Edge> tedges;
            unfold_right(cid, n, {u"R"_s, title_}, down, tedges);
            if (!down.isEmpty())
                it.tree = prodtree::Unfolded{down, tedges};
            out.push_back(std::move(it));
        }
        return out;
    };
    if (has_o) {
        std::vector<Cid> users;
        if (flows_on)
            for (const Cid &n : C.value(*o))
                if (n != c && _free(n))
                    users.push_back(n);
        std::vector<Cid> boosted, effs;
        for (const QString &k : sorted_info) {
            const Cid pn{u"prod"_s, k, 0};
            if (io.contains(pn) && pn != c && has_bonus(k, u"trade_goods_in_state"_s, o))
                boosted.push_back(pn);
        }
        {
            std::vector<Cid> keep;
            if (boosts_on)
                for (const Cid &n : boosted)
                    if (_free(n))
                        keep.push_back(n);
            boosted = keep;
        }
        for (const QString &k : sorted_info) {
            const Cid pn{u"prod"_s, k, 0};
            const production::Desc &dk = *_info.get(k);
            if (io.contains(pn) && pn != c && dk.kind == u"factory" &&
                std::any_of(dk.efficiency.begin(), dk.efficiency.end(), [&](const auto &e) { return e.first == *o; }))
                effs.push_back(pn);
        }
        {
            std::vector<Cid> keep;
            if (eff_on)
                for (const Cid &n : effs)
                    if (_free(n))
                        keep.push_back(n);
            effs = keep;
        }
        std::vector<Cid> prods, blds, needs;
        for (const Cid &n : users)
            (n.kind == u"prod" ? prods : n.kind == u"bld" ? blds : n.kind == u"need" ? needs : prods).push_back(n);
        prods.erase(std::remove_if(prods.begin(), prods.end(), [](const Cid &n) { return n.kind != u"prod"; }), prods.end());
        if (combine->isChecked()) {
            std::set<std::pair<QString, QString>> sp, sb, sbb;
            for (const Cid &n : prods)
                sp.insert(pk(n));
            for (const Cid &n : boosted)
                sb.insert(pk(n));
            for (const Cid &n : bb_all)
                sbb.insert(pk(n));
            std::set_intersection(sp.begin(), sp.end(), sb.begin(), sb.end(), std::inserter(both_trade, both_trade.end()));
            std::set_intersection(sp.begin(), sp.end(), sbb.begin(), sbb.end(), std::inserter(both_bld, both_bld.end()));
            both.insert(both_trade.begin(), both_trade.end());
            both.insert(both_bld.begin(), both_bld.end());
        }
        boosted.erase(std::remove_if(boosted.begin(), boosted.end(), [&](const Cid &n) { return both_trade.count(pk(n)) > 0; }),
                      boosted.end());
        const bool any_use = !users.empty() || !boosted.empty() || !effs.empty();
        if (goods_on && c.kind == u"prod" && any_use) {
            const Cid bid = put1({u"good"_s, *o, 0});
            box = prodtree::BoxItem{bid, sc.net(nk({u"xmade"_s, *o}), *o, col(*o), port(centre, u"out"_s))};
        }
        if (any_use) {
            const long long ni = sc.net(nk({u"xused"_s, *o}), *o, col(*o), port(box ? box->node : centre, u"out"_s));
            const std::tuple<QString, std::vector<Cid> *, QString, QString> gs[5] = {
                {u"Uses it"_s, &prods, u"in"_s, u"flow"_s},
                {u"Boosted by it in the state"_s, &boosted, u"top"_s, u"boost"_s},
                {u"Works better with it"_s, &effs, u"bottom"_s, u"eff"_s},
                {u"Buildings that cost it"_s, &blds, u"in"_s, u"flow"_s},
                {u"Pops that buy it"_s, &needs, u"in"_s, u"flow"_s}};
            for (const auto &[title_, lst, entry, st] : gs)
                if (!lst->empty())
                    groups.push_back({title_, ni, false, items(*lst, entry, st, title_)});
        }
    }
    if (c.kind == u"bld") {
        const OptCid r = b_of.contains(c.key) ? OptCid(b_of.value(c.key)) : std::nullopt;
        if (r && io.contains(*r) && _free(*r)) {
            const long long ni = sc.net(nk({u"xruns"_s, c.key}), std::nullopt, KIND_RGB.get(u"building"_s)->first,
                                        port(centre, u"out"_s), bname(c.key) + u" runs "_s + nname(*r));
            const Cid cid = put1(*r);
            prodtree::GroupItem it{cid, u"in"_s, ps.spec.value(cid).bout, u"flow"_s, std::nullopt, std::nullopt};
            prodtree::Kids down;
            std::vector<prodtree::Edge> tedges;
            unfold_right(cid, *r, {u"R"_s, u"Runs"_s}, down, tedges);
            if (!down.isEmpty())
                it.tree = prodtree::Unfolded{down, tedges};
            groups.push_back({u"Runs"_s, ni, false, {it}});
        }
    }
    const OptStr bld = bld_of_c;
    if (bld && !bld->isEmpty() && boosts_on) {
        std::vector<Cid> bb;
        for (const QString &k : sorted_info) {
            const Cid pn{u"prod"_s, k, 0};
            if (io.contains(pn) && pn != c && has_bonus(k, u"has_building"_s, bld))
                bb.push_back(pn);
        }
        std::vector<Cid> keep;
        for (const Cid &n : bb)
            if (_free(n) && !both_bld.count(pk(n)))
                keep.push_back(n);
        if (!keep.empty()) {
            const long long ni = sc.net(nk({u"xbb"_s, *bld}), std::nullopt, BOOST_RGB, port(centre, u"bout"_s),
                                        u"a "_s + bname(*bld) + u" in the state"_s);
            groups.push_back({u"Boosted by "_s + (c.kind == u"prod" ? u"its building"_s : u"it"_s) + u" in the state"_s, ni,
                              true, items(keep, u"top"_s, u"boost"_s, u"bb"_s)});
        }
    }
    QString left_t, empty;
    if (c.kind == u"good") {
        left_t = u"Made by"_s;
        empty = u"Nothing in the mod makes it"_s;
    } else if (c.kind == u"prod") {
        left_t = u"What it needs"_s;
        empty = !io.get(c)->ins.empty() ? QString() : u"Needs nothing — a raw resource"_s;
    } else {
        left_t = u"What it costs to build"_s;
        empty = u"Costs nothing to build"_s;
    }
    if (flows_on && io.contains(c) && !(io.get(c)->ins.size() <= 6 || !goods_on))
        left_t += u" (click a good: what makes it)"_s;
    prodtree::explorer_scene(sc, ps.spec, centre, up, edges, top, bottom, box, groups,
                             {left_t, u"What boosts it (in the state)"_s, u"Works better with (efficiency goods)"_s,
                              flows_on ? empty : QString()});
    const prodtree::Rect cr = *sc.rects.get(centre);
    std::optional<double> prev_y;
    bool shift = false;
    std::sort(pills_left.begin(), pills_left.end());   // the boost's value on the combined input line (close ones
    for (const auto &[ty, text] : pills_left) {          // side by side)
        shift = prev_y && ty - *prev_y < 34 ? !shift : false;
        sc.pills.emplace_back(cr[0] - (shift ? 120 : 46), cr[1] + ty, text);
        prev_y = ty;
    }
    return ps;
}

void ProductionWorkspace::_build_all(const OrderedMap<Cid, IO> &io, Pass &ps)
{
    // ⌂ Everything: every good, RGO, artisan, factory, building and pop type in blocks by kind — click one to explore
    // it. The blocks share their number of rows, picked so the whole screen is about as wide as the view.
    const OrderedMap<QString, double> costs = _prices();
    const bool money_on = money->isChecked();
    long long cnt = 0;
    auto put = [&](const Cid &n, QString as_) {
        if ((n.kind == u"bld" || n.kind == u"need") && as_ == u"card")
            as_ = u"chip"_s;
        const Cid cid{n.kind, n.key, ++cnt};
        Look lk = _look(n, io, costs, money_on, as_, std::nullopt);
        ps.spec.insert(cid, lk.spec);
        ps.slots.insert(cid, lk.slots);
        ps.draw.insert(cid, lk.d);
        return cid;
    };
    _centre_cid.reset();
    auto by_key = [&](std::vector<Cid> v) {
        std::stable_sort(v.begin(), v.end(), [&](const Cid &a, const Cid &b) { return _sort_key(a) < _sort_key(b); });
        return v;
    };
    std::vector<Cid> prods, blds, needs;
    for (const auto &kv : io)
        (kv.first.kind == u"prod" ? prods : kv.first.kind == u"bld" ? blds : needs).push_back(kv.first);
    prods = by_key(prods);
    std::vector<std::pair<QString, std::vector<Cid>>> blocks;
    {
        std::vector<QString> gs;
        for (const auto &kv : mod.goods)
            gs.push_back(kv.first);
        std::stable_sort(gs.begin(), gs.end(), [&](const QString &a, const QString &b) { return gname(a) < gname(b); });
        std::vector<Cid> lst;
        for (const QString &g : gs)
            lst.push_back({u"good"_s, g, 0});
        blocks.emplace_back(u"Goods"_s, lst);
    }
    for (const QString &k : {u"rgo"_s, u"artisan"_s, u"factory"_s}) {
        const QString kn = KIND_NAME.value(k, k);
        std::vector<Cid> lst;
        for (const Cid &n : prods)
            if (_info.get(n.key)->kind == k)
                lst.push_back(n);
        blocks.emplace_back(!kn.endsWith(u's') ? kn + u"s"_s : kn, lst);
    }
    {
        std::vector<Cid> lst;
        for (const Cid &n : prods)
            if (!KINDS.contains(_info.get(n.key)->kind))
                lst.push_back(n);
        blocks.emplace_back(u"Other productions"_s, lst);
    }
    std::vector<Cid> bl_only, nd_only;
    for (const Cid &n : blds)
        (n.kind == u"bld" ? bl_only : nd_only).push_back(n);
    for (const Cid &n : needs)
        (n.kind == u"need" ? nd_only : bl_only).push_back(n);
    blocks.emplace_back(u"Buildings"_s, by_key(bl_only));
    blocks.emplace_back(u"Pop needs"_s, by_key(nd_only));
    struct Cell {
        QString t;
        std::vector<Cid> ids;
        double cw, ch;
    };
    std::vector<Cell> cells;
    for (const auto &[t, lst] : blocks) {
        if (lst.empty())
            continue;
        Cell cl{t, {}, 0, 0};
        double mw = -INFINITY, mh = -INFINITY;
        for (const Cid &n : lst) {
            const Cid id = put(n, n.kind == u"bld" || n.kind == u"need" ? u"chip"_s : u"card"_s);
            cl.ids.push_back(id);
            mw = std::max(mw, ps.spec.value(id).w);
            mh = std::max(mh, ps.spec.value(id).h);
        }
        cl.cw = mw + 40.0;
        cl.ch = mh + 34.0;
        cells.push_back(cl);
    }
    const double aspect = std::max(1.0, double(chart->width()) / std::max(1.0, double(chart->height())));
    std::optional<std::pair<double, long long>> best;
    auto ceil_div = [](long long a, long long b) { return -py::floordiv(-a, b); };
    for (long long rows = 3; rows < 40; ++rows) {
        py::Sum wd;
        double ht = -INFINITY;
        for (const Cell &cl : cells) {
            const long long n = static_cast<long long>(cl.ids.size());
            wd.add(double(ceil_div(n, rows)) * cl.cw + 120.0);
            ht = std::max(ht, double(std::min(rows, n)) * cl.ch);
        }
        ht += 80.0;
        const double score = std::fabs(std::log(wd.value() / ht / aspect));
        if (!best || score < best->first)
            best = std::pair{score, rows};
    }
    const long long rows = best->second;
    double x = 40.0;
    for (const Cell &cl : cells) {
        ps.sc->text(x, 0.0, cl.cw * 2, cl.t + u" — "_s + QString::number(cl.ids.size()), Rgb{214, 190, 130}, 26);
        for (size_t i = 0; i < cl.ids.size(); ++i) {
            const long long ii = static_cast<long long>(i);
            const prodtree::Spec sp = ps.spec.value(cl.ids[i]);
            ps.sc->rects.insert(cl.ids[i], {x + double(ii / rows) * cl.cw, 60.0 + double(ii % rows) * cl.ch, sp.w, sp.h});
        }
        x += double(ceil_div(static_cast<long long>(cl.ids.size()), rows)) * cl.cw + 120.0;
    }
}

void ProductionWorkspace::_build_hub(const OrderedMap<Cid, IO> &io, const Cid &c, Pass &ps,
                                     const std::function<Cid(const Cid &, const QString &, const OptStr &)> &put)
{
    // A pop type's needs in the middle: the epicentre — its goods in a ring around it (a sector per kind of need), what
    // makes each further out on the same bearing, every line pointing inwards.
    (void)io;
    prodtree::Scene &sc = *ps.sc;
    static const QHash<QString, QString> SHORT = {
        {u"life_needs"_s, u"Life"_s}, {u"everyday_needs"_s, u"Everyday"_s}, {u"luxury_needs"_s, u"Luxury"_s}};
    std::vector<prodtree::Sector> sectors;
    std::vector<Cid> cards;
    const auto nit = _needs.get(c.key);
    for (const auto &[kind, title_] : NEEDS) {
        std::vector<std::pair<QString, double>> lst;
        if (nit)
            for (const auto &[k, g, a] : *nit)
                if (k == kind)
                    lst.emplace_back(g, a);
        if (lst.empty())
            continue;
        const Rgb rgb = NEED_KIND_RGB.value(kind, Rgb{150, 150, 150});
        cards.push_back(put(c, u"needsec"_s, kind));
        prodtree::Sector sec{title_, rgb, {}};
        for (const auto &[g, a] : lst) {
            const QString what = gname(g) + u" — "_s + py::lower(SHORT.value(kind, kind)) + u" need of "_s +
                                 py::split(nname(c), u" — "_s)[0] + u": "_s + amt(a);
            std::vector<Cid> ms;
            if (flows->isChecked())
                for (const Cid &p_ : _producers.value(g))
                    if (_free(p_))
                        ms.push_back(p_);
            if (!goods_between->isChecked() && !ms.empty()) {   // straight from what makes it
                for (const Cid &p_ : ms) {
                    const Cid pid = put(p_, u"card"_s, std::nullopt);
                    sec.items.push_back({pid, sc.net(nk({u"hub"_s, cid_key(pid)}), g, _gcol(g), port(pid, u"out"_s), what), {}});
                }
                continue;
            }
            const Cid gid = put({u"good"_s, g, 0}, u"card"_s, std::nullopt);
            ps.spec[gid].h = 60.0;
            ps.spec[gid].w = 200.0;
            NodeInfo &dg = *ps.draw.get(gid);
            dg.sub = SHORT.value(kind, kind) + u" need · "_s + amt(a);
            dg.sub_rgb = rgb;
            const long long ni = sc.net(nk({u"hub"_s, cid_key(gid)}), g, _gcol(g), port(gid, u"out"_s), what);
            std::vector<std::pair<Cid, long long>> makers;
            for (const Cid &p_ : ms) {
                const Cid pid = put(p_, u"card"_s, std::nullopt);
                makers.emplace_back(pid, sc.net(nk({u"hubm"_s, cid_key(gid)}), g, _gcol(g), port(pid, u"out"_s)));
            }
            sec.items.push_back({gid, ni, makers});
        }
        sectors.push_back(sec);
    }
    if (cards.empty())
        cards.push_back(put(c, u"needsec"_s, u"life_needs"_s));
    _centre_cid = cards[0];
    const moddata::PopType *pt = mod.poptypes.get(c.key);
    prodtree::hub_scene(sc, ps.spec, cards, sectors, pt ? pt->name : c.key);
}

void ProductionWorkspace::_bars()
{
    // The bar over the chart: the Explorer's trail.
    const bool ex = view == u"explore";
    iso_bar->setVisible(ex);
    crumbs->setVisible(ex);
    back_btn->setVisible(ex && ex_trail.size() > 1);
    iso_label->setVisible(false);
    if (ex)
        _fill_crumbs();
}

void ProductionWorkspace::_fill_crumbs()
{
    while (crumb_lay->count()) {
        QLayoutItem *it = crumb_lay->takeAt(0);
        if (QWidget *w = it ? it->widget() : nullptr) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    crumb_lay->addWidget(label(u"You came:"_s, u"faint"_s, false));
    const size_t last = ex_trail.size() - 1;
    for (size_t i = 0; i < ex_trail.size(); ++i) {
        const Cid n = ex_trail[i];
        if (i)
            crumb_lay->addWidget(label(u"▸"_s, u"sub"_s, false));
        const QString name = nname(n);
        auto *b = new QPushButton(name.size() <= 30 ? name : name.left(28) + u"…"_s);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(i == last);
        Rgb rgb;
        if (n == ALL) {
            rgb = {214, 190, 130};
        } else if (n.kind == u"good") {
            rgb = _gcol(n.key);
        } else {
            QString k;
            if (n.kind == u"prod") {
                const production::Desc *d = _info.get(n.key);
                k = d ? d->kind : QString();
            } else {
                k = n.kind == u"bld" ? u"building"_s : u"need"_s;
            }
            const auto *kr = KIND_RGB.get(k);
            rgb = (kr ? *kr : *KIND_RGB.get(u"factory"_s)).second;
        }
        b->setIcon(widgets::swatch_icon(rgb, 12));
        b->setToolTip(i != last ? u"Back to "_s + name : name + u" — in the middle"_s);
        connect(b, &QPushButton::clicked, this, [this, n] { explore(n); });
        crumb_lay->addWidget(b);
    }
    crumbs->updateGeometry();
}

void ProductionWorkspace::_fit()
{
    // All of it in view — but never so small it can't be read: then the middle card in view, readable.
    ProdChart *C = chart;
    C->fit();
    if (view != u"chart" && C->zoom < 0.42 && _centre_cid) {
        const QRectF *r = C->nodes.get(*_centre_cid);
        if (r) {
            C->zoom = 0.42;
            if (_centre_cid->kind == u"need")        // the needs view: its middle in the middle
                C->off = QPointF(C->width() / 2.0, C->height() / 2.0);
            else
                C->off = QPointF(C->width() * 0.38 - r->center().x() * C->zoom, C->height() / 2.0 - r->center().y() * C->zoom);
            C->update();
        }
    }
}

void ProductionWorkspace::set_view()
{
    // (Only the Explorer is left.)
    if (ex_trail.empty())
        ex_trail = {ALL};
    _rebuild();
    _fit();
    _fill_side();
}

void ProductionWorkspace::explore(const Cid &n0)
{
    // Move n to the middle of the Explorer (it joins the trail; back on the trail: the rest goes).
    const Cid n = base(n0);
    auto it = std::find(ex_trail.begin(), ex_trail.end(), n);
    if (it != ex_trail.end()) {
        ex_trail.erase(it + 1, ex_trail.end());
    } else {
        std::vector<Cid> rest;
        for (const Cid &x : ex_trail)
            if (x != ALL)
                rest.push_back(x);
        std::vector<Cid> t{ALL};
        const size_t from = rest.size() > 10 ? rest.size() - 10 : 0;
        t.insert(t.end(), rest.begin() + static_cast<std::ptrdiff_t>(from), rest.end());
        if (n != ALL)
            t.push_back(n);
        ex_trail = t;
    }
    sel = n == ALL ? OptCid() : OptCid(n);
    multi.clear();
    if (n.kind != u"good" && n.kind != u"all")
        multi.insert(pk(n));
    _rebuild();
    _fit();
    _fill_side();
}

void ProductionWorkspace::_ex_back()
{
    if (ex_trail.size() > 1)
        explore(ex_trail[ex_trail.size() - 2]);
}

void ProductionWorkspace::_toggle_open(const Cid &cid)
{
    // A card's ＋ / −: unfold what comes after (before) it, or fold it back (with all it unfolded) — the card stays where
    // it is on the screen.
    const NodeInfo *d = _draw.get(cid);
    if (!d || !d->path || d->path->isEmpty())
        return;
    const QStringList path = *d->path;
    ProdChart *C = chart;
    const QRectF *r = C->nodes.get(cid);
    std::optional<QPointF> at;
    if (r)
        at = QPointF(r->left() * C->zoom + C->off.x(), r->top() * C->zoom + C->off.y());
    const OptCid ce = _ex_centre();
    std::set<QStringList> &opened = ex_open[ce ? pk(*ce) : std::pair<QString, QString>{}];
    if (opened.count(path)) {
        for (auto q = opened.begin(); q != opened.end();)
            if (q->mid(0, path.size()) == path)
                q = opened.erase(q);
            else
                ++q;
    } else {
        opened.insert(path);
    }
    _rebuild();
    OptCid n2;
    for (const auto &[n, dd] : _draw)
        if (dd.path == path) {
            n2 = n;
            break;
        }
    const QRectF *r2 = n2 ? C->nodes.get(*n2) : nullptr;
    if (at && r2) {
        C->off = QPointF(at->x() - r2->left() * C->zoom, at->y() - r2->top() * C->zoom);
        C->update();
    }
}

// --- parking
void ProductionWorkspace::_save_parked()
{
    try {
        QStringList v;
        for (const auto &p : parked)
            v << nid_str(from_pk(p));
        std::sort(v.begin(), v.end());
        settings_::settings()->setValue(u"prodParked"_s, v);
    } catch (const std::exception &) {
    }
}

void ProductionWorkspace::park(const Cid &key)
{
    const Cid n = base(key);
    parked.insert(pk(n));
    multi.erase(pk(n));
    if (sel && *sel == n)
        sel.reset();
    _save_parked();
    _rebuild();
    _fill_side();
}

void ProductionWorkspace::unpark(const Cid &key)
{
    parked.erase(pk(base(key)));
    _save_parked();
    _rebuild();
}

void ProductionWorkspace::_search_pick(const QString &text)
{
    auto it = _search_map.find(text);
    if (it != _search_map.end()) {                   // (after the completer has finished with the pick: it rebuilds)
        const Cid n = it->second;
        QTimer::singleShot(0, this, [this, n] {
            search->clear();
            explore(n);
        });
    }
}

void ProductionWorkspace::_focus()
{
    // The selected card's chain (Python works it out and shows none of it: the focus stays empty).
    chart->multi.clear();
    if (multi.size() > 1)                            // several: the union of their chains
        for (const auto &p : multi)
            chart->multi.insert(from_pk(p));
    chart->sel = sel;
    chart->focus.clear();
    chart->update();
}

QSet<Cid> ProductionWorkspace::_chain(const OptCid &sel_) const
{
    QSet<Cid> f;
    if (!sel_ || !chart->nodes.contains(*sel_))
        return f;
    const Cid s = *sel_;
    f.insert(s);
    if (s.kind != u"good") {
        std::vector<Cid> todo{s};
        QSet<Cid> seen;
        while (!todo.empty()) {
            const Cid n = todo.back();
            todo.pop_back();
            if (seen.contains(n) || !_io.contains(n))
                continue;
            seen.insert(n);
            f.insert(n);
            for (const auto &[g, _a] : _io.get(n)->ins) {
                f.insert({u"good"_s, g, 0});
                for (const Cid &x : _producers.value(g))
                    todo.push_back(x);
            }
        }
        const IO *x = _io.get(s);
        if (x && x->out && !x->out->isEmpty()) {
            f.insert({u"good"_s, *x->out, 0});
            for (const Cid &y : _consumers.value(*x->out))
                f.insert(y);
        }
        for (const Meta &m : chart->meta)
            if (m.tgts.contains(s) && m.trunk != u"flow")
                for (const auto &[q, _p] : m.src)
                    f.insert(q);
    } else {
        for (const Cid &y : _producers.value(s.key))
            f.insert(y);
        for (const Cid &y : _consumers.value(s.key))
            f.insert(y);
    }
    return f;
}

void ProductionWorkspace::_picked(const QString &kind, const QString &key, bool ctrl)
{
    const OptCid n = !kind.isEmpty() ? OptCid(Cid{kind, key, 0}) : std::nullopt;
    if (ctrl && n && kind != u"good") {
        if (multi.empty() && sel && sel->kind != u"good")
            multi.insert(pk(*sel));
        if (multi.count(pk(*n)))                     // Ctrl+click adds / takes away
            multi.erase(pk(*n));
        else
            multi.insert(pk(*n));
        // (Python: next(iter(set)) — the set's own order; here the smallest)
        sel = multi.count(pk(*n)) ? n : (!multi.empty() ? OptCid(from_pk(*multi.begin())) : std::nullopt);
    } else {
        multi.clear();
        if (n && kind != u"good")
            multi.insert(pk(*n));
        sel = n;
    }
    _focus();
    _fill_side();
}

// ------------------------------------------------------------------ side: what's selected
void ProductionWorkspace::_fill_side()
{
    _clear_body();
    if (!whatif.empty() && edits)
        _whatif_strip();
    if (sel && sel->kind == u"tmpl" && edits && edits->db.entries.contains(sel->key)) {
        _fill_tmpl(sel->key);
        return;
    }
    if (multi.size() > 1 && edits) {
        _fill_multi();
        return;
    }
    if (!sel || !edits) {
        title->setText(u"Click a card or a good in the chart"_s);
        sub->setText(u"Green-brown = RGO, blue = artisans, red = factory, grey-blue = building, violet / gold = pop needs · "
                     u"one line per good: it splits where it goes into a card — left = input, top = boost (magenta), "
                     u"bottom = efficiency (cyan) · hover a line or a card to trace it · click = select it (all its "
                     u"copies light up) · double-click = move it to the middle · its ＋ unfolds what comes after (before) "
                     u"it · ◉ or right-click = park · Ctrl+click = select several · wheel = zoom, drag the background = "
                     u"move · ⌂ Everything on the trail: all of it"_s);
        return;
    }
    const Cid s = *sel;
    if (s.kind == u"good")
        _fill_good(s.key);
    else if (s.kind == u"bld" && edits->buildings().contains(s.key))
        _fill_building(s.key);
    else if (s.kind == u"need")
        _fill_need(s.key);
    else if (s.kind == u"prod" && edits->exists(s.key))
        _fill_prod(s.key);
}

void ProductionWorkspace::_node_list(Section *s, const std::vector<Cid> &nodes, int rows)
{
    auto *lst = new QListWidget();
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const Cid &n : nodes) {
        QString t = nname(n);
        if (n.kind == u"prod") {
            const production::Desc *d = _info.get(n.key);
            t += u"  ·  "_s + KIND_NAME.value(d ? d->kind : QString(), u"?"_s);
        } else if (n.kind == u"bld") {
            t += u"  ·  building"_s;
        }
        auto *it = new QListWidgetItem(t);
        it->setData(Qt::UserRole, QStringList{n.kind, n.key});
        lst->addItem(it);
    }
    lst->setFixedHeight(std::max(1, std::min(rows, lst->count())) * (lst->count() ? lst->sizeHintForRow(0) : 24) + 6);
    connect(lst, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QStringList d = it->data(Qt::UserRole).toStringList();
        _picked(d.value(0), d.value(1));
    });
    s->lay->addWidget(lst);
}

void ProductionWorkspace::_fill_multi()
{
    std::vector<Cid> keys;
    for (const auto &p : multi)
        keys.push_back(from_pk(p));
    std::stable_sort(keys.begin(), keys.end(), [&](const Cid &a, const Cid &b) { return nname(a) < nname(b); });
    title->setText(u"%1 cards selected"_s.arg(keys.size()));
    sub->setText(u"Ctrl+click adds or takes one away · a click without Ctrl selects just one"_s);
    auto *s = new Section();
    _node_list(s, keys, 10);
    auto *row = new QHBoxLayout();
    auto *b2 = new QPushButton(u"Park these"_s);
    connect(b2, &QPushButton::clicked, this, [this, keys] {
        for (const Cid &k : keys)
            park(k);
    });
    row->addWidget(b2);
    auto *b3 = new QPushButton(u"Clear the selection"_s);
    connect(b3, &QPushButton::clicked, this, [this] { _picked(QString(), QString()); });
    row->addWidget(b3);
    s->lay->addLayout(row);
    bl->addWidget(s);
}

// ------------------------------------------------------------------ prices: the file's, or a what-if one
double ProductionWorkspace::price(const QString &g) const
{
    // The price the sums use: a what-if price where one is set, else goods.txt's (as edited).
    auto it = whatif.find(g);
    return it != whatif.end() ? it->second : edits->cost(g);
}

OrderedMap<QString, double> ProductionWorkspace::_prices() const
{
    OrderedMap<QString, double> out = edits->costs();
    for (const auto &[g, v] : whatif)                // (Python: dict.update — in the what-if dict's own order)
        out.insert(g, v);
    return out;
}

void ProductionWorkspace::_whatif_set(const QString &g, const QString &v)
{
    const std::optional<double> x = production::num_or_none(v);
    if (!x)
        return;
    if (std::fabs(*x - edits->cost(g)) < 1e-12)
        whatif.erase(g);                             // (back at the real price: no what-if)
    else
        whatif[g] = *x;
    _rebuild();
    _fill_side();
}

void ProductionWorkspace::_whatif_reset()
{
    whatif.clear();
    _rebuild();
    _fill_side();
}

void ProductionWorkspace::_whatif_strip()
{
    // At the top of the side panel while what-if prices are set: which, and Reset.
    auto *s = new Section();
    s->setStyleSheet(u"QFrame#section { background: #3a2c10; border: 2px solid %1; border-radius: 6px; }"_s.arg(WHATIF_COL));
    auto *row = new QHBoxLayout();
    QStringList parts;
    for (const auto &[g, v] : whatif)
        parts << gname(g) + u" "_s + mny(v) + u" (real "_s + mny(edits->cost(g)) + u")"_s;
    QLabel *lb = label(u"<b style='color:"_s + WHATIF_COL + u"'>What-if prices</b> — "_s + QString::number(whatif.size()) +
                           u" good"_s + (whatif.size() == 1 ? QString() : u"s"_s) + u": "_s + parts.join(u", "_s) +
                           u". Only for these sums: never written to goods.txt, gone when the editor closes."_s,
                       u"sub"_s);
    lb->setTextFormat(Qt::RichText);
    row->addWidget(lb, 1);
    auto *b = new QPushButton(u"Reset"_s);
    b->setToolTip(u"Every what-if price back to the real one (goods.txt)"_s);
    connect(b, &QPushButton::clicked, this, [this] { _whatif_reset(); });
    row->addWidget(b, 0, Qt::AlignTop);
    s->lay->addLayout(row);
    bl->addWidget(s);
}

QWidget *ProductionWorkspace::_price_field(const QString &g, int width, bool wi)
{
    // A price box. whatif: a what-if price (amber; only for the sums, never written) — the factory views; else the
    // good's real price in goods.txt (its own page).
    production::ProdEdits *ed = edits;
    if (wi) {
        auto *f = new ValueField(u"number"_s, std::nullopt, gfmt(price(g)), u"1.5"_s);
        f->setFixedWidth(width);
        const bool on = whatif.count(g) > 0;
        f->setStyleSheet(u"QLineEdit { border: 2px solid %1; color: %2; }"_s.arg(WHATIF_COL, on ? WHATIF_COL : theme::INK));
        f->setToolTip(u"What-if price of "_s + gname(g) +
                      u" — only for these sums, never written to goods.txt (real price "_s + mny(ed->cost(g)) +
                      u"; change the real one on the good's own page)"_s +
                      (on ? u" · set to "_s + mny(whatif[g]) + u" now"_s : QString()));
        connect(f, &ValueField::committed, this, [this, g](const QString &v) { _whatif_set(g, v); });
        return f;
    }
    auto *f = new ValueField(u"number"_s, std::nullopt, gfmt(ed->cost(g)), u"1.5"_s);
    f->setFixedWidth(width);
    f->setToolTip(u"The real price in goods.txt (written on Apply)"_s);
    connect(f, &ValueField::committed, this, [this, ed, g](const QString &v) {
        if (production::num_or_none(v) && num(v) != ed->cost(g))
            Q_EMIT other(u"price of "_s + g + u": "_s + v, u"cost"_s, g, v);
    });
    return f;
}

void ProductionWorkspace::_fill_good(const QString &g)
{
    production::ProdEdits &ed = *edits;
    const moddata::Good *e = mod.goods.get(g);
    title->setText(gname(g));
    sub->setText(g + u" · "_s + pretty(e ? e->group : QString()) +
                 (ed.cost_want.contains(g) ? u"  · price changed, written on Apply"_s : QString()));
    auto *s = new Section();
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"Price (goods.txt: cost)"_s, u"sub"_s, false));
    row->addWidget(_price_field(g));
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(label(u"The base price the markets start from — every production's money strip and every building's "
                            u"cost use it."_s,
                            u"faint"_s));
    bl->addWidget(s);
    s = new Section();
    QStringList prods;
    for (const auto &kv : _info)
        prods << kv.first;
    std::sort(prods.begin(), prods.end());
    std::vector<Cid> made, used, better, blds;
    for (const QString &p : prods) {
        const production::Desc &d = *_info.get(p);
        if (d.output == OptStr(g))
            made.push_back({u"prod"_s, p, 0});
        if (std::any_of(d.inputs.begin(), d.inputs.end(), [&](const auto &x) { return x.first == g; }))
            used.push_back({u"prod"_s, p, 0});
        if (std::any_of(d.efficiency.begin(), d.efficiency.end(), [&](const auto &x) { return x.first == g; }))
            better.push_back({u"prod"_s, p, 0});
    }
    for (const QString &b : ed.buildings()) {
        const Goods cost = ed.bld_info(b).cost;
        if (std::any_of(cost.begin(), cost.end(), [&](const auto &x) { return x.first == g; }))
            blds.push_back({u"bld"_s, b, 0});
    }
    const std::pair<QString, std::vector<Cid> *> lists[4] = {
        {u"Made by"_s, &made}, {u"Used by"_s, &used}, {u"Makes it work better (efficiency)"_s, &better},
        {u"Buildings that cost it"_s, &blds}};
    for (const auto &[t, ks] : lists) {
        s->lay->addWidget(label(t + u" ("_s + QString::number(ks->size()) + u")"_s, u"sub"_s));
        _node_list(s, *ks);
    }
    QStringList pops;
    for (const QString &pt : ed.poptypes()) {
        const OrderedMap<QString, Goods> nd = ed.needs(pt);
        for (const auto &[kind, t] : NEEDS) {
            std::optional<double> a;
            for (const auto &[x, y] : nd.value(kind))
                if (x == g) {
                    a = y;
                    break;
                }
            if (a) {
                const moddata::PopType *p = mod.poptypes.get(pt);
                pops << (p ? p->name : pt) + u" "_s + gfmt(*a) + u" ("_s + py::lower(py::split(t)[0]) + u")"_s;
            }
        }
    }
    s->lay->addWidget(label(u"Pops buying it ("_s + QString::number(pops.size()) + u")"_s, u"sub"_s));
    s->lay->addWidget(label(pops.isEmpty() ? u"none"_s : pops.join(u" · "_s), u"faint"_s));
    _view_buttons(s, {u"good"_s, g, 0}, false);
    bl->addWidget(s);
}

using Emit = std::function<void(const QString &, const QString &)>;

static const Node *rootn(const QString &text, std::shared_ptr<const Node> &keep)
{
    keep = techdata::root(text);
    return keep.get();
}

static std::optional<qsizetype> block_index(const Node *r, const QString &block)
{
    if (!r)
        return std::nullopt;
    for (size_t i = 0; i < r->kids.size(); ++i)
        if (is(r->kids[i], block) && r->kids[i].is_block)
            return static_cast<qsizetype>(i);
    return std::nullopt;
}

static std::optional<std::vector<qsizetype>> good_path(const QString &text, const QString &block, const QString &gg)
{
    // (bi, ci): the block's place in the text's root block, the good's place in it (Python: next(...) — None here)
    std::shared_ptr<const Node> keep;
    const Node *r = rootn(text, keep);
    const auto bi = block_index(r, block);
    if (!bi)
        return std::nullopt;
    const Node &b = r->kids[static_cast<size_t>(*bi)];
    for (size_t i = 0; i < b.kids.size(); ++i)
        if (is(b.kids[i], gg))
            return std::vector<qsizetype>{*bi, static_cast<qsizetype>(i)};
    return std::nullopt;
}

static QComboBox *add_box(const std::vector<std::pair<QString, QString>> &items, const QSet<QString> &have, const QString &hint)
{
    auto *box = new QComboBox();
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[g, t] : items)
        if (!have.contains(g))
            box->addItem(t, g);
    box->setCurrentIndex(-1);
    box->lineEdit()->setPlaceholderText(hint);
    techpanel::searchable(box);
    return box;
}

static std::optional<QString> picked_good(QComboBox *box)
{
    const int i = box->findText(py::strip(box->currentText()));
    if (i < 0)
        return std::nullopt;
    return box->itemData(i).toString();
}

void ProductionWorkspace::_io_section(const QString &key, const production::Desc &d, const Emit &emit_,
                                      const std::function<void(const QString &, const QString &)> &set_in,
                                      const std::function<void(const QString &)> &set_out)
{
    // What it takes and makes, a day per level, at the base prices — each input good (it can be swapped), its amount,
    // price and worth, the output the same way, then what's left. Amounts and prices are edited in place.
    production::ProdEdits *ed = edits;
    auto *s = new Section();
    s->lay->addWidget(label(u"Inputs and output (a day per level, at the base prices)"_s));
    auto *hd = new QHBoxLayout();
    hd->addWidget(label(u"Good"_s, u"faint"_s, false), 1);
    for (const auto &[t_, w_] : {std::pair{u"Amount"_s, 70}, {u"Price"_s, 70}, {u"Worth"_s, 50}}) {
        QLabel *lb = label(t_, u"faint"_s, false);
        lb->setFixedWidth(w_);
        hd->addWidget(lb);
    }
    hd->addSpacing(32);
    s->lay->addLayout(hd);
    double cin = 0.0;
    for (const auto &[g, a] : d.inputs) {
        const double c = price(g);
        cin += a * c;
        auto *row = new QHBoxLayout();
        auto *gf = new ValueField(u"choice"_s, goods_items(), g);
        gf->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const QString gg = g;
        connect(gf, &ValueField::committed, this, [ed, key, gg, emit_](const QString &v) {
            const QString t = ed->text(key);
            const auto p = good_path(t, u"input_goods"_s, gg);
            if (p)
                emit_(techdata::replace_key(t, *p, v), gg + u" → "_s + v);
        });
        row->addWidget(gf, 1);
        row->addWidget(_amount_field(a, [set_in, gg](const QString &v) { set_in(gg, v); }));
        row->addWidget(_price_field(g, 70, true));
        QLabel *wl = label(mny(a * c), u"sub"_s, false);
        wl->setFixedWidth(50);
        row->addWidget(wl);
        QPushButton *x = x_button();
        connect(x, &QPushButton::clicked, this, [ed, key, gg, emit_] {
            const QString t = ed->text(key);
            const auto p = good_path(t, u"input_goods"_s, gg);
            if (p)
                emit_(techdata::remove_node(t, *p), u"no "_s + gg);
        });
        row->addWidget(x);
        s->lay->addLayout(row);
    }
    // add an input
    QSet<QString> have;
    for (const auto &[g, _a] : d.inputs)
        have.insert(g);
    QComboBox *box = add_box(goods_items(), have, u"＋ Add an input good — click for the list, or type (starts at 1)"_s);
    connect(box, &QComboBox::activated, this, [ed, key, box, emit_](int) {
        const auto g = picked_good(box);
        if (!g)
            return;
        const QString t = ed->text(key);
        std::shared_ptr<const Node> keep;
        const auto bi = block_index(rootn(t, keep), u"input_goods"_s);
        emit_(!bi ? techdata::add_block(t, {}, u"input_goods"_s, {*g + u" = 1"_s}) : techdata::add_line(t, {*bi}, *g, u"1"_s),
              u"+ "_s + *g);
    });
    s->lay->addWidget(box);
    // the output
    // efficiency goods (factories): bought every day as well — their amounts are edited below (template / own)
    double ceff = 0.0;
    const Goods eff_l = d.kind == u"factory" ? d.efficiency : Goods{};
    if (!eff_l.empty()) {
        const QString src = d.has_own_eff ? u"its own list"_s : u"template "_s + (d.tmpl && !d.tmpl->isEmpty() ? *d.tmpl : u"?"_s);
        s->lay->addWidget(label(u"Efficiency goods (from "_s + src + u")"_s, u"faint"_s, false));
        for (const auto &[g, a] : eff_l) {
            const double c = price(g);
            ceff += a * c;
            auto *row = new QHBoxLayout();
            QLabel *gl = label(gname(g), u"sub"_s, false);
            gl->setStyleSheet(u"color: %1;"_s.arg(css_rgb(EFF_RGB)));
            row->addWidget(gl, 1);
            QLabel *al = label(gfmt(a), u"sub"_s, false);
            al->setFixedWidth(70);
            al->setToolTip(u"Set in "_s + src + u" (below: Efficiency goods)"_s);
            row->addWidget(al);
            row->addWidget(_price_field(g, 70, true));
            QLabel *wl = label(mny(a * c), u"sub"_s, false);
            wl->setFixedWidth(50);
            row->addWidget(wl);
            row->addSpacing(32);
            s->lay->addLayout(row);
        }
    }
    double cout = 0.0;
    if (d.output && !d.output->isEmpty()) {
        const double c = price(*d.output);
        cout = d.value * c;
        auto *row = new QHBoxLayout();
        QLabel *ol = label(u"→ "_s + gname(*d.output), u"sub"_s, false);
        ol->setStyleSheet(u"font-weight: 600;"_s);
        row->addWidget(ol, 1);
        row->addWidget(_amount_field(d.value, set_out));
        row->addWidget(_price_field(*d.output, 70, true));
        QLabel *wl = label(mny(cout), u"sub"_s, false);
        wl->setFixedWidth(50);
        row->addWidget(wl);
        row->addSpacing(32);
        s->lay->addLayout(row);
    }
    const double diff = cout - cin - ceff;
    QLabel *res = label(u"Inputs "_s + mny(cin) + (!eff_l.empty() ? u" + efficiency goods "_s + mny(ceff) : QString()) +
                            u" · output "_s + mny(cout) + u" · left: "_s + (diff >= 0 ? u"+"_s : QString()) + mny(diff),
                        u"sub"_s);
    res->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(diff >= 0 ? u"#78e678"_s : u"#ff786e"_s));
    s->lay->addWidget(res);
    if (!eff_l.empty()) {                            // the other case: without them (measured: 75 %, nothing spent)
        const double d2 = cout * production::EFF_MISSING - cin;
        QLabel *alt = label(u"Without its efficiency goods: output "_s + mny(cout * production::EFF_MISSING) +
                                u" (75 %), nothing spent on them · left: "_s + (d2 >= 0 ? u"+"_s : QString()) + mny(d2) +
                                (d2 > diff ? u"  — better off without them at these prices"_s : QString()),
                            u"sub"_s);
        alt->setStyleSheet(u"color: %1;"_s.arg(d2 >= 0 ? u"#9fd89f"_s : u"#e8a09a"_s));
        s->lay->addWidget(alt);
    }
    bool wi = false;
    for (const auto &[g, _a] : d.inputs)
        wi = wi || whatif.count(g);
    for (const auto &[g, _a] : eff_l)
        wi = wi || whatif.count(g);
    if (d.output && !d.output->isEmpty())
        wi = wi || whatif.count(*d.output);
    if (!whatif.empty() && wi)
        s->lay->addWidget(label(u"⚠ With what-if prices (amber) — not the prices in goods.txt."_s, u"sub"_s));
    bl->addWidget(s);
}

QWidget *ProductionWorkspace::_amount_field(double a, const std::function<void(const QString &)> &commit, int width)
{
    auto *f = new ValueField(u"number"_s, std::nullopt, gfmt(a), u"1"_s);
    f->setFixedWidth(width);
    connect(f, &ValueField::committed, this, [a, commit](const QString &v) {
        if (production::num_or_none(v) && num(v) != a)
            commit(v);
    });
    return f;
}

void ProductionWorkspace::_fill_prod(const QString &key)
{
    production::ProdEdits *ed = edits;
    const QString text = ed->text(key);
    const production::Desc d = ed->info(key);       // (its template as edited)
    std::shared_ptr<const Node> keep;
    const Node *r = rootn(text, keep);
    QHash<QString, qsizetype> idx;
    if (r)
        for (size_t i = 0; i < r->kids.size(); ++i)
            if (r->kids[i].has_key && !r->kids[i].key.isEmpty())
                idx.insert(r->kids[i].key, static_cast<qsizetype>(i));
    title->setText(_names.value(key, key));
    const OptStr b = ed->building_of(key);
    sub->setText(KIND_NAME.value(d.kind, u"?"_s) + u" · "_s + key +
                 (b && !b->isEmpty() ? u" · building: "_s + *b
                                     : (d.kind == u"factory" ? u" · ⚠ no factory building uses it"_s : QString())) +
                 (ed->added.contains(key) ? u"  · new, written on Apply"_s
                                          : (ed->want.contains(key) ? u"  · changed, written on Apply"_s : QString())));
    const Emit emit_ = [this, ed, key](const QString &nw, const QString &what) {
        if (nw != ed->text(key))
            Q_EMIT edit(key + u": "_s + what, key, nw);
    };
    // --- what it makes
    auto *s = new Section();
    s->lay->addWidget(label(u"Makes"_s));
    auto *row = new QHBoxLayout();
    auto *out = new ValueField(u"choice"_s, goods_items(), d.output.value_or(QString()));
    connect(out, &ValueField::committed, this, [ed, key, idx, emit_](const QString &v) {
        const QString t = ed->text(key);
        emit_(idx.contains(u"output_goods"_s) ? techdata::set_value(t, {idx.value(u"output_goods"_s)}, v)
                                              : techdata::add_line(t, {}, u"output_goods"_s, v),
              u"makes "_s + v);
    });
    row->addWidget(out, 2);
    s->lay->addLayout(row);
    std::vector<std::pair<QString, QString>> tpls;
    for (const auto &[t, v] : ed->db.templates)
        if (v.type == OptStr(d.kind))
            tpls.emplace_back(t, t);
    if (d.kind != u"artisan" && !tpls.empty()) {
        row = new QHBoxLayout();
        row->addWidget(label(u"Template"_s, u"sub"_s, false));
        auto *tp = new ValueField(u"choice"_s, tpls, d.tmpl.value_or(QString()));
        connect(tp, &ValueField::committed, this, [ed, key, idx, emit_](const QString &v) {
            const QString t = ed->text(key);
            emit_(idx.contains(u"template"_s) ? techdata::set_value(t, {idx.value(u"template"_s)}, v)
                                              : techdata::add_line(t, {}, u"template"_s, v),
                  u"template "_s + v);
        });
        row->addWidget(tp, 1);
        s->lay->addLayout(row);
        const production::Template *tt = ed->db.templates.get(d.tmpl.value_or(QString()));
        if (tt && tt->workforce && !tt->workforce->isEmpty())
            s->lay->addWidget(label(u"Workforce "_s + *tt->workforce +
                                        u" per level (from the template: owners, workers, efficiency)."_s,
                                    u"faint"_s));
    }
    auto yes_no = [ed, key, idx, emit_](const QString &kk, bool on, const QString &what) {
        const QString t = ed->text(key);
        emit_(on || idx.contains(kk) ? (idx.contains(kk) ? techdata::set_value(t, {idx.value(kk)}, on ? u"yes"_s : u"no"_s)
                                                         : techdata::add_line(t, {}, kk, u"yes"_s))
                                     : t,
              what + u" "_s + (on ? u"yes"_s : u"no"_s));
    };
    if (d.kind == u"rgo") {
        row = new QHBoxLayout();
        for (const auto &[k2, t2] : {std::pair{u"farm"_s, u"Farm"_s}, {u"mine"_s, u"Mine"_s}}) {
            auto *cb = new QCheckBox(t2);
            cb->setChecked(k2 == u"farm" ? d.farm : d.mine);
            const QString kk = k2;
            connect(cb, &QCheckBox::clicked, this, [yes_no, kk](bool on) { yes_no(kk, on, kk); });
            row->addWidget(cb);
        }
        row->addStretch(1);
        s->lay->addLayout(row);
    }
    if (d.kind == u"factory") {
        auto *cb = new QCheckBox(u"Coastal states only (is_coastal)"_s);
        cb->setChecked(d.coastal);
        connect(cb, &QCheckBox::clicked, this, [yes_no](bool on) { yes_no(u"is_coastal"_s, on, u"coastal"_s); });
        s->lay->addWidget(cb);
    }
    bl->addWidget(s);
    // --- inputs and money: one table
    auto set_in = [ed, key, emit_](const QString &gg, const QString &v) {
        const QString t = ed->text(key);
        const auto p = good_path(t, u"input_goods"_s, gg);
        if (p)
            emit_(techdata::set_value(t, *p, v), gg + u" "_s + v);
    };
    auto set_out = [ed, key, idx, emit_](const QString &v) {
        const QString t = ed->text(key);
        emit_(idx.contains(u"value"_s) ? techdata::set_value(t, {idx.value(u"value"_s)}, v) : techdata::add_line(t, {}, u"value"_s, v),
              u"amount "_s + v);
    };
    _io_section(key, d, emit_, set_in, set_out);
    // --- efficiency (factories): whose list it is, and what an edit changes
    if (d.kind == u"factory")
        _eff_scope(key, d, idx, emit_);
    // --- boosts
    s = new Section();
    s->lay->addWidget(label(u"Boosts (bonus)"_s));
    if (d.bonuses.empty())
        s->lay->addWidget(label(u"None."_s, u"faint"_s));
    QStringList buildings;
    for (const auto &kv : ed->db.building_text)
        buildings << kv.first;
    std::sort(buildings.begin(), buildings.end());
    for (const production::Bonus &bn : d.bonuses) {
        row = new QHBoxLayout();
        const std::pair<QString, QString> trig = !bn.conds.empty() ? bn.conds[0] : std::pair{u"trade_goods_in_state"_s, QString()};
        auto *kind = new QComboBox();
        kind->addItem(u"Trade good in the state"_s, u"trade_goods_in_state"_s);
        kind->addItem(u"Building in the state"_s, u"has_building"_s);
        kind->setCurrentIndex(std::max(0, kind->findData(trig.first)));
        kind->setEnabled(bn.conds.size() <= 1);
        row->addWidget(kind);
        std::vector<std::pair<QString, QString>> opts;
        if (trig.first == u"trade_goods_in_state")
            opts = goods_items();
        else
            for (const QString &x : buildings) {
                const QString *l = mod.loc.get(x);
                opts.emplace_back(x, (l && !l->isEmpty() ? *l : pretty(x)) + u"  ("_s + x + u")"_s);
            }
        auto *tgt = new ValueField(u"choice"_s, opts, trig.second);
        row->addWidget(tgt, 1);
        auto *v = new ValueField(u"number"_s, std::nullopt, bn.value, u"0.25"_s);
        v->setFixedWidth(70);
        row->addWidget(v);
        QPushButton *x = x_button();
        row->addWidget(x);
        s->lay->addLayout(row);
        const qsizetype bi = static_cast<qsizetype>(bn.index);
        auto set_trigger = [ed, key, bi, emit_](const QString &kk, const QString &vv) {
            QString t = ed->text(key);
            std::shared_ptr<const Node> kp;
            const Node *rr = rootn(t, kp);
            if (!rr || bi >= static_cast<qsizetype>(rr->kids.size()))
                return;
            const Node &bnode = rr->kids[static_cast<size_t>(bi)];
            std::optional<qsizetype> tr;
            for (size_t j = 0; j < bnode.kids.size(); ++j)
                if (is(bnode.kids[j], u"trigger"_s)) {
                    tr = static_cast<qsizetype>(j);
                    break;
                }
            if (!tr)
                return;
            const Node &tn = bnode.kids[static_cast<size_t>(*tr)];
            if (tn.is_block ? !tn.kids.empty() : !tn.value.isEmpty()) {
                t = techdata::remove_node(t, {bi, *tr, 0});
                t = techdata::add_line(t, {bi, *tr}, kk, vv);
            } else {
                t = techdata::add_line(t, {bi, *tr}, kk, vv);
            }
            emit_(t, u"boost: "_s + kk + u" = "_s + vv);
        };
        auto set_val = [ed, key, bi, emit_](const QString &vv) {
            const QString t = ed->text(key);
            std::shared_ptr<const Node> kp;
            const Node *rr = rootn(t, kp);
            if (!rr || bi >= static_cast<qsizetype>(rr->kids.size()))
                return;
            const Node &bnode = rr->kids[static_cast<size_t>(bi)];
            for (size_t j = 0; j < bnode.kids.size(); ++j)
                if (is(bnode.kids[j], u"value"_s)) {
                    emit_(techdata::set_value(t, {bi, static_cast<qsizetype>(j)}, vv), u"boost "_s + vv);
                    return;
                }
        };
        connect(tgt, &ValueField::committed, this, [kind, set_trigger](const QString &vv) {
            set_trigger(kind->currentData().toString(), vv);
        });
        const QString first_good = mod.goods.isEmpty() ? QString() : mod.goods.begin()->first;
        const QString first_bld = buildings.value(0);
        connect(kind, &QComboBox::activated, this, [kind, set_trigger, first_good, first_bld](int) {
            const QString cd = kind->currentData().toString();
            set_trigger(cd, cd == u"trade_goods_in_state" ? first_good : first_bld);
        });
        connect(v, &ValueField::committed, this, set_val);
        connect(x, &QPushButton::clicked, this, [ed, key, bi, emit_] {
            emit_(techdata::remove_node(ed->text(key), {bi}), u"boost removed"_s);
        });
    }
    auto *add = new QPushButton(u"＋ Add a boost"_s);
    add->setToolTip(u"bonus = { trigger = { … } value = 0.25 } — +25% output when it's in the state"_s);
    const QString g0 = !d.inputs.empty() ? d.inputs[0].first : (mod.goods.isEmpty() ? QString() : mod.goods.begin()->first);
    connect(add, &QPushButton::clicked, this, [ed, key, g0, emit_] {
        emit_(techdata::add_block(ed->text(key), {}, u"bonus"_s,
                                  {u"trigger = { trade_goods_in_state = "_s + g0 + u" }"_s, u"value = 0.25"_s}),
              u"+ boost"_s);
    });
    s->lay->addWidget(add);
    bl->addWidget(s);
    // --- file / delete
    s = new Section();
    auto *fp = new techpanel::FilePreview();
    fp->set_text(text);
    s->lay->addWidget(fp);
    auto *raw = new techpanel::RawBox();
    raw->hide();
    connect(raw, &techpanel::RawBox::saved, this, [emit_](const QString &t) { emit_(t, u"edited as text"_s); });
    row = new QHBoxLayout();
    auto *rb = new QPushButton(u"Edit as text…"_s);
    connect(rb, &QPushButton::clicked, this, [raw, ed, key] { raw->open(key, ed->text(key)); });
    row->addWidget(rb);
    row->addStretch(1);
    s->lay->addLayout(row);
    _view_buttons(s, {u"prod"_s, key, 0});
    auto *rm = new QPushButton(u"Delete this production"_s);
    rm->setToolTip(u"Written on Apply; a factory's building goes with it (and anything that unlocks that building unlocks "
                   u"nothing)"_s);
    connect(rm, &QPushButton::clicked, this, [this, key] { Q_EMIT removeRequested(key); });
    row = new QHBoxLayout();
    row->addWidget(rm);
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(raw);
    bl->addWidget(s);
}

// ------------------------------------------------------------------ efficiency goods: template or its own
// MEASURED in the game 2026-10-15: they are bought every day (amount × level × staff share × price); without them a
// factory makes 75 % of its output, with them no more than 100 %; a production type's own `efficiency` block REPLACES
// its template's list (an empty one: none).
static double monotonic()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void ProductionWorkspace::_armed(widgets::WrapButton *btn, const std::pair<QString, QString> &key, const QString &ask,
                                 const std::function<void()> &fn)
{
    // A button that asks once (no dialog): the first click says what it will do, the second within 6 s does it.
    connect(btn, &QPushButton::clicked, this, [this, btn, key, ask, fn] {
        if (_eff_armed && _eff_armed->first == key && monotonic() - _eff_armed->second < 6) {
            _eff_armed.reset();
            fn();
            return;
        }
        _eff_armed = std::pair{key, monotonic()};
        btn->setText(ask);
        btn->setStyleSheet(u"QPushButton { border: 2px solid %1; color: %1; }"_s.arg(WHATIF_COL));
    });
}

QString ProductionWorkspace::_eff_goods_list(const Goods &rows) const
{
    if (rows.empty())
        return u"none"_s;
    QStringList out;
    for (const auto &[g, a] : rows)
        out << gname(g) + u" "_s + gfmt(a);
    return out.join(u", "_s);
}

void ProductionWorkspace::_eff_scope(const QString &key, const production::Desc &d, const QHash<QString, qsizetype> &idx,
                                     const Emit &emit_)
{
    // The factory's efficiency goods: from its template (shown once, edited in the template) — or its own list, which
    // replaces the template's. Every change says what it changes.
    production::ProdEdits *ed = edits;
    const QString tpl = d.tmpl.value_or(QString());
    QStringList users, own_users;
    if (!tpl.isEmpty())
        std::tie(users, own_users) = ed->users_of(tpl);
    else
        users = {key};
    auto *s = new Section();
    s->lay->addWidget(label(u"Efficiency goods"_s));
    const double cost = ed->eff_cost(d, [this](const QString &g) { return price(g); });
    if (!d.has_own_eff) {
        QLabel *lb = label(u"<b>From the template "_s + (tpl.isEmpty() ? u"(none)"_s : tpl) + u"</b> — "_s +
                               _eff_goods_list(d.efficiency) + u" · ≈ "_s + mny(cost) +
                               u" a day per level at full staff · the template is used by "_s + QString::number(users.size()) +
                               u" production type"_s + (users.size() == 1 ? QString() : u"s"_s) +
                               (!own_users.isEmpty() ? u" ("_s + QString::number(own_users.size()) +
                                                           u" of them with their own list instead)"_s
                                                     : QString()) +
                               u"."_s,
                           u"sub"_s);
        lb->setTextFormat(Qt::RichText);
        s->lay->addWidget(lb);
        auto *row = new QVBoxLayout();               // (one button a row: the panel never gets wider than itself)
        if (!tpl.isEmpty() && ed->db.entries.contains(tpl)) {
            auto *bt = new widgets::WrapButton(u"Edit the template "_s + tpl + u" ▸"_s);
            bt->setToolTip(u"Its efficiency goods are the template's: a change there changes every production type that "
                           u"uses it"_s);
            long long followers = 0;
            for (const QString &u : users)
                followers += own_users.contains(u) ? 0 : 1;
            _armed(bt, {u"tpl"_s, key},
                   u"Click again: edit "_s + tpl + u" — it changes "_s + QString::number(followers) + u" production type"_s +
                       (followers == 1 ? QString() : u"s"_s),
                   [this, tpl] { _picked(u"tmpl"_s, tpl); });
            row->addWidget(bt);
        }
        auto *bo = new widgets::WrapButton(u"Give only this one its own list"_s);
        bo->setToolTip(u"An own `efficiency` block in this production type: the game then uses it INSTEAD of the template's "
                       u"list (measured in the game) — and this one no longer follows the template"_s);
        const Goods effl = d.efficiency;
        _armed(bo, {u"own"_s, key}, u"Click again: "_s + key + u" gets its own list (replaces the template's)"_s,
               [ed, key, effl, tpl, emit_] {
                   QStringList lines;
                   for (const auto &[g, a] : effl)
                       lines << g + u" = "_s + gfmt(a);
                   emit_(techdata::add_block(ed->text(key), {}, u"efficiency"_s, lines),
                         u"own efficiency list (a copy of "_s + tpl + u"'s)"_s);
               });
        row->addWidget(bo);
        s->lay->addLayout(row);
    } else {
        const production::Template *tt = !tpl.isEmpty() ? ed->db.templates.get(tpl) : nullptr;
        QLabel *lb = label(u"<b style='color:"_s + WHATIF_COL + u"'>Its OWN list</b> — the game uses it instead of the template "_s +
                               (tpl.isEmpty() ? u"—"_s : tpl) + u"'s ("_s + _eff_goods_list(tt ? tt->efficiency : Goods{}) +
                               u"). Changes here change only this production type · ≈ "_s + mny(cost) +
                               u" a day per level at full staff"_s +
                               (d.own_eff.empty() ? u" · an EMPTY list: it needs none"_s : QString()),
                           u"sub"_s);
        lb->setTextFormat(Qt::RichText);
        s->lay->addWidget(lb);
        _goods_rows(s, [ed, key] { return ed->text(key); }, u"efficiency"_s, d.own_eff, emit_, u"Add an efficiency good"_s);
        auto *row = new QVBoxLayout();
        auto *bb = new widgets::WrapButton(u"Back to the template's list"_s);
        bb->setToolTip(u"Takes this production type's own `efficiency` block out: it follows "_s + tpl + u" again"_s);
        _armed(bb, {u"back"_s, key}, u"Click again: drop its own list, follow "_s + tpl, [ed, key, idx, tpl, emit_] {
            if (idx.contains(u"efficiency"_s))
                emit_(techdata::remove_node(ed->text(key), {idx.value(u"efficiency"_s)}),
                      u"back to "_s + tpl + u"'s efficiency list"_s);
        });
        row->addWidget(bb);
        if (!tpl.isEmpty() && ed->db.entries.contains(tpl)) {
            auto *bt = new widgets::WrapButton(u"See the template "_s + tpl + u" ▸"_s);
            connect(bt, &QPushButton::clicked, this, [this, tpl] { _picked(u"tmpl"_s, tpl); });
            row->addWidget(bt);
        }
        s->lay->addLayout(row);
    }
    s->lay->addWidget(label(u"Measured in the game: bought every day like the inputs; without them the output is 75 %%, "
                            u"with them no more than 100 %%."_s,
                            u"faint"_s));
    bl->addWidget(s);
}

void ProductionWorkspace::_fill_tmpl(const QString &name)
{
    // A production template: its efficiency goods (edited for every production type that follows it) and who uses it.
    production::ProdEdits *ed = edits;
    const auto [users, own_users] = ed->users_of(name);
    QStringList followers;
    for (const QString &u : users)
        if (!own_users.contains(u))
            followers << u;
    title->setText(u"Template "_s + name);
    sub->setText(u"A production template · "_s +
                 (ed->want.contains(name) ? u"changed, written on Apply"_s : u"common/production_types.txt"_s));
    std::shared_ptr<const Node> keep;
    const Node *r = rootn(ed->text(name), keep);
    const Goods rows = production::goods_block(r ? r->kids : script::Nodes{}, u"efficiency"_s);
    auto *s = new Section();
    QLabel *hl = label(u"<b style='color:"_s + WHATIF_COL + u"'>Changes here change "_s + QString::number(followers.size()) +
                           u" production type"_s + (followers.size() == 1 ? QString() : u"s"_s) +
                           u"</b> — every one that uses "_s + name + u" and has no list of its own."_s,
                       u"sub"_s);
    hl->setTextFormat(Qt::RichText);
    s->lay->addWidget(hl);
    py::Sum tot;
    for (const auto &[g, a] : rows)
        tot.add(a * price(g));
    s->lay->addWidget(label(u"Efficiency goods · ≈ "_s + mny(tot.value()) + u" a day per level at full staff"_s));
    const long long nf = followers.size();
    const Emit emit_ = [this, ed, name, nf](const QString &nw, const QString &what) {
        if (nw != ed->text(name))
            Q_EMIT edit(u"template "_s + name + u": "_s + what + u" ("_s + QString::number(nf) + u" production types)"_s, name, nw);
    };
    _goods_rows(s, [ed, name] { return ed->text(name); }, u"efficiency"_s, rows, emit_, u"Add an efficiency good"_s);
    bl->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"Uses it ("_s + QString::number(followers.size()) + u")"_s));
    std::vector<Cid> ks;
    for (const QString &k : followers)
        if (_io.contains({u"prod"_s, k, 0}))
            ks.push_back({u"prod"_s, k, 0});
    _node_list(s, ks, 10);
    if (!own_users.isEmpty()) {
        QStringList names;
        for (const QString &k : own_users)
            names << _names.value(k, k);
        s->lay->addWidget(label(u"Use it, but with their OWN efficiency list (not changed from here): "_s + names.join(u", "_s),
                                u"sub"_s));
    }
    bl->addWidget(s);
}

void ProductionWorkspace::_view_buttons(Section *s, const Cid &n, bool park_)
{
    auto *row = new QHBoxLayout();                   // own rows: the side panel never gets wider than itself
    auto *ex = new QPushButton(u"Explore it"_s);
    ex->setToolTip(u"The Explorer: it in the middle — what it needs on the left, what uses it on the right"_s);
    const OptCid ce = _ex_centre();
    ex->setEnabled(!(view == u"explore" && ce && *ce == n));
    connect(ex, &QPushButton::clicked, this, [this, n] { explore(n); });
    row->addWidget(ex);
    row->addStretch(1);
    s->lay->addLayout(row);
    if (park_) {
        row = new QHBoxLayout();
        auto *pkb = new QPushButton(u"Park it (out of the view)"_s);
        pkb->setToolTip(u"Only the view: it waits in the Parked area above the chart until you click it there"_s);
        connect(pkb, &QPushButton::clicked, this, [this, n] { park(n); });
        row->addWidget(pkb);
        row->addStretch(1);
        s->lay->addLayout(row);
    }
}

void ProductionWorkspace::_fill_building(const QString &b)
{
    production::ProdEdits *ed = edits;
    const QString text = ed->bld_text(b);
    const production::BuildingDesc bi = ed->bld_info(b);
    title->setText(bname(b));
    OptStr runs;
    for (const auto &kv : _info)
        if (ed->building_of(kv.first) == OptStr(b)) {
            runs = kv.first;
            break;
        }
    sub->setText(production::BUILDING_KIND.value(bi.type, pretty(bi.type)) + u" · "_s + b +
                 (runs ? u" · runs "_s + _names.value(*runs, *runs) : QString()) +
                 (ed->bld_is_new(b) ? u"  · new, written on Apply"_s
                                    : (ed->bld_want.contains(b) ? u"  · changed, written on Apply"_s : QString())));
    const Emit emit_ = [this, ed, b](const QString &nw, const QString &what) {
        if (nw != ed->bld_text(b))
            Q_EMIT other(b + u": "_s + what, u"bld"_s, b, nw);
    };
    auto *s = new Section();
    s->lay->addWidget(label(u"Costs to build (goods_cost)"_s));
    _goods_rows(s, [ed, b] { return ed->bld_text(b); }, u"goods_cost"_s, bi.cost, emit_, u"Add a good it costs"_s);
    py::Sum tot;
    for (const auto &[g, a] : bi.cost)
        tot.add(a * ed->cost(g));
    s->lay->addWidget(label(u"≈ "_s + mny(tot.value()) + u" at the base prices"_s +
                                (bi.extra && !bi.extra->isEmpty() ? u" · + "_s + *bi.extra + u" extra (cost)"_s : QString()),
                            u"sub"_s));
    std::shared_ptr<const Node> keep;
    const Node *r = rootn(text, keep);
    QHash<QString, qsizetype> idx;
    if (r)
        for (size_t i = 0; i < r->kids.size(); ++i)
            if (r->kids[i].has_key && !r->kids[i].key.isEmpty())
                idx.insert(r->kids[i].key, static_cast<qsizetype>(i));
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"Build time (days)"_s, u"sub"_s, false));
    auto *tf = new ValueField(u"number"_s, std::nullopt, bi.time.value_or(QString()), u"365"_s);
    tf->setFixedWidth(90);
    connect(tf, &ValueField::committed, this, [ed, b, idx, emit_](const QString &v) {
        const QString t = ed->bld_text(b);
        emit_(idx.contains(u"time"_s) ? techdata::set_value(t, {idx.value(u"time"_s)}, v) : techdata::add_line(t, {}, u"time"_s, v),
              u"time "_s + v);
    });
    row->addWidget(tf);
    row->addStretch(1);
    s->lay->addLayout(row);
    bl->addWidget(s);
    if (runs) {
        s = new Section();
        auto *go = new QPushButton(u"Go to what it runs: "_s + _names.value(*runs, *runs));
        const QString rk = *runs;
        connect(go, &QPushButton::clicked, this, [this, rk] {
            if (view == u"explore") {
                explore({u"prod"_s, rk, 0});
            } else {
                _picked(u"prod"_s, rk);
                chart->center_on({u"prod"_s, rk, 0});
            }
        });
        s->lay->addWidget(go);
        bl->addWidget(s);
    }
    s = new Section();
    auto *fp = new techpanel::FilePreview();
    fp->set_text(text);
    s->lay->addWidget(fp);
    auto *raw = new techpanel::RawBox();
    raw->hide();
    connect(raw, &techpanel::RawBox::saved, this, [emit_](const QString &t) { emit_(t, u"edited as text"_s); });
    auto *rb = new QPushButton(u"Edit as text…"_s);
    connect(rb, &QPushButton::clicked, this, [raw, ed, b] { raw->open(b, ed->bld_text(b)); });
    row = new QHBoxLayout();
    row->addWidget(rb);
    row->addStretch(1);
    s->lay->addLayout(row);
    _view_buttons(s, {u"bld"_s, b, 0});
    s->lay->addWidget(raw);
    bl->addWidget(s);
}

void ProductionWorkspace::_fill_need(const QString &pt)
{
    production::ProdEdits *ed = edits;
    title->setText(nname({u"need"_s, pt, 0}));
    const production::PopFile *info = ed->db.pop_files.get(pt);
    sub->setText(u"poptypes/"_s + pt + u".txt"_s +
                 (!info || info->in_mod ? QString() : u" — from the base game: a change gives the mod its own copy"_s) +
                 (ed->need_want.contains(pt) ? u"  · changed, written on Apply"_s : QString()));
    const Emit emit_ = [this, ed, pt](const QString &nw, const QString &what) {
        if (nw != ed->pop_text(pt))
            Q_EMIT other(pt + u": "_s + what, u"need"_s, pt, nw);
    };
    const OrderedMap<QString, Goods> nd = ed->needs(pt);
    OrderedMap<QString, double> baskets;
    for (const auto &[kind, title_] : NEEDS) {
        const QString krgb = css_rgb(NEED_KIND_RGB.value(kind));
        auto *s = new Section();
        QLabel *hd = label(title_);
        hd->setStyleSheet(u"color: %1;"_s.arg(krgb));
        s->lay->addWidget(hd);
        const Goods rows = nd.value(kind);
        if (rows.empty())
            s->lay->addWidget(label(u"None."_s, u"faint"_s));
        if (!rows.empty()) {
            auto *hr = new QHBoxLayout();
            hr->addWidget(label(u"Good"_s, u"faint"_s, false), 2);
            for (const auto &[t_, w_] : {std::pair{u"Amount"_s, 70}, {u"Price"_s, 70}, {u"Worth"_s, 46}}) {
                QLabel *lb = label(t_, u"faint"_s, false);
                lb->setFixedWidth(w_);
                hr->addWidget(lb);
            }
            hr->addSpacing(34);
            s->lay->addLayout(hr);
        }
        const QString kk = kind;
        for (const auto &[g, a] : rows) {
            auto *r = new QHBoxLayout();
            r->addWidget(label(gname(g), u"sub"_s, false), 2);
            const QString gg = g;
            r->addWidget(_amount_field(a, [ed, pt, kk, gg, emit_](const QString &v) {
                emit_(production::need_set(ed->pop_text(pt), kk, gg, v), kk + u" "_s + gg + u" "_s + v);
            }));
            r->addWidget(_price_field(g, 70));
            QLabel *wl = label(mny(a * ed->cost(g)), u"sub"_s, false);
            wl->setFixedWidth(46);
            r->addWidget(wl);
            QPushButton *x = x_button();
            connect(x, &QPushButton::clicked, this, [ed, pt, kk, gg, emit_] {
                emit_(production::need_set(ed->pop_text(pt), kk, gg, std::nullopt), kk + u": no "_s + gg);
            });
            r->addWidget(x);
            s->lay->addLayout(r);
        }
        QSet<QString> have;
        for (const auto &[g, _a] : rows)
            have.insert(g);
        QComboBox *add = add_box(goods_items(), have, u"＋ Add a good — click for the list, or type"_s);
        py::Sum bs;
        for (const auto &[g, a] : rows)
            bs.add(a * ed->cost(g));
        const double basket = bs.value();
        baskets.insert(kind, basket);
        auto *tl = new QHBoxLayout();
        QLabel *t1 = label(title_ + u" together"_s, u"sub"_s, false);
        t1->setStyleSheet(u"color: %1; font-weight: 700;"_s.arg(krgb));
        tl->addWidget(t1, 1);
        QLabel *t2 = label(u"≈ "_s + mny(basket), u"sub"_s, false);
        t2->setStyleSheet(u"color: %1; font-weight: 700; font-size: 15px;"_s.arg(krgb));
        tl->addWidget(t2);
        tl->addSpacing(34);
        auto *line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(u"color: %1;"_s.arg(krgb));
        s->lay->addWidget(line);
        s->lay->addLayout(tl);
        connect(add, &QComboBox::activated, this, [ed, pt, kk, add, emit_](int) {
            const auto g = picked_good(add);
            if (g)
                emit_(production::need_set(ed->pop_text(pt), kk, *g, u"1"_s), kk + u" + "_s + *g);
        });
        s->lay->addWidget(add);
        bl->addWidget(s);
    }
    auto *s = new Section();
    py::Sum tot;
    for (const auto &kv : baskets)
        tot.add(kv.second);
    QLabel *hd = label(u"All needs together ≈ "_s + mny(tot.value()) + u" a day"_s, u"title"_s);
    hd->setStyleSheet(u"color: %1;"_s.arg(theme::GOLD));
    s->lay->addWidget(hd);
    QStringList parts;
    for (const auto &[k, t] : NEEDS)
        parts << py::split(t)[0] + u" "_s + mny(baskets.value(k, 0.0));
    s->lay->addWidget(label(parts.join(u" + "_s) + u" — at the base prices, per the game's pop-size unit."_s, u"faint"_s));
    bl->addWidget(s);
    s = new Section();
    _view_buttons(s, {u"need"_s, pt, 0});
    bl->addWidget(s);
}

void ProductionWorkspace::_goods_rows(Section *s, const std::function<QString()> &get_text, const QString &block,
                                      const Goods &rows, const Emit &emit_, const QString &add_text)
{
    for (const auto &[g, a] : rows) {
        auto *row = new QHBoxLayout();
        auto *gf = new ValueField(u"choice"_s, goods_items(), g);
        row->addWidget(gf, 2);
        auto *af = new ValueField(u"number"_s, std::nullopt, gfmt(a), u"1"_s);
        af->setFixedWidth(80);
        row->addWidget(af);
        QPushButton *x = x_button();
        row->addWidget(x);
        s->lay->addLayout(row);
        const QString gg = g;
        connect(gf, &ValueField::committed, this, [get_text, block, gg, emit_](const QString &v) {
            const QString t = get_text();
            const auto p = good_path(t, block, gg);
            if (p)
                emit_(techdata::replace_key(t, *p, v), gg + u" → "_s + v);
        });
        connect(af, &ValueField::committed, this, [get_text, block, gg, emit_](const QString &v) {
            const QString t = get_text();
            const auto p = good_path(t, block, gg);
            if (p)
                emit_(techdata::set_value(t, *p, v), gg + u" "_s + v);
        });
        connect(x, &QPushButton::clicked, this, [get_text, block, gg, emit_] {
            const QString t = get_text();
            const auto p = good_path(t, block, gg);
            if (p)
                emit_(techdata::remove_node(t, *p), u"no "_s + gg);
        });
    }
    auto *row = new QHBoxLayout();
    QSet<QString> have;
    for (const auto &[g, _a] : rows)
        have.insert(g);
    QComboBox *box = add_box(goods_items(), have, u"＋ "_s + add_text + u" — click for the list, or type"_s);
    connect(box, &QComboBox::activated, this, [get_text, block, box, emit_](int) {
        const auto g = picked_good(box);
        if (!g)
            return;
        const QString t = get_text();
        std::shared_ptr<const Node> keep;
        const auto bi = block_index(rootn(t, keep), block);
        if (!bi)
            emit_(techdata::add_block(t, {}, block, {*g + u" = 1"_s}), u"+ "_s + *g);
        else
            emit_(techdata::add_line(t, {*bi}, *g, u"1"_s), u"+ "_s + *g);
    });
    row->addWidget(box, 1);
    s->lay->addLayout(row);
    if (block == u"input_goods")
        s->lay->addWidget(label(u"A new input starts at 1 — set its amount; the chart ties it in at once."_s, u"faint"_s));
}

// ------------------------------------------------------------------ new
void ProductionWorkspace::_new_form()
{
    new_section->show();
    new_err->hide();
    _suggest_key();
}

QString ProductionWorkspace::_new_kind() const
{
    for (const auto &[k, b] : new_kind)
        if (b->isChecked())
            return k;
    return QString();
}

void ProductionWorkspace::_suggest_key()
{
    if (!edits)
        return;
    QString g = new_out->currentData().toString();
    if (g.isEmpty())
        g = u"goods"_s;
    const QString kind = _new_kind();
    const QString base_ = kind == u"factory" ? g + u"_factory"_s : kind == u"artisan" ? u"artisan_"_s + g : g + u"_rgo"_s;
    new_key->setText(edits->new_key(base_));
    new_building->setVisible(kind == u"factory");
}

void ProductionWorkspace::_create()
{
    production::ProdEdits *ed = edits;
    const QString g = new_out->currentData().toString();
    const QString key = py::strip(new_key->text());
    const QString kind = _new_kind();
    QString problem;
    if (g.isEmpty())
        problem = u"Pick the good it makes."_s;
    else if (key.isEmpty() || !py::isalnum(QString(key).remove(u'_')))
        problem = u"The name must be letters, digits and _ (e.g. glass_factory)."_s;
    else if (ed->exists(key) || ed->db.entries.contains(key) || ed->db.building_text.contains(key))
        problem = key + u" is already used."_s;
    if (!problem.isEmpty()) {
        new_err->setText(u"⚠ "_s + problem);
        new_err->show();
        return;
    }
    const QString text = ed->template_text(kind, key, g);
    std::optional<std::pair<QString, QString>> building;
    if (kind == u"factory" && new_building->isChecked()) {
        ed->added.insert(key, text);                 // so its template is known for the building copy
        building = ed->building_text(key);
        ed->added.remove(key);
    }
    new_section->hide();
    sel = Cid{u"prod"_s, key, 0};
    if (view == u"explore") {
        std::vector<Cid> rest;
        for (const Cid &x : ex_trail)
            if (x != ALL)
                rest.push_back(x);
        std::vector<Cid> t{ALL};
        const size_t from = rest.size() > 10 ? rest.size() - 10 : 0;
        t.insert(t.end(), rest.begin() + static_cast<std::ptrdiff_t>(from), rest.end());
        t.push_back(*sel);
        ex_trail = t;
    }
    Q_EMIT addRequested(key, text, building, building ? gname(g) + u" Factory"_s : QString());
    sel = Cid{u"prod"_s, key, 0};
    if (view != u"chart")
        _fit();
}

}  // namespace prodpanel
