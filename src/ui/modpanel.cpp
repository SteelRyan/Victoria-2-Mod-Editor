// modpanel.py — see modpanel.h.
#include "ui/modpanel.h"
#include "core/document.h"
#include "core/events.h"
#include "core/localisation.h"
#include "core/modifiers.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace modpanel {

static const OrderedMap<QString, QString> HOW = {
    {u"adds"_s, u"adds it"_s}, {u"removes"_s, u"removes it"_s}, {u"checks"_s, u"checks for it"_s}};

QString attr_name(const moddata::Mod &mod, const QString &key)
{
    // an effect's name as the game writes it (its localisation), else the key made readable
    for (const QString &k : {key, py::upper(key), u"modifier_"_s + key, u"MODIFIER_"_s + py::upper(key)}) {
        const QString *t = mod.loc.get(k);
        if (t && !t->isEmpty() && t->size() < 60 && !t->contains(u'$'))
            return *t;
    }
    QString out = key;
    return out.replace(u'_', u' ');
}

QStringList effects_text(const moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &attrs)
{
    // ["Prestige −0.5", …] of a modifier's (key, value) list (the icon left out)
    QStringList out;
    for (const auto &[k, v] : attrs) {
        if (k == u"icon")
            continue;
        QString vv = py::strip(v);
        if (modifiers::num_ok(vv) && !vv.startsWith(u'-'))
            vv = u"+"_s + vv;
        out << u"%1 %2"_s.arg(attr_name(mod, k), vv.replace(u'-', u'−'));
    }
    return out;
}

bool SortItem::operator<(const QTreeWidgetItem &other) const
{
    const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
    const QVariant a = data(c, Qt::UserRole + 1), b = other.data(c, Qt::UserRole + 1);
    if (!a.isValid() || !b.isValid())
        return QTreeWidgetItem::operator<(other);
    if (a.typeId() == QMetaType::QString || b.typeId() == QMetaType::QString)
        return a.toString() < b.toString();
    return a.toLongLong() < b.toLongLong();
}

// dict(attrs).get(key, "")
static QString attr_value(const modifiers::Attrs &attrs, const QString &key)
{
    QString out;
    for (const auto &[k, v] : attrs)
        if (k == key)
            out = v;
    return out;
}

static localisation::LocEdits *loc_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
}

static modifiers::ModifierEdits *edits_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"event_mods"_s) ? &doc->map<modifiers::ModifierEdits>(u"event_mods"_s) : nullptr;
}

static QString nm_of(document::Document *doc, const moddata::Mod &mod, const QString &name)
{
    localisation::LocEdits *loc = loc_of(doc);
    QString t;
    if (loc != nullptr) {
        t = loc->text(name).value_or(QString());
    } else if (const QString *s = mod.loc.get(name)) {
        t = *s;
    }
    return !t.isEmpty() ? t : name;
}

