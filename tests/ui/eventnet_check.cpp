// The event network against Python, the C++ side of tools/check_eventnet.py.
// Usage: med_eventnet_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/eventnet.h"
#include "ui/eventpanel.h"
#include "ui/scriptedit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>

using namespace Qt::StringLiterals;
using pyform::v;

static constexpr int W = 1760, H = 1020;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

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

static QString uid_of(const events::Entry *e) { return e && e->uid ? *e->uid : u"None"_s; }

static QString dat(const QVariant &d)
{
    if (!d.isValid())
        return u"None"_s;
    return uid_of(reinterpret_cast<const events::Entry *>(d.value<quintptr>()));
}

static void item_rows(QTreeWidgetItem *it, int depth, QStringList &rows)
{
    rows << u"%1 %2/%3/%4/%5/%6\t%7"_s.arg(QString::number(depth), it->text(0), it->toolTip(0),
                                           it->foreground(0).color().name(), dat(it->data(0, Qt::UserRole)),
                                           QString::number(it->font(0).bold() ? 1 : 0),
                                           QString::number(it->isExpanded() ? 1 : 0));
    for (int c = 0; c < it->childCount(); ++c)
        item_rows(it->child(c), depth + 1, rows);
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
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3 | %4"_s.arg(v(c->text()), v(c->isChecked()), en, v(c->styleSheet()));
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4"_s.arg(v(btext(b)), v(b->isChecked()), en, v(b->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2"_s.arg(v(l->text()), v(l->styleSheet()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3%4"_s.arg(v(e->text()), v(e->placeholderText()), v(e->styleSheet()), en);
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i)
            items << u"%1/%2"_s.arg(c->itemText(i), c->itemData(i).toString());
        return u"C %1 %2 %3 %4%5"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                        v(c->currentText()), h(items.join(u'\n').toUtf8()), en);
    }
    if (auto *t = qobject_cast<QTreeWidget *>(w)) {
        QStringList rows;
        for (int r = 0; r < t->topLevelItemCount(); ++r)
            item_rows(t->topLevelItem(r), 0, rows);
        return u"T %1 %2"_s.arg(QString::number(t->topLevelItemCount()),
                                qEnvironmentVariableIsSet("ROWS") ? rows.join(u'\n') : h(rows.join(u'\n').toUtf8()));
    }
    if (auto *gv = qobject_cast<QGraphicsView *>(w)) {
        const QTransform t = gv->transform();
        const QRectF r = gv->sceneRect();
        return u"V %1 %2 %3 | %4 %5 %6 %7 | %8 %9"_s.arg(
            v(t.m11()), v(t.m22()), v(t.dx()), v(r.x()), v(r.y()), v(r.width()), v(r.height()),
            QString::number(gv->horizontalScrollBar()->value()), QString::number(gv->verticalScrollBar()->value()));
    }
    return std::nullopt;
}

static QString path_str(const QPainterPath &p)
{
    QStringList els;
    for (int i = 0; i < p.elementCount(); ++i) {
        const QPainterPath::Element el = p.elementAt(i);
        els << u"%1:%2,%3"_s.arg(QString::number(int(el.type)), v(el.x), v(el.y));
    }
    return els.join(u';');
}

