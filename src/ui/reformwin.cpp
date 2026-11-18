// reformwin.py — the game's politics window with the reforms laid over it (see reformwin.h).
#include "ui/reformwin.h"
#include "map/gamefont.h"
#include "ui/drawtext.h"
#include "ui/locwin.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSizePolicy>
#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace reformwin {

const QString REL = u"interface/country_politics.gui"_s;

namespace {

std::optional<QString> _val(const script::Node &n, const QString &name)
{
    for (const script::Node &c : n.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name && !c.is_block)
            return c.value;
    return std::nullopt;
}

XY _xy(const script::Node &n, const QString &name)
{
    for (const script::Node &c : n.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name && c.is_block) {
            const std::optional<QString> x = _val(c, u"x"_s), y = _val(c, u"y"_s);
            const auto fx = x && !x->isEmpty() ? py::to_float(*x) : std::optional<double>(0.0);
            const auto fy = y && !y->isEmpty() ? py::to_float(*y) : std::optional<double>(0.0);
            if (!fx || !fy)
                return {0.0, 0.0};
            return {*fx, *fy};
        }
    return {0.0, 0.0};
}

}  // namespace

OrderedMap<QString, XY> slots(const QString &text, const QString &part)
{
    OrderedMap<QString, XY> out;
    script::Nodes tops;
    try {
        tops = script::parse(text);
    } catch (...) {
        return out;
    }
    std::function<const script::Node *(const script::Nodes &, const QString &)> find =
        [&](const script::Nodes &nodes, const QString &name) -> const script::Node * {
        for (const script::Node &n : nodes)
            if (n.is_block && n.has_key && !n.key.isEmpty()) {
                if (py::lower(n.key) == u"windowtype" && _val(n, u"name"_s) == name)
                    return &n;
                if (const script::Node *got = find(n.kids, name))
                    return got;
            }
        return nullptr;
    };
    const script::Node *root = find(tops, u"country_politics"_s);
    const script::Node *win = root ? find(root->kids, part) : nullptr;
    if (!win)
        return out;
    const auto [px, py_] = _xy(*win, u"position"_s);
    for (const script::Node &c : win->kids)
        if (c.is_block && c.has_key && !c.key.isEmpty() && py::lower(c.key) == u"windowtype") {
            const std::optional<QString> nm = _val(c, u"name"_s);
            if (nm && !nm->isEmpty()) {
                const auto [x, y] = _xy(c, u"position"_s);
                out.insert(*nm, {px + x, py_ + y});
            }
        }
    return out;
}

QRectF extent(const QString &text, const QString &part,
              const std::function<std::optional<QSizeF>(const QString &)> &sprite_size)
{
    QRectF out;
    script::Nodes tops;
    try {
        tops = script::parse(text);
    } catch (...) {
        return out;
    }
    std::function<const script::Node *(const script::Nodes &, const QString &)> find =
        [&](const script::Nodes &nodes, const QString &name) -> const script::Node * {
        for (const script::Node &n : nodes)
            if (n.is_block && n.has_key && !n.key.isEmpty()) {
                if (py::lower(n.key) == u"windowtype" && _val(n, u"name"_s) == name)
                    return &n;
                if (const script::Node *got = find(n.kids, name))
                    return got;
            }
        return nullptr;
    };
    const script::Node *root = find(tops, u"country_politics"_s);
    const script::Node *win = root ? find(root->kids, part) : nullptr;
    if (!win)
        return out;
    const auto [px, py_] = _xy(*win, u"position"_s);
    auto num = [](const script::Node &n, const QString &k) {
        const std::optional<QString> v = _val(n, k);
        const std::optional<double> f = v && !v->isEmpty() ? py::to_float(*v) : std::nullopt;
        return f ? *f : 0.0;
    };
    for (const script::Node &c : win->kids) {
        if (!c.is_block || !c.has_key || c.key.isEmpty())
            continue;
        const QString kind = py::lower(c.key);
        const auto [x, y] = _xy(c, u"position"_s);
        QSizeF sz;
        if (kind == u"windowtype") {                    // (a narrower place than vanilla's: its own width, C++ only)
            const double sw = _xy(c, u"size"_s).first;
            sz = QSizeF(sw > 0 ? std::min(ROW_W, sw) : ROW_W, ROW_H);
        }
        else if (kind == u"instanttextboxtype")
            sz = QSizeF(num(c, u"maxwidth"_s), num(c, u"maxheight"_s));
        else if (kind == u"icontype") {
            const std::optional<QString> sp = _val(c, u"spritetype"_s);
            const std::optional<QSizeF> got = sp && sprite_size ? sprite_size(*sp) : std::nullopt;
            if (got)
                sz = *got;
        } else
            continue;
        const QRectF r(px + x, py_ + y, std::max(1.0, sz.width()), std::max(1.0, sz.height()));
        out = out.isNull() ? r : out.united(r);
    }
    return out;
}

