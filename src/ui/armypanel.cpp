// armypanel.py — Armies tab (see armypanel.h).
#include "ui/armypanel.h"
#include "core/countryhist.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "grid/pyset.h"
#include "ui/dialogs.h"
#include "ui/leaderbox.h"
#include "ui/leaderlist.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace armypanel {

const Rgb GOLD_RING{238, 198, 98};
const std::vector<std::pair<QString, QString>> SHIP_SCHEMES = {
    {u"ship"_s, u"Ship 51, Ship 52 … (continues the nation's numbering)"_s},
    {u"type"_s, u"Frigate 1, Frigate 2 … (numbered per type in this navy)"_s},
    {u"custom"_s, u"Custom pattern…"_s}};
const QString SHIP_HELP = u"{n} = the number, {type} = Frigate…, {navy} = the navy's name"_s;

Rgb army_color(long long i)
{
    const long long n = static_cast<long long>(oob::ARMY_COLORS.size());
    return oob::ARMY_COLORS[size_t(((i % n) + n) % n)];
}

Rgb dim(const Rgb &rgb, double k)
{
    auto f = [k](int c) { return int(double(c) * k + 70 * (1 - k)); };
    return {f(std::get<0>(rgb)), f(std::get<1>(rgb)), f(std::get<2>(rgb))};
}

namespace {

QString rgb_str(const Rgb &c)                      // str((r, g, b))
{
    return u"(%1, %2, %3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

long long get0(const OrderedMap<long long, long long> &m, const OptInt &k)   // d.get(k, 0)
{
    if (!k)
        return 0;
    const long long *v = m.get(*k);
    return v ? *v : 0;
}

QPushButton *x_button(const QString &tip)
{
    auto *x = new QPushButton(u"✕"_s);
    x->setFixedWidth(28);
    x->setStyleSheet(u"QPushButton { padding: 0px; }"_s);
    x->setToolTip(tip);
    return x;
}

QComboBox *type_combo()
{
    auto *cb = new QComboBox();
    cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cb->setMinimumContentsLength(8);
    return cb;
}

// Counter(...).items() sorted by -count (stable: the first seen first among equals)
std::vector<std::pair<QString, long long>> by_count(const OrderedMap<QString, long long> &cnt)
{
    std::vector<std::pair<QString, long long>> v(cnt.begin(), cnt.end());
    std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    return v;
}

void bump(OrderedMap<QString, long long> &cnt, const QString &k)
{
    if (long long *v = cnt.get(k))
        ++*v;
    else
        cnt.insert(k, 1);
}

}  // namespace

ArmyPanel::ArmyPanel(moddata::Mod &mod, mapview::MapData &mapdata, QWidget *parent)
    : QWidget(parent), mod(mod), mapdata(mapdata)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- Armies | Navies
    auto *segw = new QWidget();
    segw->setObjectName(u"side"_s);
    auto *sl = new QHBoxLayout(segw);
    sl->setContentsMargins(16, 10, 16, 4);
    sl->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    // the page's two sub-tabs: Armies | Generals, or Navies | Admirals (the rail picks the pair; the user 2026-11-18)
    for (const auto &[key, text] : {std::pair{u"armies"_s, u"Armies"_s}, std::pair{u"generals"_s, u"Generals"_s},
                                    std::pair{u"navies"_s, u"Navies"_s}, std::pair{u"admirals"_s, u"Admirals"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == sub);
        connect(b, &QPushButton::clicked, this, [this, k = key] { set_sub(k); });
        grp->addButton(b);
        sl->addWidget(b, 1);
        seg.emplace_back(key, b);
    }
    lay->addWidget(segw);

    // --- which nation
    auto *s = new Section();
    picker = new dialogs::PickCombo(nationpanel::nation_items(mod));
    picker->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(picker, &QComboBox::activated, this, [this](int) {
        if (const OptStr k = picker->key(); k && !k->isEmpty())
            Q_EMIT nationPicked(*k);
    });
    s->lay->addWidget(picker);
    title = label(u"Click a province of a nation on the map, or pick one above."_s, u"title"_s);
    s->lay->addWidget(title);
    summary = label(u""_s, u"sub"_s);
    summary->setTextFormat(Qt::RichText);
    s->lay->addWidget(summary);
    file_note = label(u""_s, u"faint"_s);
    s->lay->addWidget(file_note);
    rule_note = label(u""_s, u"faint"_s);
    s->lay->addWidget(rule_note);
    lay->addWidget(s);
    nation_section = s;

    // ============================================================ armies
    armies_box = new QWidget();
    auto *al = new QVBoxLayout(armies_box);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(0);
    s = new Section();
    s->lay->addWidget(label(u"Armies"_s));
    army_list = new QListWidget();
    army_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    army_list->setTextElideMode(Qt::ElideRight);
    connect(army_list, &QListWidget::currentRowChanged, this, [this](int r) { _army_row(r); });
    s->lay->addWidget(army_list);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    new_army_btn = new QPushButton(u"＋ New army"_s);
    connect(new_army_btn, &QPushButton::clicked, this, [this] { new_army(); });
    del_army_btn = new QPushButton(u"Delete army"_s);
    connect(del_army_btn, &QPushButton::clicked, this, [this] { delete_army(); });
    row->addWidget(new_army_btn, 1);
    row->addWidget(del_army_btn);
    s->lay->addLayout(row);
    al->addWidget(s);

    // --- unit types of the selected army (a setting for what's below)
    s = comp_section = new Section();
    comp_title = label(u"Unit types"_s, u"label"_s);
    s->lay->addWidget(comp_title);
    comp_host = new QWidget();
    comp_grid = new QGridLayout(comp_host);
    comp_grid->setContentsMargins(0, 0, 0, 0);
    comp_grid->setHorizontalSpacing(6);
    comp_grid->setVerticalSpacing(4);
    comp_grid->setColumnStretch(0, 1);
    s->lay->addWidget(comp_host);
    row = new QHBoxLayout();
    row->setSpacing(6);
    comp_add = new QPushButton(u"＋ Type"_s);
    comp_add->setToolTip(u"Another unit type in this army"_s);
    connect(comp_add, &QPushButton::clicked, this, [this] { _comp_row(std::nullopt, 1); });
    row->addWidget(comp_add);
    comp_total = label(u""_s, u"sub"_s);
    comp_total->setTextFormat(Qt::RichText);
    row->addWidget(comp_total, 1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    sort_types = new QCheckBox(u"Order by type"_s);
    sort_types->setChecked(true);
    sort_types->setToolTip(u"Artillery, then hussars, then infantry … like the mod's files"_s);
    row->addWidget(sort_types);
    row->addStretch(1);
    comp_btn = new QPushButton(u"Set types"_s);
    comp_btn->setToolTip(u"Change the army's brigades to these types (and counts) and name them with the "
                         u"scheme at the bottom"_s);
    connect(comp_btn, &QPushButton::clicked, this, [this] { apply_composition(); });
    row->addWidget(comp_btn);
    s->lay->addLayout(row);
    al->addWidget(s);

    // --- the selected army
    s = army_section = new Section();
    army_title = label(u""_s, u"title"_s);
    s->lay->addWidget(army_title);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    army_name = new QLineEdit();
    connect(army_name, &QLineEdit::editingFinished, this, [this] { _army_renamed(); });
    g->addWidget(army_name, 0, 1);
    g->addWidget(label(u"Location"_s, u"sub"_s, false), 1, 0);
    auto *lw = new QWidget();
    auto *lh = new QHBoxLayout(lw);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->setSpacing(6);
    army_loc = new widgets::ElideLabel(u""_s);
    lh->addWidget(army_loc, 1);
    army_move = new QPushButton(u"Move on map"_s);
    army_move->setToolTip(u"Then click the province the army starts in (Esc cancels)"_s);
    connect(army_move, &QPushButton::clicked, this, [this] { Q_EMIT moveRequested(u"army"_s); });
    lh->addWidget(army_move);
    g->addWidget(lw, 1, 1);
    s->lay->addLayout(g);
    s->lay->addWidget(label(u"Brigades"_s, u"sub"_s));
    brig_host = new QWidget();
    brig_grid = new QGridLayout(brig_host);
    brig_grid->setContentsMargins(0, 0, 0, 0);
    brig_grid->setHorizontalSpacing(6);
    brig_grid->setVerticalSpacing(3);
    brig_grid->setColumnStretch(0, 1);
    s->lay->addWidget(brig_host);
    near_btn = new widgets::WrapButton(u"＋ Brigade from the nearest state with free soldiers"_s);
    connect(near_btn, &QPushButton::clicked, this, [this] { attach_nearest(); });
    s->lay->addWidget(near_btn);
    army_hint = label(u"On the map every province with soldiers shows brigades raised / its cap (1/2, 2/5 …). "
                      u"Click a number to add a brigade from there to this army, right-click a province to "
                      u"take one of this army's away. Gold ring = room for more, a warning triangle = over the cap. Click "
                      u"the nation, the shield again or Esc for the whole nation."_s,
                      u"faint"_s);
    s->lay->addWidget(army_hint);
    al->addWidget(s);

    // --- naming
    s = name_section = new Section();
    s->lay->addWidget(label(u"Brigade names"_s));
    scheme = new QComboBox();
    scheme->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[k, t] : oob::NAME_SCHEMES)
        scheme->addItem(t, k);
    connect(scheme, &QComboBox::activated, this, [this](int) { _scheme_shown(); });
    s->lay->addWidget(scheme);
    pattern = new QLineEdit(u"{nth} {type} Regiment"_s);
    s->lay->addWidget(pattern);
    pattern_help = label(oob::PATTERN_HELP, u"faint"_s);
    s->lay->addWidget(pattern_help);
    rename_btn = new QPushButton(u"Rename this army's brigades"_s);
    connect(rename_btn, &QPushButton::clicked, this, [this] { rename_brigades(); });
    s->lay->addWidget(rename_btn);
    al->addWidget(s);
    army_leader = new leaderbox::LeaderBox(mod, u"land"_s);
    connect(army_leader, &leaderbox::LeaderBox::changed, this, [this](const QString &lb, const QStringList &extra) {
        _set_extra(u"armies"_s, sel_of(), lb, extra);
    });
    al->addWidget(army_leader);
    lay->addWidget(armies_box);

    // ============================================================ navies
    navies_box = new QWidget();
    auto *nl = new QVBoxLayout(navies_box);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->setSpacing(0);
    s = new Section();
    s->lay->addWidget(label(u"Navies"_s));
    navy_list = new QListWidget();
    navy_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navy_list->setTextElideMode(Qt::ElideRight);
    connect(navy_list, &QListWidget::currentRowChanged, this, [this](int r) { _navy_row(r); });
    s->lay->addWidget(navy_list);
    row = new QHBoxLayout();
    row->setSpacing(6);
    new_navy_btn = new QPushButton(u"＋ New navy"_s);
    connect(new_navy_btn, &QPushButton::clicked, this, [this] { new_navy(); });
    del_navy_btn = new QPushButton(u"Delete navy"_s);
    connect(del_navy_btn, &QPushButton::clicked, this, [this] { delete_navy(); });
    row->addWidget(new_navy_btn, 1);
    row->addWidget(del_navy_btn);
    s->lay->addLayout(row);
    nl->addWidget(s);

    s = navy_section = new Section();
    navy_title = label(u""_s, u"title"_s);
    s->lay->addWidget(navy_title);
    g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    navy_name = new QLineEdit();
    connect(navy_name, &QLineEdit::editingFinished, this, [this] { _navy_renamed(); });
    g->addWidget(navy_name, 0, 1);
    g->addWidget(label(u"Location"_s, u"sub"_s, false), 1, 0);
    lw = new QWidget();
    lh = new QHBoxLayout(lw);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->setSpacing(6);
    navy_loc = new widgets::ElideLabel(u""_s);
    lh->addWidget(navy_loc, 1);
    navy_move = new QPushButton(u"Move on map"_s);
    navy_move->setToolTip(u"Then click the port (a coastal province) the navy starts in (Esc cancels)"_s);
    connect(navy_move, &QPushButton::clicked, this, [this] { Q_EMIT moveRequested(u"navy"_s); });
    lh->addWidget(navy_move);
    g->addWidget(lw, 1, 1);
    s->lay->addLayout(g);
    s->lay->addWidget(label(u"Ships"_s, u"sub"_s));
    ship_host = new QWidget();
    ship_grid = new QGridLayout(ship_host);
    ship_grid->setContentsMargins(0, 0, 0, 0);
    ship_grid->setHorizontalSpacing(6);
    ship_grid->setVerticalSpacing(3);
    ship_grid->setColumnStretch(0, 1);
    s->lay->addWidget(ship_host);
    row = new QHBoxLayout();
    row->setSpacing(6);
    add_type = new QComboBox();
    add_type->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add_count = new widgets::Stepper(1, 99, 1);
    add_ship_btn = new QPushButton(u"＋ Add"_s);
    connect(add_ship_btn, &QPushButton::clicked, this, [this] { add_ships(); });
    row->addWidget(add_type, 1);
    row->addWidget(add_count);
    row->addWidget(add_ship_btn);
    s->lay->addLayout(row);
    nl->addWidget(s);

    s = ship_name_section = new Section();
    s->lay->addWidget(label(u"Ship names"_s));
    ship_scheme = new QComboBox();
    ship_scheme->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[k, t] : SHIP_SCHEMES)
        ship_scheme->addItem(t, k);
    connect(ship_scheme, &QComboBox::activated, this, [this](int) { _scheme_shown(); });
    s->lay->addWidget(ship_scheme);
    ship_pattern = new QLineEdit(u"{type} {n}"_s);
    s->lay->addWidget(ship_pattern);
    ship_help = label(SHIP_HELP, u"faint"_s);
    s->lay->addWidget(ship_help);
    s->lay->addWidget(label(u"New ships are named with this scheme."_s, u"faint"_s));
    ship_rename_btn = new QPushButton(u"Rename this navy's ships"_s);
    connect(ship_rename_btn, &QPushButton::clicked, this, [this] { rename_ships(); });
    s->lay->addWidget(ship_rename_btn);
    nl->addWidget(s);
    navy_leader = new leaderbox::LeaderBox(mod, u"sea"_s);
    connect(navy_leader, &leaderbox::LeaderBox::changed, this, [this](const QString &lb, const QStringList &extra) {
        _set_extra(u"navies"_s, nsel_of(), lb, extra);
    });
    nl->addWidget(navy_leader);
    lay->addWidget(navies_box);
    // (C++ only, a tester 2026-11-17) Generals / Admirals: every leader of the picked nation, the nations selected on
    // the map, or the world
    leaders = new leaderlist::LeaderList(mod);
    leaders->edit_fn = [this](const QString &l, const QString &t, const leaderlist::EditFn &fn) { Q_EMIT edit(l, t, fn); };
    connect(leaders, &leaderlist::LeaderList::scopeChanged, this, [this] { Q_EMIT overlayChanged(); });
    lay->addWidget(leaders);
    lay->addSpacing(fontMetrics().lineSpacing() + 4);
    lay->addStretch(1);
    _scheme_shown();
    set_sub(u"armies"_s, false);
}

