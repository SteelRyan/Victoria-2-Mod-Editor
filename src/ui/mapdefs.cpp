// mapdefs.py — line by line.
#include "ui/mapdefs.h"

#include "core/fs.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "ui/theme.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace mapdefs {

const QRegularExpression KEY_RE(uR"(^[A-Za-z_][A-Za-z0-9_]*$)"_s);
const QStringList REF_DIRS{u"events"_s, u"decisions"_s, u"common"_s, u"history"_s};

bool value_ok(const QString &v)
{
    if (v.isEmpty())
        return false;
    for (QChar c : v)
        if (py::is_py_space(c.unicode()) || u"{}=#\""_s.contains(c))
            return false;
    return true;
}

static QLabel *_lab(const QString &text, const QString &obj = u"sub"_s)
{
    auto *l = new QLabel(text);
    l->setObjectName(obj);
    l->setWordWrap(true);
    return l;
}

static QString s_if(long long n) { return n == 1 ? QString() : u"s"_s; }

QString pretty(const QString &name) { return py::capitalize(QString(name).replace(u'_', u' ')); }

// (os.walk of one folder: its files first, then each subfolder in the listing's order; a folder that can't be listed
// is skipped)
static bool walk_refs(const QString &dir, const QString &rel, const QRegularExpression &rx, QStringList &out, int limit)
{
    std::vector<fs::DirEntry> ents;
    try {
        ents = fs::scandir(dir);
    } catch (const fs::OSError &) {
        return false;
    }
    QStringList dirs;
    for (const fs::DirEntry &e : ents) {
        if (e.is_dir || fs::isdir(e.path)) {
            dirs << e.name;
            continue;
        }
        if (!e.name.toLower().endsWith(u".txt"_s))
            continue;
        try {
            if (rx.match(script::read_text(e.path)).hasMatch())
                out << rel + u'/' + e.name;
        } catch (const fs::OSError &) {
            continue;
        }
        if (out.size() >= limit)
            return true;
    }
    for (const QString &d : dirs)
        if (walk_refs(dir + u'\\' + d, rel + u'/' + d, rx, out, limit))
            return true;
    return false;
}

QStringList find_references(const moddata::Mod &mod, const QString &name, int limit)
{
    const QRegularExpression rx(u"(?<![A-Za-z0-9_])"_s + QRegularExpression::escape(name) + u"(?![A-Za-z0-9_])"_s);
    QStringList out;
    for (const QString &d : REF_DIRS)
        if (walk_refs(mod.root + u'\\' + d, d, rx, out, limit))
            return out;
    return out;
}

