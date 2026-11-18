// diplopanel.py — Diplomacy tab (see diplopanel.h).
#include "ui/diplopanel.h"
#include "core/countryhist.h"
#include "core/entryview_data.h"
#include "map/moreviews_data.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "grid/pyset.h"
#include "ui/dialogs.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"

#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace diplopanel {

const std::vector<Status> STATUS = {{u"puppet"_s, {242, 212, 80}, u"Its puppet"_s},
                                    {u"substate"_s, {150, 226, 70}, u"Its substate"_s},
                                    {u"sphere"_s, {64, 204, 184}, u"In its sphere"_s},
                                    {u"ally"_s, {84, 146, 236}, u"Ally"_s},
                                    {u"overlord"_s, {176, 104, 218}, u"Its overlord (a puppet of it)"_s},
                                    {u"suzerain"_s, {236, 112, 112}, u"Its suzerain (a substate of it)"_s},
                                    {u"lord"_s, {222, 88, 168}, u"Has it in its sphere"_s}};
const OrderedMap<QString, Rgb> STATUS_RGB = [] {
    OrderedMap<QString, Rgb> m;
    for (const Status &s : STATUS)
        m.insert(s.key, s.rgb);
    return m;
}();
const Rgb COL_OTHER{70, 72, 78};
const std::vector<std::pair<OptStr, QString>> SUBJECT_CHOICES = {
    {std::nullopt, u"Neither is a subject of the other"_s}, {u"b_vassal"_s, u"{B} is a puppet of {A}"_s},
    {u"b_substate"_s, u"{B} is a substate of {A}"_s}, {u"a_vassal"_s, u"{A} is a puppet of {B}"_s},
    {u"a_substate"_s, u"{A} is a substate of {B}"_s}};

const std::vector<std::pair<QString, QString>> DiploPanel::VIEWS = {{u"relations"_s, u"Relations"_s},
                                                                    {u"status"_s, u"Status"_s}};
const OrderedMap<QString, QString> DiploPanel::MODE = {{u"relations"_s, u"relvalues"_s}, {u"status"_s, u"status"_s}};

