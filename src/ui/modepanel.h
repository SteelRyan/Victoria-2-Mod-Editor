// modepanel.py — The map modes as a wide panel (the user, 2026-10-15): instead of one long, narrow list, every
// category is a card with a coloured header bar and its page's icon, every mode a large row with a small swatch of
// what the map will look like. The cards flow into as many columns as the window has room for (a small screen gets
// fewer columns and scrolls up / down only — never sideways); a filter box on top keeps only the modes whose name holds
// what is typed.
//
// ModeCombo keeps being the QComboBox the editor works with (findData, setCurrentIndex, …): only what opens on a click
// is this panel, and the closed box says "Category · Mode".
#pragma once
#include <QComboBox>
#include <QFrame>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QPainter;
class QRectF;

namespace modepanel {

using Rgb = std::tuple<int, int, int>;
inline constexpr int COLW = 250;              // one column of cards
inline constexpr int ROW_H = 30;
inline constexpr int HEAD_H = 32;
Rgb page_colour(const QString &page);

struct Group {                                // (title, page, [(row, key, text)])
    QString title, page;
    std::vector<std::tuple<int, QString, QString>> items;
};
std::vector<Group> groups_of(const QComboBox *combo);
QString short_title(const QString &title);

struct Swatch {                               // ("grad", stops) a gradient, a list of colours side by side, or None
    bool grad = false;
    std::vector<std::pair<double, Rgb>> stops;
    std::vector<Rgb> colours;
    bool empty() const { return grad ? stops.empty() : colours.empty(); }
};
void paint_swatch(QPainter &p, const QRectF &r, const std::optional<Swatch> &sw);
void draw_mark(QPainter &p, double cx, double cy, double r, const QString &colour);
QPixmap mark_pixmap(int size = 18, const QString &colour = QString());

class ModePanel;

class ModeCombo : public QComboBox {          // a click opens the wide panel; the closed box says "Category · Mode"
    Q_OBJECT
public:
    explicit ModeCombo(QWidget *parent = nullptr);
    std::function<std::optional<Swatch>(const QString &)> swatch;     // (set by the main window)
    std::function<std::optional<QString>(const QString &)> custom;    // "ranges" / "own" / None: colours of its own (◐)
    ModePanel *panel = nullptr;
    QString category(int i = -1) const;
    void showPopup() override;
    void hidePopup() override;

protected:
    void paintEvent(QPaintEvent *e) override;
};

class _Head : public QWidget {
    Q_OBJECT
public:
    _Head(const QString &title, const QString &page, QWidget *parent = nullptr);
    QString title, page;

protected:
    void paintEvent(QPaintEvent *e) override;
};

class _Row : public QWidget {
    Q_OBJECT
public:
    _Row(int row, const QString &key, const QString &text, std::optional<Swatch> sw, bool current, const Rgb &colour,
         std::optional<QString> custom = std::nullopt, QWidget *parent = nullptr);
    int row;
    QString key, text;
    std::optional<Swatch> sw;
    bool current;
    Rgb colour;
    std::optional<QString> custom;
    bool hot = false;
Q_SIGNALS:
    void picked(int row);

protected:
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
};

class ModePanel : public QFrame {             // every map mode at once: a card per category, flowing into columns
    Q_OBJECT
public:
    explicit ModePanel(ModeCombo *combo);
    ModeCombo *combo;
    std::vector<Group> groups;
    QLineEdit *filter;
    QLabel *none;
    QScrollArea *scroll;
    std::vector<std::pair<QFrame *, std::vector<_Row *>>> cards;
    std::vector<_Row *> rows;
    int ncols = 1;
    void place();
    void _build(int n);
    void _filter(const QString &text);
    void _pick_first();
    void _pick(int row);
};

}  // namespace modepanel
