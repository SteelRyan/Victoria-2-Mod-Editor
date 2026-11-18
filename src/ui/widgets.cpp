// widgets.py — line by line.
#include "ui/widgets.h"
#include "ui/drawtext.h"
#include "core/pynum.h"
#include "ui/textselect.h"
#include "ui/theme.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace widgets {

static long long fdiv(long long a, long long b)       // Python's // for ints
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
}
static QColor rgb_color(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }
static bool light(const QColor &c) { return (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000.0 > 140; }
static const double RAD_TO_DEG = 180.0 / std::numbers::pi, DEG_TO_RAD = std::numbers::pi / 180.0;   // (CPython's)

// ---------------------------------------------------------------------------------------------- GridFlow
GridFlow::GridFlow(QWidget *parent, int spacing, int min_cell, int row_h)
    : QLayout(parent), _sp(spacing), _min(min_cell), _row(row_h)
{
    setContentsMargins(0, 0, 0, 0);
}
GridFlow::~GridFlow()
{
    while (!_items.isEmpty())
        delete _items.takeFirst();
}
void GridFlow::addItem(QLayoutItem *item) { _items.append(item); }
int GridFlow::count() const { return int(_items.size()); }
QLayoutItem *GridFlow::itemAt(int i) const { return (i >= 0 && i < _items.size()) ? _items[i] : nullptr; }
QLayoutItem *GridFlow::takeAt(int i) { return (i >= 0 && i < _items.size()) ? _items.takeAt(i) : nullptr; }
Qt::Orientations GridFlow::expandingDirections() const { return {}; }
bool GridFlow::hasHeightForWidth() const { return true; }
int GridFlow::_cols(int w) const { return int(std::max(1LL, fdiv(w + _sp, _min + _sp))); }

int GridFlow::heightForWidth(int w) const
{
    const long long n = _items.size();
    const long long rows = n ? fdiv(n + _cols(w) - 1, _cols(w)) : 0;
    return int(rows * _row + std::max(0LL, rows - 1) * _sp);
}

void GridFlow::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    const int cols = _cols(rect.width());
    const double cw = double(rect.width() - _sp * (cols - 1)) / double(cols);
    for (int i = 0; i < _items.size(); ++i) {
        const int r = int(fdiv(i, cols)), c = i - r * cols;
        const int x = rect.x() + int(py::round_int(c * (cw + _sp)));
        _items[i]->setGeometry(QRect(x, rect.y() + r * (_row + _sp), int(cw), _row));
    }
}

QSize GridFlow::sizeHint() const { return {_min, heightForWidth(_min)}; }
QSize GridFlow::minimumSize() const { return {_min, 0}; }

// ---------------------------------------------------------------------------------------------- WrapRow
WrapRow::WrapRow(QWidget *parent, int spacing, int max_per_row) : QLayout(parent), _sp(spacing), _max(max_per_row)
{
    setContentsMargins(0, 0, 0, 0);
}
WrapRow::~WrapRow()
{
    while (!_items.isEmpty())
        delete _items.takeFirst();
}
void WrapRow::addItem(QLayoutItem *item) { _items.append(item); }
int WrapRow::count() const { return int(_items.size()); }
QLayoutItem *WrapRow::itemAt(int i) const { return (i >= 0 && i < _items.size()) ? _items[i] : nullptr; }
QLayoutItem *WrapRow::takeAt(int i) { return (i >= 0 && i < _items.size()) ? _items.takeAt(i) : nullptr; }
Qt::Orientations WrapRow::expandingDirections() const { return {}; }
bool WrapRow::hasHeightForWidth() const { return true; }
int WrapRow::heightForWidth(int w) const { return _lay(QRect(0, 0, w, 0), false); }

void WrapRow::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    _lay(rect, true);
}

QSize WrapRow::sizeHint() const
{
    const int n = std::min(int(_items.size()), _max ? _max : int(_items.size()));
    int w = 0;
    for (int i = 0; i < n; ++i)
        w += _items[i]->sizeHint().width();
    w += _sp * std::max(0, n - 1);
    return {w, heightForWidth(w)};
}

QSize WrapRow::minimumSize() const
{
    QSize s(0, 0);
    for (const QLayoutItem *it : _items)
        s = s.expandedTo(it->minimumSize());
    return s;
}

int WrapRow::_lay(const QRect &rect, bool place) const
{
    int x = rect.x(), y = rect.y(), row_h = 0, n = 0;
    for (QLayoutItem *it : _items) {
        const QSize hint = it->sizeHint();
        if (n && (x + hint.width() > rect.right() + 1 || (_max && n >= _max))) {
            x = rect.x();
            y = y + row_h + _sp;
            row_h = 0;
            n = 0;
        }
        if (place)
            it->setGeometry(QRect(x, y, hint.width(), hint.height()));
        x += hint.width() + _sp;
        row_h = std::max(row_h, hint.height());
        n += 1;
    }
    return y + row_h - rect.y();
}

// ---------------------------------------------------------------------------------------------- WrapButton
WrapButton::WrapButton(const QString &text, QWidget *parent) : QPushButton(parent)
{
    _lab = new QLabel(this);
    _lab->setWordWrap(true);
    _lab->setAttribute(Qt::WA_TransparentForMouseEvents);
    _lab->setStyleSheet(u"background: transparent; border: none; padding: 0;"_s);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(11, 8, 11, 8);
    lay->addWidget(_lab);
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setText(text);
}
void WrapButton::setText(const QString &text)
{
    _lab->setText(text);
    updateGeometry();
}
QString WrapButton::text() const { return _lab->text(); }
int WrapButton::heightForWidth(int w) const { return _lab->heightForWidth(std::max(20, w - 22)) + 16; }
QSize WrapButton::sizeHint() const { return {240, heightForWidth(240)}; }
QSize WrapButton::minimumSizeHint() const { return {80, _lab->fontMetrics().height() + 16}; }

