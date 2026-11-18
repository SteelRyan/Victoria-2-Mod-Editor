// popcaps.py — the pop type files' promotion shutdowns and who starts above them. Line by line.
#include "core/popcaps.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/pypath.h"
#include "core/pytext.h"

#include <QHash>
#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;

namespace popcaps {

using script::Node;
using script::Nodes;

const double SHUT = -100.0;

static std::optional<double> _num(const std::optional<QString> &v) { return v ? py::to_float(*v) : std::nullopt; }
static QString low_key(const Node &n) { return n.has_key ? py::lower(n.key) : QString(); }   // (n.key or "").lower()

static void _modifiers(const Nodes &nodes, std::vector<const Node *> &out)
{
    // Every `modifier = { … }` of a promotion target, the ones inside `group = { … }` too.
    for (const Node &n : nodes) {
        if (!n.is_block || !n.has_key || n.key.isEmpty())
            continue;
        const QString k = py::lower(n.key);
        if (k == u"modifier")
            out.push_back(&n);
        else if (k == u"group")
            _modifiers(n.kids, out);
    }
}

std::optional<std::pair<QString, double>> _share(const Node &mod_node, const QString &ptype)
{
    // (scope, share) when the modifier asks for nothing but a share of `ptype`, else None.
    std::vector<const Node *> conds;
    for (const Node &c : mod_node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) != u"factor")
            conds.push_back(&c);
    if (conds.size() != 1)
        return std::nullopt;
    const Node &c = *conds[0];
    const QString k = py::lower(c.key);
    if (!c.is_block) {
        const auto v = _num(c.value);
        return (k == ptype && v) ? std::optional<std::pair<QString, double>>({u"province"_s, *v}) : std::nullopt;
    }
    if (k == u"state_scope") {
        std::vector<const Node *> inner;
        for (const Node &x : c.kids)
            if (x.has_key && !x.key.isEmpty())
                inner.push_back(&x);
        if (inner.size() == 1 && !inner[0]->is_block && py::lower(inner[0]->key) == ptype) {
            const auto v = _num(inner[0]->value);
            return v ? std::optional<std::pair<QString, double>>({u"state"_s, *v}) : std::nullopt;
        }
    }
    return std::nullopt;
}

