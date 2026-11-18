// checkpanel.py — see checkpanel.h.
#include "ui/buildtypes.h"
#include "ui/checkpanel.h"
#include "core/cbcheck.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/entryview_data.h"
#include "core/eventtools.h"
#include "core/history.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/oob.h"
#include "core/positions.h"
#include "core/provrenumber.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "core/staterefs.h"
#include "map/autofix.h"
#include "map/mapsize.h"
#include "map/moreviews_data.h"
#include "map/pospanel_data.h"
#include "map/provdelete.h"
#include "ui/diplopanel.h"
#include "ui/drawpanel.h"
#include "ui/pospanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace checkpanel {

using mapcheck::SEV_ERROR;
using mapcheck::SEV_INFO;
using mapcheck::SEV_WARN;
using mapcheck::Val;
using mapcheck::XY;

// pixel problems: a close look and a ring around the spot (radius), not a cross over it
const std::map<QString, double> PIXEL_ZOOM = {{u"rivers"_s, 14.0}, {u"landsea_terrain"_s, 8.0}, {u"landsea_rivers"_s, 8.0}};
const std::map<QString, double> PIXEL_RING = {{u"rivers"_s, 4.5}, {u"landsea_terrain"_s, 7.0}, {u"landsea_rivers"_s, 7.0}};
const std::map<QString, Rgb> RING_RGB = {{u"landsea_terrain"_s, {255, 255, 255}}};   // white around yellow, else yellow

// (C++ only) the kind that stands for points inside positions.txt blocks the editor keeps as text
static const QString EXTRA_KIND = u"(other blocks)"_s;

// problems that are pixels (shown as pixels on the error map, found by the pixel under the mouse)
const QSet<QString> PIXEL_KEYS = {u"rivers"_s, u"landsea_terrain"_s, u"landsea_rivers"_s, u"stray"_s, u"unknown"_s,
                                  u"parts"_s, u"zero_small"_s};
// the error map (map mode "Errors"): its layers and their colours — each one distinct from the others
const std::vector<Layer> LAYERS = {{u"prov"_s, u"Provinces"_s, {185, 50, 50}},
                                   {u"river"_s, u"River pixels"_s, {255, 0, 0}},
                                   {u"terrain"_s, u"terrain.bmp land/sea"_s, {255, 255, 0}},
                                   {u"rivers_ls"_s, u"rivers.bmp land/sea"_s, {170, 95, 255}},
                                   {u"stray"_s, u"Stray pixel bits"_s, {0, 235, 255}},
                                   {u"unknown"_s, u"Unknown colours"_s, {255, 255, 255}},
                                   {u"adj"_s, u"Adjacency lines"_s, {255, 70, 200}},
                                   {u"markers"_s, u"Markers ✕ / rings"_s, {255, 230, 70}}};
const std::map<QString, QString> LAYER_TIPS = {
    {u"prov"_s, u"Provinces with an error (red) or a warning (brown-orange) of their own: positions, naval bases, "
                u"states, climate / continent, history, …"_s},
    {u"river"_s, u"rivers.bmp pixels that break a rule (2 px wide, diagonal only, on water, misplaced source / merge) "
                 u"— red"_s},
    {u"terrain"_s, u"terrain.bmp pixels whose land / sea doesn't match their province — yellow"_s},
    {u"rivers_ls"_s, u"rivers.bmp pixels whose land / sea doesn't match their province — violet"_s},
    {u"stray"_s, u"Small separate bits of a province — cyan"_s},
    {u"unknown"_s, u"Pixels of a colour no province uses — white"_s},
    {u"adj"_s, u"Adjacency problems: pink = missing crossings, dashed orange = wrong rows (stale, duplicate, …)"_s},
    {u"markers"_s, u"✕ / rings around the smaller problems, so they're easy to find when zoomed out"_s},
};
const Rgb ERR_FILL{185, 50, 50}, WARN_FILL{165, 115, 40};
const Rgb LAND_FILL{80, 85, 94}, SEA_FILL{27, 33, 45};

static Rgb layer_rgb(const QString &key)
{
    for (const Layer &l : LAYERS)
        if (l.key == key)
            return l.rgb;
    return {0, 0, 0};
}

Rgb sev_color(const QString &sev)
{
    if (sev == SEV_ERROR)
        return {255, 80, 80};
    if (sev == SEV_WARN)
        return {240, 170, 60};
    return {150, 160, 175};
}

QString sev_name(const QString &sev)
{
    return sev == SEV_ERROR ? u"error"_s : sev == SEV_WARN ? u"warning"_s : u"note"_s;
}

namespace {

// an item's data, read the way the Python reads its tuples
bool is_list(const Val &v) { return std::holds_alternative<std::vector<Val>>(v.v); }
size_t len(const Val &v) { return is_list(v) ? v.list().size() : 0; }
QString S(const Val &v)
{
    if (const auto *s = std::get_if<QString>(&v.v))
        return *s;
    return {};
}
mapcheck::OptStr OS(const Val &v)
{
    if (const auto *s = std::get_if<QString>(&v.v))
        return *s;
    return std::nullopt;
}
long long I(const Val &v)
{
    if (const auto *i = std::get_if<long long>(&v.v))
        return *i;
    if (const auto *d = std::get_if<double>(&v.v))
        return static_cast<long long>(*d);
    if (const auto *b = std::get_if<bool>(&v.v))
        return *b;
    return 0;
}
double D(const Val &v)
{
    if (const auto *d = std::get_if<double>(&v.v))
        return *d;
    return static_cast<double>(I(v));
}
bool head_is(const Val &v, const QString &s) { return len(v) > 0 && S(v[0]) == s; }
QString pl(long long n) { return n == 1 ? u""_s : u"s"_s; }
QString esc(const QString &s) { return s.toHtmlEscaped().replace(u'\'', u"&#x27;"_s); }   // html.escape
QString hexrgb(const Rgb &c)
{
    return u"#%1%2%3"_s.arg(std::get<0>(c), 2, 16, QChar(u'0')).arg(std::get<1>(c), 2, 16, QChar(u'0'))
        .arg(std::get<2>(c), 2, 16, QChar(u'0'));
}
QString last_part(const QString &rel) { return rel.mid(rel.lastIndexOf(u'/') + 1); }   // r.rsplit("/", 1)[-1]

using Pt = std::pair<double, double>;
const double INTO = 0.6;   // adjpanel: how far a line end reaches from the crossing point towards the province centre

Pt lerp(const Pt &p, const Pt &q, double f, long long w)
{
    // the point f of the way from p to q — around the map's left / right edge when that is the shorter way
    double qx = q.first;
    if (w && std::abs(qx - p.first) > w / 2.0)
        qx += qx < p.first ? w : -w;
    const double x = p.first + (qx - p.first) * f;
    return {w ? py::mod(x, double(w)) : x, p.second + (q.second - p.second) * f};
}

// adjpanel's line ends (the Adjacencies panel isn't ported: its geometry is the main window's)
std::optional<std::pair<Pt, Pt>> coast_ends(adjacency::AdjGeometry &geo, long long s, long long l)
{
    const auto il = geo.inner(l);
    if (!il)
        return std::nullopt;
    const auto ps = geo.nearest(s, il->first, il->second);
    if (!ps)
        return std::nullopt;
    return std::make_pair(*ps, lerp(*ps, *il, INTO, geo.md.w));
}
std::optional<std::pair<Pt, Pt>> pair_ends(adjacency::AdjGeometry &geo, long long a, long long b)
{
    const auto an = geo.anchors(a, b);
    const auto ia = geo.inner(a), ib = geo.inner(b);
    if (!an || !ia || !ib)
        return std::nullopt;
    return std::make_pair(lerp(an->first, *ia, INTO, geo.md.w), lerp(an->second, *ib, INTO, geo.md.w));
}
std::optional<std::pair<Pt, Pt>> ends(adjacency::AdjGeometry &geo, adjacency::AdjacencyFile &adj,
                                      const adjacency::Row &r)
{
    const QString cat = adj.category(r);
    if (cat == u"coast") {
        const bool a_sea = adj.sea.contains(r.a);
        const long long s = a_sea ? r.a : r.b, l = a_sea ? r.b : r.a;
        const auto e = coast_ends(geo, s, l);
        if (!e)
            return std::nullopt;
        return r.a == s ? *e : std::make_pair(e->second, e->first);
    }
    if (cat == u"canal" && r.through) {
        const auto ti = geo.inner(r.through);
        if (!ti)
            return std::nullopt;
        const auto pa = geo.nearest(r.a, ti->first, ti->second), pb = geo.nearest(r.b, ti->first, ti->second);
        if (pa && pb)
            return std::make_pair(*pa, *pb);
        return std::nullopt;
    }
    return pair_ends(geo, r.a, r.b);
}

QVariantList tag_of(int ci, int ii) { return {u"chk"_s, ci, ii}; }

mapview::Shape shape(const QString &kind, std::vector<QPointF> pts, const Rgb &color, const QVariantList &tag)
{
    mapview::Shape s;
    s.kind = kind;
    s.pts = std::move(pts);
    s.color = color;
    s.tag = tag;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- _WrapDelegate
_WrapDelegate::_WrapDelegate(QTreeWidget *tree_) : QStyledItemDelegate(tree_), tree(tree_) {}

void _WrapDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    option->features |= QStyleOptionViewItem::WrapText;
}

QSize _WrapDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QSize base = QStyledItemDelegate::sizeHint(option, index);
    int depth = 0;
    for (QModelIndex p = index.parent(); p.isValid(); p = p.parent())
        depth += 1;
    const int w = tree->viewport()->width() - tree->indentation() * (depth + 1) - 26;
    const QString text = index.data(Qt::DisplayRole).toString();
    const QRect r = option.fontMetrics.boundingRect(QRect(0, 0, std::max(60, w), 100000), Qt::TextWordWrap, text);
    return {base.width(), std::max(base.height(), r.height() + 8)};
}

// ---------------------------------------------------------------------------------------------- ErrorView
mapview::ColorTable ErrorView::table()
{
    // the colour table for the map: the province fill (drawn the fast way), the problem pixels on top
    mapview::ColorTable t;
    t.rgb = p->fill_lut;
    t.on_top = [this](long long y0, long long y1, long long x0, long long x1) { return on_top(y0, y1, x0, x1); };
    return t;
}

