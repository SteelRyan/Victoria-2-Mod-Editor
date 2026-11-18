// eventpanel.py — The Events & Decisions tab's panel: the mod's event and decision files, each opened to its events /
// decisions (Title · ID · File · MTTH · ×10 years, sorted by any column), the user's own categories on files and
// entries (kept in the files as "#category:" comments, written on Apply), and the selected one in full — who can have
// it at the start, how often within 10 years, its MTTH and what changes it, its effects, what fires it. The map shows
// it for every nation (every province, for a province event).
#pragma once
#include "core/countryhist.h"
#include "core/events.h"
#include "core/history.h"
#include "core/modifiers.h"
#include "core/ordered_map.h"
#include "core/refs.h"
#include "core/store.h"
#include "ui/mapview.h"
#include "ui/relview.h"
#include "ui/scriptedit.h"

#include <QFont>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QTimer;
namespace cbpanel { class WarGoalView; }
namespace document { class Document; }
namespace modpanel { class ModifierForm; class ModifierView; }
namespace widgets { class BadgeButton; class Section; class WrapRow; }

namespace eventpanel {

using events::Entry;
using events::EventDB;
using events::Tri;
using Rgb = std::tuple<int, int, int>;
using OptStr = std::optional<QString>;
using CatKey = events::EventCats::CatKey;    // (rel file, uid) — (rel file, None) for a file
using CatChanges = std::vector<std::pair<CatKey, QStringList>>;   // (Python: a dict, in its order)
using Who = store::Key;                      // a nation's tag or a province's id

extern const QStringList COLS;
extern const std::vector<std::pair<QString, QString>> SHOWS;
extern const std::vector<Rgb> PALETTE, GRID;
extern const Rgb NONE_RGB;
constexpr int MAX_HTML_LINES = 120;          // readable conditions / effects shown (the rest: the text)

// (a, b) positions of the new text whose refs differ from before (the typed part) — or None: nothing
std::optional<std::pair<qsizetype, qsizetype>> _changed_range(const std::vector<refs::Ref> &old,
                                                              const std::vector<refs::Ref> &fresh, qsizetype old_len,
                                                              qsizetype new_len);
// province ids named in script: `123 = { … }` scopes and province_id / capital values
void prov_ids(events::View nodes, std::set<long long> &out);
std::set<long long> prov_ids(events::View nodes);
QIcon cats_icon(const std::vector<Rgb> &rgbs, int size = 14);

// a row's data (Python: ("e", rel, entry, group) / ("f", rel, None, group) / ("c", name, None, None))
struct RowData {
    QChar kind;                              // 'e' / 'f' / 'c' (0: none)
    QString key;                             // the file — or the category's name
    Entry *e = nullptr;
    OptStr group;                            // the category it is listed under, None outside the category view
};
RowData row_data(const QTreeWidgetItem *it);

class CatTree : public QTreeWidget {
    // The list: rows can be dragged onto a category's row (the category view) or a category in the bar; the keys 0–9
    // go to the panel (a category on / off the selected rows).
    Q_OBJECT
public:
    CatTree();
    std::function<void(int)> keyHook;
    void forget();                           // the rows are made anew: the lit one is gone (Python: RuntimeError)
Q_SIGNALS:
    void dropped(const QString &category, bool copy);   // the category (or "__none"), Ctrl held (copy, not move)

protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QTreeWidgetItem *_hl = nullptr;          // the category row lit while something is dragged over it
    QTreeWidgetItem *_target(const QPoint &pos);
    void _light(QTreeWidgetItem *it);
};

class CatButton : public QPushButton {
    // A category in the bar: click = on / off the selected rows; rows dragged onto it get it.
    Q_OBJECT
public:
    explicit CatButton(const QString &text);
Q_SIGNALS:
    void dropped();

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

// a scope as a dict key: ("c", tag) / ("p", the province id as text)
using ScopeKey = std::pair<QString, QString>;
ScopeKey scope_key(const events::Scope &sc);
struct Agg {                                 // {"sure", "maybe", "pmax", "per", "allowed", "times", "cat", "pot"}
    long long id = 0;                        // (Python: id(a) — which computation this is)
    long long sure = 0, maybe = 0;
    double pmax = 0.0;
    OrderedMap<ScopeKey, events::Chance> per;
    long long allowed = 0;
    std::pair<double, double> times{0.0, 0.0};
    events::Class cat;
    OrderedMap<ScopeKey, Tri> pot;
};
struct NationChance {                        // (can, (p lo, p hi), times, mtth, allow)
    Tri can;
    std::pair<double, double> p, times;
    std::optional<std::pair<double, double>> mtth;
    Tri allow;
};
using FocusLayer = mapview::FocusLayer;      // a layer of the map's marks: {"style", "pids", "rgb"?}
// what the map shows of the entry picked: ("c" / "p", {tag or pid: (colour, certain)})
struct MapValues {
    QString kind;
    OrderedMap<QString, std::pair<Rgb, bool>> out;   // the key: a tag, or a province id as text
};
// what the answers depend on: the history edits and the nations' edits (Python: a hash of them, sorted)
struct WorldSig {
    std::vector<std::pair<long long, history::ProvState>> prov;
    std::vector<std::pair<QString, countryhist::NationKey>> nats;
    bool operator==(const WorldSig &o) const = default;
};
// a sort key (Python: a tuple of a number or two and a text)
struct SortKey {
    int a = 0;
    double n1 = 0.0, n2 = 0.0;
    QString s;
    bool operator<(const SortKey &o) const;
};

class EventPanel : public QWidget {
    Q_OBJECT
public:
    explicit EventPanel(moddata::Mod &mod, QWidget *parent = nullptr);
    ~EventPanel() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::unique_ptr<EventDB> db;
    std::unique_ptr<events::World> world;
    std::unique_ptr<events::Judge> judge;
    std::unique_ptr<events::PotentialJudge> pjudge;   // "could it ever": who / where alone
    Entry *sel = nullptr;                    // the Entry shown
    OptStr sel_file;                         // … or the file ("events/x.txt")
    OptStr nation;                           // the nation the answers are for (a click on the map)
    QHash<const Entry *, Agg> agg;
    QSet<QString> open_files;                // files unfolded in the list (grouped by file)
    QSet<QString> open_cats;                 // categories unfolded (grouped by category; "__none" = without one)
    std::set<std::pair<QString, QString>> open_cat_files;   // (category, file) unfolded under a category
    OptStr sel_cat;                          // … or a category's row
    QString _group = QStringLiteral("file"); // the list grouped by "file" or by "cat"egory
    OptStr _rename;                          // the category being renamed in the name box (None: a new one)
    OrderedMap<QString, CatButton *> bar_btn;
    scriptedit::ScriptWorkspace *ws = nullptr;   // the editor in the middle of the window
    scriptstore::ScriptStore *store = nullptr;   // doc.maps["scripts"]: the texts the editor holds
    std::unique_ptr<refs::RefIndex> refs;    // RefIndex of those texts
    std::optional<refs::Ref> sym;            // the ref at the editor's cursor
    bool sym_on_map = false;                 // (Python: sym[0] is None — chosen on the map)
    std::vector<refs::Ref> _sel_refs;        // the refs inside the editor's selected text
    std::function<bool()> hold;              // () -> True while the user builds in a tool window: reading waits
    std::vector<scriptedit::ScriptWorkspace *> extra_ws;   // the editors other pages borrow (Rebels, Pop types)
    std::function<void(const QString &, const QString &, const QString &)> script_edit;   // set by the window
    std::vector<long long> set_pins;

