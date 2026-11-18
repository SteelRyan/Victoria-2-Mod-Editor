// panels.py — Pops tab side panels: the pop editor and the split allocator.
#include "ui/panels.h"
#include "core/pynum.h"
#include "ui/dialogs.h"
#include "ui/theme.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;
using widgets::fmt;
using widgets::label;
using widgets::Rgb;
using widgets::Section;
using widgets::Segment;
using widgets::swatch_icon;

namespace panels {

const std::vector<double> STEPS = {100, 20, 10, 5, 1, 0.5, 0.1};
const QStringList DEFAULT_ELITE = {u"bureaucrats"_s, u"aristocrats"_s, u"officers"_s, u"capitalists"_s};
const QString NONE_KEY = QString(QChar(0));

QString pct_label(double v)
{
    QString s = py::fmt_g(v);
    if (v < 1)
        while (s.startsWith(u'0'))
            s.remove(0, 1);
    return s + u"%"_s;
}

QString chip_key(const OptStr &k) { return k ? *k : NONE_KEY; }
OptStr from_chip(const QString &k) { return k == NONE_KEY ? std::nullopt : OptStr(k); }
QSet<OptStr> from_chips(const QSet<QString> &keys)
{
    QSet<OptStr> out;
    for (const QString &k : keys)
        out.insert(from_chip(k));
    return out;
}

namespace {

const Rgb GREY{128, 128, 128};

OptStr pop_field(const moddata::Pop &p, const QString &field)   // p[field]
{
    if (field == u"type"_s)
        return p.type;
    return field == u"culture"_s ? p.culture : p.religion;
}

QString signed_commas(long long d)                   // "{:+,}".format(d)
{
    return d >= 0 ? u"+"_s + fmt(d) : fmt(d);
}

QString py_str(const OptStr &k) { return k ? *k : u"None"_s; }   // str(k)

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

QString swatch_style(const Rgb &c)
{
    return u"background: rgb(%1,%2,%3); border-radius: 2px;"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

}  // namespace

OptStr EditPanel::_field_of(const moddata::Pop &p) const { return pop_field(p, mix_field); }

EditPanel::EditPanel(moddata::Mod &mod_, const QString &backup_root, QWidget *parent)
    : QWidget(parent), mod(mod_), elite_types(DEFAULT_ELITE)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- header
    auto *s = new Section();
    title = label(u"Nothing selected"_s, u"title"_s);
    title->setWordWrap(true);
    sub = label(u"Click a province on the map. Ctrl+click adds more."_s, u"sub"_s);
    sub->setWordWrap(true);
    sub->setTextFormat(Qt::RichText);
    info = label(QString(), u"faint"_s);
    info->setWordWrap(true);
    s->lay->addWidget(title);
    s->lay->addWidget(sub);
    s->lay->addWidget(info);
    lay->addWidget(s);

    // --- filter
    s = new Section();
    s->lay->addWidget(label(u"Filter pops to edit"_s));
    types = new widgets::ChipGroup(u"Type"_s);
    cultures = new widgets::ChipGroup(u"Culture"_s);
    religions = new widgets::ChipGroup(u"Religion"_s);
    for (widgets::ChipGroup *g : {types, cultures, religions})
        connect(g, &widgets::ChipGroup::changed, this, &EditPanel::_filter_changed);
    s->lay->addWidget(types);
    s->lay->addSpacing(4);
    s->lay->addWidget(cultures);
    s->lay->addSpacing(4);
    s->lay->addWidget(religions);
    lay->addWidget(s);

