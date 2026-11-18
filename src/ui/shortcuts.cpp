// shortcuts.py — Rules → Shortcuts (see shortcuts.h).
#include "ui/shortcuts.h"
#include "ui/drawtext.h"
#include "core/document.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/shortcutuse.h"
#include "map/gamefont.h"
#include "ui/textselect.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QScrollArea>
#include <QSplitter>
#include <QTextDocument>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <map>

using namespace Qt::StringLiterals;
using locwin::_kid;
using locwin::_num;
using locwin::_xy;
using locwin::XY;
using widgets::label;
using widgets::Section;

namespace shortcuts {

const QStringList NAMED = {u"ESCAPE"_s, u"ENTER"_s, u"LEFT"_s, u"RIGHT"_s, u"DEL"_s, u"PAGE_UP"_s, u"PAGE_DOWN"_s,
                           u"F1"_s, u"F2"_s, u"F3"_s, u"F4"_s, u"F5"_s, u"F6"_s, u"F7"_s, u"F8"_s};
const OrderedMap<QString, QString> SCREENS = [] {
    OrderedMap<QString, QString> m;
    m.insert(u"topbarbutton_production"_s, u"country_production.gui"_s);
    m.insert(u"topbarbutton_budget"_s, u"country_budget.gui"_s);
    m.insert(u"topbarbutton_tech"_s, u"country_technology.gui"_s);
    m.insert(u"topbarbutton_politics"_s, u"country_politics.gui"_s);
    m.insert(u"topbarbutton_pops"_s, u"country_pops.gui"_s);
    m.insert(u"topbarbutton_trade"_s, u"country_trade.gui"_s);
    m.insert(u"topbarbutton_diplomacy"_s, u"country_diplomacy.gui"_s);
    m.insert(u"topbarbutton_military"_s, u"country_military.gui"_s);
    return m;
}();

OptStr key_name(int code, const QString &typed)
{
    // the game's name for a pressed key (its Qt code and the character it typed), or None
    static const std::map<int, QString> QT_NAMES = [] {
        std::map<int, QString> m = {
            {Qt::Key_Escape, u"ESCAPE"_s},   {Qt::Key_Return, u"ENTER"_s},  {Qt::Key_Enter, u"ENTER"_s},
            {Qt::Key_Left, u"LEFT"_s},       {Qt::Key_Right, u"RIGHT"_s},   {Qt::Key_Up, u"UP"_s},
            {Qt::Key_Down, u"DOWN"_s},       {Qt::Key_Delete, u"DEL"_s},    {Qt::Key_PageUp, u"PAGE_UP"_s},
            {Qt::Key_PageDown, u"PAGE_DOWN"_s}, {Qt::Key_Home, u"HOME"_s},  {Qt::Key_End, u"END"_s},
            {Qt::Key_Insert, u"INSERT"_s},   {Qt::Key_Backspace, u"BACKSPACE"_s}, {Qt::Key_Tab, u"TAB"_s},
            {Qt::Key_Space, u"SPACE"_s}};
        for (int i = 0; i < 12; ++i)
            m[Qt::Key_F1 + i] = u"F"_s + QString::number(i + 1);
        return m;
    }();
    static const std::set<int> MODIFIERS = {Qt::Key_Shift, Qt::Key_Control, Qt::Key_Alt,    Qt::Key_Meta,
                                            Qt::Key_AltGr, Qt::Key_CapsLock, Qt::Key_unknown};
    if (MODIFIERS.contains(code))
        return std::nullopt;
    if (const auto it = QT_NAMES.find(code); it != QT_NAMES.end())
        return it->second;
    if (typed.size() == 1 && py::isprintable(typed[0]) && !py::isspace(typed))
        return py::lower(typed);
    if ((Qt::Key_A <= code && code <= Qt::Key_Z) || (Qt::Key_0 <= code && code <= Qt::Key_9))
        return py::lower(QString(QChar(code)));
    return std::nullopt;
}

bool known(const QString &key) { return key.size() == 1 || NAMED.contains(py::upper(key)); }

QString tidy(const QString &key0)
{
    // a key as it is written into the file: one character in lower case, a name in capitals
    const QString key = py::strip(py::strip(key0), u"\""_s);
    return key.size() == 1 ? py::lower(key) : py::upper(key).replace(u' ', u'_');
}

const shortcutfacts::Fact *fact_of(const Button &b)
{
    return shortcutfacts::FACTS().get(py::lower(pypath::basename(b.rel)) + u'|' + b.path.join(u'/') + u'|' + b.name +
                                      u'|' + QString::number(b.nth));
}

namespace {

QString lkey0(const script::Node &n) { return n.has_key ? py::lower(n.key) : QString(); }

void _walk(const script::Nodes &nodes, const QStringList &path,
           std::vector<std::pair<const script::Node *, QStringList>> &found)
{
    // every button / tick box below `nodes` with the names of the windows it sits in: (node, path)
    for (const script::Node &n : nodes) {
        if (!n.is_block)
            continue;
        const QString k = lkey0(n);
        if (KINDS.contains(k))
            found.emplace_back(&n, path);
        const OptStr name = _val(n, u"name"_s);
        QStringList sub = path;
        if (name && !name->isEmpty() && !KINDS.contains(k))
            sub << *name;
        _walk(n.kids, sub, found);
    }
}

const script::Node *shortcut_of(const script::Node &n)
{
    for (const script::Node &c : n.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == u"shortcut" && !c.is_block)
            return &c;
    return nullptr;
}

QString rstrip_blank(const QString &s)          // s.rstrip(" \t")
{
    qsizetype e = s.size();
    while (e > 0 && (s[e - 1] == u' ' || s[e - 1] == u'\t'))
        --e;
    return s.left(e);
}

}  // namespace

std::vector<Button> scan(const QString &rel, const QString &text)
{
    // the buttons of one .gui text, in the file's order
    std::vector<std::pair<const script::Node *, QStringList>> found;
    std::vector<Button> out;
    std::map<std::pair<QStringList, QString>, long long> seen;
    script::Nodes tree;
    try {
        tree = script::parse(text);
        _walk(tree, {}, found);
    } catch (...) {
        return out;
    }
    for (const auto &[n, path] : found) {
        const QString name = _val(*n, u"name"_s).value_or(QString());
        long long &s = seen[{path, name}];
        const long long nth = s;
        s = nth + 1;
        const script::Node *sc = shortcut_of(*n);
        Button b;
        b.rel = rel;
        b.name = name;
        b.kind = lkey0(*n);
        b.path = path;
        b.nth = nth;
        b.key = sc ? tidy(sc->value) : QString();
        b.has = sc != nullptr;
        b.says = _val(*n, u"buttontext"_s).value_or(QString());
        b.tip = _val(*n, u"tooltip"_s).value_or(QString());
        out.push_back(b);
    }
    return out;
}

std::optional<QString> set_shortcut(const QString &text, const Ident &ident, const QString &key0)
{
    // the text with the shortcut of the button `ident` (rel, path, name, nth) set to `key` ("" = none).
    // -> the new text, or None when the button is not there (any more). (A text that does not parse raises.)
    const auto &[rel_, path, name, nth] = ident;
    Q_UNUSED(rel_);
    std::vector<std::pair<const script::Node *, QStringList>> found;
    std::map<std::pair<QStringList, QString>, long long> seen;
    const script::Nodes tree = script::parse(text);
    _walk(tree, {}, found);
    for (const auto &[n, p] : found) {
        const QString nm = _val(*n, u"name"_s).value_or(QString());
        long long &s = seen[{p, nm}];
        const long long i = s;
        s = i + 1;
        if (!(p == path && nm == name && i == nth))
            continue;
        const script::Node *sc = shortcut_of(*n);
        const QString key = tidy(key0);
        const QString q = u"\""_s + key + u"\""_s;
        if (sc) {
            if (!key.isEmpty())
                return text.left(sc->vstart) + q + text.mid(sc->vend);
            const qsizetype st = sc->kstart, e = sc->vend;   // no shortcut: the line goes when nothing else is on it
            const qsizetype ls = py::rfind(text, u'\n', st) + 1;
            qsizetype le = text.indexOf(u'\n', e);
            le = le < 0 ? text.size() : le;
            if (py::strip(py::slice(text, ls, st)).isEmpty() && py::strip(py::slice(text, e, le)).isEmpty())
                return text.left(ls) + py::tail(text, le + 1);
            return rstrip_blank(text.left(st)) + text.mid(e);
        }
        if (key.isEmpty())
            return text;
        if (n->kids.empty())
            return text.left(n->vend - 1) + u" shortcut = "_s + q + u" "_s + text.mid(n->vend - 1);
        const script::Node &last = n->kids.back();
        const script::Node &first = n->kids.front();
        const qsizetype ls = py::rfind(text, u'\n', first.kstart) + 1;
        const QString lead = py::slice(text, ls, first.kstart);
        const std::optional<QString> indent = py::strip(lead).isEmpty() ? std::optional<QString>(lead) : std::nullopt;
        qsizetype eol = text.indexOf(u'\n', last.vend);   // a line of its own, below the last one (and its comment)
        eol = eol < 0 ? text.size() : eol;
        eol -= (eol && text[eol - 1] == u'\r') ? 1 : 0;
        const QString tl = py::strip(py::slice(text, last.vend, eol));
        if (!indent || py::rfind(text, u'\n', last.kstart) < n->vstart || (!tl.isEmpty() && !tl.startsWith(u'#')))
            return text.left(last.vend) + u" shortcut = "_s + q + text.mid(last.vend);   // the button on one line
        const QString nl = text.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
        return text.left(eol) + nl + *indent + u"shortcut = "_s + q + text.mid(eol);
    }
    return std::nullopt;
}

QStringList gui_files(const moddata::Mod &mod)
{
    // [path below the mod, e.g. "interface/topbar.gui"] of every interface file the game reads with the mod
    std::map<QString, QString> names;
    QStringList roots;
    if (mod.base && !mod.base->isEmpty() && !mod.replaced({u"interface"_s}))
        roots << *mod.base;
    roots << mod.root;
    for (const QString &root : roots) {
        const QString d = pypath::join(root, u"interface"_s);
        if (QFileInfo(d).isDir())
            for (const QString &fn : QDir(d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
                if (py::lower(fn).endsWith(u".gui"))
                    names[py::lower(fn)] = fn;
    }
    QStringList out;
    for (const auto &[k, fn] : names)
        out << u"interface/"_s + fn;
    return out;
}

std::vector<Button> all_buttons(const moddata::Mod &mod, scriptstore::ScriptStore &store)
{
    std::vector<Button> out;
    for (const QString &rel : gui_files(mod)) {
        try {
            const std::vector<Button> got = scan(rel, store.text(rel));
            out.insert(out.end(), got.begin(), got.end());
        } catch (const std::exception &) {
        }
    }
    return out;
}

std::map<Ident, std::vector<size_t>> clashes(const std::vector<Button> &buttons)
{
    // {ident: [the other buttons that sit directly in the same window and have the same key]} (places in `buttons`)
    std::map<std::tuple<QString, QStringList, QString>, std::vector<size_t>> by;
    for (size_t i = 0; i < buttons.size(); ++i)
        if (!buttons[i].key.isEmpty())
            by[{buttons[i].rel, buttons[i].path, buttons[i].key}].push_back(i);
    std::map<Ident, std::vector<size_t>> out;
    for (const auto &[k, group] : by)
        if (group.size() > 1)
            for (size_t b : group) {
                std::vector<size_t> others;
                for (size_t x : group)
                    if (x != b)
                        others.push_back(x);
                out[buttons[b].ident()] = others;
            }
    return out;
}

const QStringList KINDS = {u"guibuttontype"_s, u"checkboxtype"_s};
const QStringList WINDOW_KINDS = {u"windowtype"_s, u"eu3dialogtype"_s};
const QStringList ELEMENTS = {u"windowtype"_s,         u"eu3dialogtype"_s, u"icontype"_s,
                              u"guibuttontype"_s,      u"checkboxtype"_s,  u"shieldtype"_s,
                              u"instanttextboxtype"_s, u"textboxtype"_s,   u"editboxtype"_s,
                              u"listboxtype"_s,        u"overlappingelementsboxtype"_s, u"scrollbartype"_s};
const std::pair<int, int> SCREEN = {1920, 1080};

std::pair<double, double> anchor(const QString &o)
{
    static const std::map<QString, std::pair<double, double>> A = {
        {u"upper_left"_s, {0, 0}},    {u"upper_right"_s, {1, 0}},  {u"lower_left"_s, {0, 1}},
        {u"lower_right"_s, {1, 1}},   {u"center"_s, {.5, .5}},     {u"center_up"_s, {.5, 0}},
        {u"center_down"_s, {.5, 1}},  {u"center_left"_s, {0, .5}}, {u"center_right"_s, {1, .5}}};
    const auto it = A.find(o);
    return it != A.end() ? it->second : std::pair<double, double>{0, 0};
}

OptStr _val(const script::Node &node, const QString &name) { return locwin::_val(node, name); }

namespace {

QString lkey(const script::Node &n) { return n.has_key ? py::lower(n.key) : QString(); }

QStringList plus(const QStringList &path, const QString &name)   // path + ((name,) if name else ())
{
    QStringList out = path;
    if (!name.isEmpty())
        out << name;
    return out;
}

}  // namespace

std::vector<QStringList> turn_groups(const script::Node &win)
{
    // the parts of a window that lie on top of each other and so take turns in the game (the pages of its tabs):
    // [[names of the direct child windows of one place], …] — each group shows one of them at a time
    struct Kid {
        QString name;
        double x, y, w, h;
    };
    std::vector<Kid> kids;
    for (const script::Node &c : win.kids)
        if (c.is_block && c.has_key && !c.key.isEmpty() && WINDOW_KINDS.contains(py::lower(c.key))) {
            const OptStr nm = _val(c, u"name"_s);
            if (!nm || nm->isEmpty())
                continue;
            const XY pos = _xy(c, u"position"_s), sz = _xy(c, u"size"_s);
            if (sz.first > 0 && sz.second > 0)
                kids.push_back({*nm, pos.first, pos.second, sz.first, sz.second});
        }
    std::vector<QStringList> groups;
    auto group_of = [&groups](const QString &nm) -> std::optional<size_t> {
        for (size_t i = 0; i < groups.size(); ++i)
            if (groups[i].contains(nm))
                return i;
        return std::nullopt;
    };
    for (size_t i = 0; i < kids.size(); ++i) {
        const Kid &a = kids[i];
        for (size_t j = i + 1; j < kids.size(); ++j) {
            const Kid &b = kids[j];
            const double iw = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
            const double ih = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
            if (iw <= 0 || ih <= 0 || iw * ih < 0.45 * a.w * a.h || iw * ih < 0.45 * b.w * b.h)
                continue;                          // (not one lying on the other: both show)
            const auto ga = group_of(a.name), gb = group_of(b.name);
            if (!ga && !gb) {
                groups.push_back({a.name, b.name});
            } else if (ga && !gb) {
                groups[*ga].append(b.name);
            } else if (gb && !ga) {
                groups[*gb].append(a.name);
            } else if (*ga != *gb) {
                groups[*ga].append(groups[*gb]);
                groups.erase(groups.begin() + std::ptrdiff_t(*gb));
            }
        }
    }
    QStringList order;
    for (const Kid &k : kids)
        order << k.name;
    for (QStringList &g : groups)
        std::stable_sort(g.begin(), g.end(),
                         [&order](const QString &x, const QString &y) { return order.indexOf(x) < order.indexOf(y); });
    return groups;
}

std::vector<std::vector<Ident>> versions_of(const std::vector<Hit> &hits, const std::set<Ident> &skip)
{
    // buttons of one part of a window that lie on each other can't be there together: the game shows one set or the
    // other. -> [[idents of version 1], [of version 2], …] — only the buttons in such a clash; [] when none clashes
    std::vector<const Hit *> live;
    for (const Hit &h : hits)
        if (!skip.contains(h.ident))
            live.push_back(&h);
    std::map<Ident, std::set<Ident>> clash;
    for (size_t i = 0; i < live.size(); ++i) {
        const Hit &a = *live[i];
        for (size_t j = i + 1; j < live.size(); ++j) {
            const Hit &b = *live[j];
            if (std::get<1>(a.ident) != std::get<1>(b.ident))
                continue;                          // (other parts of the window)
            const double iw = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
            const double ih = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
            if (iw > 0 && ih > 0 && iw * ih >= 0.25 * a.w * a.h && iw * ih >= 0.25 * b.w * b.h) {
                clash[a.ident].insert(b.ident);
                clash[b.ident].insert(a.ident);
            }
        }
    }
    std::vector<std::vector<Ident>> out;
    for (const Hit *h : live) {                    // in the file's order: the first version that has room
        const auto it = clash.find(h->ident);
        if (it == clash.end())
            continue;
        bool placed = false;
        for (std::vector<Ident> &v : out) {
            bool meets = false;
            for (const Ident &x : v)
                if (it->second.contains(x)) {
                    meets = true;
                    break;
                }
            if (!meets) {
                v.push_back(h->ident);
                placed = true;
                break;
            }
        }
        if (!placed)
            out.push_back({h->ident});
    }
    return out.size() > 1 ? out : std::vector<std::vector<Ident>>{};
}

Projected project(locwin::GameLook &look, const QString &rel, const QString &window, const std::set<QString> &chosen,
                  bool with_meta, bool flat, long long version, const QString &only_part,
                  const std::set<QString> &skip_parts)
{
    // a window of an interface file laid out as the game does it. -> (picture, [(x, y, w, h, button ident)], the
    // groups of parts that take turns) or (None, [], []). Pictures and fixed texts are drawn; the frames of the buttons
    // are for the caller. Of the parts that lie on top of each other only one per group is drawn: the one named in
    // `chosen`, else the first.
    Projected none;
    script::Nodes tops;
    try {
        tops = script::parse(look.store->text(rel));
    } catch (...) {
        return none;
    }
    std::vector<const script::Node *> found;
    std::function<void(const script::Nodes &)> tops_of = [&](const script::Nodes &nodes) {
        for (const script::Node &n : nodes)
            if (n.is_block && n.has_key && !n.key.isEmpty()) {
                if (py::lower(n.key) == u"guitypes")
                    tops_of(n.kids);
                else if (WINDOW_KINDS.contains(py::lower(n.key)))
                    found.push_back(&n);
            }
    };
    tops_of(tops);
    const script::Node *win = nullptr;
    for (const script::Node *n : found)
        if (_val(*n, u"name"_s).value_or(QString()) == window) {
            win = n;
            break;
        }
    if (!win)
        return none;
    struct Op {
        double x, y, w, h;
        std::function<void(QPainter &)> draw;
    };
    struct TextAt {
        size_t i;
        QString key;
        double x, y, w, h;
    };
    std::vector<Op> ops;
    std::vector<Hit> hits;
    std::map<std::pair<QStringList, QString>, long long> seen;
    std::vector<TextAt> btexts, ttexts;            // the texts of buttons / of text boxes: (place in ops, key, box)
    std::map<Ident, std::vector<size_t>> bops;     // a button's own drawings (places in ops)
    std::set<Ident> bgs;                           // the background buttons
    const std::vector<QStringList> groups = turn_groups(*win);
    std::set<QString> hidden;
    for (const QStringList &gr : groups) {
        QString keep = gr[0];
        for (const QString &nm : gr)
            if (chosen.contains(nm)) {
                keep = nm;
                break;
            }
        for (const QString &nm : gr)
            if (nm != keep)
                hidden.insert(nm);
    }
    if (!only_part.isEmpty())                      // (C++ only) every other sub-window of the window left out
        for (const script::Node &c : win->kids)
            if (c.is_block && c.has_key && !c.key.isEmpty() && WINDOW_KINDS.contains(py::lower(c.key))) {
                const QString nm = _val(c, u"name"_s).value_or(QString());
                if (nm != only_part)
                    hidden.insert(nm);
            }
    for (const QString &nm : skip_parts)           // (C++ only) parts left out
        hidden.insert(nm);
    const OrderedMap<QString, QString> &loc = look.mod.loc;

    auto size_of = [&](const script::Node &n) -> XY {
        const QString k = lkey(n);
        if (WINDOW_KINDS.contains(k))
            return _xy(n, u"size"_s);
        OptStr spr = _val(n, u"spritetype"_s);
        if (!spr || spr->isEmpty())
            spr = _val(n, u"quadtexturesprite"_s);
        const std::optional<XY> own = look.stretches(spr);
        if (own && own->first > 0 && own->second > 0)
            return *own;                           // (a stretched sprite: the size its definition gives it)
        const std::optional<QImage> img = look.sprite(spr);
        if (img)
            return {double(img->width()), double(img->height())};
        if (k == u"instanttextboxtype" || k == u"textboxtype" || k == u"editboxtype")
            return {_num(n, u"maxwidth"_s, 100), _num(n, u"maxheight"_s, 18)};
        return _kid(n, u"size"_s) ? _xy(n, u"size"_s) : XY{24, 24};
    };

    std::function<void(const script::Node &, const QStringList &)> count = [&](const script::Node &m,
                                                                               const QStringList &pth) {
        for (const script::Node &c : m.kids)
            if (c.is_block && c.has_key && !c.key.isEmpty()) {
                const QString ck = py::lower(c.key), cn = _val(c, u"name"_s).value_or(QString());
                if (KINDS.contains(ck))
                    seen[{pth, cn}] += 1;
                else if (WINDOW_KINDS.contains(ck))
                    count(c, plus(pth, cn));
            }
    };
    std::function<void(const script::Node &, double, double, double, double, const QStringList &, int, const QString &)>
        walk = [&](const script::Node &n, double ox, double oy, double pw, double ph, const QStringList &path, int depth,
                   const QString &bg) {
            const QString k = lkey(n);
            const QString name = _val(n, u"name"_s).value_or(QString());
            OptStr orient = _val(n, u"orientation"_s);
            const auto [ax, ay] = anchor(py::lower(py::strip(orient && !orient->isEmpty() ? *orient : u"upper_left"_s)));
            auto [x, y] = depth == 0 ? XY{0.0, 0.0} : _xy(n, u"position"_s);
            x = ox + pw * ax + x;
            y = oy + ph * ay + y;
            if (std::abs(x) > 6000 || std::abs(y) > 6000)
                return;                            // (parked far outside by the game's own files)
            auto [w, h] = size_of(n);
            if (WINDOW_KINDS.contains(k) && depth == 0 &&
                py::lower(py::strip(_val(n, u"fullscreen"_s).value_or(QString()))) == u"yes") {
                w = SCREEN.first;                  // a full-screen layer: its parts hang on the screen's corners
                h = SCREEN.second;
            }
            if (WINDOW_KINDS.contains(k)) {
                const QStringList here = plus(path, name);
                if (depth == 1 && hidden.contains(name)) {   // another part has this place now: only counted
                    count(n, here);
                    return;
                }
                if (flat && depth > 0)
                    return;                        // (a backdrop: the bar itself, not the windows it can open)
                const QString mine = py::lower(_val(n, u"background"_s).value_or(QString()));
                for (const script::Node &c : n.kids)
                    if (c.is_block && c.has_key && !c.key.isEmpty() && ELEMENTS.contains(py::lower(c.key)))
                        walk(c, x, y, w, h, here, depth + 1, mine);
                return;
            }
            if (depth == 1 && skip_parts.contains(name))
                return;                            // (C++ only) a part left out (the 4K screen: what one army hides)
            OptStr spr = _val(n, u"spritetype"_s);
            if (!spr || spr->isEmpty())
                spr = _val(n, u"quadtexturesprite"_s);
            const size_t first_op = ops.size();
            std::optional<QImage> img;
            if (!bg.isEmpty() && py::lower(name) == bg && pw > 0 && ph > 0) {
                // the window's background: the game lays it over the whole window — a stretched sprite is made the
                // window's size (corners kept), a plain picture is scaled to it
                x = ox;
                y = oy;
                w = pw;
                h = ph;
                img = look.stretches(spr) ? look.sprite_fit(spr, pw, ph) : look.sprite(spr);
                if (img && (img->width() != int(pw) || img->height() != int(ph)))
                    img = img->scaled(int(pw), int(ph));
            } else if (look.stretches(spr)) {
                img = look.sprite_fit(spr, w, h);
            } else if (std::optional<QImage> b; flat && (b = look.bar(spr, 0.75))) {
                // (C++ only, 2026-11-18: the screen previews) a progress bar at its definition's size, three quarters
                // full; "rotation = 1.5708" stands it up: its full end at the bottom, its foot on the position, to the
                // left of it (as measured for the army counter's bar)
                img = *b;
                if (std::abs(_num(n, u"rotation"_s) - 1.5708) < 0.01) {
                    img = b->transformed(QTransform().rotate(-90));
                    x -= img->width();
                    y -= img->height();
                }
            } else {
                img = look.sprite(spr);
            }
            if (img) {
                const QImage im = *img;
                const double a = x, b = y;
                ops.push_back({x, y, double(im.width()), double(im.height()),
                               [im, a, b](QPainter &p) { p.drawImage(QPointF(a, b), im); }});
            }
            OptStr text = KINDS.contains(k)             ? _val(n, u"buttontext"_s)
                          : k.endsWith(u"textboxtype") ? _val(n, u"text"_s)
                                                       : std::nullopt;
            if (text && !text->isEmpty() && py::lower(name).endsWith(u"shadow"))
                text.reset();                      // (the game's dark copy under a text: drawn once here)
            const QString *shown_p = text && !text->isEmpty() ? loc.get(*text) : nullptr;
            if (shown_p && !shown_p->isEmpty()) {
                OptStr font = _val(n, u"buttonfont"_s);
                if (!font || font->isEmpty())
                    font = _val(n, u"font"_s);
                const QString shown = *shown_p;
                const bool button = KINDS.contains(k);
                const double maxw = button ? 0.0 : _num(n, u"maxwidth"_s);
                const std::optional<gamefont::Layout> got = gamefont::layout(look.fonts, font, shown, maxw);
                if (got) {
                    const double tw = double(got->widest), th = double(got->font->line * qsizetype(got->lines.size()));
                    const double tx = button ? x + (w - tw) / 2.0 : x, ty = button ? y + (h - th) / 2.0 : y;
                    if (button)
                        btexts.push_back({ops.size(), *text, x, y, w, h});
                    else
                        ttexts.push_back({ops.size(), *text, tx, ty, std::max(tw, 1.0), std::max(th, 1.0)});
                    const QString fmt = maxw != 0 ? _val(n, u"format"_s).value_or(QString()) : u"left"_s;
                    gamefont::Fonts *fonts = &look.fonts;
                    ops.push_back({tx, ty, tw, th, [fonts, font, shown, tx, ty, maxw, fmt](QPainter &p) {
                                       gamefont::draw(p, *fonts, font, shown, tx, ty, maxw, 0, fmt);
                                   }});
                }
            }
            if (KINDS.contains(k)) {
                long long &s = seen[{path, name}];
                const long long nth = s;
                s = nth + 1;
                const Ident ident{rel, path, name, nth};
                hits.push_back({x, y, std::max(8.0, w), std::max(8.0, h), ident});
                std::vector<size_t> mineops;
                for (size_t i = first_op; i < ops.size(); ++i)
                    mineops.push_back(i);
                bops[ident] = mineops;
                if (!bg.isEmpty() && py::lower(name) == bg)
                    bgs.insert(ident);
            }
        };
    walk(*win, 0.0, 0.0, 0.0, 0.0, {}, 0, QString());
    // a button whose own text is written once more by a text box lying on it (the tabs of the production screen): the
    // game shows it once — the text box's
    std::set<size_t> twice;
    const std::vector<std::vector<Ident>> versions = versions_of(hits, bgs);
    if (!versions.empty()) {                       // only one version's buttons are drawn
        const long long show = 0 <= version && version < static_cast<long long>(versions.size()) ? version : 0;
        std::set<Ident> gone;
        for (size_t kk = 0; kk < versions.size(); ++kk)
            if (static_cast<long long>(kk) != show)
                gone.insert(versions[kk].begin(), versions[kk].end());
        for (const Ident &ident : gone)
            if (const auto it = bops.find(ident); it != bops.end())
                twice.insert(it->second.begin(), it->second.end());
        std::vector<Hit> kept;
        for (const Hit &hh : hits)
            if (!gone.contains(hh.ident))
                kept.push_back(hh);
        hits = kept;
    }
    for (const TextAt &b : btexts)
        for (const TextAt &t : ttexts)
            if (t.key == b.key && t.x < b.x + b.w && t.x + t.w > b.x && t.y < b.y + b.h && t.y + t.h > b.y)
                twice.insert(b.i);
    // two different texts that start at the same spot take turns in the game (support / back down, the army's label /
    // the navy's): the first is drawn
    std::vector<TextAt> all = btexts;
    all.insert(all.end(), ttexts.begin(), ttexts.end());
    std::stable_sort(all.begin(), all.end(), [](const TextAt &a, const TextAt &b) { return a.i < b.i; });
    std::vector<std::pair<double, double>> keptxy;
    for (const TextAt &t : all) {
        if (twice.contains(t.i))
            continue;
        const Op &o = ops[t.i];
        bool near_one = false;
        for (const auto &[qx, qy] : keptxy)
            if (std::abs(o.x - qx) <= 6 && std::abs(o.y - qy) <= 6) {
                near_one = true;
                break;
            }
        if (near_one)
            twice.insert(t.i);
        else
            keptxy.emplace_back(o.x, o.y);
    }
    std::vector<Op> left;
    for (size_t i = 0; i < ops.size(); ++i)
        if (!twice.contains(i))
            left.push_back(ops[i]);
    ops = left;
    if (ops.empty() && hits.empty()) {
        Projected r;
        r.groups = groups;
        return r;
    }
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool first = true;
    auto take = [&](double bx, double by, double bw, double bh) {
        if (first) {
            x0 = bx;
            y0 = by;
            x1 = bx + bw;
            y1 = by + bh;
            first = false;
        } else {
            x0 = std::min(x0, bx);
            y0 = std::min(y0, by);
            x1 = std::max(x1, bx + bw);
            y1 = std::max(y1, by + bh);
        }
    };
    for (const Op &o : ops)
        take(o.x, o.y, o.w, o.h);
    for (const Hit &hh : hits)
        take(hh.x, hh.y, hh.w, hh.h);
    x0 = std::max(x0, x1 - 4096);
    y0 = std::max(y0, y1 - 4096);
    QImage img(int(x1 - x0) + 2, int(y1 - y0) + 2, QImage::Format_RGBA8888);
    img.fill(with_meta ? QColor(0, 0, 0, 0) : QColor(u"#2a2f37"_s));
    {
        QPainter p(&img);
        p.translate(-x0, -y0);
        for (const Op &o : ops)
            o.draw(p);
        p.end();
    }
    Projected r;
    r.img = img;
    for (const Hit &hh : hits)
        r.hits.push_back({hh.x - x0, hh.y - y0, hh.w, hh.h, hh.ident});
    r.groups = groups;
    if (with_meta) {
        Meta m;
        m.x0 = x0;
        m.y0 = y0;
        m.position = _xy(*win, u"position"_s);
        m.size = _xy(*win, u"size"_s);
        m.versions = versions;
        const OptStr o = _val(*win, u"orientation"_s);
        m.orientation = py::lower(py::strip(o && !o->isEmpty() ? *o : u"upper_left"_s));
        m.fixed = _kid(*win, u"position"_s) && (m.position != XY{0.0, 0.0} || o.has_value());
        r.meta = m;
    }
    return r;
}

// ------------------------------------------------------------------------------- the window on the game's screen
namespace {
const std::vector<std::pair<QString, QString>> ALWAYS = {{u"interface/topbar.gui"_s, u"topbar"_s},
                                                          {u"interface/menubar.gui"_s, u"menubar"_s}};

long long rev_of(scriptstore::ScriptStore *store, const QString &rel)
{
    QTextDocument *const *d = store ? store->docs.get(rel) : nullptr;
    return d ? (*d)->revision() : -1;
}
}  // namespace

OnScreen project_screen(locwin::GameLook &look, Layers &layers, const QString &rel, const QString &window,
                        const std::set<QString> &chosen, std::pair<int, int> screen, long long version)
{
    // a window as it stands on the game's screen: the screen (a plain backdrop for the map, the top bar and the lower
    // right bar faintly, as they are always there) with the window where the game puts it. A window the game places
    // itself while it runs (a row of a list, a message) stands in the middle.
    const Projected pr = project(look, rel, window, chosen, true, false, version);
    const int sw = screen.first, sh = screen.second;
    OnScreen r;
    r.img = QImage(sw, sh, QImage::Format_RGBA8888);
    r.img.fill(QColor(u"#27405a"_s));               // (the map: a plain sea colour — the editor ships no pictures)
    QPainter p(&r.img);
    p.setPen(QPen(QColor(255, 255, 255, 10), 1));
    for (int gx = 0; gx < sw; gx += 120)
        p.drawLine(gx, 0, gx, sh);
    for (int gy = 0; gy < sh; gy += 120)
        p.drawLine(0, gy, sw, gy);
    auto place = [&](const Meta &m) -> std::pair<double, double> {   // its corner of the screen + its position
        const auto [ax, ay] = anchor(m.orientation);
        return {sw * ax + m.position.first + m.x0, sh * ay + m.position.second + m.y0};
    };
    for (const auto &[arel, awin] : ALWAYS) {
        if (arel == rel && awin == window)
            continue;
        const std::tuple<QString, QString, long long> key{arel, awin, rev_of(look.store, arel)};
        auto it = layers.find(key);
        if (it == layers.end()) {
            Projected got;
            try {
                got = project(look, arel, awin, {}, true, true);
            } catch (...) {
                got = Projected();
            }
            it = layers.emplace(key, got).first;
        }
        const Projected &a = it->second;
        if (a.img && a.meta) {
            p.setOpacity(0.5);
            const auto [ax, ay] = place(*a.meta);
            p.drawImage(QPointF(ax, ay), *a.img);
            p.setOpacity(1.0);
        }
    }
    if (pr.img) {
        const QImage &img = *pr.img;
        const bool row = shortcutuse::ROWS.contains(window) ||
                         (!shortcutuse::WINDOWS.contains(window) &&
                          py::re_search(u"(_entry|_item|listitem|_member)(_|$)"_s, window, 0, true).hasMatch());
        double x, y;
        if (!row) {
            std::tie(x, y) = place(*pr.meta);
        } else {                                    // a row of a list: the game places it — here the middle
            x = (sw - img.width()) / 2.0;
            y = (sh - img.height()) / 2.0;
        }
        if (img.width() <= sw)                      // (kept on the screen; a bar wider than it starts at its place)
            x = std::min(std::max(x, 0.0), double(sw - img.width()));
        if (img.height() <= sh)
            y = std::min(std::max(y, 0.0), double(sh - img.height()));
        p.drawImage(QPointF(x, y), img);
        for (const Hit &h : pr.hits)
            r.hits.push_back({h.x + x, h.y + y, h.w, h.h, h.ident});
        r.box = Box{x, y, double(img.width()), double(img.height())};
    } else {
        r.hits = pr.hits;
    }
    p.end();
    r.groups = pr.groups;
    if (pr.meta)
        r.versions = pr.meta->versions;
    return r;
}

// ------------------------------------------------------------------------------------------------ WindowView
WindowView::WindowView(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);
    setMinimumSize(320, 240);
    setMouseTracking(true);
}

void WindowView::show_window(const std::optional<QImage> &img_, const std::vector<Hit> &hits_,
                             const std::map<Ident, QString> &keys_, const std::optional<Ident> &pick_,
                             const std::map<Ident, std::pair<QString, QString>> &info_, const QString &banner_)
{
    if (img_.has_value() != img.has_value() || (img_ && img && img_->size() != img->size())) {
        zoom = 1.0;                                 // another window: the whole of it again
        off = {0.0, 0.0};
    }
    img = img_;
    hits = hits_;
    keys = keys_;
    pick = pick_;
    info = info_;
    banner = banner_;
    update();
}

void WindowView::set_classes(const std::set<QString> &c)
{
    classes = c;
    update();
}

void WindowView::fit()
{
    zoom = 1.0;
    off = {0.0, 0.0};
    update();
}

void WindowView::zoom_to(const std::optional<Box> &b)
{
    // the view on a box of the picture (x, y, w, h): as large as fits
    if (!img || !b)
        return;
    zoom = 1.0;
    off = {0.0, 0.0};
    const auto [s0, ox, oy] = _fit();
    Q_UNUSED(ox);
    Q_UNUSED(oy);
    const double top = banner.isEmpty() ? 0 : 44;
    const double z = std::min({(width() - 60) / std::max(1.0, b->w * s0),
                               (height() - 70 - top) / std::max(1.0, b->h * s0), 8.0});
    zoom = std::max(1.0, z);
    const auto [s1, ox1, oy1] = _fit();
    const double cx = b->x + b->w / 2.0, cy = b->y + b->h / 2.0;
    off = {width() / 2.0 - (ox1 + cx * s1), top / 2.0 + height() / 2.0 - (oy1 + cy * s1)};
    update();
}

bool WindowView::shown(const Ident &ident) const
{
    if (pick && ident == *pick)
        return true;
    const auto it = info.find(ident);
    return classes.contains(it != info.end() ? it->second.first : u"good"_s);
}

std::tuple<double, double, double> WindowView::_fit() const
{
    if (!img)
        return {1.0, 0.0, 0.0};
    const double top = banner.isEmpty() ? 0 : 44;
    double s = std::min({(width() - 16) / double(img->width()), (height() - 16 - top) / double(img->height()), 2.0});
    s *= zoom;
    return {s, (width() - img->width() * s) / 2.0 + off.first,
            top + (height() - top - img->height() * s) / 2.0 + off.second};
}

std::optional<Ident> WindowView::at(const QPointF &pos) const
{
    const auto [s, ox, oy] = _fit();
    const double x = (pos.x() - ox) / s, y = (pos.y() - oy) / s;
    const Hit *best = nullptr;
    for (const Hit &h : hits)
        if (h.x <= x && x <= h.x + h.w && h.y <= y && y <= h.y + h.h && shown(h.ident))
            if (!best || h.w * h.h < best->w * best->h)
                best = &h;
    return best ? std::optional<Ident>(best->ident) : std::nullopt;
}

void WindowView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(u"#20242b"_s));
    if (!img) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect(), Qt::AlignCenter | Qt::TextWordWrap,
                   u"Pick a window or a button in the list:\nthe window is drawn here, its buttons framed."_s);
        return;
    }
    const auto [s, ox, oy] = _fit();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(ox, oy, img->width() * s, img->height() * s), *img);
    p.setPen(QPen(QColor(10, 10, 10), 3));          // the edge of the game's screen
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(ox, oy, img->width() * s, img->height() * s));
    QFont f(font());
    f.setBold(true);
    f.setPixelSize(13);
    p.setFont(f);
    if (!banner.isEmpty()) {
        p.setPen(QColor(theme::WARN));
        drawtext::draw(p, QRectF(8, 2, width() - 16, 40), Qt::AlignCenter | Qt::TextWordWrap, banner);
    }
    std::vector<QRectF> placed;
    QFont small(font());
    small.setPixelSize(11);
    small.setBold(true);
    auto cat_of = [this](const Ident &id) {
        const auto it = info.find(id);
        return it != info.end() ? it->second.first : u"good"_s;
    };
    std::vector<const Hit *> live;
    for (const Hit &hh : hits)
        if (shown(hh.ident))
            live.push_back(&hh);
    std::vector<const Hit *> order = live;
    std::stable_sort(order.begin(), order.end(), [&cat_of](const Hit *a, const Hit *b) {
        auto rank = [&cat_of](const Hit *h) {
            const QString c = cat_of(h->ident);
            return c == u"none" ? 0 : c == u"niche" ? 1 : 2;
        };
        return rank(a) < rank(b);
    });
    auto key_of = [this](const Ident &id) {
        const auto it = keys.find(id);
        return it != keys.end() ? it->second : QString();
    };
    for (const Hit *h : order) {
        const QRectF r(ox + h->x * s, oy + h->y * s, h->w * s, h->h * s);
        const QString key = key_of(h->ident);
        const QString cat = cat_of(h->ident);
        if (pick && h->ident == *pick) {
            p.setPen(QPen(QColor(10, 10, 10), 6));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            p.setPen(QPen(QColor(theme::GOLD), 3.2));
            p.drawRect(r);
        } else if (cat == u"none") {                // not worth a key: a faint dotted frame
            p.setPen(QPen(QColor(150, 150, 150, 110), 1.2, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        } else if (cat == u"niche") {
            p.setPen(QPen(QColor(10, 10, 10, 160), 2.6));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            p.setPen(QPen(!key.isEmpty() ? QColor(theme::GOLD) : QColor(150, 160, 175), 1.3, Qt::DashLine));
            p.drawRect(r);
        } else {
            p.setPen(QPen(QColor(10, 10, 10, 220), 4.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            p.setPen(QPen(!key.isEmpty() ? QColor(theme::GOLD) : QColor(120, 200, 255), 2.4));
            p.drawRect(r);
        }
    }
    // the useful buttons (and the picked one) say what they are, where there is room
    p.setFont(small);
    std::vector<const Hit *> named = live;
    std::stable_sort(named.begin(), named.end(), [this](const Hit *a, const Hit *b) {
        const bool ka = !(pick && a->ident == *pick), kb = !(pick && b->ident == *pick);
        return ka < kb;
    });
    for (const Hit *h : named) {
        const auto it = info.find(h->ident);
        const QString cat = it != info.end() ? it->second.first : u"good"_s;
        const QString does = it != info.end() ? it->second.second : QString();
        const bool is_pick = pick && h->ident == *pick;
        if (does.isEmpty() || (cat != u"good" && !is_pick))
            continue;
        const QRectF r(ox + h->x * s, oy + h->y * s, h->w * s, h->h * s);
        // a name is as large as fits its button's size in the picture (the picked one is always readable); zooming in
        // makes the names larger and brings out the ones left away
        const double px = is_pick ? 11.0 : std::min({11.0, 0.42 * r.height(), 13.0 * s});
        if (px < 7.0)
            continue;
        small.setPixelSize(int(py::round_int(px)));
        p.setFont(small);
        const QFontMetrics fm = p.fontMetrics();
        const QString text = fm.elidedText(does, Qt::ElideRight, int(21 * px));
        const double tw = fm.horizontalAdvance(text) + 10;
        const double lh = px + 5;
        QRectF lab(std::min(std::max(2.0, r.center().x() - tw / 2.0), width() - tw - 2.0), r.bottom() + 2, tw, lh);
        if (lab.bottom() > height() - 2)
            lab.moveTop(r.top() - lh - 2);
        bool hits_one = false;
        for (const QRectF &q : placed)
            if (lab.intersects(q)) {
                hits_one = true;
                break;
            }
        if (hits_one && !is_pick)
            continue;
        placed.push_back(lab);
        p.setPen(QPen(QColor(10, 10, 10), 1));
        p.setBrush(is_pick ? QColor(theme::GOLD) : QColor(24, 28, 34, 235));
        p.drawRoundedRect(lab, 3, 3);
        p.setPen(is_pick ? QColor(20, 16, 10) : QColor(190, 225, 255));
        drawtext::draw(p, lab, Qt::AlignCenter, text);
    }
    p.setFont(f);
    for (const Hit *h : live) {
        const QRectF r(ox + h->x * s, oy + h->y * s, h->w * s, h->h * s);
        const QString key = key_of(h->ident);
        if (!key.isEmpty()) {                       // the key: a small tag on the button's upper left corner,
            const double px = std::max(6.0, std::min(13.0, 0.5 * r.height()));   // sized by the button
            f.setPixelSize(int(py::round_int(px)));
            p.setFont(f);
            const double tw = p.fontMetrics().horizontalAdvance(key) + 0.7 * px;
            const double bh = px + 4;
            const QRectF b(r.left() - 2, r.top() - 0.55 * bh, std::max(1.4 * px, tw), bh);
            p.setPen(QPen(QColor(10, 10, 10), 1.2));
            p.setBrush(QColor(theme::GOLD));
            p.drawRoundedRect(b, 3, 3);
            p.setPen(QColor(20, 16, 10));
            drawtext::draw(p, b, Qt::AlignCenter, key);
        }
    }
}

void WindowView::wheelEvent(QWheelEvent *e)
{
    if (!img)
        return;
    const auto [s0, ox, oy] = _fit();
    const double px = (e->position().x() - ox) / s0, py_ = (e->position().y() - oy) / s0;   // the place stays
    zoom = std::min(8.0, std::max(1.0, zoom * (e->angleDelta().y() > 0 ? 1.25 : 0.8)));
    if (zoom == 1.0) {
        off = {0.0, 0.0};
    } else {
        const auto [s1, ox1, oy1] = _fit();
        off = {off.first + e->position().x() - (ox1 + px * s1), off.second + e->position().y() - (oy1 + py_ * s1)};
    }
    update();
    e->accept();
}

void WindowView::mouseMoveEvent(QMouseEvent *e)
{
    if (_drag && (e->buttons() & Qt::LeftButton)) {
        const QPointF d = e->position() - _drag->start;
        if (_drag->moved || std::abs(d.x()) + std::abs(d.y()) > 5) {
            _drag->moved = true;
            off = {_drag->off.first + d.x(), _drag->off.second + d.y()};
            setCursor(Qt::ClosedHandCursor);
            update();
        }
        return;
    }
    const std::optional<Ident> ident = at(e->position());
    setCursor(ident ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (!ident) {
        setToolTip(QString());
        return;
    }
    const auto it = info.find(*ident);
    const auto k = keys.find(*ident);
    const QString &nm = std::get<2>(*ident);
    setToolTip((it != info.end() ? it->second.second : QString()) + u"\n"_s + (nm.isEmpty() ? u"(no name)"_s : nm) +
               u" — "_s + (it != info.end() ? shortcutuse::CLASS_NAME.value(it->second.first) : QString()) +
               (k != keys.end() && !k->second.isEmpty() ? u"  —  key "_s + k->second : u"  —  no shortcut"_s));
}

void WindowView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        _drag = Drag{e->position(), off, false};
}

void WindowView::mouseReleaseEvent(QMouseEvent *e)
{
    const std::optional<Drag> drag = _drag;
    _drag.reset();
    if (drag && drag->moved) {                      // the picture was moved: no pick
        unsetCursor();
        return;
    }
    if (const std::optional<Ident> ident = at(e->position()))
        Q_EMIT picked(*ident);
}

void WindowView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (const std::optional<Ident> ident = at(e->position()))
        Q_EMIT opened(*ident);
}

// ------------------------------------------------------------------------------------------------ KeyCatcher
KeyCatcher::KeyCatcher(QWidget *parent) : QPushButton(u"Press a key…"_s, parent)
{
    setCheckable(true);
    setObjectName(u"primary"_s);
    connect(this, &QPushButton::toggled, this, &KeyCatcher::_toggled);
}

void KeyCatcher::_toggled(bool on)
{
    setText(on ? u"… press the key now (click again to leave it)"_s : u"Press a key…"_s);
    if (on) {
        setFocus();
        grabKeyboard();
    } else {
        releaseKeyboard();
    }
}

void KeyCatcher::focusOutEvent(QFocusEvent *e)
{
    if (isChecked())
        setChecked(false);
    QPushButton::focusOutEvent(e);
}

void KeyCatcher::keyPressEvent(QKeyEvent *e)
{
    if (!isChecked()) {
        QPushButton::keyPressEvent(e);
        return;
    }
    if (const OptStr name = key_name(e->key(), e->text()); name && !name->isEmpty()) {
        setChecked(false);
        Q_EMIT caught(*name);
    }
}

// ------------------------------------------------------------------------------------------------ the page
ShortcutsWorkspace::ShortcutsWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);
    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 12, 8, 12);
    ll->setSpacing(8);
    ll->addWidget(label(u"Shortcuts"_s, u"title"_s));
    count_lbl = label(QString(), u"sub"_s);
    ll->addWidget(count_lbl);
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    search = new QLineEdit();
    search->setPlaceholderText(u"Search: a button's name, what it says, a window, a key …"_s);
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &) { _fill(); });
    row->addWidget(search, 1);
    sort_group = new QButtonGroup(this);
    const std::vector<std::tuple<QString, QString, QString>> sorts = {
        {u"window"_s, u"By window"_s, u"Every window of the game with its buttons, A to Z"_s},
        {u"key"_s, u"By key"_s, u"Every key that is in use, with the buttons it presses"_s},
        {u"says"_s, u"By what they do"_s, u"The buttons A to Z by what they do"_s}};
    for (const auto &[key, text, tip] : sorts) {
        auto *b = new QPushButton(text);
        b->setCheckable(true);
        b->setToolTip(tip);
        b->setChecked(key == mode);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { _set_mode(k); });
        sort_group->addButton(b);
        row->addWidget(b);
    }
    only_btn = new QPushButton(u"Only with a shortcut"_s);
    only_btn->setCheckable(true);
    connect(only_btn, &QPushButton::toggled, this, [this](bool on) { _set_only(on); });
    row->addWidget(only_btn);
    ll->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(0);
    cat_group = new QButtonGroup(this);
    const std::vector<std::tuple<QString, QString, QString>> cats = {
        {u"good"_s, u"Worth a shortcut"_s,
         u"Buttons that stand once in their window and do something — the ones a key is really of use for"_s},
        {u"niche"_s, u"Niche"_s,
         u"They work, but for something narrow: one step of a slider, sorting or filtering a list, the tutorial / start "
         u"screens / multiplayer"_s},
        {u"none"_s, u"No use"_s,
         u"A key here is pointless: buttons made once per row of a list (the key always takes the last row), slider "
         u"bodies and backgrounds (not buttons), windows the game never shows"_s},
        {u"all"_s, u"All"_s, u"Every button and tick box of the interface files"_s}};
    for (const auto &[key, text, tip] : cats) {
        auto *b = new QPushButton(text);
        b->setObjectName(u"seg"_s);
        b->setCheckable(true);
        b->setChecked(key == cat);
        b->setToolTip(tip);
        const QString k = key;
        connect(b, &QPushButton::clicked, this, [this, k] { _set_cat(k); });
        cat_group->addButton(b);
        row->addWidget(b, 1);
        cat_btn[key] = b;
    }
    ll->addLayout(row);
    tree = new QTreeWidget();
    tree->setColumnCount(5);
    tree->setHeaderLabels({u"Button"_s, u"What it does"_s, u"Where"_s, u"Shortcut"_s, u"In the game"_s});
    tree->headerItem()->setToolTip(4, u"What a test in the running game found when this button was given a key ("_s +
                                          shortcutfacts::TESTED + u") — empty: not tested"_s);
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setTextElideMode(Qt::ElideRight);
    QHeaderView *hd = tree->header();
    hd->setStretchLastSection(false);
    hd->setSectionResizeMode(0, QHeaderView::Stretch);
    hd->setSectionResizeMode(1, QHeaderView::Stretch);
    hd->setSectionResizeMode(2, QHeaderView::Fixed);
    hd->resizeSection(2, 96);
    hd->setSectionResizeMode(3, QHeaderView::Fixed);
    hd->resizeSection(3, 72);
    hd->setSectionResizeMode(4, QHeaderView::Fixed);
    hd->resizeSection(4, 118);
    connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur, QTreeWidgetItem *) { _picked(cur); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) {
        if (catch_->isEnabled())
            catch_->setChecked(true);
    });
    ll->addWidget(tree, 1);
    split->addWidget(left);
    auto *mid = new QWidget();
    mid->setMinimumWidth(380);
    auto *ml = new QVBoxLayout(mid);
    ml->setContentsMargins(4, 12, 4, 12);
    ml->setSpacing(6);
    proj_head = label(u"The window on the game's screen"_s, u"sub"_s, false);
    ml->addWidget(proj_head);
    part_host = new QWidget();
    part_row = new widgets::WrapRow(part_host, 6);
    ml->addWidget(part_host);
    row = new QHBoxLayout();
    row->setSpacing(6);
    grey_btn = new QPushButton(u"Show niche and no-use buttons too"_s);
    grey_btn->setCheckable(true);
    grey_btn->setToolTip(u"Also frame the niche buttons (dashed) and the ones of no use (dotted) in the picture. Off: "
                         u"only the buttons worth a shortcut — and the class picked in the list on the left"_s);
    connect(grey_btn, &QPushButton::toggled, this, [this](bool) { _frames(); });
    row->addWidget(grey_btn);
    auto *fitb = new QPushButton(u"Whole screen"_s);
    fitb->setToolTip(u"The picture back to the whole screen of the game (the wheel zooms in on the place under the "
                     u"mouse, dragging moves it)"_s);
    connect(fitb, &QPushButton::clicked, this, [this] { proj->fit(); });
    row->addWidget(fitb);
    near_btn = new QPushButton(u"Zoom to the window"_s);
    near_btn->setToolTip(u"Zoom in on the window that is shown, so its buttons are large"_s);
    connect(near_btn, &QPushButton::clicked, this, [this] { proj->zoom_to(proj->box); });
    row->addWidget(near_btn);
    row->addWidget(label(u"Game screen "_s + QString::number(SCREEN.first) + u" × "_s + QString::number(SCREEN.second),
                         u"faint"_s, false));
    row->addStretch(1);
    ml->addLayout(row);
    proj = new WindowView();
    connect(proj, &WindowView::picked, this, [this](const Ident &id) { _pick_ident(id); });
    connect(proj, &WindowView::opened, this, [this](const Ident &id) { _open_screen(id); });
    ml->addWidget(proj, 1);
    ml->addWidget(label(
        u"Thick blue frame with a name: a button worth a shortcut (gold: it has one, the key on it). Every window "
        u"stands on the game's screen (1920 × 1080) where the game puts it — the top bar and the lower right bar are "
        u"always there, faint; a row of a list stands in the middle. Dashed grey frame: a niche button. Dotted frame: "
        u"a button a shortcut is of no use for — both only with “Show niche and no-use buttons too”. Parts of a "
        u"window that lie on top of each other (its tabs' pages) are shown one at a time: pick the part above. Wheel: "
        u"zoom, drag: move. Click a button to pick it; double-click a tab of the top bar to go to its screen."_s,
        u"faint"_s));
    split->addWidget(mid);

    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *right = new QWidget();
    right->setObjectName(u"sideInner"_s);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    sc->setWidget(right);
    split->addWidget(sc);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 8);
    split->setStretchFactor(2, 3);
    left->setMinimumWidth(520);
    sc->setMinimumWidth(360);
    split->setSizes({610, 900, 370});
    auto *s = new Section();
    s->lay->addWidget(_bold(u"The button"_s));
    what = new QLabel();
    what->setObjectName(u"sub"_s);
    what->setWordWrap(true);
    what->setTextFormat(Qt::RichText);
    s->lay->addWidget(what);
    fact = new QLabel();
    fact->setWordWrap(true);
    fact->setTextFormat(Qt::RichText);
    fact->setObjectName(u"sub"_s);
    s->lay->addWidget(fact);
    rl->addWidget(s);
    s = new Section();
    s->lay->addWidget(_bold(u"Its shortcut"_s));
    now = label(QString(), u"sub"_s);
    s->lay->addWidget(now);
    catch_ = new KeyCatcher();
    connect(catch_, &KeyCatcher::caught, this, [this](const QString &k) { _set(k); });
    s->lay->addWidget(catch_);
    row = new QHBoxLayout();
    row->setSpacing(6);
    typed = new QLineEdit();
    typed->setPlaceholderText(u"… or type it: a, 1, +, ESCAPE, F5"_s);
    connect(typed, &QLineEdit::returnPressed, this, [this] { _set(typed->text()); });
    row->addWidget(typed, 1);
    set_btn = new QPushButton(u"Set"_s);
    connect(set_btn, &QPushButton::clicked, this, [this] { _set(typed->text()); });
    row->addWidget(set_btn);
    s->lay->addLayout(row);
    none_btn = new QPushButton(u"No shortcut"_s);
    none_btn->setToolTip(u"Take the shortcut away from this button"_s);
    connect(none_btn, &QPushButton::clicked, this, [this] { _set(QString()); });
    s->lay->addWidget(none_btn);
    note = new QLabel();
    note->setWordWrap(true);
    note->setTextFormat(Qt::RichText);
    note->setStyleSheet(u"QLabel { color: "_s + theme::WARN + u"; font-weight: 600; }"_s);
    note->hide();
    s->lay->addWidget(note);
    rl->addWidget(s);
    s = new Section();
    s->lay->addWidget(_bold(u"What the test in the game found"_s));
    s->lay->addWidget(label(
        u"Every button of the main screens was given a key and the key pressed in a running campaign ("_s +
            shortcutfacts::TESTED +
            u"; the column “In the game” has each result, the document “Shortcut test in the game” all of them).\n\n"
            u"• A shortcut only works while its button is on screen: a button on another tab of the window, or in a "
            u"window that is closed, does nothing.\n"
            u"• A button that is greyed out (not allowed right now) does not take its key.\n"
            u"• A button that is made once per row of a list — a decision, a reform option, a nation to release, a "
            u"factory — has ONE shortcut for all its rows: the key presses the button of the LAST row showing, not "
            u"the first and not a picked one. So a shortcut on such a button is of little use.\n"
            u"• The parts of a slider (“slider”, “track”) are not buttons: a key on them does nothing. Its arrows "
            u"(“up”, “down”) do take a key.\n"
            u"• The number keys 0–9 have a meaning of their own in the game (unit groups): a number no button on "
            u"screen takes closes the open window. Letters do nothing when no button takes them.\n"
            u"• Home (to the capital) and Escape (the menu) are the game's own and need no button."_s,
        u"sub"_s));
    rl->addWidget(s);
    s = new Section();
    s->lay->addWidget(_bold(u"How it works"_s));
    s->lay->addWidget(label(
        u"A shortcut belongs to a button of one window and works while that window is on screen, so the same key can "
        u"press different buttons in different windows. Two buttons of one window with the same key are marked in "
        u"amber: when both are on screen, only one of them gets the key. (The game's own files have such pairs where "
        u"the buttons take turns, like the two OK buttons of a message.)\n\n"
        u"A change is a pending edit of that window's file (interface/….gui); Apply writes it into the mod. Where the "
        u"mod has no file of its own yet, it gets a copy of the game's with the change in it.\n\n"
        u"To change only the shortcuts of somebody's mod: make a submod of it (start window → New submod of the "
        u"selected mod), open the submod and set the keys here — the submod then holds nothing but the changed files."_s,
        u"sub"_s));
    rl->addWidget(s);
    rl->addStretch(1);
    _show_pick();
}

