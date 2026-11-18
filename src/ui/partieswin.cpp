// partieswin.py — a nation's parties in a window of their own: the timeline and the party box beside it.
#include "ui/partieswin.h"
#include "ui/drawtext.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "ui/nationextras.h"
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace partieswin {

namespace {

const Rgb GREY{150, 150, 150};

QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

QString esc(const QString &s) { return s.toHtmlEscaped().replace(u'\'', u"&#x27;"_s); }   // html.escape

OptStr opt(const QVariant &d) { return d.isValid() ? OptStr(d.toString()) : std::nullopt; }

}  // namespace

double year_of(const OptStr &date, double fallback)
{
    try {
        const QStringList p = (date ? *date : u"None"_s).split(u'.');
        if (p.size() < 3)
            return fallback;
        return double(py::to_int(p[0])) + double(py::to_int(p[1]) - 1) / 12.0 + double(py::to_int(p[2]) - 1) / 365.0;
    } catch (const py::ValueError &) {
        return fallback;
    }
}

OrderedMap<QString, Rgb> ideology_colors(scriptstore::ScriptStore *store)
{
    OrderedMap<QString, Rgb> out;
    try {
        const script::Nodes nodes = script::parse(store->text(u"common/ideologies.txt"_s));
        for (const script::Node &g : nodes)
            if (g.is_block)
                for (const script::Node &i : g.kids)
                    if (i.has_key && !i.key.isEmpty() && i.is_block)
                        out.insert(i.key, script::color_of(i.kids).value_or(GREY));
    } catch (const std::exception &) {
    }
    return out;
}

// ------------------------------------------------------------------ the timeline

Timeline::Timeline(QWidget *parent) : QWidget(parent), loc([](const QString &k) { return k; })
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

Rgb Timeline::color(const QString &ideo) const { return colors.value(ideo, GREY); }

void Timeline::set_data(std::vector<Party> ps, OrderedMap<QString, Rgb> cols, QHash<QString, QString> nm,
                        std::pair<double, double> g)
{
    parties = std::move(ps);
    colors = std::move(cols);
    names = std::move(nm);
    game = g;
    relayout();
}

void Timeline::relayout()
{
    std::vector<size_t> shown;
    for (size_t i = 0; i < parties.size(); ++i)
        if (!filter || filter(parties[i]))
            shown.push_back(i);
    QStringList order;
    for (const auto &[k, _c] : colors)
        order << k;
    std::set<QString> rest;
    for (size_t i : shown)
        if (!colors.contains(parties[i].ideology))
            rest.insert(parties[i].ideology);
    for (const QString &k : rest)
        order << k;
    _lanes.clear();
    _heads.clear();
    int lane = 0;
    for (const QString &ideo : order) {
        std::vector<size_t> mine;
        for (size_t i : shown)
            if (parties[i].ideology == ideo)
                mine.push_back(i);
        std::stable_sort(mine.begin(), mine.end(), [&](size_t x, size_t y) {
            return std::make_pair(parties[x].a, parties[x].b) < std::make_pair(parties[y].a, parties[y].b);
        });
        if (mine.empty())
            continue;
        std::vector<double> ends;                  // the end year of each lane of this band
        for (size_t i : mine) {
            const Party &p = parties[i];
            int k = -1;
            for (size_t j = 0; j < ends.size(); ++j)
                if (ends[j] <= p.a + 0.01) {
                    k = int(j);
                    break;
                }
            if (k < 0) {
                ends.push_back(p.b);
                k = int(ends.size()) - 1;
            } else
                ends[size_t(k)] = p.b;
            _lanes.emplace_back(i, lane + k);
        }
        _heads.emplace_back(ideo, lane, int(ends.size()));
        lane += int(ends.size());
    }
    std::vector<double> ys;
    for (size_t i : shown)
        ys.push_back(parties[i].a);
    for (size_t i : shown)
        ys.push_back(parties[i].b);
    ys.push_back(game.first);
    ys.push_back(game.second);
    _lo = *std::min_element(ys.begin(), ys.end()) - 2;
    _hi = *std::max_element(ys.begin(), ys.end()) + 2;
    setMinimumHeight(TOP + std::max(1, lane) * ROW + 30);
    updateGeometry();
    update();
}

double Timeline::_x(double year) const
{
    const int w = std::max(50, width() - LEFT - 20);
    return LEFT + (year - _lo) / (_hi - _lo) * w;
}

double Timeline::_year(double x) const
{
    const int w = std::max(50, width() - LEFT - 20);
    return _lo + (x - LEFT) / double(w) * (_hi - _lo);
}

QRectF Timeline::_line(const Party &p, int lane) const
{
    const int y = TOP + lane * ROW + 26;
    return QRectF(_x(p.a), y - 7, std::max(6.0, _x(p.b) - _x(p.a)), 14);
}

std::tuple<std::optional<size_t>, int, QString> Timeline::_at(const QPointF &pos) const
{
    for (const auto &[q, lane] : _lanes) {
        const QRectF r = _line(parties[q], lane);
        if (r.adjusted(-5, -22, 5, 5).contains(pos)) {
            if (std::abs(pos.x() - r.left()) <= 6 && pos.y() >= r.top() - 3)
                return {q, lane, u"a"_s};
            if (std::abs(pos.x() - r.right()) <= 6 && pos.y() >= r.top() - 3)
                return {q, lane, u"b"_s};
            return {q, lane, QString()};
        }
    }
    return {std::nullopt, 0, QString()};
}

void Timeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(theme::BG));
    const int h = height();
    const double gx0 = _x(game.first), gx1 = _x(game.second);
    p.fillRect(QRectF(gx0, TOP - 6, gx1 - gx0, h - TOP), QColor(255, 255, 255, 12));   // the years played
    const double span = _hi - _lo;
    const int step = span <= 80 ? 5 : span <= 220 ? 10 : span <= 500 ? 25 : 50;
    QFont f(font());
    f.setPixelSize(12);
    p.setFont(f);
    const long long y0 = static_cast<long long>(py::floordiv(_lo, double(step))) * step;
    for (long long yr = y0; yr <= _hi; yr += step)
        if (yr >= _lo) {
            const double x = _x(double(yr));
            p.setPen(QPen(QColor(theme::LINE), 1));
            p.drawLine(QPointF(x, TOP - 8), QPointF(x, h));
            p.setPen(QColor(theme::INK_DIM));
            drawtext::draw(p, QRectF(x - 30, 4, 60, 18), Qt::AlignCenter, QString::number(yr));
        }
    for (double x : {gx0, gx1}) {
        p.setPen(QPen(QColor(theme::GOLD), 2, Qt::DashLine));
        p.drawLine(QPointF(x, TOP - 12), QPointF(x, h));
    }
    p.setPen(QColor(theme::GOLD));
    drawtext::draw(p, QRectF(gx0 + 4, h - 20, 140, 16), Qt::AlignLeft | Qt::AlignVCenter, u"the game starts"_s);
    drawtext::draw(p, QRectF(gx1 - 144, h - 20, 140, 16), Qt::AlignRight | Qt::AlignVCenter, u"the game ends"_s);
    QFont bold(f);
    bold.setBold(true);
    for (const auto &[ideo, first, n] : _heads) {                     // the bands
        const QColor col = qc(color(ideo));
        const int top = TOP + first * ROW;
        p.fillRect(QRectF(0, top + 2, width(), n * ROW - 3), QColor(col.red(), col.green(), col.blue(), 16));
        p.fillRect(QRectF(0, top + 2, 6, n * ROW - 3), col);
        p.setFont(bold);
        if (col.lightness() < 150) {                                   // (dark colours: readable on the dark)
            const double k = (150 - col.lightness()) / 150.0;
            p.setPen(QColor(int(col.red() + (255 - col.red()) * k), int(col.green() + (255 - col.green()) * k),
                            int(col.blue() + (255 - col.blue()) * k)));
        } else
            p.setPen(col);
        drawtext::draw(p, QRectF(12, top + 4, LEFT - 18, ROW - 4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   names.value(ideo, ideo));
    }
    p.setFont(f);
    const QFontMetrics fm(f);
    std::map<int, double> nxt;                                         // where the next line of a lane starts
    std::vector<std::pair<size_t, int>> by_start = _lanes;
    std::stable_sort(by_start.begin(), by_start.end(),
                     [&](const auto &x, const auto &y) { return -parties[x.first].a < -parties[y.first].a; });
    for (const auto &[qi, lane] : by_start) {
        const Party &q = parties[qi];
        QRectF r = _line(q, lane);
        if (_drag && _drag->q == qi) {
            const double a = _drag->which == u"a" ? _drag->yr : q.a;
            const double b = _drag->which == u"b" ? _drag->yr : q.b;
            r = QRectF(_x(a), r.top(), std::max(6.0, _x(b) - _x(a)), r.height());
        }
        const QColor col = qc(color(q.ideology));
        p.setPen(QPen(QColor(0, 0, 0), 2));
        p.setBrush(col);
        p.drawRoundedRect(r, 6, 6);
        if (q.i == sel) {
            p.setPen(QPen(QColor(u"#ffffff"_s), 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 7, 7);
        }
        const auto it = nxt.find(lane);
        const double room = std::max(30.0, (it != nxt.end() ? it->second : double(width() - 8)) - r.left() - 8);
        nxt[lane] = r.left();
        p.setPen(q.i == sel ? QColor(u"#ffffff"_s) : QColor(theme::INK));
        drawtext::draw(p, QRectF(r.left(), r.top() - 18, room, 17), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(q.name, Qt::ElideRight, int(room)));
    }
    if (_mouse && _mouse->x() >= LEFT) {
        const double x = _mouse->x();
        p.setPen(QPen(QColor(255, 255, 255, 150), 2));
        p.drawLine(QPointF(x, TOP - 12), QPointF(x, h));
    }
    if (_lanes.empty()) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect(), Qt::AlignCenter, !parties.empty() ? u"No party fits the filter."_s
                                                             : u"This nation has no parties yet — add one on the right."_s);
    }
    p.end();
}

