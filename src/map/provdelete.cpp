// provdelete.py — line by line.
#include "map/provdelete.h"
#include "core/countryhist.h"
#include "core/fs.h"
#include "core/mapfiles.h"
#include "core/positions.h"
#include "core/provrenumber.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "map/newprov.h"
#include "map/provbmp.h"

#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <map>
#include <set>

using namespace Qt::StringLiterals;

namespace provdelete {

using OptStr = std::optional<QString>;

static long long most_populous(document::Document &doc, const std::vector<long long> &mine)
{
    long long best = mine[0];                      // max(mine, key=(total, -q)): the first largest
    for (long long q : mine)
        if (std::make_pair(doc.total(q), -q) > std::make_pair(doc.total(best), -best))
            best = q;
    return best;
}

std::pair<OrderedMap<QString, std::optional<long long>>, std::vector<oob::Fix>>
followers(document::Document &doc, const std::vector<long long> &pids, const OrderedMap<long long, long long> &heirs)
{
    const moddata::Mod &mod = doc.mod;
    const std::set<long long> S(pids.begin(), pids.end());
    countryhist::NationEdits *nat = nullptr;
    if (const store::StoreP *np = doc.maps.get(u"nations"_s))
        nat = dynamic_cast<countryhist::NationEdits *>(np->get());
    oob::OOBEdits *ob = nullptr;
    if (const store::StoreP *op = doc.maps.get(u"oob"_s))
        ob = dynamic_cast<oob::OOBEdits *>(op->get());
    OrderedMap<QString, std::optional<long long>> caps;
    std::vector<oob::Fix> todo;
    if (nat) {
        OrderedMap<QString, std::vector<long long>> owned;
        for (const auto &[p, pr] : mod.provinces)
            if (!pr.sea && !S.count(p)) {
                const OptStr o = doc.prov_state(p).owner;
                if (o && !o->isEmpty())
                    owned[*o].push_back(p);
            }
        for (const auto &kv : mod.countries) {
            const QString &tag = kv.first;
            if (!nat->hist.files.contains(tag))
                continue;
            const OptStr c = nat->state(tag).at(u"capital"_s);
            if (c && !c->isEmpty() && py::isdigit(*c) && S.count(py::to_int(*c))) {
                const std::vector<long long> mine = owned.value(tag);
                const long long *h = heirs.get(py::to_int(*c));
                if (h && std::find(mine.begin(), mine.end(), *h) != mine.end())
                    caps.insert(tag, *h);
                else
                    caps.insert(tag, mine.empty() ? std::nullopt : std::optional(most_populous(doc, mine)));
            }
        }
    }
    if (ob) {
        QStringList tags(ob->tags_with_units().begin(), ob->tags_with_units().end());
        std::sort(tags.begin(), tags.end());
        for (const QString &tag : tags) {
            if (!mod.countries.contains(tag))
                continue;
            const oob::OOB &st = ob->state(tag);
            for (size_t ai = 0; ai < st.armies.size(); ++ai) {
                const oob::Army &a = st.armies[ai];
                if (a.location && S.count(*a.location))
                    todo.push_back({u"army_loc"_s, tag, static_cast<long long>(ai)});
                for (size_t ri = 0; ri < a.regiments.size(); ++ri)
                    if (a.regiments[ri].home && S.count(*a.regiments[ri].home))
                        todo.push_back({u"home"_s, tag, static_cast<long long>(ai), static_cast<long long>(ri)});
            }
            for (size_t ni = 0; ni < st.navies.size(); ++ni)
                if (st.navies[ni].location && S.count(*st.navies[ni].location))
                    todo.push_back({u"navy_loc"_s, tag, static_cast<long long>(ni)});
        }
    }
    return {caps, todo};
}

// os.walk(top): (folder, its files) top-down, the folders and files in scandir order
static void walk(const QString &top, std::vector<std::pair<QString, QStringList>> &out)
{
    QStringList files, dirs;
    for (const fs::DirEntry &e : fs::scandir(top))
        (e.is_dir ? dirs : files) << e.name;
    out.emplace_back(top, files);
    for (const QString &d : dirs)
        walk(pypath::join(top, d), out);
}

OrderedMap<long long, std::vector<std::pair<QString, long long>>> script_mentions(const moddata::Mod &mod,
                                                                                  const std::vector<long long> &pids)
{
    OrderedMap<QString, long long> S;
    for (long long p : pids)
        S.insert(QString::number(p), p);
    OrderedMap<long long, std::vector<std::pair<QString, long long>>> out;
    if (S.isEmpty())
        return out;
    QStringList alts;
    for (const auto &kv : S)
        alts << kv.first;
    std::stable_sort(alts.begin(), alts.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    const QRegularExpression quick(u"(?<![\\w.])(?:%1)(?![\\w.])"_s.arg(alts.join(u'|')),
                                   QRegularExpression::UseUnicodePropertiesOption);
    QHash<QString, QString> same;                  // (nothing is changed here: the places are only counted)
    for (const auto &kv : S)
        same.insert(kv.first, kv.first);
    for (const QString &folder : provrenumber::SCRIPT_FOLDERS) {
        const QString d = pypath::join(mod.root, folder.split(u'/'));
        if (!fs::isdir(d))
            continue;
        std::vector<std::pair<QString, QStringList>> tree;
        walk(d, tree);
        for (auto &[dd, fs_] : tree) {
            std::sort(fs_.begin(), fs_.end());
            for (const QString &fn : fs_) {
                if (!py::lower(fn).endsWith(u".txt"))
                    continue;
                const QString path = pypath::join(dd, fn);
                if (!fs::isfile(path))
                    continue;
                const QString text = script::read_text(path);
                if (!quick.match(text).hasMatch())
                    continue;
                const auto res = provrenumber::rewrite_script(text, same);   // (kept: the loop reads its member)
                for (const auto &[k, n] : res.hits)
                    out[*S.get(k)].emplace_back(pypath::relpath(path, mod.root).replace(u'\\', u'/'), n);
            }
        }
    }
    return out;
}

OrderedMap<long long, long long> heirs_of(const std::vector<long long> &pids, const std::vector<std::uint16_t> &old,
                                          const std::vector<std::uint16_t> &nw)
{
    OrderedMap<long long, long long> out;
    for (long long p : pids) {
        std::map<long long, long long> cnt;
        for (size_t i = 0; i < old.size(); ++i)
            if (old[i] == p && nw[i] > 0)
                cnt[nw[i]] += 1;
        if (cnt.empty())
            continue;
        long long best = cnt.begin()->first;      // bincount(...).argmax(): the lowest id among the most
        for (const auto &[v, c] : cnt)
            if (c > cnt[best])
                best = v;
        out.insert(p, best);
    }
    return out;
}

Refill refill(const mapview::MapData &md, const std::vector<long long> &pids)
{
    const long long H = md.idmap.h(), W = md.idmap.w();
    grid::Grid<std::int64_t> work(H, W);
    for (size_t i = 0; i < work.flat().size(); ++i)
        work.flat()[i] = md.idmap.flat()[i];
    std::vector<std::uint8_t> gone(size_t(md.n), 0);
    for (long long p : pids)
        if (0 < p && p < md.n)
            gone[size_t(p)] = 1;
    const auto &sea = md.sea;
    for (long long pid : pids) {
        if (!(0 < pid && pid < md.n) || !md.area[pid])
            continue;
        long long bx0 = md.x0[pid], by0 = md.y0[pid], bx1 = md.x1[pid], by1 = md.y1[pid];
        if (bx1 < bx0 || by1 < by0) {             // (its box isn't known: the whole map is looked through)
            bx0 = 0;
            by0 = 0;
            bx1 = W - 1;
            by1 = H - 1;
        }
        const long long y0 = std::max(0LL, by0 - 1), y1 = std::min(H, by1 + 2), x0 = std::max(0LL, bx0 - 1),
                        x1 = std::min(W, bx1 + 2);
        const long long h = y1 - y0, w = x1 - x0;
        auto out = [&](long long y, long long x) -> std::int64_t & { return work(y0 + y, x0 + x); };
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x)
                if (out(y, x) == pid)
                    out(y, x) = -1;
        const bool land = !sea[size_t(pid)];
        for (bool same_kind : {true, false}) {
            bool changed = true;
            auto any_open = [&] {
                for (long long y = 0; y < h; ++y)
                    for (long long x = 0; x < w; ++x)
                        if (out(y, x) == -1)
                            return true;
                return false;
            };
            while (changed && any_open()) {
                changed = false;
                for (const auto &[dy, dx] : {std::pair{0, 1}, {0, -1}, {1, 0}, {-1, 0}}) {
                    // a: a cell, b: its neighbour (dy, dx) back — every cell of this direction takes the value its
                    // neighbour had before the direction began (numpy: a[take] = b[take])
                    std::vector<std::tuple<long long, long long, std::int64_t>> takes;
                    for (long long y = std::max(0, dy); y < h + std::min(0, dy); ++y)
                        for (long long x = std::max(0, dx); x < w + std::min(0, dx); ++x) {
                            if (out(y, x) != -1)
                                continue;
                            const std::int64_t b = out(y - dy, x - dx);
                            if (b <= 0 || gone[size_t(b)])
                                continue;
                            if (same_kind && (land ? bool(sea[size_t(b)]) : !sea[size_t(b)]))
                                continue;
                            takes.emplace_back(y, x, b);
                        }
                    for (const auto &[y, x, b] : takes)
                        out(y, x) = b;
                    if (!takes.empty())
                        changed = true;
                }
            }
        }
        for (long long y = 0; y < h; ++y)
            for (long long x = 0; x < w; ++x)
                if (out(y, x) == -1)
                    out(y, x) = 0;
    }
    Refill r;
    const auto &flat = md.idmap.flat();
    for (size_t i = 0; i < flat.size(); ++i)
        if (gone[flat[i]]) {
            r.idx.push_back(std::int64_t(i));
            r.old.push_back(flat[i]);
            r.nw.push_back(std::uint16_t(work.flat()[i]));
        }
    return r;
}

QStringList describe(document::Document &doc, const mapview::MapData &md, std::vector<long long> pids0)
{
    // What deleting these provinces does, in plain lines (for the red box before the second click).
    moddata::Mod &mod = doc.mod;
    std::vector<long long> pids;
    for (long long p : pids0)
        if (mod.provinces.contains(p))
            pids.push_back(p);
    auto pname = [&](long long p) {
        const QString n = py::strip(mod.provinces.get(p)->name);
        return n.isEmpty() ? u"?"_s : n;
    };
    QStringList rows;
    long long people = 0;
    for (long long p : pids)
        if (const auto *l = doc.pops.get(p))
            for (const auto &pp : *l)
                people += pp.size;
    QStringList nm;
    for (size_t i = 0; i < pids.size() && i < 6; ++i)
        nm << u"#%1 %2"_s.arg(QString::number(pids[i]), pname(pids[i]));
    QString names = nm.join(u", "_s);
    if (pids.size() > 6)
        names += u" … (+%1)"_s.arg(pids.size() - 6);
    rows << u"%1 province%2: %3."_s.arg(QString::number(pids.size()), pids.size() == 1 ? QString() : u"s"_s, names);
    rows << u"Their land goes to the provinces around them (each pixel to the nearest; land stays land where a land "
            u"neighbour is). Their history files, pops (%1 people), state, continent, climate, places and the adjacency "
            u"rows that name them go."_s.arg(py::fmt_commas(double(people), 0));
    std::vector<long long> left;
    long long top = 0;
    for (const auto &kv : mod.provinces)
        if (std::find(pids.begin(), pids.end(), kv.first) == pids.end()) {
            left.push_back(kv.first);
            top = std::max(top, kv.first);
        }
    const Refill rf = refill(md, pids);
    const auto heirs = heirs_of(pids, rf.old, rf.nw);
    std::set<long long> fr;
    for (const auto &kv : mod.free_slots)
        fr.insert(kv.first);
    for (long long p : pids)
        if (p < top)
            fr.insert(p);
    provrenumber::Heirs hh;
    for (const auto &[k, v] : heirs)
        hh.insert(k, v);
    const auto moves = provrenumber::moves_for(left, std::vector<long long>(fr.begin(), fr.end()), hh).first;
    if (!moves.isEmpty()) {
        std::vector<std::pair<long long, long long>> mv(moves.begin(), moves.end());
        std::stable_sort(mv.begin(), mv.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
        QStringList ms;
        for (size_t i = 0; i < mv.size() && i < 5; ++i)
            ms << u"#%1 %2 → #%3"_s.arg(QString::number(mv[i].first), pname(mv[i].first), QString::number(mv[i].second));
        const bool one = moves.size() == 1;
        rows << u"On Apply the highest province%1 the freed number%2: %3%4. The numbers must stay without a gap, and the "
                u"game crashes on an empty one — so the files that name %5 are rewritten (each backed up) and the mod is "
                u"read again."_s.arg(one ? u" takes"_s : u"s take"_s, one ? QString() : u"s"_s, ms.join(u", "_s),
                                     moves.size() > 5 ? u" …"_s : QString(), one ? u"it"_s : u"them"_s);
    }
    const auto [caps, todo] = followers(doc, pids, heirs);
    std::vector<std::pair<QString, std::optional<long long>>> cs(caps.begin(), caps.end());
    std::sort(cs.begin(), cs.end());
    for (const auto &[tag, best] : cs) {
        if (best) {
            bool is_heir = false;
            for (const auto &kv : heirs)
                is_heir = is_heir || kv.second == *best;
            rows << u"Capital of %1: moves to #%2 %3, %4."_s.arg(tag, QString::number(*best), mod.provinces.get(*best)->name,
                                                                is_heir ? u"the province that takes its land"_s
                                                                        : u"its most populous province"_s);
        } else {
            rows << u"Capital of %1: it owns nothing else, so it no longer exists at the start (its capital line is left "
                    u"as it is)."_s.arg(tag);
        }
    }
    if (!todo.empty()) {
        long long na = 0, nn = 0, nh = 0;
        for (const oob::Fix &f : todo) {
            na += f.what == u"army_loc";
            nn += f.what == u"navy_loc";
            nh += f.what == u"home";
        }
        QStringList parts;
        if (na)
            parts << u"%1 arm%2"_s.arg(na).arg(na == 1 ? u"y"_s : u"ies"_s);
        if (nn)
            parts << u"%1 nav%2"_s.arg(nn).arg(nn == 1 ? u"y"_s : u"ies"_s);
        if (nh)
            parts << u"%1 brigade%2 raised there"_s.arg(nh).arg(nh == 1 ? QString() : u"s"_s);
        rows << u"%1 move: an army to its nation's capital, a navy to its nation's nearest port, a brigade's home to the "
                u"nearest province with soldiers left (it goes when there is none)."_s.arg(parts.join(u", "_s));
    }
    const auto hits = script_mentions(mod, pids);
    for (long long q : pids) {                     // the switch is seamless: no script is left naming it
        const auto *hq = hits.get(q);
        if (!hq)
            continue;
        long long n = 0;
        QStringList fl;
        for (const auto &[f, k] : *hq) {
            n += k;
            if (fl.size() < 3)
                fl << f;
        }
        const QString files = fl.join(u", "_s) + (hq->size() > 3 ? u" …"_s : QString());
        const bool one = n == 1;
        if (const long long *h = heirs.get(q)) {
            rows << u"Scripts: the %1 place%2 that name%3 #%4 (%5) will name #%6 %7 instead — the province that takes "
                    u"most of its land."_s.arg(QString::number(n), one ? QString() : u"s"_s, one ? u"s"_s : QString(),
                                              QString::number(q), files, QString::number(moves.value(*h, *h)), pname(*h));
        } else {
            rows << u"Scripts: the %1 place%2 that name%3 #%4 (%5) %6 taken out — its land goes to no province: a "
                    u"condition about it becomes never true, an effect on it is removed."_s.arg(
                        QString::number(n), one ? QString() : u"s"_s, one ? u"s"_s : QString(), QString::number(q), files,
                        one ? u"is"_s : u"are"_s);
        }
    }
    if (hits.isEmpty())
        rows << u"No script names the number%1."_s.arg(pids.size() == 1 ? QString() : u"s"_s);
    return rows;
}

std::optional<Job> prepare(document::Document &doc, mapview::MapData &md, const std::vector<long long> &pids0,
                           const QString &pixels, const OrderedMap<long long, long long> &successor,
                           layerbmp::LandSea *landsea)
{
    // The delete worked out, not done yet (so it can be one step together with something else), or None. pixels
    // "none": the map is left alone; "blank": their pixels become no province; "neighbours": their land goes to what
    // lies around them. successor: {deleted province: the province that takes its place}.
    moddata::Mod &mod = doc.mod;
    auto &defs = doc.map<newprov::ProvinceDefs>(u"definitions"_s);
    auto &dm = doc.map<mapfiles::DefaultMapFile>(u"default_map"_s);
    auto &pf = doc.map<positions::PositionsFile>(u"positions"_s);
    adjacency::AdjacencyFile *adj = doc.maps.contains(u"adjacency"_s) ? &doc.map<adjacency::AdjacencyFile>(u"adjacency"_s) : nullptr;
    auto &bmp = doc.map<provbmp::ProvinceBitmap>(u"provinces_bmp"_s);
    mapfiles::MembershipFile *cont = doc.maps.contains(u"continent"_s) ? &doc.map<mapfiles::MembershipFile>(u"continent"_s) : nullptr;
    mapfiles::MembershipFile *clim = doc.maps.contains(u"climate"_s) ? &doc.map<mapfiles::MembershipFile>(u"climate"_s) : nullptr;
    std::vector<long long> pids;
    for (long long p : pids0)
        if (std::find(pids.begin(), pids.end(), p) == pids.end() && mod.provinces.contains(p))
            pids.push_back(p);
    if (pids.empty())
        return std::nullopt;
    const std::set<long long> S(pids.begin(), pids.end());
    Refill rf;
    if (pixels == u"blank") {
        const auto &flat = md.idmap.flat();
        for (size_t i = 0; i < flat.size(); ++i)
            if (S.count(flat[i]) && flat[i] < md.n) {
                rf.idx.push_back(std::int64_t(i));
                rf.old.push_back(flat[i]);
                rf.nw.push_back(0);
            }
    } else if (pixels != u"none") {
        rf = refill(md, pids);
    }
    std::vector<layerbmp::LandSea::Change> extra;
    if (!rf.idx.empty()) {
        bmp.remember_file();
        Flat ci;
        std::vector<long long> cv;
        for (size_t i = 0; i < rf.idx.size(); ++i)
            if (rf.old[i] != rf.nw[i]) {
                ci.push_back(rf.idx[i]);
                cv.push_back(rf.nw[i]);
            }
        if (landsea && !ci.empty() && pixels != u"blank")
            extra = landsea->changes(ci, cv);
    }
    QStringList rids;
    for (const auto *tbl : {&mod.regions, &mod.utility})
        for (const auto &[rid, r] : *tbl)
            if (std::any_of(r.provinces.begin(), r.provinces.end(), [&](long long p) { return S.count(p) > 0; }))
                rids << rid;
    std::sort(rids.begin(), rids.end());
    const auto [caps, units] = followers(doc, pids);
    OrderedMap<long long, long long> heirs = pixels == u"neighbours" ? heirs_of(pids, rf.old, rf.nw) : OrderedMap<long long, long long>();
    for (const auto &[q, h] : successor)
        if (S.count(q))
            heirs.insert(q, h);                    // what stands for each in the scripts
    QList<long long> uids;
    if (adj)
        for (const auto &r : adj->rows)
            if (S.count(r.a) || S.count(r.b) || S.count(r.through))
                uids << r.uid;
    QStringList lists = {u"definitions"_s, u"default_map"_s, u"positions"_s};
    if (!rf.idx.empty())
        lists << u"provinces_bmp"_s;
    if (cont)
        lists << u"continent"_s;
    if (clim)
        lists << u"climate"_s;
    if (adj)
        lists << u"adjacency"_s;
    for (const auto &e : extra)
        if (!lists.contains(e.bitmap->kind))
            lists << e.bitmap->kind;
    if (!caps.isEmpty())
        lists << u"nations"_s;
    if (!units.empty())
        lists << u"oob"_s;

    auto run = [&doc, &md, &mod, &defs, &dm, &pf, adj, &bmp, cont, clim, pids, S, rf, extra, rids, caps = caps,
                units = units, heirs, uids]() -> long long {
        if (!rf.idx.empty()) {
            bmp.commit(rf.idx, rf.old, rf.nw);
            for (const auto &e : extra)
                e.bitmap->apply(e.idx, e.vals);
        }
        for (const QString &rid : rids) {
            moddata::Region *r = mod.regions.get(rid);
            if (!r)
                r = mod.utility.get(rid);
            std::vector<long long> keep;
            for (long long p : r->provinces)
                if (!S.count(p))
                    keep.push_back(p);
            r->provinces = keep;
        }
        for (mapfiles::MembershipFile *mf : {cont, clim})
            if (mf)
                for (auto &[name, lst] : mf->lists)
                    if (std::any_of(lst.begin(), lst.end(), [&](long long p) { return S.count(p) > 0; })) {
                        std::vector<long long> keep;
                        for (long long p : lst)
                            if (!S.count(p))
                                keep.push_back(p);
                        lst = keep;
                    }
        std::vector<long long> sea_keep;
        for (long long p : dm.sea)
            if (!S.count(p))
                sea_keep.push_back(p);
        dm.sea = sea_keep;
        for (long long p : pids) {
            pf.entries.remove(p);
            if (auto *l = doc.pops.get(p); l && !l->empty())
                doc.pops.insert(p, {});
            doc.prov_want.remove(p);
            doc.prov_orig.remove(p);
        }
        if (!uids.isEmpty())
            adj->del(uids);
        defs.del(QList<long long>(pids.begin(), pids.end()), heirs);
        doc._sync_regions();
        if (dm.max_provinces && !mod.provinces.isEmpty()) {
            long long top = 0;
            for (const auto &kv : mod.provinces)
                top = std::max(top, kv.first);
            dm.max_provinces = top + 1;            // always the highest province + 1 (unused rows lie below)
        }
        // what stood there follows: capitals, then armies / navies / brigades (the Errors tab's own rules)
        if (!caps.isEmpty()) {
            auto &nat = doc.map<countryhist::NationEdits>(u"nations"_s);
            for (const auto &kv : caps) {              // (looked at now: what its nation owns after all of it)
                const QString &tag = kv.first;
                std::vector<long long> mine;
                for (const auto &[p, pr] : mod.provinces)
                    if (!pr.sea && doc.prov_state(p).owner == OptStr(tag))
                        mine.push_back(p);
                const long long *heir = heirs.get(py::to_int(nat.state(tag).at(u"capital"_s).value_or(u"0"_s)));
                std::optional<long long> best;
                if (heir && std::find(mine.begin(), mine.end(), *heir) != mine.end())
                    best = *heir;
                else if (!mine.empty())
                    best = most_populous(doc, mine);
                if (best)
                    nat.edit(tag, [b = *best](countryhist::NationState &st) { st[u"capital"_s] = QString::number(b); });
            }
        }
        if (!units.empty()) {
            const oob::Centers c{[&md](long long p) { return md.cx[size_t(p)]; }, [&md](long long p) { return md.cy[size_t(p)]; }, md.n};
            oob::fix_units(doc, c, units)();
        }
        return static_cast<long long>(pids.size());
    };
    QStringList first5;
    for (size_t i = 0; i < pids.size() && i < 5; ++i)
        first5 << u"#%1"_s.arg(pids[i]);
    Job job;
    job.label = u"Delete %1 province%2 (%3%4)"_s.arg(QString::number(pids.size()), pids.size() == 1 ? QString() : u"s"_s,
                                                     first5.join(u", "_s), pids.size() > 5 ? u" …"_s : QString());
    job.pids = QList<long long>(pids.begin(), pids.end());
    job.rids = rids;
    job.lists = lists;
    job.run = run;
    return job;
}

long long delete_provinces(document::Document &doc, mapview::MapData &md, const std::vector<long long> &pids,
                           const QString &pixels, layerbmp::LandSea *landsea)
{
    // Delete provinces as one step. -> how many were deleted.
    const auto job = prepare(doc, md, pids, pixels, {}, landsea);
    if (!job)
        return 0;
    return doc.edit(job->label, job->pids, job->run, job->rids, job->pids, job->lists);
}

}  // namespace provdelete
