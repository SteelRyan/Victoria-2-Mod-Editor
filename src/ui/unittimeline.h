// unittimeline.py — Warfare → Unit timeline: when every unit's stats change, and by which technology — one lane per
// unit over the years, a marker at every technology (gold) or invention (blue) that changes it. A click sets the year:
// the table below shows every unit's stats IN TOTAL at that point (base + everything up to that year), the list beside
// it the changes of the picked unit — and their numbers are typed right there: that edits the technology (one undoable
// step, written on Apply), without going to the Technology page.
//
// How it is counted (my reading of the files, not measured in the game): a number a technology gives a unit is added
// to the unit's base number; army_base / navy_base lines count for every land / naval unit; an invention counts from
// the year of the technology its limit names (in the game it then comes by chance).
//
// Units are ticked in the table (their colour square is the graph's legend): "Only the ticked" leaves just them in the
// lanes and the table; "Graph" draws them — a chart per picked stat, the years from left to right, the stat's total
// upwards, a line per unit that steps where a technology changes it.
#pragma once
#include "core/ordered_map.h"
#include "ui/theme.h"

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }
namespace techdata { class TechEdits; }
namespace widgets { class WrapRow; }

namespace unittimeline {

using namespace Qt::StringLiterals;   // (TechPopup's texts, written here)

using OptStr = std::optional<QString>;

extern const QStringList LAND, NAVAL, GRAPH_LAND, LINES;
extern const QHash<QString, QString> SHORT, BASES, SUPPORT_STATS;
extern const QString TECH_COL, INV_COL;
constexpr int LEFT = 210, ROW_H = 34;

QString g(double v);
long long year_step(double span);

struct Unit {
    QString rel;
    bool naval = false;
    OrderedMap<QString, double> vals;
};
using Units = OrderedMap<QString, Unit>;

struct Change {                                  // one number a technology / invention gives a unit
    long long year = 0;
    QString src, kind, target, stat;
    double value = 0.0;
    std::vector<qsizetype> ipath;
    OptStr after;
};
using Steps = std::vector<std::pair<double, double>>;   // [(year, total from that year on)]

Units read_units(const moddata::Mod &mod, scriptstore::ScriptStore &store);
std::vector<Change> read_changes(techdata::TechEdits &techs, const Units &units);
std::vector<Change> for_unit(const std::vector<Change> &changes, const QString &unit, bool naval, bool inventions = true);
OrderedMap<QString, std::pair<double, double>> totals(const Units &units, const std::vector<Change> &changes,
                                                      const QString &unit, long long year, bool inventions = true);
Steps series(const Units &units, const std::vector<Change> &changes, const QString &unit, const QString &stat,
             long long y0, bool inventions = true);

// ---------------------------------------------------------------------------------------------- the graph
struct Line {
    QString name, col;
    Steps pts;
};
using Panel = std::pair<QString, std::vector<Line>>;   // (stat name, lines)

class Graph : public QWidget {
    // A chart per stat, one under the other: the years from left to right, the stat's total upwards; a line per
    // unit. Wheel: zoom into the years under the mouse; drag: move along the years; a click: the year. The line
    // under the mouse is drawn thicker and named.
    Q_OBJECT
public:
    explicit Graph(QWidget *parent = nullptr);
    std::vector<Panel> panels;
    long long y0 = 1836, y1 = 1936, year = 1836;
    int right_pad = 60;
    double v0 = 1836.0, v1 = 1936.0;             // the years shown (the whole time, or a part of it)
    QString note;
    std::optional<std::pair<int, int>> hover;    // (chart, line) under the mouse
    std::map<int, int> labels;                   // chart -> how many numbers were written on it
    void set_data(std::vector<Panel> panels, long long y0, long long y1, long long year, const QString &note = {});
    bool whole() const;
    void set_view(double v0, double v1);
    void zoom(double factor, std::optional<double> at_year = std::nullopt);
    void fit();
    void fit_changes();                          // (C++ only) from a little before the first change to after the last
    double x_of(double year) const;
    double year_f(double x) const;
    long long year_at(double x) const;
    std::vector<QRectF> _boxes() const;
    static double value_at(const Steps &pts, double year);
    std::pair<double, double> _range(const std::vector<Line> &lines) const;
    double _y(const QRectF &box, double lo, double hi, double v, int k, int n) const;
    std::optional<std::tuple<int, int, long long, double>> line_at(const QPointF &pos) const;
    QString hover_text(const std::tuple<int, int, long long, double> &h) const;
Q_SIGNALS:
    void picked(const QString &unit, long long year);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    struct Drag {
        QPointF start;
        double v0, v1;
        bool moved;
    };
    std::optional<Drag> _drag;
};

// ---------------------------------------------------------------------------------------------- the lanes
using Marks = std::map<QString, std::map<long long, std::vector<Change>>>;   // unit -> year -> changes
struct LaneRow {
    QString key, name;
    bool naval;
};

class Lanes : public QWidget {
    // The lanes: a row per unit, the years from left to right, a marker where its stats change.
    Q_OBJECT
public:
    explicit Lanes(bool header = false, QWidget *parent = nullptr);
    bool header;
    std::vector<LaneRow> units;
    Marks marks;
    double y0 = 1836, y1 = 1936;
    OptStr unit;
    long long year = 1836;
    std::function<QString(const Change &)> tip;  // a change in a line (set by the page)
    QHash<QString, QString> colours;             // unit -> the colour of its line in the graph (ticked units; '' = none)
    int right_pad = 60;
    void set_data(const std::vector<LaneRow> &units, const Marks &marks, double y0, double y1);
    void set_cursor_at(const OptStr &unit, long long year);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    double x_of(double year) const;
    long long year_at(double x) const;
Q_SIGNALS:
    void picked(const QString &unit, long long year);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    std::tuple<OptStr, long long, const std::vector<Change> *> _at(const QPointF &pos) const;
};

// ---------------------------------------------------------------------------------------------- the page
// the list under "Add a change"'s technology box (C++ only, the user 2026-11-18: "how would it be easy and clear to
// search for something one thinks of but can't remember the name of"): every technology and invention in sections
// army, navy, commerce, culture, industry (coloured heads), by year in each, technologies and inventions together; the
// whole list when the box is empty (or shows what is picked), the matching ones while typing (each word: a year, a part
// of the name, a section, "invention"); Up / Down / Enter / Esc, or a click
class TechPopup : public QFrame {
public:
    static const QStringList CATS;          // the technology tab's order
    static QColor cat_colour(const QString &c);
    struct Entry {
        QString cat;
        long long year;
        QString name, key;
        bool inv;
    };
    std::vector<Entry> entries;
    QComboBox *box;
    QTreeWidget *list;
    explicit TechPopup(QComboBox *b) : QFrame(b, Qt::Tool | Qt::FramelessWindowHint), box(b)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
        setObjectName(u"techPop"_s);
        setStyleSheet(u"#techPop { background: %1; border: 2px solid %2; }"_s.arg(theme::PANEL, theme::GOLD));
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(2, 2, 2, 2);
        list = new QTreeWidget();
        list->setColumnCount(2);
        list->setHeaderHidden(true);
        list->setRootIsDecorated(false);
        list->setFocusPolicy(Qt::NoFocus);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setTextElideMode(Qt::ElideRight);
        list->header()->setStretchLastSection(true);
        list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        l->addWidget(list);
        QObject::connect(list, &QTreeWidget::itemClicked, list, [this](QTreeWidgetItem *it) { pick(it); });
        box->lineEdit()->installEventFilter(this);
        QObject::connect(box->lineEdit(), &QLineEdit::textEdited, list, [this](const QString &) { open(); });
        hide();
    }
    static QString text_of(const Entry &e)        // the combo's text: the year first, no dash
    {
        return e.inv ? u"%1  %2 (invention)"_s.arg(e.year).arg(e.name) : u"%1  %2"_s.arg(e.year).arg(e.name);
    }
    void open()
    {
        const QString t = box->lineEdit()->text().trimmed();
        const bool all = t.isEmpty() || (box->currentIndex() >= 0 && t == box->itemText(box->currentIndex()).trimmed());
        fill(all ? QString() : t);
        if (!list->topLevelItemCount()) {
            hide();
            return;
        }
        const QPoint at = box->mapToGlobal(QPoint(0, box->height()));
        int h = 420;
        if (const QScreen *sc = box->screen())
            h = std::max(160, std::min(h, sc->availableGeometry().bottom() - at.y() - 8));
        setGeometry(at.x(), at.y(), std::max(box->width(), 380), h);
        show();
        raise();
    }
    void fill(const QString &flt)
    {
        const QStringList words = flt.toLower().split(u' ', Qt::SkipEmptyParts);
        list->clear();
        QTreeWidgetItem *cur = nullptr;
        const QString cur_key = box->currentData().toString();
        QString last;
        for (const Entry &e : entries) {
            const QString hay = u"%1 %2 %3 %4 %5"_s.arg(e.year).arg(e.name, e.key, e.cat,
                                                          e.inv ? u"invention"_s : u"technology"_s).toLower();
            bool hit = true;
            for (const QString &w : words)
                hit = hit && hay.contains(w);
            if (!hit)
                continue;
            if (e.cat != last) {
                last = e.cat;
                auto *head = new QTreeWidgetItem(list);
                head->setText(0, e.cat.toUpper());
                head->setFirstColumnSpanned(true);
                head->setFlags(Qt::ItemIsEnabled);
                QFont f = list->font();
                f.setBold(true);
                head->setFont(0, f);
                QColor c = cat_colour(e.cat);
                head->setForeground(0, c);
                c.setAlpha(46);
                head->setBackground(0, c);
                head->setBackground(1, c);
            }
            auto *it = new QTreeWidgetItem(list);
            it->setText(0, QString::number(e.year));
            it->setForeground(0, cat_colour(e.cat));
            it->setText(1, e.inv ? e.name + u"  · invention"_s : e.name);
            it->setToolTip(1, u"%1 (%2, %3)"_s.arg(e.name, e.key, e.inv ? u"invention"_s : u"technology"_s));
            it->setData(0, Qt::UserRole, e.key);
            if (e.key == cur_key)
                cur = it;
        }
        if (!cur)                                   // (else the first row that can be picked)
            for (int i = 0; i < list->topLevelItemCount() && !cur; ++i)
                if (!list->topLevelItem(i)->data(0, Qt::UserRole).toString().isEmpty())
                    cur = list->topLevelItem(i);
        if (cur) {
            list->setCurrentItem(cur);
            list->scrollToItem(cur, QAbstractItemView::PositionAtCenter);
        }
    }
    void pick(QTreeWidgetItem *it)
    {
        if (!it)
            return;
        const QString k = it->data(0, Qt::UserRole).toString();
        if (k.isEmpty())
            return;                                 // (a section's head)
        const int i = box->findData(k);
        if (i >= 0) {
            box->setCurrentIndex(i);
            box->setEditText(box->itemText(i));
            box->lineEdit()->setCursorPosition(0);
        }
        hide();
    }
    void step(int d)
    {
        int i = list->indexOfTopLevelItem(list->currentItem());
        for (int n = 0; n < list->topLevelItemCount(); ++n) {
            i += d;
            if (i < 0 || i >= list->topLevelItemCount())
                return;
            if (!list->topLevelItem(i)->data(0, Qt::UserRole).toString().isEmpty()) {
                list->setCurrentItem(list->topLevelItem(i));
                list->scrollToItem(list->topLevelItem(i));
                return;
            }
        }
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::FocusIn || e->type() == QEvent::MouseButtonPress) {
            QTimer::singleShot(0, list, [this] { open(); });
        } else if (e->type() == QEvent::FocusOut) {
            QTimer::singleShot(200, list, [this] {
                if (!underMouse() && !box->lineEdit()->hasFocus())
                    hide();
            });
        } else if (e->type() == QEvent::KeyPress && isVisible()) {
            const int k = static_cast<QKeyEvent *>(e)->key();
            if (k == Qt::Key_Down || k == Qt::Key_Up) {
                step(k == Qt::Key_Down ? 1 : -1);
                return true;
            }
            if (k == Qt::Key_PageDown || k == Qt::Key_PageUp) {
                for (int n = 0; n < 12; ++n)
                    step(k == Qt::Key_PageDown ? 1 : -1);
                return true;
            }
            if (k == Qt::Key_Return || k == Qt::Key_Enter) {
                pick(list->currentItem());
                return true;
            }
            if (k == Qt::Key_Escape) {
                hide();
                return true;
            }
        }
        return QFrame::eventFilter(o, e);
    }
};