// ---------------------------------------------------------------------------------------------- ElideLabel
ElideLabel::ElideLabel(const QString &text, QWidget *parent) : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setText(text);
}
void ElideLabel::setText(const QString &text)
{
    _full = text;
    QLabel::setToolTip(_full.size() > 40 ? _full : QString());
    _elide();
}
void ElideLabel::_elide() { QLabel::setText(fontMetrics().elidedText(_full, Qt::ElideRight, std::max(0, width()))); }
void ElideLabel::resizeEvent(QResizeEvent *ev)
{
    QLabel::resizeEvent(ev);
    _elide();
}
void ElideLabel::changeEvent(QEvent *ev)
{
    QLabel::changeEvent(ev);
    _elide();                                 // the style sheet may change the font
}
QSize ElideLabel::minimumSizeHint() const { return {0, QLabel::minimumSizeHint().height()}; }

// ---------------------------------------------------------------------------------------------- Chip
QString fmt(long long n)
{
    QString d = QString::number(n < 0 ? -n : n);
    for (qsizetype i = d.size() - 3; i > 0; i -= 3)
        d.insert(i, u',');
    return n < 0 ? u'-' + d : d;
}

QIcon swatch_icon(const Rgb &rgb, int size)
{
    QPixmap pm(size, size);
    pm.fill(rgb_color(rgb));
    return QIcon(pm);
}

Chip::Chip(const QString &label_, const Rgb &rgb_, std::optional<long long> amount_, QWidget *parent)
    : QAbstractButton(parent), label(label_), rgb(rgb_), amount(amount_)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    _tip();
}

void Chip::set_narrowed(std::optional<long long> n)
{
    narrowed = n;
    _tip();
    update();
}

void Chip::_tip()
{
    QString t = u"%1: %2 people"_s.arg(label, fmt(amount.value_or(0)));
    if (narrowed)
        t += u"\n%1 of them also match the other two filters"_s.arg(fmt(*narrowed));
    setToolTip(t);
}

void Chip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool on = isChecked();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(on ? theme::GOLD_DIM : theme::LINE), 1));
    p.setBrush(QColor(theme::PANEL_ALT));
    p.drawRoundedRect(r, 3, 3);
    const double cy = r.center().y();
    // check indicator
    const QRectF box(r.left() + 6, cy - 6, 12, 12);
    p.setPen(QPen(QColor(on ? theme::GOLD : theme::LINE), 1));
    p.setBrush(on ? QColor(theme::GOLD) : QColor(theme::BG));
    p.drawRoundedRect(box, 2, 2);
    // colour swatch
    p.setPen(Qt::NoPen);
    p.setBrush(rgb_color(rgb));
    p.drawRect(QRectF(box.right() + 6, cy - 4.5, 9, 9));
    const double x = box.right() + 21;
    double right = r.right() - 7;
    // numbers, right-aligned: total, then (narrowed) in gold
    p.setFont(QFont(font()));
    const QFontMetrics fm = p.fontMetrics();
    if (narrowed) {
        const QString s = u"(%1)"_s.arg(fmt(*narrowed));
        const int w = fm.horizontalAdvance(s);
        p.setPen(QColor(on ? theme::GOLD : theme::GOLD_DIM));
        drawtext::draw(p, QRectF(right - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, s);
        right -= w + 5;
    }
    if (amount) {
        const QString s = fmt(*amount);
        const int w = fm.horizontalAdvance(s);
        p.setPen(QColor(on ? theme::INK : theme::INK_FAINT));
        drawtext::draw(p, QRectF(right - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, s);
        right -= w + 8;
    }
    p.setPen(QColor(on ? theme::INK : theme::INK_DIM));
    const QString name = fm.elidedText(label, Qt::ElideRight, int(std::max(0.0, right - x)));
    drawtext::draw(p, QRectF(x, r.top(), std::max(0.0, right - x), r.height()), Qt::AlignVCenter | Qt::AlignLeft, name);
}

// ---------------------------------------------------------------------------------------------- ChipGroup
ChipGroup::ChipGroup(const QString &title, QWidget *parent, bool invert_) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    auto *lab = new QLabel(title);
    lab->setObjectName(u"faint"_s);
    lay->addWidget(lab);
    flow_host = new QWidget();
    flow = new GridFlow(flow_host, 5);
    lay->addWidget(flow_host);
    auto *links = new QHBoxLayout();
    links->setSpacing(10);
    std::vector<std::pair<QString, void (ChipGroup::*)()>> ls = {{u"Select all"_s, &ChipGroup::select_all},
                                                                 {u"Select none"_s, &ChipGroup::select_none}};
    if (invert_)
        ls.emplace_back(u"Invert"_s, &ChipGroup::invert);
    for (const auto &[text, fn] : ls) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"link"_s);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, fn);
        links->addWidget(b);
    }
    links->addStretch(1);
    lay->addLayout(links);
}

