// The game's screen drawn by the editor (see gameview.h).
#include "ui/gameview.h"
#include "core/moddata.h"
#include "map/ddsimage.h"
#include "map/mapview_data.h"
#include "ui/theme.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;

namespace gameview {

namespace {
long long wrap(long long x, long long w) { return ((x % w) + w) % w; }
QRgb mix(QRgb c, double k, int r, int g, int b)     // c toward (r, g, b) by k
{
    return qRgb(int(qRed(c) + (r - qRed(c)) * k), int(qGreen(c) + (g - qGreen(c)) * k),
                int(qBlue(c) + (b - qBlue(c)) * k));
}
QRgb times(QRgb c, double k)
{
    auto f = [k](int v) { return std::clamp(int(v * k), 0, 255); };
    return qRgb(f(qRed(c)), f(qGreen(c)), f(qBlue(c)));
}
// the colour map at a map point, smoothed between its pixels (the colour maps are smaller than the map: read plainly,
// every colour-map pixel showed as a square block)
QRgb sample(const QImage &img, double fx, double fz, long long W, long long H)
{
    const int iw = img.width(), ih = img.height();
    double u = fx * iw / double(W) - 0.5;
    if (u < 0 || u >= iw)
        u -= std::floor(u / iw) * iw;
    const double v = (double(H) - fz) * ih / double(H) - 0.5;
    const int x0 = int(u), y0 = int(std::floor(v));
    const int ax = int((u - x0) * 256), ay = int((v - y0) * 256);   // (weights in 1/256: fast enough to drag the map)
    const int x1 = x0 + 1 < iw ? x0 + 1 : 0;
    const int ya = std::clamp(y0, 0, ih - 1), yb = std::clamp(y0 + 1, 0, ih - 1);
    const QRgb *ra = reinterpret_cast<const QRgb *>(img.constScanLine(ya));
    const QRgb *rb = reinterpret_cast<const QRgb *>(img.constScanLine(yb));
    const QRgb a = ra[x0], b = ra[x1], c = rb[x0], d = rb[x1];
    auto ch = [&](int sh) {
        const int top = int((a >> sh) & 255) * (256 - ax) + int((b >> sh) & 255) * ax;
        const int bot = int((c >> sh) & 255) * (256 - ax) + int((d >> sh) & 255) * ax;
        return (top * (256 - ay) + bot * ay + 32768) >> 16;
    };
    return qRgb(ch(16), ch(8), ch(0));
}
// the game lights its terrain: the colour maps (photographs in many mods) come out lighter on screen than in the file
struct Lit {
    unsigned char t[256];
    explicit Lit(double k)
    {
        for (int v = 0; v < 256; ++v)
            t[v] = static_cast<unsigned char>(std::clamp(int(std::lround(255.0 * std::pow(v / 255.0, 0.8) * k)), 0, 255));
    }
    QRgb operator()(QRgb c) const { return qRgb(t[qRed(c)], t[qGreen(c)], t[qBlue(c)]); }
};
const Lit LIT_LAND(1.1), LIT_SEA(1.12);
}  // namespace

std::shared_ptr<World> make_world(const moddata::Mod &mod, const mapview::MapData &md, const OwnerOf &owner_of)
{
    auto w = std::make_shared<World>();
    w->W = md.w;
    w->H = md.h;
    w->version = md.version;
    const std::pair<QString, QImage *> pics[2] = {{u"colormap.dds"_s, &w->land}, {u"colormap_water.dds"_s, &w->water}};
    for (const auto &[name, img] : pics)
        if (const auto got = ddsimage::load_dds(mod.p({u"map"_s, u"terrain"_s, name})); got && !got->isNull())
            *img = got->convertToFormat(QImage::Format_RGB32).flipped(Qt::Vertical);   // (stored south up)
    const size_t n = md.cx.size();
    w->colour.assign(n, 0);
    w->owner.assign(n, -1);
    w->names.assign(n, QString());
    QHash<QString, int> at;
    for (size_t pid = 1; pid < n; ++pid) {
        if (const QString *t = mod.loc.get(u"PROV%1"_s.arg(pid)); t && !t->isEmpty())
            w->names[pid] = *t;
        else if (const moddata::Province *pr = mod.provinces.get(qint64(pid)))
            w->names[pid] = pr->name;
        if (pid < md.sea.size() && md.sea[pid])
            continue;
        const auto tag = owner_of(qint64(pid));
        if (!tag || tag->isEmpty())
            continue;
        int k = at.value(*tag, -1);
        if (k < 0) {
            k = int(w->tags.size());
            at.insert(*tag, k);
            w->tags << *tag;
            const moddata::Country *c = mod.countries.get(*tag);
            const QString *nm = mod.loc.get(*tag);
            w->tag_names << (nm && !nm->isEmpty() ? *nm : c && !c->name.isEmpty() ? c->name : *tag);
        }
        w->owner[pid] = k;
        const moddata::Country *c = mod.countries.get(*tag);
        w->colour[pid] = c ? qRgb(std::get<0>(c->color), std::get<1>(c->color), std::get<2>(c->color)) : qRgb(160, 160, 160);
    }
    // the big names: one per piece of a country (provinces joined by land; pieces of at least a quarter of its
    // biggest), along the piece's long side (area-weighted spread of its provinces)
    std::vector<size_t> up(n);
    for (size_t i = 0; i < n; ++i)
        up[i] = i;
    auto root = [&](size_t i) {
        while (up[i] != i)
            i = up[i] = up[up[i]];
        return i;
    };
    for (size_t pid = 1; pid < n; ++pid) {
        if (w->owner[pid] < 0)
            continue;
        for (long long nb : md.neighbours(qint64(pid)))
            if (nb > 0 && size_t(nb) < n && w->owner[size_t(nb)] == w->owner[pid])
                up[root(size_t(nb))] = root(pid);
    }
    struct Acc {
        double a = 0, x = 0, z = 0, xx = 0, zz = 0, xz = 0;
        int tag = -1;
    };
    std::map<size_t, Acc> acc;                       // by piece (its root province)
    for (size_t pid = 1; pid < n; ++pid) {
        const int k = w->owner[pid];
        if (k < 0 || md.cx[pid] < 0 || pid >= md.area.size() || md.area[pid] <= 0)
            continue;
        const double a = double(md.area[pid]), x = md.cx[pid], z = double(md.h) - 1 - md.cy[pid];
        const double bw = md.x1[pid] - md.x0[pid] + 1, bh = md.y1[pid] - md.y0[pid] + 1;
        Acc &c = acc[root(pid)];
        c.tag = k;
        c.a += a;
        c.x += a * x;
        c.z += a * z;
        c.xx += a * (x * x + bw * bw / 12.0);
        c.zz += a * (z * z + bh * bh / 12.0);
        c.xz += a * x * z;
    }
    std::vector<double> biggest(size_t(w->tags.size()), 0.0);
    for (const auto &[r, c] : acc)
        biggest[size_t(c.tag)] = std::max(biggest[size_t(c.tag)], c.a);
    for (const auto &[r, c] : acc) {
        if (c.a < std::max(0.25 * biggest[size_t(c.tag)], 300.0))
            continue;
        World::Label &l = w->labels.emplace_back();
        l.tag = c.tag;
        l.x = c.x / c.a;
        l.z = c.z / c.a;
        const double sxx = c.xx / c.a - l.x * l.x, szz = c.zz / c.a - l.z * l.z, sxz = c.xz / c.a - l.x * l.z;
        const double th = 0.5 * std::atan2(2 * sxz, sxx - szz);
        const double mid = (sxx + szz) / 2, d = std::sqrt(std::max(0.0, (sxx - szz) * (sxx - szz) / 4 + sxz * sxz));
        l.len = std::sqrt(12.0 * std::max(0.0, mid + d));
        l.dx = std::cos(th);
        l.dz = std::sin(th);
        if (l.dx < 0) {
            l.dx = -l.dx;
            l.dz = -l.dz;
        }
    }
    return w;
}

int owner_at(const World &w, const mapview::MapData &md, double x, double z)
{
    const long long row = w.H - 1 - (long long)std::floor(z), col = wrap((long long)std::floor(x), std::max(1LL, w.W));
    if (row < 0 || row >= md.h || col >= md.w)
        return -1;
    const size_t pid = md.idmap(row, col);
    return pid < w.owner.size() ? w.owner[pid] : -1;
}

QPointF start_point(const World &w, const moddata::Mod &mod, const mapview::MapData &md)
{
    const World::Label *best = nullptr;
    for (const auto &[tag, c] : mod.countries) {
        const qsizetype k = w.tags.indexOf(tag);
        if (k < 0)
            continue;
        for (const auto &l : w.labels)
            if (l.tag == k && (!best || l.len > best->len))
                best = &l;
        if (best)
            break;
    }
    return best ? QPointF(best->x, best->z - 30.0) : QPointF(md.w / 2.0, md.h * 0.6);
}

GameView::GameView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(240, 135);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
}

