// buildtypes — see buildtypes.h (C++ only, 2026-11-16).
#include "ui/buildtypes.h"
#include "core/blocks.h"
#include "core/history.h"
#include "core/localisation.h"
#include "core/mapfiles.h"
#include "core/provbuild.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/scriptstore.h"
#include "core/techdata.h"
#include "map/iconstrips_data.h"
#include "ui/iconstrips.h"
#include "map/imgio.h"
#include "map/gamefont.h"
#include "ui/dialogs.h"
#include "ui/locwin.h"
#include "ui/moreviews.h"
#include "ui/shortcuts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QFileDialog>
#include <QDropEvent>
#include <QTimer>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QToolButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;

namespace buildtypes {

namespace pb = provbuild;
using widgets::label;
using widgets::Section;
using widgets::Stepper;

const QString BUILDINGS = u"common/buildings.txt"_s;
const QString GFX = u"interface/modeditor_buildings.gfx"_s;
static const QString BG_SPRITE = u"GFX_provinceview_bg_modeditor"_s;
static const QString BG_REL = u"gfx/interface/province_bg_modeditor.tga"_s;
static QString strip_sprite(const QString &k) { return u"GFX_province_%1_strip"_s.arg(k); }
static QString strip_rel(const QString &k) { return u"gfx/interface/province_%1_strip.tga"_s.arg(k); }

static QString name_of(const moddata::Mod &mod, document::Document *doc, const QString &key);

QString BuildTypesPanel::gui_rel() const { return u"interface/province_interface.gui"_s; }

static QPushButton *small_btn(const QString &t, const QString &tip = QString())
{
    auto *b = new QPushButton(t);
    b->setCursor(Qt::PointingHandCursor);
    if (!tip.isEmpty())
        b->setToolTip(tip);
    return b;
}
static void clear(QLayout *lay)
{
    while (QLayoutItem *it = lay->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();                             // (gone at once: deleteLater draws it one more time)
            w->deleteLater();
        }
        else if (QLayout *l = it->layout())
            clear(l);
        delete it;
    }
}
static const QString RED = u"QLabel { color: #ff8a7a; }"_s;
static const QString GREEN = u"QLabel { color: #8fd18f; }"_s;

BuildTypesPanel::BuildTypesPanel(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // --- the list
    auto *s = new Section();
    s->lay->addWidget(label(u"Building types"_s, u"title"_s));
    intro = label(u"The buildings of a province (its window in the game): the game's fort, naval base and railroad, and "
                  u"the mod's own. A new one gets technologies that unlock its levels and a row in the game's province "
                  u"window. A province's level of it: Provinces & states → Provinces."_s,
                  u"faint"_s);
    s->lay->addWidget(intro);
    tree = new QTreeWidget();
    tree->setColumnCount(4);
    tree->setHeaderLabels({u"Building"_s, u"Levels"_s, u"Techs"_s, u"Provinces"_s});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    QHeaderView *h = tree->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        h->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    tree->setTextElideMode(Qt::ElideRight);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // every building shown, no scroll bar (the user 2026-11-17: with 7 buildings the bar went unseen and one seemed
    // missing): the list is as tall as its rows, set in _fill_tree
    tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] { _picked(); });
    s->lay->addWidget(tree);
    count = label(QString(), u"faint"_s);
    s->lay->addWidget(count);
    auto *nr = new QHBoxLayout();
    nr->setSpacing(6);
    new_name = new QLineEdit();
    new_name->setPlaceholderText(u"Name of a new building, e.g. Bank"_s);
    connect(new_name, &QLineEdit::returnPressed, this, [this] { _make_new(); });
    nr->addWidget(new_name, 1);
    auto *nb = small_btn(u"＋ New building type"_s,
                         u"A new province building, with the picked one's goods, build time and levels (no effects yet). "
                         u"Its row in the game's province window is made with it."_s);
    connect(nb, &QPushButton::clicked, this, [this] { _make_new(); });
    nr->addWidget(nb);
    s->lay->addLayout(nr);
    new_warn = label(QString(), u"warn"_s);
    new_warn->hide();
    s->lay->addWidget(new_warn);
    lay->addWidget(s);

    // --- the game's province window (the user, 2026-11-16: "a way to manage it dynamically (placements of the
    // buildings)"; "it also needs to expand according to its need ... the same as the logic with the National focus rows")
    auto *ws = new Section();
    ws->lay->addWidget(label(u"The province window"_s, u"label"_s));
    ws->lay->addWidget(label(u"As the game draws it, from the mod's files. Drag a row up or down to move its building "
                             u"there. The window grows and shrinks with the rows; when it would reach into the top bar of "
                             u"a 768-high screen the rows are made lower (as the national focus window's)."_s,
                             u"faint"_s));
    win_view = new WindowView();
    connect(win_view, &WindowView::picked, this, [this](const QString &k) {
        cur = k;                                   // (the picture stays as it is: a drag may be starting)
        _fill_tree();
        _fill_detail();
    });
    connect(win_view, &WindowView::moved, this, [this](const QString &k, int to) { _move(k, to); });
    ws->lay->addWidget(win_view);
    auto *hr = new QHBoxLayout();
    hr->setSpacing(8);
    hr->addWidget(label(u"Row height (px)"_s, u"sub"_s, false));
    row_h = new Stepper(provbuild::MIN_ROW, 50, 35);
    connect(row_h, &Stepper::valueChanged, this, [this](int v) {
        if (!_filling)
            _row_height(v);
    });
    hr->addWidget(row_h);
    hr->addStretch(1);
    ws->lay->addLayout(hr);
    win_note = label(QString(), u"sub"_s);
    ws->lay->addWidget(win_note);
    lay->addWidget(ws);

    // --- the picked one
    detail = new Section();
    title = label(QString(), u"title"_s);
    detail->lay->addWidget(title);
    sub = label(QString(), u"faint"_s);
    detail->lay->addWidget(sub);
    auto *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    name_edit = new QLineEdit();
    connect(name_edit, &QLineEdit::editingFinished, this, [this] {
        if (cur && !_filling && name_edit->isModified()) {
            name_edit->setModified(false);
            Q_EMIT nameEdit(*cur, name_edit->text());
        }
    });
    g->addWidget(label(u"Name in the game"_s, u"sub"_s, false), 0, 0);
    g->addWidget(name_edit, 0, 1);
    max_level = new Stepper(1, 99, 1);
    connect(max_level, &Stepper::valueChanged, this, [this](int v) {
        if (!_filling)
            _set(u"Max level %1"_s.arg(v), u"max_level"_s, QString::number(v));
    });
    g->addWidget(label(u"Highest level"_s, u"sub"_s, false), 1, 0);
    g->addWidget(max_level, 1, 1, Qt::AlignLeft);
    time = new Stepper(1, 9999, 365);
    connect(time, &Stepper::valueChanged, this, [this](int v) {
        if (!_filling)
            _set(u"Build time %1 days"_s.arg(v), u"time"_s, QString::number(v));
    });
    g->addWidget(label(u"Build time (days)"_s, u"sub"_s, false), 2, 0);
    g->addWidget(time, 2, 1, Qt::AlignLeft);
    g->setColumnStretch(1, 1);
    detail->lay->addLayout(g);
    // (the user 2026-11-18: the levels, the pictures and the technologies' levels must be equal — said here, in red)
    lvl_err = label(QString(), u"sub"_s);
    lvl_err->setStyleSheet(u"QLabel { color: #ffffff; background: %1; border-radius: 4px; padding: 6px 10px; "
                           u"font-weight: 600; }"_s.arg(theme::DANGER));
    lvl_err->hide();
    detail->lay->addWidget(lvl_err);

    // goods it costs
    detail->lay->addWidget(label(u"Goods it costs (per level)"_s, u"label"_s));
    cost_host = new QWidget();
    cost_lay = new QVBoxLayout(cost_host);
    cost_lay->setContentsMargins(0, 0, 0, 0);
    cost_lay->setSpacing(3);
    detail->lay->addWidget(cost_host);
    add_good = new dialogs::PickCombo({}, {{std::nullopt, u"＋ Add a good…"_s}});
    add_good->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(add_good, &QComboBox::activated, this, [this](int) {
        if (const OptStr gk = add_good->key(); gk && cur) {
            const QString t = blocks::set_value(text(BUILDINGS), {*cur, u"goods_cost"_s}, *gk, u"10"_s);
            _edit(u"%1: costs %2"_s.arg(_name(*cur), *gk), BUILDINGS, t);
        }
    });
    detail->lay->addWidget(add_good);

    // effects
    detail->lay->addWidget(label(u"Effects (each level)"_s, u"label"_s));
    fx_note = label(QString(), u"faint"_s);
    detail->lay->addWidget(fx_note);
    fx_host = new QWidget();
    fx_lay = new QVBoxLayout(fx_host);
    fx_lay->setContentsMargins(0, 0, 0, 0);
    fx_lay->setSpacing(3);
    detail->lay->addWidget(fx_host);
    add_fx = new dialogs::PickCombo({}, {{std::nullopt, u"＋ Add an effect…"_s}});
    add_fx->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(add_fx, &QComboBox::activated, this, [this](int) {
        if (const OptStr k = add_fx->key(); k && cur) {
            const QString t = blocks::set_value(text(BUILDINGS), {*cur}, *k, u"0.1"_s);
            _edit(u"%1: effect %2"_s.arg(_name(*cur), *k), BUILDINGS, t);
        }
    });
    detail->lay->addWidget(add_fx);

    // technologies
    detail->lay->addWidget(label(u"Unlocked by technology"_s, u"label"_s));
    tech_note = label(QString(), u"faint"_s);
    detail->lay->addWidget(tech_note);
    tech_host = new QWidget();
    tech_lay = new QVBoxLayout(tech_host);
    tech_lay->setContentsMargins(0, 0, 0, 0);
    tech_lay->setSpacing(3);
    detail->lay->addWidget(tech_host);
    add_tech = new dialogs::PickCombo({}, {{std::nullopt, u"＋ A technology unlocks a level…"_s}});
    add_tech->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(add_tech, &QComboBox::activated, this, [this](int) {
        if (const OptStr k = add_tech->key())
            _tech(*k, +1);
    });
    detail->lay->addWidget(add_tech);

    // the game's province window
    detail->lay->addWidget(label(u"In the game's province window"_s, u"label"_s));
    slot_note = label(QString(), u"sub"_s);
    detail->lay->addWidget(slot_note);
    room_note = label(QString(), u"sub"_s);
    detail->lay->addWidget(room_note);
    slot_btn = small_btn(u"Add the missing picture slots"_s,
                         u"Writes into interface/province_interface.gui (a copy of the game's in the mod): a picture slot "
                         u"build_iconN for every province building that has none, each with a picture strip of its own "
                         u"(gfx/interface, named in interface/modeditor_buildings.gfx), and makes the window as tall as "
                         u"its rows. The same that happens when you make a building on this page."_s);
    connect(slot_btn, &QPushButton::clicked, this, [this] { fix_window(); });
    detail->lay->addWidget(slot_btn, 0, Qt::AlignLeft);

    // its picture: the strip, one picture per level
    detail->lay->addWidget(label(u"Its picture (one per level)"_s, u"label"_s));
    size_lbl = iconstrips::size_sign(std::nullopt);
    detail->lay->addWidget(size_lbl);
    {
        auto *ir = new widgets::WrapRow(nullptr, 6);
        imp_rep_btn = new QPushButton(u"Import a picture for this building level"_s);
        imp_rep_btn->setObjectName(u"primary"_s);
        imp_rep_btn->setToolTip(u"Pick a picture file on your computer: it becomes the picture of the level picked below "
                                u"(scaled whole into the size above) — on Apply."_s);
        connect(imp_rep_btn, &QPushButton::clicked, this, [this] { _frame_import(0); });
        lib_one_btn = new QPushButton(u"Put this picture into my library"_s);
        lib_one_btn->setToolTip(u"Saves the picked level's picture as a .png file into your library folder, to use it "
                                u"in any mod."_s);
        connect(lib_one_btn, &QPushButton::clicked, this, [this] { _to_library(false); });
        lib_all_btn = new QPushButton(u"Put all pictures into my library"_s);
        lib_all_btn->setToolTip(u"Saves the picture of every level as a .png file into your library folder."_s);
        connect(lib_all_btn, &QPushButton::clicked, this, [this] { _to_library(true); });
        ir->addWidget(imp_rep_btn);
        ir->addWidget(lib_one_btn);
        ir->addWidget(lib_all_btn);
        auto *irw = new QWidget();
        irw->setLayout(ir);
        detail->lay->addWidget(irw);
    }
    pic_note = label(QString(), u"faint"_s);
    detail->lay->addWidget(pic_note);
    frames_host = new QWidget();
    // (at most 12 pictures a row: a 51-picture strip would otherwise ask for 4000 px of width and widen the window)
    frames_lay = new widgets::WrapRow(frames_host, 6, 12);
    frames_host->setAcceptDrops(true);              // (a level's picture dragged to another place)
    frames_host->installEventFilter(this);
    detail->lay->addWidget(frames_host);
    {   // as the boards of national values and focuses: earlier / later, and delete on the right
        auto *dr = new QHBoxLayout();
        dr->setSpacing(6);
        for (const auto &[txt, d] : {std::pair{u"◀ Earlier"_s, -1}, std::pair{u"Later ▶"_s, 1}}) {
            auto *b = new QPushButton(txt);
            b->setToolTip(u"The picked level's picture one place earlier / later (or drag it)"_s);
            connect(b, &QPushButton::clicked, this, [this, dd = d] { _frame_move(frame_sel, frame_sel + dd); });
            dr->addWidget(b);
        }
        dr->addStretch(1);
        cut_btn = new QPushButton(u"Delete this building level"_s);
        cut_btn->setToolTip(u"The picked level goes at once: its picture out of the strip, the highest level one lower. "
                            u"Ctrl+Z brings it back. A right click on a level's picture does the same."_s);
        connect(cut_btn, &QPushButton::clicked, this, [this] { _delete_level(frame_sel); });
        dr->addWidget(cut_btn);
        detail->lay->addLayout(dr);
    }
    frame_note = label(QString(), u"sub"_s);
    detail->lay->addWidget(frame_note);
    lv_btn = new QPushButton();
    lv_btn->setObjectName(u"primary"_s);
    lv_btn->hide();
    connect(lv_btn, &QPushButton::clicked, this, [this] {
        if (frames_n > 1)
            max_level->setValue(frames_n - 1);    // (one level per picture after level 0; its technologies: above)
    });
    detail->lay->addWidget(lv_btn, 0, Qt::AlignLeft);
    lib = new QComboBox();
    lib->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    detail->lay->addWidget(lib);
    auto *fr = new widgets::WrapRow(nullptr, 6);
    rep_btn = small_btn(u"Use it for this building level"_s,
                        u"The library picture chosen in the list becomes the picture of the level picked above (scaled "
                        u"whole into the picture's size, centred)."_s);
    connect(rep_btn, &QPushButton::clicked, this, [this] { _frame_edit(0); });
    pic_btn = small_btn(u"Make a new strip from it"_s,
                        u"The library picture becomes the building's whole strip: one picture per level, greyed for "
                        u"level 0, the level's number in the corner (as the game's own strips)."_s);
    connect(pic_btn, &QPushButton::clicked, this, [this] { _own_picture(); });
    for (QPushButton *b : {rep_btn, pic_btn})
        fr->addWidget(b);
    auto *frw = new QWidget();
    frw->setLayout(fr);
    detail->lay->addWidget(frw);

    // a province's level of it (the page left Provinces & states on 2026-11-17: no map here)
    detail->lay->addWidget(label(u"Provinces"_s, u"label"_s));
    prov_note = label(QString(), u"faint"_s);
    detail->lay->addWidget(prov_note);

    // taking it out
    del_note = label(QString(), u"faint"_s);
    detail->lay->addWidget(del_note);
    del_btn = small_btn(u"✕ Remove this building type"_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { _remove(); });
    detail->lay->addWidget(del_btn, 0, Qt::AlignLeft);
    lay->addWidget(detail);
    lay->addStretch(1);
}

