// scriptedit.py — see scriptedit.h.
#include "ui/scriptedit.h"
#include "ui/drawtext.h"

#include "core/events.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/settings.h"
#include "ui/relview.h"
#include "ui/scriptsearch.h"
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QAction>
#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace scriptedit {

// (Python's scriptstore gives every text the plain-text layout itself; here the window part installs it)
static const bool _layout_installed = [] {
    scriptstore::ScriptStore::prepare_doc = [](QTextDocument *d) { d->setDocumentLayout(new QPlainTextDocumentLayout(d)); };
    return true;
}();

// (\s of Python's re = str.isspace: the class is spelt out, PCRE2's \s differs at \x1c-\x1f)
const QRegularExpression TOKEN(
    uR"re("[^"\n]*"?|#[^\n]*|[{}=<>]|[^\t\n\x{0b}\f\r\x{1c}-\x{20}\x{85}\x{a0}\x{1680}\x{2000}-\x{200a}\x{2028}\x{2029}\x{202f}\x{205f}\x{3000}{}=<>"#]+)re"_s);
const QRegularExpression BRACES(uR"re(#[^\n]*|"[^"\n]*"?|[{}\n])re"_s);
static const QRegularExpression NUM(uR"(^-?\d+(\.\d+)?$)"_s, QRegularExpression::UseUnicodePropertiesOption);

// BRACES.finditer(text), each match's first character and start: '{', '}' or '\n' (comments and strings skipped —
// the same matches the pattern finds, without the pattern)
template <typename F>
static void braces(const QString &text, F &&fn)
{
    const qsizetype n = text.size();
    const QChar *s = text.constData();
    qsizetype i = 0;
    while (i < n) {
        const char16_t c = s[i].unicode();
        if (c == u'#') {
            while (i < n && s[i] != u'\n')
                ++i;
        } else if (c == u'"') {
            ++i;
            while (i < n && s[i] != u'"' && s[i] != u'\n')
                ++i;
            if (i < n && s[i] == u'"')
                ++i;
        } else {
            if (c == u'{' || c == u'}' || c == u'\n')
                fn(c, i);
            ++i;
        }
    }
}

std::pair<Folds, std::vector<long long>> fold_map(const QString &text)
{
    Folds folds;
    std::vector<long long> tops;
    QSet<long long> in_tops;
    std::vector<long long> stack;
    long long line = 0;
    braces(text, [&](char16_t c, qsizetype) {
        if (c == u'\n') {
            ++line;
        } else if (c == u'{') {
            stack.push_back(line);
        } else if (c == u'}' && !stack.empty()) {
            const long long s = stack.back();
            stack.pop_back();
            if (line > s) {
                if (folds.value(s, -1) < line)
                    folds[s] = line;
                if (stack.empty() && !in_tops.contains(s)) {
                    tops.push_back(s);
                    in_tops.insert(s);
                }
            }
        }
    });
    return {folds, tops};
}

std::pair<Pairs, QSet<qsizetype>> brace_pairs(const QString &text)
{
    Pairs pairs;
    QSet<qsizetype> lone;
    std::vector<qsizetype> stack;
    braces(text, [&](char16_t c, qsizetype pos) {
        if (c == u'{') {
            stack.push_back(pos);
        } else if (c == u'}') {
            if (!stack.empty()) {
                const qsizetype a = stack.back();
                stack.pop_back();
                pairs[a] = pos;
                pairs[pos] = a;
            } else {
                lone.insert(pos);
            }
        }
    });
    for (qsizetype q : stack)
        lone.insert(q);
    return {pairs, lone};
}

// len(s) - len(s.lstrip()) of text[a:b].expandtabs(4)
static long long indent_of(const QString &text, qsizetype a, qsizetype b)
{
    long long col = 0, lead = 0;
    bool in_lead = true;
    for (qsizetype i = a; i < b && in_lead; ++i) {
        const char16_t c = text.at(i).unicode();
        if (c == u'\t') {
            const long long w = 4 - col % 4;
            col += w;
            lead += w;
        } else if (py::is_py_space(c)) {
            col = (c == u'\n' || c == u'\r') ? 0 : col + 1;
            lead += 1;
        } else {
            in_lead = false;
        }
    }
    return lead;
}

std::optional<qsizetype> likely_unclosed(const QString &text, const Pairs &pairs)
{
    std::optional<qsizetype> best;
    for (auto it = pairs.cbegin(); it != pairs.cend(); ++it) {
        const qsizetype a = it.key(), b = it.value();
        if (a > b)
            continue;
        const qsizetype la = py::rfind(text, u'\n', a) + 1, lb = py::rfind(text, u'\n', b) + 1;
        if (la == lb || !py::strip(py::slice(text, lb, b)).isEmpty())   // a one-liner, or the } isn't first on its line
            continue;
        if (std::abs(indent_of(text, la, a) - indent_of(text, lb, b)) >= 2 && (!best || a > *best))
            best = a;
    }
    return best;
}

std::optional<qsizetype> bracket_place(const QString &text)
{
    auto [pairs, lone] = brace_pairs(text);
    if (lone.isEmpty())
        return std::nullopt;
    std::optional<qsizetype> extra;
    for (qsizetype q : lone)
        if (text.at(q) == u'}' && (!extra || q < *extra))
            extra = q;
    if (extra)
        return extra;
    const auto q = likely_unclosed(text, pairs);
    return q ? *q : std::max<qsizetype>(0, py::rstrip(text).size() - 1);
}

QColor mark_colour(const QString &sev)
{
    return sev == u"error" ? QColor(255, 70, 70, 98) : QColor(240, 170, 60, 78);
}

QColor mark_gutter(const QString &sev)
{
    return sev == u"error" ? QColor(170, 44, 48) : QColor(138, 100, 34);
}

const QSet<QString> LOGIC = {u"AND"_s, u"OR"_s, u"NOT"_s};
const QSet<QString> SCOPES = {u"THIS"_s, u"ROOT"_s, u"FROM"_s, u"limit"_s, u"owner"_s, u"controller"_s,
                              u"capital_scope"_s, u"any_owned_province"_s, u"any_owned"_s, u"any_core"_s,
                              u"all_core"_s, u"any_country"_s, u"all_country"_s, u"any_state"_s, u"random_owned"_s,
                              u"random_country"_s, u"any_pop"_s, u"random_pop"_s, u"any_neighbor_country"_s,
                              u"any_neighbor_province"_s, u"location"_s};
const QSet<QString> STRUCT = {u"country_event"_s, u"province_event"_s, u"trigger"_s, u"mean_time_to_happen"_s,
                              u"immediate"_s, u"option"_s, u"potential"_s, u"allow"_s, u"effect"_s, u"ai_will_do"_s,
                              u"ai_chance"_s, u"modifier"_s, u"political_decisions"_s,
                              // the parts of a war goal (common/cb_types.txt)
                              u"peace_order"_s, u"can_use"_s, u"is_valid"_s, u"allowed_states"_s,
                              u"allowed_states_in_crisis"_s, u"allowed_substate_regions"_s, u"allowed_countries"_s,
                              u"on_add"_s, u"on_po_accepted"_s, u"prerequisites"_s};

static QTextCharFormat _fmt(int r, int g, int b, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(QColor(r, g, b));
    if (bold)
        f.setFontWeight(QFont::Bold);
    if (italic)
        f.setFontItalic(true);
    return f;
}

static const QTextCharFormat F_COMMENT = _fmt(110, 116, 110, false, true);
static const QTextCharFormat F_CAT = _fmt(232, 196, 118, false, true);
static const QTextCharFormat F_STRING = _fmt(214, 180, 140);
static const QTextCharFormat F_NUM = _fmt(160, 215, 150);
static const QTextCharFormat F_KEY = _fmt(200, 205, 215);
static const QTextCharFormat F_STRUCT = _fmt(232, 196, 118, true);
static const QTextCharFormat F_LOGIC = _fmt(250, 150, 90, true);
static const QTextCharFormat F_SCOPE = _fmt(120, 190, 250, true);
static const QTextCharFormat F_BOOL = _fmt(120, 170, 250);
static const QTextCharFormat F_BRACE = _fmt(201, 161, 90, true);
static const QTextCharFormat F_OP = _fmt(130, 130, 140);
static const QTextCharFormat F_VALUE = _fmt(231, 227, 216);

static QColor gold() { return QColor::fromString(theme::GOLD); }

static std::tuple<int, int, int> kind_rgb(const QString &kind, std::tuple<int, int, int> dflt)
{
    const auto *k = refs::KINDS.get(kind);
    return k ? k->second : dflt;
}

static QString kind_name(const QString &kind)          // KINDS.get(kind, (kind,))[0]
{
    const auto *k = refs::KINDS.get(kind);
    return k ? k->first : kind;
}

static bool in_entry_files(const QString &rel)
{
    return rel.startsWith(u"events/"_s) || rel.startsWith(u"decisions/"_s);
}

// text.count("\n", 0, q)
static long long newlines_before(const QString &text, qsizetype q)
{
    return QStringView(text).left(std::clamp<qsizetype>(q, 0, text.size())).count(u'\n');
}

// ======================================================================================== ScriptHighlighter
ScriptHighlighter::ScriptHighlighter(QTextDocument *doc, EditorHost *ws_, const QString &rel_)
    : QSyntaxHighlighter(doc), ws(ws_), rel(rel_)
{
}

QTextCharFormat ScriptHighlighter::ref_format(const QString &kind, bool ok)
{
    const QString key = kind + (ok ? u"\x01"_s : u"\x00"_s);
    auto it = _ref_fmt.find(key);
    if (it != _ref_fmt.end())
        return *it;
    const auto [r, g, b] = kind_rgb(kind, {220, 220, 220});
    QTextCharFormat f;
    f.setForeground(QColor(r, g, b));
    f.setFontUnderline(true);
    if (ok) {
        f.setUnderlineStyle(QTextCharFormat::DotLine);
        f.setUnderlineColor(QColor(r, g, b));
    } else {
        f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        f.setUnderlineColor(QColor(255, 90, 80));
    }
    _ref_fmt.insert(key, f);
    return f;
}

void ScriptHighlighter::highlightBlock(const QString &text)
{
    const qsizetype n = text.size();
    QHash<qsizetype, qsizetype> words;           // start -> end of the words of the line now — outside comments /
    auto it = TOKEN.globalMatch(text);           // strings: only there a reference colour may go
    while (it.hasNext()) {
        const auto m = it.next();
        const QString t = m.captured(0);
        const qsizetype a = m.capturedStart(0);
        const QChar c = t.at(0);
        if (c == u'#') {
            const QString low = py::lower(t);
            setFormat(int(a), int(t.size()), low.contains(u"category"_s) && low.contains(u':') ? F_CAT : F_COMMENT);
            break;
        }
        if (!u"\"{}=<>"_s.contains(c))
            words.insert(a, m.capturedEnd(0));
        if (c == u'"') {
            setFormat(int(a), int(t.size()), F_STRING);
            continue;
        }
        if (c == u'{' || c == u'}') {
            setFormat(int(a), 1, F_BRACE);
            continue;
        }
        if (c == u'=' || c == u'<' || c == u'>') {
            setFormat(int(a), 1, F_OP);
            continue;
        }
        qsizetype j = m.capturedEnd(0);
        while (j < n && (text.at(j) == u' ' || text.at(j) == u'\t'))
            ++j;
        const bool is_key = j < n && (text.at(j) == u'=' || text.at(j) == u'<' || text.at(j) == u'>');
        const QTextCharFormat *f;
        if (LOGIC.contains(t))
            f = &F_LOGIC;
        else if (SCOPES.contains(t))
            f = &F_SCOPE;
        else if (STRUCT.contains(t) && is_key)
            f = &F_STRUCT;
        else if (t == u"yes" || t == u"no")
            f = &F_BOOL;
        else if (NUM.match(t).hasMatch())
            f = &F_NUM;
        else
            f = is_key ? &F_KEY : &F_VALUE;
        setFormat(int(a), int(t.size()), *f);
    }
    refs::RefIndex *refs = ws->refs;
    if (refs == nullptr)
        return;
    const qsizetype pos = currentBlock().position();
    for (const refs::Ref &r : refs->in_range(rel, pos, pos + n + 1)) {
        const qsizetype col = r.pos - pos;
        // (the index can be older than the text — a line just made a comment, text typed above: a reference colours
        // only a word that stands exactly there now, never a comment; the tester, 2026-10-17)
        if (0 <= col && col < n && words.value(col, -1) == col + r.length)
            setFormat(int(col), int(std::min(r.length, n - col)), ref_format(r.kind, r.ok));
    }
}

// ======================================================================================== LineArea
LineArea::LineArea(ScriptEditor *ed_) : QWidget(ed_), ed(ed_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
}

QSize LineArea::sizeHint() const
{
    return QSize(ed->line_area_width(), 0);
}

void LineArea::paintEvent(QPaintEvent *e)
{
    ed->paint_lines(e);
}

void LineArea::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        ed->gutter_click(e->position().toPoint());
}

void LineArea::mouseMoveEvent(QMouseEvent *e)
{
    const long long n = ed->cursorForPosition(QPoint(0, int(e->position().y()))).block().blockNumber();
    setCursor(ed->folds().first->contains(n) ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

// ======================================================================================== ScriptEditor
ScriptEditor::ScriptEditor(EditorHost *ws_, const QString &rel_, QTextDocument *doc) : ws(ws_), rel(rel_)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setDocument(doc);
    _fold_timer = new QTimer(this);
    _fold_timer->setSingleShot(true);
    _fold_timer->setInterval(400);
    connect(_fold_timer, &QTimer::timeout, this, [this] { _refold(); });
    area = nullptr;
    set_size(ws->font_size);
    // no sideways scrolling (the user's rule): long lines wrap
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    area = new LineArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int n) { _update_margin(n); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) { _update_area(r, dy); });
    _update_margin();
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { ws->_cursor_moved(this); });
    _scroll_timer = new QTimer(this);
    _scroll_timer->setSingleShot(true);
    _scroll_timer->setInterval(120);
    connect(_scroll_timer, &QTimer::timeout, this, [this] {
        if (this == ws->current())
            ws->_decorate(this);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { _scroll_timer->start(); });
    connect(this, &QPlainTextEdit::selectionChanged, this, [this] { ws->_cursor_moved(this); });
    // the partner of the bracket at the cursor lies outside what is shown: a tab at that edge names its line (a click
    // goes there)
    pill = new QPushButton(this);
    pill->setCursor(Qt::PointingHandCursor);
    pill->setToolTip(u"Go to the other bracket of this pair (Ctrl+M; Ctrl+click on a bracket)"_s);
    pill->setStyleSheet(u"QPushButton { background: %1; color: #14100a; border: 2px solid #14100a; "
                        "border-radius: 6px; font-weight: 700; padding: 5px 12px; }"_s.arg(theme::GOLD));
    pill->hide();
    connect(pill, &QPushButton::clicked, this, [this] {
        if (bracket)
            to_partner(bracket->first);
    });
}

void ScriptEditor::set_size(int pt)
{
    // (the app's style sheet sets the font size: the editor's own style sheet has to say it)
    setStyleSheet(u"QPlainTextEdit { background: #101318; color: %1; border: none; "
                  "selection-background-color: #3a4a66; font-family: %2; font-size: %3pt; }"_s.arg(
                      theme::INK, theme::MONO, QString::number(pt)));
    QFont f(theme::MONO);
    f.setPointSize(pt);
    setFont(f);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4);
    _update_margin();
}

