#include "ui/mismatchbox.h"

#include "core/document.h"
#include "map/mismatch.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;

namespace mismatchbox {

static double now()                         // (time.time())
{
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

const std::map<QString, QString> HOW = {
    {u"prov"_s, u"Paint only on them: Paint over → “Only where terrain.bmp / rivers.bmp disagrees” (a land province "
                u"goes on land terrain, a sea tile on land without terrain) — or the Eraser → “Only land provinces "
                u"where terrain.bmp has ocean”."_s},
    {u"terrain"_s, u"Paint only on them: Paint over → “Only where the provinces disagree” — each pixel is made right "
                   u"(a sea tile's gets ocean, a land province's the colour you paint with)."_s},
    {u"rivers"_s, u"Paint only on them: the Fix tool — each pixel under it is made right (land → white 255, a sea "
                  u"tile → water 254)."_s}};

MismatchBox::MismatchBox(std::function<mismatch::Mismatch *()> mismatch_, document::Document *doc_, QWidget *parent)
    : widgets::Section(parent), mismatch(std::move(mismatch_)), doc(doc_)
{
    lay->addWidget(widgets::label(u"Where the maps disagree (land ↔ sea)"_s));
    rows = new QVBoxLayout();
    rows->setSpacing(4);
    lay->addLayout(rows);
    show_cb = new QCheckBox(u"Show them on the province map too"_s);
    show_cb->setToolTip(u"The Provinces layer shows the provinces; with this on, the pixels where terrain.bmp "
                        u"or rivers.bmp disagree are painted in their colour over them"_s);
    connect(show_cb, &QCheckBox::toggled, this, [this](bool) { Q_EMIT changed(); });
    lay->addWidget(show_cb);
    how = widgets::label(QString(), u"faint"_s);
    lay->addWidget(how);
}

void MismatchBox::refresh(const QString &layer_)
{
    layer = layer_;
    mismatch::Mismatch *mm = mismatch();
    if (!mm)
        return;
    const OrderedMap<QString, long long> counts = mm->counts();
    std::vector<std::pair<QString, long long>> items;
    for (const auto &[k, n] : counts)
        items.emplace_back(k, n);
    std::sort(items.begin(), items.end());
    std::pair<QString, std::vector<std::pair<QString, long long>>> sig{layer, items};
    show_cb->setVisible(layer == u"prov"_s);
    how->setText(HOW.at(layer));
    if (_sig && *_sig == sig)
        return;
    _sig = sig;
    while (rows->count()) {
        QLayoutItem *it = rows->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    const QStringList &of = mismatch::OF_LAYER.value(layer);
    for (const mismatch::Kind &kd : mismatch::KINDS) {
        if (!of.contains(kd.key) || !counts.contains(kd.key))
            continue;
        const long long n = counts.value(kd.key);
        auto *w = new QWidget();
        auto *row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *sw = new QLabel();
        sw->setPixmap(widgets::swatch_icon(kd.colour, 16).pixmap(16, 16));
        sw->setFixedSize(18, 18);
        row->addWidget(sw);
        QLabel *lb = widgets::label(u"%1 — <b>%2 px</b>"_s.arg(kd.text, widgets::fmt(n)), u"sub"_s);
        lb->setTextFormat(Qt::RichText);
        lb->setToolTip(kd.text);
        row->addWidget(lb, 1);
        auto *b = new QPushButton(u"Fix all ▸"_s);
        b->setEnabled(n > 0);
        b->setToolTip(u"Make every one of these pixels right — the first press says what happens, a second one "
                      u"within 10 seconds does it (one undo step, written on Apply)"_s);
        const QString kk = kd.key;
        connect(b, &QPushButton::clicked, this, [this, kk] { _fix(kk); });
        row->addWidget(b);
        rows->addWidget(w);
    }
}

void MismatchBox::_fix(const QString &kind)
{
    mismatch::Mismatch *mm = mismatch();
    if (!mm)
        return;
    const long long n = mm->counts().value(kind, 0);
    if (!n)
        return;
    static const std::map<QString, QString> WHAT = {
        {u"t_sea"_s, u"get ocean in terrain.bmp"_s},
        {u"t_land"_s, u"get plains in terrain.bmp (like newly painted land — paint them over in Terrain afterwards)"_s},
        {u"r_water"_s, u"become land (white) in rivers.bmp"_s},
        {u"r_sea"_s, u"become water in rivers.bmp"_s}};
    static const std::map<QString, QString> COLOUR = {
        {u"t_sea"_s, u"orange"_s}, {u"t_land"_s, u"yellow"_s}, {u"r_water"_s, u"pink"_s}, {u"r_sea"_s, u"green"_s}};
    const QString &what = WHAT.at(kind);
    const auto &[k0, t0] = _armed;
    if (k0 != kind || now() - t0 > 10) {
        _armed = {kind, now()};
        Q_EMIT message(u"Press “Fix all” again within 10 seconds: %1 px %2. (They are the %3 pixels on the map.)"_s
                           .arg(widgets::fmt(n), what, COLOUR.at(kind)));
        return;
    }
    _armed = {std::nullopt, 0.0};
    std::vector<mismatch::Mismatch::Fix> ch = mm->fix({kind});
    if (ch.empty())
        return;
    std::set<QString> kinds;
    for (const auto &f : ch)
        kinds.insert(f.bitmap->kind);
    doc->edit(u"Fix %1 px: %2"_s.arg(widgets::fmt(n), kind), {},
              [&] {
                  for (const auto &f : ch)
                      f.bitmap->apply(f.idx, f.vals);
              },
              {}, {}, QStringList(kinds.begin(), kinds.end()));
    _sig.reset();
    Q_EMIT message(u"%1 px %2 — Ctrl+Z undoes it, Apply writes it."_s.arg(widgets::fmt(n), what.split(u" ("_s)[0]));
    refresh(layer);
    Q_EMIT changed();
}

// ---------------------------------------------------------------------------------------------- ProvMismatchView
mapview::RgbImage ProvMismatchView::colors(long long y0, long long y1, long long x0, long long x1)
{
    const size_t q = size_t(std::max(1LL, lut.Q));   // (a "mix" map mode: its first colour)
    mapview::RgbImage rgb(y1 - y0, x1 - x0);
    for (long long y = y0; y < y1; ++y)
        for (long long x = x0; x < x1; ++x) {
            const size_t at = size_t(md.idmap(y, x)) * q * 3;
            rgb(y - y0, x - x0) = {lut.rgb[at], lut.rgb[at + 1], lut.rgb[at + 2]};
        }
    if (const auto tops = on_top(y0, y1, x0, x1))
        for (const Top &t : *tops) {
            const auto [r, g, b] = *t.rgb;
            for (size_t i = 0; i < t.mask.flat().size(); ++i)
                if (t.mask.flat()[i])
                    rgb.flat()[i] = {std::uint8_t(r), std::uint8_t(g), std::uint8_t(b)};
        }
    return rgb;
}

std::optional<std::vector<mapview::PixelLayer::Top>> ProvMismatchView::on_top(long long y0, long long y1, long long x0,
                                                                              long long x1)
{
    QStringList kinds;
    for (const mismatch::Kind &k : mismatch::KINDS)
        kinds << k.key;
    std::vector<Top> out;
    for (auto &[mask, col] : mismatch::overlay(mm, kinds, y0, y1, x0, x1))
        out.push_back(Top{std::move(mask), col, {}});
    if (out.empty())
        return std::nullopt;
    return out;
}

}  // namespace mismatchbox