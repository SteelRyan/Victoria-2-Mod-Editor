// iconboard.py (see iconboard.h).
#include "ui/iconboard.h"
#include "ui/drawtext.h"
#include "core/blocks.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/imgio.h"
#include "ui/iconstrips.h"
#include "ui/locwin.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace iconboard {

const int CELL_W = 96, CELL_H = 114, HEAD_H = 36, GAP = 8;
const OrderedMap<QString, Kind> KINDS = {
    {u"focus"_s, {u"common/national_focus.txt"_s, u"GFX_province_natfocus_strip"_s, true, u"national focus"_s,
                  u"National focuses"_s,
                  u"The national focuses as the game shows them when a state's focus is picked: a row per group. Click a "
                  u"picture to change it, drag it to another place, ＋ adds one."_s}},
    {u"nv"_s, {u"common/nationalvalues.txt"_s, u"GFX_politics_nationalvalue"_s, false, u"national value"_s,
               u"National values"_s,
               u"The national values with their pictures. The game takes a national value's picture by its place in the "
               u"list (the first shows picture 1 …), so a value dragged to another place takes its picture along. Click "
               u"one to change what it gives — also what the pop files give for it."_s}},
};
const OrderedMap<QString, QString> PLACE = {
    {u"promote_to"_s, u"becoming %s"_s}, {u"ideologies"_s, u"leaning towards %s"_s}, {u"issues"_s, u"wanting %s"_s},
    {u"migration_target"_s, u"moving inside the country: how much a province draws them"_s},
    {u"country_migration_target"_s, u"emigrating: how much a nation draws them"_s},
    {u"promotion_chance"_s, u"being promoted"_s}, {u"demotion_chance"_s, u"being demoted"_s},
    {u"migration_chance"_s, u"moving inside the country"_s}, {u"colonialmigration_chance"_s, u"moving to the colonies"_s},
    {u"emigration_chance"_s, u"emigrating"_s},
    {u"assimilation_chance"_s, u"taking on the nation's culture (assimilation)"_s},
    {u"conversion_chance"_s, u"taking on the nation's religion (conversion)"_s}, {u"rebel"_s, u"rebelling"_s}};

