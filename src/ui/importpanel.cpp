// importpanel.py, the Import page (see importpanel.h).
#include "ui/importpanel.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/importer.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/submods.h"
#include "ui/colsizes.h"
#include "ui/regionimport.h"
#include "ui/scriptedit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace importpanel {

const std::map<QString, Status> STATUS = {{u"new"_s, {u"new"_s, u"#7fd68a"_s}},
                                          {u"differs"_s, {u"different from yours"_s, u"#ffb347"_s}},
                                          {u"same"_s, {u"you have it"_s, u"#6f7682"_s}}};
const int FILE_LIMIT = 4000;

namespace {
class NoRefs : public scriptedit::EditorHost {   // (the preview's highlighter: no references to look up)
public:
    scriptedit::ScriptEditor *current() const override { return nullptr; }
    void _cursor_moved(scriptedit::ScriptEditor *) override {}
    void _decorate(scriptedit::ScriptEditor *) override {}
    void zoom(int) override {}
    void back() override {}
    void forward() override {}
    void follow(const refs::Ref &, bool) override {}
    QString hover_html(const QString &, const refs::Ref &) const override { return QString(); }
};

QString read_universal(const QString &path)       // open(path, "r", encoding="latin-1").read(): newlines as "\n"
{
    QString t = script::read_text(path);
    t.replace(u"\r\n"_s, u"\n"_s);
    t.replace(u'\r', u'\n');
    return t;
}

std::pair<long long, const void *> mark_of(const document::Document &d)
{
    return {static_cast<long long>(d.undo_stack.size()), d.undo_stack.empty() ? nullptr : d.undo_stack.back().get()};
}
}  // namespace

