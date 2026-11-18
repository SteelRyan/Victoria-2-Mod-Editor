// traitodds — see traitodds.h.
#include "ui/traitodds.h"
#include "core/localisation.h"
#include "ui/traitnames.h"
#include "core/oob.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "ui/drawtext.h"
#include "ui/entryview.h"
#include "ui/leaderbox.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFrame>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace traitodds {

namespace {
constexpr int KEY_ROLE = Qt::UserRole, NUM_ROLE = Qt::UserRole + 1;
// the table's column heads: written out; a column that does not fit gets the short form (the user 2026-11-18: "I don't
// know what REL is supposed to mean")
const QStringList FULL{u"Attack"_s,  u"Defence"_s,   u"Morale"_s,     u"Organisation"_s, u"Reconnaissance"_s,
                       u"Speed"_s,   u"Attrition"_s, u"Experience"_s, u"Reliability"_s};
const QStringList SHORT{u"Att"_s, u"Def"_s, u"Mor"_s, u"Org"_s, u"Recon"_s, u"Spd"_s, u"Attr"_s, u"Exp"_s, u"Reliab"_s};

class OnResize : public QObject {             // calls fn whenever the watched widget changes size
public:
    OnResize(QWidget *w, std::function<void()> f) : QObject(w), fn(std::move(f)) { w->installEventFilter(this); }
    std::function<void()> fn;
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::Resize)
            fn();
        return false;
    }
};

class Item : public QTreeWidgetItem {         // numbers sort as numbers
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(c, NUM_ROLE), b = o.data(c, NUM_ROLE);
        if (a.isValid() && b.isValid() && a.toDouble() != b.toDouble())
            return a.toDouble() < b.toDouble();
        return text(0).toLower() < o.text(0).toLower();
    }
};

// a value as the page shows it: attack / defence as they are, shares as percent
QString shown(const QString &stat, double v, bool sign = true)
{
    const QString s = sign && v > 0 ? u"+"_s : QString();
    return traits::is_percent(stat) ? s + traits::number(std::round(v * 1000) / 10) + u" %"_s : s + traits::number(v);
}
QString pct(double p)                         // small shares with two digits that count: 0.043 %, 0.0012 %
{
    if (p <= 0)
        return u"0 %"_s;
    if (p >= 0.1)
        return QString::number(p * 100, 'f', 0) + u" %"_s;
    if (p >= 0.01)
        return QString::number(p * 100, 'f', 1) + u" %"_s;
    const int d = std::clamp(1 - int(std::floor(std::log10(p * 100))), 2, 8);
    return QString::number(p * 100, 'f', d) + u" %"_s;
}
QString one_in(double p)                      // "1 in 1,523": whole leaders
{
    return p <= 0 ? u"—"_s : u"1 in "_s + QLocale(QLocale::English).toString(std::max(1LL, std::llround(1.0 / p)));
}
QString noun(const QString &list, long long n)
{
    if (n == 1)
        return list;
    return list == u"personality" ? u"personalities"_s : list + u"s"_s;
}
QColor mix(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}
}  // namespace

// ================================================================================================ the chart
OddsChart::OddsChart(bool grid_part_, QWidget *parent) : QWidget(parent), grid_part(grid_part_)
{
    setMouseTracking(true);
    setMinimumWidth(360);
}

void OddsChart::set(const traits::Lists &l, const QString &list_, const QString &stat_, const QString &sx_,
                    const QString &sy_)
{
    const bool with_none = false;             // (no_personality / no_background: never counted)
    list = list_;
    other = list == u"background" ? u"personality"_s : u"background"_s;
    stat = stat_;
    sx = sx_;
    sy = sy_;
    one = traits::dist(l.of(list), stat, with_none);
    two = traits::dist(l.of(other), stat, with_none);
    both = traits::add(traits::dist(l.personality, stat, with_none), traits::dist(l.background, stat, with_none));
    grid = traits::add2(traits::dist2(l.personality, sx, sy, with_none), traits::dist2(l.background, sx, sy, with_none));
    n_one = traits::count(l.of(list), with_none);
    n_two = traits::count(l.of(other), with_none);
    setMinimumHeight(_height());
    updateGeometry();
    update();
}

int OddsChart::_height() const
{
    std::set<long long> ys;
    for (const auto &[k, _p] : grid)
        ys.insert(k.second);
    const int rows = std::max(1, int(ys.size()));
    if (grid_part)
        return 34 + 24 + 22 + rows * std::clamp(360 / rows, 26, 36) + 12;
    return 2 * (34 + 200) + 8;
}

QSize OddsChart::sizeHint() const { return {560, _height()}; }

void OddsChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    _bars.clear();
    _tips.clear();
    const QColor gold(theme::GOLD), ink(theme::INK), dim(theme::INK_DIM), faint(theme::INK_FAINT), line(theme::LINE),
        panel(theme::PANEL_ALT), slate(110, 140, 175), bright(255, 214, 120);
    QFont f = font();
    QFont small = f;
    if (f.pointSizeF() > 0)                       // (the theme may size fonts in pixels: then no point size)
        small.setPointSizeF(f.pointSizeF() * 0.9);
    else if (f.pixelSize() > 0)
        small.setPixelSize(std::max(8, static_cast<int>(f.pixelSize() * 0.9)));
    QFont bold = f;
    bold.setBold(true);
    const double W = width() - 8;
    double y = 4;
    const QString word = traits::stat_word(stat);
    auto title = [&](const QString &text) {
        p.setFont(bold);
        p.setPen(ink);
        drawtext::draw(p, QRectF(4, y, W, 28), Qt::AlignLeft | Qt::AlignVCenter, text);
        y += 34;
    };
    // bars: one row of values, up to two bars each
    // a set of bars: its shares, colour, how many make 100 % (traits, or pairs for the two together), what they are,
    // whether a click picks its traits
    struct Set {
        const traits::Dist *d;
        QColor c;
        long long total;
        QString what;
        bool click;
    };
    const QLocale en(QLocale::English);
    auto bars = [&](const std::vector<Set> &sets) {
        std::set<long long> keys;
        for (const Set &st : sets)
            for (const auto &[k, _p] : *st.d)
                keys.insert(k);
        const QRectF area(40, y + 22, W - 44, 138);   // (room above the highest bar for its share and its number)
        double top = 0;
        for (const Set &st : sets)
            for (const auto &[_k, q] : *st.d)
                top = std::max(top, q);
        if (keys.empty() || top <= 0) {
            p.setPen(faint);
            p.setFont(f);
            drawtext::draw(p, area, Qt::AlignCenter, u"No traits in this list."_s);
            y += 200;
            return;
        }
        top = std::min(1.0, top * 1.15);
        // the scale: 0 and the top, thin lines
        p.setFont(small);
        for (const double frac : {0.0, 0.5, 1.0}) {
            const double yy = area.bottom() - frac * area.height();
            p.setPen(QPen(line, 1));
            p.drawLine(QPointF(area.left(), yy), QPointF(area.right(), yy));
            p.setPen(faint);
            drawtext::draw(p, QRectF(0, yy - 9, 36, 18), Qt::AlignRight | Qt::AlignVCenter, pct(top * frac));
        }
        const int n = int(keys.size());
        const double slot = area.width() / n;
        const int m = int(sets.size());
        const double bw = std::min(48.0, slot * 0.8 / m);
        int i = 0;
        for (const long long k : keys) {
            const double cx = area.left() + slot * (i + 0.5);
            for (int j = 0; j < m; ++j) {
                const Set &st = sets[size_t(j)];
                const traits::Dist *d = st.d;
                const QColor &c = st.c;
                const auto it = d->find(k);
                const double q = it == d->end() ? 0.0 : it->second;
                const double h = q / top * area.height();
                const QRectF r(cx - bw * m / 2 + j * bw + 1.5, area.bottom() - h, bw - 3, h);
                p.setPen(QPen(QColor(20, 20, 24), 2));
                p.setBrush(c);
                if (h > 0)
                    p.drawRect(r);
                const long long many = std::llround(q * double(st.total));   // (the share as a count)
                if (q > 0 && slot >= 34) {
                    p.setPen(ink);
                    p.setFont(small);
                    drawtext::draw(p, QRectF(r.center().x() - 34, r.top() - 32, 68, 15), Qt::AlignCenter, pct(q));
                    p.setPen(dim);
                    drawtext::draw(p, QRectF(r.center().x() - 34, r.top() - 17, 68, 15), Qt::AlignCenter,
                                   en.toString(many));
                }
                if (st.click)
                    _bars.emplace_back(QRectF(cx - slot / 2, area.top(), slot, area.height() + 22), k);
                _tips.emplace_back(QRectF(cx - slot / 2, area.top(), slot, area.height() + 22),
                                   u"%1 %2: %3 — %4 of %5 %6"_s.arg(word, shown(stat, traits::vval(k)), pct(q),
                                                                     en.toString(many), en.toString(st.total), st.what));
            }
            p.setPen(dim);
            p.setFont(n > 14 ? small : f);
            if (slot >= 22 || i % 2 == 0)
                drawtext::draw(p, QRectF(cx - slot, area.bottom() + 3, slot * 2, 18), Qt::AlignHCenter | Qt::AlignTop,
                               shown(stat, traits::vval(k)));
            ++i;
        }
        y += 200;
    };
    const QString L = list == u"background" ? u"Background"_s : u"Personality"_s,
                  O = other == u"background" ? u"background"_s : u"personality"_s;
    auto plural = [](const QString &x) { return x == u"personality" ? u"personalities"_s : x + u"s"_s; };
    if (!grid_part) {
        const QString Word = word.at(0).toUpper() + word.mid(1);
        const QString mine = plural(L.toLower()), theirs = plural(O);
        const bool see_one = lists == u"both" || lists == list, see_two = lists == u"both" || lists == other;
        if (see_one && see_two)
            title(u"%1 of one list — gold: the %2 you edit (%3), blue: the %4 (%5)"_s.arg(Word, mine).arg(n_one)
                      .arg(theirs).arg(n_two));
        else if (see_one)
            title(u"%1 of the %2 (%3) — the list you edit"_s.arg(Word, mine).arg(n_one));
        else
            title(u"%1 of the %2 (%3)"_s.arg(Word, theirs).arg(n_two));
        std::vector<Set> sets;
        if (see_one)
            sets.push_back({&one, gold, n_one, mine, true});
        if (see_two)
            sets.push_back({&two, slate, n_two, theirs, false});
        bars(sets);
        title(u"Together: a new leader gets one personality and one background — the leader's %1"_s.arg(word));
        bars({{&both, bright, n_one * n_two, u"pairs of personality and background"_s, false}});
        return;
    }
    // the grid: two stats together, each square "1 in N" leaders
    const QString wx = traits::stat_word(sx), wy = traits::stat_word(sy);
    auto cap = [](const QString &w) { return w.isEmpty() ? w : w.at(0).toUpper() + w.mid(1); };
    title(u"Together: %1 across × %2 down — each square: one leader in how many gets this pair"_s.arg(wx, wy));
    std::set<long long> xs, ys;
    double top = 0;
    for (const auto &[k, q] : grid) {
        xs.insert(k.first);
        ys.insert(k.second);
        top = std::max(top, q);
    }
    if (xs.empty())
        return;
    const double lx = 92;                         // (the down label stands left of the row values)
    const double cw = std::clamp((W - lx) / double(xs.size()), 18.0, 96.0);
    const double ch = std::clamp(360.0 / double(ys.size()), 26.0, 36.0);
    // the axis names: across above the column values, down turned on the left
    p.setFont(bold);
    p.setPen(gold);
    drawtext::draw(p, QRectF(lx, y, cw * double(xs.size()), 22), Qt::AlignCenter, cap(wx) + u" →"_s);
    y += 24;
    {
        const double gh = 22 + ch * double(ys.size());
        p.save();
        p.translate(14, y + gh / 2);
        p.rotate(-90);
        drawtext::draw(p, QRectF(-gh / 2, -11, gh, 22), Qt::AlignCenter, cap(wy) + u" →"_s);
        p.restore();
    }
    p.setFont(small);
    int i = 0;
    for (const long long x : xs) {
        p.setPen(dim);
        drawtext::draw(p, QRectF(lx + i * cw, y, cw, 20), Qt::AlignCenter, shown(sx, traits::vval(x)));
        ++i;
    }
    y += 22;
    int r = 0;
    for (auto yit = ys.rbegin(); yit != ys.rend(); ++yit, ++r) {   // (the largest at the top)
        p.setPen(dim);
        drawtext::draw(p, QRectF(28, y + r * ch, lx - 34, ch), Qt::AlignRight | Qt::AlignVCenter, shown(sy, traits::vval(*yit)));
        int c = 0;
        for (const long long x : xs) {
            const auto it = grid.find({x, *yit});
            const double q = it == grid.end() ? 0.0 : it->second;
            const QRectF cell(lx + c * cw, y + r * ch, cw, ch);
            p.setPen(QPen(QColor(20, 20, 24), 2));
            p.setBrush(q > 0 ? mix(panel, gold, 0.18 + 0.82 * std::sqrt(q / top)) : panel);
            p.drawRect(cell.adjusted(1, 1, -1, -1));
            if (q > 0) {                          // "1 in N"; just N where the square is too narrow
                p.setPen(q / top > 0.45 ? QColor(20, 20, 24) : ink);
                const QString full = one_in(q), bare = full.mid(5);
                const QFontMetricsF fm(small);
                const QString t = fm.horizontalAdvance(full) <= cw - 6 ? full : bare;
                if (fm.horizontalAdvance(t) <= cw - 2)
                    drawtext::draw(p, cell, Qt::AlignCenter, t);
            }
            _tips.emplace_back(cell, u"%1 %2 and %3 %4: %5 leaders (%6)"_s.arg(cap(wx), shown(sx, traits::vval(x)), wy,
                                                                             shown(sy, traits::vval(*yit)), one_in(q), pct(q)));
            ++c;
        }
    }
}

