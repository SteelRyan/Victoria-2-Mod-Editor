// relgraph.py — The relations as a picture: boxes and arrows.
// The word at the cursor (a flag, a modifier, a variable, a nation …) in the middle: on the left the events /
// decisions that set / add / fire it, on the right those that check / clear / use it — each arrow says how.
// The event at the cursor in the middle: what fires it on the left (and what fires those), what it fires on the right
// (and further), and underneath the entries it is most related to, the arrow naming what they share.
// Click a box: go there. Drag: pan. Wheel: zoom.
#pragma once
#include "ui/relview.h"

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QHash>
#include <optional>
#include <tuple>

namespace relgraph {

constexpr double COL = 470.0;    // distance between columns (room for the arrows' labels)
constexpr double ROW = 92.0;     // distance between rows
constexpr int MAX_ROWS = 8;      // boxes in a column before a new column starts further out

using Rgb = std::tuple<int, int, int>;
using Go = std::optional<std::pair<QString, qsizetype>>;

class GNode : public QGraphicsItem {
public:
    GNode(const QString &title, const QString &sub, const Rgb &rgb, const Go &go, bool big = false);
    QString title, sub;
    Rgb rgb;
    Go go;
    bool big;
    double w, h;
    QRectF boundingRect() const override;
    QPointF left() const;
    QPointF right() const;
    QPointF top() const;
    QPointF bottom() const;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;

private:
    bool _hover = false;
};

class RelGraph : public QGraphicsView {
    Q_OBJECT
public:
    explicit RelGraph(QWidget *parent = nullptr);
    QHash<QString, GNode *> _nodes;   // (Python: {key tuple: node}) the key's parts joined by \x1f
    void show_symbol(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target);
    void show_entry(const refs::RefIndex &refs, events::EventDB &db, events::Entry *e, const relview::FiresMap &fires_map);
    void _fit();
    static std::pair<int, double> _column(int n, int i);
Q_SIGNALS:
    void goTo(const QString &rel, long long pos);

protected:
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void _clear();
    GNode *_node(const QString &key, const QString &title, const QString &sub, const Rgb &rgb, const Go &go,
                 const QPointF &pos, bool big = false);
    void _edge(GNode *a, GNode *b, const Rgb &rgb, const QString &label = {}, bool dashed = false, bool vertical = false,
               double label_at = 0.5);
    GNode *_entry_box(events::EventDB &db, events::Entry *e, const QPointF &pos, const QString &sub = {}, bool big = false);
    void _legend(const QString &text);
};

QString entry_key(const events::Entry &e);   // ("e", e.rel, e.uid)

}  // namespace relgraph
