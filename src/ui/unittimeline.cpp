// unittimeline.py, the Unit timeline page (see unittimeline.h).
#include "ui/unittimeline.h"
#include "core/pypath.h"
#include "ui/drawtext.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "ui/theme.h"
#include "ui/unitpanel.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QApplication>
#include <QCompleter>
#include <QFrame>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using script::Node;
using unitpanel::HIDDEN;
using unitpanel::HIDDEN_COL;
using unitpanel::nice;
using widgets::label;
using widgets::Section;

namespace unittimeline {

const QStringList LAND = {u"attack"_s, u"defence"_s, u"discipline"_s, u"support"_s, u"maneuver"_s,
                          u"reconnaissance"_s, u"siege"_s, u"default_organisation"_s, u"maximum_speed"_s,
                          u"supply_consumption"_s};
const QStringList NAVAL = {u"hull"_s, u"gun_power"_s, u"fire_range"_s, u"evasion"_s, u"torpedo_attack"_s,
                           u"default_organisation"_s, u"maximum_speed"_s, u"supply_consumption"_s};
const QHash<QString, QString> SHORT = {{u"default_organisation"_s, u"Org."_s}, {u"maximum_speed"_s, u"Speed"_s},
                                       {u"supply_consumption"_s, u"Supply"_s}, {u"reconnaissance"_s, u"Recon"_s},
                                       {u"gun_power"_s, u"Guns"_s}, {u"fire_range"_s, u"Range"_s},
                                       {u"torpedo_attack"_s, u"Torpedo"_s}, {u"discipline"_s, u"Discipl."_s}};
const QHash<QString, QString> BASES = {{u"army_base"_s, u"land"_s}, {u"navy_base"_s, u"naval"_s}};
const QString TECH_COL = u"#c9a15a"_s, INV_COL = u"#5b8fd6"_s;
const QStringList LINES = {u"#e8b04a"_s, u"#5fb8e0"_s, u"#e0665e"_s, u"#6fbf73"_s, u"#d977c8"_s, u"#a488e0"_s,
                           u"#f0e06a"_s, u"#4fb3a5"_s, u"#f09a5a"_s, u"#9fb0c8"_s, u"#c2f07a"_s, u"#ff9ec0"_s,
                           u"#7f93ff"_s, u"#c9bf8a"_s, u"#58d6c0"_s, u"#ffffff"_s};
// the graph can also draw what a unit does from the back row (the tester, 2026-10-15: "support attack / support
// defence" were missing): (attack or defence + the hidden 10) × support — support 1 = 100 % (× 1), 2 = doubled
const QHash<QString, QString> SUPPORT_STATS = {{u"support_attack"_s, u"attack"_s},
                                               {u"support_defence"_s, u"defence"_s}};
const QStringList GRAPH_LAND = [] {
    const qsizetype i = LAND.indexOf(u"support"_s) + 1;
    return LAND.mid(0, i) + QStringList{u"support_attack"_s, u"support_defence"_s} + LAND.mid(i);
}();

static widgets::Rgb rgb(const QString &hexcol)
{
    const QColor c(hexcol);
    return {c.red(), c.green(), c.blue()};
}

static double sfac(double s) { return s > 10 ? s / 100.0 : s; }   // unitpanel.support_factor of a number

QString g(double v)
{
    const long long r = py::round_int(v);
    if (std::fabs(v - static_cast<double>(r)) > 1e-9) {
        QString s = py::fmt_f(v, 3);
        while (s.endsWith(u'0'))
            s.chop(1);
        while (s.endsWith(u'.'))
            s.chop(1);
        return s;
    }
    return QString::number(r);
}

long long year_step(double span)
{
    // The years between two marks of the scale, for a span of years shown.
    return span > 60 ? 10 : span > 25 ? 5 : span > 10 ? 2 : 1;
}

// ---------------------------------------------------------------------------------------------- data
Units read_units(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // {unit: {"rel", "naval", "vals": {stat: number}}} from units/*.txt (the editor's texts).
    OrderedMap<QString, QString> files;
    QStringList dirs;
    if (mod.base && !mod.base->isEmpty())
        dirs << QDir(*mod.base).filePath(u"units"_s);
    dirs << QDir(mod.root).filePath(u"units"_s);
    for (const QString &d : dirs) {
        const QDir dir(d);
        if (!dir.exists())
            continue;
        for (const QString &fn : dir.entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Unsorted))
            if (py::lower(fn).endsWith(u".txt"))
                files.insert(py::lower(fn), u"units/"_s + fn);
    }
    QStringList rels;
    for (const auto &kv : files)
        rels << kv.second;
    std::stable_sort(rels.begin(), rels.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    Units out;
    for (const QString &rel : rels) {
        QString text;
        try {
            text = store.text(rel);
        } catch (const std::exception &) {
            continue;
        }
        for (const Node &n : script::parse(text)) {
            if (!(n.has_key && !n.key.isEmpty() && n.is_block))
                continue;
            if (!std::any_of(n.kids.begin(), n.kids.end(), [](const Node &c) { return c.has_key && c.key == u"type"; }))
                continue;
            OrderedMap<QString, QString> vals;
            for (const Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    vals.insert(c.key, py::strip(c.value));
            Unit u;
            u.rel = rel;
            const QString *t = vals.get(u"type"_s);
            u.naval = t && *t == u"naval";
            for (const auto &[k, v] : vals)
                if (unitpanel::num_like(v))
                    u.vals.insert(k, *py::to_float(v));
            out.insert(n.key, u);
        }
    }
    return out;
}

std::vector<Change> read_changes(techdata::TechEdits &techs, const Units &units)
{
    // Every number the technologies and inventions give a unit (or army_base / navy_base), in the order of the
    // years. -> [Change]
    techdata::TechDB &db = techs.db;
    const QStringList keys = techs.keys();
    const QSet<QString> tech_keys(keys.begin(), keys.end());
    QHash<QString, long long> years;
    for (const QString &k : keys)
        years.insert(k, techdata::year_of(techs.text(k)));
    std::vector<Change> out;
    auto take = [&](const QString &src, const QString &kind, long long year, const std::vector<Node> &nodes,
                    const std::vector<qsizetype> &path, const OptStr &after) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const Node &c = nodes[i];
            if (c.is_block && c.has_key && !c.key.isEmpty() && (units.contains(c.key) || BASES.contains(c.key))) {
                for (size_t j = 0; j < c.kids.size(); ++j) {
                    const Node &x = c.kids[j];
                    if (x.has_key && !x.key.isEmpty() && !x.is_block && unitpanel::num_like(py::strip(x.value))) {
                        std::vector<qsizetype> p = path;
                        p.push_back(static_cast<qsizetype>(i));
                        p.push_back(static_cast<qsizetype>(j));
                        out.push_back(Change{year, src, kind, c.key, x.key, *py::to_float(x.value), p, after});
                    }
                }
            }
        }
    };
    for (const QString &k : keys) {
        const auto r = techdata::root(techs.text(k));
        if (r)
            take(k, u"tech"_s, years.value(k), r->kids, {}, std::nullopt);
    }
    long long fallback = 1836;                   // min(years.values(), default=1836)
    if (!years.isEmpty()) {
        fallback = years.cbegin().value();
        for (auto it = years.cbegin(); it != years.cend(); ++it)
            fallback = std::min(fallback, it.value());
    }
    for (const QString &k : db.inventions()) {
        if (techs.removed.contains(k))
            continue;
        const QString text = techs.text(k);
        const auto r = techdata::root(text);
        if (!r)
            continue;
        const QStringList need = techdata::tech_refs(text, tech_keys);
        long long year = fallback;
        OptStr after;
        if (!need.isEmpty()) {
            year = years.value(need[0]);
            after = need[0];
            for (const QString &t : need)
                if (years.value(t) > year) {
                    year = years.value(t);
                    after = t;
                }
        }
        for (size_t i = 0; i < r->kids.size(); ++i) {
            const Node &c = r->kids[i];
            if (c.has_key && c.key == u"effect" && c.is_block)
                take(k, u"inv"_s, year, c.kids, {static_cast<qsizetype>(i)}, after);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Change &a, const Change &b) {
        const bool an = a.kind != u"tech", bn = b.kind != u"tech";
        return a.year != b.year ? a.year < b.year : an < bn;
    });
    return out;
}

std::vector<Change> for_unit(const std::vector<Change> &changes, const QString &unit, bool naval, bool inventions)
{
    const QString base = naval ? u"navy_base"_s : u"army_base"_s;
    std::vector<Change> out;
    for (const Change &c : changes)
        if ((c.target == unit || c.target == base) && (inventions || c.kind == u"tech"))
            out.push_back(c);
    return out;
}

OrderedMap<QString, std::pair<double, double>> totals(const Units &units, const std::vector<Change> &changes,
                                                      const QString &unit, long long year, bool inventions)
{
    // {stat: (base, added up to the year)} of a unit.
    const Unit &u = *units.get(unit);
    OrderedMap<QString, std::pair<double, double>> out;
    for (const auto &[k, v] : u.vals)
        out.insert(k, {v, 0.0});
    for (const Change &c : for_unit(changes, unit, u.naval, inventions))
        if (c.year <= year) {
            if (!out.contains(c.stat))
                out.insert(c.stat, {0.0, 0.0});
            out.get(c.stat)->second += c.value;
        }
    return out;
}

