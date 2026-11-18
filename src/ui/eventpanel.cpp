// eventpanel.py — see eventpanel.h
#include "ui/eventpanel.h"
#include "core/blockcode.h"
#include "core/cbcheck.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "map/imgio.h"
#include "map/moreviews_data.h"
#include "ui/cbpanel.h"
#include "ui/colsizes.h"
#include "ui/modpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QSizePolicy>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace eventpanel {

using events::Scope;
using events::View;
using refs::esc;

const QStringList COLS = {u"Title"_s, u"ID"_s, u"File"_s, u"MTTH"_s, u"×10 years"_s};
const std::vector<std::pair<QString, QString>> SHOWS = {
    {u"all"_s, u"Everything"_s},           {u"mtth"_s, u"Happen by themselves (MTTH)"_s},
    {u"fired"_s, u"Only when fired"_s},    {u"once"_s, u"Fire only once"_s},
    {u"major"_s, u"Major events"_s},       {u"province"_s, u"Province events"_s}};
// STATE: {True: ("✔", "#78e678"), False: ("✘", "#ff786e"), None: ("?", "#b8ab90"), "": ("•", "#8a8272")}
static std::pair<QString, QString> state_mark(bool has_state, Tri st)
{
    if (!has_state)
        return {u"•"_s, u"#8a8272"_s};
    if (!st)
        return {u"?"_s, u"#b8ab90"_s};
    return *st ? std::pair{u"✔"_s, u"#78e678"_s} : std::pair{u"✘"_s, u"#ff786e"_s};
}
const std::vector<Rgb> PALETTE = {{180, 222, 90},  {238, 104, 210}, {246, 140, 66},  {88, 196, 236},
                                  {120, 214, 110}, {206, 120, 250}, {240, 90, 90},   {90, 220, 190},
                                  {250, 230, 90},  {150, 170, 250}, {200, 150, 110}, {255, 160, 190}};
const Rgb NONE_RGB = {232, 196, 118};       // the map's colour for an entry without a category
const std::vector<Rgb> GRID = {
    {240, 90, 90},   {246, 140, 66},  {250, 190, 70},  {250, 230, 90},  {180, 222, 90},  {120, 214, 110},
    {60, 170, 90},   {90, 220, 190},  {88, 196, 236},  {70, 140, 230},  {150, 170, 250}, {120, 110, 230},
    {206, 120, 250}, {238, 104, 210}, {255, 160, 190}, {200, 60, 120},  {200, 150, 110}, {150, 100, 60},
    {232, 196, 118}, {230, 230, 220}, {170, 170, 170}, {110, 110, 120}, {140, 160, 120}, {120, 150, 170}};
// the chosen nation's rows: its tag is in the entry (1), it can happen to it (2), both (3)
static Rgb mark_rgb(int k)
{
    return k == 1 ? Rgb{80, 150, 255} : k == 2 ? Rgb{90, 205, 100} : Rgb{60, 205, 215};
}
static QColor qc(const Rgb &c, int alpha = 255)
{
    return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c), alpha);
}
static QString rgb_css(const Rgb &c)
{
    return u"rgb(%1,%2,%3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}
static QString hex6(const Rgb &c)
{
    return u"#%1%2%3"_s.arg(std::get<0>(c), 2, 16, QChar(u'0'))
        .arg(std::get<1>(c), 2, 16, QChar(u'0'))
        .arg(std::get<2>(c), 2, 16, QChar(u'0'));
}
static Tri or3(const std::vector<Tri> &vals)
{
    Tri out = false;
    for (const Tri &v : vals) {
        if (v == Tri(true))
            return true;
        if (!v)
            out = std::nullopt;
    }
    return out;
}
static bool truthy(const Tri &v) { return v.value_or(false); }
static QString rel_tail(const QString &rel) { return py::split(rel, u"/"_s, 1).last(); }   // rel.split("/", 1)[-1]
static QString uid_of(const Entry &e) { return e.uid.value_or(QString()); }
static QString rel_of(const Entry &e) { return e.rel.value_or(QString()); }
// (the line around a place: from the line's start to its end — what a "go to" lights)
static std::pair<qsizetype, qsizetype> line_span(const QString &t, qsizetype pos)
{
    const qsizetype end = t.indexOf(u'\n', pos);
    return {py::rfind(t, u'\n', pos) + 1, end < 0 ? t.size() : end};
}

// json.dumps (ensure_ascii, Python's default separators)
static QString json_str(const QString &s)
{
    QString out = u"\""_s;
    for (QChar ch : s) {
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
            out += u"\\u%1"_s.arg(int(u), 4, 16, QChar(u'0'));
        else
            out += ch;
    }
    return out + u"\""_s;
}

std::optional<std::pair<qsizetype, qsizetype>> _changed_range(const std::vector<refs::Ref> &old,
                                                              const std::vector<refs::Ref> &fresh, qsizetype old_len,
                                                              qsizetype new_len)
{
    const qsizetype no = qsizetype(old.size()), nn = qsizetype(fresh.size());
    auto same4 = [](const refs::Ref &a, const refs::Ref &b) {   // [:4] and [5]
        return a.pos == b.pos && a.length == b.length && a.kind == b.kind && a.target == b.target && a.ok == b.ok;
    };
    qsizetype i = 0;
    while (i < no && i < nn && same4(old[size_t(i)], fresh[size_t(i)]))
        ++i;
    qsizetype j = 0;
    while (j < no - i && j < nn - i) {
        const refs::Ref &a = old[size_t(no - 1 - j)], &b = fresh[size_t(nn - 1 - j)];
        if (!(old_len - a.pos == new_len - b.pos && a.length == b.length && a.kind == b.kind &&
              a.target == b.target && a.ok == b.ok))
            break;
        ++j;
    }
    if (i >= nn - j && i >= no - j)
        return std::nullopt;
    const qsizetype a = i < nn - j ? fresh[size_t(i)].pos : (i < no ? old[size_t(i)].pos : new_len);
    const qsizetype b = nn - 1 - j >= i ? fresh[size_t(nn - 1 - j)].pos + fresh[size_t(nn - 1 - j)].length : a;
    return std::pair{std::max<qsizetype>(0, std::min(a, new_len)),
                     std::max<qsizetype>(0, std::min(std::max(a, b), new_len))};
}

void prov_ids(View nodes, std::set<long long> &out)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        const script::Node &c = nodes[i];
        if (!c.has_key || c.key.isEmpty())
            continue;
        if (c.is_block) {
            if (py::isdigit(c.key))
                out.insert(py::to_int(c.key));
            prov_ids(View(c.kids), out);
        } else if ((c.key == u"province_id" || c.key == u"capital") && py::isdigit(py::strip(c.value))) {
            out.insert(py::to_int(py::strip(c.value)));
        }
    }
}

std::set<long long> prov_ids(View nodes)
{
    std::set<long long> out;
    prov_ids(nodes, out);
    return out;
}

QIcon cats_icon(const std::vector<Rgb> &rgbs, int size)
{
    // A small square in the categories' colours (side by side), hollow without any.
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (!rgbs.empty()) {
        const double w = double(size) / double(rgbs.size());
        for (size_t i = 0; i < rgbs.size(); ++i)
            p.fillRect(QRect(int(double(i) * w), 0, int(std::ceil(w)), size), qc(rgbs[i]));
        p.setPen(QPen(QColor(16, 12, 8), 1));
        p.drawRect(0, 0, size - 1, size - 1);
    } else {
        p.setPen(QPen(QColor(110, 104, 92), 1));
        p.drawRect(1, 1, size - 3, size - 3);
    }
    p.end();
    return QIcon(pm);
}

// the row's data: UserRole = [kind, key, uid, group, "1" when there is a group]; UserRole + 3 = the Entry
static void set_row_data(QTreeWidgetItem *it, QChar kind, const QString &key, Entry *e, const OptStr &group)
{
    it->setData(0, Qt::UserRole, QStringList{QString(kind), key, e ? uid_of(*e) : QString(), group.value_or(QString()),
                                             group ? u"1"_s : QString()});
    it->setData(0, Qt::UserRole + 3, QVariant::fromValue(quintptr(e)));
}

RowData row_data(const QTreeWidgetItem *it)
{
    RowData d;
    if (!it)
        return d;
    const QStringList l = it->data(0, Qt::UserRole).toStringList();
    if (l.size() < 5)
        return d;
    d.kind = l[0].isEmpty() ? QChar() : l[0][0];
    d.key = l[1];
    d.e = reinterpret_cast<Entry *>(it->data(0, Qt::UserRole + 3).value<quintptr>());
    if (!l[4].isEmpty())
        d.group = l[3];
    return d;
}

ScopeKey scope_key(const Scope &sc)
{
    if (sc.kind == u'c')
        return {u"c"_s, sc.tag.value_or(QString())};
    return {u"p"_s, sc.pid ? QString::number(*sc.pid) : QString()};
}

bool SortKey::operator<(const SortKey &o) const
{
    if (a != o.a)
        return a < o.a;
    if (n1 != o.n1)
        return n1 < o.n1;
    if (n2 != o.n2)
        return n2 < o.n2;
    return s < o.s;
}

// ---------------------------------------------------------------------------------------------- CatTree, CatButton
CatTree::CatTree()
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}

void CatTree::startDrag(Qt::DropActions)
{
    QTreeWidget::startDrag(Qt::CopyAction);  // never a move: the rows stay where they are, the categories change
}

QTreeWidgetItem *CatTree::_target(const QPoint &pos)
{
    QTreeWidgetItem *it = itemAt(pos);
    while (it && it->parent())
        it = it->parent();
    return it && row_data(it).kind == u'c' ? it : nullptr;
}

void CatTree::_light(QTreeWidgetItem *it)
{
    if (it == _hl)
        return;
    if (_hl)
        for (int c = 0; c < columnCount(); ++c)
            _hl->setBackground(c, _hl->data(1, Qt::UserRole).value<QBrush>());
    _hl = it;
    if (it)
        for (int c = 0; c < columnCount(); ++c)
            it->setBackground(c, QBrush(QColor(244, 234, 210, 110)));
}

void CatTree::forget() { _hl = nullptr; }

void CatTree::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->source() == this) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void CatTree::dragMoveEvent(QDragMoveEvent *e)
{
    QTreeWidgetItem *t = e->source() == this ? _target(e->position().toPoint()) : nullptr;
    _light(t);
    if (t) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void CatTree::dragLeaveEvent(QDragLeaveEvent *e)
{
    _light(nullptr);
    e->accept();
}

void CatTree::dropEvent(QDropEvent *e)
{
    QTreeWidgetItem *t = e->source() == this ? _target(e->position().toPoint()) : nullptr;
    _light(nullptr);
    e->setDropAction(Qt::CopyAction);
    e->accept();
    if (t) {
        const QString name = row_data(t).key;
        const bool copy = bool(e->modifiers() & Qt::ControlModifier);
        QTimer::singleShot(0, this, [this, name, copy] { Q_EMIT dropped(name, copy); });   // after the drag has finished
    }
}

void CatTree::keyPressEvent(QKeyEvent *e)
{
    const int k = e->key();
    if (keyHook && 0x30 <= k && k <= 0x39 && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        keyHook(k - 0x30);
        return;
    }
    QTreeWidget::keyPressEvent(e);
}

CatButton::CatButton(const QString &text) : QPushButton(text) { setAcceptDrops(true); }

void CatButton::dragEnterEvent(QDragEnterEvent *e)
{
    if (qobject_cast<CatTree *>(e->source())) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        setDown(true);
    } else {
        e->ignore();
    }
}

void CatButton::dragLeaveEvent(QDragLeaveEvent *) { setDown(false); }

void CatButton::dropEvent(QDropEvent *e)
{
    setDown(false);
    e->setDropAction(Qt::CopyAction);
    e->accept();
    QTimer::singleShot(0, this, [this] { Q_EMIT dropped(); });
}

// ---------------------------------------------------------------------------------------------- the panel
static QPushButton *tool_button(const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text);
    b->setToolTip(tip);
    b->setStyleSheet(u"QPushButton { padding: 6px 10px; color: %1; font-weight: 600; } "
                     u"QPushButton:disabled { color: %2; }"_s.arg(theme::GOLD, theme::INK_FAINT));
    return b;
}

static QLabel *rich_label(const QString &obj = {})
{
    auto *l = new QLabel();
    if (!obj.isEmpty())
        l->setObjectName(obj);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

EventPanel::EventPanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    _reparse_timer = new QTimer(this);
    _reparse_timer->setSingleShot(true);
    _reparse_timer->setInterval(700);
    connect(_reparse_timer, &QTimer::timeout, this, &EventPanel::_reparse_due);
    _load_cats();
    _timer = new QTimer(this);
    _timer->setInterval(0);
    connect(_timer, &QTimer::timeout, this, &EventPanel::_work);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    // --- the editor's small window (map / relations) when it is put into the side panel
    mini_host = new QWidget();
    auto *mh = new QVBoxLayout(mini_host);
    mh->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(mini_host);
    auto *s = new widgets::Section();
    // --- the builders: a new event / decision, the blocks, the network
    auto *tools = new QWidget();
    auto *tr = new widgets::WrapRow(tools, 6);
    const std::vector<std::tuple<QString, QString, QString>> TOOLS = {
        {u"new_event"_s, u"＋ New event"_s,
         u"A new event with every part ready to fill in: its number (checked: no other event has it), title, text, "
         u"conditions, how often, effects, choices"_s},
        {u"new_decision"_s, u"＋ New decision"_s, u"A new decision with every part ready to fill in"_s},
        {u"blocks"_s, u"🧩 Blocks"_s,
         u"The event / decision picked, built from blocks — every condition and effect a sentence with its values to "
         u"pick; the script follows"_s},
        {u"nodes"_s, u"⧉ Nodes"_s,
         u"The event / decision picked as a graph of nodes with wires (after ComfyUI): every line a node of its own "
         u"with its value to pick, coloured inputs for conditions and effects, wires say what goes into what — "
         u"another way to build the same script as the blocks"_s},
        {u"texts"_s, u"✎ Texts && preview"_s,
         u"The title, description and option texts of the event / decision picked — with the game's colour codes, "
         u"and shown as the game shows them: its window, picture, fonts, the hover text of a decision"_s},
        {u"network"_s, u"🕸 Network"_s,
         u"Every event and decision as a box, with coloured lines for what calls what and what shares flags, "
         u"modifiers, numbers, nations, files"_s},
        {u"ids"_s, u"🔢 IDs"_s,
         u"The mod's event-id system (a block of ids per file, one running number, or tidy each file) written into "
         u"the mod and followed by every new event — and Re-ID: the events numbered anew by it, every call of them "
         u"following"_s}};
    for (const auto &[key, text, tip] : TOOLS) {
        QPushButton *b = tool_button(text, tip);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { Q_EMIT toolRequested(k); });
        tr->addWidget(b);
        if (key == u"blocks") {
            blocks_btn = b;
            _blocks_tip = tip;
        }
    }
    s->lay->addWidget(tools);
    // --- what to list
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    const std::vector<std::pair<QString, QString>> KINDS_ = {{u"event"_s, u"Events"_s},
                                                             {u"decision"_s, u"Decisions"_s},
                                                             {u"both"_s, u"Both"_s},
                                                             {u"mods"_s, u"Modifiers"_s},
                                                             {u"cbs"_s, u"War goals"_s}};
    for (const auto &[k, t] : KINDS_) {
        auto *b = new widgets::BadgeButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == u"event");
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setStyleSheet(u"padding-left: 2px; padding-right: 2px;"_s);
        connect(b, &QPushButton::clicked, this, [this] { _kind_changed(); });
        grp->addButton(b);
        row->addWidget(b, int(t.size()));
        kind_btn.insert(k, b);
    }
    s->lay->addLayout(row);
    filters = new QWidget();
    auto *fl = new QVBoxLayout(filters);
    fl->setContentsMargins(0, 0, 0, 0);
    search = new QLineEdit();
    search->setPlaceholderText(u"Find by name, id, key, file or category…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this] { _fill_list(); });
    fl->addWidget(search);
    row = new QHBoxLayout();
    row->addWidget(widgets::label(u"Show"_s, u"sub"_s, false));
    show_box = new QComboBox();
    for (const auto &[k, t] : SHOWS)
        show_box->addItem(t, k);
    show_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(show_box, &QComboBox::currentIndexChanged, this, [this] { _fill_list(); });
    row->addWidget(show_box, 1);
    row->addWidget(widgets::label(u"Category"_s, u"sub"_s, false));
    cat_box = new QComboBox();
    cat_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(cat_box, &QComboBox::currentIndexChanged, this, [this] { _fill_list(); });
    row->addWidget(cat_box, 1);
    fl->addLayout(row);
    row = new QHBoxLayout();
    nation_lbl = widgets::label(QString(), u"sub"_s);
    row->addWidget(nation_lbl, 1);
    clear_nation = new QPushButton(u"✕ Any nation"_s);
    connect(clear_nation, &QPushButton::clicked, this, [this] { set_nation(std::nullopt); });
    row->addWidget(clear_nation, 0, Qt::AlignTop);
    fl->addLayout(row);
    row = new QHBoxLayout();
    only_nation = new QCheckBox(u"Only what can happen to it"_s);
    connect(only_nation, &QCheckBox::toggled, this, [this] { _fill_list(); });
    row->addWidget(only_nation);
    only_sym = new QCheckBox(u"Only where the word at the cursor is"_s);
    only_sym->setToolTip(u"The events / decisions that mention what the editor's cursor is on (or what you "
                         u"right-clicked on the map) — they are lit in the list anyway"_s);
    connect(only_sym, &QCheckBox::toggled, this, [this] { _fill_list(); });
    row->addWidget(only_sym);
    row->addStretch(1);
    auto *b = new QPushButton(u"Open all"_s);
    connect(b, &QPushButton::clicked, this, [this] { _open_all(true); });
    row->addWidget(b);
    b = new QPushButton(u"Close all"_s);
    connect(b, &QPushButton::clicked, this, [this] { _open_all(false); });
    row->addWidget(b);
    fl->addLayout(row);
    progress = widgets::label(QString(), u"faint"_s);
    fl->addWidget(progress);
    s->lay->addWidget(filters);
    lay->addWidget(s);
    // --- the list: files (or categories), each opened to what's in it
    s = list_sec = new widgets::Section();
    row = new QHBoxLayout();
    row->setSpacing(0);
    row->addWidget(widgets::label(u"Group by   "_s, u"sub"_s, false));
    grp = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"file"_s, u"File"_s}, {u"cat"_s, u"Category"_s}}) {
        auto *gb = new QPushButton(t);
        gb->setObjectName(u"seg"_s);
        gb->setCheckable(true);
        gb->setChecked(k == u"file");
        const QString kk = k;
        connect(gb, &QPushButton::clicked, this, [this, kk] { _set_group(kk); });
        grp->addButton(gb);
        row->addWidget(gb, 1);
        group_btn.insert(k, gb);
    }
    s->lay->addLayout(row);
    // the categories: click one (or press its number) to put it on / take it off the selected rows
    bar = new QWidget();
    bar_lay = new widgets::WrapRow(bar, 6);
    s->lay->addWidget(bar);
    new_row = new QWidget();
    auto *nv = new QVBoxLayout(new_row);
    nv->setContentsMargins(0, 0, 0, 0);
    nv->setSpacing(2);
    auto *nh = new QHBoxLayout();
    new_edit = new QLineEdit();
    new_edit->setPlaceholderText(u"Name of the new category"_s);
    connect(new_edit, &QLineEdit::returnPressed, this, &EventPanel::_new_done);
    connect(new_edit, &QLineEdit::textChanged, this, [this] { new_warn->setText(QString()); });
    nh->addWidget(new_edit, 1);
    new_ok = new QPushButton(u"Make it"_s);
    connect(new_ok, &QPushButton::clicked, this, &EventPanel::_new_done);
    nh->addWidget(new_ok);
    b = new QPushButton(u"Cancel"_s);
    connect(b, &QPushButton::clicked, this, [this] { new_row->setVisible(false); });
    nh->addWidget(b);
    nv->addLayout(nh);
    new_warn = widgets::label(QString(), u"sub"_s);
    new_warn->setStyleSheet(u"color: #ff786e;"_s);
    nv->addWidget(new_warn);
    new_row->setVisible(false);
    s->lay->addWidget(new_row);
    bar_msg = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(bar_msg);
    _say(QString());
    count = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(count);
    mark_lbl = rich_label(u"sub"_s);
    mark_lbl->setVisible(false);
    s->lay->addWidget(mark_lbl);
    lst = new CatTree();
    lst->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(lst, &QWidget::customContextMenuRequested, this, &EventPanel::_tree_menu);
    lst->keyHook = [this](int n) { _key(n); };
    connect(lst, &CatTree::dropped, this, [this](const QString &t, bool copy) { _drop_on(t, copy, _drag_keys()); });
    lst->setColumnCount(int(COLS.size()));
    lst->setHeaderLabels(COLS);
    lst->setRootIsDecorated(true);
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setMinimumHeight(330);
    lst->setSortingEnabled(false);           // sorted here (files, then what's in them)
    QHeaderView *hd = lst->header();
    colsizes::make_resizable(lst, u"eventList3"_s, {std::nullopt, 86, 110, 96});   // drag the lines between the names
    hd->setSortIndicatorShown(true);
    hd->setSectionsClickable(true);
    hd->setSortIndicator(2, Qt::AscendingOrder);
    connect(hd, &QHeaderView::sectionClicked, this, &EventPanel::_header_clicked);
    connect(lst, &QTreeWidget::currentItemChanged, this, &EventPanel::_picked);
    connect(lst, &QTreeWidget::itemSelectionChanged, this, &EventPanel::_style_bar);
    connect(lst, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) { _expanded(it, true); });
    connect(lst, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *it) { _expanded(it, false); });
    s->lay->addWidget(lst);
    // why the row picked is marked ⚠: the reason, and the way to the line
    row_prob = rich_label();
    row_prob->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(row_prob, &QLabel::linkActivated, this, &EventPanel::_link);
    row_prob->hide();
    s->lay->addWidget(row_prob);
    row_jump = new QPushButton(u"▸ Jump to the error"_s);
    row_jump->setCursor(Qt::PointingHandCursor);
    row_jump->setToolTip(u"The editor goes to the line of the problem (again: the next one)"_s);
    connect(row_jump, &QPushButton::clicked, this, &EventPanel::_jump_problem);
    row_jump->hide();
    s->lay->addWidget(row_jump);
    lay->addWidget(s);
    // --- what the editor's cursor is on: what it is, where it is set / checked / used
    s = sym_sec = new widgets::Section();
    sym_head = new QLabel();
    sym_head->setWordWrap(true);
    s->lay->addWidget(sym_head);
    sym_body = rich_label(u"sub"_s);
    sym_body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(sym_body, &QLabel::linkActivated, this, &EventPanel::_link);
    s->lay->addWidget(sym_body);
    sym_sum = rich_label(u"sub"_s);
    s->lay->addWidget(sym_sum);
    sym_tree = new relview::RefTree(nullptr, u"symTree"_s);
    sym_tree->setMinimumHeight(150);
    connect(sym_tree, &relview::RefTree::goTo, this, &EventPanel::_tree_go);
    s->lay->addWidget(sym_tree);
    // a modifier at the cursor: its editor right here
    sym_form = new modpanel::ModifierForm(mod);
    sym_form->setVisible(false);
    s->lay->addWidget(sym_form);
    s->setVisible(false);
    lay->addWidget(s);
    // --- the one selected
    s = detail = new widgets::Section();
    detail_head = new QLabel();
    detail_head->setWordWrap(true);
    s->lay->addWidget(detail_head);
    title = widgets::label(u"Pick a file, an event or a decision"_s, u"title"_s);
    s->lay->addWidget(title);
    meta = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(meta);
    // the information line "#literacy event": this one sets literacy on day 1 (the Literacy map replays it)
    lit_box = new QCheckBox(u"Literacy event — sets literacy on day 1"_s);
    lit_box->setToolTip(u"Ticked: the line “%1” stands above it in the file (a comment — the game ignores it). The "
                        u"map mode “Literacy at the start” counts in the events and decisions that carry the line, "
                        u"and nothing else: without one it shows what the country files say."_s.arg(events::LIT_LINE));
    connect(lit_box, &QCheckBox::toggled, this, &EventPanel::_lit_toggled);
    lit_box->hide();
    s->lay->addWidget(lit_box);
    pic = new QLabel();                      // the picture the event / decision shows in the game
    pic->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pic->hide();
    s->lay->addWidget(pic);
    pic_note = widgets::label(QString(), u"faint"_s);
    pic_note->hide();
    s->lay->addWidget(pic_note);
    pic_box = new QComboBox();
    pic_box->setEditable(true);
    pic_box->setInsertPolicy(QComboBox::NoInsert);
    pic_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    pic_box->lineEdit()->setPlaceholderText(u"Picture — pick one of the mod's (add your own in Library → Pictures)"_s);
    pic_box->setToolTip(u"The picture this event / decision shows. Your own pictures: Library → Pictures."_s);
    connect(pic_box, &QComboBox::activated, this, [this] { _set_picture(py::strip(pic_box->currentText())); });
    pic_box->hide();
    s->lay->addWidget(pic_box);
    // its categories: chips (✕ takes one off), a box to add one (a new name makes a new category)
    cat_host = new QWidget();
    auto *cl = new QVBoxLayout(cat_host);
    cl->setContentsMargins(0, 4, 0, 4);
    cl->setSpacing(6);
    cl->addWidget(widgets::label(u"Its categories (✕ takes one off; to add one, use the categories above the list). "
                                 u"Kept in the file as a “#category:” comment right above it — a file's as “#file "
                                 u"category:” at its top — written on Apply."_s,
                                 u"faint"_s));
    chips = new QWidget();
    chip_lay = new widgets::WrapRow(chips, 6);
    cl->addWidget(chips);
    s->lay->addWidget(cat_host);
    body = rich_label(u"sub"_s);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(body, &QLabel::linkActivated, this, &EventPanel::_link);
    s->lay->addWidget(body);
    lay->addWidget(s);
    legend = widgets::label(QString(), u"faint"_s);
    auto *ls = legend_sec = new widgets::Section();
    ls->lay->addWidget(legend);
    lay->addWidget(ls);
    // --- the modifiers (the "Modifiers" button)
    mod_view = new modpanel::ModifierView(mod);
    mod_view->setVisible(false);
    connect(mod_view, &modpanel::ModifierView::entryPicked, this, &EventPanel::_show_entry);
    connect(mod_view, &modpanel::ModifierView::modPicked, this, &EventPanel::_open_mod);
    connect(sym_form, &modpanel::ModifierForm::modEdit, mod_view, &modpanel::ModifierView::modEdit);
    connect(sym_form, &modpanel::ModifierForm::nameEdit, mod_view, &modpanel::ModifierView::nameEdit);
    connect(sym_form, &modpanel::ModifierForm::entryPicked, this, &EventPanel::_show_entry);
    connect(lst, &QTreeWidget::itemDoubleClicked, this, &EventPanel::_list_double);
    lay->addWidget(mod_view);
    // --- the war goals (the "War goals" button): common/cb_types.txt, one war goal at a time
    cb_view = new cbpanel::WarGoalView(mod);
    cb_view->setVisible(false);
    cb_view->conds_html = [this](const script::Nodes &n) { return _conds_html(View(n), std::nullopt); };
    cb_view->effects_html = [this](const script::Nodes &n) { return _effects_html(View(n)); };
    connect(cb_view, &cbpanel::WarGoalView::goalPicked, this, [this](const QString &k, bool t) { _open_cb(k, t); });
    connect(cb_view, &cbpanel::WarGoalView::linkActivated, this, &EventPanel::_link);
    cb_view->detail_sec->setVisible(false);
    lay->insertWidget(lay->indexOf(sym_sec), cb_view);                    // its list: where the files' list is
    lay->insertWidget(lay->indexOf(sym_sec) + 1, cb_view->detail_sec);   // the one picked: under the word
    lay->addStretch(1);
    for (const auto &[keys, fn] : std::vector<std::pair<QString, void (EventPanel::*)()>>{
             {u"Alt+Left"_s, &EventPanel::go_back}, {u"Alt+Right"_s, &EventPanel::go_forward}}) {
        auto *sc = new QShortcut(QKeySequence(keys), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    }
    qApp->installEventFilter(this);          // the mouse's back / forward buttons
    _show_nation();
}

