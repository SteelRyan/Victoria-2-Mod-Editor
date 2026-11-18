// mapmodes.py — More map modes: plurality, literacy at the start, technology (all / one kind), and — by country —
// the accepted pops and the bureaucrats / clergy / soldiers: which modes there are, day 1 replayed as far as literacy
// goes (StartSim), and the colour tables (MoreModes — the window's parts it reads are handed in).
//
// Literacy: the country files give the start value; an event or a decision counts as setting it on day 1 only when it
// carries the line "#literacy event" (the user, 2026-10-07: assigned ones only, nothing guessed). `literacy = x` on a
// pop SETS it.
#pragma once
#include "core/events.h"
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace document {
class Document;
}
namespace countryhist {
struct NationState;
}

namespace mapmodes {

using Rgb = std::tuple<int, int, int>;
extern const Rgb DIM, NOBODY;
extern const std::vector<std::pair<QString, QString>> POP_KINDS;
extern const QSet<QString> RANDOM, STATE_KEYS;
bool tag_re(const QString &s);               // TAG_RE: ^[A-Z][A-Z0-9]{2}$

std::vector<std::pair<QString, QString>> tech_kinds(const moddata::Mod &mod);
std::vector<std::pair<QString, QString>> extra_modes(const moddata::Mod &mod, const QString &tab);
bool handles(const QString &mode);

// the map's neighbours of a province, in the order the Python editor walks them (mapview.MapData.adjacent: sets of
// ints, iterated in CPython's set order — the MapData port has to give that order)
using Neighbours = std::function<std::vector<long long>(long long)>;

class StartSim;

class StartJudge : public events::Judge {
public:
    StartJudge(events::World &world, events::EventDB &db, StartSim &sim) : events::Judge(world, db), sim(sim) {}
    StartSim &sim;
    events::Tri prov(const QString &k, const QString &v, std::optional<long long> pid, const events::Scope &self,
                     std::optional<long long> year) override;
    events::Tri country(const QString &k, const QString &v, const events::OptStr &tag, const events::Scope &self,
                        std::optional<long long> year) override;
    events::Tri scope(const events::Node &c, const events::Scope &sc, const events::Scope &self,
                      std::optional<long long> year) override;
};

class StartSim {
public:
    // db / md / text_of: none (null / empty) when no script is marked — then the country files' values only
    StartSim(moddata::Mod &mod, document::Document &doc, events::EventDB *db, Neighbours md,
             std::function<QString(const QString &)> text_of = {});
    moddata::Mod &mod;
    document::Document &doc;
    events::EventDB *db;
    Neighbours md;
    std::function<QString(const QString &)> text_of;
    QString how = QStringLiteral("none");
    std::unique_ptr<events::World> w;
    long long year;
    std::unique_ptr<StartJudge> judge;
    OrderedMap<long long, QSet<QString>> cores;
    QHash<events::OptStr, QSet<QString>> cflags;
    QSet<QString> gflags;
    QStringList sources;
    OrderedMap<QString, long long> notes, bad_scopes;
    long long set_lines = 0, core_changes = 0;
    OrderedMap<long long, std::vector<double>> lit;
    OrderedMap<long long, std::pair<double, double>> base;
    QSet<long long> changed;

    std::pair<double, double> _file_values(const events::OptStr &tag);
    const QSet<events::OptStr> &accepted(const events::OptStr &tag);
    std::vector<long long> neighbours(long long pid);
    std::vector<long long> cores_of(const events::OptStr &tag);
    void run(events::View nodes, const events::Scope &sc, const events::Scope &root, long long depth);
    std::optional<double> province(long long pid);
    QString caption();

private:
    QSet<const events::Entry *> _ran;
    QSet<QString> _fired;
    QHash<const events::Nodes *, bool> _rel;
    QHash<events::OptStr, QSet<events::OptStr>> _culture;
    void _run_day1();
    void _marked(events::Entry *e, long long depth);
    void _root(const QString &label, const std::vector<std::function<void()>> &runs);
    bool _relevant(const events::Nodes &nodes);
    void _event(events::Entry *e, const events::Scope &sc, long long depth);
    void _fire(const events::Node &n, events::Scope sc, long long depth);
    events::OptStr _tag(const QString &v, const events::Scope &root);
    bool _passes(const std::vector<const events::Nodes *> &limits, const events::Scope &sc, const events::Scope &root);
    void _pops(const events::Nodes &body, const std::vector<const events::Nodes *> &limits, const events::Scope &sc,
               const events::Scope &root);
    events::Tri _pop_ok(const events::Nodes &nodes, long long pid, const moddata::Pop &pop, double lit,
                        const events::Scope &root);
    events::Tri _pop_one(const events::Node &c, long long pid, const moddata::Pop &pop, double lit,
                         const events::Scope &root);
};

// ---------------------------------------------------------------------------------------------- the modes
// MoreModes(win, ramp): the window's parts it reads are handed in — the document (set as it changes), the province
// count of the map, its neighbours, and the event panel's EventDB (db(true): read the scripts first, ensure()).
class MoreModes {
public:
    using Ramp = std::function<Rgb(double)>;
    MoreModes(moddata::Mod &mod, long long n, Neighbours nb, std::function<events::EventDB *(bool)> db, Ramp ramp);
    moddata::Mod &mod;
    long long n;
    Neighbours nb;
    std::function<events::EventDB *(bool)> db;
    Ramp ramp;
    document::Document *doc = nullptr;

    struct Fact {
        std::optional<double> v;
        QString text;
    };
    struct Facts {                               // {tag (or province for literacy): (value, text)}
        OrderedMap<long long, Fact> pid;
        OrderedMap<QString, Fact> tag;
    };
    QString _sig();
    bool has_line();
    StartSim &sim();
    OrderedMap<QString, std::vector<long long>> _owners(const std::vector<long long> &land);
    std::optional<countryhist::NationState> _nation(const QString &tag);
    OrderedMap<QString, Rgb> _rank(const OrderedMap<QString, std::optional<double>> &values,
                                   const std::optional<QString> &mode = std::nullopt);
    std::vector<double> values(const QString &mode, const std::vector<long long> &land);
    const Facts &facts(const QString &mode, const std::vector<long long> &land);
    void table(const QString &mode, std::vector<Rgb> &t, const std::vector<long long> &land);
    QString describe(const QString &mode, long long pid);
    QString caption(const QString &mode);

private:
    std::optional<std::pair<QString, std::unique_ptr<StartSim>>> _sim;
    std::optional<std::pair<QString, bool>> _line;
    std::optional<std::pair<QString, Facts>> _facts;
    std::vector<long long> _land() const;
};

}  // namespace mapmodes