// ======================================================================================== ModifierForm
ModifierForm::ModifierForm(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *s = new QVBoxLayout(this);
    s->setContentsMargins(0, 0, 0, 0);
    title = widgets::label(u"Pick a modifier"_s, u"title"_s);
    s->addWidget(title);
    meta = widgets::label(QString(), u"faint"_s);
    s->addWidget(meta);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    g->addWidget(widgets::label(u"Name in the game"_s, u"sub"_s, false), 0, 0);
    name_edit = new QLineEdit();
    connect(name_edit, &QLineEdit::editingFinished, this, [this] { _name_done(name_edit, cur); });
    g->addWidget(name_edit, 0, 1);
    g->addWidget(widgets::label(u"Description"_s, u"sub"_s, false), 1, 0);
    desc_edit = new QLineEdit();
    connect(desc_edit, &QLineEdit::editingFinished, this, [this] {
        _name_done(desc_edit, cur && !cur->isEmpty() ? std::optional<QString>(*cur + u"_desc"_s) : std::nullopt);
    });
    g->addWidget(desc_edit, 1, 1);
    g->addWidget(widgets::label(u"Icon"_s, u"sub"_s, false), 2, 0);
    icon_box = new QComboBox();
    for (int i = 1; i < 41; ++i)
        icon_box->addItem(u"Icon %1"_s.arg(i), QString::number(i));
    connect(icon_box, &QComboBox::activated, this, [this](int i) { _icon_done(i); });
    g->addWidget(icon_box, 2, 1, Qt::AlignLeft);
    edit_host = new QWidget();
    edit_host->setLayout(g);
    s->addWidget(edit_host);
    eff_title = widgets::label(u"Effects"_s, u"sub"_s);
    QFont f(eff_title->font());
    f.setBold(true);
    eff_title->setFont(f);
    s->addWidget(eff_title);
    rows_host = new QWidget();
    rows = new QGridLayout(rows_host);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setColumnStretch(0, 1);
    s->addWidget(rows_host);
    auto *row = new QHBoxLayout();
    add_key = new QComboBox();
    add_key->setEditable(true);
    add_key->setInsertPolicy(QComboBox::NoInsert);
    add_key->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add_key->lineEdit()->setPlaceholderText(u"Add an effect — pick or type its key"_s);
    row->addWidget(add_key, 1);
    add_val = new QLineEdit();
    add_val->setPlaceholderText(u"value"_s);
    add_val->setFixedWidth(90);
    connect(add_val, &QLineEdit::returnPressed, this, [this] { _add_done(); });
    row->addWidget(add_val);
    auto *b = new QPushButton(u"＋ Add"_s);
    connect(b, &QPushButton::clicked, this, [this] { _add_done(); });
    row->addWidget(b);
    add_row = new QWidget();
    add_row->setLayout(row);
    s->addWidget(add_row);
    add_warn = widgets::label(QString(), u"sub"_s);
    add_warn->setStyleSheet(u"color: #ff786e;"_s);
    s->addWidget(add_warn);
    used_lbl = new QLabel();
    used_lbl->setObjectName(u"sub"_s);
    used_lbl->setWordWrap(true);
    used_lbl->setTextFormat(Qt::RichText);
    used_lbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(used_lbl, &QLabel::linkActivated, this, [this](const QString &h) { _used_link(h); });
    s->addWidget(used_lbl);
    del_btn = new QPushButton(u"Delete modifier"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _delete(); });
    s->addWidget(del_btn, 0, Qt::AlignLeft);
}

void ModifierForm::refresh(document::Document *doc_, events::EventDB *db_, const Used *used_)
{
    doc = doc_;
    edits = edits_of(doc);
    db = db_;
    used = used_;
    if (edits != nullptr) {
        const QString txt = add_key->currentText();
        add_key->blockSignals(true);
        add_key->clear();
        add_key->addItems(edits->db().known_attrs);
        add_key->setCurrentIndex(-1);
        add_key->setEditText(txt);
        add_key->blockSignals(false);
    }
    fill();
}

QString ModifierForm::nm(const QString &name) const
{
    return nm_of(doc, mod, name);
}

void ModifierForm::show_mod(const std::optional<QString> &name)
{
    if (name != cur)
        _armed = false;
    cur = name;
    fill();
}

std::vector<std::pair<const events::Entry *, QString>> ModifierForm::_used_of(const QString &name) const
{
    if (used == nullptr)
        return {};
    const auto *u = used->get(name);
    return u ? *u : std::vector<std::pair<const events::Entry *, QString>>();
}

