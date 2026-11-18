// diploweb.py — Diplomacy, the views beside the map (the user, 2026-10-26 / 27: "a switchable mode … map view,
// relations view"):
//
//   Relations     a nation's diplomacy as the game's diplomacy screen sorts it — allies, wars, subjects, its overlord,
//                 its sphere (or whose sphere it is in), and on request the nations it is friendly / hostile with — as
//                 a PIE round it (the user, 2026-10-27): a section per group, a nation in two or more groups shown in a
//                 section of its own for exactly those (striped in their colours, between the two: their overlap).
//                 Drag a flag: it moves, but never out of its section (kept for this nation); drag a section's empty
//                 part: it moves round the ring (the order kept for the mod). One click on a flag: its ties with the
//                 nation in view in the side column (a tie never changes by dragging). Double click: that nation in
//                 view. Drag outside the ring (or with the right / middle button): move around; wheel: zoom.
//   Great powers  the eight with their spheres, alliances (blue) and wars (red); a double click: that power's relations.
//   Groups        as many groups as ticked in the list — spheres, alliances, subjects, war sides, the great powers,
//                 continents, culture groups, religions — as boxes, the EU picture: every group one rectangle, a nation
//                 in exactly the boxes of its groups (euler_grid).
//
// Guarantees (history/diplomacy/Guarantees.txt) are an old part of the engine: the game reads them, but nobody can
// make one in play and the game's own file is empty — they are only shown when the mod has one.
//
// Everything is read from the start of the mod as the Diplomacy page reads it; every change goes through the page's
// own (one undo step, written on Apply). Flags: the one each nation flies at the start (its government's).
#pragma once
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/ordered_map.h"
#include "ui/diplopanel.h"

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPainter;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTimer;
class QFrame;

namespace widgets {
class Section;
class SliderField;
class WrapRow;
}

namespace diploweb {

using OptStr = std::optional<QString>;
using Rgb = std::tuple<int, int, int>;
using Groups = OrderedMap<QString, QStringList>;   // {group: [tags]}
using SecKey = QStringList;                         // a section: the groups its nations are in (a tuple)

struct GroupDef {                                   // key, title as the game's screen has it, colour, where (degrees)
    QString key, title;
    Rgb rgb;
    int angle;
};
extern const std::vector<GroupDef> GROUPS;
extern const QHash<QString, QString> SHORT_TITLE;
extern const QHash<QString, Rgb> GROUP_RGB;
extern const QHash<QString, QString> GROUP_TITLE;
extern const QStringList PRIORITY;                  // a nation in several groups is drawn once, in the first of these
extern const QSet<QString> SHOWN_AT_FIRST;
extern const std::vector<std::pair<QString, QString>> SORTS;
extern const double WORLD_W, WORLD_H;               // the web is laid out on this; the view moves / zooms over it
extern const std::vector<Rgb> SET_COLS;

Groups web_of(const QString &a, const moddata::Mod &mod, const oob::OOBEdits *oob, const diplomacy::DiploEdits *dip,
              const std::vector<diplopanel::WarRow> &wars, const QStringList &gps,
              const std::optional<QStringList> &related = std::nullopt);
bool has_guarantees(const diplomacy::DiploEdits *dip);
std::pair<QString, diplopanel::EditFn> tie_set(const QString &a, const QString &b, const QString &kind, bool on);

// euler_grid(masks, k): [(col, row)] per mask and the blocks inside a box they are not in (0: the picture is exact)
std::pair<std::vector<std::pair<long long, long long>>, long long> euler_grid(const std::vector<long long> &masks,
                                                                               long long k, long long seed = 7);

class ZoomCanvas : public QWidget {                 // a view over a world of its own: drag to move, wheel to zoom
    Q_OBJECT
public:
    explicit ZoomCanvas(QWidget *parent = nullptr);
    double scale_ = 1.0;                            // (Python: scale)
    QPointF off{0, 0};
    std::optional<std::pair<QPointF, QPointF>> _pan;
    bool _fitted = false, _moved = false;
    virtual QRectF world_rect();
    void fit();
    QPointF to_world(const QPointF &p) const;
    void begin(QPainter &p);
    void start_pan(const QPointF &pos);
    void pan_to(const QPointF &pos);
    void end_pan();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
};

using FlagFn = std::function<std::optional<QImage>(const QString &)>;
using NameFn = std::function<QString(const QString &)>;

class Web : public ZoomCanvas {                     // the pie round a nation — or the great powers in a circle
    Q_OBJECT
public:
    Web(FlagFn flag_of, NameFn name_of, QWidget *parent = nullptr);
    static constexpr double R_IN = 110.0, R_OUT = 400.0;   // the ring the sections fill (inside: the nation in view)
    FlagFn flag_of;
    NameFn name_of;
    QString mode = QStringLiteral("nation");
    OptStr a;
    Groups groups;
    QSet<QString> shown;
    struct Custom {                                 // a flag moved: (section, part of its angle, radius)
        QString section;
        double part, radius;
    };
    OrderedMap<std::pair<QString, QString>, Custom> custom;   // (a, tag) -> its place
    std::vector<SecKey> sec_order;                  // the sections' order round the ring as dragged (kept for the mod)
    std::vector<SecKey> sec_keys;
    struct Sec {
        SecKey key;
        double a0, a1, rout;
        QStringList tags;
    };
    std::vector<Sec> _sections;
    struct SecDrag {
        SecKey key;
        QPointF p0;
        bool moved;
    };
    std::optional<SecDrag> _secdrag;
    std::function<QStringList(const QStringList &)> order;   // the sort
    struct GpRow {
        QString g;
        QStringList sph, al;
    };
    std::vector<GpRow> gp;                          // [(gp, [sphere], [allies among gps])]
    std::vector<std::pair<QString, QString>> wars;  // between great powers
    std::vector<std::pair<QRectF, QString>> _hit;
    std::optional<std::pair<OptStr, std::optional<SecKey>>> _hover;
    struct Drag {
        QString tag;
        QPointF p0, cur;                            // press point (screen), current point (world)
        bool moved;
    };
    std::optional<Drag> _drag;
    double ring = R_OUT;
    bool has_ring = false;

