// lookpages.py — Library → Menu & loading, and Library → Map look: the big pictures that give a mod its face.
//   Menu & loading   the main menu's background, the loading pictures, the title music
//   Map look         the pictures in map/terrain: land and water colours, terrain tiles, border lines, waves
// Each picture is shown as the game reads it (the mod's own, else the game's). It can be saved as a PNG to work on,
// replaced by any picture (brought to the size and the kind of file the game expects), or changed here: brightness,
// contrast, colour strength, a tint. A change is a pending edit (one undo step); Apply writes the file into the mod —
// or, in a submod, into the submod: none of these files changes the game's checksum.
#pragma once
#include "core/ordered_map.h"
#include "map/fourk.h"
#include "map/moreviews_data.h"

#include <QIcon>
#include <QImage>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QTimer;

namespace document {
class Document;
}
namespace widgets {
class Section;
}

namespace lookpages {

using Rgb = std::tuple<int, int, int>;
struct Slot {
    QString rel, title, tip;
};
extern const QStringList PICTURES;
extern const std::vector<Slot> MENU, MAP;
extern const std::pair<QString, QString> MUSIC;

// The picture changed: bright / contrast / colour −100 … 100, tint (r, g, b) mixed in by strength 0 … 100. Opacity is
// left as it is. (numpy's float32 arithmetic, its matmul's sum order included: see CLAUDE.md.)
fourk::Pic adjust(const fourk::Pic &rgba, int bright = 0, int contrast = 0, int colour = 0,
                  const std::optional<Rgb> &tint = std::nullopt, int strength = 0);
QImage fitted(const QImage &img, int w, int h, bool crop);   // brought to w × h: cut to the shape first, or stretched

class LookPage : public QWidget {
    Q_OBJECT
public:
    // add(label, {rel: (how, what)}): the window's pending-file step (Python: on_asset_edit)
    using Add = std::function<void(const QString &, const OrderedMap<QString, moreviews::Asset> &)>;
    LookPage(moddata::Mod &mod, Add add, const QString &title, const QString &intro, bool crop,
             QWidget *parent = nullptr);
    moddata::Mod &mod;
    Add add;
    document::Document *doc = nullptr;
    const bool CROP;                                 // a replacement is cut to the shape (False: stretched)
    std::optional<QString> rel;
    std::optional<fourk::Pic> _src, _work, _base;
    std::optional<fourk::Info> _info;
    std::optional<std::pair<QStringList, QStringList>> _sig;
    std::optional<Rgb> _tint;
    std::map<std::pair<QString, const void *>, std::optional<QIcon>> _thumb;   // (rel, the pending picture) -> icon

    QListWidget *lst;
    QLabel *head, *info, *view, *note;
    QPushButton *replace_btn, *export_btn, *use_btn, *reset_btn;
    widgets::Section *extra;
    OrderedMap<QString, QSlider *> sliders;
    std::vector<QPushButton *> tint_btns;
    QTimer *_tick;

    QLabel *_bold(const QString &text);
    virtual std::vector<Slot> slots();
    moreviews::AssetEdits *_assets() const;
    std::optional<QString> _file(const QString &rel) const;   // the file the game reads for a slot, or None
    const moreviews::PicData *_pending(const QString &rel) const;
    void refresh(document::Document *doc);
    void _thumbs();
    std::pair<fourk::Pic, fourk::Info> _read(const QString &rel) const;
    void _picked(QListWidgetItem *cur);
    virtual void fill_extra(const QString &rel);
    struct Values {
        int bright, contrast, colour;
        std::optional<Rgb> tint;
        int strength;
    };
    Values _values() const;
    void _show();
    void resizeEvent(QResizeEvent *e) override;
    void _set_tint(const Rgb &rgb, QPushButton *btn);
    void _reset(bool show = true);
    void queue(const QString &what, const fourk::Pic &rgba);
    void _use();
    void _replace();
    void take_file(const QString &path);
    void _export();

Q_SIGNALS:
    void scriptEdit(const QString &a, const QString &b, const QString &c);   // (the workspace wires these for every
    void nameEdit(const QString &a, const QString &b);                       //  page; not used here)
};

class MenuLookPage : public LookPage {
    Q_OBJECT
public:
    MenuLookPage(moddata::Mod &mod, Add add, QWidget *parent = nullptr);
    QLabel *music_lbl;
    std::vector<Slot> slots() override;
    void fill_extra(const QString &rel) override;
    void _music();
    void take_music(const QString &path);
};

class MapLookPage : public LookPage {
    Q_OBJECT
public:
    MapLookPage(moddata::Mod &mod, Add add, QWidget *parent = nullptr);
    std::vector<Slot> slots() override;
};

}  // namespace lookpages
