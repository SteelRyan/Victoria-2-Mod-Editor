// adjpanel against Python, the C++ side of tools/check_adjpanel.py.
// Usage: med_adjpanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/adjpanel.h"
#include "ui/mapview.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace adjpanel;

static QString hs(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString tg(const std::optional<QVariantList> &t)
{
    if (!t)
        return u"None"_s;
    QStringList parts;
    for (const QVariant &x : *t) {
        if (x.typeId() == QMetaType::QVariantList) {
            QStringList e;
            for (const QVariant &y : x.toList())
                e << QString::number(y.toLongLong());
            parts << e.join(u","_s);
        } else {
            parts << x.toString();
        }
    }
    return parts.join(u"/"_s);
}
static QString tg(const std::optional<PairLL> &p)
{
    return p ? u"%1/%2"_s.arg(p->first).arg(p->second) : u"None"_s;
}
static QString pt(const std::optional<Pt> &p)
{
    return p ? v(p->first) + u","_s + v(p->second) : u"None"_s;
}
static QString shp(const mapview::Shape &s)
{
    QStringList pts;
    for (const QPointF &p : s.pts)
        pts << pt(Pt{p.x(), p.y()});
    const auto [r, g, b] = s.color;
    return u"%1|%2|%3,%4,%5|%6|%7|%8|%9|%10"_s.arg(s.kind, pts.join(u";"_s))
        .arg(r)
        .arg(g)
        .arg(b)
        .arg(s.width ? v(*s.width) : u"None"_s, s.size ? v(*s.size) : u"None"_s, v(s.dash), v(s.halo), tg(s.tag));
}
static QVariantList pair_tag(const QString &k, long long a, long long b)
{
    return {k, QVariantList{a, b}};
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;
    adjacency::AdjGeometry &geo = *win.geo;
    QStringList out, msgs, sigs;
    MapPanel mp(*m, geo);
    mp.view = win.map;
    MapEditHandler &hd = mp.handler;
    auto ids = [](const QList<long long> &l) { return v(std::vector<long long>(l.begin(), l.end())); };
    QObject::connect(&mp, &MapPanel::message, [&](const QString &t) { msgs << t; });
    QObject::connect(&mp, &MapPanel::pickRequested, [&](const QString &k) { sigs << u"pick "_s + k; });
    QObject::connect(&mp, &MapPanel::zoomRequested, [&](double x, double y) { sigs << u"zoom %1 %2"_s.arg(v(x), v(y)); });
    QObject::connect(&mp, &MapPanel::selectRequested, [&](const QList<long long> &a, const QList<long long> &b) {
        sigs << u"select %1 %2"_s.arg(ids(a), ids(b));
    });
    int over = 0;
    QObject::connect(&mp, &MapPanel::overlayChanged, [&] { ++over; });
    QObject::connect(&mp, &MapPanel::editModeChanged, [&](bool on) { sigs << u"mode "_s + v(on); });
    QObject::connect(&mp, &MapPanel::canalsRequested, [&] { sigs << u"canals"_s; });
    mp.setFixedWidth(470);
    auto adj = [&]() -> adjacency::AdjacencyFile & { return doc.map<adjacency::AdjacencyFile>(u"adjacency"_s); };

    // --- the free functions
    for (const auto &[a, b] : std::vector<PairLL>{{2898, 364}, {364, 2898}, {3178, 3313}, {3385, 3068}, {364, 366},
                                                  {1810, 1811}, {623, 253}, {326, 314}, {3315, 3313}, {1563, 1562}}) {
        const auto s = guess_spec(adj(), geo, *m, a, b);
        out << u"guess %1 %2 %3"_s.arg(a).arg(b).arg(
            pyform::list({v(s.a), v(s.b), v(s.type), v(s.through), v(s.data), v(s.comment)}));
    }
    struct L4 {
        Pt p, q;
        double f, w;
    };
    for (const L4 &l : std::vector<L4>{{{10.0, 5.0}, {5030.0, 7.0}, 0.5, 5040}, {{5030.0, 7.0}, {10.0, 5.0}, 0.25, 5040},
                                       {{100.0, 5.0}, {200.0, 9.0}, 0.6, 5040}, {{100.0, 5.0}, {200.0, 9.0}, 0.6, 0},
                                       {{4000.0, 1.0}, {10.0, 2.0}, 0.6, 5040}})
        out << u"lerp "_s + pt(lerp(l.p, l.q, l.f, l.w));
    for (const auto &pts : std::vector<std::vector<Pt>>{{{10.0, 5.0}, {5030.0, 7.0}, {5000.0, 9.0}},
                                                        {{100.0, 5.0}, {200.0, 9.0}},
                                                        {{5030.0, 7.0}, {10.0, 5.0}, {5035.0, 1.0}, {3.0, 3.0}}}) {
        QStringList parts;
        for (const auto &part : seam_split(pts, 5040)) {
            QStringList ps;
            for (const Pt &p : part)
                ps << pt(p);
            parts << ps.join(u";"_s);
        }
        out << u"seam "_s + parts.join(u" | "_s);
    }

    auto items = [&] {
        QStringList rows;
        for (int i = 0; i < mp.tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = mp.tree->topLevelItem(i);
            QStringList cols;
            for (int c = 0; c < 5; ++c)
                cols << it->text(c);
            rows << u"%1 %2 %3 %4 %5"_s.arg(v(cols), v(it->toolTip(0)), tg(it->data(0, Qt::UserRole).toList()),
                                            it->foreground(0).color().name(), v(it->isSelected()));
        }
        return rows;
    };
    QStringList box_order;
    for (const auto &c : adjacency::CATEGORIES)
        box_order << c.key;
    box_order << u"missing"_s << u"rivermiss"_s << u"stale"_s;
    auto state = [&](const QString &what) {
        adjacency::AdjacencyFile &a = adj();
        std::vector<long long> nc(a.no_coast.begin(), a.no_coast.end());
        std::sort(nc.begin(), nc.end());
        out << u"%1: rows %2 marks %3 no_coast %4 sel_uid %5 sel_missing %6 overlay %7"_s.arg(what)
                   .arg(a.rows.size())
                   .arg(a.marks.size())
                   .arg(v(nc), v(mp.sel_uid), tg(mp.sel_missing))
                   .arg(over);
        over = 0;
        out << u"  head %1 %2"_s.arg(v(mp.title->text()), v(mp.sub->text()));
        QStringList sb;
        for (const QString &k : box_order)
            sb << u"%1:%2"_s.arg(mp.show_boxes[k]->text(), v(mp.show_boxes[k]->isChecked()));
        out << u"  show "_s + v(sb);
        out << u"  problems %1 %2"_s.arg(v(mp.problem_note->text()), v(mp.problem_note->isVisibleTo(&mp)));
        out << u"  coast "_s + v(mp.coast_info->text());
        out << u"  coast buttons %1 %2 %3 %4 %5 %6 %7"_s.arg(
            v(mp.gen_all->text()), v(mp.gen_all->isEnabled()), v(mp.gen_sel->text()), v(mp.gen_sel->isEnabled()),
            v(mp.next_miss->isEnabled()), v(mp.rm_stale->text()), v(mp.rm_stale->isEnabled()));
        QStringList texts;
        for (int i = 0; i < mp.river_pick->count(); ++i)
            texts << mp.river_pick->itemText(i);
        out << u"  pick %1 %2 %3"_s.arg(texts.size()).arg(hs(texts.join(u"\n"_s)), v(texts.mid(0, 4)));
        out << u"  rivers %1 %2 %3 %4 %5"_s.arg(v(mp.rivers_all->text()), v(mp.river_all_btn->text()),
                                                v(mp.river_all_btn->isEnabled()), v(mp.find_btn->text()),
                                                v(mp.find_btn->isEnabled()));
        out << u"  river %1 name %2 %3 mark %4 %5 %6 info %7 make %8 %9"_s.arg(
            v(mp.river_state->text()), v(mp.river_name->text()), v(mp.river_name->placeholderText()),
            v(mp.mark_btn->text()), v(mp.mark_btn->isEnabled()), v(mp.unmark_btn->isEnabled()),
            v(mp.river_info->text()), v(mp.river_btn->text()), v(mp.river_btn->isEnabled()));
        out << u"  nc %1 %2 %3"_s.arg(v(mp.nc_state->text()), v(mp.nc_on_btn->isEnabled()), v(mp.nc_off_btn->isEnabled()));
        out << u"  list %1 %2 %3"_s.arg(v(mp.list_title->text()), v(mp.rm_all_btn->text()), v(mp.rm_all_btn->isEnabled()));
        for (const QString &r : items())
            out << u"    "_s + r;
        out << u"  editor %1 type %2 a %3 %4 b %5 %6 t %7 %8 data %9 comment %10 help %11 warn %12 %13 "
               "buttons %14 %15 %16"_s.arg(v(mp.ed_title->text()), v(mp.ed_type->currentData().toString()),
                                           QString::number(mp.ed_a->value()), v(mp.ed_a->name->text()),
                                           QString::number(mp.ed_b->value()), v(mp.ed_b->name->text()),
                                           QString::number(mp.ed_t->value()), v(mp.ed_t->name->text()),
                                           QString::number(mp.ed_data->value()))
                   .arg(v(mp.ed_comment->text()), hs(mp.ed_help->text()), v(mp.ed_warn->text()),
                        v(mp.ed_warn->isVisibleTo(&mp)), v(mp.add_btn->isEnabled()), v(mp.upd_btn->isEnabled()),
                        v(mp.del_btn->isEnabled()));
        QStringList sh;
        for (const auto &s : mp.shapes())
            sh << shp(s);
        QStringList ends4 = sh.mid(0, 2);            // (sh[:2] + sh[-2:])
        ends4 << sh.mid(std::max<qsizetype>(sh.size() - 2, 0));
        out << u"  shapes %1 %2 %3"_s.arg(sh.size()).arg(hs(sh.join(u"\n"_s)), v(ends4));
        out << u"  msgs %1 sigs %2"_s.arg(v(msgs), v(sigs));
        msgs.clear();
        sigs.clear();
    };
    auto refresh = [&](const std::vector<long long> &sel = {}, const std::vector<long long> &sea = {},
                       const std::optional<std::pair<QString, QString>> &header = std::nullopt) {
        mp.refresh(doc, sel, sea, header);
    };
    auto grab = [&](const QString &name) {
        mp.show();
        QApplication::processEvents();
        mp.grab().save(QDir(out_dir).filePath(name));
    };
    auto layer = [&] { win.map->shape_layer->set_shapes(mp.shapes()); };

    refresh({}, {}, std::pair{u"Header"_s, u"sub <b>text</b>"_s});
    state(u"start"_s);
    grab(u"panel_start.png"_s);
    // --- the selection's list
    refresh({364});
    state(u"sel 364"_s);
    grab(u"panel_sel.png"_s);
    mp.tree->topLevelItem(0)->setSelected(true);
    state(u"tree row picked"_s);
    mp.tree->clearSelection();
    const int n = mp.tree->topLevelItemCount();
    mp.tree->topLevelItem(n - 1)->setSelected(true);
    state(u"tree missing picked"_s);
    mp._zoom_item(mp.tree->topLevelItem(0));
    mp._zoom_item(mp.tree->topLevelItem(n - 1));
    out << u"zoom "_s + v(sigs);
    sigs.clear();
    // --- the editor
    refresh({364, 366});
    mp._from_selection();
    state(u"from selection 364 366"_s);
    mp._swap();
    mp.ed_comment->setText(u"  A new strait "_s);
    state(u"swapped"_s);
    mp._add();
    refresh({364, 366});
    state(u"added"_s);
    mp.ed_comment->setText(u"Changed"_s);
    mp.ed_t->set(3178);
    state(u"changed in the editor"_s);
    mp._update();
    refresh({364, 366});
    state(u"saved"_s);
    mp.ed_type->setCurrentIndex(mp.ed_type->findData(u"canal"_s));
    mp.ed_data->setValue(3);
    state(u"canal number used"_s);
    mp.ed_data->setValue(33);
    state(u"canal number free"_s);
    mp._delete();
    refresh({364, 366});
    state(u"deleted"_s);
    mp.select_row(6);
    state(u"row 6 selected"_s);
    mp.select_row(std::nullopt);
    for (const auto &[f, pid] : std::vector<std::pair<QString, long long>>{
             {u"a"_s, 2898}, {u"b"_s, 364}, {u"through"_s, 2928}})
        mp.set_pick(f, pid);
    Q_EMIT mp.ed_a->pickRequested();
    Q_EMIT mp.ed_t->pickRequested();
    state(u"picked into the editor"_s);
    refresh({}, {2898, 2927});
    mp._from_selection();
    state(u"from selection two sea tiles"_s);
    refresh({2442}, {2927});
    mp._from_selection();
    state(u"from selection sea + land"_s);
    refresh({1810});
    mp._from_selection();
    state(u"from selection one"_s);
    // --- map_clicked
    for (const QVariantList &tag : std::vector<QVariantList>{
             {u"row"_s, 4LL}, {u"x"_s, 8LL}, {u"end"_s, 6LL, u"a"_s}, pair_tag(u"missingline"_s, 2898, 364),
             pair_tag(u"missing"_s, 2898, 364), {u"row"_s, 999999LL}, pair_tag(u"rivermiss"_s, 1810, 1811)}) {
        mp.map_clicked(tag);
        out << u"clicked %1: uid %2 missing %3 sigs %4"_s.arg(tg(tag), v(mp.sel_uid), tg(mp.sel_missing), v(sigs));
        sigs.clear();
    }
    // --- coast crossings
    refresh({364});
    mp._generate(true);
    refresh({364});
    state(u"coast for the selection"_s);
    for (int k = 0; k < 3; ++k)
        mp._next_missing();
    state(u"next missing x3"_s);
    mp._remove_stale();
    refresh({364});
    state(u"stale removed"_s);
    {
        mp.edit_btn->setChecked(true);                // (right-click a missing one: added)
        const auto miss = mp.status().missing;
        const auto ce = mp.coast_ends(miss[0].first, miss[0].second);
        layer();
        out << u"rclick missing %1 %2"_s.arg(tg(PairLL{miss[0].first, miss[0].second}),
                                              v(hd.rclick(ce->first.first, ce->first.second, 2.0, Qt::NoModifier)));
        mp.edit_btn->setChecked(false);
    }
    refresh({364});
    state(u"missing added"_s);
    mp._generate(false);
    refresh({364});
    state(u"all coast added"_s);
    // --- meme rivers
    refresh({}, {3420});
    state(u"river tile"_s);
    mp.river_gap->setValue(3.0);
    state(u"gap 3"_s);
    mp.river_name->setText(u"Test River"_s);
    state(u"named"_s);
    mp._mark_river();
    refresh({}, {3420});
    state(u"marked"_s);
    mp.river_name->setText(u"Renamed River"_s);
    state(u"renaming"_s);
    mp._mark_river();
    refresh({}, {3420});
    state(u"renamed"_s);
    mp._make_river();
    refresh({}, {3420});
    state(u"crossings made"_s);
    grab(u"panel_river.png"_s);
    mp._unmark_river();
    refresh({}, {3420, 3418});
    state(u"unmarked, two tiles"_s);
    mp._find_rivers();
    refresh({}, {3420, 3418});
    state(u"rivers found"_s);
    {
        mp.edit_btn->setChecked(true);                // (right-click a missing bank crossing: added)
        mp.shapes();
        // (Python's dict keeps the order the lines were made in: river by river, pair by pair)
        std::vector<std::pair<PairLL, QString>> rm;
        for (const auto &[name, st] : mp.river_stat())
            for (const auto &k : st.missing)
                if (mp._rivermiss.count(k))
                    rm.emplace_back(k, name);
        QStringList first;
        for (size_t i = 0; i < std::min<size_t>(4, rm.size()); ++i)
            first << tg(rm[i].first) + u" "_s + rm[i].second;
        out << u"rivermiss %1 %2"_s.arg(rm.size()).arg(v(first));
        const auto pe = mp.pair_ends(rm[0].first.first, rm[0].first.second);
        const Pt mid{(pe->first.first + pe->second.first) / 2, (pe->first.second + pe->second.second) / 2};
        layer();
        out << u"rclick rivermiss %1 %2"_s.arg(pt(mid), v(hd.rclick(mid.first, mid.second, 2.0, Qt::NoModifier)));
        mp.edit_btn->setChecked(false);
    }
    refresh({}, {3420, 3418});
    state(u"river crossing added"_s);
    mp._add_all_river_missing();
    refresh({}, {3420, 3418});
    state(u"all river missing added"_s);
    refresh({}, {3066});
    mp._make_river();
    refresh({}, {3066});
    state(u"thin tile river made"_s);
    // --- no-coast
    refresh({}, {3066, 3312});
    mp._set_no_coast(true);
    refresh({}, {3066, 3312});
    state(u"no-coast on"_s);
    mp._set_no_coast(false);
    refresh({}, {2971, 3312});
    state(u"no-coast off, mixed"_s);
    mp._pick_river_tile(0);
    mp._pick_river_tile(1);
    mp._pick_river_tile(3);
    out << u"river pick "_s + v(sigs);
    sigs.clear();
    // --- show boxes
    refresh({364});
    mp.show_boxes[u"stale"_s]->setChecked(true);
    mp.show_boxes[u"coast"_s]->setChecked(false);
    state(u"stale on, coast off"_s);
    mp.only_sel->setChecked(true);
    state(u"only the selection"_s);
    mp.only_sel->setChecked(false);
    mp.show_boxes[u"coast"_s]->setChecked(true);
    mp.show_boxes[u"missing"_s]->setChecked(false);
    mp.sel_missing.reset();
    state(u"missing off"_s);
    mp.show_boxes[u"missing"_s]->setChecked(true);
    // --- the edit handler
    mp.edit_btn->setChecked(true);
    refresh({364, 366});
    state(u"edit on"_s);
    {
        adjacency::AdjacencyFile &a = adj();
        auto e4 = *mp.ends(*a.get(4));
        out << u"ends 4 %1 %2"_s.arg(pt(e4.a), pt(e4.b));
        for (long long uid : {6LL, 8LL, 569LL, 579LL, 1LL}) {
            const adjacency::Row *r = a.get(uid);
            const auto e = r ? mp.ends(*r) : std::nullopt;
            out << u"ends %1 %2"_s.arg(uid).arg(!e ? u"None"_s : pt(e->a) + u" "_s + pt(e->b));
        }
        QStringList ce, pe;
        if (const auto c = mp.coast_ends(2898, 364))
            ce << pt(c->first) << pt(c->second);
        if (const auto p = mp.pair_ends(364, 366))
            pe << pt(p->first) << pt(p->second);
        out << u"coast_ends %1 pair_ends %2"_s.arg(v(ce), v(pe));
        const Pt i326 = *geo.inner(326);
        layer();
        out << u"hit "_s + tg(win.map->shape_layer->hit(e4.a.first, e4.a.second, 2.0 * 1.3, QStringList{u"end"_s}));
        out << u"press end "_s + v(hd.press(e4.a.first, e4.a.second, 2.0, Qt::NoModifier));
        hd.move(e4.a.first + 3, e4.a.second + 1);
        hd.move(i326.first, i326.second);
        hd.release(i326.first, i326.second, 2.0, Qt::NoModifier);
        refresh({364, 366});
        state(u"end dragged to 326"_s);
        layer();
        e4 = *mp.ends(*adj().get(4));
        out << u"press again "_s + v(hd.press(e4.b.first, e4.b.second, 2.0, Qt::NoModifier));
        hd.release(e4.b.first, e4.b.second, 2.0, Qt::NoModifier);
        state(u"end clicked"_s);
        layer();
        out << u"press bad "_s + v(hd.press(e4.b.first, e4.b.second, 2.0, Qt::NoModifier));
        hd.move(e4.b.first + 5, e4.b.second);
        hd.release(e4.b.first + 5, e4.b.second, 2.0, Qt::NoModifier);
        state(u"bad drop"_s);
        out << u"press nothing "_s + v(hd.press(-50.0, -50.0, 2.0, Qt::NoModifier));
        const auto bp = geo.border_point(3315, 3313);
        layer();
        out << u"rclick x %1 %2"_s.arg(pt(bp), bp ? v(hd.rclick(bp->first, bp->second, 2.0, Qt::NoModifier)) : u"-"_s);
        refresh({364, 366});
        state(u"x deleted"_s);
        const Pt i366 = *geo.inner(107);              // (two inland provinces without any crossing)
        const Pt i364 = *geo.inner(106);
        layer();
        // (one call per statement: C++ leaves the order of a call's arguments open, Python goes left to right)
        bool got = hd.rclick(i364.first, i364.second, 2.0, Qt::NoModifier);
        out << u"rclick province %1 start %2"_s.arg(v(got), v(hd.start));
        hd.move(i366.first, i366.second);
        got = hd.press(i366.first, i366.second, 2.0, Qt::NoModifier);
        out << u"finish %1 start %2"_s.arg(v(got), v(hd.start));
        hd.release(i366.first, i366.second, 2.0, Qt::NoModifier);
        refresh({364, 366});
        state(u"new crossing"_s);
        layer();
        out << u"rclick start "_s + v(hd.rclick(i364.first, i364.second, 2.0, Qt::NoModifier));
        got = hd.rclick(i366.first, i366.second, 2.0, Qt::NoModifier);
        out << u"rclick cancel %1 start %2"_s.arg(v(got), v(hd.start));
        out << u"rclick off the map "_s + v(hd.rclick(-50.0, -50.0, 2.0, Qt::NoModifier));
        out << u"rclick start "_s + v(hd.rclick(i364.first, i364.second, 2.0, Qt::NoModifier));
        hd.press(i364.first, i364.second, 2.0, Qt::NoModifier);
        const bool c1 = hd.cancel();
        const bool c2 = hd.cancel();
        out << u"cancel %1 %2"_s.arg(v(c1), v(c2));
        state(u"cancelled"_s);
    }
    // --- describe
    for (const QVariantList &tag : std::vector<QVariantList>{
             {u"row"_s, 4LL}, {u"row"_s, 2LL}, {u"row"_s, 30LL}, {u"x"_s, 10LL}, {u"end"_s, 6LL, u"a"_s},
             pair_tag(u"rivermiss"_s, 1810, 1811), pair_tag(u"missing"_s, 2898, 364),
             pair_tag(u"missingline"_s, 2898, 364), pair_tag(u"preview"_s, 1810, 1811), {u"other"_s, 1LL},
             {u"row"_s, 999999LL}})
        out << u"describe %1 %2"_s.arg(tg(tag), v(mp.describe(tag)));
    mp.edit_btn->setChecked(false);
    for (const QVariantList &tag : std::vector<QVariantList>{{u"x"_s, 10LL}, pair_tag(u"missing"_s, 2898, 364)})
        out << u"describe %1 %2"_s.arg(tg(tag), v(mp.describe(tag)));
    // --- remove all of the selection
    refresh({364});
    mp._remove_all_here();
    refresh({364});
    state(u"all of 364 removed"_s);
    refresh({364, 2898}, {3178});
    mp._remove_all_here();
    refresh({364, 2898}, {3178});
    state(u"all of three removed"_s);
    // --- undo to the start
    for (int k = 0; k < 60; ++k) {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
        if (!u)
            break;
    }
    refresh();
    state(u"after undo"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%d lines\n", int(out.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