std::vector<long long> ErrorView::_sig() const
{
    document::Document *doc = p->doc;
    auto *tb = doc && doc->maps.contains(u"terrain_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s) : nullptr;
    auto *rb = doc && doc->maps.contains(u"rivers_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s) : nullptr;
    std::vector<long long> s = {reinterpret_cast<long long>(doc), p->fill_version, p->ocean ? 1LL : 0LL};
    for (const auto &[k, on] : p->show)
        s.push_back(on);
    for (auto *b : {tb, rb}) {
        s.push_back(b ? b->version : -1);
        s.push_back(b ? b->stamp : -1);
        s.push_back(b && b->data ? 1 : 0);
    }
    s.push_back(p->md.version);
    return s;
}

std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> ErrorView::_layers(long long Y0, long long Y1, long long X0,
                                                                          long long X1)
{
    const mapview::MapData &md = p->md;
    document::Document *doc = p->doc;
    std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> out;
    if (!doc)
        return out;
    const long long h = Y1 - Y0, w = X1 - X0;
    auto cut = [&](const grid::Grid<std::uint8_t> &m) {          // m[Y0:Y1, X0:X1] of a whole-map mask
        grid::Grid<std::uint8_t> o(h, w);
        for (long long y = 0; y < h; ++y)
            std::copy_n(m.row(Y0 + y) + X0, w, o.row(y));
        return o;
    };
    p->ensure_masks();
    if (p->show[u"stray"_s] && p->stray_mask)
        out.emplace_back(cut(*p->stray_mask), layer_rgb(u"stray"_s));
    if (p->show[u"unknown"_s] && p->unknown_mask)
        out.emplace_back(cut(*p->unknown_mask), layer_rgb(u"unknown"_s));
    auto *tb = doc->maps.contains(u"terrain_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s) : nullptr;
    if (p->show[u"terrain"_s] && tb && tb->data && p->ocean) {
        grid::Grid<std::uint8_t> m(h, w);
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x) {
                const long long id = md.idmap(Y0 + y, X0 + x);
                m(y, x) = id != 0 && bool(md.sea[size_t(id)]) != bool((*p->ocean)[(*tb->data)(Y0 + y, X0 + x)]);
            }
        out.emplace_back(std::move(m), layer_rgb(u"terrain"_s));
    }
    auto *rb = doc->maps.contains(u"rivers_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s) : nullptr;
    if (rb && rb->data && (p->show[u"river"_s] || p->show[u"rivers_ls"_s])) {
        const bool whole = Y0 == 0 && Y1 == md.h && X0 == 0 && X1 == md.w;
        mapcheck::Masks own;
        const mapcheck::Masks &m = whole ? mapcheck::river_masks_whole(md, *rb)
                                         : (own = mapcheck::river_problem_masks(md, *rb->data, Y0, Y1, X0, X1));
        if (p->show[u"rivers_ls"_s])
            out.emplace_back(*m.get(u"landsea"_s), layer_rgb(u"rivers_ls"_s));
        if (p->show[u"river"_s]) {
            grid::Grid<std::uint8_t> bad(h, w);
            for (const auto &[k, v] : m)
                if (k != u"landsea") {
                    auto &bf = bad.flat();
                    const auto &vf = v.flat();
                    for (size_t i = 0; i < bf.size(); ++i)
                        bf[i] |= vf[i];
                }
            out.emplace_back(std::move(bad), layer_rgb(u"river"_s));
        }
    }
    return out;
}

std::optional<std::vector<mapview::PixelLayer::Top>> ErrorView::on_top(long long y0, long long y1, long long x0,
                                                                       long long x1)
{
    // the problem pixels over the shaded map, each kind in its colour — no dark edge around them (the user,
    // 2026-10-16: it hid what is next to them, the colour to fill in with). The whole map's layers are kept until
    // the checks, the layer toggles or a bitmap change.
    const mapview::MapData &md = p->md;
    const bool full = y0 == 0 && y1 == md.h && x0 == 0 && x1 == md.w;
    std::vector<long long> sig;
    if (full) {
        sig = _sig();
        if (_full && _full->first == sig)
            return _full->second;
    }
    const long long Y0 = std::max(0LL, y0 - 1), X0 = std::max(0LL, x0 - 1);
    const long long Y1 = std::min(md.h, y1 + 1), X1 = std::min(md.w, x1 + 1);
    const auto layers = _layers(Y0, Y1, X0, X1);
    std::vector<mapview::PixelLayer::Top> got;
    const long long ww = x1 - x0, LW = X1 - X0;
    for (const auto &[m, c] : layers) {
        mapview::PixelLayer::Top t;
        t.rgb = c;
        for (long long y = y0; y < y1; ++y) {
            const std::uint8_t *row = m.row(y - Y0) + (x0 - X0);
            for (long long x = 0; x < ww; ++x)
                if (row[x])
                    t.idx.push_back((y - y0) * ww + x);
        }
        (void)LW;
        if (!t.idx.empty())
            got.push_back(std::move(t));
    }
    std::optional<std::vector<mapview::PixelLayer::Top>> res;
    if (!got.empty())
        res = std::move(got);
    if (full)
        _full = std::make_pair(sig, res);
    return res;
}

// ---------------------------------------------------------------------------------------------- MapCheckPanel
MapCheckPanel::MapCheckPanel(moddata::Mod &mod_, mapview::MapData &md_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_)
{
    for (const Layer &l : LAYERS)
        show[l.key] = true;
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Errors & map check"_s, u"title"_s));
    s->lay->addWidget(widgets::label(
        u"Problems of your unsaved edits, and everything that doesn't line up between provinces.bmp, definition.csv, "
        u"default.map, the province history, region.txt, continent.txt / climate.txt and the terrain / river bitmaps. "
        u"Click a line to go there; double-click it to open the tab where you fix it (the map stays where it is). "
        u"Fixes are normal edits: Ctrl+Z undoes them and Apply writes them."_s,
        u"faint"_s));
    summary = widgets::label(u""_s, u"sub"_s);
    summary->setTextFormat(Qt::RichText);
    s->lay->addWidget(summary);
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"Check again"_s);
    connect(b, &QPushButton::clicked, this, [this] { refresh(doc, true); });
    row->addWidget(b);
    next_btn = new QPushButton(u"Go to next problem ▸"_s);
    connect(next_btn, &QPushButton::clicked, this, [this] { _next_problem(); });
    row->addWidget(next_btn, 1);
    s->lay->addLayout(row);
    lay->addWidget(s);

    // the selected problem and its fixes — ABOVE the list, so the buttons are always in view
    s = fix_section = new widgets::Section();
    sel_title = widgets::label(u""_s, u"sub"_s);
    sel_title->setTextFormat(Qt::RichText);
    s->lay->addWidget(sel_title);
    note = widgets::label(u""_s, u"faint"_s);
    s->lay->addWidget(note);
    // "Fix this one" for the selected problem (then the next one is shown), "Fix all …" for the whole group
    fix_one_btn = new widgets::WrapButton(u"Fix this one"_s);
    fix_one_btn->setObjectName(u"primary"_s);
    fix_one_btn->setToolTip(u"Fix only the selected problem, then go on to the next one of this group"_s);
    connect(fix_one_btn, &QPushButton::clicked, this, [this] { _fix(true); });
    fix_one_btn->hide();
    s->lay->addWidget(fix_one_btn);
    fix_btn = new widgets::WrapButton(u"Fix"_s);
    fix_btn->setObjectName(u"primary"_s);
    connect(fix_btn, &QPushButton::clicked, this, [this] { _fix(false); });
    fix_btn->hide();
    s->lay->addWidget(fix_btn);
    nofix = widgets::label(u""_s, u"faint"_s);
    s->lay->addWidget(nofix);
    lay->addWidget(s);

    // the error map: what it shows
    s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Show on the map — map mode “Errors” shows everything at once; click anything on "
                                     u"it to select that problem"_s,
                                     u"faint"_s));
    auto *host = new QWidget();
    auto *flow = new widgets::GridFlow(host, 4, 190, 24);   // as many columns as the panel is wide
    for (const Layer &l : LAYERS) {
        auto *cb = new QCheckBox(l.text);
        cb->setIcon(widgets::swatch_icon(l.rgb, 11));
        cb->setChecked(true);
        cb->setToolTip(LAYER_TIPS.at(l.key));
        cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const QString k = l.key;
        connect(cb, &QCheckBox::toggled, this, [this, k](bool on) { _show_changed(k, on); });
        flow->addWidget(cb);
        show_boxes[l.key] = cb;
    }
    s->lay->addWidget(host);
    lay->addWidget(s);

    s = new widgets::Section();
    tree = new QTreeWidget();
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setMinimumHeight(420);
    tree->setWordWrap(true);
    tree->setUniformRowHeights(false);
    tree->setTextElideMode(Qt::ElideNone);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    tree->setItemDelegate(new _WrapDelegate(tree));
    // a new width: lay the (wrapped) lines out again
    connect(tree->header(), &QHeaderView::sectionResized, tree, [this] { tree->doItemsLayout(); });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] { _picked(); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) { _double(it); });
    s->lay->addWidget(tree, 1);
    lay->addWidget(s, 1);
    _show_fix();
}

MapCheckPanel::~MapCheckPanel() = default;

// ---------------------------------------------------------------------------------------------- checks
std::vector<std::uint8_t> MapCheckPanel::_valid_sea() const
{
    std::vector<std::uint8_t> v = md.sea;
    v[0] = 0;
    if (doc->maps.contains(u"adjacency"_s))
        for (long long t : doc->map<adjacency::AdjacencyFile>(u"adjacency"_s).skip_coast())
            if (0 < t && t < static_cast<long long>(v.size()))
                v[size_t(t)] = 0;
    return v;
}

void MapCheckPanel::refresh(document::Document *doc_, bool force)
{
    doc = doc_;
    if (!doc)
        return;
    // only check again when something changed (an edit / undo / redo / Apply) or when asked to
    const std::vector<long long> sig = {reinterpret_cast<long long>(doc), static_cast<long long>(doc->undo_stack.size()),
                                        static_cast<long long>(doc->redo_stack.size()),
                                        doc->undo_stack.empty() ? 0LL
                                                                : reinterpret_cast<long long>(doc->undo_stack.back().get()),
                                        doc->pending_count()};
    if (!force && _sig && *_sig == sig) {
        Q_EMIT overlayChanged();
        return;
    }
    _sig = sig;
    QElapsedTimer t0;
    t0.start();
    if (force) {
        geo_cache.key.reset();
        geo_cache.kept.clear();
    }
    cats = mapcheck::run_checks(mod, md, *doc, geo_cache, _valid_sea(), adj_geo, 2.3);
    for (Category &c : _position_cats(*doc))
        cats.push_back(std::move(c));
    for (Category &c : _colormap_cats(*doc))
        cats.push_back(std::move(c));
    _build_error_map();
    const double dt = double(t0.nsecsElapsed()) / 1e9;
    std::map<QString, long long> cnt;
    for (const Category &c : cats)
        cnt[c.severity] += static_cast<long long>(c.items.size());
    QStringList parts;
    for (const QString &sev : {SEV_ERROR, SEV_WARN, SEV_INFO}) {
        const QString col = cnt[sev] ? hexrgb(sev_color(sev)) : theme::INK_FAINT;
        parts << u"<span style='color:%1'><b>%2</b> %3%4</span>"_s.arg(col).arg(cnt[sev]).arg(sev_name(sev),
                                                                                                cnt[sev] == 1 ? u""_s : u"s"_s);
    }
    summary->setText(parts.join(u" · "_s) +
                     u"<span style='color:%1'> — checked in %2 s</span>"_s.arg(theme::INK_FAINT, py::fmt_f(dt, 1)));
    next_btn->setEnabled(bool(cnt[SEV_ERROR] + cnt[SEV_WARN]));
    _fill_tree();
    _cur.reset();
    Q_EMIT bannerChanged(u""_s, std::nullopt);
    Q_EMIT overlayChanged();
    Q_EMIT errorMapChanged();
    Q_EMIT checked();
}

// ------------------------------------------------------------------------------------------- colour maps
// (C++ only, 2026-11-08) map/terrain's colour maps lie over the whole map, stretched: one drawn for a wider map (a cut
// by an older ModEditor left colormap.dds whole) colours the land where it isn't — the tester's report.
std::vector<Category> MapCheckPanel::_colormap_cats(document::Document &d)
{
    std::vector<Item> items;
    const moreviews::AssetEdits *assets = d.maps.contains(u"assets"_s) ? &d.map<moreviews::AssetEdits>(u"assets"_s) : nullptr;
    std::vector<std::uint8_t> sea(static_cast<size_t>(md.n), 0);
    for (const auto &[pid, pr] : mod.provinces)
        if (pid < md.n && pr.sea)
            sea[size_t(pid)] = 1;
    for (const mapsize::ColourMapOff &c : mapsize::colormaps_to_fit(mod, md.idmap, sea)) {
        if (assets && assets->fresh.contains(c.rel))          // (a fitted one waits for Apply)
            continue;
        Item item;
        item.text = u"%1 is %2 × %3: drawn for a map about %4 columns wide — this map is %5 × %6 (match with the "
                    u"map's land: %7 as the game stretches it, %8 once cut)"_s.arg(c.rel)
                        .arg(c.w).arg(c.h).arg(py::round_int(c.span)).arg(md.w).arg(md.h)
                        .arg(py::fmt_f(c.now, 2), py::fmt_f(c.cut, 2));
        item.data = mapcheck::tup({u"colormap"_s, c.rel});
        items.push_back(std::move(item));
    }
    std::vector<Category> out;
    const long long n = static_cast<long long>(items.size());
    std::optional<mapcheck::Fix> fix;
    if (n)
        fix = mapcheck::Fix{u"Cut them to the map (%1)"_s.arg(n), u"colormap"_s, Val()};
    out.push_back(Category{u"colormap_size"_s, u"Colour maps that don't fit the map"_s, SEV_WARN, std::move(items), fix,
                           u"The game stretches map/terrain's colour maps over the whole map. These are shaped for a map of "
                           u"another width, so their colours land beside the land they were drawn for (stains on the map). "
                           u"The fix finds where the map's land lies on the picture and cuts the picture to the map's "
                           u"columns, in its own format; written on Apply (Ctrl+Z undoes it)."_s,
                           u"map"_s, u"Cut it to the map"_s});
    return out;
}

void MapCheckPanel::_fix_colormaps(const std::vector<const Item *> &items)
{
    std::vector<std::uint8_t> sea(static_cast<size_t>(md.n), 0);
    for (const auto &[pid, pr] : mod.provinces)
        if (pid < md.n && pr.sea)
            sea[size_t(pid)] = 1;
    const QString root = !backup_root.isEmpty() ? backup_root : pypath::join(mod.root, {u".."_s, u"backups"_s});
    const QString dir = pypath::join(root, {u"fitted"_s, QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s)});
    OrderedMap<QString, moreviews::Asset> files;
    QStringList bad;
    for (const Item *it : items) {
        const QString rel = S(it->data[1]);
        const QString path = mod.p(rel.split(u'/'));
        Q_EMIT message(u"Fitting %1 to the map…"_s.arg(rel));
        QApplication::processEvents();
        QString why;
        const QByteArray bytes = mapsize::fit_colormap(path, md.idmap, sea, &why);
        if (bytes.isEmpty()) {
            bad << u"%1 %2"_s.arg(rel, why);
            continue;
        }
        QDir().mkpath(dir);
        const QString out = pypath::join(dir, {pypath::basename(rel)});
        QFile f(out);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            bad << u"%1 couldn't be written to %2"_s.arg(rel, out);
            continue;
        }
        f.close();
        moreviews::Asset a;
        a.how = u"file"_s;
        a.path = out;
        files.insert(rel, a);
    }
    if (!files.isEmpty()) {
        auto &assets = doc->map<moreviews::AssetEdits>(u"assets"_s);
        doc->edit(u"%1 colour map%2 cut to the map"_s.arg(files.size()).arg(pl(files.size())), {}, [&assets, files] {
            for (const auto &[k, a] : files)
                assets.fresh.insert(k, a);
        }, {}, {}, {u"assets"_s});
    }
    Q_EMIT message(bad.isEmpty() ? u"%1 colour map%2 cut to the map — written into the mod on Apply."_s.arg(files.size()).arg(pl(files.size()))
                                 : u"Not fitted: "_s + bad.join(u"; "_s));
}