// ---------------------------------------------------------------------------------------------- reading
QString BuildTypesPanel::text(const QString &rel)
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return QString();
    return doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(rel);
}

QStringList BuildTypesPanel::keys() { return pb::province_keys(text(BUILDINGS)); }

std::vector<std::pair<QString, QString>> BuildTypesPanel::tech_files()
{
    std::vector<std::pair<QString, QString>> out;
    for (const moddata::LayeredFile &lf : mod.layered({u"technologies"_s}))
        if (py::lower(lf.rel).endsWith(u".txt") && !lf.rel.contains(u'/'))
            out.emplace_back(u"technologies/"_s + lf.rel, text(u"technologies/"_s + lf.rel));
    return out;
}

long long BuildTypesPanel::holders(const QString &key)
{
    long long n = 0;
    if (!doc)
        return 0;
    for (const auto &[pid, p] : mod.provinces)
        if (!p.sea && doc->prov_state(pid).level_of(key) > 0)
            ++n;
    return n;
}

QString BuildTypesPanel::_name(const QString &key) { return name_of(mod, doc, key); }

std::vector<std::pair<QString, QString>> BuildTypesPanel::window_problems()
{
    std::vector<std::pair<QString, QString>> out;
    for (const auto &p : pb::window_problems(text(BUILDINGS), text(gui_rel()), [this](const QString &k) { return _name(k); }))
        out.emplace_back(p.key, p.text + u"."_s);
    return out;
}

// ---------------------------------------------------------------------------------------------- pictures
static QString store_text(document::Document *doc, const QString &rel)
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return QString();
    return doc->map<scriptstore::ScriptStore>(u"scripts"_s).text(rel);
}

static QString name_of(const moddata::Mod &mod, document::Document *doc, const QString &key)
{
    if (doc && doc->maps.contains(u"loc"_s))
        if (const auto t = doc->map<localisation::LocEdits>(u"loc"_s).text(key); t && !t->isEmpty())
            return *t;
    if (const QString *l = mod.loc.get(key); l && !l->isEmpty())
        return *l;
    QString n = key;
    n.replace(u'_', u' ');
    return py::capitalize(n);
}

QImage strip_for(const moddata::Mod &mod, document::Document *doc, const QString &key, const std::optional<QImage> &own,
                 const QString &buildings, const QString &name)
{
    long long mx = 6;
    for (const auto &l : pb::lines_of(buildings, key))
        if (l.key == u"max_level")
            mx = std::max(1LL, l.value.toLongLong());
    QImage p;
    if (own && !own->isNull()) {
        p = *own;
    } else {                     // a stand-in: the name's first letter on dark wood
        p = QImage(40, 32, QImage::Format_ARGB32);
        p.fill(QColor(74, 56, 38));
        QPainter pt(&p);
        pt.setRenderHint(QPainter::Antialiasing);
        pt.setPen(QColor(222, 190, 110));
        QFont f(u"Times New Roman"_s);
        f.setBold(true);
        f.setPixelSize(22);
        pt.setFont(f);
        const QString nm = name.isEmpty() ? name_of(mod, doc, key) : name;
        pt.drawText(QRect(2, 0, 24, 30), Qt::AlignCenter, nm.isEmpty() ? u"?"_s : nm.left(1).toUpper());
        pt.end();
    }
    return pb::make_strip(p, static_cast<int>(mx));
}

