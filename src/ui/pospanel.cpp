// pospanel.h
#include "ui/pospanel.h"
#include "core/history.h"
#include "core/positions.h"
#include "core/pynum.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace pospanel {

const Rgb PROB_COLOR{255, 70, 200};
const std::vector<Marker> MARKERS = {   // kind, label, colour, shape, size
    {u"unit"_s, u"Units"_s, {245, 245, 245}, u"square"_s, 5},
    {u"naval_base"_s, u"Naval bases"_s, {70, 150, 255}, u"dot"_s, 5.5},
    {u"fort"_s, u"Forts"_s, {200, 145, 80}, u"diamond"_s, 4.5},
    {u"railroad"_s, u"Railroads"_s, {170, 170, 170}, u"dot"_s, 4},
    {u"building_construction"_s, u"Building construction"_s, {235, 200, 110}, u"square"_s, 3.5},
    {u"military_construction"_s, u"Military construction"_s, {225, 110, 110}, u"square"_s, 3.5},
    {u"factory"_s, u"Factory"_s, {140, 205, 140}, u"square"_s, 3.5},
};

const Marker *marker(const QString &kind)
{
    for (const Marker &m : MARKERS)
        if (m.kind == kind)
            return &m;
    return nullptr;
}

static constexpr double PI = std::numbers::pi;
static double degrees(double x) { return x * (180.0 / PI); }   // (CPython: x * radToDeg)
static double radians(double x) { return x * (PI / 180.0); }
static QString kind_name(const QString &k) { return positions::KIND_NAMES.value(k, k); }
static QString plural(long long n) { return n == 1 ? QString() : u"s"_s; }

