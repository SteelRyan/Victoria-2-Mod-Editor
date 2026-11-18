// modepanel.py — line by line, see modepanel.h.
#include "ui/modepanel.h"
#include "ui/drawtext.h"
#include "ui/railicons.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace modepanel {

static QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

Rgb page_colour(const QString &page)
{
    // the colour of each category (its page) — the same wherever the category shows
    static const QHash<QString, Rgb> PAGE_COLOURS = {
        {u"pops"_s, {201, 161, 90}},       {u"provinces"_s, {122, 160, 112}}, {u"states"_s, {122, 160, 112}},
        {u"draw"_s, {150, 156, 168}},      {u"pos"_s, {150, 156, 168}},       {u"adj"_s, {150, 156, 168}},
        {u"canal"_s, {150, 156, 168}},     {u"nations"_s, {93, 143, 196}},    {u"armies"_s, {190, 104, 80}},
        {u"navies"_s, {190, 104, 80}},     {u"diplomacy"_s, {165, 130, 205}}, {u"events"_s, {80, 172, 160}},
        {u"errors"_s, {214, 84, 72}}};
    return PAGE_COLOURS.value(page, {140, 150, 170});
}

static QStandardItem *item_of(const QComboBox *combo, int i)
{
    auto *m = qobject_cast<QStandardItemModel *>(combo->model());
    return m ? m->item(i) : nullptr;
}

std::vector<Group> groups_of(const QComboBox *combo)
{
    // the headings of the combo's list and the modes under each
    std::vector<Group> out;
    for (int i = 0; i < combo->count(); ++i) {
        const QStandardItem *it = item_of(combo, i);
        const QVariant page = it ? it->data(widgets::HEAD_ROLE) : QVariant();
        if (page.isValid() && page.toBool()) {
            out.push_back({combo->itemText(i), page.typeId() == QMetaType::QString ? page.toString() : QString(), {}});
        } else if (combo->itemData(i).isValid()) {
            if (out.empty())
                out.push_back({QString(), QString(), {}});
            out.back().items.emplace_back(i, combo->itemData(i).toString(), combo->itemText(i));
        }
    }
    return out;
}

QString short_title(const QString &title)
{
    // 'This page — Population' -> 'Population', 'From Provinces & states' -> 'Provinces & states'
    QString t = title;
    for (const QString &pre : {u"This page — "_s, u"From "_s})
        if (t.startsWith(pre))
            t = t.mid(pre.size());
    return t;
}

void paint_swatch(QPainter &p, const QRectF &r, const std::optional<Swatch> &sw)
{
    if (!sw || sw->empty())
        return;
    p.save();
    p.setPen(QPen(QColor(10, 10, 12), 1.5));
    if (sw->grad) {
        QLinearGradient g(r.left(), 0, r.right(), 0);
        for (const auto &[pos, rgb] : sw->stops)
            g.setColorAt(pos, qc(rgb));
        p.setBrush(g);
        p.drawRoundedRect(r, 2.5, 2.5);
    } else {
        std::vector<Rgb> cols(sw->colours.begin(), sw->colours.begin() + std::min<size_t>(6, sw->colours.size()));
        const double w = r.width() / double(cols.size());
        for (size_t k = 0; k < cols.size(); ++k) {
            p.setPen(Qt::NoPen);
            p.setBrush(qc(cols[k]));
            p.drawRect(QRectF(r.left() + double(k) * w, r.top(), w + 0.5, r.height()));
        }
        p.setPen(QPen(QColor(10, 10, 12), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 2.5, 2.5);
    }
    p.restore();
}

void draw_mark(QPainter &p, double cx, double cy, double r, const QString &colour)
{
    // the mark of a map mode with colours of its own: a circle, its left half filled (the ◐ of the button)
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(colour), std::max(1.6, r * 0.28)));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), r, r);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(colour));
    p.drawPie(QRectF(cx - r, cy - r, 2 * r, 2 * r), 90 * 16, 180 * 16);
    p.restore();
}

