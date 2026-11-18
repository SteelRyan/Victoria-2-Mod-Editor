// bigedit.py — the large window of a list page's entry. Line by line.
#include "ui/bigedit.h"
#include "core/blocks.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/countryhist.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "ui/colsizes.h"
#include "ui/defpanel.h"
#include "ui/modpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
using widgets::label;
using script::Node;

namespace bigedit {

const OrderedMap<QString, std::pair<QString, QString>> PARTS = {
    {u"rebel"_s, {u"Rebel units"_s, u"The unit types its rebels field, as shares of the rebel army."_s}},
    {u"country_migration_target"_s, {u"Migration target: which country"_s,
                                     u"How much a foreign country draws this pop when it emigrates: the factor, times "
                                     "every modifier whose condition holds for that country."_s}},
    {u"migration_target"_s, {u"Migration target: which province"_s,
                             u"How much a province of its own country draws this pop when it moves: the factor, times "
                             "every modifier whose condition holds for that province."_s}},
    {u"promote_to"_s, {u"Promotes / demotes to"_s, u"The pop types it can turn into. For each: a base factor, times every "
                                                   "modifier whose condition holds. Up or down the ladder is decided by "
                                                   "the strata of the two types."_s}},
    {u"ideologies"_s, {u"Ideology leanings"_s, u"How strongly it is drawn to each ideology: the factor, times every "
                                               "modifier whose condition holds."_s}},
    {u"issues"_s, {u"Issue leanings"_s, u"How strongly it wants each party issue / reform option: the factor, times every "
                                        "modifier whose condition holds."_s}},
    {u"life_needs"_s, {u"Life needs"_s, u"Goods it must buy to live, per 200,000 people a day (set on the Economy page)."_s}},
    {u"everyday_needs"_s, {u"Everyday needs"_s, u"Goods it buys once its life needs are met."_s}},
    {u"luxury_needs"_s, {u"Luxury needs"_s, u"Goods it buys once its everyday needs are met."_s}},
    {u"life_needs_income"_s, {u"Income for life needs"_s, u"Where its money for life needs comes from."_s}},
    {u"everyday_needs_income"_s, {u"Income for everyday needs"_s, u"Where its money for everyday needs comes from."_s}},
    {u"luxury_needs_income"_s, {u"Income for luxury needs"_s, u"Where its money for luxury needs comes from."_s}},
    {u"assimilation"_s, {u"Assimilation"_s, u"How fast it takes on the culture of the nation it lives in."_s}},
    {u"conversion"_s, {u"Conversion"_s, u"How fast it takes on the state religion."_s}},
    {u"trigger"_s, {u"Condition"_s, u"It holds while this is true."_s}},
    {u"limit"_s, {u"Limit"_s, u"Only where this is true."_s}},
    {u"will_rise"_s, {u"Who joins"_s, u"How likely a pop is to join these rebels."_s}},
    {u"spawn_chance"_s, {u"Chance to form"_s, u"How likely a pop turns to these rebels."_s}},
    {u"movement_evaluation"_s, {u"Where they march"_s, u"How much a province draws the rebel army."_s}},
    {u"siege_won_trigger"_s, {u"When a siege is won: condition"_s, u""_s}},
    {u"siege_won_effect"_s, {u"When a siege is won"_s, u""_s}},
    {u"demands_enforced_trigger"_s, {u"When they win: condition"_s, u""_s}},
    {u"demands_enforced_effect"_s, {u"When they win"_s, u"What happens to the nation when the rebels enforce their demands."_s}},
    {u"can_use"_s, {u"Who may use it"_s, u"The war goal can be picked while this is true."_s}},
    {u"allowed_countries"_s, {u"Against whom"_s, u"The nations it can be used against."_s}},
    {u"allowed_states"_s, {u"Which states"_s, u"The states it can demand."_s}},
    {u"on_add"_s, {u"When it is added"_s, u"Happens when the war goal is added to a war."_s}},
    {u"on_po_accepted"_s, {u"When the peace is signed"_s, u"Happens when the demand is accepted."_s}},
};

const OrderedMap<QString, QString> GLOSSARY = {
    {u"sprite"_s, u"The picture of the pop type: its number in the pop icon strip (1 = the first). Library → Icon strips "
                  "shows them all."_s},
    {u"color"_s, u"Its colour in pie charts and on maps: red green blue, each 0–255."_s},
    {u"strata"_s, u"poor, middle or rich: which tax and which needs slider applies, and what counts as promotion "
                  "(towards rich) or demotion."_s},
    {u"is_artisan"_s, u"yes: it makes goods on its own (artisans) instead of working in a factory or on the land."_s},
    {u"can_be_recruited"_s, u"yes: brigades are raised from these pops (soldiers)."_s},
    {u"state_capital_only"_s, u"yes: it only lives in the state's capital province (the editor's “elite” pops)."_s},
    {u"unemployment"_s, u"As a value: yes = the pop type can be out of work. As a condition: the share of it that is "
                        "out of work (0–1)."_s},
    {u"allowed_to_vote"_s, u"no: it never votes, whatever the voting reform says."_s},
    {u"is_slave"_s, u"yes: slaves — no pay, no vote; freed when slavery is abolished."_s},
    {u"can_build"_s, u"yes: it can build factories and railroads (capitalists)."_s},
    {u"can_reduce_consciousness"_s, u"yes: it lowers consciousness in its province (clergy)."_s},
    {u"administrative_efficiency"_s, u"yes: it counts towards the state's administration (bureaucrats)."_s},
    {u"can_work_factory"_s, u"yes: factories can employ it."_s},
    {u"leadership"_s, u"Leadership points a month for each share of the population it reaches (officers)."_s},
    {u"research_points"_s, u"Research points a day at its best share of the population. Keep it off soldiers: the game "
                           "crashes at start with research points on them."_s},
    {u"research_optimum"_s, u"The share of the population (0–1) at which it gives its full research points."_s},
    {u"tax_eff"_s, u"How much each share of the population adds to tax efficiency."_s},
    {u"max_size"_s, u"Pops above this size split in two."_s},
    {u"merge_max_size"_s, u"Pops below this size merge with a like pop in the province."_s},
    {u"equivalent"_s, u"The pop type that does the same work in another kind of building (farmers ↔ labourers)."_s},
    {u"workplace_input"_s, u"How much of its workplace's input it stands for."_s},
    {u"workplace_output"_s, u"How much of its workplace's output it stands for."_s},
    {u"demote_migrant"_s, u"yes: when it demotes it may move away rather than change type."_s},
    {u"factor"_s, u"A number that multiplies the chance: the first one in a block is the base, one inside “modifier” "
                  "applies only while that modifier's condition holds. 0 switches it off; below 1 less likely; above 1 "
                  "more likely."_s},
    {u"modifier"_s, u"factor × this, but only while the condition written next to the factor holds."_s},
    {u"group"_s, u"Adds up instead of multiplying: the modifiers inside one group are summed."_s},
    {u"AND"_s, u"All of the conditions inside must hold."_s},
    {u"OR"_s, u"At least one of the conditions inside must hold."_s},
    {u"NOT"_s, u"None of the conditions inside may hold."_s},
    {u"literacy"_s, u"Condition: the pop's literacy (0–1) is at least this."_s},
    {u"consciousness"_s, u"Condition: the pop's consciousness (0–10) is at least this."_s},
    {u"militancy"_s, u"Condition: the pop's militancy (0–10) is at least this."_s},
    {u"life_needs"_s, u"As a block: the goods it must buy to live. As a condition: the share of its life needs met "
                      "(0–1) is at least this."_s},
    {u"everyday_needs"_s, u"As a block: the goods it buys next. As a condition: the share of its everyday needs met."_s},
    {u"luxury_needs"_s, u"As a block: its luxuries. As a condition: the share of its luxury needs met."_s},
    {u"political_reform_want"_s, u"Condition: how much the pop wants political reform (0–1)."_s},
    {u"social_reform_want"_s, u"Condition: how much the pop wants social reform (0–1)."_s},
    {u"state_scope"_s, u"The conditions inside are asked of the state the pop lives in."_s},
    {u"country"_s, u"The conditions inside are asked of the pop's country."_s},
    {u"location"_s, u"The conditions inside are asked of the pop's province."_s},
    {u"owner"_s, u"The conditions inside are asked of the province's owner."_s},
    {u"is_primary_culture"_s, u"Condition: the pop has the nation's primary culture."_s},
    {u"is_accepted_culture"_s, u"Condition: the pop's culture is primary or accepted."_s},
    {u"is_state_religion"_s, u"Condition: the pop has the state religion."_s},
    {u"is_culture_group"_s, u"Condition: the pop's culture belongs to this culture group."_s},
    {u"has_pop_culture"_s, u"Condition: a pop of this culture lives here."_s},
    {u"has_pop_religion"_s, u"Condition: a pop of this religion lives here."_s},
    {u"has_factories"_s, u"Condition: the state has factories."_s},
    {u"is_coastal"_s, u"Condition: the province lies on the sea."_s},
    {u"is_colonial"_s, u"Condition: the state is a colony."_s},
    {u"is_state_capital"_s, u"Condition: the province is its state's capital."_s},
    {u"is_capital"_s, u"Condition: the province is the nation's capital."_s},
    {u"civilized"_s, u"Condition: the nation is civilised."_s},
    {u"war"_s, u"Condition: the nation is at war."_s},
    {u"war_exhaustion"_s, u"Condition: the nation's war exhaustion is at least this."_s},
    {u"total_pops"_s, u"Condition: the province has at least this many people."_s},
    {u"pop_majority_ideology"_s, u"Condition: most pops of the province follow this ideology."_s},
    {u"crime_fighting"_s, u"Condition: the administration spending on crime fighting is at least this."_s},
    {u"has_country_flag"_s, u"Condition: this country flag is set."_s},
    {u"has_country_modifier"_s, u"Condition: the nation has this modifier."_s},
    {u"continent"_s, u"Condition: the province lies on this continent."_s},
    {u"poor_strata_militancy"_s, u"Condition: the average militancy of the poor is at least this."_s},
    {u"middle_strata_militancy"_s, u"Condition: the average militancy of the middle strata is at least this."_s},
    {u"rich_strata_militancy"_s, u"Condition: the average militancy of the rich is at least this."_s},
    {u"revanchism"_s, u"Condition: the nation's revanchism (0–1) is at least this."_s},
    {u"poor_tax"_s, u"Condition: the tax on the poor (0–1) is at least this."_s},
    {u"middle_tax"_s, u"Condition: the tax on the middle strata is at least this."_s},
    {u"rich_tax"_s, u"Condition: the tax on the rich is at least this."_s},
    {u"badboy_factor"_s, u"Infamy the war goal costs: this share of the usual amount."_s},
    {u"prestige_factor"_s, u"Prestige it gives when won: this share of the usual amount."_s},
    {u"peace_cost_factor"_s, u"How much of the war score it costs in the peace."_s},
    {u"months"_s, u"How long the war goal stays valid once justified."_s},
    {u"construction_speed"_s, u"How fast it is justified (1 = normal)."_s},
    {u"sprite_index"_s, u"The war goal's picture: its number in the war goal icon strip."_s},
    {u"icon"_s, u"Its picture: the number in the icon strip (Library → Icon strips shows them all)."_s},
};

static bool keyed(const Node &n) { return n.has_key && !n.key.isEmpty(); }   // `if n.key`

static QString loc_or(moddata::Mod &mod, const QString &key, const QString &fallback)   // mod.loc.get(key) or …
{
    const QString *t = mod.loc.get(key);
    return t && !t->isEmpty() ? *t : fallback;
}

static QString rgb_css(const std::tuple<int, int, int> &c)
{
    return u"rgb(%1,%2,%3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

QHash<QString, QString> _names(moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // {word: kind} of everything the mod gives a name (the kinds of refs.KINDS).
    QHash<QString, QString> out;
    auto put1 = [&](const QString &w, const QString &kind) {
        if (!out.contains(w))
            out.insert(w, kind);
    };
    auto put_keys = [&](const auto &m, const QString &kind) {
        for (const auto &kv : m)
            put1(kv.first, kind);
    };
    put_keys(mod.poptypes, u"poptype"_s);
    put_keys(mod.goods, u"goods"_s);
    for (const QString &w : defpanel::ideologies(store))
        put1(w, u"ideology"_s);
    for (const Node &grp : script::parse(store.text(u"common/issues.txt"_s))) {
        if (!grp.is_block)
            continue;
        for (const Node &issue : grp.kids) {
            if (keyed(issue) && issue.is_block) {
                put1(issue.key, u"issue"_s);
                for (const Node &o : issue.kids)
                    if (keyed(o) && o.is_block)
                        put1(o.key, u"option"_s);
            }
        }
    }
    for (const auto &[rel, kind] : {std::pair{u"common/governments.txt"_s, u"government"_s},
                                    std::pair{u"common/nationalvalues.txt"_s, u"nv"_s}})
        for (const Node &n : script::parse(store.text(rel)))
            if (keyed(n) && n.is_block)
                put1(n.key, kind);
    if (mod.nation_data) {
        put_keys(mod.nation_data->techs, u"tech"_s);
        put_keys(mod.nation_data->inventions, u"invention"_s);
    }
    put_keys(mod.cultures, u"culture"_s);
    for (const QString &g : mod.culture_groups)
        put1(g, u"cgroup"_s);
    put_keys(mod.religions, u"religion"_s);
    put_keys(mod.countries, u"nation"_s);
    put_keys(mod.regions, u"state"_s);
    for (const auto &kv : GLOSSARY) {            // a word of the script itself is no name
        auto it = out.find(kv.first);
        if (it != out.end() && *it != u"poptype" && *it != u"goods")
            out.erase(it);
    }
    return out;
}

// ======================================================================================== LocalRefs
LocalRefs::LocalRefs(moddata::Mod &mod_, QHash<QString, QString> names_) : RefIndex(mod_), names(std::move(names_)) {}

void LocalRefs::build(const QString &text)
{
    std::vector<refs::Ref> out;
    auto it = scriptedit::TOKEN.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString t = m.captured(0);
        const QChar c = t.at(0);
        if (c == u'#')
            continue;                            // (TOKEN's comment runs to the line's end)
        if (c == u'"') {
            const QString t2 = py::strip(t, u"\""_s);
            const auto k = names.constFind(t2);
            if (k != names.constEnd() && !k->isEmpty())
                out.push_back(refs::Ref{m.capturedStart(0) + 1, t2.size(), *k, t2, u"use"_s, true, std::nullopt});
            continue;
        }
        const auto k = names.constFind(t);
        if (k != names.constEnd() && !k->isEmpty())
            out.push_back(refs::Ref{m.capturedStart(0), t.size(), *k, t, u"use"_s, true, std::nullopt});
    }
    list = std::move(out);
    starts.clear();
    for (const auto &r : list)
        starts.push_back(r.pos);
}

const refs::Ref *LocalRefs::at(const QString &, qsizetype pos) const
{
    const auto i = (std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin()) - 1;
    if (i >= 0) {
        const refs::Ref &r = list[size_t(i)];
        if (r.pos <= pos && pos <= r.pos + r.length)
            return &r;
    }
    return nullptr;
}

std::vector<refs::Ref> LocalRefs::in_range(const QString &, qsizetype a, qsizetype b) const
{
    const auto i = std::lower_bound(starts.begin(), starts.end(), a) - starts.begin();
    const auto j = std::lower_bound(starts.begin(), starts.end(), b) - starts.begin();
    return j > i ? std::vector<refs::Ref>(list.begin() + i, list.begin() + j) : std::vector<refs::Ref>{};
}

std::optional<std::pair<QString, qsizetype>> LocalRefs::definition(const QString &, const QString &) const
{
    return std::nullopt;
}

// ======================================================================================== _Ws
_Ws::_Ws(BigEditor *win_, scriptstore::ScriptStore *store_) : win(win_)
{
    store = store_;
    bool ok = false;
    const int v = settings_::settings()->value(u"bigFontSize"_s, 12).toInt(&ok);
    font_size = ok ? v : 12;
}

scriptedit::ScriptEditor *_Ws::current() const { return win->ed; }
void _Ws::_cursor_moved(scriptedit::ScriptEditor *) { win->_cursor_moved(); }
void _Ws::_decorate(scriptedit::ScriptEditor *) { win->_decorate(); }

void _Ws::zoom(int step)
{
    font_size = step == 0 ? 12 : std::max(7, std::min(30, font_size + step));
    if (win->ed)
        win->ed->set_size(font_size);
    settings_::settings()->setValue(u"bigFontSize"_s, font_size);
}

void _Ws::back() { win->back(); }
void _Ws::follow(const refs::Ref &r, bool) { win->follow(r); }
QString _Ws::hover_html(const QString &, const refs::Ref &r) const { return win->ref_html(r); }

// ======================================================================================== _Editor
bool _Editor::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        BigEditor *win = static_cast<_Ws *>(ws)->win;
        const auto r = ref_at_point(he->pos());
        QString tip = r ? win->ref_html(*r) : QString();
        QTextCursor c = cursorForPosition(he->pos());
        c.select(QTextCursor::WordUnderCursor);
        const QString word = c.selectedText();
        const QString *gl = GLOSSARY.get(word);
        if (gl && !gl->isEmpty())
            tip = (tip.isEmpty() ? QString() : tip + u"<hr>"_s) +
                  u"<div style=\"max-width:520px\"><b>%1</b> — %2</div>"_s.arg(refs::esc(word), refs::esc(*gl));
        if (!tip.isEmpty())
            QToolTip::showText(he->globalPos(), tip, viewport());
        else
            QToolTip::hideText();
        return true;
    }
    return ScriptEditor::viewportEvent(e);
}

