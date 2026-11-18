// provbuild — C++ only (the user, 2026-11-16: "Tool to add extra building types (like forts, naval bases and
// railroads) and their effects. Has to track with the province UI so the buildings can be added. They also have to be
// able to unlock in levels via technology."). Text work for the Building types page, kept apart from the page so a
// check can run it:
//   - common/buildings.txt: the province buildings (province = yes), their effects, a new one made, one taken out;
//   - technologies: which technologies unlock a building (activate_building = X) and add a level (max_X = 1);
//   - interface/province_interface.gui: the province window's building row needs a picture slot build_iconN for the
//     Nth province building, and room for the extra rows; the strip picture of a new building.
//
// MEASURED IN THE GAME (HoD 3.04, 2026-11-16, Buildings test 2026-11-16\): a 4th province building loads and a campaign
// starts, but opening ANY province window then crashes the game unless the "building" row template has a build_icon3
// (with it the window shows a 4th row). With build_icon3 written AFTER build_icon0-2 its picture also showed in the
// fort and naval-base rows; build_iconN is therefore written FIRST in the row (seen in the game: right). The window
// made taller for the extra rows: seen in the game (the editor-made test mod, 4 rows, 2026-11-16).
// The user, 2026-11-16: the window "needs to expand according to its need ... the same as the logic with the National
// focus rows" — fit_text / fit_bg fit it to the rows as focuswindow.fit does the focus window.
#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace provbuild {

extern const QStringList GAME_THREE;          // fort, naval_base, railroad: the game's own
extern const QStringList BASE_FIELDS;         // the lines of a building that are not its effects
extern const QStringList ENGINE_EFFECTS;      // effects the three use (fort_level, infrastructure, …)
extern const int ROW;                         // one building row of the province window: 35 px

// ---------------------------------------------------------------------------------------------- buildings.txt
QStringList province_keys(const QString &text);                      // province = yes, in the file's order
struct Line {
    QString key, value;
    bool block = false;
};
std::vector<Line> lines_of(const QString &text, const QString &key);  // the building block's lines
std::vector<Line> effects(const QString &text, const QString &key);   // ...without BASE_FIELDS
// a new province building: a copy of `like` (or a plain one), named `key`, type = its own key, no picture on the map
QString add_building(const QString &text, const QString &key, const std::optional<QString> &like);
QString remove_building(const QString &text, const QString &key);

// ---------------------------------------------------------------------------------------------- technologies
struct Unlock {
    QString rel, tech;
    long long year = 0;
    bool activates = false;                   // activate_building = key
    long long adds = 0;                       // max_key = n
};
// every technology that names the building, sorted by year: files = [(rel, text)]
std::vector<Unlock> unlocks(const std::vector<std::pair<QString, QString>> &files, const QString &key);
struct Tech {
    QString rel, key;
    long long year = 0;
};
std::vector<Tech> techs(const std::vector<std::pair<QString, QString>> &files);   // every technology, by year
// the technology's lines for this building: activate_building = key (on / off) and max_key = adds (0: none)
QString set_unlock(const QString &text, const QString &tech, const QString &key, bool activates, long long adds);
// the levels the techs give, in words per tech ("level 1", "levels 2–3", …). MEASURED IN THE GAME (2026-11-16, cap
// test XA-XE, the Expand button's tooltip "Level have/allowed"): the levels allowed = the sum of the max_X lines of the
// technologies discovered; activate_building gives NO level (activate only: "Level 0/0") but makes the technology a
// must ("Discovered …" in the tooltip); max_X without any activate_building: "Level 0/2", no tech needed; a province
// may start (history) above it ("Level 2/1"). The game's own three: one tech activates, five give max_X = 1 — level 5
// by technology, not their max_level 6.
std::vector<std::pair<Unlock, QString>> level_steps(const std::vector<Unlock> &u, long long max_level);

