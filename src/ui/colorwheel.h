// colorwheel.py — Picking a colour: a wheel (hue around, strength towards the rim), a bar for the brightness, the
// colour as #rrggbb and as red / green / blue — plus the last three colours used and the ones the user saved. While
// the wheel is open every move is shown at once (`preview`); closing it uses the colour (`changed`), Cancel puts the
// old one back.
#pragma once
#include <QColor>
#include <QFrame>
#include <QImage>
#include <QPushButton>
#include <QStringList>
#include <QWidget>
#include <map>
#include <optional>
#include <vector>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;

namespace colorwheel {

inline std::map<QString, QStringList> *STORE = nullptr;   // a dict instead of the app's settings (tests)
inline constexpr int RECENT_N = 3;
inline constexpr int SAVED_MAX = 32;

QStringList recent();
void push_recent(const QString &color);
QStringList saved();
void save_color(const QString &color);
void unsave_color(const QString &color);
QImage _disc(int size);                          // the wheel's picture at full brightness

class Wheel : public QWidget {                   // the wheel and its brightness bar. h 0–359, s and v 0–1
    Q_OBJECT
public:
    static constexpr int D = 230, BAR = 30, GAP = 14;
    explicit Wheel(QWidget *parent = nullptr);
    double h = 0.0, s = 0.0, v = 1.0;
    std::optional<QString> _drag;
    void set_color(const QColor &c);
    QColor color() const;
    QRectF _bar() const;
    void _at(const QPointF &pos, bool start = false);
Q_SIGNALS:
    void moved();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
};

class _Swatch : public QPushButton {
    Q_OBJECT
public:
    _Swatch(const QString &color, QWidget *parent = nullptr, bool removable = false);
    QString color;
    bool removable;
Q_SIGNALS:
    void removed(const QString &color);

protected:
    void mousePressEvent(QMouseEvent *e) override;
};

class ColorPopup : public QFrame {
    Q_OBJECT
public:
    explicit ColorPopup(const QString &color, QWidget *parent = nullptr);
    QString start;
    bool _done = false, _building = false;
    Wheel *wheel;
    QLabel *was, *now;
    QLineEdit *hex;
    std::vector<QLineEdit *> rgb;
    QHBoxLayout *recent_row;
    QWidget *saved_host;
    QGridLayout *saved_grid;
    QLabel *saved_note;
    void _fill_swatches();
    void _save();
    void _unsave(const QString &c);
    QString color() const;
    void set_color(const QColor &c);
    void _show(bool emit = true);
    void _wheel_moved();
    void _hex_done();
    void _rgb_done();
    void _cancel();
    void show_at(QWidget *widget);
Q_SIGNALS:
    void preview(const QString &c);              // while looking for a colour
    void chosen(const QString &c);               // the popup closed with this colour
    void cancelled(const QString &c);            // … or was cancelled: the colour it started with

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void hideEvent(QHideEvent *e) override;
};

class ColorButton : public QPushButton {          // a colour: click for the colour wheel
    Q_OBJECT
public:
    explicit ColorButton(const QString &color, QWidget *parent = nullptr);
    QString color;
    ColorPopup *popup = nullptr;
    void _paint(const QString &shown = QString());
    void set(const QString &color, bool emit = true);
    void _open();
Q_SIGNALS:
    void changed(const QString &c);              // a colour was chosen
    void preview(const QString &c);              // the wheel is on this colour right now (not chosen yet)
};

}  // namespace colorwheel
