// lookpages.py — Library → Menu & loading, and Library → Map look.
#include "ui/lookpages.h"
#include "core/document.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Qt::StringLiterals;
using widgets::label;

namespace lookpages {

const QStringList PICTURES = {u".dds"_s, u".tga"_s, u".png"_s, u".jpg"_s, u".jpeg"_s, u".bmp"_s};
const std::vector<Slot> MENU = {
    {u"gfx/interface/frontend_main_bg.dds"_s, u"Main menu background"_s, u"The picture behind the main menu."_s}};
const std::vector<Slot> MAP = {
    {u"map/terrain/colormap.dds"_s, u"Land colours"_s,
     u"The colours of the land seen from far away (laid over the terrain)."_s},
    {u"map/terrain/colormap_water.dds"_s, u"Water colours"_s, u"The colours of seas and lakes."_s},
    {u"map/terrain/colormap_political.dds"_s, u"Land under the map modes"_s,
     u"The land's shading under the political and other coloured map modes."_s},
    {u"map/terrain/colormap_no_shadow.dds"_s, u"Land colours without shadow"_s,
     u"A second set of land colours the game mixes in."_s},
    {u"map/terrain/texturesheet.tga"_s, u"Terrain tiles"_s, u"The ground textures seen close up, as one sheet of tiles."_s},
    {u"map/terrain/texturesheet2.tga"_s, u"Terrain tiles (second sheet)"_s, u"The second sheet of ground textures."_s},
    {u"map/terrain/borders.dds"_s, u"Border lines"_s, u"The lines drawn between provinces, states and nations."_s},
    {u"map/terrain/borders2.dds"_s, u"Border lines (2)"_s, u"More border line pieces."_s},
    {u"map/terrain/borders3.dds"_s, u"Border lines (3)"_s, u"More border line pieces."_s},
    {u"map/terrain/sea_normal.dds"_s, u"Waves"_s, u"The relief of the water's surface."_s},
    {u"map/terrain/map_overlay_tile.dds"_s, u"Paper overlay"_s, u"The paper pattern laid over the map from far away."_s},
    {u"map/terrain/river_body.dds"_s, u"Rivers"_s, u"The look of a river's water."_s},
};
const std::pair<QString, QString> MUSIC = {u"music/thecoronation_titletheme.mp3"_s, u"Title music"_s};

static QString ext_of(const QString &p)          // os.path.splitext(p)[1]
{
    return p.mid(pypath::splitext_stem(p).size());
}

// --------------------------------------------------------------------------------------------- the picture changed
// numpy, float32: a Python float meeting a float32 array is a float32 (NEP 50); every operation rounds to float32,
// none is fused. `c @ [0.299, 0.587, 0.114]` goes to BLAS (sgemv, one call per row of the picture): its sum order per
// column of a row, measured on this machine (OpenBLAS in numpy 2) for every row length 2 … 40 and five long ones —
// blocks of 8 columns fma(x2,l2, fma(x1,l1, x0*l0)); of what is left, a group of 4 fma(x2,l2, fma(x0,l0, x1*l1)),
// then a pair summed without fma (x0*l0 + x1*l1) + x2*l2, then a last single one as the group of 4. (A picture one
// column wide goes numpy's dot product way, which none of these orders matched: taken as a block.)
static void grey_row(const float *c, long long w, float *out)
{
    static const float L0 = 0.299f, L1 = 0.587f, L2 = 0.114f;
    auto blk = [&](const float *x) { return std::fma(x[2], L2, std::fma(x[1], L1, x[0] * L0)); };
    auto four = [&](const float *x) { return std::fma(x[2], L2, std::fma(x[0], L0, x[1] * L1)); };
    auto two = [&](const float *x) {
        const float a = x[0] * L0, b = x[1] * L1, s = a + b, t = x[2] * L2;
        return s + t;
    };
    long long x = 0;
    const long long n8 = w == 1 ? 1 : w - w % 8;
    for (; x < n8; ++x)
        out[x] = blk(c + x * 3);
    long long r = w - x;
    if (r >= 4) {
        for (long long k = 0; k < 4; ++k, ++x)
            out[x] = four(c + x * 3);
        r -= 4;
    }
    if (r >= 2) {
        for (long long k = 0; k < 2; ++k, ++x)
            out[x] = two(c + x * 3);
        r -= 2;
    }
    if (r == 1) {
        out[x] = four(c + x * 3);
        ++x;
    }
}

fourk::Pic adjust(const fourk::Pic &rgba, int bright, int contrast, int colour, const std::optional<Rgb> &tint,
                  int strength)
{
    if (!(bright || contrast || colour || (tint && strength)))
        return rgba;
    const long long h = rgba.h, w = rgba.w, n = h * w;
    std::vector<float> c(static_cast<size_t>(n * 3));
    for (long long i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k)
            c[size_t(i * 3 + k)] = float(rgba.d[size_t(i * 4 + k)]);
    std::vector<float> grey(static_cast<size_t>(n));
    auto greys = [&] {
        for (long long y = 0; y < h; ++y)
            grey_row(&c[size_t(y * w * 3)], w, &grey[size_t(y * w)]);
    };
    if (contrast) {
        const float k = float(1.0 + contrast / 100.0);
        for (float &x : c) {
            const float d = x - 128.0f, m = d * k;
            x = m + 128.0f;
        }
    }
    if (bright) {
        const float b = float(bright * 1.28);
        for (float &x : c)
            x = x + b;
    }
    if (colour) {
        greys();
        const float k = float(1.0 + colour / 100.0);
        for (long long i = 0; i < n; ++i) {
            const float g = grey[size_t(i)];
            for (int j = 0; j < 3; ++j) {
                float &x = c[size_t(i * 3 + j)];
                const float d = x - g, m = d * k;
                x = g + m;
            }
        }
    }
    if (tint && strength) {
        const float t[3] = {float(std::get<0>(*tint)), float(std::get<1>(*tint)), float(std::get<2>(*tint))};
        greys();
        const float keep = float(1 - strength / 100.0), s = float(strength / 100.0);
        for (long long i = 0; i < n; ++i) {
            const float lum = grey[size_t(i)] / 255.0f;
            const float l13 = 1.3f * lum, f = 0.35f + l13;
            for (int j = 0; j < 3; ++j) {
                float &x = c[size_t(i * 3 + j)];
                const float a = x * keep, tf = t[j] * f, b = tf * s;
                x = a + b;
            }
        }
    }
    fourk::Pic out = rgba;
    for (long long i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k)
            out.d[size_t(i * 4 + k)] = std::uint8_t(std::clamp(c[size_t(i * 3 + k)], 0.0f, 255.0f));
    return out;
}

QImage fitted(const QImage &img, int w, int h, bool crop)
{
    if (crop) {
        const QImage s = img.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        return s.copy((s.width() - w) / 2, (s.height() - h) / 2, w, h);
    }
    return img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

static fourk::Pic strided(const fourk::Pic &a, long long step)   // a[::step, ::step] (a copy)
{
    fourk::Pic out((a.h + step - 1) / step, (a.w + step - 1) / step, a.ch);
    for (long long y = 0; y < out.h; ++y)
        for (long long x = 0; x < out.w; ++x)
            for (int k = 0; k < a.ch; ++k)
                out.at(y, x, k) = a.at(y * step, x * step, k);
    return out;
}

// ---------------------------------------------------------------------------------------------------------- page
LookPage::LookPage(moddata::Mod &mod, Add add, const QString &title, const QString &intro, bool crop, QWidget *parent)
    : QWidget(parent), mod(mod), add(std::move(add)), CROP(crop)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    left->setMinimumWidth(250);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(12, 10, 6, 10);
    ll->setSpacing(6);
    ll->addWidget(label(title, u"title"_s));
    ll->addWidget(label(intro, u"sub"_s));
    lst = new QListWidget();
    lst->setIconSize(QSize(120, 68));
    lst->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lst->setWordWrap(true);
    lst->setSpacing(2);
    connect(lst, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur, QListWidgetItem *) { _picked(cur); });
    ll->addWidget(lst, 1);
    split->addWidget(left);

