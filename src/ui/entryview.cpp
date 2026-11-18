// entryview.py — the lists of named blocks (see entryview.h).
#include "ui/entryview.h"
#include "core/blocks.h"
#include "core/issuepanel_data.h"
#include "core/localisation.h"
#include "core/modifiers.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/script.h"
#include "ui/colsizes.h"
#include "ui/iconstrips.h"
#include "ui/issuepanel.h"
#include "ui/modpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using widgets::BadgeButton;
using widgets::GridFlow;
using widgets::label;
using widgets::Section;

namespace entryview {


namespace {

QString kkey(const script::Node &c) { return c.has_key ? c.key : QString(); }

bool key_ok(const QString &s)                         // KEY_RE: ^[A-Za-z_][A-Za-z0-9_.]*$
{
    static const QRegularExpression re(uR"(^[A-Za-z_][A-Za-z0-9_.]*$)"_s);
    return re.match(s).hasMatch();
}

bool has_any(const QString &s, const QString &chars)  // any(c in s for c in chars)
{
    for (const QChar c : chars)
        if (s.contains(c))
            return true;
    return false;
}

bool is_digits(const QString &s)                      // str.isdigit() (the decimal digits)
{
    if (s.isEmpty())
        return false;
    for (const QChar c : s)
        if (!c.isDigit())
            return false;
    return true;
}

const QString RED_BORDER = u"QLineEdit { border: 2px solid #ff786e; }"_s;

}  // namespace

// ---------------------------------------------------------------------------------------------- the pages
const Spec TRAITS{
    .id = u"traits"_s, .how = u"grouped"_s, .rel = u"common/traits.txt"_s, .noun = u"leader trait"_s,
    .title = u"Leader traits"_s, .example = u"attack"_s,
    .intro = u"What generals and admirals can be like (common/traits.txt): personalities and backgrounds, each with "
             u"what it adds — attack, defence, morale, organisation, speed, reconnaissance, experience …"_s};
const Spec TERRAIN{
    .id = u"terrain"_s, .how = u"inside"_s, .rel = u"map/terrain.txt"_s, .base = {u"categories"_s},
    .noun = u"terrain type"_s, .title = u"Terrain types"_s, .example = u"movement_cost"_s,
    .intro = u"The kinds of terrain (the “categories” block of map/terrain.txt): how slowly armies cross each, what "
             u"it does to defence and supply, its colour on the terrain map. Which province has which terrain is "
             u"painted in Drawing → Terrain."_s,
    .make_new = false, .remove = false};
const Spec FOCUSES{
    .id = u"focus"_s, .how = u"grouped"_s, .rel = u"common/national_focus.txt"_s, .noun = u"national focus"_s,
    .title = u"National focuses"_s, .example = u"railroads"_s,
    .intro = u"The national focuses a nation can set on a state (common/national_focus.txt), in their groups: what "
             u"each one encourages and its limit."_s};
const Spec IDEOLOGIES{
    .id = u"ideologies"_s, .how = u"grouped"_s, .rel = u"common/ideologies.txt"_s, .noun = u"ideology"_s,
    .title = u"Ideologies"_s, .example = u"date"_s,
    .intro = u"The ideologies (common/ideologies.txt), in their groups: when each appears, its colour, whether "
             u"uncivilised nations have it, and how its parties vote on adding and removing reforms."_s};
const Spec STATIC_MODS{
    .id = u"static"_s, .how = u"top"_s, .rel = u"common/static_modifiers.txt"_s, .noun = u"standing modifier"_s,
    .title = u"Standing modifiers"_s, .example = u"tax_efficiency"_s,
    .intro = u"Modifiers the game itself applies (common/static_modifiers.txt): difficulty levels, coastal and "
             u"overseas provinces, war and peace, great powers, infamy, bankruptcy … — the keys are fixed by the game, "
             u"what each does is yours to change."_s,
    .make_new = false, .remove = false};
const Spec TRIGGERED_MODS{
    .id = u"triggered"_s, .how = u"top"_s, .rel = u"common/triggered_modifiers.txt"_s,
    .noun = u"triggered modifier"_s, .title = u"Triggered modifiers"_s, .example = u"prestige"_s,
    .intro = u"Modifiers a nation has for as long as a condition holds (common/triggered_modifiers.txt): the "
             u"condition is its “trigger” block, the rest is what it gives."_s};
static QString _crime_info(EntryView &view, const QString &key, const QString &rel, const QStringList &path)
{
    const QString text = view.store->text(rel);
    QHash<QString, QString> kids;
    for (const script::Node &c : blocks::children_top(text, path))
        if (!c.is_block)
            kids.insert(c.key, c.value);
    const auto uses = crime_uses(view.mod, *view.store, key);
    std::set<QString> on, add;                   // (sorted sets)
    for (const auto &[r, n, k] : uses) {
        if (k == u"enable_crime")
            on.insert(u"%1 (%2)"_s.arg(view.nm(n), r));
        else if (k == u"add_crime")
            add.insert(u"%1 (%2)"_s.arg(n, r));
    }
    const OptStr trig = blocks::block_text(text, path + QStringList{u"trigger"_s});
    long long live = 0;
    for (const QString &ln : py::splitlines(trig.value_or(QString())))
        if (!py::strip(ln).isEmpty() && !py::strip(ln).startsWith(u'#'))
            ++live;
    QStringList parts;
    auto join = [](const std::set<QString> &s) {
        QStringList l(s.begin(), s.end());
        return l.join(u", "_s);
    };
    if (py::lower(py::strip(kids.value(u"active"_s))) == u"yes")
        parts << u"<b>On from the start</b> ( active = yes )."_s;
    else if (!on.empty())
        parts << u"<b>Switched on by</b> "_s + join(on) + u"."_s;
    else
        parts << u"<span style='color:#ffb454'><b>Never switched on:</b> it has no  active = yes  and no invention or "
                 u"technology says  enable_crime = %1 . It can only come by an event.</span>"_s.arg(key);
    if (!add.empty())
        parts << u"<b>Put into a province directly</b> ( add_crime ) by "_s + join(add) + u"."_s;
    parts << (live ? u"<b>Where it may appear:</b> only in provinces that meet its “trigger” block below (%1 line%2)."_s.arg(
                         QString::number(live), live == 1 ? QString() : u"s"_s)
                   : u"<b>Where it may appear:</b> anywhere — its “trigger” block is empty. Conditions on the province go "
                     u"there (for example  is_colonial = no ,  owner = { tag = ENG } ,  any_pop = { … } )."_s);
    return parts.join(u' ');
}

const Spec CRIMES{
    .id = u"crime"_s, .how = u"top"_s, .rel = u"common/crime.txt"_s, .noun = u"crime"_s, .title = u"Crimes"_s,
    .example = u"local_ruling_party_support"_s,
    .intro = u"The crimes that can take hold in a province (common/crime.txt): what each does there, whether it is on "
             u"from the start ( active = yes ) or an invention has to switch it on ( enable_crime ), and in which "
             u"provinces it may appear (its “trigger” block: conditions on the province). An event can also put one "
             u"into a province ( add_crime ). A mod without this file uses the game's own; a change here writes the "
             u"file into the mod."_s,
    .info = _crime_info};
const Spec NEWS{
    .id = u"news"_s, .how = u"files"_s, .folder = u"news"_s, .noun = u"news text"_s, .title = u"News"_s,
    .intro = u"What the in-game newspaper writes (news — one file per kind of news: a battle, a war declared, a "
             u"construction finished …): each lists patterns with the texts to pick from. Shown as the file's blocks; "
             u"the wording itself is in the localisation."_s,
    .names = std::vector<std::pair<QString, QString>>{}, .add = false};
const Spec UNIT_COLOURS{
    .id = u"colours"_s, .how = u"top"_s, .rel = u"common/country_colors.txt"_s, .noun = u"nation's unit colours"_s,
    .title = u"Unit colours"_s,
    .intro = u"The colours of each nation's soldiers on the map (common/country_colors.txt): three colours per "
             u"nation, each as red green blue (0–255). A nation without an entry uses the game's default. The key is "
             u"the nation's tag."_s,
    .names = std::vector<std::pair<QString, QString>>{}, .make_new = true, .add = false};
const Spec CULTURE_GROUPS{
    .id = u"cultures"_s, .how = u"top"_s, .rel = u"common/cultures.txt"_s, .noun = u"culture group"_s,
    .title = u"Culture groups"_s, .example = u"union"_s,
    .intro = u"The culture groups (common/cultures.txt): the nation a group can unite into (union = TAG — its "
             u"members count as accepted there), its leader portraits and unit graphics, and its cultures with their "
             u"colours and names. Single cultures are moved and made on the first page of this window."_s};

// ---------------------------------------------------------------------------------------------- helpers
OptStr strip_of_field(const QString &rel, const QString &key)
{
    // the strip a line `key = N` of this file picks from, or None (iconstrips.FIELD_STRIP, in its order)
    static const std::vector<std::tuple<QString, QString, QString>> FIELD_STRIP = {
        {u"common/national_focus.txt"_s, u"icon"_s, u"GFX_province_natfocus_strip"_s},
        {u"poptypes"_s, u"sprite"_s, u"GFX_pops_small"_s},
        {u"common/crime.txt"_s, u"icon"_s, u"GFX_crime_strip"_s},
        {u"common/event_modifiers.txt"_s, u"icon"_s, u"GFX_modifier_icons"_s},
        {u"common/triggered_modifiers.txt"_s, u"icon"_s, u"GFX_modifier_icons"_s},
        {u"common/static_modifiers.txt"_s, u"icon"_s, u"GFX_modifier_icons"_s},
        {u"common/rebel_types.txt"_s, u"icon"_s, u"GFX_politics_rebel_strip"_s},
        {u"common/cb_types.txt"_s, u"sprite_index"_s, u"GFX_diplo_wargoal_icons"_s},
        {u"common/religion.txt"_s, u"icon"_s, u"GFX_icon_religion"_s},
        {u"units"_s, u"icon"_s, u"GFX_unit_strip"_s}};
    for (const auto &[where, k, sprite] : FIELD_STRIP)
        if (k == key && !where.isEmpty() && (rel == where || rel.startsWith(where + u'/')))
            return sprite;
    return std::nullopt;
}

std::vector<Entry> entries(const Spec &spec, const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    // [(group heading or None, key, rel, path)] of a spec's entries, in the files' order
    std::vector<Entry> out;
    const QString &how = spec.how;
    if (how == u"files") {                            // every file of a folder is one entry
        for (const QString &rel : _files(mod, &store, spec.folder, spec.hide_empty)) {
            const QString bn = rel.mid(rel.lastIndexOf(u'/') + 1);
            const qsizetype dot = bn.lastIndexOf(u'.');
            out.push_back({std::nullopt, dot > 0 ? bn.left(dot) : bn, rel, {}});
        }
        return out;
    }
    const QString &rel = spec.rel;
    script::Nodes nodes;
    try {
        nodes = script::parse(store.text(rel));
    } catch (...) {
        return out;
    }
    if (how == u"file")                               // the whole file is the one entry
        return {{std::nullopt, spec.key.value_or(rel.mid(rel.lastIndexOf(u'/') + 1)), rel, {}}};
    const QStringList &skip = spec.skip;
    if (how == u"inside") {                           // the blocks inside one block of the file
        if (const script::Node *node = blocks::find(nodes, spec.base))
            for (const script::Node &c : node->kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block && !skip.contains(c.key))
                    out.push_back({std::nullopt, c.key, rel, spec.base + QStringList{c.key}});
    } else if (how == u"grouped") {                   // the file's blocks are groups of entries
        for (const script::Node &g : nodes)
            if (g.has_key && !g.key.isEmpty() && g.is_block && !skip.contains(g.key))
                for (const script::Node &c : g.kids)
                    if (c.has_key && !c.key.isEmpty() && c.is_block)
                        out.push_back({g.key, c.key, rel, {g.key, c.key}});
    } else {                                          // "top": the file's own blocks
        for (const script::Node &c : nodes)
            if (c.has_key && !c.key.isEmpty() && c.is_block && !skip.contains(c.key))
                out.push_back({std::nullopt, c.key, rel, {c.key}});
        for (const auto &[k, ttl] : spec.extra)       // e.g. the peace order: a block that isn't an entry
            if (std::any_of(nodes.begin(), nodes.end(), [&k](const script::Node &c) { return kkey(c) == k; }))
                out.push_back({ttl, k, rel, {k}});
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- EntryView
EntryView::EntryView(moddata::Mod &mod_, const Spec &spec_, QWidget *parent) : QWidget(parent), mod(mod_), spec(spec_)
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
    s->lay->addWidget(label(spec.title, u"title"_s));
    s->lay->addWidget(label(spec.intro, u"faint"_s));
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a %1 (name or key)…"_s.arg(spec.noun));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this] { _fill_list(); });
    s->lay->addWidget(search);
    lst = new QTreeWidget();
    lst->setColumnCount(2);
    lst->setHeaderLabels({py::capitalize(spec.noun), u"Key in the files"_s});
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::SingleSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"entry_"_s + spec.id, {std::nullopt, 260}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) { _picked(it, prev); });
    s->lay->addWidget(lst, 1);
    lay->addWidget(s, 1);
    if (spec.make_new) {
        s = new Section();
        s->lay->addWidget(_bold(u"A new %1 (a copy of the one picked)"_s.arg(spec.noun)));
        auto *row = new QHBoxLayout();
        new_key = new QLineEdit();
        new_key->setPlaceholderText(u"its key in the files: letters, digits, _"_s);
        connect(new_key, &QLineEdit::returnPressed, this, [this] { _make_new(); });
        row->addWidget(new_key, 1);
        auto *b = new QPushButton(u"＋ Make it"_s);
        connect(b, &QPushButton::clicked, this, [this] { _make_new(); });
        row->addWidget(b);
        s->lay->addLayout(row);
        new_warn = label(QString(), u"sub"_s);
        new_warn->setStyleSheet(u"color: #ff786e;"_s);
        s->lay->addWidget(new_warn);
        lay->addWidget(s);
    }
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
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 7);
    split->setSizes({620, 1000});
    s = new Section();
    head = new QLabel();
    head->setWordWrap(true);
    head->setTextFormat(Qt::PlainText);
    s->lay->addWidget(head);
    title = label(u"Pick a %1"_s.arg(spec.noun), u"title"_s);
    auto *trow = new QHBoxLayout();
    trow->addWidget(title, 1);
    big_btn = new QPushButton(u"⤢ Open large"_s);
    big_btn->setToolTip(u"This %1 in a large window of its own: an overview of its parts on the left, its text on "
                        u"the right — coloured, with a note on every word you hover"_s.arg(spec.noun));
    connect(big_btn, &QPushButton::clicked, this, [this] { _open_big(); });
    big_btn->hide();
    trow->addWidget(big_btn);
    s->lay->addLayout(trow);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    info = new QLabel(QString());                     // what else the mod's files say of this entry (a spec's "info")
    info->setObjectName(u"sub"_s);
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    info->hide();
    s->lay->addWidget(info);
    auto *g = new QGridLayout();
    g->setColumnStretch(1, 1);
    const std::vector<std::pair<QString, QString>> names =
        spec.names ? *spec.names : std::vector<std::pair<QString, QString>>{{QString(), u"Name in the game"_s}};
    int r = 0;
    for (const auto &[suffix, txt] : names) {
        g->addWidget(label(txt, u"sub"_s, false), r, 0);
        auto *ed = new QLineEdit();
        connect(ed, &QLineEdit::editingFinished, this, [this, ed, sfx = suffix] { _loc_done(ed, sfx); });
        g->addWidget(ed, r, 1);
        name_edits.insert(suffix, ed);
        ++r;
    }
    names_host = new QWidget();
    names_host->setLayout(g);
    s->lay->addWidget(names_host);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    body_host = new QWidget();
    body_lay = new QVBoxLayout(body_host);
    body_lay->setContentsMargins(0, 0, 0, 0);
    s->lay->addWidget(body_host);
    rl->addWidget(s);
    rl->addStretch(1);
}