QPixmap mark_pixmap(int size, const QString &colour)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    draw_mark(p, size / 2.0, size / 2.0, size / 2.0 - 2, colour.isEmpty() ? theme::GOLD : colour);
    p.end();
    return pm;
}

// ---------------------------------------------------------------------------------------------- ModeCombo
ModeCombo::ModeCombo(QWidget *parent) : QComboBox(parent)
{
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(30);
}

QString ModeCombo::category(int i) const
{
    i = i < 0 ? currentIndex() : i;
    for (int j = i; j >= 0; --j) {
        const QStandardItem *it = item_of(this, j);
        if (it && it->data(widgets::HEAD_ROLE).toBool())
            return short_title(itemText(j));
    }
    return QString();
}

void ModeCombo::showPopup()
{
    if (!isEnabled())
        return;
    panel = new ModePanel(this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    connect(panel, &QObject::destroyed, this, [this](QObject *o) {
        if (panel == o)
            panel = nullptr;
    });
    panel->place();
    panel->show();
    panel->filter->setFocus();
}

void ModeCombo::hidePopup()
{
    if (panel)
        panel->close();
}

void ModeCombo::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    const QString cat = category();
    if (!cat.isEmpty() && !opt.currentText.isEmpty())
        opt.currentText = u"%1  ·  %2"_s.arg(cat, opt.currentText);
    const bool own = !opt.currentText.isEmpty() && custom && custom(currentData().toString()).has_value();
    if (own)
        opt.currentText += u"      "_s;               // (room for the mark)
    p.drawComplexControl(QStyle::CC_ComboBox, opt);
    p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    if (own) {                                     // its own colours: the same mark as in the panel
        const QRect r = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        QString trimmed = opt.currentText;
        while (!trimmed.isEmpty() && trimmed.back().isSpace())
            trimmed.chop(1);
        const int x = std::min(r.left() + fontMetrics().horizontalAdvance(trimmed) + 14, r.right() - 8);
        draw_mark(p, x, r.center().y() + 0.5, 6.5, theme::GOLD);
    }
}

// ---------------------------------------------------------------------------------------------- the panel
_Head::_Head(const QString &title_, const QString &page_, QWidget *parent) : QWidget(parent), title(title_), page(page_)
{
    setFixedHeight(HEAD_H);
}

void _Head::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    p.fillRect(r, qc(page_colour(page)));
    const QColor dark(16, 18, 22);
    const bool has = railicons::draw_icon(p, page, QRectF(QRect(8, 5, 22, 22)), QColor(dark.name()));
    QFont f(font());
    f.setBold(true);
    f.setPointSizeF(std::max(10.0, f.pointSizeF() + 0.5));
    p.setFont(f);
    p.setPen(dark);
    drawtext::draw(p, r.adjusted(has ? 36 : 10, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(title, Qt::ElideRight, r.width() - 46));
}

_Row::_Row(int row_, const QString &key_, const QString &text_, std::optional<Swatch> sw_, bool current_,
           const Rgb &colour_, std::optional<QString> custom_, QWidget *parent)
    : QWidget(parent), row(row_), key(key_), text(text_), sw(std::move(sw_)), current(current_), colour(colour_),
      custom(std::move(custom_))
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setFixedHeight(ROW_H);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    QString tip = text;
    // the two state modes said apart (C++ only, the user 2026-11-13: "some overlap between split states view and split
    // ownership for states map view")
    if (key == u"mixedowners"_s)
        tip += u"\nStates owned by more than one nation, each province in its owner's colour (one owner: grey). The game "
               u"splits such a state per owner. Where the provinces lie does not matter here — that is Split states."_s;
    else if (key == u"splitstates"_s)
        tip += u"\nStates whose provinces are not connected on the map: orange = the part cut off, brown = the biggest "
               u"part. A crossing without a sea tile (a Meme river), a land link or a crossing through a marked "
               u"Meme-river tile joins the parts; a strait through a real sea tile does not. Who owns the provinces "
               u"does not matter here — that is Mixed owners."_s;
    if (custom == u"ranges"_s)
        tip += u"\n◐ Its own colours: ranges (set with the ◐ button by the map modes)"_s;
    else if (custom == u"own"_s)
        tip += u"\n◐ Its own colours: a scale of its own (set with the ◐ button by the map modes)"_s;
    setToolTip(tip);
}