void window_work(moddata::Mod &mod, document::Document *doc, const QString &buildings, QString &gui, QString &gfx,
                 OrderedMap<QString, moreviews::Asset> &files, const OrderedMap<QString, QString> &names,
                 const QStringList &old_keys, std::optional<int> pref)
{
    const QStringList ks = pb::province_keys(buildings);
    QStringList sprites;
    // the picture each building shows now, by its place before this step: after a move or a removal slot N shows
    // the picture of building N again
    std::map<QString, QString> shows;
    {
        const pb::Window w0 = pb::read_window(gui);
        if (!w0.found)
            return;
        const QStringList &before = old_keys.isEmpty() ? ks : old_keys;
        for (int i = 0; i < before.size(); ++i)
            if (i < w0.slot_sprites.size() && !w0.slot_sprites[i].isEmpty())
                shows[before[i]] = w0.slot_sprites[i];
    }
    for (int i = 0; i < ks.size(); ++i) {
        const pb::Window w = pb::read_window(gui);
        const auto it = shows.find(ks[i]);
        const QString sp = it != shows.end() ? it->second : strip_sprite(ks[i]);
        if (!w.icons.contains(u"build_icon%1"_s.arg(i)))
            gui = pb::add_icon(gui, i, sp);
        else
            gui = pb::set_slot_sprite(gui, i, sp);
        if (it != shows.end())
            continue;                              // (its picture is there already)
        if (!files.contains(strip_rel(ks[i])))
            files.insert(strip_rel(ks[i]),
                         moreviews::Asset{u"strip"_s,
                                          strip_for(mod, doc, ks[i], std::nullopt, buildings, names.value(ks[i])), {}, {}});
        const QImage &img = files.get(strip_rel(ks[i]))->img;
        sprites << pb::sprite_block(sp, strip_rel(ks[i]), std::max(1, img.width() / 40));
    }
    // the game draws the slots from build_icon3 on in every row (measured): those are parked outside the window, their
    // rows' pictures painted into the background below; a slot that came up into the first three is put back
    {
        const pb::Window w = pb::read_window(gui);
        for (int i = 0; i < w.slot_sprites.size(); ++i) {
            if (!w.icons.contains(u"build_icon%1"_s.arg(i)))
                continue;
            if (i >= pb::LIVE_SLOTS && !w.parked(i))
                gui = pb::set_slot_place(gui, i, pb::PARK_X, w.slot_y[i]);
            else if (i < pb::LIVE_SLOTS && w.parked(i))
                gui = pb::set_slot_place(gui, i, w.icon_x, w.icon_y);
        }
    }
    // the room: the window fitted to the rows (the national focus window's way)
    const pb::Window w = pb::read_window(gui);
    pb::Source src = pb::source_of(gui);
    const bool pref_changed = pref && std::max(pb::MIN_ROW, *pref) != src.pref;
    if (pref)
        src.pref = std::max(pb::MIN_ROW, *pref);
    const pb::Fit f = pb::plan_fit(w, static_cast<int>(ks.size()), src.pref);
    // (rows from the 4th on: their pictures are in the background — drawn again at every change)
    if (f.delta != 0 || f.step != w.step || pref_changed || ks.size() > pb::LIVE_SLOTS) {
        std::optional<QImage> bg;
        if (src.sprite == BG_SPRITE) {             // (an older fit of ours without its note: the picture as it is)
            if (const moreviews::Asset *a = files.get(BG_REL))
                bg = a->img;
            else if (doc && doc->maps.contains(u"assets"_s))
                if (const moreviews::Asset *a2 = doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.get(BG_REL))
                    bg = a2->img;
        }
        if (!bg && doc)
            bg = iconstrips::strips_for(mod, *doc).image(src.sprite);
        if (bg && !bg->isNull()) {
            QImage out = pb::fit_bg(*bg, src, f.rows, f.step);
            std::vector<std::pair<int, QImage>> pics;
            std::optional<iconstrips::Sprites> all;      // (read once, when needed)
            for (int i = pb::LIVE_SLOTS; i < ks.size() && i < w.slot_sprites.size(); ++i) {
                const QString &sp = w.slot_sprites[i];
                if (sp.isEmpty())
                    continue;
                std::optional<QImage> strip;
                long long frames = 1;
                if (const moreviews::Asset *a = files.get(strip_rel(ks[i])); a && sp == strip_sprite(ks[i])) {
                    strip = a->img;                    // (made in this step)
                    frames = std::max(1, a->img.width() / 40);
                } else if (doc) {
                    iconstrips::Strips &st = iconstrips::strips_for(mod, *doc);
                    if (!all)
                        all = st.sprites();
                    strip = st.image(sp, &*all);
                    if (const iconstrips::Sprite *s = all->get(sp)) {
                        frames = s->frames;
                        // a strip changed in this very step (a picture put in / taken out / replaced): the new one,
                        // its pictures as wide as before
                        if (const moreviews::Asset *na = files.get(st.resolve(s->tex).first); na && strip && !strip->isNull()) {
                            const long long fw = std::max(1LL, static_cast<long long>(strip->width()) / std::max(1LL, frames));
                            strip = na->img;
                            frames = std::max(1LL, static_cast<long long>(na->img.width()) / fw);
                        }
                    }
                }
                if (strip && !strip->isNull())
                    pics.emplace_back(i, pb::plain_picture(*strip, static_cast<int>(frames)));
            }
            pb::paint_rows(out, w, src.top, f.step, pics);
            files.insert(BG_REL, moreviews::Asset{u"strip"_s, out, {}, {}});
            gui = pb::fit_text(gui, f.rows, f.step, BG_SPRITE, src);
            sprites << pb::sprite_block(BG_SPRITE, BG_REL, 1);
        }
    }
    if (!sprites.isEmpty())
        gfx = pb::add_sprites(gfx, sprites);
}

WindowFix window_fix(moddata::Mod &mod, document::Document &doc)
{
    WindowFix out;
    const QString gui_rel = u"interface/province_interface.gui"_s;
    QString gui = store_text(&doc, gui_rel), gfx = store_text(&doc, GFX);
    const QString g0 = gui, x0 = gfx;
    window_work(mod, &doc, store_text(&doc, BUILDINGS), gui, gfx, out.files, {});
    if (gui != g0)
        out.texts.insert(gui_rel, gui);
    if (gfx != x0)
        out.texts.insert(GFX, gfx);
    return out;
}

QImage BuildTypesPanel::_strip(const QString &key, const std::optional<QImage> &own)
{
    return strip_for(mod, doc, key, own, text(BUILDINGS), QString());
}

void BuildTypesPanel::_window_work(QString &gui, QString &gfx, OrderedMap<QString, moreviews::Asset> &files,
                                   const QStringList &)
{
    window_work(mod, doc, text(BUILDINGS), gui, gfx, files, {});
}

void BuildTypesPanel::fix_window()
{
    if (!doc)
        return;
    const WindowFix f = window_fix(mod, *doc);
    if (f.texts.isEmpty() && f.files.isEmpty())
        return;
    Q_EMIT bundleEdit(u"Province window: a row for every building"_s, f.texts, f.files, {});
}

void BuildTypesPanel::_own_picture()
{
    if (!cur || !doc)
        return;
    const QString path = lib->currentData().toString();
    const std::optional<QImage> img = path.isEmpty() ? std::nullopt : imgio::read_image(path);
    if (!img || img->isNull()) {
        pic_note->setText(u"Pick a picture of your library folder first (the folder has none, or this one can't be read)."_s);
        return;
    }
    const QString k = *cur;
    const QImage strip = _strip(k, img);
    // the slot shows our own sprite: made with the row (or for one of the game's three: put in its place)
    QString gui = text(gui_rel()), gfx = text(GFX);
    const QString x0 = gfx;
    OrderedMap<QString, moreviews::Asset> files;
    files.insert(strip_rel(k), moreviews::Asset{u"strip"_s, strip, {}, {}});
    const QStringList ks = keys();
    const int i = static_cast<int>(ks.indexOf(k));
    const QString sp = strip_sprite(k);
    const pb::Window w = pb::read_window(gui);
    OrderedMap<QString, QString> texts;
    if (w.found && i >= 0 && !w.icons.contains(u"build_icon%1"_s.arg(i)))
        _window_work(gui, gfx, files, ks);
    // the sprite's frame count follows the strip (an older sprite of ours: its block made again)
    {
        const script::Nodes nodes = script::parse(gfx);
        QString cut = gfx;
        for (const script::Node &n : nodes)
            if (n.is_block)
                for (const script::Node &c : n.kids)
                    if (c.is_block)
                        for (const script::Node &x : c.kids)
                            if (x.has_key && x.key == u"name" && x.value == sp) {
                                qsizetype a = py::rfind(cut, u'\n', c.kstart) + 1;
                                qsizetype e = cut.indexOf(u'\n', c.vend);
                                e = e < 0 ? cut.size() : e + 1;
                                cut = cut.left(a) + cut.mid(e);
                            }
        gfx = pb::add_sprites(cut, {pb::sprite_block(sp, strip_rel(k), std::max(1, strip.width() / 40))});
    }
    // a building of the game's three keeps its own sprite name in the gui: the slot is pointed at ours
    if (w.found && i >= 0 && w.icons.contains(u"build_icon%1"_s.arg(i))) {
        const script::Nodes nodes = script::parse(gui);
        std::function<const script::Node *(const script::Nodes &)> find_icon = [&](const script::Nodes &ns)
            -> const script::Node * {
            for (const script::Node &n : ns) {
                if (!n.is_block)
                    continue;
                for (const script::Node &x : n.kids)
                    if (x.has_key && x.key == u"name" && x.value == u"build_icon%1"_s.arg(i))
                        return &n;
                if (const script::Node *f = find_icon(n.kids))
                    return f;
            }
            return nullptr;
        };
        if (const script::Node *icon = find_icon(nodes))
            for (const script::Node &x : icon->kids)
                if (x.has_key && x.key == u"spriteType" && !x.is_block && x.value != sp)
                    gui = gui.left(x.vstart) + u"\"%1\""_s.arg(sp) + gui.mid(x.vend);
    }
    if (gui != text(gui_rel()))
        texts.insert(gui_rel(), gui);
    if (gfx != x0)
        texts.insert(GFX, gfx);
    Q_EMIT bundleEdit(u"%1: its own picture"_s.arg(_name(k)), texts, files, {});
}