ImportWorkspace::ImportWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *s = new Section();
    s->lay->addWidget(label(u"Import from another mod"_s, u"title"_s));
    s->lay->addWidget(label(
        u"Take things over from another mod (or from the game itself) into this one: pick the mod, tick what you "
        u"want — on the right you see everything that comes along with it and what to watch out for. Nothing is "
        u"changed until you press Import; then it is one step you can undo, and Apply writes it. The other mod is "
        u"only read, never changed."_s,
        u"faint"_s));
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(label(u"The other mod"_s, u"sub"_s, false));
    src_box = new QComboBox();
    src_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    src_box->setFixedWidth(420);
    connect(src_box, &QComboBox::activated, this, [this](int) { _open(src_box->currentData().toString()); });
    row->addWidget(src_box);
    path = new QLineEdit();
    path->setPlaceholderText(u"… or paste the folder of any mod here and press Enter"_s);
    connect(path, &QLineEdit::returnPressed, this, [this] { _open(py::strip(py::strip(path->text()), u"\""_s)); });
    row->addWidget(path, 1);
    auto *b = new QPushButton(u"Browse…"_s);
    connect(b, &QPushButton::clicked, this, [this] { _browse(); });
    row->addWidget(b);
    s->lay->addLayout(row);
    src_info = label(QString(), u"sub"_s);
    s->lay->addWidget(src_info);
    // what to take: things by their name — or provinces picked on its map
    row = new QHBoxLayout();
    row->setSpacing(0);
    const std::tuple<QString, QString, QString> modes[2] = {
        {u"things"_s, u"Things — events, nations, units, modifiers …"_s,
         u"Tick what you want in lists; what it needs comes along"_s},
        {u"map"_s, u"Provinces from its map"_s,
         u"Pick provinces on the other mod's map and bring them over as they are — with or without their countries"_s}};
    for (const auto &[k, t, tip] : modes) {
        auto *mb = new QPushButton(t);
        mb->setObjectName(u"seg"_s);
        mb->setCheckable(true);
        mb->setChecked(k == mode);
        mb->setToolTip(tip);
        connect(mb, &QPushButton::clicked, this, [this, kk = k] { set_mode(kk); });
        row->addWidget(mb, 1);
        mode_btn[k] = mb;
    }
    s->lay->addLayout(row);
    lay->addWidget(s);
    auto *split = things = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);
    region = new regionimport::RegionImportPage(mod);
    connect(region, &regionimport::RegionImportPage::message, this, &ImportWorkspace::message);
    connect(region, &regionimport::RegionImportPage::imported, this, &ImportWorkspace::imported);
    region->hide();
    lay->addWidget(region, 1);
    // --- the kinds
    s = new Section();
    s->lay->addWidget(_bold(u"What to look at"_s));
    cats = new QTreeWidget();
    cats->setColumnCount(3);
    cats->setHeaderLabels({u"Kind"_s, u"New"_s, u"Different"_s});
    cats->setRootIsDecorated(false);
    cats->setUniformRowHeights(true);
    cats->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cats->setColumnWidth(0, 205);
    cats->setColumnWidth(1, 62);
    connect(cats, &QTreeWidget::currentItemChanged, this, &ImportWorkspace::_cat_picked);
    s->lay->addWidget(cats, 1);
    split->addWidget(s);
    // --- the things of the kind
    auto *mid = new Section();
    cat_title = label(QString(), u"title"_s);
    mid->lay->addWidget(cat_title);
    cat_about = label(QString(), u"faint"_s);
    mid->lay->addWidget(cat_about);
    row = new QHBoxLayout();
    row->setSpacing(6);
    search = new QLineEdit();
    search->setPlaceholderText(u"Find (name, key, id, file)…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_timer->start(); });
    row->addWidget(search, 1);
    show_box = new QComboBox();
    show_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    show_box->setFixedWidth(250);
    show_box->addItem(u"what you don't have, or have differently"_s, u"news"_s);
    show_box->addItem(u"only what you don't have"_s, u"new"_s);
    show_box->addItem(u"everything of the other mod"_s, u"all"_s);
    connect(show_box, &QComboBox::activated, this, [this](int) { _fill_items(); });
    row->addWidget(show_box);
    mid->lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(6);
    for (const auto &[t, tip, on] : {std::tuple{u"Tick all shown"_s, u"Tick everything in the list below"_s, true},
                                      std::tuple{u"Untick all shown"_s, QString(), false}}) {
        auto *tb = new QPushButton(t);
        tb->setToolTip(tip);
        connect(tb, &QPushButton::clicked, this, [this, on = on] { _tick_all(on); });
        row->addWidget(tb);
    }
    chain_btn = new QPushButton(u"＋ the chains of the ticked events"_s);
    chain_btn->setToolTip(u"Also tick every event that calls a ticked event or is called by it — the whole chain, however "
                          u"long"_s);
    connect(chain_btn, &QPushButton::clicked, this, [this] { _add_chains(); });
    row->addWidget(chain_btn);
    row->addStretch(1);
    count = label(QString(), u"faint"_s, false);
    row->addWidget(count);
    mid->lay->addLayout(row);
    lst = new QTreeWidget();
    lst->setColumnCount(3);
    lst->setHeaderLabels({u"Name"_s, QString(), u"Where in the other mod"_s});
    lst->setUniformRowHeights(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"importItems"_s, {std::nullopt, 170, 250}, true);
    connect(lst, &QTreeWidget::itemChanged, this, &ImportWorkspace::_item_changed);
    connect(lst, &QTreeWidget::currentItemChanged, this, &ImportWorkspace::_show_preview);
    connect(lst, &QTreeWidget::itemExpanded, this, &ImportWorkspace::_expanded);
    mid->lay->addWidget(lst, 3);
    preview_head = label(QString(), u"sub"_s);
    mid->lay->addWidget(preview_head);
    preview = new QPlainTextEdit();
    preview->setReadOnly(true);
    preview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setStyleSheet(u"QPlainTextEdit { background: #101318; font-family: %1; font-size: 10pt; }"_s.arg(theme::MONO));
    preview->setTabStopDistance(preview->fontMetrics().horizontalAdvance(u' ') * 4);
    preview->setPlaceholderText(u"Click a line above: its script, as the other mod has it, shows here — and below it "
                                u"yours, when your mod has the thing differently."_s);
    _norefs = std::make_unique<NoRefs>();
    _hl = new scriptedit::ScriptHighlighter(preview->document(), _norefs.get(), QString());
    mid->lay->addWidget(preview, 2);
    split->addWidget(mid);
    // --- what comes over
    s = new Section();
    s->lay->addWidget(_bold(u"What comes over"_s));
    s->lay->addWidget(label(u"Everything ticked, in all the kinds — with what it needs. Untick a needed thing to leave it "
                            u"out (the list says what then misses)."_s,
                            u"faint"_s));
    auto *g = new QVBoxLayout();
    g->setSpacing(4);
    row = new QHBoxLayout();
    row->addWidget(label(u"A nation comes"_s, u"sub"_s, false));
    nation_mode = new QComboBox();
    nation_mode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    nation_mode->addItem(u"with its files only — I place it on the map myself"_s, u"files"_s);
    nation_mode->addItem(u"with its land, as the other mod has it"_s, u"land"_s);
    nation_mode->setToolTip(
        u"Files only: the tag, its colours, parties, history, flags and names — it exists in the game (events can use "
        u"it, it can be released), but owns nothing until you give it provinces.\n"
        u"With its land: it also takes the provinces it owns in the other mod at this start date, with its cores — only "
        u"when both mods have the same map."_s);
    connect(nation_mode, &QComboBox::activated, this, [this](int) { _replan(); });
    row->addWidget(nation_mode, 1);
    g->addLayout(row);
    row = new QHBoxLayout();
    row->addWidget(label(u"An event id you already use"_s, u"sub"_s, false));
    ev_mode = new QComboBox();
    ev_mode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    ev_mode->addItem(u"the imported event gets a new id"_s, u"renumber"_s);
    ev_mode->addItem(u"your event is replaced"_s, u"replace"_s);
    ev_mode->addItem(u"the imported event is skipped"_s, u"skip"_s);
    connect(ev_mode, &QComboBox::activated, this, [this](int) { _replan(); });
    row->addWidget(ev_mode, 1);
    g->addLayout(row);
    icons = new QCheckBox(u"Bring the icons along (national values, focuses, modifiers … get their picture)"_s);
    icons->setChecked(true);
    connect(icons, &QCheckBox::toggled, this, [this](bool) { _replan(); });
    g->addWidget(icons);
    s->lay->addLayout(g);
    plan_tree = new QTreeWidget();
    plan_tree->setHeaderHidden(true);
    plan_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    plan_tree->setTextElideMode(Qt::ElideRight);
    plan_tree->setWordWrap(true);
    connect(plan_tree, &QTreeWidget::itemChanged, this, &ImportWorkspace::_plan_changed);
    s->lay->addWidget(plan_tree, 1);
    go = new QPushButton(u"Import"_s);
    go->setObjectName(u"apply"_s);
    connect(go, &QPushButton::clicked, this, [this] { _import(); });
    s->lay->addWidget(go);
    empty_btn = new QPushButton(u"Untick everything"_s);
    connect(empty_btn, &QPushButton::clicked, this, [this] { _clear(); });
    s->lay->addWidget(empty_btn);
    result = label(QString(), u"sub"_s);
    s->lay->addWidget(result);
    split->addWidget(s);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({380, 860, 520});
    _fill_timer = new QTimer(this);
    _fill_timer->setSingleShot(true);
    _fill_timer->setInterval(250);
    connect(_fill_timer, &QTimer::timeout, this, [this] { _fill_items(); });
    _plan_timer = new QTimer(this);
    _plan_timer->setSingleShot(true);
    _plan_timer->setInterval(200);
    connect(_plan_timer, &QTimer::timeout, this, [this] { _make_plan(); });
    _count_timer = new QTimer(this);
    _count_timer->setInterval(0);
    connect(_count_timer, &QTimer::timeout, this, [this] { _count_step(); });
}

