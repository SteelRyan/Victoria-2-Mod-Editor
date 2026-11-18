// regionimport.py, its page half — line by line.
#include "ui/regionimport.h"

#include "core/pynum.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using widgets::label;

namespace regionimport {

static QString s_if(size_t n) { return n == 1 ? QString() : u"s"_s; }

// ---------------------------------------------------------------------------------------------- the picture
SourceMap::SourceMap(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setMinimumSize(420, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

void SourceMap::set_map(const grid::Grid<std::uint16_t> *ids_, const std::vector<Rgb8> &lut_, bool keep_view)
{
    const bool fresh = !ids || ids->h() != ids_->h() || ids->w() != ids_->w() || !keep_view;
    ids = ids_;
    lut = lut_;
    if (fresh)
        fit();
    update();
}

void SourceMap::fit()
{
    if (!ids)
        return;
    const double h = double(ids->h()), w = double(ids->w());
    scale = std::max(0.02, std::min(width() / w, height() / h));
    ox = (w - width() / scale) / 2.0;
    oy = (h - height() / scale) / 2.0;
    update();
}

// Zoom to a part of the map (with room around it).
void SourceMap::show_box(double x0, double y0, double x1, double y1)
{
    if (!ids)
        return;
    const double bw = std::max(40.0, (x1 - x0) * 3.0), bh = std::max(40.0, (y1 - y0) * 3.0);
    scale = std::max(0.02, std::min(24.0, std::min(width() / bw, height() / bh)));
    ox = (x0 + x1) / 2.0 - width() / scale / 2.0;
    oy = (y0 + y1) / 2.0 - height() / scale / 2.0;
    _last = 1;
    update();
}

void SourceMap::resizeEvent(QResizeEvent *e)
{
    if (ids && _last == 0)
        fit();
    QWidget::resizeEvent(e);
}

long long SourceMap::at(const QPointF &pos) const
{
    if (!ids)
        return 0;
    const long long x = static_cast<long long>(ox + pos.x() / scale), y = static_cast<long long>(oy + pos.y() / scale);
    return 0 <= x && x < ids->w() && 0 <= y && y < ids->h() ? (*ids)(y, x) : 0;
}

static std::uint8_t u8(double v) { return static_cast<std::uint8_t>(static_cast<long long>(v)); }   // (astype(uint8))

void SourceMap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(theme::BG));
    if (!ids)
        return;
    const long long h = ids->h(), w = ids->w();
    const int W = width(), H = height();
    std::vector<long long> xs(size_t(std::max(W, 0))), ys(size_t(std::max(H, 0)));
    for (int i = 0; i < W; ++i)
        xs[size_t(i)] = static_cast<long long>(std::floor(ox + (i + 0.5) / scale));
    for (int i = 0; i < H; ++i)
        ys[size_t(i)] = static_cast<long long>(std::floor(oy + (i + 0.5) / scale));
    std::vector<long long> vx, vy;               // (xs[vx], ys[vy]: the screen columns / rows on the map)
    int first_x = -1, first_y = -1;
    for (int i = 0; i < W; ++i)
        if (xs[size_t(i)] >= 0 && xs[size_t(i)] < w) {
            if (first_x < 0)
                first_x = i;
            vx.push_back(xs[size_t(i)]);
        }
    for (int i = 0; i < H; ++i)
        if (ys[size_t(i)] >= 0 && ys[size_t(i)] < h) {
            if (first_y < 0)
                first_y = i;
            vy.push_back(ys[size_t(i)]);
        }
    if (vx.empty() || vy.empty())
        return;
    const size_t SW = vx.size(), SH = vy.size(), N = SW * SH;
    std::vector<std::uint16_t> sub(N);
    for (size_t r = 0; r < SH; ++r)
        for (size_t c = 0; c < SW; ++c)
            sub[r * SW + c] = (*ids)(vy[r], vx[c]);
    std::vector<Rgb8> rgb(N);
    for (size_t i = 0; i < N; ++i)
        rgb[i] = lut[sub[i]];
    auto grp = [&](size_t i) -> long long { return edge_ids ? (*edge_ids)[sub[i]] : sub[i]; };
    std::vector<std::uint8_t> edge(N, 0);
    for (size_t r = 0; r < SH; ++r)
        for (size_t c = 0; c < SW; ++c) {
            const size_t i = r * SW + c;
            if ((c > 0 && grp(i) != grp(i - 1)) || (r > 0 && grp(i) != grp(i - SW)))
                edge[i] = 1;
        }
    for (size_t i = 0; i < N; ++i)
        if (edge[i])
            for (auto &ch : rgb[i])
                ch = u8(ch * edge_dark);
    static const double GOLD[3] = {255, 216, 74};
    if (!picked.empty()) {
        std::vector<std::uint8_t> sel(lut.size(), 0);
        for (long long q : picked)
            if (q < static_cast<long long>(sel.size()))
                sel[size_t(q)] = 1;
        std::vector<std::uint8_t> m(N);
        for (size_t i = 0; i < N; ++i)
            m[i] = sel[sub[i]];
        if (pick_outline) {   // its colour stays (it says something): a white line round it
            std::vector<std::uint8_t> rim(N, 0);
            for (size_t r = 0; r < SH; ++r)
                for (size_t c = 0; c < SW; ++c) {
                    const size_t i = r * SW + c;
                    if ((c > 0 && m[i] != m[i - 1]) || (r > 0 && m[i] != m[i - SW]))
                        rim[i] = 1;
                }
            std::vector<std::uint8_t> wide = rim;
            for (size_t r = 1; r < SH; ++r)
                for (size_t c = 0; c < SW; ++c)
                    wide[r * SW + c] |= rim[(r - 1) * SW + c];
            std::vector<std::uint8_t> wide2 = wide;
            for (size_t r = 0; r < SH; ++r)
                for (size_t c = 1; c < SW; ++c)
                    wide2[r * SW + c] |= rim[r * SW + c - 1];
            for (size_t i = 0; i < N; ++i)
                if (wide2[i])
                    rgb[i] = {255, 255, 255};
        } else {
            for (size_t i = 0; i < N; ++i)
                if (m[i])
                    for (int k = 0; k < 3; ++k)
                        rgb[i][size_t(k)] = u8(rgb[i][size_t(k)] * 0.35 + GOLD[k] * 0.65);
            for (size_t i = 0; i < N; ++i)
                if (m[i] && edge[i])
                    rgb[i] = {20, 16, 10};
        }
    }
    if (mark && mark->h() == h && mark->w() == w) {
        const long long ddx = _move ? std::get<1>(*_move) : 0, ddy = _move ? std::get<2>(*_move) : 0;   // (being dragged)
        std::vector<std::uint8_t> m(N, 0);
        for (size_t r = 0; r < SH; ++r) {
            const long long sy = vy[r] - ddy;
            if (sy < 0 || sy >= h)
                continue;
            for (size_t c = 0; c < SW; ++c) {
                const long long sx = vx[c] - ddx;
                if (sx >= 0 && sx < w)
                    m[r * SW + c] = (*mark)(sy, sx);
            }
        }
        for (size_t i = 0; i < N; ++i)
            if (m[i])
                for (int k = 0; k < 3; ++k)
                    rgb[i][size_t(k)] = u8(rgb[i][size_t(k)] * 0.4 + GOLD[k] * 0.6);
        std::vector<std::uint8_t> inner = m;
        for (size_t r = 1; r < SH; ++r)
            for (size_t c = 0; c < SW; ++c)
                inner[r * SW + c] &= m[(r - 1) * SW + c];
        for (size_t r = 0; r + 1 < SH; ++r)
            for (size_t c = 0; c < SW; ++c)
                inner[r * SW + c] &= m[(r + 1) * SW + c];
        for (size_t r = 0; r < SH; ++r)
            for (size_t c = 1; c < SW; ++c)
                inner[r * SW + c] &= m[r * SW + c - 1];
        for (size_t r = 0; r < SH; ++r)
            for (size_t c = 0; c + 1 < SW; ++c)
                inner[r * SW + c] &= m[r * SW + c + 1];
        for (size_t i = 0; i < N; ++i)
            if (m[i] && !inner[i])
                rgb[i] = {20, 16, 10};   // a dark line around what lands
    }
    QImage img(int(SW), int(SH), QImage::Format_RGB888);
    for (size_t r = 0; r < SH; ++r) {
        uchar *line = img.scanLine(int(r));
        for (size_t c = 0; c < SW; ++c) {
            line[c * 3] = rgb[r * SW + c][0];
            line[c * 3 + 1] = rgb[r * SW + c][1];
            line[c * 3 + 2] = rgb[r * SW + c][2];
        }
    }
    p.drawImage(QPointF(first_x, first_y), img);
    labelled = 0;
    if (numbers || !extra.empty())
        _paint_numbers(p, sub);
}

// (centre x, centre y, pixels) of every province of the map shown.
const SourceMap::Cent &SourceMap::centres()
{
    if (!_cent || _cent->of != ids) {
        const auto &flat = ids->flat();
        const long long w = ids->w();
        long long n = 0;
        for (std::uint16_t v : flat)
            n = std::max<long long>(n, v);
        n += 1;
        Cent c;
        c.of = ids;
        c.cnt.assign(size_t(n), 0.0);
        c.cx.assign(size_t(n), 0.0);
        c.cy.assign(size_t(n), 0.0);
        for (size_t i = 0; i < flat.size(); ++i) {
            c.cnt[flat[i]] += 1.0;
            c.cx[flat[i]] += double(static_cast<long long>(i) % w);
            c.cy[flat[i]] += double(static_cast<long long>(i) / w);
        }
        for (size_t q = 0; q < size_t(n); ++q) {
            const double safe = std::max(c.cnt[q], 1.0);
            c.cx[q] /= safe;
            c.cy[q] /= safe;
        }
        _cent = std::move(c);
    }
    return *_cent;
}

void SourceMap::_paint_numbers(QPainter &p, const std::vector<std::uint16_t> &sub)
{
    const Cent &ce = centres();
    QFont f(font());
    f.setBold(true);
    f.setPixelSize(12);
    p.setFont(f);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm = p.fontMetrics();
    auto text_at = [&](double x, double y, const QString &text, const QColor &colour, double down = 0) {
        const double sx = (x - ox) * scale, sy = (y - oy) * scale + down;
        if (!(-40 <= sx && sx <= width() + 40 && -10 <= sy && sy <= height() + 10))
            return;
        QPainterPath path;
        path.addText(sx - fm.horizontalAdvance(text) / 2.0, sy + 4, f, text);
        p.setPen(QPen(QColor(10, 10, 10), 3.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawPath(path);
        labelled += 1;
    };
    if (numbers) {
        std::vector<std::uint16_t> u(sub);
        std::sort(u.begin(), u.end());
        u.erase(std::unique(u.begin(), u.end()), u.end());
        // only provinces large enough on the screen to hold their number
        std::vector<std::uint16_t> big;
        for (std::uint16_t q : u)
            if (q > 0 && ce.cnt[q] * scale * scale >= 420)
                big.push_back(q);
        if (big.size() > 700) {   // (numpy's argsort here is its quicksort; ties may come in another order)
            std::stable_sort(big.begin(), big.end(), [&](std::uint16_t a, std::uint16_t b) { return -ce.cnt[a] < -ce.cnt[b]; });
            big.resize(700);
        }
        for (std::uint16_t q : big)
            text_at(ce.cx[q], ce.cy[q], QString::number(q), QColor(u"#f2ede2"_s));
    }
    const long long ddx = _move ? std::get<1>(*_move) : 0, ddy = _move ? std::get<2>(*_move) : 0;
    std::vector<std::array<double, 4>> placed;
    for (const auto &[x, y, text] : extra) {
        double down = 0;
        if (spread_extra) {   // a text that would cover another moves down, or is left out
            const double sx = (x + ddx - ox) * scale, sy = (y + ddy - oy) * scale + 13.0;
            const double half = fm.horizontalAdvance(text) / 2.0 + 4;
            bool found = false;
            for (double d : {0.0, 16.0, -16.0, 32.0, -32.0}) {
                down = d;
                const std::array<double, 4> bx{sx - half, sy + d - 13, sx + half, sy + d + 5};
                if (std::none_of(placed.begin(), placed.end(), [&](const std::array<double, 4> &q) {
                        return bx[0] < q[2] && q[0] < bx[2] && bx[1] < q[3] && q[1] < bx[3];
                    })) {
                    placed.push_back(bx);
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;
        }
        text_at(x + ddx, y + ddy + 13.0 / scale, text, QColor(u"#ffd84a"_s), down);
    }
}

void SourceMap::wheelEvent(QWheelEvent *e)
{
    if (!ids)
        return;
    const double f = e->angleDelta().y() > 0 ? 1.25 : 0.8;
    const QPointF pos = e->position();
    const double mx = ox + pos.x() / scale, my = oy + pos.y() / scale;
    scale = std::max(0.02, std::min(24.0, scale * f));
    ox = mx - pos.x() / scale;
    oy = my - pos.y() / scale;
    _last = 1;
    update();
}

// Is the lit part under (or right next to) the mouse?
bool SourceMap::lit(const QPointF &pos) const
{
    if (!mark || !ids)
        return false;
    const long long x = static_cast<long long>(ox + pos.x() / scale), y = static_cast<long long>(oy + pos.y() / scale);
    const long long r = std::max(1LL, py::round_int(5.0 / scale));
    const long long h = mark->h(), w = mark->w();
    // (Python's slices: an end below 0 counts from the far end)
    const long long ya = py::py_index(std::max(0LL, y - r), h), yb = py::py_index(std::min(h, y + r + 1), h);
    const long long xa = py::py_index(std::max(0LL, x - r), w), xb = py::py_index(std::min(w, x + r + 1), w);
    for (long long yy = ya; yy < yb; ++yy)
        for (long long xx = xa; xx < xb; ++xx)
            if ((*mark)(yy, xx))
                return true;
    return false;
}

void SourceMap::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        _pan = std::make_tuple(e->position(), ox, oy);
        return;
    }
    if (e->button() != Qt::LeftButton || !ids)
        return;
    const QPointF pos = e->position();
    if (movable) {   // this mod's map: drag what lands; anywhere else moves the map
        if (lit(pos)) {
            _move = std::make_tuple(pos, 0LL, 0LL);
            setCursor(Qt::ClosedHandCursor);
        } else {
            _pan = std::make_tuple(pos, ox, oy);
        }
        return;
    }
    const long long pid = at(pos);
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool ctrl = bool(mods & Qt::ControlModifier);
    const std::set<long long> before = picked;
    if (!pid) {
        if (!ctrl)
            picked.clear();
    } else {
        std::vector<long long> group;
        for (long long q : group_of ? group_of(pid, mods) : std::vector<long long>{pid})
            if (q)
                group.push_back(q);
        if (group.empty())
            group = {pid};
        if (ctrl && std::all_of(group.begin(), group.end(), [&](long long q) { return picked.count(q) > 0; })) {
            for (long long q : group)
                picked.erase(q);
            _drag = u"off"_s;
        } else if (ctrl) {
            picked.insert(group.begin(), group.end());
            _drag = u"on"_s;
        } else {
            picked = std::set<long long>(group.begin(), group.end());
            _drag = u"on"_s;
        }
        _press = pos;
    }
    if (picked != before) {
        update();
        Q_EMIT changed();
    }
}

void SourceMap::_set(long long pid)
{
    const size_t before = picked.size();
    if (_drag == u"off")
        picked.erase(pid);
    else
        picked.insert(pid);
    if (picked.size() != before) {
        update();
        Q_EMIT changed();
    }
}

void SourceMap::mouseMoveEvent(QMouseEvent *e)
{
    if (_pan) {
        const auto [p0, ox0, oy0] = *_pan;
        const QPointF d = e->position() - p0;
        ox = ox0 - d.x() / scale;
        oy = oy0 - d.y() / scale;
        _last = 1;
        update();
        return;
    }
    if (_move) {
        const QPointF d = e->position() - std::get<0>(*_move);
        _move = std::make_tuple(std::get<0>(*_move), py::round_int(d.x() / scale), py::round_int(d.y() / scale));
        update();
        return;
    }
    const long long pid = at(e->position());
    if (_drag && pid && _press && (e->position() - *_press).manhattanLength() >= 5)   // (a click that slips is a click)
        _set(pid);
    if (movable)
        setCursor(lit(e->position()) ? Qt::OpenHandCursor : Qt::CrossCursor);
    Q_EMIT hovered(int(pid));
}

void SourceMap::mouseReleaseEvent(QMouseEvent *)
{
    const auto mv = _move;
    _drag.reset();
    _pan.reset();
    _move.reset();
    _press.reset();
    if (mv) {
        setCursor(Qt::OpenHandCursor);
        if (std::get<1>(*mv) || std::get<2>(*mv))
            Q_EMIT moved(int(std::get<1>(*mv)), int(std::get<2>(*mv)));
        else
            update();
    }
}

// ---------------------------------------------------------------------------------------------- the page
RegionImportPage::RegionImportPage(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(260);
    connect(_timer, &QTimer::timeout, this, &RegionImportPage::_replan);
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 8, 8, 10);
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"theirs"_s, u"Their map — pick here"_s},
                                                                       {u"mine"_s, u"This mod's map — where it lands"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"theirs");
        connect(b, &QPushButton::clicked, this, [this] { _show_map(); });
        grp->addButton(b);
        row->addWidget(b, 1);
        view_btn[k] = b;
    }
    ll->addLayout(row);
    map = new SourceMap();
    connect(map, &SourceMap::changed, this, &RegionImportPage::_picked);
    connect(map, &SourceMap::hovered, this, &RegionImportPage::_hover);
    connect(map, &SourceMap::moved, this, &RegionImportPage::_moved);
    map->group_of = [this](long long pid, Qt::KeyboardModifiers mods) { return _group(pid, mods); };
    ll->addWidget(map, 1);
    hover = label(QString(), u"faint"_s, false);
    hover->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ll->addWidget(hover);
    keys = label(*KEYS.get(u"theirs"_s), u"faint"_s);
    ll->addWidget(keys);
    num_box = new QCheckBox(u"Province numbers on the map (zoom in to see the small ones); where the picked ones "
                            u"land: their number → this mod's"_s);
    num_box->setChecked(true);
    num_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(num_box, &QCheckBox::toggled, this, &RegionImportPage::_numbers);
    ll->addWidget(num_box);
    split->addWidget(left);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 7);
    split->setStretchFactor(1, 4);
    split->setSizes({1100, 620});
    auto *s = new widgets::Section();
    s->lay->addWidget(_bold(u"What is picked"_s));
    sel_lbl = label(u"Nothing yet — click provinces on their map."_s, u"sub"_s);
    s->lay->addWidget(sel_lbl);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[text, fn, tip] : std::vector<std::tuple<QString, void (RegionImportPage::*)(), QString>>{
             {u"＋ Its whole state"_s, &RegionImportPage::_add_state, u"Every province of the state of the one clicked last"_s},
             {u"＋ Its owner's land"_s, &RegionImportPage::_add_owner, u"Every province its owner has at the start"_s},
             {u"Unpick all"_s, &RegionImportPage::_clear, QString()}}) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, fn);
        row->addWidget(b, 1);
    }
    s->lay->addLayout(row);
    rl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"How they are pasted"_s));
    row = new QHBoxLayout();
    row->setSpacing(0);
    grp = new QButtonGroup(this);
    for (const auto &[k, t, tip] : std::vector<std::tuple<QString, QString, QString>>{
             {u"new"_s, u"As new provinces"_s, u"They arrive as new provinces of this mod, with all they have"_s},
             {u"borders"_s, u"Onto this mod's provinces (shapes only)"_s,
              u"Nothing new is made: the province of this mod with the same number (or colour) gets the "
              u"picked one's shape — its borders are overwritten, its history, pops and names stay"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"new");
        b->setToolTip(tip);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this] { _mode_changed(); });
        grp->addButton(b);
        row->addWidget(b, 1);
        mode_btn[k] = b;
    }
    s->lay->addLayout(row);
    match_host = new QWidget();
    auto *mh = new QVBoxLayout(match_host);
    mh->setContentsMargins(0, 0, 0, 0);
    row = new QHBoxLayout();
    row->setSpacing(0);
    row->addWidget(label(u"Which province of this mod: the one with "_s, u"sub"_s, false));
    grp = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"number"_s, u"the same number"_s},
                                                                       {u"colour"_s, u"the same colour (RGB)"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"number");
        connect(b, &QPushButton::clicked, this, [this] { _replan(); });
        grp->addButton(b);
        row->addWidget(b);
        match_btn[k] = b;
    }
    row->addStretch(1);
    mh->addLayout(row);
    clear_box = new QCheckBox(u"The land it had before and the new shape leaves free goes to its neighbours "
                              u"(else the province stands in two places)"_s);
    clear_box->setChecked(true);
    clear_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(clear_box, &QCheckBox::toggled, this, [this](bool) { _replan(); });
    mh->addWidget(clear_box);
    match_host->hide();
    s->lay->addWidget(match_host);
    rl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Their countries"_s));
    row = new QHBoxLayout();
    row->setSpacing(0);
    grp = new QButtonGroup(this);
    for (const auto &[k, t] : NATIONS) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"have");
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this] { _replan(); });
        grp->addButton(b);
        row->addWidget(b, 1);
        nat_btn[k] = b;
    }
    s->lay->addLayout(row);
    nat_note = label(QString(), u"faint"_s);
    s->lay->addWidget(nat_note);
    s->lay->addWidget(_bold(u"What comes with the provinces"_s));
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{
             {u"states"_s, u"Their states (a state this mod has under the same key gets them; else it is made)"_s},
             {u"pops"_s, u"Their pops (of cultures, religions and pop types this mod has)"_s},
             {u"layers"_s, u"Their terrain and river pixels (terrain.bmp, rivers.bmp)"_s},
             {u"positions"_s, u"Their places on the map (towns, units, buildings — positions.txt)"_s},
             {u"replace"_s, u"Delete this mod's provinces that end up completely covered"_s}}) {
        auto *c = new QCheckBox(t);
        c->setChecked(true);
        c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);   // (a long line never widens the panel)
        connect(c, &QCheckBox::toggled, this, [this](bool) { _replan(); });
        s->lay->addWidget(c);
        box[k] = c;
    }
    box[u"replace"_s]->setToolTip(u"On: a province of this mod that the imported ones cover completely is "
                                  u"deleted in the same step, like Drawing → Delete province. Off: it stays "
                                  u"in the files without land — the game crashes on such a land province "
                                  u"until you paint it back or delete it."_s);
    s->lay->addWidget(_bold(u"Where"_s));
    row = new QHBoxLayout();
    row->addWidget(label(u"Moved to the right by (px, minus = left)"_s, u"sub"_s, false));
    dx = new widgets::Stepper(-20000, 20000, 0);
    connect(dx, &widgets::Stepper::valueChanged, this, [this](int) { _replan(); });
    row->addWidget(dx);
    row->addStretch(1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    row->addWidget(label(u"Moved down by (px, minus = up)"_s, u"sub"_s, false));
    dy = new widgets::Stepper(-20000, 20000, 0);
    connect(dy, &widgets::Stepper::valueChanged, this, [this](int) { _replan(); });
    row->addWidget(dy);
    row->addStretch(1);
    s->lay->addLayout(row);
    auto *b = new QPushButton(u"Show where it lands — drag it into place there"_s);
    b->setToolTip(u"Switches to this mod's map: the provinces that would land are lit; drag them with the mouse"_s);
    connect(b, &QPushButton::clicked, this, [this] {
        view_btn[u"mine"_s]->setChecked(true);
        _show_map();
    });
    s->lay->addWidget(b);
    where_note = label(QString(), u"faint"_s);
    s->lay->addWidget(where_note);
    rl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"What the import does"_s));
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
    go = new QPushButton(u"Import"_s);
    go->setObjectName(u"primary"_s);
    go->setEnabled(false);
    connect(go, &QPushButton::clicked, this, &RegionImportPage::_import);
    s->lay->addWidget(go);
    rl->addWidget(s);
    rl->addStretch(1);
}

