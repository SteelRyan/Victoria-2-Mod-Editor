// scriptedit.py — The Events tab's centre: the mod's script files, open in tabs, edited directly — with a guiding
// hand. Every word the game gives a meaning (an event id, a decision, a modifier, a flag, a nation, a province, a
// culture …) is coloured by what it is and underlined; hovering tells what it is; Ctrl+click goes where it is defined
// (Shift or the middle button: in a new tab); ◀ / ▶ (Alt+← / Alt+→, the mouse's side buttons) go back and forth. A red
// wavy line marks what isn't there (an event id no event has, a flag nothing sets …). The map unfolds under the text
// with a click.
#pragma once
#include "core/ordered_map.h"
#include "core/refs.h"

#include <QColor>
#include <QFrame>
#include <QHash>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QWidget>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

class QButtonGroup;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
namespace scriptstore { class ScriptStore; }
namespace scriptsearch { class SearchPanel; }
namespace relview { class RelationsView; }

namespace scriptedit {

extern const QRegularExpression TOKEN, BRACES;
constexpr int FOLD_W = 22;                       // the gutter's column of ▾ / ▸ (fold / unfold a block)

using Folds = QHash<long long, long long>;       // {first line: last line}
// ({first line: last line} of every block that spans lines, [first lines of the top-level blocks])
std::pair<Folds, std::vector<long long>> fold_map(const QString &text);
using Pairs = QHash<qsizetype, qsizetype>;
// ({bracket: partner} both ways, {brackets without a partner}) — comments and strings don't count
std::pair<Pairs, QSet<qsizetype>> brace_pairs(const QString &text);
std::optional<qsizetype> likely_unclosed(const QString &text, const Pairs &pairs);
std::optional<qsizetype> bracket_place(const QString &text);

QColor mark_colour(const QString &sev);          // MARK
QColor mark_gutter(const QString &sev);          // MARK_GUTTER
extern const QSet<QString> LOGIC, SCOPES, STRUCT;

class ScriptWorkspace;
class ScriptEditor;

class EditorHost {
    // What a ScriptEditor / ScriptHighlighter ask of the workspace they live in (Python: duck typing — the Scripts
    // page's ScriptWorkspace, or the large window's stand-in, bigedit._Ws).
public:
    virtual ~EditorHost() = default;
    scriptstore::ScriptStore *store = nullptr;
    refs::RefIndex *refs = nullptr;              // RefIndex (set by the Events panel)
    QHash<QString, std::vector<QTextCursor>> folded;     // rel -> [cursor at each folded block's first line]
    int font_size = 10;
    virtual ScriptEditor *current() const = 0;
    virtual void _cursor_moved(ScriptEditor *ed) = 0;
    virtual void _decorate(ScriptEditor *ed) = 0;
    virtual void zoom(int step) = 0;
    virtual void back() = 0;
    virtual void forward() = 0;
    virtual void follow(const refs::Ref &r, bool new_tab = false) = 0;
    virtual QString hover_html(const QString &rel, const refs::Ref &r) const = 0;
};

class ScriptHighlighter : public QSyntaxHighlighter {
    // Colours of one file: what the text is (comments, strings, keys, numbers) and — from the reference index — what
    // the words mean (underlined in the colour of their kind; red and wavy when the thing isn't there).
    Q_OBJECT
public:
    ScriptHighlighter(QTextDocument *doc, EditorHost *ws, const QString &rel);
    EditorHost *ws;
    QString rel;
    QTextCharFormat ref_format(const QString &kind, bool ok);

protected:
    void highlightBlock(const QString &text) override;

private:
    QHash<QString, QTextCharFormat> _ref_fmt;
};

class LineArea : public QWidget {
public:
    explicit LineArea(ScriptEditor *ed);
    ScriptEditor *ed;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
};

struct Problem {                                 // (position or None, severity, text) from the checks
    std::optional<qsizetype> pos;
    QString sev, text;
    bool operator==(const Problem &o) const { return pos == o.pos && sev == o.sev && text == o.text; }
};
using ProblemsByRel = std::map<QString, std::vector<Problem>>;

class ScriptEditor : public QPlainTextEdit {
    // One tab: a file's text (the document is the store's — two tabs of one file show the same text).
    Q_OBJECT
public:
    ScriptEditor(EditorHost *ws, const QString &rel, QTextDocument *doc);
    EditorHost *ws;
    QString rel;
    LineArea *area;
    std::optional<std::pair<qsizetype, qsizetype>> flash;   // the block jumped to: lit for a moment
    QString label;                                          // a tab opened for one event / decision: its name
    std::vector<std::tuple<QTextCursor, QString, QString>> prob;   // (cursor, severity, text)
    long long _prob_gen = -1;
    std::optional<std::pair<qsizetype, std::optional<qsizetype>>> bracket;   // (position, partner or None)
    std::optional<std::pair<long long, long long>> bracket_lines;
    std::map<long long, QString> lone;                      // line -> why: a bracket without a partner stands there
    QPushButton *pill;
    std::vector<std::pair<QRect, long long>> _marks;        // (rect, first line) of the "{…}" marks of folded blocks

