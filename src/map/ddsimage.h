// ddsimage.py — Reading the game's .dds textures (DXT1 / DXT3 / DXT5 and plain RGB(A)) into QImages — Qt can't open
// .dds — and the religion icon strip (GFX_icon_religion) cut into one picture per icon number. (Its own decoder,
// in float32 with rounding, next to imgio's integer one: both are the Python editor's, both are kept.)
#pragma once
#include "core/moddata.h"

#include <QImage>
#include <QList>
#include <QPixmap>
#include <QString>
#include <optional>
#include <utility>

namespace ddsimage {

std::optional<QImage> load_dds(const QString &path);       // first mip level, or None
std::optional<QImage> load_texture(const QString &path);   // .dds through load_dds, anything else through Qt
std::pair<std::optional<QString>, long long> _sprite(const moddata::Mod &mod, const QString &name);
const QList<QPixmap> &religion_icons(moddata::Mod &mod);    // icon number i (religion.txt) is element i - 1

}  // namespace ddsimage