void ScriptEditor::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {   // Ctrl + wheel: the text bigger / smaller
        ws->zoom(e->angleDelta().y() > 0 ? 1 : -1);
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

// --- folding: ▾ in the gutter hides a block down to its closing brace, ▸ shows it again
std::pair<const Folds *, const std::vector<long long> *> ScriptEditor::folds()
{
    const QString text = ws->store != nullptr ? ws->store->text(rel) : QString();
    if (!_fold) {
        auto [f, t] = fold_map(text);
        _fold = FoldOf{text, std::move(f), std::move(t)};
        _find_lone(text);
    } else if (_fold->text.constData() != text.constData() && !_fold_timer->isActive()) {
        _fold_timer->start();                    // (not started anew by every repaint: it has to run out)
    }
    return {&_fold->folds, &_fold->tops};
}

void ScriptEditor::_refold()
{
    if (ws->store == nullptr)
        return;
    const QString text = ws->store->text(rel);
    auto [f, t] = fold_map(text);
    _fold = FoldOf{text, std::move(f), std::move(t)};
    const auto before = lone;
    _find_lone(text);
    area->update();
    if (lone != before && this == ws->current())
        ws->_decorate(this);
}

void ScriptEditor::_find_lone(const QString &text)
{
    auto [p, l] = brace_pairs(text);
    _pairs = PairsOf{text, std::move(p), std::move(l)};
    lone.clear();
    std::vector<qsizetype> sorted(_pairs->lone.cbegin(), _pairs->lone.cend());
    std::stable_sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size() && i < 40; ++i) {
        const qsizetype q = sorted[i];
        lone[newlines_before(text, q)] = text.at(q) == u'{'
                                             ? u"This { is never closed — a } is missing somewhere below it."_s
                                             : u"This } closes nothing — there is no { that it belongs to."_s;
    }
    std::optional<qsizetype> first_open;
    for (qsizetype q : _pairs->lone)
        if (text.at(q) == u'{' && (!first_open || q < *first_open))
            first_open = q;
    if (first_open) {
        const auto q = likely_unclosed(text, _pairs->pairs);
        if (q) {                                 // where the missing } most likely belongs
            lone.emplace(newlines_before(text, *q),
                         u"The } of this block is probably the one that is missing: its { and the } that now closes "
                         "it are indented differently."_s);
        } else {                                 // nothing inside looks wrong: it is missing at the end
            const qsizetype end = py::rstrip(text).size();
            lone.emplace(newlines_before(text, std::max<qsizetype>(0, end - 1)),
                         u"The file ends here while the { of line %1 is still open — a } is missing, most likely "
                         "right here at the end."_s.arg(newlines_before(text, *first_open) + 1));
        }
    }
}

// the folds chosen (a cursor at each folded block's first line: it moves with the text while editing); shared by every
// tab of the file (the text is one document)
std::vector<QTextCursor> &ScriptEditor::_folded()
{
    return ws->folded[rel];
}

QSet<long long> ScriptEditor::folded_lines()
{
    QSet<long long> out;
    for (const QTextCursor &c : _folded())
        out.insert(c.blockNumber());
    return out;
}

bool ScriptEditor::is_folded(long long line)
{
    return folded_lines().contains(line);
}

void ScriptEditor::_apply_folds()
{
    const Folds &fm = *folds().first;
    const QSet<long long> fl = folded_lines();
    std::vector<long long> firsts(fl.cbegin(), fl.cend());
    std::sort(firsts.begin(), firsts.end());
    std::vector<std::pair<long long, long long>> hide;
    for (long long s : firsts) {
        auto it = fm.constFind(s);
        if (it != fm.cend())
            hide.emplace_back(s + 1, *it);
    }
    QTextDocument *doc = document();
    const long long n = doc->blockCount();
    std::vector<int> mark(static_cast<size_t>(n + 2), 0);   // +1 where a hidden range starts, −1 after it ends
    for (const auto &[a, b] : hide) {
        mark[size_t(std::min(a, n))] += 1;
        mark[size_t(std::min(b, n - 1) + 1)] -= 1;
    }
    QTextBlock blk = doc->firstBlock();
    long long i = 0, run = 0;
    std::optional<int> lo, hi;
    while (blk.isValid()) {
        run += mark[size_t(i)];
        const bool want = run == 0;
        if (blk.isVisible() != want) {
            blk.setVisible(want);
            if (!lo)
                lo = blk.position();
            hi = blk.position() + blk.length();
        }
        blk = blk.next();
        ++i;
    }
    if (lo)
        doc->markContentsDirty(*lo, *hi - *lo);
    _after_fold();
}

