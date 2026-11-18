// palette.py — see palette.h.
#include "ui/palette.h"
#include "ui/drawtext.h"
#include "core/pytext.h"
#include "ui/colorwheel.h"
#include "ui/textselect.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace palette {

const std::vector<Category> CATEGORIES = {
    {u"mapmode"_s, u"Map modes"_s, u"#b0b8c4"_s, {u"map mode:"_s}},
    {u"window"_s, u"Windows (flag studio, cultures, block builder …)"_s, u"#c58cf0"_s,
     {u"flag studio"_s, u"flag"_s, u"cultures window"_s, u"the cultures"_s, u"religions"_s, u"block builder"_s,
      u"parties window"_s, u"texts & preview"_s, u"nodes"_s, u"network"_s}},
    {u"tech"_s, u"Technology & inventions"_s, u"#6fb7ff"_s, {u"technology"_s, u"invention"_s}},
    {u"economy"_s, u"Economy & goods"_s, u"#e0b04a"_s, {u"economy"_s, u"good:"_s, u"output estimate"_s, u"production"_s}},
    {u"popinf"_s, u"Pop influences & pop types"_s, u"#f08a5d"_s, {u"pop influences"_s, u"pop type"_s}},
    {u"army"_s, u"Armies, navies & warfare"_s, u"#ef6b6b"_s,
     {u"armies"_s, u"navies"_s, u"warfare"_s, u"unit"_s, u"army"_s, u"navy"_s, u"war "_s}},
    {u"nation"_s, u"Nation & diplomacy"_s, u"#58c26a"_s, {u"nation"_s, u"diplomacy"_s, u"relations"_s, u"flag"_s}},
    {u"rules"_s, u"Game rules & politics"_s, u"#9aa8ff"_s,
     {u"game rules"_s, u"politics"_s, u"reform"_s, u"national value"_s, u"national focus"_s, u"rebels"_s, u"crimes"_s,
      u"modifier"_s}},
    {u"scripts"_s, u"Scripts (events, decisions)"_s, u"#5fd0c6"_s, {u"scripts"_s, u"event"_s, u"decision"_s, u"war goal"_s}},
    {u"map"_s, u"Map: population, provinces, states, drawing"_s, u"#d9c27a"_s,
     {u"population"_s, u"provinces"_s, u"states"_s, u"state"_s, u"drawing"_s, u"positions"_s, u"adjacenc"_s, u"canal"_s,
      u"map size"_s, u"terrain"_s, u"rivers"_s}},
    {u"tools"_s, u"Library, errors, import & other tools"_s, u"#8fa0b3"_s,
     {u"library"_s, u"errors"_s, u"changelog"_s, u"import"_s, u"submods"_s, u"checksum"_s, u"shortcuts"_s, u"loading"_s, u"menu"_s}},
    {u"general"_s, u"Apply, undo, search, mod"_s, QString(),
     {u"apply"_s, u"undo"_s, u"redo"_s, u"revert"_s, u"switch mod"_s, u"search"_s}},
};

QString cat_name(const QString &key)
{
    for (const Category &c : CATEGORIES)
        if (c.key == key)
            return c.name;
    return key;
}

QString category(const QString &title, const QString &where)
{
    // The category key of a palette entry (by its name and where it is).
    const QString t = py::lower(title), w = py::lower(where);
    const QString head = t + u" | "_s + py::split(w, u" — "_s)[0];
    for (const Category &c : CATEGORIES)
        for (const QString &x : c.words)
            if (head.contains(x))
                return c.key;
    return u"general"_s;
}

_CatDelegate::_CatDelegate(CommandPalette *p) : QStyledItemDelegate(p->lst), pal(p) {}

