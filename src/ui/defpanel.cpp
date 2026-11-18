// defpanel.py — government forms, national values, technology schools (see defpanel.h).
#include "ui/defpanel.h"
#include "core/blocks.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include "core/modifiers.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "map/iconstrips_data.h"
#include "ui/colsizes.h"
#include "ui/issuepanel.h"
#include "ui/modpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
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
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;
using widgets::BadgeButton;
using widgets::GridFlow;
using widgets::label;
using widgets::Section;

namespace defpanel {

const std::vector<std::pair<QString, QString>> GOV_FLAGS = {
    {QString(), u"The nation's normal flag"_s}, {u"monarchy"_s, u"Monarchy flag"_s},
    {u"republic"_s, u"Republic flag"_s},        {u"communist"_s, u"Communist flag"_s},
    {u"fascist"_s, u"Fascist flag"_s}};

const Spec GOVERNMENTS = {
    u"common/governments.txt"_s, {}, u"government"_s, u"government form"_s, u"Government forms"_s,
    u"government"_s, u"governmentList"_s, {{QString(), u"Name in the game"_s}, {u"_ruler"_s, u"The ruler's title"_s}},
    u"Merchant Republic"_s, std::nullopt, std::nullopt,
    u"The forms of government (common/governments.txt): which parties may rule under each, whether there are "
    u"elections and how often, and which flag the nation flies. Make a new one as a copy of an existing one; a nation "
    u"gets it on the Nation page, or through events, decisions and reforms."_s,
    {}};
const Spec NATIONAL_VALUES = {
    u"common/nationalvalues.txt"_s, {}, u"modifiers"_s, u"national value"_s, u"National values"_s,
    u"nationalvalue"_s, u"nvList"_s, {{QString(), u"Name in the game"_s}}, u"Glory"_s, u"nv_"_s,
    u"GFX_politics_nationalvalue"_s,
    u"The national values (common/nationalvalues.txt): what each one gives the nations that have it. Change the "
    u"numbers, add or take off effects, or make a new one; a nation gets it on its page in the Nations tab. Its "
    u"picture in the game is the one at its place in the list (the first value shows picture 1 of the icon strip …): "
    u"Library → Icon strips shows them and adds pictures for new values."_s,
    {}};
const Spec SCHOOLS = {
    u"common/technology.txt"_s, {u"schools"_s}, u"modifiers"_s, u"technology school"_s, u"Technology schools"_s,
    u"schools"_s, u"schoolList"_s, {{QString(), u"Name in the game"_s}}, u"Naval Academy"_s, std::nullopt,
    std::nullopt,
    u"The technology schools (the “schools” block of common/technology.txt): each nation has one; it changes how "
    u"fast the nation researches each kind of technology (and for uncivs what their reforms cost). Change the numbers "
    u"or make a new school; a nation gets it on the Nation page."_s,
    {u"army_tech_research_bonus"_s, u"navy_tech_research_bonus"_s, u"commerce_tech_research_bonus"_s,
     u"culture_tech_research_bonus"_s, u"industry_tech_research_bonus"_s, u"unciv_economic_modifier"_s,
     u"unciv_military_modifier"_s}};

namespace {

QString kkey(const script::Node &c) { return c.has_key ? c.key : QString(); }
QString under(const QString &k) { return QString(k).replace(u'_', u' '); }

OrderedMap<QString, QString> vals_of(const std::vector<script::Node> &kids)   // {k: v.strip() … if not b}
{
    OrderedMap<QString, QString> vals;
    for (const script::Node &c : kids)
        if (!c.is_block)
            vals.insert(kkey(c), py::strip(c.value));
    return vals;
}

const QString RED_BORDER = u"QLineEdit { border: 2px solid #ff786e; }"_s;

}  // namespace

QStringList ideologies(scriptstore::ScriptStore &store)
{
    // [ideology] in the file's order (common/ideologies.txt: groups of ideologies)
    script::Nodes nodes;
    try {
        nodes = script::parse(store.text(u"common/ideologies.txt"_s));
    } catch (...) {
        return {};
    }
    QStringList out;
    for (const script::Node &g : nodes)
        if (g.is_block)
            for (const script::Node &c : g.kids)
                if (c.has_key && !c.key.isEmpty() && c.is_block)
                    out << c.key;
    return out;
}

DefView::DefView(moddata::Mod &mod_, const Spec &spec_, QWidget *parent)
    : QWidget(parent), mod(mod_), spec(spec_), rel(spec_.rel), base(spec_.base)
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
    strip_warn = label(QString(), u"sub"_s);
    strip_warn->setStyleSheet(u"color: #ff786e; font-weight: 600;"_s);
    strip_warn->hide();
    s->lay->addWidget(strip_warn);
    search = new QLineEdit();
    search->setPlaceholderText(u"Find a %1 (name, key, effect)…"_s.arg(spec.noun));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this] { _fill_list(); });
    s->lay->addWidget(search);
    lst = new QTreeWidget();
    lst->setColumnCount(3);
    lst->setHeaderLabels({py::capitalize(spec.noun), u"What it does"_s, u"Nations"_s});
    lst->setRootIsDecorated(false);
    lst->setUniformRowHeights(true);
    lst->setSelectionMode(QAbstractItemView::SingleSelection);
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setTextElideMode(Qt::ElideRight);
    colsizes::make_resizable(lst, spec.cols, {std::nullopt, 330, 80}, true);
    connect(lst, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) { _picked(it, prev); });
    s->lay->addWidget(lst, 1);
    lay->addWidget(s, 1);
    // --- a new one
    s = new Section();
    QLabel *h = label(u"A new %1"_s.arg(spec.noun), u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    s->lay->addWidget(h);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    g->setColumnStretch(1, 1);
    g->addWidget(label(u"Name in the game"_s, u"sub"_s, false), 0, 0);
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"e.g. %1"_s.arg(spec.example.value_or(u"…"_s)));
    connect(new_name, &QLineEdit::textEdited, this, [this](const QString &t) { _name_typed(t); });
    g->addWidget(new_name, 0, 1);
    g->addWidget(label(u"Its key in the files"_s, u"sub"_s, false), 1, 0);
    new_key = new QLineEdit();
    new_key->setPlaceholderText(u"letters, digits, _  (made from the name)"_s);
    connect(new_key, &QLineEdit::textEdited, this, [this] { _key_typed = true; });
    g->addWidget(new_key, 1, 1);
    g->addWidget(label(u"A copy of"_s, u"sub"_s, false), 2, 0);
    new_from = new QComboBox();
    new_from->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    g->addWidget(new_from, 2, 1);
    s->lay->addLayout(g);
    new_warn = label(QString(), u"sub"_s);
    new_warn->setStyleSheet(u"color: #ff786e;"_s);
    s->lay->addWidget(new_warn);
    auto *b = new QPushButton(u"＋ Make a new %1"_s.arg(spec.noun));
    connect(b, &QPushButton::clicked, this, [this] { _make_new(); });
    s->lay->addWidget(b);
    lay->addWidget(s);
    // --- the one picked
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
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 6);
    split->setSizes({760, 840});
    s = detail = new Section();
    head = new QLabel();
    head->setWordWrap(true);
    head->setTextFormat(Qt::PlainText);
    s->lay->addWidget(head);
    title = label(u"Pick a %1"_s.arg(spec.noun), u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    s->lay->addWidget(sub);
    pic_note = label(QString(), u"faint"_s);
    pic_note->hide();
    s->lay->addWidget(pic_note);
    g = new QGridLayout();
    g->setColumnStretch(1, 1);
    int r = 0;
    for (const auto &[suffix, txt] : spec.names) {
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
    rows_host = new QWidget();
    rows = new QVBoxLayout(rows_host);
    rows->setContentsMargins(0, 0, 0, 0);
    s->lay->addWidget(rows_host);
    rl->addWidget(s);
    rl->addStretch(1);
}

// ------------------------------------------------------------------ data
QString DefView::text() { return store->text(rel); }

void DefView::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    _holders.reset();
    _fill_list();
    _fill_detail();
}