void _Row::enterEvent(QEnterEvent *)
{
    hot = true;
    update();
}

void _Row::leaveEvent(QEvent *)
{
    hot = false;
    update();
}

void _Row::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
        Q_EMIT picked(row);
}

void _Row::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    if (current)
        p.fillRect(r, QColor(theme::GOLD_DIM));
    else if (hot)
        p.fillRect(r, QColor(48, 54, 64));
    if (current)
        p.fillRect(QRect(0, 0, 4, r.height()), qc(colour));
    paint_swatch(p, QRectF(12, (r.height() - 14) / 2.0, 26, 14), sw);
    QFont f(font());
    f.setPointSizeF(std::max(10.0, f.pointSizeF() + 0.5));
    f.setBold(current);
    p.setFont(f);
    int right = 6;
    if (custom) {                                  // its own colours (◐): the user, 2026-10-15 — "they need an indication"
        draw_mark(p, r.width() - 17, r.height() / 2.0, 7.5, !current ? theme::GOLD : u"#101216"_s);
        right = 32;
    }
    p.setPen(QColor(theme::INK));
    drawtext::draw(p, r.adjusted(48, 0, -right, 0), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(text, Qt::ElideRight, r.width() - 48 - right));
}

ModePanel::ModePanel(ModeCombo *combo_) : QFrame(combo_, Qt::Popup), combo(combo_)
{
    setObjectName(u"modePanel"_s);
    setStyleSheet(u"QFrame#modePanel { background: %1; border: 2px solid %2; } "
                  u"QFrame#modeCard { background: %3; border: 1px solid %4; }"_s.arg(theme::PANEL, theme::GOLD_DIM,
                                                                                     theme::PANEL_ALT, theme::LINE));
    groups = groups_of(combo);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 12);
    lay->setSpacing(8);
    auto *top = new QHBoxLayout();
    filter = new QLineEdit();
    filter->setPlaceholderText(u"Type to filter the map modes   (Enter picks the first · Esc closes)"_s);
    filter->setClearButtonEnabled(true);
    connect(filter, &QLineEdit::textChanged, this, &ModePanel::_filter);
    connect(filter, &QLineEdit::returnPressed, this, &ModePanel::_pick_first);
    top->addWidget(filter, 1);
    lay->addLayout(top);
    auto *krow = new QHBoxLayout();
    krow->setSpacing(8);
    auto *icon = new QLabel();
    icon->setPixmap(mark_pixmap(18));
    icon->setFixedSize(20, 20);
    krow->addWidget(icon, 0, Qt::AlignTop);
    auto *key = new QLabel(u"A map mode with colours of its own — ranges, or a scale of its own (the ◐ button by the "
                           u"map modes sets them)"_s);
    key->setStyleSheet(u"color: %1;"_s.arg(theme::INK_DIM));
    key->setWordWrap(true);
    krow->addWidget(key, 1);
    lay->addLayout(krow);
    none = new QLabel(u"No map mode has that in its name."_s);
    none->setStyleSheet(u"color: %1;"_s.arg(theme::INK_FAINT));
    none->hide();
    lay->addWidget(none);
    scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // never sideways (the user's rule)
    scroll->setFrameShape(QFrame::NoFrame);
    lay->addWidget(scroll, 1);
}

