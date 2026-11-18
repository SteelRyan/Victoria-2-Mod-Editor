// mapmarkspage.py, Library → Zoom & map marks (see mapmarkspage.h).
#include "ui/mapmarkspage.h"
#include "ui/drawtext.h"
#include "core/document.h"
#include "core/fs.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/submods.h"
#include "map/checksum.h"
#include "map/fourk.h"
#include "map/gamefont.h"
#include "map/mapmarks.h"
#include "map/mapview_data.h"
#include "ui/gameview.h"
#include "ui/locwin.h"
#include "ui/shortcuts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSlider>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;
using widgets::StepSlider;
using widgets::Stepper;

namespace mapmarkspage {

const std::vector<double> FACTORS = {0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5};
const QString DEFAULT_MAP = u"map/default.map"_s;
const QString SETTINGS_GUI = u"interface/settings.gui"_s;

static constexpr auto UCP = QRegularExpression::UseUnicodePropertiesOption;

static std::optional<double> _num(const std::optional<QString> &s)   // float(s), None when it is not a number
{
    if (!s)
        return std::nullopt;
    return py::to_float(*s);
}

Heights read_heights(const QString &text)
{
    Heights out;
    static const QRegularExpression COMMENT(uR"(#[^\n]*)"_s, UCP);
    for (const QString &key : {u"border_heights"_s, u"terrain_sheet_heights"_s}) {
        const QRegularExpression re(uR"(\b%1\s*=\s*\{([^}]*)\})"_s.arg(key), UCP);
        const auto m = re.match(text);
        if (m.hasMatch()) {
            QString inner = m.captured(1);
            inner.replace(COMMENT, QString());
            std::vector<double> vals;
            for (const QString &x : py::split(inner))
                if (const auto v = _num(x))
                    vals.push_back(*v);
            out.lists.insert(key, vals);
        }
    }
    for (const QString &key : {u"tree"_s, u"border_cutoff"_s}) {
        const QRegularExpression re(uR"(^\s*%1\s*=\s*([0-9.]+))"_s.arg(key), UCP | QRegularExpression::MultilineOption);
        const auto m = re.match(text);
        if (m.hasMatch())
            out.nums.insert(key, _num(m.captured(1)));
    }
    return out;
}

QString write_heights(QString text, const HeightVals &vals)
{
    // default.map with the heights set (the rest of the file as it was)
    auto fmt = [](double v, const QString &like) {
        return like.contains(u'.') ? py::fmt_f(v, 1) : QString::number(py::round_int(v));
    };
    for (const QString &key : {u"border_heights"_s, u"terrain_sheet_heights"_s}) {
        if (const auto *lst = vals.lists.get(key)) {
            const QRegularExpression re(uR"((\b%1\s*=\s*\{)([^}]*)(\}))"_s.arg(key), UCP);
            const auto m = re.match(text);
            if (m.hasMatch()) {
                QString inner = u"\n"_s;
                for (double v : *lst)
                    inner += u"\t%1\n"_s.arg(py::round_int(v));
                text = text.left(m.capturedStart(2)) + inner + text.mid(m.capturedEnd(2));
            }
        }
    }
    for (const QString &key : {u"tree"_s, u"border_cutoff"_s}) {
        if (const double *v = vals.nums.get(key)) {
            const QRegularExpression re(uR"(^(\s*%1\s*=\s*)([0-9.]+))"_s.arg(key),
                                        UCP | QRegularExpression::MultilineOption);
            const auto m = re.match(text);
            if (m.hasMatch())
                text = text.left(m.capturedStart(2)) + fmt(*v, m.captured(2)) + text.mid(m.capturedEnd(2));
        }
    }
    return text;
}

OrderedMap<QString, std::optional<double>> read_render(const QString &text)
{
    OrderedMap<QString, std::optional<double>> out;
    for (const QString &key : {u"counter_distance"_s, u"text_height"_s}) {
        const QRegularExpression re(uR"(\b%1\s*=\s*([0-9.]+))"_s.arg(key), UCP);
        const auto m = re.match(text);
        out.insert(key, m.hasMatch() ? _num(m.captured(1)) : std::nullopt);
    }
    return out;
}

QString write_render(QString text, const OrderedMap<QString, double> &vals)
{
    for (const auto &[key, v] : vals) {
        const QRegularExpression re(uR"((\b%1\s*=\s*)([0-9.]+))"_s.arg(key), UCP);
        const auto m = re.match(text);             // (count=1)
        if (m.hasMatch())
            text = text.left(m.capturedStart(0)) + m.captured(1) + py::fmt_f(v, 6) + text.mid(m.capturedEnd(0));
    }
    return text;
}

// ---------------------------------------------------------------------------------------------- the counter
MarkPreview::MarkPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MarkPreview::set_parts(std::optional<Parts> parts_, double factor_)
{
    parts = std::move(parts_);
    factor = factor_;
    update();
}

void MarkPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(theme::BG));
    const QRectF land(10, 10, width() - 20, height() - 20);
    p.fillRect(land, QColor(118, 132, 70));
    p.setPen(QPen(QColor(20, 22, 14), 2));
    p.drawRect(land);
    if (!parts) {
        p.setPen(QColor(theme::INK_DIM));
        drawtext::draw(p, land, Qt::AlignCenter, u"The game's map-mark pictures were not found."_s);
        return;
    }
    QFont f(u"Arial"_s);
    f.setBold(true);
    const std::pair<double, QString> sides[2] = {{1.0, u"the game's size"_s}, {factor, u"× "_s + py::fmt_g(factor)}};
    for (int i = 0; i < 2; ++i) {
        const auto &[fac, title] = sides[i];
        const double ox = land.left() + 40 + i * land.width() / 2.0;
        const double oy = land.top() + 34;
        p.setPen(QColor(240, 236, 220));
        f.setPixelSize(13);
        p.setFont(f);
        drawtext::draw(p, QRectF(ox - 20, land.top() + 6, 200, 18), Qt::AlignLeft, title);
        p.save();                                    // the counter at its own size, then made bigger / smaller
        p.translate(ox + 20, oy);
        p.scale(2.0 * fac, 2.0 * fac);               // (shown twice as big as on the screen: the game's are tiny)
        _counter(p);
        p.restore();
    }
}