// ---------------------------------------------------------------------------------------------- positions
std::vector<Category> MapCheckPanel::_position_cats(document::Document &d)
{
    pospanel::PosChecks *pp = pos_panel;
    if (!pp)
        return {};
    const auto &probs = pp->compute_problems(d);
    std::vector<long long> pids;
    for (const auto &[pid, _l] : probs)
        pids.push_back(pid);
    std::sort(pids.begin(), pids.end());
    const QStringList G = {u"crash"_s, u"naval"_s, u"place"_s, u"missing"_s};
    std::map<QString, std::vector<Item>> groups;
    // (C++ only, 2026-11-08) places that lie outside the map altogether — the tester's map, cut by Map size after a
    // renumbering, kept 22 of them (x below 0) and the game crashed at the campaign start
    std::set<long long> offmap;
    {
        auto &pf = d.map<positions::PositionsFile>(u"positions"_s);
        std::vector<long long> all;
        for (const auto &kv : pf.entries)
            all.push_back(kv.first);
        std::sort(all.begin(), all.end());
        for (long long pid : all) {
            const positions::Entry e = pf.get(pid);
            QStringList ks, where;
            for (const auto &[k, xy] : e.pts)
                if (!(0 <= xy.first && xy.first < double(md.w) && 0 <= xy.second && xy.second < double(md.h))) {
                    ks << k;
                    where << u"%1 at %2, %3"_s.arg(k, py::fmt_g(xy.first), py::fmt_g(xy.second));
                }
            // the parts Positions doesn't know (spawn_railway_track = { { x y } … }): kept as text, points in them too
            for (const QStringList *ex : {&e.extra, &e.bp_extra, &e.br_extra})
                for (const QString &s : *ex) {
                    long long moved = 0;
                    mapsize::keep_inside(s, md.w, md.h, &moved);
                    if (!moved)
                        continue;
                    const QString nm = s.trimmed().section(QRegularExpression(u"[\\s=]"_s), 0, 0);
                    if (!ks.contains(EXTRA_KIND))
                        ks << EXTRA_KIND;
                    where << u"%1 point%2 of %3"_s.arg(moved).arg(pl(moved), nm.isEmpty() ? u"a block"_s : nm);
                }
            if (ks.isEmpty())
                continue;
            offmap.insert(pid);
            const moddata::Province *pr = mod.provinces.get(pid);
            std::vector<Val> kv;
            for (const QString &k : ks)
                kv.emplace_back(k);
            Item item;
            item.text = u"#%1 %2: %3 — the map is %4 × %5"_s.arg(pid).arg(pr ? pr->name.trimmed() : u"?"_s,
                                                                         where.join(u"; "_s)).arg(md.w).arg(md.h);
            item.pids = {pid};
            if (0 < pid && pid < md.n && md.area[size_t(pid)])
                item.point = XY{md.cx[size_t(pid)] + 0.5, md.cy[size_t(pid)] + 0.5};
            item.data = mapcheck::tup({u"pos"_s, pid, Val(kv)});
            groups[u"offmap"_s].push_back(std::move(item));
        }
    }
    for (long long pid : pids) {
        const moddata::Province *pr = mod.provinces.get(pid);
        const QString nm = pr ? (pr->name.trimmed().isEmpty() ? u"(unnamed)"_s : pr->name.trimmed()) : u"?"_s;
        const QString name = u"#%1 %2"_s.arg(pid).arg(nm);
        std::optional<XY> pt;
        if (0 < pid && pid < md.n && md.area[size_t(pid)])
            pt = XY{md.cx[size_t(pid)] + 0.5, md.cy[size_t(pid)] + 0.5};
        std::map<QString, std::vector<std::pair<QString, QString>>> per;
        for (const auto &[k, t] : *probs.get(pid)) {
            if (k == u"history")                    // the history's naval base: the "naval" group has it
                continue;
            const QString g = t == positions::CRASH ? u"crash"_s : k == u"naval_base" ? u"naval"_s
                              : t.startsWith(u"no "_s)                           ? u"missing"_s
                                                                                  : u"place"_s;
            if (g == u"place" && offmap.count(pid))         // (listed as off the map: an error of its own)
                continue;
            per[g].emplace_back(k, t);
        }
        for (const QString &g : G) {
            const auto it = per.find(g);
            if (it == per.end() || it->second.empty())
                continue;
            QStringList ts;
            QStringList ks;
            for (const auto &[k, t] : it->second) {
                ts << t;
                if (!ks.contains(k))
                    ks << k;
            }
            std::sort(ks.begin(), ks.end());
            std::vector<Val> kv;
            for (const QString &k : ks)
                kv.emplace_back(k);
            Item item;
            item.text = u"%1: %2"_s.arg(name, ts.join(u"; "_s));
            item.pids = {pid};
            item.point = pt;
            item.data = mapcheck::tup({u"pos"_s, pid, Val(kv)});
            groups[g].push_back(std::move(item));
        }
    }
    auto n = [&](const QString &g) { return static_cast<long long>(groups[g].size()); };
    auto fix = [&](const QString &g, const QString &label) -> std::optional<mapcheck::Fix> {
        if (!n(g))
            return std::nullopt;
        return mapcheck::Fix{label, u"pos"_s, Val()};
    };
    std::vector<Category> out;
    out.push_back(Category{u"pos_offmap"_s, u"Positions outside the map"_s, SEV_ERROR, groups[u"offmap"_s],
                           fix(u"offmap"_s, u"Put them back on their provinces (%1)"_s.arg(n(u"offmap"_s))),
                           u"positions.txt places a unit, name, building or naval base of these provinces outside the map "
                           u"(a map cut by an older ModEditor after provinces were renumbered could leave them). The fix "
                           u"places them again like Positions does; what can't be placed goes to the province's middle."_s,
                           u"map"_s, u"Put them back on its province"_s});
    out.push_back(Category{u"pos_crash"_s, u"Positions: naval bases the game crashes on"_s, SEV_ERROR, groups[u"crash"_s],
                           fix(u"crash"_s, u"Place their naval base positions (%1)"_s.arg(n(u"crash"_s))),
                           u"The history builds a naval base here, but positions.txt has no naval base position — "
                           u"the game crashes when it draws it."_s,
                           u"map"_s, u"Place its naval base position"_s});
    out.push_back(Category{u"pos_naval"_s, u"Positions: naval base positions"_s, SEV_WARN, groups[u"naval"_s],
                           fix(u"naval"_s, u"Fix all %1 naval base positions"_s.arg(n(u"naval"_s))),
                           u"A coastal province without a naval base position crashes the game as soon as someone "
                           u"builds a naval base there. Positions on land / far out at sea get moved, ones without a "
                           u"valid coast removed (never where the history builds a base)."_s,
                           u"map"_s, u"Fix its naval base position"_s});
    out.push_back(Category{u"pos_place"_s, u"Positions: names / units / buildings in the wrong place"_s, SEV_WARN,
                           groups[u"place"_s], fix(u"place"_s, u"Re-place them (%1 provinces)"_s.arg(n(u"place"_s))),
                           u"Outside their province, or a name sticking out of it. The fix places them again like "
                           u"Positions does."_s,
                           u"map"_s, u"Re-place its positions"_s});
    out.push_back(Category{u"pos_missing"_s, u"Positions: missing unit / name positions"_s, SEV_INFO,
                           groups[u"missing"_s],
                           fix(u"missing"_s, u"Place all missing positions (%1 provinces)"_s.arg(n(u"missing"_s))),
                           u"Vanilla has a unit and a name position for every province; these have none in "
                           u"positions.txt. The fix places them like Positions does."_s,
                           u"map"_s, u"Place its missing positions"_s});
    return out;
}

// ---------------------------------------------------------------------------------------------- the error map
const Category *MapCheckPanel::_cat(const QString &key) const
{
    for (const Category &c : cats)
        if (c.key == key)
            return &c;
    return nullptr;
}

void MapCheckPanel::_build_error_map()
{
    const long long n = md.n;
    std::vector<std::uint8_t> lut(size_t(n) * 3);
    auto put = [&lut](long long p, const Rgb &c) {
        lut[size_t(p) * 3] = std::uint8_t(std::get<0>(c));
        lut[size_t(p) * 3 + 1] = std::uint8_t(std::get<1>(c));
        lut[size_t(p) * 3 + 2] = std::uint8_t(std::get<2>(c));
    };
    for (long long p = 0; p < n; ++p)
        put(p, LAND_FILL);
    for (const auto &[p, pr] : mod.provinces)
        if (pr.sea && 0 < p && p < n)
            put(p, SEA_FILL);
    if (n)
        put(0, {0, 0, 0});
    _by_pid.clear();
    for (const QString &sev : {SEV_WARN, SEV_ERROR}) {   // errors are painted over warnings
        for (int ci = 0; ci < int(cats.size()); ++ci) {
            const Category &c = cats[size_t(ci)];
            if (c.severity != sev || PIXEL_KEYS.contains(c.key) || c.key.startsWith(u"adj_"_s))
                continue;
            for (int ii = 0; ii < int(c.items.size()); ++ii)
                for (long long p : c.items[size_t(ii)].pids)
                    if (0 < p && p < n) {
                        _by_pid[p] = {ci, ii};
                        if (show[u"prov"_s])
                            put(p, sev == SEV_ERROR ? ERR_FILL : WARN_FILL);
                    }
        }
    }
    fill_lut = std::move(lut);
    fill_version += 1;
    _masks_dirty = true;                             // (the pixel masks: when the error map or its hover asks)
    _after_masks();
}

void MapCheckPanel::ensure_masks()
{
    // (C++ only, 2026-11-14) the stray-bit and unknown-colour masks of the error map, built when the Errors map or its
    // hover needs them — no longer after every brush stroke on another page (a flood fill per stray bit: 20-30 ms on
    // ZRM's Legacy, each stroke)
    if (!_masks_dirty)
        return;
    _masks_dirty = false;
    stray_mask.reset();
    _stray_at.clear();
    const Category *c = _cat(u"stray"_s);
    if (c && !c->items.empty()) {
        grid::Grid<std::uint8_t> mask(md.h, md.w);
        for (int ii = 0; ii < int(c->items.size()); ++ii) {
            const Item &it = c->items[size_t(ii)];
            if (!it.pids.empty() && it.point) {
                const autofix::Flat idx = autofix::piece_pixels(md.idmap, it.pids[0], it.point->first, it.point->second);
                for (std::int64_t f : idx) {
                    mask.flat()[size_t(f)] = 1;
                    _stray_at[f] = ii;
                }
            }
        }
        stray_mask = std::move(mask);
    }
    c = _cat(u"unknown"_s);
    if (!c || c->items.empty()) {
        unknown_mask.reset();
        _unknown_key.reset();
        _unknown_for.reset();
        _unknown_tried = false;
    } else if (!_unknown_tried || _unknown_for != geo_cache.key) {   // read the file only when the map changed
        unknown_mask.reset();
        _unknown_key.reset();
        try {
            std::shared_ptr<const grid::Grid<std::int32_t>> key =
                autofix::file_colours_kept(mod.p({u"map"_s, u"provinces.bmp"_s}), md.h, md.w);
            QSet<std::int32_t> want;
            for (const Item &it : c->items)
                want.insert(std::int32_t((I(it.data[0]) << 16) | (I(it.data[1]) << 8) | I(it.data[2])));
            grid::Grid<std::uint8_t> m(md.h, md.w);
            const auto &ids = md.idmap.flat();
            const auto &kf = key->flat();
            for (size_t i = 0; i < ids.size(); ++i)
                m.flat()[i] = ids[i] == 0 && want.contains(kf[i]);
            unknown_mask = std::move(m);
            _unknown_key = std::move(key);
            _unknown_for = geo_cache.key;
            _unknown_tried = true;
        } catch (const std::exception &) {
        }
    }
}

void MapCheckPanel::_after_masks()
{
    if (!ocean) {
        std::vector<std::uint8_t> oc(256, 0);
        bool any = false;
        for (const auto &[i, t] : layerbmp::terrain_types(mod))
            if (t == u"ocean" && i < 256) {
                oc[size_t(i)] = 1;
                any = true;
            }
        if (!any)
            oc[254] = 1;
        ocean = std::move(oc);
    }
    auto count = [this](const QString &k) -> long long {
        const Category *x = _cat(k);
        return x ? static_cast<long long>(x->items.size()) : 0;
    };
    long long adj = 0;
    for (const Category &x : cats)
        if (x.key.startsWith(u"adj_"_s))
            adj += static_cast<long long>(x.items.size());
    const std::map<QString, long long> counts = {
        {u"prov"_s, static_cast<long long>(_by_pid.size())}, {u"river"_s, count(u"rivers"_s)},
        {u"terrain"_s, count(u"landsea_terrain"_s)}, {u"rivers_ls"_s, count(u"landsea_rivers"_s)},
        {u"stray"_s, count(u"stray"_s)}, {u"unknown"_s, count(u"unknown"_s)}, {u"adj"_s, adj}};
    for (const Layer &l : LAYERS) {
        const auto it = counts.find(l.key);
        show_boxes[l.key]->setText(it != counts.end() ? u"%1  (%2)"_s.arg(l.text).arg(it->second) : l.text);
    }
}

void MapCheckPanel::_show_changed(const QString &key, bool on)
{
    show[key] = on;
    if (key == u"prov" && doc)
        _build_error_map();
    Q_EMIT errorMapChanged();
    Q_EMIT overlayChanged();
}

