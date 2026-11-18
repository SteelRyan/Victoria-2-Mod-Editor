// The Scripts tab against Python, the C++ side of tools/check_scripts.py.
// Usage: med_scripts_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/checkpanel.h"
#include "ui/eventpanel.h"
#include "ui/mapview.h"
#include "ui/scriptedit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QBrush>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
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

static constexpr int SIDE_W = 800;

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
    if (d.metaType().id() == QMetaType::QBrush)
        return u"brush:"_s + qvariant_cast<QBrush>(d).color().name();
    if (d.metaType().id() == QMetaType::QStringList) {
        QStringList l = d.toStringList();
        if (l.size() == 5) {                       // an event panel row: [kind, key, uid, group, "1" with a group]
            const QString grp = l[4] == u"1" ? l[3] : u"None"_s;
            return QStringList{l[0], l[1], l[2].isEmpty() ? u"None"_s : l[2], grp}.join(u'|');
        }
        return l.join(u'|');
    }
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

static void item_rows(QTreeWidget *w, QTreeWidgetItem *it, int depth, QStringList &rows)
{
    QStringList cols;
    for (int i = 0; i < w->columnCount(); ++i)
        cols << u"%1/%2/%3/%4"_s.arg(it->text(i), it->toolTip(i), it->foreground(i).color().name(),
                                     dat(it->data(i, Qt::UserRole)));
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
            items << u"%1/%2"_s.arg(c->itemText(i), data_str(c->itemData(i)));
        return u"C %1 %2 %3 %4%5"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                        v(c->currentText()), h(items.join(u'\n').toUtf8()), en);
    }
    if (auto *p = qobject_cast<QPlainTextEdit *>(w))
        return u"P %1"_s.arg(h(p->toPlainText().toUtf8()));
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i)
            items << u"%1/%2/%3"_s.arg(l->item(i)->text(), data_str(l->item(i)->data(Qt::UserRole)),
                                       l->item(i)->foreground().color().name());
        return u"Q %1 %2 %3"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                   h(items.join(u'\n').toUtf8()));
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
    win.resize(1900, 1000);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    QApplication::processEvents();
    win._check_timer->stop();
    QObject::disconnect(win._check_timer, &QTimer::timeout, nullptr, nullptr);   // (the Errors scan runs where the steps say)
    auto checks = [&] {
        win.chk_panel->_sig.reset();
        win._checks_due();
        QApplication::processEvents();
    };
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
    auto map_hash = [&]() -> QString {
        const mapview::ColorTable t = win.color_table();
        if (t.layer)
            return u"view"_s;
        const long long n = win.mapdata->n;
        const QString shape = t.Q ? u"%1x%2x3"_s.arg(n).arg(t.Q) : u"%1x3"_s.arg(n);
        return shape + u' ' + h(QByteArray(reinterpret_cast<const char *>(t.rgb.data()), qsizetype(t.rgb.size())));
    };
    auto common = [&](const QString &tag) {
        QStringList dirty = store.dirty();
        std::sort(dirty.begin(), dirty.end());
        QStringList texts;
        for (const QString &r : dirty)
            texts << u"%1:%2"_s.arg(r, h(store.text(r).toUtf8()));
        const events::Entry *sel = ep->sel;
        const scriptedit::ScriptEditor *ed = ws->current();
        const auto loc = ws->location();
        out << (u"%1 | tab %2 | msg %3 | %4 | steps %5 | kind %6 | sel %7 | nation %8 | map %9 "_s.arg(
                    tag, win.tab, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
                    ep->_kind(), sel ? key(sel->uid) : u"None"_s, key(ep->nation), win._map_where) +
                u"%1 %2 | editor %3 %4 %5 | %6 | fill %7"_s.arg(
                    v(win.map->isVisible()), ws->map_mode, QString::number(ws->tabs->count()),
                    ed ? ed->rel : u"None"_s, loc ? u"%1@%2"_s.arg(loc->first).arg(loc->second) : u"None"_s,
                    texts.join(u' '), map_hash()));
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        for (const auto &[name, root_w] : {std::pair<QString, QWidget *>{u"P"_s, ep}, {u"W"_s, ws}})
            for (QWidget *w : widgets_in(root_w, root_w))
                if (const auto dd = desc(w))
                    out << tag + u' ' + name + u' ' + *dd;
    };
    auto grab = [&](QWidget *w, const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)
            QApplication::processEvents();
        w->grab().save(QDir(out_dir).filePath(name));
    };
    auto pick_row = [&](const events::Entry *e) {
        std::function<QTreeWidgetItem *(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) -> QTreeWidgetItem * {
            const QStringList l = it->data(0, Qt::UserRole).toStringList();
            if (l.size() == 5 && l[0] == u"e" &&
                it->data(0, Qt::UserRole + 3).value<quintptr>() == reinterpret_cast<quintptr>(e))
                return it;
            for (int c = 0; c < it->childCount(); ++c)
                if (QTreeWidgetItem *x = walk(it->child(c)))
                    return x;
            return nullptr;
        };
        for (int r = 0; r < ep->lst->topLevelItemCount(); ++r)
            if (QTreeWidgetItem *it = walk(ep->lst->topLevelItem(r))) {
                ep->lst->setCurrentItem(it);
                QApplication::processEvents();
                return true;
            }
        return false;
    };

    // --- the tab opened
    win.set_tab(u"events"_s);
    QApplication::processEvents();
    wait();
    checks();
    snap(u"events"_s);
    grab(ep, u"scripts_list.png"_s);
    events::Entry *e = nullptr;
    for (const auto &x : ep->db->entries)
        if (x->is_event() && !x->province()) {
            e = x.get();
            break;
        }
    if (!e)
        return 4;
    ep->select(e);
    QApplication::processEvents();
    wait();
    snap(u"event picked"_s);
    grab(ep, u"scripts_entry.png"_s);
    std::vector<long long> pids;
    for (const auto &[p, pr] : m->provinces)
        pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    long long pid = -1;
    for (long long p : pids) {
        const std::optional<QString> o = d.prov_state(p).owner;
        if (!m->provinces.get(p)->sea && o && !o->isEmpty()) {
            pid = p;
            break;
        }
    }
    win.on_click(pid, Qt::NoModifier);
    QApplication::processEvents();
    wait();
    snap(u"nation clicked"_s);
    ep->only_nation->setChecked(true);
    QApplication::processEvents();
    snap(u"only the nation"_s);
    ep->only_nation->setChecked(false);
    QApplication::processEvents();
    ep->search->setText(u"war"_s);
    QApplication::processEvents();
    snap(u"search"_s);
    ep->search->setText(QString());
    QApplication::processEvents();
    win.on_right_click(pid, Qt::NoModifier);
    QApplication::processEvents();
    snap(u"right click"_s);
    win.on_right_click(pid, Qt::ControlModifier);
    QApplication::processEvents();
    snap(u"ctrl right click"_s);
    win.on_click(pid, Qt::NoModifier);            // (the same nation again: back to any nation)
    QApplication::processEvents();
    wait();
    snap(u"nation off"_s);
    // --- categories
    ep->_set_group(u"cat"_s);
    QApplication::processEvents();
    snap(u"by category"_s);
    ep->_set_group(u"file"_s);
    QApplication::processEvents();
    ep->_new_cat();
    QApplication::processEvents();
    ep->new_edit->setText(u"Port check"_s);
    ep->_new_done();
    QApplication::processEvents();
    snap(u"new category"_s);
    out << u"row "_s + v(pick_row(e));
    ep->toggle_cat(u"Port check"_s);
    QApplication::processEvents();
    snap(u"category on"_s);
    if (ep->lit_box->isVisible()) {
        ep->lit_box->setChecked(!ep->lit_box->isChecked());
        QApplication::processEvents();
    }
    snap(u"literacy line"_s);
    // --- the other lists
    for (const QString &k : {u"decision"_s, u"both"_s, u"mods"_s, u"cbs"_s, u"event"_s}) {
        (*ep->kind_btn.get(k))->click();
        QApplication::processEvents();
        snap(u"kind "_s + k);
    }
    // --- the map: the small window, then docked
    ws->show_map();
    QApplication::processEvents();
    snap(u"map shown"_s);
    ws->_dock_map();
    QApplication::processEvents();
    snap(u"map docked"_s);
    // --- the editor
    const QString rel = e->rel.value_or(QString());
    ws->open(rel, e->span.first);
    QApplication::processEvents();
    snap(u"file opened"_s);
    grab(ws, u"scripts_editor.png"_s);
    {
        OrderedMap<QString, QString> ch;
        ch.insert(rel, store.text(rel) + u"\n# checked by the port\n"_s);
        Q_EMIT ws->replaceRequested(ch, u"Port check: a line added"_s);
    }
    QApplication::processEvents();
    checks();
    snap(u"replaced"_s);
    win.on_event_province(pid);
    QApplication::processEvents();
    snap(u"province link"_s);
    // --- the main window's undo
    for (int i = 0; i < 2; ++i) {
        out << u"undo "_s + v(win.undo());
        QApplication::processEvents();
    }
    checks();
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