namespace {

const script::Node *find_window(const script::Nodes &nodes, const QString &name)
{
    for (const script::Node &n : nodes)
        if (n.is_block && n.has_key && !n.key.isEmpty()) {
            if (py::lower(n.key) == u"windowtype" && _val(n, u"name"_s) == name)
                return &n;
            if (const script::Node *got = find_window(n.kids, name))
                return got;
        }
    return nullptr;
}

const script::Node *piece(const script::Node &win, const QString &name)    // a direct piece of a window, by its name
{
    for (const script::Node &c : win.kids)
        if (c.is_block && c.has_key && _val(c, u"name"_s) == name)
            return &c;
    return nullptr;
}

std::optional<double> num_of(const script::Node &n, const QString &k)
{
    const std::optional<QString> v = _val(n, k);
    return v && !v->isEmpty() ? py::to_float(*v) : std::nullopt;
}

bool has_xy(const script::Node &n, const QString &k)
{
    for (const script::Node &c : n.kids)
        if (c.has_key && py::lower(c.key) == k && c.is_block)
            return true;
    return false;
}

}  // namespace

Layout layout(const QString &text)
{
    Layout l;
    script::Nodes tops;
    try {
        tops = script::parse(text);
    } catch (...) {
        return l;
    }
    const script::Node *w = find_window(tops, u"reform_option_window"_s);
    if (!w)
        return l;
    if (has_xy(*w, u"size"_s))
        if (const double h = _xy(*w, u"size"_s).second; h > 0)
            l.row_h = h;
    if (const script::Node *t = piece(*w, u"selected"_s); t && has_xy(*t, u"position"_s))
        std::tie(l.tick_x, l.tick_y) = _xy(*t, u"position"_s);
    if (const script::Node *b = piece(*w, u"reform_option"_s); b && has_xy(*b, u"position"_s))
        std::tie(l.bar_x, l.bar_y) = _xy(*b, u"position"_s);
    if (const script::Node *n = piece(*w, u"reform_name"_s)) {
        if (has_xy(*n, u"position"_s))
            std::tie(l.opt_x, l.opt_y) = _xy(*n, u"position"_s);
        if (const auto mw = num_of(*n, u"maxwidth"_s); mw && *mw > 0)
            l.opt_w = *mw;
        if (const auto mh = num_of(*n, u"maxheight"_s); mh && *mh > 0)
            l.opt_hmax = *mh;
        if (const auto f = _val(*n, u"font"_s); f && !f->isEmpty())
            l.opt_font = *f;
    }
    return l;
}

