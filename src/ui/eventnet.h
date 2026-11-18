// eventnet.py — The event network — a screen of its own: every event (by its id) and every decision as a box, and
// coloured lines for how they hang together: one calls the other, one sets a flag / adds a modifier / changes a number
// the other asks for; for the one picked also which others speak of the same nations and which share its file. Laid
// out by file (boxes side by side) or by connection (what calls what, in chains, left to right). A click picks one —
// its lines are lit, the rest fades, and the panel on the right lists every relation; a double-click opens it in
// Scripts.
//
// (Python walks sets of entries in _by_chain; a set of objects goes by their addresses, an order that changes from
// run to run even in Python. The port walks them in the order they were put in — tools/check_eventnet.py gives
// Python's module the same kind of set.)
#pragma once
#include "core/events.h"
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QDialog>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QHash>
#include <QSet>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QFont;
class QGraphicsPathItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace refs {
class RefIndex;
}

namespace eventnet {

using events::Entry;
using Rgb3 = std::tuple<int, int, int>;

inline constexpr int NW = 150, NH = 44;    // a box
struct Kind {
    QString key, text;
    Rgb3 rgb;
};
extern const std::vector<Kind> KINDS;
extern const OrderedMap<QString, Rgb3> KIND_RGB, NODE_RGB;
extern const OrderedMap<QString, QString> SYM_KIND;
extern const QSet<QString> GIVE, TAKE;
inline constexpr int CAP = 60;             // a flag used by more pairs than this isn't drawn pair by pair

struct Edge {                              // (a, b, kind, what): a gives / calls, b takes / is called
    Entry *a, *b;
    QString kind, what;
};

class Graph {
public:
    Graph(events::EventDB &db, refs::RefIndex *refs, const std::function<QString(const Entry &)> &name_of);
    events::EventDB &db;
    refs::RefIndex *refs;
    std::vector<Entry *> nodes;
    QHash<const Entry *, QString> name;
    std::vector<Edge> edges;
    std::vector<std::tuple<QString, QString, long long>> skipped;   // (kind, name, pairs) too widely used to draw
    OrderedMap<Entry *, QSet<QString>> tags;                        // entry -> {nation tags it speaks of}

    struct Around {
        Entry *other;
        QString kind, what;
        bool outgoing;
    };
    std::vector<Around> around(const Entry *e, const QSet<QString> &kinds) const;
    std::vector<std::pair<Entry *, QString>> same_tags(const Entry *e, int cap = 40) const;
    std::vector<Entry *> same_file(const Entry *e) const;
};

class NetView;

class NodeItem : public QGraphicsItem {
public:
    NodeItem(NetView *view, Entry *e, const QString &name);
    NetView *view;
    Entry *e;
    QString name;
    QString kind, head, title_;              // (kept: the box paints without its entry)
    bool dim = false;
    int mark = 0;                            // 0 plain, 1 a neighbour of the one picked, 2 the one picked
    QString title() const { return title_; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w = nullptr) override;
    QPointF centre() const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override;
};

class NetView : public QGraphicsView {
    Q_OBJECT
public:
    explicit NetView(QWidget *parent = nullptr);
    Graph *graph = nullptr;
    OrderedMap<Entry *, NodeItem *> items;                          // entry -> NodeItem
    struct Line {
        QGraphicsPathItem *path;
        Entry *a, *b;
        QString kind;
    };
    std::vector<Line> lines;
    std::vector<QGraphicsPathItem *> extra;  // the lines of the one picked (tags, file)
    Entry *sel = nullptr;
    std::optional<std::pair<QString, QString>> sel_key;   // (rel, uid) of the one picked (its db may be gone)
    QSet<QString> kinds{QStringLiteral("fires"), QStringLiteral("flag"), QStringLiteral("modifier"), QStringLiteral("var")};
    QString layout_mode = QStringLiteral("file");
    bool only_linked = false;

    void fit();
    void set_graph(Graph *g);
    std::vector<Edge> _active_edges() const;
    void rebuild(bool keep_view = false);
    QHash<Entry *, QPointF> _by_file(const std::vector<Entry *> &nodes);
    QHash<Entry *, QPointF> _by_chain(const std::vector<Entry *> &nodes, const std::vector<Edge> &edges);
    QGraphicsPathItem *_line(Entry *a, Entry *b, const QString &kind, const QString &what, double z = 1,
                             double width = 2.0, int alpha = 110);
    void pick(Entry *e, bool centre = false);
    void _light();
Q_SIGNALS:
    void picked(events::Entry *e);
    void opened(events::Entry *e);

protected:
    void wheelEvent(QWheelEvent *e) override;
};

class EventNet : public QDialog {
    Q_OBJECT
public:
    explicit EventNet(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    std::unique_ptr<Graph> graph;
    std::optional<std::pair<long long, long long>> _sig;
    QLabel *count, *head, *meta, *note;
    QComboBox *layout_box;
    QCheckBox *only;
    QLineEdit *find;
    OrderedMap<QString, QCheckBox *> kind_box;
    NetView *view;
    QPushButton *open_btn, *block_btn;
    QTreeWidget *tree;

    void set_data(events::EventDB *db, refs::RefIndex *refs, const std::function<QString(const Entry &)> &name_of);
    void _counts();
    void _relayout();
    void _kinds_changed();
    void _find();
    void _picked(Entry *e);
    void _group(const QString &title, const QString &kind, const std::vector<std::pair<Entry *, QString>> &rows,
                const QFont &bold, bool open_ = true);
    void _tree_clicked(QTreeWidgetItem *it, int col);
    void _open(Entry *e);
Q_SIGNALS:
    void openEntry(const QString &rel, long long pos);   // show it in Scripts
    void blocksRequested(events::Entry *e);              // the entry: open it in the block builder

protected:
    void keyPressEvent(QKeyEvent *e) override;
};

}  // namespace eventnet