// ======================================================================================== BigEditor
BigEditor::BigEditor(moddata::Mod &mod_, document::Document *doc_, std::function<void()> changed_, QWidget *parent)
    : QDialog(parent), mod(mod_), doc(doc_), changed(std::move(changed_)),
      store(&doc_->map<scriptstore::ScriptStore>(u"scripts"_s)), ws(this, store)
{
    setWindowFlags(Qt::Window);
    ws.refs = refs = new LocalRefs(mod, _names(mod, *store));
    resize(1560, 940);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(12, 8, 12, 8);
    bl->setSpacing(6);
    back_btn = new QPushButton(u"◀ Back"_s);
    connect(back_btn, &QPushButton::clicked, this, [this] { back(); });
    bl->addWidget(back_btn);
    title = label(QString(), u"title"_s, false);
    bl->addWidget(title);
    sub = new QLabel(QString());
    sub->setObjectName(u"faint"_s);
    bl->addWidget(sub, 1);
    find = new QLineEdit();
    find->setPlaceholderText(u"Find in the text (Enter: next · Shift+Enter: the one before)"_s);
    find->setFixedWidth(330);
    find->setClearButtonEnabled(true);
    connect(find, &QLineEdit::textChanged, this, [this](const QString &) { _find(0); });
    find->installEventFilter(this);
    bl->addWidget(find);
    found = label(QString(), u"faint"_s, false);
    bl->addWidget(found);
    const std::vector<std::tuple<QString, QString, std::function<void()>>> btns = {
        {u"▸ Fold all"_s, u"Fold every block (▸ in the margin opens one)"_s, [this] { _fold(true); }},
        {u"▾ Unfold all"_s, u"Open every block"_s, [this] { _fold(false); }},
        {u"A−"_s, u"Smaller text (Ctrl + wheel)"_s, [this] { ws.zoom(-1); }},
        {u"A+"_s, u"Bigger text (Ctrl + wheel)"_s, [this] { ws.zoom(1); }}};
    for (const auto &[t, tip, fn] : btns) {
        auto *b = new QPushButton(t);
        b->setToolTip(tip);
        if (t.size() <= 2)
            b->setFixedWidth(40);
        connect(b, &QPushButton::clicked, this, [fn] { fn(); });
        bl->addWidget(b);
    }
    lay->addWidget(bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);
    auto *left = new QWidget();
    left->setObjectName(u"side"_s);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(12, 10, 8, 8);
    ll->setSpacing(6);
    ll->addWidget(label(u"Its parts"_s, u"title"_s));
    ll->addWidget(label(u"A click goes there in the text. Gold: a part with blocks inside — open it for the single "
                        "targets with their base factor and how many conditions change it."_s, u"faint"_s));
    tree = new QTreeWidget();
    tree->setColumnCount(2);
    tree->setHeaderLabels({u"Part"_s, u"What it holds"_s});
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setUniformRowHeights(true);
    colsizes::make_resizable(tree, u"bigOutline"_s, {250});
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int col) { _outline_clicked(it, col); });
    ll->addWidget(tree, 1);
    about = label(QString(), u"sub"_s);
    about->setMinimumHeight(64);
    about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ll->addWidget(about);
    split->addWidget(left);
    host = new QWidget();
    host_lay = new QVBoxLayout(host);
    host_lay->setContentsMargins(0, 0, 0, 0);
    split->addWidget(host);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({500, 1060});
    auto *foot = new QWidget();
    foot->setObjectName(u"side"_s);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(12, 5, 12, 5);
    where = label(QString(), u"faint"_s, false);
    fl->addWidget(where);
    legend = new QLabel(QString());
    legend->setTextFormat(Qt::RichText);
    legend->setWordWrap(true);
    fl->addWidget(legend, 1);
    lay->addWidget(foot);
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(350);
    connect(_timer, &QTimer::timeout, this, [this] { _text_settled(); });
    store->listeners.push_back([w = QPointer<BigEditor>(this)](const QString &r) {
        if (w)
            w->_store_changed(r);
    });
    const std::vector<std::pair<QString, std::function<void()>>> keys = {
        {u"Ctrl+F"_s, [this] { find->setFocus(); find->selectAll(); }},
        {u"Ctrl+="_s, [this] { ws.zoom(1); }}, {u"Ctrl++"_s, [this] { ws.zoom(1); }},
        {u"Ctrl+-"_s, [this] { ws.zoom(-1); }}, {u"Ctrl+0"_s, [this] { ws.zoom(0); }}};
    for (const auto &[k, fn] : keys) {
        auto *sc = new QShortcut(QKeySequence(k), this);
        connect(sc, &QShortcut::activated, this, [fn] { fn(); });
    }
}