EventPanel::~EventPanel()
{
    qApp->removeEventFilter(this);
    // (the document outlives the panel only in tests: its listener goes with the panel)
}

// ---------------------------------------------------------------------------------------------- categories
QString EventPanel::_settings_key() const
{
    return u"eventCats/%1"_s.arg(mod.display_name && !mod.display_name->isEmpty() ? *mod.display_name : u"mod"_s);
}

void EventPanel::_load_cats()
{
    // The user's categories in their order with their colours (kept per mod in the settings).
    _colors.clear();
    _order.clear();
    auto st = settings_::settings();
    const QString v = st->value(_settings_key(), QString()).toString();
    std::vector<std::pair<QString, Rgb>> rows;
    auto rgb_of = [](const QJsonArray &c, Rgb &out) {
        if (c.size() != 3)
            return false;
        out = Rgb{c[0].toInt(), c[1].toInt(), c[2].toInt()};
        return true;
    };
    if (!v.isEmpty()) {
        const QJsonDocument d = QJsonDocument::fromJson(v.toUtf8());
        if (!d.isArray())
            return;
        for (const QJsonValue &r : d.array()) {
            const QJsonArray a = r.toArray();
            Rgb c;
            if (a.size() != 2 || !a[0].isString() || !rgb_of(a[1].toArray(), c))
                return;
            rows.emplace_back(a[0].toString(), c);
        }
    }
    if (rows.empty()) {                      // the first version kept only the colours
        const QString old = st->value(u"eventCatColors/%1"_s.arg(mod.display_name && !mod.display_name->isEmpty()
                                                                     ? *mod.display_name : u"mod"_s),
                                      QString()).toString();
        if (!old.isEmpty()) {
            const QJsonDocument d = QJsonDocument::fromJson(old.toUtf8());
            if (!d.isObject())
                return;
            const QJsonObject o = d.object();
            for (auto it = o.begin(); it != o.end(); ++it) {
                Rgb c;
                if (!rgb_of(it.value().toArray(), c))
                    return;
                rows.emplace_back(it.key(), c);
            }
            std::stable_sort(rows.begin(), rows.end(),
                             [](const auto &a, const auto &b) { return py::lower(a.first) < py::lower(b.first); });
        }
    }
    for (const auto &[n, c] : rows) {
        _colors.insert(n, c);
        _order.append(n);
    }
}

void EventPanel::_save_cats()
{
    QStringList rows;
    for (const QString &n : QStringList(_order)) {
        const Rgb c = color(n);
        rows.append(u"[%1, [%2, %3, %4]]"_s.arg(json_str(n)).arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c)));
    }
    settings_::settings()->setValue(_settings_key(), u"["_s + rows.join(u", "_s) + u"]"_s);
}

Rgb EventPanel::color(const QString &name)
{
    if (!_colors.contains(name)) {
        std::set<Rgb> used;
        for (const auto &[_n, c] : _colors)
            used.insert(c);
        std::optional<Rgb> free;
        for (const Rgb &c : PALETTE)
            if (!used.count(c)) {
                free = c;
                break;
            }
        _colors.insert(name, free ? *free : PALETTE[size_t(_colors.size()) % PALETTE.size()]);
    }
    return *_colors.get(name);
}

static events::EventCats *cats_store(document::Document *doc)
{
    if (!doc || !doc->maps.contains(u"event_cats"_s))
        return nullptr;
    return &doc->map<events::EventCats>(u"event_cats"_s);
}

static modifiers::ModifierEdits *mods_store(document::Document *doc)
{
    if (!doc || !doc->maps.contains(u"event_mods"_s))
        return nullptr;
    return &doc->map<modifiers::ModifierEdits>(u"event_mods"_s);
}

