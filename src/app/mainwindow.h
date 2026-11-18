// mainwindow.py — The editor's main window. Ported in stages (phase 3: the frame — toolbar, notice bar, rail, side
// panel, status line —, the map view with its map modes, the document of a scenario, the Errors page, Apply / Undo /
// Revert and the recovery file; phases 4–5: every page, 2026-11-07 — nothing of the Python window is left out).
#pragma once
#include "core/modcopy.h"
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/pyjson.h"
#include "map/moreviews_data.h"
#include "ui/scriptedit.h"                         // (ProblemsByRel)

#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QPointF>
#include <QSet>
#include <QSettings>
#include <QTransform>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QComboBox;
class QCompleter;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTimer;

namespace adjacency {
class AdjGeometry;
}
namespace adjpanel {
class MapPanel;
}
namespace applyjournal {
class ApplyJournal;
}
namespace canalpanel {
class CanalPanel;
}
namespace checkpanel {
class MapCheckPanel;
}
namespace changelog {
class ChangelogWorkspace;
}
namespace document {
class Document;
class PopFilter;
class PendingScripts;
struct Step;
}
namespace drawpanel {
class DrawPanel;
}
namespace events {
class EventDB;
struct Entry;
}
namespace eventpanel {
class EventPanel;
}
namespace scriptpage {
class ScriptListPage;
struct Spec;
}
namespace refs {
class RefIndex;
}
namespace layerbmp {
class IndexedBitmap;
class LandSea;
}
namespace mapcheck {
struct Item;
}
namespace mapmodes {
class MoreModes;
}
namespace maplook { class LookPopup; }
namespace mapsize {
class MapSizeWorkspace;
}
namespace shortcuts { class ShortcutsWorkspace; }
namespace submodpage { class SubmodWorkspace; }
namespace checksumpage { class ChecksumWorkspace; }
namespace econest { class EconWorkspace; }
namespace popinfluence { class PopInfluencePage; }
namespace palette { class CommandPalette; struct Command; }
namespace culturewin { class CulturesWindow; }
namespace importpanel {
class ImportWorkspace;
}
namespace mapview {
class MapData;
class MapView;
struct ColorTable;
struct PixelLayer;
struct Shape;
}
namespace modepanel {
class ModeCombo;
struct Swatch;
}
namespace panels {
class EditPanel;
class SplitPanel;
struct Header;
}
namespace popfixbox {
class PopFixBox;
}
namespace pospanel {
class PositionsPanel;
}
namespace production {
class ProdDB;
}
namespace countryhist {
struct NationState;
}
namespace armypanel {
class ArmyPanel;
}
namespace oob {
struct OOB;
class OOBEdits;
}
namespace diplomacy {
class DiploEdits;
}
namespace diploweb {
class DiploWebView;
}
namespace politicspanel {
class PoliticsWorkspace;
}
namespace techpanel {
class TechWorkspace;
}
namespace prodpanel {
class ProductionWorkspace;
}
namespace entryview {
class PagesWorkspace;
class EntryView;
}
namespace diplopanel {
class DiploPanel;
}
namespace partieswin {
class PartiesWindow;
}
namespace reidwin {
class ReIdWindow;
}
namespace eventnet {
class EventNet;
}
namespace bigedit {
class BigEditor;
}
namespace locwin {
class TextsWindow;
}
namespace nodegraph {
class NodeWindow;
}
namespace blockbuilder {
class BlockBuilder;
}
namespace flagstudio {
class FlagStudio;
}
namespace nationpanel {
class NationPanel;
struct Stats;
}
namespace provpanel {
class ProvincePanel;
}
namespace buildtypes {
class BuildTypesPanel;
}
namespace statepanel {
class StatePanel;
}
namespace utilpanel {
class UtilityPanel;
}
namespace rail {
class NavRail;
}
namespace techdata {
class TechDB;
}
namespace widgets {
class ElideLabel;
class PageStack;
}

