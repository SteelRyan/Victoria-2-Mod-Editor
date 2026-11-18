// checkpanel.py — Errors tab: the problems of this session and the map check — one list of everything that doesn't
// line up, with "go to" and fixes where a fix is safe (all fixes are normal undoable edits, written on Apply).
#pragma once
#include "map/adjacency_geo.h"
#include "map/layerbmp.h"
#include "map/mapcheck.h"
#include "ui/mapview.h"

#include <QSet>
#include <QStyledItemDelegate>
#include <QWidget>
#include <map>
#include <optional>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace drawpanel {
class DrawPanel;
}
namespace pospanel {
class PositionsPanel;
}
namespace widgets {
class Section;
class WrapButton;
}

namespace checkpanel {

using Rgb = std::tuple<int, int, int>;
using mapcheck::Category;
using mapcheck::Item;

extern const std::map<QString, double> PIXEL_ZOOM, PIXEL_RING;
extern const std::map<QString, Rgb> RING_RGB;
extern const QSet<QString> PIXEL_KEYS;
struct Layer {
    QString key, text;
    Rgb rgb;
};
extern const std::vector<Layer> LAYERS;
extern const std::map<QString, QString> LAYER_TIPS;
extern const Rgb ERR_FILL, WARN_FILL, LAND_FILL, SEA_FILL;
Rgb sev_color(const QString &sev);
QString sev_name(const QString &sev);

class _WrapDelegate : public QStyledItemDelegate {   // list lines wrap onto more lines at the panel's width
public:
    explicit _WrapDelegate(QTreeWidget *tree);
    QTreeWidget *tree;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

class MapCheckPanel;

// Map mode "Errors": a calm grey map with every problem on it. Provinces with errors / warnings are tinted; problem
// pixels of provinces.bmp, terrain.bmp and rivers.bmp are bright (each kind its own colour), drawn over the border
// shading so they stay visible on coasts.
class ErrorView {
public:
    explicit ErrorView(MapCheckPanel *panel) : p(panel) {}
    MapCheckPanel *p;
    mapview::ColorTable table();
    std::vector<std::pair<grid::Grid<std::uint8_t>, Rgb>> _layers(long long Y0, long long Y1, long long X0, long long X1);
    std::optional<std::vector<mapview::PixelLayer::Top>> on_top(long long y0, long long y1, long long x0, long long x1);

private:
    std::vector<long long> _sig() const;
    std::optional<std::pair<std::vector<long long>, std::optional<std::vector<mapview::PixelLayer::Top>>>> _full;
};

class MapCheckPanel : public QWidget {
    Q_OBJECT
public:
    MapCheckPanel(moddata::Mod &mod, mapview::MapData &md, QWidget *parent = nullptr);
    ~MapCheckPanel() override;
    moddata::Mod &mod;
    mapview::MapData &md;
    document::Document *doc = nullptr;
    std::map<QString, QString> *loc_tab = nullptr;   // the window's _loc_tab (Python: self.window().__dict__)
    std::vector<Category> cats;
    mapcheck::GeometryCache geo_cache;
    std::map<QString, bool> show;
    std::vector<std::uint8_t> fill_lut;           // error map: colour per province id ((n, 3))
    long long fill_version = 0;                   // (Python: id(fill_lut) — a new table each build)
    std::optional<grid::Grid<std::uint8_t>> stray_mask, unknown_mask;   // (read them after ensure_masks())
    void ensure_masks();
    std::shared_ptr<const grid::Grid<std::int32_t>> _unknown_key;   // (kept: the file on disk)
    std::optional<std::vector<std::uint8_t>> ocean;
    ErrorView error_view{this};
    pospanel::PositionsPanel *pos_panel = nullptr; // the Positions panel: its checks / placing (set by the main window)
    drawpanel::DrawPanel *draw_panel = nullptr;    // set by the main window (pixel fixes are committed like Draw strokes)
    adjacency::AdjGeometry *adj_geo = nullptr;     // the Adjacencies panel's geometry (set by the main window)
    layerbmp::LandSea *landsea = nullptr;          // (Python: doc.landsea)
    QString backup_root;
    std::optional<std::vector<long long>> _sig;   // what was checked last (None: the next refresh checks again)
    std::optional<QString> _fix_note;             // a fix outside the undo list says itself (read once by _fix)

