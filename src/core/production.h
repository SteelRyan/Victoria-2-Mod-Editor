// production.py — common/production_types.txt — RGOs, artisans and factories: what each one needs (input goods,
// efficiency goods), what it makes (output good, amount) and what boosts it (bonus = { trigger value }). Around them:
// the goods' prices (common/goods.txt: cost), the buildings (common/buildings.txt: what building one costs,
// goods_cost) and what the pops buy (poptypes/*.txt: life_needs, everyday_needs, luxury_needs).
//
// ProdEdits (doc.maps["production"]) keeps changed / new / removed production types, changed prices, buildings and
// pop needs, and writes them in place on Apply (surgical edits, the rest of each file untouched). A new factory also
// gets its building in common/buildings.txt (a copy of another factory building), else it could never be built; a
// removed factory takes its building with it.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

namespace scriptstore {
class ScriptStore;
}

namespace production {

using OptStr = std::optional<QString>;
using Span = std::pair<qsizetype, qsizetype>;
using Goods = std::vector<std::pair<QString, double>>;   // [(good, amount)]

extern const QStringList KINDS;
extern const OrderedMap<QString, QString> KIND_NAME;
extern const std::vector<std::pair<QString, QString>> NEEDS;
extern const OrderedMap<QString, QString> BUILDING_KIND;
extern const double EFF_MISSING;             // MEASURED 2026-10-15: without its efficiency goods a factory makes 75 %

QString pretty(const OptStr &key);
Goods goods_block(const script::Nodes &nodes, const QString &key);
double num(const OptStr &v, double dflt = 0.0);
std::optional<double> num_or_none(const OptStr &v);   // num(v, None)

struct ProdEntry {
    Span span;
    QString text;
    bool tmpl = false;                       // (Python: e["template"]: no output_goods)
};
struct Template {
    OptStr type, workforce;
    Goods efficiency;
};
struct BuildingText {                        // (start, end, text, type)
    qsizetype start, end;
    QString text;
    OptStr type;
};
struct Cost {                                // (vstart, vend, value) or (None, insert-at, "0")
    std::optional<qsizetype> start;
    qsizetype end;
    QString value;
};
struct PopFile {
    QString path, text;
    bool in_mod = false;
};

class ProdDB {                               // the files' blocks
public:
    explicit ProdDB(moddata::Mod &mod);
    moddata::Mod &mod;
    QString path, bpath, gpath;
    OrderedMap<QString, ProdEntry> entries;
    OrderedMap<QString, Template> templates;
    QStringList order;
    OrderedMap<QString, QString> buildings;              // production type -> building key
    OrderedMap<QString, BuildingText> building_text;     // building key -> (start, end, text, type)
    QStringList bld_order;
    OrderedMap<QString, Cost> costs;                     // good -> its price's place in goods.txt
    OrderedMap<QString, PopFile> pop_files;
    void load();
    QStringList types() const;
};

struct Bonus {
    long long index;
    std::vector<std::pair<QString, QString>> conds;
    QString value;
};
struct Desc {                                // describe(): a production type's text, read
    QString kind;
    OptStr tmpl, output;
    double value = 0;
    Goods inputs, own_eff, efficiency;
    bool has_own_eff = false;
    std::vector<Bonus> bonuses;
    bool farm = false, mine = false, coastal = false;
    OptStr workforce;
};
Desc describe(const ProdDB &db, const QString &text, const std::function<QString(const QString &)> &tpl_text = nullptr);
struct BuildingDesc {
    QString type;
    Goods cost;
    OptStr production_type, time, max_level, extra;
};
BuildingDesc describe_building(const QString &text);
OrderedMap<QString, Goods> needs_of(const QString &text);
QString edit_needs(QString text, const QString &kind, const std::function<QString(const QString &)> &fn);
QString need_set(const QString &text, const QString &kind, const QString &good, const OptStr &amount);

struct ProdSnap {                            // ProdEdits' picture
    OrderedMap<QString, QString> want, added;
    QSet<QString> removed;
    OrderedMap<QString, std::pair<QString, QString>> new_buildings;
    OrderedMap<QString, QString> cost_want, bld_want, need_want;
    bool operator==(const ProdSnap &o) const = default;
};

class ProdEdits : public store::Store {      // doc.maps["production"]
public:
    static constexpr const char *kind = "production";
    ProdEdits(moddata::Mod &mod, ProdDB &db) : mod(mod), db(db) {}
    moddata::Mod &mod;
    ProdDB &db;
    scriptstore::ScriptStore *store = nullptr;   // the editor's script texts hold the pop type files
    OrderedMap<QString, QString> want, added;
    QSet<QString> removed;
    OrderedMap<QString, std::pair<QString, QString>> new_buildings;   // production key -> (building key, text)
    OrderedMap<QString, QString> cost_want, bld_want, need_want;

    QStringList keys() const;
    bool exists(const QString &key) const;
    QString text(const QString &key) const;
    Desc info(const QString &key) const;
    QStringList templates() const;
    std::pair<QStringList, QStringList> users_of(const QString &tpl) const;
    double eff_cost(const Desc &d, const std::function<double(const QString &)> &price) const;
    void set(const QString &key, const QString &text);
    void add(const QString &key, const QString &text, const std::optional<std::pair<QString, QString>> &building = std::nullopt);
    void remove(const QString &key);
    OptStr building_of(const QString &key) const;
    double cost(const QString &good) const;
    OrderedMap<QString, double> costs() const;
    QStringList buildings() const;
    QString bld_text(const QString &b) const;
    BuildingDesc bld_info(const QString &b) const;
    bool bld_is_new(const QString &b) const;
    QStringList poptypes() const;
    QString pop_text(const QString &pt) const;
    QString pop_rel(const QString &pt) const;
    OrderedMap<QString, Goods> needs(const QString &pt) const;
    void set_other(const QString &what, const QString &key, const QString &value);
    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    QString new_key(const QString &base) const;
    QString template_text(const QString &what, const QString &key, const QString &output) const;
    std::optional<std::pair<QString, QString>> building_text(const QString &prod_key) const;
    std::optional<QStringList> save(const QString &backup_root) override;
};

}  // namespace production
