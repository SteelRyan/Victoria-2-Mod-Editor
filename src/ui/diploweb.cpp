// diploweb.py — Diplomacy, the views beside the map (see diploweb.h).
#include "ui/diploweb.h"
#include "ui/drawtext.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/pyjson.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/settings.h"
#include "grid/random.h"
#include "map/fourk.h"
#include "ui/nationextras.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;

namespace diploweb {

const std::vector<GroupDef> GROUPS = {
    {u"ally"_s, u"Allies"_s, {84, 146, 236}, 270},
    {u"war"_s, u"At war with"_s, {226, 74, 64}, 90},
    {u"subject"_s, u"Subjects (puppets, substates)"_s, {242, 212, 80}, 135},
    {u"overlord"_s, u"Its overlord"_s, {176, 104, 218}, 225},
    {u"sphere"_s, u"In its sphere"_s, {64, 204, 184}, 0},
    {u"lord"_s, u"In the sphere of"_s, {222, 88, 168}, 315},
    {u"guarantee"_s, u"Guarantees"_s, {150, 200, 120}, 45},
    {u"guaranteed_by"_s, u"Guaranteed by"_s, {120, 170, 110}, 180},
    {u"friendly"_s, u"Friendly (relations above 0)"_s, {120, 200, 140}, 300},
    {u"hostile"_s, u"Hostile (relations below 0)"_s, {200, 110, 100}, 120},
};
const QHash<QString, QString> SHORT_TITLE = {
    {u"ally"_s, u"Allies"_s},          {u"war"_s, u"At war"_s},
    {u"subject"_s, u"Subjects"_s},     {u"overlord"_s, u"Overlord"_s},
    {u"sphere"_s, u"In its sphere"_s}, {u"lord"_s, u"Its sphere's lord"_s},
    {u"guarantee"_s, u"Guarantees"_s}, {u"guaranteed_by"_s, u"Guaranteed by"_s},
    {u"friendly"_s, u"Friendly"_s},    {u"hostile"_s, u"Hostile"_s}};
const QHash<QString, Rgb> GROUP_RGB = [] {
    QHash<QString, Rgb> m;
    for (const GroupDef &g : GROUPS)
        m.insert(g.key, g.rgb);
    return m;
}();
const QHash<QString, QString> GROUP_TITLE = [] {
    QHash<QString, QString> m;
    for (const GroupDef &g : GROUPS)
        m.insert(g.key, g.title);
    return m;
}();
const QStringList PRIORITY = {u"war"_s,    u"overlord"_s,  u"subject"_s,       u"ally"_s,     u"lord"_s,
                              u"sphere"_s, u"guarantee"_s, u"guaranteed_by"_s, u"friendly"_s, u"hostile"_s};
const QSet<QString> SHOWN_AT_FIRST = {u"ally"_s, u"war"_s,    u"subject"_s,   u"overlord"_s,
                                      u"sphere"_s, u"lord"_s, u"guarantee"_s, u"guaranteed_by"_s};
const std::vector<std::pair<QString, QString>> SORTS = {{u"as"_s, u"In the files' order"_s},
                                                        {u"name"_s, u"Name (A–Z)"_s},
                                                        {u"rel"_s, u"Relations (best first)"_s},
                                                        {u"size"_s, u"Provinces owned (most first)"_s}};
const double WORLD_W = 1400.0, WORLD_H = 1000.0;
const std::vector<Rgb> SET_COLS = {{84, 146, 236}, {226, 160, 60}, {90, 190, 120},  {214, 92, 92},
                                   {172, 120, 222}, {70, 200, 200}, {230, 210, 80},  {232, 120, 180},
                                   {150, 170, 90},  {120, 150, 230}, {210, 140, 100}, {160, 160, 160}};

namespace {

constexpr double PI = 3.141592653589793;
constexpr double DEG_TO_RAD = PI / 180.0;          // math.radians: x * (pi / 180)
constexpr double RAD_TO_DEG = 180.0 / PI;          // math.degrees: x * (180 / pi)
double radians(double x) { return x * DEG_TO_RAD; }
double degrees(double x) { return x * RAD_TO_DEG; }

QColor qc(const Rgb &c) { return QColor(std::get<0>(c), std::get<1>(c), std::get<2>(c)); }

QString rgb_str(const Rgb &c)                      // str((r, g, b))
{
    return u"(%1, %2, %3)"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

QString plus(long long v)                          // "%+d" % v
{
    return v >= 0 ? u"+%1"_s.arg(v) : QString::number(v);
}

bool level5(const oob::Relation &r)                // str(r.get("level") or "").strip() == "5"
{
    return py::strip(r.level.value_or(QString())) == u"5";
}

}  // namespace

// ---------------------------------------------------------------------------------------------- data
Groups web_of(const QString &a, const moddata::Mod &mod, const oob::OOBEdits *oob, const diplomacy::DiploEdits *dip,
              const std::vector<diplopanel::WarRow> &wars, const QStringList &gps,
              const std::optional<QStringList> &related)
{
    // {group: [tags]} of nation a at the start. related: the nations with a relation value with a (friendly /
    // hostile are filled from them)
    Groups out;
    for (const GroupDef &g : GROUPS)
        out.insert(g.key, {});
    auto add = [&](const QString &k, const QString &t) {
        QStringList &l = out[k];
        if (!t.isEmpty() && t != a && mod.countries.contains(t) && !l.contains(t))
            l.append(t);
    };
    if (dip)
        for (const diplomacy::Record &r : dip->of(a)) {
            const QString &k = r.kind, &first = r.first, &second = r.second;
            if (k == u"alliance")
                add(u"ally"_s, first == a ? second : first);
            else if (k == u"vassal" || k == u"substate" || k == u"union")
                add(first == a ? u"subject"_s : u"overlord"_s, first == a ? second : first);
            else if (k == u"guarantee")
                add(first == a ? u"guarantee"_s : u"guaranteed_by"_s, first == a ? second : first);
        }
    for (const diplopanel::WarRow &w : wars) {
        if (w.att.contains(a)) {
            for (const QString &t : w.def)
                add(u"war"_s, t);
        } else if (w.def.contains(a)) {
            for (const QString &t : w.att)
                add(u"war"_s, t);
        }
    }
    if (oob) {
        for (const auto &[t, r] : oob->state(a).relations)
            if (level5(r))
                add(u"sphere"_s, t);
        for (const QString &g : gps)
            if (g != a && diplomacy::level_of(*oob, g, a) == 5)
                add(u"lord"_s, g);
        if (related)
            for (const QString &t : *related) {
                const long long v = diplomacy::relation_value(*oob, a, t);
                if (v > 0)
                    add(u"friendly"_s, t);
                else if (v < 0)
                    add(u"hostile"_s, t);
            }
    }
    return out;
}

bool has_guarantees(const diplomacy::DiploEdits *dip)
{
    if (!dip)
        return false;
    for (const diplomacy::Record &r : dip->at_start())
        if (r.kind == u"guarantee")
            return true;
    return false;
}

std::pair<QString, diplopanel::EditFn> tie_set(const QString &a, const QString &b, const QString &kind, bool on)
{
    // -> (undo label, fn(oob, dip)) that switches one tie between a (in view) and b on or off
    diplopanel::EditFn fn = [a, b, kind, on](oob::OOBEdits &oob, diplomacy::DiploEdits &dip) {
        using P = std::optional<std::pair<QString, QString>>;
        if (kind == u"ally") {
            dip.set_treaty(a, b, u"alliance"_s, on);
        } else if (kind == u"subject") {
            dip.set_subject(a, b, on ? P(std::pair{u"vassal"_s, a}) : std::nullopt);
        } else if (kind == u"overlord") {
            dip.set_subject(a, b, on ? P(std::pair{u"vassal"_s, b}) : std::nullopt);
        } else if (kind == u"sphere") {
            diplopanel::set_entry(oob, a, b, u"level"_s, on ? OptStr(u"5"_s) : std::nullopt);
        } else if (kind == u"lord") {
            diplopanel::set_entry(oob, b, a, u"level"_s, on ? OptStr(u"5"_s) : std::nullopt);
        } else if (kind == u"guarantee") {
            dip.set_treaty(a, b, u"guarantee"_s, on);
        } else if (kind == u"guaranteed_by") {
            dip.set_treaty(b, a, u"guarantee"_s, on);
        } else if (kind == u"none") {
            dip.set_treaty(a, b, u"alliance"_s, false);
            dip.set_subject(a, b, std::nullopt);
            dip.set_treaty(a, b, u"guarantee"_s, false);
            for (const auto &[x, y] : {std::pair{a, b}, std::pair{b, a}}) {
                const oob::Relation *r = oob.state(x).relations.get(y);
                if (r && r->level.value_or(QString()) == u"5")
                    diplopanel::set_entry(oob, x, y, u"level"_s, std::nullopt);
            }
        }
    };
    if (kind == u"none")
        return {u"%1 ↔ %2: every tie taken away"_s.arg(a, b), fn};
    const QHash<QString, QString> words = {{u"ally"_s, u"allied"_s},
                                           {u"subject"_s, u"%1 a puppet of %2"_s.arg(b, a)},
                                           {u"overlord"_s, u"%1 a puppet of %2"_s.arg(a, b)},
                                           {u"sphere"_s, u"%1 in %2's sphere"_s.arg(b, a)},
                                           {u"lord"_s, u"%1 in %2's sphere"_s.arg(a, b)},
                                           {u"guarantee"_s, u"%1 guarantees %2"_s.arg(a, b)},
                                           {u"guaranteed_by"_s, u"%1 guarantees %2"_s.arg(b, a)}};
    return {u"%1 ↔ %2: %3 — %4"_s.arg(a, b, words.value(kind), on ? u"on"_s : u"off"_s), fn};
}

// ---------------------------------------------------------------------------------------------- euler_grid
namespace {

std::pair<std::vector<std::pair<long long, long long>>, long long> _euler_grid(const std::vector<long long> &masks,
                                                                                long long k, long long seed)
{
    // Places blocks (one per mask: bit i = in group i) on a grid so that every group's rectangle — the box round its
    // own blocks — holds no block outside the group (an Euler diagram drawn with rectangles, as the user's EU
    // picture). A search (simulated annealing, fixed seed: the same picture every time) moves blocks between cells;
    // it also keeps the boxes small and groups without shared nations apart.
    const long long n = static_cast<long long>(masks.size());
    if (n == 0)
        return {{}, 0};
    std::vector<std::vector<char>> M(static_cast<size_t>(k), std::vector<char>(static_cast<size_t>(n)));   // group × block
    for (long long i = 0; i < k; ++i)
        for (long long b = 0; b < n; ++b)
            M[size_t(i)][size_t(b)] = char((masks[size_t(b)] >> i) & 1);
    std::vector<std::vector<char>> share(static_cast<size_t>(k), std::vector<char>(static_cast<size_t>(k)));
    for (long long i = 0; i < k; ++i)
        for (long long j = 0; j < k; ++j) {
            long long s = 0;
            for (long long b = 0; b < n; ++b)
                s += M[size_t(i)][size_t(b)] * M[size_t(j)][size_t(b)];
            share[size_t(i)][size_t(j)] = s > 0;
        }
    const long long G = static_cast<long long>(std::ceil(std::sqrt(double(n)))) + 2;
    const long long BIG = 1000000;
    std::vector<long long> x0(static_cast<size_t>(k)), x1(static_cast<size_t>(k)), y0(static_cast<size_t>(k)), y1(static_cast<size_t>(k));
    auto cost = [&](const std::vector<long long> &pos) -> std::pair<long long, long long> {
        std::vector<long long> px(static_cast<size_t>(n)), py(static_cast<size_t>(n));
        for (long long b = 0; b < n; ++b) {
            px[size_t(b)] = pos[size_t(b)] % G;
            py[size_t(b)] = pos[size_t(b)] / G;
        }
        for (long long i = 0; i < k; ++i) {
            long long a0 = BIG, a1 = -BIG, b0 = BIG, b1 = -BIG;
            for (long long b = 0; b < n; ++b)
                if (M[size_t(i)][size_t(b)]) {
                    a0 = std::min(a0, px[size_t(b)]);
                    a1 = std::max(a1, px[size_t(b)]);
                    b0 = std::min(b0, py[size_t(b)]);
                    b1 = std::max(b1, py[size_t(b)]);
                }
            x0[size_t(i)] = a0;
            x1[size_t(i)] = a1;
            y0[size_t(i)] = b0;
            y1[size_t(i)] = b1;
        }
        long long viol = 0, apart = 0, area = 0;
        for (long long i = 0; i < k; ++i)
            for (long long b = 0; b < n; ++b)
                if (!M[size_t(i)][size_t(b)] && px[size_t(b)] >= x0[size_t(i)] && px[size_t(b)] <= x1[size_t(i)] &&
                    py[size_t(b)] >= y0[size_t(i)] && py[size_t(b)] <= y1[size_t(i)])
                    ++viol;
        for (long long i = 0; i < k; ++i)
            for (long long j = 0; j < k; ++j) {
                const bool ox = std::min(x1[size_t(i)], x1[size_t(j)]) >= std::max(x0[size_t(i)], x0[size_t(j)]);
                const bool oy = std::min(y1[size_t(i)], y1[size_t(j)]) >= std::max(y0[size_t(i)], y0[size_t(j)]);
                if (ox && oy && !share[size_t(i)][size_t(j)])
                    ++apart;
            }
        apart = py::floordiv(apart, 2LL);              // groups without a shared nation, their boxes crossing
        for (long long i = 0; i < k; ++i)
            area += (x1[size_t(i)] - x0[size_t(i)] + 1) * (y1[size_t(i)] - y0[size_t(i)] + 1);
        const auto [pxl, pxh] = std::minmax_element(px.begin(), px.end());
        const auto [pyl, pyh] = std::minmax_element(py.begin(), py.end());
        const long long span = *pxh - *pxl + *pyh - *pyl;
        return {viol * 1000 + apart * 150 + area + 3 * span, viol};
    };
    grid::PyRandom rnd(static_cast<std::uint64_t>(seed));
    struct Best {
        long long cost, viol;
        std::vector<long long> pos;
    };
    std::optional<Best> best;
    const long long iters = std::min(9000LL, 1500 + 120 * n);
    for (int start = 0; start < 3; ++start) {
        std::vector<long long> cells(static_cast<size_t>(G * G));
        for (long long c = 0; c < G * G; ++c)
            cells[size_t(c)] = c;
        rnd.shuffle(cells);
        std::vector<long long> pos(cells.begin(), cells.begin() + n);
        std::map<long long, long long> occ;
        for (long long i = 0; i < n; ++i)
            occ[pos[size_t(i)]] = i;
        auto [cur, v] = cost(pos);
        Best run_best{cur, v, pos};
        double T = 60.0;
        const double decay = std::pow(0.3 / 60.0, 1.0 / double(iters));
        for (long long it = 0; it < iters; ++it) {
            const long long i = rnd.randrange(n), c = rnd.randrange(G * G);
            const long long old = pos[size_t(i)];
            if (c == old)
                continue;
            const auto jt = occ.find(c);
            const std::optional<long long> j = jt != occ.end() ? std::optional<long long>(jt->second) : std::nullopt;
            pos[size_t(i)] = c;
            if (j)
                pos[size_t(*j)] = old;
            const auto [nw, nv] = cost(pos);
            if (nw <= cur || rnd.random() < std::exp(double(cur - nw) / T)) {
                cur = nw;
                v = nv;
                occ[c] = i;
                if (j)
                    occ[old] = *j;
                else
                    occ.erase(old);
                if (cur < run_best.cost)
                    run_best = Best{cur, v, pos};
            } else {
                pos[size_t(i)] = old;
                if (j)
                    pos[size_t(*j)] = c;
            }
            T *= decay;
        }
        if (!best || run_best.cost < best->cost)
            best = run_best;
        if (best->viol == 0 && start >= 1)
            break;
    }
    std::vector<std::pair<long long, long long>> out;
    for (long long p : best->pos)
        out.emplace_back(p % G, p / G);
    return {out, best->viol};
}

}  // namespace

std::pair<std::vector<std::pair<long long, long long>>, long long> euler_grid(const std::vector<long long> &masks,
                                                                               long long k, long long seed)
{
    // (the same groups again: at once)
    static std::map<std::tuple<std::vector<long long>, long long, long long>,
                    std::pair<std::vector<std::pair<long long, long long>>, long long>>
        cache;
    const auto key = std::tuple{masks, k, seed};
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    if (cache.size() > 64)
        cache.clear();
    return cache[key] = _euler_grid(masks, k, seed);
}

// ---------------------------------------------------------------------------------------------- ZoomCanvas
ZoomCanvas::ZoomCanvas(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(420, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRectF ZoomCanvas::world_rect()
{
    return QRectF(0, 0, WORLD_W, WORLD_H);
}

void ZoomCanvas::fit()
{
    const QRectF r = world_rect();
    if (r.width() <= 0 || r.height() <= 0 || width() < 10)
        return;
    scale_ = std::min(double(width()) / r.width(), double(height()) / r.height()) * 0.96;
    off = QPointF((double(width()) - r.width() * scale_) / 2.0 - r.left() * scale_,
                  (double(height()) - r.height() * scale_) / 2.0 - r.top() * scale_);
    _fitted = true;
    _moved = false;
    update();
}

void ZoomCanvas::resizeEvent(QResizeEvent *e)
{
    if (!_moved)                                   // (until it is moved / zoomed by hand it follows the view's size)
        fit();
    QWidget::resizeEvent(e);
}

QPointF ZoomCanvas::to_world(const QPointF &p) const
{
    return QPointF((p.x() - off.x()) / scale_, (p.y() - off.y()) / scale_);
}

void ZoomCanvas::begin(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(theme::BG));
    p.translate(off);
    p.scale(scale_, scale_);
}

void ZoomCanvas::wheelEvent(QWheelEvent *e)
{
    const double f = e->angleDelta().y() > 0 ? 1.18 : 1 / 1.18;
    const QPointF pos = e->position();
    const QPointF w = to_world(pos);
    scale_ = std::max(0.15, std::min(6.0, scale_ * f));
    off = QPointF(pos.x() - w.x() * scale_, pos.y() - w.y() * scale_);
    _fitted = _moved = true;
    update();
}

void ZoomCanvas::start_pan(const QPointF &pos)
{
    _pan = std::pair{pos, off};
    setCursor(Qt::ClosedHandCursor);
}

void ZoomCanvas::pan_to(const QPointF &pos)
{
    const auto [p0, o0] = *_pan;
    off = o0 + (pos - p0);
    _moved = true;
    update();
}

void ZoomCanvas::end_pan()
{
    _pan.reset();
    setCursor(Qt::ArrowCursor);
}

// ---------------------------------------------------------------------------------------------- Web
Web::Web(FlagFn flag_of, NameFn name_of, QWidget *parent)
    : ZoomCanvas(parent), flag_of(std::move(flag_of)), name_of(std::move(name_of)), shown(SHOWN_AT_FIRST)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    order = [](const QStringList &tags) { return tags; };
}

QRectF Web::world_rect()
{
    // the pie and its names (Fit shows that much); the great powers: the whole world
    if (mode != u"nation" || !a || a->isEmpty())
        return ZoomCanvas::world_rect();
    layout();
    double R = R_IN + 110;
    if (!_sections.empty()) {
        R = _sections[0].rout;
        for (const Sec &s : _sections)
            R = std::max(R, s.rout);
    }
    return QRectF(WORLD_W / 2.0 - R - 210, WORLD_H / 2.0 - R - 70, 2 * R + 420, 2 * R + 140);
}

void Web::set_nation(const OptStr &na, const Groups &g)
{
    if (na != a || mode != u"nation")
        _fitted = false;
    mode = u"nation"_s;
    a = na;
    groups = g;
    if (!_fitted)
        fit();
    update();
}

void Web::set_gps(const std::vector<GpRow> &g, const std::vector<std::pair<QString, QString>> &w)
{
    if (mode != u"gps")
        _fitted = false;
    mode = u"gps"_s;
    gp = g;
    wars = w;
    if (!_fitted)
        fit();
    update();
}

// ------------------------------------------------------------------ drawing
void Web::_node(QPainter &p, const QPointF &c, const QString &tag, bool big, std::optional<Rgb> rg)
{
    const double w = big ? 64 : 40, h = big ? 42 : 27;
    const QRectF r(c.x() - w / 2, c.y() - h / 2, w, h);
    if (rg) {
        p.setPen(QPen(qc(*rg), 4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(-4, -4, 4, 4), 5, 5);
    }
    const std::optional<QImage> img = flag_of(tag);
    if (img)
        p.drawImage(r, *img);
    else
        p.fillRect(r, QColor(90, 92, 100));
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    QFont f(font());
    f.setPixelSize(big ? 13 : 11);
    f.setBold(big || (_hover && _hover->first && tag == *_hover->first));
    p.setFont(f);
    const QFontMetrics fm(f);
    const QString nm = fm.elidedText(name_of(tag), Qt::ElideRight, big ? 120 : 96);
    QPainterPath path;
    path.addText(c.x() - fm.horizontalAdvance(nm) / 2.0, r.bottom() + 3 + fm.ascent(), f, nm);
    p.setPen(QPen(QColor(10, 10, 10), 3));
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(theme::INK));
    p.drawPath(path);
    _hit.emplace_back(r.adjusted(-6, -6, 6, 20), tag);
}

// ------------------------------------------------------------------ the pie (the user, 2026-10-27)
std::vector<SecKey> Web::section_keys(const std::vector<SecKey> &keys) const
{
    // The sections in their order round the ring (clockwise from the top): the order the user dragged them into
    // (sec_order, kept for the mod), new ones by the groups' places in GROUPS; a section of nations with two ties
    // stands between those two when they are neighbours (so it reads as their overlap), else after the first; three
    // ties or more: after the first.
    std::vector<GroupDef> sorted_g = GROUPS;
    std::stable_sort(sorted_g.begin(), sorted_g.end(), [](const GroupDef &x, const GroupDef &y) {
        return py::mod(x.angle + 90LL, 360LL) < py::mod(y.angle + 90LL, 360LL);
    });
    QStringList base;
    for (const GroupDef &g : sorted_g)
        base << g.key;
    auto has = [](const std::vector<SecKey> &v, const SecKey &k) { return std::find(v.begin(), v.end(), k) != v.end(); };
    std::vector<SecKey> known;
    for (const SecKey &k : sec_order)
        if (has(keys, k))
            known.push_back(k);
    std::vector<SecKey> singles;
    for (const SecKey &k : keys)
        if (k.size() == 1 && !has(known, k))
            singles.push_back(k);
    std::stable_sort(singles.begin(), singles.end(),
                     [&](const SecKey &x, const SecKey &y) { return base.indexOf(x[0]) < base.indexOf(y[0]); });
    for (const SecKey &k : singles) {
        std::optional<size_t> last;
        for (size_t i = 0; i < known.size(); ++i)
            if (known[i].size() == 1 && base.indexOf(known[i][0]) < base.indexOf(k[0]))
                last = i;
        known.insert(known.begin() + std::ptrdiff_t(last ? *last + 1 : 0), k);
    }
    std::vector<SecKey> multi;
    for (const SecKey &k : keys)
        if (k.size() > 1 && !has(known, k))
            multi.push_back(k);
    std::stable_sort(multi.begin(), multi.end(), [](const SecKey &x, const SecKey &y) { return x.size() < y.size(); });
    for (const SecKey &k : multi) {
        std::vector<size_t> at;
        for (size_t i = 0; i < known.size(); ++i)
            if (known[i].size() == 1 && k.contains(known[i][0]))
                at.push_back(i);
        if (k.size() == 2 && at.size() == 2 && at[1] - at[0] == 1)
            known.insert(known.begin() + std::ptrdiff_t(at[1]), k);   // between its two
        else if (!at.empty())
            known.insert(known.begin() + std::ptrdiff_t(at[0] + 1), k);
        else
            known.push_back(k);
    }
    return known;
}

Web::Layout Web::layout()
{
    // (middle, [(group, colour, tag, point)], [(label point, colour, text, group)], {tag: [groups]}) — in the
    // world's coordinates; _sections [(key, start, end, outer radius, tags)] (angles in degrees, clockwise from 3
    // o'clock). A nation stands in the section of exactly the groups shown it is in; group = the first.
    Layout L;
    L.mid = QPointF(WORLD_W / 2.0, WORLD_H / 2.0);
    const QPointF mid = L.mid;
    const double r1 = R_OUT;
    for (const QString &k : PRIORITY)
        if (const QStringList *ts = groups.get(k))
            for (const QString &t : *ts)
                L.member[t].append(k);
    OrderedMap<SecKey, QStringList> secs;
    QStringList seen;
    for (const QString &k : PRIORITY)              // (the nations in the files' order, group by group)
        if (const QStringList *ts = groups.get(k))
            for (const QString &t : *ts)
                if (!seen.contains(t))
                    seen << t;
    for (const QString &t : seen) {
        SecKey key;
        for (const QString &k : *L.member.get(t))
            if (shown.contains(k))
                key << k;
        if (!key.isEmpty())
            secs[key].append(t);
    }
    std::vector<SecKey> skeys;
    for (const auto &[k, _t] : secs)
        skeys.push_back(k);
    const std::vector<SecKey> keys = section_keys(skeys);
    sec_keys = keys;
    const qsizetype n = qsizetype(keys.size());
    const double gap = n > 1 ? 3.0 : 0.0;
    std::vector<double> weight;
    for (const SecKey &k : keys)
        weight.push_back(double(secs.get(k)->size()) + 1.5);
    double total = py::sum(weight);
    if (total == 0)
        total = 1.0;
    const double free_ = 360.0 - gap * double(n);
    _sections.clear();
    double ang = -90.0;
    struct Plan {
        SecKey key;
        double a0, a1;
        QStringList tags;
        std::vector<std::pair<double, double>> pts;
    };
    std::vector<Plan> plan;
    for (size_t i = 0; i < keys.size(); ++i) {     // the slices, and the flags' rows in each
        const double span = free_ * weight[i] / total;
        const double a0 = ang + gap / 2.0, a1 = ang + gap / 2.0 + span;
        ang += span + gap;
        const QStringList tags = order(*secs.get(keys[i])).mid(0, 80);
        plan.push_back({keys[i], a0, a1, tags, _fill_section(a0, a1, tags.size())});
    }
    // the ring as wide as its fullest section needs (one ring for all: a round pie), never wider than R_OUT
    double widest = R_IN + 110.0;
    for (const Plan &pl : plan)
        for (const auto &[_a, rr] : pl.pts)
            widest = std::max(widest, rr + 46.0);
    ring = std::min(r1, widest);
    for (const Plan &pl : plan) {
        const QString name = pl.key.join(u'+');
        const Rgb col = GROUP_RGB.value(pl.key[0]);
        double rout = ring;
        for (qsizetype i = 0; i < pl.tags.size() && size_t(i) < pl.pts.size(); ++i) {
            const QString &tag = pl.tags[i];
            auto [an, rr] = pl.pts[size_t(i)];
            if (const Custom *fx = custom.get({a.value_or(QString()), tag}); fx && fx->section == name) {
                const auto cl = _clamp(pl.a0, pl.a1, pl.a0 + fx->part * (pl.a1 - pl.a0), fx->radius);
                an = cl.first;
                rr = cl.second;
            }
            rout = std::max(rout, rr + 40);
            L.placed.push_back({pl.key[0], col, tag,
                                QPointF(mid.x() + rr * std::cos(radians(an)), mid.y() + rr * std::sin(radians(an)))});
        }
        _sections.push_back({pl.key, pl.a0, pl.a1, rout, pl.tags});
        QString text;
        if (pl.key.size() == 1) {
            text = u"%1 (%2)"_s.arg(GROUP_TITLE.value(pl.key[0])).arg(pl.tags.size());
        } else {
            QStringList st;
            for (const QString &k : pl.key)
                st << SHORT_TITLE.value(k);
            text = u"%1 (%2)"_s.arg(st.join(u" + "_s)).arg(pl.tags.size());
        }
        const double am = radians((pl.a0 + pl.a1) / 2.0);
        L.labels.push_back({QPointF(mid.x() + (rout + 30) * std::cos(am), mid.y() + (rout + 30) * std::sin(am)), col,
                            text, pl.key[0]});
    }
    L.labels = _clear_of_flags(mid, L.placed, L.labels);
    return L;
}

std::pair<double, double> Web::_clamp(double a0, double a1, double ang, double rr) const
{
    // a place kept inside its section: the flag never leaves it (its half width off the edges)
    rr = std::max(R_IN + 34, std::min(rr, ring - 26));
    const double m = degrees(26.0 / rr);
    if (a1 - a0 <= 2 * m)
        return {(a0 + a1) / 2.0, rr};
    return {std::max(a0 + m, std::min(ang, a1 - m)), rr};
}

std::vector<std::pair<double, double>> Web::_fill_section(double a0, double a1, long long n) const
{
    // [(angle, radius)] for n flags in the section a0…a1: rows on arcs from the inside out, each as full as its
    // length allows (a flag's cell: 76 px along the arc — the flag, its ring and its name — 62 px outwards), spread
    // evenly along it
    std::vector<std::pair<double, double>> out;
    long long j = 0;
    while (static_cast<long long>(out.size()) < n) {
        const double rr = R_IN + 50 + 62 * double(j);
        const double m = degrees(30.0 / rr);
        const double span = std::max(0.0, (a1 - a0) - 2 * m);
        const long long cap = std::max(1LL, static_cast<long long>(py::floordiv(radians(span) * rr, 76.0)));
        const long long k = std::min(cap, n - static_cast<long long>(out.size()));
        for (long long i = 0; i < k; ++i)
            out.emplace_back(span > 0 ? a0 + m + span * (double(i) + 0.5) / double(k) : (a0 + a1) / 2.0, rr);
        ++j;
    }
    return out;
}

std::optional<SecKey> Web::section_at(const QPointF &w) const
{
    // the section under the world point w (for dragging it round the ring), or None
    const QPointF mid(WORLD_W / 2.0, WORLD_H / 2.0);
    const double dx = w.x() - mid.x(), dy = w.y() - mid.y();
    const double rr = py::hypot(dx, dy);
    const double ang = degrees(std::atan2(dy, dx));
    for (const Sec &s : _sections) {
        const double x = py::mod(ang - s.a0, 360.0);
        if (R_IN <= rr && rr <= s.rout + 50 && x <= (s.a1 - s.a0))
            return s.key;
    }
    return std::nullopt;
}

QRectF Web::label_rect(const QPointF &pt, const QString &text) const
{
    const double tw = QFontMetrics(_label_font()).horizontalAdvance(text) + 16;
    return QRectF(pt.x() - tw / 2.0, pt.y() - 11, tw, 22);
}

QFont Web::_label_font() const
{
    QFont f(font());
    f.setPixelSize(13);
    f.setBold(true);
    return f;
}

std::vector<Web::Label> Web::_clear_of_flags(const QPointF &mid, const std::vector<Placed> &placed,
                                             const std::vector<Label> &labels) const
{
    // The user, 2026-10-27: a group's name must never stand on a flag. Each name keeps its place when that is free;
    // else it goes to the nearest free place (rings round it), never on a flag, its name, its dots, the nation in the
    // middle or another group's name.
    std::vector<QRectF> taken{QRectF(mid.x() - 70, mid.y() - 28, 140, 72)};   // the nation in the middle + its name
    for (const Placed &pl : placed) {
        QPointF pt = pl.pt;
        if (_drag && _drag->tag == pl.tag && _drag->moved)
            pt = _drag->cur;
        taken.emplace_back(pt.x() - 52, pt.y() - 26, 104, 62);
    }
    const QRectF world(4, 4, WORLD_W - 8, WORLD_H - 8);
    std::vector<Label> out;
    const QFontMetrics fm(_label_font());
    for (const Label &lb : labels) {
        const double tw = fm.horizontalAdvance(lb.text) + 16;
        auto box = [tw](const QPointF &q) { return QRectF(q.x() - tw / 2.0, q.y() - 11, tw, 22); };
        QPointF best = lb.pt;
        for (int step = 0; step < 60; ++step) {
            const double dist = 14.0 * step;
            std::vector<QPointF> tries;
            if (step == 0)
                tries.push_back(lb.pt);
            else
                for (int a = 0; a < 360; a += 15)
                    tries.emplace_back(lb.pt.x() + dist * std::cos(radians(a)), lb.pt.y() + dist * std::sin(radians(a)));
            bool found = false;
            for (const QPointF &q : tries) {
                const QRectF bq = box(q);
                if (!world.contains(bq))
                    continue;
                bool hit = false;
                for (const QRectF &r : taken)
                    if (bq.intersects(r)) {
                        hit = true;
                        break;
                    }
                if (!hit) {
                    best = q;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        taken.push_back(box(best).adjusted(-4, -4, 4, 4));
        out.push_back({best, lb.col, lb.text, lb.grp});
    }
    return out;
}

void Web::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    begin(p);
    _hit.clear();
    if (mode == u"gps") {
        _paint_gps(p);
        return;
    }
    if (!a || a->isEmpty()) {
        p.resetTransform();
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect(), Qt::AlignCenter, u"Pick a nation on the panel (or switch to the map and click one)."_s);
        return;
    }
    const Layout L = layout();
    const QPointF mid = L.mid;
    const std::optional<SecKey> held = _secdrag && _secdrag->moved ? std::optional<SecKey>(_secdrag->key) : std::nullopt;
    for (const Sec &s : _sections) {               // the sections: slices of the ring
        QPainterPath path;
        const double rout = s.rout;
        const QRectF outer(mid.x() - rout, mid.y() - rout, 2 * rout, 2 * rout);
        const double r0 = R_IN;
        const QRectF inner(mid.x() - r0, mid.y() - r0, 2 * r0, 2 * r0);
        path.arcMoveTo(outer, -s.a0);
        path.arcTo(outer, -s.a0, -(s.a1 - s.a0));
        path.arcTo(inner, -s.a1, s.a1 - s.a0);
        path.closeSubpath();
        const QColor c = qc(GROUP_RGB.value(s.key[0]));
        QColor fill(c);
        fill.setAlpha(held && s.key == *held ? 70 : 40);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(path);
        if (s.key.size() > 1) {                    // two ties or more: wide stripes, each colour
            p.save();
            p.setClipPath(path);
            std::vector<QColor> cols;
            for (const QString &k : s.key) {
                QColor c3 = qc(GROUP_RGB.value(k));
                c3.setAlpha(60);
                cols.push_back(c3);
            }
            const double R = rout + 10;
            int i = 0;
            for (int x = int(-2 * R); x < int(2 * R); x += 22, ++i) {
                p.setPen(QPen(cols[size_t(i) % cols.size()], 9));
                p.drawLine(QPointF(mid.x() + x - R, mid.y() - R), QPointF(mid.x() + x + R, mid.y() + R));
            }
            p.restore();
        }
        QColor edge(c);
        const bool is_held = held && s.key == *held;
        edge.setAlpha(is_held ? 230 : 170);
        p.setPen(QPen(edge, is_held ? 4 : 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    for (const Placed &pl : L.placed) {
        QPointF pt = pl.pt;
        if (_drag && _drag->tag == pl.tag && _drag->moved)
            pt = _drag->cur;
        _node(p, pt, pl.tag, false, pl.col);
    }
    _node(p, mid, *a, true);
    p.setFont(_label_font());
    for (const Label &lb : L.labels) {             // the groups' names, on a dark backing (never on a flag)
        const QRectF r = label_rect(lb.pt, lb.text);
        p.setPen(QPen(qc(lb.col), 2));
        p.setBrush(QColor(16, 18, 22, 235));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(qc(lb.col));
        drawtext::draw(p, r, Qt::AlignCenter, lb.text);
    }
    if (L.placed.empty()) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, QRectF(0, mid.y() + 50, WORLD_W, 40), Qt::AlignCenter, u"No ties at the start in the groups shown."_s);
    }
}

void Web::_paint_gps(QPainter &p)
{
    const QPointF mid(WORLD_W / 2.0, WORLD_H / 2.0);
    const double R = std::min(WORLD_W, WORLD_H) / 2.0 - 190;
    const long long n = std::max<long long>(1, static_cast<long long>(gp.size()));
    std::map<QString, std::pair<QPointF, double>> pos;
    for (size_t i = 0; i < gp.size(); ++i) {
        const double d = -90 + 360.0 * double(i) / double(n);
        pos[gp[i].g] = {QPointF(mid.x() + R * std::cos(radians(d)), mid.y() + R * std::sin(radians(d))), d};
    }
    for (const auto &[x, y] : wars)
        if (pos.contains(x) && pos.contains(y)) {
            p.setPen(QPen(qc(GROUP_RGB.value(u"war"_s)), 5));
            p.drawLine(pos[x].first, pos[y].first);
        }
    for (const GpRow &r : gp)
        for (const QString &y : r.al)
            if (pos.contains(y) && r.g < y) {
                p.setPen(QPen(qc(GROUP_RGB.value(u"ally"_s)), 5));
                p.drawLine(pos[r.g].first, pos[y].first);
            }
    for (const GpRow &r : gp) {
        const auto [c, d] = pos[r.g];
        const qsizetype m = r.sph.size();
        for (qsizetype i = 0; i < std::min<qsizetype>(m, 16); ++i) {
            const QString &t = r.sph[i];
            const double dd =
                d + (m > 1 ? std::min(150.0, 30.0 * double(m - 1)) * (double(i) / double(m - 1) - 0.5) : 0.0);
            static const int RR3[3] = {78, 128, 172};
            const int rr = m > 6 ? RR3[i % 3] : (i % 2 == 0 ? 128 : 84);
            const QPointF pt(c.x() + rr * std::cos(radians(dd)), c.y() + rr * std::sin(radians(dd)));
            p.setPen(QPen(qc(GROUP_RGB.value(u"sphere"_s)), 3));
            p.drawLine(c, pt);
            _node(p, pt, t, false, GROUP_RGB.value(u"sphere"_s));
        }
        if (m > 16) {
            p.setPen(QColor(theme::INK_DIM));
            drawtext::draw(p, QRectF(c.x() - 60, c.y() + 40, 120, 16), Qt::AlignCenter,
                       u"+%1 more in its sphere"_s.arg(m - 16));
        }
    }
    for (const GpRow &r : gp)
        _node(p, pos[r.g].first, r.g, true);
}

// ------------------------------------------------------------------ mouse
OptStr Web::_at(const QPointF &pos) const
{
    const QPointF w = to_world(pos);
    for (auto it = _hit.rbegin(); it != _hit.rend(); ++it)
        if (it->first.contains(w))
            return it->second;
    return std::nullopt;
}

void Web::mousePressEvent(QMouseEvent *e)
{
    const OptStr t = _at(e->position());
    if (e->button() == Qt::RightButton) {
        if (t && mode == u"nation" && t != a)
            Q_EMIT menu(*t, e->globalPosition());
        else if (!t)
            start_pan(e->position());
        return;
    }
    if (e->button() != Qt::LeftButton) {
        if (e->button() == Qt::MiddleButton)
            start_pan(e->position());
        return;
    }
    if (!t) {
        const std::optional<SecKey> sec = mode == u"nation" ? section_at(to_world(e->position())) : std::nullopt;
        if (sec)                                   // the empty part of a section: drag it round the ring
            _secdrag = SecDrag{*sec, e->position(), false};
        else
            start_pan(e->position());
        return;
    }
    _drag = Drag{*t, e->position(), to_world(e->position()), false};
}

std::optional<std::tuple<SecKey, double, double>> Web::_section_of(const QString &tag) const
{
    for (const Sec &s : _sections)
        if (s.tags.contains(tag))
            return std::tuple{s.key, s.a0, s.a1};
    return std::nullopt;
}

QPointF Web::_held_point(const QString &tag, const QPointF &w) const
{
    // where a flag being dragged stands: under the mouse, but never out of its own section
    const auto s = _section_of(tag);
    if (!s)
        return w;
    const QPointF mid(WORLD_W / 2.0, WORLD_H / 2.0);
    const double dx = w.x() - mid.x(), dy = w.y() - mid.y();
    double ang = degrees(std::atan2(dy, dx));
    const double a0 = std::get<1>(*s), a1 = std::get<2>(*s);
    ang = a0 + py::mod(ang - a0, 360.0);
    if (ang > a1)                                  // (outside its angles: the nearer edge)
        ang = (ang - a1) < (a0 + 360.0 - ang) ? a1 : a0;
    const auto [an, rr] = _clamp(a0, a1, ang, py::hypot(dx, dy));
    return QPointF(mid.x() + rr * std::cos(radians(an)), mid.y() + rr * std::sin(radians(an)));
}

void Web::_move_section(const QPointF &w)
{
    // while a section is dragged: past the middle of a neighbour, it takes that one's place
    std::vector<SecKey> keys = sec_keys;
    const auto jt = std::find(keys.begin(), keys.end(), _secdrag->key);
    if (jt == keys.end())
        return;
    const QPointF mid(WORLD_W / 2.0, WORLD_H / 2.0);
    const double ang = degrees(std::atan2(w.y() - mid.y(), w.x() - mid.x()));
    for (size_t i = 0; i < _sections.size(); ++i) {
        const Sec &s = _sections[i];
        const double x = py::mod(ang - s.a0, 360.0);
        if (s.key == _secdrag->key || x > (s.a1 - s.a0))
            continue;
        const size_t j = size_t(std::find(keys.begin(), keys.end(), _secdrag->key) - keys.begin());
        if ((i > j && x > (s.a1 - s.a0) / 2.0) || (i < j && x < (s.a1 - s.a0) / 2.0)) {
            keys.erase(keys.begin() + std::ptrdiff_t(j));
            keys.insert(keys.begin() + std::ptrdiff_t(std::min(i, keys.size())), _secdrag->key);
            sec_order = keys;
            update();
        }
        return;
    }
}

void Web::mouseMoveEvent(QMouseEvent *e)
{
    if (_pan) {
        pan_to(e->position());
        return;
    }
    if (_secdrag) {
        SecDrag &d = *_secdrag;
        if (!d.moved && (e->position() - d.p0).manhattanLength() >= 6) {
            d.moved = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (d.moved) {
            _move_section(to_world(e->position()));
            update();
        }
        return;
    }
    if (_drag) {
        Drag &d = *_drag;
        d.cur = _held_point(d.tag, to_world(e->position()));
        if (!d.moved && (e->position() - d.p0).manhattanLength() >= 6 && mode == u"nation" && OptStr(d.tag) != a)
            d.moved = true;
        if (d.moved) {
            setCursor(Qt::ClosedHandCursor);
            update();
        }
        return;
    }
    const OptStr t = _at(e->position());
    const std::optional<SecKey> sec = t || mode != u"nation" ? std::nullopt : section_at(to_world(e->position()));
    const std::pair<OptStr, std::optional<SecKey>> hv{t, sec};
    if (!_hover || *_hover != hv) {
        _hover = hv;
        setCursor(t ? Qt::PointingHandCursor : sec ? Qt::SizeAllCursor : Qt::OpenHandCursor);
        QString tip;
        if (t && mode == u"nation" && t != a) {
            tip = u"%1 — click: its ties with %2 · double-click: show it · drag: move it (inside its section)"_s.arg(
                name_of(*t), name_of(a.value_or(QString())));
        } else if (t) {
            tip = u"%1 — double-click: show it"_s.arg(name_of(*t));
        } else if (sec) {
            QStringList st;
            for (const QString &k : *sec)
                st << SHORT_TITLE.value(k);
            tip = u"%1 — drag: move this section round the ring"_s.arg(st.join(u" + "_s));
        }
        setToolTip(tip);
        update();
    }
}

void Web::mouseReleaseEvent(QMouseEvent *e)
{
    if (_pan) {
        end_pan();
        return;
    }
    if (_secdrag) {
        const SecDrag s = *_secdrag;
        _secdrag.reset();
        setCursor(Qt::ArrowCursor);
        if (s.moved)                               // a section moved: its new place is kept for the mod
            Q_EMIT placed();
        update();
        return;
    }
    const std::optional<Drag> d = _drag;
    _drag.reset();
    setCursor(Qt::ArrowCursor);
    if (!d)
        return;
    if (d->moved) {                                // a flag moved: only its place in its section, never a tie
        if (const auto s = _section_of(d->tag)) {
            const QPointF mid(WORLD_W / 2.0, WORLD_H / 2.0);
            const QPointF pt = d->cur;
            const double a0 = std::get<1>(*s), a1 = std::get<2>(*s);
            const double an = a0 + py::mod(degrees(std::atan2(pt.y() - mid.y(), pt.x() - mid.x())) - a0, 360.0);
            custom.insert({a.value_or(QString()), d->tag},
                          Custom{std::get<0>(*s).join(u'+'), (an - a0) / std::max(1e-6, a1 - a0),
                                 py::hypot(pt.x() - mid.x(), pt.y() - mid.y())});
        }
        Q_EMIT placed();
        update();
        return;
    }
    if (mode == u"nation" && OptStr(d->tag) != a)  // one click: its ties, in the side column
        Q_EMIT menu(d->tag, e->globalPosition());
}

void Web::mouseDoubleClickEvent(QMouseEvent *e)
{
    const OptStr t = _at(e->position());
    if (t && e->button() == Qt::LeftButton)
        Q_EMIT picked(*t);
}

// ---------------------------------------------------------------------------------------------- GroupsView
GroupsView::GroupsView(FlagFn flag_of, NameFn name_of, QWidget *parent)
    : ZoomCanvas(parent), flag_of(std::move(flag_of)), name_of(std::move(name_of)), _world(0, 0, WORLD_W, WORLD_H)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
}

void GroupsView::set_sets(const std::vector<std::tuple<QString, QSet<QString>, Rgb>> &s)
{
    sets.clear();
    for (const auto &[t, tags, c] : s)
        if (!tags.isEmpty())
            sets.push_back({t, tags, c});
    _layout();
    _fitted = false;
    fit();
}

OrderedMap<long long, QStringList> GroupsView::regions() const
{
    // {mask: [tags]} — mask: bit i = in group i (the nations by name; Python sorts a set by name: one name twice
    // keeps the set's order there, here the tag decides)
    std::set<QString> all;
    for (const Set &s : sets)
        all.insert(s.tags.begin(), s.tags.end());
    std::vector<QString> tags(all.begin(), all.end());
    std::stable_sort(tags.begin(), tags.end(), [this](const QString &x, const QString &y) { return name_of(x) < name_of(y); });
    OrderedMap<long long, QStringList> out;
    for (const QString &t : tags) {
        long long m = 0;
        for (size_t i = 0; i < sets.size(); ++i)
            if (sets[i].tags.contains(t))
                m += 1LL << i;
        out[m].append(t);
    }
    return out;
}

QFont GroupsView::_label_font() const
{
    QFont f(font());
    f.setPixelSize(15);
    f.setBold(true);
    return f;
}

void GroupsView::_layout()
{
    // -> _boxes [(mask, rect, [tags], columns)], _rects [(rect, depth, label rect)] per group, the world's size,
    // misplaced. The blocks stand on euler_grid()'s grid (empty rows / columns dropped); a column is as wide as its
    // widest block, a row as high as its highest. A group's box = round its blocks, its margin growing with how many
    // boxes lie inside it (depth), so nested borders never touch and every name has a band of its own; the gaps
    // between columns / rows are made wide enough for the margins that end and begin there.
    const long long k = static_cast<long long>(sets.size());
    const OrderedMap<long long, QStringList> reg = regions();
    std::vector<long long> masks;
    for (const auto &[m, _t] : reg)
        masks.push_back(m);
    auto popcount = [](long long m) {
        int c = 0;
        for (unsigned long long x = static_cast<unsigned long long>(m); x; x >>= 1)
            c += int(x & 1);
        return c;
    };
    std::stable_sort(masks.begin(), masks.end(), [&](long long x, long long y) {
        const int px = popcount(x), py_ = popcount(y);
        if (px != py_)
            return px > py_;
        return x < y;
    });
    auto [cells, _viol] = euler_grid(masks, k);
    std::set<long long> cu, ru;
    for (const auto &[c, r] : cells) {
        cu.insert(c);
        ru.insert(r);
    }
    std::map<long long, long long> ci, ri;
    for (long long c : cu)
        ci[c] = static_cast<long long>(ci.size());
    for (long long r : ru)
        ri[r] = static_cast<long long>(ri.size());
    for (auto &[c, r] : cells) {
        c = ci[c];
        r = ri[r];
    }
    const size_t nc = cu.size(), nr = ru.size();
    struct Size {
        long long cols;
        double w, h;
    };
    std::map<long long, Size> size;
    for (long long m : masks) {
        const long long n = reg.get(m)->size();
        const long long cols = std::max(1LL, static_cast<long long>(std::ceil(std::sqrt(double(n) * 1.6))));
        size[m] = {cols, double(cols) * CW, double(static_cast<long long>(std::ceil(double(n) / double(cols)))) * CH};
    }
    std::vector<double> cw(nc, 0.0), rh(nr, 0.0);
    for (size_t b = 0; b < masks.size(); ++b) {
        const auto [c, r] = cells[b];
        cw[size_t(c)] = std::max(cw[size_t(c)], size[masks[b]].w);
        rh[size_t(r)] = std::max(rh[size_t(r)], size[masks[b]].h);
    }
    // each group's span of columns / rows, then its depth: above every box inside it, and apart from every box it
    // crosses (two crossing boxes never share a border line or a name band)
    using Span = std::array<long long, 4>;
    std::vector<Span> span;
    for (long long i = 0; i < k; ++i) {
        long long c0 = 0, c1 = 0, r0 = 0, r1 = 0;
        bool first = true;
        for (size_t b = 0; b < masks.size(); ++b)
            if (masks[b] >> i & 1) {
                const auto [c, r] = cells[b];
                if (first) {
                    c0 = c1 = c;
                    r0 = r1 = r;
                    first = false;
                } else {
                    c0 = std::min(c0, c);
                    c1 = std::max(c1, c);
                    r0 = std::min(r0, r);
                    r1 = std::max(r1, r);
                }
            }
        span.push_back({c0, c1, r0, r1});
    }
    auto inside = [](const Span &x, const Span &y) { return x[0] >= y[0] && x[1] <= y[1] && x[2] >= y[2] && x[3] <= y[3]; };
    auto cross = [](const Span &x, const Span &y) { return x[0] <= y[1] && y[0] <= x[1] && x[2] <= y[3] && y[2] <= x[3]; };
    std::vector<long long> order;
    for (long long i = 0; i < k; ++i)
        order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](long long x, long long y) {
        const long long ax = (span[size_t(x)][1] - span[size_t(x)][0] + 1) * (span[size_t(x)][3] - span[size_t(x)][2] + 1);
        const long long ay = (span[size_t(y)][1] - span[size_t(y)][0] + 1) * (span[size_t(y)][3] - span[size_t(y)][2] + 1);
        if (ax != ay)
            return ax < ay;
        const qsizetype lx = sets[size_t(x)].tags.size(), ly = sets[size_t(y)].tags.size();
        if (lx != ly)
            return lx < ly;
        return x < y;
    });
    OrderedMap<long long, long long> depth;
    for (long long i : order) {
        long long d = -1;
        for (const auto &[j, dj] : depth)
            if (inside(span[size_t(j)], span[size_t(i)]))
                d = std::max(d, dj);
        d += 1;
        for (;;) {
            bool clash = false;
            for (const auto &[j, dj] : depth)
                if (dj == d && cross(span[size_t(j)], span[size_t(i)]) && !inside(span[size_t(j)], span[size_t(i)]))
                    clash = true;
            if (!clash)
                break;
            ++d;
        }
        depth.insert(i, d);
    }
    std::vector<double> sidem(static_cast<size_t>(k)), band(static_cast<size_t>(k));
    for (long long i = 0; i < k; ++i) {
        sidem[size_t(i)] = 12.0 + 16.0 * double(*depth.get(i));
        band[size_t(i)] = 34.0 + 30.0 * double(*depth.get(i));   // top and bottom: room for the name
    }
    std::vector<double> x(nc, 0.0), y(nr, 0.0);
    for (size_t c = 1; c < nc; ++c) {
        double a = 0.0, b = 0.0;
        for (long long i = 0; i < k; ++i) {
            if (span[size_t(i)][1] == static_cast<long long>(c) - 1)
                a = std::max(a, sidem[size_t(i)]);
            if (span[size_t(i)][0] == static_cast<long long>(c))
                b = std::max(b, sidem[size_t(i)]);
        }
        x[c] = x[c - 1] + cw[c - 1] + a + b + 16.0;
    }
    for (size_t r = 1; r < nr; ++r) {
        double a = 0.0, b = 0.0;
        for (long long i = 0; i < k; ++i) {
            if (span[size_t(i)][3] == static_cast<long long>(r) - 1)
                a = std::max(a, band[size_t(i)]);
            if (span[size_t(i)][2] == static_cast<long long>(r))
                b = std::max(b, band[size_t(i)]);
        }
        y[r] = y[r - 1] + rh[r - 1] + a + b + 16.0;
    }
    _boxes.clear();
    for (size_t b = 0; b < masks.size(); ++b) {
        const auto [c, r] = cells[b];
        const Size &sz = size[masks[b]];
        _boxes.push_back({masks[b],
                          QRectF(x[size_t(c)] + (cw[size_t(c)] - sz.w) / 2.0, y[size_t(r)] + (rh[size_t(r)] - sz.h) / 2.0,
                                 sz.w, sz.h),
                          *reg.get(masks[b]), sz.cols});
    }
    const QFontMetrics fm(_label_font());
    std::vector<QRectF> rects(static_cast<size_t>(k));
    for (long long i = 0; i < k; ++i) {
        QRectF core;                               // round its own blocks (snug; still inside its columns / rows)
        for (const Box &bx : _boxes)
            if (bx.mask >> i & 1)
                core = core.united(bx.rect);
        rects[size_t(i)] = core.adjusted(-sidem[size_t(i)], -band[size_t(i)], sidem[size_t(i)], band[size_t(i)]);
    }
    _rects.assign(size_t(k), GRect{});
    std::vector<QRectF> labels;
    std::vector<QRectF> flags;
    for (const Box &bx : _boxes)
        flags.push_back(bx.rect.adjusted(-4, -6, 4, 4));
    std::vector<long long> by_depth = [&] {
        std::vector<long long> v;
        for (long long i = 0; i < k; ++i)
            v.push_back(i);
        std::stable_sort(v.begin(), v.end(), [&](long long p, long long q) { return -*depth.get(p) < -*depth.get(q); });
        return v;
    }();
    for (long long i : by_depth) {                 // outer boxes first: their names placed first
        const QRectF rect = rects[size_t(i)];
        const QString text = u"%1 (%2)"_s.arg(sets[size_t(i)].title).arg(sets[size_t(i)].tags.size());
        const double tw = std::min(double(fm.horizontalAdvance(text)) + 4.0, rect.width() - 24), th = 22.0;
        // inside its box, along the top band, then the bottom one: the first place that crosses no other box's
        // border, no other name and no flag
        const double room = std::max(0.0, rect.width() - 24 - tw);
        std::vector<QRectF> tries;
        for (double yy : {rect.top() + 6, rect.bottom() - 6 - th})
            for (double f : {0.0, 1.0, 0.5, 0.25, 0.75, 0.125, 0.375, 0.625, 0.875})
                tries.emplace_back(rect.left() + 12 + room * f, yy, tw, th);
        auto clean = [&](const QRectF &t) {
            for (const QRectF &o : labels)
                if (t.intersects(o))
                    return false;
            for (const QRectF &f : flags)
                if (t.intersects(f))
                    return false;
            for (long long j = 0; j < k; ++j)
                if (j != i && t.intersects(rects[size_t(j)]) && !rects[size_t(j)].contains(t))
                    return false;
            return true;
        };
        std::optional<QRectF> lab;
        for (const QRectF &t : tries)
            if (clean(t)) {
                lab = t;
                break;
            }
        const bool backed = !lab;                  // (no clean place: a dark backing keeps it readable)
        if (backed) {
            lab = tries[0];
            for (const QRectF &t : tries) {
                bool hit = false;
                for (const QRectF &o : labels)
                    if (t.intersects(o))
                        hit = true;
                if (!hit) {
                    lab = t;
                    break;
                }
            }
        }
        labels.push_back(*lab);
        _rects[size_t(i)] = GRect{rect, *depth.get(i), *lab, text, backed};
    }
    // what no rectangles could show: a flag inside the box of a group it is not in
    misplaced.clear();
    for (const Box &bx : _boxes)
        for (long long i = 0; i < k; ++i)
            if (!(bx.mask >> i & 1) && _rects[size_t(i)].rect.contains(bx.rect.center()))
                for (const QString &t : bx.tags)
                    misplaced.insert(t);
    QRectF allr;
    for (const GRect &g : _rects)
        allr = allr.united(g.rect);
    _world = !allr.isNull() ? allr.adjusted(-40, -40, 40, 40) : QRectF(0, 0, WORLD_W, WORLD_H);
}

void GroupsView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    begin(p);
    _hit.clear();
    if (sets.empty()) {
        p.resetTransform();
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, rect(), Qt::AlignCenter, u"Tick groups in the list on the left."_s);
        return;
    }
    std::vector<size_t> order;
    for (size_t i = 0; i < sets.size(); ++i)
        order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return -_rects[x].depth < -_rects[y].depth; });
    for (size_t i : order) {                       // the boxes, the outer ones first: a light fill each (darker where
        const QRectF rect = _rects[i].rect;        // they overlap), a strong border
        const QColor c = qc(sets[i].col);
        QColor fill(c);
        fill.setAlpha(34);
        p.setBrush(fill);
        p.setPen(QPen(c, 3.5));
        p.drawRoundedRect(rect, 18, 18);
    }
    p.setFont(_label_font());
    for (size_t i : order) {                       // their names, inside, in their colour
        const GRect &g = _rects[i];
        if (g.backed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(16, 18, 22, 225));
            p.drawRoundedRect(g.lab.adjusted(-6, -2, 6, 2), 6, 6);
        }
        p.setPen(qc(sets[i].col).lighter(125));
        drawtext::draw(p, g.lab, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(_label_font()).elidedText(g.text, Qt::ElideRight, int(g.lab.width())));
    }
    QFont sf(font());
    sf.setPixelSize(10);
    for (const Box &bx : _boxes) {                 // the flags of each block
        for (qsizetype j = 0; j < bx.tags.size(); ++j) {
            const QString &t = bx.tags[j];
            const double cx = bx.rect.left() + double(j % bx.cols) * 62.0 + 31.0,
                         cy = bx.rect.top() + double(j / bx.cols) * 50.0 + 4.0;
            const QRectF fr(cx - 20, cy, 40, 27);
            const std::optional<QImage> img = flag_of(t);
            if (img)
                p.drawImage(fr, *img);
            else
                p.fillRect(fr, QColor(90, 92, 100));
            p.setPen(QPen(QColor(10, 10, 10), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(fr);
            if (misplaced.contains(t)) {           // (inside a box it is not in: no rectangles could show it)
                QPen pen(QColor(theme::ERROR_), 3);
                pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                p.drawRoundedRect(fr.adjusted(-5, -5, 5, 5), 4, 4);
            }
            p.setFont(sf);
            p.setPen(QColor(theme::INK));
            drawtext::draw(p, QRectF(cx - 31, fr.bottom() + 1, 62, 14), Qt::AlignCenter,
                       QFontMetrics(sf).elidedText(name_of(t), Qt::ElideRight, 58));
            _hit.emplace_back(fr.adjusted(-4, -4, 4, 16), t);
        }
    }
}

OptStr GroupsView::_at(const QPointF &pos) const
{
    const QPointF w = to_world(pos);
    for (const auto &[r, t] : _hit)
        if (r.contains(w))
            return t;
    return std::nullopt;
}

void GroupsView::mousePressEvent(QMouseEvent *e)
{
    const Qt::MouseButton b = e->button();
    if ((b == Qt::LeftButton || b == Qt::RightButton || b == Qt::MiddleButton) &&
        !(b == Qt::LeftButton && _at(e->position())))
        start_pan(e->position());
}

void GroupsView::mouseMoveEvent(QMouseEvent *e)
{
    if (_pan) {
        pan_to(e->position());
        return;
    }
    const OptStr t = _at(e->position());
    setCursor(t ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    if (t) {
        QStringList in;
        for (const Set &s : sets)
            if (s.tags.contains(*t))
                in << s.title;
        setToolTip(u"%1 — in: %2 · double-click: show it"_s.arg(name_of(*t), in.join(u", "_s)));
    } else {
        setToolTip(u""_s);
    }
}

void GroupsView::mouseReleaseEvent(QMouseEvent *)
{
    if (_pan)
        end_pan();
}

void GroupsView::mouseDoubleClickEvent(QMouseEvent *e)
{
    const OptStr t = _at(e->position());
    if (t && e->button() == Qt::LeftButton)
        Q_EMIT picked(*t);
}

// ---------------------------------------------------------------------------------------------- RelChart
RelChart::RelChart(NameFn name_of, QWidget *parent) : QWidget(parent), name_of(std::move(name_of))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RelChart::set_rows(const std::vector<std::pair<QString, long long>> &r)
{
    rows = r;
    setFixedHeight(std::max(40, 26 * int(rows.size()) + 12));
    update();
}

void RelChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = width();
    const int name_w = 130;
    const double x0 = name_w + (W - name_w - 50) / 2.0;
    const double half = (W - name_w - 50) / 2.0;
    p.setPen(QPen(QColor(theme::INK_FAINT), 2));
    p.drawLine(QPointF(x0, 4), QPointF(x0, height() - 4));
    QFont f(font());
    f.setPixelSize(12);
    p.setFont(f);
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &[t, v] = rows[i];
        const int y = 8 + int(i) * 26;
        p.setPen(QColor(theme::INK));
        drawtext::draw(p, QRectF(0, y, name_w - 6, 20), Qt::AlignVCenter | Qt::AlignRight,
                   p.fontMetrics().elidedText(name_of(t), Qt::ElideRight, name_w - 8));
        const double w = half * double(std::abs(v)) / 200.0;
        const QColor col = v > 0 ? QColor(64, 190, 88) : QColor(214, 58, 48);
        const QRectF r = v > 0 ? QRectF(x0, y + 2, w, 16) : QRectF(x0 - w, y + 2, w, 16);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(col);
        p.drawRect(r);
        p.setPen(QColor(theme::INK));
        drawtext::draw(p, QRectF(v > 0 ? r.right() + 4 : x0 + 4, y, 44, 20),   // (a negative: right of the axis, clear of
                   Qt::AlignVCenter | Qt::AlignLeft, plus(v));       //  the names)
    }
}

