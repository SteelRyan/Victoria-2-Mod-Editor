// adjpanel.py — Map tab: map/adjacencies.csv — view the adjacencies on the map, edit / add / delete rows, check and
// generate the coast crossings (landing penalty) and build Meme rivers from sea tiles.
// (Python's shape tags are tuples; here QVariantLists, a (sea, land) / (a, b) pair as a QVariantList of two.)
#pragma once
#include "core/adjacency.h"
#include "core/ordered_map.h"
#include "map/adjacency_geo.h"
#include "ui/mapview.h"

#include <QList>
#include <QWidget>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace document {
class Document;
}
namespace moddata {
class Mod;
}

namespace adjpanel {

using Pt = adjacency::Pt;
using PairLL = std::pair<long long, long long>;
extern const std::map<QString, QString> KIND_NAMES;
extern const std::vector<std::pair<QString, QString>> TYPE_ITEMS;
extern const std::map<QString, QString> TYPE_HELP;
extern const OrderedMap<QString, bool> DEFAULT_SHOW;
constexpr double INTO = 0.6;   // how far a line end reaches from the crossing point towards the province centre

QString prov_name(const moddata::Mod &mod, long long pid);
adjacency::RowFields guess_spec(adjacency::AdjacencyFile &adj, adjacency::AdjGeometry &geo, const moddata::Mod &mod,
                                long long a, long long b);
Pt lerp(const Pt &p, const Pt &q, double f, double w = 0);
std::vector<std::vector<Pt>> seam_split(const std::vector<Pt> &pts, double w);

// Province id box + name + pick-on-map button.
class ProvField : public QWidget {
    Q_OBJECT
public:
    explicit ProvField(const moddata::Mod &mod, bool allow_zero = false);
    const moddata::Mod &mod;
    QSpinBox *spin;
    QLabel *name;
    void _changed();
    long long value() const;
    void set(long long v);

Q_SIGNALS:
    void pickRequested();
    void changed();
};

class MapPanel;

// Mouse tool for the map while "Edit crossings on the map" is on (installed as MapView.mouse_handler).
class MapEditHandler : public mapview::MouseHandler {
public:
    explicit MapEditHandler(MapPanel *panel) : p(panel) {}
    MapPanel *p;
    struct Drag {
        long long uid;
        QString which;
        Pt fixed;
        double sx, sy;
        bool moved;
    };
    std::optional<Drag> drag;
    std::optional<long long> start;   // first province of a new crossing (right-click)
    mapview::ShapeLayer *layer() const;
    long long _pid(double x, double y) const;
    QString _name(long long pid) const;
    bool cancel();
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void _finish(double x, double y);
};

class MapPanel : public QWidget {
    Q_OBJECT
public:
    MapPanel(moddata::Mod &mod, adjacency::AdjGeometry &geo, QWidget *parent = nullptr);
    moddata::Mod &mod;
    adjacency::AdjGeometry &geo;
    document::Document *doc = nullptr;
    std::vector<long long> sel, sea_sel;
    OrderedMap<QString, bool> show_;                 // (Python: self.show)
    std::optional<long long> sel_uid;
    std::optional<PairLL> sel_missing;
    long long _miss_i = -1;
    std::optional<std::pair<std::pair<std::vector<long long>, double>, adjacency::RiverPairs>> _river_cache;
    std::optional<adjacency::CoastStatus> _status;
    std::optional<OrderedMap<QString, adjacency::RiverStatus>> _rstat;
    OrderedMap<long long, QStringList> _probs;
    std::map<PairLL, QString> _rivermiss;
    std::optional<std::vector<std::pair<QString, long long>>> _river_items;
    mapview::MapView *view = nullptr;                // the MapView (set by the main window)
    MapEditHandler handler{this};

    QLabel *title, *sub, *edit_help, *problem_note, *coast_info, *river_state, *river_info, *rivers_all, *nc_state;
    QLabel *list_title, *ed_title, *ed_help, *ed_warn;
    QPushButton *edit_btn, *gen_all, *gen_sel, *next_miss, *rm_stale, *mark_btn, *unmark_btn, *river_btn;
    QPushButton *river_all_btn, *find_btn, *nc_on_btn, *nc_off_btn, *rm_all_btn, *add_btn, *upd_btn, *del_btn;
    std::map<QString, QCheckBox *> show_boxes;
    QCheckBox *only_sel;
    QComboBox *river_pick, *ed_type;
    QLineEdit *river_name, *ed_comment;
    QDoubleSpinBox *river_gap;
    QTreeWidget *tree;
    ProvField *ed_a, *ed_b, *ed_t;
    QSpinBox *ed_data;

    // --- helpers
    adjacency::AdjacencyFile &adj() const;
    void _edit_mode(bool on);
    void _set_show(const QString &key, bool on);
    const OrderedMap<QString, adjacency::RiverStatus> &river_stat();
    void _gap_changed();
    const adjacency::CoastStatus &status();
    QString pname(long long pid) const;
    // --- refresh
    void refresh(document::Document &doc, const std::vector<long long> &sel, const std::vector<long long> &sea_sel,
                 const std::optional<std::pair<QString, QString>> &header);
    std::vector<PairLL> _missing_for_selection();
    void _fill_river_pick();
    void _pick_river_tile(int i);
    // --- list
    std::vector<adjacency::Row> _rows_here() const;
    void _remove_all_here();
    void _fill_tree();
    void _tree_picked();
    void _zoom_item(QTreeWidgetItem *it);
    std::optional<Pt> _point_of(long long a, long long b);
    // --- selection from the map
    void select_row(std::optional<long long> uid, bool from_tree = false);
    void map_clicked(const QVariantList &tag);
    void set_pick(const QString &field, long long pid);
    // --- editor
    adjacency::Row _editor_row() const;
    void _editor_changed();
    long long _next_canal() const;
    void _swap();
    void _from_selection();
    void fill_editor(const adjacency::RowFields &spec);
    void _edit(const QString &label_text, const std::function<void(adjacency::AdjacencyFile &)> &fn);
    void _add();
    void _update();
    void _delete();
    // --- coast crossings
    void _generate(bool selection_only);
    void _next_missing();
    void _remove_stale();
    // --- meme river
    std::vector<long long> river_tiles() const;
    std::pair<adjacency::RiverPairs, long long> river_proposals();
    QString _river_default_name(const std::vector<long long> &tiles);
    void _river_changed(bool emit = true);
    void _mark_river();
    void _unmark_river();
    void _set_no_coast(bool on);
    void _find_rivers();
    void _make_river();
    void _add_all_river_missing();
    // --- overlay
    struct Ends {
        Pt a, b;
        const Pt &at(const QString &which) const { return which == u"a" ? a : b; }
    };
    std::optional<Ends> ends(const adjacency::Row &r);
    std::optional<std::pair<Pt, Pt>> coast_ends(long long s, long long l);
    std::optional<std::pair<Pt, Pt>> pair_ends(long long a, long long b);
    std::vector<mapview::Shape> shapes();
    QString describe(const QVariantList &tag);

Q_SIGNALS:
    void pickRequested(const QString &field);     // field: "a" | "b" | "through"
    void zoomRequested(double x, double y);       // map point to centre on
    void selectRequested(const QList<long long> &land, const QList<long long> &sea);
    void overlayChanged();
    void message(const QString &text);
    void editModeChanged(bool on);
    void canalsRequested();
};

}  // namespace adjpanel