std::optional<std::pair<int, int>> MapCheckPanel::problem_at(double x, double y)
{
    // (category index, item index) of the problem at map pixel (x, y) — pixel problems first, then the province's own
    // problems (errors before warnings); None if there's none
    const long long xi = static_cast<long long>(x), yi = static_cast<long long>(y);
    if (cats.empty() || !doc || !(0 <= xi && xi < md.w && 0 <= yi && yi < md.h))
        return std::nullopt;
    ensure_masks();
    std::map<QString, int> keys;
    for (int ci = 0; ci < int(cats.size()); ++ci)
        keys[cats[size_t(ci)].key] = ci;                 // (a later category of the same key wins, as in a dict)
    const std::int64_t f = yi * md.w + xi;
    if (show[u"stray"_s] && keys.contains(u"stray"_s) && _stray_at.contains(f))
        return std::make_pair(keys[u"stray"_s], _stray_at[f]);
    if (keys.contains(u"rivers"_s) && show[u"river"_s]) {
        const int ci = keys[u"rivers"_s];
        std::optional<std::pair<double, int>> best;
        const auto &items = cats[size_t(ci)].items;
        for (int ii = 0; ii < int(items.size()); ++ii) {
            const auto &pt = items[size_t(ii)].point;
            if (pt && std::abs(pt->first - x) <= 2.5 && std::abs(pt->second - y) <= 2.5) {
                const std::pair<double, int> d{std::abs(pt->first - x) + std::abs(pt->second - y), ii};
                if (!best || d < *best)
                    best = d;
            }
        }
        if (best)
            return std::make_pair(ci, best->second);
    }
    const long long p = md.idmap(yi, xi);
    if (p) {
        auto *tb = doc->maps.contains(u"terrain_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s) : nullptr;
        auto *rb = doc->maps.contains(u"rivers_bmp"_s) ? &doc->map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s) : nullptr;
        if (keys.contains(u"landsea_terrain"_s) && show[u"terrain"_s] && tb && tb->data && ocean &&
            bool(md.sea[size_t(p)]) != bool((*ocean)[(*tb->data)(yi, xi)]))
            if (const auto hit = _item_with(keys[u"landsea_terrain"_s], p))
                return hit;
        if (keys.contains(u"landsea_rivers"_s) && show[u"rivers_ls"_s] && rb && rb->data) {
            const int v = (*rb->data)(yi, xi);
            if ((md.sea[size_t(p)] && v == 255) || (!md.sea[size_t(p)] && v == 254))
                if (const auto hit = _item_with(keys[u"landsea_rivers"_s], p))
                    return hit;
        }
    } else if (keys.contains(u"unknown"_s) && unknown_mask && (*unknown_mask)(yi, xi)) {
        const std::int32_t k = (*_unknown_key)(yi, xi);
        const auto &items = cats[size_t(keys[u"unknown"_s])].items;
        for (int ii = 0; ii < int(items.size()); ++ii) {
            const Val &d = items[size_t(ii)].data;
            if (((I(d[0]) << 16) | (I(d[1]) << 8) | I(d[2])) == k)
                return std::make_pair(keys[u"unknown"_s], ii);
        }
    }
    if (p && _by_pid.contains(p))
        return _by_pid[p];
    return std::nullopt;
}

std::optional<std::pair<int, int>> MapCheckPanel::_item_with(int ci, long long pid) const
{
    const auto &items = cats[size_t(ci)].items;
    for (int ii = 0; ii < int(items.size()); ++ii)
        if (items[size_t(ii)].data == Val(pid))
            return std::make_pair(ci, ii);
    return std::nullopt;
}

QString MapCheckPanel::hover_text(double x, double y)
{
    const auto hit = problem_at(x, y);
    if (!hit)
        return u""_s;
    const Category &c = cats[size_t(hit->first)];
    return u"%1  %2  (click to select)"_s.arg(c.severity == SEV_INFO ? u"ⓘ"_s : u"⚠"_s,
                                               c.items[size_t(hit->second)].text);
}

static std::pair<int, std::optional<int>> data_of(const QTreeWidgetItem *it)
{
    const QVariantList l = it->data(0, Qt::UserRole).toList();
    if (l.size() < 2)
        return {-1, std::nullopt};
    return {l[0].toInt(), l[1].isNull() ? std::nullopt : std::optional<int>(l[1].toInt())};
}

void MapCheckPanel::select_problem(int ci, int ii)
{
    // select a problem in the list without moving the camera (it was clicked on the map)
    _quiet = true;
    struct Done {
        bool &q;
        ~Done() { q = false; }
    } done{_quiet};
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        const auto d = data_of(top);
        if (d.first == ci && !d.second) {
            top->setExpanded(true);
            QTreeWidgetItem *ch = ii < top->childCount() ? top->child(ii) : nullptr;
            if (ch && data_of(ch) == std::make_pair(ci, std::optional<int>(ii))) {
                tree->clearSelection();
                ch->setSelected(true);
                tree->scrollToItem(ch);
            } else {
                _go(ci, ii);
            }
            return;
        }
    }
}

void MapCheckPanel::show_item(const QString &key, int ii)
{
    // this problem selected in the list (its fix buttons are then at hand)
    const Category *c = _cat(key);
    if (!c || ii < 0 || ii >= int(c->items.size()))
        return;
    _select_next(key, ii);
}

std::pair<std::map<mapcheck::OptStr, long long>, long long> MapCheckPanel::error_counts() const
{
    std::map<mapcheck::OptStr, long long> out;
    long long total = 0;
    for (const Category &c : cats)
        if (c.severity == SEV_ERROR && !c.items.empty()) {
            out[c.tab] += static_cast<long long>(c.items.size());
            total += static_cast<long long>(c.items.size());
        }
    return {out, total};
}

void MapCheckPanel::_fill_tree()
{
    const auto keep = _current_cat_key();
    tree->blockSignals(true);
    tree->clear();
    auto order = [](const QString &s) { return s == SEV_ERROR ? 0 : s == SEV_WARN ? 1 : 2; };
    std::vector<int> idx(cats.size());
    for (size_t i = 0; i < idx.size(); ++i)
        idx[i] = int(i);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        const Category &x = cats[size_t(a)], &y = cats[size_t(b)];
        return std::make_tuple(x.items.empty(), order(x.severity), a) < std::make_tuple(y.items.empty(), order(y.severity), b);
    });
    QTreeWidgetItem *sel_item = nullptr;
    for (int ci : idx) {
        const Category &c = cats[size_t(ci)];
        auto *top = new QTreeWidgetItem(QStringList{
            c.items.empty() ? u"%1  ✔"_s.arg(c.title) : u"%1  (%2)"_s.arg(c.title).arg(c.items.size())});
        top->setData(0, Qt::UserRole, QVariantList{ci, QVariant()});
        top->setIcon(0, widgets::swatch_icon(c.items.empty() ? Rgb{90, 150, 90} : sev_color(c.severity), 10));
        if (c.items.empty())
            top->setForeground(0, QBrush(QColor(theme::INK_FAINT)));
        for (int ii = 0; ii < int(std::min<size_t>(c.items.size(), 600)); ++ii) {
            auto *ch = new QTreeWidgetItem(QStringList{c.items[size_t(ii)].text});
            ch->setData(0, Qt::UserRole, QVariantList{ci, ii});
            ch->setToolTip(0, c.items[size_t(ii)].text);
            top->addChild(ch);
        }
        if (c.items.size() > 600) {
            auto *more = new QTreeWidgetItem(QStringList{
                u"… and %1 more (fix the ones above, then check again)"_s.arg(c.items.size() - 600)});
            more->setData(0, Qt::UserRole, QVariantList{ci, QVariant()});
            top->addChild(more);
        }
        tree->addTopLevelItem(top);
        if (keep && c.key == *keep)
            sel_item = top;
    }
    if (sel_item) {
        sel_item->setSelected(true);
        sel_item->setExpanded(true);
    }
    tree->blockSignals(false);
    _show_fix();
}

std::optional<QString> MapCheckPanel::_current_cat_key() const
{
    const auto items = tree->selectedItems();
    if (items.isEmpty())
        return std::nullopt;
    const int ci = data_of(items[0]).first;
    if (0 <= ci && ci < int(cats.size()))
        return cats[size_t(ci)].key;
    return std::nullopt;
}

void MapCheckPanel::_picked()
{
    const auto items = tree->selectedItems();
    if (items.isEmpty())
        return;
    const auto [ci, ii] = data_of(items[0]);
    _show_fix();
    if (!ii) {
        items[0]->setExpanded(true);
        _cur = std::make_pair(ci, std::optional<int>());
        Q_EMIT bannerChanged(u""_s, std::nullopt);
        Q_EMIT overlayChanged();
        return;
    }
    _go(ci, *ii);
}

void MapCheckPanel::_double(QTreeWidgetItem *tree_item)
{
    const auto [ci, ii] = data_of(tree_item);
    if (!(0 <= ci && ci < int(cats.size())))
        return;
    const Category &c = cats[size_t(ci)];
    Q_EMIT jumpRequested(c.key, ii && *ii < int(c.items.size()) ? *ii : -1);
}

void MapCheckPanel::_go(int ci, int ii)
{
    const Category &c = cats[size_t(ci)];
    const Item &it = c.items[size_t(ii)];
    _cur = std::make_pair(ci, std::optional<int>(ii));
    Q_EMIT bannerChanged(u"%1  %2"_s.arg(c.severity == SEV_INFO ? u"ⓘ"_s : u"⚠"_s, it.text), sev_color(c.severity));
    QList<long long> land, sea;
    for (long long p : it.pids)
        if (const moddata::Province *pr = mod.provinces.get(p))
            (pr->sea ? sea : land) << p;
    if (!it.pids.empty())
        Q_EMIT selectRequested(land, sea);
    std::optional<XY> pt = it.point ? it.point : _adj_point(it);
    if (!pt && !it.pids.empty() && 0 < it.pids[0] && it.pids[0] < md.n && md.area[size_t(it.pids[0])] > 0)
        pt = XY{md.cx[size_t(it.pids[0])] + 0.5, md.cy[size_t(it.pids[0])] + 0.5};
    if (pt && !_quiet) {
        // a single pixel (rivers.bmp) needs a close look; provinces a wider one
        const auto z = PIXEL_ZOOM.find(c.key);
        Q_EMIT zoomRequested(pt->first, pt->second, z != PIXEL_ZOOM.end() ? z->second : 4.0);
    }
    Q_EMIT message(it.text);
    Q_EMIT overlayChanged();
}

std::optional<XY> MapCheckPanel::_adj_point(const Item &it)
{
    // where an adjacency problem is: the border / closest points of its two provinces
    const Val &d = it.data;
    if (!adj_geo || !len(d) || !(head_is(d, u"row"_s) || head_is(d, u"pair"_s) || head_is(d, u"bank"_s)))
        return std::nullopt;
    const bool row = head_is(d, u"row"_s);
    const long long a = I(d[row ? 2 : 1]), b = I(d[row ? 3 : 2]);
    if (const auto bp = adj_geo->border_point(a, b))
        return *bp;
    if (const auto an = adj_geo->anchors(a, b))
        return lerp(an->first, an->second, 0.5, md.w);
    return std::nullopt;
}

void MapCheckPanel::_next_problem()
{
    std::vector<std::pair<int, int>> flat;
    for (int ci = 0; ci < int(cats.size()); ++ci)
        if (cats[size_t(ci)].severity == SEV_ERROR || cats[size_t(ci)].severity == SEV_WARN)
            for (int ii = 0; ii < int(cats[size_t(ci)].items.size()); ++ii)
                flat.emplace_back(ci, ii);
    if (flat.empty())
        return;
    _next = (_next + 1) % int(flat.size());
    _go(flat[size_t(_next)].first, flat[size_t(_next)].second);
}

// ---------------------------------------------------------------------------------------------- fixes
std::pair<const Category *, const Item *> MapCheckPanel::_selected() const
{
    // (category, item or None) selected in the list
    const auto sel = tree->selectedItems();
    if (sel.isEmpty())
        return {nullptr, nullptr};
    const auto [ci, ii] = data_of(sel[0]);
    if (!(0 <= ci && ci < int(cats.size())))
        return {nullptr, nullptr};
    const Category &c = cats[size_t(ci)];
    return {&c, ii && 0 <= *ii && *ii < int(c.items.size()) ? &c.items[size_t(*ii)] : nullptr};
}

bool MapCheckPanel::_fixable(const Category &c, const Item &it)
{
    const Val &d = it.data;
    if (c.key == u"seastarts")
        return d.truthy() && (head_is(d, u"dup"_s) || head_is(d, u"undef"_s));
    if (c.key == u"newprov")
        return S(d) == u"todo" || S(d) == u"changed";
    if (c.key == u"nations")
        return d.truthy() && (!d[3].is_none() || S(d[2]) == u"upper_house" || S(d[2]) == u"culture");
    if (c.key == u"names" || c.key == u"cb_texts")
        return d.truthy() && d[2].truthy();
    if (c.key == u"wars" || c.key == u"event_ids" || c.key == u"cb_order" || c.key == u"ref_modifier")
        return d.truthy();
    if (c.key == u"brackets" || c.key == u"cb_broken")
        return d.truthy() && head_is(d, u"close"_s);
    if (c.key == u"equals")
        return d.truthy() && head_is(d, u"equals"_s);
    if (c.key == u"build_window")                       // (one fix makes the window ready for all of them)
        return d.truthy();
    if (c.key == u"script_prov")                         // (a note left by a delete is looked at, not fixed)
        return d.truthy() && head_is(d, u"missing"_s);
    if (c.key == u"oob")
        return d.truthy() && !head_is(d, u"oob_file"_s);
    if (c.key == u"offmap" || c.key == u"offmap_sea")   // a new province is painted, not deleted
        return d.truthy() && !d[2].truthy();
    if (c.key == u"adj_other")
        return d.truthy() && !d[4].is_none();
    if (c.key == u"parts")                               // sea tiles can't carry the island flag
        return !it.pids.empty() && !(d == Val(u"sea"_s));
    if (c.key == u"states")                              // never strip provinces out of their state for a super region
        return !(d.truthy() && head_is(d, u"multi"_s) && len(d) > 3 && d[3].truthy());
    return true;
}