// ---------------------------------------------------------------------------------------------- DiploWebView
namespace {

oob::OOBEdits *oob_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"oob"_s) ? &doc->map<oob::OOBEdits>(u"oob"_s) : nullptr;
}

diplomacy::DiploEdits *dip_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"diplomacy"_s) ? &doc->map<diplomacy::DiploEdits>(u"diplomacy"_s) : nullptr;
}

countryhist::NationEdits *nat_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"nations"_s) ? &doc->map<countryhist::NationEdits>(u"nations"_s) : nullptr;
}

QScrollArea *scroller(QWidget *w)
{
    auto *s = new QScrollArea();
    s->setWidgetResizable(true);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setObjectName(u"sideScroll"_s);
    s->setWidget(w);
    return s;
}

std::optional<double> num(const pyjson::Value &v)  // a JSON number
{
    if (const auto *i = std::get_if<long long>(&v.v))
        return double(*i);
    if (const auto *d = std::get_if<double>(&v.v))
        return *d;
    return std::nullopt;
}

}  // namespace

DiploWebView::DiploWebView(moddata::Mod &mod, QWidget *parent) : QWidget(parent), mod(mod)
{
    setObjectName(u"diploWeb"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(u"QWidget#diploWeb { background: %1; }"_s.arg(theme::BG));
    _rel_timer = new QTimer(this);
    _rel_timer->setSingleShot(true);
    _rel_timer->setInterval(450);
    connect(_rel_timer, &QTimer::timeout, this, [this] { _rel_done(); });
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    _outer = new QSplitter(Qt::Horizontal);        // the view (with its controls) | the side column, top to bottom
    _outer->setChildrenCollapsible(false);
    outer->addWidget(_outer);
    auto *leftw = new QWidget();
    auto *lay = new QVBoxLayout(leftw);
    lay->setContentsMargins(10, 8, 6, 8);
    auto *row = new QHBoxLayout();
    row->setSpacing(0);
    for (const auto &[k, text] : {std::pair{u"nation"_s, u"Relations of the nation"_s},
                                  std::pair{u"gps"_s, u"The great powers"_s}, std::pair{u"groups"_s, u"Groups"_s}}) {
        auto *bt = new QPushButton(text);
        bt->setObjectName(u"seg"_s);
        bt->setCheckable(true);
        bt->setChecked(k == mode);
        connect(bt, &QPushButton::clicked, this, [this, kk = k] { set_mode(kk); });
        row->addWidget(bt);
        seg.emplace_back(k, bt);
    }
    row->addSpacing(14);
    title = label(u""_s, u"label"_s);              // (wraps: a long line never widens the page)
    title->setTextFormat(Qt::RichText);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(title, 1);
    auto *fitb = new QPushButton(u"Fit"_s);
    fitb->setToolTip(u"Show all of it (drag the empty view to move it, wheel to zoom)"_s);
    connect(fitb, &QPushButton::clicked, this, [this] {
        if (auto *z = qobject_cast<ZoomCanvas *>(stack->currentWidget()))
            z->fit();
    });
    row->addWidget(fitb);
    lay->addLayout(row);
    // --- the relations' controls: sort, groups shown (like the production view's)
    ctl = new QWidget();
    auto *cl = new QVBoxLayout(ctl);
    cl->setContentsMargins(0, 4, 0, 0);
    auto *r2 = new QHBoxLayout();
    r2->addWidget(label(u"Order in each group"_s, u"sub"_s, false));
    sort_box = new QComboBox();
    for (const auto &[k, t] : SORTS)
        sort_box->addItem(t, k);
    connect(sort_box, &QComboBox::activated, this, [this](int) { set_sort(sort_box->currentData().toString()); });
    r2->addWidget(sort_box);
    auto *bt = new QPushButton(u"Lay out anew"_s);
    bt->setToolTip(u"Every flag back in its place in the order picked, the sections in their first order (the places "
                   u"dragged are forgotten)"_s);
    connect(bt, &QPushButton::clicked, this, [this] { reset_places(true); });
    r2->addWidget(bt);
    r2->addSpacing(10);
    r2->addWidget(label(u"Click a flag: its ties on the right · double-click: show that nation · drag a flag: move it "
                        u"in its section · drag a section: move it round · drag outside the ring: move the view"_s,
                        u"faint"_s, false),
                  1);
    cl->addLayout(r2);
    auto *host = new QWidget();
    chips = new widgets::WrapRow(host, 4);
    for (const GroupDef &g : GROUPS) {
        auto *c = new QPushButton(g.title);
        c->setObjectName(u"chip"_s);
        c->setCheckable(true);
        c->setChecked(SHOWN_AT_FIRST.contains(g.key));
        c->setStyleSheet(u"QPushButton:checked { border: 2px solid rgb(%1,%2,%3); }"_s.arg(std::get<0>(g.rgb))
                             .arg(std::get<1>(g.rgb))
                             .arg(std::get<2>(g.rgb)));
        connect(c, &QPushButton::toggled, this, [this, kk = g.key](bool on) { show_group(kk, on); });
        chips->addWidget(c);
        chip_btn[g.key] = c;
    }
    cl->addWidget(host);
    lay->addWidget(ctl);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    // --- the groups' list (Groups only)
    glist_box = new QWidget();
    auto *gl = new QVBoxLayout(glist_box);
    gl->setContentsMargins(0, 0, 6, 0);
    gsearch = new QLineEdit();
    gsearch->setPlaceholderText(u"Find a group…"_s);
    gsearch->setClearButtonEnabled(true);
    connect(gsearch, &QLineEdit::textChanged, this, [this] { _filter_groups(); });
    gl->addWidget(gsearch);
    glist = new QListWidget();
    glist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    glist->setWordWrap(true);
    connect(glist, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) { _group_ticked(it); });
    gl->addWidget(glist, 1);
    bt = new QPushButton(u"Tick none"_s);
    connect(bt, &QPushButton::clicked, this, [this] { _tick_none(); });
    gl->addWidget(bt);
    glist_box->hide();
    split->addWidget(glist_box);
    stack = new QStackedWidget();
    FlagFn fl = [this](const QString &t) { return flag(t); };
    NameFn nm = [this](const QString &t) { return name(t); };
    web = new Web(fl, nm);
    connect(web, &Web::picked, this, [this](const QString &t) { _picked(t); });
    connect(web, &Web::menu, this, [this](const QString &t, const QPointF &) { pick(t); });
    connect(web, &Web::placed, this, [this] { _save_places(); });
    groups_view = new GroupsView(fl, nm);
    connect(groups_view, &GroupsView::picked, this, [this](const QString &t) { _picked(t); });
    stack->addWidget(web);
    stack->addWidget(groups_view);
    split->addWidget(stack);
    split->setSizes({260, 900});
    lay->addWidget(split, 1);
    _outer->addWidget(leftw);
    // the side column, two parts one above the other: the nation in view (its flag above its ties), and apart from
    // it, framed in gold, the nation clicked in the web (its ties with the nation in view, to change)
    side = new QSplitter(Qt::Vertical);
    side->setChildrenCollapsible(false);
    auto *inner = new QWidget();
    inner->setObjectName(u"sideInner"_s);
    auto *il = new QVBoxLayout(inner);
    il->setContentsMargins(10, 10, 10, 6);
    view_box = new widgets::Section();
    QVBoxLayout *vl = view_box->lay;
    view_head = label(u"The nation in view"_s, u"sub"_s);
    vl->addWidget(view_head);
    auto *vtop = new QHBoxLayout();
    view_flag = new QLabel();
    view_flag->setFixedSize(96, 64);
    vtop->addWidget(view_flag);
    view_name = new QLabel(u""_s);
    view_name->setWordWrap(true);
    view_name->setTextFormat(Qt::RichText);
    view_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vtop->addWidget(view_name, 1);
    vl->addLayout(vtop);
    il->addWidget(view_box);
    // --- the nation clicked: its flag, its ties with the nation in view (tick as many as wanted), relations
    pick_box = new QFrame();
    pick_box->setObjectName(u"pickBox"_s);
    pick_box->setStyleSheet(u"QFrame#pickBox { background: %1; border: 2px solid %2; border-radius: 8px; }"_s.arg(
        theme::PANEL_ALT, theme::GOLD));
    auto *pl = new QVBoxLayout(pick_box);
    pl->setContentsMargins(14, 10, 14, 10);
    pl->setSpacing(6);
    pick_head = label(u""_s, u"sub"_s);
    pick_head->setStyleSheet(u"color: %1; font-weight: 700; border: none;"_s.arg(theme::GOLD));
    pick_head->setWordWrap(true);
    pl->addWidget(pick_head);
    auto *top = new QHBoxLayout();
    pick_flag = new QLabel();
    pick_flag->setFixedSize(96, 64);
    top->addWidget(pick_flag);
    pick_name = new QLabel(u""_s);
    pick_name->setWordWrap(true);
    pick_name->setTextFormat(Qt::RichText);
    top->addWidget(pick_name, 1);
    auto *close = new QPushButton(u"✕"_s);
    close->setFixedWidth(30);
    close->setToolTip(u"Close this part"_s);
    connect(close, &QPushButton::clicked, this, [this] { pick(std::nullopt); });
    top->addWidget(close, 0, Qt::AlignTop);
    pl->addLayout(top);
    pick_status = label(u""_s, u"sub"_s);
    pl->addWidget(pick_status);
    // the ties in parts with room between them (the user's order, 2026-10-27): allied; the clicked nation under the
    // nation in view (its puppet, in its sphere); the other way round; guarantees (only when the mod has one)
    for (const auto &[part, keys] :
         {std::pair{u"ally"_s, QStringList{u"ally"_s}}, std::pair{u"under"_s, QStringList{u"subject"_s, u"sphere"_s}},
          std::pair{u"over"_s, QStringList{u"overlord"_s, u"lord"_s}},
          std::pair{u"guar"_s, QStringList{u"guarantee"_s, u"guaranteed_by"_s}}}) {
        pl->addSpacing(10);
        QLabel *h = label(u""_s, u"sub"_s);
        h->setStyleSheet(u"font-weight: 700; border: none;"_s);
        pl->addWidget(h);
        tie_head[part] = h;
        for (const QString &k : keys) {
            auto *cb = new QCheckBox(u""_s);
            connect(cb, &QCheckBox::toggled, this, [this, kk = k](bool on) { _tie_ticked(kk, on); });
            pl->addWidget(cb);
            tie_box[k] = cb;
        }
    }
    pl->addSpacing(10);
    pick_war = label(u""_s, u"faint"_s);
    pl->addWidget(pick_war);
    pl->addWidget(label(u"Relations"_s, u"sub"_s));
    pick_rel = new widgets::SliderField(-200, 200, 200, 0);
    connect(pick_rel, &widgets::SliderField::valueChanged, this, [this](double) {
        if (!_filling)
            _rel_timer->start();
    });
    pl->addWidget(pick_rel);
    auto *brow = new QHBoxLayout();
    bt = new QPushButton(u"Show it (double-click)"_s);
    connect(bt, &QPushButton::clicked, this, [this] {
        if (b && !b->isEmpty())
            _picked(*b);
    });
    brow->addWidget(bt);
    bt = new QPushButton(u"Take every tie away"_s);
    connect(bt, &QPushButton::clicked, this, [this] {
        if (b && !b->isEmpty())
            _tie(*b, u"none"_s, false);
    });
    brow->addWidget(bt);
    brow->addStretch(1);
    pl->addLayout(brow);
    _filling = false;
    lists = new QLabel(u""_s);
    lists->setWordWrap(true);
    lists->setTextFormat(Qt::RichText);
    lists->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vl->addWidget(lists);
    rel_head = label(u"Relations: the best and the worst"_s, u"sub"_s);
    vl->addWidget(rel_head);
    chart = new RelChart(nm);
    vl->addWidget(chart);
    il->addStretch(1);
    side->addWidget(scroller(inner));
    auto *pw = new QWidget();
    pw->setObjectName(u"sideInner"_s);
    auto *pwl = new QVBoxLayout(pw);
    pwl->setContentsMargins(10, 6, 10, 10);
    pwl->addWidget(pick_box);
    pwl->addStretch(1);
    pick_sc = scroller(pw);
    pick_sc->hide();
    side->addWidget(pick_sc);
    side->setSizes({440, 540});
    side->setMinimumWidth(400);
    _outer->addWidget(side);
    _outer->setSizes({1100, 380});
    _load_places();
}

