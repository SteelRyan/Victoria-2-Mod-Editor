// cbpanel.py — see cbpanel.h.
#include "ui/cbpanel.h"
#include "core/cbcheck.h"
#include "core/document.h"
#include "core/events.h"
#include "core/localisation.h"
#include "core/modifiers.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/scriptstore.h"
#include "map/iconstrips_data.h"
#include "ui/iconstrips.h"
#include "ui/modpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>

using namespace Qt::StringLiterals;

namespace cbpanel {

using cbcheck::REL;

const QString ORDER = u"peace_order"_s;
const QString STRIP = u"GFX_diplo_wargoal_icons"_s;
const OrderedMap<QString, std::pair<QString, QString>> DEMANDS = {
    {u"po_annex"_s, {u"annex"_s, u"annexes the nation"_s}},
    {u"po_demand_state"_s, {u"state"_s, u"takes a state"_s}},
    {u"po_add_to_sphere"_s, {u"sphere"_s, u"adds the nation to the sphere"_s}},
    {u"po_disarmament"_s, {u"disarm"_s, u"disarms the nation"_s}},
    {u"po_reparations"_s, {u"reparations"_s, u"makes it pay war reparations"_s}},
    {u"po_transfer_provinces"_s, {u"transfer"_s, u"hands provinces over to another nation"_s}},
    {u"po_remove_prestige"_s, {u"humiliate"_s, u"takes prestige from it (humiliation)"_s}},
    {u"po_make_puppet"_s, {u"puppet"_s, u"makes the nation a puppet"_s}},
    {u"po_release_puppet"_s, {u"release"_s, u"frees a puppet of the nation"_s}},
    {u"po_status_quo"_s, {u"status quo"_s, u"ends the war as it stands (status quo)"_s}},
    {u"po_install_communist_gov_type"_s, {u"communism"_s, u"installs a communist government"_s}},
    {u"po_uninstall_communist_gov_type"_s, {u"no communism"_s, u"removes the communist government"_s}},
    {u"po_remove_cores"_s, {u"cores"_s, u"removes the cores on what is taken"_s}},
    {u"po_colony"_s, {u"colony"_s, u"takes a colony that is being claimed"_s}},
    {u"po_destroy_forts"_s, {u"forts"_s, u"tears down the forts of a state"_s}},
    {u"po_destroy_naval_bases"_s, {u"naval bases"_s, u"tears down the naval bases of a state"_s}},
    {u"po_gunboat"_s, {u"debts"_s, u"forces it to pay its debts"_s}},
    {u"po_clear_union_sphere"_s, {u"union sphere"_s, u"takes the nations of its culture group out of other spheres"_s}},
};
const OrderedMap<QString, std::pair<QString, QString>> PARTS = {
    {u"can_use"_s, {u"Who may use it"_s, u"cond"_s}},
    {u"is_valid"_s, {u"Stays valid while"_s, u"cond"_s}},
    {u"prerequisites"_s, {u"Given by itself when"_s, u"cond"_s}},
    {u"allowed_countries"_s, {u"Which nations it can be about"_s, u"cond"_s}},
    {u"allowed_states"_s, {u"Which states it can demand"_s, u"cond"_s}},
    {u"allowed_states_in_crisis"_s, {u"Which states it can demand in a crisis"_s, u"cond"_s}},
    {u"allowed_substate_regions"_s, {u"Which substate regions it can demand"_s, u"cond"_s}},
    {u"on_add"_s, {u"When it is added to a war"_s, u"effect"_s}},
    {u"on_po_accepted"_s, {u"When the peace is signed"_s, u"effect"_s}},
};
const OrderedMap<QString, QString> NICE = {
    {u"sprite_index"_s, u"Picture (number in the strip)"_s},
    {u"is_triggered_only"_s, u"Only when something gives it"_s},
    {u"months"_s, u"Valid for (months)"_s},
    {u"truce_months"_s, u"Truce after it (months)"_s},
    {u"badboy_factor"_s, u"Infamy ×"_s},
    {u"prestige_factor"_s, u"Prestige ×"_s},
    {u"peace_cost_factor"_s, u"Cost in the peace ×"_s},
    {u"penalty_factor"_s, u"Penalty when not enforced ×"_s},
    {u"tws_battle_factor"_s, u"War score from battles ×"_s},
    {u"break_truce_prestige_factor"_s, u"Breaking a truce: prestige ×"_s},
    {u"break_truce_infamy_factor"_s, u"Breaking a truce: infamy ×"_s},
    {u"break_truce_militancy_factor"_s, u"Breaking a truce: militancy ×"_s},
    {u"good_relation_prestige_factor"_s, u"At good relations: prestige ×"_s},
    {u"good_relation_infamy_factor"_s, u"At good relations: infamy ×"_s},
    {u"good_relation_militancy_factor"_s, u"At good relations: militancy ×"_s},
    {u"construction_speed"_s, u"Speed of justifying it ×"_s},
    {u"constructing_cb"_s, u"Can be justified"_s},
    {u"crisis"_s, u"Can be a war goal of a crisis"_s},
    {u"always"_s, u"Always at hand (no justifying)"_s},
    {u"all_allowed_states"_s, u"Takes every allowed state at once"_s},
    {u"is_civil_war"_s, u"The war goal of a civil war"_s},
    {u"great_war_obligatory"_s, u"Always part of a great war's peace"_s},
    {u"mutual"_s, u"Counts for the defender too"_s},
    {u"war_name"_s, u"Name of the war (key)"_s},
};
const QString TEMPLATE = u"%1 = {\n"
                         "\tsprite_index = 1\n"
                         "\tis_triggered_only = yes\n"
                         "\tmonths = 12\n"
                         "\n"
                         "\tbadboy_factor = 1\n"
                         "\tprestige_factor = 1\n"
                         "\tpeace_cost_factor = 1\n"
                         "\tpenalty_factor = 1\n"
                         "\n"
                         "\tbreak_truce_prestige_factor = 1\n"
                         "\tbreak_truce_infamy_factor = 1\n"
                         "\tbreak_truce_militancy_factor = 1\n"
                         "\ttruce_months = 60\n"
                         "\n"
                         "\tcan_use = {\n"
                         "\t\tNOT = { is_our_vassal = THIS }\n"
                         "\t}\n"
                         "\n"
                         "\tpo_remove_prestige = yes\n"
                         "\n"
                         "\twar_name = WAR_NAME\n"
                         "\n"
                         "\ton_add = {\n"
                         "\t}\n"
                         "}\n"_s;

static const QString BOX_CSS = u"QLabel { color: %1; border: 2px solid %2; border-radius: 4px; padding: 6px 8px; }"_s;

QString strip_colours(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i) == u'§') {
            if (i + 1 < s.size() && s.at(i + 1) != u'\n')
                ++i;
            continue;
        }
        out += s.at(i);
    }
    return out;
}

