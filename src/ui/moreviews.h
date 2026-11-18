// moreviews.py, its pages (kind D; the data half is map\moreviews_data): the smaller pages of Warfare, Game rules and the
// Library. The helpers (bold, two_panes, set_in, cut_node), _Base, Warfare → Wars at the start (WarsView), Game rules →
// Start dates (StartDatesView), Timed events (TimedEventsView), Flags & variables (IndexView); Library → Pictures,
// Music (_LibraryPage: PicturesPage, MusicPage) and Almanacs (AlmanacPage).
#pragma once
#include "core/ordered_map.h"
#include "core/script.h"
#include "map/moreviews_data.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace document { class Document; }
namespace moddata { class Mod; }
namespace refs { class RefIndex; }
namespace scriptstore { class ScriptStore; }
namespace widgets { class Section; }

class QTableWidget;
namespace moreviews {

using OptStr = std::optional<QString>;

extern const QStringList PIC_EXT, MUSIC_EXT;
extern QString app_dir_override;                   // (C++ only: a check puts the library folder in its scratch copy)
QString app_dir();                                 // the folder of the editor's exe
QString library_dir(const QString &kind);          // the user's library folder for pictures / music (made on first use)

QLabel *bold(const QString &text, QWidget *parent = nullptr);

class Base : public QWidget {                     // (Python: _Base)
    Q_OBJECT
public:
    explicit Base(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    QTreeWidget *lst = nullptr;                   // (two_panes)
    widgets::Section *left_sec = nullptr;
    OptStr loc(const QString &key) const;
    void refresh(document::Document *doc);
    virtual void fill() {}
    void emit_text(const QString &what, const QString &rel, const QString &nw);
    QLabel *warn_label();
    QVBoxLayout *two_panes(const QString &title, const QString &intro, const QStringList &cols, const QString &key);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
    void fileRemove(const QString &label, const QString &rel);
};

QString set_in(const QString &text, const script::Node &node, const QString &key, const QString &value);
QString cut_node(const QString &text, const script::Node &n);

class StartDatesView : public Base {
    Q_OBJECT
public:
    static const QString REL;
    explicit StartDatesView(moddata::Mod &mod, QWidget *parent = nullptr);
    long long cur = 0;
    std::vector<std::pair<QString, QLineEdit *>> ed;
    QLabel *pops_note, *warn;
    QLineEdit *new_date;
    QPushButton *rm;
    std::vector<script::Node> nodes();
    std::vector<std::vector<long long>> pop_dates() const;
    OptStr pops_for(const QString &date) const;
    void fill() override;
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _show(const std::vector<script::Node> &ns);
    void _set(const QString &k, QLineEdit *w);
    void _new();
    void _remove();
};

class WarsView : public Base {
    Q_OBJECT
public:
    explicit WarsView(moddata::Mod &mod, QWidget *parent = nullptr);
    OptStr cur;
    std::optional<War> w;
    QString w_of;                                 // (Python: w["_of"] — the text it was read from)
    QLineEdit *new_name, *name, *start, *end;
    QLabel *head, *file_note, *warn;
    std::vector<std::pair<QString, std::pair<QListWidget *, QComboBox *>>> side_box;
    QListWidget *goals;
    QComboBox *cb_box, *actor, *receiver;
    QPushButton *save_btn, *rm_btn;
    widgets::Section *form;
    QPlainTextEdit *raw;

    QComboBox *_nation_box();
    QString nm(const QString &tag) const;
    QStringList rels() const;
    void fill() override;
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _show();
    void _side_add(const QString &side);
    void _side_rm(const QString &side);
    void _goal_add();
    void _goal_rm();
    void _save();
    void _save_raw();
    void _new();
    void _remove();

private:
    QStringList &side_of(const QString &side) { return side == u"att" ? w->att : w->def; }
    std::pair<QListWidget *, QComboBox *> box_of(const QString &side) const;
};

extern const OrderedMap<QString, QString> HOOKS;

class TimedEventsView : public Base {
    Q_OBJECT
public:
    static const QString REL;
    explicit TimedEventsView(moddata::Mod &mod, std::function<OptStr(const QString &)> event_name = {},
                             QWidget *parent = nullptr);
    std::function<OptStr(const QString &)> event_name;
    OptStr cur;
    QLabel *head, *sub, *warn;
    QTreeWidget *rows;
    QLineEdit *eid, *weight;

