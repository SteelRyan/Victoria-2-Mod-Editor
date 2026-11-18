// panels.py — Pops tab side panels: the pop editor and the split allocator.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "ui/widgets.h"

#include <QStringList>
#include <QTreeWidgetItem>
#include <QWidget>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace dialogs {
class CultureCombo;
class ReligionCombo;
class PickCombo;
}  // namespace dialogs

namespace panels {

using OptStr = std::optional<QString>;
using Mix = OrderedMap<OptStr, long long>;            // {key: people} (None: pops without a culture / religion)

extern const std::vector<double> STEPS;
extern const QStringList DEFAULT_ELITE;
QString pct_label(double v);

// A chip's key is a text; Python's chips for pops without a culture / religion have the key None. Here that is
// NONE_KEY (a text no mod can have).
extern const QString NONE_KEY;
QString chip_key(const OptStr &k);
OptStr from_chip(const QString &k);
QSet<OptStr> from_chips(const QSet<QString> &keys);

struct Header {                                        // header(): (title, sub_html, info)
    QString title, sub, info;
};

// An unapplied change on the Pops page (C++ only, the user 2026-11-13): the ± buttons, a typed total and the mix all
// wait here as one preview until "Apply changes" (one undo step). It stays with the provinces / filter / mix kind it
// was made for: picking others leaves it waiting (the window's bar: Apply / Discard / Go back), coming back shows it.
struct PendSnap {
    std::optional<long long> total;               // the filtered pops' new total (None: unchanged)
    std::vector<widgets::Segment> segs;           // the mix as shares
    std::vector<OptStr> added;                    // keys added with "＋ Add…"
    bool mix_moved = false;                       // the shares differ from the pops' own
};
struct Pending {
    QList<long long> pids;
    QSet<QString> un_types, un_cultures, un_religions;   // the filter (unticked chips)
    QString field;
    long long base_total = 0;                     // the filtered pops' total when it began
    PendSnap now;
    std::vector<PendSnap> undo, redo;             // Ctrl+Z / Ctrl+Y inside the preview, one change at a time
    const void *doc_top = nullptr;                // the document's newest step when the preview last changed
    const void *doc_redo_top = nullptr;           // … its newest redo step when the preview last went back
};

class EditPanel : public QWidget {
    Q_OBJECT
public:
    EditPanel(moddata::Mod &mod, const QString &backup_root, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *_doc = nullptr;
    QList<long long> _pids;
    QStringList elite_types;
    bool can_paste = false;
    QLabel *title, *sub, *info;
    widgets::ChipGroup *types, *cultures, *religions;
    QLabel *step_label, *of_total;
    QLineEdit *total_edit;
    std::vector<QPushButton *> step_buttons;
    OrderedMap<QString, QPushButton *> mix_btns;
    QString mix_field = QStringLiteral("culture");
    widgets::Segs mix_segments = std::make_shared<std::vector<widgets::Segment>>();
    Mix mix_current, mix_all;
    widgets::PieChart *pie;
    widgets::SplitBar *mix_bar;
    widgets::ZoomBar *mix_zoom;
    std::vector<OptStr> mix_added;                     // keys added with "＋ Add…" (shown last, just left of 12 o'clock)
    QLabel *mix_ring_note, *mix_preview;
    QWidget *mix_legend_host;
    QGridLayout *mix_legend;
    std::vector<std::pair<QDoubleSpinBox *, QLabel *>> mix_rows;
    dialogs::CultureCombo *mix_add_culture;
    dialogs::ReligionCombo *mix_add_religion;
    dialogs::PickCombo *mix_add_type;
    QPushButton *cult_btn, *rel_btn, *mix_reset, *mix_apply;
    OrderedMap<QString, QPushButton *> tool_btns;
    widgets::Section *state_section;
    QLabel *cap_label;
    QPushButton *cap_btn, *cons_btn, *cons_cfg, *split_btn;
    QTreeWidget *tree;

    void set_elite_types(const QStringList &types);
    void _update_cons_text();
    void _configure_elite();
    void _toggle_elite(const QString &t, bool on);
    void _filter_changed();
    void _total_entered();
    document::PopFilter current_filter() const;
    // pids: selected province ids; header: (title, sub_html, info); states: list of fully selected region ids
    void refresh(document::Document &doc, const QList<long long> &pids, const Header &header, const QStringList &states,
                 std::optional<long long> split_source = std::nullopt);
    void set_mix_field(const QString &field);
    std::pair<QString, widgets::Rgb> _meta(const OptStr &key) const;
    void _rebuild_mix();
    std::vector<OptStr> _order(const std::vector<OptStr> &keys) const;
    void _set_all_segments();
    void _update_zoom();
    void _add_segment(dialogs::PickCombo *combo);
    void _mix_changed(QWidget *source);
    void _set_pct(int i, double value);
    Mix mix_targets() const;
    void _update_mix_ui(bool rebuild_legend = true);
    void _apply_mix();
    void _update_chip_counts(document::Document &doc, const QList<long long> &pids);
    void _update_totals(document::Document &doc, const QList<long long> &pids);
    void refresh_filter_only(document::Document &doc, const QList<long long> &pids);
    OptStr _field_of(const moddata::Pop &p) const;     // p[self.mix_field]
    // --- the unapplied change (C++ only)
    std::optional<Pending> pend;
    std::optional<long long> pend_total;          // the preview's total while it is shown
    PendSnap _last;                               // the preview as it stood after the latest change
    qint64 _last_mix_ms = -1;                     // (mix drags close together are one step)
    QLabel *pend_note;
    bool pend_shown() const;                      // the preview belongs to what the page shows now
    bool pending() const { return pend.has_value(); }
    bool pending_live() const;                    // it changes something (the bar / Apply count only these)
    PendSnap _snap() const;
    bool _begin(bool mix_drag);                   // before a change: false = refused (another one waits elsewhere)
    void _store();                                // after a change: the preview keeps it (or ends, nothing left)
    void _show_snap(const PendSnap &sn);
    void _step_total(double pct);
    void _after_mix_edit();
    void _show_total();
    long long _cur_total() const;                 // the filtered pops' total as the pops are now
    long long _new_total() const { return pend_total.value_or(_cur_total()); }
    void apply_pending();                         // "Apply changes" (also from the window's bar)
    void discard_pending();
    bool pending_undo();                          // Ctrl+Z / Ctrl+Y: the preview's own steps first
    bool pending_redo();
    QString pending_text() const;                 // for the window's bar
Q_SIGNALS:
    void scaleRequested(double pct);                   // percent
    void totalRequested(long long total);              // exact new total
    void filterChanged();
    void splitRequested();
    void consolidateRequested();
    void changeCapitalRequested();
    void eliteTypesChanged(const QStringList &types);
    void toolRequested(const QString &key);            // copy | paste | add
    void mixRequested(const QString &field, const panels::Mix &targets);   // field, {key: people}
    // the one Apply: a new total and / or the mix as shares, for these provinces and this filter
    void changesRequested(const QList<long long> &pids, const document::PopFilter &flt,
                          std::optional<long long> total, const QString &field,
                          const std::vector<std::pair<OptStr, double>> &shares);
    void pendingChanged();                        // begun, changed, shown, left behind, applied or discarded
    void pendingRefused();                        // a change while another one waits elsewhere
    void culturesRequested();                          // open the culture groups editor
    void religionsRequested();                         // open the religion groups editor
    void cultureGroupsRequested();                     // open the culture groups' own page
};

class SortItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override;
};

class SplitPanel : public QWidget {
    Q_OBJECT
public:
    explicit SplitPanel(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::optional<long long> source;
    std::optional<document::PopFilter> flt;
    widgets::Segs targets = std::make_shared<std::vector<widgets::Segment>>();   // pid, pct, locked, color, label
    QString mode = QStringLiteral("even");
    long long _color_i = 0;
    QString filter_desc;
    const std::vector<std::int64_t> *mod_area = nullptr;   // (Python: the class's mod_area = [], set to mapdata.area)
    QLabel *title, *src_label, *moving, *warn;
    QLineEdit *id_edit;
    QCheckBox *keep_cb, *ph_cb;
    QSpinBox *ph_spin;
    widgets::SplitBar *bar;
    OrderedMap<QString, QPushButton *> preset_btns;
    QWidget *rows_host;
    QGridLayout *rows;
    std::vector<std::pair<QDoubleSpinBox *, QLabel *>> row_widgets;
    QPushButton *apply_btn;

    void start(document::Document &doc, long long source, const document::PopFilter &flt, const QString &filter_desc);
    long long moving_total() const;
    std::vector<long long> target_pids() const;
    std::vector<std::pair<long long, double>> weights() const;
    void toggle_target(long long pid);
    widgets::Rgb _next_color();
    void _add(long long pid);
    void _remove(long long pid);
    void _keep_toggled(bool on);
    void _restructured();
    void _add_typed();
    std::vector<widgets::Segment *> _unlocked(const widgets::Segment *exclude = nullptr);
    void _take(double amount, const widgets::Segment *exclude);
    void _give(double amount, const widgets::Segment *exclude = nullptr);
    void _normalize();
    void apply_preset(const QString &key);
    void _set_manual();
    void _bar_changed();
    void _pct_edited(widgets::Segment *t, double value);
    void _lock(widgets::Segment *t, bool on);
    void _after_change(bool rebuild = true);
    void _build_rows();
    void _sync_rows();
    void _update_preview();
Q_SIGNALS:
    void applyRequested();
    void cancelRequested();
    void targetsChanged();
};

}  // namespace panels