OrderedMap<QString, Place> places_of(const QString &text, const QString &part)
{
    OrderedMap<QString, Place> out;
    script::Nodes tops;
    try {
        tops = script::parse(text);
    } catch (...) {
        return out;
    }
    const script::Node *root = find_window(tops, u"country_politics"_s);
    const script::Node *win = root ? find_window(root->kids, part) : nullptr;
    if (!win)
        return out;
    for (const script::Node &c : win->kids)
        if (c.is_block && c.has_key && py::lower(c.key) == u"windowtype") {
            const std::optional<QString> nm = _val(c, u"name"_s);
            if (!nm || nm->isEmpty())
                continue;
            Place p;
            if (has_xy(c, u"size"_s))
                if (const double sw = _xy(c, u"size"_s).first; sw > 0)
                    p.w = std::min(ROW_W, sw);
            if (const script::Node *n = piece(c, u"reform_name"_s)) {
                if (has_xy(*n, u"position"_s))
                    std::tie(p.name_x, p.name_y) = _xy(*n, u"position"_s);
                if (const auto mw = num_of(*n, u"maxwidth"_s); mw && *mw > 0)
                    p.name_w = *mw;
                if (const auto f = _val(*n, u"font"_s); f && !f->isEmpty())
                    p.name_font = *f;
            }
            out.insert(*nm, p);
        }
    return out;
}

