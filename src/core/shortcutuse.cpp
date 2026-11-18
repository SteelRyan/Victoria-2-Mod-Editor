// shortcutuse.py — what a game button is for, and whether a shortcut on it makes sense. Line by line.
#include "core/shortcutuse.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/shortcutfacts.h"

using namespace Qt::StringLiterals;

namespace shortcutuse {

namespace {
#include "core/shortcutuse_data.inc"

template <size_t N> OrderedMap<QString, QString> pairs(const char16_t *const (&d)[N][2])
{
    OrderedMap<QString, QString> out;
    for (const auto &r : d)
        out.insert(QString::fromUtf16(r[0]), QString::fromUtf16(r[1]));
    return out;
}
template <size_t N> OrderedMap<QString, std::pair<QString, QString>> triples(const char16_t *const (&d)[N][3])
{
    OrderedMap<QString, std::pair<QString, QString>> out;
    for (const auto &r : d)
        out.insert(QString::fromUtf16(r[0]), {QString::fromUtf16(r[1]), QString::fromUtf16(r[2])});
    return out;
}
const QString SLIDER_RE = u"(^|_)(slider|track)$|sliderbutton$|trackbutton$"_s;
const QString UPDOWN_RE = u"(^|_)(up|down)(button)?$"_s;

bool is_bg(const QString &low)
{
    return low == u"bg" || low == u"background" || low.endsWith(u"_bg") || low.endsWith(u"background");
}
QString us(QString s) { return s.replace(u'_', u' '); }
}  // namespace

const OrderedMap<QString, QString> CLASS_NAME = pairs(CLASS_NAME_DATA);
const OrderedMap<QString, std::pair<QString, QString>> WINDOWS = triples(WINDOWS_DATA);
const OrderedMap<QString, std::pair<QString, QString>> PLACES = triples(PLACES_DATA);
const QStringList LEFTOVER = [] {
    QStringList out;
    for (const char16_t *s : LEFTOVER_DATA)
        out << QString::fromUtf16(s);
    return out;
}();
const OrderedMap<QString, QString> ROWS = pairs(ROWS_DATA);
const OrderedMap<QString, QString> WHAT = pairs(WHAT_DATA);
const OrderedMap<QString, QString> SORT_WORD = pairs(SORT_WORD_DATA);

QString _pretty(const QString &name)
{
    QString s = py::re_sub(u"(?<=[a-z])(?=[A-Z])"_s, name, [](const QRegularExpressionMatch &) { return u" "_s; });
    s = py::lower(py::strip(s.replace(u'_', u' ')));
    const QString r = py::strip(py::re_sub(u"\\b(button|btn|checkbox)\\b"_s, s, [](const QRegularExpressionMatch &) { return QString(); }));
    return !r.isEmpty() ? r : (!name.isEmpty() ? name : u"(no name)"_s);
}

std::pair<QString, QString> window_title(const QString &top, const QString &rel)
{
    // (what the window is, when it is on screen) in plain words
    if (const auto *got = WINDOWS.get(top))
        return *got;
    if (ROWS.contains(top))
        return {u"One %1 (a row of a list)"_s.arg(ROWS.value(top)), u"made once for every %1 showing"_s.arg(ROWS.value(top))};
    const QString fn = py::lower(pypath::basename(rel));
    if (PLACES.contains(fn))
        return {py::capitalize(_pretty(top)), u"in "_s + PLACES.value(fn).second};
    return {!top.isEmpty() ? py::capitalize(_pretty(top)) : u"Made by the game where needed"_s, QString()};
}

std::optional<QString> is_row(const Button &b)
{
    // Is the button made once per row of a list? -> what a row is, or None
    const QString top = !b.path.isEmpty() ? b.path[0] : QString();
    if (b.path.isEmpty())
        return u"entry"_s;
    if (ROWS.contains(top))
        return ROWS.value(top);
    if (!WINDOWS.contains(top) && py::re_search(u"(_entry|_item|listitem|_member)(_|$)"_s, top, 0, true).hasMatch()) {
        const QString p = _pretty(py::re_sub(u"(_entry|_item|listitem|_member).*$"_s, top,
                                             [](const QRegularExpressionMatch &) { return QString(); }));
        return !p.isEmpty() ? p : u"row"_s;
    }
    return std::nullopt;
}

QString what(const Button &b, const QString &says)
{
    // What the button does, in a plain line.
    const QString name = b.name;
    const QString low = py::lower(name);
    const QString parent = !b.path.isEmpty() ? b.path.last() : QString();
    if (py::re_search(SLIDER_RE, low).hasMatch())
        return u"The body of the slider “%1” (dragged with the mouse)"_s.arg(_pretty(parent));
    auto m = py::re_search(UPDOWN_RE, low);
    if (m.hasMatch() && (py::lower(parent).contains(u"slider"_s) || py::lower(parent).contains(u"scroll"_s) || low == u"up" ||
                         low == u"down" || low == u"upbutton" || low == u"downbutton"))
        return u"Slider “%1”: one step %2"_s.arg(_pretty(parent), m.captured(2));
    if (is_bg(low)) {
        if (WHAT.contains(name))
            return WHAT.value(name);
        return u"The window's background (nothing to press)"_s;
    }
    if (WHAT.contains(name))
        return WHAT.value(name);
    {
        auto m1 = py::re_match(u"^(?:(.*)_)?sort_?by_(.+)$"_s, low);
        const bool first = m1.hasMatch();
        if (!first)
            m1 = py::re_match(u"^sort_(.+)$"_s, low);
        if (m1.hasMatch()) {
            const int last = first ? 2 : 1;                  // (Python: m.lastindex)
            QString col = m1.captured(last).replace(u"_button"_s, QString()).replace(u'_', u' ');
            col = SORT_WORD.contains(col) ? SORT_WORD.value(col) : QString(col).replace(u"leader "_s, u"the leader's "_s);
            const QString lst = (last == 2 && !m1.captured(1).isEmpty()) ? u" of “%1”"_s.arg(_pretty(m1.captured(1))) : QString();
            if (!b.path.isEmpty() && b.path[0] == u"naval_combat")
                return u"Sorts the ships of the sea battle by %1"_s.arg(col);
            return u"Sorts the list%1 by %2"_s.arg(lst, col);
        }
    }
    {
        auto m1 = py::re_match(u"^(?:pop)?filter_(.+)$"_s, low);
        if (!m1.hasMatch())
            m1 = py::re_match(u"^messagecat_(.+)$"_s, low);
        if (m1.hasMatch())
            return u"Shows only: %1"_s.arg(us(m1.captured(1)));
    }
    {
        auto m1 = py::re_match(u"^tab_(.+)$"_s, low);
        if (!m1.hasMatch())
            m1 = py::re_match(u"^(.+)_tab$"_s, low);
        if (m1.hasMatch())
            return u"Opens the tab “%1”"_s.arg(us(m1.captured(1)));
    }
    if (const auto m1 = py::re_match(u"^mapmode_(\\d+)$"_s, low); m1.hasMatch())
        return u"Map mode %1%2"_s.arg(m1.captured(1), !says.isEmpty() ? u" — %1"_s.arg(says)
                                                                       : u" (the row of map buttons at the lower right)"_s);
    if (const auto m1 = py::re_match(u"^outliner_view_(.+)$"_s, low); m1.hasMatch())
        return u"Outliner: show / hide “%1”"_s.arg(us(m1.captured(1)));
    if (const auto m1 = py::re_match(u"^menubar_msg_(.+)$"_s, low); m1.hasMatch())
        return u"Message log: show / hide “%1” messages"_s.arg(m1.captured(1));
    if (low.contains(u"optionbutton"_s))
        return u"An answer of the event (one button per answer)"_s;
    if (low.contains(u"flag"_s) || low == u"shield")
        return u"A flag: jumps to that nation"_s;
    if (low.endsWith(u"_icon") || low.contains(u"_icon_"_s) || low.startsWith(u"icon"))
        return u"A small picture that shows a state of things"_s;
    if (!says.isEmpty())
        return u"“%1”"_s.arg(says);
    return u"“%1”"_s.arg(_pretty(name));
}

std::tuple<QString, QString, QString> classify(const Button &b, const QString &says)
{
    // -> (class, why — in a sentence, what it does)
    const QString fn = py::lower(pypath::basename(b.rel));
    const QString name = b.name;
    const QString low = py::lower(name);
    const QString parent = py::lower(!b.path.isEmpty() ? b.path.last() : QString());
    const QString does = what(b, says);
    const auto *fact = shortcutfacts::FACTS().get(u"%1|%2|%3|%4"_s.arg(fn, b.path.join(u'/'), b.name, QString::number(b.nth)));
    QString tested;
    if (fact) {
        static const QHash<QString, QString> T = {
            {u"works"_s, u" In the test its key worked."_s},
            {u"small"_s, u" In the test its key worked."_s},
            {u"taken"_s, u" In the test the game took its key."_s},
            {u"last"_s, QString()},
            {u"dead"_s, u" In the test its key did nothing (it was greyed out or not on screen then)."_s},
            {u"gone"_s, u" In the test its key did nothing (it was greyed out or not on screen then)."_s},
            {u"silent"_s, QString()}};
        tested = T.value(fact->cls);
    }
    if (LEFTOVER.contains(fn))
        return {u"none"_s, u"This window is left over from another game of the same engine (Hearts of Iron 3) — Victoria 2 "
                           u"does not show it, as far as the test and the game's screens go."_s, does};
    if (py::re_search(SLIDER_RE, low).hasMatch())
        return {u"none"_s, u"Not a button: the body of a slider. The game never takes a key here (tested on every slider "
                           u"of the budget and trade screens)."_s, does};
    if (is_bg(low) && !WHAT.contains(name))
        return {u"none"_s, u"Not a button: the window's background. There is nothing a key could press."_s, does};
    const auto row = is_row(b);
    if (low.contains(u"optionbutton"_s))
        return {u"niche"_s, u"One button for every answer of an event: the key presses the LAST answer. Fine for events "
                            u"with a single answer (like Enter), a trap for the others."_s, does};
    if (row) {
        if (fact && fact->cls == u"last")
            return {u"none"_s, fact->text + u"."_s, does};
        return {u"none"_s, u"The game makes this button once for every %1 in a list, and all of them share the one key: "
                           u"it presses the LAST one showing — it cannot pick one (tested on decisions, reform options "
                           u"and nations to release).%2"_s.arg(*row != u"entry" ? *row : u"row"_s, tested), does};
    }
    if (low.endsWith(u"_icon") || low.contains(u"_icon_"_s))
        return {u"none"_s, u"A small picture that shows a state of things, not something to press.%1"_s.arg(tested), does};
    if (PLACES.contains(fn))
        return {u"niche"_s, u"Only in %1.%2"_s.arg(PLACES.value(fn).second, tested), does};
    if (fn == u"datewidget.gui")
        return {u"niche"_s, u"Only on the start screen's date picker."_s, does};
    const auto m = py::re_search(UPDOWN_RE, low);
    if (m.hasMatch() && (parent.contains(u"slider"_s) || parent.contains(u"scroll"_s) || low == u"up" || low == u"down" ||
                         low == u"upbutton" || low == u"downbutton"))
        return {u"niche"_s, u"Works: every press moves the slider one step (tested). Only useful if you want that one "
                            u"slider on a key.%1"_s.arg(tested.contains(u"worked"_s) ? QString() : tested), does};
    if (py::re_match(u"^(.*_)?sort_?(by_)?"_s, low).hasMatch() && low.contains(u"sort"_s))
        return {u"niche"_s, u"Works while its list is on screen: it sorts the list (again: the other way round). "
                            u"Narrow — a key for one column of one list.%1"_s.arg(tested), does};
    if (py::re_match(u"^(pop)?filter_|^messagecat_|^menubar_msg_(?!settings)|^outliner_view_|_filter"_s, low).hasMatch())
        return {u"niche"_s, u"Works while its list is on screen: it filters what the list shows. Narrow — a key for one "
                            u"filter of one list.%1"_s.arg(tested), does};
    if ((fn == u"messagesettings.gui" || fn == u"msg_filters.gui") &&
        !(name == u"ok_button" || name == u"cancel_button" || name == u"close_button"))
        return {u"niche"_s, u"A tick box of the message settings — set once, hardly ever again."_s, does};
    if (low.contains(u"flag"_s) || low == u"shield")
        return {u"niche"_s, u"A flag that jumps to its nation — works, but it is not an action.%1"_s.arg(tested), does};
    if (py::re_match(u"^unit_folder_\\d+$"_s, low).hasMatch())
        return {u"niche"_s, u"A folder of the large build-units window — which one is which was not tested."_s, does};
    const QString top = !b.path.isEmpty() ? b.path[0] : QString();
    const QString when = window_title(top, b.rel).second;
    return {u"good"_s, u"Stands once in its window%1: the key presses exactly this button while the window is on "
                       u"screen.%2"_s.arg(!when.isEmpty() ? u" (%1)"_s.arg(when) : QString(), tested), does};
}

}  // namespace shortcutuse
