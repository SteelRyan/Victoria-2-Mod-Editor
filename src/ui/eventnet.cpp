// eventnet.py — the event network. Line by line.
#include "ui/eventnet.h"
#include "ui/drawtext.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/refs.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace eventnet {

const std::vector<Kind> KINDS = {
    {u"fires"_s, u"calls it (fires the event)"_s, {240, 200, 90}},
    {u"flag"_s, u"sets a flag the other asks for"_s, {90, 220, 190}},
    {u"modifier"_s, u"adds a modifier the other asks for"_s, {200, 140, 255}},
    {u"var"_s, u"changes a number the other asks for"_s, {120, 210, 120}},
    {u"tag"_s, u"speaks of the same nations (of the one picked)"_s, {255, 125, 200}},
    {u"file"_s, u"is in the same file (of the one picked)"_s, {150, 160, 175}}};
static OrderedMap<QString, Rgb3> kind_rgb()
{
    OrderedMap<QString, Rgb3> out;
    for (const Kind &k : KINDS)
        out.insert(k.key, k.rgb);
    return out;
}
const OrderedMap<QString, Rgb3> KIND_RGB = kind_rgb();
const OrderedMap<QString, Rgb3> NODE_RGB = {{u"country_event"_s, {201, 161, 90}},
                                            {u"province_event"_s, {90, 170, 235}},
                                            {u"decision"_s, {240, 140, 70}}};
const OrderedMap<QString, QString> SYM_KIND = {{u"cflag"_s, u"flag"_s}, {u"gflag"_s, u"flag"_s}, {u"pflag"_s, u"flag"_s},
                                               {u"modifier"_s, u"modifier"_s}, {u"var"_s, u"var"_s}};
const QSet<QString> GIVE = {u"set"_s, u"add"_s, u"change"_s};
const QSet<QString> TAKE = {u"check"_s, u"remove"_s, u"clear"_s};

static QString kind_text(const QString &k)
{
    for (const Kind &x : KINDS)
        if (x.key == k)
            return x.text;
    return {};
}

static QString rel_of(const Entry *e) { return e->rel.value_or(u"None"_s); }   // str(e.rel)

// ---------------------------------------------------------------------------------------------- the graph
Graph::Graph(events::EventDB &db_, refs::RefIndex *refs_, const std::function<QString(const Entry &)> &name_of)
    : db(db_), refs(refs_)
{
    for (const auto &ep : db.entries) {
        Entry *e = ep.get();
        if (e->is_event() ? (e->id && !e->id->isEmpty()) : !e->key.isEmpty())
            nodes.push_back(e);
    }
    for (Entry *e : nodes)
        name.insert(e, name_of(*e));
    std::set<std::pair<Entry *, Entry *>> seen;
    for (const auto &[target, lst] : db.fired_by) {
        Entry *const *bp = db.by_id.get(target);
        if (!bp || !*bp)
            continue;
        Entry *b = *bp;
        for (const events::Fired &f : lst)
            if (f.src && f.src != b && !seen.count({f.src, b})) {
                seen.insert({f.src, b});
                edges.push_back({f.src, b, u"fires"_s, f.how});
            }
    }
    if (!refs)
        return;
    for (const auto &[kt, lst] : refs->uses) {
        const auto &[kind, target] = kt;
        if (kind == u"nation") {
            for (const refs::Use &u : lst)
                if (Entry *e = db.entry_at(u.rel, u.pos))
                    tags[e].insert(target);
            continue;
        }
        const QString *k = SYM_KIND.get(kind);
        if (!k)
            continue;
        std::vector<Entry *> give, take;
        for (const refs::Use &u : lst) {
            Entry *e = db.entry_at(u.rel, u.pos);
            if (!e)
                continue;
            if (GIVE.contains(u.role) && std::find(give.begin(), give.end(), e) == give.end())
                give.push_back(e);
            if (TAKE.contains(u.role) && std::find(take.begin(), take.end(), e) == take.end())
                take.push_back(e);
        }
        std::vector<std::pair<Entry *, Entry *>> pairs;
        for (Entry *a : give)
            for (Entry *b : take)
                if (a != b)
                    pairs.emplace_back(a, b);
        if (pairs.size() > size_t(CAP)) {
            skipped.emplace_back(*k, target, static_cast<long long>(pairs.size()));
            continue;
        }
        for (const auto &[a, b] : pairs)
            edges.push_back({a, b, *k, target});
    }
}