Steps series(const Units &units, const std::vector<Change> &changes, const QString &unit, const QString &stat,
             long long y0, bool inventions)
{
    // A unit's stat over the years as steps: [(year, total from that year on)], the first at y0.
    if (SUPPORT_STATS.contains(stat)) {
        const Steps a = series(units, changes, unit, SUPPORT_STATS.value(stat), y0, inventions);
        const Steps s = series(units, changes, unit, u"support"_s, y0, inventions);
        std::set<double> ys;
        for (const auto &[y, _v] : a)
            ys.insert(y);
        for (const auto &[y, _v] : s)
            ys.insert(y);
        const double hid = HIDDEN.value(SUPPORT_STATS.value(stat));
        Steps out;
        for (double y : ys)
            out.emplace_back(y, (Graph::value_at(a, y) + hid) * sfac(Graph::value_at(s, y)));
        return out;
    }
    const Unit &u = *units.get(unit);
    const double *bv = u.vals.get(stat);
    double v = bv ? *bv : 0.0;
    std::vector<std::pair<long long, double>> by_year;
    for (const Change &c : for_unit(changes, unit, u.naval, inventions))
        if (c.stat == stat) {
            auto it = std::find_if(by_year.begin(), by_year.end(), [&](const auto &p) { return p.first == c.year; });
            if (it == by_year.end())
                by_year.emplace_back(c.year, 0.0 + c.value);
            else
                it->second = it->second + c.value;
        }
    py::Sum before;
    for (const auto &[y, d] : by_year)
        if (y <= y0)
            before.add(d);
    v += before.value();
    Steps out{{static_cast<double>(y0), v}};
    std::vector<long long> later;
    for (const auto &[y, _d] : by_year)
        if (y > y0)
            later.push_back(y);
    std::sort(later.begin(), later.end());
    for (long long y : later) {
        v += std::find_if(by_year.begin(), by_year.end(), [&](const auto &p) { return p.first == y; })->second;
        out.emplace_back(static_cast<double>(y), v);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the graph
Graph::Graph(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setMinimumHeight(160);
}

void Graph::set_data(std::vector<Panel> panels_, long long y0_, long long y1_, long long year_, const QString &note_)
{
    // panels: [(stat name, [(unit name, colour, [(year, value)])])]
    y1_ = std::max(y1_, y0_ + 1);
    if (y0_ != y0 || y1_ != y1) {
        v0 = static_cast<double>(y0_);
        v1 = static_cast<double>(y1_);
    }
    panels = std::move(panels_);
    y0 = y0_;
    y1 = y1_;
    year = year_;
    note = note_;
    hover.reset();
    // (C++ only, the user 2026-11-18: "tiny graphs … a thin vertical band") a chart at least ~240 px tall; more of them
    // than fit: the graph scrolls
    setMinimumHeight(std::max(160, static_cast<int>(panels.size()) * 240 + 10));
    update();
}

bool Graph::whole() const { return v0 <= y0 && v1 >= y1; }

void Graph::set_view(double a, double b)
{
    const double span = std::min(static_cast<double>(y1 - y0), std::max(4.0, b - a));
    a = std::min(std::max(static_cast<double>(y0), a), y1 - span);
    if (a != v0 || a + span != v1) {
        v0 = a;
        v1 = a + span;
        update();
        Q_EMIT viewChanged();
    }
}

void Graph::zoom(double factor, std::optional<double> at_year)
{
    const double at = at_year ? *at_year : (v0 + v1) / 2.0;
    set_view(at - (at - v0) / factor, at + (v1 - at) / factor);
}

void Graph::fit() { set_view(static_cast<double>(y0), static_cast<double>(y1)); }

void Graph::fit_changes()
{
    std::optional<double> a, b;
    for (const Panel &pn : panels)
        for (const Line &l : pn.second)
            for (size_t i = 1; i < l.pts.size(); ++i) {
                a = a ? std::min(*a, l.pts[i].first) : l.pts[i].first;
                b = b ? std::max(*b, l.pts[i].first) : l.pts[i].first;
            }
    if (!a) {
        fit();
        return;
    }
    const double pad = std::max(3.0, (*b - *a) * 0.06);
    set_view(*a - pad, *b + pad);
}

double Graph::x_of(double yr) const { return LEFT + (yr - v0) / (v1 - v0) * (width() - LEFT - right_pad); }

double Graph::year_f(double x) const
{
    return v0 + (x - LEFT) / static_cast<double>(std::max(1, width() - LEFT - right_pad)) * (v1 - v0);
}

long long Graph::year_at(double x) const
{
    return py::round_int(std::min(static_cast<double>(y1), std::max(static_cast<double>(y0), year_f(x))));
}

std::vector<QRectF> Graph::_boxes() const
{
    const int n = std::max(1, static_cast<int>(panels.size()));
    const double h = (height() - 8) / static_cast<double>(n);
    std::vector<QRectF> out;
    for (int i = 0; i < n; ++i)                  // (under each chart: its years)
        out.emplace_back(LEFT, 8 + i * h + 10, width() - LEFT - right_pad, h - 44);
    return out;
}

double Graph::value_at(const Steps &pts, double yr)
{
    double v = pts[0].second;
    for (const auto &[y, val] : pts)
        if (y <= yr)
            v = val;
    return v;
}

std::pair<double, double> Graph::_range(const std::vector<Line> &lines) const
{
    // The values the chart shows upwards: those of the years in view, with a little room.
    std::vector<double> vals;
    for (const Line &l : lines) {
        vals.push_back(value_at(l.pts, v0));
        for (const auto &[y, v] : l.pts)
            if (v0 <= y && y <= v1)
                vals.push_back(v);
    }
    if (vals.empty())
        vals = {0.0, 1.0};
    double lo = *std::min_element(vals.begin(), vals.end()), hi = *std::max_element(vals.begin(), vals.end());
    if (hi - lo < 1e-9) {
        lo -= 1;
        hi += 1;
    }
    const double pad = (hi - lo) * 0.14;
    return {lo - pad, hi + pad};
}

double Graph::_y(const QRectF &box, double lo, double hi, double v, int k, int n) const
{
    return box.bottom() - (v - lo) / (hi - lo) * box.height() + (k - (n - 1) / 2.0) * 4.5;
}

void Graph::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(theme::PANEL));
    QFont f(font());
    f.setPixelSize(14);
    p.setFont(f);
    if (panels.empty() || std::none_of(panels.begin(), panels.end(), [](const Panel &x) { return !x.second.empty(); })) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect().adjusted(30, 0, -30, 0), Qt::AlignCenter | Qt::TextWordWrap, note);
        return;
    }
    const double cx = x_of(static_cast<double>(year));
    const long long step = year_step(v1 - v0);
    const std::vector<QRectF> boxes = _boxes();
    for (size_t pi = 0; pi < panels.size() && pi < boxes.size(); ++pi) {
        const QString &stat = panels[pi].first;
        const std::vector<Line> &lines = panels[pi].second;
        const QRectF &box = boxes[pi];
        const auto [lo, hi] = _range(lines);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(u"#14171c"_s));
        p.drawRect(box);
        double y = std::ceil(v0 / static_cast<double>(step)) * static_cast<double>(step);
        while (y <= v1 + 1e-9) {
            p.setPen(QPen(QColor(255, 255, 255, 16), 1));
            p.drawLine(QPointF(x_of(y), box.top()), QPointF(x_of(y), box.bottom()));
            y += static_cast<double>(step);
        }
        QFont fb(f);
        fb.setBold(true);
        p.setFont(fb);
        p.setPen(QColor(theme::GOLD));
        drawtext::draw(p, QRectF(12, box.top(), LEFT - 70, box.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
                   stat);
        p.setFont(f);
        const double span = hi - lo;
        double tick = 2000;
        const double room = std::max(3.0, std::min(8.0, box.height() / 42.0));   // (a number about every 42 px)
        for (double s : {0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 500.0, 1000.0})
            if (span / s <= room) {
                tick = s;
                break;
            }
        double v = std::ceil(lo / tick) * tick;
        while (v <= hi) {
            const double yy = box.bottom() - (v - lo) / (hi - lo) * box.height();
            p.setPen(QPen(QColor(255, 255, 255, 28), 1));
            p.drawLine(QPointF(box.left(), yy), QPointF(box.right(), yy));
            p.setPen(QColor(theme::INK));
            drawtext::draw(p, QRectF(LEFT - 62, yy - 10, 54, 20), Qt::AlignRight | Qt::AlignVCenter, g(v));
            p.setPen(QPen(QColor(theme::INK_DIM), 2));
            p.drawLine(QPointF(box.left() - 6, yy), QPointF(box.left(), yy));
            v += tick;
        }
        p.save();
        p.setClipRect(box.adjusted(1, 1, -1, -1));
        const int n = static_cast<int>(lines.size());
        std::vector<int> order;
        for (int k = 0; k < n; ++k)
            order.push_back(k);
        std::vector<std::tuple<int, QColor, const Steps *>> marks;
        std::optional<int> hov;
        if (hover && hover->first == static_cast<int>(pi))
            hov = hover->second;
        if (hov && *hov < n) {
            order.erase(std::find(order.begin(), order.end(), *hov));
            order.push_back(*hov);               // (the line under the mouse on top)
        }
        for (int k : order) {
            const Line &ln = lines[k];
            const Steps &pts = ln.pts;
            // (C++ only, the user 2026-11-18: "sudden steps") the levels thick — what is read — and the jump from one
            // level to the next a thin dashed riser that only joins them
            std::vector<std::tuple<double, double, double>> levels, risers;   // (x from, x to, y) / (x, y from, y to)
            double last = value_at(pts, v0 - 1), from = x_of(v0 - 1);
            for (const auto &[yr, val] : pts) {
                if (yr < v0 - 1)
                    continue;
                const double x = x_of(yr);
                levels.emplace_back(from, x, _y(box, lo, hi, last, k, n));
                if (val != last)
                    risers.emplace_back(x, _y(box, lo, hi, last, k, n), _y(box, lo, hi, val, k, n));
                last = val;
                from = x;
            }
            levels.emplace_back(from, x_of(static_cast<double>(y1)), _y(box, lo, hi, last, k, n));
            QColor c(ln.col);
            if (hov && k != *hov)
                c = c.darker(190);
            const bool on = hov && k == *hov;
            p.setBrush(Qt::NoBrush);
            for (const auto &[x, ya, yb] : risers) {
                QPen pen(c, on ? 2.6 : 2.0);
                pen.setDashPattern({2.0, 2.0});
                p.setPen(pen);
                p.drawLine(QPointF(x, ya), QPointF(x, yb));
            }
            for (QPen pen : {QPen(QColor(10, 10, 10), on ? 10.0 : 8.0), QPen(c, on ? 6.2 : 4.8)}) {
                pen.setCapStyle(Qt::RoundCap);
                p.setPen(pen);
                for (const auto &[xa, xb, yy] : levels)
                    if (xb > xa)
                        p.drawLine(QPointF(xa, yy), QPointF(xb, yy));
            }
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(c);
            for (size_t i = 1; i < pts.size(); ++i)
                p.drawEllipse(QPointF(x_of(pts[i].first), _y(box, lo, hi, pts[i].second, k, n)), 6.0, 6.0);
            marks.emplace_back(k, c, &pts);
        }
        // the number at every change (and where the line comes in): read without hovering. A number that would lie
        // on one already written is left out — the hovered line's are written first.
        std::vector<QRectF> taken;
        p.setFont(fb);
        const QFontMetrics fmb = p.fontMetrics();
        {   // (C++ only, 2026-11-18) the legend: each unit's colour, name and value in the year picked
            double lw = 0;
            std::vector<QString> rows_t;
            for (const Line &ln : lines) {
                rows_t.push_back(u"%1  %2"_s.arg(ln.name, g(value_at(ln.pts, static_cast<double>(year)))));
                lw = std::max(lw, static_cast<double>(fmb.horizontalAdvance(rows_t.back())));
            }
            const QRectF lg(box.left() + 26, box.top() + 8, lw + 34, 20.0 * static_cast<double>(lines.size()) + 8);
            p.setPen(QPen(QColor(10, 10, 10), 1.5));
            p.setBrush(QColor(20, 23, 28, 225));
            p.drawRoundedRect(lg, 4, 4);
            for (size_t i = 0; i < lines.size(); ++i) {
                const double ly = lg.top() + 4 + 20.0 * static_cast<double>(i);
                p.setPen(QPen(QColor(10, 10, 10), 1.5));
                p.setBrush(QColor(lines[i].col));
                p.drawRoundedRect(QRectF(lg.left() + 7, ly + 4, 14, 12), 2, 2);
                p.setPen(QColor(lines[i].col).lighter(125));
                drawtext::draw(p, QRectF(lg.left() + 27, ly, lw + 4, 20), Qt::AlignVCenter | Qt::AlignLeft, rows_t[i]);
            }
            taken.push_back(lg.adjusted(-2, -2, 2, 2));
        }
        std::stable_sort(marks.begin(), marks.end(), [&](const auto &a, const auto &b) {
            return (!hov || std::get<0>(a) != *hov) < (!hov || std::get<0>(b) != *hov);
        });
        for (const auto &[k, c, pts] : marks) {
            std::vector<std::pair<double, double>> spots{{std::max(v0, static_cast<double>(y0)), value_at(*pts, v0)}};
            for (size_t i = 1; i < pts->size(); ++i)
                if (v0 <= (*pts)[i].first && (*pts)[i].first <= v1)
                    spots.push_back((*pts)[i]);
            for (const auto &[yr, val] : spots) {
                const QString text = g(val);
                const int tw = fmb.horizontalAdvance(text) + 8;
                const double px = x_of(yr), py_ = _y(box, lo, hi, val, k, n);
                QRectF r(px + 5, py_ - 21, tw, 17);
                if (r.right() > box.right() - 2)
                    r.moveRight(px - 5);
                if (r.top() < box.top() + 2)
                    r.moveTop(py_ + 6);
                if (std::any_of(taken.begin(), taken.end(), [&](const QRectF &o) { return r.intersects(o); }))
                    continue;
                taken.push_back(r.adjusted(-2, -1, 2, 1));
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(10, 10, 10, 215));
                p.drawRoundedRect(r, 3, 3);
                p.setPen(QColor(c).lighter(125));
                drawtext::draw(p, r, Qt::AlignCenter, text);
            }
        }
        labels[static_cast<int>(pi)] = static_cast<int>(taken.size());
        p.restore();
        p.setFont(fb);
        const std::tuple<QString, bool, double> sides[2] = {{u"◀"_s, v0 > y0, box.left() + 4},
                                                            {u"▶"_s, v1 < y1, box.right() - 22}};
        for (const auto &[side, more, x] : sides)
            if (more) {                          // (more years on this side: the picture goes on)
                p.setPen(QColor(theme::GOLD));
                drawtext::draw(p, QRectF(x, box.top() + 2, 18, 18), Qt::AlignCenter, side);
            }
        p.setFont(f);
        {   // (C++ only, 2026-11-18) the years under the chart, with ticks
            double yy = std::ceil(v0 / static_cast<double>(step)) * static_cast<double>(step);
            while (yy <= v1 + 1e-9) {
                const double x = x_of(yy);
                p.setPen(QPen(QColor(theme::INK_DIM), 2));
                p.drawLine(QPointF(x, box.bottom()), QPointF(x, box.bottom() + 6));
                p.setPen(QColor(theme::INK));
                drawtext::draw(p, QRectF(x - 30, box.bottom() + 7, 60, 18), Qt::AlignHCenter | Qt::AlignTop,
                               QString::number(py::round_int(yy)));
                yy += static_cast<double>(step);
            }
        }
        if (v1 >= y1) {                          // the end of the game: a closed, thick edge
            p.setPen(QPen(QColor(theme::INK_DIM), 4));
            p.drawLine(QPointF(box.right(), box.top()), QPointF(box.right(), box.bottom()));
        }
        if (box.left() <= cx && cx <= box.right()) {
            p.setPen(QPen(QColor(10, 10, 10), 5));
            p.drawLine(QPointF(cx, box.top()), QPointF(cx, box.bottom()));
            p.setPen(QPen(QColor(theme::GOLD), 3));
            p.drawLine(QPointF(cx, box.top()), QPointF(cx, box.bottom()));
        }
    }
}

