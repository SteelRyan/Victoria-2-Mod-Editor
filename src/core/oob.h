// oob.py — history/units/*.txt — orders of battle: armies (regiments raised from soldier pops), navies (ships) and
// the starting relations / influence with other nations (`TAG = { value = … level = … influence_value = … }`).
//
// Everything is edited in place: an army, navy or relation that wasn't changed keeps its exact text (comments
// included), a changed one is written again in the mod's own style, new ones go next to the others of their kind.
// A nation without an OOB file gets `history/units/TAG_oob.txt` and an `oob = "TAG_oob.txt"` line in its history.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace countryhist {
class CountryHistory;
}
namespace document {
class Document;
}

namespace oob {

using OptStr = std::optional<QString>;
using OptInt = std::optional<long long>;
using Span = std::pair<qsizetype, qsizetype>;
using Date = history::Date;

extern const QStringList LEVELS;             // influence level 0 … 5
extern const std::vector<std::tuple<int, int, int>> ARMY_COLORS;
QString ordinal(long long n);

// ---------------------------------------------------------------------------------------------- unit types
struct UnitType {
    bool naval = false;
    QString unit_type, name;
};
class UnitTypes {                            // units/*.txt: key -> {naval, unit_type, name}
public:
    explicit UnitTypes(moddata::Mod &mod);
    moddata::Mod &mod;
    OrderedMap<QString, UnitType> types;
    static const QHash<QString, long long> ORDER;
    QStringList land() const;
    QStringList naval() const;
    QString name(const OptStr &key) const;
    QString word(const OptStr &key) const;   // the word regiment names use ('Artillery' for artillery_offensive)
};

// ---------------------------------------------------------------------------------------------- brigade slots
struct Num {                                 // int(val) if float(val).is_integer() else val
    double v = 0;
    bool is_int = true;
};
class SlotRules {                            // how many standing brigades a soldier pop can raise (the game's cap)
public:
    explicit SlotRules(const moddata::Mod &mod);
    Num first, per, noncore, colony, protectorate;
    QStringList missing;
    Num multiplier(long long colonial, bool is_core) const;
    long long brigades(long long soldiers, const Num &multiplier = Num{1, true}) const;
};

// ---------------------------------------------------------------------------------------------- one OOB file
struct Regiment {
    QString name, type;
    OptInt home;
    bool operator==(const Regiment &o) const = default;
};
struct Ship {
    QString name, type;
    bool operator==(const Ship &o) const = default;
};
struct ArmyKey {
    QString name;
    OptInt location;
    std::vector<std::tuple<QString, QString, OptInt>> regs;
    QStringList extra;
    bool operator==(const ArmyKey &o) const = default;
};
struct NavyKey {
    QString name;
    OptInt location;
    std::vector<std::pair<QString, QString>> ships;
    QStringList extra;
    bool operator==(const NavyKey &o) const = default;
};
using RelKey = std::tuple<OptStr, OptStr, OptStr>;
// (C++ only, the tester 2026-11-17) a general / admiral of the nation's pool: a top-level `leader = { … }` block.
// `fields` = its plain lines in the file's order (name, date, type, personality, background, prestige …), `extra` =
// anything else in it (a picture line, comments), kept as text.
using LeaderFields = std::vector<std::pair<QString, QString>>;
struct LeaderKey {
    LeaderFields fields;
    QStringList extra;
    bool operator==(const LeaderKey &o) const = default;
};
struct Leader {
    LeaderFields fields;
    QStringList extra;
    std::optional<Span> src;
    std::optional<LeaderKey> orig;
    bool operator==(const Leader &o) const = default;
    QString get(const QString &k, const QString &dflt = QString()) const;
    void set(const QString &k, const QString &v);    // (a new key goes in the written order)
};
extern const QStringList LEADER_KEYS;        // name date type personality background prestige: the written order
struct Army {
    QString name;
    OptInt location;
    std::vector<Regiment> regiments;
    QStringList extra;
    std::optional<Span> src;
    std::optional<ArmyKey> orig;
    bool operator==(const Army &o) const = default;
};
struct Navy {
    QString name;
    OptInt location;
    std::vector<Ship> ships;
    QStringList extra;
    std::optional<Span> src;
    std::optional<NavyKey> orig;
    bool operator==(const Navy &o) const = default;
};
struct Relation {
    OptStr value, level, influence_value;
    std::optional<Span> src;
    std::optional<RelKey> orig;
    bool operator==(const Relation &o) const = default;
};
struct OOB {
    std::vector<Army> armies;
    std::vector<Navy> navies;
    OrderedMap<QString, Relation> relations;
    std::vector<Leader> leaders;             // (C++ only) the pool's generals and admirals
    bool operator==(const OOB &o) const = default;
};
OOB empty_oob();
inline OOB clone_oob(const OOB &o) { return o; }
ArmyKey _army_key(const Army &a);
NavyKey _navy_key(const Navy &n);
LeaderKey _leader_key(const Leader &l);
RelKey _rel_key(const Relation &r);
struct OOBKey {
    std::vector<ArmyKey> armies;
    std::vector<NavyKey> navies;
    std::vector<std::pair<QString, RelKey>> relations;
    std::vector<LeaderKey> leaders;
    bool operator==(const OOBKey &o) const = default;
};
OOBKey oob_key(const OOB &o);
QStringList _comments(const QString &text, const script::Node &n);
OOB parse_oob(const QString &text, const QSet<QString> &tags);
QString army_text(const Army &a, const QString &nl);
QString navy_text(const Navy &n, const QString &nl);
QString relation_text(const QString &tag, const Relation &r, const QString &nl);
QString leader_block_text(const Leader &l, const QString &nl);
QString write_oob(QString text, const OOB &want);

// ---------------------------------------------------------------------------------------------- all OOBs
struct Centers {                             // what new_homes / fix_units need of MapData: md.cx, md.cy, md.n
    std::function<double(long long)> cx, cy;
    long long n = 0;
};

struct StateSlots {                          // one state of a nation: its soldiers and brigade caps
    long long soldiers = 0;
    std::vector<std::pair<long long, long long>> pid_list;   // (soldiers, pid) while being built
    QList<long long> pids;                                   // most soldiers first
    OrderedMap<long long, std::vector<long long>> pop_sizes;
    OrderedMap<long long, Num> mult;
    OrderedMap<long long, QString> why;
    OrderedMap<long long, long long> sold, pcap;
    long long cap = 0;
};
using Slots = OrderedMap<QString, StateSlots>;              // state -> its slots

class OOBEdits : public store::Store {       // doc.maps["oob"]: every nation's order of battle at the scenario's date
public:
    static constexpr const char *kind = "oob";
    OOBEdits(moddata::Mod &mod, countryhist::CountryHistory &hist, const std::optional<Date> &date);
    moddata::Mod &mod;
    countryhist::CountryHistory &hist;
    std::optional<Date> date;
    OrderedMap<QString, OOB> want;
    mutable OrderedMap<QString, OOB> _orig;
    mutable OrderedMap<QString, std::pair<OptStr, OptStr>> _files;   // tag -> (path, referenced name)
    UnitTypes *units;
    SlotRules *rules;

