// utilpanel.py — Provinces tab → Utility states.
#include "ui/utilpanel.h"
#include "ui/dialogs.h"
#include "ui/widgets.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace utilpanel {

namespace {

QString n_of(long long n, const QString &one, const QString &many)   // _n
{
    return u"%1 %2"_s.arg(n).arg(n == 1 ? one : many);
}

}  // namespace

UtilityPanel::UtilityPanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new Section();
    s->lay->addWidget(label(u"Utility states"_s, u"title"_s));
    s->lay->addWidget(label(u"Regions in region.txt that only group provinces for events and decisions (e.g. "
                            u"ANY_PROVINCE with every province in it) — no real state. The game counts the first "
                            u"region listing a province as its state; the editor does the same and skips utility "
                            u"states, so the state checks leave them alone. Which ones are utility states is kept "
                            u"per mod in the editor (nothing is written into the mod for that)."_s,
                            u"faint"_s));
    lay->addWidget(s);

    // --- the marked ones
    s = new Section();
    list_title = label(QString(), u"label"_s);
    s->lay->addWidget(list_title);
    lst = new QListWidget();
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    connect(lst, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString rid = it->data(Qt::UserRole).toString();
        Q_EMIT viewRequested(view && rid == *view ? QString() : rid);
    });
    s->lay->addWidget(lst);
    none_note = label(u"None yet — mark one below (the suggestions are regions that are nobody's state)."_s, u"faint"_s);
    s->lay->addWidget(none_note);
    lay->addWidget(s);

    // --- the viewed one
    s = view_section = new Section();
    auto *row = new QHBoxLayout();
    v_title = label(QString(), u"title"_s);
    row->addWidget(v_title, 1);
    auto *close = new QPushButton(u"✕"_s);
    close->setFixedWidth(34);
    close->setToolTip(u"Stop showing it on the map (Esc)"_s);
    connect(close, &QPushButton::clicked, this, [this] { Q_EMIT viewRequested(QString()); });
    row->addWidget(close);
    s->lay->addLayout(row);
    v_sub = label(QString(), u"sub"_s);
    s->lay->addWidget(v_sub);
    v_warn = label(QString(), u"warnAmber"_s);
    v_warn->setWordWrap(true);
    s->lay->addWidget(v_warn);
    edit_btn = new QPushButton(u"✎ Add / remove provinces by clicking the map"_s);
    edit_btn->setCheckable(true);
    edit_btn->setToolTip(u"On: a click on a province puts it into this utility state or takes it out; "
                         u"Shift+click does it for the province's whole state. Esc stops."_s);
    connect(edit_btn, &QPushButton::toggled, this, [this](bool on) { Q_EMIT editToggled(on); });
    s->lay->addWidget(edit_btn);
    edit_hint = label(u"Click: add / remove a province · Shift+click: its whole state · Esc: stop"_s, u"faint"_s);
    s->lay->addWidget(edit_hint);
    row = new QHBoxLayout();
    add_sel = new QPushButton(u"＋ Add the selected"_s);
    connect(add_sel, &QPushButton::clicked, this, [this] { Q_EMIT selectedRequested(true); });
    rm_sel = new QPushButton(u"− Take out the selected"_s);
    connect(rm_sel, &QPushButton::clicked, this, [this] { Q_EMIT selectedRequested(false); });
    row->addWidget(add_sel);
    row->addWidget(rm_sel);
    s->lay->addLayout(row);
    unmark_btn = new QPushButton(u"Make it a normal state again"_s);
    unmark_btn->setToolTip(u"It counts as a state again (the state checks see it) — its provinces stay in it"_s);
    connect(unmark_btn, &QPushButton::clicked, this, [this] {
        if (view && !view->isEmpty())
            Q_EMIT markRequested({*view}, false);
    });
    s->lay->addWidget(unmark_btn);
    lay->addWidget(s);

    // --- suggestions
    s = sugg_section = new Section();
    sugg_title = label(QString(), u"label"_s);
    s->lay->addWidget(sugg_title);
    sugg = new QListWidget();
    sugg->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sugg->setWordWrap(true);
    sugg->setSelectionMode(QAbstractItemView::ExtendedSelection);
    s->lay->addWidget(sugg);
    row = new QHBoxLayout();
    sugg_one = new QPushButton(u"Mark the selected"_s);
    connect(sugg_one, &QPushButton::clicked, this, [this] {
        QStringList rids;
        for (QListWidgetItem *it : sugg->selectedItems())
            rids << it->data(Qt::UserRole).toString();
        Q_EMIT markRequested(rids, true);
    });
    sugg_all = new QPushButton(u"Mark all suggested"_s);
    connect(sugg_all, &QPushButton::clicked, this, [this] {
        QStringList rids;
        for (int i = 0; i < sugg->count(); ++i)
            rids << sugg->item(i)->data(Qt::UserRole).toString();
        Q_EMIT markRequested(rids, true);
    });
    row->addWidget(sugg_one);
    row->addWidget(sugg_all);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- any other region
    s = new Section();
    s->lay->addWidget(label(u"Mark another region as a utility state"_s, u"label"_s));
    row = new QHBoxLayout();
    pick = new dialogs::PickCombo({}, {{std::nullopt, u"Pick a region…"_s}});
    pick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(pick, 1);
    auto *mark = new QPushButton(u"Mark"_s);
    connect(mark, &QPushButton::clicked, this, [this] {
        if (const OptStr k = pick->key(); k && !k->isEmpty())
            Q_EMIT markRequested({*k}, true);
    });
    row->addWidget(mark);
    s->lay->addLayout(row);
    lay->addWidget(s);
    lay->addSpacing(fontMetrics().lineSpacing() + 4);
    lay->addStretch(1);
}