namespace {

QString rgb_str(const Rgb &c)                      // str((r, g, b))
{
    return u"(%1, %2, %3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

QString plus(long long v)                          // "%+d" % v
{
    return v >= 0 ? u"+%1"_s.arg(v) : QString::number(v);
}

QString kind_name(const QString &k)                // KIND_NAME.get(k, k)
{
    return diplomacy::KIND_NAME.value(k, k);
}

}  // namespace

Rgb relation_rgb(double v)
{
    v = std::max(-200.0, std::min(200.0, v));
    const Rgb lo{214, 58, 48}, mid{120, 122, 128}, hi{64, 190, 88};
    const Rgb &a = mid;
    const Rgb &b = v >= 0 ? hi : lo;
    const double t = v >= 0 ? v / 200.0 : -v / 200.0;
    auto f = [t](int x, int y) { return int(double(x) + double(y - x) * t); };
    return {f(std::get<0>(a), std::get<0>(b)), f(std::get<1>(a), std::get<1>(b)), f(std::get<2>(a), std::get<2>(b))};
}

QStringList py_tags_with_units(const oob::OOBEdits &oob)
{
    // tags = set(self.want); then every nation that has an OOB is added
    std::vector<QString> keys;
    for (const auto &[t, _s] : oob.want)
        keys.push_back(t);
    grid::PySet<QString> tags = grid::PySet<QString>::from_keys(keys);
    for (const auto &[t, _c] : oob.mod.countries)
        if (oob.exists(t))
            tags.add(t);
    const std::vector<QString> items = tags.items();
    return QStringList(items.begin(), items.end());
}

OrderedMap<QString, QStringList> statuses(const oob::OOBEdits &oob, const diplomacy::DiploEdits &dip, const QString &a)
{
    // {tag: [status keys]} of every nation towards the main nation a
    OrderedMap<QString, QStringList> out;
    auto add = [&](const QString &t, const QString &k) {
        if (!t.isEmpty() && t != a && !out[t].contains(k))
            out[t].append(k);
    };
    for (const diplomacy::Record &r : dip.of(a)) {
        if (r.kind == u"vassal" || r.kind == u"substate") {
            if (r.first == a)
                add(r.second, r.kind == u"vassal" ? u"puppet"_s : u"substate"_s);
            else
                add(r.first, r.kind == u"vassal" ? u"overlord"_s : u"suzerain"_s);
        } else if (r.kind == u"alliance") {
            add(r.first == a ? r.second : r.first, u"ally"_s);
        }
    }
    for (const auto &[t, r] : oob.state(a).relations)
        if (py::strip(r.level.value_or(QString())) == u"5")
            add(t, u"sphere"_s);
    for (const QString &t : py_tags_with_units(oob))
        if (diplomacy::level_of(oob, t, a) == 5)
            add(t, u"lord"_s);
    return out;
}

DiploPanel::DiploPanel(moddata::Mod &mod, mapview::MapData &mapdata, QWidget *parent)
    : QWidget(parent), mod(mod), mapdata(mapdata)
{
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(450);
    connect(_timer, &QTimer::timeout, this, [this] { _flush(); });
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- what stands beside the panel: the map, or the relations as a web / boxes (the user, 2026-10-26)
    auto *shw = new QWidget();
    shw->setObjectName(u"side"_s);
    auto *hl = new QHBoxLayout(shw);
    hl->setContentsMargins(16, 10, 16, 0);
    hl->setSpacing(0);
    for (const auto &[key, text, tip] :
         {std::tuple{u"map"_s, u"🗺 Map"_s, u"Edit on the map: click nations"_s},
          std::tuple{u"web"_s, u"◎ Relations"_s,
                     u"Edit on the relations: the nation's allies, wars, subjects, sphere … as a web — drag a flag onto "
                     u"a group to tie it so; the great powers; groups as overlapping boxes"_s}}) {
        auto *bt = new QPushButton(text);
        bt->setObjectName(u"seg"_s);
        bt->setCheckable(true);
        bt->setChecked(key == u"map");
        bt->setToolTip(tip);
        connect(bt, &QPushButton::clicked, this, [this, k = key] { set_shown(k); });
        hl->addWidget(bt, 1);
        shown_btn.emplace_back(key, bt);
    }
    lay->addWidget(shw);
    // --- Relations | Status
    auto *segw = new QWidget();
    segw->setObjectName(u"side"_s);
    auto *sl = new QHBoxLayout(segw);
    sl->setContentsMargins(16, 10, 16, 4);
    sl->setSpacing(0);
    auto *grp = new QButtonGroup(this);
    for (const auto &[key, text] : VIEWS) {
        auto *bt = new QPushButton(text);
        bt->setObjectName(u"seg"_s);
        bt->setCheckable(true);
        bt->setChecked(key == view);
        connect(bt, &QPushButton::clicked, this, [this, k = key] { set_view(k); });
        grp->addButton(bt);
        sl->addWidget(bt, 1);
        seg.emplace_back(key, bt);
    }
    lay->addWidget(segw);

    // --- main nation
    auto *s = new Section();
    picker = new dialogs::PickCombo(nationpanel::nation_items(mod));
    picker->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(picker, &QComboBox::activated, this, [this](int) {
        if (const OptStr k = picker->key(); k && !k->isEmpty())
            Q_EMIT nationPicked(*k);
    });
    s->lay->addWidget(picker);
    auto *row = new QHBoxLayout();
    title = label(u"Click a nation on the map, or pick one above."_s, u"title"_s);
    row->addWidget(title, 1);
    clear_btn = new QPushButton(u"✕"_s);
    clear_btn->setFixedWidth(34);
    clear_btn->setToolTip(u"No main nation — the next click on the map picks one (Esc does it too)"_s);
    connect(clear_btn, &QPushButton::clicked, this, [this] { Q_EMIT nationPicked(u""_s); });
    row->addWidget(clear_btn);
    s->lay->addLayout(row);
    sub = label(u""_s, u"sub"_s);
    sub->setTextFormat(Qt::RichText);
    s->lay->addWidget(sub);
    hint = label(u"Click nations on the map to set their relations with it. Right-click a nation to make it the main "
                 u"one; ✕ or Esc clears it."_s,
                 u"faint"_s);
    s->lay->addWidget(hint);
    gp_label = label(u""_s, u"faint"_s);
    gp_label->setTextFormat(Qt::RichText);
    s->lay->addWidget(gp_label);
    lay->addWidget(s);

    // --- A and B
    s = pair_section = new Section();
    row = new QHBoxLayout();
    pair_title = label(u""_s, u"title"_s);
    pair_title->setTextFormat(Qt::RichText);
    row->addWidget(pair_title, 1);
    swap_btn = new QPushButton(u"⇄"_s);
    swap_btn->setToolTip(u"Make this nation the main one"_s);
    swap_btn->setFixedWidth(34);
    connect(swap_btn, &QPushButton::clicked, this, [this] {
        if (b && !b->isEmpty())
            Q_EMIT nationPicked(*b);
    });
    row->addWidget(swap_btn);
    s->lay->addLayout(row);
    // the Relations view: the relation between the two
    rel_box = new QWidget();
    auto *rl = new QVBoxLayout(rel_box);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(label(u"Relations"_s, u"sub"_s));
    rel = new widgets::SliderField(-200, 200, 200, 0);
    rel->slider->setMinimumWidth(60);
    rel->set_snaps({0}, 0.035);                    // a snap point at 0 (±14)
    connect(rel, &widgets::SliderField::valueChanged, this, [this](double v) { _later(u"rel"_s, v); });
    rl->addWidget(rel);
    rel_note = label(u""_s, u"faint"_s);
    rl->addWidget(rel_note);
    s->lay->addWidget(rel_box);
    // the Status view: standing / influence (great powers), treaties
    st_box = new QWidget();
    auto *stl = new QVBoxLayout(st_box);
    stl->setContentsMargins(0, 0, 0, 0);
    lvl_title = label(u""_s, u"sub"_s);
    stl->addWidget(lvl_title);
    level = new widgets::StepSlider(oob::LEVELS, 2);
    connect(level, &widgets::StepSlider::valueChanged, this, [this](int v) { _level(v); });
    stl->addWidget(level);
    infl_row = new QWidget();
    auto *h = new QHBoxLayout(infl_row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(label(u"Influence points"_s, u"faint"_s, false));
    infl = new widgets::SliderField(0, 100, 100, 0);
    infl->slider->setMinimumWidth(60);
    connect(infl, &widgets::SliderField::valueChanged, this, [this](double v) { _later(u"infl"_s, v); });
    h->addWidget(infl, 1);
    stl->addWidget(infl_row);
    lvl_note = label(u""_s, u"faint"_s);
    stl->addWidget(lvl_note);
    stl->addWidget(label(u"Treaties at the start"_s, u"sub"_s));
    subject = new QComboBox();
    subject->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(subject, &QComboBox::activated, this, [this](int) { _subject(); });
    stl->addWidget(subject);
    alliance = new QCheckBox(u"Alliance"_s);
    connect(alliance, &QCheckBox::clicked, this, [this](bool on) { _treaty(u"alliance"_s, on); });
    stl->addWidget(alliance);
    other_treaties = label(u""_s, u"faint"_s);
    stl->addWidget(other_treaties);
    treaty_note = label(u""_s, u"faint"_s);
    stl->addWidget(treaty_note);
    s->lay->addWidget(st_box);
    lay->addWidget(s);

    // --- everything of A
    s = list_section = new Section();
    list_title = label(u""_s, u"label"_s);
    s->lay->addWidget(list_title);
    rel_list = new QListWidget();
    rel_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rel_list->setTextElideMode(Qt::ElideRight);
    connect(rel_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *it) { Q_EMIT otherPicked(it->data(Qt::UserRole).toString()); });
    s->lay->addWidget(rel_list);
    lay->addWidget(s);

    // --- the wars running at the start (history/wars) — edited in Warfare, listed here as well
    s = war_section = new Section();
    s->lay->addWidget(label(u"Wars at the start"_s));
    war_note = label(u""_s, u"faint"_s);
    s->lay->addWidget(war_note);
    war_list = new QListWidget();
    war_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    war_list->setWordWrap(true);
    war_list->setToolTip(u"Double-click: the war in Warfare → Wars at the start"_s);
    connect(war_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *it) { Q_EMIT warRequested(it->data(Qt::UserRole).toString()); });
    s->lay->addWidget(war_list);
    auto *wb = new QPushButton(u"Edit the wars ▸"_s);
    wb->setToolTip(u"Warfare → Wars at the start: who attacks, who defends, the war goals; new wars"_s);
    connect(wb, &QPushButton::clicked, this, [this] {
        Q_EMIT warRequested(war_list->currentItem() ? war_list->currentItem()->data(Qt::UserRole).toString() : u""_s);
    });
    s->lay->addWidget(wb, 0, Qt::AlignLeft);
    lay->addWidget(s);

