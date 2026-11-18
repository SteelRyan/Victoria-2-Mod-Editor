// How fast the Drawing window answers (C++ only, the user 2026-11-09: "do a performance update for every single
// window … the drawing window first", cut & paste "painful to work with"). Ferrum Mare in the real main window,
// shown offscreen at 1500 × 900; every step is timed as the call + the events it leaves (the repaint):
//   select · cut · paste · drag (each mouse move) · turn · mirror · OK · undo · brush strokes (provinces, terrain) ·
//   layer switches · zoom steps · the map drawn again whole
// Usage: med_drawperf_check <work folder> [rounds]   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own)
#include "../core/nopopup.h"
#include "../core/sampler.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "map/layerbmp.h"
#include "map/mapcheck.h"
#include "map/provbmp.h"
#include "ui/checkpanel.h"
#include "ui/drawpanel.h"
#include "ui/mapview.h"
#include "ui/terrainpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QScreen>
#include <QWindow>
#include <QPushButton>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

// After every waiting stretch: the masks kept between edits (patched in the changed box only) must equal the ones
// counted afresh — river_masks_whole and landsea_masks_whole against river_problem_masks / landsea_masks.
static QString last_step;
static mainwindow::MainWindow *g_win = nullptr;
static int verify_fails = 0, verify_runs = 0;
static void verify_kept()
{
    if (!g_win)
        return;
    document::Document &doc = *g_win->doc;
    const mapview::MapData &md = *g_win->mapdata;
    layerbmp::IndexedBitmap *tb = nullptr, *rb = nullptr;
    if (const store::StoreP *bp = doc.maps.get(u"rivers_bmp"_s))
        rb = dynamic_cast<layerbmp::IndexedBitmap *>(bp->get());
    if (const store::StoreP *bp = doc.maps.get(u"terrain_bmp"_s))
        tb = dynamic_cast<layerbmp::IndexedBitmap *>(bp->get());
    ++verify_runs;
    if (rb && rb->load()) {
        const auto &kept = mapcheck::river_masks_whole(md, *rb);
        const auto fresh = mapcheck::river_problem_masks(md, *rb->data);
        for (const auto &[k, g] : fresh) {
            const auto *o = kept.get(k);
            if (!o || o->flat() != g.flat()) {
                ++verify_fails;
                std::printf("VERIFY FAIL: river mask %s differs after \"%s\"\n", qUtf8Printable(k), qUtf8Printable(last_step));
            }
        }
    }
    const auto &kept = mapcheck::landsea_masks_whole(*g_win->mod, md, tb, rb);
    const auto fresh = mapcheck::landsea_masks(*g_win->mod, md, tb, rb);
    for (const auto &[k, pr] : fresh) {
        const auto *o = kept.get(k);
        if (!o || o->first.flat() != pr.first.flat() || o->second.flat() != pr.second.flat()) {
            ++verify_fails;
            std::printf("VERIFY FAIL: land / sea mask %s differs after \"%s\"\n", qUtf8Printable(k), qUtf8Printable(last_step));
        }
    }
    if (fresh.size() != kept.size()) {
        ++verify_fails;
        std::printf("VERIFY FAIL: land / sea kinds %zu vs %zu\n", size_t(fresh.size()), size_t(kept.size()));
    }
}

static std::map<QString, std::vector<double>> times;
static std::vector<QString> order;
static prof::Sampler *sampler = nullptr;          // (a step named on the command line is profiled, with its timers)
static QString profile_step;

static void flush()
{
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents();
}

static double timed(const QString &what, const std::function<void()> &fn)
{
    if (sampler && !profile_step.isEmpty() && what.contains(profile_step))
        sampler->on = true;
    QElapsedTimer t;
    t.start();
    fn();
    flush();
    const double ms = double(t.nsecsElapsed()) / 1e6;
    if (!times.count(what))
        order.push_back(what);
    times[what].push_back(ms);
    last_step = what;
    return ms;
}