bool f_own(const QString &loc_key, const QString &key)
{
    return py::lower(loc_key).startsWith(py::lower(key));
}

static QString esc(const QString &s) { return refs::esc(s); }

static QString rstrip_nl(QString s)          // s.rstrip("\n")
{
    while (s.endsWith(u'\n'))
        s.chop(1);
    return s;
}

// a sort value standing for Python's tuple (0, s) / (1, "")
static QString tup(bool order, const QString &s = {})
{
    return order ? u"\x01"_s : u"\x00"_s + s;
}

WarGoalView::WarGoalView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *s = new widgets::Section();
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a war goal by name, key or what it demands…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_list(); });
    s->lay->addWidget(search);
    auto *row = new QHBoxLayout();
    count = widgets::label(QString(), u"sub"_s);
    row->addWidget(count, 1);
    auto *b = new QPushButton(u"＋ New war goal"_s);
    b->setToolTip(u"A new war goal: a copy of the one picked (or a plain one), put into peace_order too"_s);
    connect(b, &QPushButton::clicked, this, [this] { _new_open(); });
    row->addWidget(b, 0, Qt::AlignTop);
    s->lay->addLayout(row);
    new_row = new QWidget();
    auto *nv = new QVBoxLayout(new_row);
    nv->setContentsMargins(0, 0, 0, 0);
    new_note = widgets::label(QString(), u"faint"_s);
    nv->addWidget(new_note);
    auto *nh = new QHBoxLayout();
    new_edit = new QLineEdit();
    new_edit->setPlaceholderText(u"Key of the new war goal (letters, digits, _)"_s);
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
    lst->setHeaderLabels({u"War goal"_s, u"Key"_s, u"Demands"_s, u"Given by"_s});
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
    for (const auto &[c, w] : std::vector<std::pair<int, int>>{{1, 170}, {2, 120}, {3, 74}}) {
        hd->setSectionResizeMode(c, QHeaderView::Fixed);
        hd->resizeSection(c, w);
    }
    lst->setSortingEnabled(true);
    lst->sortByColumn(-1, Qt::AscendingOrder);   // as in the file, until a column's name is clicked
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *c, QTreeWidgetItem *p) { _picked(c, p); });
    connect(lst, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) { _double(it); });
    s->lay->addWidget(lst);
    lay->addWidget(s);
    // --- the one picked (its own part of the side panel: the Scripts panel puts it under the box that tells what the
    // word at the cursor is — list, word, the one picked: the order events and decisions have)
    s = detail_sec = new widgets::Section();
    head = new QLabel();
    head->setWordWrap(true);
    s->lay->addWidget(head);
    auto *trow = new QHBoxLayout();
    pic = new QLabel();
    pic->hide();
    trow->addWidget(pic, 0, Qt::AlignTop);
    auto *tv = new QVBoxLayout();
    title = widgets::label(u"Pick a war goal"_s, u"title"_s);
    tv->addWidget(title);
    meta = widgets::label(QString(), u"faint"_s);
    tv->addWidget(meta);
    trow->addLayout(tv, 1);
    s->lay->addLayout(trow);
    problems = new QLabel();
    problems->setWordWrap(true);
    problems->setTextFormat(Qt::RichText);
    problems->setStyleSheet(BOX_CSS.arg(theme::ERROR_, theme::ERROR_));
    problems->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(problems, &QLabel::linkActivated, this, &WarGoalView::linkActivated);
    problems->hide();
    s->lay->addWidget(problems);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    const std::vector<std::tuple<QString, QString, QString>> fields = {
        {u"name"_s, u"Name in the game"_s, u"<key>"_s},
        {u"short"_s, u"Short name"_s, u"<key>_short — in the list of war goals of a war"_s},
        {u"setup"_s, u"In the war goal window"_s,
         u"<key>_setup — the line when you pick it; $RECIPIENT$, $STATE$, $THIRD$ are filled in by the game"_s},
        {u"desc"_s, u"Description"_s, u"<key>_desc — the tooltip"_s},
        {u"normal"_s, u"Name of the war"_s,
         u"NORMAL_<war_name> — $FIRST$, $SECOND$, $ORDER$, $STATE$ are filled in by the game"_s},
        {u"agression"_s, u"Name of the war (plain attack)"_s,
         u"AGRESSION_<war_name> — when the attacker has no war goal of its own right"_s}};
    for (int r = 0; r < int(fields.size()); ++r) {
        const auto &[fid, text, tip] = fields[size_t(r)];
        QLabel *lb = widgets::label(text, u"sub"_s, false);
        lb->setToolTip(tip);
        g->addWidget(lb, r, 0);
        auto *ed = new QLineEdit();
        ed->setToolTip(tip);
        connect(ed, &QLineEdit::editingFinished, this, [this, ed, f = fid] { _loc_done(ed, f); });
        g->addWidget(ed, r, 1);
        name_edits.insert(fid, {lb, ed});
    }
    names_host = new QWidget();
    names_host->setLayout(g);
    names_host->hide();
    s->lay->addWidget(names_host);
    body = new QLabel();
    body->setObjectName(u"sub"_s);
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(body, &QLabel::linkActivated, this, &WarGoalView::linkActivated);
    s->lay->addWidget(body);
    auto *brow = new QHBoxLayout();
    del_btn = new QPushButton(u"✕ Remove this war goal"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _remove(); });
    del_btn->hide();
    brow->addWidget(del_btn);
    brow->addStretch(1);
    s->lay->addLayout(brow);
}