ImportWorkspace::~ImportWorkspace()
{
    delete _hl;                                  // (before the host it asks)
    _hl = nullptr;
    delete region;                               // (it points at the importers: it goes before them)
    region = nullptr;
    imp.reset();                                 // (an importer reads its source: it goes first)
    _old_imp.clear();
}

QLabel *ImportWorkspace::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

importer::ItemP ImportWorkspace::item_of(QTreeWidgetItem *row) const
{
    if (!row)
        return nullptr;
    const QVariant d = row->data(0, Qt::UserRole);
    if (!d.isValid())
        return nullptr;
    const auto it = _held.find(d.value<quintptr>());
    return it == _held.end() ? nullptr : it->second;
}

void ImportWorkspace::set_mode(const QString &k)
{
    mode = k;
    for (const auto &[kk, b] : mode_btn)
        b->setChecked(kk == k);
    things->setVisible(k == u"things");
    region->setVisible(k == u"map");
    if (k == u"map")
        _to_region();
}

void ImportWorkspace::_to_region()
{
    // The page of the map: told which mod to show (reading its map takes a moment the first time).
    if (!doc || !md)
        return;
    if (src && region->src != src.get()) {
        src_info->setText(u"Reading the map of %1 …"_s.arg(src->name));
        src_info->repaint();
    }
    region->set_source(doc, md, src.get(), imp.get());
    if (src)
        src_info->setText(u"%1 — %2. Its map is shown below: pick the provinces to bring over."_s.arg(src->name, src->root));
}

// ------------------------------------------------------------------ the other mod
void ImportWorkspace::refresh(document::Document *doc_)
{
    const bool new_doc = doc_ != doc;
    doc = doc_;
    if (mode == u"map" && src && !new_doc)
        region->set_source(doc, md, src.get(), imp.get());
    if (!src_box->count()) {
        for (const auto &[name, folder] : importer::find_sources(mod)) {
            src_box->addItem(name, folder);
            src_box->setItemData(src_box->count() - 1, folder, Qt::ToolTipRole);
        }
        src_box->setCurrentIndex(-1);
        src_info->setText(u"Pick the mod to take things from%1."_s.arg(
            src_box->count() ? QString() : u" — none was found next to this mod: paste its folder above"_s));
    }
    if (src && (new_doc || !imp)) {
        if (imp)
            _old_imp.push_back(std::move(imp));
        imp = std::make_unique<importer::Importer>(mod, *doc, *src);
        _restart();
    } else if (imp) {
        const auto mark = mark_of(*doc);
        if (mark != _undo_mark) {                // edited elsewhere since: read the mod's lists again
            _undo_mark = mark;
            imp->forget();
            _restart();
        }
    }
}