    auto *mid = new QWidget();
    mid->setMinimumWidth(420);
    auto *ml = new QVBoxLayout(mid);
    ml->setContentsMargins(8, 10, 8, 10);
    ml->setSpacing(6);
    head = label(QString(), u"title"_s);
    ml->addWidget(head);
    info = label(QString(), u"sub"_s);
    ml->addWidget(info);
    view = new QLabel();
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumSize(200, 150);
    view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    view->setStyleSheet(u"QLabel { background: #20242b; border: 1px solid %1; }"_s.arg(theme::LINE));
    ml->addWidget(view, 1);
    note = new QLabel(QString());
    note->setWordWrap(true);
    note->setStyleSheet(u"QLabel { color: %1; font-weight: 600; }"_s.arg(theme::WARN));
    note->hide();
    ml->addWidget(note);
    split->addWidget(mid);

    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    sc->setMinimumWidth(390);
    auto *right = new QWidget();
    right->setObjectName(u"sideInner"_s);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 9);
    split->setStretchFactor(2, 4);
    split->setSizes({300, 1000, 420});

    auto *s = new widgets::Section();
    s->lay->addWidget(_bold(u"Another picture"_s));
    replace_btn = new QPushButton(u"Replace with a picture…"_s);
    replace_btn->setObjectName(u"primary"_s);
    connect(replace_btn, &QPushButton::clicked, this, [this] { _replace(); });
    s->lay->addWidget(replace_btn);
    export_btn = new QPushButton(u"Save as PNG…"_s);
    export_btn->setToolTip(u"The picture as a PNG file, to work on in a paint program — then bring it back with "
                           u"“Replace with a picture…”"_s);
    connect(export_btn, &QPushButton::clicked, this, [this] { _export(); });
    s->lay->addWidget(export_btn);
    s->lay->addWidget(label(u"Any PNG, JPG, BMP, TGA or DDS. It is brought to the size the game expects and written "
                            u"as the kind of file that is there now."_s,
                            u"sub"_s));
    rl->addWidget(s);
    extra = new widgets::Section();
    extra->hide();
    rl->addWidget(extra);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Change this picture"_s));
    _tick = new QTimer(this);
    _tick->setSingleShot(true);
    for (const auto &[key, name] : std::vector<std::pair<QString, QString>>{
             {u"bright"_s, u"Brightness"_s}, {u"contrast"_s, u"Contrast"_s}, {u"colour"_s, u"Colour strength"_s},
             {u"strength"_s, u"Tint"_s}}) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        QLabel *lb = label(name, u"sub"_s, false);
        lb->setFixedWidth(110);
        row->addWidget(lb);
        auto *sl = new QSlider(Qt::Horizontal);
        sl->setRange(key == u"strength" ? 0 : -100, 100);
        sl->setValue(0);
        connect(sl, &QSlider::valueChanged, this, [this](int) { _tick->start(60); });
        row->addWidget(sl, 1);
        QLabel *val = label(u"0"_s, u"sub"_s, false);
        val->setFixedWidth(34);
        connect(sl, &QSlider::valueChanged, val, [val](int v) { val->setText(QString::number(v)); });
        row->addWidget(val);
        s->lay->addLayout(row);
        sliders.insert(key, sl);
    }
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    row->addWidget(label(u"Tint colour"_s, u"sub"_s, false));
    for (const auto &[name, rgb] : std::vector<std::pair<QString, Rgb>>{
             {u"sepia"_s, {176, 140, 92}}, {u"green"_s, {96, 150, 86}}, {u"blue"_s, {84, 128, 190}},
             {u"grey"_s, {140, 140, 140}}, {u"red"_s, {176, 86, 76}}, {u"gold"_s, {201, 161, 90}}}) {
        auto *b = new QPushButton();
        b->setFixedSize(30, 26);
        b->setToolTip(name);
        b->setCheckable(true);
        b->setStyleSheet(u"QPushButton { background: rgb(%1,%2,%3); border: 2px solid #0a0a0a; border-radius: 4px; }"
                         u"QPushButton:checked { border: 3px solid %4; }"_s.arg(std::get<0>(rgb))
                             .arg(std::get<1>(rgb))
                             .arg(std::get<2>(rgb))
                             .arg(theme::INK));
        connect(b, &QPushButton::clicked, this, [this, c = rgb, b] { _set_tint(c, b); });
        row->addWidget(b);
        tint_btns.push_back(b);
    }
    row->addStretch(1);
    s->lay->addLayout(row);
    use_btn = new QPushButton(u"Use this look"_s);
    use_btn->setObjectName(u"primary"_s);
    connect(use_btn, &QPushButton::clicked, this, [this] { _use(); });
    s->lay->addWidget(use_btn);
    reset_btn = new QPushButton(u"Sliders back to 0"_s);
    // (Python connects clicked to _reset itself: PySide hands it the button's checked = False as `show`, so the
    // sliders go back to 0 but the picture shown and the Use button stay as they were until the next change)
    connect(reset_btn, &QPushButton::clicked, this, [this](bool checked) { _reset(checked); });
    s->lay->addWidget(reset_btn);
    rl->addWidget(s);
    s = new widgets::Section();
    s->lay->addWidget(_bold(u"Good to know"_s));
    s->lay->addWidget(label(
        u"Every change is a pending edit with its own undo step; Apply writes the file into the mod (what it replaces "
        u"is kept in the backups). In a submod it goes into the submod — these files do not change the game's "
        u"checksum, so such a submod is for you alone.\n\n"
        u"The game keeps a drawn copy of the map: after a change to a map picture, delete the folder map\\cache in "
        u"the mod's settings folder (Documents › Paradox Interactive › Victoria II) if the game still shows the old "
        u"look."_s,
        u"sub"_s));
    rl->addWidget(s);
    rl->addStretch(1);
    connect(_tick, &QTimer::timeout, this, [this] { _show(); });
}

