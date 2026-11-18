// shortcutfacts.py — What a test in the game found for the shortcut of every button it could reach (11 October
// 2026, Ferrum Mare 1.2 on Victoria 2 3.04): each button was given a key, the key pressed in a running campaign and
// the result read from pictures of the game. Key: "file|window path|button|n-th of that name". The data is made by
// tools/gen_shortcutfacts.py from the Python module (shortcutfacts_data.inc).
#pragma once
#include "core/ordered_map.h"

#include <QString>

namespace shortcutfacts {

struct Fact {
    QString cls;    // works, small, taken, dead, gone, silent, crash, untested, last
    QString text;
};
extern const QString TESTED;
const OrderedMap<QString, QString> &WORD();
const OrderedMap<QString, Fact> &FACTS();

}  // namespace shortcutfacts