void OddsChart::mousePressEvent(QMouseEvent *e)
{
    for (const auto &[r, k] : _bars)
        if (r.contains(e->position())) {
            Q_EMIT barClicked(traits::vval(k));
            return;
        }
}

void OddsChart::mouseMoveEvent(QMouseEvent *e)
{
    for (const auto &[r, t] : _tips)
        if (r.contains(e->position())) {
            QToolTip::showText(e->globalPosition().toPoint(), t, this);
            return;
        }
    QToolTip::hideText();
}

// ================================================================================================ the view
OddsView::OddsView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- left: the list and its mass edit
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *s = new widgets::Section();
    auto *sr = new QHBoxLayout();
    sr->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const QString &k : traits::LISTS) {
        auto *b = new QPushButton(k == u"personality" ? u"Personalities"_s : u"Backgrounds"_s);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == list);
        connect(b, &QPushButton::clicked, this, [this, k] {
            list = k;
            sel.clear();
            _fill();
        });
        grp->addButton(b);
        sr->addWidget(b, 1);
        list_btns.emplace_back(k, b);
    }
    s->lay->addLayout(sr);
    s->lay->addWidget(label(u"Pick many rows (Ctrl / Shift + click, or click a bar of the chart), then change them all at "
                            u"once. A new leader gets one personality and one background; every trait of a list counts as "
                            u"equally likely (one in as many as the list has — not measured in the game)."_s,
                            u"faint"_s));
    table = new QTreeWidget();
    table->setColumnCount(10);
    QStringList heads{u"Trait"_s};
    heads += FULL;
    table->setHeaderLabels(heads);
    for (int c = 1; c < 10; ++c)
        table->headerItem()->setToolTip(c, traits::STATS[c - 1]);
    table->setRootIsDecorated(false);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setTextElideMode(Qt::ElideRight);
    table->setSortingEnabled(true);
    table->sortByColumn(0, Qt::AscendingOrder);
    QHeaderView *h = table->header();
    h->setStretchLastSection(false);
    h->setSectionsMovable(true);                  // (drag a column head to move the column; the trait stays first)
    h->setFirstSectionMovable(false);
    new OnResize(table, [this] { _heads(); });
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 10; ++c)
        h->setSectionResizeMode(c, QHeaderView::Fixed);   // (sized by _heads: the trait column keeps its room)
    table->setItemDelegateForColumn(0, new traitnames::ColourDelegate(table));   // ("§R-1 Attack" in red)
    table->setMinimumHeight(300);
    connect(table, &QTreeWidget::itemSelectionChanged, this, [this] { _picked(); });
    s->lay->addWidget(table, 1);
    sel_note = label(u""_s, u"sub"_s);
    sel_note->setTextFormat(Qt::RichText);
    s->lay->addWidget(sel_note);
    ll->addWidget(s, 1);

    s = new widgets::Section();
    s->lay->addWidget(label(u"Change the picked traits"_s));
    auto *r1 = new QHBoxLayout();
    r1->setSpacing(6);
    stat_box = new QComboBox();
    for (const QString &st : traits::STATS)
        stat_box->addItem(st, st);
    value = new QLineEdit();
    value->setPlaceholderText(u"value as in the file: 1, 0.05"_s);
    set_btn = new QPushButton(u"Set to"_s);
    add_btn = new QPushButton(u"Add"_s);
    clear_btn = new QPushButton(u"Clear"_s);
    set_btn->setToolTip(u"The picked traits get this value (0 takes the line away)."_s);
    add_btn->setToolTip(u"This value is added to what the picked traits have (a minus value takes away)."_s);
    clear_btn->setToolTip(u"The picked traits lose this stat."_s);
    connect(set_btn, &QPushButton::clicked, this, [this] { mass_set(u"set"_s); });
    connect(add_btn, &QPushButton::clicked, this, [this] { mass_set(u"add"_s); });
    connect(clear_btn, &QPushButton::clicked, this, [this] { mass_set(u"clear"_s); });
    connect(value, &QLineEdit::returnPressed, this, [this] { mass_set(u"set"_s); });
    r1->addWidget(stat_box, 2);
    r1->addWidget(value, 2);
    r1->addWidget(set_btn, 1);
    r1->addWidget(add_btn, 1);
    r1->addWidget(clear_btn, 1);
    s->lay->addLayout(r1);
    auto *r2 = new QHBoxLayout();
    r2->setSpacing(6);
    copies = new widgets::Stepper(1, 99, 1);
    copy_btn = new QPushButton(u"⧉ Copies of each"_s);
    copy_btn->setToolTip(u"Each picked trait gets this many copies (its name with _2, _3 …) — two of a kind are twice as "
                         u"likely to be rolled."_s);
    connect(copy_btn, &QPushButton::clicked, this, [this] { copy_selected(); });
    del_btn = new QPushButton(u"✕ Delete the picked"_s);
    del_btn->setToolTip(u"Leaders at the start who have one of them get no_personality / no_background instead."_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { delete_selected(); });
    r2->addWidget(copies);
    r2->addWidget(copy_btn, 1);
    r2->addWidget(del_btn, 1);
    s->lay->addLayout(r2);
    auto *r3 = new QHBoxLayout();
    r3->setSpacing(6);
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"name of a new trait"_s);
    new_btn = new QPushButton(u"＋ New trait"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { new_trait(); });
    connect(new_name, &QLineEdit::returnPressed, this, [this] { new_trait(); });
    r3->addWidget(new_name, 2);
    r3->addWidget(new_btn, 1);
    s->lay->addLayout(r3);
    warn = label(u""_s, u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    ll->addWidget(s);
    lay->addWidget(left, 5);

    // --- right: the odds
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    // two sections, one under the other with a line between them: the bars (what they show, then the bars) and the
    // grid (its two stats, then the grid) — the user 2026-11-18
    chart_stat = new QComboBox();
    grid_x = new QComboBox();
    grid_y = new QComboBox();
    for (QComboBox *b : {chart_stat, grid_x, grid_y})
        for (const QString &st : traits::STATS)
            b->addItem(st, st);
    grid_y->setCurrentIndex(1);
    for (QComboBox *b : {chart_stat, grid_x, grid_y})
        connect(b, &QComboBox::currentIndexChanged, this, [this](int) { _chart(); });
    auto *body = new QWidget();
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(6);
    auto *br = new QHBoxLayout();
    br->setSpacing(6);
    br->addWidget(label(u"Bars show"_s, u"sub"_s, false));
    br->addWidget(chart_stat, 1);
    bar_lists = new QComboBox();
    bar_lists->addItem(u"of both lists"_s, u"both"_s);
    bar_lists->addItem(u"of the personalities only"_s, u"personality"_s);
    bar_lists->addItem(u"of the backgrounds only"_s, u"background"_s);
    connect(bar_lists, &QComboBox::currentIndexChanged, this, [this](int) { _chart(); });
    br->addWidget(bar_lists, 1);
    br->addStretch(1);
    bl->addLayout(br);
    chart = new OddsChart(false);
    connect(chart, &OddsChart::barClicked, this, [this](double v) { pick_value(v); });
    bl->addWidget(chart);
    bl->addSpacing(10);
    auto *divider = new QFrame();
    divider->setFrameShape(QFrame::NoFrame);
    divider->setFixedHeight(3);
    divider->setStyleSheet(u"background: %1;"_s.arg(theme::GOLD));
    bl->addWidget(divider);
    bl->addSpacing(10);
    auto *gr = new QHBoxLayout();
    gr->setSpacing(6);
    gr->addWidget(label(u"Grid: across"_s, u"sub"_s, false));
    gr->addWidget(grid_x, 1);
    gr->addWidget(label(u"down"_s, u"sub"_s, false));
    gr->addWidget(grid_y, 1);
    bl->addLayout(gr);
    grid_chart = new OddsChart(true);
    bl->addWidget(grid_chart);
    bl->addStretch(1);
    s = new widgets::Section();
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    s->lay->addWidget(scroll, 1);
    rl->addWidget(s, 1);
    lay->addWidget(right, 6);
}