std::vector<Graph::Around> Graph::around(const Entry *e, const QSet<QString> &kinds) const
{
    // [(other, kind, what, outgoing?)] of an entry's lines.
    std::vector<Around> out;
    for (const Edge &ed : edges)
        if (kinds.contains(ed.kind)) {
            if (ed.a == e)
                out.push_back({ed.b, ed.kind, ed.what, true});
            else if (ed.b == e)
                out.push_back({ed.a, ed.kind, ed.what, false});
        }
    return out;
}

std::vector<std::pair<Entry *, QString>> Graph::same_tags(const Entry *e, int cap) const
{
    const QSet<QString> *mine = tags.get(const_cast<Entry *>(e));
    if (!mine || mine->isEmpty())
        return {};
    struct Got {
        Entry *o;
        qsizetype n;
        QStringList tags;
    };
    std::vector<Got> got;
    for (const auto &[o, t] : tags) {
        if (o == e)
            continue;
        QSet<QString> both = *mine;
        both.intersect(t);
        if (both.isEmpty())
            continue;
        QStringList l(both.begin(), both.end());
        std::sort(l.begin(), l.end());
        got.push_back({o, both.size(), l});
    }
    std::stable_sort(got.begin(), got.end(), [](const Got &x, const Got &y) { return -x.n < -y.n; });
    std::vector<std::pair<Entry *, QString>> out;
    for (size_t i = 0; i < got.size() && i < size_t(cap); ++i)
        out.emplace_back(got[i].o, got[i].tags.mid(0, 4).join(u", "_s));
    return out;
}

std::vector<Entry *> Graph::same_file(const Entry *e) const
{
    std::vector<Entry *> out;
    for (Entry *o : nodes)
        if (o->rel == e->rel && o != e)
            out.push_back(o);
    return out;
}

// ---------------------------------------------------------------------------------------------- a box
NodeItem::NodeItem(NetView *view_, Entry *e_, const QString &name_) : view(view_), e(e_), name(name_)
{
    setAcceptHoverEvents(true);
    setZValue(2);
    kind = e->kind;
    head = e->is_event() ? e->id.value_or(u"None"_s) : u"decision"_s;
    title_ = e->is_event() ? u"%1 — %2"_s.arg(e->id.value_or(u"None"_s), name) : name;
    setToolTip(u"%1\n%2"_s.arg(title_, rel_of(e)));
}

QRectF NodeItem::boundingRect() const { return QRectF(-3, -3, NW + 6, NH + 6); }

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const auto [r, g, b] = NODE_RGB.value(kind, Rgb3{160, 160, 160});
    p->setRenderHint(QPainter::Antialiasing);
    p->setOpacity(dim ? 0.22 : 1.0);
    p->setPen(QPen(mark == 2 ? QColor(u"#ffffff"_s) : QColor(r, g, b), mark ? 4 : 2));
    p->setBrush(QColor(int(py::trunc_int(r * 0.30)), int(py::trunc_int(g * 0.30)), int(py::trunc_int(b * 0.30))));
    p->drawRoundedRect(QRectF(0, 0, NW, NH), 7, 7);
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(13);
    f.setBold(true);
    p->setFont(f);
    p->setPen(QColor(r, g, b).lighter(140));
    drawtext::draw(*p, QRectF(7, 3, NW - 14, 17), Qt::AlignLeft | Qt::AlignVCenter, head);
    f.setBold(false);
    f.setPixelSize(12);
    p->setFont(f);
    p->setPen(QColor(theme::INK));
    drawtext::draw(*p, QRectF(7, 21, NW - 14, 18), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(f).elidedText(name, Qt::ElideRight, NW - 14));
}

QPointF NodeItem::centre() const { return pos() + QPointF(NW / 2.0, NH / 2.0); }

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        view->pick(e);
        ev->accept();
    }
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *) { Q_EMIT view->opened(e); }

// ---------------------------------------------------------------------------------------------- the picture
NetView::NetView(QWidget *parent) : QGraphicsView(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QColor(u"#0d1014"_s));
}

void NetView::wheelEvent(QWheelEvent *e)
{
    const double k = e->angleDelta().y() > 0 ? 1.18 : 1 / 1.18;
    const double now = transform().m11();
    if (0.05 < now * k && now * k < 4.0)
        scale(k, k);
}

void NetView::fit()
{
    const QRectF r = scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40);
    scene()->setSceneRect(r.adjusted(-600, -400, 600, 400));
    fitInView(r, Qt::KeepAspectRatio);
}

