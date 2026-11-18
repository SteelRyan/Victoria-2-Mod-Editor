// unitpanel.py — Warfare → Unit stats (see unitpanel.h).
#include "ui/unitpanel.h"
#include "core/blocks.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "ui/colsizes.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
using script::Node;
using widgets::label;
using widgets::Section;

namespace unitpanel {

const QRegularExpression NUM_RE(uR"(^-?\d+(\.\d+)?$)"_s);
bool num_like(const QString &s) { return NUM_RE.match(s).hasMatch(); }

const std::vector<std::pair<QString, QStringList>> GROUPS = {
    {u"Core"_s, {u"priority"_s, u"max_strength"_s, u"default_organisation"_s, u"maximum_speed"_s, u"weighted_value"_s}},
    {u"In battle (land)"_s,
     {u"attack"_s, u"defence"_s, u"discipline"_s, u"support"_s, u"maneuver"_s, u"reconnaissance"_s, u"siege"_s}},
    {u"In battle (naval)"_s, {u"hull"_s, u"gun_power"_s, u"fire_range"_s, u"evasion"_s, u"torpedo_attack"_s}},
    {u"Building and upkeep"_s, {u"build_time"_s, u"supply_consumption"_s, u"supply_consumption_score"_s}},
    {u"Ports and colonies"_s, {u"min_port_level"_s, u"limit_per_port"_s, u"colonial_points"_s}},
};
const std::vector<std::pair<QString, QString>> GOODS_BLOCKS = {{u"build_cost"_s, u"Costs to build"_s},
                                                              {u"supply_cost"_s, u"Supply it needs"_s}};
static const QHash<QString, QString> NICE = {
    {u"max_strength"_s, u"Max strength"_s}, {u"default_organisation"_s, u"Organisation"_s},
    {u"maximum_speed"_s, u"Speed"_s}, {u"weighted_value"_s, u"Weighted value (AI)"_s},
    {u"priority"_s, u"Priority (AI)"_s}, {u"defence"_s, u"Defence"_s}, {u"maneuver"_s, u"Maneuver"_s},
    {u"reconnaissance"_s, u"Reconnaissance"_s}, {u"gun_power"_s, u"Gun power"_s}, {u"fire_range"_s, u"Range"_s},
    {u"torpedo_attack"_s, u"Torpedo attack"_s}, {u"build_time"_s, u"Build time (days)"_s},
    {u"supply_consumption"_s, u"Supply consumption"_s}, {u"supply_consumption_score"_s, u"Supply consumption score"_s},
    {u"min_port_level"_s, u"Minimum naval base"_s}, {u"limit_per_port"_s, u"Limit per port (−1 = none)"_s},
    {u"colonial_points"_s, u"Colonial points"_s}};

const QHash<QString, double> HIDDEN = {{u"attack"_s, 10.0}, {u"defence"_s, 10.0}};
const QString HIDDEN_COL = u"#5fd3e6"_s;
const std::vector<Col> LAND_COLS = {
    {u"attack"_s, u"Base attack"_s, QString(), u"Attack as written in units/*.txt"_s},
    {u"attack"_s, u"Base + hidden 10"_s, u"yes"_s, u"Attack + the hidden base of 10: the number the game works with"_s},
    {u"defence"_s, u"Base defence"_s, QString(), u"Defence as written in units/*.txt"_s},
    {u"defence"_s, u"Base + hidden 10"_s, u"yes"_s, u"Defence + the hidden base of 10: the number the game works with"_s},
    {u"support"_s, u"Support"_s, QString(), u"Support: 1 = 100 % (× 1), 2 = 200 % (doubled), 0 = none"_s},
    {u"support_attack"_s, u"Support attack"_s, u"calc"_s,
     u"From the back row: (attack + the hidden 10) × support — 100 % support = × 1, none = 0"_s},
    {u"support_defence"_s, u"Support defence"_s, u"calc"_s,
     u"From the back row: (defence + the hidden 10) × support — 100 % support = × 1, none = 0"_s},
    {u"maximum_speed"_s, u"Speed"_s, QString(), u"Speed"_s}};
const std::vector<Col> NAVAL_COLS = {
    {u"hull"_s, u"Hull"_s, QString(), u"Hull"_s},
    {u"gun_power"_s, u"Gun power"_s, QString(), u"Gun power"_s},
    {u"fire_range"_s, u"Range"_s, QString(), u"Fire range"_s},
    {u"evasion"_s, u"Evasion"_s, QString(), u"Evasion"_s},
    {u"torpedo_attack"_s, u"Torpedo"_s, QString(), u"Torpedo attack"_s},
    {u"maximum_speed"_s, u"Speed"_s, QString(), u"Speed"_s}};

// (C++ only, the user 2026-11-17: "a check for units, used in some mods and the base game — guards can only be built
// from primary culture pops — has to be added as a possible condition for units") The unit's yes / no conditions as
// ticks: (key, text, tip, the game's value when the file has no line, land / naval / both).
struct Flag {
    QString key, text, tip;
    bool dflt;
    QString kind;
};
static const std::vector<Flag> FLAGS = {
    {u"primary_culture"_s, u"Only from primary-culture pops"_s,
     u"primary_culture = yes: only soldiers of the nation's primary culture can raise it (the base game's guards); no "
     u"line = any accepted culture"_s,
     false, u"both"_s},
    {u"active"_s, u"Can be built from the start"_s,
     u"active = yes: buildable without a technology or invention (no line = yes); active = no: something must switch it "
     u"on (activate_unit)"_s,
     true, u"both"_s},
    {u"can_build_overseas"_s, u"Can be built in overseas ports"_s,
     u"can_build_overseas = yes / no: whether ports outside the home continent may build it"_s, true, u"naval"_s}};

static OptStr get(const Vals &vals, const QString &k)
{
    const QString *v = vals.get(k);
    return v ? OptStr(*v) : std::nullopt;
}

std::optional<double> support_value(const QString &stat, const Vals &vals)
{
    // Support attack / defence: the stat WITH the hidden 10 × support (the user's rule, 2026-10-15).
    const QString b = stat == u"support_attack" ? u"attack"_s : u"defence"_s;
    const OptStr s = get(vals, u"support"_s);
    const auto r = real(b, get(vals, b));
    const auto sf = support_factor(s && !s->isEmpty() ? *s : u"0"_s);
    if (!r || !sf)
        return std::nullopt;
    return *r * *sf;
}

QString nice(const QString &k)
{
    auto it = NICE.find(k);
    return it != NICE.end() ? *it : py::capitalize(QString(k).replace(u'_', u' '));
}

std::optional<double> real(const QString &k, const OptStr &v)
{
    // The number the game works with: what the file says + the hidden base (None for other stats).
    if (!HIDDEN.contains(k) || !v)
        return std::nullopt;
    const auto f = py::to_float(*v);
    if (!f)
        return std::nullopt;
    return *f + HIDDEN.value(k);
}

std::optional<double> support_factor(const OptStr &v)
{
    // Support as a multiplier: 2 = ×2 (written as 200 in some mods: then ÷ 100).
    const auto s = v ? py::to_float(*v) : std::nullopt;
    if (!s)
        return std::nullopt;
    return *s > 10 ? *s / 100.0 : *s;
}

QString fmt_num(const std::optional<double> &x) { return x ? py::fmt_g(py::round_to(*x, 3)) : QString(); }

Bonus tech_bonuses(const techdata::TechDB *db)
{
    // {unit: {stat: total}} — what all the mod's technologies and inventions add to each unit.
    Bonus out;
    if (!db)
        return out;
    for (const auto &[key, e] : db->entries) {
        script::Nodes nodes;
        try {
            nodes = script::parse(e.text);
        } catch (const std::exception &) {
            continue;
        }
        for (const Node &top : nodes) {
            if (!top.is_block)
                continue;
            for (const Node &c : top.kids)
                if (c.is_block && c.has_key && !c.key.isEmpty())
                    for (const Node &x : c.kids)
                        if (x.has_key && !x.key.isEmpty() && !x.is_block && num_like(py::strip(x.value)))
                            out[c.key][x.key] += *py::to_float(x.value);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the view
UnitStatsView::UnitStatsView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *lay = new QVBoxLayout(left);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    split->addWidget(left);
    auto *s = new Section();
    s->lay->addWidget(label(u"Unit stats"_s, u"title"_s));
    note = new QLabel();
    note->setObjectName(u"faint"_s);
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    s->lay->addWidget(note);
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a unit (name or key)…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_list(); });
    s->lay->addWidget(search);
    // land and naval units: a tab each, each with its own columns (the user: never "Attack / Hull")
    unit_tabs = new QTabWidget();
    const std::tuple<QString, const std::vector<Col> *, std::vector<std::optional<int>>> kinds[2] = {
        {u"land"_s, &LAND_COLS, {std::nullopt, 78, 106, 86, 106, 58, 94, 104, 48}},
        {u"naval"_s, &NAVAL_COLS, {std::nullopt, 60, 78, 60, 66, 70, 54}}};
    for (const auto &[kind, cols, widths] : kinds) {
        auto *t = new QTreeWidget();
        QSet<int> tinted;
        for (size_t i = 0; i < cols->size(); ++i)
            if (!(*cols)[i].hidden.isEmpty())
                tinted.insert(static_cast<int>(i) + 1);
        t->setHeader(new widgets::TintHeader(QColor(HIDDEN_COL), tinted, t));
        t->setColumnCount(static_cast<int>(cols->size()) + 1);
        QStringList heads{u"Unit"_s};
        for (const Col &c : *cols)
            heads << c.head;
        t->setHeaderLabels(heads);
        for (size_t i = 0; i < cols->size(); ++i) {
            const int ii = static_cast<int>(i) + 1;
            t->headerItem()->setTextAlignment(ii, Qt::AlignCenter);
            t->headerItem()->setToolTip(ii, (*cols)[i].tip);
            if (!(*cols)[i].hidden.isEmpty())         // base + hidden 10: its own colour, header too
                t->headerItem()->setForeground(ii, QColor(HIDDEN_COL));
        }
        t->setRootIsDecorated(false);
        t->setUniformRowHeights(true);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        t->setMinimumHeight(300);
        colsizes::make_resizable(t, u"unitList_"_s + kind, widths, true);
        connect(t, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) { _picked(it, prev); });
        lists.insert(kind, t);
        unit_tabs->addTab(t, kind == u"land" ? u"Land units"_s : u"Naval units"_s);
    }
    lst = *lists.get(u"land"_s);
    s->lay->addWidget(unit_tabs, 1);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"name of a new unit, like Mountain Infantry"_s);
    connect(new_name, &QLineEdit::returnPressed, this, [this] { _new(); });
    row->addWidget(new_name, 1);
    new_btn = new QPushButton(u"＋ New unit"_s);
    new_btn->setToolTip(u"A new unit type: a copy of the picked one (its stats, costs, picture) under the new name, in a "
                        u"file of its own (units/<key>.txt). Its names are written to the localisation."_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { _new(); });
    row->addWidget(new_btn);
    s->lay->addLayout(row);
    new_msg = label(QString(), u"sub"_s);
    new_msg->setStyleSheet(u"color: #ff786e;"_s);
    new_msg->hide();
    s->lay->addWidget(new_msg);
    lay->addWidget(s, 1);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    lay = new QVBoxLayout(right);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 7);
    split->setSizes({900, 720});                     // (the land list's nine columns fit without a sideways bar)
    s = detail = new Section();
    head = new QLabel();
    head->setWordWrap(true);
    head->setTextFormat(Qt::PlainText);
    s->lay->addWidget(head);
    title = label(u"Pick a unit"_s, u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    row = new QHBoxLayout();
    row->addWidget(label(u"Name in the game"_s, u"sub"_s, false));
    name_edit = new QLineEdit();
    connect(name_edit, &QLineEdit::editingFinished, this, [this] { _name_done(); });
    row->addWidget(name_edit, 1);
    s->lay->addLayout(row);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    used = new QLabel(QString());
    used->setObjectName(u"sub"_s);
    used->setWordWrap(true);
    used->setTextFormat(Qt::RichText);
    s->lay->addWidget(used);
    rows_host = new QWidget();
    rows = new QVBoxLayout(rows_host);
    rows->setContentsMargins(0, 0, 0, 0);
    s->lay->addWidget(rows_host);
    del_btn = new QPushButton(u"✕ Delete this unit type"_s);
    del_btn->setToolTip(u"Asks once more: click twice"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _delete(); });
    s->lay->addWidget(del_btn, 0, Qt::AlignLeft);
    lay->addWidget(s);
    lay->addStretch(1);
}

