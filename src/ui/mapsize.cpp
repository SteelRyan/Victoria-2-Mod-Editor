// mapsize.py, its page half — line by line.
#include "ui/mapsize.h"
#include "ui/drawtext.h"

#include "core/document.h"
#include "core/entryview_data.h"
#include "core/provrenumber.h"
#include "core/pynum.h"
#include "core/scriptstore.h"
#include "map/imgio.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>
#include <cmath>

using namespace Qt::StringLiterals;
using widgets::label;

namespace mapsize {

// ---------------------------------------------------------------------------------------------- the picture
CutPreview::CutPreview(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void CutPreview::set_image(const QImage &img_, long long map_w, long long map_h)
{
    img = img_;
    w = std::max(1LL, map_w);
    h = std::max(1LL, map_h);
    update();
}

void CutPreview::set_cut(long long left_, long long right_, std::optional<long long> top_,
                         std::optional<long long> bottom_)
{
    left = left_;
    right = right_;
    if (top_) {
        top = *top_;
        bottom = bottom_.value_or(0);
    }
    update();
}

// (x of the map's left edge, y of its top, its width, its height) in the widget — when growing, of the NEW map (the
// map as it is lies inside it, the added parts around it)
std::tuple<double, double, double, double> CutPreview::_place() const
{
    if (img.isNull())
        return {0.0, 0.0, 1.0, 1.0};
    const double iw = img.width() * uw() / double(w), ih = img.height() * uh() / double(h);
    const double s = std::min((width() - 24) / iw, (height() - 8) / ih);
    const double dw = iw * s, dh = ih * s;
    return {(width() - dw) / 2.0, (height() - dh) / 2.0, dw, dh};
}

double CutPreview::edge_x(const QString &side) const
{
    const auto [x0, y0, dw, dh] = _place();
    return side == u"left" ? x0 + dw * double(left) / uw() : x0 + dw - dw * double(right) / uw();
}

double CutPreview::edge_y(const QString &side) const
{
    const auto [x0, y0, dw, dh] = _place();
    return side == u"top" ? y0 + dh * double(top) / uh() : y0 + dh - dh * double(bottom) / uh();
}

// What of the picture is at a place: a border ("left" / "right", or "top" / "bottom" when the height is cut), the
// kept part ("move") or "" (None).
QString CutPreview::_side_at(const QPointF &pos) const
{
    const auto [x0, y0, dw, dh] = _place();
    if (vertical) {
        if (!(x0 - 6 <= pos.x() && pos.x() <= x0 + dw + 6))
            return {};
        const double dt = std::abs(pos.y() - edge_y(u"top"_s)), db = std::abs(pos.y() - edge_y(u"bottom"_s));
        if (std::min(dt, db) <= 10) {
            if (dt == db)
                return pos.y() < y0 + dh / 2.0 ? u"top"_s : u"bottom"_s;
            return dt < db ? u"top"_s : u"bottom"_s;
        }
        if ((top || bottom) && edge_y(u"top"_s) < pos.y() && pos.y() < edge_y(u"bottom"_s))
            return u"move"_s;
        return {};
    }
    if (!(y0 - 6 <= pos.y() && pos.y() <= y0 + dh + 6))
        return {};
    const double dl = std::abs(pos.x() - edge_x(u"left"_s)), dr = std::abs(pos.x() - edge_x(u"right"_s));
    if (std::min(dl, dr) <= 10) {
        if (dl == dr)                                // (both borders at one place: the side the mouse is on)
            return pos.x() < x0 + dw / 2.0 ? u"left"_s : u"right"_s;
        return dl < dr ? u"left"_s : u"right"_s;
    }
    if ((left || right) && edge_x(u"left"_s) < pos.x() && pos.x() < edge_x(u"right"_s))
        return u"move"_s;
    return {};
}

// Columns (rows) cut off an end when its border is at x (y for top / bottom).
long long CutPreview::_cut_at(const QString &side, double v) const
{
    const auto [x0, y0, dw, dh] = _place();
    if (side == u"top" || side == u"bottom") {
        const double d = side == u"top" ? (v - y0) : (y0 + dh - v);
        return py::round_int(std::max(d, 0.0) / dh * uh());
    }
    const double d = side == u"left" ? (v - x0) : (x0 + dw - v);
    return py::round_int(std::max(d, 0.0) / dw * uw());
}

double CutPreview::_coord(QMouseEvent *e) const { return vertical ? e->position().y() : e->position().x(); }

void CutPreview::mousePressEvent(QMouseEvent *e)
{
    const QString side = e->button() == Qt::LeftButton ? _side_at(e->position()) : QString();
    if (side == u"move") {                           // the kept part is taken: it slides, both borders with it
        _drag = std::make_tuple(u"move"_s, _coord(e), vertical ? top : left);
        setCursor(Qt::ClosedHandCursor);
    } else if (!side.isEmpty()) {
        _drag = std::make_tuple(side, 0.0, 0LL);
        Q_EMIT dragged(side, int(_cut_at(side, _coord(e))), false);
    }
}

void CutPreview::_emit(double v, bool done)
{
    const auto [side, v_start, first_start] = *_drag;
    if (side == u"move") {
        const auto [x0, y0, dw, dh] = _place();
        if (vertical)
            Q_EMIT dragged(u"top"_s, int(py::round_int(double(first_start) + (v - v_start) / dh * uh())), done);
        else
            Q_EMIT dragged(u"left"_s, int(py::round_int(double(first_start) + (v - v_start) / dw * uw())), done);
    } else {
        Q_EMIT dragged(side, int(_cut_at(side, v)), done);
    }
}

void CutPreview::mouseMoveEvent(QMouseEvent *e)
{
    if (_drag) {
        _emit(_coord(e), false);
        return;
    }
    const QString at = _side_at(e->position());
    setCursor(at == u"left" || at == u"right"   ? Qt::SizeHorCursor
              : at == u"top" || at == u"bottom" ? Qt::SizeVerCursor
              : at == u"move"                   ? Qt::OpenHandCursor
                                                : Qt::ArrowCursor);
}

void CutPreview::mouseReleaseEvent(QMouseEvent *e)
{
    if (_drag) {
        _emit(_coord(e), true);
        _drag.reset();
        unsetCursor();
    }
}

void CutPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(theme::BG));
    if (img.isNull())
        return;
    const auto [x0, y0, dw, dh] = _place();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (grow) {
        _paint_grow(p, x0, y0, dw, dh);
        return;
    }
    p.drawImage(QRectF(x0, y0, dw, dh), img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(font());
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QPen(QColor(theme::LINE), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(x0, y0, dw, dh));
    for (const auto &[px, at_top] : {std::pair{top, true}, std::pair{bottom, false}}) {   // rows off the north / south edge
        if (!px && !vertical)
            continue;
        const double bh = dh * double(px) / double(std::max(1LL, h));
        const QRectF r = at_top ? QRectF(x0, y0, dw, bh) : QRectF(x0, y0 + dh - bh, dw, bh);
        if (px) {
            p.fillRect(r, QColor(200, 30, 30, 120));
            p.fillRect(r, QBrush(QColor(255, 90, 80, 210), Qt::BDiagPattern));
        }
        const double edge = at_top ? r.bottom() : r.top();
        p.setPen(QPen(QColor(theme::ERROR_), 2));    // the border of the cut: dragged up / down
        p.drawLine(int(x0), int(edge), int(x0 + dw), int(edge));
        if (!px)
            continue;
        const QString t = u"✂ %1 px"_s.arg(px);
        const int tw = p.fontMetrics().horizontalAdvance(t) + 16;
        const QRectF tr(x0 + dw / 2.0 - tw / 2.0, at_top ? edge + 6 : edge - 32, tw, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(u"#14100a"_s));
        p.drawRoundedRect(tr, 5, 5);
        p.setPen(QColor(u"#ffffff"_s));
        drawtext::draw(p, tr, Qt::AlignCenter, t);
    }
    for (const auto &[px, at_left] : {std::pair{left, true}, std::pair{right, false}}) {
        if (vertical && !px)
            continue;
        const double bw = dw * double(px) / double(w);
        const QRectF r = at_left ? QRectF(x0, y0, bw, dh) : QRectF(x0 + dw - bw, y0, bw, dh);
        if (px) {
            p.fillRect(r, QColor(200, 30, 30, 120));
            p.fillRect(r, QBrush(QColor(255, 90, 80, 210), Qt::BDiagPattern));
        }
        const double edge = at_left ? r.right() : r.left();
        p.setPen(QPen(QColor(theme::ERROR_), 2));    // the border of the cut: one red line, nothing on it
        p.drawLine(int(edge), int(y0), int(edge), int(y0 + dh));
        if (px) {
            const QString t = u"✂ %1 px"_s.arg(px);
            const int tw = p.fontMetrics().horizontalAdvance(t) + 16;
            const QRectF tr(at_left ? edge + 8 : edge - 8 - tw, y0 >= 32 ? y0 - 30 : y0 + 6, tw, 26);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(u"#14100a"_s));
            p.drawRoundedRect(tr, 5, 5);
            p.setPen(QColor(u"#ffffff"_s));
            drawtext::draw(p, tr, Qt::AlignCenter, t);
        }
    }
}