QStringList EventPanel::cat_order()
{
    // Every category: the user's order, then those the files have that the order doesn't know yet.
    events::EventCats *c = cats_store(doc);
    QStringList extra;
    if (c)
        for (const QString &n : c->all_names())
            if (!_order.contains(n) && !extra.contains(n))
                extra.append(n);
    std::stable_sort(extra.begin(), extra.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
    if (!extra.isEmpty()) {
        _order += extra;
        _save_cats();
    }
    return _order;
}

QStringList EventPanel::cats_of(const Entry &e) { return key_cats({rel_of(e), e.uid}); }

QStringList EventPanel::file_cats(const QString &rel) { return key_cats({rel, std::nullopt}); }

QStringList EventPanel::key_cats(const CatKey &key)
{
    if (events::EventCats *c = cats_store(doc))
        return c->get(key);
    if (!db)
        return {};
    if (!key.second)
        return db->file_cats.value(key.first);
    for (const auto &e : db->entries)
        if (e->rel == key.first && e->uid == key.second)
            return e->cats;
    return {};
}

std::vector<CatKey> EventPanel::_all_keys() const
{
    std::vector<CatKey> out;
    for (const auto &e : db->entries)
        out.emplace_back(rel_of(*e), e->uid);
    for (const QString &r : db->files)
        out.emplace_back(r, std::nullopt);
    return out;
}

std::optional<CatKey> EventPanel::_cur_key() const
{
    if (sel)
        return CatKey{rel_of(*sel), sel->uid};
    if (sel_file)
        return CatKey{*sel_file, std::nullopt};
    return std::nullopt;
}

QStringList EventPanel::_cur_cats()
{
    const auto k = _cur_key();
    return k ? key_cats(*k) : QStringList();
}

void EventPanel::_say(const QString &text)
{
    bar_msg->setText(!text.isEmpty() ? text
                                     : u"Select rows (Ctrl / Shift-click for several), then click a category — or press "
                                       u"its number — to put it on them, again to take it off · 0 takes all off · drag "
                                       u"rows onto a category · right-click a category to rename, recolour, move or "
                                       u"delete it"_s);
}

QString EventPanel::_rows(qsizetype n) { return u"%1 row%2"_s.arg(n).arg(n == 1 ? QString() : u"s"_s); }

std::vector<CatKey> EventPanel::_selected_keys()
{
    std::vector<CatKey> out;
    for (const auto &[k, _g] : _drag_keys())
        if (std::find(out.begin(), out.end(), k) == out.end())
            out.push_back(k);
    return out;
}

std::vector<std::pair<CatKey, OptStr>> EventPanel::_drag_keys()
{
    // (key, the category it's listed under or None) of every selected file / entry row.
    std::vector<std::pair<CatKey, OptStr>> out;
    for (QTreeWidgetItem *it : lst->selectedItems()) {
        const RowData d = row_data(it);
        if (d.kind == u'f')
            out.emplace_back(CatKey{d.key, std::nullopt}, d.group);
        else if (d.kind == u'e' && d.e)
            out.emplace_back(CatKey{d.key, d.e->uid}, d.group);
    }
    return out;
}

qsizetype EventPanel::_emit(const CatChanges &changes0, const QString &text)
{
    CatChanges changes;
    for (const auto &[k, v] : changes0)
        if (v != key_cats(k))
            changes.emplace_back(k, v);
    if (!changes.empty())
        Q_EMIT catEdit(changes, text);
    return qsizetype(changes.size());
}

static QStringList without(const QStringList &names, const QString &name)
{
    QStringList out;
    for (const QString &n : names)
        if (n != name)
            out.append(n);
    return out;
}

void EventPanel::toggle_cat(const QString &name)
{
    // A category's button / number: on all selected rows — or off, when they all have it already.
    const std::vector<CatKey> keys = _selected_keys();
    lst->setFocus();
    if (keys.empty()) {
        _say(u"Select rows first (Ctrl / Shift-click for several), then click “%1” or press its number."_s.arg(name));
        return;
    }
    const bool all = std::all_of(keys.begin(), keys.end(), [&](const CatKey &k) { return key_cats(k).contains(name); });
    CatChanges ch;
    if (all) {
        for (const CatKey &k : keys)
            ch.emplace_back(k, without(key_cats(k), name));
        _emit(ch, u"%1 off %2"_s.arg(name, _rows(qsizetype(keys.size()))));
    } else {
        for (const CatKey &k : keys)
            if (!key_cats(k).contains(name))
                ch.emplace_back(k, key_cats(k) + QStringList{name});
        _emit(ch, u"%1 on %2"_s.arg(name, _rows(qsizetype(keys.size()))));
    }
    _say(QString());
}

void EventPanel::clear_cats()
{
    const std::vector<CatKey> keys = _selected_keys();
    CatChanges ch;
    for (const CatKey &k : keys)
        ch.emplace_back(k, QStringList());
    _emit(ch, u"No category on %1"_s.arg(_rows(qsizetype(keys.size()))));
}

void EventPanel::_key(int n)
{
    if (n == 0) {
        clear_cats();
        return;
    }
    const QStringList order = cat_order();
    if (n <= order.size())
        toggle_cat(order[n - 1]);
}

void EventPanel::_drop_on(const QString &target, bool copy, const std::vector<std::pair<CatKey, OptStr>> &keys_groups)
{
    // Rows dropped onto a category: they get it — and, dragged from under another category in the category view,
    // leave that one (Ctrl held: they keep it). Onto “Without a category”: they leave the one they were under.
    CatChanges changes;
    std::map<CatKey, size_t> at;
    for (const auto &[key, g] : keys_groups) {
        auto f = at.find(key);
        QStringList names = f != at.end() ? changes[f->second].second : key_cats(key);
        if (target != u"__none" && !names.contains(target))
            names = names + QStringList{target};
        if (g && *g != u"__none" && *g != target && (target == u"__none" || !copy))
            names = without(names, *g);
        if (f != at.end()) {
            changes[f->second].second = names;
        } else {
            at[key] = changes.size();
            changes.emplace_back(key, names);
        }
    }
    if (changes.empty())
        return;
    _emit(changes, u"%1 → %2"_s.arg(_rows(qsizetype(changes.size())),
                                    target == u"__none" ? u"without a category"_s : target));
}

void EventPanel::_drop_cat(const QString &name)
{
    if (const auto k = _cur_key())
        _emit({{*k, without(key_cats(*k), name)}}, u"%1 off 1 row"_s.arg(name));
}

void EventPanel::_set_color(const QString &name, const Rgb &rgb)
{
    _colors.insert(name, rgb);
    _save_cats();
    cats_changed();
}

QWidget *EventPanel::_color_grid(const QString &name, QMenu *menu)
{
    // The colours to pick from (a click takes one), and a box for any colour as #rrggbb.
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(8, 8, 8, 8);
    auto *g = new QGridLayout();
    g->setSpacing(5);
    const Rgb cur = color(name);
    for (size_t i = 0; i < GRID.size(); ++i) {
        const Rgb rgb = GRID[i];
        auto *b = new QPushButton();
        b->setFixedSize(30, 30);
        b->setToolTip(hex6(rgb));
        b->setStyleSheet(u"QPushButton { background: %1; border: %2; border-radius: 5px; } "
                         u"QPushButton:hover { border: 3px solid #f4ead2; }"_s.arg(
                             rgb_css(rgb), rgb == cur ? u"3px solid #f4ead2"_s : u"2px solid #14100a"_s));
        connect(b, &QPushButton::clicked, this, [this, menu, name, rgb] {
            menu->close();
            _set_color(name, rgb);
        });
        g->addWidget(b, int(i) / 6, int(i) % 6);
    }
    v->addLayout(g);
    auto *row = new QHBoxLayout();
    auto *hx = new QLineEdit(hex6(cur));
    hx->setFixedWidth(110);
    row->addWidget(hx);
    auto *ok = new QPushButton(u"Use"_s);
    row->addWidget(ok);
    row->addStretch(1);
    v->addLayout(row);
    auto use = [this, hx, menu, name] {
        QString t = py::strip(hx->text());
        while (t.startsWith(u'#'))
            t.remove(0, 1);
        static const QRegularExpression HEX(u"^[0-9a-fA-F]{6}$"_s);
        if (!HEX.match(t).hasMatch() || t.contains(u'\n')) {
            hx->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
            hx->setToolTip(u"Six hex digits, like #3ca0e6"_s);
            return;
        }
        menu->close();
        _set_color(name, Rgb{t.mid(0, 2).toInt(nullptr, 16), t.mid(2, 2).toInt(nullptr, 16), t.mid(4, 2).toInt(nullptr, 16)});
    };
    connect(ok, &QPushButton::clicked, this, use);
    connect(hx, &QLineEdit::returnPressed, this, use);
    return w;
}

void EventPanel::_tree_menu(const QPoint &pos)
{
    const RowData d = row_data(lst->itemAt(pos));
    if (d.kind == u'c' && d.key != u"__none")
        _cat_menu(d.key, lst->viewport(), pos);
}

void EventPanel::_move_cat(const QString &name, int step)
{
    const qsizetype i = _order.indexOf(name);
    const qsizetype j = i + step;
    if (0 <= j && j < _order.size()) {
        _order.swapItemsAt(i, j);
        _save_cats();
        cats_changed();
    }
}

void EventPanel::_new_cat(const OptStr &rename)
{
    _rename = rename;
    new_edit->setText(rename.value_or(QString()));
    new_edit->setPlaceholderText(rename && !rename->isEmpty() ? u"New name of “%1”"_s.arg(*rename)
                                                              : u"Name of the new category"_s);
    new_ok->setText(rename && !rename->isEmpty() ? u"Rename"_s : u"Make it"_s);
    new_warn->setText(QString());
    new_row->setVisible(true);
    new_edit->setFocus();
    new_edit->selectAll();
}

void EventPanel::_new_done()
{
    const QString name = py::split(new_edit->text()).join(u' ');
    OptStr bad;
    for (QChar ch : u",#\"{}="_s)
        if (name.contains(ch)) {
            bad = QString(ch);
            break;
        }
    const OptStr old = _rename;
    if (name.isEmpty()) {
        new_warn->setText(u"Type a name first."_s);
        return;
    }
    if (bad) {
        new_warn->setText(u"A name can't have %1 in it (it's written into the files as a comment)."_s.arg(*bad));
        return;
    }
    const QStringList order = cat_order();
    if (!old) {
        if (order.contains(name)) {
            new_warn->setText(u"“%1” is there already."_s.arg(name));
            return;
        }
        _order.append(name);
        color(name);
        _save_cats();
        const qsizetype i = _order.size();
        _say(u"Made “%1” — select rows and click it%2 to put it on them."_s.arg(
            name, i <= 9 ? u" (or press %1)"_s.arg(i) : QString()));
    } else if (name != *old) {
        std::vector<CatKey> keys;
        for (const CatKey &k : _all_keys())
            if (key_cats(k).contains(*old))
                keys.push_back(k);
        if (order.contains(name)) {          // renamed into another one: the two become one
            _order.removeOne(*old);
        } else {
            _order[_order.indexOf(*old)] = name;
            _colors.insert(name, color(*old));
        }
        _save_cats();
        CatChanges ch;
        for (const CatKey &k : keys) {
            QStringList names;
            for (const QString &n : key_cats(k)) {
                const QString x = n == *old ? name : n;
                if (!names.contains(x))
                    names.append(x);
            }
            ch.emplace_back(k, names);
        }
        _emit(ch, u"Category %1 renamed %2 (%3)"_s.arg(*old, name, _rows(qsizetype(keys.size()))));
        _say(u"Renamed “%1” to “%2”%3 — Ctrl+Z undoes it."_s.arg(
            *old, name, order.contains(name) ? u" (merged into the one of that name)"_s : QString()));
    }
    new_row->setVisible(false);
    _rename = std::nullopt;
    cats_changed();
}

void EventPanel::_delete_cat(const QString &name)
{
    std::vector<CatKey> keys;
    for (const CatKey &k : _all_keys())
        if (key_cats(k).contains(name))
            keys.push_back(k);
    _order.removeOne(name);
    _save_cats();
    CatChanges ch;
    for (const CatKey &k : keys)
        ch.emplace_back(k, without(key_cats(k), name));
    _emit(ch, u"Category %1 deleted (off %2)"_s.arg(name, _rows(qsizetype(keys.size()))));
    _say(u"Deleted “%1”%2."_s.arg(name, !keys.empty() ? u" — taken off %1; Ctrl+Z puts it back"_s.arg(
                                                            _rows(qsizetype(keys.size())))
                                                      : QString()));
    cats_changed();
}

void EventPanel::_cat_menu(const QString &name, QWidget *btn, const QPoint &pos)
{
    QMenu m(this);
    QMenu *sub = m.addMenu(cats_icon({color(name)}), u"Colour"_s);
    auto *wa = new QWidgetAction(sub);
    wa->setDefaultWidget(_color_grid(name, &m));
    sub->addAction(wa);
    m.addAction(u"Rename…"_s, this, [this, name] { _new_cat(name); });
    QAction *a = m.addAction(u"Move left"_s, this, [this, name] { _move_cat(name, -1); });
    a->setEnabled(_order.indexOf(name) > 0);
    a = m.addAction(u"Move right"_s, this, [this, name] { _move_cat(name, 1); });
    a->setEnabled(_order.indexOf(name) < _order.size() - 1);
    m.addSeparator();
    m.addAction(u"Delete category"_s, this, [this, name] { _delete_cat(name); });
    m.exec(btn->mapToGlobal(pos));
}

void EventPanel::cats_changed()
{
    // After a category edit (or its undo): the bar, the list, the chips, the map.
    _fill_cat_box();
    _fill_list();
    _fill_detail();
    _fill_sym();
    Q_EMIT changed();
}

void EventPanel::_fill_cat_box()
{
    const QStringList order = cat_order();
    const QVariant cur = cat_box->currentData();
    cat_box->blockSignals(true);
    cat_box->clear();
    cat_box->addItem(u"All"_s, u"__all"_s);
    cat_box->addItem(u"Without a category"_s, u"__none"_s);
    for (const QString &n : order)
        cat_box->addItem(cats_icon({color(n)}), n, n);
    const int i = cat_box->findData(cur);
    cat_box->setCurrentIndex(std::max(0, i));
    cat_box->blockSignals(false);
    _fill_bar();
}

static void clear_row(widgets::WrapRow *lay)
{
    while (lay->count()) {
        QLayoutItem *item = lay->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void EventPanel::_fill_bar()
{
    clear_row(bar_lay);
    bar_btn.clear();
    QHash<QString, long long> counts;
    if (db)
        for (const auto &e : db->entries) {
            QSet<QString> names;
            for (const QString &n : cats_of(*e))
                names.insert(n);
            for (const QString &n : file_cats(rel_of(*e)))
                names.insert(n);
            for (const QString &n : names)
                ++counts[n];
        }
    const QStringList order = cat_order();
    for (qsizetype i = 0; i < order.size(); ++i) {
        const QString n = order[i];
        const long long c = counts.value(n);
        auto *b = new CatButton(i < 9 ? u"%1 · %2  %3"_s.arg(QString::number(i + 1), n, QString::number(c))
                                      : u"%1  %2"_s.arg(n, QString::number(c)));
        b->setToolTip(u"“%1” — %2 events / decisions have it (their own or their file's)\n"
                      u"Click%3: on / off the selected rows · drag rows here to give it to them\n"
                      u"Right-click: another colour, rename, move, delete"_s.arg(
                          n, QString::number(c), i < 9 ? u" or press %1 in the list"_s.arg(i + 1) : QString()));
        connect(b, &QPushButton::clicked, this, [this, n] { toggle_cat(n); });
        connect(b, &CatButton::dropped, this, [this, n] { _drop_on(n, true, _drag_keys()); });
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this, [this, n, b](const QPoint &pos) { _cat_menu(n, b, pos); });
        bar_lay->addWidget(b);
        bar_btn.insert(n, b);
    }
    auto *nb = new QPushButton(u"＋ New category"_s);
    connect(nb, &QPushButton::clicked, this, [this] { _new_cat(); });
    bar_lay->addWidget(nb);
    bar->updateGeometry();
    _style_bar();
}

void EventPanel::_style_bar()
{
    // A category's button: a bright ring when every selected row has it, a dashed one when some do.
    const std::vector<CatKey> keys = db ? _selected_keys() : std::vector<CatKey>();
    for (const auto &[n, b] : bar_btn) {
        const auto [r, g, bl] = color(n);
        long long have = 0;
        for (const CatKey &k : keys)
            if (key_cats(k).contains(n))
                ++have;
        const QString ring = !keys.empty() && have == qsizetype(keys.size()) ? u"3px solid #f4ead2"_s
                             : have                                         ? u"3px dashed #f4ead2"_s
                                                                            : u"3px solid #14100a"_s;
        b->setStyleSheet(u"QPushButton { background: rgb(%1,%2,%3); color: #14100a; font-weight: 600; border: %4; "
                         u"border-radius: 13px; padding: 3px 12px; } QPushButton:hover { background: "
                         u"rgb(%5,%6,%7); } QPushButton:pressed { background: #f4ead2; }"_s.arg(r).arg(g).arg(bl).arg(
                             ring).arg(std::min(255, r + 25)).arg(std::min(255, g + 25)).arg(std::min(255, bl + 25)));
    }
}

void EventPanel::_fill_chips()
{
    clear_row(chip_lay);
    const QStringList names = _cur_cats();
    if (names.isEmpty())
        chip_lay->addWidget(widgets::label(u"none yet"_s, u"faint"_s, false));
    for (const QString &n : names) {
        auto *b = new QPushButton(u"%1  ✕"_s.arg(n));
        b->setToolTip(u"Take “%1” off"_s.arg(n));
        b->setStyleSheet(u"QPushButton { background: %1; color: #14100a; font-weight: 600; "
                         u"border: 1px solid #14100a; border-radius: 10px; padding: 3px 12px; }"_s.arg(rgb_css(color(n))));
        connect(b, &QPushButton::clicked, this, [this, n] { _drop_cat(n); });
        chip_lay->addWidget(b);
    }
    chips->updateGeometry();
}

// ---------------------------------------------------------------------------------------------- the editor
void EventPanel::attach(scriptedit::ScriptWorkspace *w)
{
    // The editor in the middle of the window: its cursor tells what to show here; links here open there.
    ws = w;
    w->info_extra = [this](const QString &k, const QString &t) { return info_extra(k, t); };
    connect(w, &scriptedit::ScriptWorkspace::symbolChanged, this, &EventPanel::_symbol_changed);
    connect(w, &scriptedit::ScriptWorkspace::selectionRefs, this, &EventPanel::_selection_changed);
    connect(w, &scriptedit::ScriptWorkspace::relationsShown, this, &EventPanel::_fill_relations);
    connect(w, &scriptedit::ScriptWorkspace::placeChanged, this, &EventPanel::_place_changed);
    connect(w, &scriptedit::ScriptWorkspace::textEdited, this, &EventPanel::_text_edited);
}

void EventPanel::attach_extra(scriptedit::ScriptWorkspace *w)
{
    // An editor another page borrows: typing in it is read like typing here; it learns what the words mean.
    extra_ws.push_back(w);
    w->info_extra = [this](const QString &k, const QString &t) { return info_extra(k, t); };
    connect(w, &scriptedit::ScriptWorkspace::textEdited, this, &EventPanel::_text_edited);
}

void EventPanel::ensure(document::Document *d)
{
    // The scripts read and their words known — a page that borrows the editor asks before it shows.
    if (d != doc || !db || !refs)
        refresh(d);
    for (scriptedit::ScriptWorkspace *x : extra_ws) {
        x->set_store(store);
        if (x->refs != refs.get())
            x->set_refs(refs.get());
    }
}

QStringList EventPanel::_extra_rels()
{
    try {
        return QStringList{refs::REBEL_REL} + entryview::_files(mod, store, u"poptypes"_s);
    } catch (...) {
        return {};
    }
}

void EventPanel::_text_edited(const QString &rel)
{
    _pending.insert(rel);
    _reparse_timer->start();
}

void EventPanel::reparse_now()
{
    if (!_pending.isEmpty()) {
        _reparse_timer->stop();
        _reparse();
    }
}

void EventPanel::_reparse_due()
{
    if (hold && hold()) {
        _reparse_timer->start();             // (asked again in a moment)
        return;
    }
    _reparse();
}

void EventPanel::_reparse()
{
    // Typed text (or a category / modifier change): the events and decisions read again, the references of the
    // changed files again; what didn't change keeps its computed chances.
    if (!db || !store) {
        _pending.clear();
        return;
    }
    QStringList rels = _pending.values();
    std::sort(rels.begin(), rels.end());
    const QSet<QString> rel_set = _pending;
    _pending.clear();
    EventDB *old = db.get();
    QHash<QString, std::pair<std::vector<refs::Ref>, qsizetype>> before;
    for (const QString &rel : rels) {
        const auto *sp = refs->spans.get(rel);
        const QString *ot = old->texts.get(rel);
        before.insert(rel, {sp ? *sp : std::vector<refs::Ref>(), ot ? ot->size() : 0});
    }
    auto fresh = std::make_unique<EventDB>(mod, store, old);
    QHash<const Entry *, Agg> keep;
    for (const auto &e : fresh->entries) {
        Entry *const *o = old->by_uid.get({rel_of(*e), uid_of(*e)});
        if (o && agg.contains(*o) &&
            py::slice((*o)->text, (*o)->span.first, (*o)->span.second) == py::slice(e->text, e->span.first, e->span.second))
            keep.insert(e.get(), agg.value(*o));
    }
    _old_db = std::move(db);                 // (what still points into it: until the next reparse)
    db = std::move(fresh);
    EventDB *nw = db.get();
    _mention_cache.clear();
    _fires.reset();
    if (events::EventCats *cats = cats_store(doc))
        cats->attach(nw);
    if (world) {
        judge = std::make_unique<events::Judge>(*world, *nw);
        pjudge = std::make_unique<events::PotentialJudge>(*world, *nw);
    }
    agg = keep;
    if (sel) {
        Entry *const *n = nw->by_uid.get({rel_of(*sel), uid_of(*sel)});
        sel = n ? *n : nullptr;
    }
    refs->db = nw;
    QHash<QString, std::optional<std::pair<qsizetype, qsizetype>>> ranges;
    for (const QString &rel : rels) {
        const QString text = store->text(rel);
        const auto *tree = nw->trees.get(rel);
        refs->update(rel, text, tree ? tree->get() : nullptr);
        const auto *sp = refs->spans.get(rel);
        ranges.insert(rel, _changed_range(before.value(rel).first, sp ? *sp : std::vector<refs::Ref>(),
                                          before.value(rel).second, text.size()));
    }
    ws->set_refs(refs.get(), rel_set, ranges);
    for (scriptedit::ScriptWorkspace *x : extra_ws)
        x->set_refs(refs.get());
    std::vector<Entry *> todo;
    for (const auto &e : nw->entries)
        if (!keep.contains(e.get()))
            todo.push_back(e.get());
    _gen.reset();
    _queue.clear();                          // (the old one's entries: not worked out any more)
    if (!todo.empty() && world) {
        std::stable_sort(todo.begin(), todo.end(), [](Entry *a, Entry *b) { return !a->province() && b->province(); });
        _queue = todo;
        _total = qsizetype(_queue.size());
        _timer->start();
    }
    _fill_cat_box();
    _fill_list();
    _fill_detail();
    _fill_sym();
    _fill_relations();                       // (it pointed into the old reading: shown anew)
    Q_EMIT changed();
}

void EventPanel::_open_entry(Entry *e, bool record)
{
    if (ws && e)
        ws->open(rel_of(*e), e->span.first, false, record, e->span);
}

void EventPanel::_open_mod(const QString &name)
{
    modifiers::ModifierEdits *m = mods_store(doc);
    if (ws && m && m->exists(name)) {
        const auto sp = m->span(name);
        ws->open(modifiers::REL, sp.first, false, true, sp);
    }
}

void EventPanel::set_problems(const OrderedMap<QString, std::pair<long long, long long>> &counts,
                              const scriptedit::ProblemsByRel &by_rel)
{
    // What the checks found in the scripts: {kind of the list: (errors, warnings)} for the count circles on Events /
    // Decisions / Modifiers / War goals, and {file: [(position, severity, text)]} for the rows.
    const auto ev_ = counts.value(u"event"_s, {0, 0}), dec = counts.value(u"decision"_s, {0, 0});
    const std::pair<long long, long long> both{ev_.first + dec.first, ev_.second + dec.second};
    for (const auto &[k, b] : kind_btn) {
        const auto c = k == u"both" ? both : counts.value(k, {0, 0});
        b->set_counts(c.first, c.second);
    }
    if (by_rel != _problems) {
        _problems = by_rel;
        _attribute();
        if (db && isVisible() && _kind() != u"mods" && _kind() != u"cbs")
            _fill_list();
        _fill_row_problems();
    }
}

void EventPanel::_attribute()
{
    // Every problem that has a place goes to ONE event / decision — the one it lies in; when it lies outside all of
    // them (a bracket missing at the end of a file), the one before it. So a file with a problem always shows which
    // of its entries it is.
    std::map<std::pair<QString, QString>, std::vector<scriptedit::Problem>> out;
    if (db && !_problems.empty()) {
        QHash<QString, std::vector<Entry *>> by_file;
        for (const auto &e : db->entries)
            by_file[rel_of(*e)].push_back(e.get());
        for (const auto &[rel, lst_] : _problems) {
            std::vector<Entry *> ents = by_file.value(rel);
            if (ents.empty())
                continue;
            std::stable_sort(ents.begin(), ents.end(), [](Entry *a, Entry *b) { return a->span.first < b->span.first; });
            for (const scriptedit::Problem &p : lst_) {
                if (!p.pos)
                    continue;
                const qsizetype pos = *p.pos;
                Entry *hit = nullptr;
                for (Entry *e : ents)
                    if (e->span.first <= pos && pos < e->span.second) {
                        hit = e;
                        break;
                    }
                if (!hit) {
                    for (Entry *e : ents)
                        if (e->span.first <= pos)
                            hit = e;
                    if (!hit)
                        hit = ents[0];
                }
                out[{rel, uid_of(*hit)}].push_back(p);
            }
        }
    }
    _prob_entry = out;
}

std::pair<OptStr, std::vector<scriptedit::Problem>> EventPanel::_cur_problems()
{
    // (file, [(position or None, severity, text)]) of the event / decision — or the file — picked.
    if (sel) {
        auto f = _prob_entry.find({rel_of(*sel), uid_of(*sel)});
        return {rel_of(*sel), f != _prob_entry.end() ? f->second : std::vector<scriptedit::Problem>()};
    }
    if (sel_file && !sel_file->isEmpty()) {
        auto f = _problems.find(*sel_file);
        return {*sel_file, f != _problems.end() ? f->second : std::vector<scriptedit::Problem>()};
    }
    return {std::nullopt, {}};
}

void EventPanel::_fill_row_problems()
{
    const auto [rel, lst_] = _cur_problems();
    row_prob->setVisible(!lst_.empty());
    row_jump->setVisible(!lst_.empty());
    if (lst_.empty())
        return;
    const bool err = std::any_of(lst_.begin(), lst_.end(), [](const auto &p) { return p.sev == u"error"; });
    const QString col = err ? theme::ERROR_ : theme::WARN;
    const QString what = sel ? nm(*sel) : rel_tail(*rel);
    QString rows;
    for (size_t i = 0; i < std::min<size_t>(8, lst_.size()); ++i) {
        const auto &p = lst_[i];
        const qsizetype where = p.pos ? *p.pos : store->text(*rel).size();
        rows += u"<div style=\"margin-top:3px\">• %1 &nbsp;<a href=\"go:%2|%3\" style=\"color:%4\">▸ go there</a></div>"_s
                    .arg(esc(p.text), esc(*rel), QString::number(where), theme::GOLD);
    }
    row_prob->setStyleSheet(u"QLabel { color: %1; border: 2px solid %1; border-radius: 4px; padding: 6px 8px; }"_s.arg(col));
    const qsizetype n = qsizetype(lst_.size());
    row_prob->setText(u"<b>⚠ %1 — %2:</b>%3%4"_s.arg(
        esc(what),
        err ? (n > 1 ? u"%1 errors"_s.arg(n) : u"an error"_s) : (n > 1 ? u"%1 warnings"_s.arg(n) : u"a warning"_s), rows,
        n > 8 ? u"<div>… and %1 more</div>"_s.arg(n - 8) : QString()));
    row_jump->setText(n == 1 ? u"▸ Jump to the error"_s : u"▸ Jump to the next of its %1"_s.arg(n));
    row_jump->setStyleSheet(u"QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
                            u"font-weight: 700; padding: 6px 12px; }"_s.arg(col, err ? u"#ffffff"_s : u"#14100a"_s));
}

void EventPanel::_jump_problem()
{
    const auto [rel, lst_] = _cur_problems();
    if (lst_.empty() || !ws)
        return;
    _prob_i = (_prob_i + 1) % int(lst_.size());
    const QString t = store->text(*rel);
    const qsizetype pos = lst_[size_t(_prob_i)].pos ? *lst_[size_t(_prob_i)].pos : t.size();
    ws->open(*rel, pos, false, true, line_span(t, pos));
}

std::pair<OptStr, QStringList> EventPanel::_row_problems(const QString &rel)
{
    // (worst severity or None, [texts]) of the problems in a file.
    auto f = _problems.find(rel);
    if (f == _problems.end() || f->second.empty())
        return {std::nullopt, {}};
    QStringList texts;
    bool err = false;
    for (const auto &p : f->second) {
        texts.append(p.text);
        err = err || p.sev == u"error";
    }
    return {err ? u"error"_s : u"warn"_s, texts};
}

void EventPanel::_mark_problem(QTreeWidgetItem *item, const OptStr &sev, const QStringList &texts)
{
    if (!sev)
        return;
    const QBrush br(QColor::fromString(*sev == u"error" ? theme::ERROR_ : theme::WARN));
    for (int c = 0; c < item->columnCount(); ++c)   // the whole row: red = an error, amber = a warning
        item->setForeground(c, br);
    item->setData(0, Qt::UserRole + 2, *sev);  // (nothing else recolours this row)
    item->setText(0, u"⚠ "_s + item->text(0));
    item->setToolTip(0, (!item->toolTip(0).isEmpty() ? item->toolTip(0) + u"\n\n"_s : QString()) + u"⚠ "_s +
                            texts.mid(0, 6).join(u"\n⚠ "_s) +
                            (texts.size() > 6 ? u"\n… (%1 more)"_s.arg(texts.size() - 6) : QString()));
}

void EventPanel::_open_cb(const QString &key, bool new_tab)
{
    // A war goal picked in the list: the editor shows it (its lines lit for a moment).
    const auto sp = cb_view->span(key);
    if (ws && sp)
        ws->open(cbcheck::REL, sp->first, new_tab, true, sp, new_tab ? cb_view->nm(key) : QString());
    _fill_sym();
    Q_EMIT changed();
}

void EventPanel::show_cb(const OptStr &key)
{
    // The War goals list, with this war goal picked and open in the editor.
    (*kind_btn.get(u"cbs"_s))->setChecked(true);
    _set_kind_vis();
    cb_view->refresh(doc, db.get(), refs.get());
    if (key && !key->isEmpty() && cb_view->exists(*key)) {
        cb_view->select(*key);
        _open_cb(*key);
    }
    Q_EMIT changed();
}

void EventPanel::_place_changed(const QString &rel, long long pos)
{
    if (!db)
        return;
    if (rel == cbcheck::REL) {               // the cursor moved into another war goal: the list follows
        if (_kind() == u"cbs") {
            const OptStr key = cb_view->goal_at(pos);
            if (key && !key->isEmpty() && key != cb_view->cur) {
                cb_view->mark(*key);
                _fill_sym();
                Q_EMIT changed();
            }
        }
        return;
    }
    Entry *e = db->entry_at(rel, pos);
    if (!e || e == sel)
        return;
    sel = e;
    sel_file = rel_of(*e);
    sel_cat = std::nullopt;
    _mark_row(e);
    _fill_detail();
    _fill_relations();
    Q_EMIT changed();
}

void EventPanel::_mark_row(Entry *e)
{
    // The list shows the entry the cursor is in (without opening anything).
    const auto items = _items.value({rel_of(*e), uid_of(*e)});
    if (!items.empty()) {
        lst->blockSignals(true);
        lst->setCurrentItem(items[0]);
        lst->blockSignals(false);
        lst->scrollToItem(items[0]);
    }
}

void EventPanel::_symbol_changed(const std::optional<refs::Ref> &r)
{
    sym = r;
    sym_on_map = false;
    _sym_changed();
}

void EventPanel::_sym_changed()
{
    _fill_sym();
    _sym_hits_calc();
    if (only_sym->isChecked())
        _fill_list();
    else
        _paint_sym_rows();
    _fill_relations();
    Q_EMIT focusChanged();
}

void EventPanel::focus_symbol(const QString &kind, const QString &t)
{
    // A thing chosen outside the text (a nation right-clicked on the map): the focus, as if the cursor were on it —
    // the side panel, the list, the text and the map show it.
    sym = refs::Ref{0, 0, kind, t, u"use"_s, true, std::nullopt};
    sym_on_map = true;
    if (ws)
        ws->set_focus(kind, t);
    _sym_changed();
}

void EventPanel::_tree_go(const QString &rel, long long pos)
{
    if (ws)
        ws->open(rel, pos, false, true, line_span(store->text(rel), pos));
}

void EventPanel::_sym_hits_calc()
{
    QHash<std::pair<QString, QString>, long long> hits;
    if (sym && refs && db)
        for (const refs::Use &u : refs->usages(sym->kind, sym->target)) {
            Entry *e = u.rel.startsWith(u"events/") || u.rel.startsWith(u"decisions/") ? db->entry_at(u.rel, u.pos)
                                                                                      : nullptr;
            if (e)
                ++hits[{rel_of(*e), uid_of(*e)}];
        }
    _sym_hits = hits;
}

void EventPanel::_paint_sym_rows()
{
    // The list: the entries (and files) the word at the cursor is in — lit in its colour, bold.
    std::optional<Rgb> rgb;
    if (sym) {
        const auto *k = refs::KINDS.get(sym->kind);
        rgb = k ? k->second : Rgb{232, 196, 118};
    }
    QHash<std::pair<QString, QString>, long long> hits = _sym_hits;
    for (auto it = _sym_hits.begin(); it != _sym_hits.end(); ++it)
        hits[{it.key().first, QString()}] += it.value();
    QHash<std::pair<QString, QString>, std::pair<long long, Rgb>> want;
    if (rgb)
        for (auto it = hits.begin(); it != hits.end(); ++it)
            want.insert(it.key(), {it.value(), *rgb});
    QSet<std::pair<QString, QString>> keys = _painted;
    for (auto it = want.begin(); it != want.end(); ++it)
        keys.insert(it.key());
    for (const auto &key : keys) {
        const auto *w = want.contains(key) ? &want[key] : nullptr;
        const auto *p = _paint_state.contains(key) ? &_paint_state[key] : nullptr;
        if (((!w && !p) || (w && p && *w == *p)) && _painted.contains(key))
            continue;
        for (QTreeWidgetItem *item : _items.value(key)) {
            if (w)
                _paint_row(item, w->first, w->second);
            else
                _paint_row(item, 0, std::nullopt);
        }
    }
    _painted.clear();
    for (auto it = want.begin(); it != want.end(); ++it)
        _painted.insert(it.key());
    _paint_state = want;
}

void EventPanel::_paint_row(QTreeWidgetItem *item, long long n, const std::optional<Rgb> &rgb)
{
    QVariantList base = item->data(1, Qt::UserRole + 1).toList();
    if (base.size() != 2) {
        base = {QFont(item->font(0)), item->foreground(0)};
        item->setData(1, Qt::UserRole + 1, base);
    }
    const QFont bf = base[0].value<QFont>();
    if (n && rgb) {
        QFont f(bf);
        f.setBold(true);
        for (int c : {0, 1}) {
            item->setFont(c, f);
            if (item->data(0, Qt::UserRole + 2).toString().isEmpty())   // (a row with a problem stays red / amber)
                item->setForeground(c, QBrush(qc(*rgb)));
        }
        item->setToolTip(1, u"“%1” is mentioned %2× here"_s.arg(sym ? sym->target : QString(), QString::number(n)));
    } else {
        for (int c : {0, 1}) {
            item->setFont(c, bf);
            item->setForeground(c, base[1].value<QBrush>());
        }
    }
}

void EventPanel::_fill_relations()
{
    if (!ws || !ws->rel_visible())
        return;
    std::function<QString(const QString &)> text_of;
    if (store)
        text_of = [this](const QString &r) { return store->text(r); };
    ws->mini->rel->show_for(refs.get(), db.get(), text_of, sym, sel, db ? _fires_map() : relview::FiresMap());
}

void EventPanel::_selection_changed(const std::vector<refs::Ref> &r)
{
    _sel_refs = r;
    Q_EMIT focusChanged();
}

void EventPanel::_list_double(QTreeWidgetItem *item, int)
{
    const RowData d = row_data(item);
    if (d.kind == u'e' && d.e && ws)
        ws->open(rel_of(*d.e), d.e->span.first, true, true, d.e->span, nm(*d.e));
}

// ---------------------------------------------------------------------------------------------- modifiers and flags
const modifiers::ModifierDB::Uses *EventPanel::_mod_used_map()
{
    if (_mod_used.first != db.get()) {
        modifiers::ModifierEdits *m = mods_store(doc);
        _mod_used = {db.get(), m ? m->db().used_by(*db) : modifiers::ModifierDB::Uses()};
    }
    return &_mod_used.second;
}

std::optional<modifiers::Attrs> EventPanel::_mod_attrs(const QString &name)
{
    // (key, value) effects of a modifier — of common/event_modifiers.txt, or of the static / triggered modifier
    // files — or None.
    modifiers::ModifierEdits *m = mods_store(doc);
    if (m && m->exists(name))
        return m->attrs(name);
    const auto d = refs ? refs->definition(u"modifier"_s, name) : std::nullopt;
    if (!d)
        return std::nullopt;
    const auto &[rel, pos] = *d;
    const QString text = store->text(rel);
    const auto key = std::tuple{rel, pos, text.size()};
    auto f = _mod_attr_cache.find(key);
    if (f == _mod_attr_cache.end()) {
        const script::Nodes nodes = script::parse(py::tail(text, pos));
        modifiers::Attrs out;
        if (!nodes.empty() && nodes[0].is_block)
            for (const script::Node &c : nodes[0].kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    out.emplace_back(c.key, c.value);
        f = _mod_attr_cache.emplace(key, out).first;
    }
    return f->second;
}

EventPanel::Holders EventPanel::flag_holders(const QString &kind, const QString &t)
{
    // Who has / can get a flag: (at the start {tag or pid}, {tag: "now" / "later"}, [notes]).
    Holders h;
    if (const auto *hh = db->history_holders().get({kind, t}))
        h.hist = *hh;
    QStringList notes;
    for (const refs::Use &u : refs->usages(kind, t)) {
        if (u.role != u"set")
            continue;
        const refs::Ref *r = refs->at(u.rel, u.pos);
        const OptStr scope = r ? r->scope : std::nullopt;
        Entry *e = u.rel.startsWith(u"events/") || u.rel.startsWith(u"decisions/") ? db->entry_at(u.rel, u.pos)
                                                                                  : nullptr;
        const QString name = e ? nm(*e) : u.rel.split(u'/').last();
        if (scope && !scope->isEmpty() && mod.countries.contains(*scope)) {
            if (!h.could.contains(Who(*scope)))
                h.could.insert(Who(*scope), u"now"_s);
        } else if (!scope || *scope == u"THIS" || *scope == u"ROOT") {
            const Agg *a = e && agg.contains(e) ? &agg[e] : nullptr;
            if (!a)
                continue;
            auto who_of = [&](const ScopeKey &k) -> std::optional<Who> {
                if (k.first == u"p" && kind == u"cflag") {
                    const QString *o = world->owner.get(k.second.toLongLong());
                    return o ? std::optional<Who>(Who(*o)) : std::nullopt;
                }
                return k.first == u"p" ? Who(k.second.toLongLong()) : Who(k.second);
            };
            for (const auto &[k, _c] : a->per)
                if (const auto w = who_of(k))
                    h.could.insert(*w, u"now"_s);
            for (const auto &[k, _q] : a->pot)
                if (const auto w = who_of(k))
                    if (!h.could.contains(*w))
                        h.could.insert(*w, u"later"_s);
        } else if (*scope == u"FROM") {
            notes.append(u"“%1” sets it on whoever fired it (FROM)"_s.arg(name));
        } else {
            notes.append(u"“%1” sets it inside “%2”"_s.arg(name, *scope));
        }
    }
    for (const QString &n : notes)
        if (!h.notes.contains(n))
            h.notes.append(n);
    return h;
}

QString EventPanel::_names(const std::vector<Who> &keys0, size_t n)
{
    std::vector<Who> keys = keys0;
    std::stable_sort(keys.begin(), keys.end(), [this](const Who &a, const Who &b) {
        return (a.is_int() ? a.str() : _cname(a.text())) < (b.is_int() ? b.str() : _cname(b.text()));
    });
    QStringList out;
    for (size_t i = 0; i < std::min(n, keys.size()); ++i) {
        const Who &k = keys[i];
        if (!k.is_int()) {
            out.append(_cname(k.text()));
        } else {
            const auto *p = mod.provinces.get(k.num());
            out.append(p ? p->name : k.str());
        }
    }
    return out.join(u", "_s) + (keys.size() > n ? u" … (+%1)"_s.arg(keys.size() - n) : QString());
}

QString EventPanel::fill_key()
{
    // What the map's colours depend on (it is drawn again only when this changes).
    Entry *e = sel;
    const Agg *a = e && agg.contains(e) ? &agg[e] : nullptr;
    QStringList cats;
    if (e) {
        cats = cats_of(*e);
        if (cats.isEmpty())
            cats = file_cats(rel_of(*e));
    }
    const bool other = _kind() == u"mods" || _kind() == u"cbs";
    return QStringList{other ? u"1"_s : u"0"_s, e ? rel_of(*e) : u"\x01"_s, e ? uid_of(*e) : u"\x01"_s,
                       a ? QString::number(a->id) : u"\x01"_s, nation.value_or(u"\x01"_s),
                       cats.isEmpty() ? QString() : hex6(color(cats[0]))}
        .join(u'\x02');
}

// ---------------------------------------------------------------------------------------------- what the cursor is on
const EventPanel::PopIndex &EventPanel::_pop_index()
{
    if (!_pops) {
        PopIndex x;
        for (const auto &[pid, lst_] : doc->pops)
            for (const auto &p : lst_) {
                const long long n = p.size;
                x.tot[pid] += n;
                x.cul[p.culture.value_or(u"None"_s)][pid] += n;
                x.rlg[p.religion.value_or(u"None"_s)][pid] += n;
            }
        _pops = std::move(x);
    }
    return *_pops;
}

OrderedMap<long long, double> EventPanel::_share(const QString &kind, const QString &t)
{
    // {pid: share of its people} of a culture / culture group / religion / religion group.
    const PopIndex &pi = _pop_index();
    QStringList names;
    const QHash<QString, OrderedMap<long long, long long>> *table;
    if (kind == u"culture") {
        names = {t};
        table = &pi.cul;
    } else if (kind == u"cgroup") {
        for (const auto &[c, i] : mod.cultures)
            if (i.group == t)
                names.append(c);
        table = &pi.cul;
    } else if (kind == u"religion") {
        names = {t};
        table = &pi.rlg;
    } else {
        for (const auto &[r, i] : mod.religions)
            if (i.group == t)
                names.append(r);
        table = &pi.rlg;
    }
    OrderedMap<long long, long long> out;
    for (const QString &n : names) {
        const auto f = table->find(n);
        if (f != table->end())
            for (const auto &[pid, k] : f.value())
                out[pid] += k;
    }
    OrderedMap<long long, double> share;
    for (const auto &[pid, k] : out) {
        const long long tot = pi.tot.value(pid);
        if (tot)
            share.insert(pid, double(k) / double(tot));
    }
    return share;
}

QString EventPanel::_cname(const OptStr &tag) const
{
    if (!tag)
        return u"None"_s;
    const auto *c = mod.countries.get(*tag);
    return c ? c->name : *tag;
}

static QString culture_name(const moddata::Mod &m, const OptStr &c, const QString &dflt)
{
    const auto *x = c ? m.cultures.get(*c) : nullptr;
    return x ? x->name : dflt;
}

QStringList EventPanel::info_extra(const QString &kind, const QString &t)
{
    // Facts about a nation, province, state, culture, religion … at the start (for the hover and the panel).
    events::World *w = world.get();
    moddata::Mod &m = mod;
    if (!w)
        return {};
    QStringList out;
    if (kind == u"nation") {
        const countryhist::NationState *st = w->st(t);
        auto at = [st](const char16_t *k) { return st ? st->at(QString::fromUtf16(k)) : OptStr(); };
        const auto *owned = w->owned.get(t);
        const qsizetype n_owned = owned ? owned->size() : 0;
        const auto cap = judge ? judge->_capital(t) : std::nullopt;
        const auto *cp = cap ? m.provinces.get(*cap) : nullptr;
        out.append(u"%1 provinces%2 · capital %3"_s.arg(QString::number(n_owned),
                                                         w->gps.contains(t) ? u" · great power"_s : QString(),
                                                         cp ? cp->name : u"—"_s));
        const OptStr pc = at(u"primary_culture"), rl = at(u"religion"), gv = at(u"government");
        const auto *rp = rl ? m.religions.get(*rl) : nullptr;
        out.append(u"%1 · %2 · %3"_s.arg(culture_name(m, pc, pc && !pc->isEmpty() ? *pc : u"?"_s),
                                          rp ? rp->name : (rl && !rl->isEmpty() ? *rl : u"?"_s),
                                          gv && !gv->isEmpty() ? *gv : u"?"_s));
        if (!n_owned)
            out.append(u"<span style=\"color:#ff786e\">Doesn't exist at the start.</span>"_s);
    } else if (kind == u"province") {
        if (!py::isdigit(py::strip(t)))
            return out;
        const long long pid = py::to_int(t);
        const history::ProvState &pst = w->pst(pid);
        const QString *o = w->owner.get(pid);
        const auto *pr = m.provinces.get(pid);
        const OptStr reg = pr ? pr->region : std::nullopt;
        const auto *rg = reg ? m.regions.get(*reg) : nullptr;
        out.append(u"owner %1 · state %2"_s.arg(o && !o->isEmpty() ? _cname(*o) : u"nobody"_s,
                                               reg && !reg->isEmpty() ? (rg ? rg->name : *reg) : u"—"_s));
        out.append(u"trade goods %1 · life rating %2 · cores %3"_s.arg(
            pst.trade_goods && !pst.trade_goods->isEmpty() ? *pst.trade_goods : u"—"_s,
            pst.life_rating && *pst.life_rating ? QString::number(*pst.life_rating) : u"—"_s,
            pst.cores.isEmpty() ? u"—"_s : pst.cores.join(u", "_s)));
        const PopIndex &pi = _pop_index();
        const long long tot = pi.tot.value(pid);
        if (tot) {
            std::vector<std::pair<long long, QString>> top;
            for (auto it = pi.cul.begin(); it != pi.cul.end(); ++it)
                if (const long long k = it.value().value(pid))
                    top.emplace_back(k, it.key());
            std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return b < a; });   // reverse=True
            if (top.size() > 3)
                top.resize(3);
            QStringList parts;
            for (const auto &[n, c] : top)
                parts.append(u"%1 %2%"_s.arg(culture_name(m, c, c), QString::number(py::round_int(100.0 * double(n) / double(tot)))));
            out.append(u"%1 people: %2"_s.arg(py::fmt_commas(double(tot), 0), parts.join(u", "_s)));
        }
    } else if (kind == u"state") {
        if (const moddata::Region *r = m.regions.get(t)) {
            std::set<QString> owners;
            for (long long p : r->provinces)
                if (const QString *o = w->owner.get(p); o && !o->isEmpty())
                    owners.insert(*o);
            QStringList names;
            for (const QString &o : owners) {
                if (names.size() >= 6)
                    break;
                names.append(_cname(o));
            }
            out.append(u"%1 provinces · %2"_s.arg(QString::number(r->provinces.size()),
                                                 names.isEmpty() ? u"nobody owns it"_s : names.join(u", "_s)));
        }
    } else if (kind == u"culture" || kind == u"cgroup" || kind == u"religion" || kind == u"rgroup") {
        const OrderedMap<long long, double> share = _share(kind, t);
        const PopIndex &pi = _pop_index();
        py::Sum people;
        for (const auto &[p, s] : share)
            people.add(s * double(pi.tot.value(p)));
        out.append(u"%1 people in %2 provinces at the start"_s.arg(py::fmt_commas(double((long long)(people.value())), 0),
                                                                   QString::number(share.size())));
        if (kind == u"culture") {
            const auto *c = m.cultures.get(t);
            QStringList prim;
            for (const QString &x : w->tags)
                if (const auto *st = w->st(x); st && st->at(u"primary_culture"_s) == t)
                    prim.append(x);
            QStringList names;
            for (qsizetype i = 0; i < std::min<qsizetype>(6, prim.size()); ++i)
                names.append(_cname(prim[i]));
            out.append(u"group %1%2"_s.arg(c && !c->group.isEmpty() ? c->group : u"?"_s,
                                           !prim.isEmpty() ? u" · primary culture of "_s + names.join(u", "_s) : QString()));
        }
        if (kind == u"religion") {
            QStringList sr;
            for (const QString &x : w->tags)
                if (const auto *st = w->st(x); st && st->at(u"religion"_s) == t)
                    sr.append(x);
            if (!sr.isEmpty()) {
                QStringList names;
                for (qsizetype i = 0; i < std::min<qsizetype>(6, sr.size()); ++i)
                    names.append(_cname(sr[i]));
                out.append(u"state religion of %1%2"_s.arg(names.join(u", "_s), sr.size() > 6 ? u" …"_s : QString()));
            }
        }
    } else if (kind == u"goods") {
        long long n = 0;
        for (const auto &[p, _o] : w->owner)
            if (w->pst(p).trade_goods == t)
                ++n;
        out.append(u"produced in %1 provinces at the start"_s.arg(n));
    } else if (kind == u"modifier") {
        if (const auto attrs = _mod_attrs(t)) {
            const QStringList eff = modpanel::effects_text(m, *attrs);
            QStringList escd;
            for (qsizetype i = 0; i < std::min<qsizetype>(14, eff.size()); ++i)
                escd.append(esc(eff[i]));
            out.append(u"<b>Does</b>: %1"_s.arg(!eff.isEmpty() ? u"<br>&nbsp;• "_s + escd.join(u"<br>&nbsp;• "_s) +
                                                                     (eff.size() > 14 ? u" …"_s : QString())
                                                               : u"nothing by itself — a marker events check"_s));
            modifiers::ModifierEdits *me = mods_store(doc);
            if (me && me->exists(t))
                out.append(u"<span style=\"color:#8a8272\">Edit its effects on the right (click it) — or its "
                           u"text: Ctrl+click</span>"_s);
        }
    } else if (kind == u"cflag" || kind == u"pflag") {
        const Holders h = flag_holders(kind, t);
        if (!h.hist.isEmpty())
            out.append(u"<b>Held at the start</b> (history): %1"_s.arg(
                esc(_names(std::vector<Who>(h.hist.begin(), h.hist.end())))));
        else
            out.append(u"<b>Held at the start</b>: by nobody"_s);
        std::vector<Who> now, later;
        for (const auto &[k, v] : h.could)
            (v == u"now" ? now : later).push_back(k);
        if (!now.empty())
            out.append(u"<b>Can get it</b> (its setters can happen to them at the start): %1 — %2"_s.arg(
                QString::number(now.size()), esc(_names(now))));
        if (!later.empty())
            out.append(u"<b>Could get it later</b>: %1 — %2"_s.arg(QString::number(later.size()), esc(_names(later))));
        for (qsizetype i = 0; i < std::min<qsizetype>(4, h.notes.size()); ++i)
            out.append(esc(h.notes[i]));
    } else if (kind == u"gflag") {
        const auto *hist = db->history_holders().get({kind, t});
        out.append(u"<b>Set at the start</b>: %1"_s.arg(hist && !hist->isEmpty() ? u"yes (history)"_s : u"no"_s));
    }
    return out;
}

void EventPanel::_head(QLabel *lbl, const QString &text, const Rgb &rgb)
{
    // A heading bar: what this part of the panel is about (in the colour of its kind).
    lbl->setTextFormat(Qt::PlainText);
    lbl->setText(text);
    lbl->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; font-size: 15px; "
                       u"padding: 6px 10px; border-radius: 4px; }"_s.arg(rgb_css(rgb)));
}

