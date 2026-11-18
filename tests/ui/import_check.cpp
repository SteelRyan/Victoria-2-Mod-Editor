// The Import tab against Python, the C++ side of tools/check_import.py.
// Usage: med_import_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/entryview.h"
#include "ui/iconboard.h"
#include "ui/importpanel.h"
#include <QTimer>
#include "ui/iconstrips.h"
#include "ui/mapmarkspage.h"
#include "map/moreviews_data.h"
#include "ui/mapview.h"
#include "ui/moreviews.h"
#include "ui/scriptpage.h"
#include "ui/stockpilepage.h"
#include "ui/theme.h"
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
static const QStringList RELS = {u"interface/mapicons.gui"_s, u"interface/mapicons.gfx"_s, u"map/default.map"_s,
                                 u"interface/settings.gui"_s, u"interface/general_gfx.gfx"_s, u"interface/core.gfx"_s};

[[maybe_unused]] static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }

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

static importpanel::ImportWorkspace *g_ws = nullptr;
static QString row_dat(QTreeWidget *w, QTreeWidgetItem *it, int i)
{
    if (g_ws && w == g_ws->lst && i == 0 && it->data(0, Qt::UserRole).isValid())
        if (const auto x = g_ws->item_of(it))
            return x->cat + u'/' + x->key;
    return dat(it->data(i, Qt::UserRole));
}

static void item_rows(QTreeWidget *w, QTreeWidgetItem *it, int depth, QStringList &rows)
{
    QStringList cols;
    for (int i = 0; i < w->columnCount(); ++i)
        cols << u"%1/%2/%3/%4"_s.arg(it->text(i), it->toolTip(i), it->foreground(i).color().name(),
                                     row_dat(w, it, i));
    rows << QString::number(depth) + u' ' + cols.join(u'\t') +
                u"\t%1 %2"_s.arg(static_cast<int>(it->checkState(0))).arg(it->isExpanded() ? 1 : 0);
    for (int c = 0; c < it->childCount(); ++c)
        item_rows(w, it->child(c), depth + 1, rows);
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
            items << u"%1/%2"_s.arg(c->itemText(i), dat(c->itemData(i)));
        for (const QString &sd : {u"python"_s, u"cpp"_s})   // (each side's own copy)
            items.replaceInStrings(uR"(build\import\)"_s + sd + u'\\', uR"(build\import\<side>\)"_s);
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
        for (int r = 0; r < t->topLevelItemCount(); ++r)
            item_rows(t, t->topLevelItem(r), 0, rows);
        QTreeWidgetItem *cur = t->currentItem();
        return u"T %1 %2 %3 %4 %5"_s.arg(QString::number(t->columnCount()), QString::number(t->topLevelItemCount()),
                                         cur ? v(cur->text(0)) : u"None"_s, h(heads.join(u'\n').toUtf8()),
                                         qEnvironmentVariableIsSet("ROWS") ? rows.join(u'\n') : h(rows.join(u'\n').toUtf8()));
    }
    if (auto *s = qobject_cast<QSplitter *>(w)) {
        QStringList sz;
        for (int x : s->sizes())
            sz << QString::number(x);
        return u"S "_s + sz.join(u',');
    }
    return std::nullopt;
}

