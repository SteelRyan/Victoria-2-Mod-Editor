// The Technology tab against Python, the C++ side of tools/check_tech.py.
// Usage: med_tech_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/techpanel.h"
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

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;
static const QString GUI = u"interface/country_technology.gui"_s;

static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }
static QString rc(const QRectF &r) { return u"%1,%2,%3,%4"_s.arg(v(r.x()), v(r.y()), v(r.width()), v(r.height())); }

// the widgets inside w in its layouts' order (a splitter's / scroll area's / stack's own way)
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

static std::optional<QString> desc(QWidget *w)
{
    const QString en = w->isEnabled() ? QString() : u" off"_s;
    if (auto *c = qobject_cast<QCheckBox *>(w))
        return u"K %1 %2%3"_s.arg(v(c->text()), v(c->isChecked()), en);
    if (auto *b = qobject_cast<QPushButton *>(w))
        return u"B %1 %2%3 | %4 | %5"_s.arg(v(b->text()), v(b->isChecked()), en, v(b->toolTip()), v(b->styleSheet()));
    if (auto *l = qobject_cast<widgets::ElideLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2 | %3"_s.arg(v(l->text()), v(l->styleSheet()), v(l->toolTip()));
    if (auto *e = qobject_cast<QLineEdit *>(w))
        return u"E %1 | %2 | %3%4"_s.arg(v(e->text()), v(e->placeholderText()), v(e->styleSheet()), en);
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < c->count(); ++i) {
            const QVariant d = c->itemData(i);
            items << u"%1/%2"_s.arg(c->itemText(i), d.isValid() ? d.toString() : u"None"_s);
        }
        return u"C %1 %2 %3 %4%5"_s.arg(QString::number(c->count()), QString::number(c->currentIndex()),
                                        v(c->currentText()), h(items.join(u'\n').toUtf8()), en);
    }
    if (auto *p = qobject_cast<QPlainTextEdit *>(w))
        return u"P %1 %2"_s.arg(h(p->toPlainText().toUtf8())).arg(p->height());
    if (auto *l = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < l->count(); ++i) {
            const QVariant d = l->item(i)->data(Qt::UserRole);
            items << u"%1/%2/%3"_s.arg(l->item(i)->text(), d.isValid() ? d.toString() : u"None"_s,
                                       l->item(i)->toolTip());
        }
        return u"Q %1 %2 %3 %4"_s.arg(QString::number(l->count()), QString::number(l->currentRow()),
                                      h(items.join(u'\n').toUtf8()), QString::number(l->height()));
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
    auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
    auto &techs = d.map<techdata::TechEdits>(u"techs"_s);
    QStringList named;                             // the localisation keys the steps write
    techpanel::TechWorkspace *tw = win.tech_ws;

    auto common = [&](const QString &tag) {
        QStringList parts;
        for (const QString &k : techs.keys() + techs.inventions())
            parts << u"%1=%2"_s.arg(k, techs.text(k));
        QStringList names;
        for (const QString &k : named)
            names << u"%1=%2"_s.arg(k, key(loc.text(k)));
        out << u"%1 | tab %2 | msg %3 | %4 | steps %5 | techs %6 | gui %7 | names %8"_s.arg(
            tag, win.tab, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
            h(parts.join(u'\n').toUtf8()), h(store.text(GUI).toUtf8()), names.join(u';'));
    };
    auto grid_lines = [&](const QString &tag, techpanel::TechGrid *g) {
        const QImage img = g->grab().toImage();
        out << u"%1 G %2x%3 %4 | card %5 | sel %6 | flash %7 | match %8"_s.arg(
            tag, QString::number(img.width()), QString::number(img.height()),
            h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes())),
            QString::number(g->CARD_H), key(g->sel), key(g->flash),
            g->match ? QString::number(g->match->size()) : u"None"_s);
        for (const auto &[r, k] : g->_cells) {
            const techpanel::Info *dd = g->info.get(k);
            QStringList fx;
            if (dd)
                for (const techpanel::Fx &e : dd->fx)
                    fx << u"%1:%2:%3"_s.arg(QString::number(e.sign), e.num, e.words);
            out << u"%1 X %2 %3 | %4 | %5 | %6 | %7 | %8 %9"_s.arg(
                       tag, rc(r), k, v(dd ? dd->name : QString()), v(dd ? dd->year : QString()),
                       v(dd ? dd->cost : QString()), dd ? dd->unlocks.join(u',') : QString(),
                       QString::number(dd ? dd->inv : 0), v(dd && dd->same)) +
                       u" %1 | %2"_s.arg(v(dd && dd->isnew), fx.join(u';'));
        }
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        QString pend = u"None"_s;
        if (const auto &p = tw->t_effects->pending) {
            QStringList ip;
            for (qsizetype x : p->ipath)
                ip << QString::number(x);
            pend = u"%1/%2/%3"_s.arg(ip.join(u'/'), key(p->parent), p->key);
        }
        out << u"%1 W folder %2 | sel %3 | inv %4 | unciv %5 | rows %6 | armed %7 | pending %8"_s.arg(
            tag, QString::number(tw->folder), key(tw->sel), key(tw->sel_inv), v(tw->unciv_on),
            QString::number(tw->_rows_now), key(tw->_inv_armed), pend);
        QList<QWidget *> all{tw};
        all << widgets_in(tw, tw);
        for (QWidget *w : all) {
            if (auto *g = qobject_cast<techpanel::TechGrid *>(w))
                grid_lines(tag, g);
            else if (const auto dd = desc(w))
                out << tag + u' ' + *dd;
        }
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
    auto drag = [&](QWidget *w, const QPointF &a, const QPointF &b) {
        send(w, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
        send(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        send(w, QEvent::MouseMove, QPointF(a.x() + 20, a.y() + 4), Qt::NoButton, Qt::LeftButton);
        send(w, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
        send(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    };
    auto grab = [&](const QString &name) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 400)                  // (the search box's clear button fades: let it finish)
            QApplication::processEvents();
        tw->grab().save(QDir(out_dir).filePath(name));
    };
    auto found = [](QWidget *root, auto *cls_tag, const std::function<bool(QWidget *)> &pred) {
        using T = std::remove_pointer_t<decltype(cls_tag)>;
        QList<T *> res;
        for (QWidget *w : widgets_in(root, root))
            if (auto *x = qobject_cast<T *>(w))
                if (!pred || pred(w))
                    res << x;
        return res;
    };
    auto cell = [&](const std::optional<QString> &k) -> std::optional<QRectF> {
        tw->grid->grab();                          // (the cards are placed when drawn)
        if (k)
            for (const auto &[r, kk] : tw->grid->_cells)
                if (kk == *k)
                    return r;
        return std::nullopt;
    };
    auto edit = [](QLineEdit *w, const QString &text) {
        w->setText(text);
        Q_EMIT w->editingFinished();
        QApplication::processEvents();
    };
    const auto any = std::function<bool(QWidget *)>();
    auto pe = static_cast<QPushButton *>(nullptr);
    auto le = static_cast<QLineEdit *>(nullptr);
    auto co = static_cast<QComboBox *>(nullptr);
    auto txt_is = [](const QString &t) { return [t](QWidget *w) { return static_cast<QAbstractButton *>(w)->text() == t; }; };
    auto ph_is = [](const QString &t) { return [t](QWidget *w) { return static_cast<QLineEdit *>(w)->placeholderText() == t; }; };
    auto right_with_text = [](QWidget *w) {
        auto *e = static_cast<QLineEdit *>(w);
        return (e->alignment() & Qt::AlignRight) && !e->text().isEmpty();
    };

    // --- the tree
    win.set_tab(u"technology"_s);
    QApplication::processEvents();
    snap(u"technology"_s);
    grab(u"tech_grid.png"_s);
    tw->grid->grab();
    const auto cells = tw->grid->_cells;
    if (cells.size() > 3)
        click_at(tw->grid, cells[3].first.center());
    snap(u"card clicked"_s);
    if (!cells.empty()) {
        send(tw->grid, QEvent::MouseMove, cells[0].first.center(), Qt::NoButton, Qt::NoButton);
        QApplication::processEvents();
    }
    out << u"hover %1"_s.arg(int(tw->grid->cursor().shape()));
    tw->search->setText(u"rail"_s);
    QApplication::processEvents();
    snap(u"search"_s);
    Q_EMIT tw->search->returnPressed();
    QApplication::processEvents();
    snap(u"search enter"_s);
    if (tw->folder_btns.size() > 1) {
        tw->folder_btns[1]->click();
        QApplication::processEvents();
    }
    snap(u"folder 2"_s);
    grab(u"tech_folder2.png"_s);
    // --- a technology's fields
    if (!tw->_folders_cache.empty()) {
        tw->select(tw->_folders_cache[0].areas[0].keys[1]);
        QApplication::processEvents();
    }
    snap(u"picked"_s);
    if (tw->sel)
        named << *tw->sel;
    edit(tw->t_name, u"A  Test Tech  "_s);
    snap(u"named"_s);
    tw->t_year->plus->click();
    QApplication::processEvents();
    snap(u"year up"_s);
    edit(tw->t_cost, u"4321"_s);
    snap(u"cost"_s);
    tw->t_unciv->click();
    QApplication::processEvents();
    snap(u"unciv"_s);
    // --- its effects
    if (auto eds = found(tw->t_effects, le, right_with_text); !eds.isEmpty())
        edit(eds[0], u"0.25"_s);
    snap(u"effect number"_s);
    if (auto xs = found(tw->t_effects, pe, txt_is(u"✕"_s)); !xs.isEmpty()) {
        xs.back()->click();
        QApplication::processEvents();
    }
    snap(u"effect off"_s);
    if (auto ops = found(tw->t_effects, pe, [](QWidget *w) { return w->objectName() == u"adderOpen"; }); !ops.isEmpty()) {
        ops[0]->click();
        QApplication::processEvents();
    }
    snap(u"adder open"_s);
    if (auto boxes = found(tw->t_effects, co, [](QWidget *w) {
            auto *c = static_cast<QComboBox *>(w);
            return c->lineEdit() && c->lineEdit()->placeholderText().startsWith(u"＋"_s);
        });
        !boxes.isEmpty()) {
        QComboBox *box = boxes[0];
        box->hidePopup();
        int i = box->findData(u"prestige"_s);
        i = i >= 0 ? i : 0;
        box->setCurrentIndex(i);
        Q_EMIT box->activated(i);
        QApplication::processEvents();
    }
    snap(u"effect picked"_s);
    techpanel::ValueField *pf = tw->t_effects->_pending_field;
    if (tw->t_effects->pending && pf) {
        if (pf->kind == u"choice")
            static_cast<QComboBox *>(pf->w)->setCurrentIndex(0);
        else if (pf->kind != u"bool")
            static_cast<QLineEdit *>(pf->w)->setText(u"0.5"_s);
        QApplication::processEvents();
        snap(u"value typed"_s);
        if (auto oks = found(tw->t_effects, pe, txt_is(u"✓ Add"_s)); !oks.isEmpty()) {
            oks[0]->click();
            QApplication::processEvents();
        }
    }
    snap(u"effect added"_s);
    tw->t_file->box->click();
    QApplication::processEvents();
    snap(u"as in the file"_s);
    tw->t_raw_btn->click();
    QApplication::processEvents();
    {
        const QString t0 = tw->t_raw->ed->toPlainText();
        tw->t_raw->ed->setPlainText(t0 + u"\n{"_s);
        tw->t_raw->ok->click();
        QApplication::processEvents();
        snap(u"raw wrong"_s);
        const qsizetype i = t0.lastIndexOf(u'}');
        tw->t_raw->ed->setPlainText(t0.left(i) + u"\ttest_raw = 1\n"_s + t0.mid(i));
        tw->t_raw->ok->click();
        QApplication::processEvents();
    }
    snap(u"raw saved"_s);
    // --- the AI chance
    if (auto fes = found(tw->t_chance, le, ph_is(u"e.g. 1.5"_s)); !fes.isEmpty())
        edit(fes[0], u"3"_s);
    snap(u"factor"_s);
    if (auto adds = found(tw->t_chance, pe, txt_is(u"＋ Add a factor"_s)); !adds.isEmpty()) {
        adds[0]->click();
        QApplication::processEvents();
    }
    snap(u"factor added"_s);
    if (auto rms = found(tw->t_chance, pe, [](QWidget *w) { return w->toolTip() == u"Remove this factor"; });
        !rms.isEmpty()) {
        rms.back()->click();
        QApplication::processEvents();
    }
    snap(u"factor removed"_s);
    // --- the same-year order
    std::optional<QString> same;
    for (const auto &[k, dd] : tw->_info_cache)
        if (dd.same) {
            same = k;
            break;
        }
    if (same) {
        tw->select(*same);
        QApplication::processEvents();
    }
    snap(u"same year"_s);
    for (QPushButton *b : {tw->up_btn, tw->dn_btn})
        if (b->isVisibleTo(tw) && b->isEnabled()) {
            b->click();
            QApplication::processEvents();
            break;
        }
    snap(u"order"_s);
    if (same) {
        const techpanel::Info dd = *tw->_info_cache.get(*same);
        const std::optional<QString> a = dd.col.a;
        std::optional<QString> nb;
        for (const QString &x : dd.col.ks)
            if (x != *same) {
                const techpanel::Info *e = tw->_info_cache.get(x);
                if ((e ? e->year : QString()) == dd.year) {
                    nb = x;
                    break;
                }
            }
        {
            const auto ra = cell(same);
            const auto rb = nb ? cell(nb) : std::nullopt;
            if (ra && rb)
                drag(tw->grid, ra->center(), rb->center());
        }
        snap(u"dragged"_s);
        std::optional<QString> other;
        for (const auto &[r, kk] : tw->grid->_cells) {
            const techpanel::Info *e = tw->_info_cache.get(kk);
            if ((e ? e->col.a : std::nullopt) != a) {
                other = kk;
                break;
            }
        }
        const auto ra = cell(same);
        const auto rb = other ? cell(other) : std::nullopt;
        if (ra && rb)
            drag(tw->grid, ra->center(), rb->center());
    }
    snap(u"dragged across"_s);
    // --- the inventions
    std::optional<QString> many;
    for (const QString &k : techs.keys())
        if (tw->_inv_of.value(k).size() >= 2) {
            many = k;
            break;
        }
    if (many) {
        tw->select(*many);
        QApplication::processEvents();
    }
    snap(u"inventions"_s);
    grab(u"tech_inv.png"_s);
    if (tw->i_list->count() > 1) {
        tw->i_list->setCurrentRow(1);
        QApplication::processEvents();
    }
    snap(u"invention picked"_s);
    if (tw->sel_inv) {
        named << *tw->sel_inv;
        named << *tw->sel_inv + u"_desc"_s;
    }
    edit(tw->i_name, u"Test Invention Name"_s);
    tw->i_desc->setPlainText(u"A test description"_s);
    Q_EMIT tw->i_desc->done(tw->i_desc->toPlainText());
    QApplication::processEvents();
    snap(u"invention named"_s);
    tw->i_chance->plus->click();
    QApplication::processEvents();
    snap(u"chance up"_s);
    tw->i_news->click();
    QApplication::processEvents();
    snap(u"news"_s);
    if (tw->i_needs->isEnabled() && tw->i_needs->count() > 1) {
        const int n = (tw->i_needs->currentIndex() + 1) % tw->i_needs->count();
        tw->i_needs->setCurrentIndex(n);
        Q_EMIT tw->i_needs->activated(n);
        QApplication::processEvents();
    }
    snap(u"comes with"_s);
    if (many) {
        tw->select(*many);
        QApplication::processEvents();
    }
    tw->i_del->click();
    QApplication::processEvents();
    snap(u"delete armed"_s);
    tw->i_del->click();
    QApplication::processEvents();
    snap(u"deleted"_s);
    tw->i_new_name->setText(QString());
    tw->i_new_btn->click();
    QApplication::processEvents();
    snap(u"new without name"_s);
    named << u"test_gadget"_s;
    tw->i_new_name->setText(u"  Test   Gadget "_s);
    tw->i_new_btn->click();
    QApplication::processEvents();
    snap(u"new invention"_s);
    // --- a new row, the game's window, the row removed
    tw->row_btn->click();
    QApplication::processEvents();
    snap(u"row added"_s);
    grab(u"tech_row.png"_s);
    if (tw->fit_btn->isVisibleTo(tw)) {
        tw->fit_btn->click();
        QApplication::processEvents();
    }
    snap(u"window fitted"_s);
    if (tw->row_rm_btn->isVisibleTo(tw)) {
        tw->row_rm_btn->click();
        QApplication::processEvents();
    }
    snap(u"row removed"_s);
    // --- the tech schools
    if (tw->schools_btn) {
        tw->schools_btn->click();
        QApplication::processEvents();
    }
    snap(u"schools"_s);
    grab(u"tech_schools.png"_s);
    if (!tw->folder_btns.empty()) {
        tw->folder_btns[0]->click();
        QApplication::processEvents();
    }
    snap(u"folder 1"_s);
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