ShortcutsWorkspace::~ShortcutsWorkspace() = default;

QLabel *ShortcutsWorkspace::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ the list
scriptstore::ScriptStore *ShortcutsWorkspace::_store() const
{
    return doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
}

namespace {
shortcutuse::Button use_button(const Button &b) { return {b.rel, b.name, b.path, b.nth}; }
}  // namespace

void ShortcutsWorkspace::refresh(document::Document *d)
{
    doc = d;
    scriptstore::ScriptStore *store = _store();
    if (!store)
        return;
    const QStringList rels = gui_files(mod);
    std::vector<std::pair<QString, long long>> sig;
    for (const QString &r : rels)
        sig.emplace_back(r, rev_of(store, r));
    if (_sig && *_sig == sig)
        return;
    _sig = sig;
    _buttons = all_buttons(mod, *store);
    _clash = clashes(_buttons);
    _use.clear();
    for (const Button &b : _buttons)
        _use[b.ident()] = shortcutuse::classify(use_button(b), says(b));
    std::map<QString, long long> n = {{u"good"_s, 0}, {u"niche"_s, 0}, {u"none"_s, 0}};
    for (const auto &[id, v] : _use)
        n[std::get<0>(v)] += 1;
    for (const auto &[k, text] : std::vector<std::pair<QString, QString>>{
             {u"good"_s, u"Worth a shortcut"_s}, {u"niche"_s, u"Niche"_s}, {u"none"_s, u"No use"_s}})
        cat_btn[k]->setText(text + u"  ("_s + QString::number(n[k]) + u")"_s);
    cat_btn[u"all"_s]->setText(u"All  ("_s + QString::number(qsizetype(_buttons.size())) + u")"_s);
    _fill();
}

