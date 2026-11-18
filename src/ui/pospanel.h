// pospanel.py — Map tab → Positions: map/positions.txt — show names / units / buildings on the map, check them,
// regenerate them and move them by dragging. (The checks are the data half, map/pospanel_data: PosChecks.)
#pragma once
#include "map/pospanel_data.h"
#include "ui/mapview.h"

#include <QList>
#include <QSet>
#include <QWidget>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace pospanel {

using Rgb = std::tuple<int, int, int>;
extern const Rgb PROB_COLOR;
struct Marker {
    QString kind, label;
    Rgb color;
    QString shape;
    double size;
};
extern const std::vector<Marker> MARKERS;      // (MARKER: the same, looked up by kind)
const Marker *marker(const QString &kind);
constexpr double ALL_MARKERS_FROM = 1.2;        // zoom from which the markers of every province are drawn

class PositionsPanel;

// Drag position markers while "Move positions on the map" is on.
class PosEditHandler : public mapview::MouseHandler {
public:
    explicit PosEditHandler(PositionsPanel *panel) : p(panel) {}
    PositionsPanel *p;
    struct Drag {
        long long pid;
        QString kind;
        double sx, sy;
    };
    std::optional<Drag> drag;
    mapview::ShapeLayer *layer() const;
    bool cancel();
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
};

class PositionsPanel : public QWidget, public PosChecks {
    Q_OBJECT
public:
    PositionsPanel(moddata::Mod &mod, const mapview::MapData &md, QWidget *parent = nullptr);
    std::vector<long long> sel;
    mapview::MapView *view = nullptr;
    PosEditHandler handler{this};
    long long _prob_i = -1;

    QLabel *title, *sub, *edit_help, *prob_info, *sel_title, *sel_info;
    QPushButton *edit_btn, *regen_all, *regen_sel, *fix_naval, *next_prob;
    QCheckBox *tilt;
    QDoubleSpinBox *scale_spin, *rot_spin;
    QWidget *sel_section;
    OrderedMap<QString, bool> show_;            // (Python: self.show)
    std::map<QString, QCheckBox *> show_boxes;

    positions::PositionsFile &pf() const;
    QString pname(long long pid) const;
    void _set_show(const QString &key, bool on);
    void _edit_mode(bool on);
    void _edit(const QString &label_text, const std::function<void(positions::PositionsFile &)> &fn);
    void refresh(document::Document &doc, const std::vector<long long> &sel,
                 const std::optional<std::pair<QString, QString>> &header);
    void _show_problems();
    void remove_naval(const std::vector<long long> &pids);
    std::tuple<long long, long long, long long> _naval_work() const;
    void _fill_selected();
    void _scale_edited();
    void _rot_edited();
    void _remove_naval();
    positions::Entry place_entry(long long pid, positions::Entry e, const QSet<QString> &kinds,
                                 const std::vector<std::uint8_t> &valid);
    void _regenerate(const std::optional<std::vector<long long>> &pids,
                     const std::optional<QSet<QString>> &kinds = std::nullopt);
    void _fix_naval();
    void _next_problem();
    std::vector<mapview::Shape> shapes();
    std::pair<std::vector<mapview::Shape>, std::optional<mapview::NameItem>>
    _province_shapes(long long pid, const positions::EntryP &e, const Problems &probs, bool focus, bool edit);
    const std::vector<mapview::NameItem> &name_items() const { return _names; }
    QString describe(const QVariantList &tag) const;
    void map_clicked(const QVariantList &tag);

Q_SIGNALS:
    void overlayChanged();
    void message(const QString &text);
    void editModeChanged(bool on);
    void zoomRequested(double x, double y);
    void selectRequested(const QList<long long> &pids, const QList<long long> &sea);

private:
    struct Cached {
        positions::EntryP e;
        Problems probs;
        bool focus, edit;
        std::vector<bool> show;
        std::vector<mapview::Shape> shapes;
        std::optional<mapview::NameItem> name;
    };
    std::map<long long, Cached> _shape_cache;
    std::vector<mapview::NameItem> _names;
};

}  // namespace pospanel
