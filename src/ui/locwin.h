// locwin.py — Scripts → ✎ Texts & preview: the texts of the event or decision picked — its title, its description, the
// words on each option — written with the game's colour codes, and shown at once the way the game shows them.
// Left, a field per text: colour buttons (§Y … §!), new line (\n), suggestions while typing after § $ @ \ (the words the
// game fills in, the nations' flags), and what is odd about the codes, in amber. Right, the window as the game draws
// it: its own frame, picture, fonts, colours, word wrap — for a decision also the hover text that shows a long
// description whole. A change is a pending edit of the localisation (one undo step each).
// (GameLook and the helpers _xy / _num also serve the Politics page's "As the game's window", shortcuts.project.)
#pragma once
#include "core/events.h"
#include "core/moddata.h"
#include "core/ordered_map.h"
#include "core/scriptstore.h"
#include "core/script.h"
#include "map/fourk.h"
#include "map/gamefont.h"

#include <QImage>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QVBoxLayout;
namespace document { class Document; }

namespace locwin {

using OptStr = std::optional<QString>;
using XY = std::pair<double, double>;

bool NUM(const QString &s);                          // gamefont.NUM
OptStr _val(const script::Node &node, const QString &name);          // gamefont._val
const script::Node *_kid(const script::Node &node, const QString &name);   // gamefont._kid
XY _xy(const script::Node &node, const QString &name);
double _num(const script::Node &node, const QString &name, double dflt = 0.0);

class GameLook {                                     // what a preview needs of the game's files
public:
    GameLook(const moddata::Mod &mod, scriptstore::ScriptStore *store);
    const moddata::Mod &mod;
    scriptstore::ScriptStore *store;
    gamefont::Fonts fonts;
    struct Kind {                                    // sprite -> (kind, its own size, corner size)
        QString kind;
        std::optional<XY> size, border;
        QString second;                              // (C++ only) a progress bar's empty-part texture (texturefile2)
    };
    std::optional<std::map<QString, std::pair<QString, long long>>> sprites;   // name -> (texture, frames)
    std::map<QString, Kind> kinds;
    std::unique_ptr<fourk::Files> files;
    // C++ only (Building types, 2026-11-16): what the editor holds but has not written yet, shown as if it were —
    // pictures {texture path as a .gfx names it, lower case with '/': picture} and .gfx files only in the editor so far.
    // Set before the first sprite() call.
    std::map<QString, QImage> fresh;
    QStringList more_gfx;
    std::optional<QImage> picture(const QString &path);
    std::optional<QImage> sprite(const OptStr &name);
    std::optional<XY> stretches(const OptStr &name);
    std::optional<QImage> sprite_fit(const OptStr &name, double w, double h);
    // (C++ only, 2026-11-18) a progress bar ("progressBarType") as the game shows it: its definition's size, the filled
    // texture over the first `full` part, the empty texture after it, lying flat (left = empty end is right); none when
    // the sprite is no progress bar
    std::optional<QImage> bar(const OptStr &name, double full);
    const std::map<QString, const script::Node *> &gui(const QString &rel);   // {name (lower case): node}
    OptStr find_picture(const QString &sub, const QString &name) const;

private:
    void _load_sprites();
    std::map<QString, std::optional<QImage>> _pix;
    std::map<std::tuple<QString, long long, long long>, std::optional<QImage>> _fit;
    std::map<QString, std::pair<script::Nodes, std::map<QString, const script::Node *>>> _gui;
};

// The words the game fills in ($NAME$) — the Victoria 2 wiki's list ("Localisation"); what the common ones stand for
// in an event or decision is from the game's own texts.
extern const OrderedMap<QString, QString> MEANING;
extern const QStringList VARS;
struct WindowDef {                                   // kind -> (file, window, option button, option start, option offset)
    QString file, window, button, start, offset;
};
extern const OrderedMap<QString, WindowDef> WINDOWS;

using Samples = OrderedMap<QString, QString>;
using Notes = std::vector<std::pair<QString, QString>>;   // [(severity, sentence)]
// an event window as the game lays it out -> (QImage, [notes])
std::pair<std::optional<QImage>, Notes> render_event(GameLook &look, const QString &kind, const QString &title,
                                                     const QString &desc, const QStringList &options,
                                                     const OptStr &picture = std::nullopt, const Samples &samples = {});
// a decision's entry in the Politics window, and under it the hover text of its description
std::pair<std::optional<QImage>, Notes> render_decision(GameLook &look, const QString &title, const QString &desc,
                                                        const OptStr &picture = std::nullopt, const Samples &samples = {});

class TextsWindow;

class CodeEdit : public QPlainTextEdit {
    // A text of the game: suggestions after § $ @ \, Enter writes the game's line break.
    Q_OBJECT
public:
    explicit CodeEdit(TextsWindow *win, QWidget *parent = nullptr);
    TextsWindow *win;
    QString _at;
    QListWidget *pop;
    void set_text(const QString &text);
    QString value() const;
    void insert(const QString &text, int back = 0);
    void wrap(const QString &code);              // the marked words in a colour: §Y … §!
    struct WordAt {                              // (trigger, what was typed after it, how many characters that is)
        QString kind, typed;
        int n;
    };
    std::optional<WordAt> _word();
    void _suggest();
    void _take(QListWidgetItem *it);
Q_SIGNALS:
    void committed();                            // the field was left with another text than it had

protected:
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
};

class Field : public QWidget {
    // One text: what it is, its key, the buttons for the codes, the field, what is odd about it.
    Q_OBJECT
public:
    Field(TextsWindow *win, const QString &title, QWidget *parent = nullptr);
    TextsWindow *win;
    QString key;
    bool in_script = false, semicolon_ok = false;
    QLabel *title, *key_lbl, *note;
    CodeEdit *edit;
    void set(const QString &key, const QString &text, int rows = 3, const QString &hint = {});
    void show_notes(const Notes &notes);
Q_SIGNALS:
    void changed();
    void committed(const QString &key, const QString &text);
};

// Is this "key" really the text itself, written into the script (title = "Give Sanctions Menu")? A key is one word of
// letters, digits, _ and dots.
bool is_literal(const QString &key);
// The script with the text of a title / desc / option name replaced, inside the entry at span. what: "title" | "desc"
// | "name" (the nth option). -> new script text or None
OptStr set_in_script(const QString &text, const std::pair<qsizetype, qsizetype> &span, const QString &what,
                     std::optional<long long> nth, const QString &old, const QString &new_);

class TextsWindow : public QWidget {
    Q_OBJECT
public:
    explicit TextsWindow(moddata::Mod &mod, QWidget *parent = nullptr);
    moddata::Mod &mod;
    document::Document *doc = nullptr;
    std::optional<events::Entry> entry;          // (a copy: Python keeps the object, which outlives a reparse)
    std::unique_ptr<GameLook> look;
    QLabel *head, *sub, *pic_note, *view, *notes;
    QWidget *fields_host;
    QVBoxLayout *fl, *ll;
    QComboBox *nation_box, *pic_box;
    std::function<QStringList(bool)> picture_names;   // set by the window: fn(is_event) -> [names]
    std::function<void(const QString &)> set_picture; // set by the window: fn(name)
    std::vector<Field *> fields;
    std::optional<QImage> _img;

    QString loc(const QString &key) const;
    const OrderedMap<QString, long long> &usage();   // {WORD: how often the game's and the mod's texts use $WORD$}
    QStringList known_vars();
    struct Suggestion {                          // (what the list shows, what is written, colour or None)
        QString text, put;
        std::optional<gamefont::Rgb> colour;
    };
    std::vector<Suggestion> suggestions(const QString &kind, const QString &typed);
    Samples samples() const;
    void refresh(document::Document *doc, const events::Entry *entry = nullptr);   // an edit, an undo, another entry
    void _pick_picture();
    void _commit(const QString &key, const QString &text);
    void _show();
    void _fit();
Q_SIGNALS:
    void nameEdit(const QString &key, const QString &text);                     // localisation key, text
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // a text that stands in the script

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    std::optional<OrderedMap<QString, long long>> _usage;
    QTimer *_tick;
};

}  // namespace locwin