    // --- stepper
    s = new Section();
    step_label = label(u"Selected pop total"_s);
    step_label->setAlignment(Qt::AlignCenter);
    s->lay->addWidget(step_label);
    total_edit = new QLineEdit(u"—"_s);
    total_edit->setObjectName(u"bigTotal"_s);
    total_edit->setAlignment(Qt::AlignCenter);
    total_edit->setToolTip(u"Type an exact number and press Enter: the selected pops are scaled to it when you press "
                           u"Apply changes"_s);
    connect(total_edit, &QLineEdit::returnPressed, this, &EditPanel::_total_entered);
    s->lay->addWidget(total_edit);
    of_total = label(QString(), u"faint"_s);
    of_total->setAlignment(Qt::AlignCenter);
    s->lay->addWidget(of_total);
    pend_note = label(QString(), u"faint"_s);     // the preview is not written yet (C++ only)
    pend_note->setAlignment(Qt::AlignCenter);
    pend_note->setWordWrap(true);
    pend_note->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::GOLD));
    pend_note->hide();
    s->lay->addWidget(pend_note);
    for (int sign : {-1, 1}) {
        auto *row = new QHBoxLayout();
        row->setSpacing(4);
        std::vector<double> steps(STEPS.rbegin(), STEPS.rend());   // .1% on the left up to 100% on the right
        for (double v : steps) {
            auto *b = new QPushButton((sign < 0 ? u"−"_s : u"+"_s) + pct_label(v));
            b->setObjectName(sign < 0 ? u"stepMinus"_s : u"stepPlus"_s);
            b->setCursor(Qt::PointingHandCursor);
            b->setMinimumWidth(0);
            b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            const double d = sign * v;
            connect(b, &QPushButton::clicked, this, [this, d] { _step_total(d); });   // (waits for Apply: C++ only)
            row->addWidget(b);
            step_buttons.push_back(b);
        }
        s->lay->addLayout(row);
    }
    lay->addWidget(s);

    // --- composition editor: culture / religion mix of the selected pops
    s = new Section();
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"Composition of the selected pops"_s, u"label"_s, false), 1);
    const std::vector<std::pair<QString, QString>> fields = {
        {u"culture"_s, u"Culture"_s}, {u"religion"_s, u"Religion"_s}, {u"type"_s, u"Pop type"_s}};
    for (const auto &[key, text] : fields) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == u"culture"_s);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { set_mix_field(k); });
        row->addWidget(b);
        mix_btns.insert(key, b);
    }
    s->lay->addLayout(row);
    mix_field = u"culture"_s;
    pie = new widgets::PieChart();
    pie->push = true;
    connect(pie, &widgets::PieChart::changed, this, [this] { _mix_changed(pie); });
    s->lay->addWidget(pie);
    mix_bar = new widgets::SplitBar();
    mix_bar->push = true;
    mix_bar->empty_text = QString();
    mix_bar->setFixedHeight(46);
    connect(mix_bar, &widgets::SplitBar::changed, this, [this] { _mix_changed(mix_bar); });
    s->lay->addWidget(mix_bar);
    mix_zoom = new widgets::ZoomBar();
    mix_zoom->empty_text = QString();
    mix_zoom->setFixedHeight(46);
    connect(mix_zoom, &widgets::ZoomBar::changed, this, [this] { _mix_changed(mix_zoom); });
    connect(mix_zoom, &widgets::ZoomBar::released, this, &EditPanel::_update_zoom);
    mix_zoom->hide();
    s->lay->addWidget(mix_zoom);
    s->lay->addWidget(label(u"Drag an edge of the pie or the bar; it pushes through the segments it passes. For tiny "
                            u"groups use the zoom bar or type an exact %."_s,
                            u"faint"_s));
    mix_ring_note = label(u"Outer ring = all pops of the selected provinces (ignoring the filter), "
                          u"previewing the change. \"all\" in the list below shows the same."_s,
                          u"faint"_s);
    s->lay->addWidget(mix_ring_note);
    mix_legend_host = new QWidget();
    mix_legend = new QGridLayout(mix_legend_host);
    mix_legend->setContentsMargins(0, 2, 0, 2);
    mix_legend->setHorizontalSpacing(8);
    mix_legend->setVerticalSpacing(2);
    s->lay->addWidget(mix_legend_host);
    row = new QHBoxLayout();
    mix_add_culture = new dialogs::CultureCombo(mod, backup_root, u"＋ Add culture…"_s);
    mix_add_religion = new dialogs::ReligionCombo(mod, backup_root, u"＋ Add religion…"_s);
    mix_add_type = new dialogs::PickCombo(dialogs::all_poptypes(mod), {{std::nullopt, u"＋ Add pop type…"_s}});
    for (dialogs::PickCombo *cb : {static_cast<dialogs::PickCombo *>(mix_add_culture),
                                   static_cast<dialogs::PickCombo *>(mix_add_religion), mix_add_type}) {
        cb->setCurrentIndex(cb->find(std::nullopt));
        cb->_last = cb->currentIndex();
        cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(cb, &QComboBox::activated, this, [this, cb](int) { _add_segment(cb); });
        row->addWidget(cb, 1);
    }
    mix_add_religion->hide();
    mix_add_type->hide();
    // culture groups / new cultures (common/cultures.txt) — next to where cultures are used
    cult_btn = new QPushButton(u"Edit cultures…"_s);
    cult_btn->setToolTip(u"The cultures and their groups: move cultures between groups, create new ones, "
                         u"and a group's own settings — union nation, leader pictures, unit graphics "
                         u"(common/cultures.txt)"_s);
    connect(cult_btn, &QPushButton::clicked, this, [this] { Q_EMIT culturesRequested(); });
    row->addWidget(cult_btn);
    rel_btn = new QPushButton(u"Edit religions…"_s);
    rel_btn->setToolTip(u"Religion groups: move religions between groups, create new religions (with an icon) "
                        u"and groups (common/religion.txt)"_s);
    connect(rel_btn, &QPushButton::clicked, this, [this] { Q_EMIT religionsRequested(); });
    rel_btn->hide();
    row->addWidget(rel_btn);
    s->lay->addLayout(row);
    mix_preview = label(QString(), u"faint"_s);
    s->lay->addWidget(mix_preview);
    row = new QHBoxLayout();
    // one Apply for the new total and the mix together (C++ only, the user 2026-11-13)
    mix_reset = new QPushButton(u"Discard"_s);
    mix_reset->setToolTip(u"Throw away the new total and mix that are not applied yet"_s);
    connect(mix_reset, &QPushButton::clicked, this, [this] { discard_pending(); });
    mix_apply = new QPushButton(u"Apply changes"_s);
    mix_apply->setObjectName(u"apply"_s);
    mix_apply->setToolTip(u"Write the new total and the mix into the pops — one step, Ctrl+Z takes it back"_s);
    connect(mix_apply, &QPushButton::clicked, this, [this] { apply_pending(); });
    row->addWidget(mix_reset);
    row->addWidget(mix_apply, 1);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- pop tools
    s = new Section();
    s->lay->addWidget(label(u"Pop tools (work on the filtered pops of the selection)"_s));
    auto *grid = new QGridLayout();
    grid->setSpacing(4);
    const std::vector<std::tuple<QString, QString, QString>> tools = {
        {u"copy"_s, u"Copy pops"_s, u"Copy the filtered pops of the selection"_s},
        {u"paste"_s, u"Paste pops…"_s, u"Paste copied pops into the selected provinces"_s},
        {u"add"_s, u"Add pop…"_s, u"Add a new pop to every selected province"_s}};
    for (int i = 0; i < int(tools.size()); ++i) {
        const auto &[key, text, tip] = tools[size_t(i)];
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { Q_EMIT toolRequested(k); });
        grid->addWidget(b, 0, i);
        tool_btns.insert(key, b);
    }
    s->lay->addLayout(grid);
    lay->addWidget(s);

    // --- state tools
    state_section = new Section();
    s = state_section;
    cap_label = label(QString(), u"sub"_s);
    cap_label->setWordWrap(true);
    cap_label->setTextFormat(Qt::RichText);
    row = new QHBoxLayout();
    row->addWidget(cap_label, 1);
    cap_btn = new QPushButton(u"Change state capital…"_s);
    connect(cap_btn, &QPushButton::clicked, this, [this] { Q_EMIT changeCapitalRequested(); });
    row->addWidget(cap_btn);
    s->lay->addLayout(row);
    cons_btn = new QPushButton();
    cons_btn->setObjectName(u"primary"_s);
    connect(cons_btn, &QPushButton::clicked, this, [this] { Q_EMIT consolidateRequested(); });
    s->lay->addWidget(cons_btn);
    cons_cfg = new QPushButton(u"Configure which types move →"_s);
    cons_cfg->setObjectName(u"link"_s);
    connect(cons_cfg, &QPushButton::clicked, this, [this] { _configure_elite(); });
    s->lay->addWidget(cons_cfg);
    lay->addWidget(s);

    // --- split
    s = new Section();
    split_btn = new QPushButton(u"Split pops…"_s);
    split_btn->setObjectName(u"action"_s);
    connect(split_btn, &QPushButton::clicked, this, [this] { Q_EMIT splitRequested(); });
    s->lay->addWidget(split_btn);
    lay->addWidget(s);

    // --- breakdown
    s = new Section();
    s->lay->addWidget(label(u"Pops in selection (dimmed = not selected by the filter)"_s));
    tree = new QTreeWidget();
    tree->setHeaderLabels({u"Type"_s, u"Culture"_s, u"Religion"_s, u"Size"_s, u"%"_s});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSortingEnabled(true);
    tree->setMinimumHeight(220);
    QHeaderView *h = tree->header();
    h->setMinimumSectionSize(30);
    const QHeaderView::ResizeMode modes[] = {QHeaderView::Stretch, QHeaderView::Stretch, QHeaderView::Stretch,
                                             QHeaderView::ResizeToContents, QHeaderView::ResizeToContents};
    for (int col = 0; col < 5; ++col)
        h->setSectionResizeMode(col, modes[col]);
    h->setStretchLastSection(false);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    s->lay->addWidget(tree);
    lay->addWidget(s, 1);
}

// ------------------------------------------------------------------
void EditPanel::set_elite_types(const QStringList &t)
{
    elite_types = t;
    _update_cons_text();
}

void EditPanel::_update_cons_text()
{
    QStringList ns;
    for (const QString &t : elite_types) {
        const moddata::PopType *pt = mod.poptypes.get(t);
        ns << (pt ? pt->name : t);
    }
    QString names = ns.join(u", "_s);
    if (names.isEmpty())
        names = u"(none)"_s;
    cons_btn->setText(u"Consolidate elites"_s);
    cons_btn->setToolTip(u"Moves %1 from every other province of the state into its state capital."_s.arg(names));
}

void EditPanel::_configure_elite()
{
    QMenu m(this);
    QStringList keys;
    for (const auto &[k, _v] : mod.poptypes)
        keys << k;
    std::stable_sort(keys.begin(), keys.end(),
                     [&](const QString &a, const QString &b) { return mod.poptype_sort_key(a) < mod.poptype_sort_key(b); });
    for (const QString &t : keys) {
        QAction *a = m.addAction(mod.poptypes.get(t)->name);
        a->setCheckable(true);
        a->setChecked(elite_types.contains(t));
        connect(a, &QAction::toggled, this, [this, t](bool on) { _toggle_elite(t, on); });
    }
    m.exec(cons_cfg->mapToGlobal(cons_cfg->rect().bottomLeft()));
}

void EditPanel::_toggle_elite(const QString &t, bool on)
{
    if (on && !elite_types.contains(t))
        elite_types.append(t);
    else if (!on && elite_types.contains(t))
        elite_types.removeOne(t);
    _update_cons_text();
    Q_EMIT eliteTypesChanged(elite_types);
}

void EditPanel::_filter_changed() { Q_EMIT filterChanged(); }

void EditPanel::_total_entered()
{
    QString txt = total_edit->text();
    txt.remove(u',').remove(u'.').remove(u' ');
    txt = txt.trimmed();
    // str.isdigit(): non-empty, every character a digit
    bool digits = !txt.isEmpty();
    for (QChar c : txt)
        digits = digits && c.isDigit();
    if (!digits || _pids.isEmpty() || _cur_total() <= 0)
        return;
    const long long t = txt.toLongLong();
    if (t == _new_total())
        return;
    if (!_begin(false))
        return;
    pend_total = t == _cur_total() ? std::nullopt : std::optional<long long>(t);
    _store();
    _update_mix_ui(false);
}

