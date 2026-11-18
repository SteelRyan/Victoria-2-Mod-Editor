// settings.py — App settings (window state, last mod, colour modes...), stored under "ModEditor".
// A test run names a file of its own in MODEDITOR_SETTINGS: it never reads or writes the user's settings.
// While both editors exist, the C++ one keeps its own name ("ModEditorCpp", PLAN.md §4.2) so it never changes the
// Python editor's settings.
#pragma once
#include <QSettings>
#include <memory>

namespace settings_ {

inline std::unique_ptr<QSettings> settings()
{
    const QString own = qEnvironmentVariable("MODEDITOR_SETTINGS");
    if (!own.isEmpty())
        return std::make_unique<QSettings>(own, QSettings::IniFormat);
    return std::make_unique<QSettings>(QStringLiteral("ModEditorCpp"), QStringLiteral("ModEditorCpp"));
}

}  // namespace settings_