int GameView::heightForWidth(int w) const { return int(w * double(screen.height()) / screen.width()); }

void GameView::set_view(double x, double z, double h)
{
    if (x == cam.x && z == cam.z && h == cam.h)
        return;
    cam.x = x;
    cam.z = z;
    cam.h = h;
    update();
}

void GameView::set_levels(const gamecam::Levels &lv)
{
    levels = lv;
    update();
}

QRectF GameView::screen_rect() const
{
    const double a = double(screen.width()) / screen.height();
    double w = width(), h = w / a;
    if (h > height()) {
        h = height();
        w = h * a;
    }
    return {(width() - w) / 2.0, (height() - h) / 2.0, std::floor(w), std::floor(h)};
}

void GameView::_render(int rw, int rh)
{
    const World &wd = *world;
    const long long W = wd.W, H = wd.H;
    gamecam::View v = cam;
    v.w = rw;
    v.hgt = rh;
    const gamecam::Shown s = gamecam::shown(levels, cam.h);
    const size_t N = size_t(rw) * size_t(rh);
    std::vector<int> pid(N, -1);
    std::vector<float> fx(N, 0.f), fz(N, 0.f);
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x) {
            const size_t i = size_t(y) * rw + x;
            const auto m = gamecam::to_map(v, x + 0.5, y + 0.5);
            if (!m)
                continue;
            const long long row = H - 1 - (long long)std::floor(m->y());
            if (row < 0 || row >= H)
                continue;
            const long long col = wrap((long long)std::floor(m->x()), W);
            pid[i] = md->idmap(row, col);
            fx[i] = float(m->x());
            fz[i] = float(m->y());
        }
    auto is_sea = [&](int p) { return p >= 0 && size_t(p) < md->sea.size() && md->sea[size_t(p)]; };
    _map = QImage(rw, rh, QImage::Format_RGB32);
    for (int y = 0; y < rh; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(_map.scanLine(y));
        for (int x = 0; x < rw; ++x) {
            const size_t i = size_t(y) * rw + x;
            const int p = pid[i];
            if (p < 0) {
                line[x] = qRgb(12, 16, 24);
                continue;
            }
            QRgb c;
            if (is_sea(p) || p == 0) {
                if (s.flat && political) {                       // the paper map's sea: blue with a grid (seen: ~26 px)
                    c = qRgb(88, 118, 155);
                    const bool gx = x + 1 < rw && std::floor(fx[i] / 26) != std::floor(fx[i + 1] / 26);
                    const bool gz = y + 1 < rh && std::floor(fz[i] / 26) != std::floor(fz[i + size_t(rw)] / 26);
                    if (gx || gz)
                        c = qRgb(72, 100, 136);
                } else {
                    c = wd.water.isNull() ? qRgb(38, 60, 92) : LIT_SEA(sample(wd.water, fx[i], fz[i], W, H));
                    if (political)
                        c = times(c, 1.3);                       // (the political view's sea is lighter)
                    else if (s.flat)
                        c = times(c, 0.8);
                }
            } else {
                const QRgb t = wd.land.isNull() ? qRgb(118, 128, 76) : LIT_LAND(sample(wd.land, fx[i], fz[i], W, H));
                const QRgb own = size_t(p) < wd.colour.size() ? wd.colour[size_t(p)] : 0;
                if (!political) {
                    c = s.flat ? times(mix(t, 0.25, 140, 140, 120), 0.9) : t;
                } else if (s.flat) {
                    c = own ? times(mix(own, 0.35, 205, 200, 188), 0.92) : qRgb(222, 220, 212);
                } else {
                    const double lum = (qRed(t) * 0.3 + qGreen(t) * 0.59 + qBlue(t) * 0.11) / 110.0;
                    c = times(own ? own : qRgb(170, 170, 165), std::clamp(0.5 + 0.25 * lum, 0.5, 0.92));
                }
            }
            line[x] = c;
        }
    }
    // the lines between provinces, countries and the coasts (where neighbouring screen pixels show other provinces)
    auto owner = [&](int p) { return size_t(p) < wd.owner.size() ? wd.owner[size_t(p)] : -1; };
    const double prov = !s.borders ? 0.0 : s.border_step == 0 ? 0.62 : s.border_step == 1 ? 0.3 : 0.2;
    std::vector<float> dark(N, 0.f);
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x) {
            const size_t i = size_t(y) * rw + x;
            const int a = pid[i];
            if (a < 0)
                continue;
            for (int d = 0; d < 2; ++d) {
                if ((d == 0 && x + 1 >= rw) || (d == 1 && y + 1 >= rh))
                    continue;
                const size_t j = d == 0 ? i + 1 : i + size_t(rw);
                const int b = pid[j];
                if (b < 0 || b == a)
                    continue;
                const bool sa = is_sea(a) || a == 0, sb = is_sea(b) || b == 0;
                double k;
                if (sa != sb)
                    k = 0.72;                                    // coast
                else if (!sa && owner(a) != owner(b))
                    k = political ? 0.8 : 0.5;                   // a country's edge
                else
                    k = !sa ? prov : s.flat ? 0.0 : prov * 0.45; // a province's edge (none between seas on paper)
                if (k <= 0)
                    continue;
                dark[i] = std::max(dark[i], float(k));
                if (k >= 0.6)                                     // strong lines two pixels wide
                    dark[j] = std::max(dark[j], float(k * 0.8));
            }
        }
    for (int y = 0; y < rh; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(_map.scanLine(y));
        for (int x = 0; x < rw; ++x)
            if (const float k = dark[size_t(y) * rw + x]; k > 0)
                line[x] = mix(line[x], k, 18, 16, 14);
    }
}

