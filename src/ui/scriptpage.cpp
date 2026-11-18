// scriptpage.py — line by line (see scriptpage.h).
#include "ui/scriptpage.h"
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/events.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "core/scriptstore.h"
#include "ui/colsizes.h"
#include "ui/eventpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace scriptpage {

using script::Node;
using script::Nodes;
using refs::esc;

static const QString COLOR_RE = u"§.|§"_s;
const QStringList EFFECT_WORDS = {u"effect"_s, u"on_add"_s, u"on_po_accepted"_s};

static Spec make_rebels()
{
    Spec s;
    s.id = u"rebels"_s;
    s.title = u"Rebels"_s;
    s.noun = u"rebel type"_s;
    s.rel = u"common/rebel_types.txt"_s;
    s.intro = u"The kinds of rebels (common/rebel_types.txt): who joins them, how likely they rise, what they do to a "
              u"province they hold and what happens when they win. Written like an event — the editor in the middle "
              u"knows every word."_s;
    s.names = {{u"_title"_s, u"Name in the game"_s}, {u"_name"_s, u"Name of its armies"_s},
               {u"_desc"_s, u"Description"_s}, {u"_army"_s, u"Name of one army"_s}};
    s.parts.insert(u"will_rise"_s, u"How ready pops are to join it"_s);
    s.parts.insert(u"spawn_chance"_s, u"How likely it rises (each month)"_s);
    s.parts.insert(u"movement_evaluation"_s, u"Where its armies go"_s);
    s.parts.insert(u"siege_won_trigger"_s, u"A province it has taken counts when"_s);
    s.parts.insert(u"siege_won_effect"_s, u"When it has taken a province"_s);
    s.parts.insert(u"demands_met_trigger"_s, u"Its demands count as met when"_s);
    s.parts.insert(u"demands_met_effect"_s, u"When its demands are met"_s);
    s.parts.insert(u"demands_enforced_trigger"_s, u"It has won when"_s);
    s.parts.insert(u"demands_enforced_effect"_s, u"When it wins"_s);
    s.parts.insert(u"government"_s, u"The government it brings (by the one there is)"_s);
    return s;
}

static Spec make_pop_types()
{
    Spec s;
    s.id = u"poptypes"_s;
    s.title = u"Pop types"_s;
    s.noun = u"pop type"_s;
    s.folder = u"poptypes"_s;
    s.intro = u"How each kind of pop behaves (poptypes — one file each): what it turns into and how likely, where it "
              u"moves, what it votes for and believes. Written like an event — the editor in the middle knows every "
              u"word. Its needs are on the Economy page."_s;
    s.names = {{u""_s, u"Name in the game"_s}};
    s.parts.insert(u"promote_to"_s, u"What it turns into (the weight of each)"_s);
    s.parts.insert(u"migration_target"_s, u"Which province it moves to inside the nation"_s);
    s.parts.insert(u"country_migration_target"_s, u"Which nation it emigrates to"_s);
    s.parts.insert(u"issues"_s, u"What it wants (party issues and reforms)"_s);
    s.parts.insert(u"ideologies"_s, u"What it believes"_s);
    s.parts.insert(u"rebel"_s, u"The units of its rebels"_s);
    s.parts.insert(u"life_needs"_s, u"Life needs"_s);
    s.parts.insert(u"everyday_needs"_s, u"Everyday needs"_s);
    s.parts.insert(u"luxury_needs"_s, u"Luxury needs"_s);
    return s;
}

const Spec REBELS = make_rebels();
const Spec POP_TYPES = make_pop_types();

static bool keyed(const Node &n) { return n.has_key && !n.key.isEmpty(); }   // (Python: `if c.key`)
static QString under(const QString &k) { return QString(k).replace(u'_', u' '); }

