// positions.py, the placement half: PositionPlacer (good spots for a province's name, unit, buildings and naval
// base, display coordinates) and check_entry. numpy's steps in plain loops; np.linalg.eigh of the 2x2 covariance is
// LAPACK's own path for it (dsteqr -> dlaev2), and np.cov's BLAS dot is a sequential FMA sum (see eigh2 / cov2).
#pragma once
#include "core/ordered_map.h"
#include "core/positions.h"
#include "grid/grid.h"
#include "map/mapview_data.h"

#include <QString>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace positions {

using Mask = grid::Grid<std::uint8_t>;
Mask _erode(const Mask &m, bool eight);
grid::Grid<float> dist_map(const Mask &mask);
grid::Grid<std::int32_t> _run_right(const Mask &R);
grid::Grid<std::int32_t> _run_down(const Mask &R);

// np.cov of two rows and np.linalg.eigh of the result (w ascending, v's columns the eigenvectors)
std::array<double, 4> cov2(const std::vector<double> &a, const std::vector<double> &b);
void eigh2(const std::array<double, 4> &A, std::array<double, 2> &w, std::array<double, 4> &v);

struct NameFit {                            // (centre_disp, angle_disp, scale, fits); angle clockwise on screen
    XY centre;
    double angle, scale;
    bool fits;
};
struct NameBox {
    XY centre;
    double th, w, h;
};
struct Naval {
    XY at;
    double rot;
};

class PositionPlacer {
public:
    PositionPlacer(const mapview::MapData &md, std::vector<std::uint8_t> sea_valid);
    const mapview::MapData &md;
    std::vector<std::uint8_t> sea_valid;    // per province id: a sea tile that may hold a naval base
    struct Info {
        long long x0, y0;
        grid::Grid<std::uint16_t> sub;
        Mask M;
        grid::Grid<float> D;
    };
    struct Crop {
        long long x0, y0;
        grid::Grid<std::uint16_t> sub;
    };
    Crop crop(long long pid, long long pad = 2) const;
    std::shared_ptr<const Info> info(long long pid);   // (cached; the cache is emptied past 400 provinces)
    std::optional<NameFit> fit_name(long long pid, const QString &name, bool rotate = true);
    std::optional<std::array<double, 3>> _fit_at(const Mask &M, const std::vector<long long> &xs,
                                                 const std::vector<long long> &ys, const XY &pole, long long n,
                                                 double th) const;
    static NameBox name_box(const XY &centre, double th, double scale, const QString &name);
    double name_clips(long long pid, const XY &centre, double th, double scale, const QString &name) const;
    OrderedMap<QString, XY> place_points(long long pid, const std::optional<NameBox> &name_box = std::nullopt);
    std::optional<Naval> naval_spot(long long pid, const std::optional<XY> &near_xy = std::nullopt) const;
    Naval fallback_naval(long long pid, const std::optional<XY> &near_xy = std::nullopt);
    bool naval_ok(long long pid, const XY &disp_xy) const;
    std::map<long long, std::shared_ptr<const Info>> _cache;   // (cleared by the window when pixels change)
};

long long name_len(const QString &name);    // max(1, len(name.strip()))

// -> [(kind, text)] problems of one land province. want_naval: it has a valid coast; hist_naval: its history
// gives it a naval base.
std::vector<std::pair<QString, QString>> check_entry(PositionPlacer &placer, const mapview::MapData &md, long long pid,
                                                     const Entry &e, const QString &name, double H, bool want_naval,
                                                     bool hist_naval = false);

}  // namespace positions
