// maplook.py, its window half: LookPopup (and _Bar, _clear).
#include "ui/maplook.h"

#include "core/pynum.h"
#include "ui/colorwheel.h"
#include "ui/drawtext.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

using namespace Qt::StringLiterals;

namespace maplook {

namespace {
QString hex(const Rgb &c)
{
    return QString::asprintf("#%02x%02x%02x", std::get<0>(c), std::get<1>(c), std::get<2>(c));
}
Rgb rgb_of(const QString &c)
{
    const QColor q(c);
    return {q.red(), q.green(), q.blue()};
}
double pymin(double a, double b) { return b < a ? b : a; }   // (Python's min / max: the first unless the other wins)
double pymax(double a, double b) { return b > a ? b : a; }
}  // namespace

_Bar::_Bar(std::function<const Stops &()> stops_, QWidget *parent) : QWidget(parent), stops(std::move(stops_))
{
    setFixedHeight(34);
}

void _Bar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    QLinearGradient g(r.left(), 0, r.right(), 0);
    const Stops st = stops();
    for (const auto &[pos, rgb] : st)
        g.setColorAt(pos, QColor(std::get<0>(rgb), std::get<1>(rgb), std::get<2>(rgb)));
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(g);
    p.drawRoundedRect(r, 4, 4);
    p.setPen(QPen(QColor(10, 10, 10), 3));
    for (size_t i = 1; i + 1 < st.size(); ++i) {
        const double x = r.left() + st[i].first * r.width();
        p.drawLine(static_cast<int>(x), static_cast<int>(r.top()), static_cast<int>(x), static_cast<int>(r.bottom()));
    }
}

static void _clear(QLayout *lay)
{
    while (lay->count()) {
        QLayoutItem *it = lay->takeAt(0);
        if (it->layout()) {
            _clear(it->layout());
        } else if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;                                // (Python drops the taken item: a layout with it)
    }
}

LookPopup::LookPopup(const QString &mode_, QWidget *parent, std::function<std::vector<double>()> values_)
    : QFrame(parent, Qt::Popup), mode(mode_), values(std::move(values_))
{
    setObjectName(u"lookPopup"_s);
    setStyleSheet(u"QFrame#lookPopup { background: %1; border: 2px solid %2; }"_s.arg(theme::PANEL, theme::GOLD_DIM));
    setFixedWidth(540);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(8);
    msg = widgets::label(u""_s, u"sub"_s);
    msg->setStyleSheet(u"color: #ff786e;"_s);
    msg->hide();
    size_mode = mode == u"statesize" || mode == u"provsize";
    const Info *i = info(mode);
    if (size_mode) {
        _build_size(lay);
    } else if (i) {
        lay->addWidget(widgets::label(u"Colours of the map mode “%1”"_s.arg(i->name), u"label"_s));
        lay->addWidget(widgets::label(u"Counted in: %1. Smooth: low at the left, high at the right. Ranges: what lies "
                                      u"in a range gets its colour, the rest stays grey."_s.arg(i->unit),
                                      u"faint"_s));
        auto *row = new QHBoxLayout();
        row->setSpacing(0);
        for (const auto &[k, text] : std::vector<std::pair<QString, QString>>{{u"smooth"_s, u"Smooth scale"_s},
                                                                              {u"ranges"_s, u"Ranges"_s}}) {
            auto *b = new QPushButton(text);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            connect(b, &QPushButton::clicked, this, [this, kk = k] { _how(kk); });
            row->addWidget(b);
            how[k] = b;
        }
        row->addStretch(1);
        lay->addLayout(row);
        body = new QVBoxLayout();
        body->setSpacing(6);
        lay->addLayout(body);
    } else {
        lay->addWidget(widgets::label(u"The shared colour scale"_s, u"label"_s));
        lay->addWidget(widgets::label(u"This map mode has no amount of its own to colour (its colours come from the "
                                      u"mod or from what the provinces are). The shared scale below is used by every "
                                      u"map mode with an amount that has no colours of its own."_s,
                                      u"faint"_s));
        body = new QVBoxLayout();
        body->setSpacing(6);
        lay->addLayout(body);
    }
    lay->addWidget(msg);
    _fill();
}

// ------------------------------------------------------------------ what is edited
Scale &LookPopup::_s()
{
    if (!SCALES.contains(mode))
        SCALES.insert(mode, Scale{});
    return *SCALES.get(mode);
}

void LookPopup::_fill()
{
    _fill_body();
    // the popup follows its contents (smooth ↔ ranges, rows added / taken out) — once the layout has settled
    QTimer::singleShot(0, this, [this] { adjustSize(); });
}

