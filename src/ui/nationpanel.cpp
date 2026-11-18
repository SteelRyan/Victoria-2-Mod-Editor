// nationpanel.py — Nation page (see nationpanel.h).
#include "ui/nationpanel.h"
#include "ui/drawtext.h"
#include "core/fs.h"
#include "core/localisation.h"
#include "core/pynum.h"
#include "core/pyre.h"
#include "core/pytext.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "ui/iconstrips.h"
#include "ui/locwin.h"
#include "ui/reformwin.h"
#include "ui/shortcuts.h"
#include "ui/nationextras.h"
#include "ui/theme.h"
#include "ui/textselect.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;

namespace nationpanel {

namespace {

const QString DATE_RE = u"^\\d{1,4}\\.\\d{1,2}\\.\\d{1,2}$"_s;

localisation::LocEdits *loc_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"loc"_s) ? &doc->map<localisation::LocEdits>(u"loc"_s) : nullptr;
}

scriptstore::ScriptStore *scripts_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
}

countryhist::NationEdits *nations_of(document::Document *doc)
{
    return doc && doc->maps.contains(u"nations"_s) ? &doc->map<countryhist::NationEdits>(u"nations"_s) : nullptr;
}

QString loc_or(const moddata::Mod &mod, const QString &key)   // mod.loc.get(key) or pretty(key)
{
    const QString *t = mod.loc.get(key);
    return t && !t->isEmpty() ? *t : pretty(key);
}

// (frames: ui\iconstrips)

}  // namespace

QString pretty(const OptStr &key)
{
    return key && !key->isEmpty() ? py::capitalize(py::strip(QString(*key).replace(u'_', u' '))) : u"—"_s;
}

std::vector<dialogs::PickItem> nation_items(const moddata::Mod &mod)
{
    // Nations for the picker: normal ones by name, dynamic tags (D01…) last under their own heading.
    std::vector<std::pair<QString, const moddata::Country *>> kv;
    for (const auto &[t, c] : mod.countries)
        kv.emplace_back(t, &c);
    std::stable_sort(kv.begin(), kv.end(), [](const auto &a, const auto &b) {
        return std::make_pair(a.second->dynamic, py::lower(a.second->name)) <
               std::make_pair(b.second->dynamic, py::lower(b.second->name));
    });
    std::vector<dialogs::PickItem> out;
    for (const auto &[t, c] : kv)
        out.push_back({t, u"%1  (%2)"_s.arg(c->name, t), c->color,
                       c->dynamic ? OptStr(u"Dynamic tags (rarely used)"_s) : std::nullopt});
    return out;
}

std::vector<long long> stats_owned(document::Document &doc, const moddata::Mod &mod, const QString &tag)
{
    // Land provinces the nation owns at the scenario's start (with unsaved Provinces-tab edits).
    std::vector<long long> out;
    for (const auto &[p, pr] : mod.provinces)
        if (!pr.sea && doc.prov_state(p).owner == OptStr(tag))
            out.push_back(p);
    return out;
}

// ---------------------------------------------------------------------------------------------- GovCombo
void GovCombo::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    const QString s = currentData(SHORT).toString();
    if (!s.isEmpty())
        opt.currentText = s;
    p.drawComplexControl(QStyle::CC_ComboBox, opt);
    p.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

void GovCombo::showPopup()
{
    const QFontMetrics fm = view()->fontMetrics();
    int w = 0;
    for (int i = 0; i < count(); ++i)
        w = std::max(w, fm.horizontalAdvance(itemText(i)));
    view()->setMinimumWidth(std::max(width(), w + 40));
    QComboBox::showPopup();
}

// ---------------------------------------------------------------------------------------------- TechTree
const QColor TechTree::GREEN(112, 170, 98), TechTree::YELLOW(214, 198, 104), TechTree::GREY(88, 93, 102);

TechTree::TechTree(QWidget *parent) : QWidget(parent)
{
    textselect::mark_takes_mouse(this);   // (a widget that takes the mouse itself: its labels stay unselectable)
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TechTree::set_data(const countryhist::NationData *d, const OrderedMap<QString, QString> *l)
{
    data = d;
    loc = l;
    setFixedHeight(_height());
    update();
}

void TechTree::set_owned(const QSet<QString> &o)
{
    owned = o;
    update();
}

int TechTree::_rows() const
{
    if (!data || data->folders.empty())
        return 6;
    qsizetype m = 0;
    for (const auto &[_f, areas] : data->folders)
        for (const QString &a : areas)
            if (const QStringList *l = data->area_techs.get(a))
                m = std::max(m, l->size());
    return m ? int(m) : 6;
}

int TechTree::_height() const { return TAB_H + GAP + HEAD_H + _rows() * (CELL_H + GAP); }
QSize TechTree::sizeHint() const { return {420, _height()}; }
QSize TechTree::minimumSizeHint() const { return {260, _height()}; }

QString TechTree::name(const QString &key) const
{
    const QString *t = loc ? loc->get(key) : nullptr;
    return t && !t->isEmpty() ? *t : py::title(QString(key).replace(u'_', u' '));
}

void TechTree::paintEvent(QPaintEvent *)
{
    if (!data || data->folders.empty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double W = width();
    const auto &folders = data->folders;
    const int nf = int(folders.size());
    const double tw = (W - GAP * (nf - 1)) / double(nf);
    _tabs.clear();
    _cells.clear();
    QFont small(font());
    small.setPointSizeF(std::max(6.5, font().pointSizeF() - 1.5));
    QFont tiny(small);
    tiny.setPointSizeF(std::max(6.0, small.pointSizeF() - 1.0));
    // folder tabs
    for (int i = 0; i < nf; ++i) {
        const QString &fold = folders[size_t(i)].first;
        const QRectF r(i * (tw + GAP), 0, tw, TAB_H);
        _tabs.emplace_back(r, i);
        const bool cur = i == folder;
        p.setPen(QPen(QColor(cur ? theme::GOLD : theme::LINE), cur ? 2 : 1));
        p.setBrush(QColor(cur ? theme::PANEL_ALT : theme::BG));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        p.setFont(small);
        p.setPen(QColor(cur ? theme::INK : theme::INK_DIM));
        drawtext::draw(p, r.adjusted(4, 3, -4, -TAB_H / 2.0), Qt::AlignHCenter | Qt::AlignVCenter,
                   name(fold).replace(u" Tech"_s, QString()).replace(u" tech"_s, QString()));
        int total = 0, n = 0;
        for (const auto &[t, d] : data->techs)
            if (d.folder == fold) {
                ++total;
                n += owned.contains(t) ? 1 : 0;
            }
        const QRectF bar(r.left() + 6, r.top() + TAB_H / 2.0 + 2, r.width() - 12, TAB_H / 2.0 - 7);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 34, 40));
        p.drawRect(bar);
        if (total) {
            p.setBrush(QColor(60, 160, 70));
            p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * n / total, bar.height()));
        }
        p.setFont(tiny);
        p.setPen(QColor(235, 235, 235));
        drawtext::draw(p, bar, Qt::AlignCenter, u"%1/%2"_s.arg(n).arg(total));
    }
    // the chosen folder's lines as columns
    const QStringList &areas = folders[size_t(folder)].second;
    const int na = std::max(1, int(areas.size()));
    const double cw = (W - GAP * (na - 1)) / double(na);
    const double top = TAB_H + GAP;
    for (int j = 0; j < int(areas.size()); ++j) {
        const QString &area = areas[j];
        const double x = j * (cw + GAP);
        const QRectF head(x, top, cw, HEAD_H);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(38, 40, 44));
        p.drawRect(head);
        p.setFont(small);
        p.setPen(QColor(235, 230, 215));
        drawtext::draw(p, head.adjusted(2, 0, -2, 0), Qt::AlignCenter,
                   p.fontMetrics().elidedText(name(area), Qt::ElideRight, int(cw - 4)));
        const QStringList line = data->area_techs.value(area);
        OptStr nxt;
        for (const QString &t : line)
            if (!owned.contains(t)) {
                nxt = t;
                break;
            }
        for (int k = 0; k < int(line.size()); ++k) {
            const QString &t = line[k];
            const QRectF r(x, top + HEAD_H + GAP + k * (CELL_H + GAP), cw, CELL_H);
            _cells.emplace_back(r, t);
            const bool has = owned.contains(t), next = nxt && t == *nxt;
            const QColor col = has ? GREEN : next ? YELLOW : GREY;
            p.setPen(QPen(col.darker(150), 1));
            p.setBrush(col);
            p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
            const QColor ink = has || next ? QColor(22, 22, 20) : QColor(225, 225, 225);
            p.setPen(ink);
            const QString text = name(t);
            for (const QFont *f : {&small, &tiny}) {      // two lines at most: smaller font if needed
                p.setFont(*f);
                const QRect br = p.fontMetrics().boundingRect(r.adjusted(4, 0, -3, 0).toRect(), Qt::TextWordWrap, text);
                if (br.height() <= CELL_H - 2)
                    break;
            }
            drawtext::draw(p, r.adjusted(4, 1, -3, -1), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
            if (has && k + 1 < int(line.size())) {     // the little arrow down, like the game
                const double cx = r.center().x(), cy = r.bottom() + GAP / 2.0;
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(200, 205, 190));
                p.drawPolygon(QPolygonF({QPointF(cx - 4, cy - 2), QPointF(cx + 4, cy - 2), QPointF(cx, cy + 2.5)}));
            }
        }
    }
    p.end();
}

