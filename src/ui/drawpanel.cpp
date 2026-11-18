// drawpanel.py — line by line. (Python's numpy masks are byte vectors here; doc.mismatch is the panel's own.)
#include "ui/drawpanel.h"

#include "core/adjacency.h"
#include "core/document.h"
#include "core/history.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pynum.h"
#include "core/refs.h"
#include "map/adjacency_geo.h"
#include "map/layerbmp.h"
#include "map/layertools.h"
#include "map/mapcheck.h"
#include "map/mismatch.h"
#include "map/positions_geo.h"
#include "map/provbmp.h"
#include "map/provdelete.h"
#include "ui/colormappanel.h"
#include "ui/mismatchbox.h"
#include "ui/newprov_dialog.h"
#include "ui/pospanel.h"
#include "ui/riverpanel.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QHideEvent>
#include <QShowEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using widgets::fmt;
using widgets::label;

namespace drawpanel {

const std::map<QString, QString> LAYER_HELP = {
    {u"rivers"_s, u"• River system: ＋ New river system, then click what the guide asks for (source, mouth, optional points and branches) and ▶ Generate\n• Select: click a river (Shift+click: the whole network), then regenerate or delete it\n• Pen / Eraser: left-drag · right-click picks the colour under the cursor\n• Ctrl+click selects provinces · right-drag pans · Esc cancels · Ctrl+Z undoes"_s},
    {u"prov"_s, u"• Left-drag paints (Brush) or erases (Eraser) / left-click fills a connected area (Fill)\n• Right-click picks the province under the cursor to paint with\n• Shift+wheel changes the brush size · wheel / Ctrl+wheel zooms · Ctrl+click selects · right-drag pans\n• Esc cancels a stroke · Ctrl+Z undoes it"_s},
    {u"terrain"_s, u"• Left-drag paints terrain (Brush) / left-click fills a connected area of one colour (Fill)\n• Right-click picks the terrain colour under the cursor\n• Orange = terrain on a sea tile, yellow = a land province without terrain (see the legend and the Errors tab)\n• Shift+wheel changes the brush size · wheel / Ctrl+wheel zooms · Ctrl+click selects provinces · right-drag pans\n• Esc cancels a stroke · Ctrl+Z undoes it"_s},
    // (C++ only, 2026-11-10)
    {u"colours"_s, u"• Selected provinces: click / Ctrl+click provinces, then ◐ Tint — the hue spreads out around them and fades\n• Soft brush: left-drag paints the hue, strongest in the middle\n• Right-click picks the colour under the cursor\n• Shift+wheel changes the reach / brush size · wheel / Ctrl+wheel zooms · right-drag pans\n• Esc cancels a stroke · Ctrl+Z undoes a tint"_s},
};
const QString DELETE_TEXT = u"✕  Delete province…"_s;
const std::vector<std::pair<QString, QString>> PAINT_OVER = {
    {u"any"_s, u"Anything"_s}, {u"land"_s, u"Land only"_s}, {u"sea"_s, u"Sea only"_s},
    {u"selected"_s, u"Only the selected provinces / sea tiles"_s}, {u"unassigned"_s, u"Only pixels of no province"_s},
    // only where the maps disagree — and only where what you paint makes it right (the tester, 2026-10-15):
    // a land province goes where terrain.bmp (rivers.bmp) has land on a sea tile, a sea tile where a land
    // province has ocean (water)
    {u"mm_terrain"_s, u"Only where terrain.bmp disagrees (what you paint makes it right)"_s},
    {u"mm_rivers"_s, u"Only where rivers.bmp disagrees (what you paint makes it right)"_s}};

static QString s_(long long n, const QString &one = QString(), const QString &many = u"s"_s)
{
    return n == 1 ? one : many;
}

static QString pname(const moddata::Province &pr)
{
    const QString n = pr.name.trimmed();
    return n.isEmpty() ? u"(unnamed)"_s : n;
}

DrawPanel::DrawPanel(moddata::Mod &mod, mapview::MapData &md, QWidget *parent) : QWidget(parent), mod(mod), md(md)
{
    // the clean-up counts need a full-map pass: done once painting has paused for a moment
    _clean_timer = new QTimer(this);
    _clean_timer->setSingleShot(true);
    _clean_timer->setInterval(700);
    connect(_clean_timer, &QTimer::timeout, this, [this] { _update_clean(); });
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    s->lay->addWidget(label(u"Draw"_s, u"title"_s));
    intro = label(QString(), u"faint"_s);
    s->lay->addWidget(intro);
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    layer_group = new QButtonGroup(this);
    for (const auto &[key, text] : std::vector<std::pair<QString, QString>>{
             {u"prov"_s, u"Provinces"_s}, {u"terrain"_s, u"Terrain"_s}, {u"rivers"_s, u"Rivers"_s},
             {u"colours"_s, u"Colours"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == u"prov");
        if (key == u"colours")                       // (C++ only, 2026-11-10)
            b->setToolTip(u"The land colours the game lays over the terrain (map/terrain/colormap.dds)"_s);
        connect(b, &QPushButton::toggled, this, [this](bool on) {
            if (on)
                _layer_changed();
        });
        layer_group->addButton(b);
        layer_btns[key] = b;
        layer_keys << key;
        row->addWidget(b, 1);
    }
    s->lay->addLayout(row);
    // terrain / rivers: the pixels in their exact colours — the borders are lines BETWEEN the pixels, each kind
    // on or off (the tester, 2026-10-17: the shaded borders changed the colours being painted)
    border_row = new QWidget();
    auto *br = new QHBoxLayout(border_row);
    br->setContentsMargins(0, 6, 0, 0);
    br->setSpacing(0);
    br->addWidget(label(u"Borders  "_s, u"sub"_s, false));
    for (const auto &[key, text, tip] : std::vector<std::tuple<QString, QString, QString>>{
             {u"none"_s, u"None"_s, u"The pure picture — no border at all"_s},
             {u"prov"_s, u"Provinces"_s, u"Province borders as thin lines between the pixels"_s},
             {u"state"_s, u"States"_s, u"State borders as lines between the pixels"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setToolTip(tip + u" (the pixels keep their exact colours; lines show from 2× zoom)"_s);
        connect(b, &QPushButton::clicked, this, [this, k = key] { set_borders(k); });
        border_btns[key] = b;
        br->addWidget(b, 1);
    }
    _show_borders();
    s->lay->addWidget(border_row);
    border_row->hide();
    edit_btn = new QPushButton(u"✎  Draw on the map   (D)"_s);
    edit_btn->setObjectName(u"primary"_s);
    edit_btn->setToolTip(u"Switch drawing on / off — shortcut D, from any tab"_s);
    edit_btn->setCheckable(true);
    connect(edit_btn, &QPushButton::toggled, this, [this](bool on) { _edit_mode(on); });
    s->lay->addWidget(edit_btn);
    help = label(LAYER_HELP.at(u"prov"_s), u"sub"_s);
    help->hide();
    s->lay->addWidget(help);
    lay->addWidget(s);

    // where provinces.bmp, terrain.bmp and rivers.bmp disagree: legend, counts, fix per kind (every layer)
    mm_box = new mismatchbox::MismatchBox([this] { return mismatch(); }, doc, this);
    connect(mm_box, &mismatchbox::MismatchBox::message, this, &DrawPanel::message);
    lay->addWidget(mm_box);

    s = new widgets::Section();
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(label(u"Tool"_s, u"sub"_s, false), 0, 0);
    auto *trow = new QHBoxLayout();
    trow->setSpacing(0);
    tool_group = new QButtonGroup(this);
    for (const auto &[key, text] : std::vector<std::pair<QString, QString>>{
             {u"brush"_s, u"Brush"_s}, {u"fill"_s, u"Fill"_s}, {u"eraser"_s, u"Eraser"_s},
             {u"clip"_s, u"Cut && paste"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == u"brush");
        // (four buttons since Cut & paste, 2026-11-09: they share the row's width instead of widening the panel —
        // a wider panel squeezed the word-wrapped notes above)
        b->setSizePolicy(QSizePolicy::Ignored, b->sizePolicy().verticalPolicy());
        if (key == u"clip")
            b->setToolTip(u"Cut provinces off the map and paste them elsewhere — move, turn, mirror, then ✓ OK"_s);
        tool_group->addButton(b);
        tool_btns[key] = b;
        tool_keys << key;
        connect(b, &QPushButton::toggled, this, [this](bool) { _tool_changed(); });
        trow->addWidget(b, 1);
    }
    grid->addLayout(trow, 0, 1);
    QLabel *paint_lbl = label(u"Paint with"_s, u"sub"_s, false);
    grid->addWidget(paint_lbl, 1, 0);
    auto *prow = new QHBoxLayout();
    paint_spin = new QSpinBox();
    paint_spin->setRange(0, 65535);
    paint_spin->setSpecialValueText(u"—"_s);
    paint_spin->setButtonSymbols(QSpinBox::NoButtons);
    paint_spin->setFixedWidth(80);
    connect(paint_spin, &QSpinBox::valueChanged, this, [this](int v) { set_paint(v, true); });
    paint_name = label(u"right-click the map to pick"_s, u"faint"_s, false);
    paint_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // the button's text with a faint "(right click)" hint: right-clicking the map picks the province directly
    auto *ub = new QPushButton();
    ub->setToolTip(u"Paint with the first selected province / sea tile — or right-click a province on the map to "
                   "pick it directly"_s);
    auto *bl = new QHBoxLayout(ub);
    bl->setContentsMargins(12, 0, 12, 0);
    bl->setSpacing(6);
    for (const auto &[text, obj] : std::vector<std::pair<QString, QString>>{{u"Use selection"_s, QString()},
                                                                           {u"(right click)"_s, u"faint"_s}}) {
        auto *t = new QLabel(text);
        if (!obj.isEmpty())
            t->setObjectName(obj);
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(t);
    }
    ub->setMinimumWidth(bl->sizeHint().width());
    connect(ub, &QPushButton::clicked, this, [this] { _use_selection(); });
    prow->addWidget(paint_spin);
    prow->addWidget(paint_name, 1);
    prow->addWidget(ub);
    grid->addLayout(prow, 1, 1);
    // the province painted with: its separate parts on the map, and whether they are meant to be (islands)
    island_lbl = label(u"Islands"_s, u"sub"_s, false);
    grid->addWidget(island_lbl, 2, 0, Qt::AlignTop);
    island_host = new QWidget();
    auto *il = new QVBoxLayout(island_host);
    il->setContentsMargins(0, 2, 0, 2);
    il->setSpacing(2);
    island_cb = new QCheckBox(u"Has islands (its separate parts are meant to be)"_s);
    island_cb->setToolTip(u"For the province you paint with. The Errors tab then leaves its separate parts "
                          "alone (stray pixels / several parts).\nWritten as set_province_flag = island in "
                          "its history file, like the farm / mine flags — for every start date. Same box as "
                          "in Provinces."_s);
    connect(island_cb, &QCheckBox::clicked, this, [this](bool on) {
        if (paint_id && *paint_id)
            Q_EMIT islandRequested(on, {*paint_id});
    });
    island_note = label(QString(), u"faint"_s);
    island_note->setTextFormat(Qt::RichText);
    il->addWidget(island_cb);
    il->addWidget(island_note);
    grid->addWidget(island_host, 2, 1);
    QLabel *size_lbl = label(u"Brush size"_s, u"sub"_s, false);
    grid->addWidget(size_lbl, 3, 0);
    size_spin = new widgets::SliderField(1, 80, 400, 6, u"px"_s, true);
    size_spin->setToolTip(u"Brush size — drag, or type any size (up to 400 px). Shift+wheel on the map too."_s);
    connect(size_spin, &widgets::SliderField::valueChanged, this, [this](double) { prov_handler.preview(); });
    grid->addWidget(size_spin, 3, 1);
    over_lbl = label(u"Paint over"_s, u"sub"_s, false);
    grid->addWidget(over_lbl, 4, 0);
    over = new QComboBox();
    for (const auto &[k, t] : PAINT_OVER)
        over->addItem(t, k);
    grid->addWidget(over, 4, 1);
    erase_lbl = label(u"Erase"_s, u"sub"_s, false);
    grid->addWidget(erase_lbl, 5, 0);
    erase = new QComboBox();
    erase->addItem(u"Only the province you paint with"_s, u"target"_s);
    erase->addItem(u"Anything"_s, u"any"_s);
    erase->addItem(u"Only land provinces where terrain.bmp has ocean"_s, u"mm_terrain"_s);
    erase->setToolTip(u"Erased pixels belong to no province (black), like the black areas already on the map."_s);
    grid->addWidget(erase, 5, 1);
    erase_how_lbl = label(u"Erase with"_s, u"sub"_s, false);
    grid->addWidget(erase_how_lbl, 6, 0);
    erase_how = new QComboBox();
    erase_how->addItem(u"Brush — drag over the map"_s, u"brush"_s);
    erase_how->addItem(u"Fill — click a connected area"_s, u"fill"_s);
    grid->addWidget(erase_how, 6, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    paint_rows = {paint_lbl, paint_spin, paint_name, ub, size_lbl, size_spin};
    _build_clip_box(s->lay);
    new_btn = new QPushButton(u"＋  New province…"_s);
    new_btn->setToolTip(u"Make a new province or sea tile (next free id, a spare colour of definition.csv) and "
                        "paint it onto the map"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { new_province(); });
    s->lay->addWidget(new_btn);
    del_btn = new QPushButton(DELETE_TEXT);
    del_btn->setToolTip(u"Delete the provinces selected on the map for good (Ctrl+click selects them here; "
                        "with nothing selected: the province picked to paint with). Their land goes to the "
                        "provinces around them; history, pops, state and places go. Asks first."_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _delete_clicked(); });
    s->lay->addWidget(del_btn);
    del_note = new QLabel();
    del_note->setWordWrap(true);
    del_note->setTextFormat(Qt::RichText);
    del_note->setStyleSheet(u"QLabel { color: %1; border: 2px solid %2; border-radius: 4px; "
                            "padding: 6px 8px; }"_s.arg(theme::ERROR_, theme::ERROR_));
    del_note->hide();
    s->lay->addWidget(del_note);
    // provinces that are defined but have no pixels any more: always listed here, click one to paint with it
    gone_title = label(QString(), u"warn"_s);
    s->lay->addWidget(gone_title);
    gone_list = new QListWidget();

    gone_list->setToolTip(u"Click a province to take it into 'Paint with' and draw it back onto the map"_s);
    connect(gone_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *it) { bring_back(it->data(Qt::UserRole).toLongLong()); });
    s->lay->addWidget(gone_list);
    gone_title->hide();
    gone_list->hide();
    status = label(QString(), u"sub"_s);
    s->lay->addWidget(status);
    lay->addWidget(s);
    paint_section = s;
    _tool_changed();

    // provinces made in this session (until Apply): paint them, then Finish
    s = new_section = new widgets::Section();
    s->lay->addWidget(label(u"New provinces"_s));
    s->lay->addWidget(label(u"Not saved yet. Paint each one onto the map, then press Finish: it places the "
                            "positions, adds the coast crossings and moves a share of the pops along."_s,
                            u"faint"_s));
    new_box = new QVBoxLayout();
    new_box->setSpacing(10);
    s->lay->addLayout(new_box);
    lay->addWidget(s);
    s->hide();

    s = new widgets::Section();
    s->lay->addWidget(label(u"Clean up"_s));
    clean_info = label(QString(), u"sub"_s);
    s->lay->addWidget(clean_info);
    stray_btn = new QPushButton(u"Clean stray pixel bits"_s);
    stray_btn->setToolTip(u"Every separate bit of a province smaller than %1 px goes to the province most of "
                          "its neighbouring pixels belong to."_s.arg(STRAY_LIMIT));
    connect(stray_btn, &QPushButton::clicked, this, [this] { _clean(std::nullopt); });
    zero_btn = new QPushButton(u"Fill small areas of no province"_s);
    zero_btn->setToolTip(u"Areas of pixels that belong to no province and are smaller than %1 px go to the "
                         "province around them. Larger ones: paint them yourself."_s.arg(STRAY_LIMIT));
    connect(zero_btn, &QPushButton::clicked, this, [this] { _clean(0); });
    s->lay->addWidget(stray_btn);
    s->lay->addWidget(zero_btn);
    lay->addWidget(s);
    clean_section = s;

    // the terrain.bmp layer
    terrain_box = new terrainpanel::TerrainBox(mod, md, this);
    connect(terrain_box, &terrainpanel::TerrainBox::message, this, &DrawPanel::message);
    terrain_box->hide();
    lay->addWidget(terrain_box);
    // the rivers.bmp layer
    river_box = new riverpanel::RiverBox(mod, md, this);
    connect(river_box, &riverpanel::RiverBox::message, this, &DrawPanel::message);
    connect(river_box, &riverpanel::RiverBox::overlayChanged, this, &DrawPanel::overlayChanged);
    river_box->hide();
    lay->addWidget(river_box);
    // the land colour map (C++ only, 2026-11-10)
    colour_box = new colormappanel::ColormapBox(mod, md, this);
    connect(colour_box, &colormappanel::ColormapBox::message, this, &DrawPanel::message);
    colour_box->hide();
    lay->addWidget(colour_box);
    lay->addStretch(1);
    _layer_changed(false);
}

DrawPanel::~DrawPanel() = default;

// ------------------------------------------------------------------ layers
QString DrawPanel::layer() const
{
    for (const QString &k : layer_keys)
        if (layer_btns.at(k)->isChecked())
            return k;
    return u"prov"_s;
}

mapview::MouseHandler *DrawPanel::handler()
{
    // The mouse tool of the current layer.
    const QString l = layer();
    if (l == u"terrain")
        return &terrain_box->handler;
    if (l == u"rivers")
        return &river_box->handler;
    if (l == u"colours")
        return &colour_box->handler;
    return &prov_handler;
}

void DrawPanel::cancel_handlers()
{
    // floating provinces stay floating (only ✓ OK places them — the user, 2026-11-09); out of sight while another
    // layer is drawn or drawing is off, back when the Provinces layer is drawn again
    if (clip.floating()) {
        prov_handler.touched.reset();
        prov_handler.last.reset();
        clip.drop();
    } else {
        prov_handler.cancel();
    }
    terrain_box->handler.cancel();
    river_box->handler.cancel();
    colour_box->handler.cancel();
    if (clip.floating() && clip_active())
        clip.show();
    else if (clip.floating() && view)
        view->shape_layer->set_temp({});
    _clip_changed();
}

void DrawPanel::hideEvent(QHideEvent *e)
{
    if (clip.floating() && view)                     // another tab: they wait, out of sight (Apply places them)
        view->shape_layer->set_temp({});
    _clip_changed();
    QWidget::hideEvent(e);
    _draw_bar();                                     // (after: isVisible() is false now)
}

void DrawPanel::set_borders(const QString &key)
{
    // "none": the pure picture; "prov" / "state": that kind on or off.
    if (key == u"none") {
        borders = {{u"prov"_s, false}, {u"state"_s, false}};
    } else {
        borders[key] = !borders[key];
    }
    _show_borders();
    if (view) {
        mapview::EdgeLayer *el = view->edge_layer;
        el->prov = borders[u"prov"_s];
        el->state = borders[u"state"_s];
        el->update();
    }
}

void DrawPanel::_show_borders()
{
    const bool none = !(borders[u"prov"_s] || borders[u"state"_s]);
    border_btns[u"none"_s]->setChecked(none);
    border_btns[u"prov"_s]->setChecked(borders[u"prov"_s]);
    border_btns[u"state"_s]->setChecked(borders[u"state"_s]);
}

void DrawPanel::_layer_changed(bool emit)
{
    const QString lay = layer();
    cancel_handlers();
    for (QWidget *w : {static_cast<QWidget *>(paint_section), static_cast<QWidget *>(clean_section)})
        w->setVisible(lay == u"prov");
    terrain_box->setVisible(lay == u"terrain");
    river_box->setVisible(lay == u"rivers");
    colour_box->setVisible(lay == u"colours");
    mm_box->setVisible(lay != u"colours");           // (the land / sea mismatches: not a colour-map matter)
    border_row->setVisible(lay == u"terrain" || lay == u"rivers" || lay == u"colours");
    static const std::map<QString, QString> INTRO = {
        {u"prov"_s, u"Paint provinces.bmp. Every stroke is one undo step (Ctrl+Z); the file is written on Apply, with a "
                    "backup. Borders, neighbours, coasts and the checks follow at once."_s},
        {u"terrain"_s, u"Paint terrain.bmp (which terrain the game draws — and, by majority, each province's terrain "
                       "type). Every stroke is one undo step; the file is written on Apply, with a backup."_s},
        {u"rivers"_s, u"Paint rivers.bmp: 1-pixel rivers — source (green), widths (blue) and a merge pixel (orange here, "
                      "red in the file) where a river joins another. Lay out whole river systems and regenerate them "
                      "until you like them; each step is one undo step."_s},
        {u"colours"_s, u"Tint the land colours the game lays over the terrain (map/terrain/colormap.dds: brown deserts, "
                       "green farmland). Every tint is one undo step; the file is written on Apply, with a backup."_s},
    };
    intro->setText(INTRO.at(lay));
    help->setText(LAYER_HELP.at(lay));
    if (doc) {
        if (lay == u"terrain")
            terrain_box->refresh(doc);
        else if (lay == u"rivers")
            river_box->refresh(doc);
        else if (lay == u"colours")
            colour_box->refresh(doc);
        _show_new();
        if (lay != u"colours")
            try {
                mm_box->doc = doc;
                mm_box->refresh(lay);
            } catch (const std::exception &) {
            }
    }
    if (emit) {
        Q_EMIT layerChanged(lay);
        Q_EMIT editModeChanged(edit_btn->isChecked());
    }
    _draw_bar();
}

QString DrawPanel::pixel_info(long long x, long long y)
{
    // The hover line: the layer's own words, and which mismatch the pixel has (if any).
    if (layer() == u"colours")
        return colour_box->pixel_info(x, y);
    mismatch::Mismatch *mm = mismatch();
    const QString extra = mm ? mm->describe(x, y) : QString();
    if (layer() == u"terrain")
        return terrain_box->pixel_info(x, y) + extra;
    if (layer() == u"rivers")
        return river_box->pixel_info(x, y) + extra;
    if (extra.isEmpty())
        return QString();
    qsizetype i = 0;                                  // (extra.lstrip(" ·"))
    while (i < extra.size() && (extra[i] == u' ' || extra[i] == u'·'))
        ++i;
    return extra.mid(i);
}

// ------------------------------------------------------------------ state
provbmp::ProvinceBitmap &DrawPanel::bmp() const
{
    return doc->map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
}

QString DrawPanel::tool() const
{
    for (const QString &k : tool_keys)
        if (tool_btns.at(k)->isChecked())
            return k;
    return u"brush"_s;
}

void DrawPanel::_tool_changed()
{
    if (!erase)
        return;
    const bool eraser = tool() == u"eraser", clipping = tool() == u"clip";
    for (QWidget *w : {over_lbl, static_cast<QWidget *>(over)})
        w->setVisible(!eraser && !clipping);
    for (QWidget *w : {erase_lbl, static_cast<QWidget *>(erase), erase_how_lbl, static_cast<QWidget *>(erase_how)})
        w->setVisible(eraser);
    for (QWidget *w : paint_rows)
        w->setVisible(!clipping);
    if (clip_box)
        clip_box->setVisible(clipping);
    _show_island();
    if (view && clipping && !clip.floating())        // (no brush ring in Cut & paste)
        view->shape_layer->set_temp({});
    _clip_changed();
}

// ------------------------------------------------------------------ cut & paste (provclip)
void DrawPanel::_build_clip_box(QVBoxLayout *into)
{
    // The Cut & paste tool's box (2026-11-09, the user: "a utility bar … its own section alongside draw and erase
    // and fill"), and the same turn / mirror / OK buttons as a bar at the map's bottom right while provinces float.
    clip_box = new QWidget();
    auto *v = new QVBoxLayout(clip_box);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(6);
    v->addWidget(label(u"1. Pick the provinces on the map (click; Ctrl+click adds more).\n2. ✂ Cut takes them off the "
                       "map (black, no province) — ❐ Copy leaves them where they are.\n3. Paste brings them floating: "
                       "drag anywhere on the map to move them (arrow keys: 1 px, Shift: 10 px), turn ⟲ ⟳ or mirror ⇋ ⇵ "
                       "them.\n4. ✓ OK places them — only OK does. Esc / ✕ puts cut ones back, or drops a copy. "
                       "Terrain, rivers, names and ports move along.\nA copy becomes NEW provinces: the history, owner, "
                       "cores and pops of each original; the copies of a state get a new state unless they touch it. "
                       "Paste again for another copy."_s,
                       u"faint"_s));
    auto *r1 = new QHBoxLayout();
    r1->setSpacing(6);
    clip_cut_btn = new QPushButton(u"✂  Cut   (Ctrl+X)"_s);
    clip_cut_btn->setToolTip(u"Take the selected provinces / sea tiles off the map, to paste them elsewhere"_s);
    connect(clip_cut_btn, &QPushButton::clicked, this, [this] { Q_EMIT message(clip.cut()); _clip_changed(); });
    clip_copy_btn = new QPushButton(u"❐  Copy   (Ctrl+C)"_s);
    clip_copy_btn->setToolTip(u"Copy the selected provinces / sea tiles: Paste then makes new provinces like them"_s);
    connect(clip_copy_btn, &QPushButton::clicked, this, [this] { Q_EMIT message(clip.copy()); _clip_changed(); });
    clip_paste_btn = new QPushButton(u"⧉  Paste   (Ctrl+V)"_s);
    clip_paste_btn->setToolTip(u"Bring the cut provinces back, or a copy, floating over the map"_s);
    connect(clip_paste_btn, &QPushButton::clicked, this, [this] { Q_EMIT message(clip.paste()); _clip_changed(); });
    r1->addWidget(clip_cut_btn, 1);
    r1->addWidget(clip_copy_btn, 1);
    r1->addWidget(clip_paste_btn, 1);
    v->addLayout(r1);
    auto *r2 = new QHBoxLayout();
    r2->setSpacing(6);
    _clip_buttons(r2, clip_box_btns);
    v->addLayout(r2);
    clip_note = label(QString(), u"sub"_s);
    v->addWidget(clip_note);
    into->addWidget(clip_box);
    clip_box->hide();
}

void DrawPanel::_clip_buttons(QHBoxLayout *row, std::vector<QPushButton *> &out)
{
    // ⟲ ⟳ ⇋ ⇵ · ✕ Put back · ✓ OK (the box and the map's bar each get their own)
    struct B {
        QString text, tip;
        std::function<void()> fn;
        bool primary;
    };
    const std::vector<B> bs = {
        {u"⟲"_s, u"Turn them a quarter to the left (anticlockwise)"_s, [this] { clip.rotate(false); }, false},
        {u"⟳"_s, u"Turn them a quarter to the right (clockwise)"_s, [this] { clip.rotate(true); }, false},
        {u"⇋"_s, u"Mirror them left ↔ right"_s, [this] { clip.flip(true); }, false},
        {u"⇵"_s, u"Mirror them top ↕ bottom"_s, [this] { clip.flip(false); }, false},
        {u"✕  Put back"_s, u"Put them back where they were cut from, as they were — or drop a floating copy (Esc)"_s,
         [this] { Q_EMIT message(clip.back()); }, false},
        {u"✓  OK"_s, u"Place them here — one undo step (Ctrl+Z)"_s, [this] { Q_EMIT message(clip.place()); }, true},
    };
    for (const B &b : bs) {
        auto *btn = new QPushButton(b.text);
        btn->setToolTip(b.tip);
        if (b.primary)
            btn->setObjectName(u"primary"_s);
        btn->setFocusPolicy(Qt::NoFocus);          // (the arrow keys stay with the map)
        connect(btn, &QPushButton::clicked, this, b.fn);
        row->addWidget(btn, b.primary ? 2 : 1);
        out.push_back(btn);
    }
}

bool DrawPanel::clip_active() const
{
    return view && edit_btn->isChecked() && layer() == u"prov" && isVisible();
}

void DrawPanel::_clip_changed()
{
    // The buttons follow the floating provinces; the map's bar shows only while they float (and drawing is on).
    const bool fl = clip.floating(), active = clip_active();
    for (QPushButton *b : clip_box_btns)
        b->setEnabled(fl);
    if (clip_cut_btn) {
        clip_cut_btn->setEnabled(!fl);
        clip_copy_btn->setEnabled(!fl);
        clip_paste_btn->setEnabled(clip.held() && !fl);
        const QString s = clip.count() == 1 ? QString() : u"s"_s;
        clip_note->setText(fl && clip.copying() ? u"A copy of %1 province%2 floating — drag, turn, then ✓ OK."_s
                                                      .arg(clip.count()).arg(s)
                           : fl ? u"%1 province%2 floating — drag, turn, then ✓ OK."_s.arg(clip.count()).arg(s)
                           : clip.held() && clip.copying()
                               ? u"%1 province%2 copied — Paste drops a copy (again and again)."_s.arg(clip.count()).arg(s)
                           : clip.held() ? u"%1 province%2 cut — Paste brings them back."_s.arg(clip.count()).arg(s)
                                         : u"Nothing cut or copied."_s);
    }
    if (fl && active && !clip_bar && view) {
        clip_bar = new QFrame(view);
        clip_bar->setObjectName(u"clipBar"_s);
        clip_bar->setStyleSheet(u"QFrame#clipBar { background: %1; border: 2px solid %2; border-radius: 6px; }"_s
                                    .arg(theme::PANEL, theme::GOLD));
        auto *h = new QHBoxLayout(clip_bar);
        h->setContentsMargins(8, 6, 8, 6);
        h->setSpacing(6);
        std::vector<QPushButton *> bar_btns;
        _clip_buttons(h, bar_btns);
        view->installEventFilter(this);
    }
    if (clip_bar) {
        clip_bar->setVisible(fl && active);
        if (fl && active) {
            clip_bar->adjustSize();
            _place_clip_bar();
            clip_bar->raise();
        }
    }
}

void DrawPanel::_place_clip_bar()
{
    // bottom right of the map, inside its scroll bars
    if (!clip_bar || !view)
        return;
    const QRect vp = view->viewport()->geometry();
    const QSize s = clip_bar->sizeHint();
    clip_bar->setGeometry(vp.right() - s.width() - 16 + 1, vp.bottom() - s.height() - 16 + 1, s.width(), s.height());
}

bool DrawPanel::eventFilter(QObject *o, QEvent *e)
{
    if (o == view && e->type() == QEvent::Resize) {
        _place_clip_bar();
        QTimer::singleShot(0, this, [this] { _place_draw_bar(); });   // (after the view has placed its pill)
    }
    return QWidget::eventFilter(o, e);
}

// ------------------------------------------------------------------ the drawing bar on the map (C++ only)
bool DrawPanel::drawing_shown() const { return view && edit_btn->isChecked() && isVisible(); }

void DrawPanel::set_layer(const QString &key)
{
    if (layer_btns.count(key))
        layer_btns.at(key)->setChecked(true);         // (toggled → _layer_changed)
    _draw_bar();
}

void DrawPanel::toggle_grid()
{
    if (!view)
        return;
    view->set_pixel_grid(!view->pixel_grid);
    _draw_bar();
    if (view->pixel_grid && view->transform().m11() < mapview::MapView::GRID_FROM)
        Q_EMIT message(u"Pixel grid on — it shows once you zoom in closer (Ctrl+wheel or the wheel)."_s);
}

void DrawPanel::_draw_bar()
{
    const bool on = drawing_shown();
    if (on && !draw_bar) {
        draw_bar = new QFrame(view);
        draw_bar->setObjectName(u"drawBar"_s);
        draw_bar->setStyleSheet(u"QFrame#drawBar { background: %1; border: 2px solid %2; border-radius: 6px; }"_s
                                    .arg(theme::PANEL, theme::GOLD));
        auto *h = new QHBoxLayout(draw_bar);
        h->setContentsMargins(6, 5, 6, 5);
        h->setSpacing(0);
        const std::tuple<QString, QString, QString> layers[4] = {
            {u"prov"_s, u"Provinces"_s, u"1"_s}, {u"terrain"_s, u"Terrain"_s, u"2"_s}, {u"rivers"_s, u"Rivers"_s, u"3"_s},
            {u"colours"_s, u"Colours"_s, u"4"_s}};
        for (const auto &[key, text, k] : layers) {
            auto *b = new QPushButton(u"%1  %2"_s.arg(text, k));
            b->setObjectName(u"seg"_s);
            b->setCheckable(true);
            b->setToolTip(u"Draw on %1 — key %2"_s.arg(text, k));
            connect(b, &QPushButton::clicked, this, [this, key] { set_layer(key); });
            bar_layer_btns[key] = b;
            h->addWidget(b);
        }
        h->addSpacing(8);
        grid_btn = new QPushButton(u"▦ Grid  G"_s);
        grid_btn->setObjectName(u"seg"_s);
        grid_btn->setCheckable(true);
        grid_btn->setToolTip(u"Lines between the pixels when zoomed in (from %1× on) — key G"_s.arg(
            mapview::MapView::GRID_FROM));
        connect(grid_btn, &QPushButton::clicked, this, [this] { toggle_grid(); });
        h->addWidget(grid_btn);
        view->installEventFilter(this);
    }
    if (!draw_bar)
        return;
    const QString lay = layer();
    for (const auto &[key, b] : bar_layer_btns)
        b->setChecked(key == lay);
    grid_btn->setChecked(view && view->pixel_grid);
    draw_bar->setVisible(on);
    if (on) {
        draw_bar->adjustSize();
        _place_draw_bar();
        draw_bar->raise();
        QTimer::singleShot(0, this, [this] { _place_draw_bar(); });   // (once the pill shows the new layer)
    }
}

void DrawPanel::_place_draw_bar()
{
    // top left of the map (the mode pill and the banner are in the middle, the legend at the bottom left)
    if (!draw_bar || !view)
        return;
    const QRect vp = view->viewport()->geometry();
    const QSize s = draw_bar->sizeHint();
    QRect r(vp.left() + 12, vp.top() + 12, s.width(), s.height());
    // a narrow map: below the pill / banner it would touch
    for (QWidget *w : {static_cast<QWidget *>(view->_pill), static_cast<QWidget *>(view->_banner)})
        if (w && !w->isHidden() && r.adjusted(0, 0, 8, 8).intersects(w->geometry()))
            r.moveTop(w->geometry().bottom() + 9);
    draw_bar->setGeometry(r);
}

void DrawPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (clip.floating() && clip_active())            // back on this tab: the floating provinces again
        clip.show();
    else
        _clip_changed();
    _draw_bar();
}

bool DrawPanel::is_fill() const
{
    return tool() == u"fill" || (tool() == u"eraser" && erase_how->currentData().toString() == u"fill");
}

long long DrawPanel::stroke_id() const
{
    // The id a brush stroke writes: the province to paint with, or 0 (no province) for the eraser.
    return tool() == u"eraser" ? 0 : paint_id.value_or(0);
}

bool DrawPanel::needs_target() const
{
    return tool() != u"eraser" || erase->currentData().toString() == u"target";
}

void DrawPanel::_edit_mode(bool on)
{
    help->setVisible(on);
    cancel_handlers();
    _draw_bar();
    Q_EMIT editModeChanged(on);
}

void DrawPanel::set_paint(long long pid, bool from_spin)
{
    const moddata::Province *pr = mod.provinces.get(pid);
    if (!pr || !(0 < pid && pid < md.n)) {
        paint_id.reset();
        paint_name->setText(pid ? u"not a province"_s : u"right-click the map to pick"_s);
        return;
    }
    paint_id = pid;
    if (!from_spin) {
        paint_spin->blockSignals(true);
        paint_spin->setValue(int(pid));
        paint_spin->blockSignals(false);
    }
    paint_name->setText(pname(*pr) + (pr->sea ? u" (sea)"_s : QString()));
    paint_spin->setToolTip(QString());
    const auto [r, g, b] = pr->rgb;
    paint_name->setToolTip(u"colour %1;%2;%3"_s.arg(r).arg(g).arg(b));
    _show_island();
}

void DrawPanel::_show_island()
{
    // The "Has islands" row for the province painted with (land only): ticked if it carries the island flag,
    // with its separate parts counted from the current pixels.
    const std::optional<long long> pid = paint_id;
    const moddata::Province *pr = pid && *pid ? mod.provinces.get(*pid) : nullptr;
    history::IslandFlags *isl = doc && doc->maps.contains(u"islands"_s)
                                    ? &doc->map<history::IslandFlags>(u"islands"_s) : nullptr;
    const bool show = pr && !pr->sea && isl && tool() != u"clip";
    island_lbl->setVisible(show);
    island_host->setVisible(show);
    if (!show)
        return;
    island_cb->blockSignals(true);
    island_cb->setChecked(isl->marked.contains(*pid));
    island_cb->blockSignals(false);
    const bool has_file = doc->hist->main_file(*pid) != nullptr;
    island_cb->setEnabled(has_file);
    const auto parts = mapcheck::province_part_sizes(md, *pid);
    const QString name = u"#%1 %2"_s.arg(*pid).arg(pname(*pr));
    QStringList sz;
    for (size_t i = 0; i < std::min<size_t>(6, parts.size()); ++i)
        sz << fmt(parts[i]);
    const QString sizes = sz.join(u" + "_s) + (parts.size() > 6 ? u" …"_s : QString());
    QString txt;
    if (!has_file)
        txt = u"<span style='color:%1'>⚠ %2 has no history file to hold the flag</span>"_s.arg(theme::ERROR_, name);
    else if (parts.size() > 1 && !isl->marked.contains(*pid))
        txt = u"<span style='color:rgb(240, 170, 60)'>⚠ %1 is in %2 parts: %3 px — tick the box if the others "
              "are islands</span>"_s.arg(name).arg(parts.size()).arg(sizes);
    else if (parts.size() > 1)
        txt = u"%1: %2 parts (%3 px), marked as having islands"_s.arg(name).arg(parts.size()).arg(sizes);
    else if (!parts.empty())
        txt = u"%1: one connected area%2"_s.arg(
            name, isl->marked.contains(*pid) ? u" — the island flag isn't needed"_s : QString());
    else
        txt = u"%1 isn't on the map"_s.arg(name);
    island_note->setText(txt);
}

void DrawPanel::_use_selection()
{
    std::vector<long long> ids = sel;
    ids.insert(ids.end(), sea_sel.begin(), sea_sel.end());
    if (!ids.empty())
        set_paint(ids[0]);
}

void DrawPanel::refresh(document::Document &d, const std::vector<long long> &s, const std::vector<long long> &ss)
{
    doc = &d;
    sel = s;
    sea_sel = ss;
    if (_del_armed && *_del_armed != delete_targets())
        _disarm_delete();                         // (other provinces now: the question is asked anew)
    if (paint_id && !mod.provinces.contains(*paint_id))
        set_paint(0);                             // e.g. a new province that was undone
    if (!paint_id && (!sel.empty() || !sea_sel.empty()))
        set_paint(!sel.empty() ? sel[0] : sea_sel[0]);
    _show_island();
    provbmp::ProvinceBitmap &b = bmp();
    long long n = 0;
    for (long long i = 0; i < b.version; ++i)
        n += (long long)b.diffs[size_t(i)].idx.size();
    QString text = b.version ? u"%1 stroke%2 · %3 pixel%4 changed — written to provinces.bmp on Apply"_s.arg(b.version)
                                   .arg(s_(b.version))
                                   .arg(n)
                                   .arg(s_(n))
                             : u"No changes to provinces.bmp yet."_s;
    std::vector<layerbmp::IndexedBitmap *> lays;
    for (const QString &k : {u"terrain_bmp"_s, u"rivers_bmp"_s})
        if (d.maps.contains(k) && !d.map<layerbmp::IndexedBitmap>(k).dirty().isEmpty())
            lays.push_back(&d.map<layerbmp::IndexedBitmap>(k));
    if (!lays.empty()) {
        long long px = 0;
        for (long long i = 0; i < lays[0]->version; ++i)
            px += (long long)lays[0]->diffs[size_t(i)].idx.size();
        QStringList names;
        for (auto *l : lays)
            names << l->filename;
        text += u"\nLand ↔ sea: %1 follow%2 (%3 px), also written on Apply."_s.arg(names.join(u" and "_s))
                    .arg(lays.size() > 1 ? QString() : u"s"_s)
                    .arg(px);
    }
    status->setText(text);
    Q_EMIT overlayChanged();                      // the Draw mode shows no adjacency / position markers
    _show_gone();
    _show_new();
    if (layer() == u"terrain")
        terrain_box->refresh(doc);
    else if (layer() == u"rivers")
        river_box->refresh(doc);
    else if (layer() == u"colours")
        colour_box->refresh(doc);
    if (layer() != u"colours")
        try {
            mm_box->doc = doc;
            mm_box->refresh(layer());
        } catch (const std::exception &e) {       // (a map that can't be read: the box says nothing, the page works)
            mm_box->how->setText(u"Couldn't compare the maps: %1"_s.arg(QString::fromUtf8(e.what())));
        }
    if (!_clean_cache || _clean_cache->key != _clean_key()) {
        if (!_clean_cache) {
            _update_clean();
        } else {
            clean_info->setText(u"counting…"_s);
            stray_btn->setEnabled(false);
            zero_btn->setEnabled(false);
            _clean_timer->start();
        }
        return;
    }
    _show_clean();
}

void DrawPanel::_update_clean()
{
    if (prov_handler.touched) {                   // still painting: try again later
        _clean_timer->start();
        return;
    }
    auto key = _clean_key();
    const auto &flat = md.idmap.flat();
    std::vector<std::pair<Flat, std::int64_t>> stray;
    const mapcheck::Pieces &pieces = mapcheck::pieces_of(md);     // (counted once per map change, shared)
    for (auto &pc : provbmp::small_pieces(md.idmap, STRAY_LIMIT, std::nullopt, &pieces))
        if (!key.second.contains((long long)flat[size_t(pc.first[0])]))
            stray.push_back(std::move(pc));
    _clean_cache = CleanCache{std::move(key), std::move(stray), provbmp::small_pieces(md.idmap, STRAY_LIMIT, 0, &pieces)};
    _show_clean();
}

std::pair<long long, QSet<long long>> DrawPanel::_clean_key() const
{
    // The map version and the provinces marked as having islands (their separate bits are meant to be).
    if (doc && doc->maps.contains(u"islands"_s))
        return {md.version, doc->map<history::IslandFlags>(u"islands"_s).marked};
    return {md.version, {}};
}

void DrawPanel::_show_clean()
{
    const auto &stray = _clean_cache->stray;
    const auto &zero = _clean_cache->zero;
    clean_info->setText(u"%1 stray pixel bit%2 · %3 small area%4 of no province"_s.arg(stray.size())
                            .arg(s_((long long)stray.size()))
                            .arg(zero.size())
                            .arg(s_((long long)zero.size())));
    stray_btn->setText(!stray.empty() ? u"Clean %1 stray pixel bits"_s.arg(stray.size()) : u"No stray pixel bits"_s);
    stray_btn->setEnabled(!stray.empty());
    zero_btn->setText(!zero.empty() ? u"Fill %1 small areas of no province"_s.arg(zero.size())
                                    : u"No small areas of no province"_s);
    zero_btn->setEnabled(!zero.empty());
}

// ------------------------------------------------------------------ painting
std::vector<std::uint8_t> DrawPanel::allowed(const Ids &cur, const Flat *idx)
{
    // Which of the current ids may be painted over (or erased) — idx: their flat indexes (the "only where the maps
    // disagree" rules look at terrain.bmp / rivers.bmp there).
    std::vector<std::uint8_t> ok(cur.size(), 0);
    if (tool() == u"eraser") {
        const QString e = erase->currentData().toString();
        if (e == u"target") {
            for (size_t i = 0; i < cur.size(); ++i)
                ok[i] = paint_id && (long long)cur[i] == *paint_id;
            return ok;
        }
        if (e == u"mm_terrain")
            return _mm_ok(idx, cur, {u"t_land"_s});
        for (size_t i = 0; i < cur.size(); ++i)
            ok[i] = cur[i] != 0;
        return ok;
    }
    const QString rule = over->currentData().toString();
    if (rule == u"mm_terrain" || rule == u"mm_rivers") {
        const bool land = paint_id && 0 < *paint_id && *paint_id < md.n && !md.sea[size_t(*paint_id)];
        QString kind;
        if (rule == u"mm_terrain")
            kind = land ? u"t_sea"_s : u"t_land"_s;
        else
            kind = land ? u"r_sea"_s : u"r_water"_s;
        return _mm_ok(idx, cur, {kind});
    }
    const auto &sea = md.sea;
    if (rule == u"land") {
        for (size_t i = 0; i < cur.size(); ++i)
            ok[i] = !sea[cur[i]] && cur[i] != 0;
    } else if (rule == u"sea") {
        for (size_t i = 0; i < cur.size(); ++i)
            ok[i] = sea[cur[i]] && cur[i] != 0;
    } else if (rule == u"selected") {
        std::set<std::uint16_t> want;
        for (long long p : sel)
            want.insert(std::uint16_t(p));            // (np.array(…, dtype=cur.dtype))
        for (long long p : sea_sel)
            want.insert(std::uint16_t(p));
        for (size_t i = 0; i < cur.size(); ++i)
            ok[i] = want.count(cur[i]) > 0;
    } else if (rule == u"unassigned") {
        for (size_t i = 0; i < cur.size(); ++i)
            ok[i] = cur[i] == 0;
    } else {
        std::fill(ok.begin(), ok.end(), std::uint8_t(1));
    }
    return ok;
}

mismatch::Mismatch *DrawPanel::mismatch()
{
    if (!doc)
        return nullptr;
    if (!_mm || _mm_doc != doc || _mm_ls != landsea) {
        _mm = std::make_unique<mismatch::Mismatch>(mod, md, landsea);
        _mm_doc = doc;
        _mm_ls = landsea;
    }
    return _mm.get();
}

std::vector<std::uint8_t> DrawPanel::_mm_ok(const Flat *idx, const Ids &cur, const QStringList &kinds)
{
    mismatch::Mismatch *mm = mismatch();
    std::vector<std::uint8_t> ok(cur.size(), 0);
    if (!idx || !mm)
        return ok;
    const mismatch::Masks m = mm->at(*idx);
    for (const QString &k : kinds)
        if (const auto *g = m.get(k))
            for (size_t i = 0; i < ok.size(); ++i)
                ok[i] |= g->flat()[i];
    return ok;
}

void DrawPanel::commit(const QString &label_text, const Flat &idx, const Ids &old, const Ids &nw, QString msg)
{
    provbmp::ProvinceBitmap &b = bmp();
    b.remember_file();
    std::set<long long> had;
    for (std::uint16_t p : old)
        if (p != 0 && md.area[p] > 0)
            had.insert(p);
    // land painted over sea (or sea over land): terrain.bmp and rivers.bmp follow in the same undo step
    std::vector<layerbmp::LandSea::Change> extra;
    if (landsea) {
        Flat ci;
        std::vector<long long> cv;
        for (size_t i = 0; i < idx.size(); ++i)
            if (old[i] != nw[i]) {
                ci.push_back(idx[i]);
                cv.push_back(nw[i]);
            }
        if (!ci.empty())
            extra = landsea->changes(ci, cv);
    }
    QStringList lists{u"provinces_bmp"_s};
    for (const auto &c : extra)
        lists << c.bitmap->kind;
    doc->edit(label_text, {}, [&] {
        b.commit(idx, old, nw);
        for (const auto &c : extra)
            c.bitmap->apply(c.idx, c.vals);
    }, {}, {}, lists);
    if (!extra.empty()) {
        QStringList names;
        size_t most = 0;
        for (const auto &c : extra) {
            names << c.bitmap->filename;
            most = std::max(most, c.idx.size());
        }
        msg += u" Land ↔ sea: %1 follow (%2 px)."_s.arg(names.join(u" and "_s)).arg(most);
    }
    if (!msg.isEmpty())
        Q_EMIT message(msg.trimmed());
    std::vector<long long> gone;
    for (long long p : had)
        if (md.area[size_t(p)] == 0)
            gone.push_back(p);
    if (!gone.empty())
        _warn_gone(gone);
}

// ------------------------------------------------------------------ provinces no longer on the map
std::vector<long long> DrawPanel::missing() const
{
    // Provinces with no pixels (new provinces not painted yet are shown under 'New provinces').
    const newprov::ProvinceDefs *defs_ = doc && doc->maps.contains(u"definitions"_s) ? &defs() : nullptr;
    std::vector<long long> out;
    for (const auto &[p, pr] : mod.provinces)
        if (0 < p && p < md.n && md.area[size_t(p)] == 0 && !(defs_ && defs_->added.contains(p)))
            out.push_back(p);
    std::sort(out.begin(), out.end());
    return out;
}

void DrawPanel::_show_gone()
{
    const std::vector<long long> miss = missing();
    gone_title->setVisible(!miss.empty());
    gone_list->setVisible(!miss.empty());
    std::vector<long long> have;
    for (int i = 0; i < gone_list->count(); ++i)
        have.push_back(gone_list->item(i)->data(Qt::UserRole).toLongLong());
    if (have == miss)
        return;
    gone_list->clear();
    if (miss.empty())
        return;
    gone_title->setText(u"⚠ Not on the map (%1) — click one to paint with it:"_s.arg(miss.size()));
    for (long long p : miss) {
        const moddata::Province &pr = *mod.provinces.get(p);
        auto *it = new QListWidgetItem(u"#%1 %2%3"_s.arg(p).arg(pname(pr), pr.sea ? u" (sea)"_s : QString()));
        it->setData(Qt::UserRole, p);
        gone_list->addItem(it);
    }
    const int rows = gone_list->count() ? gone_list->sizeHintForRow(0) : 24;
    gone_list->setFixedHeight(std::min(160, rows * int(miss.size()) + 2 * gone_list->frameWidth() + 6));
}

void DrawPanel::bring_back(long long pid)
{
    if (!pid)
        return;
    set_paint(pid);
    tool_btns[u"brush"_s]->setChecked(true);
    if (!edit_btn->isChecked())
        edit_btn->setChecked(true);
    Q_EMIT message(u"Painting with #%1 %2 — draw it back onto the map."_s.arg(pid).arg(mod.provinces.get(pid)->name));
}

void DrawPanel::_warn_gone(const std::vector<long long> &gone)
{
    QStringList parts;
    for (size_t i = 0; i < std::min<size_t>(4, gone.size()); ++i)
        parts << u"#%1 %2"_s.arg(gone[i]).arg(mod.provinces.get(gone[i])->name);
    const QString names = parts.join(u", "_s) + (gone.size() > 4 ? u" …"_s : QString());
    Q_EMIT message(u"⚠ %1 %2 no longer on the map — listed under 'Not on the map' in Draw (click to paint it "
                   "back) · Ctrl+Z undoes the stroke."_s.arg(names, gone.size() == 1 ? u"is"_s : u"are"_s));
}

// ------------------------------------------------------------------ new provinces
newprov::ProvinceDefs &DrawPanel::defs() const
{
    return doc->map<newprov::ProvinceDefs>(u"definitions"_s);
}

std::vector<long long> DrawPanel::delete_targets() const
{
    // What "Delete province" would delete: the selection — or, with none, the province picked to paint with.
    std::vector<long long> pids;
    std::set<long long> seen;
    for (const auto *v : {&sel, &sea_sel})
        for (long long p : *v)
            if (seen.insert(p).second && mod.provinces.contains(p))
                pids.push_back(p);
    if (pids.empty() && paint_id && *paint_id && mod.provinces.contains(*paint_id))
        pids = {*paint_id};
    return pids;
}

void DrawPanel::_disarm_delete()
{
    _del_armed.reset();
    del_btn->setText(DELETE_TEXT);
    del_note->hide();
}

void DrawPanel::_delete_clicked()
{
    // The first click says in red what would go; the second one, on the same provinces, deletes them.
    if (!doc)
        return;
    const std::vector<long long> pids = delete_targets();
    if (pids.empty()) {
        _del_armed.reset();
        del_note->setText(u"Nothing to delete yet: Ctrl+click the province(s) on the map (or right-click one "
                          "to pick it), then press this again."_s);
        del_note->show();
        return;
    }
    if (_del_armed != pids) {
        _del_armed = pids;
        const QStringList rows = provdelete::describe(*doc, md, pids);
        QString t = u"<b>Delete for good?</b>"_s;
        for (const QString &r : rows)
            t += u"<div style='margin-top:4px'>%1</div>"_s.arg(refs::esc(r));
        t += u"<div style='margin-top:5px'><b>Press the button again to delete.</b> Ctrl+Z brings them back "
             "until you Apply.</div>"_s;
        del_note->setText(t);
        del_note->show();
        del_btn->setText(u"✕  Delete %1 province%2 — press again"_s.arg(pids.size()).arg(s_((long long)pids.size())));
        return;
    }
    _disarm_delete();
    try {
        provdelete::delete_provinces(*doc, md, pids, u"neighbours"_s, landsea);
    } catch (const std::exception &e) {
        del_note->setText(u"⚠ Not deleted: %1"_s.arg(refs::esc(QString::fromUtf8(e.what()))));
        del_note->show();
        return;
    }
    if (paint_id && std::find(pids.begin(), pids.end(), *paint_id) != pids.end())
        set_paint(0);
    QStringList ids;
    for (size_t i = 0; i < std::min<size_t>(6, pids.size()); ++i)
        ids << u"#%1"_s.arg(pids[i]);
    Q_EMIT message(u"%1 province%2 deleted (%3) — Ctrl+Z brings %4 back. Apply writes it, closes the "
                   "numbers up and reads the mod again."_s.arg(pids.size())
                       .arg(s_((long long)pids.size()))
                       .arg(ids.join(u", "_s) + (pids.size() > 6 ? u" …"_s : QString()))
                       .arg(pids.size() == 1 ? u"it"_s : u"them"_s));
}

void DrawPanel::new_province()
{
    if (!doc)
        return;
    newprov::ProvinceDefs &d = defs();
    long long pid;
    std::pair<newprov::Rgb, QString> picked;
    try {
        pid = d.next_id();
        picked = d.pick_colour();
    } catch (const std::exception &e) {
        Q_EMIT message(u"⚠ New province: %1"_s.arg(QString::fromUtf8(e.what())));
        return;
    }
    std::vector<long long> cand;
    std::vector<long long> from = sel;
    if (paint_id && *paint_id)
        from.push_back(*paint_id);
    for (long long p : from)
        if (const auto *pr = mod.provinces.get(p); pr && !pr->sea)
            cand.push_back(p);
    newprov::NewProvinceDialog dlg(mod, pid, picked.first, picked.second,
                                   cand.empty() ? std::nullopt : std::optional<long long>(cand[0]), this);
    const bool accepted = run_dialog ? run_dialog(dlg) : dlg.exec() == QDialog::Accepted;
    if (!accepted)
        return;
    const auto v = dlg.values();
    create(v.name, v.sea, v.tmpl);
}

std::optional<long long> DrawPanel::create(const QString &name, bool sea, std::optional<long long> tmpl)
{
    // Create a new province (one undo step) and get ready to paint it. Returns its id.
    newprov::Plan pl;
    try {
        pl = newprov::plan(*doc, defs(), name, sea, tmpl);
    } catch (const std::exception &e) {
        Q_EMIT message(u"⚠ New province: %1"_s.arg(QString::fromUtf8(e.what())));
        return std::nullopt;
    }
    newprov::create_province(*doc, pl.pid, pl.rec, pl.rid, pl.continent, pl.climate, pl.want);
    _move_pops[pl.pid] = true;
    bring_back(pl.pid);
    Q_EMIT message(u"Created #%1 %2%3. Paint it onto the map, then press Finish under 'New provinces'. "
                   "Ctrl+Z undoes it."_s.arg(pl.pid)
                       .arg(name, sea || !tmpl || !mod.provinces.contains(*tmpl)
                                      ? u" (sea tile)"_s
                                      : u" from #%1 %2"_s.arg(*tmpl).arg(mod.provinces.get(*tmpl)->name)));
    return pl.pid;
}

std::pair<QString, QString> DrawPanel::status_of(long long pid) const
{
    // (key, text) of a new province: "offmap" / "todo" / "changed" / "done".
    return newprov::new_status(defs(), bmp(), md, pid);
}

std::vector<std::pair<long long, QString>> DrawPanel::unfinished() const
{
    // [(pid, text)] of new provinces that aren't on the map or not finished (for the Apply warning).
    if (!doc || !doc->maps.contains(u"definitions"_s))
        return {};
    std::vector<long long> pids;
    for (const auto &[p, r] : defs().added)
        pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    std::vector<std::pair<long long, QString>> out;
    for (long long pid : pids) {
        const auto [k, t] = status_of(pid);
        if (k != u"done")
            out.emplace_back(pid, t);
    }
    return out;
}

QString DrawPanel::_donor_text(const OrderedMap<long long, long long> &donors) const
{
    std::vector<std::pair<long long, long long>> items(donors.begin(), donors.end());
    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
    QStringList parts;
    for (size_t i = 0; i < std::min<size_t>(5, items.size()); ++i) {
        const auto [d, cnt] = items[i];
        const moddata::Province *pr = mod.provinces.get(d);
        if (d == 0)
            parts << u"no province %1 px"_s.arg(fmt(cnt));
        else if (pr)
            parts << u"#%1 %2%3 %4 px"_s.arg(d).arg(pname(*pr), pr->sea ? u" (sea)"_s : QString(), fmt(cnt));
    }
    if (donors.size() > 5)
        parts << u"…"_s;
    return parts.join(u", "_s);
}

void DrawPanel::_show_new()
{
    const bool have = doc && doc->maps.contains(u"definitions"_s);
    std::vector<std::pair<long long, const newprov::NewProv *>> added;
    if (have)
        for (const auto &[p, r] : defs().added)
            added.emplace_back(p, &r);
    std::stable_sort(added.begin(), added.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const long long version = doc ? bmp().version : 0;
    std::vector<std::tuple<long long, newprov::NewProv, long long, long long, bool>> sig;
    for (const auto &[p, r] : added) {
        const auto it = _move_pops.find(p);
        sig.emplace_back(p, *r, (long long)md.area[size_t(p)], version, it == _move_pops.end() ? true : it->second);
    }
    new_section->setVisible(!added.empty() && layer() == u"prov");
    if (_new_sig && sig == *_new_sig)
        return;
    _new_sig = std::move(sig);
    while (new_box->count()) {
        QLayoutItem *item = new_box->takeAt(0);
        if (QWidget *wdg = item->widget()) {
            wdg->hide();                          // gone at once (deleteLater only runs later)
            wdg->setParent(nullptr);
            wdg->deleteLater();
        }
        delete item;
    }
    for (const auto &[pid, rec] : added)
        new_box->addWidget(_card(pid, *rec));
}

QWidget *DrawPanel::_card(long long pid, const newprov::NewProv &rec)
{
    auto *w = new QFrame();
    w->setObjectName(u"newCard"_s);
    w->setStyleSheet(u"QFrame#newCard { border: 1px solid %1; border-radius: 6px; }"_s.arg(theme::LINE));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(4);
    v->addWidget(label(u"#%1 %2 — %3"_s.arg(pid).arg(rec.name, rec.sea ? u"sea tile"_s : u"land province"_s),
                       u"label"_s));
    const auto [key, text] = status_of(pid);
    const QString col = key == u"done" ? theme::OK : theme::ERROR_;
    QLabel *st = label(u"<span style='color:%1'>%2 %3</span>"_s.arg(col, key == u"done" ? u"✔"_s : u"⚠"_s, text),
                       u"sub"_s);
    st->setTextFormat(Qt::RichText);
    v->addWidget(st);
    const long long area = md.area[size_t(pid)];
    OrderedMap<long long, long long> donors;
    if (area) {
        donors = newprov::pixel_donors(bmp(), pid, rec.done_version);
        v->addWidget(label(u"%1 px%2"_s.arg(fmt(area), !donors.isEmpty() ? u" · drawn over %1"_s.arg(_donor_text(donors))
                                                                         : QString()),
                           u"faint"_s));
    }
    if (!rec.sea && rec.tmpl && mod.provinces.contains(*rec.tmpl)) {
        const moddata::Province &tpl = *mod.provinces.get(*rec.tmpl);
        QStringList parts{u"owner %1"_s.arg(rec.info.owner && !rec.info.owner->isEmpty() ? *rec.info.owner
                                                                                          : u"none"_s)};
        const moddata::Province *pr = mod.provinces.get(pid);
        if (pr && pr->region && !pr->region->isEmpty())
            parts << u"state %1"_s.arg(*pr->region);
        for (const QString &kind : {u"continent"_s, u"climate"_s}) {
            const QStringList names = doc->map<mapfiles::MembershipFile>(kind).of(pid);
            if (!names.isEmpty())
                parts << u"%1 %2"_s.arg(kind, names[0]);
        }
        v->addWidget(label(u"From #%1 %2: %3"_s.arg(*rec.tmpl).arg(tpl.name, parts.join(u" · "_s)), u"faint"_s));
    }
    const auto shares = !rec.sea ? newprov::pop_shares(*doc, md, donors)
                                 : std::vector<std::pair<long long, double>>{};
    if (!shares.empty()) {
        long long people = 0;
        for (const auto &[d, s] : shares)
            if (const auto *pops = doc->pops.get(d))
                for (const auto &p : *pops)
                    people += document::distribute(p.size, {1 - s, s})[1];
        auto *cb = new QCheckBox(u"Move pops along (by area): about %1 people from %2 province%3"_s.arg(fmt(people))
                                     .arg(shares.size())
                                     .arg(s_((long long)shares.size())));
        cb->setToolTip(u"Each province it was drawn over gives the same share of each of its pops as the share of "
                       "its land it lost (e.g. 20% of the pixels → 20% of every pop)."_s);
        const auto it = _move_pops.find(pid);
        cb->setChecked(it == _move_pops.end() ? true : it->second);
        connect(cb, &QCheckBox::toggled, this, [this, pid](bool on) { _move_pops[pid] = on; });
        v->addWidget(cb);
    }
    auto *row = new QHBoxLayout();
    auto *b = new QPushButton(u"Paint it"_s);
    b->setToolTip(u"Paint with #%1"_s.arg(pid));
    connect(b, &QPushButton::clicked, this, [this, pid] { bring_back(pid); });
    row->addWidget(b);
    auto *f = new QPushButton(key == u"changed" ? u"Finish again"_s : u"Finish"_s);
    f->setObjectName(key == u"todo" || key == u"changed" ? u"primary"_s : QString());
    f->setEnabled(key != u"offmap");
    f->setToolTip(u"Place the positions, add the coast crossings%1 — one undo step"_s.arg(
        rec.sea ? QString() : u" and move the pops (if ticked)"_s));
    connect(f, &QPushButton::clicked, this, [this, pid] { finish(pid); });
    row->addWidget(f, 1);
    v->addLayout(row);
    return w;
}

positions::Entry DrawPanel::_sea_entry(long long pid)
{
    // positions.txt entry for a new sea tile, like the mod's other sea tiles.
    auto &pf = doc->map<positions::PositionsFile>(u"positions"_s);
    std::vector<const positions::Entry *> sea;
    for (const auto &[p, pr] : mod.provinces)
        if (pr.sea && pf.entries.contains(p) && p != pid)
            sea.push_back(pf.entries.get(p)->get());
    positions::Entry e;
    if (!sea.empty()) {
        // (Counter(...).most_common(1): the most frequent pair, the first one seen on a tie)
        using Key = std::pair<std::optional<double>, std::optional<double>>;
        std::vector<std::pair<Key, long long>> counts;
        for (const auto *x : sea) {
            const Key k{x->text_rotation, x->text_scale};
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto &c) { return c.first == k; });
            if (it == counts.end())
                counts.emplace_back(k, 1);
            else
                ++it->second;
        }
        size_t best = 0;
        for (size_t i = 1; i < counts.size(); ++i)
            if (counts[i].second > counts[best].second)
                best = i;
        e.text_rotation = counts[best].first.first;
        e.text_scale = counts[best].first.second;
        long long with_unit = 0;
        for (const auto *x : sea)
            with_unit += x->pts.contains(u"unit"_s) ? 1 : 0;
        const auto pt = geo ? geo->inner(pid) : std::nullopt;
        if (with_unit * 2 > (long long)sea.size() && pt) {
            e.pts.insert(u"unit"_s, pf.to_game(*pt));
            e.pts.insert(u"text_position"_s, pf.to_game(*pt));
        }
    }
    return e;
}

void DrawPanel::finish(long long pid, const positions::Entry *keep)
{
    const newprov::NewProv *recp = defs().added.get(pid);
    if (!recp || md.area[size_t(pid)] == 0) {
        Q_EMIT message(u"Paint #%1 onto the map first."_s.arg(pid));
        return;
    }
    const newprov::NewProv rec = *recp;
    pospanel::PositionsPanel *pp = pos_panel;
    const OrderedMap<long long, long long> donors = newprov::pixel_donors(bmp(), pid, rec.done_version);
    const auto mp = _move_pops.find(pid);
    const auto moves = !rec.sea && (mp == _move_pops.end() || mp->second) ? newprov::pop_shares(*doc, md, donors)
                                                                          : std::vector<std::pair<long long, double>>{};
    // positions: the new province, plus the positions of the provinces it was drawn over that are now wrong
    auto &pf = doc->map<positions::PositionsFile>(u"positions"_s);
    OrderedMap<long long, positions::Entry> entries;
    std::vector<long long> fixed;
    if (keep)                                        // (C++ only: a pasted copy brings its own, moved along)
        entries.insert(pid, *keep);
    if (pp) {
        const auto valid = pp->prepare(*doc);
        const QSet<QString> placed(positions::PLACED.begin(), positions::PLACED.end());
        if (!keep && rec.sea)
            entries.insert(pid, _sea_entry(pid));
        else if (!keep)
            entries.insert(pid, pp->place_entry(pid, pf.get(pid), placed, valid));
        std::vector<long long> ds;
        for (const auto &[d, c] : donors)
            ds.push_back(d);
        std::sort(ds.begin(), ds.end());
        for (long long d : ds) {
            const moddata::Province *pr = mod.provinces.get(d);
            if (!pr || pr->sea || md.area[size_t(d)] == 0 || defs().added.contains(d))
                continue;
            const auto probs = positions::check_entry(pp->placer, md, d, pf.get(d), pr->name, double(pf.H),
                                                      pp->_coast.contains(d), pp->_hist_naval.contains(d));
            QSet<QString> kinds;
            for (const auto &[k, t] : probs)
                if (placed.contains(k))
                    kinds.insert(k);
            if (!kinds.isEmpty()) {
                entries.insert(d, pp->place_entry(d, pf.get(d), kinds, valid));
                fixed.push_back(d);
            }
        }
    }
    // coast crossings of the new province and of the provinces around it
    auto &adj = doc->map<adjacency::AdjacencyFile>(u"adjacency"_s);
    std::vector<adjacency::RowFields> add;
    QList<long long> stale;
    if (geo) {
        const auto st = adjacency::coast_status(adj, *geo);
        std::set<long long> near_;
        for (const auto &[d, c] : donors)
            near_.insert(d);
        for (long long p : md.neighbours(pid))
            near_.insert(p);
        auto ours = [&](long long a, long long b) {
            // rows of the new province, or between provinces it was drawn over and ones around it — not the
            // mod's older missing / stale rows further away
            return pid == a || pid == b || (donors.contains(a) && near_.count(b)) ||
                   (donors.contains(b) && near_.count(a));
        };
        const QString com = adj.coast_comment();
        for (const auto &[s, l] : st.missing)
            if (ours(s, l))
                add.push_back({s, l, u"sea"_s, 0, 0, com});
        for (const auto &r : st.stale)
            if (ours(r.a, r.b))
                stale << r.uid;
    }
    const long long moved = newprov::finish_province(*doc, pid, moves, entries, add, stale, bmp().version,
                                                     md.area[size_t(pid)]);
    QStringList parts{u"positions placed"_s};
    if (!fixed.empty())
        parts << u"re-placed the broken positions of %1 province%2 it was drawn over"_s.arg(fixed.size())
                     .arg(s_((long long)fixed.size()));
    parts << u"%1 coast crossing%2 added"_s.arg(add.size()).arg(s_((long long)add.size()));
    if (!stale.isEmpty())
        parts << u"%1 stale coast row%2 removed"_s.arg(stale.size()).arg(s_(stale.size()));
    if (!moves.empty())
        parts << u"%1 people moved in"_s.arg(fmt(moved));
    Q_EMIT message(u"Finished #%1 %2: %3. Written on Apply; Ctrl+Z undoes it."_s.arg(pid).arg(rec.name,
                                                                                         parts.join(u", "_s)));
}

void DrawPanel::fill_at(double x, double y)
{
    const bool eraser = tool() == u"eraser";
    if (!paint_id && needs_target()) {
        Q_EMIT message(u"Pick a province first (right-click the map)."_s);
        return;
    }
    const long long new_id = stroke_id();
    const long long xi = (long long)x, yi = (long long)y;
    if (!(0 <= xi && xi < md.w && 0 <= yi && yi < md.h))
        return;
    const long long cid = md.idmap(yi, xi);
    if (cid == new_id)
        return;
    long long x0, y0, x1, y1;
    if (cid) {
        x0 = md.x0[size_t(cid)];
        y0 = md.y0[size_t(cid)];
        x1 = md.x1[size_t(cid)] + 1;
        y1 = md.y1[size_t(cid)] + 1;
    } else {
        x0 = md.w;
        y0 = md.h;
        x1 = -1;
        y1 = -1;
        for (long long yy = 0; yy < md.h; ++yy) {
            const std::uint16_t *r = md.idmap.row(yy);
            for (long long xx = 0; xx < md.w; ++xx)
                if (r[xx] == 0) {
                    x0 = std::min(x0, xx);
                    x1 = std::max(x1, xx);
                    y0 = std::min(y0, yy);
                    y1 = std::max(y1, yy);
                }
        }
        x1 += 1;
        y1 += 1;
    }
    grid::Grid<std::uint8_t> sub(y1 - y0, x1 - x0, 0);
    for (long long yy = y0; yy < y1; ++yy)
        for (long long xx = x0; xx < x1; ++xx)
            sub(yy - y0, xx - x0) = md.idmap(yy, xx) == cid;
    const auto comp = provbmp::component(sub, yi - y0, xi - x0);
    Flat idx;
    for (long long yy = 0; yy < comp.h(); ++yy)
        for (long long xx = 0; xx < comp.w(); ++xx)
            if (comp(yy, xx))
                idx.push_back((yy + y0) * md.w + (xx + x0));
    Ids old;
    old.reserve(idx.size());
    for (std::int64_t i : idx)
        old.push_back(md.idmap.flat()[size_t(i)]);
    const auto ok = allowed(old, &idx);
    {
        Flat ki;
        Ids ko;
        for (size_t i = 0; i < idx.size(); ++i)
            if (ok[i]) {
                ki.push_back(idx[i]);
                ko.push_back(old[i]);
            }
        idx = std::move(ki);
        old = std::move(ko);
    }
    if (idx.empty()) {
        Q_EMIT message(u"Nothing to %1 there with the current '%2' setting."_s.arg(
            eraser ? u"erase"_s : u"fill"_s, eraser ? u"Erase"_s : u"Paint over"_s));
        return;
    }
    const Ids nw(idx.size(), std::uint16_t(new_id));
    if (eraser) {
        commit(u"Erase fill %1 px"_s.arg(idx.size()), idx, old, nw,
               u"Erased %1 pixels (now no province). Ctrl+Z undoes it."_s.arg(idx.size()));
        return;
    }
    commit(u"Fill %1 px → #%2"_s.arg(idx.size()).arg(*paint_id), idx, old, nw,
           u"Filled %1 pixels with #%2 %3. Ctrl+Z undoes it."_s.arg(idx.size())
               .arg(*paint_id)
               .arg(mod.provinces.get(*paint_id)->name));
}

void DrawPanel::_clean(std::optional<long long> only)
{
    if (!_clean_cache || _clean_cache->key != _clean_key())
        _update_clean();
    const auto &pieces = !only ? _clean_cache->stray : _clean_cache->zero;
    if (pieces.empty())
        return;
    const QString what = !only ? u"stray pixel bits"_s : u"small areas of no province"_s;
    Flat idx;
    Ids nw;
    for (const auto &[i, n] : pieces) {
        idx.insert(idx.end(), i.begin(), i.end());
        nw.insert(nw.end(), i.size(), std::uint16_t(n));
    }
    Ids old;
    old.reserve(idx.size());
    for (std::int64_t i : idx)
        old.push_back(md.idmap.flat()[size_t(i)]);
    const long long count = (long long)pieces.size();
    commit(u"Clean %1 %2"_s.arg(count).arg(what), idx, old, nw,
           u"Cleaned %1 %2 (%3 pixels). Ctrl+Z undoes it."_s.arg(count).arg(what).arg(idx.size()));
}

// ------------------------------------------------------------------ map overlay
std::vector<mapview::Shape> DrawPanel::shapes() const
{
    return layer() == u"rivers" ? river_box->shapes() : std::vector<mapview::Shape>{};
}

QString DrawPanel::describe(const std::optional<QVariantList> &tag) const
{
    return tag && !tag->isEmpty() && tag->at(0).toString() == u"rplan" ? river_box->describe(riverpanel::Tag::of(tag))
                                                                         : QString();
}

// ------------------------------------------------------------------ DrawHandler
mapview::ShapeLayer *DrawHandler::layer() const
{
    return p->view->shape_layer;
}

void DrawHandler::_outline(double x, double y)
{
    last_pos = {x, y};
    if (p->clip.floating())                          // (the floating provinces are what the layer shows)
        return;
    if (p->tool() == u"clip") {                      // Cut & paste: no brush
        layer()->set_temp({});
        return;
    }
    const double r = std::max(0.5, p->size_spin->value() / 2.0);
    mapview::Shape s;
    s.kind = u"circle"_s;
    s.pts = {QPointF(x, y)};
    s.radius = r;
    s.color = {255, 255, 255};
    layer()->set_temp({s});
}

void DrawHandler::preview()
{
    // Show the brush at its new size where the mouse last was on the map (or the middle of the view).
    mapview::MapView *v = p->view;
    if (!v || !p->edit_btn->isChecked())
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

bool DrawHandler::cancel()
{
    if (p->clip.floating()) {                        // Esc: pasted provinces go back where they were cut from
        Q_EMIT p->message(p->clip.back());
        return true;
    }
    const bool had = touched.has_value();
    if (touched && !touched->isEmpty()) {
        mapview::MapData &md = p->md;
        auto &flat = md.idmap.flat();
        long long bx0 = md.w, by0 = md.h, bx1 = -1, by1 = -1;
        for (const auto &[i, c] : *touched) {
            flat[size_t(i)] = std::uint16_t(c);
            const long long yy = i / md.w, xx = i % md.w;   // (np.divmod: the indexes are never negative)
            bx0 = std::min(bx0, xx);
            by0 = std::min(by0, yy);
            bx1 = std::max(bx1, xx);
            by1 = std::max(by1, yy);
        }
        const std::array<long long, 4> box{bx0, by0, bx1 + 1, by1 + 1};
        md.refresh_masks(box);
        p->view->redraw_box(box);
    }
    touched.reset();
    last.reset();
    if (p->view)
        layer()->set_temp({});
    return had;
}

void DrawHandler::_stamp(double x, double y)
{
    mapview::MapData &md = p->md;
    const Flat disc = layertools::disc(x, y, p->size_spin->value(), md.w, md.h);
    const auto &flat = md.idmap.flat();
    Flat idx;
    Ids cur;
    for (std::int64_t i : disc)
        if (!touched->contains(i)) {
            idx.push_back(i);
            cur.push_back(flat[size_t(i)]);
        }
    const long long new_id = p->stroke_id();
    const auto ok = p->allowed(cur, &idx);
    Flat ki;
    for (size_t i = 0; i < idx.size(); ++i)
        if (ok[i] && (long long)cur[i] != new_id) {
            ki.push_back(idx[i]);
            touched->insert(idx[i], cur[i]);
        }
    if (ki.empty())
        return;
    const auto box = p->bmp().paint_live(ki, new_id);
    if (box)
        p->view->redraw_box(*box);
}

bool DrawHandler::press(double x, double y, double, Qt::KeyboardModifiers mods)
{
    // pasted provinces: a left-drag anywhere moves them; only ✓ OK places them (the user, 2026-11-09: a click beside
    // them placed them by accident)
    if (p->clip.floating() && !(mods & Qt::ControlModifier)) {
        p->clip.grab(x, y);
        return true;
    }
    if (mods & Qt::ControlModifier)
        return false;
    if (p->tool() == u"clip")                        // Cut & paste: a click selects, as on the map anywhere
        return false;
    if (!p->paint_id && p->needs_target()) {
        Q_EMIT p->message(u"Pick a province first (right-click the map)%1."_s.arg(
            p->tool() == u"eraser" ? u" — or set the eraser to erase anything"_s : QString()));
        return true;
    }
    if (p->is_fill()) {
        p->fill_at(x, y);
        return true;
    }
    touched.emplace();
    last = {x, y};
    _stamp(x, y);
    return true;
}

void DrawHandler::move(double x, double y)
{
    if (p->clip.dragging()) {
        p->clip.drag(x, y);
        return;
    }
    if (p->clip.floating())
        return;
    _outline(x, y);
    if (!touched)
        return;
    const auto [lx, ly] = *last;
    const double step = std::max(0.5, p->size_spin->value() / 4.0);
    const long long n = std::max(1LL, (long long)(py::hypot(x - lx, y - ly) / step));
    for (long long i = 1; i <= n; ++i)
        _stamp(lx + (x - lx) * double(i) / double(n), ly + (y - ly) * double(i) / double(n));
    last = {x, y};
}

void DrawHandler::release(double, double, double, Qt::KeyboardModifiers)
{
    if (p->clip.dragging()) {
        p->clip.drop();
        return;
    }
    if (!touched)
        return;
    const OrderedMap<long long, long long> t = std::move(*touched);
    touched.reset();
    last.reset();
    if (t.isEmpty())
        return;
    mapview::MapData &md = p->md;
    Flat idx;
    Ids old;
    for (const auto &[i, c] : t) {
        idx.push_back(i);
        old.push_back(std::uint16_t(c));
    }
    auto &flat = md.idmap.flat();
    Ids nw(idx.size());
    for (size_t k = 0; k < idx.size(); ++k) {
        nw[k] = flat[size_t(idx[k])];
        flat[size_t(idx[k])] = old[k];             // back to the start; commit() re-applies it as one step
    }
    if (p->tool() == u"eraser") {
        p->commit(u"Erase %1 px"_s.arg(idx.size()), idx, old, nw,
                  u"Erased %1 pixels (now no province). Ctrl+Z undoes it."_s.arg(idx.size()));
        return;
    }
    p->commit(u"Brush %1 px → #%2"_s.arg(idx.size()).arg(*p->paint_id), idx, old, nw,
              u"Painted %1 pixels with #%2 %3. Ctrl+Z undoes it."_s.arg(idx.size())
                  .arg(*p->paint_id)
                  .arg(p->mod.provinces.get(*p->paint_id)->name));
}

bool DrawHandler::rclick(double x, double y, double, Qt::KeyboardModifiers)
{
    const long long pid = p->md.province_at(double((long long)x), double((long long)y));
    if (p->mod.provinces.contains(pid)) {
        p->set_paint(pid);
        Q_EMIT p->message(u"Painting with #%1 %2"_s.arg(pid).arg(p->mod.provinces.get(pid)->name));
    }
    return true;
}

bool DrawHandler::wheel(double steps, Qt::KeyboardModifiers mods)
{
    if (!(mods & drawlink::BRUSH_KEY))               // (C++: Shift; Ctrl + wheel zooms)
        return false;
    widgets::SliderField *s = p->size_spin;
    s->setValue(std::max(1.0, std::min(s->maximum(), s->value() + double(py::round_int(steps) *
                                                                         (s->value() < 10 ? 1 : 2)))));
    if (p->view) {
        const QPointF pos = p->view->mapToScene(p->view->viewport()->mapFromGlobal(QCursor::pos()));
        _outline(pos.x(), pos.y());
    }
    return true;
}

}  // namespace drawpanel