// ------------------------------------------------------------------ helpers
const oob::OOB *ArmyPanel::st() const
{
    return oob && tag && !tag->isEmpty() ? &oob->state(*tag) : nullptr;
}

QString ArmyPanel::pname(const OptInt &pid) const
{
    const moddata::Province *pr = pid ? mod.provinces.get(*pid) : nullptr;
    if (pr) {
        const QString nm = py::strip(pr->name);
        return u"#%1 %2"_s.arg(*pid).arg(!nm.isEmpty() ? nm : u"(unnamed)"_s);
    }
    return u"#%1 ?"_s.arg(pid ? QString::number(*pid) : u"None"_s);
}

std::optional<QPointF> ArmyPanel::center(const OptInt &pid) const
{
    const mapview::MapData &md = mapdata;
    if (!pid || !(0 < *pid && *pid < md.n) || !md.area[size_t(*pid)])
        return std::nullopt;
    return QPointF(double(md.cx[size_t(*pid)]) + 0.5, double(md.cy[size_t(*pid)]) + 0.5);
}

OptInt ArmyPanel::sel_of() const
{
    const auto it = sel.find(tag.value_or(QString()));
    return it != sel.end() ? it->second : std::nullopt;
}

OptInt ArmyPanel::nsel_of() const
{
    const auto it = nsel.find(tag.value_or(QString()));
    return it != nsel.end() ? it->second : std::nullopt;
}

std::pair<OptInt, const oob::Army *> ArmyPanel::army() const
{
    const oob::OOB *s = st();
    const OptInt i = sel_of();
    if (s && i && 0 <= *i && *i < static_cast<long long>(s->armies.size()))
        return {i, &s->armies[size_t(*i)]};
    return {std::nullopt, nullptr};
}

std::pair<OptInt, const oob::Navy *> ArmyPanel::navy() const
{
    const oob::OOB *s = st();
    const OptInt i = nsel_of();
    if (s && i && 0 <= *i && *i < static_cast<long long>(s->navies.size()))
        return {i, &s->navies[size_t(*i)]};
    return {std::nullopt, nullptr};
}

