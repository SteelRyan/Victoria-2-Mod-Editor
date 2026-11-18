// adjpanel.py — line by line.
#include "ui/adjpanel.h"

#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace adjpanel {

const std::map<QString, QString> KIND_NAMES = {
    {u"strait"_s, u"Strait"_s}, {u"river"_s, u"Crossing"_s}, {u"coast"_s, u"Coast"_s}, {u"canal"_s, u"Canal"_s},
    {u"impassable"_s, u"Impassable"_s}, {u"land"_s, u"Land"_s}, {u"other"_s, u"Other"_s}};
const std::vector<std::pair<QString, QString>> TYPE_ITEMS = {
    {u"sea"_s, u"Sea crossing"_s}, {u"canal"_s, u"Canal"_s}, {u"impassable"_s, u"Impassable"_s},
    {u"land"_s, u"Land link"_s}};
const std::map<QString, QString> TYPE_HELP = {
    {u"sea"_s, u"From / To: the two provinces. Through: the sea tile a fleet can block the crossing from — "
               "0 for none (Meme rivers, coast crossings). Data: 0."_s},
    {u"canal"_s, u"From / To: the two sea tiles. Through: the land province that controls the canal. "
                 "Data: the canal number used by enable_canal = N / is_canal_enabled = N."_s},
    {u"impassable"_s, u"From / To: two provinces that touch — nothing can cross their border. Through / Data: 0."_s},
    {u"land"_s, u"From / To: the two provinces of a land link (e.g. over a strait). Through: the sea tile it goes "
                "over, or 0. Data: 0."_s},
};
const OrderedMap<QString, bool> DEFAULT_SHOW = {
    {u"strait"_s, true}, {u"river"_s, true}, {u"coast"_s, true}, {u"canal"_s, true}, {u"impassable"_s, true},
    {u"land"_s, true}, {u"other"_s, true}, {u"missing"_s, true}, {u"rivermiss"_s, true}, {u"stale"_s, false}};

static QString s_(long long n) { return n == 1 ? QString() : u"s"_s; }
static QString ids_text(const std::vector<long long> &tiles)
{
    QStringList p;
    for (size_t i = 0; i < std::min<size_t>(6, tiles.size()); ++i)
        p << u"#%1"_s.arg(tiles[i]);
    return p.join(u", "_s) + (tiles.size() > 6 ? u" …"_s : QString());
}
static QString hash_ids(const std::vector<long long> &tiles)
{
    QStringList p;
    for (long long t : tiles)
        p << u"#%1"_s.arg(t);
    return p.join(u", "_s);
}
static QList<long long> qlist(const std::vector<long long> &v) { return QList<long long>(v.begin(), v.end()); }
static QVariantList pair_tag(const QString &kind, long long a, long long b)
{
    return {kind, QVariantList{a, b}};
}
static PairLL pair_of(const QVariant &v)
{
    const QVariantList l = v.toList();
    return {l.value(0).toLongLong(), l.value(1).toLongLong()};
}

// ProvField -------------------------------------------------------------------------------------------------------
ProvField::ProvField(const moddata::Mod &mod, bool allow_zero) : mod(mod)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    spin = new QSpinBox();
    spin->setRange(0, 65535);
    spin->setFixedWidth(80);
    spin->setButtonSymbols(QSpinBox::NoButtons);
    spin->setSpecialValueText(allow_zero ? u"0"_s : u"—"_s);
    connect(spin, &QSpinBox::valueChanged, this, [this](int) { _changed(); });
    name = label(QString(), u"faint"_s, false);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *b = new QPushButton(u"◎"_s);
    b->setObjectName(u"icon"_s);
    b->setToolTip(u"Pick on the map"_s);
    connect(b, &QPushButton::clicked, this, &ProvField::pickRequested);
    lay->addWidget(spin);
    lay->addWidget(name, 1);
    lay->addWidget(b);
}

void ProvField::_changed()
{
    name->setText(spin->value() ? prov_name(mod, spin->value()) : QString());
    Q_EMIT changed();
}

long long ProvField::value() const
{
    return spin->value();
}

void ProvField::set(long long v)
{
    spin->setValue(int(v));
}

QString prov_name(const moddata::Mod &mod, long long pid)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    if (!pr)
        return u"not a province"_s;
    QString n = pr->name.trimmed();
    if (n.isEmpty())
        n = u"(unnamed)"_s;
    return n + (pr->sea ? u" (sea)"_s : QString());
}

// (Python's set & set: the smaller set is walked — the right one when both are as large — into a new set)
static grid::PySet<long long> py_and(const grid::PySet<long long> &x, const grid::PySet<long long> &y)
{
    const grid::PySet<long long> *so = &x, *other = &y;
    if (other->size() > so->size())
        std::swap(so, other);
    grid::PySet<long long> out;
    for (long long k : other->items())
        if (so->contains(k))
            out.add(k);
    return out;
}

adjacency::RowFields guess_spec(adjacency::AdjacencyFile &adj, adjacency::AdjGeometry &geo, const moddata::Mod &mod,
                                long long a, long long b)
{
    // A sensible new row between two provinces / sea tiles (type, through, data, comment guessed).
    static const grid::PySet<long long> none;
    const auto &A = geo.md.adjacent;
    const grid::PySet<long long> &Aa = A.get(a) ? *A.get(a) : none, &Ab = A.get(b) ? *A.get(b) : none;
    const bool sa = adj.sea.contains(a), sb = adj.sea.contains(b);
    if (sa != sb) {                                          // coast crossing: sea tile -> province
        const auto [s, l] = sa ? PairLL{a, b} : PairLL{b, a};
        return {s, l, u"sea"_s, 0, 0, adj.coast_comment()};
    }
    if (sa && sb) {                                          // canal between two sea tiles
        std::vector<long long> common;
        for (long long q : py_and(Aa, Ab).items())
            if (mod.provinces.contains(q) && !adj.sea.contains(q))
                common.push_back(q);
        std::sort(common.begin(), common.end());
        const auto used = adj.canal_numbers();
        return {a, b, u"canal"_s, common.empty() ? 0 : common[0],
                used.empty() ? 1 : *std::max_element(used.begin(), used.end()) + 1, QString()};
    }
    // (… & adj.sea: the shared neighbours are the smaller set, so they are walked)
    std::vector<long long> common;
    {
        grid::PySet<long long> c;
        for (long long q : py_and(Aa, Ab).items())
            if (adj.sea.contains(q))
                c.add(q);
        common = c.items();
    }
    const auto riv = !common.empty() ? adjacency::river_name_of(adj, geo, common) : std::nullopt;
    if (riv)                                                 // across a Meme river: same name as its crossings
        return {a, b, u"sea"_s, 0, 0, *riv};
    std::vector<long long> straits;
    const auto &thin = geo.thin_tiles();
    for (long long t : common)
        if (!thin.contains(t) && !adj.no_coast.contains(t))
            straits.push_back(t);
    std::sort(straits.begin(), straits.end());
    return {a, b, u"sea"_s, straits.size() == 1 ? straits[0] : 0, 0, QString()};
}

Pt lerp(const Pt &p, const Pt &q, double f, double w)
{
    // The point f of the way from p to q — around the map's left / right edge (w: the map's width) when that is the
    // shorter way.
    double qx = q.first;
    if (w && std::abs(qx - p.first) > w / 2.0)
        qx += qx < p.first ? w : -w;
    const double x = p.first + (qx - p.first) * f;
    return {w ? py::mod(x, w) : x, p.second + (q.second - p.second) * f};
}

std::vector<std::vector<Pt>> seam_split(const std::vector<Pt> &pts, double w)
{
    // A line as the pieces to draw: where two of its points lie on both sides of the map's left / right edge (the
    // short way between them goes around it), the line runs out of one edge and comes in at the other.
    std::vector<std::vector<Pt>> out;
    std::vector<Pt> cur{pts[0]};
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Pt &p = pts[i], &q = pts[i + 1];
        if (std::abs(q.first - p.first) > w / 2.0) {
            const double qx = q.first + (q.first < p.first ? w : -w);   // q as seen from p's side
            const double edge = qx > p.first ? w : 0.0;
            const double f = qx != p.first ? (edge - p.first) / (qx - p.first) : 0.5;
            const double y = p.second + (q.second - p.second) * f;
            cur.push_back({edge, y});
            out.push_back(cur);
            cur = {{w - edge, y}};
        }
        cur.push_back(q);
    }
    out.push_back(cur);
    return out;
}

static mapview::Shape shape(const QString &kind, const std::vector<Pt> &pts, const adjacency::Rgb &color)
{
    mapview::Shape s;
    s.kind = kind;
    for (const Pt &p : pts)
        s.pts.emplace_back(p.first, p.second);
    s.color = color;
    return s;
}