    // --- legends: the Status view's fill, the Relations view's fill
    s = legend_status = new Section();
    s->lay->addWidget(label(u"Map — diplomatic status with the main nation"_s));
    auto *host = new QWidget();
    auto *gf = new widgets::GridFlow(host, 4, 190, 22);
    legend_a = _chip(gf, std::nullopt, u"The main nation (its own colour)"_s);
    for (const Status &st : STATUS)
        _chip(gf, st.rgb, st.text);
    _chip(gf, COL_OTHER, u"Nothing of that"_s);
    s->lay->addWidget(host);
    s->lay->addWidget(label(u"Several at once are striped — e.g. a puppet in its sphere: yellow and teal."_s, u"faint"_s));
    lay->addWidget(s);
    s = legend_rel = new Section();
    s->lay->addWidget(label(u"Map — each nation in its relation with the main nation (the value on the disc at its "
                            u"capital)"_s));
    host = new QWidget();
    gf = new widgets::GridFlow(host, 4, 130, 22);
    legend_a2 = _chip(gf, std::nullopt, u"The main nation"_s);
    for (int v : {-200, -100, 0, 100, 200})
        _chip(gf, relation_rgb(v), v ? plus(v) : u"0"_s);
    _chip(gf, COL_OTHER, u"None set"_s);
    s->lay->addWidget(host);
    lay->addWidget(s);
    lay->addSpacing(fontMetrics().lineSpacing() + 4);
    lay->addStretch(1);
    _show_view();
}

// ---------------------------------------------------------------------------------------------- the two views
void DiploPanel::set_view(const QString &key, bool emit)
{
    if (key != u"relations" && key != u"status")
        return;
    const bool changed = key != view;
    view = key;
    for (const auto &[k, bt] : seg)
        bt->setChecked(k == key);
    _show_view();
    if (doc) {
        _building = true;
        _fill();
        _building = false;
    }
    if (emit && changed)
        Q_EMIT viewChanged(key);
}

void DiploPanel::sync_mode(const QString &mode)
{
    // the map mode was picked in the bar: the panel follows
    for (const auto &[k, m] : MODE)
        if (m == mode)
            set_view(k, false);
}

void DiploPanel::_show_view()
{
    const bool r = view == u"relations";
    rel_box->setVisible(r);
    st_box->setVisible(!r);
    legend_rel->setVisible(r);
    legend_status->setVisible(!r);
    gp_label->setVisible(!r);
}

QLabel *DiploPanel::_chip(widgets::GridFlow *gf, const std::optional<Rgb> &rgb, const QString &text)
{
    auto *w = new QWidget();
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto *sw = new QLabel();
    sw->setFixedSize(14, 14);
    if (rgb)
        sw->setPixmap(widgets::swatch_icon(*rgb, 14).pixmap(14, 14));
    h->addWidget(sw);
    h->addWidget(label(text, u"faint"_s, false), 1);
    gf->addWidget(w);
    return sw;
}