void ChipGroup::set_items(const std::vector<ItemSpec> &items)
{
    while (flow->count()) {
        QLayoutItem *it = flow->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    boxes = {};
    for (const ItemSpec &s : items) {
        auto *cb = new Chip(s.label, s.rgb, s.amount);
        cb->setChecked(!unchecked.contains(s.key));
        const QString key = s.key;
        connect(cb, &Chip::toggled, this, [this, key](bool on) { _toggled(key, on); });
        flow->addWidget(cb);
        boxes.insert(s.key, cb);
    }
    flow_host->updateGeometry();
}

void ChipGroup::set_narrowed(const std::optional<OrderedMap<QString, long long>> &counts)
{
    for (const auto &[k, cb] : boxes)
        cb->set_narrowed(counts ? std::optional<long long>(counts->value(k, 0)) : std::nullopt);
}

void ChipGroup::_toggled(const QString &key, bool on)
{
    if (on)
        unchecked.remove(key);
    else
        unchecked.insert(key);
    Q_EMIT changed();
}

void ChipGroup::_set_all(const std::function<bool(const QString &, bool)> &fn)
{
    for (const auto &[k, cb] : boxes) {
        cb->blockSignals(true);
        cb->setChecked(fn(k, cb->isChecked()));
        cb->blockSignals(false);
        if (cb->isChecked())
            unchecked.remove(k);
        else
            unchecked.insert(k);
    }
    Q_EMIT changed();
}

void ChipGroup::select_all() { _set_all([](const QString &, bool) { return true; }); }
void ChipGroup::select_none() { _set_all([](const QString &, bool) { return false; }); }
void ChipGroup::invert() { _set_all([](const QString &, bool c) { return !c; }); }

QSet<QString> ChipGroup::checked() const
{
    QSet<QString> out;
    for (const auto &[k, cb] : boxes)
        if (cb->isChecked())
            out.insert(k);
    return out;
}

// ---------------------------------------------------------------------------------------------- push_edge
void push_edge(std::vector<Segment> &segments, long long i, double pos)
{
    // Move the edge after segment i to `pos` (0..100 %), pushing any edges it passes.
    std::vector<double> b = {0.0};
    for (const Segment &s : segments)
        b.push_back(b.back() + s.pct);
    b.back() = 100.0;
    pos = std::max(0.0, std::min(100.0, pos));
    b[size_t(i + 1)] = pos;
    for (size_t j = size_t(i + 2); j + 1 < b.size(); ++j)
        b[j] = std::max(b[j], pos);
    for (size_t j = 1; j <= size_t(i); ++j)
        b[j] = std::min(b[j], pos);
    for (size_t k = 0; k < segments.size(); ++k)
        segments[k].pct = std::max(0.0, b[k + 1] - b[k]);
}

const std::vector<Rgb> SEG_COLORS = {{201, 161, 90}, {95, 179, 179}, {176, 122, 161}, {122, 155, 118}, {184, 101, 79},
                                     {110, 140, 200}, {214, 190, 120}, {140, 110, 190}, {90, 160, 120}, {200, 130, 90}};

// ---------------------------------------------------------------------------------------------- SplitBar
SplitBar::SplitBar(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);
    setMinimumHeight(54);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SplitBar::set_segments(const Segs &segs)
{
    segments_ = segs;
    update();
}

QRectF SplitBar::_bar() const { return QRectF(1, 16, width() - 2, height() - 18); }

std::vector<double> SplitBar::_edges() const
{
    const QRectF r = _bar();
    std::vector<double> xs;
    double acc = 0.0;
    for (size_t i = 0; i + 1 < seg().size(); ++i) {
        acc += seg()[i].pct;
        xs.push_back(r.left() + r.width() * acc / 100.0);
    }
    return xs;
}

std::optional<long long> SplitBar::_hit(double x) const
{
    // several edges can coincide (0% segments): prefer the right-most, which can open leftwards
    std::optional<long long> hit;
    double best = 5.0;
    const auto es = _edges();
    for (size_t i = 0; i < es.size(); ++i) {
        const Segment &a = seg()[i], &b = seg()[i + 1];
        if (std::fabs(es[i] - x) <= best && !a.locked && !b.locked) {
            hit = static_cast<long long>(i);  // nearest edge wins; on a tie the later one
            best = std::fabs(es[i] - x);
        }
    }
    return hit;
}

void SplitBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = _bar();
    p.setPen(QPen(QColor(theme::LINE), 1));
    p.setBrush(QColor(theme::PANEL_ALT));
    p.drawRoundedRect(r, 3, 3);
    if (seg().empty()) {
        p.setPen(QColor(theme::INK_FAINT));
        drawtext::draw(p, r, Qt::AlignCenter, empty_text.value_or(u"Click provinces on the map to add split targets"_s));
        return;
    }
    double x = r.left();
    p.setFont(QFont(theme::MONO, 8));
    for (const Segment &s : seg()) {
        const double w = r.width() * s.pct / 100.0;
        const QRectF seg(x, r.top(), w, r.height());
        QColor c = rgb_color(s.color);
        if (s.locked)
            c = c.darker(150);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(seg);
        if (w > 34) {
            p.setPen(light(c) ? QColor(u"#14171c"_s) : QColor(theme::INK));
            drawtext::draw(p, seg.adjusted(2, 0, -2, 0), Qt::AlignCenter,
                       r.height() > 30 ? u"%1\n%2%"_s.arg(s.label, py::fmt_f(s.pct, 1)) : py::fmt_f(s.pct, 1) + u'%');
        }
        // label above
        p.setPen(QColor(theme::INK_DIM));
        if (w > 20)
            drawtext::draw(p, QRectF(x, 0, w, 14), Qt::AlignLeft | Qt::AlignVCenter,
                       (s.locked ? u" 🔒"_s : u" "_s) + s.label);
        x += w;
    }
    p.setPen(QPen(QColor(theme::BG), 2));
    for (double ex : _edges()) {
        p.drawLine(int(ex), int(r.top()), int(ex), int(r.bottom()));
        p.setBrush(QColor(theme::INK));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(ex - 2, r.center().y() - 8, 4, 16), 2, 2);
        p.setPen(QPen(QColor(theme::BG), 2));
    }
}