void ShortcutsWorkspace::_set_mode(const QString &m)
{
    mode = m;
    _fill();
}

void ShortcutsWorkspace::_set_only(bool on)
{
    only = on;
    _fill();
}

void ShortcutsWorkspace::_set_cat(const QString &c)
{
    cat = c;
    cat_btn[c]->setChecked(true);
    _fill();
    _frames();
}

QString ShortcutsWorkspace::says(const Button &b) const
{
    for (const QString &k : {b.says, b.tip})
        if (!k.isEmpty()) {
            const QString *t = mod.loc.get(k);
            if (!t || t->isEmpty())
                t = mod.loc.get(py::upper(k));
            if (t && !t->isEmpty())
                return py::strip(py::re_sub(u"§."_s, *t, [](const QRegularExpressionMatch &) { return QString(); })
                                     .replace(u"\\n"_s, u" "_s));
        }
    return b.says;
}

void ShortcutsWorkspace::_fill()
{
    const QString want = py::lower(py::strip(search->text()));
    const std::optional<Ident> keep = _pick;
    std::vector<std::pair<const Button *, QString>> rows;
    for (const Button &b : _buttons) {
        if ((only || mode == u"key") && b.key.isEmpty())
            continue;
        const QString sy = says(b);
        const auto u = _use.find(b.ident());
        const QString c = u != _use.end() ? std::get<0>(u->second) : u"good"_s;
        const QString does = u != _use.end() ? std::get<2>(u->second) : QString();
        if (cat != u"all" && c != cat)
            continue;
        if (!want.isEmpty() && !py::lower(b.name + u' ' + sy + u' ' + b.rel + u' ' + b.path.join(u' ') + u' ' + b.key +
                                          u' ' + does).contains(want))
            continue;
        rows.emplace_back(&b, sy);
    }
    tree->blockSignals(true);
    tree->clear();
    _row_ident.clear();
    _head_of.clear();
    const QBrush amber{QColor(theme::WARN)}, gold{QColor(theme::GOLD)}, dim{QColor(theme::INK_FAINT)};
    QTreeWidgetItem *pick_item = nullptr;

    auto row_item = [&](QTreeWidgetItem *parent, const Button &b, const QString &sy, const QString &where) {
        const shortcutfacts::Fact *fct = fact_of(b);
        const auto u = _use.find(b.ident());
        const QString c = u != _use.end() ? std::get<0>(u->second) : u"good"_s;
        const QString why = u != _use.end() ? std::get<1>(u->second) : QString();
        const QString does = u != _use.end() ? std::get<2>(u->second) : sy;
        auto *it = new QTreeWidgetItem(parent, QStringList{b.name.isEmpty() ? u"(no name)"_s : b.name, does, where, b.key,
                                                           fct ? shortcutfacts::WORD().value(fct->cls) : QString()});
        if (c != u"good") {
            it->setForeground(0, c == u"none" ? dim : QBrush(QColor(theme::INK_DIM)));
            it->setForeground(1, c == u"none" ? dim : QBrush(QColor(theme::INK_DIM)));
        }
        if (fct) {
            it->setToolTip(4, fct->text);
            const QString &k = fct->cls;
            it->setForeground(4, (k == u"dead" || k == u"gone" || k == u"crash" || k == u"last") ? amber
                                 : k == u"silent"                                                ? dim
                                                                                                 : QBrush(QColor(theme::INK)));
        }
        it->setData(0, Qt::UserRole, int(_row_ident.size()));
        _row_ident.push_back(b.ident());
        it->setToolTip(0, b.rel + u" · "_s + (b.path + QStringList{b.name}).join(u" › "_s) +
                              (b.kind == u"checkboxtype" ? u"  (a tick box)"_s : QString()));
        it->setToolTip(1, does + (!sy.isEmpty() ? u"\nThe game's own text: “"_s + sy + u"”"_s : QString()) + u"\n\n"_s +
                              shortcutuse::CLASS_NAME.value(c) + u": "_s + why);
        if (!b.key.isEmpty()) {
            it->setForeground(3, gold);
            QFont f = it->font(3);
            f.setBold(true);
            it->setFont(3, f);
        }
        if (const auto cl = _clash.find(b.ident()); cl != _clash.end()) {
            for (int col = 0; col < 4; ++col)
                it->setForeground(col, amber);
            QStringList names;
            for (size_t x : cl->second)
                names << _buttons[x].name;
            it->setToolTip(3, u"Also on this key in the same window: "_s + names.join(u", "_s) +
                                  u" — when both are on screen, only one of them gets the key"_s);
        } else if (!b.key.isEmpty() && !known(b.key)) {
            it->setForeground(3, amber);
            it->setToolTip(3, u"A key name that none of the game's or the mods' files use — the game may not know it"_s);
        }
        return it;
    };

    if (mode == u"window") {
        std::map<std::pair<QString, QString>, std::vector<std::pair<const Button *, QString>>> groups;
        for (const auto &r : rows)
            groups[{py::lower(pypath::basename(r.first->rel)), py::lower(r.first->window())}].push_back(r);
        for (auto &[gk, items] : groups) {
            const Button &b0 = *items[0].first;
            long long n_key = 0;
            for (const auto &r : items)
                n_key += r.first->key.isEmpty() ? 0 : 1;
            const auto wt = shortcutuse::window_title(b0.path.isEmpty() ? QString() : b0.window(), b0.rel);
            auto *top = new QTreeWidgetItem(
                tree, QStringList{wt.first + u"   ("_s + QString::number(qsizetype(items.size())) +
                                  (n_key ? u", "_s + QString::number(n_key) + u" with a key"_s : QString()) +
                                  u")   —   "_s + b0.window() + u", "_s + pypath::basename(b0.rel) +
                                  (!wt.second.isEmpty() ? u"   ·   on screen: "_s + wt.second : QString())});
            top->setData(0, Qt::UserRole + 1, int(_head_of.size()));
            _head_of.emplace_back(b0.rel, b0.window());
            QFont f = top->font(0);
            f.setBold(true);
            top->setFont(0, f);
            top->setFirstColumnSpanned(true);
            std::stable_sort(items.begin(), items.end(), [](const auto &x, const auto &y) {
                const QString a = py::lower(x.first->name), b = py::lower(y.first->name);
                return a != b ? a < b : x.first->nth < y.first->nth;
            });
            bool has_keep = false;
            for (const auto &[b, sy] : items) {
                QTreeWidgetItem *it = row_item(top, *b, sy, b->path.mid(1).join(u" › "_s));
                if (keep && b->ident() == *keep) {
                    pick_item = it;
                    has_keep = true;
                }
            }
            top->setExpanded(!want.isEmpty() || only || has_keep);
        }
    } else {
        std::vector<std::pair<const Button *, QString>> sorted_rows = rows;
        if (mode == u"key") {
            std::stable_sort(sorted_rows.begin(), sorted_rows.end(), [](const auto &x, const auto &y) {
                const Button &a = *x.first, &b = *y.first;
                const bool la = a.key.size() > 1, lb = b.key.size() > 1;
                if (la != lb)
                    return la < lb;
                if (a.key != b.key)
                    return a.key < b.key;
                const QString ra = py::lower(a.rel), rb = py::lower(b.rel);
                if (ra != rb)
                    return ra < rb;
                return py::lower(a.name) < py::lower(b.name);
            });
        } else {
            auto does_key = [this](const Button &b) {
                const auto u = _use.find(b.ident());
                return py::strip(py::lower(u != _use.end() ? std::get<2>(u->second) : b.name), u"“"_s);
            };
            std::stable_sort(sorted_rows.begin(), sorted_rows.end(), [&does_key](const auto &x, const auto &y) {
                const QString a = does_key(*x.first), b = does_key(*y.first);
                if (a != b)
                    return a < b;
                return py::lower(x.first->rel) < py::lower(y.first->rel);
            });
        }
        for (const auto &[b, sy] : sorted_rows) {
            QTreeWidgetItem *it = row_item(nullptr, *b, sy, b->window() + u" — "_s + pypath::basename(b->rel));
            tree->addTopLevelItem(it);
            if (keep && b->ident() == *keep)
                pick_item = it;
        }
    }
    tree->blockSignals(false);
    const long long n_all = qsizetype(_buttons.size());
    long long n_key = 0;
    std::set<QString> files;
    for (const Button &b : _buttons) {
        n_key += b.key.isEmpty() ? 0 : 1;
        files.insert(b.rel);
    }
    const long long n_clash = qsizetype(_clash.size());
    count_lbl->setText(
        QString::number(n_all) + u" buttons and tick boxes in "_s + QString::number(qsizetype(files.size())) +
        u" interface files; "_s + QString::number(n_key) + u" have a shortcut"_s +
        (n_clash ? u" — "_s + QString::number(n_clash) +
                       u" share a key with another button of their window (amber: that matters when both are on screen "
                       u"together)"_s
                 : QString()) +
        u". The list shows the ones worth a shortcut — the rest are under “Niche” and “No use”, each with the reason. "
        u"Pick one, then press the key it should get (double-click = press a key)."_s);
    if (pick_item) {
        tree->setCurrentItem(pick_item);
        _show_window(std::get<0>(*_pick), std::get<1>(*_pick).isEmpty() ? OptStr() : OptStr(std::get<1>(*_pick)[0]));
    } else {
        _pick.reset();
        if (_shown)
            _show_window(_shown->rel, _shown->window);
    }
    _show_pick();
}