// ------------------------------------------------------------------ data
void WarGoalView::refresh(document::Document *doc_, events::EventDB *db_, refs::RefIndex *refs_)
{
    doc = doc_;
    db = db_;
    refs = refs_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (store == nullptr)
        return;
    _read();
    _fill_list();
    _fill_detail();
}

void WarGoalView::_read()
{
    const QString text = store->text(REL);
    if (!_text || _text->constData() != text.constData()) {
        _text = text;
        _nodes = script::parse(text);
        _goals.clear();
        _order = nullptr;
        for (const script::Node &n : _nodes)
            if (n.has_key && n.key == ORDER && n.is_block) {
                _order = &n;
                break;
            }
        for (const script::Node &n : _nodes)
            if (n.has_key && !n.key.isEmpty() && n.is_block && n.key != ORDER && !cbcheck::KNOWN.contains(n.key))
                _goals.emplace_back(n.key, &n);
    }
    _check();
}

OptStr WarGoalView::loc(const QString &key) const
{
    if (doc && doc->maps.contains(u"loc"_s)) {
        const auto &lc = doc->map<localisation::LocEdits>(u"loc"_s);
        if (const auto *v = lc.fresh.get(key))
            return v->first;
    }
    return refs::loc_text(mod, key);
}

QString WarGoalView::nm(const QString &key) const
{
    if (key == ORDER)
        return u"Order in a peace treaty"_s;
    const QString t = py::strip(strip_colours(loc(key).value_or(QString())));
    return !t.isEmpty() ? t : py::capitalize(QString(key).replace(u'_', u' '));
}

const script::Node *WarGoalView::node(const QString &key) const
{
    if (key == ORDER)
        return _order;
    for (const auto &[k, n] : _goals)
        if (k == key)
            return n;
    return nullptr;
}