void MapCheckPanel::_show_fix()
{
    const auto [c, it] = _selected();
    note->setText(c ? c->note : u""_s);
    note->setVisible(c && !c->note.isEmpty());
    if (c && c->fix) {
        fix_btn->setText(c->fix->label);
        fix_btn->show();
    } else {
        fix_btn->hide();
    }
    const bool one = c && c->fix && it && c->items.size() > 1 && _fixable(*c, *it);
    fix_one_btn->setText(one ? u"%1  ▸ next"_s.arg(c->fix_one ? *c->fix_one : u"Fix this one"_s) : u""_s);
    fix_one_btn->setVisible(one);
    if (!c)
        sel_title->setText(u"Select a problem in the list below — its fix buttons appear here."_s);
    else
        sel_title->setText(u"<b>%1</b>%2"_s.arg(esc(c->title), it ? u"<br>"_s + esc(it->text) : u""_s));
    QString why;
    if (c && !c->items.empty() && !c->fix)
        why = u"No automatic fix for this group — double-click a line to open the tab where you fix it by hand."_s;
    else if (c && c->fix && it && !_fixable(*c, *it))
        why = u"This one can't be fixed automatically — double-click it to fix it by hand."_s;
    nofix->setText(why);
    nofix->setVisible(!why.isEmpty());
}

void MapCheckPanel::_fix(bool one)
{
    const auto [cp, it] = _selected();
    if (!cp || !cp->fix)
        return;
    if (one && (!it || !_fixable(*cp, *it)))
        return;
    const Category c = *cp;                              // (a copy: the checks run again below)
    std::vector<const Item *> items;
    int pos = 0;
    if (one) {
        pos = int(it - cp->items.data());
        items.push_back(&c.items[size_t(pos)]);
    } else {
        for (const Item &x : c.items)
            if (_fixable(c, x))
                items.push_back(&x);
    }
    const QString key = c.key;
    const size_t steps = doc->undo_stack.size();
    try {
        _do_fix(c, items);
    } catch (const std::exception &e) {                 // say so in red (no pop-ups), keep the list as it is
        Q_EMIT message(u"⚠ Couldn't fix that: %1"_s.arg(QString::fromUtf8(e.what())));
        return;
    }
    if (_fix_note) {                                     // a fix outside the undo list (files moved): it says itself
        const QString n = *_fix_note;
        _fix_note.reset();
        refresh(doc);
        Q_EMIT message(n);
        return;
    }
    const bool done = doc->undo_stack.size() != steps;
    const QString label = one ? (c.fix_one ? *c.fix_one : u"Fix this one"_s) : c.fix->label;
    refresh(doc);
    if (one)
        _select_next(key, pos);                          // straight on to the next problem of the group
    Q_EMIT message(done ? u"%1 — Ctrl+Z undoes it, Apply writes it."_s.arg(label)
                        : u"⚠ That can't be fixed automatically — double-click it to fix it by hand."_s);
}

void MapCheckPanel::_select_next(const QString &key, int pos)
{
    int ci = -1;
    for (int i = 0; i < int(cats.size()); ++i)
        if (cats[size_t(i)].key == key) {
            ci = i;
            break;
        }
    if (ci < 0 || cats[size_t(ci)].items.empty())
        return;
    const int ii = std::min(pos, int(cats[size_t(ci)].items.size()) - 1);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        const auto d = data_of(top);
        if (d.first == ci && !d.second) {
            top->setExpanded(true);
            if (QTreeWidgetItem *ch = top->child(ii)) {
                tree->clearSelection();
                ch->setSelected(true);
                tree->scrollToItem(ch);
            }
            return;
        }
    }
}

void MapCheckPanel::_commit_pixels(const QString &label, const std::vector<Piece> &pieces, const QString &msg)
{
    drawpanel::DrawPanel *dp = draw_panel;
    std::vector<const std::pair<autofix::Flat, long long> *> ps;
    for (const Piece &p : pieces)
        if (p && !p->first.empty())
            ps.push_back(&*p);
    if (!dp || ps.empty())
        return;
    dp->doc = doc;
    // (np.unique(idx, return_index=True): sorted, each pixel with the id of its first piece)
    std::vector<std::pair<std::int64_t, std::uint16_t>> all;
    for (const auto *p : ps)
        for (std::int64_t f : p->first)
            all.emplace_back(f, static_cast<std::uint16_t>(p->second));
    std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const auto &idm = md.idmap.flat();
    drawpanel::Flat idx;
    drawpanel::Ids old, nw;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i && all[i].first == all[i - 1].first)
            continue;
        idx.push_back(all[i].first);
        old.push_back(idm[size_t(all[i].first)]);
        nw.push_back(all[i].second);
    }
    dp->commit(label, idx, old, nw, msg);
}

// Place the positions of the listed provinces again (the kinds that are wrong / missing), like Positions does. Never
// takes a naval base position away where the history builds a base.
void MapCheckPanel::_fix_positions(const Category &, const std::vector<const Item *> &items)
{
    pospanel::PositionsPanel *pp = pos_panel;
    if (!pp)
        return;
    pp->compute_problems(*doc);
    const std::vector<std::uint8_t> valid = pp->placer.sea_valid;
    auto &pf = doc->map<positions::PositionsFile>(u"positions"_s);
    OrderedMap<long long, positions::Entry> nw;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item *it = items[i];
        const long long pid = I(it->data[1]);
        QSet<QString> kinds;
        for (const Val &k : it->data[2].list())
            kinds.insert(S(k));
        if (kinds.contains(u"naval_base"_s) && pp->_hist_naval.contains(pid) && !pp->_coast.contains(pid) &&
            pf.get(pid).pts.contains(u"naval_base"_s))
            kinds.remove(u"naval_base"_s);
        positions::Entry base = pf.get(pid);
        bool extra_moved = false;
        if (kinds.remove(EXTRA_KIND))                // (C++ only) points in the unknown parts: onto the map's edge
            for (QStringList *ex : {&base.extra, &base.bp_extra, &base.br_extra})
                for (QString &s : *ex) {
                    long long moved = 0;
                    s = mapsize::keep_inside(s, md.w, md.h, &moved);
                    extra_moved = extra_moved || moved;
                }
        if (kinds.isEmpty() && extra_moved)
            nw.insert(pid, base);
        if (!kinds.isEmpty()) {
            positions::Entry e = pp->place_entry(pid, base, kinds, valid);
            // (C++ only) whatever is still off the map goes to the province's middle (a kind Positions doesn't place)
            for (auto &[k, xy] : e.pts)
                if (!(0 <= xy.first && xy.first < double(md.w) && 0 <= xy.second && xy.second < double(md.h)) &&
                    0 < pid && pid < md.n && md.area[size_t(pid)])
                    xy = pf.to_game(positions::XY{md.cx[size_t(pid)] + 0.5, md.cy[size_t(pid)] + 0.5});
            nw.insert(pid, e);
        }
        if (i % 40 == 39) {                          // a long run: say how far it is (no pop-up)
            Q_EMIT message(u"Placing positions… %1 of %2"_s.arg(i + 1).arg(items.size()));
            QApplication::processEvents();
        }
    }
    if (nw.isEmpty())
        return;
    const long long n = nw.size();
    doc->pos_edit(u"Positions of %1 province%2 placed"_s.arg(n).arg(pl(n)), [nw](positions::PositionsFile &pf_) {
        for (const auto &[pid, e] : nw)
            pf_.set_entry(pid, e);
    });
}