// ------------------------------------------------------------------ data
QStringList UnitStatsView::_files() const
{
    return entryview::_files(mod, store, u"units"_s);   // (the mod's, the game's it keeps, the ones made in this session)
}

void UnitStatsView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    if (!_bonus)
        _bonus = tech_bonuses(tech_db);
    const auto &date = doc->date;
    const bool is_base = date && std::get<0>(*date) == 1836;
    QString more;
    if (!is_base && date)
        more = u"<br><span style=\"color:#e0a050\"><b>This scenario starts in %1.%2.%3, not 1836:</b> its nations already "
               u"have more technologies, so the numbers they field there are higher than these.</span>"_s
                   .arg(std::get<0>(*date))
                   .arg(std::get<1>(*date))
                   .arg(std::get<2>(*date));
    note->setText(u"<b>Base stats</b> — what each unit has before any technology (units/*.txt). In the game every "
                  u"technology and invention a nation has adds to them (the faint “+ …” is what all of the mod's "
                  u"technologies together can add)."_s +
                  more);
    units.clear();
    for (const QString &rel : _files()) {
        QString text;
        try {
            text = store->text(rel);
        } catch (const std::exception &) {
            continue;
        }
        for (const Node &n : script::parse(text))
            if (n.has_key && !n.key.isEmpty() && n.is_block &&
                std::any_of(n.kids.begin(), n.kids.end(), [](const Node &c) { return c.has_key && c.key == u"type"; }))
                units.insert(n.key, rel);
    }
    _fill_list();
    _fill_detail();
}

