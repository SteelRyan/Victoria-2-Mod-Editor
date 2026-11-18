// blockshapes.py — the pieces of the block builder (see blockshapes.h).
#include "ui/blockshapes.h"
#include "ui/drawtext.h"

#include <QHash>
#include <QPainter>
#include <QPen>
#include <QWidgetItem>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace blockshapes {

int SLOT_AT(QChar k)
{
    return k == u'c' ? 12 : k == u'p' ? 40 : 68;
}

std::tuple<int, int, int> RGB_OF(const QString &kind)
{
    static const QHash<QString, std::tuple<int, int, int>> T = {
        {u"cond"_s, {86, 150, 222}},  {u"eff"_s, {226, 146, 62}},   {u"logic"_s, {104, 190, 110}},
        {u"scope"_s, {184, 124, 232}}, {u"raw"_s, {132, 138, 150}},  {u"chance"_s, {226, 196, 80}},
        {u"note"_s, {240, 230, 160}},  {u"socket"_s, {150, 124, 74}}, {u"goal"_s, {226, 110, 110}}};
    return T.value(kind, T.value(u"raw"_s));
}

const QString PICKED = u"#ffd84a"_s;

std::vector<int> slots(const QString &scopes)
{
    // The notch places of a piece that fits these scopes ("c", "p", "s", several, or "*": anywhere).
    std::vector<int> out;
    if (scopes.isEmpty() || scopes.contains(u'*')) {
        for (QChar k : u"cps"_s)
            out.push_back(SLOT_AT(k));
        return out;
    }
    for (QChar k : u"cps"_s)
        if (scopes.contains(k))
            out.push_back(SLOT_AT(k));
    if (out.empty())
        out.push_back(SLOT_AT(u'c'));
    return out;
}

std::vector<QPointF> conn(double x, double y, const QString &mode)
{
    // The points of a notch / bump from (x, y) to (x + NW, y), dipping down by NH — its shape says the kind.
    if (mode == u"cond")
        return {{x, y}, {x + NW / 2.0, y + NH}, {x + NW, y}};
    if (mode == u"list" || mode == u"goal")
        return {{x, y}, {x + 2, y + NH * 0.7}, {x + 6, double(y + NH)}, {x + NW - 6, double(y + NH)},
                {x + NW - 2, y + NH * 0.7}, {x + NW, y}};
    return {{x, y}, {x + 4, y + NH}, {x + NW - 4, y + NH}, {x + NW, y}};
}

QPainterPath piece_path(double w, double h, const std::vector<int> &tops_, const QString &top_mode,
                        const std::vector<int> &bottoms_, const QString &bottom_mode, const std::optional<Mouth> &mouth)
{
    // The outline of a piece w wide whose head is h high. tops: notches in its top edge; bottoms: bumps under its
    // bottom edge; mouth: a C piece — the mouth opens under the head (y0) down to the lip (y1), with bumps of its mode
    // at the places (measured from the arm).
    QPainterPath p;
    const double r = R;
    std::vector<int> tops = tops_, bottoms = bottoms_;
    std::stable_sort(tops.begin(), tops.end());
    std::stable_sort(bottoms.begin(), bottoms.end());
    p.moveTo(r, 0);
    for (int x : tops)
        if (x + NW <= w - r)
            for (const QPointF &q : conn(x, 0, top_mode))
                p.lineTo(q);
    p.lineTo(w - r, 0);
    p.quadTo(w, 0, w, r);
    double y2;
    if (!mouth) {
        y2 = h;
        p.lineTo(w, y2 - r);
        p.quadTo(w, y2, w - r, y2);
    } else {
        const double y0 = mouth->y0, y1 = mouth->y1;
        std::vector<int> inner = mouth->inner;
        std::stable_sort(inner.begin(), inner.end());
        p.lineTo(w, y0 - r);
        p.quadTo(w, y0, w - r, y0);
        for (auto it = inner.rbegin(); it != inner.rend(); ++it)
            if (ARM + *it + NW <= w - r) {
                const auto pts = conn(ARM + *it, y0, mouth->mode);
                for (auto q = pts.rbegin(); q != pts.rend(); ++q)
                    p.lineTo(*q);
            }
        p.lineTo(ARM + r, y0);
        p.quadTo(ARM, y0, ARM, y0 + r);
        p.lineTo(ARM, y1 - r);
        p.quadTo(ARM, y1, ARM + r, y1);
        for (int x : inner)
            if (ARM + x + NW <= w - r)
                for (const QPointF &q : conn(ARM + x, y1, mouth->mode))
                    p.lineTo(q);
        y2 = y1 + LIP;
        p.lineTo(w - r, y1);
        p.quadTo(w, y1, w, y1 + r);
        p.lineTo(w, y2 - r);
        p.quadTo(w, y2, w - r, y2);
    }
    for (auto it = bottoms.rbegin(); it != bottoms.rend(); ++it)
        if (*it + NW <= w - r) {
            const auto pts = conn(*it, y2, bottom_mode);
            for (auto q = pts.rbegin(); q != pts.rend(); ++q)
                p.lineTo(*q);
        }
    p.lineTo(r, y2);
    p.quadTo(0, y2, 0, y2 - r);
    p.lineTo(0, r);
    p.quadTo(0, 0, r, 0);
    p.closeSubpath();
    return p;
}

