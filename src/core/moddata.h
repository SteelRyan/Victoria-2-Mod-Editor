// moddata.py — Loading and saving Victoria 2 mod data (map definitions, regions, cultures, pops...).
#pragma once
#include "core/ordered_map.h"
#include "core/script.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace history {
class ProvinceHistory;
}
namespace countryhist {
class NationData;
class CountryHistory;
}
namespace oob {
class UnitTypes;
class SlotRules;
}
namespace submods {
struct Submod;
}

namespace moddata {

using Rgb = std::tuple<int, int, int>;
using OptStr = std::optional<QString>;

// The mod does not replace_path poptypes, so vanilla pop types remain valid.
extern const OrderedMap<QString, Rgb> VANILLA_POPTYPES;
extern const QStringList POPTYPE_ORDER;

QString text_hash(const QString &text);

struct Province {
    long long id = 0;
    Rgb rgb{0, 0, 0};
    QString name;
    bool sea = false;
    OptStr region;
    OptStr owner;
    OptStr controller;
    QStringList cores;
    OptStr life_rating;
    OptStr trade_goods;
    OptStr history_file;
    bool operator==(const Province &o) const = default;
};

// the name a deleted province's row keeps in map/definition.csv (the game needs its numbers without a gap)
extern const QString FREE_NAME;

struct Region {
    QString id;
    QString name;
    std::vector<long long> provinces;     // order as in region.txt; first = state capital
};

struct Country {
    QString tag, name;
    Rgb color;
    bool dynamic = false;
    QString file;
    bool is_new = false;                  // c["new"]: made in the editor, not written yet (countryhist.NewNations)
    QString template_;                    // c["template"] of such a nation
};
struct Culture {
    QString id, group, name;
    Rgb color;
};
struct Religion {
    QString id, group, name;
    Rgb color;
    std::optional<long long> icon;
    bool pagan = false;
};
struct PopType {
    QString id, name;
    Rgb color;
    QString source;                       // "vanilla" or "mod"
};
struct Good {
    QString id, group, name;
    Rgb color;
};
struct Building {
    QString id, type;
    long long max_level = 1;
    bool one_per_state = false, port = false;
    bool province = false;                      // province = yes: a level in each province (fort, railroad, …)
    OptStr production_type;
    QString name;
};
struct Production {
    QString type;
    OptStr template_, output;
    bool coastal = false, farm = false, mine = false;
};
struct Unlock {
    long long year = 0;
    double cost = 0;
    QString by, kind;
    OptStr missing;
};
struct FactoryInfo {
    OptStr output;
    QString output_name;
    OptStr group;
    QString group_name;
    long long group_order = 99;
    bool coastal = false;
};
struct ModFileValues {                    // _mod_file_values: replace_path, dependencies, every other key = value
    QStringList replace_path, dependencies;
    OrderedMap<QString, QString> keys;
};
struct ModEntry {                         // discover_mods: one mod of the folder
    QString name;
    OptStr folder, modfile;
    bool ok = false;
    QString note;
    QStringList replace, deps;
    OptStr user_dir;
};
struct ScenarioInfo {
    QString name;
    long long files = 0;
    bool isDate = false;
    OptStr bookmark;
};
struct LayeredFile {
    QString rel, path, mine;
    bool own = false;
};
using HistoryEntry = std::pair<QString, script::Nodes>;   // (rel, nodes)

bool is_mod_folder(const QString &path);
bool is_game_folder(const QString &path);
ModFileValues _mod_file_values(const QString &path);
OptStr find_base_game(const QString &mods_dir);
std::pair<std::vector<ModEntry>, OptStr> discover_mods(const QString &mods_dir,
                                                      const OptStr &base_game = std::nullopt);   // (C++ only: a base game of its own)
void _mark_submods(std::vector<ModEntry> &mods);
ModEntry _describe(const QString &name, const OptStr &folder, const OptStr &modfile, const OptStr &base);

// mod.pending_script: in the editor, what a dialog used to write at once (a new culture / religion) becomes a
// pending change of the document (one undo step, written on Apply) — document::PendingScripts
struct PendingScript {
    virtual ~PendingScript() = default;
    virtual QString text(const QString &rel) = 0;
    virtual void put(const QString &label, const QString &rel, const QString &text,
                     const std::vector<std::pair<QString, QString>> &names, const QString &section) = 0;
    virtual void names(const QString &label, const OrderedMap<QString, QString> &names, const QString &section) = 0;
    virtual void sync() = 0;                          // the lists follow the editor's texts (Mod.sync_cultures)
};

class Mod {
public:
    Mod(const QString &root, const OptStr &base = std::nullopt, const OptStr &name = std::nullopt,
        const QStringList &replace = {});

