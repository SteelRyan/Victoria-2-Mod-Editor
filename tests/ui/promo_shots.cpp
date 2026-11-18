// Promotion pictures (the user 2026-11-14: "open up the editor, open up a few different mods, and ... showcase a few
// of the possibilities ... before and after pictures"). The real main window, offscreen, driven by a small command
// file; every edit goes through the window's own handlers (the ones its panels call), Apply through its own save().
// Usage: med_promo <command file> <out folder>   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own)
// Commands, one per line (# = note):
//   open <mod folder> | <base game folder> [| replace,dirs]   size <w> <h>   page <rail key>   mode <map mode key>
//   modes (prints the map modes)   fit <set> [pad]   fitall   select <set>   clear
//   owner <tag> <set>   pops <set> <culture|-> <religion|-> <times|->   apply   shot <name>   shotmap <name>
//   count <set>   errors
//   clips: rec <name>  endrec  cursor x y  hold n  caption <text|->  moveto x y n  movepid pid n
//   clickpid pid [alt|shift|ctrl]  movepie edge n  dragpie edge to_pct n  movebtn|clickbtn n <text>  zoompid pid z
// <set> = parts joined by '&': owner:TAG  continent:name  ids:1,2,3  land  notids:1,2
#include "../core/nopopup.h"
#include "app/app.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/submods.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/panels.h"
#include "ui/widgets.h"
#include "ui/theme.h"
#include "ui/techpanel.h"
#include "ui/prodpanel.h"
#include "ui/provpanel.h"
#include "ui/nationpanel.h"
#include "ui/diplopanel.h"

#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QThread>
#include <QTimer>
#include <QMenu>
#include <QDialog>
#include <QPointer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>
#include <functional>
#include <cstdio>
#include <memory>

using namespace Qt::StringLiterals;

static QString g_out;
static std::shared_ptr<moddata::Mod> g_mod;
static std::shared_ptr<mapview::MapData> g_md;
static std::unique_ptr<mainwindow::MainWindow> g_win;

static void flush(int n = 8)
{
    for (int i = 0; i < n; ++i)
        QApplication::processEvents();
}

// ---- clips: frames of the window with a drawn mouse pointer (window coordinates), a caption at the bottom
static QString g_rec;                 // folder of the clip being recorded ('' = none)
static int g_frame = 0;
static QPointF g_cur(960, 540);
static int g_press = 0;               // frames left of the click ring
static bool g_down = false;
static QString g_caption;