std::optional<std::tuple<int, int, long long, double>> Graph::line_at(const QPointF &pos) const
{
    // The line under a place: (chart, line, year, value) or None — the nearest within a few pixels.
    const std::vector<QRectF> boxes = _boxes();
    for (size_t pi = 0; pi < panels.size() && pi < boxes.size(); ++pi) {
        const std::vector<Line> &lines = panels[pi].second;
        const QRectF &box = boxes[pi];
        if (!box.adjusted(-4, -8, 4, 8).contains(pos) || lines.empty())
            continue;
        const auto [lo, hi] = _range(lines);
        const double yr = std::min(static_cast<double>(y1), std::max(static_cast<double>(y0), year_f(pos.x())));
        std::optional<std::tuple<double, int, int, long long, double>> best;
        const int n = static_cast<int>(lines.size());
        for (int k = 0; k < n; ++k) {
            const Steps &pts = lines[k].pts;
            const double v = value_at(pts, yr);
            double d = std::fabs(_y(box, lo, hi, v, k, n) - pos.y());
            for (size_t i = 1; i < pts.size(); ++i) {   // (the upright part of a step counts too)
                const auto &[y2, v2] = pts[i];
                if (std::fabs(x_of(y2) - pos.x()) <= 5) {
                    double a = _y(box, lo, hi, value_at(pts, y2 - 0.001), k, n), b2 = _y(box, lo, hi, v2, k, n);
                    if (b2 < a)
                        std::swap(a, b2);
                    if (a <= pos.y() && pos.y() <= b2)
                        d = 0.0;
                }
            }
            if (d <= 9 && (!best || d < std::get<0>(*best)))
                best = std::tuple{d, static_cast<int>(pi), k, py::trunc_int(yr), v};
        }
        if (!best)
            return std::nullopt;
        return std::tuple{std::get<1>(*best), std::get<2>(*best), std::get<3>(*best), std::get<4>(*best)};
    }
    return std::nullopt;
}

void Graph::wheelEvent(QWheelEvent *e)
{
    zoom(e->angleDelta().y() > 0 ? 1.3 : 1 / 1.3, year_f(e->position().x()));
    e->accept();
}

void Graph::mousePressEvent(QMouseEvent *e) { _drag = Drag{e->position(), v0, v1, false}; }

void Graph::mouseMoveEvent(QMouseEvent *e)
{
    if (_drag && e->buttons()) {
        const double dx = e->position().x() - _drag->start.x();
        if (_drag->moved || std::fabs(dx) > 4) {
            _drag->moved = true;
            setCursor(Qt::ClosedHandCursor);
            const double dy = dx / static_cast<double>(std::max(1, width() - LEFT - right_pad)) * (_drag->v1 - _drag->v0);
            set_view(_drag->v0 - dy, _drag->v1 - dy);
        }
        return;
    }
    const auto h = line_at(e->position());
    std::optional<std::pair<int, int>> hv;
    if (h)
        hv = std::pair{std::get<0>(*h), std::get<1>(*h)};
    if (hv != hover) {
        hover = hv;
        update();
    }
    if (h)
        QToolTip::showText(e->globalPosition().toPoint(), hover_text(*h), this);
    else
        QToolTip::hideText();
}

QString Graph::hover_text(const std::tuple<int, int, long long, double> &h) const
{
    // What is under the mouse, in a line: the unit, its stat and the number in that year.
    const auto &[ci, li, yr, val] = h;
    const QString &stat = panels[ci].first;
    const std::vector<Line> &lines = panels[ci].second;
    QStringList same;
    for (int k = 0; k < static_cast<int>(lines.size()); ++k)
        if (k != li && std::fabs(value_at(lines[k].pts, static_cast<double>(yr)) - val) < 1e-9)
            same << lines[k].name;
    return u"%1 — %2 %3 in %4%5"_s.arg(lines[li].name, py::lower(stat), g(val), QString::number(yr),
                                      !same.isEmpty() ? u"\n(the same: %1)"_s.arg(same.join(u", "_s)) : QString());
}

void Graph::mouseReleaseEvent(QMouseEvent *e)
{
    const auto drag = _drag;
    _drag.reset();
    unsetCursor();
    if (drag && !drag->moved)
        Q_EMIT picked(QString(), year_at(e->position().x()));
}

void Graph::leaveEvent(QEvent *)
{
    if (hover) {
        hover.reset();
        update();
    }
}

// ---------------------------------------------------------------------------------------------- the lanes
Lanes::Lanes(bool header_, QWidget *parent) : QWidget(parent), header(header_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    tip = [](const Change &c) { return c.src; };
    setMouseTracking(true);
    if (header)
        setFixedHeight(34);
}

void Lanes::set_data(const std::vector<LaneRow> &units_, const Marks &marks_, double y0_, double y1_)
{
    units = units_;
    marks = marks_;
    y0 = y0_;
    y1 = std::max(y1_, y0_ + 1);
    updateGeometry();
    update();
}

void Lanes::set_cursor_at(const OptStr &unit_, long long year_)
{
    unit = unit_;
    year = year_;
    update();
}

QSize Lanes::sizeHint() const { return QSize(600, header ? 34 : ROW_H * static_cast<int>(units.size()) + 6); }

QSize Lanes::minimumSizeHint() const { return QSize(300, header ? 34 : ROW_H * static_cast<int>(units.size()) + 6); }

double Lanes::x_of(double yr) const { return LEFT + (yr - y0) / (y1 - y0) * (width() - LEFT - right_pad); }

long long Lanes::year_at(double x) const
{
    const double y = y0 + (x - LEFT) / static_cast<double>(std::max(1, width() - LEFT - right_pad)) * (y1 - y0);
    return py::round_int(std::min(y1, std::max(y0, y)));
}