std::pair<Vals, OrderedMap<QString, std::vector<std::pair<QString, QString>>>> UnitStatsView::_stats(const QString &key) const
{
    const QString *rel = units.get(key);
    if (!rel)
        return {};
    Vals vals;
    OrderedMap<QString, std::vector<std::pair<QString, QString>>> goods;
    for (const Node &n : blocks::children(store->text(*rel), {key})) {
        if (n.is_block) {
            std::vector<std::pair<QString, QString>> g;
            for (const Node &c : n.kids)
                if (c.has_key && !c.key.isEmpty() && !c.is_block)
                    g.emplace_back(c.key, c.value);
            goods.insert(n.key, g);
        } else {
            vals.insert(n.key, n.value);
        }
    }
    return {vals, goods};
}

QString UnitStatsView::uname(const QString &key) const
{
    localisation::LocEdits *loc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr t;
    if (loc)
        t = loc->text(key);
    else if (const QString *x = mod.loc.get(key))
        t = *x;
    return t && !t->isEmpty() ? *t : py::title(QString(key).replace(u'_', u' '));
}

void UnitStatsView::_fill_list()
{
    const OptStr cur_ = cur;
    const QString q = py::lower(py::strip(search->text()));
    for (const auto &[_k, t] : lists) {
        t->blockSignals(true);
        t->clear();
    }
    QFont bold(lst->font());
    bold.setBold(true);
    std::optional<std::pair<QString, QTreeWidgetItem *>> pick;
    OrderedMap<QString, long long> counts;
    counts.insert(u"land"_s, 0);
    counts.insert(u"naval"_s, 0);
    QStringList keys;
    for (const auto &kv : units)
        keys << kv.first;
    std::stable_sort(keys.begin(), keys.end(), [this](const QString &a, const QString &b) {
        return py::lower(uname(a)) < py::lower(uname(b));
    });
    for (const QString &key : keys) {
        if (!q.isEmpty() && !py::lower(key + u' ' + uname(key)).contains(q))
            continue;
        const Vals vals = _stats(key).first;
        const QString kind = vals.value(u"type"_s) == u"naval" ? u"naval"_s : u"land"_s;
        auto *it = new QTreeWidgetItem(QStringList{uname(key)});
        const std::vector<Col> &cols = kind == u"land" ? LAND_COLS : NAVAL_COLS;
        for (size_t ci = 0; ci < cols.size(); ++ci) {
            const int i = static_cast<int>(ci) + 1;
            const Col &c = cols[ci];
            const QString v = py::strip(vals.value(c.stat));
            if (c.hidden == u"calc") {                // support attack / defence: worked out, not in the file
                const auto sv = support_value(c.stat, vals);
                it->setText(i, sv ? fmt_num(sv) : QString());
                it->setForeground(i, QColor(HIDDEN_COL));
                it->setFont(i, bold);
                const QString b = c.stat == u"support_attack" ? u"attack"_s : u"defence"_s;
                const QString bv = vals.value(b), sp = vals.value(u"support"_s);
                it->setToolTip(i, u"(%1 %2 + 10) × support %3 = %4 from the back row"_s.arg(
                                      py::lower(nice(b)), py::strip(!bv.isEmpty() ? bv : u"?"_s),
                                      py::strip(!sp.isEmpty() ? sp : u"0"_s), fmt_num(sv)));
            } else if (!c.hidden.isEmpty()) {         // the number the game works with: the file's + the hidden 10
                const auto r = real(c.stat, v);
                it->setText(i, r ? fmt_num(r) : QString());
                it->setForeground(i, QColor(HIDDEN_COL));
                it->setFont(i, bold);
                it->setToolTip(i, u"%1 %2 in the file + the hidden base of 10 = %3 — the number the game works with "
                                  u"(support multiplies this one)"_s.arg(nice(c.stat), !v.isEmpty() ? v : u"?"_s, fmt_num(r)));
            } else {
                it->setText(i, v);
                it->setToolTip(i, u"%1 %2 (units/*.txt)"_s.arg(nice(c.stat), !v.isEmpty() ? v : u"—"_s));
            }
            it->setTextAlignment(i, Qt::AlignCenter);
        }
        it->setData(0, Qt::UserRole, key);
        it->setToolTip(0, u"%1 — %2"_s.arg(key, units.value(key)));
        (*lists.get(kind))->addTopLevelItem(it);
        counts[kind] += 1;
        if (cur_ && key == *cur_)
            pick = std::pair{kind, it};
    }
    int i = 0;
    for (const QString &kind : {u"land"_s, u"naval"_s})
        unit_tabs->setTabText(i++, u"%1 units (%2)"_s.arg(kind == u"land" ? u"Land"_s : u"Naval"_s).arg(counts.value(kind)));
    if (pick) {
        (*lists.get(pick->first))->setCurrentItem(pick->second);
        unit_tabs->setCurrentWidget(*lists.get(pick->first));
    }
    for (const auto &[_k, t] : lists)
        t->blockSignals(false);
}