QPixmap DiploWebView::_pixmap(const std::optional<QImage> &img) const
{
    return img ? QPixmap::fromImage(*img).scaled(96, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation) : QPixmap();
}

// ------------------------------------------------------------------ data
QString DiploWebView::name(const QString &t) const
{
    const moddata::Country *c = mod.countries.get(t);
    return c ? c->name : t;
}

std::optional<QImage> DiploWebView::flag(const QString &t)
{
    // the flag the nation flies at the scenario's start (its government's), else its plain one
    if (const auto it = _flags.find(t); it != _flags.end())
        return it->second;
    std::optional<QImage> img;
    try {                                          // (the game's .tga as the 4K maker reads it: any kind of tga)
        if (!_gov_flags)
            _gov_flags = nationextras::government_flags(doc);
        const OptStr path = nationextras::start_flag(mod, doc, t, &*_gov_flags);
        if (path)
            img = fourk::to_qimage(fourk::read_picture(*path).first);
    } catch (...) {
        img.reset();
    }
    _flags[t] = img;
    return img;
}

const std::map<QString, long long> &DiploWebView::owners()
{
    // {nation: provinces it owns at the start}
    if (!_sizes) {
        _sizes.emplace();
        for (const auto &[pid, pr] : mod.provinces)
            if (!pr.sea) {
                const OptStr &o = doc->prov_state(pid).owner;
                if (o && !o->isEmpty())
                    (*_sizes)[*o] += 1;
            }
    }
    return *_sizes;
}

