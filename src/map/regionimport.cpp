// regionimport.py, its data half — line by line.
#include "map/regionimport.h"

#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/script.h"
#include "grid/random.h"
#include "map/provbmp.h"
#include "map/provdelete.h"

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

namespace regionimport {

const QString TAG_LINE =
    uR"re((?m)^[ \t]*(owner|controller|add_core|remove_core)[ \t]*=[ \t]*"?([A-Za-z0-9]{3})"?[ \t]*(#.*)?\r?$)re"_s;
const QString GOODS_LINE = uR"re((?m)^[ \t]*trade_goods[ \t]*=[ \t]*"?(\w+)"?[ \t]*(#.*)?\r?$)re"_s;
const std::vector<std::pair<QString, QString>> NATIONS = {{u"take"_s, u"Take their countries too"_s},
                                                          {u"have"_s, u"Only countries this mod has"_s},
                                                          {u"leave"_s, u"Leave the countries out"_s}};
const OrderedMap<QString, QString> KEYS = {
    {u"theirs"_s, u"Click: that province · Shift+click: its whole state · Alt+click: all its owner's land · "
                  u"Alt+Shift+click: its continent · Ctrl: add / remove instead of replacing · drag: add what you "
                  u"pass over · wheel: zoom · right button: move the map"_s},
    {u"mine"_s, u"Drag the lit provinces to where they belong (the numbers on the right follow) · drag anywhere "
                u"else or with the right button: move the map · wheel: zoom"_s}};

static bool all_digits(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s)
        if (!c.isDigit())
            return false;
    return true;
}

// ---------------------------------------------------------------------------------------------- the other mod's map
RegionSource::RegionSource(importer::Source &src_, const QString &scenario_, const std::optional<history::Date> &date_)
    : src(src_), mod(*src_.mod), date(date_), scenario(scenario_)
{
    if (mod.provinces.isEmpty()) {
        mod.warnings.clear();
        mod.load();
    }
    md = std::make_unique<mapview::MapData>(mod, mod.p({u"map"_s, u"provinces.bmp"_s}));
    hist = std::make_unique<history::ProvinceHistory>(mod);
}

const history::ProvState &RegionSource::state(long long pid)
{
    auto it = _state.find(pid);
    if (it == _state.end())
        it = _state.emplace(pid, hist->state_at(pid, date)).first;
    return it->second;
}

QString RegionSource::name(long long pid) const
{
    const moddata::Province *pr = mod.provinces.get(pid);
    QString s = mod.loc.value(u"PROV%1"_s.arg(pid));
    if (s.isEmpty() && pr)
        s = pr->name;
    s = py::strip(s);
    return s.isEmpty() ? u"Province %1"_s.arg(pid) : s;
}

const std::vector<moddata::Pop> &RegionSource::pops(long long pid)
{
    static const std::vector<moddata::Pop> none;
    if (!_pops) {
        _pops.emplace();
        try {
            QStringList names;
            for (const auto &s : mod.scenarios())
                if (s.files)
                    names << s.name;
            const std::optional<QString> scen = names.contains(scenario) ? std::optional<QString>(scenario)
                                                : !names.isEmpty()        ? std::optional<QString>(names[0])
                                                                          : std::nullopt;
            if (scen)
                _pops = moddata::load_pop_files(mod, *scen).provinces;
        } catch (const std::exception &) {
            _pops.emplace();
        }
    }
    const moddata::PopProvince *pp = _pops->get(pid);
    return pp ? pp->pops : none;
}

const OrderedMap<long long, positions::Entry> &RegionSource::positions()
{
    if (!_pos) {
        _pos.emplace();
        try {
            const QString text = script::read_text(mod.p({u"map"_s, u"positions.txt"_s}));
            for (const script::Node &n : script::parse(text))
                if (n.has_key && all_digits(n.key) && n.is_block)
                    _pos->insert(n.key.toLongLong(), positions::parse_entry(text, n));
        } catch (const std::exception &) {
        }
    }
    return *_pos;
}

const grid::Grid<std::uint8_t> *RegionSource::layer(const QString &name)
{
    auto it = _layers.find(name);
    if (it == _layers.end()) {
        auto lb = std::make_unique<layerbmp::IndexedBitmap>(mod, *md, name + u".bmp"_s, name);
        if (!lb->load())
            lb.reset();
        it = _layers.emplace(name, std::move(lb)).first;
    }
    return it->second && it->second->data ? &*it->second->data : nullptr;
}

std::vector<long long> RegionSource::of_state(long long pid) const
{
    const moddata::Province *pr = mod.provinces.get(pid);
    const moddata::Region *r = pr && pr->region && !pr->region->isEmpty() ? mod.regions.get(*pr->region) : nullptr;
    if (!r)
        return {pid};
    std::vector<long long> out;
    for (long long p : r->provinces)
        if (mod.provinces.contains(p))
            out.push_back(p);
    return out;
}