void UnitStatsView::show_unit(const QString &key)
{
    // Pick one unit (Ctrl+K): the search box emptied so it is in the list, its stats shown.
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    cur = key;
    _fill_list();
    _fill_detail();
}

void UnitStatsView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QString key = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!key.isEmpty()) {
        cur = key;
        _fill_detail();
    }
}

// ------------------------------------------------------------------ one unit
void UnitStatsView::_clear_rows()
{
    while (rows->count()) {
        QLayoutItem *it = rows->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
}

void UnitStatsView::_fill_detail()
{
    const bool ok = cur && units.contains(*cur);
    rows_host->setVisible(ok);
    name_edit->setEnabled(ok);
    del_btn->setVisible(ok);
    used->setVisible(ok);
    if (_armed != cur) {
        _armed.reset();
        del_btn->setText(u"✕ Delete this unit type"_s);
        del_btn->setStyleSheet(QString());
    }
    if (ok)
        used->setText(_used_text(*cur));
    if (!ok) {
        head->setText(QString());
        head->setStyleSheet(QString());
        title->setText(u"Pick a unit"_s);
        sub->setText(QString());
        return;
    }
    const QString key = *cur;
    const auto [vals, goods] = _stats(key);
    head->setText(u"Selected unit: "_s + uname(key));
    head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; padding: 5px 10px; "
                        u"border-radius: 4px; }"_s.arg(theme::GOLD));
    title->setText(uname(key));
    sub->setText(u"%1 · %2 · %3"_s.arg(units.value(key), vals.value(u"type"_s, u"?"_s), vals.value(u"unit_type"_s, u"?"_s)));
    if (!name_edit->hasFocus())
        name_edit->setText(uname(key));
    Sig sig{key, {}, {}};
    for (const auto &kv : vals)
        sig.vals << kv.first;
    std::sort(sig.vals.begin(), sig.vals.end());
    {
        std::vector<std::pair<QString, std::vector<std::pair<QString, QString>>>> gs(goods.begin(), goods.end());
        std::sort(gs.begin(), gs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[k, v] : gs) {
            QStringList g;
            for (const auto &[x, _a] : v)
                g << x;
            sig.goods.emplace_back(k, g);
        }
    }
    static const OrderedMap<QString, double> NONE;
    const OrderedMap<QString, double> *bp = nullptr;
    if (_bonus)
        if (auto it = _bonus->find(key); it != _bonus->end())
            bp = &it->second;
    const OrderedMap<QString, double> &bonus = bp ? *bp : NONE;
    if (_sig && sig == *_sig) {                      // the same fields: only their values
        for (const auto &[k, cb] : _flags) {
            const Flag &f = *std::find_if(FLAGS.begin(), FLAGS.end(), [&](const Flag &x) { return x.key == k; });
            const QString v = py::lower(py::strip(vals.value(k)));
            cb->blockSignals(true);
            cb->setChecked(v.isEmpty() ? f.dflt : v == u"yes");
            cb->blockSignals(false);
        }
        for (const auto &[k, ed] : _edits) {
            QString v;
            if (k.first == u"v")
                v = vals.value(k.second);
            else if (const auto *g = goods.get(k.first)) {
                OptStr last;                         // (dict(…).get: the last of the same good)
                for (const auto &[x, a] : *g)
                    if (x == k.second)
                        last = a;
                v = last.value_or(QString());
            }
            if (!ed->hasFocus()) {
                ed->setText(py::strip(v));
                ed->setStyleSheet(QString());
            }
        }
        _real_texts(vals, bonus);
        return;
    }
    _sig = sig;
    _edits.clear();
    _reals.clear();
    _flags.clear();
    _clear_rows();
    const bool naval = vals.value(u"type"_s) == u"naval";
    QSet<QString> shown;
    for (const auto &[gt, keys] : GROUPS) {
        if ((gt.contains(u"land"_s) && naval) || (gt.contains(u"naval"_s) && !naval))
            continue;
        std::vector<std::pair<QString, QString>> present;
        for (const QString &k : keys)
            if (vals.contains(k))
                present.emplace_back(k, vals.value(k));
        if (present.empty())
            continue;
        rows->addWidget(_group(gt, present, bonus));
        for (const auto &[k, _v] : present)
            shown.insert(k);
    }
    std::vector<std::pair<QString, QString>> rest, other;
    for (const auto &[k, v] : vals) {
        if (!shown.contains(k) && num_like(py::strip(v)))
            rest.emplace_back(k, v);
        if (!num_like(py::strip(v)) &&
            std::none_of(FLAGS.begin(), FLAGS.end(), [&](const Flag &f) { return f.key == k; }))
            other.emplace_back(k, v);
    }
    if (!rest.empty())
        rows->addWidget(_group(u"Other numbers"_s, rest, bonus));
    rows->addWidget(_conditions(vals, naval));
    for (const auto &[gk, gtitle] : GOODS_BLOCKS)
        rows->addWidget(_goods(gk, gtitle, goods.value(gk)));
    if (!other.empty()) {
        QStringList parts;
        for (const auto &[k, v] : other)
            parts << nice(k) + u' ' + v;
        rows->addWidget(label(u"Also: "_s + parts.join(u" · "_s), u"faint"_s));
    }
    _real_texts(vals, bonus);
}

