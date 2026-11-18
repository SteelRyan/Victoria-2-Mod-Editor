// pospanel.py, its data half — see pospanel_data.h.
#include "map/pospanel_data.h"
#include "core/adjacency.h"
#include "core/positions.h"
#include "core/script.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace pospanel {

PosChecks::PosChecks(moddata::Mod &mod_, const mapview::MapData &md_)
    : mod(mod_), md(md_), placer(md_, md_.sea), land(_land())
{
}

std::vector<long long> PosChecks::_land() const
{
    std::vector<long long> out;
    for (const auto &[p, pr] : mod.provinces)
        if (!pr.sea && 0 < p && p < md.n && md.area[size_t(p)] > 0)
            out.push_back(p);
    return out;
}

std::pair<std::vector<std::uint8_t>, std::vector<long long>> PosChecks::_valid_sea() const
{
    // sea tiles that can hold a naval base: not Meme-river / no-coast tiles
    std::vector<std::uint8_t> v = md.sea;
    v[0] = 0;
    std::vector<long long> skip;
    if (doc->maps.contains(u"adjacency"_s)) {
        const QSet<long long> s = doc->map<adjacency::AdjacencyFile>(u"adjacency"_s).skip_coast();
        skip.assign(s.begin(), s.end());
    }
    std::sort(skip.begin(), skip.end());
    for (long long t : skip)
        if (0 < t && t < static_cast<long long>(v.size()))
            v[size_t(t)] = 0;
    return {v, skip};
}

QSet<long long> PosChecks::_coastal(const std::vector<std::uint8_t> &valid) const
{
    QSet<long long> out;
    for (long long p : land)
        for (long long q : md.neighbours(p))
            if (0 < q && q < static_cast<long long>(valid.size()) && valid[size_t(q)]) {
                out.insert(p);
                break;
            }
    return out;
}

std::vector<history::Date> PosChecks::_other_dates()
{
    if (!_dates) {
        std::vector<history::Date> ds;
        for (const auto &s : mod.scenarios())
            if (s.isDate)
                if (const auto d = script::parse_date(s.name))
                    ds.push_back(*d);
        std::sort(ds.begin(), ds.end());
        _dates = ds;
    }
    std::vector<history::Date> out;
    for (const auto &d : *_dates)
        if (!doc->date || d != *doc->date)
            out.push_back(d);
    return out;
}

QSet<long long> PosChecks::_naval_now()
{
    // provinces that have a naval base at the scenario's start (with unsaved Provinces-tab edits) or at another start
    // date. Each of them needs a naval_base position, or the game crashes drawing it.
    const QSet<long long> lset(land.begin(), land.end());
    QSet<long long> cand = positions::naval_in_history(*doc->hist);
    for (const auto &[p, _st] : doc->prov_want)
        cand.insert(p);
    cand &= lset;
    QSet<long long> out;
    _naval_other.clear();
    for (long long pid : cand) {
        if (doc->prov_state(pid).naval_base > 0) {
            out.insert(pid);
            continue;
        }
        for (const auto &d : _other_dates())
            if (doc->hist->state_at(pid, d).naval_base > 0) {
                out.insert(pid);
                _naval_other[pid] = d;
                break;
            }
    }
    return out;
}

std::vector<std::uint8_t> PosChecks::prepare(document::Document &d)
{
    // get ready to place positions (without checking every province), e.g. for a new province
    doc = &d;
    land = _land();
    auto [valid, skip] = _valid_sea();
    placer.sea_valid = valid;
    _skip = skip;
    _cache.clear();
    _coast = _coastal(valid);
    _hist_naval = _naval_now();
    return valid;
}

const OrderedMap<long long, Problems> &PosChecks::compute_problems(document::Document &d)
{
    // {pid: [(kind, text)]} of every land province's positions (cached per entry)
    doc = &d;
    const std::vector<long long> now = _land();
    if (now != land) {
        land = now;
        _skip.reset();
    }
    auto [valid, skip] = _valid_sea();
    placer.sea_valid = valid;
    if (_skip != skip) {
        _skip = skip;
        _cache.clear();
        _coast = _coastal(valid);
    }
    const QSet<long long> hn = _naval_now();
    if (hn != _hist_naval) {
        _hist_naval = hn;
        _cache.clear();
    }
    auto &pf = d.map<positions::PositionsFile>(u"positions"_s);
    _probs.clear();
    for (long long pid : land) {
        const positions::Entry e = pf.get(pid);
        auto it = _cache.find(pid);
        if (it == _cache.end() || !(it->second.first == e)) {
            Problems pr = positions::check_entry(placer, md, pid, e, mod.provinces.get(pid)->name, double(pf.H),
                                                 _coast.contains(pid), _hist_naval.contains(pid));
            it = _cache.insert_or_assign(pid, std::make_pair(e, std::move(pr))).first;
        }
        if (!it->second.second.empty())
            _probs.insert(pid, it->second.second);
    }
    return _probs;
}

}  // namespace pospanel
