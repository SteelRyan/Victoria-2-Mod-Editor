// statepanel.py — Provinces & states → States.
#include "ui/statepanel.h"
#include "core/localisation.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/dialogs.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace statepanel {

const QString WARN = u"#e8a33d"_s;

namespace {

localisation::LocEdits *loc_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
}

scriptstore::ScriptStore *scripts_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
}

}  // namespace

StatePanel::StatePanel(moddata::Mod &mod_, const mapview::MapData *md_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_), handler(*this)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new Section();
    s->lay->addWidget(label(u"States"_s, u"title"_s));
    src = label(QString(), u"faint"_s);
    src->setTextFormat(Qt::RichText);
    s->lay->addWidget(src);
    filter = new QLineEdit();
    filter->setPlaceholderText(u"Filter: key, name or owner"_s);
    connect(filter, &QLineEdit::textChanged, this, [this](const QString &) { _fill_tree(true); });
    s->lay->addWidget(filter);
    only_odd = new QCheckBox(u"Only states to look at (empty, several owners)"_s);
    connect(only_odd, &QCheckBox::toggled, this, [this](bool) { _fill_tree(true); });
    s->lay->addWidget(only_odd);
    tree = new QTreeWidget();
    tree->setColumnCount(4);
    tree->setHeaderLabels({u"State"_s, u"Name"_s, u"Owner"_s, u"Prov."_s});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSortingEnabled(true);
    QHeaderView *h = tree->header();
    h->setStretchLastSection(false);
    {
        int col = 0;
        for (auto m : {QHeaderView::ResizeToContents, QHeaderView::Stretch, QHeaderView::Stretch,
                       QHeaderView::ResizeToContents})
            h->setSectionResizeMode(col++, m);
    }
    tree->setTextElideMode(Qt::ElideRight);
    tree->setMinimumHeight(260);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &StatePanel::_picked);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { _zoom(); });
    s->lay->addWidget(tree);
    count = label(QString(), u"faint"_s);
    s->lay->addWidget(count);
    lay->addWidget(s);

    // --- the state
    s = ed = new Section();
    ed_title = label(u"No state selected"_s, u"title"_s);
    s->lay->addWidget(ed_title);
    ed_note = label(QString(), u"warnAmber"_s);
    ed_note->setTextFormat(Qt::RichText);
    s->lay->addWidget(ed_note);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    name = new QLineEdit();
    name->setPlaceholderText(u"Name of the state"_s);
    connect(name, &QLineEdit::editingFinished, this, &StatePanel::_name_done);
    grid->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    grid->addWidget(name, 0, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    name_where = label(QString(), u"faint"_s);
    s->lay->addWidget(name_where);
    s->lay->addWidget(label(u"Provinces (★ = capital, the first in region.txt)"_s));
    members = new QTreeWidget();
    members->setColumnCount(3);
    members->setHeaderLabels({u"Province"_s, u"Owner"_s, QString()});
    members->setRootIsDecorated(false);
    QHeaderView *mh = members->header();
    mh->setStretchLastSection(false);
    {
        int col = 0;
        for (auto m : {QHeaderView::Stretch, QHeaderView::ResizeToContents, QHeaderView::ResizeToContents})
            mh->setSectionResizeMode(col++, m);
    }
    members->setTextElideMode(Qt::ElideRight);
    members->setMinimumHeight(140);
    connect(members, &QTreeWidget::itemSelectionChanged, this, &StatePanel::_member_picked);
    connect(members, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *it, int) { Q_EMIT zoomRequested(it->data(0, Qt::UserRole).toLongLong()); });
    s->lay->addWidget(members);
    auto *row = new QHBoxLayout();
    cap_btn = new QPushButton(u"★ Make capital"_s);
    cap_btn->setToolTip(u"The picked province becomes the state's capital (first in map/region.txt)"_s);
    connect(cap_btn, &QPushButton::clicked, this, &StatePanel::_make_capital);
    add_btn = new QPushButton(u"＋ Add provinces — click them on the map"_s);
    add_btn->setCheckable(true);
    add_btn->setToolTip(u"While on: every province you click on the map moves into this state "
                        u"(from the state it was in). Esc or a second click here stops."_s);
    connect(add_btn, &QPushButton::toggled, this, &StatePanel::_add_toggled);
    row->addWidget(cap_btn);
    row->addWidget(add_btn, 1);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- delete
    s = del_sec = new Section();
    s->lay->addWidget(label(u"Delete this state"_s));
    del_info = label(QString(), u"sub"_s);
    del_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(del_info);
    target = new dialogs::PickCombo({});
    s->lay->addWidget(target);
    refs_info = label(QString(), u"sub"_s);
    refs_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(refs_info);
    refs = new QTreeWidget();
    refs->setColumnCount(2);
    refs->setHeaderLabels({u"Script"_s, u"Line"_s});
    refs->setRootIsDecorated(false);
    QHeaderView *rh = refs->header();
    rh->setStretchLastSection(true);
    rh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    refs->setTextElideMode(Qt::ElideRight);
    refs->setMaximumHeight(170);
    connect(refs, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        Q_EMIT scriptRequested(it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 1).toLongLong());
    });
    s->lay->addWidget(refs);
    fix_refs = new QCheckBox(u"In those scripts, write the state's provinces instead of its key"_s);
    fix_refs->setChecked(true);
    fix_refs->setToolTip(u"A scope  KEY = { … }  becomes one block per province ( 220 = { … } );\n"
                         u"region = KEY  becomes  province_id = 220  (several: OR = { province_id = … }).\n"
                         u"The provinces are the ones the state has now — the same the key meant."_s);
    s->lay->addWidget(fix_refs);
    del_btn = new QPushButton(u"✕  Delete state"_s);
    del_btn->setObjectName(u"danger"_s);
    connect(del_btn, &QPushButton::clicked, this, &StatePanel::_delete);
    s->lay->addWidget(del_btn);
    del_warn = label(QString(), u"warnAmber"_s);
    s->lay->addWidget(del_warn);
    lay->addWidget(s);
    lay->addStretch(1);
}