// ------------------------------------------------------------------ the picked button
const Button *ShortcutsWorkspace::_button(const std::optional<Ident> &ident) const
{
    if (!ident)
        return nullptr;
    for (const Button &b : _buttons)
        if (b.ident() == *ident)
            return &b;
    return nullptr;
}

std::optional<Ident> ShortcutsWorkspace::ident_of(QTreeWidgetItem *it) const
{
    if (!it)
        return std::nullopt;
    const QVariant v = it->data(0, Qt::UserRole);
    if (!v.isValid())
        return std::nullopt;
    const int i = v.toInt();
    return i >= 0 && i < int(_row_ident.size()) ? std::optional<Ident>(_row_ident[size_t(i)]) : std::nullopt;
}

void ShortcutsWorkspace::_picked(QTreeWidgetItem *cur)
{
    _pick = ident_of(cur);
    std::optional<std::pair<QString, QString>> head;
    if (cur) {
        const QVariant v = cur->data(0, Qt::UserRole + 1);
        if (v.isValid() && v.toInt() >= 0 && v.toInt() < int(_head_of.size()))
            head = _head_of[size_t(v.toInt())];
    }
    if (_pick)
        _show_window(std::get<0>(*_pick), std::get<1>(*_pick).isEmpty() ? OptStr() : OptStr(std::get<1>(*_pick)[0]));
    else if (head)
        _show_window(head->first, head->second);
    _show_pick();
}

