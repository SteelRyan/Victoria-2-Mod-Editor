// prodtree.py — the Production Explorer's layout and the pop needs epicentre. Line by line.
#include "core/prodtree.h"
#include "core/pynum.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace prodtree {

const double LANE = 24.0;        // px between two tracks side by side (never closer)
const double PAD = 46.0;         // a gap's free edges (the out arrows, the arrowheads)
const double MIN_GAP = 190.0;    // the least gap between two columns
const double SIB_GAP = 60.0;     // between two stacked subtrees
const double STUB = 28.0;        // top / bottom rows: the first turn this far from the card
const double ROW_GAP = 70.0;     // the Explorer's columns on the right: between two cards
const double ALLEY = 140.0;      // … the gap the street runs through
const double GUT = 130.0;        // … the gutter left of each column (its fork runs down it)
const long long PER_COL = 8;     // … cards per column (at most)
const Rgb TITLE = {214, 190, 130};

static const double PI = std::numbers::pi;

// Python's min() / max() over a sequence: the FIRST smallest / largest (this matters for -0.0 against 0.0)
static double py_min(const std::vector<double> &v)
{
    if (v.empty())
        throw std::invalid_argument("min() arg is an empty sequence");
    double m = v[0];
    for (double x : v)
        if (x < m)
            m = x;
    return m;
}
static double py_max(const std::vector<double> &v)
{
    if (v.empty())
        throw std::invalid_argument("max() arg is an empty sequence");
    double m = v[0];
    for (double x : v)
        if (x > m)
            m = x;
    return m;
}
static double pmax(double a, double b) { return b > a ? b : a; }   // max(a, b)
static double pmin(double a, double b) { return b < a ? b : a; }   // min(a, b)

// ------------------------------------------------------------------------------------------------ the scene
long long Scene::net(const QString &key, const std::optional<QString> &good, const Rgb &rgb, const std::optional<Port> &src,
                     const std::optional<QString> &what)
{
    // The index of net `key` (made on first use); src = (node, port) — where it starts.
    if (!idx_.contains(key)) {
        idx_.insert(key, static_cast<long long>(nets.size()));
        nets.push_back({key, {}, {}, good, rgb, what});
    }
    const long long ni = idx_.value(key);
    if (src && std::find(nets[ni].src.begin(), nets[ni].src.end(), *src) == nets[ni].src.end())
        nets[ni].src.push_back(*src);
    return ni;
}

void Scene::h(long long ni, double x1, double y, double x2, const QString &style, const OptCid &tgt)
{
    if (std::fabs(x2 - x1) > 0.1)
        pieces[ni].push_back({u"h"_s, x1, y, x2, y, style, tgt});
}

void Scene::v(long long ni, double x, double y1, double y2, const QString &style, const OptCid &tgt)
{
    if (std::fabs(y2 - y1) > 0.1)
        pieces[ni].push_back({u"v"_s, x, pmin(y1, y2), x, pmax(y1, y2), style, tgt});
}

void Scene::d(long long ni, double x1, double y1, double x2, double y2, const QString &style, const OptCid &tgt)
{
    // A straight line at any angle (the needs epicentre).
    pieces[ni].push_back({u"d"_s, x1, y1, x2, y2, style, tgt});
}

void Scene::arrow(long long ni, double x, double y, const Direction &direction, const QString &style, const OptCid &tgt)
{
    arrows[ni].push_back({x, y, direction, style, tgt});
    QString port = u"in"_s;
    if (const QString *s = std::get_if<QString>(&direction)) {
        if (*s == u"down")
            port = u"top"_s;
        else if (*s == u"up")
            port = u"bottom"_s;
    }
    const auto t = std::make_tuple(tgt, port, style);
    if (std::find(nets[ni].tgt.begin(), nets[ni].tgt.end(), t) == nets[ni].tgt.end())
        nets[ni].tgt.push_back(t);
}

void Scene::text(double x, double y, double w, const QString &t, const Rgb &rgb, long long px, const QString &align)
{
    texts.push_back({x, y, w, t, rgb, px, align});
}

std::array<double, 4> Scene::bounds() const
{
    if (rects.isEmpty())
        return {0.0, 0.0, 0.0, 0.0};
    std::vector<double> a, b, c, d;
    for (const auto &[_k, r] : rects) {
        a.push_back(r[0]);
        b.push_back(r[1]);
        c.push_back(r[0] + r[2]);
        d.push_back(r[1] + r[3]);
    }
    return {py_min(a), py_min(b), py_max(c), py_max(d)};
}