void ModifierForm::fill()
{
    const bool ok = edits != nullptr && cur && edits->exists(*cur);
    for (QWidget *w : {edit_host, static_cast<QWidget *>(eff_title), rows_host, add_row,
                       static_cast<QWidget *>(used_lbl), static_cast<QWidget *>(del_btn)})
        w->setVisible(ok);
    if (!ok) {
        title->setText(u"Pick a modifier"_s);
        meta->setText(QString());
        add_warn->setText(QString());
        return;
    }
    const QString name = *cur;
    title->setText(nm(name));
    meta->setText(u"%1 · common/event_modifiers.txt"_s.arg(name));
    localisation::LocEdits *loc = loc_of(doc);
    for (const auto &[ed, key] : std::vector<std::pair<QLineEdit *, QString>>{{name_edit, name},
                                                                              {desc_edit, name + u"_desc"_s}}) {
        if (!ed->hasFocus()) {
            const auto t = loc != nullptr && loc->fresh.contains(key) ? loc->text(key) : refs::loc_text(mod, key);
            ed->setText(t.value_or(QString()));
            ed->setPlaceholderText(u"No localisation present — type one to add it (written on Apply)"_s);
        }
    }
    const modifiers::Attrs attrs = edits->attrs(name);
    std::optional<QString> icon;
    for (const auto &[k, v] : attrs)
        if (k == u"icon") {
            icon = v;
            break;
        }
    icon_box->setCurrentIndex(icon_box->findData(py::strip(icon.value_or(QString()))));
    modifiers::Attrs eff;
    QStringList keys;
    for (const auto &[k, v] : attrs)
        if (k != u"icon") {
            eff.emplace_back(k, v);
            keys << k;
        }
    const auto sig = std::make_pair(name, keys);
    if (sig != _rows_sig) {
        _rows_sig = sig;
        while (rows->count()) {
            QLayoutItem *item = rows->takeAt(0);
            if (QWidget *w = item->widget()) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
            delete item;
        }
        _val_edits.clear();
        for (int r = 0; r < int(eff.size()); ++r) {
            const auto &[k, v] = eff[size_t(r)];
            QLabel *lb = widgets::label(attr_name(mod, k), u"sub"_s, false);
            lb->setToolTip(k);
            rows->addWidget(lb, r, 0);
            auto *ed = new QLineEdit(v);
            ed->setFixedWidth(90);
            connect(ed, &QLineEdit::editingFinished, this, [this, kk = k, ed] { _val_done(kk, ed); });
            rows->addWidget(ed, r, 1);
            auto *x = new QPushButton(u"✕"_s);
            x->setToolTip(u"Take “%1” off"_s.arg(k));
            connect(x, &QPushButton::clicked, this, [this, kk = k] { _del_attr(kk); });
            rows->addWidget(x, r, 2);
            _val_edits.insert(k, ed);
        }
        if (eff.empty())
            rows->addWidget(widgets::label(u"none — a marker that events check or remove"_s, u"faint"_s), 0, 0);
    } else {
        for (const auto &[k, v] : eff) {
            QLineEdit *ed = _val_edits.value(k);
            if (ed != nullptr && !ed->hasFocus()) {
                ed->setText(v);
                ed->setStyleSheet(QString());
            }
        }
    }
    const auto us = _used_of(name);
    if (!us.empty() && db != nullptr) {
        QString rows_html;
        for (size_t i = 0; i < us.size() && i < 60; ++i) {
            const events::Entry *e = us[i].first;
            const QString &how = us[i].second;
            const QString href = e->is_event() ? u"ev:%1"_s.arg(e->id.value_or(u"None"_s)) : u"dec:%1"_s.arg(e->key);
            const QString idk = e->id && !e->id->isEmpty() ? *e->id : e->key;
            rows_html += u"<div><a href=\"%1\" style=\"color:%2\">%3</a> <span style=\"color:#8a8272\">(%4 %5) — "
                         "%6</span></div>"_s.arg(href, theme::GOLD, refs::esc(db->name(*e)),
                                                 e->is_event() ? u"event"_s : u"decision"_s, refs::esc(idk),
                                                 *HOW.get(how));
        }
        const QString more = us.size() > 60 ? u"<div>… and %1 more</div>"_s.arg(us.size() - 60) : QString();
        used_lbl->setText(u"<h4>Used by (%1)</h4>%2%3"_s.arg(QString::number(us.size()), rows_html, more));
    } else {
        used_lbl->setText(u"<h4>Used by</h4><div style='color:#8a8272'>no event or decision adds, removes or checks "
                          "it</div>"_s);
    }
    _set_del();
}