QString UtilityPanel::rname(const QString &rid) const
{
    const moddata::Region *r = mod.any_region(rid);
    return r && !r->name.isEmpty() && r->name != rid ? u"%1 (%2)"_s.arg(r->name, rid) : rid;
}

void UtilityPanel::refresh(document::Document *d, const OptStr &view_, bool editing,
                           const std::vector<long long> &selection)
{
    doc = d;
    view = view_ && mod.utility.contains(*view_) ? view_ : std::nullopt;
    std::set<long long> land;
    for (const auto &[p, pr] : mod.provinces)
        if (!pr.sea)
            land.insert(p);
    auto land_count = [&land](const moddata::Region &r) {
        long long n = 0;
        for (long long p : r.provinces)
            n += land.contains(p) ? 1 : 0;
        return n;
    };
    // the marked ones
    lst->clear();
    QStringList urids;
    for (const auto &[rid, _r] : mod.utility)
        urids << rid;
    std::sort(urids.begin(), urids.end());
    for (const QString &rid : urids) {
        const moddata::Region &r = *mod.utility.get(rid);
        auto *it = new QListWidgetItem(u"%1  ·  %2"_s.arg(rname(rid), n_of(land_count(r), u"province"_s, u"provinces"_s)));
        it->setData(Qt::UserRole, rid);
        it->setToolTip(u"Click to show it on the map and edit its provinces"_s);
        lst->addItem(it);
        if (view && rid == *view)
            it->setSelected(true);
    }
    list_title->setText(u"Utility states of this mod (%1)"_s.arg(mod.utility.size()));
    lst->setVisible(!mod.utility.isEmpty());
    none_note->setVisible(mod.utility.isEmpty());
    int rows = std::max(1, std::min(8, lst->count()));
    lst->setFixedHeight(rows * (lst->count() ? lst->sizeHintForRow(0) : 24) + 6);
    // the viewed one
    view_section->setVisible(view.has_value());
    if (view) {
        const moddata::Region &r = *mod.utility.get(*view);
        const long long n = land_count(r);
        std::map<QString, long long> order;
        for (qsizetype i = 0; i < mod.region_order.size(); ++i)
            order[mod.region_order[i]] = i;               // ({k: i}: the last place of a key)
        auto order_of = [&order](const QString &k, long long dflt) {
            const auto it = order.find(k);
            return it == order.end() ? dflt : it->second;
        };
        long long first = 0, nostate = 0;
        for (long long p : r.provinces) {
            if (!land.contains(p))
                continue;
            const OptStr &reg = mod.provinces.get(p)->region;
            if (reg && !reg->isEmpty() && order_of(*view, 0) < order_of(*reg, 1000000))
                ++first;
            if (!reg || reg->isEmpty())
                ++nostate;
        }
        v_title->setText(rname(*view));
        const bool dirty = doc && doc->dirty_regions().contains(*view);
        v_sub->setText(u"%1 — in gold on the map%2"_s.arg(n_of(n, u"province"_s, u"provinces"_s),
                                                          dirty ? u" · changed, written to region.txt on Apply"_s : QString()));
        QStringList warn;
        if (first)
            warn << u"region.txt lists it before the real state of %1 — the game counts it as their state "
                    u"there (move it below them in the file if that's not meant)."_s.arg(
                        n_of(first, u"province"_s, u"provinces"_s));
        if (nostate)
            warn << u"%1 in no real state (only in utility states)."_s.arg(
                n_of(nostate, u"province is"_s, u"provinces are"_s));
        v_warn->setText(!warn.isEmpty() ? u"⚠ "_s + warn.join(u" "_s) : QString());
        v_warn->setVisible(!warn.isEmpty());
        edit_btn->blockSignals(true);
        edit_btn->setChecked(editing);
        edit_btn->blockSignals(false);
        edit_hint->setVisible(editing);
        const auto k = static_cast<long long>(selection.size());
        add_sel->setEnabled(k > 0);
        rm_sel->setEnabled(k > 0);
        add_sel->setText(u"＋ Add the selected (%1)"_s.arg(k));
        rm_sel->setText(u"− Take out the selected (%1)"_s.arg(k));
    }
    // suggestions
    const auto sg = mod.utility_suggestions();
    sugg->clear();
    for (const auto &[rid, why] : sg) {
        auto *it = new QListWidgetItem(u"%1 — %2"_s.arg(rname(rid), why));
        it->setData(Qt::UserRole, rid);
        sugg->addItem(it);
    }
    sugg_section->setVisible(!sg.empty());
    sugg_title->setText(u"Suggested — they look like utility states (%1)"_s.arg(sg.size()));
    sugg_all->setVisible(sg.size() > 1);
    rows = std::max(1, std::min(6, sugg->count()));
    sugg->setFixedHeight(rows * (sugg->count() ? sugg->sizeHintForRow(0) : 24) + 8);
    // any other region
    const std::pair<qsizetype, qsizetype> sig{mod.regions.size(), mod.utility.size()};
    if (_pick_sig != sig) {
        _pick_sig = sig;
        QStringList rids;
        for (const auto &[rid, _r] : mod.regions)
            rids << rid;
        std::sort(rids.begin(), rids.end());
        std::vector<dialogs::PickItem> items;
        for (const QString &rid : rids)
            items.push_back({rid, u"%1  ·  %2"_s.arg(rname(rid), n_of(static_cast<long long>(mod.regions.get(rid)->provinces.size()),
                                                                     u"province"_s, u"provinces"_s)),
                             std::nullopt, std::nullopt});
        pick->fill(items, {{std::nullopt, u"Pick a region…"_s}});
        pick->setCurrentIndex(0);
    }
}

}  // namespace utilpanel
