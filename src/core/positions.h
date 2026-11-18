// positions.py, its file half — map/positions.txt: where the game draws a province's name, units and buildings.
// Coordinates in the file are in game space (y from the BOTTOM of the map; display y = map height - game y).
// Saving replaces only the province blocks that changed (other text, comments and unknown keys stay).
// The placement half (PositionPlacer, check_entry) uses arrays and lives with the map: map\positions_geo.
#pragma once
#include "core/history.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/store.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <utility>

namespace positions {

using XY = std::pair<double, double>;
extern const QStringList POINT_KEYS, BUILDING_KEYS, PLACED;
inline constexpr double CHAR_W = 0.47;      // name width per letter (per text_scale unit, map px)
inline constexpr double TEXT_H = 0.95;      // name height
inline constexpr double SCALE_MIN = 0.6, SCALE_MAX = 10.0;
inline constexpr double ANCHOR_UP = 0.5;    // the name's centre lies this many name-heights above text_position
extern const OrderedMap<QString, QString> KIND_NAMES;
extern const QString CRASH;

XY name_center(const XY &pos, double th, double scale);
XY name_anchor(const XY &center, double th, double scale);

struct Entry {                              // a province block (points in game coordinates, unknown parts as text)
    OrderedMap<QString, XY> pts;
    std::optional<double> text_rotation, text_scale, naval_rot;
    QStringList extra, bp_extra, br_extra;
    bool operator==(const Entry &o) const;  // (freeze(a) == freeze(b): the points in any order)
};
using EntryP = std::shared_ptr<const Entry>;   // entries are never changed in place: edits replace them

Entry parse_entry(const QString &text, const script::Node &node);
QString fmt_entry(long long pid, const Entry &e, const QString &nl);

class EntrySnap : public store::SnapBase {          // one province's entry in the picture (compared by value)
public:
    explicit EntrySnap(EntryP p) : e(std::move(p)) {}
    EntryP e;
    bool equals(const store::SnapBase &o) const override
    {
        const auto *p = dynamic_cast<const EntrySnap *>(&o);
        return p && (p->e == e || *p->e == *e);
    }
};

class PositionsFile : public store::Store {
public:
    static constexpr const char *kind = "positions";
    PositionsFile(moddata::Mod &mod, long long map_h);
    moddata::Mod &mod;
    long long H;
    QString path;
    OrderedMap<long long, EntryP> entries;
    OrderedMap<long long, EntryP> orig;     // (Python: freeze(e) of each entry as loaded)
    OrderedMap<long long, std::pair<qsizetype, qsizetype>> spans;   // pid -> (start, end) of its block on disk
    std::optional<QString> hash;
    QString nl = QStringLiteral("\r\n");

    void _load(const QString &text);
    XY to_disp(const XY &xy) const { return {xy.first, double(H) - xy.second}; }
    XY to_game(const XY &xy) const;
    Entry get(long long pid) const;
    void set_entry(long long pid, Entry e);
    void set_point(long long pid, const QString &what, const std::optional<XY> &disp_xy);
    QList<long long> dirty_ids() const;

    store::Snap snapshot() const override;          // {pid: entry}
    void restore(const store::Snap &snap) override;
    QStringList dirty() const override;
    bool undo_after_apply() const override { return true; }
    bool partial_undo() const override { return true; }
    std::optional<QStringList> save(const QString &backup_root) override;
    std::optional<QString> save_file(const QString &backup_root);
};

QSet<long long> naval_in_history(const history::ProvinceHistory &hist);

}  // namespace positions