QTextCursor ScriptEditor::_cursor_at(long long line)
{
    QTextCursor c(document()->findBlockByNumber(int(line)));
    c.movePosition(QTextCursor::EndOfBlock);     // (typing at the line's start doesn't move it off the line)
    return c;
}

void ScriptEditor::fold(long long line, bool on)
{
    if (!folds().first->contains(line))
        return;
    auto &lst = _folded();
    std::erase_if(lst, [&](const QTextCursor &c) { return c.blockNumber() == line; });
    if (on)
        lst.push_back(_cursor_at(line));
    _apply_folds();
}

std::vector<long long> ScriptEditor::entry_lines()
{
    const auto [fm, tops] = folds();
    refs::RefIndex *refs = ws->refs;
    std::vector<long long> out;
    if (refs != nullptr && in_entry_files(rel)) {
        const QString text = ws->store->text(rel);
        for (const auto &e : refs->db->entries) {
            if (e->rel && *e->rel == rel) {
                const qsizetype br = text.indexOf(u'{', e->span.first);
                if (br >= 0) {
                    const long long ln = refs::line_of(text, br) - 1;
                    if (fm->contains(ln))
                        out.push_back(ln);
                }
            }
        }
    }
    return out.empty() ? *tops : out;
}

void ScriptEditor::fold_all(bool on)
{
    auto &lst = _folded();
    if (on) {
        const QSet<long long> have = folded_lines();
        for (long long n : entry_lines())
            if (!have.contains(n))
                lst.push_back(_cursor_at(n));
    } else {
        lst.clear();
    }
    _apply_folds();
}

void ScriptEditor::reveal(qsizetype pos)
{
    const QTextBlock blk = document()->findBlock(int(pos));
    if (blk.isValid() && !blk.isVisible()) {
        const long long line = blk.blockNumber();
        const Folds &fm = *folds().first;
        auto &lst = _folded();
        std::erase_if(lst, [&](const QTextCursor &c) {
            const long long s = c.blockNumber();
            return s < line && line <= fm.value(s, -1);
        });
        _apply_folds();
    }
}

void ScriptEditor::_after_fold()
{
    viewport()->update();
    area->update();
    ensureCursorVisible();
}

void ScriptEditor::gutter_click(const QPoint &pt)
{
    const long long line = cursorForPosition(QPoint(0, pt.y())).block().blockNumber();
    if (folds().first->contains(line))
        fold(line, !is_folded(line));
}

void ScriptEditor::paintEvent(QPaintEvent *e)
{
    QPlainTextEdit::paintEvent(e);
    // the "{…}" at the end of a folded block's first line (a click opens it)
    _marks.clear();
    const Folds &fm = *folds().first;
    if (fm.isEmpty())
        return;
    const QSet<long long> fl = folded_lines();
    if (fl.isEmpty())
        return;
    QPainter p(viewport());
    QFont f(font());
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics metrics = p.fontMetrics();
    QTextBlock block = firstVisibleBlock();
    const int bottom = viewport()->rect().bottom();
    while (block.isValid()) {
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.top() > bottom)
            break;
        const long long n = block.blockNumber();
        if (block.isVisible() && fm.contains(n) && fl.contains(n)) {
            QTextCursor c(block);
            c.movePosition(QTextCursor::EndOfBlock);
            const QRect r = cursorRect(c);
            QString txt = u"…} %1 lines"_s.arg(fm.value(n) - n);
            refs::RefIndex *refs = ws->refs;
            events::Entry *ent = refs != nullptr && in_entry_files(rel)
                                     ? refs->db->entry_at(rel, block.position() + block.length() - 2)
                                     : nullptr;
            if (ent != nullptr && block.position() <= ent->span.first &&
                ent->span.first < block.position() + block.length())
                txt += u" · %1%2"_s.arg(refs->db->name(*ent),
                                        ent->is_event() ? u" #%1"_s.arg(ent->id.value_or(u"None"_s)) : QString());
            const QRect box(r.right() + 8, r.top() + 1, metrics.horizontalAdvance(txt) + 16, r.height() - 2);
            p.setPen(gold());
            p.setBrush(QColor(60, 50, 28));
            p.drawRoundedRect(box, 4, 4);
            drawtext::draw(p, box, Qt::AlignCenter, txt);
            _marks.emplace_back(box, n);
            block = document()->findBlockByNumber(int(fm.value(n) + 1));   // straight past the hidden lines
            continue;
        }
        block = block.next();
    }
    p.end();
}

// --- brackets
const ScriptEditor::PairsOf &ScriptEditor::pairs()
{
    const QString text = ws->store != nullptr ? ws->store->text(rel) : QString();
    if (!_pairs || _pairs->text.constData() != text.constData()) {
        auto [p, l] = brace_pairs(text);
        _pairs = PairsOf{text, std::move(p), std::move(l)};
    }
    return *_pairs;
}

std::optional<std::pair<qsizetype, std::optional<qsizetype>>> ScriptEditor::bracket_near(qsizetype pos)
{
    const PairsOf &pp = pairs();
    for (qsizetype p : {pos, pos - 1}) {
        if (0 <= p && p < pp.text.size() && (pp.text.at(p) == u'{' || pp.text.at(p) == u'}') &&
            (pp.pairs.contains(p) || pp.lone.contains(p))) {
            auto it = pp.pairs.constFind(p);
            return std::make_pair(p, it != pp.pairs.cend() ? std::optional<qsizetype>(*it) : std::nullopt);
        }
    }
    return std::nullopt;
}

bool ScriptEditor::to_partner(std::optional<qsizetype> pos)
{
    const auto br = bracket_near(pos ? *pos : textCursor().position());
    if (!br || !br->second)
        return false;
    reveal(*br->second);
    QTextCursor c = textCursor();
    c.setPosition(int(*br->second));
    setTextCursor(c);
    centerCursor();
    return true;
}

void ScriptEditor::partner_hint()
{
    bool show = false;
    if (bracket && bracket->second) {
        const QTextBlock blk = document()->findBlock(int(*bracket->second));
        const QRectF geo = blockBoundingGeometry(blk).translated(contentOffset());
        if (!(blk.isVisible() && geo.bottom() > 0 && geo.top() < viewport()->height())) {
            const bool below = *bracket->second > bracket->first;
            pill->setText((below ? u"}  closes on line %1  ▾"_s : u"▴  {  opens on line %1"_s)
                              .arg(blk.blockNumber() + 1));
            pill->adjustSize();
            const int bar = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0;
            pill->move(width() - pill->width() - bar - 14, below ? height() - pill->height() - 12 : 10);
            pill->raise();
            show = true;
        }
    }
    pill->setVisible(show);
}

// --- what the checks found here
std::map<long long, std::pair<QString, QStringList>> ScriptEditor::problem_lines()
{
    std::map<long long, std::pair<QString, QStringList>> out;
    for (const auto &[n, why] : lone)
        out[n] = {u"error"_s, QStringList{why}};
    for (const auto &[c, sev, text] : prob) {
        const long long n = c.blockNumber();
        auto it = out.find(n);
        const QString s = it != out.end() ? it->second.first : sev;
        QStringList ts = it != out.end() ? it->second.second : QStringList();
        ts << text;
        out[n] = {(s == u"error" || sev == u"error") ? u"error"_s : u"warn"_s, ts};
    }
    return out;
}

// --- line numbers
int ScriptEditor::line_area_width()
{
    return 16 + FOLD_W + fontMetrics().horizontalAdvance(u'9') * int(QString::number(std::max(1, blockCount())).size());
}

void ScriptEditor::_update_margin(int)
{
    const int w = line_area_width();
    if (w != _margin) {
        _margin = w;
        setViewportMargins(w, 0, 0, 0);
    }
}

void ScriptEditor::_update_area(const QRect &rect, int dy)
{
    if (dy)
        area->scroll(0, dy);
    else
        area->update(0, rect.y(), area->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        _update_margin();
}

void ScriptEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    area->setGeometry(QRect(cr.left(), cr.top(), line_area_width(), cr.height()));
}

void ScriptEditor::paint_lines(QPaintEvent *e)
{
    QPainter p(area);
    p.fillRect(e->rect(), QColor(22, 25, 30));
    QTextBlock block = firstVisibleBlock();
    long long n = block.blockNumber();
    const long long cur = textCursor().blockNumber();
    long long top = py::round_int(blockBoundingGeometry(block).translated(contentOffset()).top());
    long long bottom = top + py::round_int(blockBoundingRect(block).height());
    const int h = fontMetrics().height();
    const Folds &fm = *folds().first;
    const QSet<long long> fl = folded_lines();
    const int fw = area->width();
    const auto probs = problem_lines();
    const auto span = bracket_lines;
    const QColor g = gold();
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            auto pit = probs.find(n);
            const QString sev = pit != probs.end() ? pit->second.first : QString();
            if (!sev.isEmpty())                  // the marker runs through the line numbers too
                p.fillRect(0, int(top), fw, int(bottom - top), mark_gutter(sev));
            if (span && span->first <= n && n <= span->second) {   // the block between the two brackets
                p.fillRect(2, int(top), 4, int(bottom - top), g);
                if (n == span->first)
                    p.fillRect(2, int(top), 11, 4, g);
                if (n == span->second)
                    p.fillRect(2, int(bottom - 4), 11, 4, g);
            }
            p.setPen(!sev.isEmpty() ? QColor(u"#ffffff"_s) : n == cur ? g : QColor(92, 99, 110));
            drawtext::draw(p, QRect(0, int(top), fw - FOLD_W - 4, h), Qt::AlignRight, QString::number(n + 1));
            if (fm.contains(n)) {                // ▾ open block / ▸ folded block
                const bool folded = fl.contains(n);
                QFont af(font());
                af.setBold(true);
                af.setPointSize(std::max(9, font().pointSize() + 3));
                p.setFont(af);
                p.setPen(folded ? g : QColor(170, 158, 120));
                drawtext::draw(p, QRect(fw - FOLD_W, int(top), FOLD_W, h), Qt::AlignCenter, folded ? u"▸"_s : u"▾"_s);
                p.setFont(font());
            }
        }
        if (fl.contains(n) && fm.contains(n) && block.isVisible()) {
            n = fm.value(n) + 1;                 // straight past the hidden lines
            block = document()->findBlockByNumber(int(n));
        } else {
            block = block.next();
            n += 1;
        }
        top = bottom;
        bottom = top + py::round_int(blockBoundingRect(block).height());
    }
}