GameReformView::GameReformView(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setMinimumHeight(360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GameReformView::set_data(const std::optional<QImage> &img_, XY offset_, const OrderedMap<QString, XY> &places,
                              const OrderedMap<QString, QString> &issues_, const OrderedMap<QString, QString> &names_,
                              const OrderedMap<QString, QString> &current_,
                              const std::optional<std::pair<QString, QString>> &sel_,
                              const OrderedMap<QString, std::vector<Option>> &options_, const QRectF &part,
                              locwin::GameLook *look_)
{
    img = img_;
    look = look_;
    offset = offset_;
    issues = issues_;
    names = names_;
    current = current_;
    sel = sel_;
    options = options_;
    boxes.clear();
    for (const auto &[nm, xy] : places) {
        const std::vector<Option> *o = options.get(nm);
        const double h = std::max(ROW_H, lay.tick_y + 4 + lay.row_h * double(o ? o->size() : 0));
        const Place *pl = place_info.get(nm);
        const double w = pl ? pl->w : ROW_W;
        boxes.push_back({QRectF(xy.first - offset.first, xy.second - offset.second, w, h), nm, issues.contains(nm)});
    }
    missing.clear();
    for (const auto &[k, _g] : issues)
        if (!places.contains(k))
            missing << k;
    std::sort(missing.begin(), missing.end());
    // what is shown: the part's own pieces and its reforms (C++ only) - never the tabs or the left side
    crop = part.isNull() ? QRectF() : part.translated(-offset.first, -offset.second);
    for (const Box &b : boxes)
        crop = crop.isNull() ? b.r : crop.united(b.r);
    if (img && !img->isNull())
        crop = crop.isNull() ? QRectF(img->rect()) : crop.adjusted(-6, -6, 6, 6).intersected(QRectF(img->rect()));
    update();
}

std::tuple<double, double, double> GameReformView::_scale() const
{
    if (!img || img->isNull())
        return {1.0, 0.0, 0.0};
    const QRectF c = crop.isNull() ? QRectF(img->rect()) : crop;
    const double room = std::max(1.0, height() - _note_h());   // (the red note's lines below)
    double s = std::min(width() / c.width(), room / c.height());
    s = std::max(0.2, std::min(s, 1.6));
    return {s, (width() - c.width() * s) / 2.0 - c.left() * s, -c.top() * s};
}

void GameReformView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(theme::BG));
    if (!img || img->isNull()) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect(), Qt::AlignCenter, u"The game's politics window (%1) was not found."_s.arg(REL));
        return;
    }
    const auto [s, ox, oy] = _scale();
    p.translate(ox, oy);
    p.scale(s, s);
    p.save();
    p.setClipRect(crop.isNull() ? QRectF(img->rect()) : crop);
    p.drawImage(QPointF(0, 0), *img);
    p.restore();
    // the game's pieces (C++ only): the option button (tan bar) and the tick + frame of the option it has
    const std::optional<QImage> bar = look ? look->sprite(u"GFX_politics_reform_select"_s) : std::nullopt;
    const std::optional<QImage> tick = look ? look->sprite(u"GFX_politics_reform_selected"_s) : std::nullopt;
    QFont f(u"Georgia"_s);                               // (stand-ins when the game's fonts can't be read)
    f.setPixelSize(15);
    f.setBold(true);
    QFont smallf(u"Georgia"_s);
    smallf.setPixelSize(12);
    auto game_text = [&](const QString &font, const QString &text, double x, double y, double maxw, double maxh,
                         const QFont &stand_in, const QColor &ink) {
        if (look) {
            try {
                gamefont::draw(p, look->fonts, font, text, x, y, maxw, maxh, u"left"_s, maxh > 0);
                return;
            } catch (...) {
            }
        }
        p.setFont(stand_in);
        p.setPen(ink);
        drawtext::draw(p, QRectF(x, y, maxw, maxh > 0 ? maxh : 40), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, text);
    };
    for (const Box &b : boxes) {
        const QRectF &r = b.r;
        const QString &nm = b.name;
        if (!b.mine) {                                   // an empty place of the game's window (no reform of that key)
            QPen pen(QColor(theme::WARN), 2.0);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(0, 2, -6, -2), 5, 5);
            continue;
        }
        const bool on = sel && nm == sel->first;
        QString under = nm;
        under.replace(u'_', u' ');
        const QString name = names.value(nm, under);
        const Place *pl = place_info.get(nm);
        const Place place = pl ? *pl : Place{};
        // the reform's name where the game writes it (two lines lifted half a line: my reading of the game's picture,
        // not measured)
        double ny = r.top() + place.name_y;
        if (look)
            try {
                const auto nl = gamefont::layout(look->fonts, place.name_font, name, place.name_w);
                if (nl && nl->lines.size() > 1)
                    ny -= double(nl->font->line) * double(nl->lines.size() - 1) / 2.0;
            } catch (...) {
            }
        game_text(place.name_font, name, r.left() + place.name_x, ny, place.name_w, 0, f, QColor(240, 236, 226));
        const std::vector<Option> *opts = options.get(nm);
        // the option it has: the one picked here, else the one most nations start with
        QString has;
        if (on && !sel->second.isEmpty())
            has = sel->second;
        else if (opts)
            for (const auto &[oname, ticked, okey] : *opts)
                if (ticked)
                    has = okey;
        for (size_t k = 0; opts && k < opts->size(); ++k) {
            const auto &[oname, ticked, okey] = (*opts)[k];
            const double ry = r.top() + lay.row_h * double(k);
            const std::pair<QString, QString> me{nm, okey};
            if (_hover && *_hover == me && okey != has) {   // under the mouse: the game's option button
                if (bar)
                    p.drawImage(QPointF(r.left() + lay.bar_x, ry + lay.bar_y), *bar);
                else
                    p.fillRect(QRectF(r.left() + lay.bar_x, ry + lay.bar_y, 139, 13), QColor(196, 180, 128));
            }
            if (okey == has) {
                if (tick)
                    p.drawImage(QPointF(r.left() + lay.tick_x, ry + lay.tick_y), *tick);
                else {
                    p.setPen(QPen(QColor(40, 120, 40), 2.2));
                    const double cy = ry + lay.tick_y + 7;
                    p.drawLine(QPointF(r.left() + lay.tick_x + 2, cy), QPointF(r.left() + lay.tick_x + 5, cy + 3));
                    p.drawLine(QPointF(r.left() + lay.tick_x + 5, cy + 3), QPointF(r.left() + lay.tick_x + 11, cy - 4));
                }
            }
            game_text(lay.opt_font, oname, r.left() + lay.opt_x, ry + lay.opt_y, lay.opt_w, lay.opt_hmax, smallf,
                      QColor(30, 26, 18));
        }
        if (on) {                                        // the reform picked (the editor's mark)
            p.setPen(QPen(QColor(theme::GOLD), 2.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-2, 0, -8, 0), 5, 5);
        }
    }
    p.resetTransform();
    if (!missing.isEmpty()) {                            // (C++ only: as many lines as the list needs, 2026-11-16)
        p.setFont(QFont(font()));
        p.setPen(QColor(theme::ERROR_));
        const double h = _note_h();
        drawtext::draw(p, QRectF(8, height() - h + 4, width() - 16, h - 4), Qt::AlignLeft | Qt::TextWordWrap,
                       _note_text());
    }
}

