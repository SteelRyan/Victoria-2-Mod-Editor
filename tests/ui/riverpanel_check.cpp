// riverpanel against Python, the C++ side of tools/check_riverpanel.py.
// Usage: med_riverpanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "grid/random.h"
#include "map/mismatch.h"
#include "ui/mapview.h"
#include "ui/riverpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace riverpanel;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}
static QString hs(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return h(b.constData(), size_t(b.size()));
}
static QString bl(std::initializer_list<bool> xs)
{
    QStringList p;
    for (bool x : xs)
        p << v(x);
    return pyform::list(p);
}
static QString num(double x, bool as_int) { return as_int ? QString::number((long long)x) : v(x); }
static QString tagv(const std::optional<QVariantList> &t)
{
    if (!t)
        return u"None"_s;
    QStringList p;
    for (const QVariant &x : *t)
        p << (x.typeId() == QMetaType::QString ? v(x.toString()) : v(x.toLongLong()));
    return pyform::list(p);
}
static QString pts(const std::vector<QPointF> &ps)
{
    QStringList p;
    for (const QPointF &q : ps)
        p << L(q.x(), q.y());
    return pyform::list(p);
}

struct Panel : drawlink::Panel {                     // (the Draw panel's part the box reads)
    Panel(moddata::Mod &m, mapview::MapData &md, layerbmp::LandSea *ls) : m(m), md(md), ls(ls) {}
    moddata::Mod &m;
    mapview::MapData &md;
    layerbmp::LandSea *ls;
    std::unique_ptr<mismatch::Mismatch> mm;
    QStringList zooms;
    mismatch::Mismatch *mismatch() override
    {
        if (!mm)
            mm = std::make_unique<mismatch::Mismatch>(m, md, ls);
        return mm.get();
    }
    void zoom_requested(double x, double y) override { zooms << L(x, y); }
};

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    RiverBox::random_seed = [] { return (long long)grid::Generator(12345).integers(1, 1LL << 30); };
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;

    Panel panel(*m, md, win.landsea.get());
    panel.view = win.map;
    QPushButton edit;
    edit.setCheckable(true);
    edit.setChecked(true);
    panel.edit_btn = &edit;
    QStringList out, msgs, banners;
    auto &rb = doc.map<layerbmp::IndexedBitmap>(u"rivers_bmp"_s);
    RiverBox box(*m, md, &panel);
    QObject::connect(&box, &RiverBox::message, [&](const QString &t) { msgs << t; });
    QObject::connect(&box, &RiverBox::overlayChanged, [&] { win.map->shape_layer->set_shapes(box.shapes()); });
    QObject::connect(&box, &RiverBox::bannerChanged, [&] { banners << box.banner(); });
    box.setFixedWidth(470);
    box.refresh(&doc);
    box.show();
    QApplication::processEvents();
    RiverHandler &hd = box.handler;

    auto data_h = [&] { return h(rb.data->flat().data(), rb.data->flat().size()); };
    auto plan_line = [&] {
        const RiverPlan &p = box.plan();
        QStringList br;
        for (const Branch &b : p.branches) {
            QString via = u"None"_s;
            if (b.via) {
                QStringList q;
                for (const Pt &x : *b.via)
                    q << v(x);
                via = pyform::list(q);
            }
            br << L(b.at, b.length, b.source, b.parent, b.await_) .chopped(1) + u", "_s + via + u"]"_s;
        }
        std::vector<std::pair<long long, std::pair<int, int>>> ws;
        for (const auto &[i, ac] : p.written)
            ws.emplace_back(i, ac);
        std::sort(ws.begin(), ws.end());
        QStringList wr;
        for (const auto &[i, ac] : ws)
            wr << u"%1:%2:%3"_s.arg(i).arg(ac.first).arg(ac.second);
        QStringList paths;
        for (const auto &[k, ix] : p.paths)
            paths << L(k, (long long)ix.size(), h(ix.data(), ix.size() * 8));
        QStringList via;
        for (const Pt &q : p.via)
            via << v(q);
        return u"  plan stage %1 start %2 end %3 via %4 mouth %5 seed %6 written %7 %8 info %9\n  branches %10\n"
               u"  paths %11"_s.arg(v(p.stage), v(p.start), v(p.end), pyform::list(via), v(p.mouth))
            .arg(p.seed)
            .arg((long long)p.written.size())
            .arg(hs(wr.join(u';')), v(p.info), pyform::list(br), pyform::list(paths));
    };
    auto shapes = [&] {
        QStringList sh;
        for (const mapview::Shape &s : box.shapes()) {
            const auto &[r, g, bb] = s.color;
            sh << L(s.kind).chopped(1) + u", "_s + pts(s.pts) +
                      u", [%1, %2, %3], "_s.arg(r).arg(g).arg(bb) + tagv(s.tag) + u", "_s +
                      (s.size ? num(*s.size, *s.size == std::floor(*s.size)) : u"None"_s) + u", "_s +
                      (s.width ? num(*s.width, s.kind == u"line") : u"None"_s) + u", "_s +
                      (s.kind == u"circle" ? v(s.radius) : u"None"_s) + u", "_s + v(s.dash) + u", "_s + v(s.halo) +
                      u"]"_s;
        }
        return pyform::list(sh);
    };
    auto ui = [&] {
        QStringList st;
        for (const Step &s : STEPS)
            st << L(s.key, box.step_lbls.at(s.key)->text());
        return pyform::list(st) + u" vis "_s +
               bl({box.new_btn->isVisible(), box.steps_host->isVisible(), box.done_host->isVisible(),
                   box.cur_host->isVisible(), box.plan_help->isVisible(), box.next_btn->isVisible(),
                   box.auto_btn->isVisible(), box.auto2_btn->isVisible(), box.sys_section->isVisible(),
                   box.sel_section->isVisible(), box.pen_section->isVisible(), box.size_row->isVisible(),
                   box.scope_row->isVisible()}) +
               u" en "_s +
               bl({box.back_btn->isEnabled(), box.gen_btn->isEnabled(), box.rmnode_btn->isEnabled(),
                   box.prev_node->isEnabled(), box.next_node->isEnabled(), box.sel_regen->isEnabled(),
                   box.sel_del->isEnabled(), box.sel_clear->isEnabled()}) +
               u" next "_s + v(box.next_btn->text());
    };
    auto state = [&](const QString &what) {
        out << u"%1: version %2 data %3 status %4 tool %5 value %6"_s.arg(what).arg(rb.version).arg(
            data_h(), v(box.status->text()), box.tool()).arg(box.value);
        out << plan_line();
        out << u"  ui "_s + ui();
        out << u"  cur %1 node %2 armed %3 cur_info %4 plan_info %5 sel_info %6 val %7"_s.arg(
            v(box.cur_branch()), box.cur_node ? tagv(box.cur_node->list()) : u"None"_s, v(box.armed),
            v(box.cur_info->text()), v(box.plan_info->text()), v(box.sel_info->text()), v(box.val_info->text()));
        out << u"  selected "_s + (box.selected ? u"%1 %2"_s.arg(box.selected->size()).arg(
                                                     h(box.selected->data(), box.selected->size() * 8))
                                                : u"None"_s);
        out << u"  shapes "_s + shapes();
        out << u"  banner %1 banners %2 zooms %3"_s.arg(v(box.banner()), v(banners), pyform::list(panel.zooms));
        out << u"  msgs "_s + v(msgs);
        msgs.clear();
        banners.clear();
        panel.zooms.clear();
    };
    auto click = [&](double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        hd.press(x, y, 1.0, mods);
        hd.release(x, y, 1.0, mods);
    };
    auto temp = [&](bool radius) {
        QStringList t;
        for (const auto &s : win.map->shape_layer->temp)
            t << L(s.kind).chopped(1) + u", "_s + pts(s.pts) + (radius ? u", "_s + v(s.radius) : QString()) + u"]"_s;
        return pyform::list(t);
    };

    state(u"start"_s);
    for (const auto &[x, y] : {std::pair{1981LL, 833LL}, std::pair{1932LL, 818LL}, std::pair{2482LL, 157LL},
                               std::pair{0LL, 0LL}, std::pair{5039LL, 2159LL}})
        out << u"pixel_info %1 %2 %3"_s.arg(x).arg(y).arg(v(box.pixel_info(x, y)));
    box.grab().save(QDir(out_dir).filePath(u"box_start.png"_s));
    // --- the guide
    box.new_btn->click();
    state(u"new"_s);
    click(1981.3, 833.4);
    state(u"source"_s);
    click(1932.2, 818.6);
    state(u"mouth"_s);
    box.back_btn->click();
    state(u"back"_s);
    click(1933.7, 819.1);
    click(1958.4, 829.2);
    state(u"via"_s);
    box.next_btn->click();
    state(u"next"_s);
    click(1970.5, 832.0);
    state(u"branch join"_s);
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_guide.png"_s));
    box.auto_btn->click();
    state(u"auto source"_s);
    box.gen_btn->click();
    state(u"generated"_s);
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_done.png"_s));
    box.regen_btn->click();
    state(u"regenerated"_s);
    box.meander->setValue(8);
    state(u"meander 8"_s);
    {
        const int i = box.width->findData(box.width->itemData((box.width->currentIndex() + 1) % box.width->count()));
        box.width->setCurrentIndex(i);
        Q_EMIT box.width->activated(i);
        const QVariant d = box.width->currentData();
        state(u"width "_s + (d.typeId() == QMetaType::QString ? v(d.toString()) : v(d.toLongLong())));
    }
    // its points
    {
        QStringList ns;
        for (const Tag &t : box.nodes())
            ns << tagv(t.list());
        out << u"nodes "_s + pyform::list(ns);
    }
    const auto [bx, by] = box.plan().branches[0].at;
    click(bx, by);
    state(u"branch picked"_s);
    box.next_node->click();
    box.prev_node->click();
    state(u"◂ ▸"_s);
    box.blen->setValue(60);
    state(u"branch length 60"_s);
    box.addpt_btn->click();
    state(u"＋ point armed"_s);
    click(1975.2, 842.7);
    state(u"＋ point"_s);
    box.select_node(Tag{u"start"_s, 0, std::nullopt}, true);
    box.addbr_btn->click();
    click(1950.5, 824.5);
    state(u"＋ branch join"_s);
    click(1945.3, 845.1);
    state(u"＋ branch source"_s);
    box.remove_node();
    state(u"✕ point"_s);
    {
        const auto [vx, vy] = box.plan().via[0];
        hd.press(vx, vy, 1.0, Qt::NoModifier);
        hd.move(vx + 3, vy + 2);
        out << u"drag temp "_s + temp(false);
        hd.release(vx + 4.26, vy + 2.71, 1.0, Qt::NoModifier);
    }
    state(u"drag via"_s);
    hd.rclick(1964.0, 841.0, 1.0, Qt::NoModifier);
    state(u"rclick add"_s);
    {
        const auto [vx, vy] = box.plan().via.back();
        hd.rclick(vx, vy, 1.0, Qt::NoModifier);
    }
    state(u"rclick remove"_s);
    box.done_btn->click();
    state(u"done"_s);
    // a second system: deleted; a third: cancelled
    box.new_btn->click();
    click(3580.2, 423.1);
    click(3618.4, 468.3);
    box.gen_btn->click();
    state(u"second generated"_s);
    box.del_btn->click();
    state(u"second deleted"_s);
    box.new_btn->click();
    click(3580.2, 423.1);
    box.cancel_btn->click();
    state(u"cancelled"_s);
    // --- select a real network
    box.tool_btns[u"select"_s]->setChecked(true);
    state(u"select tool"_s);
    click(2482.4, 157.3);
    state(u"select river"_s);
    click(2482.4, 157.3, Qt::ShiftModifier);
    state(u"select network"_s);
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_select.png"_s));
    {
        RiverView view(md, rb, &box);
        for (const auto &[y0, y1, x0, x1] :
             {std::array<long long, 4>{120, 280, 2400, 2560}, std::array<long long, 4>{780, 880, 1900, 2010}}) {
            const mapview::RgbImage c = view.colors(y0, y1, x0, x1);
            const auto t = view.on_top(y0, y1, x0, x1);
            QString top = u"None"_s;
            if (t && !t->empty()) {
                QStringList parts;
                for (const auto &tp : *t) {
                    long long s = 0;
                    for (auto x : tp.mask.flat())
                        s += x;
                    const auto [r, g, b] = *tp.rgb;
                    parts << u"[%1, [%2, %3, %4]]"_s.arg(s).arg(r).arg(g).arg(b);
                }
                top = pyform::list(parts);
            }
            out << u"view %1 %2 %3 %4 [%5, %6, 3] %7 top %8"_s.arg(y0).arg(y1).arg(x0).arg(x1).arg(c.h()).arg(c.w()).arg(
                h(c.flat().data(), c.flat().size() * 3), top);
        }
    }
    auto undo = [&] {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
    };
    box.sel_regen->click();
    state(u"regenerate selected"_s);
    undo();
    box.refresh(&doc);
    state(u"undo regenerate selected"_s);
    box.tool_btns[u"select"_s]->setChecked(true);
    click(2482.4, 157.3);
    box.sel_del->click();
    state(u"delete selected"_s);
    undo();
    box.refresh(&doc);
    click(2470.0, 30.0);
    state(u"select nothing"_s);
    // --- pen / eraser / fix
    box.tool_btns[u"pen"_s]->setChecked(true);
    box.sw[6]->click();
    state(u"pen 6"_s);
    hd.press(1990.2, 836.6, 1.0, Qt::NoModifier);
    for (int k = 1; k < 7; ++k)
        hd.move(1990.2 + 4 * k, 836.6 + k);
    hd.release(2015.0, 843.0, 1.0, Qt::NoModifier);
    state(u"pen stroke"_s);
    box.pen_scope->setCurrentIndex(1);
    long long sx = 1932, sy = 818;
    {
        bool found = false;
        for (long long y = sy - 30; y < sy + 30 && !found; ++y)
            for (long long x = sx - 30; x < sx + 30; ++x)
                if (md.sea[md.idmap(y, x)]) {
                    sx = x;
                    sy = y;
                    found = true;
                    break;
                }
    }
    hd.press(double(sx) + 0.5, double(sy) + 0.5, 1.0, Qt::NoModifier);
    hd.move(double(sx) + 6.5, double(sy) + 0.5);
    hd.release(double(sx) + 6.5, double(sy) + 0.5, 1.0, Qt::NoModifier);
    state(u"pen on sea"_s);
    box.show();
    QApplication::processEvents();
    box.grab().save(QDir(out_dir).filePath(u"box_pen.png"_s));
    box.tool_btns[u"fix"_s]->setChecked(true);
    box.size_spin->setValue(9);
    hd.press(double(sx) + 3.0, double(sy) + 0.5, 1.0, Qt::NoModifier);
    hd.move(double(sx) + 7.0, double(sy) + 1.5);
    out << u"fix temp "_s + temp(true);
    hd.release(double(sx) + 7.0, double(sy) + 1.5, 1.0, Qt::NoModifier);
    state(u"fix brush"_s);
    box.tool_btns[u"eraser"_s]->setChecked(true);
    hd.press(1995.0, 838.0, 1.0, Qt::NoModifier);
    hd.move(2005.0, 840.0);
    hd.release(2005.0, 840.0, 1.0, Qt::NoModifier);
    state(u"eraser"_s);
    for (const auto &[steps, ctrl] :
         {std::pair{1.0, true}, std::pair{2.5, true}, std::pair{-1.0, true}, std::pair{1.0, false}}) {
        const bool r = hd.wheel(steps, ctrl ? drawlink::BRUSH_KEY : Qt::NoModifier);   // (Python: Ctrl)
        out << u"wheel %1 %2 -> %3 size %4"_s.arg(steps == int(steps) ? QString::number(int(steps)) : v(steps), v(ctrl),
                                                  v(r), v((long long)box.size_spin->value()));
    }
    box.tool_btns[u"pen"_s]->setChecked(true);
    hd.rclick(2482.0, 157.0, 1.0, Qt::NoModifier);
    state(u"rclick pick"_s);
    hd.press(1950.0, 860.0, 1.0, Qt::NoModifier);
    hd.move(1953.0, 860.0);
    {
        const bool c = hd.cancel();
        out << u"cancel %1 %2"_s.arg(v(c), data_h());
    }
    for (int k = 0; k < 12; ++k)
        undo();
    box.refresh(&doc);
    state(u"after undo"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%d lines\n", int(out.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