// ------------------------------------------------------------------ places dragged (kept per mod)
QString DiploWebView::_key() const
{
    const QString n = mod.name();
    return u"diplomacy/places/%1"_s.arg(n.isEmpty() ? u"mod"_s : n);
}

void DiploWebView::_load_places()
{
    // the flags moved (per nation in view: section, part of its angle, radius — older free places are dropped) and
    // the sections' order round the ring
    const auto st = settings_::settings();
    web->custom = {};
    {
        QString text = st->value(_key(), u"{}"_s).toString();
        if (text.isEmpty())
            text = u"{}"_s;
        const std::optional<pyjson::Value> raw = pyjson::loads(text);
        if (raw && raw->is_obj()) {
            bool bad = false;
            OrderedMap<std::pair<QString, QString>, Web::Custom> got;
            for (const auto &[k, v] : raw->obj()) {
                if (!v.is_arr()) {                 // (Python: len() of it raises — nothing is kept)
                    bad = true;
                    break;
                }
                if (v.arr().size() != 3)
                    continue;
                const qsizetype bar = k.indexOf(u'|');
                const std::optional<double> part = num(v.arr()[1]), radius = num(v.arr()[2]);
                if (bar < 0 || !part || !radius)
                    continue;
                got.insert({k.left(bar), k.mid(bar + 1)}, Web::Custom{v.arr()[0].str(), *part, *radius});
            }
            if (!bad)
                web->custom = got;
        }
    }
    web->sec_order.clear();
    QString text = st->value(_key() + u"/sections"_s, u"[]"_s).toString();
    if (text.isEmpty())
        text = u"[]"_s;
    const std::optional<pyjson::Value> raw = pyjson::loads(text);
    if (raw && raw->is_arr())
        for (const pyjson::Value &s : raw->arr())
            web->sec_order.push_back(s.str().split(u'+'));
}