Caps read_caps(const moddata::Mod &mod, const std::function<QString(const QString &)> &text_of)
{
    // {pop type: {"province": share or None, "state": share or None, "lines": [(file, line, from type, scope, share,
    // factor)], "open": [types that can become it without such a line]}}. text_of(rel) gives a file's text as the
    // editor has it (else it is read from the mod / the game).
    const QString d = mod.p({u"poptypes"_s});
    QStringList files;
    try {
        for (const QString &f : fs::listdir(d))
            if (py::lower(f).endsWith(u".txt"))
                files << f;
    } catch (const fs::OSError &) {
        return {};
    }
    std::sort(files.begin(), files.end());
    OrderedMap<QString, QString> texts;
    for (const QString &f : files) {
        try {
            texts.insert(f, text_of ? text_of(u"poptypes/"_s + f) : script::read_text(pypath::join(d, f)));
        } catch (...) {
        }
    }
    // (Python keeps the result per mod folder while the files say the same — a cache only, the answer is the same)
    static QHash<QString, std::pair<OrderedMap<QString, QString>, Caps>> cache;
    if (const auto it = cache.constFind(mod.root); it != cache.cend() && it->first == texts)
        return it->second;
    QSet<QString> types;
    for (const QString &f : files)
        types.insert(py::lower(f.left(f.size() - 4)));
    struct Hit {
        double share, factor;
        QString rel;
        long long line;
    };
    // target -> {source: {scope: (share, factor, file, line)}}
    OrderedMap<QString, OrderedMap<QString, OrderedMap<QString, Hit>>> found;
    for (const QString &f : files) {
        const QString src = py::lower(f.left(f.size() - 4));
        const QString rel = u"poptypes/"_s + f;
        if (!texts.contains(f))
            continue;
        const QString text = *texts.get(f);
        const Nodes nodes = script::parse(text);
        for (const Node &top : nodes) {
            if (!top.is_block || low_key(top) != u"promote_to")
                continue;
            for (const Node &target : top.kids) {
                const QString t = low_key(target);
                if (!target.is_block || !types.contains(t))
                    continue;
                auto &per = found[t][src];
                std::vector<const Node *> mods;
                _modifiers(target.kids, mods);
                for (const Node *m : mods) {
                    std::optional<QString> fv;
                    for (const Node &c : m->kids)
                        if (low_key(c) == u"factor" && !c.is_block) {
                            fv = c.value;
                            break;
                        }
                    const auto fac = _num(fv);
                    if (!fac || *fac > SHUT)
                        continue;
                    const auto got = _share(*m, t);
                    if (!got)
                        continue;
                    const auto &[scope, share] = *got;
                    const Hit *old = per.get(scope);
                    if (!old || share < old->share)       // the lowest share at which it is off
                        per.insert(scope, Hit{share, *fac, rel, py::head(text, m->kstart).count(u'\n') + 1});
                }
            }
        }
    }
    Caps out;
    for (const auto &[t, by_src] : found) {
        Cap entry;
        std::vector<std::pair<QString, const OrderedMap<QString, Hit> *>> srcs;
        for (const auto &kv : by_src)
            srcs.emplace_back(kv.first, &kv.second);
        std::sort(srcs.begin(), srcs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[src, per] : srcs) {
            for (const auto &[scope, h] : *per)
                entry.lines.push_back(CapLine{h.rel, h.line, src, scope, h.share, h.factor});
            if (per->isEmpty())
                entry.open << src;
        }
        for (const QString &scope : {u"province"_s, u"state"_s}) {
            std::vector<double> shares;
            bool all = true;
            for (const auto &kv : by_src) {
                if (const Hit *hh = kv.second.get(scope))
                    shares.push_back(hh->share);
                else
                    all = false;
            }
            // a ceiling only where every type that can become it is shut off in this scope
            if (!shares.empty() && all)
                (scope == u"province" ? entry.province : entry.state) = *std::max_element(shares.begin(), shares.end());
        }
        if (!entry.lines.empty())
            out.insert(t, entry);
    }
    cache.insert(mod.root, {texts, out});
    return out;
}

std::vector<Over> over_cap(const moddata::Mod &mod, const document::Document &doc, const Caps &caps)
{
    // [(pop type, scope, cap, where, have, total)] — where: a province id / a state key — of everything that starts
    // above its ceiling (with the pops as the editor has them now), the furthest over first.
    std::vector<Over> out;
    OrderedMap<long long, std::pair<long long, OrderedMap<QString, long long>>> by_prov;
    for (const auto &[pid, pops] : doc.pops) {
        long long tot = 0;
        OrderedMap<QString, long long> per;
        for (const moddata::Pop &p : pops) {
            tot += p.size;
            per[p.type] += p.size;
        }
        if (tot > 0)
            by_prov.insert(pid, {tot, per});
    }
    for (const auto &[t, e] : caps) {
        if (e.province) {
            const double cap = *e.province;
            for (const auto &[pid, tp] : by_prov) {
                const long long have = tp.second.value(t, 0);
                if (double(have) > cap * double(tp.first) && have > 0)
                    out.push_back(Over{t, u"province"_s, cap, pid, have, tp.first});
            }
        }
        if (e.state) {
            const double cap = *e.state;
            for (const auto &[rid, r] : mod.regions) {
                long long tot = 0, have = 0;
                for (long long pid : r.provinces)
                    if (const auto *tp = by_prov.get(pid)) {
                        tot += tp->first;
                        have += tp->second.value(t, 0);
                    }
                if (tot > 0 && double(have) > cap * double(tot))
                    out.push_back(Over{t, u"state"_s, cap, rid, have, tot});
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Over &a, const Over &b) {
        return -(double(a.have) / double(a.total) - a.cap) < -(double(b.have) / double(b.total) - b.cap);
    });
    return out;
}

}  // namespace popcaps