void ShortcutsWorkspace::_show_window(const QString &rel, const OptStr &window_, bool force)
{
    // the window in the middle of the page (drawn anew when it is another one, or its file changed)
    scriptstore::ScriptStore *store = _store();
    if (!store || !window_ || window_->isEmpty())
        return;
    const QString window = *window_;
    if (!_look)
        _look = std::make_unique<locwin::GameLook>(mod, store);
    const long long rev = rev_of(store, rel);
    std::set<QString> &chosen = _parts[{rel, window}];
    OptStr prefer;
    const bool pick_here = _pick && std::get<0>(*_pick) == rel;
    if (pick_here && std::get<1>(*_pick).size() > 1 && std::get<1>(*_pick)[0] == window) {
        prefer = std::get<1>(*_pick)[1];          // the picked button's own part is the one shown
        chosen.insert(*prefer);
    }
    const auto vit = _version.find({rel, window});
    long long vers = vit != _version.end() ? vit->second : 0;
    if (pick_here && _shown && _shown->rel == rel && _shown->window == window)
        for (size_t k = 0; k < _shown->versions.size(); ++k) {   // the picked button's own version is the one shown
            const auto &v = _shown->versions[k];
            if (std::find(v.begin(), v.end(), *_pick) != v.end())
                vers = _version[{rel, window}] = qsizetype(k);
        }
    auto sorted_of = [](const std::set<QString> &c) {
        QStringList out;
        for (const QString &x : c)
            out << x;
        return out;
    };
    const QStringList key_chosen = sorted_of(chosen);
    if (force || !_shown || _shown->rel != rel || _shown->window != window || _shown->rev != rev ||
        _shown->chosen != key_chosen || _shown->vers != vers) {
        OnScreen got = project_screen(*_look, _layers, rel, window, chosen, SCREEN, vers);
        if (pick_here) {
            std::optional<long long> k;
            for (size_t i = 0; i < got.versions.size(); ++i)
                if (std::find(got.versions[i].begin(), got.versions[i].end(), *_pick) != got.versions[i].end()) {
                    k = qsizetype(i);
                    break;
                }
            if (k && *k != vers) {                 // (first time this window is drawn: the pick's version)
                vers = _version[{rel, window}] = *k;
                got = project_screen(*_look, _layers, rel, window, chosen, SCREEN, vers);
            }
        }
        for (const QStringList &gr : got.groups) {   // (one part per place: the picked button's, else the first)
            QStringList mine;
            for (const QString &nm : gr)
                if (chosen.contains(nm))
                    mine << nm;
            const OptStr keep_ = prefer && mine.contains(*prefer) ? prefer : (mine.isEmpty() ? OptStr() : OptStr(mine[0]));
            for (const QString &nm : mine)
                if (!keep_ || nm != *keep_)
                    chosen.erase(nm);
        }
        std::set<QString> all_names;
        for (const QStringList &gr : got.groups)
            for (const QString &nm : gr)
                all_names.insert(nm);
        for (auto it = chosen.begin(); it != chosen.end();)
            it = all_names.contains(*it) ? std::next(it) : chosen.erase(it);
        if (sorted_of(chosen) != key_chosen)
            got = project_screen(*_look, _layers, rel, window, chosen, SCREEN, vers);
        _shown = Shown{rel, window, rev, got.img, got.hits, got.groups, sorted_of(chosen), got.box, got.versions, vers};
    }
    const Shown &sh = *_shown;
    proj->box = sh.box;
    _fill_parts(sh.groups, chosen, sh.versions, sh.vers);
    const auto wt = shortcutuse::window_title(window, rel);
    proj_head->setText(wt.first + u"  —  "_s + window + u", "_s + pypath::basename(rel) +
                       (!wt.second.isEmpty() ? u"   ·   on screen: "_s + wt.second : QString()));
    QString banner;
    if (shortcutuse::ROWS.contains(window))
        banner = u"A row of a list: the game makes this once for every "_s + shortcutuse::ROWS.value(window) +
                 u" showing. A key on a button here always presses the LAST row — it cannot pick one."_s;
    else if (shortcutuse::LEFTOVER.contains(py::lower(pypath::basename(rel))))
        banner = u"Left over from another game (Hearts of Iron 3): Victoria 2 does not show this window."_s;
    std::map<Ident, QString> keys;
    for (const Button &b : _buttons)
        if (b.rel == rel)
            keys[b.ident()] = b.key;
    std::map<Ident, std::pair<QString, QString>> info;
    for (const auto &[i, v] : _use)
        if (std::get<0>(i) == rel)
            info[i] = {std::get<0>(v), std::get<2>(v)};
    proj->show_window(sh.img, sh.hits, keys, _pick, info, banner);
    _frames();
}