static bool keyed(const Node &n) { return n.has_key && !n.key.isEmpty(); }   // (Python: `n.key` is truthy)
static bool is_key(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static const QRegularExpression WORD_RE(uR"(^[A-Za-z0-9_]+$)"_s);
static const QRegularExpression NUM_RE(uR"(^-?\d+(\.\d+)?$)"_s, QRegularExpression::UseUnicodePropertiesOption);

QString _pretty(const QString &k) { return QString(k).replace(u'_', u' '); }

Span _line_span(const QString &text, const Node &n)
{
    qsizetype a = py::rfind(text, u'\n', n.kstart) + 1;
    if (!py::strip(py::slice(text, a, n.kstart)).isEmpty())
        a = n.kstart;
    qsizetype b = text.indexOf(u'\n', n.vend);
    b = b < 0 ? text.size() : b + 1;
    const QString after = py::strip(py::slice(text, n.vend, b));
    if (!after.isEmpty() && !after.startsWith(u'#'))
        b = n.vend;
    return {a, b};
}

std::vector<Group> read_entries(const QString &text, bool grouped)
{
    std::vector<Node> nodes;
    for (Node &n : script::parse(text))
        if (keyed(n) && n.is_block)
            nodes.push_back(std::move(n));
    std::vector<Group> out;
    if (!grouped) {
        out.push_back({QString(), std::nullopt, std::move(nodes)});
        return out;
    }
    for (Node &g : nodes) {
        std::vector<Node> es;
        for (const Node &c : g.kids)
            if (keyed(c) && c.is_block)
                es.push_back(c);
        out.push_back({g.key, g, std::move(es)});
    }
    return out;
}

QString move_entry(const QString &text, bool grouped, const QString &key, const QString &group, const QString &before)
{
    // The entry `key` moved in front of the entry `before` of the group (before "": to the group's end).
    const auto rows = read_entries(text, grouped);
    const Node *src = nullptr;
    for (const Group &g : rows) {
        for (const Node &n : g.entries)
            if (n.key == key) {
                src = &n;
                break;
            }
        if (src)
            break;
    }
    if (!src || before == key)
        return text;
    const auto [a, b] = _line_span(text, *src);
    QString seg = py::slice(text, a, b);
    if (!seg.endsWith(u'\n'))
        seg += u'\n';
    const Group *gr = &rows[0];
    for (const Group &g : rows)
        if (g.key == group) {
            gr = &g;
            break;
        }
    const Node *dst = nullptr;
    if (!before.isEmpty())
        for (const Node &n : gr->entries)
            if (n.key == before) {
                dst = &n;
                break;
            }
    qsizetype at;
    if (dst) {
        at = _line_span(text, *dst).first;
    } else if (gr->node) {
        const qsizetype close = gr->node->vend - 1;
        at = py::rfind(text, u'\n', close) + 1;
        if (!py::strip(py::slice(text, at, close)).isEmpty())
            at = close;
    } else {
        at = text.size();
    }
    if (a <= at && at < b)
        return text;
    const QString rest = py::head(text, a) + py::tail(text, b);
    at = at >= b ? at - (b - a) : at;
    if (at > 0) {
        const QString prev = py::slice(rest, at - 1, at);
        if (prev != u"\n" && !prev.isEmpty())
            seg = u'\n' + seg;
    }
    return py::head(rest, at) + seg + py::tail(rest, at);
}

std::vector<NvUse> nv_uses(scriptstore::ScriptStore &store, const QStringList &files, const QString &nv)
{
    // Where the pop files ask for a national value.
    std::vector<NvUse> out;
    for (const QString &rel : files) {
        QString text;
        script::Nodes nodes;
        try {
            text = store.text(rel);
            nodes = script::parse(text);
        } catch (const std::exception &) {
            continue;
        }
        std::function<OptStr(const script::Nodes &, bool)> mentions = [&](const script::Nodes &ns, bool neg) -> OptStr {
            for (const Node &c : ns) {
                if (is_key(c, u"nationalvalue"_s) && !c.is_block && py::strip(c.value) == nv)
                    return neg ? u"not"_s : u"yes"_s;
                if (c.is_block)
                    if (const OptStr got = mentions(c.kids, neg ^ is_key(c, u"NOT"_s)))
                        return got;
            }
            return std::nullopt;
        };
        std::function<void(const script::Nodes &, const QStringList &)> walk = [&](const script::Nodes &ns,
                                                                                   const QStringList &path) {
            for (const Node &c : ns) {
                if (!c.is_block)
                    continue;
                if (is_key(c, u"modifier"_s)) {
                    const OptStr how = mentions(c.kids, false);
                    if (how) {
                        const Node *f = nullptr;
                        for (const Node &x : c.kids)
                            if (is_key(x, u"factor"_s) && !x.is_block) {
                                f = &x;
                                break;
                            }
                        QStringList also;
                        for (const Node &x : c.kids)
                            if (!is_key(x, u"factor"_s) &&
                                !(is_key(x, u"nationalvalue"_s) && py::strip(x.is_block ? QString() : x.value) == nv))
                                also << py::split(py::slice(text, x.kstart, x.vend)).join(u' ');
                        NvUse u;
                        u.rel = rel;
                        u.path = path;
                        u.factor = f ? py::strip(f->value) : QString();
                        if (f)
                            u.fspan = Span{f->vstart, f->vend};
                        u.span = _line_span(text, c);
                        u.also = also.join(u", "_s);
                        u.neg = *how == u"not";
                        out.push_back(std::move(u));
                    }
                } else {
                    walk(c.kids, path + QStringList{c.key});
                }
            }
        };
        walk(nodes, {});
    }
    return out;
}

std::vector<std::pair<QString, QStringList>> pop_places(scriptstore::ScriptStore &store, const QStringList &files)
{
    // Every place in the pop files a modifier can be added to: the blocks that hold modifiers or a factor already.
    std::vector<std::pair<QString, QStringList>> out;
    static const QStringList SKIP = {u"modifier"_s, u"group"_s, u"AND"_s, u"OR"_s, u"NOT"_s};
    static const QStringList HOLD = {u"modifier"_s, u"factor"_s, u"group"_s};
    for (const QString &rel : files) {
        script::Nodes nodes;
        try {
            nodes = script::parse(store.text(rel));
        } catch (const std::exception &) {
            continue;
        }
        std::function<void(const script::Nodes &, const QStringList &)> walk = [&](const script::Nodes &ns,
                                                                                   const QStringList &path) {
            for (const Node &c : ns)
                if (c.is_block && !(c.has_key && SKIP.contains(c.key))) {
                    if (std::any_of(c.kids.begin(), c.kids.end(),
                                    [](const Node &x) { return x.has_key && HOLD.contains(x.key); }))
                        out.emplace_back(rel, path + QStringList{c.key});
                    else
                        walk(c.kids, path + QStringList{c.key});
                }
        };
        walk(nodes, {});
    }
    return out;
}

QString place_words(const QString &rel, const QStringList &path, const std::function<QString(const QString &)> &name)
{
    const QString stem = pypath::splitext_stem(pypath::basename(rel));
    const QString who = stem == u"pop_types" ? u"Every pop"_s : py::capitalize(name(stem));
    const QString head = !path.isEmpty() ? path[0] : QString();
    const QString *pw = PLACE.get(head);
    QString w = pw ? *pw : _pretty(head);
    const qsizetype at = w.indexOf(u"%s"_s);
    if (at >= 0)
        w = w.left(at) + (path.size() > 1 ? name(path[1]) : u"…"_s) + w.mid(at + 2);
    return u"%1 — %2"_s.arg(who, w);
}

// ---------------------------------------------------------------------------------------------- the board
Board::Board(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void Board::set_rows(const std::vector<BRow> &rows_, const OptStr &sel_, const QSet<QString> &multi_)
{
    rows = rows_;
    sel = sel_;
    multi = multi_;
    _layout();
    update();
}

QRectF Board::_x_of(const QRectF &r) const { return QRectF(r.right() - 34, r.top() + 3, 28, r.height() - 6); }

void Board::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        Q_EMIT deleteRequested();
    else
        QWidget::keyPressEvent(e);
}

void Board::_layout()
{
    const int w = std::max(CELL_W + 2 * GAP, width());
    const int per = std::max(1, (w - GAP) / (CELL_W + GAP));
    int y = GAP;
    _boxes.clear();
    _heads.clear();
    _plus.clear();
    for (const BRow &row : rows) {
        if (row.title) {
            _heads.emplace_back(QRectF(GAP, y, w - 2 * GAP, HEAD_H - 4), row.key, *row.title);
            y += HEAD_H;
        }
        const int extra = plus ? 1 : 0;
        const int n = static_cast<int>(row.cells.size());
        for (int i = 0; i < n + extra; ++i) {
            const QRectF r(GAP + (i % per) * (CELL_W + GAP), y + (i / per) * (CELL_H + GAP), CELL_W, CELL_H);
            if (i >= n)
                _plus.emplace_back(r, row.key);
            else
                _boxes.emplace_back(r, row.key, row.cells[size_t(i)]);
        }
        y += ((n + extra + per - 1) / per) * (CELL_H + GAP) + 6;
    }
    setMinimumHeight(y + GAP);
}

void Board::resizeEvent(QResizeEvent *e)
{
    _layout();
    QWidget::resizeEvent(e);
}

QSize Board::sizeHint() const { return QSize(600, minimumHeight()); }

void Board::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont f(font());
    f.setPixelSize(13);
    QFont fb(f);
    fb.setBold(true);
    for (const auto &[r, gk, title] : _heads) {
        const bool on = (sel && *sel == u'@' + gk) || multi.contains(u'@' + gk);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(theme::GOLD) : QColor(u"#2a2f37"_s));
        p.drawRoundedRect(r, 4, 4);
        p.setFont(fb);
        p.setPen(on ? QColor(u"#14100a"_s) : QColor(theme::GOLD));
        drawtext::draw(p, r.adjusted(10, 0, -46, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   (multi.contains(u'@' + gk) ? u"✔ "_s : QString()) + title);
        if (head_x) {
            const QRectF x = _x_of(r);
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(QColor(u"#3a2424"_s));
            p.drawRoundedRect(x, 4, 4);
            p.setPen(QColor(u"#ff786e"_s));
            drawtext::draw(p, x, Qt::AlignCenter, u"✕"_s);
        }
    }
    QFont fs(f);
    fs.setPixelSize(11);
    for (const auto &[r, gk, cell] : _boxes) {
        const bool on = (sel && cell.key == *sel) || multi.contains(cell.key);
        const bool ghost = _drag && _drag->key == cell.key && _drag->moving;
        p.setPen(QPen(on ? QColor(theme::GOLD) : QColor(10, 10, 10), on ? 3 : 2));
        p.setBrush(!ghost ? QColor(u"#1b1f26"_s) : QColor(u"#15181d"_s));
        p.drawRoundedRect(r, 6, 6);
        const QRectF box(r.left() + 8, r.top() + 6, r.width() - 16, 56);
        if (cell.pm && !cell.pm->isNull()) {
            const QPixmap &pm = *cell.pm;
            const double k = std::min({box.width() / pm.width(), box.height() / pm.height(), 2.0});
            const double pw = pm.width() * k, ph = pm.height() * k;
            p.setOpacity(ghost ? 0.35 : 1.0);
            p.drawPixmap(QRectF(box.center().x() - pw / 2, box.center().y() - ph / 2, pw, ph), pm,
                         QRectF(0, 0, pm.width(), pm.height()));
            p.setOpacity(1.0);
        } else {
            p.setPen(QPen(QColor(theme::WARN), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(box.center().x() - 20, box.center().y() - 20, 40, 40), 5, 5);
            p.setFont(fb);
            drawtext::draw(p, box, Qt::AlignCenter, u"?"_s);
        }
        p.setFont(fs);
        p.setPen(!ghost ? QColor(theme::INK) : QColor(theme::INK_FAINT));
        drawtext::draw(p, QRectF(r.left() + 3, r.top() + 64, r.width() - 6, 28), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   cell.name);
        if (!cell.note.isEmpty()) {
            p.setPen(QColor(theme::INK_FAINT));
            drawtext::draw(p, QRectF(r.left() + 3, r.bottom() - 19, r.width() - 6, 14), Qt::AlignCenter, cell.note);
        }
        if (multi.contains(cell.key)) {                // one of several picked: a tick in its corner
            const QRectF tb(r.left() + 5, r.top() + 5, 20, 20);
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(QColor(theme::GOLD));
            p.drawRoundedRect(tb, 4, 4);
            p.setPen(QPen(QColor(12, 12, 12), 3));
            p.drawLine(QPointF(tb.left() + 4, tb.center().y()), QPointF(tb.center().x() - 1, tb.bottom() - 5));
            p.drawLine(QPointF(tb.center().x() - 1, tb.bottom() - 5), QPointF(tb.right() - 4, tb.top() + 5));
        }
    }
    p.setFont(fb);
    for (const auto &[r, _gk] : _plus) {
        p.setPen(QPen(QColor(theme::GOLD_DIM), 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(14, 14, -14, -22), 6, 6);
        p.setPen(QColor(theme::GOLD));
        QFont f2(fb);
        f2.setPixelSize(26);
        p.setFont(f2);
        drawtext::draw(p, r.adjusted(14, 14, -14, -22), Qt::AlignCenter, u"＋"_s);
        p.setFont(fs);
        drawtext::draw(p, QRectF(r.left(), r.bottom() - 20, r.width(), 16), Qt::AlignCenter, u"new"_s);
    }
    if (_drop) {                                       // where the dragged one would go: a gold bar
        p.setPen(QPen(QColor(10, 10, 10), 7));
        p.drawLine(QPointF(_drop->x, _drop->y0), QPointF(_drop->x, _drop->y1));
        p.setPen(QPen(QColor(theme::GOLD), 4));
        p.drawLine(QPointF(_drop->x, _drop->y0), QPointF(_drop->x, _drop->y1));
    }
}

std::optional<Board::Hit> Board::_at(const QPointF &pos) const
{
    for (const auto &[r, gk, cell] : _boxes)
        if (r.contains(pos))
            return Hit{u"cell"_s, gk, cell.key, r};
    for (const auto &[r, gk] : _plus)
        if (r.contains(pos))
            return Hit{u"plus"_s, gk, QString(), r};
    for (const auto &[r, gk, _t] : _heads)
        if (r.contains(pos))
            return Hit{u"head"_s, gk, QString(), r};
    return std::nullopt;
}

void Board::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {             // a right click on a picture deletes it (Ctrl+Z brings it back)
        const auto rh = _at(e->position());
        if (rh && rh->what == u"cell")
            Q_EMIT rightDelete(rh->key);
        return;
    }
    const auto hit = e->button() == Qt::LeftButton ? _at(e->position()) : std::nullopt;
    setFocus();
    if (!hit)
        return;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (hit->what == u"cell") {
        if (ctrl) {
            Q_EMIT ctrlPicked(hit->key);
            return;
        }
        _drag = Drag{hit->key, e->position(), false};
        Q_EMIT picked(hit->key);
    } else if (hit->what == u"plus") {
        Q_EMIT addRequested(hit->gk);
    } else if (head_x && _x_of(hit->r).contains(e->position())) {
        Q_EMIT headDelete(hit->gk);
    } else if (ctrl) {
        Q_EMIT ctrlPicked(u'@' + hit->gk);
    } else {
        Q_EMIT picked(u'@' + hit->gk);
    }
}

void Board::mouseMoveEvent(QMouseEvent *e)
{
    if (!_drag) {
        setCursor(_at(e->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    if (!_drag->moving && (e->position() - _drag->start).manhattanLength() < 8)
        return;
    _drag->moving = true;
    setCursor(Qt::ClosedHandCursor);
    const auto hit = _at(e->position());
    _drop.reset();
    if (hit && (hit->what == u"cell" || hit->what == u"plus") && hit->key != _drag->key)
        _drop = Drop{hit->gk, hit->key, hit->r.left() - GAP / 2.0, hit->r.top(), hit->r.bottom()};
    update();
}

void Board::mouseReleaseEvent(QMouseEvent *)
{
    const auto drag = _drag;
    const auto drop = _drop;
    _drag.reset();
    _drop.reset();
    unsetCursor();
    update();
    if (drag && drag->moving && drop)
        Q_EMIT moved(drag->key, drop->gk, drop->before);
}

// ---------------------------------------------------------------------------------------------- the game's window
WindowPreview::WindowPreview(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void WindowPreview::set(const std::optional<QImage> &img_, const std::vector<focuswindow::Hit> &hits_)
{
    img = img_;
    hits = hits_;
    _fit();
    update();
}

void WindowPreview::_fit()
{
    if (!img) {
        setMinimumHeight(40);
        return;
    }
    scale = std::min(1.0, std::max(0.2, (width() - 2) / double(img->width())));
    setMinimumHeight(int(img->height() * scale) + 2);
}

void WindowPreview::resizeEvent(QResizeEvent *e)
{
    _fit();
    QWidget::resizeEvent(e);
}

QSize WindowPreview::sizeHint() const { return QSize(img ? img->width() : 440, minimumHeight()); }

void WindowPreview::paintEvent(QPaintEvent *)
{
    if (!img)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(0, 0, img->width() * scale, img->height() * scale), *img);
}

OptStr WindowPreview::at(const QPointF &pos) const
{
    const double x = pos.x() / scale - focuswindow::PAD, y = pos.y() / scale - focuswindow::PAD;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)   // (the later icon lies over the earlier one)
        if (!it->key.startsWith(u'@') && it->x <= x && x < it->x + it->w && it->y <= y && y < it->y + it->h)
            return it->key;
    for (const focuswindow::Hit &h : hits)
        if (h.key.startsWith(u'@') && h.x <= x && x < h.x + h.w && h.y <= y && y < h.y + h.h)
            return h.key;
    return std::nullopt;
}

void WindowPreview::mouseMoveEvent(QMouseEvent *e) { setCursor(at(e->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor); }

void WindowPreview::mousePressEvent(QMouseEvent *e)
{
    const OptStr key = e->button() == Qt::LeftButton ? at(e->position()) : std::nullopt;
    if (key && !key->isEmpty())
        Q_EMIT picked(*key);
}

// ---------------------------------------------------------------------------------------------- the page
namespace {
class LookOf : public focuswindow::Look {              // (Python: locwin.GameLook passed as the look)
public:
    LookOf(const moddata::Mod &mod, scriptstore::ScriptStore *store) : g(mod, store) { fonts = &g.fonts; }
    std::optional<QImage> sprite(const QString &name) override { return g.sprite(name); }
    locwin::GameLook g;
};

QScrollArea *board_scroll()
{
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setStyleSheet(u"QScrollArea { background: %1; border: 1px solid %2; }"_s.arg(theme::BG, theme::LINE));
    return sc;
}

QString plural(long long n, const QString &one, const QString &many) { return n == 1 ? one : many; }
}  // namespace

BoardPage::BoardPage(moddata::Mod &mod_, const QString &kind_, StripChange strip_change_, QWidget *parent)
    : QWidget(parent), mod(mod_), kind(kind_), K(*KINDS.get(kind_)), strip_change(std::move(strip_change_))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 12, 8, 12);
    ll->setSpacing(6);
    ll->addWidget(label(K.title, u"title"_s));
    ll->addWidget(label(K.intro, u"faint"_s));
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::WARN));
    warn->hide();
    ll->addWidget(warn);
    QScrollArea *sc = board_scroll();
    board = new Board();
    board->head_x = K.grouped;
    connect(board, &Board::picked, this, [this](const QString &k) { _pick(k); });
    connect(board, &Board::moved, this, [this](const QString &k, const QString &g, const QString &b) { _move(k, g, b); });
    connect(board, &Board::addRequested, this, [this](const QString &g) { _add(g); });
    connect(board, &Board::ctrlPicked, this, [this](const QString &k) { _ctrl(k); });
    connect(board, &Board::deleteRequested, this, [this] { _delete_picked(); });
    connect(board, &Board::headDelete, this, [this](const QString &g) { _del_group(g); });
    connect(board, &Board::rightDelete, this, [this](const QString &k) {
        QTimer::singleShot(0, this, [this, k] { _delete(k); });   // (after the click: the board is drawn again)
    });
    auto *host = new QWidget();
    host->setObjectName(u"sideInner"_s);
    auto *hl = new QVBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(board);
    hl->addStretch(1);
    sc->setWidget(host);
    ll->addWidget(sc, 1);
    if (K.grouped) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *b = new QPushButton(u"＋ New row"_s);
        b->setToolTip(u"A new, empty group of national focuses at the end"_s);
        connect(b, &QPushButton::clicked, this, [this] { _add_group(); });
        row->addWidget(b);
        win_btn = new QPushButton(u"Fit the game's window"_s);
        win_btn->setToolTip(u"Draws the game's focus window again for the rows and the longest row there are: its "
                            u"picture (a band a row, wider when a row holds more than fit), the rows' distance and the "
                            u"buttons' places. A pending change (Ctrl+Z undoes it)."_s);
        connect(win_btn, &QPushButton::clicked, this, [this] { _fit_window(); });
        win_btn->hide();
        row->addWidget(win_btn);
        row->addStretch(1);
        ll->addLayout(row);
        win_note = new QLabel(QString());
        win_note->setObjectName(u"sub"_s);
        win_note->setWordWrap(true);
        win_note->setTextFormat(Qt::RichText);
        ll->addWidget(win_note);
    }
    ll->addWidget(label(u"Right-click a picture: it is deleted. Ctrl + click picks several (%1) — the Delete key or the "
                        u"button on the right takes them out at once; Ctrl+Z brings them back."_s
                            .arg(K.grouped ? u"focuses, rows by their bar; ✕ on a bar: that row goes with its focuses"_s
                                           : u"national values"_s),
                        u"faint"_s));
    split->addWidget(left);
    if (K.grouped) {                                   // the game's own window, drawn: what all this looks like there
        auto *mid = new QWidget();
        auto *ml = new QVBoxLayout(mid);
        ml->setContentsMargins(8, 12, 8, 12);
        ml->setSpacing(6);
        ml->addWidget(label(u"In the game"_s, u"title"_s));
        auto *row = new QHBoxLayout();
        row->setSpacing(0);
        const std::tuple<QString, QString, QString> segs[2] = {
            {u"now"_s, u"As it is now"_s, u"The window with the files as they are (pending changes count)"_s},
            {u"fit"_s, u"After “Fit the game's window”"_s, u"What the fit would make of it — before you press the button"_s}};
        for (const auto &[k, text, tip] : segs) {
            auto *b = new QPushButton(text);
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            b->setToolTip(tip);
            connect(b, &QPushButton::clicked, this, [this, kk = k] { _set_pv(kk); });
            row->addWidget(b);
            pv_btn.emplace_back(k, b);
        }
        row->addStretch(1);
        ml->addLayout(row);
        pv_note = new QLabel(QString());
        pv_note->setObjectName(u"sub"_s);
        pv_note->setWordWrap(true);
        pv_note->setTextFormat(Qt::RichText);
        ml->addWidget(pv_note);
        QScrollArea *sc3 = board_scroll();
        pv = new WindowPreview();
        connect(pv, &WindowPreview::picked, this, [this](const QString &k) { _pick(k); });
        auto *host3 = new QWidget();
        host3->setObjectName(u"sideInner"_s);
        auto *h3 = new QVBoxLayout(host3);
        h3->setContentsMargins(0, 0, 0, 0);
        h3->addWidget(pv);
        h3->addStretch(1);
        sc3->setWidget(host3);
        ml->addWidget(sc3, 1);
        ml->addWidget(label(u"Drawn from the game's own files: the window's picture, where its rows and buttons stand, "
                            u"the icon strip, the game's fonts. Click an icon: that focus is picked. Not compared with "
                            u"the running game yet."_s,
                            u"faint"_s));
        split->addWidget(mid);
    }
    auto *sc2 = new QScrollArea();
    sc2->setWidgetResizable(true);
    sc2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc2->setObjectName(u"sideScroll"_s);
    detail = new QWidget();
    detail->setObjectName(u"sideInner"_s);
    dl = new QVBoxLayout(detail);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(0);
    sc2->setWidget(detail);
    split->addWidget(sc2);
    if (pv) {
        split->setStretchFactor(0, 6);
        split->setStretchFactor(1, 0);
        split->setStretchFactor(2, 4);
        split->setSizes({640, 480, 540});
    } else {
        split->setStretchFactor(0, 6);
        split->setStretchFactor(1, 5);
        split->setSizes({900, 760});
    }
}

