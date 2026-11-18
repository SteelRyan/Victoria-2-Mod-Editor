// iconboard.py — National focuses and national values as a board of their pictures, the way the game shows them:
// Game rules → National focuses, Game rules → National values. A row per group (the national values are one row), a
// picture per entry with its name under it; click one: its name, picture and what it does on the right; drag it to
// another place; ＋ at a row's end: a new one. A national value's effects in the pop files (modifier = { factor …
// nationalvalue = … }) are listed with it and changed from here.
// Ported: the helpers, Board, WindowPreview, BoardPage, BoardWithList. Not yet: strip_entries and AllBoardsPage
// (Library → Picture boards).
#pragma once
#include "core/ordered_map.h"
#include "core/script.h"
#include "map/focuswindow.h"
#include "map/iconstrips_data.h"

#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
namespace document { class Document; }
namespace locwin { class GameLook; }
namespace moddata { class Mod; }
namespace scriptstore { class ScriptStore; }
namespace widgets { class Section; class WrapRow; }

namespace iconboard {

using OptStr = std::optional<QString>;
using OptInt = std::optional<long long>;
using script::Node;
using Span = std::pair<qsizetype, qsizetype>;

extern const int CELL_W, CELL_H, HEAD_H, GAP;
struct Kind {
    QString rel, sprite;
    bool grouped;
    QString noun, title, intro;
};
extern const OrderedMap<QString, Kind> KINDS;
extern const OrderedMap<QString, QString> PLACE;   // where in a pop file a "modifier" can stand, in plain words

QString _pretty(const QString &k);
Span _line_span(const QString &text, const Node &n);   // a node with its whole lines
struct Group {                                         // (group key or "", group node or None, [entry nodes])
    QString key;
    std::optional<Node> node;
    std::vector<Node> entries;
};
std::vector<Group> read_entries(const QString &text, bool grouped);
QString move_entry(const QString &text, bool grouped, const QString &key, const QString &group, const QString &before);
struct NvUse {
    QString rel;
    QStringList path;                                  // the blocks the modifier stands in
    QString factor;
    std::optional<Span> fspan;                         // where the factor's number is
    Span span;                                         // the modifier's lines
    QString also;                                      // its other conditions
    bool neg = false;
};
std::vector<NvUse> nv_uses(scriptstore::ScriptStore &store, const QStringList &files, const QString &nv);
std::vector<std::pair<QString, QStringList>> pop_places(scriptstore::ScriptStore &store, const QStringList &files);
QString place_words(const QString &rel, const QStringList &path,
                    const std::function<QString(const QString &)> &name = _pretty);

// ---------------------------------------------------------------------------------------------- the board
struct Cell {                                          // (key, name, pixmap or None, note)
    QString key, name;
    std::optional<QPixmap> pm;
    QString note;
};
struct BRow {                                          // (group key, title, [cells])
    QString key;
    OptStr title;
    std::vector<Cell> cells;
};

class Board : public QWidget {
    Q_OBJECT
public:
    explicit Board(QWidget *parent = nullptr);
    std::vector<BRow> rows;
    OptStr sel;
    QSet<QString> multi;
    bool head_x = false;                               // rows' bars carry a ✕
    bool plus = true;
    void set_rows(const std::vector<BRow> &rows, const OptStr &sel, const QSet<QString> &multi = {});
    QRectF _x_of(const QRectF &r) const;
    void _layout();
    QSize sizeHint() const override;
    struct Hit {
        QString what, gk, key;
        QRectF r;
    };
    std::optional<Hit> _at(const QPointF &pos) const;
    std::vector<std::tuple<QRectF, QString, Cell>> _boxes;
    std::vector<std::tuple<QRectF, QString, QString>> _heads;
    std::vector<std::pair<QRectF, QString>> _plus;
    struct Drag {
        QString key;
        QPointF start;
        bool moving;
    };
    std::optional<Drag> _drag;
    struct Drop {
        QString gk, before;
        double x, y0, y1;
    };
    std::optional<Drop> _drop;

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
Q_SIGNALS:
    void picked(const QString &key);                   // an entry's key ("@group": a row's head)
    void moved(const QString &key, const QString &group, const QString &before);
    void addRequested(const QString &group);
    void ctrlPicked(const QString &key);
    void deleteRequested();
    void headDelete(const QString &group);
    void rightDelete(const QString &key);               // (C++ only, the user 2026-11-18: right click = delete)
};

class WindowPreview : public QWidget {
    // The game's own window as a picture (never larger than it is in the game); a click on an icon or a row's name
    // picks it.
    Q_OBJECT
public:
    explicit WindowPreview(QWidget *parent = nullptr);
    std::optional<QImage> img;
    std::vector<focuswindow::Hit> hits;
    double scale = 1.0;
    void set(const std::optional<QImage> &img, const std::vector<focuswindow::Hit> &hits);
    void _fit();
    QSize sizeHint() const override;
    OptStr at(const QPointF &pos) const;

protected:
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
Q_SIGNALS:
    void picked(const QString &key);
};

// fn(label, {picture file: ("strip", image)}, {text file: text})
using StripChange = std::function<void(const QString &, const iconstrips::StripFiles &, const OrderedMap<QString, QString> &)>;

// ---------------------------------------------------------------------------------------------- the page
class BoardPage : public QWidget {
    Q_OBJECT
public:
    BoardPage(moddata::Mod &mod, const QString &kind, StripChange strip_change = {}, QWidget *parent = nullptr);
    ~BoardPage() override;
    moddata::Mod &mod;
    QString kind;
    const Kind &K;
    StripChange strip_change;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    iconstrips::Strips *strips = nullptr;
    OptStr sel;
    QString fresh;                                     // the entry made last on this page: its picture reminder shows
    QSet<QString> multi;                               // several picked with Ctrl (entries, and rows as "@key")
    bool _can_fit = false;                             // the game's focus window does not fit: a fit would change it
    QString pv_mode = QStringLiteral("now");
    std::unique_ptr<focuswindow::Look> _look;
    locwin::GameLook *_game_look = nullptr;            // (inside _look)
    std::set<std::pair<QString, QString>> _open;       // (national value, pop file): its lines are unfolded
    QLabel *warn, *win_note = nullptr, *pv_note = nullptr;
    Board *board;
    QPushButton *win_btn = nullptr;
    WindowPreview *pv = nullptr;
    std::vector<std::pair<QString, QPushButton *>> pv_btn;
    QWidget *detail;
    QVBoxLayout *dl;
    QLineEdit *name_edit = nullptr, *add_val = nullptr, *key_edit = nullptr, *h_factor = nullptr;
    QComboBox *lib = nullptr, *add_key = nullptr, *h_place = nullptr;
    QLabel *lib_msg = nullptr, *msg = nullptr, *h_msg = nullptr;
    QPushButton *del_btn = nullptr, *row_del = nullptr, *multi_del = nullptr;

