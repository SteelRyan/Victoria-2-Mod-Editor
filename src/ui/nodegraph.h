// nodegraph.py — Scripts → ⧉ Nodes: the event or decision picked as a graph of nodes, after ComfyUI's way of working —
// next to the block builder, on the same script and the same catalogue (blockcode).
//
// Every line of the script is a node of its own:  continent = europe  is the node "continent" with one value to pick;
// capital_scope = { … }  is a node with the input "conditions";  any_country = { limit = { … } … }  a node with the
// two inputs "limit" (conditions, blue) and "effects" (orange). It reads LEFT TO RIGHT like the script (the user,
// 2026-10-26): the event itself is the first node on the left, with the inputs trigger, mean_time_to_happen,
// immediate and option on its right edge; what goes into an input stands to the right of it. A node's own dot is on
// its left; a wire carries it into an input of the same colour on the right of another node.
//
//   a value (the dark field in a node)   click: a list of what the mod has to pick from (yes / no flips at once)
//   ＋ at an input                        a list of what fits there; the one picked is made and wired in
//   drag from a dot                      a wire; onto a node / an input = wired; let go over the empty canvas = the
//                                        list of what fits, made and wired at once
//   double-click the empty canvas        any node, loose (dashed): not part of the script until it is wired
//   click a wire, then Delete            the wire goes (the node stays, loose);  Delete on a node: the node goes
//   right mouse button / wheel           move the view / zoom;  Tidy = lay everything out again
//
// Wires into one input are read from top to bottom. Every change writes the script at once (one undo step).
//
// A very large event (thousands of lines) opens FOLDED: only its upper levels are made into nodes; a node with more
// inside says "▸ 214 lines inside" — a click opens it (one level), the ▾ in a node's head folds it again. What is
// folded is not drawn and not laid out, so the size of the event doesn't matter; it stays in the script as it is.
//
// (Python's item dicts are blockcode::PItem here: held by reference, id(item) is the pointer.)
#pragma once
#include "core/blockcode.h"
#include "core/eventtemplates.h"
#include "core/ordered_map.h"

#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
namespace document { class Document; }
namespace events { class EventDB; struct Entry; }
namespace moddata { class Mod; }
namespace refs { class RefIndex; }
namespace widgets { class Stepper; }

namespace nodegraph {

using OptStr = std::optional<QString>;
using blockcode::PItem;
using blockcode::PList;
using blockcode::PPtr;

extern OrderedMap<QString, QString> COLOURS;          // where it is -> colour (changed in place by set_scheme)
extern const OrderedMap<QString, QString> TYPE_NAME;
extern const QString RED;
constexpr double ROW = 24.0, HEAD = 28.0, W_MIN = 200.0, W_MAX = 440.0;
constexpr int BUDGET = 320;                           // about this many nodes are made when an event is opened

long long count_inside(const PItem &item);            // how many lines stand inside a block (at any depth)
extern const QStringList CONTAINERS;                  // blocks that are an input of the entry
extern const OrderedMap<QString, QString> ENTRY_TYPES;
extern const OrderedMap<QString, QStringList> ENTRY_MORE;
extern const QStringList PLAIN;
struct What {
    QString key, name, colour;
};
extern const std::vector<What> WHAT;                  // what a line is — the colour of its head
extern OrderedMap<QString, QString> WHAT_COL;
struct Scheme {
    OrderedMap<QString, QString> where, what;
    QString head, canvas, body, field, grid, ink;
    double line;
    OptStr edge;                                     // a light rim round every box
};
const OrderedMap<QString, Scheme> &SCHEMES();
extern const std::vector<std::pair<QString, QString>> SCHEME_NAMES;
extern Scheme STYLE;
void set_scheme(const QString &name);
QColor _mix(const QColor &a, const QColor &b, double t);
QString what_of(const blockcode::Spec *spec);
QFont _font(bool bold = false, int px = 13);

class NodeWindow;
class NodeItem;

struct Sock {                                         // an input of a node: where its nodes stand in the script
    QString name, type;
    OptStr scope;
    PList *holder;
    OptStr sub;
    std::function<bool(const PItem &)> own;          // (none: every item of the list)
    bool single = false;
    long long count = 0;
    PList *lst(bool make = false) const;
    std::vector<PPtr> members() const;
};

struct Hit {                                          // what of a node is at a place: (what, node, index)
    QString what;
    NodeItem *node;
    std::optional<int> i;
};

class Wire : public QGraphicsPathItem {
public:
    explicit Wire(NodeItem *node);
    NodeItem *node;
    QPainterPath shape() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w = nullptr) override;
};

