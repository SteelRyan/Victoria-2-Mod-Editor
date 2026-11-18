// Scripts → ⧉ Nodes against Python, the C++ side of tools/check_nodegraph.py.
// Usage: med_nodegraph_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/blockcode.h"
#include "core/document.h"
#include "core/events.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "map/mapview_data.h"
#include "ui/eventpanel.h"
#include "ui/nodegraph.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace nodegraph;

static constexpr int W = 1760, H = 1020;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString f(double x) { return v(x); }
static QString pt(const QPointF &p) { return f(p.x()) + u","_s + f(p.y()); }
static QString rect(const QRectF &r) { return u"%1,%2,%3,%4"_s.arg(f(r.x()), f(r.y()), f(r.width()), f(r.height())); }
static QString ns(const std::optional<QString> &s) { return s ? *s : u"None"_s; }   // "%s" of a text or None
static QString attr_str(const Widget &wd) { return wd.attr == u"k" ? ns(wd.item->k) : wd.item->text; }

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));
    win.resize(1900, 1000);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    QObject::disconnect(win._check_timer, &QTimer::timeout, nullptr, nullptr);
    document::Document &d = *win.doc;
    eventpanel::EventPanel *ep = win.event_panel;
    auto &store = d.map<scriptstore::ScriptStore>(u"scripts"_s);

    auto wait = [&] {
        QElapsedTimer t;
        t.start();
        while (ep->working() && t.elapsed() < 300000)
            QApplication::processEvents();
        QApplication::processEvents();
    };
    auto settle = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            QApplication::processEvents();
        wait();
    };
    auto picker_rows = [&](NodeWindow *N) {
        Picker *p = N->picker;
        QStringList cats;
        for (int i = 0; i < p->cats->count(); ++i) {
            QListWidgetItem *it = p->cats->item(i);
            const QVariant dd = it->data(Qt::UserRole);
            cats << u"%1/%2/%3"_s.arg(it->text(), dd.isValid() ? dd.toString() : u"None"_s, it->foreground().color().name());
        }
        out << u"  picker %1 %2 | %3 | %4 | %5 | cats %6/%7 %8 | lst %9/%10"_s.arg(
            QString::number(p->isVisible() ? 1 : 0), rect(QRectF(p->geometry())), v(p->head->text()), v(p->count->text()),
            v(p->edit->text()), QString::number(p->cats->count()), QString::number(p->cats->currentRow()),
            h(cats.join(u'\n').toUtf8()), QString::number(p->lst->count()))
                   .arg(p->lst->currentRow());
        QStringList rows;
        for (int i = 0; i < p->lst->count(); ++i) {
            QListWidgetItem *it = p->lst->item(i);
            const QVariant dd = it->data(Qt::UserRole);
            const QVariantList l = dd.toList();
            rows << u"%1 | %2 | %3 | %4"_s.arg(it->text(),
                                              dd.isValid() ? l[0].toString() + u":"_s + l[1].toString() : u"None"_s,
                                              QString::number(it->flags().toInt()), it->foreground().color().name());
        }
        out << u"  picker rows "_s + h(rows.join(u'\n').toUtf8());
        for (int i = 0; i < rows.size() && i < 12; ++i)
            out << u"    "_s + rows[i];
    };
    auto snap = [&](const QString &tag, NodeWindow *N, bool all_nodes = true) {
        out << u"%1 | title %2 | head %3 | steps %4 | msg %5"_s.arg(tag, v(N->windowTitle()), v(N->head->text()),
                                                                   QString::number(d.undo_stack.size()),
                                                                   v(win.st_msg->text()));
        out << u"  info %1 | %2"_s.arg(v(N->info->text()), v(N->info->styleSheet()));
        out << u"  entry %1 | rel %2 | span %3 | kind %4 %5 | depth %6 | fold %7 | hidden %8 | text %9"_s.arg(
            N->entry_id ? v(QStringList{N->entry_id->first, N->entry_id->second}) : u"None"_s,
            N->rel ? v(*N->rel) : u"None"_s,
            N->span ? u"[%1, %2]"_s.arg(N->span->first).arg(N->span->second) : u"None"_s, N->entry_kind,
            N->entry_scope, QString::number(N->depth), QString::number(N->fold_depth), v(N->hidden_parts),
            N->rel ? h(store.text(*N->rel).toUtf8()) : u"None"_s);
        out << u"  side %1 | %2 | follow %3 | open %4 | key %5"_s.arg(
            v(N->ins_title->text()), v(N->ins_text->text()), QString::number(N->follow_box->isVisibleTo(N) ? 1 : 0),
            QString::number(N->open_btn->isVisibleTo(N) ? 1 : 0), h(N->key_lbl->text().toUtf8()));
        const QTransform tr = N->view->transform();
        out << u"  view %1 %2 | scene %3 | loose %4 | line %5 %6 | drag %7"_s.arg(
            f(tr.m11()), f(tr.m22()), rect(N->view->scene()->sceneRect()), QString::number(N->loose.size()),
            QString::number(N->line->isVisible() ? 1 : 0), v(N->line->text()), v(N->dragging.has_value()));
        QHash<const NodeItem *, int> idx;
        for (int i = 0; i < int(N->nodes.size()); ++i)
            idx.insert(N->nodes[size_t(i)], i);
        std::vector<long long> sel;
        for (QGraphicsItem *it : N->view->scene()->selectedItems())
            if (auto *n = dynamic_cast<NodeItem *>(it))
                sel.push_back(idx.value(n));
        std::sort(sel.begin(), sel.end());
        out << u"  nodes %1 | selected %2"_s.arg(QString::number(N->nodes.size()), v(sel));
        QStringList lines;
        for (int i = 0; i < int(N->nodes.size()); ++i) {
            NodeItem *n = N->nodes[size_t(i)];
            QStringList socks, wds;
            for (const Sock &s : n->socks)
                socks << u"%1/%2/%3/%4/%5/%6"_s.arg(s.name, s.type, ns(s.scope), ns(s.sub),
                                                    QString::number(s.single ? 1 : 0), QString::number(s.count));
            for (const Widget &wd : n->widgets)
                wds << u"%1/%2/%3/%4/%5"_s.arg(wd.label, wd.vtype, wd.attr, ns(wd.issue), n->shown(wd));
            const QString wire = !n->wire ? u"-"_s
                                          : u"%1 %2 %3"_s.arg(QString::number(n->wire->path().elementCount()),
                                                              rect(n->wire->path().boundingRect()),
                                                              QString::number(n->wire->isSelected() ? 1 : 0));
            lines << u"%1 %2/%3/%4 %5 | %6 %7 %8 | f%9"_s.arg(QString::number(i), n->kind, n->mode, ns(n->scope),
                                                               v(n->title), pt(n->pos()), f(n->w), f(n->h),
                                                               QString::number(n->folded ? 1 : 0)) +
                         u" %1 d%2 | %3 %4 | miss %5 | par %6 %7 kids %8 | %9"_s.arg(
                             QString::number(n->inside), QString::number(n->depth), ns(n->what),
                             QString::number(n->wrong ? 1 : 0), v(n->missing),
                             n->parent_ ? QString::number(idx.value(n->parent_)) : u"None"_s,
                             QString::number(n->psock), QString::number(n->children.size()), socks.join(u';')) +
                         u" | %1 | %2"_s.arg(wds.join(u';'), wire);
        }
        out << u"  nodes hash "_s + h(lines.join(u'\n').toUtf8());
        for (int i = 0; i < lines.size() && (all_nodes || i < 40); ++i)
            out << u"   "_s + lines[i];
        picker_rows(N);
    };
    auto grab = [&](QWidget *w, const QString &name) {
        settle(400);
        w->grab().save(QDir(out_dir).filePath(name));
    };
    auto hits = [&](NodeWindow *N, const QString &tag) {
        QHash<const NodeItem *, int> idx;
        for (int i = 0; i < int(N->nodes.size()); ++i)
            idx.insert(N->nodes[size_t(i)], i);
        QStringList rows;
        for (int i = 0; i < int(N->nodes.size()) && i < 30; ++i) {
            NodeItem *n = N->nodes[size_t(i)];
            std::vector<QPointF> pts = {QPointF(0, HEAD / 2), QPointF(n->w / 2, HEAD / 2), QPointF(n->w / 2, n->h - 4)};
            for (int k = 0; k < int(n->socks.size()); ++k)
                pts.emplace_back(n->w, n->sock_y(k));
            for (int k = 0; k < int(n->socks.size()); ++k)
                pts.push_back(n->add_rect(k).center());
            for (int j = 0; j < int(n->widgets.size()); ++j)
                pts.push_back(n->pill_rect(j).center());
            pts.push_back(n->more_rect().center());
            pts.push_back(n->fold_rect().center());
            pts.push_back(n->unfold_rect().center());
            for (const QPointF &p : pts) {
                const auto r = N->hit(n->mapToScene(p));
                rows << u"%1 %2 -> %3"_s.arg(QString::number(i), pt(p),
                                             !r ? u"None"_s
                                                : u"%1 %2 %3"_s.arg(r->what,
                                                                    idx.contains(r->node) ? QString::number(idx.value(r->node))
                                                                                          : u"None"_s,
                                                                    r->i ? QString::number(*r->i) : u"None"_s));
            }
        }
        out << u"%1 hits %2 %3"_s.arg(tag, QString::number(rows.size()), h(rows.join(u'\n').toUtf8()));
        for (int i = 0; i < rows.size() && i < 16; ++i)
            out << u"   "_s + rows[i];
    };
    auto lines_of = [&](const events::Entry &e) {
        return py::splitlines(py::slice(store.text(e.rel.value_or(QString())), e.span.first, e.span.second)).size();
    };

    win.set_tab(u"events"_s);
    QApplication::processEvents();
    wait();
    events::Entry *ev = nullptr;
    for (const auto &x : ep->db->entries)
        if (x->is_event() && x->options.size() >= 2 && x->kind == u"country_event" && lines_of(*x) < 80) {
            ev = x.get();
            break;
        }
    out << u"event "_s + ev->uid.value_or(u"None"_s);
    const OptStr ev_uid = ev->uid;                  // (a re-read frees the entries: found again by this later)
    ep->select(ev);
    QApplication::processEvents();
    wait();
    win._script_tool(u"nodes"_s);
    QApplication::processEvents();
    NodeWindow *N = win.node_win;
    N->resize(W, H);
    settle(500);
    N->fit();
    settle(200);
    snap(u"opened"_s, N);
    grab(N, u"nodes_event.png"_s);
    hits(N, u"opened"_s);
    // a choice picked
    NodeItem *opt = *std::find_if(N->nodes.begin(), N->nodes.end(), [](NodeItem *n) { return n->kind == u"option"; });
    N->view->scene()->clearSelection();
    opt->setSelected(true);
    settle(200);
    snap(u"option picked"_s, N);
    N->view->scene()->clearSelection();
    settle(100);
    // a yes / no flipped
    auto find_w = [&](const std::function<bool(const Widget &)> &ok) -> std::pair<NodeItem *, int> {
        for (NodeItem *n : N->nodes)
            for (int j = 0; j < int(n->widgets.size()); ++j)
                if (ok(n->widgets[size_t(j)]))
                    return {n, j};
        return {nullptr, -1};
    };
    {
        const auto [n, j] = find_w([](const Widget &wd) {
            return wd.vtype == u"bool" && (attr_str(wd).toLower() == u"yes" || attr_str(wd).toLower() == u"no");
        });
        if (n) {
            N->act(Hit{u"pill"_s, n, j});
            settle(300);
            snap(u"bool flipped"_s, N);
        } else {
            out << u"no bool"_s;
        }
    }
    // a text typed into a value's field
    {
        auto got = find_w([](const Widget &wd) { return wd.vtype == u"num"; });
        if (!got.first)
            got = find_w([](const Widget &wd) { return wd.vtype == u"text"; });
        if (got.first) {
            const long long uid = got.first->uid;
            const bool num = got.first->widgets[size_t(got.second)].vtype == u"num";
            N->edit_value(got.first, got.second);
            settle(100);
            snap(u"field open"_s, N, false);
            N->line->setText(num ? u"x y"_s : u"Typed text"_s);
            N->_line_done();
            settle(300);
            snap(u"field typed"_s, N, false);
            NodeItem *n2 = N->find(uid);
            if (n2 && n2->widgets[size_t(got.second)].vtype == u"num") {
                N->edit_value(n2, got.second);
                N->line->setText(u"7"_s);
                N->_line_done();
                settle(300);
                snap(u"number typed"_s, N, false);
            }
        }
    }
    // a value picked from the menu
    {
        const auto [n, j] = find_w([&](const Widget &wd) {
            static const QStringList NOT = {u"num"_s, u"int"_s, u"text"_s, u"prov"_s, u"bool"_s};
            if (NOT.contains(wd.vtype))
                return false;
            const auto ch = N->env->choices(wd.vtype, wd.issue);
            return ch && !ch->empty();
        });
        if (n) {
            N->edit_value(n, j);
            settle(100);
            snap(u"menu open"_s, N, false);
            N->picker->edit->setText(u"a"_s);
            settle(100);
            snap(u"menu searched"_s, N, false);
            N->picker->_step(1);
            N->picker->take(N->picker->lst->currentItem());
            settle(300);
            snap(u"menu taken"_s, N);
        } else {
            out << u"no menu value"_s;
        }
    }
    // ＋ at an input: immediate (or effect) of the entry
    {
        NodeItem *rn = N->root_node;
        std::optional<int> si;
        for (int i = 0; i < int(rn->socks.size()); ++i)
            if (rn->socks[size_t(i)].type == u"eff") {
                si = i;
                break;
            }
        N->act(Hit{u"add"_s, rn, si});
        settle(100);
        snap(u"add menu"_s, N, false);
        N->picker->edit->setText(u"prestige"_s);
        settle(100);
        N->picker->take(N->picker->lst->currentItem());
        settle(300);
        snap(u"added effect"_s, N);
        rn = N->root_node;
        si.reset();
        for (int i = 0; i < int(rn->socks.size()); ++i)
            if (rn->socks[size_t(i)].type == u"opt") {
                si = i;
                break;
            }
        if (si) {
            N->add_for(N->root_node, *si, std::nullopt, QPoint(50, 50));
            settle(300);
            snap(u"added option"_s, N);
        }
    }
    // the menu of everything: a typed line, loose; then wired into the first input of the entry
    N->add_anywhere(QPointF(-400, -300), QPoint(30, 30));
    settle(100);
    snap(u"anywhere menu"_s, N, false);
    N->picker->edit->setText(u"my_test_line"_s);
    settle(100);
    N->picker->take(N->picker->lst->item(N->picker->lst->count() - 1));
    settle(300);
    snap(u"loose"_s, N);
    {
        NodeItem *lo = *std::find_if(N->nodes.begin(), N->nodes.end(),
                                     [](NodeItem *n) { return !n->parent_ && n->kind != u"entry"; });
        const PPtr item = lo->item;
        N->wire(lo, N->root_node, 0);
        settle(300);
        snap(u"wired"_s, N);
        lo = N->node_of(item.get());
        if (lo && lo->wire) {
            N->view->scene()->clearSelection();
            lo->wire->setSelected(true);
            N->delete_selected();
            settle(300);
            snap(u"wire deleted"_s, N);
        }
        lo = nullptr;
        for (NodeItem *n : N->nodes)
            if (!n->parent_ && n->kind != u"entry") {
                lo = n;
                break;
            }
        if (lo) {
            N->view->scene()->clearSelection();
            lo->setSelected(true);
            N->view->setFocus();
            QKeyEvent ke(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            QApplication::sendEvent(N->view, &ke);
            settle(300);
            snap(u"node deleted"_s, N);
        }
    }
    // fold / open a choice
    {
        NodeItem *o = nullptr;
        for (NodeItem *n : N->nodes)
            if (n->kind == u"option" && !n->children.empty()) {
                o = n;
                break;
            }
        if (o) {
            N->act(Hit{u"fold"_s, o, std::nullopt});
            settle(200);
            snap(u"folded"_s, N);
            o = *std::find_if(N->nodes.begin(), N->nodes.end(), [](NodeItem *n) { return n->folded; });
            N->act(Hit{u"unfold"_s, o, std::nullopt});
            settle(200);
            snap(u"unfolded"_s, N);
        }
    }
    // ＋ Follow-up event, Ctrl+Z
    opt = *std::find_if(N->nodes.begin(), N->nodes.end(), [](NodeItem *n) { return n->kind == u"option"; });
    N->view->scene()->clearSelection();
    opt->setSelected(true);
    settle(100);
    N->follow_days->setValue(12);
    N->follow_up();
    settle(400);
    snap(u"follow-up"_s, N);
    win.undo();
    settle(400);
    snap(u"undone"_s, N);
    // a template into the file shown
    for (const auto &x : ep->db->entries)               // (the reparse made new entries: the same one by its uid)
        if (x->uid == ev_uid) {
            ev = x.get();
            break;
        }
    ep->select(ev);
    settle(200);
    N->refresh(&d, ev, ep->refs.get(), ep->db.get());
    settle(200);
    N->add_template(u"chain3"_s);
    settle(800);
    snap(u"template"_s, N);
    // Tidy
    N->layout_nodes();
    N->fit();
    settle(200);
    snap(u"tidy"_s, N);
    // a decision
    {
        events::Entry *dec = nullptr;
        for (const auto &x : ep->db->entries)
            if (!x->is_event()) {
                dec = x.get();
                break;
            }
        ep->select(dec);
        settle(300);
        snap(u"decision"_s, N);
        grab(N, u"nodes_decision.png"_s);
        hits(N, u"decision"_s);
    }
    // the largest event: folded
    {
        events::Entry *big = nullptr;
        for (const auto &x : ep->db->entries)
            if (x->is_event() && (!big || x->span.second - x->span.first > big->span.second - big->span.first))
                big = x.get();
        ep->select(big);
        settle(500);
        snap(u"large"_s, N, false);
        grab(N, u"nodes_large.png"_s);
        NodeItem *o = nullptr;
        for (NodeItem *n : N->nodes)
            if (n->folded) {
                o = n;
                break;
            }
        if (o) {
            N->act(Hit{u"unfold"_s, o, std::nullopt});
            settle(200);
            snap(u"large opened"_s, N, false);
        }
    }
    // mouse: a right click on the empty canvas (the menu), Esc; the wheel
    {
        QWidget *vp = N->view->viewport();
        QPointF p(6, 6);
        while (N->hit(N->view->mapToScene(p.toPoint())) || N->view->itemAt(p.toPoint()))
            p += QPointF(10, 10);
        const QPoint g = vp->mapToGlobal(p.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, p, QPointF(g), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        QMouseEvent rel(QEvent::MouseButtonRelease, p, QPointF(g), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &rel);
        settle(200);
        snap(u"right click"_s, N, false);
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(N->picker->edit, &esc);
        settle(100);
        snap(u"esc"_s, N, false);
        for (int k : {1, 1, -1}) {
            QWheelEvent we(p, QPointF(g), QPoint(0, 0), QPoint(0, 120 * k), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
            QApplication::sendEvent(vp, &we);
        }
        settle(100);
        snap(u"wheel"_s, N, false);
    }
    // the menu's rows, what_of
    struct RowCase {
        QString mode;
        std::optional<QString> scope;
        bool blocks;
        QString prefix;
    };
    for (const RowCase &c : {RowCase{u"cond"_s, u"c"_s, false, QString()}, RowCase{u"eff"_s, std::nullopt, true, u"x · "_s},
                             RowCase{u"goal"_s, u"c"_s, false, QString()}, RowCase{u"cond"_s, u"p"_s, false, QString()}}) {
        const auto rows = N->_spec_rows(c.mode, c.scope, c.blocks, c.prefix);
        QStringList txt;
        for (const Picker::Row &r : rows)
            txt << u"%1|%2|%3|%4"_s.arg(r.text, r.data.mode, ns(r.colour), ns(r.group));
        out << u"rows %1 %2 %3 %4"_s.arg(c.mode, ns(c.scope), QString::number(rows.size()), h(txt.join(u'\n').toUtf8()));
        for (size_t i = 0; i < rows.size() && i < 5; ++i)
            out << u"   %1 | %2 | %3 | %4"_s.arg(rows[i].text, rows[i].data.spec->key, ns(rows[i].colour), ns(rows[i].group));
    }
    for (const QString &mode : {u"cond"_s, u"eff"_s, u"goal"_s}) {
        QStringList ws;
        for (const blockcode::Spec &s : blockcode::OF_MODE(mode))
            ws << what_of(&s);
        out << u"what_of %1 %2 %3"_s.arg(mode, QString::number(ws.size()), h(ws.join(u'\n').toUtf8()));
    }
    out << u"count_inside "_s + QString::number(count_inside(*N->root));
    win._refresh_windows();
    settle(300);
    snap(u"main refresh"_s, N, false);
    QFile fo(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!fo.open(QIODevice::WriteOnly))
        return 3;
    fo.write((out.join(u'\n') + u'\n').toUtf8());
    fo.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    QApplication::setCursorFlashTime(0);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