document::PopFilter EditPanel::current_filter() const
{
    return document::PopFilter(std::nullopt, std::nullopt, types->unchecked, from_chips(cultures->unchecked),
                               from_chips(religions->unchecked));
}

// ------------------------------------------------------------------
void EditPanel::refresh(document::Document &doc, const QList<long long> &pids, const Header &header,
                        const QStringList &states, std::optional<long long> split_source)
{
    title->setText(header.title);
    sub->setText(header.sub);
    info->setText(header.info);
    info->setVisible(!header.info.isEmpty());

    OrderedMap<QString, long long> tsum;
    Mix csum, rsum;
    std::vector<std::pair<document::PopKey, long long>> groups;
    std::map<document::PopKey, size_t> gidx;
    for (long long pid : pids)
        for (const moddata::Pop &p : doc.province_pops(pid)) {
            tsum[p.type] += p.size;
            csum[p.culture] += p.size;
            rsum[p.religion] += p.size;
            const document::PopKey k = document::pop_key(p);
            auto it = gidx.find(k);
            if (it == gidx.end()) {
                gidx.emplace(k, groups.size());
                groups.emplace_back(k, p.size);
            } else {
                groups[it->second].second += p.size;
            }
        }
    {
        std::vector<std::pair<QString, long long>> ts(tsum.begin(), tsum.end());
        std::stable_sort(ts.begin(), ts.end(), [&](const auto &a, const auto &b) {
            return mod.poptype_sort_key(a.first) < mod.poptype_sort_key(b.first);
        });
        std::vector<widgets::ChipGroup::ItemSpec> items;
        for (const auto &[t, v] : ts) {
            const moddata::PopType *pt = mod.poptypes.get(t);
            items.push_back({t, pt ? pt->name : t, pt ? pt->color : GREY, v});
        }
        types->set_items(items);
    }
    const auto by_size = [](const Mix &m) {
        std::vector<std::pair<OptStr, long long>> v(m.begin(), m.end());
        std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        return v;
    };
    {
        std::vector<widgets::ChipGroup::ItemSpec> items;
        for (const auto &[c, v] : by_size(csum)) {
            const moddata::Culture *e = c ? mod.cultures.get(*c) : nullptr;
            const QString none = (c && !c->isEmpty()) ? *c : u"(none)"_s;
            items.push_back({chip_key(c), e ? e->name : none, e ? e->color : GREY, v});
        }
        cultures->set_items(items);
    }
    {
        std::vector<widgets::ChipGroup::ItemSpec> items;
        for (const auto &[r, v] : by_size(rsum)) {
            const moddata::Religion *e = r ? mod.religions.get(*r) : nullptr;
            const QString none = (r && !r->isEmpty()) ? *r : u"(none)"_s;
            items.push_back({chip_key(r), e ? e->name : none, e ? e->color : GREY, v});
        }
        religions->set_items(items);
    }
    _update_totals(doc, pids);

    // breakdown table
    const document::PopFilter flt = current_filter();
    long long total = 0;
    for (const auto &g : groups)
        total += g.second;
    if (!total)
        total = 1;
    tree->setSortingEnabled(false);
    tree->clear();
    const QBrush dim{QColor(theme::INK_FAINT)};
    for (const auto &[k, v] : groups) {
        const auto &[t, c, r] = k;
        const moddata::PopType *pt = mod.poptypes.get(t);
        const moddata::Culture *ce = c ? mod.cultures.get(*c) : nullptr;
        const moddata::Religion *re = r ? mod.religions.get(*r) : nullptr;
        const QString cdash = (c && !c->isEmpty()) ? *c : u"—"_s, rdash = (r && !r->isEmpty()) ? *r : u"—"_s;
        auto *it = new SortItem(QStringList{pt ? pt->name : t, ce ? ce->name : cdash, re ? re->name : rdash, fmt(v),
                                            py::fmt_f(100.0 * double(v) / double(total), 1)});
        it->setData(3, Qt::UserRole, v);
        it->setData(4, Qt::UserRole, v);
        it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        it->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        it->setIcon(0, swatch_icon(pt ? pt->color : GREY));
        it->setIcon(1, swatch_icon(ce ? ce->color : GREY));
        it->setIcon(2, swatch_icon(re ? re->color : GREY));
        it->setData(0, Qt::UserRole + 1, QVariantList{t, c ? QVariant(*c) : QVariant(), r ? QVariant(*r) : QVariant()});
        moddata::Pop probe;
        probe.type = t;
        probe.culture = c;
        probe.religion = r;
        if (!flt.match(probe))
            for (int col = 0; col < 5; ++col)
                it->setForeground(col, dim);
        tree->addTopLevelItem(it);
    }
    tree->setSortingEnabled(true);
    tree->sortItems(3, Qt::DescendingOrder);

    // state tools
    state_section->setVisible(!states.isEmpty());
    if (!states.isEmpty()) {
        if (states.size() == 1) {
            const QString &rid = states[0];
            const std::optional<long long> cap = doc.capital(rid);
            const moddata::Province *cp = cap ? mod.provinces.get(*cap) : nullptr;
            const QString capname = cp ? cp->name : u"—"_s;
            cap_label->setText(u"State capital: <span style='color:%1'>★ %2</span> (#%3)"_s.arg(
                theme::GOLD, capname, cap ? QString::number(*cap) : u"None"_s));
            cap_btn->setEnabled(true);
        } else {
            cap_label->setText(u"%1 states selected — consolidation runs in each"_s.arg(states.size()));
            cap_btn->setEnabled(false);
        }
        _update_cons_text();
    }

    if (split_source && *split_source) {
        const moddata::Province *pr = mod.provinces.get(*split_source);
        split_btn->setText(u"Split pops of %1 (#%2)…"_s.arg(pr ? pr->name : QString()).arg(*split_source));
        split_btn->setToolTip(
            u"Divide the filtered pops of the province you clicked across other provinces by %."_s);
    } else {
        split_btn->setText(u"Split pops…"_s);
        split_btn->setToolTip(u"Click a province that has pops first."_s);
    }
    split_btn->setEnabled(split_source && *split_source && !doc.province_pops(*split_source).empty());
    for (QPushButton *b : step_buttons)
        b->setEnabled(!pids.isEmpty());
    for (const auto &[k, b] : tool_btns)
        b->setEnabled(!pids.isEmpty() && (k != u"paste"_s || can_paste));
}

// ------------------------------------------------------------------ composition editor
void EditPanel::set_mix_field(const QString &field)
{
    mix_field = field;
    for (const auto &[k, b] : mix_btns)
        b->setChecked(k == field);
    mix_add_culture->setVisible(field == u"culture"_s);
    cult_btn->setVisible(field == u"culture"_s);
    rel_btn->setVisible(field == u"religion"_s);
    mix_add_religion->setVisible(field == u"religion"_s);
    mix_add_type->setVisible(field == u"type"_s);
    _rebuild_mix();
}

std::pair<QString, Rgb> EditPanel::_meta(const OptStr &key) const
{
    const QString fallback = (key && !key->isEmpty()) ? *key : u"—"_s;
    if (!key)
        return {fallback, GREY};
    if (mix_field == u"culture"_s) {
        if (const moddata::Culture *e = mod.cultures.get(*key))
            return {e->name, e->color};
    } else if (mix_field == u"religion"_s) {
        if (const moddata::Religion *e = mod.religions.get(*key))
            return {e->name, e->color};
    } else if (const moddata::PopType *e = mod.poptypes.get(*key)) {
        return {e->name, e->color};
    }
    return {fallback, GREY};
}

