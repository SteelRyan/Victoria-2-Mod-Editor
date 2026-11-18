// provclip (Drawing → cut / paste provinces): C++ only, no Python side — the rules are checked directly.
// Ferrum Mare in the main window, a DrawPanel of its own. Two neighbouring provinces are cut and pasted elsewhere:
// every pixel lands at the new place with its own number, its own terrain and river value; positions.txt moves by
// the same step; undo brings the maps back bit for bit; Esc puts them back; dragging with the mouse (anywhere); since
// 2026-11-09 only ✓ OK places them (a click beside them or another layer does not); off the map; the cut undone
// before Ctrl+V; turns and mirrors (pixels, terrain, positions, the name's angle); the Cut & paste tool's box.
// Usage: med_provclip_check <work folder>   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS = a settings file of its own)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pypath.h"
#include "map/colormap.h"
#include "map/layerbmp.h"
#include "map/moreviews_data.h"
#include "map/provbmp.h"
#include "ui/colormappanel.h"
#include "ui/drawpanel.h"
#include "ui/mapview.h"
#include "ui/pospanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QSpinBox>
#include <QCryptographicHash>
#include <QDir>
#include <QPushButton>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using namespace drawpanel;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    if (!c)
        ++fails;
}
template <typename T>
static QByteArray hs(const grid::Grid<T> &g)
{
    return QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char *>(g.flat().data()), qsizetype(g.flat().size() * sizeof(T))),
        QCryptographicHash::Sha1);
}

