// flagstudio.py — The flag studio — a window of its own. One of the nation's flags is always the one being worked on
// (the normal one to begin with; a double-click or Edit below takes another): it lies large in the middle, built from
// coloured shapes that are dragged, resized, turned by degrees and locked in place. Every flag keeps its own working
// state while the window lives, so looking at another flag never loses anything; shapes are copied from one flag to
// another with Ctrl+C / Ctrl+V, and a whole flag is handed on to the ticked ones. Shapes snap to the flag's logical
// lines (edges, halves, thirds, quarters, the other shapes) and to the angle of the flag's own diagonal; several shapes
// are picked with Ctrl+click to move, turn or mirror them together; a switch marks where the flag isn't symmetric. Any
// flag of the mod or the game can be taken as the baseline to draw over. Everything is drawn here in code; no
// pictures come from anywhere else.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QFrame;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace colorwheel {
class ColorButton;
}
namespace dialogs {
class PickCombo;
}
namespace widgets {
class SliderField;
class WrapRow;
}

namespace flagstudio {

using OptStr = std::optional<QString>;

inline constexpr int W = 93, H = 64;
inline constexpr int S = 8;                        // canvas pixels per flag pixel
inline constexpr int M = 44;                       // room around the flag on the canvas (handles, guides)
extern const double DIAG;                          // the angle of the flag's diagonal
extern const std::vector<double> SNAP_ANGLES;
extern const std::vector<std::pair<QString, QString>> KINDS;
QString kind_name(const QString &kind, const QString &fallback);   // KIND_NAME.get(kind, fallback)
extern const std::vector<std::pair<QString, QString>> PRESETS;

extern OptStr app_dir_override;                    // (the checks: moreviews.app_dir replaced)
QString designs_dir(const moddata::Mod &mod);

struct Num {                                       // a number of a shape that can be an int in Python (round())
    double v = 0;
    bool is_int = false;
};

struct Shape {
    QString kind;
    double x = 0, y = 0;
    Num w, h;
    double rot = 0;
    QString color;
    bool lock = false;
    std::optional<double> t;
    bool operator==(const Shape &o) const   // (Python: 12 == 12.0)
    {
        return kind == o.kind && x == o.x && y == o.y && w.v == o.w.v && h.v == o.h.v && rot == o.rot &&
               color == o.color && lock == o.lock && t == o.t;
    }
};

struct Design {
    QString bg;
    std::optional<QImage> base;
    std::vector<Shape> shapes;
};

Shape shape(const QString &kind, double x, double y, double w, double h, const QString &color, double rot = 0.0,
            bool lock = false, std::optional<double> t = std::nullopt);
double thickness(const Shape &sh);
double norm_angle(double a);
double snap_angle(double a);
std::pair<QString, std::vector<Shape>> preset(const QString &key, const std::vector<QString> &c);
QPainterPath shape_path(const Shape &sh);
QTransform shape_tf(const Shape &sh);
QRectF aabb(const Shape &sh);
void turn_group(std::vector<Shape> &shapes, const std::vector<std::pair<int, Shape>> &was, const QPointF &centre,
                double delta);
void paint_design(QPainter &p, const Design &design);
QImage render(const Design &design, int scale = 4);
QImage final_image(const Design &design);
std::pair<QImage, double> asymmetry(const Design &design, const QString &mode);
quint32 image_crc(const QImage &img);
QString design_json(const Design &design, quint32 crc);
std::optional<std::pair<Design, std::optional<long long>>> design_from_json(const QString &text);

// ---------------------------------------------------------------------------------------------- the canvas
class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr);
    Design design{QStringLiteral("#1f4fa3"), std::nullopt, {}};
    std::vector<int> sels;                         // the shapes picked (the last one is the one the fields show)
    bool snap = true;
    OptStr asym;                                   // None or "lr" / "tb" / "both"
    double asym_share = 0.0;
    std::optional<QImage> _asym_img;
    struct Drag {
        QString how;                               // move / size / rot / grot
        QPointF start;
        std::vector<std::pair<int, Shape>> was;
        QPointF centre;
    };
    std::optional<Drag> _drag;
    std::vector<std::pair<QString, double>> _guides;   // ("v", x) / ("h", y) / ("d", ±1) lit while dragging

    int sel() const { return !sels.empty() ? sels.back() : -1; }
    void set_sel(const std::vector<int> &idx);
    std::vector<Shape *> chosen();
    std::optional<QRectF> group_rect();
    QPointF to_flag(const QPointF &pt) const;
    QPointF _c(const QPointF &q) const;
    std::vector<std::pair<QString, QPointF>> _handles(const Shape &sh) const;
    std::optional<QPointF> _group_knob();
    int _hit(const QPointF &fp) const;
    std::pair<std::vector<double>, std::vector<double>> snap_lines(const std::vector<int> &skip) const;
    static std::pair<double, std::optional<double>> _near(const std::vector<double> &vals,
                                                          const std::vector<double> &lines, double tol = 1.3);
    void _knob(QPainter &p, const QPointF &top, const QPointF &at);
    void refresh_asym();
    void redraw();