BoardPage::~BoardPage() = default;

// ------------------------------------------------------------------ data
QString BoardPage::text() { return store->text(K.rel); }

std::vector<Group> BoardPage::rows() { return read_entries(text(), K.grouped); }

OptStr BoardPage::loc(const QString &key) const
{
    localisation::LocEdits *lc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr t;
    if (lc)
        t = lc->text(key);
    else if (const QString *x = mod.loc.get(key))
        t = *x;
    return t && !t->isEmpty() ? t : std::nullopt;
}

QString BoardPage::name(const QString &key) const { return loc(key).value_or(_pretty(key)); }

std::optional<std::tuple<QString, long long, Node>> BoardPage::node(const QString &key)
{
    for (Group &g : rows())
        for (size_t i = 0; i < g.entries.size(); ++i)
            if (g.entries[i].key == key)
                return std::make_tuple(g.key, static_cast<long long>(i), std::move(g.entries[i]));
    return std::nullopt;
}

OptInt BoardPage::icon_of(const QString &, long long i, const Node &n) const
{
    // The number of an entry's picture in the strip (a national value: its place in the list).
    if (kind == u"nv")
        return i + 1;
    for (const Node &c : n.kids)
        if (is_key(c, u"icon"_s) && !c.is_block)
            return py::isdigit(py::strip(c.value)) ? OptInt(py::to_int(c.value)) : std::nullopt;
    return std::nullopt;
}

static bool truthy(const OptInt &n) { return n && *n != 0; }

QStringList BoardPage::pop_files()
{
    QStringList out = entryview::_files(mod, store, u"poptypes"_s);
    if (fs::isfile(mod.p({u"common"_s, u"pop_types.txt"_s})))
        out << u"common/pop_types.txt"_s;
    return out;
}

void BoardPage::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    strips = &iconstrips::strips_for(mod, *doc);
    _show();
}

void BoardPage::_show()
{
    const std::vector<QPixmap> frames = iconstrips::frames(*strips, K.sprite);
    std::vector<BRow> brows;
    QStringList missing;
    for (const Group &g : rows()) {
        std::vector<Cell> cells;
        for (size_t i = 0; i < g.entries.size(); ++i) {
            const Node &n = g.entries[i];
            const OptInt num = icon_of(g.key, static_cast<long long>(i), n);
            std::optional<QPixmap> pm;
            if (truthy(num) && 1 <= *num && *num <= static_cast<long long>(frames.size()))
                pm = frames[size_t(*num - 1)];
            if (!pm)
                missing << n.key;
            cells.push_back({n.key, name(n.key), pm, u"picture %1"_s.arg(truthy(num) ? QString::number(*num) : u"—"_s)});
        }
        brows.push_back({g.key, !g.key.isEmpty() ? OptStr(name(g.key)) : std::nullopt, std::move(cells)});
    }
    if (sel && !sel->startsWith(u'@') && !node(*sel))
        sel.reset();
    QSet<QString> have;
    for (const BRow &r : brows) {
        for (const Cell &c : r.cells)
            have.insert(c.key);
        if (!r.key.isEmpty())
            have.insert(u'@' + r.key);
    }
    multi &= have;
    board->set_rows(brows, sel, multi);
    if (!missing.isEmpty())
        warn->setText(u"⚠ %1 of them point past the strip's last picture (%2): %3. Give them a picture on the right, or "
                      u"add pictures in Library → Icon strips."_s
                          .arg(QString::number(missing.size()), QString::number(frames.size()),
                               missing.mid(0, 8).join(u", "_s) + (missing.size() > 8 ? u" …"_s : QString())));
    warn->setVisible(!missing.isEmpty());
    if (K.grouped) {
        _window_note();
        _preview();
    }
    _fill_detail();
}

