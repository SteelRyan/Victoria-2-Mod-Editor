#pragma once
// drawtext.h — QPainter::drawText(rect, flags, text) as PySide calls it.
//
// PySide's QPainter.drawText(rect, flags, text) RETURNS the bounding rect, so it always hands Qt a br pointer. Qt lays
// a text that overflows its rect out differently without one: it stops at the rect's bottom and centres (AlignVCenter
// / AlignBottom) on the lines it kept. Measured 2026-11-07 on the Pop influences cards: the same words, font, box and
// flags came out 9 px apart (3 lines centred instead of 4). Every rect-form drawText of the port goes through here,
// so the C++ editor passes a br too.
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QString>

namespace drawtext {

inline void draw(QPainter &p, const QRectF &r, int flags, const QString &text)
{
    QRectF br;
    p.drawText(r, flags, text, &br);
}

inline void draw(QPainter &p, const QRect &r, int flags, const QString &text)
{
    QRect br;
    p.drawText(r, flags, text, &br);
}

}   // namespace drawtext
