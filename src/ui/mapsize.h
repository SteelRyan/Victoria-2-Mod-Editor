// mapsize.py, its page half: CutPreview (the whole map, small, with the parts a cut takes away struck out in red; the
// borders of the cut are dragged) and MapSizeWorkspace (Map → Map size). The cut itself: map\mapsize.
#pragma once
#include "core/ordered_map.h"
#include "map/mapsize.h"
#include "map/moreviews_data.h"

#include <QImage>
#include <QPointF>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QLabel;
class QMouseEvent;
class QPainter;
class QPushButton;
class QSlider;

namespace document {
class Document;
}
namespace widgets {
class Stepper;
}

namespace mapsize {

// The two borders of the cut are dragged — left / right when the width is cut, top / bottom when the height is
// (vertical): dragged(side, columns or rows cut off that end, the mouse was let go).
class CutPreview : public QWidget {
    Q_OBJECT
public:
    explicit CutPreview(QWidget *parent = nullptr);
    QImage img;
    long long w = 1, h = 1;
    long long left = 0, right = 0;
    long long top = 0, bottom = 0;                   // rows cut off the north / south edge
    long long max_cut = 0;                           // how many columns may go in all
    bool vertical = false;                           // the height is cut (one cut at a time — the user, 2026-10-16)
    bool grow = false;                               // (C++ only) the map grows: left / right / top / bottom are ADDED
    double uw() const { return double(grow ? w + left + right : w); }   // map columns the picture's frame stands for
    double uh() const { return double(grow ? h + top + bottom : h); }
    void _paint_grow(QPainter &p, double x0, double y0, double dw, double dh);
    std::optional<std::tuple<QString, double, long long>> _drag;

    void set_image(const QImage &img, long long map_w, long long map_h = 1);
    void set_cut(long long left, long long right, std::optional<long long> top = std::nullopt,
                 std::optional<long long> bottom = std::nullopt);
    std::tuple<double, double, double, double> _place() const;
    double edge_x(const QString &side) const;
    double edge_y(const QString &side) const;
    QString _side_at(const QPointF &pos) const;      // "" = None
    long long _cut_at(const QString &side, double v) const;
    double _coord(QMouseEvent *e) const;
    void _emit(double v, bool done);

    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

Q_SIGNALS:
    void dragged(const QString &side, int px, bool done);
};

class MapSizeWorkspace : public QWidget {
    Q_OBJECT
public:
    MapSizeWorkspace(moddata::Mod &mod, mapview::MapData &md, QWidget *parent = nullptr);
    moddata::Mod &mod;
    mapview::MapData &md;
    document::Document *doc = nullptr;
    QString backup_root;                             // ("" = None)
    std::optional<std::pair<QString, qint64>> _img_key;
    bool _armed = false;
    QString dir = QStringLiteral("w");
    std::vector<long long> _cuts{0}, _hcuts{0};
    // fn(pids) -> True: delete these provinces and Apply (the window's); throws with the reason
    std::function<bool(const std::vector<long long> &)> before_cut;
    // fn(label, {file: (how, picture)}): the window's pending-file step
    std::function<void(const QString &, const OrderedMap<QString, moreviews::Asset> &)> asset_edit;
    struct MmKey {
        QString rel;
        qint64 cache;
        long long w, version;
        bool operator==(const MmKey &) const = default;
    };
    std::optional<MmKey> _mm_key;
    std::optional<Fit> _mm;
    // (C++ only, 2026-11-10: the picture read from disk is kept while its file is unchanged — read again on every visit it
    //  had a new cacheKey each time, so the fit above, ~150 ms, was never reused)
    mutable std::optional<std::tuple<QString, qint64, qint64, QImage>> _mm_disk;

    QLabel *size_lbl, *hint, *rule, *total_lbl, *right_lbl, *h_lbl, *bottom_lbl, *result, *warn, *mm_note;
    QLabel *w_how = nullptr, *w_all = nullptr, *w_end = nullptr, *h_how = nullptr, *h_all = nullptr, *h_end = nullptr,
           *what_lbl = nullptr;
    bool growing() const { return dir.startsWith(u'g'); }   // (C++ only) "gw" / "gh": the map grows
    void _fill_choices();                            // the amounts offered (cut or grow)
    CutPreview *preview;
    std::map<QString, QPushButton *> dir_btns;
    QWidget *w_box, *h_box;
    widgets::Stepper *steps, *left, *h_steps, *top;
    QSlider *slider, *h_slider;
    QCheckBox *del_box, *mm_box;
    QPushButton *cut_btn, *mm_fit_btn;

    std::pair<QString, std::optional<QImage>> _minimap_image() const;   // ("", None) when there is none
    void _check_minimap();
    void _fit_minimap();
    QLabel *_bold(const QString &text);
    long long total() const;
    void refresh(document::Document *doc);
    void _dragged(const QString &side, int px, bool done);
    void _slid(int i);
    void set_dir(const QString &d);
    void _hint();
    void _h_slid(int i);
    void _steps_changed();
    void _h_changed();
    long long _top_min() const;
    std::pair<long long, long long> cut_rows() const;
    void _changed();
    void _changed_grow(long long tot, long long l, long long r, long long north, long long south);
    void _cut();

Q_SIGNALS:
    void message(const QString &text);
    void reloadRequested();                          // the map was cut: the mod has to be read again
};

}  // namespace mapsize