    // the widgets
    QWidget *mini_host, *filters, *bar, *new_row, *cat_host, *chips;
    QPushButton *blocks_btn = nullptr, *clear_nation, *new_ok, *row_jump;
    OrderedMap<QString, widgets::BadgeButton *> kind_btn;
    OrderedMap<QString, QPushButton *> group_btn;
    QLineEdit *search, *new_edit;
    QComboBox *show_box, *cat_box, *pic_box;
    QLabel *nation_lbl, *progress, *bar_msg, *count, *mark_lbl, *row_prob, *sym_head, *sym_body, *sym_sum,
        *detail_head, *title, *meta, *pic, *pic_note, *body, *legend, *new_warn;
    QCheckBox *only_nation, *only_sym, *lit_box;
    widgets::WrapRow *bar_lay, *chip_lay;
    widgets::Section *list_sec, *sym_sec, *detail, *legend_sec;
    CatTree *lst;
    relview::RefTree *sym_tree;
    modpanel::ModifierForm *sym_form;
    modpanel::ModifierView *mod_view;
    cbpanel::WarGoalView *cb_view;

    // categories
    QString _settings_key() const;
    void _load_cats();
    void _save_cats();
    Rgb color(const QString &name);
    QStringList cat_order();
    QStringList cats_of(const Entry &e);
    QStringList file_cats(const QString &rel);
    QStringList key_cats(const CatKey &key);
    std::vector<CatKey> _all_keys() const;
    std::optional<CatKey> _cur_key() const;
    QStringList _cur_cats();
    void _say(const QString &text);
    static QString _rows(qsizetype n);
    std::vector<CatKey> _selected_keys();
    std::vector<std::pair<CatKey, OptStr>> _drag_keys();
    qsizetype _emit(const CatChanges &changes, const QString &text);
    void toggle_cat(const QString &name);
    void clear_cats();
    void _key(int n);
    void _drop_on(const QString &target, bool copy, const std::vector<std::pair<CatKey, OptStr>> &keys_groups);
    void _drop_cat(const QString &name);
    void _set_color(const QString &name, const Rgb &rgb);
    QWidget *_color_grid(const QString &name, QMenu *menu);
    void _tree_menu(const QPoint &pos);
    void _move_cat(const QString &name, int step);
    void _new_cat(const OptStr &rename = std::nullopt);
    void _new_done();
    void _delete_cat(const QString &name);
    void _cat_menu(const QString &name, QWidget *btn, const QPoint &pos);
    void cats_changed();
    void _fill_cat_box();
    void _fill_bar();
    void _style_bar();
    void _fill_chips();