QLabel *EntryView::_bold(const QString &t, QWidget *parent)
{
    QLabel *h = label(t, u"sub"_s, false);
    if (parent)
        h->setParent(parent);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ data
void EntryView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    _fill_list();
    _fill_detail();
}

OptStr EntryView::loc(const QString &key)
{
    localisation::LocEdits *lc =
        doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    OptStr got;
    if (lc)
        got = lc->text(key);
    else if (const QString *t = mod.loc.get(key))
        got = *t;
    return got && !got->isEmpty() ? got : std::nullopt;
}

QString EntryView::nm(const QString &key)
{
    const QString t = py::strip(issuepanel::strip_colours(loc(key).value_or(QString())));
    return !t.isEmpty() ? t : py::capitalize(QString(key).replace(u'_', u' '));
}

void EntryView::_fill_list()
{
    if (!store)
        return;
    _entries = entries(spec, mod, *store);
    const QString q = py::lower(py::strip(search->text()));
    lst->blockSignals(true);
    lst->clear();
    QFont bold(lst->font());
    bold.setBold(true);
    OrderedMap<QString, QTreeWidgetItem *> heads;
    QTreeWidgetItem *pick = nullptr;
    const bool grouped =
        std::any_of(_entries.begin(), _entries.end(), [](const Entry &e) { return e.grp && !e.grp->isEmpty(); });
    lst->setRootIsDecorated(grouped);
    for (const Entry &e : _entries) {
        if (!q.isEmpty() && !py::lower(u"%1 %2"_s.arg(e.key, nm(e.key))).contains(q))
            continue;
        auto *it = new QTreeWidgetItem(QStringList{nm(e.key), e.key});
        it->setData(0, Qt::UserRole, QStringList{e.rel} + e.path);
        it->setToolTip(0, u"%1 — %2"_s.arg(e.key, e.rel));
        it->setForeground(1, QBrush(QColor(theme::INK_DIM)));
        if (grouped) {
            const bool has = e.grp && !e.grp->isEmpty();
            const QString gk = has ? *e.grp : spec.title;
            if (!heads.contains(gk)) {
                auto *h = new QTreeWidgetItem(QStringList{has ? nm(gk) : gk, QString()});
                h->setFont(0, bold);
                h->setForeground(0, QBrush(QColor(theme::GOLD)));
                h->setFlags(h->flags() & ~Qt::ItemIsSelectable);
                lst->addTopLevelItem(h);
                h->setExpanded(true);
                heads.insert(gk, h);
            }
            (*heads.get(gk))->addChild(it);
        } else {
            lst->addTopLevelItem(it);
        }
        if (cur && *cur == std::pair{e.rel, e.path})
            pick = it;
    }
    if (pick) {
        lst->setCurrentItem(pick);
        lst->scrollToItem(pick);
    }
    lst->blockSignals(false);
}

