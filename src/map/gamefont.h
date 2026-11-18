// gamefont.py — Text drawn the way the game draws it: with the game's own letter sheets (gfx/fonts/*.fnt + picture),
// the colour codes of its fonts, line breaks, flags and word wrap.
// The codes of a localisation text (the game's files and the Victoria 2 wiki, "Localisation"):
//   §W §Y §R §G §B §b §g   the colour from here on (white, yellow, red, green, blue, black, grey — the exact colours
//                          are those of the font's `colorcodes` in interface/core.gfx);  §!  back to the font's own
//   \n                     a new line (the two characters backslash and n)
//   @TAG                   that nation's flag
//   $NAME$                 a word the game fills in ($COUNTRY$, $CAPITAL$ …)
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/scriptstore.h"
#include "map/fourk.h"

#include <QImage>
#include <QPainter>
#include <QString>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace gamefont {

using OptStr = std::optional<QString>;
using Rgb = std::tuple<int, int, int>;
using Codes = OrderedMap<QString, Rgb>;
extern const Codes COLOURS;                          // (the game's own, when a font names none)
extern const OrderedMap<QString, QString> COLOUR_NAMES;

class Font {                                         // one letter sheet
public:
    Font(const QString &fnt_path, fourk::Pic sheet);
    std::map<long long, std::array<long long, 7>> chars;   // id -> (x, y, width, height, xoffset, yoffset, xadvance)
    long long line = 16, base = 12;
    fourk::Pic sheet;                                // rgba
    static int code(QChar ch);                       // its byte in Windows-1252, else 63 ('?')
    long long advance(QChar ch) const;
    long long width(const QString &text) const;
    const QImage &tinted(const Rgb &rgb);            // the sheet in one colour
    double draw(QPainter &p, double x, double y, const QString &text, const Rgb &rgb);

private:
    const std::array<long long, 7> *glyph(QChar ch) const;
    std::map<Rgb, QImage> _tinted;
};

struct FontEnt {
    QString fname;
    Rgb rgb;
    Codes codes;
};
class Fonts {                                        // the fonts the interface names (bitmapfonts in interface/*.gfx)
public:
    explicit Fonts(const moddata::Mod &mod, scriptstore::ScriptStore *store = nullptr) : mod(mod), store(store) {}
    const moddata::Mod &mod;
    scriptstore::ScriptStore *store;
    std::optional<OrderedMap<QString, FontEnt>> table;
    void load();
    struct Got {
        Font *font;
        Rgb rgb;
        Codes codes;
    };
    std::optional<Got> get(const OptStr &name);     // (raises when a font's files can't be read)
    const std::optional<QImage> &flag(const QString &tag, long long height);

private:
    QString _text(const QString &rel);
    OrderedMap<QString, std::shared_ptr<Font>> _fonts;
    std::map<std::pair<QString, long long>, std::optional<QImage>> _flags;
};

struct Word {                                        // (kind, value, colour, width): "t" text, "sp" a space, "f" a flag
    QString kind;
    QString text;
    QImage img;
    Rgb colour;
    long long width = 0;
};
using Line = std::vector<Word>;
struct Layout {
    Font *font;
    std::vector<Line> lines;
    long long widest = 0;
};
std::optional<Layout> layout(Fonts &fonts, const OptStr &font_name, const QString &text, double max_width,
                             const OrderedMap<QString, QString> &samples = {});
// -> (lines, lines that did not fit into max_height, height used)
std::tuple<long long, long long, long long> draw(QPainter &p, Fonts &fonts, const OptStr &font_name, const QString &text,
                                                 double x, double y, double max_width = 0, double max_height = 0,
                                                 const QString &fmt = QStringLiteral("left"), bool clip = false,
                                                 const OrderedMap<QString, QString> &samples = {});
// What is odd about a text's codes: [(severity "error" | "warn", sentence)].
std::vector<std::pair<QString, QString>> check(const QString &text, const QStringList &known_vars = {},
                                               const QStringList &known_tags = {});

}  // namespace gamefont