void MarkPreview::_counter(QPainter &p) { draw_counter(p, *parts); }

void draw_counter(QPainter &p, const Parts &pp)
{
    // One army counter in the window's own coordinates, drawn as the game draws it: its panel and colour frame, the
    // flag (a plain white one here), the strength number in the game's font, the organisation bar standing upright at
    // the right end — green from the bottom, red above (shown three quarters full).
    for (const Parts::Item &it : pp.items) {
        if (it.kind == u"pic" && !it.img.isNull()) {
            p.drawImage(QPointF(it.x, it.y), it.img);
        } else if (it.kind == u"flag") {
            const std::optional<QImage> &mask = pp.flag_mask, &over = pp.flag_over;
            const int w = mask ? mask->width() : 16, h = mask ? mask->height() : 14;
            QImage white(w, h, QImage::Format_ARGB32_Premultiplied);
            white.fill(QColor(246, 246, 240));
            if (mask) {                              // the flag's shape: the mask's opacity
                QPainter q(&white);
                q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                q.drawImage(0, 0, *mask);
                q.end();
            }
            p.drawImage(QPointF(it.x, it.y), white);
            if (over)
                p.drawImage(QPointF(it.x, it.y), *over);
        } else if (it.kind == u"bar") {              // rotation 1.5708 round its place: it stands up from there
            const auto [bw, bh] = pp.bar_size.value_or(std::pair{14.0, 6.0});
            const QRectF slot(it.x - bh, it.y - bw + 5, bh, bw);   // (measured on a game shot: x 56–62, y 28–42)
            p.fillRect(slot, QColor(16, 18, 12));
            const QRectF r = slot.adjusted(1.5, 1, -1.5, -0.5);   // the coloured part: 3 px wide inside its dark slot
            const double full = 0.75;
            p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() * (1 - full)), QColor(204, 0, 0));
            p.fillRect(QRectF(r.left(), r.top() + r.height() * (1 - full), r.width(), r.height() * full),
                       QColor(0, 150, 0));
        }
    }
    const auto [tx, ty, _px] = pp.text;
    if (pp.fonts) {                                  // the strength number in the game's own font
        try {
            const QString name = pp.font && !pp.font->isEmpty() ? *pp.font : u"Arial12"_s;
            gamefont::draw(p, *pp.fonts, name, u"30"_s, tx, ty, 30, 18, u"centre"_s);
            return;
        } catch (const std::exception &) {
        }
    }
    QFont f(u"Arial"_s);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor(226, 224, 210));
    drawtext::draw(p, QRectF(tx, ty, 30, 18), Qt::AlignCenter, u"30"_s);
}

// ---------------------------------------------------------------------------------------------- the ruler
Ruler::Ruler(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(118);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Ruler::set_marks(std::vector<std::tuple<double, QString, QString>> m)
{
    marks = std::move(m);
    update();
}

void Ruler::set_camera(double h)
{
    camera = h;
    update();
}

double Ruler::_top() const
{
    double top = 1200.0;
    for (const auto &m : marks)
        top = std::max(top, std::get<0>(m));
    return top * 1.1;
}

double Ruler::_at(double x) const
{
    const double x0 = 20.0, x1 = width() - 20.0;
    return std::clamp((x - x0) / std::max(1.0, x1 - x0) * _top(), gamecam::MIN_H, gamecam::MAX_H);
}

void Ruler::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        Q_EMIT picked(_at(e->position().x()));
}

void Ruler::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        Q_EMIT picked(_at(e->position().x()));
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(theme::BG));
    const double top = _top();
    const double x0 = 20.0, x1 = width() - 20.0, y = 30.0;
    auto at = [&](double h) { return x0 + (x1 - x0) * h / top; };
    p.setPen(QPen(QColor(70, 70, 76), 3));               // where the game's camera never goes
    p.drawLine(QPointF(x0, y), QPointF(at(gamecam::MIN_H), y));
    p.drawLine(QPointF(at(gamecam::MAX_H), y), QPointF(x1, y));
    p.setPen(QPen(QColor(theme::INK_DIM), 3));
    p.drawLine(QPointF(at(gamecam::MIN_H), y), QPointF(at(gamecam::MAX_H), y));
    if (camera >= 0) {                                   // the previews' camera
        const double cx = at(camera);
        QPolygonF tri;
        tri << QPointF(cx, y - 3) << QPointF(cx - 8, y - 17) << QPointF(cx + 8, y - 17);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(theme::GOLD));
        p.drawPolygon(tri);
    }
    p.setPen(QPen(QColor(theme::INK_DIM), 3));
    QFont f(font());
    f.setPixelSize(11);
    p.setFont(f);
    drawtext::draw(p, QRectF(x0, 4, 200, 16), Qt::AlignLeft, u"close to the map"_s);
    drawtext::draw(p, QRectF(x1 - 200, 4, 200, 16), Qt::AlignRight, u"far out (height %1)"_s.arg(py::trunc_int(top)));
    std::vector<std::pair<double, int>> rows;
    auto sorted = marks;
    std::stable_sort(sorted.begin(), sorted.end());
    for (const auto &[h, text, col] : sorted) {
        const double x = x0 + (x1 - x0) * h / top;
        int row = 0;
        while (std::any_of(rows.begin(), rows.end(),
                           [&](const auto &r) { return std::abs(x - r.first) < 150 && r.second == row; }))
            row += 1;
        rows.emplace_back(x, row);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(col));
        p.drawEllipse(QPointF(x, y), 7, 7);
        p.setPen(QPen(QColor(col), 2));
        const double yy = y + 14 + row * 26;
        p.drawLine(QPointF(x, y + 7), QPointF(x, yy));
        drawtext::draw(p, QRectF(x - 75, yy, 150, 24), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   u"%1  %2"_s.arg(text).arg(py::trunc_int(h)));
    }
}

