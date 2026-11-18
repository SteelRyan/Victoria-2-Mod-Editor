#include "ui/terrainpanel.h"

#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "map/layertools.h"
#include "map/mismatch.h"
#include "map/provbmp.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace terrainpanel {

const QStringList TYPE_ORDER = {u"plains"_s, u"farmlands"_s, u"steppe"_s, u"desert"_s, u"coastal_desert"_s,
                                u"forest"_s, u"woods"_s, u"jungle"_s, u"marsh"_s, u"hills"_s, u"mountain"_s,
                                u"arctic"_s};
const std::vector<std::pair<QString, QString>> STYLES = {
    {u"flat"_s, u"Flat"_s}, {u"patches"_s, u"Patches"_s}, {u"rings"_s, u"Rings (spiral)"_s}};
const Rgb MISMATCH_RGB{255, 255, 0};
const Rgb SEA_MISMATCH_RGB{255, 140, 0};

QString style_name(const QString &key)
{
    for (const auto &[k, t] : STYLES)
        if (k == key)
            return t;
    return QString();
}

QString type_label(const QString &t)
{
    const QString s = QString(t).replace(u'_', u' ');
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1).toLower();
}

static QString join(const std::vector<long long> &v, const QString &sep)
{
    QStringList out;
    for (long long j : v)
        out << QString::number(j);
    return out.join(sep);
}

static std::array<std::uint8_t, 3> px(const Rgb &c)
{
    return {std::uint8_t(std::get<0>(c)), std::uint8_t(std::get<1>(c)), std::uint8_t(std::get<2>(c))};
}

// ---------------------------------------------------------------------------------------------- TerrainView
TerrainView::TerrainView(const mapview::MapData &md_, const layerbmp::IndexedBitmap &tb_) : md(md_), tb(tb_)
{
    for (size_t i = 0; i < std::min<size_t>(256, tb.palette.size()); ++i)
        pal[i] = px(tb.palette[i]);
    pal[254] = px(mapview::SEA_RGB);
    bool any = false;
    for (const auto &[i, t] : layerbmp::terrain_types(tb.mod))
        if (t == u"ocean" && i < 256) {
            ocean[size_t(i)] = true;
            any = true;
        }
    if (!any)
        ocean[254] = true;
}

mapview::RgbImage TerrainView::colors(long long y0, long long y1, long long x0, long long x1)
{
    // (Python reads a 1 px margin around the window and crops it off again: only the window reaches the picture)
    const grid::Grid<std::uint8_t> &T = *tb.data;
    mapview::RgbImage rgb(y1 - y0, x1 - x0);
    grid::Grid<std::uint8_t> bad_sea(y1 - y0, x1 - x0, 0), bad_land(y1 - y0, x1 - x0, 0);
    // (C++ only, 2026-11-10, the performance round: row by row, the two colours taken once — the same picture)
    const auto SEA_BAD = px(SEA_MISMATCH_RGB), LAND_BAD = px(MISMATCH_RGB);
    const std::uint8_t *sea_of = md.sea.data();
    for (long long y = y0; y < y1; ++y) {
        const std::uint8_t *tr = T.row(y);
        const std::uint16_t *ir = md.idmap.row(y);
        auto *out = rgb.row(y - y0);
        std::uint8_t *bs = bad_sea.row(y - y0), *bl = bad_land.row(y - y0);
        for (long long x = x0; x < x1; ++x) {
            const std::uint8_t t = tr[x];
            const std::uint16_t id = ir[x];
            auto &c = out[x - x0];
            if (!id) {
                c = {0, 0, 0};
                continue;
            }
            c = pal[t];
            // the two kinds apart (the tester, 2026-10-15: "which mismatch is it?"): terrain on a sea tile orange, a
            // land province without terrain yellow — the legend in Drawing says which is which (mismatch.py)
            const bool sea = sea_of[id];
            if (sea && !ocean[t]) {
                bs[x - x0] = 1;
                c = SEA_BAD;                 // (no dark edge around them, the user 2026-10-16)
            } else if (!sea && ocean[t]) {
                bl[x - x0] = 1;
                c = LAND_BAD;
            }
        }
    }
    _last = Last{{y0, y1, x0, x1}, std::move(bad_sea), std::move(bad_land)};
    return rgb;
}

std::optional<std::vector<mapview::PixelLayer::Top>> TerrainView::on_top(long long y0, long long y1, long long x0,
                                                                        long long x1)
{
    // The mismatched pixels again after the map shaded its borders — so they stay bright yellow on coasts.
    if (!_last || _last->box != std::array<long long, 4>{y0, y1, x0, x1})
        colors(y0, y1, x0, x1);
    std::vector<Top> out;
    for (const auto &[m, c] : {std::pair{&_last->bad_sea, SEA_MISMATCH_RGB}, std::pair{&_last->bad_land, MISMATCH_RGB}})
        if (std::any_of(m->flat().begin(), m->flat().end(), [](std::uint8_t v) { return v != 0; }))
            out.push_back(Top{*m, c, {}});
    if (out.empty())
        return std::nullopt;
    return out;
}

// ---------------------------------------------------------------------------------------------- Swatch
Swatch::Swatch(long long idx_, const Rgb &rgb_, QWidget *parent)
    : QPushButton(QString::number(idx_), parent), idx(idx_), rgb(rgb_)
{
    setFixedSize(34, 28);
    setCursor(Qt::PointingHandCursor);
    set_state(false, true);
}

void Swatch::set_state(bool selected, bool used)
{
    const auto [r, g, b] = rgb;
    const QString ink = theme::ink_on(r, g, b);
    const QString border = selected ? u"3px solid %1"_s.arg(theme::GOLD)
                                    : (used ? u"1px solid %1"_s.arg(theme::LINE) : u"1px dashed %1"_s.arg(theme::INK_FAINT));
    setStyleSheet(u"QPushButton { background: rgb(%1,%2,%3); color: %4; border: %5; border-radius: 3px; "
                  u"padding: 0; font-size: 10px; font-weight: %6; }"_s
                      .arg(r).arg(g).arg(b)
                      .arg(ink, border, selected ? u"700"_s : u"400"_s));
}

