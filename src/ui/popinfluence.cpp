// popinfluence.py, the window half — see popinfluence.h.
#include "ui/popinfluence.h"
#include "ui/drawtext.h"
#include "core/blockcode.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "map/iconstrips_data.h"
#include "map/mapview_data.h"
#include "map/regionimport.h"
#include "ui/iconstrips.h"
#include "ui/regionimport.h"
#include "ui/textselect.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace popinfluence {

const QString GREEN = u"#58c26a"_s, RED = u"#e2574c"_s, GREY = u"#8a93a0"_s, AMBER = u"#e8b34a"_s;
static const int CARD_H = 62, CARD_GAP = 12, MID_W = 300;
static const int TAB_W = 66, WIRE = 100;      // a card's number tab; the room for the wires a side (the user,
//                                               2026-10-16: with 46 px the wires could not be followed)
static const int STEP_H = 28;
static QString box_qss()
{
    return u"QPlainTextEdit { background: #1b1f26; color: %1; border: 1px solid %2; border-radius: 4px; "
           "font-family: Consolas, monospace; }"_s.arg(theme::INK, theme::LINE);
}

// ---------------------------------------------------------------------------------------------- small helpers
static double fnum(const QString &s)          // _num(s) or 0
{
    const std::optional<double> v = _num(s);
    return v && *v != 0.0 ? *v : 0.0;
}
static QString pg(double x)                   // "%+g"
{
    const QString s = py::fmt_g(x);
    return s.startsWith(u'-') ? s : u"+"_s + s;
}
static QString pf(double x, int d)            // "%+.Nf"
{
    const QString s = py::fmt_f(x, d);
    return s.startsWith(u'-') ? s : u"+"_s + s;
}
static QString rstrip_c(QString s, QChar c)
{
    while (s.endsWith(c))
        s.chop(1);
    return s;
}
static QString short_net(double v)            // ("%+.3f" % v).rstrip("0").rstrip(".")
{
    return rstrip_c(rstrip_c(pf(v, 3), u'0'), u'.');
}
static QColor rgb_of(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }
static QString commas0(double x) { return py::fmt_commas(x, 0); }
template <typename T> static qsizetype index_of(const std::vector<T> &v, const T &x)
{
    const auto it = std::find(v.begin(), v.end(), x);
    return it == v.end() ? -1 : static_cast<qsizetype>(it - v.begin());
}

// ---------------------------------------------------------------------------------------------- the chart
Chart::Chart(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself, as Python's textselect finds it)
    rate = [](const Card &) { return QString(); };
    warn = [](const Card &) { return QString(); };
    names = [](const QString &c) { return c; };
    key_name = [](const QString &k) { return _pretty(k); };
    fmt = [](const QString &, double v) { return py::fmt_g(v); };
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void Chart::set_data(const QString &t, const QString &s, const QString &b, const std::vector<Card> &m,
                     std::optional<long long> sl)
{
    title = t;
    sub = s;
    base = b;
    mods = m;
    sel = sl;
    _lads = ladders(mods);
    _layout();
    update();
}

std::tuple<std::vector<long long>, std::vector<long long>, std::vector<long long>> Chart::sides() const
{
    std::vector<long long> neg, pos, nil;
    for (size_t i = 0; i < mods.size(); ++i) {
        const std::optional<double> v = _num(mods[i].factor);
        (!v || *v == 0.0 ? nil : *v > 0 ? pos : neg).push_back(static_cast<long long>(i));
    }
    auto key = [&](long long i) { return -std::fabs(fnum(mods[static_cast<size_t>(i)].factor)); };
    auto by = [&](long long a, long long b) { return key(a) < key(b); };
    std::stable_sort(neg.begin(), neg.end(), by);
    std::stable_sort(pos.begin(), pos.end(), by);
    return {neg, pos, nil};
}

QFont Chart::_font(int px, bool bold) const
{
    QFont f(font());
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

int Chart::_th(const QString &text, int px, bool bold) const
{
    // The height a text takes in the middle card (wrapped).
    if (text.isEmpty())
        return 0;
    return QFontMetrics(_font(px, bold))
               .boundingRect(QRect(0, 0, MID_W - 20, 4000), Qt::AlignHCenter | Qt::TextWordWrap, text)
               .height() +
           2;
}

std::optional<std::tuple<QString, QString, QString>> Chart::net_lines() const
{
    // The headline: (the big number, the line under it, a warning or "") — None without a net.
    if (!net)
        return std::nullopt;
    const Net &n = *net;
    if (n.how == u"per")
        return std::make_tuple(QString(), u"each pop that can become it, its own sum:"_s, QString());
    QString big, under;
    if (n.how == u"product") {
        big = u"× "_s + popfacts::num(n.net);
        under = u"the cards that hold, multiplied"_s;
    } else {
        big = std::fabs(n.net) < 1000 ? short_net(n.net)
                                      : (py::round_int(n.net) >= 0 ? u"+"_s : QString()) +
                                            QString::number(py::round_int(n.net));
        if (big == u"+" || big == u"-" || big == u"+0" || big == u"-0")
            big = u"0"_s;
        under = u"raises %1   ·   lowers %2"_s.arg(n.pos != 0.0 ? u"+"_s + popfacts::num(n.pos) : u"0"_s,
                                                    n.neg != 0.0 ? u"−"_s + popfacts::num(-n.neg) : u"0"_s);
    }
    return std::make_tuple(big, under, QString());
}

void Chart::_layout()
{
    const double w = std::max(900, width());
    const auto [neg, pos, nil] = sides();
    const double side_w = (w - MID_W - 2 * WIRE - 24) / 2.0;
    QHash<QString, std::pair<double, double>> ty;   // the middle card: its texts one under the other
    std::vector<std::pair<double, double>> ty_notes;
    double y = 12 + 10;
    const auto hl = net_lines();
    struct RowT {
        QString key, text;
        int px;
        bool bold;
        int gap;
    };
    std::vector<RowT> rows = {{u"title"_s, title, 17, true, 4}};
    if (hl) {
        rows.push_back({u"netbig"_s, std::get<0>(*hl), 30, true, 0});
        rows.push_back({u"netunder"_s, std::get<1>(*hl), 13, true, 2});
        rows.push_back({u"netper"_s, QString(), 0, false, 12});
        rows.push_back({u"netnote"_s, std::get<2>(*hl), 13, false, 8});
    }
    rows.push_back({u"sub"_s, sub, 13, false, 8});
    rows.push_back({u"result"_s, result, 13, true, 10});
    for (const RowT &rw : rows) {
        if (rw.key == u"netper") {                   // (several source pops: a row each)
            const double np = static_cast<double>(net->per.size());
            ty.insert(rw.key, {y, 24 * np});
            y += 24 * np + (net->per.empty() ? 0 : rw.gap);
            continue;
        }
        const int hh = _th(rw.text, rw.px, rw.bold);
        ty.insert(rw.key, {y, hh});
        y += hh + (hh ? rw.gap : 0);
    }
    if (sample) {
        const Sample &s = *sample;
        ty.insert(u"shead"_s, {y, 20});
        y += 24;
        const int hh = _th(s.headline, 15, true);
        ty.insert(u"headline"_s, {y, hh});
        y += hh + (hh ? 8 : 0);
        ty.insert(u"pie"_s, {y, 140});
        y += 152;
        ty.insert(u"rows"_s, {y, 22.0 * static_cast<double>(s.rows.size())});
        y += 22.0 * static_cast<double>(s.rows.size()) + 6;
        for (const auto &[text, _kind] : s.notes) {
            const int nh = _th(text, 13);
            ty_notes.push_back({y, nh});
            y += nh + 3;
        }
    }
    y += 28;
    _ty = ty;
    _ty_notes = ty_notes;
    _mid = QRectF((w - MID_W) / 2.0, 12, MID_W, y - 12);   // (at the top: in view)
    _boxes.clear();
    _lad_boxes.clear();
    _focus_rect.reset();
    _focus_rows.clear();
    QHash<long long, long long> inlad;
    for (size_t n = 0; n < _lads.size(); ++n)
        for (long long i : _lads[n].idx)
            inlad.insert(i, static_cast<long long>(n));
    double h = 24;
    for (int col = 0; col < 2; ++col) {
        const std::vector<long long> &idx = col == 0 ? neg : pos;
        const double x = col == 0 ? 12 : w - 12 - side_w;
        double yy = 12;
        if (col == 1 && focus_card) {                // the national focus: the first card of the raising side
            double hh = CARD_H;
            if (focus_open) {
                for (size_t k = 0; k < focus_card->tiers.size(); ++k)
                    _focus_rows.push_back({QRectF(x + 8, yy + CARD_H + static_cast<double>(k) * STEP_H, side_w - 16,
                                                  STEP_H - 2),
                                           static_cast<long long>(k)});
                hh += static_cast<double>(focus_card->tiers.size()) * STEP_H + 8;
            }
            _focus_rect = QRectF(x, yy, side_w, hh);
            yy += hh + CARD_GAP;
        }
        struct Unit {
            double wt;
            std::optional<long long> n;
            long long i;
        };
        std::vector<Unit> units;                     // what weighs most at the top: a step group by all its steps
        std::set<long long> done;
        for (long long i : idx) {
            const auto it = inlad.constFind(i);
            if (it == inlad.constEnd()) {
                units.push_back({std::fabs(fnum(mods[static_cast<size_t>(i)].factor)), std::nullopt, i});
            } else if (!done.count(*it)) {
                done.insert(*it);
                py::Sum s;
                for (long long k : _lads[static_cast<size_t>(*it)].idx)
                    s.add(std::fabs(fnum(mods[static_cast<size_t>(k)].factor)));
                units.push_back({s.value(), *it, i});
            }
        }
        std::stable_sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) { return -a.wt < -b.wt; });
        for (const Unit &u : units) {
            if (!u.n) {
                _boxes.push_back({QRectF(x, yy, side_w, CARD_H), u.i, col});
                yy += CARD_H + CARD_GAP;
            } else {                                 // a step group: one card; unfolded: a row a step under it
                const Ladder &L = _lads[static_cast<size_t>(*u.n)];
                std::vector<std::pair<QRectF, long long>> rr;
                double hh = CARD_H;
                if (open.count(*u.n)) {
                    for (size_t k = 0; k < L.idx.size(); ++k)
                        rr.push_back({QRectF(x + 8, yy + CARD_H + static_cast<double>(k) * STEP_H, side_w - 16,
                                             STEP_H - 2),
                                      L.idx[k]});
                    hh += static_cast<double>(L.idx.size()) * STEP_H + 8;
                }
                _lad_boxes.push_back({QRectF(x, yy, side_w, hh), *u.n, col, rr});
                yy += hh + CARD_GAP;
            }
        }
        h = std::max(h, yy + 12);
    }
    h = std::max(h, _mid.bottom() + 12);
    y = std::max(h, _mid.bottom() + 30);
    for (size_t k = 0; k < nil.size(); ++k)          // a factor of 0 (or none): does nothing — under the middle
        _boxes.push_back({QRectF(_mid.left(), y + static_cast<double>(k) * (CARD_H + CARD_GAP), MID_W, CARD_H), nil[k], 2});
    if (!nil.empty())
        y += static_cast<double>(nil.size()) * (CARD_H + CARD_GAP);
    setMinimumHeight(static_cast<int>(std::max(h, y) + 12));
}

void Chart::resizeEvent(QResizeEvent *e)
{
    _layout();
    QWidget::resizeEvent(e);
}

QSize Chart::sizeHint() const { return QSize(900, minimumHeight()); }

void Chart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFont f = _font(13), fb = _font(13, true), big = _font(17, true);
    const QRectF mid = _mid;
    double top = 0.0;
    bool any = false;
    for (const Card &m : mods) {
        const double v = std::fabs(fnum(m.factor));
        top = any ? std::max(top, v) : v;
        any = true;
    }
    if (!any)
        top = 1.0;
    if (top == 0.0)
        top = 1.0;
    struct Wire {
        QRectF r;
        int col;
        double v;
        bool sel, on;
    };
    std::vector<Wire> wires;
    for (const Box &b : _boxes)
        wires.push_back({b.r, b.col, std::fabs(fnum(mods[static_cast<size_t>(b.i)].factor)), sel && b.i == *sel,
                         ticked.contains(b.i)});
    for (const LadBox &lb : _lad_boxes) {
        const Ladder &L = _lads[static_cast<size_t>(lb.n)];
        py::Sum s;
        bool live = false;
        for (long long i : L.idx)
            if (ticked.contains(i)) {
                s.add(fnum(mods[static_cast<size_t>(i)].factor));
                live = true;
            }
        wires.push_back({QRectF(lb.r.left(), lb.r.top(), lb.r.width(), CARD_H), lb.col, std::fabs(s.value()), false, live});
    }
    if (_focus_rect) {
        const bool on = focus_card->pick.has_value();
        wires.push_back({QRectF(_focus_rect->left(), _focus_rect->top(), _focus_rect->width(), CARD_H), 3,
                         on ? focus_card->units.value_or(top) : 0.0, false, on});
    }
    // the wires first, under the cards (the user, 2026-10-16: "you cannot really trace the line … to see the
    // impact"): each ends at its own place on the middle card, in the cards' order (no crossings); the width goes
    // straight with the card's number against the strongest card (2 … 16 px); a wire that holds says its share of
    // its side at the card
    double side_tot[2] = {0.0, 0.0};
    for (const Wire &w_ : wires)
        if (w_.on && (w_.col == 0 || w_.col == 1 || w_.col == 3))   // (the national focus counts on the raising side)
            side_tot[w_.col == 3 ? 1 : w_.col] += w_.v;
    QHash<size_t, double> ends;
    for (int side = 0; side < 2; ++side) {
        std::vector<size_t> mine;
        for (size_t k = 0; k < wires.size(); ++k)
            if ((wires[k].col == 0 ? 0 : 1) == side && wires[k].col != 2)
                mine.push_back(k);
        std::stable_sort(mine.begin(), mine.end(),
                         [&](size_t a, size_t b) { return wires[a].r.center().y() < wires[b].r.center().y(); });
        if (mine.empty())
            continue;
        const double y0 = mid.top() + 36;
        const double span = std::max(40.0, std::min(mid.height() - 72, 30.0 * static_cast<double>(mine.size())));
        const double step = mine.size() > 1 ? span / static_cast<double>(std::max<size_t>(1, mine.size() - 1)) : 0;
        for (size_t k = 0; k < mine.size(); ++k)
            ends.insert(mine[k], y0 + (mine.size() > 1 ? static_cast<double>(k) * step : span / 2.0));
    }
    p.setFont(_font(11, true));
    for (size_t k = 0; k < wires.size(); ++k) {
        const Wire &w_ = wires[k];
        if (w_.col == 2)
            continue;
        const double wd = w_.on ? (2.0 + 14.0 * std::min(w_.v, top) / top) : 1.5;
        const QPointF a(w_.col == 0 ? w_.r.right() : w_.r.left(), w_.r.center().y());
        const QPointF b(w_.col == 0 ? mid.left() : mid.right(), ends.value(k, mid.top() + 60));
        QPainterPath path(a);
        const double dx = (b.x() - a.x()) * 0.5;
        path.cubicTo(QPointF(a.x() + dx, a.y()), QPointF(b.x() - dx, b.y()), b);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(10, 10, 10), wd + 3, Qt::SolidLine, Qt::FlatCap));
        p.drawPath(path);
        QColor c(w_.col == 3 ? theme::GOLD : w_.col == 0 ? RED : GREEN);
        c.setAlpha(w_.on ? (w_.sel ? 255 : 215) : 60);
        p.setPen(QPen(c, wd, Qt::SolidLine, Qt::FlatCap));
        p.drawPath(path);
        const double tot = side_tot[w_.col == 3 ? 1 : w_.col];
        if (w_.on && tot > 0 && w_.v > 0) {          // its share of its side, by the card
            const QString t = u"%1 %"_s.arg(py::round_int(100.0 * w_.v / tot));
            const QRectF lr(w_.col == 0 ? a.x() + 4 : a.x() - 44, a.y() - wd / 2.0 - 17, 40, 15);
            p.setPen(QColor(c.red(), c.green(), c.blue()));
            drawtext::draw(p, lr, (w_.col == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, t);
        }
    }
    p.setPen(QPen(sel && *sel == -1 ? QColor(theme::GOLD) : QColor(10, 10, 10), 3));
    p.setBrush(QColor(u"#262b33"_s));
    p.drawRoundedRect(mid, 8, 8);
    auto tx = [&](const QString &key) {
        const auto t = _ty.value(key);
        return QRectF(mid.left() + 10, t.first, MID_W - 20, t.second + 2);
    };
    const int wrap = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    p.setFont(big);
    p.setPen(QColor(theme::GOLD));
    drawtext::draw(p, tx(u"title"_s), wrap, title);
    const auto hl = net_lines();
    if (hl) {                                        // the headline: the sum of what holds, raising and lowering
        const Net &n = *net;
        const double sign = n.net - (n.how == u"product" ? 1.0 : 0.0);
        const QColor col(sign > 1e-9 ? GREEN : sign < -1e-9 ? RED : theme::INK_DIM);
        p.setFont(_font(30, true));
        p.setPen(col);
        drawtext::draw(p, tx(u"netbig"_s), wrap, std::get<0>(*hl));
        p.setFont(fb);
        p.setPen(QColor(theme::INK));
        drawtext::draw(p, tx(u"netunder"_s), wrap, std::get<1>(*hl));
        double yy = _ty.value(u"netper"_s).first;
        for (const auto &[nm, o] : n.per) {          // several source pops: each its own sum
            const double v = o.net;
            const QColor cc(v > 1e-9 ? GREEN : v < -1e-9 ? RED : theme::INK_DIM);
            const QRectF row(mid.left() + 16, yy, MID_W - 32, 22);
            p.setFont(fb);
            p.setPen(QColor(theme::INK));
            drawtext::draw(p, row, Qt::AlignLeft | Qt::AlignVCenter, nm);
            p.setFont(f);
            p.setPen(QColor(theme::INK_DIM));
            drawtext::draw(p, row.adjusted(0, 0, -70, 0), Qt::AlignRight | Qt::AlignVCenter,
                       u"+%1 / −%2"_s.arg(popfacts::num(o.pos), popfacts::num(0.0 - o.neg)));   // (0.0 - neg: Python's
                                                   // neg is the int 0 when nothing lowers — "0", never "-0")
            p.setFont(_font(17, true));
            p.setPen(cc);
            drawtext::draw(p, row, Qt::AlignRight | Qt::AlignVCenter, std::fabs(v) < 1e-9 ? u"0"_s : short_net(v));
            yy += 24;
        }
        if (!std::get<2>(*hl).isEmpty()) {
            p.setFont(f);
            p.setPen(QColor(AMBER));
            drawtext::draw(p, tx(u"netnote"_s), wrap, std::get<2>(*hl));
        }
    }
    p.setFont(f);
    p.setPen(QColor(theme::INK));
    drawtext::draw(p, tx(u"sub"_s), wrap, sub);
    if (!result.isEmpty()) {
        p.setFont(fb);
        p.setPen(QColor(theme::GOLD));
        drawtext::draw(p, tx(u"result"_s), wrap, result);
    }
    if (sample)
        _paint_sample(p, mid, f, fb);
    p.setFont(f);
    p.setPen(QColor(theme::INK_DIM));
    drawtext::draw(p, mid.adjusted(10, 0, -10, -8), Qt::AlignHCenter | Qt::AlignBottom,
               !base.isEmpty() ? u"the block's own factor: × "_s + base : QString());
    const auto [neg, pos, nil] = sides();
    p.setFont(fb);
    {
        struct Side {
            QString text;
            double x;
            Qt::Alignment al;
            QString colr;
        };
        const Side both[2] = {{u"◀ lowers it  (%1)"_s.arg(neg.size()), 12, Qt::AlignLeft, RED},
                              {u"raises it  (%1) ▶"_s.arg(pos.size()), static_cast<double>(width() - 232), Qt::AlignRight,
                               GREEN}};
        for (const Side &sd : both) {
            const bool left = sd.al == Qt::AlignLeft;
            if (!(left ? neg : pos).empty() || (!left && focus_card))
                continue;                            // (the cards say it themselves; the words only when a side is empty)
            p.setPen(QColor(sd.colr));
            drawtext::draw(p, QRectF(sd.x, mid.top() + 60, 220, 20), sd.al | Qt::AlignVCenter,
                       QString(sd.text).replace(u"(0)"_s, u"— nothing"_s));
        }
    }
    const QFontMetrics fm(f);
    for (const Box &bx : _boxes) {
        const QRectF &r = bx.r;
        const Card &m = mods[static_cast<size_t>(bx.i)];
        const QColor colr(bx.col == 0 ? RED : bx.col == 1 ? GREEN : GREY);
        const bool on = sel && bx.i == *sel;
        p.setOpacity(!ticked.contains(bx.i) ? 0.42 : 1.0);
        p.setPen(QPen(on ? QColor(theme::GOLD) : QColor(10, 10, 10), on ? 3 : 2));
        p.setBrush(QColor(u"#1b1f26"_s));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(colr);
        const QRectF tab(r.left() + 3, r.top() + 3, TAB_W, r.height() - 6);
        p.drawRoundedRect(tab, 4, 4);
        p.setFont(big);
        p.setPen(QColor(12, 12, 12));
        const std::optional<double> v = _num(m.factor);
        drawtext::draw(p, tab, Qt::AlignCenter, v ? pg(*v) : (!m.factor.isEmpty() ? m.factor : u"?"_s));
        p.setFont(f);
        p.setPen(QColor(theme::INK));
        QString words = names(m.cond);
        if (words.isEmpty())
            words = u"(no condition: always)"_s;
        const QString odd = warn(m);
        const QString rt = !odd.isEmpty() ? odd : rate(m);
        const QRectF box(tab.right() + 8, r.top() + 4, r.right() - tab.right() - 44, r.height() - (rt.isEmpty() ? 8 : 22));
        if (!rt.isEmpty()) {
            p.setPen(QColor(!odd.isEmpty() ? AMBER : theme::GOLD));
            drawtext::draw(p, QRectF(box.left(), r.bottom() - 20, box.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(rt, Qt::ElideRight, static_cast<int>(box.width())));
            p.setPen(QColor(theme::INK));
        }
        p.save();
        p.setOpacity(1.0);
        _box(p, ticked.contains(bx.i), _tick(r));   // the tick box: does this card hold in the case?
        p.restore();
        if (fm.horizontalAdvance(words) > box.width() * 2.9)
            words = fm.elidedText(words, Qt::ElideRight, static_cast<int>(box.width() * 2.9));
        drawtext::draw(p, box, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, words);
    }
    p.setOpacity(1.0);
    _paint_ladders(p, f, fb, big, fm);
    if (_focus_rect)
        _paint_focus(p, f, fb, big, fm);
}

