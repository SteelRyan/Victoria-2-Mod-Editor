// techpanel.py — see techpanel.h
#include "ui/techpanel.h"
#include "ui/drawtext.h"

#include "core/countryhist.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "ui/defpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleValidator>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using script::Node;
using widgets::label;
using widgets::Section;

namespace techpanel {

namespace {

class PopupOnClick : public QObject {                // a click into an editable combo box opens its list
public:
    explicit PopupOnClick(QComboBox *box) : QObject(box), box(box) { box->lineEdit()->installEventFilter(this); }
    QComboBox *box;
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (ev->type() == QEvent::MouseButtonPress && !box->view()->isVisible())
            box->showPopup();
        return false;
    }
};

class Room : public QObject {                        // the tree's room changed: the cards share it
public:
    Room(QObject *parent, TechGrid *grid) : QObject(parent), grid(grid) {}
    TechGrid *grid;
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::Resize)
            grid->set_view_height(static_cast<QWidget *>(obj)->height());
        return false;
    }
};

QString rstrip_chars(QString s, QChar c)             // str.rstrip(c)
{
    while (!s.isEmpty() && s.back() == c)
        s.chop(1);
    return s;
}

QFont mono()
{
    QFont f(u"Consolas"_s);
    f.setPointSize(10);
    return f;
}

const Node *root_of(const QString &text, std::shared_ptr<const Node> &keep)
{
    keep = techdata::root(text);
    return keep.get();
}

const Node *walk_to(const Node *n, const IPath &ipath)
{
    for (qsizetype i : ipath)
        n = n ? &n->kids[static_cast<size_t>(i)] : nullptr;
    return n;
}

bool is(const Node &c, const QString &k) { return c.has_key && c.key == k; }

IPath plus(IPath p, qsizetype i)
{
    p.push_back(i);
    return p;
}

QPushButton *x_button(QWidget *parent = nullptr)
{
    auto *x = new QPushButton(u"✕"_s, parent);
    x->setFixedWidth(28);
    x->setStyleSheet(u"QPushButton { padding: 0px; }"_s);
    return x;
}

}  // namespace

void searchable(QComboBox *box, int visible)
{
    box->setMaxVisibleItems(visible);
    QStringList items;
    for (int i = 0; i < box->count(); ++i)
        items << box->itemText(i);
    auto *comp = new QCompleter(items, box);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    comp->setMaxVisibleItems(visible);
    box->setCompleter(comp);
    new PopupOnClick(box);
}

QString fmt(const QString &n)
{
    const auto f = py::to_float(n);
    if (!f || !std::isfinite(*f))
        return n;
    return widgets::fmt(py::trunc_int(*f));
}

QString short_(const QString &n)
{
    const auto f = py::to_float(n);
    if (!f)
        return n;
    const double v = *f;
    if (v >= 1000)
        return (py::fmt_f(v / 1000, 1) + u"k"_s).replace(u".0k"_s, u"k"_s);
    if (!std::isfinite(v))
        return n;
    return QString::number(py::trunc_int(v));
}

QString ordinal(long long n)
{
    const long long h = py::mod(n, 100LL), t = py::mod(n, 10LL);
    const QString s = (10 <= h && h <= 20) ? u"th"_s : t == 1 ? u"st"_s : t == 2 ? u"nd"_s : t == 3 ? u"rd"_s : u"th"_s;
    return QString::number(n) + s;
}

void NoteEdit::focusOutEvent(QFocusEvent *ev)
{
    QPlainTextEdit::focusOutEvent(ev);
    Q_EMIT done(toPlainText());
}

// ---------------------------------------------------------------------------------------------- the grid
static const int COLUMN_RGB[7][3] = {{98, 160, 234}, {106, 196, 122}, {234, 172, 78}, {190, 136, 238},
                                     {238, 122, 124}, {96, 204, 204}, {214, 204, 104}};
static const int CARD_BASE[3] = {30, 34, 42};
// What "unciv_military = yes" does — the note behind the "?" next to its box. Measured in the game 2026-10-14
// (Heart of Darkness 3.04, three versions of the tech files: the flag as in the game / on no tech / on all 150).
static QString unciv_note()
{
    return u"<b>unciv_military = yes</b> — in the base game on every army and navy technology (60), on no other.<br><br>"
           "<b>Tested in the game: it changes nothing we could find.</b> Taken off every technology, or put on all 150, "
           "the game played the same:"
           "<ul style='margin-left:-20px'>"
           "<li><b>Westernizing</b> (the game's own button at 100 %): Japan got the same 14 technologies in all three "
           "versions (6 army / navy, 8 others).</li>"
           "<li><b>Unciv reforms</b>: the same reforms bought for the same research points (within normal noise).</li>"
           "<li><b>Uncivilized nations</b> gather the same research points, research nothing on their own.</li>"
           "<li><b>An uncivilized player</b> can't research it either way — the tech window says <i>Westernize to be able "
           "to research</i>, for flagged and unflagged technologies alike.</li>"
           "<li>Reforms handed out by an event (<i>military_reform</i>) or westernizing by an event (<i>civilized = yes</i>) "
           "give no technology in any version.</li></ul>"
           "<b>If you untick it:</b> nothing measurable happens. Most likely a leftover the game no longer reads.<br>"
           "<span style='color:"_s + theme::INK_DIM +
           u"'>Not tested: very long games (we ran 4 months per version), and research points an "
           "uncivilized nation gets from conquering after its military reforms.</span>"_s;
}
static const QStringList ROMAN = {u"I"_s, u"II"_s, u"III"_s, u"IV"_s, u"V"_s, u"VI"_s, u"VII"_s, u"VIII"_s,
                                  u"IX"_s, u"X"_s, u"XI"_s, u"XII"_s, u"XIII"_s, u"XIV"_s, u"XV"_s};

static QColor mix(const int *a, const int *b, double f)   // colour a with f of colour b
{
    return QColor(static_cast<int>(a[0] + (b[0] - a[0]) * f), static_cast<int>(a[1] + (b[1] - a[1]) * f),
                  static_cast<int>(a[2] + (b[2] - a[2]) * f));
}

std::pair<QString, int> signed_(const QString &v)
{
    const auto f = py::to_float(v);
    if (!f)
        return {v, 0};
    const double x = *f;
    const int sign = x > 0 ? 1 : x < 0 ? -1 : 0;
    const QString mark = x > 0 ? u"+"_s : x < 0 ? u"−"_s : QString();
    const double ax = std::fabs(x);
    QString s;
    if (0 < ax && ax < 1)
        s = rstrip_chars(rstrip_chars(py::fmt_f(ax * 100, 1), u'0'), u'.') + u" %"_s;
    else if (std::isfinite(ax) && ax == std::trunc(ax))
        s = py::fmt_f(ax, 0);
    else
        s = rstrip_chars(rstrip_chars(py::fmt_f(ax, 2), u'0'), u'.');
    return {mark + s, sign};
}

std::vector<Fx> effect_lines(const QString &text, const std::function<QString(const QString &)> &name_of)
{
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::vector<Fx> out;
    if (!r)
        return out;
    for (const Node &c : r->kids) {
        if (!c.has_key || c.key.isEmpty() || techdata::TECH_BASE.contains(c.key) || c.key == u"activate_unit" ||
            c.key == u"activate_building")
            continue;
        if (c.is_block) {
            std::vector<std::pair<QString, QString>> kids;
            for (const Node &k : c.kids)
                if (k.has_key && !k.key.isEmpty() && !k.is_block)
                    kids.emplace_back(k.key, k.value);
            if (kids.empty())
                continue;
            const auto [num, sign] = signed_(kids[0].second);
            QString words = u"%1 %2"_s.arg(name_of(c.key), py::lower(name_of(kids[0].first)));
            if (kids.size() > 1)
                words += u"  +%1 more"_s.arg(kids.size() - 1);
            out.push_back({sign, num, words});
        } else if (py::lower(c.value) == u"yes" || py::lower(c.value) == u"no") {
            const bool yes = py::lower(c.value) == u"yes";
            out.push_back({yes ? 1 : -1, yes ? u"✓"_s : u"✕"_s, name_of(c.key)});
        } else {
            const auto [num, sign] = signed_(c.value);
            out.push_back({sign, num, name_of(c.key)});
        }
    }
    return out;
}

