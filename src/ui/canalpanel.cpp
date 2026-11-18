// canalpanel.py — line by line.
#include "ui/canalpanel.h"

#include "core/document.h"
#include "core/eventtools.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "ui/adjpanel.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;
namespace C = canals;

namespace canalpanel {

const Rgb WATER{80, 165, 255};
const Rgb BUILT{215, 215, 205};
const Rgb LINE{230, 185, 70};              // the canal row: the Adjacencies page's canal colour
const QString WARN = u"#e8a33d"_s;

static QString low(const QString &s) { return s.toLower(); }
static QString canal_rel(long long num) { return u"%1/canal_%2.txt"_s.arg(C::SPRITE_DIR).arg(num); }
static mapview::Shape line_shape(const std::vector<Pt> &pts, const Rgb &col)
{
    mapview::Shape s;
    s.kind = u"line"_s;
    for (const Pt &p : pts)
        s.pts.emplace_back(p.first, p.second);
    s.color = col;
    return s;
}

CanalPanel::CanalPanel(moddata::Mod &mod_, mapview::MapData &md_, adjacency::AdjGeometry &geo_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_), geo(geo_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    title = label(u"Canals"_s, u"title"_s);
    s->lay->addWidget(title);
    s->lay->addWidget(label(u"A canal is a canal row in map/adjacencies.csv (two sea tiles, through the land "
                            "province that holds it), a script that opens it (enable_canal = its number) and its "
                            "model on the map (a province flag with a file in map/province_flag_sprites). The map "
                            "shows each model itself, seen from above — its texture, one model unit to a map "
                            "pixel — exactly where the game puts it."_s,
                            u"faint"_s));
    lay->addWidget(s);

    s = new widgets::Section();
    auto *row = new QHBoxLayout();
    new_btn = new QPushButton(u"＋  New canal — click its province"_s);
    new_btn->setObjectName(u"primary"_s);
    new_btn->setCheckable(true);
    connect(new_btn, &QPushButton::toggled, this,
            [this](bool on) { _arm(on ? std::optional<QString>(u"new"_s) : std::nullopt); });
    row->addWidget(new_btn, 1);
    s->lay->addLayout(row);
    pick_note = label(QString(), u"warnAmber"_s);
    pick_note->hide();
    s->lay->addWidget(pick_note);
    tree = new QTreeWidget();
    tree->setColumnCount(4);
    tree->setHeaderLabels({u"No."_s, u"Canal"_s, u"Province"_s, u"Opens"_s});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    QHeaderView *h = tree->header();
    h->setStretchLastSection(false);
    const QHeaderView::ResizeMode modes[] = {QHeaderView::ResizeToContents, QHeaderView::Stretch, QHeaderView::Stretch,
                                             QHeaderView::ResizeToContents};
    for (int col = 0; col < 4; ++col)
        h->setSectionResizeMode(col, modes[col]);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setMinimumHeight(180);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] { _tree_picked(); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { _zoom(); });
    s->lay->addWidget(tree);
    count = label(QString(), u"faint"_s);
    s->lay->addWidget(count);
    lay->addWidget(s);

    // --- the selected canal
    s = new widgets::Section();
    ed_title = label(u"No canal selected"_s, u"title"_s);
    s->lay->addWidget(ed_title);
    ed_probs = label(QString(), u"warnAmber"_s);
    ed_probs->setTextFormat(Qt::RichText);
    s->lay->addWidget(ed_probs);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    ed_name = new QLineEdit();
    ed_name->setPlaceholderText(u"Name (the row's comment)"_s);
    connect(ed_name, &QLineEdit::editingFinished, this, [this] { _name_edited(); });
    ed_prov = new adjpanel::ProvField(mod);
    ed_a = new adjpanel::ProvField(mod);
    ed_b = new adjpanel::ProvField(mod);
    for (const auto &[f, key] : std::vector<std::pair<adjpanel::ProvField *, QString>>{
             {ed_prov, u"prov"_s}, {ed_a, u"a"_s}, {ed_b, u"b"_s}}) {
        connect(f, &adjpanel::ProvField::pickRequested, this, [this, k = key] { _arm(k); });
        connect(f->spin, &QSpinBox::editingFinished, this, [this] { _fields_edited(); });
    }
    const std::vector<std::pair<QString, QWidget *>> rows{
        {u"Name"_s, ed_name}, {u"Province"_s, ed_prov}, {u"Sea tile"_s, ed_a}, {u"Sea tile"_s, ed_b}};
    for (size_t r = 0; r < rows.size(); ++r) {
        grid->addWidget(label(rows[r].first, u"sub"_s, false), int(r), 0);
        grid->addWidget(rows[r].second, int(r), 1);
    }
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    prov_btn = new QPushButton(u"◎  Select province (then click on the map)"_s);
    connect(prov_btn, &QPushButton::clicked, this, [this] { _arm(u"prov"_s); });
    s->lay->addWidget(prov_btn);
    s->lay->addWidget(label(u"A canal lies in one province: no land link is needed. Its row joins the two sea "
                            "tiles on both sides — picked for you from the sea tiles the province touches."_s,
                            u"faint"_s));

    s->lay->addWidget(label(u"Model on the map"_s));
    auto *mrow = new QHBoxLayout();
    mrow->setSpacing(0);
    model_group = new QButtonGroup(this);
    model_group->setExclusive(true);
    model_row = mrow;
    s->lay->addLayout(mrow);
    model_note = label(QString(), u"faint"_s);
    s->lay->addWidget(model_note);
    // how the model sits on the map's pixels (the user, 2026-10-16: "it needs to be really exact" — a model whose
    // water runs over land looks like a canal from land to land)
    fit_lbl = label(QString(), u"sub"_s);
    fit_lbl->setTextFormat(Qt::RichText);
    s->lay->addWidget(fit_lbl);
    row = new QHBoxLayout();
    cmp_btn = new QPushButton(u"Compare the models — each at its best spot ▸"_s);
    cmp_btn->setToolTip(u"Tries every model around the canal's province, pixel by pixel, and shows each where it "
                        "fits best: its water on the sea tiles and in the canal's province, its locks in the "
                        "province, nothing on other land. Click one to take it."_s);
    connect(cmp_btn, &QPushButton::clicked, this, [this] { _compare(); });
    row->addWidget(cmp_btn, 1);
    s->lay->addLayout(row);
    cmp_host = new QWidget();
    cmp_row = new QHBoxLayout(cmp_host);
    cmp_row->setContentsMargins(0, 0, 0, 0);
    cmp_row->setSpacing(6);
    cmp_host->hide();
    s->lay->addWidget(cmp_host);
    away_btn = new QPushButton(u"Place the model elsewhere — click a spot on the map"_s);
    away_btn->setToolTip(u"The canal keeps working where it is; only its picture goes to the spot you click "
                         "(far away, out of sight: a canal without a visible model)"_s);
    connect(away_btn, &QPushButton::clicked, this, [this] { _arm(u"place"_s); });
    s->lay->addWidget(away_btn);