// ------------------------------------------------------------------ what is shown
void BigEditor::open_entry(const QString &rel_, const QStringList &path_, const QString &noun_, bool record)
{
    if (record && rel && !(*rel == rel_ && path == path_))
        _trail.emplace_back(*rel, path, noun);
    rel = rel_;
    path = path_;
    noun = noun_;
    back_btn->setVisible(!_trail.empty());
    QTextDocument *d = store->document(rel_);
    if (!_hl.value(rel_))
        _hl.insert(rel_, new scriptedit::ScriptHighlighter(d, &ws, rel_));
    refs->build(store->text(rel_));
    QPointer<scriptedit::ScriptEditor> old = ed;
    ed = nullptr;
    auto *e = new _Editor(&ws, rel_, d);
    host_lay->addWidget(e);
    if (old) {
        host_lay->removeWidget(old);
        old->hide();
        old->deleteLater();
    }
    ed = e;
    _hl.value(rel_)->rehighlight();
    const QString key = !path_.isEmpty() ? path_.last() : pypath::splitext_stem(pypath::basename(rel_));
    const QString nm = loc_or(mod, key, py::capitalize(QString(key).replace(u'_', u' ')));
    title->setText(nm);
    setWindowTitle(u"%1 — %2"_s.arg(nm, noun_));
    sub->setText(u"  %1%2 · typed changes are written on Apply"_s.arg(
        rel_, !path_.isEmpty() ? u" › "_s + path_.join(u" › "_s) : QString()));
    _fill_outline();
    if (const auto ks = _node_start())
        _go(*ks, false);
    e->setFocus();
    QStringList kinds;
    for (const auto &r : refs->list)
        if (!kinds.contains(r.kind))
            kinds << r.kind;
    QStringList parts;
    for (const QString &k : kinds)
        if (const auto *kk = refs::KINDS.get(k))
            parts << u"<span style=\"color: %1\">%2</span>"_s.arg(rgb_css(kk->second), kk->first);
    legend->setText(u"Colours: "_s + parts.join(u" · "_s) +
                    u" · <span style=\"color:#e8c476\">block names</span> · "
                    "<span style=\"color:#a0d796\">numbers</span> — hover a word for what it is"_s);
}

