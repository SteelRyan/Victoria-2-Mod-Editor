// entryview.py, its data half (PLAN.md §4.1): which game files the mod has, the war files' sides. The file list
// itself (_files) is blockcode::files_of. The views (EntryView, GameSettingsView, PagesWorkspace) are kind D: ui\.
#pragma once
#include "core/blockcode.h"
#include "core/moddata.h"
#include "core/scriptstore.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace entryview {

// The game itself has this file and the mod doesn't replace its folder: without a file of the mod's own the game
// uses its own.
bool inherited(const moddata::Mod &mod, const QString &rel);
bool in_mod(const moddata::Mod &mod, const scriptstore::ScriptStore &store, const QString &rel);
// A file of the mod goes: deleted on Apply (with a backup). Never an empty file in its place — the game crashes on
// an empty war file.
void drop_file(const moddata::Mod &mod, scriptstore::ScriptStore &store, const QString &rel);
// GameSettingsView's lines of common/defines.lua: `key = value,  -- note` and `SECTION = {`
extern const QRegularExpression LINE_RE, SECT_RE;
// The nation tags a war file names as attackers and defenders.
QStringList war_sides(const QString &text);
inline QStringList _files(const moddata::Mod &mod, const scriptstore::ScriptStore *store, const QString &folder,
                          bool hide_empty = false)
{
    return blockcode::files_of(mod, store, folder, hide_empty);
}

}  // namespace entryview