ScriptListPage::ScriptListPage(moddata::Mod &mod, const Spec &spec, eventpanel::EventPanel *hub, QWidget *parent)
    : QWidget(parent), mod(mod), spec(spec), hub(hub)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    // --- the list
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    auto *s = new widgets::Section();
    s->lay->addWidget(widgets::label(spec.title, u"title"_s));
    s->lay->addWidget(widgets::label(spec.intro, u"faint"_s));
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a %1 (name or key)…"_s.arg(spec.noun));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill_list(); });
    s->lay->addWidget(search);
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderLabels({py::capitalize(spec.noun), u"Key in the files"_s});
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::SingleSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"scriptpage_"_s + spec.id, {std::nullopt, 170}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *c, QTreeWidgetItem *p) { _picked(c, p); });
    s->lay->addWidget(lst, 1);
    ll->addWidget(s, 1);
    split->addWidget(left);
    // --- the editor: the same as the Scripts page's
    ws = new scriptedit::ScriptWorkspace(nullptr, true);
    ws->empty->setText(u"Pick a %1 on the left — it opens here.\n\nHover a word to see what it is · Ctrl+click: "
                       u"go where it is defined · Ctrl+F: search"_s.arg(spec.noun));
    connect(ws, &scriptedit::ScriptWorkspace::outside, this, &ScriptListPage::outside);
    connect(ws, &scriptedit::ScriptWorkspace::placeChanged, this,
            [this](const QString &rel, long long pos) { _place_changed(rel, pos); });
    split->addWidget(ws);
    // --- the one picked, read out
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 6);
    split->setStretchFactor(2, 4);
    split->setSizes({380, 820, 560});
    s = new widgets::Section();
    head = new QLabel();
    head->setWordWrap(true);
    head->setTextFormat(Qt::PlainText);
    s->lay->addWidget(head);
    title = widgets::label(u"Pick a %1"_s.arg(spec.noun), u"title"_s);
    s->lay->addWidget(title);
    meta = widgets::label(u""_s, u"faint"_s);
    s->lay->addWidget(meta);
    prob = new QLabel();
    prob->setWordWrap(true);
    prob->setTextFormat(Qt::RichText);
    prob->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(prob, &QLabel::linkActivated, this, [this](const QString &h) { _link(h); });
    prob->hide();
    s->lay->addWidget(prob);
    jump = new QPushButton(u"▸ Jump to the error"_s);
    jump->setCursor(Qt::PointingHandCursor);
    connect(jump, &QPushButton::clicked, this, [this] { _jump_problem(); });
    jump->hide();
    s->lay->addWidget(jump);
    for (const auto &[sfx, text] : spec.names) {
        auto *row = new QHBoxLayout();
        row->addWidget(widgets::label(text, u"sub"_s, false));
        auto *ed = new QLineEdit();
        connect(ed, &QLineEdit::editingFinished, this, [this, ed, x = sfx] { _loc_done(ed, x); });
        row->addWidget(ed, 1);
        auto *w = new QWidget();
        w->setLayout(row);
        row->setContentsMargins(0, 0, 0, 0);
        w->hide();
        s->lay->addWidget(w);
        name_rows.push_back({sfx, ed, w});
    }
    body = new QLabel();
    body->setObjectName(u"sub"_s);
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(body, &QLabel::linkActivated, this, [this](const QString &h) { _link(h); });
    s->lay->addWidget(body);
    rl->addWidget(s);
    rl->addStretch(1);
    hub->attach_extra(ws);
    connect(hub, &eventpanel::EventPanel::changed, this, [this] { _hub_changed(); });
}

const Nodes &ScriptListPage::_nodes(const QString &rel, const QString &text)
{
    auto it = _parsed.find(rel);
    if (it == _parsed.end() || it->second.first != text)
        it = _parsed.insert_or_assign(rel, std::make_pair(text, script::parse(text))).first;
    return it->second.second;
}

void ScriptListPage::_hub_changed()
{
    // The scripts were read again (something was typed): the list and what is read out follow.
    if (store && isVisible()) {
        _fill_list();
        _fill_detail();
    }
}

// ------------------------------------------------------------------ data
void ScriptListPage::refresh(document::Document *d)
{
    doc = d;
    hub->ensure(d);                              // the scripts are read (once) — the words' meanings come from there
    store = d && d->maps.contains(u"scripts"_s) ? &d->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    _fill_list();
    _fill_detail();
}

QStringList ScriptListPage::rels()
{
    if (!spec.rel.isEmpty())
        return {spec.rel};
    return entryview::_files(mod, store, spec.folder);
}

