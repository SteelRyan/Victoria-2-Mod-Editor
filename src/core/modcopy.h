// modcopy — keep a copy of the original mod (C++ only, the user 2026-11-13: "I don't ever want to risk someone breaking
// their mod on accident with this … keep a copy of the base mod. And the newly changed version … appears as if it was
// the original"). Before the editor's FIRST write into a mod in a session, the untouched mod is copied into a folder
// INSIDE the mod (the user 2026-11-14: "inside of the mod folder, there's the folder with the safeguards"):
//   <mod>/ModEditor safeguards/<folder> (original 2026-11-13 2140)/
// with a .mod file of its own in the game's mod folder, named "<name> (original 2026-11-13 21:40)", so the game's
// launcher lists it beside the edited one. The edited mod keeps its folder and name. The editor's opening window lists a
// mod's safeguards under it, in a group that folds open and shut.
//
// The safeguards folder is never part of the mod: the game reads only its own folders (common, history, map, …; the
// checksum too), and the editor's walks over a whole mod skip it (the next copy, the Apply journal's scan, a submod's
// working copy).
//
// The copy is made from fs's write guard (fs::set_guard): every write, replacement or removal of a file passes it, so
// Apply, Apply by file, ↶ Apply and the pages that write at once are all covered. A copy that fails stops the write
// (OSError): nothing is written into a mod whose copy could not be made. Unticked ("Keep a copy of the original mod",
// next to Apply), no copy is made.
#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>

namespace modcopy {

struct Result {
    bool ok = false;
    QString error;              // (ok false)
    QString folder, modfile, name;   // the copy's folder, its .mod file, the name the launcher shows
    long long files = 0, bytes = 0;
};

// the folder inside a mod that holds its safeguards
extern const QString SAFE_DIR;               // "ModEditor safeguards"
bool is_safe_dir(const QString &name);       // (case as Windows treats it)
// where the copies of `folder` go: <folder>/ModEditor safeguards — or, for a safeguard itself, the folder it lies in
QString safeguard_home(const QString &folder);
// the mod a safeguard belongs to ("" when `folder` is no safeguard)
QString safeguard_owner(const QString &folder);
// the safeguards of a mod: their folders, the newest first
QStringList safeguards(const QString &mod_folder);

// The copy of `folder` (a mod folder inside the game's mod folder, or a safeguard) into its safeguards folder, with its
// own .mod file in the game's mod folder. Never overwrites: a name in use gets the seconds, then a number.
Result keep_original(const QString &folder, const QDateTime &when = QDateTime::currentDateTime());

// the folder name / launcher name the copy gets (exposed for the check)
QString copy_folder_name(const QString &folder_name, const QDateTime &when);
QString copy_mod_name(const QString &name, const QDateTime &when);

// whether `path` lies inside `folder` (or is the folder): case and slashes as Windows treats them; "X (original …)"
// next to "X" is NOT inside "X"
bool inside(const QString &path, const QString &folder);

// The guard for one mod: `folder` = the folder the editor protects (a submod: its real folder), `modfile` its .mod file.
// on() = whether a copy is wanted (the tick box); done(r) is told the outcome (a message, the tick box's text). Once per
// folder per run of the editor: a reload of the same mod does not copy again.
// Returns an id for disarm (a window replaced by a new one must not disarm the new one's guard). The environment
// variable MODEDITOR_KEEP_COPY=0 turns the copy off (the checks' scratch mods: set it in the shell that runs them).
int arm(const QString &folder, const QString &modfile, std::function<bool()> on,
        std::function<void(const Result &)> done);
void disarm(int id);
bool copied(const QString &folder);          // made in this run (or not needed: a mod created in this run)
void mark_copied(const QString &folder);     // (a brand-new mod: nothing to keep)
QString copy_of(const QString &folder);      // the copy made in this run ("" none)

}  // namespace modcopy
