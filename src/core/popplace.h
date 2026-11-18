// popplace.py — A real place as the sample of Pop influences: a province or a state picked on the map, with its own
// pops (by type, culture and religion), its owner's primary / accepted cultures and state religion, its literacy and
// life rating. In a real place each group of pops answers "is_accepted_culture = yes" by itself, and assimilation /
// conversion change the groups as the time runs (popsim.run_groups).
// What the place sets in the case when it is picked: literacy (the owner's history: literacy for the accepted
// cultures, non_state_culture_literacy for the others — weighed by people), the province's life rating (a state: the
// people-weighed mean), the people in the place, the owner's national value and whether it is civilised.
#pragma once
#include "core/ordered_map.h"
#include "core/popcase.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace document { class Document; }
namespace events { class World; }
namespace moddata { class Mod; }

namespace popplace {

using OptStr = std::optional<QString>;
using Group = std::pair<QString, QString>;          // (culture, religion) — "?" where a pop has none
using StartKey = std::pair<QString, Group>;         // (pop type, group)

extern const QStringList GROUP_FACT_PREFIXES, GROUP_FLAGS;

class Place {
public:
    Place(moddata::Mod &mod, document::Document &doc, const std::vector<long long> &pids,
          const QString &what = QStringLiteral("province"), events::World *world = nullptr);
    ~Place();
    moddata::Mod &mod;
    document::Document &doc;
    std::vector<long long> pids;
    QString what;
    events::World *world;
    OptStr owner, primary, religion, nv;
    QSet<QString> accepted;
    std::optional<bool> civilized;
    double lit_acc = 0.0, lit_other = 0.0;
    OrderedMap<StartKey, long long> start;          // {(pop type, (culture, religion)): people}
    long long total = 0;
    std::optional<double> life_rating;

    OrderedMap<Group, long long> groups() const;
    std::pair<QString, bool> kind(const Group &g) const;   // 'primary' / 'accepted' / 'other', state religion?
    std::pair<std::optional<Group>, std::optional<Group>> turn(const Group &g) const;   // assimilates / converts to
    double literacy() const;
    void fill_case(popcase::Case &c) const;
    popcase::Case case_for(const popcase::Case &c, const Group &g) const;
    QString label(const std::function<QString(const QString &)> &name_of) const;
    QString group_name(const Group &g, const std::function<QString(const QString &)> &name_of) const;

private:
    std::unique_ptr<events::World> _own;             // (the world made here when none was given)
};

}  // namespace popplace