    QString root;
    OptStr base;
    OptStr display_name;
    QStringList replace;                  // replace_path entries of the .mod file
    QStringList warnings;

    bool replaced(const QStringList &parts) const;
    QString p(const QStringList &parts) const;
    std::vector<LayeredFile> layered(const QStringList &parts) const;
    bool in_mod(const QString &path) const;
    QString adopt(const QString &path);
    QString pops_dir() const;
    QString name() const;

    void load();
    // what load() fills (Python attributes)
    OrderedMap<QString, QString> loc;
    OrderedMap<long long, Province> provinces;
    OrderedMap<long long, Rgb> free_slots;
    // newprov's Apply: deleted province -> its heir (nullopt: none), for provrenumber right after (Python: free_heirs)
    OrderedMap<long long, std::optional<long long>> free_heirs;
    QString provinces_bmp = QStringLiteral("provinces.bmp");
    QString region_hash;
    OrderedMap<QString, Region> regions, utility;
    QStringList region_order;
    QSet<QString> region_keys_on_disk;
    OrderedMap<QString, Country> countries;
    OrderedMap<QString, Culture> cultures;
    QStringList culture_groups;
    OrderedMap<QString, Religion> religions;
    QStringList religion_groups;
    std::optional<QStringList> _cult_sig;   // importer.sync_cultures: the texts the lists were last read from
    long long cult_version = 0;
    OrderedMap<QString, PopType> poptypes;
    OrderedMap<long long, std::vector<HistoryEntry>> history;
    OrderedMap<QString, QString> bookmarks;
    bool has_bookmarks = false;           // (Python: hasattr(self, "bookmarks"))
    OrderedMap<QString, Building> buildings;
    OrderedMap<QString, Good> goods;
    QStringList goods_groups;
    OrderedMap<QString, Production> production;
    std::shared_ptr<history::ProvinceHistory> hist;     // (Python: mod.hist, made by the first Document)
    // nations: the game data and the history files, shared by every scenario of the mod (made by the window)
    std::shared_ptr<countryhist::NationData> nation_data;
    std::shared_ptr<countryhist::CountryHistory> country_hist;
    // events.EventDB's caches (Python: mod._event_history_sets / mod._event_flag_holders), read once per mod
    std::shared_ptr<void> event_history_sets, event_flag_holders;
    std::optional<QStringList> modifier_attrs;          // (Python: mod._modifier_attrs, modifiers.ModifierDB)
    std::shared_ptr<oob::UnitTypes> unit_types;         // (Python: mod.unit_types / mod.slot_rules, oob.OOBEdits)
    std::shared_ptr<oob::SlotRules> slot_rules;
    // refs.loc_text: the localisation by lower-case key (Python: mod._loc_lower = (len(loc), {…}))
    std::optional<std::pair<qsizetype, QHash<QString, QString>>> loc_lower;
    PendingScript *pending_script = nullptr;            // (Python: getattr(mod, "pending_script", None))
    std::shared_ptr<submods::Submod> submod;
    std::shared_ptr<void> religion_icons;              // (Python: mod._religion_icons, ddsimage — QList<QPixmap>)            // (Python: mod.submod — the mod is a submod's working copy)