QStringList DefView::keys()
{
    QStringList out;
    try {
        for (const script::Node &c : blocks::children_top(text(), base))
            if (c.is_block)
                out << kkey(c);
    } catch (...) {
        return {};
    }
    return out;
}

OptStr DefView::loc(const QString &key)
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

QString DefView::nm(const QString &key)
{
    const QString t = py::strip(issuepanel::strip_colours(loc(key).value_or(QString())));
    return !t.isEmpty() ? t : py::capitalize(under(key));
}

const OrderedMap<QString, QStringList> &DefView::holders()
{
    if (!_holders) {
        OrderedMap<QString, QStringList> h;
        if (doc && doc->maps.contains(u"nations"_s)) {
            auto &nat = doc->map<countryhist::NationEdits>(u"nations"_s);
            std::set<QString> owners;
            for (const auto &[pid, pr] : mod.provinces)
                if (!pr.sea) {
                    const OptStr &o = doc->prov_state(pid).owner;
                    if (o && !o->isEmpty())
                        owners.insert(*o);
                }
            for (const QString &t : owners) {
                OptStr v;
                try {
                    v = nat.state(t).at(spec.state);
                } catch (...) {
                    continue;
                }
                if (v && !v->isEmpty())
                    h[*v].append(t);
            }
        }
        _holders = std::move(h);
    }
    return *_holders;
}