// ---------------------------------------------------------------------------------------------- the page
static const std::pair<QString, QString> H_KEYS[] = {
    {u"border_heights:0"_s, u"borders 1"_s},         {u"border_heights:1"_s, u"borders 2"_s},
    {u"border_heights:2"_s, u"borders 3"_s},         {u"terrain_sheet_heights:0"_s, u"terrain picture"_s},
    {u"tree"_s, u"trees"_s},                         {u"border_cutoff"_s, u"no borders above"_s}};

MapMarksPage::MapMarksPage(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *sc = new QScrollArea();
    sc->setWidgetResizable(true);
    sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sc->setObjectName(u"sideScroll"_s);
    auto *inner = new QWidget();
    inner->setObjectName(u"sideInner"_s);
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(8);
    sc->setWidget(inner);
    outer->addWidget(sc);
    lay->addWidget(label(u"Zoom & map marks"_s, u"title"_s));
    // --- map marks
    auto *s = new Section();
    s->lay->addWidget(_bold(u"Map marks: the counters of armies and fleets"_s));
    s->lay->addWidget(label(
        u"The flat counter next to an army or a fleet (its flag, its strength, the organisation bar) — and the "
        u"marks of battles, sieges, colonising and mobilising — drawn bigger or smaller. Seen in the game at "
        u"1.5 ×: every counter bigger, at every height of the camera. Pictures and interface files: the "
        u"checksum stays (players without it can play with you)."_s,
        u"sub"_s));
    auto *row = new QHBoxLayout();
    row->addWidget(label(u"Size"_s, u"sub"_s, false));
    QStringList names;
    for (double f : FACTORS)
        names << u"× "_s + py::fmt_g(f);
    size = new StepSlider(names, 1);                 // (FACTORS.index(1.0))
    connect(size, &StepSlider::valueChanged, this, [this](int) { _preview(); });
    row->addWidget(size, 1);
    s->lay->addLayout(row);
    preview = new MarkPreview();
    s->lay->addWidget(preview);
    row = new QHBoxLayout();
    use_btn = new QPushButton(u"Use this size"_s);
    use_btn->setObjectName(u"primary"_s);
    connect(use_btn, &QPushButton::clicked, this, [this] { _use_size(); });
    row->addWidget(use_btn);
    row->addStretch(1);
    s->lay->addLayout(row);
    size_msg = label(QString(), u"sub"_s);
    s->lay->addWidget(size_msg);
    lay->addWidget(s);
    // --- zoom heights: the ruler, the two screens, a slider per level
    s = new Section();
    s->lay->addWidget(_bold(u"Zoom heights (map/default.map)"_s));
    s->lay->addWidget(label(
        u"The heights of the camera at which the map changes its look. The game's camera goes from 40 (closest) to "
        u"1000 (farthest; fixed in the game), it starts at 330, and one notch of the wheel goes × 0.9 in or × 1.2 "
        u"out. Drag a level's slider and zoom the screen below to see where it changes."_s,
        u"sub"_s));
    QLabel *warn = label(u"⚠ map/default.map counts for the checksum (measured): changing it means every player needs "
                         u"it."_s,
                         u"sub"_s);
    warn->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::WARN));
    s->lay->addWidget(warn);
    ruler = new Ruler();
    connect(ruler, &Ruler::picked, this, [this](double h) { _camera(view_n->cam.x, view_n->cam.z, h); });
    s->lay->addWidget(ruler);
    // the game's screen, in the middle of the page (the user 2026-11-15: one screen, centred)
    auto *vrow = new QHBoxLayout();
    view_n = new gameview::GameView();
    view_n->screen = QSize(1920, 1080);
    view_n->title = u"Normal screen 1920 × 1080"_s;
    view_n->setMaximumWidth(1100);
    connect(view_n, &gameview::GameView::viewChanged, this, [this](double x, double z, double h) { _camera(x, z, h); });
    vrow->addStretch(1);
    vrow->addWidget(view_n, 8);
    vrow->addStretch(1);
    s->lay->addLayout(vrow);
    row = new QHBoxLayout();
    ter_btn = new QPushButton(u"Terrain view"_s);
    pol_btn = new QPushButton(u"Political view"_s);
    for (QPushButton *b : {ter_btn, pol_btn}) {
        b->setCheckable(true);
        b->setObjectName(u"chip"_s);
        connect(b, &QPushButton::clicked, this, [this, b] {
            ter_btn->setChecked(b == ter_btn);
            pol_btn->setChecked(b == pol_btn);
            view_n->political = b == pol_btn;
            view_n->update();
        });
        row->addWidget(b);
    }
    ter_btn->setChecked(true);
    row->addSpacing(12);
    cam_lbl = label(QString(), u"sub"_s, false);
    row->addWidget(cam_lbl, 1);
    s->lay->addLayout(row);
    s->lay->addWidget(label(
        u"Mouse wheel on the screen: zoom as the game does. Drag: move over the map. Click or drag on the ruler: jump to "
        u"that height. The camera, the heights and what appears are the game's (read from v2game.exe and checked in "
        u"the game); the map's colours, the names' size and where the sample counters stand are the editor's "
        u"approximation."_s,
        u"sub"_s));
    // one row per level: its name, a slider to drag, its height, what it does (measured in the game)
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);
    int gr = 0;
    const gamecam::Levels dflt;
    const std::vector<gamecam::Level> lv = gamecam::levels(dflt);
    auto add_row = [&](const QString &name, QWidget *ctl, QLabel *val, const QString &does) {
        grid->addWidget(_bold(name), gr, 0);
        if (ctl)
            grid->addWidget(ctl, gr, 1);
        if (val) {
            val->setMinimumWidth(54);
            grid->addWidget(val, gr, 2);
        }
        ++gr;
        QLabel *d = label(does, u"sub"_s);
        grid->addWidget(d, gr++, 0, 1, 3);
        grid->setRowMinimumHeight(gr++, 6);
    };
    for (const auto &[key, text] : H_KEYS) {
        const auto it = std::find_if(lv.begin(), lv.end(), [&](const gamecam::Level &l) { return l.key == key; });
        QSlider *sl = _slider(0, 1500, 10);
        QLabel *val = label(u"0"_s, u"sub"_s, false);
        connect(sl, &QSlider::valueChanged, this, [this, val](int v) {
            val->setText(QString::number(v));
            _ruler();
            _views();
        });
        h_edit.insert(key, sl);
        h_val.insert(key, val);
        add_row(it != lv.end() ? it->name : text, sl, val, it != lv.end() ? it->does : QString());
    }
    add_row(u"Unit counters"_s, nullptr, label(u"500"_s, u"sub"_s, false),
            u"The boxes next to armies and fleets are drawn below 500 — fixed in the game (v2game.exe): no file of a "
            u"mod changes it. Their size: Map marks above."_s);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    auto *b = new QPushButton(u"Use these heights"_s);
    connect(b, &QPushButton::clicked, this, [this] { _use_heights(); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    lay->addWidget(s);
    // --- the player's own view
    s = new Section();
    s->lay->addWidget(_bold(u"Your own view of this mod (not in the mod)"_s));
    s->lay->addWidget(label(
        u"Settings of YOUR game for this mod, in its settings folder — they go to nobody else. Measured in the game "
        u"(2026-11-09): counter_distance decides up to which height the province and sea NAMES are drawn (100: none "
        u"at 200; 900: names even on the flat map); text_height changed nothing that could be seen. The game's own "
        u"options slider for it ends at 500."_s,
        u"sub"_s));
    grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setColumnStretch(1, 1);
    gr = 0;
    cd = _slider(0, 1500, 10);
    cd->setValue(300);
    cd_val = label(u"300"_s, u"sub"_s, false);
    connect(cd, &QSlider::valueChanged, this, [this](int v) {
        cd_val->setText(QString::number(v));
        _ruler();
        _views();
    });
    add_row(u"Province names up to (counter_distance)"_s, cd, cd_val,
            u"Below this height the names of provinces and seas stand on the map."_s);
    th = _slider(0, 2000, 10);
    th->setValue(300);
    th_val = label(u"300"_s, u"sub"_s, false);
    connect(th, &QSlider::valueChanged, this, [this](int v) { th_val->setText(QString::number(v)); });
    add_row(u"text_height"_s, th, th_val, u"No visible effect found (50, 300 and 2000 tried)."_s);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    set_btn = new QPushButton(u"Write into my settings"_s);
    connect(set_btn, &QPushButton::clicked, this, [this] { _write_settings(); });
    row->addWidget(set_btn);
    row->addStretch(1);
    s->lay->addLayout(row);
    set_msg = label(QString(), u"sub"_s);
    s->lay->addWidget(set_msg);
    grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setColumnStretch(1, 1);
    gr = 0;
    opt_max = _slider(100, 5000, 50);
    opt_max->setValue(500);
    opt_val = label(u"500"_s, u"sub"_s, false);
    connect(opt_max, &QSlider::valueChanged, this, [this](int v) { opt_val->setText(QString::number(v)); });
    add_row(u"The options slider for the names ends at"_s, opt_max, opt_val,
            u"interface/settings.gui (counterdistance_slider): how far players can set their names in the game's "
            u"options."_s);
    s->lay->addLayout(grid);
    row = new QHBoxLayout();
    b = new QPushButton(u"Use (interface/settings.gui)"_s);
    connect(b, &QPushButton::clicked, this, [this] { _use_opt_max(); });
    row->addWidget(b);
    row->addStretch(1);
    s->lay->addLayout(row);
    lay->addWidget(s);
    s = new Section();
    s->lay->addWidget(_bold(u"Not here, and why (measured 2026-10-26)"_s));
    s->lay->addWidget(label(
        u"The stockpile slider of the trade window ends at exactly its maxValue, 2000. Set higher, its numbers "
        u"run wild: at 5000 the middle showed 412,496 and the end about 77 million, at 20000 it overflowed. "
        u"So it can only be made smaller or given other steps — tell the editor's maker if that is wanted.\n"
        u"The bar above a unit's 3D model (unit_status in mapitems.gfx, its scale) looked the same at 1.9 and "
        u"3.8: the counter above is what you see."_s,
        u"sub"_s));
    lay->addWidget(s);
    lay->addStretch(1);
}

MapMarksPage::~MapMarksPage() = default;

QSlider *MapMarksPage::_slider(int lo, int hi, int step)
{
    auto *sl = new QSlider(Qt::Horizontal);
    sl->setRange(lo, hi);
    sl->setSingleStep(step);
    sl->setPageStep(step * 5);
    sl->setMinimumWidth(160);
    return sl;
}

gamecam::Levels MapMarksPage::levels() const
{
    gamecam::Levels lv;
    auto val = [this](const QString &k, double dflt) {
        const auto *sl = h_edit.get(k);
        return sl && (*sl)->isEnabled() ? double((*sl)->value()) : dflt;
    };
    lv.borders = {val(u"border_heights:0"_s, 500), val(u"border_heights:1"_s, 800), val(u"border_heights:2"_s, 1100)};
    lv.sheet = val(u"terrain_sheet_heights:0"_s, 500);
    lv.cutoff = val(u"border_cutoff"_s, 1100);
    lv.tree = val(u"tree"_s, 350);
    lv.names = cd->value();
    return lv;
}

void MapMarksPage::_world()
{
    if (!md || (_gw && _gw->version == md->version && _gw->W == md->w))
        return;
    _gw = gameview::make_world(mod, *md, [this](long long pid) -> std::optional<QString> {
        if (doc)
            return doc->prov_state(pid).owner;
        const moddata::Province *pr = mod.provinces.get(pid);
        return pr ? pr->owner : std::nullopt;
    });
    // the first time: look at the first country of countries.txt that has land (the game opens on the player's)
    const bool fresh = !view_n->world;
    view_n->md = md;
    view_n->world = _gw;
    if (fresh) {
        const QPointF at = gameview::start_point(*_gw, mod, *md);
        _camera(at.x(), at.y(), gamecam::START_H);
    }
    const int pl = gameview::owner_at(*_gw, *md, view_n->cam.x, view_n->cam.z + 30.0);
    view_n->player = pl;
}

std::vector<gameview::Overlay> screen_windows(locwin::GameLook &look, const std::vector<ScreenWin> &want)
{
    std::vector<gameview::Overlay> wins;
    for (const ScreenWin &w : want) {
        try {
            const shortcuts::Projected pr = shortcuts::project(look, w.rel, w.window, {}, true, true, 0, {}, w.leave_out);
            if (!pr.img || !pr.meta)
                continue;
            // (placed as shortcuts::project_screen places it on the 1920 × 1080 screen, kept on the screen)
            const auto [ax, ay] = shortcuts::anchor(py::lower(py::strip(pr.meta->orientation)));
            const double sw = shortcuts::SCREEN.first, sh = shortcuts::SCREEN.second;
            double x = (w.at ? w.at->x() : sw * ax + pr.meta->position.first) + pr.meta->x0;
            double y = (w.at ? w.at->y() : sh * ay + pr.meta->position.second) + pr.meta->y0;
            if (pr.img->width() <= sw)
                x = std::clamp(x, 0.0, sw - pr.img->width());
            if (pr.img->height() <= sh)
                y = std::clamp(y, 0.0, sh - pr.img->height());
            wins.push_back({*pr.img, x, y, w.factor});
        } catch (...) {
        }
    }
    return wins;
}

void MapMarksPage::_windows()
{
    // the always-there windows at the game's size: top bar, the lower right bar with the minimap, the outliner
    if (!store)
        return;
    if (!_look)
        _look = std::make_unique<locwin::GameLook>(mod, store);
    std::vector<gameview::Overlay> wins = screen_windows(
        *_look, {{u"interface/topbar.gui"_s, u"topbar"_s, 1.0},
                 {u"interface/menubar.gui"_s, u"menubar"_s, 1.0},
                 {u"interface/outliner.gui"_s, u"outliner"_s, 1.0}});
    view_n->windows = std::move(wins);
}

void MapMarksPage::_views()
{
    const gamecam::Levels lv = levels();
    view_n->set_levels(lv);
    view_n->counter_factor = factor();
    if (_counter_parts) {
        const Parts parts = *_counter_parts;
        view_n->counter = [parts](QPainter &p) { draw_counter(p, parts); };
    } else {
        view_n->counter = nullptr;
    }
    view_n->update();
    _camera(view_n->cam.x, view_n->cam.z, view_n->cam.h);
}

void MapMarksPage::_camera(double x, double z, double h)
{
    view_n->set_view(x, z, h);
    ruler->set_camera(h);
    QStringList on;
    for (const gamecam::Level &l : gamecam::levels(levels()))
        if (l.active_below ? h < l.height : h >= l.height)
            on << l.name;
    cam_lbl->setText(u"Camera height %1 — active: %2"_s.arg(py::round_int(h)).arg(
        on.isEmpty() ? u"none"_s : on.join(u", "_s)));
}

QLabel *MapMarksPage::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ data
double MapMarksPage::factor() const { return FACTORS[size_t(size->val)]; }

std::optional<QString> MapMarksPage::find(const QString &path) const { return find_picture(mod, path); }

std::optional<QString> find_picture(const moddata::Mod &mod, const QString &path)
{
    QString rel = path;
    rel.replace(u"\\\\"_s, u"/"_s);
    rel.replace(u'\\', u'/');
    rel = py::strip(rel);
    const QString stem = pypath::splitext_stem(rel), ext = rel.mid(stem.size());
    const QString cands[2] = {rel, stem + (py::lower(ext) == u".dds" ? u".tga"_s : u".dds"_s)};
    for (const QString &cand : cands) {
        for (const QString &root : {mod.root, mod.base.value_or(QString())}) {   // (the mod's picture first)
            if (root.isEmpty())
                continue;
            const QString full = pypath::join(root, cand.split(u'/'));
            if (fs::isfile(full))
                return full;
        }
    }
    return std::nullopt;
}

std::optional<QImage> MapMarksPage::_qimage(const QString &full, long long frames)
{
    // A picture of the game (.dds too, which Qt does not read) as a QImage — its first frame.
    fourk::Pic rgba;
    try {
        rgba = fourk::read_picture(full).first;
    } catch (const std::exception &) {
        return std::nullopt;
    }
    if (frames > 1 && rgba.w % frames == 0)
        rgba = rgba.part(0, rgba.h, 0, rgba.w / frames);
    return fourk::to_qimage(rgba);
}

void MapMarksPage::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    if (!store)
        return;
    QStringList texts;
    for (const QString &d : {pypath::join(mod.base.value_or(QString()), u"interface"_s),
                             pypath::join(mod.root, u"interface"_s)}) {
        try {
            for (const QString &fn : fs::listdir(d))
                if (py::lower(fn).endsWith(u".gfx"))
                    texts << script::read_text(pypath::join(d, fn));
        } catch (const fs::OSError &) {
        }
    }
    _fonts = mapmarks::fonts_of(texts);
    const QString cur = store->text(mapmarks::GFX);
    static const QRegularExpression MADE(uR"re(mapmarks\\\\[^"]*")re"_s, UCP);
    size_msg->setText(MADE.match(cur).hasMatch() ? u"This mod has map marks made by the editor."_s : QString());
    const Heights hv = read_heights(store->text(DEFAULT_MAP));
    for (auto &[key, st] : h_edit) {
        const qsizetype colon = key.indexOf(u':');
        const QString k = colon < 0 ? key : key.left(colon);
        std::optional<double> v;
        if (colon >= 0) {
            const int i = key.mid(colon + 1).toInt();
            if (const auto *lst = hv.lists.get(k); lst && i < qsizetype(lst->size()))
                v = (*lst)[size_t(i)];
        } else if (const auto *n = hv.nums.get(k)) {
            v = *n;
        }
        st->setEnabled(v.has_value());
        st->blockSignals(true);
        if (v && *v > st->maximum())
            st->setMaximum(int(std::ceil(*v / 100.0)) * 100);
        st->setValue(v ? py::round_int(*v) : 0);
        st->blockSignals(false);
        (*h_val.get(key))->setText(v ? QString::number(st->value()) : u"—"_s);
    }
    {
        const QString sg = store->text(SETTINGS_GUI);
        static const QRegularExpression OPT(
            uR"re(name\s*=\s*"counterdistance_slider"[\s\S]*?maxValue\s*=\s*([0-9.]+))re"_s, UCP);
        const auto mm = OPT.match(sg);
        if (mm.hasMatch())
            if (const auto v = py::to_float(mm.captured(1)))   // (float("1.2.3") raises: passed over)
                opt_max->setValue(int(py::trunc_int(*v)));
    }
    _read_settings();
    _world();
    _windows();
    _preview();
    _ruler();
}