// MapPanel --------------------------------------------------------------------------------------------------------
MapPanel::MapPanel(moddata::Mod &mod, adjacency::AdjGeometry &geo, QWidget *parent)
    : QWidget(parent), mod(mod), geo(geo), show_(DEFAULT_SHOW)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- header
    auto *s = new widgets::Section();
    title = label(u"Nothing selected"_s, u"title"_s);
    sub = label(QString(), u"sub"_s);
    sub->setTextFormat(Qt::RichText);
    s->lay->addWidget(title);
    s->lay->addWidget(sub);
    s->lay->addWidget(label(u"map/adjacencies.csv — applies to every start date. Click a line or marker on the "
                            "map to select that adjacency; Ctrl+click always selects provinces / sea tiles."_s,
                            u"faint"_s));
    lay->addWidget(s);

    // --- what to show
    s = new widgets::Section();
    edit_btn = new QPushButton(u"✎  Edit crossings on the map"_s);
    edit_btn->setObjectName(u"primary"_s);
    edit_btn->setCheckable(true);
    connect(edit_btn, &QPushButton::toggled, this, [this](bool on) { _edit_mode(on); });
    s->lay->addWidget(edit_btn);
    edit_help = label(u"• Left-drag the end of a crossing onto another province to move it\n"
                      "• Right-click a province, then left-click a second one, to add a crossing\n"
                      "• Right-click a crossing to delete it (a red missing one: to add it)\n"
                      "• Esc cancels · right-drag still pans · Ctrl+Z undoes"_s,
                      u"sub"_s);
    edit_help->hide();
    s->lay->addWidget(edit_help);
    auto *b = new QPushButton(u"⚓  Canals — add one, its model, how it opens ▸"_s);
    b->setToolTip(u"The Canals page: every canal with its model on the map (Kiel / Panama / Suez), moved pixel by "
                  "pixel or dragged, and how it opens"_s);
    connect(b, &QPushButton::clicked, this, &MapPanel::canalsRequested);
    s->lay->addWidget(b);
    s->lay->addWidget(label(u"Show on the map"_s));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    std::vector<std::tuple<QString, QString, adjacency::Rgb>> entries;
    for (const auto &c : adjacency::CATEGORIES)
        entries.emplace_back(c.key, c.label, c.color);
    entries.emplace_back(u"missing"_s, u"Missing coast crossings"_s, adjacency::MISSING_COLOR);
    entries.emplace_back(u"rivermiss"_s, u"Missing Meme-river bank crossings (dashed)"_s, adjacency::MISSING_COLOR);
    entries.emplace_back(u"stale"_s, u"Coast rows whose tiles don't touch (stale)"_s, adjacency::STALE_COLOR);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto &[k, t, c] = entries[i];
        auto *cb = new QCheckBox(t);
        cb->setIcon(widgets::swatch_icon(c, 10));
        if (const QString *h = adjacency::CAT_HELP.get(k))
            cb->setToolTip(*h);
        const bool *on = show_.get(k);
        cb->setChecked(on ? *on : true);
        connect(cb, &QCheckBox::toggled, this, [this, key = k](bool on) { _set_show(key, on); });
        grid->addWidget(cb, int(i), 0);
        show_boxes[k] = cb;
    }
    s->lay->addLayout(grid);
    s->lay->addWidget(label(u"Hover a kind for what it is. In the list below, a row in red has something wrong — "
                            "hover it for what."_s,
                            u"faint"_s));
    only_sel = new QCheckBox(u"Only adjacencies of the selection"_s);
    connect(only_sel, &QCheckBox::toggled, this, [this](bool) { Q_EMIT overlayChanged(); });
    s->lay->addWidget(only_sel);
    problem_note = label(QString(), u"warnAmber"_s);
    s->lay->addWidget(problem_note);
    lay->addWidget(s);

    // --- coast crossings
    s = new widgets::Section();
    s->lay->addWidget(label(u"Coast crossings (landing penalty)"_s));
    coast_info = label(QString(), u"sub"_s);
    coast_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(coast_info);
    auto *row = new QHBoxLayout();
    gen_all = new QPushButton(u"Add all missing"_s);
    connect(gen_all, &QPushButton::clicked, this, [this] { _generate(false); });
    gen_sel = new QPushButton(u"Add for selection"_s);
    connect(gen_sel, &QPushButton::clicked, this, [this] { _generate(true); });
    row->addWidget(gen_all);
    row->addWidget(gen_sel);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    next_miss = new QPushButton(u"Go to next missing ▸"_s);
    connect(next_miss, &QPushButton::clicked, this, [this] { _next_missing(); });
    rm_stale = new QPushButton(u"Remove stale rows…"_s);
    connect(rm_stale, &QPushButton::clicked, this, [this] { _remove_stale(); });
    row->addWidget(next_miss);
    row->addWidget(rm_stale);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- meme rivers
    s = new widgets::Section();
    s->lay->addWidget(label(u"Meme rivers"_s));
    s->lay->addWidget(label(u"A Meme river is a group of sea tiles that works as a river: its two banks are joined "
                            "by crossings (named after the river) and its shores need no coast crossings. "
                            "It is written as  “# Meme river: <name> = sea tile …”  above its crossings in "
                            "adjacencies.csv. Select the tile(s) on the map (Ctrl+click for several) or pick "
                            "one here:"_s,
                            u"faint"_s));
    river_pick = new QComboBox();
    river_pick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(river_pick, &QComboBox::activated, this, [this](int i) { _pick_river_tile(i); });
    s->lay->addWidget(river_pick);
    river_state = label(QString(), u"sub"_s);
    river_state->setTextFormat(Qt::RichText);
    s->lay->addWidget(river_state);
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->addWidget(label(u"Name"_s, u"sub"_s, false), 0, 0);
    river_name = new QLineEdit();
    river_name->setPlaceholderText(u"Meme-River 18"_s);
    connect(river_name, &QLineEdit::textChanged, this, [this](const QString &) { _river_changed(false); });
    grid->addWidget(river_name, 0, 1);
    grid->addWidget(label(u"Max. gap"_s, u"sub"_s, false), 1, 0);
    river_gap = new QDoubleSpinBox();
    river_gap->setRange(1.0, 8.0);
    river_gap->setSingleStep(0.5);
    river_gap->setDecimals(1);
    river_gap->setValue(2.3);
    river_gap->setSuffix(u" px"_s);
    river_gap->setToolTip(u"How far apart (in map pixels) the two banks may be to get a crossing.\n"
                          "2.3 = straight or diagonally across a 1-px river."_s);
    connect(river_gap, &QDoubleSpinBox::valueChanged, this, [this](double) { _gap_changed(); });
    grid->addWidget(river_gap, 1, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    mark_btn = new QPushButton(u"Mark as Meme river"_s);
    connect(mark_btn, &QPushButton::clicked, this, [this] { _mark_river(); });
    unmark_btn = new QPushButton(u"Unmark"_s);
    connect(unmark_btn, &QPushButton::clicked, this, [this] { _unmark_river(); });
    row->addWidget(mark_btn, 1);
    row->addWidget(unmark_btn);
    s->lay->addLayout(row);
    river_info = label(QString(), u"sub"_s);
    s->lay->addWidget(river_info);
    river_btn = new QPushButton(u"Create crossings"_s);
    river_btn->setObjectName(u"primary"_s);
    connect(river_btn, &QPushButton::clicked, this, [this] { _make_river(); });
    s->lay->addWidget(river_btn);
    rivers_all = label(QString(), u"sub"_s);
    rivers_all->setTextFormat(Qt::RichText);
    s->lay->addWidget(rivers_all);
    row = new QHBoxLayout();
    river_all_btn = new QPushButton(u"Add all missing bank crossings"_s);
    connect(river_all_btn, &QPushButton::clicked, this, [this] { _add_all_river_missing(); });
    find_btn = new QPushButton(u"Mark the rivers already in the file…"_s);
    find_btn->setToolTip(u"Crossing groups named e.g. 'Meme-River 5' that mostly go across one sea tile:\n"
                         "mark those tiles as that Meme river."_s);
    connect(find_btn, &QPushButton::clicked, this, [this] { _find_rivers(); });
    row->addWidget(river_all_btn, 1);
    row->addWidget(find_btn, 1);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- no-coast sea tiles
    s = new widgets::Section();
    s->lay->addWidget(label(u"No-coast sea tiles"_s));
    s->lay->addWidget(label(u"Shores of no-coast sea tiles need no coast crossings — nothing else (lakes, shallows, "
                            "…). They are listed at the top of adjacencies.csv as “# no_coast = …” comment "
                            "lines (the game ignores them)."_s,
                            u"faint"_s));
    nc_state = label(QString(), u"sub"_s);
    nc_state->setTextFormat(Qt::RichText);
    s->lay->addWidget(nc_state);
    row = new QHBoxLayout();
    // the same layout as the Meme-river buttons: a wide "Mark as …" and a small "Unmark"
    nc_on_btn = new QPushButton(u"Mark as no-coast"_s);
    connect(nc_on_btn, &QPushButton::clicked, this, [this] { _set_no_coast(true); });
    nc_off_btn = new QPushButton(u"Unmark"_s);
    connect(nc_off_btn, &QPushButton::clicked, this, [this] { _set_no_coast(false); });
    row->addWidget(nc_on_btn, 1);
    row->addWidget(nc_off_btn);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // --- rows of the selection
    s = new widgets::Section();
    list_title = label(u"Adjacencies of the selection"_s);
    s->lay->addWidget(list_title);
    tree = new QTreeWidget();
    tree->setColumnCount(5);
    tree->setHeaderLabels({u"Kind"_s, u"From"_s, u"To"_s, u"Through"_s, u"Comment"_s});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    // the columns share the panel's width (no horizontal scrollbar): names that don't fit end in "…"
    QHeaderView *h = tree->header();
    h->setStretchLastSection(false);
    const QHeaderView::ResizeMode modes[] = {QHeaderView::ResizeToContents, QHeaderView::Stretch, QHeaderView::Stretch,
                                             QHeaderView::ResizeToContents, QHeaderView::Stretch};
    for (int col = 0; col < 5; ++col)
        h->setSectionResizeMode(col, modes[col]);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setMinimumHeight(240);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] { _tree_picked(); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) { _zoom_item(it); });
    s->lay->addWidget(tree);
    rm_all_btn = new QPushButton(u"Remove all adjacencies of the selection"_s);
    rm_all_btn->setToolTip(u"Every row of adjacencies.csv from, to or through the selected provinces / sea tiles "
                           "(one undo step, written on Apply)"_s);
    connect(rm_all_btn, &QPushButton::clicked, this, [this] { _remove_all_here(); });
    s->lay->addWidget(rm_all_btn);
    lay->addWidget(s);

    // --- editor
    s = new widgets::Section();
    ed_title = label(u"New adjacency"_s);
    s->lay->addWidget(ed_title);
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    ed_type = new QComboBox();
    for (const auto &[k, t] : TYPE_ITEMS)
        ed_type->addItem(t, k);
    connect(ed_type, &QComboBox::currentIndexChanged, this, [this](int) { _editor_changed(); });
    ed_a = new ProvField(mod);
    ed_b = new ProvField(mod);
    ed_t = new ProvField(mod, true);
    for (const auto &[f, key] : std::vector<std::pair<ProvField *, QString>>{
             {ed_a, u"a"_s}, {ed_b, u"b"_s}, {ed_t, u"through"_s}}) {
        connect(f, &ProvField::pickRequested, this, [this, k = key] { Q_EMIT pickRequested(k); });
        connect(f, &ProvField::changed, this, [this] { _editor_changed(); });
    }
    ed_data = new QSpinBox();
    ed_data->setRange(0, 9999);
    ed_data->setFixedWidth(80);
    connect(ed_data, &QSpinBox::valueChanged, this, [this](int) { _editor_changed(); });
    ed_comment = new QLineEdit();
    const std::vector<std::pair<QString, QWidget *>> rows{
        {u"Type"_s, ed_type}, {u"From"_s, ed_a}, {u"To"_s, ed_b}, {u"Through"_s, ed_t}, {u"Data"_s, ed_data},
        {u"Comment"_s, ed_comment}};
    for (size_t r = 0; r < rows.size(); ++r) {
        grid->addWidget(label(rows[r].first, u"sub"_s, false), int(r), 0);
        grid->addWidget(rows[r].second, int(r), 1);
    }
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    ed_help = label(QString(), u"faint"_s);
    s->lay->addWidget(ed_help);
    ed_warn = label(QString(), u"warnAmber"_s);
    s->lay->addWidget(ed_warn);
    row = new QHBoxLayout();
    b = new QPushButton(u"From selection"_s);
    b->setToolTip(u"Fill in From / To (and a guess for type and Through) from the selected provinces / sea tiles"_s);
    connect(b, &QPushButton::clicked, this, [this] { _from_selection(); });
    row->addWidget(b);
    b = new QPushButton(u"⇄ Swap"_s);
    connect(b, &QPushButton::clicked, this, [this] { _swap(); });
    row->addWidget(b);
    b = new QPushButton(u"Clear"_s);
    connect(b, &QPushButton::clicked, this, [this] { select_row(std::nullopt); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    add_btn = new QPushButton(u"＋ Add as new"_s);
    add_btn->setObjectName(u"primary"_s);
    connect(add_btn, &QPushButton::clicked, this, [this] { _add(); });
    upd_btn = new QPushButton(u"Save changes"_s);
    connect(upd_btn, &QPushButton::clicked, this, [this] { _update(); });
    del_btn = new QPushButton(u"Delete"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _delete(); });
    row->addWidget(add_btn);
    row->addWidget(upd_btn);
    row->addWidget(del_btn);
    s->lay->addLayout(row);
    lay->addWidget(s);
    lay->addStretch(1);
    _editor_changed();
}

