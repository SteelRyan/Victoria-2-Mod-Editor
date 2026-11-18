// econest.py — Economy → Output estimate: what the mines, farms and factories turn out per day at the start.
//
// The page: the GOODS, one line each (what the world — or one nation — makes of it a day, who makes the most, what
// the factories use, what is left over, its worth); the picked good on a MAP (by nation: its share of the world; by
// province: where it is made) and as a list of the nations that make it, with their share. A close look below at
// what is picked.
//
// Worked out from the mod's own files (common/production_types.txt, goods prices, the pops and provinces of the
// scenario, with the edits pending in the editor):
//   a farm / mine (RGO)   output = value × workers ÷ workforce × (1 + throughput bonuses + efficiency)
//                                  × (1 + efficiency + output bonuses + the terrain's efficiency)
//   a factory             output = value × (craftsmen ÷ workforce × their effect_multiplier) × (1 + throughput)
//                                  × (1 + clerks' bonus + bonus for an input made in the state + output bonuses)
//   artisans              the expectation — spread evenly over the products open to the nation
// MEASURED against saves of the real game (Ferrum Mare, 1836.2.1); still an ESTIMATE.
//
// Port notes: a nation's technologies are walked in the order of Python's set of them (grid::PySet, seed 0), rebuilt
// from the order the history file named them (NationState.src) — the order the bonuses are added in. A nation whose
// technologies were edited in this session: those not in its file come after, by name (Python's own order there
// depends on the edits).
#pragma once
#include "core/ordered_map.h"
#include "ui/regionimport.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>
#include <QWidget>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QSplitter;
class QTreeWidget;
namespace document { class Document; }
namespace mapview { class MapData; }
namespace moddata { class Mod; }
namespace production { class ProdEdits; }
namespace script { struct Node; }

namespace econest {

using OptStr = std::optional<QString>;
using Amounts = OrderedMap<QString, double>;
using Rgb = std::tuple<int, int, int>;

extern const double GROWTH_ROOM;
double _num(const OptStr &v, double d = 0.0);

struct Employee {
    QString pop;
    double amount = 1.0;
    QString effect;
    double mult = 1.0;
};
struct Type {                                        // one production type, its template's lines filled in
    QString type;
    double workforce = 0.0;
    std::vector<Employee> employees;
    QString output;
    double value = 0.0;
    Amounts inputs;
    std::vector<std::pair<QString, double>> bonus;   // (good in the state, value)
    bool farm = false, mine = false;
    QString owner_pop;
};

class Types {                                        // common/production_types.txt as the editor has it now
public:
    explicit Types(production::ProdEdits &prods);
    OrderedMap<QString, Type> types;
    OrderedMap<QString, QString> rgo_of;             // good -> its farm / mine type
    static Type _read(const script::Node &n, OptStr &tpl);
};

extern const QStringList MODS, GOOD_MODS;
extern const QSet<QString> NOT_EFFECTS;

struct Mods {                                        // modifiers(): {modifier: sum, goods modifier: {good: sum}, _buildings}
    Amounts num;
    OrderedMap<QString, Amounts> goods;
    QSet<QString> buildings;
    bool any() const { return !num.isEmpty() || !goods.isEmpty() || !buildings.isEmpty(); }
};
Mods modifiers(const script::Node *node);

class Bonuses {                                      // what a nation's techs, inventions and national value give
public:
    explicit Bonuses(document::Document &doc);
    document::Document &doc;
    QHash<QString, Mods> tech;
    std::vector<std::tuple<QString, QStringList, Mods>> inv;   // (invention, techs it needs, what it gives)
    OrderedMap<QString, Mods> nv;
    struct Got {
        Mods m;
        long long techs = 0;
    };
    std::map<QString, Got> _of;
    const Got &of(const QString &tag);
    double get(const QString &tag, const QString &key, const OptStr &good = std::nullopt);
};

using Terrain = OrderedMap<long long, std::pair<QString, Amounts>>;   // province -> (terrain, its modifiers)
const Terrain &province_terrain(document::Document &doc, moddata::Mod &mod, mapview::MapData *md);

struct RgoRow {
    QString owner, state;
    long long pid = 0;
    QString province, good;
    long long workers = 0;
    QStringList kinds;
    long long size = 1;
    double base = 0, mult = 0, thr = 0, outb = 0;
    QString terrain;
    double out = 0, money = 0;
};
struct FacRow {
    QString owner, state;
    long long pid = 0;
    QString building;
    long long level = 1;
    QString good;
    Amounts places, got;
    double full = 0, cap = 0;
    Amounts need;
    QStringList local;
    double bonus = 0;
    Amounts idle;
    double avail = 1.0;
    QString shortg;                                  // (Python: r["short"])
    double out = 0;
    Amounts inputs;
    double money_out = 0, money_in = 0;
};
struct Part {                                        // (good, what they could make of it, {input: needed for that})
    QString good;
    double cap = 0;
    Amounts need;
};
struct ArtRow {
    QString owner, state;
    long long pid = 0;
    QString province;
    long long people = 0;
    std::vector<Part> parts;
    long long choices = 0;
    Amounts made, could, inputs;
    double money = 0;
};
struct Estimate {
    std::vector<RgoRow> rgo;
    std::vector<FacRow> factories;
    std::vector<ArtRow> artisans;
    QStringList notes;
    Amounts avail, demand;
};
Estimate estimate(document::Document &doc, moddata::Mod &mod, mapview::MapData *md = nullptr);

struct Tot {                                         // goods_totals: one good
    Amounts rgo, fac, art, use, art_use;             // nation -> a day
    OrderedMap<long long, double> prov;
    OrderedMap<long long, std::array<double, 4>> p;  // province -> [farms + mines, factories, artisans, used]
};
OrderedMap<QString, Tot> goods_totals(const Estimate &data);
extern const Tot NOTHING;

struct Level {
    QString key, title, plural;
};
extern const std::vector<Level> LEVELS;
extern const QStringList LEVEL_KEYS, MAKERS;
QString plural_of(const QString &level);

double made(const Tot &d, const QString &tag = QString());
double used(const Tot &d, const QString &tag = QString());

extern const Rgb SEA, EMPTY_LAND;
extern const double LOW;
Rgb shade(double v, double top);
QString fmt(double v, int digits = 1);
QString pct(double v);

class Legend : public QWidget {                      // the colour scale under the map: nothing … the most
public:
    explicit Legend(QWidget *parent = nullptr);
    QString mid, top;
    void set(const QString &mid, const QString &top);
    void paintEvent(QPaintEvent *e) override;
};

class BarDelegate : public QStyledItemDelegate {     // a share as a bar behind its number (UserRole + 2: 0 … 1)
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};

class _Row : public QTreeWidgetItem {                // sorted by the number behind the text
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override;
};

class _EconMap : public regionimport::SourceMap {    // also says when double-clicked / right-clicked in place
    Q_OBJECT
public:
    using regionimport::SourceMap::SourceMap;
    std::optional<QPointF> _rpress;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
Q_SIGNALS:
    void doubleClicked(int pid);
    void rightClicked();
};

using Unit = QString;                                // a continent / nation / "owner|state" / a province's number
using Pick = std::pair<QString, Unit>;               // (level, unit)
using Vals = std::array<double, 4>;

class EconWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit EconWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::optional<Estimate> data;
    std::optional<std::tuple<long long, size_t, const void *>> _sig;
    mapview::MapData *md = nullptr;
    OrderedMap<QString, Tot> tot;
    OptStr good;
    QString by = QStringLiteral("nation");
    OrderedMap<long long, QString> _own;
    OrderedMap<QString, std::vector<long long>> _prov_of;
    OrderedMap<long long, QString> _cont;
    std::map<QString, OrderedMap<Unit, std::vector<long long>>> _mem;
    QHash<QString, QString> _names;
    Amounts worth;
    std::optional<Pick> focus, _sel;
    QString group;
    std::vector<long long> _close_pids;              // (sorted)
    std::vector<long long> _lit_list;                // the list map->picked was last made from (Python's set order)
    std::vector<long long> _grp;                     // the map's edge ids