std::optional<std::pair<QString, QString>> TechTree::_hit(const QPointF &pos) const
{
    for (const auto &[r, i] : _tabs)
        if (r.contains(pos))
            return std::pair{u"tab"_s, QString::number(i)};
    for (const auto &[r, t] : _cells)
        if (r.contains(pos))
            return std::pair{u"tech"_s, t};
    return std::nullopt;
}

void TechTree::mousePressEvent(QMouseEvent *ev)
{
    const auto h = _hit(ev->position());
    if (!h)
        return;
    if (h->first == u"tab") {
        folder = h->second.toInt();
        update();
    } else if (enabled_edit) {
        Q_EMIT clicked(h->second);
    }
}

void TechTree::mouseMoveEvent(QMouseEvent *ev)
{
    const auto h = _hit(ev->position());
    if (h && h->first == u"tech") {
        const countryhist::TechInfo *d = data->techs.get(h->second);
        QToolTip::showText(ev->globalPosition().toPoint(),
                           u"%1  ·  %2  ·  %3"_s.arg(name(h->second), d ? d->year : QString(),
                                                     owned.contains(h->second)
                                                         ? u"has it"_s
                                                         : u"click to give it (and the ones above)"_s),
                           this);
    } else {
        QToolTip::hideText();
    }
}

// ---------------------------------------------------------------------------------------------- NewNationDialog
NewNationDialog::NewNationDialog(moddata::Mod &mod_, const QString &tmpl, countryhist::NewNations &nn_, QWidget *parent)
    : QDialog(parent), mod(mod_), nn(nn_)
{
    const moddata::Country &tpl = *mod.countries.get(tmpl);
    setWindowTitle(u"New nation from %1"_s.arg(tpl.name));
    color = {std::min(255, std::get<0>(tpl.color) + 40), std::min(255, std::get<1>(tpl.color) + 40),
             std::min(255, std::get<2>(tpl.color) + 40)};
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(label(u"A copy of %1 (%2): its common/countries file with a new colour, its history without its "
                         u"armies, its flags. Give it provinces in Provinces (owner), then set its capital here. "
                         u"Everything is written on Apply."_s.arg(tpl.name, tmpl),
                         u"faint"_s));
    auto *form = new QFormLayout();
    tag = new QLineEdit();
    tag->setMaxLength(3);
    tag->setPlaceholderText(u"3 letters / digits, e.g. NE1"_s);
    name = new QLineEdit();
    name->setPlaceholderText(u"e.g. New Devogard"_s);
    adj = new QLineEdit();
    adj->setPlaceholderText(u"e.g. New Devonian"_s);
    col_btn = new QPushButton(u"Colour"_s);
    connect(col_btn, &QPushButton::clicked, this, &NewNationDialog::_pick_color);
    form->addRow(u"Tag"_s, tag);
    form->addRow(u"Name"_s, name);
    form->addRow(u"Adjective"_s, adj);
    form->addRow(u"Map colour"_s, col_btn);
    lay->addLayout(form);
    err = label(QString(), u"warn"_s);
    err->hide();
    lay->addWidget(err);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(u"Create  (written on Apply)"_s);
    connect(bb, &QDialogButtonBox::accepted, this, &NewNationDialog::_ok);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    _show_color();
}

void NewNationDialog::_show_color()
{
    col_btn->setIcon(widgets::swatch_icon(color, 14));
    col_btn->setText(u"  rgb(%1, %2, %3)"_s.arg(std::get<0>(color)).arg(std::get<1>(color)).arg(std::get<2>(color)));
}

void NewNationDialog::_pick_color()
{
    const QColor c = QColorDialog::getColor(QColor(std::get<0>(color), std::get<1>(color), std::get<2>(color)), this,
                                            u"Map colour"_s);
    if (c.isValid()) {
        color = {c.red(), c.green(), c.blue()};
        _show_color();
    }
}

void NewNationDialog::_ok()
{
    const QString p = nn.problem(tag->text(), name->text());
    if (!p.isEmpty()) {
        err->setText(u"⚠ "_s + p);
        err->show();
        return;
    }
    accept();
}

std::tuple<QString, QString, QString, Rgb> NewNationDialog::result_data() const
{
    return {py::upper(py::strip(tag->text())), py::strip(name->text()), py::strip(adj->text()), color};
}

