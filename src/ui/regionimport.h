// regionimport.py, its page half: SourceMap (the map to pick provinces on, and to drag where they land) and
// RegionImportPage (Import → Provinces from its map). The plan and the import itself: map\regionimport.
// (Python reads doc.landsea, which the window sets; here the window sets `landsea`.)
#pragma once
#include "map/regionimport.h"

#include <QPointF>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QCheckBox;
class QLabel;
class QPainter;
class QPushButton;
class QTimer;
class QWidget;

namespace widgets {
class Stepper;
}

namespace regionimport {

// A map to pick provinces on, with the main map's keys: click = that province (group_of says what a click with
// Shift / Alt means), Ctrl = add / remove instead of replace, drag = add what the mouse passes over; wheel = zoom,
// right / middle button = move the map. With `movable`: the lit part is dragged to another place.
class SourceMap : public QWidget {
    Q_OBJECT
public:
    explicit SourceMap(QWidget *parent = nullptr);
    const grid::Grid<std::uint16_t> *ids = nullptr;
    std::vector<Rgb8> lut;
    std::set<long long> picked;
    std::optional<grid::Grid<std::uint8_t>> mark;   // pixels to light (where an import lands)
    double scale = 1.0, ox = 0.0, oy = 0.0;
    std::optional<QString> _drag;                    // "on" / "off"
    std::optional<std::tuple<QPointF, double, double>> _pan;
    std::optional<std::tuple<QPointF, long long, long long>> _move;
    std::optional<QPointF> _press;
    std::function<std::vector<long long>(long long, Qt::KeyboardModifiers)> group_of;
    bool movable = false;
    bool numbers = true;                             // every province's number on it (when large enough to hold one)
    std::vector<std::tuple<double, double, QString>> extra;   // what lands where (drawn in gold)
    const std::vector<long long> *edge_ids = nullptr;   // a border where it differs (None: every province's border)
    double edge_dark = 0.45;
    bool pick_outline = false;
    bool spread_extra = false;
    struct Cent {
        const grid::Grid<std::uint16_t> *of = nullptr;
        std::vector<double> cx, cy, cnt;
    };
    std::optional<Cent> _cent;
    long long labelled = 0;
    int _last = 0;

    void set_map(const grid::Grid<std::uint16_t> *ids, const std::vector<Rgb8> &lut, bool keep_view = false);
    void fit();
    void show_box(double x0, double y0, double x1, double y1);
    long long at(const QPointF &pos) const;
    const Cent &centres();
    void _paint_numbers(QPainter &p, const std::vector<std::uint16_t> &sub);
    bool lit(const QPointF &pos) const;
    void _set(long long pid);

    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

Q_SIGNALS:
    void changed();                                  // the picked provinces changed
    void hovered(int pid);
    void moved(int right, int down);                 // the lit part was dragged by (px right, px down)
};

class RegionImportPage : public QWidget {
    Q_OBJECT
public:
    explicit RegionImportPage(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    mapview::MapData *md = nullptr;
    importer::Source *src = nullptr;
    importer::Importer *imp = nullptr;
    std::unique_ptr<RegionSource> rs;
    std::optional<Plan> plan;
    layerbmp::LandSea *landsea = nullptr;            // (Python: doc.landsea)
    QTimer *_timer;
    std::vector<Rgb8> _their_lut;
    std::optional<std::set<long long>> _theirs_picked;
    struct SrcCent {
        const grid::Grid<std::uint16_t> *of = nullptr;
        std::vector<double> xs, ys, cnt;
    };
    std::optional<SrcCent> _src_cent;

    std::map<QString, QPushButton *> view_btn, mode_btn, match_btn, nat_btn;
    std::map<QString, QCheckBox *> box;
    SourceMap *map;
    QLabel *hover, *keys, *sel_lbl, *nat_note, *where_note, *result, *warn;
    QCheckBox *num_box, *clear_box;
    QWidget *match_host;
    widgets::Stepper *dx, *dy;
    QPushButton *go;

    QLabel *_bold(const QString &text);
    void set_source(document::Document *doc, mapview::MapData *md, importer::Source *src, importer::Importer *imp);
    Opts opts() const;
    void _mode_changed();
    void _numbers(bool on);
    void _landing_labels();
    const SrcCent &_src_centres();
    std::vector<long long> _group(long long pid, Qt::KeyboardModifiers mods);
    void _moved(int right, int down);
    void _show_map(bool fresh = false, bool keep = false);
    std::set<long long> _sel() const;
    void _hover(int pid);
    void _picked();
    void _add_state();
    void _add_owner();
    void _clear();
    void _replan();
    void _borders_text(const Plan &plan, const std::function<QString(const std::vector<long long> &, int)> &names);
    void _import();

Q_SIGNALS:
    void message(const QString &text);
    void imported();
};

}  // namespace regionimport
