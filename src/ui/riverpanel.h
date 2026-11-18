// riverpanel.py — Map → Draw → Rivers: paint map/rivers.bmp and let the editor lay out whole rivers.
//
// rivers.bmp conventions (vanilla and Ferrum_Mare): rivers are 1-pixel lines of 4-connected pixels on land (255 = no
// river). Index 0 (green) is the source, 1 (red) the last pixel of a river that flows into another river, 2–11 the
// width from thin (2) to wide (11). Ferrum_Mare uses source → one pixel of 2 → width 4 to the mouth; vanilla widens
// its rivers downstream (2 → 4 → 6 → 7 …). Rivers end at the sea or with a red pixel next to another river.
//
// Tools: "River system" (a guide: place the source, the mouth, points the river must pass and branches one after
// another, then generate the whole system; "Regenerate" re-rolls it), "Select" (click a river, Shift+click a whole
// network — regenerate or delete it), a 1-pixel pen and an eraser. Every change is one undo step; rivers.bmp is
// written on Apply (with a backup).
// (Python's plan dicts are Branch; its point tags ("rplan", kind, i[, j]) are Tag; Generator's window arrays are
// Grid<uint8> masks in window coordinates.)
#pragma once
#include "core/ordered_map.h"
#include "core/store.h"
#include "map/layerbmp.h"
#include "ui/drawlink.h"
#include "ui/mapview.h"

#include <QVariant>
#include <QWidget>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace document {
class Document;
}
namespace grid {
class Generator;
}
namespace moddata {
class Mod;
}
namespace widgets {
class Section;
class SliderField;
}

namespace riverpanel {

using Rgb = std::tuple<int, int, int>;
using Mask = grid::Grid<std::uint8_t>;
using Pt = std::pair<double, double>;          // a map point (x, y)
using YX = std::pair<long long, long long>;    // a window pixel (y, x)
using Flat = std::vector<std::int64_t>;

constexpr int SOURCE = 0, MERGE = 1, LAND = 255;
extern const std::vector<int> WIDTHS;
extern const Rgb LAND_RGB, HALO_RGB, PROBLEM_RGB, SELECT_RGB;
extern const std::vector<std::pair<QVariant, QString>> WIDTH_STYLES;
std::array<std::array<std::uint8_t, 3>, 256> display_colors();

class RiverBox;

// The map shows land / sea with the rivers of rivers.bmp drawn on top (with a dark halo so thin rivers stay visible
// when zoomed out). Pixels that break a rule are red; selected rivers yellow.
class RiverView : public mapview::PixelLayer {
public:
    RiverView(const mapview::MapData &md, const layerbmp::IndexedBitmap &rb, RiverBox *box = nullptr);
    const mapview::MapData &md;
    const layerbmp::IndexedBitmap &tb;
    RiverBox *box;
    std::array<std::array<std::uint8_t, 3>, 256> disp;
    bool pure_pixels() const override { return true; }
    mapview::RgbImage colors(long long y0, long long y1, long long x0, long long x1) override;
    std::optional<std::vector<Top>> on_top(long long y0, long long y1, long long x0, long long x1) override;
};

// ---------------------------------------------------------------------------------------------- the plan
struct Branch {
    Pt at;
    long long length = 40;
    std::optional<Pt> source;
    std::optional<long long> parent;            // index of the branch it joins; None = the main river
    bool await_ = false;                        // ("await": the guide waits for its source click)
    std::optional<std::vector<Pt>> via;         // (the key exists only for branches made with ＋ Branch)
    bool operator==(const Branch &) const = default;
};

// The river being laid out (kind "river_plan" in doc.maps, part of undo so points and pixels stay in step). Never
// saved itself — its pixels are in rivers.bmp.
class RiverPlan : public store::Store {
public:
    RiverPlan() { clear(); }
    std::optional<Pt> start, end;
    std::vector<Pt> via;                        // forced points, in order
    std::vector<Branch> branches;
    long long seed = 1;
    OrderedMap<long long, std::pair<int, int>> written;   // flat index -> (value before the plan, value written)
    QString info;
    std::optional<QString> stage;               // guide: "source" → "mouth" → "via" → "branches" → "done"
    std::optional<QString> mouth;               // "sea" / "river" / None = decide from where the mouth is
    std::map<long long, Flat> paths;            // generated pixels per river: -1 = main river, i = branch i
    void clear();
    bool active() const;
    struct State {
        std::optional<Pt> start, end;
        std::vector<Pt> via;
        std::vector<Branch> branches;
        long long seed;
        std::vector<std::pair<long long, std::pair<int, int>>> written;   // (sorted, as Python's snapshot)
        QString info;
        std::optional<QString> stage, mouth;
        std::map<long long, Flat> paths;
        bool operator==(const State &) const = default;
    };
    store::Snap snapshot() const override;
    void restore(const store::Snap &s) override;
    QStringList dirty() const override { return {}; }
    std::optional<QStringList> save(const QString &) override { return std::nullopt; }
    bool undo_after_apply() const override { return true; }
};

// ---------------------------------------------------------------------------------------------- routing
Mask _dilate(const Mask &m, bool eight = false);
// Cheapest 4-connected path from start (y, x) to any goal pixel, over passable pixels; cost = the cost of entering a
// pixel (≥ 1). A* towards `target` (y, x) with `radius` slack.
std::optional<std::vector<YX>> route(const Mask &passable, const std::vector<double> &cost, YX start, const Mask &goals,
                                     std::optional<YX> target = std::nullopt, double radius = 0);

// Lays out a river (and its branches) for a plan inside a window of the map.
class Generator {
public:
    Generator(const moddata::Mod &mod, const mapview::MapData &md, const layerbmp::IndexedBitmap &rb,
              const RiverPlan &plan, double meander, const QVariant &width_style)
        : mod(mod), md(md), rb(rb), plan(plan), meander(meander), width_style(width_style) {}
    const moddata::Mod &mod;
    const mapview::MapData &md;
    const layerbmp::IndexedBitmap &rb;
    const RiverPlan &plan;
    double meander;
    QVariant width_style;
    const layerbmp::IndexedBitmap *rb_terrain = nullptr;
    std::optional<QString> error;
    bool _bends = true;
    std::array<long long, 4> box{};             // (x0, y0, x1, y1)
    grid::Grid<std::uint8_t> R;                // rivers.bmp in the window, the plan's own pixels as before it
    Mask riv, base_ok_basic, base_ok, coast;
    std::vector<float> cost;
    std::vector<YX> main;
    std::vector<std::optional<std::vector<YX>>> bpaths;
    std::vector<std::optional<long long>> branch_info;
    long long length = 0;
    QString mouth;