void Lanes::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(font());
    f.setPixelSize(13);
    p.setFont(f);
    const long long step = year_step(y1 - y0);
    const long long first = static_cast<long long>(std::ceil(y0 / static_cast<double>(step)) * step);
    const double cx = x_of(static_cast<double>(year));
    const long long last = py::trunc_int(y1);
    if (header) {
        p.fillRect(rect(), QColor(theme::PANEL));
        for (long long y = first; y < last + 1; y += step) {
            const double x = x_of(static_cast<double>(y));
            p.setPen(QColor(theme::INK_DIM));
            drawtext::draw(p, QRectF(x - 30, 2, 60, 18), Qt::AlignCenter, QString::number(y));
            p.setPen(QPen(QColor(theme::INK_FAINT), 2));
            p.drawLine(QPointF(x, 22), QPointF(x, 34));
        }
        f.setBold(true);
        p.setFont(f);
        if (!(LEFT - 1 <= cx && cx <= width() - right_pad + 1))
            return;                              // (the picked year is outside the years shown)
        const QRectF r(cx - 28, 6, 56, 24);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(theme::GOLD));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(QColor(18, 14, 8));
        drawtext::draw(p, r, Qt::AlignCenter, QString::number(year));
        return;
    }
    for (size_t i = 0; i < units.size(); ++i) {
        const LaneRow &row = units[i];
        const double top = static_cast<double>(i) * ROW_H;
        if (unit && row.key == *unit)
            p.fillRect(QRectF(0, top, width(), ROW_H), QColor(201, 161, 90, 46));
        else if (i % 2)
            p.fillRect(QRectF(0, top, width(), ROW_H), QColor(255, 255, 255, 8));
        const QString mine = colours.value(row.key);
        if (!mine.isEmpty()) {
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(QColor(mine));
            p.drawRoundedRect(QRectF(10, top + ROW_H / 2.0 - 7, 14, 14), 3, 3);
        }
        p.setPen(unit && row.key == *unit ? QColor(theme::GOLD) : QColor(theme::INK));
        const bool m = !mine.isEmpty();
        drawtext::draw(p, QRectF(m ? 32 : 12, top, LEFT - (m ? 44 : 24), ROW_H), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(row.name, Qt::ElideRight, LEFT - (m ? 46 : 26)));
        p.setPen(QPen(QColor(theme::INK_FAINT), 2));
        p.drawLine(QPointF(LEFT, top + ROW_H / 2.0), QPointF(width() - right_pad, top + ROW_H / 2.0));
    }
    for (long long y = first; y < last + 1; y += step) {
        p.setPen(QPen(QColor(255, 255, 255, 16), 1));
        p.drawLine(QPointF(x_of(static_cast<double>(y)), 0), QPointF(x_of(static_cast<double>(y)), height()));
    }
    p.setPen(QPen(QColor(10, 10, 10), 5));
    p.drawLine(QPointF(cx, 0), QPointF(cx, height()));
    p.setPen(QPen(QColor(theme::GOLD), 3));
    p.drawLine(QPointF(cx, 0), QPointF(cx, height()));
    QFont fb(f);
    fb.setBold(true);
    fb.setPixelSize(11);
    p.setFont(fb);
    for (size_t i = 0; i < units.size(); ++i) {
        const double cy = static_cast<double>(i) * ROW_H + ROW_H / 2.0;
        const auto mk = marks.find(units[i].key);
        if (mk == marks.end())
            continue;
        for (const auto &[yr, lst] : mk->second) {
            QColor col(std::any_of(lst.begin(), lst.end(), [](const Change &c) { return c.kind == u"tech"; }) ? TECH_COL
                                                                                                             : INV_COL);
            if (yr > year)
                col = col.darker(210);
            p.setPen(QPen(QColor(10, 10, 10), 2.5));
            p.setBrush(col);
            p.drawEllipse(QPointF(x_of(static_cast<double>(yr)), cy), 10, 10);
            if (lst.size() > 1) {
                p.setPen(yr <= year ? QColor(18, 14, 8) : QColor(theme::INK_DIM));
                drawtext::draw(p, QRectF(x_of(static_cast<double>(yr)) - 10, cy - 10, 20, 20), Qt::AlignCenter,
                           QString::number(lst.size()));
            }
        }
    }
}

std::tuple<OptStr, long long, const std::vector<Change> *> Lanes::_at(const QPointF &pos) const
{
    if (header)
        return {std::nullopt, year_at(pos.x()), nullptr};
    const long long i = static_cast<long long>(py::floordiv(pos.y(), static_cast<double>(ROW_H)));
    if (!(0 <= i && i < static_cast<long long>(units.size())))
        return {std::nullopt, year_at(pos.x()), nullptr};
    const QString &key = units[static_cast<size_t>(i)].key;
    std::optional<std::tuple<double, long long, const std::vector<Change> *>> near_;
    const auto mk = marks.find(key);
    if (mk != marks.end())
        for (const auto &[yr, lst] : mk->second) {
            const double d = std::fabs(x_of(static_cast<double>(yr)) - pos.x());
            if (d <= 12 && (!near_ || d < std::get<0>(*near_)))
                near_ = std::tuple{d, yr, &lst};
        }
    return {key, near_ ? std::get<1>(*near_) : year_at(pos.x()), near_ ? std::get<2>(*near_) : nullptr};
}

void Lanes::mousePressEvent(QMouseEvent *e)
{
    const auto [key, yr, _lst] = _at(e->position());
    Q_EMIT picked(key && !key->isEmpty() ? *key : unit ? *unit : QString(), yr);
}

void Lanes::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton) {
        const auto [key, yr, _lst] = _at(e->position());
        Q_EMIT picked(unit && !unit->isEmpty() ? *unit : key ? *key : QString(),
                      !header ? yr : year_at(e->position().x()));
        return;
    }
    const auto [_key, _yr, lst] = _at(e->position());
    if (lst && !lst->empty()) {
        QStringList parts;
        for (const Change &c : *lst)
            parts << tip(c);
        QToolTip::showText(e->globalPosition().toPoint(), parts.join(u'\n'), this);
    } else {
        QToolTip::hideText();
    }
}

// ---------------------------------------------------------------------------------------------- the page
const QString LANES_NOTE =
    u"A lane per unit; a marker where a technology (gold) or an invention (blue) changes its stats — the number in it: "
    u"how many changes that year. Click or drag in the lanes: the year. Below: every unit's stats in total at that year, "
    u"and the changes of the picked unit — their numbers are typed there (that edits the technology)."_s;
const QString GRAPH_NOTE =
    u"A chart per stat: a line per unit ticked in the table below (or the picked unit), the level it has over the years, "
    u"a dot where a technology changes it. Click a stat to see it alone, Ctrl+click to add or take one away. The box in "
    u"each chart: every unit's value in the year picked. Wheel: zoom into the years; drag: move; click: the year."_s;

// --- the technology / invention list under "Add a change": TechPopup (unittimeline.h)
const QStringList TechPopup::CATS = {u"army"_s, u"navy"_s, u"commerce"_s, u"culture"_s, u"industry"_s};
QColor TechPopup::cat_colour(const QString &c)
{
    if (c == u"army")
        return QColor(214, 96, 74);
    if (c == u"navy")
        return QColor(86, 140, 222);
    if (c == u"commerce")
        return QColor(222, 182, 64);
    if (c == u"culture")
        return QColor(176, 116, 206);
    if (c == u"industry")
        return QColor(124, 176, 112);
    return QColor(theme::INK_DIM);
}


