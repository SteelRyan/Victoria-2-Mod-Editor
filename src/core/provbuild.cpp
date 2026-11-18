// provbuild — see provbuild.h (C++ only, 2026-11-16).
#include "core/provbuild.h"
#include "core/blocks.h"
#include "core/pytext.h"
#include "core/script.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace provbuild {

using script::Node;
using script::Nodes;

const QStringList GAME_THREE = {u"fort"_s, u"naval_base"_s, u"railroad"_s};
const QStringList BASE_FIELDS = {u"type"_s,         u"goods_cost"_s,    u"cost"_s,           u"time"_s,
                                 u"visibility"_s,   u"onmap"_s,         u"max_level"_s,      u"province"_s,
                                 u"one_per_state"_s, u"port"_s,         u"capital"_s,        u"colonial_points"_s,
                                 u"pop_build_factory"_s, u"spawn_railway_track"_s, u"default_enabled"_s,
                                 u"production_type"_s, u"advanced_factory"_s, u"strategic_factory"_s,
                                 u"complex_shipyard"_s, u"sail"_s,     u"steam"_s,          u"on_completion"_s,
                                 u"completion_size"_s};
const QStringList ENGINE_EFFECTS = {u"fort_level"_s,     u"naval_capacity"_s, u"colonial_range"_s,
                                    u"infrastructure"_s, u"movement_cost"_s,  u"local_ship_build"_s};
const int ROW = 35;

static bool is(const Node &n, const QString &k) { return n.has_key && n.key == k; }
static const Node *kid(const Node &n, const QString &k)
{
    for (const Node &c : n.kids)
        if (is(c, k))
            return &c;
    return nullptr;
}
static QString val(const Node &n, const QString &k)
{
    const Node *c = kid(n, k);
    return c && !c->is_block ? c->value : QString();
}
static const Node *top_block(const Nodes &nodes, const QString &key)
{
    for (const Node &n : nodes)
        if (is(n, key) && n.is_block)
            return &n;
    return nullptr;
}
static qsizetype line_start(const QString &t, qsizetype i) { return py::rfind(t, u'\n', i) + 1; }
static qsizetype line_end(const QString &t, qsizetype i)       // just past the line's "\n" (or the text's end)
{
    const qsizetype j = t.indexOf(u'\n', i);
    return j < 0 ? t.size() : j + 1;
}
static QString indent_at(const QString &t, qsizetype i)
{
    const qsizetype s = line_start(t, i);
    qsizetype e = s;
    while (e < t.size() && (t[e] == u' ' || t[e] == u'\t'))
        ++e;
    return t.mid(s, e - s);
}
using Edit = std::tuple<qsizetype, qsizetype, QString>;
static QString apply(QString t, std::vector<Edit> edits)      // the edits, last first (they must not overlap)
{
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return std::get<0>(a) > std::get<0>(b); });
    for (const auto &[a, b, s] : edits)
        t = t.left(a) + s + t.mid(b);
    return t;
}

// ---------------------------------------------------------------------------------------------- buildings.txt
QStringList province_keys(const QString &text)
{
    QStringList out;
    for (const Node &n : script::parse(text))
        if (n.has_key && n.is_block && py::lower(val(n, u"province"_s)) == u"yes" && !out.contains(n.key))
            out << n.key;
    return out;
}

std::vector<Line> lines_of(const QString &text, const QString &key)
{
    std::vector<Line> out;
    const Nodes nodes = script::parse(text);
    if (const Node *b = top_block(nodes, key))
        for (const Node &c : b->kids)
            if (c.has_key)
                out.push_back({c.key, c.is_block ? text.mid(c.vstart, c.vend - c.vstart) : c.value, c.is_block});
    return out;
}

std::vector<Line> effects(const QString &text, const QString &key)
{
    std::vector<Line> out;
    for (const Line &l : lines_of(text, key))
        if (!BASE_FIELDS.contains(l.key))
            out.push_back(l);
    return out;
}