    // the editor
    void attach(scriptedit::ScriptWorkspace *ws);
    void attach_extra(scriptedit::ScriptWorkspace *ws);
    void ensure(document::Document *doc);
    QStringList _extra_rels();
    void _text_edited(const QString &rel);
    void reparse_now();
    void _reparse_due();
    void _reparse();
    void _open_entry(Entry *e, bool record = true);
    void _open_mod(const QString &name);
    void set_problems(const OrderedMap<QString, std::pair<long long, long long>> &counts,
                      const scriptedit::ProblemsByRel &by_rel);
    void _attribute();
    std::pair<OptStr, std::vector<scriptedit::Problem>> _cur_problems();
    void _fill_row_problems();
    void _jump_problem();
    std::pair<OptStr, QStringList> _row_problems(const QString &rel);
    void _mark_problem(QTreeWidgetItem *item, const OptStr &sev, const QStringList &texts);
    void _open_cb(const QString &key, bool new_tab = false);
    void show_cb(const OptStr &key = std::nullopt);
    void _place_changed(const QString &rel, long long pos);
    void _mark_row(Entry *e);
    void _symbol_changed(const std::optional<refs::Ref> &r);
    void _sym_changed();
    void focus_symbol(const QString &kind, const QString &t);
    void _tree_go(const QString &rel, long long pos);
    void _sym_hits_calc();
    void _paint_sym_rows();
    void _paint_row(QTreeWidgetItem *item, long long n, const std::optional<Rgb> &rgb);
    void _fill_relations();
    void _selection_changed(const std::vector<refs::Ref> &refs);
    void _list_double(QTreeWidgetItem *item, int col);

    // modifiers and flags, described
    const modifiers::ModifierDB::Uses *_mod_used_map();
    std::optional<modifiers::Attrs> _mod_attrs(const QString &name);
    struct Holders {                         // flag_holders: (at the start, {who: "now" / "later"}, [notes])
        QSet<Who> hist;
        OrderedMap<Who, QString> could;
        QStringList notes;
    };
    Holders flag_holders(const QString &kind, const QString &t);
    QString _names(const std::vector<Who> &keys, size_t n = 8);
    QString _cname(const OptStr &tag) const;

    // what the cursor is on
    OrderedMap<long long, double> _share(const QString &kind, const QString &t);
    QStringList info_extra(const QString &kind, const QString &t);
    static void _head(QLabel *lbl, const QString &text, const Rgb &rgb);
    void _fill_sym();
    std::pair<std::vector<FocusLayer>, std::vector<long long>> focus_spec();
    std::pair<std::vector<FocusLayer>, std::vector<long long>> _sym_focus();

    // data
    void refresh(document::Document *doc, const OptStr &nation = std::nullopt);
    WorldSig _world_sig();
    void _doc_changed(const QSet<long long> &pids);
    void _start();
    void _work();
    std::optional<NationChance> nation_chance(const Entry &e);
    QString words(const QString &text);
    QString nm(const Entry &e);

    // the nation
    void set_nation(const OptStr &tag);
    void _show_nation();

    // the list
    QString _kind() const;
    bool _folder_ok(const QString &rel) const;
    bool _cat_ok(const Entry &e);
    bool _wanted(const Entry &e);
    std::optional<std::pair<double, double>> times(const Entry &e);
    double fired_state(const Entry &e);
    static QString _fmt_times(const std::pair<double, double> &t);
    QString _times_text(const Entry &e);
    SortKey _sort_value(const Entry &e, int col);
    SortKey _file_value(const QString &rel, const std::vector<Entry *> &entries, int col);
    void _header_clicked(int col);
    void _open_all(bool on);
    void _set_kind_vis();
    void _kind_changed();
    const QSet<const Entry *> &_mentions(const QString &tag);
    int _mark(const Entry &e);
    void _fill_marks(const std::vector<Entry *> &rows);
    void _set_group(const QString &k);
    void _expanded(QTreeWidgetItem *it, bool on);
    QTreeWidgetItem *_row(Entry *e, const QFont &italic, const QBrush &grey, const OptStr &group = std::nullopt);
    std::vector<Entry *> _sorted(std::vector<Entry *> rows);
    QTreeWidgetItem *_file_row(const QString &rel, const std::vector<Entry *> &rows, const OptStr &group,
                               const QFont &bold);
    QTreeWidgetItem *_cat_row(const QString &name, qsizetype n, qsizetype n_files, double tot, const QFont &bold);
    void _fill_list();
    void _picked(QTreeWidgetItem *cur, QTreeWidgetItem *prev);
    void select(Entry *e);
    bool _mod_exists(const QString &name);
    void _link(const QString &href);
    void _show_entry(Entry *e);
    void go_back();
    void go_forward();

