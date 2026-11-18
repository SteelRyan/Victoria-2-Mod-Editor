// The command palette (Ctrl+K) against Python, the C++ side of tools/check_palette.py.
// Usage: med_palette_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/colorwheel.h"
#include "ui/mapview.h"
#include "ui/moreviews.h"
#include "ui/palette.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <cstdio>

using namespace Qt::StringLiterals;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;
static const QStringList QUERIES = {u""_s, u"apply"_s, u"army"_s, u"armies"_s, u"tech steam"_s, u"save"_s, u"good"_s,
                                    u"coal"_s, u"nation"_s, u"map mode"_s, u"rebels"_s, u"add"_s, u"delete state"_s,
                                    u"pop"_s, u"fort"_s, u"zzz"_s, u"unit"_s, u"colour"_s, u"life"_s, u"canal"_s,
                                    u"flag"_s, u"search"_s, u"undo"_s};
static const QStringList RUN = {u"Technology"_s, u"Unit stats"_s, u"Change life rating"_s, u"Events"_s,
                                u"Graphical culture"_s, u"Make the 4K submod"_s, u"Draw terrain"_s};

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
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder both sides use)
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
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

    auto wait = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QApplication::processEvents();
            QThread::msleep(5);
        }
    };
    auto pic = [](QWidget *w) {
        const QImage img = w->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        return h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes()));
    };

    win.open_palette();
    palette::CommandPalette *pal = win.cmd_palette;
    QApplication::processEvents();

    auto state = [&](const QString &tag) {
        const QRect g = pal->geometry();
        out << u"%1 | vis %2 | geo %3 %4 %5 %6 | count %7 | rows %8 | cur %9"_s.arg(
                   tag, tf(pal->isVisible()), QString::number(g.x()), QString::number(g.y()), QString::number(g.width()),
                   QString::number(g.height()), pal->count->text(), QString::number(pal->lst->count()),
                   QString::number(pal->lst->currentRow())) +
                   u" | tab %1 | sub %2 | msg %3"_s.arg(win.tab, win.map_sub, win.st_msg->text());
        QSettings *s = win.settings.get();
        out << u"%1 | settings colours %2 | geometry %3 | col %4 %5 %6"_s.arg(
            tag, s->value(u"paletteColours"_s, QString()).toString(), s->value(u"paletteGeometry"_s, QString()).toString(),
            pal->col_btn->text(), tf(pal->col_btn->isChecked()), tf(pal->col_panel->isVisibleTo(pal)));
    };
    auto rows = [&](const QString &tag, int n) {
        for (int i = 0; i < std::min(n, pal->lst->count()); ++i) {
            QListWidgetItem *it = pal->lst->item(i);
            out << u"%1   %2 %3 | %4 | %5 | %6"_s.arg(tag, QString::number(it->data(Qt::UserRole).toULongLong()),
                                                    it->text(), it->data(Qt::UserRole + 1).toString(),
                                                    it->toolTip().replace(u'\n', u"¶"_s),
                                                    QString::number(pal->lst->visualItemRect(it).height()));
        }
    };
    auto key = [&](int k) {
        QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(pal->edit, &e);
    };
    auto repr = [](const QString &q) { return u"'"_s + q + u"'"_s; };

    state(u"open"_s);
    const std::vector<palette::Command> &cmds = pal->_all;
    QStringList joined;
    for (const auto &c : cmds)
        joined << c.title + u"\t"_s + c.where;
    out << u"commands %1 %2"_s.arg(cmds.size()).arg(h(joined.join(u'\n').toUtf8()));
    for (size_t i = 0; i < cmds.size(); ++i)
        out << u"cmd %1 %2 | %3 | %4"_s.arg(QString::number(i), cmds[i].title, cmds[i].where,
                                           palette::category(cmds[i].title, cmds[i].where));
    out << u"pic open "_s + pic(pal);
    for (const QString &q : QUERIES) {
        pal->edit->setText(q);
        QApplication::processEvents();
        state(u"q "_s + repr(q));
        rows(u"q "_s + repr(q), 30);
        if (q == u"army" || q == u"tech steam" || q == u"map mode")
            out << u"pic %1 %2"_s.arg(repr(q), pic(pal));
    }
    pal->edit->setText(u"map"_s);
    for (int k : {Qt::Key_Down, Qt::Key_Down, Qt::Key_Down, Qt::Key_Up, Qt::Key_Up, Qt::Key_Up, Qt::Key_Up}) {
        key(k);
        out << u"key %1 row %2"_s.arg(k).arg(pal->lst->currentRow());
    }
    key(Qt::Key_Escape);
    QApplication::processEvents();
    state(u"esc"_s);
    for (const QString &name : RUN) {
        win.st_msg->setText(u""_s);  // the 8-second status timer would race the waits (as the Python side)
        win.open_palette();
        QApplication::processEvents();
        pal->edit->setText(name);
        QApplication::processEvents();
        QListWidgetItem *it = nullptr;
        for (int i = 0; i < pal->lst->count() && !it; ++i)
            if (pal->_all[size_t(pal->lst->item(i)->data(Qt::UserRole).toULongLong())].title == name)
                it = pal->lst->item(i);
        if (!it) {
            out << u"run %1: not found"_s.arg(name);
            continue;
        }
        pal->lst->setCurrentItem(it);
        key(Qt::Key_Return);
        QApplication::processEvents();
        wait(300);
        const auto fl = win.findChildren<palette::Flash *>();
        state(u"run "_s + name);
        for (palette::Flash *f : fl) {
            const QRect g = f->geometry();
            out << u"run %1 flash %2 %3 %4 %5"_s.arg(name).arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
        }
        wait(1700);
    }
    win.open_palette();
    QApplication::processEvents();
    pal->col_btn->setChecked(true);
    QApplication::processEvents();
    state(u"colours open"_s);
    out << u"pic colours "_s + pic(pal);
    pal->_set_colour(u"tech"_s, u"#123456"_s);
    pal->_col_btns.at(u"map"_s).second->setChecked(true);
    pal->_col_btns.at(u"general"_s).second->setChecked(false);
    QApplication::processEvents();
    pal->edit->setText(u"tech"_s);
    QApplication::processEvents();
    state(u"colours set"_s);
    rows(u"colours set"_s, 5);
    out << u"pic colours set "_s + pic(pal);
    pal->col_btn->setChecked(false);
    QApplication::processEvents();
    state(u"colours folded"_s);
    pal->move(30, 40);
    pal->resize(500, 300);
    pal->_save_geometry();
    pal->hide();
    win.open_palette();
    QApplication::processEvents();
    state(u"reopened"_s);
    win.settings->setValue(u"paletteGeometry"_s, u"[2000, -50, 9000, 100]"_s);
    pal->hide();
    win.open_palette();
    QApplication::processEvents();
    state(u"clamped"_s);
    win.settings->setValue(u"paletteGeometry"_s, u"nonsense"_s);
    pal->hide();
    win.open_palette();
    QApplication::processEvents();
    state(u"bad geometry"_s);
    win.escape();
    QApplication::processEvents();
    state(u"window esc"_s);
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