void SplitBar::mouseMoveEvent(QMouseEvent *ev)
{
    const double x = ev->position().x();
    if (_drag && push) {
        const QRectF r = _bar();
        push_edge(seg(), *_drag, double(py::round_int((x - r.left()) / r.width() * 1000)) / 10.0);
        update();
        Q_EMIT changed();
    } else if (_drag) {
        const long long i = *_drag;
        Segment &a = seg()[size_t(i)], &b = seg()[size_t(i + 1)];
        const QRectF r = _bar();
        double before = 0.0;                      // sum(s["pct"] for s in self.segments[:i])
        {
            py::Sum s;
            for (long long k = 0; k < i; ++k)
                s.add(seg()[size_t(k)].pct);
            before = s.value();
        }
        const double left = r.left() + r.width() * before / 100.0;
        const double pair = a.pct + b.pct;
        double new_a = (x - left) / r.width() * 100.0;
        new_a = std::max(0.0, std::min(pair, double(py::round_int(new_a * 10)) / 10.0));
        a.pct = new_a;
        b.pct = py::round_to(pair - new_a, 6);
        update();
        Q_EMIT changed();
    } else
        setCursor(_hit(x) ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void SplitBar::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        _drag = _hit(ev->position().x());
        _base.clear();
        for (const Segment &s : seg())
            _base.push_back(s.pct);
    }
}

void SplitBar::mouseReleaseEvent(QMouseEvent *) { _drag.reset(); }

// ---------------------------------------------------------------------------------------------- ZoomBar
ZoomBar::ZoomBar(QWidget *parent) : SplitBar(parent) { push = true; }

void ZoomBar::set_range(double lo_, double hi_)
{
    if (!_drag) {
        lo = std::max(0.0, lo_);
        hi = std::min(100.0, std::max(hi_, lo_ + 0.5));
        setToolTip(u"Zoomed view of %1–%2% of the bar, for the small segments"_s.arg(py::fmt_f(lo, 1), py::fmt_f(hi, 1)));
        update();
    }
}

double ZoomBar::_x(double pct) const
{
    const QRectF r = _bar();
    return r.left() + r.width() * (pct - lo) / (hi - lo);
}

std::vector<double> ZoomBar::_edges() const
{
    std::vector<double> xs;
    double acc = 0.0;
    for (size_t i = 0; i + 1 < seg().size(); ++i) {
        acc += seg()[i].pct;
        xs.push_back(_x(acc));
    }
    return xs;
}

void ZoomBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = _bar();
    p.setPen(QPen(QColor(theme::LINE), 1));
    p.setBrush(QColor(theme::PANEL_ALT));
    p.drawRoundedRect(r, 3, 3);
    p.setClipRect(r);
    p.setFont(QFont(theme::MONO, 8));
    double acc = 0.0;
    std::vector<std::pair<QRectF, QString>> labels;
    for (const Segment &s : seg()) {
        const double x0 = _x(acc), x1 = _x(acc + s.pct);
        acc += s.pct;
        if (x1 <= r.left() || x0 >= r.right())
            continue;
        const QRectF seg(x0, r.top(), x1 - x0, r.height());
        const QColor c = rgb_color(s.color);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(seg);
        const QRectF vis(std::max(x0, r.left()), r.top(), std::min(x1, r.right()) - std::max(x0, r.left()), r.height());
        if (vis.width() > 38) {
            p.setPen(light(c) ? QColor(u"#14171c"_s) : QColor(theme::INK));
            drawtext::draw(p, vis, Qt::AlignCenter, py::fmt_f(s.pct, 2) + u'%');
        }
        if (vis.width() > 20)
            labels.emplace_back(vis, s.label);
    }
    for (double ex : _edges())
        if (r.left() - 1 <= ex && ex <= r.right() + 1) {
            p.setPen(QPen(QColor(theme::BG), 2));
            p.drawLine(int(ex), int(r.top()), int(ex), int(r.bottom()));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(theme::INK));
            p.drawRoundedRect(QRectF(ex - 2, r.center().y() - 8, 4, 16), 2, 2);
        }
    p.setClipping(false);
    p.setPen(QColor(theme::INK_DIM));
    const QFontMetrics fm = p.fontMetrics();
    for (const auto &[vis, text] : labels) {
        const QString t = fm.elidedText(text, Qt::ElideRight, int(vis.width()) - 4);
        if (!t.isEmpty() && t != u"…")
            drawtext::draw(p, QRectF(vis.left() + 2, 0, vis.width() - 2, 14), Qt::AlignLeft | Qt::AlignVCenter, t);
    }
}

