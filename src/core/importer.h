// importer.py — Taking things over from another mod (or from the game itself). Ported so far: find_sources (what
// popcheck needs when it loads). Source / Importer (kind B: with the map arrays) follow in their phase.
#pragma once
#include "core/moddata.h"

#include <QString>
#include <utility>
#include <vector>

namespace importer {

// [(name, folder)] of mods the editor can find to import from: the folders next to the mod, in a "study mods"
// folder beside it, in the game's own mod folder — and the game itself
std::vector<std::pair<QString, QString>> find_sources(const moddata::Mod &mod);

}  // namespace importer