// ---------------------------------------------------------------------------------------------- TerrainBox
TerrainBox::TerrainBox(moddata::Mod &mod_, const mapview::MapData &md_, drawlink::Panel *panel_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_), panel(panel_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Terrain colours — by type and level"_s));
    pal_host = new QWidget();
    pal_grid = new QGridLayout(pal_host);
    pal_grid->setContentsMargins(0, 0, 0, 0);
    pal_grid->setHorizontalSpacing(8);
    pal_grid->setVerticalSpacing(5);
    s->lay->addWidget(pal_host);
    pick_info = widgets::label(QString(), u"sub"_s);
    pick_info->setTextFormat(Qt::RichText);
    s->lay->addWidget(pick_info);
    lay->addWidget(s);

    s = new widgets::Section();
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(widgets::label(u"Tool"_s, u"sub"_s, false), 0, 0);
    const std::vector<std::pair<QString, QString>> tools = {{u"brush"_s, u"Brush"_s}, {u"fill"_s, u"Fill"_s}};
    for (const auto &[k, t] : tools)
        tool_keys << k;
    tool_btns = _seg(grid, 0, tools);
    grid->addWidget(widgets::label(u"Style"_s, u"sub"_s, false), 1, 0);
    style_btns = _seg(grid, 1, STYLES);
    // rings need a whole area: picking them with the brush switches to Fill (and the brush back to Flat)
    style_btns[u"rings"_s]->setToolTip(u"Rings run from the edge of an area to its centre, so they work with "
                                       u"Fill — picking Rings switches the tool to Fill"_s);
    connect(style_btns[u"rings"_s], &QPushButton::toggled, this, &TerrainBox::_rings_toggled);
    connect(tool_btns[u"brush"_s], &QPushButton::toggled, this, &TerrainBox::_brush_toggled);
    levels_lbl = widgets::label(u"Levels"_s, u"sub"_s, false);
    grid->addWidget(levels_lbl, 2, 0, Qt::AlignTop);
    levels_host = new QWidget();
    levels_row = new widgets::WrapRow(levels_host, 4, 8);   // 8 per row, like the palette
    grid->addWidget(levels_host, 2, 1);
    order_lbl = widgets::label(QString(), u"faint"_s);
    grid->addWidget(order_lbl, 3, 1);
    ring_lbl = widgets::label(u"Ring width"_s, u"sub"_s, false);
    grid->addWidget(ring_lbl, 4, 0);
    // ring width: Auto (the levels spread to the centre of the area) or a fixed width per ring
    ring_host = new QWidget();
    auto *rrow = new QHBoxLayout(ring_host);
    rrow->setContentsMargins(0, 0, 0, 0);
    rrow->setSpacing(8);
    ring_auto = new QPushButton(u"Auto"_s);
    ring_auto->setObjectName(u"seg"_s);
    ring_auto->setCheckable(true);
    ring_auto->setChecked(true);
    ring_auto->setFixedWidth(64);
    ring_auto->setToolTip(u"Auto: the rings share the area evenly, the last level reaches its centre. Off: "
                          u"every ring is as wide as the slider says (the centre keeps the last level)."_s);
    connect(ring_auto, &QPushButton::toggled, this, [this](bool) { _options_changed(); });
    ring_w = new widgets::SliderField(1, 20, 200, 5, u"px"_s);
    ring_w->setToolTip(u"Width of every ring in pixels — moving it switches Auto off. Type up to 200 px."_s);
    connect(ring_w, &widgets::SliderField::valueChanged, this, [this](double) { ring_auto->setChecked(false); });
    _ring_fade = new QGraphicsOpacityEffect(ring_w);
    ring_w->setGraphicsEffect(_ring_fade);
    rrow->addWidget(ring_auto);
    rrow->addWidget(ring_w, 1);
    grid->addWidget(ring_host, 4, 1);
    reverse = new QCheckBox(u"Reverse: highest level at the edge"_s);
    grid->addWidget(reverse, 5, 1);
    // patches: how much of the area the other levels cover, and how big their blotches are
    dens_lbl = widgets::label(u"Density"_s, u"sub"_s, false);
    grid->addWidget(dens_lbl, 6, 0);
    density = new widgets::SliderField(0, 100, 100, 45, u"%"_s);
    density->setToolTip(u"How much of the area the other chosen levels cover, in patches on the colour you "
                        u"paint with (0% = only that colour)"_s);
    connect(density, &widgets::SliderField::valueChanged, this, [this](double) { _patch_preview(); });
    grid->addWidget(density, 6, 1);
    psize_lbl = widgets::label(u"Patch size"_s, u"sub"_s, false);
    grid->addWidget(psize_lbl, 7, 0);
    psize = new widgets::SliderField(2, 40, 200, 9, u"px"_s, true);
    psize->setToolTip(u"How big the patches are"_s);
    connect(psize, &widgets::SliderField::valueChanged, this, [this](double) { _patch_preview(); });
    grid->addWidget(psize, 7, 1);
    grad_lbl = widgets::label(u"Gradient"_s, u"sub"_s, false);
    grid->addWidget(grad_lbl, 8, 0);
    gradient = new widgets::SliderField(0, 100, 100, 50, u"%"_s);
    gradient->setToolTip(u"How wide the in-between levels are around each patch: low = a steep step from the "
                         u"colour you paint with straight to the top level, high = wide bands of the levels "
                         u"in between (needs 3 or more levels)"_s);
    connect(gradient, &widgets::SliderField::valueChanged, this, [this](double) { _options_changed(); });
    grid->addWidget(gradient, 8, 1);
    preview_lbl = new QLabel();
    preview_lbl->setToolTip(u"Preview of the patches (map pixels shown 2× larger)"_s);
    grid->addWidget(preview_lbl, 9, 1);
    grid->addWidget(widgets::label(u"Brush size"_s, u"sub"_s, false), 10, 0);
    size_spin = new widgets::SliderField(1, 80, 400, 8, u"px"_s, true);
    size_spin->setToolTip(u"Brush size — drag, or type any size (up to 400 px). Shift+wheel on the map too."_s);
    connect(size_spin, &widgets::SliderField::valueChanged, this, [this](double) { handler.preview(); });
    grid->addWidget(size_spin, 10, 1);
    grid->addWidget(widgets::label(u"Paint over"_s, u"sub"_s, false), 11, 0);
    scope = new QComboBox();
    scope->addItem(u"All land"_s, u"land"_s);
    scope->addItem(u"Only the selected provinces"_s, u"selected"_s);
    // the tester (2026-10-15): terrain on a sea tile could not be taken away — this paints only where terrain.bmp
    // and the provinces disagree, and makes each pixel right: a sea tile's gets ocean, a land province's this colour
    scope->addItem(u"Only where the provinces disagree (each pixel made right)"_s, u"mismatch"_s);
    grid->addWidget(scope, 11, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    fill_sel_btn = new QPushButton(u"Fill the selected provinces"_s);
    fill_sel_btn->setObjectName(u"primary"_s);
    connect(fill_sel_btn, &QPushButton::clicked, this, [this] { fill_selected(); });
    s->lay->addWidget(fill_sel_btn);
    fill_note = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(fill_note);
    status = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(status);
    lay->addWidget(s);
    _options_changed();
}

// ------------------------------------------------------------------ helpers
std::map<QString, QPushButton *> TerrainBox::_seg(QGridLayout *grid, int row,
                                                  const std::vector<std::pair<QString, QString>> &items)
{
    auto *box = new QHBoxLayout();
    box->setSpacing(0);
    auto *group = new QButtonGroup(this);
    std::map<QString, QPushButton *> btns;
    for (const auto &[k, t] : items) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(k == items[0].first);
        connect(b, &QPushButton::toggled, this, [this](bool) { _options_changed(); });
        group->addButton(b);
        btns[k] = b;
        box->addWidget(b, 1);
    }
    grid->addLayout(box, row, 1);
    return btns;
}