QString DefView::summary(const QString &key)
{
    const std::vector<script::Node> kids = blocks::children(text(), base + QStringList{key});
    if (spec.kind == u"government") {
        const OrderedMap<QString, QString> vals = vals_of(kids);
        const QStringList ideo_list = ideologies(*store);
        const std::set<QString> ideos(ideo_list.begin(), ideo_list.end());
        QStringList rule;
        for (const auto &[k, v] : vals)
            if (ideos.contains(k) && v == u"yes")
                rule << nm(k);
        const QString el = vals.value(u"election"_s) == u"yes"
                               ? u"elections every %1 months"_s.arg(vals.value(u"duration"_s, u"?"_s))
                               : u"no elections"_s;
        return u"%1 · %2"_s.arg(el, !rule.isEmpty() ? rule.join(u", "_s) : u"no party may rule"_s);
    }
    QStringList out;
    for (const script::Node &c : kids)
        if (!c.is_block && modifiers::num_ok(py::strip(c.value))) {
            const double fv = py::to_float(c.value).value_or(0.0);
            if (fv != 0)
                out << u"%1 %2%3"_s.arg(modpanel::attr_name(mod, kkey(c)), fv > 0 ? u"+"_s : QString(),
                                       py::strip(c.value));
        }
    return !out.isEmpty() ? out.join(u", "_s) : u"nothing"_s;
}

void DefView::_fill_list()
{
    if (!store)
        return;
    const QString q = py::lower(py::strip(search->text()));
    const QStringList ks = keys();
    const OrderedMap<QString, QStringList> &hold = holders();
    lst->blockSignals(true);
    lst->clear();
    QTreeWidgetItem *pick = nullptr;
    for (const QString &k : ks) {
        const QString summ = summary(k);
        if (!q.isEmpty() && !py::lower(u"%1 %2 %3"_s.arg(k, nm(k), summ)).contains(q))
            continue;
        const QStringList *hl = hold.get(k);
        const qsizetype n = hl ? hl->size() : 0;
        auto *it = new QTreeWidgetItem(QStringList{nm(k), summ, n ? QString::number(n) : u"—"_s});
        it->setData(0, Qt::UserRole, k);
        it->setToolTip(0, k);
        it->setToolTip(1, summ);
        it->setTextAlignment(2, Qt::AlignCenter);
        it->setForeground(1, QBrush(QColor(theme::INK_DIM)));
        lst->addTopLevelItem(it);
        if (cur && k == *cur)
            pick = it;
    }
    if (pick) {
        lst->setCurrentItem(pick);
        lst->scrollToItem(pick);
    }
    lst->blockSignals(false);
    const QVariant was = new_from->currentData();
    new_from->blockSignals(true);
    new_from->clear();
    for (const QString &k : ks)
        new_from->addItem(nm(k), k);
    int i = -1;
    if (was.isValid() && ks.contains(was.toString()))
        i = new_from->findData(was.toString());
    else if (cur)
        i = new_from->findData(*cur);
    new_from->setCurrentIndex(std::max(0, i));
    new_from->blockSignals(false);
}

void DefView::_picked(QTreeWidgetItem *it, QTreeWidgetItem *)
{
    const QString k = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (!k.isEmpty()) {
        cur = k;
        _fill_detail();
        const int i = new_from->findData(k);
        if (i >= 0)
            new_from->setCurrentIndex(i);
    }
}

