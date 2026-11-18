// popcase.py — The case of Pop influences ("what if"): the state of a nation and its pops as the cards' conditions
// ask for it (military spending, literacy, militancy, needs met, a national value, yes / no facts) and the reading
// of a card's conditions against it. ASSUMED (not measured in the game): the pop-share conditions are read against
// the sample province whatever scope they stand in; the needs / militancy conditions "of the poor / middle class"
// use the same setting as the pop's own.
#pragma once
#include "core/ordered_map.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <vector>

namespace popcase {

struct Var {
    QString name;
    double lo, hi, start;
    QString kind;   // "pct" 0 … 1 shown in per cent · "ten" 0 … 10 · "num"
};
extern const OrderedMap<QString, Var> VARS;
extern const OrderedMap<QString, QString> ALIAS;
extern const QSet<QString> SCOPES, FACTS_ON;

// ("and", kids) ("or", kids) ("nor", kids) ("var", key, number) ("share", pop type, number) ("strata", class)
// ("fact", text, wanted)
struct Tree {
    QString t;
    std::vector<Tree> kids;
    QString a;
    double num = 0.0;
    bool want = true;
};
using Shares = QHash<QString, double>;

std::optional<double> _num(const QString &v);
QString _norm(const QString &text);
Tree compile_cond(const QString &src, const QStringList &types);
std::tuple<QSet<QString>, QSet<QString>, bool> parts(const Tree &tree);

class Case {
public:
    Case() { reset(); }
    OrderedMap<QString, double> vals;
    QSet<QString> facts;
    void reset();
    bool holds(const Tree &tree, const QString &strata, const Shares &shares) const;
    void set_fact(const QString &text, bool on);
    double below(const QString &key, double number) const;
    bool make(const Tree &tree, bool want, const QString &strata, const Shares &shares);
};

// a chance block as the pop panel holds it: {"base": text or None, "mods": [{"factor": text, "src": text}]}
struct ChanceMod {
    QString factor;
    QString src;
};
struct Chance {
    std::optional<QString> base;
    std::vector<ChanceMod> mods;
};
class Block {
public:
    Block(const Chance &ch, const QStringList &types);
    double base = 1.0;
    struct Card {
        double f;
        Tree tree;
        bool dyn;
    };
    std::vector<Card> cards;
    using Prepared = std::pair<double, std::vector<std::pair<double, const Tree *>>>;
    Prepared prepare(const Case &c, const QString &strata) const;
    double number(const Case &c, const QString &strata, const Shares &shares, const Prepared *prepared = nullptr) const;
};

QString show(const QString &key, double value);

}  // namespace popcase