    auto *pad = new QGridLayout();
    pad->setHorizontalSpacing(4);
    pad->setVerticalSpacing(4);
    struct Key {
        QString key, txt;
        int r, c;
        QString tip;
    };
    for (const Key &k : std::vector<Key>{{u"up"_s, u"↑"_s, 0, 1, u"1 pixel north"_s},
                                         {u"left"_s, u"←"_s, 1, 0, u"1 pixel west"_s},
                                         {u"right"_s, u"→"_s, 1, 2, u"1 pixel east"_s},
                                         {u"down"_s, u"↓"_s, 2, 1, u"1 pixel south"_s}}) {
        auto *b = new QPushButton(k.txt);
        b->setFixedSize(40, 34);
        b->setStyleSheet(u"QPushButton { padding: 0px; font-size: 17px; font-weight: 700; }"_s);
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(350);
        b->setAutoRepeatInterval(110);
        b->setToolTip(k.tip + u" (Shift: 0.1 pixel)"_s);
        connect(b, &QPushButton::clicked, this, [this, key = k.key] { _nudge(key); });
        pad->addWidget(b, k.r, k.c);
        nudge[k.key] = b;
    }
    auto *prow = new QHBoxLayout();
    prow->addLayout(pad);
    auto *xy = new QGridLayout();
    xy->setHorizontalSpacing(6);
    ed_x = new QLineEdit();
    ed_y = new QLineEdit();
    int i = 0;
    for (const auto &[t, e] : std::vector<std::pair<QString, QLineEdit *>>{{u"x"_s, ed_x}, {u"y"_s, ed_y}}) {
        e->setFixedWidth(70);
        e->setAlignment(Qt::AlignCenter);
        connect(e, &QLineEdit::editingFinished, this, [this] { _xy_typed(); });
        xy->addWidget(label(t, u"sub"_s, false), i, 0);
        xy->addWidget(e, i, 1);
        ++i;
    }
    prow->addSpacing(12);
    prow->addLayout(xy);
    prow->addStretch(1);
    s->lay->addLayout(prow);
    s->lay->addWidget(label(u"Move it pixel by pixel with the arrows (Shift: 0.1), type x / y, or drag the model on "
                            "the map. x / y are map pixels (x east, y north) from where the game puts a model at "
                            "0 / 0: the middle of the box around the province — measured in the game."_s,
                            u"faint"_s));

    s->lay->addWidget(label(u"How it opens"_s));
    ed_open = new QComboBox();
    for (const QString &k : C::OPEN_KINDS)
        ed_open->addItem(*C::OPEN_NAMES.get(k), k);
    ed_open->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(ed_open, &QComboBox::activated, this, [this](int) { _open_changed(); });
    s->lay->addWidget(ed_open);
    open_note = label(QString(), u"faint"_s);
    open_note->setTextFormat(Qt::RichText);
    s->lay->addWidget(open_note);
    row = new QHBoxLayout();
    zoom_btn = new QPushButton(u"Show on the map"_s);
    connect(zoom_btn, &QPushButton::clicked, this, [this] { _zoom(); });
    del_btn = new QPushButton(u"Delete canal"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _delete(); });
    row->addWidget(zoom_btn, 1);
    row->addWidget(del_btn, 1);
    s->lay->addLayout(row);
    ed_section = s;
    lay->addWidget(s);
    lay->addStretch(1);
}

// ------------------------------------------------------------------ data
adjacency::AdjacencyFile &CanalPanel::adj() const
{
    return doc->map<adjacency::AdjacencyFile>(u"adjacency"_s);
}

scriptstore::ScriptStore &CanalPanel::store() const
{
    return doc->map<scriptstore::ScriptStore>(u"scripts"_s);
}

C::Models &CanalPanel::_models()
{
    if (!models) {
        models.emplace(mod);
        for (const QString &n : models->names()) {
            auto *b = new QPushButton(QString(n).replace(u"_Canal"_s, QString()).replace(u'_', u' '));
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            b->setToolTip(u"Model %1 (%2)"_s.arg(n, *models->files.get(n)));
            connect(b, &QPushButton::clicked, this, [this, name = n] { _model_picked(name); });
            model_group->addButton(b);
            model_btns[n] = b;
            model_order << n;
            model_row->addWidget(b, 1);
        }
    }
    return *models;
}

const C::Canal *CanalPanel::canal(std::optional<long long> num) const
{
    if (!num)
        return nullptr;
    for (const C::Canal &c : _list)
        if (c.num == *num)
            return &c;
    return nullptr;
}

bool CanalPanel::own(const C::Canal &c) const
{
    // The canal is opened by the editor's own file (or by nothing): the page may write its opening.
    return std::all_of(c.opens.begin(), c.opens.end(),
                       [](const C::Opening &o) { return o.rel == C::OWN_DECISIONS || o.rel == C::OWN_EVENTS; });
}

std::optional<QString> CanalPanel::mode_of(const C::Canal &c) const
{
    QSet<QString> rels;
    for (const C::Opening &o : c.opens)
        rels.insert(o.rel);
    if (rels.contains(C::OWN_EVENTS))
        return u"start"_s;
    if (rels.contains(C::OWN_DECISIONS))
        return u"decision"_s;
    if (c.opens.empty())
        return u"none"_s;
    return std::nullopt;
}

std::pair<std::optional<QString>, std::optional<C::Sprite>> CanalPanel::own_sprite(long long num)
{
    // For a canal nothing opens yet: its sprite file by the editor's name (canal_<n>).
    const auto sp = C::sprites(mod, store());
    const QString key = u"canal_%1"_s.arg(num);
    if (const C::Sprite *d = sp.get(key))
        return {key, *d};
    return {std::nullopt, std::nullopt};
}

std::optional<Pt> CanalPanel::anchor(long long pid) const
{
    // Where the game puts a province's sprite before its x / y (display coordinates): the middle of the box around
    // the province's pixels — measured (canals.py), not its pixel centre and no positions.txt point.
    if (!(0 < pid && pid < md.n) || md.area[size_t(pid)] == 0)
        return std::nullopt;
    const size_t p = size_t(pid);
    return Pt{(double(md.x0[p]) + double(md.x1[p]) + 1) / 2.0, (double(md.y0[p]) + double(md.y1[p]) + 1) / 2.0};
}

std::optional<CanalPanel::Place> CanalPanel::place(const C::Canal &c, double dx, double dy)
{
    // (anchor, model, x, y) of a canal's model on the map, or None.
    std::optional<C::Sprite> sp = c.sprite;
    if (!sp)
        sp = own_sprite(c.num).second;
    if (!sp || !sp->sprite || sp->sprite->isEmpty())
        return std::nullopt;
    const long long pid = c.flag_prov && *c.flag_prov ? *c.flag_prov : c.prov;
    const auto a = anchor(pid);
    if (!a)
        return std::nullopt;
    return Place{*a, *sp->sprite, sp->x + dx, sp->y + dy};
}

std::optional<CanalPanel::Polys> CanalPanel::model_polys(const QString &model, const Pt &a, double x, double y)
{
    // {"water": [...], "built": [...]} in display coordinates.
    const auto &o = _models().outline(model);
    if (!o || o->isEmpty())
        return std::nullopt;
    const double k = C::MODEL_UNIT;
    const double ox = a.first + x * C::OFFSET_UNIT;
    const double oy = a.second - (C::OFFSET_Y_NORTH ? y : -y) * C::OFFSET_UNIT;
    Polys out;
    for (const auto &[part, polys] : *o) {
        std::vector<std::vector<Pt>> ps;
        for (const C::Ring &poly : polys) {
            std::vector<Pt> q;
            for (const auto &[px, pz] : poly)
                q.emplace_back(ox + px * k, oy - pz * k);
            ps.push_back(q);
        }
        out.insert(part, ps);
    }
    return out;
}

