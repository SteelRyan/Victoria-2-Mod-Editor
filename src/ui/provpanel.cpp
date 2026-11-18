// provpanel.py — Provinces tab: owner / controller / cores, life rating, RGO, colonial, forts / naval bases / railroads
// and the state's factories (max 8 per state).
#include "ui/provpanel.h"
#include "core/adjacency.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/pytext.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::fmt;
using widgets::label;
using widgets::Section;
using widgets::swatch_icon;

namespace provpanel {

static const QStringList LEVEL_THREE = {u"fort"_s, u"naval_base"_s, u"railroad"_s};

const QString MIXED = u"__mixed__"_s;

namespace {

const widgets::Rgb GREY{128, 128, 128};

QString tag_list(const QList<long long> &pids, qsizetype n)   // ", #".join(str(p) for p in pids[:n])
{
    QStringList out;
    for (qsizetype i = 0; i < pids.size() && i < n; ++i)
        out << QString::number(pids[i]);
    return out.join(u", #"_s);
}

void clear_grid(QGridLayout *g)
{
    while (g->count()) {
        QLayoutItem *item = g->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

QString signed_int(long long d) { return d >= 0 ? u"+%1"_s.arg(d) : QString::number(d); }   // "%+d"

}  // namespace

OrderedMap<OptStr, long long> state_owners(document::Document &doc, const QString &rid)
{
    OrderedMap<OptStr, long long> out;
    for (long long p : doc.mod.regions.get(rid)->provinces)
        out[doc.prov_state(p).owner] += 1;
    return out;
}

std::vector<dialogs::PickItem> country_items(const moddata::Mod &mod)
{
    std::vector<const moddata::Country *> cs;
    for (const auto &[tag, c] : mod.countries)
        cs.push_back(&c);
    std::stable_sort(cs.begin(), cs.end(), [](const moddata::Country *a, const moddata::Country *b) {
        const QString la = py::lower(a->name), lb = py::lower(b->name);
        return a->dynamic != b->dynamic ? b->dynamic : la < lb;
    });
    std::vector<dialogs::PickItem> items;
    for (const moddata::Country *c : cs)
        items.push_back({c->tag, u"%1  (%2)"_s.arg(c->name, c->tag), c->color,
                         c->dynamic ? OptStr(u"Dynamic tags (rarely used)"_s) : std::nullopt});
    return items;
}

std::vector<dialogs::PickItem> goods_items(moddata::Mod &mod)
{
    // RGO choices: only raw goods produced by RGO production types (production_types.txt), grouped as Farms / Mines.
    // Falls back to all goods if the mod defines none.
    const OrderedMap<QString, QString> rgo = mod.rgo_goods();
    std::vector<dialogs::PickItem> items;
    if (rgo.isEmpty()) {
        std::vector<const moddata::Good *> gs;
        for (const auto &[k, g] : mod.goods)
            gs.push_back(&g);
        std::stable_sort(gs.begin(), gs.end(), [](const moddata::Good *a, const moddata::Good *b) { return a->id < b->id; });
        for (const moddata::Good *g : gs)
            items.push_back({g->id, u"%1  (%2)"_s.arg(g->name, g->id), g->color, std::nullopt});
        return items;
    }
    auto name_of = [&mod](const QString &k) {
        const moddata::Good *g = mod.goods.get(k);
        return g ? g->name : k;
    };
    std::vector<std::pair<QString, QString>> kv(rgo.begin(), rgo.end());
    std::stable_sort(kv.begin(), kv.end(), [&](const auto &a, const auto &b) {
        if (a.second != b.second)
            return a.second < b.second;
        return py::lower(name_of(a.first)) < py::lower(name_of(b.first));
    });
    for (const auto &[k, grp] : kv) {
        const moddata::Good *g = mod.goods.get(k);
        items.push_back({k, u"%1  (%2)"_s.arg(name_of(k), k), g ? g->color : GREY, grp});
    }
    return items;
}

ProvincePanel::ProvincePanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- header
    auto *s = new Section();
    title = label(u"Nothing selected"_s, u"title"_s);
    sub = label(u"Click a province on the map. Ctrl+click adds more."_s, u"sub"_s);
    sub->setTextFormat(Qt::RichText);
    note = label(QString(), u"faint"_s);
    s->lay->addWidget(title);
    s->lay->addWidget(sub);
    // names (localisation — every start date; written on Apply)
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    name_edit = new QLineEdit();
    name_edit->setPlaceholderText(u"Name of the province"_s);
    name_edit->setToolTip(u"The province's name in the game (PROV<id> in the localisation). Enter to apply."_s);
    connect(name_edit, &QLineEdit::editingFinished, this, [this] { _name_done(name_edit, u"prov"_s); });
    state_name_edit = new QLineEdit();
    state_name_edit->setPlaceholderText(u"Name of the state"_s);
    state_name_edit->setToolTip(u"The state's name in the game (its region.txt key in the localisation). "
                                u"Enter to apply."_s);
    connect(state_name_edit, &QLineEdit::editingFinished, this, [this] { _name_done(state_name_edit, u"state"_s); });
    grid->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    grid->addWidget(name_edit, 0, 1);
    grid->addWidget(label(u"State name"_s, u"sub"_s, false), 1, 0);
    grid->addWidget(state_name_edit, 1, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    name_where = label(QString(), u"faint"_s);
    s->lay->addWidget(name_where);
    // state membership: one compact line + a menu button
    auto *row = new QHBoxLayout();
    state_line = label(QString(), u"sub"_s);
    state_line->setTextFormat(Qt::RichText);
    row->addWidget(state_line, 1);
    move_btn = new QPushButton(u"Move to state"_s);
    move_btn->setToolTip(u"Change which state (region.txt) the selected province(s) belong to.\n"
                         u"Saved to map/region.txt — applies to every start date."_s);
    auto *menu = new QMenu(move_btn);
    menu->addAction(u"Pick target state on the map"_s, this, [this] { Q_EMIT stateMoveRequested(u"map"_s); });
    menu->addAction(u"Choose from list…"_s, this, [this] { Q_EMIT stateMoveRequested(u"list"_s); });
    menu->addSeparator();
    menu->addAction(u"New state from selection…"_s, this, [this] { Q_EMIT stateMoveRequested(u"new"_s); });
    menu->addSeparator();
    menu->addAction(u"✕ Delete this state — its provinces join a state picked on the map"_s, this,
                    [this] { Q_EMIT stateMoveRequested(u"delete_map"_s); });
    menu->addAction(u"✕ Delete this state — its provinces join a state from the list…"_s, this,
                    [this] { Q_EMIT stateMoveRequested(u"delete_list"_s); });
    move_btn->setMenu(menu);
    cap_btn = new QPushButton(u"★ Make state capital"_s);
    cap_btn->setToolTip(u"Make this province the capital of its state (first entry in map/region.txt)."_s);
    connect(cap_btn, &QPushButton::clicked, this, [this] { Q_EMIT capitalRequested(); });
    row->addWidget(cap_btn);
    row->addWidget(move_btn);
    s->lay->addLayout(row);
    cap_pops = new QCheckBox(u"With the new capital, move the state's elite pops there too"_s);
    cap_pops->setToolTip(u"Elite pop types as set in Population; only from provinces with the same owner."_s);
    cap_pops->setChecked(true);
    s->lay->addWidget(cap_pops);
    state_warn = label(QString(), u"warnAmber"_s);
    state_warn->setTextFormat(Qt::RichText);
    state_warn->hide();
    s->lay->addWidget(state_warn);
    s->lay->addWidget(note);
    lay->addWidget(s);

    // --- ownership
    s = own_section = new Section();
    s->lay->addWidget(label(u"Ownership"_s));
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    owner = new dialogs::PickCombo(country_items(mod), {{MIXED, u"(mixed)"_s}});
    controller = new dialogs::PickCombo(country_items(mod), {{MIXED, u"(mixed)"_s}});
    for (dialogs::PickCombo *cb : {owner, controller})
        cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(owner, &QComboBox::activated, this, [this](int) { _set_owner(); });
    connect(controller, &QComboBox::activated, this,
            [this](int) { _set_scalar(u"controller"_s, controller->key(), u"Controller"_s); });
    ctrl_follow = new QCheckBox(u"Controller follows owner"_s);
    ctrl_follow->setChecked(true);
    owner_core = new QCheckBox(u"New owner gets a core"_s);
    owner_core->setChecked(true);
    owner_core->setToolTip(u"Giving provinces to a nation also makes them its cores (add_core) — untick it "
                           u"for a conquest that isn't a core"_s);
    grid->addWidget(label(u"Owner"_s, u"sub"_s, false), 0, 0);
    grid->addWidget(owner, 0, 1);
    grid->addWidget(label(u"Controller"_s, u"sub"_s, false), 1, 0);
    grid->addWidget(controller, 1, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    row->addWidget(ctrl_follow);
    row->addWidget(owner_core);
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(label(u"Cores"_s, u"sub"_s));
    cores_host = new QWidget();
    cores_lay = new QGridLayout(cores_host);
    cores_lay->setContentsMargins(0, 0, 0, 0);
    cores_lay->setSpacing(4);
    s->lay->addWidget(cores_host);
    row = new QHBoxLayout();
    add_core = new dialogs::PickCombo(country_items(mod), {{std::nullopt, u"＋ Add core…"_s}});
    add_core->setCurrentIndex(0);
    add_core->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(add_core, &QComboBox::activated, this, [this](int) { _add_core(add_core->key()); });
    auto *own_core = new QPushButton(u"Add owner as core"_s);
    connect(own_core, &QPushButton::clicked, this, [this] { _add_core(std::nullopt, true); });
    row->addWidget(add_core, 1);
    row->addWidget(own_core);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- land
    s = new Section();
    s->lay->addWidget(label(u"Land"_s));
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    lr_value = label(u"—"_s, u"title"_s);
    lr_value->setStyleSheet(u"font-size: 14px;"_s);
    auto *lr_row = new QHBoxLayout();
    lr_row->setSpacing(4);
    for (long long d : {-5LL, -1LL}) {
        auto *b = new QPushButton(signed_int(d));
        b->setObjectName(u"stepMinus"_s);
        b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, [this, d] { _lr(d); });
        lr_row->addWidget(b);
    }
    lr_row->addWidget(lr_value, 1, Qt::AlignCenter);
    for (long long d : {1LL, 5LL}) {
        auto *b = new QPushButton(signed_int(d));
        b->setObjectName(u"stepPlus"_s);
        b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, [this, d] { _lr(d); });
        lr_row->addWidget(b);
    }
    grid->addWidget(label(u"Life rating"_s, u"sub"_s, false), 0, 0);
    grid->addLayout(lr_row, 0, 1);
    rgo = new dialogs::PickCombo(goods_items(mod), {{MIXED, u"(mixed)"_s}});
    rgo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(rgo, &QComboBox::activated, this, [this](int) { _set_scalar(u"trade_goods"_s, rgo->key(), u"RGO"_s); });
    grid->addWidget(label(u"RGO"_s, u"sub"_s, false), 1, 0);
    grid->addWidget(rgo, 1, 1);
    colonial = new QComboBox();
    for (const auto &[v, t] : std::vector<std::pair<int, QString>>{
             {1, u"Protectorate  (level 1)"_s}, {2, u"Colony  (level 2)"_s}, {0, u"State"_s}})
        colonial->addItem(t, v);
    colonial->setPlaceholderText(u"differs — pick one to unify"_s);
    colonial->setToolTip(u"Protectorate / colony / state is set for the whole state:\n"
                         u"all its provinces with the same owner get the same status."_s);
    connect(colonial, &QComboBox::activated, this, [this](int) { _set_status(colonial->currentData()); });
    grid->addWidget(label(u"Status"_s, u"sub"_s, false), 2, 0);
    grid->addWidget(colonial, 2, 1);
    status_note = label(QString(), u"faint"_s);
    grid->addWidget(status_note, 3, 1);
    for (const auto &[r, kind] : std::vector<std::pair<int, QString>>{{4, u"climate"_s}, {5, u"continent"_s}}) {
        auto *cb = new QComboBox();
        cb->setPlaceholderText(u"differs — pick one to unify"_s);
        cb->setToolTip(u"map/%1.txt — applies to every start date"_s.arg(kind));
        connect(cb, &QComboBox::activated, this,
                [this, k = kind, cb](int) { Q_EMIT membershipRequested(k, cb->currentData().toString()); });
        grid->addWidget(label(py::capitalize(kind), u"sub"_s, false), r, 0);
        auto *mrow = new QHBoxLayout();
        mrow->setSpacing(6);
        mrow->addWidget(cb, 1);
        auto *eb = new QPushButton(u"Edit %1s…"_s.arg(kind));
        eb->setToolTip(u"View and edit what each %1 does (its modifiers), or add a new %1."_s.arg(kind));
        connect(eb, &QPushButton::clicked, this, [this, k = kind] { Q_EMIT definitionsRequested(k); });
        eb->setFixedWidth(150);
        mrow->addWidget(eb);
        grid->addLayout(mrow, r, 1);
        member_boxes.emplace_back(kind, cb);
    }
    member_warn = label(QString(), u"warn"_s);
    grid->addWidget(member_warn, 6, 1);
    island_cb = new QCheckBox(u"Has islands (its separate parts are meant to be)"_s);
    island_cb->setToolTip(u"The Errors tab then leaves this province's separate parts alone (stray pixels / "
                          u"several parts).\nWritten as set_province_flag = island in its history file, like "
                          u"the farm / mine flags — for every start date.\nThe Errors tab can remove all of "
                          u"these flags at once, to check every province again."_s);
    connect(island_cb, &QCheckBox::clicked, this,
            [this](bool) { Q_EMIT islandRequested(island_cb->checkState() == Qt::Checked, pids); });
    grid->addWidget(label(u"Islands"_s, u"sub"_s, false), 7, 0);
    grid->addWidget(island_cb, 7, 1);
    island_note = label(QString(), u"faint"_s);
    island_note->setTextFormat(Qt::RichText);
    grid->addWidget(island_note, 8, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    lay->addWidget(s);

    // --- province buildings
    s = new Section();
    s->lay->addWidget(label(u"Buildings"_s));
    grid = level_grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    for (const auto &[key, text] : std::vector<std::pair<QString, QString>>{
             {u"fort"_s, u"Fort"_s}, {u"naval_base"_s, u"Naval base"_s}, {u"railroad"_s, u"Railroad"_s}})
        _add_level_row(key, text);
    grid->setColumnStretch(4, 1);
    s->lay->addLayout(grid);
    lay->addWidget(s);

    // --- state factories
    s = fact_section = new Section();
    fact_title = label(u"Factories"_s, u"label"_s);
    s->lay->addWidget(fact_title);
    fact_host = new QWidget();
    fact_lay = new QGridLayout(fact_host);
    fact_lay->setContentsMargins(0, 0, 0, 0);
    fact_lay->setHorizontalSpacing(6);
    fact_lay->setVerticalSpacing(3);
    s->lay->addWidget(fact_host);
    add_fact = new dialogs::PickCombo({}, {{std::nullopt, u"＋ Add factory…"_s}});
    add_fact->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(add_fact, &QComboBox::activated, this, [this](int) { _add_factory(add_fact->key()); });
    s->lay->addWidget(add_fact);
    fact_warn = label(QString(), u"warn"_s);
    s->lay->addWidget(fact_warn);
    lay->addWidget(s);

    // --- later history (read-only)
    s = later_section = new Section();
    s->lay->addWidget(label(u"History entries in this province's files (read-only)"_s));
    later = label(QString(), u"faint"_s);
    later->setTextInteractionFlags(Qt::TextSelectableByMouse);
    s->lay->addWidget(later);
    lay->addWidget(s);
    lay->addStretch(1);
}

// ---------------------------------------------------------------------------------------------- helpers
std::vector<history::ProvState> ProvincePanel::_states() const
{
    std::vector<history::ProvState> out;
    for (long long p : pids)
        out.push_back(doc->prov_state(p));
    return out;
}

dialogs::Data ProvincePanel::_common(OptStr history::ProvState::*field) const
{
    std::set<OptStr> vals;
    for (long long p : pids)
        vals.insert(doc->prov_state(p).*field);
    return vals.size() == 1 ? *vals.begin() : dialogs::Data(MIXED);
}

void ProvincePanel::_emit(const QString &lbl, const ProvFn &fn)
{
    if (!pids.isEmpty() && doc && doc->history_editable())
        Q_EMIT edit(lbl, fn);
}

void ProvincePanel::_select(dialogs::PickCombo *combo, const dialogs::Data &key)
{
    const int i = combo->find(key);
    combo->setCurrentIndex(i >= 0 ? i : 0);
}

// ---------------------------------------------------------------------------------------------- edits
void ProvincePanel::_set_owner()
{
    const OptStr tag = owner->key();
    if (!tag || tag->isEmpty() || *tag == MIXED)
        return;
    const bool follow = ctrl_follow->isChecked();
    const bool core = owner_core->isChecked();
    const QString t = *tag;
    _emit(u"Owner → %1"_s.arg(t), [t, follow, core](long long, history::ProvState &st) {
        st.owner = t;
        if (follow)
            st.controller = t;
        if (core && !st.cores.contains(t))
            st.cores.append(t);
    });
}

QList<long long> ProvincePanel::status_group() const
{
    // Provinces that share a status with the selection: same state and same owner.
    QList<long long> out;
    for (long long pid : pids) {
        const OptStr &rid = mod.provinces.get(pid)->region;
        const OptStr own = doc->prov_state(pid).owner;
        std::vector<long long> grp{pid};
        if (rid && !rid->isEmpty())
            grp = mod.regions.get(*rid)->provinces;
        for (long long q : grp)
            if (!out.contains(q) && (q == pid || doc->prov_state(q).owner == own))
                out << q;
    }
    return out;
}

void ProvincePanel::_set_status(const QVariant &value)
{
    if (!value.isValid() || !(!pids.isEmpty() && doc && doc->history_editable()))
        return;
    const long long v = value.toLongLong();
    static const std::map<long long, QString> names = {{1, u"Protectorate"_s}, {2, u"Colony"_s}, {0, u"State"_s}};
    const auto it = names.find(v);
    Q_EMIT editFor(u"Status → %1"_s.arg(it != names.end() ? it->second : QString::number(v)),
                   [v](long long, history::ProvState &st) { st.colonial = v; }, status_group());
}

void ProvincePanel::_set_scalar(const QString &key, const OptStr &value, const QString &name)
{
    if (!value || *value == MIXED)
        return;
    const QString v = *value;
    _emit(u"%1 → %2"_s.arg(name, v), [key, v](long long, history::ProvState &st) {
        (key == u"controller" ? st.controller : st.trade_goods) = v;
    });
}

void ProvincePanel::_add_core(const OptStr &tag, bool own)
{
    add_core->setCurrentIndex(0);
    if (own || (tag && !tag->isEmpty()))
        _emit(u"Add core"_s, [tag, own](long long, history::ProvState &st) {
            const OptStr t = own ? st.owner : tag;
            if (t && !t->isEmpty() && !st.cores.contains(*t))
                st.cores.append(*t);
        });
}

void ProvincePanel::_remove_core(const QString &tag)
{
    _emit(u"Remove core %1"_s.arg(tag), [tag](long long, history::ProvState &st) { st.cores.removeOne(tag); });
}

void ProvincePanel::_lr(long long d)
{
    _emit(u"Life rating %1"_s.arg(signed_int(d)), [d](long long, history::ProvState &st) {
        st.life_rating = std::max(0LL, std::min(100LL, st.life_rating.value_or(0) + d));
    });
}

void ProvincePanel::_add_level_row(const QString &key, const QString &text)
{
    const int r = static_cast<int>(level_rows.size());
    auto *minus = new QPushButton(u"−"_s);
    minus->setObjectName(u"stepMinus"_s);
    minus->setFixedWidth(34);
    auto *plus = new QPushButton(u"+"_s);
    plus->setObjectName(u"stepPlus"_s);
    plus->setFixedWidth(34);
    QLabel *val = label(u"0"_s, u"title"_s);
    val->setStyleSheet(u"font-size: 13px;"_s);
    val->setAlignment(Qt::AlignCenter);
    val->setMinimumWidth(70);
    QLabel *why = label(QString(), u"faint"_s);
    QLabel *name = label(text, u"sub"_s, false);
    connect(minus, &QPushButton::clicked, this, [this, k = key] { _level(k, -1); });
    connect(plus, &QPushButton::clicked, this, [this, k = key] { _level(k, +1); });
    level_grid->addWidget(name, r, 0);
    level_grid->addWidget(minus, r, 1);
    level_grid->addWidget(val, r, 2);
    level_grid->addWidget(plus, r, 3);
    level_grid->addWidget(why, r, 4);
    level_rows.push_back({key, {minus, val, plus, why, name}});
}

void ProvincePanel::_sync_level_rows()
{
    // The mod's other province buildings (buildings.txt, province = yes) get a row each, as the three of the game;
    // a row whose building is gone stays hidden (rows are never taken out: their buttons may be in use).
    const QStringList more = doc && doc->hist ? doc->hist->more : QStringList();
    for (const QString &k : more) {
        bool have = false;
        for (const auto &[key, lr] : level_rows)
            have = have || key == k;
        if (!have) {
            const moddata::Building *b = mod.buildings.get(k);
            _add_level_row(k, b ? b->name : k);
        }
    }
    // (the game's three are left alone, and a widget is shown / hidden only when that changes: a needless
    // setVisible makes the page keep an older, taller layout — measured, the Python page's picture came out 20 px off)
    for (auto &[key, lr] : level_rows) {
        if (LEVEL_THREE.contains(key))
            continue;
        const bool on = more.contains(key);
        for (QWidget *w : {static_cast<QWidget *>(lr.minus), static_cast<QWidget *>(lr.val),
                           static_cast<QWidget *>(lr.plus), static_cast<QWidget *>(lr.why),
                           static_cast<QWidget *>(lr.name)})
            if (w && w->isHidden() == on)
                w->setVisible(on);
        if (const moddata::Building *b = mod.buildings.get(key); b && lr.name && lr.name->text() != b->name)
            lr.name->setText(b->name);
    }
}

void ProvincePanel::_level(const QString &key, long long d)
{
    const long long mx = _max_level(key);
    auto fn = [key, d, mx](long long, history::ProvState &st) {
        st.set_level(key, std::max(0LL, std::min(mx, st.level_of(key) + d)));
    };
    if (d > 0 && key == u"naval_base") {
        QList<long long> ok;
        for (long long p : pids)
            if (_naval_ok(p))
                ok << p;
        if (ok.isEmpty())
            return;
        Q_EMIT edit(u"Naval base +1"_s, [ok, fn](long long pid, history::ProvState &st) {
            if (ok.contains(pid))
                fn(pid, st);
        });
        return;
    }
    _emit(u"%1 %2"_s.arg(py::capitalize(QString(key).replace(u'_', u' ')), signed_int(d)), fn);
}

long long ProvincePanel::_max_level(const QString &key) const
{
    const moddata::Building *b = mod.buildings.get(key);
    if (key == u"railroad" && !b)
        for (const auto &[k, x] : mod.buildings)
            if (x.type == u"infrastructure") {
                b = &x;
                break;
            }
    return b ? b->max_level : 6;
}

bool ProvincePanel::_naval_ok(long long pid) const
{
    // Coastal, and no other province of its state already has a naval base.
    if (mapdata && !mapdata->coastal[size_t(pid)])
        return false;
    const OptStr &rid = mod.provinces.get(pid)->region;
    const moddata::Building *nb = mod.buildings.get(u"naval_base"_s);
    if (rid && !rid->isEmpty() && (nb ? nb->one_per_state : true))
        for (long long q : mod.regions.get(*rid)->provinces)
            if (q != pid && doc->prov_state(q).naval_base > 0)
                return false;
    return true;
}

std::optional<OptStr> ProvincePanel::_state_rid() const
{
    std::set<OptStr> rids;
    for (long long p : pids)
        rids.insert(mod.provinces.get(p)->region);
    if (rids.size() == 1)
        return *rids.begin();
    return std::nullopt;
}

void ProvincePanel::_add_factory(const OptStr &building)
{
    add_fact->setCurrentIndex(0);
    if (!building || building->isEmpty())
        return;
    const auto rid = _state_rid();
    std::optional<long long> target;
    if (focus && pids.contains(*focus))
        target = focus;
    else if (!pids.isEmpty())
        target = pids[0];
    if (!rid || !*rid || !target)
        return;
    if (static_cast<long long>(doc->state_factories(**rid).size()) >= FACTORY_CAP) {
        fact_warn->setText(u"⚠ This state already has %1 factories (the maximum)."_s.arg(FACTORY_CAP));
        return;
    }
    const QString b = *building;
    const long long t = *target;
    Q_EMIT edit(u"Add %1"_s.arg(b), [b, t](long long pid, history::ProvState &st) {
        if (pid == t)
            st.factories.push_back({b, 1, true, nullptr});
    });
}

void ProvincePanel::_factory_level(long long pid, size_t idx, long long d)
{
    Q_EMIT edit(u"Factory level %1"_s.arg(signed_int(d)), [pid, idx, d](long long p, history::ProvState &st) {
        if (p == pid && idx < st.factories.size()) {
            history::Factory &f = st.factories[idx];
            f.level = std::max(1LL, std::min(99LL, f.level + d));
        }
    });
}

void ProvincePanel::_remove_factory(long long pid, size_t idx)
{
    const history::Factory &f = doc->prov_state(pid).factories[idx];
    Q_EMIT edit(u"Remove %1"_s.arg(f.building.value_or(u"None"_s)), [pid, idx](long long p, history::ProvState &st) {
        if (p == pid && idx < st.factories.size())
            st.factories.erase(st.factories.begin() + static_cast<std::ptrdiff_t>(idx));
    });
}

// ---------------------------------------------------------------------------------------------- refresh
void ProvincePanel::refresh(document::Document &d, const QList<long long> &pids_, const panels::Header &header,
                            std::optional<long long> focus_)
{
    doc = &d;
    pids = pids_;
    focus = focus_;
    // nations made (or renamed) in the editor: the owner / controller / core pickers list them too
    std::vector<std::pair<QString, QString>> sig;
    for (const auto &[t, c] : mod.countries)
        sig.emplace_back(t, c.name);
    std::sort(sig.begin(), sig.end());
    if (sig != _csig) {
        _csig = sig;
        const auto items = country_items(mod);
        owner->fill(items, {{MIXED, u"(mixed)"_s}});
        controller->fill(items, {{MIXED, u"(mixed)"_s}});
        add_core->fill(items, {{std::nullopt, u"＋ Add core…"_s}});
        add_core->setCurrentIndex(0);
    }
    title->setText(header.title);
    sub->setText(header.sub);
    const bool editable = d.history_editable();
    if (pids.isEmpty())
        note->setText(QString());
    else if (editable)
        note->setText(u"Edits are written into the %1 = { } block of each province's history file."_s.arg(d.scenario));
    else
        note->setText(u"'%1' is not a date folder, so province history is read-only here."_s.arg(d.scenario));
    const bool on = !pids.isEmpty() && editable;
    for (QPushButton *w : findChildren<QPushButton *>())
        w->setEnabled(on);
    for (QComboBox *w : findChildren<QComboBox *>())
        w->setEnabled(on);
    for (QCheckBox *w : findChildren<QCheckBox *>())
        w->setEnabled(on);
    move_btn->setEnabled(!pids.isEmpty());   // region.txt is not date-bound
    OptStr rid1;
    if (pids.size() == 1)
        rid1 = mod.provinces.get(pids[0])->region;
    const bool can_cap = rid1 && !rid1->isEmpty() && d.capital(*rid1) != std::optional<long long>(pids[0]);
    cap_btn->setVisible(can_cap);
    cap_btn->setEnabled(can_cap);
    cap_pops->setVisible(can_cap);
    cap_pops->setEnabled(can_cap);
    _fill_state_line();
    _fill_island();
    _fill_names();
    _sync_level_rows();
    if (pids.isEmpty()) {
        for (dialogs::PickCombo *combo : {owner, controller, rgo})
            combo->setCurrentIndex(0);
        lr_value->setText(u"—"_s);
        _fill_cores({});
        fact_section->hide();
        later_section->hide();
        for (auto &[k, lr] : level_rows) {
            lr.val->setText(u"—"_s);
            lr.why->setText(QString());
        }
        return;
    }
    const auto states = _states();
    _select(owner, _common(&history::ProvState::owner));
    _select(controller, _common(&history::ProvState::controller));
    _select(rgo, _common(&history::ProvState::trade_goods));
    const QList<long long> grp = status_group();
    std::set<long long> vals;
    for (long long q : grp)
        vals.insert(d.prov_state(q).colonial);
    colonial->setCurrentIndex(vals.size() == 1 ? colonial->findData(int(*vals.begin())) : -1);
    long long extra = 0;
    {
        const std::set<long long> g(grp.begin(), grp.end()), sel(pids.begin(), pids.end());
        for (long long q : g)
            extra += sel.contains(q) ? 0 : 1;
    }
    status_note->setText(extra ? u"Applies to the whole state: %1 more province%2 with the same owner."_s.arg(extra).arg(
                                     extra == 1 ? QString() : u"s"_s)
                               : QString());
    std::set<std::optional<long long>> lrs;
    for (const auto &st : states)
        lrs.insert(st.life_rating);
    if (lrs.size() == 1) {
        lr_value->setText(lrs.begin()->has_value() ? QString::number(**lrs.begin()) : u"None"_s);
    } else {
        long long lo = 0, hi = 0;
        bool first = true;
        for (const auto &x : lrs) {
            const long long v = x.value_or(0);
            lo = first ? v : std::min(lo, v);
            hi = first ? v : std::max(hi, v);
            first = false;
        }
        lr_value->setText(u"%1–%2"_s.arg(lo).arg(hi));
    }
    OrderedMap<QString, long long> cores;
    for (const auto &st : states)
        for (const QString &c : st.cores)
            cores[c] += 1;
    _fill_cores(cores);
    for (auto &[key, lr] : level_rows) {
        const long long mx = _max_level(key);
        std::set<long long> lv;
        for (const auto &st : states)
            lv.insert(st.level_of(key));
        lr.val->setText(lv.size() == 1 ? u"%1 / %2"_s.arg(*lv.begin()).arg(mx)
                                       : u"%1–%2 / %3"_s.arg(*lv.begin()).arg(*lv.rbegin()).arg(mx));
        lr.why->setText(QString());
        if (key == u"naval_base") {
            qsizetype bad = 0;
            for (long long p : pids)
                bad += _naval_ok(p) ? 0 : 1;
            if (bad && bad == pids.size()) {
                lr.plus->setEnabled(false);
                bool coastal = !mapdata;
                for (long long p : pids)
                    coastal = coastal || mapdata->coastal[size_t(p)];
                lr.why->setText(!coastal ? u"not coastal"_s : u"state already has one"_s);
            }
        }
    }
    _fill_membership();
    _fill_factories();
    _fill_later();
}

void ProvincePanel::_fill_names()
{
    // Name fields: the province's (one selected) and its state's (all selected in one state).
    localisation::LocEdits *loc = doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s)
                                                                      : nullptr;
    const std::optional<long long> one = pids.size() == 1 ? std::optional<long long>(pids[0]) : std::nullopt;
    std::set<OptStr> rids;
    for (long long p : pids)
        if (const moddata::Province *pr = mod.provinces.get(p))
            rids.insert(pr->region);
    const OptStr rid = rids.size() == 1 ? *rids.begin() : std::nullopt;
    _name_keys = {{u"prov"_s, one && *one ? OptStr(u"PROV%1"_s.arg(*one)) : std::nullopt}, {u"state"_s, rid}};
    const std::vector<OptStr> keys{_name_keys[u"prov"_s], _name_keys[u"state"_s]};
    if (loc) {
        QStringList want;
        for (const OptStr &k : keys)
            if (k && !k->isEmpty())
                want << *k;
        loc->prefetch(want);
    }
    for (const auto &[ed, sec] : {std::pair{name_edit, u"prov"_s}, std::pair{state_name_edit, u"state"_s}}) {
        const OptStr key = _name_keys[sec];
        const bool has = key && !key->isEmpty();
        ed->blockSignals(true);
        ed->setEnabled(has && loc);
        ed->setText(has && loc ? loc->text(*key).value_or(QString()) : QString());
        ed->blockSignals(false);
    }
    QStringList parts;
    for (const OptStr &k : keys)
        if (k && !k->isEmpty() && loc)
            parts << u"%1 → %2"_s.arg(*k, loc->where(*k));
    name_where->setText(!parts.isEmpty() ? u"Localisation: "_s + parts.join(u" · "_s) : QString());
    name_where->setVisible(!parts.isEmpty());
}

void ProvincePanel::_name_done(QLineEdit *ed, const QString &sec)
{
    localisation::LocEdits *loc = doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s)
                                                                      : nullptr;
    const OptStr key = _name_keys.contains(sec) ? _name_keys[sec] : std::nullopt;
    const QString text = py::strip(ed->text());
    if (!loc || !key || key->isEmpty() || text.isEmpty() || text == loc->text(*key).value_or(QString()))
        return;
    Q_EMIT nameRequested(*key, text, sec);
}

void ProvincePanel::_fill_island()
{
    // The "Has islands" box: ticked when every selected province carries the island flag.
    QCheckBox *cb = island_cb;
    QLabel *nt = island_note;
    history::IslandFlags *isl = doc && doc->maps.contains(u"islands"_s)
                                    ? &doc->map<store::StoreOf<history::IslandFlags>>(u"islands"_s).obj
                                    : nullptr;
    QList<long long> land;
    for (long long p : pids)
        if (const moddata::Province *pr = mod.provinces.get(p); pr && !pr->sea)
            land << p;
    cb->blockSignals(true);
    if (!isl || land.isEmpty()) {
        cb->setCheckState(Qt::Unchecked);
        cb->setEnabled(false);
        nt->setText(QString());
        cb->blockSignals(false);
        return;
    }
    QList<long long> marked;
    for (long long p : land)
        if (isl->marked.contains(p))
            marked << p;
    cb->setCheckState(marked.size() == land.size() ? Qt::Checked : !marked.isEmpty() ? Qt::PartiallyChecked : Qt::Unchecked);
    cb->blockSignals(false);
    QList<long long> nohist;
    for (long long p : land)
        if (!doc->hist->main_file(p))
            nohist << p;
    cb->setEnabled(nohist.isEmpty());
    std::map<long long, std::vector<std::int64_t>> parts;
    for (long long p : land)
        parts[p] = pieces_of ? pieces_of(p) : std::vector<std::int64_t>{};
    const QString amber = u"rgb(240, 170, 60)"_s;
    QString txt;
    if (!nohist.isEmpty()) {
        txt = u"<span style='color:%1'>⚠ #%2 has no history file to hold the flag</span>"_s.arg(amber, tag_list(nohist, 6));
    } else if (land.size() == 1) {
        const long long p = land[0];
        const auto &lst = parts[p];
        if (lst.size() > 1) {
            QStringList ss;
            for (size_t i = 0; i < lst.size() && i < 6; ++i)
                ss << fmt(lst[i]);
            txt = u"%1 separate parts on the map: %2 px"_s.arg(lst.size()).arg(ss.join(u" + "_s) +
                                                                            (lst.size() > 6 ? u" …"_s : QString()));
            if (!isl->marked.contains(p))
                txt = u"<span style='color:%1'>⚠ %2 — tick the box if the others are islands, else clean them up "
                      u"in Drawing</span>"_s.arg(amber, txt);
        } else {
            txt = u"One connected area on the map%1"_s.arg(isl->marked.contains(p) ? u" — the flag isn't needed"_s
                                                                                 : QString());
        }
    } else {
        long long split = 0;
        for (long long p : land)
            split += parts[p].size() > 1 ? 1 : 0;
        txt = u"%1 of the %2 selected are in several parts on the map · %3 marked"_s.arg(split).arg(land.size()).arg(
            marked.size());
    }
    nt->setText(txt);
}

QString ProvincePanel::pretty(const QString &name) { return py::capitalize(QString(name).replace(u'_', u' ')); }

void ProvincePanel::_fill_membership()
{
    QStringList warns;
    for (const auto &[kind, cb] : member_boxes) {
        auto &mf = doc->map<mapfiles::MembershipFile>(kind);
        QStringList have;
        for (int i = 0; i < cb->count(); ++i)
            have << cb->itemData(i).toString();
        auto count_of = [&mf](const QString &n) {
            const auto *l = mf.lists.get(n);
            return l ? static_cast<long long>(l->size()) : 0LL;
        };
        if (have != mf.names) {
            cb->clear();
            for (const QString &n : mf.names)
                cb->addItem(u"%1  (%2)"_s.arg(pretty(n)).arg(count_of(n)), n);
        } else {
            for (int i = 0; i < mf.names.size(); ++i)
                cb->setItemText(i, u"%1  (%2)"_s.arg(pretty(mf.names[i])).arg(count_of(mf.names[i])));
        }
        std::vector<QStringList> sets;
        for (long long p : pids)
            sets.push_back(mf.of(p));
        std::set<QStringList> vs(sets.begin(), sets.end());
        if (vs.size() == 1 && sets[0].size() == 1)
            cb->setCurrentIndex(cb->findData(sets[0][0]));
        else
            cb->setCurrentIndex(-1);
        cb->setEnabled(!pids.isEmpty());
        QList<long long> none;
        std::vector<std::pair<long long, QStringList>> dup;
        for (qsizetype i = 0; i < pids.size(); ++i) {
            if (sets[size_t(i)].isEmpty())
                none << pids[i];
            if (sets[size_t(i)].size() > 1)
                dup.emplace_back(pids[i], sets[size_t(i)]);
        }
        if (!none.isEmpty())
            warns << u"⚠ No %1: #%2"_s.arg(kind, tag_list(none, 8));
        for (size_t i = 0; i < dup.size() && i < 4; ++i)
            warns << u"⚠ #%1 is in %2 %3s: %4 — pick one to fix"_s.arg(dup[i].first).arg(dup[i].second.size()).arg(
                kind, dup[i].second.join(u", "_s));
    }
    member_warn->setText(warns.join(u"\n"_s));
    member_warn->setVisible(!warns.isEmpty());
}

void ProvincePanel::_fill_state_line()
{
    state_warn->hide();
    if (pids.isEmpty()) {
        state_line->setText(QString());
        return;
    }
    std::set<QString> rset;
    for (long long p : pids) {
        const OptStr &r = mod.provinces.get(p)->region;
        rset.insert(r && !r->isEmpty() ? *r : u"—"_s);
    }
    const QStringList rids(rset.begin(), rset.end());
    if (rids.size() > 1) {
        state_line->setText(u"States: %1"_s.arg(rids.mid(0, 6).join(u", "_s)) + (rids.size() > 6 ? u" …"_s : QString()));
        return;
    }
    const QString rid = rids[0];
    const moddata::Region *r = mod.regions.get(rid);
    if (!r) {
        state_line->setText(u"State: <i>none</i>"_s);
        return;
    }
    const auto cap = doc->capital(rid);
    const moddata::Province *cp = cap ? mod.provinces.get(*cap) : nullptr;
    const QString capname = cp ? cp->name : u"—"_s;
    const long long nf = static_cast<long long>(doc->state_factories(rid).size());
    state_line->setText(u"State: <b>%1</b> (<span style='color:%2'>★</span> %3) · %4 provinces · "
                        u"<span style='color:%5'>%6/%7 factories</span>"_s.arg(rid, theme::GOLD, capname)
                            .arg(r->provinces.size())
                            .arg(nf > FACTORY_CAP ? theme::DANGER : theme::INK_DIM)
                            .arg(nf)
                            .arg(FACTORY_CAP));
    QStringList warns;
    const auto owners = state_owners(*doc, rid);
    if (owners.size() > 1) {
        const OptStr own = doc->prov_state(pids[0]).owner;
        std::vector<OptStr> others;
        for (const auto &[o, _n] : owners)
            if (o != own)
                others.push_back(o);
        std::stable_sort(others.begin(), others.end(), [](const OptStr &a, const OptStr &b) {
            if (a.has_value() != b.has_value())
                return a.has_value();              // (t is None, str(t)): None last
            return a.value_or(u"None"_s) < b.value_or(u"None"_s);
        });
        auto name = [this](const OptStr &o) -> QString {
            if (!o || o->isEmpty())
                return u"no owner"_s;
            const moddata::Country *c = mod.countries.get(*o);
            return c ? c->name : *o;
        };
        QStringList legend{u"%1 ×%2 (this owner, light hatch)"_s.arg(name(own)).arg(owners.value(own, 0))};
        for (size_t i = 0; i < others.size(); ++i) {
            const auto &c = mapview::MESH_COLORS[i % mapview::MESH_COLORS.size()];
            legend << u"<span style='color:rgb(%1,%2,%3)'>▦</span> %4 ×%5"_s.arg(std::get<0>(c))
                          .arg(std::get<1>(c))
                          .arg(std::get<2>(c))
                          .arg(name(others[i]))
                          .arg(owners.value(others[i], 0));
        }
        warns << u"⚠ This state has several owners — the game splits it per owner:<br>&nbsp;&nbsp;&nbsp;"_s +
                     legend.join(u" · "_s);
    }
    if (r->provinces.size() > 1 && mapdata) {
        const Links links = state_links(doc && doc->maps.contains(u"adjacency"_s)
                                            ? &doc->map<adjacency::AdjacencyFile>(u"adjacency"_s)
                                            : nullptr);   // (meme rivers join: C++ only)
        const auto parts = state_components(mod, mapdata, rid, &links);
        if (parts.size() > 1) {
            size_t big = 0;
            for (const auto &x : parts)
                big = std::max(big, x.size());
            QList<long long> cut;
            for (long long p : pids) {
                bool in_big = false;
                for (const auto &c : parts)
                    in_big = in_big || (c.size() == big && std::find(c.begin(), c.end(), p) != c.end());
                if (!in_big)
                    cut << p;
            }
            if (!cut.isEmpty())
                warns << u"⚠ #%1 %2 not connected to the rest of %3."_s.arg(tag_list(cut, cut.size()),
                                                                            cut.size() == 1 ? u"is"_s : u"are"_s, rid);
            else
                warns << u"⚠ %1 is split into %2 unconnected parts."_s.arg(rid).arg(parts.size());
        }
    }
    if (!warns.isEmpty()) {
        state_warn->setText(warns.join(u"<br>"_s));
        state_warn->show();
    }
}

void ProvincePanel::_fill_cores(const OrderedMap<QString, long long> &cores)
{
    clear_grid(cores_lay);
    const qsizetype n = pids.size();
    std::vector<std::pair<QString, long long>> kv(cores.begin(), cores.end());
    std::stable_sort(kv.begin(), kv.end(), [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    int i = 0;
    for (const auto &[tag, cnt] : kv) {
        const moddata::Country *c = mod.countries.get(tag);
        const QString cname = c ? c->name : tag;
        auto *b = new QPushButton(u"%1  %2%3   ✕"_s.arg(tag, cname,
                                                         n == 1 ? QString() : u"  (%1/%2)"_s.arg(cnt).arg(n)));
        b->setIcon(swatch_icon(c ? c->color : GREY));
        b->setToolTip(u"Remove core %1 from the selected province(s)"_s.arg(tag));
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setEnabled(doc && doc->history_editable());
        connect(b, &QPushButton::clicked, this, [this, t = tag] { _remove_core(t); });
        cores_lay->addWidget(b, i / 2, i % 2);
        ++i;
    }
    if (cores.isEmpty())
        cores_lay->addWidget(label(u"no cores"_s, u"faint"_s), 0, 0);
}

void ProvincePanel::_fill_factories()
{
    clear_grid(fact_lay);
    const auto srid = _state_rid();
    if (!srid || !*srid) {
        fact_section->show();
        fact_title->setText(u"Factories"_s);
        fact_warn->setText(u"Select provinces of a single state to see and edit its factories."_s);
        add_fact->setEnabled(false);
        return;
    }
    const QString rid = **srid;
    fact_section->show();
    const auto facts = doc->state_factories(rid);
    const long long nf = static_cast<long long>(facts.size());
    const bool full = nf >= FACTORY_CAP;
    fact_title->setText(u"Factories in state %1:  %2 / %3"_s.arg(rid).arg(nf).arg(FACTORY_CAP));
    fact_title->setStyleSheet(u"color: %1;"_s.arg(nf > FACTORY_CAP ? theme::DANGER : theme::INK_DIM));
    const bool editable = doc->history_editable();
    // (Python: {id(f): why for the state's factory_problems} — the same rule, by the factory's place in the state)
    const bool coastal = doc->state_is_coastal(rid);
    std::vector<QString> problems(facts.size());
    for (size_t i = 0; i < facts.size(); ++i) {
        if (i >= 8)
            problems[i] = u"over the limit of 8 factories per state"_s;
        else if (!coastal && mod.factory_info(facts[i].second.building.value_or(QString())).coastal)
            problems[i] = u"needs a coastal state"_s;
    }
    const bool preexisting = !doc->session_rids().contains(rid);
    // index of each factory inside its province's list (needed for edits), then display order:
    // goods category of the output (goods.txt order), then name
    struct Row {
        long long pid;
        size_t idx;
        const history::Factory *f;
        QString bad;
    };
    std::vector<Row> rows;
    std::map<long long, size_t> per_pid;
    for (size_t i = 0; i < facts.size(); ++i) {
        const long long pid = facts[i].first;
        const size_t idx = per_pid[pid];
        per_pid[pid] = idx + 1;
        rows.push_back({pid, idx, &facts[i].second, problems[i]});
    }
    auto bname = [this](const history::Factory &f) {
        const QString k = f.building.value_or(QString());
        const moddata::Building *b = mod.buildings.get(k);
        return b ? b->name : k;
    };
    std::vector<std::pair<long long, QString>> keys;
    for (const Row &x : rows)
        keys.emplace_back(mod.factory_info(x.f->building.value_or(QString())).group_order, py::lower(bname(*x.f)));
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
    OptStr last_group;
    bool have_group = false;
    int r = -1;
    for (size_t oi : order) {
        const Row &x = rows[oi];
        const history::Factory &f = *x.f;
        const moddata::FactoryInfo fi = mod.factory_info(f.building.value_or(QString()));
        if (!have_group || fi.group_name != *last_group) {
            have_group = true;
            last_group = fi.group_name;
            ++r;
            fact_lay->addWidget(label(fi.group_name, u"faint"_s, false), r, 0, 1, 6);
        }
        ++r;
        const QString bn = bname(f);
        const QString &bad = x.bad;
        auto *name = new QLabel(bn);
        name->setToolTip(u"%1 → %2%3"_s.arg(bn, fi.output_name,
                                           !bad.isEmpty() ? u"\n⚠ "_s + bad + u" — removed on Apply"_s : QString()));
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        if (!bad.isEmpty())
            name->setStyleSheet(u"color: %1;"_s.arg(theme::DANGER));
        QLabel *where = label(u"→ %1 · #%2 %3"_s.arg(fi.output_name).arg(x.pid).arg(mod.provinces.get(x.pid)->name),
                              u"faint"_s, false);
        where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *minus = new QPushButton(u"−"_s);
        minus->setObjectName(u"stepMinus"_s);
        minus->setFixedWidth(28);
        auto *lv = new QLabel(u"lv %1"_s.arg(f.level));
        auto *plus = new QPushButton(u"+"_s);
        plus->setObjectName(u"stepPlus"_s);
        plus->setFixedWidth(28);
        auto *rm = new QPushButton(u"✕"_s);
        rm->setObjectName(u"icon"_s);
        const bool fresh = !f.src || f.src->date == doc->date;
        rm->setToolTip(u"Remove"_s + (fresh ? QString() : u" (comes from earlier history — affects all start dates)"_s));
        const long long pid = x.pid;
        const size_t idx = x.idx;
        connect(minus, &QPushButton::clicked, this, [this, pid, idx] { _factory_level(pid, idx, -1); });
        connect(plus, &QPushButton::clicked, this, [this, pid, idx] { _factory_level(pid, idx, +1); });
        connect(rm, &QPushButton::clicked, this, [this, pid, idx] { _remove_factory(pid, idx); });
        for (QWidget *w : {static_cast<QWidget *>(minus), static_cast<QWidget *>(plus), static_cast<QWidget *>(rm)})
            w->setEnabled(editable);
        int c = 0;
        for (QWidget *w : {static_cast<QWidget *>(name), static_cast<QWidget *>(where), static_cast<QWidget *>(minus),
                           static_cast<QWidget *>(lv), static_cast<QWidget *>(plus), static_cast<QWidget *>(rm)})
            fact_lay->addWidget(w, r, c++);
    }
    fact_lay->setColumnStretch(0, 2);
    fact_lay->setColumnStretch(1, 2);
    const long long target = focus && pids.contains(*focus) ? *focus : pids[0];
    QSet<QString> have;
    for (const auto &[_p, f] : facts)
        have.insert(f.building.value_or(QString()));
    std::vector<dialogs::PickItem> items;
    for (const QString &k : mod.factory_types()) {
        const moddata::FactoryInfo fi = mod.factory_info(k);
        if (have.contains(k) || (fi.coastal && !coastal))
            continue;
        const moddata::Unlock *u = mod.unlocks().get(k);
        const QString when = !u ? u"from start"_s
                             : (u->missing && !u->missing->isEmpty()) ? u"⚠ never? needs %1"_s.arg(*u->missing)
                                                                      : u"%1 %2"_s.arg(u->by).arg(u->year);
        items.push_back({k, u"%1  → %2   ·  %3"_s.arg(mod.buildings.get(k)->name, fi.output_name, when), std::nullopt,
                         fi.group_name});
    }
    add_fact->fill(items, {{std::nullopt, u"＋ Add factory to #%1 %2…"_s.arg(target).arg(mod.provinces.get(target)->name)}});
    add_fact->setCurrentIndex(0);
    add_fact->setEnabled(editable && !full);
    QStringList warns;
    long long over = 0;
    bool needs = false;
    for (const QString &why : problems) {
        over += why.startsWith(u"over"_s) ? 1 : 0;
        needs = needs || why.startsWith(u"needs"_s);
    }
    if (over && preexisting)
        warns << u"⚠ %1 factor%2 over the limit of %3 (marked red) — already like this in the mod files; "
                 u"not changed unless you edit this state."_s.arg(over).arg(over == 1 ? u"y is"_s : u"ies are"_s).arg(FACTORY_CAP);
    else if (over)
        warns << u"⚠ %1 factor%2 over the limit of %3 (marked red) — removed on Apply unless you remove "
                 u"others first."_s.arg(over).arg(over == 1 ? u"y is"_s : u"ies are"_s).arg(FACTORY_CAP);
    else if (full)
        warns << u"This state already has the maximum of %1 factories."_s.arg(FACTORY_CAP);
    if (needs)
        warns << u"⚠ Shipyards need a state with at least one coastal province (marked red) — "_s +
                     (preexisting ? u"already like this in the mod files; not changed unless you edit this state."_s
                                  : u"removed on Apply."_s);
    if (!coastal)
        warns << u"Shipyards can't be built here: no province of this state is coastal."_s;
    fact_warn->setText(warns.join(u"\n"_s));
}

void ProvincePanel::_fill_later()
{
    if (pids.size() != 1) {
        later_section->hide();
        return;
    }
    const auto blocks = doc->hist->dated_blocks(pids[0]);
    if (blocks.empty()) {
        later_section->hide();
        return;
    }
    later_section->show();
    QStringList lines;
    for (const auto &b : blocks) {
        const QString mark = doc->date && b.date == *doc->date ? u"  ← this scenario"_s : QString();
        lines << u"%1.%2.%3%4  (%5)"_s.arg(std::get<0>(b.date)).arg(std::get<1>(b.date)).arg(std::get<2>(b.date)).arg(
            mark, b.rel);
        for (const auto &[k, v] : b.lines)
            if (k && !k->isEmpty())
                lines << u"    %1 = %2"_s.arg(*k, v);
    }
    later->setText(lines.join(u"\n"_s));
}

}  // namespace provpanel