std::vector<long long> RegionSource::of_owner(long long pid)
{
    const std::optional<QString> tag = state(pid).owner;
    if (!tag || tag->isEmpty())
        return {pid};
    std::vector<long long> out;
    for (const auto &[p, pr] : mod.provinces)
        if (!pr.sea && state(p).owner == *tag)
            out.push_back(p);
    return out;
}

std::vector<long long> RegionSource::of_continent(long long pid)
{
    if (!_cont) {
        try {
            _cont = std::make_unique<mapfiles::MembershipFile>(mod, u"continent"_s);
        } catch (const std::exception &) {
            _cont = std::unique_ptr<mapfiles::MembershipFile>();
        }
    }
    mapfiles::MembershipFile *cf = _cont->get();
    if (!cf)
        return {pid};
    std::set<long long> got;
    for (const QString &n : cf->of(pid))
        if (const auto *lst = cf->lists.get(n))
            for (long long p : *lst)
                if (const moddata::Province *pr = mod.provinces.get(p); pr && !pr->sea)
                    got.insert(p);
    if (got.empty())
        return {pid};
    return {got.begin(), got.end()};
}

// ---------------------------------------------------------------------------------------------- the plan
static std::vector<long long> bincount(const std::vector<std::uint16_t> &v, size_t minlength)
{
    size_t n = minlength;
    for (std::uint16_t x : v)
        n = std::max(n, size_t(x) + 1);
    std::vector<long long> c(n, 0);
    for (std::uint16_t x : v)
        ++c[x];
    return c;
}

static size_t argmax(const std::vector<long long> &c)
{
    size_t best = 0;
    for (size_t i = 1; i < c.size(); ++i)
        if (c[i] > c[best])
            best = i;
    return best;
}

