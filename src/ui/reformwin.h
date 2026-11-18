// reformwin.py — Politics → Reforms "As the game's window": the game's own politics window
// (interface/country_politics.gui, drawn from the mod's / the game's pictures) with the mod's reforms laid over it
// where the game shows them.
//
// The game's reforms window has a sub-window per reform, NAMED after the reform's key (slavery, vote_franschise …):
// that is where the reform's icon and its current option stand. So:
//   a reform of the mod with a sub-window of its name   framed in gold, its name and the option most nations start
//                                                        with written in; a click picks it in the editor
//   a sub-window whose name is no reform of the mod      framed amber, dashed: an empty place in the game's window
//   a reform of the mod without a sub-window             listed under the picture in red: the window has no place
//                                                        for it (see NOTE)
// The unciv reforms work the same way (unciv_reforms_window).
//
// MEASURED 2026-10-26 (Ferrum Mare + a submod adding the political reform zz_test_reform with two options, no
// sub-window for it): the game loads, its Reforms tab shows the 15 reforms that have a sub-window and NOT the new one.
// The game draws a reform as its name and the list of its options, a tick at the one the nation has.
//
// C++ only (the user 2026-11-15: "It just needs to be the reform window ... it looks exactly identical, and then just on
// the right side I have its effects"): only the reforms' part of the window is shown — no tabs at the top, no left
// side (government, upper house, the ideologies' pies) — and every reform is drawn with the game's own pieces:
//   the reform's name        its font (vic_18) at the sub-window's reform_name place
//   each option              reform_option_window's row: its name in Arial12_black, 14 px a row
//   the option it has        the game's tick + frame (GFX_politics_reform_selected) — the option picked here, else
//                            the one most nations start with
//   the option under the mouse  the game's option button (GFX_politics_reform_select, the tan bar)
// The reform picked is framed in gold (the editor's own mark: the game picks no reform).
#pragma once
#include "core/ordered_map.h"

#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <functional>
#include <QString>
#include <QWidget>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace locwin { class GameLook; }

namespace reformwin {

extern const QString REL;
constexpr double ROW_W = 300.0, ROW_H = 40.0;   // the part of a reform's sub-window that shows (icon + its option)
constexpr double OPT_H = 14.0, NAME_W = 112.0;  // a row of the options' list (as the game draws it); the name's column
// reform_option_window (country_politics.gui): where its pieces stand in an option's row, from the sub-window's corner
constexpr double TICK_X = 148, TICK_Y = 8, BAR_X = 166, BAR_Y = 9, OPT_X = 169, OPT_Y = 10, OPT_W = 130;
constexpr double NAME_X = 40, NAME_Y = 15, NAME_MAX = 100;   // the sub-window's reform_name

using XY = std::pair<double, double>;
// {sub-window name: (x, y)} of the reform places in `part` (reforms_window / unciv_reforms_window), in the
// coordinates of the window country_politics
OrderedMap<QString, XY> slots(const QString &text, const QString &part);
// C++ only: the part's own pieces (its texts, pictures and reform places) in window coordinates — what is shown of the
// game's window. sprite_size: a sprite's size, or None
QRectF extent(const QString &text, const QString &part,
              const std::function<std::optional<QSizeF>(const QString &)> &sprite_size);

// C++ only (the user 2026-11-17: Victoria Universalis' reforms came out "one row to the side", its names "written all
// over"): where the pieces stand is read from the mod's country_politics.gui, not taken from vanilla's. VU puts the
// options UNDER the reform's name (reform_option at x 16, y 40 of a 151-wide sub-window), vanilla to its right (x 166,
// y 9 of 310). A piece the file doesn't give keeps vanilla's number.
struct Layout {                                     // reform_option_window: one option's row
    double tick_x = TICK_X, tick_y = TICK_Y;        //   "selected" (the tick + frame)
    double bar_x = BAR_X, bar_y = BAR_Y;            //   "reform_option" (the option button)
    double opt_x = OPT_X, opt_y = OPT_Y, opt_w = OPT_W, opt_hmax = 16.0;   // its "reform_name"
    double row_h = OPT_H;                           //   the window's size y: one option below the other
    QString opt_font = QStringLiteral("Arial12_black");
};
struct Place {                                      // a reform's sub-window
    double w = ROW_W, name_x = NAME_X, name_y = NAME_Y, name_w = NAME_MAX;
    QString name_font = QStringLiteral("vic_18");
};
Layout layout(const QString &text);
OrderedMap<QString, Place> places_of(const QString &text, const QString &part);

using Option = std::tuple<QString, bool, QString>;   // (option's name, ticked, option's key)

class GameReformView : public QWidget {             // the picture of the game's window with the reforms framed on it
    Q_OBJECT
public:
    explicit GameReformView(QWidget *parent = nullptr);
    std::optional<QImage> img;
    struct Box {
        QRectF r;
        QString name;
        bool mine;
    };
    std::vector<Box> boxes;
    XY offset{0.0, 0.0};
    OrderedMap<QString, QString> issues, names, current;
    OrderedMap<QString, std::vector<Option>> options;
    QStringList missing;
    std::optional<std::pair<QString, QString>> sel, _hover;   // sel: (reform, option or ""); _hover the same
    QRectF crop;                                        // the part shown, in the picture's coordinates (C++ only)
    Layout lay;                                         // the option rows as the mod's file has them (C++ only)
    OrderedMap<QString, Place> place_info;              // each reform place's size and name (C++ only)
    locwin::GameLook *look = nullptr;                   // the game's sprites and fonts (C++ only)
    // img: the window's picture; offset: (x0, y0) of the picture in window coordinates; places: slots(); issues: the
    // mod's reforms of this page {key: group}; names: key -> name; current: key -> what most nations have; options:
    // key -> [(option's name, ticked, key)] — drawn as the game does, the list right of the name
    void set_data(const std::optional<QImage> &img, XY offset, const OrderedMap<QString, XY> &places,
                  const OrderedMap<QString, QString> &issues, const OrderedMap<QString, QString> &names,
                  const OrderedMap<QString, QString> &current, const std::optional<std::pair<QString, QString>> &sel,
                  const OrderedMap<QString, std::vector<Option>> &options = {}, const QRectF &part = {},
                  locwin::GameLook *look = nullptr);
    struct At {
        QString name, option;
        bool mine = false;
    };
    At _at(QPointF pos) const;                          // (reform, option key or "", the mod has that reform) under pos
    QRectF _opt_row(const QRectF &r, long long k) const;
    std::tuple<double, double, double> _scale() const;
    QString _note_text() const;                         // the red note of reforms without a place (C++ only)
    double _note_h() const;                             // its height, every line of it
Q_SIGNALS:
    void picked(const QString &issue, const QString &option);   // an issue (reform) key, one of its options ("" = it)

protected:
    void paintEvent(QPaintEvent *) override;
    void leaveEvent(QEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
};

}  // namespace reformwin