void Chart::_box(QPainter &p, bool on, const QRectF &rect)
{
    // A tick box (gold when ticked).
    p.setPen(QPen(QColor(theme::GOLD), 2));
    p.setBrush(on ? QColor(theme::GOLD) : QColor(u"#14171c"_s));
    p.drawRoundedRect(rect, 4, 4);
    if (on) {
        p.setPen(QPen(QColor(12, 12, 12), 3));
        p.drawLine(QPointF(rect.left() + 5, rect.center().y()), QPointF(rect.center().x() - 1, rect.bottom() - 6));
        p.drawLine(QPointF(rect.center().x() - 1, rect.bottom() - 6), QPointF(rect.right() - 4, rect.top() + 5));
    }
}

void Chart::_paint_focus(QPainter &p, const QFont &f, const QFont &fb, const QFont &big, const QFontMetrics &fm)
{
    // The national focus on the picked pop: one gold card, its tiers unfold — one of them (or none) is set.
    const QRectF r = *_focus_rect;
    const FocusCard &fc = *focus_card;
    const auto &tiers = fc.tiers;
    const std::optional<long long> pick = fc.pick;
    const QRectF head(r.left(), r.top(), r.width(), CARD_H);
    p.setOpacity(1.0);
    p.setPen(QPen(QColor(theme::GOLD), pick ? 3 : 2));
    p.setBrush(QColor(u"#1b1f26"_s));
    p.drawRoundedRect(r, 6, 6);
    const QRectF tab(head.left() + 3, head.top() + 3, TAB_W, CARD_H - 6);
    p.setPen(Qt::NoPen);
    QColor c(theme::GOLD);
    c.setAlpha(pick ? 255 : 90);
    p.setBrush(c);
    p.drawRoundedRect(tab, 4, 4);
    p.setFont(big);
    p.setPen(QColor(12, 12, 12));
    drawtext::draw(p, tab, Qt::AlignCenter, pick ? pg(tiers[static_cast<size_t>(*pick)].value) : u"off"_s);
    p.setFont(fb);
    p.setPen(QColor(theme::INK));
    const QRectF tx(tab.right() + 8, head.top() + 5, head.right() - tab.right() - 16, 20);
    drawtext::draw(p, tx, Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(u"%1 National focus: %2"_s.arg(focus_open ? u"▾"_s : u"▸"_s, fc.name), Qt::ElideRight,
                             static_cast<int>(tx.width())));
    p.setFont(f);
    p.setPen(pick ? QColor(theme::GOLD) : QColor(theme::INK_DIM));
    const QString said = pick ? py::strip(tiers[static_cast<size_t>(*pick)].name)
                              : u"none set — click, pick one of %1"_s.arg(tiers.size());
    drawtext::draw(p, QRectF(tx.left(), tx.bottom() + 1, tx.width(), 17), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(said, Qt::ElideRight, static_cast<int>(tx.width())));
    if (pick) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, QRectF(tx.left(), tx.bottom() + 17, tx.width(), 16), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(u"all the promoted become "_s + fc.name, Qt::ElideRight, static_cast<int>(tx.width())));
    }
    for (const auto &[rr, k] : _focus_rows) {
        const bool on = pick && k == *pick;
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(u"#2a3038"_s) : QColor(u"#20242b"_s));
        p.drawRoundedRect(rr, 4, 4);
        p.setPen(QPen(QColor(theme::GOLD), 2));
        p.setBrush(on ? QColor(theme::GOLD) : QColor(u"#14171c"_s));
        p.drawEllipse(QPointF(rr.left() + 14, rr.center().y()), 6, 6);
        p.setFont(on ? fb : f);
        p.setPen(QColor(theme::INK));
        drawtext::draw(p, QRectF(rr.left() + 28, rr.top(), rr.width() - 96, rr.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(tiers[static_cast<size_t>(k)].name, Qt::ElideRight, static_cast<int>(rr.width() - 96)));
        drawtext::draw(p, QRectF(rr.right() - 64, rr.top(), 56, rr.height()), Qt::AlignVCenter | Qt::AlignRight,
                   pg(tiers[static_cast<size_t>(k)].value));
    }
}

std::pair<std::vector<long long>, std::optional<long long>> Chart::lad_state(long long n) const
{
    // (the cards of step group n that hold, the step the case stands on — its number in the group — or None)
    const Ladder &L = _lads[static_cast<size_t>(n)];
    std::vector<long long> live;
    for (long long i : L.idx)
        if (ticked.contains(i))
            live.push_back(i);
    if (live.empty() || L.kind == u"group")
        return {live, std::nullopt};
    return {live, index_of(L.idx, L.neg ? live.front() : live.back())};
}

QString Chart::lad_title(const Ladder &L) const
{
    // A folding card's name: its scale — or, for a group of the file that is no scale, its first card.
    if (L.kind == u"group") {
        QString first = names(mods[static_cast<size_t>(L.idx[0])].cond);
        if (first.isEmpty())
            first = u"always"_s;
        return u"Group of %1: %2 …"_s.arg(L.idx.size()).arg(first);
    }
    QString name = !L.key.isEmpty() ? key_name(L.key) : u"steps"_s;
    if (L.tmpl && !L.tmpl->isEmpty()) {
        const QString t = QString(*L.tmpl).replace(u'#', u'N');
        const QString words = names(t);
        if (!words.isEmpty() && words != t)
            name = QString(words).replace(u'N', u"…"_s);
    }
    return (L.grouped ? u"Group: "_s : QString()) + (L.neg && !(L.tmpl && !L.tmpl->isEmpty()) ? u"below: "_s : QString()) +
           name;
}

QString Chart::lad_row(const Ladder &L, long long k, long long ci) const
{
    // A row of an unfolded card: the step's number, or the card's own conditions.
    if (L.kind == u"group" || (L.tmpl && !L.tmpl->isEmpty())) {
        const QString w = names(mods[static_cast<size_t>(ci)].cond);
        return w.isEmpty() ? u"always"_s : w;
    }
    return u"%1 %2"_s.arg(L.neg ? u"below"_s : u"at least"_s, fmt(L.key, (*L.thr)[static_cast<size_t>(k)]));
}

void Chart::_paint_ladders(QPainter &p, const QFont &f, const QFont &fb, const QFont &big, const QFontMetrics &fm)
{
    for (const LadBox &lb : _lad_boxes) {
        const QRectF &r = lb.r;
        const Ladder &L = _lads[static_cast<size_t>(lb.n)];
        const auto [live, pick] = lad_state(lb.n);
        py::Sum ts;
        for (long long i : live)
            ts.add(fnum(mods[static_cast<size_t>(i)].factor));
        const double total = ts.value();
        const bool tmpl = L.tmpl && !L.tmpl->isEmpty();
        const QRectF head(r.left(), r.top(), r.width(), CARD_H);
        p.setOpacity(!live.empty() ? 1.0 : 0.5);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(u"#1b1f26"_s));
        p.drawRoundedRect(r, 6, 6);
        const QRectF tab(head.left() + 3, head.top() + 3, TAB_W, CARD_H - 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(lb.col == 0 ? RED : lb.col == 1 ? GREEN : GREY));
        p.drawRoundedRect(tab, 4, 4);
        p.setFont(big);
        p.setPen(QColor(12, 12, 12));
        drawtext::draw(p, tab, Qt::AlignCenter, !live.empty() ? pg(py::round_to(total, 4)) : u"0"_s);
        p.setOpacity(1.0);
        p.setFont(fb);
        p.setPen(QColor(theme::INK));
        const QRectF tx(tab.right() + 8, head.top() + 5, head.right() - tab.right() - 16, 20);
        drawtext::draw(p, tx, Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(u"%1 %2"_s.arg(open.count(lb.n) ? u"▾"_s : u"▸"_s, lad_title(L)), Qt::ElideRight,
                                 static_cast<int>(tx.width())));
        p.setFont(f);
        p.setPen(!live.empty() ? QColor(theme::GOLD) : QColor(theme::INK_DIM));
        QString said;
        const qsizetype nl = static_cast<qsizetype>(live.size()), ni = static_cast<qsizetype>(L.idx.size());
        if (L.kind == u"group")
            said = u"%1 of %2 cards hold — click to see them"_s.arg(nl).arg(ni);
        else if (tmpl)
            said = !live.empty() ? u"%1 of %2 steps hold"_s.arg(nl).arg(ni)
                                 : u"none of %1 steps (%2 … %3)"_s.arg(ni).arg(py::fmt_g(L.thr->front()),
                                                                             py::fmt_g(L.thr->back()));
        else if (pick)
            said = u"%1 %2: %3 of %4 steps"_s.arg(L.neg ? u"below"_s : u"at least"_s,
                                                 fmt(L.key, (*L.thr)[static_cast<size_t>(*pick)]))
                       .arg(nl)
                       .arg(ni);
        else
            said = u"none of %1 steps (%2 … %3)"_s.arg(ni).arg(fmt(L.key, L.thr->front()), fmt(L.key, L.thr->back()));
        drawtext::draw(p, QRectF(tx.left(), tx.bottom() + 1, tx.width(), 17), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(said, Qt::ElideRight, static_cast<int>(tx.width())));
        if (!L.rest.isEmpty()) {
            p.setPen(QColor(theme::INK_DIM));
            drawtext::draw(p, QRectF(tx.left(), tx.bottom() + 17, tx.width(), 16), Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(u"only while: "_s + names(L.rest), Qt::ElideRight, static_cast<int>(tx.width())));
        } else if (!live.empty()) {
            Card c;
            c.factor = py::repr_float(total);
            const QString rt = rate(c);
            if (!rt.isEmpty()) {
                p.setPen(QColor(theme::GOLD));
                drawtext::draw(p, QRectF(tx.left(), tx.bottom() + 17, tx.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(rt, Qt::ElideRight, static_cast<int>(tx.width())));
            }
        }
        for (size_t k = 0; k < lb.rows.size(); ++k) {   // unfolded: a row a step
            const auto &[rr, ci] = lb.rows[k];
            const bool on = ticked.contains(ci);
            const bool here = pick && static_cast<long long>(k) == *pick;
            p.setPen(Qt::NoPen);
            p.setBrush(here ? QColor(u"#2a3038"_s) : QColor(u"#20242b"_s));
            p.drawRoundedRect(rr, 4, 4);
            p.setPen(QPen(QColor(theme::GOLD), 2));
            p.setBrush(on ? QColor(theme::GOLD) : QColor(u"#14171c"_s));
            p.drawEllipse(QPointF(rr.left() + 14, rr.center().y()), 6, 6);
            p.setFont(here ? fb : f);
            p.setPen(on ? QColor(theme::INK) : QColor(theme::INK_DIM));
            drawtext::draw(p, QRectF(rr.left() + 28, rr.top(), rr.width() - 120, rr.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(lad_row(L, static_cast<long long>(k), ci), Qt::ElideRight,
                                     static_cast<int>(rr.width() - 120)));
            drawtext::draw(p, QRectF(rr.right() - 92, rr.top(), 56, rr.height()), Qt::AlignVCenter | Qt::AlignRight,
                       pg(fnum(mods[static_cast<size_t>(ci)].factor)));
            p.setPen(sel && ci == *sel ? QColor(theme::GOLD) : QColor(theme::INK_FAINT));
            drawtext::draw(p, QRectF(rr.right() - 30, rr.top(), 26, rr.height()), Qt::AlignCenter, u"✎"_s);
        }
    }
}

void Chart::_paint_sample(QPainter &p, const QRectF &mid, const QFont &f, const QFont &fb)
{
    // The sample province as the game's pop tab shows one: what becomes of the picked pop in big letters, a pie
    // (what the province is after the time), the list under it — before → after, the change in colour.
    const Sample &s = *sample;
    const auto &rows = s.rows;
    const int wrap = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
    p.setFont(fb);
    p.setPen(QColor(theme::INK));
    drawtext::draw(p, QRectF(mid.left() + 8, _ty.value(u"shead"_s).first, mid.width() - 16, 20), Qt::AlignCenter, s.head);
    if (!s.headline.isEmpty()) {
        p.setFont(_font(15, true));
        p.setPen(QColor(theme::GOLD));
        const auto hl = _ty.value(u"headline"_s);
        drawtext::draw(p, QRectF(mid.left() + 10, hl.first, MID_W - 20, hl.second + 2), wrap, s.headline);
    }
    const QRectF pie(mid.center().x() - 70, _ty.value(u"pie"_s).first, 140, 140);
    py::Sum ts;
    for (const SampleRow &r : rows)
        ts.add(std::max(0.0, r.after));
    const double tot = ts.value() != 0.0 ? ts.value() : 1.0;
    int a0 = 90 * 16;
    p.setPen(QPen(QColor(10, 10, 10), 2));
    for (const SampleRow &r : rows) {
        const int span = -static_cast<int>(py::round_int(std::max(0.0, r.after) / tot * 360 * 16));
        if (span) {
            p.setBrush(rgb_of(r.rgb));
            p.drawPie(pie, a0, span);
            a0 += span;
        }
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(10, 10, 10), 3));
    p.drawEllipse(pie);
    double y = _ty.value(u"rows"_s).first;
    py::Sum bs;
    for (const SampleRow &r : rows)
        bs.add(r.before);
    const double before_tot = bs.value() != 0.0 ? bs.value() : 1.0;
    for (const SampleRow &r : rows) {
        const bool mark = s.mark && r.name == *s.mark;
        if (mark) {                                  // the picked pop: its row framed
            p.setPen(QPen(QColor(theme::GOLD), 2));
            p.setBrush(QColor(u"#30353d"_s));
            p.drawRoundedRect(QRectF(mid.left() + 6, y - 1, mid.width() - 12, 22), 4, 4);
        }
        p.setPen(QPen(QColor(10, 10, 10), 1));
        p.setBrush(rgb_of(r.rgb));
        p.drawRoundedRect(QRectF(mid.left() + 12, y + 3, 14, 14), 3, 3);
        p.setFont(mark ? fb : f);
        p.setPen(QColor(theme::INK));
        drawtext::draw(p, QRectF(mid.left() + 32, y, 120, 20), Qt::AlignVCenter | Qt::AlignLeft, r.name);
        const double b = r.before * 100.0 / before_tot, a = r.after * 100.0 / before_tot;
        p.setFont(f);
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, QRectF(mid.left() + 150, y, 44, 20), Qt::AlignVCenter | Qt::AlignRight, py::fmt_f(b, 0) + u"%"_s);
        const double d = a - b;
        p.setFont(fb);
        p.setPen(QColor(d > 0.05 ? GREEN : d < -0.05 ? RED : theme::INK_DIM));
        drawtext::draw(p, QRectF(mid.left() + 196, y, mid.width() - 208, 20), Qt::AlignVCenter | Qt::AlignRight,
                   std::fabs(d) > 0.05 ? u"→ %1%  (%2)"_s.arg(py::fmt_f(a, 1), pf(d, 1)) : u"→ %1%"_s.arg(py::fmt_f(a, 1)));
        y += 22;
    }
    p.setFont(f);
    for (size_t k = 0; k < s.notes.size() && k < _ty_notes.size(); ++k) {
        const auto &[text, kind] = s.notes[k];
        const auto &[yy, hh] = _ty_notes[k];
        p.setPen(QColor(kind == u"warn" ? AMBER : theme::INK_DIM));
        drawtext::draw(p, QRectF(mid.left() + 10, yy, MID_W - 20, hh + 2), wrap, text);
    }
}