    std::array<long long, 4> _window(double scale = 1.0) const;
    std::optional<OrderedMap<long long, int>> run();
    bool _same_land();
    std::optional<OrderedMap<long long, int>> _run_once(double scale);
    std::vector<YX> _roughen(std::vector<YX> path, const Mask &allowed, grid::Generator &rng, bool keep_end = true);
    static bool _valid_run(const std::vector<YX> &path, long long s, long long e, const Mask &allowed);
    std::pair<std::vector<YX>, std::vector<bool>> _meanders(const std::vector<YX> &ctrl, const Mask &ok,
                                                            grid::Generator &rng);
    std::vector<YX> _subdivide(YX a, YX b, const Mask &ok, grid::Generator &rng, int depth);
    static std::optional<YX> _snap_w(long long y, long long x, const Mask &ok, long long r);
    std::optional<std::vector<YX>> _chain(const std::vector<YX> &pts, const std::vector<bool> &forced, const Mask &goals,
                                          YX target, double radius, const Mask &base_ok, bool merge,
                                          const Mask *near_ok = nullptr);
    std::optional<YX> _snap(const Pt &pt, const Mask &ok, long long r = 6) const;
    struct Mouth {
        Mask goals;
        YX target;
        double radius;
        bool merge;
    };
    std::optional<Mouth> _mouth();
    std::optional<std::vector<YX>> _branch(const Branch &b, long long bi, const std::vector<YX> &path,
                                           const Mask &own_all);
    std::vector<int> _widths(long long n, bool merge, bool branch = false) const;
};

// ---------------------------------------------------------------------------------------------- tracing
struct Traced {
    std::vector<YX> path;                       // source first
    QString end;                                // "river" / "sea" / "land"
    std::optional<YX> end_at;                   // the joined pixel ("river")
    std::optional<long long> parent;
};
std::set<YX> trace_network(const grid::Grid<std::uint8_t> &R, long long y, long long x, size_t limit = 200000);
std::pair<std::vector<Traced>, std::map<YX, long long>> split_branches(const grid::Grid<std::uint8_t> &R,
                                                                       const std::set<YX> &comp,
                                                                       const std::function<bool(YX)> &is_sea);
std::vector<long long> subtree(const std::vector<Traced> &branches, long long root);

// ---------------------------------------------------------------------------------------------- panel
struct Step {
    QString key, title, hint;
};
extern const std::vector<Step> STEPS;
extern const QStringList GUIDE;

// a point of the river system: ("rplan", kind, i[, j])
struct Tag {
    QString kind;
    long long i = 0;
    std::optional<long long> j;
    bool operator==(const Tag &) const = default;
    QVariantList list() const;
    static std::optional<Tag> of(const std::optional<QVariantList> &hit);   // None unless an "rplan" tag
};

class RiverHandler : public mapview::MouseHandler {
public:
    explicit RiverHandler(RiverBox *box) : b(box) {}
    RiverBox *b;
    std::optional<OrderedMap<long long, long long>> touched;
    std::optional<Pt> last, press_pt, last_pos;
    std::optional<Tag> drag;
    mapview::MapView *view() const;
    bool cancel();
    void _pen_to(double x, double y);
    void _fix_at(double x, double y);
    void _erase_at(double x, double y);
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void preview();
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool has_wheel() const override { return true; }
    bool wheel(double steps, Qt::KeyboardModifiers mods) override;
};

// The selection of the Select tool (Python: box.sel)
struct Selection {
    std::vector<Traced> branches;
    long long root = 0;
    std::vector<long long> members;
    bool network = false;
};

class RiverBox : public QWidget {
    Q_OBJECT
public:
    RiverBox(moddata::Mod &mod, const mapview::MapData &md, drawlink::Panel *panel, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const mapview::MapData &md;
    drawlink::Panel *panel;
    document::Document *doc = nullptr;
    int value = 4;
    std::vector<std::int64_t> counts = std::vector<std::int64_t>(256, 0);
    std::optional<Flat> selected;               // flat indexes of the rivers picked with Select (drawn yellow)
    std::optional<Selection> sel;               // the traced selection
    std::optional<Tag> cur_node;                // selected point of the river system (its river is highlighted)
    std::optional<QString> armed;               // "point" / "branch" / "bsrc": what the next map click adds
    RiverHandler handler{this};
    static std::function<long long()> random_seed;   // regenerate_selected's unseeded default_rng (a check sets it)

    std::map<QString, QPushButton *> tool_btns;
    QStringList tool_keys;
    widgets::Section *sys_section, *sel_section, *pen_section;
    QPushButton *new_btn, *back_btn, *next_btn, *auto_btn, *cancel_btn, *gen_btn, *regen_btn, *edit_btn, *done_btn,
        *del_btn, *prev_node, *next_node, *addpt_btn, *addbr_btn, *rmnode_btn, *auto2_btn, *sel_regen, *sel_del,
        *sel_clear;
    QWidget *steps_host, *done_host, *cur_host, *scope_row, *size_row;
    std::map<QString, QLabel *> step_lbls;
    QLabel *cur_info, *plan_info, *plan_help, *sel_info, *val_info, *status;
    widgets::SliderField *meander, *blen, *size_spin;
    QComboBox *width, *pen_scope;
    std::map<int, QPushButton *> sw;

    layerbmp::IndexedBitmap &rb() const;
    RiverPlan &plan() const;
    QString tool() const;
    void set_value(int v);
    void _tool_changed();
    void refresh(document::Document *doc);
    QString banner() const;
    void _update_ui();
    void _plan_edit(const QString &label_text, const std::function<void(RiverPlan &)> &fn, bool regenerate = false,
                    bool new_seed = false);
    void start_guide();
    void click(double x, double y);
    void next_step();
    void auto_source();
    void back();
    void cancel_guide();
    void generate_now();
    void edit_points();
    QString _generate();
    void regenerate(bool new_seed = true, bool quiet = false);
    void add_via(double x, double y);
    void move_point(const Tag &tag, double x, double y);
    void remove_point(const Tag &tag);
    std::optional<long long> cur_branch() const;
    std::vector<Tag> nodes() const;
    std::optional<Pt> node_pos(const Tag &tag) const;
    void select_node(const Tag &tag, bool center = false);
    void step_node(int d);
    void arm(const QString &what);
    void auto_source_armed();
    void add_point(std::optional<long long> cb, double x, double y);
    void remove_node();
    void _branch_len(double v);
    void _update_highlight();
    void _set_highlight(const std::optional<Flat> &idx);
    void done();
    void delete_plan();
    bool _is_sea(YX q) const;
    void select_at(double x, double y, bool network = false);
    void set_selection(const std::optional<Flat> &idx);
    void regenerate_selected();
    void delete_selected();
    std::vector<mapview::Shape> shapes() const;
    QString describe(const std::optional<Tag> &tag) const;
    std::vector<std::uint8_t> land_mask(const Flat &idx) const;
    void commit(const QString &label_text, Flat idx, std::vector<std::uint8_t> nw, const QString &msg);
    QString pixel_info(long long x, long long y) const;

Q_SIGNALS:
    void message(const QString &text);
    void overlayChanged();
    void bannerChanged();

private:
    void _redraw(const std::optional<Flat> &old);
};

}  // namespace riverpanel