// ---------------------------------------------------------------------------------------------- data
QString StatePanel::state_name(const QString &r) const
{
    localisation::LocEdits *loc = loc_of(doc);
    const OptStr t = loc ? loc->text(r) : std::nullopt;
    if (t && !t->isEmpty())
        return *t;
    return mod.loc.value(r);
}

QStringList StatePanel::owners(const QString &r) const
{
    QStringList out;
    for (long long p : mod.regions.get(r)->provinces) {
        const OptStr &o = doc->prov_state(p).owner;
        if (o && !o->isEmpty() && !out.contains(*o))
            out << *o;
    }
    return out;
}

bool StatePanel::odd(const QString &r) const
{
    return mod.regions.get(r)->provinces.empty() || owners(r).size() > 1;
}

// ---------------------------------------------------------------------------------------------- refresh
void StatePanel::refresh(document::Document &d, const std::vector<long long> &selection)
{
    doc = &d;
    if (!selection.empty()) {
        const moddata::Region *cur = rid ? mod.regions.get(*rid) : nullptr;
        if (!cur || std::find(cur->provinces.begin(), cur->provinces.end(), selection[0]) == cur->provinces.end()) {
            const moddata::Province *pr = mod.provinces.get(selection[0]);
            const OptStr r = pr ? pr->region : std::nullopt;
            if (r && !r->isEmpty() && !add_btn->isChecked())
                rid = r;
        }
    }
    if (rid && !mod.regions.contains(*rid))
        rid.reset();
    const QString path = mod.p({u"map"_s, u"region.txt"_s});
    src->setText(u"map/region.txt — %1. Applies to every start date."_s.arg(
        mod.in_mod(path) ? u"the mod's own file"_s
                         : u"<b>the base game's file</b>: the first change Apply writes makes the mod's own copy "
                           u"(the base game is never touched)"_s));
    _fill_tree();
    _fill_state();
}