OptStr ScriptListPage::loc(const QString &key)
{
    if (doc && doc->maps.contains(u"loc"_s)) {
        const auto &lc = doc->map<localisation::LocEdits>(u"loc"_s);
        if (const auto *x = lc.fresh.get(key))
            return x->first;
    }
    return refs::loc_text(mod, key);
}

QString ScriptListPage::nm(const QString &key)
{
    for (const auto &[sfx, _t] : spec.names) {
        const OptStr t = loc(key + sfx);
        if (t && !t->isEmpty())
            return py::strip(py::re_sub(COLOR_RE, *t, [](const QRegularExpressionMatch &) { return QString(); }));
    }
    return py::capitalize(under(key));
}

void ScriptListPage::_read()
{
    std::vector<Item> out;
    for (const QString &rel : rels()) {
        QString text;
        try {
            text = store->text(rel);
        } catch (const std::exception &) {      // (Python: OSError)
            continue;
        }
        if (!spec.rel.isEmpty()) {
            for (const Node &n : _nodes(rel, text))
                if (keyed(n) && n.is_block)
                    out.push_back({n.key, rel, n.kstart, n.vend, &n});
        } else {
            out.push_back({pypath::splitext_stem(pypath::basename(rel)), rel, 0, text.size(), nullptr});
        }
    }
    _entries = std::move(out);
}

void ScriptListPage::set_problems(const scriptedit::ProblemsByRel &by_rel)
{
    // {file: [(position or None, severity, text)]} — what the checks found in the scripts.
    const QString folder = spec.folder.isEmpty() ? u"\0"_s : spec.folder;
    scriptedit::ProblemsByRel mine;
    for (const auto &[r, v] : by_rel)
        if ((!spec.rel.isEmpty() && r == spec.rel) || r.startsWith(folder + u'/'))
            mine[r] = v;
    if (mine != _problems) {
        _problems = mine;
        if (store && isVisible()) {
            _fill_list();
            _fill_detail();
        }
    }
}

std::vector<scriptedit::Problem> ScriptListPage::_probs(const QString &rel, qsizetype a, qsizetype b, bool whole) const
{
    std::vector<scriptedit::Problem> out;
    const auto it = _problems.find(rel);
    if (it != _problems.end())
        for (const auto &x : it->second)
            if (whole || (x.pos && a <= *x.pos && *x.pos < b))
                out.push_back(x);
    return out;
}

// ------------------------------------------------------------------ the list
void ScriptListPage::_fill_list()
{
    if (!store)
        return;
    _read();
    const QString q = py::lower(py::strip(search->text()));
    lst->blockSignals(true);
    lst->clear();
    QTreeWidgetItem *pick = nullptr;
    const QBrush dim(QColor::fromString(theme::INK_DIM));
    for (const Item &e : _entries) {
        if (!q.isEmpty() && !py::lower(u"%1 %2"_s.arg(e.key, nm(e.key))).contains(q))
            continue;
        auto *it = new QTreeWidgetItem(QStringList{nm(e.key), e.key});
        it->setData(0, Qt::UserRole, QStringList{e.rel, e.key});
        it->setForeground(1, dim);
        QString tip = u"%1 — %2"_s.arg(e.key, e.rel);
        const auto probs = _probs(e.rel, e.a, e.b, e.node == nullptr);
        if (!probs.empty()) {
            const bool err = std::any_of(probs.begin(), probs.end(), [](const auto &p) { return p.sev == u"error"; });
            const QBrush br(QColor::fromString(err ? theme::ERROR_ : theme::WARN));
            it->setForeground(0, br);
            it->setForeground(1, br);
            it->setText(0, u"⚠ "_s + it->text(0));
            QStringList ts;
            for (size_t i = 0; i < probs.size() && i < 6; ++i)
                ts << probs[i].text;
            tip += u"\n\n⚠ "_s + ts.join(u"\n⚠ "_s);
        }
        it->setToolTip(0, tip);
        lst->addTopLevelItem(it);
        if (cur && *cur == std::make_pair(e.rel, e.key))
            pick = it;
    }
    if (pick) {
        lst->setCurrentItem(pick);
        lst->scrollToItem(pick);
    }
    lst->blockSignals(false);
}