void EditPanel::_rebuild_mix()
{
    // Segments = current shares of the filtered pops (by culture or religion).
    Mix cur;
    if (_doc)
        for (const moddata::Pop *p : _doc->selected_pops(_pids, current_filter()))
            cur[_field_of(*p)] += p->size;
    mix_current = cur;
    Mix allc;
    if (_doc)
        for (long long pid : _pids)
            for (const moddata::Pop &p : _doc->province_pops(pid))
                allc[_field_of(p)] += p.size;
    mix_all = allc;
    if (pend_shown()) {                           // the waiting change belongs here: shown again, kept
        _show_snap(pend->now);
        _last = pend->now;
        Q_EMIT pendingChanged();
        return;
    }
    pend_total.reset();
    long long total = 0;
    for (const auto &[_k, v] : cur)
        total += v;
    mix_added.clear();
    mix_segments = std::make_shared<std::vector<Segment>>();
    std::vector<OptStr> keys;
    for (const auto &[k, _v] : cur)
        keys.push_back(k);
    for (const OptStr &k : _order(keys)) {
        const auto [name, color] = _meta(k);
        Segment sg;
        sg.key = k;
        sg.label = name;
        sg.color = color;
        sg.locked = false;
        sg.pct = total ? 100.0 * double(cur.value(k)) / double(total) : 0.0;
        mix_segments->push_back(sg);
    }
    _set_all_segments();
    _update_mix_ui();
    _last = _snap();
    _last_mix_ms = -1;
    Q_EMIT pendingChanged();
}

std::vector<OptStr> EditPanel::_order(const std::vector<OptStr> &keys) const
{
    // One fixed order for inner pie, bar and outer ring: smallest to biggest in the whole selection (clockwise from
    // 12 o'clock / left to right), then newly added keys, so filtering never flips the order.
    const auto added = [&](const OptStr &k) { return std::find(mix_added.begin(), mix_added.end(), k) != mix_added.end(); };
    std::vector<OptStr> rest;
    for (const OptStr &k : keys)
        if (!added(k))
            rest.push_back(k);
    std::stable_sort(rest.begin(), rest.end(), [&](const OptStr &a, const OptStr &b) {
        const long long va = mix_all.value(a), vb = mix_all.value(b);
        if (va != vb)
            return va < vb;
        return py_str(a) < py_str(b);
    });
    for (const OptStr &k : mix_added)
        if (std::find(keys.begin(), keys.end(), k) != keys.end())
            rest.push_back(k);
    return rest;
}

void EditPanel::_set_all_segments()
{
    pie->set_segments(mix_segments);
    mix_bar->set_segments(mix_segments);
    mix_zoom->set_segments(mix_segments);
    _update_zoom();
}

void EditPanel::_update_zoom()
{
    // Show the zoom bar when some segment is too thin to see/grab on the main bar (< 3 %).
    // Works the same for culture, religion and pop type. Zooms on the run of small (< 5 %) segments at the end of
    // the bar, or at the start (where newly added ones sit).
    const std::vector<Segment> &segs = *mix_segments;
    const auto tiny = [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i)
            if (0 < segs[i].pct && segs[i].pct < 3.0)
                return true;
        return false;
    };
    const size_t n = segs.size();
    size_t t = n;
    while (t > 0 && segs[t - 1].pct < 5.0)
        --t;
    py::Sum ts;
    for (size_t i = t; i < n; ++i)
        ts.add(segs[i].pct);
    const double tail = ts.value();
    size_t h = 0;
    while (h < n && segs[h].pct < 5.0)
        ++h;
    py::Sum hs;
    for (size_t i = 0; i < h; ++i)
        hs.add(segs[i].pct);
    const double head = hs.value();
    if (n > 1 && t < n && tiny(t, n) && tail <= 30) {
        mix_zoom->set_range(100.0 - tail - std::max(1.5, tail * 0.6), 100.0);
    } else if (n > 1 && h > 0 && tiny(0, h) && head <= 30) {
        mix_zoom->set_range(0.0, head + std::max(1.5, head * 0.6));
    } else {
        mix_zoom->hide();
        return;
    }
    mix_zoom->show();
}

void EditPanel::_add_segment(dialogs::PickCombo *combo)
{
    const OptStr key = combo->key();
    combo->setCurrentIndex(combo->find(std::nullopt));
    combo->_last = combo->currentIndex();
    if (!key || key->isEmpty() || mix_current.isEmpty())
        return;
    for (const Segment &sg : *mix_segments)
        if (sg.key == key)
            return;
    if (!_begin(false))
        return;
    const auto [name, color] = _meta(key);
    // new segments go to the end: just left of 12 o'clock / the bar's right end, next to the biggest group, and open
    // counter-clockwise by taking from it
    mix_added.push_back(key);
    Segment sg;
    sg.key = key;
    sg.label = name;
    sg.color = color;
    sg.pct = 0.0;
    mix_segments->push_back(sg);
    _set_all_segments();
    _update_mix_ui();
    _store();
}

void EditPanel::_mix_changed(QWidget *source)
{
    for (QWidget *w : {static_cast<QWidget *>(pie), static_cast<QWidget *>(mix_bar), static_cast<QWidget *>(mix_zoom)})
        if (w != source)
            w->update();
    if (source != mix_zoom)
        _update_zoom();
    _after_mix_edit();
}

void EditPanel::_after_mix_edit()
{
    // a drag (or a typed share) changed the segments in place: a step of the preview (drags close together: one)
    if (!_begin(true))
        return;
    _update_mix_ui(false);
    _store();
}

void EditPanel::_set_pct(int i, double value)
{
    // Exact % for one segment; the others give/take the difference in proportion.
    std::vector<Segment> &segs = *mix_segments;
    value = std::max(0.0, std::min(100.0, value));
    std::vector<Segment *> others;
    for (size_t j = 0; j < segs.size(); ++j)
        if (int(j) != i)
            others.push_back(&segs[j]);
    py::Sum ro;
    for (Segment *sg : others)
        ro.add(sg->pct);
    const double rest_old = ro.value();
    const double rest_new = 100.0 - value;
    if (rest_old > 0) {
        for (Segment *sg : others)
            sg->pct = sg->pct * rest_new / rest_old;
    } else if (!others.empty()) {
        for (Segment *sg : others)
            sg->pct = rest_new / double(others.size());
    } else {
        value = 100.0;
    }
    segs[size_t(i)].pct = value;
    _set_all_segments();
    _after_mix_edit();
}

Mix EditPanel::mix_targets() const
{
    const long long total = _new_total();         // (the new total when one waits: C++ only)
    std::vector<double> w;
    for (const Segment &sg : *mix_segments)
        w.push_back(sg.pct);
    const std::vector<long long> people = !mix_segments->empty() ? document::distribute(total, w)
                                                                 : std::vector<long long>{};
    Mix out;
    for (size_t i = 0; i < mix_segments->size() && i < people.size(); ++i)
        out.insert((*mix_segments)[i].key, people[i]);
    return out;
}

