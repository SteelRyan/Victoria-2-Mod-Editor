// relgraph.py (see relgraph.h) — line by line.
#include "ui/relgraph.h"
#include "ui/drawtext.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace relgraph {

static QColor col(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

static Rgb kind_rgb(const QString &k)                // KINDS[k][1]
{
    const auto *kk = refs::KINDS.get(k);
    return kk ? kk->second : Rgb{220, 220, 220};
}

QString entry_key(const events::Entry &e)
{
    return QStringList{u"e"_s, e.rel.value_or(u"\0None"_s), e.uid.value_or(u"\0None"_s)}.join(u'\x1f');
}

// ---------------------------------------------------------------------------------------------- a box
GNode::GNode(const QString &title_, const QString &sub_, const Rgb &rgb_, const Go &go_, bool big_)
    : title(title_), sub(sub_), rgb(rgb_), go(go_), big(big_)
{
    w = big ? 300.0 : 250.0;
    h = big ? 66.0 : 50.0;
    setAcceptHoverEvents(true);
    setToolTip(title + (!sub.isEmpty() ? u"\n"_s + sub : QString()) + (go ? u"\nClick: go there"_s : QString()));
    if (go)
        setCursor(Qt::PointingHandCursor);
    setZValue(2);
}

QRectF GNode::boundingRect() const { return QRectF(-w / 2 - 3, -h / 2 - 3, w + 6, h + 6); }
QPointF GNode::left() const { return pos() + QPointF(-w / 2, 0); }
QPointF GNode::right() const { return pos() + QPointF(w / 2, 0); }
QPointF GNode::top() const { return pos() + QPointF(0, -h / 2); }
QPointF GNode::bottom() const { return pos() + QPointF(0, h / 2); }

void GNode::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const QRectF r(-w / 2, -h / 2, w, h);
    const QColor c = col(rgb);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(QColor(12, 12, 14), 7));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(r, 8, 8);
    p->setPen(QPen(_hover ? c.lighter(130) : c, big ? 4 : 3));
    p->setBrush(!big ? QColor(30, 33, 40) : QColor(46, 40, 26));
    p->drawRoundedRect(r, 8, 8);
    QFont f;
    f.setBold(true);
    f.setPixelSize(big ? 16 : 13);
    p->setFont(f);
    const QFontMetrics fm(f);
    p->setPen(QColor(240, 234, 220));
    const QRectF tr = r.adjusted(10, 5, -10, !sub.isEmpty() ? -(r.height() / 2 - 2) : -5);
    drawtext::draw(*p, tr, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(title, Qt::ElideRight, static_cast<int>(tr.width())));
    if (!sub.isEmpty()) {
        QFont f2;
        f2.setPixelSize(!big ? 12 : 13);
        p->setFont(f2);
        p->setPen(c.lighter(110));
        const QRectF sr = r.adjusted(10, r.height() / 2, -10, -4);
        drawtext::draw(*p, sr, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(f2).elidedText(sub, Qt::ElideRight, static_cast<int>(sr.width())));
    }
}

void GNode::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    _hover = true;
    update();
}

void GNode::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    _hover = false;
    update();
}

void GNode::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    if (go && e->button() == Qt::LeftButton) {
        if (auto *v = qobject_cast<RelGraph *>(scene()->views().value(0)))
            Q_EMIT v->goTo(go->first, go->second);
        e->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(e);
}

// ---------------------------------------------------------------------------------------------- the view
RelGraph::RelGraph(QWidget *parent) : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(16, 19, 24));
    setToolTip(QString());
}

void RelGraph::wheelEvent(QWheelEvent *e)
{
    const double f = std::pow(1.15, e->angleDelta().y() / 120.0);
    scale(f, f);
}

void RelGraph::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    _fit();
}