QRectF Chart::_tick(const QRectF &r) const { return QRectF(r.right() - 32, r.center().y() - 12, 24, 24); }

std::optional<long long> Chart::_at(const QPointF &pos) const
{
    for (const Box &b : _boxes)
        if (b.r.contains(pos))
            return b.i;
    if (_mid.contains(pos))
        return -1;
    return std::nullopt;
}

QString Chart::tip_at(const QPointF &pos) const
{
    // What the card under the mouse says in full (the cards are narrow).
    if (net && _ty.contains(u"netbig"_s)) {          // the headline: what holds the number down
        const Net &n = *net;
        const double tp = _ty.value(u"netbig"_s).first;
        const auto np = _ty.value(u"netper"_s);
        const double bottom = np.first + np.second;
        if (_mid.contains(pos) && tp <= pos.y() && pos.y() <= bottom) {
            if (n.how == u"per") {
                const long long k = py::trunc_int(py::floordiv(pos.y() - np.first, 24.0));
                if (0 <= k && k < static_cast<long long>(n.per.size())) {
                    const auto &[nm, o] = n.per[static_cast<size_t>(k)];
                    return u"%1: raises +%2, lowers −%3 → %4.%5"_s.arg(nm, popfacts::num(o.pos), popfacts::num(0.0 - o.neg),
                                                                       popfacts::num(o.net),
                                                                       !o.note.isEmpty() ? u"\n\n"_s + o.note : QString());
                }
            } else if (!n.note.isEmpty()) {
                return n.note;
            }
        }
    }
    if (_focus_rect && _focus_rect->contains(pos)) {
        const FocusCard &fc = *focus_card;
        const QString pts =
            (fc.units && *fc.units != 0.0 && fc.pick)
                ? u"\n\nIn the split of the raising side it counts as %1 card points: measured, a focus of value V "
                  "raised the promotion number by about 0.9 × V, and the cards reach that number through the block's "
                  "own factor."_s.arg(popfacts::num(*fc.units))
                : QString();
        QStringList ts;
        for (const FocusTier &t : fc.tiers)
            ts << u"%1  %2"_s.arg(py::strip(t.name), pg(t.value));
        return u"National focus on %1, set on the sample province's state. Measured in the game: all the state's "
               "promoted become %2 (unless its weight stays below 0) and a few more are promoted.%3\n\n%4"_s.arg(
                   fc.name, fc.name, pts, ts.join(u'\n'));
    }
    for (const LadBox &lb : _lad_boxes) {
        if (!lb.r.contains(pos))
            continue;
        const Ladder &L = _lads[static_cast<size_t>(lb.n)];
        std::set<double> each;
        for (long long i : L.idx)
            each.insert(fnum(mods[static_cast<size_t>(i)].factor));
        if (L.kind == u"group" || (L.tmpl && !L.tmpl->isEmpty())) {
            QStringList ls;
            for (long long i : L.idx)
                ls << u"%1   %2"_s.arg(pg(fnum(mods[static_cast<size_t>(i)].factor)), names(mods[static_cast<size_t>(i)].cond));
            return u"%1 — a group = { … } of the file%2; the cards that hold are added up like loose ones "
                   "(measured).\n\n%3\n\nClick: unfold it; a row switches its card, ✎ changes it."_s.arg(
                       lad_title(L), L.grouped ? QString() : u" (steps of one number)"_s, ls.join(u'\n'));
        }
        QStringList th, ea;
        for (double v : *L.thr)
            th << fmt(L.key, v);
        for (double v : each)
            ea << pg(v);
        return u"%1%2 — %3 steps: %4\neach step that holds adds %5%6\n\nClick: unfold the steps, pick the one the case "
               "stands on."_s.arg(L.neg ? u"below: "_s : QString(), key_name(L.key))
            .arg(L.idx.size())
            .arg(th.join(u", "_s), ea.join(u" / "_s), !L.rest.isEmpty() ? u"\nonly while: "_s + names(L.rest) : QString());
    }
    for (const Box &b : _boxes) {
        if (!b.r.contains(pos))
            continue;
        const Card &m = mods[static_cast<size_t>(b.i)];
        const QString odd = warn(m);
        QString w = names(m.cond);
        if (w.isEmpty())
            w = u"(no condition: always)"_s;
        return u"%1   factor %2\n\nIn this case it %3.%4"_s.arg(w, m.factor,
                                                               ticked.contains(b.i) ? u"HOLDS"_s : u"does not hold"_s,
                                                               !odd.isEmpty() ? u"\n\n"_s + odd : QString());
    }
    return QString();
}

bool Chart::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QString tip = tip_at(QPointF(he->pos()));
        if (!tip.isEmpty())
            QToolTip::showText(he->globalPos(), tip, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void Chart::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    bool over = _at(pos).has_value();
    for (const LadBox &lb : _lad_boxes)
        over = over || lb.r.contains(pos);
    over = over || (_focus_rect && _focus_rect->contains(pos));
    setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void Chart::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    if (_focus_rect && _focus_rect->contains(pos)) {
        const FocusCard &fc = *focus_card;
        for (const auto &[rr, k] : _focus_rows)
            if (rr.contains(pos)) {
                focus_open = false;                  // picked: it folds back
                Q_EMIT focusPicked(fc.pick && *fc.pick == k ? -1 : k);
                return;
            }
        if (fc.tiers.size() == 1) {                  // one tier only: the card is the switch
            Q_EMIT focusPicked(fc.pick && *fc.pick == 0 ? -1 : 0);
            return;
        }
        focus_open = !focus_open;
        _layout();
        update();
        return;
    }
    for (const LadBox &lb : _lad_boxes) {
        if (!lb.r.contains(pos))
            continue;
        const Ladder &L = _lads[static_cast<size_t>(lb.n)];
        for (size_t k = 0; k < lb.rows.size(); ++k) {
            const auto &[rr, ci] = lb.rows[k];
            if (!rr.contains(pos))
                continue;
            if (pos.x() > rr.right() - 32) {         // ✎: this step's own card, to change it
                Q_EMIT picked(ci);
                return;
            }
            if (L.kind == u"group") {                // a group that is no scale: each card switched by itself
                Q_EMIT toggled(ci);
                return;
            }
            const auto [_live, pick] = lad_state(lb.n);
            const long long n = lb.n;
            const std::vector<long long> idx = L.idx;
            const std::vector<long long> hold =
                pick && *pick == static_cast<long long>(k) ? std::vector<long long>()
                                                          : ladder_ticks(L, static_cast<long long>(k));
            open.erase(n);                           // picked: it folds back
            Q_EMIT ladderPicked(idx, hold);
            return;
        }
        if (open.count(lb.n))
            open.erase(lb.n);
        else
            open.insert(lb.n);
        _layout();
        update();
        return;
    }
    const std::optional<long long> i = _at(pos);
    if (!i)
        return;
    if (*i >= 0) {
        for (const Box &b : _boxes)
            if (b.i == *i) {
                if (_tick(b.r).adjusted(-6, -6, 6, 6).contains(pos)) {
                    Q_EMIT toggled(*i);
                    return;
                }
                break;
            }
    }
    Q_EMIT picked(*i);
}

// ---------------------------------------------------------------------------------------------- the case
static const QHash<QString, QString> KEY_WORDS = {{u"nationalvalue"_s, u"national value"_s},
                                                  {u"strata"_s, u"class"_s},
                                                  {u"is_state_religion"_s, u"state religion"_s},
                                                  {u"is_primary_culture"_s, u"primary culture"_s},
                                                  {u"is_accepted_culture"_s, u"accepted culture"_s}};
static const QRegularExpression SETFLAG(uR"(\bset_(?:country|global|province)_flag\s*=\s*"?([A-Za-z0-9_]+))"_s);

static int mult_of(const QString &kind) { return kind == u"pct" ? 100 : kind == u"ten" ? 10 : 1; }