// ------------------------------------------------------------------ the picked one, on the right
void BoardPage::_clear()
{
    while (dl->count()) {
        QLayoutItem *it = dl->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
}

QLabel *BoardPage::_bold(const QString &t)
{
    QLabel *h = label(t, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

void BoardPage::_fill_detail()
{
    _clear();
    name_edit = add_val = key_edit = h_factor = nullptr;
    lib = add_key = h_place = nullptr;
    lib_msg = msg = h_msg = nullptr;
    size_lbl = nullptr;
    import_btn = lib_one_btn = lib_all_btn = nullptr;
    del_btn = row_del = multi_del = nullptr;
    auto *s = new Section();
    if (multi.size() >= 2) {
        _fill_multi(s);
        dl->addWidget(s);
        dl->addStretch(1);
        return;
    }
    if (!sel || sel->isEmpty()) {
        s->lay->addWidget(label(u"Click a picture on the board."_s, u"sub"_s));
        dl->addWidget(s);
        dl->addStretch(1);
        return;
    }
    if (sel->startsWith(u'@')) {
        _fill_group(s, sel->mid(1));
        dl->addWidget(s);
        dl->addStretch(1);
        return;
    }
    const auto got = node(*sel);
    const QString gk = std::get<0>(*got);
    const long long i = std::get<1>(*got);
    const Node n = std::get<2>(*got);
    const QString key = n.key;
    const OptInt num = icon_of(gk, i, n);
    const QStringList path = !gk.isEmpty() ? QStringList{gk, key} : QStringList{key};
    auto *head = new QHBoxLayout();
    const std::vector<QPixmap> frames = iconstrips::frames(*strips, K.sprite);
    auto *pic = new QLabel();
    pic->setFixedSize(84, 84);
    pic->setAlignment(Qt::AlignCenter);
    pic->setStyleSheet(u"QLabel { background: #1b1f26; border: 2px solid %1; border-radius: 6px; }"_s.arg(theme::GOLD));
    if (truthy(num) && 1 <= *num && *num <= static_cast<long long>(frames.size())) {
        const QPixmap &pm = frames[size_t(*num - 1)];
        const double k = std::min({72.0 / pm.width(), 72.0 / pm.height(), 2.0});
        pic->setPixmap(pm.scaled(int(pm.width() * k), int(pm.height() * k), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    head->addWidget(pic);
    auto *col = new QVBoxLayout();
    col->addWidget(label(u"Name in the game"_s, u"sub"_s, false));
    name_edit = new QLineEdit(name(key));
    connect(name_edit, &QLineEdit::editingFinished, this, [this, key] { _rename(key); });
    col->addWidget(name_edit);
    col->addWidget(label(u"%1 · %2%3"_s.arg(key, K.rel, !gk.isEmpty() ? u" · row “%1”"_s.arg(name(gk)) : QString()),
                         u"faint"_s));
    head->addLayout(col, 1);
    s->lay->addLayout(head);
    dl->addWidget(s);
    // --- the picture
    s = new Section();
    s->lay->addWidget(_bold(u"Its picture"_s));
    // the size of a picture of the strip, big (C++ only, the user 2026-11-18), and a picture from the computer
    size_lbl = iconstrips::size_sign(iconstrips::frame_size(*strips, K.sprite));
    s->lay->addWidget(size_lbl);
    {
        auto *irw = new QWidget();
        auto *ir = new widgets::WrapRow(irw, 6);      // (the buttons wrap when the panel is narrow, never cut)
        // one button (the user 2026-11-18): a focus whose picture other focuses show too gets a picture of its own
        import_btn = new QPushButton(kind == u"focus" ? u"Import a picture for this national focus"_s
                                                      : u"Import a picture for this national value"_s);
        import_btn->setObjectName(u"primary"_s);
        import_btn->setToolTip(
            kind == u"focus"
                ? u"Pick a picture file on your computer: it becomes this national focus's picture — on Apply. If other "
                  u"focuses show the same picture, this focus gets a picture of its own and theirs stays as it is."_s
                : u"Pick a picture file on your computer: it becomes this national value's picture — on Apply."_s);
        import_btn->setEnabled(kind == u"focus" || truthy(num));
        connect(import_btn, &QPushButton::clicked, this, [this, key, path, num] {
            _import(key, path, num, kind == u"focus" && _shared(key, num));
        });
        ir->addWidget(import_btn);
        // the mod's pictures into one's own library (the user 2026-11-18)
        lib_one_btn = new QPushButton(u"Put this picture into my library"_s);
        lib_one_btn->setToolTip(u"Saves this picture as a .png file into your library folder, to use it in any mod."_s);
        lib_one_btn->setEnabled(truthy(num));
        connect(lib_one_btn, &QPushButton::clicked, this, [this, key, num] { _to_library(key, num, false); });
        ir->addWidget(lib_one_btn);
        lib_all_btn = new QPushButton(u"Put all %1 pictures into my library"_s.arg(
            iconstrips::frames(*strips, K.sprite).size()));
        lib_all_btn->setToolTip(u"Saves every picture of this strip as a .png file into your library folder."_s);
        connect(lib_all_btn, &QPushButton::clicked, this, [this, key, num] { _to_library(key, num, true); });
        ir->addWidget(lib_all_btn);
        s->lay->addWidget(irw);
    }
    {   // where it comes from (C++ only, 2026-11-16), and on a new entry a reminder how to put one's own in
        const QString words = iconstrips::picture_words(*strips, K.sprite, num);
        if (key == fresh) {
            auto *rem = label(u"New %1 - remember its picture. %2 To use your own: “Import a picture for this %1” above "
                              u"(the size in the frame), or one of your library folder below with “%3”%4."_s
                                  .arg(kind == u"focus" ? u"national focus"_s : u"national value"_s, words,
                                       kind == u"focus" ? u"Use it for this national focus"_s
                                                        : u"Use it for this national value"_s,
                                       kind == u"focus" ? u"; or pick another picture of the strip"_s : QString()),
                              u"sub"_s);
            rem->setStyleSheet(u"QLabel { background: %1; color: #14100a; padding: 6px 10px; border-radius: 4px; }"_s
                                   .arg(theme::GOLD));
            s->lay->addWidget(rem);
        } else {
            s->lay->addWidget(label(words, u"faint"_s));
        }
    }
    if (kind == u"focus") {
        auto *row = new QHBoxLayout();
        row->addWidget(label(u"Another picture of the strip"_s, u"sub"_s, false));
        auto *pk = new iconstrips::IconPicker(*strips, K.sprite, num);
        connect(pk, &iconstrips::IconPicker::picked, this, [this, key, path](long long nn) {
            _emit(u"%1: picture %2"_s.arg(name(key), QString::number(nn)),
                  blocks::set_value(text(), path, u"icon"_s, QString::number(nn)));
        });
        row->addWidget(pk);
        row->addStretch(1);
        s->lay->addLayout(row);
    } else {
        s->lay->addWidget(label(u"A national value shows the picture at its place in the list: this one is number %1, so "
                                u"picture %2 of the strip (%3 pictures). Drag it on the board to change its place — its "
                                u"picture goes with it."_s
                                    .arg(QString::number(i + 1), QString::number(i + 1), QString::number(frames.size())),
                                u"faint"_s));
    }
    lib = new QComboBox();
    lib->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    const QString folder = moreviews::library_dir(u"pictures"_s);
    QStringList names;
    for (const QString &f :     // (os.listdir: folders too)
         QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        for (const QString &ext : moreviews::PIC_EXT)
            if (py::lower(f).endsWith(ext)) {
                names << f;
                break;
            }
    std::sort(names.begin(), names.end());
    for (const QString &f : names)
        lib->addItem(f, pypath::join(folder, f));
    if (names.isEmpty())
        lib->addItem(u"(your library folder has no pictures yet)"_s, QVariant());
    s->lay->addWidget(label(u"A picture from your library folder (%1):"_s.arg(folder), u"faint"_s));
    s->lay->addWidget(lib);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    {
        auto *b = new QPushButton(kind == u"focus" ? u"Use it for this national focus"_s : u"Use it for this national value"_s);
        b->setToolTip(kind == u"focus" ? u"The library picture becomes this national focus's picture. If other focuses "
                                         u"show the same picture, this focus gets a picture of its own and theirs stays."_s
                                       : u"The library picture becomes this national value's picture."_s);
        b->setEnabled(kind == u"focus" || truthy(num));
        connect(b, &QPushButton::clicked, this, [this, key, path, num] {
            _library(key, path, num, kind == u"focus" && _shared(key, num));
        });
        row->addWidget(b);
    }
    row->addStretch(1);
    s->lay->addLayout(row);
    lib_msg = label(QString(), u"sub"_s);
    lib_msg->setStyleSheet(u"color: #ff786e;"_s);
    lib_msg->hide();
    s->lay->addWidget(lib_msg);
    dl->addWidget(s);
    // --- what it does
    s = new Section();
    s->lay->addWidget(_bold(kind == u"nv" ? u"What it gives"_s : u"What it does"_s));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    int r = 0;
    std::vector<const Node *> others;
    for (const Node &c : n.kids) {
        if (!keyed(c) || (c.key == u"icon" && kind == u"focus"))
            continue;
        if (c.is_block) {
            others.push_back(&c);
            continue;
        }
        auto *lb = new QLabel(loc(c.key) ? name(c.key) : _pretty(c.key));
        lb->setToolTip(c.key);
        grid->addWidget(lb, r, 0);
        const QString old = py::strip(c.value);
        auto *ed = new QLineEdit(old);
        ed->setFixedWidth(110);
        ed->setAlignment(Qt::AlignRight);
        connect(ed, &QLineEdit::editingFinished, this, [this, k = c.key, ed, old, key, path] {
            const QString v = py::strip(ed->text());
            if (!v.isEmpty() && v != old)
                _emit(u"%1: %2 = %3"_s.arg(name(key), k, v), blocks::set_value(text(), path, k, v));
        });
        grid->addWidget(ed, r, 1);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setToolTip(u"Take this line off"_s);
        connect(x, &QPushButton::clicked, this, [this, k = c.key, key, path] {
            _emit(u"%1: without %2"_s.arg(name(key), k), blocks::del_value(text(), path, k));
        });
        grid->addWidget(x, r, 2);
        ++r;
    }
    if (!r) {
        grid->addWidget(label(u"Nothing yet — add a line below."_s, u"faint"_s), 0, 0, 1, 3);
        r = 1;
    }
    add_key = new QComboBox();
    add_key->setEditable(true);
    add_key->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QSet<QString> have;
    for (const Node &c : n.kids)
        if (keyed(c))
            have.insert(c.key);
    for (const QString &k : _suggest())
        if (!have.contains(k))
            add_key->addItem(_pretty(k), k);
    add_key->setCurrentIndex(-1);
    add_key->lineEdit()->setPlaceholderText(u"a line more: pick or type its key …"_s);
    grid->addWidget(add_key, r, 0);
    add_val = new QLineEdit();
    add_val->setPlaceholderText(u"value"_s);
    add_val->setFixedWidth(110);
    connect(add_val, &QLineEdit::returnPressed, this, [this, key, path] { _add_line(key, path); });
    grid->addWidget(add_val, r, 1);
    auto *ab = new QPushButton(u"＋"_s);
    ab->setFixedWidth(30);
    connect(ab, &QPushButton::clicked, this, [this, key, path] { _add_line(key, path); });
    grid->addWidget(ab, r, 2);
    s->lay->addLayout(grid);
    const QString t = text();
    for (const Node *c : others) {
        auto *lt = new QLabel(u"%1 = { %2 }"_s.arg(c->key, py::split(py::slice(t, c->vstart + 1, c->vend - 1)).join(u' ')));
        lt->setWordWrap(true);
        lt->setStyleSheet(u"QLabel { color: %1; font-family: Consolas, monospace; }"_s.arg(theme::INK_DIM));
        lt->setToolTip(u"A block of conditions — change it on the list page of this file"_s);
        s->lay->addWidget(lt);
    }
    dl->addWidget(s);
    if (kind == u"nv")
        _fill_hidden(key);
    // --- place, key, delete
    s = new Section();
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[txt, d] : {std::pair{u"◀ Earlier"_s, -1}, std::pair{u"Later ▶"_s, 1}}) {
        auto *b = new QPushButton(txt);
        connect(b, &QPushButton::clicked, this, [this, key, dd = d] { _step(key, dd); });
        row->addWidget(b);
    }
    row->addStretch(1);
    del_btn = new QPushButton(u"Delete this %1"_s.arg(K.noun));
    del_btn->setToolTip(u"It goes at once — Ctrl+Z brings it back. A right click on its picture does the same. Several "
                        u"at a time: Ctrl + click them on the board, then the Delete key."_s);
    connect(del_btn, &QPushButton::clicked, this, [this, key] { _delete(key); });
    row->addWidget(del_btn);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    row->addWidget(label(u"Its key in the files"_s, u"sub"_s, false));
    key_edit = new QLineEdit(key);
    key_edit->setToolTip(u"The word the files use for it (no spaces). Other files that name it are NOT changed with it "
                         u"— rename only what nothing else refers to yet."_s);
    connect(key_edit, &QLineEdit::editingFinished, this, [this, key] { _rekey(key); });
    row->addWidget(key_edit, 1);
    s->lay->addLayout(row);
    msg = label(QString(), u"sub"_s);
    msg->setStyleSheet(u"color: #ff786e;"_s);
    msg->hide();
    s->lay->addWidget(msg);
    dl->addWidget(s);
    dl->addStretch(1);
}

void BoardPage::_fill_group(Section *s, const QString &gk)
{
    s->lay->addWidget(_bold(u"A row of national focuses"_s));
    s->lay->addWidget(label(u"Its name in the game"_s, u"sub"_s, false));
    auto *ed = new QLineEdit(name(gk));
    connect(ed, &QLineEdit::editingFinished, this, [this, ed, gk] {
        const QString v = py::strip(ed->text());
        if (!v.isEmpty() && v != name(gk))
            Q_EMIT nameEdit(gk, py::split(ed->text()).join(u' '));
    });
    s->lay->addWidget(ed);
    s->lay->addWidget(label(u"%1 · %2"_s.arg(gk, K.rel), u"faint"_s));
    long long nn = 0;
    for (const Group &g : rows())
        if (g.key == gk) {
            nn = static_cast<long long>(g.entries.size());
            break;
        }
    row_del = new QPushButton(u"Delete this row%1"_s.arg(
        nn ? u" and its %1 focus%2"_s.arg(QString::number(nn), plural(nn, QString(), u"es"_s)) : QString()));
    row_del->setToolTip(u"It goes at once%1 — Ctrl+Z brings it back. Several rows: Ctrl + click their bars, then the "
                        u"Delete key."_s.arg(nn ? u", with the focuses in it"_s : QString()));
    connect(row_del, &QPushButton::clicked, this, [this, gk] { _del_group(gk); });
    s->lay->addWidget(row_del, 0, Qt::AlignLeft);
}

void BoardPage::_fill_multi(Section *s)
{
    // Several picked with Ctrl: what they are, and one button that takes them all out.
    const auto rs = rows();
    QStringList gone;
    for (const Group &g : rs)
        if (multi.contains(u'@' + g.key))
            gone << g.key;
    long long inside = 0;
    QStringList ents;
    for (const Group &g : rs) {
        if (gone.contains(g.key)) {
            inside += static_cast<long long>(g.entries.size());
            continue;
        }
        for (const Node &n : g.entries)
            if (multi.contains(n.key))
                ents << n.key;
    }
    const QString noun = K.noun + (K.noun.endsWith(u's') ? u"es"_s : u"s"_s);
    QStringList parts;
    if (!ents.isEmpty())
        parts << u"%1 %2"_s.arg(QString::number(ents.size()), ents.size() != 1 ? noun : K.noun);
    if (!gone.isEmpty())
        parts << u"%1 row%2%3"_s.arg(QString::number(gone.size()), plural(gone.size(), QString(), u"s"_s),
                                     inside ? u" with the %1 focus%2 in them"_s.arg(QString::number(inside),
                                                                                  plural(inside, QString(), u"es"_s))
                                            : QString());
    s->lay->addWidget(_bold(u"%1 picked"_s.arg(multi.size())));
    s->lay->addWidget(label(parts.join(u" and "_s), u"sub"_s));
    QStringList names;
    for (const QString &g : gone)
        names << name(g) + u" (row)"_s;
    for (const QString &k : ents)
        names << name(k);
    s->lay->addWidget(label(names.mid(0, 40).join(u", "_s) + (names.size() > 40 ? u" …"_s : QString()), u"faint"_s));
    multi_del = new QPushButton(u"Delete the %1 picked"_s.arg(multi.size()));
    multi_del->setStyleSheet(u"QPushButton { color: #ff786e; font-weight: 700; border: 2px solid #ff786e; }"_s);
    multi_del->setToolTip(u"All of them at once, as one step — Ctrl+Z brings them back. The Delete key does the same."_s);
    connect(multi_del, &QPushButton::clicked, this, [this] { _delete_picked(); });
    s->lay->addWidget(multi_del, 0, Qt::AlignLeft);
    auto *c = new QPushButton(u"Pick none"_s);
    connect(c, &QPushButton::clicked, this, [this] { _pick(std::nullopt); });
    s->lay->addWidget(c, 0, Qt::AlignLeft);
    s->lay->addWidget(label(u"Ctrl + click adds one more or takes one off."_s, u"faint"_s));
}

void BoardPage::_fill_hidden(const QString &key)
{
    // What the pop files give for this national value.
    auto *s = new Section();
    s->lay->addWidget(_bold(u"What the pop files give for it"_s));
    const QStringList files = pop_files();
    const auto uses = nv_uses(*store, files, key);
    const auto nm = [this](const QString &k) { return name(k); };
    s->lay->addWidget(label(
        u"These do not stand in the national value itself: a pop file says “this factor counts while the nation has "
        u"this national value” for a chance of its pops (above 0 raises the chance, below 0 lowers it). The whole "
        u"picture of a chance: the page Pop influences. %1"_s
            .arg(!uses.empty() ? u"Found: %1 such line%2."_s.arg(QString::number(uses.size()),
                                                                plural(static_cast<long long>(uses.size()), QString(), u"s"_s))
                               : u"None of the pop files asks for this national value."_s),
        u"faint"_s));
    OrderedMap<QString, long long> per;
    for (const NvUse &u : uses)
        per[u.rel] = (per.contains(u.rel) ? *per.get(u.rel) : 0) + 1;
    const bool fold = uses.size() > 12;                // many: a heading per pop, its lines behind it
    QGridLayout *grid = nullptr;
    OptStr seen;
    for (size_t r = 0; r < uses.size(); ++r) {
        const NvUse &u = uses[r];
        if (!seen || u.rel != *seen) {
            seen = u.rel;
            auto *host = new QWidget();
            grid = new QGridLayout(host);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(8);
            grid->setVerticalSpacing(4);
            grid->setColumnStretch(0, 1);
            if (fold) {
                const bool is_open = _open.count({key, *seen}) > 0;
                const long long pn = *per.get(*seen);
                auto *hb = new QPushButton(u"%1  %2 — %3 line%4"_s.arg(is_open ? u"▾"_s : u"▸"_s,
                                                                      place_words(*seen, {}, nm).split(u" — "_s)[0],
                                                                      QString::number(pn), plural(pn, QString(), u"s"_s)));
                hb->setStyleSheet(u"QPushButton { text-align: left; font-weight: 600; }"_s);
                hb->setToolTip(*seen);
                connect(hb, &QPushButton::clicked, this, [this, k = std::pair{key, *seen}] { _toggle(k); });
                s->lay->addWidget(hb);
                host->setVisible(is_open);
            }
            s->lay->addWidget(host);
        }
        const QString words = place_words(u.rel, u.path, nm);
        auto *lb = new QLabel(words + (u.neg ? u"  — while it does NOT have this value"_s : QString()) +
                              (!u.also.isEmpty() ? u"\nalso needs: "_s + u.also : QString()));
        lb->setWordWrap(true);
        lb->setToolTip(u"%1 › %2"_s.arg(u.rel, u.path.join(u" › "_s)));
        const int gr = static_cast<int>(r);
        grid->addWidget(lb, gr, 0);
        grid->addWidget(new QLabel(u"factor"_s), gr, 1);
        auto *ed = new QLineEdit(u.factor);
        ed->setFixedWidth(70);
        ed->setAlignment(Qt::AlignRight);
        ed->setEnabled(u.fspan.has_value());
        connect(ed, &QLineEdit::editingFinished, this, [this, key, r, ed] { _hidden_factor(key, r, ed); });
        grid->addWidget(ed, gr, 2);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setToolTip(u"Take this modifier out of %1"_s.arg(u.rel));
        connect(x, &QPushButton::clicked, this, [this, key, r] { _hidden_remove(key, r); });
        grid->addWidget(x, gr, 3);
    }
    s->lay->addWidget(_bold(u"Add one"_s));
    h_place = new QComboBox();
    h_place->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[rel, path] : pop_places(*store, files))
        h_place->addItem(place_words(rel, path, nm), QStringList{rel} + path);   // (rel, path) as one list
    s->lay->addWidget(h_place);
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"factor (above 0 raises, below 0 lowers)"_s, u"sub"_s, false));
    h_factor = new QLineEdit();
    h_factor->setPlaceholderText(u"e.g. 0.5"_s);
    h_factor->setFixedWidth(80);
    connect(h_factor, &QLineEdit::returnPressed, this, [this, key] { _hidden_add(key); });
    row->addWidget(h_factor);
    auto *b = new QPushButton(u"＋ Add to the pop file"_s);
    connect(b, &QPushButton::clicked, this, [this, key] { _hidden_add(key); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    h_msg = label(QString(), u"sub"_s);
    h_msg->setStyleSheet(u"color: #ff786e;"_s);
    h_msg->hide();
    s->lay->addWidget(h_msg);
    dl->addWidget(s);
}

void BoardPage::_toggle(const std::pair<QString, QString> &k)
{
    if (!_open.erase(k))
        _open.insert(k);
    _fill_detail();
}

QStringList BoardPage::_suggest()
{
    // Keys other entries of the file use (and, for focuses, the pop types): for "a line more".
    OrderedMap<QString, long long> seen;
    for (const Group &g : rows())
        for (const Node &n : g.entries)
            for (const Node &c : n.kids)
                if (keyed(c) && !c.is_block && c.key != u"icon")
                    seen[c.key] = (seen.contains(c.key) ? *seen.get(c.key) : 0) + 1;
    QStringList out;
    for (const auto &kv : seen)
        out << kv.first;
    std::stable_sort(out.begin(), out.end(), [&seen](const QString &a, const QString &b) {
        const long long na = *seen.get(a), nb = *seen.get(b);
        return na != nb ? na > nb : a < b;
    });
    if (kind == u"focus") {
        QStringList pts;
        for (const auto &kv : mod.poptypes)
            pts << kv.first;
        std::sort(pts.begin(), pts.end());
        for (const QString &k : pts)
            if (!seen.contains(k))
                out << k;
    }
    return out;
}

// ------------------------------------------------------------------ changes
void BoardPage::_emit(const QString &what, const QString &nw)
{
    if (nw != text())
        Q_EMIT scriptEdit(what, K.rel, nw);
}

void BoardPage::_pick(const OptStr &key)
{
    sel = key;
    multi.clear();
    _show();
}

void BoardPage::_ctrl(const QString &key)
{
    // Ctrl + click: one more of the picked ones, or one fewer.
    if (multi.isEmpty() && sel && !sel->isEmpty() && *sel != key)
        multi = {*sel};
    if (!multi.remove(key))
        multi.insert(key);
    sel = multi.size() == 1 ? OptStr(*multi.begin()) : std::nullopt;
    _show();
}

void BoardPage::_delete_picked()
{
    // The Delete key / the button: everything picked goes, as one step.
    if (multi.size() >= 2)
        _delete_many(multi);
    else if (sel && sel->startsWith(u'@'))
        _del_group(sel->mid(1));
    else if (sel && !sel->isEmpty())
        _delete(*sel);
}

void BoardPage::_delete_many(const QSet<QString> &keys)
{
    QString t = text();
    const auto rs = rows();
    QSet<QString> gone;
    for (const QString &k : keys)
        if (k.startsWith(u'@'))
            gone.insert(k.mid(1));
    std::vector<Span> spans;
    long long n_ent = 0, n_row = 0, n_in = 0;
    for (const Group &g : rs) {
        if (gone.contains(g.key) && g.node) {
            spans.push_back(_line_span(t, *g.node));
            ++n_row;
            n_in += static_cast<long long>(g.entries.size());
        } else {
            for (const Node &n : g.entries)
                if (keys.contains(n.key)) {
                    spans.push_back(_line_span(t, n));
                    ++n_ent;
                }
        }
    }
    if (spans.empty())
        return;
    const QStringList old = _order();
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a > b; });
    for (const auto &[a, b] : spans)
        t = py::head(t, a) + py::tail(t, b);
    sel.reset();
    multi.clear();
    const QString noun = K.noun + (K.noun.endsWith(u's') ? u"es"_s : u"s"_s);
    QStringList what;
    if (n_ent)
        what << u"%1 %2"_s.arg(QString::number(n_ent), n_ent != 1 ? noun : K.noun);
    if (n_row)
        what << u"%1 row%2%3"_s.arg(QString::number(n_row), plural(n_row, QString(), u"s"_s),
                                    n_in ? u" (with %1 in them)"_s.arg(n_in) : QString());
    _commit(what.join(u" and "_s) + u" deleted"_s, t, kind == u"nv" ? std::optional<QStringList>(old) : std::nullopt);
}

void BoardPage::_rename(const QString &key)
{
    const QString t = py::split(name_edit->text()).join(u' ');
    if (!t.isEmpty() && t != name(key))
        Q_EMIT nameEdit(key, t);
}

void BoardPage::_rekey(const QString &key)
{
    const QString nw = py::strip(key_edit->text());
    if (nw.isEmpty() || nw == key)
        return;
    bool taken = false;
    for (const Group &g : rows())
        for (const Node &n : g.entries)
            taken = taken || n.key == nw;
    if (!WORD_RE.match(nw).hasMatch() || taken) {
        msg->setText(u"“%1” can't be its key: letters, digits and _ only, and no other one may have it."_s.arg(nw));
        msg->show();
        return;
    }
    const Node n = std::get<2>(*node(key));
    const QString t = text();
    sel = nw;
    _emit(u"%1: its key is now %2"_s.arg(key, nw), py::head(t, n.kstart) + nw + py::tail(t, n.kstart + key.size()));
}

void BoardPage::_add_line(const QString &key, const QStringList &path)
{
    const int ci = add_key->currentIndex();
    const QString k = ci >= 0 && add_key->currentText() == add_key->itemText(ci) ? add_key->currentData().toString()
                                                                                  : py::strip(add_key->currentText());
    const QString v = py::strip(add_val->text());
    if (k.isEmpty() || v.isEmpty() || !WORD_RE.match(k).hasMatch()) {
        msg->setText(u"A line needs a key (one word) and a value."_s);
        msg->show();
        return;
    }
    _emit(u"%1: %2 = %3"_s.arg(name(key), k, v), blocks::set_value(text(), path, k, v));
}

QStringList BoardPage::_order()
{
    QStringList out;
    for (const Group &g : rows())
        for (const Node &n : g.entries)
            out << n.key;
    return out;
}

std::pair<iconstrips::StripFiles, OrderedMap<QString, QString>> BoardPage::_strip_reorder(const QStringList &old_order,
                                                                                          const QStringList &new_order)
{
    // (files, texts) that put a national value's pictures into the new order of the list — or ({}, {}) when the
    // strip has no picture for some of them (then only the list changes).
    if (kind != u"nv")
        return {};
    const iconstrips::Sprites sps = strips->sprites();
    const long long n = sps.contains(K.sprite) ? sps.get(K.sprite)->frames : 0;
    for (const QString &k : new_order)
        if (!old_order.contains(k))
            return {};
    std::vector<long long> order;
    for (const QString &k : new_order)
        order.push_back(old_order.indexOf(k) + 1);
    if (order.empty() || *std::max_element(order.begin(), order.end()) > n)
        return {};
    std::set<long long> gone;
    for (const QString &k : old_order)
        if (!new_order.contains(k))
            gone.insert(old_order.indexOf(k) + 1);
    const std::vector<long long> kept = order;
    for (long long i = 1; i <= n; ++i)                 // (spare pictures stay at the end)
        if (std::find(kept.begin(), kept.end(), i) == kept.end() && !gone.count(i))
            order.push_back(i);
    auto got = strips->reorder(K.sprite, order);
    if (auto *ok = std::get_if<0>(&got))
        return *ok;
    return {};
}

void BoardPage::_commit(const QString &what, const QString &new_text, const std::optional<QStringList> &old_order)
{
    // A change of the list — with the pictures' order for national values — as one step.
    iconstrips::StripFiles files;
    OrderedMap<QString, QString> texts;
    if (old_order) {
        QStringList new_order;
        for (const Group &g : read_entries(new_text, K.grouped))
            for (const Node &n : g.entries)
                new_order << n.key;
        if (new_order != *old_order)
            std::tie(files, texts) = _strip_reorder(*old_order, new_order);
    }
    if ((!files.isEmpty() || !texts.isEmpty()) && strip_change) {
        texts[K.rel] = new_text;
        strip_change(what, files, texts);
    } else {
        _emit(what, new_text);
    }
}

void BoardPage::_move(const QString &key, const QString &group, const QString &before)
{
    const QStringList old = _order();
    const QString nw = move_entry(text(), K.grouped, key, group, before);
    sel = key;
    _commit(u"%1 moved"_s.arg(name(key)), nw, kind == u"nv" ? std::optional<QStringList>(old) : std::nullopt);
}

void BoardPage::_step(const QString &key, int d)
{
    const auto got = node(key);
    const QString gk = std::get<0>(*got);
    const long long i = std::get<1>(*got);
    const auto rs = rows();
    const Group *g = nullptr;
    for (const Group &x : rs)
        if (x.key == gk) {
            g = &x;
            break;
        }
    const long long j = i + d, n = static_cast<long long>(g->entries.size());
    if (!(0 <= j && j < n))
        return;
    const QString before = d < 0 ? g->entries[size_t(j)].key : (j + 1 < n ? g->entries[size_t(j + 1)].key : QString());
    _move(key, gk, before);
}

void BoardPage::_delete(const QString &key)
{
    const auto got = node(key);
    if (!got)
        return;
    const QStringList old = _order();
    const QString t = text();
    const auto [a, b] = _line_span(t, std::get<2>(*got));
    sel.reset();
    _commit(u"%1 deleted"_s.arg(name(key)), py::head(t, a) + py::tail(t, b),
            kind == u"nv" ? std::optional<QStringList>(old) : std::nullopt);
}

QString BoardPage::_free_key(const QString &stem)
{
    QSet<QString> have;
    for (const QString &k : _order())
        have.insert(k);
    for (const Group &g : rows())
        have.insert(g.key);
    long long i = 1;
    while (have.contains(u"%1_%2"_s.arg(stem).arg(i)))
        ++i;
    return u"%1_%2"_s.arg(stem).arg(i);
}

void BoardPage::_add(const QString &group)
{
    const auto last_part = [](const QString &k) { return k.mid(k.lastIndexOf(u'_') + 1); };   // key.rsplit("_", 1)[-1]
    const iconstrips::Sprites sps = strips->sprites();
    const long long total = sps.contains(K.sprite) ? sps.get(K.sprite)->frames : 0;
    if (kind == u"focus") {
        const QString key = _free_key(u"new_focus"_s);
        std::set<OptInt> used;
        for (const Group &g : rows())
            for (size_t i = 0; i < g.entries.size(); ++i)
                used.insert(icon_of(g.key, static_cast<long long>(i), g.entries[i]));
        long long fr = 1;
        for (long long k = 1; k <= total; ++k)
            if (!used.count(k)) {
                fr = k;
                break;
            }
        const QString nw = blocks::add_block(text(), {group}, key, {u"icon = %1"_s.arg(fr)});
        sel = key;
        fresh = key;
        _emit(u"New national focus %1"_s.arg(key), nw);
        Q_EMIT nameEdit(key, u"New Focus %1"_s.arg(last_part(key)));
        return;
    }
    const QString key = _free_key(u"nv_new"_s);
    QString t = text();
    while (t.endsWith(u'\n'))
        t.chop(1);
    const QString nw = t + u"\n\n%1 = {\n}\n"_s.arg(key);
    const long long n_now = _order().size();
    sel = key;
    fresh = key;
    if (n_now + 1 > total && strip_change) {           // the strip needs a picture for it
        std::vector<iconstrips::Filler> fill(size_t(n_now + 1 - total), iconstrips::Filler{u"number"_s});
        auto got = strips->extend(K.sprite, fill);
        if (auto *ok = std::get_if<0>(&got)) {
            OrderedMap<QString, QString> texts = ok->second;
            texts[K.rel] = nw;
            strip_change(u"New national value %1 (with a picture of its own)"_s.arg(key), ok->first, texts);
            Q_EMIT nameEdit(key, u"New Value %1"_s.arg(last_part(key)));
            return;
        }
    }
    _emit(u"New national value %1"_s.arg(key), nw);
    Q_EMIT nameEdit(key, u"New Value %1"_s.arg(last_part(key)));
}

// ------------------------------------------------------------------ the game's own focus window
std::optional<BoardPage::Win> BoardPage::_window()
{
    const QString t = store->text(focuswindow::GUI);
    const iconstrips::Sprites sps = strips->sprites();
    const iconstrips::Sprite *sp = sps.get(focuswindow::SPRITE);
    const auto img = sp ? strips->image(focuswindow::SPRITE) : std::nullopt;
    if (!img || !focuswindow::read(t))
        return std::nullopt;
    const QString rel = strips->resolve(sp->tex).first;
    long long most = 0, n = 0;
    for (const Group &g : rows()) {
        most = std::max(most, static_cast<long long>(g.entries.size()));
        ++n;
    }
    return Win{t, *img, rel, n, most};
}

void BoardPage::_window_note()
{
    const auto w = _window();
    if (!w) {
        win_note->setText(u"The game's focus window (%1) can't be read here — it is not checked."_s.arg(focuswindow::GUI));
        win_btn->hide();
        return;
    }
    const focuswindow::G g = *focuswindow::read(w->text);
    const focuswindow::PlanOut pl = focuswindow::plan(g, w->img, w->rows, w->most);
    QStringList off;
    if (pl.rows0 != w->rows)
        off << u"it is drawn for <b>%1 rows</b>, you have <b>%2</b>"_s.arg(pl.rows0).arg(w->rows);
    if (w->most > pl.fit0 && pl.width != g.icons.x)
        off << u"<b>%1 focuses</b> fit side by side in a row, your longest row has <b>%2</b> (the game pushes them over "
               u"each other)"_s.arg(pl.fit0).arg(w->most);
    if (!off.isEmpty())
        win_note->setText(u"<span style='color:%1'>⚠ The game's focus window does not fit: %2.</span> “Fit the game's "
                          u"window” draws it again — rows %3 px apart, the picture %4 px high%5.%6"_s.arg(
                              theme::WARN, off.join(u"; "_s), QString::number(pl.step), QString::number(pl.height),
                              pl.extra ? u", %1 px wider"_s.arg(pl.extra) : QString(),
                              !pl.notes.isEmpty() ? u" <span style='color:#ff786e'>"_s + pl.notes.join(u"; "_s) + u".</span>"_s
                                                  : QString()));
    else
        win_note->setText(u"The game's focus window fits: drawn for %1 rows, up to %2 focuses side by side (your longest "
                          u"row: %3)."_s.arg(pl.rows0).arg(pl.fit0).arg(w->most));
    _can_fit = !off.isEmpty() && strip_change;
    win_btn->setVisible(_can_fit);
}

void BoardPage::_set_pv(const QString &k)
{
    pv_mode = k;
    _preview();
}

void BoardPage::_preview()
{
    // The game's focus window drawn: as the files are, or as a fit would make them.
    if (!pv)
        return;
    const auto w = _window();
    if (pv_mode == u"fit" && !_can_fit)
        pv_mode = u"now"_s;
    for (const auto &[k, b] : pv_btn)
        b->setChecked(k == pv_mode);
    pv_btn[1].second->setEnabled(_can_fit);
    if (!w) {
        pv->set(std::nullopt, {});
        pv_note->setText(u"The game's focus window (%1) can't be read here — nothing to draw."_s.arg(focuswindow::GUI));
        return;
    }
    QString t = w->text;
    QImage img = w->img;
    if (pv_mode == u"fit") {
        const focuswindow::Fitted f = focuswindow::fit(t, img, w->rows, w->most);
        if (f.text) {
            t = *f.text;
            img = f.img;
        }
    }
    if (!_game_look || _game_look->store != store) {
        auto lk = std::make_unique<LookOf>(mod, store);
        _game_look = &lk->g;
        _look = std::move(lk);
    }
    std::vector<focuswindow::Row> rs;
    for (const Group &g : rows()) {
        focuswindow::Row r{g.key, name(g.key), {}};
        for (size_t i = 0; i < g.entries.size(); ++i)
            r.cells.emplace_back(g.entries[i].key, icon_of(g.key, static_cast<long long>(i), g.entries[i]).value_or(0));
        rs.push_back(std::move(r));
    }
    const focuswindow::Drawn d = focuswindow::picture(t, img, rs, iconstrips::frames(*strips, K.sprite), _look.get(),
                                                      [this](const QString &k) { return loc(k); }, sel,
                                                      QStringList(multi.begin(), multi.end()));
    pv->set(d.img, d.hits);
    pv_note->setText(u"%1: %2 rows, the longest with %3.%4"_s.arg(
        pv_mode == u"fit" ? u"As a fit would make it"_s : u"As it is now"_s, QString::number(rs.size()),
        QString::number(w->most),
        !d.notes.isEmpty() ? u" <span style='color:%1'>⚠ %2.</span>"_s.arg(theme::WARN, d.notes.join(u"; "_s))
                           : u" Everything has its place."_s));
}

void BoardPage::_fit_window()
{
    const auto w = _window();
    if (!w || !strip_change)
        return;
    const focuswindow::Fitted f = focuswindow::fit(w->text, w->img, w->rows, w->most);
    if (!f.text) {
        win_note->setText(u"<span style='color:#ff786e'>%1</span>"_s.arg(f.why));
        return;
    }
    iconstrips::StripFiles files;
    files.insert(w->rel, {u"strip"_s, f.img, {}});
    OrderedMap<QString, QString> texts;
    texts.insert(focuswindow::GUI, *f.text);
    strip_change(u"The game's focus window fitted to %1 rows, %2 in the longest"_s.arg(w->rows).arg(w->most), files, texts);
}

void BoardPage::_add_group()
{
    const QString key = _free_key(u"new_focus_group"_s);
    sel = u'@' + key;
    QString t = text();
    while (t.endsWith(u'\n'))
        t.chop(1);
    _emit(u"New row of national focuses"_s, t + u"\n\n%1 = {\n}\n"_s.arg(key));
    Q_EMIT nameEdit(key, u"New Row %1"_s.arg(key.mid(key.lastIndexOf(u'_') + 1)));
}

void BoardPage::_del_group(const QString &gk)
{
    // A row goes — with the focuses in it.
    const auto rs = rows();
    const Group *g = nullptr;
    for (const Group &x : rs)
        if (x.key == gk) {
            g = &x;
            break;
        }
    if (!g || !g->node)
        return;
    const QString t = text();
    const auto [a, b] = _line_span(t, *g->node);
    sel.reset();
    multi.remove(u'@' + gk);
    const long long n = static_cast<long long>(g->entries.size());
    _emit(u"Row %1 deleted%2"_s.arg(name(gk), n ? u" with its %1 focus%2"_s.arg(QString::number(n),
                                                                              plural(n, QString(), u"es"_s))
                                                 : QString()),
          py::head(t, a) + py::tail(t, b));
}

void BoardPage::_import(const QString &key, const QStringList &path, OptInt num, bool as_new)
{
    const std::optional<QSize> sz = iconstrips::frame_size(*strips, K.sprite);
    const QString p = QFileDialog::getOpenFileName(
        this, sz ? u"A picture of %1 × %2 pixels"_s.arg(sz->width()).arg(sz->height()) : u"A picture"_s, QString(),
        iconstrips::picture_filter());
    if (p.isEmpty())
        return;
    const auto img = imgio::read_image(p);
    if (!img || img->isNull()) {
        lib_msg->setStyleSheet(u"color: #ff786e;"_s);
        lib_msg->setText(u"%1 can't be read as a picture."_s.arg(pypath::basename(p)));
        lib_msg->show();
        return;
    }
    _use_image(key, path, num, as_new, *img, p);
}

bool BoardPage::_use_image(const QString &key, const QStringList &path, OptInt num, bool as_new, const QImage &img,
                           const QString &p)
{
    if (!strip_change)
        return false;
    QString fitted;
    if (const auto sz = iconstrips::frame_size(*strips, K.sprite); sz && img.size() != *sz)
        fitted = u" — it was %1 × %2 pixels, fitted to %3 × %4"_s.arg(img.width()).arg(img.height()).arg(sz->width()).arg(
            sz->height());
    if (as_new) {
        auto got = strips->extend(K.sprite, {iconstrips::Filler{u"image"_s, 0, img, {}}});
        if (auto *why = std::get_if<1>(&got)) {
            lib_msg->setText(*why);
            lib_msg->show();
            return false;
        }
        auto &ok = std::get<0>(got);
        const long long n = strips->sprites().get(K.sprite)->frames + 1;
        OrderedMap<QString, QString> texts = ok.second;
        texts[K.rel] = blocks::set_value(text(), path, u"icon"_s, QString::number(n));
        strip_change(u"%1: a new picture (%2)%3"_s.arg(name(key), pypath::basename(p), fitted), ok.first, texts);
    } else {
        const iconstrips::StripFiles files = strips->put(K.sprite, num.value_or(0), img);
        if (files.isEmpty()) {
            lib_msg->setText(u"That picture can't be replaced (the strip's picture can't be read)."_s);
            lib_msg->show();
            return false;
        }
        strip_change(u"%1: picture %2 replaced (%3)%4"_s.arg(name(key), num ? QString::number(*num) : u"None"_s,
                                                             pypath::basename(p), fitted),
                     files, {});
    }
    return true;
}

bool BoardPage::_shared(const QString &key, OptInt num)
{
    if (!num)
        return true;                                // (no picture of its own: a new one)
    for (const QString &k : _order()) {
        if (k == key)
            continue;
        const auto got = node(k);
        if (got && icon_of(std::get<0>(*got), std::get<1>(*got), std::get<2>(*got)) == num)
            return true;
    }
    return false;
}

void BoardPage::_to_library(const QString &key, OptInt num, bool all)
{
    const auto sz = iconstrips::frame_size(*strips, K.sprite);
    const auto img = strips->image(K.sprite);
    if (!sz || !img || img->isNull())
        return;
    const long long n = img->width() / std::max(1, sz->width());
    QStringList saved;
    for (long long i = all ? 1 : num.value_or(0); i >= 1 && i <= (all ? n : num.value_or(0)) && i <= n; ++i) {
        const QString what = all ? u"%1 %2"_s.arg(QString(K.sprite).remove(u"GFX_"_s)).arg(i, 2, 10, u'0')
                                 : u"%1"_s.arg(name(key));
        const QString p = iconstrips::to_library(img->copy(static_cast<int>(i - 1) * sz->width(), 0, sz->width(),
                                                           sz->height()),
                                                 mod.name(), what);
        if (!p.isEmpty()) {
            saved << pypath::basename(p);
            if (lib) {
                if (lib->count() == 1 && !lib->itemData(0).isValid())
                    lib->clear();                   // ("(your library folder has no pictures yet)")
                lib->addItem(pypath::basename(p), p);
            }
        }
    }
    lib_msg->setStyleSheet(saved.isEmpty() ? u"color: #ff786e;"_s : u"color: #8fd48a;"_s);
    lib_msg->setText(saved.isEmpty() ? u"The picture couldn't be saved into your library folder."_s
                     : saved.size() == 1 ? u"Into your library: %1"_s.arg(saved[0])
                                         : u"Into your library: %1 pictures (%2 … %3)"_s.arg(saved.size()).arg(saved.first(),
                                                                                                      saved.last()));
    lib_msg->show();
}

void BoardPage::_library(const QString &key, const QStringList &path, OptInt num, bool as_new)
{
    const QString p = lib->currentData().toString();
    const auto img = !p.isEmpty() ? imgio::read_image(p) : std::nullopt;
    if (!img || img->isNull() || !strip_change) {
        lib_msg->setText(u"Pick a picture of your library folder first (the folder has none, or this one can't be read)."_s);
        lib_msg->show();
        return;
    }
    _use_image(key, path, num, as_new, *img, p);
}


// --- the pop files
void BoardPage::_hidden_factor(const QString &key, size_t n, QLineEdit *ed)
{
    const auto uses = nv_uses(*store, pop_files(), key);
    const QString v = py::strip(ed->text()).replace(u',', u'.');
    if (n >= uses.size() || !uses[n].fspan || v == uses[n].factor)
        return;
    if (!NUM_RE.match(v).hasMatch()) {
        h_msg->setText(u"A factor is a number (like 0.5 or -1)."_s);
        h_msg->show();
        return;
    }
    const NvUse &u = uses[n];
    const QString t = store->text(u.rel);
    Q_EMIT scriptEdit(u"%1: %2 × %3"_s.arg(name(key), place_words(u.rel, u.path, [this](const QString &k) { return name(k); }), v),
                      u.rel, py::head(t, u.fspan->first) + v + py::tail(t, u.fspan->second));
}

void BoardPage::_hidden_remove(const QString &key, size_t n)
{
    const auto uses = nv_uses(*store, pop_files(), key);
    if (n >= uses.size())
        return;
    const NvUse &u = uses[n];
    const QString t = store->text(u.rel);
    Q_EMIT scriptEdit(u"%1: no longer counts for %2"_s.arg(name(key),
                                                           place_words(u.rel, u.path, [this](const QString &k) { return name(k); })),
                      u.rel, py::head(t, u.span.first) + py::tail(t, u.span.second));
}

void BoardPage::_hidden_add(const QString &key)
{
    const QStringList d = h_place->currentData().toStringList();
    const QString v = py::strip(h_factor->text()).replace(u',', u'.');
    if (d.isEmpty() || !NUM_RE.match(v).hasMatch()) {
        h_msg->setText(u"Pick where it counts and give the factor as a number (like 0.5 or -1)."_s);
        h_msg->show();
        return;
    }
    const QString rel = d[0];
    const QStringList path = d.mid(1);
    const QString t = store->text(rel);
    const QString nw = blocks::add_block(t, path, u"modifier"_s, {u"factor = %1"_s.arg(v), u"nationalvalue = %1"_s.arg(key)});
    if (nw == t) {
        h_msg->setText(u"That place could not be found in %1 any more."_s.arg(rel));
        h_msg->show();
        return;
    }
    _open.insert({key, rel});
    Q_EMIT scriptEdit(u"%1: %2 × %3"_s.arg(name(key), place_words(rel, path, [this](const QString &k) { return name(k); }), v),
                      rel, nw);
}

// ---------------------------------------------------------------------------------------------- with the list
BoardWithList::BoardWithList(moddata::Mod &mod, const QString &kind, QWidget *old_,
                             std::function<void(document::Document *)> old_refresh_, StripChange strip_change,
                             QWidget *parent)
    : QWidget(parent), board(new BoardPage(mod, kind, std::move(strip_change))), old(old_),
      old_refresh(std::move(old_refresh_))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(16, 8, 16, 0);
    bar->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    const std::pair<QString, QString> segs[2] = {{u"▦ Pictures"_s, u"As the game shows them: click, drag, add"_s},
                                                 {u"☰ List"_s, u"The list with every field and the conditions"_s}};
    for (int i = 0; i < 2; ++i) {
        auto *b = new QPushButton(segs[i].first);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setToolTip(segs[i].second);
        connect(b, &QPushButton::clicked, this, [this, i] { set_mode(i); });
        grp->addButton(b);
        bar->addWidget(b);
        btns.push_back(b);
    }
    bar->addStretch(1);
    lay->addLayout(bar);
    stack = new QStackedWidget();
    stack->addWidget(board);
    stack->addWidget(old);
    lay->addWidget(stack, 1);
    connect(board, &BoardPage::scriptEdit, this, &BoardWithList::scriptEdit);
    connect(board, &BoardPage::nameEdit, this, &BoardWithList::nameEdit);
    const std::pair<const char *, const char *> sigs[4] = {      // (the old page's signals, those it has)
        {SIGNAL(scriptEdit(QString,QString,QString)), "scriptEdit(QString,QString,QString)"},
        {SIGNAL(nameEdit(QString,QString)), "nameEdit(QString,QString)"},
        {SIGNAL(fileRemove(QString,QString)), "fileRemove(QString,QString)"},
        {SIGNAL(bigRequested(QString,QStringList,QString)), "bigRequested(QString,QStringList,QString)"}};
    for (const auto &[sig, norm] : sigs)
        if (old->metaObject()->indexOfSignal(norm) >= 0)
            connect(old, sig, this, sig);
}

void BoardWithList::set_mode(int n)
{
    btns[size_t(n)]->setChecked(true);
    stack->setCurrentIndex(n);
    if (doc)
        refresh(doc);
}

void BoardWithList::refresh(document::Document *doc_)
{
    doc = doc_;
    if (stack->currentIndex() == 0)
        board->refresh(doc);
    else
        old_refresh(doc);
}

// ---------------------------------------------------------------------------------------------- every strip's users
std::vector<StripEntry> strip_entries(const moddata::Mod &mod, scriptstore::ScriptStore *store, const iconstrips::Use &use)
{
    std::vector<StripEntry> out;
    QStringList rels = use.rels;
    if (!use.folder.isEmpty())
        rels += entryview::_files(mod, store, use.folder);
    const bool many = rels.size() > 1 && use.folder.isEmpty();
    for (const QString &rel : rels) {
        script::Nodes nodes;
        try {
            nodes = script::parse(store->text(rel));
        } catch (const fs::OSError &) {
            continue;
        }
        const QString stem = pypath::splitext_stem(pypath::basename(rel));
        const QString row = many ? _pretty(stem) : QString();
        if (use.order) {
            long long i = use.first;
            for (const Node &n : nodes) {
                if (!(n.has_key && !n.key.isEmpty() && n.is_block))
                    continue;
                if (use.order == 2) {
                    for (const Node &c : n.kids)
                        if (c.has_key && !c.key.isEmpty() && c.is_block) {
                            out.push_back({n.key, c.key, i, rel, {n.key, c.key}, true});
                            ++i;
                        }
                } else {
                    out.push_back({row, n.key, i, rel, {n.key}, true});
                    ++i;
                }
            }
            continue;
        }
        const QString &key = use.key;
        auto num = [&](const Node &c) -> OptInt {   // (the picture it shows: crimes count from 0 — pic_of)
            const QString v = py::strip(c.value);
            return py::isdigit(v) ? iconstrips::pic_of(&use, py::to_int(v)) : std::nullopt;
        };
        for (const Node &c : nodes)
            if (c.has_key && c.key == key && !c.is_block) {
                out.push_back({row, stem, num(c), rel, {}, false});
                break;
            }
        std::function<void(const script::Nodes &, const QStringList &)> walk = [&](const script::Nodes &ns,
                                                                                    const QStringList &path) {
            for (const Node &n : ns)
                if (n.is_block && n.has_key && !n.key.isEmpty()) {
                    for (const Node &c : n.kids)
                        if (c.has_key && c.key == key && !c.is_block) {
                            out.push_back({!path.isEmpty() ? path[0] : row, n.key, num(c), rel, path + QStringList{n.key},
                                           false});
                            break;
                        }
                    walk(n.kids, path + QStringList{n.key});
                }
        };
        walk(nodes, {});
    }
    return out;
}

AllBoardsPage::AllBoardsPage(moddata::Mod &mod_, StripChange strip_change_, QWidget *parent)
    : QWidget(parent), mod(mod_), strip_change(std::move(strip_change_))
{
    using widgets::label;
    use = nullptr;
    for (const iconstrips::Use &u : iconstrips::USES)
        if (u.id == u"crime") {
            use = &u;
            break;
        }
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 12, 8, 12);
    ll->setSpacing(6);
    ll->addWidget(label(u"Picture boards"_s, u"title"_s));
    ll->addWidget(label(u"Everything that shows a small picture in the game, as a board of those pictures. Pick a kind, "
                        "click a picture: its name and its picture are changed on the right; “Open on its page” leads to "
                        "where everything else of it is edited. (National focuses and national values have the full "
                        "board — drag, add, effects — in Game rules.)"_s,
                        u"faint"_s));
    auto *host = new QWidget();
    chips = new widgets::WrapRow(host, 6);
    for (const iconstrips::Use &u : iconstrips::USES) {
        auto *b = new QPushButton(u.title);
        b->setObjectName(u"chip"_s);
        b->setCheckable(true);
        connect(b, &QPushButton::clicked, this, [this, id = u.id] { set_use(id); });
        chips->addWidget(b);
        chip.insert(u.id, b);
    }
    ll->addWidget(host);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::WARN));
    warn->hide();
    ll->addWidget(warn);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setStyleSheet(u"QScrollArea { background: %1; border: 1px solid %2; }"_s.arg(theme::BG, theme::LINE));
    board = new Board();
    connect(board, &Board::picked, this, [this](const QString &k) { _pick(k); });
    board->plus = false;
    auto *inner = new QWidget();
    inner->setObjectName(u"sideInner"_s);
    auto *hl = new QVBoxLayout(inner);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(board);
    hl->addStretch(1);
    sc->setWidget(inner);
    ll->addWidget(sc, 1);
    split->addWidget(left);
    auto *sc2 = new QScrollArea();
    sc2->setWidgetResizable(true);
    sc2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc2->setObjectName(u"sideScroll"_s);
    detail = new QWidget();
    detail->setObjectName(u"sideInner"_s);
    dl = new QVBoxLayout(detail);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(0);
    sc2->setWidget(detail);
    split->addWidget(sc2);
    split->setStretchFactor(0, 7);
    split->setStretchFactor(1, 4);
    split->setSizes({1040, 620});
}