void MapCheckPanel::_do_fix(const Category &c, const std::vector<const Item *> &items)
{
    const QString what = c.fix->what;
    const Val &arg = c.fix->arg;
    document::Document &d = *doc;
    auto scripts = [&d]() -> scriptstore::ScriptStore & { return d.map<scriptstore::ScriptStore>(u"scripts"_s); };
    if (what == u"max_provinces") {
        auto &dm = d.map<mapfiles::DefaultMapFile>(u"default_map"_s);
        const long long n = I(arg);
        d.edit(u"max_provinces = %1"_s.arg(n), {}, [&dm, n] { dm.max_provinces = n; }, {}, {}, {u"default_map"_s});
    } else if (what == u"script_prov") {             // (C++ only, 2026-11-09) places naming a province that isn't there
        auto &store = scripts();
        OrderedMap<QString, QSet<QString>> by_file;
        for (const Item *it : items)
            if (_fixable(c, *it))
                by_file[S(it->data[1])].insert(S(it->data[3]));
        OrderedMap<QString, QString> tx;
        long long n = 0;
        for (const auto &[rel, nums] : by_file) {
            const QString t = store.text(rel);
            const provrenumber::ScriptResult r = provrenumber::rewrite_script(t, {}, nums);
            if (r.text != t) {
                tx.insert(rel, r.text);
                n += r.cut;
            }
        }
        if (!tx.isEmpty()) {
            d.edit(u"%1 place%2 naming a missing province taken out"_s.arg(n).arg(pl(n)), {}, [&store, tx] {
                for (const auto &[r, t] : tx)
                    store.set_text(r, t);
            }, {}, {}, {u"scripts"_s});
            Q_EMIT message(u"%1 place%2 taken out (conditions: never true; effects: removed) — each keeps a "
                           u"“# ModEditor:” note, listed here until you take the note out."_s.arg(n).arg(pl(n)));
        }
    } else if (what == u"definitions_path") {        // (C++ only, 2026-11-08)
        auto &dm = d.map<mapfiles::DefaultMapFile>(u"default_map"_s);
        const QString v = S(arg);
        d.edit(u"definitions = \"%1\""_s.arg(v), {}, [&dm, v] { dm.new_definitions = v; }, {}, {}, {u"default_map"_s});
        Q_EMIT message(u"default.map will name this mod's own definition.csv — Apply writes it."_s);
    } else if (what == u"delete_offmap") {
        std::vector<long long> pids;
        for (const Item *it : items)
            if (_fixable(c, *it)) {
                const long long p = I(it->data[1]);
                if (mod.provinces.contains(p) && md.area[size_t(p)] == 0)
                    pids.push_back(p);
            }
        if (!pids.empty()) {
            provdelete::delete_provinces(d, md, pids, u"none"_s);
            const long long k = static_cast<long long>(pids.size());
            Q_EMIT message(u"%1 province%2 without land deleted — Ctrl+Z brings %3 back; Apply writes it and closes "
                           u"the numbers up."_s.arg(k).arg(pl(k), k == 1 ? u"it"_s : u"them"_s));
        }
    } else if (what == u"renumber") {
        Q_EMIT renumberRequested();
    } else if (what == u"sea_dupes") {
        auto &dm = d.map<mapfiles::DefaultMapFile>(u"default_map"_s);
        d.edit(u"Remove duplicate sea_starts"_s, {}, [&dm] { dm.dedupe_sea(); }, {}, {}, {u"default_map"_s});
    } else if (what == u"sea_starts") {
        auto &dm = d.map<mapfiles::DefaultMapFile>(u"default_map"_s);
        QSet<long long> dup, undef;
        for (const Item *it : items) {
            if (head_is(it->data, u"dup"_s))
                dup.insert(I(it->data[1]));
            if (head_is(it->data, u"undef"_s))
                undef.insert(I(it->data[1]));
        }
        d.edit(u"Fix sea_starts (%1)"_s.arg(dup.size() + undef.size()), {}, [&dm, dup, undef] {
            QSet<long long> seen;
            std::vector<long long> out;
            for (long long p : dm.sea) {
                if (undef.contains(p) || (dup.contains(p) && seen.contains(p)))
                    continue;
                seen.insert(p);
                out.push_back(p);
            }
            dm.sea = out;
        }, {}, {}, {u"default_map"_s});
    } else if (what == u"from_neighbours") {
        _from_neighbours(S(arg), items);
    } else if (what == u"factories") {
        std::vector<document::FactoryProblem> fp;
        for (const Item *it : items)
            if (it->factory)
                fp.push_back(*it->factory);
        d.remove_factories(fp);
    } else if (what == u"naval_inland") {
        QList<long long> pids;
        for (const Item *it : items)
            for (long long p : it->pids)
                pids << p;
        d.edit_provinces(u"Remove naval bases without a coast (%1)"_s.arg(pids.size()), pids,
                         [](long long, document::ProvState &st) { st.naval_base = 0; });
    } else if (what == u"newprov") {
        if (drawpanel::DrawPanel *dp = draw_panel) {
            dp->doc = &d;
            for (const Item *it : items)
                dp->finish(it->pids[0]);
        }
    } else if (what == u"stray") {
        std::vector<Piece> pieces;
        for (const Item *it : items)
            if (!it->pids.empty() && it->point)
                pieces.push_back(autofix::piece_owner(md.idmap, it->pids[0], it->point->first, it->point->second));
        const long long n = std::count_if(pieces.begin(), pieces.end(), [](const Piece &p) { return p.has_value(); });
        _commit_pixels(u"Clean %1 stray bit%2"_s.arg(n).arg(pl(n)), pieces,
                       u"Gave %1 stray bit%2 to the province around them."_s.arg(n).arg(pl(n)));
    } else if (what == u"unknown") {
        const grid::Grid<std::int32_t> img = autofix::file_colours(mod.p({u"map"_s, u"provinces.bmp"_s}), md.h, md.w);
        std::vector<std::tuple<int, int, int>> colours;
        for (const Item *it : items)
            colours.emplace_back(int(I(it->data[0])), int(I(it->data[1])), int(I(it->data[2])));
        std::vector<Piece> pieces;
        for (auto &pc : autofix::colour_areas(md.idmap, img, colours))
            pieces.emplace_back(std::move(pc));
        const long long n = static_cast<long long>(pieces.size());
        _commit_pixels(u"Unknown colours → neighbours (%1 areas)"_s.arg(n), pieces,
                       u"Gave %1 area%2 of unknown colours to the provinces around them."_s.arg(n).arg(pl(n)));
    } else if (what == u"diplo") {
        std::vector<Val> todo;
        for (const Item *it : items)
            if (it->data.truthy())
                todo.push_back(it->data);
        auto &oob_ = d.map<oob::OOBEdits>(u"oob"_s);
        auto &dip = d.map<diplomacy::DiploEdits>(u"diplomacy"_s);
        const long long n = static_cast<long long>(todo.size());
        d.edit(u"Fix %1 diplomacy problem%2"_s.arg(n).arg(pl(n)), {}, [&oob_, &dip, todo] {
            for (const Val &v : todo) {
                const QString k0 = S(v[0]);
                if (k0 == u"treaty_remove")
                    dip.remove(I(v[1]));
                else if (k0 == u"nongp") {
                    diplopanel::set_entry(oob_, S(v[1]), S(v[2]), u"level"_s, std::nullopt);
                    diplopanel::set_entry(oob_, S(v[1]), S(v[2]), u"influence_value"_s, std::nullopt);
                } else if (k0 == u"sphere") {
                    const QString t = S(v[1]);
                    QStringList lords;
                    for (const Val &l : v[2].list())
                        lords << S(l);
                    QString keep;
                    long long best = 0;
                    for (const QString &l : lords) {     // max by (influence, tag)
                        const long long inf = diplomacy::influence_of(oob_, l, t);
                        if (keep.isNull() || inf > best || (inf == best && l > keep)) {
                            keep = l;
                            best = inf;
                        }
                    }
                    for (const QString &l : lords)
                        if (l != keep)
                            diplopanel::set_entry(oob_, l, t, u"level"_s, u"4"_s);
                }
            }
        }, {}, {}, {u"oob"_s, u"diplomacy"_s});
    } else if (what == u"south_edge") {               // (C++ only, measured in the game 2026-11-17/18)
        // the map's edges made safe (see mapcheck's south_edge): on a map whose height is no whole number of its tiles
        // the two southernmost rows become sea and carry no river, the two northernmost carry no river; the owned land
        // of the two westernmost columns becomes sea and those columns carry no river. A land pixel takes the nearest
        // sea pixel of its row / column (one provinces.bmp step), a river pixel the nearest pixel that is no river
        // (one rivers.bmp step).
        const long long W = md.w, H = md.h;
        const auto side = mapsize::tile_side(W);
        const bool rows = H > 2 && !(side && H % *side == 0), cols = W > 2;
        using Line = std::vector<std::pair<long long, long long>>;   // (y, x) pixels in order
        auto nearest = [](const std::vector<char> &keep) {
            const long long n = static_cast<long long>(keep.size());
            std::vector<long long> out(static_cast<size_t>(n), -1);
            for (long long i = 0, last = -1; i < n; ++i)
                out[size_t(i)] = last = keep[size_t(i)] ? i : last;
            for (long long i = n - 1, last = -1; i >= 0; --i) {
                last = keep[size_t(i)] ? i : last;
                const long long l = out[size_t(i)];
                out[size_t(i)] = l < 0 ? last : last < 0 ? l : (i - l <= last - i ? l : last);
            }
            return out;
        };
        auto row = [W](long long y) { Line l; for (long long x = 0; x < W; ++x) l.emplace_back(y, x); return l; };
        auto col = [H](long long x) { Line l; for (long long y = 0; y < H; ++y) l.emplace_back(y, x); return l; };
        std::vector<std::pair<Line, bool>> land_lines;   // (line, owned land only)
        std::vector<Line> river_lines;
        if (rows) {
            for (const long long y : {H - 2, H - 1}) {
                land_lines.emplace_back(row(y), false);
                river_lines.push_back(row(y));
            }
            for (const long long y : {0LL, 1LL})
                river_lines.push_back(row(y));
        }
        if (cols)
            for (const long long x : {0LL, 1LL}) {
                land_lines.emplace_back(col(x), true);
                river_lines.push_back(col(x));
            }
        std::map<long long, std::vector<std::int64_t>> to;   // sea tile -> the land pixels it takes
        std::set<std::int64_t> taken;                        // (a pixel in a row and a column: once)
        for (const auto &[line, owned_only] : land_lines) {
            std::vector<char> keep(line.size()), is_sea(line.size());
            for (size_t i = 0; i < line.size(); ++i) {
                const long long id = md.idmap(line[i].first, line[i].second);
                const auto *pr = id > 0 ? mod.provinces.get(id) : nullptr;
                is_sea[i] = pr && pr->sea;
                // (a colour no province uses is left to its own check; unowned land of a column stays)
                keep[i] = !pr || pr->sea || (owned_only && !d.prov_state(id).owner);
            }
            const std::vector<long long> src = nearest(is_sea);
            for (size_t i = 0; i < line.size(); ++i) {
                if (keep[i] || src[i] < 0)
                    continue;
                const auto [y, x] = line[i];
                const auto [sy, sx] = line[size_t(src[i])];
                if (const long long s = md.idmap(sy, sx); s > 0 && taken.insert(std::int64_t(y * W + x)).second)
                    to[s].push_back(std::int64_t(y * W + x));
            }
        }
        long long moved = 0;
        if (!to.empty()) {
            std::vector<Piece> pieces;
            for (auto &[s, px] : to) {
                moved += static_cast<long long>(px.size());
                pieces.emplace_back(std::make_pair(std::move(px), s));
            }
            _commit_pixels(u"Map edges made sea (%1 px)"_s.arg(moved), pieces,
                           u"The map's edges are safe now (%1 land pixel%2 made sea) — Ctrl+Z takes it back."_s.arg(moved).arg(
                               pl(moved)));
        }
        if (d.maps.contains(u"rivers_bmp"_s)) {
            auto &rb = d.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s);
            if (rb.load() && rb.data && rb.data->h() == H && rb.data->w() == W) {
                std::map<std::int64_t, std::uint8_t> set_to;   // (in pixel order; a row and a column meet: once)
                for (const Line &line : river_lines) {
                    std::vector<char> keep(line.size());
                    for (size_t i = 0; i < line.size(); ++i)
                        keep[i] = (*rb.data)(line[i].first, line[i].second) >= 12 ||
                                  set_to.count(std::int64_t(line[i].first * W + line[i].second));   // (0-11 are rivers)
                    const std::vector<long long> src = nearest(keep);
                    for (size_t i = 0; i < line.size(); ++i)
                        if (!keep[i]) {
                            const auto [y, x] = line[i];
                            std::uint8_t v = 255;
                            if (src[i] >= 0) {
                                const auto [sy, sx] = line[size_t(src[i])];
                                const auto f = set_to.find(std::int64_t(sy * W + sx));
                                v = f != set_to.end() ? f->second : (*rb.data)(sy, sx);
                            }
                            set_to[std::int64_t(y * W + x)] = v;
                        }
                }
                std::vector<std::int64_t> idx;
                std::vector<std::uint8_t> vals;
                for (const auto &[k, v] : set_to)
                    idx.push_back(k), vals.push_back(v);
                if (!idx.empty())
                    d.edit(u"Rivers off the map's edges (%1 px)"_s.arg(idx.size()), {},
                           [&rb, idx, vals] { rb.apply(idx, vals); }, {}, {}, {u"rivers_bmp"_s});
            }
        }
    } else if (what == u"rivers") {
        auto &rb = d.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s);
        if (rb.load()) {
            std::vector<std::pair<double, double>> pts;
            for (const Item *it : items)
                if (it->point)
                    pts.push_back(*it->point);
            auto [idx, vals] = autofix::river_repairs(md, *rb.data, pts);
            if (!idx.empty())
                d.edit(u"Repair rivers.bmp (%1 px)"_s.arg(idx.size()), {}, [&rb, idx, vals] { rb.apply(idx, vals); }, {},
                       {}, {u"rivers_bmp"_s});
        }
    } else if (what == u"landsea") {
        auto *tb = d.maps.contains(u"terrain_bmp"_s) ? &d.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s) : nullptr;
        auto *rb = d.maps.contains(u"rivers_bmp"_s) ? &d.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s) : nullptr;
        const auto masks = mapcheck::landsea_masks(mod, md, tb, rb);
        if (!landsea || !masks.contains(S(arg)))
            return;
        const auto &[a, b] = *masks.get(S(arg));
        std::vector<std::int64_t> idx;
        for (size_t i = 0; i < a.flat().size(); ++i)
            if (a.flat()[i] | b.flat()[i])
                idx.push_back(std::int64_t(i));
        if (items.size() < c.items.size()) {             // just these provinces
            QSet<long long> keep;
            for (const Item *it : items)
                keep.insert(I(it->data));
            std::vector<std::int64_t> k2;
            for (std::int64_t i : idx)
                if (keep.contains(md.idmap.flat()[size_t(i)]))
                    k2.push_back(i);
            idx = k2;
        }
        const QString kind = S(arg) == u"terrain" ? u"terrain_bmp"_s : u"rivers_bmp"_s;
        std::vector<long long> nw;
        for (std::int64_t i : idx)
            nw.push_back(md.idmap.flat()[size_t(i)]);
        std::optional<layerbmp::LandSea::Change> ch;
        for (auto &x : landsea->changes(idx, nw))
            if (x.bitmap->kind == kind) {
                ch = x;
                break;
            }
        if (!ch)
            return;
        layerbmp::IndexedBitmap *lay = ch->bitmap;
        const auto i = ch->idx;
        const auto v = ch->vals;
        d.edit(u"%1 follows the provinces (%2 px)"_s.arg(lay->filename).arg(i.size()), {}, [lay, i, v] { lay->apply(i, v); },
               {}, {}, {kind});
    } else if (what == u"islands_mark") {
        auto &isl = d.map<history::IslandFlags>(u"islands"_s);
        std::set<long long> ps;
        for (const Item *it : items)
            if (!it->pids.empty()) {
                const long long p = it->pids[0];
                const moddata::Province *pr = mod.provinces.get(p);
                if (pr && !pr->sea && d.hist->main_file(p))
                    ps.insert(p);
            }
        const QList<long long> pids(ps.begin(), ps.end());
        if (!pids.isEmpty())
            d.edit(u"Mark %1 province%2 as having islands"_s.arg(pids.size()).arg(pl(pids.size())), {},
                   [&isl, pids] { isl.set(pids, true); }, {}, {}, {u"islands"_s});
    } else if (what == u"islands_clear") {
        auto &isl = d.map<history::IslandFlags>(u"islands"_s);
        const bool everything = items.size() == c.items.size();
        std::set<long long> g;
        if (everything) {
            g.insert(isl.marked.begin(), isl.marked.end());
        } else {
            for (const Item *it : items)
                for (long long p : it->pids)
                    if (isl.marked.contains(p))
                        g.insert(p);
        }
        const QList<long long> gone(g.begin(), g.end());
        d.edit(u"Remove %1 island flag%2 (%3)"_s.arg(everything ? u"all"_s : u"the"_s, pl(gone.size())).arg(gone.size()),
               {}, [&isl, gone] { isl.set(gone, false); }, {}, {}, {u"islands"_s});
    } else if (what == u"nohist") {
        OrderedMap<long long, QString> plan;
        for (const Item *it : items)
            if (!it->pids.empty())
                plan.insert(it->pids[0], autofix::history_rel(mod, md, *d.hist, it->pids[0]));
        if (d.maps.contains(u"hist_new"_s) && !plan.isEmpty()) {
            auto &nh = d.map<history::NewHistFiles>(u"hist_new"_s);
            d.edit(u"Empty history files (%1)"_s.arg(plan.size()), {}, [&nh, plan] { nh.add_many(plan); }, {}, {},
                   {u"hist_new"_s});
        }
    } else if (what == u"states") {
        _fix_states(items);
    } else if (what == u"owners_top") {
        std::set<long long> ps;
        for (const Item *it : items)
            if (head_is(it->data, u"birth"_s))
                for (const Val &p : it->data[2].list())
                    ps.insert(I(p));
        const QList<long long> pids(ps.begin(), ps.end());
        if (d.maps.contains(u"owners_top"_s) && !pids.isEmpty()) {
            auto &top = d.map<history::OwnersOnTop>(u"owners_top"_s);
            d.edit(u"Owners at the top of %1 file%2"_s.arg(pids.size()).arg(pl(pids.size())), {},
                   [&top, pids] { top.add(pids); }, {}, {}, {u"owners_top"_s});
        }
    } else if (what == u"start_blocks") {
        std::set<long long> ps;
        std::set<QString> ts;
        for (const Item *it : items)
            if (head_is(it->data, u"startblock"_s)) {
                if (S(it->data[1]) == u"prov")
                    ps.insert(I(it->data[2]));
                else if (S(it->data[1]) == u"nation")
                    ts.insert(S(it->data[2]));
            }
        const QList<long long> pids(ps.begin(), ps.end());
        QStringList tags;
        for (const QString &t : ts)
            tags << t;
        if (d.maps.contains(u"start_blocks"_s) && (!pids.isEmpty() || !tags.isEmpty())) {
            auto &sb = d.map<history::StartBlocks>(u"start_blocks"_s);
            const long long k = pids.size() + tags.size();
            d.edit(u"Start-date blocks folded in (%1 file%2)"_s.arg(k).arg(pl(k)), {}, [&sb, pids, tags] { sb.add(pids, tags); },
                   {}, {}, {u"start_blocks"_s});
        }
    } else if (what == u"gone_state_refs") {
        auto &st = scripts();
        OrderedMap<QString, QString> texts;
        for (const Item *it : items) {
            const Val &x = it->data;
            if (!head_is(x, u"goneref"_s) || !(S(x[3]) == u"scope" || S(x[3]) == u"region") || it->pids.empty())
                continue;
            const QString rid = S(x[1]), rel = S(x[2]);
            const QString *had = texts.get(rel);
            auto [fresh, k] = staterefs::fixed_text(had ? *had : st.text(rel), rid, it->pids);
            if (k)
                texts.insert(rel, fresh);
        }
        if (!texts.isEmpty())
            d.edit(u"Scripts: a gone state's key → its provinces (%1 file%2)"_s.arg(texts.size()).arg(pl(texts.size())),
                   {}, [&st, texts] {
                       for (const auto &[k, v] : texts)
                           st.set_text(k, v);
                   }, {}, {}, {u"scripts"_s});
    } else if (what == u"pop_provinces") {
        QList<long long> pids;
        for (const Item *it : items)
            if (head_is(it->data, u"popprov"_s))
                pids << I(it->data[1]);
        if (d.maps.contains(u"drop_pop_blocks"_s) && !pids.isEmpty()) {
            auto &drop = d.map<document::DropPopBlocks>(u"drop_pop_blocks"_s);
            d.edit(u"Pops of %1 province%2 that don't exist: out on Apply"_s.arg(pids.size()).arg(pl(pids.size())), {},
                   [&drop, pids] {
                       for (long long p : pids)
                           drop.pids.insert(p);
                   }, {}, {}, {u"drop_pop_blocks"_s});
        }
    } else if (what == u"stray_mod") {
        // asks first (no pop-up): the first press says what will happen, a second one within 10 s does it
        const QString top = pypath::join(mod.root, u"mod"_s);
        const double now = double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
        if (now - _stray_armed > 10) {
            _stray_armed = now;
            _fix_note = u"Press “%1” again within 10 seconds: the folder %2 (%3 file(s)) moves out of the mod into the "
                        u"editor's backups — nothing is deleted."_s.arg(c.fix->label, top).arg(items.size());
            return;
        }
        _stray_armed = 0;
        const QString root = !backup_root.isEmpty() ? backup_root : pypath::join(mod.root, {u".."_s, u"backups"_s});
        const QString dst = pypath::join(root, {u"stray mod folder"_s,
                                         QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s)});
        QDir().mkpath(dst);
        if (!QDir().rename(top, pypath::join(dst, u"mod"_s)))
            throw std::runtime_error(u"can't move %1 to %2"_s.arg(top, dst).toStdString());
        _fix_note = u"The stray folder moved out of the mod — it is kept in %1."_s.arg(dst);
    } else if (what == u"empty_states") {
        QStringList rids;
        for (const Item *it : items)
            if (head_is(it->data, u"emptystate"_s))
                rids << S(it->data[1]);
        d.drop_empty_states(rids);
    } else if (what == u"utility") {
        QStringList rids;
        for (const Item *it : items)
            if (head_is(it->data, u"utility"_s) && mod.regions.contains(S(it->data[1])))
                rids << S(it->data[1]);
        if (d.maps.contains(u"utility"_s) && !rids.isEmpty()) {
            auto &um = d.map<document::UtilityMarks>(u"utility"_s);
            d.edit(u"Mark %1 as utility state%2"_s.arg(rids.mid(0, 3).join(u", "_s) + (rids.size() > 3 ? u" …"_s : u""_s),
                                                     pl(rids.size())),
                   {}, [&um, rids] { um.set(rids, true); }, {}, {}, {u"utility"_s});
        }
    } else if (what == u"nations") {
        auto &nations = d.map<countryhist::NationEdits>(u"nations"_s);
        std::vector<Val> todo;
        for (const Item *it : items)
            if (_fixable(c, *it))
                todo.push_back(it->data);
        if (!todo.empty())
            d.edit(u"Fix %1 nation problem%2"_s.arg(todo.size()).arg(pl(todo.size())), {}, [&nations, todo] {
                for (const Val &t : todo) {
                    const QString tag = S(t[1]), key = S(t[2]);
                    const Val val = t[3];
                    nations.edit(tag, [key, val](countryhist::NationState &st) {
                        if (key == u"upper_house") {
                            std::vector<std::pair<QString, double>> vals;
                            std::vector<double> xs;
                            for (const auto &[k, v] : st.upper_house) {
                                vals.emplace_back(k, v.toDouble());
                                xs.push_back(v.toDouble());
                            }
                            double tot = py::sum(xs);
                            if (tot == 0)
                                tot = 1.0;
                            OrderedMap<QString, long long> out;
                            for (const auto &[k, v] : vals)
                                out.insert(k, py::round_int(v * 100.0 / tot));
                            QString big;
                            long long bv = 0;
                            bool first = true;
                            long long s = 0;
                            for (const auto &[k, v] : out) {
                                s += v;
                                if (first || v > bv) {
                                    big = k;
                                    bv = v;
                                    first = false;
                                }
                            }
                            out[big] += 100 - s;
                            st.upper_house = {};
                            for (const auto &[k, v] : out)
                                st.upper_house.insert(k, QString::number(v));
                        } else if (key == u"culture") {
                            const QString v = S(val);
                            QStringList keep;
                            for (const QString &x : st.cultures)
                                if (x != v)
                                    keep << x;
                            st.cultures = keep;
                        } else {
                            st[key] = OS(val);
                        }
                    });
                }
            }, {}, {}, {u"nations"_s});
    } else if (what == u"oob") {
        std::vector<oob::Fix> todo;
        for (const Item *it : items) {
            if (!_fixable(c, *it))
                continue;
            const Val &x = it->data;
            oob::Fix f;
            f.what = S(x[0]);
            f.tag = S(x[1]);
            if (f.what == u"rel_unknown") {
                f.other = S(x[2]);
            } else if (f.what == u"over") {
                f.a = I(x[2]);
                for (const Val &m : x[3].list())
                    f.moves.emplace_back(I(m[0]), I(m[1]));
            } else {
                if (len(x) > 2)
                    f.a = I(x[2]);
                if (len(x) > 3)
                    f.b = I(x[3]);
            }
            todo.push_back(f);
        }
        if (!todo.empty()) {
            oob::Centers ctr;
            ctr.cx = [this](long long p) { return md.cx[size_t(p)]; };
            ctr.cy = [this](long long p) { return md.cy[size_t(p)]; };
            ctr.n = md.n;
            d.edit(u"Fix %1 army / navy problem%2"_s.arg(todo.size()).arg(pl(todo.size())), {}, oob::fix_units(d, ctr, todo),
                   {}, {}, {u"oob"_s});
        }
    } else if (what == u"wars") {
        auto &store = scripts();
        QStringList rels;
        for (const Item *it : items)
            if (it->data.truthy())
                rels << S(it->data[1]);
        d.edit(u"Take %1 war%2 out"_s.arg(rels.size()).arg(pl(rels.size())), {}, [this, &store, rels] {
            for (const QString &r : rels)
                entryview::drop_file(mod, store, r);
        }, {}, {}, {u"scripts"_s});
    } else if (what == u"make_modifiers") {
        auto &store = scripts();
        QStringList names;
        for (const Item *it : items)
            if (it->data.truthy() && !names.contains(S(it->data[3])))
                names << S(it->data[3]);
        const QString old = store.text(cbcheck::MAKE_REL);
        const QString fresh = cbcheck::make_modifiers(old, names);
        if (fresh != old)
            d.edit(u"Make %1 missing modifier%2 (%3%4)"_s.arg(names.size()).arg(pl(names.size()), names.mid(0, 4).join(u", "_s),
                                                             names.size() > 4 ? u" …"_s : u""_s),
                   {}, [&store, fresh] { store.set_text(cbcheck::MAKE_REL, fresh); }, {}, {}, {u"scripts"_s});
    } else if (what == u"fix_build_window") {        // C++ only (2026-11-16): the province window's rows
        const buildtypes::WindowFix f = buildtypes::window_fix(mod, d);
        if (!f.texts.isEmpty() || !f.files.isEmpty()) {
            auto &store = scripts();
            moreviews::AssetEdits *assets = d.maps.contains(u"assets"_s) ? &d.map<moreviews::AssetEdits>(u"assets"_s)
                                                                          : nullptr;
            d.edit(u"Province window: a row for every building"_s, {}, [&store, assets, f] {
                for (const auto &[r, t] : f.texts)
                    store.set_text(r, t);
                if (assets)
                    for (const auto &[k, a] : f.files)
                        assets->fresh.insert(k, a);
            }, {}, {}, {u"scripts"_s, u"assets"_s});
        }
    } else if (what == u"fix_equals" || what == u"close_brackets") {
        auto &store = scripts();
        const bool eq = what == u"fix_equals";
        std::map<QString, QString> fresh;              // (sorted, as the label's sorted(new))
        OrderedMap<QString, QString> order;            // (the dict's order, for set_text)
        for (const Item *it : items)
            if (head_is(it->data, eq ? u"equals"_s : u"close"_s)) {
                const QString rel = S(it->data[1]);
                const QString old = store.text(rel);
                const QString t = eq ? cbcheck::fix_equals(old) : cbcheck::close_brackets(old);
                if (t != old) {
                    fresh[rel] = t;
                    order.insert(rel, t);
                }
            }
        if (!fresh.empty()) {
            QStringList names;
            for (const auto &[r, _t] : fresh)
                names << last_part(r);
            d.edit((eq ? u"Make each doubled = one in %1"_s : u"Close the last block of %1"_s).arg(names.join(u", "_s)), {},
                   [&store, order] {
                       for (const auto &[r, t] : order)
                           store.set_text(r, t);
                   }, {}, {}, {u"scripts"_s});
        }
    } else if (what == u"cb_order") {
        auto &store = scripts();
        const QString old = store.text(cbcheck::REL);
        std::vector<std::pair<QString, mapcheck::OptStr>> todo;
        for (const Item *it : items)
            if (it->data.truthy())
                todo.emplace_back(S(it->data[1]), OS(it->data[2]));
        const QString fresh = cbcheck::fix_order(old, todo);
        if (fresh != old)
            d.edit(u"peace_order put right (%1)"_s.arg(items.size()), {}, [&store, fresh] { store.set_text(cbcheck::REL, fresh); },
                   {}, {}, {u"scripts"_s});
    } else if (what == u"popcap") {
        std::vector<std::tuple<QString, QList<long long>, double>> todo;
        for (const Item *it : items) {
            const Val &x = it->data;
            const QString t = S(x[1]), scope = S(x[2]);
            QList<long long> ps;
            if (scope == u"province")
                ps << I(x[3]);
            else if (const moddata::Region *r = mod.regions.get(S(x[3])))
                ps = QList<long long>(r->provinces.begin(), r->provinces.end());
            if (!ps.isEmpty())
                todo.emplace_back(t, ps, D(x[4]));
        }
        if (!todo.empty())
            d.lower_to_cap(todo);
    } else if (what == u"event_ids") {
        auto &store = scripts();
        std::vector<std::tuple<QString, qsizetype, QString>> places;
        for (const Item *it : items)
            if (it->data.truthy())
                places.emplace_back(S(it->data[1]), qsizetype(I(it->data[2])), S(it->data[3]));
        auto [texts, done] = eventtools::renumber(mod, store, places);
        if (!done.empty()) {
            QStringList ps;
            for (size_t i = 0; i < std::min<size_t>(done.size(), 4); ++i)
                ps << u"%1 → %2"_s.arg(std::get<0>(done[i]), std::get<1>(done[i]));
            const OrderedMap<QString, QString> tx = texts;
            d.edit(u"New id for %1 event%2 (%3)"_s.arg(done.size()).arg(pl(done.size()),
                                                                     ps.join(u", "_s) + (done.size() > 4 ? u" …"_s : u""_s)),
                   {}, [&store, tx] {
                       for (const auto &[r, t] : tx)
                           store.set_text(r, t);
                   }, {}, {}, {u"scripts"_s});
        }
    } else if (what == u"names") {
        std::vector<std::tuple<QString, QString, mapcheck::OptStr>> todo;
        for (const Item *it : items)
            if (it->data[2].truthy())
                todo.emplace_back(S(it->data[1]), S(it->data[2]), OS(it->data[3]));
        if (d.maps.contains(u"loc"_s) && !todo.empty()) {
            auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
            if (c.key == u"cb_texts" && loc_tab)     // (a war goal's texts count for the Scripts page)
                for (const auto &[k, _t, _sec] : todo)
                    (*loc_tab)[k] = u"events"_s;
            d.edit(u"Name %1 key%2"_s.arg(todo.size()).arg(pl(todo.size())), {}, [&loc, todo] {
                for (const auto &[k, t, sec] : todo)
                    loc.set(k, t, sec);
            }, {}, {}, {u"loc"_s});
        }
    } else if (what == u"zero_small") {
        std::vector<Piece> pieces;
        for (const Item *it : items)
            if (it->point)
                pieces.emplace_back(std::make_pair(
                    autofix::piece_pixels(md.idmap, 0, it->point->first, it->point->second), I(it->data[1])));
        const long long n = static_cast<long long>(pieces.size());
        _commit_pixels(u"Fill %1 small no-province area%2"_s.arg(n).arg(pl(n)), pieces,
                       u"Gave %1 small area%2 of no province to the province around them."_s.arg(n).arg(pl(n)));
    } else if (what == u"pos") {
        _fix_positions(c, items);
    } else if (what == u"colormap") {
        _fix_colormaps(items);
    } else if (what == u"adj_rows") {
        QList<long long> gone, zero;
        for (const Item *it : items) {
            if (S(it->data[4]) == u"delete")
                gone << I(it->data[1]);
            if (S(it->data[4]) == u"data0")
                zero << I(it->data[1]);
        }
        if (!gone.isEmpty() || !zero.isEmpty())
            d.adj_edit(u"Adjacencies: %1 row%2 fixed"_s.arg(gone.size() + zero.size()).arg(pl(gone.size() + zero.size())),
                       [gone, zero](adjacency::AdjacencyFile &adj) {
                           adj.del(gone);
                           for (long long u : zero)
                               adj.update(u, [](adjacency::Row &r) { r.data = 0; });
                       });
    } else if (what == u"adj_add_coast" || what == u"adj_add_bank") {
        const bool coast = what == u"adj_add_coast";
        const QString com = coast ? d.map<adjacency::AdjacencyFile>(u"adjacency"_s).coast_comment() : QString();
        std::vector<adjacency::RowFields> specs;
        for (const Item *it : items)
            specs.push_back({I(it->data[1]), I(it->data[2]), u"sea"_s, 0, 0, coast ? com : S(it->data[3])});
        d.adj_edit(u"Add %1 %2 crossing%3"_s.arg(specs.size()).arg(coast ? u"coast"_s : u"bank"_s, pl(specs.size())),
                   [specs](adjacency::AdjacencyFile &a) { a.add(specs); });
    } else if (what == u"adj_mark") {
        std::vector<std::pair<QString, QList<long long>>> rivers;
        for (const Item *it : items) {
            QList<long long> tiles;
            for (const Val &t : it->data[2].list())
                tiles << I(t);
            rivers.emplace_back(S(it->data[1]), tiles);
        }
        d.adj_edit(u"Mark %1 Meme river%2"_s.arg(rivers.size()).arg(pl(rivers.size())),
                   [rivers](adjacency::AdjacencyFile &adj) {
                       for (const auto &[name, tiles] : rivers)
                           adj.set_mark(name, tiles);
                   });
    }
}

