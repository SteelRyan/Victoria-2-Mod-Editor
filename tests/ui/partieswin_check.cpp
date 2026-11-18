// The parties window opened from the Nation page against Python, the C++ side of tools/check_partieswin.py.
// Usage: med_partieswin_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/nationextras.h"
#include "ui/nationpanel.h"
#include "ui/partieswin.h"
#include "ui/theme.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

template <class T> static QList<T *> form_of(QWidget *form)
{
    QList<T *> out;
    for (T *x : form->findChildren<T *>())
        if (x->parent() == form)
            out << x;
    return out;
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

    auto scripts = [&] {
        auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);
        QStringList rs = store.dirty();
        std::sort(rs.begin(), rs.end());
        QStringList parts;
        for (const QString &r : rs)
            parts << r + u":"_s + h(store.text(r).toUtf8());
        return h(parts.join(u';').toUtf8());
    };
    auto names = [&] {
        auto &loc = d.map<localisation::LocEdits>(u"loc"_s);
        QStringList ks;
        for (const auto &[k, _t] : loc.fresh)
            ks << k;
        std::sort(ks.begin(), ks.end());
        QStringList parts;
        for (const QString &k : ks)
            parts << k + u"="_s + loc.fresh.get(k)->first;
        return h(parts.join(u';').toUtf8());
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | tab %3 | nation %4 | msg %5 | %6 | steps %7 | scr %8 | loc %9"_s.arg(
            tag, win.mode, win.tab, win.nation_tag.value_or(u"None"_s), v(win.st_msg->text()), win.st_pending->text(),
            QString::number(d.undo_stack.size()), scripts(), names());
    };
    auto combo = [](const QComboBox *cb) {
        return u"%1/%2 %3"_s.arg(cb->currentIndex()).arg(cb->count()).arg(v(cb->currentText()));
    };
    auto items = [](const QComboBox *cb) {
        QStringList r;
        for (int i = 0; i < cb->count(); ++i)
            r << cb->itemText(i);
        return r.join(u';');
    };
    partieswin::PartiesWindow *pw = nullptr;
    auto wsnap = [&](const QString &tag) {
        common(tag);
        partieswin::PartiesWindow *w = pw;
        partieswin::Timeline *tl = w->timeline;
        nationextras::PartiesBox *box = w->box;
        out << u"%1 win %2 | %3 | vis %4 | geo %5,%6 %7x%8 | f %9 [%10] | %11 [%12] | %13 %14 [%15] | status %16"_s.arg(
            tag, v(w->title->text()), v(w->windowTitle()), v(w->isVisible()), QString::number(w->x()),
            QString::number(w->y()), QString::number(w->width()), QString::number(w->height()), combo(w->f_ideo))
                   .arg(items(w->f_ideo), combo(w->f_issue), items(w->f_issue), combo(w->f_opt), v(w->f_opt->isEnabled()),
                        items(w->f_opt), v(w->status->text()));
        out << u"%1 tl lo %2 hi %3 game %4 %5 | sel %6 | minh %7 | %8x%9 | cursor %10 | filter %11"_s.arg(
            tag, v(tl->_lo), v(tl->_hi), v(tl->game.first), v(tl->game.second), QString::number(tl->sel),
            QString::number(tl->minimumHeight()), QString::number(tl->width()))
                   .arg(tl->height())
                   .arg(int(tl->cursor().shape()))
                   .arg(v(bool(tl->filter)));
        QStringList ln;
        for (const auto &[q, lane] : tl->_lanes) {
            const partieswin::Party &p = tl->parties[q];
            ln << u"%1:%2:%3:%4:%5:%6"_s.arg(p.i).arg(lane).arg(v(p.a), v(p.b), p.name, p.ideology);
        }
        out << u"%1 lanes %2"_s.arg(tag, ln.join(u';'));
        QStringList hd;
        for (const auto &[ideo, first, n] : tl->_heads)
            hd << u"%1:%2:%3"_s.arg(ideo).arg(first).arg(n);
        out << u"%1 heads %2"_s.arg(tag, hd.join(u';'));
        QStringList tips;
        for (const partieswin::Party &p : tl->parties)
            tips << tl->tip(p);
        out << u"%1 tips %2 %3"_s.arg(tag).arg(tl->parties.size()).arg(h(tips.join(u'\n').toUtf8()));
        QStringList rows;
        for (int i = 0; i < box->lst->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = box->lst->topLevelItem(i);
            rows << u"%1/%2/%3/%4"_s.arg(it->text(0), it->text(1), it->text(2), it->toolTip(0));
        }
        QTreeWidgetItem *cur = box->lst->currentItem();
        out << u"%1 list %2 cur %3 | %4 | %5"_s.arg(tag)
                   .arg(box->cur)
                   .arg(cur ? box->lst->indexOfTopLevelItem(cur) : -1)
                   .arg(rows.join(u';'), v(box->summary->text()));
        QStringList fw;
        if (box->form)
            for (QWidget *x : box->form->findChildren<QWidget *>()) {
                if (x->parent() != box->form)
                    continue;
                if (auto *b = qobject_cast<QPushButton *>(x))
                    fw << u"B %1 %2"_s.arg(v(b->text()), v(b->isVisibleTo(box->form)));
                else if (auto *e = qobject_cast<QLineEdit *>(x))
                    fw << u"E %1 %2"_s.arg(v(e->text()), v(e->placeholderText()));
                else if (auto *c = qobject_cast<QComboBox *>(x))
                    fw << u"C "_s + combo(c);
                else if (auto *l = qobject_cast<QLabel *>(x))
                    fw << u"L "_s + v(l->text());
            }
        out << u"%1 form %2 | warn %3 | new %4"_s.arg(tag, fw.join(u';'), v(box->warn->text()), v(box->new_note->text()));
    };
    auto mouse = [&](QEvent::Type kind, const QPointF &pos, Qt::MouseButton button = Qt::NoButton) {
        partieswin::Timeline *tl = pw->timeline;
        const Qt::MouseButtons btns = kind == QEvent::MouseButtonPress ? Qt::MouseButtons(button) : Qt::NoButton;
        QMouseEvent ev(kind, pos, tl->mapToGlobal(pos), button, btns, Qt::NoModifier);
        QApplication::sendEvent(tl, &ev);
    };
    auto choose = [](QComboBox *cb) {
        for (int i = 0; i < cb->count(); ++i)
            if (i != cb->currentIndex()) {
                cb->setCurrentIndex(i);
                break;
            }
        Q_EMIT cb->activated(cb->currentIndex());
    };

    countryhist::NationData &data = *m->nation_data;
    countryhist::CountryHistory &hist = *m->country_hist;
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    QStringList cands;
    for (const QString &t : tags)
        if (hist.files.contains(t) && nationpanel::stats_owned(d, *m, t).size() >= 3 && data.parties(t).size() >= 2 &&
            !m->countries.get(t)->file.isEmpty())
            cands << t;
    QString tag = cands[0];
    for (const QString &t : cands)
        if (data.parties(t).size() > data.parties(tag).size())
            tag = t;
    out << u"nation %1 (%2 parties)"_s.arg(tag).arg(data.parties(tag).size());
    win.set_tab(u"nations"_s);
    win.show_nation(tag);
    QApplication::processEvents();
    win.nations_panel->parties_box->win_btn->click();
    QApplication::processEvents();
    pw = win.party_win;
    QApplication::processEvents();
    wsnap(u"open"_s);
    pw->grab().save(QDir(out_dir).filePath(u"partieswin.png"_s));
    partieswin::Timeline *tl = pw->timeline;
    // --- the mouse over a line, then a line clicked
    {
        const auto [q0, lane0] = tl->_lanes[0];
        const QRectF r = tl->_line(tl->parties[q0], lane0);
        mouse(QEvent::MouseMove, r.center());
    }
    wsnap(u"hover"_s);
    {
        const auto [q1, lane1] = tl->_lanes.size() > 1 ? tl->_lanes[1] : tl->_lanes[0];
        const QRectF r = tl->_line(tl->parties[q1], lane1);
        mouse(QEvent::MouseButtonPress, r.center(), Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, r.center(), Qt::LeftButton);
    }
    wsnap(u"clicked"_s);
    // --- a line's ends dragged
    for (const auto &[end, dx] : {std::pair{u"right"_s, 45}, std::pair{u"left"_s, -30}}) {
        size_t qi = 0;
        int lane = 0;
        for (const auto &[q, l] : tl->_lanes)
            if (tl->parties[q].i == tl->sel) {
                qi = q;
                lane = l;
                break;
            }
        const QRectF r = tl->_line(tl->parties[qi], lane);
        const double x0 = end == u"right" ? r.right() : r.left();
        const QPointF a(x0, r.center().y());
        mouse(QEvent::MouseMove, a);
        mouse(QEvent::MouseButtonPress, a, Qt::LeftButton);
        const QPointF b(x0 + dx, r.center().y());
        mouse(QEvent::MouseMove, b);
        out << u"drag %1 | status %2"_s.arg(end, v(pw->status->text()));
        mouse(QEvent::MouseButtonRelease, b, Qt::LeftButton);
        QApplication::processEvents();
        wsnap(u"dragged "_s + end);
    }
    // --- the form
    {
        QLineEdit *e = form_of<QLineEdit>(pw->box->form)[0];
        e->setText(u"Testpartei  der  Probe"_s);
        Q_EMIT e->editingFinished();
    }
    QApplication::processEvents();
    wsnap(u"named"_s);
    choose(form_of<QComboBox>(pw->box->form)[0]);
    QApplication::processEvents();
    wsnap(u"ideology"_s);
    {
        const QList<QComboBox *> cbs = form_of<QComboBox>(pw->box->form);
        if (cbs.size() > 1) {
            choose(cbs[1]);
            QApplication::processEvents();
        }
    }
    wsnap(u"policy"_s);
    for (const QString &text : {u"1840.1.1"_s, u"18x0"_s}) {
        QLineEdit *e = form_of<QLineEdit>(pw->box->form)[2];
        e->setText(text);
        Q_EMIT e->editingFinished();
        QApplication::processEvents();
        wsnap(u"date "_s + text);
    }
    // --- another party picked in the list
    nationextras::PartiesBox *box = pw->box;
    box->lst->setCurrentItem(box->lst->topLevelItem(0));
    QApplication::processEvents();
    wsnap(u"list pick"_s);
    // --- a new party, one removed
    box->new_name->setText(u"Neue Partei"_s);
    box->new_btn->click();
    QApplication::processEvents();
    wsnap(u"new party"_s);
    for (QPushButton *b : form_of<QPushButton>(box->form))
        if (b->text().contains(u"Remove"_s)) {
            b->click();
            break;
        }
    QApplication::processEvents();
    wsnap(u"removed"_s);
    // --- the filters
    {
        const std::vector<QComboBox *> fs{pw->f_ideo, pw->f_issue, pw->f_opt};
        for (size_t i = 0; i < fs.size(); ++i) {
            QComboBox *cb = fs[i];
            if (cb->count() > 1 && cb->isEnabled()) {
                cb->setCurrentIndex(1);
                Q_EMIT cb->activated(1);
            }
            wsnap(u"filter %1"_s.arg(i));
        }
        for (QComboBox *cb : {pw->f_ideo, pw->f_issue}) {
            cb->setCurrentIndex(0);
            Q_EMIT cb->activated(0);
        }
    }
    wsnap(u"filters off"_s);
    // --- Esc: the window stays
    {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(pw, &ev);
    }
    wsnap(u"esc"_s);
    tl->grab().save(QDir(out_dir).filePath(u"timeline.png"_s));
    // --- undo
    for (int i = 0; i < 2; ++i)
        out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
    wsnap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    wsnap(u"applied"_s);
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