// waits out the window's own timers (renders, checks scheduled a moment later), timed as "… then idle"
static void idle(const QString & = QString(), int ms = 900)
{
    // the longest single stretch the window was busy (the screen frozen) while its timers ran, and the busy total
    QElapsedTimer t;
    t.start();
    double worst = 0, busy = 0;
    while (t.elapsed() < ms) {
        QElapsedTimer one;
        one.start();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const double d = double(one.nsecsElapsed()) / 1e6;
        if (d > 2.0) {
            busy += d;
            worst = std::max(worst, d);
        }
        QThread::msleep(1);
    }
    if (!last_step.isEmpty() && busy > 0) {
        const QString k = u"   … then timers: longest freeze"_s;
        const QString key = last_step + u" ⟶ later freeze"_s;
        if (!times.count(key))
            order.push_back(key);
        times[key].push_back(worst);
        Q_UNUSED(k);
    }
    if (sampler)
        sampler->on = false;
    verify_kept();
    last_step.clear();
    if (sampler)
        sampler->on = false;
}

// provbmp::small_pieces as it was before 2026-11-14 (a union-find over each piece's window): the new flood fill must
// give the same pieces, the same pixels in the same order and the same neighbour
static std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> small_pieces_ref(const grid::Grid<std::uint16_t> &idm,
                                                                                    long long limit,
                                                                                    std::optional<long long> only_id,
                                                                                    const mapcheck::Pieces &pc)
{
    const long long H = idm.h(), W = idm.w();
    OrderedMap<std::int64_t, long long> per;
    for (std::int64_t p : pc.pid)
        per[p] += 1;
    std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> out;
    for (size_t k = 0; k < pc.pid.size(); ++k) {
        const std::int64_t p = pc.pid[k], s = pc.size[k], rep = pc.first[k];
        if (s >= limit)
            continue;
        if (!only_id) {
            if (p == 0 || per.value(p) < 2)
                continue;
        } else if (p != *only_id) {
            continue;
        }
        const long long y = rep / W, x = rep % W;
        const long long y0 = std::max(0LL, y - limit), y1 = std::min(H, y + limit + 1);
        const long long x0 = std::max(0LL, x - limit), x1 = std::min(W, x + limit + 1);
        grid::Grid<std::uint8_t> sub(y1 - y0, x1 - x0);
        for (long long r = y0; r < y1; ++r)
            for (long long c = x0; c < x1; ++c)
                sub(r - y0, c - x0) = std::int64_t(idm(r, c)) == p ? 1 : 0;
        const auto comp = provbmp::component(sub, y - y0, x - x0);
        std::vector<std::int64_t> ys, xs, idx;
        for (long long r = 0; r < comp.h(); ++r)
            for (long long c = 0; c < comp.w(); ++c)
                if (comp(r, c)) {
                    ys.push_back(r);
                    xs.push_back(c);
                    idx.push_back((r + y0) * W + (c + x0));
                }
        OrderedMap<std::int64_t, long long> nb;
        bool any = false;
        for (const auto &[dy, dx] : {std::pair<int, int>{0, 1}, {0, -1}, {1, 0}, {-1, 0}})
            for (size_t i = 0; i < ys.size(); ++i) {
                const long long yy = ys[i] + y0 + dy, xx = py::mod(xs[i] + x0 + dx, W);
                if (yy < 0 || yy >= H)
                    continue;
                const std::int64_t v = idm.flat()[static_cast<size_t>(yy * W + xx)];
                if (v != p && v != 0) {
                    nb[v] += 1;
                    any = true;
                }
            }
        if (any) {
            std::int64_t best = nb.begin()->first;
            long long bc = nb.begin()->second;
            for (const auto &[v, c] : nb)
                if (c > bc) {
                    best = v;
                    bc = c;
                }
            out.emplace_back(idx, best);
        }
    }
    return out;
}