void UnitStatsView::_real_texts(const Vals &vals, const OrderedMap<QString, double> &bonus)
{
    // The real numbers next to attack / defence, and what support makes of the real attack.
    for (const auto &[k, lb] : _reals) {
        if (k == u"support")
            continue;
        const auto r = real(k, get(vals, k));
        const double b = bonus.value(k, 0.0);
        lb->setText(r ? u"<span style='color:%1; font-weight:700'>%2</span>&nbsp; base + hidden 10%3"_s.arg(
                            HIDDEN_COL, fmt_num(r), b ? u" · + %1 from technologies"_s.arg(py::fmt_g(b)) : QString())
                      : QString());
    }
    QLabel *const *lbp = _reals.get(u"support"_s);
    if (!lbp)
        return;
    QLabel *lb = *lbp;
    const auto ra = real(u"attack"_s, get(vals, u"attack"_s));
    const auto sf = support_factor(get(vals, u"support"_s));
    if (!ra || !sf) {
        lb->setText(QString());
        return;
    }
    const double shown = *py::to_float(vals.value(u"attack"_s));
    if (*sf <= 0) {
        lb->setText(u"<b style='color:%1'>Support 0:</b> this unit adds no attack from the back row."_s.arg(theme::GOLD));
        return;
    }
    const QString pc = fmt_num(*sf * 100) + u" %"_s;
    lb->setText(u"<b style='color:%1'>Support works on base + hidden 10 (the coloured number), not on the base.</b>"
                u"<br>From the back row: <b style='color:%2'>(%3 + 10) × %4 = %5</b> attack"
                u"<br><span style='color:%6'>— not %7 × %8 = %9 as the file's number alone would suggest: "_s
                    .arg(HIDDEN_COL, HIDDEN_COL, fmt_num(shown), pc, fmt_num(*ra * *sf), theme::INK_DIM, fmt_num(shown),
                         pc, fmt_num(shown * *sf)) +
                u"the hidden 10 is multiplied too, so support counts for much more than it looks.</span>"_s);
}

