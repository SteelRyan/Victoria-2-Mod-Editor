// Tools → Submods & 4K against Python, the C++ side of tools/check_submodpage.py.
// Usage: med_submodpage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/fourk.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/submodpage.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QList<QWidget *> kids_of(QWidget *w)
{
    QList<QWidget *> got;
    if (auto *sp = qobject_cast<QSplitter *>(w)) {
        for (int i = 0; i < sp->count(); ++i)
            got << sp->widget(i);
        return got;
    }
    if (auto *sa = qobject_cast<QScrollArea *>(w)) {
        if (sa->widget())
            got << sa->widget();
        return got;
    }
    if (auto *st = qobject_cast<QStackedWidget *>(w)) {
        if (st->currentWidget())
            got << st->currentWidget();
        return got;
    }
    std::function<void(QLayout *)> lay_walk = [&](QLayout *lay) {
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *it = lay->itemAt(i);
            if (it->widget())
                got << it->widget();
            else if (it->layout())
                lay_walk(it->layout());
        }
    };
    if (w->layout())
        lay_walk(w->layout());
    return got;
}

static QList<QWidget *> widgets_in(QWidget *w, QWidget *root)
{
    QList<QWidget *> res;
    for (QWidget *c : kids_of(w))
        if (c->isVisibleTo(root)) {
            res << c;
            res << widgets_in(c, root);
        }
    return res;
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4"_s.arg(v(b->text()), v(b->isChecked()), en, v(b->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2%3"_s.arg(v(e->text()), v(e->placeholderText()), en);
    if (auto *g = qobject_cast<QProgressBar *>(w))
        return u"G %1 %2"_s.arg(g->value()).arg(g->maximum());
    if (auto *s = qobject_cast<QSplitter *>(w)) {
        QStringList sz;
        for (int x : s->sizes())
            sz << QString::number(x);
        return u"S "_s + sz.join(u',');
    }
    return std::nullopt;
}

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();
    submodpage::SubmodWorkspace *ws = win.sub_ws;

    auto snap = [&](const QString &tag) {
        out << u"%1 | tab %2 | msg %3 | %4"_s.arg(tag, win.tab, v(win.st_msg->text()), win.st_pending->text());
        for (QWidget *w : widgets_in(ws, ws))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto grab = [&](const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        ws->grab().save(QDir(out_dir).filePath(name));
    };
    auto pe = [] { QApplication::processEvents(); };

    win.set_tab(u"submods"_s);
    pe();
    snap(u"shown"_s);
    grab(u"sub_shown.png"_s);
    {                                              // (C++ only: the 4K screen, for a look — not compared)
        QDir().mkpath(out_dir + u"/../extra"_s);
        auto whole = [&](const QString &name) {    // (the whole left column, also what lies below the window)
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 400)
                QApplication::processEvents();
            for (QScrollArea *sa : ws->findChildren<QScrollArea *>())
                if (sa->widget() && sa->widget()->isAncestorOf(ws->screen))
                    sa->widget()->grab().save(out_dir + u"/../extra/"_s + name);
        };
        whole(u"sub_4k_default.png"_s);
        for (size_t i = 0; i < ws->screen->windows.size(); ++i) {   // (each window on it at 1 ×, with its place)
            const auto &o = ws->screen->windows[i];
            o.img.save(out_dir + u"/../extra/sub_4k_win%1_%2x%3.png"_s.arg(i).arg(int(o.x)).arg(int(o.y)));
        }
        fourk::Sizes s;
        s.topbar = 1.5;
        s.unitpanel = 1.0;
        s.marks = 1.5;
        s.names = 2.0;
        ws->set_sizes(s);
        ws->screen->set_view(ws->screen->cam.x, ws->screen->cam.z, 150);
        whole(u"sub_4k_tested.png"_s);
        ws->set_sizes({});
        ws->screen->set_view(ws->screen->cam.x, ws->screen->cam.z, gamecam::START_H);
    }
    ws->code_btn->click();
    pe();
    snap(u"codes"_s);
    ws->make_btn->click();                         // (the folder is there: only asks for a second click)
    pe();
    snap(u"armed"_s);
    for (const QString &name : {u"a/b"_s, u"mod"_s, u"fresh 4K"_s}) {
        ws->folder->setText(name);
        pe();
        snap(u"folder "_s + name);
    }
    const QString game_mods = pypath::normcase(pypath::join(m->base.value_or(QString()), u"mod"_s));
    if (pypath::normcase(pypath::abspath(ws->mods_dir())) != game_mods) {   // (never a real game start)
        ws->ck_btn->click();
        pe();
    }
    snap(u"measure"_s);
    grab(u"sub_measure.png"_s);
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
    QApplication::setCursorFlashTime(0);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
