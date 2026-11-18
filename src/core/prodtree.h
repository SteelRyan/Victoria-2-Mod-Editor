// prodtree.py — The Production tab's Explorer (the whole chart is prodlayout's): one item in the middle — what it
// needs on the left, what uses it on the right (in columns by how), what boosts it on top, its efficiency goods at the
// bottom; every line a straight run or a plain fork, nothing crosses. A card's ＋ unfolds a tree of its own. A pop
// type's needs in the middle: an epicentre (the goods in a ring of sectors, what makes them further out). It gives
// what the chart draws: rects, line pieces, dots, arrows, texts.
#pragma once
#include "core/ordered_map.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <array>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace prodtree {

extern const double LANE, PAD, MIN_GAP, SIB_GAP, STUB, ROW_GAP, ALLEY, GUT;
extern const long long PER_COL;
using Rgb = std::tuple<int, int, int>;
extern const Rgb TITLE;

// a card on the chart (prodpanel's cid): (kind, key, the n-th copy)
struct Cid {
    QString kind, key;
    long long n = 0;
    bool operator==(const Cid &o) const { return n == o.n && kind == o.kind && key == o.key; }
};
inline size_t qHash(const Cid &c, size_t seed = 0) noexcept { return ::qHash(c.key, seed) ^ ::qHash(c.n, seed + 1); }
using OptCid = std::optional<Cid>;
using Rect = std::array<double, 4>;               // (x, y, w, h)
using Port = std::pair<Cid, QString>;             // (node, port)

struct Piece {
    QString kind;   // "h" / "v" / "d"
    double x1, y1, x2, y2;
    QString style;
    OptCid tgt;
};
using Direction = std::variant<QString, double>;  // "right" / "down" / "up", or an angle in radians
struct Arrow {
    double x, y;
    Direction dir;
    QString style;
    OptCid tgt;
};
struct Text {
    double x, y, w;
    QString text;
    Rgb rgb;
    long long px;
    QString align;
};
struct Net {
    QString id;
    std::vector<Port> src;
    std::vector<std::tuple<OptCid, QString, QString>> tgt;   // (target, port, style) — Python: (tgt, port, None, style)
    std::optional<QString> good;
    Rgb rgb;
    std::optional<QString> what;
};

class Scene {
public:
    OrderedMap<Cid, Rect> rects;
    OrderedMap<long long, std::vector<Piece>> pieces;
    OrderedMap<long long, std::vector<std::pair<double, double>>> dots, labels, marks;
    OrderedMap<long long, std::vector<Arrow>> arrows;
    std::vector<Text> texts;
    std::vector<std::array<double, 4>> guides;
    std::vector<std::tuple<double, double, double, double, Rgb>> wedges;
    std::vector<std::tuple<double, double, QString>> pills;
    std::vector<Net> nets;

    long long net(const QString &key, const std::optional<QString> &good, const Rgb &rgb, const std::optional<Port> &src,
                  const std::optional<QString> &what = std::nullopt);
    void h(long long ni, double x1, double y, double x2, const QString &style = QStringLiteral("trunk"), const OptCid &tgt = {});
    void v(long long ni, double x, double y1, double y2, const QString &style = QStringLiteral("trunk"), const OptCid &tgt = {});
    void d(long long ni, double x1, double y1, double x2, double y2, const QString &style = QStringLiteral("trunk"),
           const OptCid &tgt = {});
    void arrow(long long ni, double x, double y, const Direction &direction, const QString &style, const OptCid &tgt);
    void text(double x, double y, double w, const QString &t, const Rgb &rgb = TITLE, long long px = 18,
              const QString &align = QStringLiteral("l"));
    std::array<double, 4> bounds() const;

private:
    QHash<QString, long long> idx_;
};

struct Spec {
    double w = 0, h = 0;
    std::optional<double> out, bout, out_x;
};
using Specs = QHash<Cid, Spec>;
using Kids = QHash<Cid, std::vector<Cid>>;
struct Edge {
    Cid src;
    double sy;
    Cid tgt;
    double ty;
    Port fan;
    long long ni;
    QString style;
};
using Att = QHash<std::pair<Cid, Cid>, std::pair<double, double>>;

OrderedMap<Cid, double> tidy(const Cid &root, const Kids &kids, const QHash<Cid, double> &h, const Att &att);

struct Fan {
    long long ni = 0;
    OrderedMap<Cid, double> src;
    OrderedMap<std::pair<Cid, double>, std::pair<double, QString>> tgt;
    OrderedMap<Cid, std::pair<QString, OptCid>> sst;
    std::vector<double> ly, ry;
    double lo = 0, hi = 0, dd = 0;
    std::optional<long long> track;
    std::vector<std::tuple<double, double, QString, OptCid>> left;    // (x, y, style, target)
    std::vector<std::tuple<double, double, Cid, QString>> right;      // (x, y, node, style)
};
long long assign_tracks(std::vector<Fan> &fans);
double gap_width(long long n);
void draw_fans(Scene &sc, const std::vector<Fan> &fans, double x0, double x1, long long n);
OrderedMap<long long, std::vector<Fan>> _fans(const std::vector<Edge> &edges, const OrderedMap<Cid, double> &y,
                                              const OrderedMap<Cid, long long> &col);

struct Geo {
    OrderedMap<Cid, long long> col;
    OrderedMap<Cid, double> y;
    std::vector<double> colx, cw;
    OrderedMap<Cid, std::pair<double, double>> rel;
    std::array<double, 4> bbox;
    std::vector<Edge> edges;
};
Geo tree_geometry(const Specs &spec, const Cid &root, const Kids &down, const std::vector<Edge> &edges);
void place_tree(Scene &sc, const Specs &spec, const Geo &geo, double dx, double dy);

struct RowItem {     // top / bottom: what boosts the centre / its efficiency goods
    Cid node;
    long long ni;
    QString style;
    std::optional<QString> label;
};
struct BoxItem {
    Cid node;
    long long ni;
};
struct Unfolded {
    Kids down;
    std::vector<Edge> edges;
};
struct GroupItem {
    Cid node;
    QString entry;   // "in" / "top" / "bottom"
    std::optional<double> ty;
    QString style;
    std::optional<QString> label;
    std::optional<Unfolded> tree;
};
struct Group {
    QString title;
    long long ni;
    bool boost = false;
    std::vector<GroupItem> items;
};
struct Titles {
    QString left, top, bottom, empty_left;
};
void explorer_scene(Scene &sc, const Specs &spec, const Cid &centre, const Kids &up, const std::vector<Edge> &edges,
                    const std::vector<RowItem> &top, const std::vector<RowItem> &bottom,
                    const std::optional<BoxItem> &right_box, const std::vector<Group> &groups, const Titles &titles);

std::pair<double, double> _border(const Rect &r, double ux, double uy);
bool _clear(const std::vector<Rect> &rects, double pad = 34.0);
struct SectorItem {
    Cid node;
    long long ni;
    std::vector<std::pair<Cid, long long>> makers;
};
struct Sector {
    QString title;
    Rgb rgb;
    std::vector<SectorItem> items;
};
void hub_scene(Scene &sc, const Specs &spec, const std::vector<Cid> &cards, const std::vector<Sector> &sectors,
               const QString &name = {});

}  // namespace prodtree
