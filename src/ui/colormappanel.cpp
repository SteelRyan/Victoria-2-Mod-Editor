// Map → Draw → Colours (C++ only): the land colour map tinted around chosen provinces. See colormappanel.h.
#include "ui/colormappanel.h"

#include "core/document.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "map/mapsize.h"
#include "map/moreviews_data.h"
#include "ui/colorwheel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace colormappanel {

static QString file_key(const QString &path)
{
    const QFileInfo fi(path);
    return u"%1|%2|%3"_s.arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

// ---------------------------------------------------------------------------------------------- ColormapView
mapview::RgbImage ColormapView::colors(long long y0, long long y1, long long x0, long long x1)
{
    mapview::RgbImage rgb(y1 - y0, x1 - x0);
    const QImage *land = box->shown();
    const QImage *water = box->water();
    const auto [sr, sg, sb] = mapview::SEA_RGB;
    const std::array<std::uint8_t, 3> sea_px{std::uint8_t(sr), std::uint8_t(sg), std::uint8_t(sb)};
    const colormap::Stretch ls{md.w, md.h, land ? land->width() : 1, land ? land->height() : 1};
    const colormap::Stretch ws{md.w, md.h, water ? water->width() : 1, water ? water->height() : 1};
    std::vector<long long> lc(size_t(x1 - x0)), wc(size_t(x1 - x0));
    for (long long x = x0; x < x1; ++x) {
        lc[size_t(x - x0)] = ls.col(x);
        wc[size_t(x - x0)] = ws.col(x);
    }
    for (long long y = y0; y < y1; ++y) {
        const uchar *lr = land ? land->constScanLine(int(ls.row(y))) : nullptr;
        const uchar *wr = water ? water->constScanLine(int(ws.row(y))) : nullptr;
        for (long long x = x0; x < x1; ++x) {
            auto &c = rgb(y - y0, x - x0);
            const std::uint16_t id = md.idmap(y, x);
            if (!id) {
                c = {0, 0, 0};
            } else if (md.sea[id]) {
                if (wr) {
                    const uchar *p = wr + 4 * wc[size_t(x - x0)];
                    c = {p[0], p[1], p[2]};
                } else {
                    c = sea_px;
                }
            } else if (lr) {
                const uchar *p = lr + 4 * lc[size_t(x - x0)];
                c = {p[0], p[1], p[2]};
            } else {
                c = {96, 96, 96};
            }
        }
    }
    return rgb;
}

// ---------------------------------------------------------------------------------------------- ColormapBox
ColormapBox::ColormapBox(moddata::Mod &mod_, const mapview::MapData &md_, drawlink::Panel *panel_, QWidget *parent)
    : QWidget(parent), mod(mod_), md(md_), panel(panel_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *s = new widgets::Section();
    s->lay->addWidget(widgets::label(u"Land colours (colormap.dds)"_s));
    file_note = widgets::label(QString(), u"faint"_s);
    s->lay->addWidget(file_note);
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(widgets::label(u"Tool"_s, u"sub"_s, false), 0, 0);
    auto *trow = new QHBoxLayout();
    trow->setSpacing(0);
    auto *group = new QButtonGroup(this);
    for (const auto &[key, text, tip] : std::vector<std::tuple<QString, QString, QString>>{
             {u"sel"_s, u"Selected provinces"_s,
              u"Tint the provinces selected on the map (Ctrl+click) and an area around them, fading out"_s},
             {u"brush"_s, u"Soft brush"_s, u"Paint the tint by hand — strongest in the middle of the brush"_s}}) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == u"sel");
        b->setToolTip(tip);
        b->setSizePolicy(QSizePolicy::Ignored, b->sizePolicy().verticalPolicy());
        group->addButton(b);
        tool_btns[key] = b;
        tool_keys << key;
        connect(b, &QPushButton::toggled, this, [this](bool on) {
            if (on)
                _tool_changed();
        });
        trow->addWidget(b, 1);
    }
    grid->addLayout(trow, 0, 1);
    grid->addWidget(widgets::label(u"Colour"_s, u"sub"_s, false), 1, 0);
    auto *crow = new QHBoxLayout();
    crow->setSpacing(8);
    color_btn = new colorwheel::ColorButton(u"#c8a05a"_s);
    color_btn->setToolTip(u"The hue to lay on — click for the colour wheel; right-click the map picks a colour from it"_s);
    crow->addWidget(color_btn);
    crow->addWidget(widgets::label(u"(right click on the map picks one)"_s, u"faint"_s, false), 1);
    grid->addLayout(crow, 1, 1);
    grid->addWidget(widgets::label(u"Strength"_s, u"sub"_s, false), 2, 0);
    strength = new widgets::SliderField(1, 100, 100, 60, u"%"_s);
    strength->setToolTip(u"How far the colours move towards the hue where the tint is strongest (100 %: all the way)"_s);
    grid->addWidget(strength, 2, 1);
    keep = new QCheckBox(u"Keep the light and dark of the picture"_s);
    keep->setChecked(true);
    keep->setToolTip(u"The relief, river valleys and coasts drawn into the colour map stay lighter / darker under "
                     u"the new hue. Off: one flat colour where the tint is full."_s);
    grid->addWidget(keep, 3, 1);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);

    // Selected provinces: how far around them the tint reaches
    sel_rows = new QWidget();
    auto *sg = new QGridLayout(sel_rows);
    sg->setContentsMargins(0, 6, 0, 0);
    sg->setHorizontalSpacing(8);
    sg->setVerticalSpacing(6);
    QLabel *reach_lbl = widgets::label(u"Reach"_s, u"sub"_s, false);
    sg->addWidget(reach_lbl, 0, 0);
    reach = new widgets::SliderField(0, 200, 1000, 40, u"px"_s);
    reach->setToolTip(u"How far around the provinces the tint reaches, in map pixels — it fades out to nothing there. "
                      u"Shift+wheel on the map too."_s);
    sg->addWidget(reach, 0, 1);
    apply_btn = new QPushButton(u"◐  Tint the selected provinces"_s);
    apply_btn->setObjectName(u"primary"_s);
    connect(apply_btn, &QPushButton::clicked, this, [this] { apply_selection(); });
    sg->addWidget(apply_btn, 1, 0, 1, 2);
    sel_note = widgets::label(QString(), u"faint"_s);
    sg->addWidget(sel_note, 2, 0, 1, 2);
    sg->setColumnMinimumWidth(0, reach_lbl->sizeHint().width());
    sg->setColumnStretch(1, 1);
    s->lay->addWidget(sel_rows);

    // Soft brush: its size
    brush_rows = new QWidget();
    auto *bg = new QGridLayout(brush_rows);
    bg->setContentsMargins(0, 6, 0, 0);
    bg->setHorizontalSpacing(8);
    bg->addWidget(widgets::label(u"Brush size"_s, u"sub"_s, false), 0, 0);
    size_spin = new widgets::SliderField(2, 200, 1000, 60, u"px"_s, true);
    size_spin->setToolTip(u"Brush size in map pixels — Shift+wheel on the map too."_s);
    connect(size_spin, &widgets::SliderField::valueChanged, this, [this](double) { handler.preview(); });
    bg->addWidget(size_spin, 0, 1);
    bg->setColumnStretch(1, 1);
    s->lay->addWidget(brush_rows);

    status = widgets::label(QString(), u"sub"_s);
    s->lay->addWidget(status);
    lay->addWidget(s);
    _tool_changed();
}

