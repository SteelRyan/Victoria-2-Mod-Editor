// mapfiles.py — map/climate.txt and map/continent.txt: which provinces belong to which climate / continent, and the
// modifiers (effects) each one gives; map/default.map's max_provinces and sea_starts.
// Saving edits only what changed; everything else in the files, including comments, is left untouched.
// Climates / continents that exist in the base game can never be removed; every save makes sure each of their
// definitions carries a comment saying so.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace mapfiles {

using Mods = std::vector<std::pair<QString, QString>>;     // [(key, value), ...]
using Edit = std::tuple<qsizetype, qsizetype, QString>;

extern const int PER_LINE;
extern const OrderedMap<QString, QStringList> VANILLA_FALLBACK;   // used when the base game can't be found
extern const QString NOTE_MARK;
extern const OrderedMap<QString, QString> NOTES;
extern const QStringList COMMON_MODIFIERS;    // suggestions in the editor (any other key can be typed)

struct MembershipSnap {
    QStringList names;
    OrderedMap<QString, std::vector<long long>> lists;
    OrderedMap<QString, Mods> mods;
    bool operator==(const MembershipSnap &o) const = default;
};

class MembershipFile {
public:
    static constexpr bool undo_after_apply = true;     // (its undo steps stay true when the file is written)
    MembershipFile(moddata::Mod &mod, const QString &kind);
    moddata::Mod &mod;
    QString kind;                                       // "climate" | "continent"
    QString path;
    QStringList names;                                  // in file order
    OrderedMap<QString, std::vector<long long>> lists;  // name -> [pid, ...]
    OrderedMap<QString, Mods> mods;                     // name -> [(key, value), ...]
    std::optional<QString> hash;
    QSet<QString> vanilla;
    QStringList on_disk;
    MembershipSnap orig;

    QSet<QString> _vanilla_names() const;
    const script::Node *_id_node(const script::Node &n, bool second = false) const;
    const script::Node *_mod_node(const script::Node &n, bool second = false) const;
    static std::vector<const script::Node *> _scalar_children(const script::Node &node);
    void _load();
    QStringList of(long long pid) const;
    OrderedMap<long long, QStringList> membership() const;
    QStringList dirty() const;
    QStringList removed() const;
    bool is_vanilla(const QString &name) const;
    QStringList known_modifier_keys() const;
    void assign(const QList<long long> &pids, const QString &name);
    void remove(const QStringList &names);
    void set_definitions(const OrderedMap<QString, Mods> &mods, const QStringList &new_names);
    MembershipSnap snapshot() const;
    void restore(const MembershipSnap &snap);
    static std::pair<qsizetype, qsizetype> _line_span(const QString &text, qsizetype s, qsizetype e);
    static Edit _insert_before_close(const QString &text, const script::Node &node, const QStringList &lines,
                                     const QString &nl);
    std::vector<Edit> _id_edits(const QString &text, const script::Node &idn, const QString &name, const QString &nl) const;
    std::vector<Edit> _mod_edits(const QString &text, const script::Node &mn, const QString &name, const QString &nl) const;
    QString _new_entry(const QString &name, const QString &nl) const;
    static std::pair<qsizetype, qsizetype> _cut_span(const QString &text, qsizetype s, qsizetype e);
    std::vector<Edit> _note_edits(const QString &text, const std::vector<const script::Node *> &nodes,
                                  const QString &nl) const;
    bool missing_notes() const;
    std::optional<QString> save(const QString &backup_root, bool force = false);
};

class DefaultMapFile {
public:
    static constexpr const char *kind = "default_map";
    static constexpr bool undo_after_apply = true;
    explicit DefaultMapFile(moddata::Mod &mod);
    moddata::Mod &mod;
    QString path;
    std::optional<long long> max_provinces;
    std::vector<long long> sea;
    std::optional<QString> definitions;
    // (C++ only, 2026-11-08) the `definitions = "…"` value written on Apply (nullopt: left as it is). MEASURED in
    // the game: with a bare file name ("definition.csv") the game builds its province index picture
    // (cache/provincecache.bin) from the BASE GAME's definition.csv, even when the mod has its own: a renumbered or
    // recoloured province then has no pixel there and the map loads crash at random. Every working mod writes
    // "../mod/<folder>/map/definition.csv"; with it the same test mod loaded 3/3 (crashed 3/3 before).
    std::optional<QString> new_definitions;
    std::optional<QString> hash;
    std::optional<std::pair<qsizetype, qsizetype>> _max_span;
    std::optional<std::pair<qsizetype, qsizetype>> _def_span;
    std::optional<script::Node> _sea_node;
    QString _text;
    using Snap = std::tuple<std::optional<long long>, std::vector<long long>, std::optional<QString>>;
    Snap orig;

    void _load(const QString &text);
    Snap snapshot() const { return {max_provinces, sea, new_definitions}; }
    void restore(const Snap &snap)
    {
        max_provinces = std::get<0>(snap);
        sea = std::get<1>(snap);
        new_definitions = std::get<2>(snap);
    }
    // (C++ only) the value `definitions` must get so the game reads this mod's own file, or nullopt when it is fine:
    // a bare file name while the mod has its own file of that name and it differs from the base game's
    std::optional<QString> definitions_wanted() const;
    QStringList dirty() const;
    void dedupe_sea();
    std::optional<QString> save(const QString &backup_root);
};

}  // namespace mapfiles
