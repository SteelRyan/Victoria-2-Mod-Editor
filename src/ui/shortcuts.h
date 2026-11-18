// shortcuts.py — Rules → Shortcuts: the keyboard shortcuts of the game's own buttons.
// Every button and tick box of the game's interface (interface/*.gui, the mod's file where it has one, else the
// game's) can carry `shortcut = "x"`. This page lists them all — by window, by key or by what they say — and sets,
// changes or removes the shortcut of the one picked. A change is a pending edit of that .gui file like any other
// script edit (one undo step; Apply writes the file into the mod, with a backup).
// The window projection (project, turn_groups, versions_of) also draws the Politics page's "As the game's window".
#pragma once
#include "core/shortcutfacts.h"
#include "ui/locwin.h"

#include <QImage>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
namespace document { class Document; }
namespace scriptstore { class ScriptStore; }
namespace widgets { class WrapRow; }

namespace shortcuts {

using OptStr = std::optional<QString>;
using Ident = std::tuple<QString, QStringList, QString, long long>;   // (rel, path, name, nth) of a button

extern const QStringList KINDS, WINDOW_KINDS, ELEMENTS, NAMED;
extern const std::pair<int, int> SCREEN;            // the game's screen the windows are shown on (not 4K on purpose)
std::pair<double, double> anchor(const QString &orientation);   // ANCHOR.get(o, (0, 0))
extern const OrderedMap<QString, QString> SCREENS;  // top-bar tab -> the file of its screen

OptStr key_name(int code, const QString &typed = {});   // the game's name for a pressed key, or None
bool known(const QString &key);                     // a key name the game's or a mod's files use?
QString tidy(const QString &key);                   // a key as it is written into the file

struct Button {
    QString rel, name, kind;
    QStringList path;
    long long nth = 0;
    QString key;
    bool has = false;
    QString says, tip;
    Ident ident() const { return {rel, path, name, nth}; }
    QString window() const { return path.isEmpty() ? QStringLiteral("(not in a window)") : path[0]; }
};
const shortcutfacts::Fact *fact_of(const Button &b);   // what the test in the game found, or None: not tested

OptStr _val(const script::Node &node, const QString &name);
std::vector<Button> scan(const QString &rel, const QString &text);
std::optional<QString> set_shortcut(const QString &text, const Ident &ident, const QString &key);
QStringList gui_files(const moddata::Mod &mod);
std::vector<Button> all_buttons(const moddata::Mod &mod, scriptstore::ScriptStore &store);
std::map<Ident, std::vector<size_t>> clashes(const std::vector<Button> &buttons);   // -> places in `buttons`

struct Hit {
    double x, y, w, h;
    Ident ident;
};
std::vector<QStringList> turn_groups(const script::Node &win);
std::vector<std::vector<Ident>> versions_of(const std::vector<Hit> &hits, const std::set<Ident> &skip = {});

struct Meta {
    double x0 = 0, y0 = 0;
    locwin::XY position, size;
    std::vector<std::vector<Ident>> versions;
    QString orientation;
    bool fixed = false;
};
struct Projected {
    std::optional<QImage> img;
    std::vector<Hit> hits;
    std::vector<QStringList> groups;
    std::optional<Meta> meta;                       // (with_meta)
};
// only_part (C++ only, 2026-11-15): when given, of the window's own sub-windows only the one of that name is drawn
// (the reforms' page without the movements / decisions / release nations pages lying on it)
// skip_parts (C++ only, 2026-11-16): sub-windows of the window left out (the province window without the focus,
// colony and foreign-province parts that lie over it); 2026-11-18 also single parts of the window itself (the 4K
// screen's unit panel without what the game hides when one army is picked)
Projected project(locwin::GameLook &look, const QString &rel, const QString &window,
                  const std::set<QString> &chosen = {}, bool with_meta = false, bool flat = false, long long version = 0,
                  const QString &only_part = {}, const std::set<QString> &skip_parts = {});

struct Box {
    double x, y, w, h;
};
struct OnScreen {
    QImage img;
    std::vector<Hit> hits;
    std::vector<QStringList> groups;
    std::optional<Box> box;
    std::vector<std::vector<Ident>> versions;
};
// the always-shown bars, drawn once per file revision (Python keeps them on the GameLook as look._always)
using Layers = std::map<std::tuple<QString, QString, long long>, Projected>;
OnScreen project_screen(locwin::GameLook &look, Layers &layers, const QString &rel, const QString &window,
                        const std::set<QString> &chosen = {}, std::pair<int, int> screen = SCREEN, long long version = 0);

class WindowView : public QWidget {                 // the window picked, as the game lays it out
    Q_OBJECT
public:
    explicit WindowView(QWidget *parent = nullptr);
    std::optional<QImage> img;
    std::vector<Hit> hits;
    std::map<Ident, QString> keys;
    std::optional<Ident> pick;
    std::map<Ident, std::pair<QString, QString>> info;   // ident -> (class, what it does)
    QString banner;
    std::set<QString> classes = {QStringLiteral("good")};
    double zoom = 1.0;
    std::pair<double, double> off = {0.0, 0.0};
    std::optional<Box> box;
    void show_window(const std::optional<QImage> &img, const std::vector<Hit> &hits, const std::map<Ident, QString> &keys,
                     const std::optional<Ident> &pick, const std::map<Ident, std::pair<QString, QString>> &info = {},
                     const QString &banner = {});
    void set_classes(const std::set<QString> &classes);
    void fit();
    void zoom_to(const std::optional<Box> &box);
    bool shown(const Ident &ident) const;
    std::tuple<double, double, double> _fit() const;
    std::optional<Ident> at(const QPointF &pos) const;
Q_SIGNALS:
    void picked(const shortcuts::Ident &ident);
    void opened(const shortcuts::Ident &ident);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    struct Drag {
        QPointF start;
        std::pair<double, double> off;
        bool moved;
    };
    std::optional<Drag> _drag;
};

class KeyCatcher : public QPushButton {             // "Press a key…": while it is down, the next key is the shortcut
    Q_OBJECT
public:
    explicit KeyCatcher(QWidget *parent = nullptr);
Q_SIGNALS:
    void caught(const QString &name);

protected:
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void _toggled(bool on);
};

class ShortcutsWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutsWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    ~ShortcutsWorkspace() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::optional<std::vector<std::pair<QString, long long>>> _sig;
    std::vector<Button> _buttons;
    std::map<Ident, std::vector<size_t>> _clash;
    std::optional<Ident> _pick;
    std::map<Ident, std::tuple<QString, QString, QString>> _use;   // ident -> (class, why, what it does)
    QString mode = QStringLiteral("window"), cat = QStringLiteral("good");
    bool only = false;
    QLabel *count_lbl, *proj_head, *what, *fact, *now, *note;
    QLineEdit *search, *typed;
    QButtonGroup *sort_group, *cat_group;
    std::map<QString, QPushButton *> cat_btn;
    QPushButton *only_btn, *grey_btn, *near_btn, *set_btn, *none_btn;
    QTreeWidget *tree;
    QWidget *part_host;
    widgets::WrapRow *part_row;
    std::map<std::pair<QString, QString>, std::set<QString>> _parts;   // (file, window) -> the parts picked to show
    std::map<std::pair<QString, QString>, long long> _version;      // (file, window) -> which of its versions is shown
    std::vector<QPushButton *> part_btn, ver_btn;
    WindowView *proj;
    KeyCatcher *catch_;
    struct Shown {
        QString rel, window;
        long long rev;
        QImage img;
        std::vector<Hit> hits;
        std::vector<QStringList> groups;
        QStringList chosen;                          // tuple(sorted(chosen))
        std::optional<Box> box;
        std::vector<std::vector<Ident>> versions;
        long long vers;
    };
    std::optional<Shown> _shown;