QString ColormapBox::tool() const
{
    for (const QString &k : tool_keys)
        if (tool_btns.at(k)->isChecked())
            return k;
    return u"sel"_s;
}

void ColormapBox::_tool_changed()
{
    const bool brush = tool() == u"brush";
    sel_rows->setVisible(!brush);
    brush_rows->setVisible(brush);
    handler.cancel();
    if (brush)
        handler.preview();
}

colormap::Tint ColormapBox::tint() const
{
    const QColor c(color_btn->color);
    colormap::Tint t;
    t.rgb = {c.red(), c.green(), c.blue()};
    t.strength = strength->value() / 100.0;
    t.keep_shading = keep->isChecked();
    return t;
}

colormap::Stretch ColormapBox::stretch() const
{
    const Source *s = const_cast<ColormapBox *>(this)->source();
    return {md.w, md.h, s ? s->pic.width() : 1, s ? s->pic.height() : 1};
}

std::vector<long long> ColormapBox::chosen() const
{
    std::vector<long long> ids = panel->sel;
    ids.insert(ids.end(), panel->sea_sel.begin(), panel->sea_sel.end());
    return ids;
}

const ColormapBox::Source *ColormapBox::source()
{
    // what is pending (a tint not applied yet) or the file the game reads (the mod's own, else the game's)
    moreviews::AssetEdits *assets = doc && doc->maps.contains(u"assets"_s) ? &doc->map<moreviews::AssetEdits>(u"assets"_s)
                                                                        : nullptr;
    const moreviews::Asset *ent = assets ? assets->fresh.get(colormap::REL) : nullptr;
    const QString disk = mod.p(colormap::REL.split(u'/'));
    QString key;
    if (ent && ent->how == u"dds")
        key = u"dds|%1|%2"_s.arg(ent->img.cacheKey()).arg(ent->path);
    else if (ent && ent->how == u"file")
        key = u"file|"_s + file_key(ent->path);
    else if (ent && ent->how == u"pic")
        key = u"pic|%1"_s.arg(quintptr(ent->pic.get()));
    else
        key = u"disk|"_s + file_key(disk);
    if (key == _src_key)
        return _src ? &*_src : nullptr;
    _src_key = key;
    _src.reset();
    Source s;
    s.path = disk;
    s.own = mod.in_mod(disk);
    if (ent && ent->how == u"dds") {
        s.pic = ent->img;
        s.path = ent->path;
        s.pending = true;
    } else if (ent && ent->how == u"file") {
        if (const auto img = mapsize::decode_dds(ent->path))
            s.pic = *img;
        s.path = ent->path;
        s.pending = true;
    } else if (ent && ent->how == u"pic" && ent->pic) {
        s.pic = fourk::to_qimage(ent->pic->rgba);      // (the Map look page: file rows too)
        s.pending = true;
    } else if (fs::exists(disk)) {
        if (const auto img = mapsize::decode_dds(disk))
            s.pic = *img;
    }
    if (s.pic.isNull())
        return nullptr;
    s.pic = s.pic.convertToFormat(QImage::Format_RGBA8888);
    _src = std::move(s);
    return &*_src;
}