void LookPopup::_fill_body()
{
    if (size_mode) {
        _fill_size();
        return;
    }
    _clear(body);
    if (!has_scale(mode)) {
        _smooth_editor([]() -> Stops & { return STOPS; }, true);
        return;
    }
    const Scale s = scale(mode);
    for (const auto &[k, b] : how)
        b->setChecked(s.how == k);
    if (s.how == u"ranges") {
        _ranges_editor();
        return;
    }
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    const bool has_own = s.own && !s.own->empty();
    for (const auto &[own, text] : std::vector<std::pair<bool, QString>>{{false, u"The shared scale"_s},
                                                                         {true, u"Its own scale"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(has_own == own);
        b->setToolTip(!own ? u"The shared scale: the same colours as every other map mode without colours of its own."_s
                           : u"Colours only this map mode uses."_s);
        connect(b, &QPushButton::clicked, this, [this, o = own] { _own(o); });
        row->addWidget(b);
    }
    row->addStretch(1);
    body->addLayout(row);
    if (has_own)
        _smooth_editor([this]() -> Stops & { return *_s().own; }, false);
    else
        _smooth_editor([]() -> Stops & { return STOPS; }, true);
}

void LookPopup::_own(bool on)
{
    Scale &s = _s();
    if (on)
        s.own = s.own && !s.own->empty() ? *s.own : STOPS;
    else
        s.own.reset();
    _fill();
    Q_EMIT changed();
}

void LookPopup::_how(const QString &how_)
{
    Scale &s = _s();
    s.how = how_;
    if (how_ == u"ranges" && !s.ranges) {
        const Info *i = info(mode);
        if (i && i->ranges && !i->ranges->empty())
            s.ranges = *i->ranges;
        else
            s.ranges = from_values(mode, values ? values() : std::vector<double>{});
    }
    _fill();
    Q_EMIT changed();
}

// ------------------------------------------------------------------ smooth
void LookPopup::_smooth_editor(std::function<Stops &()> target, bool shared)
{
    if (shared)
        body->addWidget(widgets::label(u"Shared by every map mode with an amount that has no colours of its own."_s,
                                       u"faint"_s));
    auto *bar = new _Bar([target]() -> const Stops & { return target(); });
    body->addWidget(bar);
    auto *host = new QWidget();
    auto *wr = new widgets::WrapRow(host, 6, 4);   // (4 a row: the popup gets the height of every row)
    for (const auto &[name, st] : PRESETS) {
        auto *b = new QPushButton(name);
        b->setObjectName(u"chip"_s);
        connect(b, &QPushButton::clicked, this, [this, target, ss = st] { _set_stops(target, ss); });
        wr->addWidget(b);
    }
    auto *rb = new QPushButton(u"⇄ Reverse"_s);
    rb->setObjectName(u"chip"_s);
    connect(rb, &QPushButton::clicked, this, [this, target] {
        Stops rev;
        for (const auto &[p, rgb] : target())
            rev.push_back({1.0 - p, rgb});
        _set_stops(target, rev);
    });
    wr->addWidget(rb);
    body->addWidget(host);
    const Stops stops = target();
    const int count = static_cast<int>(stops.size());
    for (int i = 0; i < count; ++i) {
        const auto &[pos, rgb] = stops[size_t(i)];
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        auto *cb = new colorwheel::ColorButton(hex(rgb));
        connect(cb, &colorwheel::ColorButton::changed, this,
                [this, target, i, bar](const QString &c) { _stop_colour(target, i, c, bar); });
        row->addWidget(cb);
        auto *ed = new QLineEdit(QString::number(py::round_int(pos * 100)));
        ed->setFixedWidth(60);
        ed->setAlignment(Qt::AlignRight);
        ed->setEnabled(0 < i && i < count - 1);
        ed->setToolTip(u"Where on the scale this colour stands, in per cent (the two ends are fixed)"_s);
        connect(ed, &QLineEdit::editingFinished, this, [this, target, i, ed] { _stop_place(target, i, ed); });
        row->addWidget(ed);
        row->addWidget(new QLabel(u"%"_s));
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setEnabled(count > 2 && 0 < i && i < count - 1);
        connect(x, &QPushButton::clicked, this, [this, target, i] {
            Stops s;
            const Stops &t = target();
            for (size_t k = 0; k < t.size(); ++k)
                if (int(k) != i)
                    s.push_back(t[k]);
            _set_stops(target, s);
        });
        row->addWidget(x);
        row->addStretch(1);
        body->addLayout(row);
    }
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"＋ A colour in between"_s);
    connect(b, &QPushButton::clicked, this, [this, target] { _add_stop(target); });
    row->addWidget(b);
    row->addStretch(1);
    body->addLayout(row);
}

void LookPopup::_set_stops(const std::function<Stops &()> &target, const Stops &stops)
{
    set_stops(stops, &target());
    _fill();
    Q_EMIT changed();
}

void LookPopup::_stop_colour(const std::function<Stops &()> &target, int n, const QString &c, _Bar *bar)
{
    Stops s = target();
    s[size_t(n)] = {s[size_t(n)].first, rgb_of(c)};
    set_stops(s, &target());
    bar->update();
    Q_EMIT changed();
}

void LookPopup::_stop_place(const std::function<Stops &()> &target, int n, QLineEdit *ed)
{
    const std::optional<double> f = py::to_float(ed->text().replace(u',', u'.'));
    if (!f) {
        _fill();
        return;
    }
    const double v = *f / 100.0;
    Stops s = target();
    s[size_t(n)] = {pymax(0.01, pymin(0.99, v)), s[size_t(n)].second};
    _set_stops(target, s);
}

void LookPopup::_add_stop(const std::function<Stops &()> &target)
{
    const Stops t = target();
    // max(gaps) of (gap, i): the widest gap, on a tie the later one
    size_t best = 0;
    double bg = t[1].first - t[0].first;
    for (size_t i = 1; i + 1 < t.size(); ++i) {
        const double g = t[i + 1].first - t[i].first;
        if (g > bg || g == bg) {
            bg = g;
            best = i;
        }
    }
    const double mid = (t[best].first + t[best + 1].first) / 2.0;
    Stops s = t;
    s.push_back({mid, _ramp(t, mid)});
    _set_stops(target, s);
}

// ------------------------------------------------------------------ ranges (a mode's)
void LookPopup::_ranges_editor()
{
    Scale &s = _s();
    const Info *i = info(mode);
    const int dec = i ? i->decimals : 0;
    _range_rows(body, [this]() -> Ranges & { return *_s().ranges; }, dec, [this] { _fill(); });
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"＋ A range"_s);
    b->setEnabled(s.ranges->size() < 12);
    connect(b, &QPushButton::clicked, this, [this] { _add_mode_range(); });
    row->addWidget(b);
    if (values) {
        auto *fb = new QPushButton(u"Ranges from the map"_s);
        fb->setToolTip(u"Four ranges with round limits, each about a quarter of what the map shows now"_s);
        connect(fb, &QPushButton::clicked, this, [this] { _reset_ranges(from_values(mode, values())); });
        row->addWidget(fb);
    }
    if (i && i->ranges && !i->ranges->empty()) {
        auto *sb = new QPushButton(u"↺ The suggested ranges"_s);
        connect(sb, &QPushButton::clicked, this, [this, rs = *i->ranges] { _reset_ranges(rs); });
        row->addWidget(sb);
    }
    row->addStretch(1);
    body->addLayout(row);
    body->addWidget(widgets::label(maplook::legend(mode), u"faint"_s));
}