void _CatDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    // A row of the palette: its colour bar on the left and its category's name in its colour on the right.
    QStyledItemDelegate::paint(p, opt, idx);
    const QString key = idx.data(Qt::UserRole + 1).toString();
    const QString col = pal->colour_of(key);
    if (col.isEmpty())
        return;
    const QColor c(col);
    const QRect r = opt.rect;
    p->save();
    p->fillRect(QRectF(r.left(), r.top() + 2, 5, r.height() - 4), c);
    QFont f(opt.font);
    f.setPixelSize(11);
    f.setBold(true);
    p->setFont(f);
    const QString tag = py::split(py::split(cat_name(key), u" ("_s)[0], u":"_s)[0];
    const QFontMetrics fm = p->fontMetrics();
    const int tw = fm.horizontalAdvance(tag) + 14;
    const QRectF box(r.right() - tw - 6, r.top() + (r.height() - 18) / 2.0, tw, 18);
    const QColor bg(theme::PANEL);
    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(box.adjusted(-8, -1, 0, 1), 4, 4);   // (over the row's text end: the tag stays readable)
    p->setPen(QPen(c, 1.5));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(box, 9, 9);
    p->setPen(c);
    drawtext::draw(*p, box, Qt::AlignCenter, tag);
    p->restore();
}

const std::vector<QStringList> SAME = {
    {u"add"_s, u"new"_s, u"create"_s, u"make"_s, u"＋"_s},
    {u"delete"_s, u"remove"_s, u"take out"_s, u"drop"_s},
    {u"edit"_s, u"change"_s, u"set"_s, u"rename"_s},
    {u"save"_s, u"apply"_s, u"write"_s},
    {u"open"_s, u"show"_s, u"go to"_s},
    {u"picture"_s, u"image"_s, u"graphic"_s},
    {u"nation"_s, u"country"_s},
    {u"text"_s, u"localisation"_s, u"localization"_s, u"name"_s},
    {u"find"_s, u"search"_s, u"look for"_s},
    {u"copy"_s, u"duplicate"_s, u"clone"_s},
    {u"unit"_s, u"army"_s, u"troop"_s},
    {u"colour"_s, u"color"_s},
    {u"defines"_s, u"game settings"_s, u"defines.lua"_s},
    {u"country"_s, u"nation"_s, u"tag"_s}};
const QStringList ITEMS = {u"Technology: "_s, u"Invention: "_s, u"Good: "_s, u"Pop type: "_s, u"Nation: "_s, u"Unit: "_s};

QStringList forms(const QString &w)
{
    // A typed word and its other forms: army / armies, navy / navies, rebel / rebels, good / goods.
    QStringList out = {w};
    if (w.endsWith(u'y') && w.size() > 3)
        out << w.left(w.size() - 1) + u"ies"_s;
    if (w.endsWith(u"ies"_s) && w.size() > 4)
        out << w.left(w.size() - 3) + u"y"_s;
    if (w.endsWith(u's') && w.size() > 3)
        out << w.left(w.size() - 1);
    else if (w.size() > 2)
        out << w + u"s"_s;
    return out;
}

QStringList same_words(const QString &w)
{
    // (the word in its forms, then the words that mean the same)
    for (const QStringList &group : SAME)
        if (group.contains(w)) {
            QStringList out = {w};
            for (const QString &s : group)
                if (s != w)
                    out << s;
            return out;
        }
    return {w};
}

// ---------------------------------------------------------------------------------------------- the gold frame
Flash::Flash(QWidget *target) : QWidget(target->window())
{
    QWidget *top = target->window();
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    const QRect r = QRect(target->mapTo(top, target->rect().topLeft()), target->size()).adjusted(-3, -3, 3, 3);
    setGeometry(r);
    show();
    raise();
    _t = new QTimer(this);
    connect(_t, &QTimer::timeout, this, [this] { _step(); });
    _t->start(180);
}

void Flash::_step()
{
    _n += 1;
    update();
    if (_n >= 8) {
        _t->stop();
        deleteLater();
    }
}

void Flash::paintEvent(QPaintEvent *)
{
    if (_n % 2)
        return;
    QPainter p(this);
    p.setPen(QPen(QColor(10, 10, 10), 7));
    p.drawRect(rect().adjusted(3, 3, -4, -4));
    p.setPen(QPen(QColor(theme::GOLD), 4));
    p.drawRect(rect().adjusted(3, 3, -4, -4));
}

// ---------------------------------------------------------------------------------------------- the handles
_Handle::_Handle(CommandPalette *p, const QString &k) : QWidget(p), pal(p), kind(k)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself, as Python's textselect finds it)
    setCursor(kind == u"move" ? Qt::SizeAllCursor : Qt::SizeFDiagCursor);
    if (kind == u"move") {
        setFixedHeight(16);
        setToolTip(u"Drag to move the search"_s);
    } else {
        setFixedSize(18, 18);
        setToolTip(u"Drag to make the search bigger / smaller"_s);
    }
}