void NetView::set_graph(Graph *g)
{
    graph = g;
    rebuild();
}

std::vector<Edge> NetView::_active_edges() const
{
    std::vector<Edge> out;
    for (const Edge &e : graph->edges)
        if (kinds.contains(e.kind))
            out.push_back(e);
    return out;
}

void NetView::rebuild(bool keep_view)
{
    QGraphicsScene *sc = scene();
    sc->clear();
    items.clear();
    lines.clear();
    extra.clear();
    Graph *g = graph;
    if (!g)
        return;
    const std::vector<Edge> edges = _active_edges();
    QSet<Entry *> linked;
    for (const Edge &e : edges) {
        linked.insert(e.a);
        linked.insert(e.b);
    }
    std::vector<Entry *> nodes;
    for (Entry *e : g->nodes)
        if (!only_linked || linked.contains(e))
            nodes.push_back(e);
    const QHash<Entry *, QPointF> pos = layout_mode == u"file" ? _by_file(nodes) : _by_chain(nodes, edges);
    for (Entry *e : nodes) {
        auto *it = new NodeItem(this, e, g->name.value(e));
        it->setPos(pos.value(e));
        sc->addItem(it);
        items.insert(e, it);
    }
    for (const Edge &ed : edges)
        if (items.contains(ed.a) && items.contains(ed.b))
            lines.push_back({_line(ed.a, ed.b, ed.kind, ed.what), ed.a, ed.b, ed.kind});
    if (!sel || !items.contains(sel)) {
        sel = nullptr;
        sel_key.reset();
    }
    _light();
    if (!keep_view)
        fit();
}

QHash<Entry *, QPointF> NetView::_by_file(const std::vector<Entry *> &nodes)
{
    // Boxes of files side by side (rows that wrap), the entries inside in a grid.
    OrderedMap<QString, std::vector<Entry *>> by;
    for (Entry *e : nodes)
        by[rel_of(e)].push_back(e);
    QHash<Entry *, QPointF> pos;
    double x = 0.0, y = 0.0, row_h = 0.0;
    size_t total = 0;
    for (const auto &kv : by)
        total += kv.second.size();
    const double wrap = std::max(2400.0, std::sqrt(double(std::max<size_t>(1, total))) * (NW + 30) * 1.9);
    QFont f(u"Segoe UI"_s);
    f.setPixelSize(20);
    f.setBold(true);
    QStringList rels;
    for (const auto &kv : by)
        rels << kv.first;
    std::stable_sort(rels.begin(), rels.end(), [](const QString &a, const QString &b) {
        const bool ea = a.split(u'/')[0] != u"events", eb = b.split(u'/')[0] != u"events";
        if (ea != eb)
            return ea < eb;
        return py::lower(a) < py::lower(b);
    });
    for (const QString &rel : rels) {
        std::vector<Entry *> lst = *by.get(rel);
        auto num = [](const Entry *e) -> long long {
            return e->is_event() && e->id && py::isdigit(*e->id) ? py::to_int(*e->id) : 0;
        };
        std::stable_sort(lst.begin(), lst.end(), [&](const Entry *a, const Entry *b) {
            const long long na = num(a), nb = num(b);
            if (na != nb)
                return na < nb;
            return a->key < b->key;
        });
        const long long cols = std::max(1LL, py::trunc_int(std::ceil(std::sqrt(double(lst.size()) * 1.4))));
        const long long rows = py::trunc_int(std::ceil(double(lst.size()) / double(cols)));
        const long long w = cols * (NW + 16) + 24;
        const long long h = rows * (NH + 14) + 58;
        if (x > 0 && x + double(w) > wrap) {
            y = y + row_h + 50;
            x = 0.0;
            row_h = 0.0;
        }
        auto *box = new QGraphicsRectItem(QRectF(x, y, double(w), double(h)));
        box->setPen(QPen(QColor(theme::LINE), 2));
        box->setBrush(QColor(24, 28, 35));
        box->setZValue(0);
        scene()->addItem(box);
        auto *t = new QGraphicsSimpleTextItem(py::split(rel, u"/"_s, 1).value(1));
        t->setFont(f);
        t->setBrush(QColor(rel.startsWith(u"events/") ? theme::GOLD : u"#f08c46"_s));
        t->setPos(x + 12, y + 8);
        t->setZValue(1);
        scene()->addItem(t);
        for (size_t i = 0; i < lst.size(); ++i) {
            const long long r = py::floordiv(static_cast<long long>(i), cols), c = py::mod(static_cast<long long>(i), cols);
            pos.insert(lst[i], QPointF(x + 16 + double(c * (NW + 16)), y + 46 + double(r * (NH + 14))));
        }
        x += double(w + 50);
        row_h = std::max(row_h, double(h));
    }
    return pos;
}

