// mapmarkspage.py — Library → Zoom & map marks: how big the marks on the map are and at which camera heights the map
// changes its look.
//
//   Map marks       the counters of armies and fleets (flag, strength, organisation bar), battles, sieges … made
//                   bigger or smaller (mapmarks.h) — seen in the game at 1.5 ×, 2026-10-26
//   Zoom heights    map/default.map: the heights at which the borders change (border_heights), the flat paper map
//                   (terrain_sheet_heights), the trees, where borders stop being drawn — this file COUNTS for the
//                   checksum (measured)
//   Your own view   counter_distance in YOUR settings of this mod (Documents › Paradox Interactive › Victoria II › the
//                   mod's folder): up to it the province NAMES are drawn (text_height: no effect seen) — corrected
//                   2026-11-09, measured in the game; the counters stop at 500, fixed in the game
//
// C++ only (2026-11-09, the user's wish): every height is a slider to drag, and the game's screen (1920×1080,
// gameview.h) is drawn in the middle of the page to zoom in and out like in the game and see what appears where; the
// line under it says which level is active. (A 4K screen beside it was taken out on the user's word, 2026-11-15: it
// was not drawn the way the 4K submod shows the game — Tools → Submods & 4K has that one.)
//
// Measured too (2026-10-26) and why there is no slider here for it: the trade window's stockpile slider (sell_slider,
// maxValue 2000) ends at exactly 2000; with 5000 its middle showed 412,496 and its end about 77 million, with 20000 it
// overflowed — the game's numbers run wild above 2000.
#pragma once
#include "core/ordered_map.h"
#include "map/gamecam.h"
#include "map/moreviews_data.h"
#include "ui/gameview.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QLabel;
class QPainter;
class QPushButton;
class QSlider;
namespace locwin { class GameLook; }
namespace mapview { class MapData; }
namespace document { class Document; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }
namespace gamefont { class Fonts; }
namespace widgets { class StepSlider; class Stepper; }

namespace mapmarkspage {

extern const std::vector<double> FACTORS;
extern const QString DEFAULT_MAP, SETTINGS_GUI;

struct Heights {                                     // read_heights: {border_heights: [..], …, tree: n, border_cutoff: n}
    OrderedMap<QString, std::vector<double>> lists;  // border_heights, terrain_sheet_heights
    OrderedMap<QString, std::optional<double>> nums; // tree, border_cutoff (None: a number that does not read)
};
Heights read_heights(const QString &text);
struct HeightVals {
    OrderedMap<QString, std::vector<double>> lists;
    OrderedMap<QString, double> nums;
};
QString write_heights(QString text, const HeightVals &vals);
OrderedMap<QString, std::optional<double>> read_render(const QString &text);   // counter_distance, text_height
QString write_render(QString text, const OrderedMap<QString, double> &vals);

struct Parts {                                       // the army counter's pieces at 1 ×
    struct Item {
        QImage img;                                  // (null: none)
        double x = 0, y = 0;
        QString kind;                                // pic / flag / bar
    };
    std::vector<Item> items;
    std::tuple<double, double, int> text{17, 30, 12};
    std::optional<QImage> flag_mask, flag_over;
    std::optional<std::pair<double, double>> bar_size;
    std::shared_ptr<gamefont::Fonts> fonts;          // (null: None)
    std::optional<QString> font;
};

void draw_counter(QPainter &p, const Parts &pp);    // one army counter at 1 ×, in its window's own pixels
// (C++ only, shared with the 4K screen of Tools → Submods & 4K)
std::optional<QString> find_picture(const moddata::Mod &mod, const QString &path);   // the mod's picture, else the game's
std::optional<Parts> counter_parts(const moddata::Mod &mod, scriptstore::ScriptStore *store);
struct ScreenWin {                                   // an interface window to put on the game's screen
    QString rel, window;                             // its .gui file, its windowType's name
    double factor = 1.0;                             // how much bigger it is drawn
    std::optional<QPointF> at;                       // where the game puts its origin (1920 × 1080), when the game
                                                     // places it itself (not its .gui position)
    std::set<QString> leave_out = {};                // parts of it the game does not show here
};
std::vector<gameview::Overlay> screen_windows(locwin::GameLook &look, const std::vector<ScreenWin> &want);

class MarkPreview : public QWidget {
    // The army counter as the game draws it (from the game's pictures and the window's positions), at the game's size
    // and at the size picked, side by side on a patch of land.
    Q_OBJECT
public:
    explicit MarkPreview(QWidget *parent = nullptr);
    std::optional<Parts> parts;
    double factor = 1.0;
    void set_parts(std::optional<Parts> parts, double factor);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void _counter(QPainter &p);
};

class Ruler : public QWidget {
    // The camera's height, low (close to the map) at the left: where the map changes its look. The camera of the
    // previews stands on it as a gold triangle (click or drag on the ruler to move it); grey outside 40 … 1000, where
    // the game's camera never goes.
    Q_OBJECT
public:
    explicit Ruler(QWidget *parent = nullptr);
    std::vector<std::tuple<double, QString, QString>> marks;   // (height, text, colour)
    double camera = -1;                              // (-1: none)
    void set_marks(std::vector<std::tuple<double, QString, QString>> marks);
    void set_camera(double h);
Q_SIGNALS:
    void picked(double h);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    double _top() const;
    double _at(double x) const;                      // the height at a point of the ruler
};

using Texts = OrderedMap<QString, QString>;
using Files = OrderedMap<QString, moreviews::Asset>;

class MapMarksPage : public QWidget {
    Q_OBJECT
public:
    explicit MapMarksPage(moddata::Mod &mod, QWidget *parent = nullptr);
    ~MapMarksPage() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    OrderedMap<QString, long long> _fonts;
    widgets::StepSlider *size;
    MarkPreview *preview;
    QPushButton *use_btn, *set_btn;
    QLabel *size_msg, *set_msg;
    Ruler *ruler;
    OrderedMap<QString, QSlider *> h_edit;           // the heights of map/default.map (keys as H_KEYS)
    OrderedMap<QString, QLabel *> h_val;             // the number next to each slider
    QSlider *cd, *th, *opt_max;
    QLabel *cd_val, *th_val, *opt_val;
    mapview::MapData *md = nullptr;                  // (set by the window)
    gameview::GameView *view_n;
    QPushButton *ter_btn, *pol_btn;
    QLabel *cam_lbl;

    gamecam::Levels levels() const;                  // the sliders' heights
    void _world();                                   // (re)make the previews' map when the map changed
    void _windows();
    void _views();
    void _camera(double x, double z, double h);

    double factor() const;
    std::optional<QString> find(const QString &path) const;
    static std::optional<QImage> _qimage(const QString &full, long long frames);
    void refresh(document::Document *doc);
    std::optional<Parts> _parts();
    void _preview();
    void _ruler();
    void _use_size();
    void _use_heights();
    void _use_opt_max();
    std::optional<QString> settings_file() const;
    void _read_settings();
    void _write_settings();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // (as every page of the Library)
    void nameEdit(const QString &key, const QString &text);
    void bundleEdit(const QString &label, const mapmarkspage::Texts &texts, const mapmarkspage::Files &files);

private:
    QLabel *_bold(const QString &text);
    QSlider *_slider(int lo, int hi, int step);
    std::shared_ptr<gameview::World> _gw;
    std::unique_ptr<locwin::GameLook> _look;
    std::optional<Parts> _counter_parts;
};

}  // namespace mapmarkspage