namespace mainwindow {

using OptStr = std::optional<QString>;
using Rgb = std::tuple<int, int, int>;

extern const std::map<QString, std::vector<std::pair<QString, QString>>> COLOR_MODES_BY_TAB;
extern const QStringList MAP_PAGES;
extern const QSet<QString> PAGE_BOUND_MODES;
extern const std::map<QString, QString> START_MODE, JUMP;
extern const Rgb NO_PROVINCE_RGB;
extern const std::vector<Rgb> CLIMATE_COLORS;
inline constexpr int MIX_Q = 16;               // "mix" map modes: stripes repeat every 16 px along the diagonal
inline constexpr double MIX_MIN = 0.10;        // groups under 10% get no stripe
inline constexpr int MIX_MAX = 4;              // at most 4 stripe colours per province
inline constexpr int UNDO_CONFIRM = 5;         // an undo that takes back more changes than this at once asks first
inline constexpr long long FACTORY_CAP = 8;    // (provpanel.FACTORY_CAP)

QString script_page(const QString &rel);       // the rail page a script file is edited on
std::pair<QString, OptStr> script_place(const QString &rel);
QStringList mix_slots(const std::vector<std::pair<QString, double>> &shares, int q = MIX_Q, double min_share = MIX_MIN,
                      int max_groups = MIX_MAX);
Rgb hashed_color(const QString &key, double s = 0.38, double v = 0.62);   // a stable colour for a key without one

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::shared_ptr<moddata::Mod> mod, std::shared_ptr<mapview::MapData> mapdata, const OptStr &scenario,
               const QString &backup_root);
    ~MainWindow() override;

    std::shared_ptr<moddata::Mod> mod;
    std::shared_ptr<mapview::MapData> mapdata;
    QString backup_root;
    std::unique_ptr<QSettings> settings;
    std::vector<long long> selection, sea_selection;   // ordered list of province ids
    QString view_mode = QStringLiteral("province");
    QString color_mode = QStringLiteral("political");   // the editor always opens in Owner (the user, 2026-10-16)
    std::map<QString, QString> _tab_modes;
    QString mode = QStringLiteral("edit");               // edit | split | capital
    std::optional<long long> last_clicked;
    QString tab = QStringLiteral("pops");
    QString map_sub = QStringLiteral("adj"), prov_sub = QStringLiteral("provinces");
    OptStr scenario;
    bool reload_requested = false, switch_requested = false;
    QString reload_note;
    std::shared_ptr<document::Document> doc;
    std::shared_ptr<document::Document> _draw_doc;   // the one draw_panel->doc names: kept alive (Python's reference)

    // chrome
    QLabel *notice;
    QTimer *_notice_timer;
    QWidget *rec_bar, *scan_bar, *prob_strip;
    QWidget *pend_bar = nullptr;                  // Pops: a new total / mix not applied yet, left behind (C++ only)
    QLabel *pend_lbl = nullptr;
    QLabel *rec_lbl, *scan_lbl, *sub_strip, *prob_text;
    QPushButton *rec_restore_btn, *rec_discard_btn;
    QComboBox *scenario_box;
    // the bar's start: the way out to the list of mods, then the mod's name as a text box (double-click renames it)
    QPushButton *mod_btn = nullptr;
    QLineEdit *mod_name_edit = nullptr;
    QLabel *mod_name_note = nullptr;
    void begin_rename();
    void end_rename(bool keep);
    QString rename_mod(const QString &name);       // -> the message for the status line ("⚠ …" when refused)
    void _show_mod_name();
    modepanel::ModeCombo *color_box;
    QButtonGroup *view_group;
    QPushButton *look_btn, *here_btn, *_undo_btn, *_redo_btn;
    QLineEdit *search;
    QCompleter *completer;
    ::rail::NavRail *rail = nullptr;
    QSplitter *main_split, *left_split;
    mapview::MapView *map;
    widgets::PageStack *stack;
    checkpanel::MapCheckPanel *chk_panel;
    // Population: the pop editor (with the pop problems' strip on top) and the split allocator
    panels::EditPanel *edit_panel;
    popfixbox::PopFixBox *pop_fix;
    panels::SplitPanel *split_panel;
    OptStr capital_region;                       // mode "capital": the state whose capital is being picked
    std::optional<std::pair<QString, std::vector<moddata::Pop>>> clipboard;   // Copy pops: (description, template)
    // Provinces & states: Provinces | States | Utility states
    provpanel::ProvincePanel *prov_panel;
    statepanel::StatePanel *state_panel;
    utilpanel::UtilityPanel *util_panel;
    buildtypes::BuildTypesPanel *bt_panel;       // C++ only (2026-11-16): Building types
    void _buildings_changed();                   // buildings.txt changed: the province buildings read again
    QWidget *prov_host, *prov_seg_bar;
    QPushButton *prov_switch;
    std::map<QString, QPushButton *> prov_sub_btns;
    widgets::PageStack *prov_stack;
    OptStr util_view;                            // the utility state shown on the map
    bool util_edit = false;                      // map clicks add / remove its provinces
    OptStr _state_del;                           // mode "statepick" for a deletion: the state that goes
    long long _region_version = 0;
    std::map<QString, QString> _loc_tab;         // localisation key -> the rail page its pending edit counts for
    // Nation: a nation at the start (its parties box and flags box inside)
    nationpanel::NationPanel *nations_panel;
    OptStr nation_tag;
    partieswin::PartiesWindow *party_win = nullptr;   // (windows of their own: made when first opened)
    flagstudio::FlagStudio *flag_win = nullptr;
    reidwin::ReIdWindow *reid_win = nullptr;
    eventnet::EventNet *net_win = nullptr;
    nodegraph::NodeWindow *node_win = nullptr;
    blockbuilder::BlockBuilder *block_win = nullptr;
    double _tool_edit = 0.0;                     // when the block builder last changed a script (monotonic seconds)
    QTimer *_lazy_panel;                         // the page behind a tool window follows its edits in a pause
    bigedit::BigEditor *big_win = nullptr;
    culturewin::CulturesWindow *cult_dlg = nullptr;
    entryview::EntryView *cg_win = nullptr;              // (follows the document while the window is open)
    locwin::TextsWindow *text_win = nullptr;
    // Armies: a nation's armies and navies at the start
    armypanel::ArmyPanel *army_panel = nullptr;
    // Diplomacy: relations, influence and treaties at the start between the main nation and a second one (B)
    diplopanel::DiploPanel *diplo_panel = nullptr;
    diploweb::DiploWebView *dip_view = nullptr;    // the relations instead of the map (Diplomacy)
    OptStr diplo_b;
    // Scripts: the list of events / decisions in the side panel, the script editor in the middle, the map under it
    eventpanel::EventPanel *event_panel = nullptr;
    scriptedit::ScriptWorkspace *script_ws = nullptr;
    std::vector<scriptpage::ScriptListPage *> script_pages;   // the pages that borrow the script editor
    scriptedit::ProblemsByRel _script_problems;    // every script problem by its file (the borrowed editors' pages)
    QString _map_where = QStringLiteral("main");  // where the map is: "main" / "mini" (window) / "dock"
    std::map<QString, std::pair<QTransform, QPointF>> _map_cams;   // each place keeps its own camera
    std::optional<int> _side_before;               // the side panel's width before the Scripts tab widened it
    std::optional<std::pair<QString, QString>> _events_fill_key;
    struct JumpMark {                              // the yellow ring on the spot a jump went to
        QPointF pt;
        double r;
        Rgb rgb;
    };
    std::optional<JumpMark> _jump_mark;
    // the Map tab: Adjacencies | Canals | Positions | Draw (the map check lives in the Errors tab)
    adjpanel::MapPanel *map_panel;
    canalpanel::CanalPanel *canal_panel;
    pospanel::PositionsPanel *pos_panel;
    drawpanel::DrawPanel *draw_panel;
    QWidget *map_tab, *map_seg_bar, *adj_seg_bar;
    QButtonGroup *map_sub_group;
    std::map<QString, QPushButton *> map_sub_btns, adj_sub_btns;
    widgets::PageStack *map_stack;
    mapsize::MapSizeWorkspace *size_ws;           // (the full-window workspaces: Map size and Politics so far)
    politicspanel::PoliticsWorkspace *pol_ws;
    techpanel::TechWorkspace *tech_ws;
    prodpanel::ProductionWorkspace *prod_ws;
    changelog::ChangelogWorkspace *cl_ws;
    entryview::PagesWorkspace *war_ws;
    entryview::PagesWorkspace *rules_ws;
    entryview::PagesWorkspace *lib_ws;
    importpanel::ImportWorkspace *imp_ws;
    shortcuts::ShortcutsWorkspace *keys_ws;
    submodpage::SubmodWorkspace *sub_ws;
    checksumpage::ChecksumWorkspace *ck_ws;           // (C++ only, 2026-11-18)
    econest::EconWorkspace *econ_ws;
    popinfluence::PopInfluencePage *popinf_ws;
    palette::CommandPalette *cmd_palette = nullptr;
    bool _palette_parts_made = false;
    QScrollArea *side_scroll;
    std::map<QWidget *, int> _page_scroll;        // page -> how far down it was left
    QWidget *_page_shown = nullptr;
    bool _stick_bottom = false;                   // keep the panel scrolled to the bottom while its height settles
    QTimer *_stick_timer;
    // status line
    QLabel *st_cursor, *st_pending;
    widgets::ElideLabel *st_hover, *st_msg;
    QPushButton *err_badge, *undo_apply_btn, *redo_apply_btn, *revert_btn, *by_file_btn, *apply_btn;
    QCheckBox *keep_cb = nullptr;                 // "Keep a copy of the original mod" (C++ only, modcopy)
    QCheckBox *almanac_cb = nullptr;              // "Almanac with every Apply" (C++ only, 2026-11-15)
    QString _almanac_after_apply();               // writes "<mod> almanac.html" into the mod if ticked: the note
    void _arm_keep_copy();
    int _keep_id = 0;
    void _keep_copy_done(const modcopy::Result &r);

    // what the pages read
    std::unique_ptr<adjacency::AdjGeometry> geo;
    std::unique_ptr<layerbmp::LandSea> landsea;        // (Python: doc.landsea)
    std::unique_ptr<applyjournal::ApplyJournal> journal;
    std::unique_ptr<techdata::TechDB> tech_db;         // (Python: mod.tech_db / mod.prod_db)
    std::unique_ptr<production::ProdDB> prod_db;
    std::unique_ptr<document::PendingScripts> pending_script;
    std::unique_ptr<mapmodes::MoreModes> more_modes;
    std::map<QString, std::vector<double>> _mode_vals; // map mode -> its values on the map now
    std::pair<QString, std::optional<Rgb>> _chk_banner;
    QString _adj_field;                                // the field an adjacency pick fills ("a" / "b" / "through")
    // Drawing → Terrain / Rivers: the view of the bitmap (made again when the document's bitmap is another one)
    std::map<QString, std::pair<const layerbmp::IndexedBitmap *, std::shared_ptr<mapview::PixelLayer>>> _layer_views;
    std::optional<std::optional<QString>> _err_layer;
    OrderedMap<QString, long long> _pend_pages, _err_pages;
    struct PageProblem {                               // (severity, category key, item, part of the page)
        QString sev, key;
        const mapcheck::Item *it;
        OptStr part;
    };
    std::map<QString, std::vector<PageProblem>> _page_problems;
    struct ProbCur {                                   // the problem the line names: (page, key, item) — the item a
        QString page, key;                             // COPY (Python keeps its object alive over a new check run)
        std::shared_ptr<const mapcheck::Item> it;
    };
    struct SearchHit {                                 // ("p", pid) / ("r", rid) / ("c", tag)
        QChar kind;
        long long pid = 0;
        QString key;
    };
    OrderedMap<QString, SearchHit> search_map;
    void _build_search_index();
    void run_search(const QString &text);
    std::map<QString, long long> _prob_at;
    OptStr _prob_page;
    std::optional<ProbCur> _prob_cur;
    bool _prob_jumping = false;
    void next_problem(const OptStr &page = std::nullopt);
    void jump_to_problem(const QString &key, const mapcheck::Item *item);
    void _problem_to_errors();
    void _jump_adj(const mapcheck::Item *item);
    QTimer *_check_timer, *_render_timer, *_rec_timer;
    QString _rec_path;
    OptStr _rec_offer, _rec_sig;
    const void *_undo_armed = nullptr;
    bool _revert_armed = false;

    std::optional<QString> rail_page() const;
    void go_page(const QString &key);
    void open_war(const QString &rel = {});
    QWidget *_shown_page() const;
    void _page_changed();
    void set_tab(const QString &tab);
    void fill_color_modes();
    Q_INVOKABLE void message(const QString &text, int ms = 6000, bool error = false);   // status line; ⚠ / error=True in red
    void refresh_scenarios();
    void _discard_shared(document::Document &d);
    void load_scenario(QString scenario);
    void set_view_mode(const QString &mode);
    std::optional<QString> _mode_custom(const QString &mode) const;
    std::optional<modepanel::Swatch> _mode_swatch(const QString &mode);
    OrderedMap<QString, double> state_sizes();
    void on_color_mode(int idx);
    maplook::LookPopup *look_pop = nullptr;
    void open_map_look();
    void _map_look_changed();
    std::vector<double> mode_values(const QString &m);
    void _size_legend();
    std::optional<QString> draw_layer() const;
    std::shared_ptr<mapview::PixelLayer> layer_view();
    void _layer_pixels_changed(const QString &lay, const std::array<long long, 4> &box);
    void _draw_layer_changed();
    bool _prov_mismatch_shown() const;
    mapview::ColorTable _color_table();
    void _error_map_changed();
    mapview::ColorTable color_table();
    void render_map();
    void schedule_render();
    OrderedMap<OptStr, long long> mesh_index(const QString &rid, const OptStr &own);
    OrderedMap<long long, long long> _context_codes(const std::vector<long long> &pids);
    void _leaders_outline();                     // (C++ only) the picked nations outlined red (Generals / Admirals)
    QString _lead_outline_key;
    void update_highlight();
    void set_selection(const std::vector<long long> &pids, bool render = true, const std::vector<long long> &sea = {});
    void _set_chk_banner(const QString &text, std::optional<Rgb> rgb);
    void _show_banner();
    void _mode_legend();
    std::map<QString, bool> _bld_shown{{QStringLiteral("factories"), true}, {QStringLiteral("fort"), true},
                                       {QStringLiteral("naval_base"), true},
                                       {QStringLiteral("railroad"), false}};   // (the Buildings legend: kinds on / off)
    void _bld_toggle(const QString &key);
    void _building_colors(std::vector<Rgb> &t, const std::vector<long long> &land);
    void _building_legend();
    void _update_errors();
    std::pair<OptStr, OptStr> problem_place(const QString &key, const OptStr &tab, const mapcheck::Item *item) const;
    void _update_here();
    void schedule_checks();
    void _checks_due();
    QWidget *active_map_panel() const;
    QWidget *overlay_panel() const;
    bool _cancel_handler(QWidget *panel);          // (Python: panel.handler.cancel())
    void set_map_sub(const QString &key);
    void _sync_map_tool();
    void _update_draw_pill();
    void toggle_draw();
    void update_overlay(QObject *sender = nullptr);
    void _apply_overlay(QWidget *panel = nullptr);
    void start_adj_pick(const QString &field);
    bool _map_click_hook(double x, double y, double tol, Qt::KeyboardModifiers mods);
    void zoom_to_point(double x, double y, double z = 4.0);
    void on_click(long long pid, Qt::KeyboardModifiers mods);
    void on_hover(long long pid, int x, int y);
    void on_right_click(long long pid, Qt::KeyboardModifiers mods);
    void escape();
    std::vector<QString> selected_states() const;
    std::optional<std::pair<QString, QString>> header() const;   // (title, sub) — the pages that read only these two
    panels::Header header3() const;                // (title, sub, info) — Python's header() whole
    void refresh_panel();
    void on_filter_changed();
    // the Population page's edits
    void do_scale(double pct);
    void do_set_total(long long total);
    void do_consolidate();
    void start_capital_pick();
    QString filter_description() const;
    void start_split();
    void end_split(bool silent = false);
    void apply_split();
    void on_tool(const QString &key);
    void do_mix(const QString &field, const OrderedMap<OptStr, long long> &targets);
    void do_pop_changes(const QList<long long> &pids, const document::PopFilter &flt, std::optional<long long> total,
                        const QString &field, const std::vector<std::pair<OptStr, double>> &shares);
    void _update_pend_bar();
    void _pend_go_back();
    void open_cultures(int start = 0);
    void _econ_show(const QList<long long> &pids, const QString &level);   // Output estimate → its provinces
    void open_culture_groups();       // (the cultures window on its second page)
    void open_religions();
    void after_culture_change();
    // the Provinces & states page's edits
    void on_state_move(const QString &kind);
    void on_make_capital();
    void _do_state_move(const QString &target, bool create = false);
    void _do_state_delete(const OptStr &target);
    void _regions_changed();
    void on_membership(const QString &kind, const QString &name);
    std::vector<std::int64_t> piece_sizes(long long pid) const;
    void on_name(const QString &key, const QString &text, const OptStr &section);
    QString _where(const QList<long long> &pids) const;
    void on_prov_edit_for(const QString &label, const std::function<void(long long, history::ProvState &)> &fn,
                          const QList<long long> &pids);
    void on_prov_edit(const QString &label, const std::function<void(long long, history::ProvState &)> &fn);
    void _capital_follows(const QList<long long> &pids);
    void _prov_switch_text();
    void set_prov_sub(const QString &key);
    void _util_view(const QString &rid);
    void _util_edit_toggled(bool on);
    void _util_mark(const QStringList &rids, bool on);
    void _util_selected(bool add);
    void _pixels_changed(const std::array<long long, 4> &box, const std::set<long long> &ids);
    void _defs_changed();
    void on_island(bool on, const QList<long long> &pids);
    // the Nation page
    nationpanel::Stats nation_stats(const OptStr &tag);
    void show_nation(const OptStr &tag, bool zoom = false);
    std::optional<long long> _nation_capital();
    void start_nation_capital_pick();
    void on_nation_edit(const QString &label, const QString &tag,
                        const std::function<void(countryhist::NationState &)> &fn);
    void new_nation(const QString &tmpl);
    void on_script_text(const QString &label, const QString &rel, const QString &text);
    void on_script_remove(const QString &label, const QString &rel);
    void on_tech_edit(const QString &label, const QString &key, const QString &text);
    void on_prod_edit(const QString &label, const QString &key, const QString &text);
    void on_prod_other(const QString &label, const QString &what, const QString &key, const QString &value);
    void on_prod_add(const QString &key, const QString &text, const std::optional<std::pair<QString, QString>> &building,
                     const QString &bname);
    void on_prod_remove(const QString &key);
    void on_invention_new(const QString &key, const QString &file, const QString &text, const QString &name);
    void on_invention_remove(const QString &key);
    void on_tech_order(const OptStr &area, const QString &key, long long step);
    void on_tech_drop(const OptStr &area, const QString &key, const QString &target);
    void on_tech_row_remove();
    void on_tech_row();
    void on_tech_gui(long long rows);
    void open_place(const QString &kind, const QString &key);
    void open_flag_studio(const OptStr &sfx = std::nullopt, const OptStr &tag = std::nullopt);
    void open_parties_window();
    void _flag_pick_on_map();
    // the Armies page
    void start_unit_move(const QString &kind);
    void on_oob_edit(const QString &label, const QString &tag, const std::function<void(oob::OOB &)> &fn);
    // the Diplomacy page
    void show_other(const QString &tag);
    void on_diplo_edit(const QString &label, const std::function<void(oob::OOBEdits &, diplomacy::DiploEdits &)> &fn);
    void _diplo_view(const QString &key);
    void _place_map();
    std::pair<QTransform, QPointF> _map_cam() const;
    std::vector<mapview::Shape> _jump_shapes() const;
    // the Scripts page
    void on_event_literacy(const QString &rel, const QString &uid, bool on);
    void on_event_cats(const std::vector<std::pair<std::pair<QString, OptStr>, QStringList>> &changes,
                       const QString &label);
    void on_event_province(long long pid);
    void on_script_replace(const OrderedMap<QString, QString> &changes, const QString &label);
    void _events_changed();
    void _events_focus();
    void _events_map_toggled(bool on);
    mapview::ColorTable _event_colors(std::vector<Rgb> &t, const std::vector<long long> &land);
    refs::RefIndex *_script_refs();
    void _script_jump(const QString &rel, long long pos);
    OptStr _event_name(const QString &eid);
    bool _own_script_page(const QString &rel, long long pos);
    QWidget *_script_page(const scriptpage::Spec &spec);   // (Python: _rebels_page / _pop_type_page)
    void open_script_place(const QString &rel, long long pos = 0);
    void _script_tool(const QString &key);
    QString _entry_name(const events::Entry &e);
    void open_network();
    void open_palette();
    std::vector<palette::Command> _palette_commands();
    std::vector<palette::Command> _palette_more();
    std::vector<palette::Command> _palette_buttons(QSet<QString> have);
    void open_texts();
    void open_nodes();
    // the block builder: for the entry picked in Scripts, an entry given, or a new "event" / "decision"
    void open_blocks(const OptStr &what = std::nullopt, events::Entry *entry = nullptr);
    QWidget *_tool_window() const;
    bool _tool_busy() const;
    void _lazy_refresh();
    void on_script_with_names(const QString &label, const QString &rel, const QString &text,
                              const OrderedMap<QString, QString> &names);
    void open_event_id(const QString &eid);
    void _refresh_script_windows();
    void open_reid();
    void on_flag_edit(const QString &label, const OrderedMap<QString, QImage> &files);
    void on_flag_delete(const QString &label, const QStringList &names);
    void open_big(const QString &rel, const QStringList &path, const QString &noun);
    void _big_typed();
    void _show_window(QWidget *win, std::optional<int> w = std::nullopt, std::optional<int> h = std::nullopt);
    void _refresh_windows();
    std::map<QString, Rgb> _file_colors();
    void _sync_nation_colors(bool force = false);
    void _nation_color_preview(const std::optional<Rgb> &rgb);
    void on_edit_definitions(const QString &kind);
    void on_asset_edit(const QString &label, const OrderedMap<QString, moreviews::Asset> &files);
    void on_bundle_edit(const QString &label, const OrderedMap<QString, QString> &texts,
                        const OrderedMap<QString, moreviews::Asset> &files);
    void on_bundle_names(const QString &label, const OrderedMap<QString, QString> &texts,
                         const OrderedMap<QString, moreviews::Asset> &files, const OrderedMap<QString, QString> &names);
    QWidget *_boards_page();
    void _after_import();
    QWidget *_mapmarks_page();
    std::optional<QSet<QString>> _used_pictures();
    void on_strip_change(const QString &label, const OrderedMap<QString, moreviews::Asset> &files,   // (StripFiles)
                         const OrderedMap<QString, QString> &texts);
    bool _delete_before_cut(const std::vector<long long> &pids);
    void _reload_mod();
    bool _pending_counts();
    void _show_sub_strip();
    QString _submod_sync();
    void on_doc_changed();
    OrderedMap<QString, long long> pending_by_tab();
    void update_pending();
    std::pair<long long, QString> _scope_text(const void *step) const;
    void _flush_held_edits();                      // a panel's held-back edit (a resting slider) made a step now
    OptStr undo();
    OptStr redo();
    void _refresh_undo_tips();
    void _script_typed(const QString &rel);
    QTimer *_typing_timer = nullptr;
    void _refresh_apply_undo();
    void _recovery_tick();
    bool _recovery_save(bool auto_ = false, const QString &note = {});   // -> written (Python: the data, or None)
    bool keep_edits_over_reload(const QString &note = {});
    // the pending edits by file / group: Apply or Revert one at a time
    bool _rec_internal = false;                        // (per-file Apply / Revert reverts inside: no recovery offer)
    void _revert_inside();
    bool apply_only(const QSet<QString> &keep_units);
    bool revert_only(const QSet<QString> &drop_units);
    void open_pending_panel();
    QString _apply_preview();
    QWidget *_pending_panel = nullptr;
    // files added to / taken from the mod while the editor runs
    std::set<QString> _mod_listing() const;
    void _scan_tick();
    void rescan_mod();
    std::optional<std::set<QString>> _listing;
    std::pair<QStringList, QStringList> _scan_seen;
    QTimer *_scan_timer = nullptr;
    bool _apply_whole(const QString &which);
    bool undo_apply();
    bool redo_apply();
    std::optional<std::pair<QString, QString>> _whole_armed;   // (which, the Apply's folder): asked once
    void _offer_recovery();
    void _recovery_restore();
    void _recovery_discard();
    bool confirm_discard(const QString &what = QStringLiteral("close"));
    void revert();
    // the changelog: what Apply would write (the script files' changes and the edit steps since the last Apply)
    pyjson::Array _changelog_pending();
    bool _event_step(const document::Step &entry) const;
    bool _step_matters(const document::Step &entry);
    struct ClInfo {                                // [step, the tab it was made in, it changed script files, applied]
        std::shared_ptr<document::Step> step;
        QString tab;
        bool script = false, applied = false;
    };
    QHash<const document::Step *, ClInfo> &_cl_edits();
    void _cl_note();
    QStringList _applied_undone();
    const document::Document *_cl_doc = nullptr;
    QHash<const document::Step *, ClInfo> _cl_info;
    bool _cl_script = false;
    std::optional<qsizetype> _apply_armed;
    static constexpr int APPLY_CONFIRM = 3;        // an Apply that takes back more applied changes than this asks first
    bool save(bool reload = true);
    bool close_number_gaps(bool reload = true);
    OrderedMap<long long, long long> renumbered;   // the provinces the last close-up moved (Python: dict(job.moves))

protected:
    void closeEvent(QCloseEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *e) override;   // the tooltips of Apply / undo / redo, worked out when shown

private:
    QWidget *_build_toolbar();
    QWidget *_build_tabs();
    QWidget *_build_statusbar();
    void _sync_rail();
    std::pair<QString, double> _discard_armed{QString(), 0.0};
};

}  // namespace mainwindow