// A set of entries walked in the order they were put in (see the header).
struct OSet {
    std::vector<Entry *> order;
    QSet<Entry *> in;
    void add(Entry *e)
    {
        if (!in.contains(e)) {
            in.insert(e);
            order.push_back(e);
        }
    }
};

QHash<Entry *, QPointF> NetView::_by_chain(const std::vector<Entry *> &nodes, const std::vector<Edge> &edges)
{
    // What hangs together stands together: each group in layers left to right (callers before the called), the
    // groups under each other, the unconnected in a grid at the end.
    const QSet<Entry *> nset(nodes.begin(), nodes.end());
    QHash<Entry *, OSet> adj, out, inc;
    for (const Edge &ed : edges)
        if (nset.contains(ed.a) && nset.contains(ed.b)) {
            adj[ed.a].add(ed.b);
            adj[ed.b].add(ed.a);
            out[ed.a].add(ed.b);
            inc[ed.b].add(ed.a);
        }
    QSet<Entry *> seen;
    std::vector<std::vector<Entry *>> comps;
    for (Entry *e : nodes) {
        if (seen.contains(e) || !adj.contains(e))
            continue;
        std::vector<Entry *> comp, stack{e};
        seen.insert(e);
        while (!stack.empty()) {
            Entry *n = stack.back();
            stack.pop_back();
            comp.push_back(n);
            const auto it = adj.constFind(n);
            if (it != adj.constEnd())
                for (Entry *o : it->order)
                    if (!seen.contains(o)) {
                        seen.insert(o);
                        stack.push_back(o);
                    }
        }
        comps.push_back(comp);
    }
    std::stable_sort(comps.begin(), comps.end(),
                     [](const auto &a, const auto &b) { return -qsizetype(a.size()) < -qsizetype(b.size()); });
    QHash<Entry *, QPointF> pos;
    double y = 0.0;
    for (const auto &comp : comps) {
        const QSet<Entry *> cs(comp.begin(), comp.end());
        QHash<Entry *, long long> layer;
        std::vector<Entry *> roots;
        for (Entry *n : comp) {
            const auto it = inc.constFind(n);
            bool any = false;
            if (it != inc.constEnd())
                for (Entry *o : it->order)
                    if (cs.contains(o)) {
                        any = true;
                        break;
                    }
            if (!any)
                roots.push_back(n);
        }
        if (roots.empty())
            roots.push_back(comp[0]);
        std::deque<Entry *> todo(roots.begin(), roots.end());
        for (Entry *n : roots)
            layer.insert(n, 0);
        long long guard = 0;
        const long long ncomp = static_cast<long long>(comp.size());
        while (!todo.empty() && guard < 20000) {
            ++guard;
            Entry *n = todo.front();
            todo.pop_front();
            const auto it = out.constFind(n);
            if (it == out.constEnd())
                continue;
            for (Entry *o : it->order) {
                const long long ln = layer.value(n);
                if (cs.contains(o) && layer.value(o, -1) < ln + 1 && ln + 1 <= ncomp) {
                    if (layer.contains(o) && ln + 1 > 30)
                        continue;
                    layer.insert(o, ln + 1);
                    todo.push_back(o);
                }
            }
        }
        for (Entry *n : comp)
            if (!layer.contains(n))
                layer.insert(n, 0);
        std::map<long long, std::vector<Entry *>> cols;
        for (Entry *n : comp)
            cols[layer.value(n)].push_back(n);
        size_t tall = 0;
        for (const auto &kv : cols)
            tall = std::max(tall, kv.second.size());
        for (auto &[li, col0] : cols) {
            std::vector<Entry *> col = col0;
            std::stable_sort(col.begin(), col.end(), [](const Entry *a, const Entry *b) {
                const QString ra = rel_of(a), rb = rel_of(b);
                if (ra != rb)
                    return ra < rb;
                return a->key < b->key;
            });
            const double off = double((tall - col.size()) * (NH + 16)) / 2.0;
            for (size_t i = 0; i < col.size(); ++i)
                pos.insert(col[i], QPointF(double(li * (NW + 90)), y + off + double(i * (NH + 16))));
        }
        y += double(tall * (NH + 16) + 70);
    }
    std::vector<Entry *> rest;
    for (Entry *e : nodes)
        if (!pos.contains(e))
            rest.push_back(e);
    const long long cols = std::max(1LL, py::trunc_int(std::ceil(std::sqrt(double(rest.size()) * 2.2))));
    for (size_t i = 0; i < rest.size(); ++i) {
        const long long r = py::floordiv(static_cast<long long>(i), cols), c = py::mod(static_cast<long long>(i), cols);
        pos.insert(rest[i], QPointF(double(c * (NW + 16)), y + 40 + double(r * (NH + 14))));
    }
    return pos;
}