UnitTimelineView::UnitTimelineView(moddata::Mod &mod_, TechEdit on_tech_edit_, QWidget *parent)
    : QWidget(parent), mod(mod_), on_tech_edit(std::move(on_tech_edit_))
{
    stats[false] = {u"attack"_s, u"defence"_s};
    stats[true] = {u"hull"_s, u"gun_power"_s};
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *s = new Section();
    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    row->addWidget(label(u"Unit timeline"_s, u"title"_s, false));
    auto *grp = new QButtonGroup(this);
    auto *seg = new QHBoxLayout();
    seg->setSpacing(0);
    for (const auto &[k, t] : {std::pair{false, u"Land units"_s}, std::pair{true, u"Naval units"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(!k);
        connect(b, &QPushButton::clicked, this, [this, kk = k] { set_naval(kk); });
        grp->addButton(b);
        seg->addWidget(b);
        kind_btn[k] = b;
    }
    row->addLayout(seg);
    inv_box = new QCheckBox(u"Inventions too"_s);
    inv_box->setChecked(true);
    inv_box->setToolTip(u"Count what inventions give as well (blue markers) — from the year of the technology an "
                        u"invention needs; in the game it then comes by chance"_s);
    connect(inv_box, &QCheckBox::toggled, this, [this](bool) { _show(); });
    row->addWidget(inv_box);
    only_box = new QCheckBox(u"Only the ticked"_s);
    only_box->setToolTip(u"Leave only the units ticked in the table below in the lanes and the table"_s);
    connect(only_box, &QCheckBox::toggled, this, [this](bool) { _show(); });
    row->addWidget(only_box);
    auto *grp2 = new QButtonGroup(this);
    auto *seg2 = new QHBoxLayout();
    seg2->setSpacing(0);
    const std::tuple<QString, QString, QString> views[2] = {
        {u"lanes"_s, u"Lanes"_s, u"A lane per unit with a marker where its stats change"_s},
        {u"graph"_s, u"Graph"_s, u"The ticked units' stats as lines over the years — a chart per stat picked below"_s}};
    for (const auto &[k, tx, tp] : views) {
        auto *b = new QPushButton(tx);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"lanes");
        b->setToolTip(tp);
        connect(b, &QPushButton::clicked, this, [this, kk = k] { set_view(kk); });
        grp2->addButton(b);
        seg2->addWidget(b);
        view_btn[k] = b;
    }
    row->addLayout(seg2);
    row->addStretch(1);
    const std::tuple<QString, QString, int> jumps[2] = {{u"◀ change"_s, u"The year of the change before"_s, -1},
                                                        {u"change ▶"_s, u"The year of the next change"_s, 1}};
    for (const auto &[text, tp, st] : jumps) {
        auto *b = new QPushButton(text);
        b->setToolTip(tp + u" (of the picked unit; of any unit when none is picked)"_s);
        connect(b, &QPushButton::clicked, this, [this, st_ = st] { jump(st_); });
        row->addWidget(b);
        (st < 0 ? prev_btn : next_btn) = b;
    }
    s->lay->addLayout(row);
    note = label(LANES_NOTE, u"faint"_s);
    s->lay->addWidget(note);
    stat_host = new QWidget();
    stat_row = new widgets::WrapRow(stat_host, 6);
    stat_host->hide();
    s->lay->addWidget(stat_host);
    zoom_host = new QWidget();
    auto *zr = new QHBoxLayout(zoom_host);
    zr->setContentsMargins(0, 0, 0, 0);
    zr->setSpacing(6);
    const std::tuple<QString, QString, std::function<void()>> zooms[6] = {
        {u"－ years"_s, u"Show more years (also: the mouse wheel over the graph)"_s, [this] { graph->zoom(1 / 1.5); }},
        {u"＋ years"_s, u"Zoom into the years (also: the mouse wheel over the graph)"_s, [this] { graph->zoom(1.5); }},
        {u"◀"_s, u"Earlier years (also: drag the graph)"_s, [this] { _slide(-0.4); }},
        {u"▶"_s, u"Later years (also: drag the graph)"_s, [this] { _slide(0.4); }},
        {u"Whole time"_s, u"From the start to the end of the game"_s, [this] { graph->fit(); }},
        {u"The changes"_s, u"From the first change to the last"_s, [this] { graph->fit_changes(); }}};
    for (const auto &[text, tp, fn] : zooms) {
        auto *b = new QPushButton(text);
        b->setToolTip(tp);
        connect(b, &QPushButton::clicked, this, [f = fn] { f(); });
        zr->addWidget(b);
    }
    span_note = label(QString(), u"faint"_s, false);
    zr->addWidget(span_note);
    zr->addStretch(1);
    zoom_host->hide();
    s->lay->addWidget(zoom_host);
    outer->addWidget(s);
    split = new QSplitter(Qt::Vertical);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *top = new QWidget();
    auto *tl = new QVBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);
    axis = new Lanes(true);
    connect(axis, &Lanes::picked, this, [this](const QString &, long long y) { pick(unit, y); });
    tl->addWidget(axis);
    lanes = new Lanes();
    connect(lanes, &Lanes::picked, this, [this](const QString &u, long long y) {
        pick(!u.isEmpty() ? OptStr(u) : unit, y);
    });
    lanes->tip = [this](const Change &c) { return line(c); };
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    sc->setStyleSheet(u"QScrollArea { background: %1; border: none; }"_s.arg(theme::PANEL));
    lanes->setObjectName(u"sideInner"_s);
    lanes->setAttribute(Qt::WA_StyledBackground, true);
    sc->setWidget(lanes);
    connect(split, &QSplitter::splitterMoved, this, [this](int pos, int index) { _split_moved(pos, index); });
    axis->right_pad = lanes->right_pad + style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    graph = new Graph();
    graph->right_pad = lanes->right_pad;          // (its scroll area's bar takes the rest, as the lanes')
    connect(graph, &Graph::picked, this, [this](const QString &, long long y) { pick(unit, y); });
    connect(graph, &Graph::viewChanged, this, [this] { _axis_follows(); });
    stack = new QStackedWidget();
    stack->addWidget(sc);
    graph_scroll = new QScrollArea();
    graph_scroll->setWidgetResizable(true);
    graph_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    graph_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    graph_scroll->setStyleSheet(u"QScrollArea { background: %1; border: none; }"_s.arg(theme::PANEL));
    graph_scroll->setWidget(graph);
    stack->addWidget(graph_scroll);
    tl->addWidget(stack, 1);
    split->addWidget(top);
    auto *low = new QSplitter(Qt::Horizontal);
    low->setChildrenCollapsible(false);
    split->addWidget(low);
    s = new Section();
    tot_title = label(QString(), u"title"_s);
    s->lay->addWidget(tot_title);
    tot = new QTreeWidget();
    tot->setHeader(new widgets::TintHeader(QColor(HIDDEN_COL), {}, tot));   // (its cyan-marked columns: set per fill)
    tot->setRootIsDecorated(false);
    tot->setUniformRowHeights(true);
    tot->setSelectionMode(QAbstractItemView::SingleSelection);
    tot->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(tot, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *it, QTreeWidgetItem *pv) { _tot_picked(it, pv); });
    connect(tot, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) { _tot_ticked(it, col); });
    s->lay->addWidget(tot, 1);
    s->lay->addWidget(label(u"Gold: changed by then (hover: base + what was added). The tick boxes pick the units for "
                            u"“Only the ticked” and for the graph; the colour square is the unit's line."_s,
                            u"faint"_s));
    low->addWidget(s);
    s = new Section();
    ch_title = label(u"Pick a unit"_s, u"title"_s);
    s->lay->addWidget(ch_title);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    warn->hide();
    s->lay->addWidget(warn);
    auto *sc2 = new QScrollArea();
    sc2->setWidgetResizable(true);
    sc2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc2->setStyleSheet(u"QScrollArea { background: %1; border: none; }"_s.arg(theme::PANEL));
    rows_host = new QWidget();
    rows_host->setObjectName(u"sideInner"_s);
    rows_host->setAttribute(Qt::WA_StyledBackground, true);
    rows = new QGridLayout(rows_host);
    rows->setContentsMargins(0, 0, 6, 0);
    rows->setHorizontalSpacing(8);
    rows->setVerticalSpacing(4);
    sc2->setWidget(rows_host);
    rows_scroll = sc2;
    s->lay->addWidget(sc2, 1);
    auto *add = new QHBoxLayout();
    add->setSpacing(6);
    add_tech = new QComboBox();
    add_tech->setEditable(true);
    add_tech->setInsertPolicy(QComboBox::NoInsert);
    add_tech->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add_tech->setToolTip(u"The technology (or invention) that gives the unit the number. Click into the box for the whole "
                         u"list (army, navy, commerce, culture, industry; by year), or type a year, a part of its name or "
                         u"a section"_s);
    add_tech->setCompleter(nullptr);              // (the list under it instead: TechPopup)
    tech_pop = new TechPopup(add_tech);
    connect(add_tech, &QComboBox::currentIndexChanged, this, [this] {   // (the year first, not the name's end)
        if (add_tech->lineEdit() && !add_tech->lineEdit()->hasFocus())
            add_tech->lineEdit()->setCursorPosition(0);
    });
    add->addWidget(add_tech, 5);
    add_target = new QComboBox();               // (C++ only) the picked unit, or all land / naval units
    add_target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add_target->setToolTip(u"Who gets the change: the picked unit, or every land / naval unit (army_base / navy_base)"_s);
    add->addWidget(add_target, 3);
    add_stat = new QComboBox();
    add_stat->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add->addWidget(add_stat, 3);
    add_val = new QLineEdit();
    add_val->setPlaceholderText(u"+ / −"_s);
    add_val->setFixedWidth(70);
    connect(add_val, &QLineEdit::returnPressed, this, [this] { add_change(); });
    add->addWidget(add_val);
    add_btn = new QPushButton(u"＋ Add a change"_s);
    connect(add_btn, &QPushButton::clicked, this, [this] { add_change(); });
    add->addWidget(add_btn);
    s->lay->addLayout(add);
    low->addWidget(s);
    low->setStretchFactor(0, 6);
    low->setStretchFactor(1, 5);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 6);
}

// ------------------------------------------------------------------ data
QString UnitTimelineView::tname(const QString &key) const
{
    localisation::LocEdits *loc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr t;
    if (loc)
        t = loc->text(key);
    else if (const QString *x = mod.loc.get(key))
        t = *x;
    return t && !t->isEmpty() ? *t : py::title(QString(key).replace(u'_', u' '));
}

QString UnitTimelineView::line(const Change &c) const
{
    // A change in a line (the hover text of a marker).
    const QString who = !BASES.contains(c.target) ? QString() : u" (all %1 units)"_s.arg(BASES.value(c.target));
    return u"%1 — %2%3: %4 %5%6%7"_s.arg(QString::number(c.year), tname(c.src),
                                         c.kind == u"inv" ? u" [invention]"_s : QString(), py::lower(nice(c.stat)),
                                         c.value >= 0 ? u"+"_s : QString(), g(c.value), who);
}

void UnitTimelineView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    techs = doc && doc->maps.contains(u"techs"_s) ? &doc->map<techdata::TechEdits>(u"techs"_s) : nullptr;
    if (!store || !techs)
        return;
    units = read_units(mod, *store);
    changes = read_changes(*techs, units);
    std::vector<long long> years;
    for (const QString &k : techs->keys())
        years.push_back(techdata::year_of(techs->text(k)));
    if (!years.empty()) {
        y0 = *std::min_element(years.begin(), years.end());
        y1 = *std::max_element(years.begin(), years.end());
    } else {
        y0 = 1836;
        y1 = 1936;
    }
    end_year.reset();
    try {                                        // the scale ends where the game does (common/defines.lua)
        static const QRegularExpression DM(uR"re(end_date\s*=\s*['"]?(\d{3,4})\.)re"_s,
                                            QRegularExpression::UseUnicodePropertiesOption);
        static const QRegularExpression SM(uR"re(start_date\s*=\s*['"]?(\d{3,4})\.)re"_s,
                                            QRegularExpression::UseUnicodePropertiesOption);
        const QString defs = store->text(u"common/defines.lua"_s);
        const auto dm = DM.match(defs);
        const auto sm = SM.match(defs);
        if (dm.hasMatch()) {
            end_year = dm.captured(1).toLongLong();
            y1 = std::max(y1, *end_year);
        }
        if (sm.hasMatch())
            y0 = std::min(y0, sm.captured(1).toLongLong());
    } catch (const std::exception &) {
    }
    if (!year) {
        const auto &date = doc->date;
        year = std::min(y1, std::max(y0, date ? static_cast<long long>(std::get<0>(*date)) : y0));
    }
    if (unit && (!units.contains(*unit) || units.get(*unit)->naval != naval))
        unit.reset();
    std::vector<std::pair<QString, QString>> items;
    QStringList tk = techs->keys();
    std::vector<std::tuple<long long, QString, QString>> keyed;
    for (const QString &k : tk)
        keyed.emplace_back(techdata::year_of(techs->text(k)), py::lower(tname(k)), k);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) {
        return std::get<0>(a) != std::get<0>(b) ? std::get<0>(a) < std::get<0>(b) : std::get<1>(a) < std::get<1>(b);
    });
    // (C++ only, the tester 2026-11-17: an invention could not be picked) EVERY invention, among the technologies
    // at the year of the latest technology its limit asks for — with or without an effect block (one is made).
    const QSet<QString> tech_set(tk.begin(), tk.end());
    long long first = 1836;
    if (!keyed.empty())
        first = std::get<0>(keyed.front());
    std::vector<std::tuple<long long, int, QString, QString>> all;   // (year, 0 tech / 1 invention, name, key)
    for (const auto &[y, l, k] : keyed)
        all.emplace_back(y, 0, l, k);
    for (const QString &k : techs->inventions()) {
        long long y = first;
        bool any = false;
        for (const QString &t : techdata::tech_refs(techs->text(k), tech_set)) {
            const long long ty = techdata::year_of(techs->text(t));
            y = any ? std::max(y, ty) : ty;
            any = true;
        }
        all.emplace_back(y, 1, py::lower(tname(k)), k);
    }
    // (C++ only, the user 2026-11-18) the sections in the technology tab's order, by year in each, technologies and
    // inventions together; the section is the file's first word (army_tech.txt, army_inventions.txt)
    auto cat_of = [this](const QString &k) {
        const QString f = pypath::basename(techs->file_of(k)).toLower();
        const qsizetype u = f.indexOf(u'_');
        return u > 0 ? f.left(u) : f.section(u'.', 0, 0);
    };
    auto cat_rank = [](const QString &c) {
        const qsizetype i = TechPopup::CATS.indexOf(c);
        return i >= 0 ? i : TechPopup::CATS.size();
    };
    std::vector<std::tuple<qsizetype, QString, long long, QString, QString, int>> sorted;   // (rank, cat, year, name, key, inv)
    for (const auto &[y, inv, l, k] : all) {
        const QString c = cat_of(k);
        sorted.emplace_back(cat_rank(c), c, y, l, k, inv);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return std::tie(std::get<0>(a), std::get<1>(a), std::get<2>(a), std::get<3>(a)) <
               std::tie(std::get<0>(b), std::get<1>(b), std::get<2>(b), std::get<3>(b));
    });
    tech_pop->entries.clear();
    for (const auto &[_r, c, y, _l, k, inv] : sorted) {
        TechPopup::Entry e{c, y, tname(k), k, inv == 1};
        items.emplace_back(TechPopup::text_of(e), k);
        tech_pop->entries.push_back(e);
    }
    bool same = static_cast<int>(items.size()) == add_tech->count();
    for (int i = 0; same && i < add_tech->count(); ++i)
        same = add_tech->itemData(i).toString() == items[static_cast<size_t>(i)].second;
    if (!same) {
        const QVariant cur = add_tech->currentData();
        add_tech->blockSignals(true);
        add_tech->clear();
        for (const auto &[t, d] : items)
            add_tech->addItem(t, d);
        add_tech->setCurrentIndex(std::max(0, cur.isValid() ? add_tech->findData(cur) : -1));
        add_tech->blockSignals(false);
        if (add_tech->lineEdit() && !add_tech->lineEdit()->hasFocus())   // (C++ only: the year first)
            add_tech->lineEdit()->setCursorPosition(0);
    }
    _show();
}