std::optional<std::pair<qsizetype, qsizetype>> WarGoalView::span(const QString &key) const
{
    const script::Node *n = node(key);
    if (n == nullptr)
        return std::nullopt;
    return std::make_pair(n->kstart, n->vend);
}

OptStr WarGoalView::goal_at(qsizetype pos) const
{
    // The war goal (or peace_order) a position of the file's text is in.
    for (const auto &[k, n] : _goals)
        if (n->kstart <= pos && pos < n->vend)
            return k;
    if (_order != nullptr && _order->kstart <= pos && pos < _order->vend)
        return ORDER;
    return std::nullopt;
}

long long WarGoalView::_frames()
{
    try {
        iconstrips::Strips &st = iconstrips::strips_for(mod, *doc);
        const auto sp = st.sprites();
        const auto *s = sp.get(STRIP);
        return s ? s->frames : 0;
    } catch (...) {
        return 0;
    }
}

void WarGoalView::_check()
{
    // What the check found, by war goal: {key: [(severity, text)]} — the same findings as the Errors tab (_where: the
    // position of each in the file, for the way there).
    _problems.clear();
    _where.clear();
    QSet<QString> fresh;
    if (doc && doc->maps.contains(u"loc"_s))
        for (const auto &kv : doc->map<localisation::LocEdits>(u"loc"_s).fresh)
            fresh.insert(py::lower(kv.first));
    std::optional<cbcheck::Report> rep;
    try {
        rep = cbcheck::run([this](const QString &r) { return store->text(r); }, {},
                           [this, &fresh](const QString &k) {
                               return refs::loc_text(mod, k).has_value() || fresh.contains(py::lower(k));
                           },
                           _frames());
    } catch (...) {
        rep.reset();
    }
    const auto add = [this](const QString &key, const QString &sev, const QString &text, std::optional<qsizetype> pos) {
        if (!_problems.contains(key))
            _problems.insert(key, {});
        _problems.get(key)->emplace_back(sev, text);
        if (!_where.contains(key))
            _where.insert(key, {});
        _where.get(key)->push_back(pos);
    };
    if (rep) {
        const std::vector<std::pair<QString, const std::vector<cbcheck::F> *>> lists = {
            {u"error"_s, &rep->broken}, {u"warn"_s, &rep->lines}, {u"warn"_s, &rep->order}, {u"text"_s, &rep->texts}};
        for (const auto &[sev, found] : lists) {
            for (const cbcheck::F &f : *found) {
                QString key = f.goal && !f.goal->isEmpty() ? *f.goal : (found == &rep->order ? ORDER : QString());
                if (!key.isEmpty()) {
                    const QString txt = f.text.startsWith(key + u": "_s) ? f.text.mid(key.size() + 2) : f.text;
                    add(key, sev, txt, f.pos);
                }
            }
        }
    }
    if (refs != nullptr) {                       // names in it that don't exist (red and wavy in the text)
        const QString text = _text.value_or(QString());
        for (const auto &[key, n] : _goals) {
            for (const refs::Ref &r : refs->in_range(REL, n->kstart, n->vend)) {
                if (!r.ok && (r.kind == u"modifier" || r.kind == u"cb" || r.kind == u"event")) {
                    add(key, u"error"_s,
                        u"line %1: there is no %2 “%3”"_s.arg(QString::number(refs::line_of(text, r.pos)),
                                                               py::lower(refs::KINDS.get(r.kind)->first), r.target),
                        r.pos);
                }
            }
        }
    }
    _given.clear();
    if (refs != nullptr) {
        for (const auto &[key, n] : _goals) {
            std::vector<std::tuple<QString, qsizetype, QString>> g;
            for (const refs::Use &u : refs->usages(u"cb"_s, key))
                if (u.rel != REL && (u.role == u"add" || u.role == u"use"))
                    g.emplace_back(u.rel, u.pos, u.role);
            _given.insert(key, g);
        }
    }
}

QStringList WarGoalView::demands(const script::Node &n) const
{
    QStringList out;
    for (const QString &k : cbcheck::PO)
        for (const script::Node &c : n.kids)
            if (c.has_key && c.key == k && !c.is_block && py::lower(c.value) == u"yes") {
                out << k;
                break;
            }
    return out;
}