    struct Placed {
        QString grp;
        Rgb col;
        QString tag;
        QPointF pt;
    };
    struct Label {
        QPointF pt;
        Rgb col;
        QString text, grp;
    };
    struct Layout {
        QPointF mid;
        std::vector<Placed> placed;
        std::vector<Label> labels;
        OrderedMap<QString, QStringList> member;
    };
    QRectF world_rect() override;
    void set_nation(const OptStr &a, const Groups &groups);
    void set_gps(const std::vector<GpRow> &gp, const std::vector<std::pair<QString, QString>> &wars);
    void _node(QPainter &p, const QPointF &c, const QString &tag, bool big = false, std::optional<Rgb> ring = std::nullopt);
    std::vector<SecKey> section_keys(const std::vector<SecKey> &keys) const;
    Layout layout();
    std::pair<double, double> _clamp(double a0, double a1, double ang, double rr) const;
    std::vector<std::pair<double, double>> _fill_section(double a0, double a1, long long n) const;
    std::optional<SecKey> section_at(const QPointF &w) const;
    QRectF label_rect(const QPointF &pt, const QString &text) const;
    QFont _label_font() const;
    std::vector<Label> _clear_of_flags(const QPointF &mid, const std::vector<Placed> &placed,
                                       const std::vector<Label> &labels) const;
    void _paint_gps(QPainter &p);
    OptStr _at(const QPointF &pos) const;
    std::optional<std::tuple<SecKey, double, double>> _section_of(const QString &tag) const;
    QPointF _held_point(const QString &tag, const QPointF &w) const;
    void _move_section(const QPointF &w);
Q_SIGNALS:
    void picked(const QString &tag);               // double-click: that nation in view
    void menu(const QString &tag, const QPointF &global_pos);   // one click on a flag: its ties
    void placed();                                 // a flag was put elsewhere (the places are kept)

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
};

class GroupsView : public ZoomCanvas {              // groups as boxes, the user's EU picture (2026-10-27)
    Q_OBJECT
public:
    GroupsView(FlagFn flag_of, NameFn name_of, QWidget *parent = nullptr);
    static constexpr double CW = 62.0, CH = 50.0;  // a flag's cell (the flag + its name under it)
    FlagFn flag_of;
    NameFn name_of;
    struct Set {
        QString title;
        QSet<QString> tags;
        Rgb col;
    };
    std::vector<Set> sets;
    std::vector<std::pair<QRectF, QString>> _hit;
    struct Box {
        long long mask;
        QRectF rect;
        QStringList tags;
        long long cols;
    };
    std::vector<Box> _boxes;
    QRectF _world;
    struct GRect {
        QRectF rect;
        long long depth;
        QRectF lab;
        QString text;
        bool backed;
    };
    std::vector<GRect> _rects;
    QSet<QString> misplaced;                        // tags in a box not theirs
    QRectF world_rect() override { return _world; }
    void set_sets(const std::vector<std::tuple<QString, QSet<QString>, Rgb>> &sets);
    OrderedMap<long long, QStringList> regions() const;   // {mask: [tags]} — mask: bit i = in group i
    QFont _label_font() const;
    void _layout();
    OptStr _at(const QPointF &pos) const;
Q_SIGNALS:
    void picked(const QString &tag);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
};

class RelChart : public QWidget {                   // the best and the worst relations of the nation as bars
    Q_OBJECT
public:
    explicit RelChart(NameFn name_of, QWidget *parent = nullptr);
    NameFn name_of;
    std::vector<std::pair<QString, long long>> rows;
    void set_rows(const std::vector<std::pair<QString, long long>> &rows);

protected:
    void paintEvent(QPaintEvent *e) override;
};

class DiploWebView : public QWidget {               // the Diplomacy page's view instead of the map
    Q_OBJECT
public:
    explicit DiploWebView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    OptStr a, b;                                    // b: the nation clicked, shown in the side column with its ties
    std::function<std::vector<diplopanel::WarRow>()> wars_fn;
    std::function<QStringList(const QString &)> related;
    std::map<QString, std::optional<QImage>> _flags;
    std::optional<std::map<QString, QString>> _gov_flags;
    std::optional<std::map<QString, long long>> _sizes;
    QString mode = QStringLiteral("nation");
    QString sort = QStringLiteral("as");
    std::optional<QStringList> groups_on;           // the group keys ticked (None: not yet — the biggest spheres)
    struct GSet {
        QString head, key, title;
        QSet<QString> tags;
    };
    std::vector<GSet> _sets;
    bool _filling = false;
    QTimer *_rel_timer;