// ------------------------------------------------------------------ refresh
void CanalPanel::refresh(document::Document &d, const std::vector<long long> &,
                         const std::optional<std::pair<QString, QString>> &)
{
    doc = &d;
    _models();
    _list = C::canals(adj(), mod, store());
    _fill_tree();
    _fill_editor();
    Q_EMIT overlayChanged();
}

void CanalPanel::_fill_tree()
{
    _filling = true;
    tree->clear();
    QTreeWidgetItem *cur = nullptr;
    for (const C::Canal &c : _list) {
        const moddata::Province *pr = mod.provinces.get(c.prov);
        std::set<QString> kinds;
        for (const C::Opening &o : c.opens)
            kinds.insert(o.kind);
        QString opens = QStringList(kinds.begin(), kinds.end()).join(u", "_s);
        if (opens.isEmpty())
            opens = u"—"_s;
        auto *it = new QTreeWidgetItem(QStringList{
            QString::number(c.num), c.name.isEmpty() ? u"(no name)"_s : c.name,
            c.prov ? u"#%1 %2"_s.arg(c.prov).arg(pr ? pr->name : u"?"_s) : u"—"_s, opens});
        it->setData(0, Qt::UserRole, c.num);
        const QStringList probs = C::problems(c, mod);
        if (!probs.isEmpty()) {
            it->setToolTip(1, probs.join(u"\n"_s));
            for (int col = 0; col < 4; ++col)
                it->setForeground(col, QBrush(QColor(WARN)));
        }
        tree->addTopLevelItem(it);
        if (sel_num && c.num == *sel_num)
            cur = it;
    }
    if (cur)
        tree->setCurrentItem(cur);
    else if (sel_num && !canal(sel_num))
        sel_num.reset();
    _filling = false;
    long long n_pic = 0;
    for (const C::Canal &c : _list)
        n_pic += place(c) ? 1 : 0;
    const QString names = _models().names().join(u", "_s);
    count->setText(u"%1 canal%2 · %3 with a model on the map · models found: %4"_s.arg(_list.size())
                       .arg(_list.size() == 1 ? QString() : u"s"_s)
                       .arg(n_pic)
                       .arg(names.isEmpty() ? u"none"_s : names));
}

void CanalPanel::_tree_picked()
{
    if (_filling)
        return;
    QTreeWidgetItem *it = tree->currentItem();
    select(it ? std::optional<long long>(it->data(0, Qt::UserRole).toLongLong()) : std::nullopt);
}

void CanalPanel::select(std::optional<long long> num, bool zoom)
{
    sel_num = num;
    _fill_editor();
    if (zoom)
        _zoom();
    Q_EMIT overlayChanged();
}

void CanalPanel::_fill_editor()
{
    const C::Canal *cp = sel_num ? canal(sel_num) : nullptr;
    ed_section->setEnabled(cp != nullptr);
    const std::vector<QObject *> quiet{ed_name, ed_prov->spin, ed_a->spin, ed_b->spin, ed_x, ed_y};
    for (QObject *w : quiet)
        w->blockSignals(true);
    struct Unblock {                                 // (Python's finally)
        const std::vector<QObject *> &ws;
        ~Unblock()
        {
            for (QObject *w : ws)
                w->blockSignals(false);
        }
    } unblock{quiet};
    if (!cp) {
        ed_title->setText(u"No canal selected"_s);
        ed_probs->setText(QString());
        ed_name->setText(QString());
        for (adjpanel::ProvField *f : {ed_prov, ed_a, ed_b})
            f->set(0);
        ed_x->setText(QString());
        ed_y->setText(QString());
        model_note->setText(u"Select a canal in the list or on the map, or make a new one."_s);
        open_note->setText(QString());
        fit_lbl->setText(QString());
        cmp_host->hide();
        return;
    }
    const C::Canal c = *cp;
    ed_title->setText(u"Canal %1%2"_s.arg(c.num).arg(!c.name.isEmpty() ? u" — "_s + c.name : QString()));
    QStringList probs;
    for (const QString &p : C::problems(c, mod))
        probs << u"⚠ "_s + p;
    ed_probs->setText(probs.join(u"<br>"_s));
    ed_name->setText(c.name);
    ed_prov->set(c.prov);
    ed_a->set(c.seas.first);
    ed_b->set(c.seas.second);
    ed_prov->name->setText(c.prov ? adjpanel::prov_name(mod, c.prov) : QString());
    ed_a->name->setText(c.seas.first ? adjpanel::prov_name(mod, c.seas.first) : QString());
    ed_b->name->setText(c.seas.second ? adjpanel::prov_name(mod, c.seas.second) : QString());
    const auto pl = place(c);
    for (const QString &n : model_order)
        model_btns[n]->setChecked(pl && low(pl->model) == low(n));
    const bool editable_model = _sprite_rel(c).has_value();
    for (const QString &n : model_order)
        model_btns[n]->setEnabled(editable_model);
    for (const auto &[k, b] : nudge)
        b->setEnabled(pl && editable_model);
    ed_x->setEnabled(pl && editable_model);
    ed_y->setEnabled(pl && editable_model);
    ed_x->setText(pl ? C::fmt_num(pl->x) : QString());
    ed_y->setText(pl ? C::fmt_num(pl->y) : QString());
    if (!editable_model) {
        model_note->setText(u"No model: the script that opens it sets no province flag with a sprite file. Add one "
                            "there (Scripts), or let the editor open it below."_s);
    } else if (pl && !(_models().outline(pl->model) && !_models().outline(pl->model)->isEmpty())) {
        model_note->setText(u"Model %1: its .xac file was not found or could not be read — no outline to show."_s.arg(
            pl->model));
    } else {
        const QString f = c.flag && !c.flag->isEmpty() ? *c.flag : own_sprite(c.num).first.value_or(u"None"_s);
        model_note->setText(u"Province flag %1 · file %2/%3.txt"_s.arg(f, C::SPRITE_DIR, f));
    }
    fit_lbl->setText(pl ? fit_text(fit(c, pl->model, pl->x, pl->y), c) : QString());
    cmp_btn->setEnabled(editable_model);
    away_btn->setEnabled(pl && editable_model);
    if (_cmp_for != c.num)                           // (another canal: its own comparison)
        cmp_host->hide();
    _cmp_for = c.num;
    const auto mode = mode_of(c);
    ed_open->setEnabled(mode.has_value());
    if (mode)
        ed_open->setCurrentIndex(int(C::OPEN_KINDS.indexOf(*mode)));
    open_note->setText(_open_text(c, mode));
}

QString CanalPanel::_open_text(const C::Canal &c, const std::optional<QString> &mode) const
{
    if (!mode) {
        QStringList where;
        for (const C::Opening &o : c.opens)
            where << u"%1 <b>%2</b> in %3"_s.arg(o.kind, o.name.value_or(u"None"_s), o.rel);
        return u"Opened by the mod's own script — change it in Scripts:<br>%1"_s.arg(where.join(u"<br>"_s));
    }
    if (*mode == u"start")
        return u"An event of the province's owner opens it in the first days of the game (fires once; %1). A "
               "province without an owner never opens it."_s.arg(C::OWN_EVENTS);
    if (*mode == u"decision")
        return u"A decision of the province's owner: <i>build_canal_%1</i> in %2 — no requirements; add them in "
               "Scripts if you want some."_s.arg(c.num)
            .arg(C::OWN_DECISIONS);
    return u"Closed: the game opens a canal only by enable_canal = %1 — and shows its model only when a script sets "
           "its province flag."_s.arg(c.num);
}