const ScriptListPage::Item *ScriptListPage::_entry() const
{
    if (!cur)
        return nullptr;
    for (const Item &e : _entries)
        if (e.rel == cur->first && e.key == cur->second)
            return &e;
    return nullptr;
}

void ScriptListPage::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QStringList d = it ? it->data(0, Qt::UserRole).toStringList() : QStringList();
    if (d.size() < 2)
        return;
    cur = std::make_pair(d[0], d[1]);
    _prob_i = -1;
    if (const Item *e = _entry())
        ws->open(e->rel, e->a, false, true,
                 e->node ? std::optional<std::pair<qsizetype, qsizetype>>({e->a, e->b}) : std::nullopt);
    _fill_detail();
}

void ScriptListPage::select(const QString &rel, const OptStr &key, std::optional<qsizetype> pos)
{
    // Show a thing of this page (the Errors tab's way in): by its key, or by a place in its file.
    if (!store)
        return;
    _read();
    const Item *e = nullptr;
    for (const Item &x : _entries)
        if (x.rel == rel && ((key && x.key == *key) || (pos && x.a <= *pos && *pos < x.b) || (!key && !pos))) {
            e = &x;
            break;
        }
    if (!e)
        return;
    cur = std::make_pair(e->rel, e->key);
    const Item it = *e;                          // (_fill_list reads the entries again)
    _fill_list();
    ws->open(rel, pos ? *pos : it.a, false, true,
             it.node && !pos ? std::optional<std::pair<qsizetype, qsizetype>>({it.a, it.b}) : std::nullopt);
    _fill_detail();
}

void ScriptListPage::_place_changed(const QString &rel, long long pos)
{
    const Item *e = nullptr;
    for (const Item &x : _entries)
        if (x.rel == rel && x.a <= pos && pos < x.b) {
            e = &x;
            break;
        }
    if (!e || (cur && *cur == std::make_pair(e->rel, e->key)))
        return;
    cur = std::make_pair(e->rel, e->key);
    lst->blockSignals(true);
    for (int i = 0; i < lst->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = lst->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toStringList() == QStringList{cur->first, cur->second}) {
            lst->setCurrentItem(it);
            lst->scrollToItem(it);
            break;
        }
    }
    lst->blockSignals(false);
    _fill_detail();
}

// ------------------------------------------------------------------ the one picked
void ScriptListPage::_link(const QString &href)
{
    if (href.startsWith(u"go:"_s)) {
        const QString rest = href.mid(3);
        const qsizetype bar = rest.lastIndexOf(u'|');
        const QString rel = rest.left(bar);
        const qsizetype pos = rest.mid(bar + 1).toLongLong();
        if (rels().contains(rel)) {
            const QString t = store->text(rel);
            const qsizetype end = t.indexOf(u'\n', pos);
            ws->open(rel, pos, false, true,
                     std::make_pair(py::rfind(t, u'\n', pos) + 1, end < 0 ? t.size() : end));
        } else {
            Q_EMIT outside(rel, pos);
        }
    } else {
        hub->_link(href);                        // an event, a decision, a modifier: the Scripts page shows it
    }
}

void ScriptListPage::_jump_problem()
{
    const Item *e = _entry();
    if (!e)
        return;
    const auto l = _probs(e->rel, e->a, e->b, e->node == nullptr);
    if (l.empty())
        return;
    _prob_i = (_prob_i + 1) % int(l.size());
    const auto &pos = l[size_t(_prob_i)].pos;
    _link(u"go:%1|%2"_s.arg(e->rel, QString::number(pos ? *pos : store->text(e->rel).size())));
}