void UnitTimelineView::set_naval(bool naval_)
{
    naval = naval_;
    kind_btn[naval]->setChecked(true);
    if (unit) {
        const Unit *u = units.get(*unit);
        if (!u || u->naval != naval)
            unit.reset();
    }
    _show();
}

void UnitTimelineView::set_view(const QString &view)
{
    view_btn[view]->setChecked(true);
    stack->setCurrentIndex(view == u"graph" ? 1 : 0);
    stat_host->setVisible(view == u"graph");
    zoom_host->setVisible(view == u"graph");
    note->setText(view == u"graph" ? GRAPH_NOTE : LANES_NOTE);   // (C++ only: the note says what this view shows)
    _show();
}

void UnitTimelineView::_slide(double part)
{
    const double d = (graph->v1 - graph->v0) * part;
    graph->set_view(graph->v0 + d, graph->v1 + d);
}

void UnitTimelineView::_axis_follows()
{
    // The scale above shows the years the graph shows (all of them in the lanes).
    const bool gr = stack->currentIndex() == 1;
    if (gr) {
        axis->y0 = graph->v0;
        axis->y1 = graph->v1;
    } else {
        axis->y0 = static_cast<double>(y0);
        axis->y1 = static_cast<double>(y1);
    }
    axis->update();
    const QString end = end_year ? u" — the game ends in %1"_s.arg(*end_year) : QString();
    span_note->setText(!graph->whole() ? u"%1 – %2 of %3 – %4%5"_s.arg(QString::number(py::round_int(graph->v0)),
                                                                       QString::number(py::round_int(graph->v1)),
                                                                       QString::number(y0), QString::number(y1), end)
                                       : u"The whole time, %1 – %2%3. Wheel: zoom · drag: move · click: the year."_s.arg(
                                             QString::number(y0), QString::number(y1), end));
}

void UnitTimelineView::set_ticked(const QStringList &keys)
{
    ticked.clear();
    for (const QString &k : keys)
        if (units.contains(k))
            ticked << k;
    _show();
}

OptStr UnitTimelineView::colour_of(const QString &key) const
{
    const qsizetype i = ticked.indexOf(key);
    return i >= 0 ? OptStr(LINES[i % LINES.size()]) : std::nullopt;
}

void UnitTimelineView::_stat_toggled(const QString &stat, bool on)
{
    QStringList &cur = stats[naval];
    if (on && !cur.contains(stat))
        cur << stat;
    else if (!on && cur.contains(stat))
        cur.removeOne(stat);
    _show();
}

void UnitTimelineView::_stat_clicked(const QString &stat)
{
    // (C++ only, the user 2026-11-18: picking the stats "is not very convenient") as the nations on the map: a click
    // shows that stat alone, Ctrl+click adds it or takes it away
    QStringList &cur = stats[naval];
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        if (cur.contains(stat))
            cur.removeOne(stat);
        else
            cur << stat;
    } else {
        cur = {stat};
    }
    for (const auto &[c, b] : stat_btn) {
        b->blockSignals(true);
        b->setChecked(cur.contains(c));
        b->blockSignals(false);
    }
    _show();
}

void UnitTimelineView::_fill_stats()
{
    // The buttons of the stats the graph can draw (of land or naval units) — they combine: each one ticked is a chart
    // of its own.
    const QStringList &cols = naval ? NAVAL : GRAPH_LAND;
    QStringList have;
    for (const auto &kv : stat_btn)
        have << kv.first;
    if (have != cols) {
        for (const auto &kv : stat_btn) {
            stat_row->removeWidget(kv.second);
            kv.second->hide();
            kv.second->deleteLater();
        }
        stat_btn = {};
        for (const QString &c : cols) {
            auto *b = new QPushButton(nice(c));
            b->setCheckable(true);
            b->setStyleSheet(u"QPushButton { padding: 4px 12px; } QPushButton:checked { background: %1; color: "
                             u"#14100a; }"_s.arg(theme::GOLD));
            b->setToolTip(u"Click: a chart of this stat alone. Ctrl+click: add it to the charts or take it away."_s);
            connect(b, &QPushButton::clicked, this, [this, c](bool) { _stat_clicked(c); });
            stat_row->addWidget(b);
            stat_btn.insert(c, b);
        }
    }
    for (const auto &[c, b] : stat_btn) {
        b->blockSignals(true);
        b->setChecked(stats[naval].contains(c));
        b->blockSignals(false);
    }
}

void UnitTimelineView::_fill_graph(bool inv)
{
    const QStringList &cols = naval ? NAVAL : GRAPH_LAND;
    QStringList who;
    for (const QString &k : ticked)
        if (units.get(k)->naval == naval)
            who << k;
    QString nt;
    if (who.isEmpty() && unit)
        who = {*unit};
    if (who.isEmpty())
        nt = u"Tick units in the table below (the box before their name) — each gets a line here."_s;
    QStringList sts;
    for (const QString &c : cols)
        if (stats[naval].contains(c))
            sts << c;
    if (!who.isEmpty() && sts.isEmpty())
        nt = u"Pick the stats to draw with the buttons above."_s;
    std::vector<Panel> panels;
    for (const QString &st : sts) {
        std::vector<Line> ls;
        for (const QString &k : who)
            ls.push_back(Line{tname(k), colour_of(k).value_or(LINES[0]), series(units, changes, k, st, y0, inv)});
        panels.emplace_back(nice(st), std::move(ls));
    }
    graph->set_data(!who.isEmpty() ? std::move(panels) : std::vector<Panel>{}, y0, y1, *year, nt);
    const QString key = u"%1|%2|%3|%4"_s.arg(naval).arg(who.join(u','), sts.join(u','), inv ? u"i"_s : QString());
    if (key != _graph_key) {                     // (C++ only: the years where something happens, not the whole game)
        _graph_key = key;
        graph->fit_changes();
    }
}

void UnitTimelineView::pick(const OptStr &unit_, long long yr)
{
    if (unit_ && !unit_->isEmpty() && units.contains(*unit_))
        unit = unit_;
    year = std::min(y1, std::max(y0, yr));
    _show();
}

void UnitTimelineView::jump(int step)
{
    std::vector<Change> lst;
    if (unit)
        lst = for_unit(changes, *unit, naval, inv_box->isChecked());
    else
        for (const Change &c : changes)
            if (inv_box->isChecked() || c.kind == u"tech")
                lst.push_back(c);
    std::set<long long> ys;
    for (const Change &c : lst)
        ys.insert(c.year);
    std::vector<long long> nxt;
    for (long long y : ys)
        if (step > 0 ? y > *year : y < *year)
            nxt.push_back(y);
    if (!nxt.empty())
        pick(unit, step > 0 ? nxt.front() : nxt.back());
}

QStringList UnitTimelineView::_shown_units() const
{
    QStringList keys;
    for (const auto &[k, u] : units)
        if (u.naval == naval)
            keys << k;
    std::vector<std::pair<QString, QString>> keyed;
    for (const QString &k : keys)
        keyed.emplace_back(py::lower(tname(k)), k);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &[_l, k] : keyed)
        out << k;
    return out;
}

void UnitTimelineView::_split_moved(int, int)
{
    // The user dragged the border under the lanes / the charts: that height is theirs from now on.
    if (!_setting)
        _own_size[stack->currentIndex() == 1 ? u"graph"_s : u"lanes"_s] = split->sizes()[0];
}

void UnitTimelineView::_show()
{
    if (!techs)
        return;
    const bool inv = inv_box->isChecked();
    QStringList tk;
    for (const QString &k : ticked)
        if (units.contains(k))
            tk << k;
    ticked = tk;
    QStringList keys = _shown_units();
    if (only_box->isChecked() && std::any_of(keys.begin(), keys.end(), [&](const QString &k) { return ticked.contains(k); })) {
        QStringList only;
        for (const QString &k : keys)
            if (ticked.contains(k))
                only << k;
        keys = only;
    }
    _fill_stats();
    _fill_graph(inv);
    Marks marks;
    for (const QString &k : keys) {
        auto &m = marks[k];
        for (const Change &c : for_unit(changes, k, naval, inv))
            m[c.year].push_back(c);
    }
    std::vector<LaneRow> rws;
    for (const QString &k : keys)
        rws.push_back(LaneRow{k, tname(k), naval});
    const bool gr = stack->currentIndex() == 1;
    const QString kind = gr ? u"graph"_s : u"lanes"_s;
    const std::pair<QString, long long> want{kind, gr ? static_cast<long long>(graph->panels.size())
                                                      : static_cast<long long>(rws.size())};
    const auto mine = _own_size.find(kind);      // the height the user dragged this view to: it stays
    if (mine != _own_size.end()) {
        if (!_sized || _sized->first != kind) {
            const int full = split->height() ? split->height() : 800;
            _setting = true;
            split->setSizes({mine->second, std::max(120, full - mine->second)});
            _setting = false;
        }
        _sized = want;
    } else if (want != _sized) {                 // (the lanes / the charts get the room they need)
        _sized = want;
        const int full = split->height() ? split->height() : 800;
        const int need = gr ? std::min(std::max(440, 280 * static_cast<int>(graph->panels.size()) + 60),
                                       static_cast<int>(py::trunc_int(full * 0.68)))
                            : std::min(static_cast<int>(rws.size()) * ROW_H + 50, static_cast<int>(py::trunc_int(full * 0.6)));
        _setting = true;
        split->setSizes({need, std::max(200, full - need)});
        _setting = false;
    }
    for (Lanes *w : {lanes, axis}) {
        w->set_data(rws, marks, static_cast<double>(y0), static_cast<double>(y1));
        w->set_cursor_at(unit, *year);
    }
    lanes->colours.clear();
    for (const QString &k : keys)
        lanes->colours.insert(k, colour_of(k).value_or(QString()));
    _axis_follows();
    _fill_totals(keys, inv);
    _fill_changes(inv);
}

