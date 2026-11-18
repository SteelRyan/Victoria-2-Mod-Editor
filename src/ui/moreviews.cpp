// moreviews.py, its pages (see moreviews.h).
#include "ui/moreviews.h"
#include "core/document.h"
#include "core/blocks.h"
#include "core/entryview_data.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/submods.h"
#include "map/imgio.h"
#include "ui/colsizes.h"
#include "ui/defpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPixmap>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QShortcut>
#include <QTableWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace moreviews {

const QStringList PIC_EXT = {u".png"_s, u".jpg"_s, u".jpeg"_s, u".bmp"_s, u".tga"_s, u".dds"_s, u".webp"_s, u".gif"_s};
const QStringList MUSIC_EXT = {u".mp3"_s};
QString app_dir_override;

QString app_dir() { return !app_dir_override.isEmpty() ? app_dir_override : QCoreApplication::applicationDirPath(); }

static const OrderedMap<QString, QString> README = {
    {u"pictures"_s,
     u"ModEditor — your picture library\r\n\r\n"
     u"The editor ships no pictures of its own. Whatever is in this folder was put here by you, so you know where each "
     u"picture comes from and that you are allowed to use it.\r\n\r\n"
     u"How it works:\r\n"
     u"1. Copy pictures into this folder (png, jpg, bmp, tga or dds).\r\n"
     u"2. In the editor open Library > Pictures and press \"Look again\".\r\n"
     u"3. Select the ones you want and press \"Add the selected to the mod\".\r\n"
     u"4. Apply writes them into the mod (gfx/pictures/events or gfx/pictures/decisions) in the game's format. After "
     u"that every event and decision can use them: Scripts, the \"Picture\" box of the event.\r\n\r\n"
     u"Nothing in this folder is changed or deleted by the editor.\r\n"_s},
    {u"music"_s,
     u"ModEditor — your music library\r\n\r\n"
     u"The editor ships no music of its own. Whatever is in this folder was put here by you, so you know where each "
     u"piece comes from and that you are allowed to use it.\r\n\r\n"
     u"How it works:\r\n"
     u"1. Copy mp3 files into this folder.\r\n"
     u"2. In the editor open Library > Music and press \"Look again\".\r\n"
     u"3. Select the ones you want and press \"Add the selected to the mod\".\r\n"
     u"4. Apply copies them into the mod's music folder. The game plays every mp3 of that folder in turn, together "
     u"with its own.\r\n\r\nNothing in this folder is changed or deleted by the editor.\r\n"_s}};

QString library_dir(const QString &kind)
{
    const QString d = pypath::join(app_dir(), {u"library"_s, kind});
    QDir().mkpath(d);                                  // (errors ignored, as Python's try / except OSError)
    const QString note = pypath::join(d, u"How this folder works.txt"_s);
    if (const QString *text = README.get(kind); text && !QFileInfo::exists(note)) {
        QFile f(note);
        if (f.open(QIODevice::WriteOnly))
            f.write(text->toUtf8());
    }
    return d;
}

QLabel *bold(const QString &text, QWidget *)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

static QString tail(const QString &rel) { return rel.mid(rel.lastIndexOf(u'/') + 1); }   // rel.rsplit("/", 1)[-1]

// ---------------------------------------------------------------------------------------------- _Base
Base::Base(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_) {}

OptStr Base::loc(const QString &key) const
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

void Base::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (store)
        fill();
}

void Base::emit_text(const QString &what, const QString &rel, const QString &nw)
{
    if (nw != store->text(rel))
        Q_EMIT scriptEdit(what, rel, nw);
}

QLabel *Base::warn_label()
{
    QLabel *w = label(QString(), u"sub"_s);
    w->setStyleSheet(u"color: #ff786e;"_s);
    return w;
}

QVBoxLayout *Base::two_panes(const QString &title, const QString &intro, const QStringList &cols, const QString &key)
{
    // A list left (in a section with the title and the intro), a scrolling right side: returns its layout.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *s = new Section();
    s->lay->addWidget(label(title, u"title"_s));
    s->lay->addWidget(label(intro, u"faint"_s));
    lst = new QTreeWidget();
    lst->setColumnCount(static_cast<int>(cols.size()));
    lst->setHeaderLabels(cols);
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    std::vector<std::optional<int>> widths{std::nullopt};
    for (qsizetype i = 1; i < cols.size(); ++i)
        widths.push_back(130);
    colsizes::make_resizable(lst, key, widths, true);
    s->lay->addWidget(lst, 1);
    left_sec = s;
    ll->addWidget(s, 1);
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
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 6);
    split->setSizes({680, 940});
    return rl;
}

// ---------------------------------------------------------------------------------------------- start dates
static const QRegularExpression NUM_RE(uR"(^-?\d+(\.\d+)?$)"_s, QRegularExpression::UseUnicodePropertiesOption);

QString set_in(const QString &text, const script::Node &node, const QString &key, const QString &value)
{
    for (const script::Node &c : node.kids)
        if (c.has_key && c.key == key && !c.is_block)
            return text.left(c.vstart) + value + text.mid(c.vend);
    return blocks::_insert_before_close(text, node, u"%1 = %2"_s.arg(key, value));
}

QString cut_node(const QString &text, const script::Node &n)
{
    const qsizetype s = (n.kstart > 0 ? text.lastIndexOf(u'\n', n.kstart - 1) : -1) + 1;   // text.rfind("\n", 0, kstart)
    qsizetype e = text.indexOf(u'\n', n.vend);
    e = e < 0 ? text.size() : e + 1;
    return text.left(s) + text.mid(e);
}

static OrderedMap<QString, QString> plain_fields(const script::Node &n)   // {c.key: unq(c.value)} of its plain lines
{
    OrderedMap<QString, QString> f;
    for (const script::Node &c : n.kids)
        if (c.has_key && !c.key.isEmpty() && !c.is_block)
            f.insert(c.key, unq(c.value));
    return f;
}

static QString fget(const OrderedMap<QString, QString> &f, const QString &k, const QString &dflt = QString())
{
    const QString *x = f.get(k);
    return x ? *x : dflt;
}

static std::optional<std::vector<long long>> date_tuple(const QString &date)   // tuple(int(x) for x in date.split("."))
{
    std::vector<long long> out;
    for (const QString &x : date.split(u'.')) {
        bool ok = false;
        const long long v = py::strip(x).toLongLong(&ok);
        if (!ok)
            return std::nullopt;
        out.push_back(v);
    }
    return out;
}

const QString StartDatesView::REL = u"common/bookmarks.txt"_s;

StartDatesView::StartDatesView(moddata::Mod &mod_, QWidget *parent) : Base(mod_, parent)
{
    QVBoxLayout *rl = two_panes(
        u"Start dates"_s,
        u"The dates offered on the game's first screen (common/bookmarks.txt). Each needs its own history: the game "
        u"reads every history file up to that date (the dated blocks inside them) and the pops of the newest "
        u"history/pops folder at or before it. The scenario box at the top of the editor picks which date's pops and "
        u"history you are editing."_s,
        {u"Start date"_s, u"Date"_s, u"Its pops"_s}, u"startDates"_s);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *p) { _picked(it, p); });
    auto *s = new Section();
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    const std::tuple<QString, QString, QString> fields[5] = {
        {u"name"_s, u"Name in the game"_s, QString()}, {u"desc"_s, u"Description"_s, QString()},
        {u"date"_s, u"Date"_s, u"year.month.day, like 1861.7.1"_s},
        {u"cameraX"_s, u"Camera: across"_s, u"where the map looks at first"_s}, {u"cameraY"_s, u"Camera: down"_s, QString()}};
    for (int r = 0; r < 5; ++r) {
        const auto &[k, text, ph] = fields[r];
        g->addWidget(label(text, u"sub"_s, false), r, 0);
        auto *e = new QLineEdit();
        e->setPlaceholderText(ph);
        connect(e, &QLineEdit::editingFinished, this, [this, kk = k, e] { _set(kk, e); });
        g->addWidget(e, r, 1);
        ed.emplace_back(k, e);
    }
    s->lay->addLayout(g);
    pops_note = label(QString(), u"sub"_s);
    s->lay->addWidget(pops_note);
    warn = warn_label();
    s->lay->addWidget(warn);
    auto *row = new QHBoxLayout();
    new_date = new QLineEdit();
    new_date->setPlaceholderText(u"date of a new start date, like 1861.7.1"_s);
    row->addWidget(new_date, 1);
    auto *b = new QPushButton(u"＋ New start date"_s);
    connect(b, &QPushButton::clicked, this, [this] { _new(); });
    row->addWidget(b);
    rm = new QPushButton(u"✕ Remove this one"_s);
    connect(rm, &QPushButton::clicked, this, [this] { _remove(); });
    row->addWidget(rm);
    s->lay->addLayout(row);
    rl->addWidget(s);
    rl->addStretch(1);
}

std::vector<script::Node> StartDatesView::nodes()
{
    std::vector<script::Node> out;
    for (script::Node &n : script::parse(store->text(REL)))
        if (n.has_key && n.key == u"bookmark" && n.is_block)
            out.push_back(std::move(n));
    return out;
}

std::vector<std::vector<long long>> StartDatesView::pop_dates() const
{
    std::vector<std::vector<long long>> out;
    for (const QString &f : mod.pop_scenarios())   // (the mod's start dates and the base game's)
        if (DATE_RE.match(f).hasMatch())
            out.push_back(*date_tuple(f));
    std::sort(out.begin(), out.end());
    return out;
}

OptStr StartDatesView::pops_for(const QString &date) const
{
    const auto want = date_tuple(date);
    if (!want)
        return std::nullopt;
    std::optional<std::vector<long long>> best;
    for (const auto &d : pop_dates())
        if (d <= *want)
            best = d;
    if (!best)
        return std::nullopt;
    return u"%1.%2.%3"_s.arg((*best)[0]).arg((*best)[1]).arg((*best)[2]);
}