std::optional<QString> CanalPanel::_sprite_rel(const C::Canal &c)
{
    // The sprite file this page may change for the canal (None: there is no picture to change).
    if (c.sprite)
        return c.sprite->rel;
    if (own(c)) {
        const auto [f, d] = own_sprite(c.num);
        return d ? d->rel : canal_rel(c.num);
    }
    return std::nullopt;
}

// ------------------------------------------------------------------ picking on the map
void CanalPanel::_arm(const std::optional<QString> &what)
{
    pick = what;
    if (what != u"new"_s && new_btn->isChecked()) {
        new_btn->blockSignals(true);
        new_btn->setChecked(false);
        new_btn->blockSignals(false);
    }
    static const std::map<QString, QString> texts = {
        {u"new"_s, u"Click the land province the canal goes through."_s},
        {u"place"_s, u"Click where the model's middle should go — anywhere on the map."_s},
        {u"prov"_s, u"Click the canal's land province on the map."_s},
        {u"a"_s, u"Click the first sea tile on the map."_s},
        {u"b"_s, u"Click the second sea tile on the map."_s}};
    const auto t = what ? texts.find(*what) : texts.end();
    pick_note->setText(what && !what->isEmpty() ? (t != texts.end() ? t->second : QString()) + u"  (Esc cancels)"_s
                                                : QString());
    pick_note->setVisible(what && !what->isEmpty());
    if (what && !what->isEmpty() && t != texts.end())
        Q_EMIT message(t->second);
}

void CanalPanel::picked(long long pid)
{
    const std::optional<QString> what = pick;
    _arm(std::nullopt);
    if (new_btn->isChecked())
        new_btn->setChecked(false);
    const moddata::Province *pr = mod.provinces.get(pid);
    if (!pr)
        return;
    if (what == u"new"_s) {
        _new_canal(pid);
        return;
    }
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    long long prov = c.prov, a = c.seas.first, b = c.seas.second;
    if (what == u"prov"_s) {
        if (pr->sea) {
            _say(u"#%1 is a sea tile — a canal goes through a land province."_s.arg(pid));
            return;
        }
        prov = pid;
        if (const auto got = sea_pair(pid)) {
            a = got->first;
            b = got->second;
        }
    } else if (what == u"a"_s || what == u"b"_s) {
        if (!pr->sea) {
            _say(u"#%1 is land — pick a sea tile."_s.arg(pid));
            return;
        }
        if (what == u"a"_s)
            a = pid;
        else
            b = pid;
    } else {
        return;                                      // (Python: a KeyError — no pick armed)
    }
    _set_rows(c, prov, a, b, c.name, u"Canal %1: %2"_s.arg(c.num).arg(what == u"prov"_s ? u"province"_s : u"sea tile"_s));
}

void CanalPanel::_say(const QString &text)
{
    pick_note->setText(text);
    pick_note->setVisible(true);
    Q_EMIT message(text);
}

std::optional<std::pair<long long, long long>> CanalPanel::sea_pair(long long pid)
{
    // The two sea tiles a canal through pid joins: of the sea tiles it touches, the two farthest apart.
    std::vector<long long> seas;
    if (const auto *nb = md.adjacent.get(pid))
        for (long long q : nb->items())
            if (adj().sea.contains(q))
                seas.push_back(q);
    std::sort(seas.begin(), seas.end());
    if (seas.size() < 2)
        return std::nullopt;
    std::map<long long, std::optional<Pt>> pts;
    for (long long q : seas)
        pts[q] = geo.border_point(pid, q);
    std::pair<long long, long long> best{seas[0], seas[1]};
    double far_ = -1.0;
    for (size_t i = 0; i < seas.size(); ++i) {
        const long long p = seas[i];
        for (size_t j = i + 1; j < seas.size(); ++j) {
            const long long q = seas[j];
            if (!pts[p] || !pts[q])
                continue;
            const double d = py::hypot(pts[p]->first - pts[q]->first, pts[p]->second - pts[q]->second);
            if (d > far_) {
                best = {p, q};
                far_ = d;
            }
        }
    }
    return best;
}

// ------------------------------------------------------------------ edits
void CanalPanel::_edit(const QString &label_text, const std::function<void()> &fn, const QStringList &kinds)
{
    doc->edit(label_text, {}, fn, {}, {}, kinds);
}

void CanalPanel::_new_canal(long long pid)
{
    const moddata::Province &pr = *mod.provinces.get(pid);
    if (pr.sea) {
        _say(u"#%1 is a sea tile — click the land province the canal goes through."_s.arg(pid));
        return;
    }
    const auto pair = sea_pair(pid);
    if (!pair) {
        bool any = false;
        if (const auto *nb = md.adjacent.get(pid))
            for (long long q : nb->items())
                any = any || adj().sea.contains(q);
        _say(u"#%1 %2 touches %3 sea tile — a canal joins two."_s.arg(pid).arg(pr.name, !any ? u"no"_s : u"only one"_s));
        return;
    }
    std::set<long long> used;
    for (long long n : adj().canal_numbers())
        used.insert(n);
    for (const C::Canal &c : _list)
        used.insert(c.num);
    const long long num = used.empty() ? 1 : *used.rbegin() + 1;
    const QString pname = pr.name.trimmed();
    const QString name = u"%1 Canal"_s.arg(pname.isEmpty() ? u"#%1"_s.arg(pid) : pname);
    const QStringList models_ = _models().names();
    QString model = models_.isEmpty() ? u"Kiel_Canal"_s : models_[0];
    for (const QString &m : models_)
        if (low(m).startsWith(u"kiel"_s)) {
            model = m;
            break;
        }
    const auto [a, b] = *pair;
    sel_num = num;
    doc->edit(u"New canal %1 through #%2"_s.arg(num).arg(pid), {}, [&] {
        adj().add({{a, b, u"canal"_s, pid, num, name}});
        _write_sprite(num, nullptr, model, 0.0, 0.0);
        _write_opening(num, pid, u"decision"_s, name);
    }, {}, {}, {u"adjacency"_s, u"scripts"_s, u"loc"_s});
    Q_EMIT message(u"New canal %1 through #%2 %3, between sea tiles #%4 and #%5 — written on Apply; Ctrl+Z undoes "
                   "it."_s.arg(num)
                       .arg(pid)
                       .arg(pr.name)
                       .arg(a)
                       .arg(b));
}

void CanalPanel::_set_rows(const C::Canal &c, long long prov, long long a, long long b, const QString &name,
                           const QString &label_text)
{
    if (c.rows.empty())
        return;
    const long long uid = c.rows[0].uid;
    doc->edit(label_text, {}, [&] {
        adj().update(uid, [&](adjacency::Row &r) {
            r.a = a;
            r.b = b;
            r.through = prov;
            r.comment = name;
        });
        if (own(c) && !c.opens.empty())              // the editor's own opening follows (province, name)
            _write_opening(c.num, prov, mode_of(c), name);
    }, {}, {}, {u"adjacency"_s, u"scripts"_s, u"loc"_s});
}