void ModifierForm::_set_del()
{
    const size_t n = cur ? _used_of(*cur).size() : 0;
    if (_armed) {
        del_btn->setText(u"Click again to delete “%1”%2"_s.arg(
            cur.value_or(u"None"_s),
            n ? u" — %1 events / decisions still use it"_s.arg(QString::number(n)) : QString()));
        del_btn->setStyleSheet(u"QPushButton { color: #ff786e; border-color: #ff786e; }"_s);
    } else {
        del_btn->setText(u"Delete modifier"_s);
        del_btn->setStyleSheet(QString());
    }
}

void ModifierForm::_val_done(const QString &key, QLineEdit *ed)
{
    if (!cur)
        return;
    const QString name = *cur;
    const QString v = py::strip(ed->text());
    if (!modifiers::num_ok(v)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        add_warn->setText(u"“%1” needs a number (like 0.1 or -2)."_s.arg(QString(key).replace(u'_', u' ')));
        return;
    }
    ed->setStyleSheet(QString());
    add_warn->setText(QString());
    if (py::strip(attr_value(edits->attrs(name), key)) == v)
        return;
    Q_EMIT modEdit(u"%1: %2 = %3"_s.arg(name, key, v), [name, key, v](modifiers::ModifierEdits &m) {
        m.set(name, modifiers::set_attr(m.text(name), key, v));
    });
}

void ModifierForm::_del_attr(const QString &key)
{
    if (!cur)
        return;
    const QString name = *cur;
    Q_EMIT modEdit(u"%1: %2 taken off"_s.arg(name, key), [name, key](modifiers::ModifierEdits &m) {
        m.set(name, modifiers::del_attr(m.text(name), key));
    });
}

void ModifierForm::_add_done()
{
    if (!cur)
        return;
    const QString name = *cur;
    const QString key = py::strip(add_key->currentText());
    const QString v = py::strip(add_val->text());
    if (!modifiers::name_ok(key)) {
        add_warn->setText(u"Pick an effect (its key: letters, digits, _)."_s);
        return;
    }
    if (key == u"icon") {
        add_warn->setText(u"The icon is picked above."_s);
        return;
    }
    if (!modifiers::num_ok(v)) {
        add_warn->setText(u"Give it a number (like 0.1 or -2)."_s);
        return;
    }
    if (!edits->db().known_attrs.contains(key))
        add_warn->setText(u"Note: “%1” isn't used anywhere else in the mod — check the spelling."_s.arg(key));
    else
        add_warn->setText(QString());
    add_val->clear();
    add_key->setEditText(QString());
    Q_EMIT modEdit(u"%1: %2 = %3"_s.arg(name, key, v), [name, key, v](modifiers::ModifierEdits &m) {
        m.set(name, modifiers::set_attr(m.text(name), key, v));
    });
}

void ModifierForm::_icon_done(int)
{
    if (!cur)
        return;
    const QString name = *cur;
    const QString v = icon_box->currentData().toString();
    if (!v.isEmpty() && py::strip(attr_value(edits->attrs(name), u"icon"_s)) != v)
        Q_EMIT modEdit(u"%1: icon %2"_s.arg(name, v), [name, v](modifiers::ModifierEdits &m) {
            m.set(name, modifiers::set_attr(m.text(name), u"icon"_s, v));
        });
}

void ModifierForm::_name_done(QLineEdit *ed, const std::optional<QString> &key)
{
    if (!key || key->isEmpty())
        return;
    const QString text = py::split(ed->text()).join(u' ');
    localisation::LocEdits *loc = loc_of(doc);
    QString old;
    if (loc != nullptr)
        old = loc->text(*key).value_or(QString());
    else if (const QString *s = mod.loc.get(*key))
        old = *s;
    if (!text.isEmpty() && text != old)
        Q_EMIT nameEdit(*key, text);
}

