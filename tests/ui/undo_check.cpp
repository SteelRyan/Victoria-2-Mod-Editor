// Ctrl+Z / Ctrl+Y take back ONE change at a time, newest first, on every page (C++ only, the user 2026-11-13: "in
// every single function, the Ctrl+Z for undoing something works correctly, works in small steps, and works
// chronologically correctly in every single tab"). Ferrum Mare in the real main window, offscreen: a row of edits made
// as a user makes them (clicks, typing + Enter, brush strokes), then REAL key presses (QTest, through Qt's shortcut
// map, with the focus where the last edit left it). After each edit the picture of everything unapplied (the recovery
// file's picture) is kept; every Ctrl+Z must bring back the picture before the newest edit left, every Ctrl+Y the one
// after it. Nothing is ever applied: the mod on disk is not touched.
// Usage: med_undo_check <work folder>   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/recovery.h"
#include "ui/dialogs.h"
#include "ui/diplopanel.h"
#include "ui/drawpanel.h"
#include "ui/scriptedit.h"
#include "ui/nationpanel.h"
#include "ui/mapview.h"
#include "ui/panels.h"
#include "ui/provpanel.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

static mainwindow::MainWindow *g_win = nullptr;
static int fails = 0;

static void flush()
{
    for (int i = 0; i < 4; ++i)
        QApplication::processEvents();
}

// the picture of everything unapplied, as one short text
static QString picture()
{
    flush();
    const auto pic = recovery::picture(*g_win->doc);
    if (!pic)
        return u"(nothing pending)"_s;
    recovery::Data d;
    d.version = recovery::VERSION;
    d.picture = *pic;
    return QString::fromLatin1(QCryptographicHash::hash(recovery::pack(d), QCryptographicHash::Sha1).toHex().left(12));
}

static QString focus_name()
{
    QWidget *f = QApplication::focusWidget();
    if (!f)
        return u"(no focus)"_s;
    return QString::fromLatin1(f->metaObject()->className()) + (f->objectName().isEmpty() ? QString() : u" #"_s + f->objectName());
}

static void key(Qt::Key k)
{
    QWidget *f = QApplication::focusWidget();
    QTest::keyClick(f ? f : static_cast<QWidget *>(g_win), k, Qt::ControlModifier);
    flush();
}

static void check(bool ok, const QString &what)
{
    std::printf("      %s %s\n", ok ? "ok  " : "FAIL", qUtf8Printable(what));
    fails += int(!ok);
}

static void type_into(QLineEdit *ed, const QString &text)
{
    ed->setFocus(Qt::MouseFocusReason);
    flush();
    ed->selectAll();
    QTest::keyClicks(ed, text);
    QTest::keyClick(ed, Qt::Key_Return);
    flush();
}

struct Act {
    QString name;
    std::function<void()> fn;
    bool held = false;                            // the panel writes it a moment later (a slider): Ctrl+Z comes first
};

