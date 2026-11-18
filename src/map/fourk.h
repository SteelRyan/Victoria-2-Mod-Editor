// fourk.py — The 4K interface submod of a mod.
// Victoria 2 draws its interface in fixed pixel sizes: at 3840 x 2160 every window, button and letter is half as big
// as at 1920 x 1080. build() writes a submod in which the whole interface is twice as big:
//   interface/*.gui      every position and size doubled (not the few entries that are counts)
//   interface/*.gfx      the sizes of the sprite kinds that carry one (framed tiles, bars, charts), and the height
//                        of the system fonts
//   gfx/interface, gfx/pictures, and every other picture a 2D sprite names      scaled to double size
//   gfx/fonts            the letter sheets scaled letter by letter, the .fnt measures doubled
//   gfx/cursors          the pointers doubled (Windows decides how big they are drawn; they get sharper)
// It reads the files the game reads with the mod loaded (the mod's file wins over the game's), so pictures the mod
// does not have are taken from the game and end up in the submod too. Nothing is written but into the output folder.
// Seen in the game (2026-10-07, Ferrum Mare at 3840 x 2160): positionType entries named …wrap_count / …countper…
// hold COUNTS, not pixels; a plain (not packed) .dds above about 16 MB is not drawn at all.
// (numpy's float32 work — the bicubic doubling, the DXT packing — in numpy's own order of sums: measured 2026-10-20.)
#pragma once
#include "core/moddata.h"
#include "core/ordered_map.h"

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <array>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace fourk {

using OptStr = std::optional<QString>;
extern const int S;                                  // the factor
extern const long long PLAIN_LIMIT;                  // bytes: a plain .dds above this is written packed (DXT)
extern const QStringList PICTURE_EXT;
extern const QString REPORT;                         // in the submod's folder: what was made, the list of its files
extern const QStringList PICTURE_TREES;              // scaled whole (every picture in them)
extern const QString FONT_TREE;

struct Cancelled {};                                 // (thrown by a tick: the build stops)
struct PyError : std::runtime_error {                // an exception as Python names it (the report says its type)
    PyError(const QString &type, const QString &msg) : std::runtime_error(msg.toStdString()), type(type) {}
    QString type;
};

// ---- the game's files
class Files {                                        // path below the game folder (lower case, /) -> file
public:
    explicit Files(const moddata::Mod &mod) : mod(mod) {}
    const moddata::Mod &mod;
    QStringList roots(const QString &sub) const;
    OrderedMap<QString, QString> tree(const QString &sub) const;
    std::optional<std::pair<QString, QString>> find(const QString &rel) const;
    QString true_rel(const QString &full) const;     // (raises ValueError)
};

// ---- .gui / .gfx texts
QString times(const QString &v, std::optional<double> s = std::nullopt, bool whole = false);   // by S or s
extern const QStringList TEXTURE_KEYS;
bool NUM(const QString &s);                          // NUM.match(s)
OptStr val_of(const script::Node &node, const QString &name);   // (Python: _val)
struct Scaled {
    QString text;
    long long count = 0;
    OrderedMap<QString, std::set<long long>> frames; // {sprite texture (as written): frame counts}
};
// s: another factor than S (Library → Zoom & map marks: 1.25, 1.5 …), whole: numbers rounded to whole pixels
Scaled scale_script(const QString &text, bool is_gui, std::optional<double> s = std::nullopt, bool whole = false);
OptStr check_script(const QString &old, const QString &fresh);

// ---- pictures (h, w, channels) of bytes
struct Pic {
    long long h = 0, w = 0;
    int ch = 4;
    std::vector<std::uint8_t> d;
    Pic() = default;
    Pic(long long h, long long w, int ch, std::uint8_t fill = 0) : h(h), w(w), ch(ch), d(size_t(h * w * ch), fill) {}
    std::uint8_t &at(long long y, long long x, int c) { return d[size_t((y * w + x) * ch + c)]; }
    std::uint8_t at(long long y, long long x, int c) const { return d[size_t((y * w + x) * ch + c)]; }
    Pic part(long long y0, long long y1, long long x0, long long x1) const;   // a[y0:y1, x0:x1]
    Pic channels(int from, int n) const;             // a[..., from:from + n]
    bool operator==(const Pic &o) const = default;
};
Pic up(const Pic &a);                                // bicubic, doubled; rounded after each of the two passes
Pic bigger(const Pic &rgba);                         // colour weighed by opacity
Pic resized(const Pic &rgba, long long w, long long h);
Pic scale_frames(const Pic &rgba, long long frames);
Pic qimage_rgba(const QImage &img);
QImage to_qimage(const Pic &rgba);                   // (RGBA8888, a copy)

struct Info {                                        // what a picture file really is
    QString kind;                                    // dds / tga / png / jpg / bmp
    OptStr pack;                                     // dds: DXT1 / DXT3 / DXT5 / None
    std::optional<bool> alpha, grey;                 // (missing: the key isn't in Python's dict)
    std::optional<long long> bits;                   // tga
};
std::pair<Pic, Info> read_picture(const QString &path);   // raises PyError
QByteArray dds_head(long long w, long long h, const OptStr &pack = std::nullopt, long long bits = 32,
                    bool alpha = true, bool grey = false, long long size = 0);
QByteArray dxt_bytes(const Pic &rgba, const QString &pack);
OptStr write_picture(const QString &path, const Pic &rgba, const Info &info);

// ---- fonts
extern const long long SMALL_BASE, SLOT_LIMIT;
struct ScaledFnt {
    QString text;
    std::vector<std::array<long long, 4>> boxes;     // (x, y, w, h) of every letter on the sheet
    std::pair<long long, long long> sheet;
};
ScaledFnt scale_fnt(const QString &text);
std::pair<Pic, OptStr> scale_font_sheet(const Pic &rgba, const std::vector<std::array<long long, 4>> &boxes,
                                        const std::pair<long long, long long> &sheet);
// C++ only (the user 2026-11-15: the province names in 0.1 steps): a bitmap font at f × its size. A letter's box
// [x, x + w) becomes [floor(x f), floor((x + w) f)) — boxes that did not overlap still do not — the sheet
// ceil(W f) × ceil(H f) (a power of two: the next one), the other numbers rounded. boxes / sheet as scale_fnt_by gives them (the font's own).
ScaledFnt scale_fnt_by(const QString &text, double f);
std::pair<Pic, OptStr> scale_font_sheet_by(const Pic &rgba, const std::vector<std::array<long long, 4>> &boxes,
                                           const std::pair<long long, long long> &sheet, double f);

// ---- mouse pointers
struct CurImage {
    Pic px;
    long long hx = 0, hy = 0;
};
std::vector<CurImage> cur_images(const QByteArray &data);
QByteArray cur_bytes(const std::vector<CurImage> &images);
QByteArray scale_cur(const QByteArray &data);
QByteArray scale_ani(const QByteArray &data);

// ---- the whole job
struct PicJob {
    QString rel, full;
    long long frames = 1;
};
struct Plan {
    std::vector<std::pair<QString, QString>> scripts;
    std::vector<PicJob> pictures;
    std::vector<std::tuple<QString, QString, std::vector<std::pair<QString, QString>>>> fonts;
    std::vector<std::pair<QString, QString>> cursors;
    QStringList missing;
    OrderedMap<QString, std::pair<QString, std::optional<long long>>> money;   // rel -> (its picture, side)
};
Plan plan(const moddata::Mod &mod);
struct Report {
    long long scripts = 0, numbers = 0, pictures = 0, fonts = 0, cursors = 0;
    double megapixels = 0;
    QStringList notes, problems, missing, gone;
    QString out;
    long long bytes = 0, files = 0;
};
using Tick = std::function<void(long long done, long long total, const QString &what)>;   // may throw Cancelled
// C++ only (2026-11-09, the user's sliders on the 4K screen of Tools → Submods & 4K): how big some parts are made.
// The defaults are what the build has made since 2026-11-07 (the files come out the same).
struct Sizes {
    double topbar = 2.0;                             // interface/topbar.gui
    double unitpanel = 2.0;                          // interface/unitpanel.gui + single_unitpanel.gui (a picked army)
    double marks = 4.0 / 3.0;                        // the map marks (interface/mapicons.gui: army counters …)
    double names = 1.0;                              // the map's font (province names): 1 its own size, 2 doubled,
                                                     // in between in 0.1 steps (2026-11-15)
    bool operator==(const Sizes &o) const = default;
};
extern const double MARKS;                           // 4/3
Sizes read_sizes(const QString &report);             // the sizes a submod was made with, from its REPORT file's notes
Report build(const moddata::Mod &mod, const QString &out, const Tick &tick = nullptr, int threads = 0,
             const Sizes &sizes = {});
QString report_text(const moddata::Mod &mod, const Report &rep, const QStringList &made);

}  // namespace fourk
