// rail.py — line by line.
#include "ui/rail.h"
#include "ui/drawtext.h"
#include "ui/railicons.h"
#include "ui/theme.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace rail {

const std::vector<Group> RAIL = {
    {u"Map"_s,
     {{u"pops"_s, u"Population"_s, u"Pop"_s, u"pops"_s, std::nullopt, u"The pops of every province"_s},
      {u"provinces"_s, u"Provinces & states"_s, u"Prov"_s, u"provinces"_s, std::nullopt,
       u"What a province is at the start (owner, cores, RGO, buildings) and the states: their names, which "
       u"provinces they hold, the utility states"_s},
      {u"draw"_s, u"Drawing"_s, u"Draw"_s, u"map"_s, u"draw"_s, u"Draw provinces, terrain and rivers"_s},
      {u"pos"_s, u"Positions"_s, u"Pos"_s, u"map"_s, u"pos"_s, u"Where towns, units, ports and names sit"_s},
      {u"adj"_s, u"Adjacencies"_s, u"Adj"_s, u"map"_s, u"adj"_s,
       u"Sea crossings, impassable borders — and the Canals (its second sub-tab): every canal's model on the map, "
       u"how it opens, a new one by clicking its province"_s},
      {u"mapsize"_s, u"Map size"_s, u"Size"_s, u"mapsize"_s, std::nullopt,
       u"Cut columns off the left / right end of the map — provinces, terrain and rivers together"_s}}},
    {u"Nations"_s,
     {{u"nations"_s, u"Nation"_s, u"Nat"_s, u"nations"_s, std::nullopt,
       u"A nation at the start: government, parties, reforms, techs"_s},
      {u"armies"_s, u"Armies"_s, u"Army"_s, u"armies"_s, u"armies"_s,
       u"A nation's armies at the start — and its generals (the second sub-tab)"_s},
      {u"navies"_s, u"Navies"_s, u"Navy"_s, u"armies"_s, u"navies"_s,
       u"A nation's navies at the start — and its admirals (the second sub-tab)"_s},
      {u"diplomacy"_s, u"Diplomacy"_s, u"Dipl"_s, u"diplomacy"_s, std::nullopt,
       u"Relations, alliances, spheres, puppets"_s}}},
    {u"Rules"_s,
     {{u"politics"_s, u"Politics"_s, u"Pol"_s, u"politics"_s, std::nullopt,
       u"Reforms, unciv reforms, government forms, ideologies"_s},
      {u"technology"_s, u"Technology"_s, u"Tech"_s, u"technology"_s, std::nullopt,
       u"Technologies, inventions, tech schools"_s},
      {u"production"_s, u"Economy"_s, u"Econ"_s, u"production"_s, std::nullopt,
       u"Goods, factories, production chains, pop needs — and the output estimate: what every farm, mine and "
       u"factory turns out per day at the start"_s},
      {u"popinf"_s, u"Pop influences"_s, u"PopI"_s, u"popinf"_s, std::nullopt,
       u"What pushes the pops: promotion, demotion, migration, assimilation, conversion — every influence as a "
       u"card, a sample province, what it comes to after a time"_s},
      {u"warfare"_s, u"Warfare"_s, u"War"_s, u"warfare"_s, std::nullopt,
       u"The base stats of every unit type, leader traits, terrain types, the wars running at the start, unit "
       u"colours"_s},
      {u"rules"_s, u"Game rules"_s, u"Rule"_s, u"rules"_s, std::nullopt,
       u"Game settings, pop types, national values and focuses, rebels, standing and triggered modifiers, "
       u"crimes, start dates"_s},
      {u"events"_s, u"Scripts"_s, u"Scr"_s, u"events"_s, std::nullopt,
       u"Events, decisions, modifiers and war goals (the script editor)"_s},
      {u"shortcuts"_s, u"Shortcuts"_s, u"Keys"_s, u"shortcuts"_s, std::nullopt,
       u"The keyboard shortcuts of the game's own buttons: every button listed, set the key it gets"_s}}},
    {u"Tools"_s,
     {{u"errors"_s, u"Errors"_s, u"Err"_s, u"errors"_s, std::nullopt, u"Everything the checks found, with fixes"_s},
      {u"changelog"_s, u"Changelog"_s, u"Log"_s, u"changelog"_s, std::nullopt,
       u"What was applied, and the release notes"_s},
      {u"library"_s, u"Library"_s, u"Lib"_s, u"library"_s, std::nullopt,
       u"Your own pictures and music for the mod, the main menu and loading pictures, the look of the map "
       u"(land and water colours, terrain tiles), and the almanacs written from it"_s},
      {u"import"_s, u"Import"_s, u"Imp"_s, u"import"_s, std::nullopt,
       u"Take events, nations, units, national values … over from another mod — with everything they need"_s},
      {u"submods"_s, u"Submods & 4K"_s, u"Sub"_s, u"submods"_s, std::nullopt,
       u"What a submod is and where this one's changes go; make the 4K interface submod (everything twice as "
       u"big for a 3840 × 2160 screen)"_s},
      {u"checksum"_s, u"Checksum"_s, u"Sum"_s, u"checksum"_s, std::nullopt,
       u"The game's checksum of this mod: the four letters players need the same to play together"_s}}},
    {u"Search"_s,
     {{u"search"_s, u"Search"_s, u"Find"_s, u"search"_s, std::nullopt,
       u"Search every function, button and page by name — the same as Ctrl+K anywhere"_s}}},
};
const std::vector<Page> EXTRA = {
    {u"output"_s, u"Output estimate"_s, u"Out"_s, u"econest"_s, std::nullopt, u"Economy → Output estimate"_s},
    {u"canal"_s, u"Canals"_s, u"Canal"_s, u"map"_s, u"canal"_s, u"Adjacencies → Canals"_s},   // (the user, 2026-10-16)
    // (the user, 2026-11-18: sub-tabs of Armies / Navies, not rail entries of their own)
    {u"generals"_s, u"Generals"_s, u"Gen"_s, u"armies"_s, u"generals"_s,
     u"Armies → Generals: every general at the start of the picked nations or the world"_s},
    {u"admirals"_s, u"Admirals"_s, u"Adm"_s, u"armies"_s, u"admirals"_s,
     u"Navies → Admirals: every admiral at the start of the picked nations or the world"_s}};