PositionsPanel::PositionsPanel(moddata::Mod &mod_, const mapview::MapData &md_, QWidget *parent)
    : QWidget(parent), PosChecks(mod_, md_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    title = widgets::label(u"Nothing selected"_s, u"title"_s);
    sub = widgets::label(QString(), u"sub"_s);
    sub->setTextFormat(Qt::RichText);
    s->lay->addWidget(title);
    s->lay->addWidget(sub);
    s->lay->addWidget(widgets::label(u"map/positions.txt — where the game draws each province's name, units and "
                                     u"buildings. Applies to every start date."_s,
                                     u"faint"_s));
    lay->addWidget(s);

    s = new widgets::Section();
    edit_btn = new QPushButton(u"✎  Move positions on the map"_s);
    edit_btn->setObjectName(u"primary"_s);
    edit_btn->setCheckable(true);
    connect(edit_btn, &QPushButton::toggled, this, [this](bool on) { _edit_mode(on); });
    s->lay->addWidget(edit_btn);
    edit_help = widgets::label(u"• Left-drag a marker (or the gold dot under a name) to move it\n"
                               u"• Right-click a naval base to remove it (the building goes with it)\n"
                               u"• Esc cancels · Ctrl+Z undoes"_s,
                               u"sub"_s);
    edit_help->hide();
    s->lay->addWidget(edit_help);
    s->lay->addWidget(widgets::label(u"Show on the map"_s));
    auto *grid = new QGridLayout();
    grid->setVerticalSpacing(2);
    show_.insert(u"names"_s, true);
    auto *cb = new QCheckBox(u"Names (as the game will draw them)"_s);
    cb->setChecked(true);
    connect(cb, &QCheckBox::toggled, this, [this](bool on) { _set_show(u"names"_s, on); });
    grid->addWidget(cb, 0, 0, 1, 2);
    show_boxes[u"names"_s] = cb;
    for (int i = 0; i < int(MARKERS.size()); ++i) {
        const Marker &mk = MARKERS[size_t(i)];
        cb = new QCheckBox(mk.label);
        cb->setIcon(widgets::swatch_icon(mk.color, 10));
        cb->setChecked(true);
        const QString key = mk.kind;
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) { _set_show(key, on); });
        show_.insert(mk.kind, true);
        grid->addWidget(cb, 1 + i / 2, i % 2);
        show_boxes[mk.kind] = cb;
    }
    s->lay->addLayout(grid);
    s->lay->addWidget(widgets::label(u"Markers of all provinces appear once you zoom in; those of the selection and "
                                     u"every problem are always shown (pink)."_s,
                                     u"faint"_s));
    lay->addWidget(s);

    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Check & generate"_s));
    prob_info = widgets::label(QString(), u"sub"_s);
    prob_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(prob_info);
    tilt = new QCheckBox(u"Tilt names along long, thin provinces when that fits them bigger"_s);
    tilt->setChecked(true);
    s->lay->addWidget(tilt);
    regen_all = new QPushButton(u"Regenerate all land provinces…"_s);
    regen_all->setObjectName(u"primary"_s);
    connect(regen_all, &QPushButton::clicked, this, [this] { _regenerate(std::nullopt); });
    s->lay->addWidget(regen_all);
    auto *row = new QHBoxLayout();
    regen_sel = new QPushButton(u"Regenerate selection"_s);
    connect(regen_sel, &QPushButton::clicked, this, [this] { _regenerate(sel); });
    fix_naval = new QPushButton(u"Fix naval bases"_s);
    connect(fix_naval, &QPushButton::clicked, this, [this] { _fix_naval(); });
    row->addWidget(regen_sel, 1);
    row->addWidget(fix_naval, 1);
    s->lay->addLayout(row);
    next_prob = new QPushButton(u"Go to next problem ▸"_s);
    connect(next_prob, &QPushButton::clicked, this, [this] { _next_problem(); });
    s->lay->addWidget(next_prob);
    lay->addWidget(s);

    s = new widgets::Section();
    sel_title = widgets::label(u"Selected province"_s);
    s->lay->addWidget(sel_title);
    sel_info = widgets::label(QString(), u"sub"_s);
    sel_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(sel_info);
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->addWidget(widgets::label(u"Name size"_s, u"sub"_s, false), 0, 0);
    scale_spin = new QDoubleSpinBox();
    scale_spin->setRange(0.3, 20.0);
    scale_spin->setSingleStep(0.1);
    scale_spin->setDecimals(2);
    // arrows change it at once (0.1 per click); typing counts when you press Enter or leave the box
    scale_spin->setKeyboardTracking(false);
    connect(scale_spin, &QDoubleSpinBox::valueChanged, this, [this](double) { _scale_edited(); });
    grid->addWidget(scale_spin, 0, 1);
    grid->addWidget(widgets::label(u"Name angle"_s, u"sub"_s, false), 1, 0);
    rot_spin = new QDoubleSpinBox();
    rot_spin->setRange(-180.0, 180.0);
    rot_spin->setSingleStep(1.0);
    rot_spin->setDecimals(1);
    rot_spin->setSuffix(u"°"_s);
    rot_spin->setWrapping(true);
    rot_spin->setKeyboardTracking(false);
    connect(rot_spin, &QDoubleSpinBox::valueChanged, this, [this](double) { _rot_edited(); });
    grid->addWidget(rot_spin, 1, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    auto *b = new QPushButton(u"Re-place name"_s);
    connect(b, &QPushButton::clicked, this, [this] { _regenerate(sel, QSet<QString>{u"text_position"_s}); });
    row->addWidget(b, 1);
    b = new QPushButton(u"Re-place unit && buildings"_s);
    connect(b, &QPushButton::clicked, this, [this] {
        QSet<QString> k(positions::PLACED.begin(), positions::PLACED.end());
        k.remove(u"text_position"_s);
        k.remove(u"naval_base"_s);
        _regenerate(sel, k);
    });
    row->addWidget(b, 1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    b = new QPushButton(u"Re-place naval base"_s);
    connect(b, &QPushButton::clicked, this, [this] { _regenerate(sel, QSet<QString>{u"naval_base"_s}); });
    row->addWidget(b, 1);
    b = new QPushButton(u"Remove naval base"_s);
    connect(b, &QPushButton::clicked, this, [this] { _remove_naval(); });
    row->addWidget(b, 1);
    s->lay->addLayout(row);
    sel_section = s;
    lay->addWidget(s);
    lay->addStretch(1);
}

// ------------------------------------------------------------------ helpers
positions::PositionsFile &PositionsPanel::pf() const
{
    return doc->map<positions::PositionsFile>(u"positions"_s);
}

QString PositionsPanel::pname(long long pid) const
{
    const moddata::Province *pr = mod.provinces.get(pid);
    return u"#%1 %2"_s.arg(pid).arg(pr ? pr->name : u"?"_s);
}

void PositionsPanel::_set_show(const QString &key, bool on)
{
    show_.insert(key, on);
    Q_EMIT overlayChanged();
}

void PositionsPanel::_edit_mode(bool on)
{
    edit_help->setVisible(on);
    handler.cancel();
    Q_EMIT editModeChanged(on);
    Q_EMIT overlayChanged();
}

void PositionsPanel::_edit(const QString &label_text, const std::function<void(positions::PositionsFile &)> &fn)
{
    doc->pos_edit(label_text, fn);
}

// ------------------------------------------------------------------ refresh / checks
void PositionsPanel::refresh(document::Document &d, const std::vector<long long> &s,
                             const std::optional<std::pair<QString, QString>> &header)
{
    sel.clear();
    for (long long p : s)
        if (const moddata::Province *pr = mod.provinces.get(p); pr && !pr->sea)
            sel.push_back(p);
    if (header) {
        title->setText(header->first);
        sub->setText(header->second);
    }
    compute_problems(d);
    _show_problems();
}

void PositionsPanel::_show_problems()
{
    OrderedMap<QString, long long> cnt;
    long long crash = 0;
    for (const auto &[pid, lst] : _probs) {
        bool has_crash = false;
        for (const auto &[k, txt] : lst) {
            const QString t = txt.split(u" ("_s)[0];
            cnt.insert(t, cnt.value(t, 0) + 1);
            has_crash = has_crash || txt == positions::CRASH;
        }
        crash += has_crash;
    }
    if (crash)
        cnt.remove(positions::CRASH);
    if (!cnt.isEmpty() || crash) {
        std::vector<std::pair<QString, long long>> items;
        for (const auto &[k, n] : cnt)
            items.emplace_back(k, n);
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        QStringList ls;
        for (const auto &[k, n] : items)
            ls << u"<span style='color:#ff5ac8'><b>%1</b></span> × %2"_s.arg(n).arg(k);
        const QString lines =
            (crash ? u"<span style='color:#ff4040'><b>⚠ %1 × %2</b></span><br>"_s.arg(crash).arg(positions::CRASH)
                   : QString()) +
            ls.join(u"<br>"_s);
        prob_info->setText(u"%1 of %2 land provinces have problems:<br>%3"_s.arg(_probs.size()).arg(land.size()).arg(lines));
    } else {
        prob_info->setText(u"<span style='color:%1'>✔</span> All %2 land provinces look fine."_s.arg(theme::OK).arg(
            land.size()));
    }
    const auto [add, mv, rem] = _naval_work();
    const bool any = add || mv || rem;
    fix_naval->setText(any ? u"Fix naval bases (+%1 · move %2 · −%3)"_s.arg(add).arg(mv).arg(rem)
                           : u"Naval bases: all fine"_s);
    fix_naval->setEnabled(any);
    next_prob->setEnabled(!_probs.isEmpty());
    regen_sel->setEnabled(!sel.empty());
    _fill_selected();
    Q_EMIT overlayChanged();
}

static QString date_str(const history::Date &d)
{
    return u"%1.%2.%3"_s.arg(std::get<0>(d)).arg(std::get<1>(d)).arg(std::get<2>(d));
}

void PositionsPanel::remove_naval(const std::vector<long long> &pids)
{
    // Remove naval base positions — and the naval bases themselves (a base can't exist without one). If another start
    // date still builds a base there, that position has to stay.
    std::vector<long long> now;
    for (long long p : pids)
        if (doc->prov_state(p).naval_base > 0)
            now.push_back(p);
    if (!now.empty() && !doc->history_editable()) {
        Q_EMIT message(u"This scenario isn't a start date, so its naval bases can't be removed here."_s);
        return;
    }
    std::vector<std::pair<long long, history::Date>> keep;
    for (long long p : pids)
        for (const auto &d : _other_dates())
            if (doc->hist->state_at(p, d).naval_base > 0) {
                keep.emplace_back(p, d);
                break;
            }
    QSet<long long> keep_ids;
    for (const auto &[p, _d] : keep)
        keep_ids.insert(p);
    document::Document *dc = doc;
    auto fn = [&pids, &keep_ids, &now, dc](positions::PositionsFile &pf_) {
        for (long long p : pids)
            if (!keep_ids.contains(p))
                pf_.set_point(p, u"naval_base"_s, std::nullopt);
        for (long long p : now) {
            history::ProvState st = history::clone_state(dc->prov_state(p));
            st.naval_base = 0;
            dc->prov_want.insert(p, st);
        }
    };
    const QString what = u"naval base"_s + (pids.size() == 1 ? QString() : u"s"_s);
    QStringList ids, names;
    for (size_t i = 0; i < std::min<size_t>(6, pids.size()); ++i)
        ids << u"#%1"_s.arg(pids[i]);
    for (size_t i = 0; i < std::min<size_t>(4, pids.size()); ++i)
        names << pname(pids[i]);
    doc->pos_hist_edit(u"Remove %1 (%2)"_s.arg(what, ids.join(u", "_s)), QList<long long>(now.begin(), now.end()), fn);
    QString msg = u"Removed the %1 of %2%3."_s.arg(
        what, names.join(u", "_s),
        !now.empty() ? u" (position and building in the %1 history)"_s.arg(doc->scenario) : QString());
    if (!keep.empty()) {
        QStringList kn;
        for (size_t i = 0; i < std::min<size_t>(3, keep.size()); ++i)
            kn << pname(keep[i].first);
        msg += u" Kept the position of %1: %2 still builds a naval base there."_s.arg(kn.join(u", "_s),
                                                                                      date_str(keep[0].second));
    }
    Q_EMIT message(msg + u" Ctrl+Z undoes it."_s);
}

std::tuple<long long, long long, long long> PositionsPanel::_naval_work() const
{
    long long add = 0, mv = 0, rem = 0;
    for (const auto &[pid, lst] : _probs)
        for (const auto &[k, txt] : lst) {
            if (k != u"naval_base")
                continue;
            if (txt.startsWith(u"coastal province without"_s) || txt == positions::CRASH)
                ++add;
            else if (txt.startsWith(u"naval base, but no valid coast"_s))
                ++rem;
            else
                ++mv;
        }
    return {add, mv, rem};
}

void PositionsPanel::_fill_selected()
{
    positions::PositionsFile &f = pf();
    sel_section->setEnabled(!sel.empty());
    if (sel.empty()) {
        sel_title->setText(u"Selected province"_s);
        sel_info->setText(u"Select land provinces to see and change their positions."_s);
        return;
    }
    if (sel.size() != 1) {
        sel_title->setText(u"%1 provinces selected"_s.arg(sel.size()));
        long long n = 0;
        for (long long p : sel)
            n += _probs.contains(p);
        sel_info->setText(u"%1 of them with problems. The buttons below re-place their positions."_s.arg(n));
        scale_spin->setEnabled(false);
        rot_spin->setEnabled(false);
        return;
    }
    const long long one = sel[0];
    const positions::Entry e = f.get(one);
    sel_title->setText(u"Positions of %1"_s.arg(pname(one)));
    QStringList rows;
    for (const QString &k : positions::PLACED) {
        if (const positions::XY *q = e.pts.get(k))
            rows << u"%1: %2, %3"_s.arg(positions::KIND_NAMES.value(k), py::fmt_f(q->first, 1), py::fmt_f(q->second, 1));
        else
            rows << u"<span style='color:%1'>%2: —</span>"_s.arg(theme::INK_FAINT, positions::KIND_NAMES.value(k));
    }
    QStringList probs;
    if (const Problems *pl = _probs.get(one))
        for (const auto &[k, t] : *pl)
            probs << u"<span style='color:#ff5ac8'>⚠ %1</span>"_s.arg(t);
    sel_info->setText((probs + rows).join(u"<br>"_s));
    scale_spin->setEnabled(true);
    rot_spin->setEnabled(true);
    scale_spin->blockSignals(true);
    rot_spin->blockSignals(true);
    scale_spin->setValue(e.text_scale.value_or(1.0));
    const double r = degrees(e.text_rotation.value_or(0.0));
    rot_spin->setValue(py::mod(r + 180, 360.0) - 180);
    scale_spin->blockSignals(false);
    rot_spin->blockSignals(false);
}

// ------------------------------------------------------------------ edits
void PositionsPanel::_scale_edited()
{
    if (sel.size() != 1)
        return;
    const long long pid = sel[0];
    const double v = py::round_to(scale_spin->value(), 3);
    if (pf().get(pid).text_scale.value_or(0) == v)
        return;
    _edit(u"Name size of #%1"_s.arg(pid), [pid, v](positions::PositionsFile &f) {
        positions::Entry e = f.get(pid);
        e.text_scale = v;
        f.set_entry(pid, e);
    });
}

void PositionsPanel::_rot_edited()
{
    if (sel.size() != 1)
        return;
    const long long pid = sel[0];
    const double v = py::round_to(py::mod(radians(rot_spin->value()), 2 * PI), 6);
    if (std::abs(pf().get(pid).text_rotation.value_or(0.0) - v) < 1e-6)
        return;
    _edit(u"Name angle of #%1"_s.arg(pid), [pid, v](positions::PositionsFile &f) {
        positions::Entry e = f.get(pid);
        e.text_rotation = v;
        f.set_entry(pid, e);
    });
}

void PositionsPanel::_remove_naval()
{
    std::vector<long long> pids;
    for (long long p : sel)
        if (pf().get(p).pts.contains(u"naval_base"_s) || doc->prov_state(p).naval_base > 0)
            pids.push_back(p);
    if (!pids.empty())
        remove_naval(pids);
}

positions::Entry PositionsPanel::place_entry(long long pid, positions::Entry e, const QSet<QString> &kinds,
                                             const std::vector<std::uint8_t> &)
{
    // New entry for pid with the given kinds re-placed.
    positions::PositionPlacer &pl = placer;
    positions::PositionsFile &f = pf();
    const QString name = mod.provinces.get(pid)->name;
    std::optional<positions::NameBox> box;
    if (kinds.contains(u"text_position"_s)) {
        if (const auto r = pl.fit_name(pid, name, tilt->isChecked())) {
            const positions::XY c = r->centre;
            const double th = r->angle;
            const double sc = py::round_to(std::min(positions::SCALE_MAX, std::max(0.6, r->scale * 0.9)), 2);
            e.pts.insert(u"text_position"_s, f.to_game(positions::name_anchor(c, th, sc)));
            e.text_rotation = py::round_to(py::mod(-th, 2 * PI), 6);
            e.text_scale = sc;
            box = pl.name_box(c, th, sc, name);
        }
    } else if (const positions::XY *tp = e.pts.get(u"text_position"_s)) {
        const double sc = e.text_scale.value_or(1.0);
        const double th = -e.text_rotation.value_or(0.0);
        box = pl.name_box(positions::name_center(f.to_disp(*tp), th, sc), th, sc, name);
    }
    QSet<QString> land_kinds = kinds;
    land_kinds.remove(u"text_position"_s);
    land_kinds.remove(u"naval_base"_s);
    if (!land_kinds.isEmpty()) {
        const OrderedMap<QString, positions::XY> pts = pl.place_points(pid, box);
        for (const QString &k : positions::PLACED)   // (a set walked: each kind is its own key, the order is moot)
            if (land_kinds.contains(k))
                if (const positions::XY *q = pts.get(k))
                    e.pts.insert(k, f.to_game(*q));
    }
    if (kinds.contains(u"naval_base"_s)) {
        std::optional<positions::XY> near;
        if (const positions::XY *u = e.pts.get(u"unit"_s))
            near = f.to_disp(*u);
        std::optional<positions::Naval> ns = _coast.contains(pid) ? pl.naval_spot(pid, near) : std::nullopt;
        if (!ns && _hist_naval.contains(pid)) {
            // the history builds a naval base here: it must keep a position or the game crashes
            if (e.pts.contains(u"naval_base"_s))
                return e;
            ns = pl.fallback_naval(pid, near);
        }
        if (ns) {
            e.pts.insert(u"naval_base"_s, f.to_game(ns->at));
            e.naval_rot = py::round_to(ns->rot, 6);
        } else {
            e.pts.remove(u"naval_base"_s);
            e.naval_rot.reset();
        }
    }
    return e;
}

void PositionsPanel::_regenerate(const std::optional<std::vector<long long>> &pids_,
                                 const std::optional<QSet<QString>> &kinds_)
{
    std::vector<long long> pids;
    if (!pids_) {
        pids = land;
    } else {
        const QSet<long long> ls(land.begin(), land.end());
        for (long long p : *pids_)
            if (ls.contains(p))
                pids.push_back(p);
    }
    if (pids.empty())
        return;
    const QSet<QString> kinds = kinds_ && !kinds_->isEmpty()
                                    ? *kinds_
                                    : QSet<QString>(positions::PLACED.begin(), positions::PLACED.end());
    const std::vector<std::uint8_t> valid = placer.sea_valid;
    OrderedMap<long long, positions::Entry> nw;
    positions::PositionsFile &f = pf();
    for (size_t i = 0; i < pids.size(); ++i) {
        if (pids.size() > 150 && i % 40 == 0) {    // a long run: progress in the status bar (no pop-up window)
            Q_EMIT message(u"Placing positions… %1 of %2"_s.arg(i).arg(pids.size()));
            QApplication::processEvents();
        }
        nw.insert(pids[i], place_entry(pids[i], f.get(pids[i]), kinds, valid));
    }
    const long long n = nw.size();
    _edit(u"Regenerate positions (%1 province%2)"_s.arg(n).arg(plural(n)), [&nw](positions::PositionsFile &f_) {
        for (const auto &[pid, e] : nw)
            f_.set_entry(pid, e);
    });
    Q_EMIT message(u"Placed positions for %1 province%2. Written on Apply; Ctrl+Z undoes it."_s.arg(n).arg(plural(n)));
}

void PositionsPanel::_fix_naval()
{
    std::vector<long long> todo;
    for (const auto &[pid, lst] : _probs) {
        bool nb = false;
        for (const auto &[k, t] : lst)
            nb = nb || k == u"naval_base";
        // never take a naval base position away from a province whose history builds one
        if (nb && !(_hist_naval.contains(pid) && pf().get(pid).pts.contains(u"naval_base"_s) && !_coast.contains(pid)))
            todo.push_back(pid);
    }
    if (todo.empty())
        return;
    const std::vector<std::uint8_t> valid = placer.sea_valid;
    OrderedMap<long long, positions::Entry> nw;
    for (long long pid : todo)
        nw.insert(pid, place_entry(pid, pf().get(pid), QSet<QString>{u"naval_base"_s}, valid));
    _edit(u"Fix naval bases (%1)"_s.arg(nw.size()), [&nw](positions::PositionsFile &f) {
        for (const auto &[pid, e] : nw)
            f.set_entry(pid, e);
    });
}

void PositionsPanel::_next_problem()
{
    std::vector<long long> pids;
    for (const auto &[p, _l] : _probs)
        pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    if (pids.empty())
        return;
    _prob_i = py::mod(_prob_i + 1, (long long)pids.size());
    const long long pid = pids[size_t(_prob_i)];
    Q_EMIT selectRequested({pid}, {});
    Q_EMIT zoomRequested(md.cx[size_t(pid)] + 0.5, md.cy[size_t(pid)] + 0.5);
    QStringList ts;
    for (const auto &[k, t] : *_probs.get(pid))
        ts << t;
    Q_EMIT message(u"%1: %2"_s.arg(pname(pid), ts.join(u"; "_s)));
}

// ------------------------------------------------------------------ overlay
std::vector<mapview::Shape> PositionsPanel::shapes()
{
    if (!doc)
        return {};
    positions::PositionsFile &f = pf();
    const QSet<long long> sset(sel.begin(), sel.end());
    const bool edit = edit_btn->isChecked();
    std::vector<bool> show;
    for (const auto &[k, on] : show_)
        show.push_back(on);
    std::vector<mapview::Shape> out;
    std::vector<mapview::NameItem> names;
    static const Problems none;
    for (long long pid : land) {
        const positions::EntryP *ep = f.entries.get(pid);
        const positions::EntryP e = ep ? *ep : nullptr;
        const Problems *pp = _probs.get(pid);
        const Problems &probs = pp ? *pp : none;
        const bool focus = sset.contains(pid);
        auto it = _shape_cache.find(pid);
        // rebuild a province's markers only when its entry, problems, selection or the view changed
        if (it == _shape_cache.end() || it->second.e != e || it->second.probs != probs || it->second.focus != focus ||
            it->second.edit != edit || it->second.show != show) {
            auto [sh, nm] = _province_shapes(pid, e, probs, focus, edit);
            it = _shape_cache.insert_or_assign(pid, Cached{e, probs, focus, edit, show, std::move(sh), std::move(nm)})
                     .first;
        }
        out.insert(out.end(), it->second.shapes.begin(), it->second.shapes.end());
        if (it->second.name)
            names.push_back(*it->second.name);
    }
    _names = std::move(names);
    return out;
}

std::pair<std::vector<mapview::Shape>, std::optional<mapview::NameItem>>
PositionsPanel::_province_shapes(long long pid, const positions::EntryP &e, const Problems &probs, bool focus, bool edit)
{
    // -> (shapes, name item or None) of one province.
    positions::PositionsFile &f = pf();
    std::vector<mapview::Shape> out;
    std::optional<mapview::NameItem> name_item;
    QSet<QString> bad;
    for (const auto &[k, _t] : probs)
        bad.insert(k);
    if (e) {
        for (const Marker &mk : MARKERS) {
            const positions::XY *q = e->pts.get(mk.kind);
            if (!q || !show_.value(mk.kind, true))
                continue;
            const auto [x, y] = f.to_disp(*q);
            mapview::Shape s;
            s.kind = mk.shape;
            s.pts = {QPointF(x, y)};
            s.color = bad.contains(mk.kind) ? PROB_COLOR : mk.color;
            s.size = mk.size + (bad.contains(mk.kind) ? 1.5 : 0);
            s.tag = QVariantList{u"pos"_s, pid, mk.kind};
            s.halo = focus;
            s.min_scale = (focus || bad.contains(mk.kind)) ? 0.0 : ALL_MARKERS_FROM;
            out.push_back(s);
        }
        const positions::XY *tp = e->pts.get(u"text_position"_s);
        if (tp && show_.value(u"names"_s, true)) {
            const positions::XY c = f.to_disp(*tp);
            const double sc = e->text_scale.value_or(1.0);
            const QString name = mod.provinces.get(pid)->name;
            const double ang = -e->text_rotation.value_or(0.0);
            const positions::XY mid = positions::name_center(c, ang, sc);
            name_item = mapview::NameItem{mid.first, mid.second, ang,
                                          positions::CHAR_W * double(std::max<qsizetype>(1, name.size())) * sc,
                                          positions::TEXT_H * sc, name,
                                          bad.contains(u"text_position"_s) ? PROB_COLOR : Rgb{236, 230, 214}};
            if (edit || focus) {
                mapview::Shape s;
                s.kind = u"dot"_s;
                s.pts = {QPointF(c.first, c.second)};
                s.color = {236, 200, 120};
                s.size = 4;
                s.tag = QVariantList{u"pos"_s, pid, u"text_position"_s};
                s.halo = focus;
                s.min_scale = focus ? 0.0 : ALL_MARKERS_FROM;
                out.push_back(s);
            }
        }
    }
    bool marked = false;
    if (e)
        for (const QString &k : bad)
            marked = marked || e->pts.contains(k);
    if (!probs.empty() && !marked) {
        // the problem is something missing: mark the province itself
        mapview::Shape s;
        s.kind = u"x"_s;
        s.pts = {QPointF(md.cx[size_t(pid)] + 0.5, md.cy[size_t(pid)] + 0.5)};
        s.color = PROB_COLOR;
        s.size = 4;
        s.width = 2.5;
        s.tag = QVariantList{u"posprob"_s, pid};
        out.push_back(s);
    }
    return {out, name_item};
}

QString PositionsPanel::describe(const QVariantList &tag) const
{
    if (tag.isEmpty())
        return QString();
    if (tag[0].toString() == u"pos") {
        const long long pid = tag[1].toLongLong();
        const QString kind = tag[2].toString();
        QStringList probs;
        if (const Problems *pl = _probs.get(pid))
            for (const auto &[k, t] : *pl)
                if (k == kind)
                    probs << t;
        return u"%1 of %2%3%4"_s.arg(kind_name(kind), pname(pid),
                                     !probs.isEmpty() ? u"  ⚠ "_s + probs.join(u"; "_s) : QString(),
                                     edit_btn->isChecked() ? u"  · drag to move"_s : QString());
    }
    if (tag[0].toString() == u"posprob") {
        const long long pid = tag[1].toLongLong();
        QStringList ts;
        if (const Problems *pl = _probs.get(pid))
            for (const auto &[k, t] : *pl)
                ts << t;
        return u"%1: %2"_s.arg(pname(pid), ts.join(u"; "_s));
    }
    return QString();
}

void PositionsPanel::map_clicked(const QVariantList &tag)
{
    Q_EMIT selectRequested({tag[1].toLongLong()}, {});
}

// ---------------------------------------------------------------------------------------------- PosEditHandler
mapview::ShapeLayer *PosEditHandler::layer() const
{
    return p->view->shape_layer;
}

bool PosEditHandler::cancel()
{
    const bool had = drag.has_value();
    drag.reset();
    if (p->view)
        layer()->set_temp({});
    return had;
}

bool PosEditHandler::press(double x, double y, double tol, Qt::KeyboardModifiers)
{
    const auto tag = layer()->hit(x, y, tol * 1.2, QStringList{u"pos"_s});
    if (!tag)
        return false;
    drag = Drag{(*tag)[1].toLongLong(), (*tag)[2].toString(), x, y};
    return true;
}

void PosEditHandler::move(double x, double y)
{
    if (!drag)
        return;
    const auto [pid, kind, sx, sy] = *drag;
    const Marker *mk = marker(kind);
    mapview::Shape line;
    line.kind = u"line"_s;
    line.pts = {QPointF(sx, sy), QPointF(x, y)};
    line.color = {255, 255, 255};
    line.width = 2;
    line.dash = true;
    mapview::Shape dot;
    dot.kind = u"dot"_s;
    dot.pts = {QPointF(x, y)};
    dot.color = mk ? mk->color : Rgb{236, 200, 120};
    dot.size = 6;
    layer()->set_temp({line, dot});
    const bool inside = p->md.province_at((long long)x, (long long)y) == pid;
    if (kind == u"naval_base") {
        const bool ok = p->placer.naval_ok(pid, {x, y});
        Q_EMIT p->message(u"Naval base: %1"_s.arg(ok ? u"at the coast ✔"_s : u"not at a valid coast"_s));
    } else {
        Q_EMIT p->message(u"%1: %2"_s.arg(kind_name(kind), inside ? u"inside ✔"_s : u"outside the province"_s));
    }
}

void PosEditHandler::release(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (!drag)
        return;
    const auto [pid, kind, sx, sy] = *drag;
    cancel();
    if (std::abs(x - sx) + std::abs(y - sy) < std::max(0.5, tol * 0.3)) {
        Q_EMIT p->selectRequested({pid}, {});
        return;
    }
    positions::PositionPlacer &pl = p->placer;
    const long long pid_ = pid;
    const QString kind_ = kind;
    p->_edit(u"Move %1 of #%2"_s.arg(kind_name(kind).toLower()).arg(pid), [&pl, pid_, kind_, x, y](positions::PositionsFile &f) {
        positions::Entry e = f.get(pid_);
        e.pts.insert(kind_, f.to_game({x, y}));
        if (kind_ == u"naval_base") {
            // turn the port to face the water again
            const auto cr = pl.crop(pid_, 5);
            std::optional<std::pair<double, std::pair<long long, long long>>> best;   // (d, (ys, xs)): the first minimum
            for (long long yy = 0; yy < cr.sub.h(); ++yy)
                for (long long xx = 0; xx < cr.sub.w(); ++xx)
                    if (cr.sub(yy, xx) == pid_) {
                        const double dx = double(xx) + 0.5 + double(cr.x0) - x, dy = double(yy) + 0.5 + double(cr.y0) - y;
                        const double d = dx * dx + dy * dy;
                        if (!best || d < best->first)
                            best = std::pair{d, std::pair{yy, xx}};
                    }
            if (best) {
                const auto [ys, xs] = best->second;
                const double ang = std::atan2(y - (double(ys) + 0.5 + double(cr.y0)), x - (double(xs) + 0.5 + double(cr.x0)));
                e.naval_rot = py::round_to(py::mod(ang + PI, 2 * PI), 6);
            }
        }
        f.set_entry(pid_, e);
    });
}

bool PosEditHandler::rclick(double x, double y, double tol, Qt::KeyboardModifiers)
{
    const auto tag = layer()->hit(x, y, tol, QStringList{u"pos"_s});
    if (!tag || (*tag)[2].toString() != u"naval_base")
        return false;
    p->remove_naval({(*tag)[1].toLongLong()});
    return true;
}

}  // namespace pospanel