void EditPanel::_update_mix_ui(bool rebuild_legend)
{
    long long total = 0;
    for (const auto &[_k, v] : mix_current)
        total += v;
    const long long nt = _new_total();
    const Mix targets = mix_targets();
    // the pops as they are, scaled to the new total (what a new total alone would make): the mix moves from there
    Mix scaled;
    {
        std::vector<OptStr> ks;
        std::vector<double> w;
        for (const auto &[k, v] : mix_current) {
            ks.push_back(k);
            w.push_back(double(v));
        }
        const std::vector<long long> sp = ks.empty() ? std::vector<long long>{} : document::distribute(nt, w);
        for (size_t i = 0; i < ks.size(); ++i)
            scaled.insert(ks[i], sp[i]);
    }
    const bool mix_moved = _snap().mix_moved;
    if (rebuild_legend) {
        clear_grid(mix_legend);
        mix_rows.clear();
        for (int i = 0; i < int(mix_segments->size()); ++i) {
            const Segment &sg = (*mix_segments)[size_t(i)];
            auto *sw = new QLabel();
            sw->setFixedSize(10, 10);
            sw->setStyleSheet(swatch_style(sg.color));
            auto *nm = new QLabel(sg.label);
            nm->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto *spin = new QDoubleSpinBox();
            spin->setRange(0, 100);
            spin->setDecimals(2);
            spin->setSuffix(u" %"_s);
            spin->setButtonSymbols(QDoubleSpinBox::NoButtons);
            spin->setKeyboardTracking(false);
            spin->setFixedWidth(74);
            spin->setFixedHeight(22);
            spin->setStyleSheet(u"padding: 0 6px;"_s);
            spin->setToolTip(u"Type an exact share; the other segments adjust in proportion"_s);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, i](double v) { _set_pct(i, v); });
            QLabel *val = label(QString(), u"sub"_s, false);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            mix_legend->addWidget(sw, i, 0);
            mix_legend->addWidget(nm, i, 1);
            mix_legend->addWidget(spin, i, 2);
            mix_legend->addWidget(val, i, 3);
            mix_rows.emplace_back(spin, val);
        }
        mix_legend->setColumnStretch(1, 1);
    }
    // whole selection (ignoring the filter) after the change: shown as the pie's outer ring
    Mix allc = mix_all;
    std::vector<OptStr> touched;
    for (const auto &[k, _v] : targets)
        touched.push_back(k);
    for (const auto &[k, _v] : mix_current)
        if (std::find(touched.begin(), touched.end(), k) == touched.end())
            touched.push_back(k);
    for (const OptStr &k : touched)
        allc.insert(k, allc.value(k) + targets.value(k) - mix_current.value(k));
    long long all_total = 0;
    for (const auto &[_k, v] : mix_all)
        all_total += v;
    const long long all_old = all_total;
    all_total += nt - total;                      // (the whole selection after a new total)
    std::vector<OptStr> keys;
    for (const auto &[k, _v] : allc)
        keys.push_back(k);
    for (const auto &[k, _v] : targets)
        if (std::find(keys.begin(), keys.end(), k) == keys.end())
            keys.push_back(k);
    std::vector<Segment> outer;
    for (const OptStr &k : _order(keys))
        if (all_total && allc.value(k) > 0) {
            const auto [name, color] = _meta(k);
            Segment sg;
            sg.label = name;
            sg.color = color;
            sg.pct = 100.0 * double(allc.value(k)) / double(all_total);
            outer.push_back(sg);
        }
    const bool filtered = total != all_old;
    pie->set_outer(filtered ? outer : std::vector<Segment>{});
    long long moved = 0;
    for (size_t i = 0; i < mix_segments->size() && i < mix_rows.size(); ++i) {
        const Segment &sg = (*mix_segments)[i];
        auto [spin, val] = mix_rows[i];
        spin->blockSignals(true);
        spin->setValue(sg.pct);
        spin->blockSignals(false);
        const long long cur = mix_current.value(sg.key);
        const long long nw = targets.value(sg.key);
        const long long d = mix_moved ? nw - scaled.value(sg.key) : 0;
        moved += std::max(0LL, d);
        const double cur_pct = total ? 100.0 * double(cur) / double(total) : 0;
        QString text = u"from %1%"_s.arg(py::fmt_f(cur_pct, 1));
        if (filtered && all_total && all_old) {
            const double a0 = 100.0 * double(mix_all.value(sg.key)) / double(all_old);
            const double a1 = 100.0 * double(allc.value(sg.key)) / double(all_total);
            text += u"   all %1 → %2%"_s.arg(py::fmt_f(a0, 1), py::fmt_f(a1, 1));
        }
        val->setText(text + u"   "_s + (d ? signed_commas(d) : u"±0"_s));
    }
    mix_ring_note->setVisible(filtered && all_total);
    QString pv;
    if (!total)
        pv = u"Select provinces with pops to see their mix."_s;
    else if (nt != total)
        pv = u"New total %1 (%2)"_s.arg(fmt(nt), signed_commas(nt - total)) +
             (mix_moved ? u" · the mix moves %1 people"_s.arg(fmt(moved)) : QString()) + u" — not applied yet"_s;
    else
        pv = u"Moves %1 of %2 people"_s.arg(fmt(moved), fmt(total)) + (mix_moved ? u" — not applied yet"_s : QString());
    mix_preview->setText(pv);
    _show_total();
}

void EditPanel::_apply_mix() { apply_pending(); }

void EditPanel::_update_chip_counts(document::Document &doc, const QList<long long> &pids)
{
    // Bracket numbers: for each chip, how many people also match the other two filter groups.
    const std::vector<std::pair<QString, widgets::ChipGroup *>> groups = {
        {u"type"_s, types}, {u"culture"_s, cultures}, {u"religion"_s, religions}};
    for (const auto &[field, grp] : groups) {
        std::vector<std::pair<QString, const QSet<QString> *>> others;
        for (const auto &[f, g] : groups)
            if (g != grp)
                others.emplace_back(f, &g->unchecked);
        if (std::none_of(others.begin(), others.end(), [](const auto &o) { return !o.second->isEmpty(); })) {
            grp->set_narrowed(std::nullopt);
            continue;
        }
        OrderedMap<QString, long long> counts;
        for (long long pid : pids)
            for (const moddata::Pop &p : doc.province_pops(pid)) {
                bool ok = true;
                for (const auto &[f, unchecked] : others)
                    if (unchecked->contains(chip_key(pop_field(p, f)))) {
                        ok = false;
                        break;
                    }
                if (ok)
                    counts[chip_key(pop_field(p, field))] += p.size;
            }
        grp->set_narrowed(counts);
    }
}

void EditPanel::_update_totals(document::Document &doc, const QList<long long> &pids)
{
    _doc = &doc;
    _pids = pids;
    _update_chip_counts(doc, pids);
    _rebuild_mix();
    const document::PopFilter flt = current_filter();
    (void)flt;
    _show_total();
    step_label->setText(u"Selected pop total"_s);
}

// ------------------------------------------------------------------ the unapplied change (C++ only, 2026-11-13)
long long EditPanel::_cur_total() const
{
    long long sel = 0;
    if (_doc)
        for (const moddata::Pop *p : _doc->selected_pops(_pids, current_filter()))
            sel += p->size;
    return sel;
}

void EditPanel::_show_total()
{
    long long allp = 0;
    if (_doc)
        for (long long pid : _pids)
            allp += _doc->total(pid);
    const long long cur = _cur_total(), nt = _new_total();
    total_edit->setText(!_pids.isEmpty() ? fmt(nt) : u"—"_s);
    of_total->setText(!_pids.isEmpty() ? u"of %1 total pop in %2 province%3"_s.arg(fmt(allp + nt - cur))
                                             .arg(_pids.size())
                                             .arg(_pids.size() == 1 ? QString() : u"s"_s)
                                       : QString());
    const bool live = pend_shown() && (pend->now.total || pend->now.mix_moved);
    if (live)
        pend_note->setText(nt != cur ? u"Not applied yet — now %1 (%2). Press Apply changes below."_s.arg(
                                           fmt(cur), signed_commas(nt - cur))
                                     : u"The new mix is not applied yet. Press Apply changes below."_s);
    pend_note->setVisible(live);
    mix_apply->setEnabled(live);
    mix_reset->setEnabled(live);
}

bool EditPanel::pend_shown() const
{
    return pend && pend->pids == _pids && pend->field == mix_field && pend->un_types == types->unchecked &&
           pend->un_cultures == cultures->unchecked && pend->un_religions == religions->unchecked;
}

PendSnap EditPanel::_snap() const
{
    PendSnap sn;
    sn.total = pend_total;
    sn.segs = *mix_segments;
    sn.added = mix_added;
    long long total = 0;
    for (const auto &[_k, v] : mix_current)
        total += v;
    for (const Segment &sg : sn.segs) {
        const double own = total ? 100.0 * double(mix_current.value(sg.key)) / double(total) : 0.0;
        if (std::abs(sg.pct - own) > 1e-7)
            sn.mix_moved = true;
    }
    return sn;
}