void Timeline::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    update();
}

std::vector<size_t> Timeline::active(double year) const
{
    std::vector<size_t> out;
    for (const auto &[q, _l] : _lanes)
        if (parties[q].a <= year && year < parties[q].b)
            out.push_back(q);
    return out;
}

void Timeline::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    _mouse = pos;
    if (_drag) {
        const Party &q = parties[_drag->q];
        double yr = double(py::round_int(_year(pos.x())));
        yr = _drag->which == u"a" ? std::min(yr, q.b - 1) : std::max(yr, q.a + 1);
        _drag->yr = yr;
        Q_EMIT hovered(u"%1: %2 %3"_s.arg(q.name, _drag->which == u"a" ? u"from"_s : u"until"_s)
                           .arg(static_cast<long long>(yr)));
        update();
        return;
    }
    const auto [q, _lane, end] = _at(pos);
    setCursor(!end.isEmpty() ? Qt::SizeHorCursor : q ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (pos.x() >= LEFT) {
        const double yr = _year(pos.x());
        const std::vector<size_t> act = active(yr);
        QStringList nm;
        for (size_t a : act)
            nm << parties[a].name;
        const QString joined = nm.join(u", "_s);
        Q_EMIT hovered(u"%1 — %2 part%3 exist%4: %5"_s.arg(static_cast<long long>(yr))
                           .arg(act.size())
                           .arg(act.size() == 1 ? u"y"_s : u"ies"_s, act.size() == 1 ? u"s"_s : QString(),
                                joined.isEmpty() ? u"none"_s : joined));
    }
    if (q)
        QToolTip::showText(e->globalPosition().toPoint(), tip(parties[*q]), this);
    else
        QToolTip::hideText();
    update();
}

