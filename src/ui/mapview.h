// mapview.py, the widget half: the province map in a pan / zoom QGraphicsView — the map picture made from a colour
// table, highlights, the pulsing selection, focus markings, and the layers drawn over it (stars, the crown, shapes,
// borders between pixels, province names). The data half (MapData) is map\mapview_data.
// (Python's shape dicts are Shape; a table with .colors() — a pixel layer — is PixelLayer.)
#pragma once
#include "core/ordered_map.h"
#include "map/mapview_data.h"

#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QSet>
#include <QVariantList>
#include <tuple>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QGraphicsPixmapItem;
class QLabel;
class QPushButton;
class QTimer;

namespace mapview {

extern const std::map<int, Rgb> HL_COLORS;    // 1 = selection → the red layer
extern const std::map<int, double> HL_TINT;
extern const int CTX;                          // highlight code: other provinces of the selected province's state
extern const int CTX_FOREIGN;                  // 20+k: same state, k-th OTHER owner → coloured mesh (cross-hatch)
extern const Rgb CTX_RGB;
extern const std::vector<Rgb> MESH_COLORS;

using RgbImage = grid::Grid<std::array<std::uint8_t, 3>>;   // (h, w, 3) uint8

// a pixel layer (terrain.bmp / rivers.bmp view, the map check's layer): what a table with .colors is in Python
struct PixelLayer {
    virtual ~PixelLayer() = default;
    virtual RgbImage colors(long long y0, long long y1, long long x0, long long x1) = 0;
    virtual bool pure_pixels() const { return false; }   // every pixel exact: borders are the EdgeLayer's lines
    struct Top {
        grid::Grid<std::uint8_t> mask;
        std::optional<Rgb> rgb;                           // None: darken
        std::vector<std::int64_t> idx;                    // (instead of a mask: the pixels, flat in the window —
    };                                                    //  Python's np.nonzero(m) of the whole map's few pixels)
    virtual std::optional<std::vector<Top>> on_top(long long, long long, long long, long long) { return std::nullopt; }
};

// the map's colours: (n, 3) uint8 per province (Q = 0), (n, Q, 3) stripes, or a pixel layer
struct ColorTable {
    std::vector<std::uint8_t> rgb;
    long long Q = 0;
    std::shared_ptr<PixelLayer> layer;
    // a province table that also paints pixels over the border shading (Python: _OutlinedLut and its on_top)
    std::function<std::optional<std::vector<PixelLayer::Top>>(long long, long long, long long, long long)> on_top;
};

double line_grow(double s);                    // line width factor for zoom scale s (screen px per map px)
double dot_grow(double s);                     // marker size factor: small zoomed out, big zoomed in

class StarItem : public QGraphicsItem {        // state-capital marker; constant on-screen size
public:
    StarItem();
    long long capital = 0;
    QRectF boundingRect() const override { return {-8, -8, 16, 16}; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget = nullptr) override;
};

// The map picture (C++ only, 2026-11-14). It draws as a QGraphicsPixmapItem with Qt::FastTransformation does (the whole
// pixmap at the origin, no smoothing), but it holds the ONLY copy of the pixmap: a changed box is painted into it in
// place and only that box is drawn again. (A QGraphicsPixmapItem shares its pixmap — every brush stamp copied the
// whole 48 MB map picture, and its setPixmap drew the whole view again, border lines and all.)
class BaseItem : public QGraphicsItem {
public:
    QRectF boundingRect() const override { return pm.isNull() ? QRectF() : QRectF(QPointF(0, 0), pm.deviceIndependentSize()); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    void set(QPixmap p);                             // a new picture (the whole map drawn again)
    void patch(const QImage &part, int x, int y);    // part of the picture replaced at (x, y)
    const QPixmap &pixmap() const { return pm; }

private:
    QPixmap pm;
};

class CrownItem : public QGraphicsItem {       // the nation's capital: a gold crown in a dark round badge
public:
    static constexpr double R = 15;
    CrownItem();
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget = nullptr) override;
};

// one shape of the ShapeLayer (Python: a dict; a key it leaves out has the default written here)
struct Shape {
    QString kind;                              // line|dot|x|diamond|square|circle|poly|image|badge|army|anchor|label
    std::vector<QPointF> pts;
    Rgb color{0, 0, 0};
    std::optional<double> width, size;         // (defaults differ by kind, as in Python's .get calls)
    double radius = 1.0, min_scale = 0.0, dy = 16, opacity = 1.0;
    bool dash = false, halo = false, outline = true, warn = false, hollow = false, smooth = true;
    std::optional<QVariantList> tag;           // (tag[0]: its kind, for hit's `kinds`)
    QString text;
    std::optional<Rgb> ring, frame;
    std::vector<std::vector<QPointF>> polys;
    int alpha = 210;
    QImage img;
    QRectF rect;
};

class ShapeLayer : public QGraphicsItem {      // lines and markers over the map in map-pixel coordinates
public:
    ShapeLayer();
    std::vector<Shape> shapes, temp;          // temp: rubber bands while editing (on top, never hit-tested)
    void set_bounds(long long w, long long h);
    QRectF boundingRect() const override { return _rect; }
    void set_shapes(std::vector<Shape> shapes);
    void set_temp(std::vector<Shape> shapes);
    std::optional<QVariantList> hit(double x, double y, double tol, const std::optional<QStringList> &kinds = {}) const;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget = nullptr) override;