void DiploWebView::_save_places()
{
    const auto st = settings_::settings();
    pyjson::Object o;
    for (const auto &[k, v] : web->custom)
        o.insert(u"%1|%2"_s.arg(k.first, k.second), pyjson::Value(pyjson::Array{pyjson::Value(v.section),
                                                                              pyjson::Value(v.part),
                                                                              pyjson::Value(v.radius)}));
    st->setValue(_key(), pyjson::dumps(pyjson::Value(std::move(o))));
    pyjson::Array arr;
    for (const SecKey &k : web->sec_order)
        arr.emplace_back(k.join(u'+'));
    st->setValue(_key() + u"/sections"_s, pyjson::dumps(pyjson::Value(std::move(arr))));
}

void DiploWebView::reset_places(bool sections)
{
    OrderedMap<std::pair<QString, QString>, Web::Custom> kept;
    for (const auto &[k, v] : web->custom)
        if (OptStr(k.first) != a)
            kept.insert(k, v);
    web->custom = kept;
    if (sections)
        web->sec_order.clear();
    _save_places();
    web->update();
}

// ------------------------------------------------------------------ switches
void DiploWebView::set_mode(const QString &k)
{
    mode = k;
    for (const auto &[kk, bt] : seg)
        bt->setChecked(kk == k);
    ctl->setVisible(k == u"nation");
    glist_box->setVisible(k == u"groups");
    side->setVisible(k != u"groups");
    pick_sc->setVisible(k == u"nation" && a && !a->isEmpty() && b && !b->isEmpty());
    stack->setCurrentWidget(k == u"groups" ? static_cast<QWidget *>(groups_view) : web);
    show_for(doc, a, wars_fn);
}