void CutPreview::_paint_grow(QPainter &p, double x0, double y0, double dw, double dh)
{
    // (C++ only, 2026-11-14) growing: the new map's frame; the map as it is inside it; the added parts hatched — they
    // continue the edge (the sea tiles at the ends grow into them)
    const double mx = x0 + dw * double(left) / uw(), my = y0 + dh * double(top) / uh();
    const double mw = dw * double(w) / uw(), mh = dh * double(h) / uh();
    p.fillRect(QRectF(x0, y0, dw, dh), QColor(40, 90, 150, 150));
    p.fillRect(QRectF(x0, y0, dw, dh), QBrush(QColor(120, 190, 255, 170), Qt::BDiagPattern));
    p.drawImage(QRectF(mx, my, mw, mh), img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(theme::LINE), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(x0, y0, dw, dh));
    QFont f(font());
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    auto tag = [&](const QRectF &tr, const QString &t) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(u"#14100a"_s));
        p.drawRoundedRect(tr, 5, 5);
        p.setPen(QColor(u"#ffffff"_s));
        drawtext::draw(p, tr, Qt::AlignCenter, t);
    };
    p.setPen(QPen(QColor(theme::GOLD), 3));           // the borders of the map as it is: dragged
    if (vertical) {
        for (const auto &[px, at_top] : {std::pair{top, true}, std::pair{bottom, false}}) {
            const double edge = at_top ? my : my + mh;
            p.setPen(QPen(QColor(theme::GOLD), 3));
            p.drawLine(QPointF(x0, edge), QPointF(x0 + dw, edge));
            if (!px)
                continue;
            const QString t = u"＋ %1 px"_s.arg(px);
            const int tw = p.fontMetrics().horizontalAdvance(t) + 16;
            tag(QRectF(x0 + dw / 2.0 - tw / 2.0, at_top ? edge - 32 : edge + 6, tw, 26), t);
        }
        return;
    }
    for (const auto &[px, at_left] : {std::pair{left, true}, std::pair{right, false}}) {
        const double edge = at_left ? mx : mx + mw;
        p.setPen(QPen(QColor(theme::GOLD), 3));
        p.drawLine(QPointF(edge, y0), QPointF(edge, y0 + dh));
        if (!px)
            continue;
        const QString t = u"＋ %1 px"_s.arg(px);
        const int tw = p.fontMetrics().horizontalAdvance(t) + 16;
        tag(QRectF(at_left ? edge - 8 - tw : edge + 8, y0 >= 32 ? y0 - 30 : y0 + 6, tw, 26), t);
    }
}