// ---------------------------------------------------------------------------------------------- helpers
oob::OOBEdits *DiploPanel::oob_map() const
{
    return doc && doc->maps.contains(u"oob"_s) ? &doc->map<oob::OOBEdits>(u"oob"_s) : nullptr;
}

diplomacy::DiploEdits *DiploPanel::dip_map() const
{
    return doc && doc->maps.contains(u"diplomacy"_s) ? &doc->map<diplomacy::DiploEdits>(u"diplomacy"_s) : nullptr;
}

QString DiploPanel::cname(const QString &t) const
{
    const moddata::Country *c = mod.countries.get(t);
    return c ? c->name : t;
}

QString DiploPanel::chip(const QString &t) const
{
    const moddata::Country *c = mod.countries.get(t);
    const Rgb col = c ? c->color : Rgb{128, 128, 128};
    return u"<span style='color:rgb(%1,%2,%3)'>■</span> %4"_s.arg(std::get<0>(col))
        .arg(std::get<1>(col))
        .arg(std::get<2>(col))
        .arg(cname(t));
}

// ---------------------------------------------------------------------------------------------- refresh
void DiploPanel::refresh(document::Document *d, const OptStr &na, const OptStr &nb)
{
    doc = d;
    a = na && mod.countries.contains(*na) ? na : std::nullopt;
    b = nb && mod.countries.contains(*nb) && nb != a ? nb : std::nullopt;
    gps = d && d->maps.contains(u"nations"_s)
              ? diplomacy::great_powers(mod, d->map<countryhist::NationEdits>(u"nations"_s))
              : QStringList();
    _building = true;
    _fill();
    _building = false;
    if (web_view && shown == u"web" && show_web)
        show_web();
}

void DiploPanel::set_shown(const QString &key)
{
    shown = key;
    for (const auto &[k, bt] : shown_btn)
        bt->setChecked(k == key);
    Q_EMIT shownChanged(key);
    if (key == u"web" && web_view && doc && show_web)
        show_web();
}

std::vector<WarRow> DiploPanel::wars() const
{
    // [(file, name, attackers, defenders, start, end, tags the mod does not have)] of history/wars
    std::vector<WarRow> out;
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return out;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    for (const QString &rf : entryview::_files(mod, &store, u"history/wars"_s, true)) {
        const moreviews::War w = moreviews::read_war(store.text(rf));
        std::set<QString> miss;
        for (const QStringList *side : {&w.att, &w.def})
            for (const QString &t : *side)
                if (!mod.countries.contains(t))
                    miss.insert(t);
        out.push_back({rf, !w.name.isEmpty() ? w.name : rf.mid(rf.lastIndexOf(u'/') + 1), w.att, w.def, w.start,
                       w.end, QStringList(miss.begin(), miss.end())});
    }
    return out;
}

void DiploPanel::_fill_wars()
{
    std::vector<WarRow> ws = wars();
    auto mine_of = [this](const WarRow &w) { return a && (w.att.contains(*a) || w.def.contains(*a)); };
    std::vector<WarRow> sorted_ = ws;
    std::stable_sort(sorted_.begin(), sorted_.end(), [&](const WarRow &x, const WarRow &y) {
        const bool nx = !mine_of(x), ny = !mine_of(y);
        if (nx != ny)
            return nx < ny;
        return x.name.toLower() < y.name.toLower();
    });
    war_list->clear();
    long long dead = 0;
    auto names = [this](const QStringList &ts) {
        QStringList o;
        for (const QString &t : ts)
            o << cname(t);
        return o.isEmpty() ? u"?"_s : o.join(u", "_s);
    };
    for (const WarRow &w : sorted_) {
        const bool mine = mine_of(w);
        const QString text = u"%1%2\n%3  against  %4  ·  from %5%6"_s.arg(mine ? u"● "_s : u""_s, w.name, names(w.att),
                                                                           names(w.def),
                                                                           !w.start.isEmpty() ? w.start : u"?"_s,
                                                                           !w.end.isEmpty() ? u" to "_s + w.end : u""_s);
        auto *it = new QListWidgetItem(text);
        it->setData(Qt::UserRole, w.rel);
        if (!w.missing.isEmpty()) {
            dead += 1;
            it->setText(text + u"\n⚠ names nations this mod does not have: %1"_s.arg(w.missing.join(u", "_s)));
            it->setForeground(QBrush(QColor(theme::ERROR_)));
            it->setToolTip(u"A war file left over from another set of nations (the base game's). It is listed in "
                           u"Errors with a fix that takes the file out."_s);
        }
        war_list->addItem(it);
    }
    int hsum = 0;
    for (int i = 0; i < war_list->count(); ++i)
        hsum += war_list->sizeHintForRow(i);
    war_list->setFixedHeight(std::min(260, std::max(46, hsum + 8)));
    war_list->setVisible(!ws.empty());
    long long n_mine = 0;
    for (const WarRow &w : ws)
        if (mine_of(w))
            ++n_mine;
    const qsizetype nw = qsizetype(ws.size());
    if (ws.empty())
        war_note->setText(u"No war is running at the start (history/wars has no file)."_s);
    else
        war_note->setText(
            u"%1 war file%2 in history/wars%3%4"_s.arg(nw)
                .arg(nw == 1 ? u""_s : u"s"_s,
                     a ? u" — %1 with %2 (●)"_s.arg(n_mine).arg(cname(*a)) : u""_s,
                     dead ? u". <span style='color:%1'>%2 of them name%3 nations that are not in this mod — left-over "
                            u"files, see Errors.</span>"_s.arg(theme::ERROR_)
                                .arg(dead)
                                .arg(dead == 1 ? u"s"_s : u""_s)
                          : u"."_s));
}

