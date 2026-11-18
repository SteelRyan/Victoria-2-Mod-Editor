// The Diplomacy page's ◎ Relations view against Python, the C++ side of tools/check_diploweb.py.
// Usage: med_diploweb_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/pypath.h"
#include "core/settings.h"
#include "map/mapview_data.h"
#include "ui/diplopanel.h"
#include "ui/diploweb.h"
#include "ui/mapview.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;
static const QStringList TIES = {u"ally"_s,     u"subject"_s,   u"sphere"_s,       u"overlord"_s,
                                 u"lord"_s,     u"guarantee"_s, u"guaranteed_by"_s};

static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }
static QString pt(const QPointF &p) { return u"%1,%2"_s.arg(v(p.x()), v(p.y())); }
static QString rc(const QRectF &r) { return u"%1,%2,%3,%4"_s.arg(v(r.x()), v(r.y()), v(r.width()), v(r.height())); }
static QString rgb(const std::tuple<int, int, int> &c)
{
    return u"%1,%2,%3"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
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
    diplopanel::DiploPanel *dp = win.diplo_panel;
    diploweb::DiploWebView *dv = win.dip_view;
    diploweb::Web *web = dv->web;
    diploweb::GroupsView *gv = dv->groups_view;
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;
    oob::OOBEdits &oob = d.map<oob::OOBEdits>(u"oob"_s);
    diplomacy::DiploEdits &dip = d.map<diplomacy::DiploEdits>(u"diplomacy"_s);

    auto state = [&] {
        QStringList ts;
        for (const auto &[t, _s] : oob.want)
            ts << t;
        std::sort(ts.begin(), ts.end());
        QStringList lines;
        for (const QString &t : ts)
            for (const auto &[b, r] : oob.want.get(t)->relations)
                lines << u"%1 R %2 %3 %4 %5"_s.arg(t, b, key(r.value), key(r.level), key(r.influence_value));
        for (const diplomacy::Record &r : dip.records)
            lines << u"T %1 %2 %3 %4 %5 %6"_s.arg(r.kind, r.first, r.second, key(r.start), key(r.end), r.file);
        return u"%1 %2"_s.arg(lines.size()).arg(h(lines.join(u'\n').toUtf8()));
    };
    auto vis = [&](QWidget *w) { return v(w->isVisibleTo(dv)); };
    auto common = [&](const QString &tag) {
        out << u"%1 | tab %2 | nation %3 | b %4 | cm %5 | msg %6 | %7 | steps %8 | st %9 | map %10 | view %11"_s
                   .arg(tag, win.tab, key(win.nation_tag), key(win.diplo_b), win.color_mode, v(win.st_msg->text()),
                        win.st_pending->text(), QString::number(d.undo_stack.size()), state())
                   .arg(v(win.map->isVisible()), v(dv->isVisible()));
    };
    auto web_lines = [&](const QString &tag) {
        QStringList shown(web->shown.begin(), web->shown.end());
        std::sort(shown.begin(), shown.end());
        out << u"%1 E mode %2 | a %3 | shown %4 | scale %5 | off %6 | fitted %7 moved %8 | tip %9"_s.arg(
            tag, web->mode, key(web->a), shown.join(u','), v(web->scale_), pt(web->off), v(web->_fitted),
            v(web->_moved), v(web->toolTip()));
        if (web->mode == u"gps") {
            QStringList g, w;
            for (const diploweb::Web::GpRow &r : web->gp)
                g << u"%1:%2:%3"_s.arg(r.g, r.sph.join(u','), r.al.join(u','));
            for (const auto &[x, y] : web->wars)
                w << u"%1-%2"_s.arg(x, y);
            out << u"%1 G %2 | wars %3"_s.arg(tag, g.join(u';'), w.join(u';'));
            return;
        }
        if (!web->a || web->a->isEmpty())
            return;
        const diploweb::Web::Layout L = web->layout();
        QStringList keys, order, custom;
        for (const diploweb::SecKey &k : web->sec_keys)
            keys << k.join(u'+');
        for (const diploweb::SecKey &k : web->sec_order)
            order << k.join(u'+');
        for (const auto &[k, c] : web->custom)
            custom << u"%1|%2=%3,%4,%5"_s.arg(k.first, k.second, c.section, v(c.part), v(c.radius));
        out << u"%1 E ring %2 | keys %3 | order %4 | custom %5"_s.arg(tag, v(web->ring), keys.join(u';'),
                                                                     order.join(u';'), custom.join(u';'));
        for (const diploweb::Web::Sec &s : web->_sections)
            out << u"%1 S %2 %3 %4 %5 | %6"_s.arg(tag, s.key.join(u'+'), v(s.a0), v(s.a1), v(s.rout), s.tags.join(u','));
        QStringList pl;
        for (const diploweb::Web::Placed &p : L.placed)
            pl << u"%1 %2 %3 %4"_s.arg(p.grp, rgb(p.col), p.tag, pt(p.pt));
        out << u"%1 F %2 %3"_s.arg(tag).arg(L.placed.size()).arg(h(pl.join(u'\n').toUtf8()));
        for (const diploweb::Web::Label &lb : L.labels)
            out << u"%1 N %2 %3 %4"_s.arg(tag, pt(lb.pt), lb.text, lb.grp);
    };
    auto side_lines = [&](const QString &tag) {
        QStringList segs;
        for (const auto &[k, b] : dv->seg)
            if (b->isChecked())
                segs << k;
        out << u"%1 V mode %2 | sort %3 | seg %4 | title %5 | ctl %6 glist %7 side %8 pick %9 | head %10 %11 | "
               u"name %12 | rel_head %13"_s.arg(tag, dv->mode, dv->sort, segs.join(u','), v(dv->title->text()),
                                                vis(dv->ctl), vis(dv->glist_box), vis(dv->side), vis(dv->pick_sc))
                   .arg(vis(dv->view_head), vis(dv->view_flag), v(dv->view_name->text()), vis(dv->rel_head));
        QStringList chips;
        for (const diploweb::GroupDef &g : diploweb::GROUPS) {
            QPushButton *b = dv->chip_btn.at(g.key);
            chips << u"%1:%2%3"_s.arg(g.key, v(b->isChecked()), v(b->isVisibleTo(dv)));
        }
        out << u"%1 C %2 | sortbox %3 %4"_s.arg(tag, chips.join(u';')).arg(dv->sort_box->currentIndex()).arg(
            v(dv->sort_box->currentText()));
        QStringList rows;
        for (const auto &[t, val] : dv->chart->rows)
            rows << u"%1:%2"_s.arg(t).arg(val);
        out << u"%1 L %2 | chart %3 h %4"_s.arg(tag, v(dv->lists->text()), rows.join(u';')).arg(dv->chart->height());
        out << u"%1 P b %2 | %3 | %4 | %5 | rel %6 | war %7 %8"_s.arg(
            tag, key(dv->b), v(dv->pick_head->text()), v(dv->pick_name->text()), v(dv->pick_status->text()),
            v(dv->pick_rel->value()), v(dv->pick_war->text()), vis(dv->pick_war));
        QStringList ties, heads;
        for (const QString &k : TIES) {
            QCheckBox *cb = dv->tie_box.at(k);
            ties << u"%1:%2:%3%4%5"_s.arg(k, cb->text(), v(cb->isChecked()), v(cb->isEnabled()), v(cb->isVisibleTo(dv)));
        }
        for (const QString &k : {u"ally"_s, u"under"_s, u"over"_s, u"guar"_s})
            heads << u"%1:%2:%3"_s.arg(k, dv->tie_head.at(k)->text(), v(dv->tie_head.at(k)->isVisibleTo(dv)));
        out << u"%1 T %2 | heads %3"_s.arg(tag, ties.join(u';'), heads.join(u';'));
    };
    auto group_lines = [&](const QString &tag) {
        QStringList rows;
        for (int i = 0; i < dv->glist->count(); ++i) {
            QListWidgetItem *it = dv->glist->item(i);
            const QVariant k = it->data(Qt::UserRole);
            rows << u"%1/%2/%3/%4"_s.arg(it->text(), k.isValid() ? k.toString() : u"None"_s)
                        .arg(int(it->checkState()))
                        .arg(v(it->isHidden()));
        }
        out << u"%1 R %2 %3 | on %4 | search %5"_s.arg(tag)
                   .arg(rows.size())
                   .arg(h(rows.join(u'\n').toUtf8()), dv->groups_on.value_or(QStringList()).join(u','),
                        v(dv->gsearch->text()));
        QStringList sets;
        for (const diploweb::GroupsView::Set &s : gv->sets)
            sets << u"%1:%2:%3"_s.arg(s.title).arg(s.tags.size()).arg(rgb(s.col));
        QStringList mis(gv->misplaced.begin(), gv->misplaced.end());
        std::sort(mis.begin(), mis.end());
        out << u"%1 B sets %2 | world %3 | misplaced %4"_s.arg(tag, sets.join(u';'), rc(gv->_world), mis.join(u','));
        for (const diploweb::GroupsView::Box &b : gv->_boxes)
            out << u"%1 X %2 %3 %4 %5"_s.arg(tag).arg(b.mask).arg(rc(b.rect)).arg(b.cols).arg(b.tags.join(u','));
        for (const diploweb::GroupsView::GRect &g : gv->_rects)
            out << u"%1 Q %2 %3 %4 %5 %6"_s.arg(tag, rc(g.rect)).arg(g.depth).arg(rc(g.lab), g.text, v(g.backed));
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        side_lines(tag);
        if (dv->mode == u"groups")
            group_lines(tag);
        else
            web_lines(tag);
    };
    auto send = [&](QWidget *w, QEvent::Type kind, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(kind, pos, pos, button, buttons, nomod);
        QApplication::sendEvent(w, &e);
    };
    auto screen = [](const QPointF &p, diploweb::ZoomCanvas *w) {
        return QPointF(w->off.x() + p.x() * w->scale_, w->off.y() + p.y() * w->scale_);
    };
    auto pick = [](QComboBox *cb, const QString &data) {
        cb->setCurrentIndex(cb->findData(data));
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto grab = [&](const QString &name) {
        QApplication::processEvents();
        dv->grab().save(QDir(out_dir).filePath(name));
    };
    auto seg = [&](const QString &k) {
        for (const auto &[kk, b] : dv->seg)
            if (kk == k)
                b->click();
    };
    auto shown_btn = [&](const QString &k) {
        for (const auto &[kk, b] : dp->shown_btn)
            if (kk == k)
                b->click();
    };
    auto setting = [&](const QString &k) { return settings_::settings()->value(k, u""_s).toString(); };

    countryhist::NationEdits &nat = d.map<countryhist::NationEdits>(u"nations"_s);
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    std::map<QString, std::vector<long long>> owned;
    for (const QString &t : tags)
        owned[t] = nationpanel::stats_owned(d, *m, t);
    const QStringList gps = diplomacy::great_powers(*m, nat);
    win.set_tab(u"diplomacy"_s);
    QApplication::processEvents();
    QStringList sg = gps;
    std::sort(sg.begin(), sg.end());
    const std::vector<diplopanel::WarRow> wars = dp->wars();
    std::map<QString, long long> ties;
    for (const QString &g : sg) {
        long long n = 0;
        for (const auto &[k, x] : diploweb::web_of(g, *m, &oob, &dip, wars, gps))
            if (diploweb::SHOWN_AT_FIRST.contains(k))
                n += x.size();
        ties[g] = n;
    }
    QString ta = sg[0];                            // (the great power with the most ties at the start)
    for (const QString &g : sg)
        if (ties[g] > ties[ta])
            ta = g;
    QString tb2;
    for (const QString &g : sg)
        if (g != ta) {
            tb2 = g;
            break;
        }
    QStringList tl;
    for (const QString &g : sg)
        tl << u"%1:%2"_s.arg(g).arg(ties[g]);
    out << u"gps %1 | a %2 | gp2 %3 | ties %4"_s.arg(gps.join(u','), ta, tb2, tl.join(u','));
    win.on_click(owned[ta][0], nomod);
    QApplication::processEvents();
    shown_btn(u"web"_s);
    QApplication::processEvents();
    snap(u"web"_s);
    grab(u"web_nation.png"_s);
    // --- a flag clicked: its ties in the side column
    {
        const diploweb::Web::Layout L = web->layout();
        const std::optional<QString> tf = !L.placed.empty() ? std::optional<QString>(L.placed[0].tag) : std::nullopt;
        out << u"flag "_s + key(tf);
        if (tf) {
            const QPointF p = screen(L.placed[0].pt, web);
            send(web, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
            send(web, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
            QApplication::processEvents();
        }
    }
    snap(u"flag clicked"_s);
    grab(u"web_pick.png"_s);
    dv->tie_box.at(u"ally"_s)->click();
    QApplication::processEvents();
    snap(u"ally ticked"_s);
    dv->pick_rel->setValue(75);
    Q_EMIT dv->pick_rel->valueChanged(75);
    dv->_rel_timer->stop();
    dv->_rel_done();
    QApplication::processEvents();
    snap(u"rel 75"_s);
    if (dv->b && !dv->b->isEmpty()) {
        dv->_tie(*dv->b, u"none"_s, false);
        QApplication::processEvents();
    }
    snap(u"none"_s);
    // --- a flag dragged inside its section, a section dragged round the ring, a hover
    {
        const diploweb::Web::Layout L = web->layout();
        if (!L.placed.empty()) {
            const diploweb::Web::Placed &g = L.placed.size() > 1 ? L.placed[1] : L.placed[0];
            const QPointF p = screen(g.pt, web);
            send(web, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
            for (int i : {1, 2, 3})
                send(web, QEvent::MouseMove, QPointF(p.x() + 10 * i, p.y() + 8 * i), Qt::NoButton, Qt::LeftButton);
            send(web, QEvent::MouseButtonRelease, QPointF(p.x() + 30, p.y() + 24), Qt::LeftButton, Qt::NoButton);
            QApplication::processEvents();
        }
    }
    snap(u"flag dragged"_s);
    out << u"settings %1 | %2"_s.arg(v(setting(dv->_key())), v(setting(dv->_key() + u"/sections"_s)));
    for (const QString &k : {u"friendly"_s, u"hostile"_s}) {   // (more sections: the ring has room to drag one round)
        dv->chip_btn.at(k)->click();
        QApplication::processEvents();
        snap(u"chip "_s + k);
    }
    if (web->_sections.size() >= 2) {
        const diploweb::Web::Sec s0 = web->_sections[0], s1 = web->_sections[1];
        const QPointF mid(700.0, 500.0);
        const double rr = diploweb::Web::R_IN + 12;
        constexpr double DEG = 3.141592653589793 / 180.0;
        auto at = [&](double ang) {
            return screen(QPointF(mid.x() + rr * std::cos(ang * DEG), mid.y() + rr * std::sin(ang * DEG)), web);
        };
        const QPointF p0 = at((s0.a0 + s0.a1) / 2.0);
        send(web, QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton);
        for (double f : {0.25, 0.5, 0.75})
            send(web, QEvent::MouseMove, at(s1.a0 + f * (s1.a1 - s1.a0)), Qt::NoButton, Qt::LeftButton);
        send(web, QEvent::MouseButtonRelease, at(s1.a0 + 0.75 * (s1.a1 - s1.a0)), Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    }
    snap(u"section dragged"_s);
    out << u"settings "_s + v(setting(dv->_key() + u"/sections"_s));
    {
        const diploweb::Web::Layout L = web->layout();
        if (!L.placed.empty()) {
            send(web, QEvent::MouseMove, screen(L.placed.back().pt, web), Qt::NoButton, Qt::NoButton);
            QApplication::processEvents();
        }
    }
    out << u"hover %1 | %2"_s.arg(v(web->toolTip())).arg(int(web->cursor().shape()));
    // --- the groups shown, the sorts
    dv->chip_btn.at(u"ally"_s)->click();
    QApplication::processEvents();
    snap(u"chip ally"_s);
    for (const QString &k : {u"name"_s, u"rel"_s, u"size"_s, u"as"_s}) {
        pick(dv->sort_box, k);
        QApplication::processEvents();
        snap(u"sort "_s + k);
    }
    dv->reset_places(true);
    QApplication::processEvents();
    snap(u"laid out anew"_s);
    {
        const QPointF c(web->width() / 2.0 + 40, web->height() / 2.0 - 30);
        QWheelEvent e(c, c, QPoint(0, 0), QPoint(0, 120), Qt::NoButton, nomod, Qt::NoScrollPhase, false);
        QApplication::sendEvent(web, &e);
        QApplication::processEvents();
    }
    snap(u"wheel"_s);
    web->fit();
    // --- the great powers; a double-click on one: its relations
    seg(u"gps"_s);
    QApplication::processEvents();
    snap(u"gps"_s);
    grab(u"web_gps.png"_s);
    for (const auto &[r, t] : web->_hit)
        if (t == tb2) {
            const QPointF p = screen(r.center(), web);
            send(web, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
            send(web, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
            send(web, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
            send(web, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
            QApplication::processEvents();
            break;
        }
    snap(u"gp double-click"_s);
    // --- the groups
    seg(u"groups"_s);
    QApplication::processEvents();
    snap(u"groups"_s);
    grab(u"web_groups.png"_s);
    for (int i = 0; i < dv->glist->count(); ++i) {
        QListWidgetItem *it = dv->glist->item(i);
        if (it->data(Qt::UserRole).isValid() && it->checkState() == Qt::Unchecked) {
            it->setCheckState(Qt::Checked);
            QApplication::processEvents();
            break;
        }
    }
    snap(u"group ticked"_s);
    dv->gsearch->setText(u"sphere"_s);
    QApplication::processEvents();
    snap(u"search"_s);
    dv->gsearch->setText(u""_s);
    QApplication::processEvents();
    if (!gv->_hit.empty()) {
        const QPointF p = screen(gv->_hit[0].first.center(), gv);
        send(gv, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
        send(gv, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents();
    }
    snap(u"group double-click"_s);
    seg(u"groups"_s);
    QApplication::processEvents();
    dv->_tick_none();
    QApplication::processEvents();
    snap(u"tick none"_s);
    seg(u"nation"_s);
    QApplication::processEvents();
    shown_btn(u"map"_s);
    QApplication::processEvents();
    snap(u"map"_s);
    // --- the main window's undo
    out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
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
