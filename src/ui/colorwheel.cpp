// colorwheel.py — see colorwheel.h.
#include "ui/colorwheel.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/settings.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <cmath>

using namespace Qt::StringLiterals;

namespace colorwheel {

static QStringList _get(const QString &key)
{
    if (STORE)
        return STORE->count(key) ? STORE->at(key) : QStringList();
    const QVariant v = settings_::settings()->value(u"colors/"_s + key, QStringList());
    QStringList l;
    if (v.typeId() == QMetaType::QString) {
        if (!v.toString().isEmpty())
            l << v.toString();
    } else {
        l = v.toStringList();
    }
    QStringList out;
    for (const QString &c : l)
        if (QColor(c).isValid())
            out << c;
    return out;
}

static void _put(const QString &key, const QStringList &lst)
{
    if (STORE) {
        (*STORE)[key] = lst;
        return;
    }
    settings_::settings()->setValue(u"colors/"_s + key, lst);
}

QStringList recent() { return _get(u"recent"_s).mid(0, RECENT_N); }

void push_recent(const QString &color)
{
    const QString c = QColor(color).name();
    QStringList l{c};
    for (const QString &x : _get(u"recent"_s))
        if (x != c)
            l << x;
    _put(u"recent"_s, l.mid(0, RECENT_N));
}

QStringList saved() { return _get(u"saved"_s).mid(0, SAVED_MAX); }

void save_color(const QString &color)
{
    const QString c = QColor(color).name();
    QStringList l;
    for (const QString &x : _get(u"saved"_s))
        if (x != c)
            l << x;
    l << c;
    _put(u"saved"_s, l.mid(std::max<qsizetype>(0, l.size() - SAVED_MAX)));
}

void unsave_color(const QString &color)
{
    const QString c = QColor(color).name();
    QStringList l;
    for (const QString &x : _get(u"saved"_s))
        if (x != c)
            l << x;
    _put(u"saved"_s, l);
}

QImage _disc(int size)
{
    // hue around, saturation outwards — numpy's float32 arithmetic, step by step
    static std::map<int, QImage> cache;
    if (const auto it = cache.find(size); it != cache.end())
        return it->second;
    const double r = size / 2.0;
    const float rf = float(r);
    const float RAD2DEG = float(180.0 / 3.141592653589793);
    QImage img(size, size, QImage::Format_RGBA8888);
    for (int yi = 0; yi < size; ++yi) {
        uchar *row = img.scanLine(yi);
        for (int xi = 0; xi < size; ++xi) {
            const float dx = (float(xi) - rf) + 0.5f, dy = (float(yi) - rf) + 0.5f;
            const float d = std::sqrt(dx * dx + dy * dy) / rf;
            float deg = std::atan2(-dy, dx) * RAD2DEG;
            float m = std::fmod(deg, 360.0f);              // (numpy's float32 remainder)
            if (m != 0.0f) {
                if ((m < 0.0f) != (360.0f < 0.0f))
                    m += 360.0f;
            } else {
                m = std::copysign(0.0f, 360.0f);
            }
            const float h = m / 60.0f;
            const float s = std::min(std::max(d, 0.0f), 1.0f);
            const float fl = std::floor(h);
            const int i = ((int(fl) % 6) + 6) % 6;
            const float f = h - fl;
            const float p = 1.0f - s, q = 1.0f - s * f, t = 1.0f - s * (1.0f - f);
            const float rr[6] = {1.0f, q, p, p, t, 1.0f}, gg[6] = {t, 1.0f, 1.0f, q, p, p},
                        bb[6] = {p, p, t, 1.0f, 1.0f, q};
            float a = (1.0f - d) * rf + 0.5f;
            a = std::min(std::max(a, 0.0f), 1.0f);
            row[xi * 4] = uchar(rr[i] * 255.0f);
            row[xi * 4 + 1] = uchar(gg[i] * 255.0f);
            row[xi * 4 + 2] = uchar(bb[i] * 255.0f);
            row[xi * 4 + 3] = uchar(a * 255.0f);
        }
    }
    cache[size] = img;
    return img;
}

// ---------------------------------------------------------------------------------------------- Wheel
Wheel::Wheel(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setFixedSize(D + GAP + BAR + 4, D + 4);
    setCursor(Qt::CrossCursor);
}

void Wheel::set_color(const QColor &c)
{
    const float hh = c.hsvHueF();
    if (hh >= 0)                                   // (grey has no hue: the wheel keeps the one it had)
        h = double(hh) * 360.0;
    s = c.hsvSaturationF();
    v = c.valueF();
    update();
}

QColor Wheel::color() const
{
    return QColor::fromHsvF(float(py::mod(h, 360.0) / 360.0), float(std::min(1.0, std::max(0.0, s))),
                            float(std::min(1.0, std::max(0.0, v))));
}

QRectF Wheel::_bar() const { return QRectF(D + GAP + 2, 2, BAR, D); }

void Wheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(2, 2, _disc(D));
    if (v < 1.0) {                                 // darker: black over the wheel
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, int(py::round_int((1.0 - v) * 255))));
        p.drawEllipse(QRectF(2, 2, D, D));
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(theme::LINE), 2));
    p.drawEllipse(QRectF(2, 2, D, D));
    const double r = D / 2.0;
    const double a = h * (3.141592653589793 / 180.0);
    const QPointF pt(2 + r + std::cos(a) * s * r, 2 + r - std::sin(a) * s * r);
    p.setPen(QPen(QColor(u"#000000"_s), 4));
    p.setBrush(color());
    p.drawEllipse(pt, 9, 9);
    p.setPen(QPen(QColor(u"#ffffff"_s), 2));
    p.drawEllipse(pt, 9, 9);
    const QRectF bar = _bar();
    const QColor full = QColor::fromHsvF(float(py::mod(h, 360.0) / 360.0), float(s), 1.0f);
    for (int i = 0; i < int(bar.height()); ++i) {
        const double k = 1.0 - i / (bar.height() - 1);
        p.setPen(QColor(int(full.red() * k), int(full.green() * k), int(full.blue() * k)));
        p.drawLine(QPointF(bar.left(), bar.top() + i), QPointF(bar.right(), bar.top() + i));
    }
    p.setPen(QPen(QColor(theme::LINE), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);
    const double y = bar.top() + (1.0 - v) * (bar.height() - 1);
    p.setPen(QPen(QColor(u"#000000"_s), 5));
    p.drawLine(QPointF(bar.left() - 3, y), QPointF(bar.right() + 3, y));
    p.setPen(QPen(QColor(u"#ffffff"_s), 2));
    p.drawLine(QPointF(bar.left() - 3, y), QPointF(bar.right() + 3, y));
    p.end();
}