void RelGraph::_fit()
{
    const QRectF r = scene()->itemsBoundingRect();
    if (r.isEmpty())
        return;
    fitInView(r.adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
    if (transform().m11() > 1.0) {               // never bigger than life
        resetTransform();
        centerOn(r.center());
    } else if (transform().m11() < 0.62) {       // never too small to read: the middle box in view
        resetTransform();
        scale(0.62, 0.62);
        centerOn(QPointF(0, 0));
    }
}

// ---------------------------------------------------------------------------------------------- drawing
void RelGraph::_clear()
{
    scene()->clear();
    _nodes.clear();
}

GNode *RelGraph::_node(const QString &key, const QString &title, const QString &sub, const Rgb &rgb, const Go &go,
                       const QPointF &pos, bool big)
{
    GNode *n = _nodes.value(key);
    if (!n) {
        n = new GNode(title, sub, rgb, go, big);
        n->setPos(pos);
        scene()->addItem(n);
        _nodes.insert(key, n);
    }
    return n;
}

void RelGraph::_edge(GNode *a, GNode *b, const Rgb &rgb, const QString &label, bool dashed, bool vertical, double label_at)
{
    // An arrow from box a to box b (a curve), with its label in the middle.
    QPointF p0, p3, c1, c2;
    if (vertical) {
        p0 = a->bottom();
        p3 = b->top();
        c1 = p0 + QPointF(0, 50);
        c2 = p3 - QPointF(0, 50);
    } else {
        if (b->pos().x() >= a->pos().x()) {
            p0 = a->right();
            p3 = b->left();
        } else {
            p0 = a->left();
            p3 = b->right();
        }
        const double dx = (p3.x() - p0.x()) * 0.5;
        c1 = p0 + QPointF(dx, 0);
        c2 = p3 - QPointF(dx, 0);
    }
    QPainterPath path(p0);
    path.cubicTo(c1, c2, p3);
    const QColor c = col(rgb);
    const std::pair<QColor, double> pens[2] = {{QColor(10, 10, 12), 6.0}, {c, 3.0}};
    for (int i = 0; i < 2; ++i) {
        auto *it = new QGraphicsPathItem(path);
        QPen pen(pens[i].first, pens[i].second);
        if (dashed && i == 1)
            pen.setStyle(Qt::DashLine);
        it->setPen(pen);
        it->setZValue(0);
        scene()->addItem(it);
    }
    // the arrow head
    const QPointF t = path.pointAtPercent(0.97);
    const QPointF d = p3 - t;
    double ln = std::pow(d.x() * d.x() + d.y() * d.y(), 0.5);
    if (ln == 0.0)
        ln = 1.0;
    const double ux = d.x() / ln, uy = d.y() / ln, s = 11.0;
    const QPolygonF head({p3, p3 - QPointF(ux * s - uy * s * 0.55, uy * s + ux * s * 0.55),
                          p3 - QPointF(ux * s + uy * s * 0.55, uy * s - ux * s * 0.55)});
    QPainterPath hp;
    hp.addPolygon(head);
    auto *hi = new QGraphicsPathItem(hp);
    hi->setBrush(c);
    hi->setPen(QPen(QColor(10, 10, 12), 1.5));
    hi->setZValue(1);
    scene()->addItem(hi);
    if (!label.isEmpty()) {
        const QPointF m = path.pointAtPercent(label_at);
        QFont f;
        f.setPixelSize(12);
        f.setBold(true);
        const QFontMetrics fm(f);
        const QString text = fm.elidedText(label, Qt::ElideRight, 200);
        const int w = fm.horizontalAdvance(text) + 12;
        QPainterPath box;
        box.addRoundedRect(QRectF(m.x() - w / 2.0, m.y() - 10, w, 20), 5, 5);
        auto *bi = new QGraphicsPathItem(box);
        bi->setBrush(QColor(20, 22, 27));
        bi->setPen(QPen(c, 1.5));
        bi->setZValue(3);
        bi->setToolTip(label);
        scene()->addItem(bi);
        QGraphicsSimpleTextItem *ti = scene()->addSimpleText(text, f);
        ti->setBrush(QBrush(c.lighter(120)));
        ti->setPos(m.x() - w / 2.0 + 6, m.y() - 9);
        ti->setZValue(4);
    }
}

std::pair<int, double> RelGraph::_column(int n, int i)
{
    // (column step, row y) of the i-th of n boxes on one side (MAX_ROWS a column, then further out)
    const int c = i / MAX_ROWS;
    const int k = std::min(n - c * MAX_ROWS, MAX_ROWS);
    const int row = i % MAX_ROWS;
    // every other column half a row lower: the arrows to the outer ones pass between the inner boxes
    return {c, (row - (k - 1) / 2.0) * ROW + (c % 2 ? ROW / 2.0 : 0.0)};
}

GNode *RelGraph::_entry_box(events::EventDB &db, events::Entry *e, const QPointF &pos, const QString &sub, bool big)
{
    const Rgb rgb = kind_rgb(e->is_event() ? u"event"_s : u"decision"_s);
    return _node(entry_key(*e), relview::entry_name(db, *e), sub, rgb, std::pair{e->rel.value_or(QString()), e->span.first},
                 pos, big);
}

// ---------------------------------------------------------------------------------------------- the pictures
void RelGraph::show_symbol(const refs::RefIndex &refs, events::EventDB &db, const QString &kind, const QString &target)
{
    _clear();
    const auto *kk = refs::KINDS.get(kind);
    const QString label = kk ? kk->first : kind;
    const Rgb rgb = kk ? kk->second : Rgb{200, 200, 200};
    const auto d = refs.definition(kind, target);
    GNode *c = _node(QStringList{u"s"_s, kind, target}.join(u'\x1f'), refs.name_of(kind, target), label, rgb,
                     d ? Go(std::pair{d->first, d->second}) : std::nullopt, QPointF(0, 0), true);
    OrderedMap<events::Entry *, relview::Counter> writers, readers;
    OrderedMap<QString, long long> others;
    for (const auto &[rel, per] : relview::usage_groups(refs, db, kind, target))
        for (const auto &[e, lst] : per)
            for (const auto &[p, role] : lst) {
                if (!e)
                    others[rel] += 1;
                else if (role == u"set" || role == u"add" || role == u"fire" || role == u"change" || role == u"def")
                    writers[e][role] += 1;
                else
                    readers[e][role] += 1;
            }
    auto lab = [](const relview::Counter &cnt) {
        std::vector<std::pair<QString, long long>> items(cnt.begin(), cnt.end());
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return -a.second < -b.second; });
        QStringList out;
        for (const auto &[r, n] : items)
            out << (n > 1 ? u"%1 ×%2"_s.arg(relview::SHORT.value(r, r)).arg(n) : relview::SHORT.value(r, r));
        return out.join(u" · "_s);
    };
    const std::pair<OrderedMap<events::Entry *, relview::Counter> *, int> sides[2] = {{&writers, -1}, {&readers, 1}};
    for (const auto &[group, sign] : sides) {
        std::vector<std::tuple<long long, QString, events::Entry *, const relview::Counter *>> items;
        for (const auto &[e, cnt] : *group) {
            long long tot = 0;
            for (const auto &[r, n] : cnt)
                tot += n;
            items.emplace_back(tot, db.name(*e), e, &cnt);
        }
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
            return std::get<0>(a) != std::get<0>(b) ? std::get<0>(a) > std::get<0>(b) : std::get<1>(a) < std::get<1>(b);
        });
        if (items.size() > 48)
            items.resize(48);
        const int n = static_cast<int>(items.size());
        for (int i = 0; i < n; ++i) {
            events::Entry *e = std::get<2>(items[static_cast<size_t>(i)]);
            const relview::Counter &cnt = *std::get<3>(items[static_cast<size_t>(i)]);
            const auto [cl, y] = _column(n, i);
            GNode *box = _entry_box(db, e, QPointF(sign * COL * (1 + cl), y));
            const double near_ = cl ? 0.08 : 0.2;     // the label next to its own box
            if (sign < 0)
                _edge(box, c, kind_rgb(e->is_event() ? u"event"_s : u"decision"_s), lab(cnt), false, false, near_);
            else
                _edge(c, box, rgb, lab(cnt), false, false, 1.0 - near_);
        }
    }
    long long n_others = 0;
    for (const auto &[r, k] : others)
        n_others += k;
    _legend(u"%1 “%2” — left: what sets / adds / fires it (%3) · right: what checks / clears / uses it (%4)%5"_s.arg(label, target)
                .arg(writers.size())
                .arg(readers.size())
                .arg(!others.isEmpty() ? u" · + %1 mentions outside events"_s.arg(n_others) : QString()));
    _fit();
}