void LookPopup::_reset_ranges(const Ranges &rs)
{
    _s().ranges = rs;
    _fill();
    Q_EMIT changed();
}

void LookPopup::_add_mode_range()
{
    Ranges &rs = *_s().ranges;
    const Info *i = info(mode);
    const int dec = i ? i->decimals : 0;
    const double step = dec ? std::pow(10.0, -double(dec)) : 1.0;
    const Range last = rs.empty() ? Range{0, 0.0, GREEN} : rs.back();
    const double lo = last.hi ? *last.hi + step : (last.lo > 0 ? last.lo * 2 + step : last.lo + 10);
    rs.push_back(Range{dec ? py::round_to(lo, dec) : static_cast<double>(py::trunc_int(lo)), std::nullopt,
                       Rgb{120, 150, 210}});
    _fill();
    Q_EMIT changed();
}

void LookPopup::_range_rows(QVBoxLayout *lay, const std::function<Ranges &()> &rg, int dec,
                            std::function<void()> refill)
{
    auto f = [dec](double v) {
        return dec ? py::fmt_f(v, dec) : py::fmt_commas(static_cast<double>(py::trunc_int(v)), 0);
    };
    range_edits.clear();
    const Ranges rs = rg();
    for (int i = 0; i < static_cast<int>(rs.size()); ++i) {
        const Range &r = rs[size_t(i)];
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        auto *cb = new colorwheel::ColorButton(hex(r.rgb));
        connect(cb, &colorwheel::ColorButton::changed, this,
                [this, rg, i](const QString &c) { _range_colour(rg, i, c); });
        row->addWidget(cb);
        row->addWidget(new QLabel(u"from"_s));
        auto *a = new QLineEdit(f(r.lo));
        a->setFixedWidth(110);
        a->setAlignment(Qt::AlignRight);
        row->addWidget(a);
        row->addWidget(new QLabel(u"to"_s));
        auto *b = new QLineEdit(r.hi ? f(*r.hi) : u""_s);
        b->setPlaceholderText(u"and above"_s);
        b->setFixedWidth(110);
        b->setAlignment(Qt::AlignRight);
        row->addWidget(b);
        for (QLineEdit *ed : {a, b})
            connect(ed, &QLineEdit::editingFinished, this,
                    [this, rg, i, a, b, dec, refill] { _range(rg, i, a, b, dec, refill); });
        range_edits.emplace_back(a, b);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        connect(x, &QPushButton::clicked, this, [this, rg, i, refill] { _range_del(rg, i, refill); });
        row->addWidget(x);
        row->addStretch(1);
        lay->addLayout(row);
    }
}