// ------------------------------------------------------------------------------------------------ building blocks
OrderedMap<Cid, double> tidy(const Cid &root, const Kids &kids, const QHash<Cid, double> &h, const Att &att)
{
    // Tops of the nodes of a tree with every subtree in its own band, stacked in order; a parent sits level with
    // (the mean of) where its lines meet its children. -> {node: top}, the root's top at 0.
    struct Placed {
        OrderedMap<Cid, double> pos;
        double top, bottom;
    };
    std::function<Placed(const Cid &)> place = [&](const Cid &n) -> Placed {
        const std::vector<Cid> ch = kids.value(n);
        if (ch.empty()) {
            Placed p;
            p.pos.insert(n, 0.0);
            p.top = 0.0;
            p.bottom = h.value(n);
            return p;
        }
        OrderedMap<Cid, double> pos;
        double cur = 0.0;
        for (const Cid &c : ch) {
            const Placed sub = place(c);
            const double s = cur - sub.top;
            for (const auto &[k, v] : sub.pos)
                pos.insert(k, v + s);
            cur = sub.bottom + s + SIB_GAP;
        }
        py::Sum want;
        for (const Cid &c : ch) {
            const auto a = att.value({n, c});
            want.add(pos.value(c) + a.second - a.first);
        }
        const double top = want.value() / static_cast<double>(ch.size());
        pos.insert(n, top);
        return {pos, pmin(top, 0.0), pmax(top + h.value(n), cur - SIB_GAP)};
    };
    const OrderedMap<Cid, double> pos = place(root).pos;
    const double r = pos.value(root);
    OrderedMap<Cid, double> out;
    for (const auto &[k, v] : pos)
        out.insert(k, v - r);
    return out;
}

long long assign_tracks(std::vector<Fan> &fans)
{
    // Fans between two columns each get a vertical track; overlapping ones side by side, the one reaching further
    // further out, so none crosses another. -> the number of tracks.
    std::vector<Fan *> vert;
    for (Fan &f : fans) {
        std::vector<double> ys = f.ly;
        ys.insert(ys.end(), f.ry.begin(), f.ry.end());
        f.lo = py_min(ys);
        f.hi = py_max(ys);
        f.dd = std::fabs(py::sum(f.ly) / static_cast<double>(f.ly.size()) - py::sum(f.ry) / static_cast<double>(f.ry.size()));
        f.track.reset();
        if (f.hi - f.lo > 0.5)
            vert.push_back(&f);
    }
    std::stable_sort(vert.begin(), vert.end(), [](const Fan *a, const Fan *b) {
        return a->dd != b->dd ? a->dd < b->dd : a->lo < b->lo;
    });
    std::vector<Fan *> placed;
    for (Fan *f : vert) {
        long long t = 0;
        for (const Fan *o : placed)
            if (!(o->hi < f->lo - 10 || f->hi < o->lo - 10))
                t = std::max(t, *o->track + 1);
        f->track = t;
        placed.push_back(f);
    }
    long long m = -1;
    for (const Fan *f : placed)
        m = std::max(m, *f->track);
    return m + 1;
}

double gap_width(long long n) { return pmax(MIN_GAP, 2 * PAD + static_cast<double>(std::max(0LL, n - 1)) * LANE); }

void draw_fans(Scene &sc, const std::vector<Fan> &fans, double x0, double x1, long long n)
{
    // The fans' pieces between x0 (the left column's edge) and x1: into the track, along it, out to the cards.
    const double room = x1 - x0 - 2 * PAD;
    const double step = n > 1 ? pmax(LANE, pmin(48.0, room / static_cast<double>(n - 1))) : 0.0;
    const double base = x0 + PAD + pmax(0.0, room - static_cast<double>(std::max(0LL, n - 1)) * step) / 2;
    for (const Fan &f : fans) {
        const long long ni = f.ni;
        const double tx = f.track ? base + static_cast<double>(*f.track) * step : (x0 + x1) / 2;
        std::vector<double> ends;
        for (const auto &[x, y, st, t] : f.left) {
            if (st == u"flowboost")                  // this one's line also carries a boost
                sc.h(ni, x, y, tx, u"flowboost"_s, t);
            else
                sc.h(ni, x, y, tx);
            ends.push_back(y);
        }
        for (const auto &[x, y, node, st] : f.right) {
            sc.h(ni, tx, y, x, st, node);
            sc.arrow(ni, x, y, u"right"_s, st, node);
            ends.push_back(y);
        }
        if (f.track) {
            sc.v(ni, tx, f.lo, f.hi);
            OrderedMap<double, long long> cnt;
            for (double y : ends)
                cnt[py::round_to(y, 1)] += 1;
            for (const auto &[y, k] : cnt)
                if ((f.lo + 0.5 < y && y < f.hi - 0.5) || k > 1)
                    sc.dots[ni].push_back({tx, y});
        }
    }
}