void ZoomBar::mouseMoveEvent(QMouseEvent *ev)
{
    const double x = ev->position().x();
    if (_drag) {
        const QRectF r = _bar();
        const double pos = lo + (x - r.left()) / r.width() * (hi - lo);
        push_edge(seg(), *_drag, double(py::round_int(pos * 100)) / 100.0);
        update();
        Q_EMIT changed();
    } else
        setCursor(_hit(x) ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void ZoomBar::mouseReleaseEvent(QMouseEvent *)
{
    _drag.reset();
    Q_EMIT released();
}

// ---------------------------------------------------------------------------------------------- PieChart
PieChart::PieChart(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);
    setFixedHeight(240);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PieChart::set_segments(const Segs &segs)
{
    segments_ = segs;
    update();
}

void PieChart::set_outer(const std::vector<Segment> &segs)
{
    outer = segs;
    update();
}

std::tuple<double, double, double> PieChart::_geom() const
{
    // Centre and radius of the editable (inner) pie.
    const double side = std::min(width(), height()) - 16;
    const double r = side / 2.0 - (outer.empty() ? 0 : 17);
    return {width() / 2.0, height() / 2.0, r};
}

std::vector<double> PieChart::_bounds() const
{
    std::vector<double> out;
    double acc = 0.0;
    for (size_t i = 0; i + 1 < seg().size(); ++i) {
        acc += seg()[i].pct;
        out.push_back(acc * 3.6);
    }
    return out;
}

std::tuple<double, double, double> PieChart::_angle(const QPointF &pos) const
{
    const auto [cx, cy, r] = _geom();
    const double dx = pos.x() - cx, dy = pos.y() - cy;
    return {py::mod(std::atan2(dx, -dy) * RAD_TO_DEG, 360.0), py::hypot(dx, dy), r};
}

std::optional<long long> PieChart::_hit(const QPointF &pos) const
{
    const auto [ang, dist, r] = _angle(pos);
    if (dist < r * 0.2 || dist > r + 8)
        return std::nullopt;
    std::optional<long long> hit;
    double best = std::max(4.0, 700.0 / std::max(dist, 1.0));
    const auto bs = _bounds();
    for (size_t i = 0; i < bs.size(); ++i) {
        const double d = std::min(std::fabs(ang - bs[i]), 360 - std::fabs(ang - bs[i]));
        if (d <= best) {                      // nearest edge wins; on a tie the later one
            hit = static_cast<long long>(i);
            best = d;
        }
    }
    return hit;
}

void PieChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto [cx, cy, r] = _geom();
    const QRectF rect(cx - r, cy - r, 2 * r, 2 * r);
    double total;
    {
        py::Sum s;
        for (const Segment &x : seg())
            s.add(x.pct);
        total = s.value();
    }
    if (seg().empty() || total <= 0) {
        p.setPen(QPen(QColor(theme::LINE), 1));
        p.setBrush(QColor(theme::PANEL_ALT));
        p.drawEllipse(rect);
        p.setPen(QColor(theme::INK_FAINT));
        drawtext::draw(p, rect, Qt::AlignCenter, u"No pops selected"_s);
        return;
    }
    if (!outer.empty()) {
        const double R = r + 15;
        const QRectF ring(cx - R, cy - R, 2 * R, 2 * R);
        double a0 = 0.0;
        for (const Segment &s : outer) {
            const double span = s.pct * 3.6;
            if (span > 0) {
                p.setPen(QPen(QColor(theme::BG), 1));
                p.setBrush(rgb_color(s.color));
                p.drawPie(ring, int((90 - a0) * 16), int(-span * 16));
            }
            a0 += span;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(theme::PANEL));
        p.drawEllipse(QRectF(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8));
    }
    double start = 0.0;
    p.setFont(QFont(theme::MONO, 8));
    for (const Segment &s : seg()) {
        const double span = s.pct * 3.6;
        if (span > 0) {
            p.setPen(QPen(QColor(theme::BG), 1.5));
            p.setBrush(rgb_color(s.color));
            // Qt angles: counter-clockwise from 3 o'clock, in 1/16 deg
            p.drawPie(rect, int((90 - start) * 16), int(-span * 16));
            if (span >= 18) {
                const double mid = (start + span / 2) * DEG_TO_RAD;
                const double tx = cx + std::sin(mid) * r * 0.62, ty = cy - std::cos(mid) * r * 0.62;
                const QColor c = rgb_color(s.color);
                p.setPen(light(c) ? QColor(u"#14171c"_s) : QColor(theme::INK));
                drawtext::draw(p, QRectF(tx - 45, ty - 14, 90, 28), Qt::AlignCenter, u"%1\n%2%"_s.arg(s.label, py::fmt_f(s.pct, 1)));
            }
        }
        start += span;
    }
    // edge handles
    p.setPen(Qt::NoPen);
    for (double b : _bounds()) {
        const double a = b * DEG_TO_RAD;
        const double hx = cx + std::sin(a) * (r + 1), hy = cy - std::cos(a) * (r + 1);
        p.setBrush(QColor(theme::INK));
        p.drawEllipse(QRectF(hx - 4, hy - 4, 8, 8));
    }
    p.setBrush(QColor(theme::PANEL));
    p.drawEllipse(QRectF(cx - r * 0.18, cy - r * 0.18, r * 0.36, r * 0.36));
}

void PieChart::mouseMoveEvent(QMouseEvent *ev)
{
    const QPointF pos = ev->position();
    if (_drag && push) {
        double ang = std::get<0>(_angle(pos));
        const std::optional<double> last = _last_ang;
        if (last && *last > 270 && ang < 90)
            ang = 360.0;
        else if (last && *last < 90 && ang > 270)
            ang = 0.0;
        _last_ang = ang;
        push_edge(seg(), *_drag, py::round_to(ang / 3.6, 2));
        update();
        Q_EMIT changed();
    } else if (_drag) {
        const long long i = *_drag;
        double ang = std::get<0>(_angle(pos));
        std::vector<double> bounds = {0.0};
        for (double b : _bounds())
            bounds.push_back(b);
        bounds.push_back(360.0);
        const double lo = bounds[size_t(i)], hi = bounds[size_t(i + 2)];
        // stay on the side of the circle we started from (no jumping over 12 o'clock)
        if (ang < lo - 90)
            ang += 360;
        ang = std::max(lo, std::min(hi, ang));
        Segment &a = seg()[size_t(i)], &b = seg()[size_t(i + 1)];
        const double pair = a.pct + b.pct;
        const double new_a = py::round_to((ang - lo) / 3.6, 2);
        a.pct = std::max(0.0, std::min(pair, new_a));
        b.pct = std::max(0.0, pair - std::max(0.0, std::min(pair, new_a)));
        update();
        Q_EMIT changed();
    } else {
        setCursor(_hit(pos) ? Qt::SizeAllCursor : Qt::ArrowCursor);
        // tooltip for the wedge under the cursor (tiny wedges have no label)
        const auto [ang, dist, r] = _angle(pos);
        QString tip;
        if (dist <= r) {
            double acc = 0.0;
            for (const Segment &s : seg()) {
                if (acc * 3.6 <= ang && ang < (acc + s.pct) * 3.6) {
                    tip = u"%1  %2%"_s.arg(s.label, py::fmt_f(s.pct, 2));
                    break;
                }
                acc += s.pct;
            }
        } else if (!outer.empty() && dist <= r + 16) {
            double acc = 0.0;
            for (const Segment &s : outer) {
                if (acc * 3.6 <= ang && ang < (acc + s.pct) * 3.6) {
                    tip = u"All pops: %1  %2%"_s.arg(s.label, py::fmt_f(s.pct, 2));
                    break;
                }
                acc += s.pct;
            }
        }
        if (tip != toolTip()) {
            setToolTip(tip);
            if (!tip.isEmpty())
                QToolTip::showText(ev->globalPosition().toPoint(), tip, this);
            else
                QToolTip::hideText();
        }
    }
}

void PieChart::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        _drag = _hit(ev->position());
        _last_ang = _drag ? std::optional<double>(_bounds()[size_t(*_drag)]) : std::nullopt;
        _base.clear();
        for (const Segment &s : seg())
            _base.push_back(s.pct);
    }
}