    QSplitter *_outer, *side;
    std::vector<std::pair<QString, QPushButton *>> seg;
    QLabel *title, *view_head, *view_flag, *view_name, *pick_head, *pick_flag, *pick_name, *pick_status, *pick_war,
        *lists, *rel_head;
    QWidget *ctl, *glist_box;
    QComboBox *sort_box;
    widgets::WrapRow *chips;
    std::map<QString, QPushButton *> chip_btn;
    QLineEdit *gsearch;
    QListWidget *glist;
    QStackedWidget *stack;
    Web *web;
    GroupsView *groups_view;
    widgets::Section *view_box;
    QFrame *pick_box;
    std::map<QString, QCheckBox *> tie_box;
    std::map<QString, QLabel *> tie_head;
    widgets::SliderField *pick_rel;
    RelChart *chart;
    QScrollArea *pick_sc;

    // --- data
    QString name(const QString &t) const;
    std::optional<QImage> flag(const QString &t);
    const std::map<QString, long long> &owners();
    QPixmap _pixmap(const std::optional<QImage> &img) const;
    // --- places dragged (kept per mod)
    QString _key() const;
    void _load_places();
    void _save_places();
    void reset_places(bool sections = false);
    // --- switches
    void set_mode(const QString &k);
    void set_sort(const QString &k);
    void show_group(const QString &k, bool on);
    void _picked(const QString &t);
    // --- the nation clicked: its ties in the side column
    void pick(const OptStr &b);
    void _fill_pick();
    void _tie_ticked(const QString &kind, bool on);
    void _rel_done();
    void _tie(const QString &b, const QString &kind, bool on);
    // --- filling
    void show_for(document::Document *doc, const OptStr &a, std::function<std::vector<diplopanel::WarRow>()> wars_fn);
    std::vector<GSet> sets_of(document::Document *doc, const QStringList &gps,
                              const std::vector<diplopanel::WarRow> &wars);
    void _fill_groups(document::Document *doc, const QStringList &gps, const std::vector<diplopanel::WarRow> &wars);
    void _filter_groups();
    void _group_ticked(QListWidgetItem *it);
    void _tick_none();
    void _show_groups();
Q_SIGNALS:
    void picked(const QString &tag);               // a nation: the page's main nation
    void other(const QString &tag);                // a nation: the panel's second nation
    void edit(const QString &label, const diplopanel::EditFn &fn);   // undo label, fn(oob, diplomacy)
    void message(const QString &text);
};

}  // namespace diploweb