// --- what the mouse is on
std::optional<refs::Ref> ScriptEditor::ref_at_point(const QPoint &pt)
{
    const QTextCursor c = cursorForPosition(pt);
    refs::RefIndex *refs = ws->refs;
    if (refs == nullptr)
        return std::nullopt;
    const qsizetype pos = c.position();
    const refs::Ref *r = refs->at(rel, pos);
    if (r == nullptr)
        return std::nullopt;
    // the cursor lands between characters: the word must really be under the mouse
    const QRect rect = cursorRect(c);
    if (std::abs(rect.center().x() - pt.x()) > fontMetrics().horizontalAdvance(u'M') * 1.5 && pos == r->pos + r->length)
        return std::nullopt;
    return *r;
}

bool ScriptEditor::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const auto r = ref_at_point(he->pos());
        const auto probs = problem_lines();
        auto it = probs.find(cursorForPosition(he->pos()).blockNumber());
        QString why;
        if (it != probs.end()) {                 // a marked line says why it is marked
            const QString &sev = it->second.first;
            const QStringList &texts = it->second.second;
            QStringList items;
            for (qsizetype i = 0; i < texts.size() && i < 6; ++i)
                items << u"• "_s + refs::esc(texts[i]);
            why = u"<div style=\"max-width:560px; color:%1\"><b>%2</b><br>%3</div>"_s.arg(
                sev == u"error" ? theme::ERROR_ : theme::WARN,
                (sev == u"error" ? u"⚠ Error"_s : u"⚠ Warning"_s) +
                    (texts.size() > 1 ? u" (%1)"_s.arg(texts.size()) : QString()),
                items.join(u"<br>"_s) + (texts.size() > 6 ? u"<br>…"_s : QString()));
        }
        if (r || !why.isEmpty())
            QToolTip::showText(he->globalPos(),
                               why + (r ? (why.isEmpty() ? QString() : u"<hr>"_s) + ws->hover_html(rel, *r) : QString()),
                               viewport());
        else
            QToolTip::hideText();
        return true;
    }
    return QPlainTextEdit::viewportEvent(e);
}

void ScriptEditor::mouseMoveEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseMoveEvent(e);
    if ((e->modifiers() & Qt::ControlModifier) && ref_at_point(e->position().toPoint()))
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->setCursor(Qt::IBeamCursor);
}

void ScriptEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const QPoint pt = e->position().toPoint();
        for (const auto &[r, n] : _marks) {      // a click on "…}" opens the folded block
            if (r.contains(pt)) {
                fold(n, false);
                return;
            }
        }
    }
    if (e->button() == Qt::BackButton) {
        ws->back();
        return;
    }
    if (e->button() == Qt::ForwardButton) {
        ws->forward();
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        const auto r = ref_at_point(e->position().toPoint());
        if (r)
            ws->follow(*r, true);
        return;
    }
    QPlainTextEdit::mousePressEvent(e);
}

void ScriptEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const auto r = ref_at_point(e->position().toPoint());
        if (r && ws->refs != nullptr && ws->refs->definition(r->kind, r->target)) {
            QTimer::singleShot(0, this, [w = ws, rr = *r] { w->follow(rr, true); });
            return;
        }
    }
    QPlainTextEdit::mouseDoubleClickEvent(e);
}

void ScriptEditor::mouseReleaseEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        const auto r = ref_at_point(e->position().toPoint());
        if (r) {
            const bool shift = bool(e->modifiers() & Qt::ShiftModifier);
            QTimer::singleShot(0, this, [w = ws, rr = *r, shift] { w->follow(rr, shift); });
        } else if (!textCursor().hasSelection()) {   // Ctrl+click on a bracket: to its partner
            QTimer::singleShot(0, this, [this] { to_partner(); });
        }
    }
}

bool ScriptEditor::_undo_keys(QKeyEvent *e)
{
    // Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) in the text: the editor's one undo list (typing is in it, step by step)
    if (ws->store == nullptr || !ws->store->undo_hook)
        return false;
    const auto &hook = *ws->store->undo_hook;
    if (e->matches(QKeySequence::Undo)) {
        hook.first();
        return true;
    }
    if (e->matches(QKeySequence::Redo) || (e->key() == Qt::Key_Y && e->modifiers() == Qt::ControlModifier)) {
        hook.second();
        return true;
    }
    return false;
}

void ScriptEditor::contextMenuEvent(QContextMenuEvent *e)
{
    // The right-click menu's Undo / Redo: the editor's one undo list too (never the text's own)
    QMenu *menu = createStandardContextMenu();
    if (ws->store != nullptr && ws->store->undo_hook) {
        const auto hook = *ws->store->undo_hook;
        for (QAction *a : menu->actions()) {
            if (a->objectName() == u"edit-undo" || a->objectName() == u"edit-redo") {
                QObject::disconnect(a, &QAction::triggered, nullptr, nullptr);
                const auto fn = a->objectName() == u"edit-undo" ? hook.first : hook.second;
                connect(a, &QAction::triggered, this, [fn] { fn(); });
                a->setEnabled(true);
            }
        }
    }
    menu->exec(e->globalPos());
    menu->deleteLater();
}

bool ScriptEditor::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->matches(QKeySequence::Undo) || k->matches(QKeySequence::Redo) ||
            (k->key() == Qt::Key_Y && k->modifiers() == Qt::ControlModifier)) {
            e->accept();                         // (the keys come here, not to a window shortcut)
            return true;
        }
    }
    return QPlainTextEdit::event(e);
}

void ScriptEditor::keyPressEvent(QKeyEvent *e)
{
    if (_undo_keys(e))
        return;
    if (e->key() == Qt::Key_M && e->modifiers() == Qt::ControlModifier) {   // to the partner bracket
        to_partner();
        return;
    }
    if (e->key() == Qt::Key_F12) {
        const refs::Ref *r = ws->refs ? ws->refs->at(rel, textCursor().position()) : nullptr;
        if (r != nullptr)
            ws->follow(*r, bool(e->modifiers() & Qt::ShiftModifier));
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

// ======================================================================================== MiniView
MiniView::MiniView(ScriptWorkspace *ws_) : QFrame(ws_), ws(ws_)
{
    setObjectName(u"miniView"_s);
    setStyleSheet(u"#miniView { background: #0d1014; border: 2px solid %1; border-radius: 6px; }"_s.arg(theme::GOLD));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(3, 3, 3, 3);
    lay->setSpacing(2);
    head = new QWidget();
    head->setCursor(Qt::SizeAllCursor);
    head->setToolTip(u"Drag to move · ◣ (bottom left) to resize"_s);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(4, 0, 2, 0);
    hl->setSpacing(4);
    auto *g = new QLabel(u"⠿"_s);
    g->setStyleSheet(u"color: %1; font-size: 16px;"_s.arg(theme::GOLD));
    hl->addWidget(g);
    auto *grp = new QButtonGroup(this);
    for (const auto &[k, t] : std::vector<std::pair<QString, QString>>{{u"map"_s, u"🗺 Map"_s},
                                                                        {u"rel"_s, u"🌳 Relations"_s}}) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, kk = k] { Q_EMIT pageChosen(kk); });
        grp->addButton(b);
        hl->addWidget(b);
        page_btn.insert(k, b);
    }
    hl->addStretch(1);
    side_btn = new QPushButton(u"⇥ Side panel"_s);
    side_btn->setCursor(Qt::PointingHandCursor);
    side_btn->setToolTip(u"Put this window at the top of the side panel on the right — or drag its bar there"_s);
    connect(side_btn, &QPushButton::clicked, this, [this] {
        if (side)
            Q_EMIT floatRequested(std::nullopt);
        else
            Q_EMIT sideRequested();
    });
    hl->addWidget(side_btn);
    dock_btn = new QPushButton(u"⬓ Dock map below"_s);
    dock_btn->setCursor(Qt::PointingHandCursor);
    dock_btn->setToolTip(u"Put the map under the text, full width (its ⬓ button there brings it back here)"_s);
    connect(dock_btn, &QPushButton::clicked, this, [this] { Q_EMIT dockRequested(); });
    hl->addWidget(dock_btn);
    auto *x = new QPushButton(u"✕"_s);
    x->setCursor(Qt::PointingHandCursor);
    x->setFixedWidth(30);
    connect(x, &QPushButton::clicked, this, [this] { Q_EMIT closed(); });
    hl->addWidget(x);
    lay->addWidget(head);
    stack = new QStackedWidget();
    map_page = new QWidget();
    auto *ml = new QVBoxLayout(map_page);
    ml->setContentsMargins(0, 0, 0, 0);
    rel = new relview::RelationsView();
    stack->addWidget(map_page);
    stack->addWidget(rel);
    lay->addWidget(stack, 1);
    grip = new QLabel(u"◣"_s, this);
    grip->setStyleSheet(u"color: %1; font-size: 18px; background: transparent;"_s.arg(theme::GOLD));
    grip->setCursor(Qt::SizeBDiagCursor);
    grip->setToolTip(u"Drag to resize"_s);
    head->installEventFilter(this);
    grip->installEventFilter(this);
    resize(_want.first, _want.second);
}

void MiniView::set_page(const QString &page)
{
    (*page_btn.get(page))->setChecked(true);
    stack->setCurrentIndex(page == u"map" ? 0 : 1);
    dock_btn->setVisible(page == u"map");
}

void MiniView::put_map(QWidget *m)
{
    if (m->parent() != map_page)
        map_page->layout()->addWidget(m);
    m->show();
}

QWidget *MiniView::host()
{
    return parentWidget() ? parentWidget() : static_cast<QWidget *>(ws);
}

void MiniView::place()
{
    const int W = host()->width(), H = host()->height();
    const int w = std::min(_want.first, std::max(280, W - 40));
    const int h = std::min(_want.second, std::max(200, H - 60));
    resize(w, h);
    const int x = std::max(0, W - w - _right);
    const int y = std::max(0, std::min(_top, H - h));
    move(x, y);
}

