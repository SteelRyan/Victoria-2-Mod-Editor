// changelog.py — Changelog tab: release notes in the style of the mod's GitHub releases — a title "Ferrum Mare vX.Y.Z"
// and bold category headings with "- " lines under them — built from blocks.
//
// Left, the changes: every Apply records what changed (per script file the events, decisions, units, reforms,
// government forms … added, removed or changed, with the numbers: "attack 4 → 5"; and the edit steps made in the
// other tabs), each with the category of the editor function it came from (Events, Units, Reforms, Pops, Diplomacy …).
//
// Right, the notes: every change added is a block under its category. Blocks are moved up and down (buttons or
// dragged by their handle), given another category (the headings follow by themselves), opened to rewrite their text
// and closed again, or sent back to the changes on the left. Whole categories move up and down and can be renamed.
//
// Kept in ModEditor/changelogs/<mod>/: log.json (the changes and the notes), "<title>.md" for each saved note, and
// changes/ — one text file per Apply with the mod and the scenario noted, plus "All changes.txt" with all of them.
//
// (Python's dicts — the changes, the blocks, the categories, the draft — are pyjson values: shared by reference as
// Python's are, and written to log.json in their own key order.)
#pragma once
#include "core/ordered_map.h"
#include "core/pyjson.h"

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>

class QCheckBox;
class QDateTime;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTextBrowser;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
namespace moddata { class Mod; }
namespace document { class Document; }
namespace scriptstore { class ScriptStore; }

namespace changelog {

using OptStr = std::optional<QString>;
using pyjson::Value;

extern const QStringList FUNCTION_CATS, RELEASE_CATS, CATEGORIES, ORDER;
extern const OrderedMap<QString, QString> TAB_CATS;
extern const QStringList PALETTE;
extern const QString MIME;

QString app_dir();
QString cat_color(const QString &name);

// ---------------------------------------------------------------------- what changed in a script file
using Loc = std::function<OptStr(const QString &)>;          // (empty: no localisation)
QString file_category(const QString &rel, const QString &group = QString());
pyjson::Array script_changes(scriptstore::ScriptStore &store, const Loc &loc);
// steps: (label, tab, touched the script files) → entries; repeats of one thing kept once (its last)
pyjson::Array edit_steps(const std::vector<std::tuple<QString, QString, bool>> &steps);

// ---------------------------------------------------------------------- the notes
QString notes_markdown(const pyjson::Array &sections);
pyjson::Array parse_body(const QString &body);
QString bump(const QString &version);

class NoWheelCombo : public QComboBox {   // a box that doesn't change under the mouse wheel (the wheel scrolls the list)
    Q_OBJECT
public:
    using QComboBox::QComboBox;

protected:
    void wheelEvent(QWheelEvent *e) override;
};

NoWheelCombo *cat_combo(const QString &current, const QStringList &extra = {});
QPushButton *tool_btn(const QString &text, const QString &tip, std::function<void()> fn, int width = 38);

class NotesBoard;
class BlockCard;

class Handle : public QLabel {            // the grip of a block: drag it to move the block
    Q_OBJECT
public:
    explicit Handle(BlockCard *card);
    BlockCard *card;

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    std::optional<QPoint> _press;
};

class BlockCard : public QFrame {         // one change in the notes: its text, its category, and its buttons
    Q_OBJECT
public:
    BlockCard(NotesBoard *board, const Value &sec, const Value &block, const QStringList &extra_cats);
    NotesBoard *board;
    long long bid;
    QPlainTextEdit *editor = nullptr;     // (Python: only when open — hasattr(w, "editor"))
    QLabel *text = nullptr;
    NoWheelCombo *cat;
    void _close();

protected:
    void mouseDoubleClickEvent(QMouseEvent *e) override;
};

class SectionHead : public QWidget {      // a category's heading: its name (rename it here) and ▲ ▼ for all of it
    Q_OBJECT
public:
    SectionHead(NotesBoard *board, const Value &sec, const QStringList &extra_cats);
};

class NotesBoard : public QWidget {       // the notes as blocks under their categories (the model: the draft's sections)
    Q_OBJECT
public:
    explicit NotesBoard(QWidget *parent = nullptr);
    Value sections{pyjson::Array{}};      // (a list shared with the draft, as in Python)
    std::optional<long long> open_id;
    long long next_id = 1;
    QScrollArea *scroll = nullptr;
    struct Row {
        QWidget *w;
        qsizetype si;
        std::optional<qsizetype> bi;
    };
    std::vector<Row> _rows;
    std::optional<std::tuple<long long, qsizetype, qsizetype>> _drop;
    QVBoxLayout *lay;
    QFrame *marker;

    pyjson::Array &secs() { return sections.arr(); }
    void set_sections(const Value &sections);
    std::optional<std::pair<qsizetype, qsizetype>> _find(long long bid);
    Value _section(const QString &name, bool create = true);   // (null: none)
    void _done();
    Value add_block(const QString &text, const QString &category, const Value &entry = Value(), bool quiet = false);
    void move_block(long long bid, int delta);
    void place_block(long long bid, qsizetype si, qsizetype index);
    void set_category(long long bid, const QString &name);
    void move_section(const QString &name, int delta);
    void rename_section(const QString &name, const QString &new_);
    void open_block(long long bid);
    void _keep_open();
    void close_block(long long bid, const QString &text);
    void send_back(long long bid);
    void rebuild();
    std::optional<std::tuple<qsizetype, qsizetype, int>> _target(int y);
    void apply_drop();
Q_SIGNALS:
    void edited();
    void sentBack(const pyjson::Value &block);   // the block taken out of the notes

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class ChangelogWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ChangelogWorkspace(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    QString dir;
    Value data;                                // {"entries": [...], "draft": {...}, …}
    pyjson::Array _pending;
    QSplitter *split;
    QCheckBox *hide_used, *steps_box;
    QTreeWidget *lst;
    NoWheelCombo *move_to, *own_sec;
    QLineEdit *own, *version, *title;
    QLabel *folder_note, *status;
    NotesBoard *board;
    QTextBrowser *preview;
    QTimer *_save_timer;
    QHash<QTreeWidgetItem *, Value> _item_data;   // (Python: it.data(0, Qt.UserRole) — the entry)

    pyjson::Array &entries() { return data.obj()[QStringLiteral("entries")].arr(); }
    void _load();
    void _write();
    QString _default_version();
    QString _mod_title();
    void _show_draft();
    void _version_edited(const QString &v);
    void _notes_changed();
    QString markdown();
    void _render();
    void record(const pyjson::Array &changes, const QString &scenario = QString());
    void _export(const pyjson::Array &entries, const QDateTime &now, const QString &scenario);
    void refresh(document::Document *doc, const pyjson::Array &pending = {});
    void _fill();
    void _add_items(const QList<QTreeWidgetItem *> &items);
    void _add_all();
    void _sent_back(const Value &block);
    void _forget();
    void _add_own();
    void _open_folder();
    void _copy_title();
    void _copy_notes();
    OptStr _save_md();
    void _next();
Q_SIGNALS:
    void message(const QString &text);
};

}  // namespace changelog