CasePanel::CasePanel(popcase::Case &c, std::function<QString(const QString &)> w,
                     std::function<QString(const QString &)> n, QWidget *parent)
    : QWidget(parent), cs(c), words(std::move(w)), name(std::move(n))
{
    fact_note = [](const QString &) { return QString(); };   // what is odd about a fact (a flag nothing sets)
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto *row = new QHBoxLayout();
    nv_lbl = widgets::label(u"National value of the nation"_s, u"sub"_s, false);
    row->addWidget(nv_lbl);
    nv = new QComboBox();
    nv->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(nv, &QComboBox::activated, this, &CasePanel::_nv_picked);
    row->addWidget(nv, 1);
    lay->addLayout(row);
    row = new QHBoxLayout();
    focus_lbl = widgets::label(u"National focus"_s, u"sub"_s, false);
    row->addWidget(focus_lbl);
    focus = new QComboBox();                         // only the focuses that encourage the picked pop
    focus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    focus->setToolTip(u"A national focus set on the sample province's state — only the ones that encourage the picked "
                      "pop are listed. Measured in the game: all the state's promoted become that pop (unless its "
                      "weight stays below 0), and a few more are promoted."_s);
    connect(focus, &QComboBox::activated, this, [this](int) {
        const QVariant d = focus->currentData();
        std::optional<Focus> f;
        if (d.isValid() && d.toLongLong() >= 0 && d.toLongLong() < static_cast<long long>(_tiers.size()))
            f = _tiers[static_cast<size_t>(d.toLongLong())];
        Q_EMIT focusPicked(f);
    });
    row->addWidget(focus, 1);
    lay->addLayout(row);
    grid_host = new QWidget();
    grid = new QGridLayout(grid_host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(1, 1);
    lay->addWidget(grid_host);
    more_btn = new QPushButton(u"Show every setting"_s);
    more_btn->setCheckable(true);
    more_btn->setToolTip(u"Also the settings no card in view asks for (they count for the other pops of the sample "
                         "province)"_s);
    connect(more_btn, &QPushButton::clicked, this, [this] { Q_EMIT changed(); });
    lay->addWidget(more_btn, 0, Qt::AlignLeft);
    facts_lbl = widgets::label(u"Facts of the case — click to switch on or off:"_s, u"sub"_s);
    lay->addWidget(facts_lbl);
    facts_host = new QWidget();
    facts_row = new widgets::WrapRow(facts_host, 5);
    lay->addWidget(facts_host);
    reset_btn = new QPushButton(u"Reset the case"_s);
    reset_btn->setToolTip(u"Every setting back to its start: an ordinary province of the nation's own people in peace"_s);
    connect(reset_btn, &QPushButton::clicked, this, &CasePanel::_reset);
    lay->addWidget(reset_btn, 0, Qt::AlignLeft);
}

void CasePanel::_reset()
{
    cs.reset();
    Q_EMIT changed();
}

void CasePanel::_nv_picked(int)
{
    const QVariant k = nv->currentData();
    QSet<QString> kept;
    for (const QString &f : std::as_const(cs.facts))
        if (!f.startsWith(u"nationalvalue = "))
            kept.insert(f);
    cs.facts = kept;
    if (k.isValid() && !k.toString().isEmpty())
        cs.set_fact(u"nationalvalue = "_s + k.toString(), true);
    Q_EMIT changed();
}

void CasePanel::_slid(const QString &key, int pos)
{
    const QString kind = popcase::VARS.get(key)->kind;
    cs.vals[key] = kind == u"pct" ? pos / 100.0 : kind == u"ten" ? pos / 10.0 : static_cast<double>(pos);
    sliders.get(key)->second->setText(popcase::show(key, cs.vals.value(key)));
    Q_EMIT changed();
}

void CasePanel::show_focuses(const std::vector<Focus> &tiers, const std::optional<Focus> &picked,
                             const QString &pop_name, bool every)
{
    // tiers: the focuses that encourage the picked pop [(key, pop type, value)]; picked: the one set.
    focus_lbl->setText(!pop_name.isEmpty() && !every ? u"National focus on "_s + pop_name : u"National focus"_s);
    focus->blockSignals(true);
    focus->clear();
    _tiers = tiers;
    focus->addItem(!tiers.empty() ? u"(none set)"_s
                                  : u"(none encourages %1)"_s.arg(!pop_name.isEmpty() ? pop_name : u"this pop"_s),
                   QVariant());
    for (size_t k = 0; k < tiers.size(); ++k) {
        const Focus &f = tiers[k];
        focus->addItem(u"%1  (%2%3)"_s.arg(py::strip(name(f.key)), every ? name(f.pop) + u" "_s : QString(), pg(f.value)),
                       QVariant::fromValue(static_cast<long long>(k)));
    }
    const qsizetype at = picked ? index_of(tiers, *picked) : -1;
    focus->setCurrentIndex(at < 0 ? 0 : static_cast<int>(1 + at));
    focus->setEnabled(!tiers.empty());
    focus->blockSignals(false);
}

void CasePanel::show_for(const QSet<QString> &used0, const QSet<QString> &also, const QSet<QString> &facts0,
                         const QStringList &nvs)
{
    // used: the settings the cards in view ask for (shown first, bold); also: the ones that say how many are promoted
    // at all; facts: the cards' yes / no facts; nvs: the mod's national values.
    const bool every = more_btn->isChecked();
    QStringList rest, used;
    for (const auto &[k, _v] : popcase::VARS) {
        if (!used0.contains(k) && k != u"total_pops" && (every || also.contains(k)))
            rest << k;
        if (used0.contains(k) && k != u"total_pops")
            used << k;
    }
    QStringList facts;
    for (const QString &f : facts0)
        if (!f.startsWith(u"nationalvalue = "))
            facts << f;
    std::sort(facts.begin(), facts.end());
    const auto sig = std::make_tuple(used, rest, facts, nvs);
    if (!_sig || *_sig != sig) {
        _sig = sig;
        while (grid->count()) {
            QLayoutItem *it = grid->takeAt(0);
            if (QWidget *w = it->widget()) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
            delete it;
        }
        sliders = decltype(sliders)();
        int r = 0;
        for (const QString &key : used + rest) {
            const popcase::Var &v = *popcase::VARS.get(key);
            auto *lb = new QLabel(v.name);
            if (used.contains(key)) {
                QFont f(lb->font());
                f.setBold(true);
                lb->setFont(f);
            } else {
                lb->setStyleSheet(u"color: %1;"_s.arg(theme::INK_DIM));
            }
            grid->addWidget(lb, r, 0);
            auto *sl = new QSlider(Qt::Horizontal);
            sl->setRange(static_cast<int>(v.lo * mult_of(v.kind)), static_cast<int>(v.hi * mult_of(v.kind)));
            sl->setPageStep(10);
            sl->setMinimumWidth(110);
            connect(sl, &QSlider::valueChanged, this, [this, key](int pos) { _slid(key, pos); });
            grid->addWidget(sl, r, 1);
            auto *val = new QLabel(QString());
            val->setFixedWidth(58);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(val, r, 2);
            sliders.insert(key, {sl, val});
            ++r;
        }
        for (const auto &[_t, b] : fact_btn) {
            b->hide();
            b->setParent(nullptr);
            b->deleteLater();
        }
        fact_btn = decltype(fact_btn)();
        for (const QString &ftext : facts) {
            const QString said = words(ftext);
            const QString odd = fact_note(ftext);
            auto *b = new QPushButton((!odd.isEmpty() ? u"⚠ "_s : QString()) +
                                      (said.size() <= 44 ? said : said.left(42) + u"…"_s));
            b->setObjectName(u"chip"_s);
            b->setCheckable(true);
            b->setToolTip(ftext + (!odd.isEmpty() ? u"\n\n"_s + odd : QString()));
            if (!odd.isEmpty())
                b->setStyleSheet(u"QPushButton:!checked { color: %1; }"_s.arg(AMBER));
            connect(b, &QPushButton::clicked, this, [this, ftext](bool on) {
                cs.set_fact(ftext, on);
                Q_EMIT changed();
            });
            facts_row->addWidget(b);
            fact_btn.insert(ftext, b);
        }
        nv->blockSignals(true);
        nv->clear();
        nv->addItem(u"(none set)"_s, QVariant());
        for (const QString &k : nvs)
            nv->addItem(name(k), k);
        nv->blockSignals(false);
    }
    for (const auto &[key, sv] : sliders) {
        const QString kind = popcase::VARS.get(key)->kind;
        const double v = cs.vals.value(key, 0.0);
        sv.first->blockSignals(true);
        sv.first->setValue(static_cast<int>(py::round_int(v * mult_of(kind))));
        sv.first->blockSignals(false);
        sv.second->setText(popcase::show(key, v));
    }
    for (const auto &[ftext, b] : fact_btn)
        b->setChecked(cs.facts.contains(ftext));
    // (a set walked: which "nationalvalue = " fact is the current one — the case holds one at most)
    QVariant cur;
    for (const QString &f : std::as_const(cs.facts))
        if (f.startsWith(u"nationalvalue = ")) {
            cur = py::split(f, u" = "_s, 1)[1];
            break;
        }
    nv->blockSignals(true);
    nv->setCurrentIndex(std::max(0, cur.isValid() ? nv->findData(cur) : nv->findData(QVariant())));
    nv->blockSignals(false);
    nv->setVisible(!nvs.isEmpty());
    nv_lbl->setVisible(!nvs.isEmpty());
    facts_lbl->setVisible(!fact_btn.isEmpty());
    facts_host->setVisible(!fact_btn.isEmpty());
}

// ---------------------------------------------------------------------------------------------- the page
static QPushButton *seg_button(const QString &text, const QString &obj)
{
    auto *b = new QPushButton(text);
    b->setObjectName(obj);
    b->setCheckable(true);
    return b;
}

PopInfluencePage::PopInfluencePage(moddata::Mod &m, QWidget *parent) : QWidget(parent), mod(m)
{
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(70);
    connect(_timer, &QTimer::timeout, this, &PopInfluencePage::_show);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    // --- the pops
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 12, 6, 12);
    ll->addWidget(widgets::label(u"Pop influences"_s, u"title"_s));
    pops = new QListWidget();
    pops->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pops->setIconSize(QSize(28, 28));
    connect(pops, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it, QListWidgetItem *) {
        if (it)
            _set_pop(it->data(Qt::UserRole).toString());
    });
    ll->addWidget(pops, 1);
    split->addWidget(left);
    // --- the chart
    auto *mid = new QWidget();
    auto *ml = new QVBoxLayout(mid);
    ml->setContentsMargins(8, 12, 8, 12);
    ml->setSpacing(6);
    ml->addWidget(widgets::label(
        u"Pick a pop: the middle is a sample province, the cards are every line of the pop files that pushes people "
        "INTO that pop (green raises, red lowers; the game adds up the cards that hold and multiplies by the block's "
        "own factor). Set the case on the right — military spending, literacy, a national focus … — the cards that "
        "hold light up by themselves and the sample shows what becomes of the pop after the time picked."_s,
        u"faint"_s));
    auto *host = new QWidget();
    heads = new widgets::WrapRow(host, 6);
    ml->addWidget(host);
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    for (const QString k : {u"in"_s, u"out"_s}) {
        QPushButton *b = seg_button(QString(), u"seg"_s);
        connect(b, &QPushButton::clicked, this, [this, k] { _set_dir(k); });
        row->addWidget(b);
        dir_btn[k] = b;
    }
    for (const QString k : {u"poor"_s, u"middle"_s, u"rich"_s}) {
        QPushButton *b = seg_button(u"Read for the %1 pops"_s.arg(k), u"seg"_s);
        b->setToolTip(u"Some cards hold only for one class ( strata = rich ): which class the cards are lit for"_s);
        connect(b, &QPushButton::clicked, this, [this, k] { _set_class(k); });
        row->addWidget(b);
        class_btn[k] = b;
    }
    row->addSpacing(10);
    target_lbl = widgets::label(QString(), u"sub"_s, false);
    row->addWidget(target_lbl);
    row->addSpacing(6);
    targets = new QComboBox();
    targets->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(targets, &QComboBox::activated, this, [this](int) {
        const QVariant d = targets->currentData();
        _set_target(d.isValid() ? std::optional<QString>(d.toString()) : std::nullopt);
    });
    row->addWidget(targets, 1);
    ml->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(4);
    row->addWidget(widgets::label(u"Sample province after"_s, u"sub"_s, false));
    for (const auto &[mm, text] : popsim::TIMES) {
        QPushButton *b = seg_button(text, u"chip"_s);
        b->setChecked(mm == months);
        const long long mv = mm;
        connect(b, &QPushButton::clicked, this, [this, mv] { _set_months(mv); });
        row->addWidget(b);
        time_btn[mm] = b;
    }
    row->addSpacing(14);
    focus_box = new QComboBox();
    focus_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    focus_box->setMinimumWidth(260);
    focus_box->setToolTip(u"A national focus that encourages the picked pop, set on the sample province's state — its "
                          "tiers (measured in the game: all the promoted become that pop, and a few more are "
                          "promoted)"_s);
    connect(focus_box, &QComboBox::activated, this, &PopInfluencePage::_focus_picked);
    // (not shown: the national focus is picked on its card in the chart and in the case on the right — the user,
    // 2026-10-16: "completely redundant"; the box only keeps the list the rest reads)
    focus_box->setParent(this);
    focus_box->hide();
    row->addStretch(1);
    ml->addLayout(row);
    // --- where the sample comes from: made up, or a real province / state picked on the map (popplace)
    row = new QHBoxLayout();
    row->setSpacing(0);
    row->addWidget(widgets::label(u"Sample"_s, u"sub"_s, false));
    row->addSpacing(6);
    const std::tuple<QString, QString, QString> srcs[] = {
        {u"made"_s, u"Made up (the mod's mix)"_s,
         u"100,000 people, the mod's pop types in whole per cent; no cultures — the case's ticks say what holds"_s},
        {u"province"_s, u"A province on the map"_s,
         u"Click a province: its own pops, cultures and religions; the case takes its literacy, life rating, national "
         "value"_s},
        {u"state"_s, u"A state on the map"_s, u"Click a province: its whole state (the owner's part of it)"_s}};
    for (const auto &[k, text, tip] : srcs) {
        QPushButton *b = seg_button(text, u"seg"_s);
        b->setChecked(k == u"made");
        b->setToolTip(tip);
        const QString kk = k;
        connect(b, &QPushButton::clicked, this, [this, kk] { _set_source(kk); });
        row->addWidget(b);
        src_btn[k] = b;
    }
    row->addSpacing(10);
    place_lbl = new QLabel(QString());
    place_lbl->setObjectName(u"sub"_s);
    place_lbl->setWordWrap(true);
    place_lbl->setTextFormat(Qt::RichText);
    row->addWidget(place_lbl, 1);
    ml->addLayout(row);
    pmap = new regionimport::SourceMap();
    pmap->numbers = false;
    pmap->pick_outline = true;
    pmap->setMinimumSize(300, 160);
    pmap->setFixedHeight(250);
    pmap->group_of = [this](long long pid, Qt::KeyboardModifiers) { return _place_group(pid); };
    connect(pmap, &regionimport::SourceMap::changed, this, &PopInfluencePage::_place_picked);
    connect(pmap, &regionimport::SourceMap::hovered, this, &PopInfluencePage::_place_hover);
    pmap->hide();
    ml->addWidget(pmap);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setStyleSheet(u"QScrollArea { background: %1; border: 1px solid %2; }"_s.arg(theme::BG, theme::LINE));
    chart = new Chart();
    chart->names = [this](const QString &c) { return words(c); };
    chart->key_name = [this](const QString &k) { return key_name(k); };
    chart->fmt = [this](const QString &k, double v) { return fmt(k, v); };
    connect(chart, &Chart::picked, this, &PopInfluencePage::_pick);
    connect(chart, &Chart::toggled, this, &PopInfluencePage::_card_clicked);
    connect(chart, &Chart::ladderPicked, this, &PopInfluencePage::_ladder);
    connect(chart, &Chart::focusPicked, this, &PopInfluencePage::_focus_tier);
    chart->rate = [this](const Card &c) { return _card_rate(c); };
    chart->warn = [this](const Card &c) { return _card_warn(c); };
    auto *inner = new QWidget();
    inner->setObjectName(u"sideInner"_s);
    auto *il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(chart);
    il->addStretch(1);
    sc->setWidget(inner);
    ml->addWidget(sc, 1);
    split->addWidget(mid);
    // --- the right side: the case, the numbers, the picked card
    auto *sc2 = new QScrollArea();
    sc2->setWidgetResizable(true);
    sc2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc2->setObjectName(u"sideScroll"_s);
    auto *det = new QWidget();
    det->setObjectName(u"sideInner"_s);
    auto *dl = new QVBoxLayout(det);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(0);
    auto *s = new widgets::Section();
    d_title = widgets::label(QString(), u"label"_s);
    s->lay->addWidget(d_title);
    d_file = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(d_file);
    dl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"The case — what if"_s, u"label"_s));
    s->lay->addWidget(widgets::label(u"The state of the nation and the pop. The cards that hold follow from it (a click "
                                     "on a card sets what it asks for)."_s,
                                     u"faint"_s));
    case_panel = new CasePanel(cs, [this](const QString &c) { return words(c); },
                               [this](const QString &k) { return name(k); });
    case_panel->fact_note = [this](const QString &f) { return fact_note(f); };
    connect(case_panel, &CasePanel::focusPicked, this, &PopInfluencePage::_focus_set);
    connect(case_panel, &CasePanel::changed, _timer, qOverload<>(&QTimer::start));
    s->lay->addWidget(case_panel);
    dl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"In numbers"_s, u"label"_s));
    result = new QLabel(QString());
    result->setWordWrap(true);
    result->setTextFormat(Qt::RichText);
    s->lay->addWidget(result);
    fact = new QLabel(QString());
    fact->setObjectName(u"sub"_s);
    fact->setWordWrap(true);
    fact->setTextFormat(Qt::RichText);
    s->lay->addWidget(fact);
    size_row = new QWidget();
    row = new QHBoxLayout(size_row);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(widgets::label(u"The cards' own “a month” is said for a pop of"_s, u"sub"_s, false));
    size_edit = new QLineEdit(u"100000"_s);
    size_edit->setFixedWidth(90);
    size_edit->setAlignment(Qt::AlignRight);
    connect(size_edit, &QLineEdit::editingFinished, this, &PopInfluencePage::_size_done);
    row->addWidget(size_edit);
    row->addStretch(1);
    s->lay->addWidget(size_row);
    share_row = new QWidget();
    auto *sr = new QHBoxLayout(share_row);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->addWidget(widgets::label(popfacts::SHARE_LABEL, u"sub"_s, false));
    share_edit = new QLineEdit(QString());
    share_edit->setFixedWidth(70);
    share_edit->setAlignment(Qt::AlignRight);
    share_edit->setToolTip(popfacts::SHARE_TIP);
    connect(share_edit, &QLineEdit::editingFinished, this, &PopInfluencePage::_share_done);
    sr->addWidget(share_edit);
    sr->addWidget(widgets::label(u"%"_s, u"sub"_s, false));
    sr->addStretch(1);
    s->lay->addWidget(share_row);
    dl->addWidget(s);
    s = card_sec = new widgets::Section();
    c_head = widgets::label(u"The picked card"_s, u"label"_s);
    s->lay->addWidget(c_head);
    c_where = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(c_where);
    row = new QHBoxLayout();
    row->addWidget(widgets::label(u"Factor (above 0 raises, below 0 lowers)"_s, u"sub"_s, false));
    f_edit = new QLineEdit();
    f_edit->setFixedWidth(90);
    f_edit->setAlignment(Qt::AlignRight);
    connect(f_edit, &QLineEdit::editingFinished, this, &PopInfluencePage::_factor_done);
    row->addWidget(f_edit);
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(widgets::label(u"While all of this holds (script conditions, one a line):"_s, u"sub"_s));
    c_edit = new QPlainTextEdit();
    c_edit->setFixedHeight(120);
    c_edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    c_edit->setStyleSheet(box_qss());
    s->lay->addWidget(c_edit);
    row = new QHBoxLayout();
    row->setSpacing(6);
    auto *b = new QPushButton(u"Save the conditions"_s);
    connect(b, &QPushButton::clicked, this, &PopInfluencePage::_cond_done);
    row->addWidget(b);
    del_btn = new QPushButton(u"Take this card out"_s);
    connect(del_btn, &QPushButton::clicked, this, &PopInfluencePage::_remove);
    row->addWidget(del_btn);
    row->addStretch(1);
    s->lay->addLayout(row);
    dl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Change the files"_s, u"label"_s));
    row = new QHBoxLayout();
    base_lbl = widgets::label(u"The block's own factor (multiplies the sum of the cards)"_s, u"sub"_s, false);
    row->addWidget(base_lbl);
    base_edit = new QLineEdit();
    base_edit->setFixedWidth(90);
    base_edit->setAlignment(Qt::AlignRight);
    connect(base_edit, &QLineEdit::editingFinished, this, &PopInfluencePage::_base_done);
    row->addWidget(base_edit);
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(widgets::label(u"Add an influence (a card)"_s, u"sub"_s));
    row = new QHBoxLayout();
    row->addWidget(widgets::label(u"Factor"_s, u"sub"_s, false));
    a_factor = new QLineEdit();
    a_factor->setPlaceholderText(u"e.g. 0.5 or -1"_s);
    a_factor->setFixedWidth(110);
    row->addWidget(a_factor);
    row->addStretch(1);
    s->lay->addLayout(row);
    quick = new QComboBox();
    quick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(quick, &QComboBox::activated, this, &PopInfluencePage::_quick);
    s->lay->addWidget(quick);
    a_cond = new QPlainTextEdit();
    a_cond->setPlaceholderText(u"conditions, one a line — like\nnationalvalue = nv_order\nliteracy = 0.5"_s);
    a_cond->setFixedHeight(96);
    a_cond->setStyleSheet(box_qss());
    s->lay->addWidget(a_cond);
    add_btn = new QPushButton(u"＋ Add it"_s);
    connect(add_btn, &QPushButton::clicked, this, &PopInfluencePage::_add);
    s->lay->addWidget(add_btn, 0, Qt::AlignLeft);
    msg = widgets::label(QString(), u"sub"_s);
    msg->setStyleSheet(u"color: #ff786e;"_s);
    msg->hide();
    s->lay->addWidget(msg);
    dl->addWidget(s);
    dl->addStretch(1);
    sc2->setWidget(det);
    split->addWidget(sc2);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 10);
    split->setStretchFactor(2, 4);
    split->setSizes({190, 1040, 440});
}

PopInfluencePage::~PopInfluencePage() = default;

// ------------------------------------------------------------------ data
QString PopInfluencePage::name(const QString &key) const
{
    std::optional<QString> t;
    if (doc && doc->maps.contains(u"loc"_s))
        t = doc->map<localisation::LocEdits>(u"loc"_s).text(key);
    else if (const QString *v = mod.loc.get(key))
        t = *v;
    return t && !t->isEmpty() ? *t : _pretty(key);
}

QString PopInfluencePage::key_name(const QString &key) const
{
    // A condition's key in words: a setting of the case, a pop type's share, or the key itself.
    const QString k = py::lower(key);
    const QString k2 = popcase::ALIAS.value(k, k);
    if (popcase::VARS.contains(k2))
        return popcase::VARS.get(k2)->name + (k != k2 ? u" (%1 class)"_s.arg(py::split(k, u"_"_s)[0]) : QString());
    if (_tset.contains(k))
        return name(k) + u" in the province"_s;
    return _pretty(key);
}

QString PopInfluencePage::fmt(const QString &key, double v) const
{
    // A condition's number in words ( 0.5 → 50 % for a share ).
    const QString lk = py::lower(key);
    const QString k = popcase::ALIAS.value(lk, lk);
    if (popcase::VARS.contains(k))
        return popcase::show(k, v);
    if (_tset.contains(k))
        return py::fmt_g(py::round_to(v * 100, 3)) + u" %"_s;
    return py::fmt_g(v);
}

// re.sub with a function, over a QRegularExpression
static QString re_sub(const QRegularExpression &re, const QString &text,
                      const std::function<QString(const QRegularExpressionMatch &)> &fn)
{
    QString out;
    qsizetype at = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(at, m.capturedStart() - at);
        out += fn(m);
        at = m.capturedEnd();
    }
    out += text.mid(at);
    return out;
}

static const auto UNI = QRegularExpression::UseUnicodePropertiesOption;
static const auto UNI_I = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption;
static const QRegularExpression W_NUM(uR"(\b([A-Za-z_]+)\s*=\s*(-?\d+(?:\.\d+)?)(?![\w.]))"_s, UNI);
static const QRegularExpression W_NOTGE(uR"(\bNOT\s*=\s*\{\s*([^{}=≥]+?) ≥ ([^{}]+?)\s*\})"_s, UNI_I);
static const QRegularExpression W_SCOPE(uR"(\b(?:country|owner|state_scope|location)\s*=\s*\{\s*([^{}]*?)\s*\})"_s, UNI_I);
static const QRegularExpression W_NOT(uR"(\bNOT\s*=\s*\{\s*([^{}]*?)\s*\})"_s, UNI_I);
static const QRegularExpression W_ONE(uR"(\b([A-Za-z_]+)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\b)"_s, UNI);

QString PopInfluencePage::words(const QString &cond) const
{
    // A card's conditions in words: the game's names where a line names a thing (a national value, a pop …), "at
    // least" where it asks for a number ( literacy = 0.5  holds from 50 % on ).
    QString out = re_sub(W_NUM, cond, [&](const QRegularExpressionMatch &m) {
        const QString k = py::lower(m.captured(1));
        if (popcase::VARS.contains(popcase::ALIAS.value(k, k)) || _tset.contains(k))
            return u"%1 ≥ %2"_s.arg(key_name(k), fmt(k, py::to_float(m.captured(2)).value_or(0.0)));
        return m.captured(0);
    });
    out.replace(W_NOTGE, u"\\1 below \\2"_s);
    for (int k = 0; k < 4; ++k) {                    // the scopes fall away, NOT = { … } reads "not ( … )"
        QString nw = QString(out).replace(W_SCOPE, u"\\1"_s);
        nw.replace(W_NOT, u"not (\\1)"_s);
        if (nw == out)
            break;
        out = nw;
    }
    return re_sub(W_ONE, out, [&](const QRegularExpressionMatch &m) {
        const QString k = m.captured(1), v = m.captured(2);
        const QString *lv = mod.loc.get(v);
        const bool named = doc && ((lv && !lv->isEmpty()) || k == u"nationalvalue" || k == u"is_ideology_enabled" ||
                                   k == u"government" || k == u"has_pop_type");
        const QString nm = named ? name(v) : v;
        const QString kl = py::lower(k);
        if (kl == u"has_country_flag" || kl == u"has_global_flag" || kl == u"has_province_flag")
            return u"flag “%1”"_s.arg(nm);
        return u"%1 = %2"_s.arg(KEY_WORDS.value(kl, _pretty(k)), nm);
    });
}

const QSet<QString> &PopInfluencePage::flags_set()
{
    // The flags something in the mod's files sets (events, decisions, inventions …) — a card that waits for a flag
    // nothing sets never holds in the game.
    if (!_flags || _flags->first != doc) {
        QSet<QString> found;
        for (const QString sub : {u"events"_s, u"decisions"_s, u"common"_s, u"inventions"_s, u"technologies"_s,
                                  u"history/countries"_s}) {
            const QString d = mod.p(sub.split(u'/'));
            QDir qd(d);
            if (!qd.exists())
                continue;
            for (const QString &fn : qd.entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name)) {
                if (!py::lower(fn).endsWith(u".txt"))
                    continue;
                QFile fh(qd.filePath(fn));
                if (!fh.open(QIODevice::ReadOnly))
                    continue;
                const QString text = QString::fromLatin1(fh.readAll());
                auto it = SETFLAG.globalMatch(text);
                while (it.hasNext())
                    found.insert(py::lower(it.next().captured(1)));
            }
        }
        _flags = std::make_pair(static_cast<const document::Document *>(doc), found);
    }
    return _flags->second;
}