void MiniView::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    grip->move(4, height() - grip->sizeHint().height() - 2);
    grip->raise();
}

void MiniView::set_side(bool on)
{
    side = on;
    side_btn->setText(on ? u"⇤ Float over the text"_s : u"⇥ Side panel"_s);
    side_btn->setToolTip(on ? u"Back to a small window over the text — or drag its bar there"_s
                            : u"Put this window at the top of the side panel on the right — or drag its bar there"_s);
    grip->setCursor(on ? Qt::SizeVerCursor : Qt::SizeBDiagCursor);
}

std::optional<QString> MiniView::_over(const QPoint &gpos)
{
    QWidget *area = ws->side_area;
    if (area != nullptr && area->isVisible() && area->rect().contains(area->mapFromGlobal(gpos)))
        return u"side"_s;
    if (ws->rect().contains(ws->mapFromGlobal(gpos)))
        return u"text"_s;
    return std::nullopt;
}

void MiniView::_hint(bool on)
{
    setStyleSheet(u"#miniView { background: #0d1014; border: %1px solid %2; border-radius: 6px; }"_s.arg(
        QString::number(on ? 4 : 2), on ? u"#78e678"_s : theme::GOLD));
}

bool MiniView::eventFilter(QObject *obj, QEvent *e)
{
    const QEvent::Type t = e->type();
    if (obj == head) {
        if (t == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
            _drag = std::make_pair(static_cast<QMouseEvent *>(e)->globalPosition().toPoint(), pos());
            return true;
        }
        if (t == QEvent::MouseMove && _drag) {
            const QPoint g = static_cast<QMouseEvent *>(e)->globalPosition().toPoint();
            const auto where = _over(g);
            const QPoint d = g - _drag->first;
            const QPoint p = _drag->second + d;
            const int W = host()->width(), H = host()->height();
            move(std::max(-width() + 80, std::min(p.x(), W - 60)), std::max(0, std::min(p.y(), H - 40)));
            // released over the other side: it moves there
            _hint(where == (side ? u"text"_s : u"side"_s));
            return true;
        }
        if (t == QEvent::MouseButtonRelease && _drag) {
            const QPoint g = static_cast<QMouseEvent *>(e)->globalPosition().toPoint();
            const auto where = _over(g);
            _drag.reset();
            _hint(false);
            if (!side && where == u"side"_s) {
                Q_EMIT sideRequested();
            } else if (side && where == u"text"_s) {
                Q_EMIT floatRequested(ws->mapFromGlobal(g));
            } else {
                const int W = host()->width();
                move(std::max(0, std::min(x(), W - width())), y());
                _right = W - x() - width();
                _top = y();
            }
            return true;
        }
    } else if (obj == grip) {
        if (t == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
            _rs = std::make_pair(static_cast<QMouseEvent *>(e)->globalPosition().toPoint(), geometry());
            return true;
        }
        if (t == QEvent::MouseMove && _rs) {
            const QPoint d = static_cast<QMouseEvent *>(e)->globalPosition().toPoint() - _rs->first;
            const QRect g = _rs->second;
            const int w = std::max(280, std::min(g.width() - d.x(), g.right() + 1));
            const int h = std::max(200, std::min(g.height() + d.y(), host()->height() - g.y()));
            const int x0 = std::max(0, g.right() + 1 - w);
            setGeometry(x0, g.y(), g.right() + 1 - x0, h);
            return true;
        }
        if (t == QEvent::MouseButtonRelease && _rs) {
            _rs.reset();
            _want = {width(), height()};
            _right = host()->width() - x() - width();
            _right = ws->width() - x() - width();
            _top = y();
            return true;
        }
    }
    return QFrame::eventFilter(obj, e);
}

// ======================================================================================== ScriptWorkspace
// one painter of colours per file and store, whichever editor shows it (Python: store.__dict__["_highlighters"])
static QHash<const void *, QHash<QString, QPointer<ScriptHighlighter>>> &store_highlighters()
{
    static QHash<const void *, QHash<QString, QPointer<ScriptHighlighter>>> h;
    return h;
}

ScriptWorkspace::ScriptWorkspace(QWidget *parent, bool compact) : QWidget(parent), primary(!compact)
{
    highlighters = &_own_highlighters;
    {
        bool ok = false;
        const int v = settings_::settings()->value(u"scriptFontSize"_s, 10).toInt(&ok);
        font_size = ok ? v : 10;
    }
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget();
    bar->setObjectName(u"wsBar"_s);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(6, 4, 6, 4);
    bl->setSpacing(6);
    back_btn = new QPushButton(u"◀ Back"_s);
    connect(back_btn, &QPushButton::clicked, this, [this] { back(); });
    fwd_btn = new QPushButton(u"Forward ▶"_s);
    connect(fwd_btn, &QPushButton::clicked, this, [this] { forward(); });
    for (QPushButton *b : {back_btn, fwd_btn}) {
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        bl->addWidget(b);
    }
    where = new QLabel(QString());
    where->setObjectName(u"sub"_s);
    where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bl->addWidget(where, 1);
    const auto fold_cur = [this](bool on) {
        if (ScriptEditor *ed = current())
            ed->fold_all(on);
    };
    const std::vector<std::tuple<QString, QString, std::function<void()>>> tools = {
        {u"▸ Fold all"_s, u"Fold every event / decision of this file (▸ in the gutter opens one; Ctrl+Shift+[)"_s,
         [fold_cur] { fold_cur(true); }},
        {u"▾ Unfold all"_s, u"Unfold everything in this file (Ctrl+Shift+])"_s, [fold_cur] { fold_cur(false); }},
        {u"A−"_s, u"Smaller text (Ctrl + wheel, Ctrl+−)"_s, [this] { zoom(-1); }},
        {u"A+"_s, u"Bigger text (Ctrl + wheel, Ctrl+=)"_s, [this] { zoom(1); }}};
    for (const auto &[t, tip, fn] : tools) {
        auto *fb = new QPushButton(t);
        fb->setToolTip(tip);
        if (t.size() <= 2)
            fb->setFixedWidth(40);
        connect(fb, &QPushButton::clicked, this, [fn = fn] { fn(); });
        bl->addWidget(fb);
    }
    auto *sb = new QPushButton(u"🔍 Search / replace"_s);
    sb->setToolTip(u"Ctrl+F: in this file · Ctrl+Shift+F: in all events and decisions · Ctrl+H: replace"_s);
    connect(sb, &QPushButton::clicked, this, [this] { search->open_panel(); });
    bl->addWidget(sb);
    map_btn = new QPushButton(u"🗺  Map"_s);
    map_btn->setCheckable(true);
    map_btn->setToolTip(u"The map in a small window over the text (what the cursor is on is marked on it)"_s);
    connect(map_btn, &QPushButton::clicked, this, [this] { _map_clicked(); });
    bl->addWidget(map_btn);
    undock_btn = new QPushButton(u"⬔ Map to the small window"_s);
    undock_btn->setToolTip(u"The docked map back into the small window over the text"_s);
    connect(undock_btn, &QPushButton::clicked, this, [this] { undock_map(); });
    undock_btn->setVisible(false);
    bl->addWidget(undock_btn);
    rel_btn = new QPushButton(u"🌳 Relations"_s);
    rel_btn->setCheckable(true);
    rel_btn->setToolTip(u"How the word and the event at the cursor hang together with the rest — a tree"_s);
    connect(rel_btn, &QPushButton::clicked, this, [this] { _rel_clicked(); });
    bl->addWidget(rel_btn);
    if (compact) {
        map_btn->hide();
        rel_btn->hide();
    }
    lay->addWidget(bar);
    search = new scriptsearch::SearchPanel(this);
    connect(search, &scriptsearch::SearchPanel::goTo, this,
            [this](const QString &rel, long long a, long long b) { _search_go(rel, a, b); });
    connect(search, &scriptsearch::SearchPanel::replaceRequested, this, &ScriptWorkspace::replaceRequested);
    lay->addWidget(search);
    tabs = new QTabWidget();
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    tabs->setElideMode(Qt::ElideMiddle);
    tabs->setUsesScrollButtons(true);
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { _close_tab(i); });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int) { _cursor_moved(current()); });
    // after a jump: where from, and the way back
    jump_bar = new QWidget();
    auto *jl = new QHBoxLayout(jump_bar);
    jl->setContentsMargins(8, 3, 8, 3);
    jump_lbl = new QLabel(QString());
    jump_lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    jump_lbl->setStyleSheet(u"color: #14100a; font-weight: 600;"_s);
    jl->addWidget(jump_lbl, 1);
    auto *bt = new QPushButton(u"◀ Back there"_s);
    bt->setStyleSheet(u"QPushButton { background: #14100a; color: %1; border: 1px solid #14100a; border-radius: 4px;"
                      " padding: 2px 10px; font-weight: 600; }"_s.arg(theme::GOLD));
    connect(bt, &QPushButton::clicked, this, [this] { back(); });
    jl->addWidget(bt);
    auto *x = new QPushButton(u"✕"_s);
    x->setStyleSheet(u"QPushButton { background: transparent; color: #14100a; border: none; font-weight: 700; }"_s);
    connect(x, &QPushButton::clicked, this, [this] { jump_bar->setVisible(false); });
    jl->addWidget(x);
    jump_bar->setStyleSheet(u"background: %1;"_s.arg(theme::GOLD));
    jump_bar->setVisible(false);
    lay->addWidget(jump_bar);
    lay->addWidget(tabs, 1);
    empty = new QLabel(u"Pick an event, a decision or a file on the right — it opens here.\n\n"
                       "Hover a word to see what it is · Ctrl+click: go where it is defined · "
                       "double-click (or the middle button): open it in a tab of its own · "
                       "◀ Back / Alt+←: where you were"_s);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    empty->setObjectName(u"sub"_s);
    lay->addWidget(empty, 1);
    status = new QLabel(QString());
    status->setObjectName(u"faint"_s);
    status->setWordWrap(true);
    status->setContentsMargins(8, 3, 8, 3);
    lay->addWidget(status);
    legend = new QLabel(QString());
    legend->setObjectName(u"sub"_s);
    legend->setWordWrap(true);
    legend->setTextFormat(Qt::RichText);
    legend->setContentsMargins(8, 4, 8, 4);
    legend->setVisible(false);
    lay->addWidget(legend);
    _sym_timer = new QTimer(this);
    _sym_timer->setSingleShot(true);
    _sym_timer->setInterval(160);
    connect(_sym_timer, &QTimer::timeout, this, [this] { _emit_place(); });
    const std::vector<std::pair<QString, std::function<void()>>> keys = {
        {u"Alt+Left"_s, [this] { back(); }},
        {u"Alt+Right"_s, [this] { forward(); }},
        {u"Ctrl+="_s, [this] { zoom(1); }},
        {u"Ctrl++"_s, [this] { zoom(1); }},
        {u"Ctrl+-"_s, [this] { zoom(-1); }},
        {u"Ctrl+0"_s, [this] { zoom(0); }},
        {u"Ctrl+Shift+["_s, [fold_cur] { fold_cur(true); }},
        {u"Ctrl+Shift+]"_s, [fold_cur] { fold_cur(false); }},
        {u"Ctrl+F"_s, [this] { search->open_panel(u"file"_s); }},
        {u"Ctrl+Shift+F"_s, [this] { search->open_panel(u"scripts"_s); }},
        {u"Ctrl+H"_s, [this] { search->open_panel(std::nullopt, true); }}};
    for (const auto &[k, fn] : keys) {
        auto *sc = new QShortcut(QKeySequence(k), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [fn = fn] { fn(); });
    }
    connect(this, &ScriptWorkspace::textEdited, this, [this](const QString &) {
        if (search->isVisible())
            search->_timer->start();
    });
    mini = new MiniView(this);
    mini->hide();
    connect(mini, &MiniView::pageChosen, this, [this](const QString &p) { _mini_page(p); });
    connect(mini, &MiniView::dockRequested, this, [this] { _dock_map(); });
    connect(mini, &MiniView::closed, this, [this] { _mini_closed(); });
    connect(mini, &MiniView::sideRequested, this, [this] { mini_to_side(); });
    connect(mini, &MiniView::floatRequested, this, [this](std::optional<QPoint> p) { mini_to_float(p); });
    connect(mini->rel, &relview::RelationsView::goTo, this, [this](const QString &rel, long long pos) { open(rel, pos); });
    _update_nav();
    _update_empty();
}