QString AllBoardsPage::name(const QString &key) const
{
    localisation::LocEdits *lc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr t;
    if (lc)
        t = lc->text(key);
    else if (const QString *x = mod.loc.get(key))
        t = *x;
    return t && !t->isEmpty() ? *t : _pretty(key);
}

void AllBoardsPage::set_use(const QString &uid)
{
    for (const iconstrips::Use &u : iconstrips::USES)
        if (u.id == uid) {
            use = &u;
            break;
        }
    sel.reset();
    _show();
}

void AllBoardsPage::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    strips = &iconstrips::strips_for(mod, *doc);
    _show();
}

void AllBoardsPage::_show()
{
    for (const auto &[k, b] : chip)
        b->setChecked(k == use->id);
    const iconstrips::Sprites sps = strips->sprites();
    const bool have = sps.contains(use->sprite);
    const std::vector<QPixmap> frames = have ? iconstrips::frames(*strips, use->sprite, 0) : std::vector<QPixmap>{};
    entries = strip_entries(mod, store, *use);
    OrderedMap<QString, std::vector<Cell>> rows;
    QStringList missing;
    for (const StripEntry &e : entries) {
        if (!rows.contains(e.row))
            rows.insert(e.row, {});
        std::optional<QPixmap> pm;
        if (e.num && *e.num && 1 <= *e.num && *e.num <= static_cast<long long>(frames.size()))
            pm = frames[static_cast<size_t>(*e.num - 1)];
        if (!pm)
            missing << e.key;
        rows.get(e.row)->push_back({e.key, name(e.key), pm,
                                    u"picture %1"_s.arg(e.num && *e.num ? QString::number(*e.num) : u"—"_s)});
    }
    if (sel && std::none_of(entries.begin(), entries.end(), [&](const StripEntry &e) { return e.key == *sel; }))
        sel.reset();
    std::vector<BRow> brows;
    for (const auto &[r, cells] : rows)
        brows.push_back({r, !r.isEmpty() ? OptStr(name(r)) : std::nullopt, cells});
    board->set_rows(brows, sel);
    if (!have)
        warn->setText(u"⚠ The strip %1 is not in the interface files of this mod or the game."_s.arg(use->sprite));
    else if (!missing.isEmpty())
        warn->setText(u"⚠ %1 of them have no picture (a number past the strip's last, %2, or none): %3."_s
                          .arg(missing.size())
                          .arg(frames.size())
                          .arg(missing.mid(0, 8).join(u", "_s) + (missing.size() > 8 ? u" …"_s : QString())));
    warn->setVisible(!missing.isEmpty() || !have);
    _fill();
}