// ---------------------------------------------------------------------------------------------- NationPanel
NationPanel::NationPanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(450);
    connect(_timer, &QTimer::timeout, this, &NationPanel::_flush);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- which nation
    auto *s = new Section();
    picker = new dialogs::PickCombo(nation_items(mod));
    picker->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(picker, &QComboBox::activated, this, [this](int) {
        if (const OptStr k = picker->key(); k && !k->isEmpty())
            Q_EMIT nationPicked(*k);
    });
    s->lay->addWidget(picker);
    title = label(u"Click a province of a nation on the map, or pick one above."_s, u"title"_s);
    s->lay->addWidget(title);
    sub = label(QString(), u"sub"_s);
    sub->setTextFormat(Qt::RichText);
    s->lay->addWidget(sub);
    note = label(QString(), u"faint"_s);
    s->lay->addWidget(note);
    new_btn = new QPushButton(u"＋ New nation from this one…"_s);
    new_btn->setToolTip(u"A copy of this nation under a new tag: its common/countries file, its history (without "
                        u"armies), its flags; name and adjective in the localisation"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] {
        if (tag)
            Q_EMIT newNationRequested(*tag);
    });
    s->lay->addWidget(new_btn);
    lay->addWidget(s);

    // --- names
    s = new Section();
    s->lay->addWidget(label(u"Names"_s));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    {
        int r = 0;
        for (const auto &[key, text] : {std::pair{u"name"_s, u"Name"_s}, std::pair{u"adj"_s, u"Adjective"_s},
                                        std::pair{u"gov"_s, u"Name as"_s}}) {
            auto *ed = new QLineEdit();
            connect(ed, &QLineEdit::editingFinished, this, [this, k = key, ed] { _name_done(k, ed); });
            QLabel *lab = label(text, u"sub"_s, false);
            grid->addWidget(lab, r, 0);
            if (key == u"gov") {
                // one row for every government form: pick the form, see / edit its name; the dropdown lists them all
                auto *host = new QWidget();
                auto *hl = new QHBoxLayout(host);
                hl->setContentsMargins(0, 0, 0, 0);
                hl->setSpacing(6);
                gov_pick = new GovCombo();
                gov_pick->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                gov_pick->setToolTip(u"The nation's name under each form of government — open the list to see them "
                                     u"all; ● = its government at the start"_s);
                connect(gov_pick, &QComboBox::activated, this, [this](int) { _gov_view_changed(); });
                hl->addWidget(gov_pick, 2);
                hl->addWidget(ed, 3);
                grid->addWidget(host, r, 1);
            } else {
                grid->addWidget(ed, r, 1);
            }
            name_edits.push_back({key, {ed, lab}});
            ++r;
        }
    }
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    name_where = label(QString(), u"faint"_s);
    s->lay->addWidget(name_where);
    lay->addWidget(s);

    // --- at the start
    s = start_section = new Section();
    start_title = label(u"At the start"_s);
    s->lay->addWidget(start_title);
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(5);
    int r = 0;
    auto row = [&](const QString &text, QWidget *w) {
        grid->addWidget(label(text, u"sub"_s, false), r, 0);
        grid->addWidget(w, r, 1);
        ++r;
        return w;
    };
    auto *cap_host = new QWidget();
    auto *ch = new QHBoxLayout(cap_host);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(6);
    capital = new dialogs::PickCombo({});
    connect(capital, &QComboBox::activated, this, [this](int) { _scalar(u"capital"_s, capital->key()); });
    cap_pick = new QPushButton(u"Pick on map"_s);
    cap_pick->setToolTip(u"Then click one of the nation's provinces on the map (Esc cancels)"_s);
    connect(cap_pick, &QPushButton::clicked, this, [this] { Q_EMIT capitalPickRequested(); });
    ch->addWidget(capital, 1);
    ch->addWidget(cap_pick);
    row(u"Capital"_s, cap_host);
    primary = new dialogs::PickCombo(dialogs::grouped_cultures(mod));
    row(u"Primary culture"_s, primary);
    connect(primary, &QComboBox::activated, this, [this](int) { _scalar(u"primary_culture"_s, primary->key()); });
    accepted_host = new QWidget();
    accepted_row = new widgets::WrapRow(accepted_host, 4);
    row(u"Accepted"_s, accepted_host);
    add_culture = new dialogs::PickCombo(dialogs::grouped_cultures(mod), {{std::nullopt, u"＋ Accept a culture…"_s}});
    row(QString(), add_culture);
    connect(add_culture, &QComboBox::activated, this, [this](int) { _accept(add_culture->key()); });
    religion = new dialogs::PickCombo(dialogs::grouped_religions(mod));
    row(u"Religion"_s, religion);
    connect(religion, &QComboBox::activated, this, [this](int) { _scalar(u"religion"_s, religion->key()); });
    government = new QComboBox();
    row(u"Government"_s, government);
    connect(government, &QComboBox::activated, this, [this](int) {
        const QVariant d = government->currentData();
        _scalar(u"government"_s, d.isValid() ? OptStr(d.toString()) : std::nullopt);
    });
    auto with_jump = [this](QComboBox *combo, const QString &kind, const QString &tip, bool pic) {
        auto *host = new QWidget();
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(6);
        QLabel *lb = nullptr;
        if (pic) {
            lb = new QLabel();
            lb->setFixedSize(34, 34);
            lb->setAlignment(Qt::AlignCenter);
            hl->addWidget(lb);
        }
        hl->addWidget(combo, 1);
        auto *b = new QPushButton(u"▸"_s);
        b->setFixedWidth(30);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this,
                [this, kind, combo] { Q_EMIT placeRequested(kind, combo->currentData().toString()); });
        hl->addWidget(b);
        return std::pair<QWidget *, QLabel *>{host, lb};
    };
    nv = new QComboBox();
    nv->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    {
        auto [host, lb] = with_jump(nv, u"nv"_s,
                                    u"This national value in Game rules → National values: what it gives, its "
                                    u"picture, what the pop files give for it"_s,
                                    true);
        nv_pic = lb;
        row(u"National value"_s, host);
    }
    connect(nv, &QComboBox::activated, this, [this](int) {
        const QVariant d = nv->currentData();
        _scalar(u"nationalvalue"_s, d.isValid() ? OptStr(d.toString()) : std::nullopt);
        _nv_school();
    });
    school = new QComboBox();
    school->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row(u"Tech school"_s, with_jump(school, u"school"_s, u"This tech school in Technology → Tech schools"_s, false).first);
    school_note = label(QString(), u"faint"_s);
    row(QString(), school_note);
    connect(school, &QComboBox::activated, this, [this](int) {
        const QVariant d = school->currentData();
        _scalar(u"schools"_s, d.isValid() ? OptStr(d.toString()) : std::nullopt);
        _nv_school();
    });
    party = new QComboBox();
    row(u"Ruling party"_s, party);
    connect(party, &QComboBox::activated, this, [this](int) {
        const QVariant d = party->currentData();
        _scalar(u"ruling_party"_s, d.isValid() ? OptStr(d.toString()) : std::nullopt);
    });
    election = new QLineEdit();
    row(u"Last election"_s, election);
    election->setPlaceholderText(u"e.g. 1834.6.21"_s);
    connect(election, &QLineEdit::editingFinished, this, &NationPanel::_election_done);
    civilized = new QCheckBox(u"Civilized"_s);
    row(QString(), civilized);
    connect(civilized, &QCheckBox::clicked, this, [this](bool on) { _scalar(u"civilized"_s, on ? u"yes"_s : u"no"_s); });
    struct Sl {
        const char16_t *key, *text;
        double lo, soft, hard;
        int dec;
    };
    for (const Sl &x : {Sl{u"prestige", u"Prestige", 0, 200, 5000, 0}, Sl{u"plurality", u"Plurality", 0, 100, 100, 0},
                        Sl{u"literacy", u"Literacy", 0, 1, 1, 2},
                        Sl{u"non_state_culture_literacy", u"Literacy (non-state)", 0, 1, 1, 2},
                        Sl{u"consciousness", u"Consciousness", 0, 10, 10, 1},
                        Sl{u"nonstate_consciousness", u"Consciousness (non-state)", 0, 10, 10, 1}}) {
        auto *sf = new widgets::SliderField(x.lo, x.soft, x.hard, 0, QString(), false, x.dec);
        row(QString::fromUtf16(x.text), sf);
        connect(sf, &widgets::SliderField::valueChanged, this, [this, k = QString::fromUtf16(x.key), d = x.dec](double v) {
            _later(k, !d ? QString::number(py::trunc_int(v)) : py::fmt_g(v));
        });
        sliders.emplace_back(QString::fromUtf16(x.key), sf);
    }
    for (QWidget *w : std::initializer_list<QWidget *>{capital, primary, add_culture, religion, government, nv, school,
                                                       party})
        w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    s->lay->addWidget(label(u"Upper house (%)"_s, u"sub"_s));
    grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(3);
    for (int i = 0; i < int(countryhist::UPPER_HOUSE_IDEOLOGIES.size()); ++i) {
        const QString &ideo = countryhist::UPPER_HOUSE_IDEOLOGIES[i];
        grid->addWidget(label(loc_or(mod, ideo), u"sub"_s, false), i, 0);
        auto *sf = new widgets::SliderField(0, 100, 100, 0, QString());
        connect(sf, &widgets::SliderField::valueChanged, this, [this, ideo](double v) { _uh_changed(ideo, v); });
        grid->addWidget(sf, i, 1);
        uh.emplace_back(ideo, sf);
    }
    grid->setColumnStretch(1, 1);
    s->lay->addLayout(grid);
    uh_total = label(QString(), u"sub"_s);
    uh_total->setTextFormat(Qt::RichText);
    s->lay->addWidget(uh_total);
    lay->addWidget(s);

    // --- reforms
    s = reform_section = new Section();              // titled by its "Political reforms" / "Social reforms" groups
    reform_grid = new QGridLayout();
    reform_grid->setHorizontalSpacing(8);
    reform_grid->setVerticalSpacing(4);
    reform_grid->setColumnStretch(1, 1);
    s->lay->addLayout(reform_grid);
    lay->addWidget(s);

    // --- the unciv reforms it has (C++ only)
    s = unciv_section = new Section();
    unciv_title = label(u"Unciv reforms at the start"_s);
    s->lay->addWidget(unciv_title);
    unciv_note = label(u""_s, u"faint"_s);
    unciv_note->setTextFormat(Qt::RichText);
    s->lay->addWidget(unciv_note);
    unciv_view = new reformwin::GameReformView();
    connect(unciv_view, &reformwin::GameReformView::picked, this, [this](const QString &iss, const QString &opt) {
        if (!opt.isEmpty())                            // (a click on an option: the nation has it from the start)
            _reform(iss, opt);
    });
    s->lay->addWidget(unciv_view);
    unciv_list_btn = new QPushButton(u"As a list ▾"_s);
    unciv_list_btn->setCheckable(true);
    s->lay->addWidget(unciv_list_btn);
    unciv_list = new QWidget();
    unciv_grid = new QGridLayout(unciv_list);
    unciv_grid->setContentsMargins(0, 0, 0, 0);
    unciv_grid->setHorizontalSpacing(8);
    unciv_grid->setVerticalSpacing(4);
    unciv_grid->setColumnStretch(1, 1);
    unciv_list->setVisible(false);
    s->lay->addWidget(unciv_list);
    connect(unciv_list_btn, &QPushButton::toggled, this, [this](bool on) {
        unciv_list->setVisible(on);
        unciv_list_btn->setText(on ? u"As a list ▴"_s : u"As a list ▾"_s);
    });
    lay->addWidget(s);

    // --- technologies
    s = tech_section = new Section();
    tech_title = label(u"Technologies"_s);
    s->lay->addWidget(tech_title);
    tree = new TechTree();
    connect(tree, &TechTree::clicked, this, &NationPanel::_tech_clicked);
    s->lay->addWidget(tree);
    s->lay->addSpacing(fontMetrics().lineSpacing() + 4);   // a text row of room under the tech screen
    lay->addWidget(s);
    parties_box = new nationextras::PartiesBox(mod, nullptr, false);   // (the parties themselves: a window of their own)
    lay->addWidget(parties_box);
    flags_box = new nationextras::FlagsBox(mod);
    lay->addWidget(flags_box);
    lay->addStretch(1);
    _cult_seen = mod.cult_version;
}