// ------------------------------------------------------------------ the totals at the year
void UnitTimelineView::_fill_totals(const QStringList &keys, bool inv)
{
    const QStringList &cols = naval ? NAVAL : LAND;
    // (land: after attack and defence a column of the number the game works with — + the hidden base of 10)
    std::vector<std::pair<QString, int>> spec;   // (stat, 0 plain / 1 "+ hidden 10" / 2 "calc")
    for (const QString &c : cols) {
        spec.emplace_back(c, 0);
        if (!naval) {
            if (HIDDEN.contains(c))
                spec.emplace_back(c, 1);
            else if (c == u"support") {
                spec.emplace_back(u"support_attack"_s, 2);
                spec.emplace_back(u"support_defence"_s, 2);
            }
        }
    }
    tot_title->setText(u"In %1 — every %2 unit's stats in total"_s.arg(QString::number(*year),
                                                                        naval ? u"naval"_s : u"land"_s));
    QTreeWidget *t = tot;
    t->blockSignals(true);
    t->clear();
    t->setColumnCount(static_cast<int>(spec.size()) + 1);
    QStringList heads{u"Unit"_s};
    for (const auto &[c, h] : spec)
        heads << (h == 2   ? (c == u"support_attack" ? u"Supp. attack"_s : u"Supp. defence"_s)
                  : h == 1 ? u"+ hidden 10"_s
                           : SHORT.value(c, nice(c)));
    t->setHeaderLabels(heads);
    auto *hd = static_cast<widgets::TintHeader *>(t->header());
    QSet<int> marked;
    for (size_t i = 0; i < spec.size(); ++i)
        if (spec[i].second)
            marked.insert(static_cast<int>(i) + 1);
    hd->cols = marked;
    hd->setStretchLastSection(false);
    hd->setSectionResizeMode(0, QHeaderView::Stretch);
    for (size_t j = 0; j < spec.size(); ++j) {
        const int i = static_cast<int>(j) + 1;
        const auto &[c, h] = spec[j];
        hd->setSectionResizeMode(i, QHeaderView::Fixed);
        hd->resizeSection(i, h ? 92 : 74);
        t->headerItem()->setToolTip(
            i, h == 2 ? u"From the back row: (%1 + the hidden 10) × support (100 % = × 1)"_s.arg(
                            c == u"support_attack" ? u"attack"_s : u"defence"_s)
               : h == 1 ? u"%1 + the hidden base of 10: the number the game works with"_s.arg(nice(c))
                        : nice(c));
        t->headerItem()->setTextAlignment(i, Qt::AlignCenter);
        t->headerItem()->setForeground(i, h ? QColor(HIDDEN_COL) : QColor(theme::INK));
    }
    QFont bold(t->font());
    bold.setBold(true);
    QTreeWidgetItem *pk = nullptr;
    for (const QString &k : keys) {
        const auto tt = totals(units, changes, k, *year, inv);
        auto *it = new QTreeWidgetItem(QStringList{tname(k)});
        it->setData(0, Qt::UserRole, k);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, ticked.contains(k) ? Qt::Checked : Qt::Unchecked);
        if (ticked.contains(k))
            it->setIcon(0, widgets::swatch_icon(rgb(*colour_of(k)), 14));
        for (size_t j = 0; j < spec.size(); ++j) {
            const int i = static_cast<int>(j) + 1;
            const auto &[c, hid] = spec[j];
            if (hid == 2) {                      // support attack / defence: (stat + 10) × support, that year
                const QString st = c == u"support_attack" ? u"attack"_s : u"defence"_s;
                const auto *x1 = tt.get(st);
                const auto *x2 = tt.get(u"support"_s);
                const double b1 = x1 ? x1->first : 0.0, a1 = x1 ? x1->second : 0.0;
                const double b2 = x2 ? x2->first : 0.0, a2 = x2 ? x2->second : 0.0;
                const double sf = sfac(b2 + a2);
                const double v = (b1 + a1 + HIDDEN.value(st)) * sf;
                it->setText(i, g(v));
                it->setData(i, Qt::UserRole, v);
                it->setForeground(i, QColor(HIDDEN_COL));
                it->setFont(i, bold);
                it->setToolTip(i, u"In %1: (%2 %3 + 10) × support %4 = %5 from the back row"_s.arg(
                                      QString::number(*year), st, g(b1 + a1), g(b2 + a2), g(v)));
                it->setTextAlignment(i, Qt::AlignCenter);
                continue;
            }
            const auto *x = tt.get(c);
            const double base = x ? x->first : 0.0, add = x ? x->second : 0.0;
            if (hid == 1) {                      // base (+ techs) + the hidden 10
                if (!x && !add) {
                    it->setText(i, u"—"_s);
                    it->setForeground(i, QColor(theme::INK_FAINT));
                } else {
                    const double v = base + add + HIDDEN.value(c);
                    it->setText(i, g(v));
                    it->setData(i, Qt::UserRole, v);
                    it->setForeground(i, QColor(HIDDEN_COL));
                    it->setFont(i, bold);
                    it->setToolTip(i, u"%1 in %2: %3 + the hidden base of 10 = %4 — what the game works with (support "
                                      u"multiplies this one)"_s.arg(nice(c), QString::number(*year), g(base + add), g(v)));
                }
                it->setTextAlignment(i, Qt::AlignCenter);
                continue;
            }
            if (!x && !add) {
                it->setText(i, u"—"_s);
                it->setForeground(i, QColor(theme::INK_FAINT));
            } else {
                it->setText(i, g(base + add));
                it->setData(i, Qt::UserRole, base + add);
                if (add) {
                    it->setForeground(i, QColor(theme::GOLD));
                    it->setFont(i, bold);
                    it->setToolTip(i, u"%1: base %2 %3 %4 = %5"_s.arg(nice(c), g(base), add >= 0 ? u"+"_s : u"−"_s,
                                                                      g(std::fabs(add)), g(base + add)));
                } else {
                    it->setToolTip(i, u"%1: base %2, nothing added by %3"_s.arg(nice(c), g(base), QString::number(*year)));
                }
            }
            it->setTextAlignment(i, Qt::AlignCenter);
        }
        t->addTopLevelItem(it);
        if (unit && k == *unit)
            pk = it;
    }
    if (pk)
        t->setCurrentItem(pk);
    t->blockSignals(false);
}

void UnitTimelineView::_tot_ticked(QTreeWidgetItem *it, int col)
{
    const QString k = it->data(0, Qt::UserRole).toString();
    if (col != 0 || k.isEmpty())
        return;
    const bool on = it->checkState(0) == Qt::Checked;
    if (on == ticked.contains(k))
        return;
    if (on)
        ticked << k;
    else
        ticked.removeOne(k);
    QTimer::singleShot(0, this, [this] { _show(); });   // (not while the table is handling the click)
}

void UnitTimelineView::_tot_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QString k = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!k.isEmpty() && (!unit || k != *unit))     // (not while the table is handling the selection: pick() fills
        QTimer::singleShot(0, this, [this, k] { pick(k, *year); });   // the table anew — its rows go away)
}

// ------------------------------------------------------------------ the changes of the picked unit
const UnitTimelineView::EditRow *UnitTimelineView::_edit_of(const QString &src, const std::vector<qsizetype> &ipath) const
{
    for (const EditRow &e : _edits)
        if (e.src == src && e.ipath == ipath)
            return &e;
    return nullptr;
}

