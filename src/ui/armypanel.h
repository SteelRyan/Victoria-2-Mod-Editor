// armypanel.py — Armies tab: a nation's armies and navies at the start (history/units, see oob.py).
//
// Map: each army is a shield at its location; its brigades are discs at the provinces they come from (a number when
// one province raises several); gold rings (+n) are states whose soldiers can raise n more brigades — click one to
// add a brigade from there to the selected army; click one of the selected army's discs to take a brigade away.
// Soldiers of a state count together (common/defines.lua decides how many a brigade needs).
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/ordered_map.h"
#include "map/mapview_data.h"
#include "ui/mapview.h"
#include "ui/widgets.h"

#include <QSet>
#include <QVariantList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace dialogs {
class PickCombo;
}
namespace leaderlist {
class LeaderList;
}
namespace leaderbox {
class LeaderBox;
}

namespace armypanel {

using OptStr = std::optional<QString>;
using OptInt = std::optional<long long>;
using Rgb = std::tuple<int, int, int>;
using EditFn = std::function<void(oob::OOB &)>;

extern const Rgb GOLD_RING;
extern const std::vector<std::pair<QString, QString>> SHIP_SCHEMES;
extern const QString SHIP_HELP;
Rgb army_color(long long i);
Rgb dim(const Rgb &rgb, double k = 0.45);

class ArmyPanel : public QWidget {
    Q_OBJECT
public:
    ArmyPanel(moddata::Mod &mod, mapview::MapData &mapdata, QWidget *parent = nullptr);
    moddata::Mod &mod;
    mapview::MapData &mapdata;
    document::Document *doc = nullptr;
    oob::OOBEdits *oob = nullptr;
    OptStr tag;
    QString sub = QStringLiteral("armies");
    std::map<QString, OptInt> sel;                 // tag -> selected army index
    std::map<QString, OptInt> nsel;                // tag -> selected navy index
    oob::Slots _slots;
    OrderedMap<long long, long long> _pcap, _used;
    QSet<std::pair<long long, long long>> _over;
    struct RowsSig {
        OptStr tag;
        long long ai;
        std::vector<std::tuple<QString, QString, OptInt>> regs;
        std::vector<long long> over;
        bool operator==(const RowsSig &o) const = default;
    };
    std::optional<RowsSig> _rows_sig;
    struct ShipsSig {
        OptStr tag;
        long long ni;
        std::vector<std::pair<QString, QString>> ships;
        bool operator==(const ShipsSig &o) const = default;
    };
    std::optional<ShipsSig> _nrows_sig;
    struct CompSig {
        OptStr tag;
        long long ai;
        QStringList types;
        bool operator==(const CompSig &o) const = default;
    };
    std::optional<CompSig> _comp_sig;
    std::optional<long long> _psig;
    struct CompRow {
        QComboBox *cb;
        widgets::Stepper *st;
        QPushButton *x;
    };
    std::vector<CompRow> comp_rows;
    std::function<void()> after_set_sub;           // (the main window's: set_sub wrapped to sync the rail)

    std::vector<std::pair<QString, QPushButton *>> seg;
    dialogs::PickCombo *picker;
    QLabel *title, *summary, *file_note, *rule_note;
    widgets::Section *nation_section;
    QWidget *armies_box, *navies_box;
    QListWidget *army_list, *navy_list;
    QPushButton *new_army_btn, *del_army_btn, *comp_add, *comp_btn, *army_move, *rename_btn;
    widgets::Section *comp_section, *army_section, *name_section, *navy_section, *ship_name_section;
    QLabel *comp_title, *comp_total, *army_title, *army_hint, *pattern_help, *navy_title, *ship_help;
    QWidget *comp_host, *brig_host, *ship_host;
    QGridLayout *comp_grid, *brig_grid, *ship_grid;
    QCheckBox *sort_types;
    QLineEdit *army_name, *pattern, *navy_name, *ship_pattern;
    widgets::ElideLabel *army_loc, *navy_loc;
    widgets::WrapButton *near_btn;
    QComboBox *scheme, *add_type, *ship_scheme;
    leaderbox::LeaderBox *army_leader, *navy_leader;
    leaderlist::LeaderList *leaders;               // (C++ only) the Generals / Admirals sub-pages
    QPushButton *new_navy_btn, *del_navy_btn, *navy_move, *add_ship_btn, *ship_rename_btn;
    widgets::Stepper *add_count;

    // --- helpers
    oob::UnitTypes *units() const { return oob ? oob->units : nullptr; }
    const oob::OOB *st() const;
    QString pname(const OptInt &pid) const;
    std::optional<QPointF> center(const OptInt &pid) const;
    std::pair<OptInt, const oob::Army *> army() const;
    std::pair<OptInt, const oob::Navy *> navy() const;
    OptInt sel_of() const;
    OptInt nsel_of() const;
    void _emit(const QString &label, const EditFn &fn);
    void _set_extra(const QString &kind, OptInt i, const QString &what, const QStringList &extra);
    void set_sub(const QString &key, bool emit = true);
    void _scheme_shown();
    // --- refresh
    void refresh(document::Document *doc, const OptStr &tag);
    void _fill();
    QString _army_line(const oob::Army &a) const;
    void _fill_armies(const oob::OOB &st);
    void _clear(QGridLayout *grid);
    void _fill_brigades(long long ai, const oob::Army &a);
    void _fill_composition(long long ai, const oob::Army &a);
    void _comp_row(OptStr typ, long long n);
    void _comp_drop(QPushButton *x);
    OrderedMap<QString, long long> _comp_want() const;
    void _comp_total();
    // --- army list / fields
    void _army_row(int row);
    void select_army(long long i);
    void new_army();
    void delete_army();
    void _army_renamed();
    void set_location(long long pid);
    // --- brigades
    QString _new_name(const std::vector<oob::Regiment> &regs, const QString &typ, long long ai) const;
    void attach(long long home);
    bool detach(long long pid);
    void attach_nearest();
    void right_clicked(long long pid);
    bool deselect();
    void _reg_renamed(long long k, const QString &text);
    void _reg_type(long long k, const QString &typ);
    void _reg_remove(long long k);
    void apply_composition();
    QStringList _scheme_names(long long ai, const std::vector<oob::Regiment> &regs) const;
    void rename_brigades();
    // --- navies
    QString _navy_line(const oob::Navy &n) const;
    void _fill_navies(const oob::OOB &st);
    void _fill_ships(long long ni, const oob::Navy &n);
    void _navy_row(int row);
    void select_navy(long long i);
    void new_navy();
    void delete_navy();
    void _navy_renamed();
    QStringList _ship_names(long long ni, const std::vector<oob::Ship> &ships, long long only_new = 0) const;
    void add_ships();
    void _ship_renamed(long long k, const QString &text);
    void _ship_type(long long k, const QString &typ);
    void _ship_remove(long long k);
    void rename_ships();
    // --- map
    std::vector<mapview::Shape> shapes() const;
    std::vector<mapview::Shape> _army_shapes(const oob::OOB &st) const;
    std::vector<mapview::Shape> _navy_shapes(const oob::OOB &st) const;
    void map_clicked(const QVariantList &tag);
    QString describe(const QVariantList &tag) const;
Q_SIGNALS:
    void edit(const QString &label, const QString &tag, const armypanel::EditFn &fn);   // undo label, tag, fn(oob state)
    void nationPicked(const QString &tag);
    void moveRequested(const QString &kind);       // "army" / "navy": the next map click places it
    void overlayChanged();
    void message(const QString &text);
};

}  // namespace armypanel