    QLabel *summary, *sel_title, *note, *nofix;
    QPushButton *next_btn;
    widgets::WrapButton *fix_one_btn, *fix_btn;
    widgets::Section *fix_section;
    std::map<QString, QCheckBox *> show_boxes;
    QTreeWidget *tree;

    std::vector<std::uint8_t> _valid_sea() const;
    void refresh(document::Document *doc, bool force = false);
    std::vector<Category> _position_cats(document::Document &doc);
    std::vector<Category> _colormap_cats(document::Document &doc);
    void _fix_colormaps(const std::vector<const Item *> &items);
    const Category *_cat(const QString &key) const;
    void _build_error_map();
    void _show_changed(const QString &key, bool on);
    std::optional<std::pair<int, int>> problem_at(double x, double y);
    std::optional<std::pair<int, int>> _item_with(int ci, long long pid) const;
    QString hover_text(double x, double y);
    void select_problem(int ci, int ii);
    void show_item(const QString &key, int ii);
    std::pair<std::map<mapcheck::OptStr, long long>, long long> error_counts() const;   // {tab or None: errors}, total
    void _fill_tree();
    std::optional<QString> _current_cat_key() const;
    void _picked();
    void _double(QTreeWidgetItem *it);
    void _go(int ci, int ii);
    std::optional<mapcheck::XY> _adj_point(const Item &it);
    void _next_problem();
    std::pair<const Category *, const Item *> _selected() const;
    static bool _fixable(const Category &c, const Item &it);
    void _show_fix();
    void _fix(bool one);
    void _select_next(const QString &key, int pos);
    // pieces: [(flat indexes, new province id)] -> one provinces.bmp step (terrain / rivers follow land ↔ sea)
    using Piece = std::optional<std::pair<std::vector<std::int64_t>, long long>>;
    void _commit_pixels(const QString &label, const std::vector<Piece> &pieces, const QString &msg);
    void _fix_positions(const Category &c, const std::vector<const Item *> &items);
    void _do_fix(const Category &c, const std::vector<const Item *> &items);
    void _fix_states(const std::vector<const Item *> &items);
    void _from_neighbours(const QString &kind, const std::vector<const Item *> &items);
    std::optional<QString> current_key() const;
    std::vector<mapview::Shape> _adj_shapes(int ci, const Category &c);
    std::vector<mapview::Shape> shapes();
    QString describe(const QVariantList &tag) const;
    void map_clicked(const QVariantList &tag);

Q_SIGNALS:
    void overlayChanged();
    void errorMapChanged();                       // the error map needs drawing again (checks ran / layers toggled)
    void message(const QString &text);
    void zoomRequested(double x, double y, double zoom);
    void selectRequested(const QList<long long> &land, const QList<long long> &sea);
    void bannerChanged(const QString &text, std::optional<Rgb> rgb);   // text of the selected problem ("" = none)
    void checked();                               // the checks ran (counts changed maybe)
    void jumpRequested(const QString &key, int ii);   // category key, item index or -1: open the tab where it is fixed
    void renumberRequested();                     // close the gaps in the province numbers (files, then read again)

private:
    int _next = -1;
    std::optional<std::pair<int, std::optional<int>>> _cur;   // (category index, item index) shown on the map
    bool _quiet = false;                          // selecting from a map click: don't move the camera
    std::map<std::int64_t, int> _stray_at;
    std::map<long long, std::pair<int, int>> _by_pid;
    std::optional<std::tuple<const void *, long long, double>> _unknown_for;
    bool _unknown_tried = false;
    bool _masks_dirty = false;
    void _after_masks();
    double _stray_armed = 0;
};

}  // namespace checkpanel
