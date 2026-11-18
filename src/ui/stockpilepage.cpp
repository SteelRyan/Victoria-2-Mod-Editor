// stockpilepage.py, Game rules → Stockpile slider (see stockpilepage.h).
#include "ui/stockpilepage.h"
#include "ui/drawtext.h"
#include "core/document.h"
#include "core/pynum.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/textselect.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace stockpilepage {

const QString REL = u"interface/country_trade.gui"_s;
const std::vector<std::pair<long long, QString>> PRESETS = {
    {2000, u"2,000 (the game's)"_s}, {10000, u"10,000"_s}, {100000, u"100,000"_s}, {1000000, u"1 million"_s},
    {100000000, u"100 million"_s}, {1000000000, u"1 billion — as good as no limit"_s}};
const std::vector<std::pair<long long, QString>> STEPS = {{1, u"very fine"_s}, {2, u"the game's"_s}, {5, u"fine"_s},
                                                          {10, u"medium"_s},   {25, u"large"_s},   {50, u"very large"_s}};
static const QRegularExpression SLIDER_RE(
    uR"re((name\s*=\s*"sell_slider"[\s\S]*?maxValue\s*=\s*)([0-9.]+)([\s\S]*?stepSize\s*=\s*)([0-9.]+))re"_s,
    QRegularExpression::UseUnicodePropertiesOption);

double amount(double s) { return BASE * std::exp(K * (s - BASE)); }

long long max_value_for(double m) { return py::round_int(BASE + std::log(std::max(1.0, m) / BASE) / K); }

std::optional<std::pair<double, double>> read(const QString &text)
{
    const auto mm = SLIDER_RE.match(text);
    if (!mm.hasMatch())
        return std::nullopt;
    const auto a = py::to_float(mm.captured(2)), b = py::to_float(mm.captured(4));
    if (!a || !b)                                // (Python: float("1.2.3") raises)
        throw py::ValueError("could not convert string to float");
    return std::pair{*a, *b};
}

QString write(const QString &text, long long max_value, long long step)
{
    const auto mm = SLIDER_RE.match(text);
    if (!mm.hasMatch())
        return text;
    return text.left(mm.capturedStart(0)) + mm.captured(1) + QString::number(max_value) + mm.captured(3) +
           QString::number(step) + text.mid(mm.capturedEnd(0));
}

double clicks_to_double(double step) { return std::log(2.0) / (K * step); }

QString short_(double v)
{
    if (v >= 1e9)
        return py::fmt_g(v / 1e9, 3) + u" billion"_s;
    if (v >= 1e6)
        return py::fmt_g(v / 1e6, 3) + u" million"_s;
    return v >= 10 ? py::fmt_commas(v, 0) : py::fmt_f(v, 1);
}

// ---------------------------------------------------------------------------------------------- the slider
GameSlider::GameSlider(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip(u"Drag the knob or click the arrows: the amount the game shows"_s);
}

void GameSlider::set_max(double mv, std::optional<double> st)
{
    max_value = mv;
    if (st)
        step = *st;
    update();
}

int GameSlider::_arrow(double x) const
{
    // -1 / +1 when x is on the left / right arrow, else 0.
    const QRectF tr = _track();
    if (tr.left() - 36 <= x && x <= tr.left() - 6)
        return -1;
    if (tr.right() + 6 <= x && x <= tr.right() + 36)
        return 1;
    return 0;
}

QRectF GameSlider::_track() const { return QRectF(56, 54, width() - 56 - 190, 10); }

void GameSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF panel(4, 4, width() - 8, height() - 8);   // the game's parchment
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(QColor(226, 220, 204));
    p.drawRoundedRect(panel, 6, 6);
    QFont f(u"Georgia"_s);
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(30, 26, 18));
    const QRectF tr = _track();
    drawtext::draw(p, QRectF(tr.left(), 12, tr.width(), 24), Qt::AlignCenter, u"Maximum stockpile:"_s);
    const std::pair<double, QString> arrows[2] = {{tr.left() - 34, u"◀"_s}, {tr.right() + 8, u"▶"_s}};
    for (const auto &[x, text] : arrows) {     // the arrows
        const QRectF r(x, tr.center().y() - 13, 26, 26);
        p.setPen(QPen(QColor(10, 10, 10), 2));
        p.setBrush(QColor(196, 168, 92));
        p.drawEllipse(r);
        p.setPen(QColor(30, 26, 18));
        p.setFont(QFont(font().family(), 10));
        drawtext::draw(p, r, Qt::AlignCenter, text);
    }
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(QColor(92, 86, 74));
    p.drawRoundedRect(tr, 5, 5);
    const double kx = tr.left() + tr.width() * pos;
    p.setBrush(QColor(theme::GOLD));
    p.drawEllipse(QPointF(kx, tr.center().y()), 13, 13);
    p.setBrush(QColor(40, 40, 44));
    p.drawEllipse(QPointF(kx, tr.center().y()), 7, 7);
    const QRectF box(tr.right() + 44, tr.center().y() - 17, 130, 34);   // the number box
    p.setPen(QPen(QColor(10, 10, 10), 2));
    p.setBrush(QColor(246, 240, 222));
    p.drawRect(box);
    f.setPixelSize(16);
    p.setFont(f);
    p.setPen(QColor(30, 26, 18));
    const double v = amount(max_value * pos);
    drawtext::draw(p, box, Qt::AlignCenter, v < 1e7 ? py::fmt_commas(v, 2) : short_(v));
    QFont sf(font());
    sf.setPixelSize(11);
    p.setFont(sf);
    p.setPen(QColor(70, 62, 48));
    drawtext::draw(p, QRectF(tr.left() - 40, tr.bottom() + 12, 160, 16), Qt::AlignLeft, u"left end ≈ "_s + short_(amount(0)));
    drawtext::draw(p, QRectF(tr.right() - 160, tr.bottom() + 12, 200, 16), Qt::AlignRight,
               u"right end = "_s + short_(amount(max_value)));
}