static int run(const QString &work, int rounds)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    // DRAWPERF_MOD=<folder>: another mod (a COPY — nothing is written, but never the user's own folder)
    const QString other = qEnvironmentVariable("DRAWPERF_MOD");
    auto m = std::make_shared<moddata::Mod>(other.isEmpty() ? pypath::join(work, u"Ferrum_Mare 1.2.000"_s) : other, BASE,
                                            std::nullopt,
                                            other.isEmpty() ? QStringList{u"history"_s, u"events"_s, u"decisions"_s,
                                                                          u"localisation"_s, u"gfx/flags"_s}
                                                            : QStringList{});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, QDir::temp().filePath(u"drawperf_check_backups"_s));
    g_win = &win;
    // DRAWPERF_WIN=WxH (the window), DRAWPERF_BRUSH (px), DRAWPERF_ZOOM — the user's own screen (QT_SCALE_FACTOR for
    // a scaled one)
    const QStringList wh = qEnvironmentVariable("DRAWPERF_WIN", u"1500x900"_s).split(u'x');
    win.resize(wh.value(0).toInt(), wh.value(1).toInt());
    if (const QStringList xy = qEnvironmentVariable("DRAWPERF_POS").split(u','); xy.size() == 2)
        win.move(xy[0].toInt(), xy[1].toInt());      // (a side screen: the real Windows drawing, never the main screen)
    win.setAttribute(Qt::WA_ShowWithoutActivating);  // (never takes the keyboard from the user)
    win.show();
    flush();
    win.set_tab(u"map"_s);
    win.set_map_sub(u"draw"_s);
    idle(u""_s, 1500);
    drawpanel::DrawPanel &dp = *win.draw_panel;
    auto &pf = win.doc->map<positions::PositionsFile>(u"positions"_s);
    // two neighbouring land provinces of a fair size
    long long A = 0, B = 0;
    for (long long y = 300; y < md.h - 300 && !B; y += 7) {
        const std::uint16_t *row = md.idmap.row(y);
        for (long long x = 300; x < md.w - 300 && !B; x += 3) {
            const long long a = row[x], b = row[x + 1];
            if (a && b && a != b && m->provinces.contains(a) && m->provinces.contains(b) && md.area[size_t(a)] > 800 &&
                md.area[size_t(b)] > 800 && md.area[size_t(a)] < 6000 && md.area[size_t(b)] < 6000 &&
                pf.entries.contains(a) && pf.entries.contains(b) && !m->provinces.get(a)->sea && !m->provinces.get(b)->sea) {
                A = a;
                B = b;
            }
        }
    }
    std::printf("provinces %lld (%lld px) and %lld (%lld px), map %lld x %lld\n", A, (long long)md.area[size_t(A)], B,
                (long long)md.area[size_t(B)], md.w, md.h);
    {   // the new small_pieces against the old one (all ids, then id 0), limit 20 and 64
        const mapcheck::Pieces &pc = mapcheck::pieces_of(md);
        for (long long lim : {20LL, 64LL})
            for (const std::optional<long long> only : {std::optional<long long>(), std::optional<long long>(0)}) {
                const auto a = provbmp::small_pieces(md.idmap, lim, only, &pc);
                const auto b = small_pieces_ref(md.idmap, lim, only, pc);
                std::printf("small_pieces limit %lld %s: %zu pieces, %s\n", lim, only ? "id 0" : "all ids", a.size(),
                            a == b ? "identical to the old way" : "DIFFERENT FROM THE OLD WAY");
                if (a != b)
                    ++verify_fails;
            }
    }
    timed(u"drawing on (D)"_s, [&] { win.toggle_draw(); });
    const double zoom = qEnvironmentVariable("DRAWPERF_ZOOM", u"3"_s).toDouble();
    const int brush = qEnvironmentVariableIntValue("DRAWPERF_BRUSH") ? qEnvironmentVariableIntValue("DRAWPERF_BRUSH") : 12;
    win.map->center_on_province(A, zoom);
    idle(u""_s);

    // (the middle of A, read before the cut lifts it off the map: read after it, it was -1, -1 and every stroke went to
    // the map's corner — fixed 2026-11-14)
    const double cx = md.cx[size_t(A)], cy = md.cy[size_t(A)];
    for (int r = 0; r < rounds; ++r) {
        timed(u"select 2 provinces"_s, [&] { win.set_selection({A, B}); });
        idle(u""_s);
        timed(u"Cut & paste tool"_s, [&] { dp.tool_btns.at(u"clip"_s)->setChecked(true); });
        timed(u"cut (Ctrl+X)"_s, [&] { dp.clip.cut(); });
        idle(u""_s);
        timed(u"paste (Ctrl+V)"_s, [&] { dp.clip.paste(); });
        idle(u""_s);
        drawpanel::DrawHandler &hd = dp.prov_handler;
        hd.press(cx + 0.5, cy + 0.5, 1.0, Qt::NoModifier);
        for (int k = 1; k <= 30; ++k)
            timed(u"drag: one mouse move"_s, [&] { hd.move(cx + 0.5 + 2 * k, cy + 0.5 + k); });
        timed(u"drag: release"_s, [&] { hd.release(cx + 60.5, cy + 30.5, 1.0, Qt::NoModifier); });
        timed(u"nudge (arrow key)"_s, [&] { dp.clip.nudge(1, 0); });
        timed(u"turn a quarter"_s, [&] { dp.clip.rotate(true); });
        timed(u"mirror"_s, [&] { dp.clip.flip(true); });
        timed(u"OK (place them)"_s, [&] { dp.clip.place(); });
        idle(u""_s, 1500);
        timed(u"undo the paste"_s, [&] { win.undo(); });
        idle(u""_s, 1500);
        timed(u"undo the cut"_s, [&] { win.undo(); });
        idle(u""_s, 1500);
        timed(u"Brush tool"_s, [&] { dp.tool_btns.at(u"brush"_s)->setChecked(true); });
        // a province brush stroke of 40 moves, 12 px
        dp.set_paint(A);
        dp.size_spin->setValue(brush);
        hd.press(cx + 0.5, cy + 0.5, 1.0, Qt::NoModifier);
        for (int k = 1; k <= 40; ++k)
            timed(u"province brush: one mouse move"_s, [&] { hd.move(cx + 0.5 + k, cy + 0.5 + (k % 7)); });
        timed(u"province brush: release (the undo step)"_s, [&] { hd.release(cx + 40.5, cy + 0.5, 1.0, Qt::NoModifier); });
        idle(u""_s, 1500);
        timed(u"undo the stroke"_s, [&] { win.undo(); });
        idle(u""_s, 1500);
        {   // the same stroke as real mouse events on the map (its hover, the status bar, the repaint)
            QWidget *vp = win.map->viewport();
            auto at = [&](double sx, double sy) { return QPointF(win.map->mapFromScene(QPointF(sx, sy))); };
            auto send = [&](QEvent::Type t, QPointF p, Qt::MouseButtons bs) {
                QMouseEvent ev(t, p, vp->mapToGlobal(p), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, bs,
                               Qt::NoModifier);
                QApplication::sendEvent(vp, &ev);
            };
            send(QEvent::MouseButtonPress, at(cx + 0.5, cy + 0.5), Qt::LeftButton);
            for (int k = 1; k <= 40; ++k)
                timed(u"province brush via the mouse: one move"_s,
                      [&] { send(QEvent::MouseMove, at(cx + 0.5 + k, cy + 0.5 + (k % 7)), Qt::LeftButton); });
            timed(u"province brush via the mouse: release"_s,
                  [&] { send(QEvent::MouseButtonRelease, at(cx + 40.5, cy + 0.5), Qt::NoButton); });
            // the whole-map check that follows a stroke (on its timer: 1.5 s of rest while drawing), timed by itself
            timed(u"the map check after a stroke"_s, [&] { win.chk_panel->refresh(win.doc.get()); });
            idle(u""_s, 2500);
            for (int k = 1; k <= 40; ++k)            // hovering without a button (the status bar only)
                timed(u"hover over the map: one move"_s,
                      [&] { send(QEvent::MouseMove, at(cx + 0.5 + 3 * k, cy + 0.5 + (k % 5)), Qt::NoButton); });
            timed(u"undo the mouse stroke"_s, [&] { win.undo(); });
            idle(u""_s, 1500);
            // DRAWPERF_STROKES=n: n strokes one after the other, nothing undone (does each get slower?)
            const int n = qEnvironmentVariableIntValue("DRAWPERF_STROKES");
            for (int k = 0; k < n; ++k) {
                const QString g = u" (strokes %1-%2)"_s.arg(k / 5 * 5 + 1).arg(k / 5 * 5 + 5);
                const double ox = cx + 0.5 + (k % 6) * 9, oy = cy + 0.5 + (k / 6) * 9 - 20;
                send(QEvent::MouseButtonPress, at(ox, oy), Qt::LeftButton);
                for (int j = 1; j <= 15; ++j)
                    timed(u"many strokes: one move"_s + g, [&] { send(QEvent::MouseMove, at(ox + j, oy + (j % 4)), Qt::LeftButton); });
                timed(u"many strokes: release"_s + g, [&] { send(QEvent::MouseButtonRelease, at(ox + 15, oy), Qt::NoButton); });
                idle(u""_s, 400);
            }
        }
        // the layers
        timed(u"layer: Terrain (2)"_s, [&] { dp.set_layer(u"terrain"_s); });
        idle(u""_s);
        {
            terrainpanel::TerrainHandler &th = dp.terrain_box->handler;
            dp.terrain_box->size_spin->setValue(brush);
            dp.terrain_box->scope->setCurrentIndex(0);   // ("All land": the scope is remembered between runs)
            if (dp.terrain_box->index && dp.terrain_box->tb().load() && dp.terrain_box->tb().data) {   // a type that isn't the one under the brush
                const long long here = (*dp.terrain_box->tb().data)(static_cast<long long>(cy), static_cast<long long>(cx));
                for (const auto &[i, sw] : dp.terrain_box->swatches)
                    if (i != here) {
                        dp.terrain_box->set_index(i);
                        break;
                    }
            }
            if (dp.terrain_box->index) {
                th.press(cx + 0.5, cy + 0.5, 1.0, Qt::NoModifier);
                for (int k = 1; k <= 40; ++k)
                    timed(u"terrain brush: one mouse move"_s, [&] { th.move(cx + 0.5 + k, cy + 0.5 + (k % 7)); });
                timed(u"terrain brush: release"_s, [&] { th.release(cx + 40.5, cy + 0.5, 1.0, Qt::NoModifier); });
                idle(u""_s);
                // the brush circle moved about without a button: the window as shown must equal the window drawn
                // afresh — no circle left behind by the repaint of only its own place
                win.set_selection({});                       // (the selection pulses on a timer: none while comparing,
                idle(u""_s, 2000);                           //  and the map check has run before)
                for (int k = 1; k <= 12; ++k)
                    timed(u"terrain brush circle: one move"_s, [&] { th.move(cx + 0.5 + 9 * k, cy + 0.5 - 5 * (k % 4)); });
                idle(u""_s, 300);
                {
                    QScreen *scr = win.windowHandle() ? win.windowHandle()->screen() : QGuiApplication::primaryScreen();
                    const QImage shown = scr->grabWindow(win.winId()).toImage();
                    win.map->viewport()->repaint();
                    flush();
                    const QImage fresh = scr->grabWindow(win.winId()).toImage();
                    ++verify_runs;
                    const bool same = !shown.isNull() && shown == fresh;
                    std::printf("brush circle repaint: the window as shown %s the window drawn afresh (%dx%d)\n",
                                same ? "equals" : "DIFFERS FROM", shown.width(), shown.height());
                    if (!same) {
                        ++verify_fails;
                        shown.save(QDir::temp().filePath(u"drawperf_shown.png"_s));
                        fresh.save(QDir::temp().filePath(u"drawperf_fresh.png"_s));
                    }
                }
                timed(u"undo the terrain stroke"_s, [&] { win.undo(); });
                idle(u""_s);
            }
        }
        timed(u"layer: Rivers (3)"_s, [&] { dp.set_layer(u"rivers"_s); });
        idle(u""_s);
        timed(u"layer: Colours (4)"_s, [&] { dp.set_layer(u"colours"_s); });
        idle(u""_s);
        timed(u"layer: Provinces (1)"_s, [&] { dp.set_layer(u"prov"_s); });
        idle(u""_s);
        for (int k = 0; k < 4; ++k)
            timed(u"zoom step"_s, [&] { win.map->zoom_by(k % 2 ? 1.25 : 0.8, false); });
        timed(u"the map drawn again whole (render_map)"_s, [&] { win.render_map(); });
        timed(u"grid on / off"_s, [&] { dp.toggle_grid(); });
        timed(u"grid on / off"_s, [&] { dp.toggle_grid(); });
    }
    std::printf("\n%-44s %9s %9s %9s %5s\n", "step", "median", "worst", "total", "n");
    for (const QString &k : order) {
        std::vector<double> v = times[k];
        std::sort(v.begin(), v.end());
        double tot = 0;
        for (double x : v)
            tot += x;
        std::printf("%-44s %8.1f %8.1f %9.1f %5d\n", qUtf8Printable(k), v[v.size() / 2], v.back(), tot, int(v.size()));
    }
    if (sampler) {
        sampler->on = false;
        sampler->report(qEnvironmentVariableIntValue("DRAWPERF_TOP") ? qEnvironmentVariableIntValue("DRAWPERF_TOP") : 60, qUtf8Printable(profile_step));
    }
    std::printf("\nkept masks compared with fresh ones %d times: %s\n", verify_runs,
                verify_fails ? "FAIL" : "all identical");
    g_win = nullptr;
    return verify_fails ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    if (a.size() < 2) {
        std::printf("usage: med_drawperf_check <work folder> [rounds]\n");
        return 2;
    }
    std::unique_ptr<prof::Sampler> s;
    if (a.size() > 3) {
        profile_step = a[3];
        s = std::make_unique<prof::Sampler>(GetCurrentThread());
        sampler = s.get();
    }
    try {
        return run(a[1], a.size() > 2 ? a[2].toInt() : 2);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