static void walk(const script::Nodes &ns, const script::Node *&win)
{
    for (const script::Node &n : ns) {
        if (n.is_block) {
            if (py::lower(n.has_key ? n.key : QString()) == u"windowtype" &&
                fourk::val_of(n, u"name"_s) == u"unit_mapicon"_s)
                win = &n;
            walk(n.kids, win);
        }
    }
}

static const script::Node *kid(const script::Node &node, const QString &name)   // fourk._kid
{
    for (const script::Node &c : node.kids)
        if (c.has_key && !c.key.isEmpty() && py::lower(c.key) == name)
            return &c;
    return nullptr;
}

std::optional<Parts> MapMarksPage::_parts() { return counter_parts(mod, store); }

std::optional<Parts> counter_parts(const moddata::Mod &mod, scriptstore::ScriptStore *store)
{
    // The army counter's pieces at 1 ×: from the game's window unit_mapicon and its pictures.
    auto find = [&mod](const QString &path) { return find_picture(mod, path); };
    auto _qimage = [](const QString &full, long long frames) { return MapMarksPage::_qimage(full, frames); };
    const QStringList og = mapmarks::originals(mod, store);
    const QString &gui = og[0], &gfx = og[1];
    const script::Nodes tree = script::parse(gui);
    const script::Node *win = nullptr;
    walk(tree, win);
    if (!win)
        return std::nullopt;
    const script::Nodes gtree = script::parse(gfx);
    std::map<QString, std::pair<QString, long long>> tex;
    for (const script::Node &top : gtree) {
        if (!top.is_block)
            continue;
        for (const script::Node &sp : top.kids) {
            if (!sp.is_block)
                continue;
            std::optional<QString> t = fourk::val_of(sp, u"texturefile"_s);
            if (!t || t->isEmpty())
                t = fourk::val_of(sp, u"textureFile"_s);
            const auto n = fourk::val_of(sp, u"noofframes"_s);
            const auto name = fourk::val_of(sp, u"name"_s);
            if (t && !t->isEmpty() && name)          // (a sprite without a name: never looked up)
                tex[*name] = {*t, n && !n->isEmpty() && fourk::NUM(*n) ? py::trunc_int(*py::to_float(*n)) : 1};
        }
    }
    Parts out;
    for (const script::Node &top : gtree) {          // the flag's mask and overlay, the bar's size
        if (!top.is_block)
            continue;
        for (const script::Node &sp : top.kids) {
            if (!sp.is_block)
                continue;
            const auto name = fourk::val_of(sp, u"name"_s);
            if (name == u"GFX_mapicon_flag"_s) {
                const std::pair<QString, std::optional<QImage> *> keys[2] = {{u"texturefile2"_s, &out.flag_mask},
                                                                             {u"texturefile1"_s, &out.flag_over}};
                for (const auto &[key, part] : keys) {
                    const auto t = fourk::val_of(sp, key);
                    const auto full = t && !t->isEmpty() ? find(*t) : std::nullopt;
                    *part = full ? _qimage(*full, 1) : std::nullopt;
                }
            } else if (name == u"GFX_unitpanel_organisation_on_map"_s) {
                const script::Node *sz = kid(sp, u"size"_s);
                if (sz && sz->is_block) {
                    const auto x = _num(fourk::val_of(*sz, u"x"_s)), y = _num(fourk::val_of(*sz, u"y"_s));
                    out.bar_size = std::pair{x && *x != 0 ? *x : 14.0, y && *y != 0 ? *y : 6.0};
                }
            }
        }
    }
    out.fonts = std::make_shared<gamefont::Fonts>(mod, store);   // (GameLook(self.mod, self.store).fonts)
    for (const script::Node &el : win->kids) {
        if (!el.is_block)
            continue;
        const script::Node *pos = kid(el, u"position"_s);
        std::optional<double> ox, oy;
        if (pos && pos->is_block) {
            ox = _num(fourk::val_of(*pos, u"x"_s));
            oy = _num(fourk::val_of(*pos, u"y"_s));
        }
        const double x = ox.value_or(0.0), y = oy.value_or(0.0);
        const QString k = py::lower(el.has_key ? el.key : QString());
        std::optional<QString> sprite = fourk::val_of(el, u"spritetype"_s);
        if (!sprite || sprite->isEmpty())
            sprite = fourk::val_of(el, u"quadtexturesprite"_s);
        if (k == u"instanttextboxtype") {
            out.text = {x, y, 12};
            out.font = fourk::val_of(el, u"font"_s);
        } else if (sprite == u"GFX_mapicon_flag"_s) {
            out.items.push_back({QImage(), x, y, u"flag"_s});
        } else if (sprite && !sprite->isEmpty() && py::lower(*sprite).contains(u"organisation")) {
            out.items.push_back({QImage(), x, y, u"bar"_s});
        } else if (sprite && tex.count(*sprite) && !py::lower(*sprite).contains(u"attr")) {
            const auto &[t, frames] = tex[*sprite];
            const auto full = find(t);
            const auto img = full ? _qimage(*full, frames) : std::nullopt;
            if (img && !img->isNull())
                out.items.push_back({*img, x, y, u"pic"_s});
        }
    }
    return out;
}