void GameSlider::_set_from(double x)
{
    const QRectF tr = _track();
    pos = std::max(0.0, std::min(1.0, (x - tr.left()) / std::max(1.0, tr.width())));
    update();
}

void GameSlider::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int a = _arrow(e->position().x());
    if (a) {                                     // one click = one step, as in the game
        const double s = max_value * pos + a * step;
        pos = std::max(0.0, std::min(1.0, s / std::max(1.0, max_value)));
        update();
        return;
    }
    _drag = true;
    _set_from(e->position().x());
}

void GameSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (_drag)
        _set_from(e->position().x());
}

void GameSlider::mouseReleaseEvent(QMouseEvent *) { _drag = false; }

// ---------------------------------------------------------------------------------------------- the page
StockpilePage::StockpilePage(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
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
    sc->setWidget(inner);
    outer->addWidget(sc);
    lay->addWidget(label(u"Stockpile slider"_s, u"title"_s));
    lay->addWidget(label(u"In the game's trade window every good has a slider “Maximum stockpile”: how much of that good "
                         u"the nation may keep in its stockpile. Here you set how far that slider goes. Changing it needs "
                         u"nothing else — it is an interface file (the checksum stays the same)."_s,
                         u"sub"_s));
    // 1 — how far
    auto *s = new Section();
    s->lay->addWidget(label(u"1 · The most a nation may stockpile of one good"_s, u"label"_s));
    s->lay->addWidget(label(u"The amount at the slider's right end. The game's own: 2,000."_s, u"faint"_s));
    auto *row = new QHBoxLayout();
    edit = new QLineEdit();
    edit->setFixedWidth(180);
    edit->setAlignment(Qt::AlignRight);
    connect(edit, &QLineEdit::editingFinished, this, [this] { _typed(); });
    row->addWidget(edit);
    edit_note = label(QString(), u"sub"_s, false);
    row->addWidget(edit_note, 1);
    s->lay->addLayout(row);
    row = new QHBoxLayout();
    row->setSpacing(4);
    for (const auto &[v, t] : PRESETS) {
        auto *b = new QPushButton(t);
        b->setObjectName(u"chip"_s);
        connect(b, &QPushButton::clicked, this, [this, vv = v] { _set(static_cast<double>(vv)); });
        row->addWidget(b);
    }
    row->addStretch(1);
    s->lay->addLayout(row);
    lay->addWidget(s);
    // 2 — what the player will see
    s = new Section();
    s->lay->addWidget(label(u"2 · The slider as the player will see it"_s, u"label"_s));
    s->lay->addWidget(label(u"Drag the knob or click the arrows — it works like the game's: the number is what the player "
                            u"gets there. The left part holds the small amounts, the right part the big ones."_s,
                            u"faint"_s));
    game = new GameSlider();
    s->lay->addWidget(game);
    table = new QLabel(QString());
    table->setTextFormat(Qt::RichText);
    table->setTextInteractionFlags(Qt::TextSelectableByMouse);
    s->lay->addWidget(table);
    lay->addWidget(s);
    // 3 — the arrows
    s = new Section();
    s->lay->addWidget(label(u"3 · The arrow buttons ◀ ▶ next to the slider (a detail)"_s, u"label"_s));
    s->lay->addWidget(label(u"How much one click on an arrow changes the amount. Try it on the slider above. Dragging the "
                            u"knob is the same whatever you pick here."_s,
                            u"faint"_s));
    for (int i = 0; i < static_cast<int>(STEPS.size()); ++i) {
        auto *b = new QPushButton(_step_text(i));
        b->setObjectName(u"chip"_s);
        b->setCheckable(true);
        b->setStyleSheet(u"QPushButton { text-align: left; padding: 5px 10px; }"_s);
        connect(b, &QPushButton::clicked, this, [this, i] { _set_step(i); });
        s->lay->addWidget(b);
        step_btn.push_back(b);
    }
    lay->addWidget(s);
    // 4 — use it
    s = new Section();
    result = new QLabel(QString());
    result->setWordWrap(true);
    result->setTextFormat(Qt::RichText);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    s->lay->addWidget(result);
    row = new QHBoxLayout();
    use_btn = new QPushButton(u"Use it"_s);
    use_btn->setObjectName(u"primary"_s);
    connect(use_btn, &QPushButton::clicked, this, [this] { _use(); });
    row->addWidget(use_btn);
    row->addStretch(1);
    s->lay->addLayout(row);
    lay->addWidget(s);
    lay->addStretch(1);
}