    QPushButton *chains_btn, *focus_x, *close_map, *close_x;
    std::map<QString, QPushButton *> by_btn, group_btn;
    QWidget *focus_bar, *close_box;
    QLabel *focus_lbl, *summary, *goods_head, *g_title, *hover, *rank_head, *rank_hint, *close_head, *close_text,
        *notes;
    QSplitter *split;
    QTreeWidget *goods, *rank;
    Legend *legend;
    _EconMap *map;

    QTreeWidget *_tree(const QStringList &heads, const std::vector<int> &wide);
    QString name_of(const QString &tag) const;
    QString good_name(const QString &g) const;
    void refresh(document::Document *doc);
    QString _tag() const;
    std::optional<std::vector<long long>> _fpids();  // in the order Python's set of them walks
    QString focus_name();
    void set_focus(const std::optional<Pick> &focus);
    std::optional<Unit> unit_of(long long pid, const QString &level);
    const OrderedMap<Unit, std::vector<long long>> &members(const QString &level);
    QString unit_name(const QString &level, const Unit &u);
    QString _unit_name(const QString &level, const Unit &u);
    QString belongs(const QString &level, const Unit &u);
    QString _belongs(const QString &level, const Unit &u);
    OrderedMap<Unit, Vals> by_unit(const Tot &d, const QString &level, const QSet<long long> *pids = nullptr);
    void set_nation(const QString &tag);
    void show_good(const QString &g);
    void _num(QTreeWidgetItem *it, int c, double v, const QString &text);
    void _fill();
    void _good_picked(QTreeWidgetItem *it);
    void _show_good();
    QTreeWidgetItem *_find_sel(const QString &lv);
    void _paint_map(const std::optional<QString> &light = std::nullopt);
    void _set_by(const QString &k);
    void _set_group(const QString &k, bool show = true);
    void _rank_double(QTreeWidgetItem *it);
    void jump_to(const QString &lv, const Unit &u);
    QString _line(const QString &level, const Unit &u);
    void _hover(int pid);
    std::vector<QTreeWidgetItem *> _items();
    void _map_picked();
    void _map_double(int pid);
    void _rank_picked(QTreeWidgetItem *it);
    void _light(QTreeWidgetItem *it);
    void detach();
    void _fill_close();
    void _set_picked(const std::vector<long long> &pids);   // map->picked = set(pids), Python's order kept
    std::optional<long long> _first_lit() const;     // next(iter(self.map.picked), None)

Q_SIGNALS:
    void selectRequested(const QList<long long> &pids, const QString &level);
    void chainsRequested();
};

}  // namespace econest