void ImportWorkspace::_browse()
{
    const QString start = pypath::dirname(mod.submod && !mod.submod->root.isEmpty() ? mod.submod->root : mod.root);
    const QString d = QFileDialog::getExistingDirectory(this, u"The folder of the mod to import from"_s, start);
    if (!d.isEmpty()) {
        path->setText(d);
        _open(d);
    }
}

void ImportWorkspace::_open(const QString &folder)
{
    if (folder.isEmpty() || !doc)
        return;
    bool looks = fs::isdir(folder);
    if (looks) {
        looks = false;
        for (const QString &x : {u"common"_s, u"events"_s, u"decisions"_s, u"history"_s, u"gfx"_s, u"localisation"_s})
            if (fs::isdir(pypath::join(folder, x)))
                looks = true;
    }
    if (!looks) {
        src_info->setText(u"That folder doesn't look like a mod (no common, events, history … inside): %1"_s.arg(folder));
        src_info->setStyleSheet(u"color: #ff786e;"_s);
        return;
    }
    // the mod itself: only its map is of use — to move its own provinces (Provinces from its map)
    const bool own = pypath::normcase(pypath::abspath(folder)) == pypath::normcase(pypath::abspath(mod.root));
    src_info->setStyleSheet(QString());
    src_info->setText(u"Reading %1 …"_s.arg(folder));
    src_info->repaint();
    _retire();
    src = std::make_unique<importer::Source>(folder, mod.base);
    imp = std::make_unique<importer::Importer>(mod, *doc, *src);
    picked = {};
    left_out.clear();
    also.clear();
    const QString here = pypath::normcase(pypath::abspath(folder));
    int i = -1;
    for (int j = 0; j < src_box->count(); ++j)
        if (pypath::normcase(pypath::abspath(src_box->itemData(j).toString())) == here) {
            i = j;
            break;
        }
    if (i < 0) {
        src_box->addItem(src->name, folder);
        i = src_box->count() - 1;
        src_box->setItemData(i, folder, Qt::ToolTipRole);
    }
    src_box->setCurrentIndex(i);
    const bool same = imp->map_same();
    src_info->setText(u"%1 — %2%3. %4"_s.arg(
        src->name, folder,
        !src->replace.isEmpty() ? u" · replaces the game's: "_s + src->replace.join(u", "_s) : QString(),
        same ? u"Same map as this mod: nations can come with their land, province numbers mean the same places."_s
             : u"Its map is NOT the same as this mod's: nations come with their files only, and province numbers in "
               u"what you take mean other places."_s));
    const int li = nation_mode->findData(u"land"_s);
    if (auto *m = qobject_cast<QStandardItemModel *>(nation_mode->model()))
        m->item(li)->setEnabled(same);
    if (!same)
        nation_mode->setCurrentIndex(0);
    result->setText(QString());
    _restart();
    if (own && mode != u"map")
        set_mode(u"map"_s);
    else if (mode == u"map")
        _to_region();
    if (own)
        src_info->setText(u"This mod itself, as it is on disk (Apply first). Pick its own provinces on the map below, "
                          u"choose “Onto this mod's provinces (shapes only)”, then drag them to their new place on “This "
                          u"mod's map”."_s);
}

void ImportWorkspace::_retire()
{
    if (imp)
        _old_imp.push_back(std::move(imp));
    if (src)
        _old_src.push_back(std::move(src));
}

void ImportWorkspace::_restart()
{
    // The kinds with their counts (counted one after the other, the window stays awake), the current list.
    _undo_mark = mark_of(*doc);
    _counts.clear();
    cats->blockSignals(true);
    cats->clear();
    for (const importer::Cat &c : importer::CATS) {
        auto *it = new QTreeWidgetItem(QStringList{c.title, u"…"_s, QString()});
        it->setData(0, Qt::UserRole, c.id);
        cats->addTopLevelItem(it);
        if (c.id == cat)
            cats->setCurrentItem(it);
    }
    cats->blockSignals(false);
    _todo.clear();
    for (const importer::Cat &c : importer::CATS)
        if (c.id != u"files")
            _todo << c.id;
    _count_timer->start();
    _fill_items();
    _replan();
}

void ImportWorkspace::_count_step()
{
    if (_todo.isEmpty() || !imp) {
        _count_timer->stop();
        return;
    }
    const QString cid = _todo.takeFirst();
    std::vector<importer::ItemP> items;
    try {
        items = imp->items(cid);
    } catch (const std::exception &) {
    }
    _set_count(cid, items);
}