TechGrid::TechGrid(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static Info info_or(const OrderedMap<QString, Info> &info, const QString &k)   // self.info.get(k, {})
{
    if (const Info *d = info.get(k))
        return *d;
    Info d;
    d.name = k;
    d.year = u"?"_s;
    d.cost = u"0"_s;
    return d;
}

QString TechGrid::row_years(long long j) const
{
    std::vector<long long> ys;
    for (const Area &ar : areas)
        if (j < ar.keys.size()) {
            const Info *d = info.get(ar.keys[j]);
            const QString y = d ? py::strip(d->year) : QString();
            QString t = y;
            while (t.startsWith(u'-'))
                t.remove(0, 1);
            if (py::isdigit(t)) {
                try {
                    ys.push_back(py::to_int(y));
                } catch (const py::ValueError &) {
                }
            }
        }
    if (ys.empty())
        return QString();
    const auto [lo, hi] = std::minmax_element(ys.begin(), ys.end());
    return *lo == *hi ? QString::number(ys[0]) : u"%1–%2"_s.arg(*lo).arg(*hi);
}

long long TechGrid::rows() const
{
    long long m = 1;
    for (const Area &a : areas)
        m = std::max<long long>(m, a.keys.size());
    return m;
}

void TechGrid::set_view_height(int h)
{
    // the room there is: six rows (or what the folder has) share it — within what a card may be
    if (h != _view_h) {
        _view_h = h;
        _fit();
    }
}

void TechGrid::_fit()
{
    const long long rows_ = rows();
    if (_view_h > 0) {
        const double room = (_view_h - HEAD_H - 2 * PAD) / static_cast<double>(std::max<long long>(rows_, 6)) - GAP;
        CARD_H = static_cast<int>(std::max<double>(CARD_MIN, std::min<double>(CARD_MAX, room)));
    }
    setFixedHeight(static_cast<int>(HEAD_H + PAD + rows_ * (CARD_H + GAP) + PAD));
    update();
}

void TechGrid::set_folder(const std::vector<Area> &areas_, const OrderedMap<QString, Info> &info_)
{
    areas = areas_;
    info = info_;
    _fit();
}

QSize TechGrid::sizeHint() const { return QSize(700, height()); }

double TechGrid::_col_w() const
{
    const long long n = std::max<long long>(1, static_cast<long long>(areas.size()));
    return (width() - RAIL - PAD * (n + 1)) / static_cast<double>(n);
}

void TechGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double cw = _col_w();
    _cells.clear();
    QFont f(font());
    f.setBold(true);
    const QFontMetrics fm(f);
    const double base_pt = QFontInfo(font()).pointSizeF();   // (the font may be set in pixels: its real size)
    QFont small_(font());
    small_.setPointSizeF(std::max(8.5, base_pt - 0.75));
    const QFontMetrics sm(small_);
    QFont sbold(small_);
    sbold.setBold(true);
    const QFontMetrics sbm(sbold);
    const long long rows_ = rows();
    const int CH = CARD_H;
    const int top = HEAD_H + PAD;
    const QColor gold(theme::GOLD), dark(18, 20, 24);
    // --- the rail of the eras: a band a row, its number and the year most of its technologies have
    QFont rail_f(font());
    rail_f.setBold(true);
    rail_f.setPointSizeF(base_pt + 4);
    for (long long j = 0; j < rows_; ++j) {
        const long long y = top + j * (CH + GAP);
        if (j % 2 == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 9));
            p.drawRoundedRect(QRectF(4, y - GAP / 2.0, width() - 8, CH + GAP), 8, 8);
        }
        const QString year = row_years(j);
        p.setFont(rail_f);
        p.setPen(gold);
        drawtext::draw(p, QRectF(4, y + CH / 2.0 - 26, RAIL - 6, 26), Qt::AlignCenter,
                   j < ROMAN.size() ? ROMAN[j] : QString::number(j + 1));
        p.setFont(small_);
        p.setPen(QColor(theme::INK_DIM));
        if (year.contains(u'–')) {                    // the row's technologies differ: from – to, two lines
            const QStringList ab = year.split(u'–');
            drawtext::draw(p, QRectF(2, y + CH / 2.0, RAIL - 2, 18), Qt::AlignCenter, ab[0] + u" –"_s);
            drawtext::draw(p, QRectF(2, y + CH / 2.0 + 16, RAIL - 2, 18), Qt::AlignCenter, ab[1]);
        } else
            drawtext::draw(p, QRectF(4, y + CH / 2.0, RAIL - 6, 20), Qt::AlignCenter, year);
    }
    for (size_t i = 0; i < areas.size(); ++i) {
        const Area &area = areas[i];
        const int *rgb = COLUMN_RGB[i % 7];
        const QColor accent(rgb[0], rgb[1], rgb[2]);
        const double x = RAIL + PAD + i * (cw + PAD);
        // --- the column's head: a band in its colour
        const QRectF hr(x, 6, cw, HEAD_H - 12);
        p.setPen(QPen(accent, 2));
        p.setBrush(mix(CARD_BASE, rgb, 0.42));
        p.drawRoundedRect(hr, 9, 9);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        drawtext::draw(p, hr.adjusted(8, 0, -8, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   fm.elidedText(area.title, Qt::ElideRight, static_cast<int>(2 * (cw - 16))));
        const QStringList &keys = area.keys;
        for (qsizetype j = 0; j < keys.size(); ++j) {
            const QString &k = keys[j];
            const long long y = top + j * (CH + GAP);
            const QRectF r(x, y, cw, CH);
            const Info d = info_or(info, k);
            const bool dimmed = match && !match->count(k);
            const bool is_sel = sel && *sel == k;
            if (j + 1 < keys.size()) {                // the next one needs this one: a thick link down to it
                const double cx = x + cw / 2.0;
                p.setPen(QPen(mix(CARD_BASE, rgb, 0.7), 4, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(cx, y + CH + 1), QPointF(cx, y + CH + GAP - 6));
                p.setPen(Qt::NoPen);
                p.setBrush(mix(CARD_BASE, rgb, 0.85));
                const QPointF tri[3] = {QPointF(cx - 7, y + CH + GAP - 9), QPointF(cx + 7, y + CH + GAP - 9),
                                        QPointF(cx, y + CH + GAP - 1)};
                p.drawPolygon(tri, 3);
            }
            p.save();
            if (dimmed)
                p.setOpacity(0.32);
            // --- the card: tinted with its column's colour, a stripe of it down the left
            const QRectF inner = r.adjusted(1.5, 1.5, -1.5, -1.5);
            p.setPen(QPen(is_sel ? gold : mix(CARD_BASE, rgb, 0.62), is_sel ? 3.2 : 1.8));
            p.setBrush(mix(CARD_BASE, rgb, is_sel ? 0.30 : 0.17));
            p.drawRoundedRect(inner, 9, 9);
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawRoundedRect(QRectF(x + 4, y + 9, 6, CH - 18), 3, 3);
            const double left = x + 18, right = x + cw - 10;
            // the year: a tab in the column's colour on the card's top edge
            const QString yt = d.year;
            const int bw = sbm.horizontalAdvance(yt) + 18;
            const QRectF br(right - bw + 2, y - 9, bw, 22);
            p.setPen(QPen(dark, 2));
            p.setBrush(accent);
            p.drawRoundedRect(br, 10, 10);
            p.setFont(sbold);
            p.setPen(dark);
            drawtext::draw(p, br, Qt::AlignCenter, yt);
            if (d.same) {                            // same year as a neighbour: its order is a choice
                const QRectF sr(br.left() - 27, y - 9, 23, 22);
                p.setPen(QPen(dark, 2));
                p.setBrush(QColor(238, 198, 98));
                p.drawRoundedRect(sr, 10, 10);
                p.setPen(dark);
                p.setFont(f);
                drawtext::draw(p, sr, Qt::AlignCenter, u"⇅"_s);
            }
            // the name (two lines at most, the whole width)
            const double room = right - left;
            QStringList lines;
            QString cur;
            for (const QString &wd : py::split(d.name)) {
                const QString tt = py::strip(cur + u' ' + wd);
                if (fm.horizontalAdvance(tt) <= room || cur.isEmpty())
                    cur = tt;
                else {
                    lines << cur;
                    cur = wd;
                }
            }
            lines << cur;
            if (lines.size() > 2)
                lines = QStringList{lines[0], lines.mid(1).join(u' ')};
            p.setFont(f);
            p.setPen(QColor(244, 246, 250));
            for (qsizetype li = 0; li < lines.size(); ++li)
                drawtext::draw(p, QRectF(left, y + 12 + li * 18, room, 20), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(lines[li], Qt::ElideRight, static_cast<int>(room)));
            // what it does: the numbers in green / red, the words beside them
            long long fy = y + 12 + lines.size() * 18 + 3;
            const long long foot_y = y + CH - 29;
            const std::vector<Fx> &fx = d.fx;
            const long long can = std::max<long long>(0, py::floordiv(foot_y - 4 - fy, 17LL));
            std::vector<Fx> show;
            bool more_line = false;
            if (static_cast<long long>(fx.size()) <= can)
                show = fx;
            else if (can >= 2) {
                show.assign(fx.begin(), fx.begin() + (can - 1));
                more_line = true;
            } else                                   // one line only: the first effect, and how many more
                for (long long q = 0; q < can; ++q)
                    show.push_back({fx[q].sign, fx[q].num, u"%1  (+%2)"_s.arg(fx[q].words, QString::number(fx.size() - 1))});
            for (const Fx &e : show) {
                p.setFont(sbold);
                p.setPen(e.sign > 0 ? QColor(130, 224, 140) : e.sign < 0 ? QColor(255, 138, 122) : QColor(214, 218, 226));
                const int nw = sbm.horizontalAdvance(e.num);
                drawtext::draw(p, QRectF(left, fy, nw + 2, 17), Qt::AlignLeft | Qt::AlignVCenter, e.num);
                p.setFont(small_);
                p.setPen(QColor(206, 210, 220));
                drawtext::draw(p, QRectF(left + nw + 6, fy, right - left - nw - 6, 17), Qt::AlignLeft | Qt::AlignVCenter,
                           sm.elidedText(e.words, e.words.endsWith(u')') ? Qt::ElideMiddle : Qt::ElideRight,
                                         static_cast<int>(right - left - nw - 6)));
                fy += 17;
            }
            if (more_line) {
                const long long left_n = static_cast<long long>(fx.size() - show.size());
                p.setFont(small_);
                p.setPen(QColor(150, 156, 168));
                drawtext::draw(p, QRectF(left, fy, right - left, 17), Qt::AlignLeft | Qt::AlignVCenter,
                           u"+ %1 more effect%2"_s.arg(left_n).arg(left_n == 1 ? QString() : u"s"_s));
            }
            // the foot: what it costs, its inventions, what it unlocks — as tags
            p.setPen(QPen(mix(CARD_BASE, rgb, 0.40), 1.2));
            p.drawLine(QPointF(left, foot_y - 3), QPointF(right, foot_y - 3));
            double tx = left;
            struct Tag {
                QString text;
                QColor bg, ink;
                QString icon;
            };
            std::vector<Tag> tags;
            if (d.isnew)
                tags.push_back({u"NEW"_s, QColor(96, 190, 110), QColor(16, 20, 16), QString()});
            tags.push_back({short_(d.cost) + u" RP"_s, QColor(20, 22, 28), gold, QString()});
            if (d.inv)
                tags.push_back({QString::number(d.inv), QColor(20, 22, 28), QColor(246, 226, 130), u"bulb"_s});
            for (const QString &u : d.unlocks)
                tags.push_back({u, mix(CARD_BASE, rgb, 0.55), QColor(255, 255, 255), u"key"_s});
            p.setFont(sbold);
            for (size_t ti = 0; ti < tags.size(); ++ti) {
                const Tag &tg = tags[ti];
                const int iw = tg.icon.isEmpty() ? 0 : 15;
                const int full = sbm.horizontalAdvance(tg.text) + 14 + iw;
                const double tw = std::min<double>(full, right - tx);
                if (tw < full && tw < 36 + iw) {     // no room for it (not even cut short): "+N"
                    const size_t left_over = tags.size() - ti;
                    if (right - tx >= 30) {
                        if (tg.icon == u"key") {     // … what is left are unlocks: their ring before the number
                            p.setPen(QPen(QColor(214, 218, 226), 1.8));
                            p.setBrush(Qt::NoBrush);
                            p.drawEllipse(QPointF(tx + 6, foot_y + 12), 3.6, 3.6);
                            p.drawLine(QPointF(tx + 8.5, foot_y + 9.5), QPointF(tx + 11, foot_y + 7));
                            tx += 15;
                        }
                        p.setPen(QColor(214, 218, 226));
                        drawtext::draw(p, QRectF(tx, foot_y, right - tx, 22), Qt::AlignLeft | Qt::AlignVCenter,
                                   u"+%1"_s.arg(left_over));
                    }
                    break;
                }
                const QRectF tr(tx, foot_y + 1, tw, 21);
                p.setPen(QPen(QColor(92, 98, 112), 1.2));
                p.setBrush(tg.bg);
                p.drawRoundedRect(tr, 7, 7);
                if (tg.icon == u"bulb") {            // an invention: a little lamp
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(246, 214, 96));
                    p.drawEllipse(QPointF(tx + 11, foot_y + 10), 4.6, 4.6);
                    p.drawRect(QRectF(tx + 8.8, foot_y + 14.5, 4.4, 3));
                } else if (tg.icon == u"key") {      // an unlock: an open ring
                    p.setPen(QPen(QColor(255, 255, 255), 1.8));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(QPointF(tx + 10.5, foot_y + 11.5), 3.6, 3.6);
                    p.drawLine(QPointF(tx + 13, foot_y + 9), QPointF(tx + 15.5, foot_y + 6.5));
                }
                p.setPen(tg.ink);
                drawtext::draw(p, tr.adjusted(7 + iw, 0, -5, 0), Qt::AlignLeft | Qt::AlignVCenter,
                           sbm.elidedText(tg.text, Qt::ElideRight, static_cast<int>(tw - 12 - iw)));
                tx += tw + 5;
            }
            p.restore();
            const bool flashed = flash && *flash == k;
            if ((match && match->count(k)) || flashed) {
                p.setPen(QPen(QColor(238, 198, 98), !flashed ? 2.5 : 4.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(5, 5, -5, -5), 6, 6);
            }
            if (_drag && k != _drag->first && r.contains(_drag->second)) {
                const bool ok = _can_drop(_drag->first, k);
                p.setPen(QPen(ok ? QColor(96, 200, 110) : QColor(230, 80, 70), 3.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 8, 8);
            }
            _cells.emplace_back(r, k);
        }
    }
    if (_drag) {                                     // the dragged card follows the mouse
        const auto &[k, pos] = *_drag;
        const Info d = info_or(info, k);
        const QRectF gr(pos.x() - cw / 2, pos.y() - 20, cw, 40);
        p.setPen(QPen(gold, 2.5));
        p.setBrush(QColor(60, 66, 80, 232));
        p.drawRoundedRect(gr, 8, 8);
        p.setFont(f);
        p.setPen(QColor(240, 240, 240));
        drawtext::draw(p, gr.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(u"⇅ "_s + d.name, Qt::ElideRight, static_cast<int>(cw - 16)));
    }
    p.end();
}

bool TechGrid::_can_drop(const QString &key, const QString &target) const
{
    const Info *a = info.get(key), *b = info.get(target);
    return a && b && a->col.a == b->col.a && a->year == b->year;   // (Python: the col tuples are always truthy)
}

OptStr TechGrid::_hit(const QPointF &pos) const
{
    for (const auto &[r, k] : _cells)
        if (r.contains(pos))
            return k;
    return std::nullopt;
}

void TechGrid::mousePressEvent(QMouseEvent *ev)
{
    const OptStr k = _hit(ev->position());
    if (k && !k->isEmpty())
        _press = std::make_pair(*k, ev->position());
    else
        _press.reset();
}

void TechGrid::mouseReleaseEvent(QMouseEvent *ev)
{
    if (_drag) {
        const QString k = _drag->first;
        _drag.reset();
        const OptStr t = _hit(ev->position());
        update();
        if (t && !t->isEmpty() && *t != k)
            Q_EMIT dropped(k, *t);
        return;
    }
    if (_press)
        Q_EMIT picked(_press->first);
    _press.reset();
}

void TechGrid::mouseMoveEvent(QMouseEvent *ev)
{
    if (_press && (ev->buttons() & Qt::LeftButton)) {
        if (!_drag && (ev->position() - _press->second).manhattanLength() > 8)
            _drag = std::make_pair(_press->first, ev->position());
        if (_drag) {
            _drag = std::make_pair(_drag->first, ev->position());
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
    }
    const OptStr k = _hit(ev->position());
    const bool on = k && !k->isEmpty();
    setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (on) {
        const Info d = info_or(info, *k);
        QString fx;
        for (const Fx &e : d.fx)
            fx += u"\n   %1 %2"_s.arg(e.num, e.words);
        QToolTip::showText(
            ev->globalPosition().toPoint(),
            u"%1 (%2)\n%3 · %4 research points%5%6%7%8"_s.arg(
                d.name, *k, d.year, fmt(d.cost),
                d.inv ? u"\n%1 invention%2 hang on it"_s.arg(d.inv).arg(d.inv == 1 ? QString() : u"s"_s) : QString(),
                !d.unlocks.isEmpty() ? u"\nunlocks "_s + d.unlocks.join(u", "_s) : QString(),
                !fx.isEmpty() ? u"\nwhat it does:"_s + fx : QString(),
                d.same ? u"\n⇅ same year as its neighbour — drag it onto the other one to change the order"_s
                       : QString()),
            this);
    }
}

// ---------------------------------------------------------------------------------------------- effects
ValueField::ValueField(const QString &kind_, const Options &options, const QString &value, const QString &example,
                       QWidget *parent)
    : QWidget(parent), kind(kind_)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    if (kind == u"choice") {
        auto *box = new QComboBox(this);
        w = box;
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(8);
        box->setEditable(true);
        box->setInsertPolicy(QComboBox::NoInsert);
        if (options)
            for (const auto &[v, name] : *options)
                box->addItem(name == v ? name : u"%1  (%2)"_s.arg(name, v), v);
        if (!value.isEmpty() && box->findData(value) < 0)
            box->addItem(value, value);
        box->setCurrentIndex(!value.isEmpty() ? std::max(0, box->findData(value)) : 0);
        searchable(box);
        connect(box, &QComboBox::activated, this, [this](int) { Q_EMIT committed(this->value()); });
    } else if (kind == u"bool") {
        auto *cb = new QCheckBox(u"yes"_s);
        w = cb;
        cb->setChecked(py::lower(value) != u"no");
        cb->setText(cb->isChecked() ? u"yes"_s : u"no"_s);
        connect(cb, &QCheckBox::toggled, this, [this, cb](bool on) {
            cb->setText(on ? u"yes"_s : u"no"_s);
            Q_EMIT committed(this->value());
        });
    } else {
        auto *le = new QLineEdit(value, this);
        w = le;
        le->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (kind == u"number") {
            auto *v = new QDoubleValidator(le);
            v->setNotation(QDoubleValidator::StandardNotation);
            le->setValidator(v);
            le->setPlaceholderText(u"e.g. %1"_s.arg(example));
        }
        connect(le, &QLineEdit::editingFinished, this, [this] {
            if (!this->value().isEmpty())
                Q_EMIT committed(this->value());
        });
    }
    lay->addWidget(w);
}

QString ValueField::value() const
{
    if (kind == u"choice") {
        auto *box = static_cast<QComboBox *>(w);
        const QString t = py::strip(box->currentText());
        const int i = box->findText(t);
        return i >= 0 ? box->itemData(i).toString() : t;
    }
    if (kind == u"bool")
        return static_cast<QCheckBox *>(w)->isChecked() ? u"yes"_s : u"no"_s;
    return py::strip(static_cast<QLineEdit *>(w)->text());
}

void ValueField::focus() { w->setFocus(); }

EffectEditor::EffectEditor(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
}

void EffectEditor::set_block(const QString &text_, const IPath &ipath_, const std::set<QString> &skip_,
                             const Schema &schema_)
{
    schema = schema_;
    std::shared_ptr<const Node> keep;
    const Node *n = walk_to(root_of(text_, keep), ipath_);
    std::vector<std::pair<qsizetype, const Node *>> kids;
    if (n)
        for (size_t i = 0; i < n->kids.size(); ++i) {
            const Node &c = n->kids[i];
            if (c.has_key && !skip_.count(c.key))
                kids.emplace_back(static_cast<qsizetype>(i), &c);
        }
    // built again only when its own rows changed (not when another part of the block did: the cost, the year …)
    Sig sig{ipath_, skip_, pending, {}};
    for (const auto &[i, c] : kids)
        sig.kids.emplace_back(i, text_.mid(c->kstart, c->vend - c->kstart));
    text = text_;
    ipath = ipath_;
    skip = skip_;
    if (_sig && *_sig == sig)
        return;
    _sig = sig;
    QWidget *old = body;
    body = new QWidget(this);
    lay = new QVBoxLayout(body);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);
    long long rows = 0;
    for (const auto &[i, c] : kids)
        rows += _add_rows(*c, plus(ipath, i), 0, std::nullopt);
    if (!rows && !pending)
        lay->addWidget(label(u"Nothing yet."_s, u"faint"_s));
    if (pending && pending->ipath == ipath)
        lay->addWidget(_pending_row(ipath, std::nullopt, pending->key, 0));
    lay->addWidget(_adder(ipath, std::nullopt, 0));
    outer->addWidget(body);
    if (old) {
        outer->removeWidget(old);
        old->hide();
        old->setParent(nullptr);
        old->deleteLater();
    }
}

QString EffectEditor::_name(const QString &key) const
{
    if (schema)
        if (const OptStr n = schema.name(key); n && !n->isEmpty())
            return *n;
    if (mod.unit_types && mod.unit_types->types.contains(key))
        return mod.unit_types->name(key);
    if (const moddata::Good *g = mod.goods.get(key))
        return g->name;
    if (key == u"activate_building")
        return u"Unlocks building"_s;
    if (key == u"activate_unit")
        return u"Unlocks unit"_s;
    return techdata::effect_name(mod.loc, key);
}

long long EffectEditor::_add_rows(const Node &c, const IPath &ip, int depth, const OptStr &parent)
{
    if (c.is_block) {
        auto *head = new QWidget(body);
        auto *h = new QHBoxLayout(head);
        h->setContentsMargins(depth * 18, depth == 0 ? 6 : 2, 0, 0);
        h->setSpacing(6);
        QLabel *t = label(_name(c.key), u"sub"_s, false);
        t->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::GOLD));
        t->setToolTip(u"In the file: %1 = { … }"_s.arg(c.key));
        h->addWidget(t, 1);
        h->addWidget(_x(ip, u"Remove all of %1"_s.arg(_name(c.key))));
        lay->addWidget(head);
        long long n = 0;
        for (size_t i = 0; i < c.kids.size(); ++i)
            if (c.kids[i].has_key)
                n += _add_rows(c.kids[i], plus(ip, static_cast<qsizetype>(i)), depth + 1, c.key);
        if (pending && pending->ipath == ip)
            lay->addWidget(_pending_row(ip, c.key, pending->key, depth + 1));
        if (depth < 2)
            lay->addWidget(_adder(ip, c.key, depth + 1));
        return n + 1;
    }
    auto *row = new QWidget(body);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(depth * 18, 0, 0, 0);
    h->setSpacing(6);
    auto *name = new widgets::ElideLabel(_name(c.key));
    name->setToolTip(u"In the file: %1 = %2"_s.arg(c.key, c.value));
    h->addWidget(name, 1);
    const techdata::Kind kd = schema ? schema.kind(c.key, parent) : techdata::Kind{u"text"_s, std::nullopt};
    auto *f = new ValueField(kd.kind != u"block" ? kd.kind : u"text"_s, kd.options, c.value,
                             schema ? schema.example(c.key, parent) : u"0.1"_s, row);
    f->setFixedWidth(kd.kind == u"choice" ? 160 : 96);
    connect(f, &ValueField::committed, this, [this, ip, k = c.key, old = c.value](const QString &v) {
        if (v != old)
            Q_EMIT changed(techdata::set_value(text, ip, v), u"%1 → %2"_s.arg(k, v));
    });
    h->addWidget(f);
    h->addWidget(_x(ip, u"Remove %1"_s.arg(c.key)));
    lay->addWidget(row);
    return 1;
}