// ------------------------------------------------------------------ the one shown
void DefView::_clear()
{
    while (rows->count()) {
        QLayoutItem *item = rows->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

void DefView::_fill_detail()
{
    const bool ok = store && cur && keys().contains(*cur);
    names_host->setVisible(ok);
    rows_host->setVisible(ok);
    if (!ok) {
        head->setText(QString());
        head->setStyleSheet(QString());
        title->setText(u"Pick a %1"_s.arg(spec.noun));
        sub->setText(QString());
        _sig.reset();
        return;
    }
    const QString key = *cur;
    head->setText(u"Selected %1: %2"_s.arg(spec.noun, nm(key)));
    head->setStyleSheet(u"QLabel { background: %1; color: #14100a; font-weight: 700; padding: 5px 10px; "
                        u"border-radius: 4px; }"_s.arg(theme::GOLD));
    title->setText(nm(key));
    sub->setText(u"%1 · %2%3"_s.arg(key, rel, !base.isEmpty() ? u" › "_s + base.join(u" › "_s) : QString()));
    {   // its picture, in words; on a new one a reminder (C++ only, the user 2026-11-16)
        QString words;
        if (spec.strip && doc) {
            const QStringList ks = keys();
            words = iconstrips::picture_words(iconstrips::strips_for(mod, *doc), *spec.strip,
                                              static_cast<long long>(ks.indexOf(key)) + 1) +
                    u" A national value shows the picture at its place in the list. To change it or put your own in: "
                    u"Game rules → National values, ▦ Pictures."_s;
        } else if (spec.noun == u"technology school") {
            words = u"Its picture: a technology school has none of its own. The game's technology window writes the "
                    u"school's name and, for each research bonus, that folder's icon (sprite GFX_tech_folder_strip) "
                    u"with a + or − (GFX_tech_plusminus) - the same pictures for every school. They can be changed in "
                    u"Library → Icon strips, but every school changes with them."_s;
        }
        pic_note->setVisible(!words.isEmpty());
        if (key == fresh && !words.isEmpty()) {
            pic_note->setText(u"New %1 - remember its picture. %2"_s.arg(spec.noun, words));
            pic_note->setStyleSheet(u"QLabel { background: %1; color: #14100a; padding: 6px 10px; border-radius: 4px; }"_s
                                        .arg(theme::GOLD));
        } else {
            pic_note->setText(words);
            pic_note->setStyleSheet(QString());
        }
    }
    for (const auto &[sfx, ed] : name_edits)
        if (!ed->hasFocus()) {
            ed->setText(loc(key + sfx).value_or(QString()));
            ed->setPlaceholderText(u"No localisation present — type one to add it"_s);
        }
    const std::vector<script::Node> kids = blocks::children(text(), base + QStringList{key});
    const QStringList *hp = holders().get(key);
    const QStringList hold = hp ? *hp : QStringList();
    Sig sig{key, {}, hold.size(), std::nullopt};
    for (const script::Node &c : kids)
        sig.kids.emplace_back(kkey(c), c.is_block);
    if (spec.kind == u"government")
        sig.ideos = ideologies(*store);
    if (_sig && *_sig == sig) {
        const OrderedMap<QString, QString> vals = vals_of(kids);
        for (const auto &[k, w] : _edits) {
            if (auto *le = qobject_cast<QLineEdit *>(w)) {
                if (!le->hasFocus() && vals.contains(k)) {
                    le->setText(*vals.get(k));
                    le->setStyleSheet(QString());
                }
            } else if (auto *cb = qobject_cast<QCheckBox *>(w)) {
                cb->blockSignals(true);
                cb->setChecked(vals.value(k) == u"yes");
                cb->blockSignals(false);
            } else if (auto *co = qobject_cast<QComboBox *>(w)) {
                co->blockSignals(true);
                co->setCurrentIndex(std::max(0, co->findData(vals.value(k, QString()))));
                co->blockSignals(false);
            }
        }
        _sync_gov(vals);
        return;
    }
    _sig = sig;
    _edits.clear();
    _clear();
    if (spec.kind == u"government")
        _gov_rows(key, kids);
    else
        _num_rows(key, kids);
    QStringList names;
    for (const QString &t : hold) {
        const moddata::Country *c = mod.countries.get(t);
        names << (c ? c->name : t);
    }
    const QString who = !hold.isEmpty()
                            ? names.mid(0, 40).join(u", "_s) + (names.size() > 40 ? u" …"_s : QString())
                            : u"none"_s;
    rows->addWidget(label(u"Nations with it at the start (%1): %2"_s.arg(hold.size()).arg(who), u"sub"_s));
    auto *rm = new QPushButton(u"✕ Remove this %1"_s.arg(spec.noun));
    rm->setEnabled(hold.isEmpty());
    connect(rm, &QPushButton::clicked, this, [this, key] { _remove(key); });
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 10, 0, 0);
    row->addWidget(rm);
    row->addStretch(1);
    auto *host = new QWidget();
    host->setLayout(row);
    rows->addWidget(host);
    if (!hold.isEmpty())
        rows->addWidget(label(u"It can't be removed while nations start with it — give them another one first "
                              u"(Nation page)."_s,
                              u"faint"_s));
}

QLabel *DefView::_bold(const QString &t)
{
    QLabel *h = label(t, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

void DefView::_num_rows(const QString &key, const std::vector<script::Node> &kids)
{
    const QStringList path = base + QStringList{key};
    auto *w = new QWidget();
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 6, 0, 8);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->setColumnStretch(0, 1);
    g->addWidget(_bold(u"What it does (the numbers it changes)"_s), 0, 0, 1, 3);
    int r = 1;
    std::vector<std::pair<QString, QString>> other;
    std::set<QString> have;
    for (const script::Node &c : kids) {
        if (c.is_block)
            continue;
        const QString k = kkey(c);
        if (!modifiers::num_ok(py::strip(c.value))) {
            other.emplace_back(k, c.value);
            continue;
        }
        have.insert(k);
        QLabel *lb = label(modpanel::attr_name(mod, k), u"sub"_s, false);
        lb->setToolTip(k);
        g->addWidget(lb, r, 0);
        auto *ed = new QLineEdit(py::strip(c.value));
        ed->setFixedWidth(100);
        ed->setAlignment(Qt::AlignRight);
        connect(ed, &QLineEdit::editingFinished, this, [this, path, k, ed] { _set_num(path, k, ed); });
        g->addWidget(ed, r, 1);
        _edits.insert(k, ed);
        auto *x = new QPushButton(u"✕"_s);
        x->setFixedWidth(34);
        x->setToolTip(u"Take “%1” off"_s.arg(k));
        connect(x, &QPushButton::clicked, this, [this, path, k, key] {
            _change(u"%1: %2 taken off"_s.arg(nm(key), k),
                    [path, k](const QString &t) { return blocks::del_value(t, path, k); });
        });
        g->addWidget(x, r, 2);
        r += 1;
    }
    if (r == 1) {
        g->addWidget(label(u"nothing yet"_s, u"faint"_s), r, 0);
        r += 1;
    }
    auto *box = new QComboBox();
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    std::set<QString> known;
    if (doc && doc->maps.contains(u"event_mods"_s))
        for (const QString &a : doc->map<modifiers::ModifierEdits>(u"event_mods"_s).db().known_attrs)
            known.insert(a);
    QStringList first;
    for (const QString &k : spec.common)
        if (!have.contains(k))
            first << k;
    QStringList all = first;
    for (const QString &k : known)                    // sorted(known - have - set(first))
        if (!have.contains(k) && !first.contains(k))
            all << k;
    for (const QString &k : all)
        box->addItem(u"%1  (%2)"_s.arg(modpanel::attr_name(mod, k), k), k);
    box->setCurrentIndex(-1);
    box->lineEdit()->setPlaceholderText(u"Add an effect — pick one or type its key"_s);
    g->addWidget(box, r, 0);
    auto *val = new QLineEdit();
    val->setPlaceholderText(u"value"_s);
    val->setFixedWidth(100);
    val->setAlignment(Qt::AlignRight);
    g->addWidget(val, r, 1);
    auto *add = new QPushButton(u"＋"_s);
    add->setFixedWidth(34);
    connect(add, &QPushButton::clicked, this, [this, path, box, val] { _add_num(path, box, val); });
    connect(val, &QLineEdit::returnPressed, this, [this, path, box, val] { _add_num(path, box, val); });
    g->addWidget(add, r, 2);
    rows->addWidget(w);
    if (!other.empty()) {
        QStringList kv;
        for (const auto &[k, v] : other)
            kv << u"%1 = %2"_s.arg(k, v);
        rows->addWidget(label(u"Also: "_s + kv.join(u" · "_s), u"faint"_s));
    }
}

void DefView::_gov_rows(const QString &key, const std::vector<script::Node> &kids)
{
    const QStringList path = base + QStringList{key};
    const OrderedMap<QString, QString> vals = vals_of(kids);
    auto *w = new QWidget();
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 6, 0, 8);
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(5);
    g->setColumnStretch(0, 1);
    g->setColumnStretch(1, 1);
    g->addWidget(_bold(u"Parties that may rule"_s), 0, 0, 1, 2);
    const QStringList ideos = ideologies(*store);
    for (int i = 0; i < ideos.size(); ++i) {
        const QString &ideo = ideos[i];
        auto *cb = new QCheckBox(nm(ideo));
        cb->setToolTip(ideo);
        cb->setChecked(vals.value(ideo) == u"yes");
        connect(cb, &QCheckBox::toggled, this, [this, path, k = ideo](bool on) { _flag(path, k, on, true); });
        g->addWidget(cb, 1 + i / 2, i % 2);
        _edits.insert(ideo, cb);
    }
    const int r = 2 + (int(ideos.size()) + 1) / 2;
    g->addWidget(_bold(u"Elections"_s), r, 0, 1, 2);
    auto *el = new QCheckBox(u"The nation holds elections"_s);
    el->setChecked(vals.value(u"election"_s) == u"yes");
    connect(el, &QCheckBox::toggled, this, [this, path](bool on) { _flag(path, u"election"_s, on); });
    g->addWidget(el, r + 1, 0, 1, 2);
    _edits.insert(u"election"_s, el);
    auto *row = new QHBoxLayout();
    dur_label = label(u"Months between elections"_s, u"sub"_s, false);
    row->addWidget(dur_label);
    auto *dur = dur_edit = new QLineEdit(vals.value(u"duration"_s, QString()));
    dur->setFixedWidth(100);
    dur->setAlignment(Qt::AlignRight);
    dur->setPlaceholderText(u"48"_s);
    connect(dur, &QLineEdit::editingFinished, this, [this, path, dur] { _duration(path, dur); });
    row->addWidget(dur);
    row->addStretch(1);
    g->addLayout(row, r + 2, 0, 1, 2);
    _edits.insert(u"duration"_s, dur);
    auto *ap = new QCheckBox(u"The ruler appoints the ruling party (the player may change it)"_s);
    ap->setChecked(vals.value(u"appoint_ruling_party"_s) == u"yes");
    connect(ap, &QCheckBox::toggled, this, [this, path](bool on) { _flag(path, u"appoint_ruling_party"_s, on); });
    g->addWidget(ap, r + 3, 0, 1, 2);
    _edits.insert(u"appoint_ruling_party"_s, ap);
    g->addWidget(_bold(u"Flag"_s), r + 4, 0, 1, 2);
    auto *fl = new QComboBox();
    fl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &[v, t] : GOV_FLAGS)
        fl->addItem(t, v);
    const QString curf = vals.value(u"flagType"_s, QString());
    std::set<QString> more;                           // the mod's own flag types (GFM: slot1 … slot4)
    static const QRegularExpression ft_re(uR"(\bflagType\s*=\s*"?([A-Za-z0-9_]+))"_s,
                                          QRegularExpression::UseUnicodePropertiesOption);
    for (auto it = ft_re.globalMatch(text()); it.hasNext();)
        more.insert(it.next().captured(1));
    QStringList extra(more.begin(), more.end());
    extra << curf;
    for (const QString &v : extra)
        if (fl->findData(v) < 0)
            fl->addItem(u"%1 flag"_s.arg(py::capitalize(under(v))), v);
    fl->setCurrentIndex(fl->findData(curf));
    fl->setEditable(true);
    fl->setInsertPolicy(QComboBox::NoInsert);
    fl->lineEdit()->setPlaceholderText(u"or type a flag type of your own (letters, digits, _)"_s);
    fl->setToolTip(u"The nation flies gfx/flags/TAG_<type>.tga under this government form. Type a new type (like "
                   u"slot1) to make one of your own — the flag maker on the Nation page then offers it."_s);
    const QString was = vals.value(u"flagType"_s, QString());
    connect(fl, &QComboBox::activated, this, [this, path, fl](int) { _flag_type(path, fl->currentData().toString()); });
    connect(fl->lineEdit(), &QLineEdit::editingFinished, this, [this, path, fl, was] {
        const QString t = py::strip(fl->currentText());
        const int i = fl->findText(t);
        const QString v = i >= 0 ? fl->itemData(i).toString() : t;
        if (v != was && (v.isEmpty() || modifiers::name_ok(v)))
            _flag_type(path, v);
    });
    g->addWidget(fl, r + 5, 0, 1, 2);
    _edits.insert(u"flagType"_s, fl);
    rows->addWidget(w);
    _sync_gov(vals);
    std::set<QString> known(ideos.begin(), ideos.end());
    known.insert({u"election"_s, u"duration"_s, u"appoint_ruling_party"_s, u"flagType"_s});
    QStringList kv;
    for (const auto &[k, v] : vals)
        if (!known.contains(k))
            kv << u"%1 = %2"_s.arg(k, v);
    if (!kv.isEmpty())
        rows->addWidget(label(u"Also: "_s + kv.join(u" · "_s), u"faint"_s));
}

