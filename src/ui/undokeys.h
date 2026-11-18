// Ctrl+Z / Ctrl+Y reach the editor's undo from every text box (C++ only, the user 2026-11-13: "in every single
// function, the Ctrl+Z for undoing something works correctly, works in small steps, and works chronologically").
// Qt lets a focused editable text box (QLineEdit, a number box, an editable list box, a plain text box) claim Ctrl+Z /
// Ctrl+Y for itself even when it has nothing of its own to take back — the editor's undo then did nothing whenever the
// cursor stood in a box (the pop total after Enter, a name field …). Now the box keeps the keys only for typing that is
// NOT handed over yet (its text differs from what it showed when the cursor came in or Enter was last pressed); every
// other Ctrl+Z / Ctrl+Y goes to the window's undo / redo. The script editors keep their own handling (theirs already
// is the document's undo).
#pragma once
#include <QObject>

namespace undokeys {

class Router : public QObject {
public:
    explicit Router(QObject *parent = nullptr);
    bool eventFilter(QObject *obj, QEvent *e) override;
};

void install();                               // once per application (the main window calls it)

}  // namespace undokeys
