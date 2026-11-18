// Population → Edit cultures… against Python, the C++ side of tools/check_culturewin.py.
// Usage: med_culturewin_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/culturewin.h"
#include "ui/entryview.h"
#include "ui/mapview.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

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

static void item_rows(QTreeWidget *t, QTreeWidgetItem *it, int depth, QStringList &rows)
{
    QStringList cols;
    for (int i = 0; i < t->columnCount(); ++i)
        cols << it->text(i);
    rows << QString::number(depth) + u' ' + cols.join(u'\t');
    for (int c = 0; c < it->childCount(); ++c)
        item_rows(t, it->child(c), depth + 1, rows);
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4"_s.arg(v(b->text()), v(b->isChecked()), en, v(b->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L "_s + v(l->text());
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2%3"_s.arg(v(e->text()), v(e->placeholderText()), en);
    if (auto *c = qobject_cast<QComboBox *>(w))
        return u"C %1 %2 %3%4"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                     v(c->currentText()), en);
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i)
            items << l->item(i)->text();
        return u"Q %1 %2 %3"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                   h(items.join(u'\n').toUtf8()));
    }
    if (auto *t = qobject_cast<QTreeWidget *>(w)) {
        QStringList rows;
        for (int r = 0; r < t->topLevelItemCount(); ++r)
            item_rows(t, t->topLevelItem(r), 0, rows);
        return u"T %1 %2 %3"_s.arg(QString::number(t->columnCount()), QString::number(t->topLevelItemCount()),
                                   h(rows.join(u'\n').toUtf8()));
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
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    auto *dlg = new culturewin::CulturesWindow(&win, *m, *win.doc, backups, 0);
    win.cg_win = dlg->groups;
    dlg->show();
    QApplication::processEvents();

    auto snap = [&](const QString &tag) {
        QStringList sw;
        for (QPushButton *b : dlg->btns)
            sw << v(b->isChecked());
        out << u"%1 | title %2 | visible %3 | switch %4 | page %5 | msg %6"_s.arg(
            tag, v(dlg->windowTitle()), v(dlg->isVisible()), sw.join(u','), QString::number(dlg->stack->currentIndex()),
            v(win.st_msg->text()));
        for (QWidget *w : widgets_in(dlg, dlg))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    snap(u"opened"_s);
    dlg->btns[1]->click();
    QApplication::processEvents();
    snap(u"group settings"_s);
    dlg->btns[0]->click();
    QApplication::processEvents();
    snap(u"back"_s);
    dlg->reject();
    QApplication::processEvents();
    snap(u"esc"_s);
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