void EntryView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QVariant d = it ? it->data(0, Qt::UserRole) : QVariant();
    if (d.isValid()) {
        const QStringList l = d.toStringList();
        if (!l.isEmpty()) {
            cur = std::pair{l[0], l.mid(1)};
            _fill_detail();
        }
    }
}

const Entry *EntryView::_entry() const
{
    if (!cur)
        return nullptr;
    for (const Entry &e : _entries)
        if (e.rel == cur->first && e.path == cur->second)
            return &e;
    return nullptr;
}

// ------------------------------------------------------------------ the one shown
void EntryView::_fill_detail()
{
    const Entry *ep = store ? _entry() : nullptr;
    names_host->setVisible(ep != nullptr);
    body_host->setVisible(ep != nullptr);
    big_btn->setVisible(ep != nullptr);
    if (!ep) {
        info->hide();
        head->setText(QString());
        head->setStyleSheet(QString());
        title->setText(u"Pick a %1"_s.arg(spec.noun));
        sub->setText(QString());
        _sig.reset();
        return;
    }
    const Entry e = *ep;
    const QString &key = e.key, &rel = e.rel;
    const QStringList &path = e.path;
    const QString text = store->text(rel);
    head->setText(u"Selected %1: %2"_s.arg(spec.noun, nm(key)));
    head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; padding: 5px 10px; "
                        u"border-radius: 4px; }"_s.arg(theme::GOLD));
    title->setText(nm(key));
    sub->setText(u"%1 · %2%3"_s.arg(key, rel,
                                    path.size() > 1 ? u" › "_s + path.mid(0, path.size() - 1).join(u" › "_s) : QString()));
    const QString said = spec.info ? spec.info(*this, key, rel, path) : QString();
    info->setText(said);
    info->setVisible(!said.isEmpty());
    for (const auto &[sfx, ed] : name_edits)
        if (!ed->hasFocus()) {
            ed->setText(loc(key + sfx).value_or(QString()));
            ed->setPlaceholderText(u"No localisation present — type one to add it"_s);
        }
    const QString inner = blocks::block_text(text, path).value_or(QString());
    const std::tuple<QString, QStringList, QString> sig{rel, path, inner};
    if (_sig && *_sig == sig)
        return;
    _sig = sig;
    QWidget *old = body;
    body = new QWidget(body_host);                    // built under its parent, swapped in at once
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    _whole = nullptr;
    const std::vector<script::Node> kids = blocks::children_top(text, path);
    OrderedMap<QString, long long> count;
    for (const script::Node &c : kids)
        count[kkey(c)] += 1;
    std::vector<std::pair<QString, QString>> plain;
    QStringList nested, repeated;
    for (const script::Node &c : kids) {
        if (!c.is_block && *count.get(kkey(c)) == 1)
            plain.emplace_back(kkey(c), c.value);
        if (c.is_block && *count.get(kkey(c)) == 1)
            nested << kkey(c);
    }
    for (const auto &[k, n] : count)
        if (n > 1)
            repeated << k;
    std::sort(repeated.begin(), repeated.end());
    if (!plain.empty() || spec.add)
        v->addWidget(_values(path, rel, key, plain));
    if (!repeated.isEmpty())
        v->addWidget(label(u"Repeated inside it (%1): change those in the whole text below."_s.arg(repeated.join(u", "_s)),
                           u"faint"_s),
                     0);
    for (qsizetype i = 0; i < std::min<qsizetype>(14, nested.size()); ++i)
        v->addWidget(_block_box(rel, path + QStringList{nested[i]}, nested[i]));
    if (nested.size() > 14)
        v->addWidget(label(u"… and %1 more blocks: in the whole text below."_s.arg(nested.size() - 14), u"faint"_s));
    v->addWidget(_whole_box(rel, path, key, inner));
    if (spec.remove && _removable(rel, path)) {
        auto *rm = new QPushButton(u"✕ Remove this %1"_s.arg(spec.noun), body);
        connect(rm, &QPushButton::clicked, this, [this, rel, path, key] { _remove(rel, path, key); });
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 10, 0, 0);
        row->addWidget(rm);
        row->addStretch(1);
        v->addLayout(row);
    }
    body_lay->addWidget(body);
    if (old) {
        body_lay->removeWidget(old);
        old->hide();
        old->setParent(nullptr);
        old->deleteLater();
    }
}