// ---------------------------------------------------------------------------------------------- the page
MapSizeWorkspace::MapSizeWorkspace(moddata::Mod &mod_, mapview::MapData &md_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *leftw = new QWidget();
    auto *ll = new QVBoxLayout(leftw);
    ll->setContentsMargins(16, 12, 8, 12);
    ll->addWidget(label(u"Map size"_s, u"title"_s));
    size_lbl = label(QString(), u"sub"_s);
    ll->addWidget(size_lbl);
    preview = new CutPreview();
    connect(preview, &CutPreview::dragged, this, &MapSizeWorkspace::_dragged);
    ll->addWidget(preview, 1);
    hint = label(QString(), u"faint"_s);
    ll->addWidget(hint);
    _hint();
    split->addWidget(leftw);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *rightw = new QWidget();
    rightw->setObjectName(u"sideInner"_s);
    auto *rl = new QVBoxLayout(rightw);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(rightw);
    split->addWidget(sc);
    split->setStretchFactor(0, 7);
    split->setStretchFactor(1, 4);
    split->setSizes({1100, 620});
    auto *s = new widgets::Section();
    s->lay->addWidget(_bold(u"The rule"_s));
    rule = label(QString(), u"sub"_s);
    s->lay->addWidget(rule);
    rl->addWidget(s);
    s = new widgets::Section();
    // one cut at a time — the width OR the height (the user, 2026-10-16: both at once crashed; "merge the UI … a
    // setting, vertical cut or horizontal cut"); the height has every tool the width has
    // (C++ only, 2026-11-14, the user: "expand the map in map size past its current size") — growing, the same way
    auto *grp = new QButtonGroup(this);
    for (const auto &pair_ : std::vector<std::vector<std::pair<QString, QString>>>{
             {{u"w"_s, u"↔  Cut the width"_s}, {u"h"_s, u"↕  Cut the height"_s}},
             {{u"gw"_s, u"⇔  Grow the width"_s}, {u"gh"_s, u"⇕  Grow the height"_s}}}) {
        auto *row = new QHBoxLayout();
        row->setSpacing(0);
        for (const auto &[key, text] : pair_) {
            auto *b = new QPushButton(text);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            b->setChecked(key == u"w");
            connect(b, &QPushButton::clicked, this, [this, k = key] { set_dir(k); });
            grp->addButton(b);
            dir_btns[key] = b;
            row->addWidget(b, 1);
        }
        s->lay->addLayout(row);
    }
    QHBoxLayout *row = nullptr;
    dir = u"w"_s;
    w_box = new QWidget();
    h_box = new QWidget();
    auto *wl = new QVBoxLayout(w_box), *hl = new QVBoxLayout(h_box);
    for (QVBoxLayout *l_ : {wl, hl})
        l_->setContentsMargins(0, 6, 0, 0);
    s->lay->addWidget(w_box);
    s->lay->addWidget(h_box);
    h_box->hide();
    widgets::Section *outer_s = s;
    QVBoxLayout *lay = wl;                           // (the width's rows go into its own box)
    w_how = _bold(u"How much to cut off"_s);
    lay->addWidget(w_how);
    row = new QHBoxLayout();
    w_all = label(u"Cut off in all (px)"_s, u"sub"_s, false);
    row->addWidget(w_all);
    steps = new widgets::Stepper(0, 9999, 0);
    steps->setToolTip(u"− / ＋ go to the next width the game takes; a typed number becomes the nearest one"_s);
    connect(steps, &widgets::Stepper::valueChanged, this, [this] { _steps_changed(); });
    row->addWidget(steps);
    total_lbl = label(QString(), u"sub"_s, false);
    row->addWidget(total_lbl, 1);
    lay->addLayout(row);
    slider = new QSlider(Qt::Horizontal);            // the same amount by sliding: a stop at every width the game loads
    slider->setRange(0, 0);
    slider->setPageStep(8);
    slider->setMinimumHeight(26);
    slider->setToolTip(u"Slide to set how much is cut in all — it stops only at widths the game loads "
                       u"(the same number as in the field above)"_s);
    connect(slider, &QSlider::valueChanged, this, &MapSizeWorkspace::_slid);
    lay->addWidget(slider);
    lay->addWidget(_bold(u"Which end"_s));
    row = new QHBoxLayout();
    w_end = label(u"From the left end (px)"_s, u"sub"_s, false);
    row->addWidget(w_end);
    left = new widgets::Stepper(0, 0, 0);
    connect(left, &widgets::Stepper::valueChanged, this, [this] { _changed(); });
    row->addWidget(left);
    right_lbl = label(QString(), u"sub"_s, false);
    row->addWidget(right_lbl, 1);
    lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[text, share] : std::vector<std::pair<QString, double>>{
             {u"All from the left"_s, 1.0}, {u"Half and half"_s, 0.5}, {u"All from the right"_s, 0.0}}) {
        auto *b = new QPushButton(text);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this,
                [this, f = share] { left->setValue(py::round_int(double(total()) * f)); });
        row->addWidget(b, 1);
    }
    lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[text, d] : std::vector<std::pair<QString, int>>{{u"◀ 8 px more from the left"_s, 8},
                                                                      {u"8 px more from the right ▶"_s, -8}}) {
        auto *b = new QPushButton(text);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, dd = d] { left->setValue(left->value() + dd); });
        row->addWidget(b, 1);
    }
    lay->addLayout(row);
    lay = hl;                                        // the height: the same tools
    h_how = _bold(u"How much to cut off"_s);
    lay->addWidget(h_how);
    row = new QHBoxLayout();
    h_all = label(u"Cut off the height in all (px)"_s, u"sub"_s, false);
    row->addWidget(h_all);
    h_steps = new widgets::Stepper(0, 9999, 0);
    h_steps->setToolTip(u"Rows off the north and / or the south edge of the map — − / ＋ go to the next "
                        u"height offered"_s);
    connect(h_steps, &widgets::Stepper::valueChanged, this, [this] { _h_changed(); });
    row->addWidget(h_steps);
    h_lbl = label(QString(), u"sub"_s, false);
    row->addWidget(h_lbl, 1);
    lay->addLayout(row);
    h_slider = new QSlider(Qt::Horizontal);
    h_slider->setRange(0, 0);
    h_slider->setPageStep(8);
    h_slider->setMinimumHeight(26);
    h_slider->setToolTip(u"Slide to set how much of the height is cut in all — a stop at every height offered "
                         u"(the same number as in the field above)"_s);
    connect(h_slider, &QSlider::valueChanged, this, &MapSizeWorkspace::_h_slid);
    lay->addWidget(h_slider);
    lay->addWidget(_bold(u"Which end"_s));
    row = new QHBoxLayout();
    h_end = label(u"From the top (north) (px)"_s, u"sub"_s, false);
    row->addWidget(h_end);
    top = new widgets::Stepper(0, 0, 0);
    connect(top, &widgets::Stepper::valueChanged, this, [this] { _changed(); });
    row->addWidget(top);
    bottom_lbl = label(QString(), u"sub"_s, false);
    row->addWidget(bottom_lbl, 1);
    lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[text, share] : std::vector<std::pair<QString, double>>{
             {u"All from the top"_s, 1.0}, {u"Half and half"_s, 0.5}, {u"All from the bottom"_s, 0.0}}) {
        auto *b = new QPushButton(text);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this,
                [this, f = share] { top->setValue(py::round_int(double(h_steps->value()) * f)); });
        row->addWidget(b, 1);
    }
    lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[text, d] : std::vector<std::pair<QString, int>>{{u"▲ 8 px more from the top"_s, 8},
                                                                      {u"8 px more from the bottom ▼"_s, -8}}) {
        auto *b = new QPushButton(text);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, dd = d] { top->setValue(top->value() + dd); });
        row->addWidget(b, 1);
    }
    lay->addLayout(row);
    rl->addWidget(outer_s);
    s = new widgets::Section();
    what_lbl = _bold(u"What the cut does"_s);
    s->lay->addWidget(what_lbl);
    result = new QLabel();
    result->setObjectName(u"sub"_s);
    result->setWordWrap(true);
    result->setTextFormat(Qt::RichText);
    s->lay->addWidget(result);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"QLabel { color: %1; border: 2px solid %2; border-radius: 4px; padding: 6px 8px; "
                        u"font-weight: 600; }"_s.arg(theme::ERROR_, theme::ERROR_));
    warn->hide();
    s->lay->addWidget(warn);
    del_box = new QCheckBox(u"Delete the provinces that disappear completely"_s);
    del_box->setToolTip(u"On (recommended): they are deleted first, the same way as with Drawing → Delete "
                        u"province, so nothing stays behind that the game could trip over"_s);
    del_box->setChecked(true);
    connect(del_box, &QCheckBox::toggled, this, [this] { _changed(); });
    s->lay->addWidget(del_box);
    mm_box = new QCheckBox(u"Cut the minimap picture the same way"_s);
    mm_box->setToolTip(u"On (recommended): gfx/interface/minimap.dds loses the same share at each end and is "
                       u"stretched back to its size, so the minimap in the game marks the right place again"_s);
    mm_box->setChecked(true);
    connect(mm_box, &QCheckBox::toggled, this, [this] { _changed(); });
    s->lay->addWidget(mm_box);
    cut_btn = new QPushButton(u"✂ Cut the map"_s);
    cut_btn->setObjectName(u"primary"_s);
    connect(cut_btn, &QPushButton::clicked, this, &MapSizeWorkspace::_cut);
    s->lay->addWidget(cut_btn);
    rl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"The minimap in the game"_s));
    mm_note = new QLabel(QString());
    mm_note->setObjectName(u"sub"_s);
    mm_note->setWordWrap(true);
    mm_note->setTextFormat(Qt::RichText);
    s->lay->addWidget(mm_note);
    mm_fit_btn = new QPushButton(u"Fit the minimap picture to the map"_s);
    mm_fit_btn->setToolTip(u"Redraws gfx/interface/minimap.dds so that it shows the map from its first to its "
                           u"last column — a pending edit, written on Apply (Ctrl+Z undoes it)"_s);
    connect(mm_fit_btn, &QPushButton::clicked, this, &MapSizeWorkspace::_fit_minimap);
    mm_fit_btn->hide();
    s->lay->addWidget(mm_fit_btn, 0, Qt::AlignLeft);
    rl->addWidget(s);
    rl->addStretch(1);
}

