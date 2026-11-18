// terrainpanel.py — Map → Draw → Terrain: paint map/terrain.bmp (8-bit palette; terrain.txt says which index is which
// terrain type).
//
// Colours are offered by terrain type and level. Tools: brush, fill (a connected area of one colour) and "fill the
// selected provinces", each in one of three styles:
//   Flat     one colour
//   Rings    concentric level bands from the edge of the area to its centre, across all selected provinces — how the
//            mod draws hills (16 → 17 → 19) and mountains (25 → 26 → 27 → 30 → 31)
//   Patches  a blotchy mix of the chosen levels, like the mod's deserts, jungles and grey plains
// Every stroke / fill is one undo step; terrain.bmp is written on Apply (with a backup).
#pragma once
#include "core/ordered_map.h"
#include "map/layerbmp.h"
#include "ui/drawlink.h"
#include "ui/mapview.h"

#include <QPushButton>
#include <QWidget>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QGraphicsOpacityEffect;
class QGridLayout;
class QLabel;

namespace document {
class Document;
}
namespace moddata {
class Mod;
}
namespace widgets {
class SliderField;
class WrapRow;
}

namespace terrainpanel {

using Rgb = std::tuple<int, int, int>;
using Flat = std::vector<std::int64_t>;
using Vals = std::vector<std::uint8_t>;

extern const QStringList TYPE_ORDER;
extern const std::vector<std::pair<QString, QString>> STYLES;   // Rings sits below Fill
QString style_name(const QString &key);                        // (dict(STYLES)[key])
QString type_label(const QString &t);
extern const Rgb MISMATCH_RGB;       // a land province with ocean terrain: bright yellow (no terrain is)
extern const Rgb SEA_MISMATCH_RGB;   // terrain (land) on a sea tile: orange (mismatch.py: "t_sea")

// The map shows terrain.bmp: every pixel in its palette colour (ocean as the map's sea colour). Pixels whose land /
// sea doesn't match their province are bright orange / yellow (the Errors tab lists them).
class TerrainView : public mapview::PixelLayer {
public:
    TerrainView(const mapview::MapData &md, const layerbmp::IndexedBitmap &tb);
    const mapview::MapData &md;
    const layerbmp::IndexedBitmap &tb;
    std::array<std::array<std::uint8_t, 3>, 256> pal{};
    std::array<bool, 256> ocean{};
    bool pure_pixels() const override { return true; }
    mapview::RgbImage colors(long long y0, long long y1, long long x0, long long x1) override;
    std::optional<std::vector<Top>> on_top(long long y0, long long y1, long long x0, long long x1) override;
    struct Last {
        std::array<long long, 4> box;
        grid::Grid<std::uint8_t> bad_sea, bad_land;
    };
    std::optional<Last> _last;
};

class Swatch : public QPushButton {
public:
    Swatch(long long idx, const Rgb &rgb, QWidget *parent = nullptr);
    long long idx;
    Rgb rgb;
    void set_state(bool selected, bool used);
};

class TerrainBox;

// Mouse tool of the terrain layer.
class TerrainHandler : public mapview::MouseHandler {
public:
    explicit TerrainHandler(TerrainBox *box) : b(box) {}
    TerrainBox *b;
    std::optional<OrderedMap<long long, long long>> touched;
    std::optional<std::pair<double, double>> last, last_pos;
    mapview::MapView *view() const;
    void _outline(double x, double y);
    void preview();
    bool cancel();
    void _stamp(double x, double y);
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool has_wheel() const override { return true; }
    bool wheel(double steps, Qt::KeyboardModifiers mods) override;
};

class TerrainBox : public QWidget {
    Q_OBJECT
public:
    TerrainBox(moddata::Mod &mod, const mapview::MapData &md, drawlink::Panel *panel, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const mapview::MapData &md;
    drawlink::Panel *panel;
    document::Document *doc = nullptr;
    std::optional<long long> index;
    OrderedMap<long long, QString> types;
    OrderedMap<long long, Swatch *> swatches;
    std::map<long long, QPushButton *> level_btns;
    const void *_built_for = nullptr;
    std::vector<std::int64_t> counts = std::vector<std::int64_t>(256, 0);
    TerrainHandler handler{this};

    QWidget *pal_host, *levels_host, *ring_host;
    QGridLayout *pal_grid;
    QLabel *pick_info, *levels_lbl, *order_lbl, *ring_lbl, *dens_lbl, *psize_lbl, *grad_lbl, *preview_lbl;
    QLabel *fill_note, *status;
    std::map<QString, QPushButton *> tool_btns, style_btns;   // (in the order of their items: tool_keys / STYLES)
    QStringList tool_keys;
    widgets::WrapRow *levels_row;
    QPushButton *ring_auto, *fill_sel_btn = nullptr;
    widgets::SliderField *ring_w, *density, *psize, *gradient, *size_spin;
    QGraphicsOpacityEffect *_ring_fade;
    QCheckBox *reverse;
    QComboBox *scope;

    std::map<QString, QPushButton *> _seg(QGridLayout *grid, int row, const std::vector<std::pair<QString, QString>> &items);
    void _rings_toggled(bool on);
    void _brush_toggled(bool on);
    std::optional<long long> ring_width() const;
    layerbmp::IndexedBitmap &tb() const;
    QString tool() const;
    QString style() const;
    QString type_of(long long i) const;
    std::vector<long long> same_type(long long i) const;
    std::vector<long long> chosen_levels() const;
    void _build_palette();
    void set_index(long long i);
    void _fill_levels();
    void _options_changed();
    QString style_for_fill() const;
    void refresh(document::Document *doc);
    void recount();
    Vals allowed_mask(const std::vector<std::uint16_t> &ids, const Flat *idx = nullptr);
    Vals values_for(const Flat &idx, const QString &style, const grid::Grid<std::uint8_t> *region_mask = nullptr,
                    const std::array<long long, 4> *box = nullptr);
    std::pair<long long, std::vector<long long>> _patch_order() const;
    std::pair<std::vector<long long>, std::vector<double>> patch_levels() const;
    void _patch_preview();
    void commit(const QString &label_text, Flat idx, Vals nw, QString msg);
    std::map<long long, QString> _majority(const Flat &idx) const;
    Vals mismatch_values(const Flat &idx, Vals nw);
    void fill_at(double x, double y);
    void fill_selected();
    QString pixel_info(long long x, long long y) const;

Q_SIGNALS:
    void message(const QString &text);
    void changed();
};

}  // namespace terrainpanel
