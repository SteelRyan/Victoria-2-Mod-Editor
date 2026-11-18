// The game's screen as the player sees it, drawn by the editor (C++ only, 2026-11-09): the map through the game's own
// camera (map/gamecam.h — the same heights, the same piece of map at every resolution of one shape), what the map
// shows at the height picked (borders by step, the flat paper map, province names, unit counters — the rules measured
// in the game, reference/zoom 2026-11-09), and the interface windows on top at their size on that screen.
//   wheel   zoom as the game does (one notch: × 0.9 in, × 1.2 out; 40 … 1000)
//   drag    move over the map
// viewChanged tells the page when the camera moved (Zoom & map marks: its ruler and the line of active levels).
//
// What is the editor's own approximation, not the game's drawing: the map's colours (colormap.dds / colormap_water.dds
// laid flat, the paper map's pale country colours), the size of the names (fitted to their province), where the
// sample counters stand (a few provinces in view, not the real armies).
#pragma once
#include "map/gamecam.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
namespace mapview { class MapData; }
namespace moddata { class Mod; }

namespace gameview {

struct World {                                       // what the map is drawn from (once per mod and map version)
    long long W = 0, H = 0;
    long long version = -1;                          // MapData.version it was made for
    QImage land, water;                              // colormap.dds, colormap_water.dds (null: plain colours)
    std::vector<QRgb> colour;                        // per province: its owner's colour (0: none / sea)
    std::vector<int> owner;                          // per province: its owner's place in tags (-1: none)
    QStringList tags, tag_names;
    std::vector<QString> names;                      // per province (empty: none)
    struct Label {                                   // a country's big name on the paper map (map px, z north)
        int tag = -1;                                // its place in tags
        double x = 0, z = 0, dx = 1, dz = 0, len = 0;
    };
    std::vector<Label> labels;                       // one per big piece of a country
};
using OwnerOf = std::function<std::optional<QString>(long long)>;
std::shared_ptr<World> make_world(const moddata::Mod &mod, const mapview::MapData &md, const OwnerOf &owner_of);
int owner_at(const World &w, const mapview::MapData &md, double x, double z);   // the country at a map point (-1)
// where a view opens: a little south of the biggest piece of the first country of countries.txt that has land (the game
// opens on the player's country); the middle of the map when none has
QPointF start_point(const World &w, const moddata::Mod &mod, const mapview::MapData &md);

struct Overlay {                                     // an interface window on the game's screen
    QImage img;                                      // at the game's size (1 ×)
    double x = 0, y = 0;                             // its place on a 1920 × 1080 screen (shortcuts::SCREEN); on
                                                     // another screen it keeps its distance to the nearer edges
    double factor = 1.0;                             // how much bigger it is drawn (a 4K submod's size)
};

class GameView : public QWidget {
    Q_OBJECT
public:
    explicit GameView(QWidget *parent = nullptr);
    const mapview::MapData *md = nullptr;
    std::shared_ptr<World> world;
    gamecam::View cam;                               // x, z, h (w / hgt follow the widget)
    gamecam::Levels levels;
    QSize screen{1920, 1080};                        // the player's screen
    QString title;
    bool political = false;
    int player = -1;                                 // the player's country (place in world->tags): the game darkens
                                                     // the others on the 3D map
    std::vector<Overlay> windows;
    double counter_factor = 1.0;                     // the counters' size (1: the game's)
    std::function<void(QPainter &)> counter;         // draws one counter at 1 × in its own pixels (null: a plain box)
    QSizeF counter_size{70, 46};
    double names_factor = 1.0;                       // the province names' size (2: the map font doubled)

    void set_view(double x, double z, double h);
    void set_levels(const gamecam::Levels &lv);
    QRectF screen_rect() const;                      // where the screen is drawn in the widget
    QSize sizeHint() const override { return {520, 330}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
Q_SIGNALS:
    void viewChanged(double x, double z, double h);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QImage _map;
    std::vector<double> _key;
    void _render(int rw, int rh);
    void _names(QPainter &p, const gamecam::View &v, const QRectF &r);
    void _labels(QPainter &p, const gamecam::View &v, const QRectF &r);
    void _counters(QPainter &p, const gamecam::View &v, const QRectF &r);
    std::optional<QPointF> _press;
    gamecam::View _press_cam;
};

}  // namespace gameview