// the sprite the building's row shows (its slot build_iconN in the province window), else the one the editor would make
static QString row_sprite(const QString &gui, int idx, const QString &key)
{
    QString sprite;
    if (idx >= 0) {
        std::function<void(const script::Nodes &)> walk = [&](const script::Nodes &ns) {
            for (const script::Node &n : ns)
                if (n.is_block) {
                    bool it = false;
                    for (const script::Node &x : n.kids)
                        it = it || (x.has_key && x.key == u"name" && x.value == u"build_icon%1"_s.arg(idx));
                    if (it)
                        for (const script::Node &x : n.kids)
                            if (x.has_key && x.key == u"spriteType")
                                sprite = x.value;
                    walk(n.kids);
                }
        };
        walk(script::parse(gui));
    }
    return sprite.isEmpty() ? strip_sprite(key) : sprite;
}

void BuildTypesPanel::_fill_frames(const QString &k, long long mx)
{
    clear(frames_lay);
    for (QButtonGroup *g : frames_host->findChildren<QButtonGroup *>(Qt::FindDirectChildrenOnly))
        g->deleteLater();
    frames_n = 0;
    if (_frame_key != k) {                         // (another building: its level 1 picked)
        _frame_key = k;
        frame_sel = 1;
    }
    frame_sprite = row_sprite(text(gui_rel()), static_cast<int>(keys().indexOf(k)), k);
    std::optional<QImage> img;
    long long n = 0;
    QString tex, gfx_rel;
    if (doc) {
        iconstrips::Strips &st = iconstrips::strips_for(mod, *doc);
        const iconstrips::Sprites sps = st.sprites();
        if (const iconstrips::Sprite *s = sps.get(frame_sprite)) {
            img = st.image(frame_sprite, &sps);
            n = s->frames;
            tex = st.resolve(s->tex).first;
            gfx_rel = s->gfx;
        }
    }
    lv_btn->hide();
    if (!img || img->isNull() || n < 1) {
        pic_note->setText(u"Its picture comes with its row in the province window (sprite %1 not found yet)."_s.arg(frame_sprite));
        frame_note->clear();
        size_lbl->setText(iconstrips::size_sign_text(std::nullopt));
        for (QPushButton *b : {rep_btn, cut_btn, imp_rep_btn, lib_one_btn, lib_all_btn})
            b->setEnabled(false);
        return;
    }
    const int fw = std::max(1, static_cast<int>(img->width() / n)), fh = img->height();
    size_lbl->setText(iconstrips::size_sign_text(QSize(fw, fh), u"the size of one level's picture — a picture of another "
                                                                 u"size is scaled whole into it"_s));
    for (QPushButton *b : {imp_rep_btn, lib_one_btn, lib_all_btn})
        b->setEnabled(true);
    lib_all_btn->setText(u"Put all %1 pictures into my library"_s.arg(n));
    if (n - 1 > mx) {                               // more pictures than levels: the levels follow on a click
        lv_btn->setText(u"Make the max level %1 (one level per picture), then pick the technologies that unlock the new "
                        u"levels above"_s.arg(n - 1));
        lv_btn->show();
    }
    frames_n = static_cast<int>(n);
    frame_sel = std::clamp(frame_sel, 0, frames_n - 1);
    // shown a bit bigger than the game (medium-sized), every picture with its level under it
    const int show_h = fh < 48 ? 48 : fh;
    auto *grp = new QButtonGroup(frames_host);
    grp->setExclusive(true);
    for (int i = 0; i < frames_n; ++i) {
        const QImage part = img->copy(i * fw, 0, fw, fh);
        const QPixmap pm = QPixmap::fromImage(fh == show_h ? part : part.scaledToHeight(show_h, Qt::SmoothTransformation));
        auto *b = new QToolButton();
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIcon(QIcon(pm));
        b->setIconSize(pm.size());
        b->setText(i == 0 ? u"level 0"_s : u"level %1"_s.arg(i));
        b->setCheckable(true);
        b->setChecked(i == frame_sel);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(u"Picture %1 of %2: what the row shows at level %3. Drag it to another place to change the "
                      u"order; right-click it to delete the level."_s.arg(i + 1).arg(frames_n).arg(i));
        b->setProperty("level", i);
        b->installEventFilter(this);
        b->setStyleSheet(u"QToolButton { background: %1; border: 2px solid %2; border-radius: 4px; padding: 3px; "
                         u"color: %3; } QToolButton:checked { border: 3px solid %4; color: %4; }"_s.arg(
                             theme::BG, theme::LINE, theme::INK, theme::GOLD));
        connect(b, &QToolButton::clicked, this, [this, i] {
            frame_sel = i;
            frame_note->setText(u"Picked: the picture of level %1."_s.arg(i));
        });
        grp->addButton(b);
        frames_lay->addWidget(b);
    }
    {   // ＋ : a new level (the user 2026-11-18: "a plus there … a placeholder picture … for my new level")
        auto *plus = new QToolButton();
        plus->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        QPixmap blank(std::max(24, show_h * fw / std::max(1, fh)), show_h);
        blank.fill(Qt::transparent);
        {
            QPainter p(&blank);
            QFont f = p.font();
            f.setPixelSize(std::max(18, show_h * 2 / 3));
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(theme::GOLD));
            p.drawText(blank.rect(), Qt::AlignCenter, u"＋"_s);
        }
        plus->setIcon(QIcon(blank));
        plus->setIconSize(blank.size());
        plus->setText(u"new level"_s);
        plus->setCursor(Qt::PointingHandCursor);
        plus->setToolTip(u"A new level: a placeholder picture (the highest level's) goes in at the end and the highest "
                         u"level goes one up. Then import its picture and pick the technology that unlocks it."_s);
        plus->setStyleSheet(u"QToolButton { background: %1; border: 2px dashed %2; border-radius: 4px; padding: 3px; "
                            u"color: %2; } QToolButton:hover { background: #2a2416; }"_s.arg(theme::BG, theme::GOLD));
        connect(plus, &QToolButton::clicked, this, [this] { _add_level(); });
        frames_lay->addWidget(plus);
    }
    pic_note->setText(u"%1 pictures, one per level from level 0 (not built). Each picture is %2 × %3 px — a picture of "
                      u"your own for one level should be that size (a bigger or smaller one is scaled whole into it). "
                      u"The strip: %4 (%5 × %3 px), sprite %6 in %7."_s
                          .arg(frames_n)
                          .arg(fw)
                          .arg(fh)
                          .arg(tex)
                          .arg(img->width())
                          .arg(frame_sprite, gfx_rel));
    QString fn = u"Picked: the picture of level %1. “Import a picture for this building level” above, or a picture of "
                 u"your library folder (%2) chosen in the list below. ＋ adds a level; drag a picture to change the "
                 u"order."_s
                     .arg(frame_sel)
                     .arg(moreviews::library_dir(u"pictures"_s));
    if (_new_level >= 0 && _new_level == frame_sel && _new_level < frames_n)
        fn = u"Level %1 is new: it shows the picture of level %2 until you import one for it. The technology that "
             u"unlocks it: “A technology unlocks a level…” above."_s.arg(_new_level).arg(_new_level - 1);
    _new_level = -1;
    frame_note->setText(fn);
    frame_note->setStyleSheet(QString());
    rep_btn->setEnabled(true);
    cut_btn->setEnabled(frames_n > 1);
}

void BuildTypesPanel::_add_level()
{
    if (!cur || !doc || frame_sprite.isEmpty() || frames_n < 1)
        return;
    const auto img = iconstrips::strips_for(mod, *doc).image(frame_sprite);
    if (!img || img->isNull())
        return;
    const int fw = img->width() / frames_n;
    frame_sel = frames_n - 1;                       // (in after the last one)
    _frame_edit(1, img->copy((frames_n - 1) * fw, 0, fw, img->height()), true);
}

void BuildTypesPanel::_delete_level(int i)
{
    if (!cur || !doc || frame_sprite.isEmpty() || i < 0 || i >= frames_n)
        return;
    if (frames_n <= 2) {
        frame_note->setStyleSheet(u"QLabel { color: %1; }"_s.arg(theme::DANGER));
        frame_note->setText(u"A building has at least level 0 (not built) and level 1: these two pictures stay."_s);
        return;
    }
    const QString k = *cur;
    const auto r = iconstrips::strips_for(mod, *doc).remove(frame_sprite, i + 1);
    if (const QString *why = std::get_if<QString>(&r)) {
        frame_note->setText(*why);
        return;
    }
    const auto &[f, t, moved] = std::get<0>(r);
    OrderedMap<QString, moreviews::Asset> files = f;
    OrderedMap<QString, QString> texts = t;
    const long long mx = max_level->value();
    if (mx + 1 == frames_n && mx > 1) {             // the building one level less with it
        const QString bt = texts.contains(BUILDINGS) ? *texts.get(BUILDINGS) : text(BUILDINGS);
        texts.insert(BUILDINGS, blocks::set_value(bt, {k}, u"max_level"_s, QString::number(mx - 1)));
    }
    frame_sel = std::min(i, frames_n - 2);
    _emit_strip(u"%1: level %2 deleted"_s.arg(_name(k)).arg(i), texts, files);
}

void BuildTypesPanel::_frame_move(int from, int to)
{
    if (!cur || !doc || frame_sprite.isEmpty() || from == to || std::min(from, to) < 0 || std::max(from, to) >= frames_n)
        return;
    std::vector<long long> order;
    for (int i = 1; i <= frames_n; ++i)
        order.push_back(i);
    const long long moved = order[static_cast<size_t>(from)];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, moved);
    const auto r = iconstrips::strips_for(mod, *doc).reorder(frame_sprite, order);
    if (const QString *why = std::get_if<QString>(&r)) {
        frame_note->setText(*why);
        return;
    }
    const auto &[files, texts] = std::get<0>(r);
    frame_sel = to;
    _emit_strip(u"%1: the picture of level %2 moved to level %3"_s.arg(_name(*cur)).arg(from).arg(to), texts, files);
}