void TerrainBox::_rings_toggled(bool on)
{
    if (on && tool() == u"brush")
        tool_btns[u"fill"_s]->setChecked(true);
}

void TerrainBox::_brush_toggled(bool on)
{
    if (on && style_btns[u"rings"_s]->isChecked())
        style_btns[u"flat"_s]->setChecked(true);
}

std::optional<long long> TerrainBox::ring_width() const
{
    // Pixels per ring, or None = Auto (spread the levels to the centre).
    if (ring_auto->isChecked())
        return std::nullopt;
    return (long long)ring_w->value();
}

layerbmp::IndexedBitmap &TerrainBox::tb() const
{
    return doc->map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s);
}

QString TerrainBox::tool() const
{
    for (const QString &k : tool_keys)
        if (tool_btns.at(k)->isChecked())
            return k;
    return u"brush"_s;
}

QString TerrainBox::style_for_fill() const
{
    for (const auto &[k, t] : STYLES)
        if (style_btns.at(k)->isChecked())
            return k;
    return u"flat"_s;
}

QString TerrainBox::style() const
{
    const QString st = style_for_fill();
    return st == u"rings" && tool() == u"brush" ? u"flat"_s : st;
}

QString TerrainBox::type_of(long long i) const
{
    return types.value(i, u"?"_s);
}