void ModifierForm::_delete()
{
    if (!_armed) {
        _armed = true;
        _set_del();
        return;
    }
    const QString name = cur.value_or(u"None"_s);
    _armed = false;
    cur.reset();
    Q_EMIT modEdit(u"Modifier %1 deleted"_s.arg(name), [name](modifiers::ModifierEdits &m) { m.remove(name); });
    Q_EMIT deleted();
}

void ModifierForm::_used_link(const QString &href)
{
    if (db == nullptr)
        return;
    events::Entry *e = nullptr;
    if (href.startsWith(u"ev:"_s)) {
        if (events::Entry *const *p = db->by_id.get(href.mid(3)))
            e = *p;
    } else {
        const QString key = href.mid(4);
        for (const auto &x : db->entries)
            if (!x->is_event() && x->key == key) {
                e = x.get();
                break;
            }
    }
    if (e != nullptr)
        Q_EMIT entryPicked(e);
}

// ======================================================================================== ModifierView
ModifierView::ModifierView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *s = new widgets::Section();
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a modifier by name, key or effect…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_list(); });
    s->lay->addWidget(search);
    auto *row = new QHBoxLayout();
    count = widgets::label(QString(), u"sub"_s);
    row->addWidget(count, 1);
    auto *b = new QPushButton(u"＋ New modifier"_s);
    connect(b, &QPushButton::clicked, this, [this] { _new_open(); });
    row->addWidget(b, 0, Qt::AlignTop);
    s->lay->addLayout(row);
    new_row = new QWidget();
    auto *nv = new QVBoxLayout(new_row);
    nv->setContentsMargins(0, 0, 0, 0);
    auto *nh = new QHBoxLayout();
    new_edit = new QLineEdit();
    new_edit->setPlaceholderText(u"Key of the new modifier (letters, digits, _)"_s);
    connect(new_edit, &QLineEdit::returnPressed, this, [this] { _new_done(); });
    nh->addWidget(new_edit, 1);
    b = new QPushButton(u"Make it"_s);
    connect(b, &QPushButton::clicked, this, [this] { _new_done(); });
    nh->addWidget(b);
    b = new QPushButton(u"Cancel"_s);
    connect(b, &QPushButton::clicked, this, [this] { new_row->setVisible(false); });
    nh->addWidget(b);
    nv->addLayout(nh);
    new_warn = widgets::label(QString(), u"sub"_s);
    new_warn->setStyleSheet(u"color: #ff786e;"_s);
    connect(new_edit, &QLineEdit::textChanged, this, [this](const QString &) { new_warn->setText(QString()); });
    nv->addWidget(new_warn);
    new_row->setVisible(false);
    s->lay->addWidget(new_row);
    lst = new QTreeWidget();
    lst->setColumnCount(4);
    lst->setHeaderLabels({u"Name"_s, u"Key"_s, u"Effects"_s, u"Used by"_s});
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::SingleSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    lst->setMinimumHeight(360);
    QHeaderView *hd = lst->header();
    hd->setStretchLastSection(false);
    hd->setSectionsMovable(false);
    hd->setSectionResizeMode(0, QHeaderView::Stretch);
    for (const auto &[c, w] : std::vector<std::pair<int, int>>{{1, 200}, {2, 70}, {3, 80}}) {
        hd->setSectionResizeMode(c, QHeaderView::Fixed);
        hd->resizeSection(c, w);
    }
    lst->setSortingEnabled(true);
    lst->sortByColumn(1, Qt::AscendingOrder);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *c, QTreeWidgetItem *p) { _picked(c, p); });
    s->lay->addWidget(lst);
    lay->addWidget(s);
    s = new widgets::Section();
    form = new ModifierForm(mod);
    connect(form, &ModifierForm::modEdit, this, &ModifierView::modEdit);
    connect(form, &ModifierForm::nameEdit, this, &ModifierView::nameEdit);
    connect(form, &ModifierForm::entryPicked, this, &ModifierView::entryPicked);
    connect(form, &ModifierForm::deleted, this, [this] { cur.reset(); });
    s->lay->addWidget(form);
    lay->addWidget(s);
    lay->addStretch(1);
}