// ------------------------------------------------------------------ the list
void WarGoalView::_fill_list()
{
    if (store == nullptr)
        return;
    const QString q = py::lower(py::strip(search->text()));
    lst->blockSignals(true);
    lst->setSortingEnabled(false);
    lst->clear();
    QTreeWidgetItem *cur_it = nullptr;
    int bad = 0, warned = 0;
    const QBrush red(QColor::fromString(theme::ERROR_)), amber(QColor::fromString(theme::WARN)),
        dim(QColor::fromString(theme::INK_DIM));
    std::vector<std::pair<QString, const script::Node *>> rows = _goals;
    if (_order != nullptr)
        rows.emplace_back(ORDER, _order);
    const QString text = _text.value_or(QString());
    for (const auto &[key, n] : rows) {
        const bool is_order = key == ORDER;
        QString dem, given, words;
        const QStringList dm = is_order ? QStringList() : demands(*n);
        if (is_order) {
            words = u"peace order"_s;
        } else {
            QStringList parts;
            for (const QString &k : dm)
                parts << DEMANDS.get(k)->first;
            dem = parts.join(u", "_s);
            const auto *gv = _given.get(key);
            given = QString::number(gv ? gv->size() : 0);
            words = dem;
        }
        const QString nmk = nm(key);
        if (!q.isEmpty() && !py::lower(u"%1 %2 %3"_s.arg(key, nmk, words)).contains(q))
            continue;
        auto *it = new modpanel::SortItem(QStringList{nmk, key, dem, given});
        it->setData(0, Qt::UserRole, key);
        it->setData(0, Qt::UserRole + 1, tup(is_order, py::lower(nmk)));
        it->setData(1, Qt::UserRole + 1, tup(is_order, py::lower(key)));
        it->setData(2, Qt::UserRole + 1, tup(is_order, dem));
        {
            const auto *gv = _given.get(key);
            it->setData(3, Qt::UserRole + 1, is_order ? -1LL : qlonglong(gv ? gv->size() : 0));
        }
        it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        it->setForeground(1, dim);
        it->setForeground(2, dim);
        QStringList probs;
        bool err = false;
        if (const auto *ps = _problems.get(key))
            for (const auto &[sev, t] : *ps) {
                if (sev != u"text")
                    probs << t;
                if (sev == u"error")
                    err = true;
            }
        QString tip = u"%1 — %2 line %3"_s.arg(key, REL, QString::number(refs::line_of(text, n->kstart)));
        if (!probs.isEmpty()) {                  // red: an error; amber: warnings only
            bad += err ? 1 : 0;
            warned += err ? 0 : 1;
            it->setForeground(0, err ? red : amber);
            it->setForeground(1, err ? red : amber);
            tip += u"\n\n⚠ "_s + probs.mid(0, 6).join(u"\n⚠ "_s) + (probs.size() > 6 ? u"\n…"_s : QString());
        }
        if (is_order) {
            QFont f(lst->font());
            f.setItalic(true);
            it->setFont(0, f);
        }
        it->setToolTip(0, tip);
        QStringList full;
        for (const QString &k : dm)
            full << DEMANDS.get(k)->second;
        it->setToolTip(2, !is_order ? full.join(u", "_s) : QString());
        lst->addTopLevelItem(it);
        if (cur && key == *cur)
            cur_it = it;
    }
    lst->setSortingEnabled(true);
    if (cur_it != nullptr) {
        lst->setCurrentItem(cur_it);
        lst->scrollToItem(cur_it);
    }
    lst->blockSignals(false);
    count->setText(u"%1 war goals in %2%3%4 — a click opens one in the editor, a double click in a tab of its own"_s.arg(
        QString::number(_goals.size()), REL, bad ? u" · %1 with errors (red)"_s.arg(bad) : QString(),
        warned ? u" · %1 with warnings (amber)"_s.arg(warned) : QString()));
}

void WarGoalView::_picked(QTreeWidgetItem *c, QTreeWidgetItem *)
{
    if (c == nullptr)
        return;
    cur = c->data(0, Qt::UserRole).toString();
    _armed = false;
    _fill_detail();
    Q_EMIT goalPicked(*cur, false);
}

void WarGoalView::_double(QTreeWidgetItem *it)
{
    const QString key = it != nullptr ? it->data(0, Qt::UserRole).toString() : QString();
    if (!key.isEmpty())
        Q_EMIT goalPicked(key, true);
}

void WarGoalView::mark(const QString &key)
{
    // The editor's cursor is in this war goal: the list and what is read out follow (nothing is opened).
    if ((cur && key == *cur) || !exists(key))
        return;
    cur = key;
    _armed = false;
    lst->blockSignals(true);
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = lst->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == key) {
            lst->setCurrentItem(it);
            lst->scrollToItem(it);
            break;
        }
    }
    lst->blockSignals(false);
    _fill_detail();
}

void WarGoalView::select(const QString &key)
{
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    cur = key;
    _armed = false;
    _fill_list();
    _fill_detail();
}

// ------------------------------------------------------------------ the one picked
OptStr WarGoalView::_name_key(const QString &fid, const QString &key, const OptStr &war) const
{
    if (fid == u"normal" || fid == u"agression") {
        if (!war || war->isEmpty())
            return std::nullopt;
        return (fid == u"normal" ? u"NORMAL_"_s : u"AGRESSION_"_s) + *war;
    }
    static const OrderedMap<QString, QString> SUFFIX = {
        {u"name"_s, QString()}, {u"short"_s, u"_short"_s}, {u"setup"_s, u"_setup"_s}, {u"desc"_s, u"_desc"_s}};
    return key + *SUFFIX.get(fid);
}

