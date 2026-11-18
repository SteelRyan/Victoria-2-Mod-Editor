// Map → Draw → Colours (C++ only, 2026-11-10): the land colour map (map/terrain/colormap.dds) on the map as the game
// stretches it, and two ways to tint it:
//   Selected provinces  a hue laid on the provinces selected (Ctrl+click) and an area around them, fading out with the
//                       distance (the user's wish: "radiating outward and getting lighter further out")
//   Soft brush          the same hue painted by hand, strongest in the middle of the brush
// Right-click picks the colour under the cursor. Every tint is one undo step; the file is written on Apply in its own
// format (mapsize::recolor_dds), the old one backed up.
#pragma once
#include "map/colormap.h"
#include "ui/drawlink.h"
#include "ui/mapview.h"

#include <QImage>
#include <QWidget>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;

namespace colorwheel {
class ColorButton;
}
namespace document {
class Document;
}
namespace moddata {
class Mod;
}
namespace widgets {
class SliderField;
}

namespace colormappanel {

class ColormapBox;

// The map shows the colour maps: land from colormap.dds, sea from colormap_water.dds (as the game stretches them).
class ColormapView : public mapview::PixelLayer {
public:
    ColormapView(const mapview::MapData &md, ColormapBox *box) : md(md), box(box) {}
    const mapview::MapData &md;
    ColormapBox *box;
    bool pure_pixels() const override { return true; }
    mapview::RgbImage colors(long long y0, long long y1, long long x0, long long x1) override;
};

// Mouse tool: the soft brush (left-drag), the colour under the cursor (right-click).
class ColormapHandler : public mapview::MouseHandler {
public:
    explicit ColormapHandler(ColormapBox *box) : b(box) {}
    ColormapBox *b;
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

class ColormapBox : public QWidget {
    Q_OBJECT
public:
    ColormapBox(moddata::Mod &mod, const mapview::MapData &md, drawlink::Panel *panel, QWidget *parent = nullptr);
    moddata::Mod &mod;
    const mapview::MapData &md;
    drawlink::Panel *panel;
    document::Document *doc = nullptr;
    ColormapHandler handler{this};

    colorwheel::ColorButton *color_btn;
    std::map<QString, QPushButton *> tool_btns;
    QStringList tool_keys;
    widgets::SliderField *strength, *reach, *size_spin;
    QWidget *sel_rows, *brush_rows;
    QCheckBox *keep;
    QPushButton *apply_btn;
    QLabel *sel_note, *file_note, *status;

    struct Source {
        QImage pic;            // RGBA8888, file rows (row 0 = south)
        QString path;          // the file whose format it is written in
        bool pending = false;  // changed, not applied yet
        bool own = false;      // the mod's own file
    };
    const Source *source();
    const QImage *water();
    const QImage *shown();     // what the map shows: the brush stroke under way, else the source
    // a brush stroke under way
    struct Stroke {
        QImage base, live;
        std::vector<float> w;
        double ref = 0;
    };
    std::optional<Stroke> stroke;

    QString tool() const;
    colormap::Tint tint() const;
    colormap::Stretch stretch() const;
    std::vector<long long> chosen() const;
    void refresh(document::Document *doc);
    void apply_selection();
    void commit(const QImage &pic, const QString &label_text, const QString &msg);
    QString pixel_info(long long x, long long y);
    void _tool_changed();

Q_SIGNALS:
    void message(const QString &text);

private:
    std::optional<Source> _src;
    QString _src_key;
    std::optional<QImage> _water;
    QString _water_key;
};

}  // namespace colormappanel
