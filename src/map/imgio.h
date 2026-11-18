// imgio.py — Reading the game's pictures — .tga (plain and run-length packed, 16 / 24 / 32 bit: Qt's own reader
// can't do the packed ones the flags use) and .dds (DXT1 / DXT3 / DXT5 and plain) — and writing a plain .tga /
// .dds. std::nullopt where Python returns None; a null QImage where Python returns one (a 0-pixel picture).
#pragma once
#include <QImage>
#include <QString>
#include <optional>

namespace imgio {

std::optional<QImage> read_tga(const QString &path);
void write_tga(const QString &path, const QImage &img);     // plain 24-bit, bottom row first (the game's flags)
void write_tga32(const QString &path, const QImage &img);   // plain 32-bit with transparency (the icon strips)
void write_dds32(const QString &path, const QImage &img);   // plain A8R8G8B8, no smaller copies
void write_image(const QString &path, const QImage &img);   // by the ending: .dds or .tga
std::optional<QImage> read_dds(const QString &path);        // the top picture only
std::optional<QImage> read_image(const QString &path);      // .tga / .dds, or what Qt reads itself

}  // namespace imgio