void BuildTypesPanel::_to_library(bool all)
{
    if (!cur || !doc || frame_sprite.isEmpty() || frames_n < 1)
        return;
    const auto img = iconstrips::strips_for(mod, *doc).image(frame_sprite);
    if (!img || img->isNull())
        return;
    const int fw = img->width() / frames_n, fh = img->height();
    QStringList saved;
    for (int i = all ? 0 : frame_sel; i <= (all ? frames_n - 1 : frame_sel); ++i) {
        const QString p = iconstrips::to_library(img->copy(i * fw, 0, fw, fh), mod.name(),
                                                 u"%1 level %2"_s.arg(_name(*cur)).arg(i));
        if (!p.isEmpty())
            saved << pypath::basename(p);
    }
    _fill_lib();
    frame_note->setStyleSheet(saved.isEmpty() ? u"QLabel { color: %1; }"_s.arg(theme::DANGER) : QString());
    frame_note->setText(saved.isEmpty() ? u"The picture couldn't be saved into your library folder."_s
                        : saved.size() == 1 ? u"Into your library: %1"_s.arg(saved[0])
                                            : u"Into your library: %1 pictures (%2 … %3)"_s.arg(saved.size()).arg(
                                                  saved.first(), saved.last()));
}

bool BuildTypesPanel::eventFilter(QObject *o, QEvent *e)
{
    static const QString MIME = u"application/x-modeditor-level"_s;
    if (o == frames_host) {                         // a picture dropped: on another picture, or after the last
        if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
            auto *de = static_cast<QDropEvent *>(e);
            if (de->mimeData()->hasFormat(MIME)) {
                de->acceptProposedAction();
                return true;
            }
        } else if (e->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(e);
            if (!de->mimeData()->hasFormat(MIME))
                return false;
            int to = frames_n - 1;
            for (QWidget *w = frames_host->childAt(de->position().toPoint()); w && w != frames_host; w = w->parentWidget())
                if (w->property("level").isValid()) {
                    to = w->property("level").toInt();
                    break;
                }
            _drop = {de->mimeData()->data(MIME).toInt(), to};
            de->acceptProposedAction();
            return true;
        }
        return QWidget::eventFilter(o, e);
    }
    auto *b = qobject_cast<QToolButton *>(o);
    if (b && b->property("level").isValid()) {
        if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(e)->button() == Qt::RightButton) {
            const int i = b->property("level").toInt();
            QTimer::singleShot(0, this, [this, i] { _delete_level(i); });   // (after the click: drawn again)
            return true;
        }
        if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
            _drag_from = b->property("level").toInt();
            _drag_at = static_cast<QMouseEvent *>(e)->position().toPoint();
        } else if (e->type() == QEvent::MouseMove && _drag_from >= 0 &&
                   (static_cast<QMouseEvent *>(e)->buttons() & Qt::LeftButton) &&
                   (static_cast<QMouseEvent *>(e)->position().toPoint() - _drag_at).manhattanLength() >=
                       QApplication::startDragDistance()) {
            auto *md = new QMimeData();
            md->setData(MIME, QByteArray::number(_drag_from));
            _drag_from = -1;
            _drop = {-1, -1};
            auto *drag = new QDrag(this);
            drag->setMimeData(md);
            const QPixmap pm = b->icon().pixmap(b->iconSize());
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
            b->setDown(false);
            drag->exec(Qt::MoveAction);
            const auto [from, to] = _drop;
            if (from >= 0 && to >= 0 && from != to)  // (after this event: the pictures are made again)
                QTimer::singleShot(0, this, [this, from, to] { _frame_move(from, to); });
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            _drag_from = -1;
        }
    }
    return QWidget::eventFilter(o, e);
}

void BuildTypesPanel::_frame_import(int how)
{
    if (!cur || !doc || frame_sprite.isEmpty())
        return;
    const std::optional<QSize> sz = iconstrips::frame_size(iconstrips::strips_for(mod, *doc), frame_sprite);
    const QString p = QFileDialog::getOpenFileName(
        this, sz ? u"A picture of %1 × %2 pixels"_s.arg(sz->width()).arg(sz->height()) : u"A picture"_s, QString(),
        iconstrips::picture_filter());
    if (p.isEmpty())
        return;
    const auto img = imgio::read_image(p);
    if (!img || img->isNull()) {
        frame_note->setText(u"%1 can't be read as a picture."_s.arg(pypath::basename(p)));
        return;
    }
    _frame_edit(how, img);
}

void BuildTypesPanel::_frame_edit(int how, const std::optional<QImage> &given, bool new_level)
{
    if (!cur || !doc || frame_sprite.isEmpty())
        return;
    iconstrips::Strips &st = iconstrips::strips_for(mod, *doc);
    const iconstrips::Sprites sps = st.sprites();
    const iconstrips::Sprite *sp = sps.get(frame_sprite);
    if (!sp)
        return;
    std::optional<QImage> pic = given;
    if (how != 2 && !pic) {
        const QString path = lib->currentData().toString();
        pic = path.isEmpty() ? std::nullopt : imgio::read_image(path);
        if (!pic || pic->isNull()) {
            frame_note->setText(u"Choose a picture of your library folder in the list first (the folder has none, or this "
                                u"one can't be read)."_s);
            return;
        }
    }
    const QString k = *cur;
    const long long num = frame_sel + 1;           // (the strips' pictures count from 1)
    OrderedMap<QString, moreviews::Asset> files;
    OrderedMap<QString, QString> texts;
    QString label;
    if (how == 0) {
        files = st.put(frame_sprite, num, *pic);
        label = u"%1: the picture of level %2 replaced"_s.arg(_name(k)).arg(frame_sel);
    } else if (how == 2) {
        const auto r = st.remove(frame_sprite, num);
        if (const QString *why = std::get_if<QString>(&r)) {
            frame_note->setText(*why);
            return;
        }
        const auto &[f, t, moved] = std::get<0>(r);
        files = f;
        texts = t;
        label = u"%1: the picture of level %2 taken out"_s.arg(_name(k)).arg(frame_sel);
        frame_sel = std::max(0, frame_sel - 1);
    } else {
        const std::optional<QImage> img = st.image(frame_sprite, &sps);
        const long long n = sp->frames;
        if (!img || img->isNull())
            return;
        const int fw = static_cast<int>(img->width() / n), h = img->height();
        if (fw < 1 || fw * n != img->width()) {
            frame_note->setText(u"%1 is %2 px wide for %3 pictures — that doesn't divide evenly, so no picture can be put "
                                u"in safely."_s.arg(sp->tex).arg(img->width()).arg(n));
            return;
        }
        const QImage src = img->convertToFormat(QImage::Format_ARGB32);
        QImage out(img->width() + fw, h, QImage::Format_ARGB32);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        const int cut = static_cast<int>(num) * fw;           // after the picked picture
        p.drawImage(0, 0, src.copy(0, 0, cut, h));
        p.drawImage(cut, 0, iconstrips::fit_frame(*pic, fw, h).convertToFormat(QImage::Format_ARGB32));
        if (cut < src.width())
            p.drawImage(cut + fw, 0, src.copy(cut, 0, src.width() - cut, h));
        p.end();
        files.insert(st.resolve(sp->tex).first, moreviews::Asset{u"strip"_s, out, {}, {}});
        OrderedMap<QString, long long> n_of;
        n_of.insert(frame_sprite, n + 1);
        texts.insert(sp->gfx, iconstrips::set_frames(text(sp->gfx), {frame_sprite}, n_of));
        label = u"%1: a picture put in after level %2"_s.arg(_name(k)).arg(frame_sel);
        frame_sel += 1;
        if (new_level) {                            // ＋: the building gets the level, in the same step
            label = u"%1: level %2 added"_s.arg(_name(k)).arg(frame_sel);
            _new_level = frame_sel;
            const long long mx = max_level->value();
            if (mx + 1 == n)
                texts.insert(BUILDINGS, blocks::set_value(text(BUILDINGS), {k}, u"max_level"_s, QString::number(mx + 1)));
        }
    }
    _emit_strip(label, texts, files);
}

void BuildTypesPanel::_emit_strip(const QString &label, OrderedMap<QString, QString> texts,
                                  OrderedMap<QString, moreviews::Asset> files)
{
    if (!cur || (files.isEmpty() && texts.isEmpty()))
        return;
    const QString k = *cur;
    // rows from the 4th on show their picture from the window's background: drawn again in the same step
    if (keys().indexOf(k) >= pb::LIVE_SLOTS) {
        const QString g0 = text(gui_rel()), x0 = texts.contains(GFX) ? *texts.get(GFX) : text(GFX);
        QString gui = g0, gfx = x0;
        window_work(mod, doc, text(BUILDINGS), gui, gfx, files, {});
        if (gui != g0)
            texts.insert(gui_rel(), gui);
        if (gfx != x0)
            texts.insert(GFX, gfx);
    }
    Q_EMIT bundleEdit(label, texts, files, {});
}

void BuildTypesPanel::_fill_lib()
{
    lib->clear();
    {
        const QString folder = moreviews::library_dir(u"pictures"_s);
        QStringList names;
        for (const QString &f : QDir(folder).entryList(QDir::Files))
            for (const QString &ext : moreviews::PIC_EXT)
                if (py::lower(f).endsWith(ext)) {
                    names << f;
                    break;
                }
        std::sort(names.begin(), names.end());
        for (const QString &f : names)
            lib->addItem(f, QDir(folder).filePath(f));
        if (names.isEmpty())
            lib->addItem(u"(your library folder has no pictures yet)"_s, QVariant());
    }
}

// ---------------------------------------------------------------------------------------------- editing
void BuildTypesPanel::_edit(const QString &label, const QString &rel, const QString &t)
{
    if (t == text(rel))
        return;
    OrderedMap<QString, QString> texts;
    texts.insert(rel, t);
    Q_EMIT bundleEdit(label, texts, {}, {});
}

void BuildTypesPanel::_set(const QString &label, const QString &field, const QString &value)
{
    if (!cur)
        return;
    _edit(u"%1: %2"_s.arg(_name(*cur), label), BUILDINGS, blocks::set_value(text(BUILDINGS), {*cur}, field, value));
}