QString OddsView::_text() const
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return {};
    return doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(u"common/traits.txt"_s);
}

QString OddsView::_tname(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const auto t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t && !t->isEmpty())
            return *t;
    return QString(key).replace(u'_', u' ');
}

void OddsView::refresh(document::Document *d)
{
    doc = d;
    const QString text = _text();
    if (_sig && *_sig == text)
        return;
    _sig = text;
    lists = traits::parse(text);
    _fill();
}

void OddsView::_fill()
{
    _filling = true;
    for (const auto &[k, b] : list_btns) {
        b->setChecked(k == list);
        b->setText(u"%1 (%2)"_s.arg(k == u"personality" ? u"Personalities"_s : u"Backgrounds"_s)
                       .arg(traits::count(lists.of(k), false)));
    }
    table->setSortingEnabled(false);
    table->blockSignals(true);
    table->clear();
    for (const traits::Trait &t : lists.of(list)) {
        auto *it = new Item(table);
        const QString nm = _tname(t.key);
        it->setText(0, nm.toLower() != QString(t.key).replace(u'_', u' ').toLower() ? u"%1  (%2)"_s.arg(nm, t.key) : nm);
        it->setToolTip(0, u"%1 — %2"_s.arg(t.key, traits::summary(t)));
        it->setData(0, KEY_ROLE, t.key);
        for (int c = 1; c < 10; ++c) {
            const QString &st = traits::STATS[c - 1];
            const double v = t.get(st);
            it->setText(c, v != 0 ? shown(st, v) : QString());
            it->setData(c, NUM_ROLE, v);
            it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
            if (v != 0)
                it->setForeground(c, QColor(v > 0 ? theme::GOLD : theme::DANGER));
        }
        if (sel.contains(t.key))
            it->setSelected(true);
    }
    table->setSortingEnabled(true);
    table->blockSignals(false);
    _filling = false;
    _heads();
    _picked();
    _chart();
}

