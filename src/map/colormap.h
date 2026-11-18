// Map → Draw → Colours (C++ only, the user's wish 2026-11-09): the land colour map map/terrain/colormap.dds tinted
// around chosen provinces — "select provinces, then apply a hue to them and a wider area around them, radiating
// outward and getting lighter further out".
//
// Facts the tool rests on (vanilla, read 2026-11-09): the game stretches the colour map over the whole map (1880 × 720
// for a 5616 × 2160 map), its first row is the SOUTH edge; colormap.dds is a plain 32-bit picture without smaller
// copies, other mods' are 16-bit or DXT with smaller copies. Pictures don't count for the checksum. The file is written
// back in its own format with as many smaller copies as before (mapsize::recolor_dds).
//
// My assumption, not measured: "lighter further out" read as a tint that weakens with the distance (smoothly, to
// nothing at the reach), not as a brighter colour.
#pragma once
#include "grid/grid.h"

#include <QImage>
#include <QString>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace colormap {

extern const QString REL;          // map/terrain/colormap.dds
extern const QString WATER_REL;    // map/terrain/colormap_water.dds

// which picture pixel a map pixel shows (the stretch, rows flipped): column and FILE row
struct Stretch {
    long long W = 0, H = 0, cw = 0, ch = 0;
    long long col(long long x) const { return std::min(cw - 1, x * cw / W); }
    long long row(long long y) const { return ch - 1 - std::min(ch - 1, y * ch / H); }   // (file row 0 = south)
    double sx() const { return double(W) / double(cw); }   // map px per picture px
    double sy() const { return double(H) / double(ch); }
};

// per picture pixel: is it on one of the chosen provinces (the map pixel under its middle)?
std::vector<std::uint8_t> chosen_mask(const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &chosen,
                                      const Stretch &s);
// the distance (in map px) of every picture pixel to the nearest masked one (0 on it; huge without any)
std::vector<float> distance(const std::vector<std::uint8_t> &mask, const Stretch &s);
// the tint's weight 0 … 1: 1 inside, falling smoothly to 0 at `reach` map px
float fade(float d, double reach);

struct Tint {
    std::array<int, 3> rgb{200, 160, 90};
    double strength = 0.6;          // 0 … 1: how far the colour moves towards the hue inside
    bool keep_shading = true;       // keep the picture's light and dark (relief, rivers) under the new colour
};
// the brightness the shading is kept against: the mean of the pixels tinted, weighted by w
double mean_luma(const QImage &pic, const std::vector<float> &w);
// out = base (RGBA8888, same size) with weight w[i] × strength of the tint laid on, in picture columns c0 … c1 and
// rows r0 … r1 (exclusive); alpha kept
void tint_into(QImage &out, const QImage &base, const std::vector<float> &w, const Tint &t, double ref, long long c0,
               long long r0, long long c1, long long r1);
// the whole picture tinted (RGBA8888)
QImage tinted(const QImage &pic, const std::vector<float> &w, const Tint &t);
// the soft brush: weight of a pixel d map px from the brush's middle, radius r
float brush_fade(float d, double r);
// the luminance of a colour, as the tint keeps it (Rec. 601)
inline double luma(int r, int g, int b) { return 0.299 * r + 0.587 * g + 0.114 * b; }

}  // namespace colormap