void GameView::_names(QPainter &p, const gamecam::View &v, const QRectF &r)
{
    const World &wd = *world;
    QFont f(u"Times New Roman"_s);
    f.setBold(true);
    const bool flat = gamecam::shown(levels, cam.h).flat;
    for (size_t pid = 1; pid < wd.names.size() && pid < md->cx.size(); ++pid) {
        const QString &name = wd.names[pid];
        if (name.isEmpty() || md->cx[pid] < 0)
            continue;
        double x = md->cx[pid];
        x += double(wd.W) * std::round((cam.x - x) / double(wd.W));
        const double z = double(wd.H) - 1 - md->cy[pid];
        const auto at = gamecam::to_screen(v, x, z);
        if (!at || at->x() < -60 || at->y() < -20 || at->x() > v.w + 60 || at->y() > v.hgt + 20)
            continue;
        // (seen in the game at 100: about 3.5 map px high, smaller where the province is too narrow for the name)
        const bool sea = pid < md->sea.size() && md->sea[pid];
        const double bw = md->x1[pid] - md->x0[pid] + 1, len = std::max<qsizetype>(3, name.size());
        const double fit = std::min(sea ? 3.8 : 3.6, 0.9 * bw / (len * 0.57));
        if (fit < 1.0)
            continue;
        // a doubled map font: the names twice as big on the map (seen 2026-11-09, a 4K submod with names × 2)
        const double hw = fit * names_factor;
        const double px = std::min(90.0 * names_factor, hw * gamecam::px_per_unit(v, x, z));
        if (px < 4.5)
            continue;
        f.setPixelSize(int(std::round(px)));
        p.setFont(f);
        const QFontMetricsF fm(f);
        // the game's dark ink, with a soft light rim so a name stays readable on dark land and sea
        const QPointF o(r.left() + at->x() - fm.horizontalAdvance(name) / 2.0, r.top() + at->y() + fm.capHeight() / 2.0);
        QPainterPath path;
        path.addText(o, f, name);
        p.setPen(QPen(sea ? QColor(200, 222, 245, 120) : QColor(245, 236, 210, 130), std::max(1.5, px * 0.16),
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(sea ? (flat ? QColor(40, 62, 98) : QColor(30, 52, 88)) : QColor(24, 18, 10));
        p.drawPath(path);
    }
}

void GameView::_labels(QPainter &p, const gamecam::View &v, const QRectF &r)
{
    const World &wd = *world;
    QFont f(u"Times New Roman"_s);
    f.setBold(true);
    f.setLetterSpacing(QFont::PercentageSpacing, 125);
    for (const World::Label &l : wd.labels) {
        if (l.len <= 0 || l.tag < 0)
            continue;
        double x = l.x + double(wd.W) * std::round((cam.x - l.x) / double(wd.W));
        const double half = l.len * 0.36;
        const auto a = gamecam::to_screen(v, x - l.dx * half, l.z - l.dz * half);
        const auto b = gamecam::to_screen(v, x + l.dx * half, l.z + l.dz * half);
        if (!a || !b)
            continue;
        const QPointF d = *b - *a;
        const double L = std::hypot(d.x(), d.y());
        if (L < 26)
            continue;
        const QString text = wd.tag_names[qsizetype(l.tag)].toUpper();
        f.setPixelSize(100);
        const double tw = QFontMetricsF(f).horizontalAdvance(text);
        const double px = std::min({100.0 * L / std::max(1.0, tw), L * 0.3, 110.0});
        if (px < 7)
            continue;
        f.setPixelSize(int(std::round(px)));
        const QFontMetricsF fm(f);
        const QPointF mid = (*a + *b) / 2.0 + r.topLeft();
        if (!r.adjusted(-200, -200, 200, 200).contains(mid))
            continue;
        p.save();
        p.translate(mid);
        p.rotate(std::atan2(d.y(), d.x()) * 180.0 / M_PI);
        p.setFont(f);
        p.setPen(QColor(56, 42, 28, 200));
        p.drawText(QPointF(-fm.horizontalAdvance(text) / 2.0, fm.capHeight() / 2.0), text);
        p.restore();
    }
}

void GameView::_counters(QPainter &p, const gamecam::View &v, const QRectF &r)
{
    // sample armies: every 17th owned land province in view (the real ones stand where the history puts them)
    const World &wd = *world;
    const double k = r.width() / screen.width() * counter_factor;
    int shown = 0;
    for (size_t pid = 1; pid < wd.owner.size() && pid < md->cx.size() && shown < 40; ++pid) {
        if (wd.owner[pid] < 0 || pid % 17 != 5 || md->cx[pid] < 0)
            continue;
        double x = md->cx[pid];
        x += double(wd.W) * std::round((cam.x - x) / double(wd.W));
        const auto at = gamecam::to_screen(v, x, double(wd.H) - 1 - md->cy[pid]);
        if (!at || at->x() < 0 || at->y() < 0 || at->x() > v.w || at->y() > v.hgt)
            continue;
        ++shown;
        p.save();
        p.translate(r.left() + at->x() - 10 * k, r.top() + at->y() - counter_size.height() * k);
        p.scale(k, k);
        if (counter) {
            counter(p);
        } else {
            p.fillRect(QRectF(0, 0, counter_size.width(), 20), QColor(60, 50, 40));
            p.setPen(QPen(QColor(220, 200, 150), 1));
            p.drawRect(QRectF(0, 0, counter_size.width(), 20));
        }
        p.restore();
    }
}

void GameView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(theme::BG));
    const QRectF r = screen_rect();
    if (!md || !world || world->W <= 0) {
        p.setPen(QColor(theme::INK_DIM));
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, u"The map is not loaded."_s);
        return;
    }
    const int rw = int(r.width()), rh = int(r.height());
    std::vector<double> key{cam.x, cam.z, cam.h, double(rw), double(rh), political ? 1.0 : 0.0, levels.sheet,
                            levels.cutoff, levels.names, double(world->version)};
    key.insert(key.end(), levels.borders.begin(), levels.borders.end());
    key.push_back(double(player));
    if (key != _key || _map.isNull()) {
        _render(rw, rh);
        _key = key;
    }
    p.drawImage(r.topLeft(), _map);
    p.setClipRect(r);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    gamecam::View v = cam;
    v.w = rw;
    v.hgt = rh;
    const gamecam::Shown s = gamecam::shown(levels, cam.h);
    if (s.flat && political)
        _labels(p, v, r);
    if (s.names) {
        p.save();
        _names(p, v, r);
        p.restore();
    }
    if (s.counters)
        _counters(p, v, r);
    const double sc = r.width() / screen.width();
    for (const Overlay &o : windows) {
        if (o.img.isNull())
            continue;
        const double RW = 1920, RH = 1080, w = o.img.width(), h = o.img.height();
        const double x = o.x + w / 2 > RW / 2 ? screen.width() - (RW - o.x - w) * o.factor - w * o.factor : o.x * o.factor;
        const double y = o.y + h / 2 > RH / 2 ? screen.height() - (RH - o.y - h) * o.factor - h * o.factor
                                              : o.y * o.factor;
        p.drawImage(QRectF(r.left() + x * sc, r.top() + y * sc, w * o.factor * sc, h * o.factor * sc), o.img);
    }
    if (!title.isEmpty()) {
        QFont f(font());
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QRectF t(r.left() + 6, r.bottom() - 24, QFontMetricsF(f).horizontalAdvance(title) + 16, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(16, 18, 24, 210));
        p.drawRoundedRect(t, 4, 4);
        p.setPen(QColor(240, 230, 200));
        p.drawText(t, Qt::AlignCenter, title);
    }
    p.setClipping(false);
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}

