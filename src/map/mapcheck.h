// mapcheck.py — Errors tab: problems of this session (factories over the state rules, unfinished new provinces, a
// failed Apply) and the map check — everything that doesn't line up between provinces.bmp, definition.csv,
// default.map, the history, region.txt, continent.txt / climate.txt and the terrain / river bitmaps.
// run_checks() returns a list of Category(key, title, severity, items, fix); an item has a text, the provinces it
// concerns and (optionally) a map point to show. Heavy map-geometry results (split provinces, unknown colours) are
// cached per provinces.bmp and only recomputed when the map image changes.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/scriptstore.h"
#include "grid/grid.h"
#include "map/adjacency_geo.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"

#include <QString>
#include <QStringList>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

namespace mapcheck {

using OptStr = std::optional<QString>;
extern const QString SEV_ERROR, SEV_WARN, SEV_INFO;
inline constexpr long long STRAY_MAX = 20;      // a separate piece of a province smaller than this is a stray pixel group
inline constexpr int RIVER_SEA = 254, RIVER_LAND = 255;   // rivers.bmp: water / plain land (0–11 are rivers)
inline constexpr long long HUGE_LAND = 20000;  // (C++ only, 2026-11-18, the user's rule) land provinces above this can crash the game
inline constexpr long long TINY = 8;            // provinces smaller than this (pixels) are listed as tiny

// An item's data: Python hands out tuples of ints, texts, None, tuples ("row", uid, a, b, "delete") …
struct Val {
    std::variant<std::monostate, bool, long long, double, QString, std::vector<Val>> v;
    Val() = default;
    Val(std::nullptr_t) {}
    Val(bool b) : v(b) {}
    Val(int i) : v(static_cast<long long>(i)) {}
    Val(long long i) : v(i) {}
    Val(double d) : v(d) {}
    Val(const QString &s) : v(s) {}
    Val(const char16_t *s) : v(QString::fromUtf16(s)) {}
    Val(std::vector<Val> l) : v(std::move(l)) {}
    template <typename T>
    Val(const std::optional<T> &o) { if (o) *this = Val(*o); }
    bool is_none() const { return std::holds_alternative<std::monostate>(v); }
    const std::vector<Val> &list() const { return std::get<std::vector<Val>>(v); }
    const Val &operator[](size_t i) const { return list().at(i); }
    bool truthy() const;                         // Python's bool(x)
    bool operator==(const Val &o) const = default;
};
Val tup(std::initializer_list<Val> items);

using XY = std::pair<double, double>;
struct Item {
    QString text;
    std::vector<long long> pids;
    std::optional<XY> point;
    Val data;
    std::optional<document::FactoryProblem> factory;   // (the factories category: the problem itself, for the fix)
};
struct Fix {                                     // (label, what, arg)
    QString label, what;
    Val arg;
};
struct Category {
    QString key, title, severity;
    std::vector<Item> items;
    std::optional<Fix> fix;
    QString note;
    OptStr tab = QStringLiteral("map");          // the tab the problem belongs to (its button turns red); None: none
    OptStr fix_one;                              // label of the fix for just the selected problem
};
QString _n(long long n, const QString &one, const QString &many);

// ---------------------------------------------------------------------------------------------- geometry
struct Pieces {                                  // per piece, in the order of their first run
    std::vector<std::int64_t> pid, size, first;   // province id, pixels, a pixel of the piece (flat index)
};
// connected (4-neighbour) pieces of every province of an id map
Pieces province_pieces(const grid::Grid<std::int32_t> &idm);
// province_pieces of a map's id map, kept until the map changes (C++ only, 2026-11-10: the checks, the stray-pixel
// count and the small areas each counted the whole map again after every stroke)
const Pieces &pieces_of(const mapview::MapData &md);
// sizes of the separate (4-connected) parts of one province on the map, largest first
std::vector<std::int64_t> province_part_sizes(const mapview::MapData &md, long long pid);

using Rgb = std::tuple<int, int, int>;
using UnknownColours = std::vector<std::pair<Rgb, std::pair<long long, long long>>>;   // ascending colour key
// colours on the map that no province uses: {(r, g, b): (pixel count, flat index of one pixel)}, by colour key
UnknownColours unknown_colours(const QString &bmp_path,
                                                                 const grid::Grid<std::uint16_t> &idm,
                                                                 const std::vector<Rgb> &province_rgbs = {});

struct BitmapInfo {
    long long w, h;
    bool indexed;
};
class GeometryCache {                            // split provinces / unknown colours of one provinces.bmp
public:
    std::optional<std::tuple<const void *, long long, double>> key;
    Pieces pieces;
    UnknownColours unknown;
    std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> zero_small;
    std::optional<BitmapInfo> bitmap(const QString &path);
    std::pair<const Pieces *, const UnknownColours *>
    get(const mapview::MapData &md, const QString &bmp_path, const std::vector<Rgb> &province_rgbs = {});
    struct Kept {                                // (Python: geo_cache._kept) categories kept per map / layer state
        std::vector<long long> key;
        std::vector<Category> cats;
    };
    std::map<QString, Kept> kept;

private:
    std::map<std::pair<QString, double>, std::optional<BitmapInfo>> _bmps;
};

// ---------------------------------------------------------------------------------------------- checks
XY _pt(const mapview::MapData &md, long long flat);
QString fmt_n(long long n);
XY _centre(const mapview::MapData &md, long long pid);
std::vector<std::pair<QString, QString>> stray_mod_files(const moddata::Mod &mod);
std::vector<Category> session_checks(moddata::Mod &mod, const mapview::MapData &md, document::Document &doc);

extern const std::vector<std::pair<QString, QString>> RIVER_RULES;
using Masks = OrderedMap<QString, grid::Grid<std::uint8_t>>;
Masks river_problem_masks(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R, long long y0 = 0,
                          std::optional<long long> y1 = std::nullopt, long long x0 = 0,
                          std::optional<long long> x1 = std::nullopt);
std::vector<long long> map_key(const mapview::MapData &md, const std::vector<const layerbmp::IndexedBitmap *> &layers);
const Masks &river_masks_whole(const mapview::MapData &md, const layerbmp::IndexedBitmap &rb);
Category _again(const Category &c);
Category river_checks(const mapview::MapData &md, const grid::Grid<std::uint8_t> &R, const Masks *masks = nullptr);
using MaskPair = std::pair<grid::Grid<std::uint8_t>, grid::Grid<std::uint8_t>>;
OrderedMap<QString, MaskPair> landsea_masks(const moddata::Mod &mod, const mapview::MapData &md,
                                            layerbmp::IndexedBitmap *tb = nullptr, layerbmp::IndexedBitmap *rb = nullptr);
// landsea_masks kept between edits (only the box changed since is counted again — C++ only, 2026-11-10)
const OrderedMap<QString, MaskPair> &landsea_masks_whole(const moddata::Mod &mod, const mapview::MapData &md,
                                                         layerbmp::IndexedBitmap *tb, layerbmp::IndexedBitmap *rb);
std::vector<std::tuple<long long, long long, long long>> _by_province(const mapview::MapData &md,
                                                                     const grid::Grid<std::uint8_t> &mask);
std::vector<Category> landsea_checks(const moddata::Mod &mod, const mapview::MapData &md, layerbmp::IndexedBitmap *tb,
                                     layerbmp::IndexedBitmap *rb);
extern const QStringList USELESS_ROW;
std::vector<Category> adjacency_checks(moddata::Mod &mod, const mapview::MapData &md, document::Document &doc,
                                       adjacency::AdjGeometry *geo, double gap = 2.3);
std::vector<Category> war_goal_checks(moddata::Mod &mod, document::Document &doc, scriptstore::ScriptStore &store);
std::vector<Category> run_checks(moddata::Mod &mod, const mapview::MapData &md, document::Document &doc,
                                 GeometryCache &geo_cache, const std::vector<std::uint8_t> &valid_sea,
                                 adjacency::AdjGeometry *adj_geo = nullptr, double gap = 2.3);

}  // namespace mapcheck
