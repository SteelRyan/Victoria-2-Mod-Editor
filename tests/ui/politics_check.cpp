// The Politics tab against Python, the C++ side of tools/check_politics.py.
// Usage: med_politics_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/defpanel.h"
#include "ui/entryview.h"
#include "ui/issuepanel.h"
#include "ui/mapview.h"
#include "ui/politicspanel.h"
#include "ui/reformwin.h"
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
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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
static const QStringList RELS = {u"common/issues.txt"_s, u"common/governments.txt"_s, u"common/ideologies.txt"_s};

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
        return u"B %1 %2%3 | %4"_s.arg(v(b->text()), v(b->isChecked()), en, v(b->toolTip()));
    if (auto *l = qobject_cast<QLabel *>(w))
        return u"L %1 | %2"_s.arg(v(l->text()), v(l->styleSheet()));
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
    QStringList named;                             // the localisation keys the steps write

    auto common = [&](const QString &tag) {
        QStringList texts, names;
        for (const QString &r : RELS)
            texts << u"%1:%2"_s.arg(r.mid(r.lastIndexOf(u'/') + 1), h(store.text(r).toUtf8()));
        for (const QString &k : named)
            names << u"%1=%2"_s.arg(k, key(loc.text(k)));
        out << u"%1 | tab %2 | msg %3 | %4 | steps %5 | %6 | names %7"_s.arg(
            tag, win.tab, v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()),
            texts.join(u' '), names.join(u';'));
    };
    auto tree_lines = [&](const QString &tag, QTreeWidget *t, const std::function<QString(const QVariant &)> &data) {
        std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *it, int depth) {
            out << u"%1 I %2 %3 | %4 | %5 | %6 %7 %8 | %9 | %10"_s.arg(
                tag, QString::number(depth), v(it->text(0)), v(it->text(1)),
                t->columnCount() > 2 ? v(it->text(2)) : QString(), v(it->isExpanded()), v(it->isHidden()),
                v(t->currentItem() == it), data(it->data(0, Qt::UserRole)))
                       .arg(v(it->toolTip(0)));
            for (int i = 0; i < it->childCount(); ++i)
                walk(it->child(i), depth + 1);
        };
        out << u"%1 T %2 cols %3 root %4"_s.arg(tag, QString::number(t->topLevelItemCount()),
                                                QString::number(t->columnCount()), v(t->rootIsDecorated()));
        for (int i = 0; i < t->topLevelItemCount(); ++i)
            walk(t->topLevelItem(i), 0);
    };
    auto window_lines = [&](const QString &tag, reformwin::GameReformView *wv) {
        QString pic = u"none"_s;
        if (wv->img && !wv->img->isNull()) {
            const QImage &img = *wv->img;
            pic = u"%1x%2 %3"_s.arg(img.width()).arg(img.height()).arg(
                h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes())));
        }
        const auto [s, ox, oy] = wv->_scale();
        out << u"%1 G pic %2 | off %3,%4 | missing %5 | sel %6 | hover %7 | tip %8 | scale %9"_s.arg(
            tag, pic, v(wv->offset.first), v(wv->offset.second), wv->missing.join(u','),
            wv->sel ? wv->sel->first + u'/' + wv->sel->second : u"None"_s,
            wv->_hover ? wv->_hover->first + u'/' + wv->_hover->second : u"None"_s, v(wv->toolTip()),
            u"%1,%2,%3"_s.arg(v(s), v(ox), v(oy)));
        for (const reformwin::GameReformView::Box &b : wv->boxes) {
            QStringList opts;
            if (b.mine)
                if (const auto *o = wv->options.get(b.name))
                    for (const auto &[n, t, k] : *o)
                        opts << u"%1:%2:%3"_s.arg(n, v(t), k);
            out << u"%1 X %2 %3 %4 | %5 | %6 | %7"_s.arg(tag, rc(b.r), b.name, v(b.mine), v(wv->names.value(b.name)),
                                                        v(wv->current.value(b.name)), opts.join(u';'));
        }
    };
    auto data_of = [](const QString &page) -> std::function<QString(const QVariant &)> {
        if (page == u"reforms" || page == u"unciv")
            return [](const QVariant &d) {
                if (!d.isValid())
                    return u"None"_s;
                QStringList l = d.toStringList();
                while (l.size() < 3)
                    l << u"None"_s;
                return l.join(u'|');
            };
        if (page == u"ideologies")
            return [](const QVariant &d) {
                if (!d.isValid())
                    return u"None"_s;
                const QStringList l = d.toStringList();
                return u"%1|%2"_s.arg(l.value(0), l.mid(1).join(u'/'));
            };
        return [](const QVariant &d) { return d.isValid() ? d.toString() : u"None"_s; };
    };
    politicspanel::PoliticsWorkspace *pw = win.pol_ws;
    auto snap = [&](const QString &tag) {
        common(tag);
        QStringList counts, on_ordered;           // (Python's dict: PAGES' order)
        for (const auto &[k, _t] : politicspanel::PAGES) {
            widgets::BadgeButton *b = pw->seg.at(k);
            if (b->isChecked())
                on_ordered << k;
            counts << u"%1:%2:%3"_s.arg(k).arg(b->errors).arg(b->warnings);
        }
        out << u"%1 W page %2 | seg %3 | counts %4"_s.arg(tag, pw->page, on_ordered.join(u','), counts.join(u';'));
        QWidget *page = pw->stack->currentWidget();
        for (QWidget *w : widgets_in(page, page)) {
            if (auto *t = qobject_cast<QTreeWidget *>(w))
                tree_lines(tag, t, data_of(pw->page));
            else if (auto *wv = qobject_cast<reformwin::GameReformView *>(w))
                window_lines(tag, wv);
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
    auto grab = [&](const QString &name) {
        QApplication::processEvents();
        pw->grab().save(QDir(out_dir).filePath(name));
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
    const auto any = std::function<bool(QWidget *)>();
    auto view_pt = [](reformwin::GameReformView *wv, double x, double y) {
        const auto [s, ox, oy] = wv->_scale();
        return QPointF(ox + x * s, oy + y * s);
    };
    auto pe = static_cast<QPushButton *>(nullptr);
    auto le = static_cast<QLineEdit *>(nullptr);
    auto cb_ = static_cast<QCheckBox *>(nullptr);
    auto co = static_cast<QComboBox *>(nullptr);
    auto pt_ = static_cast<QPlainTextEdit *>(nullptr);
    auto txt_is = [](const QString &t) { return [t](QWidget *w) { return static_cast<QAbstractButton *>(w)->text() == t; }; };
    auto txt_starts = [](const QString &t) {
        return [t](QWidget *w) { return static_cast<QAbstractButton *>(w)->text().startsWith(t); };
    };
    auto ph_is = [](const QString &t) { return [t](QWidget *w) { return static_cast<QLineEdit *>(w)->placeholderText() == t; }; };
    auto right_with_text = [](QWidget *w) {
        auto *e = static_cast<QLineEdit *>(w);
        return (e->alignment() & Qt::AlignRight) && !e->text().isEmpty();
    };

    // --- the reforms, as the game's window
    win.set_tab(u"politics"_s);
    QApplication::processEvents();
    snap(u"politics"_s);
    grab(u"pol_window.png"_s);
    auto *iv = static_cast<issuepanel::IssueView *>(*pw->views.get(u"reforms"_s));
    reformwin::GameReformView *wv = iv->win_view;
    std::vector<const reformwin::GameReformView::Box *> mine;
    for (const auto &b : wv->boxes)
        if (b.mine)
            mine.push_back(&b);
    const reformwin::GameReformView::Box *b0 = mine.empty() ? nullptr : mine[0];
    if (b0)
        click_at(wv, view_pt(wv, b0->r.left() + 50, b0->r.top() + 10));
    snap(u"reform clicked"_s);
    if (b0) {
        const auto *o = wv->options.get(b0->name);
        if (o && o->size() > 1) {
            const QRectF r = wv->_opt_row(b0->r, 1);
            click_at(wv, view_pt(wv, r.center().x(), r.center().y()));
        }
    }
    snap(u"option clicked"_s);
    {
        const reformwin::GameReformView::Box *other = nullptr;
        for (const auto &b : wv->boxes)
            if (!b.mine) {
                other = &b;
                break;
            }
        const reformwin::GameReformView::Box *hb = other ? other : (mine.empty() ? nullptr : mine.back());
        if (hb) {
            send(wv, QEvent::MouseMove, view_pt(wv, hb->r.left() + 60, hb->r.top() + 8), Qt::NoButton, Qt::NoButton);
            QApplication::processEvents();
        }
        out << u"hover %1 | %2"_s.arg(v(wv->toolTip())).arg(int(wv->cursor().shape()));
    }
    // --- as a list
    iv->view_btn.at(u"list"_s)->click();
    QApplication::processEvents();
    snap(u"as a list"_s);
    iv->search->setText(u"slave"_s);
    QApplication::processEvents();
    snap(u"search"_s);
    iv->search->setText(QString());
    QApplication::processEvents();
    if (auto eds = found(iv->rows_host, le, right_with_text); !eds.isEmpty()) {
        eds[0]->setText(u"0.75"_s);
        Q_EMIT eds[0]->editingFinished();
        QApplication::processEvents();
    }
    snap(u"number set"_s);
    if (auto eds = found(iv->rows_host, le, right_with_text); !eds.isEmpty()) {
        eds[0]->setText(u"abc"_s);
        Q_EMIT eds[0]->editingFinished();
        QApplication::processEvents();
    }
    snap(u"bad number"_s);
    {
        auto boxes = found(iv->rows_host, co, any);
        auto vals = found(iv->rows_host, le, ph_is(u"value"_s));
        auto adds = found(iv->rows_host, pe, txt_is(u"＋"_s));
        if (!boxes.isEmpty() && !vals.isEmpty() && !adds.isEmpty()) {
            boxes[0]->setEditText(u"rich_vote"_s);
            vals[0]->setText(u"0.2"_s);
            adds[0]->click();
            QApplication::processEvents();
        }
    }
    snap(u"effect added"_s);
    if (auto xs = found(iv->rows_host, pe, txt_is(u"✕"_s)); !xs.isEmpty()) {
        xs[0]->click();
        QApplication::processEvents();
    }
    snap(u"effect off"_s);
    if (const auto k0 = iv->_key(); k0 && !k0->isEmpty()) {
        named << *k0 << *k0 + u"_desc"_s;
    }
    iv->name_edit->setText(u"A  test   name"_s);
    Q_EMIT iv->name_edit->editingFinished();
    iv->desc_edit->setText(u"Its description"_s);
    Q_EMIT iv->desc_edit->editingFinished();
    QApplication::processEvents();
    snap(u"named"_s);
    for (const QString &extra : {u"\nthe_test_key = yes"_s, u"\n{"_s}) {
        auto eds = found(iv->rows_host, pt_, any);
        auto saves = found(iv->rows_host, pe, txt_starts(u"Save “"_s));
        if (!eds.isEmpty() && !saves.isEmpty()) {
            eds[0]->setPlainText(eds[0]->toPlainText() + extra);
            saves[0]->click();
            QApplication::processEvents();
        }
        snap(extra == u"\n{" ? u"braces"_s : u"block saved"_s);
    }
    {
        QTreeWidget *t = iv->lst;
        QTreeWidgetItem *issue_item = nullptr;
        for (int i = 0; i < t->topLevelItemCount(); ++i) {
            QTreeWidgetItem *g = t->topLevelItem(i);
            if (g->childCount()) {
                issue_item = g->child(std::min(1, g->childCount() - 1));
                break;
            }
        }
        if (issue_item) {
            t->setCurrentItem(issue_item);
            QApplication::processEvents();
        }
    }
    snap(u"reform picked"_s);
    if (auto cbs = found(iv->rows_host, cb_, any); !cbs.isEmpty()) {
        cbs[0]->click();
        QApplication::processEvents();
    }
    snap(u"ticked"_s);
    {
        auto obs = found(iv->rows_host, pe, [](QWidget *w) { return !w->toolTip().isEmpty(); });
        if (obs.size() > 1) {
            obs[1]->click();
            QApplication::processEvents();
        }
    }
    snap(u"option button"_s);
    iv->view_btn.at(u"window"_s)->click();
    QApplication::processEvents();
    snap(u"window again"_s);
    // --- the unciv reforms
    pw->seg.at(u"unciv"_s)->click();
    QApplication::processEvents();
    snap(u"unciv"_s);
    grab(u"pol_unciv.png"_s);
    {
        reformwin::GameReformView *uv = static_cast<issuepanel::IssueView *>(*pw->views.get(u"unciv"_s))->win_view;
        for (const auto &b : uv->boxes)
            if (b.mine) {
                click_at(uv, view_pt(uv, b.r.left() + 50, b.r.top() + 10));
                break;
            }
    }
    snap(u"unciv clicked"_s);
    // --- the government forms
    pw->seg.at(u"governments"_s)->click();
    QApplication::processEvents();
    snap(u"governments"_s);
    auto *gv = static_cast<defpanel::DefView *>(*pw->views.get(u"governments"_s));
    if (gv->lst->topLevelItemCount() > 1) {
        gv->lst->setCurrentItem(gv->lst->topLevelItem(1));
        QApplication::processEvents();
    }
    snap(u"government picked"_s);
    grab(u"pol_gov.png"_s);
    if (auto cbs = found(gv->rows_host, cb_, any); !cbs.isEmpty()) {
        cbs[0]->click();
        QApplication::processEvents();
    }
    snap(u"party ticked"_s);
    if (auto el = found(gv->rows_host, cb_, txt_is(u"The nation holds elections"_s)); !el.isEmpty()) {
        el[0]->click();
        QApplication::processEvents();
    }
    snap(u"elections"_s);
    if (gv->dur_edit) {
        gv->dur_edit->setText(u"0"_s);
        Q_EMIT gv->dur_edit->editingFinished();
        QApplication::processEvents();
        snap(u"months wrong"_s);
        gv->dur_edit->setText(u"60"_s);
        Q_EMIT gv->dur_edit->editingFinished();
        QApplication::processEvents();
    }
    snap(u"months"_s);
    if (auto fls = found(gv->rows_host, co, any); !fls.isEmpty()) {
        QComboBox *fl = fls[0];
        fl->setCurrentIndex(fl->findData(u"republic"_s));
        Q_EMIT fl->activated(fl->currentIndex());
        QApplication::processEvents();
    }
    snap(u"flag picked"_s);
    if (auto fls = found(gv->rows_host, co, any); !fls.isEmpty()) {
        fls[0]->lineEdit()->setText(u"slot9"_s);
        Q_EMIT fls[0]->lineEdit()->editingFinished();
        QApplication::processEvents();
    }
    snap(u"flag typed"_s);
    gv->new_name->setText(u"Test Republic"_s);
    Q_EMIT gv->new_name->textEdited(u"Test Republic"_s);
    QApplication::processEvents();
    snap(u"name typed"_s);
    named << gv->new_key->text() << gv->new_key->text() + u"_ruler"_s;
    if (auto mk = found(gv, pe, txt_starts(u"＋ Make a new"_s)); !mk.isEmpty()) {
        mk[0]->click();
        QApplication::processEvents();
    }
    snap(u"new government"_s);
    if (auto rm = found(gv->rows_host, pe, txt_starts(u"✕ Remove this"_s)); !rm.isEmpty()) {
        rm[0]->click();
        QApplication::processEvents();
    }
    snap(u"government removed"_s);
    // --- the ideologies
    pw->seg.at(u"ideologies"_s)->click();
    QApplication::processEvents();
    snap(u"ideologies"_s);
    auto *ev = static_cast<entryview::EntryView *>(*pw->views.get(u"ideologies"_s));
    for (int i = 0; i < ev->lst->topLevelItemCount(); ++i)
        if (ev->lst->topLevelItem(i)->childCount() > 1) {
            ev->lst->setCurrentItem(ev->lst->topLevelItem(i)->child(1));
            QApplication::processEvents();
            break;
        }
    snap(u"ideology picked"_s);
    grab(u"pol_ideo.png"_s);
    if (auto cbs = found(ev->body_host, cb_, any); !cbs.isEmpty()) {
        cbs[0]->click();
        QApplication::processEvents();
    }
    snap(u"ideology ticked"_s);
    if (auto eds = found(ev->body_host, le, [](QWidget *w) { return !static_cast<QLineEdit *>(w)->text().isEmpty(); });
        !eds.isEmpty()) {
        eds[0]->setText(u"1850.1.1"_s);
        Q_EMIT eds[0]->editingFinished();
        QApplication::processEvents();
    }
    snap(u"value set"_s);
    {
        auto nk = found(ev->body_host, le, [](QWidget *w) {
            return static_cast<QLineEdit *>(w)->placeholderText().startsWith(u"Add a value"_s);
        });
        auto nv = found(ev->body_host, le, ph_is(u"value"_s));
        auto adds = found(ev->body_host, pe, txt_is(u"＋"_s));
        if (!nk.isEmpty() && !nv.isEmpty() && !adds.isEmpty()) {
            nk[0]->setText(u"test_value"_s);
            nv[0]->setText(u"yes"_s);
            adds[0]->click();
            QApplication::processEvents();
        }
    }
    snap(u"value added"_s);
    {
        auto eds = found(ev->body_host, pt_, any);
        auto saves = found(ev->body_host, pe, txt_starts(u"Save “"_s));
        if (!eds.isEmpty() && !saves.isEmpty()) {
            eds[0]->setPlainText(eds[0]->toPlainText() + u"\nfactor = 2"_s);
            saves[0]->click();
            QApplication::processEvents();
        }
    }
    snap(u"ideology block saved"_s);
    if (auto shows = found(ev->body_host, pe, txt_starts(u"Show the whole"_s)); !shows.isEmpty()) {
        shows[0]->click();
        QApplication::processEvents();
    }
    snap(u"whole shown"_s);
    {
        auto eds = found(ev->body_host, pt_, any);
        auto saves = found(ev->body_host, pe, txt_is(u"Save the whole text"_s));
        if (!eds.isEmpty() && !saves.isEmpty()) {
            eds.back()->setPlainText(eds.back()->toPlainText() + u"\nwhole_test = 1"_s);
            saves[0]->click();
            QApplication::processEvents();
        }
    }
    snap(u"whole saved"_s);
    named << u"test_ideo"_s;
    ev->new_key->setText(u"test_ideo"_s);
    Q_EMIT ev->new_key->returnPressed();
    QApplication::processEvents();
    snap(u"new ideology"_s);
    if (auto rm = found(ev->body_host, pe, txt_starts(u"✕ Remove this"_s)); !rm.isEmpty()) {
        rm[0]->click();
        QApplication::processEvents();
    }
    snap(u"ideology removed"_s);
    // --- the main window's undo
    for (int i = 0; i < 3; ++i) {
        out << u"undo "_s + v(win.undo());
        QApplication::processEvents();
    }
    snap(u"undone"_s);
    pw->seg.at(u"reforms"_s)->click();
    QApplication::processEvents();
    snap(u"reforms again"_s);
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
