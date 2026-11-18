// The Library tab against Python, the C++ side of tools/check_library.py.
// Usage: med_library_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
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
    if (d.metaType().id() == QMetaType::QStringList) {   // (Python keeps a tuple there: str(tuple))
        QStringList parts;
        for (const QString &s : d.toStringList())
            parts << v(s);
        return u"("_s + parts.join(u", "_s) + (parts.size() == 1 ? u",)"_s : u")"_s);
    }
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
    if (auto *l = qobject_cast<QLabel *>(w)) {
        const auto *el = qobject_cast<widgets::ElideLabel *>(w);   // (Python's ElideLabel.text(): the whole text)
        return u"L %1 | %2 | %3"_s.arg(v(el ? el->text() : l->text()), v(l->styleSheet()), v(l->toolTip()));
    }
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3 | %4%5"_s.arg(v(e->text()), v(e->placeholderText()), v(e->styleSheet()), v(e->toolTip()), en);
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i)
            items << u"%1/%2"_s.arg(c->itemText(i), dat(c->itemData(i)));
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

static QTreeWidgetItem *first_item(QTreeWidget *t)   // the first row with data (a child under a heading, or a top row)
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
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
    entryview::PagesWorkspace *ws = win.lib_ws;
    moreviews::AssetEdits *assets = d.maps.contains(u"assets"_s) ? &d.map<moreviews::AssetEdits>(u"assets"_s) : nullptr;

    auto common = [&](const QString &tag) {
        QStringList texts, waiting, keys;
        for (const QString &r : RELS)
            texts << u"%1:%2"_s.arg(r.split(u'/').last(), h(store.text(r).toUtf8()));
        if (assets)
            for (const auto &kv : assets->fresh)
                keys << kv.first;
        std::sort(keys.begin(), keys.end());
        for (const QString &rel : keys) {
            const moreviews::Asset &a = *assets->fresh.get(rel);
            if (a.how == u"pic")
                waiting << u"%1=pic %2x%3"_s.arg(rel).arg(a.pic->rgba.w).arg(a.pic->rgba.h);
            else if (a.how == u"img" || a.how == u"strip")
                waiting << u"%1=%2 %3x%4"_s.arg(rel, a.how).arg(a.img.width()).arg(a.img.height());
            else
                waiting << u"%1=%2 %3"_s.arg(rel, a.how, pypath::basename(a.path));
        }
        out << u"%1 | tab %2 | page %3 | msg %4 | %5 | steps %6 | %7 | assets %8"_s.arg(
            tag, win.tab, ws->page, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
            texts.join(u' '), waiting.join(u';'));
    };
    auto snap = [&](const QString &tag) {
        if (qEnvironmentVariableIsSet("STEPS"))
            std::fprintf(stderr, "step %s\n", qPrintable(tag));
        common(tag);
        for (QWidget *w : widgets_in(ws, ws))
            if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
    };
    auto send = [](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(kind, pos, pos, button, buttons, Qt::NoModifier);
        QApplication::sendEvent(w, &e);
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

    // --- Icon strips
    win.set_tab(u"library"_s);
    QApplication::processEvents();
    snap(u"strips"_s);
    auto *sp = static_cast<iconstrips::StripsPage *>(ws->view(u"strips"_s));
    if (QTreeWidgetItem *it = first_item(sp->lst)) {
        sp->lst->setCurrentItem(it);
        QApplication::processEvents();
    }
    snap(u"strip picked"_s);
    grab(u"lib_strips.png"_s);
    sp->_mark(1);
    QApplication::processEvents();
    snap(u"strip marked"_s);
    sp->count->setValue(2);
    QApplication::processEvents();
    click(sp->add_btn);
    snap(u"strip longer"_s);
    grab(u"lib_strips_longer.png"_s);
    // --- Picture boards
    ws->set_page(u"boards"_s);
    QApplication::processEvents();
    snap(u"boards"_s);
    auto *bp = static_cast<iconboard::AllBoardsPage *>(ws->view(u"boards"_s));
    if (bp->chip.size() > 1) {
        bp->set_use((bp->chip.begin() + 1)->first);
        QApplication::processEvents();
    }
    snap(u"board use"_s);
    if (!bp->entries.empty()) {
        bp->_pick(bp->entries[0].key);
        QApplication::processEvents();
    }
    snap(u"board picked"_s);
    grab(u"lib_board.png"_s);
    // --- Pictures
    ws->set_page(u"pictures"_s);
    QApplication::processEvents();
    snap(u"pictures"_s);
    auto *pp = static_cast<moreviews::PicturesPage *>(ws->view(u"pictures"_s));
    if (pp->lib->count()) {
        pp->lib->setCurrentRow(0);
        QApplication::processEvents();
    }
    snap(u"picture picked"_s);
    pp->fit_box->setChecked(true);
    QApplication::processEvents();
    pp->_add();
    QApplication::processEvents();
    snap(u"picture added"_s);
    grab(u"lib_pictures.png"_s);
    pp->only_unused->setChecked(true);
    QApplication::processEvents();
    snap(u"only unused"_s);
    // --- Music
    ws->set_page(u"music"_s);
    QApplication::processEvents();
    snap(u"music"_s);
    auto *mp = static_cast<moreviews::MusicPage *>(ws->view(u"music"_s));
    if (mp->lib->count()) {
        mp->lib->setCurrentRow(0);
        QApplication::processEvents();
    }
    mp->_add();
    QApplication::processEvents();
    snap(u"music added"_s);
    // --- the look pages
    for (const QString &page : {u"menu"_s, u"maplook"_s}) {
        ws->set_page(page);
        QApplication::processEvents();
        snap(page);
        grab(u"lib_%1.png"_s.arg(page));
    }
    // --- Zoom & map marks
    ws->set_page(u"mapmarks"_s);
    QApplication::processEvents();
    snap(u"mapmarks"_s);
    grab(u"lib_mapmarks.png"_s);
    auto *mk = static_cast<mapmarkspage::MapMarksPage *>(ws->view(u"mapmarks"_s));
    if (auto *sa = mk->findChild<QScrollArea *>(); sa && sa->widget()) {   // (C++ only: the whole page, for a look)
        QDir().mkpath(out_dir + u"/../extra"_s);
        sa->widget()->grab().save(out_dir + u"/../extra/mapmarks_full.png"_s);
        Q_EMIT mk->ruler->picked(650);                   // farther out, the political view: the paper map
        click(mk->pol_btn);
        sa->widget()->grab().save(out_dir + u"/../extra/mapmarks_pol650.png"_s);
        Q_EMIT mk->ruler->picked(120);                   // close: the names
        sa->widget()->grab().save(out_dir + u"/../extra/mapmarks_pol120.png"_s);
        Q_EMIT mk->ruler->picked(330);
        click(mk->ter_btn);
    }
    mk->size->setValue(3);
    QApplication::processEvents();
    snap(u"marks 1.5"_s);
    grab(u"lib_mapmarks_15.png"_s);
    click(mk->use_btn);
    snap(u"marks used"_s);
    for (const auto &kv : mk->h_edit)
        if (kv.second->isEnabled()) {
            kv.second->setValue(kv.second->value() + 100);
            QApplication::processEvents();
            break;
        }
    snap(u"height changed"_s);
    button(mk, [](const QString &t) { return t == u"Use these heights"; });
    snap(u"heights used"_s);
    mk->opt_max->setValue(800);
    QApplication::processEvents();
    button(mk, [](const QString &t) { return t == u"Use (interface/settings.gui)"; });
    snap(u"options end"_s);
    mk->size->setValue(1);
    QApplication::processEvents();
    click(mk->use_btn);
    snap(u"marks back"_s);
    // --- Almanacs (the last page of Warfare since 2026-11-17, the user's word)
    entryview::PagesWorkspace *wws = win.war_ws;
    wws->set_page(u"almanacs"_s);
    QApplication::processEvents();
    snap(u"almanacs"_s);
    auto *av = static_cast<moreviews::AlmanacPage *>(wws->view(u"almanacs"_s));
    if (av->pick->count() > 1) {
        av->pick->setCurrentIndex(1);
        Q_EMIT av->pick->activated(1);
        QApplication::processEvents();
    }
    snap(u"almanac picked"_s);
    grab(u"lib_almanac.png"_s);
    button(av, [](const QString &t) { return t == u"Write all the almanacs"; });
    snap(u"almanacs written"_s);
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