QLabel *RegionImportPage::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ the other mod
void RegionImportPage::set_source(document::Document *doc_, mapview::MapData *md_, importer::Source *src_,
                                  importer::Importer *imp_)
{
    doc = doc_;
    md = md_;
    if (src_ == src && rs) {
        _replan();
        return;
    }
    src = src_;
    imp = imp_;
    rs.reset();
    plan.reset();
    map->picked.clear();
    if (!src) {
        map->ids = nullptr;
        map->update();
        _replan();
        return;
    }
    try {
        rs = std::make_unique<RegionSource>(*src, doc->scenario, doc->date);
    } catch (const std::exception &e) {
        warn->setText(u"⚠ Its map can't be read: %1"_s.arg(QString::fromUtf8(e.what())));
        warn->show();
        return;
    }
    RegionSource &r = *rs;
    _their_lut = owner_lut(r.mod, [&r](long long p) -> const history::ProvState & { return r.state(p); }, r.md->n);
    const bool same = r.md->w == md->w && r.md->h == md->h;
    where_note->setText(
        u"Their map is %1 × %2 px, this mod's %3 × %4%5. A province lands where it is on their map, moved by the "
        u"numbers above — or drag it on this mod's map. Pixels that fall outside this mod's map are left out."_s
            .arg(r.md->w)
            .arg(r.md->h)
            .arg(md->w)
            .arg(md->h)
            .arg(same ? u" — the same size"_s : u" — not the same size: move them to where they belong"_s));
    view_btn[u"theirs"_s]->setChecked(true);
    _show_map(true);
    _replan();
}