void OddsView::_picked()
{
    if (!_filling) {
        sel.clear();
        for (QTreeWidgetItem *it : table->selectedItems())
            sel.insert(it->data(0, KEY_ROLE).toString());
    }
    const long long n = traits::count(lists.of(list), false);   // (no_personality / no_background never counted)
    long long k = 0;
    for (const QString &key : sel)
        k += traits::is_none(key) ? 0 : 1;
    const QString many = list == u"personality" ? u"personalities"_s : u"backgrounds"_s;
    sel_note->setText(k ? u"<b>%1 picked</b> of %2 — together %3 of all %4"_s.arg(k).arg(n).arg(
                              pct(double(k) / double(std::max(1LL, n))), many)
                        : u"%1 %2. Pick rows to change them."_s.arg(n).arg(many));
    for (QPushButton *b : {set_btn, add_btn, clear_btn, copy_btn, del_btn})
        b->setEnabled(!sel.isEmpty());
}

void OddsView::_heads()
{
    // every stat column written out, as long as the trait column keeps room; else the columns that win the most get the
    // short form, one by one
    const QFontMetrics fm(table->header()->font());
    const int room = table->viewport()->width() - 170;   // (the trait column keeps 170 px)
    std::vector<int> full(10, 0), part(10, 0);
    std::vector<bool> is_short(10, false);
    int used = 0;
    const QFontMetrics bm(table->font());
    for (int c = 1; c < 10; ++c) {
        int body = 0;                             // (the widest value of the column)
        for (int i = 0; i < table->topLevelItemCount(); ++i)
            body = std::max(body, bm.horizontalAdvance(table->topLevelItem(i)->text(c)) + 16);
        full[size_t(c)] = std::max(body, fm.horizontalAdvance(FULL[c - 1]) + 24);
        part[size_t(c)] = std::max(body, fm.horizontalAdvance(SHORT[c - 1]) + 24);
        used += full[size_t(c)];
    }
    while (used > room) {
        int best = -1, gain = 0;
        for (int c = 1; c < 10; ++c)
            if (!is_short[size_t(c)] && full[size_t(c)] - part[size_t(c)] > gain) {
                gain = full[size_t(c)] - part[size_t(c)];
                best = c;
            }
        if (best < 0)
            break;
        is_short[size_t(best)] = true;
        used -= gain;
    }
    QTreeWidgetItem *hd = table->headerItem();
    for (int c = 1; c < 10; ++c) {
        const QString t = is_short[size_t(c)] ? SHORT[c - 1] : FULL[c - 1];
        if (hd->text(c) != t)
            hd->setText(c, t);
        const int w = is_short[size_t(c)] ? part[size_t(c)] : full[size_t(c)];
        if (table->header()->sectionSize(c) != w)
            table->header()->resizeSection(c, w);
    }
}