void ArmyPanel::_emit(const QString &label_, const EditFn &fn)
{
    if (tag && !tag->isEmpty())
        Q_EMIT edit(label_, *tag, fn);
}

// The general / admiral of the army / navy picked (kept with the unit's other unmanaged text).
void ArmyPanel::_set_extra(const QString &kind, OptInt i, const QString &what, const QStringList &extra)
{
    const oob::OOB *s = st();
    if (!s || !i)
        return;
    const long long n = kind == u"armies" ? static_cast<long long>(s->armies.size())
                                          : static_cast<long long>(s->navies.size());
    if (!(0 <= *i && *i < n))
        return;
    const long long k = *i;
    const QString name = kind == u"armies" ? s->armies[size_t(k)].name : s->navies[size_t(k)].name;
    _emit(u"%1: %2"_s.arg(name, what), [kind, k, extra](oob::OOB &o) {
        if (kind == u"armies")
            o.armies[size_t(k)].extra = extra;
        else
            o.navies[size_t(k)].extra = extra;
    });
}

void ArmyPanel::set_sub(const QString &key0, bool emit)
{
    QString key = key0;
    if (key != u"armies" && key != u"navies" && key != u"generals" && key != u"admirals")   // (Unit stats: Warfare)
        key = u"armies"_s;
    sub = key;
    const bool sea = key == u"navies" || key == u"admirals";
    for (const auto &[k, b] : seg) {
        b->setChecked(k == key);
        b->setVisible((k == u"navies" || k == u"admirals") == sea);
    }
    armies_box->setVisible(key == u"armies");
    navies_box->setVisible(key == u"navies");
    rule_note->setVisible(key == u"armies");
    leaders->setVisible(key == u"generals" || key == u"admirals");
    leaders->set_kind(key == u"admirals" ? u"sea"_s : u"land"_s);
    if (emit) {
        _fill();
        Q_EMIT overlayChanged();
    }
    if (after_set_sub)
        after_set_sub();
}

void ArmyPanel::_scheme_shown()
{
    bool c = scheme->currentData().toString() == u"custom";
    pattern->setVisible(c);
    pattern_help->setVisible(c);
    c = ship_scheme->currentData().toString() == u"custom";
    ship_pattern->setVisible(c);
    ship_help->setVisible(c);
}

// ------------------------------------------------------------------ refresh
void ArmyPanel::refresh(document::Document *d, const OptStr &t)
{
    doc = d;
    oob = d && d->maps.contains(u"oob"_s) ? &d->map<oob::OOBEdits>(u"oob"_s) : nullptr;
    tag = t && mod.countries.contains(*t) ? t : std::nullopt;
    _fill();
}

void ArmyPanel::_fill()
{
    const long long nc = static_cast<long long>(mod.countries.size());
    if (!_psig || *_psig != nc) {
        _psig = nc;
        picker->fill(nationpanel::nation_items(mod), {}, tag);
    }
    const int pi = picker->find(tag);
    picker->blockSignals(true);
    picker->setCurrentIndex(pi >= 0 ? pi : -1);
    picker->blockSignals(false);
    if (leaders->isVisibleTo(this))
        leaders->refresh(doc, oob, tag);
    oob::UnitTypes *un = units();
    if (un && add_type->count() == 0) {
        const oob::SlotRules &r = *oob->rules;
        rule_note->setText(
            u"Brigade cap as the game counts it: every soldier pop on its own — under %1 it raises none, else 1 "
            u"plus 1 for every full %2. In a province without the owner's core a full step is %3× as big, in a "
            u"colony %4×, in a protectorate %5× (common/defines.lua).%6"_s.arg(
                widgets::fmt(static_cast<long long>(r.first.v)), widgets::fmt(static_cast<long long>(r.per.v)),
                py::fmt_g(r.noncore.v), py::fmt_g(r.colony.v), py::fmt_g(r.protectorate.v),
                !r.missing.isEmpty()
                    ? u" ⚠ Not found in defines.lua, counted with the game's usual value: %1."_s.arg(r.missing.join(u", "_s))
                    : QString()));
        for (const QString &k : un->naval())
            add_type->addItem(un->name(k), k);
        const int i = add_type->findData(u"frigate"_s);
        add_type->setCurrentIndex(std::max(0, i));
    }
    const bool has = tag.has_value() && oob;
    for (QWidget *w : {armies_box, navies_box})
        w->setEnabled(has);
    if (!has) {
        title->setText(u"Click a province of a nation on the map, or pick one above."_s);
        summary->setText(u""_s);
        file_note->setText(u""_s);
        army_list->clear();
        navy_list->clear();
        army_section->hide();
        comp_section->hide();
        name_section->hide();
        navy_section->hide();
        ship_name_section->hide();
        _slots = {};
        return;
    }
    const oob::OOB &s = oob->state(*tag);
    _slots = oob->state_slots(*doc, *tag);
    _pcap = oob::OOBEdits::province_caps(_slots);
    _used = oob->used(QStringList{*tag});
    _over = oob->over_cap(*tag, _pcap);
    const moddata::Country &c = *mod.countries.get(*tag);
    title->setText(u"%1  (%2)"_s.arg(c.name, *tag));
    long long nb = 0, ns = 0, cap = 0, sold = 0, free = 0;
    for (const oob::Army &a : s.armies)
        nb += static_cast<long long>(a.regiments.size());
    for (const oob::Navy &n : s.navies)
        ns += static_cast<long long>(n.ships.size());
    for (const auto &[_p, v] : _pcap)
        cap += v;
    for (const auto &[_k, e] : _slots)
        sold += e.soldiers;
    for (const auto &[p, c_] : _pcap)
        free += std::max(0LL, c_ - get0(_used, p));
    const long long over = static_cast<long long>(_over.size());
    const long long na = static_cast<long long>(s.armies.size()), nn = static_cast<long long>(s.navies.size());
    summary->setText(
        u"<b>Brigades %1 / %2</b> (cap from %3 soldiers) · <span style='color:rgb%4'>%5 free</span>%6<br>"
        u"%7 arm%8 · %9 nav%10 · %11 ship%12"_s.arg(QString::number(nb), QString::number(cap), widgets::fmt(sold),
                                                    rgb_str(GOLD_RING), QString::number(free),
                                                    over ? u" · <span style='color:%1'>%2 over their province's cap</span>"_s
                                                               .arg(theme::ERROR_)
                                                               .arg(over)
                                                         : QString(),
                                                    QString::number(na), na == 1 ? u"y"_s : u"ies"_s, QString::number(nn))
            .arg(nn == 1 ? u"y"_s : u"ies"_s, QString::number(ns), ns == 1 ? QString() : u"s"_s));
    const auto [path, ref] = oob->file_of(*tag);
    file_note->setText(ref && !ref->isEmpty() && oob->exists(*tag)
                           ? u"history/units/%1"_s.arg(*ref)
                           : u"No OOB file yet — Apply writes history/units/%1_oob.txt and adds it to the nation's "
                             u"history."_s.arg(*tag));
    _fill_armies(s);
    _fill_navies(oob->state(*tag));
}

// --- armies
QString ArmyPanel::_army_line(const oob::Army &a) const
{
    oob::UnitTypes *un = units();
    OrderedMap<QString, long long> cnt;
    for (const oob::Regiment &r : a.regiments)
        bump(cnt, un->word(r.type));
    QStringList comp;
    for (const auto &[w, n] : by_count(cnt))
        comp << u"%1 %2"_s.arg(n).arg(w);
    const long long nr = static_cast<long long>(a.regiments.size());
    return u"%1 — %2 brigade%3%4"_s.arg(!a.name.isEmpty() ? a.name : u"(no name)"_s, QString::number(nr),
                                        nr == 1 ? QString() : u"s"_s,
                                        !comp.isEmpty() ? u" · "_s + comp.join(u", "_s) : QString());
}