void DiploWebView::set_sort(const QString &k)
{
    sort = k;
    const int i = sort_box->findData(k);
    if (i >= 0)
        sort_box->setCurrentIndex(i);
    oob::OOBEdits *oob = oob_of(doc);
    const QString av = a.value_or(QString());
    if (k == u"name") {
        web->order = [this](const QStringList &tags) {
            QStringList out = tags;
            std::stable_sort(out.begin(), out.end(),
                             [this](const QString &x, const QString &y) { return name(x).toLower() < name(y).toLower(); });
            return out;
        };
    } else if (k == u"rel" && oob) {
        web->order = [oob, av](const QStringList &tags) {
            QStringList out = tags;
            std::stable_sort(out.begin(), out.end(), [&](const QString &x, const QString &y) {
                return -diplomacy::relation_value(*oob, av, x) < -diplomacy::relation_value(*oob, av, y);
            });
            return out;
        };
    } else if (k == u"size") {
        web->order = [this](const QStringList &tags) {
            QStringList out = tags;
            const std::map<QString, long long> &o = owners();
            auto sz = [&o](const QString &t) {
                const auto it = o.find(t);
                return it != o.end() ? it->second : 0LL;
            };
            std::stable_sort(out.begin(), out.end(), [&](const QString &x, const QString &y) { return -sz(x) < -sz(y); });
            return out;
        };
    } else {
        web->order = [](const QStringList &tags) { return tags; };
    }
    reset_places();
}