    struct Groups;
    static void _draw_unit(QPainter *p, const Shape &sh, double s, double dot, bool halo_pass);
    static void _draw_one(QPainter *p, const Shape &sh, double s, double grow, double dot, bool halo_pass,
                          const QColor &outline);

private:
    std::vector<long long> _visible(const QRectF &ex, double s) const;
    QRectF _rect;
    struct Seg {
        double x1, y1, x2, y2;
        long long i;
        QVariant kind;
        double marker, reach, min_scale;
    };
    std::vector<Seg> _hit;
    std::vector<std::array<double, 5>> _box;   // per shape: minx, miny, maxx, maxy, min_scale
    double _scale = 1.0;                       // zoom of the last paint, so clicks match what is drawn
    std::shared_ptr<Groups> _gcache;           // the batches of the visible shapes (key: their indexes)
    std::vector<long long> _gcache_key;
};

class EdgeLayer : public QGraphicsItem {       // borders drawn BETWEEN the pixels over an exact pixel layer
public:
    static constexpr double MIN_SCALE = 2.0;
    EdgeLayer();
    MapData *data = nullptr;
    bool prov = true, state = true;
    void set_data(MapData *data);
    QRectF boundingRect() const override { return _rect; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget = nullptr) override;

private:
    QRectF _rect;
};

struct NameItem {                              // (cx, cy, angle, w, h, text, rgb) — angle clockwise, radians
    double cx, cy, angle, w, h;
    QString text;
    Rgb rgb;
};

class NameLayer : public QGraphicsItem {       // province names drawn the way positions.txt places them
public:
    NameLayer();
    std::vector<NameItem> items;
    QFont font;
    void set_bounds(long long w, long long h);
    QRectF boundingRect() const override { return _rect; }
    void set_items(std::vector<NameItem> items);
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *widget = nullptr) override;

private:
    struct PathOf {
        QPainterPath path;
        double tw, th;
    };
    const PathOf &_path(const QString &text);
    std::pair<QPixmap, double> _image(const QString &text, const Rgb &rgb, double w, double h, double hs);
    QPixmap _draw_name(const QString &text, const Rgb &rgb, double w, double h, double hq);
    QRectF _rect;
    std::vector<std::array<double, 4>> _arr;
    std::map<QString, PathOf> _paths;
    std::map<std::tuple<QString, int, int, int, double>, std::vector<std::pair<long long, QPixmap>>> _imgs;
};

struct FocusLayer {                            // {"style", "rgb", "pids"} of set_focus_layer
    QString style;
    std::optional<Rgb> rgb;
    OrderedMap<long long, double> pids;
};

// an editing tool on the map (adjpanel.MapEditHandler and the like): press / move / release / rclick (x, y, tol, …)
struct MouseHandler {
    virtual ~MouseHandler() = default;
    virtual bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) = 0;
    virtual void move(double x, double y) = 0;
    virtual void release(double x, double y, double tol, Qt::KeyboardModifiers mods) = 0;
    virtual bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) = 0;
    virtual bool rclick_passes() const { return false; }
    virtual bool has_wheel() const { return false; }    // (Python: hasattr(h, "wheel"))
    virtual bool wheel(double, Qt::KeyboardModifiers) { return false; }
};