// ------------------------------------------------------------------ the minimap against the map
// (file below the mod, the picture as it is now — a pending one counts) or ("", None).
std::pair<QString, std::optional<QImage>> MapSizeWorkspace::_minimap_image() const
{
    const auto mm = minimap_of(mod);
    if (!mm || !mm->name.endsWith(u".dds"_s))
        return {QString(), std::nullopt};
    const QString rel = u"gfx/interface/"_s + mm->name;
    if (doc && doc->maps.contains(u"assets"_s))
        for (const auto &[r, a] : doc->map<moreviews::AssetEdits>(u"assets"_s).fresh)
            if (r.toLower() == rel && a.how == u"strip")
                return {rel, a.img};
    const QFileInfo fi(mm->path);
    const qint64 stamp = fi.lastModified().toMSecsSinceEpoch(), size = fi.size();
    if (!_mm_disk || std::get<0>(*_mm_disk) != mm->path || std::get<1>(*_mm_disk) != stamp ||
        std::get<2>(*_mm_disk) != size) {
        const std::optional<QImage> img = imgio::read_image(mm->path);
        if (!img)
            return {rel, std::nullopt};
        _mm_disk = std::make_tuple(mm->path, stamp, size, *img);
    }
    return {rel, std::get<3>(*_mm_disk)};
}

void MapSizeWorkspace::_check_minimap()
{
    const auto [rel, img] = _minimap_image();
    if (!img || img->isNull()) {
        mm_note->setText(u"The mod and the game have no gfx/interface/minimap.dds to check."_s);
        mm_fit_btn->hide();
        return;
    }
    const MmKey key{rel, img->cacheKey(), md.w, md.version};
    if (!_mm_key || !(key == *_mm_key)) {
        std::vector<std::uint8_t> sea(static_cast<size_t>(md.n), 0);
        for (const auto &[pid, pr] : mod.provinces)
            if (pid < md.n && pr.sea)
                sea[size_t(pid)] = 1;
        _mm_key = key;
        _mm = minimap_fit(md, sea, *img);
    }
    const auto &fit = _mm;
    const QString rule_ = u"In the game the minimap's frame stands at the camera's share of the map's width (seen in "
                          u"the game), so the picture has to show the map from its first to its last column."_s;
    if (!fit) {
        mm_note->setText(rule_ + u" This picture can't be compared with the map (nothing is drawn on it that "
                                 u"looks like land)."_s);
        mm_fit_btn->hide();
        return;
    }
    const long long W = md.w;
    const bool off = fit->best - fit->now > 0.03 &&
                     (double(std::llabs(fit->left)) > 0.004 * double(W) ||
                      double(std::llabs(fit->width - W)) > 0.006 * double(W));
    if (!off) {
        mm_note->setText(rule_ + u" <b>%1 fits the map</b> (the land on it lies on the map's land)."_s.arg(rel));
        mm_fit_btn->hide();
        return;
    }
    const long long l = fit->left, r = fit->left + fit->width;
    mm_note->setText(
        rule_ + u" <span style='color:%1'><b>%2 does not fit this map:</b> it shows a map of about %3 columns — "
                u"%4 at the left and %5 at the right than the map has (%6). So in the game the frame is off towards "
                u"the ends, by up to about %7 px on the minimap.</span>"_s
                    .arg(theme::WARN, rel)
                    .arg(fit->width)
                    .arg(l < 0 ? u"%1 columns more"_s.arg(-l) : u"%1 columns less"_s.arg(l),
                         r > W ? u"%1 columns more"_s.arg(r - W) : u"%1 columns less"_s.arg(W - r))
                    .arg(W)
                    .arg(py::round_int(double(std::max(std::llabs(l), std::llabs(r - W))) / double(fit->width) * 267)));
    mm_fit_btn->setVisible(bool(asset_edit));
}

void MapSizeWorkspace::_fit_minimap()
{
    const auto [rel, img] = _minimap_image();
    if (!img || !_mm || !asset_edit)
        return;
    const QImage fresh = refit_minimap(*img, md.w, _mm->left, _mm->width);
    OrderedMap<QString, moreviews::Asset> files;
    files.insert(rel, moreviews::Asset{u"strip"_s, fresh, QString()});
    asset_edit(u"Minimap fitted to the map (%1 px wide)"_s.arg(md.w), files);
    _check_minimap();
}

QLabel *MapSizeWorkspace::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ data
long long MapSizeWorkspace::total() const { return steps->value(); }

void MapSizeWorkspace::refresh(document::Document *doc_)
{
    doc = doc_;
    const QString path = mod.p({u"map"_s, u"provinces.bmp"_s});
    const QFileInfo fi(path);
    const std::pair<QString, qint64> key{path, fi.isFile() ? fi.lastModified().toMSecsSinceEpoch() : 0};
    if (!_img_key || key != *_img_key) {
        _img_key = key;
        QImage img(path);
        if (!img.isNull() && img.width() > 1800)
            img = img.scaledToWidth(1800, Qt::SmoothTransformation);
        if (!img.isNull())
            img = img.flipped(Qt::Vertical);         // the same way up as the map on every other page
        preview->set_image(img, md.w, md.h);
    }
    size_lbl->setText(
        u"The map is %1 × %2 px (map/provinces.bmp; terrain.bmp and rivers.bmp must be the same)."_s.arg(md.w).arg(md.h));
    const std::vector<long long> cuts = allowed_cuts(md.w);
    const auto side = tile_side(md.w);
    rule->setText(
        u"Not every width works. Tried in the game: it lays square tiles over the map whose side has to divide "
        u"the width exactly. With a side of %1 to %2 px the map loaded (widths 5616, 5040, 5032, 5016, 5000, "
        u"4992, 4984, 4968, 4896); where only a larger side fits it crashed (5024, 4976, 5028), and so did odd "
        u"widths. So only widths that are a multiple of %3 and have such a side are offered — %4 of them for "
        u"this map; − / ＋ go from one to the next. Your map: %5 px wide, tiles of %6 px%7. The height (%8 px) can be "
        u"cut too, rows off the north and / or south edge, in 8-px steps (heights 2096, 2104, 2128 and 2152 loaded "
        u"in the game, a campaign ran). When the new height is no whole number of tiles, land or a river in the "
        u"map's two southernmost rows crashed the game while it loaded, so those two rows are then made sea. Growing "
        u"the map (up to twice its size) follows the same rules; a map grown wider and taller loaded in the game "
        u"too."_s
            .arg(TILE_MIN)
            .arg(TILE_MAX)
            .arg(WIDTH_UNIT)
            .arg(cuts.size())
            .arg(md.w)
            .arg(side ? QString::number(*side) : u"no"_s,
                 width_ok(md.w) ? QString() : u" — not a width seen to load"_s)
            .arg(md.h));
    QTimer::singleShot(60, this, [this] { _check_minimap(); });   // (a second of work the first time: after the page shows)
    _fill_choices();
    _steps_changed();
}

