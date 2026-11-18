#ifndef MODEDITOR_VERSION_H
#define MODEDITOR_VERSION_H
// The editor's version (the user 2026-11-15: the first full release is 1.0.1; 1.0.0 is the beta's tag on GitHub).
// Raise it HERE only: the title bar, the exe's Properties → Details (modeditor.rc), the RAR's name (tools\build.ps1
// reads this file) and the update check all take it from these lines. Plain #defines, because the resource compiler
// reads this file too.
#define MODEDITOR_VERSION_MAJOR 1
#define MODEDITOR_VERSION_MINOR 0
#define MODEDITOR_VERSION_PATCH 2
#define MODEDITOR_VERSION_TEXT "1.0.2"
#endif