void MapMarksPage::_preview()
{
    if (!store)
        return;
    _counter_parts = _parts();
    preview->set_parts(_counter_parts, factor());
    _views();
}

void MapMarksPage::_ruler()
{
    std::vector<std::tuple<double, QString, QString>> marks;
    for (const auto &[key, st] : h_edit) {
        if (!st->isEnabled())
            continue;
        const qsizetype colon = key.indexOf(u':');
        const QString k = colon < 0 ? key : key.left(colon);
        QString text;
        if (k == u"border_heights")
            text = colon >= 0 ? u"borders %1"_s.arg(key.mid(colon + 1).toInt() + 1) : u"borders"_s;
        else if (k == u"terrain_sheet_heights")
            text = u"terrain picture"_s;
        else if (k == u"tree")
            text = u"trees"_s;
        else
            text = u"no borders above"_s;
        if (k == u"terrain_sheet_heights")
            text = u"flat map"_s;
        const QString col = k == u"border_cutoff" ? u"#e2574c"_s
                            : k == u"tree"        ? u"#4caf50"_s
                            : k.startsWith(u"terrain") ? u"#7fb069"_s
                                                       : u"#d9a441"_s;
        marks.emplace_back(double(st->value()), text, col);
    }
    marks.emplace_back(gamecam::COUNTERS_BELOW, u"counters (fixed)"_s, u"#b0b0b0"_s);
    if (cd->value())
        marks.emplace_back(double(cd->value()), u"names (yours)"_s, u"#6fa8dc"_s);
    ruler->set_marks(std::move(marks));
}

