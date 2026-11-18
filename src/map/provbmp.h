// provbmp.py — provinces.bmp as an editable layer: pixel changes with undo (per stroke, only the changed pixels are
// kept), the derived map data kept up to date, and byte-exact saving (the original header, 24-bit rows). A BMP
// stores its bottom row first, so the file rows are simply the view rows in order.
#pragma once
#include "core/moddata.h"
#include "core/store.h"
#include "grid/grid.h"
#include "map/mapview_data.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace mapcheck {
struct Pieces;
}

namespace provbmp {

using Box = std::array<long long, 4>;
using Ids = std::set<long long>;   // (Python: a set — it only reaches refresh_ids, which sorts it, and the listeners)

QString file_hash(const QString &path);

struct Diff {
    std::vector<std::int64_t> idx;
    std::vector<std::uint16_t> old, nw;
};
struct Recovery {
    std::vector<std::int64_t> idx;
    std::vector<std::uint16_t> vals;
    bool operator==(const Recovery &) const = default;
};

class ProvinceBitmap : public store::Store {
public:
    static constexpr const char *kind = "provinces_bmp";
    ProvinceBitmap(moddata::Mod &mod, mapview::MapData &md);
    moddata::Mod &mod;
    mapview::MapData &md;
    QString path;
    long long version = 0, saved = 0;
    std::vector<Diff> diffs;                     // diffs[i]: version i -> i + 1
    std::vector<std::function<void(const Box &, const Ids &)>> listeners;
    std::optional<QString> hash;                 // file hash, taken lazily (the file is large)

    store::Snap snapshot() const override { return store::box(version); }
    void restore(const store::Snap &s) override { restore_to(store::unbox<long long>(s)); }
    void restore_to(long long v);
    QStringList dirty() const override;
    std::optional<Recovery> recovery_state() const;
    void recover(const std::optional<Recovery> &state);
    bool has_recovery() const override { return true; }
    store::Snap recovery_snap() const override
    {
        const auto r = recovery_state();
        return r ? store::box(*r) : nullptr;
    }
    void recover_snap(const store::Snap &s) override
    {
        recover(s ? std::optional<Recovery>(store::unbox<Recovery>(s)) : std::nullopt);
    }
    std::pair<Box, Ids> _write(const std::vector<std::int64_t> &idx, const std::vector<std::uint16_t> &ids);
    std::optional<Box> paint_live(const std::vector<std::int64_t> &idx, long long new_id);
    std::optional<Box> commit(const std::vector<std::int64_t> &idx, const std::vector<std::uint16_t> &old,
                              const std::vector<std::uint16_t> &nw);
    void _changed(const Box &box, const Ids &ids);
    std::optional<QStringList> save(const QString &backup_root) override;
    void reload_file();
    void remember_file();
    QByteArray encode(const QByteArray &orig) const;
    bool undo_after_apply() const override { return true; }
};

std::optional<Box> _union(const std::optional<Box> &a, const std::optional<Box> &b);
grid::Grid<std::uint8_t> component(const grid::Grid<std::uint8_t> &mask, long long y, long long x);
// pieces smaller than `limit` pixels apart from their province's main area (or, only_id = 0, of no province):
// [(flat pixel indexes, the id most of their neighbours have)]
// (`pieces`: mapcheck::province_pieces of this very id map when the caller has it — mapcheck::pieces_of)
std::vector<std::pair<std::vector<std::int64_t>, std::int64_t>> small_pieces(const grid::Grid<std::uint16_t> &idm, long long limit,
                                                                            std::optional<long long> only_id = std::nullopt,
                                                                            const mapcheck::Pieces *pieces = nullptr);

}  // namespace provbmp