    // the selected one
    QString _conds_html(events::View nodes, const std::optional<events::Scope> &scope,
                        std::optional<long long> year = std::nullopt);
    QString _effects_html(events::View nodes);
    void _fill_file_detail(const QString &rel);
    void _fill_cat_detail(const QString &name);
    QStringList _picture_names(bool is_event);
    void _set_picture(const QString &name);
    void _fill_picture(Entry *e);
    void _lit_toggled(bool on);
    void _fill_lit();
    void _fill_detail();
    QString _nlink(const QString &tag);
    QString _who_html(const Entry &e, const Agg *a);
    const relview::FiresMap &_fires_map();
    QString _elink(const Entry &x);
    QString _chain_html(Entry *e);
    QString _plink(long long pid, bool maybe = false);
    QString _provinces_html(const Entry &e, const Agg *a);
    Rgb _map_rgb(const Entry &e);
    void _legend();

    // for the map
    std::optional<MapValues> map_values();
    QString fill_key();
    bool working() const { return !_queue.empty() || _gen.has_value(); }   // (the checks: Python's _queue / _gen)

Q_SIGNALS:
    void changed();                          // what the map shows changed (the entry, the nation, the results)
    void catEdit(const eventpanel::CatChanges &changes, const QString &label);   // one undoable edit
    void provincePicked(long long pid);      // a province link: show it on the map
    void focusChanged();                     // the word at the editor's cursor changed (the map's marks follow)
    void toolRequested(const QString &key);  // "new_event" / "new_decision" / "blocks" / "network" …
    void litEdit(const QString &rel, const QString &uid, bool on);   // the "#literacy event" line above an entry

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QString _blocks_tip;
    OrderedMap<QString, Rgb> _colors;
    QStringList _order;
    QHash<QString, QSet<const Entry *>> _mention_cache;   // tag -> the entries whose text has it
    QHash<std::pair<QString, QString>, long long> _sym_hits;   // (rel, uid) -> how often the word at the cursor is there
    std::pair<const EventDB *, modifiers::ModifierDB::Uses> _mod_used{nullptr, {}};
    std::map<std::tuple<QString, qsizetype, qsizetype>, modifiers::Attrs> _mod_attr_cache;
    QSet<QString> _pending;                  // files typed in since the last reparse
    struct PopIndex {
        QHash<QString, OrderedMap<long long, long long>> cul, rlg;   // culture / religion -> {pid: people}
        OrderedMap<long long, long long> tot;                         // pid -> people
    };
    std::optional<PopIndex> _pops;
    const PopIndex &_pop_index();
    std::optional<relview::FiresMap> _fires;   // Entry -> [(event id, how)] (what it fires)
    scriptedit::ProblemsByRel _problems;      // rel -> [(position or None, "error" / "warn", text)] (the checks)
    std::map<std::pair<QString, QString>, std::vector<scriptedit::Problem>> _prob_entry;   // (rel, uid) -> its problems
    QTimer *_reparse_timer, *_timer;
    bool _stale = true;
    std::optional<WorldSig> _sig;
    std::vector<Entry *> _queue;
    qsizetype _total = 0;
    long long _agg_ids = 0;
    // the entry being worked out (Python: a generator): its scopes, how far, and what was found so far
    struct Gen {
        Entry *e;
        std::vector<events::Scope> scopes;
        size_t i = 0;
        bool paused = false;
        Agg a;
    };
    std::optional<Gen> _gen;
    std::unique_ptr<EventDB> _old_db;        // the one before the last reparse (what still points into it)
    int _sort_col = 2;
    bool _sort_desc = false;
    int _prob_i = -1;
    QHash<std::pair<QString, QString>, std::vector<QTreeWidgetItem *>> _items;   // (rel, uid or "") -> its rows
    QSet<std::pair<QString, QString>> _painted;
    QHash<std::pair<QString, QString>, std::pair<long long, Rgb>> _paint_state;
    std::pair<std::optional<std::tuple<QString, QStringList, std::vector<qint64>>>, QStringList> _pic_names;
    QHash<QString, std::optional<QPixmap>> _pic_cache;
    bool _step(Gen &g);                      // one more stretch of the generator: false when it is done
};

}  // namespace eventpanel