std::optional<QString> ScriptListPage::_weights(const QString &text, const Nodes &nodes, int depth)
{
    // A weights block (a base `factor`, then `modifier = { factor = … conditions }` lines) in plain words.
    QStringList rows;
    OptStr base;
    for (const Node &c : nodes)
        if (c.has_key && (c.key == u"factor" || c.key == u"base") && !c.is_block) {
            base = c.value;
            break;
        }
    if (base)
        rows << u"<div style='margin-left:%1px'>Base: <b>%2</b></div>"_s.arg(QString::number(14 * depth), esc(*base));
    for (const Node &c : nodes) {
        if (c.has_key && c.key == u"modifier" && c.is_block) {
            QString f = u"?"_s;
            for (const Node &x : c.kids)
                if (x.has_key && x.key == u"factor" && !x.is_block) {
                    f = x.value;
                    break;
                }
            Nodes cond;
            for (const Node &x : c.kids)
                if (!(x.has_key && x.key == u"factor"))
                    cond.push_back(x);
            rows << u"<div style='margin-left:%1px; margin-top:3px'><b>×%2</b> when:</div>%3"_s.arg(
                QString::number(14 * depth), esc(f), _shift(hub->_conds_html(events::View(cond), std::nullopt), depth + 1));
        } else if (c.has_key && c.key == u"group" && c.is_block) {
            rows << u"<div style='margin-left:%1px; margin-top:3px; color:#8a8272'>one of this group "
                    u"(the first that fits):</div>%2"_s.arg(QString::number(14 * depth),
                                                            _weights(text, c.kids, depth + 1).value_or(QString()));
        }
    }
    bool other = false;
    for (const Node &c : nodes)
        if (keyed(c) && c.key != u"factor" && c.key != u"base" && c.key != u"modifier" && c.key != u"group") {
            other = true;
            break;
        }
    if (other && !base && rows.isEmpty())
        return std::nullopt;
    return rows.join(QString());
}

QString ScriptListPage::_shift(const QString &htm, int depth)
{
    return u"<div style='margin-left:%1px'>%2</div>"_s.arg(QString::number(14 * depth), htm);
}

QString ScriptListPage::_part(const QString &text, const Node &c)
{
    // One part of the thing (a block) read out.
    std::vector<const Node *> kids;
    for (const Node &x : c.kids)
        if (keyed(x))
            kids.push_back(&x);
    for (const QString &w : EFFECT_WORDS)
        if (c.key.contains(w))
            return hub->_effects_html(events::View(c.kids));
    auto weighty = [](const Node &x) { return x.key == u"factor" || x.key == u"modifier" || x.key == u"group"; };
    if (std::any_of(kids.begin(), kids.end(), [&](const Node *x) { return weighty(*x); })) {
        if (const auto got = _weights(text, c.kids))
            return *got;
    }
    if (!kids.empty() && std::all_of(kids.begin(), kids.end(), [](const Node *x) { return x->is_block; }) &&
        std::any_of(kids.begin(), kids.end(), [&](const Node *x) {
            return std::any_of(x->kids.begin(), x->kids.end(), [&](const Node &y) { return y.has_key && weighty(y); });
        })) {
        QStringList rows;                        // one weights block per target (a pop type, an issue, an ideology)
        for (const Node *x : kids) {
            const OptStr l = loc(x->key);
            rows << u"<div style='margin-top:5px'><b>%1</b> <span style='color:#8a8272'>· %2 · line %3"
                    u"</span></div>%4"_s.arg(esc(l && !l->isEmpty() ? *l : under(x->key)), esc(x->key),
                                             QString::number(refs::line_of(text, x->kstart)),
                                             _weights(text, x->kids, 1).value_or(QString()));
        }
        return rows.join(QString());
    }
    if (!kids.empty() && std::none_of(kids.begin(), kids.end(), [](const Node *x) { return x->is_block; }) &&
        std::all_of(kids.begin(), kids.end(), [](const Node *x) {
            return py::re_match(u"^-?[\\d.]+$"_s, x->value).hasMatch();
        })) {
        QStringList rows;
        for (const Node *x : kids) {
            const OptStr l = loc(x->key);
            rows << u"<div>%1: <b>%2</b></div>"_s.arg(esc(l && !l->isEmpty() ? *l : under(x->key)), esc(x->value));
        }
        return rows.join(QString());
    }
    return hub->_conds_html(events::View(c.kids), std::nullopt);
}