void DefView::_sync_gov(const OrderedMap<QString, QString> &vals)
{
    if (spec.kind == u"government" && _edits.contains(u"duration"_s)) {
        const bool on = vals.value(u"election"_s) == u"yes";
        dur_label->setEnabled(on);
        dur_edit->setEnabled(on);
    }
}

// ------------------------------------------------------------------ changes
void DefView::_change(const QString &what, const std::function<QString(const QString &)> &fn)
{
    const QString old = text();
    const QString neu = fn(old);
    if (neu != old)
        Q_EMIT scriptEdit(what, rel, neu);
}

void DefView::_set_num(const QStringList &path, const QString &k, QLineEdit *ed)
{
    const QString v = py::strip(ed->text());
    if (!modifiers::num_ok(v)) {
        ed->setStyleSheet(RED_BORDER);
        warn->setText(u"“%1” needs a number (like 0.15 or -0.05)."_s.arg(k));
        return;
    }
    ed->setStyleSheet(QString());
    warn->setText(QString());
    _change(u"%1: %2 = %3"_s.arg(nm(path.back()), k, v),
            [path, k, v](const QString &t) { return blocks::set_value(t, path, k, v); });
}

void DefView::_add_num(const QStringList &path, QComboBox *box, QLineEdit *val)
{
    const int i = box->currentIndex();
    const QString k = i >= 0 && box->currentText() == box->itemText(i) ? box->itemData(i).toString()
                                                                       : py::strip(box->currentText());
    const QString v = py::strip(val->text());
    if (k.isEmpty() || !modifiers::name_ok(k)) {
        warn->setText(u"Pick an effect (or type its key: letters, digits, _)."_s);
        return;
    }
    if (!modifiers::num_ok(v)) {
        warn->setText(u"Give it a number (like 0.15 or -0.05)."_s);
        return;
    }
    warn->setText(QString());
    const QString *o = store->orig.get(rel);
    const OptStr like = o ? OptStr(*o) : std::nullopt;
    _change(u"%1: %2 = %3"_s.arg(nm(path.back()), k, v),
            [path, k, v, like](const QString &t) { return blocks::set_value(t, path, k, v, like); });
}