QPushButton *EffectEditor::_x(const IPath &ip, const QString &tip)
{
    QPushButton *x = x_button(body);
    x->setToolTip(tip);
    connect(x, &QPushButton::clicked, this, [this, ip, tip] { Q_EMIT changed(techdata::remove_node(text, ip), tip); });
    return x;
}

std::vector<std::pair<QString, QString>> EffectEditor::_options(const OptStr &parent) const
{
    std::vector<std::pair<QString, QString>> out;
    if (!parent) {
        for (const QString &k : schema.keys())
            out.emplace_back(k, u"%1  (%2)"_s.arg(_name(k), k));
        return out;
    }
    const techdata::Kind kd = schema.kind(*parent);
    if (kd.options)
        for (const auto &[k, n] : *kd.options)
            out.emplace_back(k, u"%1  (%2)"_s.arg(n, k));
    return out;
}

QWidget *EffectEditor::_adder(const IPath &ip, const OptStr &parent, int depth)
{
    // step 1: pick the effect — a new row then asks for its value
    auto *w = new QWidget(body);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(depth * 18, 3, 0, 0);
    h->setSpacing(6);
    const QString hint = !parent ? u"＋ %1 — click for the list, or type to search"_s.arg(add_text)
                                 : u"＋ Add to %1 — click for the list, or type"_s.arg(_name(*parent));
    auto *opener = new QPushButton(hint, w);         // stands in for the list until it is wanted
    opener->setObjectName(u"adderOpen"_s);
    opener->setStyleSheet(u"QPushButton#adderOpen { text-align: left; padding: 5px 9px; color: %1; }"_s.arg(
        theme::INK_DIM));
    opener->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    h->addWidget(opener, 1);
    connect(opener, &QPushButton::clicked, this,
            [this, w, h, opener, hint, ip, parent] { _open_adder(w, h, opener, hint, ip, parent); });
    return w;
}

QComboBox *EffectEditor::_open_adder(QWidget *w, QHBoxLayout *h, QPushButton *opener, const QString &hint,
                                     const IPath &ip, const OptStr &parent)
{
    // the real list of an "add" row, made when it is clicked: every effect it can take, searchable
    opener->hide();
    auto *box = new QComboBox(w);
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[k, t] : _options(parent))
        box->addItem(t, k);
    box->setCurrentIndex(-1);
    box->lineEdit()->setPlaceholderText(hint);
    searchable(box);
    auto pick = [this, box, ip, parent] {
        const QString t = py::strip(box->currentText());
        if (t.isEmpty())
            return;
        const int i = box->findText(t);
        QString k;
        if (i >= 0)
            k = box->itemData(i).toString();
        else if (t.endsWith(u')'))
            k = py::strip(rstrip_chars(py::split(t, u"("_s).last(), u')'));
        else
            k = QString(t).replace(u' ', u'_');
        if (k.isEmpty())
            return;
        const techdata::Kind kd = schema.kind(k, parent);
        if (kd.kind == u"block") {                   // a block needs no value: added empty, then filled
            pending.reset();
            Q_EMIT changed(techdata::add_block(text, ip, k), u"+ %1"_s.arg(k));
            return;
        }
        pending = Pending{ip, parent, k};
        _sig.reset();
        set_block(text, ipath, skip, schema);
        if (_pending_field)
            _pending_field->focus();
    };
    connect(box, &QComboBox::activated, this, [pick](int) { pick(); });
    connect(box->lineEdit(), &QLineEdit::returnPressed, this, pick);
    h->addWidget(box, 1);
    box->setFocus();
    box->showPopup();
    return box;
}