QString add_building(const QString &text, const QString &key, const std::optional<QString> &like)
{
    QString cost = u"\t\tlumber = 100\n"_s, time = u"365"_s, max_level = u"6"_s;
    if (like) {
        const Nodes nodes = script::parse(text);
        if (const Node *b = top_block(nodes, *like)) {
            if (const Node *g = kid(*b, u"goods_cost"_s); g && g->is_block) {
                cost.clear();
                for (const Node &c : g->kids)
                    if (c.has_key && !c.is_block)
                        cost += u"\t\t%1 = %2\n"_s.arg(c.key, c.value);
            }
            if (const QString t = val(*b, u"time"_s); !t.isEmpty())
                time = t;
            if (const QString m = val(*b, u"max_level"_s); !m.isEmpty())
                max_level = m;
        }
    }
    const QString block = u"%1 = {\n\ttype = %1\n\tgoods_cost = {\n%2\t}\n\ttime = %3\n\tvisibility = yes\n\tonmap = no\n"
                          u"\tmax_level = %4\n\tprovince = yes\n}\n"_s.arg(key, cost, time, max_level);
    QString t = text;
    while (t.endsWith(u'\n') || t.endsWith(u' ') || t.endsWith(u'\t'))
        t.chop(1);
    return t.isEmpty() ? block : t + u"\n\n"_s + block;
}

QString remove_building(const QString &text, const QString &key)
{
    const Nodes nodes = script::parse(text);
    const Node *b = top_block(nodes, key);
    if (!b)
        return text;
    qsizetype a = line_start(text, b->kstart);
    if (!py::strip(text.mid(a, b->kstart - a)).isEmpty())
        a = b->kstart;
    qsizetype e = b->vend;
    const qsizetype le = line_end(text, e);
    if (py::strip(text.mid(e, le - e)).isEmpty())
        e = le;
    // one empty line before it goes too (add_building wrote it)
    if (a >= 2 && text[a - 1] == u'\n' && text[a - 2] == u'\n')
        a -= 1;
    return text.left(a) + text.mid(e);
}

// ---------------------------------------------------------------------------------------------- technologies
static long long num(const QString &v)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? static_cast<long long>(d) : 0;
}

std::vector<Tech> techs(const std::vector<std::pair<QString, QString>> &files)
{
    std::vector<Tech> out;
    for (const auto &[rel, text] : files)
        for (const Node &n : script::parse(text))
            if (n.has_key && n.is_block && kid(n, u"year"_s))
                out.push_back({rel, n.key, num(val(n, u"year"_s))});
    std::stable_sort(out.begin(), out.end(), [](const Tech &a, const Tech &b) { return a.year < b.year; });
    return out;
}

std::vector<Unlock> unlocks(const std::vector<std::pair<QString, QString>> &files, const QString &key)
{
    std::vector<Unlock> out;
    const QString mx = u"max_"_s + key;
    for (const auto &[rel, text] : files)
        for (const Node &n : script::parse(text)) {
            if (!n.has_key || !n.is_block)
                continue;
            Unlock u{rel, n.key, num(val(n, u"year"_s)), false, 0};
            for (const Node &c : n.kids) {
                if (is(c, u"activate_building"_s) && !c.is_block && c.value == key)
                    u.activates = true;
                if (is(c, mx) && !c.is_block)
                    u.adds += num(c.value);
            }
            if (u.activates || u.adds)
                out.push_back(u);
        }
    std::stable_sort(out.begin(), out.end(), [](const Unlock &a, const Unlock &b) { return a.year < b.year; });
    return out;
}

QString set_unlock(const QString &text, const QString &tech, const QString &key, bool activates, long long adds)
{
    const QString mx = u"max_"_s + key;
    QString t = text;
    {   // the old lines out
        const Nodes nodes = script::parse(t);
        const Node *b = top_block(nodes, tech);
        if (!b)
            return text;
        std::vector<Edit> cut;
        for (const Node &c : b->kids)
            if (!c.is_block && ((is(c, u"activate_building"_s) && c.value == key) || is(c, mx))) {
                qsizetype a = line_start(t, c.kstart), e = line_end(t, c.vend);
                if (!py::strip(t.mid(a, c.kstart - a)).isEmpty() || !py::strip(t.mid(c.vend, e - c.vend)).isEmpty()) {
                    a = c.kstart;                     // (more on that line: only the line's own words go)
                    e = c.vend;
                }
                cut.emplace_back(a, e, QString());
            }
        t = apply(t, cut);
    }
    QStringList add;
    if (activates)
        add << u"activate_building = "_s + key;
    if (adds)
        add << u"%1 = %2"_s.arg(mx).arg(adds);
    for (const QString &l : add) {
        const Nodes nodes = script::parse(t);
        const Node *b = top_block(nodes, tech);
        t = blocks::_insert_before_close(t, *b, l);
    }
    return t;
}

