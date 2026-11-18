// refs.py — What the words in the mod's scripts are: event ids, decisions, modifiers, flags, variables, nations,
// provinces, states, cultures, religions, goods, technologies, reforms, localisation keys … For every file: its
// references with their places; for every thing: where it is defined, set, cleared, checked, fired or used — and a
// short description (what the editor shows on hover and the side panel shows for the word under the cursor).
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"

#include <QSet>
#include <atomic>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace events {
class EventDB;
}

namespace refs {

using OptStr = std::optional<QString>;
using script::Node;
using script::Nodes;

extern const QString MODS_REL;
extern const QStringList EXTRA_MOD_FILES;
extern const QString REBEL_REL;
extern const OrderedMap<QString, std::pair<QString, std::tuple<int, int, int>>> KINDS;   // kind -> (name, colour)
extern const OrderedMap<QString, QString> ROLES;
extern const QSet<QString> COND_BLOCKS, CULTURE_KEYS, RELIGION_KEYS, PROV_KEYS, NOT_NAMES, SCOPE_KEYS;

QString esc(const QString &s);                // html.escape(str(s))
OptStr loc_text(moddata::Mod &mod, const QString &key);
long long line_of(const QString &text, qsizetype pos);

struct Ref {                                  // [pos, length, kind, target, role, ok, scope]
    qsizetype pos, length;
    QString kind, target, role;
    bool ok = true;
    OptStr scope;                             // the innermost block that changes whom the line acts on, None: the own
};
struct Use {                                  // (rel, pos, role)
    QString rel;
    qsizetype pos;
    QString role;
};
using ThingKey = std::pair<QString, QString>; // (kind, target)

class RefIndex {
public:
    // extra: more script files whose words are to be known (the rebel types, the pop types)
    RefIndex(moddata::Mod &mod, events::EventDB &db, const std::function<QString(const QString &)> &text_of,
             const QStringList &extra = {});
    moddata::Mod &mod;
    events::EventDB *db;                       // (Python rebinds it after a reparse: a pointer)
    std::function<QString(const QString &)> text_of;
    QSet<QString> rebels, techs, inventions, issues, options, modifiers, cbs;
    OrderedMap<QString, std::vector<Ref>> spans;   // rel -> refs by position
    OrderedMap<QString, std::vector<qsizetype>> _starts;
    OrderedMap<ThingKey, std::vector<Use>> uses;   // (kind, target) -> [(rel, pos, role)]
    std::vector<std::tuple<qsizetype, qsizetype, QString>> cb_spans;   // the war goals of common/cb_types.txt
    // (Python's id(refs) for "the same one?": an address can be used again — a number never is)
    long long serial = next_serial();
    static long long next_serial()
    {
        static std::atomic<long long> n{0};
        return ++n;
    }

    void update(const QString &rel, const QString &text, const Nodes *nodes = nullptr);
    void _index(const QString &rel, const QString &text, const Nodes &nodes);
    void _finish();
    bool _ok(const QString &kind, const QString &t);
    virtual ~RefIndex() = default;
    // (virtual: the large window's LocalRefs answers them for its one file — Python: duck typing)
    virtual const Ref *at(const QString &rel, qsizetype pos) const;
    virtual std::vector<Ref> in_range(const QString &rel, qsizetype a, qsizetype b) const;
    virtual std::optional<std::pair<QString, qsizetype>> definition(const QString &kind, const QString &target) const;
    std::vector<Use> usages(const QString &kind, const QString &target) const;
    QString name_of(const QString &kind, const QString &t) const;
    OptStr cb_at(qsizetype pos) const;
    QString place_name(const QString &rel, qsizetype pos) const;
    QStringList usage_lines(const QString &kind, const QString &t, qsizetype per_role = 6,
                            const QStringList &roles = {}) const;
    QString info_html(const QString &kind, const QString &t, const QStringList &extra = {}, bool usages = true) const;

protected:
    explicit RefIndex(moddata::Mod &mod_) : mod(mod_), db(nullptr) {}   // no index (LocalRefs)

private:
    OptStr _scope;
    void _add(std::vector<Ref> &out, qsizetype pos, qsizetype length, const QString &kind, const QString &target,
              const QString &role);
    void _walk(const QString &rel, const QString &text, const Nodes &nodes, const QStringList &ctx,
               std::vector<Ref> &out, const OptStr &scope = std::nullopt);
};

}  // namespace refs