void PieChart::mouseReleaseEvent(QMouseEvent *) { _drag.reset(); }

// ---------------------------------------------------------------------------------------------- PageStack
PageStack::PageStack(QWidget *parent) : QWidget(parent)
{
    _lay = new QVBoxLayout(this);
    _lay->setContentsMargins(0, 0, 0, 0);
    _lay->setSpacing(0);
}

int PageStack::addWidget(QWidget *w)
{
    _pages.push_back(w);
    _lay->addWidget(w);
    if (_cur < 0) {
        _cur = 0;
        w->setVisible(true);
    } else
        w->setVisible(false);
    return int(_pages.size()) - 1;
}

QWidget *PageStack::widget(int i) const { return (i >= 0 && i < int(_pages.size())) ? _pages[size_t(i)] : nullptr; }

int PageStack::indexOf(QWidget *w) const
{
    for (size_t i = 0; i < _pages.size(); ++i)
        if (_pages[i] == w)
            return int(i);
    return -1;
}

void PageStack::setCurrentIndex(int i)
{
    if (i == _cur || !(i >= 0 && i < int(_pages.size())))
        return;
    QWidget *old = _pages[size_t(_cur)];
    _cur = i;
    _pages[size_t(i)]->setVisible(true);
    old->setVisible(false);
    Q_EMIT currentChanged(i);
}

// ---------------------------------------------------------------------------------------------- Section
Section::Section(QWidget *parent) : QFrame(parent)
{
    setObjectName(u"section"_s);
    lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(6);
}

// ---------------------------------------------------------------------------------------------- SliderField
SliderField::SliderField(double minimum, double soft_max, double hard_max, double value, const QString &suffix,
                         bool log, int decimals, QWidget *parent)
    : QWidget(parent), _min(minimum), _soft(soft_max), _hard(hard_max), _log(log), _dec(decimals), _suffix(suffix)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 1000);
    slider->setMinimumWidth(110);
    connect(slider, &QSlider::valueChanged, this, &SliderField::_slid);
    edit = new QLineEdit();
    edit->setFixedWidth(56);
    edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(edit, &QLineEdit::editingFinished, this, &SliderField::_typed);
    lay->addWidget(slider, 1);
    lay->addWidget(edit);
    if (!suffix.isEmpty()) {
        auto *lab = new QLabel(suffix);
        lab->setObjectName(u"sub"_s);
        lay->addWidget(lab);
    }
    setValue(value);
}

int SliderField::_to_pos(double v) const
{
    const double lo = _min, hi = _soft;
    v = std::min(std::max(v, lo), hi);
    if (_log && lo >= 0) {
        const double a = std::log(lo + 1.0), b = std::log(hi + 1.0);
        return b > a ? int(py::round_int((std::log(v + 1.0) - a) / (b - a) * 1000)) : 0;
    }
    return hi > lo ? int(py::round_int((v - lo) / (hi - lo) * 1000)) : 0;
}

double SliderField::_from_pos(int pos) const
{
    const double lo = _min, hi = _soft;
    double v;
    if (_log && lo >= 0) {
        const double a = std::log(lo + 1.0), b = std::log(hi + 1.0);
        v = std::exp(a + (b - a) * pos / 1000.0) - 1.0;
    } else
        v = lo + (hi - lo) * pos / 1000.0;
    return _round(v);
}

double SliderField::_round(double v) const
{
    v = std::min(std::max(v, _min), _hard);
    return _dec ? py::round_to(v, _dec) : double(py::round_int(v));
}

QString SliderField::_text(double v) const
{
    return _dec ? py::fmt_f(v, _dec) : QString::number(static_cast<long long>(v));
}

void SliderField::_show()
{
    edit->setText(_text(_val));
    slider->blockSignals(true);
    slider->setValue(_to_pos(_val));
    slider->blockSignals(false);
}

void SliderField::set_snaps(const std::vector<double> &values, double tolerance)
{
    // Values the slider snaps to when dragged near them (a share of the slider's range).
    _snaps.clear();
    for (double v : values)
        _snaps.emplace_back(v, tolerance * (_soft - _min));
}

void SliderField::_slid(int pos)
{
    double v = _from_pos(pos);
    for (const auto &[s, tol] : _snaps)
        if (std::fabs(v - s) <= tol) {
            v = s;
            slider->blockSignals(true);
            slider->setValue(_to_pos(s));
            slider->blockSignals(false);
        }
    if (!_valopt || v != _val) {
        _val = v;
        _valopt = v;
        edit->setText(_text(v));
        Q_EMIT valueChanged(v);
    }
}

void SliderField::_typed()
{
    const auto f = py::to_float(edit->text().replace(u',', u'.'));
    if (!f || !std::isfinite(*f)) {
        _show();
        return;
    }
    const double v = _round(*f);
    if (!_valopt || v != _val) {
        _val = v;
        _valopt = v;
        _show();
        Q_EMIT valueChanged(v);
    } else
        _show();
}

void SliderField::setValue(double v)
{
    v = _round(v);
    const bool changed = !_valopt || v != _val;
    _val = v;
    _valopt = v;
    _show();
    if (changed && !signalsBlocked())
        Q_EMIT valueChanged(v);
}

