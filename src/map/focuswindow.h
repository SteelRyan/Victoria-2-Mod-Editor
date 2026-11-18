// focuswindow.py — The game's national focus window (interface/province_interface.gui + gfx/interface/
// province_natfocus_bg) fitted to the focuses the mod has: more or fewer rows than the picture is drawn for, more
// focuses in a row than fit side by side.
// How the game lays the window out (read from its files):
//   * positionType focuscategory_start: where the first row stands; focuscategory_step: the distance row to row;
//   * windowType focus_category: a row — its name, and the box "focus_icons" the icons stand in (a box that pushes the
//     icons over each other when they don't fit its width);
//   * the picture province_natfocus_bg: a head, one band a row (a dark bar for the row's name, room for the icons
//     under it), a foot with the Close button.
// Fitting: the picture is drawn again — head, one band a row (the last band repeated or left out; the bands made lower
// when the window would not fit a 768-high screen), foot — the step and the Close button follow; for more focuses in
// a row than fit, the picture and the icon box are made wider (a column of the middle repeated).
// picture(): the window drawn here as the game lays it out. NOT seen in the game. How many rows / focuses a row the
// game takes at most is not known.
#pragma once
#include "map/gamefont.h"

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace focuswindow {

using OptStr = std::optional<QString>;
extern const QString GUI, SPRITE;
extern const long long ICON, ICON_W, HEAD_OFF, SCREEN_H, WINDOW_Y, MIN_STEP, PAD;
extern const QString BACK;
extern const std::vector<long long> SCREENS;

using Span = std::pair<qsizetype, qsizetype>;
std::optional<Span> _block(const QString &text, const QString &kind, const QString &name);
struct XY {                                          // (x, y, start, end)
    long long x = 0, y = 0;
    qsizetype s = 0, e = 0;
};
std::optional<XY> _xy(const QString &text, const Span &span, const QString &key);
struct G {
    XY start, step, icons, close, cancel;
};
std::optional<G> read(const QString &text);         // None: the file is laid out another way
std::tuple<long long, long long, long long, long long> drawn(const G &g, const QImage &img);   // (rows, band, head, foot)
struct PlanOut {
    long long step, width, extra, height, rows0, step0, fit0, head, foot;
    QStringList notes;
};
PlanOut plan(const G &g, const QImage &img, long long rows, long long per_row);
struct Fitted {                                      // (the .gui text, the picture) — or (None, why)
    OptStr text;
    QImage img;
    QString why;
};
Fitted fit(const QString &text, const QImage &img, long long rows, long long per_row);

// ---- the window, drawn
struct Details {
    std::pair<long long, long long> name{0, -33};
    QString font = QStringLiteral("vic_22");
    long long name_w = 360;
    std::pair<long long, long long> box{-8, -9};
    long long item = 32;
    double spacing = 1.0;
    std::tuple<QString, QString, QString> close{QStringLiteral("GFX_button_standard_200"), QStringLiteral("CLOSE"),
                                               QStringLiteral("vic_18_black")};
    std::tuple<QString, QString, QString> cancel{QStringLiteral("GFX_button_thin_160"), QStringLiteral("REMOVE_FOCUS"),
                                                QStringLiteral("vic_18_black")};
};
Details details(const QString &text);
class Look {                                         // locwin.GameLook: the buttons' pictures, the game's fonts
public:
    virtual ~Look() = default;
    virtual std::optional<QImage> sprite(const QString &name) = 0;
    gamefont::Fonts *fonts = nullptr;
};
struct Row {                                         // (group key, its name, [(focus key, number of its picture)])
    QString key, name;
    std::vector<std::pair<QString, long long>> cells;
};
struct Hit {                                         // (x, y, w, h, key); (x / w whole numbers or not, as in Python)
    double x, y, w, h;
    QString key;
    bool x_int, w_int;
};
struct Drawn {
    std::optional<QImage> img;
    std::vector<Hit> hits;
    QStringList notes;
};
Drawn picture(const QString &text, const std::optional<QImage> &bg, const std::vector<Row> &rows,
              const std::vector<QPixmap> &frames, Look *look, const std::function<OptStr(const QString &)> &words,
              const OptStr &sel = std::nullopt, const QStringList &multi = {});

}  // namespace focuswindow