OrderedMap<long long, std::vector<Fan>> _fans(const std::vector<Edge> &edges, const OrderedMap<Cid, double> &y,
                                              const OrderedMap<Cid, long long> &col)
{
    // Edges grouped: gap (the source's column) -> [fan].
    OrderedMap<std::pair<long long, Port>, Fan> fans;
    for (const Edge &e : edges) {
        const std::pair<long long, Port> key{col.value(e.src), e.fan};
        if (!fans.contains(key)) {
            Fan f;
            f.ni = e.ni;
            fans.insert(key, f);
        }
        Fan &f = *fans.get(key);
        f.src.insert(e.src, y.value(e.src) + e.sy);
        f.sst.insert(e.src, {e.style, e.tgt});
        f.tgt.insert({e.tgt, e.ty}, {y.value(e.tgt) + e.ty, e.style});
    }
    OrderedMap<long long, std::vector<Fan>> per;
    for (auto &[key, f] : fans) {
        // a line that is also a boost: its own piece carries the stripe; the shared end into the slot only when
        // every line into it does
        const bool mixed = std::any_of(f.sst.begin(), f.sst.end(), [](const auto &kv) { return kv.second.first != u"flowboost"; });
        for (auto &[t, v] : f.tgt)
            if (v.second == u"flowboost" && mixed)
                v = {v.first, u"flow"_s};
        f.ly.clear();
        for (const auto &[_s, yy] : f.src)
            f.ly.push_back(yy);
        f.ry.clear();
        for (const auto &[_t, v] : f.tgt)
            f.ry.push_back(v.first);
        per[key.first].push_back(f);
    }
    return per;
}

static void _points(OrderedMap<long long, std::vector<Fan>> &per, const OrderedMap<Cid, Rect> &rects, const Specs &spec)
{
    for (auto &[_c, lst] : per)
        for (Fan &f : lst) {
            f.left.clear();
            for (const auto &[s, yy] : f.src) {
                const Rect r = rects.value(s);
                const auto *sst = f.sst.get(s);
                const QString st = sst ? sst->first : u"flow"_s;
                const OptCid t = sst ? sst->second : OptCid();
                f.left.emplace_back(r[0] + r[2] + spec.value(s).out_x.value_or(0.0), yy, st, t);
            }
            f.right.clear();
            for (const auto &[tk, v] : f.tgt)
                f.right.emplace_back(rects.value(tk.first)[0], v.first, tk.first, v.second);
        }
}

// ------------------------------------------------------------------------------------------------ an unfolded card
Geo tree_geometry(const Specs &spec, const Cid &root, const Kids &down, const std::vector<Edge> &edges)
{
    // A small tree growing right from root (an unfolded card): positions relative to the root's top left, the
    // columns, the gaps' tracks and the bounding box — placed later with place_tree.
    Geo g;
    g.col.insert(root, 0);
    std::vector<Cid> todo = {root};
    while (!todo.empty()) {
        const Cid n = todo.back();
        todo.pop_back();
        for (const Cid &c : down.value(n)) {
            g.col.insert(c, g.col.value(n) + 1);
            todo.push_back(c);
        }
    }
    QHash<Cid, double> h;
    for (const auto &[n, _c] : g.col)
        h.insert(n, spec.value(n).h);
    Att att;
    for (const Edge &e : edges)
        att.insert({e.src, e.tgt}, {e.sy, e.ty});
    g.y = tidy(root, down, h, att);
    auto per = _fans(edges, g.y, g.col);
    QHash<long long, long long> ntr;
    for (auto &[c, lst] : per)
        ntr.insert(c, assign_tracks(lst));
    long long ncol = 0;
    for (const auto &[_n, c] : g.col)
        ncol = std::max(ncol, c);
    ++ncol;
    for (long long c = 0; c < ncol; ++c) {
        std::vector<double> ws;
        for (const auto &[n, cc] : g.col)
            if (cc == c)
                ws.push_back(spec.value(n).w);
        g.cw.push_back(py_max(ws));
    }
    double x = 0.0;
    for (long long c = 0; c < ncol; ++c) {
        g.colx.push_back(x);
        x += g.cw[c] + gap_width(ntr.value(c, 0));
    }
    std::vector<double> ys, rights, bottoms;
    for (const auto &[n, c] : g.col)
        g.rel.insert(n, {g.colx[c], g.y.value(n)});
    for (const auto &[_n, v] : g.y)
        ys.push_back(v);
    for (const auto &[n, c] : g.col) {
        rights.push_back(g.colx[c] + spec.value(n).w);
        bottoms.push_back(g.y.value(n) + h.value(n));
    }
    g.bbox = {0.0, py_min(ys), py_max(rights) + 40.0, py_max(bottoms)};
    g.edges = edges;
    return g;
}

void place_tree(Scene &sc, const Specs &spec, const Geo &geo, double dx, double dy)
{
    for (const auto &[n, rr] : geo.rel)
        sc.rects.insert(n, {rr.first + dx, rr.second + dy, spec.value(n).w, spec.value(n).h});
    OrderedMap<Cid, double> y;
    for (const auto &[n, v] : geo.y)
        y.insert(n, v + dy);
    auto per = _fans(geo.edges, y, geo.col);
    for (auto &[c, lst] : per) {
        const long long n = assign_tracks(lst);
        OrderedMap<long long, std::vector<Fan>> one;
        one.insert(c, lst);
        _points(one, sc.rects, spec);
        draw_fans(sc, one.value(c), dx + geo.colx[c] + geo.cw[c], dx + geo.colx[c + 1], n);
    }
}

