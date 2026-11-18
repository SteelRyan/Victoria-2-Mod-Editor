// palette.py — The command palette (Ctrl+K): type a few letters, pick a function of the editor, Enter — the editor
// goes there and frames the place in gold for a moment. Every page, every part of a page, the tools' windows, the map
// modes of the page that is open, Apply / Undo / Redo. (The list of functions is the main window's:
// MainWindow::_palette_commands.)
#pragma once
#include <QFrame>
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QStyledItemDelegate>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSettings;
class QTimer;

namespace colorwheel { class ColorButton; }

namespace palette {

// what a function belongs to — shown in its colour beside every result: (key, name, default colour or "" for none,
// words of the place that put a function there — the first category whose word fits wins)
struct Category {
    QString key, name, colour;
    QStringList words;
};
extern const std::vector<Category> CATEGORIES;
QString cat_name(const QString &key);
QString category(const QString &title, const QString &where);

extern const std::vector<QStringList> SAME;   // words that mean the same when looking for a function
extern const QStringList ITEMS;               // entries that name ONE thing of a long list ("Technology: Aeronautics")
QStringList forms(const QString &w);
QStringList same_words(const QString &w);

class CommandPalette;

class _CatDelegate : public QStyledItemDelegate {
public:
    explicit _CatDelegate(CommandPalette *pal);
    CommandPalette *pal;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
};

class Flash : public QWidget {                // a gold frame over a widget for a moment: "here it is"
    Q_OBJECT
public:
    explicit Flash(QWidget *target);
    int _n = 0;
    QTimer *_t;
    void _step();
protected:
    void paintEvent(QPaintEvent *e) override;
};

class _Handle : public QWidget {              // "move" (the grip bar at its top) or "size" (its bottom-right corner)
public:
    _Handle(CommandPalette *pal, const QString &kind);
    CommandPalette *pal;
    QString kind;
    std::optional<std::pair<QPoint, QRect>> _start;
protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
};

// a function of the palette: (what it is called, where it is, a function doing it -> the widget to frame or none)
struct Command {
    QString title, where;
    std::function<QWidget *()> fn;
};

class CommandPalette : public QFrame {
    Q_OBJECT
public:
    CommandPalette(QWidget *win, QSettings *settings, std::function<std::vector<Command>()> commands);
    QWidget *win;
    QSettings *settings_;
    std::function<std::vector<Command>()> commands;
    _Handle *mover, *grip;
    QLineEdit *edit;
    QPushButton *close_btn, *col_btn;
    QListWidget *lst;
    std::vector<std::pair<QString, QString>> colours;     // {category: colour} in CATEGORIES' order
    QWidget *col_panel;
    std::map<QString, std::pair<colorwheel::ColorButton *, QCheckBox *>> _col_btns;
    QLabel *count;
    std::vector<Command> _all;

    QString colour_of(const QString &key) const;
    QSettings *_settings() const { return settings_; }
    std::vector<std::pair<QString, QString>> _load_colours();
    void _set_colour(const QString &key, const QString &col);
    void _show_colours(bool on);
    void _place_grip();
    void _save_geometry();
    std::optional<QRect> _saved_geometry() const;
    void open();
    void _hide_if_left();
    // How well a command fits what was typed (smaller = better), or none: every word must be in it.
    static std::optional<std::pair<long long, long long>> rank(QStringList words, const QString &title,
                                                               const QString &where);
    void _fill(const QString &text);
    void _run(QListWidgetItem *it);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
};

}  // namespace palette
