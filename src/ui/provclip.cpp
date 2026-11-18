// provclip — see provclip.h.
#include "ui/provclip.h"

#include "core/document.h"
#include "core/positions.h"
#include "core/pynum.h"
#include "map/layerbmp.h"
#include "map/provbmp.h"
#include "map/newprov.h"
#include "map/colormap.h"
#include "map/moreviews_data.h"
#include "ui/colormappanel.h"
#include "ui/drawpanel.h"

#include <QLocale>
#include <cstring>
#include <map>

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace provclip {

static constexpr double PI = 3.14159265358979323846;

static QString plural(size_t n, const QString &one, const QString &many) { return n == 1 ? one : many; }

static QRgb tint(long long pid)                      // a colour each, light enough to see on any map mode
{
    const double h = std::fmod(double(pid) * 0.6180339887, 1.0);
    const QColor c = QColor::fromHsvF(float(h), 0.55f, 0.95f);
    return qRgba(c.red(), c.green(), c.blue(), 225);
}

void Clip::forget()
{
    pids.clear();
    float_ = false;
    copy_ = false;
    grab_at.reset();
    off0.clear();
    off.clear();
    ids.clear();
    terr.clear();
    riv.clear();
    mask.clear();
    has_cm = false;
    cm_pic = QImage();
    cm_mask.clear();
    img = QImage();
    m = {1, 0, 0, 1};
    if (p.view)
        p.view->shape_layer->set_temp({});
}

std::vector<long long> Clip::_wanted() const
{
    std::vector<long long> want(p.sel.begin(), p.sel.end());
    want.insert(want.end(), p.sea_sel.begin(), p.sea_sel.end());
    const mapview::MapData &md = p.md;
    std::vector<std::uint8_t> on(size_t(std::max<long long>(md.n, 1)), 0);
    std::vector<long long> take;
    for (long long pid : want)
        if (pid > 0 && pid < md.n && md.area[size_t(pid)] > 0 && !on[size_t(pid)]) {
            on[size_t(pid)] = 1;
            take.push_back(pid);
        }
    return take;
}

std::vector<std::int64_t> Clip::_grab(const std::vector<long long> &take)
{
    // the box around them and their pixels (and the terrain / rivers under them)
    mapview::MapData &md = p.md;
    std::vector<std::uint8_t> on(size_t(std::max<long long>(md.n, 1)), 0);
    for (long long pid : take)
        on[size_t(pid)] = 1;
    const auto &flat = md.idmap.flat();
    long long x0 = md.w, y0 = md.h, x1 = -1, y1 = -1;
    for (long long y = 0; y < md.h; ++y) {
        const std::uint16_t *row = md.idmap.row(y);
        for (long long x = 0; x < md.w; ++x)
            if (on[row[x]]) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    }
    forget();
    pids = take;
    bx = x0;
    by = y0;
    w0 = bw = x1 - x0 + 1;
    h0 = bh = y1 - y0 + 1;
    layerbmp::IndexedBitmap *tb = nullptr, *rb = nullptr;
    if (p.landsea) {
        if (p.landsea->terrain.load() && p.landsea->terrain.data)
            tb = &p.landsea->terrain;
        if (p.landsea->rivers.load() && p.landsea->rivers.data)
            rb = &p.landsea->rivers;
        p.landsea->_prepare();
    }
    has_terr = tb != nullptr;
    has_riv = rb != nullptr;
    std::vector<std::int64_t> idx;
    for (long long y = y0; y <= y1; ++y)
        for (long long x = x0; x <= x1; ++x) {
            const std::int64_t i = y * md.w + x;
            const std::uint16_t id = flat[size_t(i)];
            if (!on[id])
                continue;
            off0.push_back(std::int32_t((y - y0) * w0 + (x - x0)));
            ids.push_back(id);
            idx.push_back(i);
            if (tb)
                terr.push_back(tb->data->flat()[size_t(i)]);
            if (rb)
                riv.push_back(rb->data->flat()[size_t(i)]);
        }
    _lay_out();
    _grab_colours(idx);
    return idx;
}

// how many colour-map pixels around the provinces go along (the user: "plus 4 or 5 pixels more on the outline"; read as
// pixels of the colour map, ~15 map px in vanilla — the colours blend over that much)
static constexpr long long CM_MARGIN = 5;

void Clip::_grab_colours(const std::vector<std::int64_t> &idx)
{
    has_cm = false;
    cm_pic = QImage();
    cm_mask.clear();
    if (!p.colour_box || idx.empty())
        return;
    p.colour_box->doc = p.doc;
    const colormappanel::ColormapBox::Source *s = p.colour_box->source();
    if (!s || s->pic.isNull())
        return;
    cm_pic = s->pic.convertToFormat(QImage::Format_RGBA8888);
    cm_w = cm_pic.width();
    cm_h = cm_pic.height();
    const colormap::Stretch st{p.md.w, p.md.h, cm_w, cm_h};
    std::vector<std::uint8_t> on(size_t(cm_w * cm_h), 0);
    long long c0 = cm_w, c1 = -1, r0 = cm_h, r1 = -1;
    for (const std::int64_t i : idx) {
        const long long c = st.col(i % p.md.w), r = st.row(i / p.md.w);
        on[size_t(r * cm_w + c)] = 1;
        c0 = std::min(c0, c);
        c1 = std::max(c1, c);
        r0 = std::min(r0, r);
        r1 = std::max(r1, r);
    }
    // grown by CM_MARGIN on every side (a square: one pass along the rows, one along the columns)
    c0 = std::max(0LL, c0 - CM_MARGIN);
    c1 = std::min(cm_w - 1, c1 + CM_MARGIN);
    r0 = std::max(0LL, r0 - CM_MARGIN);
    r1 = std::min(cm_h - 1, r1 + CM_MARGIN);
    std::vector<std::uint8_t> rows(on.size(), 0);
    for (long long r = r0; r <= r1; ++r)
        for (long long c = c0; c <= c1; ++c) {
            bool any = false;
            for (long long d = -CM_MARGIN; d <= CM_MARGIN && !any; ++d) {
                const long long cc = c + d;
                any = cc >= 0 && cc < cm_w && on[size_t(r * cm_w + cc)];
            }
            rows[size_t(r * cm_w + c)] = any;
        }
    cm_mask.assign(on.size(), 0);
    for (long long r = r0; r <= r1; ++r)
        for (long long c = c0; c <= c1; ++c) {
            bool any = false;
            for (long long d = -CM_MARGIN; d <= CM_MARGIN && !any; ++d) {
                const long long rr = r + d;
                any = rr >= 0 && rr < cm_h && rows[size_t(rr * cm_w + c)];
            }
            cm_mask[size_t(r * cm_w + c)] = any;
        }
    has_cm = true;
}

bool Clip::_colours(QImage &out, QString &path) const
{
    if (!has_cm || !p.colour_box)
        return false;
    p.colour_box->doc = p.doc;
    const colormappanel::ColormapBox::Source *s = p.colour_box->source();
    if (!s || s->pic.width() != cm_w || s->pic.height() != cm_h)
        return false;
    out = s->pic.convertToFormat(QImage::Format_RGBA8888);
    path = s->path;
    const colormap::Stretch st{p.md.w, p.md.h, cm_w, cm_h};
    // every colour-map pixel where they (and the margin) land: looked up backwards — its middle on the map, turned
    // back to where it was cut from (m is a quarter turn / mirror: its inverse is its transpose)
    const double mx = (double(CM_MARGIN) + 1.0) * std::max(st.sx(), st.sy());
    const long long xa = std::max(0LL, (long long)std::floor(double(ox) - mx)),
                    xb = std::min(p.md.w - 1, (long long)std::ceil(double(ox + bw) + mx));
    const long long ya = std::max(0LL, (long long)std::floor(double(oy) - mx)),
                    yb = std::min(p.md.h - 1, (long long)std::ceil(double(oy + bh) + mx));
    const long long ca = st.col(xa), cb = st.col(xb), ra = st.row(yb), rb = st.row(ya);   // (file rows run south up)
    bool changed = false;
    for (long long r = ra; r <= rb; ++r) {
        uchar *dst = out.scanLine(int(r));
        for (long long c = ca; c <= cb; ++c) {
            const double X = (double(c) + 0.5) * st.sx(), Y = (double(cm_h - 1 - r) + 0.5) * st.sy();
            const double u2 = 2.0 * (X - double(ox)) - double(bw), v2 = 2.0 * (Y - double(oy)) - double(bh);
            const double u = m[0] * u2 + m[2] * v2, v = m[1] * u2 + m[3] * v2;
            const double sx = double(bx) + (u + double(w0)) / 2.0, sy = double(by) + (v + double(h0)) / 2.0;
            if (sx < 0 || sy < 0 || sx >= double(p.md.w) || sy >= double(p.md.h))
                continue;
            const long long sc = st.col((long long)sx), sr = st.row((long long)sy);
            if (!cm_mask[size_t(sr * cm_w + sc)])
                continue;
            const uchar *from = cm_pic.constScanLine(int(sr)) + 4 * sc;
            if (std::memcmp(dst + 4 * c, from, 4) != 0) {
                std::memcpy(dst + 4 * c, from, 4);
                changed = true;
            }
        }
    }
    return changed;
}

QString Clip::copy()
{
    if (float_)
        return u"⚠ Provinces are still floating — press ✓ OK to place them (or Esc / ✕) before copying more."_s;
    const std::vector<long long> take = _wanted();
    if (take.empty())
        return u"⚠ Nothing to copy: click (or Ctrl+click) the provinces on the map first (they must have land on the "
               "map)."_s;
    _grab(take);
    copy_ = true;
    version = p.bmp().version;
    return u"❐ Copied %1 %2 (%3 px) — Ctrl+V (or Paste) drops a copy to drag where it belongs; ✓ OK makes %4 NEW "
           "provinces (history, owner, cores, pops copied). Paste again for another copy."_s
        .arg(take.size())
        .arg(plural(take.size(), u"province"_s, u"provinces"_s))
        .arg(off0.size())
        .arg(plural(take.size(), u"it"_s, u"them"_s));
}

QString Clip::cut()
{
    if (float_)
        return u"⚠ Provinces are still floating — press ✓ OK to place them (or Esc / ✕ to put them back) before "
               "cutting more."_s;
    const std::vector<long long> take = _wanted();
    if (take.empty())
        return u"⚠ Nothing to cut: click (or Ctrl+click) the provinces on the map first (they must have land on the "
               "map)."_s;
    const std::vector<std::int64_t> idx = _grab(take);
    // they leave the map: no province there, and no river
    std::vector<std::uint16_t> old(ids), zero(ids.size(), 0);
    provbmp::ProvinceBitmap &b = p.bmp();
    b.remember_file();
    layerbmp::IndexedBitmap *rb = has_riv ? &p.landsea->rivers : nullptr;
    QStringList lists{u"provinces_bmp"_s};
    if (rb)
        lists << rb->kind;
    const long long none = p.landsea ? p.landsea->river_none : 255;
    p.doc->edit(u"Cut %1 %2"_s.arg(take.size()).arg(plural(take.size(), u"province"_s, u"provinces"_s)), {}, [&] {
        b.commit(idx, old, zero);
        if (rb)
            rb->apply(idx, std::vector<std::uint8_t>(idx.size(), std::uint8_t(none)));
    }, {}, {}, lists);
    version = b.version;
    return u"✂ Cut %1 %2 (%3 px) — Ctrl+V (or Paste) brings %4 back to move where %5 belong; Ctrl+Z undoes the cut."_s
        .arg(take.size())
        .arg(plural(take.size(), u"province"_s, u"provinces"_s))
        .arg(off0.size())
        .arg(plural(take.size(), u"it"_s, u"them"_s), plural(take.size(), u"it"_s, u"they"_s));
}

void Clip::_lay_out()
{
    // The pixels as they lie after the turn m: about the middle of the cut box, in doubled coordinates (exact).
    const bool swap = m[0] == 0;
    bw = swap ? h0 : w0;
    bh = swap ? w0 : h0;
    off.assign(off0.size(), 0);
    mask.assign(size_t(bw * bh), 0);
    for (size_t k = 0; k < off0.size(); ++k) {
        const long long i = off0[k] % w0, j = off0[k] / w0;
        const long long u = 2 * i + 1 - w0, v = 2 * j + 1 - h0;
        const long long u2 = m[0] * u + m[1] * v, v2 = m[2] * u + m[3] * v;
        const long long ni = (u2 + bw - 1) / 2, nj = (v2 + bh - 1) / 2;
        off[k] = std::int32_t(nj * bw + ni);
        mask[size_t(off[k])] = 1;
    }
    img = QImage(int(bw), int(bh), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (size_t k = 0; k < off.size(); ++k)
        img.setPixel(int(off[k] % bw), int(off[k] / bw), tint(ids[k]));
}

QString Clip::paste()
{
    if (!held())
        return u"⚠ Nothing to paste: pick provinces on the map, then Ctrl+X (Cut) or Ctrl+C (Copy)."_s;
    if (float_)
        return u"They are already floating — drag them, turn them, then press ✓ OK to place them."_s;
    if (!copy_ && p.bmp().version != version) {
        forget();
        return u"⚠ The map changed since the cut (painted, or the cut undone) — cut the provinces again."_s;
    }
    if (copy_) {                                     // (a copy: the originals must still be there)
        bool any = false;
        for (long long pid : pids)
            any = any || p.mod.provinces.contains(pid);
        if (!any) {
            forget();
            return u"⚠ The copied provinces are gone (deleted, or undone) — copy them again."_s;
        }
    }
    float_ = true;
    m = {1, 0, 0, 1};
    _lay_out();
    ox = bx;
    oy = by;
    if (copy_) {                                     // a copy floats a little off the originals, on the map
        const long long step = std::clamp<long long>(std::min(bw, bh) / 3, 12, 48);
        ox = std::max<long long>(0, std::min(bx + step, p.md.w - bw));
        oy = std::max<long long>(0, std::min(by + step, p.md.h - bh));
    }
    cx2 = 2 * ox + bw;
    cy2 = 2 * oy + bh;
    show();
    return copy_ ? u"A copy floats — drag it anywhere on the map (arrow keys: 1 px, Shift+arrow: 10 px); ⟲ ⟳ turn it, "
                   "⇋ ⇵ mirror it. ✓ OK (bottom right) makes the new provinces; Esc drops the copy."_s
                 : u"Drag anywhere on the map to move them (arrow keys: 1 px, Shift+arrow: 10 px); ⟲ ⟳ turn them, ⇋ ⇵ "
                   "mirror them. Press ✓ OK (bottom right) to place them; Esc puts them back."_s;
}

bool Clip::contains(double x, double y) const
{
    if (!float_)
        return false;
    const long long cx = (long long)std::floor(x) - ox, cy = (long long)std::floor(y) - oy;
    // (a few pixels around count too: thin provinces are easy to miss)
    for (long long dy = -2; dy <= 2; ++dy)
        for (long long dx = -2; dx <= 2; ++dx) {
            const long long xx = cx + dx, yy = cy + dy;
            if (xx >= 0 && yy >= 0 && xx < bw && yy < bh && mask[size_t(yy * bw + xx)])
                return true;
        }
    return false;
}

void Clip::grab(double x, double y)
{
    grab_at = {x, y};
    gx = ox;
    gy = oy;
}

void Clip::drag(double x, double y)
{
    if (!grab_at)
        return;
    ox = gx + (long long)std::llround(x - grab_at->first);
    oy = gy + (long long)std::llround(y - grab_at->second);
    cx2 = 2 * ox + bw;
    cy2 = 2 * oy + bh;
    show();
}

void Clip::nudge(long long dx, long long dy)
{
    if (!float_)
        return;
    ox += dx;
    oy += dy;
    cx2 = 2 * ox + bw;
    cy2 = 2 * oy + bh;
    show();
}

void Clip::_turn(const std::array<int, 4> &op)
{
    if (!float_)
        return;
    m = {op[0] * m[0] + op[1] * m[2], op[0] * m[1] + op[1] * m[3], op[2] * m[0] + op[3] * m[2],
         op[2] * m[1] + op[3] * m[3]};
    _lay_out();
    ox = py::floordiv(cx2 - bw, 2LL);               // about the same middle
    oy = py::floordiv(cy2 - bh, 2LL);
    show();
}

void Clip::rotate(bool clockwise)
{
    // screen coordinates (y down): clockwise (x, y) -> (-y, x)
    _turn(clockwise ? std::array<int, 4>{0, -1, 1, 0} : std::array<int, 4>{0, 1, -1, 0});
}

void Clip::flip(bool horizontal)
{
    _turn(horizontal ? std::array<int, 4>{-1, 0, 0, 1} : std::array<int, 4>{1, 0, 0, -1});
}

std::vector<mapview::Shape> Clip::shapes() const
{
    if (!float_)
        return {};
    mapview::Shape sh;
    sh.kind = u"image"_s;
    sh.img = img;
    sh.rect = QRectF(double(ox), double(oy), double(bw), double(bh));
    sh.smooth = false;
    sh.opacity = 0.9;
    sh.frame = mapview::Rgb{255, 255, 255};
    return {sh};
}

void Clip::show()
{
    if (p.view)
        p.view->shape_layer->set_temp(shapes());
    p._clip_changed();
}

QString Clip::back()
{
    if (!float_)
        return QString();
    if (copy_) {                                     // a copy: only the floating one goes (still held: Ctrl+V)
        float_ = false;
        grab_at.reset();
        show();
        return u"The copy was dropped — nothing placed. Ctrl+V pastes it again."_s;
    }
    m = {1, 0, 0, 1};
    _lay_out();
    ox = bx;
    oy = by;
    return place() + u" (back where they were)"_s;
}

positions::Entry Clip::turn_entry(const positions::Entry &e0, const std::array<int, 4> &m, long long map_h,
                                  long long bx, long long by, long long w0, long long h0, long long ox, long long oy)
{
    positions::Entry e = e0;
    const bool same = m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1;
    if (same) {                                      // only moved (as before the turns were built)
        const long long dx = ox - bx, dy = oy - by;
        for (auto &[k, xy] : e.pts)                  // (game coordinates: y counts up from the bottom)
            xy = {xy.first + double(dx), xy.second - double(dy)};
        return e;
    }
    const bool swap = m[0] == 0;
    const double nw = double(swap ? h0 : w0), nh = double(swap ? w0 : h0);
    for (auto &[k, xy] : e.pts) {
        const double u = xy.first - double(bx) - double(w0) / 2.0;
        const double v = (double(map_h) - xy.second) - double(by) - double(h0) / 2.0;
        const double u2 = m[0] * u + m[1] * v, v2 = m[2] * u + m[3] * v;
        xy = {py::round_to(double(ox) + nw / 2.0 + u2, 6),
              py::round_to(double(map_h) - (double(oy) + nh / 2.0 + v2), 6)};
    }
    auto turn = [&m](double a) {                     // a direction on the screen (clockwise angle) turned by m
        const double c = std::cos(a), s = std::sin(a);
        return std::atan2(m[2] * c + m[3] * s, m[0] * c + m[1] * s);
    };
    // the name: text_rotation is the anticlockwise angle (pospanel: th = -text_rotation); kept readable (never
    // upside down: a name turned past a quarter is read from the other end)
    if (e.text_rotation || swap) {
        double th = turn(-e.text_rotation.value_or(0.0));
        if (th > PI / 2 + 1e-9)
            th -= PI;
        else if (th <= -PI / 2 + 1e-9)
            th += PI;
        e.text_rotation = py::round_to(py::mod(-th, 2 * PI), 6);
    }
    // the port: naval_rot is the clockwise screen angle it faces (pospanel: atan2 of screen x / y)
    if (e.naval_rot)
        e.naval_rot = py::round_to(py::mod(turn(*e.naval_rot), 2 * PI), 6);
    return e;
}

QString Clip::place()
{
    if (!float_)
        return QString();
    if (copy_)
        return _place_copy();
    mapview::MapData &md = p.md;
    provbmp::ProvinceBitmap &b = p.bmp();
    if (b.version != version) {
        forget();
        p._clip_changed();
        return u"⚠ The map changed while the provinces floated (Ctrl+Z?) — nothing placed; cut them again."_s;
    }
    const long long dx = ox - bx, dy = oy - by;
    std::vector<std::int64_t> idx;
    std::vector<std::uint16_t> old, nw;
    std::vector<std::uint8_t> tv, rv;
    long long outside = 0;
    std::set<long long> before;
    const auto &flat = md.idmap.flat();
    for (size_t k = 0; k < off.size(); ++k) {
        const long long x = off[k] % bw + ox, y = off[k] / bw + oy;
        if (x < 0 || y < 0 || x >= md.w || y >= md.h) {
            ++outside;
            continue;
        }
        const std::int64_t i = y * md.w + x;
        idx.push_back(i);
        old.push_back(flat[size_t(i)]);
        if (flat[size_t(i)] != 0)
            before.insert(flat[size_t(i)]);
        nw.push_back(ids[k]);
        if (has_terr)
            tv.push_back(terr[k]);
        if (has_riv)
            rv.push_back(riv[k]);
    }
    if (idx.empty()) {
        m = {1, 0, 0, 1};
        _lay_out();
        ox = bx;
        oy = by;
        cx2 = 2 * ox + bw;
        cy2 = 2 * oy + bh;
        show();
        return u"⚠ They would land off the map — moved back where they were; drag them onto the map."_s;
    }
    b.remember_file();
    QStringList lists{u"provinces_bmp"_s, u"positions"_s};
    if (has_terr)
        lists << p.landsea->terrain.kind;
    if (has_riv)
        lists << p.landsea->rivers.kind;
    QImage cmo;                                      // the land colours laid in where they land
    QString cm_path;
    const bool cm = _colours(cmo, cm_path) && p.doc->maps.contains(u"assets"_s);
    if (cm)
        lists << u"assets"_s;
    const std::vector<long long> moved = pids;
    const bool tur = turned();
    p.doc->edit(u"Paste %1 %2"_s.arg(moved.size()).arg(plural(moved.size(), u"province"_s, u"provinces"_s)), {}, [&] {
        b.commit(idx, old, nw);
        if (has_terr)
            p.landsea->terrain.apply(idx, tv);
        if (has_riv)
            p.landsea->rivers.apply(idx, rv);
        if (cm) {
            moreviews::Asset a;
            a.how = u"dds"_s;
            a.img = cmo;
            a.path = cm_path;
            p.doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.insert(colormap::REL, a);
        }
        if (dx || dy || tur) {
            auto &pf = p.doc->map<positions::PositionsFile>(u"positions"_s);
            for (long long pid : moved) {
                if (!pf.entries.contains(pid))
                    continue;
                pf.set_entry(pid, turn_entry(pf.get(pid), m, md.h, bx, by, w0, h0, ox, oy));
            }
        }
    }, {}, {}, lists);
    // provinces painted over completely
    std::vector<long long> gone;
    const std::set<long long> mine(moved.begin(), moved.end());
    for (long long q : before)
        if (!mine.contains(q) && md.area[size_t(q)] == 0)
            gone.push_back(q);
    const size_t n = moved.size();
    forget();
    p._clip_changed();
    QString msg = u"Placed %1 %2 %3 px right, %4 px down%5 — terrain, rivers, land colours and positions moved along. "
                  u"Ctrl+Z undoes it."_s
                      .arg(n)
                      .arg(plural(n, u"province"_s, u"provinces"_s))
                      .arg(dx)
                      .arg(dy)
                      .arg(tur ? u", turned / mirrored"_s : QString());
    if (outside)
        msg += u" ⚠ %1 px fell off the map edge and are gone."_s.arg(outside);
    if (!gone.empty())
        p._warn_gone(gone);
    return msg;
}

QString Clip::_place_copy()
{
    // The floating copy becomes NEW provinces (see provclip.h), all of it one undo step.
    mapview::MapData &md = p.md;
    provbmp::ProvinceBitmap &b = p.bmp();
    newprov::ProvinceDefs &defs = p.defs();
    std::vector<long long> src;                      // the originals still there
    for (long long pid : pids)
        if (p.mod.provinces.contains(pid))
            src.push_back(pid);
    if (src.empty()) {
        forget();
        p._clip_changed();
        return u"⚠ The copied provinces are gone (deleted, or undone) — nothing placed; copy them again."_s;
    }
    if (defs.next_id() + (long long)src.size() > md.n)
        return u"⚠ No room for %1 more provinces in this session — Apply, open the mod again, then paste."_s.arg(
            src.size());
    const std::set<long long> keep_src(src.begin(), src.end());
    // where the pixels land (those off the map are left out)
    std::vector<size_t> ks;
    long long outside = 0;
    for (size_t k = 0; k < off.size(); ++k) {
        const long long x = off[k] % bw + ox, y = off[k] / bw + oy;
        if (x < 0 || y < 0 || x >= md.w || y >= md.h)
            ++outside;
        else if (keep_src.contains(ids[k]))
            ks.push_back(k);
    }
    if (ks.empty()) {
        ox = std::max<long long>(0, std::min(bx, md.w - bw));
        oy = std::max<long long>(0, std::min(by, md.h - bh));
        cx2 = 2 * ox + bw;
        cy2 = 2 * oy + bh;
        show();
        return u"⚠ The copy would land off the map — drag it onto the map first."_s;
    }
    const document::StepP mark = p.doc->last_step();
    const size_t n = src.size();
    const QString label = u"Paste a copy of %1 %2"_s.arg(n).arg(plural(n, u"province"_s, u"provinces"_s));
    // 1. the new provinces: each from its original (history without buildings, state, continent, climate)
    std::map<long long, long long> to;               // original -> copy
    QString fail;
    for (long long s : src) {
        const moddata::Province *pr = p.mod.provinces.get(s);
        try {
            const newprov::Plan pl = newprov::plan(*p.doc, defs, pr->name, pr->sea,
                                                   pr->sea ? std::nullopt : std::optional<long long>(s));
            newprov::create_province(*p.doc, pl.pid, pl.rec, pl.rid, pl.continent, pl.climate, pl.want);
            p._move_pops[pl.pid] = false;            // (its pops are its original's, not its neighbours')
            to[s] = pl.pid;
        } catch (const std::exception &e) {
            fail = QString::fromUtf8(e.what());
            break;
        }
    }
    if (!fail.isEmpty()) {
        p.doc->merge_steps(mark, label);
        return u"⚠ The copy stopped: %1 — Ctrl+Z takes back what was made."_s.arg(fail);
    }
    // 2. their pixels, terrain and rivers
    std::vector<std::int64_t> idx;
    std::vector<std::uint16_t> old, nw;
    std::vector<std::uint8_t> tv, rv;
    std::set<long long> before;
    const auto &flat = md.idmap.flat();
    for (size_t k : ks) {
        const std::int64_t i = (off[k] / bw + oy) * md.w + off[k] % bw + ox;
        idx.push_back(i);
        old.push_back(flat[size_t(i)]);
        if (flat[size_t(i)] != 0)
            before.insert(flat[size_t(i)]);
        nw.push_back(std::uint16_t(to.at(ids[k])));
        if (has_terr)
            tv.push_back(terr[k]);
        if (has_riv)
            rv.push_back(riv[k]);
    }
    b.remember_file();
    QStringList lists{u"provinces_bmp"_s};
    if (has_terr)
        lists << p.landsea->terrain.kind;
    if (has_riv)
        lists << p.landsea->rivers.kind;
    QImage cmo;                                      // the land colours laid in where the copy lands
    QString cm_path;
    const bool cm = _colours(cmo, cm_path) && p.doc->maps.contains(u"assets"_s);
    if (cm)
        lists << u"assets"_s;
    p.doc->edit(label, {}, [&] {
        b.commit(idx, old, nw);
        if (has_terr)
            p.landsea->terrain.apply(idx, tv);
        if (has_riv)
            p.landsea->rivers.apply(idx, rv);
        if (cm) {
            moreviews::Asset a;
            a.how = u"dds"_s;
            a.img = cmo;
            a.path = cm_path;
            p.doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.insert(colormap::REL, a);
        }
    }, {}, {}, lists);
    // 3. finished: positions moved / turned along from the originals, coast crossings
    auto &pf = p.doc->map<positions::PositionsFile>(u"positions"_s);
    for (const auto &[s, c] : to) {
        if (md.area[size_t(c)] == 0)
            continue;
        if (pf.entries.contains(s)) {
            const positions::Entry e = turn_entry(pf.get(s), m, md.h, bx, by, w0, h0, ox, oy);
            p.finish(c, &e);
        } else {
            p.finish(c);
        }
    }
    // 4. their pops: the originals' (copied first: the store grows while pasting)
    long long people = 0;
    for (const auto &[s, c] : to) {
        if (md.area[size_t(c)] == 0 || p.mod.provinces.get(s)->sea)
            continue;
        const std::vector<document::Pop> pops = p.doc->province_pops(s);
        if (!pops.empty())
            people += p.doc->paste({c}, pops, true, 1.0, std::nullopt);
    }
    // 5. states: the copies of one state get a new state of their own, unless they touch that state
    std::map<QString, QList<long long>> groups;
    for (const auto &[s, c] : to) {
        const moddata::Province *pc = p.mod.provinces.get(c);
        if (md.area[size_t(c)] == 0 || !pc || pc->sea || !pc->region || pc->region->isEmpty())
            continue;
        groups[*pc->region] << c;
    }
    QStringList states;
    for (const auto &[rid, cs] : groups) {
        const QSet<long long> mine(cs.begin(), cs.end());
        bool touches = false;
        for (long long c : cs)
            for (long long q : md.neighbours(c))
                if (!mine.contains(q))
                    if (const moddata::Province *pq = p.mod.provinces.get(q); pq && pq->region == std::optional<QString>(rid))
                        touches = true;
        if (touches)
            continue;
        const QString key = p.doc->new_state_key(rid);
        if (p.doc->move_to_state(cs, key, true).first)
            states << key;
    }
    p.doc->merge_steps(mark, label);
    // provinces painted over completely
    std::vector<long long> gone;
    for (long long q : before)
        if (!keep_src.contains(q) && md.area[size_t(q)] == 0)
            gone.push_back(q);
    float_ = false;                                  // (still held: Ctrl+V pastes another copy)
    grab_at.reset();
    show();
    std::vector<long long> made;
    for (const auto &[s, c] : to)
        made.push_back(c);
    std::sort(made.begin(), made.end());
    QString msg = u"⧉ Pasted a copy: %1 new %2 (#%3%4)"_s.arg(made.size())
                      .arg(plural(made.size(), u"province"_s, u"provinces"_s))
                      .arg(made.front())
                      .arg(made.size() > 1 ? u"–#%1"_s.arg(made.back()) : QString());
    if (people)
        msg += u", %1 people"_s.arg(QLocale(QLocale::English).toString(people));
    if (!states.isEmpty())
        msg += u", new state%1 %2"_s.arg(states.size() == 1 ? QString() : u"s"_s, states.join(u", "_s));
    msg += u" — history, owner, cores, terrain, rivers, land colours, names and ports copied. Written on Apply; Ctrl+Z undoes it all, "
           "Ctrl+V pastes another copy."_s;
    if (outside)
        msg += u" ⚠ %1 px fell off the map edge."_s.arg(outside);
    if (!gone.empty())
        p._warn_gone(gone);
    return msg;
}

}  // namespace provclip