void _Handle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c(theme::GOLD_DIM);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    if (kind == u"move") {
        const int w = width();
        for (int i = -3; i < 4; ++i)                 // a row of grip dots in the middle
            p.drawEllipse(QRectF(w / 2.0 + i * 9 - 2, 6, 4, 4));
    } else {
        for (int i = 0; i < 3; ++i)                  // the usual corner triangle of dots
            for (int j = 0; j < i + 1; ++j)
                p.drawEllipse(QRectF(14 - j * 5 - 2, 14 - (i - j) * 5 - 2, 3.5, 3.5));
    }
}

void _Handle::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        _start = std::make_pair(e->globalPosition().toPoint(), pal->geometry());
}

void _Handle::mouseMoveEvent(QMouseEvent *e)
{
    if (!_start)
        return;
    const auto [p0, g] = *_start;
    const QPoint d = e->globalPosition().toPoint() - p0;
    const QRect par = pal->parentWidget()->rect();
    if (kind == u"move") {
        const int x = std::max(0, std::min(par.width() - 120, g.x() + d.x()));
        const int y = std::max(0, std::min(par.height() - 60, g.y() + d.y()));
        pal->move(x, y);
    } else {
        const int w = std::max(420, std::min(par.width() - g.x(), g.width() + d.x()));
        const int h = std::max(260, std::min(par.height() - g.y(), g.height() + d.y()));
        pal->resize(w, h);
        pal->_place_grip();
    }
}

void _Handle::mouseReleaseEvent(QMouseEvent *)
{
    if (_start) {
        _start.reset();
        pal->_save_geometry();
    }
}

// ---------------------------------------------------------------------------------------------- the palette
CommandPalette::CommandPalette(QWidget *w, QSettings *s, std::function<std::vector<Command>()> c)
    : QFrame(w), win(w), settings_(s), commands(std::move(c))
{
    setObjectName(u"palette"_s);
    setStyleSheet(u"QFrame#palette { background: %1; border: 2px solid %2; border-radius: 8px; }"_s.arg(theme::PANEL,
                                                                                                       theme::GOLD));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 4, 12, 10);
    lay->setSpacing(8);
    mover = new _Handle(this, u"move"_s);
    lay->addWidget(mover);
    grip = new _Handle(this, u"size"_s);
    edit = new QLineEdit();
    edit->setPlaceholderText(u"Go to … / do …   (type a few letters; ↑ ↓ and Enter; Esc closes)"_s);
    edit->setStyleSheet(u"QLineEdit { font-size: 16px; padding: 7px 10px; }"_s);
    connect(edit, &QLineEdit::textChanged, this, &CommandPalette::_fill);
    edit->installEventFilter(this);
    auto *top = new QHBoxLayout();
    top->addWidget(edit, 1);
    close_btn = new QPushButton(u"✕"_s);             // (the user, 2026-10-19: a way to just close it)
    close_btn->setToolTip(u"Close (Esc)"_s);
    close_btn->setFixedSize(38, 38);
    close_btn->setFocusPolicy(Qt::NoFocus);
    close_btn->setStyleSheet(u"QPushButton { font-size: 17px; font-weight: 700; padding: 0; }"_s);
    connect(close_btn, &QPushButton::clicked, this, &QWidget::hide);
    top->addWidget(close_btn);
    lay->addLayout(top);
    lst = new QListWidget();
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setStyleSheet(u"QListWidget { font-size: 14px; } QListWidget::item { padding: 5px 6px; }"_s);
    connect(lst, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) { _run(it); });
    connect(lst, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { _run(it); });
    colours = _load_colours();
    lst->setItemDelegate(new _CatDelegate(this));
    lay->addWidget(lst, 1);
    // the colours of the categories, chosen here (kept in the settings)
    col_panel = new QWidget();
    auto *cg = new QGridLayout(col_panel);
    cg->setContentsMargins(0, 4, 0, 4);
    cg->setHorizontalSpacing(8);
    cg->setVerticalSpacing(4);
    for (size_t r = 0; r < CATEGORIES.size(); ++r) {
        const Category &cat = CATEGORIES[r];
        const QString k = cat.key;
        auto *lab = new QLabel(cat.name);
        const QString now = colour_of(k);
        auto *btn = new colorwheel::ColorButton(!now.isEmpty() ? now : !cat.colour.isEmpty() ? cat.colour : u"#8a93a0"_s);
        auto *off = new QCheckBox(u"no colour"_s);
        off->setChecked(now.isEmpty());
        connect(btn, &colorwheel::ColorButton::changed, this, [this, k, off](const QString &c) {
            off->setChecked(false);
            _set_colour(k, c);
        });
        connect(off, &QCheckBox::toggled, this, [this, k, btn](bool on) { _set_colour(k, on ? QString() : btn->color); });
        const int row = static_cast<int>(r / 2), col = static_cast<int>(r % 2) * 3;
        cg->addWidget(lab, row, col);
        cg->addWidget(btn, row, col + 1);
        cg->addWidget(off, row, col + 2);
        _col_btns[k] = {btn, off};
    }
    col_panel->hide();
    lay->addWidget(col_panel);
    auto *foot = new QHBoxLayout();
    count = new QLabel(QString());
    count->setObjectName(u"faint"_s);
    foot->addWidget(count, 1);
    col_btn = new QPushButton(u"Colours ▾"_s);
    col_btn->setCheckable(true);
    col_btn->setFocusPolicy(Qt::NoFocus);            // (a click on it never takes the typing away)
    col_btn->setToolTip(u"Which category gets which colour in this list (or none)"_s);
    connect(col_btn, &QPushButton::toggled, this, &CommandPalette::_show_colours);
    foot->addWidget(col_btn);
    lay->addLayout(foot);
    hide();
}

