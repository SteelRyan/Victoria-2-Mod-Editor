// app.py — Application start-up: pick a mod and scenario, load the map, open the main window.
#pragma once
#include "core/moddata.h"

#include <QApplication>
#include <QDialog>
#include <QIcon>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QWidget>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QFrame;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace mapview {
class MapData;
}

namespace app {

using moddata::Mod;
using moddata::ModEntry;
using OptStr = std::optional<QString>;

extern const QStringList STEAM_MOD_DIRS;

QString app_dir();                            // the folder of ModEditor.exe (Python, run as .exe: sys.executable's)
bool has_mods(const QString &path);
QString guess_mods_dir(QSettings &settings);
QIcon make_icon();

class NoWheelFilter : public QObject {        // the mouse wheel never changes dropdowns, number fields or sliders
    Q_OBJECT
public:
    using QObject::QObject;
    bool eventFilter(QObject *obj, QEvent *ev) override;
};

class Splash : public QWidget {
    Q_OBJECT
public:
    Splash();
    QLabel *msg;
    void say(const QString &text);
};

class OpenDialog : public QDialog {           // step 1: which mod.  Step 2: which scenario (history/pops/<folder>)
    Q_OBJECT
public:
    OpenDialog(QSettings &settings, const QString &mods_dir, const OptStr &error = std::nullopt,
               QWidget *parent = nullptr);
    QSettings &settings;
    QString mods_dir;
    OptStr base;
    std::shared_ptr<Mod> mod;
    std::vector<ModEntry> mods;
    std::optional<ModEntry> entry, entry_sel;
    QLabel *dir_label, *sc_hint, *err;
    QListWidget *mod_list, *sc_list;
    QPushButton *new_btn, *sub_btn, *del_btn, *ok_btn;
    QPushButton *base_btn = nullptr;             // "New mod from the base game…" (C++ only)
    // C++ only (the user 2026-11-15): the update note and its switch (update.h); hidden without a Checker
    QFrame *upd_strip = nullptr;
    QLabel *upd_label = nullptr;
    QCheckBox *upd_box = nullptr;
    void show_update();

    // C++ only (a tester and the user 2026-11-17): the base game from a path of its own, more than one mod folder
    // (the list says "all below are from <path>"), and mods dragged between the folders and the safeguards (a move;
    // Ctrl: a copy — modfolders.h).
    QString base_set;                            // the base game picked by hand ("" = found next to a mod folder)
    QStringList more_dirs;                       // the mod folders added below the first
    std::vector<QString> group_of;               // (parallel to mods) the mod folder the mod is listed under
    QLabel *game_label = nullptr, *drop_note = nullptr;
    QPushButton *game_btn = nullptr, *game_auto = nullptr, *add_dir_btn = nullptr;
    void choose_base();
    void add_dir(const QString &path = QString());   // (a path: no file dialog — the checks)
    void remove_dir(const QString &path);
    QString target_dir(QListWidgetItem *target) const;   // where a drop on that row puts a mod
    bool drop_on(QListWidgetItem *src, QListWidgetItem *target, bool copy);

    void choose_dir();
    void show_error(const QString &text);
    void fill_mods();
    void mod_changed(QListWidgetItem *cur);
    void fill_scenarios(const OptStr &select = std::nullopt);
    void new_scenario();
    void new_submod();
    void new_base_mod();
    void delete_submod();                     // to the Recycle Bin; no pop-up: a second click within 10 s does it
    OptStr scenario() const;
    void try_accept();

private:
    static QLabel *_lab(const QString &text, const QString &obj);
    const ModEntry *_entry_of(QListWidgetItem *it) const;
    std::optional<double> _del_armed;
    // C++ only (the user 2026-11-14): a mod's safeguards (modcopy) under it, in a group that folds open and shut
    QSet<QString> _open_safe;                    // the mods (normcase folder) whose group is open
    void _toggle_safe(QListWidgetItem *head);
    void _sep_texts();                           // (C++ only) the folder lines: the path shortened to the width

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
};

// a submod is edited through its working copy (submods.py) -> (Mod on the working copy, None) or (None, the error)
std::pair<std::shared_ptr<Mod>, OptStr> open_submod(const std::vector<ModEntry> &mods, const ModEntry &entry,
                                                    const OptStr &base, const OptStr &work_root = std::nullopt,
                                                    const OptStr &backup_root = std::nullopt);
// full load of mod data + map, with a splash -> (MapData or None, the error)
std::pair<std::shared_ptr<mapview::MapData>, OptStr> load_mod(Mod &mod);

// errors the window would swallow (the .exe has no console): written to errors.log next to ModEditor.exe
void log_error(const QString &text);

class App : public QApplication {             // (Python: sys.excepthook — an exception in a slot is logged, not fatal)
    Q_OBJECT
public:
    App(int &argc, char **argv);
    bool notify(QObject *receiver, QEvent *ev) override;
};

int main(int argc, char *argv[]);

}  // namespace app
