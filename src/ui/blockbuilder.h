// blockbuilder.py — The block builder — events and decisions put together from blocks, like Scratch: every part of an
// event is a card with a plain question (WHEN can it happen? HOW OFTEN? WHAT happens RIGHT AWAY? Which CHOICES are
// there?), and inside the cards lie blocks — one sentence each, with the values picked from what the mod really has
// (nations, provinces, flags, modifiers, reforms …), so what is built is always script the game understands. The blocks
// are pieces that clip into each other (blockshapes): a piece is only as long as its sentence, a piece that holds
// others is a C with a mouth, and the notches and bumps show what fits where. The list on the left shows every block
// as the piece it is: it is dragged from there into a card (or picked with a card's ＋), moved by holding it anywhere,
// taken off with ✕ or by dragging it back onto the list; a block can only be put where it is legal (a condition among
// conditions, something about a province only where a province is meant).
//
// The blocks ARE the script: every change is written into the file's text at once (one undo step of the main window),
// and the script it makes is shown beside the blocks. Lines the builder doesn't know are kept as written.
//
// (Python's item dicts are blockcode::PItem here: held by reference, id(item) is the pointer; a list's identity is
// &item->items — so a stack holds the item whose list it shows.)
#pragma once
#include "core/blockcode.h"
#include "ui/blockshapes.h"

#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
namespace document { class Document; }
namespace events { class EventDB; }
namespace moddata { class Mod; }
namespace refs { class RefIndex; }
namespace scriptedit { class ScriptHighlighter; class EditorHost; }
namespace scriptstore { class ScriptStore; }
namespace widgets { class WrapRow; }