// ------------------------------------------------------------------ helpers
adjacency::AdjacencyFile &MapPanel::adj() const
{
    return doc->map<adjacency::AdjacencyFile>(u"adjacency"_s);
}

void MapPanel::_edit_mode(bool on)
{
    edit_help->setVisible(on);
    handler.cancel();
    Q_EMIT editModeChanged(on);
    Q_EMIT overlayChanged();
}

void MapPanel::_set_show(const QString &key, bool on)
{
    show_[key] = on;
    Q_EMIT overlayChanged();
}

const OrderedMap<QString, adjacency::RiverStatus> &MapPanel::river_stat()
{
    if (!_rstat)
        _rstat = adjacency::river_status(adj(), geo, river_gap->value());
    return *_rstat;
}

void MapPanel::_gap_changed()
{
    _rstat.reset();
    refresh(*doc, sel, sea_sel, std::nullopt);
}

const adjacency::CoastStatus &MapPanel::status()
{
    if (!_status)
        _status = adjacency::coast_status(adj(), geo);
    return *_status;
}

QString MapPanel::pname(long long pid) const
{
    return pid ? u"#%1 %2"_s.arg(pid).arg(prov_name(mod, pid)) : u"—"_s;
}

// ------------------------------------------------------------------ refresh
void MapPanel::refresh(document::Document &d, const std::vector<long long> &s, const std::vector<long long> &ss,
                       const std::optional<std::pair<QString, QString>> &header)
{
    doc = &d;
    sel = s;
    sea_sel = ss;
    adjacency::AdjacencyFile &a = adj();
    if (header) {
        title->setText(header->first);
        sub->setText(header->second);
    }
    _status.reset();
    _rstat.reset();
    _probs = adjacency::problems(a, geo, mod);
    const adjacency::CoastStatus &st = status();
    // category counts on the checkboxes
    std::map<QString, long long> cnt;
    for (const adjacency::Row &r : a.rows)
        cnt[a.category(r)] += 1;
    for (const auto &c : adjacency::CATEGORIES)
        show_boxes[c.key]->setText(u"%1  (%2)"_s.arg(c.label).arg(cnt[c.key]));
    show_boxes[u"missing"_s]->setText(u"Missing coast crossings  (%1)"_s.arg(st.missing.size()));
    show_boxes[u"stale"_s]->setText(u"Coast rows whose tiles don't touch (stale)  (%1)"_s.arg(st.stale.size()));
    const long long np_ = _probs.size();
    problem_note->setText(np_ ? u"⚠ %1 row%2 look wrong — shown in red in the list below when selected."_s.arg(np_).arg(
                                    s_(np_))
                              : QString());
    problem_note->setVisible(np_ != 0);
    // coast
    const long long pairs = (long long)st.pairs.size();
    const auto &miss = st.missing;
    const auto rt = a.river_tiles();
    long long nc_only = 0;
    for (long long t : a.no_coast)
        nc_only += rt.contains(t) ? 0 : 1;
    coast_info->setText(
        u"%1 sea tile ↔ province borders · <b>%2</b> have a crossing · <span style='color:%3'><b>%4</b> "
        "missing</span> · %5 stale row%6<br><span style='color:%7'>Not checked: %8 shores of %9 Meme-river "
        "tiles, %10 shores of %11 no-coast tiles.</span>"_s.arg(pairs)
            .arg(pairs - (long long)miss.size())
            .arg(!miss.empty() ? u"#ff5ac8"_s : theme::INK_DIM)
            .arg(miss.size())
            .arg(st.stale.size())
            .arg(s_((long long)st.stale.size()))
            .arg(theme::INK_FAINT)
            .arg(st.river_pairs.size())
            .arg(rt.size())
            .arg(st.nc_pairs.size())
            .arg(nc_only));
    const auto sel_miss = _missing_for_selection();
    gen_all->setText(u"Add all %1 missing"_s.arg(miss.size()));
    gen_all->setEnabled(!miss.empty());
    gen_sel->setText(u"Add %1 for selection"_s.arg(sel_miss.size()));
    gen_sel->setEnabled(!sel_miss.empty());
    next_miss->setEnabled(!miss.empty());
    rm_stale->setText(u"Remove %1 stale rows…"_s.arg(st.stale.size()));
    rm_stale->setEnabled(!st.stale.empty());
    // river tile picker
    _fill_river_pick();
    const auto found = adjacency::detect_rivers(a, geo);
    find_btn->setText(!found.empty() ? u"Mark the %1 rivers already in the file…"_s.arg(found.size())
                                     : u"No unmarked rivers in the file"_s);
    find_btn->setEnabled(!found.empty());
    const auto &rs = river_stat();
    long long nmiss = 0, nexp = 0;
    for (const auto &[n, v] : rs) {
        nmiss += (long long)v.missing.size();
        nexp += v.expected.size();
    }
    rivers_all->setText(u"%1 Meme rivers · %2 bank crossings expected · <span style='color:%3'><b>%4</b> "
                        "missing</span>"_s.arg(a.marks.size())
                            .arg(nexp)
                            .arg(nmiss ? u"#ff5ac8"_s : theme::INK_DIM)
                            .arg(nmiss));
    river_all_btn->setText(u"Add all %1 missing bank crossings"_s.arg(nmiss));
    river_all_btn->setEnabled(nmiss != 0);
    _river_changed(false);
    // list + editor
    if (sel_uid && !a.get(*sel_uid))
        sel_uid.reset();
    _fill_tree();
    _editor_changed();
    Q_EMIT overlayChanged();
}