void CanalPanel::_fields_edited()
{
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    const long long prov = ed_prov->value(), a = ed_a->value(), b = ed_b->value();
    if (std::tuple{prov, a, b} != std::tuple{c.prov, c.seas.first, c.seas.second})
        _set_rows(c, prov, a, b, c.name, u"Canal %1: provinces"_s.arg(c.num));
}

void CanalPanel::_name_edited()
{
    const C::Canal *cp = canal(sel_num);
    const QString name = ed_name->text().simplified();
    if (!cp || name == cp->name || name.isEmpty())
        return;
    const C::Canal c = *cp;
    _set_rows(c, c.prov, c.seas.first, c.seas.second, name, u"Canal %1: name"_s.arg(c.num));
}

void CanalPanel::_write_sprite(long long num, const C::Canal *c, const QString &model, double x, double y)
{
    const std::optional<QString> rel = c ? _sprite_rel(*c) : std::optional<QString>(canal_rel(num));
    if (!rel)
        return;
    scriptstore::ScriptStore &st = store();
    st.set_text(*rel, C::sprite_text(st.text(*rel), model, x, y));
}

void CanalPanel::_write_opening(long long num, long long prov, const std::optional<QString> &mode, const QString &name)
{
    // The editor's own opening of canal num: whatever it had goes, the new one is written (mode).
    scriptstore::ScriptStore &st = store();
    const QString flag = u"canal_%1"_s.arg(num);
    for (const auto &[rel, kind] : std::vector<std::pair<QString, QString>>{{C::OWN_DECISIONS, u"decision"_s},
                                                                            {C::OWN_EVENTS, u"event"_s}}) {
        QString text = st.text(rel);
        const auto ops = C::openings(mod, st);
        if (const auto *lst = ops.get(num))
            for (const C::Opening &o : *lst)
                if (o.rel == rel)
                    text = C::remove_item(text, kind, o.name);
        if (text != st.text(rel))
            st.set_text(rel, text);
    }
    localisation::LocEdits *loc = doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    if (mode == u"decision"_s) {
        const QString key = u"build_canal_%1"_s.arg(num);
        st.set_text(C::OWN_DECISIONS, C::add_decision(st.text(C::OWN_DECISIONS), C::decision_block(key, num, prov, flag)));
        if (loc) {
            loc->set(key + u"_title"_s, u"Build the %1"_s.arg(name));
            loc->set(key + u"_desc"_s, u"Open the %1 for ships."_s.arg(name));
        }
    } else if (mode == u"start"_s) {
        const auto used = eventtools::event_ids(mod, st);
        QStringList used_keys, near_;
        for (const auto &[i, w] : used) {
            used_keys << i;
            if (std::any_of(w.begin(), w.end(), [](const eventtools::Place &p) { return std::get<0>(p) == C::OWN_EVENTS; }))
                near_ << i;
        }
        const QString eid = eventtools::free_id(used_keys, near_);
        st.set_text(C::OWN_EVENTS, C::add_event(st.text(C::OWN_EVENTS), C::event_block(eid, num, prov, flag)));
        if (loc) {
            loc->set(u"EVTNAME"_s + eid, u"The %1"_s.arg(name));
            loc->set(u"EVTDESC"_s + eid, u"The %1 is open for ships."_s.arg(name));
            loc->set(u"EVTOPTA"_s + eid, u"Good"_s);
        }
    }
}

void CanalPanel::_model_picked(const QString &model)
{
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    const auto pl = place(c);
    const double x = pl ? pl->x : 0.0, y = pl ? pl->y : 0.0;
    _edit(u"Canal %1: model %2"_s.arg(c.num).arg(model), [&] { _write_sprite(c.num, &c, model, x, y); });
}

void CanalPanel::move_to(double x, double y, const std::optional<QString> &label_text)
{
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    const auto pl = place(c);
    if (!pl)
        return;
    x = py::round_to(x, 2);
    y = py::round_to(y, 2);
    if (x == py::round_to(pl->x, 2) && y == py::round_to(pl->y, 2))
        return;
    _edit(label_text.value_or(u"Canal %1: move"_s.arg(c.num)), [&] { _write_sprite(c.num, &c, pl->model, x, y); });
}

void CanalPanel::_nudge(const QString &key)
{
    const C::Canal *cp = canal(sel_num);
    const auto pl = cp ? place(*cp) : std::nullopt;
    if (!pl)
        return;
    const long long num = cp->num;
    const double step = QApplication::keyboardModifiers() & Qt::ShiftModifier ? 0.1 : 1.0;
    double dx = 0, dy = 0;
    if (key == u"left")
        dx = -step;
    else if (key == u"right")
        dx = step;
    else if (key == u"up")
        dy = step;
    else
        dy = -step;
    move_to(pl->x + dx, pl->y + dy, u"Canal %1: move %2"_s.arg(num).arg(key));
}

void CanalPanel::_xy_typed()
{
    const auto x = py::to_float(QString(ed_x->text()).replace(u","_s, u"."_s));
    const auto y = x ? py::to_float(QString(ed_y->text()).replace(u","_s, u"."_s)) : std::nullopt;
    if (!x || !y) {
        _fill_editor();
        return;
    }
    move_to(*x, *y);
}

void CanalPanel::_open_changed()
{
    const C::Canal *cp = canal(sel_num);
    if (!cp || !mode_of(*cp))
        return;
    const C::Canal c = *cp;
    const QString mode = ed_open->currentData().toString();
    if (mode == mode_of(c))
        return;
    doc->edit(u"Canal %1: %2"_s.arg(c.num).arg(C::OPEN_NAMES.get(mode)->toLower()), {}, [&] {
        _write_opening(c.num, c.prov, mode, !c.name.isEmpty() ? c.name : u"canal %1"_s.arg(c.num));
    }, {}, {}, {u"scripts"_s, u"loc"_s});
}

void CanalPanel::_delete()
{
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    QList<long long> uids;
    for (const adjacency::Row &r : c.rows)
        uids << r.uid;
    const bool own_ = own(c);
    const std::optional<QString> rel = own_ ? _sprite_rel(c) : std::nullopt;
    doc->edit(u"Delete canal %1"_s.arg(c.num), {}, [&] {
        adj().del(uids);
        if (own_) {
            _write_opening(c.num, c.prov, u"none"_s, c.name);
            if (rel && !rel->isEmpty() && low(*rel) == low(canal_rel(c.num)))
                store().remove(*rel);
        }
    }, {}, {}, {u"adjacency"_s, u"scripts"_s, u"loc"_s});
    QString left;
    if (!own_) {
        std::set<QString> rels;
        for (const C::Opening &o : c.opens)
            rels.insert(o.rel);
        left = u"  The mod's own script (%1) still runs enable_canal = %2 — change it in Scripts."_s.arg(
                   QStringList(rels.begin(), rels.end()).join(u", "_s))
                   .arg(c.num);
    }
    Q_EMIT message(u"Deleted canal %1.%2 Ctrl+Z undoes it."_s.arg(c.num).arg(left));
    sel_num.reset();
}

void CanalPanel::_zoom()
{
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    auto a = anchor(cp->prov);
    if (!a)
        a = anchor(cp->flag_prov.value_or(0));
    if (a)
        Q_EMIT zoomRequested(a->first, a->second);
}

