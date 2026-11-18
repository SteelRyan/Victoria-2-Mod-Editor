// railicons.py — line by line.
#include "ui/railicons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace railicons {

static const double STROKE = 1.9;
static const double DEG_TO_RAD = std::numbers::pi / 180.0;   // (math.radians: x * (pi / 180))
using Pts = std::initializer_list<std::pair<double, double>>;

static void _pen(QPainter &p, const QColor &color, double width = STROKE, Qt::PenStyle style = Qt::SolidLine)
{
    p.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
}

template <class List>
static void _poly(QPainter &p, const List &pts, bool close = true)
{
    auto it = pts.begin();
    QPainterPath path(QPointF(it->first, it->second));
    for (++it; it != pts.end(); ++it)
        path.lineTo(it->first, it->second);
    if (close)
        path.closeSubpath();
    p.drawPath(path);
}

static void _line(QPainter &p, double x1, double y1, double x2, double y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); }

static void _dot(QPainter &p, const QColor &color, double x, double y, double r)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(x, y), r, r);
}

static void population(QPainter &p, const QColor &c)   // two people: heads and shoulders, one behind the other
{
    _pen(p, c);
    p.drawEllipse(QPointF(9, 7.8), 3.1, 3.1);
    QPainterPath a(QPointF(3.2, 20));
    a.cubicTo(3.2, 15.4, 5.8, 13.6, 9, 13.6);
    a.cubicTo(12.2, 13.6, 14.8, 15.4, 14.8, 20);
    p.drawPath(a);
    p.drawEllipse(QPointF(16.8, 9.2), 2.5, 2.5);
    QPainterPath b(QPointF(16.6, 14.1));
    b.cubicTo(19.2, 14.1, 21, 15.8, 21, 19.6);
    p.drawPath(b);
}

static void provinces(QPainter &p, const QColor &c)   // a piece of land cut into three provinces
{
    _pen(p, c);
    _poly(p, Pts{{4, 6.5}, {10, 3.6}, {16.2, 5}, {20.6, 9.4}, {19.2, 16.2}, {13, 20.6}, {6, 18.6}, {3.4, 12.2}});
    _pen(p, c, 1.5);
    _line(p, 10, 3.6, 11.2, 11.2);
    _line(p, 11.2, 11.2, 3.4, 12.2);
    _line(p, 11.2, 11.2, 19.2, 16.2);
}

static void drawing(QPainter &p, const QColor &c)   // a pencil, its point down left
{
    _pen(p, c);
    _poly(p, Pts{{3.8, 20.2}, {5.2, 15.4}, {15.6, 5}, {19, 8.4}, {8.6, 18.8}});
    _pen(p, c, 1.5);
    _line(p, 5.2, 15.4, 8.6, 18.8);
    _line(p, 13.4, 7.2, 16.8, 10.6);
}

static void positions(QPainter &p, const QColor &c)   // a map pin
{
    _pen(p, c);
    QPainterPath pin(QPointF(12, 21));
    pin.cubicTo(12, 21, 5.4, 14.6, 5.4, 9.6);
    pin.cubicTo(5.4, 5.6, 8.4, 3, 12, 3);
    pin.cubicTo(15.6, 3, 18.6, 5.6, 18.6, 9.6);
    pin.cubicTo(18.6, 14.6, 12, 21, 12, 21);
    p.drawPath(pin);
    p.drawEllipse(QPointF(12, 9.6), 2.5, 2.5);
}