QString PopInfluencePage::fact_note(const QString &f)
{
    // What is odd about a fact of the case ("" when nothing): a flag nothing in the mod sets.
    static const QRegularExpression re(uR"(^has_(country|global|province)_flag = (\S+)$)"_s, UNI);
    QString t = f;
    if (t.endsWith(u'\n'))                           // (Python's $ also matches before a last newline)
        t.chop(1);
    const QRegularExpressionMatch m = re.match(t);
    if (m.hasMatch() && !flags_set().contains(py::lower(m.captured(2))))
        return u"Nothing in the mod's files sets the flag “%1” (no set_%2_flag for it in events, decisions, inventions "
               "…): in the game the cards that wait for it never hold."_s.arg(m.captured(2), m.captured(1));
    return QString();
}

QStringList PopInfluencePage::files() const
{
    QStringList out = entryview::_files(mod, store, u"poptypes"_s);
    if (QFileInfo(mod.p({u"common"_s, u"pop_types.txt"_s})).isFile())
        out << u"common/pop_types.txt"_s;
    return out;
}

QStringList PopInfluencePage::types() const
{
    QStringList out;
    for (const Chance &c : chances)
        if (c.pop != u"pop_types" && !out.contains(c.pop))
            out << c.pop;
    return out;
}

std::vector<const Chance *> PopInfluencePage::into_blocks(const QString &p) const
{
    // The blocks of the pop files that say how likely a pop becomes `pop`: one a source pop.
    std::vector<const Chance *> out;
    for (const Chance &c : chances)
        if (c.head == u"promote_to" && c.target == p)
            out.push_back(&c);
    return out;
}

const PopInfluencePage::Merged &PopInfluencePage::_merged(const std::vector<const Chance *> &blocks)
{
    // Several source pops' blocks into one pop as ONE list of cards: a card that stands in several files is shown
    // once ("at": every place it stands).
    if (_merged_memo && _merged_memo->key == blocks)
        return *_merged_memo;
    Merged mg;
    mg.key = blocks;
    std::map<QString, size_t> seen;
    for (const Chance *c : blocks) {
        QHash<QString, long long> nth;
        for (size_t k = 0; k < c->mods.size(); ++k) {
            const Card &m = c->mods[k];
            // the same card in another file is one card; twice in the SAME block it counts twice in the game — two
            // cards (the user, 2026-10-16: "some is missing")
            const QString d = m.factor + QChar(2) + m.cond + QChar(2) + (m.gid ? u"1"_s + *m.gid : u"0"_s);
            const long long n = nth.value(d, -1) + 1;
            nth.insert(d, n);
            const QString dn = d + QChar(3) + QString::number(n);
            const auto it = seen.find(dn);
            if (it != seen.end()) {
                mg.at[it->second].push_back({c, k});
            } else {
                seen[dn] = mg.ch.mods.size();
                mg.ch.mods.push_back(m);
                mg.at.push_back({{c, k}});
            }
        }
    }
    QSet<QString> bases;
    for (const Chance *c : blocks)
        bases.insert(c->base);
    mg.ch.rel = QString();
    mg.ch.pop = blocks[0]->target;
    mg.ch.head = u"promote_to"_s;
    mg.ch.target = blocks[0]->target;
    mg.ch.path = {u"into"_s, blocks[0]->target};
    mg.ch.base = bases.size() == 1 ? *bases.begin() : QString();
    mg.ch.bspan = blocks[0]->bspan;
    mg.ch.span = {0, 0};
    _merged_memo = std::move(mg);
    return *_merged_memo;
}

std::optional<PopInfluencePage::View> PopInfluencePage::cur()
{
    // The block in view (several source pops' blocks into the picked pop: merged into one).
    auto one = [](const Chance *c) -> std::optional<View> {
        if (!c)
            return std::nullopt;
        View v;
        v.ch = c;
        v.blocks = {c};
        return v;
    };
    if (head && *head == u"promote_to" && pop != u"pop_types"_s) {
        if (dir == u"in") {
            const auto bl = into_blocks(pop.value_or(QString()));
            if (source) {
                for (const Chance *c : bl)
                    if (c->pop == *source)
                        return one(c);
                return std::nullopt;
            }
            if (bl.empty() || !pop)
                return std::nullopt;
            const Merged &mg = _merged(bl);
            View v;
            v.ch = &mg.ch;
            v.merged = true;
            v.blocks = mg.key;
            v.at = &mg.at;
            return v;
        }
        for (const Chance &c : chances)
            if (pop && c.pop == *pop && c.head == u"promote_to" && c.target == target)
                return one(&c);
        return std::nullopt;
    }
    for (const Chance &c : chances)
        if (pop && head && c.pop == *pop && c.head == *head && c.target == target)
            return one(&c);
    return std::nullopt;
}

std::vector<std::pair<const Chance *, size_t>> PopInfluencePage::at_of(const View &v, long long i) const
{
    // Where a card of the view stands in the files: [(block, its number there)].
    if (v.merged)
        return (*v.at)[static_cast<size_t>(i)];
    return {{v.ch, static_cast<size_t>(i)}};
}

popcase::Tree PopInfluencePage::tree(const Card &m) const { return popcase::compile_cond(m.src, _tset); }

popcase::Block &PopInfluencePage::block(const Chance &c)
{
    const std::pair<QString, QString> k{c.rel, c.path.join(QChar(0x1f))};
    auto it = _blocks.find(k);
    if (it == _blocks.end()) {
        popcase::Chance pc;
        pc.base = c.base;
        for (const Card &m : c.mods)
            pc.mods.push_back({m.factor, m.src});
        it = _blocks.emplace(k, std::make_unique<popcase::Block>(pc, _tset)).first;
    }
    return *it->second;
}

static QStringList set_order(const QStringList &types)   // set(types): walked in Python's order
{
    grid::PySet<QString> s;
    for (const QString &t : types)
        s.add(t);
    QStringList out;
    for (const QString &t : s.items())
        out << t;
    return out;
}

void PopInfluencePage::refresh(document::Document *d)
{
    doc = d;
    store = d && d->maps.contains(u"scripts"_s) ? &d->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    {
        const QStringList fl = files();
        std::vector<std::optional<QString>> texts;
        for (const QString &rel : fl) {
            try {
                texts.push_back(store->text(rel));
            } catch (const std::exception &) {
                texts.push_back(std::nullopt);
            }
        }
        if (!_chances_key || _chances_key->first != fl || _chances_key->second != texts) {
            chances = read_chances(*store, fl);
            _chances_key = std::make_pair(fl, std::move(texts));
        }
    }
    _blocks.clear();
    _merged_memo.reset();
    _tset = types();
    _info = _type_info(types());
    _start = popsim::sample(doc, mod, set_order(types()));
    if (place) {                                     // (the pops may have been edited: the place read anew)
        place = std::make_unique<popplace::Place>(mod, *doc, place->pids, place->what);
        _take_place(false);
    }
    if (src != u"made")
        _paint_place_map();
    try {
        defs = popfacts::defines(store->text(u"common/defines.lua"_s));
    } catch (...) {
        defs = popfacts::Defs();
    }
    _read_focuses();
    if (share_edit->text().isEmpty())
        share_edit->setText(py::fmt_g(py::round_to(popfacts::share(defs) * 100, 1)));
    QStringList pl;
    for (const Chance &c : chances)
        if (!pl.contains(c.pop))
            pl << c.pop;
    if (!pop || !pl.contains(*pop))
        pop = !pl.isEmpty() ? std::optional<QString>(pl[0]) : std::nullopt;
    pops->blockSignals(true);
    pops->clear();
    const std::vector<QPixmap> frames = iconstrips::frames(iconstrips::strips_for(mod, *doc), u"GFX_pops_small"_s);
    for (const QString &pk : pl) {
        auto *it = new QListWidgetItem(pk == u"pop_types" ? u"Every pop"_s : name(pk));
        it->setData(Qt::UserRole, pk);
        try {
            const std::shared_ptr<const script::Nodes> nsp = _parsed(u"poptypes/%1.txt"_s.arg(pk));
            const script::Nodes &ns = *nsp;
            const script::Node *top = nullptr;
            for (const script::Node &n : ns)
                if (n.has_key && n.key == u"sprite") {
                    top = &n;
                    break;
                }
            const long long n = top ? py::to_int(top->value) : 0;
            if (1 <= n && n <= static_cast<long long>(frames.size()))
                it->setIcon(QIcon(frames[static_cast<size_t>(n - 1)]));
        } catch (...) {
        }
        pops->addItem(it);
        if (pop && pk == *pop)
            pops->setCurrentItem(it);
    }
    pops->blockSignals(false);
    nvs.clear();
    try {
        const std::shared_ptr<const script::Nodes> nv = _parsed(u"common/nationalvalues.txt"_s);
        for (const script::Node &n : *nv)
            if (n.has_key && !n.key.isEmpty() && n.is_block)
                nvs << n.key;
    } catch (...) {
    }
    quick->clear();
    quick->addItem(u"Quick: put a condition in …"_s, QVariant());
    for (const QString &k : nvs)
        quick->addItem(u"the nation has the national value "_s + name(k), u"nationalvalue = "_s + k);
    const std::pair<QString, QString> lines[] = {
        {u"military spending at least …"_s, u"military_spending = 0.5"_s},
        {u"literacy at least …"_s, u"literacy = 0.5"_s},
        {u"militancy at least …"_s, u"militancy = 5"_s},
        {u"consciousness at least …"_s, u"consciousness = 5"_s},
        {u"unemployed share at least …"_s, u"unemployment = 0.1"_s},
        {u"NOT: …"_s, u"NOT = { literacy = 0.5 }"_s}};
    for (const auto &[text, line] : lines)
        quick->addItem(text, line);
    _show();
}

void PopInfluencePage::_read_focuses()
{
    // The national focuses that encourage a pop type ( soldiers = 0.15 inside a focus ): [(key, pop, value)].
    QSet<QString> tys;
    for (const Chance &c : chances)
        tys.insert(c.pop);
    focuses.clear();
    try {
        const std::shared_ptr<const script::Nodes> nf = _parsed(u"common/national_focus.txt"_s);
        for (const script::Node &g : *nf) {
            if (!g.is_block)
                continue;
            for (const script::Node &f : g.kids) {
                if (!(f.has_key && !f.key.isEmpty() && f.is_block))
                    continue;
                for (const script::Node &c : f.kids) {
                    const std::optional<double> v =
                        c.has_key && tys.contains(c.key) && !c.is_block ? _num(c.value) : std::nullopt;
                    if (v && *v != 0.0)
                        focuses.push_back({f.key, c.key, *v});
                }
            }
        }
    } catch (...) {
    }
}

std::vector<Focus> PopInfluencePage::tiers() const
{
    // The focuses that encourage the picked pop, weakest first ("Every pop": all of them).
    if (pop && *pop == u"pop_types")
        return focuses;
    std::vector<Focus> out;
    for (const Focus &f : focuses)
        if (pop && f.pop == *pop)
            out.push_back(f);
    std::stable_sort(out.begin(), out.end(), [](const Focus &a, const Focus &b) { return a.value < b.value; });
    return out;
}

QString PopInfluencePage::strata_of(const std::optional<View> &v) const
{
    // The class the cards of the view are read for.
    if (!v || (v->ch->pop == u"pop_types" && !v->merged))
        return view_class;
    const auto info_of = [&](const QString &t) {
        const auto it = _info.constFind(t);
        return it == _info.constEnd() ? u"poor"_s : it->second;
    };
    if (v->ch->head == u"promote_to") {
        QString best;
        long long top = 0;
        bool first = true;
        for (const Chance *c : v->blocks) {
            const long long n = _start.value(c->pop, 0);
            if (first || n > top) {
                best = c->pop;
                top = n;
                first = false;
            }
        }
        return info_of(best);
    }
    return info_of(v->ch->pop);
}

popcase::Shares PopInfluencePage::shares0() const
{
    long long s = 0;
    for (const auto &[_k, v] : _start)
        s += v;
    const double tot = s ? static_cast<double>(s) : 1.0;
    popcase::Shares out;
    for (const auto &[k, v] : _start)
        out.insert(k, static_cast<double>(v) / tot);
    return out;
}