QString CommandPalette::colour_of(const QString &key) const
{
    for (const auto &[k, c] : colours)
        if (k == key)
            return c;
    return QString();
}

// --- the categories' colours
std::vector<std::pair<QString, QString>> CommandPalette::_load_colours()
{
    std::vector<std::pair<QString, QString>> out;
    for (const Category &c : CATEGORIES)
        out.push_back({c.key, c.colour});
    if (QSettings *s = _settings()) {
        const QString raw = s->value(u"paletteColours"_s, QString()).toString();
        const QJsonDocument d = QJsonDocument::fromJson((raw.isEmpty() ? u"{}"_s : raw).toUtf8());
        if (d.isObject()) {
            const QJsonObject o = d.object();
            for (auto &[k, c] : out)
                if (o.contains(k) && o.value(k).isString())
                    c = o.value(k).toString();
        }
    }
    return out;
}

static QString json_str(const QString &s)          // json.dumps of a text (ensure_ascii)
{
    QString out = u"\""_s;
    for (const QChar ch : s) {
        const char16_t u = ch.unicode();
        if (u == u'"')
            out += u"\\\""_s;
        else if (u == u'\\')
            out += u"\\\\"_s;
        else if (u == u'\n')
            out += u"\\n"_s;
        else if (u == u'\r')
            out += u"\\r"_s;
        else if (u == u'\t')
            out += u"\\t"_s;
        else if (u == u'\b')
            out += u"\\b"_s;
        else if (u == u'\f')
            out += u"\\f"_s;
        else if (u < 0x20 || u > 0x7e)
            out += u"\\u%1"_s.arg(static_cast<int>(u), 4, 16, QChar(u'0'));
        else
            out += ch;
    }
    return out + u"\""_s;
}

void CommandPalette::_set_colour(const QString &key, const QString &col)
{
    bool found = false;
    for (auto &[k, c] : colours)
        if (k == key) {
            c = col;
            found = true;
        }
    if (!found)
        colours.push_back({key, col});
    if (QSettings *s = _settings()) {
        QStringList parts;
        for (const auto &[k, c] : colours)
            parts << json_str(k) + u": "_s + json_str(c);
        s->setValue(u"paletteColours"_s, u"{"_s + parts.join(u", "_s) + u"}"_s);
    }
    lst->viewport()->update();
}

void CommandPalette::_show_colours(bool on)
{
    // Unfold / fold the colours (the user, 2026-10-19: it has to fold up again).
    col_panel->setVisible(on);
    col_btn->setText(on ? u"Colours ▴  (fold)"_s : u"Colours ▾"_s);
    const int h = height() + (on ? 230 : -230);      // (the size the user gave it, plus / minus the colours)
    resize(width(), std::max(260, std::min(win->height() - y(), h)));
    _place_grip();
    edit->setFocus();
}

