// The Warfare tab against Python, the C++ side of tools/check_warfare.py.
// Usage: med_warfare_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "map/mapview_data.h"
#include "ui/entryview.h"
#include "ui/mapview.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/unitpanel.h"
#include "ui/unittimeline.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }

static QString data_str(const QVariant &d)
{
    if (!d.isValid())
        return u"None"_s;
    return d.toString();
}

static QString dat(const QVariant &d)
{
    if (!d.isValid())
        return u"None"_s;
    if (d.metaType().id() == QMetaType::Double)
        return v(d.toDouble());
    if (d.metaType().id() == QMetaType::QStringList)
        return d.toStringList().join(u'|');
    return d.toString();
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
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *c = qobject_cast<QRadioButton *>(w))
        return u"R %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4 | %5"_s.arg(v(btext(b)), v(b->isChecked()), en, v(b->toolTip()), v(b->styleSheet()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3 | %4%5"_s.arg(v(e->text()), v(e->placeholderText()), v(e->styleSheet()), v(e->toolTip()), en);
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i)
            items << u"%1/%2"_s.arg(c->itemText(i), data_str(c->itemData(i)));
        return u"C %1 %2 %3 %4%5"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                        v(c->currentText()), h(items.join(u'\n').toUtf8()), en);
    }
    if (auto *p = qobject_cast<QPlainTextEdit *>(w))
        return u"P %1 %2"_s.arg(h(p->toPlainText().toUtf8())).arg(p->height());
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i)
            items << u"%1/%2/%3"_s.arg(l->item(i)->text(), data_str(l->item(i)->data(Qt::UserRole)),
                                       l->item(i)->foreground().color().name());
        return u"Q %1 %2 %3 %4"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                      h(items.join(u'\n').toUtf8()), QString::number(l->height()));
    }
    if (auto *t = qobject_cast<QTreeWidget *>(w)) {
        QTreeWidgetItem *hi = t->headerItem();
        QStringList heads, rows;
        for (int i = 0; i < t->columnCount(); ++i)
            heads << u"%1/%2/%3"_s.arg(hi->text(i), hi->toolTip(i), hi->foreground(i).color().name());
        for (int r = 0; r < t->topLevelItemCount(); ++r) {
            QTreeWidgetItem *it = t->topLevelItem(r);
            QStringList cols;
            for (int i = 0; i < t->columnCount(); ++i)
                cols << u"%1/%2/%3/%4"_s.arg(it->text(i), it->toolTip(i), it->foreground(i).color().name(),
                                             dat(it->data(i, Qt::UserRole)));
            rows << cols.join(u'\t') + u'\t' + QString::number(static_cast<int>(it->checkState(0)));
        }
        QTreeWidgetItem *cur = t->currentItem();
        return u"T %1 %2 %3 %4 %5"_s.arg(QString::number(t->columnCount()), QString::number(t->topLevelItemCount()),
                                         QString::number(cur ? t->indexOfTopLevelItem(cur) : -1),
                                         h(heads.join(u'\n').toUtf8()), h(rows.join(u'\n').toUtf8()));
    }
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
    document::Document &d = *win.doc;
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
    auto &techs = d.map<techdata::TechEdits>(u"techs"_s);
    entryview::PagesWorkspace *ws = win.war_ws;

    auto common = [&](const QString &tag) {
        QStringList rels, gone, texts, tt;
        for (const auto &kv : store.docs)
            if (kv.first.startsWith(u"units/"_s) || kv.first.startsWith(u"history/wars/"_s))
                rels << kv.first;
        std::sort(rels.begin(), rels.end());
        for (const QString &g : store.gone)
            gone << g;
        std::sort(gone.begin(), gone.end());
        for (const QString &r : rels)
            texts << u"%1=%2"_s.arg(r, store.text(r));
        for (const QString &k : techs.keys() + techs.inventions())
            tt << u"%1=%2"_s.arg(k, techs.text(k));
        out << u"%1 | tab %2 | page %3 | msg %4 | %5 | steps %6 | files %7 | gone %8 | texts %9"_s.arg(
                   tag, win.tab, ws->page, v(win.st_msg->text()), win.st_pending->text(),
                   QString::number(d.undo_stack.size()), rels.join(u','), gone.join(u','), h(texts.join(u'\n').toUtf8())) +
                   u" | techs "_s + h(tt.join(u'\n').toUtf8());
    };
    auto timeline = [&](const QString &tag, unittimeline::UnitTimelineView *tv) {
        unittimeline::Lanes *ln = tv->lanes, *ax = tv->axis;
        unittimeline::Graph *gr = tv->graph;
        QStringList rows, mks, cols, chs, labs;
        for (const auto &r : ln->units)
            rows << r.key;
        for (const auto &[k, mk] : ln->marks) {
            QStringList ys;
            for (const auto &[y, l] : mk)
                ys << u"%1=%2"_s.arg(y).arg(l.size());
            mks << k + u':' + ys.join(u',');
        }
        QStringList ck = ln->colours.keys();
        std::sort(ck.begin(), ck.end());
        for (const QString &k : ck)
            cols << u"%1=%2"_s.arg(k, ln->colours.value(k).isEmpty() ? u"None"_s : ln->colours.value(k));
        for (const auto &c : tv->changes) {
            QStringList ip;
            for (qsizetype x : c.ipath)
                ip << QString::number(x);
            chs << u"%1 %2 %3 %4 %5 %6 %7 %8"_s.arg(QString::number(c.year), c.src, c.kind, c.target, c.stat, v(c.value),
                                                   ip.join(u','), key(c.after));
        }
        for (const auto &[a, b] : gr->labels)
            labs << u"%1=%2"_s.arg(a).arg(b);
        out << u"%1 TL unit %2 year %3 naval %4 | y %5-%6 end %7 | ticked %8 | stats %9"_s.arg(
                   tag, key(tv->unit), tv->year ? QString::number(*tv->year) : u"None"_s, v(tv->naval),
                   QString::number(tv->y0), QString::number(tv->y1),
                   tv->end_year ? QString::number(*tv->end_year) : u"None"_s, tv->ticked.join(u','),
                   tv->stats[tv->naval].join(u',')) +
                   u" | rows %1 | marks %2"_s.arg(rows.join(u','), h(mks.join(u';').toUtf8()));
        out << u"%1 TL axis %2-%3 | colours %4 | changes %5 %6"_s.arg(tag, v(ax->y0), v(ax->y1), cols.join(u','),
                                                                     QString::number(tv->changes.size()),
                                                                     h(chs.join(u';').toUtf8()));
        out << u"%1 TG v %2-%3 y %4-%5 year %6 | note %7 | labels %8"_s.arg(
            tag, v(gr->v0), v(gr->v1), QString::number(gr->y0), QString::number(gr->y1), QString::number(gr->year),
            v(gr->note), labs.join(u','));
        for (const auto &[stat, lines] : gr->panels)
            for (const auto &l : lines) {
                QStringList pts;
                for (const auto &[y, x] : l.pts)
                    pts << u"%1,%2"_s.arg(v(y), v(x));
                out << u"%1 TG %2 | %3 %4 | %5"_s.arg(tag, stat, l.name, l.col, pts.join(u';'));
            }
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        if (ws->page == u"timeline")
            timeline(tag, static_cast<unittimeline::UnitTimelineView *>(*ws->views.get(u"timeline"_s)));
        for (QWidget *w : widgets_in(ws, ws))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto send = [](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(kind, pos, pos, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(w, &e);
    };
    auto click_at = [&](QWidget *w, const QPointF &p) {
        send(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        send(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    };
    auto settle = [] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 200)
            QApplication::processEvents();
    };
    auto grab = [&](const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        ws->grab().save(QDir(out_dir).filePath(name));
    };
    auto found_le = [](QWidget *root, const std::function<bool(QLineEdit *)> &pred) {
        QList<QLineEdit *> res;
        for (QWidget *w : widgets_in(root, root))
            if (auto *x = qobject_cast<QLineEdit *>(w))
                if (!pred || pred(x))
                    res << x;
        return res;
    };
    auto button = [](QWidget *root, const std::function<bool(const QString &)> &f, int i = 0) {
        QList<QPushButton *> bs;
        for (QWidget *w : widgets_in(root, root))
            if (auto *b = qobject_cast<QPushButton *>(w))
                if (f(btext(b)))
                    bs << b;
        if (bs.size() > i) {
            bs[i]->click();
            QApplication::processEvents();
        }
        return bs.size() > i;
    };
    auto edit = [](QLineEdit *w, const QString &text) {
        w->setText(text);
        Q_EMIT w->editingFinished();
        QApplication::processEvents();
    };

    // --- Unit stats
    win.set_tab(u"warfare"_s);
    QApplication::processEvents();
    snap(u"units"_s);
    auto *uv = static_cast<unitpanel::UnitStatsView *>(ws->view(u"units"_s));
    QStringList ks;
    for (const auto &kv : uv->units)
        ks << kv.first;
    std::sort(ks.begin(), ks.end());
    const QString unit = uv->units.contains(u"infantry"_s) ? u"infantry"_s : ks.value(0);
    uv->show_unit(unit);
    QApplication::processEvents();
    snap(u"unit shown"_s);
    grab(u"war_units.png"_s);
    if (const auto es = found_le(uv->detail, [uv](QLineEdit *e) { return e != uv->name_edit && e->placeholderText() != u"amount"; });
        !es.isEmpty())
        edit(es[0], u"4"_s);
    snap(u"stat typed"_s);
    if (const auto es = found_le(uv->detail, [](QLineEdit *e) { return e->placeholderText() == u"amount"; }); !es.isEmpty()) {
        es[0]->setText(u"2"_s);
        button(uv->detail, [](const QString &t) { return t == u"＋"; });
    }
    snap(u"good added"_s);
    uv->new_name->setText(u"Check Guard"_s);
    uv->new_btn->click();
    QApplication::processEvents();
    snap(u"new unit"_s);
    for (int i = 0; i < 2; ++i) {
        uv->del_btn->click();
        QApplication::processEvents();
        snap(u"delete %1"_s.arg(i));
    }
    // --- Unit timeline
    ws->set_page(u"timeline"_s);
    QApplication::processEvents();
    auto *tv = static_cast<unittimeline::UnitTimelineView *>(ws->view(u"timeline"_s));
    snap(u"timeline"_s);
    grab(u"war_lanes.png"_s);
    {
        const QStringList shown = tv->_shown_units();
        const std::optional<QString> tu = shown.contains(u"infantry"_s) ? std::optional<QString>(u"infantry"_s)
                                          : !shown.isEmpty()            ? std::optional<QString>(shown[0])
                                                                        : std::nullopt;
        tv->pick(tu, tv->y0 + 30);
        QApplication::processEvents();
    }
    snap(u"picked"_s);
    for (int r = 0; r < std::min(2, tv->tot->topLevelItemCount()); ++r) {
        tv->tot->topLevelItem(r)->setCheckState(0, Qt::Checked);
        settle();
    }
    snap(u"ticked"_s);
    tv->jump(1);
    snap(u"next change"_s);
    tv->jump(-1);
    snap(u"change before"_s);
    settle();
    if (const auto es = found_le(tv->rows_host, {}); !es.isEmpty())
        edit(es[0], u"3"_s);
    snap(u"change typed"_s);
    tv->add_tech->setCurrentIndex(std::min(3, tv->add_tech->count() - 1));
    tv->add_stat->setCurrentIndex(std::min(1, tv->add_stat->count() - 1));
    tv->add_val->setText(u"1.5"_s);
    tv->add_change();
    QApplication::processEvents();
    snap(u"change added"_s);
    tv->add_val->setText(u"x"_s);
    tv->add_change();
    QApplication::processEvents();
    snap(u"change wrong"_s);
    button(tv->rows_host, [](const QString &t) { return t == u"✕"; });
    snap(u"change out"_s);
    tv->set_view(u"graph"_s);
    QApplication::processEvents();
    snap(u"graph"_s);
    grab(u"war_graph.png"_s);
    snap(u"graph drawn"_s);
    if (QPushButton *const *b = tv->stat_btn.get(u"discipline"_s)) {
        (*b)->click();
        QApplication::processEvents();
    }
    snap(u"stat button"_s);
    tv->graph->zoom(1.5);
    QApplication::processEvents();
    snap(u"zoomed"_s);
    tv->_slide(-0.4);
    snap(u"slid"_s);
    tv->graph->fit();
    QApplication::processEvents();
    snap(u"whole time"_s);
    tv->set_view(u"lanes"_s);
    tv->only_box->click();
    QApplication::processEvents();
    snap(u"only ticked"_s);
    tv->only_box->click();
    tv->set_naval(true);
    QApplication::processEvents();
    snap(u"naval"_s);
    tv->set_naval(false);
    QApplication::processEvents();
    click_at(tv->lanes, QPointF(210 + 120, 34 * 1.5));
    snap(u"lane click"_s);
    // --- the entry pages
    for (const QString &page : {u"traits"_s, u"terrain"_s, u"colours"_s}) {
        ws->set_page(page);
        QApplication::processEvents();
        snap(page);
    }
    // --- Wars at the start
    ws->set_page(u"wars"_s);
    QApplication::processEvents();
    auto *wv = static_cast<moreviews::WarsView *>(ws->view(u"wars"_s));
    snap(u"wars"_s);
    if (wv->lst->topLevelItemCount()) {
        wv->lst->setCurrentItem(wv->lst->topLevelItem(0));
        QApplication::processEvents();
    }
    snap(u"war picked"_s);
    grab(u"war_wars.png"_s);
    QListWidget *att_lw = wv->side_box[0].second.first;
    QComboBox *att_cb = wv->side_box[0].second.second;
    att_cb->setCurrentIndex(std::min(5, att_cb->count() - 1));
    button(wv, [](const QString &t) { return t == u"＋ Add"; });
    snap(u"attacker added"_s);
    att_lw->setCurrentRow(0);
    button(wv, [](const QString &t) { return t == u"✕ Take off"; });
    snap(u"attacker off"_s);
    wv->cb_box->setCurrentIndex(std::min(1, wv->cb_box->count() - 1));
    wv->actor->setCurrentIndex(std::min(3, wv->actor->count() - 1));
    wv->receiver->setCurrentIndex(std::min(4, wv->receiver->count() - 1));
    button(wv, [](const QString &t) { return t == u"＋"; });
    snap(u"goal added"_s);
    wv->goals->setCurrentRow(0);
    button(wv, [](const QString &t) { return t == u"✕"; });
    snap(u"goal off"_s);
    wv->save_btn->click();
    QApplication::processEvents();
    snap(u"saved"_s);
    wv->new_name->setText(QString());
    button(wv, [](const QString &t) { return t == u"＋ New war"; });
    snap(u"new war empty"_s);
    wv->new_name->setText(u"Check Border War"_s);
    button(wv, [](const QString &t) { return t == u"＋ New war"; });
    snap(u"new war"_s);
    wv->raw->setPlainText(wv->raw->toPlainText() + u"# checked\n"_s);
    button(wv, [](const QString &t) { return t == u"Save the text"; });
    snap(u"text saved"_s);
    wv->raw->setPlainText(u"a = {"_s);
    button(wv, [](const QString &t) { return t == u"Save the text"; });
    snap(u"text wrong"_s);
    wv->rm_btn->click();
    QApplication::processEvents();
    snap(u"removed"_s);
    win.open_war(wv->lst->topLevelItemCount() ? wv->lst->topLevelItem(0)->data(0, Qt::UserRole).toString() : QString());
    QApplication::processEvents();
    snap(u"open war"_s);
    // --- the main window's undo
    for (int i = 0; i < 3; ++i) {
        out << u"undo "_s + v(win.undo());
        QApplication::processEvents();
    }
    snap(u"undone"_s);
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