void ImportWorkspace::_set_count(const QString &cid, const std::vector<importer::ItemP> &items)
{
    long long n = 0, d = 0;
    for (const auto &it : items) {
        n += it->status == u"new";
        d += it->status == u"differs";
    }
    _counts[cid] = {n, d};
    for (int i = 0; i < cats->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = cats->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == cid) {
            it->setText(1, n ? QString::number(n) : u"–"_s);
            it->setText(2, d ? QString::number(d) : u"–"_s);
            it->setForeground(1, QBrush(QColor(n ? STATUS.at(u"new"_s).colour : theme::INK_FAINT)));
            it->setForeground(2, QBrush(QColor(d ? STATUS.at(u"differs"_s).colour : theme::INK_FAINT)));
            const bool dim = !n && !d;
            it->setForeground(0, QBrush(QColor(dim ? theme::INK_FAINT : theme::INK)));
        }
    }
}

void ImportWorkspace::_cat_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QString cid = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!cid.isEmpty() && cid != cat) {
        cat = cid;
        search->blockSignals(true);
        search->clear();
        search->blockSignals(false);
        _fill_items();
    }
}

// ------------------------------------------------------------------ the things of the kind
void ImportWorkspace::_fill_items()
{
    const importer::Cat &c = importer::cat_of(cat);
    cat_title->setText(c.title);
    cat_about->setText(c.about);
    cat_about->setVisible(!c.about.isEmpty());
    chain_btn->setVisible(cat == u"events");
    lst->clear();
    _held.clear();
    preview->setPlainText(QString());
    preview_head->setText(QString());
    if (!imp) {
        count->setText(QString());
        return;
    }
    const std::vector<importer::ItemP> items = imp->items(cat);   // (a copy: the importer may read again)
    if (!_counts.count(cat))
        _set_count(cat, items);
    const QString q = py::lower(py::strip(search->text()));
    const QString show = show_box->currentData().toString();
    _filling = true;
    lst->setUpdatesEnabled(false);
    QFont bold(lst->font());
    bold.setBold(true);
    OrderedMap<QString, QTreeWidgetItem *> groups;
    long long n = 0;
    _kids.clear();
    const bool lazy = cat == u"files";
    for (const importer::ItemP &it : items) {
        if (show == u"new" && it->status != u"new")
            continue;
        if (show == u"news" && it->status == u"same")
            continue;
        if (!q.isEmpty() && !py::lower(u"%1 %2 %3 %4"_s.arg(it->name, it->key, it->group.value_or(QString()), it->rel))
                                 .contains(q))
            continue;
        n += 1;
        const QString gk = it->group.value_or(QString());
        QTreeWidgetItem *parent = nullptr;
        if (!gk.isEmpty()) {
            if (QTreeWidgetItem *const *have = groups.get(gk)) {
                parent = *have;
            } else {
                parent = new QTreeWidgetItem(
                    QStringList{lazy ? gk : (!gk.contains(u'.') ? imp->nice(gk) : gk), QString(), QString()});
                groups.insert(gk, parent);
                parent->setFont(0, bold);
                parent->setForeground(0, QBrush(QColor(theme::GOLD)));
                parent->setFlags(parent->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                parent->setData(0, Qt::UserRole + 1, gk);
                parent->setCheckState(0, Qt::Unchecked);
                lst->addTopLevelItem(parent);
            }
            _kids[gk].push_back(it);
        }
        if (lazy && parent)
            continue;                            // (files: made when their folder is opened)
        QTreeWidgetItem *r = _row(it);
        if (parent)
            parent->addChild(r);
        else
            lst->addTopLevelItem(r);
    }
    for (const auto &[gk, parent] : groups) {
        const auto &kids = *_kids.get(gk);
        long long on = 0;
        for (const auto &k : kids)
            on += picked.contains(k->id());
        parent->setText(1, QString::number(kids.size()));
        parent->setForeground(1, QBrush(QColor(theme::INK_FAINT)));
        if (lazy) {
            parent->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            parent->setCheckState(0, on == static_cast<long long>(kids.size()) ? Qt::Checked
                                     : on                                       ? Qt::PartiallyChecked
                                                                                : Qt::Unchecked);
        } else {                                 // (its tick follows its lines by itself)
            parent->setExpanded(!q.isEmpty() || groups.size() <= 12);
        }
    }
    lst->setUpdatesEnabled(true);
    _filling = false;
    count->setText(u"%1 shown of %2"_s.arg(n).arg(items.size()));
}

QTreeWidgetItem *ImportWorkspace::_row(const importer::ItemP &it)
{
    auto [st, col] = STATUS.at(it->status);
    if (it->cat == u"nations" && it->status == u"same")
        st = u"in your mod already"_s;
    auto *row = new QTreeWidgetItem(QStringList{it->name, st, it->cat != u"files" ? it->rel : QString()});
    const quintptr k = reinterpret_cast<quintptr>(it.get());
    _held[k] = it;
    row->setData(0, Qt::UserRole, QVariant::fromValue(k));
    row->setForeground(1, QBrush(QColor(col)));
    row->setForeground(2, QBrush(QColor(theme::INK_FAINT)));
    row->setToolTip(0, u"%1 — %2"_s.arg(it->key, it->rel));
    const bool can = !(it->cat == u"nations" && it->status == u"same");
    if (can) {
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(0, picked.contains(it->id()) ? Qt::Checked : Qt::Unchecked);
    } else {
        row->setForeground(0, QBrush(QColor(theme::INK_FAINT)));
    }
    return row;
}

void ImportWorkspace::_expanded(QTreeWidgetItem *parent)
{
    // A folder of files opened: its files are listed now.
    if (cat != u"files" || parent->childCount())
        return;
    const auto *kids = _kids.get(parent->data(0, Qt::UserRole + 1).toString());
    _filling = true;
    if (kids)
        for (size_t i = 0; i < kids->size() && i < size_t(FILE_LIMIT); ++i)
            parent->addChild(_row((*kids)[i]));
    _filling = false;
}

void ImportWorkspace::_item_changed(QTreeWidgetItem *row, int col)
{
    if (_filling || col != 0)
        return;
    const importer::ItemP it = item_of(row);
    if (!it) {                                   // a heading: all of its things
        const auto *kids = _kids.get(row->data(0, Qt::UserRole + 1).toString());
        if (cat == u"files" && row->checkState(0) != Qt::PartiallyChecked) {
            const bool on = row->checkState(0) == Qt::Checked;
            if (kids)
                for (const auto &k : *kids) {
                    if (on)
                        picked.add(k->id());
                    else
                        picked.discard(k->id());
                }
            _filling = true;
            for (int i = 0; i < row->childCount(); ++i)
                row->child(i)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            _filling = false;
            _replan();
        }
        return;
    }
    if (row->checkState(0) == Qt::Checked) {
        picked.add(it->id());
        left_out.erase(it->id());
    } else {
        picked.discard(it->id());
    }
    _replan();
}

std::vector<QTreeWidgetItem *> ImportWorkspace::_rows()
{
    std::vector<QTreeWidgetItem *> out;
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = lst->topLevelItem(i);
        if (item_of(top))
            out.push_back(top);
        for (int j = 0; j < top->childCount(); ++j)
            out.push_back(top->child(j));
    }
    return out;
}