QString Timeline::tip(const Party &q) const
{
    const Rgb col = color(q.ideology);
    QString rows;
    for (const auto &[k, v] : q.policies)
        rows += u"<tr><td style='color:#9aa1ad; padding-right:10px'>%1</td><td>%2</td></tr>"_s.arg(esc(loc(k)),
                                                                                                 esc(loc(v)));
    return u"<div style='max-width:460px'><b>%1</b><br><span style='color: rgb(%2,%3,%4)'>%5</span> · %6 – %7"
           u"<table style='margin-top:4px'>%8</table><span style='color:#8a8272'>click: edit it on the right · "
           u"drag an end: another year</span></div>"_s.arg(esc(q.name))
        .arg(std::get<0>(col))
        .arg(std::get<1>(col))
        .arg(std::get<2>(col))
        .arg(esc(names.value(q.ideology, q.ideology)), q.f.value(u"start_date"_s, u"?"_s),
             q.f.value(u"end_date"_s, u"?"_s), rows.isEmpty() ? u"<tr><td>no policies set</td></tr>"_s : rows);
}

void Timeline::leaveEvent(QEvent *)
{
    _mouse.reset();
    update();
}

void Timeline::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const auto [q, _lane, end] = _at(e->position());
    if (!q)
        return;
    const Party &p = parties[*q];
    if (p.i != sel) {
        sel = p.i;
        Q_EMIT picked(int(p.i));
    }
    if (!end.isEmpty())
        _drag = Drag{*q, end, end == u"a" ? p.a : p.b};
    update();
}