static QLabel *bold_label(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

QWidget *UnitStatsView::_conditions(const Vals &vals, bool naval)
{
    // (C++ only) "Conditions": a tick for each yes / no condition of the unit; a click writes `key = yes / no`.
    auto *w = new QWidget();
    auto *g = new QVBoxLayout(w);
    g->setContentsMargins(0, 8, 0, 2);
    g->setSpacing(4);
    g->addWidget(bold_label(u"Conditions"_s));
    for (const Flag &f : FLAGS) {
        if (f.kind == u"naval" && !naval)
            continue;
        const QString v = py::lower(py::strip(vals.value(f.key)));
        auto *cb = new QCheckBox(f.text + u"  ("_s + f.key + u')');
        cb->setChecked(v.isEmpty() ? f.dflt : v == u"yes");
        cb->setToolTip(f.tip + (v.isEmpty() ? u" — not in the file now: the game's default"_s : QString()));
        const QString k = f.key;
        connect(cb, &QCheckBox::toggled, this, [this, k](bool on) { _set_flag(k, on); });
        g->addWidget(cb);
        _flags.emplace_back(f.key, cb);
    }
    return w;
}

void UnitStatsView::_set_flag(const QString &key, bool on)
{
    const QString c = *cur, v = on ? u"yes"_s : u"no"_s;
    _emit(key + u" = "_s + v, [c, key, v](const QString &t) { return blocks::set_value(t, {c}, key, v); });
}

QWidget *UnitStatsView::_group(const QString &title_, const std::vector<std::pair<QString, QString>> &items,
                               const OrderedMap<QString, double> &bonus)
{
    auto *w = new QWidget();
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 6, 0, 2);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(0, 1);
    g->addWidget(bold_label(title_), 0, 0, 1, 3);
    int r = 0;
    for (const auto &[k, v] : items) {
        ++r;
        QLabel *lb = label(nice(k), u"sub"_s, false);
        lb->setToolTip(k);
        g->addWidget(lb, r, 0);
        auto *ed = new QLineEdit(py::strip(v));
        ed->setFixedWidth(90);
        ed->setAlignment(Qt::AlignRight);
        const QString kk = k;
        connect(ed, &QLineEdit::editingFinished, this, [this, kk, ed] { _set(kk, ed); });
        g->addWidget(ed, r, 1);
        _edits.emplace_back(std::pair{u"v"_s, k}, ed);
        const double b = bonus.value(k, 0.0);
        if (HIDDEN.contains(k)) {                    // the real number (the hidden 10 on top), in gold brackets
            auto *rl = new QLabel(QString());
            rl->setObjectName(u"sub"_s);
            rl->setTextFormat(Qt::RichText);
            rl->setToolTip(u"The game gives every unit a hidden base %1 of 10; the file's number comes on top of it"_s.arg(
                py::lower(nice(k))));
            g->addWidget(rl, r, 2);
            _reals.insert(k, rl);
        } else {
            g->addWidget(label(b ? u"+ %1 from technologies"_s.arg(py::fmt_g(b)) : QString(), u"faint"_s, false), r, 2);
        }
        if (k == u"support") {                       // what support makes of it: under the row, across the width
            auto *sl = new QLabel(QString());
            sl->setWordWrap(true);
            sl->setTextFormat(Qt::RichText);
            sl->setStyleSheet(u"QLabel { background:#14272b; color:%1; border:2px solid %2; border-radius:4px; "
                              u"padding:6px 8px; }"_s.arg(theme::INK, u"#2f7f8c"_s));
            ++r;
            g->addWidget(sl, r, 0, 1, 3);
            _reals.insert(u"support"_s, sl);
        }
    }
    return w;
}

