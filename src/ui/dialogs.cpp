// dialogs.py — Dialogs: add pop, paste pops, change culture/religion/type, new culture / religion, culture and
// religion managers.
#include "ui/dialogs.h"
#include "core/culturefile.h"
#include "core/document.h"
#include "core/localisation.h"
#include "map/ddsimage.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::fmt;
using widgets::swatch_icon;

namespace dialogs {

const QString HEADER = u"__header__"_s;
const QString NEW = u"__new__"_s;

namespace {

int rnd() { return int(QRandomGenerator::global()->bounded(40, 221)); }   // random.randint(40, 220)
Rgb random_color()
{
    const int r = rnd(), g = rnd(), b = rnd();
    return {r, g, b};
}

bool key_ok(const QString &k) { return culturefile::KEY_RE.match(k).hasMatch(); }   // KEY_RE.match(k)

// " ".join(w.capitalize() for w in t.split("_") if w)
QString capitalized_words(const QString &t)
{
    QStringList out;
    for (const QString &w : t.split(u'_'))
        if (!w.isEmpty())
            out << w.left(1).toUpper() + w.mid(1).toLower();
    return out.join(u' ');
}

QString capitalize(const QString &s) { return s.left(1).toUpper() + s.mid(1).toLower(); }

QString rgb_text(const Rgb &c)
{
    return u"  rgb %1 %2 %3"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

QColor qcolor(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

QVariant var(const Data &d) { return d ? QVariant(*d) : QVariant(); }
Data unvar(const QVariant &v) { return v.isValid() && !v.isNull() ? Data(v.toString()) : std::nullopt; }

void repolish(QPushButton *btn)
{
    btn->style()->unpolish(btn);
    btn->style()->polish(btn);
}

QString loc_or(const moddata::Mod &mod, const QString &k)   // mod.loc.get(k) or k
{
    const QString v = mod.loc.value(k);
    return v.isEmpty() ? k : v;
}

}  // namespace

QLabel *_lab(const QString &text, const QString &obj)
{
    auto *l = new QLabel(text);
    l->setObjectName(obj);
    l->setWordWrap(true);
    return l;
}

void report(QWidget *parent, const QString &text)
{
    if (!parent)
        return;
    const QMetaObject *mo = parent->metaObject();
    if (mo->indexOfMethod("show_error(QString)") >= 0)
        QMetaObject::invokeMethod(parent, "show_error", Q_ARG(QString, text));
    else if (mo->indexOfMethod("message(QString)") >= 0)
        QMetaObject::invokeMethod(parent, "message", Q_ARG(QString, text));
}

// ---------------------------------------------------------------------------------------------- new culture
NewCultureDialog::NewCultureDialog(moddata::Mod &, const QStringList &groups, const QSet<QString> &taken_,
                                   const OptStr &default_group, QWidget *parent)
    : QDialog(parent), taken(taken_), color(random_color())
{
    setWindowTitle(u"New culture"_s);
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    key_edit = new QLineEdit();
    key_edit->setPlaceholderText(u"lowercase_key, e.g. north_verspodian"_s);
    name_edit = new QLineEdit();
    name_edit->setPlaceholderText(u"Shown in game, e.g. North Verspodian"_s);
    connect(key_edit, &QLineEdit::textEdited, this, &NewCultureDialog::_suggest_name);
    color_btn = new QPushButton();
    connect(color_btn, &QPushButton::clicked, this, [this] { _pick_color(); });
    _show_color();
    group_box = new QComboBox();
    group_box->addItem(u"＋ New group…"_s, u"__new__"_s);
    for (const QString &g : groups)
        group_box->addItem(g, g);
    const int i = (default_group && !default_group->isEmpty()) ? group_box->findData(*default_group) : 1;
    group_box->setCurrentIndex(group_box->count() > 1 ? std::max(1, i) : 0);
    new_group = new QLineEdit();
    new_group->setPlaceholderText(u"new group key, e.g. north_group"_s);
    connect(group_box, &QComboBox::currentIndexChanged, this,
            [this](int) { new_group->setVisible(group_box->currentData().toString() == u"__new__"_s); });
    new_group->setVisible(group_box->currentData().toString() == u"__new__"_s);
    form->addRow(u"Culture key"_s, key_edit);
    form->addRow(u"Display name"_s, name_edit);
    form->addRow(u"Map colour"_s, color_btn);
    form->addRow(u"Culture group"_s, group_box);
    form->addRow(QString(), new_group);
    lay->addLayout(form);
    lay->addWidget(_lab(u"The new culture copies the name lists of another culture in its group. "
                        u"It is written to common/cultures.txt (a backup is made first)."_s,
                        u"faint"_s));
    err = _lab(QString(), u"warn"_s);
    err->hide();
    lay->addWidget(err);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &NewCultureDialog::_ok);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void NewCultureDialog::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

void NewCultureDialog::_suggest_name(const QString &t)
{
    if (!name_edit->isModified())
        name_edit->setText(capitalized_words(t));
}

void NewCultureDialog::_show_color()
{
    color_btn->setText(rgb_text(color));
    color_btn->setIcon(swatch_icon(color, 14));
}

void NewCultureDialog::_pick_color()
{
    const QColor c = QColorDialog::getColor(qcolor(color), this, u"Culture colour"_s);
    if (c.isValid()) {
        color = {c.red(), c.green(), c.blue()};
        _show_color();
    }
}

void NewCultureDialog::_ok()
{
    const QString key = key_edit->text().trimmed();
    if (!key_ok(key))
        return show_error(u"⚠ Culture key: use lowercase letters, digits and _ (starting with a letter)."_s);
    if (taken.contains(key))
        return show_error(u"⚠ '%1' is already used as a culture or group key."_s.arg(key));
    if (group_box->currentData().toString() == u"__new__"_s) {
        const QString g = new_group->text().trimmed();
        if (!key_ok(g) || taken.contains(g) || g == key)
            return show_error(u"⚠ Give the new group a unique lowercase key."_s);
    }
    accept();
}

Result NewCultureDialog::result_data() const
{
    QString g = group_box->currentData().toString();
    OptStr ng;
    if (g == u"__new__"_s) {
        g = new_group->text().trimmed();
        ng = g;
    }
    return {key_edit->text().trimmed(), name_edit->text().trimmed(), color, std::nullopt, g, ng};
}

OptStr create_culture(QWidget *parent, moddata::Mod &mod, const QString &backup_root, const OptStr &default_group)
{
    QSet<QString> taken;
    for (const auto &[k, _c] : mod.cultures)
        taken.insert(k);
    for (const QString &g : mod.culture_groups)
        taken.insert(g);
    NewCultureDialog dlg(mod, mod.culture_groups, taken, default_group, parent);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    const Result d = dlg.result_data();
    try {
        std::vector<std::pair<QString, OptStr>> ng;
        if (d.new_group && !d.new_group->isEmpty())
            ng.emplace_back(*d.new_group, default_group);
        culturefile::apply_changes(mod, ng, {{d.key, d.group, d.color, d.name, std::nullopt}}, {}, backup_root);
    } catch (const std::exception &e) {
        report(parent, u"⚠ New culture not saved: %1"_s.arg(QString::fromUtf8(e.what())));
        return std::nullopt;
    }
    OrderedMap<QString, QString> names;
    if (!d.name.isEmpty())
        names.insert(d.key, d.name);
    reload_cultures(mod, names);
    return d.key;
}

void reload_cultures(moddata::Mod &mod, const OrderedMap<QString, QString> &names)
{
    if (mod.pending_script) {                         // (pending: the lists follow the editor's texts)
        mod.pending_script->sync();
        return;
    }
    for (const auto &[k, v] : names)
        if (!mod.loc.contains(k))
            mod.loc.insert(k, v);
    mod._load_cultures();
}

// ---------------------------------------------------------------------------------------------- the managers
GroupTree::GroupTree(const QString &item_title, QWidget *parent) : QTreeWidget(parent)
{
    setHeaderLabels({item_title, u"Key"_s, u"Pops"_s});
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setAlternatingRowColors(true);
    setUniformRowHeights(true);
    header()->setStretchLastSection(false);
    setColumnWidth(0, 250);
    setColumnWidth(1, 170);
}

void GroupTree::dropEvent(QDropEvent *ev)
{
    QTreeWidgetItem *target = itemAt(ev->position().toPoint());
    ev->setDropAction(Qt::IgnoreAction);
    ev->ignore();
    if (!target)
        return;
    if (target->data(0, Qt::UserRole + 1).toString() == u"item"_s)
        target = target->parent();
    const QString gid = target ? target->data(0, Qt::UserRole).toString() : QString();
    QStringList keys;
    for (QTreeWidgetItem *it : selectedItems())
        if (it->data(0, Qt::UserRole + 1).toString() == u"item"_s)
            keys << it->data(0, Qt::UserRole).toString();
    if (!keys.isEmpty() && !gid.isEmpty())
        Q_EMIT moved(keys, gid);
}

GroupManager::GroupManager(moddata::Mod &mod_, document::Document &doc_, const QString &backup_root_,
                           QWidget *parent, const QString &kind, const QString &kinds, const QString &file)
    : QDialog(parent), KIND(kind), KINDS(kinds), FILE(file), mod(mod_), doc(doc_), backup_root(backup_root_)
{
}

// --- what is edited (overridden by the religion manager)
OrderedMap<QString, ItemInfo> GroupManager::_items() const
{
    OrderedMap<QString, ItemInfo> out;
    for (const auto &[k, c] : mod.cultures)
        out.insert(k, {c.group, c.name, c.color, std::nullopt});
    return out;
}

QStringList GroupManager::_group_order() const { return mod.culture_groups; }

QDialog *GroupManager::_new_dialog()
{
    return new NewCultureDialog(mod, groups, _taken(), _selected_group(), this);
}

Result GroupManager::_result_of(QDialog *dlg) const { return static_cast<NewCultureDialog *>(dlg)->result_data(); }

NewItem GroupManager::_new_entry(const Result &d) const { return {d.key, d.group, d.color, d.name, std::nullopt}; }

QString GroupManager::_apply()
{
    std::vector<culturefile::NewCulture> items;
    for (const NewItem &c : new_items)
        items.push_back({c.key, c.gid, c.color, c.name, std::nullopt});
    return culturefile::apply_changes(mod, new_groups, items, {moves.begin(), moves.end()}, backup_root);
}

void GroupManager::_reload()
{
    OrderedMap<QString, QString> names;
    for (const NewItem &c : new_items)
        if (!c.name.isEmpty())
            names.insert(c.key, c.name);
    reload_cultures(mod, names);
}

void GroupManager::_decorate(QTreeWidgetItem *ci, const QString &, const ItemInfo *item, const NewItem *nw) const
{
    ci->setIcon(0, swatch_icon(item ? item->color : nw->color, 12));
}

// ---
void GroupManager::build()
{
    setWindowTitle(u"%1 — %2"_s.arg(capitalize(KINDS), mod.name()));
    resize(720, 680);
    changed_file = false;
    _reset_pending();

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(_lab(u"Drag %1 (Ctrl/Shift for several) onto another group to move them. "
                        u"“Take over” makes them pending changes of the editor — %2 is written on Apply "
                        u"(Ctrl+Z in the main window undoes them)."_s.arg(KINDS, FILE)));
    search = new QLineEdit();
    search->setPlaceholderText(u"Search %1…"_s.arg(KINDS));
    connect(search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    lay->addWidget(search);
    tree = new GroupTree(capitalize(KIND));
    connect(tree, &GroupTree::moved, this, &GroupManager::move);
    lay->addWidget(tree, 1);
    auto *row = new QHBoxLayout();
    const std::vector<std::pair<QString, std::function<void()>>> buttons = {
        {u"New %1…"_s.arg(KIND), [this] { new_item(); }},
        {u"New group…"_s, [this] { new_group(); }},
        {u"Move selected to…"_s, [this] { move_to(); }},
        {u"Expand all"_s, [this] { tree->expandAll(); }},
        {u"Collapse all"_s, [this] { tree->collapseAll(); }}};
    for (const auto &[text, fn] : buttons) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, fn);
        row->addWidget(b);
    }
    row->addStretch(1);
    lay->addLayout(row);
    // rename the selected culture / religion / group (its name in the localisation)
    row = new QHBoxLayout();
    row->addWidget(_lab(u"Name in the game"_s, u"sub"_s));
    name_field = new QLineEdit();
    name_field->setPlaceholderText(u"select a %1 or a group"_s.arg(KIND));
    name_field->setEnabled(false);
    connect(name_field, &QLineEdit::returnPressed, this, &GroupManager::rename);
    row->addWidget(name_field, 1);
    rename_btn = new QPushButton(u"Rename"_s);
    rename_btn->setEnabled(false);
    connect(rename_btn, &QPushButton::clicked, this, [this] { rename(); });
    row->addWidget(rename_btn);
    lay->addLayout(row);
    connect(tree, &QTreeWidget::currentItemChanged, this, [this] { _show_name(); });
    pending_label = _lab(QString(), u"faint"_s);
    lay->addWidget(pending_label);
    err = _lab(QString(), u"warn"_s);                 // problems in red here, no pop-ups
    err->hide();
    lay->addWidget(err);
    _armed.reset();                                   // "save" / "close": the button was clicked once already
    row = new QHBoxLayout();
    row->addStretch(1);
    save_btn = new QPushButton(_save_text());
    save_btn->setObjectName(u"apply"_s);
    connect(save_btn, &QPushButton::clicked, this, [this] { save(); });
    close_btn = new QPushButton(u"Close"_s);
    connect(close_btn, &QPushButton::clicked, this, [this] { close_dialog(); });
    row->addWidget(close_btn);
    row->addWidget(save_btn);
    lay->addLayout(row);
    rebuild();
}

QString GroupManager::_save_text() const { return u"Take over (written on Apply)"_s; }

void GroupManager::_reset_pending()
{
    groups = _group_order();
    members.clear();
    const OrderedMap<QString, ItemInfo> items = _items();
    for (const auto &[k, c] : items)
        members[c.group].append(k);
    new_groups.clear();                               // (gid, template)
    new_items.clear();                                // (key, gid, color, name, template or icon)
    moves.clear();                                    // key -> gid
    renames.clear();                                  // key (item or group) -> new name in the game
    orig_group.clear();
    for (const auto &[k, c] : items)
        orig_group.insert(k, c.group);
}

long long GroupManager::pending() const
{
    return (long long)new_groups.size() + (long long)new_items.size() + moves.size() + renames.size();
}

QString GroupManager::_name_of(const QString &key) const
{
    if (const QString *r = renames.get(key))
        return *r;
    for (const NewItem &c : new_items)
        if (c.key == key) {
            if (!c.name.isEmpty())
                return c.name;
            break;
        }
    return loc_or(mod, key);
}

void GroupManager::_show_name()
{
    QTreeWidgetItem *it = tree->currentItem();
    const QString key = it ? it->data(0, Qt::UserRole).toString() : QString();
    name_field->setEnabled(!key.isEmpty());
    rename_btn->setEnabled(!key.isEmpty());
    name_field->setText(!key.isEmpty() ? _name_of(key) : QString());
}

void GroupManager::rename()
{
    QTreeWidgetItem *it = tree->currentItem();
    const QString key = it ? it->data(0, Qt::UserRole).toString() : QString();
    const QString name = name_field->text().split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts).join(u' ');
    if (key.isEmpty() || name.isEmpty())
        return;
    const bool is_new = std::any_of(new_items.begin(), new_items.end(), [&](const NewItem &c) { return c.key == key; });
    if (is_new) {                                     // a new one: its name goes with it
        for (NewItem &c : new_items)
            if (c.key == key)
                c.name = name;
    } else if (name == loc_or(mod, key)) {
        renames.remove(key);
    } else {
        renames.insert(key, name);
    }
    const QString kind = it->data(0, Qt::UserRole + 1).toString();
    rebuild();
    // keep the renamed entry selected
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        std::vector<QTreeWidgetItem *> xs = {top};
        for (int j = 0; j < top->childCount(); ++j)
            xs.push_back(top->child(j));
        for (QTreeWidgetItem *x : xs)
            if (x->data(0, Qt::UserRole).toString() == key && x->data(0, Qt::UserRole + 1).toString() == kind) {
                tree->setCurrentItem(x);
                return;
            }
    }
}