void ScriptListPage::_fill_detail()
{
    const Item *e = store ? _entry() : nullptr;
    for (const NameRow &r : name_rows)
        r.w->setVisible(e != nullptr);
    if (!e) {
        head->setText(u""_s);
        head->setStyleSheet(u""_s);
        title->setText(u"Pick a %1"_s.arg(spec.noun));
        meta->setText(u""_s);
        body->setText(u""_s);
        prob->hide();
        jump->hide();
        return;
    }
    const Item it = *e;
    const QString text = store->text(it.rel);
    head->setText(u"Current %1: %2"_s.arg(spec.noun, nm(it.key)));
    head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; font-size: 15px; "
                        u"padding: 6px 10px; border-radius: 4px; }"_s.arg(theme::GOLD));
    title->setText(nm(it.key));
    meta->setText(u"%1 · %2%3"_s.arg(it.key, it.rel,
                                     it.node ? u" · line %1"_s.arg(refs::line_of(text, it.a)) : QString()));
    for (const NameRow &r : name_rows) {
        if (!r.ed->hasFocus())
            r.ed->setText(loc(it.key + r.sfx).value_or(QString()));
        r.ed->setPlaceholderText(u"No text for %1 — type one to add it"_s.arg(it.key + r.sfx));
    }
    const auto probs = _probs(it.rel, it.a, it.b, it.node == nullptr);
    prob->setVisible(!probs.empty());
    jump->setVisible(!probs.empty());
    if (!probs.empty()) {
        const bool err = std::any_of(probs.begin(), probs.end(), [](const auto &p) { return p.sev == u"error"; });
        const QString col = err ? theme::ERROR_ : theme::WARN;
        prob->setStyleSheet(u"QLabel { color: %1; border: 2px solid %1; border-radius: 4px; padding: 6px 8px; }"_s.arg(col));
        QString h = u"<b>⚠ The check found:</b>"_s;
        for (size_t i = 0; i < probs.size() && i < 10; ++i)
            h += u"<div style=\"margin-top:3px\">• %1 &nbsp;<a href=\"go:%2|%3\" style=\"color:%4\">▸ go there</a></div>"_s.arg(
                esc(probs[i].text), esc(it.rel), QString::number(probs[i].pos ? *probs[i].pos : text.size()), theme::GOLD);
        if (probs.size() > 10)
            h += u"<div>… and %1 more</div>"_s.arg(qsizetype(probs.size()) - 10);
        prob->setText(h);
        jump->setText(probs.size() == 1 ? u"▸ Jump to the error"_s
                                        : u"▸ Jump to the next of its %1"_s.arg(qsizetype(probs.size())));
        jump->setStyleSheet(u"QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
                            u"font-weight: 700; padding: 6px 12px; }"_s.arg(col, err ? u"#ffffff"_s : u"#14100a"_s));
    }
    const Nodes &kids = it.node ? it.node->kids : _nodes(it.rel, text);
    QStringList plain;
    for (const Node &c : kids)
        if (keyed(c) && !c.is_block && plain.size() < 60)
            plain << u"<tr><td style='padding-right:14px'>%1</td><td><b>%2</b></td></tr>"_s.arg(esc(under(c.key)),
                                                                                              esc(c.value));
    QStringList parts;
    if (!plain.isEmpty())
        parts << u"<h4>Its values</h4><table cellspacing='0' cellpadding='1'>%1</table>"_s.arg(plain.join(QString()));
    for (const Node &c : kids) {
        if (!(keyed(c) && c.is_block))
            continue;
        QString inner;
        try {
            inner = _part(text, c);
        } catch (...) {
            inner = u"<div style='color:#8a8272'>(see the text)</div>"_s;
        }
        parts << u"<h4>%1 <span style='color:#8a8272; font-weight:400'>· %2 = { … } · "
                 u"<a href=\"go:%3|%4\" style=\"color:%5\">line %6</a></span></h4>%7"_s.arg(
                     esc(spec.parts.value(c.key, py::capitalize(under(c.key)))), esc(c.key), esc(it.rel),
                     QString::number(c.kstart), theme::GOLD, QString::number(refs::line_of(text, c.kstart)), inner);
    }
    body->setText(parts.join(QString()));
}

void ScriptListPage::_loc_done(QLineEdit *ed, const QString &sfx)
{
    const Item *e = _entry();
    if (!e)
        return;
    const QString text = py::split(ed->text()).join(u' ');
    if (!text.isEmpty() && text != loc(e->key + sfx).value_or(QString()))
        Q_EMIT nameEdit(e->key + sfx, text);
}

}  // namespace scriptpage
