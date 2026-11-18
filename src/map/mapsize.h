// mapsize.py, its data half (PLAN.md §4.1): cut columns off the left / right end of the map (or rows off the north /
// south edge). provinces.bmp, terrain.bmp and rivers.bmp get exactly the same cut; map/positions.txt moves with it;
// the colour maps of map/terrain and the minimap are cut by the same share. Which widths the game takes was tried in
// the game itself (2026-10-05): a multiple of 8 whose tile side (the smallest divisor from 58 px up) is 60 … 100 px.
// The page (CutPreview, MapSizeWorkspace) is kind C: ui\.
#pragma once
#include "core/moddata.h"
#include "map/mapview_data.h"

#include <QByteArray>
#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace mapsize {

inline constexpr long long TILE_FROM = 58, TILE_MIN = 60, TILE_MAX = 100;
inline constexpr long long WIDTH_UNIT = 8, HEIGHT_UNIT = 8, MIN_HEIGHT = 512, MIN_WIDTH = 1024;
extern const QStringList BITMAPS, COLORMAPS, MINIMAPS;

std::optional<long long> tile_side(long long width);
bool width_ok(long long width);
bool height_ok(long long height, long long width);
std::vector<long long> allowed_height_cuts(long long height, long long width);
std::vector<long long> allowed_cuts(long long width, std::optional<long long> least = std::nullopt);
// (C++ only, 2026-11-14, the user: "expand the map in map size past its current size") growing: up to GROW_MAX times
// the size; the widths by the same tile rule as a cut (NOT yet tried in the game). apply_cut with negative amounts adds
// columns / rows that continue the edge: each new column repeats the edge column's sea tile (a land pixel there takes
// the nearest sea tile along the edge), each new row the edge row; pictures repeat their edge pixels.
inline constexpr long long GROW_MAX = 2;
std::vector<long long> allowed_grows(long long width);
std::vector<long long> allowed_height_grows(long long height, long long width);
QImage clamp_copy(const QImage &img, long long x, long long y, long long w, long long h);
long long mirror_row(long long y, long long h);       // a row index outside 0 … h-1 mirrored back in at that edge

struct Bmp {                                      // (bytes, offset of the pixels, width, height (signed), bits per pixel)
    QByteArray raw;
    long long off, w, h, bpp;
};
Bmp read_bmp(const QString &path);               // throws py::ValueError
QByteArray cut_bmp(const QByteArray &raw, long long off, long long w, long long h, long long bpp, long long left,
                   long long right);
QByteArray cut_bmp_rows(const QByteArray &raw, long long off, long long w, long long h, long long bpp, long long first,
                        long long last);
bool stored_first_is_north(const mapview::MapData &md, const QByteArray &raw, long long off, long long w, long long h,
                           long long bpp);
bool stored_first_is_north(const grid::Grid<std::uint16_t> &idmap, const QByteArray &raw, long long off, long long w,
                           long long h, long long bpp);
QString move_positions_y(const QString &text, long long south);
std::pair<QString, std::vector<long long>> pull_in_positions(const QString &text, const mapview::MapData &md,
                                                             long long left, long long right, long long north,
                                                             long long south);
std::pair<QString, std::vector<long long>> pull_in_positions(const QString &text, const grid::Grid<std::uint16_t> &idmap,
                                                             long long left, long long right, long long north,
                                                             long long south);
// (C++ only, 2026-11-08) the province map as the files on disk number it now; every x / y pair kept inside w × h
// (distinct_unknown: a colour no definition.csv line has gets a number of its own, counted down from 65534, instead of 0
// — the deleted provinces' pixels a cut is about to take away still show where the map's borders run)
grid::Grid<std::uint16_t> ids_on_disk(const moddata::Mod &mod, bool distinct_unknown = false);
QString keep_inside(const QString &text, long long w, long long h, long long *moved = nullptr);
// colour maps (map/terrain) that don't fit the map's shape, and fitting one by the map's land (C++ only, 2026-11-08)
struct ColourMapOff {
    QString rel, path;
    long long w = 0, h = 0;
    double span = 0;                                 // map columns the picture stands for, by its shape
    double now = 0, cut = 0;                         // (colormaps_to_fit) its match with the land: stretched / cut
};
std::vector<ColourMapOff> colormaps_off(const moddata::Mod &mod, long long map_w, long long map_h);
std::vector<ColourMapOff> colormaps_to_fit(const moddata::Mod &mod, const grid::Grid<std::uint16_t> &idmap,
                                           const std::vector<std::uint8_t> &sea);
std::optional<QImage> decode_dds(const QString &path);
std::optional<std::pair<long long, double>> colormap_fit(const grid::Grid<std::uint16_t> &idmap,
                                                         const std::vector<std::uint8_t> &sea, const QImage &pic,
                                                         double *now = nullptr);   // now: the match as stretched
QByteArray fit_colormap(const QString &path, const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &sea,
                        QString *why);
// a .dds cut in its own format (smaller copies made again); empty + why when it can't be
QByteArray cut_dds(const QString &path, long long a, long long b, long long ta, long long tb, QString *why);
// a new top picture (RGBA, file rows) in the format of the .dds at path (Map → Draw → Colours); empty + why when not
QByteArray recolor_dds(const QString &path, const QImage &pic, QString *why);
bool image_rows_north_first(const QImage &img, const mapview::MapData &md, const std::vector<std::uint8_t> &sea);
bool image_rows_north_first(const QImage &img, const grid::Grid<std::uint16_t> &idmap, const std::vector<std::uint8_t> &sea);
// (C++ only) the refusal text for a cut from the south that leaves a whole number of tiles (crashed the game, tried)
QString south_whole_note(long long nh, long long side);
// (C++ only) land provinces a cut leaves only in the edge it makes sea: they disappear too (the page deletes them first)
std::vector<long long> edge_band_gone(const grid::Grid<std::uint16_t> &idmap, long long n,
                                      const std::vector<std::uint8_t> &sea, long long left, long long right,
                                      long long north, long long south);
std::pair<std::vector<long long>, std::vector<long long>> affected(const mapview::MapData &md, long long left,
                                                                  long long right, long long north = 0, long long south = 0);
std::pair<std::vector<long long>, std::vector<long long>> affected(const grid::Grid<std::uint16_t> &idmap, long long n,
                                                                  long long left, long long right, long long north,
                                                                  long long south);
QString move_positions(const QString &text, long long left, const std::vector<long long> &gone);
struct MapFile {
    QString name, path;
    bool own;
};
std::vector<MapFile> files_of(const moddata::Mod &mod);
std::optional<MapFile> minimap_of(const moddata::Mod &mod);
QImage cut_minimap(const QImage &img, long long map_w, long long left, long long right);
struct Fit {                                      // {"left", "width", "best", "now"}
    long long left, width;
    double best, now;
};
std::optional<Fit> minimap_fit(const mapview::MapData &md, const std::vector<std::uint8_t> &sea, const QImage &img);
QImage refit_minimap(const QImage &img, long long map_w, long long left, long long width);
// -> (written paths, backup folder); throws py::ValueError when the size isn't one the game is known to load.
// Negative amounts GROW the map (all of them ≤ 0, the width or the height — not both).
std::pair<QStringList, QString> apply_cut(moddata::Mod &mod, const mapview::MapData &md, long long left, long long right,
                                          const QString &backup_root, bool check = true, bool minimap = true,
                                          long long north = 0, long long south = 0, bool dry = false);
// (dry: everything is worked out and every refusal raised, nothing is written — the page runs it BEFORE it deletes the
// provinces a cut takes away, so a cut that would be refused leaves the mod as it was)

}  // namespace mapsize