const OrderedMap<QString, QString> ALIAS = [] {
    OrderedMap<QString, QString> m;
    m.insert(u"output"_s, u"production"_s);
    m.insert(u"canal"_s, u"adj"_s);
    m.insert(u"generals"_s, u"armies"_s);
    m.insert(u"admirals"_s, u"navies"_s);
    return m;
}();
const OrderedMap<QString, Page> PAGES = [] {
    OrderedMap<QString, Page> m;
    for (const Group &g : RAIL)
        for (const Page &p : g.pages)
            m.insert(p.key, p);
    for (const Page &p : EXTRA)
        m.insert(p.key, p);
    return m;
}();
const OrderedMap<QString, QString> PAGE_NAMES = [] {
    OrderedMap<QString, QString> m;
    for (const auto &[k, p] : PAGES)
        m.insert(k, p.name);
    return m;
}();
const int WIDE = 164, SLIM = 46;

std::optional<QString> page_of(const QString &tab, const std::optional<QString> &sub)
{
    for (const auto &[k, p] : PAGES)
        if (p.tab == tab && (!p.sub || p.sub == sub))
            return k;
    for (const auto &[k, p] : PAGES)
        if (p.tab == tab)
            return k;
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------- RailItem
RailItem::RailItem(const Page &page, QWidget *parent)
    : QAbstractButton(parent), key(page.key), name(page.name), code(page.code), tip(page.tip)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _tip();
}

QSize RailItem::sizeHint() const { return {slim ? SLIM : WIDE, slim ? 38 : 27}; }

void RailItem::set_counts(long long pending_, long long errors_)
{
    if (pending_ != pending || errors_ != errors) {
        pending = pending_;
        errors = errors_;
        _tip();
        update();
    }
}

void RailItem::_tip()
{
    QString t = u"%1 — %2"_s.arg(name, tip);
    if (pending)
        t += u"\n%1 pending edit%2 (Apply writes them)"_s.arg(pending).arg(pending == 1 ? u""_s : u"s"_s);
    if (errors)
        t += u"\n%1 error%2"_s.arg(errors).arg(errors == 1 ? u""_s : u"s"_s);
    setToolTip(t);
}

void RailItem::enterEvent(QEnterEvent *e)
{
    update();
    QAbstractButton::enterEvent(e);
}

void RailItem::leaveEvent(QEvent *e)
{
    update();
    QAbstractButton::leaveEvent(e);
}

void RailItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width(), h = height();
    const bool on = isChecked(), hover = underMouse();
    if (on) {
        p.fillRect(0, 0, w, h, QColor(theme::PANEL_ALT));
        p.fillRect(0, 0, 3, h, QColor(theme::GOLD));
    } else if (hover) {
        p.fillRect(0, 0, w, h, QColor(theme::PANEL));
    }
    const QColor ink(on ? theme::GOLD : errors ? theme::ERROR_ : hover ? theme::INK : theme::INK_DIM);
    QFont f(font());
    f.setPixelSize(!slim ? 13 : 12);
    f.setBold(on || errors);
    p.setFont(f);
    p.setPen(ink);
    if (slim) {                                       // folded: the page's icon (its name is the tooltip)
        if (!railicons::draw_icon(p, key, QRectF((w - 28) / 2.0 + 1, (h - 28) / 2.0, 28, 28), QColor(ink.name())))
            drawtext::draw(p, QRectF(0, 0, w, h), Qt::AlignCenter, code);
        if (pending) {                                // a gold dot top-right, a red one top-left
            p.setPen(QPen(QColor(theme::BG), 1.5));
            p.setBrush(QColor(theme::GOLD));
            p.drawEllipse(QRectF(w - 13, 3, 10, 10));
        }
        if (errors) {
            p.setPen(QPen(QColor(theme::BG), 1.5));
            p.setBrush(QColor(theme::ERROR_));
            p.drawEllipse(QRectF(5, 3, 10, 10));
        }
        return;
    }
    int right = w - 6;
    QFont bf(font());
    bf.setPixelSize(11);
    bf.setBold(true);
    const std::tuple<long long, QString, QString> parts[] = {{errors, theme::ERROR_, u"#ffffff"_s},
                                                            {pending, theme::GOLD, u"#14100a"_s}};
    for (const auto &[n, bg, fg] : parts) {
        if (!n)
            continue;
        p.setFont(bf);
        const QString t = n < 1000 ? QString::number(n) : u"%1k"_s.arg(n / 1000);   // (n > 0: // is /)
        const int bw = std::max(18, p.fontMetrics().horizontalAdvance(t) + 10);
        const QRectF r(right - bw, (h - 17) / 2.0, bw, 17);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(bg));
        p.drawRoundedRect(r, 8.5, 8.5);
        p.setPen(QColor(fg));
        drawtext::draw(p, r, Qt::AlignCenter, t);
        right -= bw + 3;
    }
    p.setFont(f);
    p.setPen(ink);
    const QRectF r(13, 0, std::max(10, right - 16), h);
    drawtext::draw(p, r, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(name, Qt::ElideRight, int(r.width())));
}