void MapSizeWorkspace::_fill_choices()
{
    // the amounts offered: what may be cut off, or (C++ only, growing) what may be added — and the captions to match
    const bool g = growing();
    const std::vector<long long> cuts = g ? allowed_grows(md.w) : allowed_cuts(md.w);
    std::vector<long long> c{0};
    c.insert(c.end(), cuts.begin(), cuts.end());
    steps->blockSignals(true);
    steps->setChoices(c);
    steps->blockSignals(false);
    _cuts = c;
    slider->blockSignals(true);
    slider->setRange(0, int(_cuts.size()) - 1);
    slider->blockSignals(false);
    preview->max_cut = cuts.empty() ? 0 : cuts.back();
    const std::vector<long long> hcuts = g ? allowed_height_grows(md.h, md.w) : allowed_height_cuts(md.h, md.w);
    std::vector<long long> hc{0};
    hc.insert(hc.end(), hcuts.begin(), hcuts.end());
    h_steps->blockSignals(true);
    h_steps->setChoices(hc);
    h_steps->blockSignals(false);
    _hcuts = hc;
    h_slider->blockSignals(true);
    h_slider->setRange(0, int(_hcuts.size()) - 1);
    h_slider->blockSignals(false);
    top->blockSignals(true);
    top->setRange(_top_min(), h_steps->value());
    top->blockSignals(false);
    w_how->setText(g ? u"How much to add"_s : u"How much to cut off"_s);
    w_all->setText(g ? u"Add in all (px)"_s : u"Cut off in all (px)"_s);
    w_end->setText(g ? u"At the left end (px)"_s : u"From the left end (px)"_s);
    h_how->setText(g ? u"How much to add"_s : u"How much to cut off"_s);
    h_all->setText(g ? u"Add to the height in all (px)"_s : u"Cut off the height in all (px)"_s);
    h_end->setText(g ? u"At the top (north) (px)"_s : u"From the top (north) (px)"_s);
    what_lbl->setText(g ? u"What growing does"_s : u"What the cut does"_s);
    del_box->setVisible(!g);
    mm_box->setText(g ? u"Grow the minimap picture the same way"_s : u"Cut the minimap picture the same way"_s);
    steps->setToolTip(g ? u"− / ＋ go to the next width the game should take (the cut's rule, up to twice the width)"_s
                        : u"− / ＋ go to the next width the game takes; a typed number becomes the nearest one"_s);
}

// A border (or the kept part) dragged in the picture. The number "Cut off in all" leads: dragging never changes how
// much is cut, only how it is shared between the two ends — the border taken goes where it is put (as far as the
// amount reaches) and the other end takes the rest.
void MapSizeWorkspace::_dragged(const QString &side, int px_, bool done)
{
    if (side == u"top" || side == u"bottom") {       // the height: the same, up and down
        const long long htot = h_steps->value();
        const long long px = std::max(0LL, std::min(static_cast<long long>(px_), htot));
        const long long new_top = side == u"top" ? px : htot - px;
        top->blockSignals(true);
        top->setRange(_top_min(), htot);
        top->setValue(new_top);
        top->blockSignals(false);
        if (done) {
            _changed();
        } else {
            const long long t2 = std::min(top->value(), htot);
            preview->set_cut(0, 0, t2, htot - t2);
            bottom_lbl->setText(u"→ %1 px from the bottom (south)"_s.arg(htot - t2));
        }
        return;
    }
    const long long tot = total();
    const long long px = std::max(0LL, std::min(static_cast<long long>(px_), tot));
    const long long new_left = side == u"left" ? px : tot - px;
    left->blockSignals(true);
    left->setRange(0, tot);
    left->setValue(new_left);
    left->blockSignals(false);
    if (done) {
        _changed();
    } else {                                         // while dragging: only the picture and the numbers (quick)
        const long long l2 = std::min(left->value(), tot);
        preview->set_cut(l2, tot - l2);
        right_lbl->setText(u"→ %1 px from the right end"_s.arg(tot - l2));
    }
}

void MapSizeWorkspace::_slid(int i)
{
    if (0 <= i && size_t(i) < _cuts.size() && _cuts[size_t(i)] != total())
        steps->setValue(_cuts[size_t(i)]);
}

// Cut the width ("w") or the height ("h") — one at a time: the other's amount goes back to 0.
void MapSizeWorkspace::set_dir(const QString &d)
{
    const bool was_grow = growing();
    dir = d;
    const bool vert = d.endsWith(u'h');
    for (const auto &[k, b] : dir_btns)
        b->setChecked(k == d);
    w_box->setVisible(!vert);
    h_box->setVisible(vert);
    preview->vertical = vert;
    preview->grow = growing();
    _hint();
    if (was_grow != growing()) {                     // (C++ only) cut <-> grow: every amount back to 0, the other list
        for (widgets::Stepper *st : {steps, h_steps, left, top}) {
            st->blockSignals(true);
            st->setValue(0);
            st->blockSignals(false);
        }
        for (QSlider *sl : {slider, h_slider}) {
            sl->blockSignals(true);
            sl->setValue(0);
            sl->blockSignals(false);
        }
        _fill_choices();
        _changed();
        return;
    }
    widgets::Stepper *other = vert ? steps : h_steps;
    if (other->value()) {
        other->blockSignals(true);
        other->setValue(0);
        other->blockSignals(false);
        widgets::Stepper *first = vert ? left : top;
        first->blockSignals(true);
        first->setValue(0);
        first->blockSignals(false);
        for (QSlider *sl : {slider, h_slider})
            sl->blockSignals(true);
        slider->setValue(vert ? 0 : slider->value());
        h_slider->setValue(!vert ? 0 : h_slider->value());
        for (QSlider *sl : {slider, h_slider})
            sl->blockSignals(false);
    }
    _changed();
}

void MapSizeWorkspace::_hint()
{
    if (growing()) {
        hint->setText(u"First set how much is added in all (the number on the right). Then drag a gold line — or the "
                      u"map — %1: that only shares the same amount between the two ends; the amount itself never "
                      u"changes by dragging."_s.arg(dir == u"gh" ? u"up or down"_s : u"left or right"_s));
        return;
    }
    hint->setText(u"First set how much is cut in all (the number on the right). Then drag a red line — or the part of "
                  u"the map that stays — %1: that only shares the same amount between the two ends; the amount itself "
                  u"never changes by dragging."_s.arg(dir == u"h" ? u"up or down"_s : u"left or right"_s));
}

void MapSizeWorkspace::_h_slid(int i)
{
    if (0 <= i && size_t(i) < _hcuts.size() && _hcuts[size_t(i)] != h_steps->value())
        h_steps->setValue(_hcuts[size_t(i)]);
}

void MapSizeWorkspace::_steps_changed()
{
    if (total() && dir.endsWith(u'h'))              // (a width amount set from elsewhere: the width is cut, not both)
        set_dir(growing() ? u"gw"_s : u"w"_s);
    const auto it = std::find(_cuts.begin(), _cuts.end(), total());
    if (it != _cuts.end()) {                         // the slider stands where the number is
        slider->blockSignals(true);
        slider->setValue(int(it - _cuts.begin()));
        slider->blockSignals(false);
    }
    left->blockSignals(true);
    left->setRange(0, total());
    left->blockSignals(false);
    _changed();
}

void MapSizeWorkspace::_h_changed()
{
    if (h_steps->value() && dir.endsWith(u'w'))
        set_dir(growing() ? u"gh"_s : u"h"_s);
    const auto it = std::find(_hcuts.begin(), _hcuts.end(), h_steps->value());
    if (it != _hcuts.end()) {                        // the slider stands where the number is
        h_slider->blockSignals(true);
        h_slider->setValue(int(it - _hcuts.begin()));
        h_slider->blockSignals(false);
    }
    top->blockSignals(true);
    top->setRange(_top_min(), h_steps->value());
    top->blockSignals(false);
    _changed();
}

// (C++ only, tried in the game 2026-11-18) the fewest rows that may come off the top: a cut that leaves a whole number
// of the map's tiles crashed the game when rows came off the bottom (1440 and 1728 px from the base game, even with the
// edge made sea) and loaded when they all came off the top — such a height is cut from the top only
long long MapSizeWorkspace::_top_min() const
{
    const long long htot = h_steps->value();
    if (growing() || htot <= 0)
        return 0;
    const auto side = tile_side(md.w - total());
    return side && (md.h - htot) % *side == 0 ? htot : 0;
}

// (rows off the north edge, rows off the south edge)
std::pair<long long, long long> MapSizeWorkspace::cut_rows() const
{
    const long long hv = h_steps->value();
    const long long north = std::min(top->value(), hv);
    return {north, hv - north};
}