Opts RegionImportPage::opts() const
{
    Opts o;
    o.states = box.at(u"states"_s)->isChecked();
    o.pops = box.at(u"pops"_s)->isChecked();
    o.layers = box.at(u"layers"_s)->isChecked();
    o.positions = box.at(u"positions"_s)->isChecked();
    o.replace = box.at(u"replace"_s)->isChecked();
    for (const auto &[k, _t] : NATIONS)
        if (nat_btn.at(k)->isChecked()) {
            o.nations = k;
            break;
        }
    o.dx = dx->value();
    o.dy = dy->value();
    o.mode = mode_btn.at(u"borders"_s)->isChecked() ? u"borders"_s : u"new"_s;
    o.match = match_btn.at(u"colour"_s)->isChecked() ? u"colour"_s : u"number"_s;
    o.clear_old = clear_box->isChecked();
    return o;
}

void RegionImportPage::_mode_changed()
{
    const bool borders = mode_btn[u"borders"_s]->isChecked();
    match_host->setVisible(borders);
    for (const QString &k : {u"states"_s, u"pops"_s, u"positions"_s})   // (nothing of theirs but the shape comes)
        box[k]->setEnabled(!borders);
    for (auto &[_k, b] : nat_btn)
        b->setEnabled(!borders);
    _replan();
}