void UnitTimelineView::_fill_changes(bool inv)
{
    const OptStr u = unit;
    add_btn->setEnabled(u.has_value());
    std::vector<Change> lst;
    if (!u) {
        ch_title->setText(u"Pick a unit (a lane, or a row of the table)"_s);
    } else {
        lst = for_unit(changes, *u, naval, inv);
        ch_title->setText(u"%1 — %2 change%3 by technology"_s.arg(tname(*u), QString::number(lst.size()),
                                                                  lst.size() == 1 ? QString() : u"s"_s));
        QStringList sts = naval ? NAVAL : LAND;
        QStringList vk;
        for (const auto &kv : units.get(*u)->vals)
            vk << kv.first;
        std::sort(vk.begin(), vk.end());
        sts += vk;
        for (const Change &c : lst)
            sts << c.stat;
        sts.removeDuplicates();                  // (dict.fromkeys: the first place kept)
        const QString base = naval ? u"navy_base"_s : u"army_base"_s;
        const std::vector<std::pair<QString, QString>> tg{
            {tname(*u), *u}, {naval ? u"All naval units"_s : u"All land units"_s, base}};
        bool same_t = add_target->count() == 2;
        for (int i = 0; same_t && i < 2; ++i)
            same_t = add_target->itemData(i).toString() == tg[static_cast<size_t>(i)].second;
        if (!same_t) {
            const bool was_all = add_target->currentData().toString().endsWith(u"_base"_s);
            add_target->clear();
            for (const auto &[t, d] : tg)
                add_target->addItem(t, d);
            add_target->setCurrentIndex(was_all ? 1 : 0);
        }
        QStringList have;
        for (int i = 0; i < add_stat->count(); ++i)
            have << add_stat->itemData(i).toString();
        if (have != sts) {
            const QVariant cur = add_stat->currentData();
            add_stat->clear();
            for (const QString &st : sts)
                add_stat->addItem(nice(st), st);
            add_stat->setCurrentIndex(std::max(0, cur.isValid() ? add_stat->findData(cur) : -1));
        }
    }
    Sig sig{u, {}};
    for (const Change &c : lst)
        sig.lines.emplace_back(c.src, c.ipath, c.stat, c.year);
    if (_sig && sig == *_sig) {                  // the same lines: only their numbers and the year's look
        for (EditRow &e : _edits) {
            const Change &c = *std::find_if(lst.begin(), lst.end(),
                                            [&](const Change &x) { return x.src == e.src && x.ipath == e.ipath; });
            if (!e.ed->hasFocus()) {
                e.ed->setText(g(c.value));
                e.ed->setStyleSheet(QString());
            }
            _style_row(e.labs, c);
        }
        if (_year_shown != year)
            _to_year(lst);
        _year_shown = year;
        return;
    }
    _sig = sig;
    _edits.clear();
    while (rows->count()) {
        QLayoutItem *li = rows->takeAt(0);
        QWidget *w = li->widget();
        delete li;
        if (w) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
    }
    rows->setColumnStretch(1, 1);
    for (size_t r = 0; r < lst.size(); ++r) {
        const Change &c = lst[r];
        auto *ly = new QLabel(QString::number(c.year));
        auto *ln = new QLabel(tname(c.src) + (c.kind == u"inv" ? u"  · invention"_s : QString()) +
                              (BASES.contains(c.target) ? u"  · all %1 units"_s.arg(BASES.value(c.target)) : QString()));
        ln->setToolTip(u"%1 — %2%3"_s.arg(c.src, c.kind == u"inv" ? u"invention"_s : u"technology"_s,
                                          c.after ? u", after %1"_s.arg(tname(*c.after)) : QString()));
        ln->setMinimumWidth(60);
        ln->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *ls = new QLabel(nice(c.stat));
        auto *ed = new QLineEdit(g(c.value));
        ed->setFixedWidth(70);
        ed->setAlignment(Qt::AlignRight);
        connect(ed, &QLineEdit::editingFinished, this, [this, c, ed] { set_change(c, ed); });
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setToolTip(u"Take this line out of %1"_s.arg(tname(c.src)));
        connect(x, &QPushButton::clicked, this, [this, c] { remove_change(c); });
        const int ri = static_cast<int>(r);
        int col = 0;
        for (QWidget *w : std::initializer_list<QWidget *>{ly, ln, ls, ed, x})
            rows->addWidget(w, ri, col++);
        for (QLabel *lb : {ly, ln, ls}) {        // (C++ only) a click: this line's source, target and stat go below
            lb->installEventFilter(this);
            lb->setCursor(Qt::PointingHandCursor);
        }
        ls->setToolTip(u"Click: %1, %2 and %3 go into “Add a change” below — to add another change to it"_s.arg(
            tname(c.src), BASES.contains(c.target) ? u"all %1 units"_s.arg(BASES.value(c.target)) : tname(c.target),
            py::lower(nice(c.stat))));
        _edits.push_back(EditRow{c.src, c.ipath, ed, {ly, ln, ls}, c});
        _style_row({ly, ln, ls}, c);
    }
    rows->setRowStretch(static_cast<int>(lst.size()), 1);
    _to_year(lst);
}

void UnitTimelineView::_to_year(const std::vector<Change> &lst)
{
    // The list scrolled to the changes of the picked year (or the last before it).
    std::vector<const Change *> upto;
    for (const Change &c : lst)
        if (c.year <= *year)
            upto.push_back(&c);
    if (upto.empty())
        return;
    const long long ly = upto.back()->year;
    const Change *first = *std::find_if(upto.begin(), upto.end(), [&](const Change *c) { return c->year == ly; });
    const EditRow *er = _edit_of(first->src, first->ipath);
    if (!er)
        return;
    QPointer<QLineEdit> ed(er->ed);
    const QString src = first->src;
    const std::vector<qsizetype> ipath = first->ipath;
    QTimer::singleShot(30, this, [this, ed, src, ipath] {
        const EditRow *now = _edit_of(src, ipath);
        if (ed && now && now->ed == ed.data()) {
            rows->activate();
            rows_host->adjustSize();
            rows_scroll->verticalScrollBar()->setValue(std::max(0, ed->y() - 40));
        }
    });
}

void UnitTimelineView::_style_row(const std::vector<QLabel *> &labs, const Change &c)
{
    const QString col = c.year == *year ? theme::GOLD : c.year < *year ? theme::INK : theme::INK_FAINT;
    for (QLabel *lb : labs)
        lb->setStyleSheet(u"QLabel { color: %1; font-weight: %2; }"_s.arg(col, c.year == *year ? u"700"_s : u"400"_s));
}

// ------------------------------------------------------------------ edits (into the technology's text)
void UnitTimelineView::_say(const QString &text)
{
    warn->setText(text);
    warn->setVisible(!text.isEmpty());
}

OptStr UnitTimelineView::_num(QLineEdit *ed)
{
    QString v = py::strip(ed->text()).replace(u',', u'.');
    while (v.startsWith(u'+'))
        v.remove(0, 1);
    if (!unitpanel::num_like(v)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        _say(u"A change needs a number (like 1, 0.5 or -2)."_s);
        return std::nullopt;
    }
    ed->setStyleSheet(QString());
    _say(QString());
    return v;
}

void UnitTimelineView::set_change(const Change &c, QLineEdit *ed)
{
    const OptStr v = _num(ed);
    if (!v || *py::to_float(*v) == c.value)
        return;
    const QString text = techs->text(c.src);
    on_tech_edit(u"%1: %2 %3 %4 (was %5)"_s.arg(tname(c.src), c.target, py::lower(nice(c.stat)), *v, g(c.value)), c.src,
                 techdata::set_value(text, c.ipath, *v));
}

void UnitTimelineView::remove_change(const Change &c)
{
    const QString text = techs->text(c.src);
    const std::vector<qsizetype> up(c.ipath.begin(), c.ipath.end() - 1);
    std::shared_ptr<const Node> keep;
    const Node &block = techdata::node_at(text, up, keep);
    const std::vector<qsizetype> path = block.kids.size() == 1 ? up : c.ipath;   // (its last line: the unit's block goes too)
    on_tech_edit(u"%1: %2 without %3"_s.arg(tname(c.src), c.target, py::lower(nice(c.stat))), c.src,
                 techdata::remove_node(text, path));
}

void UnitTimelineView::add_change()
{
    const OptStr u = unit;
    QString src = add_tech->currentData().toString();
    const QString stat = add_stat->currentData().toString();
    if (add_tech->currentText() != add_tech->itemText(add_tech->currentIndex())) {
        const int i = add_tech->findText(add_tech->currentText(), Qt::MatchContains);
        src = i >= 0 ? add_tech->itemData(i).toString() : QString();
    }
    const QString target = add_target->currentData().toString().isEmpty() ? (u ? *u : QString())
                                                                          : add_target->currentData().toString();
    if (target.isEmpty() || src.isEmpty() || stat.isEmpty()) {
        _say(u"Pick the technology or invention from the list first."_s);
        return;
    }
    const OptStr v = _num(add_val);
    if (!v)
        return;
    const QString text = techs->text(src);
    const auto r = techdata::root(text);
    if (!r)
        return;
    std::vector<qsizetype> base;
    const techdata::TechEntry *ent = techs->db.entries.get(src);
    const bool is_inv = (ent && ent->kind == u"inv") || techs->inventions().contains(src);
    if (is_inv)
        for (size_t i = 0; i < r->kids.size(); ++i)
            if (r->kids[i].has_key && r->kids[i].key == u"effect" && r->kids[i].is_block) {
                base = {static_cast<qsizetype>(i)};
                break;
            }
    if (is_inv && base.empty()) {                // (C++ only) an invention without an effect block: one is made
        const QString made = techdata::add_block(text, {}, u"effect"_s,
                                                 {target + u" = {"_s, u"\t"_s + stat + u" = "_s + *v, u"}"_s});
        add_val->clear();
        on_tech_edit(u"%1: %2 %3 %4%5"_s.arg(tname(src), target, py::lower(nice(stat)),
                                             *py::to_float(*v) >= 0 ? u"+"_s : QString(), *v),
                     src, made);
        return;
    }
    std::shared_ptr<const Node> keep;
    const Node &holder = !base.empty() ? techdata::node_at(text, base, keep) : *r;
    std::optional<qsizetype> at;
    for (size_t i = 0; i < holder.kids.size(); ++i)
        if (holder.kids[i].has_key && holder.kids[i].key == target && holder.kids[i].is_block) {
            at = static_cast<qsizetype>(i);
            break;
        }
    QString fresh;
    if (at) {
        const Node &blk = holder.kids[static_cast<size_t>(*at)];
        if (std::any_of(blk.kids.begin(), blk.kids.end(), [&](const Node &x) { return x.has_key && x.key == stat; })) {
            _say(u"%1 already changes %2 of %3 — pick another stat (or unit), or type the new number in its line "
                 u"above."_s.arg(tname(src), py::lower(nice(stat)),
                                 BASES.contains(target) ? u"all %1 units"_s.arg(BASES.value(target)) : tname(target)));
            return;
        }
        std::vector<qsizetype> p = base;
        p.push_back(*at);
        fresh = techdata::add_line(text, p, stat, *v);
    } else {
        fresh = techdata::add_block(text, base, target, {u"%1 = %2"_s.arg(stat, *v)});
    }
    add_val->clear();
    if (!is_inv) {
        const long long yo = techdata::year_of(text);
        year = std::min(y1, std::max(y0, yo ? yo : *year));
    }
    on_tech_edit(u"%1: %2 %3 %4%5"_s.arg(tname(src), target, py::lower(nice(stat)),
                                         *py::to_float(*v) >= 0 ? u"+"_s : QString(), *v),
                 src, fresh);
}

void UnitTimelineView::prefill(const Change &c)
{
    // (C++ only, the tester 2026-11-17) "when I click on one of these inventions or techs in the listing, I want what
    // is in there to already be in my add a change, so I can add a new one to exactly that".
    int i = add_tech->findData(c.src);
    if (i < 0)
        return;
    add_tech->setCurrentIndex(i);
    add_tech->setEditText(add_tech->itemText(i));
    i = add_target->findData(c.target);
    if (i >= 0)
        add_target->setCurrentIndex(i);
    i = add_stat->findData(c.stat);
    if (i >= 0)
        add_stat->setCurrentIndex(i);
    _say(QString());
    add_val->setFocus();
    add_val->selectAll();
}

bool UnitTimelineView::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton)
        if (auto *lb = qobject_cast<QLabel *>(o); lb && !lb->hasSelectedText())   // (marking text stays marking)
            for (const EditRow &r : _edits)
                if (std::find(r.labs.begin(), r.labs.end(), lb) != r.labs.end()) {
                    const Change c = r.c;
                    QTimer::singleShot(0, this, [this, c] { prefill(c); });
                    break;
                }
    return QWidget::eventFilter(o, e);
}

}  // namespace unittimeline
