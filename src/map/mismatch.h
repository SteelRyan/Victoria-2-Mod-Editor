// mismatch.py — where the three maps disagree about land and sea (the tester, 2026-10-15): provinces.bmp, terrain.bmp,
// rivers.bmp.
//   t_sea    terrain.bmp has land on a sea tile                      (orange)  → the terrain becomes ocean
//   t_land   a land province has ocean in terrain.bmp                (yellow)  → land terrain (the colour painted with)
//   r_water  land in provinces.bmp and terrain.bmp, but water in     (pink)    → land in rivers.bmp (255, white)
//            rivers.bmp
//   r_sea    a sea tile has land / a river in rivers.bmp            (green)   → water in rivers.bmp (254)
// (Python reads doc.landsea, set by the window; here it is passed in.)
#pragma once
#include "core/ordered_map.h"
#include "grid/grid.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"

#include <QString>
#include <optional>
#include <tuple>
#include <vector>

namespace mismatch {

using Rgb = std::tuple<int, int, int>;
struct Kind {
    QString key, text;
    Rgb colour;
    QString layer;
};
extern const std::vector<Kind> KINDS;
Rgb colour_of(const QString &kind);
QString text_of(const QString &kind);
extern const OrderedMap<QString, QStringList> OF_LAYER;

using Masks = OrderedMap<QString, grid::Grid<std::uint8_t>>;
using Flat = std::vector<std::int64_t>;

class Mismatch {
public:
    Mismatch(const moddata::Mod &mod, const mapview::MapData &md, layerbmp::LandSea *ls) : mod(mod), md(md), ls(ls) {}
    const moddata::Mod &mod;
    const mapview::MapData &md;
    layerbmp::LandSea *ls;
    std::pair<bool, bool> ready();
    Masks _masks(const std::vector<std::uint16_t> &ids, const std::vector<std::uint8_t> *T, const std::vector<std::uint8_t> *R,
                 long long h, long long w, bool ok_t, bool ok_r) const;
    Masks window(long long y0, long long y1, long long x0, long long x1);
    Masks at(const Flat &idx);                    // (each mask 1 x len(idx))
    const OrderedMap<QString, Flat> &whole();
    OrderedMap<QString, long long> counts();
    std::optional<QString> kind_at(long long x, long long y);
    QString describe(long long x, long long y);
    std::vector<std::uint8_t> fix_values(const QString &kind, long long n, std::optional<long long> land_value = std::nullopt) const;
    layerbmp::IndexedBitmap *bitmap(const QString &kind) const;
    struct Fix {
        layerbmp::IndexedBitmap *bitmap;
        Flat idx;
        std::vector<std::uint8_t> vals;
    };
    std::vector<Fix> fix(const QStringList &kinds, const std::optional<Flat> &idx = std::nullopt,
                         std::optional<long long> land_value = std::nullopt);

    // (C++ only, 2026-11-10: the last overlay() — a layer's picture asks for it twice per render)
    struct KeptOverlay {
        std::vector<long long> key;
        QStringList kinds;
        std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> out;
    };
    std::optional<KeptOverlay> _overlay;
    std::vector<long long> _overlay_key(long long y0, long long y1, long long x0, long long x1);

private:
    std::optional<std::pair<std::vector<long long>, OrderedMap<QString, Flat>>> _counts;
    std::vector<long long> _sig() const;
};

std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> overlay(Mismatch &mm, const QStringList &kinds, long long y0,
                                                              long long y1, long long x0, long long x1);

}  // namespace mismatch