void RelGraph::show_entry(const refs::RefIndex &refs, events::EventDB &db, events::Entry *e, const relview::FiresMap &fires_map)
{
    _clear();
    GNode *c = _entry_box(db, e, QPointF(0, 0), e->is_event() ? u"the event at the cursor"_s : u"the decision at the cursor"_s,
                          true);
    auto fired = [&db](events::Entry *x) {
        std::vector<events::Fired> out;
        if (x->is_event())
            if (const auto *lst = db.fired_by.get(x->id.value_or(events::NO_ID)))
                out = *lst;
        return out;
    };
    // what fires it (and what fires those)
    std::vector<events::Fired> ups;
    for (const events::Fired &f : fired(e))
        if (f.src)
            ups.push_back(f);
    if (ups.size() > 24)
        ups.resize(24);
    for (int i = 0; i < static_cast<int>(ups.size()); ++i) {
        const auto [cl, y] = _column(static_cast<int>(ups.size()), i);
        GNode *b = _entry_box(db, ups[static_cast<size_t>(i)].src, QPointF(-COL * (1 + cl), y));
        _edge(b, c, kind_rgb(u"event"_s), ups[static_cast<size_t>(i)].how, false, false, cl ? 0.1 : 0.25);
    }
    int gcount = 0;
    for (size_t k = 0; k < ups.size() && k < 6; ++k) {
        events::Entry *s = ups[k].src;
        GNode *src = _nodes.value(entry_key(*s));
        const auto fs = fired(s);
        for (size_t j = 0; j < fs.size() && j < 3; ++j) {
            events::Entry *g = fs[j].src;
            if (!g || g == e || _nodes.contains(entry_key(*g)))
                continue;
            GNode *b = _entry_box(db, g, QPointF(src->pos().x() - COL, src->pos().y() + (gcount % 3 - 1) * 46));
            _edge(b, src, Rgb{150, 140, 110}, fs[j].how, false, false, 0.3);
            ++gcount;
        }
    }
    // what it fires (and further)
    auto fires_of = [&fires_map](const events::Entry *x) {
        const auto it = fires_map.find(x);
        return it != fires_map.end() ? *it : std::vector<std::pair<QString, QString>>{};
    };
    std::vector<std::pair<events::Entry *, QString>> downs;
    for (const auto &[t, how] : fires_of(e))
        if (events::Entry *const *y = db.by_id.get(t))
            downs.emplace_back(*y, how);
    if (downs.size() > 24)
        downs.resize(24);
    for (int i = 0; i < static_cast<int>(downs.size()); ++i) {
        const auto [cl, y] = _column(static_cast<int>(downs.size()), i);
        GNode *b = _entry_box(db, downs[static_cast<size_t>(i)].first, QPointF(COL * (1 + cl), y));
        _edge(c, b, kind_rgb(u"event"_s), downs[static_cast<size_t>(i)].second, false, false, cl ? 0.9 : 0.75);
    }
    gcount = 0;
    for (size_t k = 0; k < downs.size() && k < 6; ++k) {
        events::Entry *y_ = downs[k].first;
        GNode *src = _nodes.value(entry_key(*y_));
        const auto fs = fires_of(y_);
        for (size_t j = 0; j < fs.size() && j < 3; ++j) {
            events::Entry *const *zp = db.by_id.get(fs[j].first);
            events::Entry *z = zp ? *zp : nullptr;
            if (!z || z == e || _nodes.contains(entry_key(*z)))
                continue;
            GNode *b = _entry_box(db, z, QPointF(src->pos().x() + COL, src->pos().y() + (gcount % 3 - 1) * 46));
            _edge(src, b, Rgb{150, 140, 110}, fs[j].second, false, false, 0.7);
            ++gcount;
        }
    }
    // related through what they share, underneath
    std::vector<std::pair<events::Entry *, relview::Shared>> rel_items;
    for (const auto &item : relview::related_entries(refs, db, e))
        if (!_nodes.contains(entry_key(*item.first)))
            rel_items.push_back(item);
    if (rel_items.size() > 7)
        rel_items.resize(7);
    double low = 0;
    for (GNode *n : _nodes)
        low = std::max(low, n->pos().y());
    low += 220;
    for (int i = 0; i < static_cast<int>(rel_items.size()); ++i) {
        const auto &[o, syms] = rel_items[static_cast<size_t>(i)];
        const double x = (i - (static_cast<double>(rel_items.size()) - 1) / 2.0) * 330;
        GNode *b = _entry_box(db, o, QPointF(x, low + (i % 2) * 90));
        QStringList names;
        for (const auto &[k, v] : syms) {
            if (names.size() == 2)
                break;
            names << k.second;
        }
        _edge(c, b, Rgb{130, 200, 170},
              u"shares %1: %2"_s.arg(syms.size()).arg(names.join(u", "_s) + (syms.size() > 2 ? u" …"_s : QString())), true,
              true, 0.75);
    }
    _legend(u"left: what fires it (%1) · right: what it fires (%2) · below, dashed: the most related (what they share)"_s
                .arg(ups.size())
                .arg(downs.size()));
    _fit();
}

void RelGraph::_legend(const QString &text)
{
    QFont f;
    f.setPixelSize(13);
    const QRectF r = scene()->itemsBoundingRect();
    QGraphicsSimpleTextItem *t = scene()->addSimpleText(text, f);
    t->setBrush(QBrush(QColor(170, 160, 140)));
    t->setPos(r.left(), r.top() - 40);
}

}  // namespace relgraph