QFrame *EffectEditor::_pending_row(const IPath &ip, const OptStr &parent, const QString &key, int depth)
{
    // step 2: the new effect's value, in the field made for it, with the line it will write
    auto *frame = new QFrame(body);
    frame->setObjectName(u"pending"_s);
    frame->setStyleSheet(u"QFrame#pending { border: 2px solid %1; border-radius: 6px; }"_s.arg(theme::GOLD));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(8 + depth * 18, 6, 8, 6);
    v->setSpacing(4);
    auto *h = new QHBoxLayout();
    h->setSpacing(6);
    auto *new_lb = new widgets::ElideLabel(u"New: "_s + _name(key));   // (shortened, never wider than the panel)
    new_lb->setToolTip(_name(key));
    h->addWidget(new_lb, 1);
    const techdata::Kind kd = schema.kind(key, parent);
    auto *f = new ValueField(kd.kind, kd.options, kd.kind == u"bool" ? u"yes"_s : QString(), schema.example(key, parent));
    f->setFixedWidth(kd.kind == u"choice" ? 170 : 110);
    h->addWidget(f);
    auto *ok = new QPushButton(u"✓ Add"_s);
    QPushButton *cancel = x_button();
    h->addWidget(ok);
    h->addWidget(cancel);
    v->addLayout(h);
    auto *preview = new QLabel();
    preview->setFont(mono());
    preview->setStyleSheet(u"color: %1;"_s.arg(theme::INK_DIM));
    v->addWidget(preview);
    const QString ex = schema.example(key, parent);
    const QString hint = kd.kind == u"choice" ? u"pick one from the list (or type to search it)"_s
                         : kd.kind == u"bool" ? u"tick = yes, untick = no"_s
                         : kd.kind == u"number" ? u"a number, e.g. %1"_s.arg(ex)
                                                : u"a value"_s;
    v->addWidget(label(u"It takes "_s + hint + u"."_s, u"faint"_s));
    auto show = [f, preview, key] {
        const QString val = f->value();
        preview->setStyleSheet(u"color: %1;"_s.arg(theme::INK_DIM));
        preview->setText(u"In the file:  %1 = %2"_s.arg(key, !val.isEmpty() ? val : u"…"_s));
    };
    auto commit = [this, f, preview, ex, ip, key] {
        const QString val = f->value();
        if (val.isEmpty()) {
            preview->setText(u"⚠ Fill in a value first — e.g. %1"_s.arg(ex));
            preview->setStyleSheet(u"color: %1;"_s.arg(theme::ERROR_));
            return;
        }
        pending.reset();
        Q_EMIT changed(techdata::add_line(text, ip, key, val), u"+ %1 = %2"_s.arg(key, val));
    };
    auto drop = [this] {
        pending.reset();
        _sig.reset();
        set_block(text, ipath, skip, schema);
    };
    show();
    if (kd.kind == u"choice") {
        auto *box = static_cast<QComboBox *>(f->w);
        connect(box, &QComboBox::currentIndexChanged, f, [show](int) { show(); });
        connect(box, &QComboBox::editTextChanged, f, [show](const QString &) { show(); });
    } else if (kd.kind == u"bool")
        connect(static_cast<QCheckBox *>(f->w), &QCheckBox::toggled, f, [show](bool) { show(); });
    else {
        auto *le = static_cast<QLineEdit *>(f->w);
        connect(le, &QLineEdit::textChanged, f, [show](const QString &) { show(); });
        connect(le, &QLineEdit::returnPressed, this, commit);
    }
    connect(ok, &QPushButton::clicked, this, commit);
    connect(cancel, &QPushButton::clicked, this, drop);
    _pending_field = f;
    _pending_commit = commit;
    return frame;
}

// ---------------------------------------------------------------------------------------------- chance factors
ChanceEditor::ChanceEditor(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
}

void ChanceEditor::set_block(const QString &text_, const IPath &ipath_, const Schema &schema)
{
    std::shared_ptr<const Node> keep;
    const Node *n = walk_to(root_of(text_, keep), ipath_);
    const std::pair<IPath, QString> sig{ipath_, text_.mid(n->kstart, n->vend - n->kstart)};   // its own block
    text = text_;
    ipath = ipath_;
    if (_sig && *_sig == sig) {
        for (EffectEditor *ed : editors)
            ed->text = text_;
        return;
    }
    _sig = sig;
    QWidget *old = body;
    body = new QWidget(this);
    lay = new QVBoxLayout(body);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    editors.clear();
    std::vector<std::pair<qsizetype, const Node *>> mods;
    for (size_t i = 0; i < n->kids.size(); ++i)
        if (is(n->kids[i], u"modifier"_s) && n->kids[i].is_block)
            mods.emplace_back(static_cast<qsizetype>(i), &n->kids[i]);
    if (mods.empty())
        lay->addWidget(label(u"No factors — the chance is always the same."_s, u"faint"_s));
    for (const auto &[i, c] : mods) {
        auto *card = new QFrame(body);
        card->setObjectName(u"chance"_s);
        card->setStyleSheet(u"QFrame#chance { border: 1px solid %1; border-radius: 6px; }"_s.arg(theme::LINE));
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(8, 6, 8, 6);
        v->setSpacing(3);
        auto *h = new QHBoxLayout();
        h->addWidget(label(u"× factor"_s, u"sub"_s, false));
        std::optional<qsizetype> fi;
        for (size_t j = 0; j < c->kids.size(); ++j)
            if (is(c->kids[j], u"factor"_s)) {
                fi = static_cast<qsizetype>(j);
                break;
            }
        const QString fv = fi ? c->kids[static_cast<size_t>(*fi)].value : u"1"_s;
        auto *fe = new ValueField(u"number"_s, std::nullopt, fv, u"1.5"_s, card);
        fe->setFixedWidth(90);
        connect(fe, &ValueField::committed, this, [this, m = i, fi, fv](const QString &val) {
            if (val == fv)
                return;
            Q_EMIT changed(fi ? techdata::set_value(text, plus(plus(ipath, m), *fi), val)
                              : techdata::add_line(text, plus(ipath, m), u"factor"_s, val),
                           u"factor → %1"_s.arg(val));
        });
        h->addWidget(fe);
        h->addWidget(label(u"when:"_s, u"faint"_s, false), 1);
        QPushButton *x = x_button();
        x->setToolTip(u"Remove this factor"_s);
        connect(x, &QPushButton::clicked, this, [this, m = i] {
            Q_EMIT changed(techdata::remove_node(text, plus(ipath, m)), u"factor removed"_s);
        });
        h->addWidget(x);
        v->addLayout(h);
        auto *ed = new EffectEditor(mod, card);
        ed->add_text = u"Add a condition"_s;
        connect(ed, &EffectEditor::changed, this, &ChanceEditor::changed);
        ed->set_block(text_, plus(ipath, i), {u"factor"_s}, schema);
        v->addWidget(ed);
        editors.push_back(ed);
        lay->addWidget(card);
    }
    auto *add = new QPushButton(u"＋ Add a factor"_s);
    add->setToolTip(u"modifier = { factor = 1.5  … } — then pick its conditions"_s);
    connect(add, &QPushButton::clicked, this, [this] {
        Q_EMIT changed(techdata::add_block(text, ipath, u"modifier"_s, {u"factor = 1.5"_s}), u"+ factor"_s);
    });
    auto *row = new QHBoxLayout();
    row->addWidget(add);
    row->addStretch(1);
    auto *w = new QWidget(body);
    w->setLayout(row);
    lay->addWidget(w);
    outer->addWidget(body);
    if (old) {
        outer->removeWidget(old);
        old->hide();
        old->setParent(nullptr);
        old->deleteLater();
    }
}

// ---------------------------------------------------------------------------------------------- raw text
RawBox::RawBox(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    ed = new QPlainTextEdit();
    ed->setMinimumHeight(260);
    ed->setFont(mono());
    ed->setTabStopDistance(28);
    lay->addWidget(ed);
    err = label(QString(), u"warn"_s);
    err->hide();
    lay->addWidget(err);
    auto *row = new QHBoxLayout();
    row->addStretch(1);
    cancel = new QPushButton(u"Cancel"_s);
    ok = new QPushButton(u"Take the text"_s);
    row->addWidget(cancel);
    row->addWidget(ok);
    lay->addLayout(row);
    connect(ok, &QPushButton::clicked, this, &RawBox::_ok);
    connect(cancel, &QPushButton::clicked, this, &QWidget::hide);
}

void RawBox::open(const QString &key_, const QString &text)
{
    key = key_;
    ed->setPlainText(QString(text).replace(u"\r\n"_s, u"\n"_s));
    err->hide();
    show();
}

void RawBox::_ok()
{
    const QString t = ed->toPlainText();
    const QString problem = techdata::check_block(t, key.value_or(QString()));
    if (!problem.isEmpty()) {
        err->setText(u"⚠ "_s + problem);
        err->show();
        return;
    }
    hide();
    Q_EMIT saved(t);
}

FilePreview::FilePreview(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    box = new QCheckBox(u"Show as in the file"_s);
    lay->addWidget(box);
    view = new QPlainTextEdit();
    view->setReadOnly(true);
    view->setFont(mono());
    view->setTabStopDistance(28);
    view->setMinimumHeight(220);
    view->hide();
    lay->addWidget(view);
    connect(box, &QCheckBox::toggled, view, &QWidget::setVisible);
}

void FilePreview::set_text(const QString &text)
{
    const QString t = QString(text).replace(u"\r\n"_s, u"\n"_s);
    if (view->toPlainText() != t) {
        const int bar = view->verticalScrollBar()->value();
        view->setPlainText(t);
        view->verticalScrollBar()->setValue(bar);
    }
}

// ---------------------------------------------------------------------------------------------- the workspace
static const std::vector<std::pair<QString, long long>> GUI_Y = {
    {u"possible_inventions"_s, 384}, {u"sort_by_type"_s, 406}, {u"sort_by_name"_s, 405},
    {u"sort_by_percent"_s, 406},     {u"inventions"_s, 433},   {u"selected_tech_window"_s, 366}};
static constexpr long long ROW_H = 39;

// re.search(pattern, text, pos, endpos): the match, or none
static QRegularExpressionMatch search(const QRegularExpression &re, const QString &text, qsizetype pos = 0,
                                      qsizetype endpos = -1)
{
    if (endpos >= 0 && endpos < text.size())
        return re.match(text.left(endpos), pos);
    return re.match(text, pos);
}

static QRegularExpression name_re(const QString &name)
{
    return QRegularExpression(uR"(name\s*=\s*"?)"_s + name + uR"("?)"_s);
}

OptStr fit_tech_window(QString text, long long rows)
{
    const long long shift = std::max<long long>(0, rows - 6) * ROW_H;
    static const QRegularExpression pos(uR"((position\s*=\s*\{\s*x\s*=\s*-?\d+\s*y\s*=\s*)(-?\d+))"_s);
    for (const auto &[name, y] : GUI_Y) {
        const auto m = search(name_re(name), text);
        if (!m.hasMatch())
            return std::nullopt;
        const auto p = search(pos, text, m.capturedEnd(), m.capturedEnd() + 400);
        if (!p.hasMatch())
            return std::nullopt;
        text = text.left(p.capturedStart(2)) + QString::number(y + shift) + text.mid(p.capturedEnd(2));
    }
    const auto m = search(name_re(u"inventions"_s), text);
    static const QRegularExpression size(uR"((size\s*=\s*\{\s*x\s*=\s*\d+\s*y\s*=\s*)(\d+))"_s);
    const auto s = search(size, text, m.capturedEnd(), m.capturedEnd() + 400);
    if (!s.hasMatch())
        return std::nullopt;
    text = text.left(s.capturedStart(2)) + QString::number(std::max<long long>(60, 175 - shift)) +
           text.mid(s.capturedEnd(2));
    const auto w = search(name_re(u"selected_tech_window"_s), text);
    if (w.hasMatch()) {
        const auto d = search(name_re(u"desc"_s), text.mid(w.capturedEnd()));
        if (d.hasMatch()) {
            static const QRegularExpression mh(uR"((maxHeight\s*=\s*)(\d+))"_s);
            const qsizetype at = w.capturedEnd() + d.capturedEnd();
            const auto hh = search(mh, text, at, at + 300);
            if (hh.hasMatch())
                text = text.left(hh.capturedStart(2)) + QString::number(std::max<long long>(36, 100 - shift)) +
                       text.mid(hh.capturedEnd(2));
        }
    }
    return text;
}