std::vector<PairLL> MapPanel::_missing_for_selection()
{
    const std::set<long long> land(sel.begin(), sel.end()), sea(sea_sel.begin(), sea_sel.end());
    std::vector<PairLL> out;
    for (const auto &p : status().missing)
        if (sea.count(p.first) || land.count(p.second))
            out.emplace_back(p.first, p.second);
    return out;
}

void MapPanel::_fill_river_pick()
{
    adjacency::AdjacencyFile &a = adj();
    const auto &nc = a.no_coast;
    const auto marks = a.river_tiles();
    std::set<long long> cands;
    for (long long t : geo.thin_tiles().items())
        cands.insert(t);
    for (long long t : nc)
        cands.insert(t);
    for (const auto &[t, m] : marks)
        cands.insert(t);
    for (long long p : a.sea)
        if (mod.provinces.get(p)->name.trimmed().isEmpty() && geo.md.area[size_t(p)] > 0)
            cands.insert(p);
    std::vector<long long> order(cands.begin(), cands.end());
    auto key = [&](long long t) {
        const auto *m = marks.get(t);
        return std::make_tuple(!m, m ? m->name : QString(), !nc.contains(t), t);
    };
    std::stable_sort(order.begin(), order.end(), [&](long long x, long long y) { return key(x) < key(y); });
    std::vector<std::pair<QString, long long>> items;
    for (long long t : order) {
        long long banks = 0;
        if (const auto *nb = geo.md.adjacent.get(t))
            for (long long q : nb->items())
                if (const auto *pr = mod.provinces.get(q); pr && !pr->sea)
                    ++banks;
        const auto *m = marks.get(t);
        const QString what = m ? m->name : (nc.contains(t) ? u"no-coast"_s : u"—"_s);
        items.emplace_back(u"#%1 %2 — %3 · %4 shore provinces"_s.arg(t).arg(prov_name(mod, t), what).arg(banks), t);
    }
    if (_river_items && *_river_items == items)
        return;
    _river_items = items;
    river_pick->clear();
    river_pick->addItem(u"Pick a sea tile (Meme rivers first, then no-coast, then narrow / unnamed)…"_s, QVariant());
    for (const auto &[text, t] : items)
        river_pick->addItem(text, t);
}

void MapPanel::_pick_river_tile(int i)
{
    const long long t = river_pick->itemData(i).toLongLong();
    if (t) {
        Q_EMIT selectRequested({}, {t});
        Q_EMIT zoomRequested(geo.md.cx[size_t(t)], geo.md.cy[size_t(t)]);
    }
}

// ------------------------------------------------------------------ list
std::vector<adjacency::Row> MapPanel::_rows_here() const
{
    std::set<long long> ids(sel.begin(), sel.end());
    ids.insert(sea_sel.begin(), sea_sel.end());
    if (ids.empty())
        return {};
    std::vector<adjacency::Row> out;
    for (const adjacency::Row &r : adj().rows)
        if (ids.count(r.a) || ids.count(r.b) || ids.count(r.through))
            out.push_back(r);
    return out;
}

void MapPanel::_remove_all_here()
{
    const auto rows = _rows_here();
    if (rows.empty())
        return;
    std::set<long long> idset(sel.begin(), sel.end());
    idset.insert(sea_sel.begin(), sea_sel.end());
    const std::vector<long long> ids(idset.begin(), idset.end());
    const QString who = ids.size() == 1 ? pname(ids[0]) : u"%1 provinces"_s.arg(ids.size());
    QList<long long> uids;
    for (const auto &r : rows)
        uids << r.uid;
    sel_uid.reset();
    const QString what = uids.size() == 1 ? u"the adjacency"_s : u"all %1 adjacencies"_s.arg(uids.size());
    _edit(u"Remove %1 of %2"_s.arg(what, who), [uids](adjacency::AdjacencyFile &f) { f.del(uids); });
    Q_EMIT message(u"Removed %1 of %2 — Ctrl+Z brings %3 back, Apply writes it."_s.arg(
        what, who, uids.size() == 1 ? u"it"_s : u"them"_s));
}

void MapPanel::_fill_tree()
{
    adjacency::AdjacencyFile &a = adj();
    tree->blockSignals(true);
    tree->clear();
    const auto rows = _rows_here();
    std::set<long long> idset(sel.begin(), sel.end());
    idset.insert(sea_sel.begin(), sea_sel.end());
    const long long n_ids = (long long)idset.size();
    const QString whose = n_ids == 1 ? u"this province"_s : u"the %1 selected"_s.arg(n_ids);
    rm_all_btn->setText(!rows.empty() ? (rows.size() == 1 ? u"Remove the adjacency of %1"_s.arg(whose)
                                                          : u"Remove all %1 adjacencies of %2"_s.arg(rows.size()).arg(whose))
                                      : u"Remove all adjacencies of the selection"_s);
    rm_all_btn->setEnabled(!rows.empty());
    const auto miss = _missing_for_selection();
    const QBrush red(QColor(u"#ff8a7a"_s));
    QTreeWidgetItem *cur = nullptr;
    for (const adjacency::Row &r : rows) {
        const QString cat = a.category(r);
        auto *it = new QTreeWidgetItem(QStringList{KIND_NAMES.at(cat), pname(r.a), pname(r.b),
                                                   r.through ? pname(r.through) : u"—"_s, r.comment});
        it->setIcon(0, widgets::swatch_icon(*adjacency::CAT_COLOR.get(cat), 10));
        it->setData(0, Qt::UserRole, QVariantList{u"row"_s, r.uid});
        QStringList warn;
        if (const QStringList *ps = _probs.get(r.uid))
            for (const QString &p : *ps)
                warn << u"⚠ "_s + p;
        const QString tip = u"%1;%2;%3;%4;%5;%6"_s.arg(r.a).arg(r.b).arg(r.type).arg(r.through).arg(r.data).arg(r.comment) +
                            u"\n"_s + warn.join(u"\n"_s);
        for (int c = 0; c < 5; ++c) {
            it->setToolTip(c, tip.trimmed());
            if (_probs.contains(r.uid))
                it->setForeground(c, red);
        }
        tree->addTopLevelItem(it);
        if (sel_uid && r.uid == *sel_uid)
            cur = it;
    }
    for (const auto &[s, l] : miss) {
        auto *it = new QTreeWidgetItem(QStringList{u"Missing"_s, pname(s), pname(l), u"—"_s, u"no coast crossing"_s});
        it->setIcon(0, widgets::swatch_icon(adjacency::MISSING_COLOR, 10));
        it->setData(0, Qt::UserRole, pair_tag(u"missing"_s, s, l));
        for (int c = 0; c < 5; ++c)
            it->setForeground(c, QBrush(QColor(theme::INK_FAINT)));
        tree->addTopLevelItem(it);
        if (sel_missing && *sel_missing == PairLL{s, l})
            cur = it;
    }
    if (cur) {
        cur->setSelected(true);
        tree->scrollToItem(cur);
    }
    tree->blockSignals(false);
    const long long n = (long long)rows.size();
    list_title->setText(!sel.empty() || !sea_sel.empty()
                            ? u"Adjacencies of the selection  (%1%2)"_s.arg(n).arg(
                                  !miss.empty() ? u", %1 missing"_s.arg(miss.size()) : QString())
                            : u"Adjacencies of the selection — select provinces or sea tiles"_s);
}

void MapPanel::_tree_picked()
{
    const auto items = tree->selectedItems();
    if (items.isEmpty())
        return;
    const QVariantList d = items[0]->data(0, Qt::UserRole).toList();
    const QString kind = d.value(0).toString();
    if (kind == u"row") {
        select_row(d.value(1).toLongLong(), true);
    } else {
        const PairLL key = pair_of(d.value(1));
        sel_missing = key;
        select_row(std::nullopt, true);
        ed_type->setCurrentIndex(0);
        ed_a->set(key.first);
        ed_b->set(key.second);
        ed_t->set(0);
        ed_data->setValue(0);
        ed_comment->setText(adj().coast_comment());
        Q_EMIT overlayChanged();
    }
}

void MapPanel::_zoom_item(QTreeWidgetItem *it)
{
    const QVariantList d = it->data(0, Qt::UserRole).toList();
    std::optional<Pt> pt;
    if (d.value(0).toString() == u"missing") {
        const PairLL key = pair_of(d.value(1));
        pt = _point_of(key.first, key.second);
    } else if (const adjacency::Row *r = adj().get(d.value(1).toLongLong())) {
        pt = _point_of(r->a, r->b);
    }
    if (pt)
        Q_EMIT zoomRequested(pt->first, pt->second);
}

std::optional<Pt> MapPanel::_point_of(long long a, long long b)
{
    // A map point for a row or a (sea, land) pair.
    if (const auto bp = geo.border_point(a, b))
        return bp;
    if (const auto an = geo.anchors(a, b))
        return lerp(an->first, an->second, 0.5, double(geo.md.w));
    return std::nullopt;
}