void SliderField::setToolTip(const QString &text)
{
    QWidget::setToolTip(text);
    slider->setToolTip(text);
    edit->setToolTip(text);
}

// ---------------------------------------------------------------------------------------------- counts, badges
void paint_counts(QPainter &p, const QRect &rect, long long errors, long long warnings, int right)
{
    QFont f(p.font());
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    p.setRenderHint(QPainter::Antialiasing);
    double x = rect.right() - right;
    const std::tuple<long long, QString, QString> parts[] = {{errors, theme::ERROR_, u"#ffffff"_s},
                                                            {warnings, theme::WARN, u"#14100a"_s}};
    for (const auto &[n, bg, fg] : parts) {
        if (!n)
            continue;
        const QString t = n < 1000 ? QString::number(n) : u"%1k"_s.arg(fdiv(n, 1000));
        const int w = std::max(19, p.fontMetrics().horizontalAdvance(t) + 11);
        const QRectF r(x - w, rect.top() + (rect.height() - 19) / 2.0, w, 19);
        p.setPen(QPen(QColor(u"#14100a"_s), 1.6));
        p.setBrush(QColor(bg));
        p.drawRoundedRect(r, 9.5, 9.5);
        p.setPen(QColor(fg));
        drawtext::draw(p, r, Qt::AlignCenter, t);
        x -= w + 3;
    }
}

BadgeButton::BadgeButton(const QString &text, QWidget *parent) : QPushButton(text, parent) {}

void BadgeButton::setToolTip(const QString &tip)
{
    _tip = tip;
    _show_tip();
}

void BadgeButton::_show_tip()
{
    QStringList parts;
    if (errors)
        parts << u"%1 error%2"_s.arg(errors).arg(errors == 1 ? u""_s : u"s"_s);
    if (warnings)
        parts << u"%1 warning%2"_s.arg(warnings).arg(warnings == 1 ? u""_s : u"s"_s);
    const QString extra = parts.isEmpty() ? QString()
                                          : parts.join(u" · "_s) + u" here — see them with the red “here ▸” button at the top"_s;
    QStringList t;
    for (const QString &s : {_tip, extra})
        if (!s.isEmpty())
            t << s;
    QPushButton::setToolTip(t.join(u'\n'));
}

void BadgeButton::set_counts(long long e, long long w)
{
    if (e != errors || w != warnings) {
        errors = e;
        warnings = w;
        _show_tip();
        update();
    }
}

void BadgeButton::paintEvent(QPaintEvent *e)
{
    QPushButton::paintEvent(e);
    if (errors || warnings) {
        QPainter p(this);
        paint_counts(p, rect(), errors, warnings);
        p.end();
    }
}

TintHeader::TintHeader(const QColor &color_, const QSet<int> &cols_, QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent), color(color_), cols(cols_)
{
    setSectionsClickable(true);
}

void TintHeader::paintSection(QPainter *p, const QRect &rect, int i) const
{
    QHeaderView::paintSection(p, rect, i);
    if (cols.contains(i)) {
        p->save();
        p->fillRect(QRect(rect.left() + 3, rect.bottom() - 3, rect.width() - 6, 4), color);
        p->restore();
    }
}

HelpDot::HelpDot(const QString &note_, QWidget *parent) : QLabel(u"?"_s, parent)
{
    setFixedSize(22, 22);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::WhatsThisCursor);
    setStyleSheet(u"QLabel { background:%1; color:#1a1612; border-radius:11px; font-weight:800; font-size:13px; }"_s.arg(
        theme::GOLD));
    set_note(note_);
}

void HelpDot::set_note(const QString &n)
{
    note = n;
    setToolTip(!n.isEmpty() ? u"<div style='width:380px'>%1</div>"_s.arg(n) : QString());
}

void HelpDot::enterEvent(QEnterEvent *e)
{
    if (!note.isEmpty())
        QToolTip::showText(mapToGlobal(rect().bottomLeft()), toolTip(), this);
    QLabel::enterEvent(e);
}

QLabel *label(const QString &text, const QString &obj, bool wrap)
{
    auto *l = new QLabel(text);
    l->setObjectName(obj);
    l->setWordWrap(wrap);
    return l;
}

// ---------------------------------------------------------------------------------------------- Stepper
Stepper::Stepper(long long minimum, long long maximum, long long value, QWidget *parent)
    : QWidget(parent), _min(minimum), _max(maximum)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);
    minus = new QPushButton(u"−"_s);
    plus = new QPushButton(u"＋"_s);
    for (QPushButton *b : {minus, plus}) {
        b->setFixedSize(30, 28);
        b->setStyleSheet(u"QPushButton { padding: 0px; font-size: 15px; font-weight: 600; }"_s);
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(400);
        b->setAutoRepeatInterval(90);
    }
    edit = new QLineEdit();
    edit->setFixedWidth(std::max<int>(40, 12 + 10 * int(QString::number(maximum).size())));   // room for the biggest number
    edit->setAlignment(Qt::AlignCenter);
    connect(minus, &QPushButton::clicked, this, [this] { _step(-1); });
    connect(plus, &QPushButton::clicked, this, [this] { _step(1); });
    connect(edit, &QLineEdit::editingFinished, this, &Stepper::_typed);
    lay->addWidget(minus);
    lay->addWidget(edit);
    lay->addWidget(plus);
    setValue(value);
}

void Stepper::_typed()
{
    try {
        setValue(py::to_int(edit->text()));
    } catch (const py::ValueError &) {
        edit->setText(QString::number(_val));
    }
}

void Stepper::_step(int d)
{
    if (_choices.empty()) {
        setValue(_val + d);
        return;
    }
    if (d > 0) {
        for (long long x : _choices)
            if (x > _val) {
                setValue(x);
                return;
            }
    } else
        for (auto it = _choices.rbegin(); it != _choices.rend(); ++it)
            if (*it < _val) {
                setValue(*it);
                return;
            }
}