namespace blockbuilder {

using OptStr = std::optional<QString>;
using blockcode::PItem;
using blockcode::PList;
using blockcode::PPtr;
using blockcode::Spec;

extern const QString MIME;
extern const QSet<QString> HANDLED_EVENT, HANDLED_DECISION;
const Spec &WEIGHT();
QString rgb(const QString &kind, double a = 1.0);

struct Ctx {                                  // where blocks are: conditions or effects, about a nation / province / pop
    QString mode, scope;
    bool is_list = false;
    QString name() const;
};

QString slot_type(const Spec &spec, const QString &name);
int slot_of(const QString &scope);            // SLOT.get(scope, SLOT["c"])

class BlockBuilder;
class BlockW;
class ContainerW;

// ---------------------------------------------------------------------------------------------- value editors
class LazyCombo : public QComboBox {
    // A picker over a long list (nations, cultures …): typing finds, the list is filled when first needed.
    Q_OBJECT
public:
    using Get = std::function<std::optional<blockcode::Choices>()>;
    LazyCombo(Get get_choices, const QString &value, bool free = false, std::function<QString(const QString &)> name_of = {},
              QWidget *parent = nullptr, int width = 210, bool optional = false);
    Get get_choices;
    bool free_;
    QString value;
    bool optional;
    void fit();
    void _fill();
    void showPopup() override;
Q_SIGNALS:
    void picked(const QString &v);

protected:
    void focusInEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    bool _filled = false;
    int _max;
    void _set(const QString &v);
    void _activated(int i);
    void _typed();
    void _mark(bool ok);
};

class ValueEdit : public QLineEdit {
    // A number, a province number, a text: checked when typed; a value the game wouldn't take isn't written.
    Q_OBJECT
public:
    ValueEdit(std::shared_ptr<blockcode::Env> env, const QString &vtype, const QString &value, QWidget *parent = nullptr,
              bool optional = false);
    std::shared_ptr<blockcode::Env> env;      // (Python holds the Env it was made with)
    QString vtype, value;
    bool optional;
    void fit();
Q_SIGNALS:
    void picked(const QString &v);

protected:
    void focusInEvent(QFocusEvent *e) override;

private:
    void _tip();
    void _done();
};

QWidget *make_editor(BlockBuilder *b, const QString &vtype, const QString &value,
                     std::function<void(const QString &)> on_change, QWidget *parent, const OptStr &issue = std::nullopt,
                     bool optional = false);

// ---------------------------------------------------------------------------------------------- blocks
class Piece : public QWidget {
    // A piece: a head with its sentence, maybe a mouth holding more pieces — painted in its shape (blockshapes). It is
    // only as wide as its head (or what is in its mouth).
public:
    explicit Piece(QWidget *parent, const QString &kind = QStringLiteral("raw"));
    QString kind;
    std::vector<int> tops, bottoms;
    QString top_mode = QStringLiteral("eff"), bottom_mode = QStringLiteral("eff");
    std::optional<std::pair<std::vector<int>, QString>> inner;   // ([places], kind of bump) of the mouth
    bool wrong = false;
    QWidget *body = nullptr;
    QWidget *head;
    QVBoxLayout *head_lay;
    void set_body(QWidget *w, const std::pair<std::vector<int>, QString> &inner_);
    QPainterPath shape() const;
    virtual bool picked() const { return false; }

protected:
    void paintEvent(QPaintEvent *e) override;
    QVBoxLayout *_v;
};

class BlockW : public Piece {
    // One block of the script: a condition, an effect, a block holding others, a note.
public:
    BlockW(BlockBuilder *b, ContainerW *cont, PPtr it);
    BlockBuilder *b;
    ContainerW *cont;
    PPtr it;
    const Spec *spec = nullptr;
    widgets::WrapRow *row;
    QLineEdit *note_edit = nullptr;
    ContainerW *body_c() const;               // the mouth as a stack (Python's self.body)
    bool picked() const override;
    void start_drag();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;

private:
    std::optional<QPoint> _press;
    void _note(QWidget *host);
    void _say(QWidget *host, const QString &text, bool bold = false);
    void _sentence(QWidget *host, const Spec &spec);
    void _set_field(const QString &f, const QString &val, const QString &vt);
    void _set_key(const QString &val);
    void _event_editors(QWidget *host);
    void _raw(QWidget *host, bool is_block);
};

constexpr int LAZY_FROM = 600;    // an entry with more lines than this is opened folded …
constexpr int LAZY_INSIDE = 30;   // … a stack with more than this inside starts closed (a click opens it) …
constexpr int LAZY_STEP = 60;     // … and a long stack shows this many blocks at a time

long long lines_inside(const PList &items);

class ContainerW : public QWidget {
    // A stack of pieces (the inside of a card, or the mouth of a piece): they are added, dropped and reordered here.
    // In a card it is a base plate with the bump the first piece clips onto; in a mouth the piece around it draws the
    // bump.
public:
    ContainerW(BlockBuilder *b, PPtr owner, const Ctx &ctx, QWidget *parent, const QSet<QString> &skip = {},
               const QString &hint = {}, const QSet<QString> &skip_blocks = {}, std::function<void()> on_first = {});
    BlockBuilder *b;
    PPtr owner;                               // the item whose list this is (it lives as long as the stack)
    PList &items() const { return owner->items; }
    Ctx ctx;
    QSet<QString> skip, skip_blocks;
    QString hint;
    std::function<void()> on_first;           // called before the first block goes in (the part joins the script)
    bool top;
    blockshapes::PieceStack *lay;
    QFrame *line;
    std::vector<BlockW *> blocks;
    std::optional<int> _limit;
    PList shown() const;
    void rebuild();
    void add_note();
    void _grew();
    void take(PPtr it);
    void remove(PPtr it);
    void remake(BlockW *bw);
    void join();
    void insert(const PPtr &it, std::optional<qsizetype> index = std::nullopt);
    int _index(double y) const;

protected:
    void paintEvent(QPaintEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    void _open_lazy();
    void _more_lazy();
    void _show_line(int i);
};

class Factor;
class FactorCard : public Piece {
    // "… × N when all of this is true": a piece with a mouth for conditions.
public:
    FactorCard(Factor *fac, PPtr m);
};

class Factor : public QFrame {
    // A base number and "… × N when …" pieces (mean time to happen, AI chance, AI will do).
public:
    Factor(BlockBuilder *b, PPtr owner, const QString &scope, QWidget *parent, const QString &base_key,
           const QString &base_text, const QString &more_text, const QString &hint);
    BlockBuilder *b;
    PPtr owner;
    PList &items() const { return owner->items; }
    QString scope, base_key, base_text, more_text, hint;
    QVBoxLayout *lay;
    void rebuild();
    void _set_time(const OptStr &v, const OptStr &unit);
};

QWidget *bc_num(BlockBuilder *b, const QString &value, QWidget *parent, std::function<void(const QString &)> on_change);

class _Words : public QPlainTextEdit {
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
Q_SIGNALS:
    void left();

protected:
    void focusOutEvent(QFocusEvent *e) override;
};

class TextField : public QWidget {
    // A text of the game (a title, a description, a button): the words the player reads are typed here; the key they
    // hang on is shown small.
public:
    TextField(BlockBuilder *b, const QString &key, QWidget *parent, int lines = 1);
    BlockBuilder *b;
    QString key;
    QWidget *ed;
    void _done();
};

class Card : public QFrame {
public:
    Card(QWidget *parent, const QString &title, const QString &about, const QString &kind);
    QVBoxLayout *v;
    QHBoxLayout *head;
};

// ---------------------------------------------------------------------------------------------- the window
std::vector<std::pair<QString, QString>> spec_chunks(const Spec &spec);

class PaletteBlock : public QWidget {
    // A block of the catalogue as the piece it is — its sentence, a slot for every value, the notches that say where it
    // fits, a mouth when it holds others. It is dragged from here into a card.
    Q_OBJECT
public:
    PaletteBlock(BlockBuilder *b, const Spec *spec, QWidget *parent);
    BlockBuilder *b;
    const Spec *spec;
    blockshapes::Flow flow;
    QString kind, mode;
    std::vector<int> tops;
    std::optional<std::pair<std::vector<int>, QString>> mouth;
    bool lit = false;
    int hh = 0;
    void relayout(int maxw);
Q_SIGNALS:
    void clicked(const blockcode::Spec *spec);
    void doubleClicked(const blockcode::Spec *spec);

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    std::optional<QPoint> _press;
    std::optional<int> _maxw;
};

class BlockList : public QScrollArea {
    // Blocks as pieces, under their headings: to look through, to find, to drag from. A block dragged out of a card and
    // let go here is taken off.
public:
    using OnSpec = std::function<void(const Spec *)>;
    BlockList(BlockBuilder *b, const std::vector<const Spec *> &specs, OnSpec on_click = {}, OnSpec on_double = {},
              QWidget *parent = nullptr);
    BlockBuilder *b;
    QVBoxLayout *lay;
    OrderedMap<QString, QLabel *> heads;
    std::vector<std::pair<const Spec *, PaletteBlock *>> rows;
    QLabel *none;
    const Spec *apply_filter(const QString &q);
    void jump(const QString &cat);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

struct Dragging {                             // ("new", spec, None) / ("move", item, its stack)
    QString what;
    const Spec *spec = nullptr;
    PPtr it;
    QPointer<ContainerW> src;
};
struct Drop {                                 // (stack, index) / ("delete",)
    bool del = false;
    QPointer<ContainerW> cont;
    int index = 0;
};
struct PSpan {                                // (first line, last line, item) of the script on the right
    long long first, last;
    const PItem *item;
};

class BlockBuilder : public QDialog {
    Q_OBJECT
public:
    BlockBuilder(moddata::Mod &mod, QWidget *parent = nullptr, std::function<QStringList()> pictures = {});
    ~BlockBuilder() override;
    moddata::Mod &mod;
    std::function<QStringList()> pictures;
    document::Document *doc = nullptr;
    scriptstore::ScriptStore *store = nullptr;
    std::shared_ptr<blockcode::Env> env;
    OptStr rel, kind;
    QString ident;
    PPtr root;                                // the entry's item ({"k": "country_event", "v": [...]})
    long long depth = 0;
    std::optional<Dragging> dragging;
    std::optional<Drop> pending_drop;
    QPointer<ContainerW> target;              // the container clicked last
    PPtr sel;                                 // the block picked (its item): yellow, and lit in the script
    std::vector<PSpan> _spans;
    bool lazy = false;
    std::optional<QSet<const void *>> open_lists;   // the lists opened (id(list)); None before the first entry
    std::vector<PPtr> _keep;                  // (so an opened list's identity stays its own)
    OptStr rel_hint;
    QWidget *page;
    QLabel *title, *sub, *help, *status;
    QPointer<QLabel> warn, id_note;           // (gone with the page they stand on: Python's RuntimeError)
    bool _id_note_made = false;               // (Python: hasattr(self, "id_note"))
    QPlainTextEdit *preview;
    QScrollArea *scroll;
    QLineEdit *find;
    QWidget *chips;
    widgets::WrapRow *chip_row;
    QStackedWidget *palette;
    OrderedMap<QString, BlockList *> lists;   // "cond" / "eff" -> BlockList (made when first shown)
    OrderedMap<QString, QPushButton *> mode_btn;
    QVBoxLayout *page_lay;
    QPointer<QFrame> _pop;
    QString _mode = QStringLiteral("cond");   // the palette shown: "cond" / "eff"