Plan make_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, const std::vector<long long> &pids_in,
               const Opts &opts)
{
    // What an import of the picked provinces does.
    moddata::Mod &mod = doc.mod;
    Plan plan;
    std::vector<long long> pids;
    for (long long p : pids_in)
        if (rs.mod.provinces.contains(p))
            pids.push_back(p);
    std::sort(pids.begin(), pids.end());
    if (pids.empty()) {
        plan.problem = u"Nothing is picked yet."_s;
        return plan;
    }
    if (!(mod.in_mod(mod.p({u"map"_s, u"definition.csv"_s})) && mod.in_mod(mod.p({u"map"_s, u"provinces.bmp"_s})))) {
        plan.problem = u"This mod uses the game's own map (it has no map/definition.csv and provinces.bmp of its "
                       u"own), so new provinces can't be written into it. Copy the game's map folder into the mod "
                       u"first."_s;
        return plan;
    }
    const long long dx = opts.dx, down = opts.dy;
    const auto &src_ids = rs.md->idmap;
    const long long sh = src_ids.h(), sw = src_ids.w();
    long long smax = 0;
    for (std::uint16_t v : src_ids.flat())
        smax = std::max<long long>(smax, v);
    std::vector<std::uint8_t> want(static_cast<size_t>(std::max(rs.md->n, smax + 1)), 0);
    for (long long p : pids)
        want[size_t(p)] = 1;
    std::vector<long long> ys, xs;            // (np.nonzero: row by row; only the ones that land on this map)
    for (long long y = 0; y < sh; ++y)
        for (long long x = 0; x < sw; ++x)
            if (want[src_ids(y, x)]) {
                const long long tx = x + dx, ty = y + down;
                if (tx >= 0 && tx < md.w && ty >= 0 && ty < md.h) {
                    ys.push_back(y);
                    xs.push_back(x);
                } else {
                    ++plan.outside;
                }
            }
    if (ys.empty()) {
        plan.problem = u"None of their pixels lie on this mod's map (it is %1 × %2, theirs %3 × %4)."_s.arg(md.w)
                           .arg(md.h)
                           .arg(rs.md->w)
                           .arg(rs.md->h);
        return plan;
    }
    Flat idx(ys.size());
    plan.src_pid.resize(ys.size());
    std::vector<std::uint16_t> old(ys.size());
    for (size_t i = 0; i < ys.size(); ++i) {
        idx[i] = (ys[i] + down) * md.w + xs[i] + dx;
        plan.src_pid[i] = src_ids(ys[i], xs[i]);
        old[i] = md.idmap.flat()[size_t(idx[i])];
    }
    plan.idx = idx;
    const std::vector<long long> cnt = bincount(old, size_t(md.n));
    for (size_t p = 0; p < cnt.size(); ++p)
        if (cnt[p] && p && mod.provinces.contains(static_cast<long long>(p)))
            (cnt[p] >= md.area[p] ? plan.covered : plan.partly).push_back(static_cast<long long>(p));
    if (opts.layers) {
        for (const QString &name : {u"terrain"_s, u"rivers"_s}) {
            const grid::Grid<std::uint8_t> *data = rs.layer(name);
            const QString kind = name + u"_bmp"_s;
            if (data && doc.maps.contains(kind) && doc.map<layerbmp::IndexedBitmap>(kind).load()) {
                std::vector<std::uint8_t> vals(ys.size());
                for (size_t i = 0; i < ys.size(); ++i)
                    vals[i] = (*data)(ys[i], xs[i]);
                (name == u"terrain" ? plan.terrain : plan.rivers) = std::move(vals);
            }
        }
    }
    std::vector<long long> kept;
    {
        std::set<long long> k(plan.src_pid.begin(), plan.src_pid.end());
        kept.assign(k.begin(), k.end());
    }
    if (opts.mode == u"borders")
        return _borders_plan(doc, md, rs, plan, kept, opts);
    auto &defs = doc.map<newprov::ProvinceDefs>(u"definitions"_s);
    const long long first = defs.next_id();
    if (first + static_cast<long long>(kept.size()) >= md.n) {
        plan.problem = u"That is more new provinces than fit in one session (%1 free) — Apply, restart the editor, "
                       u"then import the rest."_s.arg(std::max(0LL, md.n - first - 1));
        return plan;
    }
    std::set<moddata::Rgb> used;
    for (const auto &kv : mod.provinces)
        used.insert(kv.second.rgb);
    const std::vector<bool> &seen = defs.file_colours();
    grid::Generator rng(static_cast<std::uint64_t>(first));
    auto colour = [&](const moddata::Rgb &w) {
        moddata::Rgb c = w;
        for (;;) {
            const auto [r, g, b] = c;
            if (!(used.count(c) || c == moddata::Rgb{0, 0, 0} || seen[(size_t(r) << 16) | (size_t(g) << 8) | size_t(b)]))
                break;
            const auto v = rng.integers(16, 240, 3);
            c = {int(v[0]), int(v[1]), int(v[2])};
        }
        used.insert(c);
        return c;
    };
    const QString &mode = opts.nations;
    std::set<QString> tags_missing, tags_kept, tags_dropped;
    static const OrderedMap<long long, positions::Entry> no_pos;
    const OrderedMap<long long, positions::Entry> &pos = opts.positions ? rs.positions() : no_pos;
    std::vector<long long> lut(static_cast<size_t>(smax + 1), 0);
    const QString folder = newprov::_history_folder_near(*doc.hist, first);
    std::map<long long, std::pair<long long, long long>> under_of;
    {
        std::vector<size_t> order(plan.src_pid.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return plan.src_pid[a] < plan.src_pid[b]; });
        size_t a = 0;
        while (a < order.size()) {
            size_t b = a;
            while (b < order.size() && plan.src_pid[order[b]] == plan.src_pid[order[a]])
                ++b;
            std::vector<std::uint16_t> so;
            for (size_t k = a; k < b; ++k)
                so.push_back(old[order[k]]);
            std::vector<long long> c = bincount(so, 0);
            if (c.size() > 1)
                c[0] = 0;
            under_of[plan.src_pid[order[a]]] = {static_cast<long long>(argmax(c)), static_cast<long long>(b - a)};
            a = b;
        }
    }
    auto keep = [&](const std::optional<QString> &tag) {
        if (!tag || tag->isEmpty())
            return false;
        if (mode == u"leave") {
            tags_dropped.insert(*tag);
            return false;
        }
        if (mod.countries.contains(*tag)) {
            tags_kept.insert(*tag);
            return true;
        }
        if (mode == u"take") {
            tags_missing.insert(*tag);
            return true;
        }
        tags_dropped.insert(*tag);
        return false;
    };
    for (size_t i = 0; i < kept.size(); ++i) {
        const long long s = kept[i];
        const moddata::Province pr = *rs.mod.provinces.get(s);
        Prov p;
        p.src = s;
        p.pid = first + static_cast<long long>(i);
        p.sea = pr.sea;
        p.name = rs.name(s);
        if (auto it = under_of.find(s); it != under_of.end())
            std::tie(p.under, p.px) = it->second;
        lut[size_t(s)] = p.pid;
        std::optional<QString> hist_rel, hist_text;
        newprov::Info info;
        if (!p.sea) {
            const history::ProvState st = rs.state(s);
            const history::HistFileP hf = rs.hist->main_file(s);
            QString text = hf ? hf->text : QString();
            text = py::re_sub(TAG_LINE, text, [&](const QRegularExpressionMatch &m) {
                return keep(m.captured(2)) ? m.captured(0) : QString();
            });
            std::optional<QString> goods = st.trade_goods;
            if (goods && !goods->isEmpty() && !mod.goods.contains(*goods)) {
                plan.dropped_goods.insert(*goods);
                const QString g = *goods;
                text = py::re_sub(GOODS_LINE, text, [&](const QRegularExpressionMatch &m) {
                    return m.captured(1) == g ? QString() : m.captured(0);
                });
                goods.reset();
            }
            text = py::re_sub(uR"(\n[ \t]*\n[ \t]*\n+)"_s, text, [](const QRegularExpressionMatch &) {
                return u"\n\n"_s;
            });
            const std::optional<QString> owner = keep(st.owner) ? st.owner : std::nullopt;
            info.owner = owner;
            info.controller = owner && !owner->isEmpty() && keep(st.controller) ? st.controller : owner;
            for (const QString &t : st.cores)
                if (keep(t))
                    info.cores << t;
            info.life_rating = st.life_rating;
            info.trade_goods = goods;
            hist_rel = u"%1/%2 - %3.txt"_s.arg(folder).arg(p.pid).arg(newprov::safe_file_name(p.name));
            const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
            hist_text = u"# %1 - %2: imported in ModEditor from %3 (its #%4)%5%6"_s.arg(p.pid)
                            .arg(p.name, rs.src.name)
                            .arg(s)
                            .arg(nl, text);
            if (opts.pops) {
                for (const moddata::Pop &pop : rs.pops(s)) {
                    if (mod.poptypes.contains(pop.type) && pop.culture && mod.cultures.contains(*pop.culture) &&
                        pop.religion && mod.religions.contains(*pop.religion) && pop.size > 0) {
                        p.pops.push_back(pop);
                    } else if (pop.size > 0) {
                        p.skipped += pop.size;
                        const QString k = u"%1 / %2"_s.arg(pop.culture.value_or(u"None"_s),
                                                            pop.religion.value_or(u"None"_s));
                        plan.skipped_pops.insert(k, plan.skipped_pops.value(k, 0) + pop.size);
                    }
                }
            }
            if (opts.states && pr.region && !pr.region->isEmpty() && rs.mod.regions.contains(*pr.region)) {
                p.region = pr.region;
                plan.new_regions[*pr.region].push_back(p.pid);
            }
        }
        if (const positions::Entry *e0 = pos.get(s)) {
            positions::Entry e = *e0;
            const long long dy = md.h - rs.md->h - down;   // (places are counted from the map's bottom edge)
            for (auto &[k, xy] : e.pts)
                xy = {xy.first + double(dx), xy.second + double(dy)};
            p.entry = e;
        }
        p.rec = newprov::NewProv{p.name, colour(pr.rgb), p.sea, std::nullopt, hist_rel, hist_text, info,
                                 std::nullopt, 0};
        plan.provs.push_back(std::move(p));
    }
    plan.nw.resize(plan.src_pid.size());
    for (size_t i = 0; i < plan.src_pid.size(); ++i)
        plan.nw[i] = lut[plan.src_pid[i]];
    plan.missing_tags = QStringList(tags_missing.begin(), tags_missing.end());
    for (const QString &t : tags_kept)
        if (!tags_missing.count(t))
            plan.kept_tags << t;
    for (const QString &t : tags_dropped)
        if (!tags_kept.count(t) && !tags_missing.count(t))
            plan.dropped_tags << t;
    return plan;
}