std::vector<std::pair<Unlock, QString>> level_steps(const std::vector<Unlock> &u, long long max_level)
{
    std::vector<std::pair<Unlock, QString>> out;
    long long lvl = 0;
    for (const Unlock &x : u) {
        const long long gives = x.adds;               // (activate_building gives no level: measured)
        const long long a = lvl + 1, b = lvl + gives;
        lvl = b;
        QString w = gives <= 0 ? (x.activates ? u"activates it (no level by itself)"_s : u"no level"_s)
                    : a == b   ? u"level %1"_s.arg(a)
                               : u"levels %1–%2"_s.arg(a).arg(b);
        if (gives > 0 && x.activates)
            w = u"activates it, "_s + w;
        if (gives > 0 && a > max_level)
            w += u" (above its max_level %1)"_s.arg(max_level);
        out.emplace_back(x, w);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- the province window
static const Node *window_named(const Nodes &nodes, const QString &name, bool deep)
{
    for (const Node &n : nodes) {
        if (!n.is_block)
            continue;
        if (n.has_key && py::lower(n.key) == u"windowtype" && val(n, u"name"_s) == name)
            return &n;
        if (deep)
            if (const Node *w = window_named(n.kids, name, true))
                return w;
    }
    return nullptr;
}
static const Node *pos_y(const Node &el, const QString &what = u"position"_s)   // the y of position = { x= y= }
{
    const Node *p = kid(el, what);
    return p && p->is_block ? kid(*p, u"y"_s) : nullptr;
}
static bool element(const Node &c)                // a part of a window (not its own position / size)
{
    return c.has_key && c.is_block && c.key != u"position" && c.key != u"size" && kid(c, u"name"_s);
}

Window read_window(const QString &gui)
{
    Window w;
    const Nodes nodes = script::parse(gui);
    const Node *root = top_block(nodes, u"guiTypes"_s);
    if (!root) {
        w.why = u"the file has no guiTypes block"_s;
        return w;
    }
    const Node *row = window_named(root->kids, u"building"_s, false);
    const Node *view = window_named(root->kids, u"province_view"_s, false);
    const Node *part = view ? window_named(view->kids, u"province_buildings"_s, true) : nullptr;
    if (!row || !view || !part) {
        w.why = !row ? u"the building row (windowType \"building\") is missing"_s
                     : u"the province window's buildings part (\"province_buildings\") is missing"_s;
        return w;
    }
    auto xy = [](const Node &el, int &x, int &y) {
        if (const Node *p = kid(el, u"position"_s); p && p->is_block) {
            if (const Node *a = kid(*p, u"x"_s))
                x = static_cast<int>(num(a->value));
            if (const Node *b = kid(*p, u"y"_s))
                y = static_cast<int>(num(b->value));
        }
    };
    int icon_from = -1;                           // the slot icon_x / icon_y were read from
    for (const Node &c : row->kids) {
        if (!element(c))
            continue;
        const QString nm = val(c, u"name"_s);
        if (nm.startsWith(u"build_icon")) {
            w.icons << nm;
            bool ok = false;
            const int i = nm.mid(10).toInt(&ok);
            int sx = 0, sy = 0;
            xy(c, sx, sy);
            if (ok && i >= 0 && i < 1000) {
                while (w.slot_sprites.size() <= i) {
                    w.slot_sprites << QString();
                    w.slot_x << 0;
                    w.slot_y << 0;
                }
                w.slot_sprites[i] = val(c, u"spriteType"_s);
                w.slot_x[i] = sx;
                w.slot_y[i] = sy;
            }
            // a row's picture place: the lowest slot the game shows per row (not a parked one)
            const int rank = ok && i >= 0 && i < LIVE_SLOTS ? i : sx <= PARK_X / 2 ? 2000 : 1000;
            if (icon_from < 0 || rank < icon_from) {
                w.icon_x = sx;
                w.icon_y = sy;
                icon_from = rank;
            }
        } else if (nm == u"description") {
            xy(c, w.name_x, w.name_y);
            if (const QString mw = val(c, u"maxWidth"_s); !mw.isEmpty())
                w.name_w = static_cast<int>(num(mw));
            if (const QString f = val(c, u"font"_s); !f.isEmpty())
                w.name_font = f;
        } else if (nm == u"expand") {
            xy(c, w.expand_x, w.expand_y);
            if (const QString s = val(c, u"quadTextureSprite"_s); !s.isEmpty())
                w.expand_sprite = s;
            if (const QString t = val(c, u"buttonText"_s); !t.isEmpty())
                w.expand_text = t;
            if (const QString f = val(c, u"buttonFont"_s); !f.isEmpty())
                w.expand_font = f;
        }
    }
    if (const Node *y = pos_y(*row, u"size"_s); y && num(y->value) > 0)
        w.step = static_cast<int>(num(y->value));
    if (const Node *s = kid(*row, u"size"_s); s && s->is_block)
        if (const Node *x = kid(*s, u"x"_s); x && num(x->value) > 0)
            w.row_w = static_cast<int>(num(x->value));
    {
        int px = 0, py = 0;
        xy(*part, px, py);
        w.part_x = px;
    }
    long long miny = -1;
    for (const Node &c : part->kids)
        if (element(c))
            if (const Node *y = pos_y(c); y && (miny < 0 || num(y->value) < miny))
                miny = num(y->value);
    w.room = miny < 0 ? 5 * w.step : static_cast<int>(miny + 1);
    w.fits = w.room / std::max(1, w.step);
    if (const Node *y = pos_y(*view))
        w.view_y = static_cast<int>(num(y->value));
    if (const Node *y = pos_y(*view, u"size"_s))
        w.view_h = static_cast<int>(num(y->value));
    if (const Node *y = pos_y(*part, u"size"_s))
        w.part_h = static_cast<int>(num(y->value));
    if (const Node *y = pos_y(*part))
        w.part_y = static_cast<int>(num(y->value));
    for (const Node &c : view->kids)
        if (element(c) && val(c, u"name"_s) == u"background") {
            w.bg_sprite = val(c, u"spriteType"_s);
            xy(c, w.bg_x, w.bg_y);
        }
    w.found = true;
    return w;
}

std::vector<Problem> window_problems(const QString &buildings, const QString &gui,
                                     const std::function<QString(const QString &)> &name)
{
    std::vector<Problem> out;
    const QStringList ks = province_keys(buildings);
    if (ks == GAME_THREE)
        return out;
    const Window w = read_window(gui);
    if (!w.found) {
        out.push_back({QString(), u"The province window (interface/province_interface.gui) can't be read: "_s + w.why, false});
        return out;
    }
    for (int i = 0; i < ks.size(); ++i)
        if (!w.icons.contains(u"build_icon%1"_s.arg(i)))
            out.push_back({ks[i], u"%1 (%2), province building no. %3, has no picture slot in the province window "
                                  u"(build_icon%4): the game crashes the moment any province is opened"_s
                                      .arg(name(ks[i]), ks[i], QString::number(i + 1), QString::number(i)),
                           true});
    // slots from build_icon3 on are drawn in every row (measured): more than one of them, or one written after a slot
    // the game switches per row, shows the wrong picture in some rows
    {
        QStringList shown;                                 // the slots from 3 on that are not parked
        bool after_live = false, live_seen = false;
        for (const QString &nm : w.icons) {
            bool ok = false;
            const int i = nm.mid(10).toInt(&ok);
            if (!ok)
                continue;
            if (i < LIVE_SLOTS)
                live_seen = true;
            else if (!w.parked(i)) {
                shown << nm;
                after_live = after_live || live_seen;
            }
        }
        if (shown.size() > 1 || (!shown.isEmpty() && after_live))
            out.push_back({QString(), u"Rows 4 and later may show another building's picture: the game draws the "
                                      u"picture slots from the 4th on (%1) in every row, on top of each other (tested "
                                      u"in the game). No crash. Game rules → Building types paints those rows' pictures "
                                      u"into the window's background instead when you change a building there"_s
                                          .arg(shown.join(u", "_s)),
                           false});
    }
    if (ks.size() > w.fits)
        out.push_back({QString(), u"The province window has room for %1 building rows, but there are %2 province "
                                  u"buildings: the last row lies on top of the army / navy buttons (seen in the game). "
                                  u"No crash"_s
                                      .arg(w.fits)
                                      .arg(ks.size()),
                       false});
    return out;
}

QString add_icon(const QString &gui, int index, const QString &sprite)
{
    const Nodes nodes = script::parse(gui);
    const Node *root = top_block(nodes, u"guiTypes"_s);
    const Node *row = root ? window_named(root->kids, u"building"_s, false) : nullptr;
    if (!row)
        return gui;
    const QString name = u"build_icon%1"_s.arg(index);
    const Node *first = nullptr;
    for (const Node &c : row->kids)
        if (element(c) && val(c, u"name"_s).startsWith(u"build_icon")) {
            if (val(c, u"name"_s) == name)
                return gui;                           // (already there)
            if (!first)
                first = &c;
        }
    if (!first)
        return gui;
    // a copy of the first picture slot: its place, another name and sprite
    std::vector<Edit> in;
    for (const Node &c : first->kids)
        if (!c.is_block && (is(c, u"name"_s) || is(c, u"spriteType"_s)))
            in.emplace_back(c.vstart - first->kstart, c.vend - first->kstart,
                            u"\"%1\""_s.arg(is(c, u"name"_s) ? name : sprite));
    const QString copy = apply(gui.mid(first->kstart, first->vend - first->kstart), in);
    const qsizetype at = line_start(gui, first->kstart);
    const QString ind = indent_at(gui, first->kstart);
    return gui.left(at) + ind + copy + u'\n' + gui.mid(at);
}

const int MIN_ROW = 30, SCREEN_H = 768, TOP_BAR = 82, LIVE_SLOTS = 3, PARK_X = -3000;

static const QRegularExpression NOTE_RE(
    uR"([ \t]*#[ \t]*ModEditor province window: background (\S+), rows at (-?\d+), (\d+) high, (\d+) a row, wanted (\d+)[^\n]*\n?)"_s);

Source source_of(const QString &gui)
{
    Source s;
    const auto m = NOTE_RE.match(gui);
    if (m.hasMatch()) {
        s = {m.captured(1), m.captured(2).toInt(), m.captured(3).toInt(), std::max(1, m.captured(4).toInt()),
             std::max(MIN_ROW, m.captured(5).toInt()), true};
        return s;
    }
    const Window w = read_window(gui);
    s.sprite = w.bg_sprite;
    s.top = w.part_y - w.bg_y;
    s.room = w.room;
    s.step = std::max(1, w.step);
    s.pref = std::max(MIN_ROW, w.step);
    return s;
}

Fit plan_fit(const Window &w, int rows, int pref)
{
    Fit f;
    f.rows = std::max(0, rows);
    pref = std::max(MIN_ROW, pref);
    // the rows may reach up to the top bar of a 768-high screen: the window hangs from the screen's bottom
    const int room_max = w.room + SCREEN_H + w.view_y - TOP_BAR;
    f.step = f.rows * pref <= room_max ? pref : std::max(MIN_ROW, room_max / std::max(1, f.rows));
    f.delta = f.rows * f.step - w.room;
    f.height = w.view_h + f.delta;
    f.top_768 = SCREEN_H + w.view_y - f.delta;
    f.fits_768 = f.top_768 >= TOP_BAR;
    f.words = u"%1 rows of %2 px: the window %3 px tall%4"_s.arg(f.rows).arg(f.step).arg(f.height).arg(
        f.fits_768 ? u", on a 768-high screen its top %1 px under the top bar"_s.arg(f.top_768 - TOP_BAR)
                   : u" — on a 768-high screen it reaches %1 px into the top bar even with rows of %2 px"_s
                         .arg(TOP_BAR - f.top_768)
                         .arg(MIN_ROW));
    if (f.step < pref)
        f.words += u" (rows lowered from %1 px so the window fits)"_s.arg(pref);
    return f;
}

QString fit_text(const QString &gui0, int rows, int step, const QString &bg_sprite, const Source &src)
{
    // the comment line first (out, then written again above province_view), so the places below are read afresh
    QString gui = gui0;
    gui.remove(NOTE_RE);
    const Nodes nodes = script::parse(gui);
    const Node *root = top_block(nodes, u"guiTypes"_s);
    const Node *row = root ? window_named(root->kids, u"building"_s, false) : nullptr;
    const Node *view = root ? window_named(root->kids, u"province_view"_s, false) : nullptr;
    const Node *part = view ? window_named(view->kids, u"province_buildings"_s, true) : nullptr;
    if (!row || !part)
        return gui0;
    const Window w = read_window(gui);
    const int d = rows * step - w.room;
    std::vector<Edit> e;
    auto shift = [&](const Node *y, int by) {
        if (y && by)
            e.emplace_back(y->vstart, y->vend, QString::number(num(y->value) + by));
    };
    shift(pos_y(*view), -d);
    shift(pos_y(*view, u"size"_s), d);
    shift(pos_y(*part, u"size"_s), d);
    for (const Node &c : part->kids)
        if (element(c))
            shift(pos_y(c), d);
    if (const Node *y = pos_y(*row, u"size"_s); y && num(y->value) != step)
        e.emplace_back(y->vstart, y->vend, QString::number(step));
    for (const Node &c : view->kids)
        if (element(c) && val(c, u"name"_s) == u"background")
            if (const Node *s = kid(c, u"spriteType"_s); s && !s->is_block && s->value != bg_sprite)
                e.emplace_back(s->vstart, s->vend, u"\"%1\""_s.arg(bg_sprite));
    const qsizetype at = line_start(gui, view->kstart);
    const QString nl = gui.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    e.emplace_back(at, at,
                   indent_at(gui, view->kstart) +
                       u"# ModEditor province window: background %1, rows at %2, %3 high, %4 a row, wanted %5 "
                       u"(the editor makes the background again from this picture)"_s
                           .arg(src.sprite)
                           .arg(src.top)
                           .arg(src.room)
                           .arg(src.step)
                           .arg(src.pref) +
                       nl);
    return apply(gui, e);
}

QImage fit_bg(const QImage &src_img, const Source &s, int rows, int step)
{
    const QImage src = src_img.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    const int top = std::clamp(s.top, 0, h), room = std::clamp(s.room, 0, h - top);
    const int r0 = std::max(1, room / std::max(1, s.step));
    const int foot = h - top - room;
    QImage out(w, top + rows * step + foot, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(0, 0, src, 0, 0, w, top);
    for (int r = 0; r < rows; ++r) {
        const int sy = top + std::min(r, r0 - 1) * s.step;
        const QImage band = src.copy(0, sy, w, std::min(s.step, h - sy));
        if (step == band.height())
            p.drawImage(0, top + r * step, band);
        else
            p.drawImage(QRect(0, top + r * step, w, step), band);
    }
    p.drawImage(0, top + rows * step, src, 0, top + room, w, foot);
    p.end();
    return out;
}

QString set_slot_sprite(const QString &gui, int index, const QString &sprite)
{
    const Nodes nodes = script::parse(gui);
    const Node *root = top_block(nodes, u"guiTypes"_s);
    const Node *row = root ? window_named(root->kids, u"building"_s, false) : nullptr;
    if (!row)
        return gui;
    for (const Node &c : row->kids)
        if (element(c) && val(c, u"name"_s) == u"build_icon%1"_s.arg(index))
            if (const Node *s = kid(c, u"spriteType"_s); s && !s->is_block && s->value != sprite)
                return gui.left(s->vstart) + u"\"%1\""_s.arg(sprite) + gui.mid(s->vend);
    return gui;
}

QString set_slot_place(const QString &gui, int index, int x, int y)
{
    const Nodes nodes = script::parse(gui);
    const Node *root = top_block(nodes, u"guiTypes"_s);
    const Node *row = root ? window_named(root->kids, u"building"_s, false) : nullptr;
    if (!row)
        return gui;
    for (const Node &c : row->kids) {
        if (!element(c) || val(c, u"name"_s) != u"build_icon%1"_s.arg(index))
            continue;
        const Node *p = kid(c, u"position"_s);
        if (!p || !p->is_block)
            return gui;
        std::vector<Edit> e;
        for (const auto &[k, v] : {std::pair{u"x"_s, x}, std::pair{u"y"_s, y}})
            if (const Node *n = kid(*p, k); n && !n->is_block && num(n->value) != v)
                e.emplace_back(n->vstart, n->vend, QString::number(v));
        return apply(gui, e);
    }
    return gui;
}

QImage plain_picture(const QImage &strip0, int frames)
{
    const QImage strip = strip0.convertToFormat(QImage::Format_ARGB32);
    frames = std::max(1, frames);
    const int fw = strip.width() / frames, h = strip.height();
    if (fw <= 0 || h <= 0)
        return QImage();
    const int f1 = frames > 1 ? 1 : 0;
    QImage out = strip.copy(f1 * fw, 0, fw, h);
    if (frames < 2)
        return out;
    // the number's box: where the frames 1.. differ (with one level only: the bottom right corner, where the game's
    // strips and the editor's have it)
    int x0 = fw, y0 = h, x1 = -1, y1 = -1;
    for (int f = 2; f < frames; ++f)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < fw; ++x)
                if (strip.pixel(f * fw + x, y) != strip.pixel(fw + x, y)) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
    // (the game's own strips change the whole picture with the level: then, too, the corner)
    if (frames == 2 || (x1 >= 0 && (x1 - x0 + 1 > fw / 2 || y1 - y0 + 1 > h * 2 / 3))) {
        x0 = fw - 14;
        y0 = h - 16;
        x1 = fw - 1;
        y1 = h - 1;
    }
    if (x1 < 0)
        return out;
    x0 = std::max(0, x0 - 2);
    y0 = std::max(0, y0 - 2);
    x1 = std::min(fw - 1, x1 + 2);
    y1 = std::min(h - 1, y1 + 2);
    // filled with the picture mirrored from above it (or, at the top, from below)
    for (int y = y0; y <= y1; ++y) {
        int sy = y0 - 1 - (y - y0);
        if (sy < 0)
            sy = y1 + 1 + (y - y0);
        if (sy < 0 || sy >= h || (sy >= y0 && sy <= y1))
            continue;
        for (int x = x0; x <= x1; ++x)
            out.setPixel(x, y, strip.pixel(fw * f1 + x, sy));
    }
    return out;
}

void paint_rows(QImage &bg, const Window &w, int top, int step, const std::vector<std::pair<int, QImage>> &pics)
{
    if (bg.isNull() || pics.empty())
        return;
    QPainter p(&bg);
    for (const auto &[i, pic] : pics)
        if (!pic.isNull())
            p.drawImage(w.part_x - w.bg_x + w.icon_x, top + i * step + w.icon_y, pic);
    p.end();
}

QString move_building(const QString &text, const QString &key, int to)
{
    const QStringList ks = province_keys(text);
    const int from = static_cast<int>(ks.indexOf(key));
    if (from < 0 || to == from || to < 0 || to >= ks.size())
        return text;
    const Nodes nodes = script::parse(text);
    const Node *b = top_block(nodes, key);
    if (!b)
        return text;
    // the block's own lines (from its line's start to past its closing line)
    const qsizetype a = line_start(text, b->kstart), e = line_end(text, b->vend - 1);
    QString lines = text.mid(a, e - a);
    if (!lines.endsWith(u'\n'))
        lines += u'\n';
    const QString rest = text.left(a) + text.mid(e);
    QStringList others = ks;
    others.removeAt(from);
    const Nodes rn = script::parse(rest);
    if (to < others.size()) {                           // before the building now at that place
        const Node *t = top_block(rn, others[to]);
        if (!t)
            return text;
        const qsizetype at = line_start(rest, t->kstart);
        return rest.left(at) + lines + rest.mid(at);
    }
    const Node *t = top_block(rn, others.last());      // after the last one
    if (!t)
        return text;
    const qsizetype at = line_end(rest, t->vend - 1);
    QString r = rest.left(at);
    if (!r.endsWith(u'\n'))
        r += u'\n';
    return r + lines + rest.mid(at);
}

QImage make_strip(const QImage &picture, int levels, int w, int h)
{
    levels = std::max(1, levels);
    QImage out(w * (levels + 1), h, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    const QImage pic = picture.isNull() ? QImage() : picture.convertToFormat(QImage::Format_ARGB32)
                                                          .scaled(w, h, Qt::KeepAspectRatioByExpanding,
                                                                  Qt::SmoothTransformation);
    QImage grey = pic;
    for (int y = 0; y < grey.height(); ++y)
        for (int x = 0; x < grey.width(); ++x) {
            const QColor c = grey.pixelColor(x, y);
            const int g = qGray(c.rgb()) * 2 / 3;
            grey.setPixelColor(x, y, QColor(g, g, g, c.alpha()));
        }
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(u"Times New Roman"_s);
    f.setBold(true);
    f.setPixelSize(std::max(9, h * 13 / 32));
    for (int i = 0; i <= levels; ++i) {
        const QRect cell(i * w, 0, w, h);
        p.save();
        p.setClipRect(cell);
        if (!pic.isNull()) {
            const QImage &src = i == 0 ? grey : pic;
            p.drawImage(cell.x() + (w - src.width()) / 2, (h - src.height()) / 2, src);
        }
        // the level's number at the bottom right, white with a dark edge (as the game's own strips)
        QPainterPath path;
        const QString s = QString::number(i);
        path.addText(0, 0, f, s);
        const QRectF br = path.boundingRect();
        path.translate(cell.right() - 2 - br.right(), h - 4);   // (on the baseline: every number as high)
        p.setPen(QPen(QColor(20, 16, 10), 3));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.fillPath(path, QColor(245, 240, 225));
        p.restore();
    }
    p.end();
    return out;
}

QString sprite_block(const QString &name, const QString &tex, int frames)
{
    QString t = tex;
    t.replace(u'/', u"\\\\"_s);
    QString s = u"\tspriteType = {\n\t\tname = \"%1\"\n\t\ttextureFile = \"%2\"\n"_s.arg(name, t);
    if (frames > 1)
        s += u"\t\tnoOfFrames = %1\n"_s.arg(frames);
    return s + u"\t}\n"_s;
}

QString add_sprites(const QString &gfx_text, const QStringList &blocks)
{
    QString t = gfx_text;
    for (const QString &b : blocks) {
        const Nodes bn = script::parse(b);
        const QString name = bn.empty() || bn[0].kids.empty() ? QString() : val(bn[0], u"name"_s);
        const Nodes nodes = script::parse(t);
        const Node *root = nullptr;
        for (const Node &n : nodes)
            if (n.has_key && n.is_block && py::lower(n.key) == u"spritetypes")
                root = &n;
        if (!root) {
            while (t.endsWith(u'\n'))
                t.chop(1);
            t += (t.isEmpty() ? QString() : u"\n\n"_s) + u"spriteTypes = {\n"_s + b + u"}\n"_s;
            continue;
        }
        bool have = false;
        for (const Node &c : root->kids)
            have = have || (c.is_block && !name.isEmpty() && val(c, u"name"_s) == name);
        if (have)
            continue;
        const qsizetype close = root->vend - 1;
        const qsizetype ls = line_start(t, close);
        if (py::strip(t.mid(ls, close - ls)).isEmpty())
            t = t.left(ls) + b + t.mid(ls);
        else
            t = t.left(close) + u'\n' + b + t.mid(close);
    }
    return t;
}

}  // namespace provbuild