static QGridLayout *grid_lay()
{
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    g->setColumnStretch(1, 1);
    return g;
}

static QScrollArea *side_scroll(QWidget *w)
{
    auto *s = new QScrollArea();
    s->setWidgetResizable(true);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setObjectName(u"sideScroll"_s);
    s->setWidget(w);
    return s;
}

TechWorkspace::TechWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(16, 10, 16, 8);
    bar_layout->setSpacing(0);
    folder_group = new QButtonGroup(this);
    search = new QLineEdit();
    search->setPlaceholderText(u"Search technologies and inventions (name, key, effect)…"_s);
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(240);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_grid(false); });
    search_note = label(QString(), u"faint"_s, false);
    row_btn = new QPushButton(u"＋ Add the 7th tech row"_s);
    row_btn->setToolTip(u"One new technology at the bottom of every column of every folder — the game needs "
                        "all columns equally long, or it crashes"_s);
    connect(row_btn, &QPushButton::clicked, this, &TechWorkspace::rowRequested);
    row_rm_btn = new QPushButton(u"✕ Remove the 7th tech row"_s);
    row_rm_btn->setToolTip(u"The bottom row of every column goes (only rows past the 6th)"_s);
    connect(row_rm_btn, &QPushButton::clicked, this, &TechWorkspace::rowRemoveRequested);
    fit_btn = new QPushButton(u"Fit the game's window"_s);
    fit_btn->setToolTip(u"The game's technology window has room for six rows: this changes the mod's "
                        "interface/country_technology.gui so the rows you have fit (what is under them "
                        "moves down)"_s);
    connect(fit_btn, &QPushButton::clicked, this, [this] { Q_EMIT guiPatchRequested(_rows_now); });
    fit_btn->hide();
    connect(search, &QLineEdit::returnPressed, this, &TechWorkspace::_search_jump);
    lay->addWidget(bar);
    rows_warn = label(QString(), u"warn"_s);
    rows_warn->hide();
    lay->addWidget(rows_warn);

    split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    grid = new TechGrid();
    connect(grid, &TechGrid::picked, this, &TechWorkspace::select);
    connect(grid, &TechGrid::dropped, this, &TechWorkspace::_dropped);
    auto *gw = new QWidget();
    auto *gl = new QVBoxLayout(gw);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->addWidget(grid);
    gl->addStretch(1);
    QScrollArea *gs = side_scroll(gw);
    split->addWidget(gs);
    gs->viewport()->installEventFilter(new Room(this, grid));

    // --- the technology
    detail = new QWidget();
    auto *dl = new QVBoxLayout(detail);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(0);
    auto *s = new Section();
    t_title = label(u"Pick a technology"_s, u"title"_s);
    s->lay->addWidget(t_title);
    t_sub = label(QString(), u"faint"_s);
    s->lay->addWidget(t_sub);
    QGridLayout *g = grid_lay();
    g->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    t_name = new QLineEdit();
    connect(t_name, &QLineEdit::editingFinished, this, [this] { _name(sel, t_name->text()); });
    g->addWidget(t_name, 0, 1);
    s->lay->addLayout(g);
    dl->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"Research"_s));
    g = grid_lay();
    g->addWidget(label(u"Available from"_s, u"sub"_s, false), 0, 0);
    t_year = new widgets::Stepper(1700, 2100, 1836);
    connect(t_year, &widgets::Stepper::valueChanged, this, [this](int v) { _field(u"year"_s, QString::number(v)); });
    auto *yr = new QHBoxLayout();
    yr->addWidget(t_year);
    yr->addStretch(1);
    g->addLayout(yr, 0, 1);
    g->addWidget(label(u"Research cost"_s, u"sub"_s, false), 1, 0);
    t_cost = new QLineEdit();
    t_cost->setValidator(new QIntValidator(0, 1000000000, t_cost));
    t_cost->setFixedWidth(120);
    t_cost->setToolTip(u"Research points"_s);
    connect(t_cost, &QLineEdit::editingFinished, this, [this] { _field(u"cost"_s, py::strip(t_cost->text())); });
    auto *cr = new QHBoxLayout();
    cr->addWidget(t_cost);
    cr->addWidget(label(u"RP"_s, u"faint"_s, false));
    cr->addStretch(1);
    g->addLayout(cr, 1, 1);
    g->addWidget(label(u"Column"_s, u"sub"_s, false), 2, 0);
    t_area = new QComboBox();
    t_area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(t_area, &QComboBox::activated, this, [this](int) {
        const QVariant d = t_area->currentData();
        _field(u"area"_s, d.isValid() ? OptStr(d.toString()) : std::nullopt);
    });
    g->addWidget(t_area, 2, 1);
    s->lay->addLayout(g);
    t_order = label(QString(), u"sub"_s);
    t_order->setTextFormat(Qt::RichText);
    s->lay->addWidget(t_order);
    same_row = new QWidget();
    auto *sr = new QHBoxLayout(same_row);
    sr->setContentsMargins(0, 0, 0, 0);
    same_note = label(QString(), u"faint"_s);
    same_note->setStyleSheet(u"color: rgb(238, 198, 98);"_s);
    sr->addWidget(same_note, 1);
    up_btn = new QPushButton(u"▲ Earlier"_s);
    dn_btn = new QPushButton(u"▼ Later"_s);
    connect(up_btn, &QPushButton::clicked, this, [this] {
        if (sel)
            Q_EMIT orderRequested(edits->area_of(*sel), *sel, -1);
    });
    connect(dn_btn, &QPushButton::clicked, this, [this] {
        if (sel)
            Q_EMIT orderRequested(edits->area_of(*sel), *sel, 1);
    });
    sr->addWidget(up_btn);
    sr->addWidget(dn_btn);
    s->lay->addWidget(same_row);
    t_unciv = new QCheckBox(u"Military tech for uncivilized nations (unciv_military)"_s);
    connect(t_unciv, &QCheckBox::clicked, this,
            [this](bool on) { _field(u"unciv_military"_s, on ? OptStr(u"yes"_s) : std::nullopt); });
    auto *ur = new QHBoxLayout();
    ur->setContentsMargins(0, 0, 0, 0);
    ur->addWidget(t_unciv, 1);
    ur->addWidget(new widgets::HelpDot(unciv_note()));
    s->lay->addLayout(ur);
    dl->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"What it does"_s));
    t_effects = new EffectEditor(mod);
    connect(t_effects, &EffectEditor::changed, this,
            [this](const QString &t, const QString &what) { _set_text(sel, t, what); });
    s->lay->addWidget(t_effects);
    t_ai = label(QString(), u"sub"_s);
    s->lay->addWidget(t_ai);
    t_chance = new ChanceEditor(mod);
    connect(t_chance, &ChanceEditor::changed, this,
            [this](const QString &t, const QString &what) { _set_text(sel, t, u"AI chance: "_s + what); });
    s->lay->addWidget(t_chance);
    t_file = new FilePreview();
    s->lay->addWidget(t_file);
    auto *row = new QHBoxLayout();
    row->addStretch(1);
    t_raw_btn = new QPushButton(u"Edit as text…"_s);
    t_raw_btn->setToolTip(u"The whole block — for anything the rows above don't cover (AI chance, …)"_s);
    connect(t_raw_btn, &QPushButton::clicked, this, [this] {
        if (sel)
            t_raw->open(*sel, edits->text(*sel));
    });
    row->addWidget(t_raw_btn);
    s->lay->addLayout(row);
    t_raw = new RawBox();
    t_raw->hide();
    connect(t_raw, &RawBox::saved, this, [this](const QString &t) { _set_text(sel, t, u"edited as text"_s); });
    s->lay->addWidget(t_raw);
    dl->addWidget(s);
    dl->addStretch(1);
    split->addWidget(side_scroll(detail));

    // --- the inventions
    inv = new QWidget();
    auto *il = new QVBoxLayout(inv);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(0);
    s = new Section();
    i_head = label(u"Inventions"_s, u"label"_s);
    s->lay->addWidget(i_head);
    i_list = new QListWidget();
    i_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    i_list->setTextElideMode(Qt::ElideRight);
    connect(i_list, &QListWidget::currentRowChanged, this, &TechWorkspace::_inv_row);
    s->lay->addWidget(i_list);
    row = new QHBoxLayout();
    row->setSpacing(6);
    i_new_name = new QLineEdit();
    i_new_name->setPlaceholderText(u"name of a new invention for this technology"_s);
    connect(i_new_name, &QLineEdit::returnPressed, this, &TechWorkspace::_inv_new);
    row->addWidget(i_new_name, 1);
    i_new_btn = new QPushButton(u"＋ New invention"_s);
    i_new_btn->setToolTip(u"A new invention that comes with the picked technology: a base chance of 2 % a "
                          "year and no effect yet — give it its effects below. Its name is written to the "
                          "localisation."_s);
    connect(i_new_btn, &QPushButton::clicked, this, &TechWorkspace::_inv_new);
    row->addWidget(i_new_btn);
    s->lay->addLayout(row);
    i_new_msg = label(QString(), u"sub"_s);
    i_new_msg->setStyleSheet(u"color: #ff786e;"_s);
    i_new_msg->hide();
    s->lay->addWidget(i_new_msg);
    il->addWidget(s);
    s = i_section = new Section();
    i_title = label(QString(), u"title"_s);
    s->lay->addWidget(i_title);
    i_sub = label(QString(), u"faint"_s);
    s->lay->addWidget(i_sub);
    g = grid_lay();
    g->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    i_name = new QLineEdit();
    connect(i_name, &QLineEdit::editingFinished, this, [this] { _name(sel_inv, i_name->text()); });
    g->addWidget(i_name, 0, 1);
    g->addWidget(label(u"Description"_s, u"sub"_s, false), 1, 0, Qt::AlignTop);
    i_desc = new NoteEdit();
    i_desc->setFixedHeight(76);
    connect(i_desc, &NoteEdit::done, this, [this](const QString &t) {
        _name(sel_inv ? OptStr(*sel_inv + u"_desc"_s) : std::nullopt, t);
    });
    g->addWidget(i_desc, 1, 1);
    g->addWidget(label(u"Comes with"_s, u"sub"_s, false), 2, 0);
    i_needs = new QComboBox();
    i_needs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    i_needs->setEditable(true);
    i_needs->setInsertPolicy(QComboBox::NoInsert);
    i_needs->setToolTip(u"The technology it needs (limit = { <technology> = 1 }) — pick another to move it"_s);
    connect(i_needs, &QComboBox::activated, this, [this](int) { _inv_needs(); });
    g->addWidget(i_needs, 2, 1);
    g->addWidget(label(u"Base chance"_s, u"sub"_s, false), 3, 0);
    cr = new QHBoxLayout();
    i_chance = new widgets::Stepper(0, 1000, 20);
    connect(i_chance, &widgets::Stepper::valueChanged, this, [this](int v) { _inv_chance(v); });
    cr->addWidget(i_chance);
    i_chance_note = label(QString(), u"faint"_s);
    cr->addWidget(i_chance_note, 1);
    g->addLayout(cr, 3, 1);
    s->lay->addLayout(g);
    i_need_note = label(QString(), u"faint"_s);
    s->lay->addWidget(i_need_note);
    s->lay->addWidget(label(u"What changes the chance"_s, u"sub"_s));
    i_chance_ed = new ChanceEditor(mod);
    connect(i_chance_ed, &ChanceEditor::changed, this,
            [this](const QString &t, const QString &what) { _set_text(sel_inv, t, u"chance: "_s + what); });
    s->lay->addWidget(i_chance_ed);
    i_news = new QCheckBox(u"In the newspaper"_s);
    i_news->setToolTip(u"Unticked: news = no — no newspaper article when it's invented"_s);
    connect(i_news, &QCheckBox::clicked, this, &TechWorkspace::_inv_news);
    s->lay->addWidget(i_news);
    s->lay->addWidget(label(u"What it does"_s, u"sub"_s));
    i_effects = new EffectEditor(mod);
    connect(i_effects, &EffectEditor::changed, this,
            [this](const QString &t, const QString &what) { _set_text(sel_inv, t, what); });
    s->lay->addWidget(i_effects);
    i_mods_title = label(u"Country modifiers"_s, u"sub"_s);
    s->lay->addWidget(i_mods_title);
    i_mods = new EffectEditor(mod);
    connect(i_mods, &EffectEditor::changed, this,
            [this](const QString &t, const QString &what) { _set_text(sel_inv, t, what); });
    s->lay->addWidget(i_mods);
    i_file = new FilePreview();
    s->lay->addWidget(i_file);
    row = new QHBoxLayout();
    row->addStretch(1);
    i_del = new QPushButton(u"✕ Delete this invention"_s);
    i_del->setToolTip(u"Asks once more: click twice"_s);
    connect(i_del, &QPushButton::clicked, this, &TechWorkspace::_inv_delete);
    row->insertWidget(0, i_del);
    i_raw_btn = new QPushButton(u"Edit as text…"_s);
    connect(i_raw_btn, &QPushButton::clicked, this, [this] {
        if (sel_inv)
            i_raw->open(*sel_inv, edits->text(*sel_inv));
    });
    row->addWidget(i_raw_btn);
    s->lay->addLayout(row);
    i_raw = new RawBox();
    i_raw->hide();
    connect(i_raw, &RawBox::saved, this, [this](const QString &t) { _set_text(sel_inv, t, u"edited as text"_s); });
    s->lay->addWidget(i_raw);
    il->addWidget(s);
    il->addStretch(1);
    split->addWidget(side_scroll(inv));
    split->widget(0)->setMinimumWidth(520);
    for (int i : {1, 2})
        split->widget(i)->setMinimumWidth(340);
    split->setStretchFactor(0, 6);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 3);
    split->setSizes({1000, 480, 480});
    lay->addWidget(split, 1);
    schools = new defpanel::DefView(mod, defpanel::SCHOOLS);
    schools->hide();
    lay->addWidget(schools, 1);
}