void StartDatesView::fill()
{
    const auto ns = nodes();
    lst->blockSignals(true);
    lst->clear();
    for (size_t i = 0; i < ns.size(); ++i) {
        const auto f = plain_fields(ns[i]);
        auto *it = new QTreeWidgetItem(QStringList{loc(fget(f, u"name"_s)).value_or(fget(f, u"name"_s, u"?"_s)),
                                                   fget(f, u"date"_s, u"?"_s),
                                                   pops_for(fget(f, u"date"_s)).value_or(u"none!"_s)});
        it->setData(0, Qt::UserRole, static_cast<int>(i));
        lst->addTopLevelItem(it);
    }
    cur = std::max<long long>(0, std::min<long long>(cur, static_cast<long long>(ns.size()) - 1));
    if (!ns.empty())
        lst->setCurrentItem(lst->topLevelItem(static_cast<int>(cur)));
    lst->blockSignals(false);
    _show(ns);
}

void StartDatesView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    if (it) {
        cur = it->data(0, Qt::UserRole).toLongLong();
        _show(nodes());
    }
}

void StartDatesView::_show(const std::vector<script::Node> &ns)
{
    const script::Node *n = !ns.empty() && cur < static_cast<long long>(ns.size()) ? &ns[static_cast<size_t>(cur)] : nullptr;
    const auto f = n ? plain_fields(*n) : OrderedMap<QString, QString>();
    for (const auto &[k, e] : ed) {
        e->setEnabled(n != nullptr);
        if (!e->hasFocus()) {
            e->setText(k == u"name" || k == u"desc" ? loc(fget(f, k)).value_or(QString()) : fget(f, k));
            e->setStyleSheet(QString());
        }
    }
    rm->setEnabled(n && ns.size() > 1);
    const OptStr p = n ? pops_for(fget(f, u"date"_s)) : std::nullopt;
    if (!n) {
        pops_note->setText(QString());
    } else if (p) {
        QStringList ds;
        for (const auto &d : pop_dates())
            ds << u"%1.%2.%3"_s.arg(d[0]).arg(d[1]).arg(d[2]);
        pops_note->setText(u"The game starts this date with the pops of history/pops/%1. The pops folders there are: %2."_s
                               .arg(*p, ds.join(u", "_s)));
    } else {
        pops_note->setText(u"⚠ There is no history/pops folder at or before this date: the game would start without "
                           u"pops. Use “New scenario” at the top of the editor to make one."_s);
    }
}

void StartDatesView::_set(const QString &k, QLineEdit *w)
{
    const auto ns = nodes();
    if (cur >= static_cast<long long>(ns.size()))
        return;
    const script::Node &n = ns[static_cast<size_t>(cur)];
    const auto f = plain_fields(n);
    const QString v = py::split(w->text()).join(u' ');
    if (k == u"name" || k == u"desc") {
        const QString *key = f.get(k);
        if (key && !key->isEmpty() && !v.isEmpty() && v != loc(*key).value_or(QString()))
            Q_EMIT nameEdit(*key, v);
        return;
    }
    if (v == fget(f, k))
        return;
    if ((k == u"date" && !DATE_RE.match(v).hasMatch()) || (k != u"date" && !NUM_RE.match(v).hasMatch())) {
        w->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        warn->setText(u"A date is year.month.day (1861.7.1); the camera needs numbers."_s);
        return;
    }
    warn->setText(QString());
    emit_text(u"Start date %1: %2 %3"_s.arg(loc(fget(f, u"name"_s)).value_or(fget(f, u"name"_s)), k, v), REL,
              set_in(store->text(REL), n, k, v));
}

void StartDatesView::_new()
{
    const QString d = py::strip(new_date->text());
    if (!DATE_RE.match(d).hasMatch()) {
        warn->setText(u"Give the new start date its date: year.month.day, like 1861.7.1."_s);
        return;
    }
    const auto ns = nodes();
    for (const script::Node &n : ns)
        for (const script::Node &c : n.kids)
            if (c.has_key && c.key == u"date" && (c.is_block ? QString() : unq(c.value)) == d) {
                warn->setText(u"There is a start date on %1 already."_s.arg(d));
                return;
            }
    warn->setText(QString());
    const QString tag = u"BM_"_s + QString(d).replace(u'.', u'_');
    OrderedMap<QString, QString> src;
    if (!ns.empty())
        for (const script::Node &c : ns[static_cast<size_t>(std::min<long long>(cur, static_cast<long long>(ns.size()) - 1))].kids)
            if (c.has_key && !c.key.isEmpty())
                src.insert(c.key, c.is_block ? QString() : unq(c.value));
    const QString block = u"bookmark =\n{\n\tname = \"%1_NAME\"\n\tdesc = \"%1_DESC\"\n\tdate = %2\n\tcameraX = %3\n\tcameraY = %4\n}"_s
                              .arg(tag, d, fget(src, u"cameraX"_s, u"2000"_s), fget(src, u"cameraY"_s, u"1500"_s));
    const QString text = store->text(REL);
    cur = static_cast<long long>(ns.size());
    new_date->clear();
    QString head = text;
    while (head.endsWith(u'\n'))
        head.chop(1);
    emit_text(u"New start date %1"_s.arg(d), REL, head + u"\n\n"_s + block + u"\n"_s);
    Q_EMIT nameEdit(tag + u"_NAME"_s, u"Start in %1"_s.arg(d.split(u'.')[0]));
    Q_EMIT nameEdit(tag + u"_DESC"_s, u"The world on %1."_s.arg(d));
}

void StartDatesView::_remove()
{
    const auto ns = nodes();
    if (cur < static_cast<long long>(ns.size()) && ns.size() > 1) {
        const auto f = plain_fields(ns[static_cast<size_t>(cur)]);
        store->drop_names = {fget(f, u"name"_s), fget(f, u"desc"_s)};
        emit_text(u"Start date %1 removed"_s.arg(fget(f, u"date"_s)), REL, cut_node(store->text(REL), ns[static_cast<size_t>(cur)]));
    }
}

// ---------------------------------------------------------------------------------------------- wars at the start
WarsView::WarsView(moddata::Mod &mod_, QWidget *parent) : Base(mod_, parent)
{
    QVBoxLayout *rl = two_panes(
        u"Wars at the start"_s,
        u"The wars already running when the game starts (history/wars — one file each). Pick who attacks, who defends "
        u"and what each side wants; “Save the war” writes the file. A file that is laid out differently (nations "
        u"joining on several dates …) is edited as text below. An empty war file crashes the game: a war is removed by "
        u"deleting its file."_s,
        {u"War"_s, u"Sides"_s, u"From"_s}, u"warsList"_s);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *p) { _picked(it, p); });
    auto *row = new QHBoxLayout();
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"name of a new war, like Border War"_s);
    row->addWidget(new_name, 1);
    auto *b = new QPushButton(u"＋ New war"_s);
    connect(b, &QPushButton::clicked, this, [this] { _new(); });
    row->addWidget(b);
    left_sec->lay->addLayout(row);
    auto *s = new Section();
    head = label(u"Pick a war"_s, u"title"_s);
    s->lay->addWidget(head);
    file_note = label(QString(), u"faint"_s);
    s->lay->addWidget(file_note);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    name = new QLineEdit();
    start = new QLineEdit();
    start->setPlaceholderText(u"year.month.day — on or before the start date, like 1836.1.1"_s);
    end = new QLineEdit();
    end->setPlaceholderText(u"empty: the war goes on — or the day it ends"_s);
    const std::pair<QString, QLineEdit *> fields[3] = {{u"Name"_s, name}, {u"Starts"_s, start}, {u"Ends"_s, end}};
    for (int r = 0; r < 3; ++r) {
        g->addWidget(label(fields[r].first, u"sub"_s, false), r, 0);
        g->addWidget(fields[r].second, r, 1);
    }
    s->lay->addLayout(g);
    for (const auto &[side, text] : {std::pair{u"att"_s, u"Attackers"_s}, std::pair{u"def"_s, u"Defenders"_s}}) {
        s->lay->addWidget(bold(text));
        auto *lw = new QListWidget();
        lw->setFixedHeight(96);
        lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        s->lay->addWidget(lw);
        row = new QHBoxLayout();
        QComboBox *cb = _nation_box();
        row->addWidget(cb, 1);
        auto *add = new QPushButton(u"＋ Add"_s);
        const QString sd = side;
        connect(add, &QPushButton::clicked, this, [this, sd] { _side_add(sd); });
        row->addWidget(add);
        auto *rm = new QPushButton(u"✕ Take off"_s);
        connect(rm, &QPushButton::clicked, this, [this, sd] { _side_rm(sd); });
        row->addWidget(rm);
        s->lay->addLayout(row);
        side_box.emplace_back(side, std::pair{lw, cb});
    }
    s->lay->addWidget(bold(u"War goals"_s));
    goals = new QListWidget();
    goals->setFixedHeight(96);
    goals->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->lay->addWidget(goals);
    row = new QHBoxLayout();
    cb_box = new QComboBox();
    cb_box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(cb_box, 2);
    actor = _nation_box();
    row->addWidget(actor, 2);
    row->addWidget(label(u"against"_s, u"faint"_s, false));
    receiver = _nation_box();
    row->addWidget(receiver, 2);
    auto *add = new QPushButton(u"＋"_s);
    add->setFixedWidth(34);
    connect(add, &QPushButton::clicked, this, [this] { _goal_add(); });
    row->addWidget(add);
    auto *rm = new QPushButton(u"✕"_s);
    rm->setFixedWidth(34);
    connect(rm, &QPushButton::clicked, this, [this] { _goal_rm(); });
    row->addWidget(rm);
    s->lay->addLayout(row);
    warn = warn_label();
    s->lay->addWidget(warn);
    row = new QHBoxLayout();
    save_btn = new QPushButton(u"Save the war"_s);
    save_btn->setObjectName(u"apply"_s);
    connect(save_btn, &QPushButton::clicked, this, [this] { _save(); });
    row->addWidget(save_btn, 1);
    rm_btn = new QPushButton(u"✕ Remove this war"_s);
    connect(rm_btn, &QPushButton::clicked, this, [this] { _remove(); });
    row->addWidget(rm_btn);
    s->lay->addLayout(row);
    form = s;
    rl->addWidget(s);
    s = new Section();
    s->lay->addWidget(bold(u"The file as text"_s));
    raw = new QPlainTextEdit();
    raw->setFont(QFont(theme::MONO, 10));
    raw->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    raw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    raw->setFixedHeight(260);
    s->lay->addWidget(raw);
    b = new QPushButton(u"Save the text"_s);
    connect(b, &QPushButton::clicked, this, [this] { _save_raw(); });
    s->lay->addWidget(b);
    rl->addWidget(s);
    rl->addStretch(1);
}