struct Widget {                                        // a value of a node: (label, item, value type, "v" / "k", issue)
    QString label;
    PPtr item;
    QString vtype, attr;
    OptStr issue;
};

class NodeItem : public QGraphicsItem {
    // kind: entry / option / weight / mod / chance / line (a condition or an effect: mode says which).
public:
    NodeItem(NodeWindow *win, PPtr item, const QString &kind, const QString &mode, const OptStr &scope);
    NodeWindow *win;
    PPtr item;
    QString kind, mode;
    OptStr scope;
    long long uid;                                   // (a node is known by this across a rebuild: Python's `in self.nodes`)
    NodeItem *parent_ = nullptr;
    int psock = -1;                                  // (-1: None)
    Wire *wire = nullptr;
    PList *home = nullptr;
    std::vector<NodeItem *> children;
    bool folded = false;
    long long inside = 0;
    int depth = 0;
    const blockcode::Spec *spec = nullptr;
    std::vector<Widget> widgets;
    std::vector<Sock> socks;
    QStringList missing;
    bool wrong = false;
    OptStr what;
    QString title;
    double w = 0, h = 0;

    QString type_() const;
    void describe();
    void _line(const std::vector<PPtr> &rows);
    QString shown(const Widget &wd) const;
    void measure();
    QRectF fold_rect() const;
    QRectF unfold_rect() const;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    double sock_y(int i) const;
    QPointF in_dot(int i) const;
    QPointF out_dot() const;
    QRectF add_rect(int i) const;
    QRectF pill_rect(int j) const;
    QRectF more_rect() const;
    std::optional<Hit> hit(const QPointF &p);
    static void _plus(QPainter *p, const QRectF &r);
    void paint(QPainter *p, const QStyleOptionGraphicsItem *o, QWidget *w = nullptr) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
};

