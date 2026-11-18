// Scripts → 🔢 IDs against Python, the C++ side of tools/check_reid.py.
// Usage: med_reid_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/blockcode.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/reid.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/checkpanel.h"
#include "ui/reidwin.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
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
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static const QStringList READ_TEXTS = {
    u"model = tidy\nstart = 500\nblock = x\nfiles = {\n\t\"events/a.txt\" = 7\n\tb = 9\n\tc = { }\n\td = x\n}\n"_s,
    u"model = weird\n"_s, QString(), u"start = 12\nfiles = { \"x.txt\" = 3 }\n"_s, u"model = running\nblock = 77 }\n"_s};

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

static QString btext(const QPushButton *b)      // (a WrapButton keeps its own text)
{
    if (const auto *wb = qobject_cast<const widgets::WrapButton *>(b))
        return wb->text();
    return b->text();
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4 | %5"_s.arg(v(btext(b)), v(b->isChecked()), en, v(b->toolTip()), v(b->styleSheet()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3%4"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()), en);
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2%3"_s.arg(v(e->text()), v(e->styleSheet()), en);
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i)
            items << u"%1/%2/%3"_s.arg(l->item(i)->text(), l->item(i)->data(Qt::UserRole).toString(),
                                       QString::number(static_cast<int>(l->item(i)->checkState())));
        const QString all = items.join(u'\n');
        return u"Q %1 %2 %3"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                   qEnvironmentVariableIsSet("ROWS") ? all : h(all.toUtf8()));
    }
    return std::nullopt;
}

static QString ids_str(const eventtools::Ids &ids)
{
    QStringList rows;
    for (const auto &[i, lst] : ids) {
        QStringList ps;
        for (const auto &[rel, pos, ln] : lst)
            ps << u"%1@%2+%3"_s.arg(rel, QString::number(pos), QString::number(ln));
        rows << i + u':' + ps.join(u';');
    }
    return u"%1 %2"_s.arg(QString::number(ids.size()), h(rows.join(u'\n').toUtf8()));
}

static QString sys_str(const std::optional<reid::System> &s)
{
    if (!s)
        return u"None"_s;
    QStringList fs;
    for (const auto &[r, f] : s->files)
        fs << u"%1=%2"_s.arg(r, QString::number(f));
    return u"%1 %2 %3 %4"_s.arg(s->model, QString::number(s->start), QString::number(s->block), v(fs.join(u';')));
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
    win.resize(1900, 1000);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    QObject::disconnect(win._check_timer, &QTimer::timeout, nullptr, nullptr);
    document::Document &d = *win.doc;
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);

    QStringList own;
    for (const QString &r : blockcode::files_of(*m, &store, u"events"_s))
        if (reid::is_own(*m, r))
            own << r;
    auto suggestions = [&](const QString &tag) {
        QStringList rs = own.mid(0, 6);
        rs << u"events/port_new_file.txt"_s << QString();
        for (const QString &r : rs)
            out << u"%1 suggest %2 %3"_s.arg(tag, r, v(reid::suggest(*m, store, r)));
    };

    // --- the data
    for (const QString &t : READ_TEXTS) {
        const auto s = reid::read_system(t);
        out << u"read %1 | %2"_s.arg(sys_str(s), s ? v(reid::write_system(*s)) : u"None"_s);
    }
    out << u"own %1 %2"_s.arg(QString::number(own.size()), v(own.mid(0, 12).join(u';')));
    out << u"calls "_s + ids_str(reid::calls(*m, store));
    out << u"defined %1 | all %2"_s.arg(ids_str(reid::defined(*m, store)), ids_str(reid::defined(*m, store, false)));
    suggestions(u"start"_s);

    reidwin::ReIdWindow *rw = nullptr;
    auto snap = [&](const QString &tag) {
        QStringList dirty = store.dirty();
        std::sort(dirty.begin(), dirty.end());
        QStringList texts;
        for (const QString &r : dirty)
            texts << u"%1:%2"_s.arg(r, h(store.text(r).toUtf8()));
        out << u"%1 | msg %2 | %3 | steps %4 | %5"_s.arg(tag, v(win.st_msg->text()), win.st_pending->text(),
                                                        QString::number(d.undo_stack.size()), texts.join(u' '));
        if (rw->_plan) {
            const auto &p = *rw->_plan;
            QStringList mp, pt;
            for (const auto &[a, b] : p.mapping)
                mp << a + u'>' + b;
            for (const auto &[r, t] : p.texts)
                pt << u"%1:%2"_s.arg(r, h(t.toUtf8()));
            out << u"%1 plan %2 %3 | sys %4 | texts %5 | cnt %6 %7 %8"_s.arg(
                tag, QString::number(p.mapping.size()), h(mp.join(u'\n').toUtf8()), sys_str(p.sys), pt.join(u' '),
                QString::number(p.cnt.defined), QString::number(p.cnt.calls), QString::number(p.cnt.files));
        }
        out << u"%1 window %2 %3 %4 %5"_s.arg(tag, rw->model, QString::number(rw->start->value()),
                                              QString::number(rw->block->value()), v(rw->picked_files().join(u';')));
        for (QWidget *w : widgets_in(rw, rw))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto grab = [&](QWidget *w, const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        w->grab().save(QDir(out_dir).filePath(name));
    };
    auto preview = [&] {
        rw->_timer->stop();
        rw->_preview();
        QApplication::processEvents();
    };

    // --- the window
    win._script_tool(u"ids"_s);
    QApplication::processEvents();
    rw = win.reid_win;
    rw->resize(1180, 760);
    QApplication::processEvents();
    snap(u"opened"_s);
    grab(rw, u"reid_open.png"_s);
    for (const QString &k : {u"running"_s, u"tidy"_s, u"blocks"_s}) {
        rw->_set_model(k);
        preview();
        snap(u"model "_s + k);
    }
    rw->start->setValue(20000);
    preview();
    snap(u"start 20000"_s);
    rw->block->setValue(50);
    preview();
    snap(u"block 50"_s);
    if (rw->files->count() > 1) {
        rw->files->item(1)->setCheckState(Qt::Unchecked);
        preview();
        snap(u"one unticked"_s);
    }
    rw->_tick_all(false);
    snap(u"none ticked"_s);
    rw->_tick_all(true);
    snap(u"all ticked"_s);
    grab(rw, u"reid_changed.png"_s);
    // --- Renumber
    rw->_go();
    QApplication::processEvents();
    QApplication::processEvents();
    snap(u"renumbered"_s);
    grab(rw, u"reid_done.png"_s);
    suggestions(u"after"_s);
    for (int i = 0; i < 2; ++i) {                  // (69 files at once: the second press confirms)
        out << u"undo "_s + v(win.undo());
        QApplication::processEvents();
    }
    snap(u"undone"_s);
    for (int i = 0; i < 2; ++i) {
        out << u"redo "_s + v(win.redo());
        QApplication::processEvents();
    }
    win._script_tool(u"ids"_s);
    QApplication::processEvents();
    snap(u"redone"_s);
    rw->_set_model(u"tidy"_s);
    preview();
    snap(u"tidy again"_s);
    rw->_system_only();
    QApplication::processEvents();
    snap(u"system only"_s);
    suggestions(u"tidy"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    snap(u"applied"_s);
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