void DiploPanel::_fill()
{
    oob::OOBEdits *ob = oob_map();
    diplomacy::DiploEdits *dip = dip_map();
    _fill_wars();
    const long long nc = static_cast<long long>(mod.countries.size());
    if (!_psig || *_psig != nc) {
        _psig = nc;
        picker->fill(nationpanel::nation_items(mod), {}, a);
    }
    const int pi = picker->find(a);
    picker->blockSignals(true);
    picker->setCurrentIndex(pi >= 0 ? pi : -1);
    picker->blockSignals(false);
    QStringList chips;
    for (const QString &t : gps)
        chips << chip(t);
    gp_label->setText(u"Great powers at the start (the top 8 westernized nations by prestige): "_s + chips.join(u", "_s));
    clear_btn->setVisible(a.has_value());
    if (!a || !ob) {
        title->setText(u"Click a nation on the map, or pick one above."_s);
        sub->setText(u""_s);
        pair_section->hide();
        list_section->hide();
        return;
    }
    const QString A = *a;
    for (QLabel *lg : {legend_a, legend_a2})
        lg->setPixmap(widgets::swatch_icon(mod.countries.get(A)->color, 14).pixmap(14, 14));
    title->setText(u"%1  (%2)%3"_s.arg(cname(A), A, gps.contains(A) ? u"  ·  great power"_s : u""_s));
    if (view == u"relations") {
        std::vector<long long> vals;
        for (const QString &t : _related(A))
            vals.push_back(diplomacy::relation_value(*ob, A, t));
        long long good = 0, bad = 0;
        for (long long v : vals) {
            good += v > 0;
            bad += v < 0;
        }
        const qsizetype nv = qsizetype(vals.size());
        sub->setText(!vals.empty() ? u"%1 relation%2 set: <span style='color:rgb%3'>%4 friendly</span> · "
                                     u"<span style='color:rgb%5'>%6 hostile</span>"_s.arg(nv)
                                         .arg(nv == 1 ? u""_s : u"s"_s, rgb_str(relation_rgb(150)))
                                         .arg(good)
                                         .arg(rgb_str(relation_rgb(-150)))
                                         .arg(bad)
                                   : u"No relations set."_s);
    } else {
        const OrderedMap<QString, QStringList> st = statuses(*ob, *dip, A);
        static const QHash<QString, QString> NAMES = {
            {u"puppet"_s, u"puppets"_s},         {u"substate"_s, u"substates"_s}, {u"sphere"_s, u"in its sphere"_s},
            {u"ally"_s, u"allies"_s},            {u"overlord"_s, u"overlord"_s},  {u"suzerain"_s, u"suzerain"_s},
            {u"lord"_s, u"in the sphere of"_s}};
        QStringList facts;
        for (const Status &s : STATUS) {
            QStringList ts;
            for (const auto &[t, ks] : st)
                if (ks.contains(s.key) && mod.countries.contains(t))
                    ts << t;
            std::sort(ts.begin(), ts.end());
            if (!ts.isEmpty()) {
                QStringList nm;
                for (const QString &t : ts)
                    nm << cname(t);
                facts << u"<span style='color:rgb%1'>%2</span>: %3"_s.arg(
                    rgb_str(s.rgb), NAMES.value(s.key), ts.size() <= 3 ? nm.join(u", "_s) : QString::number(ts.size()));
            }
        }
        sub->setText(!facts.isEmpty() ? facts.join(u" · "_s) : u"No treaties, no sphere."_s);
    }
    _fill_pair();
    _fill_list();
}

std::optional<std::pair<QString, QString>> DiploPanel::_gp_side() const
{
    // (great power, the other one) for the standing between A and B, or None
    const bool ga = a && gps.contains(*a), gb = b && gps.contains(*b);
    if (ga && !gb)
        return std::pair{*a, b.value_or(QString())};
    if (gb && !ga)
        return std::pair{*b, a.value_or(QString())};
    return std::nullopt;
}