void BigEditor::back()
{
    if (!_trail.empty()) {
        auto [r, p, n] = _trail.back();
        _trail.pop_back();
        open_entry(r, p, n, false);
    }
}

std::optional<qsizetype> BigEditor::_node_start()
{
    if (path.isEmpty())
        return std::nullopt;
    const script::Nodes nodes = script::parse(store->text(*rel));
    const Node *n = blocks::find(nodes, path);
    return n ? std::optional<qsizetype>(n->kstart) : std::nullopt;
}

// ------------------------------------------------------------------ the overview
QString BigEditor::_summary(const Node &n)
{
    // What a block holds, in a few words.
    std::vector<const Node *> blk, leaf, mods, named;
    for (const Node &c : n.kids) {
        if (!keyed(c))
            continue;
        (c.is_block ? blk : leaf).push_back(&c);
    }
    for (const Node *c : blk) {
        if (c->key == u"modifier")
            mods.push_back(c);
        if (c->key != u"modifier" && c->key != u"group")
            named.push_back(c);
    }
    const Node *fac = nullptr;
    for (const Node *c : leaf)
        if (c->key == u"factor") {
            fac = c;
            break;
        }
    if (fac || !mods.empty())
        return u"%1%2 condition%3"_s.arg(fac ? u"factor %1 · "_s.arg(fac->value) : QString())
            .arg(qsizetype(mods.size()))
            .arg(mods.size() == 1 ? QString() : u"s"_s);
    if (!named.empty()) {
        QStringList l;
        for (size_t i = 0; i < named.size() && i < 6; ++i)
            l << _nm(named[i]->key);
        return u"%1: %2"_s.arg(qsizetype(named.size())).arg(l.join(u", "_s) + (named.size() > 6 ? u" …"_s : QString()));
    }
    if (!leaf.empty()) {
        QStringList l;
        for (size_t i = 0; i < leaf.size() && i < 6; ++i)
            l << _nm(leaf[i]->key) + u' ' + leaf[i]->value;
        return l.join(u", "_s) + (leaf.size() > 6 ? u" …"_s : QString());
    }
    QStringList bare;
    for (const Node &c : n.kids)
        if (!c.has_key && !c.is_block)
            bare << c.value;
    return !bare.isEmpty() ? bare.mid(0, 8).join(u' ') : u"empty"_s;
}