void DefView::_flag(const QStringList &path, const QString &k, bool on, bool remove)
{
    // yes / no; an ideology that may not rule is simply not listed (remove)
    const QString name = nm(path.back());
    if (on) {
        const QString *o = store->orig.get(rel);
        const OptStr like = o ? OptStr(*o) : std::nullopt;
        _change(u"%1: %2 yes"_s.arg(name, k),
                [path, k, like](const QString &t) { return blocks::set_value(t, path, k, u"yes"_s, like); });
    } else if (remove) {
        _change(u"%1: %2 no more"_s.arg(name, k), [path, k](const QString &t) { return blocks::del_value(t, path, k); });
    } else {
        _change(u"%1: %2 no"_s.arg(name, k),
                [path, k](const QString &t) { return blocks::set_value(t, path, k, u"no"_s); });
    }
}

void DefView::_duration(const QStringList &path, QLineEdit *ed)
{
    const QString v = py::strip(ed->text());
    if (v.isEmpty())
        return;
    bool digits = true, zero = true;                  // v.isdigit() and int(v) < 1
    for (const QChar c : v) {
        if (!c.isDigit())
            digits = false;
        else if (c.digitValue() != 0)
            zero = false;
    }
    if (!digits || zero) {
        ed->setStyleSheet(RED_BORDER);
        warn->setText(u"The months between elections need a whole number (like 48)."_s);
        return;
    }
    ed->setStyleSheet(QString());
    warn->setText(QString());
    _change(u"%1: elections every %2 months"_s.arg(nm(path.back()), v),
            [path, v](const QString &t) { return blocks::set_value(t, path, u"duration"_s, v); });
}