QLabel *LookPage::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------------------------------------------ the list
std::vector<Slot> LookPage::slots() { return {}; }

moreviews::AssetEdits *LookPage::_assets() const
{
    return doc && doc->maps.contains(u"assets"_s) ? &doc->map<moreviews::AssetEdits>(u"assets"_s) : nullptr;
}

std::optional<QString> LookPage::_file(const QString &r) const
{
    // The file the game reads for a slot (the mod's own, else the game's; .dds for .tga and back) or None.
    const auto hit = fourk::Files(mod).find(r);
    return hit ? std::optional<QString>(hit->second) : std::nullopt;
}

const moreviews::PicData *LookPage::_pending(const QString &r) const
{
    moreviews::AssetEdits *a = _assets();
    const moreviews::Asset *ent = a ? a->fresh.get(r) : nullptr;
    return ent && ent->how == u"pic" ? ent->pic.get() : nullptr;
}

void LookPage::refresh(document::Document *d)
{
    doc = d;
    moreviews::AssetEdits *a = _assets();
    QStringList keys, rels;
    if (a)
        keys = a->dirty();                         // (sorted(a.new))
    for (const Slot &s : slots())
        rels << s.rel;
    const auto sig = std::make_pair(keys, rels);
    if (sig == _sig && lst->count())
        return;
    _sig = sig;
    const std::optional<QString> keep = rel;
    lst->blockSignals(true);
    lst->clear();
    QListWidgetItem *pick = nullptr;
    for (const Slot &s : slots()) {
        auto *it = new QListWidgetItem(s.title);
        it->setData(Qt::UserRole, QStringList{s.rel, s.title, s.tip});
        it->setToolTip(u"%1\n%2"_s.arg(s.tip, s.rel));
        if (_pending(s.rel)) {
            it->setText(s.title + u"   ● changed"_s);
            it->setForeground(QColor(theme::GOLD));
        }
        lst->addItem(it);
        if (keep && s.rel == *keep)
            pick = it;
    }
    lst->blockSignals(false);
    lst->setCurrentItem(pick ? pick : (lst->count() ? lst->item(0) : nullptr));
    QTimer::singleShot(0, this, [this] { _thumbs(); });
}