void StatePanel::_fill_tree(bool force)
{
    localisation::LocEdits *loc = loc_of(doc);
    const auto sig = std::make_tuple(doc->region_version, mod.regions.size(), filter->text(), only_odd->isChecked(),
                                     loc ? loc->fresh.size() : qsizetype(0), doc->prov_want.size());
    if (!force && _sig == sig) {
        _select_row();
        return;
    }
    _sig = sig;
    const QString f = py::lower(py::strip(filter->text()));
    tree->setSortingEnabled(false);
    tree->blockSignals(true);
    tree->clear();
    long long n = 0;
    std::map<QString, QString> names;
    for (const auto &[t, c] : mod.countries)
        names[t] = c.name;
    auto name_of = [&names](const QString &t) {
        const auto it = names.find(t);
        return it == names.end() ? t : it->second;
    };
    for (const auto &[r, reg] : mod.regions) {
        const QStringList own = owners(r);
        const QString nm = state_name(r);
        QStringList ot;
        for (const QString &o : own)
            ot << u"%1 (%2)"_s.arg(name_of(o), o);
        const QString otext = ot.isEmpty() ? u"—"_s : ot.join(u", "_s);
        if (!f.isEmpty() && !py::lower(r).contains(f) && !py::lower(nm).contains(f) && !py::lower(otext).contains(f))
            continue;
        if (only_odd->isChecked() && !odd(r))
            continue;
        auto *it = new QTreeWidgetItem(QStringList{r, nm, otext, QString()});
        it->setData(3, Qt::DisplayRole, static_cast<int>(reg.provinces.size()));
        it->setData(0, Qt::UserRole, r);
        if (reg.provinces.empty()) {
            it->setToolTip(0, u"Empty — it is gone from region.txt on Apply"_s);
            for (int c = 0; c < 4; ++c)
                it->setForeground(c, QBrush(QColor(theme::INK_FAINT)));
        } else if (own.size() > 1) {
            it->setToolTip(2, u"Several owners: in the game a state with several owners is split per owner"_s);
            it->setForeground(2, QBrush(QColor(WARN)));
        }
        tree->addTopLevelItem(it);
        ++n;
    }
    tree->blockSignals(false);
    tree->setSortingEnabled(true);
    tree->sortItems(0, Qt::AscendingOrder);
    count->setText(u"%1 of %2 states shown"_s.arg(n).arg(mod.regions.size()));
    _select_row();
}

void StatePanel::_select_row()
{
    tree->blockSignals(true);
    bool found = false;
    for (int i = 0; i < tree->topLevelItemCount() && !found; ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        if (rid && it->data(0, Qt::UserRole).toString() == *rid) {
            tree->setCurrentItem(it);
            tree->scrollToItem(it);
            found = true;
        }
    }
    if (!found)
        tree->clearSelection();
    tree->blockSignals(false);
}

void StatePanel::_picked()
{
    QTreeWidgetItem *it = tree->currentItem();
    if (!it)
        return;
    rid = it->data(0, Qt::UserRole).toString();
    _fill_state();
    const moddata::Region *r = mod.regions.get(*rid);
    if (r && !r->provinces.empty())
        Q_EMIT selectRequested(QList<long long>(r->provinces.begin(), r->provinces.end()));
}

void StatePanel::_zoom()
{
    const moddata::Region *r = rid ? mod.regions.get(*rid) : nullptr;
    if (r && !r->provinces.empty())
        Q_EMIT zoomRequested(r->provinces[0]);
}