// (C++ only, 2026-11-18) the land provinces a cut would leave ONLY on the edge it makes sea (the two southernmost rows
// when the height changes, the two westernmost columns after a cut from the west) and not wholly cut off: they would
// end up without a pixel, which crashes the game — the page deletes them with the ones that disappear
static std::vector<long long> edge_only(const moddata::Mod &mod, const mapview::MapData &md, long long l, long long r,
                                        long long north, long long south, const std::vector<long long> &gone)
{
    std::vector<std::uint8_t> sea(static_cast<size_t>(std::max(0LL, md.n)), 0);
    for (const auto &[pid, pr] : mod.provinces)
        if (pid > 0 && pid < md.n && pr.sea)
            sea[size_t(pid)] = 1;
    const std::set<long long> g(gone.begin(), gone.end());
    std::vector<long long> out;
    for (const long long x : edge_band_gone(md.idmap, md.n, sea, l, r, north, south))
        if (!g.count(x))
            out.push_back(x);
    return out;
}

void MapSizeWorkspace::_changed()
{
    _armed = false;
    const long long tot = total(), l = std::min(left->value(), tot);
    const long long r = tot - l;
    const auto [north, south] = cut_rows();
    const long long htot = north + south;
    if (growing()) {
        _changed_grow(tot, l, r, north, south);
        return;
    }
    if (tot) {
        const auto ts = tile_side(md.w - tot);
        total_lbl->setText(u"→ %1 px wide, map tiles of %2 px"_s.arg(md.w - tot).arg(ts ? QString::number(*ts)
                                                                                       : u"None"_s));
    } else {
        total_lbl->setText(u"nothing is cut"_s);
    }
    right_lbl->setText(u"→ %1 px from the right end"_s.arg(r));
    h_lbl->setText(htot ? u"→ %1 px high"_s.arg(md.h - htot) : u"the height stays"_s);
    bottom_lbl->setText(u"→ %1 px from the bottom (south)"_s.arg(south));
    preview->set_cut(l, r, north, south);
    const long long pend = doc ? doc->pending_count() : 0;
    if (!tot && !htot) {
        result->setText(u"Nothing is cut. Press ＋ above to go to the first width (or height) the game takes."_s);
        warn->hide();
        cut_btn->setEnabled(false);
        cut_btn->setText(u"✂ Cut the map"_s);
        return;
    }
    const auto [gone, part] = affected(md, l, r, north, south);
    const std::vector<long long> edge = edge_only(mod, md, l, r, north, south, gone);
    const auto &provs = mod.provinces;
    auto names = [&provs](const std::vector<long long> &lst, size_t n = 10) {
        QStringList out;
        for (size_t i = 0; i < lst.size() && i < n; ++i) {
            const long long p = lst[i];
            if (const moddata::Province *pr = provs.get(p)) {
                const QString nm = py::strip(pr->name);
                out << u"#%1 %2"_s.arg(p).arg(nm.isEmpty() ? u"?"_s : nm);
            } else {
                out << u"#%1"_s.arg(p);
            }
        }
        return out.join(u", "_s) + (lst.size() > n ? u" … (+%1)"_s.arg(lst.size() - n) : QString());
    };
    const std::vector<MapFile> files = files_of(mod);
    QStringList rows;
    rows << u"<b>New size: %1 × %2 px</b> (now %3 × %4) — %5 px off the left end, %6 px off the right end, %7 off "
            u"the top, %8 off the bottom."_s.arg(md.w - tot)
                .arg(md.h - htot)
                .arg(md.w)
                .arg(md.h)
                .arg(l)
                .arg(r)
                .arg(north)
                .arg(south);
    const auto nside = tile_side(md.w - tot);
    const bool south_whole = south > 0 && nside && (md.h - htot) % *nside == 0;
    if (south_whole)                                 // (C++ only, tried 2026-11-18; _top_min keeps it from happening)
        rows << u"<span style='color:%1'><b>This cut is refused:</b> %2</span>"_s.arg(theme::ERROR_,
                                                                                    south_whole_note(md.h - htot, *nside));
    else if (htot && _top_min() == htot)
        rows << u"<span style='color:%1'><b>All %2 rows come off the top</b> — %3 px is a whole number of the map's "
                u"tiles (%4 of %5 px). Cut from the bottom to such a height, the game crashed while it loaded the map "
                u"(tried: 1440 and 1728 px from the base game); cut from the top it loaded. To take rows off the bottom, "
                u"cut 8 px more or less.</span>"_s.arg(theme::WARN)
                    .arg(htot)
                    .arg(md.h - htot)
                    .arg((md.h - htot) / *nside)
                    .arg(*nside);
    if (north || south)
        rows << u"<b>The two new southernmost rows are made sea and the two northernmost carry no river</b> — land "
                u"and rivers there take the nearest sea / the nearest pixel without a river of their row. Land or a "
                u"river in the two southernmost rows, or a river in the two northernmost, crashed the game while it "
                u"loaded (tried); the base game has only sea along its south edge."_s;
    if (l > 0)
        rows << u"<b>The two new westernmost columns are made sea and carry no river</b> — each land pixel takes the "
                u"nearest sea of its column. Owned land there crashed the game at the main menu, when it drew the "
                u"country borders (tried after cuts from the west)."_s;
    const bool one_g = gone.size() == 1, one_p = part.size() == 1;
    QString gone_tail = u"."_s;
    if (!gone.empty())
        gone_tail = u": %1. %2"_s.arg(
            names(gone),
            del_box->isChecked()
                ? u"They are deleted first, the same way as with Drawing → Delete province: their history files, pops "
                  u"(every start date), state, continent, climate, places and adjacency rows go, and the highest "
                  u"provinces take their numbers so the numbers have no gap."_s
                : u"<span style='color:%1'><b>They stay in the files as provinces without land</b> — with their "
                  u"owner, pops and state. The game crashes on a land province without a pixel (tried), so "
                  u"afterwards paint them back onto the map in Drawing or delete them (the Errors tab lists them). "
                  u"Tick the box below to have them deleted now.</span>"_s.arg(theme::ERROR_));
    rows << u"<b>%1 province%2 disappear%3 completely</b>%4"_s.arg(gone.size())
                .arg(one_g ? QString() : u"s"_s, one_g ? u"s"_s : QString(), gone_tail);
    if (!edge.empty()) {                             // (C++ only, 2026-11-18)
        const bool one_e = edge.size() == 1;
        rows << u"<span style='color:%1'><b>%2 more province%3 would be left only on the edge that is made sea</b>: "
                u"%4. %5</span>"_s.arg(del_box->isChecked() ? theme::WARN : theme::ERROR_)
                    .arg(edge.size())
                    .arg(one_e ? QString() : u"s"_s, names(edge),
                         del_box->isChecked()
                             ? u"They would have no pixel left, which crashes the game (tried): they are deleted with "
                               u"the ones that disappear."_s
                             : u"They would have no pixel left, which crashes the game (tried) — tick the box below to "
                               u"have them deleted, or cut a different amount. The cut is refused until then."_s);
    }
    // (C++ only, 2026-11-09, the user's wish) events / decisions that name a province the cut removes: listed in red
    // BEFORE the cut — a script naming a province the mod no longer has crashes the game when it runs (measured)
    if (!gone.empty() && doc) {
        scriptstore::ScriptStore *store = nullptr;
        if (const store::StoreP *sp = doc->maps.get(u"scripts"_s))
            store = dynamic_cast<scriptstore::ScriptStore *>(sp->get());
        static QHash<QString, std::pair<size_t, std::vector<provrenumber::ProvRef>>> cache;  // rel -> (hash, refs)
        const QSet<long long> gset(gone.begin(), gone.end());
        QStringList hits;
        long long nplaces = 0;
        try {
            for (const QString folder : {u"events"_s, u"decisions"_s})
                for (const QString &rel : entryview::_files(mod, store, folder, true)) {
                    const QString t = store ? store->text(rel) : QString();
                    const size_t hs = qHash(t);
                    auto c = cache.find(rel);
                    if (c == cache.end() || c->first != hs)
                        c = cache.insert(rel, {hs, provrenumber::province_refs(t)});
                    QStringList where;
                    for (const provrenumber::ProvRef &x : c->second)
                        if (!x.note && gset.contains(x.num)) {
                            ++nplaces;
                            if (where.size() < 6)
                                where << u"line %1 (#%2)"_s.arg(t.left(x.pos).count(u'\n') + 1).arg(x.num);
                        }
                    if (!where.isEmpty())
                        hits << u"%1: %2"_s.arg(rel.toHtmlEscaped(), where.join(u", "_s));
                }
        } catch (const std::exception &) {
        }
        if (nplaces) {
            const long long nf = hits.size();
            rows << u"<span style='color:%1'><b>%2 place%3 in %4 event / decision file%5 name%6 a province that "
                    u"disappears</b> — %7. %8</span>"_s.arg(theme::ERROR_)
                        .arg(nplaces)
                        .arg(nplaces == 1 ? QString() : u"s"_s)
                        .arg(nf)
                        .arg(nf == 1 ? QString() : u"s"_s, nplaces == 1 ? u"s"_s : QString(),
                             (nf > 8 ? hits.mid(0, 8).join(u"; "_s) + u" … (+%1 files)"_s.arg(nf - 8)
                                     : hits.join(u"; "_s)),
                             del_box->isChecked()
                                 ? u"A script naming a province the mod no longer has crashes the game when it runs "
                                   u"(tried). The cut takes each place out — a condition becomes never true, an effect "
                                   u"is removed — and leaves a “# ModEditor:” note; the Errors tab lists every one "
                                   u"afterwards, so check what each event or decision was for (a canal, a claim …)."_s
                                 : u"The provinces stay in the files, so the scripts still find them — but they have "
                                   u"no land, and an event about a place that is not on the map may not do what it "
                                   u"was meant for."_s);
        }
    }
    rows << u"<b>%1 province%2 lose%3 a part</b>%4"_s.arg(part.size())
                .arg(one_p ? QString() : u"s"_s, one_p ? u"s"_s : QString(),
                     part.empty() ? u"."_s : u": %1."_s.arg(names(part)));
    QStringList bmps, terr;
    bool has_pos = false;
    for (const MapFile &f : files) {
        if (BITMAPS.contains(f.name))
            bmps << f.name;
        if (f.name == u"positions.txt")
            has_pos = true;
        if (f.name.startsWith(u"terrain/"_s))
            terr << f.name.split(u'/').last();
    }
    rows << u"<b>Files that get the cut</b> — the same columns in each: %1.%2%3"_s.arg(
        bmps.join(u", "_s),
        has_pos && (l || south || !gone.empty())
            ? u" map/positions.txt: every place moves %1 px to the left%2; the places of the provinces that disappear "
              u"go."_s.arg(l).arg(south ? u" and %1 px down (its y counts from the south edge)"_s.arg(south) : QString())
            : QString(),
        !terr.isEmpty()
            ? u" The colour maps (%1) are cut by the same share, so the land's colours stay under the provinces."_s.arg(
                  terr.join(u", "_s))
            : QString());
    const auto mm = minimap_of(mod);
    const bool dds = mm && mm->name.endsWith(u".dds"_s);
    if (dds)
        rows << u"<b>The minimap</b> (gfx/interface/%1) %2"_s.arg(
            mm->name,
            mm_box->isChecked()
                ? u"gets the same cut — the same share off each end, then stretched back to its own size — so a place "
                  u"on it is the same place on the map again. (Seen in the game: the minimap's frame stands at the "
                  u"camera's share of the map's real width.)"_s
                : u"<span style='color:%1'>is left as it is: it then shows more than the map has, and the place it "
                  u"marks is off towards the ends.</span>"_s.arg(theme::WARN));
    mm_box->setVisible(dds);
    QStringList inherited;
    for (const MapFile &f : files)
        if (!f.own)
            inherited << f.name;
    if (!inherited.isEmpty())
        rows << u"The mod has no %1 of its own (it uses the game's): the cut ones are written into the mod; "
                u"the game's files are not touched."_s.arg(inherited.join(u", "_s));
    rows << u"Each file is copied to the backup folder first. Then the mod is read again — a map of another "
            u"size can't be undone with Ctrl+Z; to go back, put the backed-up files back."_s;
    QString html;
    for (const QString &x : rows)
        html += u"<p>%1</p>"_s.arg(x);
    result->setText(html);
    if (pend)
        warn->setText(u"⚠ %1 pending edit%2: Apply (Ctrl+S) or revert first. The cut writes the map's files "
                      u"at once and reads the mod again — pending edits would be lost."_s.arg(pend)
                          .arg(pend == 1 ? QString() : u"s"_s));
    warn->setVisible(bool(pend));
    cut_btn->setEnabled(!pend && !south_whole && (edge.empty() || del_box->isChecked()));
    cut_btn->setText(u"✂ Cut the map to %1 × %2"_s.arg(md.w - tot).arg(md.h - htot));
}

