// The start of the bar (C++ only): the way out to the list of mods, the name as a read-only text box that a double click
// makes writable, Esc keeps the name, a bad name stays in the box with a red note, the same name writes nothing.
// Ferrum Mare — nothing is renamed (its .mod file in the work folder must stay as it is).
// Usage: med_modname_check <work folder>   (offscreen, MODEDITOR_SETTINGS = a settings file of its own)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/mapview.h"
#include "ui/theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    if (!c)
        ++fails;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    if (argc < 2)
        return 2;
    const QString work = QString::fromLocal8Bit(argv[1]);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s),
                                            pypath::join(work, u"Victoria 2"_s), std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, QDir::temp().filePath(u"modname_check_backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    win.show();
    QApplication::processEvents();
    const QString name0 = m->name();
    // the bar's start (the user 2026-11-17): the way out, the name as a text box, the scenario, then the search
    ok(win.mod_btn->text().contains(u"Mod list"_s), u"the first button is the way out: "_s + win.mod_btn->text());
    ok(win.mod_name_edit->isVisible() && win.mod_name_edit->isReadOnly() && win.mod_name_edit->text() == name0,
       u"the name is shown in a text box (read only until a double click): "_s + win.mod_name_edit->text());
    ok(win.mod_name_edit->actions().size() == 1, u"…with a ✎ in it"_s);
    QWidget *pal = nullptr, *scen = nullptr;
    for (QPushButton *b : win.findChildren<QPushButton *>())
        if (b->text().startsWith(u"⌕"_s))
            pal = b;
    for (QComboBox *cb : win.findChildren<QComboBox *>())
        if (cb == win.scenario_box)
            scen = cb;
    auto xof = [&](QWidget *w) { return w ? w->mapTo(&win, QPoint(0, 0)).x() : -1; };
    ok(xof(win.mod_btn) < xof(win.mod_name_edit) && xof(win.mod_name_edit) < xof(scen) && xof(scen) < xof(pal) &&
           xof(pal) < xof(win.search),
       u"order: Mod list, name, scenario, ⌕ search, … the province search (x %1 %2 %3 %4 %5)"_s.arg(xof(win.mod_btn))
           .arg(xof(win.mod_name_edit)).arg(xof(scen)).arg(xof(pal)).arg(xof(win.search)));
    // the double click on the name
    const QPointF c(win.mod_name_edit->rect().center());
    QMouseEvent dbl(QEvent::MouseButtonDblClick, c, win.mod_name_edit->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
    QApplication::sendEvent(win.mod_name_edit, &dbl);
    QApplication::processEvents();
    ok(!win.mod_name_edit->isReadOnly() && win.mod_name_edit->text() == name0 && !win.switch_requested,
       u"double click: the name can be written (no other mod opened)"_s);
    // Esc keeps the name
    win.mod_name_edit->setText(u"Something else"_s);
    QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(win.mod_name_edit, &so);
    ok(so.isAccepted(), u"Esc is the box's own key (not the window's shortcut)"_s);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(win.mod_name_edit, &esc);
    QApplication::processEvents();
    ok(win.mod_name_edit->isReadOnly() && win.mod_name_edit->text() == name0 && m->name() == name0,
       u"Esc: read only again, the old name back"_s);
    // a bad name: the box stays writable, a red note
    QApplication::sendEvent(win.mod_name_edit, &dbl);
    QApplication::processEvents();
    win.mod_name_edit->setText(u"Bad \"name"_s);
    Q_EMIT win.mod_name_edit->textEdited(win.mod_name_edit->text());
    ok(win.mod_name_note->isVisibleTo(&win) && win.mod_name_note->text().startsWith(u"⚠"_s),
       u"typing a quote: red note at once — "_s + win.mod_name_note->text());
    win.end_rename(true);
    ok(!win.mod_name_edit->isReadOnly() && m->name() == name0, u"Enter on a bad name: the box stays open, name kept"_s);
    // Enter with the name unchanged: nothing written. (A real rename is NOT tried here: this mod's .mod file is the work
    // folder's own — 2026-11-08 a first version of this check renamed it. The writing is checked by
    // med_modrename_check in a mods folder of its own.)
    const QString modfile = pypath::join(work, u"Ferrum_Mare 1.2.000.mod"_s);
    const QDateTime before = QFileInfo(modfile).lastModified();
    win.mod_name_edit->setText(u"  "_s + name0 + u" "_s);
    win.end_rename(true);
    QApplication::processEvents();
    ok(win.mod_name_edit->isReadOnly() && win.mod_name_edit->text() == name0 && m->name() == name0,
       u"the same name: read only again, name kept"_s);
    ok(QFileInfo(modfile).lastModified() == before, u"…and the .mod file not written"_s);
    ok(!win.switch_requested, u"no other mod was opened along the way"_s);
    // the way out: one click (nothing pending here, so nothing to ask)
    win.mod_btn->click();
    QApplication::processEvents();
    ok(win.switch_requested, u"Mod list: one click goes back to the list of mods"_s);    std::printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