void ImportWorkspace::_tick_all(bool on)
{
    _filling = true;
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = lst->topLevelItem(i);
        const bool heading = !item_of(top);
        if (heading)
            if (const auto *kids = _kids.get(top->data(0, Qt::UserRole + 1).toString()))
                for (const auto &k : *kids)
                    if (!(k->cat == u"nations" && k->status == u"same")) {
                        if (on)
                            picked.add(k->id());
                        else
                            picked.discard(k->id());
                    }
        if (heading && (top->flags() & Qt::ItemIsUserCheckable) && cat == u"files")
            top->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    }
    for (QTreeWidgetItem *row : _rows()) {
        const importer::ItemP it = item_of(row);
        if (it && (row->flags() & Qt::ItemIsUserCheckable)) {
            row->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            if (on)
                picked.add(it->id());
            else
                picked.discard(it->id());
        }
    }
    _filling = false;
    _replan();
}

void ImportWorkspace::_add_chains()
{
    if (!imp)
        return;
    grid::PySet<importer::Id> more;
    for (const auto &[cid, key] : picked.items()) {   // (list(self.picked))
        if (cid == u"events") {
            grid::PySet<importer::Id> part;      // {("events", e) for e in self.imp.chain(key)}
            for (const QString &e : imp->chain(key).items())
                part.add({u"events"_s, e});
            more.merge(part);
        }
    }
    const size_t added = more.difference(picked).size();
    picked.merge(more);
    _fill_items();
    _replan();
    result->setText(added ? u"%1 more event%2 ticked: the chains of the ticked ones."_s.arg(added).arg(
                                added == 1 ? QString() : u"s"_s)
                          : u"Tick an event first — its chain is every event that calls it or is called by it."_s);
}