static const void *top_of(const std::vector<document::StepP> &st) { return st.empty() ? nullptr : st.back().get(); }

bool EditPanel::_begin(bool mix_drag)
{
    if (pend && !pend_shown()) {
        if (pend->now.total || pend->now.mix_moved) {
            _show_snap(_last);                    // (a drag moved the segments already: back)
            Q_EMIT pendingRefused();
            return false;
        }
        pend.reset();                             // (nothing left in it: gone)
    }
    if (!pend) {
        Pending p;
        p.pids = _pids;
        p.un_types = types->unchecked;
        p.un_cultures = cultures->unchecked;
        p.un_religions = religions->unchecked;
        p.field = mix_field;
        p.base_total = _cur_total();
        p.now = _last;
        pend = p;
    }
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    const bool merge = mix_drag && _last_mix_ms >= 0 && t - _last_mix_ms < 600 && !pend->undo.empty();
    if (!merge)
        pend->undo.push_back(_last);
    pend->redo.clear();
    _last_mix_ms = mix_drag ? t : -1;
    return true;
}

void EditPanel::_store()
{
    if (!pend)
        return;
    _last = _snap();
    pend->now = _last;
    pend->doc_top = _doc ? top_of(_doc->undo_stack) : nullptr;
    _show_total();
    Q_EMIT pendingChanged();
}

void EditPanel::_show_snap(const PendSnap &sn)
{
    pend_total = sn.total;
    mix_segments = std::make_shared<std::vector<Segment>>(sn.segs);
    mix_added = sn.added;
    _set_all_segments();
    _update_mix_ui();
}

void EditPanel::_step_total(double pct)
{
    // the ± buttons: from the total shown (a new one waiting, else the pops'), as the clicks did before
    if (_pids.isEmpty() || _cur_total() <= 0)
        return;
    const long long cur = _new_total();
    long long target = py::round_int(double(cur) * (1 + pct / 100.0));
    if (target == cur && pct != 0)                // small pops: always move at least one
        target = cur + (pct > 0 ? 1 : -1);
    target = std::max<long long>(0, target);
    if (target == cur || !_begin(false))
        return;
    pend_total = target == _cur_total() ? std::nullopt : std::optional<long long>(target);
    _store();
    _update_mix_ui(false);
}

void EditPanel::apply_pending()
{
    if (!pend || !(pend->now.total || pend->now.mix_moved))
        return;
    const Pending p = *pend;
    pend.reset();
    pend_total.reset();
    _last_mix_ms = -1;
    const document::PopFilter flt(std::nullopt, std::nullopt, p.un_types, from_chips(p.un_cultures),
                                  from_chips(p.un_religions));
    std::vector<std::pair<OptStr, double>> shares;
    if (p.now.mix_moved)
        for (const Segment &sg : p.now.segs)
            shares.emplace_back(sg.key, sg.pct);
    Q_EMIT changesRequested(p.pids, flt, p.now.total, p.field, shares);
    _rebuild_mix();                               // (the pops as they are now; emits pendingChanged)
}

void EditPanel::discard_pending()
{
    pend.reset();
    pend_total.reset();
    _last_mix_ms = -1;
    _rebuild_mix();
}

bool EditPanel::pending_undo()
{
    // the preview's own steps, newest first — unless a change of the document came after it (that one goes first)
    if (!pend || pend->undo.empty() || !_doc || top_of(_doc->undo_stack) != pend->doc_top)
        return false;
    pend->redo.push_back(pend->now);
    pend->now = pend->undo.back();
    pend->undo.pop_back();
    pend->doc_redo_top = top_of(_doc->redo_stack);
    _last_mix_ms = -1;
    if (pend_shown()) {
        _show_snap(pend->now);
        _last = pend->now;
    }
    Q_EMIT pendingChanged();
    return true;
}

bool EditPanel::pending_redo()
{
    if (!pend || pend->redo.empty() || !_doc || top_of(_doc->undo_stack) != pend->doc_top ||
        top_of(_doc->redo_stack) != pend->doc_redo_top)
        return false;
    pend->undo.push_back(pend->now);
    pend->now = pend->redo.back();
    pend->redo.pop_back();
    _last_mix_ms = -1;
    if (pend_shown()) {
        _show_snap(pend->now);
        _last = pend->now;
    }
    Q_EMIT pendingChanged();
    return true;
}

bool EditPanel::pending_live() const { return pend && (pend->now.total || pend->now.mix_moved); }

QString EditPanel::pending_text() const
{
    if (!pending_live())
        return QString();
    QStringList what;
    if (pend->now.total)
        what << u"total %1 → %2"_s.arg(fmt(pend->base_total), fmt(*pend->now.total));
    if (pend->now.mix_moved)
        what << u"a new %1 mix"_s.arg(pend->field == u"type"_s ? u"pop type"_s : pend->field);
    QString where;
    if (pend->pids.size() == 1) {
        const moddata::Province *pr = mod.provinces.get(pend->pids[0]);
        where = u"%1 (#%2)"_s.arg(pr ? pr->name : QString()).arg(pend->pids[0]);
    } else {
        where = u"%1 provinces"_s.arg(pend->pids.size());
    }
    return u"Pop changes in %1 are not applied yet: %2."_s.arg(where, what.join(u", "_s));
}


void EditPanel::refresh_filter_only(document::Document &doc, const QList<long long> &pids)
{
    // Filter changed: update totals and dimming without rebuilding the chips.
    _update_totals(doc, pids);
    const document::PopFilter flt = current_filter();
    const QBrush dim{QColor(theme::INK_FAINT)};
    const QBrush ink{QColor(theme::INK)};
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        const QVariantList d = it->data(0, Qt::UserRole + 1).toList();
        moddata::Pop probe;
        probe.type = d.value(0).toString();
        if (d.value(1).isValid())
            probe.culture = d.value(1).toString();
        if (d.value(2).isValid())
            probe.religion = d.value(2).toString();
        const QBrush &b = flt.match(probe) ? ink : dim;
        for (int col = 0; col < 5; ++col)
            it->setForeground(col, b);
    }
}

bool SortItem::operator<(const QTreeWidgetItem &other) const
{
    const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
    const QVariant a = data(col, Qt::UserRole), b = other.data(col, Qt::UserRole);
    if (a.isValid() && b.isValid())
        return a.toLongLong() < b.toLongLong();
    return text(col) < other.text(col);
}