static void adjacencies(QPainter &p, const QColor &c)   // two places and the crossing between them
{
    _pen(p, c);
    p.drawEllipse(QPointF(5.8, 17), 2.8, 2.8);
    p.drawEllipse(QPointF(18.2, 7), 2.8, 2.8);
    QPen pen(c, STROKE, Qt::CustomDashLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setDashPattern({1.3, 1.9});
    p.setPen(pen);
    QPainterPath arc(QPointF(7.4, 14.4));
    arc.cubicTo(9.4, 9.4, 12.4, 7.6, 15.2, 7.2);
    p.drawPath(arc);
}

static void canals(QPainter &p, const QColor &c)   // two banks curving across the land, a lock gate at each end
{
    _pen(p, c);
    QPainterPath a(QPointF(3.6, 17.6));
    a.cubicTo(8.4, 17.2, 10.2, 11.4, 15.2, 8.6);
    a.lineTo(20.4, 6.2);
    p.drawPath(a);
    QPainterPath b(QPointF(3.6, 21.2));
    b.cubicTo(9.4, 20.8, 12.2, 14.6, 16.8, 11.8);
    b.lineTo(20.4, 10.2);
    p.drawPath(b);
    _pen(p, c, 2.4);
    _line(p, 5.4, 16.6, 5.4, 22);
    _line(p, 18.6, 5.2, 18.6, 11);
}

static void states(QPainter &p, const QColor &c)   // three provinces joined: a bold outline, a star at the capital
{
    _pen(p, c, 2.3);
    _poly(p, Pts{{3.4, 7}, {11, 3.4}, {20.4, 6.4}, {20.6, 16.4}, {12.4, 20.8}, {3.6, 17.4}});
    _pen(p, c, 1.3);
    _line(p, 11, 3.4, 11.6, 12);
    _line(p, 11.6, 12, 3.6, 13.4);
    _line(p, 11.6, 12, 20.6, 13.6);
    _dot(p, c, 15.6, 8.4, 1.9);
}

static void search(QPainter &p, const QColor &c)   // a magnifying glass
{
    _pen(p, c, 2.2);
    p.drawEllipse(QPointF(10.2, 10.2), 6.2, 6.2);
    _pen(p, c, 3.0);
    _line(p, 14.8, 14.8, 20.6, 20.6);
}

static void nation(QPainter &p, const QColor &c)   // a flag on its pole
{
    _pen(p, c);
    _line(p, 5.6, 3.4, 5.6, 21);
    QPainterPath flag(QPointF(5.6, 4.6));
    flag.cubicTo(9, 2.4, 12, 7.2, 19, 4.8);
    flag.lineTo(19, 13);
    flag.cubicTo(12, 15.4, 9, 10.6, 5.6, 12.8);
    p.drawPath(flag);
}

static void armies(QPainter &p, const QColor &c)   // two crossed swords
{
    _pen(p, c);
    for (int m : {1, -1}) {
        auto x = [m](double v) { return m == 1 ? v : 24 - v; };
        _line(p, x(6.6), 17.4, x(19.8), 4.2);    // the blade, its point up
        _line(p, x(4.6), 13.8, x(10.2), 19.4);   // the guard, across it
        _line(p, x(6.2), 17.8, x(3.8), 20.2);    // the grip
    }
}

static void navies(QPainter &p, const QColor &c)   // a sailing ship
{
    _pen(p, c);
    _poly(p, Pts{{2.8, 15}, {21.2, 15}, {18.2, 20}, {5.8, 20}});
    _line(p, 12, 15, 12, 3.4);
    _poly(p, Pts{{13.6, 4.6}, {19, 12.2}, {13.6, 12.2}});
    _poly(p, Pts{{10.4, 7}, {6.2, 12.2}, {10.4, 12.2}});
}

static void generals(QPainter &p, const QColor &c)   // a five-pointed star over a bar (a general's insignia)
{
    _pen(p, c);
    std::vector<std::pair<double, double>> star;
    for (int i = 0; i < 10; ++i) {
        const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5, r = i % 2 ? 3.6 : 8.4;
        star.push_back({12 + r * std::cos(a), 10.6 + r * std::sin(a)});
    }
    _poly(p, star);
    _line(p, 5, 21, 19, 21);
}

static void admirals(QPainter &p, const QColor &c)   // an anchor with a star above it
{
    _pen(p, c);
    _line(p, 12, 8.6, 12, 21);
    _line(p, 8, 11.4, 16, 11.4);
    QPainterPath arc;
    arc.moveTo(4.6, 15.6);
    arc.quadTo(5.6, 21.2, 12, 21);
    arc.quadTo(18.4, 21.2, 19.4, 15.6);
    p.drawPath(arc);
    std::vector<std::pair<double, double>> star;
    for (int i = 0; i < 10; ++i) {
        const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5, r = i % 2 ? 1.5 : 3.6;
        star.push_back({12 + r * std::cos(a), 4.6 + r * std::sin(a)});
    }
    _poly(p, star);
}

static void diplomacy(QPainter &p, const QColor &c)   // two linked rings
{
    _pen(p, c);
    p.drawEllipse(QPointF(8.8, 12), 5.6, 5.6);
    p.drawEllipse(QPointF(15.2, 12), 5.6, 5.6);
}

static void politics(QPainter &p, const QColor &c)   // a house of parliament: a gable on columns
{
    _pen(p, c);
    _poly(p, Pts{{3.4, 9.4}, {12, 3.6}, {20.6, 9.4}});
    for (double x : {6.6, 12.0, 17.4})
        _line(p, x, 12, x, 17.4);
    _line(p, 3.4, 20.2, 20.6, 20.2);
}

static void technology(QPainter &p, const QColor &c)   // a cog wheel
{
    _pen(p, c);
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i < 8; ++i) {
        const double a = (i * 45) * DEG_TO_RAD;
        for (auto [r, d] : {std::pair{6.6, -17}, {9.2, -9}, {9.2, 9}, {6.6, 17}}) {
            const double b = a + d * DEG_TO_RAD;
            pts.emplace_back(12 + r * std::cos(b), 12 + r * std::sin(b));
        }
    }
    _poly(p, pts);
    p.drawEllipse(QPointF(12, 12), 2.7, 2.7);
}