Flat neighbour_fill(grid::Grid<std::uint16_t> &final, const grid::Grid<std::uint8_t> &mask,
                    const std::vector<std::uint8_t> *avoid)
{
    const long long h = final.h(), w = final.w();
    long long ymin = h, ymax = -1, xmin = w, xmax = -1;
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x)
            if (mask(y, x)) {
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
                xmin = std::min(xmin, x);
                xmax = std::max(xmax, x);
            }
    if (ymax < 0)
        return {};
    const long long y0 = std::max(0LL, ymin - 1), y1 = std::min(h, ymax + 2);
    const long long x0 = std::max(0LL, xmin - 1), x1 = std::min(w, xmax + 2);
    const long long H = y1 - y0, W = x1 - x0;
    auto sub = [&](long long y, long long x) -> std::uint16_t & { return final(y0 + y, x0 + x); };
    std::vector<std::uint8_t> todo(size_t(H * W));
    for (long long y = 0; y < H; ++y)
        for (long long x = 0; x < W; ++x)
            todo[size_t(y * W + x)] = mask(y0 + y, x0 + x);
    bool strict = avoid != nullptr;
    auto avoided = [&](std::uint16_t v) { return size_t(v) < avoid->size() && (*avoid)[v]; };
    const int dirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    std::vector<std::uint16_t> got(size_t(H * W));
    std::vector<std::uint8_t> have(size_t(H * W)), usable(size_t(H * W));
    for (long long it = 0; it < 2 * (H + W); ++it) {
        if (std::none_of(todo.begin(), todo.end(), [](std::uint8_t t) { return t != 0; }))
            break;
        std::fill(got.begin(), got.end(), std::uint16_t(0));
        std::fill(have.begin(), have.end(), std::uint8_t(0));
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                usable[i] = !todo[i] && !(strict && avoided(sub(y, x)));
            }
        bool any = false;
        for (const auto &d : dirs) {
            const int dy = d[0], dx = d[1];
            for (long long y = 0; y < H; ++y)
                for (long long x = 0; x < W; ++x) {
                    const size_t i = size_t(y * W + x);
                    if (!todo[i] || have[i])
                        continue;
                    const long long yy = y - dy, xx = x - dx;   // (np.roll by (dy, dx); the wrapped edge is off)
                    if (yy < 0 || yy >= H || xx < 0 || xx >= W || !usable[size_t(yy * W + xx)])
                        continue;
                    got[i] = sub(yy, xx);
                    have[i] = 1;
                    any = true;
                }
        }
        if (!any) {
            if (strict) {
                strict = false;   // (only the reshaped ones lie next to it: they take it)
                continue;
            }
            break;                // (an island of its own: nothing next to it)
        }
        for (long long y = 0; y < H; ++y)
            for (long long x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                if (have[i]) {
                    sub(y, x) = got[i];
                    todo[i] = 0;
                }
            }
    }
    Flat out;
    for (long long y = 0; y < h; ++y)
        for (long long x = 0; x < w; ++x) {
            if (!mask(y, x))
                continue;
            const bool in_box = y >= y0 && y < y1 && x >= x0 && x < x1;
            if (in_box && todo[size_t((y - y0) * W + (x - x0))])
                continue;
            out.push_back(y * w + x);
        }
    return out;
}

