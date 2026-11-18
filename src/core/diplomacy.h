// diplomacy.py — history/diplomacy/*.txt: treaties at the start: alliances, puppets (`vassal`), substates, unions
// (`kind = { first = A second = B start_date = … end_date = … }`), plus helpers for the relations / influence that
// live in the OOB files (oob.py).
//
// A treaty counts at the scenario date when start_date <= date < end_date. Removing one deletes its block; a new one
// goes after the last treaty of the same kind with the same first nation (or at the end of the kind's usual file),
// starting at the scenario date and ending like the others of its kind.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace countryhist {
class NationEdits;
}
namespace oob {
class OOBEdits;
struct Relation;
}

namespace diplomacy {

using Date = history::Date;
using OptStr = std::optional<QString>;
using Span = std::pair<qsizetype, qsizetype>;

extern const QStringList KINDS;
extern const OrderedMap<QString, QString> KIND_FILE;
extern const OrderedMap<QString, QString> KIND_NAME;

QString date_str(const Date &d);

struct Record {                              // one treaty
    long long id = 0;
    QString kind, first, second;
    OptStr start, end;
    QString file;
    std::optional<Span> span;                // None: made in the editor
    bool operator==(const Record &o) const = default;
};

class DiploEdits : public store::Store {     // doc.maps["diplomacy"]: every treaty; `records` is the edited list
public:
    static constexpr const char *kind = "diplomacy";
    DiploEdits(moddata::Mod &mod, const std::optional<Date> &date);
    moddata::Mod &mod;
    std::optional<Date> date;
    OrderedMap<QString, QString> files;      // name -> path: the mod's files, plus base game files the mod doesn't have
    std::vector<Record> orig, records;
    long long _next = 0;

    OrderedMap<QString, QString> _files() const;
    std::vector<Record> _read() const;
    // --- queries
    bool active(const Record &r) const;
    std::vector<Record> at_start() const;
    std::vector<Record> between(const QString &a, const QString &b) const;   // either direction
    std::vector<Record> of(const QString &tag) const;
    std::optional<std::pair<QString, QString>> overlord(const QString &tag) const;   // (overlord, kind)
    // --- edits (call inside doc.edit)
    void remove(long long rid);
    void add(const QString &what, const QString &first, const QString &second);
    void set_subject(const QString &a, const QString &b, const std::optional<std::pair<QString, QString>> &subj);
    void set_treaty(const QString &a, const QString &b, const QString &what, bool on);
    // --- undo
    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    static QString block(const Record &r, const QString &nl);
    std::optional<QStringList> save(const QString &backup_root) override;
};

// the great powers at the start: the top 8 westernized (civilized) nations by prestige
QStringList great_powers(const moddata::Mod &mod, countryhist::NationEdits &nations, qsizetype n = 8);

// ---------------------------------------------------------------------------------------------- relations (OOB)
// (a's entry for b, b's entry for a) — or None
std::pair<const oob::Relation *, const oob::Relation *> pair(const oob::OOBEdits &oob, const QString &a, const QString &b);
long long relation_value(const oob::OOBEdits &oob, const QString &a, const QString &b);   // both added, ±200
std::optional<long long> level_of(const oob::OOBEdits &oob, const QString &a, const QString &b);
long long influence_of(const oob::OOBEdits &oob, const QString &a, const QString &b);
OrderedMap<QString, QStringList> sphere_lords(const oob::OOBEdits &oob, const QStringList &tags);   // level 5 towards it

}  // namespace diplomacy
