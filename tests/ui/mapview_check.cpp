// mapview's widget half against Python, the C++ side of tools/check_mapview.py (the same scene, the same steps).
// Usage: med_mapview --dump <out folder> <work folder>
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/mapview.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using namespace mapview;
using pyform::L;
using pyform::v;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}

static QString qh(QImage img)                          // a picture's pixels (as ARGB32) as a hash, with its size
{
    img = img.convertToFormat(QImage::Format_ARGB32);
    return u"%1x%2 %3"_s.arg(img.width()).arg(img.height()).arg(h(img.constBits(), size_t(img.sizeInBytes())));
}

static QString box_v(const std::optional<std::array<long long, 4>> &b)
{
    return b ? L((*b)[0], (*b)[1], (*b)[2], (*b)[3]) : u"None"_s;
}

static QString tag_v(const std::optional<QVariantList> &t)
{
    if (!t)
        return u"None"_s;
    QStringList p;
    for (const QVariant &x : *t)
        p << (x.typeId() == QMetaType::QString ? v(x.toString()) : v(x.toLongLong()));
    return pyform::list(p);
}

struct Layer : PixelLayer {
    bool pure;
    explicit Layer(bool p) : pure(p) {}
    bool pure_pixels() const override { return pure; }
    RgbImage colors(long long y0, long long y1, long long x0, long long x1) override
    {
        RgbImage o(y1 - y0, x1 - x0);
        for (long long y = y0; y < y1; ++y)
            for (long long x = x0; x < x1; ++x)
                o(y - y0, x - x0) = {std::uint8_t((x * 7) % 256), std::uint8_t((y * 5) % 256),
                                     std::uint8_t(((x + y) * 3) % 256)};
        return o;
    }
    std::optional<std::vector<Top>> on_top(long long y0, long long y1, long long x0, long long x1) override
    {
        grid::Grid<std::uint8_t> a(y1 - y0, x1 - x0), b(y1 - y0, x1 - x0);
        for (long long y = y0; y < y1; ++y)
            for (long long x = x0; x < x1; ++x) {
                a(y - y0, x - x0) = (x + y) % 50 == 0;
                b(y - y0, x - x0) = (x * 3 + y) % 97 == 0;
            }
        return std::vector<Top>{{a, Rgb{255, 0, 0}}, {b, std::nullopt}};
    }
};

static Shape shape(const QString &kind, std::vector<QPointF> pts, Rgb color)
{
    Shape s;
    s.kind = kind;
    s.pts = std::move(pts);
    s.color = color;
    return s;
}