QComboBox *WarsView::_nation_box()
{
    auto *cb = new QComboBox();
    cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    std::vector<std::pair<QString, QString>> cs;  // (lower name, tag)
    for (const auto &kv : mod.countries)
        cs.emplace_back(py::lower(kv.second.name), kv.first);
    std::stable_sort(cs.begin(), cs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[_l, t] : cs)
        cb->addItem(u"%1  (%2)"_s.arg(mod.countries.get(t)->name, t), t);
    return cb;
}

QString WarsView::nm(const QString &tag) const
{
    const moddata::Country *c = mod.countries.get(tag);
    return c ? u"%1 (%2)"_s.arg(c->name, tag) : tag + u" — not in the mod"_s;
}

QStringList WarsView::rels() const { return entryview::_files(mod, store, u"history/wars"_s, true); }

std::pair<QListWidget *, QComboBox *> WarsView::box_of(const QString &side) const
{
    for (const auto &[k, v] : side_box)
        if (k == side)
            return v;
    return {nullptr, nullptr};
}

void WarsView::fill()
{
    lst->blockSignals(true);
    lst->clear();
    QTreeWidgetItem *pick = nullptr;
    for (const QString &rel : rels()) {
        const War wr = read_war(store->text(rel));
        auto *it = new QTreeWidgetItem(QStringList{
            !wr.name.isEmpty() ? wr.name : tail(rel),
            u"%1 vs %2"_s.arg(!wr.att.isEmpty() ? wr.att.join(u", "_s) : u"?"_s,
                              !wr.def.isEmpty() ? wr.def.join(u", "_s) : u"?"_s),
            wr.start});
        it->setData(0, Qt::UserRole, rel);
        const QStringList both = wr.att + wr.def;
        if (std::any_of(both.begin(), both.end(), [this](const QString &t) { return !mod.countries.contains(t); })) {
            it->setForeground(1, QBrush(QColor(theme::ERROR_)));
            it->setToolTip(1, u"A nation here is not in the mod"_s);
        }
        lst->addTopLevelItem(it);
        if (cur && rel == *cur)
            pick = it;
    }
    if (pick)
        lst->setCurrentItem(pick);
    lst->blockSignals(false);
    QStringList cbs;
    try {
        for (const script::Node &n : script::parse(store->text(u"common/cb_types.txt"_s)))
            if (n.has_key && !n.key.isEmpty() && n.is_block && n.key != u"peace_order")
                cbs << n.key;
    } catch (const std::exception &) {
        cbs.clear();
    }
    if (cb_box->count() != cbs.size()) {
        cb_box->clear();
        for (const QString &k : cbs)
            cb_box->addItem(u"%1  (%2)"_s.arg(loc(k).value_or(k), k), k);
    }
    _show();
}

void WarsView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    if (it) {
        cur = it->data(0, Qt::UserRole).toString();
        w.reset();
        _show();
    }
}

void WarsView::_show()
{
    const bool ok = cur && rels().contains(*cur);
    form->setEnabled(ok);
    if (!ok) {
        head->setText(u"Pick a war"_s);
        file_note->setText(QString());
        raw->setPlainText(QString());
        return;
    }
    const QString text = store->text(*cur);
    if (!w || w_of != text) {
        w = read_war(text);
        w_of = text;
        raw->setPlainText(text);
    }
    head->setText(!w->name.isEmpty() ? w->name : tail(*cur));
    const bool own = entryview::in_mod(mod, *store, *cur);
    file_note->setText(*cur +
                       (own ? QString() : u" — the game's own file (the mod inherits it; it can't be removed from here)"_s) +
                       (w->simple ? QString()
                                  : u"  ·  ⚠ laid out in a way the pickers can't show: “Save the war” would rewrite it — "
                                    u"use the text below"_s));
    rm_btn->setEnabled(own);
    const std::pair<QLineEdit *, QString> es[3] = {{name, w->name}, {start, w->start}, {end, w->end}};
    for (const auto &[e, v] : es)
        if (!e->hasFocus())
            e->setText(v);
    for (const auto &[side, boxes] : side_box) {
        QListWidget *lw = boxes.first;
        lw->clear();
        for (const QString &t : side == u"att" ? w->att : w->def) {
            auto *it = new QListWidgetItem(nm(t));
            if (!mod.countries.contains(t))
                it->setForeground(QBrush(QColor(theme::ERROR_)));
            lw->addItem(it);
        }
    }
    goals->clear();
    for (const auto &[cb, a, r] : w->goals)
        goals->addItem(u"%1: %2 against %3"_s.arg(loc(cb).value_or(cb), nm(a), nm(r)));
}

void WarsView::_side_add(const QString &side)
{
    const QString t = box_of(side).second->currentData().toString();
    if (w && !t.isEmpty() && !w->att.contains(t) && !w->def.contains(t)) {
        side_of(side).append(t);
        _show();
    }
}

void WarsView::_side_rm(const QString &side)
{
    const int i = box_of(side).first->currentRow();
    if (w && 0 <= i && i < side_of(side).size()) {
        side_of(side).removeAt(i);
        _show();
    }
}

void WarsView::_goal_add()
{
    if (w && !cb_box->currentData().toString().isEmpty()) {
        w->goals.emplace_back(cb_box->currentData().toString(), actor->currentData().toString(),
                              receiver->currentData().toString());
        _show();
    }
}

void WarsView::_goal_rm()
{
    const int i = goals->currentRow();
    if (w && 0 <= i && i < static_cast<int>(w->goals.size())) {
        w->goals.erase(w->goals.begin() + i);
        _show();
    }
}

void WarsView::_save()
{
    if (!w)
        return;
    w->name = QString(name->text()).remove(u'"').simplified();
    w->start = py::strip(start->text());
    w->end = py::strip(end->text());
    if (w->name.isEmpty() || !DATE_RE.match(w->start).hasMatch() || (!w->end.isEmpty() && !DATE_RE.match(w->end).hasMatch())) {
        warn->setText(u"A war needs a name and a start date (year.month.day); the end date may stay empty."_s);
        return;
    }
    if (w->att.isEmpty() || w->def.isEmpty()) {
        warn->setText(u"A war needs at least one attacker and one defender."_s);
        return;
    }
    warn->setText(QString());
    const QString text = war_text(*w);
    const QString nm_ = w->name;
    w.reset();
    emit_text(u"War “%1”"_s.arg(nm_), *cur, text);
    _show();
}

void WarsView::_save_raw()
{
    const QString t = raw->toPlainText();
    if (py::strip(t).isEmpty()) {
        warn->setText(u"An empty war file crashes the game — remove the war instead."_s);
        return;
    }
    if (t.count(u'{') != t.count(u'}')) {
        warn->setText(u"The braces don't match — not saved."_s);
        return;
    }
    warn->setText(QString());
    w.reset();
    emit_text(u"War file "_s + tail(*cur), *cur, t);
}

void WarsView::_new()
{
    const QString nm_ = QString(new_name->text()).remove(u'"').simplified();
    static const QRegularExpression NON(u"[^A-Za-z0-9]+"_s);
    const QString stem = py::title(nm_).remove(NON);
    if (stem.isEmpty()) {
        warn->setText(u"Give the new war a name."_s);
        return;
    }
    const QString rel = u"history/wars/%1.txt"_s.arg(stem);
    if (rels().contains(rel)) {
        warn->setText(u"There is a war file %1 already."_s.arg(rel));
        return;
    }
    warn->setText(QString());
    new_name->clear();
    const QString d = doc && doc->date ? u"%1.%2.%3"_s.arg(std::get<0>(*doc->date)).arg(std::get<1>(*doc->date))
                                             .arg(std::get<2>(*doc->date))
                                       : u"1836.1.1"_s;
    QStringList tags;
    for (const auto &kv : mod.countries)
        tags << kv.first;
    std::sort(tags.begin(), tags.end());
    War nw;
    nw.name = nm_;
    nw.start = d;
    nw.att = tags.mid(0, 1);
    nw.def = tags.mid(1, 1);
    cur = rel;
    w.reset();
    Q_EMIT scriptEdit(u"New war “%1”"_s.arg(nm_), rel, war_text(nw));
}

void WarsView::_remove()
{
    if (cur) {
        const QString rel = *cur;
        cur.reset();
        Q_EMIT fileRemove(u"War %1 removed"_s.arg(tail(rel)), rel);
    }
}

// ---------------------------------------------------------------------------------------------- timed events
static const QRegularExpression TIMED_LINE_RE(uR"(^(\s*)(\d+)\s*=\s*(\d+)\s*(#.*)?$)"_s,   // (Python: LINE_RE)
                                              QRegularExpression::UseUnicodePropertiesOption);