// ------------------------------------------------------------------ edits
void MapMarksPage::_use_size()
{
    if (!store)
        return;
    const QStringList og = mapmarks::originals(mod, store);
    const double f = factor();
    if (std::abs(f - 1.0) < 1e-9) {
        Q_EMIT bundleEdit(u"Map marks back to the game's size"_s, Texts{{mapmarks::GUI, og[0]}, {mapmarks::GFX, og[1]}},
                          Files{});
        size_msg->setText(u"The game's own size again (on Apply). The pictures the editor made stay in %1, unused."_s.arg(
            mapmarks::PIC_DIR));
        return;
    }
    const mapmarks::Made made = mapmarks::make(og[0], og[1], [this](const QString &p) { return find(p); }, f, _fonts);
    Files files;
    for (const auto &[rel, pic] : made.pics) {
        moreviews::Asset a;
        a.how = u"pic"_s;
        a.pic = pic;
        files.insert(rel, a);
    }
    Q_EMIT bundleEdit(u"Map marks × "_s + py::fmt_g(f), Texts{{mapmarks::GUI, made.gui}, {mapmarks::GFX, made.gfx}},
                      files);
    size_msg->setText(u"Map marks × %1: %2 pictures, the two interface files — written on Apply. %3"_s.arg(
        py::fmt_g(f), QString::number(made.pics.size()), made.notes.join(u"; "_s)));
}