void RegionImportPage::_numbers(bool on)
{
    map->numbers = on;
    _landing_labels();
    map->update();
}

// On this mod's map: at every picked province's new place "their number → this mod's".
void RegionImportPage::_landing_labels()
{
    map->extra.clear();
    if (view_btn[u"theirs"_s]->isChecked() || !plan || !plan->idx || !plan->problem.isEmpty() || !num_box->isChecked())
        return;
    std::map<long long, long long> to;
    if (plan->mode == u"borders")
        for (const auto &[s, t] : plan->pairs)
            to[s] = t;
    else
        for (const Prov &p : plan->provs)
            to[p.src] = p.pid;
    const SrcCent &c = _src_centres();
    const long long ddx = dx->value(), ddy = dy->value();
    size_t k = 0;
    for (const auto &[s, t] : to) {
        if (k++ >= 400)
            break;
        map->extra.emplace_back(c.xs[size_t(s)] + double(ddx), c.ys[size_t(s)] + double(ddy), u"%1 → %2"_s.arg(s).arg(t));
    }
}

const RegionImportPage::SrcCent &RegionImportPage::_src_centres()
{
    if (!_src_cent || _src_cent->of != &rs->md->idmap) {
        const auto &ids = rs->md->idmap;
        const auto &flat = ids.flat();
        const long long w = ids.w();
        long long n = 0;
        for (std::uint16_t v : flat)
            n = std::max<long long>(n, v);
        n += 1;
        SrcCent c;
        c.of = &ids;
        c.cnt.assign(size_t(n), 0.0);
        c.xs.assign(size_t(n), 0.0);
        c.ys.assign(size_t(n), 0.0);
        for (size_t i = 0; i < flat.size(); ++i) {
            c.cnt[flat[i]] += 1.0;
            c.xs[flat[i]] += double(static_cast<long long>(i) % w);
            c.ys[flat[i]] += double(static_cast<long long>(i) / w);
        }
        for (size_t q = 0; q < size_t(n); ++q) {
            c.cnt[q] = std::max(c.cnt[q], 1.0);
            c.xs[q] /= c.cnt[q];
            c.ys[q] /= c.cnt[q];
        }
        _src_cent = std::move(c);
    }
    return *_src_cent;
}