void ModifierView::refresh(document::Document *doc_, events::EventDB *db_)
{
    doc = doc_;
    edits = edits_of(doc);
    db = db_;
    if (edits == nullptr)
        return;
    if (_used_src != db && db != nullptr) {
        used = edits->db().used_by(*db);
        _used_src = db;
    }
    _fill_list();
    form->cur = cur;
    form->refresh(doc, db, &used);
}

QString ModifierView::nm(const QString &name) const
{
    return nm_of(doc, mod, name);
}

bool ModifierView::exists(const QString &name) const
{
    return edits != nullptr && edits->exists(name);
}

void ModifierView::select(const QString &name)
{
    cur = name;
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    _fill_list();
    form->show_mod(name);
}

void ModifierView::_fill_list()
{
    if (edits == nullptr)
        return;
    const QString q = py::lower(py::strip(search->text()));
    lst->blockSignals(true);
    lst->setSortingEnabled(false);
    lst->clear();
    QTreeWidgetItem *cur_it = nullptr;
    int n = 0;
    for (const QString &name : edits->names()) {
        modifiers::Attrs attrs;
        QStringList keys;
        for (const auto &[k, v] : edits->attrs(name))
            if (k != u"icon") {
                attrs.emplace_back(k, v);
                keys << k;
            }
        const auto *u = used.get(name);
        const qsizetype nu = u ? qsizetype(u->size()) : 0;
        const QString nmn = nm(name);
        if (!q.isEmpty() && !py::lower(u"%1 %2 %3"_s.arg(name, nmn, keys.join(u' '))).contains(q))
            continue;
        auto *it = new SortItem(QStringList{nmn, name, QString::number(attrs.size()), QString::number(nu)});
        it->setData(0, Qt::UserRole, name);
        it->setData(0, Qt::UserRole + 1, py::lower(nmn));
        it->setData(1, Qt::UserRole + 1, py::lower(name));
        it->setData(2, Qt::UserRole + 1, qlonglong(attrs.size()));
        it->setData(3, Qt::UserRole + 1, qlonglong(nu));
        for (int c : {2, 3})
            it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
        const QString tip = effects_text(mod, attrs).join(u'\n');
        it->setToolTip(0, !tip.isEmpty() ? tip : u"no effects (a marker other events check)"_s);
        lst->addTopLevelItem(it);
        ++n;
        if (cur && name == *cur)
            cur_it = it;
    }
    lst->setSortingEnabled(true);
    if (cur_it != nullptr) {
        lst->setCurrentItem(cur_it);
        lst->scrollToItem(cur_it);
    }
    lst->blockSignals(false);
    count->setText(u"%1 modifiers in common/event_modifiers.txt — click a column's name to sort"_s.arg(n));
}

void ModifierView::_picked(QTreeWidgetItem *c, QTreeWidgetItem *)
{
    if (c == nullptr)
        return;
    cur = c->data(0, Qt::UserRole).toString();
    form->show_mod(cur);
    Q_EMIT modPicked(*cur);
}

void ModifierView::_new_open()
{
    new_row->setVisible(true);
    new_edit->clear();
    new_edit->setFocus();
}

void ModifierView::_new_done()
{
    const QString name = py::strip(new_edit->text());
    if (!modifiers::name_ok(name)) {
        new_warn->setText(u"A key is letters, digits and _ (no spaces), not starting with a digit."_s);
        return;
    }
    if (edits->exists(name)) {
        new_warn->setText(u"“%1” is there already."_s.arg(name));
        return;
    }
    new_row->setVisible(false);
    cur = name;
    form->cur = name;
    Q_EMIT modEdit(u"New modifier %1"_s.arg(name), [name](modifiers::ModifierEdits &m) { m.add(name); });
}

}  // namespace modpanel