void DefView::_flag_type(const QStringList &path, const QString &v)
{
    const QString name = nm(path.back());
    if (!v.isEmpty())
        _change(u"%1: flag %2"_s.arg(name, v),
                [path, v](const QString &t) { return blocks::set_value(t, path, u"flagType"_s, v); });
    else
        _change(u"%1: the normal flag"_s.arg(name),
                [path](const QString &t) { return blocks::del_value(t, path, u"flagType"_s); });
}

void DefView::_loc_done(QLineEdit *ed, const QString &suffix)
{
    if (!cur || cur->isEmpty())
        return;
    const QString key = *cur;
    const QString t = py::split(ed->text()).join(u' ');
    if (!t.isEmpty() && t != loc(key + suffix).value_or(QString()))
        Q_EMIT nameEdit(key + suffix, t);
}

void DefView::_remove(const QString &key)
{
    const QStringList *h = holders().get(key);
    if (h && !h->isEmpty())
        return;
    const QString name = nm(key);
    cur.reset();
    store->drop_names.clear();                        // its names go with it
    for (const auto &[sfx, _t] : spec.names)
        store->drop_names << key + sfx;
    const QStringList b = base;
    _change(u"%1 “%2” removed"_s.arg(py::capitalize(spec.noun), name),
            [b, key](const QString &t) { return blocks::del_value(t, b, key); });
}