OrderedMap<OptStr, long long> GroupManager::_pop_sizes() const
{
    OrderedMap<OptStr, long long> sizes;
    const bool rel = KIND == u"religion"_s;
    for (const auto &[pid, pops] : doc.pops)
        for (const moddata::Pop &p : pops)
            sizes[rel ? p.religion : p.culture] += p.size;
    return sizes;
}

void GroupManager::rebuild()
{
    const QString q = search->text().trimmed().toLower();
    QSet<QString> expanded;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->isExpanded())
            expanded.insert(tree->topLevelItem(i)->data(0, Qt::UserRole).toString());
    tree->clear();
    const OrderedMap<OptStr, long long> sizes = _pop_sizes();
    const OrderedMap<QString, ItemInfo> items = _items();
    QFont bold;
    bold.setBold(true);
    const QBrush gold{QColor(theme::GOLD)};
    OrderedMap<QString, NewItem> nw;
    for (const NewItem &c : new_items)
        nw.insert(c.key, c);
    for (const QString &gid : groups) {
        const QStringList mem = members.value(gid);
        QStringList shown;
        for (const QString &k : mem) {
            if (q.isEmpty() || k.toLower().contains(q)) {
                shown << k;
                continue;
            }
            const ItemInfo *c = items.get(k);
            QString nm = c ? c->name : QString();
            if (nm.isEmpty())
                nm = nw.contains(k) ? nw.get(k)->name : QString();
            if (nm.toLower().contains(q))
                shown << k;
        }
        if (!q.isEmpty() && shown.isEmpty() && !gid.toLower().contains(q))
            continue;
        long long total = 0;
        for (const QString &k : mem)
            total += sizes.value(k);
        auto *gi = new QTreeWidgetItem(QStringList{u"📁  "_s + _name_of(gid), gid, fmt(total)});
        gi->setData(0, Qt::UserRole, gid);
        gi->setData(0, Qt::UserRole + 1, u"group"_s);
        gi->setFont(0, bold);
        gi->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        gi->setFlags((gi->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsDragEnabled);
        if (std::any_of(new_groups.begin(), new_groups.end(), [&](const auto &g) { return g.first == gid; })) {
            gi->setForeground(0, gold);
            gi->setText(0, gi->text(0) + u"   (new)"_s);
        }
        if (mem.isEmpty())
            gi->setText(0, gi->text(0) + u"   (empty)"_s);
        if (renames.contains(gid)) {
            gi->setForeground(0, gold);
            gi->setText(0, gi->text(0) + u"   (renamed)"_s);
        }
        tree->addTopLevelItem(gi);
        for (const QString &k : shown) {
            const ItemInfo *c = items.get(k);
            const NewItem *n = nw.get(k);
            QString name = renames.value(k);
            if (name.isEmpty())
                name = c ? c->name : (n && !n->name.isEmpty() ? n->name : k);
            auto *ci = new QTreeWidgetItem(QStringList{name, k, fmt(sizes.value(k))});
            _decorate(ci, k, c, n);
            ci->setData(0, Qt::UserRole, k);
            ci->setData(0, Qt::UserRole + 1, u"item"_s);
            ci->setFlags((ci->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
            ci->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
            if (n) {
                ci->setForeground(0, gold);
                ci->setText(0, name + u"   (new)"_s);
            } else if (moves.contains(k)) {
                ci->setForeground(0, gold);
                const QString *og = orig_group.get(k);
                ci->setText(0, name + u"   (moved from %1)"_s.arg(og ? *og : u"None"_s));
            } else if (renames.contains(k)) {
                ci->setForeground(0, gold);
                ci->setText(0, name + u"   (renamed)"_s);
            }
            gi->addChild(ci);
        }
        gi->setExpanded(!q.isEmpty() || expanded.contains(gid));
    }
    const long long n = pending();
    pending_label->setText(n ? u"%1 unsaved change(s)."_s.arg(n) : u"No unsaved changes."_s);
    save_btn->setEnabled(n > 0);
}

void GroupManager::move(const QStringList &keys, const QString &gid)
{
    for (const QString &k : keys) {
        OptStr cur;
        for (const auto &[g, m] : members)
            if (m.contains(k)) {
                cur = g;
                break;
            }
        if (!cur || *cur == gid)
            continue;
        members[*cur].removeOne(k);
        members[gid].append(k);
        const bool is_new = std::any_of(new_items.begin(), new_items.end(), [&](const NewItem &c) { return c.key == k; });
        if (is_new) {
            for (NewItem &c : new_items)
                if (c.key == k)
                    c.gid = gid;
        } else if (orig_group.contains(k) && *orig_group.get(k) == gid) {
            moves.remove(k);
        } else {
            moves.insert(k, gid);
        }
    }
    rebuild();
}

void GroupManager::move_to()
{
    QStringList keys;
    for (QTreeWidgetItem *it : tree->selectedItems())
        if (it->data(0, Qt::UserRole + 1).toString() == u"item"_s)
            keys << it->data(0, Qt::UserRole).toString();
    if (keys.isEmpty()) {
        show_error(u"⚠ Select one or more %1 first."_s.arg(KINDS));
        return;
    }
    show_error(QString());
    bool ok = false;
    const QString gid = QInputDialog::getItem(this, u"Move %1"_s.arg(KINDS),
                                              u"Move %1 %2(s) to group:"_s.arg(keys.size()).arg(KIND), groups, 0,
                                              false, &ok);
    if (ok)
        move(keys, gid);
}

QSet<QString> GroupManager::_taken() const
{
    QSet<QString> out;
    for (const auto &[k, _c] : _items())
        out.insert(k);
    for (const QString &g : groups)
        out.insert(g);
    for (const NewItem &c : new_items)
        out.insert(c.key);
    return out;
}

OptStr GroupManager::_selected_group() const
{
    QTreeWidgetItem *it = tree->currentItem();
    if (!it)
        return std::nullopt;
    if (it->data(0, Qt::UserRole + 1).toString() == u"item"_s)
        it = it->parent();
    return it ? OptStr(it->data(0, Qt::UserRole).toString()) : std::nullopt;
}

void GroupManager::new_group()
{
    bool ok = false;
    QString g = QInputDialog::getText(this, u"New %1 group"_s.arg(KIND), u"Key for the new group (lowercase):"_s,
                                      QLineEdit::Normal, QString(), &ok);
    g = g.trimmed();
    if (!ok || g.isEmpty())
        return;
    if (!key_ok(g) || _taken().contains(g)) {
        show_error(u"⚠ New group '%1': use a unique lowercase key (letters, digits, _)."_s.arg(g));
        return;
    }
    OptStr tmpl = _selected_group();
    if (!tmpl || tmpl->isEmpty())
        tmpl = groups.isEmpty() ? std::nullopt : OptStr(groups[0]);
    new_groups.emplace_back(g, tmpl);
    groups.append(g);
    rebuild();
    show_error(QString());
    pending_label->setText(pending_label->text() + u"  Group '%1' added: drag %2 into it or create a new "
                                                   u"%3 in it — a group must contain at least one."_s.arg(g, KINDS, KIND));
}

void GroupManager::new_item()
{
    QDialog *dlg = _new_dialog();
    const int r = dlg->exec();
    if (r != QDialog::Accepted) {
        dlg->deleteLater();
        return;
    }
    const Result d = _result_of(dlg);
    dlg->deleteLater();
    if (d.new_group && !d.new_group->isEmpty()) {
        new_groups.emplace_back(*d.new_group, _selected_group());
        groups.append(*d.new_group);
    }
    new_items.push_back(_new_entry(d));
    members[d.group].append(d.key);
    rebuild();
}

void GroupManager::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

void GroupManager::_arm(const QString &what, QPushButton *btn, const QString &text)
{
    // No dialog: the button asks once more, in red.
    _armed = what;
    btn->setText(text);
    btn->setObjectName(u"confirm"_s);
    repolish(btn);
}

void GroupManager::_disarm()
{
    _armed.reset();
    const std::vector<std::tuple<QPushButton *, QString, QString>> btns = {{save_btn, _save_text(), u"apply"_s},
                                                                          {close_btn, u"Close"_s, QString()}};
    for (const auto &[btn, text, name] : btns) {
        btn->setText(text);
        btn->setObjectName(name);
        repolish(btn);
    }
}

void GroupManager::save()
{
    QStringList empty;
    for (const QString &g : groups) {
        const QStringList *m = members.get(g);
        if (!m || m->isEmpty())
            empty << g;
    }
    if (!empty.isEmpty() && _armed != u"save"_s) {
        show_error(u"⚠ These groups would have no %1: %2 — Victoria 2 may not like empty groups. Click Save "
                   u"again to save anyway."_s.arg(KINDS, empty.join(u", "_s)));
        _arm(u"save"_s, save_btn, u"Save anyway"_s);
        return;
    }
    _disarm();
    QString bdir;
    try {
        bdir = (!new_groups.empty() || !new_items.empty() || !moves.isEmpty()) ? _apply() : u"—"_s;
        if (!renames.isEmpty()) {
            const QString sec = KIND == u"religion"_s ? u"religion"_s : u"culture"_s;
            if (mod.pending_script) {
                QStringList first;
                for (const auto &[k, _v] : renames)
                    if (first.size() < 3)
                        first << k;
                mod.pending_script->names(u"Rename %1"_s.arg(first.join(u", "_s)), renames, sec);
            } else {
                OrderedMap<QString, QString> secs;
                for (const auto &[k, _v] : renames)
                    secs.insert(k, sec);
                localisation::write_names(mod, renames, backup_root, secs);
            }
        }
    } catch (const std::exception &e) {
        show_error(u"⚠ Not saved: %1"_s.arg(QString::fromUtf8(e.what())));
        return;
    }
    _reload();
    changed_file = true;
    _reset_pending();
    rebuild();
    show_error(QString());
    pending_label->setText(mod.pending_script
                               ? u"Taken over — %1 is written when you press Apply in the main window."_s.arg(FILE)
                               : u"Saved %1. Backup: %2"_s.arg(FILE, bdir));
}

void GroupManager::close_dialog()
{
    if (pending() && _armed != u"close"_s) {
        _arm(u"close"_s, close_btn, u"Click again: discard %1 change(s)"_s.arg(pending()));
        return;
    }
    accept();
}

CultureManager::CultureManager(moddata::Mod &mod_, document::Document &doc_, const QString &backup_root_,
                               QWidget *parent)
    : GroupManager(mod_, doc_, backup_root_, parent)
{
    build();
}

ReligionManager::ReligionManager(moddata::Mod &mod_, document::Document &doc_, const QString &backup_root_,
                                 QWidget *parent)
    : GroupManager(mod_, doc_, backup_root_, parent, u"religion"_s, u"religions"_s, u"common/religion.txt"_s)
{
    build();
}

OrderedMap<QString, ItemInfo> ReligionManager::_items() const
{
    OrderedMap<QString, ItemInfo> out;
    for (const auto &[k, r] : mod.religions)
        out.insert(k, {r.group, r.name, r.color, r.icon});
    return out;
}

QStringList ReligionManager::_group_order() const
{
    if (!mod.religion_groups.isEmpty())
        return mod.religion_groups;
    std::set<QString> gs;
    for (const auto &[_k, r] : mod.religions)
        gs.insert(r.group);
    return QStringList(gs.begin(), gs.end());
}

QDialog *ReligionManager::_new_dialog()
{
    return new NewReligionDialog(mod, groups, _taken(), _selected_group(), this);
}

Result ReligionManager::_result_of(QDialog *dlg) const { return static_cast<NewReligionDialog *>(dlg)->result_data(); }

NewItem ReligionManager::_new_entry(const Result &d) const { return {d.key, d.group, d.color, d.name, d.icon}; }

QString ReligionManager::_apply()
{
    std::vector<religionfile::NewReligion> items;
    for (const NewItem &c : new_items)
        items.push_back({c.key, c.gid, c.color, c.name, c.icon});
    return religionfile::apply_changes(mod, new_groups, items, {moves.begin(), moves.end()}, backup_root);
}

void ReligionManager::_reload()
{
    OrderedMap<QString, QString> names;
    for (const NewItem &c : new_items)
        if (!c.name.isEmpty())
            names.insert(c.key, c.name);
    reload_religions(mod, names);
}

void ReligionManager::_decorate(QTreeWidgetItem *ci, const QString &, const ItemInfo *item, const NewItem *nw) const
{
    const QList<QPixmap> &icons = ddsimage::religion_icons(mod);
    const std::optional<long long> n = item ? item->icon : (nw ? nw->icon : std::nullopt);
    const Rgb col = item ? item->color : nw->color;
    if (n && *n && 0 < *n && *n <= icons.size())
        ci->setIcon(0, QIcon(icons[int(*n - 1)]));
    else
        ci->setIcon(0, swatch_icon(col, 12));
    ci->setIcon(1, swatch_icon(col, 12));
}

void reload_religions(moddata::Mod &mod, const OrderedMap<QString, QString> &names)
{
    if (mod.pending_script) {
        mod.pending_script->sync();
        return;
    }
    for (const auto &[k, v] : names)
        if (!mod.loc.contains(k))
            mod.loc.insert(k, v);
    mod._load_religions();
}

// ---------------------------------------------------------------------------------------------- new religion
NewReligionDialog::NewReligionDialog(moddata::Mod &mod_, const QStringList &groups, const QSet<QString> &taken_,
                                     const OptStr &default_group, QWidget *parent)
    : QDialog(parent), mod(mod_), taken(taken_), color(random_color())
{
    setWindowTitle(u"New religion"_s);
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    key_edit = new QLineEdit();
    key_edit->setPlaceholderText(u"lowercase_key, e.g. old_believers"_s);
    name_edit = new QLineEdit();
    name_edit->setPlaceholderText(u"Shown in game, e.g. Old Believers"_s);
    connect(key_edit, &QLineEdit::textEdited, this, &NewReligionDialog::_suggest_name);
    color_btn = new QPushButton();
    connect(color_btn, &QPushButton::clicked, this, [this] { _pick_color(); });
    _show_color();
    group_box = new QComboBox();
    group_box->addItem(u"＋ New group…"_s, u"__new__"_s);
    for (const QString &g : groups)
        group_box->addItem(loc_or(mod, g), g);
    const int i = (default_group && !default_group->isEmpty()) ? group_box->findData(*default_group) : 1;
    group_box->setCurrentIndex(group_box->count() > 1 ? std::max(1, i) : 0);
    new_group = new QLineEdit();
    new_group->setPlaceholderText(u"new group key, e.g. dharmic"_s);
    connect(group_box, &QComboBox::currentIndexChanged, this,
            [this](int) { new_group->setVisible(group_box->currentData().toString() == u"__new__"_s); });
    new_group->setVisible(group_box->currentData().toString() == u"__new__"_s);
    // icon: one of the frames of the game's religion icon strip
    const QList<QPixmap> &icons = ddsimage::religion_icons(mod);
    icon = 1;
    auto *icon_w = new QWidget();
    auto *grid = new QGridLayout(icon_w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    icon_group = new QButtonGroup(this);
    OrderedMap<long long, QStringList> used;
    for (const auto &[_k, r] : mod.religions)
        if (r.icon && *r.icon)
            used[*r.icon].append(r.name);
    const QString g0 = group_box->currentData().toString();
    long long first_in_group = 1;
    for (const auto &[_k, r] : mod.religions)
        if (r.group == g0 && r.icon && *r.icon) {
            first_in_group = *r.icon;
            break;
        }
    for (int k = 0; k < icons.size(); ++k) {
        const QPixmap &pm = icons[k];
        const long long n = k + 1;
        auto *b = new QPushButton();
        b->setCheckable(true);
        b->setIcon(QIcon(pm));
        b->setIconSize(pm.size());
        b->setFixedSize(pm.width() + 12, pm.height() + 12);
        const QStringList *u = used.get(n);
        b->setToolTip(u"icon = %1%2"_s.arg(n).arg(u ? u" — used by "_s + u->join(u", "_s) : u" — unused"_s));
        connect(b, &QPushButton::clicked, this, [this, n] { _set_icon(n); });
        icon_group->addButton(b, int(n));
        grid->addWidget(b, k / 7, k % 7);
    }
    if (icons.isEmpty()) {
        icon_spin = new QSpinBox();
        icon_spin->setRange(1, 99);
        connect(icon_spin, &QSpinBox::valueChanged, this, [this](int v) { _set_icon(v); });
        grid->addWidget(icon_spin, 0, 0);
    }
    icon_note = _lab(QString(), u"faint"_s);
    _set_icon(first_in_group);
    form->addRow(u"Religion key"_s, key_edit);
    form->addRow(u"Display name"_s, name_edit);
    form->addRow(u"Map colour"_s, color_btn);
    form->addRow(u"Icon"_s, icon_w);
    form->addRow(QString(), icon_note);
    form->addRow(u"Religion group"_s, group_box);
    form->addRow(QString(), new_group);
    lay->addLayout(form);
    lay->addWidget(_lab(u"Icons come from the game's religion icon strip (GFX_icon_religion); several religions can "
                        u"share one. The new religion copies the other lines (e.g. pagan = yes) of a religion of "
                        u"its group. It is written to common/religion.txt (a backup is made first)."_s,
                        u"faint"_s));
    err = _lab(QString(), u"warn"_s);
    err->hide();
    lay->addWidget(err);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &NewReligionDialog::_ok);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void NewReligionDialog::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

void NewReligionDialog::_set_icon(long long n)
{
    icon = n;
    if (QAbstractButton *b = icon_group->button(int(icon)))
        b->setChecked(true);
    QStringList users;
    for (const auto &[_k, r] : mod.religions)
        if (r.icon && *r.icon == icon)
            users << r.name;
    icon_note->setText(u"icon = %1%2"_s.arg(icon).arg(
        !users.isEmpty() ? u" (also used by %1)"_s.arg(users.join(u", "_s)) : QString()));
}

void NewReligionDialog::_suggest_name(const QString &t)
{
    if (!name_edit->isModified())
        name_edit->setText(capitalized_words(t));
}

void NewReligionDialog::_show_color()
{
    color_btn->setText(rgb_text(color));
    color_btn->setIcon(swatch_icon(color, 14));
}

void NewReligionDialog::_pick_color()
{
    const QColor c = QColorDialog::getColor(qcolor(color), this, u"Religion colour"_s);
    if (c.isValid()) {
        color = {c.red(), c.green(), c.blue()};
        _show_color();
    }
}

void NewReligionDialog::_ok()
{
    const QString key = key_edit->text().trimmed();
    if (!key_ok(key))
        return show_error(u"⚠ Religion key: use lowercase letters, digits and _ (starting with a letter)."_s);
    if (taken.contains(key))
        return show_error(u"⚠ '%1' is already used as a religion or group key."_s.arg(key));
    if (group_box->currentData().toString() == u"__new__"_s) {
        const QString g = new_group->text().trimmed();
        if (!key_ok(g) || taken.contains(g) || g == key)
            return show_error(u"⚠ Give the new group a unique lowercase key."_s);
    }
    accept();
}

Result NewReligionDialog::result_data() const
{
    QString g = group_box->currentData().toString();
    OptStr ng;
    if (g == u"__new__"_s) {
        g = new_group->text().trimmed();
        ng = g;
    }
    return {key_edit->text().trimmed(), name_edit->text().trimmed(), color, icon, g, ng};
}

OptStr create_religion(QWidget *parent, moddata::Mod &mod, const QString &backup_root, const OptStr &default_group)
{
    QSet<QString> taken;
    for (const auto &[k, _r] : mod.religions)
        taken.insert(k);
    for (const QString &g : mod.religion_groups)
        taken.insert(g);
    NewReligionDialog dlg(mod, mod.religion_groups, taken, default_group, parent);
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    const Result d = dlg.result_data();
    try {
        std::vector<std::pair<QString, OptStr>> ng;
        if (d.new_group && !d.new_group->isEmpty())
            ng.emplace_back(*d.new_group, std::nullopt);
        religionfile::apply_changes(mod, ng, {{d.key, d.group, d.color, d.name, d.icon}}, {}, backup_root);
    } catch (const std::exception &e) {
        report(parent, u"⚠ New religion not saved: %1"_s.arg(QString::fromUtf8(e.what())));
        return std::nullopt;
    }
    OrderedMap<QString, QString> names;
    if (!d.name.isEmpty())
        names.insert(d.key, d.name);
    reload_religions(mod, names);
    return d.key;
}

// ---------------------------------------------------------------------------------------------- pickers
// a tree row's data: UserRole = "item" / "group", UserRole + 1 = the search text, UserRole + 2 = the data
// (Python: UserRole holds the tuple (kind, data))
static constexpr int DATA_ROLE = Qt::UserRole + 2;

PickerPopup::PickerPopup(const Lead &lead, const std::vector<PickItem> &items, const Data &current, QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);               // (Python: the closed popup lingers as a hidden child)
    setObjectName(u"picker"_s);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    search = new QLineEdit();
    search->setPlaceholderText(u"Type to search…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, &PickerPopup::_filter);
    connect(search, &QLineEdit::returnPressed, this, &PickerPopup::_pick_first);
    search->installEventFilter(this);
    lay->addWidget(search);
    tree = new QTreeWidget();
    tree->setHeaderHidden(true);
    tree->setIndentation(14);
    tree->setUniformRowHeights(true);
    tree->setRootIsDecorated(false);
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int c) { _clicked(it, c); });
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it, int c) { _clicked(it, c); });
    lay->addWidget(tree, 1);
    QFont bold;
    bold.setBold(true);
    QTreeWidgetItem *cur_item = nullptr;
    for (const auto &[d, text] : lead) {
        if (!d && text.startsWith(u"＋"_s))
            continue;                                 // "＋ Add …" placeholder: nothing to pick
        auto *it = new QTreeWidgetItem(QStringList{text});
        it->setData(0, Qt::UserRole, u"item"_s);
        it->setData(0, DATA_ROLE, var(d));
        if (d == NEW)
            it->setForeground(0, QBrush(QColor(theme::GOLD)));
        tree->addTopLevelItem(it);
    }
    OrderedMap<QString, QTreeWidgetItem *> groups;
    for (const PickItem &p : items) {
        QTreeWidgetItem *parent_item = nullptr;
        if (p.group) {
            parent_item = groups.value(*p.group, nullptr);
            if (!parent_item) {
                parent_item = new QTreeWidgetItem(QStringList{*p.group});
                parent_item->setFont(0, bold);
                parent_item->setForeground(0, QBrush(QColor(theme::INK_DIM)));
                parent_item->setFlags(Qt::ItemIsEnabled);
                parent_item->setData(0, Qt::UserRole, u"group"_s);
                parent_item->setData(0, DATA_ROLE, *p.group);
                tree->addTopLevelItem(parent_item);
                groups.insert(*p.group, parent_item);
            }
        }
        auto *it = new QTreeWidgetItem(QStringList{p.text});
        if (p.rgb)
            it->setIcon(0, swatch_icon(*p.rgb, 12));
        it->setData(0, Qt::UserRole, u"item"_s);
        it->setData(0, DATA_ROLE, p.key);
        it->setData(0, Qt::UserRole + 1, u"%1 %2 %3"_s.arg(p.text, p.key, p.group.value_or(QString())).toLower());
        if (parent_item)
            parent_item->addChild(it);
        else
            tree->addTopLevelItem(it);
        if (current && p.key == *current)
            cur_item = it;
    }
    tree->expandAll();
    if (cur_item) {
        tree->setCurrentItem(cur_item);
        tree->scrollToItem(cur_item, QAbstractItemView::PositionAtCenter);
    }
}