void DiploPanel::_fill_pair()
{
    pair_section->setVisible(b.has_value());
    if (!b)
        return;
    const QString A = *a, B = *b;
    oob::OOBEdits &ob = *oob_map();
    diplomacy::DiploEdits &dip = *dip_map();
    pair_title->setText(u"%1  ↔  %2"_s.arg(chip(A), chip(B)));
    const auto [ra, rb] = diplomacy::pair(ob, A, B);
    const OptStr va = ra ? ra->value : std::nullopt;
    const OptStr vb = rb ? rb->value : std::nullopt;
    rel->blockSignals(true);
    rel->setValue(double(diplomacy::relation_value(ob, A, B)));
    rel->blockSignals(false);
    if (va && vb)
        rel_note->setText(u"Both files set it: %1's %2 + %3's %4 — the game adds them. Moving the slider writes the "
                          u"total into %5's file only."_s.arg(A, *va, B, *vb, A));
    else if (va || vb)
        rel_note->setText(u"Written in %1's OOB file."_s.arg(va ? A : B));
    else
        rel_note->setText(u"Not set (0). Moving the slider writes it into %1's OOB file."_s.arg(A));
    const auto side = _gp_side();
    level->setVisible(side.has_value());
    infl_row->setVisible(side.has_value());
    lvl_title->setVisible(side.has_value());
    if (side) {
        const auto &[gp, other] = *side;
        lvl_title->setText(u"%1's standing with %2  (%3 is a great power)"_s.arg(cname(gp), cname(other), cname(gp)));
        const std::optional<long long> lvl = diplomacy::level_of(ob, gp, other);
        level->setValue(lvl ? int(*lvl) : 2);
        infl->blockSignals(true);
        infl->setValue(double(diplomacy::influence_of(ob, gp, other)));
        infl->blockSignals(false);
        lvl_note->setText(u"In sphere = %1 is in %2's sphere of influence. Neutral isn't written (it's the game's "
                          u"default)."_s.arg(cname(other), cname(gp)));
    } else if (gps.contains(A) && gps.contains(B)) {
        lvl_note->setText(u"Both are great powers — great powers have no standing with each other."_s);
    } else {
        lvl_note->setText(u"Neither is a great power at the start, so neither has a standing (influence, sphere) with "
                          u"the other."_s);
    }
    for (const auto &[x, y] : {std::pair{A, B}, std::pair{B, A}})
        if (!gps.contains(x) && (diplomacy::level_of(ob, x, y) || diplomacy::influence_of(ob, x, y))) {
            lvl_note->setText(lvl_note->text() + u"  ⚠ %1's file gives it a standing with %2 anyway — the game ignores "
                                                 u"it (the Errors tab removes it)."_s.arg(x, y));
            break;
        }
    // treaties
    subject->blockSignals(true);
    subject->clear();
    for (const auto &[key, text] : SUBJECT_CHOICES)
        subject->addItem(QString(text).replace(u"{A}"_s, cname(A)).replace(u"{B}"_s, cname(B)),
                         key ? QVariant(*key) : QVariant());
    const std::vector<diplomacy::Record> recs = dip.between(A, B);
    OptStr cur;
    for (const diplomacy::Record &r : recs)
        if (r.kind == u"vassal" || r.kind == u"substate")
            cur = (r.first == A ? u"b_"_s : u"a_"_s) + r.kind;
    subject->setCurrentIndex(cur ? std::max(0, subject->findData(*cur)) : 0);
    subject->blockSignals(false);
    bool ally = false;
    QStringList other;
    for (const diplomacy::Record &r : recs) {
        if (r.kind == u"alliance")
            ally = true;
        else if (r.kind != u"vassal" && r.kind != u"substate")
            other << u"%1 (%2 → %3, until %4)"_s.arg(kind_name(r.kind), r.first, r.second, r.end.value_or(u"None"_s));
    }
    alliance->setChecked(ally);
    other_treaties->setVisible(!other.isEmpty());
    other_treaties->setText(u"Also: "_s + other.join(u", "_s));
    QStringList tn;
    for (const diplomacy::Record &r : recs)
        tn << u"%1: %2, %3 – %4"_s.arg(kind_name(r.kind), r.file, r.start.value_or(u"None"_s), r.end.value_or(u"None"_s));
    treaty_note->setText(!recs.empty() ? tn.join(u" · "_s)
                                       : u"New treaties start at the scenario date and end like the others of their "
                                         u"kind."_s);
}

QStringList DiploPanel::_related(const QString &na) const
{
    // nations with a relation value with a (in either file)
    const oob::OOBEdits &ob = *oob_map();
    std::vector<QString> keys;
    for (const auto &[t, _r] : ob.state(na).relations)
        keys.push_back(t);
    grid::PySet<QString> comp;
    for (const QString &t : py_tags_with_units(ob))
        if (ob.state(t).relations.contains(na))
            comp.add(t);
    grid::PySet<QString> all = grid::PySet<QString>::copy_of(grid::PySet<QString>::from_keys(keys));
    all.merge(comp);
    QStringList out;
    for (const QString &t : all.items()) {
        if (t == na || !mod.countries.contains(t))
            continue;
        const auto [ra, rb] = diplomacy::pair(ob, na, t);
        if ((ra && ra->value) || (rb && rb->value))
            out << t;
    }
    return out;
}