void MapSizeWorkspace::_changed_grow(long long tot, long long l, long long r, long long north, long long south)
{
    // (C++ only, 2026-11-14) what growing the map does — said before it is done
    const long long htot = north + south;
    if (tot) {
        const auto ts = tile_side(md.w + tot);
        total_lbl->setText(u"→ %1 px wide, map tiles of %2 px"_s.arg(md.w + tot).arg(ts ? QString::number(*ts)
                                                                                       : u"None"_s));
    } else {
        total_lbl->setText(u"nothing is added"_s);
    }
    right_lbl->setText(u"→ %1 px at the right end"_s.arg(r));
    h_lbl->setText(htot ? u"→ %1 px high"_s.arg(md.h + htot) : u"the height stays"_s);
    bottom_lbl->setText(u"→ %1 px at the bottom (south)"_s.arg(south));
    preview->set_cut(l, r, north, south);
    const long long pend = doc ? doc->pending_count() : 0;
    if (!tot && !htot) {
        result->setText(u"Nothing is added. Press ＋ above to go to the first width (or height) offered."_s);
        warn->hide();
        cut_btn->setEnabled(false);
        cut_btn->setText(u"＋ Grow the map"_s);
        return;
    }
    QStringList rows;
    rows << u"<b>New size: %1 × %2 px</b> (now %3 × %4) — %5 px added at the left end, %6 px at the right end, %7 at "
            u"the top, %8 at the bottom."_s.arg(md.w + tot)
                .arg(md.h + htot)
                .arg(md.w)
                .arg(md.h)
                .arg(l)
                .arg(r)
                .arg(north)
                .arg(south);
    rows << u"<b>A grown map loaded in the game</b> (wider and taller, a campaign started). The sizes offered follow "
            u"the rule for cut widths (tiles of 60 – 100 px), up to twice the size. New sea at the edge can make new "
            u"coasts and sea links there: look at the map once before you build on it."_s;
    if (north || south)
        rows << u"<b>The two southernmost rows are made sea and the two northernmost carry no river</b> — land or a "
                u"river in the two southernmost rows, or a river in the two northernmost, crashed the game while it "
                u"loaded (tried)."_s;
    rows << (tot ? u"<b>What fills the new part:</b> each new column repeats the column at that end of the map, so the "
                   u"sea tiles there grow into it (where the end column has land, the nearest sea tile along it is "
                   u"taken instead — no land is drawn out into a stripe). Draw new land or sea tiles there in "
                   u"Drawing (New province, or copy and paste provinces)."_s
                 : u"<b>What fills the new part:</b> each new row repeats the row at that edge of the map, so the "
                   u"provinces along it reach further — sea and land alike. Draw new provinces there in Drawing."_s);
    rows << u"No province disappears; no event or decision changes."_s;
    QStringList bmps, terr;
    bool has_pos = false;
    const std::vector<MapFile> files = files_of(mod);
    for (const MapFile &f : files) {
        if (BITMAPS.contains(f.name))
            bmps << f.name;
        if (f.name == u"positions.txt")
            has_pos = true;
        if (f.name.startsWith(u"terrain/"_s))
            terr << f.name.split(u'/').last();
    }
    rows << u"<b>Files that grow</b> — %1, all alike (terrain.bmp: the land's own terrain, on the sea the sea's usual "
            u"value; rivers.bmp: no river).%2%3"_s.arg(
        bmps.join(u", "_s),
        has_pos && (l || south)
            ? u" map/positions.txt: every place moves%1%2."_s.arg(
                  l ? u" %1 px to the right"_s.arg(l) : QString(),
                  south ? u" %1 px up (its y counts from the south edge)"_s.arg(south) : QString())
            : QString(),
        !terr.isEmpty() ? u" The colour maps (%1) grow by the same share, their edge colours repeated."_s.arg(
                              terr.join(u", "_s))
                        : QString());
    const auto mm = minimap_of(mod);
    const bool dds = mm && mm->name.endsWith(u".dds"_s);
    if (dds)
        rows << u"<b>The minimap</b> (gfx/interface/%1) %2"_s.arg(
            mm->name,
            mm_box->isChecked()
                ? u"gets the same share added at each end (its edge colours repeated) and is squeezed back to its own "
                  u"size, so a place on it is the same place on the map."_s
                : u"<span style='color:%1'>is left as it is: it then shows less than the map has, and the place it "
                  u"marks is off towards the middle.</span>"_s.arg(theme::WARN));
    mm_box->setVisible(dds);
    QStringList inherited;
    for (const MapFile &f : files)
        if (!f.own)
            inherited << f.name;
    if (!inherited.isEmpty())
        rows << u"The mod has no %1 of its own (it uses the game's): the grown ones are written into the mod; "
                u"the game's files are not touched."_s.arg(inherited.join(u", "_s));
    rows << u"Each file is copied to the backup folder first. Then the mod is read again — a map of another "
            u"size can't be undone with Ctrl+Z; to go back, put the backed-up files back."_s;
    QString html;
    for (const QString &x : rows)
        html += u"<p>%1</p>"_s.arg(x);
    result->setText(html);
    if (pend)
        warn->setText(u"⚠ %1 pending edit%2: Apply (Ctrl+S) or revert first. Growing writes the map's files "
                      u"at once and reads the mod again — pending edits would be lost."_s.arg(pend)
                          .arg(pend == 1 ? QString() : u"s"_s));
    warn->setVisible(bool(pend));
    cut_btn->setEnabled(!pend);
    cut_btn->setText(u"＋ Grow the map to %1 × %2"_s.arg(md.w + tot).arg(md.h + htot));
}