// ------------------------------------------------------------------ selection from the map
void MapPanel::select_row(std::optional<long long> uid, bool from_tree)
{
    sel_uid = uid;
    if (uid)
        sel_missing.reset();
    const adjacency::Row *r = uid ? adj().get(*uid) : nullptr;
    ed_title->setText(r ? u"Edit adjacency"_s : u"New adjacency"_s);
    if (r) {
        const adjacency::Row row = *r;
        int i = ed_type->findData(row.type);
        if (i < 0) {
            ed_type->addItem(row.type, row.type);
            i = ed_type->count() - 1;
        }
        ed_type->setCurrentIndex(i);
        ed_a->set(row.a);
        ed_b->set(row.b);
        ed_t->set(row.through);
        ed_data->setValue(int(row.data));
        ed_comment->setText(row.comment);
    }
    if (!from_tree)
        _fill_tree();
    _editor_changed();
    Q_EMIT overlayChanged();
}

void MapPanel::map_clicked(const QVariantList &tag)
{
    // A shape on the map was clicked: select its row (and its provinces, so it shows in the list).
    QString kind = tag.value(0).toString();
    const QVariant key = tag.value(1);
    if (kind == u"x" || kind == u"end")
        kind = u"row"_s;
    if (kind == u"missingline")
        kind = u"missing"_s;
    if (kind == u"rivermiss") {
        const PairLL k = pair_of(key);
        Q_EMIT selectRequested({k.first, k.second}, {});
        return;
    }
    if (kind == u"row") {
        const adjacency::AdjacencyFile &a = adj();
        const adjacency::Row *r = a.get(key.toLongLong());
        if (!r)
            return;
        sel_uid = key.toLongLong();
        sel_missing.reset();
        QList<long long> land, sea;
        for (long long p : {r->a, r->b, r->through})
            if (p)
                (a.sea.contains(p) ? sea : land) << p;
        Q_EMIT selectRequested(land, sea);
        select_row(key.toLongLong());
    } else if (kind == u"missing") {
        const PairLL k = pair_of(key);
        sel_missing = k;
        Q_EMIT selectRequested({k.second}, {k.first});
    }
}

void MapPanel::set_pick(const QString &field, long long pid)
{
    (field == u"a" ? ed_a : field == u"b" ? ed_b : ed_t)->set(pid);
}

// ------------------------------------------------------------------ editor
adjacency::Row MapPanel::_editor_row() const
{
    adjacency::Row r;
    r.uid = -1;
    r.a = ed_a->value();
    r.b = ed_b->value();
    const QString t = ed_type->currentData().toString();
    r.type = t.isEmpty() ? u"sea"_s : t;
    r.through = ed_t->value();
    r.data = ed_data->value();
    r.comment = ed_comment->text().trimmed();
    return r;
}

void MapPanel::_editor_changed()
{
    if (!doc)
        return;
    const adjacency::Row r = _editor_row();
    const auto th = TYPE_HELP.find(r.type);
    ed_help->setText(th != TYPE_HELP.end() ? th->second : QString());
    const bool ok = r.a && r.b;
    QStringList warns = ok ? adjacency::check_row(adj(), geo, mod, r) : QStringList();
    const auto used = adj().canal_numbers();
    if (r.type == u"canal" && std::find(used.begin(), used.end(), r.data) != used.end()) {
        const adjacency::Row *s = sel_uid ? adj().get(*sel_uid) : nullptr;
        if (!(s && s->data == r.data))
            warns << u"canal number %1 is already used — next free: %2"_s.arg(r.data).arg(_next_canal());
    }
    QStringList lines;
    for (const QString &w : warns)
        lines << u"⚠ "_s + w;
    ed_warn->setText(lines.join(u"\n"_s));
    ed_warn->setVisible(!warns.isEmpty());
    add_btn->setEnabled(ok);
    const adjacency::Row *cur = sel_uid ? adj().get(*sel_uid) : nullptr;
    const bool same = cur && cur->a == r.a && cur->b == r.b && cur->type == r.type && cur->through == r.through &&
                      cur->data == r.data && cur->comment == r.comment;
    upd_btn->setEnabled(ok && cur && !same);
    del_btn->setEnabled(cur != nullptr);
}

long long MapPanel::_next_canal() const
{
    const auto used = adj().canal_numbers();
    return used.empty() ? 1 : *std::max_element(used.begin(), used.end()) + 1;
}

void MapPanel::_swap()
{
    const long long a = ed_a->value(), b = ed_b->value();
    ed_a->set(b);
    ed_b->set(a);
}

void MapPanel::_from_selection()
{
    const std::vector<long long> land = sel, sea = sea_sel;
    std::vector<long long> ids = land;
    ids.insert(ids.end(), sea.begin(), sea.end());
    if (ids.empty())
        return;
    select_row(std::nullopt);
    if (ids.size() == 1) {
        ed_a->set(ids[0]);
        return;
    }
    long long a, b;
    if (!land.empty() && !sea.empty()) {
        a = sea[0];
        b = land[0];
    } else if (land.size() >= 2) {
        a = land[0];
        b = land[1];
    } else {
        a = sea[0];
        b = sea[1];
    }
    fill_editor(guess_spec(adj(), geo, mod, a, b));
}

void MapPanel::fill_editor(const adjacency::RowFields &spec)
{
    const int i = ed_type->findData(spec.type);
    ed_type->setCurrentIndex(std::max(0, i));
    ed_a->set(spec.a);
    ed_b->set(spec.b);
    ed_t->set(spec.through);
    ed_data->setValue(int(spec.data));
    ed_comment->setText(spec.comment);
}

void MapPanel::_edit(const QString &label_text, const std::function<void(adjacency::AdjacencyFile &)> &fn)
{
    doc->adj_edit(label_text, fn);
}

void MapPanel::_add()
{
    const adjacency::Row r = _editor_row();
    const adjacency::RowFields spec{r.a, r.b, r.type, r.through, r.data, r.comment};
    std::optional<long long> uid;
    _edit(u"Add adjacency %1 → %2"_s.arg(r.a).arg(r.b), [&](adjacency::AdjacencyFile &f) { uid = f.add({spec})[0]; });
    sel_missing.reset();
    select_row(uid);
}

void MapPanel::_update()
{
    const adjacency::Row r = _editor_row();
    const long long uid = *sel_uid;
    _edit(u"Edit adjacency %1 → %2"_s.arg(r.a).arg(r.b), [&](adjacency::AdjacencyFile &f) {
        f.update(uid, [&](adjacency::Row &x) {
            x.a = r.a;
            x.b = r.b;
            x.type = r.type;
            x.through = r.through;
            x.data = r.data;
            x.comment = r.comment;
        });
    });
}

void MapPanel::_delete()
{
    if (!sel_uid)
        return;
    const long long uid = *sel_uid;
    const adjacency::Row *rp = adj().get(uid);
    if (!rp)
        return;
    const adjacency::Row r = *rp;
    sel_uid.reset();
    _edit(u"Delete adjacency %1 → %2"_s.arg(r.a).arg(r.b), [uid](adjacency::AdjacencyFile &f) { f.del({uid}); });
    select_row(std::nullopt);
}

// ------------------------------------------------------------------ coast crossings
void MapPanel::_generate(bool selection_only)
{
    std::vector<PairLL> pairs;
    if (selection_only) {
        pairs = _missing_for_selection();
    } else {
        for (const auto &p : status().missing)
            pairs.emplace_back(p.first, p.second);
    }
    if (pairs.empty())
        return;
    const QString com = adj().coast_comment();
    std::sort(pairs.begin(), pairs.end());
    std::vector<adjacency::RowFields> specs;
    for (const auto &[s, l] : pairs)
        specs.push_back({s, l, u"sea"_s, 0, 0, com});
    _edit(u"Add %1 coast crossings"_s.arg(specs.size()), [&](adjacency::AdjacencyFile &f) { f.add(specs); });
    Q_EMIT message(u"Added %1 coast crossings (sea tile;province;sea;0;0;%2) — written on Apply, Ctrl+Z undoes "
                   "it."_s.arg(specs.size())
                       .arg(com));
}

void MapPanel::_next_missing()
{
    const auto &miss = status().missing;
    if (miss.empty())
        return;
    _miss_i = py::mod(_miss_i + 1, (long long)miss.size());
    const auto [s, l] = miss[size_t(_miss_i)];
    sel_missing = PairLL{s, l};
    sel_uid.reset();
    Q_EMIT selectRequested({l}, {s});
    if (const auto pt = _point_of(s, l))
        Q_EMIT zoomRequested(pt->first, pt->second);
}

void MapPanel::_remove_stale()
{
    const auto &stale = status().stale;
    if (stale.empty())
        return;
    QList<long long> uids;
    for (const auto &r : stale)
        uids << r.uid;
    _edit(u"Remove %1 stale coast rows"_s.arg(uids.size()), [uids](adjacency::AdjacencyFile &f) { f.del(uids); });
    Q_EMIT message(u"Removed %1 coast rows whose sea tile and province don't touch — Ctrl+Z undoes it."_s.arg(
        uids.size()));
}