// ------------------------------------------------------------------ how a model fits the map's pixels
const std::vector<std::uint8_t> &CanalPanel::_sea()
{
    if (_sea_arr.empty() || (long long)_sea_arr.size() != md.n) {
        std::vector<std::uint8_t> a(size_t(md.n), 0);
        for (const auto &[pid, pr] : mod.provinces)
            if (0 < pid && pid < md.n && pr.sea)
                a[size_t(pid)] = 1;
        _sea_arr = a;
    }
    return _sea_arr;
}

const std::optional<C::Samples> &CanalPanel::_samples(const QString &model, long long stride)
{
    const std::pair<QString, long long> key{model, stride};
    auto it = _samp.find(key);
    if (it == _samp.end()) {
        const auto &r = _models().render(model);
        std::optional<C::Samples> got;
        if (r) {
            const C::Samples s = C::fit_samples(*r);
            C::Samples t;
            for (size_t i = 0; i < s.xs.size(); i += size_t(stride)) {   // (xs[::stride] …)
                t.xs.push_back(s.xs[i]);
                t.zs.push_back(s.zs[i]);
                t.wet.push_back(s.wet[i]);
            }
            got = t;
        }
        it = _samp.emplace(key, got).first;
    }
    return it->second;
}

std::optional<C::Score> CanalPanel::fit(const C::Canal &c, const QString &model, double x, double y, long long stride)
{
    // fit_score of a model at offset x / y for the canal (None: no model picture).
    const auto a = anchor(c.flag_prov && *c.flag_prov ? *c.flag_prov : c.prov);
    const auto &smp = _samples(model, stride);
    if (!a || !smp)
        return std::nullopt;
    return C::fit_score(md, _sea(), c.prov, {c.seas.first, c.seas.second}, *smp, a->first + x, a->second - y);
}

std::optional<std::tuple<C::Score, double, double>> CanalPanel::best_fit(const C::Canal &c, const QString &model,
                                                                         const std::optional<Pt> &around, long long reach)
{
    // (score dict, x, y) of the model's best spot near the canal's province: a whole-pixel search, then half pixels
    // around the best one.
    std::vector<Pt> starts{{0.0, 0.0}};
    if (around)
        starts.push_back(*around);
    const auto af = anchor(c.flag_prov && *c.flag_prov ? *c.flag_prov : c.prov), ap = anchor(c.prov);
    if (af && ap && *af != *ap)                      // (the picture set on another province: around the canal's own)
        starts.emplace_back(ap->first - af->first, af->second - ap->second);
    std::optional<std::tuple<double, double, double>> best;
    for (const auto &[sx, sy] : starts)
        for (long long dx = -reach; dx <= reach; ++dx)
            for (long long dy = -reach; dy <= reach; ++dy) {
                const auto f = fit(c, model, sx + double(dx), sy + double(dy), 6);
                if (!f)
                    return std::nullopt;
                if (!best || f->score > std::get<0>(*best))
                    best = {f->score, sx + double(dx), sy + double(dy)};
            }
    const auto [_s, bx, by] = *best;
    std::optional<std::tuple<C::Score, double, double>> out;
    for (double dx : {-1.0, -0.5, 0.0, 0.5, 1.0})
        for (double dy : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
            const auto f = fit(c, model, bx + dx, by + dy);
            if (!out || f->score > std::get<0>(*out).score)
                out = {*f, bx + dx, by + dy};
        }
    return out;
}

QString CanalPanel::fit_text(const std::optional<C::Score> &f, const C::Canal &) const
{
    // A fit in words, coloured: green when it sits well, amber when some of it is on other land.
    if (!f)
        return QString();
    const double bad = f->other;
    if (!f->near_)
        return u"<span style='color:#9aa4b2'>The model stands away from its canal (none of it on the canal's "
               "province or its sea tiles) — fine if that is meant: the canal works, its picture is elsewhere."
               "</span>"_s;
    const QString col = bad < 0.03 && f->reaches.size() == 2 ? u"#58c26a"_s : WARN;
    const QString reaches = f->reaches.size() == 2 ? u"both sea tiles"_s
                            : !f->reaches.empty()  ? u"one sea tile (#%1)"_s.arg(f->reaches[0])
                                                   : u"no sea tile"_s;
    return u"<span style='color:%1'><b>Fit:</b> water %2 % on sea, %3 % in the province, <b>%4 % on other land</b> · "
           "locks %5 % in the province · reaches %6</span>"_s.arg(col)
        .arg(py::round_int(100 * f->water_sea))
        .arg(py::round_int(100 * f->water_prov))
        .arg(py::round_int(100 * f->other))
        .arg(py::round_int(100 * f->built_prov))
        .arg(reaches);
}

void CanalPanel::_compare()
{
    // Every model at its best spot: a picture of each over the map around the canal, its fit; a click takes it
    // (model and spot) — one undo step.
    const C::Canal *cp = canal(sel_num);
    if (!cp)
        return;
    const C::Canal c = *cp;
    while (cmp_row->count()) {
        QLayoutItem *it = cmp_row->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    const auto pl = place(c);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    struct Restore {
        ~Restore() { QApplication::restoreOverrideCursor(); }
    } restore;
    static const QRegularExpression tags(u"<[^>]+>"_s);
    for (const QString &name : _models().names()) {
        const auto got = best_fit(c, name, pl && low(pl->model) == low(name) ? std::optional<Pt>(Pt{pl->x, pl->y})
                                                                                 : std::nullopt);
        auto *b = new QToolButton();                 // a card: the picture on top, the words under it
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setStyleSheet(u"QToolButton { background: #1b1f26; border: 1px solid #3a404a; border-radius: 6px; "
                         "padding: 6px; color: #e8e2d4; } QToolButton:hover { border: 2px solid #d4a94a; }"_s);
        if (!got) {
            b->setText(u"%1\n(no model file)"_s.arg(QString(name).replace(u'_', u' ')));
            b->setEnabled(false);
        } else {
            const auto &[f, x, y] = *got;
            if (const auto pic = preview(c, name, x, y)) {
                b->setIcon(QIcon(QPixmap::fromImage(*pic)));
                b->setIconSize(pic->size());
            }
            const long long bad = py::round_int(100 * f.other);
            b->setText(u"%1\n%2"_s.arg(QString(name).replace(u"_Canal"_s, QString()).replace(u'_', u' '),
                                       bad ? u"%1 % on other land"_s.arg(bad)
                                       : f.reaches.size() == 2 ? u"fits — both sea tiles"_s
                                                               : u"reaches %1 of 2 sea tiles"_s.arg(f.reaches.size())));
            b->setToolTip(u"x %1, y %2\n%3"_s.arg(C::fmt_num(x), C::fmt_num(y), QString(fit_text(f, c)).remove(tags)));
            connect(b, &QToolButton::clicked, this, [this, n = name, xx = x, yy = y] { _take(n, xx, yy); });
        }
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        b->setMinimumHeight(178);
        cmp_row->addWidget(b, 1);
    }
    cmp_host->show();
}

void CanalPanel::_take(const QString &model, double x, double y)
{
    const C::Canal *cp = canal(sel_num);
    if (!cp || !_sprite_rel(*cp))
        return;
    const C::Canal c = *cp;
    _edit(u"Canal %1: %2 at x %3, y %4"_s.arg(c.num).arg(model, C::fmt_num(x), C::fmt_num(y)),
          [&] { _write_sprite(c.num, &c, model, py::round_to(x, 2), py::round_to(y, 2)); });
}

std::optional<QImage> CanalPanel::preview(const C::Canal &c, const QString &model, double x, double y, long long scale)
{
    // The map's pixels around the canal (sea blue, the canal's province sand, other land grey, borders dark), the
    // model over them where x / y put it — a QImage.
    const auto &r = _models().render(model);
    const auto a = anchor(c.flag_prov && *c.flag_prov ? *c.flag_prov : c.prov);
    if (!r || !a)
        return std::nullopt;
    const double ox = a->first + x, oy = a->second - y;
    const double mx0 = ox + r->minx, my0 = oy - r->maxz;
    const long long pad = 4;
    long long X0 = (long long)std::floor(mx0) - pad, Y0 = (long long)std::floor(my0) - pad;
    long long X1 = (long long)std::ceil(mx0 + r->w) + pad, Y1 = (long long)std::ceil(my0 + r->h) + pad;
    X0 = std::max(0LL, X0);
    Y0 = std::max(0LL, Y0);
    X1 = std::min(md.w, X1);
    Y1 = std::min(md.h, Y1);
    if (X1 <= X0 || Y1 <= Y0)
        return std::nullopt;
    const long long w = X1 - X0, h = Y1 - Y0;
    const auto &sea = _sea();
    QImage base(int(w), int(h), QImage::Format_RGB888);
    auto id = [&](long long yy, long long xx) { return md.idmap(Y0 + yy, X0 + xx); };
    for (long long yy = 0; yy < h; ++yy) {
        uchar *line = base.scanLine(int(yy));
        for (long long xx = 0; xx < w; ++xx) {
            const auto i = id(yy, xx);
            std::array<std::uint8_t, 3> px{110, 112, 104};
            if (i == c.prov)
                px = {196, 172, 120};
            if (sea[i])
                px = {40, 70, 120};
            const bool edge = (yy > 0 && i != id(yy - 1, xx)) || (xx > 0 && i != id(yy, xx - 1));
            if (edge)
                for (auto &v : px)
                    v = std::uint8_t(double(v) * 0.55);
            line[3 * xx] = px[0];
            line[3 * xx + 1] = px[1];
            line[3 * xx + 2] = px[2];
        }
    }
    QImage img = base.scaled(int(w * scale), int(h * scale));   // (sharp: one map pixel = scale × scale)
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF((mx0 - double(X0)) * double(scale), (my0 - double(Y0)) * double(scale),
                           r->w * double(scale), r->h * double(scale)),
                    r->img);
    }
    if (img.width() > 120 || img.height() > 120)
        img = img.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