QWidget *UnitStatsView::_goods(const QString &block, const QString &title_,
                               const std::vector<std::pair<QString, QString>> &rows_)
{
    auto *w = new QWidget();
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 8, 0, 2);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(0, 1);
    g->addWidget(bold_label(title_), 0, 0, 1, 3);
    auto gname = [this](const QString &k) {
        const moddata::Good *e = mod.goods.get(k);
        return e ? e->name : k;
    };
    int r = 0;
    for (const auto &[good, amt] : rows_) {
        ++r;
        g->addWidget(label(gname(good), u"sub"_s, false), r, 0);
        auto *ed = new QLineEdit(py::strip(amt));
        ed->setFixedWidth(90);
        ed->setAlignment(Qt::AlignRight);
        const QString gg = good;
        connect(ed, &QLineEdit::editingFinished, this, [this, block, gg, ed] { _set_good(block, gg, ed); });
        g->addWidget(ed, r, 1);
        _edits.emplace_back(std::pair{block, good}, ed);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(30);
        x->setToolTip(u"Take %1 off"_s.arg(good));
        connect(x, &QPushButton::clicked, this, [this, block, gg] { _del_good(block, gg); });
        g->addWidget(x, r, 2);
    }
    r = static_cast<int>(rows_.size()) + 1;
    auto *box = new QComboBox();
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QSet<QString> have;
    for (const auto &[x, _a] : rows_)
        have.insert(x);
    QStringList gks;
    for (const auto &kv : mod.goods)
        gks << kv.first;
    std::stable_sort(gks.begin(), gks.end(),
                     [&](const QString &a, const QString &b) { return py::lower(gname(a)) < py::lower(gname(b)); });
    for (const QString &gk : gks)
        if (!have.contains(gk))
            box->addItem(gname(gk), gk);
    g->addWidget(box, r, 0);
    auto *amt = new QLineEdit();
    amt->setPlaceholderText(u"amount"_s);
    amt->setFixedWidth(90);
    g->addWidget(amt, r, 1);
    auto *add = new QPushButton(u"＋"_s);
    add->setFixedWidth(30);
    connect(add, &QPushButton::clicked, this, [this, block, box, amt] {
        _add_good(block, box->currentData().toString(), amt);
    });
    g->addWidget(add, r, 2);
    return w;
}

// ------------------------------------------------------------------ changes (into the file's text)
void UnitStatsView::_emit(const QString &what, const std::function<QString(const QString &)> &fn)
{
    const QString key = *cur;
    const QString rel = units.value(key);
    const QString nw = fn(store->text(rel));
    if (nw != store->text(rel))
        Q_EMIT scriptEdit(uname(key) + u": "_s + what, rel, nw);
}

OptStr UnitStatsView::_num(QLineEdit *ed, const QString &what)
{
    const QString v = py::strip(ed->text());
    if (!num_like(v)) {
        ed->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        warn->setText(u"“%1” needs a number (like 4 or 0.5)."_s.arg(what));
        return std::nullopt;
    }
    ed->setStyleSheet(QString());
    warn->setText(QString());
    return v;
}

void UnitStatsView::_set(const QString &k, QLineEdit *ed)
{
    if (const OptStr v = _num(ed, nice(k))) {
        const QString c = *cur;
        _emit(py::lower(nice(k)) + u' ' + *v, [c, k, v](const QString &t) { return blocks::set_value(t, {c}, k, *v); });
    }
}

void UnitStatsView::_set_good(const QString &block, const QString &good, QLineEdit *ed)
{
    if (const OptStr v = _num(ed, good)) {
        const QString c = *cur;
        _emit(QString(block).replace(u'_', u' ') + u' ' + good + u' ' + *v,
              [c, block, good, v](const QString &t) { return blocks::set_value(t, {c, block}, good, *v); });
    }
}

void UnitStatsView::_del_good(const QString &block, const QString &good)
{
    const QString c = *cur;
    _emit(QString(block).replace(u'_', u' ') + u" without "_s + good,
          [c, block, good](const QString &t) { return blocks::del_value(t, {c, block}, good); });
}

void UnitStatsView::_add_good(const QString &block, const QString &good, QLineEdit *amt)
{
    const OptStr v = _num(amt, u"the amount"_s);
    if (!v || good.isEmpty())
        return;
    const QString c = *cur;
    _emit(QString(block).replace(u'_', u' ') + u" + "_s + good + u' ' + *v, [c, block, good, v](const QString &t) {
        if (!blocks::find(script::parse(t), {c, block}))
            return blocks::set_block(t, {c, block}, u"\n\t\t%1 = %2\n\t"_s.arg(good, *v));
        return blocks::set_value(t, {c, block}, good, *v);
    });
}

// ------------------------------------------------------------------ who names a unit type; new; delete
std::tuple<std::vector<std::pair<QString, QString>>, long long, long long> UnitStatsView::users(const QString &key) const
{
    // (what switches the unit on: [(file, the technology / invention)], armies / navies of the start that have it: how
    // many files, how many regiments / ships).
    std::vector<std::pair<QString, QString>> on;
    const QRegularExpression act(uR"(\bactivate_unit\s*=\s*)"_s + QRegularExpression::escape(key) + uR"(\b)"_s,
                                 QRegularExpression::UseUnicodePropertiesOption);
    for (const QString &folder : {u"technologies"_s, u"inventions"_s})
        for (const QString &rel : entryview::_files(mod, store, folder)) {
            QString text;
            try {
                text = store->text(rel);
            } catch (const std::exception &) {
                continue;
            }
            if (!text.contains(key))
                continue;
            for (const Node &n : script::parse(text))
                if (n.has_key && !n.key.isEmpty() && n.is_block && act.match(text.mid(n.kstart, n.vend - n.kstart)).hasMatch())
                    on.emplace_back(rel, n.key);
        }
    long long files = 0, count = 0;
    const QString d = mod.p({u"history"_s, u"units"_s});
    const QRegularExpression rx(uR"(\btype\s*=\s*)"_s + QRegularExpression::escape(key) + uR"(\b)"_s,
                                QRegularExpression::UseUnicodePropertiesOption);
    if (QFileInfo(d).isDir()) {
        QDirIterator di(d, QDir::Files, QDirIterator::Subdirectories);
        while (di.hasNext()) {
            const QString f = di.next();
            if (!f.toLower().endsWith(u".txt"_s))
                continue;
            long long got = 0;
            QFile fh(f);
            if (fh.open(QIODevice::ReadOnly)) {
                const QString text = QString::fromLatin1(fh.readAll());   // (Python: cp1252, errors="replace")
                for (auto m = rx.globalMatch(text); m.hasNext(); m.next())
                    ++got;
            }
            files += got ? 1 : 0;
            count += got;
        }
    }
    return {on, files, count};
}