Plan &_borders_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan,
                    const std::vector<long long> &kept, const Opts &opts)
{
    // Onto this mod's provinces: every picked province's pixels go to the province of this mod with the same number
    // (or the same colour). Nothing new is made.
    moddata::Mod &mod = doc.mod;
    plan.mode = u"borders"_s;
    const QString &by = opts.match;
    std::map<moddata::Rgb, long long> rgb_to;
    if (by == u"colour")
        for (const auto &[pid, pr] : mod.provinces)
            rgb_to[pr.rgb] = pid;
    long long smax = 0;
    for (std::uint16_t v : rs.md->idmap.flat())
        smax = std::max<long long>(smax, v);
    std::vector<long long> lut(size_t(smax + 1), 0);
    for (long long s : kept) {
        const moddata::Province &pr = *rs.mod.provinces.get(s);
        std::optional<long long> t;
        if (by == u"number") {
            if (mod.provinces.contains(s))
                t = s;
        } else if (auto it = rgb_to.find(pr.rgb); it != rgb_to.end()) {
            t = it->second;
        }
        if (!t || *t >= md.n) {
            plan.no_target.push_back(s);
        } else {
            lut[size_t(s)] = *t;
            plan.pairs.emplace_back(s, *t);
        }
    }
    std::vector<size_t> keep;
    for (size_t i = 0; i < plan.src_pid.size(); ++i)
        if (lut[plan.src_pid[i]] > 0)
            keep.push_back(i);
    if (keep.empty()) {
        plan.problem = u"None of the picked provinces is in this mod by the same %1."_s.arg(by);
        return plan;
    }
    for (auto *layer : {&plan.terrain, &plan.rivers})
        if (*layer) {
            std::vector<std::uint8_t> v;
            for (size_t i : keep)
                v.push_back((**layer)[i]);
            *layer = std::move(v);
        }
    {
        Flat idx;
        std::vector<std::uint16_t> sp;
        for (size_t i : keep) {
            idx.push_back((*plan.idx)[i]);
            sp.push_back(plan.src_pid[i]);
        }
        plan.idx = std::move(idx);
        plan.src_pid = std::move(sp);
    }
    plan.nw.resize(plan.src_pid.size());
    for (size_t i = 0; i < plan.src_pid.size(); ++i)
        plan.nw[i] = lut[plan.src_pid[i]];
    std::vector<long long> targets;
    {
        std::set<long long> t;
        for (const auto &pr : plan.pairs)
            t.insert(pr.second);
        targets.assign(t.begin(), t.end());
    }
    grid::Grid<std::uint16_t> final = md.idmap;
    auto &flat = final.flat();
    for (size_t i = 0; i < plan.idx->size(); ++i)
        flat[size_t((*plan.idx)[i])] = static_cast<std::uint16_t>(plan.nw[i]);
    const auto &oldf = md.idmap.flat();
    if (opts.clear_old) {   // what they had before and the new shape leaves free
        std::vector<std::uint8_t> is_t(size_t(md.n), 0);
        for (long long t : targets)
            is_t[size_t(t)] = 1;
        std::vector<std::uint8_t> pasted(flat.size(), 0);
        for (std::int64_t i : *plan.idx)
            pasted[size_t(i)] = 1;
        grid::Grid<std::uint8_t> mask(md.h, md.w, 0);
        for (size_t i = 0; i < oldf.size(); ++i)
            mask.flat()[i] = size_t(oldf[i]) < is_t.size() && is_t[oldf[i]] && !pasted[i];
        plan.fill_idx = neighbour_fill(final, mask, &is_t);
        plan.fill_new.clear();
        for (std::int64_t i : *plan.fill_idx)
            plan.fill_new.push_back(flat[size_t(i)]);
    }
    const std::vector<long long> cnt = bincount(flat, size_t(md.n));
    for (long long p = 0; p < md.n; ++p)
        if (md.area[size_t(p)] > 0 && cnt[size_t(p)] < md.area[size_t(p)])
            if (p && mod.provinces.contains(p) && !std::binary_search(targets.begin(), targets.end(), p))
                (cnt[size_t(p)] == 0 ? plan.covered : plan.partly).push_back(p);
    for (long long p : plan.covered) {   // who takes its place: the one with most of its land
        std::vector<std::uint16_t> now;
        for (size_t i = 0; i < oldf.size(); ++i)
            if (oldf[i] == p)
                now.push_back(flat[i]);
        if (!now.empty())
            plan.heirs.insert(p, static_cast<long long>(argmax(bincount(now, 0))));
    }
    for (long long t : targets)
        plan.sizes.insert(t, {md.area[size_t(t)], cnt[size_t(t)]});
    return plan;
}