void Wheel::_at(const QPointF &pos, bool start)
{
    const double r = D / 2.0;
    if (start) {
        const double dx = pos.x() - 2 - r, dy = pos.y() - 2 - r;
        if (py::hypot(dx, dy) <= r + 4)
            _drag = u"disc"_s;
        else if (pos.x() >= D + GAP - 4)
            _drag = u"bar"_s;
        else
            _drag.reset();
    }
    if (_drag == u"disc") {
        const double dx = pos.x() - 2 - r, dy = pos.y() - 2 - r;
        h = py::mod(std::atan2(-dy, dx) * (180.0 / 3.141592653589793), 360.0);
        s = std::min(1.0, py::hypot(dx, dy) / r);
        if (s < 0.04)
            s = 0.0;                               // the middle catches: plain white / grey
    } else if (_drag == u"bar") {
        const QRectF bar = _bar();
        v = std::min(1.0, std::max(0.0, 1.0 - (pos.y() - bar.top()) / (bar.height() - 1)));
    } else {
        return;
    }
    update();
    Q_EMIT moved();
}

void Wheel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        _at(e->position(), true);
}

void Wheel::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        _at(e->position());
}

void Wheel::mouseReleaseEvent(QMouseEvent *) { _drag.reset(); }

// ---------------------------------------------------------------------------------------------- _Swatch
_Swatch::_Swatch(const QString &color_, QWidget *parent, bool removable_)
    : QPushButton(parent), color(color_), removable(removable_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setFixedSize(34, 28);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(u"QPushButton { background: %1; border: 2px solid #000000; border-radius: 4px; } "
                  u"QPushButton:hover { border-color: %2; }"_s.arg(color, theme::GOLD));
    setToolTip(color + (removable ? u"  —  right-click takes it off the saved ones"_s : u""_s));
}

void _Swatch::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton && removable) {
        Q_EMIT removed(color);
        return;
    }
    QPushButton::mousePressEvent(e);
}