void MapSizeWorkspace::_cut()
{
    const long long tot = total(), l = std::min(left->value(), tot);
    const auto [north, south] = cut_rows();
    if (!(tot || north || south) || !doc || doc->pending_count() || backup_root.isEmpty())
        return;
    if (tot && (north || south)) {                   // (both at once crashed — the user, 2026-10-16: one at a time)
        warn->setText(u"⚠ Cut the width and the height one after the other, not both at once."_s);
        warn->show();
        return;
    }
    if (!_armed) {
        _armed = true;
        cut_btn->setText(growing() ? u"＋ Click again to grow — the files are backed up first"_s
                                   : u"✂ Click again to cut — the files are backed up first"_s);
        return;
    }
    _armed = false;
    if (growing()) {                                 // (C++ only) negative amounts: added; nothing disappears
        QStringList written;
        QString bdir;
        try {
            std::tie(written, bdir) = apply_cut(mod, md, -l, -(tot - l), backup_root, true, mm_box->isChecked(),
                                                -north, -south);
        } catch (const std::exception &e) {
            warn->setText(u"⚠ The map was not grown — nothing was written: %1"_s.arg(QString::fromUtf8(e.what())));
            warn->show();
            return;
        }
        Q_EMIT message(u"Map grown to %1 × %2 (%3 files written; the old ones are in %4). Reading the mod again…"_s
                           .arg(md.w + tot)
                           .arg(md.h + north + south)
                           .arg(written.size())
                           .arg(bdir));
        Q_EMIT reloadRequested();
        return;
    }
    auto [gone, _part] = affected(md, l, tot - l, north, south);
    if (const auto edge = edge_only(mod, md, l, tot - l, north, south, gone); !edge.empty()) {
        if (!del_box->isChecked()) {                 // (C++ only, 2026-11-18: see edge_only)
            QStringList ids;
            for (const long long x : edge)
                ids << u"#%1"_s.arg(x);
            warn->setText(u"⚠ The map was not cut — nothing was changed: %1 province%2 would be left only on the edge "
                          u"that is made sea (%3), with no pixel — tick “Delete the provinces that disappear”."_s
                              .arg(edge.size())
                              .arg(edge.size() == 1 ? QString() : u"s"_s, ids.join(u", "_s)));
            warn->show();
            return;
        }
        gone.insert(gone.end(), edge.begin(), edge.end());
        std::sort(gone.begin(), gone.end());
    }
    try {                                            // (C++ only) every refusal BEFORE anything is deleted or written
        apply_cut(mod, md, l, tot - l, backup_root, true, mm_box->isChecked(), north, south, true);
    } catch (const std::exception &e) {
        warn->setText(u"⚠ The map was not cut — nothing was changed: %1"_s.arg(QString::fromUtf8(e.what())));
        warn->show();
        return;
    }
    if (!gone.empty() && del_box->isChecked() && before_cut) {
        bool ok;
        QString why;
        try {                                        // the provinces the cut takes away go first (written at once)
            ok = bool(before_cut(gone));
            why = u"Apply failed — see the Errors tab"_s;
        } catch (const std::exception &e) {
            ok = false;
            why = QString::fromUtf8(e.what());
        }
        if (!ok) {
            warn->setText(u"⚠ The map was not cut: the provinces that disappear could not be deleted (%1)."_s.arg(why));
            warn->show();
            return;
        }
    }
    QStringList written;
    QString bdir;
    try {
        std::tie(written, bdir) = apply_cut(mod, md, l, tot - l, backup_root, true, mm_box->isChecked(), north, south);
    } catch (const std::exception &e) {
        warn->setText(u"⚠ The map was not cut — nothing was written: %1"_s.arg(QString::fromUtf8(e.what())));
        warn->show();
        return;
    }
    Q_EMIT message(u"Map cut to %1 × %2 (%3 files written; the old ones are in %4). Reading the mod again…"_s
                       .arg(md.w - tot)
                       .arg(md.h - north - south)
                       .arg(written.size())
                       .arg(bdir));
    Q_EMIT reloadRequested();
}

}  // namespace mapsize