widgets::SliderField *NationPanel::slider(const QString &key) const
{
    for (const auto &[k, sf] : sliders)
        if (k == key)
            return sf;
    return nullptr;
}

widgets::SliderField *NationPanel::uh_slider(const QString &key) const
{
    for (const auto &[k, sf] : uh)
        if (k == key)
            return sf;
    return nullptr;
}

void NationPanel::_nv_school()
{
    // The picked national value's picture (by its place in the file, as the game takes it) and what the picked tech
    // school gives, in a line.
    scriptstore::ScriptStore *store = scripts_of(doc);
    nv_pic->clear();
    school_note->setText(QString());
    if (!store)
        return;
    try {
        QStringList keys;
        for (const script::Node &n : script::parse(store->text(u"common/nationalvalues.txt"_s)))
            if (n.has_key && !n.key.isEmpty() && n.is_block)
                keys << n.key;
        const auto fr = iconstrips::frames(iconstrips::strips_for(mod, *doc), u"GFX_politics_nationalvalue"_s, 32);
        const qsizetype i = keys.indexOf(nv->currentData().toString());
        if (nv->currentData().isValid() && i >= 0 && i < qsizetype(fr.size()))
            nv_pic->setPixmap(fr[size_t(i)]);
    } catch (const fs::OSError &) {
    }
    try {
        const script::Nodes nodes = script::parse(store->text(u"common/technology.txt"_s));
        const script::Node *top = nullptr;
        for (const script::Node &n : nodes)
            if (n.has_key && n.key == u"schools" && n.is_block) {
                top = &n;
                break;
            }
        const script::Node *sc = nullptr;
        if (top)
            for (const script::Node &c : top->kids)
                if (c.has_key && school->currentData().isValid() && c.key == school->currentData().toString() &&
                    c.is_block) {
                    sc = &c;
                    break;
                }
        QStringList parts;
        if (sc)
            for (const script::Node &c : sc->kids) {
                if (c.is_block)
                    continue;
                const std::optional<double> v = py::to_float(c.value);
                if (!v)
                    continue;
                if (c.has_key && !c.key.isEmpty() && *v)
                    parts << u"<span style='color:%1'>%2 %3%4%</span>"_s.arg(
                        *v > 0 ? u"#7ed07e"_s : u"#ff786e"_s, pretty(QString(c.key).replace(u"_research_bonus"_s, QString())),
                        py::round_int(*v * 100) >= 0 ? u"+"_s : QString(), QString::number(py::round_int(*v * 100)));
            }
        school_note->setText(!parts.isEmpty() ? parts.join(u" · "_s) : (sc ? u"Gives nothing."_s : QString()));
    } catch (const fs::OSError &) {
    }
}