void ShortcutsWorkspace::_frames()
{
    // which classes of buttons are framed in the picture: the useful ones, the class picked in the list, and the grey
    // ones when their switch is on
    std::set<QString> classes = {u"good"_s};
    if (grey_btn->isChecked()) {
        classes.insert(u"niche"_s);
        classes.insert(u"none"_s);
    }
    if (cat == u"niche" || cat == u"none")
        classes.insert(cat);
    proj->set_classes(classes);
}

void ShortcutsWorkspace::_fill_parts(const std::vector<QStringList> &groups, const std::set<QString> &chosen,
                                     const std::vector<std::vector<Ident>> &versions, long long vers)
{
    // the buttons for the parts of the window that take turns (the pages of its tabs) and for its versions (sets of
    // buttons that lie on each other: a message with one OK / with Cancel and OK)
    std::vector<std::pair<int, QString>> want;
    for (size_t gi = 0; gi < groups.size(); ++gi)
        for (const QString &nm : groups[gi])
            want.emplace_back(int(gi), nm);
    std::vector<QStringList> vwant;
    for (const auto &v : versions) {
        QStringList names;
        for (const Ident &i : v)
            names << std::get<2>(i);
        vwant.push_back(names);
    }
    const QString seg_css = u"QPushButton { padding: 3px 9px; } QPushButton:checked { background: "_s + theme::GOLD +
                            u"; color: #14100a; }"_s;
    if (_pwant != want || !_vwant || *_vwant != vwant) {
        _vwant = vwant;
        while (part_row->count()) {
            QLayoutItem *item = part_row->takeAt(0);
            QWidget *w = item ? item->widget() : nullptr;
            delete item;
            if (w) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
        }
        part_btn.clear();
        _pwant.clear();
        if (!want.empty())
            part_row->addWidget(label(u"Parts that take turns — showing:"_s, u"sub"_s, false));
        for (const auto &[gi, nm] : want) {
            auto *b = new QPushButton(QString(nm).replace(u'_', u' '));
            b->setCheckable(true);
            b->setToolTip(u"Show this part of the window ("_s + nm +
                          u") — the others at its place are hidden meanwhile, as in the game"_s);
            b->setStyleSheet(seg_css);
            const int g = gi;
            const QString n = nm;
            connect(b, &QPushButton::clicked, this, [this, g, n] { _set_part(g, n); });
            part_row->addWidget(b);
            part_btn.push_back(b);
            _pwant.emplace_back(gi, nm);
        }
        ver_btn.clear();
        if (!vwant.empty()) {
            QLabel *lb = label(u"This window has "_s + QString::number(qsizetype(vwant.size())) +
                                   u" versions (buttons that lie on each other) — showing:"_s,
                               u"sub"_s, false);
            lb->setStyleSheet(u"QLabel { color: "_s + theme::WARN + u"; font-weight: 700; }"_s);
            part_row->addWidget(lb);
        }
        for (size_t k = 0; k < vwant.size(); ++k) {
            QStringList does;
            for (const Ident &ident : versions[k]) {
                const auto got = _use.find(ident);
                does << (got != _use.end() ? std::get<2>(got->second) : std::get<2>(ident));
            }
            auto *b = new QPushButton(u"Version "_s + QString::number(qsizetype(k) + 1) + u":  "_s + does.join(u"  +  "_s));
            b->setCheckable(true);
            b->setToolTip(u"Show the window with these buttons: "_s + vwant[k].join(u", "_s) +
                          u" — the game shows either this set or the other, never both"_s);
            b->setStyleSheet(seg_css);
            const long long kk = qsizetype(k);
            connect(b, &QPushButton::clicked, this, [this, kk] { _set_version(kk); });
            part_row->addWidget(b);
            ver_btn.push_back(b);
        }
    }
    for (size_t i = 0; i < part_btn.size(); ++i) {
        const auto &[gi, nm] = _pwant[i];
        bool any = false;
        for (const QString &x : groups[size_t(gi)])
            any = any || chosen.contains(x);
        const bool on = chosen.contains(nm) || (!any && groups[size_t(gi)][0] == nm);
        part_btn[i]->blockSignals(true);
        part_btn[i]->setChecked(on);
        part_btn[i]->blockSignals(false);
    }
    for (size_t k = 0; k < ver_btn.size(); ++k) {
        ver_btn[k]->blockSignals(true);
        ver_btn[k]->setChecked(qsizetype(k) == vers);
        ver_btn[k]->blockSignals(false);
    }
    part_host->setVisible(!want.empty() || !vwant.empty());
}

