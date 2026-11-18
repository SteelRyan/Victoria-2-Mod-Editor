// widgets.py — Reusable widgets: grid flow layout, checkbox chip groups, the drag-to-resize split bar and pie.
// (Python overrides a few non-virtual Qt functions — ElideLabel / WrapButton::setText, BadgeButton /
// SliderField::setToolTip: here they are functions of the subclass; callers hold the subclass type.)
#pragma once
#include "core/ordered_map.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QLineEdit;
class QSlider;
class QPainter;

namespace widgets {

using Rgb = std::tuple<int, int, int>;

class GridFlow : public QLayout {             // equal-width cells, column count from the available width only
public:
    explicit GridFlow(QWidget *parent = nullptr, int spacing = 5, int min_cell = 175, int row_h = 26);
    ~GridFlow() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int i) const override;
    QLayoutItem *takeAt(int i) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int _cols(int w) const;
    QList<QLayoutItem *> _items;
    int _sp, _min, _row;
};

class WrapRow : public QLayout {              // fixed-size items left to right, wrapping when the width runs out
public:
    explicit WrapRow(QWidget *parent = nullptr, int spacing = 4, int max_per_row = 0);
    ~WrapRow() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int i) const override;
    QLayoutItem *takeAt(int i) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int _lay(const QRect &rect, bool place) const;
    QList<QLayoutItem *> _items;
    int _sp, _max;
};

