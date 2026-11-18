// mapview.py, the data half (PLAN.md §4.1): MapData — pixel -> province id lookup plus derived masks, centroids and
// bounding boxes, the neighbours of every province. The widget half (MapView and its layers) is ui\mapview.
// `adjacent` holds CPython sets (grid::PySet): the order they are walked in is Python's, as the literacy replay and
// the map checks need it.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "grid/grid.h"
#include "grid/pyset.h"

#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace mapview {

using Rgb = std::tuple<int, int, int>;
extern const Rgb SEA_RGB;
extern const double WARN_SCALE;
extern const Rgb STATE_BORDER_RGB;
extern const long long SPARE_IDS;             // new provinces per session

class MapData {
public:
    MapData(const moddata::Mod &mod, const QString &bmp_path);   // throws std::runtime_error when unreadable

    long long w = 0, h = 0, n = 0;
    grid::Grid<std::uint16_t> idmap;
    std::vector<std::uint8_t> sea, coastal;               // per province (bool)
    grid::Grid<std::uint8_t> prov_border, sea_border, state_border;
    OrderedMap<long long, grid::PySet<long long>> adjacent;
    std::vector<std::int64_t> area;
    std::vector<double> cx, cy;
    std::vector<std::int32_t> x0, y0, x1, y1;
    QStringList region_ids;
    std::vector<std::int32_t> reg_idx;
    long long version = 0;                                 // bumped on every pixel change
    // (C++ only: which map this is, for caches kept by (address, uid, version) — a map read again at the same address
    // starts at version 0 again)
    long long uid = _next_uid();
    long long pix_stamp = 0;                               // (C++ only) up by one at every refresh_masks: live strokes too
    // (C++ only, 2026-11-10) the boxes (x0, y0, x1, y1) of the last pixel changes, by pix_stamp — whole-map results
    // kept elsewhere redo only these. -> the union of the boxes changed after `stamp` ({0, 0, 0, 0}: none); None when
    // the list doesn't reach back that far.
    std::deque<std::pair<long long, std::array<long long, 4>>> pix_log;
    std::optional<std::array<long long, 4>> changed_since(long long stamp) const;
    static long long _next_uid()
    {
        static long long n = 0;
        return ++n;
    }

    void _compute_adjacency();
    void _compute_stats();
    void rebuild_regions(const moddata::Mod &mod);
    const grid::Grid<std::int32_t> &pix_key();
    const grid::Grid<std::int32_t> &pix_key_mix(std::int32_t Q);
    void refresh_masks(const std::array<long long, 4> &box);
    void refresh_ids(const std::vector<long long> &ids, const std::array<long long, 4> &box);
    long long province_at(double x, double y) const;
    std::optional<std::array<long long, 4>> bbox(const std::vector<long long> &ids) const;
    std::vector<long long> neighbours(long long pid) const;   // list(md.adjacent.get(pid, ())) in Python's order

private:
    std::optional<grid::Grid<std::int32_t>> _key;
    std::map<std::int32_t, grid::Grid<std::int32_t>> _mix_key;   // (Python: a dict; only looked up)
    void _coastal_full();
};

}  // namespace mapview
