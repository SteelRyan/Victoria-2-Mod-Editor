// regionimport.py, its data half (PLAN.md §4.1): Import → Provinces from its map — pick provinces on the other mod's
// map and bring them over: their shape pixel for pixel at the same place (or moved sideways), as new provinces of this
// mod with their names, history, pops, places (positions), terrain and river pixels; their countries taken along, kept
// only where this mod has them, or left out; their states brought or not. What lies under them on this mod's map is
// replaced. Or, "shapes only": the picked provinces' shapes onto this mod's provinces of the same number / colour.
// One step that can be undone (plus one for the countries, when they are taken along); Apply writes it. The other mod is
// only read. The page (SourceMap, RegionImportPage) is kind C: ui\regionimport.
// (Python reads doc.landsea, which the window sets; here it is passed in: `landsea`.)
#pragma once
#include "core/document.h"
#include "core/history.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/positions.h"
#include "grid/grid.h"
#include "map/importer.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"
#include "map/newprov.h"

#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace regionimport {

using Flat = std::vector<std::int64_t>;
using Rgb8 = std::array<std::uint8_t, 3>;
extern const QString TAG_LINE, GOODS_LINE;                    // (patterns for py::rx, multi-line)
extern const std::vector<std::pair<QString, QString>> NATIONS;
extern const OrderedMap<QString, QString> KEYS;

// ---------------------------------------------------------------------------------------------- the other mod's map
// The other mod read for its map: provinces by pixel, their history at the start, pops, places, layers.
class RegionSource {
public:
    RegionSource(importer::Source &src, const QString &scenario, const std::optional<history::Date> &date);
    importer::Source &src;
    moddata::Mod &mod;
    std::unique_ptr<mapview::MapData> md;
    std::unique_ptr<history::ProvinceHistory> hist;
    std::optional<history::Date> date;
    QString scenario;

    const history::ProvState &state(long long pid);
    QString name(long long pid) const;
    const std::vector<moddata::Pop> &pops(long long pid);
    const OrderedMap<long long, positions::Entry> &positions();
    // terrain.bmp / rivers.bmp of the other mod (nullptr when it can't be read or isn't the map's size)
    const grid::Grid<std::uint8_t> *layer(const QString &name);
    std::vector<long long> of_state(long long pid) const;
    std::vector<long long> of_owner(long long pid);
    std::vector<long long> of_continent(long long pid);

private:
    std::optional<OrderedMap<long long, moddata::PopProvince>> _pops;
    std::optional<OrderedMap<long long, positions::Entry>> _pos;
    std::optional<std::unique_ptr<mapfiles::MembershipFile>> _cont;   // (Python: None / False / the file)
    std::map<QString, std::unique_ptr<layerbmp::IndexedBitmap>> _layers;
    std::map<long long, history::ProvState> _state;
};

// ---------------------------------------------------------------------------------------------- the plan
struct Prov {
    long long src = 0, pid = 0;
    QString name;
    bool sea = false;
    newprov::NewProv rec;
    std::vector<moddata::Pop> pops;
    long long skipped = 0;
    std::optional<positions::Entry> entry;
    long long under = 0;
    std::optional<QString> region;
    long long px = 0;
};

struct Opts {                                   // (Python: a dict; opts.get(k, default))
    long long dx = 0, dy = 0;                   // px to the right, px down
    QString nations = QStringLiteral("have");   // "take" / "have" / "leave"
    bool states = true, pops = true, layers = true, positions = true, replace = true;
    QString mode = QStringLiteral("new");       // "borders": onto this mod's provinces, only the shapes
    QString match = QStringLiteral("number");   // "number" / "colour"
    bool clear_old = true;
};

struct Plan {
    std::vector<Prov> provs;
    std::optional<Flat> idx;
    std::vector<std::uint16_t> src_pid;
    Flat nw;                                     // (Python: plan.new)
    std::optional<std::vector<std::uint8_t>> terrain, rivers;
    long long outside = 0;
    std::vector<long long> covered, partly;
    QStringList missing_tags, kept_tags, dropped_tags;
    OrderedMap<QString, long long> skipped_pops;
    std::set<QString> dropped_goods;
    OrderedMap<QString, std::vector<long long>> new_regions;
    QString problem;
    QString mode = QStringLiteral("new");
    std::vector<std::pair<long long, long long>> pairs;   // [(their province, this mod's)]
    std::vector<long long> no_target;            // theirs without one here
    std::optional<Flat> fill_idx;                // old land of the reshaped provinces that goes to the neighbours
    Flat fill_new;
    OrderedMap<long long, long long> heirs;
    OrderedMap<long long, std::pair<long long, long long>> sizes;
};

Plan make_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, const std::vector<long long> &pids,
               const Opts &opts);
// Pixels of `mask` get the province of the nearest pixel that is not in the mask — grown inwards a ring at a time.
// `avoid`: a flag per province — those never spread into the mask; only where nothing else is next to it do they.
// Changes `final` in place; -> the flat places it filled.
Flat neighbour_fill(grid::Grid<std::uint16_t> &final, const grid::Grid<std::uint8_t> &mask,
                    const std::vector<std::uint8_t> *avoid = nullptr);
Plan &_borders_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan,
                    const std::vector<long long> &kept, const Opts &opts);
QString _apply_borders(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan, const Opts &opts,
                       layerbmp::LandSea *landsea);
QString apply_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan, const Opts &opts,
                   layerbmp::LandSea *landsea = nullptr);

// A colour for every province id: its owner's colour (sea blue, unowned land grey), or its own.
std::vector<Rgb8> owner_lut(const moddata::Mod &mod, const std::function<const history::ProvState &(long long)> &state_of,
                            long long n, bool by_owner = true);

}  // namespace regionimport