    void set_size(int pt);
    std::pair<const Folds *, const std::vector<long long> *> folds();
    void _refold();
    void _find_lone(const QString &text);
    std::vector<QTextCursor> &_folded();
    QSet<long long> folded_lines();
    bool is_folded(long long line);
    void _apply_folds();
    QTextCursor _cursor_at(long long line);
    void fold(long long line, bool on);
    std::vector<long long> entry_lines();
    void fold_all(bool on);
    void reveal(qsizetype pos);
    void _after_fold();
    void gutter_click(const QPoint &pt);
    struct PairsOf {
        QString text;
        Pairs pairs;
        QSet<qsizetype> lone;
    };
    const PairsOf &pairs();
    std::optional<std::pair<qsizetype, std::optional<qsizetype>>> bracket_near(qsizetype pos);
    bool to_partner(std::optional<qsizetype> pos = std::nullopt);
    void partner_hint();
    std::map<long long, std::pair<QString, QStringList>> problem_lines();
    int line_area_width();
    void _update_margin(int n = 0);
    void _update_area(const QRect &rect, int dy);
    void paint_lines(QPaintEvent *e);
    std::optional<refs::Ref> ref_at_point(const QPoint &pt);
    QTextBlock first_visible() const { return firstVisibleBlock(); }   // (protected in Qt; Python calls it)

protected:
    void wheelEvent(QWheelEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool viewportEvent(QEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct FoldOf {
        QString text;
        Folds folds;
        std::vector<long long> tops;
    };
    std::optional<FoldOf> _fold;                 // the last computation
    std::optional<PairsOf> _pairs;
    QTimer *_fold_timer, *_scroll_timer;
    int _margin = -1;
    bool _undo_keys(QKeyEvent *e);
};

class MiniView : public QFrame {
    // A small window over the text (top right): the map, or the relations tree. Drag its bar to move it, its ◣ corner
    // to resize it; ⬓ docks the map under the text; ✕ closes it.
    Q_OBJECT
public:
    explicit MiniView(ScriptWorkspace *ws);
    ScriptWorkspace *ws;
    QWidget *head;
    OrderedMap<QString, QPushButton *> page_btn;
    QPushButton *side_btn, *dock_btn;
    QStackedWidget *stack;
    QWidget *map_page;
    relview::RelationsView *rel;
    QLabel *grip;
    int _right = 18, _top = 70;                  // the gap to the workspace's right edge, the distance from its top
    std::pair<int, int> _want{560, 380};         // the size chosen (smaller only while the text area is too small)
    bool side = false;                           // sitting in the side panel (not floating over the text)
    void set_page(const QString &page);
    void put_map(QWidget *m);
    QWidget *host();
    void place();
    void set_side(bool on);
    std::optional<QString> _over(const QPoint &gpos);
    void _hint(bool on);
Q_SIGNALS:
    void pageChosen(const QString &page);       // "map" / "rel"
    void dockRequested();
    void closed();
    void sideRequested();                        // dragged onto the side panel (or its ⇥ button)
    void floatRequested(std::optional<QPoint> pos);   // dragged back over the text: the place in the workspace (or None)

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    std::optional<std::pair<QPoint, QPoint>> _drag;
    std::optional<std::pair<QPoint, QRect>> _rs;
};

class ScriptWorkspace : public QWidget, public EditorHost {
    Q_OBJECT
public:
    // compact: the editor as another page borrows it (Rebels, Pop types) — without the map and the relations tree;
    // the Scripts page's own editor stays the one that repaints the colours.
    explicit ScriptWorkspace(QWidget *parent = nullptr, bool compact = false);
    bool primary;
    std::function<QStringList(const QString &, const QString &)> info_extra;   // fn(kind, target) -> [html lines]
    QHash<QString, QPointer<ScriptHighlighter>> *highlighters;   // rel -> ScriptHighlighter (one per file and store)
    std::vector<std::pair<QString, qsizetype>> _back, _fwd;
    std::optional<std::pair<QString, QString>> _last_sym;
    std::optional<std::pair<QString, QString>> _focus;   // (kind, target) chosen outside the text (a map right-click)
    ProblemsByRel problems;                      // rel -> [(position or None, severity, text)] from the checks
    long long _prob_gen = 0;
    QString map_mode = QStringLiteral("mini");   // the map: in the mini window, or "dock"ed under the text
    bool _dock_on = false;
    QWidget *side_area = nullptr;                // the side panel (set by the main window): drop target of the mini
    QWidget *side_host = nullptr;                // … and the place in it the mini window goes
    QPushButton *back_btn, *fwd_btn, *map_btn, *undock_btn, *rel_btn;
    QLabel *where, *jump_lbl, *empty, *status, *legend;
    scriptsearch::SearchPanel *search;
    QTabWidget *tabs;
    QWidget *jump_bar;
    MiniView *mini;

    void set_store(scriptstore::ScriptStore *store);
    void set_refs(refs::RefIndex *refs, const std::optional<QSet<QString>> &changed = std::nullopt,
                  const std::optional<QHash<QString, std::optional<std::pair<qsizetype, qsizetype>>>> &ranges = std::nullopt);
    void rehighlight_all();
    ScriptEditor *current() const override;
    static bool _same_file(const QString &a, const QString &b);
    std::pair<int, ScriptEditor *> _editor_for(const QString &rel) const;
    QString _tab_title(const QString &rel, const QString &label = {}) const;
    QString _tab_tip(const QString &rel, const QString &label = {}) const;
    std::pair<int, ScriptEditor *> _new_tab(const QString &rel, const QString &label = {});
    std::optional<std::pair<QString, qsizetype>> location() const;
    ScriptEditor *open(const QString &rel, std::optional<qsizetype> pos = std::nullopt, bool new_tab = false,
                       bool record = true, const std::optional<std::pair<qsizetype, qsizetype>> &span = std::nullopt,
                       const QString &label = {});
    QString _place_name(const QString &rel, qsizetype pos) const;
    void _flash(ScriptEditor *ed, const std::pair<qsizetype, qsizetype> &span);
    void _close_tab(int i);
    void _update_empty();
    void refresh_titles();
    void _text_changed(const QString &rel);
    void follow(const refs::Ref &r, bool new_tab = false) override;
    void back() override;
    void forward() override;
    void _update_nav();
    QString _line_of(const QString &rel, qsizetype pos) const;
    void zoom(int step) override;
    void _search_go(const QString &rel, qsizetype a, qsizetype b);
    void set_focus(const QString &kind, const QString &target);
    void _decorate_current();
    QString hover_html(const QString &rel, const refs::Ref &r) const override;
    void _cursor_moved(ScriptEditor *ed) override;
    void set_problems(const ProblemsByRel &by_rel);
    void _problem_marks(ScriptEditor *ed);
    void _decorate(ScriptEditor *ed) override;
    void _emit_place();
    bool map_visible() const;
    bool map_on() const { return map_visible(); }
    bool rel_visible() const;
    void _sync();
    void _show_mini(const QString &page);
    void mini_to_side();
    void tab_shown(bool on);
    void mini_to_float(std::optional<QPoint> pos = std::nullopt);
    void _remember_side(bool on);
    void set_side_host(QWidget *area, QWidget *host);
    void show_map();
    void _map_clicked();
    void _rel_clicked();
    void _mini_page(const QString &page);
    void _dock_map();
    void undock_map();
    void _mini_closed();
Q_SIGNALS:
    void symbolChanged(const std::optional<refs::Ref> &r);   // the ref at the cursor or None
    void selectionRefs(const std::vector<refs::Ref> &refs);   // the refs inside the selected text ([] when none)
    void placeChanged(const QString &rel, long long pos);   // rel, cursor position
    void mapToggled(bool on);                    // the map shown / hidden (or moved: mini window ↔ docked)
    void relationsShown();                       // the relations tree became visible (fill it)
    void textEdited(const QString &rel);
    void replaceRequested(const OrderedMap<QString, QString> &changes, const QString &label);   // {rel: new text}
    void outside(const QString &rel, long long pos);   // (a borrowed editor) a link leads into a file it doesn't show

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QHash<QString, QPointer<ScriptHighlighter>> _own_highlighters;   // (no store yet)
    QTimer *_sym_timer;
    std::optional<std::vector<std::pair<QString, QString>>> _last_sel;
    bool _mini_was = false, _mini_away = false;
};

}  // namespace scriptedit
