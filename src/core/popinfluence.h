// popinfluence.py, its window-less half: the chances of the pop files read as cards (read_chances), a card's text
// (modifier_text), whether typed conditions can be script (cond_ok) and the cards shown together as one folding card —
// step groups and the file's own groups (ladders). The page itself (Chart, CasePanel, PopInfluencePage): ui\popinfluence.
#pragma once
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

namespace scriptstore { class ScriptStore; }

namespace popinfluence {

using Span = std::pair<qsizetype, qsizetype>;

extern const std::vector<std::pair<QString, std::pair<QString, QString>>> PER_TARGET;   // head -> (title, "… %s")
extern const std::vector<std::pair<QString, QString>> SINGLE;                         // head -> its words
const std::pair<QString, QString> *per_target(const QString &head);
const QString *single(const QString &head);

QString _pretty(const QString &k);
Span _line_span(const QString &text, qsizetype kstart, qsizetype vend);
std::optional<double> _num(const QString &s);

struct Card {                                        // one modifier = { factor … conditions } of a chance
    QString factor;
    std::optional<Span> fspan;
    Span span, node;
    QString src, cond;
    std::optional<long long> group;
    std::optional<QString> gid;                      // (sig, n) said as one text: the same in every file with that group
};
struct Chance {
    QString rel, pop, head, target;
    QStringList path;
    QString base;
    std::optional<Span> bspan;
    Span span;
    std::vector<Card> mods;
};
std::vector<Chance> read_chances(scriptstore::ScriptStore &store, const QStringList &files);

QString modifier_text(const QString &factor, const QString &cond, const QString &indent);
std::optional<QString> cond_ok(const QString &cond);   // None when the conditions can be read as script, else why not

struct Ladder {
    QString kind;                                    // "steps" / "group"
    QString key;
    bool neg = false;
    QString rest;
    std::optional<QString> tmpl;                     // (steps by one number: the condition with # for it)
    std::vector<long long> idx;
    std::optional<std::vector<double>> thr;          // None for a group
    bool grouped = false;
};
std::optional<Ladder> _tmpl_steps(const std::vector<Card> &mods, const std::vector<long long> &idx);
std::vector<Ladder> ladders(const std::vector<Card> &mods);
std::vector<Ladder> _loose_ladders(const std::vector<Card> &mods, long long least);
std::vector<long long> ladder_ticks(const Ladder &lad, long long j);

}  // namespace popinfluence