QString BigEditor::_nm(const QString &key) { return loc_or(mod, key, key); }

void BigEditor::_fill_outline()
{
    const QString VALUES = QString(QChar(0)) + u"values"_s;
    QSet<QString> open_keys;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = tree->topLevelItem(i);
        if (it->isExpanded())
            open_keys.insert(it->data(0, Qt::UserRole + 1).toString());
    }
    const std::pair<QString, QStringList> now{*rel, path};
    const bool first_time = tree->topLevelItemCount() == 0 || _outline_of != now;
    _outline_of = now;
    tree->clear();
    QFont bold(tree->font());
    bold.setBold(true);
    const script::Nodes nodes = script::parse(store->text(*rel));
    const script::Nodes *within = &nodes;
    static const script::Nodes none;
    if (!path.isEmpty()) {
        const Node *n = blocks::find(nodes, path);
        within = n ? &n->kids : &none;
    }
    std::vector<const Node *> kids, plain;
    for (const Node &c : *within)
        if (keyed(c)) {
            kids.push_back(&c);
            if (!c.is_block)
                plain.push_back(&c);
        }
    if (!plain.empty()) {
        QStringList ks;
        for (size_t i = 0; i < plain.size() && i < 5; ++i)
            ks << plain[i]->key;
        auto *head = new QTreeWidgetItem(QStringList{
            u"Basic values"_s,
            u"%1: %2"_s.arg(qsizetype(plain.size())).arg(ks.join(u", "_s) + (plain.size() > 5 ? u" …"_s : QString()))});
        head->setFont(0, bold);
        head->setData(0, Qt::UserRole, qlonglong(plain[0]->kstart));
        head->setData(0, Qt::UserRole + 1, VALUES);
        head->setData(0, Qt::UserRole + 2, u"The single lines of the %1: one key = one value. Hover a key in the "
                                           "text for what it does."_s.arg(noun));
        tree->addTopLevelItem(head);
        for (const Node *c : plain) {
            auto *it = new QTreeWidgetItem(QStringList{modpanel::attr_name(mod, c->key), c->value});
            it->setData(0, Qt::UserRole, qlonglong(c->kstart));
            it->setData(0, Qt::UserRole + 2, GLOSSARY.value(c->key, QString()));
            it->setToolTip(0, c->key);
            it->setForeground(1, QBrush(QColor(u"#a0d796"_s)));
            head->addChild(it);
        }
        head->setExpanded(open_keys.contains(VALUES));
    }
    QHash<QString, int> seen;
    for (const Node *c : kids) {
        if (!c->is_block)
            continue;
        seen[c->key] = seen.value(c->key, 0) + 1;
        const auto *part = PARTS.get(c->key);
        const QString name = part ? part->first : modpanel::attr_name(mod, c->key);
        const QString about_ = part ? part->second : QString();
        std::vector<const Node *> inner;
        for (const Node &k : c->kids)
            if (keyed(k) && k.is_block && k.key != u"modifier" && k.key != u"group")
                inner.push_back(&k);
        const int s = seen.value(c->key);
        auto *it = new QTreeWidgetItem(QStringList{name + (s > 1 ? u" (%1)"_s.arg(s) : QString()), _summary(*c)});
        it->setFont(0, bold);
        if (!inner.empty())
            it->setForeground(0, QBrush(QColor::fromString(theme::GOLD)));
        it->setData(0, Qt::UserRole, qlonglong(c->kstart));
        it->setData(0, Qt::UserRole + 1, c->key);
        it->setData(0, Qt::UserRole + 2, !about_.isEmpty() ? about_ : GLOSSARY.value(c->key, QString()));
        it->setToolTip(0, c->key);
        it->setToolTip(1, _summary(*c));
        tree->addTopLevelItem(it);
        for (const Node *k : inner) {
            const QString kind = refs->names.value(k->key);
            const auto *kk = kind.isEmpty() ? nullptr : refs::KINDS.get(kind);
            auto *ch = new QTreeWidgetItem(QStringList{_nm(k->key), _summary(*k)});
            ch->setData(0, Qt::UserRole, qlonglong(k->kstart));
            ch->setData(0, Qt::UserRole + 2,
                        u"%1%2 — %3"_s.arg(kk ? kk->first + u' ' : QString(), k->key, _summary(*k)));
            ch->setToolTip(0, k->key);
            if (kk)
                ch->setForeground(0, QBrush(QColor(std::get<0>(kk->second), std::get<1>(kk->second),
                                                   std::get<2>(kk->second))));
            it->addChild(ch);
        }
        it->setExpanded(!first_time ? open_keys.contains(c->key) : inner.size() <= 12 && !inner.empty());
    }
    if (kids.empty())
        tree->addTopLevelItem(new QTreeWidgetItem(QStringList{u"(nothing in it yet)"_s, QString()}));
}