    std::pair<OptStr, OptStr> file_of(const QString &tag) const;
    bool exists(const QString &tag) const;
    const OOB &_orig_state(const QString &tag) const;
    const OOB &state(const QString &tag) const;
    void edit(const QString &tag, const std::function<void(OOB &)> &fn);
    QSet<QString> tags_with_units() const;
    // --- brigade slots
    void _finish(StateSlots &e) const;
    Slots state_slots(document::Document &doc, const QString &tag) const;   // (Python: slots — a Qt keyword here)
    OrderedMap<QString, Slots> all_slots(document::Document &doc, const OptStr &only = std::nullopt) const;
    static OrderedMap<long long, long long> province_caps(const Slots &sl);
    OrderedMap<long long, long long> used(const std::optional<QStringList> &tags = std::nullopt) const;
    QSet<std::pair<long long, long long>> over_cap(const QString &tag, const OrderedMap<long long, long long> &pcap) const;
    QList<long long> new_homes(document::Document &doc, const Centers &md, const QString &tag, OptInt near, long long k,
                               const std::optional<Slots> &slots_ = std::nullopt, const QList<long long> &exclude = {}) const;
    // --- undo
    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    bool partial_undo() const override { return true; }
    std::optional<QStringList> save(const QString &backup_root) override;
    OptStr _add_oob_line(const QString &tag, const QString &ref, const QString &bdir);
};

// ---------------------------------------------------------------------------------------------- naming
extern const std::vector<std::pair<QString, QString>> NAME_SCHEMES;
extern const QString PATTERN_HELP;
QStringList name_regiments(const std::vector<Regiment> &regs, const UnitTypes &units, const QString &scheme,
                           const QString &pattern = {}, const QString &army_name = {},
                           const OrderedMap<QString, long long> &start = {},
                           const OrderedMap<long long, moddata::Province> *provinces = nullptr);
QString next_army_name(const std::vector<Army> &armies);
QList<long long> next_ship_numbers(const std::vector<Navy> &navies, long long k);

// ---------------------------------------------------------------------------------------------- repairs
struct Fix {                                 // one item of the Errors tab: ("home", tag, army, regiment), …
    QString what, tag;
    long long a = 0, b = 0;                  // army / navy index, regiment / ship index
    QString other;                           // rel_unknown: the nation
    std::vector<std::pair<long long, long long>> moves;   // over: [(army, regiment)]
};
std::function<void()> fix_units(document::Document &doc, const Centers &md, const std::vector<Fix> &todo);

}  // namespace oob
