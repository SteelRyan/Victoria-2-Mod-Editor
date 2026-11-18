// autofix.py — automatic repairs for the Errors tab ("Fix this one" / "Fix all"): what each fix changes, worked out
// here, and applied by the Errors tab as normal undoable edits (written on Apply).
//   rivers.bmp spots   river_repairs()     rivers on water, 2x2 blocks, diagonal joints, misplaced sources / merges
//   stray pixel bits   piece_owner()       a small separate bit goes to the province most of its neighbours belong to
//   unknown colours    colour_areas()      each area of a colour no province uses goes to the province around it
//   no history file    history_folder()    where an empty history file for a province goes
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "grid/grid.h"
#include "map/mapview_data.h"

#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace autofix {

using Flat = std::vector<std::int64_t>;

// Pixels of rivers.bmp that repair its rule problems -> (flat indexes, new values). points: [(x, y)] — only problems
// within 2 px of these (empty: everywhere).
std::pair<Flat, std::vector<std::uint8_t>> river_repairs(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R,
                                                         const std::vector<std::pair<double, double>> &points = {},
                                                         int rounds = 6);

// The id most 4-neighbours of the pixels have — not `own`, not 0; None if none.
std::optional<long long> _vote(const grid::Grid<std::uint16_t> &idm, const Flat &ys, const Flat &xs, long long own);
// (flat indexes of the separate bit of `pid` at pixel (x, y), the id most of its neighbours have) — None when there's
// no such bit or no neighbour to give it to.
std::optional<std::pair<Flat, long long>> piece_owner(const grid::Grid<std::uint16_t> &idm, long long pid, double x,
                                                      double y, long long radius = 24);
Flat piece_pixels(const grid::Grid<std::uint16_t> &idm, long long pid, double x, double y, long long radius = 24);

// provinces.bmp's colours in the map view's orientation, as 24-bit keys (r << 16 | g << 8 | b) per pixel (Python: an
// (h, w, 3) array; its one user, colour_areas, keys them the same way). Throws when the size differs from `h` x `w`.
grid::Grid<std::int32_t> file_colours(const QString &bmp_path, long long h, long long w);
// The same, kept while the file stays the same (path, time, size): the map check asks after every stroke, and the file
// only changes on Apply (C++ only, 2026-11-14 — ZRM's Legacy read its 36 MB provinces.bmp again after each stroke).
std::shared_ptr<const grid::Grid<std::int32_t>> file_colours_kept(const QString &bmp_path, long long h, long long w);
// 4-connected areas of a bool array -> (label per pixel, 0 = outside; number of areas)
std::pair<grid::Grid<std::int32_t>, long long> labels4(const grid::Grid<std::uint8_t> &mask);
// [(flat indexes, id most of their neighbours have)] for each connected area of pixels that have one of the colours
// in the file and no province in the editor
std::vector<std::pair<Flat, long long>> colour_areas(const grid::Grid<std::uint16_t> &idm,
                                                     const grid::Grid<std::int32_t> &rgb_keys,
                                                     const std::vector<std::tuple<int, int, int>> &colours);

QString history_folder(const moddata::Mod &mod, const mapview::MapData &md, const history::ProvinceHistory &hist,
                       long long pid);
QString history_rel(const moddata::Mod &mod, const mapview::MapData &md, const history::ProvinceHistory &hist,
                    long long pid);

}  // namespace autofix