void BigEditor::_outline_clicked(QTreeWidgetItem *it, int)
{
    const QVariant pos = it->data(0, Qt::UserRole);
    about->setText(it->data(0, Qt::UserRole + 2).toString());
    if (pos.isValid())
        _go(qsizetype(pos.toLongLong()));
}

void BigEditor::_go(qsizetype pos, bool flash)
{
    QPointer<scriptedit::ScriptEditor> e = ed;
    if (!e)
        return;
    e->reveal(pos);
    QTextCursor c = e->textCursor();
    c.setPosition(int(std::max<qsizetype>(0, std::min<qsizetype>(pos, e->document()->characterCount() - 1))));
    e->setTextCursor(c);
    e->centerCursor();
    if (flash) {
        const QString text = store->text(*rel);
        const qsizetype end = text.indexOf(u'\n', pos);
        e->flash = std::pair<qsizetype, qsizetype>(py::rfind(text, u'\n', pos) + 1, end < 0 ? text.size() : end);
        _decorate();
        QTimer::singleShot(1300, this, [this, e] {
            if (e)
                e->flash.reset();
            _decorate();
        });
    }
}

// ------------------------------------------------------------------ what the editor asks
void BigEditor::_cursor_moved()
{
    if (!ed)
        return;
    const QTextCursor c = ed->textCursor();
    where->setText(u"line %1, column %2   "_s.arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
    _decorate();
}

void BigEditor::_decorate()
{
    if (!ed)
        return;
    QList<QTextEdit::ExtraSelection> sels;
    QTextEdit::ExtraSelection cl;
    cl.format.setBackground(QColor(30, 34, 42));
    cl.format.setProperty(QTextFormat::FullWidthSelection, true);
    cl.cursor = ed->textCursor();
    cl.cursor.clearSelection();
    sels << cl;
    const int n = ed->document()->characterCount();
    if (ed->flash) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(90, 75, 30));
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = QTextCursor(ed->document());
        s.cursor.setPosition(int(std::min<qsizetype>(ed->flash->first, n - 1)));
        s.cursor.setPosition(int(std::min<qsizetype>(ed->flash->second, n - 1)), QTextCursor::KeepAnchor);
        sels << s;
    }
    for (size_t i = 0; i < _hits.size() && i < 1500; ++i) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(200, 150, 40, 120));
        s.cursor = QTextCursor(ed->document());
        s.cursor.setPosition(int(std::min<qsizetype>(_hits[i].first, n - 1)));
        s.cursor.setPosition(int(std::min<qsizetype>(_hits[i].second, n - 1)), QTextCursor::KeepAnchor);
        sels << s;
    }
    const refs::Ref *r = refs->at(rel.value_or(QString()), ed->textCursor().position());
    if (r) {                                     // the word at the cursor: lit everywhere it stands
        const auto *kk = refs::KINDS.get(r->kind);
        const auto rgb = kk ? kk->second : std::tuple<int, int, int>{200, 200, 200};
        for (const auto &o : refs->list) {
            if (o.kind == r->kind && o.target == r->target && o.pos + o.length < n) {
                QTextEdit::ExtraSelection s;
                s.format.setBackground(QColor(std::get<0>(rgb), std::get<1>(rgb), std::get<2>(rgb), 70));
                s.cursor = QTextCursor(ed->document());
                s.cursor.setPosition(int(o.pos));
                s.cursor.setPosition(int(o.pos + o.length), QTextCursor::KeepAnchor);
                sels << s;
            }
        }
    }
    ed->setExtraSelections(sels);
}

