// adjacency.py, the map half (PLAN.md §4.1): anchor points for drawing adjacencies and the map-derived checks (coast
// pairs, Meme-river crossings, rows that look wrong). The file half (AdjacencyFile, its rows and marks) is
// core\adjacency. Sets that Python walks are grid::PySet, so they come in Python's order.
#pragma once
#include "core/adjacency.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "grid/pyset.h"
#include "map/mapview_data.h"

#include <QString>
#include <QStringList>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace adjacency {

using Pt = std::pair<double, double>;
using Pair = std::pair<std::int64_t, std::int64_t>;
struct RiverPair {
    double dist;
    Pt pa, pb;
};
using RiverPairs = OrderedMap<std::pair<long long, long long>, RiverPair>;

class AdjGeometry {
public:
    AdjGeometry(const mapview::MapData &md, const QSet<long long> &sea_ids);
    const mapview::MapData &md;
    std::vector<std::uint8_t> sea_lut;
    void reset();                                 // forget everything derived from the map pixels
    using Pixels = std::pair<std::vector<float>, std::vector<float>>;   // (xs, ys) + 0.5, float32
    std::optional<Pixels> pixels(long long p);
    std::optional<Pt> nearest(long long p, double x, double y);
    std::optional<Pt> inner(long long p);
    std::optional<Pt> border_point(long long a, long long b);
    std::optional<std::pair<Pt, Pt>> anchors(long long a, long long b);
    const grid::PySet<long long> &thin_tiles();
    const grid::PySet<Pair> &coast_pairs();
    bool adjacent(long long a, long long b) const;
    RiverPairs river_pairs(const std::vector<long long> &tiles, double gap = 2.3);
    RiverPairs _river_pairs(const std::vector<long long> &tiles, double gap = 2.3) const;

private:
    QHash<long long, Pixels> _px;
    std::map<std::tuple<long long, long long, long long>, Pt> _near;   // (Python: a dict; only looked up)
    QHash<std::pair<long long, long long>, std::optional<std::pair<Pt, Pt>>> _anchors;
    std::optional<QHash<std::pair<long long, long long>, Pt>> _border;
    std::optional<grid::PySet<long long>> _thin;
    std::optional<grid::PySet<Pair>> _coast_pairs;
    std::map<std::pair<std::vector<long long>, double>, RiverPairs> _river_cache;
    void _build_borders();
    std::optional<std::pair<Pt, Pt>> _anchors_calc(long long a, long long b);
};

struct DetectedRiver {
    QString name;
    std::vector<long long> tiles;
    long long rows;
};
std::vector<DetectedRiver> detect_rivers(AdjacencyFile &adj, AdjGeometry &geo, double min_share = 0.3);
std::optional<QString> river_name_of(AdjacencyFile &adj, AdjGeometry &geo, const std::vector<long long> &tiles);

struct CoastStatus {
    grid::PySet<Pair> pairs;
    std::vector<Pair> missing;
    std::vector<Row> stale;
    grid::PySet<Pair> river_pairs, nc_pairs;
};
CoastStatus coast_status(AdjacencyFile &adj, AdjGeometry &geo);
struct RiverStatus {
    RiverPairs expected;
    std::vector<std::pair<long long, long long>> missing;
};
OrderedMap<QString, RiverStatus> river_status(AdjacencyFile &adj, AdjGeometry &geo, double gap = 2.3);
QStringList check_row(AdjacencyFile &adj, AdjGeometry &geo, const moddata::Mod &mod, const Row &r);
OrderedMap<long long, QStringList> problems(AdjacencyFile &adj, AdjGeometry &geo, const moddata::Mod &mod);

}  // namespace adjacency