void MapCheckPanel::_fix_states(const std::vector<const Item *> &items)
{
    // no state: the state most land neighbours are in. Several: keep the one most neighbours share. A sea tile /
    // unknown id in a state list: taken out.
    document::Document &d = *doc;
    OrderedMap<long long, QStringList> regs;
    for (const auto &[rid, r] : mod.regions)
        for (long long p : r.provinces)
            regs[p] << rid;
    struct Plan {
        long long p;
        QString join;
        QStringList leave;
    };
    std::vector<Plan> plan;
    // Counter.most_common(1): the highest count, the first counted of equal ones
    auto most_common = [](const std::vector<QString> &xs) {
        OrderedMap<QString, long long> votes;
        for (const QString &x : xs)
            votes[x] += 1;
        QString best;
        long long bv = 0;
        for (const auto &[k, v] : votes)
            if (v > bv) {
                best = k;
                bv = v;
            }
        return best;
    };
    for (const Item *it : items) {
        const QString kind = S(it->data[0]);
        const long long p = I(it->data[1]);
        std::vector<long long> nb;
        for (long long q : md.neighbours(p))
            if (const moddata::Province *pr = mod.provinces.get(q); pr && !pr->sea)
                nb.push_back(q);
        if (kind == u"none") {
            std::vector<QString> votes;
            for (long long q : nb)
                if (const QStringList *r = regs.get(q); r && r->size() == 1)
                    votes.push_back((*r)[0]);
            if (!votes.empty())
                plan.push_back({p, most_common(votes), {}});
        } else if (kind == u"multi") {
            QStringList cand;
            for (const QString &r : regs.value(p))
                if (!cand.contains(r))
                    cand << r;
            std::map<QString, long long> votes;
            for (long long q : nb)
                for (const QString &r : regs.value(q))
                    if (cand.contains(r))
                        votes[r] += 1;
            QString keep;
            std::optional<std::pair<long long, long long>> best;
            for (qsizetype i = 0; i < cand.size(); ++i) {
                const std::pair<long long, long long> k{votes[cand[i]], -i};
                if (!best || k > *best) {
                    best = k;
                    keep = cand[i];
                }
            }
            QStringList leave;
            for (const QString &r : cand)
                if (r != keep)
                    leave << r;
            plan.push_back({p, QString(), leave});
        } else if (kind == u"bad") {
            QStringList leave;
            for (const Val &r : it->data[2].list())
                leave << S(r);
            plan.push_back({p, QString(), leave});
        }
    }
    if (plan.empty())
        return;
    std::set<QString> rs;
    for (const Plan &x : plan) {
        if (!x.join.isEmpty() && mod.regions.contains(x.join))
            rs.insert(x.join);
        for (const QString &r : x.leave)
            if (mod.regions.contains(r))
                rs.insert(r);
    }
    QStringList rids;
    for (const QString &r : rs)
        rids << r;
    d.edit(u"Fix states (%1)"_s.arg(plan.size()), {}, [this, &d, plan] {
        for (const Plan &x : plan) {
            for (const QString &r : x.leave)
                if (moddata::Region *reg = mod.regions.get(r))
                    reg->provinces.erase(std::remove(reg->provinces.begin(), reg->provinces.end(), x.p),
                                         reg->provinces.end());
            if (!x.join.isEmpty())
                if (moddata::Region *reg = mod.regions.get(x.join))
                    if (std::find(reg->provinces.begin(), reg->provinces.end(), x.p) == reg->provinces.end())
                        reg->provinces.push_back(x.p);
        }
        d._sync_regions();
    }, rids);
}