class Canvas : public QGraphicsView {
public:
    explicit Canvas(NodeWindow *win);
    NodeWindow *win;
    std::optional<QPointF> _pan, _pan_from;
    std::optional<std::pair<std::optional<Hit>, QPoint>> _press;
    std::vector<NodeItem *> _keep;
    bool _band = false;

protected:
    void drawBackground(QPainter *p, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
};

struct PickData {                                     // what a row of the menu stands for
    QString s;                                       // a word ("option", a field, a value) or the typed text
    QString mode;
    const blockcode::Spec *spec = nullptr;           // (mode, spec): a line of the catalogue
};

class Picker : public QFrame {
    // The menu over the canvas: groups on the left, what is in the picked group on the right; typing a few letters
    // searches all of it. ↑ ↓ and Enter, or a click; Esc closes.
public:
    explicit Picker(QWidget *parent);
    struct Row {
        QString text;
        PickData data;
        OptStr colour, group;
    };
    using Take = std::function<void(const PickData &, bool)>;
    QLabel *head, *count;
    QLineEdit *edit;
    QListWidget *cats, *lst;
    std::vector<Row> rows;
    Take on_take;
    OptStr free;
    void open(const QPoint &vp, const QString &title, const std::vector<Row> &rows, Take on_take,
              const OptStr &free = std::nullopt);
    void fill(const QString &text);
    void _step(int d, std::optional<int> start = std::nullopt);
    bool eventFilter(QObject *obj, QEvent *e) override;
    void take(QListWidgetItem *it);
};

class NodeWindow : public QWidget {
    Q_OBJECT
public:
    explicit NodeWindow(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::unique_ptr<blockcode::Env> env;
    std::optional<std::pair<QString, QString>> entry_id;   // (kind, id or key)
    QString entry_kind = QStringLiteral("event"), entry_scope = QStringLiteral("c");
    OptStr rel;
    std::optional<std::pair<qsizetype, qsizetype>> span;
    long long depth = 0;
    PPtr root;
    NodeItem *root_node = nullptr;
    struct Loose {
        PPtr item;
        QString kind, mode;
        OptStr scope;
    };
    std::vector<Loose> loose;
    std::vector<NodeItem *> nodes;
    QHash<const PItem *, QPointF> pos;
    std::optional<Hit> dragging;
    QGraphicsPathItem *_drag_path = nullptr;
    OptStr _written;
    std::optional<std::pair<long long, int>> _edit_of;     // (node uid, widget)
    QStringList hidden_parts;
    long long fold_depth = 1000000000;
    QSet<const PItem *> opened, closed;
    QHash<const PItem *, NodeItem *> _by_item;
    QString _dump;
    OptStr _called;
    long long _serial = 0;
    PList _kept;                                      // lists taken out while nodes still point into them (Python: alive)

    QLabel *head, *info, *ins_title, *ins_text, *key_lbl;
    QPushButton *add_btn, *side_btn, *follow_btn, *open_btn;
    QMap<QString, QPushButton *> scheme_btn;          // (one look only — high contrast — the user, 2026-10-27)
    Canvas *view;
    QScrollArea *side;
    QWidget *follow_box;
    widgets::Stepper *follow_days, *follow_choices;
    QLineEdit *line;
    Picker *picker;

    QScrollArea *_side_panel();
    void _key();
    void set_colours(const QString &name, bool save = true);
    void _inspect(NodeItem *n);
    void _open_called();
    QStringList _new_ids(int n);
    void follow_up();
    void add_template(const QString &key);
    bool eventFilter(QObject *obj, QEvent *e) override;
    void say(const QString &text, bool bad = false);
    void tell();
    void refresh(document::Document *doc, const events::Entry *entry = nullptr, refs::RefIndex *refs = nullptr,
                 events::EventDB *db = nullptr);
    long long _fold_depth();
    void set_folded(NodeItem *node, bool folded);
    QString _path(NodeItem *node);
    QHash<QString, std::pair<OptStr, QPointF>> _paths();
    void rebuild();
    void fit();
    const std::vector<NodeItem *> &kids(NodeItem *node) { return node->children; }
    void _layout();
    void layout_nodes();
    void route(NodeItem *node);
    static QPainterPath _curve(const QPointF &a, const QPointF &b);
    std::optional<Hit> hit(const QPointF &sp);
    void close_editors();
    NodeItem *node_of(const PItem *item);
    NodeItem *find(long long uid) const;
    void start_wire(const Hit &h);
    void drag_wire(const QPointF &sp);
    void end_wire(const QPointF &sp, const QPoint &vp);
    bool fits(NodeItem *node, const Sock &sock);
    static bool _holds(const PItem &item, const PList *lst);
    void unhook(NodeItem *node);
    bool wire(NodeItem *node, NodeItem *target, int si);
    void act(const Hit &h);
    void add_field(NodeItem *node, const QString &f);
    void edit_value(NodeItem *node, int j);
    void _line_done();
    void set_value(NodeItem *node, int j, const QString &val);
    std::vector<Picker::Row> _spec_rows(const QString &mode, const OptStr &scope, bool blocks_only = false,
                                        const QString &prefix = {});
    Loose _new_line(const PickData &data, bool free, const QString &mode, const OptStr &scope);
    void add_anywhere(const QPointF &sp, const QPoint &vp);
    NodeItem *_pick(const PItem *item);
    PPtr _option();
    void add_for(NodeItem *node, int i, const std::optional<QPointF> &sp, const QPoint &vp);
    void add_around(NodeItem *node, const QPointF &sp, const QPoint &vp);
    void delete_selected();
    QString script();
    void changed(const QString &what, const OptStr &after = std::nullopt,
                 const eventtemplates::Names &names = eventtemplates::Names());
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void scriptNamesEdit(const QString &label, const QString &rel, const QString &text, const eventtemplates::Names &names);
    void entryWanted(const QString &eid);
    void undoRequested();
    void redoRequested();
};

}  // namespace nodegraph