void OddsView::_chart()
{
    chart->lists = bar_lists->currentData().toString();
    for (OddsChart *c : {chart, grid_chart})
        c->set(lists, list, chart_stat->currentData().toString(), grid_x->currentData().toString(),
               grid_y->currentData().toString());
}

void OddsView::pick_value(double v)
{
    const QString st = chart_stat->currentData().toString();
    table->blockSignals(true);
    table->clearSelection();
    for (int i = 0; i < table->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = table->topLevelItem(i);
        if (const traits::Trait *t = lists.find(it->data(0, KEY_ROLE).toString()); t && traits::vkey(t->get(st)) == traits::vkey(v))
            it->setSelected(true);
    }
    table->blockSignals(false);
    _picked();
    stat_box->setCurrentIndex(stat_box->findData(st));
}

QString OddsView::_tname_raw(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const auto t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t)
            return *t;
    return QString();
}

void OddsView::_commit(const QString &lbl, const QString &text, const std::vector<std::pair<QString, QString>> &names_in,
                       const QSet<QString> &gone)
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    oob::OOBEdits *ob = doc->maps.contains(u"oob"_s) ? &doc->map<oob::OOBEdits>(u"oob"_s) : nullptr;
    QStringList lists_touched{u"scripts"_s};
    std::vector<std::pair<QString, QString>> names = names_in;
    if (loc) {
        const traitnames::Rules rules = traitnames::for_mod(mod, lists, [this](const QString &k) { return _tname_raw(k); });
        if (rules.auto_names) {
            const traits::Lists now = traits::parse(text);
            for (const QString &l : traits::LISTS)
                for (const traits::Trait &t : now.of(l)) {
                    if (traits::is_none(t.key))
                        continue;
                    const traits::Trait *was = lists.find(t.key);
                    if (was && was->v == t.v)
                        continue;          // (unchanged)
                    std::erase_if(names, [&](const auto &p) { return p.first == t.key; });
                    names.emplace_back(t.key, traitnames::name_of(t, rules));
                }
        }
    }
    if (loc && !names.empty())
        lists_touched << u"loc"_s;
    long long moved = 0;
    // leaders at the start who had a trait that goes: no_personality / no_background (one step with the rest)
    std::vector<std::pair<QString, std::function<void(oob::OOB &)>>> fixes;
    if (ob && !gone.isEmpty()) {
        const QString none = list == u"background" ? u"no_background"_s : u"no_personality"_s;
        for (const auto &[t, _c] : mod.countries) {
            if (!ob->exists(t) && !ob->want.contains(t))
                continue;
            const oob::OOB &st = ob->state(t);
            long long here = 0;
            for (const oob::Leader &l : st.leaders)
                here += gone.contains(l.get(list));
            for (const oob::Army &a : st.armies)
                here += gone.contains(leaderbox::leader_of(a.extra).second.value(list));
            for (const oob::Navy &v : st.navies)
                here += gone.contains(leaderbox::leader_of(v.extra).second.value(list));
            if (!here)
                continue;
            moved += here;
            fixes.emplace_back(t, [gone, none, key = list](oob::OOB &o) {
                for (oob::Leader &l : o.leaders)
                    if (gone.contains(l.get(key)))
                        l.set(key, none);
                for (QStringList *ex : [&] {
                         std::vector<QStringList *> v;
                         for (oob::Army &a : o.armies)
                             v.push_back(&a.extra);
                         for (oob::Navy &n : o.navies)
                             v.push_back(&n.extra);
                         return v;
                     }()) {
                    auto [li, f] = leaderbox::leader_of(*ex);
                    if (li && gone.contains(f.value(key))) {
                        f[key] = none;
                        (*ex)[*li] = leaderbox::leader_text(f);
                    }
                }
            });
        }
        if (!fixes.empty())
            lists_touched << u"oob"_s;
    }
    doc->edit(lbl, {}, [&] {
        store.set_text(u"common/traits.txt"_s, text);
        if (loc)
            for (const auto &[k, t] : names)
                loc->set(k, t);
        for (const auto &[t, fn] : fixes)
            ob->edit(t, fn);
    }, {}, {}, lists_touched);
    Q_EMIT message(u"%1%2 — written to common/traits.txt on Apply (Ctrl+Z undoes it)."_s.arg(
        lbl, moved ? u"; %1 leader%2 at the start had one of them and got %3 instead"_s.arg(moved).arg(
                         moved == 1 ? QString() : u"s"_s, list == u"background" ? u"no_background"_s : u"no_personality"_s)
                   : QString()));
    refresh(doc);
}

