// textselect.py — Text in the editor can be marked and copied (the user, 2026-10-16: "in general text should be
// markable and copyable" — e.g. a technology's code name, to use it in a decision).
// One rule for the whole application, set once (theme::apply): every QLabel that shows text gets selectable by the
// mouse — its links keep working. Left as they are: labels inside something that is clicked as a whole (a button, a
// list / table, a menu, a combo box, the tool tip, or a widget of the editor that takes the mouse itself — a card, a
// chip), because a selectable label takes the click for itself.
// (Python finds "a widget of the editor that takes the mouse itself" by looking at its class; in C++ such a class
// sets the property TAKES_MOUSE in its constructor — mark_takes_mouse(this).)
#pragma once

class QApplication;
class QLabel;
class QWidget;

namespace textselect {

extern const char *const TAKES_MOUSE;
inline void mark_takes_mouse(QWidget *w);
bool selectable_ok(const QLabel *lab);
void install(QApplication &app);

}  // namespace textselect

#include <QWidget>
inline void textselect::mark_takes_mouse(QWidget *w) { w->setProperty(TAKES_MOUSE, true); }
