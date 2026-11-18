// provdelete.py — deleting provinces for good (Drawing → "Delete province…", and the provinces a map cut removes).
// A deleted province gives up everything that makes it one — its pixels (they go to what lies around it), its history
// file, its pops, its place in a state, continent and climate, its sea_starts entry, its places (positions.txt) and
// every adjacency row that names it. One step that can be undone; Apply writes it. On Apply the highest province takes
// the freed number in every file (provrenumber) and the mod is read again.
// (Python reads doc.landsea, which the window sets; here it is passed in: `landsea`.)
#pragma once
#include "core/document.h"
#include "core/oob.h"
#include "map/layerbmp.h"
#include "map/mapview_data.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

namespace provdelete {

using Flat = std::vector<std::int64_t>;
// What stands on the provinces at the start and has to follow when they go: ({tag: its new capital, or None when it
// owns nothing else}, the army / navy / brigade entries for oob::fix_units).
std::pair<OrderedMap<QString, std::optional<long long>>, std::vector<oob::Fix>>
followers(document::Document &doc, const std::vector<long long> &pids, const OrderedMap<long long, long long> &heirs = {});
// {province: [(file, how often)]} — the scripts that name one of the provinces where a province is meant
OrderedMap<long long, std::vector<std::pair<QString, long long>>> script_mentions(const moddata::Mod &mod,
                                                                                  const std::vector<long long> &pids);
// {deleted province: the province that got most of its pixels}
OrderedMap<long long, long long> heirs_of(const std::vector<long long> &pids, const std::vector<std::uint16_t> &old,
                                          const std::vector<std::uint16_t> &nw);
struct Refill {
    Flat idx;
    std::vector<std::uint16_t> old, nw;
};
// the pixels of the provinces handed to what lies around them — each to the nearest neighbour, land to land and sea
// to sea where there is one; what touches nothing becomes "no province"
Refill refill(const mapview::MapData &md, const std::vector<long long> &pids);
QStringList describe(document::Document &doc, const mapview::MapData &md, std::vector<long long> pids);

struct Job {                                      // {"label", "pids", "rids", "lists", "run"} for Document.edit
    QString label;
    QList<long long> pids;
    QStringList rids, lists;
    std::function<long long()> run;
};
std::optional<Job> prepare(document::Document &doc, mapview::MapData &md, const std::vector<long long> &pids,
                           const QString &pixels = QStringLiteral("neighbours"),
                           const OrderedMap<long long, long long> &successor = {}, layerbmp::LandSea *landsea = nullptr);
long long delete_provinces(document::Document &doc, mapview::MapData &md, const std::vector<long long> &pids,
                           const QString &pixels = QStringLiteral("neighbours"), layerbmp::LandSea *landsea = nullptr);

}  // namespace provdelete
