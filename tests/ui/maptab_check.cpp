// The main window's Map tab and Map size page (phase 4) against Python, the C++ side of tools/check_maptab.py.
// Usage: med_maptab_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/adjpanel.h"
#include "ui/canalpanel.h"
#include "ui/drawpanel.h"
#include "ui/mapsize.h"
#include "ui/mapview.h"
#include "ui/mismatchbox.h"
#include "ui/pospanel.h"
#include "ui/riverpanel.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    const QString scen = m->scenarios()[0].name;
    mainwindow::MainWindow win(m, md, scen, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();
    auto *dp = win.draw_panel;

    auto handler = [&]() -> QString {
        mapview::MouseHandler *hd = win.map->mouse_handler;
        const std::pair<const char *, mapview::MouseHandler *> hs[] = {
            {"adj", &win.map_panel->handler}, {"pos", &win.pos_panel->handler},
            {"canal", &win.canal_panel->handler}, {"draw-prov", &dp->prov_handler},
            {"draw-terrain", &dp->terrain_box->handler}, {"draw-rivers", &dp->river_box->handler}};
        for (const auto &[name, x] : hs)
            if (hd == x)
                return QString::fromLatin1(name);
        return hd ? u"other"_s : u"None"_s;
    };
    auto banner = [&] {
        QLabel *b = win.map->_banner;
        return b && b->isVisibleTo(win.map) ? b->text() : u""_s;
    };
    auto pill = [&] {
        auto *p = win.map->_pill;
        return p && p->isVisibleTo(win.map) ? u"%1 / %2"_s.arg(p->text(), p->toolTip()) : u""_s;
    };
    auto img = [&] { return h(win.map->_img32.data(), win.map->_img32.size() * 4); };
    auto checked = [](const std::map<QString, QPushButton *> &btns, const QStringList &order) {
        QStringList r;
        for (const QString &k : order)
            if (btns.at(k)->isChecked())
                r << k;
        return r.join(u',');
    };
    auto state = [&](const QString &tag) {
        out << u"%1 tab %2 sub %3 page %4 mode %5 | seg %6 adj %7 bar %8 | stack %9 | tool %10 | pill %11"_s.arg(
            tag, win.tab, win.map_sub, v(win.rail_page()), win.mode,
            checked(win.map_sub_btns, {u"adj"_s, u"canal"_s, u"pos"_s, u"draw"_s}),
            checked(win.adj_sub_btns, {u"adj"_s, u"canal"_s}), v(win.adj_seg_bar->isVisibleTo(&win)),
            QString::number(win.stack->currentWidget() == win.map_tab ? win.map_stack->currentIndex() : -1))
                   .arg(handler(), v(pill()));
        out << u"%1 shapes %2 names %3 %4 | layer %5 | banner %6 | msg %7"_s.arg(
            tag, QString::number(win.map->shape_layer->shapes.size()),
            QString::number(win.map->name_layer->items.size()), v(win.map->name_layer->isVisible()),
            v(win.draw_layer()), v(banner()), v(win.st_msg->text()));
    };
    auto table = [&]() -> QString {
        const mapview::ColorTable t = win.color_table();
        if (!t.layer)
            return u"table"_s;
        if (dynamic_cast<terrainpanel::TerrainView *>(t.layer.get()))
            return u"TerrainView"_s;
        if (dynamic_cast<riverpanel::RiverView *>(t.layer.get()))
            return u"RiverView"_s;
        if (dynamic_cast<mismatchbox::ProvMismatchView *>(t.layer.get()))
            return u"ProvMismatchView"_s;
        return u"PixelLayer"_s;
    };
    auto head = [&] {
        const auto hd = win.header();
        return hd ? u"%1 | %2"_s.arg(hd->first, hd->second) : u"None"_s;
    };
    auto hover = [&](long long p, const QString &tag) {
        const int x = int(md->cx[size_t(p)]), y = int(md->cy[size_t(p)]);
        win.on_hover(p, x, y);
        out << (tag.isEmpty() ? u"hover %1 %2"_s.arg(p).arg(v(win.st_hover->text()))
                              : u"hover %1"_s.arg(v(win.st_hover->text())));
    };

    std::vector<long long> keys;
    for (const auto &[p, _pr] : m->provinces)
        keys.push_back(p);
    std::sort(keys.begin(), keys.end());
    std::vector<long long> land, sea;
    for (long long p : keys)
        if (p < md->n && md->area[size_t(p)] > 0)
            (m->provinces.get(p)->sea ? sea : land).push_back(p);
    const Qt::KeyboardModifiers none{};
    // --- the Map tab: Adjacencies. (Opened from the Errors page: the side panel's height is the shown page's, and the
    // Pops page the window opens on is not in the port yet — the scroll area sizes the next page from where it was.)
    win.set_tab(u"errors"_s);
    win._check_timer->stop();
    QApplication::processEvents();
    win._check_timer->stop();
    win.set_tab(u"map"_s);
    QApplication::processEvents();
    state(u"map"_s);
    out << u"image "_s + img();
    {
        QStringList hs;
        for (QWidget *w : std::initializer_list<QWidget *>{win.map_panel, win.canal_panel, win.pos_panel,
                                                           win.draw_panel, win.map_stack, win.map_tab, win.stack})
            hs << u"%1/%2"_s.arg(w->sizeHint().height()).arg(w->height());
        out << u"heights "_s + hs.join(u' ');
    }
    win.map_tab->grab().save(QDir(out_dir).filePath(u"side_adj.png"_s));
    for (const auto &[p, mods] : std::vector<std::pair<long long, int>>{
             {land[5], 0}, {land[5], 0x02000000}, {land[40], 0x04000000}, {sea[0], 0}, {sea[1], 0x04000000}, {0, 0}}) {
        win.on_click(p, Qt::KeyboardModifiers(mods));
        out << u"click %1 %2 | %3"_s.arg(p).arg(mods).arg(head());
    }
    win.set_view_mode(u"state"_s);
    win.on_click(land[5], none);
    out << u"state view | %1 | states %2"_s.arg(head(), v(win.selected_states()));
    win.set_view_mode(u"province"_s);
    win.on_click(land[5], none);
    win.map_panel->edit_btn->setChecked(true);
    state(u"adj edit"_s);
    win.escape();
    state(u"escape"_s);
    win.start_adj_pick(u"a"_s);
    state(u"pick a"_s);
    win.on_click(land[7], none);
    state(u"picked"_s);
    win.start_adj_pick(u"through"_s);
    win.escape();
    state(u"pick cancelled"_s);
    win.start_adj_pick(u"b"_s);
    win.on_click(0, none);
    state(u"picked nothing"_s);
    win.map_panel->edit_btn->setChecked(false);
    for (long long p : {land[5], land[7], sea[0]})
        hover(p, {});
    // --- Canals, Positions
    win.adj_sub_btns.at(u"canal"_s)->click();
    state(u"canal"_s);
    win.set_map_sub(u"pos"_s);
    state(u"pos"_s);
    win.pos_panel->edit_btn->setChecked(true);
    state(u"pos edit"_s);
    for (long long p : {land[5], land[60]})
        hover(p, {});
    win.escape();
    win.pos_panel->edit_btn->setChecked(false);
    // --- Draw
    win.map_sub_btns.at(u"draw"_s)->click();
    state(u"draw"_s);
    win.map_tab->grab().save(QDir(out_dir).filePath(u"side_draw.png"_s));
    win.toggle_draw();
    state(u"D: drawing"_s);
    win.toggle_draw();
    state(u"D: stopped"_s);
    for (const QString &lay : {u"terrain"_s, u"rivers"_s}) {
        dp->layer_btns.at(lay)->click();
        QApplication::processEvents();
        state(u"layer "_s + lay);
        out << u"table %1 image %2"_s.arg(table(), img());
        for (long long p : {land[5], sea[0]})
            hover(p, {});
        win.map->grab().save(QDir(out_dir).filePath(u"map_%1.png"_s.arg(lay)));
    }
    dp->layer_btns.at(u"prov"_s)->click();
    state(u"layer prov"_s);
    out << u"table %1 image %2"_s.arg(table(), img());
    dp->mm_box->show_cb->setChecked(true);
    QApplication::processEvents();
    out << u"mismatches shown: table %1 image %2 shown %3"_s.arg(table(), img(), v(win._prov_mismatch_shown()));
    hover(land[5], u"x"_s);
    win.map->grab().save(QDir(out_dir).filePath(u"map_mismatch.png"_s));
    dp->mm_box->show_cb->setChecked(false);
    win.render_map();
    // --- D from another tab
    win.set_tab(u"pops"_s);
    state(u"pops"_s);
    win.toggle_draw();
    state(u"D from pops"_s);
    win.toggle_draw();
    // --- the island flag
    win.on_island(true, {land[5], land[6], sea[0]});
    out << u"island on: pending %1 msg %2"_s.arg(v(win.st_pending->text()), v(win.st_msg->text()));
    win.on_island(true, {land[5]});
    out << u"island again: msg "_s + v(win.st_msg->text());
    {
        const auto u = win.undo();
        out << u"undo %1 pending %2"_s.arg(v(u), v(win.st_pending->text()));
    }
    win.on_island(true, {land[8]});
    out << u"island one: msg "_s + v(win.st_msg->text());
    win.undo();
    // --- Map size
    win.go_page(u"mapsize"_s);
    QApplication::processEvents();
    auto *sw = win.size_ws;
    sw->_check_minimap();
    out << u"mapsize tab %1 shown %2 split %3 | %4 | %5 | fit %6"_s.arg(
        win.tab, v(sw->isVisibleTo(&win)), v(win.main_split->isVisibleTo(&win)), sw->size_lbl->text(),
        sw->cut_btn->text(), v(sw->mm_fit_btn->isVisibleTo(sw)));
    if (sw->mm_fit_btn->isVisibleTo(sw)) {
        sw->mm_fit_btn->click();
        out << u"fitted: pending %1 msg %2 | %3"_s.arg(v(win.st_pending->text()), v(win.st_msg->text()),
                                                       v(sw->mm_note->text()));
        sw->_changed();
        out << u"cut %1 %2 warn %3"_s.arg(v(sw->cut_btn->isEnabled()), sw->cut_btn->text(), v(sw->warn->text()));
        const auto u = win.undo();
        out << u"undo %1 pending %2"_s.arg(v(u), v(win.st_pending->text()));
    }
    win.go_page(u"adj"_s);
    state(u"back to adj"_s);
    win.go_page(u"draw"_s);
    state(u"rail: draw"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
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
