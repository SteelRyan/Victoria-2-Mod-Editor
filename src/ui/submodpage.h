// submodpage.py — Tools → Submods & 4K: what a submod is and how this mod stands (a mod, or a submod of which mod, and
// where its changes go), and the maker of the 4K interface submod (fourk). A submod is made and opened in the start
// window ("New submod of the selected mod…"); every page of the editor then works on it, and Apply writes only what
// differs from the parent mod into the submod's own folder (submods).
//
// C++ only (2026-11-09, the user's wish): the 4K screen — the game's screen at 3840 × 2160 as the submod will show it,
// with a slider each for the top bar, the unit panel (an army picked), the army counters and the province names
// (everything else × 2); "Make the 4K submod" makes it with those sizes (fourk::Sizes). Seen in the game with the top
// bar × 1.5, the unit panel × 1, the counters × 1.5, the names × 2.
#pragma once
#include "map/checksum.h"
#include "ui/gameview.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace fourk { struct Sizes; }
namespace locwin { class GameLook; }
namespace mapmarkspage { struct Parts; }
namespace mapview { class MapData; }
namespace widgets { class StepSlider; }

namespace submodpage {

class SubmodWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit SubmodWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    ~SubmodWorkspace() override;
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    double _armed = 0.0;
    bool _running = false, _stop = false;
    QLabel *state, *holds, *where, *warn, *progress, *result, *ck_files, *ck_result;
    QLineEdit *folder;
    QPushButton *make_btn, *stop_btn, *ck_btn, *code_btn, *ck_stop;
    QProgressBar *bar;
    QTimer *_ck_timer;
    // a mod of the game's mod folder: (its .mod file, user_dir, folder, replace_path list)
    using Entry = std::tuple<QString, std::optional<QString>, QString, QStringList>;
    struct Done {
        QString what;
        checksum::Result r;
        QString code;                                // (C++: the letters worked out for a new settings folder)
    };
    std::unique_ptr<checksum::Probe> _probe;
    std::vector<std::pair<QString, std::vector<Entry>>> _todo;
    std::vector<Done> _done;

    QString real_root() const;                       // the folder the game reads this mod from
    QString mods_dir() const;
    QStringList chain_names() const;
    void refresh(document::Document *doc);
    std::optional<QString> known_4k() const;         // the folder of a 4K submod this mod has already
    std::optional<QString> target() const;
    void _show();
    std::map<QString, Entry> _entries() const;
    std::vector<std::pair<QString, std::vector<Entry>>> ck_sets() const;   // what to compare, lowest first
    void _show_ck();
    checksum::Prediction _code_of(const std::vector<Entry> &rows) const;   // (C++: the game's own checksum)
    void _codes();
    void _measure();
    void _next_probe();
    void _measure_tick();
    void _measure_stop();
    void _ck_report(const std::optional<QString> &running = std::nullopt);
    void _ask_stop();
    void _make();
    void run(const QString &target);                 // make the 4K submod in `target` and its .mod file next to it

    // the 4K screen (C++ only)
    mapview::MapData *md = nullptr;                  // (set by the window: the screen draws the map)
    gameview::GameView *screen;
    widgets::StepSlider *sz_top, *sz_unit, *sz_marks, *sz_names;
    QLabel *sz_note;
    fourk::Sizes sizes() const;                      // the sliders
    void set_sizes(const fourk::Sizes &s);
    void _screen_read();                             // the map, the windows at 1 ×, the counter (on refresh)
    void _screen();                                  // the sliders' sizes on the screen
Q_SIGNALS:
    void message(const QString &text);

private:
    QLabel *_bold(const QString &text);
    bool _quit_hooked = false;
    std::shared_ptr<gameview::World> _gw;
    std::unique_ptr<locwin::GameLook> _look;
    std::shared_ptr<mapmarkspage::Parts> _parts;
    QString _look_key;                                     // (what _look / _wins / _parts were read for: doc, undo, texts)
    std::vector<std::pair<int, gameview::Overlay>> _wins;   // (which slider: 0 top bar, 1 unit panel, 2 × 2; the window)
    QString _sizes_of;                               // the 4K submod whose sizes the sliders were set from
};

}  // namespace submodpage