class WrapButton : public QPushButton {       // text wraps onto more lines instead of making its panel wider
    Q_OBJECT
public:
    explicit WrapButton(const QString &text = QString(), QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QLabel *_lab;
};

class ElideLabel : public QLabel {            // one line, cut with "…" (the whole text in the tooltip)
    Q_OBJECT
public:
    explicit ElideLabel(const QString &text = QString(), QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const { return _full; }
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *ev) override;
    void changeEvent(QEvent *ev) override;

private:
    void _elide();
    QString _full;
};

QString fmt(long long n);                     // "{:,}".format(int(n))
QIcon swatch_icon(const Rgb &rgb, int size = 10);

class Chip : public QAbstractButton {         // checkable filter chip: [✓] ■ Name ........ 66,785 (24,035)
    Q_OBJECT
public:
    Chip(const QString &label, const Rgb &rgb, std::optional<long long> amount, QWidget *parent = nullptr);
    QString label;
    Rgb rgb;
    std::optional<long long> amount, narrowed;
    void set_narrowed(std::optional<long long> n);
    QSize sizeHint() const override { return {175, 26}; }

protected:
    void paintEvent(QPaintEvent *ev) override;

private:
    void _tip();
};

class ChipGroup : public QWidget {            // a labelled flow of chips with Select all / none / invert links
    Q_OBJECT
public:
    explicit ChipGroup(const QString &title, QWidget *parent = nullptr, bool invert = true);
    struct ItemSpec {
        QString key, label;
        Rgb rgb;
        std::optional<long long> amount;
    };
    QSet<QString> unchecked;
    OrderedMap<QString, Chip *> boxes;
    QWidget *flow_host;
    GridFlow *flow;
    void set_items(const std::vector<ItemSpec> &items);
    void set_narrowed(const std::optional<OrderedMap<QString, long long>> &counts);
    void select_all();
    void select_none();
    void invert();
    QSet<QString> checked() const;
Q_SIGNALS:
    void changed();

private:
    void _toggled(const QString &key, bool on);
    void _set_all(const std::function<bool(const QString &, bool)> &fn);
};

struct Segment {                              // {"label", "pct", "locked", "color"} (+ what the pages keep in it)
    QString label;
    double pct = 0;
    bool locked = false;
    Rgb color{0, 0, 0};
    std::optional<QString> key;               // panels' mix: the culture / religion / pop type (None: a pop without)
    long long pid = 0;                        // panels' split: the target province
};
// Python's set_segments keeps the list itself: the page, the bars and the pie share one list of the same dicts, and
// a drag in one is what the others (and the page) read. The port shares the vector the same way.
using Segs = std::shared_ptr<std::vector<Segment>>;
void push_edge(std::vector<Segment> &segments, long long i, double pos);
extern const std::vector<Rgb> SEG_COLORS;

class SplitBar : public QWidget {             // stacked bar; drag a divider to move share between its neighbours
    Q_OBJECT
public:
    explicit SplitBar(QWidget *parent = nullptr);
    Segs segments_ = std::make_shared<std::vector<Segment>>();
    std::vector<Segment> &seg() const { return *segments_; }   // (Python: self.segments)
    std::optional<QString> empty_text;        // (Python: getattr(self, "empty_text", …))
    bool push = false;
    void set_segments(const Segs &segs);
    void set_segments(const std::vector<Segment> &segs) { set_segments(std::make_shared<std::vector<Segment>>(segs)); }
Q_SIGNALS:
    void changed();

protected:
    QRectF _bar() const;
    virtual std::vector<double> _edges() const;
    std::optional<long long> _hit(double x) const;
    void paintEvent(QPaintEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    std::optional<long long> _drag;
    std::vector<double> _base;
};

class ZoomBar : public SplitBar {             // magnified view of part of a bar (lo..hi %)
    Q_OBJECT
public:
    explicit ZoomBar(QWidget *parent = nullptr);
    double lo = 0, hi = 100;
    void set_range(double lo, double hi);
Q_SIGNALS:
    void released();

protected:
    double _x(double pct) const;
    std::vector<double> _edges() const override;
    void paintEvent(QPaintEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
};

class PieChart : public QWidget {             // editable pie; same segments as SplitBar
    Q_OBJECT
public:
    explicit PieChart(QWidget *parent = nullptr);
    Segs segments_ = std::make_shared<std::vector<Segment>>();
    std::vector<Segment> &seg() const { return *segments_; }   // (Python: self.segments)
    std::vector<Segment> outer;               // an optional context ring, not editable
    bool push = false;
    void set_segments(const Segs &segs);
    void set_segments(const std::vector<Segment> &segs) { set_segments(std::make_shared<std::vector<Segment>>(segs)); }
    void set_outer(const std::vector<Segment> &segs);
Q_SIGNALS:
    void changed();

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;

private:
    std::tuple<double, double, double> _geom() const;
    std::vector<double> _bounds() const;
    std::tuple<double, double, double> _angle(const QPointF &pos) const;
    std::optional<long long> _hit(const QPointF &pos) const;
    std::optional<long long> _drag;
    std::optional<double> _last_ang;
    std::vector<double> _base;
};

class PageStack : public QWidget {            // pages of which one is on show — and only that one counts for size
    Q_OBJECT
public:
    explicit PageStack(QWidget *parent = nullptr);
    int addWidget(QWidget *w);
    int count() const { return int(_pages.size()); }
    QWidget *widget(int i) const;
    int indexOf(QWidget *w) const;
    int currentIndex() const { return _cur; }
    QWidget *currentWidget() const { return widget(_cur); }
    void setCurrentIndex(int i);
    void setCurrentWidget(QWidget *w) { setCurrentIndex(indexOf(w)); }
Q_SIGNALS:
    void currentChanged(int i);

private:
    QVBoxLayout *_lay;
    std::vector<QWidget *> _pages;
    int _cur = -1;
};

class Section : public QFrame {
    Q_OBJECT
public:
    explicit Section(QWidget *parent = nullptr);
    QVBoxLayout *lay;
};

class SliderField : public QWidget {          // a slider for the usual range + a box for any value up to the max
    Q_OBJECT
public:
    SliderField(double minimum, double soft_max, double hard_max, double value, const QString &suffix = QString(),
                bool log = false, int decimals = 0, QWidget *parent = nullptr);
    QSlider *slider;
    QLineEdit *edit;
    double value() const { return _val; }    // (a whole number when decimals = 0)
    void setValue(double v);
    double minimum() const { return _min; }
    double maximum() const { return _hard; }
    void setToolTip(const QString &text);
    void set_snaps(const std::vector<double> &values, double tolerance = 0.03);
Q_SIGNALS:
    void valueChanged(double v);

private:
    int _to_pos(double v) const;
    double _from_pos(int pos) const;
    double _round(double v) const;
    QString _text(double v) const;
    void _show();
    void _slid(int pos);
    void _typed();
    double _min, _soft, _hard;
    bool _log;
    int _dec;
    QString _suffix;
    std::optional<double> _valopt;
    double _val = 0;
    std::vector<std::pair<double, double>> _snaps;
};

// count circles at the right end of a rect: red with the errors, amber with the warnings
void paint_counts(QPainter &p, const QRect &rect, long long errors, long long warnings, int right = 5);

class BadgeButton : public QPushButton {      // a button that can carry counts (errors red, warnings amber)
    Q_OBJECT
public:
    explicit BadgeButton(const QString &text = QString(), QWidget *parent = nullptr);
    long long errors = 0, warnings = 0;
    void setToolTip(const QString &tip);
    void set_counts(long long errors, long long warnings = 0);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void _show_tip();
    QString _tip;
};

class TintHeader : public QHeaderView {       // marks some columns with a thick coloured bar under their title
    Q_OBJECT
public:
    TintHeader(const QColor &color, const QSet<int> &cols = {}, QWidget *parent = nullptr);
    QColor color;
    QSet<int> cols;

protected:
    void paintSection(QPainter *p, const QRect &rect, int i) const override;
};

class HelpDot : public QLabel {               // a round gold "?" — hovering it shows a note at once
    Q_OBJECT
public:
    explicit HelpDot(const QString &note = QString(), QWidget *parent = nullptr);
    QString note;
    void set_note(const QString &note);

protected:
    void enterEvent(QEnterEvent *e) override;
};

QLabel *label(const QString &text, const QString &obj = QStringLiteral("label"), bool wrap = true);

class Stepper : public QWidget {              // a whole number with big − / ＋ buttons (never spin-box arrows)
    Q_OBJECT
public:
    explicit Stepper(long long minimum = 0, long long maximum = 99, long long value = 0, QWidget *parent = nullptr);
    QPushButton *minus, *plus;
    QLineEdit *edit;
    long long value() const { return _val; }
    void setValue(long long v);
    void setChoices(const std::vector<long long> &values);
    void setRange(long long lo, long long hi);
Q_SIGNALS:
    void valueChanged(int v);

private:
    void _typed();
    void _step(int d);
    long long _min, _max, _val = 0;
    bool _has_val = false;
    std::vector<long long> _choices;
};

class StepSlider : public QWidget {           // a slider that snaps to named positions
    Q_OBJECT
public:
    explicit StepSlider(const QStringList &names, int value = 0, QWidget *parent = nullptr);
    QStringList names;
    int val;
    QSize sizeHint() const override { return {300, 74}; }
    QSize minimumSizeHint() const override { return {160, 74}; }
    int value() const { return val; }
    void setValue(int v);
Q_SIGNALS:
    void valueChanged(int v);

protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void paintEvent(QPaintEvent *ev) override;

private:
    std::vector<double> _xs() const;
    int _pick(double x) const;
    void _set_from(double x);
    bool _drag = false;
    int _start = 0;
};

extern const int HEAD_ROLE;                   // a list row with this set is a group heading, not something to pick
// a delegate for a combo box's list whose group headings stand apart (dark ground, gold capitals, a gold line above)
QStyledItemDelegate *heading_delegate(QObject *parent = nullptr);

}  // namespace widgets