QString StockpilePage::_step_text(int i)
{
    const double st = static_cast<double>(STEPS[static_cast<size_t>(i)].first);
    const double pct = (std::exp(K * st) - 1) * 100;
    static const QStringList names = {u"Very fine"_s, u"The game's own"_s, u"Fine"_s, u"Medium"_s, u"Large"_s,
                                      u"Very large"_s};
    return u"%1 — each click +%2 %, about %3 clicks to double the amount"_s.arg(
        names[i], py::fmt_f(pct, 1), QString::number(py::round_int(clicks_to_double(st))));
}

void StockpilePage::refresh(document::Document *doc_)
{
    doc = doc_;
    store = doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    const auto got = store ? read(store->text(REL)) : std::nullopt;
    if (!got) {
        result->setText(u"<span style='color:%1'>⚠ The slider (sell_slider in %2) was not found.</span>"_s.arg(theme::ERROR_,
                                                                                                             REL));
        use_btn->setEnabled(false);
        return;
    }
    use_btn->setEnabled(true);
    const auto [mv, st] = *got;
    want = amount(mv);
    step_i = 0;
    for (int i = 1; i < static_cast<int>(STEPS.size()); ++i)   // min(range(len(STEPS)), key=…): the first closest
        if (std::fabs(STEPS[static_cast<size_t>(i)].first - st) < std::fabs(STEPS[static_cast<size_t>(step_i)].first - st))
            step_i = i;
    edit->setText(py::fmt_commas(want, 0));
    _show(got);
}

void StockpilePage::_typed()
{
    const QString t = QString(edit->text()).remove(u',').remove(u' ').remove(u'_');
    const auto v = py::to_float(t);
    if (!v) {
        edit_note->setText(u"⚠ a number, like 100000"_s);
        return;
    }
    _set(*v);
}

void StockpilePage::_set(double v)
{
    edit_note->setText(QString());
    want = std::max(2.0, std::min(LIMIT, v));
    edit->setText(py::fmt_commas(want, 0));
    _show();
}

void StockpilePage::_set_step(int i)
{
    step_i = i;
    _show();
}

void StockpilePage::_show(const std::optional<std::pair<double, double>> &now)
{
    const long long mv = max_value_for(want);
    const long long step = STEPS[static_cast<size_t>(step_i)].first;
    for (int i = 0; i < static_cast<int>(step_btn.size()); ++i)
        step_btn[static_cast<size_t>(i)]->setChecked(i == step_i);
    game->set_max(static_cast<double>(mv), static_cast<double>(step));
    QString cells;
    const std::pair<double, QString> parts[5] = {{0.0, u"left end"_s},
                                                 {0.25, u"a quarter"_s},
                                                 {0.5, u"the middle"_s},
                                                 {0.75, u"three quarters"_s},
                                                 {1.0, u"right end"_s}};
    for (const auto &[f, where] : parts)
        cells += u"<td style='padding:2px 14px; text-align:center'><span style='color:%1'>%2</span><br><b>%3</b></td>"_s.arg(
            theme::INK_DIM, where, short_(amount(static_cast<double>(mv) * f)));
    table->setText(u"<table><tr>%1</tr></table>"_s.arg(cells));
    const auto cur = now ? now : (store ? read(store->text(REL)) : std::nullopt);
    result->setText(u"<b>In the game:</b> each good's slider goes up to <b>%1</b>; one arrow click changes it by about "
                    u"<b>%2 %</b>.%3"_s.arg(short_(amount(static_cast<double>(mv))),
                                            py::fmt_f((std::exp(K * static_cast<double>(step)) - 1) * 100, 1),
                                            cur ? u"<br><span style='color:%1'>Now in the mod: up to %2.</span>"_s.arg(
                                                      theme::INK_DIM, short_(amount(cur->first)))
                                                : QString()));
}

void StockpilePage::_use()
{
    if (!store)
        return;
    const QString text = store->text(REL);
    const long long mv = max_value_for(want), step = STEPS[static_cast<size_t>(step_i)].first;
    Q_EMIT scriptEdit(u"Stockpile slider: up to %1"_s.arg(short_(amount(static_cast<double>(mv)))), REL,
                      write(text, mv, step));
}

}  // namespace stockpilepage
