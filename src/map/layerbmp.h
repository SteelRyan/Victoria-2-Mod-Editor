// layerbmp.py — terrain.bmp and rivers.bmp as editable layers (8-bit palette bitmaps the size of provinces.bmp).
// Pixel changes are undoable (per step only the changed pixels are kept) and saving writes the original header and
// palette back with the new rows. A BMP stores its bottom row first, so the file rows are the view rows in order.
// Land and sea have to match between the bitmaps (LandSea): land painted where there was sea gets plains in
// terrain.bmp and "no river" in rivers.bmp; sea painted over land gets the sea values the other sea tiles have.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/store.h"
#include "grid/grid.h"
#include "map/mapview_data.h"

#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

namespace layerbmp {

using Rgb = std::tuple<int, int, int>;
using Box = std::array<long long, 4>;   // (x0, y0, x1, y1)

struct Diff {
    std::vector<std::int64_t> idx;
    std::vector<std::uint8_t> old, nw;
};
struct Recovery {                       // ("pixels", flat indexes, their values now)
    std::vector<std::int64_t> idx;
    std::vector<std::uint8_t> vals;
    bool operator==(const Recovery &) const = default;
};

// one 8-bit bitmap (kind "terrain_bmp" / "rivers_bmp") in doc.maps; loaded when first needed
class IndexedBitmap : public store::Store {
public:
    IndexedBitmap(moddata::Mod &mod, const mapview::MapData &md, const QString &filename, const QString &kind);
    moddata::Mod &mod;
    const mapview::MapData &md;
    QString filename, kind, path;
    std::optional<grid::Grid<std::uint8_t>> data;   // (h, w), view orientation
    std::vector<Rgb> palette;
    std::optional<QString> error;
    long long version = 0;
    long long stamp = 0;                 // goes up with every change of the pixels (edit, undo, redo, reload)
    long long saved = 0;
    std::vector<Diff> diffs;             // diffs[i]: version i -> i + 1
    std::optional<QString> hash;
    std::vector<std::function<void(const Box &)>> listeners;   // fn(box) after pixels changed
    // (C++ only, 2026-11-10) the boxes of the last pixel changes, by stamp, as MapData.pix_log: the union of the boxes
    // changed after `stamp` ({0, 0, 0, 0}: none), None when the list doesn't reach back that far
    std::deque<std::pair<long long, Box>> stamp_log;
    std::optional<Box> changed_since(long long since) const;

    void _notify(const std::vector<std::int64_t> &idx);
    bool load();
    long long color_index(const Rgb &rgb, long long dflt) const;
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
    void apply(const std::vector<std::int64_t> &idx, const std::vector<std::uint8_t> &vals);
    void live(const std::vector<std::int64_t> &idx, const std::vector<std::uint8_t> &vals);
    void revert_unsaved();
    std::optional<QStringList> save(const QString &backup_root) override;
    bool undo_after_apply() const override { return true; }   // (its undo steps stay true when the file is written)
    std::optional<std::tuple<qsizetype, qsizetype, bool>> _layout;   // (offset, stride, top-down)
};

OrderedMap<long long, QString> terrain_types(const moddata::Mod &mod);

class LandSea {
public:
    LandSea(const moddata::Mod &mod, const mapview::MapData &md, IndexedBitmap &terrain, IndexedBitmap &rivers)
        : mod(mod), md(md), terrain(terrain), rivers(rivers) {}
    const moddata::Mod &mod;
    const mapview::MapData &md;
    IndexedBitmap &terrain, &rivers;
    std::vector<std::uint8_t> ocean;     // 256 flags
    long long ocean_fill = 0, river_sea = 0, river_none = 0;
    std::optional<long long> plains;
    std::pair<bool, bool> _prepare();
    struct Change {
        IndexedBitmap *bitmap;
        std::vector<std::int64_t> idx;
        std::vector<std::uint8_t> vals;
    };
    std::vector<Change> changes(const std::vector<std::int64_t> &idx, const std::vector<long long> &nw);

private:
    std::optional<std::pair<bool, bool>> _ready;
};

}  // namespace layerbmp