// ---------------------------------------------------------------------------------------------- FoldButton
FoldButton::FoldButton(QWidget *parent) : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize FoldButton::sizeHint() const { return {SLIM, 26}; }

void FoldButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (underMouse())
        p.fillRect(rect(), QColor(theme::PANEL));
    QFont f(font());
    f.setPixelSize(14);
    p.setFont(f);
    p.setPen(QColor(underMouse() ? theme::INK : theme::INK_DIM));
    drawtext::draw(p, rect(), Qt::AlignCenter, slim ? u"»"_s : u"«  fold"_s);
}

void FoldButton::enterEvent(QEnterEvent *e)
{
    update();
    QAbstractButton::enterEvent(e);
}

void FoldButton::leaveEvent(QEvent *e)
{
    update();
    QAbstractButton::leaveEvent(e);
}

// ---------------------------------------------------------------------------------------------- NavRail
NavRail::NavRail(bool slim_, QWidget *parent) : QWidget(parent)
{
    setObjectName(u"rail"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(u"QWidget#rail { background: %1; border-right: 1px solid %2; }"_s.arg(theme::BG, theme::LINE));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 4, 1, 0);
    lay->setSpacing(0);
    for (size_t gi = 0; gi < RAIL.size(); ++gi) {
        const Group &group = RAIL[gi];
        auto *line = new QFrame();
        line->setFixedHeight(1);
        line->setStyleSheet(u"background: %1;"_s.arg(theme::LINE));
        lay->addSpacing(gi ? 5 : 0);
        lay->addWidget(line);
        lines.push_back(line);
        line->setVisible(false);
        auto *head = new QLabel(group.name);
        head->setStyleSheet(u"color: %1; font-size: 11px; padding: 5px 0 2px 13px; background: transparent;"_s.arg(
            theme::INK_FAINT));
        lay->addWidget(head);
        heads.push_back(head);
        for (const Page &page : group.pages) {
            auto *it = new RailItem(page);
            const QString k = page.key;
            connect(it, &QAbstractButton::clicked, this, [this, k] { Q_EMIT picked(k); });
            lay->addWidget(it);
            items.insert(page.key, it);
        }
    }
    lay->addStretch(1);
    fold = new FoldButton();
    fold->setToolTip(u"Fold the list to a slim strip / open it again"_s);
    connect(fold, &QAbstractButton::clicked, this, [this] { set_slim(!slim); });
    lay->addWidget(fold);
    set_slim(slim_, true);
}

void NavRail::set_slim(bool slim_, bool quiet)
{
    slim = slim_;
    for (size_t i = 0; i < heads.size(); ++i) {
        heads[i]->setVisible(!slim);
        lines[i]->setVisible(slim && i > 0);
    }
    for (const auto &[_k, it] : items) {
        it->slim = slim;
        it->updateGeometry();
        it->update();
    }
    fold->slim = slim;
    fold->update();
    setFixedWidth(slim ? SLIM : WIDE);
    if (!quiet)
        Q_EMIT folded(slim);
}

void NavRail::set_current(const QString &key_)
{
    const QString key = ALIAS.value(key_, key_);
    for (const auto &[k, it] : items)
        if (it->isChecked() != (k == key)) {
            it->setChecked(k == key);
            it->update();
        }
}

void NavRail::set_counts(const OrderedMap<QString, long long> &pending, const OrderedMap<QString, long long> &errors)
{
    OrderedMap<QString, QString> extra;
    for (const auto &[k, a] : ALIAS)
        extra.insert(a, k);
    for (const auto &[k, it] : items) {
        const QString e = extra.value(k);
        it->set_counts(pending.value(k, 0) + (!e.isEmpty() ? pending.value(e, 0) : 0),
                       errors.value(k, 0) + (!e.isEmpty() ? errors.value(e, 0) : 0));
    }
}

}  // namespace rail