QColor fill_of(const QString &kind)
{
    const auto [r, g, b] = RGB_OF(kind);
    return QColor(int(r * 0.50), int(g * 0.50), int(b * 0.50));
}

QColor edge_of(const QString &kind)
{
    const auto [r, g, b] = RGB_OF(kind);
    return QColor(r, g, b);
}

void paint_piece(QPainter *p, const QPainterPath &path, const QString &kind, bool wrong, bool lit, bool picked)
{
    p->setRenderHint(QPainter::Antialiasing);
    QColor fill = fill_of(kind);
    if (lit)
        fill = fill.lighter(125);
    if (picked)                                              // lit up, towards yellow
        fill = QColor((fill.red() * 2 + 255) / 3, (fill.green() * 2 + 216) / 3, (fill.blue() * 2 + 74) / 3);
    p->setBrush(fill);
    p->setPen(QPen(wrong ? QColor(u"#ff5a4e"_s) : edge_of(kind), wrong ? 3 : 2));
    p->drawPath(path);
    if (picked) {                                            // a thick yellow edge: "this is where I am"
        p->setBrush(Qt::NoBrush);
        QPen pen(QColor(PICKED), 4);
        pen.setJoinStyle(Qt::RoundJoin);
        p->setPen(pen);
        p->drawPath(path);
    }
}

// ---------------------------------------------------------------------------------------------- PieceStack
bool is_piece(const QWidget *w)
{
    return w && w->property("is_piece").toBool();
}

PieceStack::PieceStack(QWidget *parent, const QMargins &margins) : QLayout(parent)
{
    setContentsMargins(margins);
}

PieceStack::~PieceStack()
{
    while (QLayoutItem *it = takeAt(0))
        delete it;
}

void PieceStack::addItem(QLayoutItem *item) { _items.push_back(item); }

void PieceStack::insertWidget(int i, QWidget *w)
{
    addChildWidget(w);
    const int at = std::max(0, std::min(i, int(_items.size())));
    _items.insert(_items.begin() + at, new QWidgetItem(w));
    invalidate();
}

int PieceStack::count() const { return int(_items.size()); }

QLayoutItem *PieceStack::itemAt(int i) const { return 0 <= i && i < int(_items.size()) ? _items[size_t(i)] : nullptr; }

QLayoutItem *PieceStack::takeAt(int i)
{
    if (i < 0 || i >= int(_items.size()))
        return nullptr;
    QLayoutItem *it = _items[size_t(i)];
    _items.erase(_items.begin() + i);
    return it;
}

Qt::Orientations PieceStack::expandingDirections() const { return Qt::Orientations(); }

bool PieceStack::hasHeightForWidth() const { return true; }

int PieceStack::heightForWidth(int w) const { return _lay(QRect(0, 0, w, 0), false); }

void PieceStack::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    _lay(rect, true);
}

QSize PieceStack::sizeHint() const
{
    const QMargins m = contentsMargins();
    int w = MIN_W;
    for (QLayoutItem *it : _items)
        if (is_piece(it->widget()))
            w = std::max(w, it->sizeHint().width());
    w += m.left() + m.right();
    return QSize(w, heightForWidth(w));
}

QSize PieceStack::minimumSize() const
{
    const QMargins m = contentsMargins();
    return QSize(m.left() + m.right() + MIN_W, 0);
}

int PieceStack::_lay(const QRect &rect, bool place) const
{
    const QMargins m = contentsMargins();
    const int x = rect.x() + m.left();
    int y = rect.y() + m.top();
    const int w = std::max(40, rect.width() - m.left() - m.right());
    bool prev = false, first = true;
    for (QLayoutItem *it : _items) {
        const bool piece = is_piece(it->widget());
        const QSize hint = it->sizeHint();
        const int iw = piece ? std::max(std::min(hint.width(), w), std::min(it->minimumSize().width(), w)) : w;
        const int ih = it->hasHeightForWidth() ? it->heightForWidth(iw) : hint.height();
        if (!piece && (prev || first))
            y += NH + 2;                                  // clear of the bump above
        if (place)
            it->setGeometry(QRect(x, y, iw, ih));
        y += ih - (piece ? NH : 0);                       // the next piece sits over this one's bump
        prev = piece;
        first = false;
    }
    if (prev)
        y += NH;
    return y + m.bottom() - rect.y();
}