QString BigEditor::ref_html(const refs::Ref &r)
{
    const QString &kind = r.kind, &t = r.target;
    const auto *kk = refs::KINDS.get(kind);
    const QString name = kk ? kk->first : kind;
    const auto rgb = kk ? kk->second : std::tuple<int, int, int>{220, 220, 220};
    const QString *loc = mod.loc.get(t);
    long long n = 0;
    for (const auto &o : refs->list)
        if (o.kind == kind && o.target == t)
            ++n;
    const QString tip = kind == u"poptype" && _pop_rel(t) != rel ? u"Ctrl+click: open this pop type here"_s : QString();
    return u"<div style=\"max-width:520px\"><b style=\"color: rgb(%1,%2,%3)\">%4</b> %5 <span style=\"color:#8a8272\">"
           "(%6)</span><br>%7 time%8 in this file%9</div>"_s
        .arg(std::get<0>(rgb)).arg(std::get<1>(rgb)).arg(std::get<2>(rgb))
        .arg(refs::esc(name), refs::esc(loc && !loc->isEmpty() ? *loc : QString(t).replace(u'_', u' ')), refs::esc(t))
        .arg(n)
        .arg(n == 1 ? QString() : u"s"_s,
             !tip.isEmpty() ? u"<br><span style=\"color:#8a8272\">%1</span>"_s.arg(tip) : QString());
}