QString UnitStatsView::_used_text(const QString &key) const
{
    const Vals vals = _stats(key).first;
    const auto [on, files, count] = users(key);
    QStringList parts;
    if (py::lower(py::strip(vals.value(u"active"_s))) == u"no") {
        if (!on.empty()) {
            QStringList ns;
            for (const auto &[_r, k] : on)
                ns << u"<b>"_s + uname(k) + u"</b>"_s;
            parts << u"Switched on by: "_s + ns.join(u", "_s);
        } else {
            parts << u"<span style='color:#ffb454'>Nothing switches it on: “active = no” and no technology or invention "
                     u"has  activate_unit = %1  — nobody can build it.</span>"_s.arg(key);
        }
    } else {
        parts << u"Buildable from the start."_s;
    }
    parts << (count ? u"At the start: %1 regiments / ships in %2 army files."_s.arg(count).arg(files)
                    : u"No army or navy of the start has it."_s);
    return parts.join(u' ');
}

void UnitStatsView::_new()
{
    const QString name = new_name->text().simplified();   // (" ".join(text.split()))
    static const QRegularExpression NON(u"[^a-z0-9]+"_s);
    QString key = py::lower(name);
    key.replace(NON, u"_"_s);
    while (key.startsWith(u'_'))
        key.remove(0, 1);
    while (key.endsWith(u'_'))
        key.chop(1);
    const OptStr src = cur && units.contains(*cur) ? cur : std::nullopt;
    QString why;
    if (key.isEmpty())
        why = u"Type the new unit's name first."_s;
    else if (units.contains(key) || QFileInfo(mod.p({u"units"_s, key + u".txt"_s})).isFile())
        why = u"There is a unit “%1” already."_s.arg(key);
    else if (!src)
        why = u"Pick the unit to copy first (the new one starts with its stats)."_s;
    if (!why.isEmpty()) {
        new_msg->setText(why);
        new_msg->show();
        return;
    }
    new_msg->hide();
    const QString text = store->text(units.value(*src));
    QString body;
    for (const Node &n : script::parse(text))
        if (n.has_key && n.key == *src) {
            body = key + text.mid(n.kstart + src->size(), n.vend - n.kstart - src->size()) + u"\n"_s;
            break;
        }
    new_name->clear();
    cur = key;
    Q_EMIT scriptEdit(u"New unit type %1 (a copy of %2)"_s.arg(name, uname(*src)), u"units/%1.txt"_s.arg(key), body);
    Q_EMIT nameEdit(key, name);
    Q_EMIT nameEdit(key + u"_short"_s, py::strip(name.left(4)));
}

void UnitStatsView::_delete()
{
    if (!cur || !units.contains(*cur))
        return;
    const QString key = *cur;
    if (_armed != key) {
        _armed = key;
        const auto [on, files, count] = users(key);
        const QString more = count || !on.empty() ? u" — %1 regiments / ships of the start and %2 technologies still name it"_s
                                                        .arg(count)
                                                        .arg(on.size())
                                                  : QString();
        del_btn->setText(u"Click again: %1 goes%2"_s.arg(uname(key), more));
        del_btn->setStyleSheet(u"QPushButton { color: #ff786e; font-weight: 700; border: 2px solid #ff786e; }"_s);
        return;
    }
    const QString rel = units.value(key);
    const QString text = store->text(rel);
    std::vector<const Node *> nodes;
    const script::Nodes all = script::parse(text);
    for (const Node &x : all)
        if (x.has_key && !x.key.isEmpty() && x.is_block)
            nodes.push_back(&x);
    const Node *n = nullptr;
    for (const Node *x : nodes)
        if (x->key == key) {
            n = x;
            break;
        }
    store->drop_names = {key, key + u"_short"_s};
    const QString name = uname(key);
    _armed.reset();
    cur.reset();
    if (nodes.size() == 1) {
        Q_EMIT fileRemove(u"Unit type %1 deleted"_s.arg(name), rel);
    } else if (n) {
        const qsizetype a = text.lastIndexOf(u'\n', n->kstart - 1 < 0 ? 0 : n->kstart - 1) + 1;
        const qsizetype b = text.indexOf(u'\n', n->vend);
        Q_EMIT scriptEdit(u"Unit type %1 deleted"_s.arg(name), rel, text.left(a) + text.mid(b >= 0 ? b + 1 : text.size()));
    }
}

void UnitStatsView::_name_done()
{
    const QString text = name_edit->text().simplified();
    if (cur && !text.isEmpty() && text != uname(*cur))
        Q_EMIT nameEdit(*cur, text);
}

}  // namespace unitpanel