bool PickerPopup::eventFilter(QObject *obj, QEvent *ev)
{
    // arrow keys in the search box move through the list
    if (obj == search && ev->type() == QEvent::KeyPress) {
        const int k = static_cast<QKeyEvent *>(ev)->key();
        if (k == Qt::Key_Down || k == Qt::Key_Up) {
            tree->setFocus();
            QCoreApplication::sendEvent(tree, ev);
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

std::vector<QTreeWidgetItem *> PickerPopup::_visible_items() const
{
    std::vector<QTreeWidgetItem *> out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        if (top->isHidden())
            continue;
        if (top->data(0, Qt::UserRole).toString() == u"item"_s)
            out.push_back(top);
        for (int j = 0; j < top->childCount(); ++j)
            if (!top->child(j)->isHidden())
                out.push_back(top->child(j));
    }
    return out;
}

void PickerPopup::_filter(const QString &text)
{
    const QString q = text.trimmed().toLower();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        if (top->data(0, Qt::UserRole).toString() == u"group"_s) {
            const bool gmatch = top->text(0).toLower().contains(q);
            bool any_child = false;
            for (int j = 0; j < top->childCount(); ++j) {
                QTreeWidgetItem *ch = top->child(j);
                const bool show = q.isEmpty() || gmatch || ch->data(0, Qt::UserRole + 1).toString().contains(q);
                ch->setHidden(!show);
                any_child |= show;
            }
            top->setHidden(!any_child);
        } else {
            QString blob = top->data(0, Qt::UserRole + 1).toString();
            if (blob.isEmpty())
                blob = top->text(0).toLower();
            top->setHidden(!q.isEmpty() && !blob.contains(q));
        }
    }
    std::vector<QTreeWidgetItem *> vis;
    for (QTreeWidgetItem *it : _visible_items())
        if (unvar(it->data(0, DATA_ROLE)) != NEW || q.isEmpty())
            vis.push_back(it);
    if (!vis.empty()) {
        QTreeWidgetItem *cur = tree->currentItem();
        tree->setCurrentItem(!q.isEmpty() ? vis[0] : (cur ? cur : vis[0]));
    }
}

void PickerPopup::_pick_first()
{
    QTreeWidgetItem *it = tree->currentItem();
    if (!it || it->isHidden() || it->data(0, Qt::UserRole).toString() != u"item"_s) {
        const std::vector<QTreeWidgetItem *> vis = _visible_items();
        it = vis.empty() ? nullptr : vis[0];
    }
    if (it)
        _clicked(it);
}

void PickerPopup::_clicked(QTreeWidgetItem *it, int)
{
    if (it->data(0, Qt::UserRole).toString() != u"item"_s)
        return;
    const Data d = unvar(it->data(0, DATA_ROLE));
    close();
    Q_EMIT picked(d);
}

PickCombo::PickCombo(const std::vector<PickItem> &items, const Lead &lead, const Data &default_, QWidget *parent)
    : QComboBox(parent)
{
    setMaxVisibleItems(24);
    fill(items, lead, default_);
}

void PickCombo::showPopup()
{
    auto *pop = new PickerPopup(_lead_items, _items, current(), this);
    connect(pop, &PickerPopup::picked, this, &PickCombo::_picked);
    const int w = std::max(width(), 400);
    QSet<QString> gs;
    for (const PickItem &p : _items)
        if (p.group)
            gs.insert(*p.group);
    const int h = std::min(520, 90 + 24 * int(_items.size() + _lead_items.size() + size_t(gs.size())));
    QPoint pos = mapToGlobal(QPoint(0, height() + 2));
    const QRect scr = screen()->availableGeometry();
    if (pos.y() + h > scr.bottom())
        pos.setY(std::max(scr.top(), mapToGlobal(QPoint(0, 0)).y() - h - 2));
    pos.setX(std::max(scr.left(), std::min(pos.x(), scr.right() - w)));
    pop->setGeometry(pos.x(), pos.y(), w, std::max(h, 200));
    pop->show();
    pop->search->setFocus();
}

void PickCombo::_picked(const Data &d)
{
    const int i = find(d);
    if (i >= 0) {
        setCurrentIndex(i);
        Q_EMIT activated(i);
    }
}

Data PickCombo::data_at(int i) const { return unvar(itemData(i)); }

int PickCombo::find(const Data &d) const
{
    for (int i = 0; i < count(); ++i)
        if (data_at(i) == d)
            return i;
    return -1;
}

OptStr PickCombo::key() const
{
    const Data d = current();
    return (d == HEADER || d == NEW) ? std::nullopt : d;
}

void PickCombo::fill(const std::vector<PickItem> &items, const Lead &lead, const Data &default_)
{
    _items = items;
    _lead_items = lead;
    blockSignals(true);
    clear();
    for (const auto &[d, text] : lead)
        addItem(text, var(d));
    QFont bold;
    bold.setBold(true);
    bool started = false;                             // (Python: group = object(), equal to nothing)
    QString group;
    for (const PickItem &p : items) {
        if (p.group && (!started || *p.group != group)) {
            started = true;
            group = *p.group;
            addItem(u"— %1 —"_s.arg(*p.group), HEADER);
            if (auto *m = qobject_cast<QStandardItemModel *>(model())) {
                QStandardItem *it = m->item(count() - 1);
                it->setEnabled(false);
                it->setFont(bold);
            }
        }
        if (p.rgb)
            addItem(swatch_icon(*p.rgb), (p.group ? u"   "_s : QString()) + p.text, p.key);
        else
            addItem(p.text, p.key);
    }
    int i = default_ ? find(default_) : -1;
    if (i < 0) {
        i = 0;
        for (int j = 0; j < count(); ++j) {
            const Data d = data_at(j);
            if (d != HEADER && d != NEW) {
                i = j;
                break;
            }
        }
    }
    setCurrentIndex(i);
    _last = i;
    blockSignals(false);
}

std::vector<PickItem> grouped_cultures(const moddata::Mod &mod)
{
    OrderedMap<QString, long long> order;
    for (int i = 0; i < mod.culture_groups.size(); ++i)   // ({g: i}: a repeated group keeps the last index)
        order.insert(mod.culture_groups[i], i);
    std::vector<const std::pair<QString, moddata::Culture> *> items;
    for (const auto &kv : mod.cultures)
        items.push_back(&kv);
    std::stable_sort(items.begin(), items.end(), [&](auto *a, auto *b) {
        const long long oa = order.value(a->second.group, 999), ob = order.value(b->second.group, 999);
        if (oa != ob)
            return oa < ob;
        return a->second.name.toLower() < b->second.name.toLower();
    });
    std::vector<PickItem> out;
    for (auto *kv : items) {
        const moddata::Culture &c = kv->second;
        out.push_back({kv->first, u"%1  (%2)"_s.arg(c.name, kv->first), c.color, loc_or(mod, c.group)});
    }
    return out;
}

std::vector<PickItem> grouped_religions(const moddata::Mod &mod)
{
    QStringList groups = mod.religion_groups;
    for (const auto &[_k, r] : mod.religions)
        if (!groups.contains(r.group))
            groups.append(r.group);
    std::vector<const std::pair<QString, moddata::Religion> *> items;
    for (const auto &kv : mod.religions)
        items.push_back(&kv);
    std::stable_sort(items.begin(), items.end(), [&](auto *a, auto *b) {
        return groups.indexOf(a->second.group) < groups.indexOf(b->second.group);
    });
    std::vector<PickItem> out;
    for (auto *kv : items) {
        const moddata::Religion &r = kv->second;
        out.push_back({kv->first, u"%1  (%2)"_s.arg(r.name, kv->first), r.color, loc_or(mod, r.group)});
    }
    return out;
}

std::vector<PickItem> all_poptypes(const moddata::Mod &mod)
{
    // Vanilla pop types (always) plus what the mod adds, under two headings.
    std::vector<PickItem> items;
    const std::vector<std::pair<QString, QString>> srcs = {{u"vanilla"_s, u"Vanilla"_s}, {u"mod"_s, u"Added by mod"_s}};
    for (const auto &[src, heading] : srcs) {
        QStringList keys;
        for (const auto &[k, v] : mod.poptypes)
            if (v.source == src)
                keys << k;
        std::stable_sort(keys.begin(), keys.end(),
                         [&](const QString &a, const QString &b) { return mod.poptype_sort_key(a) < mod.poptype_sort_key(b); });
        for (const QString &k : keys) {
            const moddata::PopType &t = *mod.poptypes.get(k);
            items.push_back({k, t.name, t.color, heading});
        }
    }
    return items;
}

const std::vector<long long> SizeField::STEPS = {1000, 5000, 10000, 50000, 100000};

SizeField::SizeField(long long value_, long long minimum, const QString &suffix, QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    spin = new QSpinBox();
    spin->setRange(int(minimum), 1000000000);
    spin->setValue(int(value_));
    spin->setGroupSeparatorShown(true);
    spin->setButtonSymbols(QSpinBox::NoButtons);
    spin->setSuffix(suffix);
    lay->addWidget(spin);
    for (int sign : {-1, 1}) {
        auto *row = new QHBoxLayout();
        row->setSpacing(4);
        for (long long v : STEPS) {                   // both rows run 1k → 100k
            auto *b = new QPushButton((sign < 0 ? u"−"_s : u"+"_s) + u"%1k"_s.arg(v / 1000));
            b->setObjectName(sign < 0 ? u"stepMinus"_s : u"stepPlus"_s);
            b->setFocusPolicy(Qt::NoFocus);
            const int d = int(sign * v);
            connect(b, &QPushButton::clicked, this, [this, d] { spin->setValue(spin->value() + d); });
            row->addWidget(b);
        }
        lay->addLayout(row);
    }
}

long long SizeField::value() const { return spin->value(); }

CultureCombo::CultureCombo(moddata::Mod &mod_, const QString &backup_root_, const OptStr &keep_text_,
                           const Data &default_, QWidget *parent)
    : PickCombo({}, {}, std::nullopt, parent), mod(mod_), backup_root(backup_root_), keep_text(keep_text_)
{
    fill(grouped_cultures(mod), _lead(), default_);
    connect(this, &QComboBox::activated, this, &CultureCombo::_activated);
}

Lead CultureCombo::_lead() const
{
    Lead lead = {{NEW, u"＋ New culture…"_s}};
    if (keep_text && !keep_text->isEmpty())
        lead.emplace_back(std::nullopt, *keep_text);
    return lead;
}

void CultureCombo::_activated(int i)
{
    if (data_at(i) == NEW) {
        const Data prev = data_at(_last);
        const OptStr k = create_culture(window(), mod, backup_root);
        fill(grouped_cultures(mod), _lead(), (k && !k->isEmpty()) ? k : prev);
    } else {
        _last = i;
    }
}

ReligionCombo::ReligionCombo(moddata::Mod &mod_, const QString &backup_root_, const OptStr &keep_text_,
                             const Data &default_, QWidget *parent)
    : PickCombo({}, {}, std::nullopt, parent), mod(mod_), backup_root(backup_root_), keep_text(keep_text_)
{
    fill(grouped_religions(mod), _lead(), default_);
    connect(this, &QComboBox::activated, this, &ReligionCombo::_activated);
}

Lead ReligionCombo::_lead() const
{
    Lead lead = {{NEW, u"＋ New religion…"_s}};
    if (keep_text && !keep_text->isEmpty())
        lead.emplace_back(std::nullopt, *keep_text);
    return lead;
}

void ReligionCombo::_activated(int i)
{
    if (data_at(i) == NEW) {
        const Data prev = data_at(_last);
        const OptStr k = create_religion(window(), mod, backup_root);
        fill(grouped_religions(mod), _lead(), (k && !k->isEmpty()) ? k : prev);
    } else {
        _last = i;
    }
}

AddPopDialog::AddPopDialog(moddata::Mod &mod_, document::Document &doc, long long n_provinces,
                           const QString &backup_root_, QWidget *parent)
    : QDialog(parent), mod(mod_), backup_root(backup_root_)
{
    setWindowTitle(u"Add pop"_s);
    resize(460, 0);
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    type_box = new PickCombo(all_poptypes(mod), {}, u"farmers"_s);
    // default culture/religion: the most common in the scenario
    OrderedMap<OptStr, long long> cult_sizes, rel_sizes;
    for (const auto &[pid, pops] : doc.pops)
        for (const moddata::Pop &p : pops) {
            cult_sizes[p.culture] += p.size;
            rel_sizes[p.religion] += p.size;
        }
    auto most = [](const OrderedMap<OptStr, long long> &m) -> Data {   // max(d, key=d.get): the first largest
        Data best;
        long long bv = 0;
        bool any = false;
        for (const auto &[k, v] : m)
            if (!any || v > bv) {
                any = true;
                best = k;
                bv = v;
            }
        return best;
    };
    cult = new CultureCombo(mod, backup_root, std::nullopt, most(cult_sizes));
    rel = new ReligionCombo(mod, backup_root, std::nullopt, most(rel_sizes));
    size = new SizeField(10000);
    form->addRow(u"Pop type"_s, type_box);
    form->addRow(u"Culture"_s, cult);
    form->addRow(u"Religion"_s, rel);
    form->addRow(u"Size (each province)"_s, size);
    lay->addLayout(form);
    lay->addWidget(_lab(u"Added to each of the %1 selected province(s). Merges into an existing pop with the "
                        u"same type, culture and religion."_s.arg(n_provinces),
                        u"faint"_s));
    err = _lab(QString(), u"warn"_s);
    err->hide();
    lay->addWidget(err);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &AddPopDialog::_ok);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void AddPopDialog::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

void AddPopDialog::_ok()
{
    const auto empty = [](const OptStr &k) { return !k || k->isEmpty(); };
    if (empty(type_box->key()) || empty(cult->key()) || empty(rel->key())) {
        show_error(u"⚠ Pick a pop type, culture and religion."_s);
        return;
    }
    accept();
}

std::tuple<OptStr, OptStr, OptStr, long long> AddPopDialog::values() const
{
    return {type_box->key(), cult->key(), rel->key(), size->value()};
}

PasteDialog::PasteDialog(const QString &clip_desc, long long clip_total, long long n_provinces, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(u"Paste pops"_s);
    resize(620, 0);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(_lab(u"Clipboard: %1"_s.arg(clip_desc)));
    lay->addWidget(_lab(u"Paste into the %1 selected province(s):"_s.arg(n_provinces)));
    add_rb = new QRadioButton(u"Add to the pops already there"_s);
    rep_rb = new QRadioButton(u"Replace all pops of the province"_s);
    add_rb->setChecked(true);
    auto *g1 = new QButtonGroup(this);
    g1->addButton(add_rb);
    g1->addButton(rep_rb);
    lay->addWidget(add_rb);
    lay->addWidget(rep_rb);
    lay->addSpacing(6);
    pct_rb = new QRadioButton(u"Sizes ×"_s);
    tot_rb = new QRadioButton(u"Scale so each province gets"_s);
    pct_rb->setChecked(true);
    auto *g2 = new QButtonGroup(this);
    g2->addButton(pct_rb);
    g2->addButton(tot_rb);
    pct = new QDoubleSpinBox();
    pct->setRange(0.1, 10000);
    pct->setValue(100);
    pct->setSuffix(u" %"_s);
    pct->setButtonSymbols(QDoubleSpinBox::NoButtons);
    total = new SizeField(std::max(1LL, clip_total), 1, u" people"_s);
    const std::vector<std::pair<QRadioButton *, QWidget *>> rows = {{pct_rb, pct}, {tot_rb, total}};
    for (const auto &[rb, w] : rows) {
        auto *row = new QHBoxLayout();
        row->addWidget(rb, 0, Qt::AlignTop);
        row->addWidget(w, 1);
        lay->addLayout(row);
    }
    connect(pct, &QDoubleSpinBox::valueChanged, this, [this](double) { pct_rb->setChecked(true); });
    connect(total->spin, &QSpinBox::valueChanged, this, [this](int) { tot_rb->setChecked(true); });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

std::tuple<bool, double, std::optional<long long>> PasteDialog::values() const
{
    return {rep_rb->isChecked(), pct->value() / 100.0,
            tot_rb->isChecked() ? std::optional<long long>(total->value()) : std::nullopt};
}

}  // namespace dialogs
