// pospanel.py, its data half: the checks of every land province's positions (compute_problems — also used by the
// Errors tab while the Positions panel isn't on screen). The Positions panel itself (placing, the handles on the map)
// is in ui/pospanel.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "map/mapview_data.h"
#include "map/positions_geo.h"

#include <QSet>
#include <QString>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace pospanel {

using Problems = std::vector<std::pair<QString, QString>>;   // [(kind, text)]

class PosChecks {
public:
    PosChecks(moddata::Mod &mod, const mapview::MapData &md);
    moddata::Mod &mod;
    const mapview::MapData &md;
    document::Document *doc = nullptr;
    positions::PositionPlacer placer;
    std::vector<long long> land;
    QSet<long long> _coast, _hist_naval;
    std::map<long long, history::Date> _naval_other;
    OrderedMap<long long, Problems> _probs;

    std::vector<long long> _land() const;
    std::pair<std::vector<std::uint8_t>, std::vector<long long>> _valid_sea() const;   // (valid, sorted skip)
    QSet<long long> _coastal(const std::vector<std::uint8_t> &valid) const;
    std::vector<history::Date> _other_dates();
    QSet<long long> _naval_now();
    std::vector<std::uint8_t> prepare(document::Document &doc);
    const OrderedMap<long long, Problems> &compute_problems(document::Document &doc);
    // (cleared by the window when pixels change)
    std::optional<std::vector<long long>> _skip;
    std::map<long long, std::pair<positions::Entry, Problems>> _cache;

private:
    std::optional<std::vector<history::Date>> _dates;
};

}  // namespace pospanel