    scriptstore::ScriptStore *_store() const;
    void refresh(document::Document *doc);
    void _set_mode(const QString &m);
    void _set_only(bool on);
    void _set_cat(const QString &c);
    QString says(const Button &b) const;
    void _fill();
    const Button *_button(const std::optional<Ident> &ident) const;
    void _picked(QTreeWidgetItem *cur);
    void _show_window(const QString &rel, const OptStr &window, bool force = false);
    void _frames();
    void _fill_parts(const std::vector<QStringList> &groups, const std::set<QString> &chosen,
                     const std::vector<std::vector<Ident>> &versions = {}, long long vers = 0);
    void _set_version(long long k);
    void _set_part(int gi, const QString &name);
    void _pick_ident(const Ident &ident);
    void _open_screen(const Ident &ident);
    void _show_pick();
    void _set(const QString &key);
    std::optional<Ident> ident_of(QTreeWidgetItem *it) const;   // it.data(0, UserRole)
Q_SIGNALS:
    void edit(const QString &label, const QString &rel, const QString &text);
    void message(const QString &text);

private:
    QLabel *_bold(const QString &text);
    std::unique_ptr<locwin::GameLook> _look;
    Layers _layers;
    std::vector<Ident> _row_ident;                   // (the rows' idents: UserRole holds a place here)
    std::vector<std::pair<QString, QString>> _head_of;   // (the window rows': UserRole + 1 holds a place here)
    std::vector<std::pair<int, QString>> _pwant;     // (the part buttons' "part" properties)
    std::optional<std::vector<QStringList>> _vwant;
};

}  // namespace shortcuts