void Timeline::mouseReleaseEvent(QMouseEvent *)
{
    if (_drag) {
        const Drag dr = *_drag;
        _drag.reset();
        const Party &q = parties[dr.q];
        if (dr.yr != (dr.which == u"a" ? q.a : q.b))
            Q_EMIT dateDragged(int(q.i), dr.which == u"a" ? u"start_date"_s : u"end_date"_s,
                               u"%1.1.1"_s.arg(static_cast<long long>(dr.yr)));
        update();
    }
}

// ------------------------------------------------------------------ the window

PartiesWindow::PartiesWindow(moddata::Mod &mod_, QWidget *parent) : QDialog(parent), mod(mod_)
{
    setWindowFlags(Qt::Window);
    resize(1560, 900);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(14, 10, 14, 8);
    bl->setSpacing(8);
    title = label(u"Parties"_s, u"title"_s, false);
    bl->addWidget(title);
    bl->addStretch(1);
    bl->addWidget(label(u"Show only"_s, u"sub"_s, false));
    f_ideo = new QComboBox();
    f_issue = new QComboBox();
    f_opt = new QComboBox();
    for (const auto &[b, w] : {std::pair{f_ideo, 210}, std::pair{f_issue, 210}, std::pair{f_opt, 230}}) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setFixedWidth(w);
        bl->addWidget(b);
    }
    connect(f_ideo, &QComboBox::activated, this, [this](int) { _filter(); });
    connect(f_issue, &QComboBox::activated, this, [this](int) {
        _fill_opts();
        _filter();
    });
    connect(f_opt, &QComboBox::activated, this, [this](int) { _filter(); });
    lay->addWidget(bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setFrameShape(QFrame::NoFrame);
    timeline = new Timeline();
    timeline->loc = [this](const QString &k) { return _loc(k); };
    connect(timeline, &Timeline::picked, this, [this](int i) { _timeline_picked(i); });
    connect(timeline, &Timeline::dateDragged, this,
            [this](int i, const QString &k, const QString &d) { _date_dragged(i, k, d); });
    connect(timeline, &Timeline::hovered, this, [this](const QString &t) { status->setText(t); });
    sc->setWidget(timeline);
    ll->addWidget(sc, 1);
    auto *foot = new QWidget();
    foot->setObjectName(u"side"_s);
    auto *fl = new QVBoxLayout(foot);
    fl->setContentsMargins(14, 6, 14, 8);
    status = label(QString(), u"sub"_s);
    status->setMinimumHeight(34);
    fl->addWidget(status);
    fl->addWidget(label(u"One line per party, from the year it appears to the year it goes, in its ideology's "
                        u"colour; the parties of one ideology share a band. Hover a line for its policies · click "
                        u"to edit it on the right · drag an end to another year · the lighter stretch is the time "
                        u"the game is played."_s,
                        u"faint"_s));
    ll->addWidget(foot);
    split->addWidget(left);
    auto *rsc = new QScrollArea();
    rsc->setWidgetResizable(true);
    rsc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rsc->setObjectName(u"sideScroll"_s);
    box = new nationextras::PartiesBox(mod, nullptr, true, false);
    connect(box, &nationextras::PartiesBox::scriptEdit, this, &PartiesWindow::scriptEdit);
    connect(box, &nationextras::PartiesBox::nameEdit, this, &PartiesWindow::nameEdit);
    connect(box, &nationextras::PartiesBox::partyPicked, this, [this](long long i) { _box_picked(i); });
    rsc->setWidget(box);
    split->addWidget(rsc);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({1080, 480});
}

void PartiesWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)   // (a dialog would close)
        return;
    QDialog::keyPressEvent(e);
}

QString PartiesWindow::_loc(const QString &key) const
{
    OptStr t;
    if (doc && doc->maps.contains(u"loc"_s))
        t = doc->map<localisation::LocEdits>(u"loc"_s).text(key);
    else if (const QString *s = mod.loc.get(key))
        t = *s;
    if (t && !t->isEmpty())
        return *t;
    return QString(key).replace(u'_', u' ');
}

void PartiesWindow::open_for(document::Document *d, const OptStr &t)
{
    refresh(d, t);
    show();
    raise();
    activateWindow();
}

void PartiesWindow::refresh(document::Document *d, const OptStr &t0)
{
    const OptStr t = t0 && !t0->isEmpty() ? t0 : tag;
    const bool fresh = d != doc || t != tag;
    doc = d;
    tag = t;
    if (!d || !t)
        return;
    const moddata::Country *c = mod.countries.get(*t);
    const QString name = c ? c->name : *t;
    title->setText(u"Parties of %1 (%2)"_s.arg(name, *t));
    setWindowTitle(u"Parties — %1"_s.arg(name));
    box->refresh(d, t);
    if (!d->maps.contains(u"scripts"_s) || !box->rel)
        return;
    auto &store = d->map<scriptstore::ScriptStore>(u"scripts"_s);
    const std::vector<std::pair<QString, QStringList>> issues = box->issue_groups();
    QStringList keys;
    for (const auto &[k, _o] : issues)
        keys << k;
    OrderedMap<QString, Rgb> cols = ideology_colors(&store);
    std::vector<Party> parties;
    script::Nodes nodes;
    try {
        nodes = script::parse(store.text(*box->rel));
    } catch (const std::exception &) {
        return;
    }
    const std::vector<const script::Node *> ps = nationextras::party_nodes(nodes);
    for (size_t i = 0; i < ps.size(); ++i) {
        Party p;
        p.i = (long long)i;
        p.f = nationextras::fields(*ps[i]);
        const QString *sd = p.f.contains(u"start_date"_s) ? &*p.f.constFind(u"start_date"_s) : nullptr;
        const QString *ed = p.f.contains(u"end_date"_s) ? &*p.f.constFind(u"end_date"_s) : nullptr;
        p.a = year_of(sd ? OptStr(*sd) : std::nullopt, 1800.0);
        p.b = std::max(p.a + 1, year_of(ed ? OptStr(*ed) : std::nullopt, 2000.0));
        p.key = p.f.value(u"name"_s);
        p.name = _loc(p.f.value(u"name"_s));
        if (p.name.isEmpty())
            p.name = u"?"_s;
        p.ideology = p.f.value(u"ideology"_s);
        for (const QString &k : keys)
            if (!p.f.value(k).isEmpty())
                p.policies.emplace_back(k, p.f.value(k));
        parties.push_back(std::move(p));
    }
    std::pair<double, double> game{1836.0, 1936.0};
    try {
        const QString tx = store.text(u"common/defines.lua"_s);
        const auto m1 = py::re_search(u"start_date\\s*=\\s*'(\\d+)"_s, tx);
        const auto m2 = py::re_search(u"end_date\\s*=\\s*'(\\d+)"_s, tx);
        game = {m1.hasMatch() ? m1.captured(1).toDouble() : 1836.0, m2.hasMatch() ? m2.captured(1).toDouble() + 1 : 1936.0};
    } catch (const std::exception &) {
    }
    QStringList col_keys;
    for (const auto &[k, _c] : cols)
        col_keys << k;
    auto sig = std::make_pair(col_keys, issues);
    if (fresh || !_f_sig || sig != *_f_sig) {
        _f_sig = sig;
        const QVariant k0 = !fresh ? f_ideo->currentData() : QVariant(), k1 = !fresh ? f_issue->currentData() : QVariant(),
                       k2 = !fresh ? f_opt->currentData() : QVariant();
        f_ideo->clear();
        f_ideo->addItem(u"every ideology"_s, QVariant());
        for (const QString &k : col_keys)
            f_ideo->addItem(_loc(k), k);
        f_issue->clear();
        f_issue->addItem(u"any policy"_s, QVariant());
        for (const QString &k : keys)
            f_issue->addItem(_loc(k), k);
        _issues.clear();
        for (const auto &[k, o] : issues)
            _issues.insert(k, o);
        f_ideo->setCurrentIndex(std::max(0, f_ideo->findData(k0)));
        f_issue->setCurrentIndex(std::max(0, f_issue->findData(k1)));
        _fill_opts(k2);
    }
    timeline->sel = !parties.empty() ? box->cur : -1;
    timeline->filter = _filter_fn();
    QHash<QString, QString> names;
    for (const QString &k : col_keys)
        names.insert(k, _loc(k));
    timeline->set_data(std::move(parties), std::move(cols), std::move(names), game);
    _count();
}