QGraphicsPathItem *NetView::_line(Entry *a, Entry *b, const QString &kind, const QString &what, double z, double width,
                                  int alpha)
{
    const QPointF pa = (*items.get(a))->centre(), pb = (*items.get(b))->centre();
    const auto [r, g, bl] = KIND_RGB.value(kind);
    QPainterPath path(pa);
    const double dx = pb.x() - pa.x(), dy = pb.y() - pa.y();
    const double bend = std::min(120.0, py::hypot(dx, dy) * 0.18);
    const QPointF mid((pa.x() + pb.x()) / 2.0 - dy * 0.0, (pa.y() + pb.y()) / 2.0 - bend);
    path.quadTo(mid, pb);
    if (kind == u"fires" || kind == u"flag" || kind == u"modifier" || kind == u"var") {   // an arrow head at the taker's edge
        const double t = 0.5;
        const QPointF pt = path.pointAtPercent(t);
        const double ang = -path.angleAtPercent(t) * (3.141592653589793 / 180.0);
        const double s = 9.0;
        const QPolygonF head({pt + QPointF(std::cos(ang) * s, std::sin(ang) * s),
                              pt + QPointF(std::cos(ang + 2.5) * s, std::sin(ang + 2.5) * s),
                              pt + QPointF(std::cos(ang - 2.5) * s, std::sin(ang - 2.5) * s)});
        path.addPolygon(head);
        path.closeSubpath();
    }
    auto *it = new QGraphicsPathItem(path);
    QPen pen(QColor(r, g, bl, alpha), width);
    if (kind == u"tag" || kind == u"file")
        pen.setStyle(Qt::DashLine);
    it->setPen(pen);
    it->setZValue(z);
    it->setToolTip(u"%1: %2"_s.arg(kind_text(kind), what));
    scene()->addItem(it);
    return it;
}

void NetView::pick(Entry *e, bool centre)
{
    sel = e && items.contains(e) ? e : nullptr;
    if (sel && sel->uid)
        sel_key = std::pair{rel_of(sel), *sel->uid};
    else
        sel_key.reset();
    _light();
    if (centre && sel)
        centerOn((*items.get(e))->centre());
    Q_EMIT picked(sel);
}

void NetView::_light()
{
    for (QGraphicsPathItem *it : extra) {
        scene()->removeItem(it);
        delete it;
    }
    extra.clear();
    Entry *s = sel;
    QSet<Entry *> near;
    for (Line &ln : lines) {
        const bool on = s && (ln.a == s || ln.b == s);
        const auto [r, g, bl] = KIND_RGB.value(ln.kind);
        if (on)
            near.insert(ln.a == s ? ln.b : ln.a);
        ln.path->setPen(QPen(QColor(r, g, bl, on ? 255 : (s ? 26 : 110)), on ? 4.0 : 2.0));
        ln.path->setZValue(on ? 1.5 : 1);
    }
    if (s && graph) {
        if (kinds.contains(u"tag"_s))
            for (const auto &[o, what] : graph->same_tags(s))
                if (items.contains(o)) {
                    extra.push_back(_line(s, o, u"tag"_s, what, 1.4, 3.0, 230));
                    near.insert(o);
                }
        if (kinds.contains(u"file"_s)) {
            const std::vector<Entry *> same = graph->same_file(s);
            for (size_t i = 0; i < same.size() && i < 60; ++i)
                if (items.contains(same[i])) {
                    extra.push_back(_line(s, same[i], u"file"_s, rel_of(s), 1.3, 2.0, 170));
                    near.insert(same[i]);
                }
        }
    }
    for (const auto &[e, it] : items) {
        it->mark = e == s ? 2 : near.contains(e) ? 1 : 0;
        it->dim = s && e != s && !near.contains(e);
        it->update();
    }
}