std::vector<long long> TerrainBox::same_type(long long i) const
{
    const QString t = type_of(i);
    std::vector<long long> out;
    for (const auto &[j, tt] : types)
        if (tt == t && j < (long long)tb().palette.size())
            out.push_back(j);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<long long> TerrainBox::chosen_levels() const
{
    std::vector<long long> lv;
    for (const auto &[i, b] : level_btns)
        if (b->isChecked())
            lv.push_back(i);
    if (reverse->isChecked())
        std::reverse(lv.begin(), lv.end());
    return lv;
}

static std::vector<std::int64_t> bincount(const grid::Grid<std::uint8_t> &g)
{
    std::vector<std::int64_t> c(256, 0);
    for (std::uint8_t v : g.flat())
        ++c[v];
    return c;
}

// ------------------------------------------------------------------ palette
void TerrainBox::_build_palette()
{
    layerbmp::IndexedBitmap &t_b = tb();
    if (!t_b.load()) {
        pick_info->setText(u"<span style='color:%1'>⚠ %2</span>"_s.arg(theme::ERROR_, t_b.error.value_or(u"None"_s)));
        return;
    }
    types = layerbmp::terrain_types(mod);
    counts = bincount(*t_b.data);
    while (pal_grid->count()) {
        QLayoutItem *it = pal_grid->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    swatches.clear();
    std::vector<std::pair<QString, std::vector<long long>>> rows;
    QStringList present;
    for (const QString &t : TYPE_ORDER)
        for (const auto &[i, tt] : types)
            if (tt == t) {
                present << t;
                break;
            }
    std::set<QString> others;
    for (const auto &[i, tt] : types)
        if (!TYPE_ORDER.contains(tt) && tt != u"ocean")
            others.insert(tt);
    for (const QString &t : others)
        present << t;
    const long long npal = (long long)t_b.palette.size();
    for (const QString &t : present) {
        std::vector<long long> idxs;
        for (const auto &[i, tt] : types)
            if (tt == t && i < npal)
                idxs.push_back(i);
        std::sort(idxs.begin(), idxs.end());
        std::vector<long long> main, extra;
        for (long long i : idxs)
            if (i < 64)
                main.push_back(i);
            else if (counts[size_t(i)] > 0)      // the long 64–253 range: only used ones
                extra.push_back(i);
        for (size_t k = 0; k < main.size(); k += 8)
            rows.emplace_back(k == 0 ? type_label(t) : QString(),
                              std::vector<long long>(main.begin() + qsizetype(k),
                                                     main.begin() + qsizetype(std::min(main.size(), k + 8))));
        if (!extra.empty())
            rows.emplace_back(u"%1 (more)"_s.arg(type_label(t)),
                              std::vector<long long>(extra.begin(), extra.begin() + qsizetype(std::min<size_t>(8, extra.size()))));
    }
    for (int r = 0; r < int(rows.size()); ++r) {
        const auto &[name, idxs] = rows[size_t(r)];
        pal_grid->addWidget(widgets::label(name, u"sub"_s, false), r, 0);
        auto *row = new QHBoxLayout();
        row->setSpacing(4);
        for (long long i : idxs) {
            auto *sw = new Swatch(i, t_b.palette[size_t(i)]);
            connect(sw, &QPushButton::clicked, this, [this, i] { set_index(i); });
            std::optional<long long> lvl;
            if (i < 64) {
                std::vector<long long> same;
                for (long long j : same_type(i))
                    if (j < 64 || counts[size_t(j)])
                        same.push_back(j);
                lvl = (long long)(std::find(same.begin(), same.end(), i) - same.begin()) + 1;
            }
            sw->setToolTip(u"index %1 · %2%3 · %4 px on the map"_s.arg(i).arg(
                type_label(type_of(i)), lvl ? u" level %1"_s.arg(*lvl) : QString(), widgets::fmt(counts[size_t(i)])));
            swatches.insert(i, sw);
            row->addWidget(sw);
        }
        row->addStretch(1);
        auto *host = new QWidget();
        host->setLayout(row);
        row->setContentsMargins(0, 0, 0, 0);
        pal_grid->addWidget(host, r, 1);
    }
    pal_grid->setColumnStretch(1, 1);
    _built_for = &t_b;
    if (!index || !swatches.contains(*index)) {
        std::optional<long long> best;
        for (const auto &[i, sw] : swatches)
            if (counts[size_t(i)] && (!best || counts[size_t(i)] > counts[size_t(*best)]))
                best = i;
        set_index(best ? *best : (swatches.isEmpty() ? 0 : swatches.begin()->first));
    } else {
        set_index(*index);
    }
}

void TerrainBox::set_index(long long i)
{
    if (!swatches.contains(i))
        return;
    const std::optional<QString> old_type = index ? std::optional<QString>(type_of(*index)) : std::nullopt;
    index = i;
    for (const auto &[j, sw] : swatches)
        sw->set_state(j == i, counts[size_t(j)] > 0);
    const QString t = type_of(i);
    std::vector<long long> same;
    for (long long j : same_type(i))
        if (j < 64 || counts[size_t(j)])
            same.push_back(j);
    const auto at = std::find(same.begin(), same.end(), i);
    pick_info->setText(u"Painting with <b>%1</b> · %2, level %3 of %4 · %5 px on the map%6"_s.arg(i).arg(
        type_label(t), QString::number(at != same.end() ? (at - same.begin()) + 1 : 0), QString::number(same.size()),
        widgets::fmt(counts[size_t(i)]),
        !counts[size_t(i)] ? u" (right-click the map to pick a colour)"_s : QString()));
    if (!old_type || t != *old_type || level_btns.empty())
        _fill_levels();
    _options_changed();
}

void TerrainBox::_fill_levels()
{
    for (const auto &[j, b] : level_btns) {
        b->hide();
        b->setParent(nullptr);
        b->deleteLater();
    }
    level_btns.clear();
    while (levels_row->count())
        delete levels_row->takeAt(0);
    levels_row->invalidate();
    std::vector<long long> same, used;
    for (long long j : same_type(*index))
        if (j < 64 || counts[size_t(j)])
            same.push_back(j);
    for (long long j : same)
        if (counts[size_t(j)] > 0)
            used.push_back(j);
    for (long long j : same) {
        auto *b = new QPushButton(QString::number(j));
        b->setCheckable(true);
        b->setChecked(used.empty() ? true : std::find(used.begin(), used.end(), j) != used.end());
        const auto [r, g, bb] = tb().palette[size_t(j)];
        b->setFixedSize(34, 26);
        b->setToolTip(u"Use level %1 in rings / patches (%2 px on the map)"_s.arg(j).arg(widgets::fmt(counts[size_t(j)])));
        b->setStyleSheet(u"QPushButton { background: rgb(%1,%2,%3); color: %4; border: 1px solid %5; border-radius: 3px;"
                         u" padding: 0; font-size: 10px; } QPushButton:checked { border: 3px solid %6; font-weight: 700; }"_s
                             .arg(r).arg(g).arg(bb)
                             .arg((r * 299 + g * 587 + bb * 114) / 1000.0 > 140 ? u"#111"_s : u"#f4f4f4"_s, theme::LINE,
                                  theme::GOLD));
        connect(b, &QPushButton::toggled, this, [this](bool) { _options_changed(); });
        level_btns[j] = b;
        levels_row->addWidget(b);
    }
    levels_host->updateGeometry();
}

void TerrainBox::_options_changed()
{
    if (!fill_sel_btn)
        return;
    const QString st = style();
    const bool multi = st == u"rings" || st == u"patches";
    for (QWidget *w : {(QWidget *)levels_lbl, levels_host, (QWidget *)order_lbl})
        w->setVisible(multi);
    for (QWidget *w : {(QWidget *)ring_lbl, ring_host, (QWidget *)reverse})
        w->setVisible(st == u"rings");
    _ring_fade->setOpacity(ring_auto->isChecked() ? 0.4 : 1.0);
    for (QWidget *w : {(QWidget *)dens_lbl, (QWidget *)density, (QWidget *)psize_lbl, (QWidget *)psize,
                       (QWidget *)grad_lbl, (QWidget *)gradient, (QWidget *)preview_lbl})
        w->setVisible(st == u"patches");
    const std::vector<long long> lv = chosen_levels();
    if (st == u"rings") {
        order_lbl->setText(!lv.empty() ? u"Edge → centre: %1"_s.arg(join(lv, u" → "_s)) : u"Pick at least one level"_s);
    } else if (st == u"patches") {
        const auto [base, acc] = _patch_order();
        if (acc.size() >= 2)
            order_lbl->setText(u"Patches of %1 on %2, with %3 in between"_s.arg(acc.back()).arg(base).arg(
                join(std::vector<long long>(acc.begin(), acc.end() - 1), u", "_s)));
        else
            order_lbl->setText(u"Patches of %1 on %2"_s.arg(acc.empty() ? u"—"_s : QString::number(acc[0])).arg(base));
        gradient->setEnabled(acc.size() >= 2);
        grad_lbl->setEnabled(acc.size() >= 2);
        _patch_preview();
    }
    const size_t sel = panel ? panel->sel.size() : 0;
    fill_sel_btn->setText(sel ? u"Fill the %1 selected province%2 (%3)"_s.arg(sel).arg(sel == 1 ? u""_s : u"s"_s,
                                                                                      style_name(style_for_fill()))
                              : u"Fill the selected provinces (select some first)"_s);
    fill_sel_btn->setEnabled(sel && index.has_value());
    fill_note->setText(style_for_fill() == u"rings" ? u"Rings run across all selected provinces together: the edge of "
                                                      u"the whole selection is the outer ring."_s
                                                    : QString());
    fill_note->setVisible(style_for_fill() == u"rings");
}

// ------------------------------------------------------------------ refresh
void TerrainBox::refresh(document::Document *doc_)
{
    doc = doc_;
    if (_built_for != &tb() || swatches.isEmpty())
        _build_palette();
    const layerbmp::IndexedBitmap &t_b = tb();
    if (t_b.data) {
        long long n = 0;
        for (long long k = 0; k < t_b.version; ++k)
            n += (long long)t_b.diffs[size_t(k)].idx.size();
        status->setText(t_b.version ? u"%1 change%2 · %3 px — written to terrain.bmp on Apply"_s.arg(t_b.version).arg(
                                          t_b.version == 1 ? u""_s : u"s"_s, widgets::fmt(n))
                                    : u"No changes to terrain.bmp yet."_s);
    }
    _options_changed();
}

void TerrainBox::recount()
{
    if (doc && tb().data) {
        counts = bincount(*tb().data);
        if (index && swatches.contains(*index))
            for (const auto &[j, sw] : swatches)
                sw->set_state(j == *index, counts[size_t(j)] > 0);
    }
}

// ------------------------------------------------------------------ painting
Vals TerrainBox::allowed_mask(const std::vector<std::uint16_t> &ids, const Flat *idx)
{
    // Land pixels (never sea / no province), and only the selected provinces if asked — or, "where the provinces
    // disagree", only the mismatched pixels (idx: their flat indexes), sea tiles too.
    const QString sc = scope->currentData().toString();
    if (sc == u"mismatch") {
        mismatch::Mismatch *mm = panel->mismatch();
        if (!mm || !idx)
            return Vals(ids.size(), 0);
        const mismatch::Masks m = mm->at(*idx);
        Vals ok(idx->size(), 0);
        for (const QString &k : {u"t_sea"_s, u"t_land"_s})
            if (const auto *g = m.get(k))
                for (size_t i = 0; i < ok.size(); ++i)
                    ok[i] |= g->flat()[i];
        return ok;
    }
    Vals ok(ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
        ok[i] = ids[i] != 0 && !md.sea[ids[i]];
    if (sc == u"selected") {
        std::vector<std::uint8_t> lut(static_cast<size_t>(md.n), 0);
        for (long long p : panel->sel)
            if (0 < p && p < md.n)
                lut[size_t(p)] = 1;
        for (size_t i = 0; i < ids.size(); ++i)
            ok[i] = ok[i] && lut[ids[i]];
    }
    return ok;
}

Vals TerrainBox::values_for(const Flat &idx, const QString &style, const grid::Grid<std::uint8_t> *region_mask,
                            const std::array<long long, 4> *box)
{
    // New palette index for pixels idx (flat) in the given style (region_mask/box: the area, for rings).
    if (style == u"flat" || !index)
        return Vals(idx.size(), std::uint8_t(index.value_or(0)));
    std::vector<long long> lv = chosen_levels();
    if (lv.empty())
        lv = {*index};
    Flat ys(idx.size()), xs(idx.size());
    for (size_t i = 0; i < idx.size(); ++i) {
        ys[i] = idx[i] / md.w;
        xs[i] = idx[i] % md.w;
    }
    Vals out(idx.size());
    if (style == u"patches") {
        const auto [order, w] = patch_levels();
        const auto k = layertools::weighted_levels(layertools::patch_noise(ys, xs, 11, psize->value()), w);
        for (size_t i = 0; i < idx.size(); ++i)
            out[i] = std::uint8_t(order[size_t(std::min<std::int64_t>(k[i], (std::int64_t)order.size() - 1))]);
        return out;
    }
    // rings: depth inside the area, levels from its edge to its centre
    const long long x0 = (*box)[0], y0 = (*box)[1];
    const grid::Grid<std::int32_t> d = layertools::depth(*region_mask);
    const long long dh = d.h(), dw = d.w();
    Flat gy(static_cast<size_t>(dh * dw)), gx(static_cast<size_t>(dh * dw));
    for (long long y = 0; y < dh; ++y)
        for (long long x = 0; x < dw; ++x) {
            gy[size_t(y * dw + x)] = y0 + y;
            gx[size_t(y * dw + x)] = x0 + x;
        }
    const std::vector<float> wob = layertools::patch_noise(gy, gx, 5, 14.0);
    const auto lvl = layertools::ring_levels(d.flat(), (long long)lv.size(), ring_width(), &wob);
    for (size_t i = 0; i < idx.size(); ++i)
        out[i] = std::uint8_t(lv[size_t(lvl[size_t((ys[i] - y0) * dw + (xs[i] - x0))])]);
    return out;
}

std::pair<long long, std::vector<long long>> TerrainBox::_patch_order() const
{
    // (background, other levels from the nearest to the farthest level from it): the last one is the top of the
    // patches, the ones before it lie around it as the gradient.
    std::vector<long long> lv = chosen_levels();
    if (lv.empty())
        lv = {*index};
    std::sort(lv.begin(), lv.end());
    const long long base = std::find(lv.begin(), lv.end(), *index) != lv.end() ? *index : lv[0];
    auto at = [&](long long j) { return (long long)(std::find(lv.begin(), lv.end(), j) - lv.begin()); };
    const long long pos = at(base);
    std::vector<long long> acc;
    for (long long j : lv)
        if (j != base)
            acc.push_back(j);
    std::stable_sort(acc.begin(), acc.end(), [&](long long a, long long b) {
        return std::pair(std::llabs(at(a) - pos), at(a)) < std::pair(std::llabs(at(b) - pos), at(b));
    });
    return {base, acc};
}

std::pair<std::vector<long long>, std::vector<double>> TerrainBox::patch_levels() const
{
    // (levels, weights) for patches: the colour painted with is the background; the other chosen levels cover
    // `density` % of the area. Of that, `gradient` % goes to the in-between levels (equal bands around each patch)
    // and the rest to the top level in the middle of the patches.
    const auto [base, acc] = _patch_order();
    const double d = density->value() / 100.0;
    if (acc.empty() || d <= 0)
        return {{base}, {1.0}};
    std::vector<long long> order{base};
    order.insert(order.end(), acc.begin(), acc.end());
    if (acc.size() == 1)
        return {order, {std::max(0.0, 1.0 - d), d}};
    const double g = gradient->value() / 100.0;
    std::vector<double> w{std::max(0.0, 1.0 - d)};
    for (size_t k = 0; k + 1 < acc.size(); ++k)
        w.push_back(d * g / double(acc.size() - 1));
    w.push_back(d * (1.0 - g));
    return {order, w};
}

void TerrainBox::_patch_preview()
{
    if (!doc || !tb().data || !index)
        return;
    const auto [order, w] = patch_levels();
    const long long H = 30, W = 130;                    // map pixels, shown twice as large
    Flat ys, xs;
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x) {
            ys.push_back(y);
            xs.push_back(x);
        }
    const auto k = layertools::weighted_levels(layertools::patch_noise(ys, xs, 11, psize->value()), w);
    QImage qi(int(W * 2), int(H * 2), QImage::Format_RGB888);
    for (long long y = 0; y < H * 2; ++y) {
        uchar *line = qi.scanLine(int(y));
        for (long long x = 0; x < W * 2; ++x) {
            const long long j = order[size_t(std::min<std::int64_t>(k[size_t((y / 2) * W + x / 2)],
                                                                     (std::int64_t)order.size() - 1))];
            const auto [r, g, b] = tb().palette[size_t(j)];
            line[x * 3] = uchar(r);
            line[x * 3 + 1] = uchar(g);
            line[x * 3 + 2] = uchar(b);
        }
    }
    preview_lbl->setPixmap(QPixmap::fromImage(qi));
}

void TerrainBox::commit(const QString &label_text, Flat idx, Vals nw, QString msg)
{
    layerbmp::IndexedBitmap &t_b = tb();
    {
        Flat ki;
        Vals kv;
        const auto &flat = t_b.data->flat();
        for (size_t i = 0; i < idx.size(); ++i)
            if (flat[size_t(idx[i])] != nw[i]) {
                ki.push_back(idx[i]);
                kv.push_back(nw[i]);
            }
        idx = std::move(ki);
        nw = std::move(kv);
    }
    if (idx.empty()) {
        Q_EMIT message(u"Nothing changed — those pixels already have that terrain."_s);
        return;
    }
    const auto before = _majority(idx);
    doc->edit(label_text, {}, [&] { t_b.apply(idx, nw); }, {}, {}, {u"terrain_bmp"_s});
    const auto after = _majority(idx);
    std::vector<std::tuple<long long, QString, QString>> moved;
    for (const auto &[p, b] : after) {
        const auto it = before.find(p);
        if (it == before.end() || it->second != b)
            moved.emplace_back(p, it == before.end() ? u"None"_s : it->second, b);
    }
    if (!moved.empty()) {
        QStringList parts;
        for (size_t k = 0; k < std::min<size_t>(4, moved.size()); ++k) {
            const auto &[p, a, b] = moved[k];
            parts << u"#%1 %2 %3 → %4"_s.arg(p).arg(mod.provinces.get(p)->name, a, b);
        }
        msg += u" Terrain type of %1: %2."_s.arg(moved.size() == 1 ? u"province"_s : u"%1 provinces"_s.arg(moved.size()),
                                                  parts.join(u", "_s) + (moved.size() > 4 ? u" …"_s : QString()));
    }
    recount();
    Q_EMIT message(msg + u" Ctrl+Z undoes it."_s);
}

std::map<long long, QString> TerrainBox::_majority(const Flat &idx) const
{
    // {province: majority terrain type} of the provinces the pixels belong to (the game uses it unless the province
    // history sets terrain = …).
    const layerbmp::IndexedBitmap &t_b = tb();
    std::set<QString> tset;
    for (const auto &[i, t] : types)
        tset.insert(t);
    const std::vector<QString> tnames(tset.begin(), tset.end());
    std::array<long long, 256> lut;
    lut.fill(-1);
    for (const auto &[i, t] : types)
        if (i < 256)
            lut[size_t(i)] = std::find(tnames.begin(), tnames.end(), t) - tnames.begin();
    std::set<long long> ps;
    for (std::int64_t f : idx)
        ps.insert(md.idmap.flat()[size_t(f)]);
    std::map<long long, QString> out;
    for (long long p : ps) {
        if (p == 0 || md.sea[size_t(p)] || md.x1[size_t(p)] < 0)
            continue;
        std::vector<std::int64_t> bc(tnames.size(), 0);
        bool any = false;
        for (long long y = md.y0[size_t(p)]; y <= md.y1[size_t(p)]; ++y)
            for (long long x = md.x0[size_t(p)]; x <= md.x1[size_t(p)]; ++x)
                if (md.idmap(y, x) == p) {
                    const long long v = lut[(*t_b.data)(y, x)];
                    if (v >= 0) {
                        ++bc[size_t(v)];
                        any = true;
                    }
                }
        if (any)
            out[p] = tnames[size_t(std::max_element(bc.begin(), bc.end()) - bc.begin())];
    }
    return out;
}

Vals TerrainBox::mismatch_values(const Flat &idx, Vals nw)
{
    // Where the provinces disagree: a sea tile's pixels get ocean whatever is painted (they are made right).
    if (scope->currentData().toString() != u"mismatch")
        return nw;
    mismatch::Mismatch *mm = panel->mismatch();
    if (!mm || idx.empty())
        return nw;
    const mismatch::Masks m = mm->at(idx);
    if (const auto *g = m.get(u"t_sea"_s))
        for (size_t i = 0; i < nw.size(); ++i)
            if (g->flat()[i])
                nw[i] = std::uint8_t(mm->ls->ocean_fill);
    return nw;
}

void TerrainBox::fill_at(double x, double y)
{
    layerbmp::IndexedBitmap &t_b = tb();
    const long long xi = (long long)x, yi = (long long)y;
    if (!(0 <= xi && xi < md.w && 0 <= yi && yi < md.h) || !index)
        return;
    const QString sc = scope->currentData().toString();
    if (sc == u"mismatch") {
        // the connected patch of mismatched pixels around the click, each made right
        mismatch::Mismatch *mm = panel->mismatch();
        const long long r = 400;
        const long long x0 = std::max(0LL, xi - r), y0 = std::max(0LL, yi - r), x1 = std::min(md.w, xi + r + 1),
                        y1 = std::min(md.h, yi + r + 1);
        const mismatch::Masks m = mm ? mm->window(y0, y1, x0, x1) : mismatch::Masks();
        grid::Grid<std::uint8_t> bad(y1 - y0, x1 - x0, 0);
        for (const QString &k : {u"t_sea"_s, u"t_land"_s})
            if (const auto *g = m.get(k))
                for (size_t i = 0; i < bad.flat().size(); ++i)
                    bad.flat()[i] |= g->flat()[i];
        if (!bad(yi - y0, xi - x0)) {
            Q_EMIT message(u"No mismatch there — the orange / yellow pixels are the ones this fills."_s);
            return;
        }
        const grid::Grid<std::uint8_t> comp = provbmp::component(bad, yi - y0, xi - x0);
        Flat idx;
        for (long long yy = 0; yy < comp.h(); ++yy)
            for (long long xx = 0; xx < comp.w(); ++xx)
                if (comp(yy, xx))
                    idx.push_back((yy + y0) * md.w + (xx + x0));
        const Vals nw = mismatch_values(idx, Vals(idx.size(), std::uint8_t(*index)));
        commit(u"Terrain: %1 mismatched px made right"_s.arg(idx.size()), idx, nw,
               u"%1 mismatched px made right (sea tiles → ocean, land → this colour)."_s.arg(
                   widgets::fmt((long long)idx.size())));
        return;
    }
    if (!allowed_mask({md.idmap(yi, xi)})[0]) {
        Q_EMIT message(u"⚠ Terrain can only be painted on land%1."_s.arg(
            sc == u"selected" ? u" of the selected provinces"_s : QString()));
        return;
    }
    // the area: pixels of the clicked colour connected to the click, inside what may be painted
    std::array<long long, 4> bx{0, 0, md.w, md.h};
    if (sc == u"selected")
        if (const auto b = md.bbox(panel->sel))
            bx = *b;
    const auto [x0, y0, x1, y1] = bx;
    std::vector<std::uint16_t> sub_ids;
    sub_ids.reserve(size_t((y1 - y0) * (x1 - x0)));
    for (long long yy = y0; yy < y1; ++yy)
        for (long long xx = x0; xx < x1; ++xx)
            sub_ids.push_back(md.idmap(yy, xx));
    const Vals ok = allowed_mask(sub_ids);
    const std::uint8_t c0 = (*t_b.data)(yi, xi);
    grid::Grid<std::uint8_t> mask(y1 - y0, x1 - x0, 0);
    for (long long yy = y0; yy < y1; ++yy)
        for (long long xx = x0; xx < x1; ++xx) {
            const size_t k = size_t((yy - y0) * (x1 - x0) + (xx - x0));
            mask.flat()[k] = (*t_b.data)(yy, xx) == c0 && ok[k];
        }
    const grid::Grid<std::uint8_t> comp = provbmp::component(mask, yi - y0, xi - x0);
    Flat idx;
    long long mnx = comp.w(), mny = comp.h(), mxx = -1, mxy = -1;
    for (long long yy = 0; yy < comp.h(); ++yy)
        for (long long xx = 0; xx < comp.w(); ++xx)
            if (comp(yy, xx)) {
                idx.push_back((yy + y0) * md.w + (xx + x0));
                mnx = std::min(mnx, xx);
                mny = std::min(mny, yy);
                mxx = std::max(mxx, xx);
                mxy = std::max(mxy, yy);
            }
    const QString st = style_for_fill();
    // rings only need the area itself (plus a 1 px margin): crop before measuring the depth
    const long long cx0 = std::max(0LL, mnx - 1), cy0 = std::max(0LL, mny - 1);
    const long long cx1 = std::min((long long)comp.w(), mxx + 2), cy1 = std::min((long long)comp.h(), mxy + 2);
    grid::Grid<std::uint8_t> region(cy1 - cy0, cx1 - cx0);
    for (long long yy = cy0; yy < cy1; ++yy)
        for (long long xx = cx0; xx < cx1; ++xx)
            region(yy - cy0, xx - cx0) = comp(yy, xx);
    const std::array<long long, 4> rbox{x0 + cx0, y0 + cy0, x0 + cx1, y0 + cy1};
    const Vals nw = values_for(idx, st, &region, &rbox);
    commit(u"Terrain fill %1 px"_s.arg(idx.size()), idx, nw,
           u"Filled %1 px (%2)."_s.arg(widgets::fmt((long long)idx.size()), style_name(st)));
}

void TerrainBox::fill_selected()
{
    std::vector<long long> sel;
    for (long long p : panel->sel)
        if (0 < p && p < md.n && !md.sea[size_t(p)] && md.area[size_t(p)] > 0)
            sel.push_back(p);
    if (sel.empty() || !index)
        return;
    auto [x0, y0, x1, y1] = *md.bbox(sel);
    x0 = std::max(0LL, x0 - 1);
    y0 = std::max(0LL, y0 - 1);
    x1 = std::min(md.w, x1 + 1);
    y1 = std::min(md.h, y1 + 1);
    std::vector<std::uint8_t> lut(static_cast<size_t>(md.n), 0);
    for (long long p : sel)
        lut[size_t(p)] = 1;
    grid::Grid<std::uint8_t> mask(y1 - y0, x1 - x0, 0);
    Flat idx;
    for (long long yy = y0; yy < y1; ++yy)
        for (long long xx = x0; xx < x1; ++xx)
            if (lut[md.idmap(yy, xx)]) {
                mask(yy - y0, xx - x0) = 1;
                idx.push_back(yy * md.w + xx);
            }
    const QString st = style_for_fill();
    const std::array<long long, 4> box{x0, y0, x1, y1};
    const Vals nw = values_for(idx, st, &mask, &box);
    const QString s = sel.size() == 1 ? QString() : u"s"_s;
    commit(u"Terrain of %1 province%2"_s.arg(sel.size()).arg(s), idx, nw,
           u"Filled %1 province%2 (%3 px, %4)."_s.arg(sel.size()).arg(s, widgets::fmt((long long)idx.size()),
                                                                      style_name(st)));
}

// ------------------------------------------------------------------ hover
QString TerrainBox::pixel_info(long long x, long long y) const
{
    if (!doc || !tb().data || !(0 <= x && x < md.w && 0 <= y && y < md.h))
        return QString();
    const long long i = (*tb().data)(y, x);
    const long long p = md.idmap(y, x);
    const moddata::Province *pr = mod.provinces.get(p);
    const auto maj = _majority({y * md.w + x});
    const auto it = maj.find(p);
    return u"terrain.bmp %1 · %2%3"_s.arg(i).arg(
        type_label(type_of(i)),
        pr && it != maj.end() ? u"  ·  #%1 %2: %3"_s.arg(p).arg(pr->name, it->second) : QString());
}

// ---------------------------------------------------------------------------------------------- TerrainHandler
mapview::MapView *TerrainHandler::view() const
{
    return b->panel->view;
}

void TerrainHandler::_outline(double x, double y)
{
    last_pos = {x, y};
    const double r = std::max(0.5, b->size_spin->value() / 2.0);
    mapview::Shape s;
    s.kind = u"circle"_s;
    s.pts = {QPointF(x, y)};
    s.radius = r;
    s.color = {255, 255, 255};
    view()->shape_layer->set_temp({s});
}

void TerrainHandler::preview()
{
    // Show the brush at its new size where the mouse last was on the map (or the middle of the view).
    mapview::MapView *v = view();
    if (!v || !b->panel->edit_btn->isChecked())
        return;
    std::pair<double, double> pos;
    if (last_pos) {
        pos = *last_pos;
    } else {
        const QPointF c = v->mapToScene(v->viewport()->rect().center());
        pos = {c.x(), c.y()};
    }
    _outline(pos.first, pos.second);
}

bool TerrainHandler::cancel()
{
    const bool had = touched.has_value();
    if (touched && !touched->isEmpty()) {
        Flat idx;
        Vals old;
        for (const auto &[i, c] : *touched) {
            idx.push_back(i);
            old.push_back(std::uint8_t(c));
        }
        b->tb().live(idx, old);
    }
    touched.reset();
    last.reset();
    if (view())
        view()->shape_layer->set_temp({});
    return had;
}

void TerrainHandler::_stamp(double x, double y)
{
    const mapview::MapData &md = b->md;
    Flat idx = layertools::disc(x, y, b->size_spin->value(), md.w, md.h);
    {
        std::vector<std::uint16_t> ids;
        ids.reserve(idx.size());
        for (std::int64_t f : idx)
            ids.push_back(md.idmap.flat()[size_t(f)]);
        const Vals ok = b->allowed_mask(ids, &idx);
        Flat keep;
        for (size_t i = 0; i < idx.size(); ++i)
            if (ok[i] && !touched->contains(idx[i]))
                keep.push_back(idx[i]);
        idx = std::move(keep);
    }
    if (idx.empty())
        return;
    const Vals nw = b->mismatch_values(idx, b->values_for(idx, b->style()));
    const auto &flat = b->tb().data->flat();
    for (std::int64_t i : idx)
        touched->insert(i, flat[size_t(i)]);
    b->tb().live(idx, nw);
}

bool TerrainHandler::press(double x, double y, double, Qt::KeyboardModifiers mods)
{
    if (mods & Qt::ControlModifier)
        return false;
    if (!b->index || !b->tb().data)
        return true;
    if (b->tool() == u"fill") {
        b->fill_at(x, y);
        return true;
    }
    touched.emplace();
    last = {x, y};
    _stamp(x, y);
    return true;
}

void TerrainHandler::move(double x, double y)
{
    _outline(x, y);
    if (!touched)
        return;
    const auto [lx, ly] = *last;
    const double step = std::max(0.5, b->size_spin->value() / 4.0);
    const long long n = std::max(1LL, (long long)(py::hypot(x - lx, y - ly) / step));
    for (long long i = 1; i <= n; ++i)
        _stamp(lx + (x - lx) * double(i) / double(n), ly + (y - ly) * double(i) / double(n));
    last = {x, y};
}

void TerrainHandler::release(double, double, double, Qt::KeyboardModifiers)
{
    if (!touched)
        return;
    const OrderedMap<long long, long long> t = std::move(*touched);
    touched.reset();
    last.reset();
    if (t.isEmpty())
        return;
    Flat idx;
    Vals old;
    for (const auto &[i, c] : t) {
        idx.push_back(i);
        old.push_back(std::uint8_t(c));
    }
    auto &flat = b->tb().data->flat();
    Vals nw(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        nw[k] = flat[size_t(idx[k])];
        flat[size_t(idx[k])] = old[k];         // back to the start; commit() applies it as one step
    }
    b->commit(u"Terrain brush %1 px"_s.arg(idx.size()), idx, nw,
              u"Painted %1 px of terrain."_s.arg(widgets::fmt((long long)idx.size())));
}

bool TerrainHandler::rclick(double x, double y, double, Qt::KeyboardModifiers)
{
    const mapview::MapData &md = b->md;
    const long long xi = (long long)x, yi = (long long)y;
    if (b->tb().data && 0 <= xi && xi < md.w && 0 <= yi && yi < md.h) {
        const long long i = (*b->tb().data)(yi, xi);
        if (b->swatches.contains(i)) {
            b->set_index(i);
            Q_EMIT b->message(u"Painting with terrain %1 (%2)."_s.arg(i).arg(type_label(b->type_of(i))));
        }
    }
    return true;
}

bool TerrainHandler::wheel(double steps, Qt::KeyboardModifiers mods)
{
    if (!(mods & drawlink::BRUSH_KEY))               // (C++: Shift; Ctrl + wheel zooms)
        return false;
    widgets::SliderField *s = b->size_spin;
    s->setValue(std::max(1.0, std::min(s->maximum(), s->value() + double(py::round_int(steps) *
                                                                         (s->value() < 10 ? 1 : 2)))));
    return true;
}

}  // namespace terrainpanel