const QImage *ColormapBox::water()
{
    const QString p = mod.p(colormap::WATER_REL.split(u'/'));
    const QString key = file_key(p);
    if (key != _water_key) {
        _water_key = key;
        _water.reset();
        if (const auto img = mapsize::decode_dds(p); img && !img->isNull())
            _water = img->convertToFormat(QImage::Format_RGBA8888);
    }
    return _water ? &*_water : nullptr;
}

const QImage *ColormapBox::shown()
{
    if (stroke)
        return &stroke->live;
    const Source *s = source();
    return s ? &s->pic : nullptr;
}

void ColormapBox::refresh(document::Document *doc_)
{
    doc = doc_;
    const Source *s = source();
    const std::vector<long long> ids = chosen();
    if (!s) {
        file_note->setText(u"No colormap.dds could be read (map/terrain, in the mod or the game)."_s);
        apply_btn->setEnabled(false);
        status->setText(QString());
        return;
    }
    file_note->setText(u"%1 × %2 px, stretched over the %3 × %4 map (first row = south) · %5"_s.arg(s->pic.width())
                           .arg(s->pic.height())
                           .arg(md.w)
                           .arg(md.h)
                           .arg(s->pending ? u"CHANGED — written into the mod on Apply"_s
                                : s->own   ? u"the mod's own file"_s
                                           : u"the game's file (Apply writes the mod its own)"_s));
    apply_btn->setEnabled(!ids.empty());
    sel_note->setText(ids.empty() ? u"Select provinces on the map first (Ctrl+click; Ctrl+drag adds more)."_s
                                  : u"%1 province%2 selected."_s.arg(ids.size()).arg(ids.size() == 1 ? u""_s : u"s"_s));
    status->setText(u"Every tint is one undo step (Ctrl+Z). Water is drawn from colormap_water.dds and isn't changed "
                    u"here."_s);
}

void ColormapBox::apply_selection()
{
    const Source *s = source();
    const std::vector<long long> ids = chosen();
    if (!s || ids.empty())
        return;
    const colormap::Stretch st = stretch();
    std::vector<std::uint8_t> pick(size_t(md.n), 0);
    for (long long p : ids)
        if (0 < p && p < md.n)
            pick[size_t(p)] = 1;
    const auto mask = colormap::chosen_mask(md.idmap, pick, st);
    if (std::none_of(mask.begin(), mask.end(), [](std::uint8_t v) { return v != 0; })) {
        Q_EMIT message(u"Those provinces are too small to cover a pixel of the colour map — try the soft brush."_s);
        return;
    }
    const auto dist = colormap::distance(mask, st);
    const double r = reach->value();
    std::vector<float> w(dist.size());
    for (size_t i = 0; i < w.size(); ++i)
        w[i] = colormap::fade(dist[i], r);
    const QImage out = colormap::tinted(s->pic, w, tint());
    commit(out, u"Colour map tinted: %1 province%2, reach %3 px"_s.arg(ids.size()).arg(ids.size() == 1 ? u""_s : u"s"_s)
                    .arg(r),
           u"Tinted the colour map around %1 province%2 (%3, %4 %, reach %5 px)."_s.arg(ids.size())
               .arg(ids.size() == 1 ? u""_s : u"s"_s, color_btn->color)
               .arg(strength->value())
               .arg(r));
}

