// drawpanel.py — Map tab → Draw: paint provinces.bmp (brush / fill), pick provinces, clean stray pixels — and, as
// further layers, terrain.bmp (terrainpanel) and rivers.bmp (riverpanel). Every stroke is one undo step; nothing is
// written before Apply.
// (Python keeps the Mismatch on doc.mismatch and reads doc.landsea; here the panel owns the Mismatch — made again
// when the document or the land / sea link changes — and the window sets `landsea`, `geo` and `pos_panel`.)
#pragma once
#include "core/ordered_map.h"
#include "map/newprov.h"
#include "ui/drawlink.h"
#include "ui/mapview.h"
#include "ui/provclip.h"

#include <QList>
#include <QSet>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;
class QVBoxLayout;

namespace adjacency {
class AdjGeometry;
}
namespace colormappanel {
class ColormapBox;
}
namespace document {
class Document;
}
namespace layerbmp {
class LandSea;
}
namespace mismatch {
class Mismatch;
}
namespace mismatchbox {
class MismatchBox;
}
namespace moddata {
class Mod;
}
namespace newprov {
class NewProvinceDialog;
}
namespace positions {
struct Entry;
}
namespace pospanel {
class PositionsPanel;
}
namespace provbmp {
class ProvinceBitmap;
}
namespace riverpanel {
class RiverBox;
}
namespace terrainpanel {
class TerrainBox;
}
namespace widgets {
class Section;
class SliderField;
}

