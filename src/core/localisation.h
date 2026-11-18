// localisation.py — Localisation (localisation/*.csv): names of provinces, states, cultures, religions, … and how the
// editor writes them. The game reads the files in ONE alphabetical order over the mod's and the base game's files (a
// mod file taking the place of the game's file with the same name) and uses the FIRST definition of a key (measured
// 2026-10-15). A defined key is changed IN PLACE where the game reads it (a base game file copied into the mod
// first); a new key goes to the editor's own file.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace localisation {

using OptStr = std::optional<QString>;
using Where = std::pair<QString, bool>;           // (path, in the mod?)

extern const QString OWN_FILE;
extern const QString LEGACY_FILE;                 // (its name until 2026-10-16 — a mod that has it keeps using it)
extern const QString RULE;
extern const QStringList HEADER;
extern const QString SECTION_END;
extern const std::vector<std::pair<QString, QString>> SECTIONS;   // section key -> title in OWN_FILE
extern const OrderedMap<QString, QString> TITLES;

QString own_file(const moddata::Mod &mod);
QString banner(const QString &title);
QString entry(const QString &key, const QString &text);
QString clean(const QString &text);               // one line, no ';', Windows-1252 bytes (read latin-1)
QString section_of(const QString &key);
std::vector<Where> loc_dirs(const moddata::Mod &mod);
std::vector<Where> loc_files(const moddata::Mod &mod);
const OrderedMap<QString, Where> &_index(const moddata::Mod &mod);
OrderedMap<QString, Where> defined_where(const moddata::Mod &mod, const QStringList &keys);
QString _add_to_own(const QString &text, const OrderedMap<QString, std::vector<std::pair<QString, QString>>> &entries);
QStringList write_names(moddata::Mod &mod, const OrderedMap<QString, QString> &names, const QString &backup_root,
                        const OrderedMap<QString, QString> &sections = {});
QString _where_text(const std::optional<Where> &w, const moddata::Mod &mod);

class LocEdits : public store::Store {           // doc.maps["loc"]: {key: (text, section)}
public:
    static constexpr const char *kind = "loc";
    explicit LocEdits(moddata::Mod &mod) : mod(mod) {}
    moddata::Mod &mod;
    OrderedMap<QString, std::pair<QString, QString>> fresh;   // (Python: self.new)
    OrderedMap<QString, QString> _orig;                        // names the editor showed before (to undo)
    OrderedMap<QString, QString> _where;                       // key -> file it is written to (cached)

    OptStr text(const QString &key) const;
    void set(const QString &key, const QString &text, const OptStr &section = std::nullopt);
    void drop(const QString &key);
    void prefetch(const QStringList &keys);
    QString where(const QString &key);
    void _show();

    store::Snap snapshot() const override;
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    bool partial_undo() const override { return true; }
    bool has_rebase() const override { return true; }
    store::Snap rebase_begin() override;
    store::Snap rebase(const store::Snap &snap, const store::Snap &old) override;
    std::optional<QStringList> save(const QString &backup_root) override;
};

}  // namespace localisation