    void _load_buildings();
    void buildings_from(const QString &text);   // C++ only (2026-11-16): buildings.txt's text as the editor has it
    FactoryInfo factory_info(const QString &key);
    QStringList factory_types();
    const OrderedMap<QString, Unlock> &unlocks();
    void _load_production();
    OrderedMap<QString, QString> rgo_goods() const;
    void _load_goods();
    std::vector<std::pair<QString, bool>> loc_files() const;
    OrderedMap<QString, QString> _load_localisation();
    void _load_definitions();
    void _load_default_map();
    void _load_regions();
    QString _utility_key() const;
    QStringList utility_marks() const;
    void set_utility(const QString &rid, bool on);
    void save_utility();
    const Region *any_region(const QString &rid) const;
    std::vector<std::pair<QString, QString>> utility_suggestions() const;
    void _load_countries();
    void _load_cultures(const std::optional<QString> &text = std::nullopt);
    void _load_religions(const std::optional<QString> &text = std::nullopt);
    void _load_poptypes();
    std::pair<long long, QString> poptype_sort_key(const QString &t) const;
    void _load_history();
    void _load_bookmarks();
    void apply_history(const QString &scenario);
    std::vector<ScenarioInfo> scenarios();
    QStringList pop_scenarios() const;
    OrderedMap<QString, QString> pop_files(const QString &scenario) const;
    QString scenario_dir(const QString &scenario) const;
    void create_scenario(const QString &name, const OptStr &copy_from = std::nullopt);

private:
    template <typename F>
    void _safe(const QString &what, F fn);
    std::optional<OrderedMap<QString, Unlock>> _unlocks;
};

// ---------------------------------------------------------------------- pop files
struct PopOrigin {                        // pop["o"]: where a pop stands on disk, as it was read
    QString file;
    std::pair<qsizetype, qsizetype> span;
    std::optional<std::pair<qsizetype, qsizetype>> size_span;
    long long line = 0;
    QString type;
    OptStr culture, religion;
    long long size = 0;
    std::vector<std::pair<OptStr, QString>> extra;
    bool operator==(const PopOrigin &o) const = default;
};
struct Pop {
    QString type;
    OptStr culture, religion;
    long long size = 0;
    std::vector<std::pair<OptStr, QString>> extra;     // (key or None, text)
    std::shared_ptr<const PopOrigin> o;               // shared: Python keeps the same dict (identity matters)
    bool operator==(const Pop &b) const
    {
        // (as Python's dict ==: the origin compared by what it says, not by being the same object)
        return type == b.type && culture == b.culture && religion == b.religion && size == b.size &&
               extra == b.extra && (o == b.o || (o && b.o && *o == *b.o));
    }
};
struct PopProvince {
    QString file;
    std::pair<qsizetype, qsizetype> block;
    qsizetype open = 0, close = 0;
    std::vector<Pop> pops;
    QStringList files;
    long long line = 0;
    bool other = false;
};
struct PopLoad {
    OrderedMap<long long, PopProvince> provinces;
    OrderedMap<QString, QString> files;               // filename -> sha1 of its text
    QStringList problems;
};

PopLoad load_pop_files(Mod &mod, const QString &scenario);
QString _render_pop(const Pop &pop, const QString &ind, const QString &ind2, const QString &nl);
std::optional<QString> _line_indent(const QString &text, qsizetype pos);
std::pair<QString, QString> _file_style(const QString &text);
std::pair<qsizetype, qsizetype> _delete_span(const QString &text, qsizetype s, qsizetype e);
std::pair<QStringList, OptStr> save_pops(Mod &mod, const QString &scenario,
                                         const OrderedMap<long long, PopProvince> &provinces_src,
                                         const OrderedMap<QString, QString> &file_hashes,
                                         const OrderedMap<long long, std::vector<Pop>> &current,
                                         const std::vector<long long> &dirty,
                                         const OrderedMap<long long, QString> &target_files,
                                         const QString &backup_root);
QStringList strip_pop_blocks(Mod &mod, const QSet<long long> &pids, const QString &backup_root);
OptStr save_regions(Mod &mod, const OrderedMap<QString, std::vector<long long>> &changed, const QString &backup_root);

}  // namespace moddata