// ------------------------------------------------------------------ the files
void ScriptWorkspace::set_store(scriptstore::ScriptStore *st)
{
    // A new document (scenario loaded / reverted): the tabs go, the new texts are used from now on.
    if (st == store)
        return;
    while (tabs->count()) {
        QWidget *w = tabs->widget(0);
        tabs->removeTab(0);
        w->deleteLater();
    }
    // one painter of colours per file, whichever editor shows it (two on one text would fight)
    if (st != nullptr) {
        highlighters = &store_highlighters()[st];
    } else {
        _own_highlighters.clear();
        highlighters = &_own_highlighters;
    }
    folded.clear();
    _back.clear();
    _fwd.clear();
    store = st;
    if (st != nullptr) {
        QPointer<ScriptWorkspace> self(this);
        st->listeners.push_back([self](const QString &rel) {
            if (self)
                self->_text_changed(rel);
        });
    }
    _update_nav();
    _update_empty();
}

void ScriptWorkspace::set_refs(refs::RefIndex *r, const std::optional<QSet<QString>> &changed,
                               const std::optional<QHash<QString, std::optional<std::pair<qsizetype, qsizetype>>>> &ranges)
{
    // The reference index (again, after a reparse): the colours of the open files follow — of a typed-in file only the
    // lines whose references changed (ranges: rel -> (a, b) or None).
    refs = r;
    if (primary) {
        for (auto it = highlighters->begin(); it != highlighters->end(); ++it) {
            ScriptHighlighter *hl = it.value();
            const QString &rel = it.key();
            if (hl == nullptr)
                continue;
            if (changed && !changed->contains(rel))
                continue;
            if (ranges && ranges->contains(rel)) {
                const auto rg = ranges->value(rel);
                if (!rg)
                    continue;
                QTextDocument *doc = hl->document();
                QTextBlock b = doc->findBlock(int(rg->first));
                const QTextBlock end = doc->findBlock(int(rg->second));
                const int last = end.isValid() ? end.blockNumber() : doc->blockCount() - 1;
                while (b.isValid() && b.blockNumber() <= last) {
                    hl->rehighlightBlock(b);
                    b = b.next();
                }
            } else {
                hl->rehighlight();
            }
        }
    }
    _cursor_moved(current());
}

void ScriptWorkspace::rehighlight_all()
{
    // Every open file coloured again (after an Apply / a re-read: the texts may have changed under them)
    const auto hls = highlighters->values();
    for (const auto &hl : hls)
        if (hl)                                  // (its text is gone)
            hl->rehighlight();
}

ScriptEditor *ScriptWorkspace::current() const
{
    return qobject_cast<ScriptEditor *>(tabs->currentWidget());
}

bool ScriptWorkspace::_same_file(const QString &a, const QString &b)
{
    const auto norm = [](const QString &r) {
        return pypath::normcase(pypath::normpath(QString(r).replace(u'\\', u'/')));
    };
    return norm(a) == norm(b);
}

std::pair<int, ScriptEditor *> ScriptWorkspace::_editor_for(const QString &rel) const
{
    // The tab of this file (one tab a file: the path compared normalised — the tester, 2026-10-17)
    for (int i = 0; i < tabs->count(); ++i) {
        auto *w = qobject_cast<ScriptEditor *>(tabs->widget(i));
        if (w != nullptr && _same_file(w->rel, rel))
            return {i, w};
    }
    return {-1, nullptr};
}

QString ScriptWorkspace::_tab_title(const QString &rel, const QString &) const
{
    // Always the file's own name (the entry a tab was opened for is in its tooltip, not its title)
    const QString name = rel.split(u'/').last();
    QTextDocument *const *d = store != nullptr ? store->docs.get(rel) : nullptr;
    const bool dirty = d != nullptr && (*d)->isModified();
    return (dirty ? u"● "_s : QString()) + name;
}

QString ScriptWorkspace::_tab_tip(const QString &rel, const QString &label) const
{
    return rel + (!label.isEmpty() ? u"\nopened at: %1"_s.arg(label) : QString());
}

std::pair<int, ScriptEditor *> ScriptWorkspace::_new_tab(const QString &rel, const QString &label)
{
    QTextDocument *doc = store->document(rel);
    auto it = highlighters->find(rel);
    if (it == highlighters->end() || it->isNull())
        highlighters->insert(rel, new ScriptHighlighter(doc, this, rel));
    auto *ed = new ScriptEditor(this, rel, doc);
    ed->label = label;
    const int i = tabs->count() ? tabs->insertTab(tabs->currentIndex() + 1, ed, _tab_title(rel, label))
                                : tabs->addTab(ed, _tab_title(rel, label));
    tabs->setTabToolTip(i, _tab_tip(rel, label));
    _update_empty();
    return {i, ed};
}

std::optional<std::pair<QString, qsizetype>> ScriptWorkspace::location() const
{
    ScriptEditor *ed = current();
    if (ed == nullptr)
        return std::nullopt;
    return std::make_pair(ed->rel, qsizetype(ed->textCursor().position()));
}

ScriptEditor *ScriptWorkspace::open(const QString &rel, std::optional<qsizetype> pos, bool new_tab, bool record,
                                    const std::optional<std::pair<qsizetype, qsizetype>> &span, const QString &label)
{
    // Show a file (at a position: the cursor goes there, the line in the middle; `span` (a, b) is lit for a moment).
    // record: the place we come from goes into ◀'s list. new_tab: a tab of its own (named `label`) — or the tab that
    // already shows that entry.
    if (store == nullptr)
        return nullptr;
    const auto here = location();
    const QString here_name = here ? _place_name(here->first, here->second) : QString();
    // one tab a file: a file already open is focused there and the cursor goes to the entry (before 2026-10-17 an entry
    // opened "in a tab of its own" made a second tab of the same file, titled after the entry)
    auto [i, ed] = _editor_for(rel);
    if (ed == nullptr) {
        std::tie(i, ed) = _new_tab(rel, new_tab ? label : QString());
    } else if (new_tab && !label.isEmpty()) {
        ed->label = label;
        tabs->setTabToolTip(i, _tab_tip(ed->rel, label));
    }
    const bool jumped = record && here && !(pos && here->first == rel && here->second == *pos);
    if (jumped) {
        if (_back.empty() || _back.back() != *here) {
            _back.push_back(*here);
            if (_back.size() > 80)
                _back.erase(_back.begin(), _back.end() - 80);
        }
        _fwd.clear();
    }
    tabs->setCurrentIndex(i);
    if (pos) {
        ed->reveal(*pos);
        if (span)
            ed->reveal(span->second > *pos ? span->second : *pos);
        QTextCursor c = ed->textCursor();
        c.setPosition(int(std::max<qsizetype>(0, std::min<qsizetype>(*pos, ed->document()->characterCount() - 1))));
        ed->setTextCursor(c);
        ed->centerCursor();
        if (span)
            _flash(ed, *span);
    }
    ed->setFocus();
    _update_nav();
    if (jumped) {
        jump_lbl->setText(u"↩ You came here from %1 (line %2)"_s.arg(here_name, _line_of(here->first, here->second)));
        jump_bar->setVisible(true);
    } else if (!record) {
        jump_bar->setVisible(false);
    }
    return ed;
}

QString ScriptWorkspace::_place_name(const QString &rel, qsizetype pos) const
{
    if (refs != nullptr) {
        try {
            return refs->place_name(rel, pos);
        } catch (...) {
        }
    }
    return rel.split(u'/').last();
}

void ScriptWorkspace::_flash(ScriptEditor *ed, const std::pair<qsizetype, qsizetype> &span)
{
    ed->flash = span;
    _decorate(ed);
    QTimer::singleShot(1400, ed, [this, ed] {
        ed->flash.reset();
        _decorate(ed);
    });
}