[[maybe_unused]] static QTreeWidgetItem *first_item(QTreeWidget *t)   // the first row with data (a child under a heading, or a top row)
{
    for (int r = 0; r < t->topLevelItemCount(); ++r) {
        QTreeWidgetItem *it = t->topLevelItem(r);
        if (it->data(0, Qt::UserRole).isValid())
            return it;
        for (int c = 0; c < it->childCount(); ++c)
            if (it->child(c)->data(0, Qt::UserRole).isValid())
                return it->child(c);
    }
    return nullptr;
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
    importpanel::ImportWorkspace *ws = win.imp_ws;
    g_ws = ws;

    auto pairs = [](std::vector<importer::Id> ids) {
        std::sort(ids.begin(), ids.end());
        QStringList o;
        for (const auto &[a, b] : ids)
            o << a + u'/' + b;
        return o.join(u',');
    };
    auto common = [&](const QString &tag) {
        const QString ticks = pairs(ws->picked.items());
        const QString plan = !ws->plan ? u"None"_s
                                       : u"%1 %2 %3 %4"_s.arg(ws->plan->items.size()).arg(ws->plan->needed.size())
                                             .arg(ws->plan->loc.size()).arg(ws->plan->warnings.size());
        out << u"%1 | tab %2 | mode %3 | cat %4 | msg %5 | %6 | steps %7 | ticks %8 %9 | out %10 | also %11 | plan %12"_s.arg(
            tag, win.tab, ws->mode, ws->cat, v(win.st_msg->text()), win.st_pending->text(),
            QString::number(d.undo_stack.size()), QString::number(ws->picked.size()), h(ticks.toUtf8()),
            pairs({ws->left_out.begin(), ws->left_out.end()}), pairs({ws->also.begin(), ws->also.end()}), plan);
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        for (QWidget *w : widgets_in(ws, ws))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto send = [](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(kind, pos, pos, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(w, &e);
    };
    auto settle = [&] {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 500 || ws->_count_timer->isActive() || ws->_fill_timer->isActive() ||
               ws->_plan_timer->isActive())
            QApplication::processEvents();
    };
    auto grab = [&](const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        ws->grab().save(QDir(out_dir).filePath(name));
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
    auto click = [](QPushButton *b) {
        b->click();
        QApplication::processEvents();
    };

    // --- before a mod is picked
    win.set_tab(u"import"_s);
    QApplication::processEvents();
    snap(u"import"_s);
    // --- CoRGI opened by its folder
    const QString CORGI = pypath::join(BASE, {u"mod"_s, u"CoRGI_puir 18.1"_s});
    ws->path->setText(u"\"%1\""_s.arg(CORGI));
    Q_EMIT ws->path->returnPressed();
    settle();
    snap(u"opened"_s);
    grab(u"imp_opened.png"_s);
    auto checkable = [&](QTreeWidget *t, int n) {
        QList<QTreeWidgetItem *> rows;
        for (int r = 0; r < t->topLevelItemCount(); ++r) {
            QTreeWidgetItem *top = t->topLevelItem(r);
            QList<QTreeWidgetItem *> all{top};
            for (int c = 0; c < top->childCount(); ++c)
                all << top->child(c);
            for (QTreeWidgetItem *it : all)
                if (it->data(0, Qt::UserRole).isValid() && (it->flags() & Qt::ItemIsUserCheckable))
                    rows << it;
        }
        return rows.mid(0, n);
    };
    for (QTreeWidgetItem *it : checkable(ws->lst, 2))
        it->setCheckState(0, Qt::Checked);
    settle();
    snap(u"events ticked"_s);
    grab(u"imp_ticked.png"_s);
    ws->chain_btn->click();
    settle();
    snap(u"chains"_s);
    {
        const auto rows = checkable(ws->lst, 1);
        if (!rows.isEmpty()) {
            ws->lst->setCurrentItem(rows[0]);
            QApplication::processEvents();
        }
    }
    snap(u"preview"_s);
    grab(u"imp_preview.png"_s);
    ws->show_box->setCurrentIndex(2);
    Q_EMIT ws->show_box->activated(2);
    settle();
    snap(u"show all"_s);
    ws->search->setText(u"a"_s);
    settle();
    snap(u"search"_s);
    ws->search->setText(QString());
    settle();
    // --- nations
    for (int r = 0; r < ws->cats->topLevelItemCount(); ++r)
        if (ws->cats->topLevelItem(r)->data(0, Qt::UserRole).toString() == u"nations")
            ws->cats->setCurrentItem(ws->cats->topLevelItem(r));
    settle();
    snap(u"nations"_s);
    for (QTreeWidgetItem *it : checkable(ws->lst, 1))
        it->setCheckState(0, Qt::Checked);
    settle();
    snap(u"nation ticked"_s);
    // --- a needed thing left out, the event-id choice
    {
        bool done = false;
        for (int r = 0; r < ws->plan_tree->topLevelItemCount(); ++r) {
            QTreeWidgetItem *top = ws->plan_tree->topLevelItem(r);
            for (int c = 0; c < top->childCount(); ++c) {
                QTreeWidgetItem *it = top->child(c);
                if (!done && it->data(0, Qt::UserRole).isValid() && it->data(0, Qt::UserRole + 1).toBool()) {
                    it->setCheckState(0, Qt::Unchecked);
                    done = true;
                }
            }
        }
    }
    settle();
    snap(u"left out"_s);
    ws->ev_mode->setCurrentIndex(2);
    Q_EMIT ws->ev_mode->activated(2);
    settle();
    snap(u"skip doubled ids"_s);
    // --- Import
    ws->go->click();
    settle();
    snap(u"imported"_s);
    grab(u"imp_imported.png"_s);
    // --- the map mode and back
    ws->set_mode(u"map"_s);
    settle();
    snap(u"map mode"_s);
    ws->set_mode(u"things"_s);
    settle();
    snap(u"things again"_s);
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
    QApplication::setCursorFlashTime(0);           // (the caret always drawn: a blink is no difference)
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder both sides use)
    moreviews::AlmanacPage::now_override = u"2026-11-06 12:00"_s;   // (Python: datetime.now() fixed)
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