// ------------------------------------------------------------------ meme river
std::vector<long long> MapPanel::river_tiles() const
{
    std::vector<long long> t = sea_sel;
    std::sort(t.begin(), t.end());
    return t;
}

std::pair<adjacency::RiverPairs, long long> MapPanel::river_proposals()
{
    const std::vector<long long> tiles = river_tiles();
    if (tiles.empty())
        return {{}, 0};
    const std::pair<std::vector<long long>, double> key{tiles, py::round_to(river_gap->value(), 2)};
    if (!_river_cache || _river_cache->first != key)
        _river_cache.emplace(key, geo.river_pairs(tiles, river_gap->value()));
    const adjacency::RiverPairs &found = _river_cache->second;
    const auto &idx = adj().pair_index();
    adjacency::RiverPairs nw;
    for (const auto &[k, v] : found)
        if (!idx.contains({std::min(k.first, k.second), std::max(k.first, k.second)}))
            nw.insert(k, v);
    return {nw, (long long)found.size() - (long long)nw.size()};
}

QString MapPanel::_river_default_name(const std::vector<long long> &tiles)
{
    const auto n = adjacency::river_name_of(adj(), geo, tiles);
    return n ? *n : u"Meme-River %1"_s.arg(adj().river_names());
}

void MapPanel::_river_changed(bool emit)
{
    const std::vector<long long> tiles = river_tiles();
    adjacency::AdjacencyFile &a = adj();
    const auto &nc = a.no_coast;
    const auto marks = a.river_tiles();
    const auto [nw, have] = river_proposals();
    const QString dflt = !tiles.empty() ? _river_default_name(tiles) : u"Meme-River %1"_s.arg(a.river_names());
    river_name->setPlaceholderText(dflt);
    const QString t = river_name->text().trimmed();
    const QString name = t.isEmpty() ? dflt : t;
    if (!tiles.empty()) {
        const QString ids = ids_text(tiles);
        std::vector<long long> marked;
        std::set<QString> nameset;
        for (long long x : tiles)
            if (const auto *m = marks.get(x)) {
                marked.push_back(x);
                nameset.insert(m->name);
            }
        const QStringList names(nameset.begin(), nameset.end());
        if (marked.size() == tiles.size())
            river_state->setText(u"<span style='color:%1'>✔</span> %2 %3 Meme river <b>%4</b>"_s.arg(
                theme::OK, ids, tiles.size() == 1 ? u"is"_s : u"are"_s, names.join(u" / "_s)));
        else if (!marked.empty())
            river_state->setText(u"%1: %2 of %3 are Meme-river tiles (%4)"_s.arg(ids)
                                     .arg(marked.size())
                                     .arg(tiles.size())
                                     .arg(names.join(u" / "_s)));
        else
            river_state->setText(u"%1: not a Meme river"_s.arg(ids));
        const bool renaming = names.size() == 1 && marked.size() == tiles.size() && name != names[0];
        mark_btn->setText(renaming ? u"Rename to “%1”"_s.arg(name) : u"Mark as Meme river"_s);
        mark_btn->setEnabled(marked.size() < tiles.size() || renaming);
        unmark_btn->setEnabled(!marked.empty());
        river_info->setText(u"Bank crossings across the selection: %1 found · %2 missing · %3 there"_s.arg(
                                nw.size() + have)
                                .arg(nw.size())
                                .arg(have));
        size_t on = 0;
        for (long long x : tiles)
            on += nc.contains(x) ? 1 : 0;
        if (on == tiles.size())
            nc_state->setText(u"<span style='color:%1'>✔</span> %2 %3 no-coast"_s.arg(
                theme::OK, ids, tiles.size() == 1 ? u"is"_s : u"are all"_s));
        else if (on)
            nc_state->setText(u"%1: %2 of %3 are no-coast"_s.arg(ids).arg(on).arg(tiles.size()));
        else if (marked.size() == tiles.size())
            nc_state->setText(u"%1: not no-coast — but Meme-river shores are never checked anyway"_s.arg(ids));
        else
            nc_state->setText(u"%1: not no-coast (its shores are checked for coast crossings)"_s.arg(ids));
        nc_on_btn->setEnabled(on < tiles.size());
        nc_off_btn->setEnabled(on != 0);
    } else {
        river_state->setText(u"No sea tile selected."_s);
        river_info->setText(QString());
        mark_btn->setText(u"Mark as Meme river"_s);
        mark_btn->setEnabled(false);
        unmark_btn->setEnabled(false);
        nc_state->setText(u"No sea tile selected · %1 sea tiles are no-coast"_s.arg(nc.size()));
        nc_on_btn->setEnabled(false);
        nc_off_btn->setEnabled(false);
    }
    river_btn->setText(!nw.isEmpty() ? u"Create %1 bank crossings"_s.arg(nw.size()) : u"Create bank crossings"_s);
    river_btn->setEnabled(!nw.isEmpty());
    if (emit)
        Q_EMIT overlayChanged();
}

void MapPanel::_mark_river()
{
    const std::vector<long long> tiles = river_tiles();
    if (tiles.empty())
        return;
    const QString t = river_name->text().trimmed();
    const QString name = t.isEmpty() ? river_name->placeholderText() : t;
    const auto marks = adj().river_tiles();
    std::set<QString> olds;
    bool all = true;
    for (long long x : tiles) {
        if (const auto *m = marks.get(x))
            olds.insert(m->name);
        else
            all = false;
    }
    if (olds.size() == 1 && all && !olds.count(name)) {
        const QString old = *olds.begin();
        _edit(u"Rename Meme river %1 → %2"_s.arg(old, name),
              [&](adjacency::AdjacencyFile &f) { f.rename_mark(old, name); });
    } else {
        _edit(u"Meme river %1: %2"_s.arg(name, hash_ids(tiles)),
              [&](adjacency::AdjacencyFile &f) { f.set_mark(name, qlist(tiles)); });
    }
    river_name->clear();
}

void MapPanel::_unmark_river()
{
    const std::vector<long long> tiles = river_tiles();
    _edit(u"Unmark Meme river %1"_s.arg(hash_ids(tiles)), [&](adjacency::AdjacencyFile &f) { f.unmark(qlist(tiles)); });
}

void MapPanel::_set_no_coast(bool on)
{
    const std::vector<long long> tiles = river_tiles();
    if (tiles.empty())
        return;
    _edit(u"%1 %2"_s.arg(on ? u"Mark as no-coast:"_s : u"Unmark no-coast:"_s, hash_ids(tiles)),
          [&](adjacency::AdjacencyFile &f) { f.set_no_coast(qlist(tiles), on); });
}

void MapPanel::_find_rivers()
{
    const auto found = adjacency::detect_rivers(adj(), geo);
    if (found.empty())
        return;
    _edit(u"Mark %1 Meme rivers"_s.arg(found.size()), [&](adjacency::AdjacencyFile &f) {
        for (const auto &d : found)
            f.set_mark(d.name, qlist(d.tiles));
    });
    QStringList names;
    for (size_t i = 0; i < std::min<size_t>(6, found.size()); ++i)
        names << found[i].name;
    Q_EMIT message(u"Marked %1 Meme rivers (%2) — each gets a “# Meme river: …” line on Apply, Ctrl+Z undoes it."_s.arg(
                       found.size())
                       .arg(names.join(u", "_s) + (found.size() > 6 ? u" …"_s : QString())));
}

void MapPanel::_make_river()
{
    const auto [nw, _have] = river_proposals();
    if (nw.isEmpty())
        return;
    const std::vector<long long> tiles = river_tiles();
    const QString t = river_name->text().trimmed();
    const QString name = t.isEmpty() ? river_name->placeholderText() : t;
    std::vector<PairLL> keys;
    for (const auto &[k, v] : nw)
        keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    std::vector<adjacency::RowFields> specs;
    for (const auto &[x, y] : keys)
        specs.push_back({x, y, u"sea"_s, 0, 0, name});
    _edit(u"%1: %2 bank crossings"_s.arg(name).arg(specs.size()), [&](adjacency::AdjacencyFile &f) {
        const auto marks = f.river_tiles();
        if (std::any_of(tiles.begin(), tiles.end(), [&](long long x) {
                const auto *m = marks.get(x);
                return !m || m->name != name;
            }))
            f.set_mark(name, qlist(tiles));
        f.add(specs);
    });
}

void MapPanel::_add_all_river_missing()
{
    const auto &rs = river_stat();
    std::vector<adjacency::RowFields> specs;
    long long rivers = 0;
    for (const auto &[name, v] : rs) {
        for (const auto &[x, y] : v.missing)
            specs.push_back({x, y, u"sea"_s, 0, 0, name});
        rivers += v.missing.empty() ? 0 : 1;
    }
    if (specs.empty())
        return;
    _edit(u"Add %1 Meme-river bank crossings"_s.arg(specs.size()), [&](adjacency::AdjacencyFile &f) { f.add(specs); });
    Q_EMIT message(u"Added %1 bank crossings to %2 Meme rivers — Ctrl+Z undoes it."_s.arg(specs.size()).arg(rivers));
}

