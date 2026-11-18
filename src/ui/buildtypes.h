// buildtypes — C++ only (the user, 2026-11-16): Building types — since 2026-11-17 a page of Game rules, between
// National focuses and Rebels (the user: "a listing ... of different types", like values and focuses; before: a sub-tab
// of Provinces & states). A province's level of a building is set on Provinces & states → Provinces. The province buildings of
// common/buildings.txt (the game's fort, naval base and railroad, and the mod's own): make a new one, its name, levels,
// build time, goods, effects, the technologies that unlock its levels; its row in the game's province window (the
// picture slot build_iconN — without it the game crashes when a province window opens, measured — the picture strip,
// room for the extra rows); the picked provinces' level of it. Every change is one undoable step, written on Apply.
// The text work is core/provbuild.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "map/moreviews_data.h"

#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;
class QComboBox;
namespace widgets {
class Section;
class Stepper;
}
namespace dialogs {
class PickCombo;
}
namespace history {
struct ProvState;
}

namespace buildtypes {

using OptStr = std::optional<QString>;
using LevelFn = std::function<void(long long, history::ProvState &)>;
extern const QString BUILDINGS, GFX;                // common/buildings.txt, interface/modeditor_buildings.gfx

struct WindowFix {                                  // what the province window needs: {rel: text}, {rel: picture}
    OrderedMap<QString, QString> texts;
    OrderedMap<QString, moreviews::Asset> files;
};
// the picture slots, strips and room the province window needs for every province building of the editor's texts
WindowFix window_fix(moddata::Mod &mod, document::Document &doc);
// ...on given texts: `buildings` the buildings.txt text to work for, names {key: name} for new ones; old_keys the
// province buildings' order before this step (a move, a removal: each slot shows its building's picture again; empty:
// the same order); pref a new row height wanted (none: the one noted in the file). The window is fitted to the rows
// (grows, shrinks, lower rows on a small screen), its background drawn again from the original picture.
void window_work(moddata::Mod &mod, document::Document *doc, const QString &buildings, QString &gui, QString &gfx,
                 OrderedMap<QString, moreviews::Asset> &files, const OrderedMap<QString, QString> &names,
                 const QStringList &old_keys = {}, std::optional<int> pref = std::nullopt);
QImage strip_for(const moddata::Mod &mod, document::Document *doc, const QString &key, const std::optional<QImage> &own,
                 const QString &buildings, const QString &name);

// The game's province window as the game draws it (from the mod's files, the editor's edits not yet written
// included), its building rows laid over it where the game puts them. A click picks a building; a row dragged up or
// down moves the building (and its row) to that place.
class WindowView : public QWidget {
    Q_OBJECT
public:
    explicit WindowView(QWidget *parent = nullptr);
    QImage img;
    struct Row {
        QString key;
        QRectF r;                                   // in the picture's coordinates
    };
    std::vector<Row> rows;
    QString sel;
    int drag = -1, drop = -1;                       // the row being dragged, the place it would go
    QPointF press;
    void set(const QImage &img, const std::vector<Row> &rows, const QString &sel);
    double scale() const;
    int row_at(QPointF pos) const;                  // a row under a point of the widget, or -1
    int drop_at(QPointF pos) const;                 // the place a row dropped here goes to
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
Q_SIGNALS:
    void picked(const QString &key);
    void moved(const QString &key, int to);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
};

class BuildTypesPanel : public QWidget {
    Q_OBJECT
public:
    explicit BuildTypesPanel(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    QList<long long> pids;                          // (unused since the page left Provinces & states)
    OptStr cur;                                     // the building on show
    void refresh(document::Document &doc, const QList<long long> &pids);

    QStringList keys();                             // the province buildings (buildings.txt order)
    QString text(const QString &rel);
    std::vector<std::pair<QString, QString>> tech_files();   // [(rel, text)] of technologies/
    QString gui_rel() const;
    long long holders(const QString &key);          // provinces with a level of it
    // what the province window lacks for the province buildings (each: building, words); empty = nothing
    std::vector<std::pair<QString, QString>> window_problems();
    // the picture slots, the strips and the room the province window needs for every province building, as one step
    void fix_window();
    // fix_window's work on given texts: (gui, gfx, pictures), for a new building made in the same step
    void _window_work(QString &gui, QString &gfx, OrderedMap<QString, moreviews::Asset> &files, const QStringList &ks);

Q_SIGNALS:
    // texts, pictures and names (localisation keys: only written where they have no text yet) as one step
    void bundleEdit(const QString &label, const OrderedMap<QString, QString> &texts,
                    const OrderedMap<QString, moreviews::Asset> &files, const OrderedMap<QString, QString> &names);
    void nameEdit(const QString &key, const QString &text);

public:   // (open to the checks, as the other panels)
    QLabel *intro, *count, *title, *sub, *slot_note, *room_note, *pic_note, *frame_note, *tech_note, *fx_note,
        *del_note, *new_warn, *prov_note;
    QLineEdit *new_name, *name_edit;
    QTreeWidget *tree;
    widgets::Section *detail;
    widgets::Stepper *max_level, *time;
    QWidget *cost_host, *fx_host, *tech_host;
    QVBoxLayout *cost_lay, *fx_lay, *tech_lay;
    dialogs::PickCombo *add_good, *add_fx, *add_tech;
    QPushButton *slot_btn, *del_btn, *pic_btn;
    QComboBox *lib;
    // the strip's pictures, one per level (the user 2026-11-17: a strip of 6 or of 100 — every picture shown, any one
    // picked, replaced, one put in or taken out, its size said)
    QWidget *frames_host;
    QLayout *frames_lay;                            // (a widgets::WrapRow)
    QPushButton *rep_btn, *cut_btn;
    // (C++ only, the user 2026-11-18) the size of a level's picture, big; a picture file of the computer for the
    // picked level; ＋ after the last picture = a new level (a placeholder picture, the max level one higher);
    // pictures dragged to another place; the pictures into one's library; when the strip has more pictures than
    // levels: the max level raised on a click
    QLabel *size_lbl;
    QPushButton *imp_rep_btn, *lv_btn, *lib_one_btn, *lib_all_btn;
    void _frame_import(int how);
    void _add_level();
    void _delete_level(int i);                      // its picture out, the highest level one lower (one step)
    QLabel *lvl_err;                                // pictures / highest level / technologies' levels don't match
    void _frame_move(int from, int to);
    void _to_library(bool all);
    void _fill_lib();
    void _emit_strip(const QString &label, OrderedMap<QString, QString> texts,
                     OrderedMap<QString, moreviews::Asset> files);
    int _drag_from = -1, _new_level = -1;
    QPoint _drag_at;
    std::pair<int, int> _drop{-1, -1};
    bool eventFilter(QObject *o, QEvent *e) override;
    QString frame_sprite, _frame_key;               // the strip shown; the building it was shown for
    int frame_sel = 0;                              // the picture picked (0 = level 0)
    int frames_n = 0;
    void _fill_frames(const QString &key, long long max_level);
    void _frame_edit(int how, const std::optional<QImage> &given = std::nullopt, bool new_level = false);                      // 0: replace the picked one, 1: one more after it, 2: take it out
    WindowView *win_view;                           // the province window
    QLabel *win_note;
    widgets::Stepper *row_h;                        // the row height wanted
    QString _sig;
    bool _filling = false;

    void _fill_window();                            // the province window drawn, its notes
    void _move(const QString &key, int to);         // a building (and its row) to another place
    void _row_height(int px);                       // the rows' height wanted: the window fitted to it
    // buildings.txt as `buildings` and the province window fitted to it, as one undoable step
    void _bundle(const QString &label, const QString &buildings, const QStringList &old_keys, std::optional<int> pref);

    void _fill_tree();
    void _fill_detail();
    void _picked();
    void _edit(const QString &label, const QString &rel, const QString &text);
    void _set(const QString &label, const QString &field, const QString &value);
    void _make_new();
    void _remove();
    void _tech(const QString &tech, int how);       // +1: one level more, 0: off
    QString _name(const QString &key);
    QImage _strip(const QString &key, const std::optional<QImage> &own);   // a strip for a building
    void _own_picture();
};

}  // namespace buildtypes