void BuildTypesPanel::_make_new()
{
    const QString nm = py::strip(new_name->text());
    new_warn->hide();
    if (nm.isEmpty()) {
        new_warn->setText(u"Type the new building's name first."_s);
        new_warn->show();
        return;
    }
    QString key;
    for (const QChar c : py::lower(nm))
        key += (c.isLetterOrNumber() && c.unicode() < 128) ? c : u'_';
    while (key.contains(u"__"_s))
        key.replace(u"__"_s, u"_"_s);
    key = py::strip(key, u"_"_s);
    if (key.isEmpty() || key[0].isDigit())
        key = u"building_"_s + key;
    const QString base = key;
    const QString bt = text(BUILDINGS);
    QSet<QString> taken;
    for (const script::Node &n : script::parse(bt))
        if (n.has_key)
            taken.insert(n.key);
    for (int i = 2; taken.contains(key); ++i)
        key = u"%1_%2"_s.arg(base).arg(i);
    const QString b2 = pb::add_building(bt, key, cur ? cur : OptStr(u"fort"_s));
    QString gui = text(gui_rel()), gfx = text(GFX);
    const QString g0 = gui, x0 = gfx;
    OrderedMap<QString, moreviews::Asset> files;
    OrderedMap<QString, QString> names;
    names.insert(key, nm);
    window_work(mod, doc, b2, gui, gfx, files, names, keys());   // (its row, picture and room in the same step)
    OrderedMap<QString, QString> texts;
    texts.insert(BUILDINGS, b2);
    if (gui != g0)
        texts.insert(gui_rel(), gui);
    if (gfx != x0)
        texts.insert(GFX, gfx);
    cur = key;
    new_name->clear();
    Q_EMIT bundleEdit(u"New building type: %1"_s.arg(nm), texts, files, names);
}

void BuildTypesPanel::_remove()
{
    if (!cur || pb::GAME_THREE.contains(*cur))
        return;
    const QString k = *cur;
    cur.reset();
    // the window follows: the rows after it show their own pictures again, the window a row lower
    const QString b2 = pb::remove_building(text(BUILDINGS), k);
    _bundle(u"Building type %1 removed"_s.arg(_name(k)), b2, keys(), std::nullopt);
}

void BuildTypesPanel::_bundle(const QString &label, const QString &buildings, const QStringList &old_keys,
                              std::optional<int> pref)
{
    // buildings.txt as `buildings`, and the province window made to fit it, as one step
    QString gui = text(gui_rel()), gfx = text(GFX);
    const QString g0 = gui, x0 = gfx;
    OrderedMap<QString, moreviews::Asset> files;
    window_work(mod, doc, buildings, gui, gfx, files, {}, old_keys, pref);
    OrderedMap<QString, QString> texts;
    if (buildings != text(BUILDINGS))
        texts.insert(BUILDINGS, buildings);
    if (gui != g0)
        texts.insert(gui_rel(), gui);
    if (gfx != x0)
        texts.insert(GFX, gfx);
    if (texts.isEmpty() && files.isEmpty())
        return;
    Q_EMIT bundleEdit(label, texts, files, {});
}

void BuildTypesPanel::_move(const QString &key, int to)
{
    const QStringList ks = keys();
    if (!ks.contains(key) || to < 0 || to >= ks.size() || ks.indexOf(key) == to)
        return;
    cur = key;
    _bundle(u"%1: row %2 of the province window"_s.arg(_name(key)).arg(to + 1), pb::move_building(text(BUILDINGS), key, to),
            ks, std::nullopt);
}

void BuildTypesPanel::_row_height(int px)
{
    _bundle(u"Province window: rows of %1 px"_s.arg(px), text(BUILDINGS), keys(), px);
}

void BuildTypesPanel::_tech(const QString &tech, int how)
{
    if (!cur)
        return;
    const QString k = *cur;
    const auto files = tech_files();
    const auto us = pb::unlocks(files, k);
    QString rel;
    for (const auto &t : pb::techs(files))
        if (t.key == tech) {
            rel = t.rel;
            break;
        }
    if (rel.isEmpty())
        return;
    bool act = false, any_act = false;
    long long adds = 0;
    for (const auto &u : us) {
        any_act = any_act || u.activates;
        if (u.tech == tech) {
            act = u.activates;
            adds = u.adds;
        }
    }
    QString label;
    if (how == 0) {
        act = false;
        adds = 0;
        label = u"%1 no longer unlocks %2"_s.arg(tech, _name(k));
    } else if (!any_act) {
        // the first technology activates it and gives level 1: activate_building alone gives no level (measured)
        act = true;
        adds = std::max(0LL, adds) + 1;
        label = u"%1 unlocks %2"_s.arg(tech, _name(k));
    } else {
        adds += how;
        label = u"%1 gives %2 a level more"_s.arg(tech, _name(k));
    }
    _edit(label, rel, pb::set_unlock(text(rel), tech, k, act, std::max(0LL, adds)));
}

// ---------------------------------------------------------------------------------------------- showing
void BuildTypesPanel::refresh(document::Document &d, const QList<long long> &ps)
{
    doc = &d;
    pids = ps;
    const QString bt = text(BUILDINGS), gui = text(gui_rel());
    QString sig = QString::number(qHash(bt)) + u'|' + QString::number(qHash(gui)) + u'|' + QString::number(qHash(text(GFX)));
    for (const auto &[rel, t] : tech_files())
        sig += u'|' + QString::number(qHash(t));
    for (long long p : ps)
        sig += u',' + QString::number(p) + u':' + (cur ? QString::number(doc->prov_state(p).level_of(*cur)) : QString());
    if (cur)                                       // (levels set in the provinces: the counts follow)
        for (const auto &[p, want] : doc->prov_want)
            sig += u';' + QString::number(want.level_of(*cur));
    if (doc->maps.contains(u"assets"_s))
        sig += u'#' + QString::number(doc->map<moreviews::AssetEdits>(u"assets"_s).fresh.size());
    if (doc->maps.contains(u"loc"_s))
        sig += u'~' + (cur ? _name(*cur) : QString());
    if (sig == _sig)
        return;
    _sig = sig;
    _fill_tree();
    _fill_detail();
    _fill_window();
}

void BuildTypesPanel::_fill_tree()
{
    _filling = true;
    const QStringList ks = keys();
    if (cur && !ks.contains(*cur))
        cur.reset();
    if (!cur && !ks.isEmpty())
        cur = ks.size() > 3 ? ks.last() : ks.first();
    const auto files = tech_files();
    const QString bt = text(BUILDINGS);
    tree->clear();
    QTreeWidgetItem *pick = nullptr;
    for (const QString &k : ks) {
        QString mx = u"1"_s;
        for (const auto &l : pb::lines_of(bt, k))
            if (l.key == u"max_level")
                mx = l.value;
        auto *it = new QTreeWidgetItem(
            {_name(k), mx, QString::number(pb::unlocks(files, k).size()), QString::number(holders(k))});
        it->setData(0, Qt::UserRole, k);
        it->setToolTip(0, k);
        if (!pb::GAME_THREE.contains(k))
            it->setForeground(0, QColor(theme::GOLD));
        tree->addTopLevelItem(it);
        if (cur && k == *cur)
            pick = it;
    }
    if (pick)
        tree->setCurrentItem(pick);
    {                                              // as tall as its rows: never a scroll bar
        const int rows = tree->topLevelItemCount();
        const int rh = rows ? std::max(18, tree->sizeHintForRow(0)) : 24;
        tree->setFixedHeight(tree->header()->sizeHint().height() + rh * std::max(1, rows) + 2 * tree->frameWidth() + 4);
    }
    count->setText(u"%1 province buildings, %2 of the mod's own."_s.arg(ks.size()).arg(
        std::count_if(ks.begin(), ks.end(), [](const QString &k) { return !pb::GAME_THREE.contains(k); })));
    _filling = false;
}

void BuildTypesPanel::_picked()
{
    if (_filling)
        return;
    const auto items = tree->selectedItems();
    if (items.isEmpty())
        return;
    cur = items[0]->data(0, Qt::UserRole).toString();
    // the window's picture marks it too (the user 2026-11-17: a click in the list must pick it in the picture)
    win_view->sel = *cur;
    win_view->update();
    _fill_detail();
}