// ------------------------------------------------------------------ overlay
std::optional<MapPanel::Ends> MapPanel::ends(const adjacency::Row &r)
{
    // Where a row's line starts and ends: {"a": (x, y), "b": (x, y)} or None.
    //
    // Lines start at the crossing point (the shore for coast crossings, the closest points of the two provinces
    // otherwise) and reach part of the way into each province, so crossings that share a province end at different
    // spots.
    adjacency::AdjacencyFile &a = adj();
    const QString cat = a.category(r);
    if (cat == u"coast") {
        const bool a_sea = a.sea.contains(r.a);
        const long long s = a_sea ? r.a : r.b, l = a_sea ? r.b : r.a;
        const auto e = coast_ends(s, l);
        if (!e)
            return std::nullopt;
        return r.a == s ? Ends{e->first, e->second} : Ends{e->second, e->first};
    }
    if (cat == u"canal" && r.through) {
        const auto ti = geo.inner(r.through);
        if (!ti)
            return std::nullopt;
        const auto pa = geo.nearest(r.a, ti->first, ti->second), pb = geo.nearest(r.b, ti->first, ti->second);
        if (pa && pb)
            return Ends{*pa, *pb};
        return std::nullopt;
    }
    const auto e = pair_ends(r.a, r.b);
    if (e)
        return Ends{e->first, e->second};
    return std::nullopt;
}

std::optional<std::pair<Pt, Pt>> MapPanel::coast_ends(long long s, long long l)
{
    // (sea end, land end) of a coast crossing line.
    const auto il = geo.inner(l);
    if (!il)
        return std::nullopt;
    const auto ps = geo.nearest(s, il->first, il->second);
    if (!ps)
        return std::nullopt;
    return std::pair{*ps, lerp(*ps, *il, INTO, double(geo.md.w))};
}

std::optional<std::pair<Pt, Pt>> MapPanel::pair_ends(long long a, long long b)
{
    const auto an = geo.anchors(a, b);
    const auto ia = geo.inner(a), ib = geo.inner(b);
    if (!an || !ia || !ib)
        return std::nullopt;
    const double w = double(geo.md.w);
    return std::pair{lerp(an->first, *ia, INTO, w), lerp(an->second, *ib, INTO, w)};
}

std::vector<mapview::Shape> MapPanel::shapes()
{
    if (!doc)
        return {};
    adjacency::AdjacencyFile &a = adj();
    const bool edit = edit_btn->isChecked();
    std::set<long long> ids(sel.begin(), sel.end());
    ids.insert(sea_sel.begin(), sea_sel.end());
    const bool only = only_sel->isChecked() && !ids.empty();
    std::vector<mapview::Shape> lines, marks, handles;
    const adjacency::CoastStatus &st = status();
    std::set<long long> stale_ids;
    for (const auto &r : st.stale)
        stale_ids.insert(r.uid);
    static const std::map<QString, double> widths = {{u"strait"_s, 5},     {u"river"_s, 4.5}, {u"coast"_s, 4},
                                                     {u"canal"_s, 5},      {u"other"_s, 4},   {u"impassable"_s, 4},
                                                     {u"land"_s, 5}};
    auto shown = [&](const QString &k) {
        const bool *v = show_.get(k);
        return v ? *v : true;
    };
    const auto rows = a.rows;                        // (the shapes are built from the rows as they are now)
    for (const adjacency::Row &r : rows) {
        if (only && !(ids.count(r.a) || ids.count(r.b) || ids.count(r.through)))
            continue;
        const QString cat = a.category(r);
        const bool is_sel = sel_uid && r.uid == *sel_uid;
        const bool stale = stale_ids.count(r.uid) > 0;
        if (!(stale ? shown(u"stale"_s) : shown(cat)) && !is_sel)
            continue;
        const QVariantList tag{u"row"_s, r.uid};
        const adjacency::Rgb col = stale ? adjacency::STALE_COLOR : *adjacency::CAT_COLOR.get(cat);
        if (cat == u"impassable" && geo.adjacent(r.a, r.b)) {
            if (const auto bp = geo.border_point(r.a, r.b)) {
                mapview::Shape m = shape(u"x"_s, {*bp}, col);
                m.size = 9;
                m.width = 4.5;
                m.tag = QVariantList{u"x"_s, r.uid};
                m.halo = is_sel;
                marks.push_back(m);
            }
            continue;
        }
        const auto e = ends(r);
        if (!e)
            continue;
        if (cat == u"canal" && r.through) {
            const Pt ti = *geo.inner(r.through);
            mapview::Shape ln = shape(u"line"_s, {e->a, ti, e->b}, col);
            ln.width = widths.at(cat);
            ln.tag = tag;
            ln.halo = is_sel;
            lines.push_back(ln);
            mapview::Shape m = shape(u"square"_s, {ti}, col);
            m.size = 6;
            m.tag = tag;
            m.halo = is_sel;
            marks.push_back(m);
        } else {
            mapview::Shape ln = shape(u"line"_s, {e->a, e->b}, col);
            const auto wi = widths.find(cat);
            ln.width = wi != widths.end() ? wi->second : 2.0;
            ln.dash = stale || cat == u"impassable";
            ln.tag = tag;
            ln.halo = is_sel;
            lines.push_back(ln);
            if (cat == u"coast" && !edit) {
                mapview::Shape m = shape(u"dot"_s, {a.sea.contains(r.a) ? e->b : e->a}, col);
                m.size = 4;
                m.tag = tag;
                marks.push_back(m);
            }
        }
        if (r.through && cat != u"canal") {
            const Pt mid = lerp(e->a, e->b, 0.5, double(geo.md.w));
            if (const auto tp = geo.nearest(r.through, mid.first, mid.second)) {
                mapview::Shape ln = shape(u"line"_s, {mid, *tp}, col);
                ln.width = 1.5;
                ln.dash = true;
                lines.push_back(ln);
                mapview::Shape m = shape(u"diamond"_s, {*tp}, col);
                m.size = 5.5;
                m.tag = tag;
                m.halo = is_sel;
                marks.push_back(m);
            }
        }
        if (edit) {
            for (const QString &which : {u"a"_s, u"b"_s}) {
                mapview::Shape hd = shape(u"dot"_s, {e->at(which)}, col);
                hd.size = 7;
                hd.tag = QVariantList{u"end"_s, r.uid, which};
                hd.halo = is_sel;
                handles.push_back(hd);
            }
        }
    }
    if (shown(u"missing"_s) || sel_missing) {
        for (const auto &p : st.missing) {
            const long long s = p.first, l = p.second;
            if ((only && !(ids.count(s) || ids.count(l))) ||
                (!shown(u"missing"_s) && !(sel_missing && *sel_missing == PairLL{s, l})))
                continue;
            if (const auto ce = coast_ends(s, l)) {
                mapview::Shape ln = shape(u"line"_s, {ce->first, ce->second}, adjacency::MISSING_COLOR);
                ln.width = 4.5;
                ln.tag = pair_tag(u"missingline"_s, s, l);
                ln.halo = sel_missing && *sel_missing == PairLL{s, l};
                lines.push_back(ln);
                mapview::Shape m = shape(u"dot"_s, {ce->first}, adjacency::MISSING_COLOR);
                m.size = 6;
                m.tag = pair_tag(u"missing"_s, s, l);
                marks.push_back(m);
            }
        }
    }
    _rivermiss.clear();
    if (shown(u"rivermiss"_s)) {
        for (const auto &[name, v] : river_stat()) {
            for (const auto &[x, y] : v.missing) {
                if (only && !(ids.count(x) || ids.count(y)))
                    continue;
                if (const auto pe = pair_ends(x, y)) {
                    _rivermiss[{x, y}] = name;
                    mapview::Shape ln = shape(u"line"_s, {pe->first, pe->second}, adjacency::MISSING_COLOR);
                    ln.width = 4;
                    ln.dash = true;
                    ln.tag = pair_tag(u"rivermiss"_s, x, y);
                    lines.push_back(ln);
                }
            }
        }
    }
    const auto [nw, _have] = river_proposals();
    for (const auto &[k, v] : nw) {
        if (_rivermiss.count(k))
            continue;
        if (const auto pe = pair_ends(k.first, k.second)) {
            mapview::Shape ln = shape(u"line"_s, {pe->first, pe->second}, adjacency::PREVIEW_COLOR);
            ln.width = 4;
            ln.dash = true;
            ln.tag = pair_tag(u"preview"_s, k.first, k.second);
            lines.push_back(ln);
        }
    }
    // a crossing over the map's left / right edge: drawn around it (two pieces), not across the whole map
    const double w = double(geo.md.w);
    std::vector<mapview::Shape> cut;
    for (const mapview::Shape &ln : lines) {
        std::vector<Pt> pts;
        for (const QPointF &q : ln.pts)
            pts.emplace_back(q.x(), q.y());
        const auto parts = seam_split(pts, w);
        if (parts.size() == 1) {
            cut.push_back(ln);
        } else {
            for (const auto &part : parts)
                if (part.size() > 1) {
                    mapview::Shape c = ln;
                    c.pts.clear();
                    for (const Pt &q : part)
                        c.pts.emplace_back(q.first, q.second);
                    cut.push_back(c);
                }
        }
    }
    cut.insert(cut.end(), marks.begin(), marks.end());
    cut.insert(cut.end(), handles.begin(), handles.end());
    return cut;
}