class UnitTimelineView : public QWidget {
    Q_OBJECT
public:
    using TechEdit = std::function<void(const QString &label, const QString &key, const QString &text)>;
    UnitTimelineView(moddata::Mod &mod, TechEdit on_tech_edit, QWidget *parent = nullptr);
    moddata::Mod &mod;
    TechEdit on_tech_edit;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    techdata::TechEdits *techs = nullptr;
    Units units;
    std::vector<Change> changes;
    OptStr unit;
    std::optional<long long> year;
    bool naval = false;
    QStringList ticked;                          // the units ticked in the table, in the order they were ticked
    std::map<bool, QStringList> stats;           // the graph's stats
    long long y0 = 1836, y1 = 1936;
    std::optional<long long> end_year;

    std::map<bool, QPushButton *> kind_btn;
    std::map<QString, QPushButton *> view_btn;
    QCheckBox *inv_box, *only_box;
    QPushButton *prev_btn, *next_btn, *add_btn;
    QLabel *note, *span_note, *tot_title, *ch_title, *warn;
    QWidget *stat_host, *zoom_host, *rows_host;
    widgets::WrapRow *stat_row;
    OrderedMap<QString, QPushButton *> stat_btn;
    QSplitter *split;
    Lanes *axis, *lanes;
    Graph *graph;
    QStackedWidget *stack;
    QTreeWidget *tot;
    QGridLayout *rows;
    QScrollArea *rows_scroll;
    // the list under add_tech (C++ only, the user 2026-11-18: "find something one remembers but can't name"): every
    // technology and invention in sections army, navy, commerce, culture, industry, by year in each; the whole list
    // when the box is empty, the matching ones while typing
    TechPopup *tech_pop = nullptr;
    QComboBox *add_tech, *add_target, *add_stat;   // (C++ only, 2026-11-17: the target — the unit or all land / naval
    QLineEdit *add_val;                             //  units — and every invention, not only those with an effect)

