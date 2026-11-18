// regionimport against Python, the C++ side of tools/check_regionimport.py.
// Usage: med_regionimport_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/newprov.h"
#include "ui/regionimport.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QWheelEvent>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace regionimport;

static QString hs(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString hs(const QString &s) { return hs(s.toUtf8()); }
template <typename T>
static QString L(const T &seq)
{
    QStringList p;
    for (const auto &x : seq) {
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, QString>)
            p << x;
        else
            p << QString::number(x);
    }
    return p.join(u","_s);
}
template <typename T>
static QString ha_bytes(const std::vector<T> &a)
{
    return u"%1:%2"_s.arg(a.size()).arg(hs(QByteArray(reinterpret_cast<const char *>(a.data()),
                                                     qsizetype(a.size() * sizeof(T)))));
}
template <typename T>
static QString ha64(const std::optional<std::vector<T>> &a)
{
    if (!a)
        return u"None"_s;
    std::vector<std::int64_t> w(a->begin(), a->end());
    return ha_bytes(w);
}
static QString ha8(const std::optional<std::vector<std::uint8_t>> &a) { return a ? ha_bytes(*a) : u"None"_s; }
static QString opt(const std::optional<QString> &s) { return s ? *s : u"None"_s; }
static QString opt(const std::optional<long long> &s) { return s ? QString::number(*s) : u"None"_s; }

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList out;
    // --- neighbour_fill
    auto grid_of = [](long long h, long long w, const std::vector<int> &vals) {
        grid::Grid<std::uint16_t> g(h, w, 0);
        for (size_t i = 0; i < vals.size(); ++i)
            g.flat()[i] = std::uint16_t(vals[i]);
        return g;
    };
    auto mask_of = [](const grid::Grid<std::uint16_t> &g, int val) {
        grid::Grid<std::uint8_t> m(g.h(), g.w(), 0);
        for (size_t i = 0; i < g.flat().size(); ++i)
            m.flat()[i] = g.flat()[i] == val;
        return m;
    };
    {
        auto a = grid_of(4, 5, {1, 1, 1, 2, 2, 1, 9, 9, 9, 2, 1, 9, 9, 9, 2, 3, 3, 3, 2, 2});
        const auto f = neighbour_fill(a, mask_of(a, 9));
        out << u"fill %1 | %2"_s.arg(L(a.flat()), L(f));
        auto b = grid_of(4, 4, {5, 5, 5, 5, 5, 7, 7, 5, 5, 7, 8, 5, 5, 5, 5, 5});
        std::vector<std::uint8_t> avoid(10, 0);
        avoid[5] = 1;
        const auto f2 = neighbour_fill(b, mask_of(b, 7), &avoid);
        out << u"fill avoid %1 | %2"_s.arg(L(b.flat()), L(f2));
        auto c = grid_of(2, 3, {4, 4, 4, 4, 4, 4});
        const auto f3 = neighbour_fill(c, mask_of(c, 4));
        out << u"fill all %1 | %2"_s.arg(L(c.flat()), L(f3));
        auto d = grid_of(1, 3, {1, 2, 3});
        const auto f4 = neighbour_fill(d, mask_of(d, 99));
        out << u"fill none %1 | %2"_s.arg(L(d.flat()), L(f4));
    }
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;
    auto pstate = [&doc](long long p) -> const history::ProvState & { return doc.prov_state(p); };
    {
        const auto lut = owner_lut(*m, pstate, md.n);
        const auto lut2 = owner_lut(*m, pstate, md.n, false);
        out << u"owner_lut %1 %2 | by own %3"_s.arg(lut.size()).arg(ha_bytes(lut).section(u':', 1),
                                                                     ha_bytes(lut2).section(u':', 1));
    }

    RegionImportPage P(*m);
    P.landsea = win.landsea.get();
    QStringList msgs;
    QObject::connect(&P, &RegionImportPage::message, [&](const QString &t) { msgs << t; });
    P.resize(1500, 900);
    P.show();
    QApplication::processEvents();
    SourceMap *mp = P.map;

    auto state = [&](const QString &what) {
        out << u"== "_s + what;
        out << u"sel %1 | warn %2 %3 | go %4 %5"_s.arg(P.sel_lbl->text(), v(P.warn->isVisibleTo(&P)), P.warn->text(),
                                                       v(P.go->isEnabled()), P.go->text());
        out << u"result "_s + P.result->text();
        out << u"nat %1 | where %2"_s.arg(P.nat_note->text(), P.where_note->text());
        QStringList bx, nb;
        for (const auto &[k, c] : P.box)
            bx << u"%1=%2/%3"_s.arg(k, v(c->isChecked()), v(c->isEnabled()));
        for (const auto &[k, b] : P.nat_btn)
            nb << u"%1=%2/%3"_s.arg(k, v(b->isChecked()), v(b->isEnabled()));
        out << u"keys %1 | hover %2 | match %3 | boxes %4 | nat btns %5"_s.arg(
            P.keys->text(), P.hover->text(), v(P.match_host->isVisibleTo(&P)), bx.join(u','), nb.join(u','));
        long long marks = 0;
        if (mp->mark)
            for (std::uint8_t x : mp->mark->flat())
                marks += x;
        QStringList ex;
        for (const auto &[x, y, t] : mp->extra)
            ex << u"%1,%2,%3"_s.arg(v(x), v(y), t);
        out << u"map movable %1 picked %2 theirs %3 mark %4 | scale %5 ox %6 oy %7 | extra %8"_s.arg(
            v(mp->movable), L(mp->picked), P._theirs_picked ? L(*P._theirs_picked) : u"None"_s,
            mp->mark ? QString::number(marks) : u"None"_s, v(mp->scale), v(mp->ox), v(mp->oy), ex.join(u" / "_s));
        out << u"dx %1 dy %2 | timer %3"_s.arg(P.dx->value()).arg(P.dy->value()).arg(v(P._timer->isActive()));
        if (P.plan) {
            const Plan &pl = *P.plan;
            QStringList sk, regs, pairs, heirs, sizes;
            for (const auto &[k, x] : pl.skipped_pops)
                sk << u"%1=%2"_s.arg(k).arg(x);
            for (const auto &[k, x] : pl.new_regions)
                regs << k + u':' + L(x);
            for (const auto &[a, b] : pl.pairs)
                pairs << u"%1>%2"_s.arg(a).arg(b);
            for (const auto &[a, b] : pl.heirs)
                heirs << u"%1>%2"_s.arg(a).arg(b);
            for (const auto &[k, ab] : pl.sizes)
                sizes << u"%1:%2/%3"_s.arg(k).arg(ab.first).arg(ab.second);
            out << u"plan %1 | %2 | out %3 | cov %4 | part %5 | tags %6 / %7 / %8 | skip %9 | goods %10 | regs %11"_s
                       .arg(pl.mode, pl.problem)
                       .arg(pl.outside)
                       .arg(L(pl.covered), L(pl.partly), L(pl.missing_tags), L(pl.kept_tags), L(pl.dropped_tags),
                            sk.join(u','), L(pl.dropped_goods), regs.join(u' '));
            out << u"  idx %1 src %2 new %3 ter %4 riv %5"_s.arg(
                ha64(pl.idx), pl.idx ? ha64(std::optional<std::vector<std::uint16_t>>(pl.src_pid)) : u"None"_s,
                pl.idx ? ha64(std::optional<std::vector<std::int64_t>>(pl.nw)) : u"None"_s, ha8(pl.terrain),
                ha8(pl.rivers));
            out << u"  pairs %1 | none %2 | fill %3 %4 | heirs %5 | sizes %6"_s.arg(
                pairs.join(u','), L(pl.no_target), ha64(pl.fill_idx),
                pl.fill_idx ? ha64(std::optional<std::vector<std::int64_t>>(pl.fill_new)) : u"None"_s, heirs.join(u','),
                sizes.join(u','));
            for (const Prov &p : pl.provs) {
                const newprov::NewProv &r = p.rec;
                const auto [cr, cg, cb] = r.rgb;
                QString info;
                if (!p.sea)
                    info = u"controller=%1 cores=%2 life_rating=%3 owner=%4 trade_goods=%5"_s.arg(
                        opt(r.info.controller), L(r.info.cores), opt(r.info.life_rating), opt(r.info.owner),
                        opt(r.info.trade_goods));
                QString entry = u"None"_s;
                if (p.entry) {
                    QStringList e;
                    for (const auto &[k, xy] : p.entry->pts)
                        e << u"%1:%2,%3"_s.arg(k, v(xy.first), v(xy.second));
                    entry = e.join(u' ');
                }
                out << u"  prov %1>%2 %3 sea %4 rgb %5,%6,%7 | %8 | %9 | info %10 | pops %11 skip %12 | under %13 px %14 "
                       u"reg %15 | %16"_s.arg(p.src)
                           .arg(p.pid)
                           .arg(p.name, v(p.sea))
                           .arg(cr)
                           .arg(cg)
                           .arg(cb)
                           .arg(opt(r.hist_rel), r.hist_text ? hs(*r.hist_text) : u"None"_s, info)
                           .arg(p.pops.size())
                           .arg(p.skipped)
                           .arg(p.under)
                           .arg(p.px)
                           .arg(opt(p.region), entry);
            }
        }
    };
    auto grab = [&](const QString &name) {
        QApplication::processEvents();
        P.grab().save(QDir(out_dir).filePath(name));
        out << u"grab %1 labelled %2"_s.arg(name).arg(mp->labelled);
    };
    auto first_px = [](const grid::Grid<std::uint16_t> &ids, long long pid) {
        for (long long y = 0; y < ids.h(); ++y)
            for (long long x = 0; x < ids.w(); ++x)
                if (ids(y, x) == pid)
                    return std::pair<double, double>(double(x), double(y));
        return std::pair<double, double>(-1, -1);
    };
    auto scr = [&](double x, double y) { return QPointF((x + 0.5 - mp->ox) * mp->scale, (y + 0.5 - mp->oy) * mp->scale); };
    auto press = [&](const QPointF &pos, Qt::KeyboardModifiers mods = Qt::NoModifier,
                     Qt::MouseButton button = Qt::LeftButton) {
        QMouseEvent ev(QEvent::MouseButtonPress, pos, pos, button, button, mods);
        mp->mousePressEvent(&ev);
    };
    auto move = [&](const QPointF &pos, Qt::KeyboardModifiers mods = Qt::NoModifier,
                    Qt::MouseButtons buttons = Qt::LeftButton) {
        QMouseEvent ev(QEvent::MouseMove, pos, pos, Qt::NoButton, buttons, mods);
        mp->mouseMoveEvent(&ev);
    };
    auto release = [&](const QPointF &pos, Qt::MouseButton button = Qt::LeftButton) {
        QMouseEvent ev(QEvent::MouseButtonRelease, pos, pos, button, Qt::NoButton, Qt::NoModifier);
        mp->mouseReleaseEvent(&ev);
    };
    auto settle = [&](const QString &what) {
        out << u"timer "_s + v(P._timer->isActive());
        P._timer->stop();
        P._replan();
        state(what);
    };
    auto set_picked = [&](const std::vector<long long> &a, std::optional<long long> extra = std::nullopt) {
        mp->picked = std::set<long long>(a.begin(), a.end());
        if (extra)
            mp->picked.insert(*extra);
    };

    // --- HPM as the other mod
    state(u"no source"_s);
    importer::Source src(pypath::join(work, {u"study mods"_s, u"HPM"_s}), BASE);
    P.set_source(&doc, &md, &src, nullptr);
    RegionSource *rs = P.rs.get();
    state(u"HPM set"_s);
    grab(u"page_start.png"_s);
    long long sea = -1;
    if (const auto *adj = rs->md->adjacent.get(303))
        for (long long q : adj->items())
            if (const moddata::Province *pr = rs->mod.provinces.get(q); pr && pr->sea && (sea < 0 || q < sea))
                sea = q;
    auto shape = [&](const QString &name) {
        const auto *g = rs->layer(name);
        return g ? u"(%1, %2)"_s.arg(g->h()).arg(g->w()) : u"None"_s;
    };
    out << u"rs %1 %2x%3 n %4 | sea next to 303: %5 | positions %6 | terrain %7 rivers %8"_s.arg(src.name)
               .arg(rs->md->w)
               .arg(rs->md->h)
               .arg(rs->md->n)
               .arg(sea)
               .arg(rs->positions().size())
               .arg(shape(u"terrain"_s), shape(u"rivers"_s));
    for (long long pid : {300LL, 301LL, 305LL, 1LL, 2600LL, sea, 9999LL}) {
        const history::ProvState st = rs->state(pid);
        out << u"rs %1 %2 owner %3 ctrl %4 cores %5 | state %6 | owner's %7 | cont %8 | pops %9"_s.arg(pid)
                   .arg(rs->name(pid), opt(st.owner), opt(st.controller), L(st.cores), L(rs->of_state(pid)))
                   .arg(rs->of_owner(pid).size())
                   .arg(rs->of_continent(pid).size())
                   .arg(rs->pops(pid).size());
    }
    for (const auto &[pid, mods] : std::vector<std::pair<long long, Qt::KeyboardModifiers>>{
             {300, Qt::NoModifier}, {300, Qt::ShiftModifier}, {300, Qt::AltModifier}, {sea, Qt::ShiftModifier},
             {300, Qt::AltModifier | Qt::ShiftModifier}}) {
        const auto g = P._group(pid, mods);
        out << u"group %1 %2: %3 %4"_s.arg(pid).arg(mods.toInt()).arg(g.size()).arg(hs(L(g)));
    }
    {
        long long x0 = rs->md->w, y0 = rs->md->h, x1 = -1, y1 = -1;
        for (long long y = 0; y < rs->md->h; ++y)
            for (long long x = 0; x < rs->md->w; ++x)
                if (rs->md->idmap(y, x) == 300) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
        mp->show_box(double(x0), double(y0), double(x1 + 1), double(y1 + 1));
    }
    const auto [x300, y300] = first_px(rs->md->idmap, 300);
    const auto [x302, y302] = first_px(rs->md->idmap, 302);
    const auto [x301, y301] = first_px(rs->md->idmap, 301);
    const auto [xs_, ys_] = first_px(rs->md->idmap, sea);
    out << u"at 300 %1 302 %2 301 %3 sea %4"_s.arg(mp->at(scr(x300, y300)))
               .arg(mp->at(scr(x302, y302)))
               .arg(mp->at(scr(x301, y301)))
               .arg(mp->at(scr(xs_, ys_)));
    press(scr(x300, y300), Qt::ShiftModifier);
    release(scr(x300, y300));
    settle(u"shift-click: the state"_s);
    grab(u"page_picked.png"_s);
    press(scr(xs_, ys_), Qt::ControlModifier);
    release(scr(xs_, ys_));
    press(scr(x301, y301), Qt::ControlModifier);
    move(scr(x301 + 0.3, y301), Qt::ControlModifier);
    release(scr(x301, y301));
    settle(u"ctrl: sea added, 301 off"_s);
    press(scr(x300, y300));
    move(scr(x300 + 2, y300));
    move(scr(x302, y302));
    release(scr(x302, y302));
    settle(u"a drag from 300 to 302"_s);
    press(QPointF(-50.0, -50.0));
    release(QPointF(-50.0, -50.0));
    settle(u"a click off the map"_s);
    move(scr(x300, y300), Qt::NoModifier, Qt::NoButton);
    out << u"hover "_s + P.hover->text();
    {
        QWheelEvent we(QPointF(300.0, 200.0), QPointF(300.0, 200.0), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        mp->wheelEvent(&we);
    }
    out << u"wheel scale %1 ox %2 oy %3"_s.arg(v(mp->scale), v(mp->ox), v(mp->oy));
    press(QPointF(400.0, 300.0), Qt::NoModifier, Qt::RightButton);
    move(QPointF(430.0, 280.0), Qt::NoModifier, Qt::RightButton);
    release(QPointF(430.0, 280.0), Qt::RightButton);
    out << u"pan scale %1 ox %2 oy %3"_s.arg(v(mp->scale), v(mp->ox), v(mp->oy));
    // the state + a sea tile, the countries
    set_picked(rs->of_state(300), sea);
    P._replan();
    state(u"state + sea"_s);
    for (const QString &k : {u"take"_s, u"leave"_s, u"have"_s}) {
        P.nat_btn[k]->click();
        state(u"countries: "_s + k);
    }
    P.dx->setValue(2400);
    state(u"moved 2400 right: partly off the map"_s);
    P.dx->setValue(0);
    P.dy->setValue(-5000);
    state(u"moved off the map"_s);
    P.dy->setValue(0);
    for (const QString &k : {u"pops"_s, u"layers"_s, u"states"_s, u"positions"_s, u"replace"_s}) {
        P.box[k]->setChecked(false);
        state(u"without "_s + k);
        P.box[k]->setChecked(true);
    }
    // this mod's map
    P.view_btn[u"mine"_s]->click();
    state(u"this mod's map"_s);
    grab(u"page_mine.png"_s);
    P._numbers(false);
    out << u"numbers off: extra %1"_s.arg(mp->extra.size());
    P.num_box->setChecked(true);
    out << u"numbers on: extra %1"_s.arg(mp->extra.size());
    mp->spread_extra = true;
    grab(u"page_mine_spread.png"_s);
    mp->spread_extra = false;
    double lx = -1, ly = -1;
    for (long long y = 0; y < mp->mark->h() && lx < 0; ++y)
        for (long long x = 0; x < mp->mark->w(); ++x)
            if ((*mp->mark)(y, x)) {
                lx = double(x);
                ly = double(y);
                break;
            }
    out << u"lit %1 %2"_s.arg(v(mp->lit(scr(lx, ly))), v(mp->lit(QPointF(2.0, 2.0))));
    press(scr(lx, ly));
    move(scr(lx + 7, ly - 4));
    out << u"moving (%1, %2)"_s.arg(std::get<1>(*mp->_move)).arg(std::get<2>(*mp->_move));
    release(scr(lx + 7, ly - 4));
    state(u"the lit part dragged"_s);
    P._moved(-3, 2);
    state(u"moved by -3, 2"_s);
    P.view_btn[u"theirs"_s]->click();
    state(u"their map again"_s);
    P._add_state();
    state(u"add state"_s);
    P._add_owner();
    state(u"add owner's land"_s);
    P._clear();
    state(u"cleared"_s);
    // the import: England onto Ferrum Mare's largest land province
    auto largest = [&]() {
        std::vector<std::pair<long long, long long>> sizes;
        for (const auto &[q, pr] : m->provinces)
            if (!pr.sea && 0 < md.area[size_t(q)])
                sizes.emplace_back(md.area[size_t(q)], q);
        std::sort(sizes.begin(), sizes.end(), std::greater<>());
        return sizes;
    };
    auto mean_of = [](const grid::Grid<std::uint16_t> &ids, const std::function<bool(std::uint16_t)> &in) {
        // (whole numbers far below 2**53: numpy's sum order gives the same exact total)
        long long sx = 0, sy = 0, n = 0;
        for (long long y = 0; y < ids.h(); ++y)
            for (long long x = 0; x < ids.w(); ++x)
                if (in(ids(y, x))) {
                    sx += x;
                    sy += y;
                    ++n;
                }
        return std::pair<double, double>(double(sx) / double(n), double(sy) / double(n));
    };
    const long long big = largest()[0].second;
    const auto [bmx, bmy] = mean_of(md.idmap, [&](std::uint16_t q) { return q == big; });
    const std::vector<long long> eng = rs->of_state(300);
    const auto [emx, emy] = mean_of(rs->md->idmap, [&](std::uint16_t q) {
        return std::find(eng.begin(), eng.end(), q) != eng.end();
    });
    auto do_import = [&](const QString &what, const QString &grab_name = QString()) {
        set_picked(rs->of_state(300), sea);
        P._replan();
        state(u"to import: "_s + what);
        const long long n0 = m->provinces.size();
        P._import();
        out << u"imported: %1 | msgs %2 | provinces %3 -> %4"_s.arg(P.result->text(), msgs.join(u','))
                   .arg(n0)
                   .arg(m->provinces.size());
        state(u"after the import: "_s + what);
        if (!grab_name.isEmpty())
            grab(grab_name);
        auto &defs = doc.map<newprov::ProvinceDefs>(u"definitions"_s);
        std::vector<long long> added;
        for (const auto &[pid, _r] : defs.added)
            added.push_back(pid);
        std::sort(added.begin(), added.end());
        auto &cont = doc.map<mapfiles::MembershipFile>(u"continent"_s);
        auto &clim = doc.map<mapfiles::MembershipFile>(u"climate"_s);
        auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);
        for (long long pid : added) {
            const newprov::NewProv &r = *defs.added.get(pid);
            const moddata::Province *pr = m->provinces.get(pid);
            const auto [cr, cg, cb] = r.rgb;
            out << u"  new %1 %2 rgb %3,%4,%5 area %6 | done %7 %8 | region %9 | pops %10 %11 | cont %12 clim %13 | pos %14"_s
                       .arg(pid)
                       .arg(r.name)
                       .arg(cr)
                       .arg(cg)
                       .arg(cb)
                       .arg(md.area[size_t(pid)])
                       .arg(opt(r.done_version))
                       .arg(r.done_area)
                       .arg(pr ? opt(pr->region) : u"None"_s)
                       .arg(doc.province_pops(pid).size())
                       .arg(doc.total(pid))
                       .arg(L(cont.of(pid)), L(clim.of(pid)), v(pf.entries.contains(pid)));
        }
        auto &dm = doc.map<mapfiles::DefaultMapFile>(u"default_map"_s);
        QStringList regs, locs;
        for (const auto &[k, r] : m->regions)
            if (k.startsWith(u"ENG"_s) || std::find(r.provinces.begin(), r.provinces.end(), big) != r.provinces.end())
                regs << u"%1:%2"_s.arg(k).arg(r.provinces.size());
        for (const auto &[k, ts] : doc.map<localisation::LocEdits>(u"loc"_s).fresh)
            locs << u"%1=%2"_s.arg(k, ts.first);
        out << u"default map %1 sea %2 | regions %3 | loc %4"_s.arg(opt(dm.max_provinces))
                   .arg(dm.sea.size())
                   .arg(regs.join(u','), locs.join(u','));
        const auto u = doc.undo();
        out << u"undo %1 | provinces %2"_s.arg(opt(u)).arg(m->provinces.size());
        P.view_btn[u"theirs"_s]->click();
        state(u"after undo: "_s + what);
    };
    P.nat_btn[u"take"_s]->click();
    P.dx->setValue(static_cast<long long>(bmx - emx));
    P.dy->setValue(static_cast<long long>(bmy - emy));
    do_import(u"their states, countries taken"_s, u"page_imported.png"_s);
    P.box[u"states"_s]->setChecked(false);
    P.box[u"positions"_s]->setChecked(false);
    P.nat_btn[u"have"_s]->click();
    do_import(u"no states, no places"_s);
    P.box[u"states"_s]->setChecked(true);
    P.box[u"positions"_s]->setChecked(true);

    // --- Ferrum Mare as its own source: shapes only
    importer::Source me(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE);
    P.set_source(&doc, &md, &me, nullptr);
    rs = P.rs.get();
    state(u"Ferrum Mare as the source"_s);
    const long long pid = 357;
    std::vector<long long> nb;
    {
        long long x0 = md.w, y0 = md.h, x1 = -1, y1 = -1;
        for (long long y = 0; y < md.h; ++y)
            for (long long x = 0; x < md.w; ++x)
                if (md.idmap(y, x) == pid) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
        std::set<long long> u;
        for (long long y = y0; y <= y1; ++y)
            for (long long x = x0; x <= x1; ++x)
                u.insert(md.idmap(y, x));
        for (long long q : u)
            if (q && q != pid && !m->provinces.get(q)->sea) {
                nb.push_back(q);
                break;
            }
    }
    std::vector<long long> pick{pid};
    pick.insert(pick.end(), nb.begin(), nb.end());
    set_picked(pick);
    P._replan();
    P.mode_btn[u"borders"_s]->click();
    state(u"shapes only"_s);
    P.dx->setValue(12);
    P.dy->setValue(5);
    state(u"shapes only, moved 12, 5"_s);
    grab(u"page_borders.png"_s);
    P.match_btn[u"colour"_s]->click();
    state(u"by colour"_s);
    P.clear_box->setChecked(false);
    state(u"old land stays"_s);
    P.clear_box->setChecked(true);
    P.match_btn[u"number"_s]->click();
    P.view_btn[u"mine"_s]->click();
    state(u"shapes only on this mod's map"_s);
    {
        const grid::Grid<std::uint16_t> old = md.idmap;
        P._import();
        long long changed = 0;
        for (size_t i = 0; i < old.flat().size(); ++i)
            changed += old.flat()[i] != md.idmap.flat()[i];
        out << u"imported: %1 | changed px %2"_s.arg(P.result->text()).arg(changed);
        state(u"after the shapes"_s);
        const auto u = doc.undo();
        out << u"undo %1 | same %2"_s.arg(opt(u), v(old == md.idmap));
    }
    // a large one over a small one: swallowed
    P.view_btn[u"theirs"_s]->click();
    {
        const auto sizes = largest();
        const long long bg = sizes[0].second;
        long long small = 0;
        for (auto it = sizes.rbegin(); it != sizes.rend(); ++it)
            if (it->first > 20) {
                small = it->second;
                break;
            }
        const auto [bx, by] = mean_of(md.idmap, [&](std::uint16_t q) { return q == bg; });
        const auto [sx, sy] = mean_of(md.idmap, [&](std::uint16_t q) { return q == small; });
        mp->picked = {bg};
        P.dx->setValue(static_cast<long long>(sx - bx));
        P.dy->setValue(static_cast<long long>(sy - by));
        state(u"large #%1 over small #%2"_s.arg(bg).arg(small));
        P._import();
        out << u"imported: %1 | provinces %2"_s.arg(P.result->text()).arg(m->provinces.size());
        const auto u = doc.undo();
        out << u"undo %1 | provinces %2"_s.arg(opt(u)).arg(m->provinces.size());
    }
    // as new provinces, at the same place
    P.view_btn[u"theirs"_s]->click();
    P.mode_btn[u"new"_s]->click();
    P.dx->setValue(0);
    P.dy->setValue(0);
    set_picked(pick);
    P._replan();
    state(u"new provinces from itself"_s);
    P._import();
    out << u"imported: %1 | provinces %2"_s.arg(P.result->text()).arg(m->provinces.size());
    {
        const auto u = doc.undo();
        out << u"undo %1 | provinces %2"_s.arg(opt(u)).arg(m->provinces.size());
    }
    P.set_source(&doc, &md, nullptr, nullptr);
    state(u"no source again"_s);

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
