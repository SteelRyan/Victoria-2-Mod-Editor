// modrename — give the mod a new name (the user, 2026-11-08: "I might start with one mod and make it into a completely
// different mod … double click it there … and rewrite the name"). New, C++ only (not in the Python editor).
//
// The name the game's launcher shows is `name = "…"` in the mod's .mod file next to its folder. Renaming writes:
//   - that line of the mod's own .mod file (no other line, no folder renamed: path / user_dir stay, so saves stay)
//   - every other .mod file of the same mod folder that names the mod in `dependencies = { … }` (its submods, e.g. a
//     4K submod: the game finds the parent by its name)
// The editor's own folders named after the mod (changelogs, almanacs) are moved by the main window.
#pragma once
#include <QString>
#include <QStringList>

namespace modrename {

// "" when the name can be used, else why not (shown in red under the name)
QString check_name(const QString &name);

struct Result {
    bool ok = false;
    QString error;              // (ok false)
    QString modfile;            // the .mod file written
    QStringList dependents;     // other .mod files changed (file names)
};

// folder: the mod's own folder (a submod: the submod's folder). Writes at once (not on Apply).
Result rename(const QString &folder, const QString &old_name, const QString &new_name);

// the text of a .mod file with its name line changed / added (exposed for the check)
QString with_name(const QString &text, const QString &name);
// the text with `old_name` in dependencies = { … } replaced; changed = whether it named it
QString with_dependency(const QString &text, const QString &old_name, const QString &new_name, bool &changed);

}  // namespace modrename