void ColormapBox::commit(const QImage &pic, const QString &label_text, const QString &msg)
{
    const Source *s = source();
    if (!doc || !s || !doc->maps.contains(u"assets"_s))
        return;
    if (pic == s->pic) {
        Q_EMIT message(u"Nothing changed — the colours are already like that."_s);
        return;
    }
    moreviews::Asset a;
    a.how = u"dds"_s;
    a.img = pic;
    a.path = s->path;
    auto &assets = doc->map<moreviews::AssetEdits>(u"assets"_s);
    doc->edit(label_text, {}, [&assets, a] { assets.fresh.insert(colormap::REL, a); }, {}, {}, {u"assets"_s});
    refresh(doc);
    Q_EMIT message(msg + u" Written into the mod on Apply; Ctrl+Z undoes it."_s);
}

QString ColormapBox::pixel_info(long long x, long long y)
{
    const QImage *pic = shown();
    if (!pic || !(0 <= x && x < md.w && 0 <= y && y < md.h))
        return QString();
    const colormap::Stretch st{md.w, md.h, pic->width(), pic->height()};
    const long long c = st.col(x), r = st.row(y);
    const uchar *p = pic->constScanLine(int(r)) + 4 * c;
    const long long id = md.idmap(y, x);
    const moddata::Province *pr = mod.provinces.get(id);
    return u"colormap.dds pixel %1, %2 · %3, %4, %5%6"_s.arg(c).arg(r).arg(p[0]).arg(p[1]).arg(p[2]).arg(
        pr ? u"  ·  #%1 %2%3"_s.arg(id).arg(pr->name, md.sea[size_t(id)] ? u" (sea: colormap_water.dds)"_s : QString())
           : QString());
}

// ---------------------------------------------------------------------------------------------- ColormapHandler
mapview::MapView *ColormapHandler::view() const
{
    return b->panel->view;
}

void ColormapHandler::_outline(double x, double y)
{
    last_pos = {x, y};
    if (!view())
        return;
    if (b->tool() != u"brush") {
        view()->shape_layer->set_temp({});
        return;
    }
    mapview::Shape s;
    s.kind = u"circle"_s;
    s.pts = {QPointF(x, y)};
    s.radius = std::max(0.5, b->size_spin->value() / 2.0);
    s.color = {255, 255, 255};
    view()->shape_layer->set_temp({s});
}