Q_SIGNALS:
    void changed();                                // the design changed (a drag in progress too)
    void committed();                              // … and the change is complete (one step for undo)
    void picked();                                 // which shapes are selected changed

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
};

class _Thumb : public QLabel {
    Q_OBJECT
public:
    explicit _Thumb(QWidget *parent = nullptr);
Q_SIGNALS:
    void clicked();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
};

// ---------------------------------------------------------------------------------------------- the window
class FlagStudio : public QDialog {
    Q_OBJECT
public:
    explicit FlagStudio(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    OptStr tag;
    std::vector<std::pair<QString, QString>> variants;
    QString cur;                                   // the flag being worked on ("" = the normal one, "_monarchy" …)
    using Steps = std::shared_ptr<std::vector<Design>>;   // (Python: one list, shared by the studio and the entry)
    struct Entry {
        Design design;
        Steps undo = std::make_shared<std::vector<Design>>(), redo = std::make_shared<std::vector<Design>>();
        bool dirty = false;
        QString how;
        std::optional<quint32> crc;
    };
    std::map<QString, OrderedMap<QString, Entry>> works;   // tag -> {suffix: its working state}
    std::vector<Shape> clip;                       // shapes copied with Ctrl+C
    std::vector<QString> colors{QStringLiteral("#1f4fa3"), QStringLiteral("#ffffff"), QStringLiteral("#b3202c")};
    Steps _undo = std::make_shared<std::vector<Design>>(), _redo = std::make_shared<std::vector<Design>>();
    Design _last;
    bool _building = false;
    OrderedMap<QString, QStringList> _files;
    struct Cell {
        QFrame *cell;
        QLabel *cap;
        _Thumb *pic;
        QLabel *state;
        QCheckBox *tick;
        QPushButton *edit, *rm;
    };
    OrderedMap<QString, Cell> cells;

    QLabel *head, *editing, *info, *sh_title, *thick_label, *small, *foot_title, *warn;
    dialogs::PickCombo *nation_pick;
    QComboBox *preset_box, *base_tag, *base_var, *asym_mode;
    std::vector<colorwheel::ColorButton *> cbtn;
    QPushButton *clear_base, *undo_btn, *redo_btn, *set_btn;
    QTreeWidget *lst;
    QCheckBox *snap_box, *asym_box, *lock;
    Canvas *canvas;
    colorwheel::ColorButton *bg_btn, *col_btn;
    std::vector<std::pair<QString, QLineEdit *>> fields;   // x, y, w, h
    widgets::SliderField *rot, *thick;
    QWidget *thumb_host;
    widgets::WrapRow *thumb_row;

    QLabel *_bold(const QString &text);
    Design &design() { return canvas->design; }
    Design _state() const;
    void _set_state(const Design &st, const std::vector<int> &sels = {});
    void _commit_step();
    void undo();
    void redo();
    void _touch();
    void _sync();
    void _select_in_list();
    void _show_small();
    Shape *_shape();
    void _show_shape();
    void _canvas_changed();
    void _canvas_picked();
    void _list_picked();
    void _select_all();
    void _asym_changed();
    void _prop(const QString &key, const std::function<void(Shape &)> &set, bool commit = true);
    void _turn_to(double angle);
    void _typed(const QString &key, QLineEdit *ed);
    void _bg(const QString &c, bool commit = true);
    void _add(const QString &kind);
    void _del();
    void _insert(std::vector<Shape> fresh, const std::vector<int> &after = {});
    void _dup();
    void copy_shapes();
    void paste_shapes();
    void _mirror(const QString &how, bool as_copy = true);
    void _order(int step);
    void _place(const QString &how);
    void _use_preset();
    OrderedMap<QString, QStringList> _flag_files() const;
    void _fill_base_tags();
    void _fill_base_variants();
    std::optional<QImage> _flag_image(const QString &fn) const;
    bool _take_base(const OptStr &fn = std::nullopt);
    void _drop_base();
    OrderedMap<QString, Entry> &_work();
    Entry *_entry(const OptStr &sfx = std::nullopt);
    QString _name(const QString &sfx) const;
    Entry &_load_entry(const QString &sfx);
    void _stash();
    void _show_entry(const QString &sfx);
    void select_flag(const QString &sfx);
    void open_for(document::Document *doc, const QString &tag, const OptStr &sfx = std::nullopt);
    void refresh(document::Document *doc, bool rebuild = false);
    void _show_cells();
    void _delete_flag(const QString &sfx);
    void _commit();
Q_SIGNALS:
    void flagEdit(const QString &label, const OrderedMap<QString, QImage> &files);   // {file name: QImage}
    void flagDelete(const QString &label, const QStringList &names);
    void nationWanted(const QString &tag);         // another nation picked in the studio
    void pickOnMap();                              // … or to be clicked on the map

protected:
    void keyPressEvent(QKeyEvent *e) override;
};

}  // namespace flagstudio