void GameView::wheelEvent(QWheelEvent *e)
{
    const int notches = e->angleDelta().y() / 120;
    if (notches == 0)
        return;
    cam.h = gamecam::wheel(cam.h, notches);
    update();
    Q_EMIT viewChanged(cam.x, cam.z, cam.h);
    e->accept();
}

void GameView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    _press = e->position();
    _press_cam = cam;
    setCursor(Qt::ClosedHandCursor);
}

void GameView::mouseMoveEvent(QMouseEvent *e)
{
    if (!_press || !world)
        return;
    const QRectF r = screen_rect();
    gamecam::View v = _press_cam;
    v.w = r.width();
    v.hgt = r.height();
    const auto a = gamecam::to_map(v, _press->x() - r.left(), _press->y() - r.top());
    const auto b = gamecam::to_map(v, e->position().x() - r.left(), e->position().y() - r.top());
    if (!a || !b)
        return;
    double x = _press_cam.x + (a->x() - b->x());
    x = std::fmod(x, double(world->W));
    if (x < 0)
        x += double(world->W);
    const double z = std::clamp(_press_cam.z + (a->y() - b->y()), 0.0, double(world->H));
    cam.x = x;
    cam.z = z;
    update();
    Q_EMIT viewChanged(cam.x, cam.z, cam.h);
}

void GameView::mouseReleaseEvent(QMouseEvent *)
{
    _press.reset();
    setCursor(Qt::OpenHandCursor);
}

}  // namespace gameview
