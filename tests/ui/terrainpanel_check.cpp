// terrainpanel + mismatchbox against Python, the C++ side of tools/check_terrainpanel.py.
// Usage: med_terrainpanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mismatch.h"
#include "ui/mapview.h"
#include "ui/mismatchbox.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace terrainpanel;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}
static QString b(bool x) { return x ? u"True"_s : u"False"_s; }
static QString opt(const std::optional<long long> &x) { return x ? QString::number(*x) : u"None"_s; }
static QString vl(const std::vector<long long> &x) { return v(x); }

struct Panel : drawlink::Panel {                     // (the Draw panel's part the boxes read)
    Panel(moddata::Mod &m, mapview::MapData &md, layerbmp::LandSea *ls) : m(m), md(md), ls(ls) {}
    moddata::Mod &m;
    mapview::MapData &md;
    layerbmp::LandSea *ls;
    std::unique_ptr<mismatch::Mismatch> mm;
    mismatch::Mismatch *mismatch() override
    {
        if (!mm)
            mm = std::make_unique<mismatch::Mismatch>(m, md, ls);
        return mm.get();
    }
    void zoom_requested(double, double) override {}
};

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;

    Panel panel(*m, md, win.landsea.get());
    panel.view = win.map;
    QPushButton edit;
    edit.setCheckable(true);
    edit.setChecked(true);
    panel.edit_btn = &edit;
    QStringList out, msgs;
    auto &tb = doc.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s);
    TerrainBox box(*m, md, &panel);
    QObject::connect(&box, &TerrainBox::message, [&](const QString &t) { msgs << t; });
    box.setFixedWidth(470);
    box.refresh(&doc);
    box.show();
    QApplication::processEvents();

    auto data_h = [&] { return h(tb.data->flat().data(), tb.data->flat().size()); };
    auto state = [&](const QString &what) {
        out << u"%1: version %2 data %3 index %4 status %5"_s.arg(what).arg(tb.version).arg(data_h(), opt(box.index),
                                                                                         v(box.status->text()));
        out << u"  pick "_s + v(box.pick_info->text());
        out << u"  msgs "_s + v(msgs);
        msgs.clear();
    };
    {
        QStringList sw;
        for (const auto &[i, s] : box.swatches)
            sw << L(i, s->toolTip(), s->styleSheet());
        out << u"swatches "_s + pyform::list(sw);
        QStringList ty;
        for (const auto &[i, t] : box.types)
            ty << L(i, t);
        out << u"types "_s + pyform::list(ty);
    }
    state(u"start"_s);

    auto options = [&](const QString &what) {
        std::vector<long long> lv;
        for (const auto &[j, btn] : box.level_btns)
            lv.push_back(j);
        QStringList vis;
        for (QWidget *w : {(QWidget *)box.levels_lbl, box.ring_host, (QWidget *)box.density, (QWidget *)box.gradient,
                           (QWidget *)box.preview_lbl, (QWidget *)box.fill_note})
            vis << b(w->isVisible());
        out << u"options %1 style %2 fill %3 tool %4 levels %5 chosen %6 order %7 fill_btn %8 %9 note %10 vis %11"_s
                   .arg(what, box.style(), box.style_for_fill(), box.tool(), vl(lv), vl(box.chosen_levels()),
                        v(box.order_lbl->text()), v(box.fill_sel_btn->text()), b(box.fill_sel_btn->isEnabled()))
                   .arg(v(box.fill_note->text()), u"["_s + vis.join(u", "_s) + u"]"_s);
    };
    options(u"start"_s);
    box.grab().save(QDir(out_dir).filePath(u"box_flat.png"_s));
    std::vector<long long> land, seas;
    for (long long p = 1; p < md.n; ++p) {
        if (!md.sea[size_t(p)] && md.area[size_t(p)] > 0 && m->provinces.contains(p))
            land.push_back(p);
        if (md.sea[size_t(p)] && md.area[size_t(p)] > 0)
            seas.push_back(p);
    }
    auto pixel = [&](long long p, long long k) {    // the k-th pixel of p in row order
        std::vector<std::pair<long long, long long>> pts;
        for (long long y = md.y0[size_t(p)]; y <= md.y1[size_t(p)]; ++y)
            for (long long x = md.x0[size_t(p)]; x <= md.x1[size_t(p)]; ++x)
                if (md.idmap(y, x) == p)
                    pts.emplace_back(x, y);
        return pts[size_t(std::min<long long>(k, (long long)pts.size() - 1))];
    };
    const std::vector<long long> picks{land[3], land[40], land[120], land[260]};
    out << u"picks %1 seas %2"_s.arg(vl(picks), vl(std::vector<long long>(seas.begin(), seas.begin() + 3)));
    for (long long p : picks) {
        const auto [x, y] = pixel(p, md.area[size_t(p)] / 2);
        out << u"pixel_info %1 %2 %3"_s.arg(x).arg(y).arg(v(box.pixel_info(x, y)));
    }
    // the terrain view
    TerrainView tv(md, tb);
    for (const auto &[y0, y1, x0, x1] : {std::array<long long, 4>{0, 64, 0, 64},
                                         std::array<long long, 4>{md.h / 3, md.h / 3 + 200, md.w / 4, md.w / 4 + 300}}) {
        const mapview::RgbImage c = tv.colors(y0, y1, x0, x1);
        const auto t = tv.on_top(y0, y1, x0, x1);
        QString top = u"None"_s;
        if (t) {
            QStringList parts;
            for (const auto &tp : *t) {
                long long s = 0;
                for (auto x : tp.mask.flat())
                    s += x;
                const auto [r, g, bb] = *tp.rgb;
                parts << u"[%1, [%2, %3, %4]]"_s.arg(s).arg(r).arg(g).arg(bb);
            }
            top = u"["_s + parts.join(u", "_s) + u"]"_s;
        }
        out << u"view %1 %2 %3 %4 [%5, %6, 3] %7 top %8"_s.arg(y0).arg(y1).arg(x0).arg(x1).arg(c.h()).arg(c.w()).arg(
            h(c.flat().data(), c.flat().size() * 3), top);
    }
    // a flat fill at a click
    auto [x, y] = pixel(picks[0], 0);
    box.handler.press(double(x), double(y), 1.0, Qt::NoModifier);   // (brush: one stamp)
    box.handler.release(double(x), double(y), 1.0, Qt::NoModifier);
    state(u"brush dot"_s);
    box.tool_btns[u"fill"_s]->setChecked(true);
    options(u"fill"_s);
    box.handler.press(double(x), double(y), 1.0, Qt::NoModifier);
    state(u"flat fill"_s);
    // rings: picking them with the brush switches to Fill; fill a click and the selection
    box.tool_btns[u"brush"_s]->setChecked(true);
    box.style_btns[u"rings"_s]->setChecked(true);
    options(u"rings picked with the brush"_s);
    for (const auto &[k, sw] : box.swatches)
        if (box.type_of(k) == u"hills" || box.type_of(k) == u"mountain") {
            box.set_index(k);
            break;
        }
    options(u"rings, hills"_s);
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_rings.png"_s));
    std::tie(x, y) = pixel(picks[1], md.area[size_t(picks[1])] / 3);
    box.fill_at(double(x), double(y));
    state(u"rings fill"_s);
    panel.sel = {picks[1], picks[2]};
    box._options_changed();
    options(u"2 selected"_s);
    box.ring_w->setValue(3);
    options(u"ring width 3"_s);
    box.fill_selected();
    state(u"rings selected"_s);
    box.reverse->setChecked(true);
    box.ring_auto->setChecked(true);
    options(u"reverse, auto"_s);
    box.fill_selected();
    state(u"rings selected reverse"_s);
    // patches
    box.style_btns[u"patches"_s]->setChecked(true);
    {
        std::vector<QPushButton *> lb;
        for (const auto &[j, btn] : box.level_btns)
            lb.push_back(btn);
        for (QPushButton *btn : lb)
            btn->setChecked(true);
    }
    options(u"patches"_s);
    box.density->setValue(60);
    box.psize->setValue(14);
    box.gradient->setValue(30);
    options(u"patches 60 / 14 / 30"_s);
    {
        const auto [order, w] = box.patch_levels();
        const auto [base, acc] = box._patch_order();
        out << u"patch levels [%1, %2] order [%3, %4]"_s.arg(vl(order), v(w)).arg(base).arg(vl(acc));
        const QImage pm = box.preview_lbl->pixmap().toImage();
        out << u"preview %1 %2 %3"_s.arg(pm.width()).arg(pm.height()).arg(h(pm.constBits(), size_t(pm.sizeInBytes())));
    }
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_patches.png"_s));
    box.fill_selected();
    state(u"patches selected"_s);
    panel.sel = {picks[3]};
    box.scope->setCurrentIndex(1);
    box.tool_btns[u"brush"_s]->setChecked(true);
    box.size_spin->setValue(12);
    std::tie(x, y) = pixel(picks[3], md.area[size_t(picks[3])] / 2);
    box.handler.press(double(x), double(y), 1.0, Qt::NoModifier);
    for (long long k = 1; k < 9; ++k)
        box.handler.move(double(x + 3 * k), double(y + k % 3));
    {
        QStringList sh;
        for (const auto &s : win.map->shape_layer->temp)
            sh << u"['%1', [[%2, %3]], %4]"_s.arg(s.kind, v((long long)s.pts[0].x()), v((long long)s.pts[0].y()), v(s.radius));
        out << u"outline ["_s + sh.join(u", "_s) + u"]"_s;
    }
    box.handler.release(double(x + 24), double(y + 2), 1.0, Qt::NoModifier);
    state(u"brush stroke, selected only"_s);
    const auto [x2, y2] = pixel(picks[0], 0);
    box.handler.press(double(x2), double(y2), 1.0, Qt::NoModifier);
    box.handler.release(double(x2), double(y2), 1.0, Qt::NoModifier);
    state(u"brush outside the selection"_s);
    box.fill_at(double(x2), double(y2));
    state(u"fill outside the selection"_s);
    for (const auto &[steps, ctrl] : {std::pair{1.0, true}, std::pair{2.5, true}, std::pair{-1.0, true},
                                      std::pair{3.5, true}, std::pair{1.0, false}}) {
        const bool r = box.handler.wheel(steps, ctrl ? drawlink::BRUSH_KEY : Qt::NoModifier);   // (Python: Ctrl)
        out << u"wheel %1 %2 -> %3 size %4"_s.arg(steps == int(steps) ? QString::number(int(steps)) : v(steps), b(ctrl),
                                                  b(r), v((long long)box.size_spin->value()));
    }
    box.handler.rclick(double(x2), double(y2), 1.0, Qt::NoModifier);
    state(u"rclick"_s);
    // the brush cancelled half way
    box.handler.press(double(x), double(y), 1.0, Qt::NoModifier);
    box.handler.move(double(x + 5), double(y));
    {
        const bool c = box.handler.cancel();
        out << u"cancel %1 %2"_s.arg(b(c), data_h());
    }
    // mismatches made on purpose: ocean on land, land terrain on sea
    layerbmp::LandSea &ls = *win.landsea;
    ls._prepare();
    const auto [lx, ly] = pixel(picks[2], 0);
    Flat idx;
    for (long long i = 0; i < 6; ++i)
        if (md.idmap.flat()[size_t(ly * md.w + lx + i)] == picks[2])
            idx.push_back(ly * md.w + lx + i);
    box.commit(u"ocean on land"_s, idx, Vals(idx.size(), std::uint8_t(ls.ocean_fill)), u"Ocean painted."_s);
    const auto [sx, sy] = pixel(seas[0], 0);
    Flat sidx;
    for (long long i = 0; i < 5; ++i)
        if (md.idmap.flat()[size_t(sy * md.w + sx + i)] == seas[0])
            sidx.push_back(sy * md.w + sx + i);
    box.commit(u"land on sea"_s, sidx, Vals(sidx.size(), std::uint8_t(*box.index)), u"Land painted."_s);
    state(u"mismatches made"_s);
    mismatchbox::MismatchBox mb([&] { return panel.mismatch(); }, &doc);
    QObject::connect(&mb, &mismatchbox::MismatchBox::message, [&](const QString &t) { msgs << t; });
    int ch = 0;
    QObject::connect(&mb, &mismatchbox::MismatchBox::changed, [&] { ++ch; });
    mb.setFixedWidth(470);
    for (const QString &layer : {u"prov"_s, u"terrain"_s, u"rivers"_s}) {
        mb.refresh(layer);
        out << u"legend %1 rows %2 how %3 show %4"_s.arg(layer).arg(mb.rows->count()).arg(v(mb.how->text()),
                                                                                      b(mb.show_cb->isVisibleTo(&mb)));
    }
    mb.refresh(u"terrain"_s);
    mb.show();
    QApplication::processEvents();
    mb.grab().save(QDir(out_dir).filePath(u"legend.png"_s));
    auto counts = [&] {
        std::vector<std::pair<QString, long long>> c;
        for (const auto &[k, n] : panel.mismatch()->counts())
            c.emplace_back(k, n);
        std::sort(c.begin(), c.end());
        QStringList parts;
        for (const auto &[k, n] : c)
            parts << L(k, n);
        return pyform::list(parts);
    };
    out << u"counts "_s + counts();
    box.scope->setCurrentIndex(2);
    box.tool_btns[u"fill"_s]->setChecked(true);
    box.fill_at(double(lx), double(ly));
    state(u"mismatch fill"_s);
    box.fill_at(double(lx), double(ly));
    state(u"mismatch fill again"_s);
    mb._fix(u"t_sea"_s);
    out << u"fix armed "_s + v(msgs);
    mb._fix(u"t_sea"_s);
    state(u"fix t_sea"_s);
    out << u"counts %1 changed %2"_s.arg(counts()).arg(ch);
    for (int k = 0; k < 3; ++k) {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
    }
    box.refresh(&doc);
    state(u"after undo"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%d lines\n", int(out.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