void PartiesWindow::_fill_opts(const QVariant &keep)
{
    const OptStr issue = opt(f_issue->currentData());
    f_opt->clear();
    f_opt->setEnabled(issue.has_value());
    if (!issue) {
        f_opt->addItem(u"—"_s, QVariant());
        return;
    }
    for (const QString &o : _issues.value(*issue))
        f_opt->addItem(_loc(o), o);
    f_opt->setCurrentIndex(std::max(0, f_opt->findData(keep)));
}

std::function<bool(const Party &)> PartiesWindow::_filter_fn() const
{
    const OptStr ideo = opt(f_ideo->currentData()), issue = opt(f_issue->currentData()), o = opt(f_opt->currentData());
    if (!ideo && !issue)
        return {};
    return [ideo, issue, o](const Party &p) {
        if (ideo && p.ideology != *ideo)
            return false;
        if (!issue)
            return true;
        const auto it = p.f.constFind(*issue);
        const OptStr have = it != p.f.cend() ? OptStr(*it) : std::nullopt;
        return have == o;
    };
}

void PartiesWindow::_filter()
{
    timeline->filter = _filter_fn();
    timeline->relayout();
    _count();
}

void PartiesWindow::_count()
{
    const size_t n = timeline->_lanes.size(), tot = timeline->parties.size();
    status->setText(n != tot ? u"%1 of %2 parties shown."_s.arg(n).arg(tot) : u"%1 parties."_s.arg(tot));
}

void PartiesWindow::_timeline_picked(int i) { box->select(i); }

void PartiesWindow::_box_picked(long long i)
{
    timeline->sel = i;
    timeline->update();
}

void PartiesWindow::_date_dragged(int i, const QString &key, const QString &date)
{
    box->select(i);
    box->_field(key, date);
}

}  // namespace partieswin