    std::vector<script::Node> hooks();
    std::vector<std::pair<QString, QString>> lines(const script::Node &n);   // (weight, event id)
    void fill() override;
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _show();
    void _add();
    void _remove();
};

extern const OrderedMap<QString, QString> KIND_NAME;

class IndexView : public Base {
    Q_OBJECT
public:
    IndexView(moddata::Mod &mod, std::function<refs::RefIndex *()> get_refs,
              std::function<void(const QString &, long long)> jump, QWidget *parent = nullptr);
    std::function<refs::RefIndex *()> get_refs;
    std::function<void(const QString &, long long)> jump;
    refs::RefIndex *refs = nullptr;
    QLineEdit *search;
    QComboBox *kind, *show_box;
    QLabel *count, *detail;

    void fill() override;
    void _fill();
    void _picked(QTreeWidgetItem *it, QTreeWidgetItem *prev);
    void _go(QTreeWidgetItem *it, int col);
};

// ---------------------------------------------------------------------------------------------- the library pages
using AddFn = std::function<void(const QString &, const OrderedMap<QString, Asset> &)>;

class LibraryPage : public QWidget {              // (Python: _LibraryPage) left the user's folder, right the mod's
    Q_OBJECT
public:
    LibraryPage(moddata::Mod &mod, AddFn add, QWidget *parent = nullptr);
    moddata::Mod &mod;
    AddFn add;
    document::Document *doc = nullptr;
    QString folder;
    QLabel *status = nullptr;
    void refresh(document::Document *doc);
    virtual void fill() {}
    void open_folder();
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);
    void nameEdit(const QString &key, const QString &text);
};

class PicturesPage : public LibraryPage {
    Q_OBJECT
public:
    using UsedFn = std::function<std::optional<QSet<QString>>()>;   // None while the scripts aren't read
    PicturesPage(moddata::Mod &mod, AddFn add, UsedFn used_pictures = {}, QWidget *parent = nullptr);
    UsedFn used_pictures;
    QListWidget *lib, *have;
    QComboBox *target;
    QCheckBox *fit_box, *only_unused;
    QLabel *mod_note;
    QListWidget *_grid();
    void fill() override;
    void _add();
};

class MusicPage : public LibraryPage {
    Q_OBJECT
public:
    MusicPage(moddata::Mod &mod, AddFn add, QWidget *parent = nullptr);
    QListWidget *lib, *have;
    QLabel *mod_note;
    void fill() override;
    void _add();
};

QString table(const QStringList &head, const std::vector<QStringList> &rows);

// The almanac's sheets as tables (C++ only, 2026-11-15): the same rows the text sheets are written from, for the
// table on the page and the almanac Apply writes into the mod.
struct AlmanacSheet {
    QString title, what;                          // its name ("Army unit stats"), its heading ("army unit stats …")
    QStringList head;
    std::vector<QStringList> rows;
};
QString almanac_name(const moddata::Mod &mod, document::Document *doc, const QString &key);   // a key's own name
std::vector<AlmanacSheet> almanac_sheets(moddata::Mod &mod, scriptstore::ScriptStore *st,
                                         const std::function<QString(const QString &)> &nm);
QString almanac_now();                            // the time stamp (AlmanacPage::now_override in the checks)
// one page with every sheet as a table: "<mod name> almanac.html"
QString almanac_html(const QString &mod_name, const QString &scen, const QString &now,
                     const std::vector<AlmanacSheet> &sheets);
QString almanac_file(const QString &folder, const QString &mod_name);
// writes it into `folder` (the mod's own) -> (the file, "") or ("", what went wrong)
std::pair<QString, QString> write_almanac(moddata::Mod &mod, document::Document *doc, const QString &folder);

class AlmanacPage : public Base {
    Q_OBJECT
public:
    explicit AlmanacPage(moddata::Mod &mod, QWidget *parent = nullptr);
    QString dir;
    QComboBox *pick;
    QLabel *status;
    QLabel *sheet_head;                           // the sheet's heading over the table
    QTableWidget *grid;                           // the sheet as a table: fixed columns (the user 2026-11-15)
    OrderedMap<QString, QString> sheets;          // the text sheets "Write all the almanacs" writes
    std::vector<AlmanacSheet> tabs;
    static QString now_override;                  // (C++ only: a check fixes the time stamp of the sheets)
    QString nm(const QString &k) const;
    OrderedMap<QString, QString> build();
    void fill() override;
    void _show();
    void _write();
    void _write_mod();                            // the almanac into the mod now, without an Apply
    void _open();
    void _copy();                                 // Ctrl+C: the picked cells, tab-separated (pastes into a sheet)
};

}  // namespace moreviews