void ArmyPanel::_fill_armies(const oob::OOB &s)
{
    QListWidget *lst = army_list;
    lst->blockSignals(true);
    lst->clear();
    for (size_t i = 0; i < s.armies.size(); ++i) {
        const oob::Army &a = s.armies[i];
        auto *it = new QListWidgetItem(widgets::swatch_icon(army_color(static_cast<long long>(i)), 12), _army_line(a));
        it->setToolTip(u"%1\nat %2"_s.arg(_army_line(a), pname(a.location)));
        lst->addItem(it);
    }
    const auto [i, a] = army();
    if (!a)
        sel.erase(tag.value_or(QString()));
    lst->setCurrentRow(a ? int(*i) : -1);
    lst->blockSignals(false);
    const int rows = std::max(1, std::min(8, lst->count()));
    lst->setFixedHeight(rows * (lst->count() ? lst->sizeHintForRow(0) : 24) + 6);
    del_army_btn->setEnabled(a != nullptr);
    for (QWidget *w : {static_cast<QWidget *>(army_section), static_cast<QWidget *>(comp_section),
                       static_cast<QWidget *>(name_section)})
        w->setVisible(a != nullptr);
    army_leader->show_for(doc, a ? std::optional<QStringList>(a->extra) : std::nullopt, a ? a->name : QString());
    if (!a) {
        _rows_sig.reset();
        _comp_sig.reset();
        return;
    }
    army_title->setText(u"■ "_s + (!a->name.isEmpty() ? a->name : u"(no name)"_s));
    army_title->setStyleSheet(u"color: rgb%1;"_s.arg(rgb_str(army_color(*i))));
    if (!army_name->hasFocus())
        army_name->setText(a->name);
    const moddata::Province *pr = a->location ? mod.provinces.get(*a->location) : nullptr;
    const OptStr own = pr ? doc->prov_state(*a->location).owner : std::nullopt;
    army_loc->setText(a->location ? pname(a->location) + (own == tag ? QString() : u"  ⚠ not %1's"_s.arg(*tag))
                                  : u"—"_s);
    _fill_brigades(*i, *a);
    _fill_composition(*i, *a);
    bool any = false;
    for (const auto &[p, c_] : _pcap)
        if (c_ > get0(_used, p)) {
            any = true;
            break;
        }
    near_btn->setEnabled(any);
}

void ArmyPanel::_clear(QGridLayout *grid)
{
    while (grid->count()) {
        QLayoutItem *item = grid->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void ArmyPanel::_fill_brigades(long long ai, const oob::Army &a)
{
    QSet<long long> over;
    for (const auto &[x, ri] : _over)
        if (x == ai)
            over.insert(ri);
    std::vector<long long> ov(over.begin(), over.end());
    std::sort(ov.begin(), ov.end());
    RowsSig sig{tag, ai, {}, ov};
    for (const oob::Regiment &r : a.regiments)
        sig.regs.emplace_back(r.name, r.type, r.home);
    if (_rows_sig && *_rows_sig == sig)
        return;
    _rows_sig = sig;
    oob::UnitTypes *un = units();
    QGridLayout *g = brig_grid;
    _clear(g);
    if (a.regiments.empty()) {
        g->addWidget(label(u"No brigades yet — click a province's number on the map."_s, u"faint"_s), 0, 0, 1, 4);
        return;
    }
    const QStringList land = un->land();
    for (size_t r_i = 0; r_i < a.regiments.size(); ++r_i) {
        const oob::Regiment &r = a.regiments[r_i];
        const long long k = static_cast<long long>(r_i);
        auto *ed = new QLineEdit(r.name);
        ed->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(ed, &QLineEdit::editingFinished, this, [this, k, ed] { _reg_renamed(k, ed->text()); });
        QComboBox *cb = type_combo();
        QStringList ks = land;
        if (!land.contains(r.type))
            ks << r.type;
        for (const QString &t : ks)
            cb->addItem(un->name(t), t);
        cb->setCurrentIndex(std::max(0, cb->findData(r.type)));
        connect(cb, &QComboBox::activated, this, [this, k, cb](int) { _reg_type(k, cb->currentData().toString()); });
        auto *home = new widgets::ElideLabel(r.home ? py::split(pname(r.home), u" "_s, 1).last() : u"—"_s);
        home->setObjectName(u"sub"_s);
        home->setFixedWidth(88);
        const OptInt h = r.home;
        if (over.contains(k)) {
            home->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::ERROR_));
            const long long u = get0(_used, h);
            home->setToolTip(u"Raised from %1 — over its cap: it raises %2 brigade%3, its soldiers allow %4"_s.arg(
                pname(h), QString::number(u), u == 1 ? QString() : u"s"_s, QString::number(get0(_pcap, h))));
        } else {
            home->setToolTip(u"Raised from %1 (%2/%3)"_s.arg(pname(h)).arg(get0(_used, h)).arg(get0(_pcap, h)));
        }
        QPushButton *x = x_button(u"Remove this brigade"_s);
        connect(x, &QPushButton::clicked, this, [this, k] { _reg_remove(k); });
        g->addWidget(ed, int(r_i), 0);
        g->addWidget(cb, int(r_i), 1);
        g->addWidget(home, int(r_i), 2);
        g->addWidget(x, int(r_i), 3);
    }
    g->setColumnStretch(0, 3);
    g->setColumnStretch(1, 2);
    g->setColumnMinimumWidth(2, 88);
}

// One row per unit type of the army: type, −/＋ count, ✕ (kept while editing until the army changes).
void ArmyPanel::_fill_composition(long long ai, const oob::Army &a)
{
    comp_title->setText(u"Unit types — %1"_s.arg(!a.name.isEmpty() ? a.name : u"(no name)"_s));
    CompSig sig{tag, ai, {}};
    for (const oob::Regiment &r : a.regiments)
        sig.types << r.type;
    if (_comp_sig && *_comp_sig == sig) {
        _comp_total();
        return;
    }
    _comp_sig = sig;
    _clear(comp_grid);
    comp_rows.clear();
    OrderedMap<QString, long long> cnt;
    for (const oob::Regiment &r : a.regiments)
        bump(cnt, r.type);
    std::vector<std::pair<QString, long long>> items(cnt.begin(), cnt.end());
    std::sort(items.begin(), items.end());
    for (const auto &[k, n] : items)
        _comp_row(k, n);
    if (cnt.isEmpty())
        _comp_row(units()->types.contains(u"infantry"_s) ? OptStr(u"infantry"_s) : std::nullopt, 0);
    _comp_total();
}

void ArmyPanel::_comp_row(OptStr typ, long long n)
{
    oob::UnitTypes *un = units();
    QSet<QString> used;
    for (const CompRow &r : comp_rows)
        used.insert(r.cb->currentData().toString());
    const QStringList land = un->land();
    if (!typ) {
        for (const QString &k : land)
            if (!used.contains(k)) {
                typ = k;
                break;
            }
        if (!typ)
            typ = land.value(0);
    }
    QComboBox *cb = type_combo();
    QStringList ks = land;
    if (!land.contains(*typ))
        ks << *typ;
    for (const QString &k : ks)
        cb->addItem(un->name(k), k);
    cb->setCurrentIndex(std::max(0, cb->findData(*typ)));
    connect(cb, &QComboBox::activated, this, [this](int) { _comp_total(); });
    auto *st_ = new widgets::Stepper(0, 60, n);
    connect(st_, &widgets::Stepper::valueChanged, this, [this](int) { _comp_total(); });
    QPushButton *x = x_button(u"Not this type"_s);
    connect(x, &QPushButton::clicked, this, [this, x] { _comp_drop(x); });
    const int i = static_cast<int>(comp_rows.size());
    comp_grid->addWidget(cb, i, 0);
    comp_grid->addWidget(st_, i, 1);
    comp_grid->addWidget(x, i, 2);
    comp_rows.push_back({cb, st_, x});
    _comp_total();
}

void ArmyPanel::_comp_drop(QPushButton *x)
{
    std::vector<std::pair<QString, long long>> keep;
    for (const CompRow &r : comp_rows)
        if (r.x != x)
            keep.emplace_back(r.cb->currentData().toString(), r.st->value());
    _clear(comp_grid);
    comp_rows.clear();
    for (const auto &[k, n] : keep)
        _comp_row(k, n);
    _comp_total();
}

OrderedMap<QString, long long> ArmyPanel::_comp_want() const
{
    OrderedMap<QString, long long> want;
    for (const CompRow &r : comp_rows)
        if (r.st->value() > 0) {
            const QString k = r.cb->currentData().toString();
            const long long *v = want.get(k);
            want.insert(k, (v ? *v : 0) + r.st->value());
        }
    return want;
}

void ArmyPanel::_comp_total()
{
    const auto [i, a] = army();
    long long n = 0;
    for (const auto &[_k, v] : _comp_want())
        n += v;
    comp_add->setEnabled(comp_rows.size() < 8);
    const long long have = a ? static_cast<long long>(a->regiments.size()) : 0;
    if (n == have)
        comp_total->setText(u"%1 brigade%2"_s.arg(n).arg(n == 1 ? QString() : u"s"_s));
    else if (n < have)
        comp_total->setText(u"<span style='color:%1'>%2 of %3 brigades — Set types removes %4</span>"_s.arg(
            theme::ERROR_, QString::number(n), QString::number(have), QString::number(have - n)));
    else
        comp_total->setText(u"<span style='color:rgb%1'>%2 of %3 — Set types adds %4 from the nearest "
                            u"states with free soldiers</span>"_s.arg(rgb_str(GOLD_RING), QString::number(n),
                                                                     QString::number(have), QString::number(n - have)));
}