void EntryView::_open_big()
{
    if (const Entry *e = _entry())
        Q_EMIT bigRequested(e->rel, e->path, spec.noun);
}

QWidget *EntryView::_values(const QStringList &path, const QString &rel, const QString &key,
                            const std::vector<std::pair<QString, QString>> &plain)
{
    auto *w = new QWidget(body);
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 6, 0, 8);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(0, 1);
    g->addWidget(_bold(u"Its values"_s, w), 0, 0, 1, 3);
    int r = 1;
    for (const auto &[k, val0] : plain) {
        const QString val = py::strip(val0);
        auto *lb = new QLabel(modpanel::attr_name(mod, k), w);
        lb->setObjectName(u"sub"_s);
        lb->setToolTip(k);
        g->addWidget(lb, r, 0);
        const OptStr sprite = is_digits(val) ? strip_of_field(rel, k) : std::nullopt;
        QWidget *pk = nullptr;
        if (sprite && doc && !iconstrips::frames(iconstrips::strips_for(mod, *doc), *sprite).empty()) {
            auto *ip = new iconstrips::IconPicker(iconstrips::strips_for(mod, *doc), *sprite, py::to_int(val), w);
            connect(ip, &iconstrips::IconPicker::picked, this, [this, rel, path, key, kk = k](long long n) {
                _change(u"%1: %2 = %3"_s.arg(nm(key), kk).arg(n), rel, [path, kk, n](const QString &t) {
                    return blocks::set_value(t, path, kk, QString::number(n));
                });
            });
            pk = ip;
        }
        if (pk) {
            pk->setFixedWidth(110);
            g->addWidget(pk, r, 1, Qt::AlignRight);
        } else if (val == u"yes" || val == u"no") {
            auto *cb = new QCheckBox(val == u"yes" ? u"yes"_s : u"no"_s, w);
            cb->setChecked(val == u"yes");
            connect(cb, &QCheckBox::toggled, this, [this, rel, path, key, kk = k](bool on) {
                const QString yn = on ? u"yes"_s : u"no"_s;
                _change(u"%1: %2 %3"_s.arg(nm(key), kk, yn), rel,
                        [path, kk, yn](const QString &t) { return blocks::set_value(t, path, kk, yn); });
            });
            g->addWidget(cb, r, 1);
        } else {
            auto *ed = new QLineEdit(val, w);
            const bool num = modifiers::num_ok(val);
            ed->setFixedWidth(num ? 110 : 240);
            if (num)
                ed->setAlignment(Qt::AlignRight);
            connect(ed, &QLineEdit::editingFinished, this,
                    [this, rel, path, key, kk = k, ed, old = val] { _set(rel, path, key, kk, ed, old); });
            g->addWidget(ed, r, 1, Qt::AlignRight);
        }
        auto *x = new QPushButton(u"✕"_s, w);
        x->setFixedWidth(34);
        x->setToolTip(u"Take “%1” off"_s.arg(k));
        connect(x, &QPushButton::clicked, this, [this, rel, path, key, kk = k] {
            _change(u"%1: %2 taken off"_s.arg(nm(key), kk), rel,
                    [path, kk](const QString &t) { return blocks::del_value(t, path, kk); });
        });
        g->addWidget(x, r, 2);
        r += 1;
    }
    if (r == 1) {
        g->addWidget(label(u"none"_s, u"faint"_s), r, 0);
        r += 1;
    }
    if (spec.add) {
        auto *nk = new QLineEdit(w);
        nk->setPlaceholderText(u"Add a value — its key (e.g. %1)"_s.arg(spec.example.value_or(u"badboy_factor"_s)));
        g->addWidget(nk, r, 0);
        auto *nv = new QLineEdit(w);
        nv->setPlaceholderText(u"value"_s);
        nv->setFixedWidth(110);
        g->addWidget(nv, r, 1, Qt::AlignRight);
        auto *add = new QPushButton(u"＋"_s, w);
        add->setFixedWidth(34);
        auto do_add = [this, nk, nv, rel, path, key] {
            const QString k = py::strip(nk->text()), val = py::strip(nv->text());
            if (!key_ok(k)) {
                warn->setText(u"The key needs letters, digits or _ (no spaces)."_s);
                return;
            }
            if (val.isEmpty() || has_any(val, u"{}\n"_s)) {
                warn->setText(u"Give it a value (a number, yes / no or a word)."_s);
                return;
            }
            warn->setText(QString());
            const QString *o = store->orig.get(rel);
            const OptStr like = o ? OptStr(*o) : std::nullopt;
            _change(u"%1: %2 = %3"_s.arg(nm(key), k, val), rel,
                    [path, k, val, like](const QString &t) { return blocks::set_value(t, path, k, val, like); });
        };
        connect(add, &QPushButton::clicked, this, do_add);
        connect(nv, &QLineEdit::returnPressed, this, do_add);
        g->addWidget(add, r, 2);
    }
    return w;
}