void MapCheckPanel::_from_neighbours(const QString &kind, const std::vector<const Item *> &items)
{
    // provinces without a continent / climate get the one most of their land neighbours have; provinces in several
    // keep the one most neighbours share
    auto &mf = doc->map<mapfiles::MembershipFile>(kind);
    const auto mem = mf.membership();
    OrderedMap<long long, QString> todo;
    for (const Item *it : items)
        for (long long p : it->pids) {
            const QStringList cur = mem.value(p);
            if (cur.size() == 1)
                continue;
            OrderedMap<QString, long long> votes;
            for (long long q : md.neighbours(p)) {
                const moddata::Province *pr = mod.provinces.get(q);
                const QStringList *mq = mem.get(q);
                if (pr && !pr->sea && mq && mq->size() == 1 && (cur.isEmpty() || cur.contains((*mq)[0])))
                    votes[(*mq)[0]] += 1;
            }
            if (!votes.isEmpty()) {
                QString best;
                long long bv = 0;
                for (const auto &[k, v] : votes)
                    if (v > bv) {
                        best = k;
                        bv = v;
                    }
                todo.insert(p, best);
            } else if (!cur.isEmpty()) {
                todo.insert(p, cur[0]);
            }
        }
    if (todo.isEmpty()) {
        Q_EMIT message(u"None of them has a neighbour with a %1 to copy."_s.arg(kind));
        return;
    }
    QString cap = kind;
    if (!cap.isEmpty())
        cap = cap.left(1).toUpper() + cap.mid(1).toLower();
    doc->edit(u"%1 from neighbours (%2)"_s.arg(cap).arg(todo.size()), {}, [&mf, todo] {
        for (const auto &[p, name] : todo)
            mf.assign({p}, name);
    }, {}, {}, {kind});
}

// ---------------------------------------------------------------------------------------------- overlay
std::optional<QString> MapCheckPanel::current_key() const
{
    // category key of what is selected in the list (None if nothing)
    if (!_cur || !(0 <= _cur->first && _cur->first < int(cats.size())))
        return std::nullopt;
    return cats[size_t(_cur->first)].key;
}

std::vector<mapview::Shape> MapCheckPanel::_adj_shapes(int ci, const Category &c)
{
    std::vector<mapview::Shape> out;
    if (!adj_geo || !doc || !doc->maps.contains(u"adjacency"_s))
        return out;
    auto &adj = doc->map<adjacency::AdjacencyFile>(u"adjacency"_s);
    auto qp = [](const Pt &p) { return QPointF(p.first, p.second); };
    for (int ii = 0; ii < int(std::min<size_t>(c.items.size(), 1500)); ++ii) {
        const Val &d = c.items[size_t(ii)].data;
        const bool cur = _cur && _cur->first == ci && _cur->second == ii;
        const QVariantList tag = tag_of(ci, ii);
        try {
            if (head_is(d, u"row"_s)) {
                const adjacency::Row *r = adj.get(I(d[1]));
                if (const auto e = r ? ends(*adj_geo, adj, *r) : std::nullopt) {
                    auto s = shape(u"line"_s, {qp(e->first), qp(e->second)}, {255, 150, 40}, tag);
                    s.width = cur ? 5 : 4;
                    s.dash = true;
                    s.halo = cur;
                    out.push_back(s);
                }
            } else if (head_is(d, u"pair"_s)) {
                if (const auto ce = coast_ends(*adj_geo, I(d[1]), I(d[2]))) {
                    auto s = shape(u"line"_s, {qp(ce->first), qp(ce->second)}, {255, 70, 200}, tag);
                    s.width = cur ? 5 : 4.5;
                    s.halo = cur;
                    out.push_back(s);
                    auto dot = shape(u"dot"_s, {qp(ce->first)}, {255, 70, 200}, tag);
                    dot.size = 6;
                    out.push_back(dot);
                }
            } else if (head_is(d, u"bank"_s)) {
                if (const auto pe = pair_ends(*adj_geo, I(d[1]), I(d[2]))) {
                    auto s = shape(u"line"_s, {qp(pe->first), qp(pe->second)}, {255, 70, 200}, tag);
                    s.width = cur ? 5 : 4;
                    s.dash = true;
                    s.halo = cur;
                    out.push_back(s);
                }
            } else if (head_is(d, u"river"_s)) {
                for (const Val &tv : d[2].list()) {
                    const long long t = I(tv);
                    if (0 < t && t < md.n && md.area[size_t(t)]) {
                        auto s = shape(u"circle"_s, {QPointF(md.cx[size_t(t)] + 0.5, md.cy[size_t(t)] + 0.5)},
                                       {90, 130, 255}, tag);
                        s.radius = 6;
                        s.width = cur ? 3.5 : 2.5;
                        out.push_back(s);
                    }
                }
            }
        } catch (const std::exception &) {
            continue;
        }
    }
    return out;
}

std::vector<mapview::Shape> MapCheckPanel::shapes()
{
    std::vector<mapview::Shape> out;
    for (int ci = 0; ci < int(cats.size()); ++ci) {
        const Category &c = cats[size_t(ci)];
        const bool selected = _cur && _cur->first == ci;
        if (c.key.startsWith(u"adj_"_s)) {
            // adjacency problems: lines like in the Adjacencies panel
            if (show[u"adj"_s] || selected) {
                auto a = _adj_shapes(ci, c);
                out.insert(out.end(), a.begin(), a.end());
            }
            continue;
        }
        if (!show[u"markers"_s] && !selected)
            continue;
        if (c.severity == SEV_INFO && !selected)
            continue;
        const Rgb col = sev_color(c.severity);
        for (int ii = 0; ii < int(c.items.size()); ++ii) {
            const Item &it = c.items[size_t(ii)];
            if (!it.point)
                continue;
            const QPointF pt(it.point->first, it.point->second);
            const bool this_one = _cur && _cur->first == ci && _cur->second == ii;
            const auto ring_r = PIXEL_RING.find(c.key);
            if (ring_r != PIXEL_RING.end()) {
                // the broken pixels show on the bitmap's own map (red rivers / yellow terrain): rings around them keep
                // them visible — a cross would cover them. The selected one gets the big ring.
                const auto rr = RING_RGB.find(c.key);
                const Rgb ring = rr != RING_RGB.end() ? rr->second : Rgb{255, 230, 70};
                auto s = shape(u"circle"_s, {pt}, ring, tag_of(ci, ii));
                s.radius = this_one ? ring_r->second : ring_r->second * 0.7;
                s.width = this_one ? 3.5 : 2.5;
                out.push_back(s);
                continue;
            }
            const bool cur = this_one || (_cur && _cur->first == ci && !_cur->second);
            auto s = shape(u"x"_s, {pt}, col, tag_of(ci, ii));
            s.size = cur ? 7 : 5;
            s.width = 3;
            s.halo = this_one;
            out.push_back(s);
        }
    }
    return out;
}

QString MapCheckPanel::describe(const QVariantList &tag) const
{
    if (tag.size() < 3)
        return u""_s;
    const int ci = tag[1].toInt(), ii = tag[2].toInt();
    if (0 <= ci && ci < int(cats.size()) && 0 <= ii && ii < int(cats[size_t(ci)].items.size()))
        return cats[size_t(ci)].items[size_t(ii)].text;
    return u""_s;
}

void MapCheckPanel::map_clicked(const QVariantList &tag)
{
    // a marker / line clicked on the map: select that problem in the list (the camera stays)
    if (tag.size() >= 3)
        select_problem(tag[1].toInt(), tag[2].toInt());
}

}  // namespace checkpanel