// What a click means, as on the main map: the province · Shift: its state · Alt: its owner's land · Alt+Shift: its
// continent.
std::vector<long long> RegionImportPage::_group(long long pid, Qt::KeyboardModifiers mods)
{
    const moddata::Province *pr = rs ? rs->mod.provinces.get(pid) : nullptr;
    if (!pr || pr->sea)
        return {pid};
    const bool shift = bool(mods & Qt::ShiftModifier), alt = bool(mods & Qt::AltModifier);
    if (alt && shift)
        return rs->of_continent(pid);
    if (alt)
        return rs->of_owner(pid);
    if (shift)
        return rs->of_state(pid);
    return {pid};
}

// The lit provinces were dragged on this mod's map.
void RegionImportPage::_moved(int right, int down)
{
    for (const auto &[st, v] : {std::pair{dx, right}, std::pair{dy, down}}) {
        st->blockSignals(true);
        st->setValue(st->value() + v);
        st->blockSignals(false);
    }
    _replan();
}

void RegionImportPage::_show_map(bool fresh, bool keep)
{
    if (!rs)
        return;
    const bool theirs = view_btn[u"theirs"_s]->isChecked();
    map->movable = !theirs;
    map->setCursor(Qt::CrossCursor);
    keys->setText(*KEYS.get(theirs ? u"theirs"_s : u"mine"_s));
    if (theirs) {
        map->mark.reset();
        map->extra.clear();
        map->set_map(&rs->md->idmap, _their_lut, !fresh);
    } else {
        document::Document *d = doc;
        const std::vector<Rgb8> lut =
            owner_lut(mod, [d](long long p) -> const history::ProvState & { return d->prov_state(p); }, md->n);
        std::optional<grid::Grid<std::uint8_t>> mark;
        if (plan && plan->idx) {
            mark.emplace(md->h, md->w, 0);
            for (std::int64_t i : *plan->idx)
                mark->flat()[size_t(i)] = 1;
        }
        if (!_theirs_picked) {
            _theirs_picked = map->picked;
            map->picked.clear();
        }
        map->mark = std::move(mark);
        _landing_labels();
        map->set_map(&md->idmap, lut, true);
        if (!keep && map->mark && plan && !plan->idx->empty()) {
            long long x0 = md->w, y0 = md->h, x1 = -1, y1 = -1;
            for (std::int64_t i : *plan->idx) {
                const long long y = i / md->w, x = i % md->w;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
            map->show_box(double(x0), double(y0), double(x1 + 1), double(y1 + 1));
        }
        return;
    }
    if (_theirs_picked) {
        map->picked = *_theirs_picked;
        _theirs_picked.reset();
        map->update();
    }
}

std::set<long long> RegionImportPage::_sel() const
{
    if (view_btn.at(u"theirs"_s)->isChecked())
        return map->picked;
    return _theirs_picked ? *_theirs_picked : std::set<long long>();
}

void RegionImportPage::_hover(int pid)
{
    if (!rs || !pid)
        return;
    if (view_btn[u"theirs"_s]->isChecked()) {
        const history::ProvState &st = rs->state(pid);
        const moddata::Province *pr = rs->mod.provinces.get(pid);
        const moddata::Region *reg =
            pr && pr->region && !pr->region->isEmpty() ? rs->mod.regions.get(*pr->region) : nullptr;
        hover->setText(u"#%1 %2%3%4%5"_s.arg(pid).arg(
            rs->name(pid), pr && pr->sea ? u" (sea)"_s : QString(),
            st.owner && !st.owner->isEmpty() ? u" · owner %1"_s.arg(*st.owner) : QString(),
            reg ? u" · state %1"_s.arg(reg->name.isEmpty() ? reg->id : reg->name) : QString()));
    } else {
        const moddata::Province *pr = mod.provinces.get(pid);
        hover->setText(u"#%1 %2 — this mod's"_s.arg(pid).arg(pr ? pr->name : u"?"_s));
    }
}

void RegionImportPage::_picked()
{
    if (!view_btn[u"theirs"_s]->isChecked())
        return;
    const size_t n = map->picked.size();
    sel_lbl->setText(u"%1 province%2 picked …"_s.arg(n).arg(s_if(n)));
    _timer->start();   // (worked out when the mouse rests: picking stays quick)
}

void RegionImportPage::_add_state()
{
    if (rs && view_btn[u"theirs"_s]->isChecked() && !map->picked.empty()) {
        const std::set<long long> was = map->picked;
        for (long long p : was)
            for (long long q : rs->of_state(p))
                map->picked.insert(q);
        map->update();
        _replan();
    }
}

void RegionImportPage::_add_owner()
{
    if (rs && view_btn[u"theirs"_s]->isChecked() && !map->picked.empty()) {
        const std::set<long long> was = map->picked;
        for (long long p : was)
            if (const moddata::Province *pp = rs->mod.provinces.get(p); pp && !pp->sea)
                for (long long q : rs->of_owner(p))
                    map->picked.insert(q);
        map->update();
        _replan();
    }
}

void RegionImportPage::_clear()
{
    map->picked.clear();
    if (view_btn[u"theirs"_s]->isChecked())
        _theirs_picked.reset();
    else
        _theirs_picked = std::set<long long>();
    map->update();
    _replan();
}

// ------------------------------------------------------------------ the plan
static QString moved_text(long long dx, long long dy)
{
    if (!dx && !dy)
        return u", at the same place"_s;
    QStringList parts;
    for (const auto &[v, a, b] : {std::tuple{dx, u"to the right"_s, u"to the left"_s}, std::tuple{dy, u"down"_s, u"up"_s}})
        if (v)
            parts << u"%1 px %2"_s.arg(std::llabs(v)).arg(v > 0 ? a : b);
    return u", moved "_s + parts.join(u" and "_s);
}

void RegionImportPage::_replan()
{
    const std::set<long long> sel = _sel();
    plan.reset();
    warn->hide();
    if (!rs) {
        sel_lbl->setText(u"Pick the other mod at the top first."_s);
        result->setText(QString());
        go->setEnabled(false);
        return;
    }
    RegionSource &r = *rs;
    size_t land = 0;
    for (long long p : sel)
        if (const moddata::Province *pp = r.mod.provinces.get(p); !pp || !pp->sea)   // (Python: a KeyError)
            ++land;
    sel_lbl->setText(!sel.empty() ? u"%1 province%2 picked (%3 land, %4 sea)."_s.arg(sel.size())
                                        .arg(s_if(sel.size()))
                                        .arg(land)
                                        .arg(sel.size() - land)
                                  : u"Nothing yet — click provinces on their map."_s);
    if (sel.empty()) {
        result->setText(QString());
        go->setEnabled(false);
        go->setText(u"Import"_s);
        nat_note->setText(QString());
        return;
    }
    const Opts o = opts();
    plan = make_plan(*doc, *md, r, std::vector<long long>(sel.begin(), sel.end()), o);
    const Plan &pl = *plan;
    if (!pl.problem.isEmpty()) {
        warn->setText(u"⚠ "_s + pl.problem);
        warn->show();
        result->setText(QString());
        go->setEnabled(false);
        if (!view_btn[u"theirs"_s]->isChecked())
            _show_map(false, true);
        return;
    }
    auto names = [this](const std::vector<long long> &lst, int n) {
        QStringList out;
        for (size_t i = 0; i < lst.size() && i < size_t(n); ++i) {
            const QString nm = py::strip(mod.provinces.get(lst[i])->name);
            out << u"#%1 %2"_s.arg(lst[i]).arg(nm.isEmpty() ? u"?"_s : nm);
        }
        return out.join(u", "_s) + (lst.size() > size_t(n) ? u" … (+%1)"_s.arg(lst.size() - size_t(n)) : QString());
    };
    if (pl.mode == u"borders") {
        _borders_text(pl, names);
        return;
    }
    auto tags = [&](const QStringList &lst, int n = 10) {
        QStringList out;
        for (qsizetype i = 0; i < lst.size() && i < n; ++i) {
            const QString &t = lst[i];
            const moddata::Country *c = r.mod.countries.get(t);
            if (!c)
                c = mod.countries.get(t);
            out << u"%1 (%2)"_s.arg(c ? c->name : t, t);
        }
        return out.join(u", "_s) + (lst.size() > n ? u" … (+%1)"_s.arg(lst.size() - n) : QString());
    };
    size_t nl = 0;
    for (const Prov &p : pl.provs)
        if (!p.sea)
            ++nl;
    const size_t np = pl.provs.size();
    QStringList rows;
    rows << u"<b>%1 new province%2</b> (%3 land, %4 sea) with the numbers #%5 … #%6 — their shape pixel for "
            u"pixel%7."_s.arg(np)
                .arg(s_if(np))
                .arg(nl)
                .arg(np - nl)
                .arg(pl.provs.front().pid)
                .arg(pl.provs.back().pid)
                .arg(moved_text(dx->value(), dy->value()));
    const size_t nc = pl.covered.size(), npart = pl.partly.size();
    rows << u"<b>On this mod's map</b>: %1%2%3"_s.arg(
        nc ? u"%1 province%2 disappear%3 completely (%4) — %5 "_s.arg(nc)
                 .arg(s_if(nc), nc == 1 ? u"s"_s : QString(), names(pl.covered, 8),
                      o.replace ? u"they are deleted in the same step, as with Drawing → Delete province (history, pops, "
                                  u"state, places; a capital or an army there moves)."_s
                                : u"<span style='color:%1'>they stay in the files without land, with their owner and "
                                  u"pops. The game crashes on a land province without a pixel (tried): paint them back "
                                  u"in Drawing or delete them (the Errors tab lists them) — or tick the box above."
                                  u"</span>"_s.arg(theme::ERROR_))
           : u"no province disappears. "_s,
        npart ? u"%1 lose%2 a part (%3). "_s.arg(npart).arg(npart == 1 ? u"s"_s : QString(), names(pl.partly, 8))
              : QString(),
        pl.outside ? u"%1 px fall outside this mod's map and are left out."_s.arg(widgets::fmt(pl.outside)) : QString());
    const QString &mode = o.nations;
    QStringList nat;
    if (!pl.kept_tags.isEmpty())
        nat << u"stay as owners / cores, this mod has them: %1"_s.arg(tags(pl.kept_tags));
    if (!pl.missing_tags.isEmpty())
        nat << u"<b>imported too</b> (their files; a step of its own): %1"_s.arg(tags(pl.missing_tags));
    if (!pl.dropped_tags.isEmpty())
        nat << u"left out (%1): %2"_s.arg(mode == u"have" ? u"this mod doesn't have them"_s : u"as chosen"_s,
                                          tags(pl.dropped_tags));
    rows << u"<b>Countries</b> — %1."_s.arg(!nat.isEmpty() ? nat.join(u"; "_s) : u"none are named"_s);
    size_t owned = 0;
    for (const Prov &p : pl.provs)
        if (!p.sea && p.rec.info.owner && !p.rec.info.owner->isEmpty())
            ++owned;
    rows << u"%1 of the %2 land provinces come with an owner, %3 without (uncolonised land until you give "
            u"them one in Provinces)."_s.arg(owned)
                .arg(nl)
                .arg(nl - owned);
    long long npop = 0, people = 0, skipped = 0;
    for (const Prov &p : pl.provs) {
        npop += static_cast<long long>(p.pops.size());
        for (const moddata::Pop &pp : p.pops)
            people += pp.size;
    }
    for (const auto &[_k, v] : pl.skipped_pops)
        skipped += v;
    if (o.pops) {
        QString tail = u"."_s;
        if (skipped) {
            QStringList ks;
            for (const auto &[k, _v] : pl.skipped_pops)
                ks << k;
            std::stable_sort(ks.begin(), ks.end(), [&](const QString &a, const QString &b) {
                return -*pl.skipped_pops.get(a) < -*pl.skipped_pops.get(b);
            });
            tail = u". <span style='color:%1'>%2 people are left out — this mod has no such culture / religion: %3</span>"_s
                       .arg(theme::WARN, widgets::fmt(skipped), refs::esc(ks.mid(0, 6).join(u", "_s)));
        }
        rows << u"<b>Pops</b>: %1 pops, %2 people%3"_s.arg(widgets::fmt(npop), widgets::fmt(people), tail);
    }
    if (!pl.new_regions.isEmpty()) {
        size_t made = 0;
        for (const auto &[rid, _l] : pl.new_regions)
            if (!mod.any_region(rid))
                ++made;
        const size_t nr = static_cast<size_t>(pl.new_regions.size());
        rows << u"<b>States</b>: %1 — %2 new, %3 this mod has under the same key (they get the provinces)."_s.arg(nr)
                    .arg(made)
                    .arg(nr - made);
    } else if (nl) {
        rows << u"<b>States</b>: each new province joins the state of what lay under it."_s;
    }
    QStringList bits;
    if (pl.terrain)
        bits << u"terrain.bmp"_s;
    if (pl.rivers)
        bits << u"rivers.bmp"_s;
    size_t places = 0;
    for (const Prov &p : pl.provs)
        if (p.entry)
            ++places;
    rows << u"<b>Also</b>: %1%2 places (positions.txt); continent and climate of what lay under each. "
            u"Buildings and trade goods come with the history%3. Sea crossings (adjacencies) are not "
            u"brought — the Errors tab finds the missing ones."_s
                .arg(!bits.isEmpty() ? u"the same pixels of %1; "_s.arg(bits.join(u" and "_s)) : QString())
                .arg(places)
                .arg(!pl.dropped_goods.empty()
                         ? u" (trade goods this mod doesn't have are left out: %1)"_s.arg(
                               QStringList(pl.dropped_goods.begin(), pl.dropped_goods.end()).join(u", "_s))
                         : QString());
    QString html;
    for (const QString &row : rows)
        html += u"<p>%1</p>"_s.arg(row);
    result->setText(html);
    nat_note->setText(mode == u"take" ? u"Owners, controllers and cores stay as they are; a country this mod doesn't "
                                        u"have is imported with its files."_s
                      : mode == u"have" ? u"Owners, controllers and cores stay where this mod has the country; the "
                                          u"others are taken out of the history."_s
                                        : u"No owner, controller or core comes along: the land arrives unowned."_s);
    go->setEnabled(true);
    go->setText(u"Import %1 province%2"_s.arg(np).arg(s_if(np)));
    if (!view_btn[u"theirs"_s]->isChecked())
        _show_map(false, true);
}

// What pasting the shapes onto this mod's provinces does.
void RegionImportPage::_borders_text(const Plan &pl,
                                     const std::function<QString(const std::vector<long long> &, int)> &names)
{
    RegionSource &r = *rs;
    const size_t n = pl.pairs.size();
    const QString by = match_btn[u"number"_s]->isChecked() ? u"number"_s : u"colour"_s;
    QStringList rows;
    rows << u"<b>%1 province%2 of this mod get%3 a new shape</b> — the one of the picked province with the same "
            u"%4, pixel for pixel%5. Nothing new is made; their history, pops and names stay as they are."_s.arg(n)
                .arg(s_if(n), n == 1 ? u"s"_s : QString(), by, moved_text(dx->value(), dy->value()));
    QStringList table;
    for (size_t i = 0; i < pl.pairs.size() && i < 14; ++i) {
        const auto [s, t] = pl.pairs[i];
        const auto *sz = pl.sizes.get(t);
        const long long was = sz ? sz->first : 0, now = sz ? sz->second : 0;
        const QString nm = py::strip(mod.provinces.get(t)->name);
        table << u"their #%1 %2 → this mod's #%3 %4 (%5 px → %6 px)"_s.arg(s)
                     .arg(refs::esc(r.name(s)))
                     .arg(t)
                     .arg(refs::esc(nm.isEmpty() ? u"?"_s : nm), widgets::fmt(was), widgets::fmt(now));
    }
    rows << u"<b>What overwrites what</b>:<br>"_s + table.join(u"<br>"_s) +
                (n > 14 ? u"<br>… and %1 more"_s.arg(n - 14) : QString());
    if (!pl.no_target.empty()) {
        QStringList lst;
        for (size_t i = 0; i < pl.no_target.size() && i < 10; ++i)
            lst << u"#%1 %2"_s.arg(pl.no_target[i]).arg(refs::esc(r.name(pl.no_target[i])));
        const size_t k = pl.no_target.size();
        rows << u"<span style='color:%1'><b>%2 picked province%3 left out</b> — this mod has none with the "
                u"same %4: %5</span>"_s.arg(theme::WARN)
                    .arg(k)
                    .arg(k == 1 ? u" is"_s : u"s are"_s, by, lst.join(u", "_s));
    }
    const size_t free = pl.fill_idx ? pl.fill_idx->size() : 0;
    rows << (clear_box->isChecked()
                 ? u"%1 px of their old land the new shapes leave free go to the neighbours."_s.arg(
                       widgets::fmt(static_cast<long long>(free)))
                 : u"<span style='color:%1'>Their old land stays theirs: a province whose new shape lies elsewhere "
                   u"stands in two places then.</span>"_s.arg(theme::ERROR_));
    if (!pl.covered.empty()) {
        const size_t nc = pl.covered.size();
        rows << u"<span style='color:%1'><b>%2 province%3 of this mod %4 swallowed — no land left</b> (%5): %6"
                u"</span>"_s.arg(theme::ERROR_)
                    .arg(nc)
                    .arg(s_if(nc), nc == 1 ? u"is"_s : u"are"_s, names(pl.covered, 8),
                         opts().replace ? u"deleted in the same step, as with Drawing → Delete province."_s
                                        : u"they stay in the files without land — the game crashes on that; tick the box "
                                          u"“Delete this mod's provinces that end up completely covered”."_s);
    }
    if (!pl.partly.empty()) {
        const size_t k = pl.partly.size();
        rows << u"%1 other province%2 lose%3 a part: %4."_s.arg(k).arg(s_if(k), k == 1 ? u"s"_s : QString(),
                                                                       names(pl.partly, 8));
    }
    if (pl.outside)
        rows << u"%1 px fall outside this mod's map and are left out."_s.arg(widgets::fmt(pl.outside));
    QStringList bits;
    if (pl.terrain)
        bits << u"terrain.bmp"_s;
    if (pl.rivers)
        bits << u"rivers.bmp"_s;
    if (!bits.isEmpty())
        rows << u"The same pixels of %1 come along."_s.arg(bits.join(u" and "_s));
    rows << u"Places on the map (positions.txt) and sea crossings are not changed — the Errors tab finds what "
            u"no longer fits."_s;
    QString html;
    for (const QString &row : rows)
        html += u"<p>%1</p>"_s.arg(row);
    result->setText(html);
    nat_note->setText(QString());
    go->setEnabled(true);
    go->setText(u"Overwrite the shape of %1 province%2"_s.arg(n).arg(s_if(n)));
    if (!view_btn[u"theirs"_s]->isChecked())
        _show_map(false, true);
}

void RegionImportPage::_import()
{
    if (!plan || !plan->problem.isEmpty() || !rs)
        return;
    QStringList notes;
    QString msg;
    try {
        if (!plan->missing_tags.isEmpty() && imp) {   // their countries first (their own step)
            std::vector<importer::ItemP> items;
            for (const QString &t : plan->missing_tags)
                if (importer::ItemP it = imp->find(u"nations"_s, t))
                    items.push_back(it);
            if (!items.empty()) {
                std::set<importer::Id> ids;
                for (const auto &it : items)
                    ids.insert(it->id());
                importer::Options o;
                o.nation_mode = u"files"_s;
                o.event_conflict = u"skip"_s;
                o.icons = true;
                const importer::Plan np_ = imp->plan(std::vector<importer::Id>(ids.begin(), ids.end()), {}, o, {});
                notes << imp->apply(np_);
                const std::set<long long> sel = _sel();
                plan = make_plan(*doc, *md, *rs, std::vector<long long>(sel.begin(), sel.end()), opts());
            }
        }
        msg = apply_plan(*doc, *md, *rs, *plan, opts(), landsea);
    } catch (const std::exception &e) {
        warn->setText(u"⚠ The import failed (%1)."_s.arg(QString::fromUtf8(e.what())));
        warn->show();
        return;
    }
    map->picked.clear();
    _theirs_picked.reset();
    plan.reset();
    Q_EMIT message(msg);
    Q_EMIT imported();
    view_btn[u"mine"_s]->setChecked(true);
    _show_map();
    QString html = u"<p><b>%1</b></p>"_s.arg(refs::esc(msg));
    for (const QString &n : notes)
        html += u"<p>%1</p>"_s.arg(refs::esc(n));
    result->setText(html);
    go->setEnabled(false);
    go->setText(u"Import"_s);
    sel_lbl->setText(u"Nothing picked."_s);
}

}  // namespace regionimport
