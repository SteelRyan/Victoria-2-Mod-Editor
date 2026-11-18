// The game's map camera (see gamecam.h).
#include "map/gamecam.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace gamecam {

double wheel(double h, int notches, double zoom_speed, double min_h)
{
    // 0x63FA52: s = h / 60 * 0.8 * speed / 100; in: h - 20 * 0.75 * s, out: h + 20 * 1.5 * s; then held in [min, 1000]
    for (int i = 0; i < std::abs(notches); ++i) {
        const double s = h / 60.0 * 0.8 * zoom_speed / 100.0;
        h = notches > 0 ? std::max(min_h, h - 20.0 * 0.75 * s) : std::min(MAX_H, h + 20.0 * 1.5 * s);
    }
    return h;
}

namespace {
struct Axes {                                         // D3DXMatrixLookAtLH(eye (0,h,0), at (0,12,30), up (0,0,1))
    double zy, zz;                                    // the view direction (x part 0)
    double t;                                         // tan(fov / 2)
};
Axes axes(const View &v)
{
    const double dy = LOOK_Y - v.h, dz = LOOK_Z, n = std::sqrt(dy * dy + dz * dz);
    return {dy / n, dz / n, std::tan(FOV / 2.0)};
}
}  // namespace

// camera axes: x = (1, 0, 0), y = (0, zz, -zy), z = (0, zy, zz)
std::optional<QPointF> to_map(const View &v, double sx, double sy)
{
    const Axes a = axes(v);
    const double aspect = v.w / v.hgt;
    const double rx = (2.0 * sx / v.w - 1.0) * a.t * aspect, ry = (1.0 - 2.0 * sy / v.hgt) * a.t;
    const double dy = ry * a.zz + a.zy;               // the ray's world y (x * 0 + y-axis * ry + z-axis * 1)
    if (dy >= -1e-9)
        return std::nullopt;
    const double s = v.h / -dy;
    return QPointF(v.x + s * rx, v.z + s * (ry * -a.zy + a.zz));
}

std::optional<QPointF> to_screen(const View &v, double mx, double mz)
{
    const Axes a = axes(v);
    const double X = mx - v.x, Y = -v.h, Z = mz - v.z;
    const double cx = X, cy = Y * a.zz + Z * -a.zy, cz = Y * a.zy + Z * a.zz;
    if (cz <= 1e-6)
        return std::nullopt;
    const double aspect = v.w / v.hgt;
    const double nx = cx / (cz * a.t * aspect), ny = cy / (cz * a.t);
    return QPointF((nx + 1.0) / 2.0 * v.w, (1.0 - ny) / 2.0 * v.hgt);
}

double px_per_unit(const View &v, double mx, double mz)
{
    const auto p = to_screen(v, mx, mz), q = to_screen(v, mx + 1.0, mz);
    if (!p || !q)
        return 0.0;
    return std::abs(q->x() - p->x());
}

Shown shown(const Levels &lv, double h)
{
    Shown s;
    s.counters = h < COUNTERS_BELOW;
    s.names = h < lv.names;
    s.flat = h >= lv.sheet;
    s.borders = h < lv.cutoff;
    for (double b : lv.borders)
        if (h >= b)
            s.border_step += 1;
    return s;
}

std::vector<Level> levels(const Levels &lv)
{
    std::vector<Level> out;
    for (size_t i = 0; i < lv.borders.size(); ++i)
        out.push_back({u"border_heights:%1"_s.arg(i),
                       i == 0 ? u"Strong province borders"_s : u"Thinner borders (step %1)"_s.arg(i + 1),
                       i == 0 ? u"Below it the province borders are strong dark lines; above it they turn faint and thin."_s
                              : u"Above it the province borders are drawn thinner once more (hard to see on a normal "
                                u"screen)."_s,
                       lv.borders[i], i == 0, true});
    out.push_back({u"terrain_sheet_heights:0"_s, u"Flat paper map"_s,
                   u"Above it the map turns into the flat paper map: in the political view plain country colours with "
                   u"the big country names, in the terrain view flat colours with dark coasts."_s,
                   lv.sheet, false, true});
    out.push_back({u"border_cutoff"_s, u"Province borders"_s,
                   u"Above it no province borders are drawn at all (coasts and country edges stay)."_s, lv.cutoff, true,
                   true});
    out.push_back({u"tree"_s, u"Trees (no effect seen)"_s,
                   u"No visible effect found in the game (150 against 350: the same picture)."_s, lv.tree, true, true});
    out.push_back({u"counters"_s, u"Unit counters"_s,
                   u"The boxes next to armies and fleets are drawn below 500. Fixed in the game: no file changes it."_s,
                   COUNTERS_BELOW, true, false});
    out.push_back({u"names"_s, u"Province names"_s,
                   u"Province and sea names are drawn below it: counter_distance in the player's own settings "
                   u"(the game's options), not in the mod."_s,
                   lv.names, true, false});
    return out;
}

}  // namespace gamecam