TechWorkspace::~TechWorkspace() = default;

void TechWorkspace::set_unciv(bool on)
{
    unciv_on = on;
    split->setVisible(!on);
    schools->setVisible(on);
    search->setEnabled(!on);
    row_btn->setVisible(!on);
    if (on) {
        row_rm_btn->hide();
        rows_warn->hide();
        if (doc)
            schools->refresh(doc);
    }
}

// ------------------------------------------------------------------ data helpers
QString TechWorkspace::name(const OptStr &key) const
{
    const QString k = key.value_or(QString());
    localisation::LocEdits *loc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    if (loc)
        if (const OptStr t = loc->text(k); t && !t->isEmpty())
            return *t;
    if (const QString *l = mod.loc.get(k); l && !l->isEmpty())
        return *l;
    return techdata::pretty(key);
}

QString TechWorkspace::_fx_name(const QString &key)
{
    // an effect's name as the cards say it (a unit, a good, a modifier as the game calls it)
    if (const auto it = _fx_names.find(key); it != _fx_names.end())
        return it->second;
    QString got;
    if (mod.unit_types && mod.unit_types->types.contains(key))
        got = mod.unit_types->name(key);
    else if (const moddata::Good *g = mod.goods.get(key))
        got = g->name;
    else
        got = techdata::effect_name(mod.loc, key);
    _fx_names[key] = got;
    return got;
}

static QString loc_or_pretty(const moddata::Mod &mod, const QString &k)
{
    if (const QString *l = mod.loc.get(k); l && !l->isEmpty())
        return *l;
    return techdata::pretty(k);
}

static QString file_name(const QString &fn) { return fn.mid(fn.lastIndexOf(u'\\') + 1); }   // fn.split("\\")[-1]

OrderedMap<QString, Info> &TechWorkspace::_compute()
{
    // columns from the edited texts; per technology its card; inventions per technology
    techdata::TechEdits &ed = *edits;
    const QStringList keys = ed.keys();
    const QSet<QString> techs(keys.begin(), keys.end());
    OrderedMap<QString, QStringList> inv_of;
    for (const QString &i : ed.inventions())
        for (const QString &t : techdata::tech_refs(ed.text(i), techs))
            inv_of[t].append(i);
    _inv_of = inv_of;
    OrderedMap<QString, OptStr> areas;
    for (const QString &k : keys)
        areas.insert(k, ed.area_of(k));
    std::vector<Folder> folders;
    if (countryhist::NationData *data = mod.nation_data.get())
        for (const auto &[folder_, fareas] : data->folders) {
            Folder fo{folder_, loc_or_pretty(mod, folder_), {}};
            for (const QString &a : fareas)
                fo.areas.push_back({a, loc_or_pretty(mod, a), ed.column(a, &areas)});
            folders.push_back(fo);
        }
    _folders_cache = folders;
    OrderedMap<QString, Info> info;
    for (const Folder &fo : folders)
        for (const Area &ar : fo.areas) {
            std::vector<long long> years;
            for (const QString &k : ar.keys)
                years.push_back(techdata::year_of(ed.text(k)));
            for (qsizetype j = 0; j < ar.keys.size(); ++j) {
                const QString &k = ar.keys[j];
                const QString text = ed.text(k);
                const auto f = techdata::fields(text);
                std::shared_ptr<const Node> keep;
                const Node *r = root_of(text, keep);
                QStringList unl;
                if (r)
                    for (const Node &c : r->kids) {
                        if (is(c, u"activate_unit"_s) && !c.is_block)
                            unl << (mod.unit_types ? mod.unit_types->name(c.value) : techdata::pretty(c.value));
                        else if (is(c, u"activate_building"_s) && !c.is_block)
                            unl << techdata::pretty(c.value);
                    }
                const size_t uj = static_cast<size_t>(j);
                const bool same = (j > 0 && years[uj - 1] == years[uj]) ||
                                  (uj + 1 < years.size() && years[uj + 1] == years[uj]);
                Info d;
                d.name = name(k);
                d.year = f.value(u"year"_s, u"?"_s);
                d.cost = f.value(u"cost"_s, u"0"_s);
                d.unlocks = unl;
                d.inv = inv_of.contains(k) ? inv_of.value(k).size() : 0;
                d.same = same;
                d.isnew = ed.added.contains(k);
                d.col = Col{ar.a, ar.title, ar.keys, j};
                d.fx = effect_lines(text, [this](const QString &x) { return _fx_name(x); });
                info.insert(k, d);
            }
        }
    _info_cache = info;
    return _info_cache;
}

// ------------------------------------------------------------------ refresh
void TechWorkspace::refresh(document::Document *doc_)
{
    doc = doc_;
    edits = doc && doc->maps.contains(u"techs"_s) ? &doc->map<techdata::TechEdits>(u"techs"_s) : nullptr;
    if (!edits)
        return;
    if (!schema) {
        schema = std::make_unique<techdata::EffectSchema>(mod, edits->db);
        cond_schema = std::make_unique<techdata::ConditionSchema>(mod, edits->db);
    }
    _compute();
    _fill_grid(false);
    _fill_tech();
    _fill_inv();
    if (unciv_on)
        schools->refresh(doc);
}

void TechWorkspace::_fill_grid(bool compute)
{
    if (!edits)
        return;
    const OrderedMap<QString, Info> &info = compute ? _compute() : _info_cache;
    const std::vector<Folder> &folders = _folders_cache;
    const QString q = py::lower(py::strip(search->text()));
    std::optional<std::set<QString>> match;
    if (!q.isEmpty()) {
        match.emplace();
        for (const QString &k : edits->keys()) {
            const Info *d = info.get(k);
            const QString blob = py::lower(k + u' ' + (d ? d->name : QString()) + u' ' + edits->text(k));
            bool hit = blob.contains(q);
            if (!hit && _inv_of.contains(k))
                for (const QString &i : _inv_of.value(k))
                    if (py::lower(i + u' ' + name(i) + u' ' + edits->text(i)).contains(q)) {
                        hit = true;
                        break;
                    }
            if (hit)
                match->insert(k);
        }
    }
    if (folder_btns.empty()) {
        QHBoxLayout *bl = bar_layout;
        for (size_t i = 0; i < folders.size(); ++i) {
            auto *b = new QPushButton(folders[i].title);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            connect(b, &QPushButton::clicked, this, [this, i] { _set_folder(static_cast<long long>(i)); });
            folder_group->addButton(b);
            bl->addWidget(b, 1);
            folder_btns.push_back(b);
        }
        auto *b = schools_btn = new QPushButton(u"Tech schools"_s);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setToolTip(u"The technology schools: how fast a nation researches each kind of technology"_s);
        connect(b, &QPushButton::clicked, this, [this] { set_unciv(true); });
        folder_group->addButton(b);
        bl->addWidget(b, 1);
        bl->addSpacing(16);
        bl->addWidget(search, 2);
        bl->addSpacing(8);
        bl->addWidget(search_note);
        bl->addSpacing(12);
        bl->addWidget(row_btn);
        bl->addSpacing(6);
        bl->addWidget(row_rm_btn);
        bl->addSpacing(6);
        bl->addWidget(fit_btn);
    }
    for (size_t i = 0; i < folders.size() && i < folder_btns.size(); ++i) {
        long long n = 0;
        if (match)
            for (const Area &ar : folders[i].areas)
                for (const QString &k : ar.keys)
                    n += match->count(k);
        folder_btns[i]->setText(match && n ? u"%1  (%2)"_s.arg(folders[i].title, QString::number(n)) : folders[i].title);
        folder_btns[i]->setChecked(static_cast<long long>(i) == folder && !unciv_on);
    }
    search_note->setText(match ? u"%1 match%2"_s.arg(match->size()).arg(match->size() == 1 ? QString() : u"es"_s)
                               : QString());
    std::set<long long> lens;
    for (const Folder &fo : folders)
        for (const Area &ar : fo.areas)
            lens.insert(ar.keys.size());
    const long long rows = lens.empty() ? 0 : *lens.rbegin();
    row_btn->setText(u"＋ Add the %1 tech row"_s.arg(ordinal(rows + 1)));
    row_rm_btn->setText(u"✕ Remove the %1 tech row"_s.arg(ordinal(rows)));
    row_rm_btn->setVisible(rows >= 7 && !unciv_on);
    _rows_now = rows;
    fit_btn->setVisible(rows >= 7 && !unciv_on);
    fit_btn->setText(u"Fit the game's window to %1 rows"_s.arg(rows));
    row_btn->setVisible(!unciv_on);
    _search_items(info);
    if (lens.size() > 1 && !unciv_on) {
        QStringList short_cols;
        for (const Folder &fo : folders)
            for (const Area &ar : fo.areas)
                if (ar.keys.size() != rows)
                    short_cols << u"%1 (%2)"_s.arg(ar.title, QString::number(ar.keys.size()));
        rows_warn->setText(u"  ⚠ The columns have different numbers of technologies — the game needs them all "
                           "equally long (%1): %2"_s.arg(QString::number(rows), short_cols.mid(0, 8).join(u", "_s)));
        rows_warn->show();
    } else
        rows_warn->hide();
    if (!folders.empty()) {
        grid->match = match;
        grid->sel = sel;
        grid->set_folder(folders[static_cast<size_t>(folder)].areas, info);
    }
    t_area->blockSignals(true);
    if (t_area->count() == 0)
        for (const Folder &fo : folders)
            for (const Area &ar : fo.areas)
                t_area->addItem(u"%1 — %2"_s.arg(fo.title, ar.title), ar.a);
    t_area->blockSignals(false);
}

void TechWorkspace::_set_folder(long long i)
{
    folder = i;
    if (unciv_on)
        set_unciv(false);
    _fill_grid(false);
}

void TechWorkspace::select(const QString &key)
{
    if (grid->flash != OptStr(key))
        grid->flash.reset();
    sel = key;
    sel_inv.reset();
    for (size_t i = 0; i < _folders_cache.size(); ++i)
        for (const Area &ar : _folders_cache[i].areas)
            if (ar.keys.contains(key)) {
                folder = static_cast<long long>(i);
                break;
            }
    t_raw->hide();
    i_raw->hide();
    t_effects->pending.reset();
    _fill_grid(false);
    _fill_tech();
    _fill_inv();
}