struct LegendRow {                             // (rgb, text) or (rgb, text, key, on): a row that switches its kind
    Rgb rgb;
    QString text;
    std::optional<QString> key;
    bool on = false;
};

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MapView(QWidget *parent = nullptr);
    ~MapView() override;

    BaseItem *item;
    QGraphicsPixmapItem *sel_item, *focus_item, *pattern_item;
    MapData *data = nullptr;
    std::vector<std::uint32_t> _base32, _img32;   // 0xFFRRGGBB (Python's base / img are views onto them)
    bool has_base = false;                         // (Python: self.base is not None)
    std::vector<std::uint8_t> hl;
    std::optional<std::array<long long, 4>> hl_box;
    std::vector<StarItem *> stars;
    bool show_stars = false;
    // (C++ only, the user's wish 2026-11-09) hard lines between the pixels when zoomed in, as in GIMP: one pixel can
    // be told from the next. Drawn from GRID_FROM screen px per map px.
    bool pixel_grid = false;
    static constexpr double GRID_FROM = 6.0;
    void set_pixel_grid(bool on);
    std::vector<long long> sel_ids;
    ShapeLayer *shape_layer;
    NameLayer *name_layer;
    EdgeLayer *edge_layer;
    CrownItem *crown = nullptr;
    std::function<bool(double, double, double, Qt::KeyboardModifiers)> click_hook;
    MouseHandler *mouse_handler = nullptr;
    QSet<long long> focus_capitals;

    void set_data(MapData *data);
    RgbImage _colors(const ColorTable &table, long long y0, long long y1, long long x0, long long x1);
    static std::vector<std::uint32_t> _shaded_table(const ColorTable &table, const QString &view_mode);
    std::vector<std::uint32_t> _render_window(const ColorTable &table, const QString &view_mode, long long y0,
                                              long long y1, long long x0, long long x1,
                                              const std::vector<std::uint32_t> *packed = nullptr);
    void render_base(const ColorTable &table, const QString &view_mode);
    void redraw_box(const std::array<long long, 4> &box);
    void set_selection_layer(const std::vector<long long> &pids, bool force = false);
    static grid::Grid<std::uint8_t> _grow(const grid::Grid<std::uint8_t> &m, int n);
    static grid::Grid<std::uint8_t> _shrink(const grid::Grid<std::uint8_t> &m, int n);
    void set_focus_layer(const std::vector<FocusLayer> &layers);
    std::optional<std::array<long long, 4>> focus_box() const;
    void show_box(const std::optional<std::array<long long, 4>> &box);
    void camera_moved();
    void set_highlight(const OrderedMap<long long, long long> &codes);
    void set_nation_capital(std::optional<long long> pid);
    void set_capitals(const std::vector<long long> &pids);
    void set_show_stars(bool on);
    void set_focus_capitals(const std::vector<long long> &pids);
    void fit();
    void zoom_by(double f, bool anchor_mouse = true, std::optional<QPoint> pos = std::nullopt);
    void _zoomed();
    void center_on_province(long long pid, std::optional<double> zoom_to = std::nullopt);
    void set_banner(const QString &text, std::optional<Rgb> rgb = std::nullopt);
    void set_mode_pill(const QString &text, std::function<void()> clicked = {});
    void set_legend(const QString &title, const std::vector<LegendRow> &rows);
    QPushButton *_pill = nullptr;
    QLabel *_banner = nullptr, *_legend = nullptr;
    QTimer *_focus_timer, *_pulse, *_calm;        // (public, as in Python: the checks stop the pulse)
    void _render_focus();

Q_SIGNALS:
    void provinceClicked(int pid, Qt::KeyboardModifiers mods);
    void provinceRightClicked(int pid, Qt::KeyboardModifiers mods);   // when no drawing tool takes the right-click
    void provinceHovered(int pid, int x, int y);
    void zoomChanged(double z);
    void legendToggled(const QString &key);   // a switch row of the map's legend was clicked (its key)

protected:
    void scrollContentsBy(int dx, int dy) override;
    void drawForeground(QPainter *p, const QRectF &rect) override;   // (the pixel grid)
    void wheelEvent(QWheelEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    void leaveEvent(QEvent *ev) override;

private:
    static void _shade(RgbImage &base, const MapData &d, long long y0, long long x0, const QString &view_mode);
    void _on_top(const ColorTable &table, long long y0, long long y1, long long x0, long long x1);
    void _render_patterns(const std::vector<FocusLayer> &layers);
    int _focus_step() const;
    void _resume_pulse();
    void _tick();
    void _overlay(const std::array<long long, 4> &box);
    void _push_full();
    void _push_rect(const std::array<long long, 4> &box);
    void _scale_crown();
    void _update_star_visibility();
    double _tol() const;
    void _place_banner();
    void _place_legend();

    std::optional<ColorTable> _table;
    QString _view_mode;
    std::optional<std::vector<std::uint32_t>> _packed;
    std::vector<FocusLayer> _focus_layers, _pattern_layers;
    std::optional<std::vector<std::tuple<QString, std::optional<Rgb>, std::vector<std::pair<long long, double>>>>>
        _focus_key;
    std::optional<int> _focus_k;
    double _phase = 0.0;
    std::optional<QPointF> _press, _last;
    bool _panning = false, _handler_active = false;
    std::function<void()> _pill_cb;
};

}  // namespace mapview