// ---------------------------------------------------------------------------------------------- the window
EventNet::EventNet(moddata::Mod &mod_, QWidget *parent) : QDialog(parent), mod(mod_)
{
    setWindowFlags(Qt::Window);
    setWindowTitle(u"Event network"_s);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"side"_s);
    auto *bv = new QVBoxLayout(bar);
    bv->setContentsMargins(12, 8, 12, 8);
    bv->setSpacing(6);
    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    row->addWidget(label(u"Event network"_s, u"title"_s, false));
    count = label(QString(), u"faint"_s, false);
    row->addWidget(count, 1);
    row->addWidget(label(u"Laid out"_s, u"sub"_s, false));
    layout_box = new QComboBox();
    layout_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout_box->setFixedWidth(260);
    layout_box->addItem(u"by file (one box a file)"_s, u"file"_s);
    layout_box->addItem(u"by connection (chains, left to right)"_s, u"chain"_s);
    connect(layout_box, &QComboBox::activated, this, [this](int) { _relayout(); });
    row->addWidget(layout_box);
    only = new QCheckBox(u"Only the connected ones"_s);
    connect(only, &QCheckBox::toggled, this, [this](bool) { _relayout(); });
    row->addWidget(only);
    find = new QLineEdit();
    find->setPlaceholderText(u"Find an event id, a name or a decision — Enter goes there"_s);
    find->setFixedWidth(360);
    connect(find, &QLineEdit::returnPressed, this, [this] { _find(); });
    row->addWidget(find);
    const std::vector<std::pair<QString, std::function<void()>>> zoom = {
        {u"＋"_s, [this] { view->scale(1.3, 1.3); }},
        {u"−"_s, [this] { view->scale(1 / 1.3, 1 / 1.3); }},
        {u"Show all"_s, [this] { view->fit(); }}};
    for (const auto &[t, fn] : zoom) {
        auto *b = new QPushButton(t);
        if (t.size() == 1)
            b->setFixedWidth(40);
        const auto f = fn;
        connect(b, &QPushButton::clicked, this, [f] { f(); });
        row->addWidget(b);
    }
    bv->addLayout(row);
    auto *host = new QWidget();
    auto *wr = new widgets::WrapRow(host, 16);
    auto *lb = new QLabel(u"Lines:"_s, host);
    lb->setObjectName(u"sub"_s);
    wr->addWidget(lb);
    for (const Kind &k : KINDS) {
        const auto [r, g, b] = k.rgb;
        auto *cb = new QCheckBox((k.key != u"tag" && k.key != u"file" ? u"━━  "_s : u"╌╌  "_s) + k.text, host);
        cb->setStyleSheet(u"QCheckBox { color: rgb(%1,%2,%3); font-weight: 600; }"_s.arg(r).arg(g).arg(b));
        cb->setChecked(k.key != u"tag" && k.key != u"file");
        connect(cb, &QCheckBox::toggled, this, [this](bool) { _kinds_changed(); });
        wr->addWidget(cb);
        kind_box.insert(k.key, cb);
    }
    for (const auto &[k, t] : {std::pair{u"country_event"_s, u"■ event of a nation"_s},
                               std::pair{u"province_event"_s, u"■ event of a province"_s},
                               std::pair{u"decision"_s, u"■ decision"_s}}) {
        auto *lg = new QLabel(t, host);
        const auto [r, g, b] = NODE_RGB.value(k);
        lg->setStyleSheet(u"color: rgb(%1,%2,%3);"_s.arg(r).arg(g).arg(b));
        wr->addWidget(lg);
    }
    bv->addWidget(host);
    lay->addWidget(bar);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    lay->addWidget(split, 1);
    view = new NetView();
    connect(view, &NetView::picked, this, [this](Entry *e) { _picked(e); });
    connect(view, &NetView::opened, this, [this](Entry *e) { _open(e); });
    split->addWidget(view);
    auto *right = new QWidget();
    right->setObjectName(u"side"_s);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(12, 10, 12, 10);
    rl->setSpacing(6);
    head = new QLabel(u"Click a box"_s);
    head->setWordWrap(true);
    head->setStyleSheet(u"font-size: 15px; font-weight: 700;"_s);
    rl->addWidget(head);
    meta = label(QString(), u"sub"_s);
    rl->addWidget(meta);
    row = new QHBoxLayout();
    open_btn = new QPushButton(u"Show it in Scripts"_s);
    connect(open_btn, &QPushButton::clicked, this, [this] {
        if (view->sel)
            _open(view->sel);
    });
    row->addWidget(open_btn);
    block_btn = new QPushButton(u"🧩 Open it as blocks"_s);
    connect(block_btn, &QPushButton::clicked, this, [this] {
        if (view->sel)
            Q_EMIT blocksRequested(view->sel);
    });
    row->addWidget(block_btn);
    rl->addLayout(row);
    tree = new QTreeWidget();
    tree->setHeaderHidden(true);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setTextElideMode(Qt::ElideRight);
    connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int c) { _tree_clicked(it, c); });
    rl->addWidget(tree, 1);
    note = label(QString(), u"faint"_s);
    rl->addWidget(note);
    rl->addWidget(label(u"Wheel: closer / farther · drag the ground to move · click a box: its lines light up, the "
                        u"list above names every relation (a click there goes to the other one) · double-click: "
                        u"open it in Scripts."_s,
                        u"faint"_s));
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({1240, 400});
    _picked(nullptr);
}