void Stepper::setChoices(const std::vector<long long> &values)
{
    // Only these numbers can be chosen: − / ＋ go to the one before / after, a typed number becomes the nearest.
    _choices = values;
    std::sort(_choices.begin(), _choices.end());
    _choices.erase(std::unique(_choices.begin(), _choices.end()), _choices.end());
    if (!_choices.empty()) {
        _min = _choices.front();
        _max = _choices.back();
    }
    setValue(_has_val ? _val : _min);
}

void Stepper::setValue(long long v)
{
    v = std::max(_min, std::min(_max, v));
    if (!_choices.empty()) {
        long long best = _choices.front();
        for (long long x : _choices)
            if (std::make_pair(std::llabs(x - v), x) < std::make_pair(std::llabs(best - v), best))
                best = x;
        v = best;
    }
    const bool changed = !_has_val || v != _val;
    _val = v;
    _has_val = true;
    edit->setText(QString::number(v));
    minus->setEnabled(v > _min);
    plus->setEnabled(v < _max);
    if (changed && !signalsBlocked())
        Q_EMIT valueChanged(int(v));
}

void Stepper::setRange(long long lo, long long hi)
{
    _min = lo;
    _max = hi;
    setValue(_val);
}

// ---------------------------------------------------------------------------------------------- StepSlider
StepSlider::StepSlider(const QStringList &names_, int value, QWidget *parent) : QWidget(parent), names(names_), val(value)
{
    textselect::mark_takes_mouse(this);
    setMinimumHeight(74);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StepSlider::setValue(int v)
{
    val = std::max(0, std::min(int(names.size()) - 1, v));
    update();
}

std::vector<double> StepSlider::_xs() const
{
    const double pad = 34;
    const double w = std::max(1, int(width() - 2 * pad));
    const double n = double(names.size() - 1);
    std::vector<double> out;
    for (qsizetype i = 0; i < names.size(); ++i)
        out.push_back(pad + w * double(i) / n);
    return out;
}

int StepSlider::_pick(double x) const
{
    const auto xs = _xs();
    int best = 0;
    for (size_t i = 1; i < xs.size(); ++i)
        if (std::fabs(xs[i] - x) < std::fabs(xs[size_t(best)] - x))
            best = int(i);
    return best;
}

void StepSlider::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        _drag = true;
        _start = val;                         // what's committed; the release commits the new position
        _set_from(ev->position().x());
    }
}

void StepSlider::mouseMoveEvent(QMouseEvent *ev)
{
    if (_drag)
        _set_from(ev->position().x());
}

void StepSlider::mouseReleaseEvent(QMouseEvent *ev)
{
    if (_drag) {
        _drag = false;
        _set_from(ev->position().x());
        if (val != _start)
            Q_EMIT valueChanged(val);
    }
}

void StepSlider::_set_from(double x)
{
    const int i = _pick(x);
    if (i != val) {
        val = i;
        update();
    }
}

void StepSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto xs = _xs();
    const double y = height() / 2.0;
    const bool on = isEnabled();
    const QColor track(theme::LINE);
    const QColor gold = on ? QColor(theme::GOLD) : QColor(110, 110, 110);
    QPen pen(track, 6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(int(xs.front()), int(y), int(xs.back()), int(y));
    pen = QPen(gold, 6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(int(xs.front()), int(y), int(xs[size_t(val)]), int(y));
    QFont f(font());
    for (size_t i = 0; i < xs.size(); ++i) {
        const double x = xs[i];
        p.setPen(Qt::NoPen);
        p.setBrush(int(i) <= val ? gold : track);
        p.drawEllipse(QRectF(x - 4.5, y - 4.5, 9, 9));
        f.setBold(int(i) == val);
        p.setFont(f);
        p.setPen(int(i) == val ? gold : QColor(theme::INK_DIM));
        const QRectF box(x - 60, i % 2 == 0 ? 0 : y + 12, 120, y - 12);
        drawtext::draw(p, box, Qt::AlignHCenter | (i % 2 == 0 ? Qt::AlignBottom : Qt::AlignTop), names[qsizetype(i)]);
    }
    const double x = xs[size_t(val)];
    p.setPen(QPen(QColor(12, 14, 18), 3));
    p.setBrush(gold);
    p.drawEllipse(QRectF(x - 10, y - 10, 20, 20));
    p.end();
}

// ---------------------------------------------------------------------------------------------- heading delegate
const int HEAD_ROLE = Qt::UserRole + 50;

namespace {
class HeadingDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, index);
        if (index.data(HEAD_ROLE).toBool())
            s.setHeight(s.height() + 12);
        return s;
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        if (!index.data(HEAD_ROLE).toBool()) {
            // a row under a heading: indented (its highlight still spans the whole row)
            if (opt.state & (QStyle::State_Selected | QStyle::State_MouseOver))
                p->fillRect(opt.rect, QColor(theme::GOLD_DIM));
            QStyleOptionViewItem o(opt);
            o.rect = opt.rect.adjusted(18, 0, 0, 0);
            QStyledItemDelegate::paint(p, o, index);
            return;
        }
        const QRect r = opt.rect;
        p->save();
        p->fillRect(r, QColor(u"#0c0e12"_s));
        if (index.row() > 0)                  // the break: a gold line between this group and the one above
            p->fillRect(QRect(r.left(), r.top(), r.width(), 2), QColor(theme::GOLD));
        QFont f(opt.font);
        f.setBold(true);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::PercentageSpacing, 106);
        p->setFont(f);
        p->setPen(QColor(theme::GOLD));
        drawtext::draw(*p, r.adjusted(10, 3, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        p->restore();
    }
};
}  // namespace

QStyledItemDelegate *heading_delegate(QObject *parent) { return new HeadingDelegate(parent); }

}  // namespace widgets