void ShortcutsWorkspace::_set_version(long long k)
{
    if (!_shown)
        return;
    const QString rel = _shown->rel, window = _shown->window;
    const auto versions = _shown->versions;
    _version[{rel, window}] = k;
    bool hidden = false;
    if (_pick)
        for (size_t i = 0; i < versions.size(); ++i)
            if (qsizetype(i) != k && std::find(versions[i].begin(), versions[i].end(), *_pick) != versions[i].end())
                hidden = true;
    if (hidden) {
        _pick.reset();                             // (the picked button is in the version now hidden)
        tree->blockSignals(true);
        tree->setCurrentItem(nullptr);
        tree->blockSignals(false);
        _show_pick();
    }
    _show_window(rel, window);
}

void ShortcutsWorkspace::_set_part(int gi, const QString &name)
{
    if (!_shown)
        return;
    const QString rel = _shown->rel, window = _shown->window;
    const QStringList group = _shown->groups[size_t(gi)];
    std::set<QString> &chosen = _parts[{rel, window}];
    for (const QString &nm : group)
        chosen.erase(nm);
    chosen.insert(name);
    if (_pick && std::get<0>(*_pick) == rel && std::get<1>(*_pick).size() > 1 &&
        group.contains(std::get<1>(*_pick)[1]) && std::get<1>(*_pick)[1] != name) {
        _pick.reset();                             // (the picked button is in the part now hidden)
        tree->blockSignals(true);
        tree->setCurrentItem(nullptr);
        tree->blockSignals(false);
        _show_pick();
    }
    _show_window(rel, window);
}