const OrderedMap<QString, QString> HOOKS = {
    {u"on_yearly_pulse"_s, u"Once a year, one of these"_s}, {u"on_quarterly_pulse"_s, u"Every three months, one of these"_s},
    {u"on_battle_won"_s, u"After a battle won"_s}, {u"on_battle_lost"_s, u"After a battle lost"_s},
    {u"on_new_great_nation"_s, u"A nation becomes a great power"_s}, {u"on_lost_great_nation"_s, u"A nation stops being one"_s},
    {u"on_election_tick"_s, u"During an election campaign"_s}, {u"on_colony_to_state"_s, u"A colony becomes a state"_s},
    {u"on_state_conquest"_s, u"A state is conquered"_s}, {u"on_colony_to_state_free_slaves"_s, u"A colony becomes a free state"_s},
    {u"on_debtor_default"_s, u"A debtor defaults"_s}, {u"on_debtor_default_small"_s, u"A small debtor defaults"_s},
    {u"on_debtor_default_second"_s, u"A debtor defaults again"_s}, {u"on_civilize"_s, u"A nation westernises"_s},
    {u"on_my_factories_nationalized"_s, u"Its factories abroad are nationalised"_s},
    {u"on_crisis_declare_interest"_s, u"A great power takes interest in a crisis"_s}};

static QString hook_name(const QString &key, const QString &dflt)   // HOOKS.get(key, dflt)
{
    const QString *x = HOOKS.get(key);
    return x ? *x : dflt;
}

const QString TimedEventsView::REL = u"common/on_actions.txt"_s;

TimedEventsView::TimedEventsView(moddata::Mod &mod_, std::function<OptStr(const QString &)> event_name_, QWidget *parent)
    : Base(mod_, parent), event_name(event_name_ ? std::move(event_name_) : [](const QString &) { return OptStr(); })
{
    QVBoxLayout *rl = two_panes(
        u"Timed events"_s,
        u"The events the game fires by itself (common/on_actions.txt): at each of these moments it picks one event of "
        u"the list, the higher its weight the more often. The events themselves are written on the Scripts page."_s,
        {u"When"_s, u"Events"_s}, u"timedList"_s);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *p) { _picked(it, p); });
    auto *s = new Section();
    head = label(u"Pick a moment"_s, u"title"_s);
    s->lay->addWidget(head);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    rows = new QTreeWidget();
    rows->setColumnCount(3);
    rows->setHeaderLabels({u"Event"_s, u"Its number"_s, u"Weight"_s});
    rows->setRootIsDecorated(false);
    rows->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rows->setTextElideMode(Qt::ElideRight);
    rows->setMinimumHeight(360);
    colsizes::make_resizable(rows, u"timedRows"_s, {std::nullopt, 120, 90}, true);
    s->lay->addWidget(rows);
    auto *row = new QHBoxLayout();
    eid = new QLineEdit();
    eid->setPlaceholderText(u"number of the event to add"_s);
    row->addWidget(eid, 2);
    weight = new QLineEdit(u"100"_s);
    weight->setFixedWidth(90);
    weight->setToolTip(u"Weight: how often against the others of the list"_s);
    row->addWidget(weight);
    auto *b = new QPushButton(u"＋ Add"_s);
    connect(b, &QPushButton::clicked, this, [this] { _add(); });
    row->addWidget(b);
    b = new QPushButton(u"✕ Take the selected off"_s);
    connect(b, &QPushButton::clicked, this, [this] { _remove(); });
    row->addWidget(b);
    s->lay->addLayout(row);
    warn = warn_label();
    s->lay->addWidget(warn);
    rl->addWidget(s);
    rl->addStretch(1);
}

std::vector<script::Node> TimedEventsView::hooks()
{
    std::vector<script::Node> out;
    try {
        for (script::Node &n : script::parse(store->text(REL)))
            if (n.has_key && !n.key.isEmpty() && n.is_block)
                out.push_back(std::move(n));
    } catch (const std::exception &) {
        out.clear();
    }
    return out;
}

std::vector<std::pair<QString, QString>> TimedEventsView::lines(const script::Node &n)
{
    const QString text = store->text(REL);
    std::vector<std::pair<QString, QString>> out;
    for (const QString &ln : py::slice(text, n.vstart + 1, n.vend - 1).split(u'\n')) {
        const QRegularExpressionMatch m = TIMED_LINE_RE.match(ln);
        if (m.hasMatch())
            out.emplace_back(m.captured(2), m.captured(3));
    }
    return out;
}

void TimedEventsView::fill()
{
    lst->blockSignals(true);
    lst->clear();
    QTreeWidgetItem *pick = nullptr;
    for (const script::Node &n : hooks()) {
        auto *it = new QTreeWidgetItem(QStringList{hook_name(n.key, py::capitalize(QString(n.key).replace(u'_', u' '))),
                                                   QString::number(lines(n).size())});
        it->setToolTip(0, n.key);
        it->setData(0, Qt::UserRole, n.key);
        it->setTextAlignment(1, Qt::AlignCenter);
        lst->addTopLevelItem(it);
        if (cur && n.key == *cur)
            pick = it;
    }
    if (pick)
        lst->setCurrentItem(pick);
    lst->blockSignals(false);
    _show();
}

void TimedEventsView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    if (it) {
        cur = it->data(0, Qt::UserRole).toString();
        _show();
    }
}

static std::optional<script::Node> hook_of(std::vector<script::Node> ns, const OptStr &cur)
{
    for (script::Node &x : ns)
        if (cur && x.key == *cur)
            return std::move(x);
    return std::nullopt;
}

void TimedEventsView::_show()
{
    const auto n = hook_of(hooks(), cur);
    rows->clear();
    if (!n) {
        head->setText(u"Pick a moment"_s);
        sub->setText(QString());
        return;
    }
    head->setText(hook_name(n->key, n->key));
    sub->setText(u"%1 · %2"_s.arg(n->key, REL));
    for (const auto &[wgt, e] : lines(*n)) {
        OptStr nm = event_name(e);
        if (!nm || nm->isEmpty())
            nm = loc(u"EVTNAME"_s + e);
        const QString name = nm && !nm->isEmpty() ? *nm : u"?"_s;
        auto *it = new QTreeWidgetItem(QStringList{name, e, wgt});
        it->setData(0, Qt::UserRole, QStringList{wgt, e});
        if (name == u"?") {
            it->setForeground(0, QBrush(QColor(theme::ERROR_)));
            it->setToolTip(0, u"No event with this number was found"_s);
        }
        rows->addTopLevelItem(it);
    }
}

void TimedEventsView::_add()
{
    const auto n = hook_of(hooks(), cur);
    const QString e = py::strip(eid->text()), wg = py::strip(weight->text());
    if (!n || !py::isdigit(e) || !py::isdigit(wg)) {
        warn->setText(u"Pick a moment, and give the event's number and a weight (whole numbers)."_s);
        return;
    }
    warn->setText(QString());
    eid->clear();
    const QString text = store->text(REL);
    emit_text(u"%1: + event %2"_s.arg(hook_name(n->key, n->key), e), REL,
              blocks::_insert_before_close(text, *n, u"%1 = %2"_s.arg(wg, e)));
}

void TimedEventsView::_remove()
{
    QTreeWidgetItem *it = rows->currentItem();
    const auto n = hook_of(hooks(), cur);
    if (!it || !n)
        return;
    const QStringList d = it->data(0, Qt::UserRole).toStringList();
    const QString &wgt = d[0], &e = d[1];
    const QString text = store->text(REL);
    QStringList inner = py::slice(text, n->vstart + 1, n->vend - 1).split(u'\n');
    for (qsizetype i = 0; i < inner.size(); ++i) {
        const QRegularExpressionMatch m = TIMED_LINE_RE.match(inner[i]);
        if (m.hasMatch() && m.captured(2) == wgt && m.captured(3) == e) {
            inner.removeAt(i);
            break;
        }
    }
    emit_text(u"%1: event %2 taken off"_s.arg(hook_name(n->key, n->key), e), REL,
              py::head(text, n->vstart + 1) + inner.join(u'\n') + py::tail(text, n->vend - 1));
}

// ---------------------------------------------------------------------------------------------- flags and variables
const OrderedMap<QString, QString> KIND_NAME = {{u"var"_s, u"Variable"_s}, {u"cflag"_s, u"Country flag"_s},
                                               {u"gflag"_s, u"Global flag"_s}, {u"pflag"_s, u"Province flag"_s}};

IndexView::IndexView(moddata::Mod &mod_, std::function<refs::RefIndex *()> get_refs_,
                     std::function<void(const QString &, long long)> jump_, QWidget *parent)
    : Base(mod_, parent), get_refs(std::move(get_refs_)), jump(std::move(jump_))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *s = new Section();
    s->lay->addWidget(label(u"Flags and variables"_s, u"title"_s));
    s->lay->addWidget(label(u"Every flag and variable the events and decisions use: how often each is set, changed, "
                            u"cleared and checked. One that is checked but never set can never be true; one that is set "
                            u"but never checked does nothing. Double-click a line to go to it in Scripts."_s,
                            u"faint"_s));
    auto *row = new QHBoxLayout();
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a flag or a variable…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill(); });
    row->addWidget(search, 2);
    kind = new QComboBox();
    for (const auto &[k, t] : {std::pair{u""_s, u"Flags and variables"_s}, std::pair{u"var"_s, u"Variables"_s},
                               std::pair{u"cflag"_s, u"Country flags"_s}, std::pair{u"gflag"_s, u"Global flags"_s},
                               std::pair{u"pflag"_s, u"Province flags"_s}})
        kind->addItem(t, k);
    connect(kind, &QComboBox::activated, this, [this](int) { _fill(); });
    row->addWidget(kind, 1);
    show_box = new QComboBox();
    for (const auto &[k, t] : {std::pair{u""_s, u"All"_s}, std::pair{u"noset"_s, u"Checked but never set"_s},
                               std::pair{u"nocheck"_s, u"Set but never checked"_s}})
        show_box->addItem(t, k);
    connect(show_box, &QComboBox::activated, this, [this](int) { _fill(); });
    row->addWidget(show_box, 1);
    s->lay->addLayout(row);
    count = label(QString(), u"faint"_s);
    s->lay->addWidget(count);
    lst = new QTreeWidget();
    lst->setColumnCount(6);
    lst->setHeaderLabels({u"Name"_s, u"Kind"_s, u"Set"_s, u"Changed"_s, u"Cleared"_s, u"Checked"_s});
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setSortingEnabled(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"flagIndex"_s, {std::nullopt, 150, 80, 90, 90, 90}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *p) { _picked(it, p); });
    connect(lst, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int c) { _go(it, c); });
    s->lay->addWidget(lst, 1);
    lay->addWidget(s, 1);
    s = new Section();
    detail = new QLabel(QString());
    detail->setWordWrap(true);
    detail->setTextFormat(Qt::RichText);
    s->lay->addWidget(detail);
    lay->addWidget(s);
}