static int run(const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, QDir::temp().filePath(u"provclip_check_backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;
    pospanel::PositionsPanel pp(*m, md);
    pp.view = win.map;
    DrawPanel dp(*m, md);
    dp.view = win.map;
    dp.geo = win.geo.get();
    dp.landsea = win.landsea.get();
    dp.pos_panel = &pp;
    QStringList msgs;
    QObject::connect(&dp, &DrawPanel::message, [&](const QString &t) { msgs << t; });
    DrawHandler &hd = dp.prov_handler;
    layerbmp::IndexedBitmap &tb = dp.landsea->terrain;
    layerbmp::IndexedBitmap &rb = dp.landsea->rivers;
    tb.load();
    rb.load();
    dp.landsea->_prepare();
    ok(tb.data.has_value() && rb.data.has_value(), u"terrain.bmp and rivers.bmp read"_s);
    auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);

    // two neighbouring land provinces with positions, one with rivers if there is such a pair
    long long A = 0, B = 0;
    for (long long y = 200; y < md.h - 200 && !B; y += 7) {
        const std::uint16_t *row = md.idmap.row(y);
        for (long long x = 200; x < md.w - 200 && !B; x += 3) {
            const long long a = row[x], b = row[x + 1];
            if (a && b && a != b && m->provinces.contains(a) && m->provinces.contains(b) && md.area[size_t(a)] > 300 &&
                md.area[size_t(b)] > 300 && md.area[size_t(a)] < 6000 && md.area[size_t(b)] < 6000 &&
                pf.entries.contains(a) && pf.entries.contains(b) && !m->provinces.get(a)->sea && !m->provinces.get(b)->sea) {
                A = a;
                B = b;
            }
        }
    }
    ok(A && B, u"picked provinces %1 and %2"_s.arg(A).arg(B));
    if (!A)
        return 1;
    // where they are now
    struct Px { long long x, y; std::uint16_t id; std::uint8_t t, r; };
    std::vector<Px> px;
    for (long long y = 0; y < md.h; ++y)
        for (long long x = 0; x < md.w; ++x) {
            const std::uint16_t id = md.idmap.row(y)[x];
            if (id == A || id == B)
                px.push_back({x, y, id, tb.data->row(y)[x], rb.data->row(y)[x]});
        }
    long long rivers_on = 0;
    for (const Px &q : px)
        rivers_on += q.r != std::uint8_t(dp.landsea->river_none);
    std::printf("     %zu pixels, %lld with a river\n", px.size(), rivers_on);
    const long long areaA = md.area[size_t(A)], areaB = md.area[size_t(B)];
    const positions::Entry eA = pf.get(A), eB = pf.get(B);
    const QByteArray h0 = hs(md.idmap), t0 = hs(*tb.data), r0 = hs(*rb.data);
    // the colour map as it is now (the mod's own or the base game's)
    QImage cm0;
    if (dp.colour_box) {
        dp.colour_box->doc = &doc;
        if (const auto *s = dp.colour_box->source())
            cm0 = s->pic.convertToFormat(QImage::Format_RGBA8888);
    }
    ok(!cm0.isNull(), u"the colour map read (%1 × %2)"_s.arg(cm0.width()).arg(cm0.height()));
    dp.edit_btn->setChecked(true);
    dp.refresh(doc, {A, B}, {});

    // --- 1. cut, paste, nudge, click outside → placed 40 right, 25 down
    QString msg = dp.clip.cut();
    std::printf("     %s\n", qUtf8Printable(msg));
    bool all_black = true;
    for (const Px &q : px)
        all_black = all_black && md.idmap.row(q.y)[q.x] == 0 &&
                    rb.data->row(q.y)[q.x] == std::uint8_t(dp.landsea->river_none);
    ok(all_black && md.area[size_t(A)] == 0 && md.area[size_t(B)] == 0, u"cut: their pixels are no province, no river"_s);
    ok(dp.clip.held() && !dp.clip.floating(), u"cut: held, not floating yet"_s);
    msg = dp.clip.paste();
    ok(dp.clip.floating(), u"Ctrl+V: floating"_s);
    ok(md.area[size_t(A)] == 0, u"floating: not on the map yet"_s);
    dp.clip.nudge(40, 25);
    ok(dp.clip.contains(double(px[0].x + 40) + 0.5, double(px[0].y + 25) + 0.5), u"a pixel of them, moved, counts as inside"_s);
    // a click far away does NOT place them any more (2026-11-09: only ✓ OK does)
    ok(hd.press(5.5, 5.5, 1.0, Qt::NoModifier), u"press outside taken by the clip"_s);
    hd.release(5.5, 5.5, 1.0, Qt::NoModifier);
    ok(dp.clip.floating() && md.area[size_t(A)] == 0, u"a click beside them: still floating"_s);
    ok(!dp.clip_box_btns.empty() && dp.clip_box_btns.back()->isEnabled() &&
           dp.clip_box_btns.back()->text().contains(u"OK"_s), u"the box's ✓ OK is on while they float"_s);
    dp.clip_box_btns.back()->click();                // ✓ OK
    ok(!dp.clip.floating() && !dp.clip.held(), u"✓ OK: placed, nothing held any more"_s);
    ok(!dp.clip_box_btns.back()->isEnabled(), u"✓ OK off again"_s);
    std::printf("     %s\n", qUtf8Printable(msgs.isEmpty() ? QString() : msgs.back()));
    bool landed = true, terr_ok = true, riv_ok = true;
    for (const Px &q : px) {
        landed = landed && md.idmap.row(q.y + 25)[q.x + 40] == q.id;
        terr_ok = terr_ok && tb.data->row(q.y + 25)[q.x + 40] == q.t;
        riv_ok = riv_ok && rb.data->row(q.y + 25)[q.x + 40] == q.r;
    }
    ok(landed, u"every pixel at +40 +25 with its own number"_s);
    ok(terr_ok, u"terrain moved along"_s);
    ok(riv_ok, u"rivers moved along"_s);
    ok(md.area[size_t(A)] == areaA && md.area[size_t(B)] == areaB, u"areas unchanged"_s);
    bool pos_ok = true;
    for (const auto &[pid, e0] : std::vector<std::pair<long long, positions::Entry>>{{A, eA}, {B, eB}}) {
        const positions::Entry e1 = pf.get(pid);
        for (const auto &[k, xy] : e0.pts) {
            const positions::XY *n = e1.pts.get(k);
            pos_ok = pos_ok && n && n->first == xy.first + 40 && n->second == xy.second - 25;
        }
    }
    ok(pos_ok, u"positions moved +40 x, -25 y (game coordinates)"_s);
    ok(m->provinces.contains(A) && m->provinces.contains(B), u"the provinces (history, owner …) are still there"_s);
    // the land colours (colormap.dds) went along: the user 2026-11-17 — the provinces' own colour-map pixels and
    // CM_MARGIN (5) colour-map pixels around them, at the new place; nothing else of the colour map changed
    {
        const auto &ae = doc.map<moreviews::AssetEdits>(u"assets"_s);
        const moreviews::Asset *a = ae.fresh.get(colormap::REL);
        ok(a && !a->img.isNull() && a->img.size() == cm0.size(), u"colour map: a new one pending (%1 × %2)"_s
                                                                     .arg(cm0.width()).arg(cm0.height()));
        if (a && a->img.size() == cm0.size()) {
            const QImage cm1 = a->img.convertToFormat(QImage::Format_RGBA8888);
            const colormap::Stretch st{md.w, md.h, cm0.width(), cm0.height()};
            // every colour-map pixel whose middle lands on a moved province pixel: the colour from where it came from
            std::set<std::pair<long long, long long>> under;
            for (const Px &q : px)
                under.insert({st.col(q.x + 40), st.row(q.y + 25)});
            long long same = 0, tried = 0;
            for (const auto &[c, r] : under) {
                const double X = (double(c) + 0.5) * st.sx(), Y = (double(cm0.height() - 1 - r) + 0.5) * st.sy();
                const long long sx = (long long)(X - 40.0), sy = (long long)(Y - 25.0);
                if (sx < 0 || sy < 0 || sx >= md.w || sy >= md.h)
                    continue;
                ++tried;
                same += std::memcmp(cm1.constScanLine(int(r)) + 4 * c,
                                    cm0.constScanLine(int(st.row(sy))) + 4 * st.col(sx), 4) == 0;
            }
            ok(tried > 0 && same == tried, u"colour map: the %1 pixels under them carry their old colours (%2 do)"_s
                                               .arg(tried).arg(same));
            // the margin: colour-map pixels 1 … 3 outside them carry the colours from around their old place too
            long long m_tried = 0, m_same = 0;
            for (const auto &[c0, r0] : under)
                for (long long dc = -3; dc <= 3; ++dc)
                    for (long long dr = -3; dr <= 3; ++dr) {
                        const long long c = c0 + dc, r = r0 + dr;
                        if (under.count({c, r}) || c < 0 || r < 0 || c >= cm0.width() || r >= cm0.height())
                            continue;
                        const double X = (double(c) + 0.5) * st.sx(), Y = (double(cm0.height() - 1 - r) + 0.5) * st.sy();
                        const long long sx = (long long)(X - 40.0), sy = (long long)(Y - 25.0);
                        if (sx < 0 || sy < 0 || sx >= md.w || sy >= md.h)
                            continue;
                        ++m_tried;
                        m_same += std::memcmp(cm1.constScanLine(int(r)) + 4 * c,
                                              cm0.constScanLine(int(st.row(sy))) + 4 * st.col(sx), 4) == 0;
                    }
            ok(m_tried > 0 && m_same == m_tried, u"colour map: the margin (%1 pixel visits up to 3 outside) carries its old "
                                                  "colours too (%2 do)"_s.arg(m_tried).arg(m_same));
            long long changed = 0;
            for (int r = 0; r < cm0.height(); ++r)
                for (int c = 0; c < cm0.width(); ++c)
                    changed += std::memcmp(cm0.constScanLine(r) + 4 * c, cm1.constScanLine(r) + 4 * c, 4) != 0;
            // the margin: colour-map pixels around them moved too (more changed than lie under them), but only near
            ok(changed >= (long long)under.size() / 2 && changed < (long long)under.size() * 20 + 2000,
               u"colour map: %1 pixels changed (%2 under them, the rest the margin around)"_s.arg(changed).arg(under.size()));
        }
    }
    const auto u1 = doc.undo();
    const auto u2 = doc.undo();
    std::printf("     undo: %s / %s\n", u1 ? qUtf8Printable(*u1) : "None", u2 ? qUtf8Printable(*u2) : "None");
    ok(!doc.map<moreviews::AssetEdits>(u"assets"_s).fresh.contains(colormap::REL), u"two undos: the colour map as before"_s);
    ok(hs(md.idmap) == h0 && hs(*tb.data) == t0 && hs(*rb.data) == r0, u"two undos: all three maps as at the start"_s);
    ok(pf.get(A).pts == eA.pts && pf.get(B).pts == eB.pts, u"two undos: positions as at the start"_s);

    // --- 2. Esc while floating: back where they were
    dp.refresh(doc, {A, B}, {});
    dp.clip.cut();
    dp.clip.paste();
    dp.clip.nudge(13, -7);
    ok(hd.cancel(), u"Esc taken by the clip"_s);
    ok(hs(md.idmap) == h0 && hs(*rb.data) == r0 && hs(*tb.data) == t0, u"Esc: back as they were"_s);
    doc.undo();
    doc.undo();
    ok(hs(md.idmap) == h0 && hs(*rb.data) == r0, u"undo after Esc: as at the start"_s);

    // --- 3. drag with the mouse (anywhere, not only on them), another layer keeps them floating, ✓ OK places
    dp.refresh(doc, {A, B}, {});
    dp.clip.cut();
    dp.clip.paste();
    const double gx = 7.5, gy = 9.5;                 // far from them
    ok(hd.press(gx, gy, 1.0, Qt::NoModifier) && dp.clip.dragging(), u"press anywhere grabs them"_s);
    hd.move(gx + 10, gy);
    hd.move(gx + 30, gy - 12);
    hd.release(gx + 30, gy - 12, 1.0, Qt::NoModifier);
    ok(dp.clip.floating() && !dp.clip.dragging(), u"released: still floating"_s);
    dp.cancel_handlers();                            // (what another layer / drawing off / another tab does)
    ok(dp.clip.floating() && md.area[size_t(A)] == 0, u"leaving the layer: still floating (not placed)"_s);
    msgs << dp.clip.place();
    landed = true;
    for (const Px &q : px)
        landed = landed && md.idmap.row(q.y - 12)[q.x + 30] == q.id;
    ok(!dp.clip.floating() && landed, u"dragged +30 -12, placed by ✓ OK"_s);
    doc.undo();
    doc.undo();
    ok(hs(md.idmap) == h0 && hs(*tb.data) == t0 && hs(*rb.data) == r0, u"undo: as at the start"_s);

    // --- 4. off the map
    dp.refresh(doc, {A, B}, {});
    dp.clip.cut();
    dp.clip.paste();
    dp.clip.nudge(-100000, 0);
    msg = dp.clip.place();
    std::printf("     %s\n", qUtf8Printable(msg));
    ok(dp.clip.floating(), u"all off the map: still floating, moved back"_s);
    msg = dp.clip.back();
    ok(hs(md.idmap) == h0, u"then Esc: back as they were"_s);
    doc.undo();
    doc.undo();

    // --- 5. the cut undone before Ctrl+V
    dp.refresh(doc, {A, B}, {});
    dp.clip.cut();
    doc.undo();
    msg = dp.clip.paste();
    std::printf("     %s\n", qUtf8Printable(msg));
    ok(!dp.clip.floating() && !dp.clip.held() && hs(md.idmap) == h0, u"cut undone, Ctrl+V: refused, map as it was"_s);

    // --- 6. nothing selected
    dp.refresh(doc, {}, {});
    msg = dp.clip.cut();
    ok(msg.startsWith(u"⚠"_s) && hs(md.idmap) == h0, u"nothing selected: a note, nothing cut"_s);
    msg = dp.clip.paste();
    ok(msg.startsWith(u"⚠"_s), u"nothing held: a note"_s);

    // --- 7. turn and mirror (2026-11-09)
    long long x0 = md.w, y0 = md.h, x1 = -1, y1 = -1;
    for (const Px &q : px) {
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    const long long w0 = x1 - x0 + 1, h0b = y1 - y0 + 1;
    auto fdiv = [](long long a, long long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const auto empties = [&] {
        for (int i = 0; i < 2; ++i)
            doc.undo();
    };
    // four quarter turns, ⟳⟲, ⇋⇋, ⇵⇵: nothing changes
    for (const auto &[what, ops] : std::vector<std::pair<QString, std::vector<int>>>{
             {u"⟳ ×4"_s, {0, 0, 0, 0}}, {u"⟳⟲"_s, {0, 1}}, {u"⇋⇋"_s, {2, 2}}, {u"⇵⇵"_s, {3, 3}}}) {
        dp.refresh(doc, {A, B}, {});
        dp.clip.cut();
        dp.clip.paste();
        for (int op : ops)
            op == 0 ? dp.clip.rotate(true) : op == 1 ? dp.clip.rotate(false) : dp.clip.flip(op == 2);
        ok(!dp.clip.turned(), what + u": no turn left"_s);
        dp.clip.place();
        ok(hs(md.idmap) == h0 && hs(*tb.data) == t0 && hs(*rb.data) == r0 && pf.get(A) == eA && pf.get(B) == eB,
           what + u", placed: map and positions as at the start"_s);
        empties();
        ok(hs(md.idmap) == h0, what + u": undone"_s);
    }
    // one quarter clockwise: pixel (i, j) of the box -> (h0 - 1 - j, i), about the same middle
    for (const QString &which : {u"cw"_s, u"flipH"_s}) {
        dp.refresh(doc, {A, B}, {});
        dp.clip.cut();
        dp.clip.paste();
        const bool cw = which == u"cw";
        cw ? dp.clip.rotate(true) : dp.clip.flip(true);
        const long long nw = cw ? h0b : w0, nh = cw ? w0 : h0b;
        const long long ox = fdiv(2 * x0 + w0 - nw, 2), oy = fdiv(2 * y0 + h0b - nh, 2);
        msg = dp.clip.place();
        std::printf("     %s\n", qUtf8Printable(msg));
        bool pix = true, ter = true;
        for (const Px &q : px) {
            const long long i = q.x - x0, j = q.y - y0;
            const long long nx = ox + (cw ? h0b - 1 - j : w0 - 1 - i), ny = oy + (cw ? i : j);
            pix = pix && md.idmap.row(ny)[nx] == q.id;
            ter = ter && tb.data->row(ny)[nx] == q.t;
        }
        ok(pix, which + u": every pixel where the turn puts it, with its own number"_s);
        ok(ter, which + u": terrain turned along"_s);
        bool pos = true;
        for (const auto &[pid, e0] : std::vector<std::pair<long long, positions::Entry>>{{A, eA}, {B, eB}}) {
            const positions::Entry e1 = pf.get(pid);
            for (const auto &[k, xy] : e0.pts) {
                const double a = xy.first - double(x0), b = (double(md.h) - xy.second) - double(y0);
                const double ex = double(ox) + (cw ? double(h0b) - b : double(w0) - a), ey = double(oy) + (cw ? a : b);
                const positions::XY *n = e1.pts.get(k);
                pos = pos && n && std::abs(n->first - ex) < 2e-6 && std::abs((double(md.h) - n->second) - ey) < 2e-6;
            }
            std::printf("     #%lld text_rotation %s -> %s, naval_rot %s -> %s\n", pid,
                        e0.text_rotation ? qUtf8Printable(QString::number(*e0.text_rotation)) : "-",
                        e1.text_rotation ? qUtf8Printable(QString::number(*e1.text_rotation)) : "-",
                        e0.naval_rot ? qUtf8Printable(QString::number(*e0.naval_rot)) : "-",
                        e1.naval_rot ? qUtf8Printable(QString::number(*e1.naval_rot)) : "-");
            if (cw && !e0.text_rotation)
                pos = pos && e1.text_rotation && std::abs(*e1.text_rotation - 4.712389) < 1e-6;
        }
        ok(pos, which + u": positions turned along (names turned with the land)"_s);
        empties();
        ok(hs(md.idmap) == h0 && hs(*tb.data) == t0 && pf.get(A) == eA, which + u": undone"_s);
    }
    // --- copy (2026-11-14): Ctrl+C, Ctrl+V, moved off the originals, ✓ OK -> two NEW provinces like them, ONE undo
    // step; the originals stay; the copy stays held for another paste
    {
        dp.refresh(doc, {A, B}, {});
        const size_t steps0 = doc.undo_stack.size();
        const long long popsA = doc.total(A), popsB = doc.total(B);
        msg = dp.clip.copy();
        std::printf("     %s\n", qUtf8Printable(msg));
        ok(dp.clip.held() && dp.clip.copying() && !dp.clip.floating() && hs(md.idmap) == h0 &&
               doc.undo_stack.size() == steps0, u"copy: held, the map and the undo list untouched"_s);
        msg = dp.clip.paste();
        ok(dp.clip.floating(), u"copy, Ctrl+V: floating"_s);
        dp.clip.nudge(w0 + 50, 0);
        msg = dp.clip.place();
        std::printf("     %s\n", qUtf8Printable(msg));
        long long cA = 0, cB = 0;
        for (const auto &[pid, rec] : dp.defs().added) {
            if (rec.tmpl == A)
                cA = pid;
            else if (rec.tmpl == B)
                cB = pid;
        }
        ok(cA && cB && cA != cB, u"copy: two new provinces, #%1 from #%2 and #%3 from #%4"_s.arg(cA).arg(A).arg(cB).arg(B));
        ok(md.area[size_t(A)] == areaA && md.area[size_t(B)] == areaB, u"copy: the originals stay as they were"_s);
        ok(cA && cB && md.area[size_t(cA)] == areaA && md.area[size_t(cB)] == areaB, u"copy: same areas as the originals"_s);
        // where the copy landed: the box of the new pixels against the old one
        long long nx0 = md.w, ny0 = md.h;
        for (long long y = 0; y < md.h; ++y)
            for (long long x = 0; x < md.w; ++x)
                if (const std::uint16_t id = md.idmap.row(y)[x]; cA && (id == cA || id == cB)) {
                    nx0 = std::min(nx0, x);
                    ny0 = std::min(ny0, y);
                }
        const long long ddx = nx0 - x0, ddy = ny0 - y0;
        bool pix = cA && cB, ter = pix, riv = pix;
        for (const Px &q : px) {
            if (!pix)
                break;
            const long long nx = q.x + ddx, ny = q.y + ddy;
            pix = pix && md.idmap.row(ny)[nx] == (q.id == A ? cA : cB);
            ter = ter && tb.data->row(ny)[nx] == q.t;
            riv = riv && rb.data->row(ny)[nx] == q.r;
        }
        ok(pix && ddx > w0, u"copy: every pixel at its place, %1 px right, %2 px down, with its new number"_s.arg(ddx).arg(ddy));
        ok(ter && riv, u"copy: terrain and rivers copied along"_s);
        bool pos = cA && pf.entries.contains(cA);
        if (pos) {
            const positions::Entry e1 = pf.get(cA);
            for (const auto &[k, xy] : eA.pts) {
                const positions::XY *n = e1.pts.get(k);
                pos = pos && n && std::abs(n->first - (xy.first + double(ddx))) < 1e-6 &&
                      std::abs(n->second - (xy.second - double(ddy))) < 1e-6;
            }
        }
        ok(pos, u"copy: its positions are the original's, moved along"_s);
        ok(cA && doc.total(cA) == popsA && doc.total(cB) == popsB && doc.total(A) == popsA,
           u"copy: the same pops as the originals (%1 + %2 people), the originals keep theirs"_s.arg(popsA).arg(popsB));
        const moddata::Province *pa = cA ? m->provinces.get(cA) : nullptr;
        ok(pa && pa->region && !pa->region->isEmpty(), u"copy: in a state (%1)"_s.arg(pa && pa->region ? *pa->region : u"-"_s));
        ok(doc.undo_stack.size() == steps0 + 1 && doc.undo_stack.back()->label.startsWith(u"Paste a copy"_s),
           u"copy: ONE undo step (%1)"_s.arg(doc.undo_stack.empty() ? QString() : doc.undo_stack.back()->label));
        ok(dp.clip.held() && dp.clip.copying() && !dp.clip.floating(), u"copy: still held after ✓ OK (paste again)"_s);
        dp.clip.paste();
        ok(dp.clip.floating() && dp.clip.back().contains(u"dropped"_s) && !dp.clip.floating() && dp.clip.held(),
           u"copy: Esc drops a floating copy, nothing placed, still held"_s);
        doc.undo();
        ok(hs(md.idmap) == h0 && hs(*tb.data) == t0 && hs(*rb.data) == r0 && cA && !dp.defs().added.contains(cA) &&
               !dp.defs().added.contains(cB) && !m->provinces.contains(cA) && doc.total(A) == popsA,
           u"copy: one Ctrl+Z takes it all back (map, new provinces, pops)"_s);
        dp.clip.forget();
    }
    // the Cut & paste tool: the paint rows go, its box comes; a click selects (not taken by the tool)
    dp.tool_btns.at(u"clip"_s)->setChecked(true);
    ok(dp.clip_box->isVisibleTo(&dp) && !dp.paint_spin->isVisibleTo(&dp) && !dp.size_spin->isVisibleTo(&dp),
       u"Cut & paste tool: its box shown, brush rows hidden"_s);
    ok(!hd.press(double(px[0].x) + 0.5, double(px[0].y) + 0.5, 1.0, Qt::NoModifier),
       u"Cut & paste tool: a click is left to the map (selects)"_s);
    dp.tool_btns.at(u"brush"_s)->setChecked(true);
    ok(!dp.clip_box->isVisibleTo(&dp) && dp.paint_spin->isVisibleTo(&dp), u"Brush again: brush rows back"_s);
    std::printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 2)
            return run(a[1]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