void ImportWorkspace::_show_preview(QTreeWidgetItem *row, QTreeWidgetItem *)
{
    const importer::ItemP it = item_of(row);
    if (!it)
        return;
    if (it->cat == u"files") {
        const QFileInfo fi(it->text);
        const long long size = fi.isFile() ? fi.size() : 0;
        const QString about = it->status == u"new"       ? u" · your mod doesn't have this file"_s
                              : it->status == u"differs" ? u" · your mod has another file of this name: it would be replaced"_s
                                                         : u" · your mod has a file of this name and size"_s;
        preview_head->setText(u"%1 — %2 bytes%3"_s.arg(it->key, py::fmt_commas(double(size), 0), about));
        QString text;
        const QString low = py::lower(it->key);
        bool ext = false;
        for (const QString &e : {u".txt"_s, u".lua"_s, u".gui"_s, u".gfx"_s, u".csv"_s, u".map"_s})
            ext = ext || low.endsWith(e);
        if (ext && size < 400000) {
            try {
                text = read_universal(it->text);
            } catch (const fs::OSError &) {
            }
        }
        preview->setPlainText(text);
        return;
    }
    QString head = u"%1 — as the other mod has it"_s.arg(it->name);
    QString text = it->text;
    if (it->cat == u"nations" && it->hist && !it->hist->isEmpty()) {
        try {
            text += u"\n\n# ---- its history (%1)\n"_s.arg(pypath::basename(*it->hist)) + read_universal(*it->hist);
        } catch (const fs::OSError &) {
        }
    }
    if (it->cat == u"settings")
        text = u"%1 = %2%3"_s.arg(it->xkey, it->text, !it->note.isEmpty() ? u"    -- "_s + it->note : QString());
    if (it->status == u"differs" && it->cat != u"settings") {
        const auto &ti = imp->tindex(it->cat);
        if (const importer::TEntry *mine = ti.get(it->key)) {
            text += u"\n\n# ================= yours (%1) =================\n%2"_s.arg(mine->rel.value_or(u"None"_s),
                                                                                    mine->text);
            head += u" — and below it: yours"_s;
        }
    }
    preview_head->setText(head);
    text.replace(u"\r\n"_s, u"\n"_s);
    preview->setPlainText(text);
}

// ------------------------------------------------------------------ the plan
void ImportWorkspace::_replan() { _plan_timer->start(); }

importer::Options ImportWorkspace::_options() const
{
    importer::Options o;
    o.nation_mode = nation_mode->currentData().toString();
    o.event_conflict = ev_mode->currentData().toString();
    o.icons = icons->isChecked();
    return o;
}