void CanalPanel::place_at(double cx, double cy)
{
    // The model's middle to the clicked spot (display coordinates).
    _arm(std::nullopt);
    const C::Canal *cp = canal(sel_num);
    const auto pl = cp ? place(*cp) : std::nullopt;
    if (!pl)
        return;
    const long long num = cp->num;
    const auto &r = _models().render(pl->model);
    const double mx = r ? r->minx + r->w / 2.0 : 0.0, mz = r ? r->maxz - r->h / 2.0 : 0.0;
    const Pt a = pl->a;
    move_to(cx - a.first - mx, a.second - cy - mz, u"Canal %1: model placed elsewhere"_s.arg(num));
}

// ------------------------------------------------------------------ overlay
std::optional<QPainterPath> CanalPanel::outline_path(const C::Canal &c, double dx, double dy)
{
    const auto pl = place(c, dx, dy);
    if (!pl)
        return std::nullopt;
    const auto polys = model_polys(pl->model, pl->a, pl->x, pl->y);
    if (!polys)
        return std::nullopt;
    QPainterPath path;
    for (const auto &[part, ps] : *polys)
        for (const auto &poly : ps) {
            QPolygonF q;
            for (const auto &[x, y] : poly)
                q << QPointF(x, y);
            path.addPolygon(q);
        }
    return path;
}

std::vector<mapview::Shape> CanalPanel::canal_shapes(const C::Canal &c, bool sel, double dx, double dy, bool temp)
{
    std::vector<mapview::Shape> out;
    if (!temp && !c.rows.empty()) {
        const adjacency::Row &r = c.rows[0];
        std::vector<std::optional<Pt>> pts;
        for (long long s : {r.a, r.b}) {
            const auto bp = r.through ? geo.border_point(r.through, s) : std::nullopt;
            pts.push_back(bp ? bp : anchor(s));
        }
        const auto mid = anchor(r.through);
        if (pts[0] && pts[1] && mid) {
            mapview::Shape ln = line_shape({*pts[0], *mid, *pts[1]}, LINE);
            ln.width = sel ? 1.2 : 2.0;
            ln.outline = !sel;                       // (selected: thin — the model shows)
            ln.tag = QVariantList{u"canal"_s, c.num};
            out.push_back(ln);
        }
    }
    const auto pl = place(c, dx, dy);
    if (!pl)
        return out;
    const auto &r = _models().render(pl->model);
    std::optional<Polys> polys;
    if (r) {                                         // the model itself, from above, exactly where the game puts it
        const double ox = pl->a.first + pl->x * C::OFFSET_UNIT;
        const double oy = pl->a.second - pl->y * C::OFFSET_UNIT * (C::OFFSET_Y_NORTH ? 1 : -1);
        const double x = ox + r->minx * C::MODEL_UNIT, y = oy - r->maxz * C::MODEL_UNIT;
        const double w = r->w * C::MODEL_UNIT, h = r->h * C::MODEL_UNIT;
        mapview::Shape im;
        im.kind = u"image"_s;
        im.img = r->img;
        im.rect = QRectF(x, y, w, h);
        im.pts = {QPointF(x, y), QPointF(x + w, y + h)};
        im.opacity = temp ? 0.55 : (sel ? 1.0 : 0.9);
        if (sel && !temp)
            im.frame = Rgb{255, 220, 120};
        out.push_back(im);
    } else {
        polys = model_polys(pl->model, pl->a, pl->x, pl->y);
    }
    if (polys && !polys->isEmpty()) {
        for (const auto &[part, col] : std::vector<std::pair<QString, Rgb>>{{u"water"_s, WATER}, {u"built"_s, BUILT}}) {
            const auto *pp = polys->get(part);
            if (pp && !pp->empty()) {
                mapview::Shape s;
                s.kind = u"poly"_s;
                for (const auto &poly : *pp) {
                    std::vector<QPointF> q;
                    for (const auto &[x, y] : poly) {
                        q.emplace_back(x, y);
                        s.pts.emplace_back(x, y);
                    }
                    s.polys.push_back(q);
                }
                s.color = col;
                s.alpha = sel ? 235 : 150;
                s.halo = sel && !temp;
                s.dash = temp;
                out.push_back(s);
            }
        }
    }
    const double ax = pl->a.first + pl->x * C::OFFSET_UNIT;
    const double ay = pl->a.second - pl->y * C::OFFSET_UNIT * (C::OFFSET_Y_NORTH ? 1 : -1);
    if (!temp) {
        mapview::Shape d;
        d.kind = u"dot"_s;
        d.pts = {QPointF(ax, ay)};
        d.color = sel ? Rgb{255, 220, 120} : Rgb{200, 200, 200};
        d.size = sel ? 3.5 : 2.5;
        d.tag = QVariantList{u"canal"_s, c.num};
        out.push_back(d);
    }
    return out;
}