void TechWorkspace::_fill_tech()
{
    const bool on = sel && edits && edits->exists(*sel);
    for (QWidget *w : std::initializer_list<QWidget *>{t_name, t_year, t_cost, t_area, t_unciv, t_raw_btn})
        w->setEnabled(on);
    const Schema sch{schema.get(), nullptr};
    if (!on) {
        t_title->setText(u"Pick a technology in the tree"_s);
        t_sub->setText(QString());
        t_order->setText(QString());
        same_row->hide();
        t_effects->set_block(u"x = { }"_s, {}, {}, sch);
        t_file->set_text(QString());
        return;
    }
    const QString k = *sel;
    const QString text = edits->text(k);
    const auto f = techdata::fields(text);
    t_title->setText(name(k));
    Col col;
    if (const Info *d = _info_cache.get(k))
        col = d->col;
    else {
        const QString *a = f.get(u"area"_s);
        col = Col{a ? OptStr(*a) : std::nullopt, a ? *a : u"None"_s, {k}, 0};
    }
    QString folder_t = u"?"_s;
    for (const Folder &fo : _folders_cache) {
        bool found = false;
        for (const Area &ar : fo.areas)
            if (col.a && ar.a == *col.a)
                found = true;
        if (found) {
            folder_t = fo.title;
            break;
        }
    }
    const QString fn = edits->db.entries.contains(k) ? edits->db.entries.get(k)->file : edits->added.get(k)->file;
    t_sub->setText(u"%1 › %2 · %3 · %4%5"_s.arg(folder_t, col.at, k, file_name(fn),
                                                edits->added.contains(k) ? u"  · new, written on Apply"_s
                                                : edits->want.contains(k) ? u"  · changed, written on Apply"_s
                                                                          : QString()));
    const QStringList &ks = col.ks;
    const long long j = col.j;
    const OptStr prev = j > 0 ? OptStr(ks[j - 1]) : std::nullopt;
    const OptStr nxt = j + 1 < ks.size() ? OptStr(ks[j + 1]) : std::nullopt;
    t_order->setText(u"%1 in its column · needs first: <b>%2</b> · then: %3"_s.arg(
        ordinal(j + 1), prev ? name(prev) : u"nothing (the first)"_s, nxt ? name(nxt) : u"nothing (the last)"_s));
    const long long yo = techdata::year_of(text);
    QStringList same;
    for (const QString &x : ks)
        if (x != k && techdata::year_of(edits->text(x)) == yo)
            same << x;
    same_row->setVisible(!same.isEmpty());
    if (!same.isEmpty()) {
        QStringList names;
        for (const QString &x : same)
            names << name(x);
        same_note->setText(u"⇅ Same year as %1 — which comes first is your choice:"_s.arg(names.join(u", "_s)));
        up_btn->setEnabled(j > 0 && techdata::year_of(edits->text(ks[j - 1])) == yo);
        dn_btn->setEnabled(nxt && techdata::year_of(edits->text(*nxt)) == yo);
    }
    if (!t_name->hasFocus())
        t_name->setText(name(k));
    t_year->blockSignals(true);
    try {
        t_year->setValue(py::to_int(f.value(u"year"_s, u"1836"_s)));
    } catch (const py::ValueError &) {
    }
    t_year->blockSignals(false);
    if (!t_cost->hasFocus())
        t_cost->setText(f.value(u"cost"_s));
    {
        const QString *a = f.get(u"area"_s);
        t_area->setCurrentIndex(std::max(0, a ? t_area->findData(*a) : -1));
    }
    t_unciv->setChecked(py::lower(f.value(u"unciv_military"_s)) == u"yes");
    t_effects->set_block(text, {}, std::set<QString>(techdata::TECH_BASE.begin(), techdata::TECH_BASE.end()), sch);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::optional<qsizetype> ai;
    for (size_t n = 0; n < r->kids.size(); ++n)
        if (is(r->kids[n], u"ai_chance"_s) && r->kids[n].is_block) {
            ai = static_cast<qsizetype>(n);
            break;
        }
    if (ai) {
        QString fac = u"?"_s;
        for (const Node &c : r->kids[static_cast<size_t>(*ai)].kids)
            if (is(c, u"factor"_s)) {
                fac = c.value;
                break;
            }
        t_ai->setText(u"How much the AI wants it: base factor %1, times these:"_s.arg(fac));
        t_chance->setVisible(true);
        t_chance->set_block(text, {*ai}, Schema{nullptr, cond_schema.get()});
    } else {
        t_ai->setText(QString());
        t_chance->setVisible(false);
    }
    t_file->set_text(text);
}

void TechWorkspace::_fill_inv()
{
    const QStringList invs = sel ? _inv_of.value(*sel) : QStringList();
    i_head->setText(sel ? u"Inventions that come with %1 (%2)"_s.arg(name(sel), QString::number(invs.size())) : u"Inventions"_s);
    QListWidget *lst = i_list;
    lst->blockSignals(true);
    lst->clear();
    for (const QString &i : invs) {
        std::shared_ptr<const Node> keep;
        const Node *r = root_of(edits->text(i), keep);
        const Node *ch = nullptr, *eff = nullptr;
        for (const Node &c : r->kids) {
            if (!ch && is(c, u"chance"_s) && c.is_block)
                ch = &c;
            if (!eff && is(c, u"effect"_s) && c.is_block)
                eff = &c;
        }
        QString base = u"?"_s;
        if (ch)
            for (const Node &c : ch->kids)
                if (is(c, u"base"_s)) {
                    base = c.value;
                    break;
                }
        QStringList effs;
        if (eff)
            for (const Node &c : eff->kids)
                if (c.has_key && !c.key.isEmpty())
                    effs << i_effects->_name(c.key);
        auto *it = new QListWidgetItem(u"%1  ·  %2% base%3"_s.arg(
            name(i), base,
            !effs.isEmpty() ? u"  ·  "_s + effs.mid(0, 3).join(u", "_s) + (effs.size() > 3 ? u" …"_s : QString())
                            : QString()));
        it->setData(Qt::UserRole, i);
        it->setToolTip(i);
        lst->addItem(it);
    }
    if (!sel_inv || !invs.contains(*sel_inv))
        sel_inv = !invs.isEmpty() ? OptStr(invs[0]) : std::nullopt;
    lst->setCurrentRow(sel_inv && invs.contains(*sel_inv) ? static_cast<int>(invs.indexOf(*sel_inv)) : -1);
    lst->blockSignals(false);
    const int rows = std::max(1, std::min(8, lst->count()));
    lst->setFixedHeight(rows * (lst->count() ? lst->sizeHintForRow(0) : 24) + 8);
    i_section->setVisible(sel_inv.has_value());
    if (!sel_inv)
        return;
    const QString i = *sel_inv;
    const QString text = edits->text(i);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    const Schema sch{schema.get(), nullptr};
    i_title->setText(name(i));
    i_sub->setText(u"%1 · %2%3"_s.arg(i, file_name(edits->file_of(i)),
                                      edits->added.contains(i)  ? u"  · new, written on Apply"_s
                                      : edits->want.contains(i) ? u"  · changed, written on Apply"_s
                                                                : QString()));
    _inv_armed.reset();
    i_del->setText(u"✕ Delete this invention"_s);
    i_del->setStyleSheet(QString());
    if (!i_name->hasFocus())
        i_name->setText(name(i));
    if (!i_desc->hasFocus()) {
        localisation::LocEdits *loc =
            doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
        QString t;
        if (loc)
            t = loc->text(i + u"_desc"_s).value_or(QString());
        if (t.isEmpty())
            if (const QString *l = mod.loc.get(i + u"_desc"_s))
                t = *l;
        i_desc->setPlainText(t);
    }
    const QStringList techs = edits->keys();
    const QSet<QString> tset(techs.begin(), techs.end());
    const QStringList refs = techdata::tech_refs(text, tset);
    const bool simple = techdata::simple_limit(text, tset) && refs.size() == 1;
    i_needs->blockSignals(true);
    if (i_needs->count() != techs.size()) {
        i_needs->clear();
        for (const Folder &fo : _folders_cache)
            for (const Area &ar : fo.areas)
                for (const QString &t : ar.keys)
                    i_needs->addItem(u"%1 — %2  (%3)"_s.arg(fo.title, name(t), t), t);
        QStringList items;
        for (int x = 0; x < i_needs->count(); ++x)
            items << i_needs->itemText(x);
        auto *comp = new QCompleter(items, i_needs);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setFilterMode(Qt::MatchContains);
        i_needs->setCompleter(comp);
    }
    i_needs->setCurrentIndex(!refs.isEmpty() ? std::max(0, i_needs->findData(refs[0])) : -1);
    i_needs->setEnabled(simple);
    i_needs->blockSignals(false);
    i_need_note->setVisible(!simple);
    i_need_note->setText(u"Its limit asks for more than one technology — Edit as text to change it."_s);
    std::optional<qsizetype> chi, eff, news;
    for (size_t n = 0; n < r->kids.size(); ++n) {
        const Node &c = r->kids[n];
        if (!chi && is(c, u"chance"_s) && c.is_block)
            chi = static_cast<qsizetype>(n);
        if (!eff && is(c, u"effect"_s) && c.is_block)
            eff = static_cast<qsizetype>(n);
        if (!news && is(c, u"news"_s))
            news = static_cast<qsizetype>(n);
    }
    const Node *base = nullptr;
    if (chi)
        for (const Node &c : r->kids[static_cast<size_t>(*chi)].kids)
            if (is(c, u"base"_s)) {
                base = &c;
                break;
            }
    i_chance->blockSignals(true);
    if (!base)
        i_chance->setValue(0);
    else if (const auto fv = py::to_float(base->value); fv && std::isfinite(*fv))
        i_chance->setValue(py::trunc_int(*fv));
    i_chance->blockSignals(false);
    i_chance_note->setText(u"% a year, times the factors below"_s);
    i_chance_ed->setVisible(chi.has_value());
    if (chi)
        i_chance_ed->set_block(text, {*chi}, Schema{nullptr, cond_schema.get()});
    i_news->setChecked(!news || py::lower(r->kids[static_cast<size_t>(*news)].value) != u"no");
    if (eff)
        i_effects->set_block(text, {*eff}, {}, sch);
    else
        i_effects->set_block(u"x = { }"_s, {}, {}, sch);
    i_effects->setEnabled(eff.has_value());
    i_mods->set_block(text, {}, std::set<QString>(techdata::INV_BASE.begin(), techdata::INV_BASE.end()), sch);
    i_file->set_text(text);
}

// ------------------------------------------------------------------ search / drag
void TechWorkspace::_search_items(const OrderedMap<QString, Info> &info)
{
    // the search box's list: every technology and invention; picking one jumps to it
    if (_search_sig && *_search_sig == info.size())
        return;
    _search_sig = info.size();
    QStringList items;
    _search_map.clear();
    for (const auto &[k, d] : info) {
        const QString t = u"Technology: %1  (%2)"_s.arg(d.name, k);
        items << t;
        _search_map.insert_or_assign(t, std::make_pair(k, OptStr()));
    }
    for (const QString &i : edits->inventions()) {
        QStringList refs;
        for (const auto &[t, invs] : _inv_of)
            if (invs.contains(i))
                refs << t;
        if (!refs.isEmpty()) {
            const Info *d = info.get(refs[0]);
            const QString t = u"Invention: %1  (of %2)"_s.arg(name(i), d ? d->name : refs[0]);
            items << t;
            _search_map.insert_or_assign(t, std::make_pair(refs[0], OptStr(i)));
        }
    }
    std::sort(items.begin(), items.end());
    auto *comp = new QCompleter(items, search);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    comp->setMaxVisibleItems(18);
    connect(comp, qOverload<const QString &>(&QCompleter::activated), this, &TechWorkspace::_search_pick);
    search->setCompleter(comp);
}