void MapMarksPage::_use_heights()
{
    const QString text = store ? store->text(DEFAULT_MAP) : QString();
    if (text.isEmpty())
        return;
    const Heights hv = read_heights(text);
    HeightVals vals;
    for (const auto &[key, st] : h_edit) {
        if (!st->isEnabled())
            continue;
        const qsizetype colon = key.indexOf(u':');
        if (colon >= 0) {
            const QString k = key.left(colon);
            if (!vals.lists.contains(k))
                vals.lists.insert(k, hv.lists.value(k));
            vals.lists[k][size_t(key.mid(colon + 1).toInt())] = double(st->value());
        } else {
            vals.nums.insert(key, double(st->value()));
        }
    }
    Q_EMIT scriptEdit(u"Zoom heights"_s, DEFAULT_MAP, write_heights(text, vals));
}

void MapMarksPage::_use_opt_max()
{
    const QString sg = store ? store->text(SETTINGS_GUI) : QString();
    if (sg.isEmpty())
        return;
    static const QRegularExpression OPT(
        uR"re((name\s*=\s*"counterdistance_slider"[\s\S]*?maxValue\s*=\s*)([0-9.]+))re"_s, UCP);
    QString fresh;
    qsizetype at = 0;
    auto it = OPT.globalMatch(sg);
    while (it.hasNext()) {
        const auto m = it.next();
        fresh += sg.mid(at, m.capturedStart(0) - at) + m.captured(1) + QString::number(opt_max->value());
        at = m.capturedEnd(0);
    }
    fresh += sg.mid(at);
    Q_EMIT scriptEdit(u"Options slider of the counters up to %1"_s.arg(opt_max->value()), SETTINGS_GUI, fresh);
}

