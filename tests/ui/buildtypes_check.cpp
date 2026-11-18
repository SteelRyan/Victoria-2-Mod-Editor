// Building types (C++ only, the user 2026-11-16): a new mod from the base game opened in the real main window
// (offscreen); Provinces & states → Building types: a new province building "Test Bank" made, an effect, two
// technologies, London given level 1, undo / redo, Apply; then the files in the mod and the mod opened again.
// The mod is left in build/buildtypes/mod/Bank Test for a game test.
// Usage: med_buildtypes_check <work folder>   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/blocks.h"
#include "core/document.h"
#include "core/history.h"
#include "core/modcopy.h"
#include "core/moddata.h"
#include "core/provbuild.h"
#include "core/pypath.h"
#include "core/script.h"
#include "core/submods.h"
#include "map/imgio.h"
#include "map/iconstrips_data.h"
#include "ui/buildtypes.h"
#include "ui/entryview.h"
#include "ui/moreviews.h"
#include "ui/dialogs.h"
#include "ui/mapview.h"
#include "ui/provpanel.h"
#include "ui/checkpanel.h"
#include "map/mapcheck.h"
#include "core/scriptstore.h"
#include "ui/issuepanel.h"
#include "ui/politicspanel.h"
#include "ui/reformwin.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <cstdio>

using namespace Qt::StringLiterals;
namespace pb = provbuild;

static int fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", qUtf8Printable(what));
    fails += int(!ok);
}
static QString read(const QString &p) { return QFile::exists(p) ? script::read_text(p) : QString(); }
static void settle()
{
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
}

