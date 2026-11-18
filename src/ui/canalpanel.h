// canalpanel.py — Map tab → Canals: every canal of the mod (its adjacencies.csv row, what opens it, its model on the
// map) — add one by clicking its province, switch the model (Kiel / Panama / Suez), move it pixel by pixel or drag it,
// choose how it opens. The model is drawn as its outline seen from above, where the game puts it (measured, see
// canals.py).
// (Python's canal dicts are canals::Canal; a function that edits works on a copy, as Python's dict outlives the
// refresh that an edit sets off.)
#pragma once
#include "map/adjacency_geo.h"
#include "map/canals.h"
#include "ui/mapview.h"

#include <QWidget>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPainterPath;
class QPushButton;
class QTreeWidget;

namespace document {
class Document;
}
namespace adjpanel {
class ProvField;
}

namespace canalpanel {

using Pt = adjacency::Pt;
using Rgb = adjacency::Rgb;
extern const Rgb WATER, BUILT, LINE;
extern const QString WARN;

class CanalPanel;

// The page's map tool: picks a province when a pick is armed, selects a canal by its outline, drags the selected
// canal's model.
class CanalHandler : public mapview::MouseHandler {
public:
    explicit CanalHandler(CanalPanel *panel) : p(panel) {}
    CanalPanel *p;
    std::optional<std::tuple<long long, double, double>> drag;
    bool rclick_passes() const override { return true; }   // a right-click it doesn't use goes on to the map (its menu)
    mapview::ShapeLayer *layer() const;
    bool cancel();
    std::optional<canals::Canal> _canal_at(double x, double y);
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
};

class CanalPanel : public QWidget {
    Q_OBJECT
public:
    static constexpr bool always_handler = true;   // its map tool is on whenever the page is shown
    CanalPanel(moddata::Mod &mod, mapview::MapData &md, adjacency::AdjGeometry &geo, QWidget *parent = nullptr);
    moddata::Mod &mod;
    mapview::MapData &md;
    adjacency::AdjGeometry &geo;
    document::Document *doc = nullptr;
    mapview::MapView *view = nullptr;
    std::optional<canals::Models> models;            // read on first use (the .gfx files)
    CanalHandler handler{this};
    std::optional<long long> sel_num;
    std::optional<QString> pick;                     // armed map pick: "new" | "prov" | "a" | "b" | "place"
    std::vector<canals::Canal> _list;
    bool _filling = false;
    std::optional<long long> _cmp_for;
    std::vector<std::uint8_t> _sea_arr;
    std::map<std::pair<QString, long long>, std::optional<canals::Samples>> _samp;
    std::optional<std::pair<long long, long long>> _edge_key;
    std::vector<mapview::Shape> _edges;

    QLabel *title, *pick_note, *count, *ed_title, *ed_probs, *model_note, *fit_lbl, *open_note;
    QPushButton *new_btn, *prov_btn, *cmp_btn, *away_btn, *zoom_btn, *del_btn;
    QTreeWidget *tree;
    QLineEdit *ed_name, *ed_x, *ed_y;
    adjpanel::ProvField *ed_prov, *ed_a, *ed_b;
    QButtonGroup *model_group;
    std::map<QString, QPushButton *> model_btns;     // (Python: a dict, filled in the models' order)
    QStringList model_order;
    QHBoxLayout *model_row, *cmp_row;
    QWidget *cmp_host, *ed_section;
    std::map<QString, QPushButton *> nudge;
    QComboBox *ed_open;

    struct Place {                                   // (anchor, model, x, y)
        Pt a;
        QString model;
        double x, y;
    };
    using Polys = OrderedMap<QString, std::vector<std::vector<Pt>>>;

    // --- data
    adjacency::AdjacencyFile &adj() const;
    scriptstore::ScriptStore &store() const;
    canals::Models &_models();
    const canals::Canal *canal(std::optional<long long> num) const;
    bool own(const canals::Canal &c) const;
    std::optional<QString> mode_of(const canals::Canal &c) const;
    std::pair<std::optional<QString>, std::optional<canals::Sprite>> own_sprite(long long num);
    std::optional<Pt> anchor(long long pid) const;
    std::optional<Place> place(const canals::Canal &c, double dx = 0.0, double dy = 0.0);
    std::optional<Polys> model_polys(const QString &model, const Pt &a, double x, double y);
    // --- refresh
    void refresh(document::Document &doc, const std::vector<long long> &sel,
                 const std::optional<std::pair<QString, QString>> &header);
    void _fill_tree();
    void _tree_picked();
    void select(std::optional<long long> num, bool zoom = false);
    void _fill_editor();
    QString _open_text(const canals::Canal &c, const std::optional<QString> &mode) const;
    std::optional<QString> _sprite_rel(const canals::Canal &c);
    // --- picking on the map
    void _arm(const std::optional<QString> &what);
    void picked(long long pid);
    void _say(const QString &text);
    std::optional<std::pair<long long, long long>> sea_pair(long long pid);
    // --- edits
    void _edit(const QString &label_text, const std::function<void()> &fn,
               const QStringList &kinds = {QStringLiteral("adjacency"), QStringLiteral("scripts")});
    void _new_canal(long long pid);
    void _set_rows(const canals::Canal &c, long long prov, long long a, long long b, const QString &name,
                   const QString &label_text);
    void _fields_edited();
    void _name_edited();
    void _write_sprite(long long num, const canals::Canal *c, const QString &model, double x, double y);
    void _write_opening(long long num, long long prov, const std::optional<QString> &mode, const QString &name);
    void _model_picked(const QString &model);
    void move_to(double x, double y, const std::optional<QString> &label_text = std::nullopt);
    void _nudge(const QString &key);
    void _xy_typed();
    void _open_changed();
    void _delete();
    void _zoom();
    // --- how a model fits the map's pixels
    const std::vector<std::uint8_t> &_sea();
    const std::optional<canals::Samples> &_samples(const QString &model, long long stride = 1);
    std::optional<canals::Score> fit(const canals::Canal &c, const QString &model, double x, double y,
                                     long long stride = 1);
    std::optional<std::tuple<canals::Score, double, double>> best_fit(const canals::Canal &c, const QString &model,
                                                                       const std::optional<Pt> &around = std::nullopt,
                                                                       long long reach = 30);
    QString fit_text(const std::optional<canals::Score> &f, const canals::Canal &c) const;
    void _compare();
    void _take(const QString &model, double x, double y);
    std::optional<QImage> preview(const canals::Canal &c, const QString &model, double x, double y, long long scale = 5);
    void place_at(double cx, double cy);
    // --- overlay
    std::optional<QPainterPath> outline_path(const canals::Canal &c, double dx = 0.0, double dy = 0.0);
    std::vector<mapview::Shape> canal_shapes(const canals::Canal &c, bool sel, double dx = 0.0, double dy = 0.0,
                                             bool temp = false);
    const std::vector<mapview::Shape> &prov_edges(long long pid);
    std::vector<mapview::Shape> shapes();
    QString describe(const std::optional<QVariantList> &tag) const;
    void map_clicked(const std::optional<QVariantList> &tag);

Q_SIGNALS:
    void overlayChanged();
    void message(const QString &text);
    void zoomRequested(double x, double y);
    void selectRequested(const QList<long long> &land, const QList<long long> &sea);
};

}  // namespace canalpanel
