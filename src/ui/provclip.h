// provclip — Drawing → Provinces: cut and paste the selected provinces, the way a paint program cuts and pastes a
// piece of a picture (the user, 2026-11-08: "Control X, it pops away, it's all black … Control V, they are there again,
// but I can move them around … when I click away … it snaps in"; "terrain and rivers should move with the provinces").
//
//   Ctrl+X   the selected provinces / sea tiles leave the map: their pixels become no province (black), the rivers on
//            them go along (one undo step)
//   Ctrl+V   they float over the map where they were, drawn in a colour each; a left-drag ANYWHERE moves them (or
//            nudge with the arrow keys); ⟲ ⟳ turn them a quarter, ⇋ ⇵ mirror them (2026-11-09, the user: "I want a okay
//            check … only when I press it do the provinces lock in … rotate them as well as flip them")
//   ✓ OK     (the bar at the map's bottom right, or the Cut & paste tool's box) — ONLY this places them:
//            provinces.bmp, terrain.bmp and rivers.bmp get the cut pixels at the new place, the land colour map
//            (colormap.dds) its colours under them plus 5 of its pixels around them (2026-11-17), positions.txt is moved /
//            turned / mirrored along (names and ports keep facing the same way on the land), one undo step. Same
//            numbers, same history, owners, pops — only the place changes. What lay there before is painted over.
//            Apply (Ctrl+S) while they float places them first (nothing is lost).
//   Esc / ✕  while they float: back where they were cut from, as they were
//   Ctrl+C   (2026-11-14, the user: "a copy and paste option along the cut and paste option") the selected provinces
//            stay where they are; Ctrl+V floats a copy (a little off the originals), moved / turned the same way,
//            and ✓ OK makes it NEW provinces: each a new number and colour, the history of its original (without
//            buildings: owner, cores, RGO, life rating), its pops, its terrain / rivers / positions moved along; the
//            copies of one state get a new state of their own unless they touch that state. All of it ONE undo
//            step. The copy stays held: Ctrl+V again pastes another. Esc / ✕ only drops the floating copy.
// Not in the Python editor (new, C++ only).
#pragma once
#include "core/positions.h"
#include "ui/mapview.h"

#include <QImage>
#include <array>
#include <QString>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace drawpanel {
class DrawPanel;
}

namespace provclip {

class Clip {
public:
    explicit Clip(drawpanel::DrawPanel &p) : p(p) {}
    drawpanel::DrawPanel &p;
    bool held() const { return !pids.empty(); }
    bool floating() const { return float_; }
    bool copying() const { return copy_; }           // held provinces are a copy (Ctrl+C), not cut
    QString cut();                                   // -> the message for the status line
    QString copy();
    QString paste();
    bool contains(double x, double y) const;         // a pixel of the floating provinces
    void grab(double x, double y);
    bool dragging() const { return grab_at.has_value(); }
    void drag(double x, double y);
    void drop() { grab_at.reset(); }
    void nudge(long long dx, long long dy);
    void rotate(bool clockwise);                     // a quarter turn about their middle
    void flip(bool horizontal);                      // mirrored left-right (horizontal) or top-bottom
    bool turned() const { return m[0] != 1 || m[1] != 0 || m[2] != 0 || m[3] != 1; }
    size_t count() const { return pids.size(); }
    QString place();                                 // put them down where they float (one undo step)
    QString back();                                  // Esc: where they were cut from
    void forget();
    std::vector<mapview::Shape> shapes() const;
    void show();
    // what a positions.txt entry becomes when placed with the turn m, from the cut box (bx, by, w0 × h0) to the box
    // at (ox, oy) — points moved / turned, text_rotation and naval_rot turned along (public for the check)
    static positions::Entry turn_entry(const positions::Entry &e, const std::array<int, 4> &m, long long map_h,
                                       long long bx, long long by, long long w0, long long h0, long long ox,
                                       long long oy);

private:
    std::vector<long long> pids;
    bool float_ = false;
    bool copy_ = false;
    long long bx = 0, by = 0, bw = 0, bh = 0;        // the box where they float now (bw × bh: after the turns)
    long long w0 = 0, h0 = 0;                        // the box they were cut from (at bx, by)
    long long ox = 0, oy = 0;                        // where they float
    long long cx2 = 0, cy2 = 0;                      // twice the middle they turn about (kept, so 4 turns are none)
    std::array<int, 4> m{1, 0, 0, 1};                // the turn so far (screen x right, y down): [a b; c d]
    long long version = -1;                          // provinces.bmp's version right after the cut
    std::vector<std::int32_t> off0;                  // their pixels as cut: y * w0 + x
    std::vector<std::int32_t> off;                   // their pixels now: y * bw + x in the box
    std::vector<std::uint16_t> ids;
    std::vector<std::uint8_t> terr, riv, mask;
    bool has_terr = false, has_riv = false;
    // the land colours (map/terrain/colormap.dds) under them and around them — the user 2026-11-17: "it has to take the
    // correct color hue with it ... the selected area plus 4 or 5 pixels more on the outline": the colour map as cut,
    // and which of its pixels go along (those under the provinces + CM_MARGIN colour-map pixels around them)
    bool has_cm = false;
    QImage cm_pic;                                   // RGBA8888, file rows (row 0 = south)
    std::vector<std::uint8_t> cm_mask;
    long long cm_w = 0, cm_h = 0;
    void _grab_colours(const std::vector<std::int64_t> &idx);
    bool _colours(QImage &out, QString &path) const;   // the colour map with them laid in where they float
    QImage img;
    std::optional<std::pair<double, double>> grab_at;
    long long gx = 0, gy = 0;
    void _turn(const std::array<int, 4> &op);
    std::vector<long long> _wanted() const;          // the picked provinces / sea tiles with pixels on the map
    std::vector<std::int64_t> _grab(const std::vector<long long> &take);   // box, pixels, terrain, rivers; -> map indexes
    QString _place_copy();
    void _lay_out();                                 // off / mask / img from off0 and m
};

}  // namespace provclip