void WarGoalView::_fill_detail()
{
    const QString key = cur.value_or(QString());
    const script::Node *n = !key.isEmpty() ? node(key) : nullptr;
    names_host->setVisible(n != nullptr && key != ORDER);
    del_btn->setVisible(n != nullptr && key != ORDER);
    del_btn->setText(u"✕ Remove this war goal"_s);
    if (n == nullptr) {
        head->setText(QString());
        head->setStyleSheet(QString());
        head->hide();
        pic->hide();
        problems->hide();
        title->setText(u"Pick a war goal"_s);
        meta->setText(QString());
        body->setText(QString());
        return;
    }
    const QString text = _text.value_or(QString());
    head->show();
    head->setTextFormat(Qt::PlainText);
    head->setText(key != ORDER ? u"Current war goal: %1"_s.arg(nm(key)) : u"The order in a peace treaty"_s);
    const auto [r0, g0, b0] = refs::KINDS.get(u"cb"_s)->second;
    head->setStyleSheet(u"QLabel { background: rgb(%1,%2,%3); color: #14100a; font-weight: 700; font-size: 15px; "
                        "padding: 6px 10px; border-radius: 4px; }"_s.arg(r0).arg(g0).arg(b0));
    title->setText(nm(key));
    meta->setText(u"%1 · %2 · line %3"_s.arg(key, REL, QString::number(refs::line_of(text, n->kstart))));
    const std::vector<std::pair<QString, QString>> probs = _problems.get(key) ? *_problems.get(key)
                                                                              : std::vector<std::pair<QString, QString>>();
    const std::vector<std::optional<qsizetype>> spots = _where.get(key) ? *_where.get(key)
                                                                        : std::vector<std::optional<qsizetype>>();
    std::vector<std::pair<QString, std::optional<qsizetype>>> hard;
    bool any_err = false;
    for (size_t i = 0; i < probs.size(); ++i) {
        if (probs[i].first == u"error")
            any_err = true;
        if (probs[i].first != u"text")
            hard.emplace_back(probs[i].second, i < spots.size() ? spots[i] : std::nullopt);
    }
    problems->setVisible(!hard.empty());
    if (!hard.empty()) {
        const QString col = any_err ? theme::ERROR_ : theme::WARN;
        problems->setStyleSheet(BOX_CSS.arg(col, col));
        QString h = u"<b>⚠ The check found:</b>"_s;
        for (size_t i = 0; i < hard.size() && i < 12; ++i) {
            const auto &[t, p] = hard[i];
            h += u"<div>• %1%2</div>"_s.arg(
                esc(t), p ? u" &nbsp;<a href=\"go:%1|%2\" style=\"color:%3\">▸ go there</a>"_s.arg(
                                REL, QString::number(*p), theme::GOLD)
                          : QString());
        }
        if (hard.size() > 12)
            h += u"<div>… and %1 more (Errors tab)</div>"_s.arg(hard.size() - 12);
        problems->setText(h);
    }
    if (key == ORDER) {
        pic->hide();
        _fill_order(*n);
        return;
    }
    OrderedMap<QString, QString> vals;
    for (const script::Node &c : n->kids)
        if (c.has_key && !c.key.isEmpty() && !c.is_block && !vals.contains(c.key))
            vals.insert(c.key, c.value);
    const auto val = [&vals](const QString &k) -> OptStr {
        const QString *v = vals.get(k);
        return v ? OptStr(*v) : std::nullopt;
    };
    _fill_picture(val(u"sprite_index"_s));
    const OptStr war = val(u"war_name"_s);
    _name_keys.clear();
    for (const auto &[fid, le] : name_edits) {
        const auto &[lb, ed] = le;
        const OptStr k = _name_key(fid, key, war);
        _name_keys.insert(fid, k);
        lb->setVisible(bool(k));
        ed->setVisible(bool(k));
        if (!k)
            continue;
        if (!ed->hasFocus())
            ed->setText(loc(*k).value_or(QString()));
        ed->setPlaceholderText(u"No text for %1 — the game shows the bare key; type one to add it"_s.arg(*k));
    }
    QStringList parts;
    const QStringList dem = demands(*n);
    QString dh;
    for (const QString &k : dem)
        dh += u"<div>• %1 <span style='color:#8a8272'>· %2 = yes</span></div>"_s.arg(esc(DEMANDS.get(k)->second), k);
    parts << u"<h4>What it demands in the peace</h4>"_s +
                 (!dh.isEmpty() ? dh : u"<div style='color:#e0a050'>nothing — no po_… line is set to yes</div>"_s);
    QString rows;
    for (const script::Node &c : n->kids)
        if (c.has_key && !c.key.isEmpty() && !c.is_block && !cbcheck::PO.contains(c.key) && NICE.contains(c.key))
            rows += u"<tr><td style='padding-right:14px'>%1</td><td><b>%2</b></td>"
                    "<td style='color:#8a8272; padding-left:14px'>%3</td></tr>"_s.arg(esc(*NICE.get(c.key)),
                                                                                       esc(c.value), esc(c.key));
    if (!rows.isEmpty())
        parts << u"<h4>Its numbers</h4><table cellspacing='0' cellpadding='1'>%1</table>"_s.arg(rows);
    for (const script::Node &c : n->kids) {      // its parts, in the file's order, each under its heading
        if (c.has_key && PARTS.contains(c.key) && c.is_block) {
            const auto &[ptitle, how] = *PARTS.get(c.key);
            const auto &fn = how == u"cond" ? conds_html : effects_html;
            QString inner;
            try {
                inner = fn ? fn(c.kids) : QString();
            } catch (...) {
                inner = u"<div style='color:#8a8272'>(see the text)</div>"_s;
            }
            parts << u"<h4>%1 <span style='color:#8a8272; font-weight:400'>· %2 = { … } · line %3</span></h4>%4"_s.arg(
                esc(ptitle), c.key, QString::number(refs::line_of(text, c.kstart)), inner);
        }
    }
    const auto *given = _given.get(key);
    if (given && !given->empty()) {
        std::set<std::pair<QString, QString>> seen;
        QStringList grows;
        for (const auto &[rel, pos, role] : *given) {
            const QString name = refs->place_name(rel, pos);
            if (seen.count({name, role}))
                continue;
            seen.insert({name, role});
            grows << u"<div><a href=\"go:%1|%2\" style=\"color:%3\">%4</a> — %5</div>"_s.arg(
                esc(rel), QString::number(pos), theme::GOLD, esc(name),
                role == u"add" ? u"gives it"_s : u"starts a war with it"_s);
        }
        parts << u"<h4>Given by</h4>"_s + grows.mid(0, 30).join(QString()) +
                     (grows.size() > 30 ? u"<div>…</div>"_s : QString());
    } else {
        const bool trig = py::lower(val(u"is_triggered_only"_s).value_or(u"no"_s)) == u"yes";
        const bool make = py::lower(val(u"constructing_cb"_s).value_or(u"yes"_s)) != u"no";
        parts << u"<h4>Given by</h4><div style='color:#8a8272'>no event or decision names it%1</div>"_s.arg(
            make   ? u" — it can be justified by a nation that may use it"_s
            : trig ? u" — and it can't be justified (constructing_cb = no): only the game itself can add it"_s
                   : QString());
    }
    qsizetype soft = 0;
    for (const auto &[sev, t] : probs)
        if (sev == u"text")
            ++soft;
    if (soft)
        parts << u"<div style='color:#e0a050; margin-top:8px'>%1 of its texts %2 missing (the empty boxes above).</div>"_s
                     .arg(QString::number(soft), soft == 1 ? u"is"_s : u"are"_s);
    body->setText(parts.join(QString()));
}

