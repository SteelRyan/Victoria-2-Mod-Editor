// focuswindow.py — line by line.
#include "map/focuswindow.h"
#include "ui/drawtext.h"
#include "core/pynum.h"
#include "core/pytext.h"

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;

namespace focuswindow {

const QString GUI = u"interface/province_interface.gui"_s;
const QString SPRITE = u"GFX_province_natfocus_bg"_s;
const long long ICON = 33;                           // an icon and its gap
const long long ICON_W = 32;                         // an icon
const long long HEAD_OFF = 37;                       // the band begins this much above the row's position
const long long SCREEN_H = 768, WINDOW_Y = 64;
const long long MIN_STEP = 54;                       // a name bar and an icon row need this much
const long long PAD = 12;
const QString BACK = u"#27405a"_s;                   // (the map behind the window: a plain sea colour)
const std::vector<long long> SCREENS = {768, 1080};  // a line where a screen this high ends

static const auto RX = QRegularExpression::UseUnicodePropertiesOption;
static long long fdiv(long long a, long long b)      // Python's // for ints
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
}

std::optional<Span> _block(const QString &text, const QString &kind, const QString &name)
{
    // (start, end) of  kind = { … name = "name" … } — the innermost (a window lies in windows).
    std::optional<Span> best;
    const QRegularExpression head(u"\\b%1\\s*=\\s*\\{"_s.arg(kind), RX);
    const QRegularExpression named(u"name\\s*=\\s*\"?%1\"?\\s"_s.arg(QRegularExpression::escape(name)), RX);
    for (auto it = head.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        long long d = 0;
        qsizetype i = m.capturedEnd() - 1;
        while (i < text.size()) {
            if (text[i] == u'{')
                d += 1;
            else if (text[i] == u'}') {
                d -= 1;
                if (d == 0)
                    break;
            }
            i += 1;
        }
        const QString body = text.mid(m.capturedStart(), i + 1 - m.capturedStart());
        if (named.match(body).hasMatch())
            if (!best || body.size() < best->second - best->first)
                best = Span{m.capturedStart(), i + 1};
    }
    return best;
}

std::optional<XY> _xy(const QString &text, const Span &span, const QString &key)
{
    const QRegularExpression re(u"%1\\s*=\\s*\\{\\s*x\\s*=\\s*(-?\\d+)\\s*y\\s*=\\s*(-?\\d+)\\s*\\}"_s.arg(key), RX);
    const QString part = py::slice(text, span.first, span.second);
    const auto m = re.match(part);
    if (!m.hasMatch())
        return std::nullopt;
    return XY{py::to_int(m.captured(1)), py::to_int(m.captured(2)), span.first + m.capturedStart(),
              span.first + m.capturedEnd()};
}

std::optional<G> read(const QString &text)
{
    G out;
    const std::tuple<XY *, QString, QString, QString, QString> what[] = {
        {&out.start, u"start"_s, u"positionType"_s, u"focuscategory_start"_s, u"position"_s},
        {&out.step, u"step"_s, u"positionType"_s, u"focuscategory_step"_s, u"position"_s},
        {&out.icons, u"icons"_s, u"OverlappingElementsBoxType"_s, u"focus_icons"_s, u"size"_s},
        {&out.close, u"close"_s, u"guiButtonType"_s, u"close_button"_s, u"position"_s},
        {&out.cancel, u"cancel"_s, u"guiButtonType"_s, u"cancel_button"_s, u"position"_s}};
    for (const auto &[dst, key, kind, name, field] : what) {
        std::optional<Span> span;
        if (key == u"close" || key == u"cancel") {   // (the buttons of the focus window, not of another)
            if (const auto win = _block(text, u"windowType"_s, u"national_focus_window"_s))
                if (const auto sub = _block(py::slice(text, win->first, win->second), kind, name))
                    span = Span{win->first + sub->first, win->first + sub->second};
        } else
            span = _block(text, kind, name);
        const auto got = span ? _xy(text, *span, field) : std::nullopt;
        if (!got)
            return std::nullopt;
        *dst = *got;
    }
    return out;
}

std::tuple<long long, long long, long long, long long> drawn(const G &g, const QImage &img)
{
    // (rows the picture is drawn for, the height of a band, the head's height, the foot's height)
    const long long step = g.step.y;
    const long long head = std::max(0LL, g.start.y - HEAD_OFF);
    long long foot = img.height() - (g.close.y - 6);    // the Close button stands 6 px into the foot
    foot = std::max(40LL, std::min(img.height() - head - step, foot));
    if (step == 0)
        throw py::ValueError("float division by zero");
    const long long rows = std::max(1LL, py::round_int(double(img.height() - head - foot) / double(step)));
    return {rows, step, head, img.height() - head - rows * step};
}