void TechWorkspace::_search_pick(const QString &text)
{
    const auto it = _search_map.find(text);
    if (it == _search_map.end())
        return;
    const auto [tech, inv_] = it->second;
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    grid->flash = tech;
    select(tech);
    if (inv_) {
        sel_inv = inv_;
        _fill_inv();
    }
}

void TechWorkspace::_search_jump()
{
    // Enter in the search box: the first match
    const QString t = py::lower(py::strip(search->text()));
    if (t.isEmpty())
        return;
    for (const auto &[text, _hit] : _search_map)
        if (py::lower(text).contains(t)) {
            _search_pick(text);
            return;
        }
}

void TechWorkspace::_dropped(const QString &key, const QString &target)
{
    const Info *d = _info_cache.get(key), *e = _info_cache.get(target);
    if (!d || !e || d->col.a != e->col.a) {
        Q_EMIT message(u"Technologies stay in their column — change its Column to move it to another."_s, 8000);
        return;
    }
    if (d->year != e->year) {
        Q_EMIT message(u"Only technologies of the same year can swap places — the others are in the "
                       "order of their years (change the year to move one)."_s,
                       8000);
        return;
    }
    Q_EMIT dropRequested(d->col.a, key, target);
}

// ------------------------------------------------------------------ edits
void TechWorkspace::_set_text(const OptStr &key, const QString &text, const QString &what)
{
    if (key && !key->isEmpty() && edits && text != edits->text(*key))
        Q_EMIT edit(u"%1: %2"_s.arg(*key, what), *key, text);
}

void TechWorkspace::_name(const OptStr &key, const QString &text0)
{
    const QString text = py::strip(text0);
    if (!key || key->isEmpty() || text.isEmpty())
        return;
    localisation::LocEdits *loc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    QString cur;
    if (loc)
        cur = loc->text(*key).value_or(QString());
    if (cur.isEmpty())
        if (const QString *l = mod.loc.get(*key))
            cur = *l;
    if (text != cur)
        Q_EMIT nameRequested(*key, text, u"other"_s);
}

void TechWorkspace::_field(const QString &key, const OptStr &value)
{
    if (!sel || !edits)
        return;
    const QString k = *sel;
    const QString text = edits->text(k);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::optional<qsizetype> idx;
    for (size_t i = 0; i < r->kids.size(); ++i)
        if (is(r->kids[i], key)) {
            idx = static_cast<qsizetype>(i);
            break;
        }
    if (!value || value->isEmpty()) {
        if (idx)
            _set_text(k, techdata::remove_node(text, {*idx}), u"no %1"_s.arg(key));
        return;
    }
    if (idx) {
        const Node &c = r->kids[static_cast<size_t>(*idx)];
        if (c.is_block || c.value != *value)
            _set_text(k, techdata::set_value(text, {*idx}, *value), u"%1 → %2"_s.arg(key, *value));
    } else
        _set_text(k, techdata::add_line(text, {}, key, *value), u"%1 → %2"_s.arg(key, *value));
}

void TechWorkspace::_inv_row(int row)
{
    if (row < 0)
        return;
    sel_inv = i_list->item(row)->data(Qt::UserRole).toString();
    i_raw->hide();
    i_effects->pending.reset();
    _fill_inv();
}

void TechWorkspace::_inv_needs()
{
    const QVariant td = i_needs->currentData();
    if (!sel_inv || sel_inv->isEmpty() || !td.isValid() || td.toString().isEmpty())
        return;
    const QString i = *sel_inv, t = td.toString();
    const QString text = edits->text(i);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::optional<qsizetype> lim;
    for (size_t n = 0; n < r->kids.size(); ++n)
        if (is(r->kids[n], u"limit"_s) && r->kids[n].is_block) {
            lim = static_cast<qsizetype>(n);
            break;
        }
    if (!lim) {
        _set_text(i, techdata::add_line(text, {}, u"limit"_s, u"{ %1 = 1 }"_s.arg(t)), u"comes with %1"_s.arg(t));
        return;
    }
    const QStringList keys = edits->keys();
    const QSet<QString> techs(keys.begin(), keys.end());
    const Node &l = r->kids[static_cast<size_t>(*lim)];
    std::optional<qsizetype> idx;
    for (size_t n = 0; n < l.kids.size(); ++n)
        if (l.kids[n].has_key && techs.contains(l.kids[n].key)) {
            idx = static_cast<qsizetype>(n);
            break;
        }
    if (!idx)
        _set_text(i, techdata::add_line(text, {*lim}, t, u"1"_s), u"comes with %1"_s.arg(t));
    else if (l.kids[static_cast<size_t>(*idx)].key != t)
        _set_text(i, techdata::replace_key(text, {*lim, *idx}, t), u"comes with %1"_s.arg(t));
}

void TechWorkspace::_inv_chance(long long v)
{
    if (!sel_inv || sel_inv->isEmpty())
        return;
    const QString i = *sel_inv;
    const QString text = edits->text(i);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::optional<qsizetype> ch;
    for (size_t n = 0; n < r->kids.size(); ++n)
        if (is(r->kids[n], u"chance"_s) && r->kids[n].is_block) {
            ch = static_cast<qsizetype>(n);
            break;
        }
    if (!ch) {
        _set_text(i, techdata::add_line(text, {}, u"chance"_s, u"{ base = %1 }"_s.arg(v)), u"chance %1"_s.arg(v));
        return;
    }
    std::optional<qsizetype> base;
    const Node &c = r->kids[static_cast<size_t>(*ch)];
    for (size_t n = 0; n < c.kids.size(); ++n)
        if (is(c.kids[n], u"base"_s)) {
            base = static_cast<qsizetype>(n);
            break;
        }
    if (!base)
        _set_text(i, techdata::add_line(text, {*ch}, u"base"_s, QString::number(v)), u"chance %1"_s.arg(v));
    else
        _set_text(i, techdata::set_value(text, {*ch, *base}, QString::number(v)), u"chance %1"_s.arg(v));
}

void TechWorkspace::_inv_news(bool on)
{
    if (!sel_inv || sel_inv->isEmpty())
        return;
    const QString i = *sel_inv;
    const QString text = edits->text(i);
    std::shared_ptr<const Node> keep;
    const Node *r = root_of(text, keep);
    std::optional<qsizetype> n;
    for (size_t q = 0; q < r->kids.size(); ++q)
        if (is(r->kids[q], u"news"_s)) {
            n = static_cast<qsizetype>(q);
            break;
        }
    if (on && n)
        _set_text(i, techdata::remove_node(text, {*n}), u"in the newspaper"_s);
    else if (!on)
        _set_text(i, n ? techdata::set_value(text, {*n}, u"no"_s) : techdata::add_line(text, {}, u"news"_s, u"no"_s),
                  u"not in the newspaper"_s);
}

// ------------------------------------------------------------------ a new row
void TechWorkspace::_inv_new()
{
    // a new invention of the picked technology
    static const QRegularExpression nonkey(uR"([^a-z0-9]+)"_s);
    const QString nm = py::split(i_new_name->text()).join(u' ');
    const QString key = py::strip(py::lower(nm).replace(nonkey, u"_"_s), u"_"_s);
    techdata::TechEdits &ed = *edits;
    QString why;
    if (!sel || sel->isEmpty())
        why = u"Pick the technology it comes with first."_s;
    else if (key.isEmpty())
        why = u"Type the invention's name first."_s;
    else if (ed.exists(key) || ed.db.entries.contains(key))
        why = u"“%1” is the key of a technology or invention already — pick another name."_s.arg(key);
    if (!why.isEmpty()) {
        i_new_msg->setText(why);
        i_new_msg->show();
        return;
    }
    i_new_msg->hide();
    const QString k = *sel;
    const QStringList sib = _inv_of.value(k);
    const QStringList invs = ed.inventions();
    QString file;
    if (!sib.isEmpty())
        file = ed.file_of(sib.last());
    else if (!invs.isEmpty())
        file = ed.file_of(invs.last());
    else
        file = pypath::join(pypath::join(mod.root, u"inventions"_s), u"new_inventions.txt"_s);
    const QString text = QStringList{u"%1 = {"_s.arg(key), u"\tlimit = { %1 = 1 }"_s.arg(k), u"\tchance = {"_s,
                                     u"\t\tbase = 2"_s, u"\t}"_s, u"\teffect = {"_s, u"\t}"_s, u"}"_s}
                             .join(u"\r\n"_s);
    i_new_name->clear();
    sel_inv = key;
    Q_EMIT inventionRequested(key, file, text, nm);
}

void TechWorkspace::_inv_delete()
{
    if (!sel_inv || sel_inv->isEmpty())
        return;
    const QString i = *sel_inv;
    if (_inv_armed != OptStr(i)) {
        _inv_armed = i;
        i_del->setText(u"Click again: %1 goes"_s.arg(name(i)));
        i_del->setStyleSheet(u"QPushButton { color: #ff786e; font-weight: 700; border: 2px solid #ff786e; }"_s);
        return;
    }
    _inv_armed.reset();
    sel_inv.reset();
    Q_EMIT inventionRemoveRequested(i);
}

std::vector<NewTech> TechWorkspace::new_row_plan() const
{
    // one new technology at the bottom of every column — a later year and a higher cost than the one above it (same
    // steps as the last two), an AI chance of 1
    const techdata::TechEdits &ed = *edits;
    std::vector<NewTech> out;
    QSet<QString> taken;
    for (const QString &k : ed.keys())
        taken.insert(k);
    for (const auto &kv : ed.db.entries)
        taken.insert(kv.first);
    long long rows = -1;
    for (const Folder &fo : _folders_cache)
        for (const Area &ar : fo.areas)
            rows = std::max<long long>(rows, ar.keys.size());
    if (rows < 0)
        return out;
    const long long n = rows + 1;
    for (const Folder &fo : _folders_cache)
        for (const Area &ar : fo.areas) {
            const QStringList &ks = ar.keys;
            const OptStr last = !ks.isEmpty() ? OptStr(ks.last()) : std::nullopt;
            const OptStr prev = ks.size() > 1 ? OptStr(ks[ks.size() - 2]) : std::nullopt;
            const long long ly = last ? techdata::year_of(ed.text(*last)) : 1836;
            const long long py_ = prev ? techdata::year_of(ed.text(*prev)) : ly - 10;
            const long long year = ly + std::max<long long>(5, ly - py_);
            double lc = 3600.0, pc = 3000.0;
            {
                bool bad = false;
                if (last) {
                    const auto fl = techdata::fields(ed.text(*last));
                    const auto v = fl.contains(u"cost"_s) ? py::to_float(fl.value(u"cost"_s)) : std::optional<double>(3600.0);
                    if (v)
                        lc = *v;
                    else
                        bad = true;
                }
                if (!bad) {
                    if (prev) {
                        const auto fp = techdata::fields(ed.text(*prev));
                        const auto v = fp.contains(u"cost"_s) ? py::to_float(fp.value(u"cost"_s)) : std::optional<double>(lc);
                        if (v)
                            pc = *v;
                        else
                            bad = true;
                    } else
                        pc = lc * 0.8;
                }
                if (bad) {
                    lc = 3600.0;
                    pc = 3000.0;
                }
            }
            const long long cost = py::round_int(lc + std::max(lc - pc, lc * 0.2));
            QString key = u"%1_%2"_s.arg(ar.a, QString::number(n));
            while (taken.contains(key))
                key += u"_b"_s;
            taken.insert(key);
            QString file;
            if (last)
                file = ed.db.entries.contains(*last) ? ed.db.entries.get(*last)->file : ed.added.get(*last)->file;
            else
                file = ed.db.entries.get(ed.db.techs()[0])->file;
            const QString text =
                QStringList{u"%1 = {"_s.arg(key), u"\tarea = %1"_s.arg(ar.a), u"\tyear = %1"_s.arg(year),
                            u"\tcost = %1"_s.arg(cost), u"\t"_s, u"\tai_chance = {"_s, u"\t\tfactor = 1"_s, u"\t}"_s,
                            u"}"_s}
                    .join(u"\r\n"_s);
            out.push_back({key, file, text, u"%1 %2"_s.arg(ar.title, QString::number(n))});
        }
    return out;
}

}  // namespace techpanel