void StatePanel::_fill_state()
{
    const moddata::Region *r = rid && !rid->isEmpty() ? mod.regions.get(*rid) : nullptr;
    ed->setEnabled(r != nullptr);
    del_sec->setEnabled(r != nullptr);
    members->blockSignals(true);
    members->clear();
    members->blockSignals(false);
    refs->clear();
    if (!r) {
        ed_title->setText(u"No state selected"_s);
        ed_note->setText(QString());
        name->setText(QString());
        name_where->setText(u"Pick a state in the list, or click a province on the map."_s);
        del_info->setText(QString());
        refs_info->setText(QString());
        del_warn->setText(QString());
        return;
    }
    const QString &k0 = *rid;
    const QString nm = state_name(k0);
    ed_title->setText(u"%1%2"_s.arg(k0, !nm.isEmpty() ? u" — "_s + nm : QString()));
    const QStringList own = owners(k0);
    QStringList notes;
    if (r->provinces.empty())
        notes << u"⚠ Empty: it is gone from region.txt on Apply."_s;
    if (own.size() > 1)
        notes << u"⚠ Several owners (%1): in the game a state with several owners is split per owner."_s.arg(
            own.join(u", "_s));
    ed_note->setText(notes.join(u"<br>"_s));
    if (!name->hasFocus())
        name->setText(nm);
    localisation::LocEdits *loc = loc_of(doc);
    name_where->setText(u"Its name: %1 in the localisation%2"_s.arg(
        k0, loc ? u" — written to %1 on Apply"_s.arg(loc->where(k0)) : QString()));
    const std::optional<long long> cap = !r->provinces.empty() ? std::optional<long long>(r->provinces[0]) : std::nullopt;
    std::map<QString, QString> names;
    for (const auto &[t, c] : mod.countries)
        names[t] = c.name;
    auto name_of = [&names](const QString &t) {
        const auto it = names.find(t);
        return it == names.end() ? t : it->second;
    };
    for (long long p : r->provinces) {
        const moddata::Province *pr = mod.provinces.get(p);
        const OptStr o = doc->prov_state(p).owner;
        auto *it = new QTreeWidgetItem(QStringList{
            u"%1#%2 %3"_s.arg(p == cap ? u"★ "_s : QString()).arg(p).arg(pr ? pr->name : u"?"_s),
            o ? name_of(*o) : u"—"_s, QString()});
        if (o && o->isEmpty())
            it->setText(1, u"—"_s);
        it->setData(0, Qt::UserRole, p);
        if (p == cap)
            it->setForeground(0, QBrush(QColor(theme::GOLD)));
        members->addTopLevelItem(it);
    }
    // the province picked in the list stays picked (picking it selects it on the map, which refreshes the page)
    cap_btn->setEnabled(false);
    for (int i = 0; i < members->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = members->topLevelItem(i);
        if (_member && it->data(0, Qt::UserRole).toLongLong() == *_member) {
            members->blockSignals(true);
            members->setCurrentItem(it);
            members->blockSignals(false);
            cap_btn->setEnabled(_member != cap);
            break;
        }
    }
    // delete: the target and what names the state
    QStringList keys;
    for (const auto &[k, _rr] : mod.regions)
        keys << k;
    std::sort(keys.begin(), keys.end());
    std::vector<dialogs::PickItem> items;
    for (const QString &k : keys) {
        const moddata::Region &rr = *mod.regions.get(k);
        if (k == k0 || rr.provinces.empty())
            continue;
        const QStringList o = owners(k);
        const QString sn = state_name(k);
        items.push_back({k, u"%1 — %2 · %3 prov"_s.arg(k, sn.isEmpty() ? u"?"_s : sn).arg(rr.provinces.size()),
                         std::nullopt,
                         !o.isEmpty() ? OptStr(u"%1 (%2)"_s.arg(name_of(o[0]), o[0])) : OptStr(u"No owner"_s)});
    }
    const QString mine = !own.isEmpty() ? u"%1 (%2)"_s.arg(name_of(own[0]), own[0]) : QString();
    std::stable_sort(items.begin(), items.end(), [&mine](const dialogs::PickItem &a, const dialogs::PickItem &b) {
        const bool oa = *a.group != mine, ob = *b.group != mine;
        if (oa != ob)
            return !oa;
        if (*a.group != *b.group)
            return *a.group < *b.group;
        return a.key < b.key;
    });
    const dialogs::Data cur = target->current();
    target->fill(items);
    if (cur && target->find(cur) >= 0)
        target->setCurrentIndex(target->find(cur));
    target->setVisible(!r->provinces.empty());
    del_info->setText(!r->provinces.empty()
                          ? u"Its %1 province(s) join the state picked here (states of the same owner first):"_s.arg(
                                r->provinces.size())
                          : u"It has no provinces — deleting takes its entry out of region.txt on Apply."_s);
    const auto rf = references();
    std::map<QString, long long> kinds;
    for (const auto &x : rf) {
        kinds[x.kind] += 1;
        auto *it = new QTreeWidgetItem(QStringList{x.rel, u"%1: %2"_s.arg(x.line).arg(x.text)});
        it->setData(0, Qt::UserRole, x.rel);
        it->setData(0, Qt::UserRole + 1, static_cast<long long>(x.pos));
        it->setToolTip(1, x.text);
        if (x.kind == u"other")
            for (int c = 0; c < 2; ++c)
                it->setForeground(c, QBrush(QColor(WARN)));
        refs->addTopLevelItem(it);
    }
    refs->setVisible(!rf.empty());
    fix_refs->setVisible(!rf.empty() && !former(k0).empty() && (kinds[u"scope"_s] || kinds[u"region"_s]));
    if (!rf.empty())
        refs_info->setText(u"<span style='color:%1'>⚠ %2 place(s) in the scripts name %3</span> — %4 as a "
                           u"scope, %5 as <i>region =</i>%6. Double-click one to open it."_s.arg(WARN)
                               .arg(rf.size())
                               .arg(k0)
                               .arg(kinds[u"scope"_s])
                               .arg(kinds[u"region"_s])
                               .arg(kinds[u"other"_s] ? u", %1 otherwise (left as they are — check them)"_s.arg(
                                                            kinds[u"other"_s])
                                                      : QString()));
    else
        refs_info->setText(u"<span style='color:%1'>✔</span> No script names %2."_s.arg(theme::OK, k0));
    del_warn->setText(QString());
}