static QString s_if(size_t n) { return n == 1 ? QString() : u"s"_s; }

QString _apply_borders(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan, const Opts &opts,
                       layerbmp::LandSea *landsea)
{
    auto &bmp = doc.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
    layerbmp::IndexedBitmap *tb = doc.maps.contains(u"terrain_bmp"_s) ? &doc.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s)
                                                                      : nullptr;
    layerbmp::IndexedBitmap *rb = doc.maps.contains(u"rivers_bmp"_s) ? &doc.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s)
                                                                     : nullptr;
    bmp.remember_file();
    Flat idx = *plan.idx;
    std::vector<std::uint16_t> nw;
    for (std::int64_t v : plan.nw)
        nw.push_back(static_cast<std::uint16_t>(v));
    if (plan.fill_idx && !plan.fill_idx->empty()) {
        idx.insert(idx.end(), plan.fill_idx->begin(), plan.fill_idx->end());
        for (std::int64_t v : plan.fill_new)
            nw.push_back(static_cast<std::uint16_t>(v));
    }
    std::vector<std::uint16_t> old(idx.size());
    for (size_t i = 0; i < idx.size(); ++i)
        old[i] = md.idmap.flat()[size_t(idx[i])];
    std::vector<layerbmp::LandSea::Change> extra;
    if (!plan.terrain && !plan.rivers && landsea) {
        Flat ci;
        std::vector<long long> cv;
        for (size_t i = 0; i < idx.size(); ++i)
            if (old[i] != nw[i]) {
                ci.push_back(idx[i]);
                cv.push_back(nw[i]);
            }
        if (!ci.empty())
            extra = landsea->changes(ci, cv);
    }
    QStringList lists{u"provinces_bmp"_s};
    if (tb && plan.terrain)
        lists << u"terrain_bmp"_s;
    if (rb && plan.rivers)
        lists << u"rivers_bmp"_s;
    for (const auto &c : extra)
        if (!lists.contains(c.bitmap->kind))
            lists << c.bitmap->kind;
    std::optional<provdelete::Job> gone;
    if (opts.replace && !plan.covered.empty())
        gone = provdelete::prepare(doc, md, plan.covered, u"none"_s, plan.heirs, landsea);
    auto run = [&]() -> long long {
        bmp.commit(idx, old, nw);
        if (tb && plan.terrain)
            tb->apply(*plan.idx, *plan.terrain);
        if (rb && plan.rivers)
            rb->apply(*plan.idx, *plan.rivers);
        for (const auto &c : extra)
            c.bitmap->apply(c.idx, c.vals);
        if (gone)
            gone->run();
        return static_cast<long long>(plan.pairs.size());
    };
    QList<long long> pids;
    QStringList rids;
    if (gone) {
        pids = gone->pids;
        rids = gone->rids;
        rids.sort();
        for (const QString &k : gone->lists)
            if (!lists.contains(k))
                lists << k;
    }
    const size_t n = plan.pairs.size();
    doc.edit(u"Shapes of %1 province%2 from %3"_s.arg(n).arg(s_if(n), rs.src.name), pids, run, rids, pids, lists);
    return u"%1 province%2 of this mod got %3 shape from %4%5 — Ctrl+Z undoes it, Apply writes it."_s.arg(n).arg(
        s_if(n), n == 1 ? u"its"_s : u"their"_s, rs.src.name,
        gone ? u"; %1 left without land deleted"_s.arg(gone->pids.size()) : QString());
}