QPlainTextEdit *EntryView::_editor(QWidget *parent, const QString &text, long long lines_max)
{
    auto *ed = new QPlainTextEdit(parent);
    ed->setFont(QFont(theme::MONO, 10));
    ed->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ed->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ed->setPlainText(text);
    const long long n = std::max(2LL, std::min(lines_max, static_cast<long long>(text.count(u'\n')) + 2));
    ed->setFixedHeight(int(ed->fontMetrics().lineSpacing() * n + 16));
    return ed;
}

QWidget *EntryView::_block_box(const QString &rel, const QStringList &path, const QString &k)
{
    auto *w = new QWidget(body);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 6, 0, 0);
    v->setSpacing(3);
    auto *row = new QHBoxLayout();
    row->addWidget(_bold(modpanel::attr_name(mod, k), w), 1);
    auto *save = new QPushButton(u"Save “%1”"_s.arg(k), w);
    row->addWidget(save);
    auto *x = new QPushButton(u"✕"_s, w);
    x->setFixedWidth(34);
    x->setToolTip(u"Take the whole block “%1” off"_s.arg(k));
    connect(x, &QPushButton::clicked, this, [this, rel, path, k] {
        const QStringList up = path.mid(0, path.size() - 1);
        _change(u"%1: %2 taken off"_s.arg(path.size() > 1 ? nm(path[path.size() - 2]) : k, k), rel,
                [up, k](const QString &t) { return blocks::del_value(t, up, k); });
    });
    row->addWidget(x);
    v->addLayout(row);
    const QString inner = blocks::block_text(store->text(rel), path).value_or(QString());
    const bool one_line = !py::strip(inner).contains(u'\n');
    QPlainTextEdit *ed =
        _editor(w, !one_line ? issuepanel::reindent(py::strip(inner, u"\n"_s), 0, u"    "_s) : py::strip(inner));
    v->addWidget(ed);
    connect(save, &QPushButton::clicked, this, [this, ed, rel, path, k, one_line] {
        const QString b = ed->toPlainText();
        const qsizetype open = b.count(u'{'), close = b.count(u'}');
        if (open != close) {
            warn->setText(u"“%1”: the braces don't match ({ %2, } %3) — not saved."_s.arg(k).arg(open).arg(close));
            return;
        }
        warn->setText(QString());
        QString neu;
        if (!py::strip(b).contains(u'\n') && one_line)
            neu = u" "_s + py::strip(b) + u" "_s;
        else
            neu = u"\n"_s + issuepanel::reindent(py::strip(b, u"\n"_s), path.size()) + u"\n"_s +
                  QString(u"\t"_s).repeated(path.size() - 1);
        _change(u"%1: %2"_s.arg(title->text(), k), rel,
                [path, neu](const QString &t) { return blocks::set_block(t, path, neu); });
    });
    return w;
}

QWidget *EntryView::_whole_box(const QString &rel, const QStringList &path, const QString &key, const QString &inner)
{
    // the whole entry as text — made when asked for (it can be long)
    auto *w = new QWidget(body);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(3);
    auto *show = new QPushButton(u"Show the whole %1 as text ▾"_s.arg(spec.noun), w);
    v->addWidget(show);
    connect(show, &QPushButton::clicked, this, [this, show, w, v, rel, path, key, inner] {
        show->hide();
        const bool file_level = path.isEmpty();
        QPlainTextEdit *ed =
            _editor(w, file_level ? inner : issuepanel::reindent(py::strip(inner, u"\n"_s), 0, u"    "_s), 34);
        v->addWidget(ed);
        auto *save = new QPushButton(u"Save the whole text"_s, w);
        v->addWidget(save);
        _whole = ed;
        connect(save, &QPushButton::clicked, this, [this, ed, rel, path, key, file_level] {
            const QString b = ed->toPlainText();
            const qsizetype open = b.count(u'{'), close = b.count(u'}');
            if (open != close) {
                warn->setText(u"The braces don't match ({ %1, } %2) — not saved."_s.arg(open).arg(close));
                return;
            }
            warn->setText(QString());
            const QString neu = file_level ? b
                                           : u"\n"_s + issuepanel::reindent(py::strip(b, u"\n"_s), path.size()) +
                                                 u"\n"_s + QString(u"\t"_s).repeated(path.size() - 1);
            _change(u"%1 rewritten"_s.arg(nm(key)), rel,
                    [path, neu](const QString &t) { return blocks::set_block(t, path, neu); });
        });
    });
    return w;
}

// ------------------------------------------------------------------ changes
void EntryView::_change(const QString &what, const QString &rel, const std::function<QString(const QString &)> &fn)
{
    const QString old = store->text(rel);
    const QString neu = fn(old);
    if (neu != old)
        Q_EMIT scriptEdit(what, rel, neu);
}

void EntryView::_set(const QString &rel, const QStringList &path, const QString &key, const QString &k, QLineEdit *ed,
                     const QString &old)
{
    const QString val = py::strip(ed->text());
    if (val == old)
        return;
    if (val.isEmpty() || has_any(val, u"{}\n"_s)) {
        ed->setStyleSheet(RED_BORDER);
        warn->setText(u"“%1” needs a value (a number, yes / no or a word)."_s.arg(k));
        return;
    }
    if (modifiers::num_ok(old) && !modifiers::num_ok(val)) {
        ed->setStyleSheet(RED_BORDER);
        warn->setText(u"“%1” needs a number (it was %2)."_s.arg(k, old));
        return;
    }
    ed->setStyleSheet(QString());
    warn->setText(QString());
    _change(u"%1: %2 = %3"_s.arg(nm(key), k, val), rel,
            [path, k, val](const QString &t) { return blocks::set_value(t, path, k, val); });
}

