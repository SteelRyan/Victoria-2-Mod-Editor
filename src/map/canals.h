// canals.py — Canals: what the game needs for one, read from the mod and written back.
// A canal in Vic2 is three things (measured in the game, 2026-10-15):
//   * a row of map/adjacencies.csv:  sea tile;sea tile;canal;<land province>;<canal number>;<comment>
//   * a script that opens it:        enable_canal = <number>   (a decision, an event …) — closed until then
//   * its picture on the map:        <province> = { set_province_flag = <flag> }  in that same script, and the file
//                                    map/province_flag_sprites/<flag>.txt:  sprite = Kiel_Canal / x = … / y = …
// The sprite is a 3D model (gfx/anims/*.xac, named in interface/*.gfx as an EMFXActorType). The editor draws the
// model's outline seen from above — its shape, not its look.
#pragma once
#include "core/adjacency.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/scriptstore.h"
#include "grid/grid.h"
#include "map/mapview_data.h"

#include <QImage>
#include <QSet>
#include <QString>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace canals {

using OptStr = std::optional<QString>;
extern const QString SPRITE_DIR, OWN_DECISIONS, OWN_EVENTS;
extern const QStringList OPEN_KINDS;
extern const OrderedMap<QString, QString> OPEN_NAMES;
// where the game puts a sprite and what its x / y mean — MEASURED in the game (canal test 2026-10-15): the model's
// origin sits on the middle of the box around the province's pixels; x moves it east, y north, 10 = 10 map pixels;
// one model unit = one map pixel
extern const double MODEL_UNIT, OFFSET_UNIT;
extern const bool OFFSET_Y_NORTH;

// ---- the 3D models
using V3 = std::array<double, 3>;
using V2 = std::array<double, 2>;
using Tri = std::array<long long, 3>;
struct Mesh {                                    // {"pos", "nrm" (or None), "uv" (or None), "tris"}
    std::vector<V3> pos;
    std::optional<std::vector<V3>> nrm;
    std::optional<std::vector<V2>> uv;
    std::vector<Tri> tris;
};
using PosTris = std::pair<std::vector<V3>, std::vector<Tri>>;
std::vector<PosTris> read_xac(const QString &path);     // y is up; x east, z north on the map
std::vector<Mesh> read_meshes(const QString &path);     // (raises when the file can't be read)
OptStr texture_name(const QString &path);               // the material's texture (Kiel_Canal_Diffuse), or None
extern const double WATER_Y;                             // faces below this height are the canal's bed (its water)

struct Render {                                          // the model from straight above
    QImage img;                                          // ARGB, ppu pixels a model unit
    double minx = 0, maxz = 0;                           // the picture's top-left corner in model units
    long long ppu = 8;
    grid::Grid<std::uint8_t> kind;                       // per pixel: 0 nothing, 1 water, 2 built
    double w = 0, h = 0;
};
std::optional<Render> render_top(const std::vector<Mesh> &meshes, const std::optional<QImage> &tex = std::nullopt,
                                 long long ppu = 8, bool flip_v = false);
struct Samples {                                         // the picture's covered pixels as model-unit points
    std::vector<double> xs, zs;
    std::vector<std::uint8_t> wet;
};
Samples fit_samples(const Render &render);
struct Score {
    double water_sea = 0, water_prov = 0, water_other = 0, built_prov = 0, built_sea = 0, built_other = 0;
    double other = 0;
    bool near_ = false;                                  // (Python: "near")
    std::vector<long long> reaches;
    double score = 0;
};
Score fit_score(const mapview::MapData &md, const std::vector<std::uint8_t> &sea, long long prov,
                const std::vector<long long> &seas, const Samples &samples, double ox, double oy);
using Ring = std::vector<std::pair<double, double>>;
OrderedMap<QString, std::vector<Ring>> outline(const std::vector<PosTris> &meshes);   // {"water", "built"}
OrderedMap<QString, QString> actor_files(const moddata::Mod &mod);

class Models {                                           // the canal models of the mod, read once
public:
    explicit Models(const moddata::Mod &mod);
    const moddata::Mod &mod;
    OrderedMap<QString, QString> files;
    bool exists(const QString &name) const;
    const std::optional<Render> &render(const QString &name, long long ppu = 8);
    QStringList names() const;
    OptStr path(const QString &name) const;
    const std::optional<OrderedMap<QString, std::vector<Ring>>> &outline(const QString &name);

private:
    OrderedMap<QString, std::optional<OrderedMap<QString, std::vector<Ring>>>> _outline;
    OrderedMap<QString, std::optional<Render>> _render;
};

// ---- sprite files
struct Sprite {                                          // {"sprite", "x", "y"} (+ "rel", "flag" in sprites())
    OptStr sprite;
    double x = 0, y = 0;
    QString rel, flag;
};
Sprite parse_sprite(const QString &text);
QString fmt_num(double v);
QString sprite_text(const OptStr &old, const QString &sprite, double x, double y);
OrderedMap<QString, Sprite> sprites(const moddata::Mod &mod, scriptstore::ScriptStore &store);

// ---- the scripts
struct Opening {                                         // {"kind", "name", "rel", "pos", "flags": [(prov, flag)]}
    QString kind;
    OptStr name;
    QString rel;
    qsizetype pos = 0;
    std::vector<std::pair<long long, QString>> flags;
};
OrderedMap<long long, std::vector<Opening>> openings(const moddata::Mod &mod, scriptstore::ScriptStore &store);

// ---- one canal
struct Canal {
    long long num = 0;
    std::vector<adjacency::Row> rows;
    long long prov = 0;
    std::pair<long long, long long> seas{0, 0};
    QString name;
    std::vector<Opening> opens;
    OptStr flag;
    std::optional<Sprite> sprite;
    std::optional<long long> flag_prov;
};
std::vector<Canal> canals(const adjacency::AdjacencyFile &adj, const moddata::Mod &mod, scriptstore::ScriptStore &store);
QStringList problems(const Canal &c, const moddata::Mod &mod);

// ---- writing
QString flag_name(long long num, const QSet<QString> &taken);
QString decision_block(const QString &key, long long num, long long prov, const QString &flag,
                       const QString &picture = QStringLiteral("suezcanal"));
QString event_block(const QString &eid, long long num, long long prov, const QString &flag);
QString remove_item(const QString &text, const QString &kind, const OptStr &name);   // (name None: as Python's ==)
QString _cut(const QString &text, const script::Node &n);
QString add_decision(const QString &text, const QString &block);
QString add_event(const QString &text, const QString &block);

}  // namespace canals