QString GameReformView::_note_text() const
{
    QStringList shown;
    for (const QString &k : missing)
        shown << names.value(k, k);
    return u"Reforms with no place in the game's window — the game does not show them (measured): %1"_s.arg(
        shown.join(u", "_s));
}

double GameReformView::_note_h() const
{
    if (missing.isEmpty())
        return 0.0;
    const QRect r = QFontMetrics(font()).boundingRect(QRect(0, 0, std::max(40, width() - 16), 100000),
                                                      Qt::AlignLeft | Qt::TextWordWrap, _note_text());
    return double(r.height()) + 8.0;
}

QRectF GameReformView::_opt_row(const QRectF &r, long long k) const
{
    // where the k-th option of the reform framed by r stands (reform_option_window's row: from the tick to the end of
    // the option button)
    const double x0 = std::min(lay.tick_x, lay.bar_x), x1 = std::max(lay.bar_x + 139, lay.opt_x + lay.opt_w);
    return QRectF(r.left() + x0, r.top() + lay.tick_y + double(k) * lay.row_h, x1 - x0, lay.row_h);
}

GameReformView::At GameReformView::_at(QPointF pos) const
{
    const auto [s, ox, oy] = _scale();
    const QPointF pt((pos.x() - ox) / s, (pos.y() - oy) / s);
    for (const Box &b : boxes)
        if (b.r.contains(pt)) {
            if (b.mine)
                if (const std::vector<Option> *o = options.get(b.name))
                    for (size_t k = 0; k < o->size(); ++k)
                        if (_opt_row(b.r, static_cast<long long>(k)).adjusted(-2, 0, 2, 0).contains(pt))
                            return {b.name, std::get<2>((*o)[k]), b.mine};
            return {b.name, QString(), b.mine};
        }
    return {QString(), QString(), false};
}

void GameReformView::leaveEvent(QEvent *)
{
    if (_hover) {
        _hover.reset();
        update();
    }
}

void GameReformView::mouseMoveEvent(QMouseEvent *e)
{
    const At a = _at(e->position());
    std::optional<std::pair<QString, QString>> hov;
    if (!a.name.isEmpty())
        hov = std::pair{a.name, a.option};
    if (hov != _hover) {
        _hover = hov;
        setCursor(a.mine ? Qt::PointingHandCursor : Qt::ArrowCursor);
        QString on;                                     // ({okey: name}.get(opt): the last option of that key)
        if (a.mine)
            if (const std::vector<Option> *o = options.get(a.name))
                for (const auto &[oname, _t, okey] : *o)
                    if (okey == a.option)
                        on = oname;
        if (!on.isEmpty())
            setToolTip(u"%1 — click: its effects on the right, to change"_s.arg(on));
        else if (a.mine)
            setToolTip(u"%1 — click one of its options for that option's effects"_s.arg(names.value(a.name, a.name)));
        else if (!a.name.isEmpty())
            setToolTip(u"A place of the game's window named “%1”: the mod has no reform of that key, so it stays "
                       u"empty in the game"_s.arg(a.name));
        else
            setToolTip(QString());
        update();
    }
}

void GameReformView::mousePressEvent(QMouseEvent *e)
{
    const At a = _at(e->position());
    if (!a.name.isEmpty() && a.mine && e->button() == Qt::LeftButton) {
        sel = std::pair{a.name, a.option};
        update();
        Q_EMIT picked(a.name, a.option);
    }
}

}  // namespace reformwin