std::optional<QString> BigEditor::_pop_rel(const QString &t)
{
    const QString r = u"poptypes/%1.txt"_s.arg(t);
    return fs::isfile(mod.p(r.split(u'/'))) || store->docs.contains(r) ? std::optional<QString>(r) : std::nullopt;
}

void BigEditor::follow(const refs::Ref &r)
{
    if (r.kind == u"poptype") {
        const auto pr = _pop_rel(r.target);
        if (pr && !pr->isEmpty() && pr != rel)
            open_entry(*pr, {}, u"pop type"_s);
    }
}

void BigEditor::_fold(bool on)
{
    if (ed)
        ed->fold_all(on);
}

// ------------------------------------------------------------------ find
bool BigEditor::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == find && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            _find(k->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
    }
    return QDialog::eventFilter(obj, e);
}

void BigEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)   // (a dialog would close on Enter)
        return;
    if (e->key() == Qt::Key_Escape && !find->text().isEmpty()) {
        find->clear();
        return;
    }
    QDialog::keyPressEvent(e);
}

void BigEditor::_find(int step)
{
    const QString q = find->text();
    const QString text = rel ? store->text(*rel) : QString();
    _hits.clear();
    if (q.size() >= 2) {
        const QRegularExpression re(QRegularExpression::escape(q), QRegularExpression::CaseInsensitiveOption);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            _hits.emplace_back(m.capturedStart(0), m.capturedEnd(0));
        }
    }
    found->setText(!q.isEmpty() ? u"%1 found"_s.arg(qsizetype(_hits.size())) : QString());
    if (!_hits.empty() && ed) {
        const qsizetype pos = ed->textCursor().position();
        std::vector<qsizetype> starts;
        for (const auto &h : _hits)
            starts.push_back(h.first);
        const auto nn = qsizetype(starts.size());
        qsizetype i;
        if (step >= 0) {
            i = std::lower_bound(starts.begin(), starts.end(), pos + (step ? 1 : 0)) - starts.begin();
            i = i >= nn ? 0 : i;
        } else {
            i = (std::lower_bound(starts.begin(), starts.end(), pos) - starts.begin()) - 1;
            if (i < 0)
                i += nn;                         // (Python's starts[-1]: the last one)
        }
        const qsizetype a = starts[size_t(i)];
        ed->reveal(a);
        QTextCursor c = ed->textCursor();
        c.setPosition(int(a));
        ed->setTextCursor(c);
        ed->centerCursor();
    }
    _decorate();
}

// ------------------------------------------------------------------ the text changed (typed here or anywhere)
void BigEditor::_store_changed(const QString &r)
{
    if (rel && r == *rel && isVisible())
        _timer->start();
}

void BigEditor::_text_settled()
{
    if (!rel)
        return;
    refs->build(store->text(*rel));
    if (auto hl = _hl.value(*rel))
        hl->rehighlight();
    _fill_outline();
    if (!find->text().isEmpty())
        _find(0);
    if (changed)
        changed();
}

void BigEditor::refresh(document::Document *d)
{
    if (d != doc || !d->maps.contains(u"scripts"_s) || &d->map<scriptstore::ScriptStore>(u"scripts"_s) != store) {
        close();
        return;
    }
    if (isVisible() && rel)
        _timer->start();
}

}  // namespace bigedit