void EventPanel::_fill_sym()
{
    if (!sym || !refs) {
        sym_sec->setVisible(false);
        return;
    }
    const QString kind = sym->kind, t = sym->target;
    if (kind == u"cb" && _kind() == u"cbs" && cb_view->cur && t == *cb_view->cur) {
        sym_sec->setVisible(false);          // (the war goal picked itself: it is read out right below)
        return;
    }
    const auto *kd = refs::KINDS.get(kind);
    const QString klabel = kd ? kd->first : kind;
    const Rgb krgb = kd ? kd->second : Rgb{200, 200, 200};
    _head(sym_head, u"%1 %2: %3"_s.arg(sym_on_map ? u"Selected on the map —"_s : u"Selected"_s, py::lower(klabel),
                                       refs->name_of(kind, t)),
          krgb);
    QStringList parts{u"<div style=\"font-size:15px\">%1</div>"_s.arg(refs->info_html(kind, t, info_extra(kind, t), false))};
    sym_tree->clear();
    if (!refs->usages(kind, t).empty()) {
        sym_sum->setText(u"<h4>Where it is in the scripts</h4>%1<br><span style='color:#8a8272'>files and events "
                         u"with the most mentions first · ▸ opens one · a click goes there</span>"_s.arg(
                             relview::usage_summary(*refs, *db, kind, t)));
        const long long n = sym_tree->add_usage(nullptr, *refs, *db,
                                                [this](const QString &r) { return store->text(r); }, kind, t);
        long long rows = 0;
        for (int i = 0; i < n; ++i) {
            QTreeWidgetItem *it = sym_tree->topLevelItem(i);
            rows += 1 + (it->isExpanded() ? it->childCount() : 0);
        }
        sym_tree->setFixedHeight(int(std::max<long long>(150, std::min<long long>(380, 34 + 26 * rows))));
        sym_sum->setVisible(true);
        sym_tree->setVisible(true);
    } else {
        sym_sum->setVisible(false);
        sym_tree->setVisible(false);
    }
    static const QHash<QString, QString> LOOK = {
        {u"nation"_s, u"a thick bright outline around its land"_s},
        {u"province"_s, u"cyan rim and ◆ pin"_s},
        {u"cflag"_s, u"the nations that hold it or can get it — outlined"_s},
        {u"state"_s, u"a dashed outline"_s},
        {u"culture"_s, u"a green rim inside each of its provinces, hatched (denser = more of the people)"_s},
        {u"cgroup"_s, u"a green rim inside each of its provinces, hatched (denser = more of the people)"_s},
        {u"religion"_s, u"a yellow rim inside each of its provinces, level hatching (denser = more of the people)"_s},
        {u"rgroup"_s, u"a yellow rim inside each of its provinces, level hatching (denser = more of the people)"_s},
        {u"goods"_s, u"orange rims (where it is produced)"_s},
        {u"continent"_s, u"a dashed outline"_s}};
    const QString look = LOOK.value(kind);
    if (!look.isEmpty())
        parts.append(u"<div style=\"color:#8a8272\">On the map (🗺 Map above the text): %1.</div>"_s.arg(look));
    if (sym_on_map)
        parts.append(u"<div style=\"color:#8a8272\">Chosen on the map (right-click) — lit in the text and in the "
                     u"list above.</div>"_s);
    sym_body->setText(parts.join(QString()));
    modifiers::ModifierEdits *mods = mods_store(doc);
    if (kind == u"modifier" && mods && mods->exists(t)) {
        sym_form->refresh(doc, db.get(), _mod_used_map());
        sym_form->show_mod(t);
        sym_form->setVisible(true);
    } else {
        sym_form->setVisible(false);
    }
    sym_sec->setVisible(true);
}