// ------------------------------------------------------------------ build once the game data is there
void NationPanel::_build()
{
    government->clear();
    for (const QString &g : data->governments)
        government->addItem(loc_or(mod, g), g);
    nv->clear();
    for (const QString &v : data->nationalvalues)
        nv->addItem(loc_or(mod, v), v);
    school->clear();
    for (const QString &v : data->schools)
        school->addItem(loc_or(mod, v), v);
    while (reform_grid->count()) {                     // (built again when the Politics tab changed the lists)
        QLayoutItem *it = reform_grid->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    reform_boxes.clear();
    int row = 0;
    OptStr last;
    for (const countryhist::Reform &rf : data->reforms) {
        if (!last || rf.section != *last) {
            QLabel *head = label(rf.section == u"political_reforms" ? u"Political reforms"_s
                                 : rf.section == u"social_reforms"  ? u"Social reforms"_s
                                                                     : pretty(rf.section),
                                 u"groupHead"_s, false);
            if (last) {
                reform_grid->setRowMinimumHeight(row, 10);   // the small gap between the groups
                ++row;
            }
            reform_grid->addWidget(head, row, 0, 1, 2);
            ++row;
            last = rf.section;
        }
        const int i = row;
        ++row;
        auto *cb = new QComboBox();
        cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        for (const QString &o : rf.options)
            cb->addItem(loc_or(mod, o), o);
        connect(cb, &QComboBox::activated, this, [this, k = rf.issue, cb](int) {
            const QVariant d = cb->currentData();
            _reform(k, d.isValid() ? OptStr(d.toString()) : std::nullopt);
        });
        reform_grid->addWidget(label(loc_or(mod, rf.issue), u"sub"_s, false), i, 0);
        reform_grid->addWidget(cb, i, 1);
        reform_boxes.emplace_back(rf.issue, cb);
    }
    while (unciv_grid->count()) {
        QLayoutItem *it = unciv_grid->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    unciv_boxes.clear();
    {
        int r = 0;
        OptStr sec;
        for (const countryhist::Reform &rf : data->unciv) {
            if (!sec || rf.section != *sec) {
                unciv_grid->addWidget(label(rf.section == u"economic_reforms" ? u"Economic reforms"_s
                                            : rf.section == u"military_reforms" ? u"Military reforms"_s
                                                                                 : pretty(rf.section),
                                            u"groupHead"_s, false),
                                      r++, 0, 1, 2);
                sec = rf.section;
            }
            auto *cb = new QComboBox();
            cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            for (const QString &o : rf.options)
                cb->addItem(loc_or(mod, o), o);
            connect(cb, &QComboBox::activated, this, [this, k = rf.issue, cb](int) {
                const QVariant d = cb->currentData();
                _reform(k, d.isValid() ? OptStr(d.toString()) : std::nullopt);
            });
            unciv_grid->addWidget(label(loc_or(mod, rf.issue), u"sub"_s, false), r, 0);
            unciv_grid->addWidget(cb, r++, 1);
            unciv_boxes.emplace_back(rf.issue, cb);
        }
    }
    _unciv_sig.clear();
    tree->set_data(data, &mod.loc);
    _built = true;
}

// ------------------------------------------------------------------ refresh
void NationPanel::refresh(document::Document *doc_, countryhist::NationData *data_, const OptStr &tag_,
                          const Stats &stats)
{
    doc = doc_;
    data = data_;
    scriptstore::ScriptStore *store = scripts_of(doc);
    if (data)                                         // (the store itself, kept alive as Python's reference)
        data->store = store ? std::static_pointer_cast<scriptstore::ScriptStore>(*doc->maps.get(u"scripts"_s)) : nullptr;
    if (data && store) {                              // governments, national values, schools, reforms: as the
        std::optional<QStringList> sig;               // Politics / Technology tabs have them now
        try {
            QStringList texts;
            for (const QString &r : countryhist::NationData::LIST_FILES)
                texts << store->text(r);
            sig = texts;
        } catch (const fs::OSError &) {
            sig.reset();
        }
        if (sig && (!_lists_sig || *_lists_sig != sig)) {
            if ((_lists_sig && *_lists_sig) || !store->dirty().isEmpty()) {
                data->reload_lists(*store);
                _built = false;
            }
            _lists_sig = sig;
        }
    }
    if (data && !_built)
        _build();
    const long long csig = mod.cult_version;
    if (csig != _cult_seen) {                         // cultures / religions came or went: the pickers follow
        primary->fill(dialogs::grouped_cultures(mod));
        add_culture->fill(dialogs::grouped_cultures(mod), {{std::nullopt, u"＋ Accept a culture…"_s}});
        religion->fill(dialogs::grouped_religions(mod));
    }
    _cult_seen = csig;
    tag = tag_ && mod.countries.contains(*tag_) ? tag_ : std::nullopt;
    parties_box->refresh(doc, tag);
    flags_box->setVisible(bool(tag));
    flags_box->refresh(doc, tag);
    _building = true;
    try {
        _fill(stats);
    } catch (...) {
        _building = false;
        throw;
    }
    _building = false;
}

void NationPanel::_fill(const Stats &stats)
{
    countryhist::NationEdits *nations = nations_of(doc);
    std::vector<std::pair<QString, QString>> sig;
    {
        QStringList ts;
        for (const auto &[t, _c] : mod.countries)
            ts << t;
        std::sort(ts.begin(), ts.end());
        for (const QString &t : ts)
            sig.emplace_back(t, mod.countries.get(t)->name);
    }
    if (!_psig || *_psig != sig) {                   // nations added / renamed: fill the picker again
        _psig = sig;
        picker->fill(nation_items(mod), {}, tag);
    }
    const int pi = picker->find(tag);
    picker->blockSignals(true);
    picker->setCurrentIndex(pi >= 0 ? pi : -1);
    picker->blockSignals(false);
    countryhist::CountryHistory *hist = nations ? &nations->hist : nullptr;
    const bool has_file = tag && hist && hist->files.contains(*tag);
    for (QWidget *w : std::initializer_list<QWidget *>{start_section, reform_section, tech_section})
        w->setVisible(has_file);
    unciv_section->setVisible(has_file && data && !data->unciv.empty());
    new_btn->setVisible(bool(tag));
    if (!tag) {
        title->setText(u"Click a province of a nation on the map, or pick one above."_s);
        sub->setText(QString());
        note->setText(QString());
        for (auto &[_k, el] : name_edits) {
            el.first->setEnabled(false);
            el.first->setText(QString());
        }
        name_where->setText(QString());
        return;
    }
    const moddata::Country &c = *mod.countries.get(*tag);
    std::optional<countryhist::NationState> st;
    if (has_file)
        st = nations->state(*tag);
    const OptStr gov = st ? st->at(u"government"_s) : std::nullopt;
    title->setText(u"%1  (%2)"_s.arg(c.name, *tag));
    QStringList cl;
    for (const auto &[n, pct] : stats.cultures)
        cl << u"%1 %2%"_s.arg(n).arg(pct);
    sub->setText(u"<span style='color:rgb(%1,%2,%3)'>■</span> %4 · %5 province%6 in %7 state%8 · %9 people%10"_s
                     .arg(std::get<0>(c.color))
                     .arg(std::get<1>(c.color))
                     .arg(std::get<2>(c.color))
                     .arg(gov && !gov->isEmpty() ? loc_or(mod, *gov) : u"no history"_s)
                     .arg(stats.n_prov)
                     .arg(stats.n_prov == 1 ? QString() : u"s"_s)
                     .arg(stats.n_states)
                     .arg(stats.n_states == 1 ? QString() : u"s"_s)
                     .arg(widgets::fmt(stats.pops), !cl.isEmpty() ? u"<br>"_s + cl.join(u" · "_s) : QString()));
    if (!has_file)
        note->setText(u"%1 has no history file (history/countries/%1 - ….txt) — nothing to edit at the start %2."_s.arg(
            *tag, c.dynamic ? u"(a dynamic tag)"_s : QString()));
    else if (doc->history_editable())
        note->setText(u"Changes are written into the %1 = { } block of %2 on Apply%3."_s.arg(
            doc->scenario, hist->files.get(*tag)->split(u'\\').last().split(u'/').last(),
            c.is_new ? u" (a new nation: made on Apply)"_s : QString()));
    else
        note->setText(u"'%1' is not a date folder: read-only here."_s.arg(doc->scenario));
    // names
    localisation::LocEdits *loc = loc_of(doc);
    OptStr shown_gov;
    if (auto it = _gov_view.find(*tag); it != _gov_view.end() && it->second && !it->second->isEmpty())
        shown_gov = it->second;
    else if (gov && !gov->isEmpty())
        shown_gov = gov;
    else if (data && !data->governments.isEmpty())
        shown_gov = data->governments[0];
    _name_keys = {{u"name"_s, *tag}, {u"adj"_s, *tag + u"_ADJ"_s},
                  {u"gov"_s, shown_gov && !shown_gov->isEmpty() ? OptStr(u"%1_%2"_s.arg(*tag, *shown_gov)) : std::nullopt}};
    _fill_gov_pick(*tag, gov, shown_gov);
    if (loc) {
        QStringList ks;
        for (const auto &[_k, key] : _name_keys)
            if (key && !key->isEmpty())
                ks << *key;
        loc->prefetch(ks);
    }
    // a nation made in this run: its name and adjective reach the localisation on Apply — show them meanwhile
    // (C++ only, 2026-11-14: the Python editor said "not named" for a nation just named in its dialog)
    const countryhist::NewNation *fresh = doc && doc->maps.contains(u"new_nations"_s)
                                              ? doc->map<countryhist::NewNations>(u"new_nations"_s).added.get(*tag)
                                              : nullptr;
    for (size_t i = 0; i < name_edits.size(); ++i) {
        QLineEdit *ed = name_edits[i].second.first;
        const OptStr &key = _name_keys[i].second;
        const bool k = key && !key->isEmpty();
        ed->setEnabled(k && loc);
        ed->setText(k && loc ? loc->text(*key).value_or(QString()) : QString());
        const QString pend = !fresh ? QString() : i == 0 ? fresh->name : i == 1 ? fresh->adj : QString();
        ed->setPlaceholderText(!k ? QString()
                               : !pend.isEmpty() ? u"%1  (written on Apply)"_s.arg(pend)
                                                 : u"not named (the game shows %1)"_s.arg(*key));
    }
    {
        QStringList w;
        for (const auto &[_k, key] : _name_keys)
            if (key && !key->isEmpty() && loc)
                w << u"%1 → %2"_s.arg(*key, loc->where(*key));
        name_where->setText(u"Localisation: "_s + w.join(u" · "_s));
    }
    if (!st)
        return;
    const bool editable = doc->history_editable();
    for (const auto &[_k, sf] : sliders)
        sf->setEnabled(editable);
    for (const auto &[_k, sf] : uh)
        sf->setEnabled(editable);
    for (const auto &[_k, cb] : reform_boxes)
        cb->setEnabled(editable);
    for (QWidget *w : std::initializer_list<QWidget *>{capital, cap_pick, primary, add_culture, religion, government, nv,
                                                       school, party, election, civilized})
        w->setEnabled(editable);
    tree->enabled_edit = editable;
    start_title->setText(u"At the start (%1)"_s.arg(doc->scenario));
    // capital: the provinces it owns (plus the one written, if it owns none of them)
    const std::vector<long long> owned = stats_owned(*doc, mod, *tag);
    const OptStr capv = st->at(u"capital"_s);
    std::optional<long long> cap;
    if (capv && !capv->isEmpty() && py::isdigit(*capv))
        cap = py::to_int(*capv);
    std::vector<long long> by_name = owned;
    std::stable_sort(by_name.begin(), by_name.end(),
                     [this](long long a, long long b) { return mod.provinces.get(a)->name < mod.provinces.get(b)->name; });
    std::vector<dialogs::PickItem> items;
    for (long long p : by_name)
        items.push_back({QString::number(p), u"#%1 %2"_s.arg(p).arg(mod.provinces.get(p)->name), std::nullopt, std::nullopt});
    if (cap && std::find(owned.begin(), owned.end(), *cap) == owned.end() && mod.provinces.contains(*cap))
        items.insert(items.begin(), {QString::number(*cap),
                                     u"#%1 %2 — not owned by %3!"_s.arg(*cap).arg(mod.provinces.get(*cap)->name, *tag),
                                     std::nullopt, std::nullopt});
    capital->fill(items, {}, cap ? OptStr(QString::number(*cap)) : std::nullopt);
    _select(primary, st->at(u"primary_culture"_s));
    _select(religion, st->at(u"religion"_s));
    _select(government, st->at(u"government"_s));
    _select(nv, st->at(u"nationalvalue"_s));
    _select(school, st->at(u"schools"_s));
    _nv_school();
    party->clear();
    for (const countryhist::Party &pt : data->parties(*tag))
        party->addItem(u"%1  (%2, %3–%4)"_s.arg(loc_or(mod, pt.name), pt.ideology, pt.start.left(4), pt.end.left(4)),
                       pt.name);
    _select(party, st->at(u"ruling_party"_s));
    election->setText(st->at(u"last_election"_s).value_or(QString()));
    civilized->setChecked(py::lower(st->at(u"civilized"_s).value_or(u"None"_s)) == u"yes");
    for (const auto &[k, sf] : sliders) {
        sf->blockSignals(true);
        const OptStr v = st->at(k);
        sf->setValue(v && !v->isEmpty() ? py::to_float(*v).value_or(0) : 0);
        sf->blockSignals(false);
    }
    // accepted cultures: chips with ✕
    while (accepted_row->count()) {
        QLayoutItem *it = accepted_row->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete it;
    }
    for (const QString &cul : st->cultures) {
        const moddata::Culture *cu = mod.cultures.get(cul);
        auto *b = new QPushButton(u"%1  ✕"_s.arg(cu ? cu->name : cul));
        b->setToolTip(u"%1 — click to no longer accept it"_s.arg(cul));
        b->setStyleSheet(u"QPushButton { padding: 3px 8px; }"_s);
        connect(b, &QPushButton::clicked, this, [this, cul] { _unaccept(cul); });
        accepted_row->addWidget(b);
    }
    if (st->cultures.isEmpty())
        accepted_row->addWidget(label(u"none"_s, u"faint"_s, false));
    accepted_host->updateGeometry();
    add_culture->setCurrentIndex(0);
    // upper house
    double tot = 0;
    for (const auto &[ideo, sf] : uh) {
        const QString *v = st->upper_house.get(ideo);
        sf->blockSignals(true);
        sf->setValue(v && !v->isEmpty() ? py::to_float(*v).value_or(0) : 0);
        sf->blockSignals(false);
        tot += sf->value();
    }
    uh_total->setText(tot == 100 ? u"Total %1%"_s.arg(py::trunc_int(tot))
                                 : u"<span style='color:%1'>⚠ Total %2% — should be 100%</span>"_s.arg(
                                       theme::ERROR_).arg(py::trunc_int(tot)));
    // reforms
    for (const auto &[issue, cb] : reform_boxes) {
        const QString *o = st->reforms.get(issue);
        _select(cb, o ? OptStr(*o) : std::nullopt);
    }
    _fill_unciv(*st);
    // techs
    tree->set_owned(st->techs);
    tech_title->setText(u"Technologies — %1"_s.arg(data->folder_counts(st->techs)));
}

void NationPanel::_select(QComboBox *combo, const OptStr &key)
{
    int i;
    if (auto *pc = qobject_cast<dialogs::PickCombo *>(combo))
        i = pc->find(key);
    else
        i = key ? combo->findData(*key) : combo->findData(QVariant());
    combo->blockSignals(true);
    combo->setCurrentIndex(i);
    combo->blockSignals(false);
}

// ------------------------------------------------------------------ edits
void NationPanel::_emit(const QString &text, const NationFn &fn)
{
    if (_building || !tag || !doc || !doc->history_editable())
        return;
    Q_EMIT edit(text, *tag, fn);
}

void NationPanel::set_capital(long long pid)
{
    _scalar(u"capital"_s, QString::number(pid));   // the capital picked on the map
}

void NationPanel::_scalar(const QString &key, const OptStr &value)
{
    if (!value || value->isEmpty())
        return;
    const QString v = *value;
    _emit(u"%1: %2 → %3"_s.arg(tag.value_or(u"None"_s), key, v),
          [key, v](countryhist::NationState &st) { st[key] = v; });
}

void NationPanel::_later(const QString &key, const QString &value)
{
    // Sliders: one undo step once the value has settled.
    if (_building)
        return;
    _pending[key] = value;
    _timer->start();
}

void NationPanel::_uh_changed(const QString &key, double value)
{
    // One upper-house share moved: the others go up / down in proportion so the total stays 100 %.
    if (_building)
        return;
    QStringList others;
    for (const QString &i : countryhist::UPPER_HOUSE_IDEOLOGIES)
        if (i != key)
            others << i;
    if (_uh_drag != OptStr(key) || !_timer->isActive()) {
        _uh_drag = key;                              // the others' shares at the start of this move
        _uh_base.clear();
        for (const QString &i : others)
            _uh_base[i] = uh_slider(i)->value();
    }
    std::vector<double> base;
    for (const QString &i : others)
        base.push_back(_uh_base.value(i));
    std::vector<double> weights = py::sum(base) > 0 ? base : std::vector<double>(size_t(others.size()), 1.0);
    const long long rest = std::max<long long>(0, 100 - py::round_int(value));
    const std::vector<long long> parts = document::distribute(rest, weights);
    for (qsizetype j = 0; j < others.size() && j < qsizetype(parts.size()); ++j) {
        widgets::SliderField *sf = uh_slider(others[j]);
        sf->blockSignals(true);
        sf->setValue(double(parts[size_t(j)]));
        sf->blockSignals(false);
    }
    long long total = 0;
    std::vector<double> vals;
    for (const QString &i : countryhist::UPPER_HOUSE_IDEOLOGIES) {
        _pending[u"uh:"_s + i] = QString::number(py::trunc_int(uh_slider(i)->value()));
        vals.push_back(uh_slider(i)->value());
    }
    total = py::trunc_int(py::sum(vals));
    uh_total->setText(u"Total %1%"_s.arg(total));
    _timer->start();
}

void NationPanel::_flush()
{
    OrderedMap<QString, QString> pend = _pending;
    _pending = {};
    if (pend.isEmpty())
        return;
    QStringList parts;
    for (const auto &[k, v] : pend)
        parts << u"%1 = %2"_s.arg(QString(k).replace(u"uh:"_s, u"upper house "_s), v);
    _emit(u"%1: %2"_s.arg(tag.value_or(u"None"_s), parts.join(u", "_s)), [pend](countryhist::NationState &st) {
        for (const auto &[k, v] : pend) {
            if (k.startsWith(u"uh:"_s))
                st.upper_house[k.mid(3)] = v;
            else
                st[k] = v;
        }
    });
}

void NationPanel::_election_done()
{
    const QString t = py::strip(election->text());
    if (py::re_match(DATE_RE, t).hasMatch())
        _scalar(u"last_election"_s, t);
}

void NationPanel::_accept(const OptStr &cul)
{
    if (!cul || cul->isEmpty())
        return;
    const QString c = *cul;
    _emit(u"%1 accepts %2"_s.arg(tag.value_or(u"None"_s), c), [c](countryhist::NationState &st) {
        if (!st.cultures.contains(c))
            st.cultures.append(c);
    });
}

void NationPanel::_unaccept(const QString &cul)
{
    _emit(u"%1 no longer accepts %2"_s.arg(tag.value_or(u"None"_s), cul), [cul](countryhist::NationState &st) {
        if (st.cultures.contains(cul))
            st.cultures.removeAt(st.cultures.indexOf(cul));
    });
}

NationPanel::~NationPanel() = default;

// the unciv reforms of this nation: the game's window with its options ticked (the first option where the history
// names none: the game's default), the list beside it, how far they bring it towards westernising
void NationPanel::_fill_unciv(const countryhist::NationState &st)
{
    if (!data || data->unciv.empty())
        return;
    const bool civ = py::lower(st.at(u"civilized"_s).value_or(u"None"_s)) == u"yes";
    OrderedMap<QString, QString> issues, names, current;
    OrderedMap<QString, std::vector<reformwin::Option>> options;
    long long taken = 0;
    double progress = 0;
    for (const countryhist::Reform &rf : data->unciv) {
        const QString *has = st.reforms.get(rf.issue);
        const QString now = has && rf.options.contains(*has) ? *has : rf.options.front();
        issues.insert(rf.issue, rf.section);
        names.insert(rf.issue, loc_or(mod, rf.issue));
        current.insert(rf.issue, loc_or(mod, now));
        std::vector<reformwin::Option> ol;
        for (const QString &o : rf.options)
            ol.emplace_back(loc_or(mod, o), o == now, o);
        options.insert(rf.issue, ol);
        if (now != rf.options.front())
            ++taken;
        // the progress the options up to the one it has bring (a reform of steps: every step before counts too)
        for (const QString &o : rf.options) {
            if (const double *v = data->unciv_progress.get(o))
                progress += *v;
            if (o == now)
                break;
        }
    }
    for (const auto &[issue, cb] : unciv_boxes) {           // (none named: the first option, the game's default)
        const QString *o = st.reforms.get(issue);
        _select(cb, o ? OptStr(*o) : cb->count() ? OptStr(cb->itemData(0).toString()) : std::nullopt);
    }
    unciv_title->setText(u"Unciv reforms at the start — %1 of %2 taken"_s.arg(taken).arg(data->unciv.size()));
    unciv_note->setText(
        (civ ? u"<span style='color:%1'>This nation is civilized: the game uses its unciv reforms only while a nation is "
               u"uncivilized.</span><br>"_s.arg(theme::GOLD)
             : QString()) +
        u"Click an option to give it to this nation from the start. Their westernisation from these reforms: "
        u"<b>%1%</b>. What a reform does: Politics → Unciv reforms."_s.arg(QString::number(std::round(progress * 1000) / 10)));
    // the game's window (made once per set of interface files)
    scriptstore::ScriptStore *store = scripts_of(doc);
    if (!store)
        return;
    const QString part = u"unciv_reforms_window"_s;
    if (!_look || _look->store != store) {
        _look = std::make_unique<locwin::GameLook>(mod, store);
        _unciv_sig.clear();
    }
    const QString gui = store->text(reformwin::REL);
    static std::optional<QImage> img;           // (one Nation tab: kept until the interface files change)
    static reformwin::XY off{0.0, 0.0};
    static OrderedMap<QString, reformwin::XY> places;
    static QRectF shown;
    if (_unciv_sig != gui) {
        _unciv_sig = gui;
        img.reset();
        off = {0.0, 0.0};
        try {
            shortcuts::Projected got = shortcuts::project(*_look, reformwin::REL, u"country_politics"_s, {part}, true,
                                                          false, 0, part);
            img = got.img;
            if (got.meta)
                off = {got.meta->x0, got.meta->y0};
        } catch (...) {
            img.reset();
        }
        places = reformwin::slots(gui, part);
        locwin::GameLook *look = _look.get();
        shown = reformwin::extent(gui, part, [look](const QString &sp) -> std::optional<QSizeF> {
            const std::optional<QImage> im = look->sprite(sp);
            if (!im || im->isNull())
                return std::nullopt;
            return QSizeF(im->size());
        });
        unciv_view->lay = reformwin::layout(gui);
        unciv_view->place_info = reformwin::places_of(gui, part);
    }
    unciv_view->setVisible(img.has_value());
    if (!img && !unciv_list_btn->isChecked())
        unciv_list_btn->setChecked(true);           // (no window to draw: the list instead)
    unciv_view->set_data(img, off, places, issues, names, current, std::nullopt, options, shown, _look.get());
}

void NationPanel::_reform(const QString &issue, const OptStr &opt)
{
    const QString o = opt.value_or(QString());
    _emit(u"%1: %2 → %3"_s.arg(tag.value_or(u"None"_s), issue, opt.value_or(u"None"_s)),
          [issue, o](countryhist::NationState &st) { st.reforms[issue] = o; });
}

void NationPanel::_tech_clicked(const QString &tech)
{
    const countryhist::TechInfo &info = *data->techs.get(tech);
    const QStringList line = data->area_techs.value(info.area);
    const qsizetype k = line.indexOf(tech);
    const QSet<QString> has = nations_of(doc)->state(*tag).techs;
    qsizetype last_owned = -1;
    for (qsizetype i = 0; i < line.size(); ++i)
        if (has.contains(line[i]))
            last_owned = i;
    if (has.contains(tech) && k == last_owned) {
        const QSet<QString> take(line.begin() + k, line.end());   // its last one: take it away (and anything after it)
        _emit(u"%1: without %2"_s.arg(*tag, tech), [take](countryhist::NationState &st) { st.techs -= take; });
    } else {
        const QSet<QString> give(line.begin(), line.begin() + k + 1), take(line.begin() + k + 1, line.end());
        _emit(u"%1: %2 up to %3"_s.arg(*tag, info.area, tech), [give, take](countryhist::NationState &st) {
            st.techs |= give;
            st.techs -= take;
        });
    }
}

void NationPanel::_fill_gov_pick(const QString &t, const OptStr &gov, const OptStr &shown)
{
    // Every government form with the nation's name under it (the dropdown shows them all at once).
    localisation::LocEdits *loc = loc_of(doc);
    GovCombo *cb = gov_pick;
    cb->blockSignals(true);
    cb->clear();
    const QStringList govs = data ? data->governments : QStringList();
    if (loc) {
        QStringList ks;
        for (const QString &g : govs)
            ks << u"%1_%2"_s.arg(t, g);
        loc->prefetch(ks);
    }
    for (const QString &g : govs) {
        const OptStr nm = loc ? loc->text(u"%1_%2"_s.arg(t, g)) : std::nullopt;
        const QString gname = loc_or(mod, g);
        cb->addItem(u"%1%2 — %3"_s.arg(gov && g == *gov ? u"● "_s : QString(), gname,
                                       nm && !nm->isEmpty() ? *nm : u"not named"_s),
                    g);
        cb->setItemData(cb->count() - 1, gname, GovCombo::SHORT);
    }
    const int i = shown ? cb->findData(*shown) : cb->findData(QVariant());
    cb->setCurrentIndex(i >= 0 ? i : 0);
    cb->setEnabled(!govs.isEmpty() && loc);
    cb->blockSignals(false);
}

void NationPanel::_gov_view_changed()
{
    if (!tag)
        return;
    const QVariant d = gov_pick->currentData();
    const OptStr g = d.isValid() ? OptStr(d.toString()) : std::nullopt;
    _gov_view[*tag] = g;
    localisation::LocEdits *loc = loc_of(doc);
    const QString key = u"%1_%2"_s.arg(*tag, g.value_or(u"None"_s));
    _name_keys[2].second = key;
    QLineEdit *ed = name_edits[2].second.first;
    ed->setText(loc ? loc->text(key).value_or(QString()) : QString());
    ed->setPlaceholderText(u"not named (the game shows %1)"_s.arg(key));
    QStringList w;
    for (const auto &[_k, k] : _name_keys)
        if (k && !k->isEmpty() && loc)
            w << u"%1 → %2"_s.arg(*k, loc->where(*k));
    name_where->setText(u"Localisation: "_s + w.join(u" · "_s));
}

void NationPanel::_name_done(const QString &k, QLineEdit *ed)
{
    OptStr key;
    for (const auto &[kk, v] : _name_keys)
        if (kk == k)
            key = v;
    localisation::LocEdits *loc = loc_of(doc);
    const QString text = py::strip(ed->text());
    if (!key || key->isEmpty() || !loc || text.isEmpty() || text == loc->text(*key).value_or(QString()))
        return;
    Q_EMIT nameRequested(*key, text, u"country"_s);
}

}  // namespace nationpanel
