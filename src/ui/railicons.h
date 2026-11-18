// railicons.py — The icons of the navigation rail's folded view, drawn stroke by stroke (no image files). Each is
// a function painting into a 24 × 24 box with one colour; draw_icon() scales it to the size wanted.
#pragma once
#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

namespace railicons {

bool draw_icon(QPainter &p, const QString &key, const QRectF &rect, const QColor &color);   // false: no such icon

}  // namespace railicons