// ---------------------------------------------------------------------------------------------- split
SplitPanel::SplitPanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new Section();
    title = label(u"Split pops"_s, u"title"_s);
    src_label = label(QString(), u"sub"_s);
    src_label->setWordWrap(true);
    moving = label(QString(), u"sub"_s);
    moving->setWordWrap(true);
    s->lay->addWidget(title);
    s->lay->addWidget(src_label);
    s->lay->addWidget(moving);
    lay->addWidget(s);

    s = new Section();
    s->lay->addWidget(label(u"Targets: click provinces on the map (click again to remove), or type IDs"_s));
    auto *row = new QHBoxLayout();
    id_edit = new QLineEdit();
    id_edit->setPlaceholderText(u"e.g. 1053 1054 1056"_s);
    connect(id_edit, &QLineEdit::returnPressed, this, &SplitPanel::_add_typed);
    auto *add = new QPushButton(u"Add"_s);
    connect(add, &QPushButton::clicked, this, [this] { _add_typed(); });
    row->addWidget(id_edit, 1);
    row->addWidget(add);
    s->lay->addLayout(row);
    keep_cb = new QCheckBox(u"Keep a share in the source province"_s);
    connect(keep_cb, &QCheckBox::toggled, this, &SplitPanel::_keep_toggled);
    s->lay->addWidget(keep_cb);
    bar = new widgets::SplitBar();
    connect(bar, &widgets::SplitBar::changed, this, &SplitPanel::_bar_changed);
    s->lay->addWidget(bar);
    row = new QHBoxLayout();
    row->addWidget(label(u"Arrange:"_s));
    const std::vector<std::tuple<QString, QString, QString>> presets = {
        {u"even"_s, u"Even"_s, u"Equal share for every target"_s},
        {u"pop"_s, u"By population"_s, u"Proportional to each target's current population"_s},
        {u"area"_s, u"By area"_s, u"Proportional to each target's size on the map"_s}};
    for (const auto &[key, text, tip] : presets) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { apply_preset(k); });
        row->addWidget(b);
        preset_btns.insert(key, b);
    }
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(label(u"Drag the dividers, or type exact values. Locked (🔒) rows keep their share."_s, u"faint"_s));
    rows_host = new QWidget();
    rows = new QGridLayout(rows_host);
    rows->setContentsMargins(0, 4, 0, 0);
    rows->setHorizontalSpacing(6);
    rows->setVerticalSpacing(3);
    s->lay->addWidget(rows_host);
    lay->addWidget(s);

    s = new Section();
    row = new QHBoxLayout();
    ph_cb = new QCheckBox(u"Leave pops of size ≤"_s);
    ph_cb->setChecked(true);
    ph_cb->setToolTip(u"Pops at or below this size stay in the source (e.g. the 100-farmer placeholders)"_s);
    ph_spin = new QSpinBox();
    ph_spin->setRange(0, 100000);
    ph_spin->setValue(100);
    connect(ph_cb, &QCheckBox::toggled, this, [this](bool) { _update_preview(); });
    connect(ph_spin, &QSpinBox::valueChanged, this, [this](int) { _update_preview(); });
    row->addWidget(ph_cb);
    row->addWidget(ph_spin);
    row->addWidget(label(u"in the source"_s, u"sub"_s, false));
    row->addStretch(1);
    s->lay->addLayout(row);
    s->lay->addWidget(label(u"Moved pops merge into the target's pop with the same type, culture and religion; "
                            u"otherwise a new pop entry is created. Leftover 1s go to earlier targets."_s,
                            u"faint"_s));
    row = new QHBoxLayout();
    apply_btn = new QPushButton(u"Apply split"_s);
    apply_btn->setObjectName(u"apply"_s);
    connect(apply_btn, &QPushButton::clicked, this, [this] { Q_EMIT applyRequested(); });
    auto *cancel = new QPushButton(u"Cancel"_s);
    connect(cancel, &QPushButton::clicked, this, [this] { Q_EMIT cancelRequested(); });
    row->addWidget(apply_btn);
    row->addWidget(cancel);
    row->addStretch(1);
    s->lay->addLayout(row);
    warn = label(QString(), u"warn"_s);
    warn->hide();
    s->lay->addWidget(warn);
    lay->addWidget(s);
    lay->addStretch(1);
}

// ------------------------------------------------------------------
void SplitPanel::start(document::Document &doc_, long long source_, const document::PopFilter &flt_,
                       const QString &filter_desc_)
{
    doc = &doc_;
    source = source_;
    flt = flt_;
    targets = std::make_shared<std::vector<Segment>>();
    _color_i = 0;
    keep_cb->blockSignals(true);
    keep_cb->setChecked(false);
    keep_cb->blockSignals(false);
    const moddata::Province *pr = mod.provinces.get(source_);
    src_label->setText(u"From: <b>%1</b> (#%2)"_s.arg(pr ? pr->name : QString()).arg(source_));
    src_label->setTextFormat(Qt::RichText);
    filter_desc = filter_desc_;
    apply_preset(u"even"_s);
}

long long SplitPanel::moving_total() const
{
    const long long lim = ph_cb->isChecked() ? ph_spin->value() : 0;
    long long total = 0;
    for (const moddata::Pop &p : doc->province_pops(*source))
        if (flt->match(p) && !(lim && p.size <= lim))
            total += p.size;
    return total;
}

std::vector<long long> SplitPanel::target_pids() const
{
    std::vector<long long> out;
    for (const Segment &t : *targets)
        if (!source || t.pid != *source)
            out.push_back(t.pid);
    return out;
}

std::vector<std::pair<long long, double>> SplitPanel::weights() const
{
    std::vector<std::pair<long long, double>> out;
    for (const Segment &t : *targets)
        out.emplace_back(t.pid, t.pct);
    return out;
}

void SplitPanel::toggle_target(long long pid)
{
    if (source && pid == *source) {
        keep_cb->setChecked(!keep_cb->isChecked());
        return;
    }
    const std::vector<long long> tp = target_pids();
    if (std::find(tp.begin(), tp.end(), pid) != tp.end())
        _remove(pid);
    else
        _add(pid);
}

Rgb SplitPanel::_next_color()
{
    const Rgb c = widgets::SEG_COLORS[size_t(_color_i % (long long)widgets::SEG_COLORS.size())];
    _color_i += 1;
    return c;
}

void SplitPanel::_add(long long pid)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    const std::vector<long long> tp = target_pids();
    if (!pr || pr->sea || std::find(tp.begin(), tp.end(), pid) != tp.end())
        return;
    const double n = double(targets->size() + 1);
    Segment t;
    t.pid = pid;
    t.pct = 0.0;
    t.locked = false;
    t.color = _next_color();
    t.label = QString::number(pid);
    if (mode == u"manual"_s) {
        const double share = 100.0 / n;
        _take(share, nullptr);
        t.pct = share;
        targets->push_back(t);
        _normalize();
    } else {
        targets->push_back(t);
    }
    _restructured();
}

void SplitPanel::_remove(long long pid)
{
    auto it = std::find_if(targets->begin(), targets->end(), [&](const Segment &t) { return t.pid == pid; });
    if (it == targets->end())
        return;
    const Segment t = *it;
    targets->erase(it);
    if (mode == u"manual"_s)
        _give(t.pct);
    _restructured();
}

void SplitPanel::_keep_toggled(bool on)
{
    if (on) {
        Segment t;
        t.pid = source.value_or(0);
        t.pct = 0.0;
        t.locked = false;
        t.color = {120, 120, 120};
        t.label = u"stays"_s;
        targets->insert(targets->begin(), t);
        if (mode == u"manual"_s) {
            const double share = 100.0 / double(targets->size());
            _take(share, nullptr);
            (*targets)[0].pct = share;
        }
    } else {
        auto nt = std::make_shared<std::vector<Segment>>();
        for (const Segment &t : *targets)
            if (!source || t.pid != *source)
                nt->push_back(t);
        targets = nt;
        if (mode == u"manual"_s)
            _normalize();
    }
    _restructured();
}

void SplitPanel::_restructured()
{
    if (mode == u"manual"_s)
        _after_change();
    else
        apply_preset(mode);
}

void SplitPanel::_add_typed()
{
    QString text = id_edit->text();
    text.replace(u',', u' ');
    for (const QString &tok : text.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts)) {
        bool digits = true;
        for (QChar c : tok)
            digits = digits && c.isDigit();
        if (digits) {
            const long long pid = tok.toLongLong();
            const std::vector<long long> tp = target_pids();
            if ((!source || pid != *source) && std::find(tp.begin(), tp.end(), pid) == tp.end())
                _add(pid);
        }
    }
    id_edit->clear();
}

std::vector<Segment *> SplitPanel::_unlocked(const Segment *exclude)
{
    std::vector<Segment *> out;
    for (Segment &t : *targets)
        if (!t.locked && &t != exclude)
            out.push_back(&t);
    return out;
}

void SplitPanel::_take(double amount, const Segment *exclude)
{
    // Remove `amount` pct from unlocked targets proportionally.
    const std::vector<Segment *> pool = _unlocked(exclude);
    py::Sum s;
    for (Segment *t : pool)
        s.add(t->pct);
    const double tot = s.value();
    if (tot <= 0)
        return;
    for (Segment *t : pool)
        t->pct = std::max(0.0, t->pct - amount * t->pct / tot);
}