void EventNet::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)
        return;
    QDialog::keyPressEvent(e);
}

// ------------------------------------------------------------------ data
void EventNet::set_data(events::EventDB *db, refs::RefIndex *refs, const std::function<QString(const Entry &)> &name_of)
{
    // The events as read now (built again only when they changed).
    const std::pair<long long, long long> sig{db ? db->serial : 0, refs ? refs->serial : 0};
    if (_sig == sig)
        return;
    _sig = sig;
    const auto key = view->sel_key;                      // (the one picked: its old db may be gone)
    const bool first = view->graph == nullptr;
    graph = std::make_unique<Graph>(*db, refs, name_of);
    view->kinds.clear();
    for (const auto &[k, cb] : kind_box)
        if (cb->isChecked())
            view->kinds.insert(k);
    view->layout_mode = layout_box->currentData().toString();
    view->only_linked = only->isChecked();
    Entry *const *got = key ? db->by_uid.get(*key) : nullptr;
    view->sel = got ? *got : nullptr;
    if (!view->sel)
        view->sel_key.reset();
    view->graph = graph.get();
    view->rebuild(!first);
    _counts();
    _picked(view->sel);
}

void EventNet::_counts()
{
    Graph *g = graph.get();
    std::map<QString, long long> n;
    for (const Edge &e : g->edges)
        ++n[e.kind];
    auto c = [&](const QString &k) { return QString::number(n.count(k) ? n.at(k) : 0); };
    count->setText(u"  %1 events and decisions · %2 call another · %3 flag lines · %4 modifier lines · %5 number lines"_s.arg(
        QString::number(g->nodes.size()), c(u"fires"_s), c(u"flag"_s), c(u"modifier"_s), c(u"var"_s)));
    if (g->skipped.empty()) {
        note->setText(QString());
        return;
    }
    auto sk = g->skipped;
    std::stable_sort(sk.begin(), sk.end(), [](const auto &a, const auto &b) { return -std::get<2>(a) < -std::get<2>(b); });
    QStringList parts;
    for (size_t i = 0; i < sk.size() && i < 8; ++i)
        parts << u"%1 %2 (%3 pairs)"_s.arg(std::get<0>(sk[i]), std::get<1>(sk[i]), QString::number(std::get<2>(sk[i])));
    note->setText(u"Not drawn pair by pair (used by too many): %1."_s.arg(parts.join(u", "_s)));
}

void EventNet::_relayout()
{
    view->layout_mode = layout_box->currentData().toString();
    view->only_linked = only->isChecked();
    view->rebuild();
}

void EventNet::_kinds_changed()
{
    view->kinds.clear();
    for (const auto &[k, cb] : kind_box)
        if (cb->isChecked())
            view->kinds.insert(k);
    view->rebuild(view->layout_mode == u"file" && !view->only_linked);
    _picked(view->sel);
}

void EventNet::_find()
{
    const QString q = py::lower(py::strip(find->text()));
    if (q.isEmpty() || !graph)
        return;
    Graph *g = graph.get();
    Entry *hit = nullptr;
    for (Entry *e : g->nodes)
        if (py::lower(e->id && !e->id->isEmpty() ? *e->id : e->key) == q) {
            hit = e;
            break;
        }
    if (!hit)
        for (Entry *e : g->nodes)
            if (py::lower(g->name.value(e)).contains(q) || py::lower(e->key).contains(q)) {
                hit = e;
                break;
            }
    if (hit && view->items.contains(hit)) {
        view->resetTransform();
        view->pick(hit, true);
    } else {
        find->setStyleSheet(u"QLineEdit { border: 2px solid #ff786e; }"_s);
        QTimer::singleShot(900, find, [this] { find->setStyleSheet(QString()); });
    }
}