// ---------------------------------------------------------------------------------------------- the province window
// MEASURED IN THE GAME (2026-11-16, variants L1-L4 of the buildings test): the game stacks the building rows from the
// top of "province_buildings", one row every <the "building" template's size y> px (28 gave rows 28 apart); the
// template's own position is NOT used (40, 20 moved nothing); the part's size does not cut the rows off or wrap them
// (100 high: all four rows drawn); the second "province_buildings" block of the file (after the colonist items) is
// not used. The background is a picture of its own: rows packed closer leave its 35-px bands where they were.
extern const int MIN_ROW;                     // 30: the lowest row (the 32-px pictures overlap at 28 — seen)
extern const int SCREEN_H, TOP_BAR;           // 768: the smallest screen; 82: the top bar's height there (seen)
// MEASURED IN THE GAME (2026-11-16, slot test S1: numbered strips in build_icon3 / build_icon4 at their own places):
// the game shows build_icon0..2 only in their own row (0 in the first, ...), but build_icon3 and every later slot in
// EVERY row (5 rows: the blue 3 and the green 4 in all five). A 5th building's own picture can't be a slot of its own:
// the editor parks the slots from 3 on outside the window (x = PARK_X) and paints the pictures of the rows from the
// 4th on into the window's background (the rows stand at fixed places). Its level is then shown by the row's number
// box only (the painted picture is the strip's picture without its level number).
extern const int LIVE_SLOTS;                  // 3: the slots the game shows per row
extern const int PARK_X;                      // -3000: a parked slot's x (outside the window, drawn nowhere)
struct Window {
    bool found = false;                       // the "building" row and the "province_buildings" part were found
    QStringList icons;                        // build_iconN names in the row (in the file's order)
    QStringList slot_sprites;                 // [N] = the sprite build_iconN shows ("" = no such slot)
    QList<int> slot_x, slot_y;                // [N] = build_iconN's place in the row
    bool parked(int i) const { return i < slot_x.size() && slot_x[i] <= PARK_X / 2; }
    int fits = 0;                             // rows that fit above the army / navy part
    int step = 35;                            // a row's height: the "building" template's size y
    int room = 0;                             // px above the army / navy part (its highest element + 1)
    int view_y = 0, view_h = 0, part_h = 0;   // province_view's position y and height, province_buildings' height
    int part_x = 0, part_y = 0;               // province_buildings' place in the window
    int row_w = 380;                          // a row's width (the template's size x)
    int bg_x = 0, bg_y = 0;                   // the background picture's place in the window
    QString bg_sprite;                        // its sprite
    // the row template's pieces (in a row's own coordinates): the picture slot, the name, the Expand button
    int icon_x = 23, icon_y = -4, name_x = 69, name_y = 7, name_w = 135, expand_x = 204, expand_y = 1;
    QString name_font = QStringLiteral("vic_18_black"), expand_sprite = QStringLiteral("GFX_button_thin_120"),
            expand_text = QStringLiteral("PROVINCEVIEW_EXPAND"), expand_font = QStringLiteral("vic_18_black");
    QString why;                              // not found: why
};
Window read_window(const QString &gui);
// The picture the window's background was first made from, and the rows' place in it: written by fit_text as a
// comment line in the .gui ("# ModEditor province window: …") so every later fit starts from the original picture
// (no band scaled twice); without that line, the window as it is now.
struct Source {
    QString sprite;                           // the original background sprite
    int top = 0;                              // the first row's y in that picture
    int room = 0, step = 35;                  // the rows' height there, one row's height
    int pref = 35;                            // the row height wanted (the editor's setting): lowered only to fit
    bool noted = false;                       // read from the comment line
};
Source source_of(const QString &gui);
struct Fit {
    int rows = 0, step = 35;                  // rows, a row's height
    int delta = 0;                            // px the window grows (< 0: shrinks)
    int height = 0;                           // the window's new height
    int top_768 = 0;                          // its top on a 768-high screen (below the top bar: >= TOP_BAR)
    bool fits_768 = true;
    QString words;                            // what it comes to, in a sentence
};
// `rows` rows: the wanted height `pref`, lower (down to MIN_ROW) when the window would reach into the top bar of a
// 768-high screen — the national focus window's way (focuswindow.plan)
Fit plan_fit(const Window &w, int rows, int pref);
// the .gui for `rows` rows of `step` px: the row template's height, the window (it hangs from the screen's bottom: it
// grows upwards), the army / navy part moved, the background sprite `bg_sprite`; the comment line written
QString fit_text(const QString &gui, int rows, int step, const QString &bg_sprite, const Source &src);
// the background for `rows` rows of `step` px: the original's head, a band per row (its own bands, the last one
// repeated, scaled to `step`), its foot
QImage fit_bg(const QImage &src_img, const Source &src, int rows, int step);
// build_icon<index> shows `sprite`
QString set_slot_sprite(const QString &gui, int index, const QString &sprite);
// build_icon<index> stands at x, y of the row (parked: x = PARK_X)
QString set_slot_place(const QString &gui, int index, int x, int y);
// a strip's picture without its level number: each pixel as most of the frames 1..frames-1 have it (the number differs
// from frame to frame, the picture does not); one frame: the strip as it is
QImage plain_picture(const QImage &strip, int frames);
// the pictures of rows (index, picture) painted into the window's background `bg`: at the row's picture slot, rows
// `step` px apart from `top` (the first row's y in the picture)
void paint_rows(QImage &bg, const Window &w, int top, int step, const std::vector<std::pair<int, QImage>> &pics);
// the province building `key` moved to place `to` among the province buildings (its row in the window moves with it)
QString move_building(const QString &text, const QString &key, int to);
struct Problem {
    QString key;                              // the building ("" for the window as a whole)
    QString text;
    bool crash = false;                       // measured: the game crashes
};
// what the province window lacks for the province buildings of `buildings` (empty: nothing); name(key) = its name
std::vector<Problem> window_problems(const QString &buildings, const QString &gui,
                                     const std::function<QString(const QString &)> &name);
// build_icon<index> showing `sprite`, written FIRST in the "building" row
QString add_icon(const QString &gui, int index, const QString &sprite);
// a picture strip for a building: frame 0 the picture greyed (level 0), frames 1..levels with the level's number
QImage make_strip(const QImage &picture, int levels, int w = 40, int h = 32);
QString sprite_block(const QString &name, const QString &tex, int frames);   // a spriteType = { … } block (tabs)
QString add_sprites(const QString &gfx_text, const QStringList &blocks);     // into spriteTypes = { … } (made if new)

}  // namespace provbuild