// ---------------------------------------------------------------------------------------------- ColorPopup
ColorPopup::ColorPopup(const QString &color_, QWidget *parent) : QFrame(parent, Qt::Popup)
{
    start = QColor(color_).name();
    setObjectName(u"colorPopup"_s);
    setStyleSheet(u"#colorPopup { background: %1; border: 2px solid %2; border-radius: 6px; }"_s.arg(theme::PANEL,
                                                                                                    theme::GOLD_DIM));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    wheel = new Wheel(this);
    connect(wheel, &Wheel::moved, this, [this] { _wheel_moved(); });
    v->addWidget(wheel, 0, Qt::AlignHCenter);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    was = new QLabel(this);
    now = new QLabel(this);
    for (const auto &[lb, tip] : {std::pair<QLabel *, QString>{was, u"The colour before"_s}, {now, u"The colour picked"_s}}) {
        lb->setFixedSize(46, 30);
        lb->setToolTip(tip);
        row->addWidget(lb);
    }
    was->setStyleSheet(u"background: %1; border: 2px solid #000000; border-radius: 4px;"_s.arg(start));
    hex = new QLineEdit(this);
    hex->setFixedWidth(84);
    hex->setPlaceholderText(u"#rrggbb"_s);
    connect(hex, &QLineEdit::editingFinished, this, [this] { _hex_done(); });
    row->addWidget(hex);
    for (const QString &name : {u"R"_s, u"G"_s, u"B"_s}) {
        row->addWidget(widgets::label(name, u"sub"_s, false));
        auto *ed = new QLineEdit(this);
        ed->setFixedWidth(42);
        ed->setAlignment(Qt::AlignRight);
        connect(ed, &QLineEdit::editingFinished, this, [this] { _rgb_done(); });
        row->addWidget(ed);
        rgb.push_back(ed);
    }
    row->addStretch(1);
    v->addLayout(row);
    v->addWidget(widgets::label(u"Last used"_s, u"sub"_s, false));
    recent_row = new QHBoxLayout();
    recent_row->setSpacing(6);
    v->addLayout(recent_row);
    auto *srow = new QHBoxLayout();
    srow->addWidget(widgets::label(u"Saved colours"_s, u"sub"_s, false), 1);
    auto *b = new QPushButton(u"＋ Save this colour"_s, this);
    connect(b, &QPushButton::clicked, this, [this] { _save(); });
    srow->addWidget(b);
    v->addLayout(srow);
    saved_host = new QWidget(this);
    saved_grid = new QGridLayout(saved_host);
    saved_grid->setContentsMargins(0, 0, 0, 0);
    saved_grid->setSpacing(5);
    v->addWidget(saved_host);
    saved_note = widgets::label(u""_s, u"faint"_s);
    v->addWidget(saved_note);
    auto *brow = new QHBoxLayout();
    auto *ok = new QPushButton(u"Use this colour"_s, this);
    ok->setObjectName(u"apply"_s);
    connect(ok, &QPushButton::clicked, this, &QWidget::close);
    brow->addWidget(ok, 1);
    auto *cancel = new QPushButton(u"Cancel"_s, this);
    connect(cancel, &QPushButton::clicked, this, [this] { _cancel(); });
    brow->addWidget(cancel);
    v->addLayout(brow);
    wheel->set_color(QColor(start));
    _fill_swatches();
    _show(false);
}

void ColorPopup::_fill_swatches()
{
    while (recent_row->count()) {
        QLayoutItem *it = recent_row->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    const QStringList rec = recent();
    for (const QString &c : rec) {
        auto *s = new _Swatch(c, this);
        connect(s, &QPushButton::clicked, this, [this, c] { set_color(QColor(c)); });
        recent_row->addWidget(s);
    }
    if (rec.isEmpty())
        recent_row->addWidget(widgets::label(u"none yet"_s, u"faint"_s, false));
    recent_row->addStretch(1);
    while (saved_grid->count()) {
        QLayoutItem *it = saved_grid->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    const QStringList sv = saved();
    for (qsizetype i = 0; i < sv.size(); ++i) {
        const QString c = sv[i];
        auto *s = new _Swatch(c, saved_host, true);
        connect(s, &QPushButton::clicked, this, [this, c] { set_color(QColor(c)); });
        connect(s, &_Swatch::removed, this, [this](const QString &x) { _unsave(x); });
        saved_grid->addWidget(s, int(i / 7), int(i % 7));
    }
    saved_grid->setColumnStretch(7, 1);
    saved_note->setText(!sv.isEmpty() ? u"Right-click a saved colour to take it off."_s
                                      : u"None saved yet — “Save this colour” keeps the one picked for every nation "
                                        u"and flag."_s);
}

void ColorPopup::_save()
{
    save_color(color());
    _fill_swatches();
}

void ColorPopup::_unsave(const QString &c)
{
    unsave_color(c);
    _fill_swatches();
}

QString ColorPopup::color() const { return wheel->color().name(); }

void ColorPopup::set_color(const QColor &c)
{
    if (c.isValid()) {
        wheel->set_color(c);
        _show();
    }
}

void ColorPopup::_show(bool emit)
{
    const QColor c = wheel->color();
    _building = true;
    now->setStyleSheet(u"background: %1; border: 2px solid #000000; border-radius: 4px;"_s.arg(c.name()));
    if (!hex->hasFocus())
        hex->setText(c.name());
    const int vals[3] = {c.red(), c.green(), c.blue()};
    for (size_t i = 0; i < rgb.size(); ++i)
        if (!rgb[i]->hasFocus())
            rgb[i]->setText(QString::number(vals[i]));
    _building = false;
    if (emit)
        Q_EMIT preview(c.name());
}

void ColorPopup::_wheel_moved() { _show(); }

void ColorPopup::_hex_done()
{
    const QString t = py::strip(hex->text());
    const QColor c(t.startsWith(u'#') ? t : u"#"_s + t);
    if (c.isValid() && c.name() != color()) {
        wheel->set_color(c);
        _show();
    }
}

void ColorPopup::_rgb_done()
{
    int vals[3];
    try {
        for (size_t i = 0; i < 3; ++i)
            vals[i] = int(std::max(0LL, std::min(255LL, py::to_int(py::strip(rgb[i]->text())))));
    } catch (const py::ValueError &) {
        _show(false);
        return;
    }
    const QColor c(vals[0], vals[1], vals[2]);
    if (c.name() != color()) {
        wheel->set_color(c);
        _show();
    }
}

void ColorPopup::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        _cancel();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        QWidget *w = focusWidget();
        if (w == hex)
            _hex_done();
        else if (std::find(rgb.begin(), rgb.end(), w) != rgb.end())
            _rgb_done();
        return;
    }
    QFrame::keyPressEvent(e);
}