// ---------------------------------------------------------------------------------------------- RemoveDialog
RemoveDialog::RemoveDialog(const QString &kind, const QString &name, long long count, const QStringList &targets,
                           const QStringList &refs, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(u"Remove %1"_s.arg(kind));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(_lab(u"Remove <b>%1</b> (%2) from map/%3.txt?"_s.arg(pretty(name), name, kind), u"title"_s));
    if (count) {
        lay->addWidget(_lab(u"Its %1 province%2 need another %3:"_s.arg(count).arg(s_if(count), kind)));
        target = new QComboBox();
        for (const QString &t : targets)
            target->addItem(pretty(t), t);
        lay->addWidget(target);
    } else {
        lay->addWidget(_lab(u"It has no provinces."_s, u"faint"_s));
    }
    if (!refs.isEmpty()) {
        const QString more = refs.size() <= 8 ? QString() : u"\n… and %1 more"_s.arg(refs.size() - 8);
        lay->addWidget(_lab(u"⚠ “%1” is still mentioned in these mod files. Anything there that uses it "
                            u"(e.g. %2 = %3 triggers) stops working:\n%4%5"_s
                                .arg(name, kind, name, refs.mid(0, 8).join(u'\n'), more),
                            u"warn"_s));
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(u"Remove"_s);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

std::optional<QString> RemoveDialog::chosen() const
{
    if (!target)
        return std::nullopt;
    const QVariant d = target->currentData();
    if (!d.isValid())
        return std::nullopt;
    return d.toString();
}

// ---------------------------------------------------------------------------------------------- ModifierRow
ModifierRow::ModifierRow(const QString &k, const QString &v, const QStringList &keys,
                         const std::function<void(ModifierRow *)> &on_remove)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    key = new QLineEdit(k);
    key->setPlaceholderText(u"modifier_key"_s);
    auto *comp = new QCompleter(keys, key);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setFilterMode(Qt::MatchContains);
    key->setCompleter(comp);
    value = new QLineEdit(v);
    value->setPlaceholderText(u"0.0"_s);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setFixedWidth(110);
    auto *rm = new QPushButton(u"✕"_s);
    rm->setObjectName(u"iconDel"_s);
    rm->setToolTip(u"Remove this modifier"_s);
    connect(rm, &QPushButton::clicked, this, [this, on_remove] { on_remove(this); });
    lay->addWidget(key, 1);
    lay->addWidget(value);
    lay->addWidget(rm);
}

std::pair<QString, QString> ModifierRow::pair() const { return {py::strip(key->text()), py::strip(value->text())}; }

// ---------------------------------------------------------------------------------------------- MapDefsDialog
MapDefsDialog::MapDefsDialog(mapfiles::MembershipFile &mf_, const std::optional<QString> &start, QWidget *parent)
    : QDialog(parent), mf(mf_), kind(mf_.kind)
{
    setWindowTitle(u"%1s — map/%2.txt"_s.arg(py::capitalize(kind), kind));
    resize(780, 540);
    for (const QString &n : mf.names) {
        const mapfiles::Mods *m = mf.mods.get(n);
        work.insert(n, m ? *m : mapfiles::Mods{});
    }
    names = mf.names;
    keys = mf.known_modifier_keys();
    get_text = [](QWidget *p, const QString &title, const QString &label) {
        bool ok = false;
        const QString t = QInputDialog::getText(p, title, label, QLineEdit::Normal, QString(), &ok);
        return std::pair<QString, bool>(t, ok);
    };
    run_remove = [](RemoveDialog &dlg) { return dlg.exec() == QDialog::Accepted; };

    auto *lay = new QVBoxLayout(this);
    if (!mf.mod.in_mod(mf.path))
        lay->addWidget(_lab(u"This mod has no map/%1.txt of its own (it uses the base game's). Edit away: "
                            u"Apply copies the base game's file into the mod and writes your changes there — "
                            u"the base game is never touched."_s.arg(kind),
                            u"faint"_s));
    auto *body = new QHBoxLayout();
    body->setSpacing(14);
    lay->addLayout(body, 1);

    auto *left = new QVBoxLayout();
    list = new QListWidget();
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(list, &QListWidget::currentRowChanged, this, &MapDefsDialog::_on_row);
    left->addWidget(list, 1);
    auto *brow = new QHBoxLayout();
    new_btn = new QPushButton(u"＋ New %1…"_s.arg(kind));
    connect(new_btn, &QPushButton::clicked, this, &MapDefsDialog::_new);
    del_btn = new QPushButton(u"Remove…"_s);
    connect(del_btn, &QPushButton::clicked, this, &MapDefsDialog::_remove);
    brow->addWidget(new_btn, 1);
    brow->addWidget(del_btn);
    left->addLayout(brow);
    body->addLayout(left, 2);

    auto *right = new QVBoxLayout();
    head = new QLabel();
    head->setObjectName(u"title"_s);
    right->addWidget(head);
    name_row = new QWidget();
    auto *nr = new QHBoxLayout(name_row);
    nr->setContentsMargins(0, 0, 0, 0);
    nr->addWidget(_lab(u"Name in the game"_s, u"sub"_s));
    name_field = new QLineEdit();
    name_field->setToolTip(u"The continent's name in the localisation — written on Apply (in place, or to "
                           u"the editor's own localisation file if the mod has no name for it yet)"_s);
    connect(name_field, &QLineEdit::editingFinished, this, &MapDefsDialog::_name_edited);
    nr->addWidget(name_field, 1);
    right->addWidget(name_row);
    name_row->setVisible(kind == u"continent"_s);
    info = _lab(QString(), u"faint"_s);
    right->addWidget(info);
    auto *hdr = new QHBoxLayout();
    hdr->setContentsMargins(2, 6, 0, 0);
    hdr->addWidget(_lab(u"Modifier"_s, u"label"_s), 1);
    QLabel *v = _lab(u"Value"_s, u"label"_s);
    v->setFixedWidth(110 + 6 + 28);
    hdr->addWidget(v);
    right->addLayout(hdr);
    rows_box = new QWidget();
    rows_lay = new QVBoxLayout(rows_box);
    rows_lay->setContentsMargins(0, 0, 4, 0);
    rows_lay->setSpacing(6);
    rows_lay->addStretch(1);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidget(rows_box);
    sc->setObjectName(u"defsScroll"_s);
    sc->viewport()->setObjectName(u"defsViewport"_s);
    rows_box->setObjectName(u"defsRows"_s);
    sc->setStyleSheet(u"#defsScroll, #defsViewport, #defsRows { background: transparent; }"_s);
    right->addWidget(sc, 1);
    auto *row = new QHBoxLayout();
    add_btn = new QPushButton(u"＋ Add modifier"_s);
    connect(add_btn, &QPushButton::clicked, this, [this] { _add_row(QString(), QString(), true); });
    row->addWidget(add_btn);
    row->addStretch(1);
    right->addLayout(row);
    right->addWidget(_lab(u"Values are added to the province, e.g. farm_rgo_eff = -0.1 is −10% farm "
                          u"efficiency, max_attrition = 5 adds 5% attrition. Any modifier key can be typed; "
                          u"the list suggests the ones used in the mod plus common province modifiers."_s,
                          u"faint"_s));
    body->addLayout(right, 3);

    err = _lab(QString(), u"warn"_s);   // problems in red here, no pop-ups
    err->hide();
    lay->addWidget(err);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(u"OK  (written on Apply)"_s);
    connect(bb, &QDialogButtonBox::accepted, this, &MapDefsDialog::_ok);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    _fill_list();
    const int i = start && names.contains(*start) ? int(names.indexOf(*start)) : 0;
    if (!names.isEmpty())
        list->setCurrentRow(i);
}

// ------------------------------------------------------------------ list
// Provinces the name will have once the pending removals have moved theirs.
long long MapDefsDialog::_count(const QString &name) const
{
    auto len = [this](const QString &n) -> long long {
        const auto *l = mf.lists.get(n);
        return l ? static_cast<long long>(l->size()) : 0;
    };
    long long n = len(name);
    for (const QString &r : removed)
        if (_final_target(r) == name)
            n += len(r);
    return n;
}

std::optional<QString> MapDefsDialog::_final_target(const QString &r) const
{
    std::optional<QString> t = moves.value(r, std::nullopt);
    while (t && moves.contains(*t))
        t = moves.value(*t, std::nullopt);
    return t;
}

void MapDefsDialog::_fill_list()
{
    list->blockSignals(true);
    list->clear();
    for (const QString &n : names) {
        const bool van = mf.is_vanilla(n);
        auto *it = new QListWidgetItem(u"%1  (%2)%3%4"_s.arg(_shown(n)).arg(_count(n))
                                           .arg(new_names.contains(n) ? u"  — new"_s : QString(),
                                                van ? u"   · vanilla"_s : QString()));
        it->setData(Qt::UserRole, n);
        it->setToolTip(van ? u"%1 — vanilla, can't be removed"_s.arg(n) : n);
        if (van)
            it->setForeground(QColor(theme::INK_DIM));
        list->addItem(it);
    }
    list->blockSignals(false);
}

void MapDefsDialog::_on_row(int row)
{
    if (current)
        _commit();
    if (row < 0 || row >= names.size()) {
        current.reset();
        _set_rows({});
        del_btn->setEnabled(false);
        return;
    }
    const QString n = names[row];
    current = n;
    const long long cnt = _count(n);
    const bool van = mf.is_vanilla(n);
    head->setText(_shown(n));
    name_field->blockSignals(true);
    QString shown = renames.value(n);
    if (shown.isEmpty())
        shown = mf.mod.loc.value(n);
    name_field->setText(shown);
    name_field->setPlaceholderText(pretty(n));
    name_field->blockSignals(false);
    info->setText(u"key: %1  ·  %2 province%3%4%5"_s.arg(n).arg(cnt).arg(
        s_if(cnt), van ? u"  ·  vanilla — can't be removed"_s : QString(),
        cnt == 0 ? u"  ·  assign provinces with the %1 box in the Provinces panel"_s.arg(py::capitalize(kind))
                 : QString()));
    del_btn->setEnabled(!van);
    del_btn->setToolTip(van ? u"Vanilla %1s can't be removed — the base game expects them."_s.arg(kind)
                            : u"Remove %1 (its provinces move to another %2)."_s.arg(n, kind));
    _set_rows(work.value(n));
}

// What the list calls it: the name in the game (continents), else the key written out.
QString MapDefsDialog::_shown(const QString &n) const
{
    if (kind == u"continent"_s) {
        QString s = renames.value(n);
        if (s.isEmpty())
            s = mf.mod.loc.value(n);
        return s.isEmpty() ? pretty(n) : s;
    }
    return pretty(n);
}

void MapDefsDialog::_name_edited()
{
    const std::optional<QString> n = current;
    const QString name = py::split(name_field->text()).join(u' ');
    if (!n || name.isEmpty())
        return;
    const QString *loc = mf.mod.loc.get(*n);
    if (loc && name == *loc)
        renames.remove(*n);
    else
        renames.insert(*n, name);
    const int row = list->currentRow();
    head->setText(_shown(*n));
    _fill_list();
    list->setCurrentRow(row);
}

// ------------------------------------------------------------------ modifier rows
void MapDefsDialog::_set_rows(const mapfiles::Mods &pairs)
{
    for (ModifierRow *r : rows) {
        r->setParent(nullptr);
        r->deleteLater();
    }
    rows.clear();
    for (const auto &[k, v] : pairs)
        _add_row(k, v);
}

void MapDefsDialog::_add_row(const QString &key, const QString &value, bool focus)
{
    auto *r = new ModifierRow(key, value, keys, [this](ModifierRow *x) { _remove_row(x); });
    rows_lay->insertWidget(int(rows.size()), r);
    rows.push_back(r);
    if (focus) {
        r->key->setFocus();
        r->key->completer()->complete();
    }
}

void MapDefsDialog::_remove_row(ModifierRow *row)
{
    auto it = std::find(rows.begin(), rows.end(), row);
    if (it != rows.end()) {
        rows.erase(it);
        row->setParent(nullptr);
        row->deleteLater();
    }
}

mapfiles::Mods MapDefsDialog::_rows() const
{
    mapfiles::Mods out;
    for (ModifierRow *r : rows) {
        auto p = r->pair();
        if (!p.first.isEmpty() || !p.second.isEmpty())
            out.push_back(p);
    }
    return out;
}

void MapDefsDialog::_commit()
{
    if (current)
        work.insert(*current, _rows());
}

// ------------------------------------------------------------------ new / remove
void MapDefsDialog::_new()
{
    const std::optional<QString> base = current;
    const mapfiles::Mods *bw = base ? work.get(*base) : nullptr;
    const QString hint = base && !base->isEmpty() && bw && !bw->empty()
                             ? u" It starts with a copy of %1's modifiers."_s.arg(pretty(*base))
                             : QString();
    auto [typed, ok] = get_text(this, u"New %1"_s.arg(kind),
                                u"Key for the new %1 (lowercase_with_underscores).%2"_s.arg(kind, hint));
    if (!ok)
        return;
    const QString key = py::strip(typed);
    const QString problem = !KEY_RE.match(key).hasMatch()
                                ? u"“%1”: use letters, digits and underscores only."_s.arg(key)
                            : names.contains(key) ? u"%1 already exists."_s.arg(key)
                            : removed.contains(key)
                                ? u"%1 was just removed — Cancel this window to keep it instead."_s.arg(key)
                                : QString();
    show_error(problem.isEmpty() ? QString() : u"⚠ New %1: %2"_s.arg(kind, problem));
    if (!problem.isEmpty())
        return;
    _commit();
    names.append(key);
    new_names.append(key);
    work.insert(key, base && !base->isEmpty() ? work.value(*base) : mapfiles::Mods{});
    _fill_list();
    list->setCurrentRow(int(names.size()) - 1);
}

void MapDefsDialog::_remove()
{
    if (!current || mf.is_vanilla(*current))
        return;
    const QString n = *current;
    _commit();
    const long long cnt = _count(n);
    QStringList targets;
    for (const QString &t : names)
        if (t != n)
            targets << t;
    bool ok;
    std::optional<QString> target;
    if (new_names.contains(n) && !cnt) {
        ok = true;   // made in this dialog: nothing on disk to worry about
    } else {
        if (cnt && targets.isEmpty()) {
            show_error(u"⚠ %1 can't be removed: there is no other %2 to move its provinces to."_s.arg(pretty(n), kind));
            return;
        }
        const QStringList refs = !new_names.contains(n) ? find_references(mf.mod, n) : QStringList();
        RemoveDialog dlg(kind, n, cnt, targets, refs, this);
        ok = run_remove(dlg);
        target = dlg.chosen();
    }
    if (!ok)
        return;
    const int row = int(names.indexOf(n));
    names.removeOne(n);
    work.remove(n);
    current.reset();
    if (new_names.contains(n)) {
        new_names.removeOne(n);
        for (auto &[r, t] : moves)   // something was moved into it: send it on
            if (t == n)
                t = target;
    } else {
        removed.append(n);
        moves.insert(n, target);
    }
    _fill_list();
    list->setCurrentRow(std::min(row, int(names.size()) - 1));
}

// ------------------------------------------------------------------ result
void MapDefsDialog::_ok()
{
    _commit();
    for (const QString &n : names) {
        QSet<QString> seen;
        for (const auto &[k, v] : work.value(n)) {
            const QString where = u"%1: "_s.arg(pretty(n));
            if (!KEY_RE.match(k).hasMatch())
                return _bad(n, where + u"“%1” is not a valid modifier key."_s.arg(k));
            if (v.isEmpty() || !value_ok(v))
                return _bad(n, where + u"%1 needs a value (e.g. 0.1)."_s.arg(k));
            if (seen.contains(k))
                return _bad(n, where + u"%1 is listed twice."_s.arg(k));
            seen.insert(k);
        }
    }
    accept();
}

void MapDefsDialog::_bad(const QString &name, const QString &msg)
{
    list->setCurrentRow(int(names.indexOf(name)));
    show_error(u"⚠ "_s + msg);
}

void MapDefsDialog::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

std::tuple<OrderedMap<QString, mapfiles::Mods>, QStringList, QStringList, OrderedMap<QString, QString>>
MapDefsDialog::result_changes() const
{
    OrderedMap<QString, mapfiles::Mods> mods;
    for (const auto &[n, lst] : work) {
        const mapfiles::Mods *was = mf.mods.get(n);
        if (names.contains(n) && (new_names.contains(n) || lst != (was ? *was : mapfiles::Mods{})))
            mods.insert(n, lst);
    }
    OrderedMap<QString, QString> mv;
    for (const auto &[r, t] : moves)
        mv.insert(r, t.value_or(QString()));
    return {mods, new_names, removed, mv};
}

}  // namespace mapdefs