void ScriptWorkspace::_close_tab(int i)
{
    QWidget *w = tabs->widget(i);
    tabs->removeTab(i);
    if (w != nullptr)
        w->deleteLater();
    _update_empty();
}

void ScriptWorkspace::_update_empty()
{
    const bool has = tabs->count() > 0;
    tabs->setVisible(has);
    empty->setVisible(!has);
}

void ScriptWorkspace::refresh_titles()
{
    for (int i = 0; i < tabs->count(); ++i)
        if (auto *w = qobject_cast<ScriptEditor *>(tabs->widget(i)))
            tabs->setTabText(i, _tab_title(w->rel, w->label));
}

void ScriptWorkspace::_text_changed(const QString &rel)
{
    for (int i = 0; i < tabs->count(); ++i) {
        auto *w = qobject_cast<ScriptEditor *>(tabs->widget(i));
        if (w != nullptr && _same_file(w->rel, rel))
            tabs->setTabText(i, _tab_title(w->rel, w->label));
    }
    Q_EMIT textEdited(rel);
}

// ------------------------------------------------------------------ going places
void ScriptWorkspace::follow(const refs::Ref &r, bool new_tab)
{
    // Ctrl+click / F12 / middle button on a ref: where the thing is defined (a flag: where it is set)
    if (refs == nullptr)
        return;
    const QString kind = r.kind, target = r.target;
    const auto d = refs->definition(kind, target);
    ScriptEditor *ed = current();
    if (!d || (ed != nullptr && d->first == ed->rel && d->second == r.pos)) {
        status->setText(u"%1 “%2” — nothing to go to (it isn't defined in a file); its facts are on the right, and on "
                        "the map."_s.arg(kind_name(kind), target));
        return;
    }
    const QString rel = d->first;
    const qsizetype pos = d->second;
    if (!primary && _editor_for(rel).second == nullptr) {
        Q_EMIT outside(rel, pos);                // (another file: the page it belongs to shows it)
        return;
    }
    const QString text = store->text(rel);
    // the whole block it defines (to light it for a moment)
    const qsizetype end = text.indexOf(u'\n', pos);
    std::pair<qsizetype, qsizetype> span{py::rfind(text, u'\n', pos) + 1, end < 0 ? text.size() : end};
    QString label;
    if (new_tab) {
        events::Entry *e = in_entry_files(rel) ? refs->db->entry_at(rel, pos) : nullptr;
        label = e != nullptr ? refs->db->name(*e) : u"%1: %2"_s.arg(kind_name(kind), target);
        if (e != nullptr)
            span = {e->span.first, e->span.second};
    }
    open(rel, pos, new_tab, true, span, label);
}

void ScriptWorkspace::back()
{
    jump_bar->setVisible(false);
    if (_back.empty())
        return;
    const auto here = location();
    const auto [rel, pos] = _back.back();
    _back.pop_back();
    if (here)
        _fwd.push_back(*here);
    open(rel, pos, false, false);
}

void ScriptWorkspace::forward()
{
    if (_fwd.empty())
        return;
    const auto here = location();
    const auto [rel, pos] = _fwd.back();
    _fwd.pop_back();
    if (here)
        _back.push_back(*here);
    open(rel, pos, false, false);
}

void ScriptWorkspace::_update_nav()
{
    const QString on = u"QPushButton { background: %1; color: #14100a; font-weight: 600; border: 1px solid #14100a; "
                       "border-radius: 4px; padding: 3px 12px; }"_s.arg(theme::GOLD);
    const std::vector<std::tuple<QPushButton *, const std::vector<std::pair<QString, qsizetype>> *, QString>> rows = {
        {back_btn, &_back, u"◀ Back"_s}, {fwd_btn, &_fwd, u"Forward ▶"_s}};
    for (const auto &[b, lst, t] : rows) {
        b->setEnabled(!lst->empty());
        b->setStyleSheet(!lst->empty() ? on : QString());
        const bool is_back = t.startsWith(u"◀"_s);
        if (!lst->empty()) {
            const auto &last = lst->back();
            const QString name = _place_name(last.first, last.second);
            b->setToolTip(u"%1 to %2 (%3 line %4) — Alt+%5, the mouse's side buttons"_s.arg(
                t, name, last.first, _line_of(last.first, last.second), is_back ? u"←"_s : u"→"_s));
            const QString shrt = name.size() <= 28 ? name : name.left(26) + u"…"_s;
            b->setText(is_back ? u"◀ Back: %1"_s.arg(shrt) : u"Forward: %1 ▶"_s.arg(shrt));
        } else {
            b->setToolTip(QString());
            b->setText(t);
        }
    }
}

QString ScriptWorkspace::_line_of(const QString &rel, qsizetype pos) const
{
    if (store == nullptr)
        return u"?"_s;
    return QString::number(refs::line_of(store->text(rel), pos));
}

void ScriptWorkspace::zoom(int step)
{
    // The text bigger / smaller (0: back to normal) — every tab, remembered.
    font_size = step == 0 ? 10 : std::max(6, std::min(30, font_size + step));
    for (int i = 0; i < tabs->count(); ++i)
        if (auto *w = qobject_cast<ScriptEditor *>(tabs->widget(i)))
            w->set_size(font_size);
    status->setText(u"Text size %1 (Ctrl + wheel / Ctrl+= / Ctrl+− · Ctrl+0: normal)"_s.arg(font_size));
    settings_::settings()->setValue(u"scriptFontSize"_s, font_size);
}

void ScriptWorkspace::_search_go(const QString &rel, qsizetype a, qsizetype b)
{
    ScriptEditor *ed = open(rel, a);
    if (ed != nullptr) {
        QTextCursor c = ed->textCursor();
        c.setPosition(int(a));
        c.setPosition(int(b), QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
        ed->centerCursor();
    }
}

void ScriptWorkspace::set_focus(const QString &kind, const QString &target)
{
    // A thing chosen outside the text (a nation right-clicked on the map): lit in the text like the word at the cursor.
    if (!kind.isEmpty())
        _focus = std::make_pair(kind, target);
    else
        _focus.reset();
    _decorate_current();
}

void ScriptWorkspace::_decorate_current()
{
    if (ScriptEditor *ed = current())
        _decorate(ed);
}

// ------------------------------------------------------------------ what the cursor is on
QString ScriptWorkspace::hover_html(const QString &, const refs::Ref &r) const
{
    static const QSet<QString> MAPPED = {u"nation"_s, u"province"_s, u"state"_s, u"culture"_s, u"cgroup"_s,
                                         u"religion"_s, u"rgroup"_s, u"goods"_s, u"continent"_s, u"cflag"_s,
                                         u"gflag"_s};
    const QStringList extra = info_extra ? info_extra(r.kind, r.target) : QStringList();
    const bool go = bool(refs->definition(r.kind, r.target));
    const QString tip = (go ? u"Ctrl+click: go there · double-click: in a tab of its own · "_s : QString()) +
                        u"click: its details on the right"_s + (MAPPED.contains(r.kind) ? u" and on the map"_s : QString());
    return u"<div style=\"max-width:560px\">"_s + refs->info_html(r.kind, r.target, extra) +
           u"<br><span style=\"color:#8a8272\">%1</span></div>"_s.arg(tip);
}

void ScriptWorkspace::_cursor_moved(ScriptEditor *ed)
{
    if (ed == nullptr || ed != current()) {
        if (ed == nullptr)
            where->setText(QString());
        return;
    }
    const QTextCursor c = ed->textCursor();
    _decorate(ed);
    QString extra;
    if (ed->bracket) {                           // where the bracket at the cursor has its partner
        if (!ed->bracket_lines)
            extra = u"  ·  ⚠ this bracket has no partner"_s;
        else
            extra = u"  ·  { line %1 … } line %2  ·  Ctrl+M: to the other one"_s.arg(ed->bracket_lines->first + 1)
                        .arg(ed->bracket_lines->second + 1);
    }
    where->setText(u"%1 · line %2, column %3%4"_s.arg(ed->rel, QString::number(c.blockNumber() + 1),
                                                       QString::number(c.positionInBlock() + 1), extra));
    _sym_timer->start();
}

void ScriptWorkspace::set_problems(const ProblemsByRel &by_rel)
{
    // What the checks found in the scripts: {file: [(position or None, severity, text)]}. The lines get their marker
    // (it stays on its line while you type; the next check puts it right).
    if (by_rel == problems)
        return;
    problems = by_rel;
    _prob_gen += 1;
    for (int i = 0; i < tabs->count(); ++i) {
        if (auto *w = qobject_cast<ScriptEditor *>(tabs->widget(i))) {
            _problem_marks(w);
            w->area->update();
        }
    }
    if (ScriptEditor *ed = current())
        _decorate(ed);
}

void ScriptWorkspace::_problem_marks(ScriptEditor *ed)
{
    if (ed->_prob_gen == _prob_gen)
        return;
    ed->_prob_gen = _prob_gen;
    ed->prob.clear();
    const int last = std::max(0, ed->document()->characterCount() - 1);
    auto it = problems.find(ed->rel);
    if (it == problems.end())
        return;
    for (const Problem &pr : it->second) {
        if (!pr.pos)                             // (about the file as a whole: no line to mark)
            continue;
        QTextCursor c(ed->document());
        c.setPosition(int(std::min<qsizetype>(std::max<qsizetype>(0, *pr.pos), last)));
        ed->prob.emplace_back(c, pr.sev, pr.text);
    }
}

void ScriptWorkspace::_decorate(ScriptEditor *ed)
{
    // The current line; the lines the checks complain about (marker); the bracket at the cursor and its partner; every
    // other place of the word under the cursor (lit in its colour); a block just jumped to.
    QList<QTextEdit::ExtraSelection> sels;
    QTextEdit::ExtraSelection cl;
    cl.format.setBackground(QColor(30, 34, 42));
    cl.format.setProperty(QTextFormat::FullWidthSelection, true);
    cl.cursor = ed->textCursor();
    cl.cursor.clearSelection();
    sels.append(cl);
    ed->folds();                                 // (after typing: folds and lone brackets are looked at again)
    _problem_marks(ed);
    for (const auto &[line, st] : ed->problem_lines()) {
        const QTextBlock blk = ed->document()->findBlockByNumber(int(line));
        if (!blk.isValid())
            continue;
        QTextEdit::ExtraSelection s;
        s.format.setBackground(mark_colour(st.first));
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = QTextCursor(blk);
        sels.append(s);
    }
    const auto br = !ed->textCursor().hasSelection() ? ed->bracket_near(ed->textCursor().position())
                                                     : std::optional<std::pair<qsizetype, std::optional<qsizetype>>>();
    std::optional<std::pair<long long, long long>> lines;
    if (br) {
        for (const std::optional<qsizetype> &p : {std::optional<qsizetype>(br->first), br->second}) {
            if (!p)
                continue;
            QTextEdit::ExtraSelection s;
            // (gold: the pair; red: a bracket that has no partner)
            s.format.setBackground(br->second ? gold() : QColor::fromString(theme::ERROR_));
            s.format.setForeground(br->second ? QColor(u"#14100a"_s) : QColor(u"#ffffff"_s));
            s.cursor = QTextCursor(ed->document());
            s.cursor.setPosition(int(*p));
            s.cursor.setPosition(int(*p + 1), QTextCursor::KeepAnchor);
            sels.append(s);
        }
        if (br->second) {
            const qsizetype a = std::min(br->first, *br->second), b = std::max(br->first, *br->second);
            lines = std::make_pair<long long, long long>(ed->document()->findBlock(int(a)).blockNumber(),
                                                         ed->document()->findBlock(int(b)).blockNumber());
        }
    }
    if (br != ed->bracket || lines != ed->bracket_lines) {
        ed->bracket = br;
        ed->bracket_lines = lines;
        ed->area->update();
    }
    ed->partner_hint();
    if (ed->flash) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(90, 75, 30));
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = QTextCursor(ed->document());
        s.cursor.setPosition(int(ed->flash->first));
        s.cursor.setPosition(int(std::min<qsizetype>(ed->flash->second, ed->document()->characterCount() - 1)),
                             QTextCursor::KeepAnchor);
        sels.append(s);
    }
    const qsizetype n = ed->document()->characterCount();
    // only around what is on screen (a few screens up and down): the file may have thousands
    const qsizetype top = ed->first_visible().position();
    const qsizetype bottom = ed->cursorForPosition(QPoint(0, ed->viewport()->height())).position();
    const qsizetype span = std::max<qsizetype>(4000, bottom - top);
    const qsizetype lo = std::max<qsizetype>(0, top - 2 * span), hi = std::min(n, bottom + 2 * span);
    const auto found = search->matches_in(ed->rel, lo, hi);
    for (size_t i = 0; i < found.size() && i < 800; ++i) {
        const auto &[a, b] = found[i];
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(200, 150, 40, 120));
        s.cursor = QTextCursor(ed->document());
        s.cursor.setPosition(int(a));
        s.cursor.setPosition(int(std::min(b, n - 1)), QTextCursor::KeepAnchor);
        sels.append(s);
    }
    const refs::Ref *r = refs != nullptr ? refs->at(ed->rel, ed->textCursor().position()) : nullptr;
    const auto want = r != nullptr ? std::optional<std::pair<QString, QString>>(std::make_pair(r->kind, r->target))
                                   : _focus;
    if (want && refs != nullptr) {
        const auto [cr, cg, cb] = kind_rgb(want->first, {200, 200, 200});
        for (const refs::Ref &o : refs->in_range(ed->rel, lo, hi)) {
            if (o.kind == want->first && o.target == want->second && o.pos + o.length < n) {
                QTextEdit::ExtraSelection s;
                s.format.setBackground(QColor(cr, cg, cb, 70));
                s.cursor = QTextCursor(ed->document());
                s.cursor.setPosition(int(o.pos));
                s.cursor.setPosition(int(o.pos + o.length), QTextCursor::KeepAnchor);
                sels.append(s);
            }
        }
    }
    ed->setExtraSelections(sels);
}

