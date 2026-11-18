// Rules → Shortcuts against Python, the C++ side of tools/check_keyspage.py.
// Usage: med_keyspage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/pynum.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/shortcuts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using pyform::v;
using shortcuts::Ident;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;
static shortcuts::ShortcutsWorkspace *g_ws = nullptr;

static QString f3(double x) { return py::repr_float(x); }   // (repr(float(x)), as check_keyspage.py's rf)

static QString ident_s(const std::optional<Ident> &x)
{
    if (!x)
        return u"None"_s;
    return std::get<0>(*x) + u'|' + std::get<1>(*x).join(u'/') + u'|' + std::get<2>(*x) + u'|' +
           QString::number(std::get<3>(*x));
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

static void item_rows(QTreeWidget *w, QTreeWidgetItem *it, int depth, QStringList &rows)
{
    QStringList cols;
    for (int i = 0; i < w->columnCount(); ++i)
        cols << u"%1/%2/%3/%4/%5"_s.arg(it->text(i), it->toolTip(i), it->foreground(i).color().name(),
                                        i == 0 ? ident_s(g_ws->ident_of(it)) : u"None"_s,
                                        QString::number(it->font(i).bold() ? 1 : 0));
    rows << QString::number(depth) + u' ' + cols.join(u'\t') + u"\t%1"_s.arg(it->isExpanded() ? 1 : 0);
    for (int c = 0; c < it->childCount(); ++c)
        item_rows(w, it->child(c), depth + 1, rows);
}

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4 | %5"_s.arg(v(b->text()), v(b->isChecked()), en, v(b->toolTip()), v(b->styleSheet()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3%4"_s.arg(v(e->text()), v(e->placeholderText()), v(e->toolTip()), en);
    if (auto *t = qobject_cast<QTreeWidget *>(w)) {
        QTreeWidgetItem *hi = t->headerItem();
        QStringList heads, rows;
        for (int i = 0; i < t->columnCount(); ++i)
            heads << u"%1/%2"_s.arg(hi->text(i), hi->toolTip(i));
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

static QTreeWidgetItem *first_item(QTreeWidget *t)
{
    for (int r = 0; r < t->topLevelItemCount(); ++r) {
        QTreeWidgetItem *it = t->topLevelItem(r);
        if (g_ws->ident_of(it))
            return it;
        for (int c = 0; c < it->childCount(); ++c)
            if (g_ws->ident_of(it->child(c)))
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
    shortcuts::ShortcutsWorkspace *ws = win.keys_ws;
    g_ws = ws;
    shortcuts::WindowView *pv = ws->proj;

    auto common = [&](const QString &tag) {
        QStringList rels, texts;
        for (const auto &kv : store.docs)
            if (kv.first.startsWith(u"interface/"))
                rels << kv.first;
        std::sort(rels.begin(), rels.end());
        for (const QString &r : rels)
            texts << u"%1:%2"_s.arg(r.split(u'/').last(), h(store.text(r).toUtf8()));
        out << u"%1 | tab %2 | msg %3 | %4 | steps %5 | %6"_s.arg(tag, win.tab, v(win.st_msg->text()),
                                                                 win.st_pending->text(),
                                                                 QString::number(d.undo_stack.size()), texts.join(u' '));
        QStringList hits, keys, info;
        for (const shortcuts::Hit &x : pv->hits)
            hits << u"%1,%2,%3,%4,%5"_s.arg(f3(x.x), f3(x.y), f3(x.w), f3(x.h), ident_s(x.ident));
        std::vector<std::pair<QString, QString>> ks, is;
        for (const auto &[i, k] : pv->keys)
            if (!k.isEmpty())
                ks.emplace_back(ident_s(i), k);
        std::stable_sort(ks.begin(), ks.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[i, k] : ks)
            keys << i + u'=' + k;
        for (const auto &[i, cw] : pv->info)
            is.emplace_back(ident_s(i), cw.first + u'/' + cw.second);
        std::stable_sort(is.begin(), is.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[i, cw] : is)
            info << i + u'=' + cw;
        QStringList cls(pv->classes.begin(), pv->classes.end());
        std::sort(cls.begin(), cls.end());
        QString img = u"None"_s;
        if (pv->img)
            img = u"%1x%2 %3"_s.arg(pv->img->width()).arg(pv->img->height()).arg(
                h(QByteArray::fromRawData(reinterpret_cast<const char *>(pv->img->constBits()),
                                          qsizetype(pv->img->sizeInBytes()))));
        QString box = u"None"_s;
        if (pv->box)
            box = QStringList{f3(pv->box->x), f3(pv->box->y), f3(pv->box->w), f3(pv->box->h)}.join(u',');
        out << tag + u" | view zoom "_s + f3(pv->zoom) + u" off "_s + f3(pv->off.first) + u","_s + f3(pv->off.second) +
                   u" box "_s + box + u" | pick "_s + ident_s(pv->pick) + u" | classes "_s + cls.join(u',') +
                   u" | banner "_s + v(pv->banner) + u" | img "_s + img + u" | hits "_s +
                   QString::number(qsizetype(pv->hits.size())) + u' ' + h(hits.join(u';').toUtf8()) + u" | keys "_s +
                   h(keys.join(u';').toUtf8()) + u" | info "_s + h(info.join(u';').toUtf8()) + u" | ws pick "_s +
                   ident_s(ws->_pick) + u" mode "_s + ws->mode + u" cat "_s + ws->cat + u" only "_s + v(ws->only);
    };
    auto snap = [&](const QString &tag) {
        common(tag);
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
    auto mouse = [&](QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(kind, pos, pv->mapToGlobal(pos), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(pv, &e);
        QApplication::processEvents();
    };
    auto centre = [&](const shortcuts::Hit &hit) {
        const auto [s, ox, oy] = pv->_fit();
        return QPointF(ox + (hit.x + hit.w / 2.0) * s, oy + (hit.y + hit.h / 2.0) * s);
    };
    auto sort_btn = [&](const QString &text) {
        for (QAbstractButton *b : ws->sort_group->buttons())
            if (b->text() == text) {
                b->click();
                QApplication::processEvents();
            }
    };
    auto pe = [] { QApplication::processEvents(); };

    // --- the page
    win.set_tab(u"shortcuts"_s);
    pe();
    snap(u"shown"_s);
    grab(u"keys_shown.png"_s);
    if (QTreeWidgetItem *it = first_item(ws->tree)) {
        ws->tree->setCurrentItem(it);
        pe();
    }
    snap(u"picked"_s);
    grab(u"keys_picked.png"_s);
    // --- keys
    ws->typed->setText(u"x"_s);
    ws->set_btn->click();
    pe();
    snap(u"key x"_s);
    ws->typed->setText(u"not a key"_s);
    Q_EMIT ws->typed->returnPressed();
    pe();
    snap(u"not a key"_s);
    ws->catch_->setChecked(true);
    pe();
    snap(u"catching"_s);
    {
        QKeyEvent e1(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, QString());
        QApplication::sendEvent(ws->catch_, &e1);
        pe();
        QKeyEvent e2(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier, QString());
        QApplication::sendEvent(ws->catch_, &e2);
        pe();
    }
    snap(u"caught F5"_s);
    ws->typed->setText(u"ESCAPE"_s);
    ws->set_btn->click();
    pe();
    snap(u"key ESCAPE"_s);
    ws->none_btn->click();
    pe();
    snap(u"no key"_s);
    grab(u"keys_set.png"_s);
    // --- the list's orders and filters
    sort_btn(u"By key"_s);
    snap(u"by key"_s);
    sort_btn(u"By what they do"_s);
    snap(u"by does"_s);
    sort_btn(u"By window"_s);
    ws->only_btn->setChecked(true);
    pe();
    snap(u"only"_s);
    ws->only_btn->setChecked(false);
    ws->search->setText(u"close"_s);
    pe();
    snap(u"search"_s);
    ws->search->setText(QString());
    pe();
    for (const QString &k : {u"niche"_s, u"none"_s, u"all"_s, u"good"_s}) {
        ws->cat_btn[k]->click();
        pe();
        snap(u"cat "_s + k);
    }
    ws->grey_btn->setChecked(true);
    pe();
    snap(u"grey on"_s);
    grab(u"keys_grey.png"_s);
    // --- the production screen from the top bar
    for (const shortcuts::Button &b : ws->_buttons)
        if (py::lower(b.name) == u"topbarbutton_production") {
            const Ident id = b.ident();
            ws->_open_screen(id);
            pe();
            break;
        }
    snap(u"production"_s);
    grab(u"keys_production.png"_s);
    for (int i : {1, 0})
        if (int(ws->part_btn.size()) > i) {
            ws->part_btn[size_t(i)]->click();
            pe();
            snap(u"part "_s + QString::number(i));
        }
    if (ws->ver_btn.size() > 1) {
        ws->ver_btn[1]->click();
        pe();
        snap(u"version 2"_s);
    }
    // --- a button clicked in the drawn window
    std::optional<shortcuts::Hit> hit;
    for (const shortcuts::Hit &x : pv->hits)
        if (pv->shown(x.ident) && !(pv->pick && x.ident == *pv->pick)) {
            hit = x;
            break;
        }
    if (hit) {
        const QPointF p = centre(*hit);
        mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        out << u"tooltip "_s + v(pv->toolTip());
        mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    }
    snap(u"clicked"_s);
    grab(u"keys_clicked.png"_s);
    ws->near_btn->click();
    pe();
    snap(u"zoomed"_s);
    const QPointF pos(pv->width() / 3.0, pv->height() / 2.0);
    {
        QWheelEvent e(pos, pv->mapToGlobal(pos), QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
        QApplication::sendEvent(pv, &e);
        pe();
    }
    snap(u"wheel"_s);
    mouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, pos + QPointF(40, 25), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, pos + QPointF(40, 25), Qt::LeftButton, Qt::NoButton);
    snap(u"dragged"_s);
    grab(u"keys_zoomed.png"_s);
    for (QPushButton *b : ws->findChildren<QPushButton *>())
        if (b->text() == u"Whole screen") {
            b->click();
            pe();
        }
    snap(u"whole"_s);
    ws->typed->setText(u"k"_s);
    ws->set_btn->click();
    pe();
    snap(u"key k"_s);
    // --- the main window's undo
    out << u"undo "_s + v(win.undo());
    pe();
    snap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    pe();
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