void WarGoalView::_fill_order(const script::Node &n)
{
    QStringList names;
    for (const script::Node &c : n.kids)
        if (!c.has_key && !c.is_block)
            names << c.value;
    QSet<QString> have;
    for (const auto &[k, _n] : _goals)
        have.insert(k);
    QString rows;
    int i = 0;
    for (const QString &v : names) {
        ++i;
        const bool ok = have.contains(v);
        const bool game = cbcheck::GAME_ORDER_ONLY.contains(v);
        rows += u"<div><span style='color:#8a8272'>%1.</span> %2 <span style='color:%3'>· %4%5</span></div>"_s.arg(
            QString::number(i), ok ? esc(nm(v)) : esc(v), ok || game ? u"#8a8272"_s : u"#ff786e"_s, esc(v),
            ok     ? QString()
            : game ? u" (the game's own, no war goal of this name)"_s
                   : u" — no war goal of this name"_s);
    }
    body->setText(u"<p>The order in which the war goals of a peace are carried out (peace_order at the top of %1) — "
                  "every war goal is listed here once.</p>%2"_s.arg(REL, rows));
}

void WarGoalView::_fill_picture(const OptStr &idx)
{
    std::optional<QPixmap> pm;
    try {
        if (idx && py::isdigit(*idx) && _frames()) {
            const auto fr = iconstrips::frames(iconstrips::strips_for(mod, *doc), STRIP, 44);
            const long long i = py::to_int(*idx) - 1;
            if (0 <= i && i < (long long)fr.size())
                pm = fr[size_t(i)];
        }
    } catch (...) {
        pm.reset();
    }
    pic->setVisible(bool(pm));
    if (pm) {
        pic->setPixmap(*pm);
        pic->setToolTip(
            u"Picture %1 of the war goal strip (sprite_index) — the strip: Library → Icon strips"_s.arg(*idx));
    }
}