namespace drawpanel {

using Flat = std::vector<std::int64_t>;
using Ids = std::vector<std::uint16_t>;
extern const std::map<QString, QString> LAYER_HELP;
extern const QString DELETE_TEXT;
extern const std::vector<std::pair<QString, QString>> PAINT_OVER;
constexpr long long STRAY_LIMIT = 20;

class DrawPanel;

// Mouse tool of the Draw mode (installed as MapView.mouse_handler while "Draw on the map" is on).
class DrawHandler : public mapview::MouseHandler {
public:
    explicit DrawHandler(DrawPanel *panel) : p(panel) {}
    DrawPanel *p;
    std::optional<OrderedMap<long long, long long>> touched;   // {flat index: original id} during a stroke
    std::optional<std::pair<double, double>> last, last_pos;
    mapview::ShapeLayer *layer() const;
    void _outline(double x, double y);
    void preview();
    bool cancel();
    void _stamp(double x, double y);
    bool press(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    void move(double x, double y) override;
    void release(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool rclick(double x, double y, double tol, Qt::KeyboardModifiers mods) override;
    bool has_wheel() const override { return true; }
    bool wheel(double steps, Qt::KeyboardModifiers mods) override;
};

class DrawPanel : public QWidget, public drawlink::Panel {
    Q_OBJECT
public:
    DrawPanel(moddata::Mod &mod, mapview::MapData &md, QWidget *parent = nullptr);
    ~DrawPanel() override;
    moddata::Mod &mod;
    mapview::MapData &md;
    document::Document *doc = nullptr;
    std::optional<long long> paint_id;
    pospanel::PositionsPanel *pos_panel = nullptr;   // set by the main window (places positions of new provinces)
    adjacency::AdjGeometry *geo = nullptr;
    layerbmp::LandSea *landsea = nullptr;            // (Python: doc.landsea)
    std::map<long long, bool> _move_pops;            // new province -> "move pops along" checked
    DrawHandler prov_handler{this};
    provclip::Clip clip{*this};                      // Ctrl+X / Ctrl+V: cut provinces and paste them elsewhere
    // what "＋ New province…" opens (the dialog's exec(); a check answers it without a window)
    std::function<bool(newprov::NewProvinceDialog &)> run_dialog;

    QLabel *intro, *help, *paint_name, *island_lbl, *island_note, *del_note, *gone_title, *status, *clean_info;
    QButtonGroup *layer_group, *tool_group;
    std::map<QString, QPushButton *> layer_btns, border_btns, tool_btns;
    QStringList layer_keys, tool_keys;
    QWidget *border_row, *island_host, *over_lbl, *erase_lbl, *erase_how_lbl;
    std::map<QString, bool> borders{{QStringLiteral("prov"), true}, {QStringLiteral("state"), true}};
    mismatchbox::MismatchBox *mm_box;
    QSpinBox *paint_spin;
    QCheckBox *island_cb;
    widgets::SliderField *size_spin;
    QComboBox *over = nullptr, *erase = nullptr, *erase_how = nullptr;
    QPushButton *new_btn, *del_btn, *stray_btn, *zero_btn;
    QListWidget *gone_list;
    widgets::Section *paint_section, *new_section, *clean_section;
    QVBoxLayout *new_box;
    terrainpanel::TerrainBox *terrain_box;
    riverpanel::RiverBox *river_box;
    colormappanel::ColormapBox *colour_box;          // (C++ only, 2026-11-10: the land colour map)
    std::optional<std::vector<long long>> _del_armed;
    QTimer *_clean_timer;

    // --- layers
    QString layer() const;
    mapview::MouseHandler *handler();
    void cancel_handlers();                          // (Python: for h in self.handlers(): h.cancel())
    void set_borders(const QString &key);
    void _show_borders();
    void _layer_changed(bool emit = true);
    QString pixel_info(long long x, long long y);
    // --- state
    provbmp::ProvinceBitmap &bmp() const;
    QString tool() const;
    void _tool_changed();
    bool is_fill() const;
    long long stroke_id() const;
    bool needs_target() const;
    void _edit_mode(bool on);
    void set_paint(long long pid, bool from_spin = false);
    void _show_island();
    void _use_selection();
    void refresh(document::Document &doc, const std::vector<long long> &sel, const std::vector<long long> &sea_sel);
    void _update_clean();
    std::pair<long long, QSet<long long>> _clean_key() const;
    void _show_clean();
    // --- painting
    std::vector<std::uint8_t> allowed(const Ids &cur, const Flat *idx = nullptr);
    mismatch::Mismatch *mismatch() override;
    void zoom_requested(double x, double y) override { Q_EMIT zoomRequested(x, y); }
    std::vector<std::uint8_t> _mm_ok(const Flat *idx, const Ids &cur, const QStringList &kinds);
    void commit(const QString &label_text, const Flat &idx, const Ids &old, const Ids &nw, QString msg = QString());
    // --- provinces no longer on the map
    std::vector<long long> missing() const;
    void _show_gone();
    void bring_back(long long pid);
    void _warn_gone(const std::vector<long long> &gone);
    // --- new provinces
    newprov::ProvinceDefs &defs() const;
    std::vector<long long> delete_targets() const;
    void _disarm_delete();
    void _delete_clicked();
    void new_province();
    std::optional<long long> create(const QString &name, bool sea, std::optional<long long> tmpl);
    std::pair<QString, QString> status_of(long long pid) const;
    std::vector<std::pair<long long, QString>> unfinished() const;
    QString _donor_text(const OrderedMap<long long, long long> &donors) const;
    void _show_new();
    QWidget *_card(long long pid, const newprov::NewProv &rec);
    positions::Entry _sea_entry(long long pid);
    void finish(long long pid, const positions::Entry *keep = nullptr);   // keep: its positions as they are (a copy)
    QPushButton *clip_copy_btn = nullptr;
    void fill_at(double x, double y);
    void _clean(std::optional<long long> only);
    // --- map overlay
    std::vector<mapview::Shape> shapes() const;
    QString describe(const std::optional<QVariantList> &tag) const;
    void map_clicked(const QVariantList &) {}

    // --- cut & paste (provclip): the Cut & paste tool's box and the bar at the map's bottom right
    QWidget *clip_box = nullptr;
    QFrame *clip_bar = nullptr;
    QPushButton *clip_cut_btn = nullptr, *clip_paste_btn = nullptr;
    QLabel *clip_note = nullptr;
    std::vector<QPushButton *> clip_box_btns;
    std::vector<QWidget *> paint_rows;               // hidden in Cut & paste
    void _build_clip_box(QVBoxLayout *into);
    void _clip_buttons(QHBoxLayout *row, std::vector<QPushButton *> &out);
    bool clip_active() const;                        // drawing on, Provinces layer, this panel shown
    void _clip_changed();
    void _place_clip_bar();
    bool eventFilter(QObject *o, QEvent *e) override;

    // --- the drawing bar at the map's top left (C++ only, the user's wish 2026-11-09): the layer to draw on (keys 1 / 2
    // / 3) and the pixel grid (G), on the map while drawing is on
    QFrame *draw_bar = nullptr;
    std::map<QString, QPushButton *> bar_layer_btns;
    QPushButton *grid_btn = nullptr;
    bool drawing_shown() const;                      // drawing on and this panel shown
    void set_layer(const QString &key);
    void toggle_grid();
    void _draw_bar();
    void _place_draw_bar();

protected:
    void hideEvent(QHideEvent *e) override;          // (another tab: floating provinces wait out of sight)
    void showEvent(QShowEvent *e) override;

Q_SIGNALS:
    void overlayChanged();
    void message(const QString &text);
    void editModeChanged(bool on);
    void selectRequested(const QList<long long> &pids, const QList<long long> &sea);
    void zoomRequested(double x, double y);
    void undoRequested();
    void layerChanged(const QString &layer);
    void islandRequested(bool on, const QList<long long> &pids);   // mark / unmark provinces as having islands

private:
    struct CleanCache {
        std::pair<long long, QSet<long long>> key;
        std::vector<std::pair<Flat, std::int64_t>> stray, zero;
    };
    std::optional<CleanCache> _clean_cache;
    // (pid, record, area, version, move pops) of every new province, as the cards were last built
    std::optional<std::vector<std::tuple<long long, newprov::NewProv, long long, long long, bool>>> _new_sig;
    std::unique_ptr<mismatch::Mismatch> _mm;
    document::Document *_mm_doc = nullptr;
    layerbmp::LandSea *_mm_ls = nullptr;
};

}  // namespace drawpanel