static OrderedMap<long long, double> ones(const std::vector<long long> &pids)
{
    OrderedMap<long long, double> out;
    for (long long p : pids)
        out.insert(p, 1.0);
    return out;
}

std::pair<std::vector<FocusLayer>, std::vector<long long>> EventPanel::focus_spec()
{
    // What the map marks: every province the selected text — or else the event / decision at the cursor — names
    // (cyan hatching; the nations a selection names: outlines), and the word at the cursor in the look of its kind.
    set_pins.clear();
    if (!world || _kind() == u"mods")
        return {};
    events::World &w = *world;
    std::vector<FocusLayer> base;
    std::vector<refs::Ref> src = _sel_refs;
    if (src.empty() && _kind() == u"cbs") {  // the provinces the war goal picked names
        const auto sp = cb_view->cur ? cb_view->span(*cb_view->cur) : std::nullopt;
        if (sp && refs)
            src = refs->in_range(cbcheck::REL, sp->first, sp->second);
    } else if (src.empty() && sel && refs) {
        src = refs->in_range(rel_of(*sel), sel->span.first, sel->span.second);
    }
    std::set<long long> provs;
    for (const refs::Ref &x : src)
        if (x.kind == u"province")
            provs.insert(py::to_int(x.target));
    set_pins.assign(provs.begin(), provs.end());
    if (!provs.empty())
        base.push_back(FocusLayer{u"provset"_s, std::nullopt, ones(set_pins)});
    if (!_sel_refs.empty()) {
        QStringList tags;
        for (const refs::Ref &x : _sel_refs)
            if (x.kind == u"nation" && !tags.contains(x.target))
                tags.append(x.target);
        if (!tags.isEmpty()) {
            OrderedMap<long long, double> pids;
            for (const QString &t : tags)
                if (const auto *o = w.owned.get(t))
                    for (long long p : *o)
                        pids.insert(p, 1.0);
            base.push_back(FocusLayer{u"outline"_s, std::nullopt, pids});
        }
    }
    if (nation && !nation->isEmpty()) {      // the nation clicked for its own numbers: white
        OrderedMap<long long, double> pids;
        if (const auto *o = w.owned.get(*nation))
            for (long long p : *o)
                pids.insert(p, 1.0);
        base.push_back(FocusLayer{u"outline"_s, Rgb{250, 250, 250}, pids});
    }
    auto [layers, pins] = _sym_focus();
    base.insert(base.end(), layers.begin(), layers.end());
    return {base, pins};
}

std::pair<std::vector<FocusLayer>, std::vector<long long>> EventPanel::_sym_focus()
{
    if (!sym)
        return {};
    const QString kind = sym->kind, t = sym->target;
    events::World &w = *world;
    auto owned_of = [&w](const QStringList &tags) {
        OrderedMap<long long, double> pids;
        for (const QString &x : tags)
            if (const auto *o = w.owned.get(x))
                for (long long p : *o)
                    pids.insert(p, 1.0);
        return pids;
    };
    if (kind == u"cflag") {
        const Holders h = flag_holders(kind, t);
        QStringList tags;
        for (const Who &k : h.hist)
            if (!k.is_int() && !tags.contains(k.text()))
                tags.append(k.text());
        for (const auto &[k, _v] : h.could)
            if (!k.is_int() && !tags.contains(k.text()))
                tags.append(k.text());
        return {{FocusLayer{u"outline"_s, std::nullopt, owned_of(tags)}}, {}};
    }
    if (kind == u"nation")
        return {{FocusLayer{u"outline"_s, std::nullopt, owned_of({t})}}, {}};
    if (kind == u"province") {
        if (!py::isdigit(py::strip(t)))
            return {};
        const long long p = py::to_int(t);
        return {{FocusLayer{u"provinces"_s, std::nullopt, ones({p})}}, {p}};
    }
    if (kind == u"state" && mod.regions.contains(t))
        return {{FocusLayer{u"dashed"_s, std::nullopt, ones(mod.regions.get(t)->provinces)}}, {}};
    if (kind == u"culture" || kind == u"cgroup")
        return {{FocusLayer{u"dots"_s, std::nullopt, _share(kind, t)}}, {}};
    if (kind == u"religion" || kind == u"rgroup")
        return {{FocusLayer{u"lines"_s, std::nullopt, _share(kind, t)}}, {}};
    if (kind == u"goods") {                  // orange (the event's own provinces are cyan)
        OrderedMap<long long, double> pids;
        for (const auto &[p, _o] : w.owner)
            if (w.pst(p).trade_goods == t)
                pids.insert(p, 1.0);
        return {{FocusLayer{u"provinces"_s, Rgb{255, 150, 40}, pids}}, {}};
    }
    if (kind == u"continent") {
        OrderedMap<long long, double> pids;
        for (auto it = w.cont.begin(); it != w.cont.end(); ++it)
            if (it.value() == t)
                pids.insert(it.key(), 1.0);
        return {{FocusLayer{u"dashed"_s, std::nullopt, pids}}, {}};
    }
    return {};
}

// ---------------------------------------------------------------------------------------------- data
void EventPanel::refresh(document::Document *d, const OptStr &)
{
    if (d != doc) {
        doc = d;
        _stale = true;
        if (db)                              // (C++: what still points into the old one stays valid)
            _old_db = std::move(db);         // read again: the categories written on Apply (db = None)
        _pops.reset();
        if (!d) {                            // (C++ only: the document is about to go — nothing may use it)
            _timer->stop();
            _reparse_timer->stop();
            _pending.clear();
            _queue.clear();
            _gen.reset();
            store = nullptr;
            if (ws)
                ws->set_store(nullptr);
            for (scriptedit::ScriptWorkspace *x : extra_ws)
                x->set_store(nullptr);
        }
        if (d) {
            QPointer<EventPanel> self(this);
            d->listeners.push_back([self](const QSet<long long> &pids) {
                if (self)
                    self->_doc_changed(pids);
            });
            store = d->maps.contains(u"scripts"_s) ? &d->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
            if (ws)
                ws->set_store(store);
        }
    }
    if (!d)
        return;
    reparse_now();
    if (!db) {
        db = std::make_unique<EventDB>(mod, store);
        std::unique_ptr<refs::RefIndex> old_refs = std::move(refs);
        if (store)
            refs = std::make_unique<refs::RefIndex>(mod, *db, [this](const QString &r) { return store->text(r); },
                                                    _extra_rels());
        _pending.clear();
        if (ws)
            ws->set_refs(refs.get());
        for (scriptedit::ScriptWorkspace *x : extra_ws)   // (they held the old index: it is gone)
            if (x->refs == old_refs.get())
                x->set_refs(refs.get());
        sel = nullptr;
        _mention_cache.clear();
        _fires.reset();
    }
    if (events::EventCats *cats = cats_store(d))
        cats->attach(db.get());
    if (_stale || !_sig || !(*_sig == _world_sig()))
        _start();
    else if ((!_queue.empty() || _gen) && !_timer->isActive())
        _timer->start();                     // (it waited while another page was open)
    _fill_cat_box();
    _fill_list();
    _fill_detail();
}

