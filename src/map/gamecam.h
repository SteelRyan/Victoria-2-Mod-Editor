// The game's map camera and what each height shows, as the game does it (C++ only, 2026-11-09; measured and
// checked in the game).
//
// The camera stands at height h above the map point (x, z) (map pixels: x east, z north = rows counted from the
// map's bottom) and looks at the point 30 px north of it at height 12; 45° field of view in the vertical, the
// screen's own aspect: the same piece of map at every resolution of the same shape. Close up it looks at a slant
// (47° at 40), far out almost straight down (1.7° at 1000).
//   counters (army / fleet boxes)   while h < 500 (fixed in the exe)
//   province and sea names          while h < counter_distance (the PLAYER's settings.txt)
//   flat "paper" map                from terrain_sheet_heights on (map/default.map)
//   province borders                strong below border_heights[0], faint above, none above border_cutoff
#pragma once
#include <QPointF>
#include <QString>
#include <optional>
#include <vector>

namespace gamecam {

constexpr double FOV = 0.785;                        // rad, vertical (D3DXMatrixPerspectiveFovLH)
constexpr double LOOK_Y = 12.0, LOOK_Z = 30.0;       // the point looked at, from the camera's foot
constexpr double MIN_H = 40.0, MAX_H = 1000.0, START_H = 330.0;
constexpr double COUNTERS_BELOW = 500.0;              // 0xE45B50

// one notch of the mouse wheel at the player's zoom speed (0 … 100, the game's default 50): × 0.9 in, × 1.2 out
double wheel(double h, int notches, double zoom_speed = 50.0, double min_h = MIN_H);

struct View {
    double x = 0, z = 0;                             // the camera's foot on the map
    double h = START_H;
    double w = 1920, hgt = 1080;                     // the screen in pixels (only its shape matters for the map)
};
std::optional<QPointF> to_map(const View &v, double sx, double sy);   // screen pixel -> map point (None: sky)
std::optional<QPointF> to_screen(const View &v, double mx, double mz); // map point -> screen pixel (None: behind)
double px_per_unit(const View &v, double mx, double mz);               // screen pixels per map pixel there

// default.map and the player's settings
struct Levels {
    std::vector<double> borders{500, 800, 1100};     // border_heights
    double sheet = 500;                              // terrain_sheet_heights (the first)
    double cutoff = 1100;                            // border_cutoff
    double tree = 350;                               // (no visible effect found)
    double names = 300;                              // counter_distance (the player's)
};
struct Shown {
    bool counters = false, names = false, flat = false, borders = false;
    int border_step = 0;                             // 0 strong, 1 … after each border height passed
};
Shown shown(const Levels &lv, double h);

// the levels, for lists and rulers: what each does, in words measured in the game
struct Level {
    QString key;                                     // "border_heights:0", "terrain_sheet_heights:0", "border_cutoff",
                                                     // "tree", "counters", "names"
    QString name, does;
    double height = 0;
    bool active_below = true;                        // what it describes is there below the height
    bool in_mod = true;                              // map/default.map (false: the exe / the player's settings)
};
std::vector<Level> levels(const Levels &lv);

}  // namespace gamecam