void DiploWebView::show_group(const QString &k, bool on)
{
    if (on)
        web->shown.insert(k);
    else
        web->shown.remove(k);
    if ((k == u"friendly" || k == u"hostile") && on)
        show_for(doc, a, wars_fn);
    if (!web->_moved)                              // (the pie grows / shrinks with what is shown)
        web->fit();
    web->update();
}

void DiploWebView::_picked(const QString &t)
{
    a = t;
    b.reset();
    Q_EMIT picked(t);
    if (mode != u"nation")                         // (a double-click in the great powers or the groups: that nation's
        set_mode(u"nation"_s);                     //  relations)
    else
        show_for(doc, t, wars_fn);
}

// ------------------------------------------------------------------ the nation clicked: its ties in the side column
void DiploWebView::pick(const OptStr &nb)
{
    // show the nation b (clicked in the web) at the top of the side column — None: close that part
    b = nb && !nb->isEmpty() && nb != a ? nb : std::nullopt;
    _fill_pick();
}

void DiploWebView::_fill_pick()
{
    const bool both = a && !a->isEmpty() && b && !b->isEmpty();
    pick_sc->setVisible(both && mode == u"nation");
    if (!both || !doc)
        return;
    const QString A = *a, B = *b;
    oob::OOBEdits *oob = oob_of(doc);
    diplomacy::DiploEdits *dip = dip_of(doc);
    countryhist::NationEdits *nat = nat_of(doc);
    const QStringList gps = nat ? diplomacy::great_powers(mod, *nat) : QStringList();
    const Groups &g = web->groups;
    auto in = [&g](const QString &k, const QString &t) {
        const QStringList *l = g.get(k);
        return l && l->contains(t);
    };
    pick_head->setText(u"The nation you clicked — its ties with %1, to change"_s.arg(name(A)));
    pick_flag->setPixmap(_pixmap(flag(B)));
    pick_name->setText(u"<span style='font-size:16px; font-weight:700'>%1</span> (%2)%3"_s.arg(
        name(B), B, gps.contains(B) ? u" · a great power"_s : u""_s));
    QStringList have;
    for (const QString &k : PRIORITY)
        if (in(k, B) && k != u"friendly" && k != u"hostile")
            have << GROUP_TITLE.value(k);
    pick_status->setText(u"Now: "_s + (!have.isEmpty() ? have.join(u", "_s) : u"no tie"_s));
    const bool guar = has_guarantees(dip) || in(u"guarantee"_s, B) || in(u"guaranteed_by"_s, B);
    const std::map<QString, std::pair<QString, bool>> texts = {
        {u"ally"_s, {u"Allied"_s, true}},
        {u"subject"_s, {u"%1 is a puppet of %2"_s.arg(name(B), name(A)), true}},
        {u"overlord"_s, {u"%1 is a puppet of %2"_s.arg(name(A), name(B)), true}},
        {u"sphere"_s, {u"%1 is in %2's sphere"_s.arg(name(B), name(A)), gps.contains(A)}},
        {u"lord"_s, {u"%1 is in %2's sphere"_s.arg(name(A), name(B)), gps.contains(B)}},
        {u"guarantee"_s, {u"%1 guarantees %2"_s.arg(name(A), name(B)), guar}},
        {u"guaranteed_by"_s, {u"%1 guarantees %2"_s.arg(name(B), name(A)), guar}}};
    _filling = true;
    for (const auto &[k, cb] : tie_box) {
        const auto &[text, can] = texts.at(k);
        const bool on = in(k, B);
        cb->setText(text);
        cb->setChecked(on);
        cb->setEnabled(can || on);
        cb->setToolTip(can || on ? u""_s : u"Only a great power has a sphere"_s);
        cb->setVisible((k != u"guarantee" && k != u"guaranteed_by") || guar);
    }
    const QString nb = name(B), na = name(A);
    tie_head[u"ally"_s]->setText(u"Alliance"_s);
    tie_head[u"under"_s]->setText(u"%1 under %2"_s.arg(nb, na));
    tie_head[u"over"_s]->setText(u"%1 under %2"_s.arg(na, nb));
    tie_head[u"guar"_s]->setText(u"Guarantees"_s);
    tie_head[u"guar"_s]->setVisible(guar);
    pick_rel->setValue(oob ? double(diplomacy::relation_value(*oob, A, B)) : 0.0);
    _filling = false;
    const bool war = in(u"war"_s, B);
    pick_war->setText(war ? u"At war at the start — the wars: Warfare → Wars at the start"_s : u""_s);
    pick_war->setVisible(war);
}

void DiploWebView::_tie_ticked(const QString &kind, bool on)
{
    if (_filling || !(a && !a->isEmpty() && b && !b->isEmpty()))
        return;
    _tie(*b, kind, on);
}

void DiploWebView::_rel_done()
{
    if (!(a && !a->isEmpty() && b && !b->isEmpty()))
        return;
    const QString A = *a, B = *b;
    const long long v = static_cast<long long>(pick_rel->value());
    Q_EMIT edit(u"%1 ↔ %2: relations %3"_s.arg(A, B, plus(v)),
                [A, B, v](oob::OOBEdits &oob, diplomacy::DiploEdits &) { diplopanel::set_relation(oob, A, B, v); });
}

void DiploWebView::_tie(const QString &bt, const QString &kind, bool on)
{
    const auto [lab, fn] = tie_set(a.value_or(QString()), bt, kind, on);
    Q_EMIT edit(lab, fn);
}