QString apply_plan(document::Document &doc, mapview::MapData &md, RegionSource &rs, Plan &plan, const Opts &opts,
                   layerbmp::LandSea *landsea)
{
    // The import as one step. -> the text for the status line.
    if (plan.mode == u"borders")
        return _apply_borders(doc, md, rs, plan, opts, landsea);
    moddata::Mod &mod = doc.mod;
    auto &defs = doc.map<newprov::ProvinceDefs>(u"definitions"_s);
    auto &dm = doc.map<mapfiles::DefaultMapFile>(u"default_map"_s);
    auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);
    auto &bmp = doc.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
    auto opt_map = [&](const QString &k) { return doc.maps.contains(k); };
    localisation::LocEdits *loc = opt_map(u"loc"_s) ? &doc.map<localisation::LocEdits>(u"loc"_s) : nullptr;
    layerbmp::IndexedBitmap *tb = opt_map(u"terrain_bmp"_s) ? &doc.map<layerbmp::IndexedBitmap>(u"terrain_bmp"_s) : nullptr;
    layerbmp::IndexedBitmap *rb = opt_map(u"rivers_bmp"_s) ? &doc.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s) : nullptr;
    mapfiles::MembershipFile *cont = opt_map(u"continent"_s) ? &doc.map<mapfiles::MembershipFile>(u"continent"_s) : nullptr;
    mapfiles::MembershipFile *clim = opt_map(u"climate"_s) ? &doc.map<mapfiles::MembershipFile>(u"climate"_s) : nullptr;
    bmp.remember_file();
    std::vector<std::uint16_t> old(plan.idx->size()), nw(plan.idx->size());
    for (size_t i = 0; i < plan.idx->size(); ++i) {
        old[i] = md.idmap.flat()[size_t((*plan.idx)[i])];
        nw[i] = static_cast<std::uint16_t>(plan.nw[i]);
    }
    QStringList rids;
    for (const auto &[rid, _lst] : plan.new_regions) {   // their states: this mod's of the same key, or a new one
        if (!mod.any_region(rid)) {
            const QString nm = rs.mod.loc.value(rid);
            mod.regions.insert(rid, moddata::Region{rid, nm.isEmpty() ? rid : nm, {}});
            if (!doc.region_orig.contains(rid))
                doc.region_orig.insert(rid, {});
        }
        rids << rid;
    }
    std::vector<layerbmp::LandSea::Change> extra;
    if (!plan.terrain && !plan.rivers && landsea) {   // no layers brought: land / sea still has to fit
        Flat ci;
        std::vector<long long> cv;
        for (size_t i = 0; i < old.size(); ++i)
            if (old[i] != nw[i]) {
                ci.push_back((*plan.idx)[i]);
                cv.push_back(nw[i]);
            }
        if (!ci.empty())
            extra = landsea->changes(ci, cv);
    }
    QStringList lists{u"definitions"_s, u"default_map"_s, u"provinces_bmp"_s, u"positions"_s};
    if (cont)
        lists << u"continent"_s;
    if (clim)
        lists << u"climate"_s;
    if (loc)
        lists << u"loc"_s;
    if (tb && plan.terrain)
        lists << u"terrain_bmp"_s;
    if (rb && plan.rivers)
        lists << u"rivers_bmp"_s;
    for (const auto &c : extra)
        if (!lists.contains(c.bitmap->kind))
            lists << c.bitmap->kind;
    QList<long long> land;
    for (const Prov &p : plan.provs)
        if (!p.sea)
            land << p.pid;
    std::optional<provdelete::Job> gone;   // this mod's provinces that end up completely covered
    if (opts.replace && !plan.covered.empty()) {
        std::vector<const Prov *> by_px;
        for (const Prov &p : plan.provs)
            by_px.push_back(&p);
        std::stable_sort(by_px.begin(), by_px.end(), [](const Prov *a, const Prov *b) { return a->px < b->px; });
        OrderedMap<long long, long long> heirs;
        for (const Prov *p : by_px)
            if (!p->sea)
                heirs.insert(p->under, p->pid);
        plan.heirs = heirs;   // (covered province -> the imported one in its place)
        gone = provdelete::prepare(doc, md, plan.covered, u"none"_s, heirs, landsea);
    }
    auto run = [&]() -> long long {
        for (const Prov &p : plan.provs) {
            defs.add(p.pid, p.rec);
            if (dm.max_provinces && *dm.max_provinces < p.pid + 1)
                dm.max_provinces = p.pid + 1;
            if (p.sea && std::find(dm.sea.begin(), dm.sea.end(), p.pid) == dm.sea.end())
                dm.sea.push_back(p.pid);
            const moddata::Province *up = mod.provinces.get(p.under);
            if (!p.sea && up && !up->sea) {   // what lay there: its continent and climate
                const std::optional<QString> under_region = up->region;
                for (mapfiles::MembershipFile *mf : {cont, clim}) {
                    if (!mf)
                        continue;
                    const QStringList of = mf->of(p.under);
                    if (!of.isEmpty() && !of[0].isEmpty())
                        mf->assign({p.pid}, of[0]);
                }
                // … and, when theirs aren't brought, its state
                if (!p.region && under_region && mod.regions.contains(*under_region))
                    mod.regions[*under_region].provinces.push_back(p.pid);
            }
        }
        for (const auto &[rid, lst] : plan.new_regions) {
            moddata::Region *r = mod.regions.get(rid);
            if (!r)
                r = mod.utility.get(rid);
            r->provinces.insert(r->provinces.end(), lst.begin(), lst.end());
            if (loc && mod.loc.value(rid).isEmpty() && !rs.mod.loc.value(rid).isEmpty())
                loc->set(rid, *rs.mod.loc.get(rid), u"state"_s);
        }
        doc._sync_regions();
        bmp.commit(*plan.idx, old, nw);
        if (tb && plan.terrain)
            tb->apply(*plan.idx, *plan.terrain);
        if (rb && plan.rivers)
            rb->apply(*plan.idx, *plan.rivers);
        for (const auto &c : extra)
            c.bitmap->apply(c.idx, c.vals);
        for (const Prov &p : plan.provs) {
            for (const moddata::Pop &pop : p.pops)
                doc._add_to(p.pid, pop, pop.size);
            if (p.entry)
                pf.set_entry(p.pid, *p.entry);
            defs.update(p.pid, bmp.version, md.area[size_t(p.pid)]);
        }
        if (gone)   // (they have no pixel left now: deleted with all they had)
            gone->run();
        return static_cast<long long>(plan.provs.size());
    };
    std::set<QString> touched(rids.begin(), rids.end());
    for (const Prov &p : plan.provs) {
        const moddata::Province *u = mod.provinces.get(p.under);
        if (!p.sea && !p.region && u && u->region && mod.regions.contains(*u->region))
            touched.insert(*u->region);
    }
    QList<long long> pops_of = land, hist_of = land;
    if (gone) {
        touched.insert(gone->rids.begin(), gone->rids.end());
        pops_of += gone->pids;
        hist_of += gone->pids;
        for (const QString &k : gone->lists)
            if (!lists.contains(k))
                lists << k;
    }
    const size_t n = plan.provs.size();
    doc.edit(u"Import %1 province%2 from %3"_s.arg(n).arg(s_if(n), rs.src.name), pops_of, run,
             QStringList(touched.begin(), touched.end()), hist_of, lists);
    return u"%1 province%2 imported from %3 as #%4 … #%5%6 — Ctrl+Z undoes it, Apply writes it."_s.arg(n)
        .arg(s_if(n), rs.src.name)
        .arg(plan.provs.front().pid)
        .arg(plan.provs.back().pid)
        .arg(gone ? u"; %1 covered province%2 of this mod deleted"_s.arg(gone->pids.size())
                        .arg(s_if(size_t(gone->pids.size())))
                  : QString());
}