    QString text();
    std::vector<Group> rows();
    OptStr loc(const QString &key) const;
    QString name(const QString &key) const;
    std::optional<std::tuple<QString, long long, Node>> node(const QString &key);
    OptInt icon_of(const QString &gk, long long i, const Node &n) const;
    QStringList pop_files();
    void refresh(document::Document *doc);
    void _show();
    void _clear();
    QLabel *_bold(const QString &text);
    void _fill_detail();
    void _fill_group(widgets::Section *s, const QString &gk);
    void _fill_multi(widgets::Section *s);
    void _fill_hidden(const QString &key);
    void _toggle(const std::pair<QString, QString> &k);
    QStringList _suggest();
    void _emit(const QString &what, const QString &nw);
    void _pick(const OptStr &key);
    void _ctrl(const QString &key);
    void _delete_picked();
    void _delete_many(const QSet<QString> &keys);
    void _rename(const QString &key);
    void _rekey(const QString &key);
    void _add_line(const QString &key, const QStringList &path);
    QStringList _order();
    std::pair<iconstrips::StripFiles, OrderedMap<QString, QString>> _strip_reorder(const QStringList &old_order,
                                                                                   const QStringList &new_order);
    void _commit(const QString &what, const QString &new_text, const std::optional<QStringList> &old_order = std::nullopt);
    void _move(const QString &key, const QString &group, const QString &before);
    void _step(const QString &key, int d);
    void _delete(const QString &key);
    QString _free_key(const QString &stem);
    void _add(const QString &group);
    struct Win {                                       // (the .gui text, the window's picture, where it is, rows, most)
        QString text;
        QImage img;
        QString rel;
        long long rows, most;
    };
    std::optional<Win> _window();
    void _window_note();
    void _set_pv(const QString &k);
    void _preview();
    void _fit_window();
    void _add_group();
    void _del_group(const QString &gk);
    void _library(const QString &key, const QStringList &path, OptInt num, bool as_new);
    // (C++ only, 2026-11-18) a picture file picked on the computer: into the strip on Apply
    void _import(const QString &key, const QStringList &path, OptInt num, bool as_new);
    bool _use_image(const QString &key, const QStringList &path, OptInt num, bool as_new, const QImage &img,
                    const QString &file);
    QLabel *size_lbl = nullptr;
    QPushButton *import_btn = nullptr, *lib_one_btn = nullptr, *lib_all_btn = nullptr;
    // another focus shows the same picture of the strip (then this one gets a picture of its own)
    bool _shared(const QString &key, OptInt num);
    void _to_library(const QString &key, OptInt num, bool all);
    void _hidden_factor(const QString &key, size_t n, QLineEdit *ed);
    void _hidden_remove(const QString &key, size_t n);
    void _hidden_add(const QString &key);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
};

class BoardWithList : public QWidget {
    // The board, and the list page this file had before it (every field, the conditions) behind a switch.
    Q_OBJECT
public:
    BoardWithList(moddata::Mod &mod, const QString &kind, QWidget *old, std::function<void(document::Document *)> old_refresh,
                  StripChange strip_change = {}, QWidget *parent = nullptr);
    document::Document *doc = nullptr;
    BoardPage *board;
    QWidget *old;
    std::function<void(document::Document *)> old_refresh;
    std::vector<QPushButton *> btns;
    QStackedWidget *stack;
    void set_mode(int n);
    void refresh(document::Document *doc);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void fileRemove(const QString &label, const QString &rel);
    void bigRequested(const QString &rel, const QStringList &path, const QString &noun);
};

// ---------------------------------------------------------------------------------------------- every strip's users
struct StripEntry {                                    // (row title or "", key, picture number or None, rel, path, by order)
    QString row, key;
    OptInt num;
    QString rel;
    QStringList path;                                  // the blocks down to the entry (empty: the file itself is the entry)
    bool by_order = false;
};
std::vector<StripEntry> strip_entries(const moddata::Mod &mod, scriptstore::ScriptStore *store, const iconstrips::Use &use);

class AllBoardsPage : public QWidget {
    // Library → Picture boards.
    Q_OBJECT
public:
    AllBoardsPage(moddata::Mod &mod, StripChange strip_change = {}, QWidget *parent = nullptr);
    moddata::Mod &mod;
    StripChange strip_change;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    iconstrips::Strips *strips = nullptr;
    const iconstrips::Use *use;
    OptStr sel;
    std::vector<StripEntry> entries;
    widgets::WrapRow *chips;
    OrderedMap<QString, QPushButton *> chip;
    QLabel *warn, *msg = nullptr;
    Board *board;
    QWidget *detail;
    QVBoxLayout *dl;
    QLineEdit *name_edit = nullptr;
    QComboBox *lib = nullptr;

    QString name(const QString &key) const;
    void set_use(const QString &uid);
    void refresh(document::Document *doc);
    void _show();
    void _pick(const QString &key);
    void _clear();
    void _fill();
    long long _pos(const QString &rel, const QStringList &path);
    void _rename(const QString &key);
    QString _with_num(const QString &rel, const QStringList &path, long long n);
    void _set_num(const QString &key, const QString &rel, const QStringList &path, long long n);
    void _library(const QString &key, const QString &rel, const QStringList &path, OptInt num, bool as_new);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void placeRequested(const QString &rel, long long pos);   // a file and a place in it: the page it is edited on
};

}  // namespace iconboard