static void economy(QPainter &p, const QColor &c)   // a factory: saw-tooth roof and a chimney
{
    _pen(p, c);
    _poly(p, Pts{{3.2, 20.2}, {3.2, 11.2}, {8.4, 14.4}, {8.4, 11.2}, {13.6, 14.4}, {15, 14.4}, {15, 4.2}, {18.6, 4.2},
                 {18.6, 14.4}, {20.8, 14.4}, {20.8, 20.2}});
}

static void scripts(QPainter &p, const QColor &c)   // curly braces with three dots: script text
{
    _pen(p, c);
    for (int m : {1, -1}) {
        auto x = [m](double v) { return m == 1 ? v : 24 - v; };
        QPainterPath b(QPointF(x(9.4), 3.8));
        b.cubicTo(x(6.6), 3.8, m == 1 ? 6.6 : 17.4, 5.6, x(6.6), 7.6);
        b.lineTo(x(6.6), 9.4);
        b.cubicTo(x(6.6), 11, x(5.6), 12, x(3.8), 12);
        b.cubicTo(x(5.6), 12, x(6.6), 13, x(6.6), 14.6);
        b.lineTo(x(6.6), 16.4);
        b.cubicTo(x(6.6), 18.4, x(6.6), 20.2, x(9.4), 20.2);
        p.drawPath(b);
    }
    for (double dx : {9.4, 12.0, 14.6})
        _dot(p, c, dx, 12, 1.05);
}

static void errors(QPainter &p, const QColor &c)   // a warning triangle
{
    _pen(p, c);
    _poly(p, Pts{{12, 3.6}, {21.4, 20}, {2.6, 20}});
    _line(p, 12, 9.6, 12, 14.4);
    _dot(p, c, 12, 17.3, 1.15);
}

static void changelog(QPainter &p, const QColor &c)   // a list: three points, three lines
{
    for (double y : {6.8, 12.0, 17.2})
        _dot(p, c, 5, y, 1.35);
    _pen(p, c);
    for (auto [y, x2] : {std::pair{6.8, 20.6}, {12.0, 17.4}, {17.2, 20.6}})
        _line(p, 9, y, x2, y);
}

static void warfare(QPainter &p, const QColor &c)   // a shield with a bar across it
{
    _pen(p, c);
    QPainterPath sh(QPointF(12, 3.2));
    sh.lineTo(19.6, 5.8);
    sh.lineTo(19.6, 11.4);
    sh.cubicTo(19.6, 16.2, 16.4, 19.4, 12, 21);
    sh.cubicTo(7.6, 19.4, 4.4, 16.2, 4.4, 11.4);
    sh.lineTo(4.4, 5.8);
    sh.closeSubpath();
    p.drawPath(sh);
    _pen(p, c, 1.5);
    _line(p, 12, 3.6, 12, 20.4);
    _line(p, 4.8, 10.6, 19.2, 10.6);
}

static void game_rules(QPainter &p, const QColor &c)   // three sliders: the game's settings
{
    _pen(p, c);
    for (auto [y, x] : {std::pair{6.4, 15.2}, {12.0, 8.6}, {17.6, 13.4}}) {
        _line(p, 3.4, y, x - 2.6, y);
        _line(p, x + 2.6, y, 20.6, y);
        p.drawEllipse(QPointF(x, y), 2.5, 2.5);
    }
}

static void library(QPainter &p, const QColor &c)   // a folder
{
    _pen(p, c);
    QPainterPath f(QPointF(3.2, 7));
    f.lineTo(3.2, 19.4);
    f.lineTo(20.8, 19.4);
    f.lineTo(20.8, 8.6);
    f.lineTo(11.6, 8.6);
    f.lineTo(9.6, 5.4);
    f.lineTo(3.2, 5.4);
    f.closeSubpath();
    p.drawPath(f);
    _pen(p, c, 1.5);
    _line(p, 3.6, 11.4, 20.4, 11.4);
}

