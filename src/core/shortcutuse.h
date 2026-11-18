// shortcutuse.py — What a button of the game is FOR, and whether a shortcut on it makes sense (Rules → Shortcuts).
// Every button gets a plain line of what it does and one of three classes: good (stands once in its window and does
// something), niche (works, but for something narrow), none (a row of a list — the key presses the LAST row — the
// body of a slider, a background, a window Victoria 2 never shows). The classes rest on the game test of
// 11 October 2026 (shortcutfacts.h).
#pragma once
#include "core/ordered_map.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <utility>

namespace shortcutuse {

// what shortcuts.Button holds and this module looks at
struct Button {
    QString rel, name;
    QStringList path;     // the names of the windows it sits in, outermost first
    long long nth = 0;    // the n-th button of that name in that window
};

extern const OrderedMap<QString, QString> CLASS_NAME;
extern const OrderedMap<QString, std::pair<QString, QString>> WINDOWS, PLACES;
extern const QStringList LEFTOVER;
extern const OrderedMap<QString, QString> ROWS, WHAT, SORT_WORD;

QString _pretty(const QString &name);
std::pair<QString, QString> window_title(const QString &top, const QString &rel = {});
std::optional<QString> is_row(const Button &b);
QString what(const Button &b, const QString &says = {});
std::tuple<QString, QString, QString> classify(const Button &b, const QString &says = {});

}  // namespace shortcutuse