void EntryView::_loc_done(QLineEdit *ed, const QString &suffix)
{
    const Entry *e = _entry();
    if (!e)
        return;
    const QString t = py::split(ed->text()).join(u' ');
    if (!t.isEmpty() && t != loc(e->key + suffix).value_or(QString()))
        Q_EMIT nameEdit(e->key + suffix, t);
}

bool EntryView::_removable(const QString &rel, const QStringList &path)
{
    // entries inside a file always; a whole file only when it is the mod's own (one the game itself has, and the mod
    // merely inherits, can't be taken out from here)
    return !path.isEmpty() || in_mod(mod, *store, rel);
}

void EntryView::_remove(const QString &rel, const QStringList &path, const QString &key)
{
    const QString name = nm(key);
    cur.reset();
    store->drop_names.clear();                        // its names go with it
    for (const auto &[sfx, _ed] : name_edits)
        store->drop_names << key + sfx;
    const QString what = u"%1 “%2” removed"_s.arg(py::capitalize(spec.noun), name);
    if (!path.isEmpty()) {
        const QStringList up = path.mid(0, path.size() - 1);
        _change(what, rel, [up, key](const QString &t) { return blocks::del_value(t, up, key); });
    } else {
        Q_EMIT fileRemove(what, rel);
    }
}

void EntryView::_make_new()
{
    const QString key = py::strip(new_key->text());
    const Entry *ep = _entry();
    if (!ep) {
        new_warn->setText(u"Pick the one to copy first."_s);
        return;
    }
    if (!key_ok(key)) {
        new_warn->setText(u"The key needs letters, digits or _ (no spaces)."_s);
        return;
    }
    if (std::any_of(_entries.begin(), _entries.end(), [&key](const Entry &x) { return x.key == key; })) {
        new_warn->setText(u"“%1” is there already — pick another key."_s.arg(key));
        return;
    }
    new_warn->setText(QString());
    const Entry e = *ep;
    const QString text = store->text(e.rel);
    new_key->clear();
    auto name_it = [this, key] {
        // its name in the game, written at once (it can be changed in the name box): a key without a name shows as
        // the bare key in the game
        if (name_edits.contains(QString()) && !loc(key))
            Q_EMIT nameEdit(key, py::title(QString(key).replace(u'_', u' ')));
    };
    if (e.path.isEmpty()) {                           // a file of its own, next to the one copied
        const QString new_rel = u"%1/%2.txt"_s.arg(e.rel.left(e.rel.lastIndexOf(u'/')), key);
        cur = std::pair{new_rel, QStringList()};
        Q_EMIT scriptEdit(u"New %1 “%2”"_s.arg(spec.noun, key), new_rel, text);
        name_it();
        return;
    }
    const OptStr bt = blocks::block_text(text, e.path);
    const QString inner = bt && !bt->isEmpty() ? *bt : u"\n"_s;
    const QStringList np = e.path.mid(0, e.path.size() - 1) + QStringList{key};
    cur = std::pair{e.rel, np};
    _change(u"New %1 “%2”"_s.arg(spec.noun, key), e.rel,
            [np, inner](const QString &t) { return blocks::set_block(t, np, inner); });
    name_it();
}

// ---------------------------------------------------------------------------------------------- crimes
std::vector<std::tuple<QString, QString, QString>> crime_uses(const moddata::Mod &mod, scriptstore::ScriptStore &store,
                                                              const QString &key)
{
    std::vector<std::tuple<QString, QString, QString>> out;
    const QRegularExpression rx(uR"(\b(enable_crime|add_crime|remove_crime)\s*=\s*)"_s + QRegularExpression::escape(key) +
                                    uR"(\b)"_s,
                                QRegularExpression::UseUnicodePropertiesOption);
    auto name_of = [](const script::Node &n) -> QString {
        if (n.key == u"country_event" || n.key == u"province_event") {
            for (const script::Node &c : n.kids)
                if (c.has_key && c.key == u"id")
                    return u"event "_s + (c.is_block ? QString() : c.value);
            return u"event ?"_s;
        }
        return n.key;
    };
    for (const QString &folder : {u"inventions"_s, u"technologies"_s, u"events"_s, u"decisions"_s, u"common"_s}) {
        for (const QString &rel : _files(mod, &store, folder)) {
            QString text;
            try {
                text = store.text(rel);
            } catch (const std::exception &) {
                continue;
            }
            if (!text.contains(key) || rel.endsWith(u"crime.txt"))
                continue;
            script::Nodes nodes;
            try {
                nodes = script::parse(text);
            } catch (...) {
                continue;
            }
            for (const script::Node &n : nodes) {
                std::vector<const script::Node *> tops;
                if (n.has_key && n.key == u"political_decisions" && n.is_block) {
                    for (const script::Node &c : n.kids)
                        if (c.is_block)
                            tops.push_back(&c);
                } else {
                    tops.push_back(&n);
                }
                for (const script::Node *b : tops)
                    if (b->has_key && !b->key.isEmpty() && b->is_block) {
                        auto it = rx.globalMatch(text.mid(b->kstart, b->vend - b->kstart));
                        while (it.hasNext())
                            out.emplace_back(rel, name_of(*b), it.next().captured(1));
                    }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- defines.lua
// (LINE_RE, SECT_RE: core/entryview_data)
static const QRegularExpression NUM_RE(uR"(^-?\d+(\.\d+)?$)"_s, QRegularExpression::UseUnicodePropertiesOption);
const QString GameSettingsView::REL = u"common/defines.lua"_s;

GameSettingsView::GameSettingsView(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *s = new Section();
    s->lay->addWidget(label(u"Game settings"_s, u"title"_s));
    s->lay->addWidget(label(u"Every number the game itself runs on (common/defines.lua): start and end date, infamy "
                            u"limits, how fast things grow, what brigades need … Pick one, change its value below. Wrong "
                            u"values can break the game — the note behind each says what it is."_s,
                            u"faint"_s));
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a setting (name, value or note)…"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill(); });
    s->lay->addWidget(search);
    lst = new QTreeWidget();
    lst->setColumnCount(3);
    lst->setHeaderLabels({u"Setting"_s, u"Value"_s, u"What the file says about it"_s});
    lst->setUniformRowHeights(true);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, u"gameSettings"_s, {430, 150});
    connect(lst, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *p) { _picked(it, p); });
    s->lay->addWidget(lst, 1);
    lay->addWidget(s, 1);
    s = new Section();
    head = label(u"Pick a setting"_s, u"title"_s);
    s->lay->addWidget(head);
    note = label(QString(), u"sub"_s);
    s->lay->addWidget(note);
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"Value"_s, u"sub"_s, false));
    value = new QLineEdit();
    value->setFixedWidth(260);
    connect(value, &QLineEdit::returnPressed, this, [this] { _commit(); });
    row->addWidget(value);
    set_btn = new QPushButton(u"Set"_s);
    connect(set_btn, &QPushButton::clicked, this, [this] { _commit(); });
    row->addWidget(set_btn);
    was = label(QString(), u"faint"_s, false);
    row->addWidget(was, 1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    row->addWidget(label(u"Note"_s, u"sub"_s, false));
    note_edit = new QLineEdit();
    note_edit->setPlaceholderText(u"The comment behind this setting in the file — yours to write (what it does, why you "
                                  u"changed it …)"_s);
    connect(note_edit, &QLineEdit::returnPressed, this, [this] { _commit_note(); });
    row->addWidget(note_edit, 1);
    note_btn = new QPushButton(u"Set the note"_s);
    connect(note_btn, &QPushButton::clicked, this, [this] { _commit_note(); });
    row->addWidget(note_btn);
    s->lay->addLayout(row);
    warn = label(QString(), u"sub"_s);
    warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(warn);
    lay->addWidget(s);
}

std::vector<GameSettingsView::Row> GameSettingsView::rows(const QString &text)
{
    // [(section, key, value, note, line index)].
    std::vector<Row> out;
    QStringList stack;
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &ln = lines[i];
        const QString code = py::split(ln, u"--"_s, 1).value(0);
        const auto sm = SECT_RE.match(code);
        if (sm.hasMatch()) {
            stack << sm.captured(1);
            continue;
        }
        if (py::strip(code).startsWith(u'}')) {
            if (!stack.isEmpty())
                stack.removeLast();
            continue;
        }
        const auto m = LINE_RE.match(ln);
        if (m.hasMatch())
            out.push_back(Row{!stack.isEmpty() ? stack.last() : QString(), m.captured(2), py::strip(m.captured(4)),
                              py::strip(m.captured(7)), static_cast<long long>(i)});
    }
    return out;
}

