// modfolders — mods moved / copied between folders (C++ only, the user and a tester 2026-11-17).
//
// The tester keeps his mods for work OUTSIDE the game's folder: the opening window takes a base-game path of its own
// and more than one mod folder ("all below this line are from path XYZ"). The user: "I want to be able to drag mods
// from the safeguards to the mod folder and vice versa freely — light folder management". So a mod (its folder and its
// .mod file) can be moved — or copied (Ctrl) — into another mod folder, into a mod's safeguards, or out of them.
//
// The .mod file goes with the folder:
//   into a mod folder:          <to>/<name>.mod, path = "mod/<name>"
//   into a mod's safeguards:    next to the owner (where its own .mod file lies), path = "mod/<owner>/ModEditor
//                               safeguards/<name>" (as modcopy writes a safeguard's)
// A safeguard without a .mod file gets one when it leaves the safeguards (it becomes a mod proper). A name in use
// gets " (2)", " (3)" … (folder and launcher name alike). A copy leaves the safeguards of the copied mod out (as
// modcopy's copies do); a move takes them along, and their .mod files follow.
//
// Nothing is deleted: a move across drives copies, then sends the old folder to the Recycle Bin.
#pragma once
#include "core/moddata.h"

#include <QString>
#include <QStringList>

namespace modfolders {

struct Result {
    bool ok = false;
    QString error;              // (ok false)
    QString folder, modfile, name;   // where the mod is now, its .mod file, its launcher name
};

// The mod `m` into the folder `to` (a mod folder, or <mod>/ModEditor safeguards). copy = Ctrl+drag.
Result move_mod(const moddata::ModEntry &m, const QString &to, bool copy,
                const std::vector<moddata::ModEntry> &all = {});

// the .mod file's text with `path = "mod/<rel>"` (and the name, when given) set; other lines kept as they are
QString with_path(const QString &text, const QString &rel, const QString &name = QString());

// a free name in `dir` for the folder `base` (and its .mod file next to `modfile_dir`): base, base (2), base (3) …
QString free_name(const QString &dir, const QString &base, const QString &modfile_dir, const QString &own_modfile = {});

}  // namespace modfolders