// ------------------------------------------------------------------ the view
void PopInfluencePage::_show()
{
    if (!store)
        return;
    QListWidgetItem *it = pops->currentItem();
    if (!it || !pop || it->data(Qt::UserRole).toString() != *pop) {   // (the pop was set from elsewhere: the list follows)
        pops->blockSignals(true);
        for (int k = 0; k < pops->count(); ++k)
            if (pop && pops->item(k)->data(Qt::UserRole).toString() == *pop)
                pops->setCurrentRow(k);
        pops->blockSignals(false);
    }
    const bool real = !(pop && *pop == u"pop_types");
    const QString pk = pop.value_or(QString());
    std::vector<const Chance *> mine;
    for (const Chance &c : chances)
        if (pop && c.pop == *pop)
            mine.push_back(&c);
    QStringList hs;
    bool any_promo = false;
    for (const Chance *c : mine)
        any_promo = any_promo || c->head == u"promote_to";
    if (real && (!into_blocks(pk).empty() || any_promo))
        hs << u"promote_to"_s;
    for (const Chance *c : mine)
        if (!hs.contains(c->head))
            hs << c->head;
    if (!head || !hs.contains(*head))
        head = !hs.isEmpty() ? std::optional<QString>(hs[0]) : std::nullopt;
    QStringList have;
    for (const auto &[h, _b] : head_btn)
        have << h;
    if (have != hs) {
        for (const auto &[_h, b] : head_btn) {
            b->hide();
            b->setParent(nullptr);
            b->deleteLater();
        }
        head_btn = decltype(head_btn)();
        for (const QString &h : hs) {
            const auto *pt = per_target(h);
            const QString *sg = single(h);
            QPushButton *b = seg_button(pt ? pt->first : sg ? *sg : _pretty(h), u"chip"_s);
            connect(b, &QPushButton::clicked, this, [this, h] { _set_head(h); });
            heads->addWidget(b);
            head_btn.insert(h, b);
        }
    }
    for (const auto &[h, b] : head_btn)
        b->setChecked(head && h == *head);
    const bool promo = head && *head == u"promote_to" && real;
    const std::vector<const Chance *> into = promo ? into_blocks(pk) : std::vector<const Chance *>();
    std::vector<const Chance *> outs;
    if (promo)
        for (const Chance *c : mine)
            if (c->head == u"promote_to")
                outs.push_back(c);
    if (promo) {
        if (dir == u"in" && into.empty())
            dir = u"out"_s;
        else if (dir == u"out" && outs.empty())
            dir = u"in"_s;
    }
    const QString nm = pop && real ? name(*pop) : QString();
    for (const auto &[k, b] : dir_btn) {
        b->setVisible(promo);
        b->setChecked(k == dir);
        b->setEnabled(!(k == u"in" ? into : outs).empty());
    }
    dir_btn[u"in"_s]->setText(u"Who becomes "_s + nm);
    dir_btn[u"in"_s]->setToolTip(
        u"What pushes other pops into %1: the cards of the pops that can become it. The same cards count when a pop is "
        "promoted (into the same or a higher class) and when it is demoted (into the same or a lower one)."_s.arg(nm));
    dir_btn[u"out"_s]->setText(u"What %1 become"_s.arg(nm));
    dir_btn[u"out"_s]->setToolTip(u"What pushes %1 into other pops"_s.arg(nm));
    for (const auto &[k, b] : class_btn) {
        b->setVisible(!real);
        b->setChecked(k == view_class);
    }
    // the box next to it: the source pop (who becomes it), or the target
    targets->blockSignals(true);
    targets->clear();
    const bool per = head && per_target(*head);
    if (promo && dir == u"in") {
        bool found = false;
        for (const Chance *c : into)
            found = found || (source && c->pop == *source);
        if (!found)
            source.reset();
        target_lbl->setText(u"From"_s);
        QStringList ns;
        for (const Chance *c : into)
            ns << name(c->pop);
        targets->addItem(u"every pop that can (%1)"_s.arg(ns.join(u", "_s)), QVariant());
        for (const Chance *c : into)
            targets->addItem(u"%1 only  (%2 cards)"_s.arg(name(c->pop)).arg(c->mods.size()), c->pop);
        targets->setCurrentIndex(std::max(0, source ? targets->findData(*source) : 0));
    } else if (per) {
        std::vector<const Chance *> tg;
        if (promo)
            tg = outs;
        else
            for (const Chance *c : mine)
                if (c->head == *head)
                    tg.push_back(c);
        bool found = false;
        for (const Chance *c : tg)
            found = found || c->target == target;
        if (!found) {
            const Chance *best = nullptr;
            for (const Chance *c : tg)
                if (!best || c->mods.size() > best->mods.size())
                    best = c;
            target = best ? best->target : QString();
        }
        const QHash<QString, QString> lbl = {
            {u"promote_to"_s, u"Becoming"_s}, {u"ideologies"_s, u"Leaning towards"_s}, {u"issues"_s, u"Wanting"_s}};
        target_lbl->setText(lbl.value(*head));
        for (const Chance *c : tg)
            targets->addItem(u"%1  (%2 card%3)"_s.arg(name(c->target))
                                 .arg(c->mods.size())
                                 .arg(c->mods.size() == 1 ? QString() : u"s"_s),
                             c->target);
        targets->setCurrentIndex(targets->findData(target));
    } else {
        target = QString();
    }
    targets->blockSignals(false);
    targets->setVisible(per && real);
    target_lbl->setVisible(per && real);
    // the national focus: the tiers that encourage the picked pop
    const std::vector<Focus> tr = tiers();
    if (focus && index_of(tr, *focus) < 0)
        focus.reset();
    focus_box->blockSignals(true);
    focus_box->clear();
    focus_box->addItem(!tr.empty() ? u"No national focus on %1"_s.arg(!nm.isEmpty() ? nm : u"a pop"_s)
                                   : u"No national focus encourages %1"_s.arg(!nm.isEmpty() ? nm : u"a pop"_s),
                       QVariant());
    for (size_t k = 0; k < tr.size(); ++k)
        focus_box->addItem(u"National focus: %1  (%2 %3)"_s.arg(name(tr[k].key), name(tr[k].pop), pg(tr[k].value)),
                           QVariant::fromValue(static_cast<long long>(k)));
    focus_box->setCurrentIndex(!focus ? 0 : static_cast<int>(1 + index_of(tr, *focus)));
    focus_box->setEnabled(!tr.empty());
    focus_box->blockSignals(false);
    case_panel->show_focuses(tr, focus, nm, !real);
    const std::optional<View> cv = cur();
    const QString who = !real ? u"Every pop"_s : nm;
    if (!cv) {
        chart->focus_card.reset();
        chart->sample.reset();
        chart->result = QString();
        chart->net.reset();
        chart->ticked.clear();
        chart->set_data(who, u"nothing to show"_s, QString(), {}, std::nullopt);
        d_title->setText(QString());
        d_file->setText(QString());
        result->setText(QString());
        card_sec->hide();
        return;
    }
    const View &v = *cv;
    const Chance &ch = *v.ch;
    const long long nmods = static_cast<long long>(ch.mods.size());
    if (sel && !(-1 <= *sel && *sel < nmods))
        sel.reset();
    const bool merged = v.merged;
    QString what;
    if (promo && dir == u"in") {
        QStringList ns;
        for (const Chance *c : v.blocks)
            ns << name(c->pop);
        what = u"who becomes %1 — from %2"_s.arg(nm, ns.join(u", "_s));
    } else if (per) {
        what = QString(per_target(ch.head)->second).replace(u"%s"_s, name(ch.target));
    } else {
        const QString *sg = single(ch.head);
        what = sg ? *sg : _pretty(ch.head);
    }
    // the cards that hold: worked out from the case
    const QString strata = strata_of(cv);
    const popcase::Shares shares = shares0();
    grid::PySet<long long> held;
    for (long long i = 0; i < nmods; ++i)
        if (cs.holds(tree(ch.mods[static_cast<size_t>(i)]), strata, shares))
            held.add(i);
    chart->ticked.clear();
    for (long long i : held.items())
        chart->ticked.insert(i);
    QSet<QString> used, facts, also;
    for (const Card &m : ch.mods) {
        const auto [vs, fs, _d] = popcase::parts(tree(m));
        used.unite(vs);
        facts.unite(fs);
    }
    if (ch.head == u"promote_to")                    // how many are promoted / demoted at all: Every pop's chances
        for (const Chance &c : chances)
            if (c.pop == u"pop_types" && (c.head == u"promotion_chance" || c.head == u"demotion_chance"))
                for (const Card &m : c.mods)
                    also.unite(std::get<0>(popcase::parts(tree(m))));
    bool has_nv = false;
    for (const QString &f : std::as_const(facts))
        has_nv = has_nv || f.startsWith(u"nationalvalue = ");
    case_panel->show_for(used, also, facts, has_nv || case_panel->more_btn->isChecked() ? nvs : QStringList());
    const auto [rule, _has] = popfacts::rule(ch.head, defs);
    fact->setText(rule);
    const bool chance = popfacts::SCALES.contains(ch.head);
    share_row->setVisible(ch.head == u"promotion_chance" || ch.head == u"promote_to");
    size_row->setVisible(chance);
    double base = 1.0;
    if (!ch.base.isEmpty())
        base = _num(ch.base).value_or(1.0);
    py::Sum ts;
    for (long long i : held.items())
        ts.add(fnum(ch.mods[static_cast<size_t>(i)].factor));
    const double total = ts.value();
    const double value = base * total;
    QString line = u"%1 of %2 cards hold: sum %3 × %4 = %5"_s.arg(held.size())
                       .arg(nmods)
                       .arg(popfacts::num(total), !ch.base.isEmpty() ? ch.base : u"1"_s, popfacts::num(value));
    if (merged && v.blocks.size() > 1)               // (one sum over every file's cards means nothing: a pop each)
        line = u"%1 of %2 cards hold — each pop's own sum is above, × the block's own factor %3"_s.arg(held.size())
                   .arg(nmods)
                   .arg(!ch.base.isEmpty() ? ch.base : u"(differs by file)"_s);
    const QString said =
        chance ? popfacts::say(ch.head, value, size, defs, _share())
               : (ch.head == u"promote_to" || ch.head == u"migration_target" || ch.head == u"country_migration_target"
                      ? u"a weight: where the people go, not how many"_s
                      : QString());
    chart->result = line + (!said.isEmpty() ? u"\n"_s + said : QString());
    chart->net = _net(v, held);
    chart->focus_card.reset();
    if (promo && dir == u"in" && !tr.empty()) {
        const qsizetype pk2 = focus ? index_of(tr, *focus) : -1;
        const std::optional<long long> pick = pk2 >= 0 ? std::optional<long long>(pk2) : std::nullopt;
        // its weight against the cards, in card points: measured, a focus of value V raised the promotion number by
        // about 0.9 × V; the cards reach that number through the block's own factor (card × factor)
        std::vector<std::optional<double>> bases;
        for (const Chance *b : v.blocks)
            if (!b->base.isEmpty())
                bases.push_back(_num(b->base));
        const std::optional<double> own =
            !ch.base.isEmpty() ? _num(ch.base) : (!bases.empty() ? bases[0] : std::nullopt);
        FocusCard fc;
        fc.name = nm;
        for (const Focus &f : tr)
            fc.tiers.push_back({f.key, name(f.key), f.value});
        fc.pick = pick;
        if (pick && own && *own != 0.0)
            fc.units = 0.9 * tr[static_cast<size_t>(*pick)].value / *own;
        chart->focus_card = fc;
    } else {
        chart->focus_open = false;
    }
    chart->sample = _sample(v);
    chart->set_data(who, what, ch.base, ch.mods, sel);
    d_title->setText(!(promo && dir == u"in") ? u"%1 — %2"_s.arg(who, what) : py::upper(what.left(1)) + what.mid(1));
    const auto [neg, pos, nil] = chart->sides();
    if (merged) {
        QStringList fs;
        for (const Chance *c : v.blocks)
            fs << py::split(c->rel, u"/"_s).last();
        d_file->setText(u"%1 pop files (%2) › promote_to › %3 · %4 cards raise it, %5 lower it. A card that stands in "
                        "several of them is shown once; a change goes into every one."_s.arg(v.blocks.size())
                            .arg(fs.join(u", "_s), ch.target)
                            .arg(pos.size())
                            .arg(neg.size()));
    } else {
        d_file->setText(u"%1 › %2 · %3 raise it, %4 lower it%5"_s.arg(ch.rel, ch.path.join(u" › "_s))
                            .arg(pos.size())
                            .arg(neg.size())
                            .arg(!nil.empty() ? u", %1 with factor 0 (do nothing)"_s.arg(nil.size()) : QString()));
    }
    result->setText(u"<b>%1</b>%2%3"_s.arg(line, !said.isEmpty() ? u"<br>"_s + said : QString(), _sample_text()));
    if (!(base_edit->hasFocus() && base_edit->isModified()))   // (not while it is being typed in — but an undone
        base_edit->setText(ch.base);                           // number must not stay in the box and come back)
    base_edit->setEnabled(ch.bspan.has_value());
    base_lbl->setText(u"The block's own factor%1"_s.arg(merged ? u" — in all %1 files"_s.arg(v.blocks.size())
                                                               : u" (multiplies the sum of the cards)"_s));
    add_btn->setText(u"＋ Add it%1"_s.arg(merged ? u" to all %1 files"_s.arg(v.blocks.size()) : QString()));
    const Card *m = sel && *sel >= 0 ? &ch.mods[static_cast<size_t>(*sel)] : nullptr;
    card_sec->setVisible(m != nullptr);
    _armed = false;
    del_btn->setText(u"Take this card out"_s);
    del_btn->setStyleSheet(QString());
    if (m) {
        const auto at = at_of(v, *sel);
        f_edit->setText(m->factor);
        f_edit->setEnabled(m->fspan.has_value());
        c_edit->setPlainText(_cond_lines(v, *sel).join(u'\n'));
        c_head->setText(u"The picked card%1"_s.arg(m->group && *m->group
                                                       ? u" (in group %1 of the file)"_s.arg(*m->group)
                                                       : QString()));
        QStringList ws;
        for (const auto &[c, _k] : at)
            ws << u"%1 › %2"_s.arg(py::split(c->rel, u"/"_s).last(), c->path.join(u" › "_s));
        c_where->setText(u"It stands in: %1. %2"_s.arg(ws.join(u", "_s), at.size() > 1
                                                                            ? u"A change here changes all of them."_s
                                                                            : QString()));
    }
}

Net PopInfluencePage::_net(const View &v, const grid::PySet<long long> &held)
{
    // The headline of the chart (the user, 2026-10-16: everything raising was on and nothing moved — a −100 still
    // held on the other side and took a long search): what the cards that hold come to, raising and lowering apart,
    // and the card that holds it down.
    const Chance &ch = *v.ch;
    if (v.merged && v.blocks.size() > 1) {
        // several source pops: each has its own block and its own sum — a −100 in one pop's file stops only that
        // pop. One line each.
        Net out;
        py::Sum ps, ns;
        std::optional<double> lo;
        for (const Chance *b : v.blocks) {
            grid::PySet<long long> mine;
            for (long long i : held.items()) {
                bool in = false;
                for (const auto &[c, _k] : (*v.at)[static_cast<size_t>(i)])
                    in = in || c == b;
                if (in)
                    mine.add(i);
            }
            View one;
            one.ch = v.ch;
            one.blocks = {b};
            const Net o = _net(one, mine);
            out.per.push_back({name(b->pop), o});
            ps.add(o.pos);
            ns.add(o.neg);
            lo = lo ? std::min(*lo, o.net) : o.net;
        }
        out.pos = ps.value();
        out.neg = ns.value();
        out.net = lo.value_or(0.0);
        out.how = u"per"_s;
        return out;
    }
    std::vector<std::pair<double, long long>> vals;
    for (long long i : held.items())
        vals.push_back({_num(ch.mods[static_cast<size_t>(i)].factor).value_or(0.0), i});
    py::Sum ps, ns;
    std::vector<std::pair<double, long long>> lows;
    for (const auto &[x, i] : vals) {
        if (x > 0)
            ps.add(x);
        if (x < 0) {
            ns.add(x);
            lows.push_back({x, i});
        }
    }
    std::sort(lows.begin(), lows.end());
    Net out;
    out.pos = ps.value();
    out.neg = ns.value();
    auto wds = [&](long long i) {
        const QString w = words(ch.mods[static_cast<size_t>(i)].cond);
        return w.isEmpty() ? u"always"_s : w;
    };
    if (ch.head == u"migration_target") {            // (measured: multiplied there)
        double prod = 1.0;
        for (const auto &[x, _i] : vals)
            prod *= x;
        out.net = prod;
        out.how = u"product"_s;
        return out;
    }
    out.net = out.pos + out.neg;
    out.how = u"sum"_s;
    if (vals.empty()) {
        out.note = u"No card holds in this case — "_s + (ch.head == u"promote_to" ? u"this target is never picked."_s
                                                                                  : u"nothing comes of it."_s);
    } else if (out.net <= 0 && !lows.empty()) {
        const size_t more = lows.size() - 1;
        out.note = u"Held down by %1  (%2)%3 — %4"_s.arg(
            popfacts::num(lows[0].first), wds(lows[0].second),
            more ? u" and %1 more lowering"_s.arg(more) : QString(),
            ch.head == u"promote_to" ? u"at 0 or below this target is never picked."_s : u"nothing comes of it."_s);
    }
    return out;
}

QString PopInfluencePage::_sample_text() const
{
    // What became of the picked pop in the sample, for the panel on the right.
    if (!chart->sample || chart->sample->headline.isEmpty())
        return QString();
    const Sample &s = *chart->sample;
    QString warns;
    for (const auto &[t, kind] : s.notes)
        if (kind == u"warn")
            warns += u"<br><span style='color:%1;'>%2</span>"_s.arg(AMBER, t);
    return u"<br><span style='color:%1; font-weight:700;'>Sample province: %2</span>%3"_s.arg(theme::GOLD, s.headline,
                                                                                               warns);
}

// ------------------------------------------------------------------ the sample: made up or a real place
double PopInfluencePage::_tot() const
{
    long long s = 0;
    for (const auto &[_k, v] : _start)
        s += v;
    return s ? static_cast<double>(s) : 1.0;
}

void PopInfluencePage::_set_source(const QString &k)
{
    src = k;
    for (const auto &[kk, b] : src_btn)
        b->setChecked(kk == k);
    pmap->setVisible(k != u"made");
    if (k == u"made") {
        place.reset();
        pmap->picked.clear();
        _start = doc ? popsim::sample(doc, mod, set_order(types())) : OrderedMap<QString, long long>();
        place_lbl->setText(QString());
    } else {
        _paint_place_map();
        if (place && place->what != k) {             // the same spot, the other size
            const auto g = _place_group(place->pids[0]);
            pmap->picked = std::set<long long>(g.begin(), g.end());
            _place_picked();
            return;
        }
        if (!place)
            place_lbl->setText(u"Click a %1 on the map below (wheel: zoom, right button: move the map)."_s.arg(
                k == u"province" ? u"province"_s : u"province — its state is taken"_s));
    }
    _show();
}

void PopInfluencePage::_paint_place_map()
{
    if (!md || !doc || !pmap->isVisible())
        return;
    document::Document *d = doc;
    pmap->set_map(&md->idmap,
                  regionimport::owner_lut(
                      mod, [d](long long p) -> const history::ProvState & { return d->prov_state(p); }, md->n),
                  true);
}

std::vector<long long> PopInfluencePage::_place_group(long long pid)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    if (!pr || pr->sea)
        return {};
    if (src != u"state" || !pr->region || pr->region->isEmpty() || !mod.regions.contains(*pr->region))
        return {pid};
    const auto own = doc->prov_state(pid).owner;
    std::vector<long long> out;
    for (long long p : mod.regions.get(*pr->region)->provinces)
        if (mod.provinces.contains(p) && doc->prov_state(p).owner == own)
            out.push_back(p);
    return out;
}

void PopInfluencePage::_place_picked()
{
    const std::vector<long long> pids(pmap->picked.begin(), pmap->picked.end());
    if (pids.empty() || !doc) {
        place.reset();
        _start = doc ? popsim::sample(doc, mod, set_order(types())) : OrderedMap<QString, long long>();
        place_lbl->setText(src != u"made" ? u"Nothing picked: the made-up province stands in."_s : QString());
    } else {
        place = std::make_unique<popplace::Place>(mod, *doc, pids, src);
        _take_place(true);
        if (pmap->_last == 0 && pmap->ids) {         // the whole world shown: zoom to the place
            const auto &cn = pmap->centres();
            std::vector<double> xs, ys;
            for (long long p : pids)
                if (p < static_cast<long long>(cn.cnt.size()) && cn.cnt[static_cast<size_t>(p)] != 0.0) {
                    xs.push_back(cn.cx[static_cast<size_t>(p)]);
                    ys.push_back(cn.cy[static_cast<size_t>(p)]);
                }
            if (!xs.empty())
                pmap->show_box(*std::min_element(xs.begin(), xs.end()) - 25, *std::min_element(ys.begin(), ys.end()) - 25,
                               *std::max_element(xs.begin(), xs.end()) + 25, *std::max_element(ys.begin(), ys.end()) + 25);
        }
    }
    _show();
}

void PopInfluencePage::_take_place(bool fill)
{
    // The picked place becomes the sample (and, when fill, sets the case: literacy, life rating …).
    popplace::Place &pl = *place;
    _gcases.clear();
    auto nm = [this](const QString &k) { return name(k); };
    if (!pl.total) {
        place_lbl->setText(u"<span style='color:%1'>⚠ Nobody lives in %2 at the start — the made-up province stands "
                           "in.</span>"_s.arg(AMBER, pl.label(nm)));
        place.reset();
        _start = popsim::sample(doc, mod, set_order(types()));
        return;
    }
    OrderedMap<QString, long long> start;
    for (const auto &[k, v] : pl.start)
        start.insert(k.first, start.value(k.first, 0) + v);
    _start = start;
    if (fill)
        pl.fill_case(cs);
    const auto grp = pl.groups();
    std::vector<popplace::Group> big;
    for (const auto &[g, _v] : grp)
        big.push_back(g);
    std::stable_sort(big.begin(), big.end(),
                     [&](const popplace::Group &a, const popplace::Group &b) { return grp.value(a) > grp.value(b); });
    QStringList parts;
    for (size_t i = 0; i < big.size() && i < 3; ++i)
        parts << u"%1 %2 %"_s.arg(pl.group_name(big[i], nm))
                     .arg(py::round_int(static_cast<double>(grp.value(big[i])) * 100.0 / static_cast<double>(pl.total)));
    place_lbl->setText(u"<b>%1</b> of <b>%2</b> — %3 people; %4%5"_s.arg(
        pl.label(nm), pl.owner ? name(*pl.owner) : u"nobody"_s, commas0(static_cast<double>(pl.total)),
        parts.join(u", "_s), big.size() > 3 ? u" …"_s : QString()));
}

void PopInfluencePage::_place_hover(int pid)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    if (pr && !pr->sea && doc) {
        long long n = 0;
        if (const auto *ps = doc->pops.get(pid))
            for (const moddata::Pop &p : *ps)
                n += p.size;
        pmap->setToolTip(u"#%1 %2 — %3 people"_s.arg(pid).arg(pr->name, commas0(static_cast<double>(n))));
    }
}

const popcase::Case &PopInfluencePage::_gcase(const popplace::Group &g)
{
    // The case as a group of the place sees it (its culture, religion …).
    auto it = _gcases.find(g);
    if (it == _gcases.end())
        it = _gcases.emplace(g, place->case_for(cs, g)).first;
    return it->second;
}

// ------------------------------------------------------------------ the sample province
std::pair<PopInfluencePage::NumFn, QHash<QString, popcase::Block *>> PopInfluencePage::_numbers_fn()
{
    // n(pop type, shares): the chances' numbers of "Every pop" as the case stands, read for that pop's class.
    QHash<QString, popcase::Block *> gl;
    for (const Chance &c : chances)
        if (c.pop == u"pop_types")
            gl.insert(c.head, &block(c));
    static const std::vector<std::pair<QString, QString>> keys = {
        {u"promotion"_s, u"promotion_chance"_s}, {u"demotion"_s, u"demotion_chance"_s},
        {u"migration"_s, u"migration_chance"_s}, {u"emigration"_s, u"emigration_chance"_s},
        {u"colonial"_s, u"colonialmigration_chance"_s}};
    static const std::vector<std::pair<QString, QString>> gkeys = [] {
        auto k = keys;
        k.push_back({u"assimilation"_s, u"assimilation_chance"_s});
        k.push_back({u"conversion"_s, u"conversion_chance"_s});
        return k;
    }();
    auto prep = std::make_shared<std::map<std::tuple<QString, QString, std::optional<popplace::Group>>,
                                          popcase::Block::Prepared>>();
    NumFn n = [this, gl, prep](const QString &t, const popcase::Shares &shares,
                               const std::optional<popplace::Group> &g) {
        const QString strata = _info.value(t).second;
        const popcase::Case &cse = g && place ? _gcase(*g) : cs;
        QHash<QString, double> out;
        for (const auto &[k, hd] : (g ? gkeys : keys)) {
            popcase::Block *b = gl.value(hd, nullptr);
            if (!b) {
                out.insert(k, 0.0);
                continue;
            }
            const auto key = std::make_tuple(hd, strata, g);
            auto it = prep->find(key);
            if (it == prep->end())
                it = prep->emplace(key, b->prepare(cse, strata)).first;
            out.insert(k, b->number(cse, strata, shares, &it->second));
        }
        return out;
    };
    return {n, gl};
}