    QString tname(const QString &key) const;
    QString line(const Change &c) const;
    void refresh(document::Document *doc);
    void set_naval(bool naval);
    void set_view(const QString &view);
    void _slide(double part);
    void _axis_follows();
    void set_ticked(const QStringList &keys);
    OptStr colour_of(const QString &key) const;
    void _stat_toggled(const QString &stat, bool on);
    void _stat_clicked(const QString &stat);     // (C++ only) a click: that stat alone; Ctrl+click: add / take away
    QString _graph_key;                          // the units and stats drawn: the view fits the changes when they change
    QScrollArea *graph_scroll = nullptr;
    void _fill_stats();
    void _fill_graph(bool inv);
    void pick(const OptStr &unit, long long year);
    void jump(int step);
    QStringList _shown_units() const;
    void _split_moved(int pos, int index);
    void _show();
    void _fill_totals(const QStringList &keys, bool inv);
    void _tot_ticked(QTreeWidgetItem *it, int col);
    void _tot_picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _fill_changes(bool inv);
    void _say(const QString &text);
    OptStr _num(QLineEdit *ed);
    void set_change(const Change &c, QLineEdit *ed);
    void remove_change(const Change &c);
    void add_change();
    void prefill(const Change &c);               // (C++ only) a click on a change line: its source, target and stat
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // (not used here; the pages of
    void nameEdit(const QString &key, const QString &text);                           //  a workspace all have these)

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Sig {
        OptStr u;
        std::vector<std::tuple<QString, std::vector<qsizetype>, QString, long long>> lines;
        bool operator==(const Sig &o) const { return u == o.u && lines == o.lines; }
    };
    struct EditRow {
        QString src;
        std::vector<qsizetype> ipath;
        QLineEdit *ed;
        std::vector<QLabel *> labs;
        Change c;
    };
    std::optional<Sig> _sig;
    std::vector<EditRow> _edits;
    std::map<QString, int> _own_size;
    bool _setting = false;
    std::optional<std::pair<QString, long long>> _sized;
    std::optional<long long> _year_shown;
    const EditRow *_edit_of(const QString &src, const std::vector<qsizetype> &ipath) const;
    void _to_year(const std::vector<Change> &lst);
    void _style_row(const std::vector<QLabel *> &labs, const Change &c);
};

}  // namespace unittimeline
