// The Game rules tab (step A) against Python, the C++ side of tools/check_rules.py.
// Usage: med_rules_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
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
static const QStringList RELS = {u"common/defines.lua"_s,          u"common/bookmarks.txt"_s,
                                 u"common/static_modifiers.txt"_s, u"common/triggered_modifiers.txt"_s,
                                 u"common/crime.txt"_s,            u"interface/country_trade.gui"_s,
                                 u"common/on_actions.txt"_s,       u"common/nationalvalues.txt"_s,
                                 u"common/national_focus.txt"_s,   u"interface/province_interface.gui"_s};

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
    auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
    entryview::PagesWorkspace *ws = win.rules_ws;
    QStringList named;                             // the localisation keys the steps write

    auto common = [&](const QString &tag) {
        QStringList news, gone, texts, names;
        for (const auto &kv : store.docs)
            if (kv.first.startsWith(u"news/"_s))
                news << kv.first;
        std::sort(news.begin(), news.end());
        for (const QString &g : store.gone)
            gone << g;
        std::sort(gone.begin(), gone.end());
        for (const QString &r : RELS + news)
            texts << u"%1:%2"_s.arg(r.split(u'/').last(), h(store.text(r).toUtf8()));
        for (const QString &k : named)
            names << u"%1=%2"_s.arg(k, key(loc.text(k)));
        out << u"%1 | tab %2 | page %3 | msg %4 | %5 | steps %6 | gone %7 | %8 | names %9"_s.arg(
            tag, win.tab, ws->page, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
            gone.join(u','), texts.join(u' '), names.join(u';'));
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        if (ws->page == u"stockpile") {
            auto *pv = static_cast<stockpilepage::StockpilePage *>(*ws->views.get(u"stockpile"_s));
            const auto *g = pv->game;
            out << u"%1 SP want %2 step %3 | slider %4 %5 %6"_s.arg(tag, v(pv->want), QString::number(pv->step_i),
                                                                   v(g->max_value), v(g->pos), v(g->step));
        }
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

    // --- Game settings
    win.set_tab(u"rules"_s);
    QApplication::processEvents();
    snap(u"settings"_s);
    auto *gv = static_cast<entryview::GameSettingsView *>(ws->view(u"settings"_s));
    gv->search->setText(u"infamy"_s);
    QApplication::processEvents();
    snap(u"search"_s);
    if (QTreeWidgetItem *it = first_item(gv->lst)) {
        gv->lst->setCurrentItem(it);
        QApplication::processEvents();
    }
    snap(u"setting picked"_s);
    grab(u"rules_settings.png"_s);
    gv->value->setText(u"2.5"_s);
    click(gv->set_btn);
    snap(u"value set"_s);
    gv->value->setText(u"many"_s);
    click(gv->set_btn);
    snap(u"value a word"_s);
    gv->value->setText(u"1,2"_s);
    click(gv->set_btn);
    snap(u"value a comma"_s);
    gv->note_edit->setText(u"  checked   by the   port "_s);
    click(gv->note_btn);
    snap(u"note set"_s);
    gv->note_edit->setText(QString());
    click(gv->note_btn);
    snap(u"note off"_s);
    gv->search->setText(QString());
    QApplication::processEvents();
    snap(u"search cleared"_s);
    // --- Start dates
    ws->set_page(u"bookmarks"_s);
    QApplication::processEvents();
    auto *sv = static_cast<moreviews::StartDatesView *>(ws->view(u"bookmarks"_s));
    snap(u"bookmarks"_s);
    if (sv->lst->topLevelItemCount()) {
        sv->lst->setCurrentItem(sv->lst->topLevelItem(0));
        QApplication::processEvents();
    }
    snap(u"date picked"_s);
    grab(u"rules_dates.png"_s);
    auto ed = [sv](const QString &k) -> QLineEdit * {
        for (const auto &[kk, e] : sv->ed)
            if (kk == k)
                return e;
        return nullptr;
    };
    edit(ed(u"cameraX"_s), u"2100"_s);
    snap(u"camera typed"_s);
    edit(ed(u"cameraY"_s), u"down"_s);
    snap(u"camera a word"_s);
    edit(ed(u"date"_s), u"1836.13"_s);
    snap(u"date wrong"_s);
    {
        const auto ns = sv->nodes();
        if (sv->cur >= 0 && sv->cur < static_cast<long long>(ns.size()))
            for (const auto &c : ns[static_cast<size_t>(sv->cur)].kids)
                if (c.has_key && c.key == u"name" && !c.is_block) {
                    const QString nn = py::strip(c.value, u"\""_s);
                    if (!nn.isEmpty())
                        named << nn;
                    break;
                }
    }
    edit(ed(u"name"_s), u"Checked  start"_s);
    snap(u"name typed"_s);
    sv->new_date->setText(u"1850.1.1"_s);
    button(sv, [](const QString &t) { return t == u"＋ New start date"; });
    named << u"BM_1850_1_1_NAME"_s << u"BM_1850_1_1_DESC"_s;
    snap(u"new date"_s);
    sv->new_date->setText(u"1850.1.1"_s);
    button(sv, [](const QString &t) { return t == u"＋ New start date"; });
    snap(u"same date"_s);
    sv->new_date->setText(u"soon"_s);
    button(sv, [](const QString &t) { return t == u"＋ New start date"; });
    snap(u"no date"_s);
    click(sv->rm);
    snap(u"date removed"_s);
    // --- the entry pages
    for (const QString &page : {u"static"_s, u"triggered"_s, u"crime"_s, u"news"_s}) {
        ws->set_page(page);
        QApplication::processEvents();
        snap(page);
    }
    ws->set_page(u"crime"_s);
    QApplication::processEvents();
    auto *cv = static_cast<entryview::EntryView *>(ws->view(u"crime"_s));
    if (QTreeWidgetItem *it = first_item(cv->lst)) {
        cv->lst->setCurrentItem(it);
        QApplication::processEvents();
    }
    snap(u"crime picked"_s);
    grab(u"rules_crime.png"_s);
    // --- Stockpile slider
    ws->set_page(u"stockpile"_s);
    QApplication::processEvents();
    auto *pv = static_cast<stockpilepage::StockpilePage *>(ws->view(u"stockpile"_s));
    snap(u"stockpile"_s);
    grab(u"rules_stockpile.png"_s);
    button(pv, [](const QString &t) { return t == u"1 million"; });
    snap(u"preset"_s);
    click(pv->step_btn[3]);
    snap(u"step"_s);
    edit(pv->edit, u"250 000"_s);
    snap(u"typed"_s);
    edit(pv->edit, u"plenty"_s);
    snap(u"typed a word"_s);
    {
        const QRectF tr = pv->game->_track();
        QPointF p(tr.left() - 20, tr.center().y());
        send(pv->game, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(pv->game, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
        snap(u"arrow click"_s);
        p = QPointF(tr.left() + tr.width() * 0.3, tr.center().y());
        send(pv->game, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(pv->game, QEvent::MouseMove, QPointF(tr.left() + tr.width() * 0.6, tr.center().y()), Qt::NoButton,
             Qt::LeftButton);
        send(pv->game, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
        snap(u"dragged"_s);
    }
    grab(u"rules_stockpile_dragged.png"_s);
    click(pv->use_btn);
    snap(u"used"_s);
    // --- Pop types / Rebels (scriptpage: the Scripts page's editor borrowed)
    for (const QString &page : {u"poptypes"_s, u"rebels"_s}) {
        ws->set_page(page);
        QApplication::processEvents();
        snap(page);
        auto *sp = static_cast<scriptpage::ScriptListPage *>(ws->view(page));
        if (sp->lst->topLevelItemCount()) {
            sp->lst->setCurrentItem(sp->lst->topLevelItem(0));
            QApplication::processEvents();
        }
        snap(page + u" picked"_s);
        grab(u"rules_%1.png"_s.arg(page));
        if (sp->cur && !sp->name_rows.empty()) {
            const auto &nr = sp->name_rows[0];
            named << sp->cur->second + nr.sfx;
            edit(nr.ed, u"  Port   checked "_s);
            snap(page + u" named"_s);
        }
    }
    // --- Timed events
    ws->set_page(u"timed"_s);
    QApplication::processEvents();
    snap(u"timed"_s);
    auto *tv = static_cast<moreviews::TimedEventsView *>(ws->view(u"timed"_s));
    if (tv->lst->topLevelItemCount()) {
        tv->lst->setCurrentItem(tv->lst->topLevelItem(0));
        QApplication::processEvents();
    }
    snap(u"timed picked"_s);
    grab(u"rules_timed.png"_s);
    tv->eid->setText(u"soon"_s);
    button(tv, [](const QString &t) { return t == u"＋ Add"; });
    snap(u"timed a word"_s);
    tv->eid->setText(u" 424242 "_s);
    tv->weight->setText(u"50"_s);
    button(tv, [](const QString &t) { return t == u"＋ Add"; });
    snap(u"timed added"_s);
    if (tv->rows->topLevelItemCount()) {
        tv->rows->setCurrentItem(tv->rows->topLevelItem(0));
        QApplication::processEvents();
    }
    button(tv, [](const QString &t) { return t == u"✕ Take the selected off"; });
    snap(u"timed taken off"_s);
    // --- Flags & variables
    ws->set_page(u"index"_s);
    QApplication::processEvents();
    snap(u"index"_s);
    auto *xv = static_cast<moreviews::IndexView *>(ws->view(u"index"_s));
    grab(u"rules_index.png"_s);
    xv->search->setText(u"a"_s);
    QApplication::processEvents();
    snap(u"index search"_s);
    for (const auto &[box, i, tag] : {std::tuple{xv->kind, 2, u"index country flags"_s},
                                      std::tuple{xv->show_box, 1, u"index never set"_s},
                                      std::tuple{xv->show_box, 2, u"index never checked"_s},
                                      std::tuple{xv->show_box, 0, u"index all"_s},
                                      std::tuple{xv->kind, 0, u"index every kind"_s}}) {
        box->setCurrentIndex(i);
        Q_EMIT box->activated(i);
        QApplication::processEvents();
        snap(tag);
    }
    if (xv->lst->topLevelItemCount()) {
        xv->lst->setCurrentItem(xv->lst->topLevelItem(0));
        QApplication::processEvents();
    }
    snap(u"index picked"_s);
    if (QTreeWidgetItem *it = xv->lst->currentItem()) {
        Q_EMIT xv->lst->itemDoubleClicked(it, 0);
        QApplication::processEvents();
    }
    snap(u"index jumped"_s);
    win.set_tab(u"rules"_s);
    QApplication::processEvents();
    snap(u"index back"_s);
    // --- National values: the icon board
    ws->set_page(u"values"_s);
    QApplication::processEvents();
    snap(u"values"_s);
    auto *bw = static_cast<iconboard::BoardWithList *>(ws->view(u"values"_s));
    iconboard::BoardPage *bp = bw->board;
    QStringList keys = bp->_order();
    if (!keys.isEmpty()) {
        bp->_pick(keys[0]);
        QApplication::processEvents();
    }
    snap(u"nv picked"_s);
    grab(u"rules_values.png"_s);
    if (bp->board->_boxes.size() > 1) {                // a click on the second picture
        const QPointF c = std::get<0>(bp->board->_boxes[1]).center();
        send(bp->board, QEvent::MouseButtonPress, c, Qt::LeftButton, Qt::LeftButton);
        send(bp->board, QEvent::MouseButtonRelease, c, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    }
    snap(u"nv clicked"_s);
    if (bp->board->_boxes.size() > 2) {                // the first dragged in front of the third
        const QPointF a = std::get<0>(bp->board->_boxes[0]).center(), b = std::get<0>(bp->board->_boxes[2]).center();
        send(bp->board, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        send(bp->board, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
        send(bp->board, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    }
    snap(u"nv dragged"_s);
    if (bp->h_place && bp->h_place->count()) {
        bp->h_factor->setText(u"0,5"_s);
        bp->_hidden_add(*bp->sel);
        QApplication::processEvents();
    }
    snap(u"nv pop file"_s);
    button(bw, [](const QString &t) { return t == u"Use it as this value's picture"; });
    snap(u"nv picture put"_s);
    bp->_add(QString());
    QApplication::processEvents();
    named << *bp->sel;
    snap(u"nv added"_s);
    keys = bp->_order();
    if (keys.size() > 2) {
        bp->_pick(keys[1]);
        bp->_ctrl(keys[2]);
        QApplication::processEvents();
        snap(u"nv two picked"_s);
        bp->_delete_picked();
        QApplication::processEvents();
    }
    snap(u"nv two deleted"_s);
    bw->set_mode(1);
    QApplication::processEvents();
    snap(u"nv list"_s);
    bw->set_mode(0);
    QApplication::processEvents();
    snap(u"nv board"_s);
    // --- National focuses: the icon board and the game's window
    ws->set_page(u"focus"_s);
    QApplication::processEvents();
    snap(u"focus"_s);
    iconboard::BoardPage *fp = static_cast<iconboard::BoardWithList *>(ws->view(u"focus"_s))->board;
    keys = fp->_order();
    QStringList groups;
    for (const iconboard::Group &g : fp->rows())
        groups << g.key;
    if (!keys.isEmpty()) {
        fp->_pick(keys[0]);
        QApplication::processEvents();
    }
    snap(u"focus picked"_s);
    grab(u"rules_focus.png"_s);
    if (!groups.isEmpty()) {
        fp->_pick(u'@' + groups[0]);
        QApplication::processEvents();
        snap(u"focus row picked"_s);
        fp->_add(groups[0]);
        QApplication::processEvents();
        named << *fp->sel;
        snap(u"focus added"_s);
    }
    fp->_add_group();
    QApplication::processEvents();
    named << fp->sel->mid(1);
    snap(u"focus row added"_s);
    if (fp->_can_fit) {
        fp->_set_pv(u"fit"_s);
        QApplication::processEvents();
        snap(u"focus fit shown"_s);
        grab(u"rules_focus_fit.png"_s);
        fp->_fit_window();
        QApplication::processEvents();
    }
    snap(u"focus fitted"_s);
    if (!keys.isEmpty()) {
        fp->_step(keys[0], 1);
        QApplication::processEvents();
        snap(u"focus later"_s);
        fp->_pick(keys[0]);
        fp->key_edit->setText(u"port_checked_focus"_s);
        fp->_rekey(keys[0]);
        QApplication::processEvents();
    }
    snap(u"focus rekeyed"_s);
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