PlanOut plan(const G &g, const QImage &img, long long rows, long long per_row)
{
    const auto [r0, s0, head, foot] = drawn(g, img);
    const long long room = SCREEN_H - WINDOW_Y - head - foot - 8;
    const long long step = rows * 62 <= room ? 62 : std::max(MIN_STEP, fdiv(room, std::max(1LL, rows)));
    const long long fit_ = std::max(1LL, fdiv(g.icons.x - ICON_W, ICON) + 1);   // (n - 1) x 33 + 32 side by side
    const long long width = std::max(g.icons.x, per_row * ICON - 1);
    const long long extra = std::max(0LL, per_row - fit_) * ICON;   // the picture: an icon's width more for each
    QStringList notes;
    if (rows * step > room)
        notes << u"%1 rows are %2 px too tall for a 768-high screen even with the lowest rows"_s.arg(rows).arg(rows * step - room);
    return {step, width, extra, head + rows * step + foot, r0, s0, fit_, head, foot, notes};
}

Fitted fit(const QString &text0, const QImage &img, long long rows, long long per_row)
{
    const auto g = read(text0);
    if (!g)
        return {std::nullopt, {}, u"The focus window in %1 is laid out another way than the game's — not changed."_s.arg(GUI)};
    const PlanOut pl = plan(*g, img, rows, per_row);
    const long long r0 = pl.rows0, s0 = pl.step0, head = pl.head, foot = pl.foot;
    const QImage src = img.convertToFormat(QImage::Format_ARGB32);
    const long long extra_w = pl.extra;
    const long long new_w = std::max(64LL, src.width() + extra_w);
    QImage out(int(new_w), int(pl.height), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    auto row_of = [&](long long y, long long h, long long dst_y, long long dst_h) {
        // A band of the picture drawn at dst_y, dst_h high, as wide as the new picture (its middle repeated).
        const long long mid = src.width() / 2;
        QImage band = src.copy(0, int(y), src.width(), int(h));
        if (new_w != src.width()) {
            QImage wide(int(new_w), int(h), QImage::Format_ARGB32);
            wide.fill(Qt::transparent);
            QPainter q(&wide);
            q.setCompositionMode(QPainter::CompositionMode_Source);
            const long long left = std::min(mid, new_w / 2);
            const long long right = std::min(src.width() - mid, new_w - left);
            q.drawImage(0, 0, band.copy(0, 0, int(left), int(h)));
            q.drawImage(QRect(int(left), 0, int(new_w - left - right), int(h)), band.copy(int(mid), 0, 1, int(h)));
            q.drawImage(int(new_w - right), 0, band.copy(int(src.width() - right), 0, int(right), int(h)));
            q.end();
            band = wide;
        }
        p.drawImage(QRect(0, int(dst_y), int(new_w), int(dst_h)), band);
    };
    row_of(0, head, 0, head);
    for (long long r = 0; r < rows; ++r)
        row_of(head + std::min(r, r0 - 1) * s0, s0, head + r * pl.step, pl.step);
    row_of(src.height() - foot, foot, head + rows * pl.step, foot);
    p.end();
    // the numbers: from the back of the text forwards, so the places stay right
    std::vector<std::tuple<qsizetype, qsizetype, QString>> edits = {
        {g->step.s, g->step.e, u"position = { x = %1 y = %2 }"_s.arg(g->step.x).arg(pl.step)},
        {g->icons.s, g->icons.e, u"size = { x=%1 y= %2 }"_s.arg(pl.width).arg(g->icons.y)},
        {g->close.s, g->close.e, u"position = { x=%1 y =%2}"_s.arg(g->close.x + fdiv(extra_w, 2)).arg(head + rows * pl.step + 6)},
        {g->cancel.s, g->cancel.e, u"position = { x=%1 y =%2}"_s.arg(g->cancel.x + extra_w).arg(g->cancel.y)}};
    std::sort(edits.begin(), edits.end(), std::greater<>());
    QString text = text0;
    for (const auto &[a, b, fresh] : edits)
        text = text.left(a) + fresh + text.mid(b);
    return {text, out, {}};
}

// ---------------------------------------------------------------------------------------------- the window, drawn
static QString word(const QString &body, const QString &key, const QString &dflt)
{
    const QRegularExpression re(u"%1\\s*=\\s*\"?([A-Za-z0-9_.]+)\"?"_s.arg(key), RX);
    const auto m = re.match(body);
    return m.hasMatch() ? m.captured(1) : dflt;
}

Details details(const QString &text)
{
    // What the drawing needs besides read(): where a row's name and its icon box stand, the buttons' looks.
    Details d;
    if (const auto cat = _block(text, u"windowType"_s, u"focus_category"_s)) {
        const QString sub = py::slice(text, cat->first, cat->second);
        if (const auto nb = _block(sub, u"instantTextBoxType"_s, u"name"_s)) {
            if (const auto pos = _xy(sub, *nb, u"position"_s))
                d.name = {pos->x, pos->y};
            const QString body = py::slice(sub, nb->first, nb->second);
            d.font = word(body, u"font"_s, d.font);
            if (const auto f = py::to_float(word(body, u"maxWidth"_s, QString::number(d.name_w)))) {
                if (std::isinf(*f))                  // (int(inf): OverflowError, not caught; int(nan): ValueError, caught)
                    throw std::overflow_error("cannot convert float infinity to integer");
                if (!std::isnan(*f))
                    d.name_w = py::trunc_int(*f);
            }
        }
        if (const auto ib = _block(sub, u"OverlappingElementsBoxType"_s, u"focus_icons"_s)) {
            if (const auto pos = _xy(sub, *ib, u"position"_s))
                d.box = {pos->x, pos->y};
            if (const auto f = py::to_float(word(py::slice(sub, ib->first, ib->second), u"spacing"_s, u"1.0"_s)))
                d.spacing = *f;
        }
    }
    if (const auto win = _block(text, u"windowType"_s, u"national_focus_window"_s)) {
        const QString sub = py::slice(text, win->first, win->second);
        const std::pair<std::tuple<QString, QString, QString> *, QString> bs[] = {{&d.close, u"close_button"_s},
                                                                                {&d.cancel, u"cancel_button"_s}};
        for (const auto &[dst, name] : bs)
            if (const auto b = _block(sub, u"guiButtonType"_s, name)) {
                const QString body = py::slice(sub, b->first, b->second);
                *dst = {word(body, u"quadTextureSprite"_s, std::get<0>(*dst)), word(body, u"buttonText"_s, std::get<1>(*dst)),
                        word(body, u"buttonFont"_s, std::get<2>(*dst))};
            }
    }
    return d;
}

Drawn picture(const QString &text, const std::optional<QImage> &bg, const std::vector<Row> &rows,
              const std::vector<QPixmap> &frames, Look *look, const std::function<OptStr(const QString &)> &words,
              const OptStr &sel, const QStringList &multi)
{
    // The national focus window as the game lays it out with these files. -> (picture, the icons' places and the rows'
    // names ("@group"), notes) — (None, [], []) when the window is laid out another way than the game's.
    const auto g = read(text);
    if (!g || !bg)
        return {};
    const Details d = details(text);
    const long long step = g->step.y, ox = g->start.x, oy0 = g->start.y;
    const long long item = !frames.empty() ? frames[0].width() : d.item;
    const long long item_h = !frames.empty() ? frames[0].height() : d.item;
    const long long box_w = g->icons.x;
    const double pitch0 = double(item) + d.spacing;
    const long long last = !rows.empty() ? oy0 + (static_cast<long long>(rows.size()) - 1) * step + d.box.second + item_h : 0;
    struct Button {
        QString key;
        std::optional<QImage> spr;
        long long x, y, w, h;
        std::tuple<QString, QString, QString> look;
    };
    std::vector<Button> buttons;
    for (const auto &[key, xy, lk] : {std::tuple{u"close"_s, g->close, d.close}, std::tuple{u"cancel"_s, g->cancel, d.cancel}}) {
        std::optional<QImage> spr = look ? look->sprite(std::get<0>(lk)) : std::nullopt;
        const long long w = spr ? spr->width() : 160, h = spr ? spr->height() : 26;
        buttons.push_back({key, spr, xy.x, xy.y, w, h, lk});
    }
    long long right = std::max<long long>(bg->width(), ox + d.box.first + box_w);
    long long bottom = std::max<long long>(bg->height(), last + 8);
    for (const Button &b : buttons) {
        right = std::max(right, b.x + b.w);
        bottom = std::max(bottom, b.y + b.h);
    }
    QImage img(int(right + 2 * PAD), int(bottom + 2 * PAD), QImage::Format_ARGB32);
    img.fill(QColor(BACK));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(double(PAD), double(PAD));
    p.drawImage(0, 0, *bg);
    gamefont::Fonts *fonts = look ? look->fonts : nullptr;
    auto write = [&](const QString &font, const QString &s, double x, double y, double width = 0,
                     const QString &fmt = u"left"_s) {
        if (fonts && fonts->get(font))
            gamefont::draw(p, *fonts, font, s, x, y, width, 0, fmt);
        else {                                       // (the game's font is not there: a plain one)
            QFont f;
            f.setPixelSize(15);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(235, 225, 200));
            drawtext::draw(p, QRectF(x, y, width != 0 ? width : 400, 22),
                       (fmt != u"left" ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter, s);
        }
    };
    for (const Button &b : buttons) {
        if (b.spr)
            p.drawImage(QPointF(double(b.x), double(b.y)), *b.spr);
        else {
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(QColor(120, 100, 70));
            p.drawRect(QRectF(double(b.x), double(b.y), double(b.w), double(b.h)));
        }
        OptStr said = words ? words(std::get<1>(b.look)) : std::nullopt;
        const QString s = said && !said->isEmpty() ? *said : py::title(QString(std::get<1>(b.look)).replace(u'_', u' '));
        const auto got = fonts ? gamefont::layout(*fonts, std::get<2>(b.look), s, 0) : std::nullopt;
        const long long line = got ? got->font->line : 18;
        write(std::get<2>(b.look), s, double(b.x), double(b.y) + double(b.h - line) / 2.0, double(b.w), u"centre"_s);
    }
    Drawn res;
    QStringList squeezed;
    long long below = 0;
    std::set<QString> lit(multi.begin(), multi.end());
    if (sel && !sel->isEmpty())
        lit.insert(*sel);
    for (size_t r = 0; r < rows.size(); ++r) {
        const Row &row = rows[r];
        const long long oy = oy0 + static_cast<long long>(r) * step;
        write(d.font, row.name, double(ox + d.name.first), double(oy + d.name.second), double(d.name_w));
        res.hits.push_back({double(ox + d.name.first), double(oy + d.name.second), double(d.name_w), 22, u"@"_s + row.key,
                            true, true});
        if (lit.count(u"@"_s + row.key)) {
            p.setPen(QPen(QColor(255, 216, 74), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(double(ox + d.name.first - 4), double(oy + d.name.second - 2), double(d.name_w), 22));
        }
        const long long n = static_cast<long long>(row.cells.size());
        const bool fits = n <= 1 || double(n - 1) * pitch0 + double(item) <= double(box_w);
        const double pitch = fits ? pitch0 : double(box_w - item) / double(n - 1);
        if (!fits)
            squeezed << row.name;
        if (oy + d.box.second + item_h > bg->height() - 30)
            below += 1;
        std::vector<QRectF> marks;
        for (long long i = 0; i < n; ++i) {
            const auto &[key, num] = row.cells[size_t(i)];
            const double x = double(ox + d.box.first) + double(i) * pitch;
            const double y = double(oy + d.box.second);
            if (num && 1 <= num && num <= static_cast<long long>(frames.size()))
                p.drawPixmap(QPointF(x, y), frames[size_t(num - 1)]);
            else {
                p.setPen(QPen(QColor(255, 120, 110), 2, Qt::DashLine));
                p.setBrush(QColor(40, 30, 30));
                p.drawRect(QRectF(x + 2, y + 2, double(item - 4), double(item_h - 4)));
            }
            const bool lastc = i == n - 1;
            res.hits.push_back({x, y, lastc ? double(item) : std::max(5.0, pitch), double(item_h), key, false, lastc});
            if (lit.count(key))
                marks.emplace_back(x, y, double(item), double(item_h));
        }
        p.setPen(QPen(QColor(255, 216, 74), 2));     // the picked ones framed (over their neighbours)
        p.setBrush(Qt::NoBrush);
        for (const QRectF &m : marks)
            p.drawRect(m);
    }
    for (long long sh : SCREENS) {                   // where a small screen ends
        const long long yl = sh - WINDOW_Y;
        if (bottom > yl) {
            p.setPen(QPen(QColor(255, 120, 110), 2, Qt::DashLine));
            p.drawLine(QPointF(double(-PAD), double(yl)), QPointF(double(right + PAD), double(yl)));
            QFont f;
            f.setPixelSize(12);
            f.setBold(true);
            p.setFont(f);
            drawtext::draw(p, QRectF(0, double(yl - 18), double(right), 16), Qt::AlignRight | Qt::AlignVCenter,
                       u"a %1-high screen ends here"_s.arg(sh));
            res.notes << u"the window reaches below a %1-high screen"_s.arg(sh);
        }
    }
    p.end();
    if (below)
        res.notes << u"%1 row%2 hang%3 below the picture"_s.arg(below).arg(below == 1 ? u""_s : u"s"_s,
                                                                        below == 1 ? u"s"_s : u""_s);
    if (!squeezed.isEmpty())
        res.notes << u"pushed over each other: "_s + squeezed.mid(0, 4).join(u", "_s) + (squeezed.size() > 4 ? u" …"_s : u""_s);
    res.img = img;
    return res;
}

}  // namespace focuswindow