void GameSettingsView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (store)
        _fill();
}

void GameSettingsView::_fill()
{
    const QString text = store->text(REL);
    const QString *o = store->orig.get(REL);
    const std::vector<Row> orig_rows = rows(o ? *o : text);
    std::map<std::pair<QString, QString>, QString> orig, orig_note;
    for (const Row &r : orig_rows) {
        orig[{r.sec, r.key}] = r.value;
        orig_note[{r.sec, r.key}] = r.note;
    }
    const QString q = py::lower(py::strip(search->text()));
    lst->blockSignals(true);
    lst->clear();
    QFont bold(lst->font());
    bold.setBold(true);
    std::map<QString, QTreeWidgetItem *> heads;
    QTreeWidgetItem *pick = nullptr;
    for (const Row &r : rows(text)) {
        if (!q.isEmpty() && !py::lower(u"%1 %2 %3"_s.arg(r.key, r.value, r.note)).contains(q))
            continue;
        if (!heads.contains(r.sec)) {
            const QString cap = py::capitalize(r.sec);
            auto *h = new QTreeWidgetItem(QStringList{r.sec == u"defines" ? u"General"_s : !cap.isEmpty() ? cap : u"General"_s,
                                                      QString(), QString()});
            h->setFont(0, bold);
            h->setForeground(0, QBrush(QColor(theme::GOLD)));
            h->setFlags(h->flags() & ~Qt::ItemIsSelectable);
            lst->addTopLevelItem(h);
            h->setExpanded(true);
            heads[r.sec] = h;
        }
        auto *it = new QTreeWidgetItem(QStringList{r.key, r.value, r.note});
        it->setData(0, Qt::UserRole, QStringList{r.sec, r.key});
        it->setToolTip(2, r.note);
        it->setForeground(2, QBrush(QColor(theme::INK_DIM)));
        const auto ov = orig.find({r.sec, r.key});
        if (ov == orig.end() || ov->second != r.value) {
            it->setForeground(1, QBrush(QColor(theme::GOLD)));
            it->setToolTip(1, u"Changed — in the file: %1"_s.arg(ov != orig.end() ? ov->second : u"—"_s));
        }
        const auto on = orig_note.find({r.sec, r.key});
        const QString was_note = on != orig_note.end() ? on->second : QString();
        if (was_note != r.note) {
            it->setForeground(2, QBrush(QColor(theme::GOLD)));
            it->setToolTip(2, u"%1\nChanged — in the file: %2"_s.arg(r.note, !was_note.isEmpty() ? was_note : u"no note"_s));
        }
        heads[r.sec]->addChild(it);
        if (cur && cur->first == r.sec && cur->second == r.key)
            pick = it;
    }
    if (pick)
        lst->setCurrentItem(pick);
    lst->blockSignals(false);
    _show();
}

void GameSettingsView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QStringList d = it ? it->data(0, Qt::UserRole).toStringList() : QStringList();
    if (d.size() == 2) {
        cur = std::pair{d[0], d[1]};
        _show();
    }
}

std::optional<GameSettingsView::Row> GameSettingsView::_row()
{
    if (!cur || !store)
        return std::nullopt;
    for (const Row &r : rows(store->text(REL)))
        if (r.sec == cur->first && r.key == cur->second)
            return r;
    return std::nullopt;
}

void GameSettingsView::_show()
{
    const auto r = _row();
    value->setEnabled(r.has_value());
    set_btn->setEnabled(r.has_value());
    note_edit->setEnabled(r.has_value());
    note_btn->setEnabled(r.has_value());
    if (!r) {
        head->setText(u"Pick a setting"_s);
        note->setText(QString());
        was->setText(QString());
        note_edit->setText(QString());
        return;
    }
    head->setText(r->key);
    note->setText(!r->note.isEmpty() ? r->note : u"The file has no note for this one — write one below."_s);
    if (!value->hasFocus())
        value->setText(r->value);
    if (!note_edit->hasFocus())
        note_edit->setText(r->note);
    std::optional<QString> orig;
    const QString *o = store->orig.get(REL);
    for (const Row &x : rows(o ? *o : QString()))
        if (x.sec == cur->first && x.key == cur->second) {
            orig = x.value;
            break;
        }
    was->setText(orig && *orig != r->value ? u"in the file: %1"_s.arg(*orig) : QString());
}

void GameSettingsView::_commit()
{
    const auto r = _row();
    if (!r)
        return;
    const QString v = py::strip(value->text());
    if (v.isEmpty() || v.contains(u',') || v.contains(u'{') || v.contains(u'}') || v.contains(u'\n') || v.contains(u"--"_s)) {
        warn->setText(u"Give it one value (a number, or a date in quotes like '1836.1.1')."_s);
        return;
    }
    if (NUM_RE.match(r->value).hasMatch() && !NUM_RE.match(v).hasMatch()) {
        warn->setText(u"“%1” needs a number (it is %2)."_s.arg(r->key, r->value));
        return;
    }
    warn->setText(QString());
    if (v == r->value)
        return;
    QStringList lines = store->text(REL).split(u'\n');
    const auto m = LINE_RE.match(lines[r->line]);
    lines[r->line] = m.captured(1) + m.captured(2) + m.captured(3) + v +
                     (!m.captured(5).isEmpty() ? m.captured(5) : u","_s) + m.captured(6);
    Q_EMIT scriptEdit(u"%1 = %2"_s.arg(r->key, v), REL, lines.join(u'\n'));
}