void OddsView::mass_set(const QString &how)
{
    warn->clear();
    if (sel.isEmpty())
        return;
    const QString st = stat_box->currentData().toString();
    std::optional<double> v;
    if (how != u"clear") {
        bool ok = false;
        const double d = py::strip(value->text()).replace(u',', u'.').toDouble(&ok);
        if (!ok || !std::isfinite(d)) {
            warn->setText(u"Type a number first (as the file writes it: 1 for attack, 0.05 for 5 % speed)."_s);
            return;
        }
        v = d;
    }
    const QString text = how == u"add" ? traits::add_to_stat(_text(), list, sel, st, *v)
                                       : traits::set_stat(_text(), list, sel, st, how == u"set" ? v : std::nullopt);
    if (text == _text()) {
        warn->setText(u"Nothing changed: the picked traits have that already."_s);
        return;
    }
    const QString what = how == u"set"   ? u"%1 set to %2"_s.arg(st, traits::number(*v))
                         : how == u"add" ? u"%1 %2"_s.arg(st, (*v > 0 ? u"+"_s : QString()) + traits::number(*v))
                                         : u"%1 cleared"_s.arg(st);
    _commit(u"Leader traits: %1 for %2 %3"_s.arg(what).arg(sel.size()).arg(noun(list, sel.size())),
            text, {}, {});
}

