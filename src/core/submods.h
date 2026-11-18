// submods.py — Submods: a mod that sits on top of another mod and holds only what it changes. To the game a submod
// is a mod whose .mod file names its parent (dependencies = { "Parent" }). To the editor it is edited through a
// WORKING COPY: the parent's files with the submod's laid over them, in a folder of the editor's own. After an
// Apply the files that changed there are compared with the parent: what differs goes into the submod's folder,
// what is the parent's again leaves it. The parent mod is only ever read.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace submods {

extern const QString WORK_FOLDER;
extern const QStringList EMPTY_TAKES_AWAY, NEVER_EMPTY, CHECKSUM_FOLDERS, CHECKSUM_FILES;

// {path below root, lower case: (path as it is, size, time of the last change in ns)}
using State = OrderedMap<QString, std::tuple<QString, long long, long long>>;
// {path below the mod, lower case: (path as it is, the file, size, time)} — the parent's view
using View = OrderedMap<QString, std::tuple<QString, QString, long long, long long>>;

// mod.submod (app.py): set when the mod is a submod's working copy
struct Submod {
    QString root, name;
    QStringList parents, parent_roots;
    View view;
    State state;
    QString work;
    std::optional<std::pair<QStringList, QStringList>> recovered;
};

bool counts(const QString &rel);
std::vector<std::tuple<QString, bool, long long>> holds(const QString &root);
std::pair<bool, QString> verdict(const std::vector<std::tuple<QString, bool, long long>> &files, bool pending = false);
bool name_ok(const QString &name);   // NAME_OK

struct ModfileValues {
    QStringList replace_path, dependencies;
    OrderedMap<QString, QString> other;   // name, path, user_dir, … (the first of each)
};
ModfileValues modfile_values(const QString &path);
QString modfile_text(const QString &name, const QString &folder, const QStringList &deps = {},
                     const std::optional<QString> &user_dir = std::nullopt, const QStringList &replace = {});
QByteArray cp1252_replace(const QString &text);   // .encode("cp1252", "replace")
void write_modfile(const QString &path, const QString &name, const QString &folder, const QStringList &deps = {},
                   const std::optional<QString> &user_dir = std::nullopt, const QStringList &replace = {});
std::pair<QString, QString> create(const QString &mods_dir, const moddata::ModEntry &parent, QString name);
// A brand-new mod from the base game (C++ only, the user 2026-11-13: "Basegame start of mod functionality"): an empty
// folder with history/pops (so the editor lists it; its start dates are the base game's, read through) and a .mod
// file with no dependencies. Everything not in the folder is the base game's. -> (folder, .mod file)
std::pair<QString, QString> create_new(const QString &mods_dir, QString name);
std::vector<moddata::ModEntry> chain(const std::vector<moddata::ModEntry> &mods, const moddata::ModEntry &entry);

State tree_state(const QString &root);
bool _same_bytes(const QString &a, const QString &b);
QString state_file(const QString &work);
struct Prepared {
    View view;
    State state;
};
Prepared prepare(const QStringList &layers, const QString &work, const std::function<void(const QString &)> &say = {});
QByteArray state_json(const State &state);   // json.dump({k: list(v)}) as Python writes it
void save_state(const QString &work, const State &state);
std::optional<State> load_state(const QString &work);
struct Synced {
    QStringList written, removed, emptied, cannot;
    State now;
};
using GameFile = std::function<std::optional<QString>(const QString &rel)>;
Synced sync(const QString &work, const View &view, const State &state, const QString &target, const QString &backup_root,
            const GameFile &game = {});
std::optional<std::pair<QStringList, QStringList>> recover(const QString &work, const QStringList &layers,
                                                           const QString &backup_root);
QString real_root(const moddata::Mod &mod);

}  // namespace submods
