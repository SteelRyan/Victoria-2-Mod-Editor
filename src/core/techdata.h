// techdata.py — technologies/*.txt and inventions/*.txt — every technology and invention as its block of text, edited
// in place.
//
// Edits change the block's text surgically (a value, a line added / removed; comments and layout stay) or replace the
// whole block (Edit as text). TechEdits (doc.maps["techs"]) keeps the changed blocks until Apply writes them back into
// their files.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace techdata {

using OptStr = std::optional<QString>;
using Span = std::pair<qsizetype, qsizetype>;
using script::Node;
using script::Nodes;

extern const QSet<QString> TECH_BASE;        // not effects
extern const QSet<QString> INV_BASE;

QString pretty(const OptStr &key);
QString effect_name(const OrderedMap<QString, QString> &loc, const QString &key);

// ---------------------------------------------------------------------------------------------- block text edits
// root(text): the text's first node when it is a block (its parse kept alive with it), else None — cached by text
std::shared_ptr<const Node> root(const QString &text);
const Node &node_at(const QString &text, const std::vector<qsizetype> &ipath, std::shared_ptr<const Node> &keep);
QString _nl(const QString &text);
QString set_value(const QString &text, const std::vector<qsizetype> &ipath, const QString &value);
QString remove_node(const QString &text, const std::vector<qsizetype> &ipath);
QString add_line(const QString &text, const std::vector<qsizetype> &ipath, const QString &key, const QString &value);
QString add_block(const QString &text, const std::vector<qsizetype> &ipath, const QString &key,
                  const QStringList &lines = {});
QString check_block(const QString &text, const QString &key);   // '' if it is one block `key = { … }`, else what's wrong

// ---------------------------------------------------------------------------------------------- the files
struct TechEntry {
    QString kind;                            // "tech" / "inv"
    QString file;
    Span span;
    QString text;
    long long order = 0;
};

class TechDB {                               // every technology and invention
public:
    explicit TechDB(moddata::Mod &mod);
    moddata::Mod &mod;
    OrderedMap<QString, TechEntry> entries;
    OrderedMap<QString, QStringList> order;  // "tech" / "inv" -> keys
    long long generation = 0;                // (Python: id(self.entries) — changes on every load)
    QStringList _files(const QString &folder) const;
    void load();
    QStringList techs() const { return order.value(QStringLiteral("tech")); }
    QStringList inventions() const { return order.value(QStringLiteral("inv")); }
};

OrderedMap<QString, QString> fields(const QString &text);
QStringList tech_refs(const QString &text, const QSet<QString> &techs);
bool simple_limit(const QString &text, const QSet<QString> &techs);
QString replace_key(const QString &text, const std::vector<qsizetype> &ipath, const QString &new_key);
long long year_of(const QString &text);
QString mod_target(const moddata::Mod &mod, const QString &path);

struct Added {                               // TechEdits.added[key]: {"file", "text", "kind"}
    QString file, text, kind = QStringLiteral("tech");
    bool operator==(const Added &o) const = default;
};

struct TechSnap {                            // TechEdits' picture
    OrderedMap<QString, QString> want;
    OrderedMap<QString, Added> added;
    OrderedMap<OptStr, QStringList> order;
    QSet<QString> removed;
    bool operator==(const TechSnap &o) const = default;
};

class TechEdits : public store::Store {      // doc.maps["techs"]
public:
    static constexpr const char *kind = "techs";
    TechEdits(moddata::Mod &mod, TechDB &db) : mod(mod), db(db) {}
    moddata::Mod &mod;
    TechDB &db;
    OrderedMap<QString, QString> want;
    OrderedMap<QString, Added> added;
    OrderedMap<OptStr, QStringList> order;   // area -> keys (the order picked for same-year technologies)
    QSet<QString> removed;

    QStringList keys() const;
    QStringList inventions() const;
    QString file_of(const QString &key) const;
    bool exists(const QString &key) const;
    void remove(const QString &key);
    bool move_to(const OptStr &area, const QString &key, const QString &target);
    QString text(const QString &key) const;
    void set(const QString &key, const QString &text);
    void add(const QString &key, const QString &file, const QString &text, const QString &kind = QStringLiteral("tech"));
    OptStr area_of(const QString &key) const;
    QStringList column(const OptStr &area, const OrderedMap<QString, OptStr> *areas = nullptr) const;
    void move(const OptStr &area, const QString &key, long long step);
    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    OrderedMap<QString, QStringList> _file_sequences() const;
    QStringList dirty() const override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

// ---------------------------------------------------------------------------------------------- effect schema
// Python's collections.Counter: counts in the order the keys came; most_common = by count, ties in that order
class Counter {
public:
    OrderedMap<QString, long long> n;
    void add(const QString &k, long long by = 1) { n[k] += by; }
    void update(const Counter &o)
    {
        for (const auto &[k, c] : o.n)
            n[k] += c;
    }
    std::vector<std::pair<QString, long long>> most_common() const;
    bool empty() const { return n.isEmpty(); }
};

struct Kind {                                // ("number" | "bool" | "text", None) / ("choice" | "block", [(value, name)])
    QString kind;
    std::optional<std::vector<std::pair<QString, QString>>> options;
};

class EffectSchema {                         // what each effect takes, learned from the mod's techs / inventions
public:
    EffectSchema(moddata::Mod &mod, const TechDB &db);
    moddata::Mod &mod;
    struct Info {
        long long block = 0;
        Counter values, children;
    };
    OrderedMap<std::pair<OptStr, QString>, Info> info;   // (parent, key) -> what was seen
    oob::UnitTypes *units;
    QStringList goods, buildings, unit_stats;
    QSet<QString> modifiers;
    QStringList keys() const;
    Counter _values(const QString &key, const OptStr &parent = std::nullopt) const;
    Kind kind(const QString &key, const OptStr &parent = std::nullopt) const;
    QString example(const QString &key, const OptStr &parent = std::nullopt) const;
    QString _good_name(const QString &g) const;
    bool _goods_block(const QString &key) const;
};

bool _num(const QString &v);                 // float(v) works

class ConditionSchema {                      // the conditions of chance factors
public:
    ConditionSchema(moddata::Mod &mod, const TechDB &db);
    moddata::Mod &mod;
    static const QStringList LOGIC;
    QSet<QString> techs, invs;
    struct Info {
        long long block = 0;
        Counter values, children;
    };
    OrderedMap<QString, Info> info;
    QStringList keys() const;
    QString name(const QString &key) const;
    Kind kind(const QString &key, const OptStr &parent = std::nullopt) const;
    QString example(const QString &key, const OptStr &parent = std::nullopt) const;
};

}  // namespace techdata
