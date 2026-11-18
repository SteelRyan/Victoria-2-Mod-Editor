// drawpanel against Python, the C++ side of tools/check_drawpanel.py.
// Usage: med_drawpanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pypath.h"
#include "map/layerbmp.h"
#include "map/moreviews_data.h"
#include "map/provbmp.h"
#include "ui/colorwheel.h"
#include "ui/colormappanel.h"
#include "ui/drawpanel.h"
#include "ui/mapview.h"
#include "ui/mismatchbox.h"
#include "ui/newprov_dialog.h"
#include "ui/pospanel.h"
#include "ui/riverpanel.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace drawpanel;

static QString hs(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString hs(const QString &s) { return hs(s.toUtf8()); }
template <typename T>
static QString hs_grid(const grid::Grid<T> &g)
{
    return hs(QByteArray(reinterpret_cast<const char *>(g.flat().data()), qsizetype(g.flat().size() * sizeof(T))));
}

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
    QStringList out, msgs, sigs, dialogs;
    pospanel::PositionsPanel pp(*m, md);
    pp.view = win.map;
    DrawPanel dp(*m, md);
    dp.view = win.map;
    dp.geo = win.geo.get();
    dp.landsea = win.landsea.get();
    dp.pos_panel = &pp;
    QObject::connect(&dp, &DrawPanel::message, [&](const QString &t) { msgs << t; });
    QObject::connect(&dp, &DrawPanel::editModeChanged, [&](bool on) { sigs << u"mode "_s + v(on); });
    QObject::connect(&dp, &DrawPanel::layerChanged, [&](const QString &k) { sigs << u"layer "_s + k; });
    QObject::connect(&dp, &DrawPanel::islandRequested, [&](bool on, const QList<long long> &ps) {
        sigs << u"island %1 %2"_s.arg(v(on), v(std::vector<long long>(ps.begin(), ps.end())));
    });
    QObject::connect(&dp, &DrawPanel::selectRequested, [&](const QList<long long> &a, const QList<long long> &b) {
        sigs << u"select %1 %2"_s.arg(v(std::vector<long long>(a.begin(), a.end())),
                                      v(std::vector<long long>(b.begin(), b.end())));
    });
    QObject::connect(&dp, &DrawPanel::zoomRequested, [&](double x, double y) { sigs << u"zoom %1 %2"_s.arg(v(x), v(y)); });
    dp.setFixedWidth(470);
    DrawHandler &hd = dp.prov_handler;
    dp.run_dialog = [&](newprov::NewProvinceDialog &dlg) {
        dlg.name->setText(u"Newport"_s);
        dlg.adjustSize();
        const auto vals = dlg.values();
        dialogs << u"dialog note %1 warn %2 %3 tpl %4 %5 values %6"_s.arg(
            v(dlg.note->text()), v(dlg.warn->text()), v(dlg.warn->isVisibleTo(&dlg)), v(dlg.tpl_lbl->text()),
            v(dlg.tpl_name->text()), pyform::list({v(vals.name), v(vals.sea), v(vals.tmpl)}));
        dlg.grab().save(QDir(out_dir).filePath(u"dialog.png"_s));
        return true;
    };

    auto vis = [&](QWidget *w) { return v(w->isVisibleTo(&dp)); };
    auto cards = [&] {
        QStringList rows;
        for (int i = 0; i < dp.new_box->count(); ++i) {
            QWidget *w = dp.new_box->itemAt(i)->widget();
            QStringList parts;
            for (QWidget *c : w->findChildren<QWidget *>()) {
                if (auto *cb = qobject_cast<QCheckBox *>(c))
                    parts << u"C:%1:%2"_s.arg(cb->text(), v(cb->isChecked()));
                else if (auto *b = qobject_cast<QPushButton *>(c))
                    parts << u"B:%1:%2:%3"_s.arg(b->text(), v(b->isEnabled()), b->objectName());
                else if (auto *l = qobject_cast<QLabel *>(c))
                    parts << u"L:"_s + l->text();
            }
            rows << parts.join(u" | "_s);
        }
        return rows;
    };
    auto state = [&](const QString &what) {
        auto &b = doc.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
        auto &tb = doc.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s);
        auto &rb = doc.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s);
        out << u"%1: layer %2 idmap %3 md %4 bmp %5 terrain %6 %7 rivers %8 %9"_s.arg(
            what, dp.layer(), hs_grid(md.idmap), QString::number(md.version), QString::number(b.version),
            QString::number(tb.version), tb.data ? hs_grid(*tb.data) : u"None"_s, QString::number(rb.version),
            rb.data ? hs_grid(*rb.data) : u"None"_s);
        QStringList bb;
        for (const QString &k : {u"none"_s, u"prov"_s, u"state"_s})
            bb << v(dp.border_btns[k]->isChecked());
        out << u"  intro %1 help %2 %3 border %4 %5"_s.arg(hs(dp.intro->text()), hs(dp.help->text()), vis(dp.help),
                                                          vis(dp.border_row), pyform::list(bb));
        out << u"  sections paint %1 clean %2 new %3 terrain %4 rivers %5 edit %6 tool %7 fill %8"_s.arg(
            vis(dp.paint_section), vis(dp.clean_section), vis(dp.new_section), vis(dp.terrain_box),
            vis(dp.river_box), v(dp.edit_btn->isChecked()), dp.tool(), v(dp.is_fill()));
        out << u"  paint %1 name %2 tip %3 spin %4 island %5 %6 %7 note %8"_s.arg(
            v(dp.paint_id), v(dp.paint_name->text()), v(dp.paint_name->toolTip()), QString::number(dp.paint_spin->value()),
            vis(dp.island_host), v(dp.island_cb->isChecked()), v(dp.island_cb->isEnabled()), v(dp.island_note->text()));
        out << u"  over %1 %2 erase %3 %4 %5 size %6"_s.arg(
            vis(dp.over), v(dp.over->currentData().toString()), vis(dp.erase), v(dp.erase->currentData().toString()),
            v(dp.erase_how->currentData().toString()), v(dp.size_spin->value()));
        out << u"  status "_s + v(dp.status->text());
        out << u"  clean %1 %2 %3 %4 %5"_s.arg(v(dp.clean_info->text()), v(dp.stray_btn->text()),
                                               v(dp.stray_btn->isEnabled()), v(dp.zero_btn->text()),
                                               v(dp.zero_btn->isEnabled()));
        QStringList gone;
        for (int i = 0; i < dp.gone_list->count(); ++i)
            gone << dp.gone_list->item(i)->text();
        out << u"  gone %1 %2 %3"_s.arg(vis(dp.gone_title), v(dp.gone_title->text()), v(gone));
        out << u"  delete %1 %2 %3"_s.arg(v(dp.del_btn->text()), vis(dp.del_note), v(dp.del_note->text()));
        out << u"  mm "_s + v(dp.mm_box->how->text());
        for (const QString &c : cards())
            out << u"  card "_s + v(c);
        out << u"  msgs %1 sigs %2"_s.arg(v(msgs), v(sigs));
        msgs.clear();
        sigs.clear();
    };
    auto refresh = [&](const std::vector<long long> &sel = {}, const std::vector<long long> &sea = {}) {
        dp.refresh(doc, sel, sea);
        dp._clean_timer->stop();                     // (counted when the dump asks: the same on both sides)
    };
    auto stroke = [&](const std::vector<std::pair<double, double>> &pts,
                      Qt::KeyboardModifiers mods = Qt::NoModifier) {
        const bool got = hd.press(pts[0].first, pts[0].second, 1.0, mods);
        for (size_t i = 1; i < pts.size(); ++i)
            hd.move(pts[i].first, pts[i].second);
        hd.release(pts.back().first, pts.back().second, 1.0, Qt::NoModifier);
        return got;
    };
    auto set_combo = [](QComboBox *c, const QString &key) { c->setCurrentIndex(c->findData(key)); };
    auto grab = [&](const QString &name) {
        dp.show();
        QApplication::processEvents();
        dp._clean_timer->stop();
        dp.grab().save(QDir(out_dir).filePath(name));
    };
    auto button = [&](const QString &text) {
        for (QPushButton *b : dp.new_section->findChildren<QPushButton *>())
            if (b->text() == text)
                return b;
        throw std::runtime_error("no button");
    };
    auto pixel_infos = [&] {
        for (const auto &[x, y] : {std::pair{2143LL, 1592LL}, std::pair{2144LL, 1592LL}, std::pair{2059LL, 145LL}})
            out << u"pixel_info %1 %2 %3"_s.arg(x).arg(y).arg(v(dp.pixel_info(x, y)));
    };

    refresh();
    state(u"start"_s);
    grab(u"panel_start.png"_s);
    // --- layers
    for (const QString &key : {u"terrain"_s, u"rivers"_s}) {
        dp.layer_btns[key]->setChecked(true);
        state(u"layer "_s + key);
        pixel_infos();
    }
    for (const QString &key : {u"prov"_s, u"none"_s, u"state"_s, u"prov"_s, u"state"_s}) {
        dp.set_borders(key);
        mapview::EdgeLayer *el = win.map->edge_layer;
        QStringList bb;
        for (const QString &k : {u"none"_s, u"prov"_s, u"state"_s})
            bb << v(dp.border_btns[k]->isChecked());
        out << u"borders %1 %2 edge %3 %4"_s.arg(key, pyform::list(bb), v(el->prov), v(el->state));
    }
    out << u"shapes %1 describe %2 %3 %4"_s.arg(dp.shapes().size())
               .arg(v(dp.describe(QVariantList{u"rplan"_s, u"source"_s, 0})), v(dp.describe(QVariantList{u"pos"_s, 1, u"unit"_s})),
                    v(dp.describe(std::nullopt)));
    dp.layer_btns[u"prov"_s]->setChecked(true);
    pixel_infos();
    out << u"shapes %1"_s.arg(dp.shapes().size());
    dp.edit_btn->setChecked(true);
    state(u"edit on"_s);
    // --- picking
    out << u"rclick "_s + v(hd.rclick(2210.5, 1580.5, 1.0, Qt::NoModifier));
    state(u"picked by right-click"_s);
    for (int val : {1267, 0, 65000, 1}) {
        dp.paint_spin->setValue(val);
        state(u"id box %1"_s.arg(val));
    }
    dp.island_cb->click();
    state(u"islands ticked"_s);
    refresh({94});
    dp._use_selection();
    state(u"use selection"_s);
    // --- brush strokes, every 'Paint over' rule
    dp.set_paint(9);
    out << u"press "_s + v(stroke({{2140.5, 1592.5}, {2144.5, 1592.5}, {2148.5, 1593.5}}));
    refresh({94});
    state(u"brush any"_s);
    struct Rule {
        QString rule;
        long long pid;
        std::vector<std::pair<double, double>> pts;
        std::vector<long long> sel;
    };
    for (const Rule &r : std::vector<Rule>{
             {u"land"_s, 3007, {{2136.5, 1585.5}, {2150.5, 1585.5}}, {94}},
             {u"sea"_s, 94, {{2136.5, 1580.5}, {2152.5, 1580.5}}, {94}},
             {u"selected"_s, 9, {{2130.5, 1576.5}, {2152.5, 1576.5}}, {94}},
             {u"unassigned"_s, 2813, {{2057.5, 145.5}, {2062.5, 146.5}}, {}},
             {u"mm_terrain"_s, 94, {{2140.5, 1598.5}, {2152.5, 1598.5}}, {}},
             {u"mm_rivers"_s, 94, {{2140.5, 1600.5}, {2152.5, 1600.5}}, {}},
             {u"mm_terrain"_s, 3007, {{2140.5, 1590.5}, {2152.5, 1590.5}}, {}}}) {
        set_combo(dp.over, r.rule);
        dp.set_paint(r.pid);
        refresh(r.sel);
        stroke(r.pts);
        refresh(r.sel);
        state(u"brush over %1 with %2"_s.arg(r.rule).arg(r.pid));
    }
    set_combo(dp.over, u"any"_s);
    // --- cancel, wheel
    {
        const QString before = hs_grid(md.idmap);
        hd.press(2130.5, 1570.5, 1.0, Qt::NoModifier);
        hd.move(2134.5, 1570.5);
        const bool changed = before != hs_grid(md.idmap);
        const bool c = hd.cancel();
        out << u"cancel %1 %2 back %3 touched %4"_s.arg(v(changed), v(c), v(before == hs_grid(md.idmap)),
                                                        hd.touched ? u"{…}"_s : u"None"_s);
        out << u"cancel again "_s + v(hd.cancel());
    }
    for (const auto &[steps, mods] : std::vector<std::pair<double, Qt::KeyboardModifiers>>{
             // (the brush key: Ctrl in the Python editor, Shift in C++ — drawlink::BRUSH_KEY)
             {2, drawlink::BRUSH_KEY}, {-1, Qt::NoModifier}, {5, drawlink::BRUSH_KEY}, {1, drawlink::BRUSH_KEY},
             {-0.5, drawlink::BRUSH_KEY}, {-1.5, drawlink::BRUSH_KEY}}) {
        const bool got = hd.wheel(steps, mods);
        out << u"wheel %1 %2 size %3"_s.arg(steps == std::floor(steps) ? QString::number((long long)steps) : v(steps),
                                            v(got), v(dp.size_spin->value()));
    }
    dp.size_spin->setValue(6);
    out << u"ctrl press "_s + v(hd.press(2130.5, 1570.5, 1.0, Qt::ControlModifier));
    // --- fill
    dp.tool_btns[u"fill"_s]->setChecked(true);
    dp.set_paint(9);
    out << u"fill press "_s + v(hd.press(342.5, 1262.5, 1.0, Qt::NoModifier));
    refresh();
    state(u"fill 1267 with 9"_s);
    grab(u"panel_gone.png"_s);
    dp.fill_at(342.5, 1262.5);
    dp.fill_at(-3.0, 10.0);
    set_combo(dp.over, u"sea"_s);
    dp.fill_at(1938.5, 774.5);
    set_combo(dp.over, u"any"_s);
    refresh();
    state(u"fill nothing"_s);
    Q_EMIT dp.gone_list->itemClicked(dp.gone_list->item(0));
    stroke({{342.5, 1262.5}, {343.5, 1262.5}});
    refresh();
    state(u"brought back"_s);
    dp.tool_btns[u"fill"_s]->setChecked(true);
    dp.set_paint(2813);
    dp.fill_at(1552.5, 1119.5);
    refresh();
    state(u"fill an area of no province"_s);
    // --- eraser
    dp.tool_btns[u"eraser"_s]->setChecked(true);
    dp.set_paint(1267);
    stroke({{342.5, 1262.5}, {342.5, 1263.5}});
    refresh();
    state(u"erase target"_s);
    set_combo(dp.erase, u"any"_s);
    set_combo(dp.erase_how, u"fill"_s);
    dp.set_paint(0);
    out << u"erase fill press "_s + v(hd.press(1938.5, 774.5, 1.0, Qt::NoModifier));
    refresh();
    state(u"erase fill anything"_s);
    set_combo(dp.erase_how, u"brush"_s);
    set_combo(dp.erase, u"mm_terrain"_s);
    stroke({{2140.5, 1596.5}, {2152.5, 1596.5}});
    refresh();
    state(u"erase where terrain has ocean"_s);
    set_combo(dp.erase, u"target"_s);
    out << u"eraser without target "_s + v(hd.press(2130.5, 1570.5, 1.0, Qt::NoModifier));
    dp.tool_btns[u"brush"_s]->setChecked(true);
    out << u"brush without target "_s + v(hd.press(2130.5, 1570.5, 1.0, Qt::NoModifier));
    dp.tool_btns[u"fill"_s]->setChecked(true);
    dp.fill_at(2130.5, 1570.5);
    dp.tool_btns[u"brush"_s]->setChecked(true);
    state(u"no target"_s);
    // --- clean up
    dp._update_clean();
    state(u"counted"_s);
    dp.stray_btn->click();
    refresh();
    dp._update_clean();
    state(u"stray bits cleaned"_s);
    dp.zero_btn->click();
    refresh();
    dp._update_clean();
    state(u"small areas filled"_s);
    // --- delete
    dp.del_btn->click();
    state(u"delete with nothing"_s);
    refresh({1266});
    dp.del_btn->click();
    state(u"delete armed"_s);
    refresh({1266, 1331});
    state(u"other selection"_s);
    dp.del_btn->click();
    dp.del_btn->click();
    refresh();
    state(u"deleted"_s);
    // --- new provinces
    dp.set_paint(94);
    refresh({94});
    dp.new_province();
    out << dialogs;
    dialogs.clear();
    refresh({94});
    state(u"new province made"_s);
    const long long nw = *dp.paint_id;
    dp.size_spin->setValue(8);
    stroke({{2134.5, 1580.5}, {2138.5, 1582.5}, {2142.5, 1584.5}});
    refresh({94});
    state(u"new province painted"_s);
    grab(u"panel_new.png"_s);
    auto unfinished = [&] {
        QStringList rows;
        for (const auto &[p, t] : dp.unfinished())
            rows << pyform::list({v(p), v(t)});
        return u"unfinished "_s + pyform::list(rows);
    };
    out << unfinished();
    const auto cbs = dp.new_section->findChildren<QCheckBox *>();
    if (!cbs.isEmpty()) {
        cbs[0]->setChecked(false);
        cbs[0]->setChecked(true);
    }
    button(u"Finish"_s)->click();
    refresh({94});
    state(u"finished"_s);
    auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);
    out << u"entry "_s + v(positions::fmt_entry(nw, pf.get(nw), u"\n"_s));
    stroke({{2134.5, 1586.5}, {2138.5, 1586.5}});
    refresh({94});
    state(u"painted again"_s);
    out << unfinished();
    button(u"Paint it"_s)->click();
    button(u"Finish again"_s)->click();
    refresh({94});
    state(u"finished again"_s);
    out << u"create bad "_s + v(dp.create(u"Bad"_s, false, 999999));
    const long long sea_id = *dp.create(u"Deep Water"_s, true, std::nullopt);
    out << u"create sea "_s + v(sea_id);
    dp.finish(sea_id);
    dp.size_spin->setValue(12);
    stroke({{3165.5, 70.5}, {3175.5, 72.5}});
    refresh();
    state(u"sea tile painted"_s);
    dp.finish(sea_id);
    refresh();
    state(u"sea tile finished"_s);
    out << u"entry "_s + v(positions::fmt_entry(sea_id, pf.get(sea_id), u"\n"_s));
    // --- undo to the start
    for (int k = 0; k < 60; ++k) {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
        if (!u)
            break;
    }
    refresh();
    state(u"after undo"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    {   // (C++ only: the drawing bar and the pixel grid on the real window's map, for a look — not compared)
        QDir().mkpath(out_dir + u"/../extra"_s);
        dp.hide();                                  // (the check's own panel: its bar off the window's map)
        win.resize(1500, 900);
        win.show();
        QApplication::processEvents();
        win.toggle_draw();
        win.draw_panel->set_layer(u"terrain"_s);
        win.map->center_on_province(94, 12.0);
        win.draw_panel->toggle_grid();
        auto settle = [] {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 500)
                QApplication::processEvents();
        };
        settle();
        win.map->grab().save(out_dir + u"/../extra/drawbar_grid.png"_s);
        win.draw_panel->set_layer(u"prov"_s);       // (key 1's call)
        settle();
        win.map->grab().save(out_dir + u"/../extra/drawbar_grid_prov.png"_s);
        // the Colours layer (C++ only, 2026-11-10): a province and its neighbours tinted, the map and the panel
        win.draw_panel->toggle_grid();
        win.draw_panel->set_layer(u"colours"_s);    // (key 4's call)
        std::vector<long long> pick = {94};
        for (long long n : mdp->neighbours(94))
            if (!mdp->sea[size_t(n)])
                pick.push_back(n);
        win.set_selection(pick);
        win.map->zoom_by(0.5 / win.map->transform().m11(), false);   // (zoomed out: the fade shows)
        win.map->center_on_province(94);
        settle();
        win.map->grab().save(out_dir + u"/../extra/colours_before.png"_s);
        colormappanel::ColormapBox *cb = win.draw_panel->colour_box;
        cb->color_btn->set(u"#c04030"_s);
        cb->reach->setValue(80);
        cb->apply_selection();
        settle();
        win.map->grab().save(out_dir + u"/../extra/colours_tinted.png"_s);
        win.draw_panel->grab().save(out_dir + u"/../extra/colours_panel.png"_s);
        std::printf("colours: %s\n", qPrintable(cb->status->text()));
        std::printf("colours: pending %d, undo: %s\n", int(win.doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.size()),
                    qPrintable(win.undo().value_or(u"None"_s)));
        settle();
        std::printf("colours: after undo pending %d\n", int(win.doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.size()));
        win.map->grab().save(out_dir + u"/../extra/colours_undone.png"_s);
        // the soft brush: one stroke across the province, then undone
        cb->tool_btns.at(u"brush"_s)->setChecked(true);
        cb->size_spin->setValue(120);
        const double px = mdp->cx[94], py_ = mdp->cy[94];
        cb->handler.press(px - 100, py_, 1.0, Qt::NoModifier);
        for (int k = 1; k <= 10; ++k)
            cb->handler.move(px - 100 + 20 * k, py_ + 3 * k);
        cb->handler.release(px + 100, py_ + 30, 1.0, Qt::NoModifier);
        settle();
        win.map->grab().save(out_dir + u"/../extra/colours_brush.png"_s);
        const int pend = int(win.doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.size());
        const std::optional<QString> un = win.undo();
        std::printf("colours: brush pending %d, undo: %s\n", pend, qPrintable(un.value_or(u"None"_s)));
    }
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