QString MapPanel::describe(const QVariantList &tag)
{
    // Status-bar text for a hovered shape.
    const QString kind = tag.value(0).toString();
    const QVariant key = tag.value(1);
    if (kind == u"end")
        return u"Drag to move this end · right-click to delete — "_s + describe({u"row"_s, key});
    if (kind == u"x")
        return describe({u"row"_s, key}) + (edit_btn->isChecked() ? u"  · right-click to delete"_s : QString());
    if (kind == u"row") {
        const adjacency::AdjacencyFile &a = adj();
        const adjacency::Row *r = a.get(key.toLongLong());
        if (!r)
            return QString();
        QString s = u"%1: %2 → %3"_s.arg(KIND_NAMES.at(a.category(*r)), pname(r->a), pname(r->b));
        if (r->through)
            s += u" via %1"_s.arg(pname(r->through));
        if (r->type == u"canal")
            s += u" · canal %1"_s.arg(r->data);
        if (!r->comment.isEmpty())
            s += u" · %1"_s.arg(r->comment);
        if (const QStringList *ps = _probs.get(r->uid))
            s += u"  ⚠ "_s + ps->join(u"; "_s);
        return s;
    }
    if (kind == u"rivermiss") {
        const PairLL k = pair_of(key);
        const auto it = _rivermiss.find(k);
        return u"Missing bank crossing of %1: %2 ↔ %3%4"_s.arg(
            it != _rivermiss.end() ? it->second : u"Meme river"_s, pname(k.first), pname(k.second),
            edit_btn->isChecked() ? u"  · right-click to add it"_s : QString());
    }
    if (kind == u"missing" || kind == u"missingline") {
        const PairLL k = pair_of(key);
        return u"Missing coast crossing: %1 → %2%3"_s.arg(
            pname(k.first), pname(k.second),
            edit_btn->isChecked() ? u"  · right-click the red dot to add it"_s : QString());
    }
    if (kind == u"preview") {
        const PairLL k = pair_of(key);
        return u"New Meme-river crossing (not created yet): %1 ↔ %2"_s.arg(pname(k.first), pname(k.second));
    }
    return QString();
}

// MapEditHandler --------------------------------------------------------------------------------------------------
mapview::ShapeLayer *MapEditHandler::layer() const
{
    return p->view->shape_layer;
}

long long MapEditHandler::_pid(double x, double y) const
{
    return p->geo.md.province_at(double((long long)x), double((long long)y));
}

QString MapEditHandler::_name(long long pid) const
{
    return p->pname(pid);
}

bool MapEditHandler::cancel()
{
    const bool had = drag.has_value() || start.has_value();
    drag.reset();
    start.reset();
    if (p->view)
        layer()->set_temp({});
    return had;
}

// --- mouse
bool MapEditHandler::press(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (start) {
        _finish(x, y);
        return true;
    }
    const auto tag = layer()->hit(x, y, tol * 1.3, QStringList{u"end"_s});
    if (!tag)
        return false;
    const long long uid = tag->value(1).toLongLong();
    const QString which = tag->value(2).toString();
    const adjacency::Row *r = p->adj().get(uid);
    const auto e = r ? p->ends(*r) : std::nullopt;
    if (!e)
        return false;
    drag = Drag{uid, which, e->at(which == u"a" ? u"b"_s : u"a"_s), x, y, false};
    return true;
}

void MapEditHandler::move(double x, double y)
{
    const adjacency::Rgb col{255, 255, 255};
    if (drag) {
        drag->moved = drag->moved || std::abs(x - drag->sx) + std::abs(y - drag->sy) > 1.5;
        const long long pid = _pid(x, y);
        mapview::Shape ln = shape(u"line"_s, {drag->fixed, {x, y}}, col);
        ln.width = 3.5;
        ln.dash = true;
        mapview::Shape dt = shape(u"dot"_s, {{x, y}}, col);
        dt.size = 7;
        layer()->set_temp({ln, dt});
        Q_EMIT p->message(p->mod.provinces.contains(pid) ? u"Drop on %1"_s.arg(_name(pid)) : u"Drop on a province"_s);
    } else if (start) {
        if (const auto a = p->geo.inner(*start)) {
            mapview::Shape ln = shape(u"line"_s, {*a, {x, y}}, col);
            ln.width = 3.5;
            ln.dash = true;
            mapview::Shape dt = shape(u"dot"_s, {*a}, col);
            dt.size = 7;
            layer()->set_temp({ln, dt});
        }
    }
}

void MapEditHandler::release(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (!drag)                                       // the click that finished a new crossing
        return;
    const Drag d = *drag;
    const bool moved = d.moved || std::abs(x - d.sx) + std::abs(y - d.sy) > std::max(1.5, tol * 0.3);
    drag.reset();
    layer()->set_temp({});
    const adjacency::Row *rp = p->adj().get(d.uid);
    if (!rp)
        return;
    const adjacency::Row r = *rp;
    if (!moved) {
        p->map_clicked({u"row"_s, d.uid});
        return;
    }
    const long long pid = _pid(x, y);
    const long long other = d.which == u"a" ? r.b : r.a;
    const long long mine = d.which == u"a" ? r.a : r.b;
    if (!p->mod.provinces.contains(pid) || pid == other || pid == mine) {
        Q_EMIT p->message(u"Not moved (drop it on a different province)"_s);
        return;
    }
    p->sel_uid = d.uid;
    p->_edit(u"Move crossing end to #%1"_s.arg(pid), [&](adjacency::AdjacencyFile &f) {
        f.update(d.uid, [&](adjacency::Row &x) { (d.which == u"a" ? x.a : x.b) = pid; });
    });
    Q_EMIT p->message(u"Crossing now %1 → %2. Ctrl+Z to undo."_s.arg(_name(d.which == u"a" ? pid : r.a),
                                                                       _name(d.which == u"b" ? pid : r.b)));
}

bool MapEditHandler::rclick(double x, double y, double tol, Qt::KeyboardModifiers)
{
    if (start) {                                     // the second end must be a left click: right click cancels
        cancel();
        Q_EMIT p->message(u"New crossing cancelled"_s);
        return true;
    }
    const auto tag = layer()->hit(x, y, tol, QStringList{u"end"_s, u"x"_s, u"missing"_s, u"rivermiss"_s});
    if (tag) {
        const QString kind = tag->value(0).toString();
        if (kind == u"rivermiss") {
            const PairLL k = pair_of(tag->value(1));
            const auto it = p->_rivermiss.find(k);
            const QString name = it != p->_rivermiss.end() ? it->second : QString();
            p->_edit(u"Add %1 crossing %2 ↔ %3"_s.arg(name).arg(k.first).arg(k.second), [&](adjacency::AdjacencyFile &f) {
                f.add({{k.first, k.second, u"sea"_s, 0, 0, name}});
            });
            Q_EMIT p->message(u"Added %1 crossing %2 ↔ %3"_s.arg(name, _name(k.first), _name(k.second)));
        } else if (kind == u"missing") {
            const PairLL k = pair_of(tag->value(1));
            const QString com = p->adj().coast_comment();
            p->_edit(u"Add coast crossing %1 → %2"_s.arg(k.first).arg(k.second), [&](adjacency::AdjacencyFile &f) {
                f.add({{k.first, k.second, u"sea"_s, 0, 0, com}});
            });
            Q_EMIT p->message(u"Added coast crossing %1 → %2"_s.arg(_name(k.first), _name(k.second)));
        } else {
            const long long uid = tag->value(1).toLongLong();
            if (const adjacency::Row *rp = p->adj().get(uid)) {
                const adjacency::Row r = *rp;
                if (p->sel_uid && *p->sel_uid == uid)
                    p->sel_uid.reset();
                p->_edit(u"Delete crossing %1 → %2"_s.arg(r.a).arg(r.b),
                         [uid](adjacency::AdjacencyFile &f) { f.del({uid}); });
                Q_EMIT p->message(u"Deleted %1 → %2 (%3). Ctrl+Z to undo."_s.arg(
                    _name(r.a), _name(r.b), r.comment.isEmpty() ? r.type : r.comment));
            }
        }
        return true;
    }
    const long long pid = _pid(x, y);
    if (p->mod.provinces.contains(pid)) {
        start = pid;
        move(x, y);
        Q_EMIT p->message(u"New crossing from %1 — left-click the other province · right-click or Esc "
                          "cancels"_s.arg(_name(pid)));
        return true;
    }
    return false;
}

void MapEditHandler::_finish(double x, double y)
{
    const long long a = *start;
    start.reset();
    layer()->set_temp({});
    const long long b = _pid(x, y);
    if (!p->mod.provinces.contains(b) || b == a) {
        Q_EMIT p->message(u"New crossing cancelled"_s);
        return;
    }
    const adjacency::RowFields spec = guess_spec(p->adj(), p->geo, p->mod, a, b);
    std::optional<long long> uid;
    p->_edit(u"Add crossing %1 → %2"_s.arg(spec.a).arg(spec.b),
             [&](adjacency::AdjacencyFile &f) { uid = f.add({spec})[0]; });
    p->select_row(uid);
    Q_EMIT p->message(u"Added %1 → %2 (%3%4). Change it in the panel if needed; Ctrl+Z to undo."_s.arg(
        _name(spec.a), _name(spec.b), spec.type, !spec.comment.isEmpty() ? u", "_s + spec.comment : QString()));
}

}  // namespace adjpanel