static int run(const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, QDir::temp().filePath(u"undo_check_backups"_s));
    g_win = &win;
    win.resize(1500, 900);
    win.show();
    win.activateWindow();
    (void)QTest::qWaitForWindowActive(&win, 3000);
    flush();
    // two land provinces with pops, side by side
    long long A = 0, B = 0;
    for (long long y = 300; y < md.h - 300 && !B; y += 7) {
        const std::uint16_t *row = md.idmap.row(y);
        for (long long x = 300; x < md.w - 300 && !B; x += 3) {
            const long long a = row[x], b = row[x + 1];
            if (a && b && a != b && m->provinces.contains(a) && m->provinces.contains(b) && md.area[size_t(a)] > 800 &&
                md.area[size_t(b)] > 800 && !m->provinces.get(a)->sea && !m->provinces.get(b)->sea &&
                !win.doc->province_pops(a).empty() && !win.doc->province_pops(b).empty()) {
                A = a;
                B = b;
            }
        }
    }
    std::printf("provinces %lld and %lld\n", A, B);
    auto step_btn = [&](const QString &text) -> QPushButton * {
        for (QPushButton *b : win.edit_panel->step_buttons)
            if (b->text() == text)
                return b;
        return nullptr;
    };
    drawpanel::DrawPanel &dp = *win.draw_panel;
    const double cx = md.cx[size_t(A)], cy = md.cy[size_t(A)];
    auto stroke = [&](double dy) {
        drawpanel::DrawHandler &hd = dp.prov_handler;
        hd.press(cx + 0.5, cy + 0.5 + dy, 1.0, Qt::NoModifier);
        for (int k = 1; k <= 12; ++k)
            hd.move(cx + 0.5 + k, cy + 0.5 + dy);
        hd.release(cx + 12.5, cy + 0.5 + dy, 1.0, Qt::NoModifier);
    };
    const std::vector<Act> acts = {
        {u"Pops: +10% and a typed total wait; Ctrl+Z / Ctrl+Y step through them; Apply changes = one step"_s, [&] {
             panels::EditPanel &ep = *win.edit_panel;
             win.set_tab(u"pops"_s);
             win.set_selection({A});
             flush();
             const QString pic0 = picture();
             const QString t0 = ep.total_edit->text();
             QTest::mouseClick(step_btn(u"+10%"_s), Qt::LeftButton);
             flush();
             const QString t1 = ep.total_edit->text();
             type_into(ep.total_edit, u"123456"_s);
             check(picture() == pic0, u"the clicks and the typed total wait (the pops are not changed yet)"_s);
             check(ep.total_edit->text() == u"123,456"_s, u"the box shows the waiting total"_s);
             key(Qt::Key_Z);
             check(ep.total_edit->text() == t1, u"Ctrl+Z (cursor in the box): the typed total goes, +10% stays"_s);
             key(Qt::Key_Z);
             check(ep.total_edit->text() == t0 && !ep.pending_live(), u"Ctrl+Z: +10% goes too"_s);
             key(Qt::Key_Y);
             key(Qt::Key_Y);
             check(ep.total_edit->text() == u"123,456"_s, u"Ctrl+Y twice: both back"_s);
             check(picture() == pic0, u"… still nothing written"_s);
             QTest::mouseClick(ep.mix_apply, Qt::LeftButton);
             flush();
             check(ep._cur_total() == 123456 && !ep.pending_live(), u"Apply changes: the pops hold the new total"_s);
         }},
        {u"Pops: a new mix share AND a new total, one Apply: both happen"_s, [&] {
             panels::EditPanel &ep = *win.edit_panel;
             if (ep.mix_segments->size() < 2) {     // one culture only: a second one added ("＋ Add culture…")
                 dialogs::PickCombo *ac = ep.mix_add_culture;
                 for (int i = 0; i < ac->count(); ++i) {
                     ac->setCurrentIndex(i);
                     const auto k = ac->key();
                     if (k && !k->isEmpty() && k != ep.mix_segments->front().key)
                         break;
                 }
                 Q_EMIT ac->activated(ac->currentIndex());
                 flush();
             }
             check(ep.mix_segments->size() >= 2, u"two cultures in the mix"_s);
             QDoubleSpinBox *spin = ep.mix_rows.front().first;
             const auto k0 = ep.mix_segments->front().key;
             spin->setValue(spin->value() > 50 ? 30.0 : 70.0);
             const double want = ep.mix_segments->front().pct;
             flush();
             QTest::mouseClick(step_btn(u"+20%"_s), Qt::LeftButton);
             flush();
             const long long nt = py::round_int(123456 * 1.2);
             QTest::mouseClick(ep.mix_apply, Qt::LeftButton);
             flush();
             long long tot = 0, of_k = 0;
             for (const moddata::Pop *p : win.doc->selected_pops({A}, ep.current_filter())) {
                 tot += p->size;
                 if (ep._field_of(*p) == k0)
                     of_k += p->size;
             }
             check(tot == nt, u"the total holds (%1, wanted %2)"_s.arg(tot).arg(nt));
             check(tot && std::abs(100.0 * double(of_k) / double(tot) - want) < 0.01,
                   u"the mix holds (%1 %, wanted %2 %)"_s.arg(tot ? 100.0 * double(of_k) / double(tot) : 0).arg(want));
         }},
        {u"Pops: a waiting +5% left behind (bar), gone back to, applied from the bar"_s, [&] {
             panels::EditPanel &ep = *win.edit_panel;
             QTest::mouseClick(step_btn(u"+5%"_s), Qt::LeftButton);
             flush();
             win.set_selection({B});
             flush();
             check(win.pend_bar->isVisible(), u"another province picked: the warning bar shows"_s);
             win._pend_go_back();
             flush();
             check(!win.pend_bar->isVisible() && ep.pend_shown() && ep.pending_live(),
                   u"Go back: the waiting +5% is shown again"_s);
             win.set_tab(u"nations"_s);
             flush();
             check(win.pend_bar->isVisible(), u"another tab: the bar shows"_s);
             ep.apply_pending();
             flush();
             check(!win.pend_bar->isVisible() && !ep.pending_live(), u"Apply from the bar: done, the bar goes"_s);
         }},
        {u"Provinces: type a province name + Enter"_s, [&] {
             win.set_tab(u"provinces"_s);
             win.set_selection({B});
             flush();
             type_into(win.prov_panel->name_edit, u"Undoburg"_s);
         }},
        {u"Drawing: province stroke 1"_s, [&] {
             win.set_tab(u"map"_s);
             win.set_map_sub(u"draw"_s);
             if (!dp.edit_btn->isChecked())
                 win.toggle_draw();
             dp.set_layer(u"prov"_s);
             dp.tool_btns.at(u"brush"_s)->setChecked(true);
             dp.set_paint(A);
             dp.size_spin->setValue(4);
             flush();
             stroke(-20);
         }},
        {u"Drawing: province stroke 2"_s, [&] { stroke(-10); }},
        {u"Drawing: province stroke 3"_s, [&] { stroke(0); }},
        {u"Drawing: terrain stroke"_s, [&] {
             dp.set_layer(u"terrain"_s);
             flush();
             terrainpanel::TerrainBox &tb = *dp.terrain_box;
             if (!tb.index)
                 tb.set_index(1);
             tb.size_spin->setValue(4);
             terrainpanel::TerrainHandler &th = tb.handler;
             th.press(cx + 0.5, cy + 30.5, 1.0, Qt::NoModifier);
             for (int k = 1; k <= 12; ++k)
                 th.move(cx + 0.5 + k, cy + 30.5);
             th.release(cx + 12.5, cy + 30.5, 1.0, Qt::NoModifier);
         }},
        {u"Provinces: fort + 1"_s, [&] {
             win.set_tab(u"provinces"_s);
             win.set_selection({A});
             flush();
             for (auto &[k, row] : win.prov_panel->level_rows)
                 if (k == u"fort"_s)
                     QTest::mouseClick(row.plus, Qt::LeftButton);
         }},
        {u"Provinces: another owner (list box)"_s, [&] {
             dialogs::PickCombo *o = win.prov_panel->owner;
             const auto now = o->key();
             for (int i = 0; i < o->count(); ++i) {
                 o->setCurrentIndex(i);
                 if (o->key() && !o->key()->isEmpty() && o->key() != now && *o->key() != u"__mixed__"_s)
                     break;
             }
             Q_EMIT o->activated(o->currentIndex());
         }},
        {u"Nations: tick 'civilized'"_s, [&] {
             win.set_tab(u"nations"_s);
             win.show_nation(win.doc->prov_state(B).owner);
             flush();
             QCheckBox *cb = win.nations_panel->civilized;   // (the box is stretched across the row: click its tick)
             QTest::mouseClick(cb, Qt::LeftButton, {}, QPoint(8, cb->height() / 2));
         }},
        {u"Nations: a slider's box typed + Enter, Ctrl+Z before it rests"_s, [&] {
             widgets::SliderField *sf = win.nations_panel->sliders.front().second;
             type_into(sf->edit, QString::number(py::trunc_int(sf->value()) + 7));
         }, true},
        {u"Diplomacy: tick 'alliance' between two nations"_s, [&] {
             win.set_tab(u"diplomacy"_s);
             win.show_nation(win.doc->prov_state(A).owner);
             win.show_other(*win.doc->prov_state(B).owner);
             flush();
             QCheckBox *cb = win.diplo_panel->alliance;
             QTest::mouseClick(cb, Qt::LeftButton, {}, QPoint(8, cb->height() / 2));
         }},
        {u"Diplomacy: relation box typed + Enter, Ctrl+Z before it rests"_s, [&] {
             widgets::SliderField *sf = win.diplo_panel->rel;
             type_into(sf->edit, QString::number(py::trunc_int(sf->value()) + 13));
         }, true},
        {u"Scripts: type a line into an event file"_s, [&] {
             win.set_tab(u"events"_s);
             const QStringList ev = QDir(pypath::join(m->root, u"events"_s)).entryList({u"*.txt"_s}, QDir::Files,
                                                                                       QDir::Name);
             scriptedit::ScriptEditor *ed = win.script_ws->open(u"events/"_s + ev.front());
             flush();
             ed->setFocus(Qt::MouseFocusReason);
             ed->moveCursor(QTextCursor::End);
             QTest::keyClick(ed, Qt::Key_Return);
             QTest::keyClicks(ed, u"# undo check"_s);
             flush();
         }},
        {u"Scripts: a second line, later"_s, [&] {
             QTest::qWait(1700);                  // (past the typing merge: a step of its own)
             scriptedit::ScriptEditor *ed = win.script_ws->current();
             ed->setFocus(Qt::MouseFocusReason);
             ed->moveCursor(QTextCursor::End);
             QTest::keyClick(ed, Qt::Key_Return);
             QTest::keyClicks(ed, u"# second"_s);
             flush();
         }},
    };
    std::vector<QString> pics{picture()};
    std::vector<size_t> depth{win.doc->undo_stack.size()};
    for (const Act &a : acts) {
        a.fn();
        if (a.held) {                             // Ctrl+Z at once, before the panel writes it: it must go, alone
            const QString where = focus_name();
            key(Qt::Key_Z);
            const bool ok_z = picture() == pics.back() && win.doc->undo_stack.size() == depth.back();
            key(Qt::Key_Y);
            std::printf("  %s Ctrl+Z before the held edit is written (focus: %s)\n", ok_z ? "ok  " : "FAIL",
                        qUtf8Printable(where));
            fails += int(!ok_z);
            QTest::qWait(700);
        }
        const QString p = picture();
        const size_t d = win.doc->undo_stack.size();
        std::printf("  did   %-62s steps %zu -> %zu   %s\n", qUtf8Printable(a.name), depth.back(), d, qUtf8Printable(p));
        // at once: Ctrl+Z with the focus where this edit left it, then Ctrl+Y
        {
            const QString where = focus_name();
            key(Qt::Key_Z);
            const bool ok_z = picture() == pics.back();
            key(Qt::Key_Y);
            const bool ok_y = picture() == p;
            std::printf("  %s Ctrl+Z at once (focus: %s)\n  %s Ctrl+Y at once\n", ok_z ? "ok  " : "FAIL",
                        qUtf8Printable(where), ok_y ? "ok  " : "FAIL");
            fails += int(!ok_z) + int(!ok_y);
            if (!ok_y) {                            // (put it back the way it was for the rest)
                while (win.doc->undo_stack.size() > d)
                    win.undo();
                while (win.doc->undo_stack.size() < d && !win.doc->redo_stack.empty())
                    win.redo();
            }
        }
        if (d != depth.back() + 1) {
            ++fails;
            std::printf("FAIL: \"%s\" made %lld undo steps (one expected)\n", qUtf8Printable(a.name),
                        (long long)d - (long long)depth.back());
        }
        if (p == pics.back()) {
            ++fails;
            std::printf("FAIL: \"%s\" changed nothing that is pending\n", qUtf8Printable(a.name));
        }
        pics.push_back(p);
        depth.push_back(d);
    }
    // Ctrl+Z, newest first, with the focus where the last edit left it
    for (size_t i = acts.size(); i > 0; --i) {
        const QString where = focus_name();
        key(Qt::Key_Z);
        const QString p = picture();
        const bool ok = p == pics[i - 1];
        std::printf("  %s Ctrl+Z (focus: %s) should take back \"%s\"\n", ok ? "ok  " : "FAIL", qUtf8Printable(where),
                    qUtf8Printable(acts[i - 1].name));
        if (!ok) {
            ++fails;
            for (size_t j = 0; j < pics.size(); ++j)
                if (pics[j] == p)
                    std::printf("        (the pending edits are now as after edit %zu)\n", j);
            pics[i - 1] = p;                      // (go on from where it is)
        }
    }
    // Ctrl+Y, oldest first
    for (size_t i = 1; i <= acts.size(); ++i) {
        const QString where = focus_name();
        key(Qt::Key_Y);
        const QString p = picture();
        const bool ok = p == pics[i];
        std::printf("  %s Ctrl+Y (focus: %s) should bring back \"%s\"\n", ok ? "ok  " : "FAIL", qUtf8Printable(where),
                    qUtf8Printable(acts[i - 1].name));
        if (!ok)
            ++fails;
    }
    std::printf(fails ? "undo: FAIL (%d)\n" : "undo: PASS\n", fails);
    return fails ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    if (a.size() < 2) {
        std::printf("usage: med_undo_check <work folder>\n");
        return 2;
    }
    try {
        return run(a[1]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