// ------------------------------------------------------------------ filling
void DiploWebView::show_for(document::Document *d, const OptStr &na, std::function<std::vector<diplopanel::WarRow>()> wf)
{
    // doc: the editor's document; a: the main nation (or None); wars_fn() -> the wars at the start
    doc = d;
    a = na;
    wars_fn = std::move(wf);
    if (!doc)
        return;
    _gov_flags.reset();                            // (a government / owner changed: they follow)
    _flags.clear();
    _sizes.reset();
    oob::OOBEdits *oob = oob_of(doc);
    diplomacy::DiploEdits *dip = dip_of(doc);
    countryhist::NationEdits *nat = nat_of(doc);
    const QStringList gps = nat ? diplomacy::great_powers(mod, *nat) : QStringList();
    const std::vector<diplopanel::WarRow> wars = wars_fn ? wars_fn() : std::vector<diplopanel::WarRow>();
    const bool guar = has_guarantees(dip);
    for (const QString k : {u"guarantee"_s, u"guaranteed_by"_s})   // an old part of the engine: only when the mod has one
        chip_btn[k]->setVisible(guar);
    const bool has_a = a && !a->isEmpty();
    rel_head->setVisible(mode == u"nation" && has_a);
    const bool on = mode == u"nation" && has_a;    // the nation in view: its flag above its ties
    view_head->setVisible(on);
    view_flag->setVisible(on);
    view_name->setVisible(on);
    if (on) {
        view_flag->setPixmap(_pixmap(flag(*a)));
        view_name->setText(u"<span style='font-size:18px; font-weight:700'>%1</span> (%2)%3"_s.arg(
            name(*a), *a, gps.contains(*a) ? u" · a great power"_s : u""_s));
    }
    if (mode == u"groups") {
        _fill_groups(doc, gps, wars);
        return;
    }
    if (mode == u"gps") {
        std::vector<Web::GpRow> rows;
        std::set<std::pair<QString, QString>> gwars;
        for (const QString &g : gps) {
            QStringList sph;
            if (oob) {
                for (const auto &[t, r] : oob->state(g).relations)
                    if (level5(r) && mod.countries.contains(t))
                        sph << t;
                std::stable_sort(sph.begin(), sph.end(), [this](const QString &x, const QString &y) { return name(x) < name(y); });
            }
            QStringList al;
            const Groups gw = web_of(g, mod, oob, dip, wars, gps);   // (kept: the loop reads inside it)
            for (const QString &x : *gw.get(u"ally"_s))
                if (gps.contains(x))
                    al << x;
            rows.push_back({g, sph, al});
        }
        for (const diplopanel::WarRow &w : wars)
            for (const QString &x : w.att)
                for (const QString &y : w.def)
                    if (gps.contains(x) && gps.contains(y))
                        gwars.insert(x < y ? std::pair{x, y} : std::pair{y, x});
        web->set_gps(rows, std::vector<std::pair<QString, QString>>(gwars.begin(), gwars.end()));
        title->setText(u"The great powers: their spheres (teal), alliances (blue), wars (red)"_s);
        QStringList ls;
        for (const Web::GpRow &r : rows) {
            QString allied;
            if (!r.al.isEmpty()) {
                QStringList ns;
                for (const QString &x : r.al)
                    ns << name(x);
                allied = u" · allied with "_s + ns.join(u", "_s);
            }
            ls << u"<b>%1</b>: %2 in its sphere%3"_s.arg(name(r.g)).arg(r.sph.size()).arg(allied);
        }
        lists->setText(ls.join(u"<br>"_s));
        chart->set_rows({});
        return;
    }
    if (!has_a) {
        web->set_nation(std::nullopt, {});
        title->setText(u""_s);
        lists->setText(u""_s);
        chart->set_rows({});
        return;
    }
    const QString A = *a;
    QStringList related_l;
    if (related) {
        related_l = related(A);
    } else if (oob) {
        for (const auto &[t, _r] : oob->state(A).relations)
            related_l << t;
    }
    Groups groups = web_of(A, mod, oob, dip, wars, gps, related_l);
    if (!guar) {
        groups[u"guarantee"_s] = {};
        groups[u"guaranteed_by"_s] = {};
    }
    web->set_nation(A, groups);
    title->setText(u"%1 (%2)%3"_s.arg(name(A), A, gps.contains(A) ? u" · a great power"_s : u""_s));
    QStringList parts;
    for (const GroupDef &gd : GROUPS) {
        const QStringList *tp = groups.get(gd.key);
        const QStringList ts = tp ? *tp : QStringList();
        if ((gd.key == u"friendly" || gd.key == u"hostile") && !web->shown.contains(gd.key))
            continue;
        if ((gd.key == u"guarantee" || gd.key == u"guaranteed_by") && !guar)
            continue;
        QString body;
        if (!ts.isEmpty()) {
            QStringList ns;
            for (const QString &t : ts)
                ns << name(t);
            body = ns.join(u", "_s);
        } else {
            body = u"<span style='color:%1'>none</span>"_s.arg(theme::INK_FAINT);
        }
        parts << u"<span style='color:rgb%1; font-weight:700'>%2 (%3)</span><br>%4"_s.arg(rgb_str(gd.rgb), gd.title)
                     .arg(ts.size())
                     .arg(body);
    }
    lists->setText(parts.join(u"<br><br>"_s));
    if (oob) {
        std::vector<std::pair<long long, QString>> vals;
        const std::set<QString> uniq(related_l.begin(), related_l.end());
        for (const QString &t : uniq)
            if (mod.countries.contains(t) && t != A)
                vals.emplace_back(diplomacy::relation_value(*oob, A, t), t);
        std::sort(vals.begin(), vals.end(), std::greater<>());
        std::vector<std::pair<QString, long long>> rows;
        for (const auto &[v, t] : vals)
            if (v > 0 && rows.size() < 6)
                rows.emplace_back(t, v);
        size_t worst = 0;
        for (auto it = vals.rbegin(); it != vals.rend(); ++it)
            if (it->first < 0 && worst < 6) {
                rows.emplace_back(it->second, it->first);
                ++worst;
            }
        chart->set_rows(rows);
    }
    if (b == a)
        b.reset();
    _fill_pick();                                  // (after an edit: the ticks show what is now)
}

// ------------------------------------------------------------------ groups
std::vector<DiploWebView::GSet> DiploWebView::sets_of(document::Document *d, const QStringList &gps,
                                                      const std::vector<diplopanel::WarRow> &wars)
{
    // [(heading, key, title, set of tags)] — every group there is: the great powers, every sphere, every alliance
    // network, every nation's subjects, both sides of every war, the continents, culture groups and religions
    oob::OOBEdits *oob = oob_of(d);
    diplomacy::DiploEdits *dip = dip_of(d);
    countryhist::NationEdits *nat = nat_of(d);
    std::set<QString> alive;
    for (const auto &[t, _n] : owners())
        alive.insert(t);
    auto qset = [](const QStringList &l) { return QSet<QString>(l.begin(), l.end()); };
    std::vector<GSet> out;
    out.push_back({u"Great powers"_s, u"gps"_s, u"The great powers"_s, qset(gps)});
    for (const QString &g : gps) {
        QSet<QString> sph;
        if (oob)
            for (const auto &[t, r] : oob->state(g).relations)
                if (level5(r))
                    sph.insert(t);
        if (!sph.isEmpty()) {
            sph.insert(g);
            out.push_back({u"Spheres"_s, u"sphere:"_s + g, u"%1's sphere"_s.arg(name(g)), sph});
        }
    }
    OrderedMap<QString, QSet<QString>> allies, subjects;
    if (dip)
        for (const diplomacy::Record &r : dip->at_start()) {
            if (r.kind == u"alliance") {
                allies[r.first].insert(r.second);
                allies[r.second].insert(r.first);
            } else if (r.kind == u"vassal" || r.kind == u"substate" || r.kind == u"union") {
                subjects[r.first].insert(r.second);
            }
        }
    auto by_size = [this](const OrderedMap<QString, QSet<QString>> &m) {
        QStringList keys;
        for (const auto &[t, _s] : m)
            keys << t;
        std::stable_sort(keys.begin(), keys.end(), [&](const QString &x, const QString &y) {
            const qsizetype lx = m.get(x)->size(), ly = m.get(y)->size();
            if (lx != ly)
                return lx > ly;
            return name(x) < name(y);
        });
        return keys;
    };
    for (const QString &t : by_size(allies)) {
        QSet<QString> s = *allies.get(t);
        s.insert(t);
        out.push_back({u"Alliances"_s, u"allies:"_s + t, u"%1 and its allies"_s.arg(name(t)), s});
    }
    for (const QString &t : by_size(subjects)) {
        QSet<QString> s = *subjects.get(t);
        s.insert(t);
        out.push_back({u"Subjects"_s, u"subjects:"_s + t, u"%1 and its subjects"_s.arg(name(t)), s});
    }
    for (const diplopanel::WarRow &w : wars) {
        out.push_back({u"Wars"_s, u"att:"_s + w.rel, u"%1 — attackers"_s.arg(w.name), qset(w.att)});
        out.push_back({u"Wars"_s, u"def:"_s + w.rel, u"%1 — defenders"_s.arg(w.name), qset(w.def)});
    }
    OrderedMap<QString, QSet<QString>> cont, cgroup, rel;
    OrderedMap<long long, QStringList> memb;
    try {
        if (d->maps.contains(u"continent"_s))
            memb = d->map<mapfiles::MembershipFile>(u"continent"_s).membership();
    } catch (...) {
        memb = {};
    }
    for (const QString &t : alive) {
        const countryhist::NationState st = nat ? nat->state(t) : countryhist::NationState{};
        long long cap = 0;
        try {
            const OptStr c = st.at(u"capital"_s);
            cap = py::to_int(py::strip(c && !c->isEmpty() ? *c : u"0"_s));
        } catch (...) {
            cap = 0;
        }
        if (const QStringList *cs = memb.get(cap); cs && !cs->isEmpty())
            cont[cs->first()].insert(t);
        const OptStr pc = st.at(u"primary_culture"_s);
        const moddata::Culture *info = pc && !pc->isEmpty() ? mod.cultures.get(*pc) : nullptr;
        if (info && !info->group.isEmpty())
            cgroup[info->group].insert(t);
        const OptStr rg = st.at(u"religion"_s);
        if (rg && !rg->isEmpty())
            rel[*rg].insert(t);
    }
    localisation::LocEdits *loc = d->maps.contains(u"loc"_s) ? &d->map<localisation::LocEdits>(u"loc"_s) : nullptr;
    auto nm = [loc](const QString &k) {
        const OptStr got = loc ? loc->text(k) : std::nullopt;
        return got && !got->isEmpty() ? *got : py::capitalize(QString(k).replace(u'_', u' '));
    };
    auto by_name = [&nm](const OrderedMap<QString, QSet<QString>> &m) {
        QStringList keys;
        for (const auto &[c, _s] : m)
            keys << c;
        std::stable_sort(keys.begin(), keys.end(), [&](const QString &x, const QString &y) { return nm(x) < nm(y); });
        return keys;
    };
    for (const QString &c : by_name(cont))
        out.push_back({u"Continents (by capital)"_s, u"cont:"_s + c, nm(c), *cont.get(c)});
    for (const QString &c : by_name(cgroup))
        out.push_back({u"Culture groups (primary culture)"_s, u"cg:"_s + c, nm(c), *cgroup.get(c)});
    for (const QString &c : by_name(rel))
        out.push_back({u"Religions (state religion)"_s, u"rel:"_s + c, nm(c), *rel.get(c)});
    std::vector<GSet> kept;
    for (GSet &s : out) {
        QSet<QString> f;
        for (const QString &x : s.tags)
            if (mod.countries.contains(x))
                f.insert(x);
        if (!f.isEmpty()) {                        // (a war side of nations the mod does not have: nothing to show)
            s.tags = f;
            kept.push_back(s);
        }
    }
    return kept;
}

void DiploWebView::_fill_groups(document::Document *d, const QStringList &gps, const std::vector<diplopanel::WarRow> &wars)
{
    _sets = sets_of(d, gps, wars);
    if (!groups_on) {                              // the first time: the three biggest spheres
        std::vector<const GSet *> sph;
        for (const GSet &x : _sets)
            if (x.key.startsWith(u"sphere:"))
                sph.push_back(&x);
        std::stable_sort(sph.begin(), sph.end(), [](const GSet *x, const GSet *y) { return x->tags.size() > y->tags.size(); });
        groups_on = QStringList();
        for (size_t i = 0; i < sph.size() && i < 3; ++i)
            groups_on->append(sph[i]->key);
    }
    glist->blockSignals(true);
    glist->clear();
    QFont bold(glist->font());
    bold.setBold(true);
    OptStr last;
    for (const GSet &s : _sets) {
        if (s.head != last) {
            auto *hi = new QListWidgetItem(s.head);
            hi->setFont(bold);
            hi->setFlags(Qt::NoItemFlags);
            hi->setData(Qt::UserRole, QVariant());
            glist->addItem(hi);
            last = s.head;
        }
        auto *it = new QListWidgetItem(u"%1 (%2)"_s.arg(s.title).arg(s.tags.size()));
        it->setData(Qt::UserRole, s.key);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(groups_on->contains(s.key) ? Qt::Checked : Qt::Unchecked);
        glist->addItem(it);
    }
    glist->blockSignals(false);
    _filter_groups();
    _show_groups();
}

void DiploWebView::_filter_groups()
{
    const QString q = py::strip(gsearch->text()).toLower();
    for (int i = 0; i < glist->count(); ++i) {
        QListWidgetItem *it = glist->item(i);
        it->setHidden(!q.isEmpty() && it->data(Qt::UserRole).isValid() && !it->text().toLower().contains(q));
    }
}

void DiploWebView::_group_ticked(QListWidgetItem *it)
{
    const QVariant kv = it->data(Qt::UserRole);
    if (!kv.isValid())
        return;
    const QString k = kv.toString();
    if (!groups_on)
        groups_on = QStringList();
    const bool on = it->checkState() == Qt::Checked;
    if (on && !groups_on->contains(k))
        groups_on->append(k);
    else if (!on && groups_on->contains(k))
        groups_on->removeOne(k);
    _show_groups();
}

void DiploWebView::_tick_none()
{
    groups_on = QStringList();
    glist->blockSignals(true);
    for (int i = 0; i < glist->count(); ++i) {
        QListWidgetItem *it = glist->item(i);
        if (it->data(Qt::UserRole).isValid())
            it->setCheckState(Qt::Unchecked);
    }
    glist->blockSignals(false);
    _show_groups();
}

void DiploWebView::_show_groups()
{
    std::map<QString, const GSet *> by;
    for (const GSet &s : _sets)
        by[s.key] = &s;
    std::vector<std::tuple<QString, QSet<QString>, Rgb>> chosen;
    const QStringList on = groups_on.value_or(QStringList());
    for (qsizetype i = 0; i < on.size(); ++i)
        if (const auto it = by.find(on[i]); it != by.end())
            chosen.emplace_back(it->second->title, it->second->tags, SET_COLS[size_t(i) % SET_COLS.size()]);
    groups_view->set_sets(chosen);
    const qsizetype bad = groups_view->misplaced.size();
    title->setText(u"Groups: %1 ticked — a nation stands in exactly the boxes of its groups%2"_s.arg(chosen.size()).arg(
        !bad ? u""_s
             : u" · <span style='color:%1'>%2 (red ring) could not be kept out of a box they are not in: these groups "
               u"cross too much for boxes</span>"_s.arg(theme::ERROR_)
                   .arg(bad)));
}

}  // namespace diploweb
