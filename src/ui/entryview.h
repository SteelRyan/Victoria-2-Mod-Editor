// entryview.py — One editor for the many game files that are lists of named blocks: war goals (common/cb_types.txt),
// leader traits, rebel types, ideologies, national focuses, pop types (poptypes/*.txt), terrain types, crimes, the
// static and triggered modifiers, the wars running at the start (history/wars/*.txt) …
//
// The list left (grouped where the file groups them), the one picked right: its name in the game, every plain value
// (yes / no as a tick box, the rest as a field; ✕ takes one off, a row adds one), every block inside it as text with
// its own Save, and the whole entry as text for anything else. New ones are made as a copy of the one picked; ones of
// the mod's own can be removed. The texts are the editor's (doc.maps["scripts"]): every change is one undoable step,
// written on Apply.
// Ported: the helpers, EntryView (with iconstrips' IconPicker for a `icon = N` field), the specs, GameSettingsView,
// PagesWorkspace, crime_uses.
#pragma once
#include "core/document.h"
#include "core/entryview_data.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/scriptstore.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace widgets { class BadgeButton; }

namespace entryview {

using OptStr = std::optional<QString>;

class EntryView;

struct Spec {
    QString id, how = QStringLiteral("top"), rel, folder;
    QStringList base;
    QString noun, title;
    OptStr example;
    QString intro;
    std::optional<std::vector<std::pair<QString, QString>>> names;   // absent: [("", "Name in the game")]
    bool make_new = true, remove = true, add = true, hide_empty = false;
    QStringList skip;
    std::vector<std::pair<QString, QString>> extra;                    // (key, heading): a block that isn't an entry
    OptStr key;                                                        // how = "file": the entry's key
    std::function<QString(EntryView &, const QString &, const QString &, const QStringList &)> info;
};
extern const Spec TRAITS, TERRAIN, FOCUSES, IDEOLOGIES, STATIC_MODS, TRIGGERED_MODS, CRIMES, NEWS, UNIT_COLOURS,
    CULTURE_GROUPS;
// where the mod's scripts switch a crime on or put it into a province: [(file, the event / decision / invention it
// stands in, "enable_crime" / "add_crime" / "remove_crime")]
std::vector<std::tuple<QString, QString, QString>> crime_uses(const moddata::Mod &mod, scriptstore::ScriptStore &store,
                                                              const QString &key);

// (inherited, in_mod, drop_file, war_sides, _files: core/entryview_data)
OptStr strip_of_field(const QString &rel, const QString &key);   // iconstrips.strip_of_field

struct Entry {
    OptStr grp;
    QString key, rel;
    QStringList path;
};
std::vector<Entry> entries(const Spec &spec, const moddata::Mod &mod, scriptstore::ScriptStore &store);

class EntryView : public QWidget {
    Q_OBJECT
public:
    EntryView(moddata::Mod &mod, const Spec &spec, QWidget *parent = nullptr);
    moddata::Mod &mod;
    Spec spec;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::optional<std::pair<QString, QStringList>> cur;   // (rel, path)
    QLineEdit *search, *new_key = nullptr;
    QTreeWidget *lst;
    QLabel *new_warn = nullptr, *head, *title, *sub, *info, *warn;
    QPushButton *big_btn;
    OrderedMap<QString, QLineEdit *> name_edits;
    QWidget *names_host, *body_host, *body = nullptr;
    QVBoxLayout *body_lay;

    QLabel *_bold(const QString &text, QWidget *parent = nullptr);
    void refresh(document::Document *doc);
    OptStr loc(const QString &key);
    QString nm(const QString &key);
    void _fill_list();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    const Entry *_entry() const;
    void _fill_detail();
    void _open_big();
    QWidget *_values(const QStringList &path, const QString &rel, const QString &key,
                     const std::vector<std::pair<QString, QString>> &plain);
    QPlainTextEdit *_editor(QWidget *parent, const QString &text, long long lines_max = 16);
    QWidget *_block_box(const QString &rel, const QStringList &path, const QString &k);
    QWidget *_whole_box(const QString &rel, const QStringList &path, const QString &key, const QString &inner);
    void _change(const QString &what, const QString &rel, const std::function<QString(const QString &)> &fn);
    void _set(const QString &rel, const QStringList &path, const QString &key, const QString &k, QLineEdit *ed,
              const QString &old);
    void _loc_done(QLineEdit *ed, const QString &suffix);
    bool _removable(const QString &rel, const QStringList &path);
    void _remove(const QString &rel, const QStringList &path, const QString &key);
    void _make_new();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel file, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text
    void fileRemove(const QString &label, const QString &rel);                        // undo label, rel file
    void bigRequested(const QString &rel, const QStringList &path, const QString &noun);   // the entry, large

private:
    std::optional<std::tuple<QString, QStringList, QString>> _sig;
    std::vector<Entry> _entries;
    QPlainTextEdit *_whole = nullptr;
};

// ---------------------------------------------------------------------------------------------- defines.lua
// common/defines.lua: every setting of the game with its value and the note the file has for it.
class GameSettingsView : public QWidget {
    Q_OBJECT
public:
    static const QString REL;
    struct Row {
        QString sec, key, value, note;
        long long line;
    };
    explicit GameSettingsView(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::optional<std::pair<QString, QString>> cur;   // (section, key)
    QLineEdit *search, *value, *note_edit;
    QTreeWidget *lst;
    QLabel *head, *note, *was, *warn;
    QPushButton *set_btn, *note_btn;
    static std::vector<Row> rows(const QString &text);
    void refresh(document::Document *doc);
    void _fill();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    std::optional<Row> _row();
    void _show();
    void _commit();
    void _commit_note();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
};

// ---------------------------------------------------------------------------------------------- a workspace of pages
// A full-window workspace of several pages (a bar of buttons, the page picked below); a page is made when it is first
// opened. (Python: pages = [(key, text, make)]; v.refresh(doc) is duck-typed — here each page brings its refresh.)
struct PageDef {
    QString key, text;
    std::function<QWidget *()> make;
    std::function<void(QWidget *, document::Document *)> refresh;
};

class PagesWorkspace : public QWidget {
    Q_OBJECT
public:
    PagesWorkspace(moddata::Mod &mod, std::vector<PageDef> pages, QWidget *parent = nullptr);
    moddata::Mod &mod;
    std::vector<PageDef> pages;
    document::Document *doc = nullptr;
    QString page;
    OrderedMap<QString, QWidget *> views;
    OrderedMap<QString, widgets::BadgeButton *> seg;
    QStackedWidget *stack;
    QWidget *view(const QString &key);
    void set_counts(const std::map<QString, std::pair<long long, long long>> &counts);   // {page: (errors, warnings)}
    void set_page(const QString &key);
    void refresh(document::Document *doc);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void fileRemove(const QString &label, const QString &rel);
    void bigRequested(const QString &rel, const QStringList &path, const QString &noun);
};

}  // namespace entryview