void IndexView::fill()
{
    refs = get_refs();
    _fill();
}

void IndexView::_fill()
{
    lst->setSortingEnabled(false);
    lst->clear();
    if (!refs) {
        count->setText(u"The scripts are not read yet — open Scripts once."_s);
        return;
    }
    const QString q = py::lower(py::strip(search->text()));
    const QString k_want = kind->currentData().toString(), show = show_box->currentData().toString();
    static const char16_t *ROLES[4] = {u"set", u"change", u"clear", u"check"};
    int n = 0;
    for (const auto &[key, uses] : refs->uses) {
        const auto &[k, name] = key;
        if (!KIND_NAME.contains(k) || uses.empty() || (!k_want.isEmpty() && k != k_want) ||
            (!q.isEmpty() && !py::lower(name).contains(q)))
            continue;
        int c[4] = {0, 0, 0, 0};
        for (const refs::Use &u : uses)
            for (int r = 0; r < 4; ++r)
                if (u.role == QStringView(ROLES[r]))
                    ++c[r];
        const int setter = c[0] + c[1];
        if ((show == u"noset" && (setter || !c[3])) || (show == u"nocheck" && (c[3] || !setter)))
            continue;
        QStringList cols{name, *KIND_NAME.get(k)};
        for (int r = 0; r < 4; ++r)
            cols << (c[r] ? u"%1"_s.arg(c[r], 4) : QString());
        auto *it = new QTreeWidgetItem(cols);
        it->setData(0, Qt::UserRole, QStringList{k, name});
        for (int col : {2, 3, 4, 5})
            it->setTextAlignment(col, Qt::AlignCenter);
        if (!setter && c[3]) {
            it->setForeground(0, QBrush(QColor(theme::ERROR_)));
            it->setToolTip(0, u"Checked but never set"_s);
        } else if (setter && !c[3]) {
            it->setForeground(0, QBrush(QColor(u"#e0a050"_s)));
            it->setToolTip(0, u"Set but never checked"_s);
        }
        lst->addTopLevelItem(it);
        ++n;
    }
    lst->setSortingEnabled(true);
    lst->sortItems(0, Qt::AscendingOrder);
    count->setText(u"%1 shown"_s.arg(n));
}

void IndexView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QStringList d = it ? it->data(0, Qt::UserRole).toStringList() : QStringList();
    if (!d.isEmpty() && refs)
        detail->setText(u"<b>%1</b> — %2<br>%3"_s.arg(d[1], *KIND_NAME.get(d[0]),
                                                     refs->usage_lines(d[0], d[1], 14).join(u"<br>"_s)));
}

void IndexView::_go(QTreeWidgetItem *it, int)
{
    const QStringList d = it->data(0, Qt::UserRole).toStringList();
    if (!d.isEmpty() && refs) {
        auto at = refs->definition(d[0], d[1]);
        if (!at) {
            const auto us = refs->usages(d[0], d[1]);
            if (!us.empty())
                at = std::pair{us.front().rel, us.front().pos};
        }
        if (at)
            jump(at->first, at->second);
    }
}

// ---------------------------------------------------------------------------------------------- the library pages
static const int EVENT_PIC_W = 521, EVENT_PIC_H = 203;   // EVENT_PIC

static bool ends_with_any(const QString &name, const QStringList &exts)
{
    const QString l = py::lower(name);
    return std::any_of(exts.begin(), exts.end(), [&](const QString &e) { return l.endsWith(e); });
}

// os.listdir (raises -> empty: the callers catch OSError / check isdir)
static QStringList listdir(const QString &d)
{
    if (!QFileInfo(d).isDir())
        return {};
    return QDir(d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
}

LibraryPage::LibraryPage(moddata::Mod &mod_, AddFn add_, QWidget *parent) : QWidget(parent), mod(mod_), add(std::move(add_))
{
}

void LibraryPage::refresh(document::Document *doc_)
{
    doc = doc_;
    fill();
}

void LibraryPage::open_folder()
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        status->setText(u"⚠ Could not open %1: %2"_s.arg(folder, u"the folder could not be opened"_s));
}

static AssetEdits *assets_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"assets"_s) ? &doc->map<AssetEdits>(u"assets"_s) : nullptr;
}

PicturesPage::PicturesPage(moddata::Mod &mod_, AddFn add_, UsedFn used_pictures_, QWidget *parent)
    : LibraryPage(mod_, std::move(add_), parent), used_pictures(std::move(used_pictures_))
{
    folder = library_dir(u"pictures"_s);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split);
    auto *s = new Section();
    s->lay->addWidget(label(u"Your picture library"_s, u"title"_s));
    s->lay->addWidget(label(
        u"The editor comes with no pictures of its own — everything here is what YOU put into this folder, so you "
        "know where each picture is from and that you may use it:\n%1\nHow it works: copy pictures into that "
        "folder (png, jpg, bmp, tga, dds), press “Look again”, select the ones you want and press “Add the "
        "selected to the mod”. They are turned into the game's format and written into the mod on Apply; after "
        "that every event and decision can use them (Scripts: the “Picture” box of the event)."_s.arg(folder),
        u"faint"_s));
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"Open the folder"_s);
    connect(b, &QPushButton::clicked, this, [this] { open_folder(); });
    row->addWidget(b);
    b = new QPushButton(u"Look again"_s);
    connect(b, &QPushButton::clicked, this, [this] { fill(); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    lib = _grid();
    lib->setSelectionMode(QAbstractItemView::ExtendedSelection);
    s->lay->addWidget(lib, 1);
    row = new QHBoxLayout();
    target = new QComboBox();
    target->addItem(u"as event pictures (gfx/pictures/events)"_s, u"events"_s);
    target->addItem(u"as decision pictures (gfx/pictures/decisions)"_s, u"decisions"_s);
    target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(target, 2);
    fit_box = new QCheckBox(u"Fit to the event picture size (%1 × %2)"_s.arg(EVENT_PIC_W).arg(EVENT_PIC_H));
    fit_box->setChecked(true);
    row->addWidget(fit_box, 2);
    s->lay->addLayout(row);
    b = new QPushButton(u"Add the selected to the mod ▸"_s);
    b->setObjectName(u"apply"_s);
    connect(b, &QPushButton::clicked, this, [this] { _add(); });
    s->lay->addWidget(b);
    status = label(QString(), u"faint"_s);
    s->lay->addWidget(status);
    split->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"Pictures of the mod"_s, u"title"_s));
    mod_note = label(QString(), u"faint"_s);
    s->lay->addWidget(mod_note);
    only_unused = new QCheckBox(u"Only the ones no event uses"_s);
    connect(only_unused, &QCheckBox::toggled, this, [this](bool) { fill(); });
    s->lay->addWidget(only_unused);
    have = _grid();
    s->lay->addWidget(have, 1);
    split->addWidget(s);
    split->setSizes({820, 820});
}

QListWidget *PicturesPage::_grid()
{
    auto *lw = new QListWidget();
    lw->setViewMode(QListView::IconMode);
    lw->setResizeMode(QListView::Adjust);
    lw->setMovement(QListView::Static);
    lw->setIconSize(QSize(156, 78));
    lw->setGridSize(QSize(176, 112));
    lw->setWordWrap(true);
    lw->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return lw;
}