void ScriptWorkspace::_emit_place()
{
    ScriptEditor *ed = current();
    if (ed == nullptr) {
        Q_EMIT symbolChanged(std::nullopt);
        return;
    }
    const QTextCursor c = ed->textCursor();
    const std::vector<refs::Ref> sel = refs != nullptr && c.hasSelection()
                                           ? refs->in_range(ed->rel, c.selectionStart(), c.selectionEnd())
                                           : std::vector<refs::Ref>();
    std::vector<std::pair<QString, QString>> key;
    for (const refs::Ref &r : sel)
        key.emplace_back(r.kind, r.target);
    if (key != _last_sel.value_or(std::vector<std::pair<QString, QString>>())) {
        _last_sel = key;
        Q_EMIT selectionRefs(sel);
    }
    const qsizetype pos = ed->textCursor().position();
    const refs::Ref *r = refs != nullptr ? refs->at(ed->rel, pos) : nullptr;
    const auto k = r != nullptr ? std::optional<std::pair<QString, QString>>(std::make_pair(r->kind, r->target))
                                : std::nullopt;
    if (k != _last_sym) {
        _last_sym = k;
        if (r != nullptr)
            _focus.reset();                      // the cursor's word takes over from a map right-click
        if (r != nullptr || !_focus)
            Q_EMIT symbolChanged(r != nullptr ? std::optional<refs::Ref>(*r) : std::nullopt);
    }
    Q_EMIT placeChanged(ed->rel, pos);
}

// ------------------------------------------------------------------ the map and the relations: the mini window
bool ScriptWorkspace::map_visible() const
{
    if (map_mode == u"dock")
        return _dock_on;
    return !mini->isHidden() && mini->stack->currentIndex() == 0;
}

bool ScriptWorkspace::rel_visible() const
{
    return !mini->isHidden() && mini->stack->currentIndex() == 1;
}

void ScriptWorkspace::_sync()
{
    const bool mv = map_visible();
    map_btn->setChecked(mv);
    undock_btn->setVisible(map_mode == u"dock" && mv);
    rel_btn->setChecked(rel_visible());
    legend->setVisible(mv);
    Q_EMIT mapToggled(mv);
    if (rel_visible())
        Q_EMIT relationsShown();
}

void ScriptWorkspace::_show_mini(const QString &page)
{
    mini->set_page(page);
    if (mini->isHidden()) {
        mini->show();
        if (!mini->side)
            mini->place();
    }
    mini->raise();
}

void ScriptWorkspace::mini_to_side()
{
    // The small window floats over the side panel instead (at its top, where it stays while the panel scrolls; moved
    // by its bar, resized at ◣ — like over the text).
    if (side_area == nullptr)
        return;
    MiniView *m = mini;
    m->set_side(true);
    m->setParent(side_area);
    m->setMinimumHeight(0);
    m->setMaximumHeight(16777215);
    m->resize(std::min(m->_want.first, std::max(280, side_area->width() - 30)), m->_want.second);
    m->_right = 22;
    m->_top = 8;
    m->show();
    m->place();
    m->raise();
    _remember_side(true);
    _sync();
}

void ScriptWorkspace::tab_shown(bool on)
{
    // The Events tab shown / left: over the side panel (shared by all tabs) the small window only shows here.
    if (!mini->side)
        return;
    if (!on) {
        _mini_was = !mini->isHidden();
        _mini_away = true;
        mini->hide();
    } else if (_mini_away) {
        _mini_away = false;
        if (_mini_was) {
            mini->show();
            mini->place();
            mini->raise();
        }
    }
}

void ScriptWorkspace::mini_to_float(std::optional<QPoint> pos)
{
    MiniView *m = mini;
    m->set_side(false);
    m->setParent(this);
    m->setMinimumHeight(0);
    m->setMaximumHeight(16777215);
    m->resize(m->_want.first, m->_want.second);
    m->show();
    if (pos) {
        m->move(std::max(0, std::min(pos->x() - m->width() / 2, width() - m->width())), std::max(0, pos->y() - 12));
        m->_right = width() - m->x() - m->width();
        m->_top = m->y();
    } else {
        m->place();
    }
    m->raise();
    _remember_side(false);
    _sync();
}

void ScriptWorkspace::_remember_side(bool on)
{
    settings_::settings()->setValue(u"eventsMiniSide"_s, on);
}

void ScriptWorkspace::set_side_host(QWidget *area, QWidget *host)
{
    side_area = area;
    side_host = host;
    area->installEventFilter(this);              // the side panel resized: the small window keeps its place
    if (settings_::settings()->value(u"eventsMiniSide"_s, false).toBool())
        QTimer::singleShot(0, this, [this] {
            mini_to_side();
            mini->hide();
            _sync();
        });
}

void ScriptWorkspace::show_map()
{
    if (map_mode == u"dock")
        _dock_on = true;
    else
        _show_mini(u"map"_s);
    _sync();
}

void ScriptWorkspace::_map_clicked()
{
    if (map_visible()) {
        if (map_mode == u"dock")
            _dock_on = false;
        else
            mini->hide();
        _sync();
    } else {
        show_map();
    }
}

void ScriptWorkspace::_rel_clicked()
{
    if (rel_visible())
        mini->hide();
    else
        _show_mini(u"rel"_s);
    _sync();
}

void ScriptWorkspace::_mini_page(const QString &page)
{
    if (page == u"map" && map_mode == u"dock") {
        undock_map();                            // undocked: back into the small window
        return;
    }
    _show_mini(page);
    _sync();
}

void ScriptWorkspace::_dock_map()
{
    map_mode = u"dock"_s;
    _dock_on = true;
    mini->hide();
    _sync();
}

void ScriptWorkspace::undock_map()
{
    map_mode = u"mini"_s;
    _dock_on = false;
    _sync();                                     // (the map leaves the space under the text first)
    QTimer::singleShot(0, this, [this] {
        _show_mini(u"map"_s);
        mini->place();
        _sync();
    });
}

void ScriptWorkspace::_mini_closed()
{
    mini->hide();
    _sync();
}

void ScriptWorkspace::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (mini->isVisible() && !mini->side)
        mini->place();
}

bool ScriptWorkspace::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == side_area && e->type() == QEvent::Resize && mini->side && mini->isVisible())
        mini->place();
    return QWidget::eventFilter(obj, e);
}

}  // namespace scriptedit