void ModePanel::place()
{
    // columns for the screen the box is on, the panel under the box (or above it), inside the screen
    const QRect scr = combo->screen()->availableGeometry();
    const int n = std::max(1, std::min({static_cast<int>(groups.size()), (scr.width() - 60) / COLW, 5}));
    ncols = n;
    _build(n);
    QWidget *inner = scroll->widget();
    inner->adjustSize();
    const int want_h = inner->sizeHint().height() + 70;
    const int w = n * COLW + (n - 1) * 10 + 24 + scroll->verticalScrollBar()->sizeHint().width();
    const QPoint below = combo->mapToGlobal(QPoint(0, combo->height() + 2));
    const int room_below = scr.bottom() - below.y() - 8;
    const int room_above = combo->mapToGlobal(QPoint(0, 0)).y() - scr.top() - 8;
    const int h = std::min(want_h, std::max({room_below, room_above, 300}));
    const int y = (room_below >= h || room_below >= room_above) ? below.y()
                                                                 : combo->mapToGlobal(QPoint(0, 0)).y() - h - 2;
    const int x = std::max(scr.left() + 4, std::min(below.x() - 40, scr.right() - w - 4));
    setGeometry(x, std::max(scr.top() + 4, y), w, h);
}

void ModePanel::_build(int n)
{
    auto *inner = new QWidget();
    auto *hl = new QHBoxLayout(inner);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);
    std::vector<QVBoxLayout *> cols;
    std::vector<int> heights;
    for (int i = 0; i < n; ++i) {
        auto *v = new QVBoxLayout();
        v->setSpacing(10);
        hl->addLayout(v, 1);
        cols.push_back(v);
        heights.push_back(0);
    }
    const int cur = combo->currentIndex();
    for (const Group &g : groups) {
        if (g.items.empty())
            continue;
        auto *card = new QFrame();
        card->setObjectName(u"modeCard"_s);
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(0, 0, 0, 4);
        cl->setSpacing(0);
        cl->addWidget(new _Head(short_title(g.title) + (g.title.startsWith(u"This page"_s) ? u"   (this page)"_s
                                                                                           : QString()), g.page));
        std::vector<_Row *> rws;
        for (const auto &[row, key, text] : g.items) {
            auto *rw = new _Row(row, key, text, combo->swatch ? combo->swatch(key) : std::nullopt, row == cur,
                                page_colour(g.page), combo->custom ? combo->custom(key) : std::nullopt);
            connect(rw, &_Row::picked, this, &ModePanel::_pick);
            cl->addWidget(rw);
            rws.push_back(rw);
            rows.push_back(rw);
        }
        const auto k = size_t(std::min_element(heights.begin(), heights.end()) - heights.begin());
        cols[k]->addWidget(card);                  // the shortest column takes the next card
        heights[k] += HEAD_H + ROW_H * static_cast<int>(g.items.size()) + 14;
        cards.emplace_back(card, rws);
    }
    for (auto *v : cols)
        v->addStretch(1);
    scroll->setWidget(inner);
}

void ModePanel::_filter(const QString &text)
{
    const QString t = text.trimmed().toLower();
    int shown = 0;
    for (auto &[card, rws] : cards) {
        const QString title = card->findChild<_Head *>()->title.toLower();
        int vis = 0;
        for (_Row *rw : rws) {
            const bool ok = t.isEmpty() || rw->text.toLower().contains(t) || title.contains(t);
            rw->setVisible(ok);
            vis += ok;
        }
        card->setVisible(vis > 0);
        shown += vis;
    }
    none->setVisible(shown == 0);
}

void ModePanel::_pick_first()
{
    for (auto &[card, rws] : cards)
        if (card->isVisible())
            for (_Row *rw : rws)
                if (rw->isVisible()) {
                    _pick(rw->row);
                    return;
                }
}

void ModePanel::_pick(int row)
{
    close();
    if (row != combo->currentIndex())
        combo->setCurrentIndex(row);
    Q_EMIT combo->activated(row);
    combo->update();
}

}  // namespace modepanel