WorldSig EventPanel::_world_sig()
{
    // What the answers depend on: the history edits and the nations' edits (not the categories).
    WorldSig s;
    for (const auto &[p, st] : doc->prov_want)
        s.prov.emplace_back(p, st);
    std::stable_sort(s.prov.begin(), s.prov.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    if (doc->maps.contains(u"nations"_s)) {
        for (const auto &[t, st] : doc->map<countryhist::NationEdits>(u"nations"_s).want)
            s.nats.emplace_back(t, countryhist::key_of(st));
        std::stable_sort(s.nats.begin(), s.nats.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    } else {
        s.prov.clear();
    }
    return s;
}

void EventPanel::_doc_changed(const QSet<long long> &pids)
{
    if (!pids.isEmpty())
        _pops.reset();
    if (doc && _sig && !(_world_sig() == *_sig))
        _stale = true;                       // recomputed the next time the tab is shown
}

void EventPanel::_start()
{
    _stale = false;
    _sig = _world_sig();
    world = std::make_unique<events::World>(mod, *doc);
    judge = std::make_unique<events::Judge>(*world, *db);
    pjudge = std::make_unique<events::PotentialJudge>(*world, *db);
    agg.clear();
    _queue.clear();
    for (const auto &e : db->entries)
        _queue.push_back(e.get());
    std::stable_sort(_queue.begin(), _queue.end(), [](Entry *a, Entry *b) { return !a->province() && b->province(); });
    _total = qsizetype(_queue.size());       // nations first (fast), provinces after
    _gen.reset();
    _timer->start();
}

void EventPanel::_work()
{
    // A slice of the chances (≈ 20 ms, then the window gets its turn: scrolling / typing never stall).
    if (QApplication::mouseButtons() != Qt::NoButton)   // something is being dragged: this waits
        return;
    if (!isVisible()) {                      // another page is open: it goes on when this one is
        _timer->stop();
        return;
    }
    QElapsedTimer clock;
    clock.start();
    while (clock.nsecsElapsed() < 20'000'000) {
        if (!_gen) {
            if (_queue.empty())
                break;
            Entry *e = _queue.front();
            _queue.erase(_queue.begin());
            _gen = Gen{e, events::scopes_of(*world, *e)};
        }
        if (!_step(*_gen))
            _gen.reset();
    }
    const qsizetype done = _total - qsizetype(_queue.size());
    if (!_queue.empty() || _gen) {
        progress->setText(u"Working out who can have what at the start… %1 / %2"_s.arg(done).arg(_total));
    } else {
        _timer->stop();
        progress->setText(QString());
        _fill_list();
        _fill_detail();
        Q_EMIT changed();
    }
}

bool EventPanel::_step(Gen &g)
{
    // Works out entry e (agg[e] at the end), pausing every 60 nations / provinces.
    Agg &a = g.a;
    const Entry &e = *g.e;
    while (g.i < g.scopes.size()) {
        if (g.i && g.i % 60 == 0 && !g.paused) {
            g.paused = true;
            return true;
        }
        g.paused = false;
        const Scope &sc = g.scopes[g.i++];
        events::Chance c = events::chance(*judge, e, sc);
        if (c.can == Tri(false)) {
            const Tri q = events::potential(*pjudge, e, sc);
            if (q != Tri(false))
                a.pot.insert(scope_key(sc), q);   // not at the start — but who / where fits: it could later
            continue;
        }
        if (c.can == Tri(true))
            ++a.sure;
        else
            ++a.maybe;
        a.pmax = std::max(a.pmax, c.p.second);
        a.times.first += c.times.first;
        a.times.second += c.times.second;
        if (!e.is_event() && c.allow == Tri(true))
            ++a.allowed;
        a.per.insert(scope_key(sc), std::move(c));
    }
    a.cat = events::classify(e);
    if (a.cat.kind == u"player" && a.per.size()) {   // one nation has it: the player's
        bool first = true;
        for (const auto &[_k, c] : a.per) {
            if (first) {
                a.times = c.times;
                first = false;
            } else {
                a.times.first = std::max(a.times.first, c.times.first);
                a.times.second = std::max(a.times.second, c.times.second);
            }
        }
    }
    a.id = ++_agg_ids;
    agg.insert(g.e, std::move(a));
    return false;
}

std::optional<NationChance> EventPanel::nation_chance(const Entry &e)
{
    // (can, (p lo, p hi), times, mtth, allow) of entry e for the chosen nation (a province event: any of its
    // provinces).
    const auto f = agg.constFind(&e);
    if (f == agg.cend() || !nation || nation->isEmpty())
        return std::nullopt;
    const Agg &a = f.value();
    const NationChance none{false, {0.0, 0.0}, {0.0, 0.0}, std::nullopt, false};
    if (!e.province()) {
        const events::Chance *c = a.per.get({u"c"_s, *nation});
        if (!c)
            return none;
        return NationChance{c->can, c->p, c->times, c->mtth, c->allow};
    }
    std::vector<const events::Chance *> cs;
    for (const auto &[k, c] : a.per) {
        const QString *o = world->owner.get(k.second.toLongLong());
        if (o && *o == *nation)
            cs.push_back(&c);
    }
    if (cs.empty())
        return none;
    double keep_lo = 1.0, keep_hi = 1.0;
    std::vector<Tri> cans;
    py::Sum t0, t1;
    for (const events::Chance *c : cs) {
        keep_lo *= 1 - c->p.first;
        keep_hi *= 1 - c->p.second;
        cans.push_back(c->can);
        t0.add(c->times.first);
        t1.add(c->times.second);
    }
    return NationChance{or3(cans), {1 - keep_lo, 1 - keep_hi}, {t0.value(), t1.value()}, cs[0]->mtth, std::nullopt};
}

QString EventPanel::words(const QString &text)
{
    // The script's placeholders ($COUNTRY$, $COUNTRY_ADJ$, $CAPITAL$ …): the chosen nation's words, else ‹nation›
    // markers.
    if (!text.contains(u'$'))
        return text;
    const OptStr tag = nation && !nation->isEmpty() ? nation : std::nullopt;
    OptStr name, adj, cap, cul;
    if (tag) {
        name = _cname(*tag);
        const QString a = mod.loc.value(*tag + u"_ADJ"_s);
        adj = !a.isEmpty() ? a : *name;
        if (world) {
            const auto c = judge ? judge->_capital(*tag) : std::nullopt;
            const auto *p = c ? mod.provinces.get(*c) : nullptr;
            if (p)
                cap = p->name;
            if (const countryhist::NationState *st = world->st(*tag)) {
                const OptStr pc = st->at(u"primary_culture"_s);
                const auto *x = pc ? mod.cultures.get(*pc) : nullptr;
                if (x)
                    cul = x->name;
            }
        }
    }
    const QHash<QString, OptStr> known = {
        {u"COUNTRY"_s, name},
        {u"COUNTRY_ADJ"_s, adj},
        {u"CAPITAL"_s, cap},
        {u"CULTURE"_s, cul},
        {u"CULTURE_ADJ"_s, cul},
        {u"FROMCOUNTRY"_s, u"‹the other nation›"_s},
        {u"FROMCOUNTRY_ADJ"_s, u"‹the other nation's›"_s},
        {u"PROVINCENAME"_s, u"‹the province›"_s},
        {u"STATENAME"_s, u"‹the state›"_s},
        {u"MONARCHTITLE"_s, u"‹the ruler›"_s},
        {u"CB_TARGET_NAME"_s, u"‹the target›"_s},
        {u"CB_TARGET_NAME_ADJ"_s, u"‹the target's›"_s}};
    static const QHash<QString, QString> generic = {{u"COUNTRY"_s, u"‹the nation›"_s},
                                                    {u"COUNTRY_ADJ"_s, u"‹the nation's›"_s},
                                                    {u"CAPITAL"_s, u"‹the capital›"_s},
                                                    {u"CULTURE"_s, u"‹the culture›"_s},
                                                    {u"CULTURE_ADJ"_s, u"‹the culture's›"_s}};
    static const QRegularExpression RX(u"\\$([A-Za-z_]+)\\$"_s);
    QString out;
    qsizetype last = 0;
    auto it = RX.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString k = py::upper(m.captured(1));
        const OptStr kv = known.value(k);
        if (kv && !kv->isEmpty())
            out += *kv;
        else if (!generic.value(k).isEmpty())
            out += generic.value(k);
        else
            out += u"‹%1›"_s.arg(py::lower(k).replace(u'_', u' '));
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

QString EventPanel::nm(const Entry &e) { return words(db->name(e)); }

// ---------------------------------------------------------------------------------------------- the nation
void EventPanel::set_nation(const OptStr &tag)
{
    nation = tag;
    _show_nation();
    _fill_list();
    _fill_detail();
    Q_EMIT changed();
}

void EventPanel::_show_nation()
{
    const bool on = nation && !nation->isEmpty();
    if (on)
        nation_lbl->setText(u"For %1 (%2) — click another nation on the map to switch"_s.arg(_cname(*nation), *nation));
    else
        nation_lbl->setText(u"For any nation — click a nation on the map to see its own numbers"_s);
    clear_nation->setVisible(on);
    only_nation->setEnabled(on);
}

// ---------------------------------------------------------------------------------------------- the list
QString EventPanel::_kind() const
{
    for (const auto &[k, b] : kind_btn)
        if (b->isChecked())
            return k;
    return QString();
}

bool EventPanel::_folder_ok(const QString &rel) const
{
    const QString kind = _kind();
    return kind == u"both" || rel.startsWith(kind == u"event" ? u"events/"_s : u"decisions/"_s);
}

bool EventPanel::_cat_ok(const Entry &e)
{
    const QString want = cat_box->currentData().toString();
    if (want.isEmpty() || want == u"__all")
        return true;
    QSet<QString> names;
    for (const QString &n : cats_of(e))
        names.insert(n);
    for (const QString &n : file_cats(rel_of(e)))
        names.insert(n);
    return want == u"__none" ? names.isEmpty() : names.contains(want);
}

bool EventPanel::_wanted(const Entry &e)
{
    if (!_folder_ok(rel_of(e)))
        return false;
    const QString show = show_box->currentData().toString();
    const bool mt = e.mtth && *e.mtth;
    if (show == u"mtth" && !(e.is_event() && mt && !e.triggered_only))
        return false;
    if (show == u"fired" && !(e.is_event() && (e.triggered_only || !mt)))
        return false;
    if (show == u"once" && !e.once)
        return false;
    if (show == u"major" && !e.major)
        return false;
    if (show == u"province" && !e.province())
        return false;
    if (!_cat_ok(e))
        return false;
    const QString q = py::lower(py::strip(search->text()));
    if (!q.isEmpty() &&
        !py::lower(u"%1 %2 %3 %4 %5 %6"_s.arg(db->name(e), e.key, e.id.value_or(QString()), e.file,
                                              cats_of(e).join(u' '), file_cats(rel_of(e)).join(u' ')))
             .contains(q))
        return false;
    if (nation && !nation->isEmpty() && only_nation->isChecked()) {
        const auto nc = nation_chance(e);
        if (!nc || nc->can == Tri(false))
            return false;
    }
    if (only_sym->isChecked() && sym && !_sym_hits.contains({rel_of(e), uid_of(e)}))
        return false;
    return true;
}

std::optional<std::pair<double, double>> EventPanel::times(const Entry &e)
{
    // (lo, hi) of how often it happens within 10 years — for the chosen nation, else everywhere together; None when
    // it doesn't happen by itself (a decision, an event only something fires).
    if (!e.is_event() || e.triggered_only || !(e.mtth && *e.mtth))
        return std::nullopt;
    const auto f = agg.constFind(&e);
    if (f == agg.cend())
        return std::nullopt;
    const auto nc = nation_chance(e);
    return nc ? nc->times : f.value().times;
}

double EventPanel::fired_state(const Entry &e)
{
    // An event only something fires / a decision: can it be (1 = yes, 0.5 = maybe, 0 = no) — for the chosen nation,
    // else anywhere.
    const auto f = agg.constFind(&e);
    if (f == agg.cend())
        return 0;
    const Agg &a = f.value();
    if (const auto nc = nation_chance(e)) {
        if (!e.is_event())
            return truthy(nc->can) && truthy(nc->allow) ? 1 : truthy(nc->can) ? 0.75 : !nc->can ? 0.5 : 0;
        return truthy(nc->can) ? 1 : !nc->can ? 0.5 : 0;
    }
    if (!e.is_event())
        return a.allowed ? 1 : a.sure ? 0.75 : a.maybe ? 0.5 : 0;
    return a.sure ? 1 : a.maybe ? 0.5 : 0;
}

QString EventPanel::_fmt_times(const std::pair<double, double> &t)
{
    const auto [lo, hi] = t;
    auto f = [](double x) {
        return x >= 9.5 ? py::fmt_f(x, 0) : x >= 0.05 ? py::fmt_f(x, 1) : x == 0 ? u"0"_s : u"<0.1"_s;
    };
    return hi - lo < 0.05 * std::max(1.0, hi) ? f(hi) : u"%1–%2"_s.arg(f(lo), f(hi));
}

QString EventPanel::_times_text(const Entry &e)
{
    const auto t = times(e);
    if (!t) {
        const double st = fired_state(e);
        if (!e.is_event())
            return st == 1 ? u"can take"_s : st == 0.75 ? u"sees it"_s : st == 0.5 ? u"maybe"_s : u"—"_s;
        return st ? u"when fired"_s : u"—"_s;
    }
    return _fmt_times(*t);
}

SortKey EventPanel::_sort_value(const Entry &e, int col)
{
    if (col == 0)
        return {0, 0, 0, py::lower(nm(e))};
    if (col == 1) {
        const QString id = e.id.value_or(QString());
        return py::isdigit(id) ? SortKey{0, double(py::to_int(id)), 0, QString()} : SortKey{1, 0, 0, py::lower(e.key)};
    }
    if (col == 2)
        return {0, double(e.span.first), 0, QString()};   // within a file: the file's own order
    if (col == 3)
        return e.is_event() && e.mtth && *e.mtth && !e.triggered_only ? SortKey{0, *e.mtth, 0, QString()}
                                                                       : SortKey{1, 0, 0, QString()};
    if (const auto t = times(e))
        return {0, -t->second, -t->first, QString()};
    return {1, -fired_state(e), 0, QString()};
}

SortKey EventPanel::_file_value(const QString &rel, const std::vector<Entry *> &entries, int col)
{
    if (col == 0 || col == 2)
        return {0, 0, 0, py::lower(py::split(rel, u"/"_s, 1).value(1))};
    if (col == 1)
        return {0, double(entries.size()), 0, QString()};
    if (col == 3) {
        std::optional<double> lo;
        for (Entry *e : entries)
            if (e->is_event() && e->mtth && *e->mtth && !e->triggered_only)
                lo = lo ? std::min(*lo, *e->mtth) : *e->mtth;
        return lo ? SortKey{0, *lo, 0, QString()} : SortKey{1, 0, 0, QString()};
    }
    py::Sum tot;
    for (Entry *e : entries) {
        if (const auto t = times(*e))
            tot.add(t->second);
        else
            tot.add(0LL);
    }
    return {0, -tot.value(), 0, QString()};
}

void EventPanel::_header_clicked(int col)
{
    if (col == _sort_col) {
        _sort_desc = !_sort_desc;
    } else {
        _sort_col = col;
        _sort_desc = false;
    }
    lst->header()->setSortIndicator(col, _sort_desc ? Qt::DescendingOrder : Qt::AscendingOrder);
    _fill_list();
}

void EventPanel::_open_all(bool on)
{
    if (!db)
        return;
    if (_group == u"file") {
        open_files.clear();
        if (on)
            for (const QString &r : db->files)
                open_files.insert(r);
    } else {
        open_cats.clear();
        if (on) {
            for (const QString &n : cat_order())
                open_cats.insert(n);
            open_cats.insert(u"__none"_s);
        } else {
            open_cat_files.clear();
        }
    }
    _fill_list();
}

void EventPanel::_set_kind_vis()
{
    const QString kind = _kind();
    const bool other = kind == u"mods" || kind == u"cbs";
    for (QWidget *w : std::initializer_list<QWidget *>{filters, list_sec, detail, legend_sec})
        w->setVisible(!other);
    mod_view->setVisible(kind == u"mods");
    cb_view->setVisible(kind == u"cbs");
    cb_view->detail_sec->setVisible(kind == u"cbs");
    blocks_btn->setEnabled(kind != u"cbs");   // (the blocks build events and decisions)
    blocks_btn->setToolTip(kind != u"cbs" ? _blocks_tip
                                          : u"The blocks build events and decisions. A war goal is written in the "
                                            u"editor — its parts are read out below the list."_s);
}

void EventPanel::_kind_changed()
{
    _set_kind_vis();
    _fill_list();
    Q_EMIT changed();
}

const QSet<const Entry *> &EventPanel::_mentions(const QString &tag)
{
    // The entries whose script has the tag (as a word).
    auto f = _mention_cache.find(tag);
    if (f == _mention_cache.end()) {
        const QRegularExpression rx(u"(?<![A-Za-z0-9_])%1(?![A-Za-z0-9_])"_s.arg(QRegularExpression::escape(tag)));
        QSet<const Entry *> got;
        for (const auto &e : db->entries)   // (rx.search(text, a, b): the text as if it ended at b)
            if (rx.matchView(QStringView(e->text).left(std::min(e->span.second, e->text.size())), e->span.first).hasMatch())
                got.insert(e.get());
        f = _mention_cache.insert(tag, got);
    }
    return f.value();
}

int EventPanel::_mark(const Entry &e)
{
    // For the chosen nation: 1 = its tag is in the entry, 2 = it can happen to it, 3 = both.
    if (!nation || nation->isEmpty() || !db)
        return 0;
    int m = _mentions(*nation).contains(&e) ? 1 : 0;
    const auto nc = nation_chance(e);
    if (nc && nc->can != Tri(false))
        m |= 2;
    return m;
}

void EventPanel::_fill_marks(const std::vector<Entry *> &rows)
{
    if (!nation || nation->isEmpty()) {
        mark_lbl->setVisible(false);
        return;
    }
    int c[4] = {0, 0, 0, 0};
    for (Entry *e : rows)
        ++c[_mark(*e)];
    auto sq = [](int k) { return u"<span style=\"color:%1; font-size:17px\">■</span>"_s.arg(rgb_css(mark_rgb(k))); };
    mark_lbl->setText(u"For <b>%1</b>: %2 names its tag (%3) &nbsp; %4 can happen to it (%5) &nbsp; %6 both (%7)"_s.arg(
        esc(_cname(*nation)), sq(1), QString::number(c[1]), sq(2), QString::number(c[2]), sq(3), QString::number(c[3])));
    mark_lbl->setVisible(true);
}

void EventPanel::_set_group(const QString &k)
{
    _group = k;
    _fill_list();
}

void EventPanel::_expanded(QTreeWidgetItem *it, bool on)
{
    const RowData d = row_data(it);
    if (d.kind == u'c') {
        on ? (void)open_cats.insert(d.key) : (void)open_cats.remove(d.key);
    } else if (d.kind == u'f' && !d.group) {
        on ? (void)open_files.insert(d.key) : (void)open_files.remove(d.key);
    } else if (d.kind == u'f') {
        on ? (void)open_cat_files.insert({*d.group, d.key}) : (void)open_cat_files.erase({*d.group, d.key});
    }
}

QTreeWidgetItem *EventPanel::_row(Entry *e, const QFont &italic, const QBrush &grey, const OptStr &group)
{
    const bool mt = e->mtth && *e->mtth;
    const QString mtt = e->is_event() && mt && !e->triggered_only ? events::fmt_days(e->mtth) : u"—"_s;
    auto *it = new QTreeWidgetItem(QStringList{nm(*e), e->is_event() ? e->id.value_or(u"None"_s) : e->key, e->file, mtt,
                                               _times_text(*e)});
    const QStringList cats = cats_of(*e);
    std::vector<Rgb> rgbs;
    for (const QString &c : cats)
        rgbs.push_back(color(c));
    it->setIcon(0, cats_icon(rgbs));
    set_row_data(it, u'e', rel_of(*e), e, group);
    it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    for (int c : {3, 4})
        it->setTextAlignment(c, Qt::AlignCenter);
    const bool fired = e->is_event() && (e->triggered_only || !mt);
    if (fired)
        for (int c = 0; c < COLS.size(); ++c)
            it->setFont(c, italic);
    if (agg.contains(e)) {
        const auto t = times(*e);
        if ((t ? t->second : 0.0) == 0 && fired_state(*e) == 0)
            for (int c = 0; c < COLS.size(); ++c)
                it->setForeground(c, grey);
    }
    if (const int mk = _mark(*e)) {
        const QBrush br(qc(mark_rgb(mk), 95));
        for (int c = 0; c < COLS.size(); ++c)
            it->setBackground(c, br);
    }
    it->setToolTip(0, nm(*e) + (!cats.isEmpty() ? u"\ncategories: "_s + cats.join(u", "_s) : QString()) +
                          (fired ? u"\nonly when something fires it"_s : QString()));
    const auto f = _prob_entry.find({rel_of(*e), uid_of(*e)});
    if (f != _prob_entry.end() && !f->second.empty()) {
        QStringList texts;
        bool err = false;
        for (const auto &p : f->second) {
            texts.append(p.text);
            err = err || p.sev == u"error";
        }
        _mark_problem(it, err ? u"error"_s : u"warn"_s, texts);
    }
    return it;
}

std::vector<Entry *> EventPanel::_sorted(std::vector<Entry *> rows)
{
    std::vector<std::pair<SortKey, Entry *>> keyed;
    for (Entry *e : rows)
        keyed.emplace_back(_sort_value(*e, _sort_col), e);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    rows.clear();
    for (const auto &[_k, e] : keyed)
        rows.push_back(e);
    if (_sort_desc)
        std::reverse(rows.begin(), rows.end());
    return rows;
}

QTreeWidgetItem *EventPanel::_file_row(const QString &rel, const std::vector<Entry *> &rows, const OptStr &group,
                                       const QFont &bold)
{
    const QStringList fcats = file_cats(rel);
    long long n_e = 0;
    py::Sum tot;
    for (Entry *e : rows) {
        n_e += e->is_event() ? 1 : 0;
        if (const auto t = times(*e))
            tot.add(t->second);
        else
            tot.add(0LL);
    }
    const QStringList parts = py::split(rel, u"/"_s, 1);
    const double tv = tot.value();
    auto *top = new QTreeWidgetItem(QStringList{parts.value(1), QString::number(rows.size()),
                                                !fcats.isEmpty() ? fcats.join(u", "_s) : parts.value(0), QString(),
                                                tv ? _fmt_times({tv, tv}) : QString()});
    std::vector<Rgb> rgbs;
    for (const QString &c : fcats)
        rgbs.push_back(color(c));
    top->setIcon(0, cats_icon(rgbs));
    set_row_data(top, u'f', rel, nullptr, group);
    for (int c = 0; c < COLS.size(); ++c)
        top->setFont(c, bold);
    for (int c : {1, 4})
        top->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    top->setToolTip(0, u"%1 — %2 events, %3 decisions%4"_s.arg(
                           rel, QString::number(n_e), QString::number(qsizetype(rows.size()) - n_e),
                           !fcats.isEmpty() ? u"\ncategories: "_s + fcats.join(u", "_s) : QString()));
    top->setToolTip(2, !fcats.isEmpty() ? fcats.join(u", "_s) : u"no category of its own"_s);
    if (!_problems.empty()) {
        const auto [sev, texts] = _row_problems(rel);
        _mark_problem(top, sev, texts);
    }
    int mk = 0;
    for (Entry *e : rows)
        mk |= _mark(*e);
    if (mk) {
        const QBrush br(qc(mark_rgb(mk), 45));
        for (int c = 0; c < COLS.size(); ++c)
            top->setBackground(c, br);
    }
    return top;
}

QTreeWidgetItem *EventPanel::_cat_row(const QString &name, qsizetype n, qsizetype n_files, double tot, const QFont &bold)
{
    QTreeWidgetItem *top;
    QBrush bg;
    if (name == u"__none") {
        top = new QTreeWidgetItem(QStringList{u"Without a category"_s, QString::number(n), QString(), QString(), QString()});
        top->setIcon(0, cats_icon({}));
        bg = QBrush(QColor(120, 114, 102, 45));
    } else {
        const Rgb rgb = color(name);
        top = new QTreeWidgetItem(QStringList{
            name, QString::number(n),
            n_files ? u"%1 file%2"_s.arg(QString::number(n_files), n_files == 1 ? QString() : u"s"_s) : QString(),
            QString(), tot ? _fmt_times({tot, tot}) : QString()});
        top->setIcon(0, cats_icon({rgb}, 16));
        bg = QBrush(qc(rgb, 45));
    }
    set_row_data(top, u'c', name, nullptr, std::nullopt);
    top->setData(1, Qt::UserRole, bg);
    top->setFlags(top->flags() & ~Qt::ItemIsDragEnabled);
    for (int c = 0; c < COLS.size(); ++c) {
        top->setFont(c, bold);
        top->setBackground(c, bg);
    }
    for (int c : {1, 4})
        top->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    top->setToolTip(0, u"Drag rows here to %1"_s.arg(
                           name == u"__none" ? u"take them out of their category"_s
                                             : u"put them in “%1” (Ctrl: keep their other one too)"_s.arg(name)));
    return top;
}

void EventPanel::_fill_list()
{
    if (!db)
        return;
    _attribute();                            // (the entries may have been read again)
    if (_kind() == u"mods") {
        mod_view->refresh(doc, db.get());
        return;
    }
    if (_kind() == u"cbs") {
        cb_view->refresh(doc, db.get(), refs.get());
        return;
    }
    const int col = _sort_col;
    const bool desc = _sort_desc;
    QString q = py::strip(search->text());
    std::set<CatKey> keep;
    for (const CatKey &k : _selected_keys())
        keep.insert(k);
    QHash<QString, std::vector<Entry *>> by_file;
    for (const auto &e : db->entries)
        if (_wanted(*e))
            by_file[rel_of(*e)].push_back(e.get());
    const QString cat_want = cat_box->currentData().toString();
    const bool filtering = !q.isEmpty() || !(cat_want.isEmpty() || cat_want == u"__all") ||
                           show_box->currentData().toString() != u"all" ||
                           (nation && !nation->isEmpty() && only_nation->isChecked()) ||
                           (only_sym->isChecked() && sym);
    if (q.isEmpty() && only_sym->isChecked() && sym)
        q = u"x"_s;                          // (unfolds the files)
    std::vector<std::pair<SortKey, QString>> fk;
    for (const QString &r : db->files)
        if (_folder_ok(r) && (by_file.contains(r) || !filtering))
            fk.emplace_back(_file_value(r, by_file.value(r), col), r);
    std::stable_sort(fk.begin(), fk.end(), [desc](const auto &a, const auto &b) {
        return desc ? b.first < a.first : a.first < b.first;
    });
    QStringList files;
    for (const auto &[_k, r] : fk)
        files.append(r);
    lst->blockSignals(true);
    lst->setUpdatesEnabled(false);
    lst->forget();
    lst->clear();
    QFont italic(lst->font());
    italic.setItalic(true);
    QFont bold(lst->font());
    bold.setBold(true);
    const QBrush grey(QColor(120, 114, 102));
    long long n_ev = 0, n_all = 0;
    std::vector<Entry *> every_row;
    for (const QString &r : files)
        for (Entry *e : by_file.value(r)) {
            n_ev += e->is_event() ? 1 : 0;
            ++n_all;
            every_row.push_back(e);
        }
    const long long n_dec = n_all - n_ev;
    _fill_marks(every_row);
    auto times_hi = [this](const std::vector<Entry *> &rows) {
        py::Sum s;
        for (Entry *e : rows) {
            if (const auto t = times(*e))
                s.add(t->second);
            else
                s.add(0LL);
        }
        return s.value();
    };
    if (_group == u"file") {
        for (const QString &rel : files) {
            const std::vector<Entry *> rows = _sorted(by_file.value(rel));
            QTreeWidgetItem *top = _file_row(rel, rows, std::nullopt, bold);
            lst->addTopLevelItem(top);
            for (Entry *e : rows)
                top->addChild(_row(e, italic, grey));
            top->setExpanded(open_files.contains(rel) || !q.isEmpty());
        }
        count->setText(u"%1 files · %2 events · %3 decisions — click ▸ to open a file · click a column's name to "
                       u"sort by it"_s.arg(QString::number(files.size()), QString::number(n_ev), QString::number(n_dec)));
    } else {
        const bool all_cats = cat_want.isEmpty() || cat_want == u"__all";
        QStringList groups;
        for (const QString &n : cat_order())
            if (all_cats || cat_want == n)
                groups.append(n);
        if (all_cats || cat_want == u"__none")
            groups.append(u"__none"_s);
        for (const QString &g : groups) {
            std::vector<std::pair<QString, std::vector<Entry *>>> subs;
            std::vector<Entry *> direct;
            if (g == u"__none") {
                for (const QString &r : files) {
                    if (!file_cats(r).isEmpty())
                        continue;
                    std::vector<Entry *> rows;
                    for (Entry *e : by_file.value(r))
                        if (cats_of(*e).isEmpty())
                            rows.push_back(e);
                    if (!rows.empty())
                        subs.emplace_back(r, rows);
                }
            } else {
                for (const QString &r : files)
                    if (file_cats(r).contains(g))
                        subs.emplace_back(r, by_file.value(r));
                for (const QString &r : files)
                    if (!file_cats(r).contains(g))
                        for (Entry *e : by_file.value(r))
                            if (cats_of(*e).contains(g))
                                direct.push_back(e);
            }
            std::vector<Entry *> every;
            for (const auto &[_r, rows] : subs)
                every.insert(every.end(), rows.begin(), rows.end());
            every.insert(every.end(), direct.begin(), direct.end());
            QTreeWidgetItem *top = _cat_row(g, qsizetype(every.size()), g != u"__none" ? qsizetype(subs.size()) : 0,
                                            times_hi(every), bold);
            lst->addTopLevelItem(top);
            for (const auto &[r, rows0] : subs) {
                const std::vector<Entry *> rows = _sorted(rows0);
                QTreeWidgetItem *fr = _file_row(r, rows, g, bold);
                top->addChild(fr);
                for (Entry *e : rows)
                    fr->addChild(_row(e, italic, grey, g));
                fr->setExpanded(open_cat_files.count({g, r}) || !q.isEmpty());
            }
            for (Entry *e : _sorted(direct))
                top->addChild(_row(e, italic, grey, g));
            top->setExpanded(open_cats.contains(g) || !q.isEmpty());
        }
        count->setText(u"%1 categories · %2 events · %3 decisions — click ▸ to open a category · drag rows onto a "
                       u"category to put them in it"_s.arg(QString::number(groups.size() - (groups.contains(u"__none"_s) ? 1 : 0)),
                                                           QString::number(n_ev), QString::number(n_dec)));
    }
    // the one shown, and the rows that were selected (still selected, their groups opened)
    QTreeWidgetItem *cur = nullptr;
    std::vector<QTreeWidgetItem *> picked;
    _items.clear();
    _painted.clear();
    for (QTreeWidgetItemIterator it(lst); *it; ++it) {
        QTreeWidgetItem *item = *it;
        const RowData d = row_data(item);
        if (d.kind == u'e' && d.e)
            _items[{d.key, uid_of(*d.e)}].push_back(item);
        else if (d.kind == u'f')
            _items[{d.key, QString()}].push_back(item);
        if (!cur && ((d.kind == u'e' && d.e == sel && sel) || (d.kind == u'f' && !sel && sel_file && d.key == *sel_file) ||
                     (d.kind == u'c' && !sel && !sel_file && sel_cat && d.key == *sel_cat)))
            cur = item;
        if ((d.kind == u'e' && d.e && keep.count(CatKey{d.key, d.e->uid})) ||
            (d.kind == u'f' && keep.count(CatKey{d.key, std::nullopt})))
            picked.push_back(item);
    }
    if (cur) {
        lst->setCurrentItem(cur, 0, QItemSelectionModel::NoUpdate);
        if (picked.empty())
            picked = {cur};
    }
    for (QTreeWidgetItem *item : picked) {
        item->setSelected(true);
        for (QTreeWidgetItem *par = item->parent(); par; par = par->parent())
            if (!par->isExpanded()) {
                par->setExpanded(true);
                _expanded(par, true);
            }
    }
    if (cur)
        lst->scrollToItem(cur);
    lst->setUpdatesEnabled(true);
    lst->blockSignals(false);
    _style_bar();
    _paint_sym_rows();
}

void EventPanel::_picked(QTreeWidgetItem *cur, QTreeWidgetItem *)
{
    if (!cur)
        return;
    const RowData d = row_data(cur);
    if (d.kind == u'c') {
        sel = nullptr;
        sel_file = std::nullopt;
        sel_cat = d.key;
    } else if (d.kind == u'f') {
        sel = nullptr;
        sel_file = d.key;
        sel_cat = std::nullopt;
        if (ws)
            ws->open(d.key);
    } else {
        sel = d.e;
        sel_file = d.key;
        sel_cat = std::nullopt;
        _open_entry(d.e);
    }
    _fill_detail();
    Q_EMIT changed();
}

void EventPanel::select(Entry *e)
{
    sel = e;
    sel_file = rel_of(*e);
    sel_cat = std::nullopt;
    open_files.insert(rel_of(*e));
    lst->clearSelection();
    _fill_list();
    _fill_detail();
    _open_entry(e);
    Q_EMIT changed();
}

bool EventPanel::_mod_exists(const QString &name)
{
    modifiers::ModifierEdits *m = mods_store(doc);
    return m && m->exists(name);
}

void EventPanel::_link(const QString &href)
{
    if (href.startsWith(u"prov:")) {
        Q_EMIT provincePicked(py::to_int(href.mid(5)));
        return;
    }
    if (href.startsWith(u"go:")) {
        const QString rest = href.mid(3);
        const qsizetype bar_at = rest.lastIndexOf(u'|');
        if (ws && bar_at >= 0) {
            const QString rel = rest.left(bar_at);
            const qsizetype pos = py::to_int(rest.mid(bar_at + 1));
            ws->open(rel, pos, false, true, line_span(store->text(rel), pos));
        }
        return;
    }
    if (href.startsWith(u"nat:")) {
        set_nation(href.mid(4));
        return;
    }
    if (href.startsWith(u"mod:")) {
        const QString name = href.mid(4);
        if (_mod_exists(name))
            _open_mod(name);
        return;
    }
    if (href.startsWith(u"cb:")) {
        show_cb(href.mid(3));
        return;
    }
    Entry *e = nullptr;
    if (href.startsWith(u"ev:")) {
        Entry *const *p = db->by_id.get(href.mid(3));
        e = p ? *p : nullptr;
    } else if (href.startsWith(u"dec:")) {
        for (const auto &x : db->entries)
            if (!x->is_event() && x->key == href.mid(4)) {
                e = x.get();
                break;
            }
    }
    if (e)
        _show_entry(e);
}

void EventPanel::_show_entry(Entry *e)
{
    const QString kind = _kind();
    if (kind == u"mods" || kind == u"cbs" || (kind == u"decision" && e->is_event()) || (kind == u"event" && !e->is_event()))
        (*kind_btn.get(u"both"_s))->setChecked(true);
    _set_kind_vis();
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    show_box->blockSignals(true);
    show_box->setCurrentIndex(0);
    show_box->blockSignals(false);
    cat_box->blockSignals(true);
    cat_box->setCurrentIndex(0);
    cat_box->blockSignals(false);
    select(e);
}

void EventPanel::go_back()
{
    if (ws)
        ws->back();
}

void EventPanel::go_forward()
{
    if (ws)
        ws->forward();
}

bool EventPanel::eventFilter(QObject *obj, QEvent *e)
{
    // (every event of the whole application comes through here: anything but a mouse press leaves at once)
    if (e->type() != QEvent::MouseButtonPress)
        return false;
    const auto *me = static_cast<QMouseEvent *>(e);
    if ((me->button() == Qt::BackButton || me->button() == Qt::ForwardButton) && isVisible() && obj->isWidgetType()) {
        auto *w = static_cast<QWidget *>(obj);
        if ((w == this || isAncestorOf(w)) && ws && !ws->isAncestorOf(w)) {
            if (me->button() == Qt::BackButton)
                go_back();
            else
                go_forward();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------- the selected one
static QString more_lines(QStringList rows, const QString &none)
{
    if (rows.size() > MAX_HTML_LINES) {
        const qsizetype n = rows.size() - MAX_HTML_LINES;
        rows = rows.mid(0, MAX_HTML_LINES);
        rows.append(u"<div style=\"color:#8a8272\">… %1 more lines — see the text</div>"_s.arg(n));
    }
    return !rows.isEmpty() ? rows.join(QString()) : none;
}

QString EventPanel::_conds_html(View nodes, const std::optional<Scope> &scope, std::optional<long long> year)
{
    QStringList rows;
    static const QRegularExpression MODRX(u"^(has (?:country|province) modifier) “(.+)”$"_s);
    for (const events::CondLine &l : events::cond_lines(*db, judge.get(), nodes, scope, year)) {
        const auto [mark, col] = state_mark(l.has_state, l.state);
        const QRegularExpressionMatch m = MODRX.match(l.text);
        QString t;
        if (m.hasMatch() && _mod_exists(m.captured(2))) {
            const QString loc = mod.loc.value(m.captured(2));
            t = u"%1 <a href=\"mod:%2\" style=\"color:%3\">%4</a>"_s.arg(esc(m.captured(1)), esc(m.captured(2)),
                                                                        theme::GOLD, esc(!loc.isEmpty() ? loc : m.captured(2)));
        } else {
            t = esc(l.text);
        }
        rows.append(u"<div style=\"margin-left:%1px\"><span style=\"color:%2\">%3</span> %4</div>"_s.arg(
            QString::number(14 * l.depth), col, mark, t));
    }
    return more_lines(rows, u"<div style=\"color:#8a8272\">none (always)</div>"_s);
}

QString EventPanel::_effects_html(View nodes)
{
    QStringList rows;
    for (const events::Line &l : events::effect_lines(*db, nodes)) {
        QString t = esc(l.text);
        if (l.link && l.link->startsWith(u"mod:")) {
            if (_mod_exists(l.link->mid(4)))
                t = u"<a href=\"%1\" style=\"color:%2\">%3</a>"_s.arg(esc(*l.link), theme::GOLD, t);
        } else if (l.link && l.link->startsWith(u"prov:")) {
            t = u"<a href=\"%1\" style=\"color:%2\">%3</a>"_s.arg(*l.link, theme::GOLD, t);
        } else if (l.link && !l.link->isEmpty() && db->by_id.contains(*l.link)) {
            t = u"<a href=\"ev:%1\" style=\"color:%2\">%3</a>"_s.arg(*l.link, theme::GOLD, t);
        }
        rows.append(u"<div style=\"margin-left:%1px\">%2</div>"_s.arg(QString::number(14 * l.depth), t));
    }
    return more_lines(rows, u"<div style=\"color:#8a8272\">nothing</div>"_s);
}

void EventPanel::_fill_file_detail(const QString &rel)
{
    std::vector<Entry *> rows;
    for (const auto &e : db->entries)
        if (e->rel == rel)
            rows.push_back(e.get());
    long long n_e = 0;
    for (Entry *e : rows)
        n_e += e->is_event() ? 1 : 0;
    const long long n = qsizetype(rows.size());
    title->setText(py::split(rel, u"/"_s, 1).value(1));
    meta->setText(u"%1 · %2 events · %3 decisions"_s.arg(rel, QString::number(n_e), QString::number(n - n_e)));
    QStringList parts;
    std::map<QString, long long> by_cat;
    for (Entry *e : rows)
        for (const QString &c : cats_of(*e))
            ++by_cat[c];
    if (!by_cat.empty()) {
        QStringList cs;
        for (const auto &[c, k] : by_cat)
            cs.append(u"<span style=\"color:%1\">■</span> %2 (%3)"_s.arg(rgb_css(color(c)), esc(c), QString::number(k)));
        parts.append(u"<p>Its entries' categories: %1</p>"_s.arg(cs.join(u", "_s)));
    }
    long long n_mt = 0;
    for (Entry *e : rows)
        if (e->is_event() && e->mtth && *e->mtth && !e->triggered_only)
            ++n_mt;
    parts.append(u"<p>%1 happen by themselves (MTTH), %2 only when something fires them%3.</p>"_s.arg(
        QString::number(n_mt), QString::number(n_e - n_mt), n > n_e ? u", %1 decisions"_s.arg(n - n_e) : QString()));
    py::Sum tot;
    for (Entry *e : rows) {
        if (const auto t = times(*e))
            tot.add(t->second);
        else
            tot.add(0LL);
    }
    const double tv = tot.value();
    if (tv)
        parts.append(u"<p>Within 10 years, all together about <b>%1</b> times%2.</p>"_s.arg(
            _fmt_times({tv, tv}), nation && !nation->isEmpty() ? u" for %1"_s.arg(*nation) : QString()));
    body->setText(parts.join(QString()));
}

void EventPanel::_fill_cat_detail(const QString &name)
{
    if (name == u"__none") {
        long long n = 0;
        for (const auto &e : db->entries)
            if (cats_of(*e).isEmpty() && file_cats(rel_of(*e)).isEmpty())
                ++n;
        title->setText(u"Without a category"_s);
        meta->setText(u"%1 events and decisions"_s.arg(n));
        body->setText(u"<p>Select rows under it and click a category above the list (or press its number) — or drag "
                      u"them onto a category.</p>"_s);
    } else {
        long long files = 0, own = 0;
        for (const QString &r : db->files)
            files += file_cats(r).contains(name) ? 1 : 0;
        for (const auto &e : db->entries)
            own += cats_of(*e).contains(name) ? 1 : 0;
        title->setText(name);
        meta->setText(u"Category · %1 files · %2 events / decisions of their own"_s.arg(files).arg(own));
        body->setText(u"<p>Right-click “%1” above the list to give it another colour, rename it, move it or delete "
                      u"it.</p><p>Written into the files on Apply as <tt>#category: %1</tt> above an event or decision, "
                      u"<tt>#file category: %1</tt> at the top of a file.</p>"_s.arg(esc(name)));
    }
}

static QString stem(const QString &f)        // os.path.splitext(f)[0]
{
    const qsizetype dot = f.lastIndexOf(u'.');
    qsizetype lead = 0;
    while (lead < f.size() && f[lead] == u'.')
        ++lead;
    return dot > lead - 1 && dot >= lead ? f.left(dot) : f;
}

QStringList EventPanel::_picture_names(bool is_event)
{
    // The pictures an event / decision can show: the mod's, the ones added from the library in this session and the
    // game's own (unless the mod replaces that folder).
    const QString sub = is_event ? u"events"_s : u"decisions"_s;
    QStringList roots{QDir(mod.root).filePath(u"gfx/pictures/"_s + sub)};
    if (mod.base && !mod.base->isEmpty() && !mod.replaced({u"gfx"_s, u"pictures"_s, sub}))
        roots.append(QDir(*mod.base).filePath(u"gfx/pictures/"_s + sub));
    QStringList pend;
    if (doc && doc->maps.contains(u"assets"_s))
        for (const auto &[r, _a] : doc->map<moreviews::AssetEdits>(u"assets"_s).fresh)
            if (r.startsWith(u"gfx/pictures/%1/"_s.arg(sub)))
                pend.append(r.split(u'/').last());
    std::vector<qint64> times_;
    for (const QString &d : roots) {
        const QFileInfo fi(d);
        times_.push_back(fi.isDir() ? fi.lastModified().toMSecsSinceEpoch() : 0);
    }
    const auto sig = std::tuple{sub, pend, times_};
    if (!_pic_names.first || *_pic_names.first != sig) {
        QStringList names;
        auto add = [&names](const QString &n) {
            if (!names.contains(n))
                names.append(n);
        };
        for (const QString &f : pend)
            add(stem(f));
        for (const QString &d : roots)
            if (QFileInfo(d).isDir())
                for (const QString &f : QDir(d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
                    const QString lf = py::lower(f);
                    if (lf.endsWith(u".tga") || lf.endsWith(u".dds"))
                        add(stem(f));
                }
        std::stable_sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
        _pic_names = {sig, names};
    }
    return _pic_names.second;
}

void EventPanel::_set_picture(const QString &name)
{
    Entry *e = sel;
    if (!e || !store || name.isEmpty() || !script_edit)
        return;
    const QString text = store->text(rel_of(*e));
    const auto [a, b] = e->span;
    const QString block = py::slice(text, a, b);
    static const QRegularExpression RX(u"(\\bpicture\\s*=\\s*)(\"[^\"\\n]*\"|[\\w.\\-]+)"_s,
                                       QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch m = RX.match(block);
    QString fresh;
    if (m.hasMatch()) {
        fresh = block.left(m.capturedStart(2)) + u"\"%1\""_s.arg(name) + block.mid(m.capturedEnd(2));
    } else {
        const qsizetype i = block.indexOf(u'{') + 1;
        if (i <= 0)
            return;
        fresh = block.left(i) + u"\n\tpicture = \"%1\""_s.arg(name) + block.mid(i);
    }
    script_edit(u"Picture of %1: %2"_s.arg(nm(*e), name), rel_of(*e), py::head(text, a) + fresh + py::tail(text, b));
}

void EventPanel::_fill_picture(Entry *e)
{
    // The event's / decision's picture (gfx/pictures/events or …/decisions), read by the editor itself.
    QString name = e ? py::strip(py::strip(e->picture), u"\""_s) : QString();
    if (e && name.isEmpty() && !e->is_event())
        name = e->key;                       // a decision without `picture` uses its own key
    pic_box->setVisible(e != nullptr);
    if (e) {
        const QStringList names = _picture_names(e->is_event());
        pic_box->blockSignals(true);
        if (pic_box->count() != names.size() || (!names.isEmpty() && pic_box->itemText(0) != names[0])) {
            pic_box->clear();
            pic_box->addItems(names);
        }
        pic_box->setCurrentIndex(!name.isEmpty() ? pic_box->findText(name) : -1);
        if (!name.isEmpty() && pic_box->currentIndex() < 0)
            pic_box->setEditText(name);
        pic_box->blockSignals(false);
    }
    if (name.isEmpty()) {
        pic->hide();
        pic_note->hide();
        return;
    }
    if (!_pic_cache.contains(name)) {
        std::optional<QPixmap> pm;
        const QStringList folders = e->is_event() ? QStringList{u"events"_s, u"decisions"_s}
                                                  : QStringList{u"decisions"_s, u"events"_s};
        for (const QString &folder : folders) {
            for (const QString &ext : {u".tga"_s, u".dds"_s}) {
                const QString p = mod.p({u"gfx"_s, u"pictures"_s, folder, name + ext});
                if (QFileInfo(p).isFile()) {
                    if (const auto img = imgio::read_image(p)) {
                        pm = QPixmap::fromImage(*img);
                        if (pm->width() > 360)
                            pm = pm->scaledToWidth(360, Qt::SmoothTransformation);
                    }
                    break;
                }
            }
            if (pm)
                break;
        }
        _pic_cache.insert(name, pm);
    }
    const std::optional<QPixmap> pm = _pic_cache.value(name);
    pic->setVisible(pm.has_value());
    if (pm)
        pic->setPixmap(*pm);
    const bool explicit_ = !py::strip(e->picture).isEmpty();
    pic_note->setText(pm ? u"Picture: %1"_s.arg(name)
                         : u"Picture “%1”: no such file in gfx/pictures/%2."_s.arg(
                               name, e->is_event() ? u"events"_s : u"decisions"_s));
    pic_note->setVisible(pm.has_value() || explicit_);
}

void EventPanel::_lit_toggled(bool on)
{
    if (sel)
        Q_EMIT litEdit(rel_of(*sel), uid_of(*sel), on);
}

void EventPanel::_fill_lit()
{
    Entry *e = sel;
    lit_box->setVisible(e && store);
    if (!e || !store)
        return;
    const QString text = store->text(rel_of(*e));
    bool on;
    if (text == e->text)                     // (the entry's place is known: no need to read the file again)
        on = events::lit_line_above(text, e->span.first).has_value();
    else
        on = events::lit_marked({rel_of(*e)}, [this](const QString &r) { return store->text(r); })
                 .contains({rel_of(*e), uid_of(*e)});
    lit_box->blockSignals(true);
    lit_box->setChecked(on);
    lit_box->blockSignals(false);
}

void EventPanel::_fill_detail()
{
    Entry *e = sel;
    _fill_lit();
    _fill_row_problems();
    _fill_picture(e);
    if (e)
        _head(detail_head, u"Current %1: %2%3"_s.arg(e->is_event() ? u"event"_s : u"decision"_s, nm(*e),
                                                      e->is_event() ? u" #%1"_s.arg(e->id.value_or(u"None"_s)) : QString()),
              Rgb{232, 196, 118});
    else if (sel_file)
        _head(detail_head, u"Current file: %1"_s.arg(*sel_file), Rgb{200, 190, 160});
    else if (sel_cat)
        _head(detail_head, u"Category: %1"_s.arg(*sel_cat == u"__none" ? u"without a category"_s : *sel_cat),
              *sel_cat != u"__none" ? color(*sel_cat) : Rgb{170, 165, 150});
    detail_head->setVisible(e || sel_file || sel_cat);
    cat_host->setVisible(_cur_key() && db);
    if (db && _cur_key())
        _fill_chips();
    if (!e && !sel_file && sel_cat && db) {
        _fill_cat_detail(*sel_cat);
        _legend();
        return;
    }
    if (!e && sel_file && db) {
        _fill_file_detail(*sel_file);
        _legend();
        return;
    }
    if (!e || !db || !judge) {
        title->setText(u"Pick a file, an event or a decision"_s);
        meta->setText(QString());
        body->setText(QString());
        _legend();
        return;
    }
    title->setText(nm(*e));
    const QString kind = e->kind == u"country_event"    ? u"Country event"_s
                         : e->kind == u"province_event" ? u"Province event"_s
                                                        : u"Decision"_s;
    QStringList flags;
    if (e->once)
        flags.append(u"fires only once"_s);
    if (e->triggered_only)
        flags.append(u"only when fired"_s);
    if (e->major)
        flags.append(u"major"_s);
    meta->setText(u"%1 %2 · %3%4"_s.arg(kind, e->is_event() ? u"#%1"_s.arg(e->id.value_or(u"None"_s)) : e->key,
                                        rel_of(*e), !flags.isEmpty() ? u" · "_s + flags.join(u", "_s) : QString()));
    std::optional<Scope> scope;
    if (nation && !nation->isEmpty()) {
        scope = Scope::c(*nation);
        if (e->province())
            scope.reset();                   // conditions are per province: shown without answers
    }
    const Agg *a = agg.contains(e) ? &agg[e] : nullptr;
    QStringList parts;
    const events::Class cat = a ? a->cat : events::classify(*e);
    if (cat.kind == u"tags") {
        QStringList tags(cat.tags.begin(), cat.tags.end());
        tags.sort();
        QStringList names;
        for (const QString &t : tags)
            names.append(u"%1 (%2)"_s.arg(_cname(t), t));
        parts.append(u"<p>The trigger names these nations: %1</p>"_s.arg(esc(names.join(u", "_s))));
    } else if (cat.kind == u"player") {
        parts.append(u"<p>Only the player's nation (ai = no) — counted once, not for every nation.</p>"_s);
    }
    const QString desc = db->description(*e);
    if (!desc.isEmpty())
        parts.append(u"<p style=\"color:#a89c84\"><i>%1</i></p>"_s.arg(esc(words(desc).replace(u"\\n"_s, u" "_s).left(600))));
    const bool mt = e->mtth && *e->mtth;
    const QString ncname = nation ? _cname(*nation) : QString();
    if (!a) {
        parts.append(u"<p>Working it out…</p>"_s);
    } else if (e->is_event()) {
        const QString who = u"%1 %2 for sure%3"_s.arg(QString::number(a->sure), e->province() ? u"provinces"_s : u"nations"_s,
                                                    a->maybe ? u", %1 more maybe"_s.arg(a->maybe) : QString());
        if (e->triggered_only || !mt)
            parts.append(u"<p><b>Happens only when something fires it</b> — the trigger holds for %1 at the start.</p>"_s.arg(who));
        else
            parts.append(u"<p><b>Happens by itself</b> — MTTH %1 (after that time half the %2 it can happen to have "
                         u"had it). It can happen to %3; within 10 years about <b>%4</b> times in all.</p>"_s.arg(
                             events::fmt_days(e->mtth), e->province() ? u"provinces"_s : u"nations"_s, who,
                             _fmt_times(a->times)));
        if (const auto nc = nation_chance(*e)) {
            if (nc->can == Tri(false)) {
                parts.append(u"<p style=\"color:#ff786e\"><b>%1</b>: can't happen (the trigger fails at the start)%2.</p>"_s.arg(
                    esc(ncname), !e->province() ? QString() : u" in any of its provinces"_s));
            } else if (e->triggered_only || !mt) {
                parts.append(u"<p><b>%1</b>: the trigger %2.</p>"_s.arg(
                    esc(ncname), truthy(nc->can) ? u"holds"_s : u"may hold (see the ? below)"_s));
            } else {
                QString mt_for;
                if (nc->mtth) {
                    QStringList ds;
                    for (double y : {nc->mtth->first, nc->mtth->second}) {
                        const QString d = events::fmt_days(y);
                        if (!ds.contains(d))
                            ds.append(d);
                    }
                    mt_for = u" · MTTH for it %1"_s.arg(ds.join(u"–"_s));
                }
                parts.append(u"<p><b>%1</b>: about <b>%2</b> times within 10 years%3%4.</p>"_s.arg(
                    esc(ncname), _fmt_times(nc->times), e->once ? u" (it fires only once)"_s : QString(), mt_for));
            }
        }
    } else {
        parts.append(u"<p>Shown to <b>%1</b> nations%2 at the start, allowed (can be taken) for <b>%3</b>.</p>"_s.arg(
            QString::number(a->sure), a->maybe ? u" (+%1 maybe)"_s.arg(a->maybe) : QString(), QString::number(a->allowed)));
        if (const auto nc = nation_chance(*e))
            parts.append(u"<p><b>%1</b>: %2.</p>"_s.arg(
                esc(ncname), truthy(nc->can) && truthy(nc->allow) ? u"can take it now"_s
                             : truthy(nc->can)                     ? u"sees it, not allowed yet"_s
                             : !nc->can                            ? u"may see it"_s
                                                                   : u"doesn't see it"_s));
    }
    parts.append(_who_html(*e, a));
    parts.append(_provinces_html(*e, a));
    parts.append(_chain_html(e));
    if (e->is_event()) {
        parts.append(u"<h4>Trigger</h4>"_s + _conds_html(View(e->trigger), scope, world->start_year));
        if (mt) {
            QStringList rows{u"<div>Base: %1</div>"_s.arg(events::fmt_days(e->mtth))};
            for (const auto &[f, cond] : e->mods) {
                Tri st;
                bool has = false;
                if (scope) {
                    st = judge->all(View(cond), *scope, *scope, world->start_year);
                    has = true;
                }
                const auto [mark, col] = state_mark(has, st);
                QStringList txt;
                const auto cl = events::cond_lines(*db, judge.get(), View(cond), std::nullopt, std::nullopt);
                for (size_t i = 0; i < std::min<size_t>(4, cl.size()); ++i)
                    txt.append(cl[i].text);
                rows.append(u"<div><span style=\"color:%1\">%2</span> ×%3 when %4</div>"_s.arg(
                    col, mark, py::fmt_g(f), esc(txt.join(u"; "_s)) + (cond.size() > 4 ? u" …"_s : QString())));
            }
            parts.append(u"<h4>Mean time to happen</h4>"_s + rows.join(QString()));
        }
        if (const auto *src = db->fired_by.get(e->id.value_or(events::NO_ID)); src && !src->empty()) {
            QStringList rows;
            for (size_t i = 0; i < std::min<size_t>(30, src->size()); ++i) {
                const events::Fired &f = (*src)[i];
                if (!f.src)
                    rows.append(u"<div>%1</div>"_s.arg(esc(f.how)));
                else if (f.src->is_event())
                    rows.append(u"<div><a href=\"ev:%1\" style=\"color:%2\">%3 (#%1)</a> — %4</div>"_s.arg(
                        f.src->id.value_or(u"None"_s), theme::GOLD, esc(nm(*f.src)), esc(f.how)));
                else
                    rows.append(u"<div><a href=\"dec:%1\" style=\"color:%2\">decision %3</a> — %4</div>"_s.arg(
                        f.src->key, theme::GOLD, esc(nm(*f.src)), esc(f.how)));
            }
            parts.append(u"<h4>Fired by</h4>"_s + rows.join(QString()) + (src->size() > 30 ? u"<div>…</div>"_s : QString()));
        }
        if (e->immediate && !e->immediate->empty())
            parts.append(u"<h4>At once (immediate)</h4>"_s + _effects_html(View(e->immediate)));
        for (size_t i = 0; i < e->options.size(); ++i) {
            const events::Option &o = e->options[i];
            const QString loc = mod.loc.value(o.name);
            const QString onm = !loc.isEmpty() ? loc : !o.name.isEmpty() ? o.name : u"Option %1"_s.arg(i + 1);
            parts.append(u"<h4>Option %1: %2</h4>"_s.arg(QChar(char16_t(65 + i)), esc(onm)) + _effects_html(View(o.effects)));
        }
    } else {
        parts.append(u"<h4>Shown when (potential)</h4>"_s + _conds_html(View(e->potential), scope, world->start_year));
        parts.append(u"<h4>Can be taken when (allow)</h4>"_s + _conds_html(View(e->allow), scope, world->start_year));
        parts.append(u"<h4>Effect</h4>"_s + _effects_html(View(e->effect)));
    }
    body->setText(parts.join(QString()));
    _legend();
}

QString EventPanel::_nlink(const QString &tag)
{
    return u"<a href=\"nat:%1\" style=\"color:%2\">%3</a>"_s.arg(tag, theme::GOLD, esc(_cname(tag)));
}

static void sort_by_name(QStringList &tags, const std::function<QString(const QString &)> &name)
{
    std::stable_sort(tags.begin(), tags.end(), [&name](const QString &a, const QString &b) { return name(a) < name(b); });
}

QString EventPanel::_who_html(const Entry &e, const Agg *a)
{
    // Who can get a nation's event / decision: at the start — and later, where who / where fits (the rest of the
    // conditions can come true in a game); everyone else: never.
    if (!a || e.province())
        return QString();
    QStringList now, later;
    for (const auto &[k, _c] : a->per)
        if (!now.contains(k.second))
            now.append(k.second);
    for (const auto &[k, _q] : a->pot)
        if (!now.contains(k.second) && !later.contains(k.second))
            later.append(k.second);
    auto name = [this](const QString &t) { return _cname(t); };
    sort_by_name(now, name);
    sort_by_name(later, name);
    const qsizetype never = world->tags.size() - now.size() - later.size();
    QStringList rows;
    const std::vector<std::tuple<QString, QStringList *, QString>> heads = {
        {u"At the start"_s, &now, theme::GOLD},
        {u"Later — who / where fits, the rest could come true"_s, &later, u"#b8ab90"_s}};
    for (const auto &[head, tags, _col] : heads) {
        if (tags->isEmpty())
            continue;
        QStringList links;
        for (qsizetype i = 0; i < std::min<qsizetype>(50, tags->size()); ++i)
            links.append(_nlink((*tags)[i]));
        rows.append(u"<div><b>%1 (%2)</b>: %3%4</div>"_s.arg(head, QString::number(tags->size()), links.join(u", "_s),
                                                             tags->size() > 50 ? u" … +%1"_s.arg(tags->size() - 50) : QString()));
    }
    rows.append(u"<div style=\"color:#8a8272\">Never (%1): its conditions name who / where, and they don't fit. "
                u"Click a nation for its own numbers.</div>"_s.arg(std::max<qsizetype>(0, never)));
    return u"<h4>Who can get it</h4>"_s + rows.join(QString());
}

const relview::FiresMap &EventPanel::_fires_map()
{
    if (!_fires) {
        relview::FiresMap f;
        for (const auto &[tgt, lst_] : db->fired_by)
            for (const events::Fired &x : lst_)
                if (x.src)
                    f[x.src].emplace_back(tgt, x.how);
        _fires = std::move(f);
    }
    return *_fires;
}

QString EventPanel::_elink(const Entry &x)
{
    if (x.is_event())
        return u"<a href=\"ev:%1\" style=\"color:%2\">%3</a> <span style=\"color:#8a8272\">#%1</span>"_s.arg(
            x.id.value_or(u"None"_s), theme::GOLD, esc(nm(x)));
    return u"<a href=\"dec:%1\" style=\"color:%2\">%3</a> <span style=\"color:#8a8272\">decision</span>"_s.arg(
        x.key, theme::GOLD, esc(nm(x)));
}

QString EventPanel::_chain_html(Entry *e)
{
    // The chain it is in: what fires it (and what fires that …) and what it fires (and so on), 3 steps.
    EventDB &d = *db;
    const relview::FiresMap &fires = _fires_map();
    QStringList rows;
    int budget = 30;                         // lines each way (the whole chain: 🌳 Relations)
    std::function<void(const Entry *, int, std::set<const Entry *>)> up = [&](const Entry *x, int depth,
                                                                             std::set<const Entry *> seen) {
        if (!x->is_event())
            return;
        const auto *src = d.fired_by.get(x->id.value_or(events::NO_ID));
        if (!src)
            return;
        for (size_t i = 0; i < std::min<size_t>(8, src->size()); ++i) {
            const events::Fired &f = (*src)[i];
            if (budget <= 0)
                return;
            --budget;
            const int pad = 16 * depth;
            if (!f.src) {
                rows.append(u"<div style=\"margin-left:%1px\">↑ %2</div>"_s.arg(QString::number(pad), esc(f.how)));
            } else if (seen.count(f.src)) {
                rows.append(u"<div style=\"margin-left:%1px\">↑ %2 (loop)</div>"_s.arg(QString::number(pad), _elink(*f.src)));
            } else {
                rows.append(u"<div style=\"margin-left:%1px\">↑ %2 <span style=\"color:#8a8272\">%3</span></div>"_s.arg(
                    QString::number(pad), _elink(*f.src), esc(f.how)));
                if (depth < 3) {
                    std::set<const Entry *> s2 = seen;
                    s2.insert(f.src);
                    up(f.src, depth + 1, s2);
                }
            }
        }
    };
    std::function<void(const Entry *, int, std::set<const Entry *>)> down = [&](const Entry *x, int depth,
                                                                               std::set<const Entry *> seen) {
        const auto fit = fires.constFind(x);
        if (fit == fires.cend())
            return;
        const auto &outs = fit.value();
        for (size_t i = 0; i < std::min<size_t>(8, outs.size()); ++i) {
            const auto &[tgt, how] = outs[i];
            if (budget <= 0)
                return;
            --budget;
            Entry *const *yp = d.by_id.get(tgt);
            const Entry *y = yp ? *yp : nullptr;
            const int pad = 16 * depth;
            if (!y) {
                rows.append(u"<div style=\"margin-left:%1px\">↓ <span style=\"color:#ff786e\">#%2 — no such event</span></div>"_s.arg(
                    QString::number(pad), esc(tgt == events::NO_ID ? u"None"_s : tgt)));
            } else if (seen.count(y)) {
                rows.append(u"<div style=\"margin-left:%1px\">↓ %2 (loop)</div>"_s.arg(QString::number(pad), _elink(*y)));
            } else {
                rows.append(u"<div style=\"margin-left:%1px\">↓ %2 <span style=\"color:#8a8272\">%3</span></div>"_s.arg(
                    QString::number(pad), _elink(*y), esc(how)));
                if (depth < 3) {
                    std::set<const Entry *> s2 = seen;
                    s2.insert(y);
                    down(y, depth + 1, s2);
                }
            }
        }
    };
    up(e, 0, {e});
    qsizetype n_up = rows.size();
    if (budget <= 0) {
        rows.append(u"<div style=\"color:#8a8272\">… more above — the whole chain: 🌳 Relations</div>"_s);
        ++n_up;
    }
    budget = 30;
    down(e, 0, {e});
    if (budget <= 0)
        rows.append(u"<div style=\"color:#8a8272\">… more below — the whole chain: 🌳 Relations</div>"_s);
    if (rows.isEmpty())
        return QString();
    const QString below = rows.mid(n_up).join(QString());
    return u"<h4>Chain</h4>%1%2%3"_s.arg(n_up ? rows.mid(0, n_up).join(QString())
                                              : u"<div style=\"color:#8a8272\">↑ nothing fires it</div>"_s,
                                         u"<div style=\"margin:3px 0\"><b>%1</b></div>"_s.arg(esc(nm(*e))),
                                         !below.isEmpty() ? below : u"<div style=\"color:#8a8272\">↓ it fires nothing</div>"_s);
}

QString EventPanel::_plink(long long pid, bool maybe)
{
    const auto *pr = mod.provinces.get(pid);
    return u"<a href=\"prov:%1\" style=\"color:%2\">%3</a>%4"_s.arg(
        QString::number(pid), theme::GOLD, esc(pr ? pr->name : QString::number(pid)),
        maybe ? u" <span style=\"color:#b8ab90\">?</span>"_s : QString());
}

QString EventPanel::_provinces_html(const Entry &e, const Agg *a)
{
    // The provinces the entry is about: where a province event can happen (by owner), and the provinces its
    // conditions / effects name.
    QStringList out;
    const auto &provs = mod.provinces;
    auto pname = [&provs](long long p) {
        const auto *x = provs.get(p);
        return x ? x->name : QString();
    };
    auto owner_of = [this](long long pid) -> OptStr {
        const QString *o = world->owner.get(pid);
        return o ? OptStr(*o) : std::nullopt;
    };
    const bool for_nation = nation && !nation->isEmpty();
    if (e.province() && a) {
        OrderedMap<OptStr, std::vector<std::pair<long long, Tri>>> by;
        for (const auto &[k, c] : a->per) {
            const long long pid = k.second.toLongLong();
            const OptStr tag = owner_of(pid);
            if (for_nation && tag != nation)
                continue;
            by[tag].emplace_back(pid, c.can);
        }
        OrderedMap<OptStr, std::vector<long long>> later;
        for (const auto &[k, _q] : a->pot) {
            const long long pid = k.second.toLongLong();
            const OptStr tag = owner_of(pid);
            if (for_nation && tag != nation)
                continue;
            later[tag].push_back(pid);
        }
        long long n = 0;
        for (const auto &[_t, v] : by)
            n += qsizetype(v.size());
        auto by_size = [](auto &items) {
            std::stable_sort(items.begin(), items.end(), [](const auto &x, const auto &y) {
                const auto kx = std::pair{-qsizetype(x.second.size()), x.first.value_or(u"None"_s)};
                const auto ky = std::pair{-qsizetype(y.second.size()), y.first.value_or(u"None"_s)};
                return kx < ky;
            });
        };
        std::vector<std::pair<OptStr, std::vector<std::pair<long long, Tri>>>> items(by.begin(), by.end());
        by_size(items);
        QStringList rows;
        for (size_t i = 0; i < std::min<size_t>(40, items.size()); ++i) {
            auto &[tag, ps] = items[i];
            std::stable_sort(ps.begin(), ps.end(), [&pname](const auto &x, const auto &y) { return pname(x.first) < pname(y.first); });
            const QString name = tag && !tag->isEmpty() ? _cname(*tag) : u"no owner"_s;
            QStringList links;
            for (size_t j = 0; j < std::min<size_t>(40, ps.size()); ++j)
                links.append(_plink(ps[j].first, ps[j].second != Tri(true)));
            rows.append(u"<div><b>%1%2</b> · %3: %4%5</div>"_s.arg(
                esc(name), tag && !tag->isEmpty() ? u" (%1)"_s.arg(*tag) : QString(), QString::number(ps.size()),
                links.join(u", "_s), ps.size() > 40 ? u" … +%1 more"_s.arg(ps.size() - 40) : QString()));
        }
        const QString who = for_nation ? u" — %1's"_s.arg(_cname(*nation)) : QString();
        out.append(u"<h4>Provinces it can happen in at the start (%1%2)</h4>%3%4<div style='color:#8a8272'>? = can't "
                   u"be told from the start · click a province to find it on the map</div>"_s.arg(
                       QString::number(n), who, !rows.isEmpty() ? rows.join(QString()) : u"<div>none</div>"_s,
                       by.size() > 40 ? u"<div>… and %1 more nations</div>"_s.arg(by.size() - 40) : QString()));
        if (later.size()) {
            std::vector<std::pair<OptStr, std::vector<long long>>> litems(later.begin(), later.end());
            by_size(litems);
            QStringList lrows;
            long long tot = 0;
            for (const auto &[_t, v] : later)
                tot += qsizetype(v.size());
            for (size_t i = 0; i < std::min<size_t>(30, litems.size()); ++i) {
                auto &[tag, ps] = litems[i];
                std::stable_sort(ps.begin(), ps.end(), [&pname](long long x, long long y) { return pname(x) < pname(y); });
                const QString name = tag && !tag->isEmpty() ? _cname(*tag) : u"no owner"_s;
                QStringList links;
                for (size_t j = 0; j < std::min<size_t>(30, ps.size()); ++j)
                    links.append(_plink(ps[j]));
                lrows.append(u"<div><b>%1</b> · %2: %3%4</div>"_s.arg(esc(name), QString::number(ps.size()),
                                                                      links.join(u", "_s),
                                                                      ps.size() > 30 ? u" … +%1"_s.arg(ps.size() - 30) : QString()));
            }
            out.append(u"<h4>Could happen there later (%1) — where it fits, the rest of its conditions not yet</h4>%2"_s.arg(
                QString::number(tot), lrows.join(QString())));
        }
    }
    std::set<long long> conds, effs;
    if (e.is_event()) {
        conds = prov_ids(View(e.trigger));
        effs = prov_ids(View(e.immediate));
        for (const events::Option &o : e.options)
            prov_ids(View(o.effects), effs);
    } else {
        events::NodeList both;
        if (e.potential)
            both.insert(both.end(), e.potential->begin(), e.potential->end());
        if (e.allow)
            both.insert(both.end(), e.allow->begin(), e.allow->end());
        conds = prov_ids(View(both));
        effs = prov_ids(View(e.effect));
    }
    for (const auto &[head, ids0] : std::vector<std::pair<QString, const std::set<long long> *>>{
             {u"Provinces its conditions name"_s, &conds}, {u"Provinces its effects name"_s, &effs}}) {
        std::vector<long long> ids;
        for (long long p : *ids0)
            if (provs.contains(p))
                ids.push_back(p);
        if (ids.empty())
            continue;
        QStringList links;
        for (size_t i = 0; i < std::min<size_t>(150, ids.size()); ++i)
            links.append(u"%1 <span style='color:#8a8272'>#%2</span>"_s.arg(_plink(ids[i]), QString::number(ids[i])));
        out.append(u"<h4>%1 (%2) — all hatched cyan on the map</h4><div>%3%4</div>"_s.arg(
            head, QString::number(ids.size()), links.join(u", "_s),
            ids.size() > 150 ? u" … +%1 (all on the map)"_s.arg(ids.size() - 150) : QString()));
    }
    return out.join(QString());
}

Rgb EventPanel::_map_rgb(const Entry &e)
{
    QStringList cats = cats_of(e);
    if (cats.isEmpty())
        cats = file_cats(rel_of(e));
    return !cats.isEmpty() ? color(cats[0]) : NONE_RGB;
}

void EventPanel::_legend()
{
    Entry *e = sel;
    if (!e) {
        legend->setText(u"Map: pick an event or a decision in a file."_s);
        return;
    }
    QStringList cats = cats_of(*e);
    if (cats.isEmpty())
        cats = file_cats(rel_of(*e));
    const QString what = !cats.isEmpty() ? u"in the colour of its category “%1”"_s.arg(cats[0]) : u"in gold (no category yet)"_s;
    const bool named = agg.contains(e) && agg[e].cat.kind == u"tags";
    const QString tags = named ? u" The nations its trigger names are lit even where it fails at the start."_s : QString();
    const QString later = u" · dim = not at the start, but it could later (who / where fits) · dark = never (wrong "
                          u"nation / place)"_s;
    if (!e->is_event())
        legend->setText(u"Map %1: bright = can take it at the start · half bright = sees it, not allowed yet · striped "
                        u"= can't be told from the start (a flag set by events, the AI …)%2.%3"_s.arg(what, later, tags));
    else if (e->triggered_only || !(e->mtth && *e->mtth))
        legend->setText(u"Map %1: lit = the trigger holds — it happens when something fires it · striped = can't be "
                        u"told from the start%2.%3"_s.arg(what, later, tags));
    else
        legend->setText(u"Map %1: the brighter, the more often it happens within 10 years (bright = 3 times or more) · "
                        u"striped = can't be told from the start (a flag set by events, the AI, militancy …)%2. Click a "
                        u"nation for its own numbers.%3"_s.arg(what, later, tags));
}

// ---------------------------------------------------------------------------------------------- for the map
std::optional<MapValues> EventPanel::map_values()
{
    // None, or ("c" / "p", {tag or pid: (colour, certain)}) for the selected entry: its category's colour (gold
    // without one), the brighter the more often (a decision: can take it / sees it); the nations its trigger names
    // are lit.
    Entry *e = sel;
    if (!e || _kind() == u"mods" || _kind() == u"cbs" || !agg.contains(e))
        return std::nullopt;
    const Agg &a = agg[e];
    const Rgb rgb = _map_rgb(*e);
    const Rgb dark{44, 46, 52};
    auto shade = [&](double f) {
        auto ch = [f](int c, int d) { return int(double(d) + double(c - d) * f); };
        return Rgb{ch(std::get<0>(rgb), std::get<0>(dark)), ch(std::get<1>(rgb), std::get<1>(dark)),
                   ch(std::get<2>(rgb), std::get<2>(dark))};
    };
    MapValues mv;
    mv.kind = e->province() ? u"p"_s : u"c"_s;
    const bool fired = e->is_event() && (e->triggered_only || !(e->mtth && *e->mtth));
    for (const auto &[k, c] : a.per) {
        double f;
        bool certain;
        if (!e->is_event()) {
            f = truthy(c.can) && truthy(c.allow) ? 1.0 : 0.7;
            certain = c.can == Tri(true) && c.allow.has_value();
        } else if (fired) {
            f = 0.85;
            certain = c.can == Tri(true);
        } else {
            const double t = c.times.second;
            f = 0.55 + 0.45 * std::min(1.0, std::log1p(t) / std::log1p(3.0));
            certain = c.can == Tri(true) && c.times.second - c.times.first < 0.05 * std::max(1.0, c.times.second);
        }
        mv.out.insert(k.second, {shade(f), certain});
    }
    for (const auto &[k, _q] : a.pot)        // not at the start, but who / where fits: dim
        if (!mv.out.contains(k.second))
            mv.out.insert(k.second, {shade(0.24), true});
    if (a.cat.kind == u"tags" && !e->province()) {   // the nations it names lit, even where it fails now
        QStringList tags(a.cat.tags.begin(), a.cat.tags.end());
        tags.sort();
        for (const QString &t : tags)
            if (!mv.out.contains(t) && world->owned.contains(t))
                mv.out.insert(t, {shade(0.24), true});
    }
    return mv;
}

}  // namespace eventpanel