PopInfluencePage::WgtFn PopInfluencePage::_weights_fn()
{
    // weights(pop type, shares): how strongly that pop is drawn to each pop it can become.
    auto by = std::make_shared<OrderedMap<QString, std::vector<std::pair<QString, popcase::Block *>>>>();
    for (const Chance &c : chances)
        if (c.head == u"promote_to")
            (*by)[c.pop].push_back({c.target, &block(c)});
    auto prep = std::make_shared<std::map<std::pair<const popcase::Block *, std::optional<popplace::Group>>,
                                          popcase::Block::Prepared>>();
    return [this, by, prep](const QString &t, const popcase::Shares &shares, const std::optional<popplace::Group> &g) {
        const QString strata = _info.value(t).second;
        const popcase::Case &cse = g && place ? _gcase(*g) : cs;
        popsim::Weights out;
        if (const auto *lst = by->get(t))
            for (const auto &[target, b] : *lst) {
                const auto key = std::make_pair(static_cast<const popcase::Block *>(b), g);
                auto it = prep->find(key);
                if (it == prep->end())
                    it = prep->emplace(key, b->prepare(cse, strata)).first;
                out.insert(target, b->number(cse, strata, shares, &it->second));
            }
        return out;
    };
}

static QString times_text(long long months)
{
    for (const auto &[m, t] : popsim::TIMES)
        if (m == months)
            return t;
    return QString();
}

Sample PopInfluencePage::_sample(const View &v)
{
    // What the middle shows: the sample province now and after the time picked, as the case stands.
    const QStringList tys = types();
    const QString hd = v.ch->head;
    const QString when = times_text(months);
    _gcases.clear();
    const auto [nfn, gl] = _numbers_fn();
    if (place)
        return _sample_place(v, nfn, when);
    if (hd == u"assimilation_chance" || hd == u"conversion_chance") {
        const bool a = hd == u"assimilation_chance";
        popcase::Block *b = gl.value(hd, nullptr);
        const double n = b ? b->number(cs, view_class, shares0()) : 0.0;
        const double scale = defs.value(a ? u"ASSIMILATION_SCALE"_s : u"CONVERSION_SCALE"_s, 0.0);
        const long long other = popsim::SIZE * 20 / 100;
        const double left = popsim::decay(static_cast<double>(other), n, scale, static_cast<double>(months));
        Sample s;
        s.rows = {{a ? u"Primary culture"_s : u"State religion"_s, Rgb{214, 170, 80},
                   static_cast<double>(popsim::SIZE - other), static_cast<double>(popsim::SIZE) - left},
                  {a ? u"A culture not accepted"_s : u"Another religion"_s, Rgb{120, 150, 200},
                   static_cast<double>(other), left}};
        s.head = u"Sample province after "_s + when;
        s.headline = u"%1 of %2 changed over"_s.arg(commas0(static_cast<double>(other) - left),
                                                     commas0(static_cast<double>(other)));
        s.notes = {{u"number %1 as the case stands"_s.arg(popfacts::num(n)), QString()},
                   {u"(80 % / 20 % to begin with — a stand-in)"_s, QString()}};
        return s;
    }
    const WgtFn wfn = _weights_fn();
    sample_now_limit.reset();
    std::optional<std::pair<QString, double>> fo;
    if (focus)
        fo = std::make_pair(focus->pop, focus->value);
    QHash<QString, QString> strata;
    for (const QString &t : tys)
        strata.insert(t, _info.value(t).second);
    OrderedMap<QString, double> start;
    for (const auto &[k, x] : _start)
        start.insert(k, static_cast<double>(x));
    const popsim::NFn nf = [nfn](const QString &t, const popsim::Shares &sh) {
        popsim::Numbers out;
        const auto m = nfn(t, sh, std::nullopt);
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            out.insert(it.key(), popsim::NVal(it.value()));
        return out;
    };
    const popsim::WFn wf = [wfn](const QString &t, const popsim::Shares &sh) { return wfn(t, sh, std::nullopt); };
    const popsim::Result res = popsim::run(start, strata, static_cast<double>(months), popsim::NArg(nf),
                                           popsim::WArg(wf), defs, _share(), fo);
    const auto &after = res.after;
    QStringList order = tys;
    std::stable_sort(order.begin(), order.end(),
                     [&](const QString &a, const QString &b) { return -_start.value(a, 0) < -_start.value(b, 0); });
    Sample s;
    for (const QString &t : order)
        if (_start.value(t, 0) || after.value(t, 0.0) >= 50)
            s.rows.push_back({name(t), _info.value(t).first, static_cast<double>(_start.value(t, 0)), after.value(t, 0.0)});
    std::optional<QString> x = pop && tys.contains(*pop) ? pop : std::nullopt;
    if (!x && !s.rows.empty()) {
        double best = -1.0;
        for (const QString &t : tys) {
            const double d = std::fabs(after.value(t, 0.0) - static_cast<double>(_start.value(t, 0)));
            if (!x || d > best) {
                x = t;
                best = d;
            }
        }
    }
    const double tot = static_cast<double>(popsim::SIZE);
    if (x) {
        const double a0 = static_cast<double>(_start.value(*x, 0)) * 100.0 / tot, a1 = after.value(*x, 0.0) * 100.0 / tot;
        s.headline = u"%1: %2 % → %3 %  (%4)"_s.arg(name(*x), py::fmt_f(a0, 1), py::fmt_f(a1, 1), pf(a1 - a0, 1));
        for (const auto &nt : _limit(*x, after, wfn))
            s.notes.push_back(nt);
        std::vector<std::pair<double, QString>> got, lost;
        for (const auto &[st, val] : res.moved) {
            if (st.second == *x && val >= 1)
                got.push_back({val, st.first});
            if (st.first == *x && val >= 1)
                lost.push_back({val, st.second});
        }
        std::sort(got.begin(), got.end(), std::greater<>());
        std::sort(lost.begin(), lost.end(), std::greater<>());
        QStringList gs, ls;
        for (size_t i = 0; i < got.size() && i < 4; ++i)
            gs << name(got[i].second) + u" "_s + commas0(got[i].first);
        for (size_t i = 0; i < lost.size() && i < 4; ++i)
            ls << name(lost[i].second) + u" "_s + commas0(lost[i].first);
        if (!got.empty())
            s.notes.push_back({u"came from: "_s + gs.join(u", "_s), QString()});
        if (!lost.empty())
            s.notes.push_back({u"went on to: "_s + ls.join(u", "_s), QString()});
    }
    if (res.left >= 1)
        s.notes.push_back({u"%1 moved away (%2 % of the province)"_s.arg(commas0(res.left), py::fmt_f(res.left * 100.0 / tot, 1)),
                           QString()});
    const popcase::Shares sh0 = shares0();
    QString big;
    long long bn = 0;
    for (const QString &t : tys)
        if (big.isEmpty() || _start.value(t, 0) > bn) {
            big = t;
            bn = _start.value(t, 0);
        }
    const auto n0 = nfn(big, sh0, std::nullopt);
    if (n0.value(u"promotion"_s) <= 0 && n0.value(u"demotion"_s) <= 0)
        s.notes.push_back({u"nobody is promoted or demoted: no card of “Every pop › Being promoted” holds in this case"_s,
                           u"warn"_s});
    else
        s.notes.push_back({u"%1: promotion number %2, demotion %3"_s.arg(name(big), popfacts::num(n0.value(u"promotion"_s)),
                                                                        popfacts::num(std::max(0.0, n0.value(u"demotion"_s)))),
                           QString()});
    if (focus)
        s.notes.push_back({u"national focus: %1 (%2)"_s.arg(name(focus->key), pg(focus->value)), QString()});
    s.head = u"Sample province after "_s + when;
    s.mark = x ? std::optional<QString>(name(*x)) : std::nullopt;
    return s;
}

Sample PopInfluencePage::_sample_place(const View &v, const NumFn &nfn, const QString &when)
{
    // The sample is a real place: its pops by culture and religion, each group reading the cards for itself,
    // assimilation and conversion running beside promotion (popsim.run_groups).
    popplace::Place &pl = *place;
    const QStringList tys = types();
    const QString hd = v.ch->head;
    auto nmf = [this](const QString &k) { return name(k); };
    OrderedMap<popsim::GKey, double> start;
    OrderedMap<popsim::GKey, long long> start_i;
    for (const auto &[k, x] : pl.start)
        if (_info.contains(k.first)) {
            start.insert(k, static_cast<double>(x));
            start_i.insert(k, x);
        }
    QHash<QString, QString> strata;
    for (const QString &t : tys)
        strata.insert(t, _info.value(t).second);
    const WgtFn wfn = _weights_fn();
    std::optional<std::pair<QString, double>> fo;
    if (focus)
        fo = std::make_pair(focus->pop, focus->value);
    popplace::Place *pp = &pl;
    const popsim::GResult res = popsim::run_groups(
        start, strata, static_cast<double>(months),
        [nfn](const QString &t, const popsim::Group &g, const popsim::Shares &sh) { return nfn(t, sh, g); },
        [wfn](const QString &t, const popsim::Group &g, const popsim::Shares &sh) { return wfn(t, sh, g); }, defs,
        _share(), fo, [pp](const popsim::Group &g) { return pp->turn(g); }, months <= 60 ? 1 : 3);
    long long ssum = 0;
    for (const auto &[_k, x] : start_i)
        ssum += x;
    const double tot = ssum ? static_cast<double>(ssum) : 1.0;
    OrderedMap<QString, double> after;
    for (const auto &[k, x] : res.after)
        after.insert(k.first, after.value(k.first, 0.0) + x);
    sample_now_limit.reset();
    const QString plc = pl.label(nmf);
    Sample s;
    if (hd == u"assimilation_chance" || hd == u"conversion_chance") {
        OrderedMap<popplace::Group, long long> g0;
        OrderedMap<popplace::Group, double> g1;
        for (const auto &[k, x] : start_i)
            g0.insert(k.second, g0.value(k.second, 0) + x);
        for (const auto &[k, x] : res.after)
            g1.insert(k.second, g1.value(k.second, 0.0) + x);
        std::vector<popplace::Group> k0, k1;
        for (const auto &[g, _x] : g0)
            k0.push_back(g);
        for (const auto &[g, _x] : g1)
            k1.push_back(g);
        grid::PySet<popplace::Group> both = grid::PySet<popplace::Group>::copy_of(grid::PySet<popplace::Group>::from_keys(k0));
        both.merge(grid::PySet<popplace::Group>::from_keys(k1));
        std::vector<popplace::Group> order = both.items();
        std::stable_sort(order.begin(), order.end(), [&](const popplace::Group &a, const popplace::Group &b) {
            return -g0.value(a, 0) < -g0.value(b, 0);
        });
        static const std::vector<Rgb> palette = {{214, 170, 80},  {120, 150, 200}, {170, 110, 190},
                                                 {110, 180, 140}, {200, 120, 110}, {150, 150, 150}};
        for (size_t i = 0; i < order.size(); ++i) {
            const popplace::Group &g = order[i];
            if (g0.value(g, 0) || g1.value(g, 0.0) >= 50)
                s.rows.push_back({pl.group_name(g, nmf), palette[std::min(i, palette.size() - 1)],
                                  static_cast<double>(g0.value(g, 0)), g1.value(g, 0.0)});
        }
        const bool a = hd == u"assimilation_chance";
        py::Sum mv;
        for (const auto &[gg, x] : res.changed)
            if ((gg.first.first != gg.second.first) == a)
                mv.add(x);
        const double moved_n = mv.value();
        const popcase::Shares sh0 = shares0();
        for (size_t i = 0; i < order.size() && i < 4; ++i) {
            const popplace::Group &g = order[i];
            const auto [k, srel] = pl.kind(g);
            if ((a && k == u"other") || (!a && k != u"other" && !srel)) {
                QString t0 = tys.value(0);
                for (const auto &[sk, _x] : start_i)
                    if (sk.second == g) {
                        t0 = sk.first;
                        break;
                    }
                const auto nn = nfn(t0, sh0, g);
                s.notes.push_back({u"%1: number %2"_s.arg(pl.group_name(g, nmf),
                                                          popfacts::num(nn.value(a ? u"assimilation"_s : u"conversion"_s, 0.0))),
                                   QString()});
            }
        }
        bool any_other = false;
        for (const auto &[g, _x] : g0)
            any_other = any_other || pl.kind(g).first == u"other";
        if (!any_other && a)
            s.notes.push_back({u"every pop here is of an accepted culture: nobody assimilates (measured)"_s, u"warn"_s});
        s.head = u"%1 after %2"_s.arg(plc, when);
        s.headline = u"%1 people %2"_s.arg(commas0(moved_n), a ? u"took on the primary culture"_s
                                                              : u"took on the state religion"_s);
        return s;
    }
    QStringList order = tys;
    std::stable_sort(order.begin(), order.end(),
                     [&](const QString &a, const QString &b) { return -_start.value(a, 0) < -_start.value(b, 0); });
    for (const QString &t : order)
        if (_start.value(t, 0) || after.value(t, 0.0) >= 50)
            s.rows.push_back({name(t), _info.value(t).first, static_cast<double>(_start.value(t, 0)), after.value(t, 0.0)});
    std::optional<QString> x = pop && tys.contains(*pop) ? pop : std::nullopt;
    if (!x && !s.rows.empty()) {
        double best = -1.0;
        for (const QString &t : tys) {
            const double d = std::fabs(after.value(t, 0.0) - static_cast<double>(_start.value(t, 0)));
            if (!x || d > best) {
                x = t;
                best = d;
            }
        }
    }
    if (x) {
        const double a0 = static_cast<double>(_start.value(*x, 0)) * 100.0 / tot, a1 = after.value(*x, 0.0) * 100.0 / tot;
        s.headline = u"%1: %2 % → %3 %  (%4)"_s.arg(name(*x), py::fmt_f(a0, 1), py::fmt_f(a1, 1), pf(a1 - a0, 1));
        for (const auto &nt : _limit(*x, after, wfn))
            s.notes.push_back(nt);
        OrderedMap<popplace::Group, std::pair<double, double>> by_g;   // the picked pop, culture by culture
        for (const auto &[k, val] : start_i)
            if (k.first == *x)
                by_g[k.second].first += static_cast<double>(val);
        for (const auto &[k, val] : res.after)
            if (k.first == *x)
                by_g[k.second].second += val;
        std::vector<popplace::Group> gs;
        for (const auto &[g, _p] : by_g)
            gs.push_back(g);
        std::stable_sort(gs.begin(), gs.end(), [&](const popplace::Group &a, const popplace::Group &b) {
            const auto pa = by_g.value(a), pb = by_g.value(b);
            return -std::max(pa.first, pa.second) < -std::max(pb.first, pb.second);
        });
        for (size_t i = 0; i < gs.size() && i < 4; ++i) {
            const auto [b, a] = by_g.value(gs[i]);
            s.notes.push_back({u"%1: %2 → %3"_s.arg(pl.group_name(gs[i], nmf), commas0(b), commas0(a)), QString()});
        }
        std::vector<std::pair<double, QString>> got;
        for (const auto &[st, val] : res.moved)
            if (st.second == *x && val >= 1)
                got.push_back({val, st.first});
        std::sort(got.begin(), got.end(), std::greater<>());
        QStringList gl;
        for (size_t i = 0; i < got.size() && i < 4; ++i)
            gl << name(got[i].second) + u" "_s + commas0(got[i].first);
        if (!got.empty())
            s.notes.push_back({u"came from: "_s + gl.join(u", "_s), QString()});
    }
    if (res.left >= 1)
        s.notes.push_back(
            {u"%1 moved away (%2 % of the place)"_s.arg(commas0(res.left), py::fmt_f(res.left * 100.0 / tot, 1)), QString()});
    py::Sum turned;
    for (const auto &[_g, val] : res.changed)
        turned.add(val);
    if (turned.value() >= 1)
        s.notes.push_back({u"%1 took on the primary culture or the state religion"_s.arg(commas0(turned.value())), QString()});
    s.notes.push_back({u"the place's own pops, each culture reading the cards for itself; who moves IN is not simulated"_s,
                       QString()});
    s.head = u"%1 after %2"_s.arg(plc, when);
    s.mark = x ? std::optional<QString>(name(*x)) : std::nullopt;
    return s;
}

std::vector<std::pair<QString, QString>> PopInfluencePage::_limit(const QString &x,
                                                                 const OrderedMap<QString, double> &after,
                                                                 const WgtFn &wfn)
{
    // How far the picked pop can grow as the case stands: the share of the province at which no pop is drawn to it
    // any more — its weight is 0 or below (a national focus on it: its value on top). The card that stops it is named
    // when it is a hard stop ( soldiers = 0.05, factor -100 ). -> [(note, kind)]
    std::vector<const Chance *> blocks;
    for (const Chance *c : into_blocks(x))
        if (_start.value(c->pop, 0) > 0)
            blocks.push_back(c);
    if (blocks.empty())
        return {};
    const popcase::Shares sh = shares0();
    const double r0 = 1.0 - sh.value(x, 0.0);
    const double rest = r0 != 0.0 ? r0 : 1.0;
    const double add = focus && focus->pop == x ? focus->value : 0.0;
    std::optional<std::pair<double, popcase::Shares>> top;
    for (int k = 0; k <= 400; ++k) {                 // in steps of 0.1 % up to 40 % of the province
        const double s = k / 1000.0;
        popcase::Shares shares;
        for (auto it = sh.cbegin(); it != sh.cend(); ++it)
            shares.insert(it.key(), it.value() * (1.0 - s) / rest);
        shares.insert(x, s);
        bool any = false;
        for (const Chance *c : blocks)
            if (wfn(c->pop, shares, std::nullopt).value(x, 0.0) + add > 0) {
                any = true;
                break;
            }
        if (!any) {
            top = std::make_pair(s, shares);
            break;
        }
    }
    if (!top)
        return {};
    const auto &[s, shares] = *top;
    std::optional<std::pair<double, QString>> worst;
    for (const Chance *c : blocks)
        for (const Card &m : c->mods) {
            const double f = fnum(m.factor);
            if (f <= -10 && cs.holds(tree(m), _info.value(c->pop).second, shares) && (!worst || f < worst->first))
                worst = std::make_pair(f, m.cond);
        }
    const QString nm = name(x);
    const double now = sh.value(x, 0.0), end = after.value(x, 0.0) / _tot();
    sample_now_limit = s;
    if (s <= 0)
        return {{u"Nobody becomes %1 in this case, however few there are: the cards that lower it outweigh the ones "
                 "that raise it"_s.arg(nm),
                 u"warn"_s}};
    if (s <= now + 1e-9)
        return {{u"In this case %1 stop growing at %2 % of the province — the sample has %3 % already. Raise what the "
                 "green cards ask for."_s.arg(nm, py::fmt_f(s * 100, 1), py::fmt_f(now * 100, 1)),
                 u"warn"_s}};
    const QString why = worst ? u": there the card “%1” (%2) holds"_s.arg(words(worst->second), pg(worst->first))
                              : u": there the cards that lower it outweigh the ones that raise it"_s;
    if (end >= s - 0.0015)
        return {{u"%1 have reached their limit for this case, %2 % of the province%3"_s.arg(nm, py::fmt_f(s * 100, 1), why),
                 u"warn"_s}};
    return {{u"In this case %1 grow until they are %2 % of the province%3"_s.arg(nm, py::fmt_f(s * 100, 1), why),
             QString()}};
}

