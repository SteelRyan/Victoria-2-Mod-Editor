// prodpanel.py — Production tab (full screen): the Explorer over every RGO, artisan and factory of production_types.txt,
// the buildings (what they cost to build) and the pops' needs. It starts at ⌂ Everything (all of it, by kind); a click
// on any card puts it in the middle — what it needs on the left, what uses it on the right, what boosts it on top, its
// efficiency goods below (prodtree lays it out). A card's ＋ unfolds what comes after (before) it; a pop type's needs
// are an epicentre (its goods in a ring, a sector per kind of need).
//
// Hover a line or a card: everything it is connected to lights up. The editor on the right changes the selected
// production (its goods, amounts, boosts), good (its price), building (what it costs) or pop needs.
#pragma once
#include "core/ordered_map.h"
#include "core/prodtree.h"
#include "core/production.h"

#include <QColor>
#include <QHash>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPainter;
class QPushButton;
class QRadioButton;
class QVBoxLayout;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace widgets { class Section; class WrapButton; class WrapRow; }

namespace prodpanel {

using prodtree::Cid;
using prodtree::OptCid;
using prodtree::Rgb;
using OptStr = std::optional<QString>;

extern const double CARD_W, GOOD_W, GOOD_H, NEED_W, SLOT, BSLOT, FOOT;
extern const OrderedMap<QString, std::pair<Rgb, Rgb>> KIND_RGB;   // kind colours (top, bottom of the card)
extern const OrderedMap<QString, Rgb> NEED_KIND_RGB;
extern const Rgb BOOST_RGB, EFF_RGB;
extern const QString WHATIF_COL;
extern const Cid ALL;                                  // the Explorer's start screen: everything, by kind

QColor bright(const Rgb &rgb);
int header_h(const QString &name, double width = 256);
std::vector<double> body_slots(double top, double bottom, long long n);
QString money(double v);
QString amt(double a);
QString nid_str(const Cid &n);
Cid base(const Cid &n);                                // a card's own id (kind, key) — the copy number dropped
Cid str_nid(const QString &s);
// {good: picture} from the game's goods icon strip — made once per mod (Python: kept on the mod)
const QHash<QString, QPixmap> &goods_icons(const moddata::Mod &mod);

// what the chart draws for a card (Python: a dict with some of these keys)
struct NodeInfo {
    QString look;                                      // "" / "chip" / "list" / "needsec"
    QString name;
    std::optional<double> hdr, out, bout;
    QString kind;
    std::vector<std::pair<QString, QString>> inputs;   // (good, amount text)
    std::vector<double> slots;
    OptStr output;
    double value = 0;
    long long bonuses = 0;
    std::optional<std::pair<double, double>> money;    // (inputs, output)
    QString tip;
    std::optional<double> cost;                        // a good's price
    QString cost_text;                                 // a needs sector's basket ("≈ …")
    QString sub;
    std::optional<Rgb> sub_rgb, color, rgb;
    QString title, all;
    std::vector<std::pair<QString, QString>> goods;
    std::vector<std::tuple<QString, double, Rgb>> groups, sums;
    std::optional<std::pair<QString, double>> total;
    QString body;
    QString plus;                                      // "left" / "right": it can unfold
    std::optional<QStringList> path;
    bool open = false, inner = false;
    QString badge;
    bool badge_left = false;
};

struct Meta {                                          // a net: {good, rgb, trunk, src, tgts, members, title}
    OptStr good;
    Rgb rgb;
    QString trunk;
    std::vector<prodtree::Port> src;
    QSet<Cid> tgts, members;
    QString title;
};

struct Hit {                                           // _hit: a card, its ＋ ("plus") or a parked card ("parked")
    QString what;                                      // "" / "node" / "plus" / "parked"
    Cid n;
    explicit operator bool() const { return !what.isEmpty(); }
};

// ---------------------------------------------------------------------------------------------- the chart
class ProdChart : public QWidget {                     // zoom (wheel) and pan (drag the background) over the flowchart
    Q_OBJECT
public:
    explicit ProdChart(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    OrderedMap<Cid, QRectF> nodes, ghosts, parked_at;
    std::optional<QRectF> park_area;
    OrderedMap<Cid, NodeInfo> info;
    std::vector<Meta> meta;
    std::shared_ptr<const prodtree::Scene> lay;
    OptCid sel;
    QSet<Cid> multi, focus;
    struct Hover {                                     // ("net", index) / ("node", node)
        bool net = false;
        long long ni = 0;
        Cid n;
        bool operator==(const Hover &o) const { return net == o.net && (net ? ni == o.ni : n == o.n); }
    };
    std::optional<Hover> hover;
    double zoom = 0.5;
    QPointF off{20, 20};
    struct Press {
        QPointF pos, off;
        bool moved;
        Qt::MouseButton button;
        Hit n;
    };
    std::optional<Press> _press;
    bool _fitted = false;
    QHash<QString, QPixmap> icons;
    struct Paths {
        QPainterPath trunk;
        std::vector<std::pair<std::pair<Cid, QString>, QPainterPath>> tg;   // (target, style) -> its branch
    };
    std::map<long long, Paths> _paths;
    std::vector<std::tuple<double, double, double, double, long long>> _seg;
    QHash<std::pair<long long, long long>, std::vector<long long>> _grid;
    QSet<Cid> bout;

    void set_graph(std::shared_ptr<const prodtree::Scene> lay, const OrderedMap<Cid, NodeInfo> &info,
                   const std::vector<Meta> &meta, const OrderedMap<Cid, QRectF> &ghosts = {},
                   const OrderedMap<Cid, QRectF> &parked_at = {}, const std::optional<QRectF> &park_area = std::nullopt);
    void fit();
    void center_on(const Cid &node);
    QPointF _scene(const QPointF &pos) const;
    std::optional<QRectF> _plus_box(const Cid &n, const QRectF &r) const;
    Hit _hit(const QPointF &pos) const;
    std::optional<long long> _hit_line(const QPointF &pos) const;
    static QRectF _park_box(const QRectF &r);
    QString _gname(const QString &g) const;
    QString _tip(const Cid &n) const;
    std::tuple<int, const QSet<Cid> *, bool> _net_state(long long ni) const;
    const NodeInfo &_info(const Cid &n) const;
Q_SIGNALS:
    void picked(const QString &kind, const QString &key, bool ctrl);   // "prod" / "good" / "bld" / "need", key, Ctrl held
    void parked(const Cid &n);                         // a card goes to the holding area
    void isolated(const Cid &n);                       // double-click: explore it
    void expanded(const Cid &n);                       // its ＋ / − pressed
    void unparked(const Cid &n);                       // a click on a parked card puts it back

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void leaveEvent(QEvent *ev) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void paintEvent(QPaintEvent *) override;

private:
    QColor _style_color(const QString &st, const Meta &m) const;
    static void _set_pen(QPainter &p, const QString &st, const QColor &col, int alpha, double w);
    void _icon(QPainter &p, const QString &g, const QRectF &rect) const;
    void _plus(QPainter &p, const QRectF &b, bool is_open, bool dim) const;
    void _badge(QPainter &p, const QRectF &r, const QString &text, bool dim, bool left) const;
    void _draw_node(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
    void _draw_good(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
    std::pair<double, QColor> _card_base(QPainter &p, const Cid &n, const QRectF &r, bool dim,
                                         const std::pair<Rgb, Rgb> &rgb, bool arrow = true) const;
    void _slots(QPainter &p, const QRectF &r, const NodeInfo &d, const QColor &dark, double width = 46,
                bool names = false) const;
    void _foot(QPainter &p, const QRectF &r, const NodeInfo &d) const;
    void _draw_card(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
    QColor _band_frame(QPainter &p, const Cid &n, const QRectF &r, bool dim, const Rgb &top_rgb, const Rgb &bot_rgb) const;
    void _park_dot(QPainter &p, const QRectF &r, const QColor &dark) const;
    void _head(QPainter &p, const Cid &n, const QRectF &r, const QColor &dark, double h = 26) const;
    std::pair<Rgb, Rgb> _frame_rgb(const Cid &n) const;
    void _draw_needsec(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
    void _draw_chip(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
    void _draw_list(QPainter &p, const Cid &n, const QRectF &r, bool dim) const;
};

QString _pct(const QString &v);
QString _cond(const moddata::Mod &mod, const std::vector<std::pair<QString, QString>> &conds);

// ---------------------------------------------------------------------------------------------- the workspace
struct IO {                                            // a card's (inputs [(good, amount)], output good or None)
    production::Goods ins;
    OptStr out;
};
struct Look {                                          // _look: (layout spec, drawing)
    prodtree::Spec spec;
    std::vector<double> slots;
    NodeInfo d;
};
struct Pass {                                          // what an Explorer pass makes: spec, draw, the scene
    prodtree::Specs spec;
    QHash<Cid, std::vector<double>> slots;
    OrderedMap<Cid, NodeInfo> draw;
    std::shared_ptr<prodtree::Scene> sc = std::make_shared<prodtree::Scene>();
};
struct SortKey {                                       // _sort_key: (rank, sub rank, (pop type key), name)
    long long a = 0, b = 0;
    QString b2, c;
    bool operator<(const SortKey &o) const
    {
        return std::tie(a, b, b2, c) < std::tie(o.a, o.b, o.b2, o.c);
    }
};

class ProductionWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ProductionWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    production::ProdEdits *edits = nullptr;
    OptCid sel;                                        // a node: ("prod" / "good" / "bld" / "need" / "tmpl", key)
    std::vector<Cid> ex_trail;                         // the Explorer's way here (nodes), the last one in the middle
    std::set<std::pair<QString, QString>> _skipped;    // parked cards a focused view left out
    OptCid _centre_cid;
    std::shared_ptr<prodtree::Scene> _lay;
    QString view = QStringLiteral("explore");
    std::map<std::pair<QString, QString>, std::set<QStringList>> ex_open;   // the Explorer: centre -> unfolded paths
    OrderedMap<QString, QPushButton *> show_kind;
    QLineEdit *search;
    QPushButton *output_btn, *fit_btn, *new_btn;
    QCheckBox *boosts, *eff, *flows, *goods_between, *combine, *money;
    std::set<std::pair<QString, QString>> parked;
    std::map<QString, double> whatif;                  // good -> a what-if price: only for the sums, never written
    std::optional<std::pair<std::pair<QString, QString>, double>> _eff_armed;   // ((what, key), when)
    std::set<std::pair<QString, QString>> multi;
    QWidget *iso_bar, *crumbs;
    widgets::WrapRow *crumb_lay;
    QLabel *iso_label;
    QPushButton *back_btn;
    ProdChart *chart;
    QWidget *side;
    QVBoxLayout *sl;
    widgets::Section *new_section, *head;
    OrderedMap<QString, QRadioButton *> new_kind;
    QComboBox *new_out;
    QLineEdit *new_key;
    QCheckBox *new_building;
    QLabel *new_err, *title, *sub;
    QWidget *body;
    QVBoxLayout *bl;
    // the model (made by _rebuild)
    OrderedMap<QString, production::Desc> _info;
    QHash<QString, QString> _names;
    QHash<QString, production::BuildingDesc> _bld;
    OrderedMap<QString, std::vector<std::tuple<QString, QString, double>>> _needs;   // pop type -> (kind, good, amount)
    OrderedMap<Cid, IO> _io;
    QHash<QString, std::vector<Cid>> _producers, _consumers;
    OrderedMap<Cid, NodeInfo> _draw;
    std::map<QString, Cid> _search_map;

    QString gname(const QString &g) const;
    QString pname(const QString &key) const;
    QString bname(const QString &b) const;
    QString nname(const Cid &n) const;
    std::vector<std::pair<QString, QString>> goods_items() const;
    void refresh(document::Document *doc);
    OrderedMap<Cid, IO> _model();
    SortKey _sort_key(const Cid &n) const;
    Rgb _gcol(const QString &g) const;
    Look _look(const Cid &n, const OrderedMap<Cid, IO> &io, const OrderedMap<QString, double> &costs, bool money_on,
               QString as_ = QStringLiteral("card"), const OptStr &good = std::nullopt);
    void _rebuild();
    bool _valid(const OptCid &n, const OrderedMap<Cid, IO> &io) const;
    OptCid _ex_centre() const;
    bool _free(const Cid &n);
    Pass _explore_pass(const OrderedMap<Cid, IO> &io);
    void _build_all(const OrderedMap<Cid, IO> &io, Pass &ps);
    void _build_hub(const OrderedMap<Cid, IO> &io, const Cid &c, Pass &ps,
                    const std::function<Cid(const Cid &, const QString &, const OptStr &)> &put);
    void _bars();
    void _fill_crumbs();
    void _fit();
    void set_view();
    void explore(const Cid &n);
    void _ex_back();
    void _toggle_open(const Cid &cid);
    void park(const Cid &n);
    void unpark(const Cid &n);
    void _search_pick(const QString &text);
    void _focus();
    QSet<Cid> _chain(const OptCid &sel) const;
    void _picked(const QString &kind, const QString &key, bool ctrl = false);
    void _fill_side();
    double price(const QString &g) const;
    OrderedMap<QString, double> _prices() const;
    void _whatif_set(const QString &g, const QString &v);
    void _whatif_reset();
    void _create();
    void _new_form();
    void _suggest_key();

private:
    void _build_side();
    void _clear_body();
    void _save_filters();
    void _filters_changed();
    void _save_parked();
    void _node_list(widgets::Section *s, const std::vector<Cid> &nodes, int rows = 8);
    void _fill_multi();
    void _whatif_strip();
    QWidget *_price_field(const QString &g, int width = 90, bool whatif = false);
    QWidget *_amount_field(double a, const std::function<void(const QString &)> &commit, int width = 70);
    void _fill_good(const QString &g);
    void _io_section(const QString &key, const production::Desc &d, const std::function<void(const QString &, const QString &)> &emit,
                     const std::function<void(const QString &, const QString &)> &set_in,
                     const std::function<void(const QString &)> &set_out);
    void _fill_prod(const QString &key);
    void _armed(widgets::WrapButton *btn, const std::pair<QString, QString> &key, const QString &ask,
                const std::function<void()> &fn);
    QString _eff_goods_list(const production::Goods &rows) const;
    void _eff_scope(const QString &key, const production::Desc &d, const QHash<QString, qsizetype> &idx,
                    const std::function<void(const QString &, const QString &)> &emit);
    void _fill_tmpl(const QString &name);
    void _view_buttons(widgets::Section *s, const Cid &n, bool park = true);
    void _fill_building(const QString &b);
    void _fill_need(const QString &pt);
    void _goods_rows(widgets::Section *s, const std::function<QString()> &get_text, const QString &block,
                     const production::Goods &rows, const std::function<void(const QString &, const QString &)> &emit,
                     const QString &add_text);
    QString _new_kind() const;
Q_SIGNALS:
    void edit(const QString &label, const QString &key, const QString &text);   // undo label, production key, new text
    void other(const QString &label, const QString &what, const QString &key, const QString &value);   // "cost" / "bld" / "need"
    void addRequested(const QString &key, const QString &text, const std::optional<std::pair<QString, QString>> &building,
                      const QString &bname);
    void removeRequested(const QString &key);
    void outputRequested();                            // the Output estimate page
};

}  // namespace prodpanel