void DiploPanel::_fill_list()
{
    const QString A = *a;
    const oob::OOBEdits &ob = *oob_map();
    const OrderedMap<QString, QStringList> st = statuses(ob, *dip_map(), A);
    const bool r = view == u"relations";
    QStringList rows;
    QSet<QString> seen;
    auto put = [&](const QString &t) {
        if (!seen.contains(t)) {
            seen.insert(t);
            rows << t;
        }
    };
    if (!r)
        for (const auto &[t, _ks] : st)
            put(t);
    for (const auto &[t, _r] : ob.state(A).relations)
        put(t);
    for (const QString &t : py_tags_with_units(ob))
        if (ob.state(t).relations.contains(A))
            put(t);
    static const QHash<QString, QString> NAMES = {
        {u"puppet"_s, u"puppet"_s},       {u"substate"_s, u"substate"_s}, {u"suzerain"_s, u"its suzerain"_s},
        {u"sphere"_s, u"in its sphere"_s}, {u"ally"_s, u"ally"_s},         {u"overlord"_s, u"its overlord"_s},
        {u"lord"_s, u"has it in its sphere"_s}};
    struct Item {
        long long r0;                              // rank: (-v, name) or (0 / 1, -v)
        QString r1n;
        long long r1v;
        QString t, text;
    };
    std::vector<Item> items;
    for (const QString &t : rows) {
        if (!mod.countries.contains(t) || t == A)
            continue;
        const long long v = diplomacy::relation_value(ob, A, t);
        const QStringList ks = st.value(t);
        QStringList parts;
        Item it;
        if (r) {
            parts << plus(v);
            it.r0 = -v;
            it.r1n = cname(t);
            it.r1v = 0;
        } else {
            for (const QString &k : ks)
                parts << NAMES.value(k);
            parts << u"relations %1"_s.arg(plus(v));
            it.r0 = ks.isEmpty() ? 1 : 0;
            it.r1v = -v;
        }
        it.t = t;
        it.text = u"%1 (%2)  ·  %3"_s.arg(cname(t), t, parts.join(u" · "_s));
        items.push_back(it);
    }
    std::sort(items.begin(), items.end(), [](const Item &x, const Item &y) {
        if (x.r0 != y.r0)
            return x.r0 < y.r0;
        if (x.r1n != y.r1n)
            return x.r1n < y.r1n;
        if (x.r1v != y.r1v)
            return x.r1v < y.r1v;
        if (x.t != y.t)
            return x.t < y.t;
        return x.text < y.text;
    });
    QListWidget *lst = rel_list;
    lst->clear();
    for (const Item &i : items) {
        auto *it = new QListWidgetItem(widgets::swatch_icon(mod.countries.get(i.t)->color, 12), i.text);
        it->setData(Qt::UserRole, i.t);
        it->setToolTip(i.text + u"\nClick to set its relations with %1"_s.arg(cname(A)));
        lst->addItem(it);
        if (b && i.t == *b)
            it->setSelected(true);
    }
    list_section->setVisible(true);
    list_title->setText((r ? u"%1's relations, friendliest first (%2)"_s : u"%1's treaties, spheres and relations (%2)"_s)
                            .arg(cname(A))
                            .arg(items.size()));
    const int rows_n = std::max(1, std::min(10, lst->count()));
    lst->setFixedHeight(rows_n * (lst->count() ? lst->sizeHintForRow(0) : 24) + 6);
}

// ---------------------------------------------------------------------------------------------- edits
void DiploPanel::_later(const QString &key, double value)
{
    if (_building || !b)
        return;
    _pending[key] = value;
    _timer->start();
}

void DiploPanel::_flush()
{
    std::map<QString, double> pend;
    pend.swap(_pending);
    if (pend.empty() || !a || !b)
        return;
    const QString A = *a, B = *b;
    const auto side = _gp_side();
    const std::optional<long long> rv =
        pend.contains(u"rel"_s) ? std::optional<long long>((long long)pend[u"rel"_s]) : std::nullopt;
    const std::optional<long long> iv =
        pend.contains(u"infl"_s) ? std::optional<long long>((long long)pend[u"infl"_s]) : std::nullopt;
    QStringList what;
    if (rv)
        what << u"relations %1"_s.arg(plus(*rv));
    if (iv)
        what << u"influence %1"_s.arg(*iv);
    const double infl_raw = iv ? pend[u"infl"_s] : 0.0;
    Q_EMIT edit(u"%1 ↔ %2: %3"_s.arg(A, B, what.join(u", "_s)),
                [A, B, rv, iv, infl_raw, side](oob::OOBEdits &ob, diplomacy::DiploEdits &) {
                    if (rv)
                        set_relation(ob, A, B, *rv);
                    if (iv && side)
                        set_entry(ob, side->first, side->second, u"influence_value"_s,
                                  infl_raw != 0 ? OptStr(QString::number(*iv)) : std::nullopt);
                });
}

void DiploPanel::_level(int lvl)
{
    const auto side = _gp_side();
    if (!side || _building)
        return;
    const auto [gp, other] = *side;
    const OptStr val = lvl == 2 ? std::nullopt : OptStr(QString::number(lvl));   // Neutral = the game's default: not written
    Q_EMIT edit(u"%1's standing with %2 → %3"_s.arg(gp, other, oob::LEVELS[lvl]),
                [gp, other, val](oob::OOBEdits &ob, diplomacy::DiploEdits &) {
                    set_entry(ob, gp, other, u"level"_s, val);
                });
}

void DiploPanel::_subject()
{
    const QString A = a.value_or(QString()), B = b.value_or(QString());
    const QVariant key = subject->currentData();
    std::optional<std::pair<QString, QString>> kind;
    if (key.isValid() && !key.toString().isEmpty()) {
        const QStringList p = key.toString().split(u'_');
        kind = std::pair{p[1], p[0] == u"b" ? A : B};
    }
    Q_EMIT edit(u"%1 ↔ %2: %3"_s.arg(A, B, subject->currentText()),
                [A, B, kind](oob::OOBEdits &, diplomacy::DiploEdits &dip) { dip.set_subject(A, B, kind); });
}