std::shared_ptr<const script::Nodes> PopInfluencePage::_parsed(const QString &rel) const
{
    const QString text = store->text(rel);
    const auto it = _parse_memo.find(rel);
    if (it != _parse_memo.end() && it->second.first == text)
        return it->second.second;
    auto ns = std::make_shared<const script::Nodes>(script::parse(text));
    _parse_memo[rel] = {text, ns};
    return ns;
}

QHash<QString, std::pair<Rgb, QString>> PopInfluencePage::_type_info(const QStringList &tys) const{
    // {pop type: (its colour, its class)} from the pop files.
    QHash<QString, std::pair<Rgb, QString>> out;
    for (const QString &t : tys) {
        Rgb rgb{150, 150, 150};
        QString strata = u"poor"_s;
        try {
            const std::shared_ptr<const script::Nodes> nsp = _parsed(u"poptypes/%1.txt"_s.arg(t));
            for (const script::Node &nd : *nsp) {
                if (nd.has_key && nd.key == u"color" && nd.is_block) {
                    std::vector<int> v;
                    for (const script::Node &x : nd.kids) {
                        const QString txt = !x.has_key || x.key.isEmpty() ? x.value : x.key;
                        const std::optional<double> f = py::to_float(txt);
                        if (!f)
                            throw std::runtime_error("float");
                        v.push_back(static_cast<int>(py::trunc_int(*f)));
                    }
                    if (v.size() >= 3)
                        rgb = Rgb{v[0], v[1], v[2]};
                } else if (nd.has_key && nd.key == u"strata" && !nd.is_block) {
                    strata = py::lower(py::strip(nd.value));
                }
            }
        } catch (...) {
        }
        out.insert(t, {rgb, strata});
    }
    return out;
}

// ------------------------------------------------------------------ the numbers
double PopInfluencePage::_share() const
{
    const std::optional<double> v = _num(QString(share_edit->text()).replace(u","_s, u"."_s));
    return v ? std::max(0.0, std::min(100.0, *v)) / 100.0 : popfacts::share(defs);
}

QString PopInfluencePage::_card_rate(const Card &m)
{
    const std::optional<View> cv = cur();
    const std::optional<double> v = _num(m.factor);
    if (!cv || !v || *v == 0.0 || !popfacts::SCALES.contains(cv->ch->head))
        return QString();
    double base = 1.0;
    if (!cv->ch->base.isEmpty()) {
        const std::optional<double> b = _num(cv->ch->base);
        base = b ? *b : 0.0;                         // ((base or 0) — None counts 0)
    }
    return popfacts::say(cv->ch->head, base * *v, size, defs, _share(), true);
}

QString PopInfluencePage::_card_warn(const Card &m)
{
    // What is odd about a card: it waits for a flag nothing in the mod sets.
    const auto pa = popcase::parts(tree(m));        // (kept: a range-for over a part of a temporary reads freed memory)
    for (const QString &f : std::get<1>(pa))
        if (!fact_note(f).isEmpty())
            return u"⚠ nothing sets this flag"_s;
    return QString();
}

void PopInfluencePage::_size_done()
{
    const QString t = QString(size_edit->text()).remove(u',').remove(u'.').remove(u' ');
    if (py::isdigit(t)) {
        try {
            const long long n = py::to_int(t);
            if (n > 0)
                size = n;
        } catch (...) {
        }
    }
    size_edit->setText(QString::number(size));
    _show();
}

void PopInfluencePage::_share_done()
{
    share_edit->setText(py::fmt_g(py::round_to(_share() * 100, 1)));
    _show();
}

// ------------------------------------------------------------------ the case, set by clicks on the cards
void PopInfluencePage::_card_clicked(long long i)
{
    // A card's tick box: the case is changed so that the card holds (or no longer does).
    const std::optional<View> cv = cur();
    if (!cv || !(0 <= i && i < static_cast<long long>(cv->ch->mods.size())))
        return;
    const popcase::Tree t = tree(cv->ch->mods[static_cast<size_t>(i)]);
    const QString strata = strata_of(cv);
    const popcase::Shares shares = shares0();
    const bool now = cs.holds(t, strata, shares);
    const auto vals = cs.vals;
    const auto fs = cs.facts;
    if (cs.make(t, !now, strata, shares)) {
        _say(QString());
    } else {
        cs.vals = vals;                              // (it could not be switched: the case stays as it was)
        cs.facts = fs;
        _say(u"The case can't switch this card: it follows the sample province (how many of a pop there are — it "
             "switches by itself as the sample runs) or the pop's class, or it holds whatever the setting is (at least "
             "0)."_s);
    }
    _show();
}

void PopInfluencePage::_ladder(const std::vector<long long> &cards, const std::vector<long long> &hold)
{
    // A step of a step group was picked (hold: its cards that should hold; none: the group is cleared).
    const std::optional<View> cv = cur();
    const Ladder *L = nullptr;
    for (const Ladder &x : chart->_lads)
        if (x.idx == cards) {
            L = &x;
            break;
        }
    if (!cv || !L)
        return;
    const Ladder lad = *L;                           // (the chart's list is laid out anew on _show)
    const Chance &ch = *cv->ch;
    const QString strata = strata_of(cv);
    const popcase::Shares shares = shares0();
    const QString key = popcase::ALIAS.value(lad.key, lad.key);
    _say(QString());
    if (popcase::VARS.contains(key) && !(lad.tmpl && !lad.tmpl->isEmpty())) {   // (steps inside a scope: card by card)
        if (!hold.empty()) {
            const qsizetype j = index_of(lad.idx, lad.neg ? hold.front() : hold.back());
            const double th = (*lad.thr)[static_cast<size_t>(j)];
            cs.vals[key] = lad.neg ? cs.below(key, th) : th;
            cs.make(tree(ch.mods[static_cast<size_t>(lad.idx[static_cast<size_t>(j)])]), true, strata, shares);
        } else if (!lad.rest.isEmpty()) {            // cleared: what the steps share is switched off, the setting stays
            for (long long i : cards) {
                const popcase::Tree t = tree(ch.mods[static_cast<size_t>(i)]);
                if (cs.holds(t, strata, shares))
                    cs.make(t, false, strata, shares);
            }
        } else {
            cs.vals[key] = lad.neg ? lad.thr->back() : cs.below(key, lad.thr->front());
        }
    } else if (_tset.contains(lad.key)) {
        _say(u"These steps follow the sample province: %1 are %2 % of it at the start, and the steps switch as that "
             "changes — they can't be set by hand."_s.arg(name(lad.key), py::fmt_f(shares.value(lad.key, 0.0) * 100, 1)));
    } else {
        for (long long i : cards)
            cs.make(tree(ch.mods[static_cast<size_t>(i)]), std::find(hold.begin(), hold.end(), i) != hold.end(), strata,
                    shares);
    }
    _show();
}

void PopInfluencePage::_focus_tier(long long j)
{
    const std::vector<Focus> tr = tiers();
    if (0 <= j && j < static_cast<long long>(tr.size()))
        focus = tr[static_cast<size_t>(j)];
    else
        focus.reset();
    _show();
}

void PopInfluencePage::_focus_set(const std::optional<Focus> &f)
{
    // The national focus picked in the case panel.
    focus = f;
    _show();
}

void PopInfluencePage::_focus_picked(int)
{
    const QVariant d = focus_box->currentData();
    const std::vector<Focus> tr = tiers();
    if (d.isValid() && d.toLongLong() >= 0 && d.toLongLong() < static_cast<long long>(tr.size()))
        focus = tr[static_cast<size_t>(d.toLongLong())];
    else
        focus.reset();
    _show();
}

void PopInfluencePage::_set_months(long long m)
{
    months = m;
    for (const auto &[k, b] : time_btn)
        b->setChecked(k == m);
    _show();
}

QStringList PopInfluencePage::_cond_lines(const View &v, long long i) const
{
    const auto [c, k] = at_of(v, i)[0];
    const Card &m = c->mods[k];
    const QString text = store->text(c->rel);
    const QString inner = py::slice(text, m.node.first + 1, m.node.second - 1);
    QStringList out;
    for (const script::Node &n : script::parse(inner))
        if (n.has_key && !n.key.isEmpty() && n.key != u"factor")
            out << py::split(inner.mid(n.kstart, n.vend - n.kstart)).join(u' ');
    return out;
}

// ------------------------------------------------------------------ picking
void PopInfluencePage::_set_pop(const QString &pk)
{
    pop = pk;
    sel.reset();
    source.reset();
    if (!into_blocks(pk).empty())
        dir = u"in"_s;
    _show();
}

void PopInfluencePage::_set_head(const QString &h)
{
    head = h;
    sel.reset();
    _show();
}

void PopInfluencePage::_set_dir(const QString &k)
{
    dir = k;
    sel.reset();
    _show();
}

void PopInfluencePage::_set_class(const QString &k)
{
    view_class = k;
    _show();
}

void PopInfluencePage::_set_target(const std::optional<QString> &t)
{
    if (head && *head == u"promote_to" && dir == u"in" && !(pop && *pop == u"pop_types")) {
        source = t;
        sel.reset();
    } else {
        target = t.value_or(QString());
        sel.reset();
    }
    _show();
}

void PopInfluencePage::_pick(long long i)
{
    sel = i;
    _show();
}

void PopInfluencePage::show_place(const QString &p, const QString &h, const QString &t, const QString &way,
                                  const std::optional<QString> &s)
{
    // Jump to a view. way "in": who becomes `pop` (source: from one pop only); "out": `pop` becoming `target`.
    pop = p;
    head = h;
    target = t;
    sel.reset();
    dir = way;
    source = s;
    if (doc)
        refresh(doc);
}

// ------------------------------------------------------------------ changes (into the pop files)
void PopInfluencePage::_say(const QString &text)
{
    msg->setText(text);
    msg->setVisible(!text.isEmpty());
}

QString PopInfluencePage::_label(const View &v) const
{
    const Chance &ch = *v.ch;
    if (v.merged)
        return u"Becoming %1 (%2 pop files)"_s.arg(name(ch.target)).arg(v.blocks.size());
    const QString who = ch.pop == u"pop_types" ? u"Every pop"_s : name(ch.pop);
    const auto *pt = per_target(ch.head);
    const QString *sg = single(ch.head);
    return u"%1, %2"_s.arg(who, pt ? QString(pt->second).replace(u"%s"_s, name(ch.target))
                                   : py::lower(sg ? *sg : _pretty(ch.head)));
}

std::optional<QString> PopInfluencePage::_number(const QString &s) const
{
    static const QRegularExpression re(uR"(^-?\d+(\.\d+)?$)"_s, UNI);
    QString v = py::strip(s).replace(u","_s, u"."_s);
    QString t = v;
    if (t.endsWith(u'\n'))
        t.chop(1);
    return re.match(t).hasMatch() ? std::optional<QString>(v) : std::nullopt;
}

void PopInfluencePage::_emit(const QString &label,
                             const std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> &edits)
{
    // edits: [(file, from, to, new text)] against the files as they are — one step, however many files.
    OrderedMap<QString, std::vector<std::tuple<qsizetype, qsizetype, QString>>> by;
    for (const auto &[rel, a, b, nw] : edits)
        by[rel].push_back({a, b, nw});
    OrderedMap<QString, QString> out;
    for (const auto &[rel, lst0] : by) {
        QString t = store->text(rel);
        auto lst = lst0;
        std::sort(lst.begin(), lst.end(), std::greater<>());
        for (const auto &[a, b, nw] : lst)
            t = py::head(t, a) + nw + py::tail(t, b);
        out.insert(rel, t);
    }
    if (out.size() == 1)
        Q_EMIT scriptEdit(label, out.begin()->first, out.begin()->second);
    else if (!out.isEmpty())
        Q_EMIT scriptsEdit(out, label);
}

void PopInfluencePage::_base_done()
{
    const std::optional<View> cv = cur();
    const std::optional<QString> v = _number(base_edit->text());
    if (!cv || !cv->ch->bspan || py::strip(base_edit->text()) == cv->ch->base)
        return;
    if (!v) {
        _say(u"A number, like 0.01."_s);
        return;
    }
    _say(QString());
    base_edit->setModified(false);
    std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> edits;
    for (const Chance *c : cv->blocks)
        if (c->bspan)
            edits.push_back({c->rel, c->bspan->first, c->bspan->second, *v});
    _emit(u"%1: own factor %2"_s.arg(_label(*cv), *v), edits);
}

void PopInfluencePage::_factor_done()
{
    const std::optional<View> cv = cur();
    if (!cv || !sel || *sel < 0)
        return;
    const Card &m = cv->ch->mods[static_cast<size_t>(*sel)];
    const std::optional<QString> v = _number(f_edit->text());
    if (!m.fspan || py::strip(f_edit->text()) == m.factor)
        return;
    if (!v) {
        _say(u"A factor is a number, like 0.5 or -1."_s);
        return;
    }
    _say(QString());
    const auto at = at_of(*cv, *sel);
    sel.reset();                                     // (its side and place may change with the number)
    std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> edits;
    for (const auto &[c, k] : at)
        if (c->mods[k].fspan)
            edits.push_back({c->rel, c->mods[k].fspan->first, c->mods[k].fspan->second, *v});
    _emit(u"%1: factor %2"_s.arg(_label(*cv), *v), edits);
}

QString PopInfluencePage::_indent(const QString &text, qsizetype at) const
{
    const qsizetype a = py::rfind(text, u'\n', at) + 1;
    const QString s = py::slice(text, a, at);
    qsizetype n = 0;
    while (n < s.size() && (s[n] == u' ' || s[n] == u'\t'))
        ++n;
    return s.left(n);
}

void PopInfluencePage::_cond_done()
{
    const std::optional<View> cv = cur();
    if (!cv || !sel || *sel < 0)
        return;
    const QString cond = c_edit->toPlainText();
    QStringList ls;
    for (const QString &ln : py::splitlines(cond))
        if (!py::strip(ln).isEmpty())
            ls << py::strip(ln);
    if (ls == _cond_lines(*cv, *sel))
        return;
    const std::optional<QString> why = cond_ok(cond);
    if (why) {
        _say(*why);
        return;
    }
    _say(QString());
    std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> edits;
    for (const auto &[c, k] : at_of(*cv, *sel)) {
        const Card &m = c->mods[k];
        const QString t = store->text(c->rel);
        const auto [a, b] = m.span;
        const qsizetype f = t.indexOf(u"modifier"_s, a);
        const QString ind = _indent(t, f >= 0 ? f : a);
        edits.push_back({c->rel, a, b, modifier_text(m.factor, cond, ind)});
    }
    _emit(u"%1: conditions of a card"_s.arg(_label(*cv)), edits);
}

void PopInfluencePage::_remove()
{
    const std::optional<View> cv = cur();
    if (!cv || !sel || *sel < 0)
        return;
    if (!_armed) {
        _armed = true;
        const size_t n = at_of(*cv, *sel).size();
        del_btn->setText(u"Click again: it goes%1"_s.arg(n > 1 ? u" from all %1 files"_s.arg(n) : QString()));
        del_btn->setStyleSheet(u"QPushButton { color: #ff786e; font-weight: 700; border: 2px solid #ff786e; }"_s);
        return;
    }
    const auto at = at_of(*cv, *sel);
    sel.reset();
    std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> edits;
    for (const auto &[c, k] : at)
        edits.push_back({c->rel, c->mods[k].span.first, c->mods[k].span.second, QString()});
    _emit(u"%1: a card taken out"_s.arg(_label(*cv)), edits);
}

void PopInfluencePage::_quick(int)
{
    const QVariant line = quick->currentData();
    quick->setCurrentIndex(0);
    if (line.isValid() && !line.toString().isEmpty()) {
        const QString now = py::rstrip(a_cond->toPlainText());
        a_cond->setPlainText((!now.isEmpty() ? now + u"\n"_s : QString()) + line.toString());
    }
}

void PopInfluencePage::_add()
{
    const std::optional<View> cv = cur();
    const std::optional<QString> v = _number(a_factor->text());
    const QString cond = a_cond->toPlainText();
    if (!cv)
        return;
    const std::optional<QString> why = !v ? std::optional<QString>(u"A factor is a number, like 0.5 or -1."_s) : cond_ok(cond);
    if (why) {
        _say(*why);
        return;
    }
    _say(QString());
    std::vector<std::tuple<QString, qsizetype, qsizetype, QString>> edits;
    for (const Chance *c : cv->blocks) {
        const QString t = store->text(c->rel);
        const qsizetype end = c->span.second;        // just before the block's closing }
        const qsizetype a = py::rfind(t, u'\n', end) + 1;
        const bool blank = py::strip(py::slice(t, a, end)).isEmpty();
        const QString ind = blank ? _indent(t, end) : QString();
        QString nw = modifier_text(*v, cond, ind + u"\t"_s);
        const qsizetype at = blank ? a : end;
        if (at == end)
            nw = u"\n"_s + nw;
        edits.push_back({c->rel, at, at, nw});
    }
    a_factor->clear();
    a_cond->clear();
    _emit(u"%1: a new card (%2)"_s.arg(_label(*cv), *v), edits);
}

}  // namespace popinfluence