std::vector<long long> StatePanel::former(const QString &r) const
{
    // The provinces its key stands for: the ones it has — or, emptied in this session (Sam: DC moved into Maryland),
    // the ones it had in region.txt.
    const moddata::Region *reg = mod.regions.get(r);
    if (reg && !reg->provinces.empty())
        return reg->provinces;
    const auto *o = doc->region_orig.get(r);
    return o ? *o : std::vector<long long>{};
}

std::vector<staterefs::Place> StatePanel::references()
{
    scriptstore::ScriptStore *st = scripts_of(doc);
    if (!st || !rid || rid->isEmpty())
        return {};
    const QString live = st->live_sig();
    if (!_refs || _refs->rid != *rid || _refs->version != doc->region_version || _refs->live != live)
        _refs = Refs{*rid, doc->region_version, live, staterefs::find(mod, *st, *rid)};
    return _refs->refs;
}

// ---------------------------------------------------------------------------------------------- edits
void StatePanel::_name_done()
{
    if (!rid || rid->isEmpty())
        return;
    const QString text = name->text().simplified();
    if (!text.isEmpty() && text != state_name(*rid))
        Q_EMIT nameRequested(*rid, text, u"state"_s);
}

void StatePanel::_member_picked()
{
    QTreeWidgetItem *it = members->currentItem();
    const moddata::Region *r = rid ? mod.regions.get(*rid) : nullptr;
    if (!it || !r) {
        cap_btn->setEnabled(false);
        return;
    }
    const long long pid = it->data(0, Qt::UserRole).toLongLong();
    _member = pid;
    cap_btn->setEnabled(!r->provinces.empty() && pid != r->provinces[0]);
    Q_EMIT selectRequested({pid});
}

