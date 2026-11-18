// provpanel.py, its data half (PLAN.md §4.1): the parts of a state on the map. The Provinces panel itself is kind D.
#pragma once
#include "core/moddata.h"
#include "map/mapview_data.h"

#include <QHash>
#include <QString>
#include <vector>

namespace adjacency {
class AdjacencyFile;
}

namespace provpanel {

// Land provinces linked by adjacencies.csv although their pixels do not touch (C++ only, the user 2026-11-13: "River
// fucks up split states view … the meme river in the middle does not belong to it, this is faulty although technically
// correct"): a crossing without a sea tile (type sea, through 0 — Ferrum Mare's "Meme-River" rows), a land link (a
// bridge), and a crossing through a marked Meme-river tile. A strait through a real sea tile links nothing.
using Links = QHash<long long, std::vector<long long>>;
Links state_links(const adjacency::AdjacencyFile *adj);

// Connected parts of a state (land adjacency on the map, plus `links`): list of sets of province ids. The parts come in
// the order Python's set.pop() hands out their first province (a tie for the largest part depends on it).
std::vector<std::vector<long long>> state_components(const moddata::Mod &mod, const mapview::MapData *md,
                                                     const QString &rid, const Links *links = nullptr);

}  // namespace provpanel