// --look <mod folder> <base> <out folder>: another mod opened (read only, nothing applied): Game rules → Building types
// and Politics → the reforms as the game's window grabbed as pictures, the buildings' strips and the reform boxes listed
// (the user 2026-11-17 on Victoria Universalis: a 51-picture strip broke the page; the reforms stood a row aside)
static int look(const QString &folder, const QString &base, const QString &out)
{
    QDir().mkpath(out);
    auto m = std::make_shared<moddata::Mod>(folder, base, std::nullopt);
    m->load();
    const auto sc = m->scenarios();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, mdp, sc[0].name, pypath::join(out, u"backups"_s));
    win.resize(1400, 860);
    win.show();
    settle();
    win.go_page(u"rules"_s);
    win.rules_ws->set_page(u"buildings"_s);
    settle();
    buildtypes::BuildTypesPanel *bt = win.bt_panel;
    std::printf("buildings: %s\n", qUtf8Printable(bt->keys().join(u", "_s)));
    const bool bar = bt->tree->verticalScrollBar()->isVisible();
    check(!bar && bt->tree->visualItemRect(bt->tree->topLevelItem(bt->tree->topLevelItemCount() - 1)).bottom() <
                     bt->tree->viewport()->height(),
          u"every building row in sight, no scroll bar"_s);
    for (int i = 0; i < bt->tree->topLevelItemCount(); ++i) {
        bt->tree->setCurrentItem(bt->tree->topLevelItem(i));
        settle();
        const QString cur = bt->cur.value_or(QString());
        std::printf("  %-24s frames %d, picked in the picture: %s, page width %d (window %d)\n", qUtf8Printable(cur),
                    bt->frames_n, bt->win_view->sel == cur ? "yes" : "NO", bt->sizeHint().width(), win.width());
        bt->grab().save(pypath::join(out, u"buildings_%1.png"_s.arg(cur)));
        check(bt->win_view->sel == cur, u"…%1 picked in the window's picture"_s.arg(cur));
        // the page as tall as it needs (nothing squeezed), and narrow: neither it nor the Game rules workspace widens the
        // window (the window's own minimum is its toolbar's: ~1680 px, ~1870 with a "⚠ N on this page" button)
        const int need = bt->layout()->hasHeightForWidth() ? bt->layout()->heightForWidth(bt->width()) : 0;
        check(bt->height() >= need - 2, u"…not squeezed (%1 high, needs %2)"_s.arg(bt->height()).arg(need));
        check(bt->minimumSizeHint().width() < 600 && win.rules_ws->minimumSizeHint().width() < 600,
              u"…the page (%1 px) and Game rules (%2 px) ask for little width"_s.arg(bt->minimumSizeHint().width())
                  .arg(win.rules_ws->minimumSizeHint().width()));
    }
    win.set_tab(u"politics"_s);
    settle();
    politicspanel::PoliticsWorkspace *pw = win.pol_ws;
    auto *iv = static_cast<issuepanel::IssueView *>(*pw->views.get(u"reforms"_s));
    reformwin::GameReformView *wv = iv->win_view;
    std::printf("reform layout: tick %.0f,%.0f  bar %.0f,%.0f  name %.0f,%.0f  row %.0f  font %s\n", wv->lay.tick_x,
                wv->lay.tick_y, wv->lay.bar_x, wv->lay.bar_y, wv->lay.opt_x, wv->lay.opt_y, wv->lay.row_h,
                qUtf8Printable(wv->lay.opt_font));
    for (const auto &b : wv->boxes)
        std::printf("  %-28s %4.0f %4.0f %4.0f x %4.0f%s\n", qUtf8Printable(b.name), b.r.x(), b.r.y(), b.r.width(),
                    b.r.height(), b.mine ? "" : "  (not shown)");
    wv->grab().save(pypath::join(out, u"reforms_window.png"_s));
    std::printf("look: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == u"--look")
        return look(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), QString::fromLocal8Bit(argv[4]));
    if (argc < 2) {
        std::printf("usage: med_buildtypes_check <work folder>\n");
        return 2;
    }
    const QString work = QString::fromLocal8Bit(argv[1]);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QString scratch = pypath::join(work, u"ModEditor Cpp/build/buildtypes"_s);
    QDir(scratch).removeRecursively();           // (our own scratch folder, made fresh)
    const QString mods = pypath::join(scratch, u"mod"_s);
    QDir().mkpath(mods);
    const auto [folder, modfile] = submods::create_new(mods, u"Bank Test"_s);
    modcopy::mark_copied(folder);
    const long long LONDON = 300;
    const QString K = u"test_bank"_s;
    {
        auto m = std::make_shared<moddata::Mod>(folder, BASE, u"Bank Test"_s);
        m->load();
        const auto sc = m->scenarios();
        auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
        mainwindow::MainWindow win(m, mdp, sc[0].name, pypath::join(scratch, u"backups"_s));
        win.resize(1400, 860);
        win.show();
        settle();
        // (a page of Game rules since 2026-11-17, after National focuses)
        win.go_page(u"rules"_s);
        win.rules_ws->set_page(u"buildings"_s);
        settle();
        buildtypes::BuildTypesPanel *bt = win.bt_panel;
        auto *bt_sc = qobject_cast<QScrollArea *>(win.rules_ws->view(u"buildings"_s));
        check(bt_sc && bt_sc->widget() == bt && bt->isVisible(), u"Game rules → Building types shows the page (in a scroll area)"_s);
        // nothing squeezed: the page is at least as tall as its layout needs at its width
        check(bt->height() >= bt->layout()->heightForWidth(bt->width()) - 2 || !bt->layout()->hasHeightForWidth(),
              u"…the page as tall as it needs (%1 for %2)"_s.arg(bt->height()).arg(bt->layout()->hasHeightForWidth()
                                                                                         ? bt->layout()->heightForWidth(bt->width())
                                                                                         : -1));
        check(bt->keys() == pb::GAME_THREE && bt->window_problems().empty(), u"the game's three, nothing to fix"_s);
        check(bt->tree->topLevelItemCount() == 3, u"the list shows them (%1 rows)"_s.arg(bt->tree->topLevelItemCount()));
        // every row shown, no scroll bar (the user 2026-11-17)
        check(bt->tree->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                  bt->tree->visualItemRect(bt->tree->topLevelItem(2)).bottom() < bt->tree->viewport()->height(),
              u"…all three rows in sight, no scroll bar"_s);

        // a new one
        bt->new_name->setText(u"Test Bank"_s);
        bt->_make_new();
        settle();
        check(bt->keys().size() == 4 && bt->keys().last() == K, u"new building: "_s + bt->keys().join(u", "_s));
        check(bt->cur == K, u"…shown"_s);
        check(win.doc->hist->more == QStringList{K}, u"…the history reads its levels"_s);
        check(bt->window_problems().empty(), u"…its row slot and room made in the same step"_s);
        const pb::Window w = pb::read_window(bt->text(bt->gui_rel()));
        check(w.icons.first() == u"build_icon3" && w.fits == 4 && w.view_y == -563,
              u"…build_icon3 first, room for 4 rows, the window a row higher"_s);
        check(bt->title->text() == u"Test Bank", u"…its name in the game: "_s + bt->title->text());
        check(bt->frames_n == 7 && bt->frames_lay->count() == 8, u"…its strip shown picture by picture (%1, and ＋ for a new level)"_s.arg(bt->frames_n));
        // a click in the list picks it in the window's picture too (the user 2026-11-17)
        bt->tree->setCurrentItem(bt->tree->topLevelItem(0));
        settle();
        check(bt->cur == u"fort" && bt->win_view->sel == u"fort", u"list click: fort picked in the window picture too"_s);
        bt->tree->setCurrentItem(bt->tree->topLevelItem(3));
        settle();

        // an effect
        const int fi = bt->add_fx->find(u"supply_limit"_s);
        check(fi >= 0, u"supply_limit offered as an effect"_s);
        bt->add_fx->setCurrentIndex(fi);
        Q_EMIT bt->add_fx->activated(fi);
        settle();
        bt->_set(u"supply"_s, u"supply_limit"_s, u"5"_s);
        settle();
        const auto fx = pb::effects(bt->text(buildtypes::BUILDINGS), K);
        check(fx.size() == 1 && fx[0].key == u"supply_limit" && fx[0].value == u"5", u"effect supply_limit = 5"_s);

        // technologies: the first activates it, a second gives a level more
        bt->_tech(u"private_banks"_s, +1);
        settle();
        bt->_tech(u"stock_exchange"_s, +1);
        settle();
        const auto us = pb::unlocks(bt->tech_files(), K);
        // (the first gives level 1 too: activate_building alone gives none — measured)
        check(us.size() == 2 && us[0].tech == u"private_banks" && us[0].activates && us[0].adds == 1 && us[1].adds == 1,
              u"private banks activates it, stock exchange adds a level"_s);
        check(bt->tech_lay->count() == 2, u"…both listed"_s);

        // the strip's pictures: one put in after level 1, then taken out again (the user 2026-11-17)
        {
            const QString lib_dir = moreviews::library_dir(u"pictures"_s);
            QDir().mkpath(lib_dir);
            QImage red(40, 32, QImage::Format_ARGB32);
            red.fill(QColor(200, 30, 30));
            const QString red_path = QDir(lib_dir).filePath(u"zz_check_red.png"_s);
            red.save(red_path);
            bt->_fill_detail();
            settle();
            bt->lib->setCurrentIndex(bt->lib->findData(red_path));
            bt->frame_sel = 1;
            bt->_frame_edit(1);
            settle();
            check(bt->frames_n == 8 && bt->frame_sel == 2, u"a picture put in after level 1: 8 pictures, level 2 picked"_s);
            const auto img = iconstrips::strips_for(*win.mod, *win.doc).image(u"GFX_province_test_bank_strip"_s);
            check(img && img->width() == 320 && img->pixelColor(2 * 40 + 20, 16) == QColor(200, 30, 30),
                  u"…the strip 320 px wide, the red one third"_s);
            bt->grab().save(pypath::join(scratch, u"panel_frames.png"_s));   // (to be looked at)
            bt->_frame_edit(2);
            settle();
            const auto img2 = iconstrips::strips_for(*win.mod, *win.doc).image(u"GFX_province_test_bank_strip"_s);
            check(bt->frames_n == 7 && img2 && img2->width() == 280, u"…taken out again: 7 pictures"_s);
            win.undo();
            win.undo();
            settle();
            check(bt->frames_n == 7, u"…both undone"_s);
            QFile::remove(red_path);
        }
        // London starts with level 1: on Provinces & states → Provinces (the page has no map since 2026-11-17)
        bt->grab().save(pypath::join(scratch, u"panel_seen.png"_s));   // (to be looked at)
        win.grab().save(pypath::join(scratch, u"window_seen.png"_s));
        win.set_tab(u"provinces"_s);
        win.set_prov_sub(u"provinces"_s);
        win.set_selection({LONDON});
        settle();
        bool row = false;
        for (const auto &[k, lr] : win.prov_panel->level_rows)
            if (k == K && lr.plus->isEnabled()) {
                lr.plus->click();
                settle();
                row = true;
            }
        check(row && win.doc->prov_state(LONDON).level_of(K) == 1, u"Provinces: London's Test Bank row, + → level 1"_s);
        win.go_page(u"rules"_s);
        win.rules_ws->set_page(u"buildings"_s);
        settle();

        // undo / redo (Ctrl+Z everywhere)
        win.undo();
        settle();
        check(win.doc->prov_state(LONDON).level_of(K) == 0, u"undo: London back to 0"_s);
        win.redo();
        settle();
        check(win.doc->prov_state(LONDON).level_of(K) == 1, u"redo: level 1 again"_s);

        // the Errors tab: a building made by hand (no row in the province window) is a crash to be fixed
        {
            auto &store = win.doc->map<scriptstore::ScriptStore>(u"scripts"_s);
            const QString before = store.text(buildtypes::BUILDINGS);
            win.doc->edit(u"by hand"_s, {}, [&] {
                store.set_text(buildtypes::BUILDINGS, pb::add_building(before, u"hand_made"_s, std::nullopt));
            }, {}, {}, {u"scripts"_s});
            settle();
            static std::vector<mapcheck::Category> cats;
            auto group = [&](const QString &key = u"build_window"_s) -> const mapcheck::Category * {
                cats = mapcheck::war_goal_checks(*win.mod, *win.doc, store);
                for (const auto &c : cats)
                    if (c.key == key)
                        return &c;
                return nullptr;
            };
            const mapcheck::Category *c = group();
            check(c && c->items.size() == 1 && c->fix && c->items[0].text.contains(u"build_icon4"_s) &&
                      c->severity == u"error",
                  u"Errors tab: the crash — no slot build_icon4 (%1)"_s.arg(c ? c->items.size() : 0));
            {
                const mapcheck::Category *lk = group(u"build_window_look"_s);
                check(lk && lk->items.size() == 1 && !lk->fix && lk->severity != u"error" &&
                          lk->items[0].text.contains(u"No crash"_s),
                      u"…and apart, no fix button: no room for 5 rows, a warning"_s);
            }
            c = group();
            if (c) {
                std::vector<const mapcheck::Item *> its;
                for (const auto &it : c->items)
                    its.push_back(&it);
                const mapcheck::Category cc = *c;
                win.chk_panel->doc = win.doc.get();
                win.chk_panel->_do_fix(cc, its);
                settle();
            }
            check(group() == nullptr, u"…its fix: nothing left"_s);
            const pb::Window w5 = pb::read_window(store.text(bt->gui_rel()));
            check(w5.icons.size() == 5 && w5.fits == 5 && w5.view_y == -598, u"…5 slots, 5 rows, the window 2 rows higher"_s);
            win.undo();                              // (the fix and the hand-made one taken back)
            win.undo();
            settle();
            check(store.text(buildtypes::BUILDINGS) == before && pb::read_window(store.text(bt->gui_rel())).fits == 4,
                  u"…undone: 4 buildings, 4 rows"_s);
        }

        // the province window as the game draws it, and its layout (the user: "a way to manage it dynamically
        // (placements of the buildings)"; "it also needs to expand according to its need ... as the National focus rows")
        {
            auto &store = win.doc->map<scriptstore::ScriptStore>(u"scripts"_s);
            auto gw = [&] { return pb::read_window(store.text(bt->gui_rel())); };
            check(!bt->win_view->img.isNull() && bt->win_view->rows.size() == 4 &&
                      bt->win_view->rows[3].key == K,
                  u"the province window drawn, 4 rows (%1 x %2)"_s.arg(bt->win_view->img.width()).arg(bt->win_view->img.height()));
            bt->win_view->grab().save(pypath::join(scratch, u"window_view.png"_s));
            // a row moved: the building first, every slot shows its own building's picture
            bt->_move(K, 0);
            settle();
            const pb::Window wm = gw();
            check(bt->keys().first() == K && wm.slot_sprites.value(0) == u"GFX_province_test_bank_strip" &&
                      wm.slot_sprites.value(1) == u"GFX_province_fort_strip" &&
                      wm.slot_sprites.value(3) == u"GFX_province_infra_strip",
                  u"moved to row 1: "_s + bt->keys().join(u", "_s) + u" / "_s + wm.slot_sprites.join(u", "_s));
            check(bt->win_view->rows.size() == 4 && bt->win_view->rows[0].key == K, u"…the drawn window follows"_s);
            win.undo();
            settle();
            check(bt->keys().last() == K && gw().slot_sprites.value(0) == u"GFX_province_fort_strip", u"…undone"_s);
            // lower rows
            bt->_row_height(30);
            settle();
            check(gw().step == 30 && gw().view_y == -543 && pb::source_of(store.text(bt->gui_rel())).pref == 30 &&
                      bt->row_h->value() == 30,
                  u"rows of 30 px: the window %1"_s.arg(gw().view_y));
            win.undo();
            settle();
            check(gw().step == 35 && gw().view_y == -563, u"…undone: 35 px, %1"_s.arg(gw().view_y));
            // eight buildings: the window grows with them, the rows lowered so it stays under a 768 screen's top bar
            for (const QString nm : {u"Mill"_s, u"Mine"_s, u"Dock"_s, u"Inn"_s}) {
                bt->new_name->setText(nm);
                bt->_make_new();
                settle();
            }
            const pb::Window w8 = gw();
            check(bt->keys().size() == 8 && w8.fits == 8 && w8.step == 32 && bt->window_problems().empty(),
                  u"8 buildings: 8 rows of %1 px, window %2, nothing to fix"_s.arg(w8.step).arg(w8.view_y));
            bt->win_view->grab().save(pypath::join(scratch, u"window_8rows.png"_s));
            // one taken out again: the window a row lower, the rows after it keep their own pictures
            bt->cur = u"dock"_s;
            bt->_remove();
            settle();
            const pb::Window w7 = gw();
            check(bt->keys().size() == 7 && w7.fits == 7 && w7.step == 35 &&
                      w7.slot_sprites.value(6) == u"GFX_province_inn_strip",
                  u"Dock removed: 7 rows of %1 px, slot 6 shows %2"_s.arg(w7.step).arg(w7.slot_sprites.value(6)));
            for (int i = 0; i < 5; ++i)              // (the removal and the four new ones taken back)
                win.undo();
            settle();
            check(bt->keys().size() == 4 && gw().fits == 4 && gw().view_y == -563, u"…all undone: 4 rows again"_s);
        }

        const bool saved = win.save();
        settle();
        check(saved && win.doc->pending_count() == 0, u"Apply"_s);
        win.doc->undo_stack.clear();
    }
    // the files
    const QString b = read(pypath::join(folder, u"common/buildings.txt"_s));
    check(pb::province_keys(b).contains(K) && b.contains(u"supply_limit = 5"_s), u"buildings.txt: test_bank, effect"_s);
    const QString t = read(pypath::join(folder, u"technologies/commerce_tech.txt"_s));
    check(t.contains(u"activate_building = test_bank"_s) && t.contains(u"max_test_bank = 1"_s), u"commerce_tech.txt"_s);
    QString lon;
    for (QDirIterator it(pypath::join(folder, u"history/provinces"_s), QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString f = it.next();
        if (QFileInfo(f).fileName().startsWith(u"300 "))
            lon = read(f);
    }
    check(lon.contains(u"test_bank = 1"_s), u"London's history: test_bank = 1"_s);
    const QString gui = read(pypath::join(folder, u"interface/province_interface.gui"_s));
    check(gui.contains(u"build_icon3"_s) && gui.contains(u"y=-563"_s), u"province_interface.gui in the mod"_s);
    const QString gfx = read(pypath::join(folder, u"interface/modeditor_buildings.gfx"_s));
    check(gfx.contains(u"GFX_province_test_bank_strip"_s) && gfx.contains(u"GFX_provinceview_bg_modeditor"_s),
          u"modeditor_buildings.gfx: both sprites"_s);
    const auto strip = imgio::read_image(pypath::join(folder, u"gfx/interface/province_test_bank_strip.tga"_s));
    const auto bg = imgio::read_image(pypath::join(folder, u"gfx/interface/province_bg_modeditor.tga"_s));
    check(strip && strip->size() == QSize(280, 32), u"the strip picture (7 x 40 x 32)"_s);
    check(bg && bg->size() == QSize(416, 583), u"the taller background picture"_s);
    if (strip)
        strip->scaled(840, 96).save(pypath::join(scratch, u"strip_seen.png"_s));
    QString locs;
    for (QDirIterator it(pypath::join(folder, u"localisation"_s), QDir::Files); it.hasNext();)
        locs += read(it.next());
    check(locs.contains(u"test_bank;Test Bank;"_s), u"its name in the mod's localisation"_s);
    // opened again: the level read back from the history
    {
        auto m = std::make_shared<moddata::Mod>(folder, BASE, u"Bank Test"_s);
        m->load();
        const auto sc = m->scenarios();
        auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
        mainwindow::MainWindow win(m, mdp, sc[0].name, pypath::join(scratch, u"backups"_s));
        win.show();
        settle();
        check(win.doc->hist->more == QStringList{K} && win.doc->prov_state(LONDON).level_of(K) == 1,
              u"opened again: London starts with level 1"_s);
        win.go_page(u"rules"_s);
        win.rules_ws->set_page(u"buildings"_s);
        settle();
        check(win.bt_panel->window_problems().empty(), u"…nothing to fix in the province window"_s);
        // a layout for the game: Test Bank in row 1, a fifth building, rows of 30 px — written, then seen in the game
        buildtypes::BuildTypesPanel *b2 = win.bt_panel;
        b2->_move(K, 0);
        settle();
        b2->new_name->setText(u"Grain Mill"_s);
        b2->_make_new();
        settle();
        b2->_row_height(30);
        settle();
        auto &store = win.doc->map<scriptstore::ScriptStore>(u"scripts"_s);
        const pb::Window wl = pb::read_window(store.text(b2->gui_rel()));
        check(b2->keys() == QStringList({K, u"fort"_s, u"naval_base"_s, u"railroad"_s, u"grain_mill"_s}) && wl.fits == 5 &&
                  wl.step == 30 && wl.view_y == -573 && b2->window_problems().empty(),
              u"layout: %1 — 5 rows of %2 px, window %3"_s.arg(b2->keys().join(u", "_s)).arg(wl.step).arg(wl.view_y));
        // the game draws slots from build_icon3 on in every row (measured): parked, their pictures in the background
        check(wl.parked(3) && wl.parked(4) && !wl.parked(0) && !wl.parked(2) && wl.icon_x == 23 &&
                  wl.slot_sprites.value(3) == u"GFX_province_infra_strip" &&
                  wl.slot_sprites.value(4) == u"GFX_province_grain_mill_strip",
              u"…slots 3 and 4 parked outside the window (%1, %2)"_s.arg(wl.slot_sprites.value(3), wl.slot_sprites.value(4)));
        b2->win_view->grab().save(pypath::join(scratch, u"window_layout.png"_s));
        check(win.save() && win.doc->pending_count() == 0, u"…Apply"_s);
        win.doc->undo_stack.clear();
    }
    {
        const auto bgl = imgio::read_image(pypath::join(folder, u"gfx/interface/province_bg_modeditor.tga"_s));
        check(bgl && bgl->size() == QSize(416, 548 - 105 + 150), u"the background for 5 rows of 30 px (%1 high)"_s.arg(
                                                                        bgl ? bgl->height() : 0));
        const QString g = read(pypath::join(folder, u"interface/province_interface.gui"_s));
        check(g.count(u"ModEditor province window"_s) == 1 && g.contains(u"background GFX_provinceview_bg, rows at 362"_s),
              u"…its note in the .gui: made from the game's own picture"_s);
        // the 5th row's picture (Grain Mill, without its level number) painted into the background at its row
        const auto mill = imgio::read_image(pypath::join(folder, u"gfx/interface/province_grain_mill_strip.tga"_s));
        const pb::Window wg = pb::read_window(g);
        if (bgl && mill) {
            const QImage plain = pb::plain_picture(*mill, mill->width() / 40);
            const int x = wg.part_x - wg.bg_x + wg.icon_x, y = 362 + 4 * 30 + wg.icon_y;
            const QImage got = bgl->convertToFormat(QImage::Format_ARGB32).copy(x, y, plain.width(), plain.height());
            check(got == plain.convertToFormat(QImage::Format_ARGB32),
                  u"…the 5th row's picture painted into it at %1, %2"_s.arg(x).arg(y));
            bgl->copy(0, 300, 416, 300).scaled(832, 600).save(pypath::join(scratch, u"bg_rows_painted.png"_s));
        }
    }
    std::printf("buildtypes: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