// ------------------------------------------------------------------------------------------------ the Explorer
namespace {
struct ItemState {
    const GroupItem *it;
    std::optional<Geo> geo;
    std::array<double, 3> cell{};   // (width, top, bottom)
};
struct Chunk {
    const Group *g;
    std::vector<ItemState> items;
    bool first;
    std::vector<long long> rows;
    double tx = 0, x = 0, w = 0;
    std::optional<double> top;
};
}  // namespace

void explorer_scene(Scene &sc, const Specs &spec, const Cid &centre, const Kids &up, const std::vector<Edge> &edges,
                    const std::vector<RowItem> &top, const std::vector<RowItem> &bottom,
                    const std::optional<BoxItem> &right_box, const std::vector<Group> &groups, const Titles &titles)
{
    const Spec cs = spec.value(centre);
    // --- what it needs (left): a tree, the centre its root
    Att att;
    for (const Edge &e : edges)
        att.insert({e.tgt, e.src}, {e.ty, e.sy});
    QHash<Cid, double> h;
    for (auto it = spec.cbegin(); it != spec.cend(); ++it)
        h.insert(it.key(), it.value().h);
    OrderedMap<Cid, double> y;
    if (!up.value(centre).empty())
        y = tidy(centre, up, h, att);
    else
        y.insert(centre, 0.0);
    OrderedMap<Cid, long long> col;
    col.insert(centre, 0);
    std::vector<Cid> todo = {centre};
    while (!todo.empty()) {
        const Cid n = todo.back();
        todo.pop_back();
        for (const Cid &c : up.value(n)) {
            col.insert(c, col.value(n) - 1);
            todo.push_back(c);
        }
    }
    auto per = _fans(edges, y, col);
    QHash<long long, long long> ntr;
    for (auto &[c, lst] : per)
        ntr.insert(c, assign_tracks(lst));
    std::set<long long> lset;
    for (const auto &[_n, c] : col)
        if (c < 0)
            lset.insert(c);
    const std::vector<long long> lcols(lset.begin(), lset.end());
    QHash<long long, double> cw;
    for (const auto &[n, c] : col)
        cw.insert(c, pmax(cw.value(c, 0.0), spec.value(n).w));
    // --- the centre column: the card, the row of what boosts it above, its efficiency goods below
    auto row_w = [&](const std::vector<RowItem> &lst) {
        py::Sum s;
        for (const RowItem &it : lst)
            s.add(spec.value(it.node).w);
        return s.value() + 70.0 * static_cast<double>(std::max<long long>(0, static_cast<long long>(lst.size()) - 1));
    };
    const double CW = pmax(pmax(cs.w, row_w(top)), row_w(bottom));
    double x = 0.0;
    QHash<long long, double> colx;
    for (long long c : lcols) {
        colx.insert(c, x);
        x += cw.value(c) + gap_width(ntr.value(c, 0));
    }
    colx.insert(0, x);
    const double cx0 = x;                            // the centre column's left
    const double card_x = cx0 + (CW - cs.w) / 2;
    sc.rects.insert(centre, {card_x, 0.0, cs.w, cs.h});
    for (const auto &[n, c] : col)
        if (c < 0)
            sc.rects.insert(n, {colx.value(c) + cw.value(c) - spec.value(n).w, y.value(n), spec.value(n).w, spec.value(n).h});
    _points(per, sc.rects, spec);
    for (const auto &[c, lst] : per)                 // (the tracks stay in the gap)
        draw_fans(sc, lst, colx.value(c) + cw.value(c), colx.value(c + 1), ntr.value(c));
    if (!lcols.empty()) {
        const double lx = colx.value(lcols[0]);
        std::vector<double> tops;
        for (const auto &[n, c] : col)
            if (c < 0)
                tops.push_back(sc.rects.value(n)[1]);
        sc.text(lx, py_min(tops) - 64, colx.value(0) - lx, titles.left);
    } else if (!titles.empty_left.isEmpty()) {
        sc.text(cx0 - 420, cs.h / 2 - 14, 380, titles.empty_left, {170, 160, 140}, 16);
    }
    // the rows above / below
    const Rect card = sc.rects.value(centre);
    for (const auto &[lst, side] : {std::pair<const std::vector<RowItem> &, QString>(top, u"top"_s),
                                    std::pair<const std::vector<RowItem> &, QString>(bottom, u"bottom"_s)}) {
        if (lst.empty())
            continue;
        const long long n = static_cast<long long>(lst.size());
        std::vector<double> ports, xs, cxs;
        for (long long i = 0; i < n; ++i)
            ports.push_back(card[0] + 34 + (card[2] - 68) * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
        double xx = cx0 + (CW - row_w(lst)) / 2;
        for (const RowItem &it : lst) {
            xs.push_back(xx);
            xx += spec.value(it.node).w + 70.0;
        }
        for (long long i = 0; i < n; ++i)
            cxs.push_back(xs[i] + spec.value(lst[i].node).w / 2);
        QHash<long long, long long> level;
        std::vector<long long> right, left;
        for (long long i = 0; i < n; ++i) {
            if (cxs[i] < ports[i] - 1)
                right.push_back(i);
            if (cxs[i] > ports[i] + 1)
                left.push_back(i);
        }
        std::stable_sort(right.begin(), right.end(), [&](long long a, long long b) { return cxs[a] < cxs[b]; });
        std::stable_sort(left.begin(), left.end(), [&](long long a, long long b) { return -cxs[a] < -cxs[b]; });
        for (size_t k = 0; k < right.size(); ++k)
            level.insert(right[k], static_cast<long long>(k));
        for (size_t k = 0; k < left.size(); ++k)
            level.insert(left[k], static_cast<long long>(k));
        long long K = -1;
        for (auto it = level.cbegin(); it != level.cend(); ++it)
            K = std::max(K, it.value());
        ++K;
        const double edge = side == u"top" ? card[1] : card[1] + card[3];
        const int sgn = side == u"top" ? -1 : 1;
        const double row_edge = edge + sgn * (STUB + static_cast<double>(K) * LANE + 34);   // the row's edge facing the card
        for (long long i = 0; i < n; ++i) {
            const RowItem &it = lst[i];
            const Cid &nd = it.node;
            const double w = spec.value(nd).w, hh = spec.value(nd).h;
            const double ry = side == u"top" ? row_edge - hh : row_edge;
            sc.rects.insert(nd, {xs[i], ry, w, hh});
            const long long ni = it.ni;
            const QString &st = it.style;
            if (level.contains(i)) {
                const double ly = edge + sgn * (STUB + static_cast<double>(level.value(i)) * LANE);
                sc.v(ni, cxs[i], row_edge, ly);
                sc.h(ni, cxs[i], ly, ports[i], st, centre);
                sc.v(ni, ports[i], ly, edge, st, centre);
            } else {
                sc.v(ni, cxs[i], row_edge, edge, st, centre);
            }
            sc.arrow(ni, ports[i], edge, side == u"top" ? u"down"_s : u"up"_s, st, centre);
            if (it.label && !it.label->isEmpty())
                sc.pills.emplace_back(cxs[i],
                                      (row_edge + (level.contains(i) ? edge + sgn * (STUB + static_cast<double>(level.value(i)) * LANE)
                                                                     : edge)) / 2,
                                      *it.label);
        }
        std::vector<double> hs;
        for (const RowItem &it : lst)
            hs.push_back(spec.value(it.node).h);
        const QString t = side == u"top" ? titles.top : titles.bottom;
        if (side == u"top")
            sc.text(cx0, row_edge - py_max(hs) - 40, CW, t);
        else
            sc.text(cx0, row_edge + py_max(hs) + 14, CW, t);
    }
    // --- what uses it (right): its output good's box, then columns along a street
    const double y_s = card[1] + cs.out.value_or(cs.h / 2);
    x = cx0 + CW;
    const double x_b = x + 60.0;                     // the boost line's way up
    double src_x = card[0] + card[2] + cs.out_x.value_or(0.0);
    if (right_box) {
        const double bx = x + 190.0;
        const Cid &b = right_box->node;
        sc.rects.insert(b, {bx, y_s - spec.value(b).h / 2, spec.value(b).w, spec.value(b).h});
        sc.h(right_box->ni, src_x, y_s, bx, u"flow"_s, b);
        sc.arrow(right_box->ni, bx, y_s, u"right"_s, u"flow"_s, b);
        x = bx + spec.value(b).w;
        src_x = x;
    }
    std::vector<Chunk> chunks;
    for (const Group &g : groups) {                  // columns of up to PER_COL, evened out (22 -> 8, 7, 7)
        const long long n = static_cast<long long>(g.items.size());
        const long long per_ = n ? -py::floordiv(-n, -py::floordiv(-n, PER_COL)) : 1;
        for (long long i = 0; i < n; i += per_) {
            Chunk ch{&g, {}, i == 0, {}};
            for (long long j = i; j < std::min(n, i + per_); ++j)
                ch.items.push_back({&g.items[j], std::nullopt, {}});
            chunks.push_back(ch);
        }
    }
    if (chunks.empty())
        return;
    QHash<long long, double> hrow;
    for (Chunk &ch : chunks) {
        const long long n = static_cast<long long>(ch.items.size());
        const long long ka = py::floordiv(n, 2LL);
        for (long long r = -ka; r < n - ka; ++r)
            ch.rows.push_back(r);
        for (size_t k = 0; k < ch.items.size(); ++k) {
            ItemState &is = ch.items[k];
            const long long r = ch.rows[k];
            const Cid &nd = is.it->node;
            if (is.it->tree) {                       // unfolded: its own tree to its right, in its own cell
                is.geo = tree_geometry(spec, nd, is.it->tree->down, is.it->tree->edges);
                const auto &bx = is.geo->bbox;
                is.cell = {bx[2], bx[1], bx[3]};
            } else {
                is.cell = {spec.value(nd).w, 0.0, spec.value(nd).h};
            }
            hrow.insert(r, pmax(hrow.value(r, 0.0), is.cell[2] - is.cell[1]));
        }
    }
    QHash<long long, double> rtop;
    {
        double t = y_s + ALLEY / 2;
        long long r = 0;
        while (hrow.contains(r)) {
            rtop.insert(r, t);
            t += hrow.value(r) + ROW_GAP;
            ++r;
        }
        double b = y_s - ALLEY / 2;
        r = -1;
        while (hrow.contains(r)) {
            rtop.insert(r, b - hrow.value(r));
            b = rtop.value(r) - ROW_GAP;
            --r;
        }
    }
    x += 60.0;
    for (Chunk &ch : chunks) {
        x += GUT;
        ch.tx = x - GUT / 2;
        ch.x = x;
        std::vector<double> ws;
        for (const ItemState &is : ch.items)
            ws.push_back(is.cell[0]);
        const double w = py_max(ws);
        ch.w = w;
        ch.top.reset();
        for (size_t k = 0; k < ch.items.size(); ++k) {
            const ItemState &is = ch.items[k];
            const long long r = ch.rows[k];
            const Cid &nd = is.it->node;
            const double cell_h = is.cell[2] - is.cell[1];
            const double ctop = r < 0 ? rtop.value(r) + hrow.value(r) - cell_h : rtop.value(r);
            const double yy = ctop - is.cell[1];
            sc.rects.insert(nd, {x, yy, spec.value(nd).w, spec.value(nd).h});
            if (is.geo)
                place_tree(sc, spec, *is.geo, x, yy);
            ch.top = ch.top ? pmin(*ch.top, ctop) : ctop;
        }
        x += w + 40.0;
    }
    std::vector<double> tops;
    for (const Chunk &ch : chunks)
        tops.push_back(*ch.top);
    const double y_top = py_min(tops) - 150.0;       // the street over the top (boosts of its building)
    std::optional<size_t> last_main, first_boost, last_boost;
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!chunks[i].g->boost)
            last_main = i;
        else {
            if (!first_boost)
                first_boost = i;
            last_boost = i;
        }
    }
    if (last_main) {
        const long long main_ni = chunks[*last_main].g->ni;
        sc.h(main_ni, src_x, y_s, chunks[*last_main].tx);
        const auto &good = sc.nets[main_ni].good;
        if (!right_box && good && !good->isEmpty() && centre.kind != u"good")   // (a good's own box names it)
            sc.labels[main_ni].push_back({src_x + 70.0, y_s});
    }
    if (first_boost) {
        const long long ni = chunks[*first_boost].g->ni;
        const double by = card[1] + cs.bout.value_or(14.0);
        sc.h(ni, card[0] + card[2], by, x_b);
        sc.v(ni, x_b, by, y_top);
        std::vector<double> txs;
        for (const Chunk &ch : chunks)
            if (ch.g->boost)
                txs.push_back(ch.tx);
        sc.h(ni, x_b, y_top, py_max(txs));
    }
    for (size_t i = 0; i < chunks.size(); ++i) {
        const Chunk &ch = chunks[i];
        const Group &g = *ch.g;
        const long long ni = g.ni;
        const double tx = ch.tx;
        std::vector<double> ends = {g.boost ? y_top : y_s};
        for (const ItemState &is : ch.items) {
            const GroupItem &it = *is.it;
            const Cid &nd = it.node;
            const QString &st = it.style;
            const Rect r = sc.rects.value(nd);
            double yy;
            if (it.entry == u"in") {
                yy = r[1] + it.ty.value_or(r[3] / 2);
                sc.h(ni, tx, yy, r[0], st, nd);
                sc.arrow(ni, r[0], yy, u"right"_s, st, nd);
            } else {
                const bool t = it.entry == u"top";
                yy = t ? r[1] - 24 : r[1] + r[3] + 24;
                const double px = r[0] + 42;
                sc.h(ni, tx, yy, px, st, nd);
                sc.v(ni, px, yy, t ? r[1] : r[1] + r[3], st, nd);
                sc.arrow(ni, px, t ? r[1] : r[1] + r[3], t ? u"down"_s : u"up"_s, st, nd);
            }
            if (it.label && !it.label->isEmpty())
                sc.pills.emplace_back((tx + (it.entry != u"in" ? r[0] + 42 : r[0])) / 2, yy, *it.label);
            ends.push_back(yy);
        }
        const double lo = py_min(ends), hi = py_max(ends);
        sc.v(ni, tx, lo, hi);
        for (double yy : ends)
            if (lo + 0.5 < yy && yy < hi - 0.5)
                sc.dots[ni].push_back({tx, yy});
        const double street = g.boost ? y_top : y_s;
        if ((g.boost && i != *last_boost) || (!g.boost && i != *last_main))
            sc.dots[ni].push_back({tx, street});
        const QString title = ch.first ? u"%1 — %2"_s.arg(g.title, QString::number(g.items.size())) : u"%1 (more)"_s.arg(g.title);
        std::vector<double> ws;
        for (const ItemState &is : ch.items)
            ws.push_back(spec.value(is.it->node).w);
        sc.text(ch.x, *ch.top - 70, py_max(ws) + 40, title);
    }
}