// ---------------------------------------------------------------------------------------------- Flow
Flow::Flow(const std::vector<std::pair<QString, QString>> &chunks_, const QFont &font_, const QFont &slot_font_)
    : chunks(chunks_), fm(font_), sfm(slot_font_), font(font_), slot_font(slot_font_)
{
}

int Flow::width_of(const QString &kind, const QString &text) const
{
    return kind == u"t" ? fm.horizontalAdvance(text) : sfm.horizontalAdvance(text) + 18;
}

std::pair<int, int> Flow::lay(int maxw, int min_w)
{
    // Places every chunk for a piece at most maxw wide. -> (width, height of the head)
    int x = PAD_L, y = NH + 4;
    places.clear();
    int right = 0;
    const int gap = fm.horizontalAdvance(u" "_s);
    for (const auto &[kind, text] : chunks) {
        const int cw = width_of(kind, text);
        if (x > PAD_L && x + cw > maxw - PAD_R) {
            x = PAD_L;
            y = y + LINE;
        }
        places.push_back({kind, text, x, y, cw});
        x += cw + gap;
        right = std::max(right, x - gap);
    }
    w = std::max(min_w, std::min(maxw, right + PAD_R));
    h = y + LINE + 3;
    return {w, h};
}

void Flow::paint(QPainter *p) const
{
    for (const Place &pl : places) {
        if (pl.kind == u"t") {
            p->setFont(font);
            p->setPen(QColor(u"#ffffff"_s));
            drawtext::draw(*p, QRectF(pl.x, pl.y, pl.cw + 2, LINE), Qt::AlignLeft | Qt::AlignVCenter, pl.text);
        } else {
            const QRectF r(pl.x, pl.y + 1, pl.cw, LINE - 3);
            p->setPen(QPen(QColor(255, 255, 255, 80), 1));
            p->setBrush(QColor(10, 12, 16, 200));
            p->drawRoundedRect(r, 9, 9);
            p->setFont(slot_font);
            p->setPen(QColor(255, 255, 255, 190));
            drawtext::draw(*p, r, Qt::AlignCenter, pl.text);
        }
    }
}

// ---------------------------------------------------------------------------------------------- Legend
Legend::Legend(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(92);
}

void Legend::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(11);
    const QFont small(f);
    f.setBold(true);
    int x = 2;
    for (const auto &[kind, mode, text] : {std::tuple{u"cond"_s, u"cond"_s, u"condition"_s},
                                           std::tuple{u"eff"_s, u"eff"_s, u"effect"_s}}) {
        const int w = 104;
        p.save();
        p.translate(x, 2);
        paint_piece(&p, piece_path(w, 30, slots(u"*"_s), mode, slots(u"*"_s), mode), kind);
        p.setFont(f);
        p.setPen(QColor(u"#ffffff"_s));
        drawtext::draw(p, QRectF(0, NH, w, 30 - NH), Qt::AlignCenter, text);
        p.restore();
        x += w + 14;
    }
    p.setFont(small);
    p.setPen(QColor(200, 204, 212));
    drawtext::draw(p, QRectF(x, 0, std::max(40, width() - x), 40), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
               u"pointed notch: a condition\nflat notch: an effect"_s);
    const int y = 48;
    p.setPen(QColor(200, 204, 212));
    const auto [sr, sg, sb] = RGB_OF(u"socket"_s);
    for (QChar k : u"cps"_s) {
        const double cx = 2 + SLOT_AT(k) + NW / 2.0;
        const int dy = k == u'c' ? 4 : k == u'p' ? 18 : 32;
        p.setPen(QPen(QColor(sr, sg, sb), 2));
        p.drawLine(QPointF(cx, 2 + 30 + NH + 1), QPointF(cx, y + dy));
        p.drawLine(QPointF(cx, y + dy), QPointF(104, y + dy));
    }
    p.setPen(QColor(200, 204, 212));
    int i = 0;
    for (const QString &name : {u"left: about a nation"_s, u"middle: about a province"_s, u"right: about a pop"_s}) {
        drawtext::draw(p, QRectF(110, y - 4 + i * 14, width() - 112, 14), Qt::AlignLeft | Qt::AlignVCenter, name);
        ++i;
    }
    p.end();
}

}  // namespace blockshapes