void ImportWorkspace::_make_plan()
{
    plan_tree->blockSignals(true);
    plan_tree->clear();
    if (!imp || picked.empty()) {
        plan.reset();
        go->setEnabled(false);
        go->setText(u"Import"_s);
        empty_btn->setEnabled(false);
        if (imp)
            plan_tree->addTopLevelItem(new QTreeWidgetItem(QStringList{u"Nothing ticked yet."_s}));
        plan_tree->blockSignals(false);
        return;
    }
    plan = imp->plan(picked.items(), left_out, _options(), also);
    const importer::Plan &pl = *plan;
    QFont bold(plan_tree->font());
    bold.setBold(true);
    auto head = [&](const QString &text, const QString &color, bool open_) {
        auto *h = new QTreeWidgetItem(QStringList{text});
        h->setFont(0, bold);
        h->setForeground(0, QBrush(QColor(color)));
        plan_tree->addTopLevelItem(h);
        h->setExpanded(open_);
        return h;
    };
    std::vector<importer::ItemP> mine;
    for (const auto &it : pl.items)
        if (picked.contains(it->id()))
            mine.push_back(it);
    QTreeWidgetItem *h = head(u"Ticked by you (%1)"_s.arg(mine.size()), theme::GOLD, mine.size() <= 40);
    OrderedMap<QString, std::vector<importer::ItemP>> by;
    for (const auto &it : mine)
        by[it->cat].push_back(it);
    for (const auto &[cid, l] : by) {
        auto *c = new QTreeWidgetItem(QStringList{u"%1: %2"_s.arg(importer::cat_of(cid).title).arg(l.size())});
        h->addChild(c);
        for (size_t i = 0; i < l.size() && i < 300; ++i)
            c->addChild(new QTreeWidgetItem(QStringList{l[i]->name}));
    }
    const std::tuple<bool, QString, QString> kinds[2] = {
        {true, u"Comes along (%1) — what you ticked can't do without it"_s,
         u"Untick to leave it out (the list below then says what misses)."_s},
        {false, u"Only named (%1) — rules your mod doesn't have; tick to bring one along"_s,
         u"Technologies, reforms, goods … of the other mod. They stay out unless ticked: a line asking for one is then "
         u"simply never true."_s}};
    for (const auto &[hard, title, tip] : kinds) {
        std::vector<importer::Needed> rows;
        for (const auto &x : pl.needed)
            if (x.hard == hard)
                rows.push_back(x);
        if (rows.empty())
            continue;
        h = head(title.arg(rows.size()), hard ? theme::GOLD : theme::INK_DIM, hard || rows.size() <= 12);
        h->setToolTip(0, tip);
        std::stable_sort(rows.begin(), rows.end(), [](const importer::Needed &a, const importer::Needed &b) {
            return std::tie(a.dep->cat, a.dep->name) < std::tie(b.dep->cat, b.dep->name);
        });
        for (const auto &x : rows) {
            auto *r = new QTreeWidgetItem(
                QStringList{u"%1: %2   — %3"_s.arg(importer::cat_of(x.dep->cat).title, x.dep->name, x.why)});
            r->setFlags(r->flags() | Qt::ItemIsUserCheckable);
            r->setCheckState(0, x.comes ? Qt::Checked : Qt::Unchecked);
            r->setData(0, Qt::UserRole, QStringList{x.dep->cat, x.dep->key});
            r->setData(0, Qt::UserRole + 1, hard);
            r->setToolTip(0, u"%1 — %2. %3"_s.arg(x.dep->name, x.why, tip));
            h->addChild(r);
        }
    }
    if (!pl.loc.isEmpty()) {
        h = head(u"Texts (%1 names and descriptions)"_s.arg(pl.loc.size()), theme::INK_DIM, false);
        QStringList keys;
        for (const auto &kv : pl.loc)
            keys << kv.first;
        std::sort(keys.begin(), keys.end());
        for (qsizetype i = 0; i < keys.size() && i < 400; ++i)
            h->addChild(new QTreeWidgetItem(QStringList{u"%1: %2"_s.arg(keys[i], pl.loc.get(keys[i])->left(90))}));
    }
    if (!pl.files.isEmpty() || !pl.flags.isEmpty()) {
        h = head(u"Pictures and flags (%1)"_s.arg(pl.files.size() + pl.flags.size()), theme::INK_DIM, false);
        QStringList fk, gk;
        for (const auto &kv : pl.files)
            fk << kv.first;
        for (const auto &kv : pl.flags)
            gk << kv.first;
        std::sort(fk.begin(), fk.end());
        std::sort(gk.begin(), gk.end());
        for (const QString &k : fk)
            h->addChild(new QTreeWidgetItem(QStringList{k}));
        for (const QString &k : gk)
            h->addChild(new QTreeWidgetItem(QStringList{u"gfx/flags/"_s + k}));
    }
    if (pl.icons)
        head(u"Icons: %1 picture%2 for the icon strips"_s.arg(pl.icons).arg(pl.icons == 1 ? QString() : u"s"_s),
             theme::INK_DIM, true);
    if (!pl.notes.isEmpty()) {
        h = head(u"Good to know (%1)"_s.arg(pl.notes.size()), u"#9ad0ff"_s, true);
        for (const QString &t : pl.notes)
            h->addChild(new QTreeWidgetItem(QStringList{t}));
    }
    if (!pl.warnings.isEmpty()) {
        h = head(u"Watch out (%1)"_s.arg(pl.warnings.size()), u"#ff786e"_s, true);
        for (const QString &t : pl.warnings) {
            auto *r = new QTreeWidgetItem(QStringList{t});
            r->setForeground(0, QBrush(QColor(u"#ff9a92"_s)));
            r->setToolTip(0, t);
            h->addChild(r);
        }
    }
    plan_tree->blockSignals(false);
    const size_t n = pl.items.size();
    go->setEnabled(n > 0);
    go->setText(u"Import %1 thing%2 from %3"_s.arg(n).arg(n == 1 ? QString() : u"s"_s, src->name));
    empty_btn->setEnabled(true);
}

void ImportWorkspace::_plan_changed(QTreeWidgetItem *row, int)
{
    const QVariant d = row->data(0, Qt::UserRole);
    if (!d.isValid())
        return;
    const QStringList dl = d.toStringList();
    const importer::Id dep{dl.value(0), dl.value(1)};
    const bool on = row->checkState(0) == Qt::Checked;
    if (row->data(0, Qt::UserRole + 1).toBool()) {
        if (on)
            left_out.erase(dep);
        else
            left_out.insert(dep);
    } else {
        if (on)
            also.insert(dep);
        else
            also.erase(dep);
    }
    _replan();
}

void ImportWorkspace::_clear()
{
    picked = {};
    left_out.clear();
    also.clear();
    _fill_items();
    _replan();
}

void ImportWorkspace::_import()
{
    if (!imp || picked.empty())
        return;
    const importer::Plan pl = imp->plan(picked.items(), left_out, _options(), also);
    QString msg;
    try {
        msg = imp->apply(pl);
    } catch (const std::exception &e) {          // nothing half done: the step is only made at the end
        result->setText(u"⚠ The import failed (%1) — nothing was changed."_s.arg(QString::fromStdString(e.what())));
        result->setStyleSheet(u"color: #ff786e;"_s);
        return;
    }
    result->setStyleSheet(QString());
    result->setText(msg);
    Q_EMIT message(msg);
    picked = {};
    left_out.clear();
    also.clear();
    Q_EMIT imported();
    _restart();
}

}  // namespace importpanel