static QVariantList tag(const QString &k, int i) { return {k, i}; }

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    moddata::Mod m(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                   {u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s, u"gfx/flags"_s});
    m.load();
    MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
    const long long n = md.n;
    MapView view;
    view.resize(900, 600);
    view.show();
    QApplication::processEvents();
    QStringList sig;
    QObject::connect(&view, &MapView::provinceClicked, [&](int p, Qt::KeyboardModifiers mods) {
        sig << u"click %1 %2"_s.arg(p).arg(mods.toInt());
    });
    QObject::connect(&view, &MapView::provinceRightClicked, [&](int p, Qt::KeyboardModifiers mods) {
        sig << u"rclick %1 %2"_s.arg(p).arg(mods.toInt());
    });
    QObject::connect(&view, &MapView::provinceHovered,
                     [&](int p, int x, int y) { sig << u"hover %1 %2 %3"_s.arg(p).arg(x).arg(y); });
    QObject::connect(&view, &MapView::zoomChanged, [&](double z) { sig << u"zoom "_s + v(z); });
    QObject::connect(&view, &MapView::legendToggled, [&](const QString &k) { sig << u"legend "_s + k; });
    view.set_data(&md);
    auto img = [&] { return h(view._img32.data(), view._img32.size() * 4); };

    // --- the map picture
    ColorTable A, TQ;
    for (long long i = 0; i < n; ++i)
        for (long long c : {(i * 37) % 256, (i * 91) % 256, (i * 53) % 256})
            A.rgb.push_back(std::uint8_t(c));
    TQ.Q = 3;
    for (long long i = 0; i < n; ++i)
        for (long long q = 0; q < 3; ++q)
            for (long long c : {(i * 37 + q * 85) % 256, (i * 91 + q * 40) % 256, (i * 53 + q * 120) % 256})
                TQ.rgb.push_back(std::uint8_t(c));
    ColorTable shaded, pure;
    shaded.layer = std::make_shared<Layer>(false);
    pure.layer = std::make_shared<Layer>(true);
    {
        const RgbImage c1 = view._colors(A, 100, 300, 200, 500), c2 = view._colors(TQ, 101, 301, 199, 497);
        out << u"colors %1 %2"_s.arg(h(c1.flat().data(), c1.flat().size() * 3), h(c2.flat().data(), c2.flat().size() * 3));
    }
    view.render_base(A, u"province"_s);
    out << u"base province "_s + img();
    view.render_base(A, u"state"_s);
    out << u"base state "_s + img();
    view.render_base(TQ, u"province"_s);
    out << u"base stripes "_s + img();
    view.render_base(TQ, u"state"_s);
    out << u"base stripes state "_s + img();
    view.render_base(shaded, u"state"_s);
    out << u"layer shaded %1 edge %2"_s.arg(img(), v(view.edge_layer->isVisible()));
    view.render_base(pure, u"province"_s);
    out << u"layer pure %1 edge %2"_s.arg(img(), v(view.edge_layer->isVisible()));
    view.render_base(A, u"province"_s);
    // --- the provinces the scene uses: a land province with four land neighbours, then outwards
    auto land = [&](long long q) { return 0 < q && q < n && !md.sea[size_t(q)] && md.area[size_t(q)] > 0; };
    long long p0 = 0;
    for (long long p = 1; p < n && !p0; ++p) {
        if (md.area[size_t(p)] < 400 || md.sea[size_t(p)])
            continue;
        const auto nb = md.neighbours(p);
        if (std::count_if(nb.begin(), nb.end(), land) >= 4)
            p0 = p;
    }
    std::vector<long long> P{p0};
    for (size_t i = 0; P.size() < 12 && i < P.size(); ++i)
        for (long long q : md.neighbours(P[i]))
            if (land(q) && std::find(P.begin(), P.end(), q) == P.end()) {
                P.push_back(q);
                if (P.size() == 12)
                    break;
            }
    out << u"provinces "_s + v(P);
    // --- highlights
    const std::vector<long long> codes{1, 2, 3, 4, CTX, CTX_FOREIGN, CTX_FOREIGN + 1, CTX_FOREIGN + 7, CTX, 3, 2, 4};
    auto codes_of = [&] {
        OrderedMap<long long, long long> c;
        for (size_t i = 0; i < P.size(); ++i)
            c.insert(P[i], codes[i]);
        return c;
    };
    view.set_highlight(codes_of());
    out << u"highlight %1 pixmap %2"_s.arg(img(), qh(view.item->pixmap().toImage()));
    view.render_base(A, u"province"_s);
    out << u"highlight rendered %1 box %2"_s.arg(img(), box_v(view.hl_box));
    view.set_highlight({{P[0], 4}, {P[3], CTX_FOREIGN + 2}, {P[5], 1}, {0, 2}, {n + 4, 3}});
    out << u"highlight changed %1 pixmap %2"_s.arg(img(), qh(view.item->pixmap().toImage()));
    view.set_highlight({{P[0], 4}, {P[3], CTX_FOREIGN + 2}, {P[5], 1}});
    out << u"highlight same "_s + img();
    view.redraw_box(*md.bbox(P));
    out << u"redraw %1 pixmap %2"_s.arg(img(), qh(view.item->pixmap().toImage()));
    view.set_highlight(codes_of());
    // --- selection
    {
        std::vector<long long> sel(P.begin(), P.begin() + 5);
        for (long long x : {0LL, -3LL, n + 5, P[1]})
            sel.push_back(x);
        view.set_selection_layer(sel);
    }
    out << u"selection %1 %2 %3 pos %4"_s.arg(v(view.sel_ids), v(view.sel_item->isVisible()),
                                              qh(view.sel_item->pixmap().toImage()),
                                              L(view.sel_item->pos().x(), view.sel_item->pos().y()));
    view.set_selection_layer({});
    out << u"selection empty %1 %2"_s.arg(v(view.sel_ids), v(view.sel_item->isVisible()));
    view.set_selection_layer(std::vector<long long>(P.begin(), P.begin() + 6));
    // --- focus markings and patterns
    auto fl = [](const QString &style, std::vector<std::pair<long long, double>> pids, std::optional<Rgb> rgb = {}) {
        FocusLayer l;
        l.style = style;
        l.rgb = rgb;
        for (const auto &[p, x] : pids)
            l.pids.insert(p, x);
        return l;
    };
    const std::vector<FocusLayer> layers{
        fl(u"outline"_s, {{P[0], 1}, {P[1], 1}}),
        fl(u"dashed"_s, {{P[2], 1}, {P[3], 1}}),
        fl(u"provinces"_s, {{P[4], 1}}, Rgb{250, 120, 30}),
        fl(u"provset"_s, {{P[5], 1}, {P[6], 1}}),
        fl(u"outline"_s, {{P[11], 1}}, Rgb{40, 200, 90}),
        fl(u"dots"_s, {{P[7], 0.7}, {P[8], 0.3}, {P[9], 0.1}}),
        fl(u"lines"_s, {{P[10], 0.65}, {P[11], 0.2}, {n + 9, 0.5}}),
        fl(u"zzz"_s, {{P[1], 1}}),
        fl(u"dashed"_s, {})};
    auto focus_line = [&](const QString &t) {
        QGraphicsPixmapItem *fi = view.focus_item, *pi = view.pattern_item;
        out << u"%1 focus %2 %3 pos %4 scale %5 | pattern %6 %7 pos %8 | box %9"_s.arg(
            t, v(fi->isVisible()), qh(fi->pixmap().toImage()), L(fi->pos().x(), fi->pos().y()), v(fi->scale()),
            v(pi->isVisible()), qh(pi->pixmap().toImage()), L(pi->pos().x(), pi->pos().y()), box_v(view.focus_box()));
    };
    view.set_focus_layer(layers);
    focus_line(u"k1"_s);
    view.zoom_by(0.3, true, QPoint(450, 300));
    view._render_focus();
    focus_line(u"k3"_s);
    view.set_focus_layer(layers);
    out << u"focus same key: "_s + v(view.focus_item->isVisible());
    // --- navigation
    view.fit();
    out << u"fit %1 scroll %2 %3"_s.arg(v(view.transform().m11())).arg(view.horizontalScrollBar()->value()).arg(
        view.verticalScrollBar()->value());
    view.zoom_by(2.0, true, QPoint(100, 120));
    QPointF c = view.mapToScene(view.viewport()->rect().center());
    out << u"zoom_by %1 scroll %2 %3 centre %4"_s.arg(v(view.transform().m11()))
               .arg(view.horizontalScrollBar()->value())
               .arg(view.verticalScrollBar()->value())
               .arg(L(c.x(), c.y()));
    view.zoom_by(1000.0, false);
    out << u"zoom_by clamp "_s + v(view.transform().m11());
    view.fit();
    view.center_on_province(P[0], 3.0);
    c = view.mapToScene(view.viewport()->rect().center());
    out << u"centre %1 %2"_s.arg(v(view.transform().m11()), L(c.x(), c.y()));
    // --- stars, crown
    {
        std::vector<long long> caps(P.begin(), P.begin() + 6);
        caps.push_back(0);
        caps.push_back(n + 3);
        view.set_capitals(caps);
    }
    QStringList st;
    for (StarItem *s : view.stars)
        st << L(s->capital, s->isVisible(), s->pos().x(), s->pos().y());
    out << u"stars "_s + pyform::list(st);
    view.set_show_stars(true);
    view.set_focus_capitals({P[2]});
    std::vector<bool> shown;
    for (StarItem *s : view.stars)
        shown.push_back(s->isVisible());
    out << u"stars shown "_s + v(shown);
    view.set_nation_capital(P[1]);
    out << u"crown %1 %2 %3"_s.arg(v(view.crown->isVisible()), L(view.crown->pos().x(), view.crown->pos().y()),
                                   v(view.crown->scale()));
    view.set_nation_capital(std::nullopt);
    out << u"crown hidden "_s + v(view.crown->isVisible());
    view.set_nation_capital(P[1]);
    // --- shapes
    std::vector<double> cx, cy;
    for (long long p : P)
        cx.push_back(md.cx[size_t(p)]), cy.push_back(md.cy[size_t(p)]);
    QImage pic(8, 8, QImage::Format_ARGB32);
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            pic.setPixel(xx, yy, QColor(xx * 30, yy * 30, 128, 255).rgba());
    std::vector<Shape> shapes;
    {
        Shape s = shape(u"line"_s, {{cx[0], cy[0]}, {cx[1], cy[1]}, {cx[2], cy[2]}}, {240, 200, 60});
        s.width = 3, s.tag = tag(u"adj"_s, 1), s.halo = true;
        shapes.push_back(s);
        s = shape(u"line"_s, {{cx[3], cy[3]}, {cx[4], cy[4]}}, {90, 200, 250});
        s.width = 1, s.dash = true, s.tag = tag(u"adj"_s, 2);
        shapes.push_back(s);
        s = shape(u"line"_s, {{cx[5], cy[5]}, {cx[6], cy[6]}}, {250, 90, 90});
        s.outline = false, s.tag = tag(u"sea"_s, 3);
        shapes.push_back(s);
        s = shape(u"dot"_s, {{cx[3], cy[3]}}, {255, 255, 255});
        s.size = 4, s.tag = tag(u"dot"_s, 4);
        shapes.push_back(s);
        s = shape(u"dot"_s, {{cx[4] + 3, cy[4]}}, {255, 60, 60});
        s.halo = true;
        shapes.push_back(s);
        s = shape(u"square"_s, {{cx[4], cy[4] + 4}}, {60, 255, 60});
        s.tag = tag(u"sq"_s, 5);
        shapes.push_back(s);
        s = shape(u"diamond"_s, {{cx[5], cy[5]}}, {60, 60, 255});
        s.size = 5, s.halo = true, s.tag = tag(u"dia"_s, 6);
        shapes.push_back(s);
        s = shape(u"x"_s, {{cx[6], cy[6]}}, {255, 160, 0});
        s.width = 2, s.size = 5, s.tag = tag(u"x"_s, 7);
        shapes.push_back(s);
        s = shape(u"x"_s, {{cx[6] + 6, cy[6]}}, {255, 160, 0});
        s.halo = true;
        shapes.push_back(s);
        s = shape(u"circle"_s, {{cx[7], cy[7]}}, {200, 200, 255});
        s.radius = 6.0, s.width = 2;
        shapes.push_back(s);
        s = shape(u"poly"_s, {{cx[8], cy[8]}}, {180, 90, 220});
        s.polys = {{{cx[8] - 4, cy[8] - 3}, {cx[8] + 5, cy[8] - 3}, {cx[8] + 5, cy[8] + 4}, {cx[8] - 4, cy[8] + 4}}};
        s.alpha = 150, s.dash = true, s.halo = true;
        shapes.push_back(s);
        s = shape(u"image"_s, {{cx[9], cy[9]}}, {0, 0, 0});
        s.img = pic, s.rect = QRectF(cx[9] - 4, cy[9] - 4, 8.0, 8.0), s.smooth = false, s.frame = Rgb{255, 255, 255};
        s.opacity = 0.8;
        shapes.push_back(s);
        s = shape(u"badge"_s, {{cx[10], cy[10]}}, {200, 40, 40});
        s.size = 9, s.text = u"3"_s, s.tag = tag(u"badge"_s, 8);
        shapes.push_back(s);
        s = shape(u"badge"_s, {{cx[10] + 8, cy[10]}}, {40, 160, 200});
        s.text = u"2/5"_s, s.hollow = true, s.halo = true;
        shapes.push_back(s);
        s = shape(u"badge"_s, {{cx[10], cy[10] + 9}}, {230, 210, 40});
        s.text = u"!"_s, s.warn = true, s.halo = true;
        shapes.push_back(s);
        s = shape(u"badge"_s, {{cx[10] - 9, cy[10]}}, {240, 240, 240});
        s.text = u"17"_s, s.ring = Rgb{255, 255, 0};
        shapes.push_back(s);
        s = shape(u"army"_s, {{cx[11], cy[11]}}, {120, 30, 30});
        s.text = u"12"_s, s.tag = tag(u"army"_s, 9), s.halo = true;
        shapes.push_back(s);
        s = shape(u"anchor"_s, {{cx[2], cy[2] + 6}}, {220, 220, 255});
        s.text = u"4"_s, s.tag = tag(u"navy"_s, 10);
        shapes.push_back(s);
        s = shape(u"label"_s, {{cx[1], cy[1]}}, {255, 230, 160});
        s.text = u"Army of the North"_s, s.dy = 18;
        shapes.push_back(s);
        s = shape(u"dot"_s, {{cx[0], cy[0]}}, {0, 0, 0});
        s.min_scale = 100.0, s.tag = tag(u"hidden"_s, 11);
        shapes.push_back(s);
    }
    view.shape_layer->set_shapes(shapes);
    {
        Shape t = shape(u"line"_s, {{cx[0], cy[0]}, {cx[5], cy[5]}}, {255, 255, 255});
        t.width = 2, t.dash = true;
        view.shape_layer->set_temp({t});
    }
    view.shape_layer->setVisible(true);
    // --- names
    view.name_layer->set_items({{cx[0], cy[0] - 3, 0.0, 60.0, 14.0, u"Alpha"_s, {240, 230, 200}},
                                {cx[1], cy[1] + 2, 0.35, 80.0, 18.0, u"Beta Gamma"_s, {255, 255, 255}},
                                {cx[2], cy[2], -0.6, 40.0, 30.0, u"Ölund"_s, {200, 220, 255}},
                                {cx[3], cy[3], 0.0, 2.0, 1.0, u"tiny"_s, {255, 0, 0}}});
    view.name_layer->setVisible(true);
    // --- banner, pill, legend
    view.set_banner(u"A problem: province 12 & <more>"_s, Rgb{255, 120, 120});
    view.set_mode_pill(u"Drawing ▸ click to stop"_s, [&] { sig << u"pill"_s; });
    view.set_legend(u"Owners & more"_s, {{{255, 0, 0}, u"Red"_s, std::nullopt, false},
                                         {{0, 255, 0}, u"Green <b>"_s, u"green"_s, true},
                                         {{0, 0, 255}, u"Blue's"_s, u"blue"_s, false}});
    out << u"banner %1 pill %2 %3 legend %4 %5"_s.arg(
        L(view._banner->x(), view._banner->y(), view._banner->width(), view._banner->height()),
        L(view._pill->x(), view._pill->y(), view._pill->width(), view._pill->height()), v(view._pill->isVisible()),
        L(view._legend->x(), view._legend->y(), view._legend->width(), view._legend->height()),
        v(view._legend->text()));
    auto still = [&] {
        view._pulse->stop();
        view._calm->stop();
        view._focus_timer->stop();
        view.sel_item->setOpacity(1.0);
    };
    still();
    view._render_focus();
    QApplication::processEvents();
    still();
    view.grab().save(QDir(out_dir).filePath(u"zoomed.png"_s));
    // --- hit tests (after a paint: the zoom of the last paint counts)
    QStringList hits;
    const std::vector<std::tuple<double, double, double, std::optional<QStringList>>> probes{
        {cx[0], cy[0], 1.0, std::nullopt},
        {(cx[0] + cx[1]) / 2, (cy[0] + cy[1]) / 2, 0.5, std::nullopt},
        {cx[3], cy[3], 0.5, std::nullopt},
        {cx[3], cy[3], 0.5, QStringList{u"adj"_s}},
        {cx[6], cy[6], 0.5, std::nullopt},
        {cx[10], cy[10], 0.5, std::nullopt},
        {cx[11], cy[11], 0.5, QStringList{u"army"_s, u"navy"_s}},
        {cx[0], cy[0], 0.5, QStringList{u"hidden"_s}},
        {cx[2], cy[2] + 6, 2.0, std::nullopt},
        {-50.0, -50.0, 3.0, std::nullopt}};
    for (const auto &[x, y, tol, kinds] : probes)
        hits << tag_v(view.shape_layer->hit(x, y, tol, kinds));
    out << u"hits "_s + pyform::list(hits);
    // --- the same scene with a pure pixel layer (borders between the pixels)
    view.render_base(pure, u"state"_s);
    view.center_on_province(P[0], 6.0);
    still();
    QApplication::processEvents();
    still();
    view.grab().save(QDir(out_dir).filePath(u"pure.png"_s));
    // --- the whole map, fitted
    view.render_base(A, u"state"_s);
    view.shape_layer->set_temp({});
    view.set_legend(u"Owners"_s, {});
    view.set_banner(QString());
    view.set_mode_pill(QString());
    view.fit();
    view._render_focus();
    still();
    QApplication::processEvents();
    still();
    view.grab().save(QDir(out_dir).filePath(u"fit.png"_s));
    out << u"hidden %1 %2 %3"_s.arg(v(view._legend->isVisible()), v(view._banner->isVisible()),
                                    v(view._pill->isVisible()));
    // --- mouse: click, right click, drag, wheel, leave (real events into the viewport)
    sig.clear();
    QWidget *vp = view.viewport();
    const QPointF centre(450, 300);
    auto mouse = [&](QEvent::Type kind, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                     Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent ev(kind, pos, QPointF(vp->mapToGlobal(pos.toPoint())), button, buttons, mods);
        QApplication::sendEvent(vp, &ev);
    };
    mouse(QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
    mouse(QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    mouse(QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    mouse(QEvent::MouseButtonPress, QPointF(300, 200), Qt::RightButton, Qt::RightButton);
    mouse(QEvent::MouseButtonRelease, QPointF(300, 200), Qt::RightButton, Qt::NoButton);
    mouse(QEvent::MouseMove, QPointF(610, 410), Qt::NoButton, Qt::NoButton);
    const std::vector<long long> before{view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()};
    mouse(QEvent::MouseButtonPress, QPointF(400, 300), Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, QPointF(403, 301), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, QPointF(430, 320), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, QPointF(470, 345), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, QPointF(470, 345), Qt::LeftButton, Qt::NoButton);
    const std::vector<long long> after{view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()};
    const QPointF wp(500, 250);
    QWheelEvent we(wp, QPointF(vp->mapToGlobal(wp.toPoint())), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(vp, &we);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    QStringList hooked;
    view.click_hook = [&](double, double, double tol, Qt::KeyboardModifiers) {
        hooked << py::repr_float(py::round_to(tol, 6));
        return true;
    };
    mouse(QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
    view._pill->click();
    out << u"pan %1 -> %2 zoom %3 hook %4"_s.arg(v(before), v(after), v(view.transform().m11()), v(hooked));
    out << u"signals "_s + v(sig);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%lld lines, 3 pictures\n", static_cast<long long>(out.size()));
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