void ColormapHandler::preview()
{
    mapview::MapView *v = view();
    if (!v || !b->panel->edit_btn || !b->panel->edit_btn->isChecked())
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

bool ColormapHandler::cancel()
{
    const bool had = b->stroke.has_value();
    b->stroke.reset();
    last.reset();
    if (view()) {
        view()->shape_layer->set_temp({});
        if (had)
            view()->redraw_box({0, 0, b->md.w, b->md.h});
    }
    return had;
}

void ColormapHandler::_stamp(double x, double y)
{
    // the brush's weights where it is now (the strongest of the stroke so far), shown at once
    auto &st = *b->stroke;
    const colormap::Stretch s = b->stretch();
    const double r = std::max(1.0, b->size_spin->value() / 2.0);
    const long long c0 = std::max(0LL, (long long)std::floor((x - r) / s.sx()));
    const long long c1 = std::min(s.cw, (long long)std::ceil((x + r) / s.sx()) + 1);
    // file rows: row f stands for map rows (ch - 1 - f) * sy … ; the north edge of the brush is the larger row
    const long long n0 = std::max(0LL, (long long)std::floor((y - r) / s.sy()));
    const long long n1 = std::min(s.ch, (long long)std::ceil((y + r) / s.sy()) + 1);
    const long long r0 = s.ch - n1, r1 = s.ch - n0;
    for (long long f = r0; f < r1; ++f) {
        const double my = (double(s.ch - 1 - f) + 0.5) * s.sy();
        for (long long c = c0; c < c1; ++c) {
            const double mx = (double(c) + 0.5) * s.sx();
            const float wv = colormap::brush_fade(float(py::hypot(mx - x, my - y)), r);
            float &cur = st.w[size_t(f * s.cw + c)];
            cur = std::max(cur, wv);
        }
    }
    colormap::tint_into(st.live, st.base, st.w, b->tint(), st.ref, c0, r0, c1, r1);
    if (view())
        view()->redraw_box({(long long)std::floor(x - r - 2 * s.sx()), (long long)std::floor(y - r - 2 * s.sy()),
                            (long long)std::ceil(x + r + 2 * s.sx()), (long long)std::ceil(y + r + 2 * s.sy())});
}

bool ColormapHandler::press(double x, double y, double, Qt::KeyboardModifiers mods)
{
    if (mods & Qt::ControlModifier)
        return false;                                // (Ctrl+click selects provinces)
    if (b->tool() != u"brush")
        return false;                                // (a plain click selects too in that tool)
    const ColormapBox::Source *src = b->source();
    if (!src)
        return true;
    ColormapBox::Stroke st;
    st.base = src->pic;
    st.live = src->pic.copy();
    st.w.assign(size_t(src->pic.width()) * size_t(src->pic.height()), 0.0f);
    b->stroke = std::move(st);
    // the shading is kept against the brightness under the first touch (it must not drift during the stroke)
    {
        const colormap::Stretch s = b->stretch();
        const double r = std::max(1.0, b->size_spin->value() / 2.0);
        double sum = 0, n = 0;
        for (long long f = 0; f < s.ch; ++f) {
            const double my = (double(s.ch - 1 - f) + 0.5) * s.sy();
            if (std::fabs(my - y) > r)
                continue;
            const uchar *p = src->pic.constScanLine(int(f));
            for (long long c = 0; c < s.cw; ++c) {
                const double mx = (double(c) + 0.5) * s.sx();
                if (py::hypot(mx - x, my - y) <= r) {
                    sum += colormap::luma(p[4 * c], p[4 * c + 1], p[4 * c + 2]);
                    n += 1;
                }
            }
        }
        if (!n) {
            const colormap::Stretch &s2 = s;
            const uchar *p = src->pic.constScanLine(int(s2.row((long long)std::clamp(y, 0.0, double(b->md.h - 1)))));
            const long long c = s2.col((long long)std::clamp(x, 0.0, double(b->md.w - 1)));
            sum = colormap::luma(p[4 * c], p[4 * c + 1], p[4 * c + 2]);
            n = 1;
        }
        b->stroke->ref = sum / n;
    }
    last = {x, y};
    _stamp(x, y);
    return true;
}

void ColormapHandler::move(double x, double y)
{
    _outline(x, y);
    if (!b->stroke || !last)
        return;
    const auto [lx, ly] = *last;
    const double step = std::max(0.5, b->size_spin->value() / 6.0);
    const long long n = std::max(1LL, (long long)(py::hypot(x - lx, y - ly) / step));
    for (long long i = 1; i <= n; ++i)
        _stamp(lx + (x - lx) * double(i) / double(n), ly + (y - ly) * double(i) / double(n));
    last = {x, y};
}

void ColormapHandler::release(double, double, double, Qt::KeyboardModifiers)
{
    if (!b->stroke)
        return;
    const QImage pic = b->stroke->live;
    b->stroke.reset();
    last.reset();
    b->commit(pic, u"Colour map: soft brush %1 px"_s.arg(b->size_spin->value()),
              u"Painted the colour map (%1, %2 %)."_s.arg(b->color_btn->color).arg(b->strength->value()));
}

bool ColormapHandler::rclick(double x, double y, double, Qt::KeyboardModifiers)
{
    const QImage *pic = b->shown();
    const long long xi = (long long)x, yi = (long long)y;
    if (pic && 0 <= xi && xi < b->md.w && 0 <= yi && yi < b->md.h) {
        const colormap::Stretch st{b->md.w, b->md.h, pic->width(), pic->height()};
        const uchar *p = pic->constScanLine(int(st.row(yi))) + 4 * st.col(xi);
        const QString hex = QColor(p[0], p[1], p[2]).name();
        b->color_btn->set(hex);
        Q_EMIT b->message(u"Tinting with %1, picked from the colour map."_s.arg(hex));
    }
    return true;
}

bool ColormapHandler::wheel(double steps, Qt::KeyboardModifiers mods)
{
    if (!(mods & drawlink::BRUSH_KEY))
        return false;
    widgets::SliderField *s = b->tool() == u"brush" ? b->size_spin : b->reach;
    const double v = s->value(), lo = s->minimum();
    s->setValue(std::max(lo, std::min(s->maximum(), v + double(py::round_int(steps)) * (v < 20 ? 2 : v < 100 ? 5 : 10))));
    return true;
}

}  // namespace colormappanel