void LookPage::_thumbs()
{
    // The small pictures of the list (read once each).
    for (int i = 0; i < lst->count(); ++i) {
        QListWidgetItem *it = lst->item(i);
        const QString r = it->data(Qt::UserRole).toStringList()[0];
        const moreviews::PicData *pend = _pending(r);
        const auto key = std::make_pair(r, static_cast<const void *>(pend));
        if (!_thumb.count(key)) {
            try {
                const fourk::Pic rgba = pend ? pend->rgba : _read(r).first;
                const long long step = std::max<long long>(1, rgba.w / 240);
                _thumb[key] = QIcon(QPixmap::fromImage(fourk::to_qimage(strided(rgba, step))
                                                           .scaled(120, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            } catch (const std::exception &) {
                _thumb[key] = std::nullopt;
            }
        }
        if (_thumb[key])
            it->setIcon(*_thumb[key]);
    }
}

std::pair<fourk::Pic, fourk::Info> LookPage::_read(const QString &r) const
{
    const std::optional<QString> path = _file(r);
    if (!path)
        throw std::runtime_error("not found");
    return fourk::read_picture(*path);
}

void LookPage::_picked(QListWidgetItem *cur)
{
    if (!cur)
        return;
    const QStringList d = cur->data(Qt::UserRole).toStringList();
    const QString &r = d[0], &title = d[1], &tip = d[2];
    rel = r;
    note->hide();
    _reset(false);
    const moreviews::PicData *pend = _pending(r);
    try {
        auto [src, inf] = _read(r);
        _src = std::move(src);
        _info = std::move(inf);
    } catch (const std::exception &e) {
        _src.reset();
        _info.reset();
        head->setText(title);
        info->setText(u"%1 — could not be read (%2)."_s.arg(r, QString::fromUtf8(e.what())));
        view->setPixmap(QPixmap());
        return;
    }
    _work = pend ? pend->rgba : *_src;
    const QString path = *_file(r);
    const bool own = mod.in_mod(path);
    const long long h = _work->h, w = _work->w;
    head->setText(title);
    info->setText(u"%1 — %2 · %3 × %4 · %5%6"_s.arg(tip, r)
                      .arg(w)
                      .arg(h)
                      .arg(own ? u"the mod's own file"_s : u"the game's file (the mod has none yet)"_s,
                           pend ? u" · CHANGED, not applied yet"_s : u""_s));
    const long long step = std::max<long long>(1, std::max(w / 1400, h / 900));
    _base = strided(*_work, step);
    _show();
    fill_extra(r);
}

void LookPage::fill_extra(const QString &) { extra->hide(); }

// ------------------------------------------------------------------------------------------------------ changing
LookPage::Values LookPage::_values() const
{
    return {sliders.value(u"bright"_s)->value(), sliders.value(u"contrast"_s)->value(),
            sliders.value(u"colour"_s)->value(), _tint, _tint ? sliders.value(u"strength"_s)->value() : 0};
}

void LookPage::_show()
{
    if (!_base)
        return;
    const Values v = _values();
    const QImage img = fourk::to_qimage(adjust(*_base, v.bright, v.contrast, v.colour, v.tint, v.strength));
    const QPixmap pm = QPixmap::fromImage(img);
    view->setPixmap(pm.scaled(view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    use_btn->setEnabled(v.bright || v.contrast || v.colour || v.strength);
}

void LookPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    _tick->start(30);
}

void LookPage::_set_tint(const Rgb &rgb, QPushButton *btn)
{
    for (QPushButton *b : tint_btns)
        b->setChecked(b == btn);
    _tint = rgb;
    if (sliders.value(u"strength"_s)->value() == 0)
        sliders.value(u"strength"_s)->setValue(35);
    _tick->start(30);
}

void LookPage::_reset(bool show)
{
    for (const auto &[_k, s] : sliders) {
        s->blockSignals(true);
        s->setValue(0);
        s->blockSignals(false);
    }
    for (QPushButton *b : tint_btns)
        b->setChecked(false);
    _tint.reset();
    if (show)
        _show();
}

void LookPage::queue(const QString &what, const fourk::Pic &rgba)
{
    // A new picture for the slot picked: a pending edit.
    if (!rel || !_info)
        return;
    const std::optional<QString> path = _file(*rel);
    const QString r = !path ? *rel : rel->left(std::max<qsizetype>(0, rel->size() - 4)) + py::lower(ext_of(*path));
    _sig.reset();
    OrderedMap<QString, moreviews::Asset> files;
    moreviews::Asset a;
    a.how = u"pic"_s;
    a.pic = std::make_shared<const moreviews::PicData>(moreviews::PicData{rgba, *_info});
    files.insert(r, a);
    add(u"%1: %2"_s.arg(lst->currentItem()->data(Qt::UserRole).toStringList()[1], what), files);
    QTimer::singleShot(0, this, [this] { refresh(doc); });
}

void LookPage::_use()
{
    if (!_work)
        return;
    const Values v = _values();
    auto sgn = [](int x) { return (x >= 0 ? u"+"_s : u""_s) + QString::number(x); };   // %+d
    QStringList bits;
    if (v.bright)
        bits << u"brightness "_s + sgn(v.bright);
    if (v.contrast)
        bits << u"contrast "_s + sgn(v.contrast);
    if (v.colour)
        bits << u"colour "_s + sgn(v.colour);
    if (v.strength)
        bits << u"tint %1 %"_s.arg(v.strength);
    queue(bits.join(u", "_s), adjust(*_work, v.bright, v.contrast, v.colour, v.tint, v.strength));
}

void LookPage::_replace()
{
    if (!_info)
        return;
    const QString path = QFileDialog::getOpenFileName(this, u"A picture for: %1"_s.arg(head->text()), QString(),
                                                      u"Pictures (*.png *.jpg *.jpeg *.bmp *.tga *.dds)"_s);
    if (!path.isEmpty())
        take_file(path);
}

void LookPage::take_file(const QString &path)
{
    fourk::Pic rgba;
    try {
        rgba = fourk::read_picture(path).first;
    } catch (const std::exception &e) {
        note->setText(u"⚠ %1 could not be read as a picture (%2)."_s.arg(pypath::basename(path), QString::fromUtf8(e.what())));
        note->show();
        return;
    }
    const long long h = _src->h, w = _src->w;
    if (rgba.h != h || rgba.w != w)
        rgba = fourk::qimage_rgba(fitted(fourk::to_qimage(rgba), int(w), int(h), CROP));
    if (!_info->alpha.value_or(true) || _info->bits == 24)
        for (long long i = 0; i < rgba.h * rgba.w; ++i)
            rgba.d[size_t(i * 4 + 3)] = 255;
    note->hide();
    queue(u"replaced by %1"_s.arg(pypath::basename(path)), rgba);
}

void LookPage::_export()
{
    if (!_work)
        return;
    const QString path = QFileDialog::getSaveFileName(this, u"Save as PNG"_s,
                                                      pypath::splitext_stem(pypath::basename(*rel)) + u".png"_s,
                                                      u"PNG (*.png)"_s);
    if (!path.isEmpty()) {
        const bool ok = fourk::to_qimage(*_work).save(path, "PNG");
        note->setText(ok ? u"Saved as %1."_s.arg(path) : u"⚠ Could not write %1."_s.arg(path));
        note->show();
    }
}

// ------------------------------------------------------------------------------------------------ Menu & loading
MenuLookPage::MenuLookPage(moddata::Mod &mod, Add add, QWidget *parent)
    : LookPage(mod, std::move(add), u"Menu & loading"_s,
               u"The main menu's background, the loading pictures and the title music."_s, true, parent)
{
    widgets::Section *s = extra;
    s->lay->addWidget(_bold(u"Title music"_s));
    music_lbl = label(QString(), u"sub"_s);
    s->lay->addWidget(music_lbl);
    auto *b = new QPushButton(u"Another title music (MP3)…"_s);
    connect(b, &QPushButton::clicked, this, [this] { _music(); });
    s->lay->addWidget(b);
    s->lay->addWidget(label(u"The tune of the main menu is the file %1: your MP3 takes its place under that name."_s
                                .arg(MUSIC.first),
                            u"sub"_s));
}

std::vector<Slot> MenuLookPage::slots()
{
    std::vector<Slot> out = MENU;
    const auto tree = fourk::Files(mod).tree(u"gfx/loadingscreens"_s);
    QStringList rels;
    for (const auto &[r, _p] : tree)
        rels << r;
    std::sort(rels.begin(), rels.end());
    for (const QString &r : rels)
        if (std::any_of(PICTURES.begin(), PICTURES.end(), [&](const QString &e) { return r.endsWith(e); }))
            out.push_back({r, u"Loading picture %1"_s.arg(pypath::splitext_stem(pypath::basename(r)).replace(u"load_"_s, u""_s)),
                           u"One of the pictures shown while the game loads (it picks among them)."_s});
    return out;
}

void MenuLookPage::fill_extra(const QString &)
{
    moreviews::AssetEdits *a = _assets();
    const moreviews::Asset *pend = a ? a->fresh.get(MUSIC.first) : nullptr;
    const QString path = mod.p(MUSIC.first.split(u'/'));
    QString now;
    if (pend)
        now = u"%1 — CHANGED, not applied yet"_s.arg(pypath::basename(pend->path));
    else if (mod.in_mod(path) && QFileInfo(path).isFile())
        now = u"the mod's own"_s;
    else if (QFileInfo(path).isFile())
        now = u"the game's"_s;
    else
        now = u"none found"_s;
    music_lbl->setText(u"Now: "_s + now);
    extra->show();
}

void MenuLookPage::_music()
{
    const QString path = QFileDialog::getOpenFileName(this, u"The title music"_s, QString(), u"MP3 (*.mp3)"_s);
    if (!path.isEmpty())
        take_music(path);
}

void MenuLookPage::take_music(const QString &path)
{
    _sig.reset();
    OrderedMap<QString, moreviews::Asset> files;
    moreviews::Asset a;
    a.how = u"file"_s;
    a.path = path;
    files.insert(MUSIC.first, a);
    add(u"Title music: %1"_s.arg(pypath::basename(path)), files);
    QTimer::singleShot(0, this, [this] {
        refresh(doc);
        fill_extra(rel.value_or(QString()));
    });
}

// ------------------------------------------------------------------------------------------------------ Map look
MapLookPage::MapLookPage(moddata::Mod &mod, Add add, QWidget *parent)
    : LookPage(mod, std::move(add), u"Map look"_s,
               u"The pictures that colour and texture the map (map/terrain). Provinces are not touched."_s, false,
               parent)
{
}

std::vector<Slot> MapLookPage::slots()
{
    std::vector<Slot> out;
    for (const Slot &s : MAP)
        if (_file(s.rel))
            out.push_back(s);
    return out;
}

}  // namespace lookpages
