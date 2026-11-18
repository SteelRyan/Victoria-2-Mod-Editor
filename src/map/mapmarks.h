// mapmarks.py — Map marks: the flat marks the game draws on the map (an army's or a fleet's counter, a battle, a
// siege, colonising, mobilising: interface/mapicons.gui + interface/mapicons.gfx) made bigger or smaller for comfort.
//
// Seen in the game (2026-10-26, Ferrum Mare, 1600 × 900): what is on the map next to an army at every height the camera
// reaches is the counter of mapicons.gui (window unit_mapicon), drawn at its pixel size whatever the zoom. make() with a
// factor f: every position and size of mapicons.gui × f (whole pixels), the sizes in mapicons.gfx × f, every picture of
// mapicons.gfx at f × its size (frame by frame) as a file of its own in gfx/interface/mapmarks/, the two sprites other
// windows share copied under a name of their own, and the counters' font swapped for a font of the mod about f × as
// tall. Interface files and pictures do not change the checksum (measured).
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "map/fourk.h"
#include "map/moreviews_data.h"

#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace scriptstore { class ScriptStore; }

namespace mapmarks {

extern const QString GUI, GFX, PIC_DIR, MAP_SUFFIX;
extern const QStringList SHARED;                     // used by other windows too: copied for the map

OrderedMap<QString, long long> fonts_of(const QStringList &texts);   // {bitmap font: its size in px}
QString font_for(const QString &name, double factor, const OrderedMap<QString, long long> &fonts);
OrderedMap<QString, long long> frames_of(const QString &text);       // {texture as written: frames}
fourk::Pic scale_picture(const fourk::Pic &rgba, double factor, long long frames = 1);
QString _copy_shared(const QString &gfx_text);

using Find = std::function<std::optional<QString>(const QString &path)>;   // texture as written -> file, or None
struct Made {
    QString gui, gfx;
    OrderedMap<QString, moreviews::PicP> pics;       // {picture path below the mod: (rgba, info)}
    QStringList notes;
};
Made make(const QString &gui_text, const QString &gfx_text, const Find &find, double factor,
          const OrderedMap<QString, long long> &fonts);
// [gui text, gfx text] as they are now (the mod's first; a file made by an earlier run: the game's)
QStringList originals(const moddata::Mod &mod, scriptstore::ScriptStore *store);

}  // namespace mapmarks