void StatePanel::_make_capital()
{
    QTreeWidgetItem *it = members->currentItem();
    if (!it || !rid || rid->isEmpty())
        return;
    const long long pid = it->data(0, Qt::UserRole).toLongLong();
    doc->set_capital(*rid, pid);
    Q_EMIT message(u"#%1 is now the capital of %2. Ctrl+Z undoes it."_s.arg(pid).arg(*rid));
}

void StatePanel::_add_toggled(bool on)
{
    Q_EMIT addModeChanged(on);
    if (on)
        Q_EMIT message(u"Click provinces on the map: each moves into %1. Esc stops."_s.arg(rid.value_or(u"None"_s)));
}

void StatePanel::add_province(long long pid)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    if (!rid || rid->isEmpty() || !pr || pr->sea)
        return;
    const auto [ok, msg] = doc->move_to_state({pid}, *rid);
    Q_EMIT message((ok ? QString() : u"⚠ "_s) + msg + (ok ? u" Ctrl+Z undoes it."_s : QString()));
}

void StatePanel::_delete()
{
    const OptStr r0 = rid;
    const moddata::Region *r = r0 && !r0->isEmpty() ? mod.regions.get(*r0) : nullptr;
    if (!r)
        return;
    const QString k0 = *r0;
    const OptStr tgt = !r->provinces.empty() ? target->current() : std::nullopt;
    if (!r->provinces.empty() && !(tgt && mod.regions.contains(*tgt))) {
        del_warn->setText(u"⚠ Pick the state its provinces should join first."_s);
        return;
    }
    const std::vector<long long> provinces = former(k0);
    const auto rf = references();
    const bool fix = fix_refs->isVisible() && fix_refs->isChecked();
    scriptstore::ScriptStore *st = scripts_of(doc);
    OrderedMap<QString, QString> texts;
    if (fix && !provinces.empty()) {
        std::set<QString> rels;
        for (const auto &x : rf)
            if (x.kind == u"scope" || x.kind == u"region")
                rels.insert(x.rel);
        for (const QString &rel : rels) {
            const auto [fresh, n] = staterefs::fixed_text(st->text(rel), k0, provinces);
            if (n)
                texts.insert(rel, fresh);
        }
    }
    std::function<void()> extra;
    if (!texts.isEmpty())
        extra = [st, texts] {
            for (const auto &[k, v] : texts)
                st->set_text(k, v);
        };
    auto [ok, msg] = doc->delete_state(k0, tgt, extra, !texts.isEmpty() ? QStringList{u"scripts"_s} : QStringList{});
    if (ok && !texts.isEmpty()) {
        QStringList ps;
        for (size_t i = 0; i < provinces.size() && i < 6; ++i)
            ps << u"#%1"_s.arg(provinces[i]);
        msg += u" In %1 script file(s) its key was replaced by its provinces (%2)."_s.arg(texts.size()).arg(
            ps.join(u", "_s) + (provinces.size() > 6 ? u" …"_s : QString()));
    }
    long long left = 0;
    for (const auto &x : rf)
        left += (x.kind == u"other" || (!fix && x.kind != u"other")) ? 1 : 0;
    if (ok && left)
        msg += u" ⚠ %1 place(s) still name %2 — check them in Scripts."_s.arg(left).arg(k0);
    Q_EMIT message((ok ? QString() : u"⚠ "_s) + msg + (ok ? u" Ctrl+Z undoes it."_s : QString()));
    if (ok)
        rid = tgt;
}

// ---------------------------------------------------------------------------------------------- the map handler
bool AddHandler::cancel()
{
    if (p.add_btn->isChecked()) {
        p.add_btn->setChecked(false);
        return true;
    }
    return false;
}

bool AddHandler::press(double x, double y, double, Qt::KeyboardModifiers)
{
    const long long pid = p.md->province_at(double(int(x)), double(int(y)));
    const moddata::Province *pr = pid ? p.mod.provinces.get(pid) : nullptr;
    if (pr && !pr->sea)
        p.add_province(pid);
    return true;
}

}  // namespace statepanel