std::vector<Rgb8> owner_lut(const moddata::Mod &mod, const std::function<const history::ProvState &(long long)> &state_of,
                            long long n, bool by_owner)
{
    std::vector<Rgb8> lut(size_t(std::max(0LL, n)), Rgb8{30, 34, 44});
    for (const auto &[pid, pr] : mod.provinces) {
        if (pid >= n)
            continue;
        if (pr.sea) {
            lut[size_t(pid)] = {46, 78, 112};
            continue;
        }
        std::optional<Rgb8> c;
        if (by_owner) {
            const std::optional<QString> tag = state_of(pid).owner;
            const moddata::Country *cn = tag && !tag->isEmpty() ? mod.countries.get(*tag) : nullptr;
            if (cn) {
                const auto [r, g, b] = cn->color;
                c = Rgb8{std::uint8_t(r), std::uint8_t(g), std::uint8_t(b)};
            } else if (tag && !tag->isEmpty()) {
                long long hsh = 0;
                for (qsizetype i = 0; i < tag->size(); ++i)
                    hsh += (*tag)[i].unicode() * (i * 7 + 3);
                c = Rgb8{std::uint8_t(70 + hsh % 150), std::uint8_t(70 + (hsh / 7) % 150),
                         std::uint8_t(70 + (hsh / 53) % 150)};
            }
        }
        lut[size_t(pid)] = c.value_or(Rgb8{118, 114, 104});
    }
    return lut;
}

}  // namespace regionimport