static QImage compose()                  // the window with the pickers / dialogs on show drawn over it
{
    QImage im = g_win->grab().toImage().convertToFormat(QImage::Format_RGB32);
    QPainter p(&im);
    for (QWidget *t : QApplication::topLevelWidgets())
        if (t != g_win.get() && t->isVisible() && t->width() > 4 && t->height() > 4 &&
            !t->windowFlags().testFlag(Qt::ToolTip)) {
            const QPoint at = g_win->mapFromGlobal(t->mapToGlobal(QPoint(0, 0)));
            p.fillRect(QRect(at + QPoint(6, 8), t->size()), QColor(0, 0, 0, 90));   // a soft shadow
            p.drawPixmap(at, t->grab());
            p.setPen(QPen(QColor(214, 170, 92, 160), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRect(at, t->size()).adjusted(0, 0, -1, -1));
        }
    p.end();
    return im;
}

// a before / after wipe: the snapshot is shown right of a gold line that sweeps over the map
static std::map<QString, QImage> g_snaps;
static const QImage *g_wipe_img = nullptr;
static double g_wipe_x = -1;              // window x of the line (-1: no wipe)
static QRect g_wipe_rect;

static void frame()
{
    if (g_rec.isEmpty())
        return;
    flush(3);
    QImage im = compose();
    if (g_wipe_img && g_wipe_x >= 0) {
        QPainter w(&im);
        const QRect r(QPoint(int(g_wipe_x), g_wipe_rect.top()), g_wipe_rect.bottomRight());
        w.drawImage(r, *g_wipe_img, r);
        w.setPen(QPen(QColor(214, 170, 92), 6));
        w.drawLine(QPointF(g_wipe_x, g_wipe_rect.top()), QPointF(g_wipe_x, g_wipe_rect.bottom()));
        QFont f(u"Segoe UI"_s);
        f.setPixelSize(30);
        f.setBold(true);
        w.setFont(f);
        auto tag = [&](const QString &t, double x, bool right) {
            const QRectF br = w.boundingRect(QRectF(0, 0, 400, 60), Qt::AlignCenter, t);
            const QRectF box(right ? x + 18 : x - 18 - br.width() - 36, g_wipe_rect.top() + 24, br.width() + 36, 50);
            w.setPen(Qt::NoPen);
            w.setBrush(QColor(14, 17, 23, 220));
            w.drawRoundedRect(box, 12, 12);
            w.setPen(QColor(214, 170, 92));
            w.drawText(box, Qt::AlignCenter, t);
        };
        if (g_wipe_x > g_wipe_rect.left() + 220)
            tag(u"AFTER"_s, g_wipe_x, false);
        if (g_wipe_x < g_wipe_rect.right() - 220)
            tag(u"BEFORE"_s, g_wipe_x, true);
    }
    QPainter p(&im);
    p.setRenderHint(QPainter::Antialiasing);
    if (g_press > 0 || g_down) {                      // the click: a gold ring around the pointer
        const double k = g_down ? 1.0 : g_press / 8.0;
        p.setPen(QPen(QColor(214, 170, 92, int(230 * k)), 4));
        p.setBrush(QColor(214, 170, 92, int(70 * k)));
        const double r = 16 + (1.0 - k) * 18;
        p.drawEllipse(g_cur, r, r);
        if (g_press > 0)
            --g_press;
    }
    // the pointer: a white arrow with a dark outline, ~1.6x the usual size
    const double s = 1.6;
    QPolygonF arrow;
    for (const auto &[x, y] : std::vector<std::pair<double, double>>{
             {0, 0}, {0, 17}, {4, 13}, {7, 20}, {10, 19}, {7, 12}, {12, 12}})
        arrow << g_cur + QPointF(x * s, y * s);
    p.setPen(QPen(QColor(10, 10, 12), 2.2));
    p.setBrush(Qt::white);
    p.drawPolygon(arrow);
    if (!g_caption.isEmpty()) {
        QFont f(u"Segoe UI"_s);
        f.setPixelSize(38);
        f.setBold(true);
        p.setFont(f);
        const QRectF tr = p.boundingRect(QRectF(0, 0, im.width(), 200), Qt::AlignCenter, g_caption);
        const QRectF box(im.width() / 2.0 - tr.width() / 2 - 34, im.height() - 150, tr.width() + 68, tr.height() + 30);
        p.setPen(QPen(QColor(214, 170, 92), 3));
        p.setBrush(QColor(14, 17, 23, 235));
        p.drawRoundedRect(box, 18, 18);
        p.setPen(QColor(236, 220, 186));
        p.drawText(box, Qt::AlignCenter, g_caption);
    }
    p.end();
    im.save(QDir(g_rec).filePath(u"f%1.jpg"_s.arg(g_frame++, 5, 10, QChar(u'0'))), "JPG", 93);
}

static void hold(int n)
{
    for (int i = 0; i < n; ++i)
        frame();
}

static double ease(double t) { return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2; }

static void move_to(QPointF to, int n, const std::function<void(QPointF)> &each = nullptr)
{
    const QPointF from = g_cur;
    for (int i = 1; i <= n; ++i) {
        g_cur = from + (to - from) * ease(double(i) / n);
        if (each)
            each(g_cur);
        frame();
    }
}

static QPointF pid_point(long long pid)            // a province's middle, in window coordinates
{
    const QPoint vp = g_win->map->mapFromScene(QPointF(g_md->cx[size_t(pid)] + 0.5, g_md->cy[size_t(pid)] + 0.5));
    return QPointF(g_win->map->viewport()->mapTo(g_win.get(), vp));
}

static Qt::KeyboardModifiers mods_of(const QString &m)
{
    Qt::KeyboardModifiers k;
    if (m.contains(u"alt"))
        k |= Qt::AltModifier;
    if (m.contains(u"shift"))
        k |= Qt::ShiftModifier;
    if (m.contains(u"ctrl"))
        k |= Qt::ControlModifier;
    return k;
}

static void send_mouse(QWidget *w, QEvent::Type t, QPointF win_pt, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    const QPointF local = w->mapFrom(g_win.get(), win_pt.toPoint());
    const QPointF global = w->mapToGlobal(local.toPoint());
    const Qt::MouseButton b = t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent ev(t, local, local, global, b, buttons, mods);
    QApplication::sendEvent(w, &ev);
}

static QPointF pie_point(long long edge, double pct = -1)   // an edge of the pie (or an angle, pct of the circle)
{
    widgets::PieChart *pie = g_win->edit_panel->pie;
    double acc = 0;
    for (long long i = 0; i <= edge && size_t(i) < pie->seg().size(); ++i)
        acc += pie->seg()[size_t(i)].pct;
    const double ang = (pct >= 0 ? pct : acc) * 3.6 * M_PI / 180.0;
    const double side = std::min(pie->width(), pie->height()) - 16;
    const double r = side / 2.0 * 0.78;
    const QPointF c(pie->width() / 2.0 + r * std::sin(ang), pie->height() / 2.0 - r * std::cos(ang));
    return QPointF(pie->mapTo(g_win.get(), c.toPoint()));
}

static QPushButton *button_named(const QString &text)
{
    for (QPushButton *b : g_win->findChildren<QPushButton *>())
        if (b->isVisible() && b->text() == text)
            return b;
    return nullptr;
}


// ---- generic: find a widget on screen (a "locator"), click it for real, type into it
struct Spot {
    QWidget *w = nullptr;        // the widget the events go to
    QPointF at;                  // in that widget
};

static QString text_of(QWidget *w)
{
    if (auto *wb = qobject_cast<widgets::WrapButton *>(w))
        return wb->text();
    if (auto *b = qobject_cast<QAbstractButton *>(w))
        return b->text();
    if (auto *el = qobject_cast<widgets::ElideLabel *>(w))
        return el->text();
    if (auto *l = qobject_cast<QLabel *>(w))
        return l->text();
    return {};
}

static bool g_any = false;              // (see: also the widgets scrolled out of view)
static bool shown(QWidget *w)
{
    if (!w->isVisible() || w->width() < 2 || w->height() < 2)
        return false;
    if (g_any)
        return true;
    for (QWidget *p = w; p && !p->isWindow() && (p = p->parentWidget());)   // in a scroll area of its own window: in view
        if (auto *sa = qobject_cast<QScrollArea *>(p)) {
            const QRect vr(sa->viewport()->mapToGlobal(QPoint(0, 0)), sa->viewport()->size());
            const QRect wr(w->mapToGlobal(QPoint(0, 0)), w->size());
            if (!vr.intersects(wr))
                return false;
        }
    return true;
}

static QList<QWidget *> roots()          // the windows on show, the newest (pickers, dialogs) first
{
    QList<QWidget *> out;
    for (QWidget *t : QApplication::topLevelWidgets())
        if (t != g_win.get() && t->isVisible())
            out.prepend(t);
    out.append(g_win.get());
    return out;
}

template <class T> static QList<T *> all_shown()
{
    QList<T *> out;
    for (QWidget *r : roots()) {
        if (auto *rt = qobject_cast<T *>(r); rt && shown(rt))
            out << rt;
        for (T *w : r->findChildren<T *>())
            if (shown(w))
                out << w;
    }
    return out;
}

static QPointF gpos(QWidget *w, QPointF local) { return QPointF(w->mapToGlobal(local.toPoint())); }

static Spot locate(const QString &loc0)
{
    QString loc = loc0.trimmed();
    QPointF off;
    if (const int at = loc.lastIndexOf(u'@'); at > 0 && loc.mid(at + 1).contains(u',')) {
        off = QPointF(loc.mid(at + 1).section(u',', 0, 0).toDouble(), loc.mid(at + 1).section(u',', 1).toDouble());
        loc = loc.left(at);
    }
    const QString k = loc.section(u'=', 0, 0), v = loc.section(u'=', 1);
    Spot s;
    auto centre = [&](QWidget *w) {
        s.w = w, s.at = QPointF(w->rect().center()) + off;
        if (auto *cb = qobject_cast<QCheckBox *>(w)) {   // a tick box only reacts on its box + text, not its full width
            QStyleOptionButton o;
            o.initFrom(cb);
            o.text = cb->text();
            s.at = QPointF(cb->style()->subElementRect(QStyle::SE_CheckBoxClickRect, &o, cb).center()) + off;
        }
    };
    if (k == u"btn" || k == u"btn~") {    // btn=Text#2: the second one
        QString want = v;
        int nth = 1;
        if (const int h = v.lastIndexOf(u'#'); h > 0 && v.mid(h + 1).toInt() > 0)
            nth = v.mid(h + 1).toInt(), want = v.left(h);
        for (QAbstractButton *b : all_shown<QAbstractButton>()) {
            const QString t = text_of(b).replace(u'&', QString());
            if ((k == u"btn" ? t.trimmed() == want : t.contains(want)) && --nth == 0) {
                centre(b);
                return s;
            }
        }
    } else if (k == u"ph") {
        for (QLineEdit *e : all_shown<QLineEdit>())
            if (e->placeholderText().contains(v)) {
                centre(e);
                return s;
            }
    } else if (k == u"lbl") {
        for (QLabel *l : all_shown<QLabel>())
            if (text_of(l).contains(v)) {
                centre(l);
                return s;
            }
    } else if (k == u"near") {         // near=Label text|what : the widget of that kind on the label's row, right of it
        const QString lt = v.section(u'|', 0, 0), what = v.section(u'|', 1);
        QLabel *lab = nullptr;
        for (QLabel *l : all_shown<QLabel>())
            if (text_of(l).trimmed() == lt) {
                lab = l;
                break;
            }
        if (!lab)
            for (QLabel *l : all_shown<QLabel>())
                if (text_of(l).contains(lt)) {
                    lab = l;
                    break;
                }
        if (!lab)
            return s;
        const QPointF lc = gpos(lab, lab->rect().center());
        QWidget *best = nullptr;
        double bd = 1e9;
        auto consider = [&](QWidget *w) {
            const QPointF c = gpos(w, w->rect().center());
            const QRectF r(gpos(w, QPointF(0, 0)), w->size());
            if (std::abs(c.y() - lc.y()) > std::max(16.0, r.height() / 2 + 4) || r.right() < lc.x())
                return;
            const double d = c.x() - lc.x();
            if (d < bd)
                bd = d, best = w;
        };
        if (what == u"edit")
            for (QLineEdit *w : all_shown<QLineEdit>())
                consider(w);
        else if (what == u"combo")
            for (QComboBox *w : all_shown<QComboBox>())
                consider(w);
        else if (what == u"slider")
            for (QSlider *w : all_shown<QSlider>())
                consider(w);
        else if (what == u"step")
            for (widgets::StepSlider *w : all_shown<widgets::StepSlider>())
                consider(w);
        else
            for (QAbstractButton *w : all_shown<QAbstractButton>())
                if (text_of(w).trimmed() == what)
                    consider(w);
        if (!best) {                    // none on the label's row: the nearest one just below it (a label on top)
            QList<QWidget *> cands;
            if (what == u"edit")
                for (QLineEdit *w : all_shown<QLineEdit>())
                    cands << w;
            else if (what == u"combo")
                for (QComboBox *w : all_shown<QComboBox>())
                    cands << w;
            else if (what == u"slider")
                for (QSlider *w : all_shown<QSlider>())
                    cands << w;
            else if (what == u"step")
                for (widgets::StepSlider *w : all_shown<widgets::StepSlider>())
                    cands << w;
            const QRectF lr(gpos(lab, QPointF(0, 0)), lab->size());
            for (QWidget *w : cands) {
                const QRectF r(gpos(w, QPointF(0, 0)), w->size());
                const double d = r.top() - lr.bottom();
                if (d < -4 || d > 90 || r.right() < lr.left() || r.left() > lr.right() + 400)
                    continue;
                if (d < bd)
                    bd = d, best = w;
            }
        }
        if (best)
            centre(best);
    } else if (k == u"item") {          // a row of a list / tree on show
        for (QTreeWidget *t : all_shown<QTreeWidget>())
            for (QTreeWidgetItem *it : t->findItems(v, Qt::MatchContains | Qt::MatchRecursive))
                if (!it->isHidden() && it->childCount() == 0) {
                    t->scrollToItem(it);
                    flush(2);
                    s.w = t->viewport(), s.at = QPointF(t->visualItemRect(it).center()) + off;
                    return s;
                }
        for (QListWidget *l : all_shown<QListWidget>())
            for (QListWidgetItem *it : l->findItems(v, Qt::MatchContains))
                if (!it->isHidden()) {
                    l->scrollToItem(it);
                    flush(2);
                    s.w = l->viewport(), s.at = QPointF(l->visualItemRect(it).center()) + off;
                    return s;
                }
        for (QAbstractItemView *vw : all_shown<QAbstractItemView>()) {   // a combo's popup list and the like
            if (qobject_cast<QTreeWidget *>(vw) || qobject_cast<QListWidget *>(vw) || !vw->model())
                continue;
            QAbstractItemModel *m = vw->model();
            for (int r = 0; r < m->rowCount(vw->rootIndex()); ++r) {
                const QModelIndex ix = m->index(r, 0, vw->rootIndex());
                if (!m->data(ix).toString().contains(v))
                    continue;
                vw->scrollTo(ix);
                flush(2);
                s.w = vw->viewport(), s.at = QPointF(vw->visualRect(ix).center()) + off;
                return s;
            }
        }
    } else if (k == u"menu") {          // an entry of a menu on show
        for (QMenu *m : all_shown<QMenu>())
            for (QAction *act : m->actions())
                if (act->text().replace(u'&', QString()).contains(v)) {
                    s.w = m, s.at = QPointF(m->actionGeometry(act).center()) + off;
                    return s;
                }
    } else if (k == u"pid" || k == u"map") {
        QPointF sc;
        if (k == u"pid")
            sc = QPointF(g_md->cx[size_t(v.toLongLong())] + 0.5, g_md->cy[size_t(v.toLongLong())] + 0.5);
        else
            sc = QPointF(v.section(u',', 0, 0).toDouble(), v.section(u',', 1).toDouble());
        s.w = g_win->map->viewport(), s.at = QPointF(g_win->map->mapFromScene(sc)) + off;
    } else if (k == u"win") {
        s.w = g_win.get(), s.at = QPointF(v.section(u',', 0, 0).toDouble(), v.section(u',', 1).toDouble());
    } else if (k == u"tech") {
        for (techpanel::TechGrid *g : all_shown<techpanel::TechGrid>())
            for (const auto &[r, key] : g->_cells)
                if (key == v) {
                    s.w = g, s.at = r.center() + off;
                    return s;
                }
    } else if (k == u"card") {          // card=kind:key — a card of the production chart
        prodpanel::ProdChart *ch = g_win->prod_ws->chart;
        for (const auto &[n, r] : ch->nodes)
            if (n.kind == v.section(u':', 0, 0) && n.key == v.section(u':', 1)) {
                s.w = ch;
                s.at = QPointF(ch->off.x() + r.center().x() * ch->zoom, ch->off.y() + r.center().y() * ch->zoom) + off;
                return s;
            }
    } else if (k == u"w") {             // named widgets
        QWidget *w = nullptr;
        if (v == u"owner") w = g_win->prov_panel->owner;
        else if (v == u"controller") w = g_win->prov_panel->controller;
        else if (v == u"addcore") w = g_win->prov_panel->add_core;
        else if (v == u"nation") w = g_win->nations_panel->picker;
        else if (v == u"pie") w = g_win->edit_panel->pie;
        else if (v == u"search") w = g_win->search;
        else if (v == u"modes") w = g_win->color_box;
        else if (v == u"apply") w = g_win->apply_btn;
        else if (v == u"modaledit")
            if (QWidget *m = QApplication::activeModalWidget())
                w = m->findChild<QLineEdit *>();
        if (w)
            centre(w);
    }
    return s;
}

static QPointF win_pt(const Spot &s) { return QPointF(g_win->mapFromGlobal(s.w->mapToGlobal(s.at.toPoint()))); }

static void real_mouse(QWidget *w, QEvent::Type t, QPointF local, Qt::MouseButton b, Qt::MouseButtons bs,
                       Qt::KeyboardModifiers mods, bool deep = true)
{
    QWidget *tgt = w;                  // the deepest child under the point gets it (as with a real mouse)
    QPoint lp = local.toPoint();
    if (deep)
        while (QWidget *c = tgt->childAt(lp)) {
            lp = c->mapFrom(tgt, lp);
            tgt = c;
        }
    const QPointF gl = QPointF(tgt->mapToGlobal(lp));
    QMouseEvent ev(t, QPointF(lp), QPointF(lp), gl, b, bs, mods);
    QApplication::sendEvent(tgt, &ev);
}

static void real_click(const Spot &s, Qt::KeyboardModifiers mods, bool right = false, bool dbl = false)
{
    const Qt::MouseButton b = right ? Qt::RightButton : Qt::LeftButton;
    g_down = true;
    hold(2);
    real_mouse(s.w, QEvent::MouseButtonPress, s.at, b, b, mods);
    real_mouse(s.w, QEvent::MouseButtonRelease, s.at, b, Qt::NoButton, mods);
    if (dbl) {
        real_mouse(s.w, QEvent::MouseButtonDblClick, s.at, b, b, mods);
        real_mouse(s.w, QEvent::MouseButtonRelease, s.at, b, Qt::NoButton, mods);
    }
    g_down = false;
    g_press = 8;
    flush(10);
}

static void send_key(QWidget *w, int key, Qt::KeyboardModifiers mods, const QString &text)
{
    QKeyEvent pr(QEvent::KeyPress, key, mods, text), rl(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(w, &pr);
    QApplication::sendEvent(w, &rl);
}

static void wait_ms(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

static QPointer<QWidget> g_typed;   // the last widget typed into (key / type without a locator go there)

static int run_lines(bool nested);

static std::vector<long long> eval_set(const QString &expr)
{
    std::vector<long long> out;
    const QStringList parts = expr.split(u'&', Qt::SkipEmptyParts);
    mapfiles::MembershipFile *cont = nullptr;
    if (g_win->doc->maps.contains(u"continent"_s))
        cont = &g_win->doc->map<mapfiles::MembershipFile>(u"continent"_s);
    for (const auto &[pid, prov] : g_mod->provinces) {
        const moddata::Province *pr = &prov;
        if (!pr || pid <= 0 || size_t(pid) >= g_md->area.size() || g_md->area[size_t(pid)] == 0)
            continue;
        bool ok = true;
        for (const QString &p : parts) {
            const QString k = p.section(u':', 0, 0), v = p.section(u':', 1);
            if (k == u"land")
                ok = !pr->sea;
            else if (k == u"owner")
                ok = !pr->sea && g_win->doc->prov_state(pid).owner.value_or(QString()) == v;
            else if (k == u"continent")
                ok = cont && cont->of(pid).contains(v);
            else if (k == u"ids" || k == u"notids") {
                bool in = false;
                for (const QString &s : v.split(u','))
                    in = in || s.toLongLong() == pid;
                ok = k == u"ids" ? in : !in;
            }
            if (!ok)
                break;
        }
        if (ok)
            out.push_back(pid);
    }
    return out;
}

static QRectF box_of(const std::vector<long long> &pids)
{
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (long long p : pids) {
        const double x = g_md->cx[size_t(p)], y = g_md->cy[size_t(p)];
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

static void shot(QWidget *w, const QString &name)
{
    flush(12);
    if (w == g_win.get()) {
        compose().save(QDir(g_out).filePath(name + u".png"_s));
        std::printf("shot %s\n", qUtf8Printable(name));
        return;
    }
    w->grab().save(QDir(g_out).filePath(name + u".png"_s));
    std::printf("shot %s\n", qUtf8Printable(name));
}

static QStringList g_lines;
static qsizetype g_idx = 0;
static int W = 1920, H = 1080;

// the commands, from g_idx on; nested = run inside a dialog (from a timer): it returns at 'endmodal'
static int run_lines(bool nested)
{
    while (g_idx < g_lines.size()) {
        QString line = g_lines[g_idx++].trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const QString cmd = line.section(u' ', 0, 0), rest = line.section(u' ', 1).trimmed();
        const QStringList a = rest.split(u' ', Qt::SkipEmptyParts);
        std::printf("> %s\n", qUtf8Printable(line));
        std::fflush(stdout);
        if (cmd == u"size") {
            W = a[0].toInt(), H = a[1].toInt();
            if (g_win)
                g_win->resize(W, H);
        } else if (cmd == u"open") {
            g_win.reset();
            {   // (the last window's unapplied edits, kept as a recovery file: not offered in a fresh run)
                QDirIterator it(QDir(g_out).filePath(u"backups"_s), {u"*.medrec"_s}, QDir::Files,
                                QDirIterator::Subdirectories);
                while (it.hasNext())
                    QFile::remove(it.next());
            }
            const QStringList p = rest.split(u'|');
            QStringList replace;
            if (p.size() > 2)
                replace = p[2].trimmed().split(u',', Qt::SkipEmptyParts);
            g_mod = std::make_shared<moddata::Mod>(p[0].trimmed(), p[1].trimmed(), std::nullopt, replace);
            g_mod->load();
            g_md = std::make_shared<mapview::MapData>(*g_mod, g_mod->p({u"map"_s, g_mod->provinces_bmp}));
            g_win = std::make_unique<mainwindow::MainWindow>(g_mod, g_md, g_mod->scenarios()[0].name,
                                                             QDir(g_out).filePath(u"backups"_s));
            g_win->resize(W, H);
            g_win->show();
            flush(20);
            std::printf("opened %s: %lld provinces\n", qUtf8Printable(g_mod->name()), (long long)g_mod->provinces.size());
        } else if (cmd == u"reopen") {             // what app.cpp does when a window asked for a re-read (map cut)
            if (!g_win || !g_win->reload_requested) {
                std::printf("  NO RELOAD REQUESTED\n");
                continue;
            }
            const QString note = g_win->reload_note;
            auto again = std::make_shared<moddata::Mod>(g_mod->root, g_mod->base, g_mod->name(), g_mod->replace);
            again->submod = g_mod->submod;
            again->load();
            g_win.reset();
            g_mod = again;
            g_md = std::make_shared<mapview::MapData>(*g_mod, g_mod->p({u"map"_s, g_mod->provinces_bmp}));
            g_win = std::make_unique<mainwindow::MainWindow>(g_mod, g_md, g_mod->scenarios()[0].name,
                                                             QDir(g_out).filePath(u"backups"_s));
            g_win->resize(W, H);
            g_win->show();
            if (!note.isEmpty())
                g_win->message(note, 30000);
            flush(20);
            std::printf("  reopened: map %lld × %lld, %lld provinces\n", (long long)g_md->w, (long long)g_md->h,
                        (long long)g_mod->provinces.size());
        } else if (cmd == u"newmod") {             // newmod <mods dir> | <name>: the open window's "New mod from the base game"
            const QStringList p = rest.split(u'|');
            const auto [folder, modfile] = submods::create_new(p[0].trimmed(), p[1].trimmed());
            std::printf("  made %s + %s\n", qUtf8Printable(folder), qUtf8Printable(modfile));
        } else if (cmd == u"page") {
            g_win->go_page(a[0]);
            flush();
        } else if (cmd == u"mode") {
            const int i = g_win->color_box->findData(a[0]);
            if (i < 0)
                std::printf("  NO MODE %s\n", qUtf8Printable(a[0]));
            else
                g_win->color_box->setCurrentIndex(i);
            flush();
        } else if (cmd == u"modes") {
            for (int i = 0; i < g_win->color_box->count(); ++i)
                std::printf("  %s = %s\n", qUtf8Printable(g_win->color_box->itemData(i).toString()),
                            qUtf8Printable(g_win->color_box->itemText(i)));
        } else if (cmd == u"fit" || cmd == u"fitall") {
            QRectF r = cmd == u"fitall" ? QRectF(0, 0, g_md->w, g_md->h) : box_of(eval_set(a[0]));
            const double pad = a.size() > 1 ? a[1].toDouble() : 60;
            if (cmd == u"fit")
                r = r.adjusted(-pad, -pad, pad, pad);
            g_win->map->fitInView(r, Qt::KeepAspectRatio);
            g_win->map->_zoomed();
            flush();
        } else if (cmd == u"select") {
            g_win->set_selection(eval_set(a[0]));
            flush();
        } else if (cmd == u"clear") {
            g_win->set_selection({});
            flush();
        } else if (cmd == u"count") {
            const auto s = eval_set(a[0]);
            long long people = 0;
            for (long long p : s)
                people += g_win->doc->total(p);
            std::printf("  %zu provinces, %lld people\n", s.size(), people);
        } else if (cmd == u"owner") {
            const QString tag = a[0];
            const auto s = eval_set(a[1]);
            g_win->set_selection(s);
            g_win->on_prov_edit_for(u"Owner → %1"_s.arg(tag), [tag](long long, history::ProvState &st) {
                st.owner = tag;
                st.controller = tag;
                if (!st.cores.contains(tag))
                    st.cores << tag;
            }, QList<long long>(s.begin(), s.end()));
            flush();
            std::printf("  %zu provinces to %s\n", s.size(), qUtf8Printable(tag));
        } else if (cmd == u"pops") {
            const auto s = eval_set(a[0]);
            const QList<long long> pids(s.begin(), s.end());
            g_win->set_selection(s);
            long long people = 0;
            for (long long p : s)
                people += g_win->doc->total(p);
            std::optional<long long> total;
            if (a[3] != u"-")
                total = (long long)(double(people) * a[3].toDouble());
            if (a[1] != u"-")
                g_win->do_pop_changes(pids, document::PopFilter(), total, u"culture"_s, {{a[1], 100.0}});
            else if (total)
                g_win->do_pop_changes(pids, document::PopFilter(), total, u"culture"_s, {});
            if (a[2] != u"-")
                g_win->do_pop_changes(pids, document::PopFilter(), std::nullopt, u"religion"_s, {{a[2], 100.0}});
            flush();
            long long after = 0;
            for (long long p : s)
                after += g_win->doc->total(p);
            std::printf("  %zu provinces: %lld -> %lld people\n", s.size(), people, after);
        } else if (cmd == u"undo" || cmd == u"redo") {   // the main window's Ctrl+Z / Ctrl+Y (a big undo asks twice)
            const std::optional<QString> r = cmd == u"undo" ? g_win->undo() : g_win->redo();
            flush(10);
            std::printf("  %s: %s\n", qUtf8Printable(cmd), r ? qUtf8Printable(*r) : "(nothing)");
        } else if (cmd == u"apply") {
            const bool ok = g_win->save();
            flush(20);
            std::printf("  apply %s, pending now %lld\n", ok ? "ok" : "FAILED", g_win->doc->pending_count());
        } else if (cmd == u"shot") {
            shot(g_win.get(), a[0]);
        } else if (cmd == u"shotmap") {
            shot(g_win->map, a[0]);
        } else if (cmd == u"rec") {               // rec <folder name under out>: start a clip
            g_rec = QDir(g_out).filePath(a[0]);
            QDir(g_rec).removeRecursively();
            QDir().mkpath(g_rec);
            g_frame = 0;
        } else if (cmd == u"endrec") {
            std::printf("  %d frames\n", g_frame);
            g_rec.clear();
        } else if (cmd == u"cursor") {            // cursor x y (window coordinates)
            g_cur = QPointF(a[0].toDouble(), a[1].toDouble());
        } else if (cmd == u"hold") {
            hold(a[0].toInt());
        } else if (cmd == u"caption") {
            g_caption = rest == u"-" ? QString() : rest;
        } else if (cmd == u"moveto") {            // moveto x y frames
            move_to(QPointF(a[0].toDouble(), a[1].toDouble()), a[2].toInt());
        } else if (cmd == u"movepid") {           // movepid pid frames
            move_to(pid_point(a[0].toLongLong()), a[1].toInt());
        } else if (cmd == u"clickpid") {          // clickpid pid [alt|shift|ctrl…]: a real click on the map there
            const long long pid = a[0].toLongLong();
            move_to(pid_point(pid), 0);
            const auto mods = mods_of(a.size() > 1 ? a[1] : QString());
            QWidget *vp = g_win->map->viewport();
            g_down = true;
            hold(2);
            send_mouse(vp, QEvent::MouseButtonPress, g_cur, Qt::LeftButton, mods);
            send_mouse(vp, QEvent::MouseButtonRelease, g_cur, Qt::NoButton, mods);
            g_down = false;
            g_press = 8;
            flush(10);
            std::printf("  selection now %zu provinces\n", g_win->selection.size());
        } else if (cmd == u"showpie") {           // scroll the side panel so the pie is in view
            QWidget *pie = g_win->edit_panel->pie;
            for (QWidget *w = pie->parentWidget(); w; w = w->parentWidget())
                if (auto *sa = qobject_cast<QScrollArea *>(w)) {
                    sa->ensureWidgetVisible(pie, 0, 60);
                    break;
                }
            flush();
        } else if (cmd == u"movepie") {           // movepie edge frames
            move_to(pie_point(a[0].toLongLong()), a[1].toInt());
        } else if (cmd == u"dragpie") {           // dragpie edge to_pct frames: drag that edge round the pie
            widgets::PieChart *pie = g_win->edit_panel->pie;
            const long long edge = a[0].toLongLong();
            double from = 0;
            for (long long i = 0; i <= edge; ++i)
                from += pie->seg()[size_t(i)].pct;
            const double to = a[1].toDouble();
            const int n = a[2].toInt();
            g_cur = pie_point(edge);
            g_down = true;
            send_mouse(pie, QEvent::MouseButtonPress, g_cur, Qt::LeftButton, {});
            hold(3);
            for (int i = 1; i <= n; ++i) {
                g_cur = pie_point(edge, from + (to - from) * ease(double(i) / n));
                send_mouse(pie, QEvent::MouseMove, g_cur, Qt::LeftButton, {});
                frame();
            }
            send_mouse(pie, QEvent::MouseButtonRelease, g_cur, Qt::NoButton, {});
            g_down = false;
            g_press = 8;
            flush(10);
        } else if (cmd == u"movebtn" || cmd == u"clickbtn") {   // movebtn|clickbtn frames text…
            const QString text = rest.section(u' ', 1);
            QPushButton *b = button_named(text);
            if (!b) {
                std::printf("  NO BUTTON %s\n", qUtf8Printable(text));
                continue;
            }
            move_to(QPointF(b->mapTo(g_win.get(), b->rect().center())), a[0].toInt());
            if (cmd == u"clickbtn") {
                g_down = true;
                hold(2);
                g_down = false;
                g_press = 8;
                b->click();
                flush(12);
            }
        } else if (cmd == u"zoompid") {           // zoompid pid zoom
            g_win->zoom_to_point(g_md->cx[size_t(a[0].toLongLong())] + 0.5, g_md->cy[size_t(a[0].toLongLong())] + 0.5,
                                 a[1].toDouble());
            flush();
        } else if (cmd == u"pidof") {             // pidof <set>: the first few provinces of a set (to find one)
            const auto s = eval_set(a[0]);
            QStringList ids;
            for (size_t i = 0; i < s.size() && i < 12; ++i)
                ids << QString::number(s[i]);
            std::printf("  %s\n", qUtf8Printable(ids.join(u", "_s)));
        } else if (cmd == u"pie") {               // pie: the segments now
            for (const auto &sg : g_win->edit_panel->pie->seg())
                std::printf("  %s %.2f\n", qUtf8Printable(sg.label), sg.pct);
        } else if (cmd == u"errors") {
            g_win->_checks_due();
            flush(20);
            std::printf("  errors badge: %s\n", qUtf8Printable(g_win->err_badge->text()));
        } else if (cmd == u"endmodal") {          // (the end of the commands run inside a dialog)
            if (nested) {
                if (QWidget *m = QApplication::activeModalWidget()) {   // still open: never hang
                    std::printf("  DIALOG STILL OPEN (%s) - closed\n", m->metaObject()->className());
                    if (auto *d = qobject_cast<QDialog *>(m))
                        d->reject();
                }
                return 0;
            }
        } else if (cmd == u"click" || cmd == u"move") {   // click n [alt shift ctrl right dbl modal] <locator>
            int i = 1;
            QStringList flags;
            static const QStringList FL = {u"alt"_s, u"shift"_s, u"ctrl"_s, u"right"_s, u"dbl"_s, u"modal"_s};
            while (i < a.size() && FL.contains(a[i]))
                flags << a[i++];
            const QString loc = a.mid(i).join(u' ');
            const Spot sp = locate(loc);
            if (!sp.w) {
                std::printf("  NOT FOUND %s\n", qUtf8Printable(loc));
                continue;
            }
            move_to(win_pt(sp), a[0].toInt());
            if (cmd == u"click") {
                if (flags.contains(u"modal"))
                    QTimer::singleShot(250, [] { run_lines(true); });
                Qt::KeyboardModifiers m = mods_of(flags.join(u' '));
                const QPointer<QWidget> alive(sp.w);   // (a picker closes itself on the click)
                real_click(sp, m, flags.contains(u"right"), flags.contains(u"dbl"));
                if (!alive)
                    continue;
                QWidget *hit = sp.w->childAt(sp.at.toPoint());   // the deepest widget under the pointer
                if (!hit)
                    hit = sp.w;
                if (auto *spin = qobject_cast<QAbstractSpinBox *>(hit))
                    hit = spin->findChild<QLineEdit *>();
                if (hit && qobject_cast<QLineEdit *>(hit)) {     // typing goes there next — never to the map
                    g_typed = hit;
                    hit->setFocus(Qt::MouseFocusReason);
                } else {
                    g_typed.clear();                              // (a missed field: never a stale one)
                }
                if (auto *cb = qobject_cast<QComboBox *>(sp.w))   // (the offscreen screen is small: a picker opens
                    for (QWidget *t : QApplication::topLevelWidgets())   //  above it there — put it under the box)
                        if (t != g_win.get() && t->isVisible() && t->windowFlags().testFlag(Qt::Popup)) {
                            t->move(cb->mapToGlobal(QPoint(0, cb->height() + 2)));
                            if (auto *le = t->findChild<QLineEdit *>())
                                g_typed = le;
                        }
            }
        } else if (cmd == u"type") {              // type <frames per letter> [locator] :: text
            const QString loc = rest.section(u"::"_s, 0, 0).section(u' ', 1).trimmed();
            const QString text = rest.section(u"::"_s, 1).trimmed();
            QWidget *w = g_typed.data();
            if (!loc.isEmpty()) {
                const Spot sp = locate(loc);
                w = sp.w;
            }
            if (!w)
                w = QApplication::focusWidget();
            if (auto *spin = w ? qobject_cast<QAbstractSpinBox *>(w) : nullptr)
                w = spin->findChild<QLineEdit *>();
            if (!w || !w->isVisible() ||
                !(qobject_cast<QLineEdit *>(w) || w->inherits("QTextEdit") || w->inherits("QPlainTextEdit"))) {
                // never into the map or a page: letters and digits are the editor's shortcuts there
                std::printf("  NOTHING TO TYPE INTO (%s)\n", w ? w->metaObject()->className() : "none");
                continue;
            }
            g_typed = w;
            for (QChar ch : text) {
                const int key = ch.isLetter() ? int(ch.toUpper().unicode()) : int(ch.unicode());
                send_key(w, key, Qt::NoModifier, QString(ch));
                hold(a[0].toInt());
            }
            flush(6);
        } else if (cmd == u"key") {               // key <name> [locator]
            QWidget *w = a.size() > 1 ? locate(a.mid(1).join(u' ')).w : g_typed.data();
            if (!w)
                w = QApplication::focusWidget();
            if (w && !w->isVisible()) {           // (a field of a page gone from view: never)
                std::printf("  KEY: target hidden\n");
                continue;
            }
            static const QHash<QString, int> KEYS = {{u"Return"_s, Qt::Key_Return}, {u"Escape"_s, Qt::Key_Escape},
                                                     {u"Tab"_s, Qt::Key_Tab}, {u"Backspace"_s, Qt::Key_Backspace},
                                                     {u"Down"_s, Qt::Key_Down}, {u"Up"_s, Qt::Key_Up},
                                                     {u"Delete"_s, Qt::Key_Delete}};
            if (a[0] == u"ctrl+a")
                send_key(w, Qt::Key_A, Qt::ControlModifier, u"\x01"_s);
            else if (w)
                send_key(w, KEYS.value(a[0]), Qt::NoModifier,
                         a[0] == u"Return" ? u"\r"_s : a[0] == u"Escape" ? u"\x1b"_s : QString());
            flush(10);
        } else if (cmd == u"slide") {             // slide n <value> <locator>: a slider moved by the pointer
            const Spot sp = locate(a.mid(2).join(u' '));
            QSlider *sl = sp.w ? qobject_cast<QSlider *>(sp.w) : nullptr;
            if (!sl && sp.w)
                sl = sp.w->findChild<QSlider *>();
            if (!sl) {
                std::printf("  NO SLIDER\n");
                continue;
            }
            auto handle = [sl] {
                QStyleOptionSlider o;
                o.initFrom(sl);
                o.orientation = sl->orientation(), o.minimum = sl->minimum(), o.maximum = sl->maximum();
                o.sliderPosition = sl->value(), o.sliderValue = sl->value();
                const QRect r = sl->style()->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, sl);
                return QPointF(g_win->mapFromGlobal(sl->mapToGlobal(r.center())));
            };
            move_to(handle(), a[0].toInt());
            const int from = sl->value(), to = a[1].toInt(), n = std::max(1, a[0].toInt());
            g_down = true;
            hold(2);
            sl->setSliderDown(true);
            for (int k = 1; k <= n; ++k) {
                sl->setSliderPosition(int(std::lround(from + (to - from) * ease(double(k) / n))));
                g_cur = handle();
                frame();
            }
            sl->setSliderDown(false);
            g_down = false;
            g_press = 8;
            flush(10);
        } else if (cmd == u"stepdrag") {          // stepdrag n <index> <locator of a StepSlider>
            const Spot sp = locate(a.mid(2).join(u' '));
            auto *ss = sp.w ? qobject_cast<widgets::StepSlider *>(sp.w) : nullptr;
            if (!ss) {
                std::printf("  NO STEP SLIDER\n");
                continue;
            }
            const double pad = 34, w = std::max(1, int(ss->width() - 2 * pad));
            auto xof = [&](int i) { return pad + w * double(i) / double(ss->names.size() - 1); };
            const double y = ss->height() / 2.0;
            const int n = std::max(1, a[0].toInt());
            const QPointF p0(xof(ss->value()), y), p1(xof(a[1].toInt()), y);
            move_to(QPointF(g_win->mapFromGlobal(ss->mapToGlobal(p0.toPoint()))), n);
            g_down = true;
            hold(2);
            real_mouse(ss, QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton, {}, false);
            for (int k = 1; k <= n; ++k) {
                const QPointF q = p0 + (p1 - p0) * ease(double(k) / n);
                real_mouse(ss, QEvent::MouseMove, q, Qt::NoButton, Qt::LeftButton, {}, false);
                g_cur = QPointF(g_win->mapFromGlobal(ss->mapToGlobal(q.toPoint())));
                frame();
            }
            real_mouse(ss, QEvent::MouseButtonRelease, p1, Qt::LeftButton, Qt::NoButton, {}, false);
            g_down = false;
            g_press = 8;
            flush(10);
        } else if (cmd == u"paint" || cmd == u"drag") {   // paint n x,y x,y … (map pixels) | drag n <loc> -> <loc>
            QWidget *w = nullptr;
            std::vector<QPointF> pts;          // in w
            if (cmd == u"paint") {
                w = g_win->map->viewport();
                for (int k = 1; k < a.size(); ++k)
                    pts.push_back(QPointF(g_win->map->mapFromScene(
                        QPointF(a[k].section(u',', 0, 0).toDouble(), a[k].section(u',', 1).toDouble()))));
            } else {
                const Spot s0 = locate(rest.section(u' ', 1).section(u"->"_s, 0, 0));
                const Spot s1 = locate(rest.section(u"->"_s, 1));
                if (!s0.w || !s1.w) {
                    std::printf("  DRAG: NOT FOUND\n");
                    continue;
                }
                w = s0.w;
                pts = {s0.at, QPointF(w->mapFromGlobal(s1.w->mapToGlobal(s1.at.toPoint())))};
            }
            const int n = std::max(1, a[0].toInt());
            auto wp = [w](QPointF q) { return QPointF(g_win->mapFromGlobal(w->mapToGlobal(q.toPoint()))); };
            move_to(wp(pts[0]), std::min(n, 20));
            g_down = true;
            hold(2);
            real_mouse(w, QEvent::MouseButtonPress, pts[0], Qt::LeftButton, Qt::LeftButton, {}, false);
            double len = 0;                    // along the polyline, evenly
            std::vector<double> acc{0};
            for (size_t k = 1; k < pts.size(); ++k)
                acc.push_back(len += std::hypot(pts[k].x() - pts[k - 1].x(), pts[k].y() - pts[k - 1].y()));
            const int sub = 4;                 // mouse moves per frame (a smooth stroke)
            for (int k = 1; k <= n * sub; ++k) {
                const double d = len * double(k) / (n * sub);
                size_t j = 1;
                while (j + 1 < acc.size() && acc[j] < d)
                    ++j;
                const double t = acc[j] > acc[j - 1] ? (d - acc[j - 1]) / (acc[j] - acc[j - 1]) : 1.0;
                const QPointF q = pts[j - 1] + (pts[j] - pts[j - 1]) * std::clamp(t, 0.0, 1.0);
                real_mouse(w, QEvent::MouseMove, q, Qt::NoButton, Qt::LeftButton, {}, false);
                g_cur = wp(q);
                if (k % sub == 0)
                    frame();
            }
            real_mouse(w, QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton, Qt::NoButton, {}, false);
            g_down = false;
            g_press = 8;
            flush(10);
        } else if (cmd == u"hover") {             // hover n <locator>: the pointer goes there, the widget sees it move
            const Spot sp = locate(a.mid(1).join(u' '));
            if (!sp.w)
                continue;
            move_to(win_pt(sp), a[0].toInt());
            real_mouse(sp.w, QEvent::MouseMove, sp.at, Qt::NoButton, Qt::NoButton, {});
            flush(6);
        } else if (cmd == u"wheel") {             // wheel <steps> <locator>
            const Spot sp = locate(a.mid(1).join(u' '));
            if (!sp.w)
                continue;
            for (int k = 0; k < std::abs(a[0].toInt()); ++k) {
                QWheelEvent ev(sp.at, sp.w->mapToGlobal(sp.at), QPoint(), QPoint(0, a[0].toInt() > 0 ? 120 : -120),
                               Qt::NoButton, {}, Qt::NoScrollPhase, false);
                QApplication::sendEvent(sp.w, &ev);
                frame();
            }
        } else if (cmd == u"see") {               // see <locator>: scroll it into view
            g_any = true;
            const Spot sp = locate(rest);
            g_any = false;
            for (QWidget *w = sp.w ? sp.w->parentWidget() : nullptr; w; w = w->parentWidget())
                if (auto *sa = qobject_cast<QScrollArea *>(w)) {
                    sa->ensureWidgetVisible(sp.w, 0, 120);
                    break;
                }
            flush();
        } else if (cmd == u"wait") {
            wait_ms(a[0].toInt());
        } else if (cmd == u"fitrect") {           // fitrect x0 y0 x1 y1 (map pixels)
            g_win->map->fitInView(QRectF(QPointF(a[0].toDouble(), a[1].toDouble()), QPointF(a[2].toDouble(), a[3].toDouble())),
                                  Qt::KeepAspectRatio);
            g_win->map->_zoomed();
            flush();
        } else if (cmd == u"centercard") {        // centercard kind:key [zoom]
            prodpanel::ProdChart *ch = g_win->prod_ws->chart;
            ch->center_on(prodtree::Cid{rest.section(u' ', 0, 0).section(u':', 0, 0),
                                        rest.section(u' ', 0, 0).section(u':', 1), 0});
            if (a.size() > 1) {
                const QPointF c(ch->width() / 2.0, ch->height() / 2.0), sc = ch->_scene(c);
                ch->zoom = a[1].toDouble();
                ch->off = c - sc * ch->zoom;
            }
            ch->update();
            flush();
        } else if (cmd == u"where") {             // where <locator>: print it
            const Spot sp = locate(rest);
            if (sp.w) {
                const QPointF p = win_pt(sp);
                std::printf("  %s at %.0f,%.0f\n", sp.w->metaObject()->className(), p.x(), p.y());
            } else {
                std::printf("  NOT FOUND\n");
            }
        } else if (cmd == u"pos") {               // pos pid…: middles in map pixels
            for (const QString &x : a)
                std::printf("  %s: %.0f,%.0f\n", qUtf8Printable(x), g_md->cx[size_t(x.toLongLong())],
                            g_md->cy[size_t(x.toLongLong())]);
        } else if (cmd == u"state") {             // state <locator>: a button's tick / a widget's being on show
            const Spot sp = locate(rest);
            if (auto *b = sp.w ? qobject_cast<QAbstractButton *>(sp.w) : nullptr)
                std::printf("  checked=%d enabled=%d mode=%s\n", b->isChecked(), b->isEnabled(),
                            qUtf8Printable(g_win->mode));
            else
                std::printf("  %s\n", sp.w ? "found" : "NOT FOUND");
        } else if (cmd == u"techs") {             // the tech cards on show
            QStringList ks;
            for (techpanel::TechGrid *g : all_shown<techpanel::TechGrid>())
                for (const auto &[r, key] : g->_cells)
                    ks << key;
            std::printf("  %s\n", qUtf8Printable(ks.join(u' ')));
        } else if (cmd == u"snap") {              // snap <name>: keep the window as it looks now
            flush(12);
            g_snaps[a[0]] = compose();
        } else if (cmd == u"wipe") {              // wipe <snap> <frames> [back]: sweep from before to now
            g_wipe_img = &g_snaps[a[0]];
            g_wipe_rect = QRect(g_win->map->viewport()->mapTo(g_win.get(), QPoint(0, 0)), g_win->map->viewport()->size());
            const int n = std::max(1, a[1].toInt());
            const bool back = a.size() > 2;
            for (int k = 0; k <= n; ++k) {
                const double t = ease(double(k) / n);
                g_wipe_x = g_wipe_rect.left() + g_wipe_rect.width() * (back ? 1.0 - t : t);
                frame();
            }
            if (!back) {
                g_wipe_x = -1;
                g_wipe_img = nullptr;
            }
        } else if (cmd == u"wipehold") {          // wipehold <snap> <frames>: the whole map as it was
            g_wipe_img = &g_snaps[a[0]];
            g_wipe_rect = QRect(g_win->map->viewport()->mapTo(g_win.get(), QPoint(0, 0)), g_win->map->viewport()->size());
            g_wipe_x = g_wipe_rect.left();
            hold(a[1].toInt());
        } else if (cmd == u"wipeoff") {
            g_wipe_x = -1;
            g_wipe_img = nullptr;
        } else if (cmd == u"cards") {             // cards <kind>: the keys of the chart's cards of that kind
            QStringList ks;
            for (const auto &[n, r] : g_win->prod_ws->chart->nodes)
                if (n.kind == rest)
                    ks << n.key;
            std::printf("  %s\n", qUtf8Printable(ks.join(u' ')));
        } else if (cmd == u"tops") {              // the windows on show
            for (QWidget *t : QApplication::topLevelWidgets())
                if (t->isVisible())
                    std::printf("  %s '%s' %dx%d\n", t->metaObject()->className(), qUtf8Printable(t->windowTitle()),
                                t->width(), t->height());
        } else if (cmd == u"labels") {            // labels <contains>: texts on show (to find a locator)
            for (QWidget *w : all_shown<QWidget>()) {
                const QString t = text_of(w);
                if (!t.isEmpty() && (rest.isEmpty() || t.contains(rest)))
                    std::printf("  %s: %s\n", w->metaObject()->className(), qUtf8Printable(t.left(90)));
            }
        } else {
            std::printf("  UNKNOWN %s\n", qUtf8Printable(cmd));
        }
        std::fflush(stdout);
    }
    return 0;
}

static int run(const QString &cmdfile)
{
    QFile f(cmdfile);
    if (!f.open(QIODevice::ReadOnly))
        return 2;
    g_lines = QString::fromUtf8(f.readAll()).split(u'\n');
    run_lines(false);
    g_win.reset();
    return 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    theme::apply(app);
    if (argc < 3)
        return 2;
    g_out = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(g_out);
    return run(QString::fromLocal8Bit(argv[1]));
}