void ShortcutsWorkspace::_pick_ident(const Ident &ident)
{
    // a button clicked in the drawn window: picked in the list too
    _pick = ident;
    const auto got = _use.find(ident);
    if (got != _use.end() && cat != u"all" && cat != std::get<0>(got->second)) {   // (its class is shown)
        cat = std::get<0>(got->second);
        cat_btn[cat]->setChecked(true);
    }
    if (mode != u"window" || only || !py::strip(search->text()).isEmpty()) {
        search->blockSignals(true);
        search->clear();
        search->blockSignals(false);
        only = false;
        only_btn->blockSignals(true);
        only_btn->setChecked(false);
        only_btn->blockSignals(false);
        mode = u"window"_s;
        for (QAbstractButton *b : sort_group->buttons())
            b->setChecked(b->text() == u"By window");
    }
    _fill();
    if (QTreeWidgetItem *it = tree->currentItem())
        tree->scrollToItem(it);
}

void ShortcutsWorkspace::_open_screen(const Ident &ident)
{
    // a double-click on a top-bar tab: its screen is drawn (the largest window of that screen's file)
    const QString *fn = SCREENS.get(py::lower(std::get<2>(ident)));
    if (!fn)
        return;
    OptStr rel;
    for (const QString &r : gui_files(mod))
        if (py::lower(r).endsWith(u"/"_s + *fn)) {
            rel = r;
            break;
        }
    OrderedMap<QString, long long> wins;
    for (const Button &b : _buttons)
        if (rel && b.rel == *rel) {
            const long long *c = wins.get(b.window());
            wins.insert(b.window(), (c ? *c : 0) + 1);
        }
    if (wins.isEmpty())
        return;
    QString best;
    long long most = -1;
    for (const auto &[w, c] : wins)
        if (c > most) {
            most = c;
            best = w;
        }
    _pick.reset();
    tree->setCurrentItem(nullptr);
    _show_window(*rel, best);
    _show_pick();
}

void ShortcutsWorkspace::_show_pick()
{
    const Button *b = _button(_pick);
    const bool on = b != nullptr;
    for (QWidget *w : std::initializer_list<QWidget *>{catch_, typed, set_btn})
        w->setEnabled(on);
    none_btn->setEnabled(on && (!b->key.isEmpty() || b->has));
    note->hide();
    if (!on) {
        what->setText(u"Pick a button in the list."_s);
        fact->setText(QString());
        now->setText(QString());
        if (catch_->isChecked())
            catch_->setChecked(false);
        return;
    }
    const QString sy = says(*b);
    const auto u = _use.find(b->ident());
    const QString c = u != _use.end() ? std::get<0>(u->second) : u"good"_s;
    const QString why = u != _use.end() ? std::get<1>(u->second) : QString();
    const QString does = u != _use.end() ? std::get<2>(u->second) : sy;
    const auto wt = shortcutuse::window_title(b->path.isEmpty() ? QString() : b->path[0], b->rel);
    const QString col = c == u"good" ? u"#7fd18a"_s : c == u"niche" ? theme::INK_DIM : theme::WARN;
    what->setText(u"<b>"_s + does + u"</b><br><span style='color:"_s + theme::INK_DIM + u"'>"_s +
                  (b->name.isEmpty() ? u"(no name)"_s : b->name) +
                  (b->kind == u"checkboxtype" ? u"  (a tick box)"_s : QString()) + u"</span><br><br><b style='color:"_s +
                  col + u"'>"_s + shortcutuse::CLASS_NAME.value(c) + u".</b> "_s + why + u"<br><br><b>Where:</b> "_s +
                  wt.first + (!wt.second.isEmpty() ? u" — on screen: "_s + wt.second : QString()) +
                  u"<br><span style='color:"_s + theme::INK_FAINT + u"'>"_s +
                  (!sy.isEmpty() ? u"the game's own text: “"_s + sy + u"” · "_s : QString()) + b->rel + u" · "_s +
                  (b->path.isEmpty() ? u"not in a window"_s : b->path.join(u" › "_s)) + u"</span>"_s);
    now->setText(u"Now: "_s + (!b->key.isEmpty() ? b->key : u"none"_s));
    if (const shortcutfacts::Fact *fct = fact_of(*b)) {
        const bool bad = fct->cls == u"dead" || fct->cls == u"gone" || fct->cls == u"crash" || fct->cls == u"last";
        fact->setText(u"<b>In the game</b> (tested "_s + shortcutfacts::TESTED + u"): <span style='color:"_s +
                      (bad ? theme::WARN : theme::INK) + u"'>"_s + fct->text + u"</span>"_s);
    } else {
        fact->setText(u"<span style='color:"_s + theme::INK_FAINT +
                      u"'>In the game: not tested (the test reached the eight main screens, the province window and "
                      u"the main screen's bars).</span>"_s);
    }
    QStringList notes;
    const auto cl = _clash.find(b->ident());
    if (cl != _clash.end()) {
        QStringList names;
        for (size_t x : cl->second)
            names << _buttons[x].name;
        notes << u"⚠ In this window the key "_s + b->key + u" also presses: "_s + names.join(u", "_s) +
                     u". When both are on screen, only one of them gets the key (fine for buttons that take turns)."_s;
    }
    if (!b->key.isEmpty() && !known(b->key))
        notes << u"⚠ “"_s + b->key + u"” is a key name that none of the game's or the mods' files use — the game may not "
                                     u"know it."_s;
    if (!b->key.isEmpty()) {
        std::set<Ident> mine;
        if (cl != _clash.end())
            for (size_t x : cl->second)
                mine.insert(_buttons[x].ident());
        std::vector<const Button *> near_, others;
        std::set<QString> far_;
        for (const Button &x : _buttons)
            if (x.key == b->key && x.ident() != b->ident() && !mine.contains(x.ident())) {
                if (x.rel == b->rel && x.window() == b->window())
                    near_.push_back(&x);
                else
                    far_.insert(x.window());
            }
        const QString dim0 = u"<span style='color:"_s + theme::INK_DIM + u"; font-weight:400'>"_s;
        if (!near_.empty()) {
            QStringList parts;
            for (size_t i = 0; i < near_.size() && i < 4; ++i)
                parts << (near_[i]->path.mid(1) + QStringList{near_[i]->name}).join(u" › "_s);
            notes << dim0 + u"In another part of this window the key also presses: "_s + parts.join(u", "_s) + u"."_s +
                         (near_.size() > 4 ? u" …"_s : QString()) + u"</span>"_s;
        }
        if (!far_.empty()) {
            QStringList fl(far_.begin(), far_.end());
            const qsizetype nf = fl.size();
            notes << dim0 + u"The key is also used in "_s + QString::number(nf) + u" other window"_s +
                         (nf == 1 ? QString() : u"s"_s) + u" ("_s + fl.mid(0, 4).join(u", "_s) +
                         (nf > 4 ? u" …"_s : QString()) + u") — that is no clash."_s + u"</span>"_s;
        }
    }
    if (!notes.isEmpty()) {
        note->setText(notes.join(u"<br>"_s));
        note->show();
    }
}

void ShortcutsWorkspace::_set(const QString &key0)
{
    const Button *bp = _button(_pick);
    scriptstore::ScriptStore *store = _store();
    if (!bp || !store)
        return;
    const Button b = *bp;
    const QString key = tidy(key0);
    if (!key.isEmpty() && (key.size() > 16 || py::re_search(u"[\\s\\x1c-\\x1f\"{}=#]"_s, key).hasMatch())) {
        note->setText(u"⚠ That is not a key: one character, or a name like ESCAPE or F5."_s);
        note->show();
        return;
    }
    if (key == b.key && (!key.isEmpty() || !b.has))
        return;
    std::optional<QString> nw;
    try {
        nw = set_shortcut(store->text(b.rel), b.ident(), key);
    } catch (const std::exception &) {
        return;                                    // (Python: the slot stops with the exception)
    }
    if (!nw) {
        Q_EMIT message(u"⚠ The button "_s + b.name + u" is not in "_s + b.rel + u" any more — nothing changed."_s);
        return;
    }
    typed->clear();
    const QString w = !key.isEmpty() ? u"Shortcut "_s + key + u" for "_s + b.name : u"No shortcut for "_s + b.name;
    Q_EMIT edit(w + u" ("_s + pypath::basename(b.rel) + u")"_s, b.rel, *nw);
    QTimer::singleShot(0, this, [this] { refresh(doc); });
}

}  // namespace shortcuts
