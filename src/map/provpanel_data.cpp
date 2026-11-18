// provpanel.py, the data half.
#include "map/provpanel_data.h"
#include "core/adjacency.h"
#include "grid/pyset.h"

namespace provpanel {

Links state_links(const adjacency::AdjacencyFile *adj)
{
    Links out;
    if (!adj)
        return out;
    const auto rivers = adj->river_tiles();
    for (const adjacency::Row &r : adj->rows) {
        const QString cat = adj->category(r);
        const bool link = cat == u"river" || cat == u"land" || (cat == u"strait" && rivers.contains(r.through));
        if (link && r.a != r.b) {
            out[r.a].push_back(r.b);
            out[r.b].push_back(r.a);
        }
    }
    return out;
}

std::vector<std::vector<long long>> state_components(const moddata::Mod &mod, const mapview::MapData *md,
                                                     const QString &rid, const Links *links)
{
    grid::PySet<long long> left;                 // set(provs): added one by one, as Python builds it from a list
    for (long long p : mod.regions.get(rid)->provinces)
        if (mod.provinces.contains(p))
            left.add(p);
    std::vector<std::vector<long long>> parts;
    while (!left.empty()) {
        const long long start = left.pop();
        std::vector<long long> comp = {start}, todo = {start};
        while (!todo.empty()) {
            const long long p = todo.back();
            todo.pop_back();
            if (!md)
                continue;
            for (long long q : md->neighbours(p))
                if (left.contains(q)) {
                    left.discard(q);
                    comp.push_back(q);
                    todo.push_back(q);
                }
            if (links)
                if (auto it = links->constFind(p); it != links->constEnd())
                    for (long long q : *it)
                        if (left.contains(q)) {
                            left.discard(q);
                            comp.push_back(q);
                            todo.push_back(q);
                        }
        }
        parts.push_back(comp);                   // (the members: only their count and the sorted list are used)
    }
    return parts;
}

}  // namespace provpanel
