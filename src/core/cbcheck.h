// cbcheck.py — The war goals (casus belli, common/cb_types.txt) checked: are the brackets right, is each war goal
// whole — the lines every war goal of the game has, lines the game doesn't know, what it demands in the peace (po_…)
// together with what that demand needs, its place in peace_order, its picture, its texts — and does a script
// anywhere name a modifier or a war goal that doesn't exist (the game crashes when it comes to such a line).
//
// Nothing here is Qt widgets: mapcheck turns the findings into the Errors tab's groups. What counts as "whole" is
// taken from the game's own file and from 1,486 war goals of 22 mods: a rule is only a rule when the game keeps it.
#pragma once
#include "core/script.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace cbcheck {

using script::Node;
using script::Nodes;
using OptStr = std::optional<QString>;

extern const QString REL;
extern const QStringList MOD_RELS;
extern const QString MAKE_REL;
extern const QStringList NEEDED, PO, YESNO, NUMBER, WHOLE, BLOCKS, STATE_LINES, SCAN_FOLDERS, BRACKET_FOLDERS;
extern const QSet<QString> KNOWN, GAME_GAPS, GAME_ORDER_ONLY;

using Val = std::variant<std::monostate, long long, QString>;   // None / an int / a text (a finding's data)
using Data = std::vector<Val>;

struct F {                                   // one finding: what, where, the war goal it is about, what a fix needs
    QString text;
    QString rel = REL;
    std::optional<qsizetype> pos;
    OptStr goal;
    std::optional<Data> data;
};

struct Report {
    QStringList goals;                       // the war goals the game reads, in the file's order
    std::vector<F> broken, lines, order, texts, no_mod, no_cb, brackets;
    std::vector<F> equals;                   // script files with a doubled = ("key = = value")
    long long frames = 0;
};

long long line_of(const QString &text, qsizetype pos);
QString blank_comments(const QString &text);
long long _indent(const QString &text, qsizetype pos);
bool _looks_like_goal(const Node &n);
std::vector<const Node *> _swallowed(const Node &node, std::vector<const Node *> &out);
const Node *_unclosed(const QString &text, const Nodes &nodes, std::optional<qsizetype> stop = std::nullopt);
std::pair<long long, std::vector<qsizetype>> bracket_depths(const QString &text);
std::optional<F> check_brackets(const QString &rel, const QString &text);
// "key = = value": no script of the game has two = in a row, and the game crashes on one in a pop type file
std::vector<std::pair<qsizetype, qsizetype>> doubled_equals(const QString &text);
std::optional<F> check_equals(const QString &rel, const QString &text);
QString fix_equals(QString text);
QString close_brackets(const QString &text);

struct Structure {
    const Node *order = nullptr;
    std::vector<const Node *> goals;
    QSet<QString> inside, broken;
};
Structure read_structure(const QString &text, const Nodes &nodes, Report &rep);
void check_goal(const QString &text, const Node &g, Report &rep, long long frames = 0);
void check_order(const QString &text, const Node *order, const std::vector<const Node *> &goals,
                 const QSet<QString> &inside, Report &rep);
QString fix_order(const QString &text, const std::vector<std::pair<QString, OptStr>> &todo);
std::vector<std::pair<QString, QString>> text_keys(const QString &name, const OptStr &war_name);
void check_texts(const QString &text, const std::vector<const Node *> &goals, const QSet<QString> &skip, Report &rep,
                 const std::function<bool(const QString &)> &has_text,
                 const std::function<OptStr(const QString &)> &game_text = nullptr);

// ---------------------------------------------------------------------------------------------- names that don't exist
QSet<QString> modifier_names(const std::function<QString(const QString &)> &text_of);
struct Ref {                                 // (kind "mod" / "cb", the line's key, the name, position of the name)
    QString kind, key, name;
    qsizetype pos;
};
std::vector<Ref> scan_refs(const QString &text);
struct Cache {                               // (Python: a dict kept between runs, by the text's identity)
    QHash<QString, std::pair<QString, std::vector<Ref>>> refs;
    QHash<QString, std::tuple<QString, std::optional<F>, std::optional<F>>> brackets;   // (text, brackets, equals)
};
void check_refs(const QStringList &rels, const std::function<QString(const QString &)> &text_of,
                const QSet<QString> &mods, const QSet<QString> &goals, Report &rep, Cache *cache = nullptr,
                const std::function<bool(const QString &)> &own = nullptr,
                const std::function<QString(const QString &, qsizetype)> &where_in = nullptr);
QString make_modifiers(const QString &text, const QStringList &names);

// ---------------------------------------------------------------------------------------------- everything
Report run(const std::function<QString(const QString &)> &text_of, const QStringList &rels,
           const std::function<bool(const QString &)> &has_text, long long frames = 0,
           const std::function<OptStr(const QString &)> &game_text = nullptr, Cache *cache = nullptr,
           const std::function<bool(const QString &)> &own = nullptr, const QStringList &bracket_rels = {});

}  // namespace cbcheck