static QStringList scene_rows(eventnet::NetView *view)
{
    QStringList rows;
    for (QGraphicsItem *it : view->scene()->items(Qt::AscendingOrder)) {
        if (auto *n = dynamic_cast<eventnet::NodeItem *>(it)) {
            rows << u"N %1 | %2,%3 z%4 m%5 d%6 | %7"_s.arg(n->title(), v(n->pos().x()), v(n->pos().y()), v(n->zValue()),
                                                       QString::number(n->mark), QString::number(n->dim ? 1 : 0),
                                                       n->toolTip());
        } else if (auto *p = qgraphicsitem_cast<QGraphicsPathItem *>(it)) {
            const QPen pen = p->pen();
            rows << u"P %1 | %2 %3 %4 z%5 | %6"_s.arg(path_str(p->path()), pen.color().name(QColor::HexArgb),
                                                     v(pen.widthF()), QString::number(int(pen.style())), v(p->zValue()),
                                                     p->toolTip());
        } else if (auto *r = qgraphicsitem_cast<QGraphicsRectItem *>(it)) {
            const QRectF rc = r->rect();
            rows << u"R %1 %2 %3 %4 z%5 %6"_s.arg(v(rc.x()), v(rc.y()), v(rc.width()), v(rc.height()), v(r->zValue()),
                                                 r->brush().color().name());
        } else if (auto *s = qgraphicsitem_cast<QGraphicsSimpleTextItem *>(it)) {
            rows << u"S %1 | %2,%3 z%4 %5"_s.arg(s->text(), v(s->pos().x()), v(s->pos().y()), v(s->zValue()),
                                                s->brush().color().name());
        } else {
            rows << u"? item"_s;
        }
    }
    return rows;
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
    eventpanel::EventPanel *ep = win.event_panel;
    scriptedit::ScriptWorkspace *ws = win.script_ws;

    auto wait = [&] {
        QElapsedTimer t;
        t.start();
        while (ep->working() && t.elapsed() < 300000)
            QApplication::processEvents();
        QApplication::processEvents();
    };
    auto settle = [](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
    };
    eventnet::EventNet *nw = nullptr;
    auto snap = [&](const QString &tag) {
        const scriptedit::ScriptEditor *ed = ws->current();
        const auto loc = ws->location();
        out << u"%1 | tab %2 | msg %3 | steps %4 | editor %5 %6 | sel %7"_s.arg(
            tag, win.tab, v(win.st_msg->text()), QString::number(d.undo_stack.size()), ed ? ed->rel : u"None"_s,
            loc ? u"%1@%2"_s.arg(loc->first).arg(loc->second) : u"None"_s, uid_of(nw->view->sel));
        for (QWidget *w : widgets_in(nw, nw))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
        const QStringList rows = scene_rows(nw->view);
        std::map<QChar, int> kinds;
        for (const QString &r : rows)
            ++kinds[r[0]];
        QStringList ks;
        for (const auto &[k, n] : kinds)
            ks << QString(k) + QString::number(n);
        out << u"%1 scene %2 %3"_s.arg(tag, ks.join(u' '), h(rows.join(u'\n').toUtf8()));
        if (qEnvironmentVariableIsSet("ROWS"))
            for (const QString &r : rows)
                out << tag + u"   "_s + r;
    };
    auto grab = [&](QWidget *w, const QString &name) {
        settle(400);
        w->grab().save(QDir(out_dir).filePath(name));
    };

    // --- the window
    win.set_tab(u"events"_s);
    QApplication::processEvents();
    wait();
    win._script_tool(u"network"_s);
    QApplication::processEvents();
    nw = win.net_win;
    nw->resize(W, H);
    settle(500);
    nw->view->fit();
    QApplication::processEvents();
    snap(u"opened"_s);
    grab(nw, u"net_open.png"_s);
    eventnet::Graph *g = nw->graph.get();
    const QSet<QString> all = {u"fires"_s, u"flag"_s, u"modifier"_s, u"var"_s};
    events::Entry *best = g->nodes[0];
    size_t best_n = g->around(best, all).size();
    for (events::Entry *e : g->nodes) {
        const size_t n = g->around(e, all).size();
        if (n > best_n) {
            best = e;
            best_n = n;
        }
    }
    out << u"best %1 %2"_s.arg(uid_of(best), QString::number(best_n));
    nw->view->pick(best, true);
    QApplication::processEvents();
    snap(u"picked"_s);
    grab(nw, u"net_picked.png"_s);
    for (const QString &k : {u"tag"_s, u"file"_s}) {
        (*nw->kind_box.get(k))->setChecked(true);
        QApplication::processEvents();
        snap(u"lines "_s + k);
    }
    grab(nw, u"net_all_lines.png"_s);
    nw->layout_box->setCurrentIndex(1);
    nw->_relayout();
    QApplication::processEvents();
    snap(u"by connection"_s);
    grab(nw, u"net_chain.png"_s);
    nw->only->setChecked(true);
    QApplication::processEvents();
    snap(u"only connected"_s);
    nw->only->setChecked(false);
    QApplication::processEvents();
    const events::Entry *q = g->nodes[std::min<size_t>(3, g->nodes.size() - 1)];
    nw->find->setText(u" %1 "_s.arg(py::upper(q->id && !q->id->isEmpty() ? *q->id : q->key)));
    nw->_find();
    QApplication::processEvents();
    snap(u"found"_s);
    nw->find->setText(u"zz_port_nothing_zz"_s);
    nw->_find();
    QApplication::processEvents();
    snap(u"not found"_s);
    settle(1200);
    snap(u"not found later"_s);
    QTreeWidgetItem *top = nw->tree->topLevelItem(0);
    if (top && top->childCount()) {
        nw->_tree_clicked(top->child(0), 0);
        QApplication::processEvents();
        snap(u"row clicked"_s);
    }
    nw->open_btn->click();
    QApplication::processEvents();
    snap(u"shown in scripts"_s);
    if (events::Entry *sel = nw->view->sel) {
        const QString rel = sel->rel.value_or(QString());
        OrderedMap<QString, QString> ch;
        ch.insert(rel, store.text(rel) + u"\n# checked by the port\n"_s);
        Q_EMIT ws->replaceRequested(ch, u"Port check: a line added"_s);
        QApplication::processEvents();
        wait();
        settle(300);
        snap(u"edited"_s);
    }
    (*nw->kind_box.get(u"fires"_s))->setChecked(false);
    QApplication::processEvents();
    snap(u"no calls"_s);
    nw->layout_box->setCurrentIndex(0);
    nw->_relayout();
    QApplication::processEvents();
    snap(u"by file"_s);
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