void LookPopup::_range(const std::function<Ranges &()> &rg, int n, QLineEdit *a, QLineEdit *b, int dec,
                       const std::function<void()> &refill)
{
    auto parse = [dec](const QString &t) -> std::optional<double> {
        if (dec)
            return num_dec(t);
        const auto v = num(t);
        return v ? std::optional<double>(double(*v)) : std::nullopt;
    };
    const bool b_typed = !b->text().trimmed().isEmpty();
    const std::optional<double> lo = parse(a->text());
    const std::optional<double> hi = b_typed ? parse(b->text()) : std::nullopt;
    if (!lo || (b_typed && !hi) || (hi && *hi < *lo)) {
        msg->setText(u"A range is two numbers, the second not below the first (or left empty for “and above”) — "
                     u"like 250,000 to 500,000%1."_s.arg(dec ? u" or 10.5 to 30"_s : u""_s));
        msg->show();
        return;
    }
    msg->hide();
    Ranges &rs = rg();
    if (n >= static_cast<int>(rs.size()))
        return;
    Range &r = rs[size_t(n)];
    if (*lo != r.lo || hi != r.hi) {
        r = Range{*lo, hi, r.rgb};
        refill();
        Q_EMIT changed();
    }
}

void LookPopup::_range_colour(const std::function<Ranges &()> &rg, int n, const QString &c)
{
    Range &r = rg()[size_t(n)];
    r.rgb = rgb_of(c);
    Q_EMIT changed();
}

void LookPopup::_range_del(const std::function<Ranges &()> &rg, int n, const std::function<void()> &refill)
{
    Ranges &rs = rg();
    rs.erase(rs.begin() + n);
    refill();
    Q_EMIT changed();
}

// ------------------------------------------------------------------ the size filters
void LookPopup::_build_size(QVBoxLayout *lay)
{
    lay->addWidget(widgets::label(u"Size filters — the map modes “State size” and “Province size”"_s, u"label"_s));
    lay->addWidget(widgets::label(u"Everything whose size lies in a range gets that range's colour; the rest stays "
                                  u"grey. “to” left empty: and everything above."_s,
                                  u"faint"_s));
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    kind = mode == u"provsize" ? u"prov_people"_s : state_kind();
    for (const auto &[k, text] : KINDS) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        connect(b, &QPushButton::clicked, this, [this, kk = k] { _by(kk); });
        row->addWidget(b);
        by.emplace_back(k, b);
    }
    row->addStretch(1);
    lay->addLayout(row);
    ranges = new QVBoxLayout();
    ranges->setSpacing(4);
    lay->addLayout(ranges);
    row = new QHBoxLayout();
    add_range = new QPushButton(u"＋ A range"_s);
    connect(add_range, &QPushButton::clicked, this, [this] { _add_range(); });
    row->addWidget(add_range);
    row->addStretch(1);
    lay->addLayout(row);
    legend = widgets::label(u""_s, u"faint"_s);
    lay->addWidget(legend);
}

void LookPopup::_fill_size()
{
    for (const auto &[k, b] : by)
        b->setChecked(kind == k);
    _clear(ranges);
    const QString kd = kind;
    _range_rows(ranges, [kd]() -> Ranges & { return SIZE.kinds[kd]; }, 0, [this] { _fill(); });
    add_range->setEnabled(SIZE.kinds[kind].size() < 12);
    legend->setText(size_legend(kind));
}

void LookPopup::_by(const QString &k)
{
    kind = k;
    if (k.startsWith(u"state_"_s))
        SIZE.by = k.section(u'_', 1);
    _fill();
    Q_EMIT changed();
}

void LookPopup::_add_range()
{
    Ranges &rs = SIZE.kinds[kind];
    const Range last = rs.empty() ? Range{0, 0.0, GREEN} : rs.back();
    const double lo = last.hi ? *last.hi + 1 : last.lo * 2 + 1;
    rs.push_back(Range{lo, std::nullopt, Rgb{120, 150, 210}});
    _fill();
    Q_EMIT changed();
}

}  // namespace maplook