// --- army list / fields
void ArmyPanel::_army_row(int r)
{
    sel[tag.value_or(QString())] = r >= 0 ? OptInt(r) : std::nullopt;
    _rows_sig.reset();
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::select_army(long long i)
{
    sel[tag.value_or(QString())] = i;
    _rows_sig.reset();
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::new_army()
{
    const oob::OOB *s = st();
    if (!s)
        return;
    OptInt loc;
    OptStr cap;
    if (doc->maps.contains(u"nations"_s))
        cap = doc->map<countryhist::NationEdits>(u"nations"_s).state(*tag).at(u"capital"_s);
    if (cap && !cap->isEmpty() && py::isdigit(*cap) && mod.provinces.contains(py::to_int(*cap)))
        loc = py::to_int(*cap);
    const QString name = oob::next_army_name(s->armies);
    const long long idx = static_cast<long long>(s->armies.size());
    _emit(u"New army %1 (%2)"_s.arg(name, *tag),
          [name, loc](oob::OOB &o) { o.armies.push_back(oob::Army{name, loc, {}, {}, std::nullopt, std::nullopt}); });
    select_army(idx);
    Q_EMIT message(u"%1 made at %2 — click gold rings on the map to give it brigades; Move on map places it."_s.arg(
        name, loc ? pname(loc) : u"no location"_s));
}

void ArmyPanel::delete_army()
{
    const auto [i, a] = army();
    if (!a)
        return;
    const long long k = *i;
    _emit(u"Delete %1 (%2)"_s.arg(a->name, *tag), [k](oob::OOB &o) { o.armies.erase(o.armies.begin() + k); });
    sel[tag.value_or(QString())] = std::nullopt;
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::_army_renamed()
{
    const auto [i, a] = army();
    const QString name = py::strip(army_name->text()).replace(u'"', u'\'');
    if (!a || name.isEmpty() || name == a->name)
        return;
    const long long k = *i;
    _emit(u"Rename army → %1"_s.arg(name), [k, name](oob::OOB &o) { o.armies[size_t(k)].name = name; });
}

// The location picked on the map (Move on map).
void ArmyPanel::set_location(long long pid)
{
    if (sub == u"navies") {
        const auto [i, n] = navy();
        if (n) {
            const long long k = *i;
            _emit(u"Move %1 → %2"_s.arg(n->name, pname(pid)), [k, pid](oob::OOB &o) { o.navies[size_t(k)].location = pid; });
        }
        return;
    }
    const auto [i, a] = army();
    if (a) {
        const long long k = *i;
        _emit(u"Move %1 → %2"_s.arg(a->name, pname(pid)), [k, pid](oob::OOB &o) { o.armies[size_t(k)].location = pid; });
    }
}

// --- brigades
// A name for one more brigade of `typ` (the current scheme; 'keep' / custom continue the numbering).
QString ArmyPanel::_new_name(const std::vector<oob::Regiment> &regs, const QString &typ, long long ai) const
{
    oob::UnitTypes *un = units();
    OrderedMap<QString, long long> start;
    const oob::OOB &s = *st();
    if (scheme->currentData().toString() == u"nation")
        for (size_t j = 0; j < s.armies.size(); ++j)
            if (static_cast<long long>(j) != ai)
                for (const oob::Regiment &r : s.armies[j].regiments)
                    bump(start, un->word(r.type));
    const oob::Army &a = s.armies[size_t(ai)];
    std::vector<oob::Regiment> all = regs;
    all.push_back(oob::Regiment{QString(), typ, std::nullopt});
    const QStringList names = oob::name_regiments(all, *un, scheme->currentData().toString(), pattern->text(), a.name,
                                                  start, &mod.provinces);
    return names.last();
}

// One more brigade raised in province `home` for the selected army.
void ArmyPanel::attach(long long home)
{
    const auto [i, a] = army();
    if (!a) {
        Q_EMIT message(u"Select an army first (or make one with ＋ New army), then click a province's number."_s);
        return;
    }
    if (get0(_pcap, home) <= get0(_used, home)) {
        Q_EMIT message(u"%1: no soldiers left for another brigade (%2/%3)."_s.arg(pname(home))
                           .arg(get0(_used, home))
                           .arg(get0(_pcap, home)));
        return;
    }
    OrderedMap<QString, long long> cnt;
    for (const oob::Regiment &r : a->regiments)
        bump(cnt, r.type);
    const QString typ = !cnt.isEmpty() ? by_count(cnt).front().first : u"infantry"_s;
    const QString name = _new_name(a->regiments, typ, *i);
    const QString an = a->name;
    const long long k = *i;
    _emit(u"%1: brigade from %2"_s.arg(an, pname(home)),
          [k, name, typ, home](oob::OOB &o) { o.armies[size_t(k)].regiments.push_back(oob::Regiment{name, typ, home}); });
    Q_EMIT message(u"%1: + %2 (%3) from %4 — Ctrl+Z undoes it."_s.arg(an, name, units()->name(typ), pname(home)));
}

bool ArmyPanel::detach(long long pid)
{
    const auto [i, a] = army();
    if (!a)
        return false;
    std::vector<long long> idx;
    for (size_t k = 0; k < a->regiments.size(); ++k)
        if (a->regiments[k].home == pid)
            idx.push_back(static_cast<long long>(k));
    if (idx.empty())
        return false;
    const long long k = idx.back(), ai = *i;
    const QString name = a->regiments[size_t(k)].name, an = a->name;
    _emit(u"%1: remove %2"_s.arg(an, name),
          [ai, k](oob::OOB &o) { o.armies[size_t(ai)].regiments.erase(o.armies[size_t(ai)].regiments.begin() + k); });
    Q_EMIT message(u"%1: removed %2 (from %3) — Ctrl+Z undoes it."_s.arg(an, name, pname(pid)));
    return true;
}

void ArmyPanel::attach_nearest()
{
    const auto [i, a] = army();
    if (!a)
        return;
    const oob::Centers ctr{[this](long long p) { return mapdata.cx[size_t(p)]; },
                           [this](long long p) { return mapdata.cy[size_t(p)]; }, mapdata.n};
    const QList<long long> homes = oob->new_homes(*doc, ctr, *tag, a->location, 1, _slots);
    if (homes.isEmpty()) {
        Q_EMIT message(u"No province of %1 has soldiers for more brigades."_s.arg(*tag));
        return;
    }
    attach(homes[0]);
}

// Right-click on the map: one brigade of the selected army from that province goes.
void ArmyPanel::right_clicked(long long pid)
{
    const auto [i, a] = army();
    if (!a)
        return;
    const QString an = a->name;
    if (!detach(pid))
        Q_EMIT message(u"%1 has no brigade from %2."_s.arg(an, pname(pid)));
}

// No army selected: the whole nation on the map.
bool ArmyPanel::deselect()
{
    const QString key = tag.value_or(QString());
    if (sub == u"navies") {
        if (!nsel_of())
            return false;
        nsel[key] = std::nullopt;
        _nrows_sig.reset();
    } else {
        if (!sel_of())
            return false;
        sel[key] = std::nullopt;
        _rows_sig.reset();
    }
    _fill();
    Q_EMIT overlayChanged();
    return true;
}

void ArmyPanel::_reg_renamed(long long k, const QString &text0)
{
    const auto [i, a] = army();
    const QString text = py::strip(text0).replace(u'"', u'\'');
    if (!a || k >= static_cast<long long>(a->regiments.size()) || text == a->regiments[size_t(k)].name)
        return;
    const long long ai = *i;
    _emit(u"Rename brigade → %1"_s.arg(text),
          [ai, k, text](oob::OOB &o) { o.armies[size_t(ai)].regiments[size_t(k)].name = text; });
}

void ArmyPanel::_reg_type(long long k, const QString &typ)
{
    const auto [i, a] = army();
    if (!a || k >= static_cast<long long>(a->regiments.size()) || typ == a->regiments[size_t(k)].type)
        return;
    const long long ai = *i;
    _emit(u"Brigade type → %1"_s.arg(typ),
          [ai, k, typ](oob::OOB &o) { o.armies[size_t(ai)].regiments[size_t(k)].type = typ; });
}

void ArmyPanel::_reg_remove(long long k)
{
    const auto [i, a] = army();
    if (!a || k >= static_cast<long long>(a->regiments.size()))
        return;
    const long long ai = *i;
    _emit(u"%1: remove %2"_s.arg(a->name, a->regiments[size_t(k)].name),
          [ai, k](oob::OOB &o) { o.armies[size_t(ai)].regiments.erase(o.armies[size_t(ai)].regiments.begin() + k); });
}

void ArmyPanel::apply_composition()
{
    const auto [i, a] = army();
    if (!a)
        return;
    const OrderedMap<QString, long long> want = _comp_want();
    std::vector<oob::Regiment> regs = a->regiments;
    long long n = 0;
    for (const auto &[_k, v] : want)
        n += v;
    // fewer: the last ones go; more: from the nearest states with free soldiers
    if (n < static_cast<long long>(regs.size())) {
        regs.resize(size_t(n));
    } else if (n > static_cast<long long>(regs.size())) {
        const long long need = n - static_cast<long long>(regs.size());
        const oob::Centers ctr{[this](long long p) { return mapdata.cx[size_t(p)]; },
                               [this](long long p) { return mapdata.cy[size_t(p)]; }, mapdata.n};
        const QList<long long> homes = oob->new_homes(*doc, ctr, *tag, a->location, need, _slots);
        for (long long h : homes)
            regs.push_back(oob::Regiment{QString(), QString(), h});
        const long long got = homes.size();
        if (got < need)
            Q_EMIT message(u"⚠ Only %1 more brigade%2 possible — the soldiers of %3's states are used up."_s.arg(
                QString::number(got), got == 1 ? QString() : u"s"_s, *tag));
    }
    // keep types that still fit, give the rest the missing ones
    OrderedMap<QString, long long> left = want;
    std::vector<oob::Regiment *> todo;
    for (oob::Regiment &r : regs) {
        long long *l = left.get(r.type);
        if (l && *l > 0)
            --*l;
        else
            todo.push_back(&r);
    }
    QStringList missing;
    for (const auto &[k, _v] : want) {
        const long long *l = left.get(k);
        for (long long j = 0; j < (l ? *l : 0); ++j)
            missing << k;
    }
    for (size_t j = 0; j < todo.size() && j < size_t(missing.size()); ++j)
        todo[j]->type = missing[qsizetype(j)];
    if (sort_types->isChecked())
        std::stable_sort(regs.begin(), regs.end(),
                         [](const oob::Regiment &x, const oob::Regiment &y) { return x.type < y.type; });
    const QStringList names = _scheme_names(*i, regs);
    for (size_t j = 0; j < regs.size() && j < size_t(names.size()); ++j)
        regs[j].name = names[qsizetype(j)];
    const QString an = a->name;
    const long long ai = *i;
    _emit(u"%1: unit types"_s.arg(an), [ai, regs](oob::OOB &o) { o.armies[size_t(ai)].regiments = regs; });
    QStringList parts;
    for (const auto &[k, v] : want)
        parts << u"%1 %2"_s.arg(v).arg(units()->name(k));
    Q_EMIT message(u"%1: %2 — named with '%3'."_s.arg(
        an, !parts.isEmpty() ? parts.join(u", "_s) : u"no brigades"_s,
        py::split(py::split(scheme->currentText(), u" — "_s)[0], u" ("_s)[0]));
}

QStringList ArmyPanel::_scheme_names(long long ai, const std::vector<oob::Regiment> &regs) const
{
    oob::UnitTypes *un = units();
    OrderedMap<QString, long long> start;
    const oob::OOB &s = *st();
    if (scheme->currentData().toString() == u"nation")
        for (long long j = 0; j < ai && j < static_cast<long long>(s.armies.size()); ++j)
            for (const oob::Regiment &r : s.armies[size_t(j)].regiments)
                bump(start, un->word(r.type));
    return oob::name_regiments(regs, *un, scheme->currentData().toString(), pattern->text(),
                               s.armies[size_t(ai)].name, start, &mod.provinces);
}

void ArmyPanel::rename_brigades()
{
    const auto [i, a] = army();
    if (!a)
        return;
    const QStringList names = _scheme_names(*i, a->regiments);
    const long long ai = *i;
    _emit(u"%1: rename brigades"_s.arg(a->name), [ai, names](oob::OOB &o) {
        std::vector<oob::Regiment> &rs = o.armies[size_t(ai)].regiments;
        for (size_t j = 0; j < rs.size() && j < size_t(names.size()); ++j)
            rs[j].name = names[qsizetype(j)];
    });
}

// ------------------------------------------------------------------ navies
QString ArmyPanel::_navy_line(const oob::Navy &n) const
{
    oob::UnitTypes *un = units();
    OrderedMap<QString, long long> cnt;
    for (const oob::Ship &s : n.ships)
        bump(cnt, un->name(s.type));
    QStringList comp;
    for (const auto &[w, v] : by_count(cnt))
        comp << u"%1 %2"_s.arg(v).arg(w);
    const long long ns = static_cast<long long>(n.ships.size());
    return u"%1 — %2 ship%3%4"_s.arg(!n.name.isEmpty() ? n.name : u"(no name)"_s, QString::number(ns),
                                    ns == 1 ? QString() : u"s"_s,
                                    !comp.isEmpty() ? u" · "_s + comp.join(u", "_s) : QString());
}

void ArmyPanel::_fill_navies(const oob::OOB &s)
{
    QListWidget *lst = navy_list;
    lst->blockSignals(true);
    lst->clear();
    for (const oob::Navy &n : s.navies) {
        auto *it = new QListWidgetItem(widgets::swatch_icon(Rgb{120, 170, 230}, 12), _navy_line(n));
        it->setToolTip(u"%1\nat %2"_s.arg(_navy_line(n), pname(n.location)));
        lst->addItem(it);
    }
    const auto [i, n] = navy();
    if (!n)
        nsel.erase(tag.value_or(QString()));
    lst->setCurrentRow(n ? int(*i) : -1);
    lst->blockSignals(false);
    const int rows = std::max(1, std::min(8, lst->count()));
    lst->setFixedHeight(rows * (lst->count() ? lst->sizeHintForRow(0) : 24) + 6);
    del_navy_btn->setEnabled(n != nullptr);
    for (QWidget *w : {static_cast<QWidget *>(navy_section), static_cast<QWidget *>(ship_name_section)})
        w->setVisible(n != nullptr);
    navy_leader->show_for(doc, n ? std::optional<QStringList>(n->extra) : std::nullopt, n ? n->name : QString());
    if (!n) {
        _nrows_sig.reset();
        return;
    }
    navy_title->setText(u"⚓ "_s + (!n->name.isEmpty() ? n->name : u"(no name)"_s));
    if (!navy_name->hasFocus())
        navy_name->setText(n->name);
    const OptInt loc = n->location;
    QString warn;
    if (loc && mod.provinces.contains(*loc) && !mod.provinces.get(*loc)->sea) {
        const auto &coastal = doc->coastal;
        if (coastal && *loc < static_cast<long long>(coastal->size()) && !(*coastal)[size_t(*loc)])
            warn = u"  ⚠ not coastal"_s;
    }
    navy_loc->setText(loc ? pname(loc) + warn : u"—"_s);
    _fill_ships(*i, *n);
}

void ArmyPanel::_fill_ships(long long ni, const oob::Navy &n)
{
    ShipsSig sig{tag, ni, {}};
    for (const oob::Ship &s : n.ships)
        sig.ships.emplace_back(s.name, s.type);
    if (_nrows_sig && *_nrows_sig == sig)
        return;
    _nrows_sig = sig;
    oob::UnitTypes *un = units();
    QGridLayout *g = ship_grid;
    _clear(g);
    if (n.ships.empty()) {
        g->addWidget(label(u"No ships yet — add some below."_s, u"faint"_s), 0, 0, 1, 3);
        return;
    }
    const QStringList naval = un->naval();
    for (size_t j = 0; j < n.ships.size(); ++j) {
        const oob::Ship &s = n.ships[j];
        const long long k = static_cast<long long>(j);
        auto *ed = new QLineEdit(s.name);
        ed->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(ed, &QLineEdit::editingFinished, this, [this, k, ed] { _ship_renamed(k, ed->text()); });
        QComboBox *cb = type_combo();
        QStringList ks = naval;
        if (!naval.contains(s.type))
            ks << s.type;
        for (const QString &t : ks)
            cb->addItem(un->name(t), t);
        cb->setCurrentIndex(std::max(0, cb->findData(s.type)));
        connect(cb, &QComboBox::activated, this, [this, k, cb](int) { _ship_type(k, cb->currentData().toString()); });
        QPushButton *x = x_button(u"Remove this ship"_s);
        connect(x, &QPushButton::clicked, this, [this, k] { _ship_remove(k); });
        g->addWidget(ed, int(j), 0);
        g->addWidget(cb, int(j), 1);
        g->addWidget(x, int(j), 2);
    }
    g->setColumnStretch(0, 3);
    g->setColumnStretch(1, 2);
}

void ArmyPanel::_navy_row(int r)
{
    nsel[tag.value_or(QString())] = r >= 0 ? OptInt(r) : std::nullopt;
    _nrows_sig.reset();
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::select_navy(long long i)
{
    nsel[tag.value_or(QString())] = i;
    _nrows_sig.reset();
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::new_navy()
{
    const oob::OOB *s = st();
    if (!s)
        return;
    OptStr cap;
    if (doc->maps.contains(u"nations"_s))
        cap = doc->map<countryhist::NationEdits>(u"nations"_s).state(*tag).at(u"capital"_s);
    const OptInt loc = cap && !cap->isEmpty() && py::isdigit(*cap) && mod.provinces.contains(py::to_int(*cap))
                           ? OptInt(py::to_int(*cap))
                           : std::nullopt;
    const QString *adjp = mod.loc.get(*tag + u"_ADJ"_s);
    const QString adj = adjp && !adjp->isEmpty() ? *adjp : mod.countries.get(*tag)->name;
    QSet<QString> names;
    for (const oob::Navy &n : s->navies)
        names.insert(n.name);
    QString name = u"%1 Navy"_s.arg(adj);
    long long k = 2;
    while (names.contains(name)) {
        name = u"%1 Navy %2"_s.arg(adj).arg(k);
        ++k;
    }
    const long long idx = static_cast<long long>(s->navies.size());
    _emit(u"New navy %1 (%2)"_s.arg(name, *tag),
          [name, loc](oob::OOB &o) { o.navies.push_back(oob::Navy{name, loc, {}, {}, std::nullopt, std::nullopt}); });
    select_navy(idx);
    Q_EMIT message(u"%1 made — add ships, and Move on map puts it in a port."_s.arg(name));
}

void ArmyPanel::delete_navy()
{
    const auto [i, n] = navy();
    if (!n)
        return;
    const long long k = *i;
    _emit(u"Delete %1 (%2)"_s.arg(n->name, *tag), [k](oob::OOB &o) { o.navies.erase(o.navies.begin() + k); });
    nsel[tag.value_or(QString())] = std::nullopt;
    _fill();
    Q_EMIT overlayChanged();
}

void ArmyPanel::_navy_renamed()
{
    const auto [i, n] = navy();
    const QString name = py::strip(navy_name->text()).replace(u'"', u'\'');
    if (!n || name.isEmpty() || name == n->name)
        return;
    const long long k = *i;
    _emit(u"Rename navy → %1"_s.arg(name), [k, name](oob::OOB &o) { o.navies[size_t(k)].name = name; });
}

// Names for `ships` (all of the navy's, or the last `only_new` of them).
QStringList ArmyPanel::_ship_names(long long ni, const std::vector<oob::Ship> &ships, long long only_new) const
{
    const oob::OOB &s = *st();
    const QString sch = ship_scheme->currentData().toString();
    oob::UnitTypes *un = units();
    QStringList out;
    const long long ns = static_cast<long long>(ships.size());
    if (sch == u"ship") {
        std::vector<oob::Navy> others;
        for (size_t j = 0; j < s.navies.size(); ++j)
            if (static_cast<long long>(j) != ni)
                others.push_back(s.navies[j]);
        oob::Navy base;
        if (only_new)
            base.ships.assign(ships.begin(), ships.begin() + std::max(0LL, ns - only_new));
        others.push_back(base);
        for (long long k : oob::next_ship_numbers(others, !only_new ? ns : only_new))
            out << u"Ship %1"_s.arg(k);
    } else {
        OrderedMap<QString, long long> cnt;
        const QString nav_name = s.navies[size_t(ni)].name;
        for (const oob::Ship &sh : ships) {
            bump(cnt, sh.type);
            const long long n = *cnt.get(sh.type);
            if (sch == u"custom")
                out << QString(ship_pattern->text())
                           .replace(u"{n}"_s, QString::number(n))
                           .replace(u"{type}"_s, un->name(sh.type))
                           .replace(u"{navy}"_s, nav_name);
            else
                out << u"%1 %2"_s.arg(un->name(sh.type)).arg(n);
        }
        if (only_new)
            out = out.mid(std::max<qsizetype>(0, out.size() - only_new));
    }
    return out;
}

void ArmyPanel::add_ships()
{
    const auto [i, n] = navy();
    if (!n)
        return;
    const QString typ = add_type->currentData().toString();
    const long long k = add_count->value();
    std::vector<oob::Ship> ships = n->ships;
    for (long long j = 0; j < k; ++j)
        ships.push_back(oob::Ship{QString(), typ});
    const QStringList names = _ship_names(*i, ships, k);
    const size_t from = ships.size() - size_t(k);
    for (size_t j = 0; j < size_t(k) && j < size_t(names.size()); ++j)
        ships[from + j].name = names[qsizetype(j)];
    const long long ni = *i;
    _emit(u"%1: + %2 %3"_s.arg(n->name).arg(k).arg(units()->name(typ)),
          [ni, ships](oob::OOB &o) { o.navies[size_t(ni)].ships = ships; });
}

void ArmyPanel::_ship_renamed(long long k, const QString &text0)
{
    const auto [i, n] = navy();
    const QString text = py::strip(text0).replace(u'"', u'\'');
    if (!n || k >= static_cast<long long>(n->ships.size()) || text == n->ships[size_t(k)].name)
        return;
    const long long ni = *i;
    _emit(u"Rename ship → %1"_s.arg(text), [ni, k, text](oob::OOB &o) { o.navies[size_t(ni)].ships[size_t(k)].name = text; });
}

void ArmyPanel::_ship_type(long long k, const QString &typ)
{
    const auto [i, n] = navy();
    if (!n || k >= static_cast<long long>(n->ships.size()) || typ == n->ships[size_t(k)].type)
        return;
    const long long ni = *i;
    _emit(u"Ship type → %1"_s.arg(typ), [ni, k, typ](oob::OOB &o) { o.navies[size_t(ni)].ships[size_t(k)].type = typ; });
}

void ArmyPanel::_ship_remove(long long k)
{
    const auto [i, n] = navy();
    if (!n || k >= static_cast<long long>(n->ships.size()))
        return;
    const long long ni = *i;
    _emit(u"%1: remove %2"_s.arg(n->name, n->ships[size_t(k)].name),
          [ni, k](oob::OOB &o) { o.navies[size_t(ni)].ships.erase(o.navies[size_t(ni)].ships.begin() + k); });
}

void ArmyPanel::rename_ships()
{
    const auto [i, n] = navy();
    if (!n)
        return;
    const QStringList names = _ship_names(*i, n->ships);
    const long long ni = *i;
    _emit(u"%1: rename ships"_s.arg(n->name), [ni, names](oob::OOB &o) {
        std::vector<oob::Ship> &ss = o.navies[size_t(ni)].ships;
        for (size_t j = 0; j < ss.size() && j < size_t(names.size()); ++j)
            ss[j].name = names[qsizetype(j)];
    });
}

// ------------------------------------------------------------------ map
std::vector<mapview::Shape> ArmyPanel::shapes() const
{
    const oob::OOB *s = st();
    if (!s)
        return {};
    return sub == u"navies" || sub == u"admirals" ? _navy_shapes(*s) : _army_shapes(*s);
}

std::vector<mapview::Shape> ArmyPanel::_army_shapes(const oob::OOB &s) const
{
    std::vector<mapview::Shape> out, marks;
    const OptInt sel_i = army().first;
    OrderedMap<long long, OrderedMap<long long, long long>> per_home;   // home -> Counter(army index)
    for (size_t i = 0; i < s.armies.size(); ++i)
        for (const oob::Regiment &r : s.armies[i].regiments)
            if (r.home) {
                if (!per_home.contains(*r.home))
                    per_home.insert(*r.home, {});
                OrderedMap<long long, long long> &c = *per_home.get(*r.home);
                const long long ii = static_cast<long long>(i);
                if (long long *v = c.get(ii))
                    ++*v;
                else
                    c.insert(ii, 1);
            }
    // the selected army's brigades: lines to where it stands
    if (sel_i) {
        const oob::Army &a = s.armies[size_t(*sel_i)];
        if (const auto c = center(a.location)) {
            grid::PySet<long long> homes;
            for (const oob::Regiment &r : a.regiments)
                if (r.home)
                    homes.add(*r.home);
            for (long long pid : homes.items())
                if (const auto h = center(pid)) {
                    mapview::Shape sh;
                    sh.kind = u"line"_s;
                    sh.pts = {*h, *c};
                    sh.color = army_color(*sel_i);
                    sh.width = 2.2;
                    sh.dash = true;
                    out.push_back(sh);
                }
        }
    }
    // every province with soldiers (or brigades): "used/cap", centred on the province
    std::vector<long long> pk, hk;
    for (const auto &[p, _c] : _pcap)
        pk.push_back(p);
    for (const auto &[p, _c] : per_home)
        hk.push_back(p);
    grid::PySet<long long> all = grid::PySet<long long>::copy_of(grid::PySet<long long>::from_keys(pk));
    all.merge(grid::PySet<long long>::from_keys(hk));
    for (long long pid : all.items()) {
        const auto h = center(pid);
        if (!h)
            continue;
        const OrderedMap<long long, long long> *cp = per_home.get(pid);
        const OrderedMap<long long, long long> cnt = cp ? *cp : OrderedMap<long long, long long>{};
        long long u = 0;
        for (const auto &[_k, v] : cnt)
            u += v;
        const long long c = get0(_pcap, pid);
        if (u == 0 && c == 0)
            continue;                              // a few soldiers, but the state's brigades are raised elsewhere
        const QString text = u"%1/%2"_s.arg(u).arg(c);
        mapview::Shape sh;
        sh.kind = u"badge"_s;
        sh.pts = {*h};
        sh.size = 9.0;
        sh.text = text;
        sh.tag = QVariantList{u"prov"_s, pid};
        if (u == 0) {
            sh.color = GOLD_RING;
            sh.hollow = true;
        } else {
            const bool has_sel = sel_i && cnt.contains(*sel_i);
            long long main = 0;
            if (has_sel) {
                main = *sel_i;
            } else {                               // cnt.most_common(1)[0][0]: the first of the most
                long long best = -1;
                for (const auto &[k, v] : cnt)
                    if (v > best) {
                        best = v;
                        main = k;
                    }
            }
            Rgb col = army_color(main);
            if (sel_i && !has_sel)
                col = dim(col);
            // over the cap: the same colours, but a warning triangle instead of the pill
            sh.color = col;
            sh.halo = has_sel;
            if (u < c)
                sh.ring = GOLD_RING;
            sh.warn = u > c;
        }
        marks.push_back(sh);
    }
    // armies: shields (several in one province side by side)
    std::map<OptInt, long long> at;
    for (size_t i = 0; i < s.armies.size(); ++i) {
        const oob::Army &a = s.armies[i];
        auto c = center(a.location);
        if (!c)
            continue;
        const long long j = at[a.location];
        at[a.location] += 1;
        const QPointF p(c->x() + 9.0 * double(j), c->y() - 3.0);
        QString num;
        for (const QChar ch : a.name)
            if (ch.isDigit())
                num += ch;
        num = num.left(3);
        if (num.isEmpty())
            num = QString::number(i + 1);
        mapview::Shape sh;
        sh.kind = u"army"_s;
        sh.pts = {p};
        sh.color = army_color(static_cast<long long>(i));
        sh.size = 9.0;
        sh.text = num;
        sh.tag = QVariantList{u"army"_s, static_cast<long long>(i)};
        sh.halo = sel_i && static_cast<long long>(i) == *sel_i;
        marks.push_back(sh);
        mapview::Shape lb;
        lb.kind = u"label"_s;
        lb.pts = {p};
        lb.color = Rgb{235, 235, 235};
        lb.size = 9.0;
        lb.dy = 22;
        lb.text = a.name;
        lb.min_scale = 0.9;
        marks.push_back(lb);
    }
    out.insert(out.end(), marks.begin(), marks.end());
    return out;
}

std::vector<mapview::Shape> ArmyPanel::_navy_shapes(const oob::OOB &s) const
{
    std::vector<mapview::Shape> out;
    const OptInt sel_i = navy().first;
    std::map<OptInt, long long> at;
    for (size_t i = 0; i < s.navies.size(); ++i) {
        const oob::Navy &n = s.navies[i];
        auto c = center(n.location);
        if (!c)
            continue;
        const long long j = at[n.location];
        at[n.location] += 1;
        const QPointF p(c->x() + 9.0 * double(j), c->y());
        mapview::Shape sh;
        sh.kind = u"anchor"_s;
        sh.pts = {p};
        sh.color = Rgb{150, 196, 250};
        sh.size = 9.0;
        sh.tag = QVariantList{u"navy"_s, static_cast<long long>(i)};
        sh.halo = sel_i && static_cast<long long>(i) == *sel_i;
        out.push_back(sh);
        mapview::Shape lb;
        lb.kind = u"label"_s;
        lb.pts = {p};
        lb.color = Rgb{210, 228, 250};
        lb.size = 9.0;
        lb.dy = 22;
        lb.text = u"%1 (%2)"_s.arg(n.name).arg(n.ships.size());
        lb.min_scale = 0.9;
        out.push_back(lb);
    }
    return out;
}

void ArmyPanel::map_clicked(const QVariantList &t)
{
    const QString kind = t.value(0).toString();
    if (kind == u"army") {
        const long long i = t.value(1).toLongLong();
        if (sel_of() == i)
            deselect();                            // the selected army's shield again: the whole nation
        else
            select_army(i);
    } else if (kind == u"navy") {
        const long long i = t.value(1).toLongLong();
        if (nsel_of() == i)
            deselect();
        else
            select_navy(i);
    } else if (kind == u"prov") {
        const long long pid = t.value(1).toLongLong();
        const auto [i, a] = army();
        if (a) {
            attach(pid);
        } else {
            const oob::OOB &s = *st();
            OptInt first;
            for (size_t k = 0; k < s.armies.size() && !first; ++k)
                for (const oob::Regiment &r : s.armies[k].regiments)
                    if (r.home == pid) {
                        first = static_cast<long long>(k);
                        break;
                    }
            if (first)
                select_army(*first);
            else
                Q_EMIT message(u"Select an army (or ＋ New army), then click numbers to add brigades."_s);
        }
    }
}

QString ArmyPanel::describe(const QVariantList &t) const
{
    const oob::OOB *s = st();
    if (!s)
        return u""_s;
    const QString kind = t.value(0).toString();
    const long long idx = t.value(1).toLongLong();
    if (kind == u"army" && idx < static_cast<long long>(s->armies.size())) {
        const oob::Army &a = s->armies[size_t(idx)];
        return u"%1 at %2  ·  click to select it"_s.arg(_army_line(a), pname(a.location));
    }
    if (kind == u"navy" && idx < static_cast<long long>(s->navies.size())) {
        const oob::Navy &n = s->navies[size_t(idx)];
        return u"%1 at %2  ·  click to select it"_s.arg(_navy_line(n), pname(n.location));
    }
    if (kind == u"prov") {
        const long long pid = idx;
        QStringList parts;
        for (const oob::Army &a : s->armies) {
            QStringList k;
            for (const oob::Regiment &r : a.regiments)
                if (r.home == pid)
                    k << units()->name(r.type);
            if (!k.isEmpty())
                parts << u"%1 of %2 (%3)"_s.arg(k.size()).arg(a.name, k.join(u", "_s));
        }
        const long long u = get0(_used, pid), c = get0(_pcap, pid);
        QStringList sizes;
        if (const std::vector<moddata::Pop> *ps = doc->pops.get(pid))
            for (const moddata::Pop &p : *ps)
                if (p.type == u"soldiers")
                    sizes << widgets::fmt(p.size);
        QString text = u"%1: %2/%3 brigades · soldier pop%4 %5"_s.arg(
            pname(pid), QString::number(u), QString::number(c), sizes.size() == 1 ? QString() : u"s"_s,
            !sizes.isEmpty() ? sizes.join(u" + "_s) : u"none"_s);
        if (!parts.isEmpty())
            text += u" · "_s + parts.join(u"; "_s);
        if (u > c)
            text += u"  ·  ⚠ %1 over its cap"_s.arg(u - c);
        const auto [i, a] = army();
        if (a) {
            bool has = false;
            for (const oob::Regiment &r : a->regiments)
                if (r.home == pid)
                    has = true;
            text += u"  ·  click: +1 for %1%2"_s.arg(a->name, has ? u" · right-click: −1"_s : QString());
        } else {
            text += !parts.isEmpty() ? u"  ·  click: select its army"_s : u"  ·  select an army to add brigades"_s;
        }
        return text;
    }
    return u""_s;
}

}  // namespace armypanel