void OddsView::copy_selected()
{
    warn->clear();
    if (sel.isEmpty())
        return;
    QStringList keys;
    for (const traits::Trait &t : lists.of(list))   // (in the file's order)
        if (sel.contains(t.key))
            keys << t.key;
    std::vector<std::pair<QString, QString>> made;
    const QString text = traits::copy_traits(_text(), list, keys, copies->value(), &made);
    std::vector<std::pair<QString, QString>> names;
    for (const auto &[k, of] : made) {
        const QString nm = _tname(of);
        if (nm != QString(of).replace(u'_', u' '))
            names.emplace_back(k, nm);            // a copy shows the same name in the game
    }
    _commit(u"Leader traits: %1 cop%2 of %3 %4"_s.arg(made.size()).arg(made.size() == 1 ? u"y"_s : u"ies"_s)
                .arg(keys.size()).arg(noun(list, keys.size())),
            text, names, {});
}

void OddsView::delete_selected()
{
    warn->clear();
    if (sel.isEmpty())
        return;
    QSet<QString> gone = sel;
    gone.remove(u"no_personality"_s);
    gone.remove(u"no_background"_s);
    if (gone.isEmpty()) {
        warn->setText(u"no_personality / no_background stay: leaders without a trait use them."_s);
        return;
    }
    const QString text = traits::remove_traits(_text(), list, gone);
    sel.clear();
    _commit(u"Leader traits: %1 %2 deleted"_s.arg(gone.size()).arg(noun(list, gone.size())),
            text, {}, gone);
}

void OddsView::new_trait()
{
    warn->clear();
    const QString nm = py::split(new_name->text()).join(u' ');
    QString key = nm.toLower();
    key.replace(QRegularExpression(u"[^a-z0-9_]+"_s), u"_"_s);
    key = py::strip(key, u"_"_s);
    if (key.isEmpty()) {
        warn->setText(u"Type the new trait's name first."_s);
        return;
    }
    key = traits::free_key(lists, key);
    const QString text = traits::new_trait(_text(), list, key);
    sel = {key};
    new_name->clear();
    _commit(u"Leader traits: new %1 %2"_s.arg(list, key), text, {{key, nm}}, {});
}

// ================================================================================================ the page
TraitsPage::TraitsPage(moddata::Mod &mod, QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 8, 16, 4);
    bl->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[k, text] : {std::pair{u"odds"_s, u"Odds && mass edit"_s}, std::pair{u"names"_s, u"Names && colours"_s},
                                  std::pair{u"one"_s, u"One trait"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == _view);
        connect(b, &QPushButton::clicked, this, [this, k] { set_view(k); });
        grp->addButton(b);
        bl->addWidget(b, 1);
        seg.emplace_back(k, b);
    }
    lay->addWidget(bar);
    stack = new QStackedWidget();
    odds = new OddsView(mod);
    entry = new entryview::EntryView(mod, entryview::TRAITS);
    names = new traitnames::NamesView(mod);
    stack->addWidget(odds);
    stack->addWidget(names);
    stack->addWidget(entry);
    // (in a scroll area that scrolls up and down only: the switch above must not make the whole Warfare tab taller
    // than its other pages need — on a low window the page scrolls instead)
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setWidget(stack);
    lay->addWidget(sc, 1);
    connect(odds, &OddsView::message, this, &TraitsPage::message);
    connect(names, &traitnames::NamesView::message, this, &TraitsPage::message);
    connect(entry, &entryview::EntryView::scriptEdit, this, &TraitsPage::scriptEdit);
    connect(entry, &entryview::EntryView::nameEdit, this, &TraitsPage::nameEdit);
    connect(entry, &entryview::EntryView::fileRemove, this, &TraitsPage::fileRemove);
    connect(entry, &entryview::EntryView::bigRequested, this, &TraitsPage::bigRequested);
}

void TraitsPage::set_view(const QString &key)
{
    _view = key;
    for (const auto &[k, b] : seg)
        b->setChecked(k == key);
    stack->setCurrentWidget(key == u"one" ? static_cast<QWidget *>(entry) : key == u"names" ? static_cast<QWidget *>(names) : odds);
    if (doc)
        refresh(doc);
}

void TraitsPage::refresh(document::Document *d)
{
    doc = d;
    if (_view == u"one")
        entry->refresh(d);
    else if (_view == u"names")
        names->refresh(d);
    else
        odds->refresh(d);
}

}  // namespace traitodds