void ColorPopup::_cancel()
{
    _done = true;
    Q_EMIT cancelled(start);
    close();
}

void ColorPopup::hideEvent(QHideEvent *e)
{
    if (!_done) {
        _done = true;
        QWidget *w = focusWidget();                // a number typed but not confirmed still counts
        if (w == hex)
            _hex_done();
        else if (std::find(rgb.begin(), rgb.end(), w) != rgb.end())
            _rgb_done();
        const QString c = color();
        if (c != start) {
            push_recent(c);
            Q_EMIT chosen(c);
        } else {
            Q_EMIT cancelled(start);
        }
    }
    QFrame::hideEvent(e);
}

void ColorPopup::show_at(QWidget *widget)
{
    adjustSize();
    const QPoint pt = widget->mapToGlobal(QPoint(0, widget->height() + 2));
    QScreen *scr = QGuiApplication::screenAt(pt);
    if (!scr)
        scr = QGuiApplication::primaryScreen();
    const QRect g = scr->availableGeometry();
    const int x = std::min(std::max(g.left() + 4, pt.x()), g.right() - width() - 4);
    int y = pt.y();
    if (y + height() > g.bottom() - 4)
        y = std::max(g.top() + 4, widget->mapToGlobal(QPoint(0, 0)).y() - height() - 2);
    move(x, y);
    show();
}

// ---------------------------------------------------------------------------------------------- ColorButton
ColorButton::ColorButton(const QString &color_, QWidget *parent) : QPushButton(parent)
{
    color = QColor(color_).name();
    setObjectName(u"colorBtn"_s);                  // (its style must not reach the buttons of its popup)
    setFixedSize(46, 28);
    setCursor(Qt::PointingHandCursor);
    connect(this, &QPushButton::clicked, this, [this] { _open(); });
    _paint();
}

void ColorButton::_paint(const QString &shown)
{
    const QString c = shown.isEmpty() ? color : shown;
    setStyleSheet(u"QPushButton#colorBtn { background: %1; border: 2px solid %2; border-radius: 4px; }"_s.arg(c,
                                                                                                          theme::LINE));
    setToolTip(u"%1 — click for the colour wheel"_s.arg(c));
}

void ColorButton::set(const QString &color_, bool emit)
{
    const QColor c(color_);
    if (!c.isValid())
        return;
    color = c.name();
    _paint();
    if (emit)
        Q_EMIT changed(color);
}

void ColorButton::_open()
{
    auto *pop = popup = new ColorPopup(color, this);
    connect(pop, &ColorPopup::preview, this, [this](const QString &c) {
        _paint(c);
        Q_EMIT preview(c);
    });
    connect(pop, &ColorPopup::cancelled, this, [this](const QString &c) {
        _paint();
        Q_EMIT preview(c);
    });
    connect(pop, &ColorPopup::chosen, this, [this](const QString &c) { set(c); });
    pop->show_at(this);
}

}  // namespace colorwheel