std::optional<QString> MapMarksPage::settings_file() const
{
    const submods::ModfileValues v = submods::modfile_values(submods::real_root(mod) + u".mod"_s);
    const QString *ud = v.other.get(u"user_dir"_s);
    if (!ud || ud->isEmpty())
        return std::nullopt;
    return pypath::join(checksum::documents_dir(), {*ud, u"settings.txt"_s});
}

void MapMarksPage::_read_settings()
{
    const auto f = settings_file();
    OrderedMap<QString, std::optional<double>> vals;
    try {
        if (!f)
            throw fs::OSError("TypeError");
        vals = read_render(script::read_text(*f));
    } catch (const fs::OSError &) {
        set_msg->setText(u"No settings of yours for this mod yet (%1): start the mod once, then they can be set here."_s
                             .arg(f.value_or(u"the mod has no user_dir"_s)));
        set_btn->setEnabled(false);
        return;
    }
    set_btn->setEnabled(true);
    const std::tuple<QSlider *, QLabel *, QString> sets[2] = {{cd, cd_val, u"counter_distance"_s},
                                                             {th, th_val, u"text_height"_s}};
    for (const auto &[st, val, k] : sets) {
        const auto v = vals.value(k);
        if (v) {
            st->blockSignals(true);
            if (*v > st->maximum())
                st->setMaximum(int(std::ceil(*v / 100.0)) * 100);
            st->setValue(int(py::round_int(*v)));
            st->blockSignals(false);
            val->setText(QString::number(st->value()));
        }
    }
    set_msg->setText(u"Your settings: %1"_s.arg(*f));
}

void MapMarksPage::_write_settings()
{
    const auto f = settings_file();
    if (!f)
        return;
    try {
        const QString text = script::read_text(*f);
        const QString fresh = write_render(text, {{u"counter_distance"_s, double(cd->value())},
                                                  {u"text_height"_s, double(th->value())}});
        bool all_there = true;
        for (const QString &k : {u"counter_distance"_s, u"text_height"_s})
            if (!QRegularExpression(uR"(\b%1\s*=)"_s.arg(k), UCP).match(text).hasMatch())
                all_there = false;
        if (fresh == text && !all_there) {
            set_msg->setText(u"⚠ Your settings file has no counter_distance / text_height lines yet: change anything "
                             u"in the game's options once, then set it here."_s);
            return;
        }
        script::write_text(*f, fresh);
        set_msg->setText(u"Written into %1 — the game reads it at its next start."_s.arg(*f));
        _ruler();
    } catch (const fs::OSError &e) {
        set_msg->setText(u"⚠ Could not write %1: %2"_s.arg(*f, e.text()));
    }
}

}  // namespace mapmarkspage