// ------------------------------------------------------------------------------------------------ the needs epicentre
std::pair<double, double> _border(const Rect &r, double ux, double uy)
{
    // Where the ray from rect r's centre in direction (ux, uy) leaves it.
    const double cx = r[0] + r[2] / 2, cy = r[1] + r[3] / 2;
    const double t = pmin(std::fabs(ux) > 1e-9 ? (r[2] / 2) / std::fabs(ux) : 1e9,
                          std::fabs(uy) > 1e-9 ? (r[3] / 2) / std::fabs(uy) : 1e9);
    return {cx + ux * t, cy + uy * t};
}

bool _clear(const std::vector<Rect> &rects, double pad)
{
    for (size_t i = 0; i < rects.size(); ++i) {
        const Rect &a = rects[i];
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const Rect &b = rects[j];
            if (a[0] - pad < b[0] + b[2] && b[0] - pad < a[0] + a[2] && a[1] - pad < b[1] + b[3] && b[1] - pad < a[1] + a[3])
                return false;
        }
    }
    return true;
}

void hub_scene(Scene &sc, const Specs &spec, const std::vector<Cid> &cards, const std::vector<Sector> &sectors,
               const QString &name)
{
    // A pop type's needs: a card per kind of need close together in the middle, each facing its own sector of the
    // ring; the goods of that kind around it in that sector (what makes each further out on the same bearing).
    std::vector<std::pair<long long, const SectorItem *>> items;
    for (size_t si = 0; si < sectors.size(); ++si)
        for (const SectorItem &it : sectors[si].items)
            items.emplace_back(static_cast<long long>(si), &it);
    const long long k = static_cast<long long>(cards.size());
    std::vector<double> weights;
    for (const auto &[_si, it] : items)
        weights.push_back(pmax(1.0, static_cast<double>(it->makers.size())));
    if (weights.empty())
        weights = {1.0};
    const double GAP = 1.6;
    const double unit = 2 * PI / (py::sum(weights) + GAP * static_cast<double>(std::max<long long>(1, static_cast<long long>(sectors.size()))));
    double a = 0.0;
    std::optional<long long> prev;
    std::vector<std::pair<double, double>> spans;
    OrderedMap<long long, std::vector<double>> sec;
    std::vector<double> bounds;
    for (size_t i = 0; i < items.size(); ++i) {
        const long long si = items[i].first;
        const double wgt = weights[i];
        if (!prev || si != *prev) {
            if (prev)
                bounds.push_back(a + GAP * unit / 2);
            a += GAP * unit;
        }
        spans.emplace_back(a, a + wgt * unit);
        sec[si].push_back(a);
        sec[si].push_back(a + wgt * unit);
        a += wgt * unit;
        prev = si;
    }
    bounds.push_back(a + GAP * unit / 2);
    OrderedMap<long long, double> mids;
    for (const auto &[si, vv] : sec)
        mids.insert(si, (py_min(vv) + py_max(vv)) / 2);
    const double rot = mids.contains(0) ? (-PI * 5 / 6 - mids.value(0)) : 0.0;   // the first (life) upper left
    for (auto &[s0, s1] : spans) {
        s0 += rot;
        s1 += rot;
    }
    for (double &b : bounds)
        b += rot;
    for (auto &[_si, v] : mids)
        v += rot;
    // the section cards: out from the middle along their sector's bearing, as close as they fit
    std::vector<double> sizes;
    for (const Cid &c : cards)
        sizes.push_back(pmax(spec.value(c).w, spec.value(c).h));
    const double size = py_max(sizes);
    auto card_rects = [&](double d) {
        std::vector<Rect> out;
        for (long long si = 0; si < k; ++si) {
            const Cid &c = cards[si];
            const double ang = mids.contains(si) ? mids.value(si) : -PI / 2 + 2 * PI * static_cast<double>(si) / static_cast<double>(k);
            const double w = spec.value(c).w, h = spec.value(c).h;
            out.push_back({d * std::cos(ang) - w / 2, d * std::sin(ang) - h / 2, w, h});
        }
        return out;
    };
    double d = k == 1 ? 0.0 : size * 0.45;
    while (k > 1 && !_clear(card_rects(d), 60.0))
        d += 20.0;
    const std::vector<Rect> crects = card_rects(d);
    for (size_t i = 0; i < cards.size(); ++i)
        sc.rects.insert(cards[i], crects[i]);
    std::vector<double> inners;
    for (const Rect &r : crects)
        inners.push_back(py::hypot(r[0] + (r[0] + r[2] / 2 > 0 ? r[2] : 0), r[1] + (r[1] + r[3] / 2 > 0 ? r[3] : 0)));
    const double inner = py_max(inners);
    if (!name.isEmpty())
        sc.text(-160, -16, 320, name, {236, 222, 190}, 30, u"c"_s);
    if (items.empty())
        return;

    auto rect_at = [&](const Cid &n, double ang, double R) -> Rect {
        const double w = spec.value(n).w, h = spec.value(n).h;
        return {R * std::cos(ang) - w / 2, R * std::sin(ang) - h / 2, w, h};
    };
    double R1 = inner + 260.0;
    std::vector<Rect> ring;
    while (true) {
        ring.clear();
        for (size_t i = 0; i < items.size(); ++i)
            ring.push_back(rect_at(items[i].second->node, (spans[i].first + spans[i].second) / 2, R1));
        std::vector<Rect> both = ring;
        both.insert(both.end(), crects.begin(), crects.end());
        if (_clear(both, 44.0))
            break;
        R1 *= 1.06;
    }
    struct Outer {
        Cid m;
        long long mni;
        double ang;
        const SectorItem *it;
    };
    std::vector<Outer> outer;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &[s0, s1] = spans[i];
        const SectorItem *it = items[i].second;
        for (size_t j = 0; j < it->makers.size(); ++j)
            outer.push_back({it->makers[j].first, it->makers[j].second,
                             s0 + (s1 - s0) * (static_cast<double>(j) + 0.5) / static_cast<double>(it->makers.size()), it});
    }
    double R2 = R1 + 380.0;
    while (!outer.empty()) {
        std::vector<Rect> orects;
        for (const Outer &o : outer)
            orects.push_back(rect_at(o.m, o.ang, R2));
        orects.insert(orects.end(), ring.begin(), ring.end());
        if (_clear(orects))
            break;
        R2 *= 1.05;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const long long si = items[i].first;
        const SectorItem *it = items[i].second;
        const Rect r = ring[i];
        sc.rects.insert(it->node, r);
        const Rect cr = si < static_cast<long long>(crects.size()) ? crects[si] : crects[0];
        const double cx = r[0] + r[2] / 2, cy = r[1] + r[3] / 2;
        const double kx = cr[0] + cr[2] / 2, ky = cr[1] + cr[3] / 2;
        double L = py::hypot(kx - cx, ky - cy);
        if (L == 0.0)
            L = 1.0;
        const double ux = (kx - cx) / L, uy = (ky - cy) / L;
        const auto [x1, y1] = _border(r, ux, uy);
        const auto [x2, y2] = _border(cr, -ux, -uy);
        const Cid tgt = si < static_cast<long long>(cards.size()) ? cards[si] : cards[0];
        sc.d(it->ni, x1, y1, x2, y2, u"flow"_s, tgt);
        sc.arrow(it->ni, x2, y2, std::atan2(uy, ux), u"flow"_s, tgt);
    }
    for (const Outer &o : outer) {
        const Rect r = rect_at(o.m, o.ang, R2);
        sc.rects.insert(o.m, r);
        const Rect g = sc.rects.value(o.it->node);
        const double gx = g[0] + g[2] / 2, gy = g[1] + g[3] / 2;
        const double mx = r[0] + r[2] / 2, my = r[1] + r[3] / 2;
        double L = py::hypot(gx - mx, gy - my);
        if (L == 0.0)
            L = 1.0;
        const double ux = (gx - mx) / L, uy = (gy - my) / L;
        const auto [x1, y1] = _border(r, ux, uy);
        const auto [x2, y2] = _border(g, -ux, -uy);
        sc.d(o.mni, x1, y1, x2, y2, u"flow"_s, o.it->node);
        sc.arrow(o.mni, x2, y2, std::atan2(uy, ux), u"flow"_s, o.it->node);
    }
    std::vector<double> bigs;
    for (const auto &[_si, it] : items)
        bigs.push_back(pmax(spec.value(it->node).w, spec.value(it->node).h));
    const double big = py_max(bigs);
    const double r_in = pmax(inner + 60.0, R1 - big / 2 - 70.0);
    const double r_out = !outer.empty() ? (R2 + 190.0) : (R1 + big / 2 + 90.0);
    if (sectors.size() > 1) {
        std::vector<double> starts = {bounds.back() - 2 * PI};
        starts.insert(starts.end(), bounds.begin(), bounds.end() - 1);
        for (size_t si = 0; si < sectors.size(); ++si)
            sc.wedges.emplace_back(starts.at(si), bounds.at(si), r_in, r_out, sectors[si].rgb);
        for (double b : bounds)
            sc.guides.push_back({std::cos(b) * r_in, std::sin(b) * r_in, std::cos(b) * r_out, std::sin(b) * r_out});
    }
    for (size_t si = 0; si < sectors.size(); ++si)
        if (mids.contains(static_cast<long long>(si))) {
            const double ang = mids.value(static_cast<long long>(si));
            const double R = r_out + 50.0;
            sc.text(R * std::cos(ang) - 200, R * std::sin(ang) - 18, 400, sectors[si].title, sectors[si].rgb, 30, u"c"_s);
        }
}

}  // namespace prodtree