static void importing(QPainter &p, const QColor &c)   // a tray with an arrow coming down into it
{
    _pen(p, c);
    QPainterPath t(QPointF(3.4, 13.4));
    t.lineTo(3.4, 20);
    t.lineTo(20.6, 20);
    t.lineTo(20.6, 13.4);
    p.drawPath(t);
    _line(p, 12, 3.4, 12, 14.6);
    QPainterPath a(QPointF(7.6, 10.4));
    a.lineTo(12, 14.8);
    a.lineTo(16.4, 10.4);
    p.drawPath(a);
}

static void output(QPainter &p, const QColor &c)   // bars growing to the right: what is produced
{
    _pen(p, c);
    _line(p, 4, 4, 4, 20);
    _line(p, 4, 20, 20.5, 20);
    _pen(p, c, 2.6);
    _line(p, 8.2, 20, 8.2, 14);
    _line(p, 12.6, 20, 12.6, 10);
    _line(p, 17, 20, 17, 6);
}

static void shortcuts(QPainter &p, const QColor &c)   // a key of a keyboard: the cap, its top face, a letter on it
{
    _pen(p, c);
    p.drawRoundedRect(QRectF(3.2, 4.2, 17.6, 16.2), 3.2, 3.2);
    _pen(p, c, 1.5);
    p.drawRoundedRect(QRectF(6.2, 6.2, 11.6, 9.8), 2.0, 2.0);
    _pen(p, c, 1.8);
    _poly(p, Pts{{9.6, 13.8}, {12, 8.4}, {14.4, 13.8}}, false);
    _line(p, 10.5, 12, 13.5, 12);
}

static void checksum_icon(QPainter &p, const QColor &c)   // a # sign (C++ only, 2026-11-18)
{
    _pen(p, c, 2.0);
    _line(p, 9.6, 4.2, 7.8, 19.8);
    _line(p, 16.2, 4.2, 14.4, 19.8);
    _line(p, 4.4, 9.2, 19.8, 9.2);
    _line(p, 4.0, 14.8, 19.4, 14.8);
}

static void submods(QPainter &p, const QColor &c)   // two layers: a sheet lying on a larger one
{
    _pen(p, c);
    _poly(p, Pts{{12, 3.4}, {20.8, 8.2}, {12, 13}, {3.2, 8.2}});
    QPainterPath low(QPointF(3.2, 12.6));
    low.lineTo(12, 17.4);
    low.lineTo(20.8, 12.6);
    p.drawPath(low);
    QPainterPath low2(QPointF(3.2, 16.6));
    low2.lineTo(12, 21.4);
    low2.lineTo(20.8, 16.6);
    p.drawPath(low2);
}

using Fn = void (*)(QPainter &, const QColor &);
static const QHash<QString, Fn> ICONS = {
    {u"output"_s, output}, {u"shortcuts"_s, shortcuts}, {u"submods"_s, submods}, {u"checksum"_s, checksum_icon}, {u"import"_s, importing},
    {u"library"_s, library}, {u"warfare"_s, warfare}, {u"rules"_s, game_rules}, {u"pops"_s, population},
    {u"provinces"_s, provinces}, {u"draw"_s, drawing}, {u"pos"_s, positions}, {u"adj"_s, adjacencies},
    {u"search"_s, search}, {u"states"_s, states}, {u"canal"_s, canals}, {u"nations"_s, nation}, {u"armies"_s, armies},
    {u"navies"_s, navies}, {u"generals"_s, generals}, {u"admirals"_s, admirals}, {u"diplomacy"_s, diplomacy}, {u"politics"_s, politics}, {u"technology"_s, technology},
    {u"production"_s, economy}, {u"events"_s, scripts}, {u"errors"_s, errors}, {u"changelog"_s, changelog}};

bool draw_icon(QPainter &p, const QString &key, const QRectF &rect, const QColor &color)
{
    const Fn fn = ICONS.value(key, nullptr);
    if (!fn)
        return false;
    const double s = std::min(rect.width(), rect.height()) / 24.0;
    p.save();
    p.translate(rect.x() + (rect.width() - 24 * s) / 2.0, rect.y() + (rect.height() - 24 * s) / 2.0);
    p.scale(s, s);
    fn(p, color);
    p.restore();
    return true;
}

}  // namespace railicons