// ------------------------------------------------------------------ the one picked
void EventNet::_picked(Entry *e)
{
    tree->clear();
    open_btn->setEnabled(e != nullptr);
    block_btn->setEnabled(e != nullptr);
    if (!e || !graph) {
        head->setText(u"Click a box"_s);
        meta->setText(u"Every box is an event (its id) or a decision; the lines say how they hang together."_s);
        return;
    }
    Graph *g = graph.get();
    head->setText(e->is_event() ? u"%1 — %2"_s.arg(e->id.value_or(u"None"_s), g->name.value(e)) : g->name.value(e));
    meta->setText(u"%1 · %2"_s.arg(e->kind == u"country_event"    ? u"an event of a nation"_s
                                   : e->kind == u"province_event" ? u"an event of a province"_s
                                                                  : u"a decision"_s,
                                   rel_of(e)));
    QFont bold(tree->font());
    bold.setBold(true);
    const std::vector<std::tuple<QString, bool, QString>> groups = {
        {u"fires"_s, true, u"It calls"_s},
        {u"fires"_s, false, u"It is called by"_s},
        {u"flag"_s, true, u"It sets a flag these ask for"_s},
        {u"flag"_s, false, u"It asks for a flag these set"_s},
        {u"modifier"_s, true, u"It adds a modifier these ask for"_s},
        {u"modifier"_s, false, u"It asks for a modifier these add"_s},
        {u"var"_s, true, u"It changes a number these ask for"_s},
        {u"var"_s, false, u"It asks for a number these change"_s}};
    const auto around = g->around(e, {u"fires"_s, u"flag"_s, u"modifier"_s, u"var"_s});
    for (const auto &[k, outgoing, title] : groups) {
        std::vector<std::pair<Entry *, QString>> rows;
        for (const auto &a : around)
            if (a.kind == k && a.outgoing == outgoing)
                rows.emplace_back(a.other, a.what);
        if (!rows.empty())
            _group(title, k, rows, bold);
    }
    const auto tags = g->same_tags(e);
    if (!tags.empty())
        _group(u"Speak of the same nations"_s, u"tag"_s, tags, bold);
    const auto same = g->same_file(e);
    if (!same.empty()) {
        std::vector<std::pair<Entry *, QString>> rows;
        for (size_t i = 0; i < same.size() && i < 80; ++i)
            rows.emplace_back(same[i], QString());
        _group(u"In the same file"_s, u"file"_s, rows, bold, false);
    }
    if (!tree->topLevelItemCount())
        tree->addTopLevelItem(new QTreeWidgetItem(QStringList{u"Nothing else has to do with it."_s}));
}

void EventNet::_group(const QString &title, const QString &kind, const std::vector<std::pair<Entry *, QString>> &rows,
                      const QFont &bold, bool open_)
{
    const auto [r, g, b] = KIND_RGB.value(kind);
    auto *h = new QTreeWidgetItem(QStringList{u"%1 (%2)"_s.arg(title, QString::number(rows.size()))});
    h->setFont(0, bold);
    h->setForeground(0, QBrush(QColor(r, g, b)));
    tree->addTopLevelItem(h);
    for (const auto &[o, what] : rows) {
        const QString name = graph->name.value(o);
        auto *it = new QTreeWidgetItem(QStringList{
            (o->is_event() ? u"%1 — %2"_s.arg(o->id.value_or(u"None"_s), name) : name) +
            (!what.isEmpty() ? u"   · "_s + what : QString())});
        it->setData(0, Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(o)));
        it->setToolTip(0, rel_of(o) + (!what.isEmpty() ? u" — "_s + what : QString()));
        h->addChild(it);
    }
    h->setExpanded(open_);
}

void EventNet::_tree_clicked(QTreeWidgetItem *it, int)
{
    const QVariant d = it->data(0, Qt::UserRole);
    Entry *o = d.isValid() ? reinterpret_cast<Entry *>(d.value<quintptr>()) : nullptr;
    if (o && view->items.contains(o))
        view->pick(o, true);
}

void EventNet::_open(Entry *e) { Q_EMIT openEntry(e->rel.value_or(QString()), e->span.first); }

}  // namespace eventnet