void BuildTypesPanel::_fill_detail()
{
    detail->setVisible(cur.has_value());
    if (!cur)
        return;
    _filling = true;
    const QString k = *cur;
    const QString bt = text(BUILDINGS);
    const auto lines = pb::lines_of(bt, k);
    auto field = [&](const QString &f) {
        for (const auto &l : lines)
            if (l.key == f)
                return l.value;
        return QString();
    };
    title->setText(_name(k));
    const bool mine = !pb::GAME_THREE.contains(k);
    sub->setText(u"%1 · %2 · type = %3%4"_s.arg(k, BUILDINGS, field(u"type"_s),
                                                mine ? QString() : u" · one of the game's own"_s));
    name_edit->setText(_name(k));
    name_edit->setModified(false);
    const long long mx = std::max(1LL, field(u"max_level"_s).toLongLong());
    max_level->setValue(mx);
    time->setValue(std::max(1LL, field(u"time"_s).toLongLong()));

    // goods
    clear(cost_lay);
    std::vector<dialogs::PickItem> goods;
    QSet<QString> have;
    for (const auto &l : lines)
        if (l.key == u"goods_cost" && l.block)
            for (const script::Node &c : blocks::children(bt, {k, u"goods_cost"_s}))
                if (c.has_key && !c.is_block) {
                    have.insert(c.key);
                    auto *row = new QHBoxLayout();
                    row->setSpacing(6);
                    const moddata::Good *gd = mod.goods.get(c.key);
                    row->addWidget(label(gd ? gd->name : c.key, u"sub"_s, false), 1);
                    auto *ed = new QLineEdit(c.value);
                    ed->setFixedWidth(80);
                    const QString gk = c.key;
                    connect(ed, &QLineEdit::editingFinished, this, [this, ed, gk, k] {
                        if (!ed->isModified())
                            return;
                        ed->setModified(false);
                        bool ok = false;
                        ed->text().toDouble(&ok);
                        if (ok)
                            _edit(u"%1: %2 costs %3"_s.arg(_name(k), gk, ed->text()), BUILDINGS,
                                  blocks::set_value(text(BUILDINGS), {k, u"goods_cost"_s}, gk, py::strip(ed->text())));
                    });
                    row->addWidget(ed);
                    auto *x = small_btn(u"✕"_s, u"Not in its cost any more"_s);
                    x->setFixedWidth(30);
                    connect(x, &QPushButton::clicked, this, [this, gk, k] {
                        _edit(u"%1: %2 out of its cost"_s.arg(_name(k), gk), BUILDINGS,
                              blocks::del_value(text(BUILDINGS), {k, u"goods_cost"_s}, gk));
                    });
                    row->addWidget(x);
                    cost_lay->addLayout(row);
                }
    for (const auto &[gk, g] : mod.goods)
        if (!have.contains(gk))
            goods.push_back({gk, g.name, g.color, std::nullopt});
    add_good->fill(goods, {{std::nullopt, u"＋ Add a good…"_s}});
    add_good->setCurrentIndex(0);

    // effects
    clear(fx_lay);
    const auto fx = pb::effects(bt, k);
    QSet<QString> fx_have;
    for (const auto &l : fx) {
        fx_have.insert(l.key);
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        auto *nmL = label(u"%1  (%2)"_s.arg(techdata::effect_name(mod.loc, l.key), l.key), u"sub"_s, false);
        nmL->setToolTip(l.key);
        row->addWidget(nmL, 1);
        if (l.block) {
            row->addWidget(label(l.value.simplified(), u"faint"_s, false));
        } else {
            auto *ed = new QLineEdit(l.value);
            ed->setFixedWidth(80);
            const QString ek = l.key;
            connect(ed, &QLineEdit::editingFinished, this, [this, ed, ek, k] {
                if (!ed->isModified())
                    return;
                ed->setModified(false);
                _edit(u"%1: %2 = %3"_s.arg(_name(k), ek, ed->text()), BUILDINGS,
                      blocks::set_value(text(BUILDINGS), {k}, ek, py::strip(ed->text())));
            });
            row->addWidget(ed);
        }
        auto *x = small_btn(u"✕"_s, u"Take this effect off"_s);
        x->setFixedWidth(30);
        const QString ek = l.key;
        connect(x, &QPushButton::clicked, this, [this, ek, k] {
            _edit(u"%1: effect %2 off"_s.arg(_name(k), ek), BUILDINGS, blocks::del_value(text(BUILDINGS), {k}, ek));
        });
        row->addWidget(x);
        fx_lay->addLayout(row);
    }
    fx_note->setText(fx.empty() ? u"None yet. Effects are lines of the building's block, per level (the game's railroad: "
                                  u"infrastructure = 0.16, movement_cost = -0.05)."_s
                                : u"Per level, as the game's own (railroad: infrastructure = 0.16 per level)."_s);
    {
        std::vector<dialogs::PickItem> fxi;
        QStringList pool = pb::ENGINE_EFFECTS + mapfiles::COMMON_MODIFIERS;
        pool.removeDuplicates();
        for (const QString &e : pool)
            if (!fx_have.contains(e))
                fxi.push_back({e, u"%1  (%2)"_s.arg(techdata::effect_name(mod.loc, e), e), std::nullopt,
                               pb::ENGINE_EFFECTS.contains(e) ? OptStr(u"What the game's three use"_s)
                                                              : OptStr(u"Province modifiers"_s)});
        add_fx->fill(fxi, {{std::nullopt, u"＋ Add an effect…"_s}});
        add_fx->setCurrentIndex(0);
    }

    // technologies
    clear(tech_lay);
    const auto files = tech_files();
    const auto us = pb::unlocks(files, k);
    for (const auto &[u, words] : pb::level_steps(us, mx)) {
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        QString what;
        if (u.activates)
            what += u"activate_building"_s;
        if (u.adds)
            what += (what.isEmpty() ? QString() : u" + "_s) + u"max_%1 = %2"_s.arg(k).arg(u.adds);
        auto *l = label(u"%1 · %2 — %3"_s.arg(QString::number(u.year), _name(u.tech), words), u"sub"_s);
        l->setToolTip(u"%1 (%2): %3"_s.arg(u.tech, u.rel, what));
        row->addWidget(l, 1);
        auto *plus = small_btn(u"+1"_s, u"This technology gives one level more (max_%1)"_s.arg(k));
        plus->setFixedWidth(40);
        const QString tk = u.tech;
        connect(plus, &QPushButton::clicked, this, [this, tk] { _tech(tk, +1); });
        row->addWidget(plus);
        auto *x = small_btn(u"✕"_s, u"This technology no longer unlocks it"_s);
        x->setFixedWidth(30);
        connect(x, &QPushButton::clicked, this, [this, tk] { _tech(tk, 0); });
        row->addWidget(x);
        tech_lay->addLayout(row);
    }
    {
        long long total = 0;
        bool act = false;
        for (const auto &u : us) {
            total += u.adds;
            act = act || u.activates;
        }
        QString t = us.empty() ? u"No technology unlocks it yet. Pick one below: the first activates it and gives level 1 "
                                 u"(activate_building = %1 and max_%1 = 1), each further one gives a level more "
                                 u"(max_%1 = 1)."_s
                                     .arg(k)
                               : u"Up to level %1 with every technology above%2."_s.arg(total).arg(
                                     total < mx ? u" (its highest level is %1)"_s.arg(mx) : QString());
        t += u" Measured in the game: only the max_%1 lines give levels (the Expand button's tooltip: “Level 1/1”); "
             u"activate_building gives none by itself, it makes the technology a must (“Discovered …”). The game's own "
             u"three reach level 5 by technology, not their highest level 6. A province may start above it."_s.arg(k);
        if (!us.empty() && !act)
            t += u" None of them activates it: then there is no “Discovered …” condition — the levels come with the "
                 u"max_%1 lines of the technologies discovered (measured: “Level 0/2”)."_s.arg(k);
        tech_note->setText(t);
        std::vector<dialogs::PickItem> ti;
        for (const auto &x : pb::techs(files))
            ti.push_back({x.key, u"%1 · %2"_s.arg(QString::number(x.year), _name(x.key)), std::nullopt,
                          OptStr(x.rel.mid(x.rel.lastIndexOf(u'/') + 1))});
        add_tech->fill(ti, {{std::nullopt, u"＋ A technology unlocks a level…"_s}});
        add_tech->setCurrentIndex(0);
    }

    // the province window
    const QStringList ks = keys();
    const int idx = static_cast<int>(ks.indexOf(k));
    const pb::Window w = pb::read_window(text(gui_rel()));
    const bool has_slot = w.found && w.icons.contains(u"build_icon%1"_s.arg(idx));
    if (!w.found) {
        slot_note->setText(u"The province window can't be read: "_s + w.why);
        slot_note->setStyleSheet(RED);
    } else if (has_slot) {
        slot_note->setText(u"✓ It has its picture slot in the province window (build_icon%1: it is province building "
                           u"no. %2)."_s.arg(idx).arg(idx + 1));
        slot_note->setStyleSheet(GREEN);
    } else {
        slot_note->setText(u"✗ The game will crash: the province window has no picture slot for it. Every province "
                           u"building needs one (build_icon%1 for this one, the %2th); without it the game crashes the "
                           u"moment you open any province (tested in the game, 2026-11-16). The button below adds it."_s
                               .arg(idx)
                               .arg(idx + 1));
        slot_note->setStyleSheet(RED);
    }
    if (w.found && ks.size() > w.fits) {
        room_note->setText(u"⚠ No crash, but it won't look right: the window has room for %1 building rows and there are "
                           u"%2 buildings, so the last row lies on top of the army / navy buttons (seen in the game). "
                           u"Make the rows lower above (Row height) or let the button below make the window taller."_s
                               .arg(w.fits)
                               .arg(ks.size()));
        room_note->setStyleSheet(u"QLabel { color: %1; }"_s.arg(theme::WARN));
    } else if (w.found) {
        room_note->setText(u"✓ The window has room for every row (%1 rows, %2 buildings)."_s.arg(w.fits).arg(ks.size()));
        room_note->setStyleSheet(GREEN);
    }
    {
        bool crash = false;                        // (the button only where it mends something you can name)
        for (const auto &p : pb::window_problems(text(BUILDINGS), text(gui_rel()), [](const QString &x) { return x; }))
            crash = crash || p.crash;
        slot_btn->setVisible(crash || (w.found && ks.size() > w.fits));
        slot_btn->setText(crash ? u"Add the missing picture slots"_s : u"Make the window tall enough for every row"_s);
    }
    _fill_frames(k, mx);
    _fill_lib();
    {   // the levels: pictures (one per level from 0), the highest level, the technologies' levels — equal
        long long by_tech = 0;
        for (const auto &u : pb::unlocks(tech_files(), k))
            by_tech += u.adds;
        QStringList bad;
        if (frames_n > 0 && frames_n != mx + 1)
            bad << (frames_n > mx + 1
                        ? u"Too many pictures: %1 pictures, but the building has levels 0–%2 (%3 pictures). Delete "
                          u"%4 (right-click a picture), or raise the highest level."_s.arg(frames_n).arg(mx).arg(mx + 1).arg(
                              frames_n - mx - 1)
                        : u"Too few pictures: %1 pictures, but the building has levels 0–%2 (%3 pictures). Add %4 with "
                          u"＋, or lower the highest level."_s.arg(frames_n).arg(mx).arg(mx + 1).arg(mx + 1 - frames_n));
        if (by_tech > mx)
            bad << u"Too many levels by technology: the technologies unlock up to level %1, but the highest level is "
                   u"%2. Take a technology's level away (✕ or fewer +1), or raise the highest level."_s.arg(by_tech).arg(mx);
        else if (by_tech < mx)
            bad << u"Too few levels by technology: the technologies unlock up to level %1, but the highest level is %2 — "
                   u"level %3 can't be built. Add a technology for each (“A technology unlocks a level…”), or lower "
                   u"the highest level."_s.arg(by_tech).arg(mx).arg(by_tech + 1 == mx ? QString::number(mx)
                                                                                        : u"%1–%2"_s.arg(by_tech + 1).arg(mx));
        lvl_err->setText(u"⚠ "_s + bad.join(u"\n⚠ "_s));
        lvl_err->setVisible(!bad.isEmpty());
    }

    // the provinces that start with it
    prov_note->setText(u"%1 province(s) start with it. To give a province a level of it: Provinces & states → "
                       u"Provinces, click the province, then − / ＋ in its “%2” row (written into the province's "
                       u"history file as %3 = N, like forts and railroads)."_s
                           .arg(holders(k))
                           .arg(_name(k), k));

    // remove
    const long long h = holders(k);
    del_btn->setVisible(mine);
    del_note->setVisible(mine);
    del_btn->setEnabled(mine && h == 0 && us.empty());
    del_note->setText(h || !us.empty()
                          ? u"To remove it, first: %1%2%3."_s.arg(
                                h ? u"no province may start with it (%1 do)"_s.arg(h) : QString(),
                                h && !us.empty() ? u", "_s : QString(),
                                !us.empty() ? u"no technology may unlock it (✕ above)"_s : QString())
                          : u"The province window follows: the rows after it move up and the window gets a row shorter. "
                            u"Its picture slot stays in the window file, unused (an extra slot: no harm seen, not "
                            u"tested on its own)."_s);
    _filling = false;
}