const std::vector<mapview::Shape> &CanalPanel::prov_edges(long long pid)
{
    // The province's outline along its pixels' edges (thin lines, display coordinates): to see exactly which pixels
    // the model's water and locks fall on.
    static const std::vector<mapview::Shape> none;
    if (!(0 < pid && pid < md.n) || md.area[size_t(pid)] == 0)
        return none;
    const std::pair<long long, long long> key{pid, md.version};
    if (_edge_key == key)
        return _edges;
    const size_t p = size_t(pid);
    const long long x0 = md.x0[p], x1 = md.x1[p], y0 = md.y0[p], y1 = md.y1[p];
    const long long sh = y1 - y0 + 3, sw = x1 - x0 + 3;
    std::vector<std::uint8_t> sub(size_t(sh * sw), 0);
    for (long long yy = 1; yy < sh - 1; ++yy)
        for (long long xx = 1; xx < sw - 1; ++xx)
            sub[size_t(yy * sw + xx)] = md.idmap(y0 + yy - 1, x0 + xx - 1) == pid;
    std::vector<std::pair<Pt, Pt>> segs;
    for (long long yy = 0; yy < sh - 1; ++yy)        // edges between rows: horizontal lines (np.nonzero: row-major)
        for (long long xx = 0; xx < sw; ++xx)
            if (sub[size_t((yy + 1) * sw + xx)] != sub[size_t(yy * sw + xx)])
                segs.push_back({{double(x0 + xx - 1), double(y0 + yy)}, {double(x0 + xx), double(y0 + yy)}});
    for (long long yy = 0; yy < sh; ++yy)            // edges between columns: vertical lines
        for (long long xx = 0; xx < sw - 1; ++xx)
            if (sub[size_t(yy * sw + xx + 1)] != sub[size_t(yy * sw + xx)])
                segs.push_back({{double(x0 + xx), double(y0 + yy - 1)}, {double(x0 + xx), double(y0 + yy)}});
    _edge_key = key;
    _edges.clear();
    for (const auto &[a, b] : segs) {
        mapview::Shape s = line_shape({a, b}, Rgb{255, 214, 110});
        s.width = 1.4;
        s.outline = false;
        _edges.push_back(s);
    }
    return _edges;
}

std::vector<mapview::Shape> CanalPanel::shapes()
{
    if (!doc)
        return {};
    std::vector<mapview::Shape> out;
    const C::Canal *sel = sel_num ? canal(sel_num) : nullptr;
    if (sel && sel->prov) {
        const auto &e = prov_edges(sel->prov);
        out.insert(out.end(), e.begin(), e.end());
    }
    for (const C::Canal &c : _list) {
        const auto s = canal_shapes(c, sel_num && c.num == *sel_num);
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

QString CanalPanel::describe(const std::optional<QVariantList> &tag) const
{
    if (tag && !tag->isEmpty() && tag->value(0).toString() == u"canal") {
        if (const C::Canal *c = canal(tag->value(1).toLongLong())) {
            const moddata::Province *pr = mod.provinces.get(c->prov);
            return u"Canal %1%2 · through #%3 %4 · sea tiles #%5 – #%6%7"_s.arg(c->num)
                .arg(!c->name.isEmpty() ? u" "_s + c->name : QString())
                .arg(c->prov)
                .arg(pr ? pr->name : u"?"_s)
                .arg(c->seas.first)
                .arg(c->seas.second)
                .arg(sel_num && c->num == *sel_num ? u"  · drag the outline to move it"_s : QString());
        }
    }
    return QString();
}

void CanalPanel::map_clicked(const std::optional<QVariantList> &tag)
{
    if (tag && !tag->isEmpty() && tag->value(0).toString() == u"canal")
        select(tag->value(1).toLongLong());
}

// CanalHandler ----------------------------------------------------------------------------------------------------
mapview::ShapeLayer *CanalHandler::layer() const
{
    return p->view->shape_layer;
}

bool CanalHandler::cancel()
{
    const bool had = drag.has_value() || p->pick.has_value();
    drag.reset();
    if (p->pick) {
        p->_arm(std::nullopt);
        if (p->new_btn->isChecked())
            p->new_btn->setChecked(false);
    }
    if (p->view)
        layer()->set_temp({});
    return had;
}

std::optional<canals::Canal> CanalHandler::_canal_at(double x, double y)
{
    const canals::Canal *sel = p->sel_num ? p->canal(p->sel_num) : nullptr;
    std::vector<canals::Canal> order;
    if (sel)
        order.push_back(*sel);
    for (const canals::Canal &c : p->_list)
        if (&c != sel)
            order.push_back(c);
    for (const canals::Canal &c : order) {
        const auto path = p->outline_path(c);
        if (path && path->contains(QPointF(x, y)))
            return c;
    }
    return std::nullopt;
}

bool CanalHandler::press(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (!p->doc)
        return false;
    if (p->pick == u"place"_s) {                     // the model's picture to a spot (away from its canal)
        p->place_at(x, y);
        return true;
    }
    if (p->pick) {
        const long long pid = p->md.province_at(double((long long)x), double((long long)y));
        if (pid)
            p->picked(pid);
        return true;
    }
    std::optional<canals::Canal> c = _canal_at(x, y);
    if (!c) {
        const auto tag = layer()->hit(x, y, tol, QStringList{u"canal"_s});
        if (tag)
            if (const canals::Canal *cp = p->canal(tag->value(1).toLongLong()))
                c = *cp;
    }
    if (!c)
        return false;
    if (!p->sel_num || c->num != *p->sel_num)
        p->select(c->num);
    if (p->place(*c) && p->_sprite_rel(*c))
        drag = std::tuple{c->num, x, y};
    return true;
}

void CanalHandler::move(double x, double y)
{
    if (!drag)
        return;
    const auto [num, sx, sy] = *drag;
    const canals::Canal *cp = p->canal(num);
    if (!cp)
        return;
    const canals::Canal c = *cp;
    const double dx = (x - sx) / canals::OFFSET_UNIT;
    const double dy = -(y - sy) / canals::OFFSET_UNIT * (canals::OFFSET_Y_NORTH ? 1 : -1);
    layer()->set_temp(p->canal_shapes(c, true, dx, dy, true));
    if (const auto pl = p->place(c, dx, dy))
        Q_EMIT p->message(u"Canal %1: x %2, y %3"_s.arg(num).arg(canals::fmt_num(py::round_to(pl->x, 1)),
                                                                 canals::fmt_num(py::round_to(pl->y, 1))));
}

void CanalHandler::release(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (!drag)
        return;
    const auto [num, sx, sy] = *drag;
    drag.reset();
    layer()->set_temp({});
    if (std::abs(x - sx) + std::abs(y - sy) < std::max(0.3, tol * 0.3))
        return;
    const canals::Canal *cp = p->canal(num);
    const auto pl = cp ? p->place(*cp) : std::nullopt;
    if (!pl)
        return;
    const double dx = (x - sx) / canals::OFFSET_UNIT;
    const double dy = -(y - sy) / canals::OFFSET_UNIT * (canals::OFFSET_Y_NORTH ? 1 : -1);
    p->move_to(py::round_to(pl->x + dx, 1), py::round_to(pl->y + dy, 1), u"Canal %1: drag"_s.arg(num));
}

bool CanalHandler::rclick(double, double, double, Qt::KeyboardModifiers)
{
    return false;
}

}  // namespace canalpanel