// ------------------------------------------------------------------ changes
void WarGoalView::_loc_done(QLineEdit *ed, const QString &fid)
{
    const OptStr *k = _name_keys.get(fid);
    const QString text = py::split(ed->text()).join(u' ');
    if (k && *k && !(*k)->isEmpty() && !text.isEmpty() && text != loc(**k).value_or(QString()))
        Q_EMIT nameEdit(**k, text);
}

void WarGoalView::_new_open()
{
    const bool src = cur && !cur->isEmpty() && *cur != ORDER && exists(*cur);
    new_note->setText(src ? u"A copy of “%1” — change what differs in the editor."_s.arg(nm(*cur))
                          : u"A plain war goal with every line the game's own have (pick one in the list first to copy "
                            "that one instead)."_s);
    new_row->setVisible(true);
    new_edit->clear();
    new_edit->setFocus();
}

void WarGoalView::_new_done()
{
    const QString key = py::strip(new_edit->text());
    if (!modifiers::name_ok(key)) {
        new_warn->setText(u"A key is letters, digits and _ (no spaces), not starting with a digit."_s);
        return;
    }
    if (exists(key) || key == ORDER || cbcheck::KNOWN.contains(key)) {
        new_warn->setText(u"“%1” is taken already."_s.arg(key));
        return;
    }
    const QString text = store->text(REL);
    const script::Node *src = cur && !cur->isEmpty() && *cur != ORDER ? node(*cur) : nullptr;
    QString out;
    if (src != nullptr) {
        const QString block = key + py::slice(text, src->kstart + src->key.size(), src->vend) + u"\n"_s;
        const qsizetype pos = src->vend;
        out = py::head(text, pos) + u"\n\n"_s + rstrip_nl(block) + py::tail(text, pos);
    } else {
        out = rstrip_nl(text) + u"\n\n"_s + TEMPLATE.arg(key);
    }
    if (_order != nullptr)
        out = cbcheck::fix_order(out, {{u"add"_s, key}});
    new_row->setVisible(false);
    cur = key;
    Q_EMIT scriptEdit(u"New war goal %1"_s.arg(key), REL, out);
    _read();
    _fill_list();
    _fill_detail();
    if (exists(key))
        Q_EMIT goalPicked(key, false);
}

void WarGoalView::_remove()
{
    const QString key = cur.value_or(QString());
    const script::Node *n = !key.isEmpty() && key != ORDER ? node(key) : nullptr;
    if (n == nullptr)
        return;
    if (!_armed) {
        _armed = true;
        const auto *g = _given.get(key);
        const qsizetype used = g ? qsizetype(g->size()) : 0;
        del_btn->setText(u"✕ Click again to remove “%1”%2"_s.arg(
            nm(key), used ? u" — %1 script line%2 still name%3 it"_s.arg(QString::number(used),
                                                                        used == 1 ? QString() : u"s"_s,
                                                                        used == 1 ? u"s"_s : QString())
                          : QString()));
        return;
    }
    _armed = false;
    const QString text = store->text(REL);
    qsizetype a = n->kstart, b = n->vend;
    const qsizetype la = py::rfind(text, u'\n', a) + 1;
    qsizetype lb = text.indexOf(u'\n', b);
    lb = lb < 0 ? text.size() : lb + 1;
    if (py::strip(py::slice(text, la, a)).isEmpty() && py::strip(py::slice(text, b, lb)).isEmpty()) {
        a = la;
        b = lb;
        while (py::slice(text, b, b + 1) == u"\n" && py::slice(text, std::max<qsizetype>(0, a - 2), a) == u"\n\n")
            b += 1;                              // one empty line stays
    }
    const QString out = cbcheck::fix_order(py::head(text, a) + py::tail(text, b), {{u"drop"_s, key}});
    const QString name = nm(key);
    OptStr war;
    for (const script::Node &c : n->kids)
        if (c.has_key && c.key == u"war_name" && !c.is_block) {
            war = c.value;
            break;
        }
    QStringList drop;
    for (const auto &kv : name_edits) {
        const OptStr k = _name_key(kv.first, key, war);
        if (k && !k->isEmpty() && f_own(*k, key))
            drop << *k;
    }
    store->drop_names = drop;
    cur.reset();
    Q_EMIT scriptEdit(u"War goal “%1” removed"_s.arg(name), REL, out);
    _read();
    _fill_list();
    _fill_detail();
}

}  // namespace cbpanel