void SplitPanel::_give(double amount, const Segment *exclude)
{
    std::vector<Segment *> pool = _unlocked(exclude);
    if (pool.empty())
        for (Segment &t : *targets)
            if (&t != exclude)
                pool.push_back(&t);
    if (pool.empty())
        return;
    py::Sum s;
    for (Segment *t : pool)
        s.add(t->pct);
    const double tot = s.value();
    for (Segment *t : pool)
        t->pct += amount * (tot > 0 ? t->pct / tot : 1.0 / double(pool.size()));
}

void SplitPanel::_normalize()
{
    py::Sum s;
    for (const Segment &t : *targets)
        s.add(t.pct);
    const double tot = s.value();
    if (!targets->empty() && std::abs(tot - 100.0) > 1e-6) {
        if (tot < 100)
            _give(100.0 - tot);
        else
            _take(tot - 100.0, nullptr);
    }
}

void SplitPanel::apply_preset(const QString &key)
{
    mode = key;
    for (const auto &[k, b] : preset_btns)
        b->setChecked(k == key);
    if (!targets->empty()) {
        py::Sum ls;
        std::vector<Segment *> free;
        for (Segment &t : *targets) {
            if (t.locked)
                ls.add(t.pct);
            else
                free.push_back(&t);
        }
        const double avail = std::max(0.0, 100.0 - ls.value());
        std::vector<double> w;
        double s = 0;
        if (key == u"even"_s) {
            w.assign(free.size(), 1.0);
            s = double(free.size());
        } else {
            long long si = 0;
            for (Segment *t : free) {
                long long wi = 1;
                if (key == u"pop"_s)
                    wi = std::max(1LL, doc->total(t->pid));
                else if (mod_area && t->pid < (long long)mod_area->size())
                    wi = std::max(1LL, (long long)(*mod_area)[size_t(t->pid)]);
                w.push_back(double(wi));
                si += wi;
            }
            s = double(si);
        }
        if (s == 0)
            s = 1.0;
        for (size_t i = 0; i < free.size(); ++i)
            free[i]->pct = avail * w[i] / s;
    }
    _after_change();
}

void SplitPanel::_set_manual()
{
    mode = u"manual"_s;
    for (const auto &[_k, b] : preset_btns)
        b->setChecked(false);
}

void SplitPanel::_bar_changed()
{
    _set_manual();
    _sync_rows();
    _update_preview();
}

void SplitPanel::_pct_edited(Segment *t, double value)
{
    _set_manual();
    std::vector<Segment *> others;
    py::Sum ls;
    for (Segment &o : *targets) {
        if (&o == t)
            continue;
        if (o.locked)
            ls.add(o.pct);
        else
            others.push_back(&o);
    }
    const double locked = ls.value();
    value = std::max(0.0, std::min(100.0 - locked, value));
    if (others.empty())
        value = 100.0 - locked;
    const double delta = value - t->pct;
    t->pct = value;
    if (delta > 0)
        _take(delta, t);
    else
        _give(-delta, t);
    // guard rounding drift
    py::Sum ts;
    for (const Segment &o : *targets)
        ts.add(o.pct);
    const double tot = ts.value();
    if (!others.empty() && std::abs(tot - 100) > 1e-6)
        others.back()->pct = std::max(0.0, others.back()->pct + 100 - tot);
    _after_change(false);
}

void SplitPanel::_lock(Segment *t, bool on)
{
    t->locked = on;
    bar->update();
}

void SplitPanel::_after_change(bool rebuild)
{
    bar->set_segments(targets);
    if (rebuild)
        _build_rows();
    else
        _sync_rows();
    _update_preview();
    Q_EMIT targetsChanged();
}

void SplitPanel::_build_rows()
{
    clear_grid(rows);
    row_widgets.clear();
    for (int i = 0; i < int(targets->size()); ++i) {
        const Segment &t = (*targets)[size_t(i)];
        const moddata::Province *pr = mod.provinces.get(t.pid);
        const QString pname = pr ? pr->name : QString();
        const QString name = (source && t.pid == *source) ? u"stays in %1"_s.arg(pname)
                                                          : u"%1 (#%2)"_s.arg(pname).arg(t.pid);
        auto *sw = new QLabel();
        sw->setFixedSize(10, 10);
        sw->setStyleSheet(swatch_style(t.color));
        auto *nm = new QLabel(name);
        nm->setToolTip(name);
        nm->setMinimumWidth(40);
        nm->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *spin = new QDoubleSpinBox();
        spin->setRange(0, 100);
        spin->setDecimals(1);
        spin->setSingleStep(1);
        spin->setSuffix(u" %"_s);
        spin->setFixedWidth(104);
        spin->setValue(t.pct);
        spin->setKeyboardTracking(false);
        // (Python's lambda holds the row's dict; the rows are made again whenever the list changes, so the place in
        // the list names the same entry)
        const widgets::Segs held = targets;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, held, i](double v) { _pct_edited(&(*held)[size_t(i)], v); });
        auto *lock = new QCheckBox(u"🔒"_s);
        lock->setToolTip(u"Lock this share"_s);
        lock->setChecked(t.locked);
        connect(lock, &QCheckBox::toggled, this, [this, held, i](bool on) { _lock(&(*held)[size_t(i)], on); });
        QLabel *amt = label(QString(), u"faint"_s);
        amt->setMinimumWidth(60);
        amt->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *rm = new QPushButton(u"✕"_s);
        rm->setObjectName(u"icon"_s);
        rm->setToolTip(u"Remove target"_s);
        if (source && t.pid == *source) {
            connect(rm, &QPushButton::clicked, this, [this] { keep_cb->setChecked(false); });
        } else {
            const long long pid = t.pid;
            connect(rm, &QPushButton::clicked, this, [this, pid] { _remove(pid); });
        }
        QWidget *ws[] = {sw, nm, spin, lock, amt, rm};
        for (int col = 0; col < 6; ++col)
            rows->addWidget(ws[col], i, col);
        rows->setColumnStretch(1, 1);
        row_widgets.emplace_back(spin, amt);
    }
}

void SplitPanel::_sync_rows()
{
    for (size_t i = 0; i < targets->size() && i < row_widgets.size(); ++i) {
        QDoubleSpinBox *spin = row_widgets[i].first;
        spin->blockSignals(true);
        spin->setValue((*targets)[i].pct);
        spin->blockSignals(false);
    }
}

void SplitPanel::_update_preview()
{
    if (!doc)
        return;
    const long long total = moving_total();
    const long long n = (long long)target_pids().size();
    moving->setText(u"Moving <b>%1</b> people (%2) to %3 province%4."_s.arg(fmt(total), filter_desc).arg(n).arg(
        n == 1 ? QString() : u"s"_s));
    moving->setTextFormat(Qt::RichText);
    std::vector<double> w;
    for (const Segment &t : *targets)
        w.push_back(t.pct);
    const std::vector<long long> shares = !targets->empty() ? document::distribute(total, w) : std::vector<long long>{};
    for (size_t i = 0; i < row_widgets.size() && i < shares.size(); ++i)
        row_widgets[i].second->setText(fmt(shares[i]));
    apply_btn->setEnabled(n > 0 && total > 0);
    QString reason;
    if (total <= 0) {
        const long long lim = ph_cb->isChecked() ? ph_spin->value() : 0;
        bool matching = false;
        for (const moddata::Pop &p : doc->province_pops(*source))
            if (flt->match(p)) {
                matching = true;
                break;
            }
        if (!matching)
            reason = u"No pops of this province match the filter (type / culture / religion)."_s;
        else if (lim)
            reason = u"No available pop groups are above the minimum set threshold of %1. "
                     u"Lower it or untick \"Leave pops of size ≤\"."_s.arg(fmt(lim));
    } else if (n == 0) {
        reason = u"Add at least one target province (click it on the map or type its ID)."_s;
    }
    warn->setText(reason);
    warn->setVisible(!reason.isEmpty());
}

}  // namespace panels
