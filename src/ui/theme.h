// theme.py — Dark 'cartographer's instrument panel' theme (matches the concept mockup).
#pragma once
#include <QString>

class QApplication;
class QWidget;

namespace theme {

extern const QString BG, PANEL, PANEL_ALT, LINE, LINE_SOFT, INK, INK_DIM, INK_FAINT, GOLD, GOLD_DIM, DANGER, OK,
    ERROR_, WARN, MONO;                      // (ERROR is a Windows macro: ERROR_)
extern const QString QSS;

QString ink_on(int r, int g, int b);        // black or white text on this background (contrast ratio)
// The theme on the whole application: a dark palette under the style sheet, so that a widget no rule of the sheet
// names is dark too (the user's rule: never light text on a light ground).
void apply(QApplication &app);
// The user's rule: NEVER a horizontal scrollbar in a panel, list, dialog or menu — content wraps or elides instead.
// The style sheet makes every horizontal scrollbar 0 px high; no_hscroll() also switches them off.
void no_hscroll(QWidget *root);

}  // namespace theme