void CommandPalette::_place_grip()
{
    grip->move(width() - grip->width() - 3, height() - grip->height() - 3);
    grip->raise();
}

void CommandPalette::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    _place_grip();
}

void CommandPalette::_save_geometry()
{
    if (QSettings *s = _settings()) {
        const QRect g = geometry();
        s->setValue(u"paletteGeometry"_s, u"[%1, %2, %3, %4]"_s.arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
    }
}

std::optional<QRect> CommandPalette::_saved_geometry() const
{
    QSettings *s = _settings();
    if (!s)
        return std::nullopt;
    const QString raw = s->value(u"paletteGeometry"_s, QString()).toString();
    const QJsonDocument d = QJsonDocument::fromJson((raw.isEmpty() ? u"null"_s : raw).toUtf8());
    if (!d.isArray() || d.array().size() != 4)
        return std::nullopt;
    const QJsonArray a = d.array();
    int g[4];
    for (int i = 0; i < 4; ++i) {
        if (!a[i].isDouble())
            return std::nullopt;
        g[i] = static_cast<int>(a[i].toDouble());
    }
    const int W = win->width(), H = win->height();
    const int w = std::max(420, std::min(W, g[2])), h = std::max(260, std::min(H, g[3]));   // (still inside the window)
    return QRect(std::max(0, std::min(W - w, g[0])), std::max(0, std::min(H - h, g[1])), w, h);
}

void CommandPalette::open()
{
    _all = commands();
    if (const auto saved = _saved_geometry()) {
        setGeometry(*saved);                         // where it was put and how big it was made
    } else {
        const int w = std::min(760, win->width() - 80);
        const int h = std::min(560, win->height() - 160);
        setGeometry((win->width() - w) / 2, 90, w, h);
    }
    _place_grip();
    edit->clear();
    _fill(QString());
    show();
    raise();
    edit->setFocus();
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == edit && e->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent *>(e)->key();
        if (k == Qt::Key_Down || k == Qt::Key_Up) {
            const int n = lst->count();
            if (n) {
                const int r = lst->currentRow() + (k == Qt::Key_Down ? 1 : -1);
                lst->setCurrentRow(((r % n) + n) % n);
            }
            return true;
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            _run(lst->currentItem());
            return true;
        }
        if (k == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    if (obj == edit && e->type() == QEvent::FocusOut)   // (not while the colours are being chosen)
        QTimer::singleShot(150, this, [this] { _hide_if_left(); });
    return QFrame::eventFilter(obj, e);
}

void CommandPalette::_hide_if_left()
{
    // Closes when the focus went somewhere else — not to a part of the palette itself (the colours, their wheel).
    QWidget *f = QApplication::focusWidget();
    if (f && (f == this || isAncestorOf(f)))
        return;
    if (col_btn->isChecked() || QApplication::activePopupWidget())
        return;
    hide();
}

static bool starts_any(const QString &s, std::initializer_list<const char16_t *> ps)
{
    for (const char16_t *p : ps)
        if (s.startsWith(QString::fromUtf16(p)))
            return true;
    return false;
}

std::optional<std::pair<long long, long long>> CommandPalette::rank(QStringList words, const QString &title,
                                                                    const QString &where)
{
    // Best: the title (or a listed thing's own name) starts with the word; then a word of the title starts with it;
    // then it is inside the title; then only where it is. A form of the word (armies for army) counts as the word; a
    // word that only means the same (save for apply) counts less — so the exact word wins. Ties: the shorter title.
    const QString t = py::lower(title);
    QString hay = py::lower(title + u" "_s + where);
    QString name = t;
    bool listed = false;
    for (const QString &p : ITEMS)
        if (title.startsWith(p)) {
            name = t.mid(p.size());
            listed = true;
            break;
        }
    auto any_form_in = [](const QString &w, const QString &in) {
        for (const QString &s : same_words(w))
            for (const QString &f : forms(s))
                if (in.contains(f))
                    return true;
        return false;
    };
    if (listed) {
        // one thing of a long list (a tech, a good …): found by ITS OWN name only — the list's word ("technology",
        // "good") or the page it is on finds the page and its functions, not hundreds of things (the user,
        // 2026-10-19). That word still narrows when a word of the name is typed too: "tech steam".
        const QString kind = t.left(t.size() - name.size());
        QStringList own;
        for (const QString &w : words)
            if (any_form_in(w, name))
                own << w;
        if (own.isEmpty())
            return std::nullopt;
        for (const QString &w : words)
            if (!own.contains(w) && !any_form_in(w, kind))
                return std::nullopt;
        hay = name;
        words = own;
    }
    const QString main = py::split(name, u"  ("_s)[0];   // (an invention's "(of Steamers)" is not its name)
    const bool rail = starts_any(where, {u"Map — ", u"Nations — ", u"Rules — ", u"Tools — ", u"Search — "});
    long long score = 0;
    bool in_title = false;
    for (const QString &w : words) {
        std::optional<long long> best;
        const QStringList sw = same_words(w);
        for (qsizetype i = 0; i < sw.size(); ++i) {
            const QStringList fs = forms(sw[i]);
            for (qsizetype j = 0; j < fs.size(); ++j) {
                const QString &s = fs[j];
                if (!hay.contains(s))
                    continue;
                long long v = (t.startsWith(s) || name.startsWith(s))                         ? 0
                              : ((u" "_s + t).contains(u" "_s + s) || t.contains(u"("_s + s)) ? 1
                              : t.contains(s)                                                 ? 2
                                                                                              : 5;
                v += (i ? 3 : 0) + (j ? 1 : 0);
                if (listed && !main.contains(s))
                    v += 3;                          // (only in what it belongs to: Coke "(of Clean Coal)")
                in_title = in_title || t.contains(s);
                best = best ? std::min(*best, v) : v;
            }
        }
        if (!best)
            return std::nullopt;
        score += *best;
    }
    if (!words.isEmpty() && !in_title && !rail)
        return std::nullopt;                         // (only its page / its tooltip has the word)
    const QString q = words.join(u' ');
    if (!q.isEmpty() && (q == t || q == name || q == py::strip(t, u" ＋✕▸…"_s)))
        score -= 4;                                  // (exactly what was typed)
    if (listed) {
        score += 1;                                  // (one thing of a long list: a page / function first)
        if (!words.isEmpty() && !name.startsWith(words[0]))
            score += 1;                              // (… and matched only inside its name)
    }
    if (rail)
        score -= 1;                                  // (a page of the rail)
    return std::make_pair(score, static_cast<long long>(title.toUcs4().size()));
}

void CommandPalette::_fill(const QString &text)
{
    const QStringList words = py::split(py::lower(text));
    struct Row {
        std::pair<long long, long long> r;
        size_t i;
    };
    std::vector<Row> rows;
    for (size_t i = 0; i < _all.size(); ++i)
        if (const auto r = rank(words, _all[i].title, _all[i].where))
            rows.push_back({*r, i});
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.r != b.r ? a.r < b.r : a.i < b.i;
    });
    lst->clear();
    for (size_t k = 0; k < rows.size() && k < 300; ++k) {
        const Command &c = _all[rows[k].i];
        auto *it = new QListWidgetItem(!c.where.isEmpty() ? u"%1      —  %2"_s.arg(c.title, c.where) : c.title);
        it->setData(Qt::UserRole, static_cast<qulonglong>(rows[k].i));
        const QString cat = category(c.title, c.where);
        it->setData(Qt::UserRole + 1, cat);
        it->setToolTip(u"%1\n%2\n\n(%3)"_s.arg(c.title, c.where, cat_name(cat)));
        lst->addItem(it);
    }
    if (lst->count())
        lst->setCurrentRow(0);
    count->setText(u"%1 of %2 functions"_s.arg(rows.size()).arg(_all.size()));
}

void CommandPalette::_run(QListWidgetItem *it)
{
    if (!it)
        return;
    const size_t i = static_cast<size_t>(it->data(Qt::UserRole).toULongLong());
    const auto fn = _all[i].fn;
    hide();
    QWidget *target = fn ? fn() : nullptr;
    if (target && target->isVisible()) {
        QPointer<QWidget> t(target);
        QTimer::singleShot(120, this, [t] {
            if (t)
                new Flash(t);
        });
    }
}

}  // namespace palette
