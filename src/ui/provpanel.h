// provpanel.py — Provinces tab: owner / controller / cores, life rating, RGO, colonial, forts / naval bases / railroads
// and the state's factories (max 8 per state). The data half (state_components) is map\provpanel_data.
#pragma once
#include "core/document.h"
#include "core/moddata.h"
#include "map/provpanel_data.h"
#include "ui/dialogs.h"
#include "ui/panels.h"

#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace mapview {
class MapData;
}
namespace widgets {
class Section;
}

namespace provpanel {

using OptStr = std::optional<QString>;
using ProvFn = std::function<void(long long, history::ProvState &)>;   // fn(pid, state): one province's history

inline constexpr long long FACTORY_CAP = 8;
extern const QString MIXED;

OrderedMap<OptStr, long long> state_owners(document::Document &doc, const QString &rid);   // Counter of owners
std::vector<dialogs::PickItem> country_items(const moddata::Mod &mod);   // dynamic tags (D01–D50) last, own heading
std::vector<dialogs::PickItem> goods_items(moddata::Mod &mod);           // RGO goods, Farms / Mines

class ProvincePanel : public QWidget {
    // Emits edit(label, fn) where fn(pid, state) mutates one province's history state.
    Q_OBJECT
public:
    explicit ProvincePanel(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    QList<long long> pids;
    std::optional<long long> focus;
    const mapview::MapData *mapdata = nullptr;
    std::function<std::vector<std::int64_t>(long long)> pieces_of;   // pid -> sizes of its separate parts on the map
    bool _building = false;

    QLabel *title, *sub, *note, *name_where, *state_line, *state_warn, *lr_value, *status_note, *member_warn,
        *island_note, *fact_title, *fact_warn, *later;
    QLineEdit *name_edit, *state_name_edit;
    std::map<QString, OptStr> _name_keys;
    QPushButton *move_btn, *cap_btn;
    QCheckBox *cap_pops, *ctrl_follow, *owner_core, *island_cb;
    widgets::Section *own_section, *fact_section, *later_section;
    dialogs::PickCombo *owner, *controller, *add_core, *rgo, *add_fact;
    QWidget *cores_host, *fact_host;
    QGridLayout *cores_lay, *fact_lay;
    QComboBox *colonial;
    std::vector<std::pair<QString, QComboBox *>> member_boxes;     // climate, continent
    struct LevelRow {
        QPushButton *minus;
        QLabel *val;
        QPushButton *plus;
        QLabel *why;
        QLabel *name = nullptr;                    // (C++ only: shown / renamed with the mod's buildings)
    };
    std::vector<std::pair<QString, LevelRow>> level_rows;          // fort, naval_base, railroad, then the mod's others
    QGridLayout *level_grid = nullptr;
    void _add_level_row(const QString &key, const QString &text);
    void _sync_level_rows();                   // C++ only (2026-11-16): a row for each other province building
    std::optional<std::vector<std::pair<QString, QString>>> _csig;

    std::vector<history::ProvState> _states() const;
    dialogs::Data _common(OptStr history::ProvState::*field) const;   // the one value, or MIXED
    void _emit(const QString &label, const ProvFn &fn);
    static void _select(dialogs::PickCombo *combo, const dialogs::Data &key);
    void _set_owner();
    QList<long long> status_group() const;
    void _set_status(const QVariant &value);
    void _set_scalar(const QString &key, const OptStr &value, const QString &name);
    void _add_core(const OptStr &tag, bool owner = false);
    void _remove_core(const QString &tag);
    void _lr(long long d);
    void _level(const QString &key, long long d);
    long long _max_level(const QString &key) const;
    bool _naval_ok(long long pid) const;
    std::optional<OptStr> _state_rid() const;      // the one state of the selection (None inside: no state)
    void _add_factory(const OptStr &building);
    void _factory_level(long long pid, size_t idx, long long d);
    void _remove_factory(long long pid, size_t idx);
    void refresh(document::Document &doc, const QList<long long> &pids, const panels::Header &header,
                 std::optional<long long> focus = std::nullopt);
    void _fill_names();
    void _name_done(QLineEdit *ed, const QString &sec);
    void _fill_island();
    static QString pretty(const QString &name);
    void _fill_membership();
    void _fill_state_line();
    void _fill_cores(const OrderedMap<QString, long long> &cores);
    void _fill_factories();
    void _fill_later();

Q_SIGNALS:
    void edit(const QString &label, const provpanel::ProvFn &fn);
    void editFor(const QString &label, const provpanel::ProvFn &fn, const QList<long long> &pids);
    void membershipRequested(const QString &kind, const QString &name);   // kind ("climate"/"continent"), name
    void definitionsRequested(const QString &kind);                      // open the climate / continent editor
    void coresViewRequested(const QString &tag);
    void stateMoveRequested(const QString &kind);                        // "map" | "list" | "new" | "delete_…"
    void capitalRequested();
    void islandRequested(bool on, const QList<long long> &pids);         // mark / unmark provinces as having islands
    void nameRequested(const QString &key, const QString &text, const QString &section);   // ("prov" / "state")
};

}  // namespace provpanel
