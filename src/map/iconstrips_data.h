// iconstrips.py, its data half (PLAN.md §4.1): the game's icon strips — one picture holding many small ones side by
// side; an interface/*.gfx file names the picture and says how many it holds (noOfFrames). Here: the sprites as the
// editor has them now (pending changes included), their pictures, and making a strip longer (the picture wider by
// whole frames AND noOfFrames up, for the strip and its sister strips) or putting a picture into a frame. The strip
// page and the pickers are kind C / D: ui\.
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "map/moreviews_data.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <optional>
#include <tuple>
#include <variant>

namespace document {
class Document;
}

namespace iconstrips {

struct Use {                                  // one entry of USES: what picks a picture of which strip
    QString id, title, sprite;
    QStringList also, rels;
    QString folder, key;
    long long order = 0, first = 1;           // order: by the entry's place (depth 1 / 2); first: the first picture
    bool zero = false;                        // counts from 0 (crimes)
    QString where, note;
};
extern const std::vector<Use> USES;
const Use *use_of(const QString &sprite);     // (Python: USE_OF.get(sprite))
std::optional<long long> pic_of(const Use *use, std::optional<long long> value);
long long value_of(const Use *use, long long picture);

QString tex_rel(const QString &value);
QImage placeholder(int w, int h, long long number);
QImage fit_frame(const QImage &img, int w, int h);
QString set_frames(QString text, const QStringList &names, const OrderedMap<QString, long long> &n_of);
const script::Node *_sprite_node(const script::Nodes &nodes, const QString &name);

struct Sprite {                               // {"tex": rel, "frames": n, "gfx": rel}
    QString tex;
    long long frames = 1;
    QString gfx;
};
using Sprites = OrderedMap<QString, Sprite>;

// a filler for extend(): ("number",), ("copy", frame), ("image", picture, {sister strip: its own picture})
struct Filler {
    QString kind;
    long long frame = 0;
    QImage img;
    OrderedMap<QString, QImage> sisters;
};
using StripFiles = OrderedMap<QString, moreviews::Asset>;   // rel -> ("strip", QImage)

class Strips {
public:
    Strips(const moddata::Mod &mod, scriptstore::ScriptStore *store, moreviews::AssetEdits *assets = nullptr)
        : mod(mod), store(store), assets(assets) {}
    const moddata::Mod &mod;
    scriptstore::ScriptStore *store;
    moreviews::AssetEdits *assets;            // (Python: doc.maps.get("assets"))
    QStringList gfx_rels() const;
    Sprites sprites();
    static void _walk(const script::Nodes &nodes, OrderedMap<QString, std::pair<QString, long long>> &found);
    std::pair<QString, std::optional<QString>> resolve(const QString &tex) const;
    std::optional<QImage> image(const QString &name, const Sprites *sprites = nullptr);
    QStringList family(const QString &name);
    // -> ({rel: ("strip", QImage)}, {gfx rel: text}), or the reason it can't
    std::variant<std::pair<StripFiles, OrderedMap<QString, QString>>, QString> extend(const QString &name,
                                                                                     const std::vector<Filler> &fillers);
    StripFiles put(const QString &name, long long number, const QImage &picture);
    // picture `number` out of the strip and its family; every line that picked a later picture renumbered (one down)
    // -> ({rel: ("strip", QImage)}, {rel: text}, how many lines were renumbered), or the reason it can't
    std::variant<std::tuple<StripFiles, OrderedMap<QString, QString>, long long>, QString> remove(const QString &name,
                                                                                                 long long number);
    std::variant<std::pair<StripFiles, OrderedMap<QString, QString>>, QString> reorder(const QString &name,
                                                                                      const std::vector<long long> &order);
    // [(entry key, picture number)] of what picks pictures of a strip (number None: not a number)
    std::vector<std::pair<QString, std::optional<long long>>> users(const Use &use);

private:
    void _users(const script::Nodes &nodes, const QString &key,
                std::vector<std::pair<QString, std::optional<long long>>> &out, const Use &use);
    OrderedMap<QString, QString> _texts;       // rel -> the text its sprites were read from
    OrderedMap<QString, OrderedMap<QString, std::pair<QString, long long>>> _by_file;
};

// doc's Strips (one per document and mod, kept)
Strips &strips_for(const moddata::Mod &mod, document::Document &doc);

// C++ only (the user 2026-11-16: "a reminder, if I made a new National Value or National Focus or a tech school, what
// sprite is used for it and how easily can I change it"): picture `num` of `sprite` in words — the strip's file, the
// .gfx that names it, and whether the strip has that picture at all
QString picture_words(Strips &strips, const QString &sprite, std::optional<long long> num);

}  // namespace iconstrips