// ------------------------------------------------------------------ a new one
void DefView::_name_typed(const QString &t)
{
    if (!_key_typed) {
        static const QRegularExpression re(u"[^a-z0-9]+"_s);
        const QString k = py::strip(py::lower(t).replace(re, u"_"_s), u"_"_s);
        const QString pre = spec.prefix.value_or(QString());
        new_key->setText(!k.isEmpty() && !k.startsWith(pre) ? pre + k : k);
    }
}

void DefView::_make_new()
{
    const QString key = py::strip(new_key->text());
    const QString name = py::split(new_name->text()).join(u' ');
    const QString src = new_from->currentData().toString();
    if (name.isEmpty()) {
        new_warn->setText(u"Give it a name."_s);
        return;
    }
    if (!modifiers::name_ok(key)) {
        new_warn->setText(u"The key needs letters, digits or _ (no spaces), like %1example."_s.arg(
            spec.prefix.value_or(QString())));
        return;
    }
    if (keys().contains(key) || loc(key)) {
        new_warn->setText(u"“%1” is already used — pick another key."_s.arg(key));
        return;
    }
    new_warn->setText(QString());
    QStringList lines;
    if (!src.isEmpty())
        for (const script::Node &c : blocks::children(text(), base + QStringList{src}))
            if (!c.is_block)
                lines << u"%1 = %2"_s.arg(kkey(c), py::strip(c.value));
    cur = key;
    fresh = key;
    _key_typed = false;
    new_name->clear();
    new_key->clear();
    Q_EMIT nameEdit(key, name);
    const QStringList b = base;
    _change(u"New %1 “%2”"_s.arg(spec.noun, name),
            [b, key, lines](const QString &t) { return blocks::add_block(t, b, key, lines); });
}

}  // namespace defpanel