void GameSettingsView::_commit_note()
{
    // The comment behind the setting (-- … to the end of its line) rewritten; an empty one takes it off.
    const auto r = _row();
    if (!r)
        return;
    const QString nt = py::split(note_edit->text()).join(u' ');
    if (nt == r->note)
        return;
    warn->setText(QString());
    QStringList lines = store->text(REL).split(u'\n');
    const auto m = LINE_RE.match(lines[r->line]);
    const QString sep = m.captured(5);           // the comma (the last line of a table has none) and space
    const QString hd = m.captured(1) + m.captured(2) + m.captured(3) + m.captured(4);
    if (!nt.isEmpty())
        lines[r->line] = hd + (!py::strip(sep, u","_s).isEmpty() ? sep : sep + u"\t"_s) + u"-- "_s + nt;
    else
        lines[r->line] = hd + py::rstrip(sep);
    Q_EMIT scriptEdit(u"%1: note %2"_s.arg(r->key, !nt.isEmpty() ? u"“%1”"_s.arg(nt) : u"taken off"_s), REL,
                      lines.join(u'\n'));
}

// ---------------------------------------------------------------------------------------------- a workspace of pages
namespace {
// The row of page buttons. (C++ only, 2026-11-17: with Building types on Game rules the one row of 14 buttons made the
// whole main window at least 1681 px wide.) One row while it fits — laid out exactly as before — else the buttons go
// onto as many rows as they need, each row filled evenly; the bar never asks for more width than its widest button.
class SegBar : public QWidget {
public:
    explicit SegBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        _v = new QVBoxLayout(this);
        _v->setContentsMargins(16, 10, 16, 8);
        _v->setSpacing(0);
    }
    void add(QPushButton *b)
    {
        _btns << b;
        _rows = 0;
        _arrange(1);
    }
    QSize minimumSizeHint() const override
    {
        int w = 0;
        for (QPushButton *b : _btns)
            w = std::max(w, b->sizeHint().width());
        const QSize m = QWidget::minimumSizeHint();
        return {w + 32, m.height()};
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        const int room = std::max(1, width() - 32);
        int n = 1;
        while (n < _btns.size() && _need_most(n) > room)
            ++n;
        if (n != _rows)
            _arrange(n);
    }

private:
    QVBoxLayout *_v;
    QList<QPushButton *> _btns;
    QList<QHBoxLayout *> _hs;
    int _rows = 0;
    // the widest of n rows (the buttons in order, split as evenly as their count allows)
    int _need_most(int n) const
    {
        int most = 0;
        for (int r = 0; r < n; ++r) {
            int w = 0;
            for (int i = int(_btns.size()) * r / n; i < int(_btns.size()) * (r + 1) / n; ++i)
                w += _btns[i]->sizeHint().width();
            most = std::max(most, w);
        }
        return most;
    }
    void _arrange(int n)
    {
        for (QHBoxLayout *h : _hs) {
            while (h->count())
                delete h->takeAt(0);                 // (the layout items only: the buttons stay)
            _v->removeItem(h);
            delete h;
        }
        _hs.clear();
        for (int r = 0; r < n; ++r) {
            auto *h = new QHBoxLayout();
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(0);
            for (int i = int(_btns.size()) * r / n; i < int(_btns.size()) * (r + 1) / n; ++i)
                h->addWidget(_btns[i], 1);
            _v->addLayout(h);
            _hs << h;
        }
        _rows = n;
    }
};
}  // namespace

PagesWorkspace::PagesWorkspace(moddata::Mod &mod_, std::vector<PageDef> pages_, QWidget *parent)
    : QWidget(parent), mod(mod_), pages(std::move(pages_))
{
    page = pages.front().key;
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new SegBar();
    bar->setObjectName(u"side"_s);
    auto *grp = new QButtonGroup(this);
    for (const PageDef &pd : pages) {
        auto *b = new widgets::BadgeButton(pd.text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(pd.key == page);
        connect(b, &QPushButton::clicked, this, [this, k = pd.key] { set_page(k); });
        grp->addButton(b);
        bar->add(b);
        seg.insert(pd.key, b);
    }
    lay->addWidget(bar);
    stack = new QStackedWidget();
    lay->addWidget(stack, 1);
}

QWidget *PagesWorkspace::view(const QString &key)
{
    if (QWidget *const *have = views.get(key))
        return *have;
    const auto pd = std::find_if(pages.begin(), pages.end(), [&](const PageDef &x) { return x.key == key; });
    QWidget *v = pd->make();
    views.insert(key, v);
    const QMetaObject *mo = v->metaObject();       // (Python: hasattr(v, "fileRemove") …)
    const std::pair<const char *, const char *> sigs[4] = {
        {SIGNAL(scriptEdit(QString,QString,QString)), "scriptEdit(QString,QString,QString)"},
        {SIGNAL(nameEdit(QString,QString)), "nameEdit(QString,QString)"},
        {SIGNAL(fileRemove(QString,QString)), "fileRemove(QString,QString)"},
        {SIGNAL(bigRequested(QString,QStringList,QString)), "bigRequested(QString,QStringList,QString)"}};
    for (const auto &[sig, norm] : sigs)
        if (mo->indexOfSignal(norm) >= 0)
            connect(v, sig, this, sig);
    stack->addWidget(v);
    return v;
}

void PagesWorkspace::set_counts(const std::map<QString, std::pair<long long, long long>> &counts)
{
    for (const auto &[k, b] : seg) {
        const auto it = counts.find(k);
        const auto [e, w] = it != counts.end() ? it->second : std::pair<long long, long long>{0, 0};
        b->set_counts(e, w);
    }
}

void PagesWorkspace::set_page(const QString &key)
{
    page = key;
    for (const auto &[k, b] : seg)
        b->setChecked(k == key);
    QWidget *v = view(key);
    stack->setCurrentWidget(v);
    if (doc)
        std::find_if(pages.begin(), pages.end(), [&](const PageDef &x) { return x.key == key; })->refresh(v, doc);
}

void PagesWorkspace::refresh(document::Document *doc_)
{
    doc = doc_;
    QWidget *v = view(page);
    stack->setCurrentWidget(v);
    std::find_if(pages.begin(), pages.end(), [&](const PageDef &x) { return x.key == page; })->refresh(v, doc);
}

}  // namespace entryview