void DiploPanel::_treaty(const QString &kind, bool on)
{
    const QString A = a.value_or(QString()), B = b.value_or(QString());
    Q_EMIT edit(u"%1 ↔ %2: %3 %4"_s.arg(A, B, diplomacy::KIND_NAME.value(kind), on ? u"on"_s : u"off"_s),
                [A, B, kind, on](oob::OOBEdits &, diplomacy::DiploEdits &dip) { dip.set_treaty(A, B, kind, on); });
}

// ---------------------------------------------------------------------------------------------- map
std::vector<mapview::Shape> DiploPanel::shapes() const
{
    std::vector<mapview::Shape> out;
    if (!a || !doc)
        return out;
    const QString A = *a;
    const oob::OOBEdits &ob = *oob_map();
    if (b) {
        const auto ca = _cap(A), cb = _cap(*b);
        if (ca && cb) {
            mapview::Shape sh;
            sh.kind = u"line"_s;
            sh.pts = {*ca, *cb};
            sh.color = {246, 216, 120};
            sh.width = 3.5;
            out.push_back(sh);
        }
    }
    if (view != u"relations")
        return out;
    // the relation with A: a disc with the number at every capital that has one
    std::vector<QString> keys;
    for (const auto &[t, _r] : ob.state(A).relations)
        keys.push_back(t);
    grid::PySet<QString> seen = grid::PySet<QString>::from_keys(keys);
    for (const QString &t : py_tags_with_units(ob))
        if (ob.state(t).relations.contains(A))
            seen.add(t);
    for (const QString &t : seen.items()) {
        if (t == A || !mod.countries.contains(t))
            continue;
        const auto [ra, rb] = diplomacy::pair(ob, A, t);
        if (!((ra && ra->value) || (rb && rb->value)))
            continue;
        const auto c = _cap(t);
        if (!c)
            continue;
        const long long v = diplomacy::relation_value(ob, A, t);
        mapview::Shape sh;
        sh.kind = u"badge"_s;
        sh.pts = {*c};
        sh.color = relation_rgb(double(v));
        sh.size = 11.0;
        sh.text = v ? plus(v) : u"0"_s;
        sh.tag = QVariantList{u"rel"_s, t};
        sh.halo = b && t == *b;
        out.push_back(sh);
    }
    return out;
}

std::optional<QPointF> DiploPanel::_cap(const QString &tag) const
{
    OptStr cap;
    if (doc && doc->maps.contains(u"nations"_s) && mod.country_hist && mod.country_hist->files.contains(tag))
        cap = doc->map<countryhist::NationEdits>(u"nations"_s).state(tag).at(u"capital"_s);
    const std::optional<long long> pid =
        cap && !cap->isEmpty() && py::isdigit(*cap) ? std::optional<long long>(py::to_int(*cap)) : std::nullopt;
    const mapview::MapData &md = mapdata;
    if (!pid || !(0 < *pid && *pid < md.n) || !md.area[size_t(*pid)])
        return std::nullopt;
    return QPointF(double(md.cx[size_t(*pid)]) + 0.5, double(md.cy[size_t(*pid)]) + 0.5);
}

void DiploPanel::map_clicked(const QVariantList &tag)
{
    if (!tag.isEmpty() && tag[0].toString() == u"rel")
        Q_EMIT otherPicked(tag[1].toString());
}

QString DiploPanel::describe(const QVariantList &tag) const
{
    if (!tag.isEmpty() && tag[0].toString() == u"rel" && a) {
        const QString t = tag[1].toString();
        return u"%1 ↔ %2: relations %3  ·  click to set them"_s.arg(cname(*a), cname(t),
                                                                   plus(diplomacy::relation_value(*oob_map(), *a, t)));
    }
    return u""_s;
}

// ---------------------------------------------------------------------------------------------- relation edits
void set_entry(oob::OOBEdits &ob, const QString &x, const QString &y, const QString &key, const OptStr &value)
{
    // x's OOB entry for y: set one field (None removes it; an empty entry goes)
    ob.edit(x, [&](oob::OOB &s) {
        oob::Relation *r = s.relations.get(y);
        if (!r) {
            if (!value)
                return;
            s.relations.insert(y, oob::Relation{});
            r = s.relations.get(y);
        }
        if (key == u"value")
            r->value = value;
        else if (key == u"level")
            r->level = value;
        else
            r->influence_value = value;
        if (!r->value && !r->level && !r->influence_value)
            s.relations.remove(y);
    });
}

void set_relation(oob::OOBEdits &ob, const QString &a, const QString &b, long long v)
{
    // the relation between a and b = v: written into a's file; b's value for a goes (the game adds both)
    {
        const oob::Relation *rb = ob.state(b).relations.get(a);
        if (rb && rb->value)
            set_entry(ob, b, a, u"value"_s, std::nullopt);
    }
    const oob::Relation *ra = ob.state(a).relations.get(b);
    const bool keep = ra && (ra->level || ra->influence_value);
    set_entry(ob, a, b, u"value"_s, v != 0 || keep ? OptStr(QString::number(v)) : std::nullopt);
}

}  // namespace diplopanel