// ---------------------------------------------------------------------------------------------- the province window
void BuildTypesPanel::_fill_window()
{
    if (!doc || !doc->maps.contains(u"scripts"_s))
        return;
    auto &store = doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const QString gui = text(gui_rel());
    const pb::Window w = pb::read_window(gui);
    const QStringList ks = keys();
    _filling = true;
    row_h->setValue(pb::source_of(gui).pref);
    _filling = false;
    if (!w.found) {
        win_view->set(QImage(), {}, QString());
        win_note->setText(u"The province window can't be read: "_s + w.why);
        win_note->setStyleSheet(RED);
        return;
    }
    // the game's look, with what the editor has not written yet (new strips, the fitted background, the new .gfx)
    locwin::GameLook look(mod, &store);
    if (doc->maps.contains(u"assets"_s))
        for (const auto &[rel, a] : doc->map<moreviews::AssetEdits>(u"assets"_s).fresh)
            if (!a.img.isNull())
                look.fresh[py::lower(rel)] = a.img;
    look.more_gfx << GFX;
    QImage img;
    double x0 = 0, y0 = 0;
    try {
        // a province of one's own: the header, its statistics, the buildings part (not the focus window, the colony
        // or the foreign province's parts that the game shows in their place)
        const shortcuts::Projected pr = shortcuts::project(
            look, gui_rel(), u"province_view"_s, {u"province_statistics"_s, u"province_buildings"_s}, true, false, 0, {},
            {u"national_focus_window"_s, u"province_colony"_s, u"province_other"_s});
        if (pr.img && pr.meta) {
            img = pr.img->convertToFormat(QImage::Format_ARGB32);
            x0 = pr.meta->x0;
            y0 = pr.meta->y0;
        }
    } catch (...) {
        img = QImage();
    }
    std::vector<WindowView::Row> rows;
    if (!img.isNull()) {
        QPainter p(&img);
        const OrderedMap<QString, QString> &loc = mod.loc;
        const QString *ex = loc.get(py::lower(w.expand_text));
        const QString expand = ex ? *ex : (loc.get(w.expand_text) ? *loc.get(w.expand_text) : u"Expand"_s);
        const std::optional<QImage> button = look.sprite(w.expand_sprite);
        for (int i = 0; i < ks.size(); ++i) {
            const double rx = w.part_x - x0, ry = w.part_y + double(i) * w.step - y0;
            // as the game: in the file's order, the row's own slot of the first three, every slot from 3 on (parked
            // ones fall outside the picture; the rows' pictures from the 4th on are in the background)
            for (const QString &nm : w.icons) {
                bool ok = false;
                const int j = nm.mid(10).toInt(&ok);
                if (!ok || j >= w.slot_sprites.size() || w.slot_sprites[j].isEmpty() ||
                    (j < pb::LIVE_SLOTS && j != i))
                    continue;
                if (const std::optional<QImage> ic = look.sprite(w.slot_sprites[j]))
                    p.drawImage(QPointF(rx + w.slot_x[j], ry + w.slot_y[j]), *ic);
            }
            try {
                gamefont::draw(p, look.fonts, w.name_font, _name(ks[i]), rx + w.name_x, ry + w.name_y, w.name_w, 16);
                if (button) {
                    p.drawImage(QPointF(rx + w.expand_x, ry + w.expand_y), *button);
                    gamefont::draw(p, look.fonts, w.expand_font, expand, rx + w.expand_x,
                                   ry + w.expand_y + (button->height() - 16) / 2.0, button->width(), 16, u"centre"_s);
                }
            } catch (...) {
            }
            rows.push_back({ks[i], QRectF(rx, ry, w.row_w, w.step)});
        }
        p.end();
    }
    win_view->set(img, rows, cur.value_or(QString()));
    // what it comes to
    const pb::Fit f = pb::plan_fit(w, static_cast<int>(ks.size()), pb::source_of(gui).pref);
    QString t;
    bool bad = false;
    if (ks.size() > w.fits) {
        t = u"⚠ No crash, but it won't look right: the window has room for %1 building rows and there are %2 buildings, "
            u"so the last row lies on top of the army / navy buttons (seen in the game). Lower the rows (Row height) "
            u"or press “Make the window tall enough for every row” in the building's part below."_s
                .arg(w.fits)
                .arg(ks.size());
        bad = true;
    } else {
        t = u"%1 rows of %2 px, the window %3 px tall"_s.arg(ks.size()).arg(w.step).arg(w.view_h);
        const int top = pb::SCREEN_H + w.view_y;
        t += top >= pb::TOP_BAR ? u"; on a 1024 × 768 screen it ends %1 px below the top bar."_s.arg(top - pb::TOP_BAR)
                                : u"; on a 1024 × 768 screen it reaches %1 px into the top bar."_s.arg(pb::TOP_BAR - top);
        if (w.fits > ks.size())
            t += u" Room for %1 rows, %2 empty."_s.arg(w.fits).arg(w.fits - ks.size());
        if (f.step != w.step || f.delta != 0)
            t += u" Fitted now it would be: "_s + f.words + u'.';
    }
    if (ks.size() > pb::LIVE_SLOTS)
        t += u" Rows 4 and later: the game can change the picture by level only in the first three rows (tested in "
             u"the game). For the later rows the editor paints each building's level-1 picture into the window's "
             u"background, so those rows show the same picture at every level; the number box beside it still shows "
             u"the level."_s;
    win_note->setText(t);
    win_note->setStyleSheet(bad ? u"QLabel { color: %1; }"_s.arg(theme::WARN) : QString());
}

// ---------------------------------------------------------------------------------------------- WindowView
WindowView::WindowView(QWidget *parent) : QWidget(parent)
{
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setMouseTracking(true);
    setMinimumHeight(60);
}

void WindowView::set(const QImage &img_, const std::vector<Row> &rows_, const QString &sel_)
{
    img = img_;
    rows = rows_;
    sel = sel_;
    drag = drop = -1;
    updateGeometry();
    update();
}

double WindowView::scale() const
{
    if (img.isNull() || width() <= 0)
        return 1.0;
    return std::min(1.0, double(width()) / img.width());
}

int WindowView::heightForWidth(int w) const
{
    if (img.isNull())
        return 60;
    return std::max(60, int(std::ceil(img.height() * std::min(1.0, double(w) / img.width()))));
}

QSize WindowView::sizeHint() const { return img.isNull() ? QSize(300, 60) : img.size(); }

int WindowView::row_at(QPointF pos) const
{
    const double s = scale(), ox = (width() - img.width() * s) / 2;
    const QPointF q((pos.x() - ox) / s, pos.y() / s);
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].r.contains(q))
            return static_cast<int>(i);
    return -1;
}

int WindowView::drop_at(QPointF pos) const
{
    if (rows.empty())
        return -1;
    const double s = scale();
    const double y = pos.y() / s;
    const double top = rows.front().r.top(), h = rows.front().r.height();
    return std::clamp(static_cast<int>(std::floor((y - top) / std::max(1.0, h))), 0, static_cast<int>(rows.size()) - 1);
}

void WindowView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (img.isNull()) {
        p.setPen(QColor(theme::INK_FAINT));
        p.drawText(rect(), Qt::AlignCenter, u"(the province window can't be drawn)"_s);
        return;
    }
    const double s = scale(), ox = (width() - img.width() * s) / 2;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(ox, 0);
    p.scale(s, s);
    p.drawImage(0, 0, img);
    // the picked building's row: a thick gold frame (the editor's own mark)
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].key == sel) {
            p.setPen(QPen(QColor(theme::GOLD), 3.0 / s));
            p.setBrush(Qt::NoBrush);
            p.drawRect(rows[i].r.adjusted(1, 1, -1, -1));
        }
    // dragging: the row lifted (dimmed in place), a thick gold line where it would go
    if (drag >= 0 && drop >= 0 && drop != drag) {
        p.fillRect(rows[size_t(drag)].r, QColor(0, 0, 0, 110));
        const QRectF &t = rows[size_t(drop)].r;
        const double y = drop > drag ? t.bottom() : t.top();
        p.setPen(QPen(QColor(theme::GOLD), 5.0 / s));
        p.drawLine(QPointF(t.left() + 4, y), QPointF(t.right() - 4, y));
    }
}

void WindowView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    drag = row_at(e->position());
    drop = drag;
    press = e->position();
    if (drag >= 0) {
        sel = rows[size_t(drag)].key;
        update();
        Q_EMIT picked(sel);
    }
}

void WindowView::mouseMoveEvent(QMouseEvent *e)
{
    if (drag < 0 || !(e->buttons() & Qt::LeftButton)) {
        setCursor(row_at(e->position()) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    setCursor(Qt::ClosedHandCursor);
    const int d = drop_at(e->position());
    if (d != drop) {
        drop = d;
        update();
    }
}

void WindowView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || drag < 0)
        return;
    const int from = drag, to = drop;
    drag = drop = -1;
    setCursor(Qt::OpenHandCursor);
    update();
    if (to >= 0 && to != from)
        Q_EMIT moved(rows[size_t(from)].key, to);
}

}  // namespace buildtypes
