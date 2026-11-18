// The main window's ways around against Python, the C++ side of tools/check_mainnav.py: the search box, the problem
// line (next_problem, "Fix it… (Errors)"), jump_to_problem for every category, the files-changed bar.
// Usage: med_mainnav_check --run <mod> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/recovery.h"
#include "map/layerbmp.h"
#include "map/mapcheck.h"
#include "map/provdelete.h"
#include "map/mapview_data.h"
#include "ui/changelog.h"
#include "ui/checkpanel.h"
#include "ui/colorwheel.h"
#include "ui/entryview.h"
#include "ui/iconboard.h"
#include "ui/maplook.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHelpEvent>
#include <QImage>
#include <QInputDialog>
#include <QPixmap>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <set>

using namespace Qt::StringLiterals;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString r(double x) { return py::repr_float(x); }
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }
static QString opt(const std::optional<QString> &x) { return x ? *x : u"None"_s; }
static QString join(const std::vector<long long> &l)
{
    QStringList s;
    for (long long x : l)
        s << QString::number(x);
    return s.join(u',');
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
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));   // (as the Python side)
    auto quiet = [&] {
        win._check_timer->stop();
        win._rec_timer->stop();
        win._scan_timer->stop();
    };
    quiet();
    win.resize(1500, 900);
    win.show();
    win.map->setFixedSize(1000, 700);
    QApplication::processEvents();
    quiet();
    win.map->fit();
    QApplication::processEvents();

    auto mark = [&] {
        if (!win._jump_mark)
            return u"None"_s;
        const auto &j = *win._jump_mark;
        return u"%1,%2 r %3 rgb %4,%5,%6"_s.arg(r(j.pt.x()), r(j.pt.y()), r(j.r))
            .arg(std::get<0>(j.rgb)).arg(std::get<1>(j.rgb)).arg(std::get<2>(j.rgb));
    };
    auto st = [&](const QString &tag) {
        QApplication::processEvents();
        quiet();
        out << u"%1 | tab %2 page %3 sub %4 | sel %5 sea %6 | msg %7 | m11 %8 | search %9"_s.arg(
            tag, win.tab, opt(win.rail_page()), win.map_sub, join(win.selection), join(win.sea_selection),
            win.st_msg->text(), r(win.map->transform().m11()), win.search->text());
    };

    // --- the search box
    const QStringList items = qobject_cast<QStringListModel *>(win.completer->model())->stringList();
    {
        QStringList shown = items.mid(0, 4);
        shown += items.mid(items.size() - 3);
        out << u"index %1 | %2"_s.arg(items.size()).arg(shown.join(u" ¦ "_s));
        out << u"index hash %1"_s.arg(h(items.join(u'\n').toUtf8()));
    }
    std::vector<long long> land;
    for (const auto &[p, pr] : m->provinces)
        if (!pr.sea)
            land.push_back(p);
    std::sort(land.begin(), land.end());
    const long long p0 = land[land.size() / 3];
    std::vector<QString> rids;
    for (const auto &[rid, _r] : m->regions)
        rids.push_back(rid);
    std::sort(rids.begin(), rids.end());
    const QString rid = rids[5];
    std::set<QString> owners;
    for (const auto &[p, pr] : m->provinces)
        if (pr.owner && !pr.owner->isEmpty())
            owners.insert(*pr.owner);
    const QString tag = *std::next(owners.begin(), 3);
    QString part = py::strip(m->provinces.get(land[7])->name).mid(1, 4).toLower();
    if (part.isEmpty())
        part = u"zz"_s;
    for (const QString &q : {items[0], QString::number(p0), rid, tag.toLower(), part, u"  "_s, u"qqqqzzzz"_s,
                             items[items.size() - 1], items[items.size() / 2]}) {
        win.run_search(q);
        st(u"search "_s + py::repr_str(q));
    }
    win.search->setText(QString::number(land[2]));
    Q_EMIT win.search->returnPressed();
    st(u"search return"_s);
    win.set_selection({});
    // --- the problems: the Errors page checked, the problem line on each page with problems
    win.set_tab(u"errors"_s);
    quiet();
    win._checks_due();
    st(u"checked"_s);
    {
        QStringList ps;
        for (const auto &[k, x] : win._page_problems)
            ps << u"%1:%2"_s.arg(k).arg(x.size());
        out << u"pages "_s + ps.join(u' ');
        // (C++ only, 2026-11-08) the problems of the groups Python doesn't have, per page and severity — the check takes
        // them out of the counts it compares, and this line out of the comparison
        QStringList co;
        for (const auto &[k, x] : win._page_problems) {
            std::map<QString, long long> n;
            for (const auto &pp : x)
                if (pp.key == u"crash_production" || pp.key == u"crash_countries" || pp.key == u"pos_offmap" ||
                    pp.key == u"colormap_size" || pp.key == u"defpath" || pp.key == u"script_prov" ||
                    pp.key == u"flags_base")
                    ++n[pp.sev];
            for (const auto &[sev, c] : n)
                co << u"%1:%2:%3"_s.arg(k, sev).arg(c);
        }
        out << u"cpp_only "_s + co.join(u' ');
    }
    std::vector<QString> pages;
    for (const auto &[k, _x] : win._page_problems)
        pages.push_back(k);
    for (size_t i = 0; i < pages.size() && i < 8; ++i)
        for (int n = 0; n < 2; ++n) {
            win.next_problem(pages[i]);
            st(u"next "_s + pages[i]);
            out << u"   strip %1 | %2 | mark %3 | here %4 %5"_s.arg(tf(win.prob_strip->isVisibleTo(&win)),
                                                                   win.prob_text->text(), mark(),
                                                                   tf(win.here_btn->isVisibleTo(&win)),
                                                                   win.here_btn->text());
        }
    win._problem_to_errors();
    st(u"to errors"_s);
    out << u"   picked "_s + win.chk_panel->sel_title->text();
    win.prob_strip->hide();
    // --- jump_to_problem: the first problem of every category, then one without its item
    std::vector<std::pair<QString, std::optional<mapcheck::Item>>> cats;
    for (const auto &c : win.chk_panel->cats)
        cats.emplace_back(c.key, !c.items.empty() ? std::optional<mapcheck::Item>(c.items[0]) : std::nullopt);
    for (const auto &[key, it] : cats) {
        win.set_tab(u"errors"_s);
        quiet();
        win.jump_to_problem(key, it ? &*it : nullptr);
        st(u"jump "_s + key);
        out << u"   mark "_s + mark();
    }
    for (const QString &key : {u"rivers"_s, u"names"_s, u"adj_missing"_s, u"oob"_s, u"diplo"_s, u"cb_lines"_s,
                               u"wars"_s, u"utility_suggest"_s, u"nowhere"_s}) {
        win.set_tab(u"errors"_s);
        quiet();
        win.jump_to_problem(key, nullptr);
        st(u"jump-none "_s + key);
    }
    // --- pending edits: the tooltips worked out on hover, the panel by file, Revert / Apply one part
    auto tip = [&](QWidget *b) {
        QHelpEvent he(QEvent::ToolTip, QPoint(5, 5), b->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(b, &he);
        return b->toolTip();
    };
    auto unit_list = [&] {
        std::vector<QString> us;
        for (const auto &[u, _w] : recovery::units(recovery::picture(*win.doc)))
            us.push_back(u);
        return us;
    };
    auto units = [&] {
        QStringList s;
        for (const auto &[u, w] : recovery::units(recovery::picture(*win.doc)))
            s << u + u'=' + w;
        return s.join(u" ¦ "_s);
    };
    win.set_tab(u"provinces"_s);
    quiet();
    out << u"tips none | %1 | %2 | %3"_s.arg(tip(win.apply_btn), tip(win._undo_btn), tip(win._redo_btn));
    win.on_island(true, {land[5]});
    win.on_name(u"PROV%1"_s.arg(land[11]), u"Checkname"_s, std::nullopt);
    win.on_island(true, {land[30]});
    st(u"edited"_s);
    out << u"pending %1 | %2"_s.arg(win.doc->pending_count()).arg(units());
    out << u"tips | %1 | %2 | %3"_s.arg(tip(win.apply_btn), tip(win._undo_btn), tip(win._redo_btn));
    win.open_pending_panel();
    QWidget *pp = win._pending_panel;
    QApplication::processEvents();
    {
        QStringList labs, btns;
        for (QLabel *w : pp->findChildren<QLabel *>())
            labs << w->text() + u'/' + w->toolTip();
        for (QPushButton *w : pp->findChildren<QPushButton *>())
            btns << w->text();
        out << u"panel %1 %2x%3 | %4 | %5"_s.arg(tf(pp->isVisible())).arg(pp->width()).arg(pp->height())
                   .arg(labs.join(u" ¦ "_s), btns.join(u" ¦ "_s));
    }
    pp->close();
    QString isl;
    for (const QString &u : unit_list())
        if (u.contains(u"island"_s)) {
            isl = u;
            break;
        }
    out << u"revert_only "_s + tf(win.revert_only({isl}));
    st(u"reverted"_s);
    out << u"pending %1 | %2"_s.arg(win.doc->pending_count()).arg(units());
    const std::vector<QString> rest = unit_list();
    out << u"apply_only "_s + (rest.empty() ? u"None"_s : tf(win.apply_only({rest[0]})));
    st(u"applied"_s);
    out << u"pending %1 | %2"_s.arg(win.doc->pending_count()).arg(units());
    // --- the Buildings map mode: its colours, its legend (a line clicked: that kind off / on), the hover line
    win.set_tab(u"provinces"_s);
    win.set_prov_sub(u"provinces"_s);                 // (the utility jump left it on Utility states)
    quiet();
    win.color_box->setCurrentIndex(win.color_box->findData(u"buildings"_s));
    auto bld = [&](const QString &tag) {
        QApplication::processEvents();
        quiet();
        const mapview::ColorTable ct = win.color_table();
        QLabel *lg = win.map->_legend;
        out << u"bld %1 | %2 | table %3 | legend %4 %5"_s.arg(
            tag, win.color_mode,
            h(QByteArray(reinterpret_cast<const char *>(ct.rgb.data()), qsizetype(ct.rgb.size()))),
            lg ? tf(lg->isVisibleTo(win.map)) : u"None"_s, lg ? h(lg->text().toUtf8()) : u"-"_s);
    };
    bld(u"on"_s);
    out << u"   legend "_s + win.map->_legend->text();
    for (const QString k : {u"factories"_s, u"fort"_s, u"naval_base"_s, u"both"_s, u"railroad"_s, u"nowhere"_s}) {
        Q_EMIT win.map->legendToggled(k);
        bld(u"toggled "_s + k);
    }
    for (const QString k : {u"railroad"_s, u"naval_base"_s, u"fort"_s, u"factories"_s})
        Q_EMIT win.map->legendToggled(k);
    bld(u"back"_s);
    {
        std::vector<long long> picks{land[0], land[land.size() / 2]};
        for (const QString key : {u"fort"_s, u"naval_base"_s, u"railroad"_s}) {
            int k = 0;
            for (long long p : land)
                if (k < 2 && win.doc->prov_state(p).level_of(key) > 0) {
                    picks.push_back(p);
                    k += 1;
                }
        }
        int k = 0;
        for (long long p : land) {
            const std::optional<QString> &rg = m->provinces.get(p)->region;
            if (k < 3 && rg && !rg->isEmpty() && !win.doc->state_factories(*rg).empty()) {
                picks.push_back(p);
                k += 1;
            }
        }
        for (long long p : land) {
            const std::optional<QString> &rg = m->provinces.get(p)->region;
            if (!rg || rg->isEmpty()) {
                picks.push_back(p);
                break;
            }
        }
        for (long long p : picks) {
            win.on_hover(p, 10, 10);
            out << u"   hover "_s + QString::number(p) + u" | "_s + win.st_hover->text();
        }
    }
    win.set_tab(u"errors"_s);
    win.set_tab(u"provinces"_s);
    bld(u"tab again"_s);
    // --- the ◐ popup: the colours of the map modes (shared scale, a mode's own scale and ranges, the size filters)
    auto look = [&](const QString &tag) {
        QApplication::processEvents();
        QApplication::processEvents();
        quiet();
        maplook::LookPopup *pop = win.look_pop;
        QStringList ws;
        for (QWidget *w : pop->findChildren<QWidget *>()) {
            if (!w->isVisibleTo(pop))
                continue;
            if (auto *cb = qobject_cast<colorwheel::ColorButton *>(w))
                ws << u"C[%1]"_s.arg(cb->color);
            else if (auto *b = qobject_cast<QPushButton *>(w))
                ws << u"B["_s + b->text() + (b->isChecked() ? u"*"_s : u""_s) + (b->isEnabled() ? u""_s : u"-"_s) + u"]"_s;
            else if (auto *e = qobject_cast<QLineEdit *>(w))
                ws << u"E["_s + e->text() + u"|"_s + e->placeholderText() + (e->isEnabled() ? u""_s : u"-"_s) + u"]"_s;
            else if (auto *l = qobject_cast<QLabel *>(w))
                ws << u"L["_s + l->text() + u"]"_s;
        }
        const QImage img = pop->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        out << u"look "_s + tag + u" | %1 %2x%3 | pic %4x%5 %6 | "_s.arg(tf(pop->isVisible())).arg(pop->width())
                                      .arg(pop->height()).arg(img.width()).arg(img.height())
                                      .arg(h(QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes()))) +
                   ws.join(u' ');
        auto sv = [&](const QString &k) {
            const QVariant v = win.settings->value(k);
            return v.isValid() ? v.toString() : u"None"_s;
        };
        out << u"   settings "_s + sv(u"mapScales"_s) + u" | "_s + sv(u"mapSizeFilter"_s) + u" | "_s + sv(u"mapRamp"_s);
        const mapview::ColorTable ct = win.color_table();
        out << u"   map "_s + h(QByteArray(reinterpret_cast<const char *>(ct.rgb.data()), qsizetype(ct.rgb.size()))) +
                   u" | msg "_s + win.st_msg->text();
    };
    auto vis_buttons = [&] {
        std::vector<QPushButton *> v;
        for (QPushButton *b : win.look_pop->findChildren<QPushButton *>())
            if (b->isVisibleTo(win.look_pop))
                v.push_back(b);
        return v;
    };
    auto vis_edits = [&] {
        std::vector<QLineEdit *> v;
        for (QLineEdit *e : win.look_pop->findChildren<QLineEdit *>())
            if (e->isVisibleTo(win.look_pop))
                v.push_back(e);
        return v;
    };
    auto vis_colours = [&] {
        std::vector<colorwheel::ColorButton *> v;
        for (auto *c : win.look_pop->findChildren<colorwheel::ColorButton *>())
            if (c->isVisibleTo(win.look_pop))
                v.push_back(c);
        return v;
    };
    auto btn = [&](const QString &text) {
        for (QPushButton *b : vis_buttons())
            if (b->text() == text)
                return b;
        throw std::runtime_error(("no button " + text).toStdString());
    };
    auto typed = [&](QLineEdit *ed, const QString &text) {
        ed->setText(text);
        Q_EMIT ed->editingFinished();
    };
    auto look_mode = [&](const QString &key) {
        if (win.look_pop)
            win.look_pop->close();
        win.color_box->setCurrentIndex(win.color_box->findData(key));
        win.st_msg->setText(u""_s);
        win.open_map_look();
        look(u"open "_s + key);
    };
    auto first_x = [&](bool enabled_only) {
        for (QPushButton *b : vis_buttons())
            if (b->text() == u"✕" && (!enabled_only || b->isEnabled()))
                return b;
        throw std::runtime_error("no ✕");
    };
    look_mode(u"political"_s);                       // the shared scale
    btn(u"Blue → red"_s)->click();
    look(u"preset"_s);
    btn(u"⇄ Reverse"_s)->click();
    look(u"reverse"_s);
    btn(u"＋ A colour in between"_s)->click();
    look(u"in between"_s);
    typed(vis_edits()[1], u"40"_s);
    look(u"place 40"_s);
    typed(vis_edits()[1], u"x"_s);
    look(u"place x"_s);
    vis_colours()[0]->set(u"#123456"_s);
    look(u"colour"_s);
    first_x(true)->click();
    look(u"stop out"_s);
    look_mode(u"population"_s);                      // a mode with an amount: smooth / own / ranges
    btn(u"Its own scale"_s)->click();
    look(u"own"_s);
    btn(u"Green → red"_s)->click();
    look(u"own preset"_s);
    btn(u"The shared scale"_s)->click();
    look(u"shared again"_s);
    btn(u"Ranges"_s)->click();
    look(u"ranges"_s);
    {
        const auto eds = vis_edits();
        typed(eds[1], u"30k"_s);
        look(u"to 30k"_s);
        typed(eds[3], u"10"_s);
        look(u"bad range"_s);
        typed(eds[3], u""_s);
        look(u"open end"_s);
    }
    vis_colours()[1]->set(u"#abcdef"_s);
    look(u"range colour"_s);
    btn(u"＋ A range"_s)->click();
    look(u"add range"_s);
    first_x(false)->click();
    look(u"range out"_s);
    btn(u"Ranges from the map"_s)->click();
    look(u"from the map"_s);
    btn(u"↺ The suggested ranges"_s)->click();
    look(u"suggested"_s);
    look_mode(u"liferating"_s);
    btn(u"Ranges"_s)->click();
    look(u"lr ranges"_s);
    look_mode(u"literacy"_s);                        // decimals; its values from the more-modes
    btn(u"Ranges"_s)->click();
    look(u"lit ranges"_s);
    btn(u"Ranges from the map"_s)->click();
    look(u"lit from the map"_s);
    btn(u"＋ A range"_s)->click();
    look(u"lit add"_s);
    typed(vis_edits()[0], u"2,5"_s);
    look(u"lit 2,5"_s);
    look_mode(u"statesize"_s);                       // the size filters
    btn(u"States by provinces"_s)->click();
    look(u"by provinces"_s);
    btn(u"＋ A range"_s)->click();
    look(u"size add"_s);
    typed(vis_edits()[2], u"4"_s);
    look(u"size 4"_s);
    btn(u"Provinces by people"_s)->click();
    look(u"prov people"_s);
    look_mode(u"provsize"_s);
    win.look_pop->close();
    win.color_box->setCurrentIndex(win.color_box->findData(u"political"_s));
    // --- Nation page ▸ national value: Game rules → National values, the value picked on its board
    for (const QString key : {u"nv_equality"_s, u""_s}) {
        win.open_place(u"nv"_s, key);
        QApplication::processEvents();
        quiet();
        auto *v = qobject_cast<iconboard::BoardWithList *>(win.rules_ws->view(u"values"_s));
        out << u"nv %1 | tab %2 page %3 | rules %4 | mode %5 | sel %6 | msg %7"_s.arg(
            u"'"_s + key + u"'"_s, win.tab,
            opt(win.rail_page()), win.rules_ws->page, QString::number(v->stack->currentIndex()), opt(v->board->sel),
            win.st_msg->text());
    }
    win.set_tab(u"provinces"_s);
    quiet();
    // --- scenarios: a new one through the box (its name dialog answered), the unsaved shared edits put back
    {
        const QString scen0 = win.doc->scenario;
        std::vector<long long> with_region;
        for (long long p : land)
            if (const auto &rg = m->provinces.get(p)->region; rg && !rg->isEmpty())
                with_region.push_back(p);
        const long long lr = with_region[40];
        const QString rid_from = *m->provinces.get(lr)->region;
        QStringList others;
        for (const auto &[r, _x] : m->regions)
            if (r != rid_from)
                others << r;
        std::sort(others.begin(), others.end());
        const QString rid_to = others[3], rid_u = others[7];
        win.doc->move_to_state({lr}, rid_to);
        win._util_mark({rid_u}, true);
        const QString lkey = u"PROV%1"_s.arg(land[12]);
        win.on_name(lkey, u"Scenname"_s, std::nullopt);
        auto prov_list = [&](const QString &rid) {
            const moddata::Region *r = m->any_region(rid);
            if (!r)
                return u"-"_s;
            QStringList s;
            for (long long p : r->provinces)
                s << QString::number(p);
            return s.join(u',');
        };
        auto shared = [&](const QString &tag) {
            QApplication::processEvents();
            quiet();
            QStringList box;
            for (int i = 0; i < win.scenario_box->count(); ++i)
                box << win.scenario_box->itemText(i);
            QStringList util;
            for (const auto &[k, _r] : m->utility)
                util << k;
            std::sort(util.begin(), util.end());
            out << u"scen "_s + tag + u" | "_s + win.doc->scenario + u" | box "_s + box.join(u" ¦ "_s) +
                       u" | pending "_s + QString::number(win.doc->pending_count()) + u" | "_s + rid_from + u": "_s +
                       prov_list(rid_from) + u" | "_s + rid_to + u": "_s + prov_list(rid_to) + u" | "_s + rid_u +
                       u": "_s + prov_list(rid_u) + u" | util "_s + util.join(u',') + u" | loc "_s +
                       m->loc.value(lkey, u"-"_s) + u" | msg "_s + win.st_msg->text();
            const mapview::ColorTable ct = win.color_table();
            out << u"   map "_s + h(QByteArray(reinterpret_cast<const char *>(ct.rgb.data()), qsizetype(ct.rgb.size()))) +
                       u" | notice "_s + tf(win.notice->isVisibleTo(&win)) + u" "_s + win.notice->text();
        };
        shared(u"edited"_s);
        QStringList seen;
        auto answer = [&](const QString &text) {
            QTimer::singleShot(0, [&seen, text] {
                QWidget *d = QApplication::activeModalWidget();
                if (auto *dl = qobject_cast<QInputDialog *>(d)) {
                    seen << dl->windowTitle() + u": "_s + dl->labelText();
                    dl->setTextValue(text);
                    dl->accept();
                } else {
                    seen << (d ? d->metaObject()->className() : "None");
                }
            });
        };
        answer(u"1900.1.1"_s);
        Q_EMIT win.scenario_box->activated(win.scenario_box->findData(u"__new__"_s));
        out << u"   dialog "_s + seen.join(u" ¦ "_s);
        shared(u"new, first try"_s);
        Q_EMIT win.scenario_box->activated(win.scenario_box->findData(u"1900.1.1"_s));
        shared(u"switched"_s);
        Q_EMIT win.scenario_box->activated(win.scenario_box->findData(scen0));
        shared(u"back"_s);
        answer(u"1900.1.1"_s);
        Q_EMIT win.scenario_box->activated(win.scenario_box->findData(u"__new__"_s));
        shared(u"same name again"_s);
    }
    // --- the files-changed bar (last: a file of the copy is moved away)
    win.set_tab(u"provinces"_s);
    quiet();
    win._scan_tick();
    out << u"scan first | %1 | %2"_s.arg(tf(win.scan_bar->isVisibleTo(&win)), win.scan_lbl->text());
    {
        QFile f(pypath::join(pypath::join(root, u"events"_s), u"zz_check_added.txt"_s));
        if (f.open(QIODevice::WriteOnly))
            f.write("# added by check_mainnav\n");
    }
    QStringList common = QDir(pypath::join(root, u"common"_s))
                             .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    std::sort(common.begin(), common.end());
    QDir().rename(pypath::join(pypath::join(root, u"common"_s), common[0]), pypath::join(out_dir, common[0]));   // (kept in out)
    win._scan_tick();
    out << u"scan changed | %1 | %2"_s.arg(tf(win.scan_bar->isVisibleTo(&win)), win.scan_lbl->text());
    for (QPushButton *b : win.scan_bar->findChildren<QPushButton *>())
        if (b->text() == u"Not now") {
            b->click();
            break;
        }
    out << u"scan not now | "_s + tf(win.scan_bar->isVisibleTo(&win));
    win._scan_tick();
    out << u"scan again | "_s + tf(win.scan_bar->isVisibleTo(&win));
    // --- a province deleted, then Apply: the numbers are closed up and the mod is read again (last: the window closes)
    {
        std::vector<long long> all_land;
        for (const auto &[p, pr] : m->provinces)
            if (!pr.sea)
                all_land.push_back(p);
        std::sort(all_land.begin(), all_land.end());
        const long long gone_p = all_land[100];
        const long long nd = provdelete::delete_provinces(*win.doc, *win.mapdata, {gone_p}, u"neighbours"_s,
                                                          win.landsea.get());
        out << u"deleted %1: %2"_s.arg(gone_p).arg(nd);
        Q_EMIT win.chk_panel->renumberRequested();
        out << u"close-up early | msg "_s + win.st_msg->text();
        const bool ok = win.save();
        QApplication::processEvents();
        out << u"saved %1 | reload %2 | visible %3 | msg "_s.arg(tf(ok), tf(win.reload_requested), tf(win.isVisible())) +
                   win.st_msg->text();
        out << u"   note "_s + win.reload_note;
        QStringList mv;
        std::vector<std::pair<long long, long long>> kv(win.renumbered.begin(), win.renumbered.end());
        std::sort(kv.begin(), kv.end());
        for (const auto &[a, b] : kv)
            mv << u"%1>%2"_s.arg(a).arg(b);
        out << u"   moved "_s + mv.join(u' ');
        for (const QString rel : {u"map/definition.csv"_s, u"map/region.txt"_s, u"map/adjacencies.csv"_s}) {
            QFile fl(QDir(root).filePath(rel));
            out << u"   "_s + rel + u" "_s + (fl.open(QIODevice::ReadOnly) ? h(fl.readAll()) : u"-"_s);
        }
    }
    QFile f(pypath::join(out_dir, u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 2;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);      // (as the Python side: no close, no recovery file)
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder: never legacy\)
    QApplication::setCursorFlashTime(0);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    std::fprintf(stderr, "usage: med_mainnav_check --run <mod> <backups> <out folder> <work folder>\n");
    return 2;
}