    BlockW *block_of(const PItem *it) const;
    void select(const PPtr &it, bool show = false, bool script = true);
    void _mark_script(bool scroll = true);
    void _script_clicked();
    void say(const QString &text, bool bad = false);
    void explain(const PItem *it, const Spec *spec, const Ctx *ctx);
    void _set_mode(const QString &mode);
    void _fill_palette();
    OptStr why_not(ContainerW *cont, const Spec *spec = nullptr);
    void finish_drop(ContainerW *cont, int index, const Spec *spec = nullptr,
                     const std::optional<Dragging> &moved = std::nullopt);
    void autoscroll(const QPoint &gpos);
    void _scroll_tick();
    void add_from_palette(const Spec *spec);
    void open_add(ContainerW *cont, QPushButton *button);
    void set_doc(document::Document *doc, const refs::RefIndex *refs = nullptr, events::EventDB *db = nullptr);
    std::optional<script::Node> _find();
    void open_entry(const QString &rel, const QString &kind, const QString &ident);
    std::optional<std::vector<size_t>> _path(const PItem *it) const;
    void _load();
    void refresh(document::Document *doc, const refs::RefIndex *refs = nullptr, events::EventDB *db = nullptr);
    std::tuple<QString, std::optional<qsizetype>, std::optional<qsizetype>> _where();
    void changed(const QString &what);
    QString _name() const;
    void _to_editor();
    void _show_preview();
    void _clear_page();
    void _show_empty(const QString &text);
    void _build();
    QWidget *_flag(QWidget *parent, PPtr owner, const QString &key, const QString &text, const QString &tip);
    QWidget *_picture_box(QWidget *parent, PPtr owner);
    void _build_event(PPtr owner);
    void _build_decision(PPtr owner);
    QLabel *_h(QWidget *parent, const QString &text);
    qsizetype _slot(const PList &items, const QString &key) const;
    QWidget *_text(QWidget *parent, PPtr owner, const QString &key, const QString &default_key, int lines = 1);
    void _add_option(PPtr owner);
    void _others(const PList &items, const QSet<QString> &handled);
    void _set_id(QLineEdit *ed);
    void _check();
    void show_new(const QString &kind);
Q_SIGNALS:
    void scriptEdit(const QString &label, const QString &rel, const QString &text);   // undo label, rel, new text
    void nameEdit(const QString &key, const QString &text);                           // localisation key, text
    void openInEditor(const QString &rel, int pos);
    void undoRequested();
    void redoRequested();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    bool _marking = false;
    bool _own = false;                        // the builder itself is writing: no reading back
    std::optional<std::tuple<QString, QString, qsizetype, qsizetype>> _span_cache;   // (rel, text, start, end)
    OptStr _written;
    std::optional<std::pair<QString, bool>> _said;
    QTimer *_scroll_timer;
    int _scroll_step = 0;
    std::unique_ptr<scriptedit::EditorHost> _norefs;
    scriptedit::ScriptHighlighter *_hl;
};

}  // namespace blockbuilder