static QIcon thumb(const QImage &img)
{
    return QIcon(QPixmap::fromImage(img.scaled(156, 78, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void PicturesPage::fill()
{
    lib->clear();
    QStringList names;
    for (const QString &f : listdir(folder))
        if (ends_with_any(f, PIC_EXT))
            names << f;
    std::sort(names.begin(), names.end());
    for (qsizetype i = 0; i < names.size() && i < 600; ++i) {
        const QString &f = names[i];
        const std::optional<QImage> img = imgio::read_image(pypath::join(folder, f));
        auto *it = new QListWidgetItem(img ? thumb(*img) : QIcon(), f);
        it->setData(Qt::UserRole, f);
        it->setToolTip(img ? f : f + u" — could not be read"_s);
        if (!img)
            it->setFlags(Qt::NoItemFlags);
        lib->addItem(it);
    }
    status->setText(!names.isEmpty()
                        ? u"%1 picture%2 in your library%3."_s.arg(names.size())
                              .arg(names.size() == 1 ? u""_s : u"s"_s,
                                   names.size() > 600 ? u" (the first 600 shown)"_s : u""_s)
                        : u"Your library is empty — copy pictures into the folder above, then press “Look again”."_s);
    have->clear();
    AssetEdits *assets = assets_of(doc);
    const std::optional<QSet<QString>> used = used_pictures ? used_pictures() : std::nullopt;
    long long shown = 0, total = 0;
    for (const QString &sub : {u"events"_s, u"decisions"_s}) {
        const QString d = pypath::join(mod.root, {u"gfx"_s, u"pictures"_s, sub});
        QHash<QString, QString> files;
        for (const QString &f : listdir(d))
            if (py::lower(f).endsWith(u".tga"_s) || py::lower(f).endsWith(u".dds"_s))
                files.insert(f, pypath::join(d, f));
        QHash<QString, QImage> pend;
        if (assets)
            for (const auto &[rel, a] : assets->fresh)
                if (rel.startsWith(u"gfx/pictures/%1/"_s.arg(sub)) && a.how == u"img")
                    pend.insert(tail(rel), a.img);
        QStringList all = files.keys();
        for (auto it = pend.cbegin(); it != pend.cend(); ++it)
            if (!files.contains(it.key()))
                all << it.key();
        std::sort(all.begin(), all.end());            // (a set: equal lower-case names in no set order)
        std::stable_sort(all.begin(), all.end(), [](const QString &a, const QString &b) { return py::lower(a) < py::lower(b); });
        for (const QString &f : all) {
            ++total;
            const QString stem = pypath::splitext_stem(f);
            const bool is_used = !used || sub != u"events" || used->contains(py::lower(stem));
            if (only_unused->isChecked() && (is_used || sub != u"events"))
                continue;
            if (shown >= 500)
                continue;
            std::optional<QImage> img;
            if (pend.contains(f) && !pend.value(f).isNull())
                img = pend.value(f);
            else if (files.contains(f))
                img = imgio::read_image(files.value(f));
            auto *it = new QListWidgetItem(img ? thumb(*img) : QIcon(),
                                           stem + (pend.contains(f) ? u" (new)"_s : u""_s) + (is_used ? u""_s : u" — unused"_s));
            it->setToolTip(u"gfx/pictures/%1/%2%3"_s.arg(sub, f, is_used ? u""_s : u"\nNo event uses it"_s));
            if (!is_used)
                it->setForeground(QBrush(QColor(u"#e0a050"_s)));
            have->addItem(it);
            ++shown;
        }
    }
    mod_note->setText(u"%1 picture%2 in the mod's own gfx/pictures (the game's own are there as well, unless the mod "
                      "replaces that folder)%3."_s.arg(total)
                          .arg(total == 1 ? u""_s : u"s"_s,
                               used ? u""_s : u" — open Scripts once to see which ones are unused"_s));
}

void PicturesPage::_add()
{
    const QList<QListWidgetItem *> items = lib->selectedItems();
    if (items.isEmpty()) {
        status->setText(u"Select pictures in your library first (Ctrl or Shift for several)."_s);
        return;
    }
    const QString sub = target->currentData().toString();
    OrderedMap<QString, Asset> files;
    for (QListWidgetItem *it : items) {
        const QString f = it->data(Qt::UserRole).toString();
        std::optional<QImage> img = imgio::read_image(pypath::join(folder, f));
        if (!img)
            continue;
        if (fit_box->isChecked())
            img = fit(*img, EVENT_PIC_W, EVENT_PIC_H);
        Asset a;
        a.how = u"img"_s;
        a.img = img->convertToFormat(QImage::Format_RGB32);
        files.insert(u"gfx/pictures/%1/%2.tga"_s.arg(sub, safe_stem(f)), a);
    }
    if (files.size()) {
        add(u"%1 picture%2 from the library"_s.arg(files.size()).arg(files.size() == 1 ? u""_s : u"s"_s), files);
        fill();
        QStringList rels;
        for (const auto &kv : files)
            rels << kv.first;
        std::sort(rels.begin(), rels.end());
        QStringList stems;
        for (const QString &r : rels)
            stems << pypath::splitext_stem(tail(r));
        status->setText(u"%1 added: %2 — written on Apply."_s.arg(files.size()).arg(py::head(stems.join(u", "_s), 160)));
    }
}

MusicPage::MusicPage(moddata::Mod &mod_, AddFn add_, QWidget *parent) : LibraryPage(mod_, std::move(add_), parent)
{
    folder = library_dir(u"music"_s);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split);
    auto *s = new Section();
    s->lay->addWidget(label(u"Your music library"_s, u"title"_s));
    s->lay->addWidget(label(
        u"The editor comes with no music of its own — everything here is what YOU put into this folder, so you "
        "know where each piece is from and that you may use it:\n%1\nHow it works: copy mp3 files into that "
        "folder, press “Look again”, select the ones you want and press “Add the selected to the mod”. They "
        "are copied into the mod's music folder on Apply. The game plays every mp3 of that folder in turn, "
        "together with its own — there is no list to keep."_s.arg(folder),
        u"faint"_s));
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"Open the folder"_s);
    connect(b, &QPushButton::clicked, this, [this] { open_folder(); });
    row->addWidget(b);
    b = new QPushButton(u"Look again"_s);
    connect(b, &QPushButton::clicked, this, [this] { fill(); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    lib = new QListWidget();
    lib->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lib->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->lay->addWidget(lib, 1);
    b = new QPushButton(u"Add the selected to the mod ▸"_s);
    b->setObjectName(u"apply"_s);
    connect(b, &QPushButton::clicked, this, [this] { _add(); });
    s->lay->addWidget(b);
    status = label(QString(), u"faint"_s);
    s->lay->addWidget(status);
    split->addWidget(s);
    s = new Section();
    s->lay->addWidget(label(u"Music of the mod"_s, u"title"_s));
    mod_note = label(QString(), u"faint"_s);
    s->lay->addWidget(mod_note);
    have = new QListWidget();
    have->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->lay->addWidget(have, 1);
    split->addWidget(s);
    split->setSizes({820, 820});
}

void MusicPage::fill()
{
    lib->clear();
    QStringList names;
    for (const QString &f : listdir(folder))
        if (ends_with_any(f, MUSIC_EXT))
            names << f;
    std::sort(names.begin(), names.end());
    for (const QString &f : names) {
        const double mb = static_cast<double>(QFileInfo(pypath::join(folder, f)).size()) / 1048576.0;
        auto *it = new QListWidgetItem(u"%1   (%2 MB)"_s.arg(f, QString::number(mb, 'f', 1)));
        it->setData(Qt::UserRole, f);
        lib->addItem(it);
    }
    status->setText(!names.isEmpty()
                        ? u"%1 piece%2 in your library."_s.arg(names.size()).arg(names.size() == 1 ? u""_s : u"s"_s)
                        : u"Your library is empty — copy mp3 files into the folder above, then press “Look again”."_s);
    have->clear();
    const QString d = pypath::join(mod.root, u"music"_s);
    QStringList files;
    for (const QString &f : listdir(d))
        if (ends_with_any(f, MUSIC_EXT))
            files << f;
    std::sort(files.begin(), files.end());
    QStringList pend;
    if (AssetEdits *assets = assets_of(doc))
        for (const auto &kv : assets->fresh)
            if (kv.first.startsWith(u"music/"_s))
                pend << tail(kv.first);
    std::sort(pend.begin(), pend.end());
    for (const QString &f : files)
        have->addItem(f);
    long long more = 0;
    for (const QString &f : pend)
        if (!files.contains(f)) {
            auto *it = new QListWidgetItem(f + u"   (new — copied on Apply)"_s);
            it->setForeground(QBrush(QColor(theme::GOLD)));
            have->addItem(it);
            ++more;
        }
    mod_note->setText(u"%1 mp3 in the mod's music folder%2."_s.arg(files.size()).arg(
        !pend.isEmpty() ? u", %1 more on Apply"_s.arg(more) : u""_s));
}

void MusicPage::_add()
{
    const QList<QListWidgetItem *> items = lib->selectedItems();
    if (items.isEmpty()) {
        status->setText(u"Select pieces in your library first (Ctrl or Shift for several)."_s);
        return;
    }
    OrderedMap<QString, Asset> files;
    for (QListWidgetItem *it : items) {
        const QString f = it->data(Qt::UserRole).toString();
        Asset a;
        a.how = u"file"_s;
        a.path = pypath::join(folder, f);
        files.insert(u"music/"_s + f, a);
    }
    add(u"%1 piece%2 of music from the library"_s.arg(files.size()).arg(files.size() == 1 ? u""_s : u"s"_s), files);
    fill();
    status->setText(u"%1 added — copied into the mod's music folder on Apply."_s.arg(files.size()));
}

// ---------------------------------------------------------------------------------------------- almanacs
static qsizetype cp_len(const QString &s) { return s.toUcs4().size(); }   // len(str): code points

QString table(const QStringList &head, const std::vector<QStringList> &rows)
{
    std::vector<QStringList> cols;
    for (qsizetype i = 0; i < head.size(); ++i) {
        QStringList c{head[i]};
        for (const QStringList &r : rows)
            c << r.value(i);
        cols.push_back(c);
    }
    std::vector<qsizetype> widths;
    for (const QStringList &c : cols) {
        qsizetype w = 0;
        for (const QString &x : c)
            w = std::max(w, cp_len(x));
        widths.push_back(w);
    }
    std::vector<bool> left;                       // numbers to the right, words to the left
    for (size_t i = 0; i < cols.size(); ++i) {
        bool l = i == 0;
        for (qsizetype k = 1; !l && k < cols[i].size(); ++k)
            if (!cols[i][k].isEmpty() && !NUM_RE.match(cols[i][k]).hasMatch())
                l = true;
        left.push_back(l);
    }
    auto line = [&](const QStringList &r) {
        QStringList parts;
        for (size_t i = 0; i < widths.size() && i < size_t(r.size()); ++i) {
            const QString &x = r[qsizetype(i)];
            const QString pad(std::max<qsizetype>(0, widths[i] - cp_len(x)), u' ');
            parts << (left[i] ? x + pad : pad + x);
        }
        return py::rstrip(parts.join(u"  "_s));
    };
    QStringList out{line(head)};
    QStringList dashes;
    for (qsizetype w : widths)
        dashes << QString(w, u'-');
    out << dashes.join(u"  "_s);
    for (const QStringList &r : rows)
        out << line(r);
    return out.join(u'\n');
}

QString AlmanacPage::now_override;

AlmanacPage::AlmanacPage(moddata::Mod &mod_, QWidget *parent) : Base(mod_, parent)
{
    static const QRegularExpression BAD(u"[^\\w .-]"_s, QRegularExpression::UseUnicodePropertiesOption);
    dir = pypath::join(app_dir(), {u"almanacs"_s, QString(!mod.name().isEmpty() ? mod.name() : u"mod"_s).replace(BAD, u"_"_s)});
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *s = new Section();
    s->lay->addWidget(label(u"Almanacs"_s, u"title"_s));
    s->lay->addWidget(label(u"Reference sheets written from the mod as it is in the editor now (pending edits "
                            "included): the stats of every army and navy unit, the war goals, the government "
                            "forms, the national values and the technology schools — plain text tables to post "
                            "or keep next to the changelog. They are written to %1."_s.arg(dir),
                            u"faint"_s));
    auto *row = new QHBoxLayout();
    pick = new QComboBox();
    connect(pick, &QComboBox::activated, this, [this](int) { _show(); });
    row->addWidget(pick, 2);
    auto *b = new QPushButton(u"Write all the almanacs"_s);
    b->setObjectName(u"apply"_s);
    connect(b, &QPushButton::clicked, this, [this] { _write(); });
    row->addWidget(b, 1);
    b = new QPushButton(u"Open the folder"_s);
    connect(b, &QPushButton::clicked, this, [this] { _open(); });
    row->addWidget(b);
    s->lay->addLayout(row);
    b = new QPushButton(u"Write it into the mod now"_s);
    b->setToolTip(u"The almanac as one page (\"<mod name> almanac.html\", every sheet a table) into the mod's own "
                  u"folder, now — without an Apply. With the tick \"Almanac with every Apply\" next to Apply, every "
                  u"Apply writes it anew."_s);
    connect(b, &QPushButton::clicked, this, [this] { _write_mod(); });
    row->addWidget(b);
    status = label(QString(), u"faint"_s);
    s->lay->addWidget(status);
    // the sheet as a table: every number in its own column (the user 2026-11-15: "neat ... like an Excel sheet")
    sheet_head = label(QString(), u"sub"_s);
    sheet_head->setTextInteractionFlags(Qt::TextSelectableByMouse);
    s->lay->addWidget(sheet_head);
    grid = new QTableWidget();
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid->setAlternatingRowColors(true);
    grid->setWordWrap(true);
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // (no sideways scrolling: the wide columns wrap)
    grid->verticalHeader()->hide();
    grid->horizontalHeader()->setHighlightSections(false);
    grid->setStyleSheet(u"QTableWidget { alternate-background-color: %1; gridline-color: %2; }"_s.arg(theme::PANEL_ALT,
                                                                                                  theme::LINE_SOFT));
    auto *copy = new QShortcut(QKeySequence::Copy, grid);
    connect(copy, &QShortcut::activated, this, [this] { _copy(); });
    s->lay->addWidget(grid, 1);
    lay->addWidget(s, 1);
}

QString almanac_name(const moddata::Mod &mod, document::Document *doc, const QString &k)
{
    static const QRegularExpression SEC(u"§.|§"_s);
    localisation::LocEdits *lc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr l;
    if (lc)
        l = lc->text(k);
    else if (const QString *x = mod.loc.get(k))
        l = *x;
    const QString t = py::strip(QString(l.value_or(QString())).remove(SEC));
    return !t.isEmpty() ? t : py::capitalize(QString(k).replace(u'_', u' '));
}

QString AlmanacPage::nm(const QString &k) const { return almanac_name(mod, doc, k); }

static bool has_key(const script::Node &n) { return n.has_key && !n.key.isEmpty(); }

static bool rows_less(const QStringList &a, const QStringList &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

QString almanac_now()
{
    return !AlmanacPage::now_override.isEmpty() ? AlmanacPage::now_override
                                                : QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm"_s);
}

OrderedMap<QString, QString> AlmanacPage::build()
{
    const QString now = almanac_now();
    const QString scen = doc ? doc->scenario : QString();
    tabs = almanac_sheets(mod, store, [this](const QString &k) { return nm(k); });
    OrderedMap<QString, QString> out;
    for (const AlmanacSheet &t : tabs)
        out.insert(t.title, u"%1 — %2\n(scenario %3, written %4)\n\n"_s.arg(mod.name(), t.what, scen, now) +
                                table(t.head, t.rows));
    return out;
}

std::vector<AlmanacSheet> almanac_sheets(moddata::Mod &mod, scriptstore::ScriptStore *st,
                                         const std::function<QString(const QString &)> &nm)
{
    std::vector<AlmanacSheet> out;
    if (!st)
        return out;
    std::vector<QStringList> land, sea;
    QStringList d_units = entryview::_files(mod, st, u"units"_s);
    std::sort(d_units.begin(), d_units.end());
    d_units.erase(std::unique(d_units.begin(), d_units.end()), d_units.end());
    for (const QString &rel : d_units) {
        script::Nodes nodes;
        try {
            nodes = script::parse(st->text(rel));
        } catch (...) {
            continue;
        }
        for (const script::Node &n : nodes) {
            if (!(has_key(n) && n.is_block))
                continue;
            const OrderedMap<QString, QString> f = plain_fields(n);
            if (!f.contains(u"type"_s))
                continue;
            QStringList cost;
            for (const script::Node &c : n.kids)
                if (c.has_key && c.key == u"build_cost" && c.is_block) {
                    for (const script::Node &x : c.kids)
                        if (has_key(x))
                            cost << u"%1 %2"_s.arg(unq(x.value), x.key);
                    break;
                }
            auto g = [&](const QString &k) { return f.value(k); };
            if (f.value(u"type"_s) == u"naval")
                sea.push_back({nm(n.key), g(u"hull"_s), g(u"gun_power"_s), g(u"fire_range"_s), g(u"evasion"_s),
                               g(u"torpedo_attack"_s), g(u"maximum_speed"_s), g(u"build_time"_s),
                               g(u"supply_consumption"_s), g(u"min_port_level"_s), cost.join(u", "_s)});
            else
                land.push_back({nm(n.key), g(u"attack"_s), g(u"defence"_s), g(u"discipline"_s), g(u"support"_s),
                                g(u"maneuver"_s), g(u"reconnaissance"_s), g(u"siege"_s), g(u"maximum_speed"_s),
                                g(u"build_time"_s), g(u"supply_consumption"_s), cost.join(u", "_s)});
        }
    }
    std::sort(land.begin(), land.end(), rows_less);
    std::sort(sea.begin(), sea.end(), rows_less);
    out.push_back({u"Army unit stats"_s, u"army unit stats (before technologies)"_s,
                   {u"Unit"_s, u"Attack"_s, u"Defence"_s, u"Discipline"_s, u"Support"_s, u"Maneuver"_s, u"Recon"_s,
                    u"Siege"_s, u"Speed"_s, u"Days"_s, u"Supply"_s, u"Costs to build"_s},
                   land});
    out.push_back({u"Navy unit stats"_s, u"navy unit stats (before technologies)"_s,
                   {u"Unit"_s, u"Hull"_s, u"Guns"_s, u"Range"_s, u"Evasion"_s, u"Torpedo"_s, u"Speed"_s, u"Days"_s,
                    u"Supply"_s, u"Port"_s, u"Costs to build"_s},
                   sea});
    std::vector<QStringList> rows;
    try {
        for (const script::Node &n : script::parse(st->text(u"common/cb_types.txt"_s)))
            if (has_key(n) && n.is_block && n.key != u"peace_order") {
                const OrderedMap<QString, QString> f = plain_fields(n);
                QStringList po;
                for (const auto &[k, v] : f)
                    if (k.startsWith(u"po_"_s) && v == u"yes")
                        po << k.mid(3).replace(u'_', u' ');
                rows.push_back({nm(n.key), f.value(u"badboy_factor"_s), f.value(u"prestige_factor"_s),
                                f.value(u"peace_cost_factor"_s), f.value(u"months"_s), f.value(u"truce_months"_s),
                                f.value(u"is_triggered_only"_s) == u"yes" ? u"yes"_s : u""_s, po.join(u", "_s)});
            }
    } catch (...) {
    }
    out.push_back({u"War goals"_s, u"war goals"_s,
                   {u"War goal"_s, u"Infamy"_s, u"Prestige"_s, u"Peace cost"_s, u"Months"_s, u"Truce"_s,
                    u"By event only"_s, u"It may demand"_s},
                   rows});
    rows.clear();
    try {
        const QStringList ideos = defpanel::ideologies(*st);
        for (const script::Node &n : script::parse(st->text(u"common/governments.txt"_s)))
            if (has_key(n) && n.is_block) {
                const OrderedMap<QString, QString> f = plain_fields(n);
                QStringList may;
                for (const QString &i : ideos)
                    if (f.value(i) == u"yes")
                        may << nm(i);
                rows.push_back({nm(n.key),
                                f.value(u"election"_s) == u"yes"
                                    ? u"every %1 months"_s.arg(f.value(u"duration"_s, u"?"_s))
                                    : u"none"_s,
                                f.value(u"appoint_ruling_party"_s) == u"yes" ? u"yes"_s : u"no"_s,
                                f.value(u"flagType"_s, u"normal"_s), may.join(u", "_s)});
            }
    } catch (...) {
    }
    out.push_back({u"Government forms"_s, u"government forms"_s,
                   {u"Government form"_s, u"Elections"_s, u"Appoints party"_s, u"Flag"_s, u"Parties that may rule"_s},
                   rows});
    const std::vector<std::tuple<QString, QString, QStringList>> lists = {
        {u"National values"_s, u"common/nationalvalues.txt"_s, {}},
        {u"Technology schools"_s, u"common/technology.txt"_s, {u"schools"_s}}};
    for (const auto &[title, rel, base] : lists) {
        rows.clear();
        try {
            const QString text = st->text(rel);
            for (const script::Node &top : blocks::children_top(text, base))
                if (top.is_block) {
                    QStringList gives;
                    for (const script::Node &c : blocks::children(text, base + QStringList{top.key}))
                        if (!c.is_block) {
                            const QString vv = py::strip(c.value);
                            if (vv != u"0" && vv != u"0.0")
                                gives << u"%1 %2"_s.arg(QString(c.key).replace(u'_', u' '), vv);
                        }
                    rows.push_back({nm(top.key), gives.join(u", "_s)});
                }
        } catch (...) {
        }
        out.push_back({title, py::lower(title), {title.left(title.size() - 1), u"What it gives"_s}, rows});
    }
    return out;
}

static const QRegularExpression &num_cell()
{
    static const QRegularExpression re(u"^-?\\d+(\\.\\d+)?%?$"_s);
    return re;
}

QString almanac_html(const QString &mod_name, const QString &scen, const QString &now,
                     const std::vector<AlmanacSheet> &sheets)
{
    auto e = [](const QString &t) { return t.toHtmlEscaped(); };
    QString h;
    h += u"<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"_s;
    h += u"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"_s;
    h += u"<title>%1 almanac</title>\n"_s.arg(e(mod_name));
    h += uR"(<style>
:root { --bg: #f7f5f0; --ink: #1d1d1f; --dim: #6b6b70; --line: #d9d4c7; --head: #2a2f3a; --stripe: #efebe2; --gold: #b8892e; }
@media (prefers-color-scheme: dark) { :root { --bg: #14171c; --ink: #e8e6e0; --dim: #9a9aa2; --line: #2e333d;
  --head: #232833; --stripe: #1a1e25; --gold: #c9a45c; } }
body { background: var(--bg); color: var(--ink); font: 15px/1.45 "Segoe UI", system-ui, sans-serif; margin: 0; padding: 24px 16px 48px; }
main { max-width: 1200px; margin: 0 auto; }
h1 { font-size: 26px; margin: 0 0 4px; }
.sub { color: var(--dim); margin: 0 0 18px; }
nav a { color: var(--gold); margin-right: 14px; text-decoration: none; font-weight: 600; }
h2 { font-size: 19px; margin: 34px 0 10px; border-bottom: 2px solid var(--gold); padding-bottom: 4px; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }
th, td { padding: 5px 9px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { background: var(--head); color: #f1ead8; text-align: left; font-weight: 600; position: sticky; top: 0; }
tbody tr:nth-child(even) { background: var(--stripe); }
td.n, th.n { text-align: right; white-space: nowrap; }
td:first-child { font-weight: 600; }
.empty { color: var(--dim); font-style: italic; }
</style>
</head>
<body><main>
)"_s;
    h += u"<h1>%1 almanac</h1>\n<p class=\"sub\">Scenario %2 · written %3 by ModEditor</p>\n<nav>"_s.arg(
        e(mod_name), e(scen), e(now));
    for (size_t i = 0; i < sheets.size(); ++i)
        h += u"<a href=\"#s%1\">%2</a>"_s.arg(i).arg(e(sheets[i].title));
    h += u"</nav>\n"_s;
    for (size_t i = 0; i < sheets.size(); ++i) {
        const AlmanacSheet &s = sheets[i];
        h += u"<h2 id=\"s%1\">%2</h2>\n"_s.arg(i).arg(e(s.title));
        if (s.rows.empty()) {
            h += u"<p class=\"empty\">None in this mod.</p>\n"_s;
            continue;
        }
        std::vector<bool> num(size_t(s.head.size()), false);   // a column of numbers only: right-aligned
        for (qsizetype c = 1; c < s.head.size(); ++c) {
            bool any = false, all = true;
            for (const QStringList &r : s.rows) {
                const QString x = r.value(c);
                if (x.isEmpty())
                    continue;
                any = true;
                all = all && num_cell().match(x).hasMatch();
            }
            num[size_t(c)] = any && all;
        }
        h += u"<table>\n<thead><tr>"_s;
        for (qsizetype c = 0; c < s.head.size(); ++c)
            h += num[size_t(c)] ? u"<th class=\"n\">%1</th>"_s.arg(e(s.head[c])) : u"<th>%1</th>"_s.arg(e(s.head[c]));
        h += u"</tr></thead>\n<tbody>\n"_s;
        for (const QStringList &r : s.rows) {
            h += u"<tr>"_s;
            for (qsizetype c = 0; c < s.head.size(); ++c)
                h += num[size_t(c)] ? u"<td class=\"n\">%1</td>"_s.arg(e(r.value(c))) : u"<td>%1</td>"_s.arg(e(r.value(c)));
            h += u"</tr>\n"_s;
        }
        h += u"</tbody>\n</table>\n"_s;
    }
    h += u"</main></body>\n</html>\n"_s;
    return h;
}

QString almanac_file(const QString &folder, const QString &mod_name)
{
    static const QRegularExpression BAD(uR"([<>:"/\\|?*\x00-\x1f])"_s);
    QString n = QString(!mod_name.isEmpty() ? mod_name : u"mod"_s).replace(BAD, u"_"_s).trimmed();
    return pypath::join(folder, u"%1 almanac.html"_s.arg(n));
}

std::pair<QString, QString> write_almanac(moddata::Mod &mod, document::Document *doc, const QString &folder)
{
    scriptstore::ScriptStore *st =
        doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!st)
        return {QString(), u"the mod's scripts are not read"_s};
    const auto sheets = almanac_sheets(mod, st, [&](const QString &k) { return almanac_name(mod, doc, k); });
    const QString path = almanac_file(folder, mod.name());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(almanac_html(mod.name(), doc ? doc->scenario : QString(), almanac_now(), sheets).toUtf8()) < 0 ||
        !f.commit())
        return {QString(), u"could not write %1"_s.arg(path)};
    return {path, QString()};
}

void AlmanacPage::fill()
{
    sheets = build();
    const QString cur = pick->currentText();
    pick->clear();
    for (const auto &kv : sheets)
        pick->addItem(kv.first);
    if (sheets.contains(cur))
        pick->setCurrentText(cur);
    _show();
}

void AlmanacPage::_show()
{
    const QString cur = pick->currentText();
    const auto it = std::find_if(tabs.begin(), tabs.end(), [&](const AlmanacSheet &t) { return t.title == cur; });
    grid->clear();
    if (it == tabs.end()) {
        sheet_head->clear();
        grid->setRowCount(0);
        grid->setColumnCount(0);
        return;
    }
    sheet_head->setText(u"%1 — %2  (scenario %3)"_s.arg(mod.name(), it->what, doc ? doc->scenario : QString()));
    grid->setColumnCount(int(it->head.size()));
    grid->setHorizontalHeaderLabels(it->head);
    grid->setRowCount(int(it->rows.size()));
    std::vector<bool> num(size_t(it->head.size()), false);   // a column of numbers only: right-aligned
    for (qsizetype c = 1; c < it->head.size(); ++c) {
        bool any = false, all = true;
        for (const QStringList &r : it->rows)
            if (const QString x = r.value(c); !x.isEmpty()) {
                any = true;
                all = all && num_cell().match(x).hasMatch();
            }
        num[size_t(c)] = any && all;
    }
    QFont bold = grid->font();
    bold.setBold(true);
    for (int r = 0; r < int(it->rows.size()); ++r)
        for (int c = 0; c < int(it->head.size()); ++c) {
            auto *cell = new QTableWidgetItem(it->rows[size_t(r)].value(c));
            cell->setTextAlignment(num[size_t(c)] ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
            if (c == 0)
                cell->setFont(bold);
            grid->setItem(r, c, cell);
        }
    QHeaderView *hh = grid->horizontalHeader();
    for (int c = 0; c < int(it->head.size()); ++c)   // numbers as wide as they need, words share the rest and wrap
        hh->setSectionResizeMode(c, num[size_t(c)] || c == 0 ? QHeaderView::ResizeToContents : QHeaderView::Stretch);
    for (int c = 0; c < int(it->head.size()); ++c)
        if (num[size_t(c)])
            if (QTableWidgetItem *hd = grid->horizontalHeaderItem(c))
                hd->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->resizeRowsToContents();
}

void AlmanacPage::_copy()
{
    const QList<QTableWidgetSelectionRange> rs = grid->selectedRanges();
    if (rs.isEmpty())
        return;
    QStringList lines;
    for (const QTableWidgetSelectionRange &r : rs)
        for (int y = r.topRow(); y <= r.bottomRow(); ++y) {
            QStringList cells;
            for (int x = r.leftColumn(); x <= r.rightColumn(); ++x) {
                const QTableWidgetItem *it = grid->item(y, x);
                cells << (it ? it->text() : QString());
            }
            lines << cells.join(u'\t');
        }
    QGuiApplication::clipboard()->setText(lines.join(u'\n'));
}

void AlmanacPage::_write_mod()
{
    const QString folder = mod.submod ? mod.submod->root : mod.root;
    const auto [path, err] = write_almanac(mod, doc, folder);
    status->setText(err.isEmpty() ? u"Written: %1"_s.arg(path) : u"⚠ The almanac was not written: %1"_s.arg(err));
}

void AlmanacPage::_write()
{
    if (!QDir().mkpath(dir)) {
        status->setText(u"⚠ Could not write: %1"_s.arg(dir));
        return;
    }
    for (const auto &[name, text] : build()) {
        QFile f(pypath::join(dir, u"%1.txt"_s.arg(name)));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write((text + u"\n"_s).toUtf8()) < 0) {
            status->setText(u"⚠ Could not write: %1"_s.arg(f.fileName()));
            return;
        }
    }
    status->setText(u"Written: %1 almanacs in %2"_s.arg(sheets.size()).arg(dir));
}

void AlmanacPage::_open()
{
    QDir().mkpath(dir);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
        status->setText(u"⚠ Could not open %1: %2"_s.arg(dir, u"the folder could not be opened"_s));
}

}  // namespace moreviews