void AllBoardsPage::_pick(const QString &key)
{
    if (!key.startsWith(u'@')) {
        sel = key;
        _show();
    }
}

void AllBoardsPage::_clear()
{
    while (dl->count()) {
        QLayoutItem *li = dl->takeAt(0);
        if (QWidget *w = li ? li->widget() : nullptr) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete li;
    }
}

void AllBoardsPage::_fill()
{
    using widgets::label;
    _clear();
    auto *s = new widgets::Section();
    const StripEntry *e = nullptr;
    if (sel)
        for (const StripEntry &x : entries)
            if (x.key == *sel) {
                e = &x;
                break;
            }
    if (!e) {
        s->lay->addWidget(label(u"%1 — %2. Click a picture."_s.arg(use->title).arg(entries.size()), u"sub"_s));
        if (!use->note.isEmpty())
            s->lay->addWidget(label(use->note, u"faint"_s));
        dl->addWidget(s);
        dl->addStretch(1);
        return;
    }
    const QString key = e->key, rel = e->rel;
    const OptInt num = e->num;
    const QStringList path = e->path;
    const bool by_order = e->by_order;
    const std::vector<QPixmap> frames = iconstrips::frames(*strips, use->sprite, 0);
    const long long nf = static_cast<long long>(frames.size());
    auto *head = new QHBoxLayout();
    auto *pic = new QLabel();
    pic->setFixedSize(84, 84);
    pic->setAlignment(Qt::AlignCenter);
    pic->setStyleSheet(u"QLabel { background: #1b1f26; border: 2px solid %1; border-radius: 6px; }"_s.arg(theme::GOLD));
    if (num && *num && 1 <= *num && *num <= nf) {
        const QPixmap &pm = frames[static_cast<size_t>(*num - 1)];
        const double k = std::min({72.0 / pm.width(), 72.0 / pm.height(), 2.0});
        pic->setPixmap(pm.scaled(int(pm.width() * k), int(pm.height() * k), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    head->addWidget(pic);
    auto *col = new QVBoxLayout();
    col->addWidget(label(u"Name in the game"_s, u"sub"_s, false));
    name_edit = new QLineEdit(name(key));
    connect(name_edit, &QLineEdit::editingFinished, this, [this, key] { _rename(key); });
    col->addWidget(name_edit);
    col->addWidget(label(u"%1 · %2"_s.arg(key, rel), u"faint"_s));
    head->addLayout(col, 1);
    s->lay->addLayout(head);
    auto *b = new QPushButton(u"Open on its page ▸  (%1)"_s.arg(use->where));
    connect(b, &QPushButton::clicked, this, [this, rel, path] { Q_EMIT placeRequested(rel, _pos(rel, path)); });
    s->lay->addWidget(b, 0, Qt::AlignLeft);
    dl->addWidget(s);
    s = new widgets::Section();
    QLabel *hb = label(u"Its picture"_s, u"sub"_s, false);
    QFont f(hb->font());
    f.setBold(true);
    hb->setFont(f);
    s->lay->addWidget(hb);
    const QString num_s = num ? QString::number(*num) : u"None"_s;
    if (by_order) {
        s->lay->addWidget(label(u"Picked by its place in the file: this one is picture %1 of the strip (%2 pictures). %3"_s
                                    .arg(num_s)
                                    .arg(nf)
                                    .arg(use->note),
                                u"faint"_s));
    } else {
        auto *row = new QHBoxLayout();
        row->addWidget(label(u"Another picture of the strip"_s, u"sub"_s, false));
        auto *pk = new iconstrips::IconPicker(*strips, use->sprite, num);
        connect(pk, &iconstrips::IconPicker::picked, this, [this, key, rel, path](long long nn) { _set_num(key, rel, path, nn); });
        row->addWidget(pk);
        row->addStretch(1);
        s->lay->addLayout(row);
    }
    const QString folder = moreviews::library_dir(u"pictures"_s);
    lib = new QComboBox();
    lib->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QStringList names;
    if (QFileInfo(folder).isDir())
        for (const QString &x : QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                       QDir::NoSort)) {
            const QString l = py::lower(x);
            if (std::any_of(moreviews::PIC_EXT.begin(), moreviews::PIC_EXT.end(), [&](const QString &ext) { return l.endsWith(ext); }))
                names << x;
        }
    std::sort(names.begin(), names.end());
    for (const QString &x : names)
        lib->addItem(x, pypath::join(folder, x));
    if (names.isEmpty())
        lib->addItem(u"(your library folder has no pictures yet)"_s, QVariant());
    s->lay->addWidget(label(u"A picture from your library folder (%1):"_s.arg(folder), u"faint"_s));
    s->lay->addWidget(lib);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    if (!by_order) {
        b = new QPushButton(u"Use it — as a new picture of the strip"_s);
        connect(b, &QPushButton::clicked, this, [this, key, rel, path, num] { _library(key, rel, path, num, true); });
        row->addWidget(b);
    }
    b = new QPushButton(u"Put it into picture %1"_s.arg(num_s));
    b->setToolTip(u"Picture %1 of the strip is replaced — everything that shows it changes with it"_s.arg(num_s));
    b->setEnabled(num && *num && *num <= nf);
    connect(b, &QPushButton::clicked, this, [this, key, rel, path, num] { _library(key, rel, path, num, false); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    msg = label(QString(), u"sub"_s);
    msg->setStyleSheet(u"color: #ff786e;"_s);
    msg->hide();
    s->lay->addWidget(msg);
    dl->addWidget(s);
    dl->addStretch(1);
}

long long AllBoardsPage::_pos(const QString &rel, const QStringList &path)
{
    if (path.isEmpty())
        return 0;
    const script::Nodes nodes = script::parse(store->text(rel));
    const Node *n = blocks::find(nodes, path);
    return n ? n->kstart : 0;
}

void AllBoardsPage::_rename(const QString &key)
{
    const QString t = py::split(name_edit->text()).join(u' ');
    if (!t.isEmpty() && t != name(key))
        Q_EMIT nameEdit(key, t);
}

QString AllBoardsPage::_with_num(const QString &rel, const QStringList &path, long long n)
{
    // The file's text with this entry picking picture n.
    const QString t = store->text(rel);
    const QString &k = use->key;
    const QString v = QString::number(iconstrips::value_of(use, n));   // (crimes: picture n is `icon = n - 1`)
    if (!path.isEmpty())
        return blocks::set_value(t, path, k, v);
    for (const Node &c : script::parse(t))
        if (c.has_key && c.key == k && !c.is_block)
            return t.left(c.vstart) + v + t.mid(c.vend);
    return t;
}

void AllBoardsPage::_set_num(const QString &key, const QString &rel, const QStringList &path, long long n)
{
    const QString nw = _with_num(rel, path, n);
    if (nw != store->text(rel))
        Q_EMIT scriptEdit(u"%1: picture %2"_s.arg(name(key)).arg(n), rel, nw);
}

void AllBoardsPage::_library(const QString &key, const QString &rel, const QStringList &path, OptInt num, bool as_new)
{
    const QString p = lib->currentData().toString();
    const std::optional<QImage> img = !p.isEmpty() ? imgio::read_image(p) : std::nullopt;
    const QString sprite = use->sprite;
    if (!img || img->isNull() || !strip_change) {
        msg->setText(u"Pick a picture of your library folder first (the folder has none, or this one can't be read)."_s);
        msg->show();
        return;
    }
    if (as_new) {
        iconstrips::Filler f;
        f.kind = u"image"_s;
        f.img = *img;
        auto got = strips->extend(sprite, {f});
        if (const QString *why = std::get_if<QString>(&got)) {
            msg->setText(*why);
            msg->show();
            return;
        }
        auto [files, texts] = std::get<0>(got);
        const long long n = strips->sprites().get(sprite)->frames + 1;
        texts.insert(rel, _with_num(rel, path, n));
        strip_change(u"%1: a new picture (%2)"_s.arg(name(key), pypath::basename(p)), files, texts);
    } else {
        const iconstrips::StripFiles files = strips->put(sprite, num.value_or(0), *img);
        if (!files.size()) {
            msg->setText(u"That picture can't be replaced (the strip's picture can't be read)."_s);
            msg->show();
            return;
        }
        strip_change(u"%1: picture %2 replaced (%3)"_s.arg(name(key), num ? QString::number(*num) : u"None"_s,
                                                           pypath::basename(p)),
                     files, {});
    }
}

}  // namespace iconboard
