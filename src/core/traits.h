// traits — the leader traits of common/traits.txt and the odds of a new general / admiral (C++ only, the tester and the
// user 2026-11-17: "if I got 500 different traits it is exactly a 1/500 chance for each of them to be rolled").
//
// The file has two lists, `personality = { … }` and `background = { … }`; a leader gets one of each and their values add
// up. The odds here take every trait of a list as equally likely (the user's rule — NOT measured in the game), the
// "no_personality" / "no_background" entries counted or left out by the caller's choice.
//
// The rewriters change only what they are asked to: a trait whose values change is written again in the base game's
// way (one tab, its lines one tab deeper), everything else keeps its exact text.
#pragma once
#include "core/ordered_map.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace traits {

extern const QStringList LISTS;              // personality, background
extern const QStringList STATS;              // attack defence morale organisation reconnaissance speed attrition experience reliability
QString stat_word(const QString &stat);      // "attack", "defence", … as shown
bool is_percent(const QString &stat);        // shown as a share (0.05 = 5 %): all but attack / defence

struct Trait {
    QString key;
    OrderedMap<QString, double> v;           // its stat lines, in the file's order
    QStringList other;                       // its other lines, kept as text
    double get(const QString &stat) const;
};
struct Lists {
    std::vector<Trait> personality, background;
    const std::vector<Trait> &of(const QString &list) const { return list == u"background" ? background : personality; }
    const Trait *find(const QString &key) const;
    QString list_of(const QString &key) const;   // "personality" / "background" / ""
};
Lists parse(const QString &text);

QString number(double v);                    // 1, -0.25, 0.15 (no float noise)
QString summary(const Trait &t);             // "+1 attack, −25 % speed" ("no effect")
bool is_none(const QString &key);            // no_personality / no_background

// --- odds (each trait of a list equally likely)
using Dist = std::map<long long, double>;                       // value × 1e6 (rounded) -> chance
using Dist2 = std::map<std::pair<long long, long long>, double>;
long long vkey(double v);
double vval(long long k);
Dist dist(const std::vector<Trait> &list, const QString &stat, bool with_none);
Dist2 dist2(const std::vector<Trait> &list, const QString &sx, const QString &sy, bool with_none);
Dist add(const Dist &a, const Dist &b);       // the sum of two independent rolls
Dist2 add2(const Dist2 &a, const Dist2 &b);
long long count(const std::vector<Trait> &list, bool with_none);

// --- rewriters: the new text of traits.txt
QString set_stat(const QString &text, const QString &list, const QSet<QString> &keys, const QString &stat,
                 const std::optional<double> &value);              // value none: the line goes
QString add_to_stat(const QString &text, const QString &list, const QSet<QString> &keys, const QString &stat, double by);
// copies of `keys`, `n` each: the new keys in `made` (key_2, key_3 … not used in either list)
QString copy_traits(const QString &text, const QString &list, const QStringList &keys, long long n,
                    std::vector<std::pair<QString, QString>> *made);   // (new key, the key it copies)
QString remove_traits(const QString &text, const QString &list, const QSet<QString> &keys);
QString new_trait(const QString &text, const QString &list, const QString &key);
QString free_key(const Lists &l, const QString &base);              // base, base_2, base_3 … not in use

}  // namespace traits
